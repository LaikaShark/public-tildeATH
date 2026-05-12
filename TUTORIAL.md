# The `~ATH` Tutorial

This tutorial introduces the dialect of `~ATH` implemented by this
compiler. The companion document `SPEC.md` is the authoritative
reference; section numbers prefixed with `§` in this tutorial refer to
it.

`~ATH` is built on a single data type — the **object** — together with a
small set of statements that allocate objects, decompose and recompose
them, observe whether they are alive, and kill them. All higher-level
features (numbers, strings, arithmetic, comparisons, file I/O, timers)
are expressed in terms of those primitives through a fixed runtime
library.

The language has no expressions and no infix operators. Every
computation is a statement that binds a fresh object to a name. The
only forms of control flow are the `~ATH` loop (which runs while a
watched object is alive) and the `BRANCH` statement (one-shot
dispatch).

---

## 1. Compiling and running

The runtime is a C archive built once with `make`:

```bash
make runtime
```

A source file is compiled and linked by the Python driver:

```bash
python -m athc.cli prog.ath -o prog
./prog
```

The driver invokes `clang` (or the value of `--cc`) to assemble and
link the emitted object against the runtime archive.

Useful command-line flags:

- `--emit-ir` — print LLVM IR to stdout; do not link.
- `--emit-obj PATH` — write the object file to `PATH` and stop before
  linking.
- `--compose fresh|intern` — choose the composition discipline
  (§4.4.3). Default is `fresh`.
- `--runtime PATH` — link against a specific runtime archive instead of
  the default in `runtime/`.
- `--cc COMPILER` — C compiler to use for the link step.
- `-D NAME:MIN:MAX` (or `--define-lifetime`) — register an additional
  lifetime-library entry for this build, repeatable. See §11.

Two environment variables are consulted at runtime:

- `ATH_PATH` — colon-separated directories searched by the angle-bracket
  form of `importf` (§4.4.9) before the compiler-adjacent `stdlib/`
  directory.
- `ATH_SEED` — decimal unsigned integer used to seed the runtime random
  source. Setting it makes lifetime sampling and `RANDOM` calls
  reproducible.

A program either runs to completion (returning OS exit code 0) or runs
indefinitely. There are no runtime errors.

---

## 2. A first program

```ath
print Hello, ~ATH!;
THIS.DIE();
```

The program contains two statements.

`print` writes its payload to standard output followed by a single
newline. The payload begins immediately after the one required space
following the keyword `print` and ends at the next unescaped `;`.
Bytes are copied through verbatim, including literal newlines in
source.

Five escape sequences are recognized in the payload (§2.4):

| Source | Output      |
|--------|-------------|
| `\;`   | `;`         |
| `\\`   | `\`         |
| `\n`   | line feed   |
| `\t`   | tab         |
| `\r`   | carriage return |

A backslash followed by any other character is a compile-time
lexical error. To emit a literal semicolon (which would otherwise
terminate the payload), write `\;`. To emit a literal backslash,
write `\\`.

String literals `"..."` recognize `\"`, `\\`, `\n`, `\t`, `\r`
(§2.3) — the same set as `print` minus `\;` (which is unnecessary
inside a quoted literal). Unknown escapes are likewise a
compile-time error. String literals are consumed by file-path
statements (§4.4.9 and related) and by the `text` statement (§13.x).

`THIS.DIE();` terminates the program. Every activation — the top-level
program and every function call — owns a predefined object named
`THIS`. Killing `THIS` returns from the activation; killing `THIS` at
the top level terminates the program with exit code 0 (§4.4.5,
§4.5).

A program also terminates by falling off the end of its top-level
statement list, in which case `THIS.DIE();` is unnecessary.

---

## 3. The object model

An object is a heap-allocated record with three primary fields:

| Field   | Meaning                                                |
|---------|--------------------------------------------------------|
| `alive` | Boolean. True for newly allocated objects, with one exception (see `NULL` below). |
| `left`  | Pointer to another object, initially unset.            |
| `right` | Pointer to another object, initially unset.            |

Two rules govern these fields (§4.1):

1. `alive` transitions from true to false at most once and never back.
2. `left` and `right` are set together on first decomposition (see §5)
   and never change afterwards.

Other optional fields carry numeric payloads, lifetime extensions, and
dependency information; they are introduced in later sections.

### 3.1 Predefined names

Every activation begins with two predefined identifiers (§4.2):

- `THIS` — a fresh alive object representing the current activation.
- `NULL` — a globally shared, immortal-in-deadness object: `alive` is
  false and never changes.

Inside a function call, a third name `ARGS` is also predefined; it is
bound to the argument object the caller provided (§4.4.10, §4.4.11).
`ARGS` is not defined at the top level.

`NULL` is **read-only**: any statement that would rebind `NULL` is a
compile-time error. `THIS` and `ARGS` are ordinary rebindable
variables.

`THIS` and `NULL` must be spelled in uppercase. The identifier rule is
case-sensitive (§2.2), so `this` and `Null` are distinct, unbound
identifiers that a program may introduce itself.

### 3.2 Variables and objects

A variable is a name in the current activation's environment. It
points to exactly one object at any moment. Two variables may point to
the same object. Rebinding a variable does not mutate the object it
previously pointed to (§4.3).

Reading a variable always reads its current binding. A read of an
unbound name yields `NULL` at runtime (§4.2). The compile-time scope
check is syntactic and conservative; if it admits a read of a variable
whose introducing statement happens to lie on an unexecuted path, the
read is well-defined and produces `NULL`.

---

## 4. Importing objects

A new alive object is introduced into the environment with `import`:

```ath
import V;
```

The variable `V` is bound to a fresh alive object whose `left` and
`right` are unset (§4.4.1).

`import` accepts one or more identifiers before the target name. All
but the last are joined with single spaces into a **concept name**
that the runtime looks up in the lifetime library (§11). If no library
entry matches, the import behaves as above and allocates a plain alive
object. If an entry matches, the object's lifetime is governed by the
matched entry — for example:

```ath
import mayfly M;          // alive for somewhere between 5 minutes and 1 day
import dead grandmother G; // matches no entry: plain alive object
```

The first identifier after `import` must not be the contextual marker
`builtin` or `number`; those dispatch to separate forms (§9, §10).

If the target variable is already bound, `import` is a no-op.

---

## 5. Decomposition and composition

`BIFURCATE` has two forms, distinguished by what follows the keyword.

### 5.1 Decompose

```ath
BIFURCATE V[L, R];
```

This reads the object currently bound to `V` and binds `L` and `R` to
its left and right halves (§4.4.2). On the first decomposition of an
object, the runtime allocates two fresh alive objects and stores them
as the halves; subsequent decompositions read those same halves back.
The reads of `V` happen before the writes to `L` and `R`, so any of
`V`, `L`, `R` may overlap safely.

The halves persist for the lifetime of the program. Decomposing the
same object many times always yields the same two halves.

### 5.2 Compose

```ath
BIFURCATE [L, R] V;
```

This reads `L` and `R`, allocates a fresh alive object with those
halves, and binds `V` to it (§4.4.3). The exact identity of the
returned object depends on the composition discipline selected at link
time:

- **`fresh` mode** (default): every call returns a new object. Two
  composes of the same `(L, R)` produce two distinct objects.
- **`intern` mode** (`--compose intern`): the runtime keeps a
  hash-consing table keyed by the raw pointer pair `(L, R)`. Two
  composes of the same `(L, R)` return the *same* object, so killing
  one kills every name that ever obtained it from a structurally equal
  compose.

Both modes are observably identical for programs that do not depend
on the distinctness or sharing of composites. The conformance suite
runs every example under both modes.

### 5.3 Distinguishing the two forms

The token immediately after `BIFURCATE` distinguishes the forms: an
`IDENT` selects decompose, a `[` selects compose.

```ath
BIFURCATE A[B, C];   // decompose A into B and C
BIFURCATE [B, C] A;  // compose B and C into A
```

---

## 6. Killing objects

```ath
V.DIE();
```

Killing an object sets its `alive` field to false (§4.4.5). The
change affects only this one object; halves, parent composites, and
aliases continue to observe themselves. Killing an already-dead object
is a no-op.

`THIS.DIE();` has special meaning: it returns from the current
activation (or terminates the program at the top level).

A second form, `V.DIE(RET);`, sets the activation's pending return
value to `RET`'s current binding *before* killing `V`. When used as
`THIS.DIE(RET);`, this is how a function returns a value (§8.3).

When the killed object owns a file (see §16 on `read`), the runtime
also calls `unlink` on the file path before flipping `alive`. This is
the only path in the runtime that deletes files.

---

## 7. The `~ATH` loop

```ath
~ATH(V) {
    statements
}
```

The loop reads `V` from the environment, calls `ath_is_alive` on the
resulting object, and runs the body if the object is alive. After the
body, it returns to the start and re-evaluates the condition. The
loop exits when `ath_is_alive(V)` returns false (§4.4.4).

`V` is **re-read every iteration**. Rebinding `V` inside the body
changes which object is being watched. This is the canonical way to
exit a loop without killing the object originally bound to `V` — see
§7.3.

The body may be empty:

```ath
~ATH(V) { }
```

If `V` is alive at entry the loop runs forever; if dead, the
construct exits immediately.

### 7.1 Inverted loops

A leading `!` inverts the condition:

```ath
~ATH(!V) {
    statements
}
```

The body runs while `V` is **dead**. Because objects cannot become
alive again, an inverted loop runs the body at most once (and only if
`V` was already dead at entry); if `V` is alive at entry, the body
never runs.

### 7.2 The `EXECUTE` postfix

An optional `EXECUTE(IDENT)` may follow the closing brace:

```ath
~ATH(V) {
    statements
} EXECUTE(NULL);
```

The identifier inside `EXECUTE(...)` is parsed and scope-checked but
has no current runtime effect. It is accepted for syntactic
compatibility with surface-level Homestuck `~ATH` source. With the
postfix, the construct terminates with `;`; without it, the closing
`}` is the terminator and no `;` follows.

### 7.3 Exiting a loop by rebinding

To exit `~ATH(V)` without killing the object `V` points to, rebind `V`
to a dead object:

```ath
BIFURCATE NULL[J, V];
```

This decomposes `NULL` (legal, since every object can be decomposed),
binding `J` to its left half and `V` to its right half. Both halves
are fresh alive objects — but `NULL` itself is dead, so its halves
inherit nothing from it. The detail that matters is that the
decomposition rebinds the *name* `V` to a freshly allocated object
that we promptly do not use, while leaving the object originally bound
to `V` untouched.

A clearer idiom for the same purpose:

```ath
BIFURCATE V[L, R];   // observe the halves
V.DIE();             // kill V; halves and other aliases unaffected
```

The right idiom depends on whether other aliases of `V` need to stay
alive after the loop.

To exit an inverted loop `~ATH(!V)`, rebind `V` to a fresh alive
object:

```ath
BIFURCATE [NULL, NULL] V;
```

This composes a new alive object from two `NULL` halves and binds `V`
to it. The new object is alive, so the inverted-loop condition fails
and the loop exits.

---

## 8. Functions

A function is a separate `.ath` file registered into the current
program by `importf`. There is no inline function definition syntax.

### 8.1 Defining a function

A function file is an ordinary `~ATH` program. Inside the file, the
predefined names are `THIS`, `NULL`, and `ARGS` (the argument
object). The file ends by killing `THIS`, optionally with a return
value:

```ath
// hello.ath
print Hello from a function.;
THIS.DIE();
```

### 8.2 Registering and calling

```ath
importf "hello.ath" as HELLO;
import a A;
import b B;
HELLO [A, B] R;
THIS.DIE();
```

`importf "PATH" as NAME;` (§4.4.9) is a compile-time directive: it
parses the file at `PATH`, resolved relative to the importing file,
and registers it under `NAME`. Function names are matched
case-insensitively. The statement emits no runtime code.

A function call has two surface forms:

- **Compose-argument form**: `FN [L, R] V;` composes `L` and `R` into
  a single argument object, calls `FN`, and binds `V` to the result
  (§4.4.10).
- **Decompose-result form**: `FN A [L, R];` calls `FN` with `A`,
  decomposes the result, and binds `L` and `R` to its halves
  (§4.4.11).

Either form may be used at any call site. The compose form is more
common when passing two operands; the decompose form is useful when
the result is logically a pair.

### 8.3 Returning a value

`THIS.DIE(RET);` sets the pending return value to `RET`'s binding,
then returns. If `THIS` falls off the end of the body without an
explicit `DIE(RET)`, the return value is `NULL`.

```ath
// return_args.ath
THIS.DIE(ARGS);   // returns the argument unchanged
```

### 8.4 Search-path imports

A second form of `importf` consults the `ATH_PATH` environment
variable:

```ath
importf <add> as ADD;
```

The bare identifier between angle brackets is the file *stem* (without
`.ath`). The runtime tries each colon-separated directory in
`ATH_PATH` in order, then the compiler-adjacent `stdlib/`. The first
existing `<stem>.ath` wins (§4.4.9, §5.4). This form is used to bring
in standard-library shims (see §10).

### 8.5 Recursion

A function may call itself or any other registered function. All
registered functions are visible from every call site, including from
within other function bodies. Mutual recursion works without forward
declarations because the compiler emits all function signatures before
any function body.

---

## 9. C ABI builtins

A function whose body is implemented in C is declared with:

```ath
import builtin SYMBOL as NAME;
```

This registers `NAME` in the function registry as a direct call to
the C symbol `SYMBOL` (§4.4.13). The C function must have the
signature `ath_obj *(ath_obj *, ath_obj *)`; the linker, not the
compiler, verifies that the symbol exists. The statement emits no
runtime code at its source position.

This form is rarely written directly in user code. The standard
library uses it to expose runtime functions through ordinary `~ATH`
shim files:

```ath
// stdlib/add.ath
import builtin ath_add as ATH_ADD;
BIFURCATE ARGS [X, Y];
ATH_ADD [X, Y] R;
THIS.DIE(R);
```

A user program then brings the shim in via `importf <add> as ADD;`
(§8.4) and calls `ADD [X, Y] R;` like any other function. The
indirection through a shim file lets the standard library evolve
independently of the runtime ABI.

---

## 10. Numbers

`~ATH` has no number literal in the grammar. A number is an object
that carries an integer payload in addition to its `alive` bit. Two
fields hold the payload (§4.8):

- `has_value` — nonzero iff a payload is set.
- `value` — signed 64-bit integer.

A new number object is introduced with:

```ath
import number 42 as N;
```

This allocates a fresh object with `has_value = 1`, `value = 42`,
and binds `N` to it (§4.4.14). The literal must fit signed 64-bit
range; otherwise the program fails to compile.

A number object is **eternal-alive** by default — it has no deadline,
no watch path, no awaited signal. It outlives the program. To give a
number a finite lifetime, compose it with a mortal carrier:

```ath
import number 42 as N;
import mayfly M;
BIFURCATE [N, M] MORTAL;
```

`MORTAL` is alive as long as `M` is, but `BIFURCATE` composition by
itself does **not** install a dependency from `MORTAL` onto `M`'s
lifetime — see §12.1 for the dep-tracking machinery that the
arithmetic builtins use.

### 10.1 Arithmetic builtins

Seven arithmetic functions are shipped as stdlib shims (§4.8.2):

| Surface call            | Result                                |
|-------------------------|---------------------------------------|
| `ADD [X, Y] R;`         | `X.value + Y.value`                   |
| `SUB [X, Y] R;`         | `X.value - Y.value`                   |
| `MUL [X, Y] R;`         | `X.value * Y.value`                   |
| `DIV [X, Y] R;`         | `X.value / Y.value` (toward zero)     |
| `MOD [X, Y] R;`         | `X.value % Y.value`                   |
| `TO_STRING [N, _] S;`   | string encoding of `N.value` (§13)    |
| `PARSE [S, _] N;`       | int64 parsed from the string `S`      |

A numeric **second wave** adds the usual maths and bit-twiddling
(§4.8.2):

| Surface call            | Result                                |
|-------------------------|---------------------------------------|
| `POW [X, Y] R;`         | `X` to the power `Y` (`Y >= 0`)       |
| `ABS [X, _] R;`         | magnitude of `X`                      |
| `NEG [X, _] R;`         | `-X`                                  |
| `MIN [X, Y] R;` / `MAX` | lesser / greater of `X`, `Y`          |
| `GCD [X, Y] R;`         | greatest common divisor               |
| `SIGN [X, _] R;`        | `-1`, `0`, or `1`                     |
| `BAND`/`BOR`/`BXOR`     | bitwise `&` / `\|` / `^`              |
| `BNOT [X, _] R;`        | bitwise `~X`                          |
| `SHL [X, Y] R;` / `SHR` | left / arithmetic-right shift (0..63) |
| `CLAMP [X, PAIR] R;`    | `X` confined to `[LO, HI]`            |

`CLAMP` packs its bounds with `ENTANGLE [LO, HI] PAIR;` (the compose-pair
pattern, §13.2.3). `POW` rejects negative exponents; `ABS`/`NEG`/`GCD`
reject `INT64_MIN`; shifts require a count of 0..63.

All are brought in by name:

```ath
importf <add> as ADD;
importf <to_string> as TO_STRING;
```

Each call returns a fresh object that is alive on success and **born
dead** on failure. Failure conditions (§4.8.2) include:

- Either operand is dead at the call site.
- Integer overflow (detected via the compiler's overflow intrinsics).
- Division or modulo by zero, or `INT64_MIN / -1` and `INT64_MIN % -1`
  (which wrap in two's complement).
- `PARSE` of a string with non-digit characters, or a value outside
  signed 64-bit range.
- `TO_STRING` of a number whose `has_value` is zero.

Born-dead objects have `alive = 0`, `has_value = 0`, and `value = 0`.

The unary builtins `TO_STRING` and `PARSE` take two operands because
the C ABI is fixed at two `ath_obj *` arguments. The second operand
is read and discarded; convention is to pass any in-scope identifier
(traditionally `NULL` or a one-letter placeholder).

---

## 11. Lifetime extensions

An object may carry up to five optional lifetime conditions in
addition to its explicit `.DIE`-driven mortality (§4.7). On every
`ath_is_alive` observation, the runtime checks them in order; the
first that fails flips `alive` to false.

| Extension      | Set by                                                | Effect                                                         |
|----------------|-------------------------------------------------------|----------------------------------------------------------------|
| Deadline       | `import <library-entry>`, `TIMER N as T;`             | Dies when the monotonic clock reaches the timestamp.           |
| Watched path   | `watch "PATH" as V;`, `read "PATH" as V;`             | Dies when `access(F_OK)` on the path fails.                    |
| Awaited signal | `watch signal NAME as V;`                             | Dies when the named POSIX signal is received.                  |
| One-shot       | `import once V;`                                      | First observation returns alive; subsequent observations dead. |
| `owns_path`    | `read "PATH" as V;` only                              | Combined with watch_path, direct kill calls `unlink`.          |

All death is one-way (§4.1); a dead object never becomes alive.

### 11.1 The lifetime library

`import NAME... VAR;` matches the joined concept name (case-insensitive)
against a fixed runtime table. A match samples a uniformly-random
deadline from the matched range. A miss falls through to a plain
alive object.

The full table is in §5.3 of the spec. A representative selection:

| Concept name      | Range (seconds)        |
|-------------------|------------------------|
| `instant`         | 0 (born dead)          |
| `tick`            | 0.001 – 0.01           |
| `blink`           | 0.1 – 0.4              |
| `second`          | 1 – 1                  |
| `minute`          | 60 – 60                |
| `mayfly`          | 300 – 86 400           |
| `fly`             | 86 400 – 259 200       |
| `human`           | 1.58e9 – 3.79e9        |
| `universe`        | 3e100 – 3e110          |
| `forever`         | 1e308 – 1e308          |
| `once`            | special — see §11.2    |

Names with `min == max` have zero variance. Names with `min == 0` may
be born dead.

The samples are deterministic when `ATH_SEED` is set to a decimal
unsigned integer in the environment.

Additional entries may be registered for a single build with `-D`:

```bash
python -m athc.cli prog.ath -D 'tortoise:50:150' -o prog
```

Each `-D` adds an entry to a per-program user table consulted before
the built-in table, so a user entry overrides any built-in of the
same name. The reference runtime permits at most 64 user entries per
program.

### 11.2 The `once` entry

```ath
import once V;
~ATH(V) {
    print runs exactly once.;
}
```

The first `ath_is_alive(V)` observation returns true and flips the
underlying `alive` to false; every later observation returns false.
Because `~ATH` checks the condition before each iteration, the body
executes exactly once.

### 11.3 Watching signals

```ath
watch signal SIGUSR1 as V;
~ATH(V) {
    print waiting for SIGUSR1;
    sleep ONE_SEC;
}
print signal received;
```

The signal name is matched case-insensitively against this set
(§4.4.12): `SIGHUP`, `SIGINT`, `SIGQUIT`, `SIGUSR1`, `SIGUSR2`,
`SIGPIPE`, `SIGALRM`, `SIGTERM`, `SIGCHLD`. Other names produce a
born-dead object and a stderr warning.

The flag the runtime sets in its signal handler is **sticky** and
process-global. All watchers of the same signal die together when the
signal arrives. A watcher allocated after a signal has already
arrived is born dead.

The contextual marker `signal` is recognized only as the second token
after `watch`; elsewhere it is a normal identifier.

### 11.4 Watching files

```ath
watch "/tmp/keep_alive" as V;
~ATH(V) {
    print file still present;
    sleep ONE_SEC;
}
```

If the file exists at allocation time the object is born alive; if
not, it is born dead. Every `ath_is_alive(V)` call runs
`access(F_OK)` on the path. The check is one-way: recreating a deleted
file does not revive the object.

`watch` is purely observational. It does **not** create, delete, or
take ownership of the file. The owning equivalent is `read` (§16).

---

## 12. Comparisons and verdicts

A **verdict** is an object whose `alive` bit carries the truth of a
comparison: alive iff the comparison is true. Verdicts carry no
payload and are observed only through `ath_is_alive`, typically in a
loop header.

Six primitive comparisons are shipped (§4.8.3):

| Surface call    | Alive when            |
|-----------------|-----------------------|
| `LT [X, Y] V;`  | `X.value < Y.value`   |
| `LE [X, Y] V;`  | `X.value <= Y.value`  |
| `EQ [X, Y] V;`  | `X.value == Y.value`  |
| `NE [X, Y] V;`  | `X.value != Y.value`  |
| `GE [X, Y] V;`  | `X.value >= Y.value`  |
| `GT [X, Y] V;`  | `X.value > Y.value`   |

A verdict is born dead if the comparison is false, if either operand
is dead, or if either operand lacks a payload.

All six are primitives rather than derived forms. This is so that a
negated comparison can be combined with `AND` and `OR` (§12.3)
without needing a NOT-of-verdict construct (which would conflict with
the one-way-death rule — see §12.3).

`~ATH(!V)` (§7.1) provides the same inversion at a single observation
site, and is the right choice when the negated verdict is consumed
immediately:

```
GT [X, Y] V;
~ATH(!V) { ... }    // body runs while X <= Y (i.e. NOT X > Y)
```

### 12.1 Lifetime inheritance

A true verdict installs a dependency on both operands via the runtime
helper `ath_inherit_lifetime` (§4.8.1). The runtime records the two
operands in the result's `dep1` and `dep2` slots; subsequent
`ath_is_alive` observations on the verdict return false if either
operand has since died.

All arithmetic, comparison, and string builtins install operand
dependencies on their results. `BIFURCATE [L, R] V;` composition does
**not** install dependencies — this is the only common source of a
derived value whose lifetime is independent of its constituents.

Dependency-driven death is one-way: once an operand is dead, the
derived value is dead at every observation, and that decision is not
re-evaluated.

### 12.2 A complete example

```ath
importf <add> as ADD;
importf <lt> as LT;

import number 0 as ZERO;
import number 5 as FIVE;
import number 1 as ONE;
import number 0 as I;

LT [I, FIVE] COND;
~ATH(COND) {
    print iteration;
    ADD [I, ONE] I;
    LT [I, FIVE] COND;     // re-evaluate
}
THIS.DIE();
```

This prints `iteration` five times. The comparison is re-evaluated
explicitly inside the loop; the runtime does not re-run builtins. The
verdict object bound to `COND` is replaced each iteration; the prior
verdicts become unreachable and are reclaimed at program exit.

### 12.3 Combining verdicts: AND, OR

Two logical combinators take verdicts (or any objects) and produce a
new verdict. Both are stdlib shims over runtime builtins
(§4.8.3).

| Surface call    | Alive when                                                | Born dead when                |
|-----------------|-----------------------------------------------------------|-------------------------------|
| `AND [X, Y] V;` | both `X` and `Y` are alive at every observation           | either operand dead at call   |
| `OR  [X, Y] V;` | at least one of `X`, `Y` is alive at every observation    | both operands dead at call    |

`AND` uses the conjunctive dependency machinery from §12.1: the
result inherits both operands as deps, so it becomes dead at the
next observation as soon as either operand dies, and stays dead.

`OR` installs both operands as deps but evaluates them
**disjunctively**: the runtime walks both on every observation and
returns alive as long as at least one is alive. Only once both are
dead does the OR-result flip to dead permanently (consistent with
the one-way-death rule, §6).

```ath
importf <gt>  as GT;
importf <and> as AND;
importf <or>  as OR;

import number 0 as ZERO;

GT [X, ZERO] X_POS;          // X > 0
GT [Y, ZERO] Y_POS;          // Y > 0
AND [X_POS, Y_POS] BOTH;     // X > 0 AND Y > 0
~ATH(BOTH) { print both positive; BOTH.DIE(); }
```

The OR-result's runtime check evaluates its two deps on every
`ath_is_alive` call; this is unavoidable since "at least one alive"
cannot be cached. AND-results, by contrast, cache the first dead
observation and become a constant-time check thereafter.

### 12.4 Why there is no `NOT`

A NOT-of-verdict is not provided. The reason is structural: such a
verdict would have to be born dead when its operand is alive, then
become alive the moment the operand died. Dead→alive transitions
violate the one-way-death rule that the entire object model rests on
(§3, §6).

Negation is expressed in two places instead:

- At an observation site, use `~ATH(!V)` or `BRANCH(!V)`. Both
  re-check the condition at each observation and produce the inverse
  truth value without materializing a NOT-object.
- When the negated verdict must be combined with `AND` or `OR`, use
  the contrapositive comparison primitive. For "X < Y is false AND
  Z != 0," write `GE [X, Y] V1; NE [Z, ZERO] V2; AND [V1, V2] V;`
  rather than trying to negate `LT`. This is why `LE`, `GE`, `NE`
  are primitive — they fill the gap that `NOT` would otherwise need
  to bridge.

---

## 13. Strings

A **string** is a right-nested cons-list of **character atoms**,
terminated by `NULL` (§4.6).

- The empty string is `NULL`.
- A non-empty string with first character `c` and tail `t` is
  `compose(ATOM(c), t)`, i.e. `BIFURCATE [ATOM(c), t] S;`.

A character atom is an alive object allocated by the runtime, exactly
one per distinct byte value (0..255). Two strings sharing a character
share the same atom by pointer identity.

Strings enter a program through `INPUT`, `read`, `TO_STRING`, or the
`text` statement (§13.3), and are written out through `PRINT2`,
`write`, or `append`.

### 13.1 `INPUT` and `PRINT2`

```ath
INPUT line;
PRINT2 line;
THIS.DIE();
```

`INPUT VAR;` reads a single line from standard input, strips the
trailing line feed (and a preceding `\r` if present), encodes the
remaining bytes as a cons-list, and binds `VAR` to it (§4.4.7). On
end-of-file or read error, the line is treated as empty. Lines longer
than the implementation's input buffer (at least 4096 bytes) are
returned in successive `INPUT` calls.

`PRINT2 VAR;` walks `VAR`'s right-spine, writing the byte represented
by each left-half atom, and finally writes a single line feed
(§4.4.8). The walk terminates at the first dead cell, the first
`NULL`, or the first left half that is not a recognized character
atom.

`print` (§2) is **not** the same statement. `print` writes a literal
payload from the source text; `PRINT2` walks an object. Use `print`
for static messages and `PRINT2` for dynamically constructed text.

### 13.2 String operations

The runtime ships string operations in two waves (§4.8.4). Two are
dedicated statement forms, surfaced by the parser; the rest are stdlib
shims brought in by `importf`.

The first wave — access, measurement, search-and-edit:

| Operation     | Surface form                              |
|---------------|-------------------------------------------|
| `length`      | `LENGTH [S, _] N;`                        |
| `concat`      | `CONCAT [A, B] R;`                        |
| `find`        | `FIND [HAY, NEEDLE] IDX;`                 |
| `replace`     | `REPLACE [S, PAIR] R;` (compose-pair)     |
| `replace_all` | `REPLACE_ALL [S, PAIR] R;` (compose-pair) |
| subscript     | `S[N] X;`                                 |
| slice         | `S[I..J] X;`                              |

The second wave — predicates (returning verdicts), transforms, and
structural reshaping. Each is a stdlib shim over the two-operand ABI:

| Operation    | Surface form                | Yields                          |
|--------------|-----------------------------|---------------------------------|
| `streq`      | `STREQ [A, B] V;`           | verdict: `A` byte-equals `B`    |
| `startswith` | `STARTSWITH [HAY, PRE] V;`  | verdict: `HAY` begins with `PRE`|
| `endswith`   | `ENDSWITH [HAY, SUF] V;`    | verdict: `HAY` ends with `SUF`  |
| `strlt`      | `STRLT [A, B] V;`           | verdict: `A` < `B` (byte order) |
| `strgt`      | `STRGT [A, B] V;`           | verdict: `A` > `B` (byte order) |
| `lower`      | `LOWER [S, _] R;`           | `S` with `A`–`Z` lowercased     |
| `upper`      | `UPPER [S, _] R;`           | `S` with `a`–`z` uppercased     |
| `trim`       | `TRIM [S, _] R;`            | `S` without outer whitespace    |
| `lstrip`     | `LSTRIP [S, _] R;`          | `S` without leading whitespace  |
| `rstrip`     | `RSTRIP [S, _] R;`          | `S` without trailing whitespace |
| `split`      | `SPLIT [S, SEP] LIST;`      | cons-list of substrings         |
| `join`       | `JOIN [LIST, SEP] R;`       | substrings joined by `SEP`      |
| `contains`   | `CONTAINS [HAY, NEEDLE] V;` | verdict: `NEEDLE` occurs in `HAY`|
| `count`      | `COUNT [HAY, NEEDLE] N;`    | # non-overlapping occurrences   |
| `rfind`      | `RFIND [HAY, NEEDLE] IDX;`  | index of the *last* occurrence  |
| `repeat`     | `REPEAT [S, N] R;`          | `S` repeated `N` times          |
| `reverse`    | `REVERSE [S, _] R;`         | `S` with characters reversed    |
| `pad_left`   | `PAD_LEFT [S, N] R;`        | `S` space-padded to width `N`   |
| `pad_right`  | `PAD_RIGHT [S, N] R;`       | `S` space-padded to width `N`   |
| `ord`        | `ORD [A, _] N;`             | code (0..255) of char atom `A`  |
| `chr`        | `CHR [N, _] S;`             | length-1 string for code `N`    |
| `compare`    | `COMPARE [A, B] N;`         | three-way `-1`/`0`/`1`          |
| `char_at`    | `CHAR_AT [S, N] STR;`       | Nth char as a length-1 string   |
| `find_from`  | `FIND_FROM [S, PAIR] IDX;`  | find from an offset (packed)    |
| `capitalize` | `CAPITALIZE [S, _] R;`      | first char up, rest down        |
| `title`      | `TITLE [S, _] R;`           | titlecase each word             |
| `strip_chars`| `STRIP_CHARS [S, CHARS] R;` | strip a custom char set         |
| `lstrip_chars`/`rstrip_chars` | `… [S, CHARS] R;` | one-sided custom strip     |
| `pad_left_with`/`pad_right_with` | `… [S, PAIR] R;` | pad with a custom fill char |

`CONTAINS` is the verdict companion to `FIND`: where `FIND` born-dies
when the needle is absent, `CONTAINS` simply yields a dead verdict, and
`COUNT` yields a live `0`. `RFIND` is `FIND` from the right. `REPEAT`
and the `PAD` ops take a number payload as their second operand; padding
uses spaces and never truncates. `ORD` and `CHR` bridge a character
atom — the value `S[N]` yields — and its byte code: subscript a string
to get an atom, `ORD` it to a number, `CHR` a number back to a length-1
string. `S[N]` returns an independent snapshot of the character, so
`ORD` of it is unaffected by what happens to other strings sharing that
character.

`COMPARE` collapses `STRLT`/`STREQ`/`STRGT` into one `-1`/`0`/`1` sort
key. `CHAR_AT` is the string-valued cousin of `S[N]` (atom). The
two-extra-argument ops — `FIND_FROM` (needle + start), `CLAMP` (lo +
hi), and `PAD_LEFT_WITH`/`PAD_RIGHT_WITH` (width + fill) — pack the pair
with `ENTANGLE` just like `REPLACE` (§13.2.3). `STRIP_CHARS` and its
one-sided variants take the strip set as a plain string.

The predicates feed a `BRANCH` the same way numeric comparisons do
(§9): the verdict is alive when the relation holds, dead otherwise.
Every string starts and ends with the empty string, so `STARTSWITH`
and `ENDSWITH` against an empty (`NULL`) operand are always alive. The
transforms return a fresh string and pass `NULL` (or any name) as the
ignored second operand, exactly like the unary arithmetic shims.
`SPLIT` born-dies on an empty separator; `JOIN` of an empty list is
`NULL`. The two are inverses when the separator does not occur inside
any element.

`LENGTH` returns the number of right-spine cells walked before
hitting `NULL` or a dead cell. `LENGTH` of `NULL` is `0`, not dead —
the empty string is a valid string with a well-defined length.

`CONCAT` allocates a fresh cons-list containing every character atom
of `A` followed by every character atom of `B`, terminated by `NULL`.
It is born dead if either operand is dead.

#### 13.2.1 Subscript and slice

```ath
import number 0 as IDX;
S[IDX] C;
```

`S[N] X;` (§4.4.15) walks `S`'s right-spine `N.value` steps and binds
`X` to the left half of the resulting cell. For a string this yields
the Nth character **atom**, not a length-1 string.

`S[I..J] X;` (§4.4.16) builds a fresh cons-list of the elements in the
half-open range `[I, J)`, terminated by `NULL`. For strings this is a
substring; for other right-spine shapes it is a sublist. The slice
is born dead on out-of-range indices, on operands without payloads,
or when `I > J`.

Both forms install dependencies: killing the source or any index
operand invalidates the result at the next observation.

The bracket-form syntax is disambiguated by the contents between the
brackets:

- `S[N] X;` — exactly one identifier: subscript.
- `S[I..J] X;` — two identifiers separated by `..`: slice.
- `S[L, R] X;` — two identifiers separated by `,`: function call
  (compose-argument form, §8.2).

The single bracket form distinguishes from decomposition (`BIFURCATE
S[L, R];`) by the presence of the `BIFURCATE` keyword.

#### 13.2.2 Wrapping an atom in a string

A subscript returns the character atom directly. To turn it into a
printable single-character string:

```ath
S[IDX] C;
BIFURCATE [C, NULL] STR;
PRINT2 STR;
```

#### 13.2.3 The compose-pair pattern

`REPLACE` and `REPLACE_ALL` conceptually take three arguments —
source, needle, replacement — but the builtin ABI accepts only two.
The needle and replacement are packed into a single composite, which
the builtin decomposes internally. The recommended packer is
`ENTANGLE`:

```ath
importf <entangle> as ENTANGLE;

ENTANGLE [NEEDLE, REPLACEMENT] PAIR;
REPLACE [S, PAIR] R;
```

`ENTANGLE` does the same composition as `BIFURCATE [L, R] V;` and
additionally installs both operands as dependencies of the result
(§12.1). Killing `NEEDLE` or `REPLACEMENT` after the `REPLACE` call
then invalidates `PAIR` on the next observation, which in turn
invalidates `R` through the standard dep chain.

The same packing convention is used by `ath_slice` internally for
its range endpoints (§13.2.1), but the user never writes that
composition — the slice statement emits it.

##### Plain `BIFURCATE` as the no-deps alternative

```ath
BIFURCATE [NEEDLE, REPLACEMENT] PAIR;
REPLACE [S, PAIR] R;
```

`BIFURCATE` composition does **not** install deps. `PAIR` has no
internal dependency on `NEEDLE` or `REPLACEMENT`; killing either
after the call does not propagate death to `R`. This form is
correct when the carrier composite must outlive its operands. For
the typical search-and-replace use case, `ENTANGLE` is the right
choice; `BIFURCATE` is the advanced alternative.

#### 13.2.4 Empty needles

`FIND` with an empty `NEEDLE` returns `0` (the empty string is
notionally a prefix at position 0). `REPLACE` and `REPLACE_ALL` with
an empty `NEEDLE` return a born-dead result: "replace nothing with
something" is deliberately undefined. Use `CONCAT` to prepend or
append.

### 13.3 Building strings: the `text` statement

The `text` statement is the surface form for constructing strings in
source code (§4.4.25). It has two forms — a single string literal
(primitive) and an interpolation (sugar) — distinguished only by the
number and kinds of parts before `as`.

#### 13.3.1 Primitive form

```ath
text "hello world" as GREETING;
PRINT2 GREETING;
```

`text "..." as VAR;` decodes the string literal (with the §2.3
escapes applied) and binds `VAR` to the resulting cons-list of
character atoms. An empty literal `text "" as VAR;` binds `VAR` to
`NULL` (which is the empty string per §13).

Source-level newlines inside the literal are taken literally too —
either embed them directly or use the `\n` escape:

```ath
text "first line\nsecond line" as TWO;
PRINT2 TWO;
```

#### 13.3.2 Interpolation form

```ath
import number 42 as N;
text "value: " N " (end)" as MSG;
PRINT2 MSG;
```

A `text` statement may contain any sequence of STRING literals and
identifiers before `as`. Each part is reduced to a string and the
parts are concatenated left to right.

- A **STRING part** contributes its decoded byte sequence (per §2.3).
- An **IDENT part** is read and coerced: if it carries an int64
  payload (a number), the runtime calls `TO_STRING` to produce its
  decimal representation; otherwise the value is treated as already
  a string (or an existing cons-list) and passed through.

The final value installs operand dependencies via `ath_concat`'s dep
machinery (§12.1): killing any of the source identifiers after the
`text` statement runs invalidates `MSG` at the next observation.

#### 13.3.3 Single-IDENT case

```ath
text N as M;
```

Equivalent to `TO_STRING [N, NULL] M;` when `N` carries a payload.
When `N` is already a string, `M` becomes a pointer-alias of `N` (no
copy). Rarely useful on its own, but consistent with the
interpolation rule.

#### 13.3.4 What `text` is and is not

`text` is the only source-level way to introduce a string value.
It is **not** the same as `print`:

| Form        | Source contains              | Result                       |
|-------------|------------------------------|------------------------------|
| `print TEXT;` | raw bytes up to `;`        | written to stdout immediately, no value bound |
| `text "..." as V;` | a string literal      | a string-cons-list bound to V |

To print a constructed string, use `text` + `PRINT2`. To emit a
fixed literal that needs no value, use `print` directly. Use
`PRINT2` over `print` whenever the content is dynamic.

### 13.4 Lists

A string is a right-nested cons-list of character atoms; a **list** is
the same shape with arbitrary elements. Build one with `BIFURCATE`,
head first:

```ath
import number 8 as N8;
import number 3 as N3;
import number 4 as N4;
BIFURCATE [N8, NULL] L1;     // [8]
BIFURCATE [N3, L1] L2;       // [3, 8]
BIFURCATE [N4, L2] LIST;     // [4, 3, 8]
```

A family of stdlib shims folds and slices number lists (§4.8.6):

| Surface call            | Result                                  |
|-------------------------|-----------------------------------------|
| `SUM [LIST, _] N;`      | Σ of element payloads (empty → `0`)     |
| `PRODUCT [LIST, _] N;`  | Π of element payloads (empty → `1`)     |
| `MAXIMUM [LIST, _] N;`  | greatest element (empty → dead)         |
| `MINIMUM [LIST, _] N;`  | least element (empty → dead)            |
| `MEMBER [LIST, X] V;`   | verdict: some element payload equals `X`|
| `TAKE [LIST, N] R;`     | fresh list of the first `N` elements    |
| `DROP [LIST, N] R;`     | fresh list of all but the first `N`     |

These read each element's payload, so they work on number lists and
born-die on a string (whose elements are character atoms). `LENGTH`,
`S[N]`, and `S[I..J]` (the slice form) already apply to any list.
There is no `map`/`filter`/`reduce` — ~ATH has no first-class
functions to pass — so list processing stays at the level of these
fixed folds plus `SPLIT`/`JOIN` (§13.2) for strings.

---

## 14. `BRANCH` and `CLONE`

`BRANCH` is a one-shot dispatch on an object's liveness. `CLONE`
copies an object so it can be inspected without destroying the
original. They are typically used together.

### 14.1 `BRANCH`

```ath
BRANCH(V) {
    statements run when V is alive
} ELSE {
    statements run when V is dead
}
```

The `ELSE` keyword is optional sugar; `BRANCH(V) { } { }` and
`BRANCH(V) { } ELSE { }` parse identically. The else clause itself is
optional; omitting it skips the dispatch when `V` is dead.

After whichever body runs (or after the skipped dispatch), the
runtime calls `ath_die` on `V`. `V` is therefore guaranteed dead on
exit from a `BRANCH`, regardless of which arm ran (§4.4.17).

The inverted form swaps which arm runs:

```ath
BRANCH(!V) {
    runs when V is dead
} ELSE {
    runs when V is alive
}
```

`V` is still consumed.

If a body rebinds `V`, the post-dispatch kill reads the *current*
binding and kills that. A `THIS.DIE(...)` inside a body returns from
the activation immediately, and the post-dispatch kill never runs.

### 14.2 `CLONE`

```ath
CLONE V as W;
```

`CLONE` allocates a fresh object `W` that copies, field by field,
from `V`'s current binding at clone time (§4.4.18):

- `alive` — `W` reflects `V`'s **currently observable** liveness, not
  the raw alive bit. The runtime evaluates `V` through a pure
  non-mutating check (the same predicate `ath_is_alive` uses, minus
  the side effects) and stores the result. This matters when `V`'s
  upstream operands have died since `V` was last directly observed —
  the clone captures the dep-walk result rather than `V`'s stale bit.
- `left`, `right` — pointer-copied; the deeper structure is shared.
- `has_value`, `value` — full payload copy.
- `deadline_s`, `watch_path`, `is_oneshot`, `awaiting_signal`,
  `dep_mode` — every intrinsic lifetime extension and the
  dep-evaluation mode are copied. A clone of a one-shot is itself a
  one-shot; a clone of a file watcher watches the same path; a clone
  of a deadlined object dies at the same deadline.

The clone of a one-shot is **not** consumed by the act of cloning.
The non-mutating refresh used for the alive bit does not trip
`is_oneshot`. So `CLONE V as W;` on a fresh one-shot leaves `V`
unfired and produces `W` as an independent fresh one-shot.

`W` does **not** copy `V`'s `dep1`/`dep2`, and does not copy the
`owns_path` flag (see §16). `W` is independent: killing one of `V`
or `W` does not affect the other. Because `dep_mode` is copied but
the deps are not, an OR-mode clone with no deps degenerates to a
plain alive/dead object that trusts its captured bit — the snapshot
is frozen at clone time and won't re-evaluate as upstream operands
change later.

Cloning `NULL` yields a fresh born-dead object.

### 14.3 Non-destructive checking

The canonical pattern for inspecting an object without killing it:

```ath
CLONE V as VCHECK;
BRANCH(VCHECK) {
    body
} ELSE {
    body
}
// V is still alive (assuming it was) and untouched
```

`VCHECK` is consumed by the `BRANCH`. `V` is unaffected.

This is the *only* way to consume a comparison verdict without
losing the underlying operands — the verdict itself can be cloned and
the clone passed to `BRANCH`.

---

## 15. Time, sleep, timers, randomness

The runtime exposes a monotonic clock, a sleep primitive, a deadline
allocator, and a uniform random source.

### 15.1 `NOW`

```ath
importf <now> as NOW;
NOW [NULL, NULL] T;
```

`NOW` returns a fresh number-payload object whose value is monotonic
milliseconds since the system's monotonic-clock origin (typically
boot). Successive calls within an activation observe non-decreasing
values. The result is **not** dep-tracked against its operands — both
are ignored — and the operands are conventionally `NULL`.

The zero point is not a wall-clock epoch. `NOW` is useful only for
measuring elapsed time:

```ath
NOW [NULL, NULL] T0;
// ... work ...
NOW [NULL, NULL] T1;
SUB [T1, T0] ELAPSED;
```

### 15.2 `sleep`

```ath
sleep N;
```

`sleep N;` blocks the current activation for `N.value` milliseconds
(§4.4.19). If `N` is `NULL`, dead, lacks a payload, or carries a
non-positive value, `sleep` returns immediately as a no-op.

The implementation uses `nanosleep`; interrupted sleeps may return
early. The duration is read at the start of the call, so changing
`N`'s binding mid-sleep has no effect.

`sleep` does not consume `N` and has no return value.

### 15.3 `TIMER`

```ath
TIMER N as T;
```

`TIMER` allocates a fresh alive object whose deadline is set to
`N.value` milliseconds from now, and binds `T` to it (§4.4.20). `T`
becomes observably dead when the deadline is reached.

`T` is **independent of `N`**: no dependency is installed, so killing
`N` after the `TIMER` call does not affect `T`. The duration is
consumed at allocation time.

The combination of `TIMER` with `~ATH` gives a bounded loop:

```ath
import number 5000 as FIVE_SEC;
import number 1000 as ONE_SEC;
TIMER FIVE_SEC as T;
~ATH(T) {
    print still running;
    sleep ONE_SEC;
}
print timed out;
```

### 15.4 `RANDOM`

```ath
importf <random> as RANDOM;
import number 0 as LO;
import number 100 as HI;
RANDOM [LO, HI] R;
```

`RANDOM` returns a fresh number-payload object whose value is
uniformly distributed over the half-open interval
`[LO.value, HI.value)`. It is born dead if either operand is dead,
lacks a payload, or if `LO.value >= HI.value`.

The random source is seeded once at runtime startup, either from
`ATH_SEED` or from the wall clock. Subsequent draws within a single
process consume that sequence; setting `ATH_SEED` makes the entire
sequence reproducible.

`RANDOM`'s result is **not** dep-tracked against its bounds: once
drawn, killing `LO` or `HI` does not invalidate the result.

---

## 16. File I/O

Four statements interact with the filesystem:

- `read "PATH" as VAR;` — slurp a file into a string-cons-list and
  take ownership.
- `write SRC to "PATH" [as VERDICT];` — truncate-and-write a string.
- `append SRC to "PATH" [as VERDICT];` — append a string.
- `close VAR;` — release ownership and kill.

`watch "PATH" as V;` (§11.4) is also file-related but is observational
only and was covered earlier.

### 16.1 `read`

```ath
read "input.txt" as F;
PRINT2 F;
F.DIE();
```

`read "PATH" as VAR;` (§4.4.21) opens `PATH` for reading (relative to
the program's current working directory) and binds `VAR` to a
cons-list containing the file's bytes as character atoms. On any
failure — missing file, permission denied, I/O error — `VAR` is bound
to a born-dead object.

The head of the cons-list is a **wrapper** object carrying:

- `watch_path` set to a copy of `PATH`. Every `ath_is_alive` call on
  `VAR` (or any object that inherits `VAR`'s deps) runs
  `access(F_OK)` and dies if the file is gone.
- `owns_path` set to 1. This flags the wrapper as the owner of the
  file.

The wrapper is **non-interned**: even under `--compose intern`, two
`read` calls on the same path return distinct wrappers.

The combination of `watch_path` and `owns_path` triggers a special
rule: when `VAR` is killed directly via `V.DIE()` or consumed by
`BRANCH` (§14.1), the runtime calls `unlink(PATH)` *before* flipping
the alive bit (§4.7.1). The example above therefore **deletes
`input.txt`** when `F.DIE()` runs.

Passive deaths — deadline expiration, dependency propagation,
watch-path observation, signal arrival, one-shot consumption — do not
unlink. The unlink trigger fires only on direct kills of a still-alive
owner.

To release the file without deleting it, use `close` (§16.5).

The `owns_path` flag does **not** propagate. Composing the wrapper
with anything else (`BIFURCATE`, `CONCAT`, `REPLACE`, …) yields a
result that observes the file through dependency tracking but does
not own it; cloning the wrapper produces a non-owning copy. Only the
original wrapper, the object returned by `read`, owns the file.

### 16.2 `write`

```ath
write S to "out.txt";
```

`write SRC to "PATH" [as VERDICT];` (§4.4.22) opens `PATH` for
writing, truncating any existing file, walks `SRC`'s right-spine
writing each character atom's byte, and closes the file. The walk
follows the same termination rules as `PRINT2`: it stops at the
first `NULL`, dead cell, or non-character left half.

If `SRC` is `NULL` or dead at entry, the file is created and left
empty.

The optional `as VERDICT` clause binds `VERDICT` to a fresh object
that is alive iff every step succeeded (open, every write, close).
On any I/O failure the verdict is born dead. Without the clause,
the verdict is allocated internally and discarded — failures are
silent.

```ath
write S to "out.txt" as OK;
~ATH(!OK) {
    print write failed;
    BIFURCATE [NULL, NULL] OK;
}
```

The contextual marker `to` is recognized only in this position.
Outside `write` and `append`, `to` is a normal identifier.

### 16.3 `append`

```ath
append S to "out.txt" [as VERDICT];
```

Identical to `write` (§16.2) except the file is opened for appending:
existing contents are preserved and new bytes are added after.
Failure semantics and the optional verdict are the same.

### 16.4 `close`

```ath
close F;
```

`close VAR;` (§4.4.24) clears `VAR`'s `owns_path` flag, then sets its
`alive` field to false. The cleared flag means the kill does not
trigger `unlink`; the file persists. If `VAR` was already dead, the
statement is a no-op.

On an object that does not own a file, `close` is indistinguishable
from `VAR.DIE();`.

### 16.5 Inspecting a read result without deleting

Because `BRANCH(V)` consumes its subject, running a `read`-result
through `BRANCH` deletes the file. To check without releasing, clone
first (§14.2):

```ath
read "config.txt" as F;
CLONE F as FCHECK;
BRANCH(FCHECK) {
    PRINT2 F;
} ELSE {
    print no config;
}
// F is still alive and still owns the file
```

The clone does not carry `owns_path` (it is not copied by `CLONE`),
so consuming the clone in `BRANCH` does not affect the file.

---

## 17. Errors

There are two categories.

**Compile-time errors** (§6.1):

- Lexical: unterminated `/* */`, unterminated `"..."`, missing space
  after `print`, illegal character.
- Syntactic: any deviation from the grammar in §3.
- Reference to an unbound name in a read position. The check is
  syntactic and conservative: a name is in scope if introduced by
  some preceding statement in the same block or an enclosing block.
- Reference to an unknown function name in a function-call statement.
- Binding `NULL` (in any case variant of the surface form, since
  `NULL` is case-sensitive — `null` is a different name).
- File-not-found or parse error in an `importf` target.
- An `import number` literal that does not fit signed 64-bit range.

Missing C symbols declared by `import builtin` surface as **link-time**
errors, not compile-time. The compiler trusts the symbol will be
resolved when the runtime archive is linked.

`watch` paths are not validated at compile time; missing files cause
the watching object to be born dead at runtime.

**Run-time behavior** (§6.2):

There are no runtime errors. A program that passes the compile-time
checks either runs to completion or runs forever. The runtime cannot
crash, abort, or print a diagnostic. Every operation in §4.4 is
defined when its source operand is `NULL`, so even reads of variables
whose introducing statement was on an unexecuted path are
well-defined: they read `NULL`.

---

## 18. Composition modes

The runtime ships two archives that differ only in their
implementation of `ath_compose` (§4.4.3, §5.2):

- `libath_fresh.a` — `ath_compose` always allocates a new object.
- `libath_intern.a` — `ath_compose` hash-conses by the raw pointer
  pair `(left, right)`. Two composes of the same pair return the
  same object; killing it kills every name that observed it.

The CLI flag `--compose fresh|intern` selects the archive at link
time. `fresh` is the default.

The two modes are observably identical for programs that do not
depend on the distinctness or sharing of composites. The conformance
suite runs every example under both modes and requires byte-identical
output.

The difference is visible only in programs that compose the same
operand pair twice and then kill one result:

```ath
import a A;
import b B;
BIFURCATE [A, B] X;
BIFURCATE [A, B] Y;
X.DIE();
// Under fresh: Y is still alive.
// Under intern: Y is dead (X and Y are the same object).
```

A program that relies on either behavior is not portable across
modes. The recommended default is `fresh`.

---

## 19. Where to look next

- `SPEC.md` is the authoritative reference. Section numbers cited
  throughout this tutorial point into it.
- `examples/` contains a working program for every feature discussed
  here, plus a few combined examples (`fizzbuzz`, `search_replace`).
- `stdlib/` shows how runtime builtins are wrapped as ordinary `~ATH`
  function shims via `import builtin`.
- `runtime/ath_runtime.h` is the C ABI that the compiler emits calls
  against. The two implementation files `compose_fresh.c` and
  `compose_intern.c` differ only in their `ath_compose`
  implementation.
- `tests/test_conformance.py` runs every example under both
  composition modes and is the executable specification of expected
  output.
