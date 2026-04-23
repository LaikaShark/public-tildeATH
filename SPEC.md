# ~ATH Language Specification — v0

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

### Deferred (reserved syntax — implementations must reject for v0)

- `V.DIE(ARG);` (function return form)
- `importf "FILE" as FN;` and function call forms `FN [A,B]C;`, `FN A[B,C];`
- `INPUT V;`, `PRINT2 V;`
- The predefined name `ARGS`
- Homestuck-surface syntax: `EXECUTE(...)` postfix, lowercase `bifurcate`,
  `!VAR` inversion, multi-word concept names in `import`
- Numeric, string, or list literal sugar

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
KEYWORD     := 'import' | 'importf' | 'as' | 'BIFURCATE' | 'print'
              | 'INPUT' | 'PRINT2' | 'EXECUTE'   [matched case-insensitively]
LOOPSTART   := '~ATH'                          [the 'ATH' part is case-insensitive]
DIE         := '.DIE'                          [the 'DIE' part is case-insensitive]
IDENT       := [A-Za-z_][A-Za-z0-9_]*          [case-sensitive]
STRING      := '"' (any char except '"')* '"'  [no escapes in v1]
PUNCT       := '(' | ')' | '[' | ']' | '{' | '}' | ',' | ';' | '!'
```

**Case sensitivity.** Keywords, the `~ATH` loop-start token, the `.DIE`
method token, and (in v1+) function names match **case-insensitively**:
`IMPORT`, `Import`, and `import` all denote the same keyword;
`~ath`, `~Ath`, and `~ATH` all denote the same loop-start;
`.die` and `.DIE` denote the same method token. Identifiers (variable
names) are **case-sensitive**: `Foo`, `foo`, and `FOO` are three distinct
variables.

**Reserved words** (no case variant of any of these may appear as an
identifier):

- Active: `import`, `importf`, `as`, `BIFURCATE`, `print`, `INPUT`,
  `PRINT2`, `EXECUTE`.

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
sequence of bytes terminated by the next `"`. There are no escape
sequences in v1: `\n`, `\"`, etc. are not interpreted. Newlines inside the
body are part of the string. The body may be empty.

Used by `importf` (§4.4.9) to name a file path.

### 2.4 `print` payload

After the keyword `print`, the lexer enters a one-shot raw mode:

1. Consume exactly one ASCII space (`U+0020`). It is an error if the next
   character is not a space.
2. Capture every subsequent character (including newlines, brackets, anything)
   verbatim into a `RAWTEXT` token, stopping immediately before the next `;`.
3. The captured text may be empty.
4. The `;` is then consumed as a normal token.

`RAWTEXT` cannot contain `;`. There is no escape mechanism in v0.

---

## 3. Grammar

EBNF. `*` is zero-or-more, `?` is optional.

```
program       = statement* ;

statement     = import-stmt
              | importf-stmt
              | bifurcate-stmt
              | ath-loop
              | die-stmt
              | print-stmt
              | input-stmt
              | print2-stmt
              | funcall-stmt ;

import-stmt   = 'import' IDENT+ ';' ;
                (* the last IDENT is the variable bound; preceding IDENTs
                   are metadata, joined with single spaces *)

importf-stmt  = 'importf' STRING 'as' IDENT ';' ;

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
```

Notes:

- `~ATH` followed by anything other than `(` is a syntax error.
- A statement may not appear outside a `program` or `ath-loop` body.
- The two `BIFURCATE` forms are distinguished by the token following
  `BIFURCATE`: an `IDENT` selects decompose; a `[` selects compose.
- An `IDENT`-starting statement is disambiguated by the next token:
  `.DIE` → die-stmt; `[` → funcall compose-arg form;
  `IDENT` → funcall decompose-result form.

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

#### 4.4.1 `import NAME... VAR;`

One or more IDENTs follow `import`. The **last** IDENT is `VAR`, the
variable to bind. Any preceding IDENTs are metadata (joined by single
spaces to form a "concept name").

If `VAR` is already bound: no-op.

Otherwise: allocate a fresh alive object with no halves, bind `VAR` to it.

The metadata preserves the flavor of the source program for tooling and
diagnostics. The compiler MAY warn on collisions with other declared
metadata strings but MUST NOT use it to alter program behavior. This
accommodates the Homestuck surface form `import dead grandmother G;` as
well as the drocta-style `import x V;`.

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

#### 4.4.3 `BIFURCATE [L, R] V;` (compose)

1. Read `L`'s and `R`'s current bindings, the objects `lo` and `ro`.
2. Call the runtime entry point `ath_compose(lo, ro)`. Bind `V` to the result.

In the v0 default `fresh` mode (§5.2): `ath_compose` allocates a new alive
object with `left = lo`, `right = ro`. Two `BIFURCATE [L,R] V;` statements
with structurally identical operands produce two distinct objects.

In the reserved `intern` mode (§5.2): `ath_compose` consults an intern table
keyed by the operand object identities. If a previously composed object with
those exact halves exists, it is returned (whether alive or dead);
otherwise, a new alive object is allocated and inserted.

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
~ATH(V) { S* } EXECUTE(NULL);
```

The `IDENT` after `EXECUTE` is **accepted but currently has no semantic
effect** — it is a syntactic accommodation of the Homestuck surface,
where `EXECUTE` historically named the action to perform after the loop
exits. Future revisions may interpret it (e.g., as a function to invoke).

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

Step 1 happens before step 3, so `THIS.DIE(THIS);` returns the activation's
own THIS object — the read of `THIS` is well-defined because the kill
hasn't happened yet.

Killing an already-dead object is a no-op.

#### 4.4.6 `print TEXT;`

Write `TEXT` to standard output, followed by a single line feed (`U+000A`).
No interpretation of escape sequences. No flushing guarantees beyond what
the C runtime provides.

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

#### 4.4.9 `importf "PATH" as NAME;`

A **compile-time directive**, not a runtime operation:

1. The implementation opens the file at `PATH`, resolved relative to the
   directory of the file containing this statement.
2. The file's contents are parsed as a Program per §3 and registered as a
   function under the name `NAME` (case-insensitively, per §2.2).
3. The statement emits no runtime code.

If `PATH` does not exist or fails to parse, compilation fails. A function
registered by `importf` is callable from any function in the compilation
unit, including from inside loops and from other functions.

A program may register multiple functions under the same name; the last
registration in source order wins. (Subject to revision; see §9.)

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

---

## 5. Runtime ABI

The compiler emits LLVM IR that calls a small set of runtime functions
implemented in C. The codegen MUST NOT inline the bodies of these functions
or bypass them with direct struct manipulation. They are the swap points
that let us evolve semantics without touching the frontend.

### 5.1 Types

```c
typedef struct ath_obj {
    int            alive;   /* nonzero = alive */
    struct ath_obj *left;   /* NULL = UNSET    */
    struct ath_obj *right;  /* NULL = UNSET    */
} ath_obj;
```

### 5.2 Functions and globals

```c
/* allocation */
ath_obj *ath_alloc_alive(void);

/* operations */
ath_obj *ath_compose(ath_obj *l, ath_obj *r);
void     ath_decompose(ath_obj *v, ath_obj **l_out, ath_obj **r_out);
void     ath_die(ath_obj *v);
int      ath_is_alive(ath_obj *v);

/* I/O */
void     ath_print(const char *text, size_t len);
ath_obj *ath_input_line(void);
void     ath_print_obj(ath_obj *s);
ath_obj *ath_char_atom(int c);

/* program control */
void     ath_halt(void) __attribute__((noreturn));

/* the singleton dead object */
extern ath_obj *ath_NULL;
```

`ath_compose` is the **(A)/(B) swap point**. Two implementations are
provided as separate runtime archives, selected at link time:

- `runtime_fresh.a` — `ath_compose` always allocates. **Default in v0.**
- `runtime_intern.a` — `ath_compose` interns. **Not built in v0** (reserved).

The driver flag `-fcompose=fresh|intern` selects which archive is linked.

`ath_decompose` is the swap point for lazy-halves vs. strict semantics.
`ath_die` is the swap point for any future cascading-death rule.
`ath_is_alive` is the swap point for any future structural- or derived-
liveness rule. None of these alternative behaviors are implemented in v0.

`ath_halt` is invoked exactly when `THIS.DIE();` executes. Implementations
typically call `_exit(0)`.

---

## 6. Errors

v0 errors fall into two classes:

### 6.1 Compile-time errors

- Lexical: unterminated `/*`, unterminated `"..."`, missing space after
  `print`, illegal character.
- Syntactic: any deviation from the grammar in §3.
- Reference to an unbound name in any read position, checked syntactically:
  a name is in scope if introduced by some preceding statement in the same
  block or an enclosing block. The scope is per-activation — function
  bodies have their own scope starting with `THIS`, `NULL`, `ARGS`.
- Reference to an unknown function in a `funcall-stmt`: matched
  case-insensitively against names registered by `importf`.
- Binding `NULL` (any case variant in a write position) is rejected per §4.2.
- File-not-found or parse error in an `importf` target.

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
- Should `print` support an escape mechanism for `;`?
- Should `importf` registering a name twice be a hard error rather than
  last-wins?
- Should `STRING` support escape sequences (`\n`, `\"`, `\\`)? Currently
  no escapes in v1.
- Concrete intern semantics for `ath_compose` in `intern` mode: by raw
  pointer pair, or recursively by structural identity of halves?
