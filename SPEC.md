# ~ATH Language Specification

This document defines the dialect of `~ATH` that our compiler accepts and the
runtime semantics it implements. It is the source of truth: when the
implementation and this document disagree, one of them is a bug.

The dialect is a **custom hybrid** rooted in drocta `~ATH` (the implementable
variant defined at <https://learn-tilde-ath.tumblr.com/>) with explicit
decisions on points the reference interpreter leaves ambiguous.

---

## 1. Scope of v0

### Included

- Object allocation via `import`
- Object decomposition and composition via `BIFURCATE` (both forms)
- The `~ATH(V) { ... }` loop
- Object death via `V.DIE();` (no-argument form)
- `print TEXT;` to stdout
- `//` line comments and `/* ... */` block comments
- Predefined names `THIS` and `NULL`

### Implemented since v0 (formerly deferred)

These were reserved in the original v0 draft and are now part of the
language — they are **not** rejected. Each is specified in §4:

- `V.DIE(ARG);` return form (§4.4.5), and `importf` plus the function-call
  forms `FN [A,B] C;` / `FN A [B,C];` (§4.4.9–4.4.11).
- `INPUT V;` (§4.4.7), `PRINT2 V;` (§4.4.8), and the predefined name
  `ARGS` in function bodies (§4.2).
- Homestuck-surface syntax: the `EXECUTE(F)` postfix (§4.4.4), lowercase
  `bifurcate`, `!VAR` loop inversion (§4.4.4), and multi-word concept
  names in `import` (§4.4.1).
- Numeric literals (`import number`, §4.4.14) and the numeric / string /
  list builtins (§4.8). General literal sugar (e.g. inline list/string
  literals) remains future work (§8).

---

## 2. Lexical structure

### 2.1 Whitespace and comments

Whitespace (space, tab, CR, LF) separates tokens and is otherwise
insignificant, **except** inside a `print` statement's text payload (§3.6).

Comments:

- `// ... <LF>` — from `//` to the next line feed.
- `/* ... */` — block comment. **Not nestable.** A `/*` inside a block comment
  is part of the comment.

### 2.2 Tokens

```
KEYWORD     := 'import' | 'importf' | 'as' | 'watch' | 'BIFURCATE'
              | 'print' | 'INPUT' | 'PRINT2' | 'EXECUTE'
              | 'BRANCH' | 'ELSE' | 'CLONE'
              | 'sleep' | 'TIMER'
              | 'read' | 'write' | 'append' | 'close'
              | 'text' | 'loop' | 'every'                   [matched case-insensitively]
LOOPSTART   := '~ATH'                          [the 'ATH' part is case-insensitive]
DIE         := '.DIE'                          [the 'DIE' part is case-insensitive]
IDENT       := [A-Za-z_][A-Za-z0-9_]*          [case-sensitive]
INT         := '-'? [0-9]+                     [signed int64 literal, §4.8]
STRING      := '"' (any char except '"')* '"'  [escapes \" \\ \n \t \r, §2.3]
PUNCT       := '(' | ')' | '[' | ']' | '{' | '}' | '<' | '>' | ',' | ';' | '!'
```

**Contextual markers.** Three bare identifiers act as contextual
markers — they look like ordinary `IDENT` tokens to the lexer but the
parser checks for their literal value at specific positions:

- `builtin` and `number` as the second token after `import` (§4.4.13,
  §4.4.14).
- `signal`, `pid`, and `mtime` as the second token after `watch`
  (§4.4.12).
- `to` between the source ident and the destination string in
  `write` and `append` (§4.4.22, §4.4.23).

Outside those positions the words are perfectly legal variable names.
Matching is case-insensitive in the marker position.

**Angle brackets.** `<` and `>` are tokenized as PUNCT but currently only
appear in the search-path form of `importf` (§4.4.9). They have no other
syntactic role.

**Two-character punctuation.** The lexer recognizes one multi-char
operator: `..` (DOTDOT, §4.4.16). When the lexer sees `.`, it looks at
the next character: another `.` produces `DOTDOT`; otherwise the
single `.` begins a `.DIE` method token (§2.2 DIE). A lone `.` followed
by anything other than `die` or `.` is a lexical error.

**Case sensitivity.** Keywords, the `~ATH` loop-start token, the `.DIE`
method token, and (in v1+) function names match **case-insensitively**:
`IMPORT`, `Import`, and `import` all denote the same keyword;
`~ath`, `~Ath`, and `~ATH` all denote the same loop-start;
`.die` and `.DIE` denote the same method token. Identifiers (variable
names) are **case-sensitive**: `Foo`, `foo`, and `FOO` are three distinct
variables.

**Reserved words** (no case variant of any of these may appear as an
identifier):

- Active: `import`, `importf`, `as`, `watch`, `BIFURCATE`, `print`,
  `INPUT`, `PRINT2`, `EXECUTE`, `BRANCH`, `ELSE`, `CLONE`, `sleep`,
  `TIMER`, `read`, `write`, `append`, `close`, `text`, `loop`, `every`.

`THIS` and `NULL` are predefined *identifiers* (§4.2), not reserved words —
they follow the case-sensitive identifier rule. The names `this`, `Null`,
and so on are distinct, unbound, perfectly legal identifiers that the
program may introduce via `import`.

Tokenization of an identifier-or-keyword uses maximal munch on
`[A-Za-z_][A-Za-z0-9_]*`. The resulting chunk is then compared
case-insensitively against the reserved-word set: matches become keywords,
non-matches become identifiers. Hence `printer` is an identifier, not the
keyword `print` followed by `er`.

`~ATH` is one token. The tilde is significant.

### 2.3 String literals

A double-quoted string literal `"..."` is a `STRING` token. The body is a
sequence of bytes terminated by the next `"`. Newlines inside the body
are part of the string. The body may be empty.

The body recognizes five escape sequences:

| Source | Decoded |
|--------|---------|
| `\"`   | `"` (U+0022) |
| `\\`   | `\` (U+005C) |
| `\n`   | line feed (U+000A) |
| `\t`   | tab (U+0009) |
| `\r`   | carriage return (U+000D) |

A backslash followed by any other character (including end-of-input)
is a compile-time lexical error. The diagnostic reports the position
of the backslash and the recognized set.

Used by `importf` (§4.4.9), `watch` (§4.4.12), `read` (§4.4.21),
`write` and `append` (§4.4.22, §4.4.23) to name file paths, and by
`text` (§4.4.25) as a string-literal value source. The decoded
byte sequence is what every consumer sees; the original `\X` source
form is not retained.

### 2.4 `print` payload

After the keyword `print`, the lexer enters a one-shot raw mode:

1. Consume exactly one ASCII space (`U+0020`). It is an error if the next
   character is not a space.
2. Capture every subsequent character (including newlines, brackets, anything)
   up to — but not including — the first unescaped `;`, splitting it into an
   ordered run of **parts**:
   - A maximal run of literal characters becomes a `RAWTEXT` part.
   - A `$` immediately followed by an identifier start character
     (`[A-Za-z_]`) begins an **interpolation part**: the lexer reads the
     following `[A-Za-z_][A-Za-z0-9_]*` as a (case-sensitive) variable name
     and emits a `PRINTVAR` part. At run time the named variable's current
     binding is walked as a string (§4.6) and written in place; see §4.4.6.
3. The payload may be empty (zero parts).
4. The `;` is then consumed as a normal token.

The literal runs decode the following escape sequences:

| Source | Decoded |
|--------|---------|
| `\;`   | `;` (U+003B) |
| `\\`   | `\` (U+005C) |
| `\n`   | line feed (U+000A) |
| `\t`   | tab (U+0009) |
| `\r`   | carriage return (U+000D) |
| `\$`   | `$` (U+0024) |

A backslash followed by any other character (including end-of-input)
is a compile-time lexical error. The diagnostic reports the position
of the backslash and the recognized set.

A literal semicolon in the payload requires the `\;` escape; otherwise
the unescaped `;` terminates the payload. A literal backslash requires
`\\`. A **literal `$`** requires `\$`: a `$` that is *not* followed by an
identifier-start character is a compile-time lexical error (so the
interpolation marker is never ambiguous).

---

## 3. Grammar

EBNF. `*` is zero-or-more, `?` is optional.

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
              | print2-stmt
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
              | every-stmt ;

import-stmt   = import-concept
              | import-builtin
              | import-number ;

import-concept
              = 'import' IDENT+ IDENT ';' ;
                (* at least TWO IDENTs: one or more metadata words (joined
                   with single spaces into the concept name) followed by the
                   variable bound. A bare `import VAR;` (single IDENT) is a
                   compile-time error (§6.1). The first IDENT must not be the
                   contextual marker 'builtin' or 'number' (matched
                   case-insensitively). *)

import-builtin
              = 'import' 'builtin' IDENT 'as' IDENT ';' ;
                (* declares a C-ABI function. First IDENT is the C symbol
                   name (case-sensitive). Second IDENT is the function
                   name in the ~ATH function registry (case-insensitive).
                   See §4.4.13. *)

import-number = 'import' 'number' INT 'as' IDENT ';' ;
                (* allocates an eternal-alive object carrying the int64
                   payload. See §4.4.14. *)

importf-stmt  = 'importf' STRING 'as' IDENT ';'
              | 'importf' '<' IDENT '>' 'as' IDENT ';' ;
                (* quoted form: path is relative to the importing file.
                   angle form: name is resolved against ATH_PATH (§5.4),
                   appending '.ath' to the bare identifier. *)

watch-stmt    = 'watch' STRING 'as' IDENT ';'              (* file form   *)
              | 'watch' 'signal' IDENT 'as' IDENT ';'      (* signal form *)
              | 'watch' 'pid' IDENT 'as' IDENT ';'         (* pid form    *)
              | 'watch' 'mtime' STRING 'as' IDENT ';' ;    (* mtime form  *)
                (* 'signal', 'pid', and 'mtime' are *contextual* keywords:
                   bare IDENTs recognized only as the second token after
                   'watch' (case-insensitive). Elsewhere they are normal
                   identifiers. See §4.4.12. *)

bifurcate-stmt
              = decompose-stmt
              | compose-stmt ;

decompose-stmt
              = 'BIFURCATE' IDENT '[' IDENT ',' IDENT ']' ';' ;

compose-stmt  = 'BIFURCATE' '[' IDENT ',' IDENT ']' IDENT ';' ;

ath-loop      = '~ATH' '(' [ '!' ] IDENT ')' '{' statement* '}'
                [ 'EXECUTE' '(' IDENT ')' ] ';'? ;

die-stmt      = IDENT '.DIE' '(' [ IDENT ] ')' ';' ;

print-stmt    = 'print' RAWTEXT ';' ;

input-stmt    = 'INPUT' IDENT ';' ;

print2-stmt   = 'PRINT2' IDENT ';' ;

funcall-stmt  = IDENT '[' IDENT ',' IDENT ']' IDENT ';'        (* compose-arg form *)
              | IDENT IDENT '[' IDENT ',' IDENT ']' ';' ;      (* decompose-result form *)

subscript-stmt
              = IDENT '[' IDENT ']' IDENT ';' ;                (* S[N] X; *)

slice-stmt    = IDENT '[' IDENT '..' IDENT ']' IDENT ';' ;     (* S[I..J] X; *)

branch-stmt   = 'BRANCH' '(' [ '!' ] IDENT ')'
                '{' statement* '}'
                [ [ 'ELSE' ] '{' statement* '}' ] ;
                (* One-shot dispatch. After whichever body runs (or after the
                   skipped dispatch when V is dead and no else clause is
                   present), the runtime kills V — BRANCH consumes its
                   subject. ELSE is optional sugar before the second block;
                   `BRANCH(V) { } { }` and `BRANCH(V) { } ELSE { }` parse
                   identically. *)

clone-stmt    = 'CLONE' IDENT 'as' IDENT ';' ;
                (* Shallow snapshot. The clone copies V's alive bit, halves,
                   payload, and lifetime extensions, but not V's dep chain.
                   Independent identity — killing one does not kill the
                   other. *)

sleep-stmt    = 'sleep' IDENT ';' ;
                (* Block the current activation for IDENT.value milliseconds
                   if IDENT carries a payload and is alive; otherwise no-op.
                   See §4.4.19. *)

timer-stmt    = 'TIMER' IDENT 'as' IDENT ';' ;
                (* Bind the second IDENT to a fresh alive object with a
                   deadline of IDENT.value milliseconds from now. The
                   duration is a parameter, not a dependency — killing it
                   later does not kill the timer. See §4.4.20. *)

read-stmt     = 'read' STRING 'as' IDENT ';' ;
                (* Slurp the file as a string-cons-list bound to IDENT.
                   The result *owns* the file: explicit .DIE() or BRANCH
                   consumption deletes it. See §4.4.21. *)

write-stmt    = 'write' IDENT 'to' STRING [ 'as' IDENT ] ';' ;
                (* Truncate-and-write the source string to the path.
                   The optional 'as' clause binds a verdict object alive
                   iff the write succeeded. 'to' is a contextual marker.
                   See §4.4.22. *)

append-stmt   = 'append' IDENT 'to' STRING [ 'as' IDENT ] ';' ;
                (* Like write-stmt but appends. See §4.4.23. *)

close-stmt    = 'close' IDENT ';' ;
                (* Disown the file (clear owns_path) and kill IDENT. The
                   file persists; the object is dead. See §4.4.24. *)

text-stmt     = 'text' text-part+ 'as' IDENT ';' ;
text-part     = STRING | IDENT ;
                (* Build a string-cons-list (§4.6) from a sequence of
                   parts and bind it to the trailing IDENT. STRING parts
                   contribute their decoded bytes verbatim; IDENT parts
                   are read and coerced (payload-bearing operands route
                   through TO_STRING; existing cons-lists pass through).
                   Parts are folded left to right with CONCAT.
                   See §4.4.25. *)

loop-stmt     = 'loop' IDENT '{' statement* '}' ;
                (* Run the body exactly IDENT.value times (count snapshotted
                   on entry via ath_count_of; dead/payload-less/negative
                   runs zero times). Not a liveness loop. See §4.4.26. *)

every-stmt    = 'every' IDENT '{' statement* '}' ;
                (* Run the body, sleep IDENT.value ms (re-read each pass),
                   repeat forever. Only exits are THIS.DIE() or process
                   death. See §4.4.27. *)

Notes:

- `~ATH` followed by anything other than `(` is a syntax error.
- A statement may not appear outside a `program` or `ath-loop` body.
- The two `BIFURCATE` forms are distinguished by the token following
  `BIFURCATE`: an `IDENT` selects decompose; a `[` selects compose.
- An `IDENT`-starting statement is disambiguated by the next token,
  and (for the bracket forms) by what appears between the brackets:
  - `.DIE` → die-stmt.
  - `[` followed by `IDENT ',' IDENT ']'` → funcall compose-arg.
  - `[` followed by `IDENT ']'` → subscript-stmt (single bracket
    contents, no comma, no `..`).
  - `[` followed by `IDENT '..' IDENT ']'` → slice-stmt.
  - `IDENT` → funcall decompose-result form.

---

## 4. Semantics

### 4.1 Object model

An **object** is a heap-allocated record:

```
ath_obj { alive: bool, left: ath_obj* | UNSET, right: ath_obj* | UNSET }
```

- `alive` is `true` for newly allocated objects, with the single exception of
  `NULL` (§4.2), which is born dead.
- `left` and `right` are initially `UNSET` and become set on first
  decomposition (§4.4).
- Once `alive` transitions to `false`, it never transitions back.
- Setting `left`/`right` is one-way: once set, they do not change.

### 4.2 Initial environment

Each function activation (including the top-level program — its "main"
activation) starts with a fresh local environment containing:

| Name   | Object                                                                 |
|--------|------------------------------------------------------------------------|
| `THIS` | A fresh alive object for this activation. Killing it returns from the function (or, for main, terminates the program). |
| `NULL` | A globally-shared, immortal-in-deadness object: `alive = false`.       |
| `ARGS` | (Function activations only.) The object passed by the caller per §4.4.10/§4.4.11. Not defined in main. |

These bindings use exactly the spellings `THIS` and `NULL` (uppercase). Per
the case-sensitive identifier rule (§2.2), the names `this`, `Null`, etc.,
are not predefined; they are unbound until a program introduces them.

`NULL` is **read-only**: any statement that would rebind it (as an `import`
target, a `BIFURCATE` decompose output, or a `BIFURCATE` compose target) is
a compile-time error. `THIS` is rebindable.

Any other name is unbound until introduced by `import` or by a `BIFURCATE`
that names it as an output.

`NULL` also serves as the **behavioral identity for unbound names at
runtime**: if the syntactic in-scope check (§6.1) lets a read of `V` through
but at execution time no introduction has actually run for `V`, the read
yields `NULL`. Every statement in §4.4 is defined when its source operand is
`NULL` (and therefore when it is unbound).

### 4.3 Variables vs. objects

A variable is a name in the (single global, in v0) environment that points to
exactly one object at any moment. Distinct variables may point to the same
object. Rebinding a variable does **not** mutate the object it formerly
pointed to.

Reading the variable always reads its current binding.

### 4.4 Statement semantics

#### 4.4.1 `import NAME... VAR;` (concept form)

**Two or more** IDENTs follow `import`. The **last** IDENT is `VAR`, the
variable to bind. The one-or-more preceding IDENTs are metadata, joined
by single spaces into a "concept name." A bare `import VAR;` with no
metadata word is a compile-time error (§6.1) — every concept import
carries at least one concept word.

The first IDENT after `import` must not be the contextual marker
`builtin` or `number` (matched case-insensitively). Those words dispatch
to §4.4.13 and §4.4.14 respectively. To bind a variable to the concept
called "builtin" or "number," prefix with another metadata word
(`import the builtin B;`).

If `VAR` is already bound: no-op.

Otherwise: the concept name is looked up case-insensitively in the
runtime's **lifetime library** (§5.3). If a match is found, allocate a
fresh object whose lifetime is sampled uniformly from the library entry's
`[min_s, max_s]` range — see §4.7 for the precise semantics. If no
match is found, allocate a plain alive object (the v0/v1 behavior).

`VAR` is then bound to the resulting object.

The concept name is matched in full; `import fly F;` matches the entry
`fly`, but `import dead fly F;` looks up `dead fly` and falls through
to plain alive.

#### 4.4.2 `BIFURCATE V[L, R];` (decompose)

1. Read `V`'s current binding, the object `o`.
2. If `o.left == UNSET` (equivalently, `o.right == UNSET` — they are always
   set together):
   - Allocate two fresh alive objects `lo`, `ro`.
   - Atomically set `o.left = lo` and `o.right = ro`.
3. Let `(lo, ro) = (o.left, o.right)`.
4. Bind `L := lo` and `R := ro` in the environment.

If `L`, `R`, and `V` overlap, the reads in step 1–3 happen before the writes
in step 4. If `L == R` literally (same identifier), the second write wins
(`R`'s value is what remains bound).

**NULL and dead sources.** Step 2's test is pointer identity against the
`NULL` singleton (§4.2), **not** a liveness check:

- If `o` is `NULL` (V unbound, or bound to the dead singleton), both `L`
  and `R` are bound to `NULL`. No halves are allocated, nothing is
  mutated. This is the only outcome that yields dead children.
- If `o` is a real object that is **dead** (e.g. killed via `.DIE()` but
  not the `NULL` singleton), decompose proceeds normally: on first
  decomposition it allocates two **fresh alive** halves and caches them on
  `o`; thereafter it returns those same halves. A dead parent therefore
  hands back *live* children. Liveness does not propagate downward through
  decomposition — only the `NULL` singleton is barren.

#### 4.4.3 `BIFURCATE [L, R] V;` (compose)

1. Read `L`'s and `R`'s current bindings, the objects `lo` and `ro`.
2. Call the runtime entry point `ath_compose(lo, ro)`. Bind `V` to the result.

The default `fresh` mode (§5.2): `ath_compose` allocates a new alive
object with `left = lo`, `right = ro`. Two `BIFURCATE [L,R] V;` statements
with structurally identical operands produce two distinct objects.

The `intern` mode (§5.2; selectable via `--compose intern`):
`ath_compose` consults a hash-cons table keyed by the **raw pointer pair**
`(lo, ro)`. If a previously composed object with those exact operand
pointers exists, it is returned (whether alive or dead); otherwise, a
new alive object is allocated and the entry inserted. Killing a
composite kills every other variable that ever observed it; structurally
equal composites share storage forever (the table never evicts).

The two modes produce identical observable behavior for any program
that does not rely on the distinctness or shared-identity of composites.
All sample programs in `examples/` run identically under both modes (the
conformance suite verifies this); the difference shows up only in
programs that compose the same operand pair twice and then kill one
result.

#### 4.4.4 `~ATH(V) { S* }` and `~ATH(!V) { S* }`

```
loop:
  alive := ath_is_alive(env[V])
  if not alive (or alive, if the '!' form): goto end_loop
  execute S*
  goto loop
end_loop:
```

The check re-reads `V` from the environment every iteration. Rebinding `V`
inside the body changes what is being watched.

The body may be empty (`{}`), in which case the construct loops forever if
the initial check passes.

The `!V` form (Homestuck "inversion") inverts the condition: the body
runs while `V` is **dead**. Because objects can never come back to life,
`~ATH(!V)` runs at most once if `V` is already dead at entry, then exits;
if `V` is alive at entry, the loop never runs.

An optional `EXECUTE(IDENT)` postfix may follow the closing `}`:

```
~ATH(V) { S* } EXECUTE(F);
```

`EXECUTE(F)` names a **function to invoke once the loop exits by its
condition** — the Homestuck reading of "when the subject dies, EXECUTE
the action." On reaching the loop's exit:

1. The subject `V` is read. For a normal loop it is dead (the condition
   failed); for an inverted loop it is alive.
2. `F` is called with `V` as its single argument. `F` is resolved like
   any function call (§4.4.13): a user function (`importf`) receives `V`
   as its composed argument; a builtin (`import builtin`) is called as
   `F(V, NULL)`. The result is discarded.

The canonical idiom `EXECUTE(NULL)` is the **no-op**: `NULL` is the
predefined empty object, not a function, so no call is emitted. Any
other `IDENT` must be a declared function (sema rejects an undeclared
name with the same error as a bad function call).

`EXECUTE` fires **only on the condition-false exit**. A `THIS.DIE()`
inside the body returns from the enclosing function before reaching the
exit, so `F` does not run — `EXECUTE` is the subject's death action, not
a finalizer that survives an abrupt `THIS.DIE` (§4.5). The loop never
entering (subject dead at entry) still counts as a condition-false exit,
so `F` runs.

With an `EXECUTE` postfix, the construct terminates with `;`. Without
`EXECUTE`, the closing `}` is the terminator (no `;`).

#### 4.4.5 `V.DIE();` and `V.DIE(RET);`

1. If a `RET` argument is given, read its current binding and update the
   current activation's **pending return value** to point to that object.
2. Read `V`'s current binding, the object `o`.
3. If `V` is the name `THIS`: return from the current activation
   immediately. No subsequent statement in this activation executes. The
   caller receives the activation's pending return value (defaulting to
   `NULL` if never set). For main, the pending return value is discarded
   and the program terminates with OS exit code 0.
4. Otherwise: set `o.alive = false`. This affects only `o` itself — its
   halves, any composites it is part of, and any other aliases of `o`
   continue to observe `o` as dead, but no other object is modified.
5. **If `o` was alive *and* `o.owns_path` is set *and* `o.watch_path`
   is not NULL**, the runtime calls `unlink(o.watch_path)` *before*
   flipping `o.alive` to false. `unlink` errors are silently ignored
   (a file already deleted externally is fine). See §4.7 ext 5 and
   §4.7.1 for the rationale and the full direct-kill rule. This is
   the only path in the runtime that deletes files; passive deaths
   via deadline expiration, dep propagation, watch-path observation,
   one-shot consumption, or signal arrival do not unlink. `close VAR;`
   (§4.4.24) sidesteps step 5 by clearing `owns_path` first.

Step 1 happens before step 3, so `THIS.DIE(THIS);` returns the activation's
own THIS object — the read of `THIS` is well-defined because the kill
hasn't happened yet.

Killing an already-dead object is a no-op.

#### 4.4.6 `print PAYLOAD;`

Write the `PAYLOAD` to standard output, followed by **exactly one** line
feed (`U+000A`) for the whole statement. The payload is the ordered run of
parts produced by §2.4; each part is emitted in source order with no
inter-part separator:

- A **literal part** contributes its already-decoded bytes (its escape
  sequences `\;` `\\` `\n` `\t` `\r` `\$` were resolved at lex time).
- An **interpolation part** `$VAR` reads `VAR`'s current binding and walks
  it as a string per §4.6 — exactly the walk formerly performed by
  `PRINT2` (§4.4.8, removed): decompose each cell, write the recognized
  character atom, stop on a dead object, `NULL`, or the first unrecognized
  left half. A dead, `NULL`, or non-string `VAR` therefore contributes
  nothing (or a truncated prefix); it never aborts the statement or
  crashes (§6.2). To print a numeric payload, convert it with `TO_STRING`
  first (§4.8.2).

A payload with zero parts (`print ;`) writes just the trailing line feed.

`$VAR` is a **read position**: an unbound interpolation variable is a
compile-time error (§6.1), the same as any other read — unlike literal
text, a mistyped `$NAME` is caught rather than printed verbatim.

No flushing guarantees beyond what the C runtime provides. (A single
`print` of N parts is byte-for-byte identical to emitting each part with
no newline and then one final line feed.)

#### 4.4.7 `INPUT VAR;`

1. Read one line of text from standard input. The trailing line feed
   (`U+000A`), and a preceding `U+000D` if present, are stripped.
2. On end-of-file or read error, treat the line as empty.
3. Encode the line as a string per §4.6.
4. Bind `VAR` to the resulting object.

`VAR` is a write target: it must not be `NULL` (§4.2). Lines longer than
the implementation's input buffer (≥ 4096 bytes) are split: the first
buffer-worth becomes the string returned by this call; the remainder is
read by subsequent calls.

#### 4.4.8 `PRINT2 VAR;`

1. Read `VAR`'s current binding, the object `o`.
2. Walk `o` as a string per §4.6: at each cell, decompose to `(l, r)` per
   §4.4.2, look up `l` in the canonical character-atom table, write the
   matched character to standard output, then continue with `r`.
3. The walk stops as soon as any of the following holds:
   - the current object is dead (`ath_is_alive` returns 0), or
   - the current object is `NULL`, or
   - the left half is not a recognized character atom.
4. After the walk, write a single line feed (`U+000A`).

`PRINT2` reads `VAR` but never writes. If `o` is not a well-formed string
(per §4.6) the output is implementation-defined garbage up to the first
unrecognized atom, but `PRINT2` never crashes (per §6.2).

#### 4.4.9 `importf "PATH" as NAME;` and `importf <STEM> as NAME;`

A **compile-time directive**, not a runtime operation. Two forms,
distinguished by the token following `importf`.

**Quoted form** — `importf "PATH" as NAME;`:

1. Open the file at `PATH`, resolved relative to the directory of the
   file containing this statement.
2. Parse its contents as a Program per §3 and register it under `NAME`
   (case-insensitively, per §2.2).
3. Emit no runtime code.

**Search-path form** — `importf <STEM> as NAME;`:

1. Form the candidate filename `STEM + ".ath"`. `STEM` is taken
   case-sensitively (the file system does the matching).
2. Resolve `STEM.ath` against `ATH_PATH` (§5.4): each colon-separated
   directory is tried in order, then the compiler-adjacent `stdlib/`
   directory as a fallback.
3. The first existing file wins; remaining directories are not consulted.
4. Then proceed as in the quoted form.

If the file does not exist (in either form) or fails to parse,
compilation fails. A function registered by `importf` is callable from
any function in the compilation unit, including from inside loops and
from other functions.

A program may register multiple functions under the same name; the last
registration in source order wins. (Subject to revision; see §9.) A
file registered via the search-path form is otherwise indistinguishable
from one registered via the quoted form — both produce ordinary user
functions unless the file itself contains `import builtin` (§4.4.13),
in which case the function is a C-ABI shim.

#### 4.4.10 `FN [L, R] V;` (function call, compose-argument form)

1. Read `L` and `R` from the current scope.
2. Compose them via `ath_compose(L, R)` to a single argument object.
3. Invoke function `FN` (resolved case-insensitively against the function
   registry from §4.4.9) with that argument. The call yields an object.
4. Bind `V` in the current scope to that object.

#### 4.4.11 `FN A [B, C];` (function call, decompose-result form)

1. Read `A` from the current scope.
2. Invoke function `FN` with `A`. The call yields an object `o`.
3. Decompose `o` via `ath_decompose` and bind `B` to the left half, `C` to
   the right half (per §4.4.2 semantics, including lazy half allocation
   if `o` is a leaf).

In both forms, `FN` is matched against the function registry; if no such
function is registered, compilation fails (§6.1).

#### 4.4.12 `watch` — file, signal, pid, and mtime forms

Four forms, dispatched on the first token after `watch`.

**File form** — `watch "PATH" as VAR;`:

Allocates a fresh object whose liveness is tied to the existence of the
file at `PATH`. `PATH` is resolved at runtime (not compile time) against
the program's current working directory.

- If `PATH` exists at allocation time, the object is born **alive**.
- If `PATH` does not exist at allocation time, the object is born
  **dead**.

On every subsequent `ath_is_alive` check, the runtime calls `access(F_OK)`
on the path. If the file is gone, the object transitions to dead and
stays dead — even if the file is later recreated, since death is one-way
(§4.1).

**Signal form** — `watch signal NAME as VAR;`:

Allocates a fresh object whose liveness is tied to a POSIX signal. The
runtime installs a sticky-flag handler for `NAME` (idempotent across
multiple watchers) and the object's `ath_is_alive` check consults the
flag.

- The signal name `NAME` is one of `SIGHUP`, `SIGINT`, `SIGQUIT`,
  `SIGUSR1`, `SIGUSR2`, `SIGPIPE`, `SIGALRM`, `SIGTERM`, `SIGCHLD`
  (case-insensitive). Names outside this set produce a born-dead object
  and a stderr warning at runtime.
- If the signal has not yet been received, the object is born alive.
- If the signal has *already* been received (e.g., by an earlier watcher
  that triggered it), the object is born dead — the flag is sticky.
- Multiple watchers of the same signal all die when the signal arrives.

**Pid form** — `watch pid N as VAR;`:

Allocates a fresh object whose liveness is tied to a running process.
`N` is an identifier bound to a number-payload object; its value is the
pid.

- Born **dead** if `N` has no payload, is non-positive, exceeds `INT_MAX`,
  or names a process that does not exist at allocation time.
- On every `ath_is_alive` check the runtime calls `kill(pid, 0)`. The
  object transitions to dead exactly when that reports `ESRCH` (no such
  process) — i.e. when the process has exited *and been reaped*. A
  permission error (`EPERM`, the process exists but is not signalable) is
  **not** death. A zombie still counts as alive until reaped.

**Mtime form** — `watch mtime "PATH" as VAR;`:

Allocates a fresh object whose liveness is tied to a file's modification
time, captured (seconds + nanoseconds) at allocation.

- Born **dead** if `PATH` does not exist at allocation time.
- On every check the runtime `stat()`s the path; the object dies once the
  mtime differs from the captured value, or the file is gone. This is
  change detection, and like all death it is one-way — the watcher does
  not revive if the file is restored to its original mtime.

**Shared rules** (all forms):

`VAR` must not be `NULL` (§4.2). If `VAR` is already bound, `watch` is a
no-op (idempotent, matching `import`). The contextual keywords `signal`,
`pid`, and `mtime` are recognized only as the second token after
`watch`; elsewhere they are normal identifiers. All four forms are
**monotonic**: file deletion, signal arrival, process exit, and the
first mtime change are permanent, so they never violate one-way death
(§4.1).

#### 4.4.13 `import builtin SYM as NAME;`

A **compile-time directive** that registers a C-ABI function under
`NAME` in the function registry (§4.4.9). `SYM` is the C symbol that
the linker resolves.

1. The C symbol `SYM` must have the signature
   `ath_obj *(ath_obj *, ath_obj *)`. The linker checks this; the
   compiler does not.
2. `NAME` is added to the function registry (case-insensitively).
   Subsequent `NAME [L, R] V;` and `NAME A [L, R];` call forms
   (§4.4.10, §4.4.11) emit a direct call to `SYM` instead of an
   `ath_user_*` thunk.
3. The statement emits no runtime code at its source position.
4. A program may not register the same `NAME` twice with conflicting
   underlying mechanisms (one as C-ABI, one as `importf`'d ~ATH). The
   last registration wins, same rule as §4.4.9.

Built-ins are typically declared in single-file shims under `stdlib/`
and brought into a program via `importf <name> as NAME;` (§4.4.9). The
arithmetic family — `add`, `sub`, `mul`, `div`, `mod`, `to_string`,
`parse` — is shipped this way.

The unresolved-symbol case is a **link-time** error, not a compile-time
one: the compiler trusts that `SYM` will be available when the runtime
archive is linked. Misspelling a runtime symbol surfaces as a linker
error, not as an athc diagnostic.

#### 4.4.14 `import number N as VAR;`

A **runtime operation** that allocates a fresh object carrying an
int64 payload.

1. `N` is an `INT` token (§2.2), parsed as a signed 64-bit integer.
   Out-of-range literals (e.g., `999999999999999999999`) are a
   compile-time error.
2. If `VAR` is already bound: no-op (matches §4.4.1's idempotence).
3. Otherwise: allocate a fresh **eternal-alive** object with
   `has_value = 1` and `value = N`. The object has no deadline,
   no watch path, no awaited signal, no one-shot flag.
4. Bind `VAR` to that object.

Eternal-alive means the object outlives the program by default —
"forty-two doesn't decay." A program that wants a mortal number
composes it with a mortal carrier:

```
import number 42 as N;
import mayfly M;          // dies in 5 min – 1 day
BIFURCATE [N, M] MORTAL;  // MORTAL is alive while M is alive
```

`VAR` must not be `NULL` (§4.2).

#### 4.4.15 `S[N] VAR;` (subscript / element access)

Element access on any object treated as a right-nested cons-list. `S`
is the source, `N` is a number-payload object holding the index, and
`VAR` receives the Nth element of the right-spine walk.

1. Read `S` and `N`.
2. Invoke `ath_index(S, N)` (§5.2).
3. Bind `VAR` to the result.

`ath_index` walks `S` `n` steps to the right (i.e. follows `right`
halves `n` times), then takes the `left` half of the resulting object.
For strings (cons-lists of character atoms terminated with `NULL`),
this yields the Nth **character** — not a length-1 string. To wrap it
into a printable single-char string, use `BIFURCATE [VAR, NULL] STR;`
(§4.4.3).

The result is a fresh *snapshot* of that element, not the element
object itself. Because a string's head is the **canonical** character
atom (§4.6) — shared by every string containing that character —
returning it directly and installing dependencies on it would mutate a
value other expressions also hold, killing that character globally when
`S` dies. Instead `ath_index` clones the element (preserving its
numeric payload and character identity) and installs the dependencies
on the clone, so the indexed result dies with `S` while leaving the
shared atom untouched. The snapshot is still recognized as its
character by `PRINT2` and `ORD`.

The result is born dead if any of the following holds:

- `S` is dead, `NULL`, or unbound.
- `N` is dead, lacks `has_value`, or holds a negative value.
- The walk encounters `NULL` or a dead object before reaching position
  `n` (out-of-range).

A born-dead failure result is a **fresh, payload-less, character-less
dead object** — *not* the `NULL` singleton (§4.2). The distinction is
observable: it exits a `~ATH(VAR)` guard like any dead object and carries
no number or character for downstream arithmetic/`ORD`/`PRINT2`, but
because it is a real object rather than `NULL`, decomposing it with
`BIFURCATE VAR[L,R];` yields two **fresh alive** halves (the dead-source
rule of §4.4.2), not dead ones. Code that means to test "did the
subscript land?" should branch on `VAR`'s liveness, not decompose it.

The result inherits both `S` and `N` as dependencies (§4.8.1), so
killing either invalidates the indexed value on the next observation.

Subscripting is **not** restricted to strings. For any cons-list
shape (lists of numbers, lists of objects, future SPLIT results),
`S[N] X;` reads the Nth right-spine head.

`VAR` must not be `NULL`.

#### 4.4.16 `S[I..J] VAR;` (range / slice)

Right-spine slice. `S` is the source, `I` and `J` are number-payload
objects holding the inclusive start and exclusive end indices, and
`VAR` receives a fresh cons-list of the elements in `S[I..J-1]`,
terminated with `NULL`.

1. Read `S`, `I`, `J`.
2. The compiler emits `ath_compose(I, J)` to form a range pair,
   then calls `ath_inherit_lifetime(range_pair, I, J)` so that the
   pair tracks both endpoints (§4.8.1).
3. Invoke `ath_slice(S, range_pair)` (§5.2). Inside, the runtime
   decomposes the pair to recover `I` and `J`, walks `S` to position
   `I`, and accumulates `J - I` consecutive elements as a new
   right-nested composition terminated with `NULL`.
4. Bind `VAR` to the result.

The result is born dead if:

- `S` is dead, `NULL`, or unbound.
- `I` or `J` is dead, lacks `has_value`, or is negative.
- `I > J` (empty-or-invalid range — empty slice is also dead, by
  design, to keep failure-as-death uniform).
- The walk encounters `NULL` or a dead object before reaching
  position `J` (out-of-range).

As with `S[N]` (§4.4.15), a born-dead slice is a fresh, payload-less,
non-`NULL` dead object: it fails liveness guards but decomposes into
fresh alive halves, so test the slice by its liveness rather than by
decomposing it.

The result inherits `S` and the range pair as dependencies, and the
range pair inherits `I` and `J`, so killing any of `S`, `I`, or `J`
invalidates the slice on the next observation.

For strings, `S[I..J]` is a substring. For a list of numbers, it's
a sublist. The slice's right-spine terminator is always `NULL`,
regardless of what terminated `S`.

`VAR` must not be `NULL`.

#### 4.4.17 `BRANCH(V) { ... } [ELSE] { ... }` (one-shot dispatch)

A non-looping conditional that dispatches on `V`'s liveness exactly
once, then consumes `V`.

1. Read `V`. Compute `alive = ath_is_alive(V)` (or its negation, if
   the `!` inversion form is used).
2. If `alive` is true, execute the first body. Otherwise, execute the
   else body if one is present; if not, execute nothing.
3. After dispatch (whichever body ran, or after no body if none was
   selected), re-read `V` and invoke `ath_die(V)`.

`BRANCH` is the direct sugar over the canonical if-then idiom:

```
~ATH(V) { S* ; V.DIE(); }
```

extended with an optional else block. It is **one-shot**: the
condition is evaluated exactly once, unlike `~ATH` which re-evaluates
every iteration.

**V is consumed.** After a `BRANCH`, `V` is guaranteed dead — whether
the alive body ran (the explicit kill happens) or the dead body ran
(`V` was already dead, the kill is a no-op). This eliminates the
"was V killed?" ambiguity of the `~ATH(V) { ...; V.DIE(); }` idiom
where the kill is buried in the body.

To check `V` without losing it, use `CLONE V as VCHECK;` (§4.4.18)
and `BRANCH(VCHECK) { ... }` on the clone. `V` is then untouched.

If the body rebinds `V` (e.g. `BIFURCATE [NULL, NULL] V;`), the
post-dispatch `ath_die` reads the *current* binding and kills that
object. Same semantics as a literal `V.DIE();` at the end of the
body.

`THIS.DIE(...)` inside a body terminates the function as usual
(§4.4.5); the post-dispatch kill never runs in that case.

The inverted form `BRANCH(!V)` swaps which body runs (the first body
runs when `V` is dead, the else body when `V` is alive). `V` is
still consumed after dispatch.

#### 4.4.18 `CLONE V as W;`

Allocates a fresh object `W` that is a shallow snapshot of `V` at
clone time. `W` and `V` have **independent identity** — killing one
has no effect on the other.

`W` copies, field by field, from `V`:

- `alive` — set to `V`'s **currently observable** liveness, computed
  via the runtime's pure `ath_observe` (the same predicate
  `ath_is_alive` uses, but without flipping any bits or consuming
  one-shots). This means a clone of a verdict whose upstream operands
  have since died is born dead, even if `V`'s raw `alive` bit has not
  yet been refreshed by a direct observation. The clone of a one-shot
  is *not* born dead by the cloning itself — `ath_observe` does not
  trip `is_oneshot`, so a one-shot that has not been directly
  observed yet clones to a fresh, unfired one-shot.
- `left`, `right` — pointer-copied (shared with `V`'s halves; the
  cons-list structure beneath is not deep-copied).
- `has_value`, `value` — full int64 payload copy.
- `deadline_s`, `watch_path`, `is_oneshot`, `awaiting_signal`,
  `dep_mode` — all lifetime extensions and the dep-evaluation mode
  are copied, so the clone has the same intrinsic mortality as the
  original. A clone of a `mayfly` dies at the same deadline; a clone
  of a file-watcher watches the same path; a clone of a `once`
  object is itself a one-shot.

`W` does **not** copy `V`'s `dep1`/`dep2`. Inherited mortality
(`ath_inherit_lifetime` from upstream operands) is *not* preserved
across the clone — `W` is a snapshot at the moment of cloning,
independent of what `V` was tracking. Killing one of `V`'s dep
sources after the clone kills `V` but not `W`. The clone's `alive`
bit captured the dep-walk result at clone time; further dep-source
deaths are not observed by `W`. Because `dep_mode` is copied but
deps are not, an OR-mode clone with no deps degenerates to a plain
alive/dead object that trusts its captured bit.

`CLONE` is the canonical primitive for **non-destructive checking**:

```
CLONE V as VCHECK;
BRANCH(VCHECK) { ... } { ... }   // VCHECK is consumed; V is untouched
```

`W` must not be `NULL`. Cloning `NULL` yields a fresh born-dead
object (alive=0, no payload, no halves).

#### 4.4.19 `sleep N;`

Block the current activation for `N.value` milliseconds, then continue.

1. Read `N`.
2. If `N` is `NULL`, dead, lacks `has_value`, or carries a non-positive
   value, return immediately (no-op).
3. Otherwise, sleep for `N.value` milliseconds using a monotonic clock.
   The implementation uses POSIX `nanosleep`; interrupted sleeps may
   return early. The spec does not guarantee that the full duration
   elapses, only that the runtime does not block longer than `N.value`
   ms plus scheduler jitter.

`sleep` produces no return value and does not consume `N`. The
parameter `N` is read at the start of the call; subsequent mutations
to `N`'s binding do not affect the in-progress sleep.

#### 4.4.20 `TIMER N as T;`

Bind `T` to a fresh alive object that becomes observably dead after
`N.value` milliseconds.

1. Read `N`. If `N` is `NULL`, dead, lacks `has_value`, or carries a
   non-positive value, allocate `T` born dead and return.
2. Otherwise, allocate a fresh alive object with
   `deadline_s = ath_now_s() + N.value / 1000.0` (§4.7 deadline).
3. Bind `T` to that object.

`T` is **independent of `N`** — no dependency is installed. Killing
`N` after the TIMER call does not affect `T`. This is deliberate: the
duration is a parameter consumed at allocation time, not a lifetime
source. Once started, the timer's death is governed solely by the
clock.

`T` must not be `NULL` (§4.2). The combination of `TIMER` and
`~ATH(T)` gives the canonical bounded-loop idiom:

```
import number 5000 as FIVE_SEC;     // 5000 ms = 5 seconds
TIMER FIVE_SEC as T;
~ATH(T) {
    print still running;
    sleep ONE_SEC;
}
print timed out;
```

#### 4.4.21 `read "PATH" as VAR;`

Slurp the entire file at `PATH` into a string-cons-list (§4.6) and
bind it to `VAR`.

1. The runtime opens `PATH` for reading (relative to the program's
   current working directory).
2. On any failure — file does not exist, permission denied, I/O
   error during read — `VAR` is bound to a born-dead object.
3. On success, the file's bytes are turned into a cons-list of
   character atoms (§4.6). The head of the cons-list is a freshly
   allocated wrapper (not subject to intern-mode hash-consing, even
   when `--compose intern` is selected) carrying:
   - `watch_path` set to a copy of `PATH`,
   - **`owns_path` set to 1** (§4.7),
   - the file's bytes as its right-spine.
4. `VAR` is bound to that wrapper.

The wrapper observes the file's continued existence the same way
`watch "PATH" as F;` does (§4.4.12): every `ath_is_alive(VAR)` call
runs `access(PATH, F_OK)` and flips the wrapper to dead if the file
is gone. **Additionally**, because `owns_path` is set, killing the
wrapper via explicit `.DIE()` or BRANCH consumption (§4.4.18)
**deletes the file** (`unlink(PATH)`); see §4.7 for the precise
rule. Death triggered by the watch-path check itself does *not*
attempt to delete (the file is already gone).

To release the file without deleting it, use `close VAR;`
(§4.4.24).

The head's `owns_path` is not propagated by `BIFURCATE` composition
or by `CLONE` (§4.4.18). Derived strings (via subscript,
range subscript, or `CONCAT`) inherit only the watch_path-driven
lifetime through the existing dep machinery — they observe the file
but do not own it.

`VAR` must not be `NULL` (§4.2).

#### 4.4.22 `write SRC to "PATH" [as VERDICT];`

Open `PATH` for writing (truncating any existing file), walk `SRC`
as a string per §4.6, write each character atom's byte, then close
the file.

1. Read `SRC`. If `SRC` is `NULL` or dead, the file is created and
   left empty.
2. Walk `SRC`'s right-spine until reaching `NULL`, a dead cell, or a
   non-character left-half. Each character atom encountered is
   written verbatim. The walk follows the same termination rules as
   `PRINT2` (§4.4.8).
3. If the `as VERDICT` clause is present, bind `VERDICT` to a fresh
   object that is alive iff every step above succeeded (open, all
   writes, close). On any I/O failure `VERDICT` is born dead.
4. Without the `as` clause, the verdict is allocated and discarded.

`to` is a contextual keyword (§2.2). `VERDICT` must not be `NULL`
when the clause is present.

`write` is fire-and-forget by default — no return value, no error
propagation. Capture the verdict if you need to react to failure.

#### 4.4.23 `append SRC to "PATH" [as VERDICT];`

Identical to `write` (§4.4.22) except the file is opened in append
mode: if `PATH` exists, the new bytes are added after the existing
contents; if not, the file is created. Failure semantics and the
optional verdict clause are the same.

#### 4.4.24 `close VAR;`

Release a file-owning object without deleting the file.

1. Read `VAR`. If `NULL` or already dead, this is a no-op.
2. Clear `VAR`'s `owns_path` flag (so the upcoming kill will not
   trigger `unlink`).
3. Set `VAR`'s `alive` field to false.

`close` is the canonical primitive for "I'm done reading this file
but want it to stay." On objects without `owns_path` set, `close` is
indistinguishable from `VAR.DIE();` — it just kills the object.

#### 4.4.25 `text PART+ as VAR;`

Build a string-cons-list (§4.6) from a sequence of literal-string
parts and identifier parts, and bind it to `VAR`.

A **part** is either a STRING literal or an IDENT. The grammar
requires at least one part; the trailing `as IDENT` is the binding
target. The two part-kinds may appear in any order and combination,
e.g. `text "value: " N as MSG;` or `text PREFIX " — " SUFFIX as
MSG;`.

Each part is reduced to a string-shaped object, then all parts are
folded left to right with `ath_concat` (§4.8.4):

1. **STRING part** — the decoded byte sequence (after the §2.3
   escape rules) is turned into a cons-list of character atoms via
   `ath_string_from_bytes`. An empty STRING (`""`) contributes
   `NULL` (the empty string).
2. **IDENT part** — the variable is read from the current scope and
   passed through `ath_coerce_string`. Operands with `has_value`
   set (numbers) are routed through `ath_to_string` to their decimal
   representation. Operands without a payload (existing cons-lists,
   generic composites, `NULL`, character atoms) pass through
   unchanged. The coerced value is then concatenated.

The intermediate `ath_concat` results install operand dependencies
via `ath_inherit_lifetime` (§4.8.1), so the final string inherits
deps from every part transitively. Killing any IDENT part operand
after the call invalidates the resulting string at the next
observation. STRING parts have no upstream operand and contribute
no dep.

`VAR` is a write target; binding `NULL` is a compile-time error
(§4.2). Unlike `import` and similar idempotent forms, `text` is
**not** idempotent: it always overwrites `VAR`. This matches the
implicit "always-fresh" behavior of the underlying `ath_concat`
chain.

The empty case `text "" as V;` binds `V` to `NULL`. The
single-STRING-part case `text "hello" as V;` binds `V` to a fresh
literal cons-list. The single-IDENT case `text N as V;` binds `V`
to `ath_coerce_string(N)`: a fresh TO_STRING result for a
payload-bearing `N` (with `N` installed as a dep), or `N` itself
(pointer-aliased) for any non-payload operand.

#### 4.4.26 `loop N { body }` (count loop)

Runs `body` exactly `N.value` times. `N` is an identifier bound to a
number-payload object.

1. Read `N` and compute the count via `ath_count_of(N)` (§5.2): its
   non-negative int64 payload, or `0` if `N` is dead, lacks a payload,
   or is negative — so a bad or non-positive count runs `body` zero
   times.
2. Iterate, emitting `body` each time, until the counter reaches `0`.

`loop` is **not** a liveness loop: it does not consult `ath_is_alive`,
and rebinding `N` inside the body does not change the remaining count
(the count is snapshotted once on entry). Loops nest, and `body` may
exit early by terminating the activation (`THIS.DIE()`), which returns
from the enclosing function. This is distinct from the string built-in
`repeat` (§4.8.4), which repeats a *string*; the two share no syntax.

#### 4.4.27 `every N { body }` (interval loop)

Runs `body`, sleeps `N.value` milliseconds, and repeats — forever.
`N` is an identifier bound to a number-payload object, re-read each
iteration; the sleep uses the same semantics as the `sleep` statement
(§4.4.19), so a dead/non-positive `N` makes the pause a no-op.

The loop has no liveness guard and no count: the **only** exits are the
body terminating the activation (`THIS.DIE()`) or process death (a
watched signal, `ath_halt`, a fatal signal). It is the
canonical "do this every N ms" daemon construct. Recurrence lives here,
in the loop — never in a liveness object, which could not come back
alive without violating one-way death (§4.1). Code textually after an
`every` whose body never terminates is unreachable.

### 4.5 Program termination

A program terminates when its main activation returns. This happens when:

- `THIS.DIE();` (or `THIS.DIE(RET);`) is executed at the top level, or
- Control falls off the end of the top-level statement list.

In both cases the OS-visible exit code is `0`. The pending return value of
main, if any, is discarded.

A function activation returns when `THIS.DIE();` is executed in its body
or when control falls off the end of its body. Its pending return value
(defaulting to `NULL`) is delivered to the caller as the value of the
function call expression.

### 4.6 String encoding

`INPUT` and `PRINT2` interpret objects as **strings**. A string is a
(possibly empty) sequence of characters, represented as a chain of objects:

- The empty string is `NULL`.
- A non-empty string with first character `c` and tail `t` is the composite
  produced by `BIFURCATE [ATOM(c), t] S;` — i.e. `compose(ATOM(c), t)`.

A **character atom** is an alive object the runtime allocates lazily,
exactly once per distinct character code (0..255). Two strings sharing a
character at any position share the same atom by pointer identity.

Atoms have no observable internal structure: their `left`/`right` halves
are initially unset. Decomposing an atom is permitted but yields freshly
allocated halves that have no meaning as characters — the atom itself
remains the canonical representative for PRINT2's reverse lookup.

The encoding is deliberately the same as drocta `~ATH`'s `getStrObj` /
`getObjStr`, so strings round-trip across implementations.

### 4.7 Lifetime extensions

Every object carries seven optional lifetime conditions in addition to
its explicit `.DIE`-driven mortality:

1. **Deadline.** A monotonic-clock timestamp (in seconds since some
   epoch fixed by the runtime). When set and the runtime's clock
   reaches or passes the timestamp, the object becomes dead at the next
   `ath_is_alive` observation. Once dead, the deadline check is not
   re-evaluated.
2. **Watched path.** A filesystem path. When set, every `ath_is_alive`
   observation calls `access(F_OK)` on the path; if the call fails for
   any reason (file doesn't exist, permission denied, etc.), the object
   becomes dead. Subsequent recreation of the file does not revive it.
3. **Awaited signal.** A POSIX signal number. When set, every
   `ath_is_alive` observation consults a sticky per-signal flag set by
   a process-wide handler; if the signal has been received, the object
   becomes dead. The flag is process-global, so all watchers of the
   same signal die together.
4. **One-shot flag.** When set, the first **direct** `ath_is_alive`
   observation returns alive and atomically flips the underlying
   `alive` field to false; every subsequent observation returns dead.
   Combined with the `~ATH` loop's "re-check before every iteration"
   rule, this causes the body to execute exactly once. "Direct"
   excludes transitive observation through another object's
   dependency walk (§4.8.1) — dep walks use the runtime's pure
   `ath_observe` path, which never consumes one-shots. Only an
   explicit `ath_is_alive(V)` call on the one-shot itself fires it.
5. **Path-ownership flag (`owns_path`).** When set in combination with
   `watch_path`, the object is the **owner** of the underlying file.
   An explicit `ath_die` call on a still-alive owner triggers
   `unlink(watch_path)` *before* the alive bit is cleared. Passive
   deaths via the other extensions (deadline expiration,
   dep-propagation from §4.8.1, watch-path observation, one-shot
   consumption, signal arrival) do **not** trigger unlink — the
   ownership semantics only fire on direct kills. Set exclusively by
   `read "PATH" as VAR;` (§4.4.21). Not copied by `CLONE` (§4.4.18),
   not propagated by `BIFURCATE` composition, not installed by
   `watch "PATH" as VAR;` (which is observation-only).
6. **Watched pid.** A process id. When set, every `ath_is_alive`
   observation calls `kill(pid, 0)`; the object becomes dead exactly
   when that reports `ESRCH` (the process has exited and been reaped). A
   permission error (`EPERM`) is not death, and a zombie counts as alive
   until reaped. Set by `watch pid N as VAR;` (§4.4.12).
7. **Watched mtime.** A filesystem path plus the modification time
   (seconds + nanoseconds) captured at allocation. When set, every
   observation `stat()`s the path; the object becomes dead once the
   mtime differs from the captured value, or the file is gone. Set by
   `watch mtime "PATH" as VAR;` (§4.4.12).

These conditions are independent of the object's `alive` field — they
are *additional* ways an object can be observed dead. An object with
none of them set behaves exactly as in earlier specs (lives until
explicitly killed). An object may have any combination set.

`import NAME... VAR;` sets the deadline when `NAME` matches a
range-based library entry (§5.3), or sets the one-shot flag when
`NAME` matches the special entry `once`. `watch "PATH" as VAR;` sets
the watched path (only). `read "PATH" as VAR;` (§4.4.21) sets the
watched path *and* the ownership flag. `TIMER N as T;` (§4.4.20)
sets the deadline. `watch pid N as VAR;` sets the watched pid and
`watch mtime "PATH" as VAR;` the watched mtime (both §4.4.12). There
are no other surface forms that set these — they are entry-point
allocations, not mutators.

The lifetime sampling is **uniform** over the library entry's range,
seeded by the `ATH_SEED` environment variable if set (decimal unsigned
integer), or by the wall clock otherwise. Setting `ATH_SEED` makes
library-sampled programs deterministic for testing.

### 4.7.1 Direct-kill rules and the unlink trigger

When `V.DIE();` runs on a still-alive `V` (§4.4.5), or when `BRANCH`
consumes a still-alive `V` (§4.4.17), the runtime invokes
`ath_die(V)`. The rule is:

```
if V is alive and V.owns_path is set and V.watch_path != NULL:
    unlink(V.watch_path)                # errors silently ignored
V.alive = false
```

This is the *only* place `unlink` is called by the runtime. All
other deaths — deadline expiration, dep propagation, one-shot
observation, watch-path detection, signal handling — flip the alive
bit without touching the filesystem.

`close VAR;` (§4.4.24) sidesteps this by clearing `owns_path` before
calling the kill primitive, so the resulting `unlink` check fails
and the file persists.

**Warning.** Because `BRANCH(V)` always consumes its subject (§4.4.17),
running a read-result through `BRANCH` deletes the file. If you want
to *check* a read-result without releasing the file, `CLONE` it
first (§4.4.18) — clones never carry `owns_path`.

### 4.8 Numeric payload and built-in arithmetic

An object may carry an int64 **payload** in addition to its alive bit
and halves. Two struct fields hold it:

- `has_value` — nonzero iff a payload is set. Zero by default.
- `value` — signed 64-bit integer. Only meaningful when `has_value` is
  nonzero.

`import number N as VAR;` (§4.4.14) is the only surface form that
allocates with a payload. The runtime built-ins `ath_add` ... `ath_parse`
(§5.2) propagate payloads. The character atoms used by string
encoding (§4.6) do **not** set `has_value` — they are pointer-identified,
not value-identified.

#### 4.8.1 Lifetime inheritance

Derived values inherit death from their operands. The runtime
allocator `ath_inherit_lifetime(result, x, y)` records `x` and `y` as
dependencies of `result`. Every `ath_is_alive(result)` observation then
returns false if either dependency is dead, in addition to checking
`result`'s own alive bit and lifetime extensions (§4.7).

Dependencies are recorded in two slots on `ath_obj` (`dep1`, `dep2`).
Unary built-ins use only `dep1` and pass NULL for `dep2`. Calls beyond
arity 2 are out of scope for v2.

This inheritance is **dynamic**: once an operand dies, the derived
object becomes dead at the next observation, even if it appeared alive
at allocation time. The reverse never holds — dependency-driven death
is one-way like every other form (§4.1).

`BIFURCATE [L, R] V;` composition (§4.4.3) does **not** install
dependencies. This preserves v0/v1 behavior: composites have their
own lifetimes independent of their halves. Only `ath_inherit_lifetime`
installs deps, and only the built-in C functions call it.

#### 4.8.2 Arithmetic built-ins

The runtime exports seven C functions, brought into a program via
`importf <NAME> as NAME;` referencing the corresponding `stdlib/NAME.ath`
shim (§4.4.13).

| Name | Surface call | Result `value` | Born dead when |
|---|---|---|---|
| `add` | `ADD [X, Y] R;` | `X.value + Y.value` | overflow; either operand dead at call |
| `sub` | `SUB [X, Y] R;` | `X.value - Y.value` | overflow; either operand dead |
| `mul` | `MUL [X, Y] R;` | `X.value * Y.value` | overflow; either operand dead |
| `div` | `DIV [X, Y] R;` | `X.value / Y.value` (trunc toward zero) | `Y.value == 0`; `INT64_MIN / -1`; either operand dead |
| `mod` | `MOD [X, Y] R;` | `X.value % Y.value` | `Y.value == 0`; `INT64_MIN % -1`; either operand dead |
| `to_string` | `TO_STRING [N, _] S;` | (string encoding §4.6 of `N.value`) | `N` dead or `has_value == 0` |
| `parse` | `PARSE [S, _] N;` | (int64 parsed from string) | `S` dead, malformed digits, or overflow |

All seven call `ath_inherit_lifetime(R, X, Y)` on success (with `_` =
NULL for unary ops, which records only `X` as a dependency).

"Born dead" objects have `alive = 0`, `has_value = 0`, `value = 0`.
Subsequent arithmetic on a born-dead object propagates death.

The second operand of `to_string` and `parse` is conventionally `NULL`
but any value is accepted and ignored. Using `_` as a placeholder
identifier is a stylistic convention; sema enforces the same in-scope
rule as for any other operand (§6.1).

Overflow detection uses `__builtin_add_overflow` / `__builtin_sub_overflow`
/ `__builtin_mul_overflow` (or their portable equivalents). Division
and modulo specially handle `INT64_MIN / -1` since that wraps in two's
complement. Parse uses `strtoll` with full-string-consumed validation;
trailing non-digit characters fail.

`to_string` produces the canonical decimal representation: optional
leading `-` for negatives, no leading zeros (except for `0` itself), no
thousands separators.

##### Numeric second wave

A further group of `stdlib/` shims over the same int64-payload ABI. Each
installs its operands as deps and is born dead on a dead or
non-payload operand.

| Name | Surface call | Result `value` | Born dead when |
|---|---|---|---|
| `pow` | `POW [X, Y] R;` | `X` raised to `Y` | `Y < 0` (integer exponents only); overflow |
| `abs` | `ABS [X, _] R;` | magnitude of `X` | `X == INT64_MIN` (no positive rep) |
| `neg` | `NEG [X, _] R;` | `-X` | `X == INT64_MIN` (overflow) |
| `min` | `MIN [X, Y] R;` | lesser of `X`, `Y` | — |
| `max` | `MAX [X, Y] R;` | greater of `X`, `Y` | — |
| `gcd` | `GCD [X, Y] R;` | gcd of `\|X\|`, `\|Y\|` (gcd(0,0)=0) | `X` or `Y` is `INT64_MIN` |
| `sign` | `SIGN [X, _] R;` | `-1`, `0`, or `1` | — |
| `band` | `BAND [X, Y] R;` | `X & Y` | — |
| `bor` | `BOR [X, Y] R;` | `X \| Y` | — |
| `bxor` | `BXOR [X, Y] R;` | `X ^ Y` | — |
| `bnot` | `BNOT [X, _] R;` | `~X` (one's complement) | — |
| `shl` | `SHL [X, Y] R;` | `X << Y` (logical) | `Y < 0` or `Y > 63` |
| `shr` | `SHR [X, Y] R;` | `X >> Y` (arithmetic) | `Y < 0` or `Y > 63` |
| `clamp` | `CLAMP [X, PAIR] R;` | `X` confined to `[LO, HI]` | `LO > HI`; `X`/`LO`/`HI` unusable |

`POW` uses exponentiation by squaring with overflow checks at every
multiply; `0^0 == 1`. `SHL` shifts an unsigned copy to avoid
signed-overflow UB; `SHR` is sign-extending. `CLAMP` packs its bounds
`(LO, HI)` into a single composite, exactly like the compose-pair
pattern of §4.8.4 — pass `ENTANGLE [LO, HI] PAIR;` for dep propagation.

#### 4.8.3 Comparisons as verdicts

A **verdict** is an object whose alive bit carries the truth of a
comparison: alive iff true, dead iff false. Verdicts carry no payload
(`has_value == 0`) — they are observed only through `ath_is_alive`,
typically in a `~ATH` loop header:

```
LT [X, Y] V;
~ATH(V) {
    print X is less than Y;
    V.DIE();
}
```

The runtime exports six comparison primitives, brought in via the
search-path importf form:

| Name | Surface call | Alive when | Born dead when |
|---|---|---|---|
| `lt` | `LT [X, Y] V;` | `X.value < Y.value`  | comparison false; either operand dead; either operand has no payload |
| `eq` | `EQ [X, Y] V;` | `X.value == Y.value` | comparison false; either operand dead; either operand has no payload |
| `gt` | `GT [X, Y] V;` | `X.value > Y.value`  | comparison false; either operand dead; either operand has no payload |
| `le` | `LE [X, Y] V;` | `X.value <= Y.value` | comparison false; either operand dead; either operand has no payload |
| `ge` | `GE [X, Y] V;` | `X.value >= Y.value` | comparison false; either operand dead; either operand has no payload |
| `ne` | `NE [X, Y] V;` | `X.value != Y.value` | comparison false; either operand dead; either operand has no payload |

`le`, `ge`, `ne` are primitive rather than derived; this lets a
negated comparison be combined with `AND`/`OR` (below) without
needing a `NOT`-of-verdict construct.

A true verdict's lifetime inherits from both operands via
`ath_inherit_lifetime(V, X, Y)` (§4.8.1): if either operand dies after
the comparison, `V` becomes dead at the next observation. A false
verdict is allocated dead from the start, and its dependencies are
not recorded (dead objects do not become alive again).

Comparison of objects without `has_value` set — strings, generic
composites, `NULL` — always yields a born-dead verdict. Object-identity
comparison ("is X the same object as Y") is a deliberately separate
question and is not in scope for v2; the verdict primitives compare
int64 payloads only.

##### Logical combinators

Two logical combinators over verdicts are exposed as builtins through
the same shim pattern as the comparisons:

| Name | Surface call | Alive when | Born dead when |
|---|---|---|---|
| `and` | `AND [X, Y] V;` | both `X` and `Y` alive at every observation | either operand dead at call |
| `or`  | `OR  [X, Y] V;` | at least one of `X`, `Y` alive at every observation | both operands dead at call |

`AND` uses the existing conjunctive dep machinery — it allocates an
alive verdict and calls `ath_inherit_lifetime(V, X, Y)`, so the
result becomes dead at the next observation as soon as either operand
dies, and stays dead.

`OR` allocates an alive verdict, installs both operands in `dep1`
and `dep2`, and sets `dep_mode = ATH_DEP_OR` (§5.1) so `ath_is_alive`
treats them disjunctively: the result stays alive as long as at least
one dep is alive, and only flips its own `alive` to `0` once both are
observed dead. After that flip the result is permanently dead (§4.1).

There is **no `NOT` combinator.** A materialized `NOT(V)` would
require a dead→alive transition when `V` later dies, which §4.1
forbids. Negation is expressed at the observation site instead, via
`~ATH(!V)` (§4.4.4) or `BRANCH(!V)` (§4.4.17). Where the negated
verdict needs to be combined with another, use the primitive
contrapositive (`X >= Y` instead of `NOT (X < Y)`) and feed that
verdict into `AND`/`OR` directly.

#### 4.8.4 String operations

Strings are right-nested cons-lists of character atoms (§4.6). The
runtime exports operations over them in two waves. The first wave —
the table immediately below — covers access, measurement, and
search-and-edit: `length` and `concat` as `stdlib/` function calls;
`index` and `slice` as dedicated statement syntax (§4.4.15, §4.4.16);
`find` as a function call and `replace`, `replace_all` using the
compose-pair pattern described below. The second wave — predicates,
transforms, and structural reshaping — is documented in the
"Predicates, transforms, and structural operations" subsection
further down; every entry there is a `stdlib/` function call over the
fixed two-operand builtin ABI (§4.4.13).

| Name | Surface form | Result | Born dead when |
|---|---|---|---|
| `length` | `LENGTH [S, _] N;` | int64 payload = number of right-spine elements walked before hitting `NULL` or a dead object | `S` is `NULL` (yields `0` rather than dead) — never dead unless walk encounters a dead non-`NULL` cell |
| `concat` | `CONCAT [A, B] R;` | fresh cons-list: elements of `A` followed by elements of `B`, terminated with `NULL` | `A` or `B` is dead at the call |
| index | `S[N] X;` (§4.4.15) | the Nth right-spine head of `S` | `S`/`N` dead or unbound; `N` has no payload or is negative; walk hits `NULL`/dead before position `N` |
| slice | `S[I..J] X;` (§4.4.16) | fresh cons-list of elements `I..J-1`, terminated with `NULL` | `S`/`I`/`J` dead; `I` or `J` has no payload or is negative; `I > J`; walk hits `NULL`/dead before `J` |
| `find` | `FIND [HAY, NEEDLE] IDX;` | int64 payload = 0-indexed position of the first occurrence of `NEEDLE` in `HAY` | NEEDLE not present in HAY; HAY or NEEDLE dead; non-character atom encountered during slurp |
| `replace` | `REPLACE [S, PAIR] R;` (see compose-pair below) | fresh cons-list with the first occurrence of `NEEDLE` in `S` replaced by `REPLACEMENT` | NEEDLE not present in S; NEEDLE is empty; S/PAIR dead; non-character atom during slurp |
| `replace_all` | `REPLACE_ALL [S, PAIR] R;` | fresh cons-list with every non-overlapping occurrence of `NEEDLE` replaced by `REPLACEMENT` | same failure modes as `replace` |

##### Compose-pair pattern

`REPLACE` and `REPLACE_ALL` conceptually take three arguments
(source, needle, replacement) but the builtin FFI signature
(§4.4.13) is fixed at two `ath_obj *` inputs. The convention,
identical to how the range-subscript form `S[I..J]` lowers in
§4.4.16, is to pack `NEEDLE` and `REPLACEMENT` into a single
composite and pass that composite as the second argument. Two
forms of composition are available, distinguished by whether the
resulting `PAIR` inherits its operands as deps.

**Recommended form — `ENTANGLE`** (the `ath_entangle` builtin
shipped as `stdlib/entangle.ath`):

```
ENTANGLE [NEEDLE, REPLACEMENT] PAIR;
REPLACE [S, PAIR] R;
```

`ENTANGLE` performs the same composition as `BIFURCATE [L, R] V;`
and additionally calls `ath_inherit_lifetime(PAIR, NEEDLE,
REPLACEMENT)` (§4.8.1). Killing `NEEDLE` or `REPLACEMENT` after the
`REPLACE` call invalidates `PAIR` on the next observation, which in
turn invalidates `R` through the standard dep chain.

**Alternative — plain `BIFURCATE`:**

```
BIFURCATE [NEEDLE, REPLACEMENT] PAIR;
REPLACE [S, PAIR] R;
```

`BIFURCATE` composition does *not* install deps. `PAIR` carries no
internal dependency on `NEEDLE` or `REPLACEMENT`; killing either
after the call does not propagate death to `R`. Use this form when
the carrier composite must outlive its operands — an uncommon
requirement for the search-and-replace use case but available for
advanced use.

Inside the runtime, `ath_replace` and `ath_replace_all` decompose
the pair to recover `NEEDLE` and `REPLACEMENT`, then perform the
search-and-substitute pass. Both work identically with either form
of composition; the only difference is the resulting dep chain on
`R`.

##### Other behavior shared across the family

`LENGTH` on `NULL` returns the eternal payload object `0` (not a
dead result) — the empty string is a real cons-list with a known
length. This is the one place "absent" is distinguished from
"failed."

All seven operations install operand dependencies on their results
via `ath_inherit_lifetime` (§4.8.1) — within the limits noted for
the compose-pair pattern above. Slicing a string and then killing
the source kills the slice on the next observation.

In `intern` composition mode (§4.4.3), concatenated, sliced, and
replaced results allocate fresh cons cells; sharing only happens at
the char-atom level (which is canonical regardless of mode). Equal
results produced by separate operations remain distinct objects.

`LENGTH`'s second operand and `INDEX`/`SLICE`'s output type follow
the same conventions as the arithmetic ops (§4.8.2): unary calls
pass `NULL` (or any name) as the ignored second argument, and the
chain dies on the first failure.

**Empty needle.** `FIND` with an empty `NEEDLE` returns `0`
(the empty string is a prefix of any string at position 0).
`REPLACE` and `REPLACE_ALL` with an empty `NEEDLE` return a
born-dead `R` — "replace nothing with something" is deliberately
under-defined (Python's behavior of inserting at every position
boundary is surprising and rarely what users want; sed rejects
it). Use `CONCAT` if you want to prepend or append text.

##### Predicates, transforms, and structural operations

The second wave of string operations. Each is a `stdlib/` shim over a
two-operand builtin. They fall into three kinds:

- **Predicates** return a *verdict* — alive iff the relation holds,
  dead otherwise (§4.8.3) — with both operands installed as deps via
  `ath_verdict_true`. They are born dead on a malformed string: a
  cell whose left half is not a recognized character atom encountered
  partway through the walk.
- **Transforms** return a fresh cons-list with the source installed as
  a dep. `NULL` in yields `NULL` out.
- **Structural** ops (`split`, `join`) move between a string and a
  cons-list of strings.

| Name | Surface form | Kind | Result | Born dead when |
|---|---|---|---|---|
| `streq` | `STREQ [A, B] V;` | predicate | alive iff `A` and `B` are byte-identical | malformed `A` or `B` |
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

**Empty-string boundary cases.** Every string starts and ends with the
empty string: `STARTSWITH`/`ENDSWITH` with an empty (`NULL`) prefix or
suffix yield an *alive* verdict, and `STREQ [NULL, NULL]` is alive.
Whitespace for the strip family is space, tab, LF, and CR.

**`split` details.** The empty separator is born dead — "split on
nothing" is under-defined, matching `replace`'s empty-needle rule
above. A trailing `SEP` yields a trailing empty-string element, so
`SPLIT` of `"a,b,"` on `","` is a three-element list `["a", "b", ""]`.
The result is terminated by `NULL` and inherits both `S` and `SEP` as
deps.

**`join` details.** `JOIN` walks `LIST`'s right spine; for each cell it
slurps the left half as a string into the output, appending `SEP`
between cells but not after the last. An empty `SEP` is permitted and
concatenates with no separators. An empty `LIST` (`NULL`) returns
`NULL`. `SPLIT` and `JOIN` are inverses when `SEP` is non-empty and
does not occur inside any element.

Malformed input is distinguished from empty: a transform whose source
is dead or contains a non-character atom returns `NULL` (treated as
the empty result), while a predicate over a malformed operand is born
dead.

##### Search, measurement, construction, and the atom bridge

A further group of `stdlib/` shims, all over the two-operand ABI.
Search/measurement parallels `find`; construction builds fresh
cons-lists (like `concat`); the atom bridge moves between the
character atoms that `S[N]` (§4.4.15) yields and their integer codes.

| Name | Surface form | Result | Born dead when |
|---|---|---|---|
| `contains` | `CONTAINS [HAY, NEEDLE] V;` | verdict, alive iff `NEEDLE` occurs in `HAY` | dead operand; malformed string. (Absence → dead verdict, not error.) Empty needle → alive |
| `count` | `COUNT [HAY, NEEDLE] N;` | int64 payload = number of non-overlapping occurrences (`0` if none, *alive*) | empty `NEEDLE`; dead operand; non-character atom |
| `rfind` | `RFIND [HAY, NEEDLE] IDX;` | int64 payload = index of the *last* occurrence | `NEEDLE` absent; dead operand; non-character atom. Empty needle → `len(HAY)` |
| `repeat` | `REPEAT [S, N] R;` | fresh cons-list = `S` repeated `N` times | `N` < 0 or has no payload; `S` dead. `N == 0` → `NULL` |
| `reverse` | `REVERSE [S, _] R;` | fresh cons-list with `S`'s characters reversed | `S` dead/malformed (→ `NULL`) |
| `pad_left` | `PAD_LEFT [S, N] R;` | `S` left-padded with spaces to width `N` (copy of `S` if already ≥ `N`) | `N` < 0 or has no payload; `S` dead |
| `pad_right` | `PAD_RIGHT [S, N] R;` | `S` right-padded with spaces to width `N` | as `pad_left` |
| `ord` | `ORD [A, _] N;` | int64 payload = code (0..255) of the character atom `A` | `A` is not a character atom; `A` dead |
| `chr` | `CHR [N, _] S;` | length-1 string whose character has code `N` | `N` < 0, `N` > 255, no payload, or dead |

**Search semantics.** `COUNT` matches non-overlapping, left-to-right:
`COUNT` of `"aaaa"` for `"aa"` is `2`, not `3`. Unlike `find`, a zero
count is a live `0` payload, not a dead result — "absent" and "failed"
coincide for `find`/`rfind` (born dead) but not for `count`. The empty
needle follows §4.8.4's established split: present everywhere
(`CONTAINS` alive, `RFIND` at `len`), but born dead for `COUNT`.

**Construction.** `REPEAT` and the `PAD` ops take a number payload as
their second operand (the same convention as the range endpoints in
§4.4.16). Padding always uses the space character `0x20` and never
truncates — widening past `len(S)` is a no-op copy. All three build
fresh cons cells and inherit both operands as deps.

**Atom bridge.** `ORD` is the inverse of `CHR`. `ORD` consumes a single
character *atom* — the value `S[N]` (§4.4.15) yields, not a length-1
string — and `CHR` produces a length-1 string. To go from a one-char
string to a code, subscript it first (`S[0]` then `ORD`); to print a
`CHR` result, it is already a string. Codes are byte values (0..255),
matching the string encoding of §4.6. Because `S[N]` returns a
*snapshot* of the character rather than the canonical atom (§4.4.15),
`ORD` of an `S[N]` result is unaffected by the liveness of other
strings sharing that character.

##### String polish

The remaining `stdlib/` string shims: a three-way compare, a
string-valued subscript, an offset search, two case transforms, a
custom-charset strip family, and custom-fill padding.

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

`COMPARE` is the three-way form of the `strlt`/`streq`/`strgt`
verdicts — useful as a sort key. `CHAR_AT` complements `S[N]`: the
subscript yields a character atom (for `ORD`), `CHAR_AT` a printable
length-1 string. `FIND_FROM`, `CLAMP` (§4.8.2), and the `PAD_*_WITH`
ops all use the compose-pair convention of the search-and-replace
family: pack the two trailing arguments with `ENTANGLE` (dep
propagation) or `BIFURCATE`. The fill for padding is the first
character of `FILL`; widening past `len(S)` is a no-op copy. `TITLE`
treats only whitespace as a word boundary, so `"abc-def"` titlecases
to `"Abc-def"`.

#### 4.8.5 Time and randomness built-ins

Two function-call builtins read the runtime clock and the random
source. Both are brought in via `importf <NAME> as NAME;` against
the corresponding `stdlib/NAME.ath` shims.

| Name | Surface call | Result `value` | Born dead when |
|---|---|---|---|
| `now` | `NOW [_, _] T;` | monotonic milliseconds since boot | never (always alive) |
| `random` | `RANDOM [LO, HI] R;` | uniform-ish int64 in `[LO.value, HI.value)` | LO or HI dead; either lacks payload; `LO.value >= HI.value` |

`NOW` ignores both operands; convention is to pass `NULL` for both.
Each call returns a fresh number-payload object. The result is
**not** dep-tracked against its operands — `NOW` is a clock reading,
not a derived value.

`RANDOM` produces a uniformly-distributed value over the half-open
interval `[LO, HI)`. The random source is the global `rand()`
seeded at runtime startup by `ATH_SEED` (if set) or by the wall
clock (§4.7). Multiple `rand()` calls are combined to span the full
int64 range; the distribution has minor modulo bias for very wide
ranges but is uniform for any practical use.

`RANDOM` results are **not** dep-tracked against `LO`/`HI` either —
the bounds are parameters, not lifetime sources. Once a random
value has been drawn, killing the bounds does not invalidate it.

`NOW` is monotonic — successive calls within an activation observe
non-decreasing values. The zero point is the system's monotonic
clock origin (typically boot), not a wall-clock epoch. Programs that
care only about elapsed time between two readings can subtract.

#### 4.8.6 Generic list operations

A **list** is any right-nested cons-list (§4.6) — the same shape as a
string, but with arbitrary objects as the left-half elements. These
`stdlib/` shims walk the right-spine and read each element's int64
payload, so they operate on lists of numbers (and born-die on a
string, whose elements are payload-less character atoms). Build a list
with `BIFURCATE`: `BIFURCATE [HEAD, REST] LIST;`.

| Name | Surface call | Result | Born dead when |
|---|---|---|---|
| `sum` | `SUM [LIST, _] N;` | Σ of element payloads (empty → `0`) | a non-payload or dead element; overflow |
| `product` | `PRODUCT [LIST, _] N;` | Π of element payloads (empty → `1`) | a non-payload or dead element; overflow |
| `maximum` | `MAXIMUM [LIST, _] N;` | greatest element payload | empty list; non-payload/dead element |
| `minimum` | `MINIMUM [LIST, _] N;` | least element payload | empty list; non-payload/dead element |
| `member` | `MEMBER [LIST, X] V;` | verdict, alive iff some element's payload equals `X`'s | `X` lacks a payload (→ dead verdict) |
| `take` | `TAKE [LIST, N] R;` | fresh list of the first `N` elements (all of `LIST` if `N >= length`) | `N < 0` or no payload; dead `LIST`. `N == 0` → `NULL` |
| `drop` | `DROP [LIST, N] R;` | fresh list of all but the first `N` elements | `N < 0` or no payload; dead `LIST`. `N >= length` → `NULL` |

`SUM`/`PRODUCT` use the empty-list **identity** (0 and 1), mirroring
`LENGTH`'s "empty is real" rule (§4.8.4); `MAXIMUM`/`MINIMUM` instead
born-die on an empty list, since there is no extremum. `MEMBER`
compares by payload, so it finds numbers, not arbitrary sub-objects.
`TAKE`/`DROP` allocate fresh cons cells (like `CONCAT`/`SLICE`) and
inherit `LIST` and `N` as deps. There is no `map`/`filter`/`reduce`:
~ATH has no first-class functions to pass, so transformation stays at
the level of these fixed folds. `LENGTH` (§4.8.4) and the subscript
forms `S[N]` / `S[I..J]` (§4.4.15–16) already cover length, indexing,
and slicing for lists as well as strings.

**Dead backbone vs. dead element.** Every generic list operation walks
the right-spine under one guard: a cell is visited only while it is
non-`NULL` **and alive**. The two ways a list can be "partially dead"
therefore behave differently:

- A **dead spine cell** (a cons cell on the backbone whose `alive` bit
  is clear) acts as a **terminator**, indistinguishable from the `NULL`
  end of the list. The walk stops *before* it; every element from that
  cell onward is invisible. `LENGTH` counts only the live prefix; the
  folds, `MEMBER`, and `TAKE`/`DROP` all see just that prefix. Killing a
  backbone cell thus silently truncates the list at that point.
- A **dead element** (the `left` head of a still-live spine cell) is not
  a terminator — the walk continues past it — but its treatment is
  per-operation: `SUM`/`PRODUCT`/`MAXIMUM`/`MINIMUM` born-die on it (a
  dead or payload-less element poisons the whole fold); `MEMBER` simply
  fails to match it and keeps scanning; `TAKE`/`DROP` copy the element
  through **as-is**, so a dead element survives structurally in the
  fresh result. `ALL_OF`/`ANY_OF` read elements as lifetimes, so a dead
  element directly drives the combined verdict (see below).

This matches `LENGTH`'s "empty is real, but a dead spine cell stops the
count" rule (§4.8.4): liveness gates the **backbone**, while element
liveness is a value-level concern each fold decides for itself.

##### n-ary lifetime combinators

Two further shims fold the AND/OR verdict combinators (§4.8.3) over a
**list of lifetimes** — useful for "wait for all of these" or "any of
these" without manually nesting `AND`/`OR`.

| Name | Surface call | Result | Empty list |
|---|---|---|---|
| `all_of` | `ALL_OF [LIST, _] V;` | verdict alive iff **every** element of `LIST` is alive — dies when the first element dies | alive (vacuous) |
| `any_of` | `ANY_OF [LIST, _] V;` | verdict alive iff **some** element of `LIST` is alive — dies only when the last does | dead |

The result is **dependency-tracked**, not a point-in-time snapshot:
`ALL_OF` folds `ath_and` from a fresh always-alive identity, `ANY_OF`
folds `ath_or` from a fresh dead identity, so the verdict is a tree of
`AND`/`OR` nodes (§4.8.1) over the elements. Killing any element after
the call propagates through the tree to invalidate the combined verdict
on its next observation — exactly as a nested `AND`/`OR` would. The
elements are read as lifetimes, not payloads, so the lists need not be
numbers.

There is no `none_of`/value-level "is dead": a verdict that went from
dead to alive as its operand died would contradict the monotonic-death
invariant (§4.7) — once observed dead, an object stays dead. Negation
lives only at the loop level, in the inverted `~ATH(!V)` form (§4.4.10).

---

## 5. Runtime ABI

The compiler emits LLVM IR that calls a small set of runtime functions
implemented in C. The codegen MUST NOT inline the bodies of these functions
or bypass them with direct struct manipulation. They are the swap points
that let us evolve semantics without touching the frontend.

### 5.1 Types

```c
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

    /* §4.8 numeric payload */
    int            has_value;
    int64_t        value;

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
     * subscript form S[N], §4.4.15) is still recognized as that character. */
    int            is_char;
    int            char_code;

    /* §4.4.12 extended watch sources, appended after the codegen-modeled
     * prefix. watch_pid > 0 ties liveness to a running process (dies when
     * kill(pid,0) reports ESRCH). mtime_path, when non-NULL, ties liveness
     * to a file's modification time captured at allocation. Both are
     * monotonic — process exit and the first mtime change are permanent. */
    int            watch_pid;
    const char    *mtime_path;
    int64_t        mtime_sec;
    int64_t        mtime_nsec;
} ath_obj;
```

The exact field order is an ABI commitment to the codegen — `alive`,
`left`, `right` MUST remain the first three fields. The optional
extension fields MAY be reordered or extended in subsequent revisions.

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
 * ath_print / ath_print_obj are those plus one line feed. The unified
 * `print` statement (§4.4.6) emits its parts via the raw forms and adds a
 * single trailing newline for the whole statement. */
void     ath_print(const char *text, size_t len);
void     ath_print_bytes(const char *text, size_t len);
ath_obj *ath_input_line(void);
void     ath_print_obj(ath_obj *s);
void     ath_print_obj_raw(ath_obj *s);
ath_obj *ath_char_atom(int c);

/* Literal-string + coercion helpers used by the `text` statement (§4.4.25). */
ath_obj *ath_string_from_bytes(const char *bytes, size_t len);
ath_obj *ath_coerce_string(ath_obj *v);

/* Lifetime extensions (§4.7) */
ath_obj *ath_alloc_with_lifetime(double min_s, double max_s);
ath_obj *ath_alloc_watching_file(const char *path);
ath_obj *ath_alloc_watching_signal(int signum);
ath_obj *ath_alloc_watching_signal_by_name(const char *name);
ath_obj *ath_alloc_watching_pid(ath_obj *n);      /* §4.4.12 watch pid  */
ath_obj *ath_alloc_watching_mtime(const char *p); /* §4.4.12 watch mtime */
ath_obj *ath_alloc_oneshot(void);
ath_obj *ath_alloc_from_library(const char *name);
int      ath_library_lookup(const char *name, double *min_out, double *max_out);
/* Registers a runtime lifetime-library entry; emitted in main's prologue
 * for each built-in and -D/--define-lifetime range (§5.3.1). */
void     ath_register_lifetime(const char *name, double min_s, double max_s);

/* Numeric payload + arithmetic (§4.8) */
ath_obj *ath_alloc_number(int64_t v);
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

/* Logical combinators over verdicts (§4.8.3). NOT is not provided —
 * see the §4.8.3 commentary. */
ath_obj *ath_and(ath_obj *x, ath_obj *y);
ath_obj *ath_or(ath_obj *x, ath_obj *y);

/* Compose with dep propagation (§4.8.4). Equivalent to ath_compose
 * followed by ath_inherit_lifetime, in one call. Use for the
 * compose-pair pattern (notably (needle, replacement) for REPLACE)
 * when the carrier composite must die if either operand dies. */
ath_obj *ath_entangle(ath_obj *x, ath_obj *y);

/* String operations (SPEC §4.8.4). All install operand deps on results
 * via ath_inherit_lifetime. ath_index and ath_slice are also invoked
 * by the subscript and range-subscript statement codegen. */
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

/* Shallow clone for non-destructive checking (SPEC §4.4.18). Copies all
 * fields of v except dep1/dep2, which are zeroed. */
ath_obj *ath_clone(ath_obj *v);

/* Time and timer (SPEC §4.4.19, §4.4.20, §4.8.5). All times are int64
 * milliseconds. ath_sleep_ms is called directly by sleep-stmt codegen
 * and is a no-op if n is dead/no-payload. ath_alloc_timer_ms produces
 * an alive object with a deadline; the duration is not dep-tracked. */
void     ath_sleep_ms(ath_obj *n);
ath_obj *ath_alloc_timer_ms(ath_obj *n);
ath_obj *ath_now(ath_obj *a, ath_obj *b);
ath_obj *ath_random_range(ath_obj *lo, ath_obj *hi);

/* File I/O (SPEC §4.4.21-24, §4.7 ext 5). ath_alloc_read_file slurps the
 * file into a cons-list whose head is a non-interned wrapper carrying
 * watch_path and owns_path=1. ath_write_file/ath_append_file return a
 * fresh verdict object. ath_close clears owns_path before killing v so
 * the file is not unlinked. ath_die is modified to unlink the file when
 * a still-alive owner is killed via explicit .DIE() or BRANCH. */
ath_obj *ath_alloc_read_file(const char *path);
ath_obj *ath_write_file(ath_obj *s, const char *path);
ath_obj *ath_append_file(ath_obj *s, const char *path);
void     ath_close(ath_obj *v);

/* Extracts a non-negative int64 iteration count from a number object,
 * clamped at 0 for dead/payload-less/negative inputs. Called by the
 * `loop N` and `every N` statement codegen (§4.4.26-27). */
int64_t  ath_count_of(ath_obj *n);

/* program control */
void     ath_halt(void) __attribute__((noreturn));

/* the singleton dead object */
extern ath_obj *ath_NULL;
```

`ath_compose` is the **(A)/(B) swap point**. Two implementations are
provided as separate runtime archives, selected at link time:

- `libath_fresh.a` — `ath_compose` always allocates. Default.
- `libath_intern.a` — `ath_compose` hash-conses by raw pointer pair.

The compiler driver flag `--compose fresh|intern` selects which archive
is linked. Both share `runtime_common.o` (everything except
`ath_compose`).

`ath_decompose` is the swap point for lazy-halves vs. strict semantics.
`ath_die` is the swap point for any future cascading-death rule.
`ath_is_alive` is the swap point for any future structural- or derived-
liveness rule. None of these alternative behaviors are implemented in v0.

`ath_alloc_with_lifetime` allocates a fresh alive object and arranges
for it to become observably dead after a uniform-random delay in the
half-open interval [`min_s`, `max_s`] seconds. A sample of zero or less
results in a born-dead object. Samples larger than `1e308` are clamped.

`ath_alloc_watching_file` allocates a fresh object whose liveness is
gated on `access(F_OK)` for `path`. If the file does not exist at
allocation time, the object is born dead.

`ath_alloc_oneshot` allocates a fresh object whose `is_oneshot` flag is
set. The first observation by `ath_is_alive` returns alive and flips
the underlying `alive` field to false; every subsequent observation
returns dead. The library exposes this allocator via the special name
`once`.

`ath_alloc_from_library` looks `name` up case-insensitively. The
special name `once` dispatches to `ath_alloc_oneshot`; range-based
names dispatch to `ath_alloc_with_lifetime`; misses fall through to
`ath_alloc_alive`. `ath_library_lookup` returns only range-based hits
— it does not recognize `once`.

### 5.3 Lifetime library

The runtime ships a fixed table mapping case-insensitive concept names
to lifetime ranges in seconds. The current contents — spanning roughly
ten microseconds to 10^110 seconds, with a mix of low- and high-variance
entries — are listed below. Implementations MAY add entries but MUST
preserve the named ones with at least the documented ranges.

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

Names with `min == max` have zero variance; names with `min == 0` have
the possibility of being born dead. The library is intentionally
suggestive — `import author Karkat;` and `import dead universe U;` both
do something meaningful — without trying to be a complete ontology.

#### 5.3.1 User-extended entries

The compiler accepts `-D NAME:MIN:MAX` (long form
`--define-lifetime`) on the command line, repeatable. Each occurrence
registers an additional library entry in the resulting binary:

```
athc -D "tortoise:50:150" -D "soap bubble:1:5" prog.ath -o prog
```

- `NAME` is the concept name (same matching rules as built-ins; spaces
  permitted; matched case-insensitively against the joined `import`
  metadata).
- `MIN` and `MAX` are non-negative floats in seconds, with `MIN <= MAX`.

Mechanism: the compiler emits calls to `ath_register_lifetime` at the
very top of `main`, before any user code runs. The runtime stores
user-registered entries in a separate table consulted *before* the
built-in table by `ath_library_lookup`, so a user entry overrides any
built-in of the same name. There is currently no way to register
non-time-based entries (e.g., the `once`-style flag) from the CLI; that
is fixed in the runtime.

Implementation limit: an implementation MAY refuse to register more
than 64 user entries per program. The reference runtime emits a
diagnostic to stderr and silently ignores excess entries beyond that
limit.

`ath_halt` is invoked exactly when `THIS.DIE();` executes. Implementations
typically call `_exit(0)`.

### 5.4 Search path

The search-path form of `importf` (§4.4.9) resolves bare stems against
an ordered list of directories:

1. Each entry of the `ATH_PATH` environment variable, colon-separated,
   in order.
2. The compiler-adjacent `stdlib/` directory (next to the `athc`
   package — same parent as `runtime/`).

The first directory that contains `STEM.ath` wins. Relative paths in
`ATH_PATH` are resolved against the current working directory at
compile time.

Programs that ship without depending on `ATH_PATH` only see the
default `stdlib/`, which is sufficient for all built-in arithmetic
and any future standard-library additions. Programs that depend on
project-local helpers should prefer quoted `importf` (relative to the
importing file) over manipulating `ATH_PATH`.

---

## 6. Errors

v0 errors fall into two classes:

### 6.1 Compile-time errors

- Lexical: unterminated `/*`, unterminated `"..."`, missing space after
  `print`, illegal character.
- Syntactic: any deviation from the grammar in §3. This includes a
  concept `import` with no metadata word (a bare `import VAR;`): a
  concept import requires at least one metadata word before `VAR`
  (§4.4.1).
- Reference to an unbound name in any read position, checked syntactically:
  a name is in scope if introduced by some preceding statement in the same
  block or an enclosing block. The scope is per-activation — function
  bodies have their own scope starting with `THIS`, `NULL`, `ARGS`.
- Reference to an unknown function in a `funcall-stmt`: matched
  case-insensitively against names registered by `importf` or
  `import builtin` (§4.4.13).
- Binding `NULL` (any case variant in a write position) is rejected per §4.2.
- File-not-found or parse error in an `importf` target. For the
  search-path form (§4.4.9), "not found" means no `ATH_PATH` entry and
  no compiler-adjacent `stdlib/` contains `STEM.ath`.
- An `INT` literal in `import number` (§4.4.14) that does not fit
  signed 64-bit range.
- `watch` paths are *not* validated at compile time; missing files cause
  the watching object to be born dead at runtime, never a compile error.
- Missing C symbols declared by `import builtin` (§4.4.13) surface as
  **link-time** errors, not compile-time. The compiler trusts the
  symbol will be resolved when `libath_<mode>.a` is linked.

### 6.2 Run-time behavior

There are no run-time errors in v0. A program that passes the §6.1 checks
either runs to completion or runs forever — it cannot crash, abort, or
produce a diagnostic from the runtime.

This guarantee survives even when §6.1's syntactic check admits a read of a
variable whose introducing statement happens to lie on an unexecuted
control-flow path (e.g., inside a `~ATH` body that runs zero times). Such
reads yield `NULL` (§4.2), and every operation in §4.4 is defined on `NULL`.

---

## 7. Example: canonical countdown (from drocta `looptest.~ATH`)

```
import blah A;
BIFURCATE A[Z,A];
BIFURCATE A[G,A];
BIFURCATE [Z,G]N;
BIFURCATE A[G,A];
BIFURCATE [G,N]N;
BIFURCATE A[G,A];
BIFURCATE [G,N]N;
BIFURCATE A[G,A];
BIFURCATE [G,N]N;
BIFURCATE A[G,A];
BIFURCATE [G,N]N;
BIFURCATE [Z,N]N;

BIFURCATE N[Z,N];
~ATH(Z){
    BIFURCATE N[D,N];
    D.DIE();
    print APPLE;
    print ORANGE;
}
THIS.DIE();
```

Under v0 semantics this:
1. Builds a list-encoded "counter" `N` whose left half `Z` is the canonical
   "alive sentinel" and whose right half is a chain.
2. The `~ATH(Z)` loop runs while `Z` (the sentinel) is alive; each iteration
   peels one element off `N`, kills it, prints two lines.
3. Eventually a peel rebinds `Z` to a dead object (the last cell's terminator)
   and the loop ends.
4. `THIS.DIE();` halts the program.

This program is the v0 conformance target.

### 7.2 Function call (v1)

`hello.ath`:
```
import unused U;
~ATH(U) {
    print Hello from a function.;
    THIS.DIE(THIS);
}
THIS.DIE();
```

`main.ath`:
```
importf "hello.ath" as HELLO;
import a A;
import b B;
HELLO [A, B] R;
THIS.DIE();
```

Compiling `main.ath` produces a binary that prints `Hello from a function.`
once and exits. The `HELLO` function ignores its `ARGS` and is included
purely to exercise the call machinery.

---

## 8. Reserved for v1+

The following will be specified in subsequent revisions and MUST be rejected
by v0 compilers as syntax errors:

(All previously-deferred items are now implemented. This section is
empty pending v2+ features.)

---

## 9. Open questions

- Should there be lexical block scope for variables introduced inside a
  `~ATH` loop, or is the environment flat within an activation? (Currently
  flat per activation. Activations themselves are isolated.)
- Should `import NAME VAR;` warn on duplicate `NAME` across distinct `VAR`s?
- Should `importf` registering a name twice be a hard error rather than
  last-wins?
- (Resolved.) `print` escapes `;` via `\;`, plus `\\ \n \t \r`. See §2.4.
- (Resolved.) `STRING` supports the escapes `\" \\ \n \t \r`. See §2.3.
- (Resolved.) `intern` mode hashes by raw pointer pair, not recursive
  structural identity. See §4.4.3.
