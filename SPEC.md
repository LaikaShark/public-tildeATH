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
KEYWORD     := 'import' | 'importf' | 'as' | 'watch' | 'BIFURCATE'
              | 'print' | 'INPUT' | 'PRINT2' | 'EXECUTE'   [matched case-insensitively]
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

- Active: `import`, `importf`, `as`, `watch`, `BIFURCATE`, `print`,
  `INPUT`, `PRINT2`, `EXECUTE`.

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
              | watch-stmt
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

watch-stmt    = 'watch' STRING 'as' IDENT ';'              (* file form *)
              | 'watch' 'signal' IDENT 'as' IDENT ';' ;   (* signal form *)
                (* 'signal' is a *contextual* keyword: a bare IDENT whose
                   value matches "signal" case-insensitively. Outside the
                   second token after 'watch' it is a normal identifier. *)

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
variable to bind. Any preceding IDENTs are metadata, joined by single
spaces into a "concept name."

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

#### 4.4.12 `watch "PATH" as VAR;` and `watch signal NAME as VAR;`

Two forms, dispatched on the first token after `watch`.

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

**Shared rules** (both forms):

`VAR` must not be `NULL` (§4.2). If `VAR` is already bound, `watch` is a
no-op (idempotent, matching `import`). The contextual keyword `signal`
is recognized only as the second token after `watch`; elsewhere it is a
normal identifier.

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

Every object carries four optional lifetime conditions in addition to
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
4. **One-shot flag.** When set, the first `ath_is_alive` observation
   returns alive and atomically flips the underlying `alive` field to
   false; every subsequent observation returns dead. Combined with the
   `~ATH` loop's "re-check before every iteration" rule, this causes
   the body to execute exactly once.

These conditions are independent of the object's `alive` field — they
are *additional* ways an object can be observed dead. An object with
none of them set behaves exactly as in earlier specs (lives until
explicitly killed). An object may have any combination set.

`import NAME... VAR;` sets the deadline when `NAME` matches a
range-based library entry (§5.3), or sets the one-shot flag when
`NAME` matches the special entry `once`. `watch "PATH" as VAR;` sets
the watched path. There are no other surface forms that set these —
they are entry-point allocations, not mutators.

The lifetime sampling is **uniform** over the library entry's range,
seeded by the `ATH_SEED` environment variable if set (decimal unsigned
integer), or by the wall clock otherwise. Setting `ATH_SEED` makes
library-sampled programs deterministic for testing.

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

/* Lifetime extensions (§4.7) */
ath_obj *ath_alloc_with_lifetime(double min_s, double max_s);
ath_obj *ath_alloc_watching_file(const char *path);
ath_obj *ath_alloc_watching_signal(int signum);
ath_obj *ath_alloc_watching_signal_by_name(const char *name);
ath_obj *ath_alloc_oneshot(void);
ath_obj *ath_alloc_from_library(const char *name);
int      ath_library_lookup(const char *name, double *min_out, double *max_out);

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
- `watch` paths are *not* validated at compile time; missing files cause
  the watching object to be born dead at runtime, never a compile error.

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
- (Resolved.) `intern` mode hashes by raw pointer pair, not recursive
  structural identity. See §4.4.3.
