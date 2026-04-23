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
KEYWORD     := 'import' | 'BIFURCATE' | 'print'    [matched case-insensitively]
LOOPSTART   := '~ATH'                              [the 'ATH' part is case-insensitive]
DIE         := '.DIE'                              [the 'DIE' part is case-insensitive]
IDENT       := [A-Za-z_][A-Za-z0-9_]*              [case-sensitive]
PUNCT       := '(' | ')' | '[' | ']' | '{' | '}' | ',' | ';'
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

- Active: `import`, `BIFURCATE`, `print`, `INPUT`, `PRINT2`.
- Reserved against future use: `importf`. Programs using this token are
  rejected with a "reserved for v1+" diagnostic rather than treating it
  as an identifier.

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

### 2.3 `print` payload

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
              | bifurcate-stmt
              | ath-loop
              | die-stmt
              | print-stmt ;

import-stmt   = 'import' IDENT IDENT ';' ;

bifurcate-stmt
              = decompose-stmt
              | compose-stmt ;

decompose-stmt
              = 'BIFURCATE' IDENT '[' IDENT ',' IDENT ']' ';' ;

compose-stmt  = 'BIFURCATE' '[' IDENT ',' IDENT ']' IDENT ';' ;

ath-loop      = '~ATH' '(' IDENT ')' '{' statement* '}' ;

die-stmt      = IDENT '.DIE' '(' ')' ';' ;

print-stmt    = 'print' RAWTEXT ';' ;
```

Notes:

- `~ATH` followed by anything other than `(` is a syntax error.
- A statement may not appear outside a `program` or `ath-loop` body.
- The two `BIFURCATE` forms are distinguished by the token following
  `BIFURCATE`: an `IDENT` selects decompose; a `[` selects compose.

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

Before the first statement of the program executes, the environment contains
exactly two bindings:

| Name   | Object                                                                 |
|--------|------------------------------------------------------------------------|
| `THIS` | A fresh alive object with no halves. Killing it terminates the program. |
| `NULL` | A globally-shared, immortal-in-deadness object: `alive = false`.       |

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

#### 4.4.1 `import NAME VAR;`

If `VAR` is already bound: no-op.

Otherwise: allocate a fresh alive object with no halves, bind `VAR` to it.

`NAME` is metadata (it preserves a flavor of the source program for tooling
and diagnostics). The compiler MAY warn on collisions with other declared
names but MUST NOT use `NAME` to alter program behavior.

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

#### 4.4.4 `~ATH(V) { S* }`

```
loop:
  if not ath_is_alive(env[V]): goto end_loop
  execute S*
  goto loop
end_loop:
```

The check re-reads `V` from the environment every iteration. Rebinding `V`
inside the body changes what is being watched.

The body may be empty (`{}`), in which case the construct loops forever if
the initial check passes.

#### 4.4.5 `V.DIE();`

1. Read `V`'s current binding, the object `o`.
2. If `V` is the name `THIS`: terminate the program immediately. No
   subsequent statement, in any enclosing loop, executes.
3. Otherwise: set `o.alive = false`. This affects only `o` itself — its
   halves, any composites it is part of, and any other aliases of `o` (other
   variables pointing to the same object) all continue to observe `o` as
   dead, but no other object is modified.

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

### 4.5 Program termination

A program terminates when either:

- `THIS.DIE();` is executed (§4.4.5), or
- Control falls off the end of the top-level statement list.

These have identical observable effects.

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

- Lexical: unterminated `/*`, missing space after `print`, illegal character.
- Syntactic: any deviation from the grammar in §3.
- Reference to an unbound name. (Specifically: `~ATH(V)`, `V.DIE();`,
  decomposition source `V`, and compose operands must name variables that
  have been introduced by `import` or by an earlier `BIFURCATE` *on every
  control-flow path*. The "every path" rule is checked syntactically: a name
  is in scope if it would be introduced by some preceding statement in the
  same block or an enclosing block.)

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

---

## 8. Reserved for v1+

The following will be specified in subsequent revisions and MUST be rejected
by v0 compilers as syntax errors:

- `V.DIE(ARG);` — function return value
- `importf "FILE" as FN;` — function import
- `FN [L,R] V;` and `FN V [L,R];` — function call (compose-result and
  decompose-result forms)
- `ARGS` — function input parameter
- `EXECUTE(...)` postfix, lowercase `bifurcate`, `!VAR`, multi-token `import`
  forms — Homestuck-surface compatibility layer

---

## 9. Open questions (to resolve before v1)

- Should there be lexical block scope for variables introduced inside a
  `~ATH` loop, or is the environment flat? (v0 is flat — single global
  namespace.)
- Should `import NAME VAR;` warn on duplicate `NAME` across distinct `VAR`s?
- Should `print` support an escape mechanism for `;`?
- Should we add quoted string literals as an alternative to `RAWTEXT`?
- Concrete intern semantics for `ath_compose` in `intern` mode: by raw
  pointer pair, or recursively by structural identity of halves?
