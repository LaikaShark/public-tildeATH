# ~ATH language specification

the dialect of `~ATH` this compiler accepts and the runtime semantics it
implements. this document is the source of truth: when the implementation
and this document disagree, one of them is a bug

---

## 1. Language model

control flow is driven by object *liveness*. an **object** is a heap record
carrying an alive bit, two halves, an optional numeric payload, and optional
lifetime conditions (§4.7). death is **one-way**: once `alive` becomes false
it never returns

surface forms:

- `import NAME... V;` — allocate an object, bind `V`
- `BIFURCATE V[L,R];` / `BIFURCATE [L,R] V;` — decompose / compose
- `~ATH(V) { ... }` — run the body while `V` is alive (`~ATH(!V)` while dead)
- `V.DIE();` — kill `V`; `THIS.DIE();` returns from the activation
- `print TEXT;` — write to stdout

a program is a flat sequence of statements (§3) executed top to bottom in a
single activation. functions (`importf`) add further activations, each with
its own environment (§4.2). there are no run-time errors (§6.2)

---

## 2. Lexical structure

### 2.1 Whitespace and comments

whitespace (space, tab, CR, LF) separates tokens and is otherwise
insignificant, **except** inside a `print` statement's text payload (§2.4)

- `// ... <LF>` — line comment, from `//` to the next line feed
- `/* ... */` — block comment, **not nestable**: a `/*` inside a block
  comment is part of the comment

### 2.2 Tokens

```
KEYWORD     := 'import' | 'importf' | 'as' | 'watch' | 'BIFURCATE'
              | 'print' | 'INPUT' | 'EXECUTE'
              | 'BRANCH' | 'ELSE' | 'CLONE'
              | 'sleep' | 'TIMER'
              | 'read' | 'write' | 'append' | 'close'
              | 'text' | 'loop' | 'every'                   [matched case-insensitively]
LOOPSTART   := '~ATH'                          [the 'ATH' part is case-insensitive]
DIE         := '.DIE'                          [the 'DIE' part is case-insensitive]
IDENT       := [A-Za-z_][A-Za-z0-9_]*          [case-sensitive]
INT         := '-'? [0-9]+                     [integer literal, §4.8; one
                                                that exceeds int64 is a BIGINT
                                                (bignum, §4.8.7)]
FLOAT       := '-'? [0-9]+ ('.' [0-9]+)? ([eE] [+-]? [0-9]+)?
                                               [IEEE-754 double; must have a
                                                fraction or exponent, §4.8]
STRING      := '"' (any char except '"')* '"'  [escapes \" \\ \n \t \r, §2.3]
PUNCT       := '(' | ')' | '[' | ']' | '{' | '}' | '<' | '>' | ',' | ';' | '!'
```

**contextual markers.** these bare identifiers are recognized only at
specific parser positions and are ordinary `IDENT` tokens elsewhere; matching
is case-insensitive in the marker position:

- `builtin` and `number` as the second token after `import` (§4.4.12, §4.4.13)
- `signal`, `pid`, and `mtime` as the second token after `watch` (§4.4.11)
- `to` between source ident and destination string in `write` / `append`
  (§4.4.21, §4.4.22)

**angle brackets.** `<` and `>` tokenize as PUNCT and appear only in the
search-path form of `importf` (§4.4.8)

**two-character punctuation.** the lexer recognizes one multi-char operator,
`..` (DOTDOT, §4.4.15). on `.`: a following `.` yields `DOTDOT`; otherwise the
`.` begins a `.DIE` method token. a lone `.` followed by anything other than
`die` or `.` is a lexical error

a `.` **inside a number** is disambiguated by one character of lookahead:
while scanning digits, a `.` before a digit opens a FLOAT fractional part, a
`.` before another `.` ends the number (`1..3` is `INT DOTDOT INT`), and a `.`
before a non-digit ends it (`3.die` is `INT` then `.DIE`). an `e`/`E` is an
exponent only when a digit (after an optional sign) follows; otherwise it
starts the next identifier (`1exit` is `INT` then `exit`)

**case sensitivity.** keywords, `~ATH`, `.DIE`, and function names match
**case-insensitively**: `IMPORT`/`Import`/`import` are one keyword,
`~ath`/`~ATH` one loop-start, `.die`/`.DIE` one method token. identifiers are
**case-sensitive**: `Foo`, `foo`, `FOO` are three variables

**reserved words** (no case variant may be an identifier): `import`,
`importf`, `as`, `watch`, `BIFURCATE`, `print`, `INPUT`, `EXECUTE`, `BRANCH`,
`ELSE`, `CLONE`, `sleep`, `TIMER`, `read`, `write`, `append`, `close`,
`text`, `loop`, `every`

`THIS` and `NULL` are predefined *identifiers* (§4.2), not reserved words —
they follow the case-sensitive rule, so `this`, `Null`, etc. are distinct,
unbound, legal identifiers

tokenization of an identifier-or-keyword uses maximal munch on
`[A-Za-z_][A-Za-z0-9_]*`; the chunk is then compared case-insensitively
against the reserved-word set. hence `printer` is an identifier, not `print`
followed by `er`. `~ATH` is one token; the tilde is significant

### 2.3 String literals

a double-quoted `"..."` is a `STRING` token: a byte sequence terminated by the
next `"`. embedded newlines are part of the string; the body may be empty. it
recognizes five escapes:

| Source | Decoded |
|--------|---------|
| `\"`   | `"` (U+0022) |
| `\\`   | `\` (U+005C) |
| `\n`   | line feed (U+000A) |
| `\t`   | tab (U+0009) |
| `\r`   | carriage return (U+000D) |

a backslash before any other character (including end-of-input) is a
compile-time lexical error; the diagnostic reports the backslash position and
the recognized set

`STRING` names file paths in `importf` (§4.4.8), `watch` (§4.4.11), `read`
(§4.4.20), `write`/`append` (§4.4.21, §4.4.22), and supplies literal values
to `text` (§4.4.24). consumers see the decoded bytes; the source `\X` form is
not retained

### 2.4 `print` payload

after the keyword `print`, the lexer enters a one-shot raw mode:

1. consume exactly one ASCII space (`U+0020`); it is an error if the next
   character is not a space
2. capture every subsequent character (newlines, brackets, anything) up to —
   but not including — the first unescaped `;`, splitting it into an ordered
   run of **parts**:
   - a maximal run of literal characters → a `RAWTEXT` part
   - a `$` immediately followed by an identifier-start character (`[A-Za-z_]`)
     → an **interpolation** part: the lexer reads the following
     `[A-Za-z_][A-Za-z0-9_]*` as a case-sensitive variable name and emits a
     `PRINTVAR` part (§4.4.6)
3. the payload may be empty (zero parts)
4. the `;` is consumed as a normal token

literal runs decode these escapes:

| Source | Decoded |
|--------|---------|
| `\;`   | `;` (U+003B) |
| `\\`   | `\` (U+005C) |
| `\n`   | line feed (U+000A) |
| `\t`   | tab (U+0009) |
| `\r`   | carriage return (U+000D) |
| `\$`   | `$` (U+0024) |

a backslash before any other character is a compile-time lexical error. a
literal `;` requires `\;`; a literal `\` requires `\\`; a literal `$` requires
`\$` — a `$` not followed by an identifier-start character is a compile-time
lexical error, so the interpolation marker is never ambiguous

---

## 3. Grammar

EBNF. `*` is zero-or-more, `?` is optional

```
program       = statement* ;

statement     = import-stmt
              | importf-stmt
              | watch-stmt
              | bifurcate-stmt
              | ath-loop
              | die-stmt
              | print-stmt
              | input-stmt
              | funcall-stmt
              | subscript-stmt
              | slice-stmt
              | branch-stmt
              | clone-stmt
              | sleep-stmt
              | timer-stmt
              | read-stmt
              | write-stmt
              | append-stmt
              | close-stmt
              | text-stmt
              | loop-stmt
              | every-stmt
              | spawn-stmt
              | send-stmt
              | recv-stmt
              | yield-stmt
              | join-stmt
              | channel-stmt
              | universe-stmt
              | listen-stmt
              | accept-stmt
              | connect-stmt ;

import-stmt   = import-concept
              | import-builtin
              | import-number ;

import-concept
              = 'import' IDENT+ IDENT ';' ;
                (* at least TWO IDENTs: one or more metadata words (joined
                   with single spaces into the concept name) followed by the
                   variable bound. A bare `import VAR;` is a compile-time
                   error (§6.1). The first IDENT must not be the contextual
                   marker 'builtin' or 'number'. *)

import-builtin
              = 'import' 'builtin' IDENT 'as' IDENT ';' ;
                (* declares a C-ABI function. First IDENT is the C symbol
                   (case-sensitive); second is the registry name
                   (case-insensitive). §4.4.12. *)

import-number = 'import' 'number' (INT | FLOAT) 'as' IDENT ';' ;
                (* allocates an eternal-alive object carrying the literal's
                   payload — INT → int64 (or bignum, §4.8.7); FLOAT → double.
                   §4.4.13. *)

importf-stmt  = 'importf' STRING 'as' IDENT ';'
              | 'importf' '<' IDENT '>' 'as' IDENT ';' ;
                (* quoted form: path relative to the importing file.
                   angle form: name resolved against ATH_PATH (§5.4),
                   appending '.ath' to the bare identifier. *)

watch-stmt    = 'watch' STRING 'as' IDENT ';'              (* file form   *)
              | 'watch' 'signal' IDENT 'as' IDENT ';'      (* signal form *)
              | 'watch' 'pid' IDENT 'as' IDENT ';'         (* pid form    *)
              | 'watch' 'mtime' STRING 'as' IDENT ';' ;    (* mtime form  *)
                (* 'signal', 'pid', 'mtime' are contextual keywords: bare
                   IDENTs recognized only as the second token after 'watch'.
                   §4.4.11. *)

bifurcate-stmt
              = decompose-stmt
              | compose-stmt ;

decompose-stmt
              = 'BIFURCATE' IDENT '[' IDENT ',' IDENT ']' ';' ;

compose-stmt  = 'BIFURCATE' '[' IDENT ',' IDENT ']' IDENT ';' ;

ath-loop      = '~ATH' '(' [ '!' ] IDENT ')' '{' statement* '}'
                [ 'EXECUTE' '(' IDENT ')' ] ';'? ;

die-stmt      = IDENT '.DIE' '(' [ IDENT ] ')' ';' ;

print-stmt    = 'print' print-part* ';' ;
print-part    = RAWTEXT | PRINTVAR ;
                (* The lexer splits the raw payload (§2.4) into RAWTEXT
                   parts and `$VAR` PRINTVAR parts, emitted in order with
                   one trailing newline. Zero parts prints a blank line.
                   §4.4.6. *)

input-stmt    = 'INPUT' IDENT ';' ;

operand       = IDENT | INT | FLOAT | STRING ;
                (* A read-operand is a bound name or an inline literal. A
                   literal materializes a fresh object at the call site,
                   equivalent to introducing it with import-number / a string
                   first. Only read positions accept literals; write targets,
                   a funcall's function name, and a subscript/slice source
                   stay IDENT. §4.4. *)

funcall-stmt  = IDENT '[' operand ',' operand ']' IDENT ';'    (* compose-arg form *)
              | IDENT operand '[' IDENT ',' IDENT ']' ';' ;    (* decompose-result form *)

subscript-stmt
              = IDENT '[' operand ']' IDENT ';' ;              (* S[N] X; *)

slice-stmt    = IDENT '[' operand '..' operand ']' IDENT ';' ; (* S[I..J] X; *)

branch-stmt   = 'BRANCH' '(' [ '!' ] IDENT ')'
                '{' statement* '}'
                [ [ 'ELSE' ] '{' statement* '}' ] ;
                (* One-shot dispatch. After whichever body runs (or after the
                   skipped dispatch when V is dead and no else clause is
                   present), the runtime kills V. ELSE is optional sugar
                   before the second block. §4.4.16. *)

clone-stmt    = 'CLONE' IDENT 'as' IDENT ';' ;
                (* Shallow snapshot: copies V's alive bit, halves, payload,
                   and lifetime extensions, but not V's dep chain. Killing
                   one does not kill the other. §4.4.17. *)

sleep-stmt    = 'sleep' IDENT ';' ;
                (* Block the activation for IDENT.value ms if IDENT carries a
                   payload and is alive; else no-op. §4.4.18. *)

timer-stmt    = 'TIMER' IDENT 'as' IDENT ';' ;
                (* Bind the second IDENT to a fresh alive object deadlined
                   IDENT.value ms from now. The duration is a parameter, not
                   a dependency. §4.4.19. *)

read-stmt     = 'read' STRING 'as' IDENT ';' ;
                (* Slurp the file as a string-cons-list. The result owns the
                   file: explicit .DIE() or BRANCH consumption deletes it.
                   §4.4.20. *)

write-stmt    = 'write' IDENT 'to' STRING [ 'as' IDENT ] ';' ;
                (* Truncate-and-write the source string to the path. The
                   optional 'as' clause binds a verdict alive iff the write
                   succeeded. 'to' is a contextual marker. §4.4.21. *)

append-stmt   = 'append' IDENT 'to' STRING [ 'as' IDENT ] ';' ;
                (* Like write-stmt but appends. §4.4.22. *)

close-stmt    = 'close' IDENT ';' ;
                (* Clear owns_path and kill IDENT; the file persists. §4.4.23. *)

text-stmt     = 'text' text-part+ 'as' IDENT ';' ;
text-part     = STRING | IDENT ;
                (* Build a string-cons-list (§4.6) from parts and bind it.
                   STRING parts contribute decoded bytes; IDENT parts are
                   read and coerced (payload-bearing operands route through
                   TO_STRING; existing cons-lists pass through). Folded left
                   to right with CONCAT. §4.4.24. *)

loop-stmt     = 'loop' IDENT '{' statement* '}' ;
                (* Run the body exactly IDENT.value times (count snapshotted
                   on entry; dead/payload-less/negative runs zero times). Not
                   a liveness loop. §4.4.25. *)

every-stmt    = 'every' IDENT '{' statement* '}' ;
                (* Run the body, sleep IDENT.value ms (re-read each pass),
                   repeat forever. Only exits are THIS.DIE() or process
                   death. §4.4.26. *)

spawn-stmt    = 'spawn' IDENT operand [ 'into' IDENT ] 'as' IDENT ';' ;
                (* Start an actor running importf function IDENT with the
                   composed operand; bind a live handle. 'into' scopes it to a
                   universe. 'into' is a contextual marker. §7. *)

send-stmt     = 'send' operand 'to' IDENT ';' ;
                (* FIFO-enqueue the operand onto an actor/channel mailbox;
                   non-blocking. 'to' is a contextual marker. §7. *)

recv-stmt     = 'recv' [ 'from' IDENT ] 'as' IDENT ';' ;
                (* Dequeue a message, yielding until one arrives. Without
                   'from', the running actor's own mailbox; with 'from', the
                   named channel/handle. 'from' is a contextual marker. §7. *)

yield-stmt    = 'yield' ';' ;
                (* Cooperative yield to the scheduler. §7. *)

join-stmt     = 'join' IDENT ';' ;
                (* Drive the scheduler until IDENT (actor or universe) dies.
                   'join' is a soft keyword (a stdlib function name), so this
                   shape — IDENT IDENT ';' — is what selects it. §7. *)

channel-stmt  = 'channel' 'as' IDENT ';' ;
                (* Bind a fresh closeable channel. §7. *)

universe-stmt = 'universe' 'as' IDENT ';' ;
                (* Bind a fresh universe (supervision scope). 'universe' is a
                   soft keyword — it also names a lifetime-library concept
                   (§5.3) — selected by the 'universe' 'as' IDENT shape. §7. *)

listen-stmt   = 'listen' ( operand | STRING ) 'as' IDENT ';' ;
                (* Bind a listening socket. An operand is a TCP port; a STRING
                   must be "unix:/path" for a Unix-domain socket. §7.6. *)

accept-stmt   = 'accept' 'from' IDENT 'as' IDENT ';' ;
                (* Accept one connection from a listener, binding a connection
                   handle. 'from' is a contextual marker. §7.6. *)

connect-stmt  = 'connect' STRING [ operand ] 'as' IDENT ';' ;
                (* Open a connection. "unix:/path" host takes no port; any other
                   host is TCP and requires a port operand. §7.6. *)

Notes:

- `~ATH` followed by anything other than `(` is a syntax error.
- A statement may not appear outside a `program` or `ath-loop` body.
- The two `BIFURCATE` forms are distinguished by the token following
  `BIFURCATE`: an `IDENT` selects decompose; a `[` selects compose.
- An `IDENT`-starting statement is disambiguated by the next token, and
  (for the bracket forms) by what appears between the brackets:
  - `.DIE` → die-stmt.
  - `[` followed by `IDENT ',' IDENT ']'` → funcall compose-arg.
  - `[` followed by `IDENT ']'` → subscript-stmt (single bracket
    contents, no comma, no `..`).
  - `[` followed by `IDENT '..' IDENT ']'` → slice-stmt.
  - `IDENT` → funcall decompose-result form.
```

---

## 4. Semantics

### 4.1 Object model

an **object** is a heap-allocated record:

```
ath_obj { alive: bool, left: ath_obj* | UNSET, right: ath_obj* | UNSET }
```

- `alive` is `true` for newly allocated objects, except `NULL` (§4.2), which
  is born dead
- `left` and `right` are initially `UNSET`, set together on first
  decomposition (§4.4)
- once `alive` transitions to false it never returns; setting `left`/`right`
  is one-way

### 4.2 Initial environment

each function activation (including the top-level program's "main"
activation) starts with a fresh local environment:

| Name   | Object                                                                 |
|--------|------------------------------------------------------------------------|
| `THIS` | a fresh alive object for this activation; killing it returns from the function (or, for main, terminates the program) |
| `NULL` | a globally-shared object with `alive = false`                          |
| `ARGS` | (function activations only) the object passed by the caller per §4.4.9/§4.4.10; not defined in main |

these bindings use exactly `THIS` and `NULL` (uppercase); `this`, `Null`,
etc. are unbound until a program introduces them

`NULL` is **read-only**: any statement that would rebind it (as an `import`
target, a `BIFURCATE` decompose output, or a compose target) is a
compile-time error. `THIS` is rebindable

`NULL` is also the **behavioral identity for unbound names at runtime**: if
the syntactic in-scope check (§6.1) admits a read of `V` but no introduction
has run for `V` at execution time, the read yields `NULL`. every statement in
§4.4 is defined when its source operand is `NULL`

### 4.3 Variables and objects

a variable is a name in the (single global) environment pointing to exactly
one object at a time. distinct variables may point to the same object;
rebinding a variable does not mutate the object it formerly pointed to.
reading a variable always reads its current binding

### 4.4 Statement semantics

#### 4.4.1 `import NAME... VAR;` (concept form)

two or more IDENTs follow `import`. the **last** is `VAR`; the preceding
one-or-more are metadata, joined by single spaces into a "concept name." a
bare `import VAR;` is a compile-time error (§6.1)

the first IDENT must not be `builtin` or `number` (case-insensitive), which
dispatch to §4.4.12 and §4.4.13. to bind a variable to a concept literally
named "builtin"/"number," prefix another metadata word (`import the
builtin B;`)

- if `VAR` is already bound: no-op
- otherwise: look the concept name up case-insensitively in the lifetime
  library (§5.3). a match allocates a fresh object whose lifetime is sampled
  uniformly from the entry's `[min_s, max_s]` range (§4.7); a miss allocates a
  plain alive object

`VAR` is then bound to the result. the concept name is matched in full:
`import fly F;` matches `fly`, but `import dead fly F;` looks up `dead fly`
and falls through to plain alive. reusing a concept name across variables is
allowed and not diagnosed — the name is a library selector, not a unique key

#### 4.4.2 `BIFURCATE V[L, R];` (decompose)

1. read `V`'s current binding, the object `o`
2. if `o.left == UNSET` (equivalently `o.right == UNSET` — always set
   together): allocate two fresh alive objects `lo`, `ro`; atomically set
   `o.left = lo`, `o.right = ro`
3. let `(lo, ro) = (o.left, o.right)`
4. bind `L := lo` and `R := ro`

reads (steps 1–3) precede writes (step 4). if `L == R` literally, the second
write wins

**NULL and dead sources.** step 2's test is pointer identity against the
`NULL` singleton (§4.2), not a liveness check:

- if `o` is `NULL` (V unbound or bound to the dead singleton), both `L` and
  `R` bind to `NULL`; no halves are allocated. this is the only outcome that
  yields dead children
- if `o` is a real **dead** object (e.g. killed via `.DIE()`, but not the
  `NULL` singleton), decompose proceeds normally: on first decomposition it
  allocates two **fresh alive** halves and caches them, returning those
  thereafter. liveness does not propagate downward through decomposition;
  only the `NULL` singleton is barren

#### 4.4.3 `BIFURCATE [L, R] V;` (compose)

1. read `L`'s and `R`'s bindings, `lo` and `ro`
2. call `ath_compose(lo, ro)`; bind `V` to the result

`fresh` mode (§5.2, default): `ath_compose` allocates a new alive object with
`left = lo`, `right = ro`. two composes with structurally identical operands
produce two distinct objects

`intern` mode (§5.2, `--compose intern`): `ath_compose` consults a hash-cons
table keyed by the raw pointer pair `(lo, ro)`. a previously composed object
with those exact operand pointers is returned (alive or dead); otherwise a
new alive object is allocated and inserted. killing a composite kills every
variable that observed it; structurally equal composites share storage
forever (no eviction)

the two modes are observationally identical for any program that does not
rely on the distinctness or shared identity of composites; the difference
shows only when the same operand pair is composed twice and one result is
killed

#### 4.4.4 `~ATH(V) { S* }` and `~ATH(!V) { S* }`

```
loop:
  alive := ath_is_alive(env[V])
  if not alive (or alive, if the '!' form): goto end_loop
  execute S*
  goto loop
end_loop:
```

the check re-reads `V` every iteration; rebinding `V` in the body changes
what is watched. an empty body (`{}`) loops forever if the initial check
passes

the `!V` form inverts the condition: the body runs while `V` is **dead**.
since death is one-way, `~ATH(!V)` runs at most once (if `V` is dead at
entry) then exits, and never runs if `V` is alive at entry

an optional `EXECUTE(IDENT)` postfix may follow the closing `}`:

```
~ATH(V) { S* } EXECUTE(F);
```

`F` is a function invoked once the loop exits **by its condition**. on exit:

1. `V` is read — dead for a normal loop, alive for an inverted one
2. `F` is called with `V` as its single argument, resolved like any function
   call (§4.4.12): a user function (`importf`) receives `V` as its composed
   argument; a builtin (`import builtin`) is called as `F(V, NULL)`. the
   result is discarded

`EXECUTE(NULL)` is the no-op: `NULL` is the predefined empty object, not a
function, so no call is emitted. any other `IDENT` must be a declared
function (an undeclared name is rejected as for a bad function call)

`EXECUTE` fires **only on the condition-false exit**. a `THIS.DIE()` in the
body returns from the enclosing function before reaching the exit, so `F`
does not run. the loop never entering (subject dead at entry) still counts as
a condition-false exit, so `F` runs

with an `EXECUTE` postfix the construct terminates with `;`; without it the
closing `}` is the terminator

**variable scope.** there is **no lexical block scope**. names are flat per
activation: a variable first bound inside a loop body (or `BRANCH` arm) is
visible everywhere after that point in the same activation, including after
the loop. every variable in a function gets one slot, allocated and
zero-initialized in the prologue (§4.5). a binding created only on an
unexecuted path (e.g. a `~ATH(!V)` body that never runs) leaves the slot at
its initial value, so a later read observes `NULL` — defined behavior per the
null-safety contract (§5.1). activations are isolated; this flat scope is
within a single activation

#### 4.4.5 `V.DIE();` and `V.DIE(RET);`

1. if a `RET` argument is given, read its binding and update the current
   activation's **pending return value** to point to that object
2. read `V`'s binding, the object `o`
3. if `V` is the name `THIS`: return from the current activation immediately;
   no subsequent statement runs. the caller receives the activation's pending
   return value (defaulting to `NULL`). for main, the pending return value is
   discarded and the program terminates with OS exit code 0
4. otherwise: set `o.alive = false`. this affects only `o` — its halves, any
   composites it is part of, and other aliases observe `o` as dead, but no
   other object is modified
5. **if `o` was alive, `o.owns_path` is set, and `o.watch_path` is not
   NULL**, the runtime calls `unlink(o.watch_path)` before flipping `o.alive`
   to false; `unlink` errors are silently ignored. this is the only path that
   deletes files (§4.7 ext 5, §4.7.1). passive deaths do not unlink.
   `close VAR;` (§4.4.23) sidesteps step 5 by clearing `owns_path` first

step 1 precedes step 3, so `THIS.DIE(THIS);` returns the activation's own
THIS object. killing an already-dead object is a no-op

#### 4.4.6 `print PAYLOAD;`

write `PAYLOAD` to standard output followed by **exactly one** line feed
(`U+000A`) for the whole statement. each part (§2.4) is emitted in source
order with no inter-part separator:

- a **literal part** contributes its already-decoded bytes
- an **interpolation part** `$VAR` reads `VAR`'s binding. if `VAR` carries a
  **numeric payload** (§4.8) it renders as its decimal form — exactly what
  `TO_STRING` produces (signed decimal for an integer, shortest round-trip
  for a float, or `nan`/`inf`). otherwise `VAR` is walked as a string (§4.6):
  decompose each cell, write the recognized character atom, stop on a dead
  object, `NULL`, or the first unrecognized left half. a dead, `NULL`, or
  non-string-non-number `VAR` contributes nothing (or a truncated prefix); it
  never aborts the statement or crashes (§6.2)

a zero-part payload (`print ;`) writes just the trailing line feed

`$VAR` is a **read position**: an unbound interpolation variable is a
compile-time error (§6.1)

#### 4.4.7 `INPUT VAR;`

1. read one line from standard input; the trailing `U+000A`, and a preceding
   `U+000D` if present, are stripped
2. on end-of-file or read error, treat the line as empty
3. encode the line as a string (§4.6)
4. bind `VAR`

`VAR` is a write target: it must not be `NULL` (§4.2). lines longer than the
input buffer (≥ 4096 bytes) are split across successive calls

#### 4.4.8 `importf "PATH" as NAME;` and `importf <STEM> as NAME;`

a **compile-time directive**, not a runtime operation. two forms,
distinguished by the token following `importf`

**Quoted form** — `importf "PATH" as NAME;`:

1. open the file at `PATH`, resolved relative to the directory of the file
   containing this statement
2. parse its contents as a Program (§3) and register it under `NAME`
   (case-insensitively)
3. emit no runtime code

**Search-path form** — `importf <STEM> as NAME;`:

1. form `STEM + ".ath"`; `STEM` is taken case-sensitively
2. resolve `STEM.ath` against `ATH_PATH` (§5.4): each colon-separated
   directory in order, then the compiler-adjacent `stdlib/` directory
3. the first existing file wins
4. then proceed as in the quoted form

a missing or unparseable file fails compilation. a registered function is
callable from any function in the compilation unit, including inside loops
and other functions

a function name binds to exactly one file. registering the same `NAME` for
two **different** resolved files is a compile-time error; registering the
same `NAME` for the **same** resolved file is idempotent (this makes diamond
imports legal). resolution is by canonical absolute path, so the quoted and
search-path forms agree whenever they name the same file. a registered
function is an ordinary user function unless its file contains
`import builtin` (§4.4.12), in which case it is a C-ABI shim

#### 4.4.9 `FN [L, R] V;` (function call, compose-argument form)

1. read `L` and `R`
2. compose them via `ath_compose(L, R)` to a single argument object
3. invoke `FN` (resolved case-insensitively against the registry from
   §4.4.8) with that argument; the call yields an object
4. bind `V` to that object

#### 4.4.10 `FN A [B, C];` (function call, decompose-result form)

1. read `A`
2. invoke `FN` with `A`; the call yields an object `o`
3. decompose `o` via `ath_decompose`, binding `B` to the left half and `C` to
   the right (§4.4.2 semantics, including lazy half allocation if `o` is a
   leaf)

in both forms, an unregistered `FN` fails compilation (§6.1)

#### 4.4.11 `watch` — file, signal, pid, and mtime forms

four forms, dispatched on the first token after `watch`

**File form** — `watch "PATH" as VAR;`:

allocates a fresh object whose liveness is tied to the existence of `PATH`,
resolved at runtime against the current working directory.

- exists at allocation → born alive; absent → born dead
- every subsequent `ath_is_alive` check calls `access(F_OK)`; a gone file
  transitions the object to dead permanently (recreation does not revive it)

**Signal form** — `watch signal NAME as VAR;`:

allocates a fresh object whose liveness is tied to a POSIX signal. the
runtime installs a sticky-flag handler for `NAME` (idempotent across
watchers) and the check consults the flag.

- `NAME` ∈ `SIGHUP`, `SIGINT`, `SIGQUIT`, `SIGUSR1`, `SIGUSR2`, `SIGPIPE`,
  `SIGALRM`, `SIGTERM`, `SIGCHLD` (case-insensitive); names outside this set
  produce a born-dead object and a stderr warning
- not yet received → born alive; already received → born dead (the flag is
  sticky). all watchers of the same signal die together

**Pid form** — `watch pid N as VAR;`:

allocates a fresh object whose liveness is tied to a running process; `N` is
a number-payload object holding the pid.

- born **dead** if `N` has no payload, is non-positive, exceeds `INT_MAX`, or
  names a process absent at allocation
- every check calls `kill(pid, 0)`; the object dies exactly when that reports
  `ESRCH` (process exited and reaped). `EPERM` (exists, not signalable) is
  not death; a zombie counts as alive until reaped

**Mtime form** — `watch mtime "PATH" as VAR;`:

allocates a fresh object whose liveness is tied to a file's modification time
(seconds + nanoseconds) captured at allocation.

- born **dead** if `PATH` is absent at allocation
- every check `stat()`s the path; the object dies once the mtime differs from
  the captured value or the file is gone (one-way; no revival)

**Shared rules.** `VAR` must not be `NULL`. if `VAR` is already bound,
`watch` is a no-op (idempotent, like `import`). `signal`, `pid`, `mtime` are
contextual keywords recognized only as the second token after `watch`. all
four forms are **monotonic** — file deletion, signal arrival, process exit,
and the first mtime change are permanent (§4.1)

#### 4.4.12 `import builtin SYM as NAME;`

a **compile-time directive** registering a C-ABI function under `NAME` in the
function registry (§4.4.8). `SYM` is the C symbol the linker resolves.

1. `SYM` must have signature `ath_obj *(ath_obj *, ath_obj *)`; the linker
   checks this, the compiler does not
2. `NAME` is added to the registry (case-insensitively); subsequent
   `NAME [L, R] V;` and `NAME A [L, R];` calls (§4.4.9, §4.4.10) emit a direct
   call to `SYM` instead of an `ath_user_*` thunk
3. the statement emits no runtime code at its source position
4. registration is file-scoped: each `NAME` is local to the declaring file;
   re-declaring the same `NAME` within one file is last-wins. distinct from
   `importf`'s whole-program binding (§4.4.8), which rejects a name bound to
   two different files

built-ins are typically declared in single-file shims under `stdlib/` and
brought in via `importf <name> as NAME;` (§4.4.8). a missing `SYM` is a
**link-time** error, not a compile-time one

#### 4.4.13 `import number N as VAR;`

a **runtime operation** allocating a fresh object with a numeric payload.

1. `N` is an `INT` or `FLOAT` token (§2.2). a fitting `INT` parses as signed
   int64; one exceeding int64 is a **bignum** literal (§4.8.7). a `FLOAT`
   parses as an IEEE-754 double; a non-finite literal (e.g. `1e999`) is a
   compile-time error
2. if `VAR` is already bound: no-op
3. otherwise allocate a fresh **eternal-alive** object whose payload is the
   literal's value — `num_kind = ATH_NUM_INT` (`num.i = N`) for an int64,
   `ATH_NUM_FLOAT` (`num.f = N`) for a float, `ATH_NUM_BIG` for an over-int64
   integer. no deadline, watch path, awaited signal, or one-shot flag
4. bind `VAR`

eternal-alive means the object outlives the program by default. a mortal
number composes with a mortal carrier:

```
import number 42 as N;
import mayfly M;          // dies in 5 min – 1 day
BIFURCATE [N, M] MORTAL;  // MORTAL is alive while M is alive
```

`VAR` must not be `NULL`

#### 4.4.14 `S[N] VAR;` (subscript / element access)

element access on any object treated as a right-nested cons-list. `S` is the
source, `N` a number-payload object holding the index, `VAR` receives the Nth
element of the right-spine walk.

1. read `S` and `N`
2. invoke `ath_index(S, N)` (§5.2)
3. bind `VAR`

`ath_index` walks `S` `n` steps right (following `right` halves `n` times),
then takes the `left` half. for strings (cons-lists of character atoms
terminated by `NULL`) this yields the Nth **character** — not a length-1
string; wrap with `BIFURCATE [VAR, NULL] STR;` (§4.4.3) to make a printable
single-char string

the result is a fresh **snapshot** of the element, not the element object: a
string's head is the canonical character atom (§4.6) shared by every string
containing that character, so `ath_index` clones the element (preserving
numeric payload and character identity) and installs dependencies on the
clone — the indexed result dies with `S` while the shared atom is untouched.
the snapshot is still recognized by `print $VAR` and `ORD`

the result is born dead if any of:

- `S` is dead, `NULL`, or unbound
- `N` is dead, lacks a payload, or is negative
- the walk hits `NULL` or a dead object before position `n` (out of range)

a born-dead failure result is a **fresh, payload-less, character-less dead
object** — not the `NULL` singleton. the distinction is observable: it exits
a `~ATH(VAR)` guard like any dead object and carries no number/character, but
because it is a real object, `BIFURCATE VAR[L,R];` yields two fresh alive
halves (§4.4.2), not dead ones. test "did the subscript land?" by `VAR`'s
liveness, not by decomposing it

the result inherits both `S` and `N` as dependencies (§4.8.1). subscripting
is not restricted to strings: for any cons-list shape, `S[N] X;` reads the
Nth right-spine head. `VAR` must not be `NULL`

#### 4.4.15 `S[I..J] VAR;` (range / slice)

right-spine slice. `S` is the source, `I` and `J` number-payload objects
holding the inclusive start and exclusive end, `VAR` receives a fresh
cons-list of `S[I..J-1]` terminated with `NULL`.

1. read `S`, `I`, `J`
2. emit `ath_compose(I, J)` to form a range pair, then
   `ath_inherit_lifetime(range_pair, I, J)` so the pair tracks both endpoints
   (§4.8.1)
3. invoke `ath_slice(S, range_pair)` (§5.2): the runtime decomposes the pair
   to recover `I` and `J`, walks `S` to position `I`, and accumulates `J - I`
   consecutive elements as a new right-nested composition terminated with
   `NULL`
4. bind `VAR`

born dead if:

- `S` is dead, `NULL`, or unbound
- `I` or `J` is dead, lacks a payload, or is negative
- `I > J` (empty-or-invalid range; an empty slice is also dead)
- the walk hits `NULL` or a dead object before position `J`

as with `S[N]`, a born-dead slice is a fresh, payload-less, non-`NULL` dead
object: test it by liveness, not by decomposing. the result inherits `S` and
the range pair as deps, and the range pair inherits `I` and `J`. for strings
`S[I..J]` is a substring; for a list of numbers a sublist. the slice's
right-spine terminator is always `NULL`. `VAR` must not be `NULL`

#### 4.4.16 `BRANCH(V) { ... } [ELSE] { ... }` (one-shot dispatch)

a non-looping conditional that dispatches on `V`'s liveness once, then
consumes `V`.

1. read `V`; compute `alive = ath_is_alive(V)` (or its negation for `!`)
2. if `alive`, execute the first body; otherwise execute the else body if
   present, else nothing
3. after dispatch, re-read `V` and invoke `ath_die(V)`

`BRANCH` is the direct sugar over `~ATH(V) { S* ; V.DIE(); }` extended with an
optional else block, evaluated exactly once

**V is consumed.** after a `BRANCH`, `V` is guaranteed dead — whether the
alive body ran (the kill happens) or the dead body ran (the kill is a no-op).
to check `V` without losing it, `CLONE V as VCHECK;` (§4.4.17) and
`BRANCH(VCHECK) { ... }` on the clone

if the body rebinds `V`, the post-dispatch `ath_die` reads the current
binding and kills that object. `THIS.DIE(...)` in a body terminates the
function (§4.4.5); the post-dispatch kill never runs in that case

the inverted form `BRANCH(!V)` swaps which body runs (first body when `V` is
dead, else body when alive); `V` is still consumed

#### 4.4.17 `CLONE V as W;`

allocates a fresh `W` that is a shallow snapshot of `V` at clone time. `W` and
`V` have **independent identity** — killing one does not affect the other.
`W` copies, field by field, from `V`:

- `alive` — set to `V`'s **currently observable** liveness via the pure
  `ath_observe` (the predicate `ath_is_alive` uses, without flipping bits or
  consuming one-shots). a clone of a verdict whose upstream operands have died
  is born dead. a one-shot clone is *not* born dead by cloning — `ath_observe`
  does not trip `is_oneshot`, so a one-shot not yet directly observed clones to
  a fresh, unfired one-shot
- `left`, `right` — pointer-copied (shared with `V`'s halves; the cons-list
  beneath is not deep-copied)
- `num_kind`, `num` — full numeric payload copy
- `deadline_s`, `watch_path`, `is_oneshot`, `awaiting_signal`, `dep_mode` —
  all lifetime extensions and the dep mode are copied, so the clone has the
  same intrinsic mortality (a `mayfly` clone dies at the same deadline; a
  file-watcher clone watches the same path; a `once` clone is itself a
  one-shot)

`W` does **not** copy `V`'s `dep1`/`dep2`. inherited mortality
(`ath_inherit_lifetime`) is not preserved across the clone — `W` is a
snapshot, independent of what `V` was tracking. killing one of `V`'s dep
sources after the clone kills `V` but not `W`. because `dep_mode` is copied
but deps are not, an OR-mode clone with no deps trusts its captured bit

`CLONE` is the canonical primitive for **non-destructive checking**:

```
CLONE V as VCHECK;
BRANCH(VCHECK) { ... } { ... }   // VCHECK is consumed; V is untouched
```

`W` must not be `NULL`. cloning `NULL` yields a fresh born-dead object
(alive=0, no payload, no halves)

#### 4.4.18 `sleep N;`

block the current activation for `N.value` milliseconds, then continue.

1. read `N`
2. if `N` is `NULL`, dead, lacks a payload, or is non-positive, return
   immediately (no-op)
3. otherwise sleep `N.value` ms using a monotonic clock (POSIX `nanosleep`;
   interrupted sleeps may return early). the spec guarantees only that the
   runtime does not block longer than `N.value` ms plus scheduler jitter

`sleep` produces no return value and does not consume `N`. `N` is read at the
start of the call; later mutations do not affect the in-progress sleep

#### 4.4.19 `TIMER N as T;`

bind `T` to a fresh alive object that becomes observably dead after `N.value`
milliseconds.

1. read `N`. if `N` is `NULL`, dead, lacks a payload, or is non-positive,
   allocate `T` born dead and return
2. otherwise allocate a fresh alive object with
   `deadline_s = ath_now_s() + N.value / 1000.0` (§4.7 deadline)
3. bind `T`

`T` is **independent of `N`** — no dependency is installed; the duration is a
parameter consumed at allocation, and the timer's death is governed solely by
the clock. `T` must not be `NULL`. `TIMER` + `~ATH(T)` is the bounded-loop
idiom:

```
import number 5000 as FIVE_SEC;     // 5000 ms = 5 seconds
TIMER FIVE_SEC as T;
~ATH(T) {
    print still running;
    sleep ONE_SEC;
}
print timed out;
```

#### 4.4.20 `read "PATH" as VAR;`

slurp the entire file at `PATH` into a string-cons-list (§4.6) and bind `VAR`.

1. the runtime opens `PATH` for reading (relative to the current working
   directory)
2. on any failure (absent, permission denied, I/O error), `VAR` is bound to a
   born-dead object
3. on success the bytes become a cons-list of character atoms (§4.6); the
   head is a freshly allocated wrapper (not interned even under
   `--compose intern`) carrying `watch_path = PATH`, `owns_path = 1` (§4.7),
   and the file's bytes as its right-spine
4. bind `VAR` to that wrapper

the wrapper observes the file's existence like `watch "PATH" as F;`
(§4.4.11): every `ath_is_alive(VAR)` runs `access(PATH, F_OK)` and flips dead
if the file is gone. because `owns_path` is set, killing the wrapper via
explicit `.DIE()` or BRANCH consumption **deletes the file** (`unlink(PATH)`;
§4.7). death from the watch-path check itself does not attempt deletion. to
release without deleting, use `close VAR;` (§4.4.23)

`owns_path` is not propagated by `BIFURCATE` composition or `CLONE`. derived
strings (subscript, slice, `CONCAT`) inherit only the watch_path-driven
lifetime through the dep machinery — they observe but do not own the file.
`VAR` must not be `NULL`

#### 4.4.21 `write SRC to "PATH" [as VERDICT];`

open `PATH` for writing (truncating), walk `SRC` as a string (§4.6), write
each character atom's byte, close.

1. read `SRC`. if `SRC` is `NULL` or dead, the file is created and left empty
2. walk `SRC`'s right-spine to `NULL`, a dead cell, or a non-character left
   half, writing each character atom verbatim (same termination as
   `print $VAR`, §4.4.6)
3. if `as VERDICT` is present, bind `VERDICT` to a fresh object alive iff
   every step succeeded (open, all writes, close); on any I/O failure it is
   born dead
4. without `as`, the verdict is allocated and discarded

`to` is a contextual keyword. `VERDICT` must not be `NULL` when present.
`write` is fire-and-forget by default — capture the verdict to react to
failure

#### 4.4.22 `append SRC to "PATH" [as VERDICT];`

identical to `write` (§4.4.21) except the file is opened in append mode:
existing contents are preserved and the new bytes follow; an absent file is
created. failure semantics and the optional verdict clause are the same

#### 4.4.23 `close VAR;`

release a file-owning object without deleting the file.

1. read `VAR`. if `NULL` or already dead, no-op
2. clear `VAR`'s `owns_path` flag (so the upcoming kill does not `unlink`)
3. set `VAR`'s `alive` to false

on objects without `owns_path` set, `close` is indistinguishable from
`VAR.DIE();`

#### 4.4.24 `text PART+ as VAR;`

build a string-cons-list (§4.6) from a sequence of STRING and IDENT parts and
bind `VAR`. at least one part is required; parts may appear in any order
(`text "value: " N as MSG;`, `text PREFIX " — " SUFFIX as MSG;`)

each part is reduced to a string-shaped object, then all parts are folded
left to right with `ath_concat` (§4.8.4):

1. **STRING part** — the decoded bytes become a cons-list of character atoms
   via `ath_string_from_bytes`; an empty STRING (`""`) contributes `NULL`
2. **IDENT part** — read from scope and passed through `ath_coerce_string`:
   numeric-payload operands route through `ath_to_string` to their decimal
   form; payload-less operands (cons-lists, generic composites, `NULL`,
   character atoms) pass through unchanged

intermediate `ath_concat` results install operand deps via
`ath_inherit_lifetime` (§4.8.1), so the final string inherits deps from every
IDENT part transitively; killing any IDENT operand afterward invalidates the
result at the next observation. STRING parts contribute no dep

`VAR` is a write target; binding `NULL` is a compile-time error (§4.2).
`text` is **not** idempotent — it always overwrites `VAR`. `text "" as V;`
binds `V` to `NULL`; `text "hello" as V;` binds a fresh literal cons-list;
`text N as V;` binds `ath_coerce_string(N)`

#### 4.4.25 `loop N { body }` (count loop)

runs `body` exactly `N.value` times; `N` is a number-payload object.

1. compute the count via `ath_count_of(N)` (§5.2): its non-negative int64
   payload, or `0` if `N` is dead, lacks a payload, or is negative
2. iterate, emitting `body` each time, until the counter reaches `0`

`loop` is **not** a liveness loop: it does not consult `ath_is_alive`, and
rebinding `N` in the body does not change the remaining count (snapshotted on
entry). loops nest; `body` may exit early by terminating the activation
(`THIS.DIE()`). distinct from the string built-in `repeat` (§4.8.4)

#### 4.4.26 `every N { body }` (interval loop)

runs `body`, sleeps `N.value` milliseconds, repeats forever. `N` is a
number-payload object re-read each iteration; the sleep uses the same
semantics as `sleep` (§4.4.18), so a dead/non-positive `N` makes the pause a
no-op

the loop has no liveness guard and no count: the **only** exits are the body
terminating the activation (`THIS.DIE()`) or process death (a watched signal,
`ath_halt`, a fatal signal). recurrence lives here, in the loop — never in a
liveness object, which could not come back alive without violating one-way
death (§4.1). code textually after an `every` whose body never terminates is
unreachable

### 4.5 Program termination

a program terminates when its main activation returns: `THIS.DIE();` (or
`THIS.DIE(RET);`) at the top level, or control falling off the end of the
top-level statement list. the OS-visible exit code is `0`; main's pending
return value, if any, is discarded

a function activation returns when `THIS.DIE();` is executed in its body or
control falls off the end. its pending return value (defaulting to `NULL`) is
delivered to the caller as the value of the function call

### 4.6 String encoding

`INPUT` and `print $VAR` interpret objects as **strings**. a string is a
(possibly empty) sequence of characters, represented as a chain of objects:

- the empty string is `NULL`
- a non-empty string with first character `c` and tail `t` is the composite
  `compose(ATOM(c), t)` — i.e. `BIFURCATE [ATOM(c), t] S;`

a **character atom** is an alive object the runtime allocates lazily, exactly
once per distinct character code (0..255). two strings sharing a character at
any position share the same atom by pointer identity

atoms have no observable internal structure: their `left`/`right` halves are
initially unset. decomposing an atom yields freshly allocated halves with no
character meaning; the atom remains the canonical representative for the
interpolation reverse lookup. the encoding is a stable string ↔ object
mapping, so strings round-trip across implementations

### 4.7 Lifetime extensions

every object carries seven optional lifetime conditions in addition to its
explicit `.DIE`-driven mortality. these are **additional** ways an object can
be observed dead, independent of its `alive` field; an object with none set
lives until explicitly killed, and any combination may be set:

1. **Deadline.** a monotonic-clock timestamp (seconds since a runtime-fixed
   epoch). when the clock reaches or passes it, the object becomes dead at the
   next `ath_is_alive`; once dead, not re-evaluated
2. **Watched path.** a filesystem path; every observation calls
   `access(F_OK)`, and any failure makes the object dead. recreation does not
   revive it
3. **Awaited signal.** a POSIX signal number; every observation consults a
   sticky per-signal flag set by a process-wide handler. the flag is
   process-global, so all watchers of the same signal die together
4. **One-shot flag.** the first **direct** `ath_is_alive` observation returns
   alive and atomically flips `alive` to false; every subsequent observation
   returns dead. with `~ATH`'s re-check-before-each-iteration rule this runs a
   body exactly once. "direct" excludes transitive observation through another
   object's dependency walk (§4.8.1) — dep walks use the pure `ath_observe`,
   which never consumes one-shots
5. **Path-ownership flag (`owns_path`).** when set with `watch_path`, the
   object **owns** the file. an explicit `ath_die` on a still-alive owner
   triggers `unlink(watch_path)` before clearing the alive bit. passive deaths
   (deadline, dep-propagation, watch-path observation, one-shot consumption,
   signal arrival) do **not** unlink. set exclusively by `read "PATH" as VAR;`
   (§4.4.20); not copied by `CLONE`, not propagated by composition, not
   installed by `watch "PATH" as VAR;` (observation-only)
6. **Watched pid.** a process id; every observation calls `kill(pid, 0)`, and
   the object dies exactly when that reports `ESRCH` (exited and reaped).
   `EPERM` is not death; a zombie counts as alive until reaped. set by
   `watch pid N as VAR;`
7. **Watched mtime.** a path plus the modification time (seconds +
   nanoseconds) captured at allocation; every observation `stat()`s the path,
   and the object dies once the mtime differs or the file is gone. set by
   `watch mtime "PATH" as VAR;`

surface forms that set these: `import NAME... VAR;` sets the deadline on a
range-based library match (§5.3), or the one-shot flag on the entry `once`;
`watch "PATH" as VAR;` sets the watched path; `read "PATH" as VAR;` sets the
watched path *and* the ownership flag; `TIMER N as T;` sets the deadline;
`watch pid` sets the watched pid; `watch mtime` sets the watched mtime. there
are no other surface forms — these are entry-point allocations, not mutators

lifetime sampling is **uniform** over the library entry's range, seeded by
`ATH_SEED` (decimal unsigned integer) if set, else by the wall clock; setting
`ATH_SEED` makes library-sampled programs deterministic

### 4.7.1 Direct-kill rules and the unlink trigger

when `V.DIE();` runs on a still-alive `V` (§4.4.5), or `BRANCH` consumes a
still-alive `V` (§4.4.16), the runtime invokes `ath_die(V)`:

```
if V is alive and V.owns_path is set and V.watch_path != NULL:
    unlink(V.watch_path)                # errors silently ignored
V.alive = false
```

this is the only place `unlink` is called. all other deaths — deadline
expiration, dep propagation, one-shot observation, watch-path detection,
signal handling — flip the alive bit without touching the filesystem.
`close VAR;` (§4.4.23) sidesteps this by clearing `owns_path` first, so the
file persists. because `BRANCH(V)` always consumes its subject (§4.4.16),
running a read-result through `BRANCH` deletes the file; `CLONE` it first
(§4.4.17) to check it without releasing — clones never carry `owns_path`

### 4.8 Numeric payload and built-in arithmetic

an object may carry a numeric **payload** in addition to its alive bit and
halves. the payload is **tagged**:

- `num_kind` — `ATH_NUM_NONE` (none, default), `ATH_NUM_INT` (signed int64),
  `ATH_NUM_FLOAT` (IEEE-754 `double`), or `ATH_NUM_BIG` (arbitrary-precision
  integer, §4.8.7)
- `num` — a union holding the `int64` (`num.i`), the `double` (`num.f`), or a
  pointer to a heap bigint (`num.b`), selected by `num_kind`

"has a payload" means `num_kind != ATH_NUM_NONE` (test via `ath_has_value()`).
`import number N as VAR;` (§4.4.13) is the only surface form that allocates
with a payload. character atoms (§4.6) carry **no** numeric payload — they are
pointer-identified, not value-identified

**the numeric tower.** binary arithmetic and comparisons promote along
**FLOAT > BIG > INT**: both INT runs in int64 (with overflow-to-dead);
either BIG (and neither FLOAT) runs in exact arbitrary precision yielding BIG;
either FLOAT reads both as `double` yielding FLOAT. so `2`, `2.0`, and a
bignum `2` compare equal, `2 + 1.5` is `3.5`, and a bignum sum never
overflows. the bitwise operators and `gcd` are integer-only and **born dead**
on any FLOAT *or* BIG operand. float results are never born dead from
overflow: an operation producing `±inf`/`nan` (e.g. float ÷0) yields a
**live** float carrying that IEEE value — born-death is reserved for
operations that cannot produce a number at all (dead/absent operand, int64
overflow, integer ÷0)

#### 4.8.1 Lifetime inheritance

derived values inherit death from their operands. the allocator
`ath_inherit_lifetime(result, x, y)` records `x` and `y` as dependencies of
`result`; every `ath_is_alive(result)` then returns false if either
dependency is dead, in addition to checking `result`'s own alive bit and
lifetime extensions (§4.7)

dependencies are recorded in two slots (`dep1`, `dep2`); unary built-ins use
only `dep1` and pass NULL for `dep2`. only arity ≤ 2 is supported

inheritance is **dynamic**: once an operand dies, the derived object becomes
dead at the next observation, even if alive at allocation; the reverse never
holds (§4.1). `BIFURCATE [L, R] V;` composition (§4.4.3) does **not** install
dependencies — composites have lifetimes independent of their halves. only
`ath_inherit_lifetime` installs deps, and only the built-in C functions call
it

#### 4.8.2 Arithmetic built-ins

the runtime exports these C functions, brought in via `importf <NAME> as
NAME;` referencing `stdlib/NAME.ath` (§4.4.12). all promote per the tower
(§4.8):

| Name | Surface call | Result | Born dead when |
|---|---|---|---|
| `add` | `ADD [X, Y] R;` | `X + Y` | int overflow; either operand dead at call |
| `sub` | `SUB [X, Y] R;` | `X - Y` | int overflow; either operand dead |
| `mul` | `MUL [X, Y] R;` | `X * Y` | int overflow; either operand dead |
| `div` | `DIV [X, Y] R;` | int: `X / Y` trunc toward zero; float: true division | int `Y == 0`; int `INT64_MIN / -1`; either operand dead. **Float ÷0 is live `±inf`/`nan`** |
| `mod` | `MOD [X, Y] R;` | int: `X % Y`; float: `fmod(X, Y)` | int `Y == 0`; int `INT64_MIN % -1`; either operand dead. **Float `fmod(_,0)` is live `nan`** |
| `to_string` | `TO_STRING [N, _] S;` | decimal string (§4.6) of `N` | `N` dead or has no payload |
| `parse` | `PARSE [S, _] N;` | number parsed from string (FLOAT if it contains `.`/`e`/`E`, else INT) | `S` dead, malformed, or out of range |

all call `ath_inherit_lifetime(R, X, Y)` on success (`_` = NULL for unary ops
records only `X` as a dependency)

`to_string` of a FLOAT renders the **shortest decimal that round-trips**:
fixed-point for decimal exponent in `[-4, 16)`, scientific outside — so
`2500.0`, `3.14`, `0.0001`, but `1e+16` and `1e-05`. the result always
carries a `.` or exponent so a float reads distinctly from an integer;
`nan`/`inf`/`-inf` print as those names. `to_string` of an INT is the
canonical signed decimal (optional leading `-`, no leading zeros except `0`,
no thousands separators)

**Conversions and rounding** (each `[X, _] R;`):

| Name | Result |
|---|---|
| `int_to_bignum` | `X` as a (sticky) BIG (§4.8.7). an INT or BIG converts/passes through; an integral FLOAT converts exactly; a non-integral or non-finite FLOAT is born dead |
| `int_to_float` | `X` as a FLOAT (a FLOAT passes through) |
| `float_to_int` | `X` truncated toward zero to an INT (an INT passes through); a `nan` or out-of-int64-range float is born dead |
| `floor` / `ceil` / `round` | a FLOAT `X` rounded down / up / to-nearest-half-away-from-zero (still FLOAT); an INT passes through unchanged |

**Transcendentals** always return a FLOAT (an INT operand promotes), born
dead only on a dead/absent operand — an out-of-domain input (e.g. `SQRT` of a
negative, `LOG` of `0`) produces the live IEEE result (`nan` or `-inf`):

| Name | Result |
|---|---|
| `sqrt` / `cbrt` | square / cube root of `X` |
| `exp` | `e` raised to `X` |
| `log` / `log2` / `log10` | natural / base-2 / base-10 logarithm |
| `sin` / `cos` / `tan` | trig functions of `X` (radians) |
| `asin` / `acos` / `atan` | inverse trig functions |
| `atan2` | `ATAN2 [Y, X] R;` — angle of `(X, Y)`, both signs |
| `hypot` | `HYPOT [X, Y] R;` — `sqrt(X*X + Y*Y)` without overflow |

the second operand of `to_string` and `parse` is conventionally `NULL` but
any value is accepted and ignored; `_` as a placeholder is stylistic, and
sema enforces the same in-scope rule as for any operand (§6.1). overflow
detection uses `__builtin_*_overflow` (or portable equivalents); division and
modulo special-case `INT64_MIN / -1`. parse uses `strtoll` with
full-string-consumed validation

##### Numeric second wave

a further group of `stdlib/` shims; each installs its operands as deps and is
born dead on a dead or non-payload operand. the **arithmetic** ops (`pow`,
`abs`, `neg`, `min`, `max`, `sign`, `clamp`) promote per the tower (§4.8); the
**integer-only** ops (`gcd` and the bitwise/shift group) are born dead on any
FLOAT operand:

| Name | Surface call | Result | Born dead when |
|---|---|---|---|
| `pow` | `POW [X, Y] R;` | `X` raised to `Y` | int: `Y < 0` or overflow. Float: never (out-of-domain → live `nan`) |
| `abs` | `ABS [X, _] R;` | magnitude of `X` | int `X == INT64_MIN` (no positive rep) |
| `neg` | `NEG [X, _] R;` | `-X` | int `X == INT64_MIN` (overflow) |
| `min` | `MIN [X, Y] R;` | lesser of `X`, `Y` | — |
| `max` | `MAX [X, Y] R;` | greater of `X`, `Y` | — |
| `gcd` | `GCD [X, Y] R;` | gcd of `\|X\|`, `\|Y\|` (gcd(0,0)=0) | `X` or `Y` is `INT64_MIN`; **any FLOAT operand** |
| `sign` | `SIGN [X, _] R;` | `-1`, `0`, or `1` (FLOAT in → `-1.0`/`0.0`/`1.0`) | — |
| `band` | `BAND [X, Y] R;` | `X & Y` | any FLOAT operand |
| `bor` | `BOR [X, Y] R;` | `X \| Y` | any FLOAT operand |
| `bxor` | `BXOR [X, Y] R;` | `X ^ Y` | any FLOAT operand |
| `bnot` | `BNOT [X, _] R;` | `~X` (one's complement) | a FLOAT operand |
| `shl` | `SHL [X, Y] R;` | `X << Y` (logical) | `Y < 0` or `Y > 63`; any FLOAT operand |
| `shr` | `SHR [X, Y] R;` | `X >> Y` (arithmetic) | `Y < 0` or `Y > 63`; any FLOAT operand |
| `clamp` | `CLAMP [X, PAIR] R;` | `X` confined to `[LO, HI]` | `LO > HI`; `X`/`LO`/`HI` unusable |

`POW` uses exponentiation by squaring with overflow checks at every multiply
for the int path (`0^0 == 1`), and `pow(3)` for the float path. `SHL` shifts
an unsigned copy to avoid signed-overflow UB; `SHR` is sign-extending.
`CLAMP` packs its bounds `(LO, HI)` into a single composite (the compose-pair
pattern of §4.8.4) — pass `ENTANGLE [LO, HI] PAIR;` for dep propagation

#### 4.8.3 Comparisons as verdicts

a **verdict** is an object whose alive bit carries the truth of a comparison:
alive iff true, dead iff false. verdicts carry no payload — they are observed
only through `ath_is_alive`, typically in a `~ATH` header:

```
LT [X, Y] V;
~ATH(V) {
    print X is less than Y;
    V.DIE();
}
```

| Name | Surface call | Alive when | Born dead when |
|---|---|---|---|
| `lt` | `LT [X, Y] V;` | `X.value < Y.value`  | comparison false; either operand dead; either operand has no payload |
| `eq` | `EQ [X, Y] V;` | `X.value == Y.value` | comparison false; either operand dead; either operand has no payload |
| `gt` | `GT [X, Y] V;` | `X.value > Y.value`  | comparison false; either operand dead; either operand has no payload |
| `le` | `LE [X, Y] V;` | `X.value <= Y.value` | comparison false; either operand dead; either operand has no payload |
| `ge` | `GE [X, Y] V;` | `X.value >= Y.value` | comparison false; either operand dead; either operand has no payload |
| `ne` | `NE [X, Y] V;` | `X.value != Y.value` | comparison false; either operand dead; either operand has no payload |

`le`, `ge`, `ne` are primitive rather than derived, so a negated comparison
can be combined with `AND`/`OR` without a `NOT`-of-verdict construct

a true verdict's lifetime inherits from both operands via
`ath_inherit_lifetime(V, X, Y)` (§4.8.1): if either operand dies after the
comparison, `V` becomes dead at the next observation. a false verdict is
allocated dead from the start, with no dependencies recorded

comparison of payload-less objects (strings, generic composites, `NULL`)
always yields a born-dead verdict. the verdict primitives compare numeric
payloads only

##### Logical combinators

| Name | Surface call | Alive when | Born dead when |
|---|---|---|---|
| `and` | `AND [X, Y] V;` | both `X` and `Y` alive at every observation | either operand dead at call |
| `or`  | `OR  [X, Y] V;` | at least one of `X`, `Y` alive at every observation | both operands dead at call |

`AND` allocates an alive verdict and calls `ath_inherit_lifetime(V, X, Y)`, so
the result dies once either operand dies and stays dead. `OR` allocates an
alive verdict, installs both operands in `dep1`/`dep2`, and sets
`dep_mode = ATH_DEP_OR` (§5.1) so `ath_is_alive` treats them disjunctively:
the result stays alive while at least one dep is alive and flips dead only
once both are observed dead (permanently, §4.1)

there is **no `NOT` combinator**: a materialized `NOT(V)` would require a
dead→alive transition when `V` later dies, which §4.1 forbids. negation is
expressed at the observation site via `~ATH(!V)` (§4.4.4) or `BRANCH(!V)`
(§4.4.16); to combine a negated verdict, use the primitive contrapositive
(`X >= Y` instead of `NOT (X < Y)`) and feed that into `AND`/`OR`

#### 4.8.4 String operations

strings are right-nested cons-lists of character atoms (§4.6). `length` and
`concat` are `stdlib/` function calls; `index` and `slice` are statement
syntax (§4.4.14, §4.4.15); `find` is a function call; `replace`/`replace_all`
use the compose-pair pattern below:

| Name | Surface form | Result | Born dead when |
|---|---|---|---|
| `length` | `LENGTH [S, _] N;` | int64 payload = right-spine elements walked before `NULL` or a dead object | `S` is `NULL` (yields `0`, not dead) — never dead unless the walk hits a dead non-`NULL` cell |
| `concat` | `CONCAT [A, B] R;` | fresh cons-list: elements of `A` then `B`, terminated `NULL` | `A` or `B` dead at the call |
| index | `S[N] X;` (§4.4.14) | the Nth right-spine head of `S` | `S`/`N` dead or unbound; `N` has no payload or is negative; walk hits `NULL`/dead before position `N` |
| slice | `S[I..J] X;` (§4.4.15) | fresh cons-list of elements `I..J-1`, terminated `NULL` | `S`/`I`/`J` dead; `I` or `J` has no payload or is negative; `I > J`; walk hits `NULL`/dead before `J` |
| `find` | `FIND [HAY, NEEDLE] IDX;` | int64 = 0-indexed position of the first occurrence of `NEEDLE` in `HAY` | NEEDLE absent; HAY or NEEDLE dead; non-character atom during slurp |
| `replace` | `REPLACE [S, PAIR] R;` | fresh cons-list with the first occurrence of `NEEDLE` in `S` replaced by `REPLACEMENT` | NEEDLE absent; NEEDLE empty; S/PAIR dead; non-character atom during slurp |
| `replace_all` | `REPLACE_ALL [S, PAIR] R;` | fresh cons-list with every non-overlapping occurrence replaced | same failure modes as `replace` |

##### Compose-pair pattern

`REPLACE`/`REPLACE_ALL` conceptually take three arguments (source, needle,
replacement) but the builtin FFI is fixed at two `ath_obj *` inputs. pack
`NEEDLE` and `REPLACEMENT` into a single composite and pass it as the second
argument, identical to how `S[I..J]` lowers (§4.4.15). two compositions are
available, differing only in whether `PAIR` inherits its operands as deps:

**Recommended — `ENTANGLE`** (`ath_entangle`, `stdlib/entangle.ath`):

```
ENTANGLE [NEEDLE, REPLACEMENT] PAIR;
REPLACE [S, PAIR] R;
```

`ENTANGLE` performs the same composition as `BIFURCATE [L, R] V;` and
additionally calls `ath_inherit_lifetime(PAIR, NEEDLE, REPLACEMENT)`
(§4.8.1); killing `NEEDLE`/`REPLACEMENT` after the `REPLACE` invalidates
`PAIR`, which invalidates `R` through the dep chain

**Alternative — plain `BIFURCATE`:**

```
BIFURCATE [NEEDLE, REPLACEMENT] PAIR;
REPLACE [S, PAIR] R;
```

`BIFURCATE` composition does not install deps: `PAIR` carries no dependency on
`NEEDLE`/`REPLACEMENT`. use this form when the carrier must outlive its
operands

internally `ath_replace`/`ath_replace_all` decompose the pair to recover
`NEEDLE` and `REPLACEMENT`; both work with either composition, differing only
in the resulting dep chain on `R`

##### Shared behavior

`LENGTH` on `NULL` returns the eternal payload object `0` (not a dead
result) — the empty string is a real cons-list with a known length. all seven
operations install operand deps on their results via `ath_inherit_lifetime`
(within the compose-pair limits above). in `intern` mode (§4.4.3),
concatenated/sliced/replaced results allocate fresh cons cells; sharing
happens only at the char-atom level. unary calls pass `NULL` (or any name) as
the ignored second argument, and the chain dies on the first failure

**Empty needle.** `FIND` with an empty `NEEDLE` returns `0`.
`REPLACE`/`REPLACE_ALL` with an empty `NEEDLE` return a born-dead `R`; use
`CONCAT` to prepend or append text

##### Predicates, transforms, and structural operations

each is a `stdlib/` shim over a two-operand builtin, in three kinds:

- **Predicates** return a *verdict* — alive iff the relation holds, dead
  otherwise (§4.8.3) — with both operands installed as deps; born dead on a
  malformed string (a cell whose left half is not a recognized character atom
  encountered mid-walk)
- **Transforms** return a fresh cons-list with the source installed as a dep;
  `NULL` in → `NULL` out
- **Structural** ops (`split`, `join`) move between a string and a cons-list
  of strings

| Name | Surface form | Kind | Result | Born dead when |
|---|---|---|---|---|
| `streq` | `STREQ [A, B] V;` | predicate | alive iff `A`, `B` byte-identical | malformed `A` or `B` |
| `startswith` | `STARTSWITH [HAY, PREFIX] V;` | predicate | alive iff `HAY` begins with `PREFIX` | malformed operand |
| `endswith` | `ENDSWITH [HAY, SUFFIX] V;` | predicate | alive iff `HAY` ends with `SUFFIX` | malformed operand |
| `strlt` | `STRLT [A, B] V;` | predicate | alive iff `A` < `B` lexicographically (byte order) | dead operand; `A >= B` → dead verdict |
| `strgt` | `STRGT [A, B] V;` | predicate | alive iff `A` > `B` lexicographically | dead operand; `A <= B` → dead verdict |
| `lower` | `LOWER [S, _] R;` | transform | fresh copy of `S` with `A`–`Z` lowercased | `S` dead/malformed (→ `NULL`) |
| `upper` | `UPPER [S, _] R;` | transform | fresh copy with `a`–`z` uppercased | `S` dead/malformed (→ `NULL`) |
| `trim` | `TRIM [S, _] R;` | transform | `S` with leading and trailing whitespace removed | `S` dead/malformed (→ `NULL`) |
| `lstrip` | `LSTRIP [S, _] R;` | transform | `S` with leading whitespace removed | as `trim` |
| `rstrip` | `RSTRIP [S, _] R;` | transform | `S` with trailing whitespace removed | as `trim` |
| `split` | `SPLIT [S, SEP] LIST;` | structural | right-nested cons-list whose left halves are the substrings of `S` between occurrences of `SEP` | `SEP` empty or dead; `S` dead |
| `join` | `JOIN [LIST, SEP] R;` | structural | the strings in `LIST` (its left halves) concatenated, interleaved with `SEP` | `LIST` or `SEP` dead; empty `LIST` → `NULL` |

**Empty-string boundaries.** every string starts and ends with the empty
string: `STARTSWITH`/`ENDSWITH` with an empty (`NULL`) prefix/suffix yield an
alive verdict, and `STREQ [NULL, NULL]` is alive. whitespace for the strip
family is space, tab, LF, CR

**`split`.** the empty separator is born dead. a trailing `SEP` yields a
trailing empty-string element, so `SPLIT` of `"a,b,"` on `","` is
`["a", "b", ""]`. the result is `NULL`-terminated and inherits both `S` and
`SEP` as deps

**`join`.** walks `LIST`'s right spine, slurping each cell's left half and
appending `SEP` between cells but not after the last. an empty `SEP`
concatenates with no separators; an empty `LIST` returns `NULL`. `SPLIT` and
`JOIN` are inverses when `SEP` is non-empty and absent from every element

a transform whose source is dead or contains a non-character atom returns
`NULL`; a predicate over a malformed operand is born dead

##### Search, measurement, construction, and the atom bridge

| Name | Surface form | Result | Born dead when |
|---|---|---|---|
| `contains` | `CONTAINS [HAY, NEEDLE] V;` | verdict, alive iff `NEEDLE` occurs in `HAY` | dead operand; malformed string. Empty needle → alive |
| `count` | `COUNT [HAY, NEEDLE] N;` | int64 = number of non-overlapping occurrences (`0` if none, *alive*) | empty `NEEDLE`; dead operand; non-character atom |
| `rfind` | `RFIND [HAY, NEEDLE] IDX;` | int64 = index of the *last* occurrence | `NEEDLE` absent; dead operand; non-character atom. Empty needle → `len(HAY)` |
| `repeat` | `REPEAT [S, N] R;` | fresh cons-list = `S` repeated `N` times | `N` < 0 or no payload; `S` dead. `N == 0` → `NULL` |
| `reverse` | `REVERSE [S, _] R;` | fresh cons-list with `S`'s characters reversed | `S` dead/malformed (→ `NULL`) |
| `pad_left` | `PAD_LEFT [S, N] R;` | `S` left-padded with spaces to width `N` (copy if already ≥ `N`) | `N` < 0 or no payload; `S` dead |
| `pad_right` | `PAD_RIGHT [S, N] R;` | `S` right-padded with spaces to width `N` | as `pad_left` |
| `ord` | `ORD [A, _] N;` | int64 = code (0..255) of the character atom `A` | `A` is not a character atom; `A` dead |
| `chr` | `CHR [N, _] S;` | length-1 string whose character has code `N` | `N` < 0, `N` > 255, no payload, or dead |

`COUNT` matches non-overlapping, left-to-right: `COUNT` of `"aaaa"` for `"aa"`
is `2`. unlike `find`, a zero count is a live `0` payload. the empty needle is
present everywhere (`CONTAINS` alive, `RFIND` at `len`) but born dead for
`COUNT`. `REPEAT` and the `PAD` ops take a number payload as their second
operand; padding always uses space `0x20` and never truncates; all three
build fresh cons cells and inherit both operands as deps. `ORD` is the inverse
of `CHR`: `ORD` consumes a single character *atom* (the value `S[N]` yields,
§4.4.14), `CHR` produces a length-1 string. because `S[N]` returns a snapshot
rather than the canonical atom, `ORD` of an `S[N]` result is unaffected by
the liveness of other strings sharing that character

##### String polish

| Name | Surface form | Result | Born dead when |
|---|---|---|---|
| `compare` | `COMPARE [A, B] N;` | int64 `-1`/`0`/`1` by byte-lexicographic order | dead or malformed operand |
| `char_at` | `CHAR_AT [S, N] STR;` | the Nth character as a **length-1 string** (vs `S[N]`'s bare atom) | `N` < 0, no payload, or out of range; `S` dead/malformed |
| `find_from` | `FIND_FROM [S, PAIR] IDX;` | int64 = first index of `NEEDLE` at or after `START`, where `PAIR` packs `(NEEDLE, START)` | needle absent at/after `START`; `START` < 0 or no payload; dead operand. Empty needle → `min(START, len)` |
| `capitalize` | `CAPITALIZE [S, _] R;` | first character uppercased, the rest lowercased | `S` dead/malformed (→ `NULL`) |
| `title` | `TITLE [S, _] R;` | first character of each whitespace-delimited word uppercased, rest lowercased | `S` dead/malformed (→ `NULL`) |
| `strip_chars` | `STRIP_CHARS [S, CHARS] R;` | `S` with leading and trailing characters in the `CHARS` set removed | `S` dead/malformed (→ `NULL`). Empty `CHARS` → copy of `S` |
| `lstrip_chars` | `LSTRIP_CHARS [S, CHARS] R;` | as `strip_chars`, leading only | as `strip_chars` |
| `rstrip_chars` | `RSTRIP_CHARS [S, CHARS] R;` | as `strip_chars`, trailing only | as `strip_chars` |
| `pad_left_with` | `PAD_LEFT_WITH [S, PAIR] R;` | `S` left-padded to width with a fill character, where `PAIR` packs `(WIDTH, FILL)` | `WIDTH` < 0 or no payload; empty `FILL`; `S` dead |
| `pad_right_with` | `PAD_RIGHT_WITH [S, PAIR] R;` | as `pad_left_with`, padding on the right | as `pad_left_with` |

`COMPARE` is the three-way form of `strlt`/`streq`/`strgt`. `CHAR_AT`
complements `S[N]`: the subscript yields a character atom (for `ORD`),
`CHAR_AT` a printable length-1 string. `FIND_FROM`, `CLAMP` (§4.8.2), and the
`PAD_*_WITH` ops use the compose-pair convention: pack the two trailing
arguments with `ENTANGLE` (dep propagation) or `BIFURCATE`. the fill for
padding is the first character of `FILL`; widening past `len(S)` is a no-op
copy. `TITLE` treats only whitespace as a word boundary, so `"abc-def"`
titlecases to `"Abc-def"`

#### 4.8.5 Time and randomness built-ins

two function-call builtins read the runtime clock and random source, brought
in via `importf <NAME> as NAME;`:

| Name | Surface call | Result `value` | Born dead when |
|---|---|---|---|
| `now` | `NOW [_, _] T;` | monotonic milliseconds since boot | never (always alive) |
| `random` | `RANDOM [LO, HI] R;` | uniform-ish int64 in `[LO.value, HI.value)` | LO or HI dead; either lacks payload; `LO.value >= HI.value` |

`NOW` ignores both operands (convention: pass `NULL` for both) and returns a
fresh number-payload object; it is **not** dep-tracked. it is monotonic —
successive calls within an activation observe non-decreasing values from the
system's monotonic-clock origin (typically boot)

`RANDOM` produces a uniformly-distributed value over `[LO, HI)`. the source is
the global `rand()` seeded at startup by `ATH_SEED` (if set) or the wall clock
(§4.7); multiple `rand()` calls span the full int64 range, with minor modulo
bias only for very wide ranges. results are **not** dep-tracked against
`LO`/`HI` — the bounds are parameters, not lifetime sources

#### 4.8.6 Generic list operations

a **list** is any right-nested cons-list (§4.6) with arbitrary objects as
left-half elements. these `stdlib/` shims walk the right-spine and read each
element's int64 payload, so they operate on lists of numbers (and born-die on
a string, whose elements are payload-less character atoms). build a list with
`BIFURCATE [HEAD, REST] LIST;`:

| Name | Surface call | Result | Born dead when |
|---|---|---|---|
| `sum` | `SUM [LIST, _] N;` | Σ of element payloads (empty → `0`) | a non-payload or dead element; overflow |
| `product` | `PRODUCT [LIST, _] N;` | Π of element payloads (empty → `1`) | a non-payload or dead element; overflow |
| `maximum` | `MAXIMUM [LIST, _] N;` | greatest element payload | empty list; non-payload/dead element |
| `minimum` | `MINIMUM [LIST, _] N;` | least element payload | empty list; non-payload/dead element |
| `member` | `MEMBER [LIST, X] V;` | verdict, alive iff some element's payload equals `X`'s | `X` lacks a payload (→ dead verdict) |
| `take` | `TAKE [LIST, N] R;` | fresh list of the first `N` elements (all of `LIST` if `N >= length`) | `N < 0` or no payload; dead `LIST`. `N == 0` → `NULL` |
| `drop` | `DROP [LIST, N] R;` | fresh list of all but the first `N` elements | `N < 0` or no payload; dead `LIST`. `N >= length` → `NULL` |

`SUM`/`PRODUCT` use the empty-list **identity** (0 and 1, kept **integer**);
`MAXIMUM`/`MINIMUM` born-die on an empty list. all four folds promote per the
tower (§4.8). `MEMBER` compares by payload (a FLOAT element equals an INT of
the same value). `TAKE`/`DROP` allocate fresh cons cells and inherit `LIST`
and `N` as deps. there is no `map`/`filter`/`reduce` — `~ATH` has no
first-class functions to pass. `LENGTH` (§4.8.4) and `S[N]`/`S[I..J]`
(§4.4.14–15) cover length, indexing, and slicing for lists too

**Dead backbone vs. dead element.** every generic list operation walks the
right-spine under one guard: a cell is visited only while non-`NULL` **and
alive**:

- a **dead spine cell** acts as a **terminator**, indistinguishable from
  `NULL`: the walk stops before it, and every element from that cell on is
  invisible. `LENGTH` counts only the live prefix; the folds, `MEMBER`, and
  `TAKE`/`DROP` see just that prefix. killing a backbone cell silently
  truncates the list there
- a **dead element** (the `left` head of a still-live spine cell) is not a
  terminator — the walk continues past it — but treatment is per-operation:
  `SUM`/`PRODUCT`/`MAXIMUM`/`MINIMUM` born-die on it; `MEMBER` fails to match
  and keeps scanning; `TAKE`/`DROP` copy it through as-is; `ALL_OF`/`ANY_OF`
  read elements as lifetimes, so a dead element directly drives the combined
  verdict

liveness gates the **backbone**, while element liveness is a value-level
concern each fold decides for itself

##### n-ary lifetime combinators

| Name | Surface call | Result | Empty list |
|---|---|---|---|
| `all_of` | `ALL_OF [LIST, _] V;` | verdict alive iff **every** element of `LIST` is alive — dies when the first element dies | alive (vacuous) |
| `any_of` | `ANY_OF [LIST, _] V;` | verdict alive iff **some** element of `LIST` is alive — dies only when the last does | dead |

the result is **dependency-tracked**, not a snapshot: `ALL_OF` folds `ath_and`
from a fresh always-alive identity, `ANY_OF` folds `ath_or` from a fresh dead
identity, so the verdict is a tree of `AND`/`OR` nodes (§4.8.1) over the
elements. killing any element after the call propagates through the tree at
the next observation. the elements are read as lifetimes, not payloads, so the
lists need not be numbers. there is no `none_of` — a dead→alive transition
would contradict one-way death (§4.7); negation lives only at the loop level,
in `~ATH(!V)` (§4.4.4)

#### 4.8.7 Bignum (arbitrary-precision integers)

a `BIG` payload is a heap-allocated, arbitrary-precision signed integer. a
value becomes BIG in exactly two ways — an **integer literal too large for
int64** (`import number 99999999999999999999 as N;`, §4.4.13), or an explicit
**`int_to_bignum`** conversion (§4.8.2) — and then propagates through
arithmetic by the tower (§4.8). there is **no auto-promotion**: int64 overflow
stays born dead; arbitrary precision is opt-in

- **Stickiness & exactness.** BIG is sticky: BIG arithmetic (`+ - * / %`,
  comparisons, `NEG`, `ABS`, `MIN`, `MAX`, `SIGN`, `CLAMP`) is exact, never
  overflows, and is **not demoted** even when it would fit int64. this lets a
  growing computation reach arbitrary precision: seed an accumulator with
  `int_to_bignum` and a factorial never overflows. a BIG compares and prints
  identically to the equal integer, so stickiness is invisible to arithmetic
  and output
- **Division.** `DIV`/`MOD` truncate toward zero (remainder takes the
  dividend's sign); a zero divisor is born dead
- **Mixed with float.** a BIG combined with a FLOAT promotes to FLOAT via a
  `double` approximation (FLOAT > BIG precedence)
- **Integer-only and small-integer ops.** the bitwise group, `gcd`, and `POW`
  are born dead on a BIG operand; so are `chr`, `float_to_int`, and an
  out-of-range count/index/duration. a *small* sticky BIG is accepted where
  its value fits: `count_of` (loop counts) uses a BIG's value when it fits
  int64 and saturates a too-large positive one to `INT64_MAX`.
  `SUM`/`PRODUCT`/`MAXIMUM`/`MINIMUM` over a list containing a BIG element are
  born dead (scalar BIG arithmetic is unaffected)
- **Rendering.** `TO_STRING` and `print $N` emit the full signed decimal,
  however long

bignums (and their digit arrays) are allocated and never freed, like every
other runtime object

---

## 5. Runtime ABI

the compiler emits LLVM IR that calls a small set of C runtime functions. the
codegen MUST NOT inline their bodies or bypass them with direct struct
manipulation — they are the swap points that let semantics evolve without
touching the frontend

### 5.1 Types

```c
typedef enum {
    ATH_NUM_NONE = 0, ATH_NUM_INT, ATH_NUM_FLOAT, ATH_NUM_BIG /* §4.8.7 */
} ath_num_kind;

typedef struct ath_obj {
    int            alive;       /* nonzero = alive */
    struct ath_obj *left;       /* NULL = UNSET    */
    struct ath_obj *right;      /* NULL = UNSET    */

    /* §4.7 lifetime extensions (optional, any may be unused) */
    double         deadline_s;
    const char    *watch_path;
    int            is_oneshot;
    int            awaiting_signal;
    int            owns_path;   /* §4.7 ext 5; only set by read */

    /* §4.8 numeric payload (tagged). num_kind ∈ {NONE, INT, FLOAT, BIG};
     * num.i holds the int64 when INT, num.f the double when FLOAT. Test
     * presence with ath_has_value() (num_kind != NONE). */
    ath_num_kind   num_kind;
    union { int64_t i; double f; ath_bigint *b; } num;  /* b: §4.8.7 */

    /* §4.8.1 dependency tracking */
    struct ath_obj *dep1;       /* NULL = no dep */
    struct ath_obj *dep2;       /* NULL = no dep */

    /* §4.8.3 dep evaluation mode.
     *   ATH_DEP_AND (0, default) — result dead if any non-null dep dead.
     *   ATH_DEP_OR  (1)          — result stays alive while at least one
     *                              non-null dep alive; flips dead only
     *                              once both observed dead.
     * Set exclusively by ath_or; everything else leaves it at 0. */
    int dep_mode;

    /* §4.6 character identity. is_char nonzero iff char_code (0..255)
     * carries this object's character. Set by ath_char_atom and copied by
     * ath_clone, so a snapshot of a character atom (e.g. the result of the
     * subscript form S[N], §4.4.14) is still recognized as that character. */
    int            is_char;
    int            char_code;

    /* §4.4.11 extended watch sources, appended after the codegen-modeled
     * prefix. watch_pid > 0 ties liveness to a running process (dies when
     * kill(pid,0) reports ESRCH). mtime_path, when non-NULL, ties liveness
     * to a file's modification time captured at allocation. Both are
     * monotonic — process exit and the first mtime change are permanent. */
    int            watch_pid;
    const char    *mtime_path;
    int64_t        mtime_sec;
    int64_t        mtime_nsec;

    /* §7 concurrency (mailbox/actor/universe) and §7.6 networking (socket fd,
     * listener flag, end-of-stream flag, recv line-buffer) append further
     * optional fields after this prefix; all zero on a plain object. */
} ath_obj;
```

the exact field order is an ABI commitment to the codegen — `alive`, `left`,
`right` MUST remain the first three fields. the optional extension fields MAY
be reordered or extended

### 5.2 Functions and globals

```c
/* allocation */
ath_obj *ath_alloc_alive(void);

/* operations */
ath_obj *ath_compose(ath_obj *l, ath_obj *r);
void     ath_decompose(ath_obj *v, ath_obj **l_out, ath_obj **r_out);
void     ath_die(ath_obj *v);
int      ath_is_alive(ath_obj *v);

/* I/O. The *_bytes / *_obj_raw forms write WITHOUT a trailing newline;
 * ath_print / ath_print_obj are those plus one line feed. The `print`
 * statement (§4.4.6) emits its parts via the raw forms and adds a single
 * trailing newline for the whole statement. */
void     ath_print(const char *text, size_t len);
void     ath_print_bytes(const char *text, size_t len);
ath_obj *ath_input_line(void);
void     ath_print_obj(ath_obj *s);
void     ath_print_obj_raw(ath_obj *s);
ath_obj *ath_char_atom(int c);

/* Literal-string + coercion helpers used by the `text` statement (§4.4.24). */
ath_obj *ath_string_from_bytes(const char *bytes, size_t len);
ath_obj *ath_coerce_string(ath_obj *v);

/* Lifetime extensions (§4.7) */
ath_obj *ath_alloc_with_lifetime(double min_s, double max_s);
ath_obj *ath_alloc_watching_file(const char *path);
ath_obj *ath_alloc_watching_signal(int signum);
ath_obj *ath_alloc_watching_signal_by_name(const char *name);
ath_obj *ath_alloc_watching_pid(ath_obj *n);      /* §4.4.11 watch pid  */
ath_obj *ath_alloc_watching_mtime(const char *p); /* §4.4.11 watch mtime */
ath_obj *ath_alloc_oneshot(void);
ath_obj *ath_alloc_from_library(const char *name);
int      ath_library_lookup(const char *name, double *min_out, double *max_out);
/* Registers a runtime lifetime-library entry; emitted in main's prologue
 * for each built-in and -D/--define-lifetime range (§5.3.1). */
void     ath_register_lifetime(const char *name, double min_s, double max_s);

/* Numeric payload + arithmetic (§4.8) */
ath_obj *ath_alloc_number(int64_t v);     /* ATH_NUM_INT payload   */
ath_obj *ath_alloc_float(double v);       /* ATH_NUM_FLOAT payload */
ath_obj *ath_alloc_bignum_from_decimal(const char *s); /* §4.8.7 BIG literal */
void     ath_inherit_lifetime(ath_obj *result, ath_obj *a, ath_obj *b);
ath_obj *ath_add(ath_obj *x, ath_obj *y);
ath_obj *ath_sub(ath_obj *x, ath_obj *y);
ath_obj *ath_mul(ath_obj *x, ath_obj *y);
ath_obj *ath_div(ath_obj *x, ath_obj *y);
ath_obj *ath_mod(ath_obj *x, ath_obj *y);
ath_obj *ath_to_string(ath_obj *x, ath_obj *unused);
ath_obj *ath_parse(ath_obj *s, ath_obj *unused);
ath_obj *ath_lt(ath_obj *x, ath_obj *y);
ath_obj *ath_eq(ath_obj *x, ath_obj *y);
ath_obj *ath_gt(ath_obj *x, ath_obj *y);
ath_obj *ath_le(ath_obj *x, ath_obj *y);
ath_obj *ath_ge(ath_obj *x, ath_obj *y);
ath_obj *ath_ne(ath_obj *x, ath_obj *y);

/* Second-wave numeric built-ins (§4.8.2). Unary ops take a dummy second
 * operand. ath_pow/abs/neg/min/max/gcd/sign are arithmetic; the band/bor/
 * bxor/bnot/shl/shr group is bitwise; ath_clamp takes a (lo, hi) pair. */
ath_obj *ath_pow(ath_obj *x, ath_obj *y);
ath_obj *ath_abs(ath_obj *x, ath_obj *unused);
ath_obj *ath_neg(ath_obj *x, ath_obj *unused);
ath_obj *ath_min(ath_obj *x, ath_obj *y);
ath_obj *ath_max(ath_obj *x, ath_obj *y);
ath_obj *ath_gcd(ath_obj *x, ath_obj *y);
ath_obj *ath_sign(ath_obj *x, ath_obj *unused);
ath_obj *ath_band(ath_obj *x, ath_obj *y);
ath_obj *ath_bor(ath_obj *x, ath_obj *y);
ath_obj *ath_bxor(ath_obj *x, ath_obj *y);
ath_obj *ath_bnot(ath_obj *x, ath_obj *unused);
ath_obj *ath_shl(ath_obj *x, ath_obj *y);
ath_obj *ath_shr(ath_obj *x, ath_obj *y);
ath_obj *ath_clamp(ath_obj *x, ath_obj *pair);

/* Numeric conversions and rounding (§4.8.2). */
ath_obj *ath_int_to_bignum(ath_obj *x, ath_obj *unused);  /* sticky BIG, §4.8.7 */
ath_obj *ath_int_to_float(ath_obj *x, ath_obj *unused);
ath_obj *ath_float_to_int(ath_obj *x, ath_obj *unused);
ath_obj *ath_floor(ath_obj *x, ath_obj *unused);
ath_obj *ath_ceil(ath_obj *x, ath_obj *unused);
ath_obj *ath_round(ath_obj *x, ath_obj *unused);

/* Float transcendentals (§4.8.2). FLOAT result; out-of-domain → nan/inf. */
ath_obj *ath_sqrt(ath_obj *x, ath_obj *unused);
ath_obj *ath_cbrt(ath_obj *x, ath_obj *unused);
ath_obj *ath_exp(ath_obj *x, ath_obj *unused);
ath_obj *ath_log(ath_obj *x, ath_obj *unused);
ath_obj *ath_log2(ath_obj *x, ath_obj *unused);
ath_obj *ath_log10(ath_obj *x, ath_obj *unused);
ath_obj *ath_sin(ath_obj *x, ath_obj *unused);
ath_obj *ath_cos(ath_obj *x, ath_obj *unused);
ath_obj *ath_tan(ath_obj *x, ath_obj *unused);
ath_obj *ath_asin(ath_obj *x, ath_obj *unused);
ath_obj *ath_acos(ath_obj *x, ath_obj *unused);
ath_obj *ath_atan(ath_obj *x, ath_obj *unused);
ath_obj *ath_atan2(ath_obj *y, ath_obj *x);
ath_obj *ath_hypot(ath_obj *x, ath_obj *y);

/* Null-safe payload-presence test (num_kind != NONE), provided as a
 * static inline in the header. */
int      ath_has_value(const ath_obj *o);

/* Logical combinators over verdicts (§4.8.3). NOT is not provided —
 * see the §4.8.3 commentary. */
ath_obj *ath_and(ath_obj *x, ath_obj *y);
ath_obj *ath_or(ath_obj *x, ath_obj *y);

/* Compose with dep propagation (§4.8.4). Equivalent to ath_compose
 * followed by ath_inherit_lifetime, in one call. Use for the
 * compose-pair pattern (notably (needle, replacement) for REPLACE)
 * when the carrier composite must die if either operand dies. */
ath_obj *ath_entangle(ath_obj *x, ath_obj *y);

/* String operations (§4.8.4). All install operand deps on results via
 * ath_inherit_lifetime. ath_index and ath_slice are also invoked by the
 * subscript and range-subscript statement codegen. */
ath_obj *ath_length(ath_obj *s, ath_obj *unused);
ath_obj *ath_concat(ath_obj *a, ath_obj *b);
ath_obj *ath_index(ath_obj *s, ath_obj *n);
ath_obj *ath_slice(ath_obj *s, ath_obj *range);
ath_obj *ath_find(ath_obj *hay, ath_obj *needle);
ath_obj *ath_replace(ath_obj *s, ath_obj *pair);
ath_obj *ath_replace_all(ath_obj *s, ath_obj *pair);

/* String predicates returning verdicts (§4.8.4). */
ath_obj *ath_streq(ath_obj *a, ath_obj *b);
ath_obj *ath_startswith(ath_obj *hay, ath_obj *prefix);
ath_obj *ath_endswith(ath_obj *hay, ath_obj *suffix);
ath_obj *ath_strlt(ath_obj *a, ath_obj *b);
ath_obj *ath_strgt(ath_obj *a, ath_obj *b);
ath_obj *ath_contains(ath_obj *hay, ath_obj *needle);
ath_obj *ath_count(ath_obj *hay, ath_obj *needle);   /* occurrence count */
ath_obj *ath_compare(ath_obj *a, ath_obj *b);        /* -1 / 0 / 1 */

/* String transforms (§4.8.4). Unary ops take a dummy second operand;
 * the *_chars / *_with / repeat / pad family take an operand or pair. */
ath_obj *ath_lower(ath_obj *s, ath_obj *unused);
ath_obj *ath_upper(ath_obj *s, ath_obj *unused);
ath_obj *ath_trim(ath_obj *s, ath_obj *unused);
ath_obj *ath_lstrip(ath_obj *s, ath_obj *unused);
ath_obj *ath_rstrip(ath_obj *s, ath_obj *unused);
ath_obj *ath_reverse(ath_obj *s, ath_obj *unused);
ath_obj *ath_capitalize(ath_obj *s, ath_obj *unused);
ath_obj *ath_title(ath_obj *s, ath_obj *unused);
ath_obj *ath_strip_chars(ath_obj *s, ath_obj *chars);
ath_obj *ath_lstrip_chars(ath_obj *s, ath_obj *chars);
ath_obj *ath_rstrip_chars(ath_obj *s, ath_obj *chars);
ath_obj *ath_repeat(ath_obj *s, ath_obj *n);
ath_obj *ath_pad_left(ath_obj *s, ath_obj *n);
ath_obj *ath_pad_right(ath_obj *s, ath_obj *n);
ath_obj *ath_pad_left_with(ath_obj *s, ath_obj *pair);
ath_obj *ath_pad_right_with(ath_obj *s, ath_obj *pair);

/* String indexing/search/codec (§4.8.4). */
ath_obj *ath_rfind(ath_obj *hay, ath_obj *needle);
ath_obj *ath_find_from(ath_obj *s, ath_obj *pair);   /* (needle, start) */
ath_obj *ath_char_at(ath_obj *s, ath_obj *n);
ath_obj *ath_ord(ath_obj *a, ath_obj *unused);
ath_obj *ath_chr(ath_obj *n, ath_obj *unused);
ath_obj *ath_split(ath_obj *s, ath_obj *sep);        /* string -> list  */
ath_obj *ath_join(ath_obj *list, ath_obj *sep);      /* list -> string  */

/* Generic list operations over any cons-list payload (§4.8.6). */
ath_obj *ath_sum(ath_obj *list, ath_obj *unused);
ath_obj *ath_product(ath_obj *list, ath_obj *unused);
ath_obj *ath_maximum(ath_obj *list, ath_obj *unused);
ath_obj *ath_minimum(ath_obj *list, ath_obj *unused);
ath_obj *ath_member(ath_obj *list, ath_obj *x);
ath_obj *ath_take(ath_obj *list, ath_obj *n);
ath_obj *ath_drop(ath_obj *list, ath_obj *n);

/* N-ary lifetime combinators over a list (§4.7). all_of/any_of build a
 * dep-tracked verdict that is alive while every / any element is alive. */
ath_obj *ath_all_of(ath_obj *list, ath_obj *unused);
ath_obj *ath_any_of(ath_obj *list, ath_obj *unused);

/* Shallow clone for non-destructive checking (§4.4.17). Copies all fields
 * of v except dep1/dep2, which are zeroed. */
ath_obj *ath_clone(ath_obj *v);

/* Time and timer (§4.4.18, §4.4.19, §4.8.5). All times are int64
 * milliseconds. ath_sleep_ms is called directly by sleep-stmt codegen and
 * is a no-op if n is dead/no-payload. ath_alloc_timer_ms produces an alive
 * object with a deadline; the duration is not dep-tracked. */
void     ath_sleep_ms(ath_obj *n);
ath_obj *ath_alloc_timer_ms(ath_obj *n);
ath_obj *ath_now(ath_obj *a, ath_obj *b);
ath_obj *ath_random_range(ath_obj *lo, ath_obj *hi);

/* File I/O (§4.4.20-23, §4.7 ext 5). ath_alloc_read_file slurps the file
 * into a cons-list whose head is a non-interned wrapper carrying watch_path
 * and owns_path=1. ath_write_file/ath_append_file return a fresh verdict.
 * ath_close clears owns_path before killing v so the file is not unlinked.
 * ath_die unlinks the file when a still-alive owner is killed via explicit
 * .DIE() or BRANCH. */
ath_obj *ath_alloc_read_file(const char *path);
ath_obj *ath_write_file(ath_obj *s, const char *path);
ath_obj *ath_append_file(ath_obj *s, const char *path);
void     ath_close(ath_obj *v);

/* Extracts a non-negative int64 iteration count from a number object,
 * clamped at 0 for dead/payload-less/negative inputs. Called by the
 * `loop N` and `every N` statement codegen (§4.4.25-26). */
int64_t  ath_count_of(ath_obj *n);

/* program control */
void     ath_halt(void) __attribute__((noreturn));

/* the singleton dead object */
extern ath_obj *ath_NULL;
```

`ath_compose` is the **fresh/intern swap point**. two implementations ship as
separate archives, selected at link time:

- `libath_fresh.a` — `ath_compose` always allocates (default)
- `libath_intern.a` — `ath_compose` hash-conses by raw pointer pair

the driver flag `--compose fresh|intern` selects the archive; both share
`runtime_common.o` (everything except `ath_compose`). `ath_decompose`,
`ath_die`, and `ath_is_alive` are swap points for future lazy-halves,
cascading-death, and derived-liveness rules

`ath_alloc_with_lifetime` allocates a fresh alive object that becomes
observably dead after a uniform-random delay in `[min_s, max_s]` seconds; a
sample ≤ 0 is born dead, and samples above `1e308` are clamped.
`ath_alloc_watching_file` gates liveness on `access(F_OK)` (born dead if the
file is absent at allocation). `ath_alloc_oneshot` sets `is_oneshot` — the
first `ath_is_alive` returns alive and flips `alive` to false; exposed via the
library name `once`. `ath_alloc_from_library` looks `name` up
case-insensitively: `once` → `ath_alloc_oneshot`, range names →
`ath_alloc_with_lifetime`, misses → `ath_alloc_alive`. `ath_library_lookup`
returns only range-based hits (it does not recognize `once`)

### 5.3 Lifetime library

the runtime ships a fixed table mapping case-insensitive concept names to
lifetime ranges in seconds. implementations MAY add entries but MUST preserve
the named ones with at least the documented ranges:

| Name | min (s) | max (s) | character |
|---|---|---|---|
| `once` | — | — | **special**: alive for exactly one `ath_is_alive` observation, dead thereafter |
| `instant` | 0 | 0 | zero lifetime — born dead |
| `muzzle flash` | 5e-4 | 2e-3 | sub-millisecond |
| `tick` | 1e-3 | 1e-2 | low-millisecond |
| `flash` | 0.05 | 0.5 | one-tenth of a second-ish |
| `blink` | 0.1 | 0.4 | low variance, very short |
| `second` | 1 | 1 | zero variance, exactly 1 s |
| `minute` | 60 | 60 | zero variance |
| `hour` | 3600 | 3600 | zero variance |
| `day` | 86 400 | 86 400 | zero variance |
| `week` | 604 800 | 604 800 | zero variance |
| `year` | 31 557 600 | 31 557 600 | zero variance (Julian year) |
| `spark` | 0.1 | 2 | moderate variance |
| `soap bubble` | 2 | 30 | moderate variance |
| `smoke ring` | 5 | 60 | moderate variance |
| `snowflake` | 60 | 600 | moderate |
| `ice cube` | 900 | 7200 | moderate |
| `mayfly` | 300 | 86 400 | wide |
| `fruit fly` | 28 800 | 180 000 | moderate |
| `fly` | 86 400 | 259 200 | 1–3 days |
| `banana` | 259 200 | 1 209 600 | 3–14 days |
| `daisy` | 43 200 | 604 800 | 12 h – 1 week |
| `rose` | 604 800 | 2 592 000 | 1–30 days |
| `moth` | 604 800 | 2 419 200 | 1–28 days |
| `mouse` | 31 536 000 | 94 608 000 | 1–3 years |
| `goldfish` | 94 608 000 | 1 262 304 000 | 3–40 years |
| `dog` | 315 360 000 | 567 648 000 | 10–18 years |
| `human` | 1.58e9 | 3.79e9 | 50–120 years |
| `sequoia` | 3.15e10 | 1.10e11 | 1000–3500 years |
| `pyramid` | 1.26e11 | 2.52e11 | 4000–8000 years |
| `continent` | 3e15 | 3e16 | ~100M–1B years |
| `star` | 3e16 | 3.2e17 | 1B–10B years |
| `red dwarf` | 3e17 | 3e19 | 10B–1T years |
| `galaxy` | 3e18 | 3e19 | 100B–1T years |
| `black hole` | 3e90 | 3e100 | ~googol years |
| `proton` | 3e37 | 3e41 | hypothetical baryon decay |
| `universe` | 3e100 | 3e110 | heat death |
| `forever` | 1e308 | 1e308 | effectively infinite |
| `lightning` | 1e-4 | 10 | very high variance (5 OOM) |
| `campaign` | 0 | 100 | highly variable |
| `experiment` | 1 | 1e6 | 6 OOM |
| `empire` | 3.15e9 | 3.15e13 | 100 years – 1M years |
| `author` | 2.52e9 | 3.15e9 | 80–100 years |
| `meson` | 1e-8 | 1e-7 | 10–100 ns |

names with `min == max` have zero variance; names with `min == 0` may be born
dead

#### 5.3.1 User-extended entries

the compiler accepts `-D NAME:MIN:MAX` (long form `--define-lifetime`) on the
command line, repeatable; each registers an additional library entry in the
binary:

```
athc -D "tortoise:50:150" -D "soap bubble:1:5" prog.ath -o prog
```

- `NAME` is the concept name (same matching rules as built-ins; spaces
  permitted; matched case-insensitively against the joined `import` metadata)
- `MIN` and `MAX` are non-negative floats in seconds, with `MIN <= MAX`

the compiler emits `ath_register_lifetime` calls at the top of `main`, before
any user code. the runtime stores user entries in a separate table consulted
*before* the built-in table by `ath_library_lookup`, so a user entry
overrides a built-in of the same name. non-time-based entries (e.g. the
`once`-style flag) cannot be registered from the CLI

an implementation MAY refuse to register more than 64 user entries per
program; the reference runtime emits a stderr diagnostic and ignores excess
entries. `ath_halt` is invoked exactly when `THIS.DIE();` executes (typically
`_exit(0)`)

### 5.4 Search path

the search-path form of `importf` (§4.4.8) resolves bare stems against an
ordered list of directories:

1. each colon-separated entry of `ATH_PATH`, in order
2. the compiler-adjacent `stdlib/` directory (same parent as `runtime/`)

the first directory containing `STEM.ath` wins. relative `ATH_PATH` entries
are resolved against the current working directory at compile time. programs
that ship without `ATH_PATH` see only the default `stdlib/`, which is
sufficient for all built-in arithmetic; project-local helpers should prefer
quoted `importf` (relative to the importing file)

---

## 6. Errors

### 6.1 Compile-time errors

- lexical: unterminated `/*`, unterminated `"..."`, missing space after
  `print`, illegal character
- syntactic: any deviation from the grammar in §3, including a concept
  `import` with no metadata word (a bare `import VAR;`, §4.4.1)
- reference to an unbound name in any read position, checked syntactically: a
  name is in scope if introduced by some preceding statement in the same
  activation, whose scope starts with `THIS`, `NULL`, `ARGS`
- reference to an unknown function in a `funcall-stmt`: matched
  case-insensitively against names registered by `importf` or `import builtin`
  (§4.4.12)
- binding `NULL` (any case variant in a write position) per §4.2
- file-not-found or parse error in an `importf` target; for the search-path
  form (§4.4.8), "not found" means no `ATH_PATH` entry and no
  compiler-adjacent `stdlib/` contains `STEM.ath`
- a `FLOAT` literal in `import number` (§4.4.13) that is not finite (e.g.
  `1e999`). an integer literal exceeding int64 is **not** an error — it is a
  bignum literal (§4.8.7)
- `watch` paths are not validated at compile time; a missing file makes the
  watching object born dead at runtime, never a compile error
- missing C symbols declared by `import builtin` (§4.4.12) surface as
  **link-time** errors, not compile-time

### 6.2 Run-time behavior

there are no run-time errors. a program that passes the §6.1 checks either
runs to completion or runs forever — it cannot crash, abort, or produce a
runtime diagnostic

this survives even when §6.1's syntactic check admits a read of a variable
whose introducing statement lies on an unexecuted control-flow path (e.g.
inside a `~ATH` body that runs zero times): such reads yield `NULL` (§4.2),
and every operation in §4.4 is defined on `NULL`

## 7. Concurrency (cooperative actors)

~ATH has optional cooperative concurrency that reuses the liveness model as its
scheduler. It is **single-OS-thread and cooperative**: exactly one actor runs at
a time, and control only switches at explicit yield points (`yield`, `recv`,
`join`, and `sleep` inside an actor). There is no preemption and no shared-memory
data race; the runtime needs no locks. A program that never uses `spawn` behaves
exactly as in §1–§6 (the scheduler is created lazily on the first spawn).

### 7.1 Model

- **Actor** — an activation of an `importf` function (`THIS` is its own object,
  `ARGS` its spawn argument, §4.2) running on its own coroutine stack with a
  runtime mailbox. Built-ins are not spawnable (compile error).
- **Handle** — the object bound by `spawn`. It is **alive while the actor runs**
  and dies when the actor returns or is cancelled. Other code observes, joins, or
  cancels an actor purely through this handle's liveness: `~ATH(A) {...}` waits on
  actor `A`, `A.DIE()` requests cooperative cancellation.
- **Channel** — a handle (bound by `channel`) carrying a FIFO mailbox but no
  coroutine. `send` enqueues; `recv from` dequeues; closing it (`close C;` or
  `C.DIE()`) makes it die. A channel is "closed" exactly when it is dead.
- **Universe** — a handle (bound by `universe`) that scopes a group of actors
  (`spawn ... into N`). It is **alive while any scoped child is** (so `join N`
  waits for the whole group) and `N.DIE()` **cancels the subtree**: each child's
  handle depends on the universe (§4.7), so it is observed dead and unwinds at its
  next yield point. (`universe` is a soft keyword, distinct from the `universe`
  lifetime-library concept of §5.3; the `universe as IDENT;` shape selects it.)

### 7.2 Statement semantics

- `spawn FN <operand> [into N] as A;` — create an actor running `FN` with the
  composed operand as `ARGS`; bind live handle `A`. With `into N`, the child is
  scoped to universe `N`. The actor does not run yet; it is enqueued.
- `send <operand> to D;` — FIFO-enqueue the operand onto handle `D`'s mailbox
  (actor or channel). Non-blocking. A send to a dead `D` is dropped.
- `recv [from S] as M;` — dequeue one message, **yielding until** one is
  available. Without `from`, the running actor's own mailbox. With `from S`,
  channel/handle `S`. Buffered messages are delivered FIFO even after `S` dies;
  once `S` is dead **and** drained, `recv` yields `NULL` (the **EOF** signal). A
  cancelled actor's own `recv` returns `NULL` immediately so it can unwind.
- `yield;` — return control to the scheduler; the actor resumes later.
- `join A;` — drive the scheduler until handle `A` (actor or universe) is dead.
  Usable at top level and inside an actor. Equivalent to `~ATH(A) { yield; }`.
- `channel as C;` / `universe as N;` — bind a fresh channel / universe.

The idiomatic consumer drains a channel by looping on the received message's
liveness, which ends exactly at EOF:

```
recv from C as M;
~ATH(M) { /* use M */ recv from C as M; }
```

### 7.3 Scheduling and determinism

The scheduler is a deterministic FIFO round-robin run loop. Spawn order, message
FIFO order, and round-robin order are fully specified, so output is reproducible
and **identical under `fresh` and `intern`** (the scheduler keys on actor
identity and the run queue, never on value identity; handles are plain alive
objects). At the end of `main` — or at a top-level `THIS.DIE()` — a program that
spawned actors drives the scheduler to completion before exiting. `sleep` inside
an actor parks (yields with a deadline) instead of blocking the whole scheduler;
at top level `sleep` blocks as before (§4.4).

Cancellation is **cooperative**: `A.DIE()` / `N.DIE()` clears liveness, and the
actor notices at its next `recv`/`yield`/`~ATH` check, then unwinds by returning
normally. An actor that never yields cannot be cancelled.

There are still no run-time errors (§6.2). A program that deadlocks (every actor
blocked on a message that never arrives, with none sleeping) simply stops making
progress and the scheduler returns.

### 7.4 REPL limitation

The REPL is a synchronous interpreter and does not run the coroutine scheduler;
the concurrency statements (`spawn`, `send`, `recv`, `yield`, `join`, `channel`,
`universe`) raise a clear error there. They are supported in compiled programs
only.

### 7.5 Runtime ABI

The scheduler lives in `runtime/scheduler.c`, compiled into both archives. The
emitted code calls (see §5.2 for the object type):

```
ath_obj *ath_spawn(ath_obj *(*fn)(ath_obj *), ath_obj *arg);
ath_obj *ath_spawn_into(ath_obj *(*fn)(ath_obj *), ath_obj *arg, ath_obj *universe);
void     ath_send(ath_obj *dest, ath_obj *msg);
ath_obj *ath_recv(void);
ath_obj *ath_recv_from(ath_obj *src);
void     ath_yield(void);
void     ath_join_handle(ath_obj *handle);   /* `join` (the statement) */
ath_obj *ath_channel(void);
ath_obj *ath_universe_new(void);
void     ath_scheduler_drain(void);
int      ath_in_actor(void);
void     ath_park_until(double deadline_s);
```

## 7.6 Networking

Networking extends the actor model rather than adding a parallel I/O subsystem: a
**network connection is a channel whose liveness is the socket**. The connection
handle is alive while the socket is open; peer disconnect, a socket error, or an
explicit `close`/`.DIE()` makes it dead, which ends a `~ATH(CONN) {...}` loop with
no new control-flow concept. Because a connection is a channel, the existing
`send` and `recv` statements (§7.2) carry its traffic — there are no networking
verbs beyond the three that set a connection up.

Two stream transports are supported: **TCP** (`AF_INET`) and **Unix-domain**
(`AF_UNIX`). They share one accept/connect/send/recv path; a Unix-domain address
is written `"unix:/path"`. Unix-domain is the deterministic, port-free choice for
local tests.

### 7.6.1 Statements

- `listen PORT as L;` / `listen "unix:/path" as L;` — bind a listening socket and
  bind handle `L`, alive while the socket is open. A numeric/bound-name operand is
  a TCP port (1–65535); a `"unix:/path"` literal is a Unix-domain socket (a stale
  socket file at that path is removed first). Born dead on any bind/listen error.
- `accept from L as C;` — take one connection from listener `L`, binding the fresh
  connection handle `C`. Blocks (parking the actor, §7.6.2) until a client
  connects. `C` is a channel: `send`/`recv` on it cross the wire.
- `connect "host" PORT as C;` / `connect "unix:/path" as C;` — open a connection,
  binding `C` (alive while connected, born dead on failure). A `"unix:/path"` host
  takes no port; any other host is TCP to `host:port`.

`send` and `recv` on a connection handle behave as in §7.2 but move bytes over the
socket, **newline-framed**:

- `send M to C;` — coerce `M` to a string (§4.6), write its bytes followed by a
  `\n`. A write to a closed/broken peer kills `C`.
- `recv from C as M;` — yield until a full line arrives, then bind `M` to that line
  as a string (the trailing `\n`/`\r\n` stripped), exactly like `input` (§4.4). On
  peer close, any unterminated final line is delivered, then the connection goes
  dead and subsequent `recv` yields `NULL`. As with channels, the loop condition is
  the **connection's** liveness, not the message's:

```
accept from SERVER as CONN;
~ATH(CONN) {            // ends when the peer hangs up
    recv from CONN as LINE;
    send LINE to CONN;  // echo; a send to a dead CONN is dropped
}
```

### 7.6.2 Scheduling, EOF, and resources

Socket fds are non-blocking. A `accept`/`recv`/`send`/`connect` that would block
**parks the current actor** on the fd (a new blocked-on-I/O state) and yields; the
scheduler's idle step `poll()`s every parked fd alongside the nearest `sleep`
deadline and rewakes actors as fds become ready. So one actor per connection can
serve many clients concurrently. At top level (no actor) the same wait is a
blocking `poll`, like top-level `sleep`.

Peer-close is observed without a syscall in the liveness hot path: the read/write
paths latch an end-of-stream flag and kill the handle, and `~ATH`/`ath_is_alive`
just read it. Determinism holds as in §7.3 — connection handles are plain alive
objects (never interned, no deadlines) and framing reuses the §4.6 string
builders, so a well-behaved networked program produces identical output under
`fresh` and `intern`.

A connection's fd is closed when its handle dies through `close C;`/`C.DIE()` (or
the runtime's internal kill on EOF/error). A handle that dies **passively** —
e.g. a connection whose variable is dropped without `close` — has its fd reclaimed
by a sweep at scheduler drain; until then the fd lingers, as with any un-`close`d
resource. The REPL limitation of §7.4 applies: `listen`/`accept`/`connect` (which
ride on `send`/`recv`) are compiled-only.

### 7.6.3 Runtime ABI

Networking lives in `runtime/net.c`, compiled into both archives. The emitted code
calls:

```
ath_obj *ath_listen(const char *spec, ath_obj *port);   /* spec NULL => TCP(port) */
ath_obj *ath_accept(ath_obj *listener);
ath_obj *ath_connect(const char *host, ath_obj *port);  /* "unix:/p" host => AF_UNIX */
```

`send`/`recv` on a connection are the same `ath_send`/`ath_recv_from` as §7.5: each
checks for a socket fd on the handle and routes to the socket path
(`ath_sock_send` / `ath_sock_recv_line`) instead of the in-process mailbox.
