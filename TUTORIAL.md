# The `~ATH` tutorial

this tutorial introduces `~ATH`. `SPEC.md` is for technical specs.`§X.Y` points to spec section. 

`~ATH` is built on a single data type (`object`) and two control flow structures 
the `~ATH(V)` loop (loop while object `V` is alive) and the `BRANCH` conditional statement

---

## 1. Compiling and running

the runtime is a C archive built once with `make`:

```bash
make runtime
```

a source file is compiled and linked by the Python driver:

```bash
python -m athc.cli prog.ath -o prog
./prog
```

the driver invokes `clang` (or the value of `--cc`) to assemble and
link the emitted object against the runtime archive

useful command-line flags:

- `--emit-ir` — print LLVM IR to stdout; do not link
- `--emit-obj PATH` — write the object file to `PATH` and stop before
  linking
- `--compose fresh|intern` — choose the composition discipline
  (§4.4.3). default is `fresh`
- `--runtime PATH` — link against a specific runtime archive instead of
  the default in `runtime/`
- `--cc COMPILER` — C compiler to use for the link step
- `-D NAME:MIN:MAX` (or `--define-lifetime`) — register an additional
  lifetime-library entry for this build, repeatable. see §11

two environment variables are consulted at runtime:

- `ATH_PATH` — colon-separated directories searched by the angle-bracket
  form of `IMPORTF` (§4.4.8) before the compiler-adjacent `stdlib/`
  directory
- `ATH_SEED` — decimal unsigned integer used to seed the runtime random
  source. setting it makes lifetime sampling and `RANDOM` calls
  reproducible

a program either runs to completion (returning OS exit code 0) or runs
indefinitely. there are no runtime errors

### 1.1 The REPL

to explore interactively, start the REPL with `python -m athc.cli --repl`
(or just `athc` with no source file). it evaluates statements against the same runtime so behavior
is identical

```
~ATH> IMPORT NUMBER 5 AS N;
~ATH> IMPORTF <add> AS ADD;
~ATH> ADD [N, N]R;
~ATH> PRINT $N doubled is $R;
5 doubled is 10
~ATH> :inspect R
R: live · int · 10
```

- statements end with `;`
- a block (`~ATH(…){ … }`, `BRANCH`, `LOOP`) spans
lines until its `}`. 
- meta-commands start with `:`
  - `:inspect VAR`
  - `:env`
  - `:load FILE`
  - `:reset`
  - `:compose fresh|intern`
  - `:help`
  - `:quit` (or Ctrl-D)

a few REPL-only conventions: 
- a top-level `THIS.DIE()` ends the current line
rather than the session (use `:quit` to exit)
- `INPUT` reads the next typed line
- `every N { }` (infinite in a compiled program) is bounded so it
cannot hang the session. 

building the runtime (`make runtime`) produces the
shared libraries the REPL loads

---

## 2. A first program

```ath
PRINT Hello, ~ATH!;
THIS.DIE();
```

`PRINT` writes its payload to standard output followed by a single
newline. bytes are copied through verbatim, including literal newlines in
source. a `$VAR` marker in the payload coerces the object bound to `VAR` to a string (§13.1)

six escape sequences are recognized in the payload (§2.4):

| Source | Output      |
|--------|-------------|
| `\;`   | `;`         |
| `\\`   | `\`         |
| `\n`   | line feed   |
| `\t`   | tab         |
| `\r`   | carriage return |
| `\$`   | `$`         |

a backslash followed by any other character is a compile-time
lexical error. 

string literals `"..."` recognize `\"`, `\\`, `\n`, `\t`, `\r`
(§2.3). unknown escapes are also a compile-time error.

`THIS.DIE();` terminates the program. the top-level
program and every function call owns itself as `THIS`. 
killing `THIS` returns from the activation and killing `THIS` at
the top level terminates the program (§4.4.5,
§4.5)

a program also terminates by falling off the end of its top-level
statement list, in which case `THIS.DIE();` is unnecessary but may
provide clarity

---

## 3. The object model

an object is a heap-allocated record with three primary fields:

| Field   | Meaning                                                |
|---------|--------------------------------------------------------|
| `alive` | Boolean. True for newly allocated objects, with one exception (see `NULL` below). |
| `left`  | Pointer to another object, initially unset.            |
| `right` | Pointer to another object, initially unset.            |

two rules govern these fields (§4.1):

1. `alive` transitions from true to false at most once and never back
2. `left` and `right` are set together on first decomposition (§5)
   and never change afterwards

other optional fields carry numeric payloads, lifetime extensions, and
dependency information

### 3.1 Predefined names

every activation begins with two predefined identifiers (§4.2):

- `THIS` — a fresh alive object representing the current activation. `THIS` can be rebound
- `NULL` — a globally shared, immortal-in-deadness object: `alive` is
  false and never changes. `NULL` cannot be rebound

inside a function call, a third name `ARGS` is also predefined; it is
bound to the argument object the caller provided (§4.4.9, §4.4.10).
`ARGS` is not defined at the top level. `ARGS` can be rebound

`NULL` is read-only: any statement that would rebind `NULL` is a
compile-time error

`THIS` and `NULL` must be spelled in uppercase. the identifier rule is
case-sensitive (§2.2), `this` and `Null` are distinct identifiers

### 3.2 Variables and objects

a variable is a name in the current activation's environment. it
points to exactly one object at any moment. two variables may point to
the same object. rebinding a variable does not mutate the object it
previously pointed to (§4.3)

reading a variable always reads its current binding. a read of an
unbound name yields `NULL` at runtime (§4.2). read of a variable that has
not been introduced produces `NULL`

---

## 4. Importing objects

a new alive object is introduced into the environment with `IMPORT`:

```ath
IMPORT OBJECT V;
```

the variable `V` is bound to a fresh alive object whose `left` and
`right` are unset (§4.4.1)

`IMPORT` takes one or more identifiers before the target name — at least
one is required, so `IMPORT V;` alone is a compile error. all but the
last are joined with single spaces into a concept that the
runtime looks up in the lifetime library (§11). if no library entry
matches the import allocates a plain alive object. if it matches, 
the object's lifetime is governed by the matched entry

```ath
IMPORT MAYFLY M;  // alive for somewhere between 5 minutes and 1 day
IMPORT EGG E;  // matches no entry: plain alive object
```

the first identifier after `IMPORT` must not be the contextual marker
`BUILTIN` or `NUMBER`; those dispatch to separate forms (§9, §10)

if the target variable is already bound, `IMPORT` is a no-op

---

## 5. Decomposition and composition

### 5.1 Decompose

```ath
BIFURCATE V[L, R];
```

this reads the object currently bound to `V` and binds `L` and `R` to
its left and right halves (§4.4.2). on the first decomposition of an
object, the runtime allocates two fresh alive objects and stores them
as the halves; subsequent decompositions read those same halves back.
the reads of `V` happen before the writes to `L` and `R`, so any of
`V`, `L`, `R` may overlap safely

the halves persist for the lifetime of the program. decomposing the
same object many times always yields the same two halves

### 5.2 Compose

```ath
BIFURCATE [L, R] V;
```

this reads `L` and `R`, allocates a fresh alive object with those
halves, and binds `V` to it (§4.4.3). the exact identity of the
returned object depends on the composition discipline selected at link
time:

- `fresh` mode (default): every call returns a new object. two
  composes of the same `(L, R)` produce two distinct objects
- `intern` mode (`--compose intern`): the runtime keeps a
  hash-consing table keyed by the raw pointer pair `(L, R)`. two
  composes of the same `(L, R)` return the *same* object, so killing
  one kills every name that ever obtained it from a structurally equal
  compose

both modes are identical for programs that do not depend
on the distinctness or sharing of composites

### 5.3 Distinguishing the two forms


```ath
BIFURCATE A[B, C];   // decompose A into B and C
BIFURCATE [B, C] A;  // compose B and C into A
```

---

## 6. Killing objects

```ath
V.DIE();
```

killing an object sets its `alive` field to false (§4.4.5). the
change affects only this one object; halves, parent composites, and
aliases continue to observe themselves. killing an already-dead object
is a no-op

`THIS.DIE();`  returns from the current activation 
(or terminates the program at the top level)

a second form, `V.DIE(RET);`, sets the activation's pending return
value to `RET`'s current binding *before* killing `V`. when used as
`THIS.DIE(RET);`, this is how a function returns a value (§8.3)

when the killed object owns a file (see §16 on `READ`), the runtime
also calls `unlink` on the file path before flipping `alive`. this is
the only path in the runtime that deletes files

---

## 7. The `~ATH` loop

```ath
~ATH(V)
{
    statements
}
```

the loop reads `V` from the environment, calls `ath_is_alive` on the
resulting object, and runs the body if the object is alive. after the
body, it returns to the start and re-evaluates the condition. the
loop exits when `ath_is_alive(V)` returns false (§4.4.4)

`V` is checked every iteration. rebinding `V` inside the body
changes which object is being watched. this is the canonical way to
exit a loop without killing the object originally bound to `V` (§7.3)

the body may be empty:

```ath
~ATH(V)
{
}
```

if `V` is alive at entry the loop runs forever otherwise the block is skipped

### 7.1 Inverted loops

a leading `!` inverts the condition:

```ath
~ATH(!V)
{
    statements
}
```

the body runs while `V` is dead. because objects cannot become
alive again, an inverted loop runs the body at most once (and only if
`V` was already dead at entry); if `V` is alive at entry, the body
never runs

### 7.2 The `EXECUTE` postfix

an optional `EXECUTE(IDENT)` may follow the closing brace:

```ath
IMPORTF "cleanup.ath" AS CLEANUP;
~ATH(V)
{
    statements
} EXECUTE(CLEANUP);
```

`EXECUTE(F)` calls the function `F` once, when the loop exits by its
condition, passing the subject `V` (dead, for a normal loop) as `F`'s
argument. it is the "when the subject dies, do the action" hook — handy
for an after-loop report or cleanup

```ath
~ATH(V)
{
    ...
} EXECUTE(NULL);  // the canonical no-op
```

`EXECUTE(NULL)` runs nothing — `NULL` is the empty object, not a
function. any other name must be a declared function. the hook fires
only on the normal (condition-false) exit; a `THIS.DIE()` inside the
body returns before the loop's exit, so `F` does not run — `EXECUTE` is
a death action, not a guaranteed finalizer

with the postfix, the construct terminates with `;`; without it, the
closing `}` is the terminator and no `;` follows

### 7.3 Exiting a loop by rebinding

to exit `~ATH(V)` without killing the object `V` points to, rebind `V`
to a dead object:

```ath
BIFURCATE NULL[J, V];
```

this decomposes `NULL` (legal, since every object can be decomposed),
binding `J` to its left half and `V` to its right half. both halves
are fresh alive objects — but `NULL` itself is dead, so its halves
inherit nothing from it. the detail that matters is that the
decomposition rebinds the *name* `V` to a freshly allocated object
that we promptly do not use, while leaving the *object* originally bound
to `V` untouched

a clearer idiom for the same purpose:

```ath
BIFURCATE V[L, R];  // observe the halves
V.DIE();  // kill V; halves and other aliases unaffected
```

the right idiom depends on whether other aliases of `V` need to stay
alive after the loop

to exit an inverted loop `~ATH(!V)`, rebind `V` to a fresh alive
object:

```ath
BIFURCATE [NULL, NULL] V;
```

this composes a new alive object from two `NULL` halves and binds `V`
to it. the new object is alive, so the inverted-loop condition fails
and the loop exits

### 7.4 Count and interval loops

two loops are driven by a count and a clock rather than by liveness

```ath
IMPORT NUMBER 3 AS N;
// runs the body 3 times
LOOP N
{
    PRINT tick;
}
```

`loop N { body }` (§4.4.25) runs `body` exactly `N.value` times; a
dead, payload-less, or negative `N` runs it zero times. the count is
snapshotted on entry, so rebinding `N` in the body does not change the
remaining iterations. unlike `~ATH`, it never consults liveness

```ath
IMPORT NUMBER 1000 AS SEC;
EVERY SEC
{
    // every second, forever
    PRINT poll;
}
```

`every N { body }` (§4.4.26) runs `body`, sleeps `N.value` ms, and
repeats forever — the "do this every N ms" daemon. its only exits are
the body ending the activation (`THIS.DIE()`) or a signal; code after
an `EVERY` with a non-terminating body is unreachable. recurrence is a
property of the loop, never of an object — an object cannot be revived (§6)

---

## 8. Functions

a function is a separate `.ath` file registered into the current
program by `IMPORTF`. there is no inline function definition syntax

### 8.1 Defining a function

a function file is an ordinary `~ATH` program. inside the file, the
predefined names are `THIS`, `NULL`, and `ARGS`. the file ends by killing `THIS`, optionally with a return
value:

```ath
// hello.ath
PRINT Hello from a function.;
THIS.DIE();
```

### 8.2 Registering and calling

```ath
IMPORTF "hello.ath" AS HELLO;
IMPORT A A;
IMPORT B B;
HELLO [A, B]R;
THIS.DIE();
```

`IMPORTF "PATH" AS NAME;` (§4.4.8) is a compile-time directive: it
parses the file at `PATH`, resolved relative to the importing file,
and registers it under `NAME`. function names are matched
case-insensitively. the statement emits no runtime code

each `NAME` binds to exactly one file. importing two different files
under the same `NAME` is a compile error; importing the same file under
the same `NAME` from several places is fine

a function call has two surface forms:

- compose-argument form: `FN [L, R]V;` composes `L` and `R` into
  a single argument object, calls `FN`, and binds `V` to the result
  (§4.4.9)
- decompose-result form: `FN A[L, R];` calls `FN` with `A`,
  decomposes the result, and binds `L` and `R` to its halves
  (§4.4.10)

either form may be used at any call site. the compose form is more
common when passing two operands; the decompose form is useful when
the result is logically a pair

a read operand may be a bound name *or* an inline literal — int, float,
bignum, or string — so a constant needs no `IMPORT NUMBER` first:

```ath
ADD [A, 1]NEXT_A;           // instead of: IMPORT NUMBER 1 AS ONE; ADD [A, ONE]NEXT_A;
REPEAT ["string", N3]REP;   // string literal straight into the call
S[0]FIRST;                  // literal subscript index
S[0..2]HEAD;                // literal slice bounds
```

a literal materializes a fresh object each time the statement runs.
write targets, the function name, and a subscript/slice source still
have to be names

### 8.3 Returning a value

`THIS.DIE(RET);` sets the pending return value to `RET`'s binding,
then returns. if `THIS` falls off the end of the body without an
explicit `DIE(RET)`, the return value is `NULL`

```ath
// return_args.ath
THIS.DIE(ARGS);
// returns the argument unchanged
```

### 8.4 Search-path imports

a second form of `IMPORTF` consults the `ATH_PATH` environment
variable:

```ath
IMPORTF <add> AS ADD;
```

the bare identifier between angle brackets is the file stem without
`.ath`. the runtime tries each colon-separated directory in
`ATH_PATH`, then the compiler-adjacent `stdlib/`. the first
existing `<stem>.ath` wins (§4.4.8, §5.4). this form is used to bring
in standard-library shims (§10)

### 8.5 Recursion

a function may call itself or any other registered function. all
registered functions are visible from every call site, including from
within other function bodies. mutual recursion works without forward
declarations.

---

## 9. C ABI builtins

a function whose body is implemented in C is declared with:

```ath
IMPORT BUILTIN SYMBOL AS NAME;
```

this registers `NAME` in the function registry as a direct call to
the C symbol `SYMBOL` (§4.4.12). the C function must have the
signature `ath_obj *(ath_obj *, ath_obj *)`

this should be rarely used. the standard
library uses it to expose runtime functions through ordinary `~ATH`
shim files:

```ath
// stdlib/add.ath
IMPORT BUILTIN ath_add AS ATH_ADD;
BIFURCATE ARGS[X, Y];
ATH_ADD [X, Y]R;
THIS.DIE(R);
```

a user program then brings the shim in via `IMPORTF <add> AS ADD;`
(§8.4) and calls `ADD [X, Y]R;` like any other function

---

## 10. Numbers

a number is an object that carries a numeric payload in addition to its
`alive` bit. the payload is tagged (§4.8) with a `num_kind` that says whether it
is absent, an `int64` or `double` and a union holds the
value. "has a payload" means `num_kind` is not `NONE`

a new number object is introduced with:

```ath
IMPORT NUMBER 42 AS N;  // int64 payload
IMPORT NUMBER 3.14 AS PI;  // double payload
```

a bare integer literal gives an INT payload; a literal with a `.` or an
exponent (`3.14`, `1e6`, `2.5e-3`) gives a FLOAT payload (§4.8). the
object is bound to the name (§4.4.13). an integer literal must fit
signed 64-bit range and a float literal must be finite; otherwise the
program fails to compile

a number object is eternal by default — it has no deadline,
no watch path, no awaited signal. it outlives the program. to give a
number a finite lifetime, compose it with a mortal carrier:

```ath
IMPORT NUMBER 42 AS N;
IMPORT MAYFLY M;
BIFURCATE [N, M] MORTAL;
```

`MORTAL` is alive as long as `M` is, but `BIFURCATE` composition by
itself does **not** install a dependency from `MORTAL` onto `M`'s
lifetime (§12.1) 

### 10.1 Arithmetic builtins


| Surface call            | Result                                |
|-------------------------|---------------------------------------|
| `ADD [X, Y]R;`         | `X + Y`                               |
| `SUB [X, Y]R;`         | `X - Y`                               |
| `MUL [X, Y]R;`         | `X * Y`                               |
| `DIV [X, Y]R;`         | `X / Y` (int: toward zero; float: true division) |
| `MOD [X, Y]R;`         | `X % Y` (float: `fmod`)               |
| `TO_STRING [N, _]S;`   | decimal string of `N` (§13)           |
| `PARSE [S, _]N;`       | number parsed from the string `S`     |
| `POW [X, Y]R;`         | `X` to the power `Y` (`Y >= 0`)       |
| `ABS [X, _]R;`         | magnitude of `X`                      |
| `NEG [X, _]R;`         | `-X`                                  |
| `MIN [X, Y]R;`         | lesser of `X`, `Y`                    |
| `MAX [X, Y]R;`         | greater of `X`, `Y`                   |
| `GCD [X, Y]R;`         | greatest common divisor               |
| `SIGN [X, _]R;`        | `-1`, `0`, or `1`                     |
| `BAND`/`BOR`/`BXOR`    | bitwise `&`                           |
| `BAND`/`BOR`/`BXOR`    | bitwise `\|`                          |
| `BAND`/`BOR`/`BXOR`    | bitwise `^`                           |
| `BNOT [X, _]R;`        | bitwise `~X`                          |
| `SHL [X, Y]R;`         | arithmetic-left shift                 |
| `SHR [X, Y]R;`         | arithmetic-right shift                |
| `CLAMP [X, PAIR]R;`    | `X` confined to the entangled `[LO, HI]`   |

- `CLAMP` packs its bounds with `ENTANGLE [LO, HI]PAIR;` which is the compose-pair
pattern (§13.2.3). 
- for integers, `POW` must be a positive exponent
- `ABS`/`NEG`/`GCD` reject `INT64_MIN`
- `SHL`/`SHR` require a count of 0..63.
- the arithmetic ops (`POW`/`ABS`/`NEG`/`MIN`/`MAX`/`SIGN`/`CLAMP`) promote
to float (§10.2)
- the bitwise group and `GCD` are integer-only, verdict is born dead on a float

all are brought in by name:

```ath
IMPORTF <add> AS ADD;
IMPORTF <to_string> AS TO_STRING;
```

each call returns a fresh object that is alive on success and born-dead on failure. failure conditions (§4.8.2) include:

- either operand is dead at the call site
- integer overflow (detected via the compiler's overflow intrinsics)
- division or modulo by zero, `INT64_MIN / -1`, or `INT64_MIN % -1`
- `PARSE` of a malformed string, or a value outside the representable
  range
- `TO_STRING` of a number with no payload

born-dead objects have `alive = 0` and no payload

the unary builtins `TO_STRING` and `PARSE` take two operands because
the C ABI is fixed at two `ath_obj *` arguments. the second operand
is read and discarded; convention is to pass NULL

### 10.2 Floats and the numeric tower

arithmetic mixes integers and floats by promotion, when both
operands are integers the result is an int and when either operand is a float, 
both are read as a double with the result born as a float. 
- `2` and `2.0` compare as equal
- `7 / 2` is int `3`
- `7 / 2.0` is float `3.5`
- `SUM` over a list containing any float yields a float

```ath
IMPORT NUMBER 7 AS SEVEN;
IMPORT NUMBER 2.0 AS TWO;
DIV [SEVEN, TWO]HALF;      // 3.5 (float: true division)
TO_STRING [HALF, _]HS;
PRINT 7 / 2.0 = $HS;       // => 7 / 2.0 = 3.5
```

`TO_STRING` of a float prints the shortest decimal that round-trips,
in fixed-point notation for ordinary magnitudes and scientific notation
for the extremes (`3.14`, `2500.0`, `0.0001`, but `1e+16` and `1e-05`
(the same style python uses)). it always carries a `.` or
exponent so it reads as a float

`nan`, `inf`, and `-inf` print by name.

`PARSE` returns a float when the text contains a `.` or exponent, else an
integer

float division (and `MOD`, via `fmod`) by zero does not get born-dead
it yields a live `inf` or `nan`, since those are still number-ish.

integer-only operations are born-dead on any float operand. 

helpers convert and round explicitly:

| Surface call            | Result                                       |
|-------------------------|----------------------------------------------|
| `INT_TO_FLOAT [X, _]R;`| `X` as a float                               |
| `FLOAT_TO_INT [X, _]R;`| `X` truncated to an int (nan/overflow: dead) |
| `FLOOR`/`CEIL`/`ROUND`  | round a float down / up / to nearest (still a float) |

`SQRT`, `CBRT`, `EXP`, `LOG`/`LOG2`/`LOG10`,
`SIN`/`COS`/`TAN`, `ASIN`/`ACOS`/`ATAN`, plus the binary `ATAN2 [Y, X]R;`
and `HYPOT [X, Y]R;` each return a float and an int operand promotes. 

an out-of-domain argument (e.g. `SQRT` of a
negative) yields a live `nan`

### 10.3 Bignums

an integer literal too large for int64 becomes a bignum (§4.8.7):

```ath
IMPORTF <mul> AS MUL;
IMPORT NUMBER 99999999999999999999 AS BIG;  // beyond int64: bignum
MUL [BIG, BIG]SQ;
PRINT $SQ;  // => 9999999999999999999800000000000000000001
```

bignums sit between ints and floats in the tower (FLOAT > BIG > INT): an
int operand promotes to bignum, a float operand pulls the result back to
a float approximation. there is no auto-promotion on overflow — plain
`int64` arithmetic that overflows is still born dead, you must opt into bignums explicitly

besides an over-int64 literal, the other opt-in is `INT_TO_BIGNUM`

bignum is sticky, there is no demotion back to int

```ath
IMPORTF <int_to_bignum> AS TO_BIG;
IMPORTF <mul> AS MUL;
IMPORT NUMBER 1 AS ONE;
TO_BIG [ONE, ONE]ACC;  // ACC is now a (sticky) bignum 1
IMPORT NUMBER 25 AS K;
// 25! exactly, no overflow
LOOP K
{
    MUL [ACC, K]ACC;
    ...
}
```

a bignum compares and prints identically to the equal integer (`2`,
`2.0`, and a bignum `2` all compare equal), so stickiness is invisible to
arithmetic and output. 

the difference only where a *machine* integer is required: 
the bitwise/`GCD`/`POW` operations reject bignums (born dead) and
a bignum used as a char code or out-of-range index/count is likewise rejected

---

## 11. Lifetime extensions

an object may carry up to five optional lifetime conditions in
addition to its explicit `.DIE`-driven mortality (§4.7). on every
`ath_is_alive` observation, the runtime checks them in order; the
first that fails flips `alive` to false

| Extension      | Set by                                                | Effect                                                         |
|----------------|-------------------------------------------------------|----------------------------------------------------------------|
| Deadline       | `import <library-entry>`, `TIMER N AS T;`             | Dies when the monotonic clock reaches the timestamp.           |
| Watched path   | `WATCH "PATH" AS V;`, `READ "PATH" AS V;`             | Dies when `access(F_OK)` on the path fails.                    |
| Awaited signal | `WATCH SIGNAL NAME AS V;`                             | Dies when the named POSIX signal is received.                  |
| Watched pid    | `WATCH PID N AS V;`                                   | Dies when process `N` exits (and is reaped) — `kill(pid,0)` ESRCH. |
| Watched mtime  | `WATCH MTIME "PATH" AS V;`                            | Dies when the file's modification time changes, or it is gone. |
| One-shot       | `IMPORT ONCE V;`                                      | First observation returns alive; subsequent observations dead. |
| `owns_path`    | `READ "PATH" AS V;` only                              | Combined with watch_path, direct kill calls `unlink`.          |

all death is one-way (§4.1); a dead object never becomes alive

### 11.1 The lifetime library

`IMPORT NAME... VAR;` matches the joined concept name (case-insensitive)
against a fixed runtime table. a match samples a uniformly-random
deadline from the matched range. a miss falls through to a plain
alive object

the full table is in the spec (§5.3 ) a representative selection:

| Concept name      | Range (seconds)        |
|-------------------|------------------------|
| `INSTANT`         | 0 (born dead)          |
| `TICK`            | 0.001 – 0.01           |
| `BLINK`           | 0.1 – 0.4              |
| `SECOND`          | 1 – 1                  |
| `MINUTE`          | 60 – 60                |
| `MAYFLY`          | 300 – 86 400           |
| `FLY`             | 86 400 – 259 200       |
| `HUMAN`           | 1.58e9 – 3.79e9        |
| `UNIVERSE`        | 3e100 – 3e110          |
| `FOREVER`         | 1e308 – 1e308          |
| `ONCE`            | special — see §11.2    |

names with `min == max` have zero variance. names with `min == 0` may
be born dead

the samples are deterministic when `ATH_SEED` is set to a decimal
unsigned integer in the environment

additional entries may be registered for a single build with `-D`:

```bash
python -m athc.cli prog.ath -D 'tortoise:50:150' -o prog
```

each `-D` adds an entry to a per-program user table consulted before
the built-in table, so a user entry overrides any built-in of the
same name. the reference runtime permits at most 64 user entries per
program

### 11.2 The `ONCE` entry

```ath
IMPORT ONCE V;
~ATH(V)
{
    PRINT runs exactly once.;
}
```

the first `ath_is_alive(V)` observation returns true and flips the
underlying `alive` to false; every later observation returns false.
because `~ATH` checks the condition before each iteration, the body
executes exactly once

### 11.3 Watching signals

```ath
WATCH SIGNAL SIGUSR1 AS V;
~ATH(V)
{
    PRINT waiting for SIGUSR1;
    SLEEP ONE_SEC;
}
PRINT signal received;
```

the signal name is matched case-insensitively against this set:
(§4.4.11): `SIGHUP`, `SIGINT`, `SIGQUIT`, `SIGUSR1`, `SIGUSR2`,
`SIGPIPE`, `SIGALRM`, `SIGTERM`, `SIGCHLD`. other names produce a
born-dead object and a stderr warning

the flag the runtime sets in its signal handler is latched and
process-global. all watchers of the same signal die together when the
signal arrives. a watcher allocated after a signal has already
arrived is born dead

the contextual marker `SIGNAL` is recognized only as the second token
after `WATCH`; elsewhere it is a normal identifier

### 11.4 Watching files

```ath
WATCH "/tmp/keep_alive" AS V;
~ATH(V)
{
    PRINT file still present;
    SLEEP ONE_SEC;
}
```

if the file exists at allocation time the object is born alive; if
not, it is born dead. every `ath_is_alive(V)` call runs
`access(F_OK)` on the path. the check is one-way: recreating a deleted
file does not revive the object

`WATCH` is purely observational. it does **not** create, delete, or
take ownership of the file. the owning equivalent is `READ` (§16)

### 11.5 Watching processes and file changes

two more `WATCH` forms tie liveness to external state (§4.4.11):

```ath
IMPORT NUMBER 4242 AS WPID;
WATCH PID WPID AS PROC;  // alive while process 4242 runs
~ATH(PROC)
{
    PRINT worker still up;
    SLEEP ONE_SEC;
}
PRINT worker exited;

WATCH MTIME "config.toml" AS CFG;  // alive until the file changes
~ATH(CFG)
{
    PRINT config unchanged;
    SLEEP ONE_SEC;
}
PRINT config changed;
```

`watch pid N` reads `N`'s payload as a pid and dies when `kill(pid, 0)`
reports the process is gone (after it exits *and* is reaped; a zombie
still counts as alive, and a permission error does not)

`watch mtime "PATH"` captures the file's modification time at allocation and dies the
moment a `stat()` shows a different mtime — change detection — or the
file disappears. both are latched, once dead they stay dead, so they
honor one-way death like every other lifetime source. 

`PID` and `MTIME` are contextual markers, special only right after `WATCH`

---

## 12. Comparisons and verdicts

a verdict is an object whose `alive` bit carries the truth of a
comparison: alive iff the comparison is true. verdicts carry no
payload and are observed only through `ath_is_alive`

| Surface call    | Alive when            |
|-----------------|-----------------------|
| `LT [X, Y]V;`  | `X.value < Y.value`   |
| `LE [X, Y]V;`  | `X.value <= Y.value`  |
| `EQ [X, Y]V;`  | `X.value == Y.value`  |
| `NE [X, Y]V;`  | `X.value != Y.value`  |
| `GE [X, Y]V;`  | `X.value >= Y.value`  |
| `GT [X, Y]V;`  | `X.value > Y.value`   |

a verdict is born dead if the comparison is false, if either operand
is dead, or if either operand lacks a payload

all six are primitives rather than derived forms. this is so that a
negated comparison can be combined with `AND` and `OR` (§12.3)
without needing a NOT-of-verdict construct (which would conflict with
mortality)

`~ATH(!V)` (§7.1) provides the same inversion at a single observation
site, and is the right choice when the negated verdict is consumed
immediately:

```ath
GT [X, Y]V;
// body runs while X <= Y (i.e. NOT X > Y)
~ATH(!V)
{
    ...
}
```

### 12.1 Lifetime inheritance

a true verdict installs a dependency on both operands via the runtime
helper `ath_inherit_lifetime` (§4.8.1). the runtime records the two
operands in the result's `dep1` and `dep2` slots; subsequent
`ath_is_alive` observations on the verdict return false if either
operand has since died

all arithmetic, comparison, and string builtins install operand
dependencies on their results. `BIFURCATE [L, R] V;` composition does
**not** install dependencies — this is the only common source of a
derived value whose lifetime is independent of its constituents

dependency-driven death is one-way: once an operand is dead, the
derived value is dead at every observation, and that decision is not
re-evaluated

### 12.2 A complete example

```ath
IMPORTF <add> AS ADD;
IMPORTF <lt> AS LT;

IMPORT NUMBER 0 AS ZERO;
IMPORT NUMBER 5 AS FIVE;
IMPORT NUMBER 1 AS ONE;
IMPORT NUMBER 0 AS I;

LT [I, FIVE]COND;
~ATH(COND)
{
    PRINT iteration;
    ADD [I, ONE]I;
    LT [I, FIVE]COND;  // re-evaluate
}
THIS.DIE();
```

this prints `iteration` five times. the comparison is re-evaluated
explicitly inside the loop. the verdict object bound to `COND` is replaced each iteration.
the prior verdicts become unreachable and are reclaimed at program exit

### 12.3 Combining verdicts: AND, OR

two logical combinators take verdicts (or any objects) and produce a
new verdict. (§4.8.3)

| Surface call    | Alive when                                                | Born dead when                |
|-----------------|-----------------------------------------------------------|-------------------------------|
| `AND [X, Y]V;` | both `X` and `Y` are alive at every observation           | either operand dead at call   |
| `OR [X, Y]V;` | at least one of `X`, `Y` is alive at every observation    | both operands dead at call    |

`AND` uses the conjunctive dependency machinery from §12.1: the
result inherits both operands as deps, so it becomes dead at the
next observation as soon as either operand dies, and stays dead

`OR` installs both operands as deps but evaluates them
**disjunctively**: the runtime walks both on every observation and
returns alive as long as at least one is alive. only once both are
dead does the OR-result flip to dead permanently

```ath
IMPORTF <gt> AS GT;
IMPORTF <and> AS AND;
IMPORTF <or> AS OR;

IMPORT NUMBER 0 AS ZERO;

GT [X, ZERO]X_POS;  // X > 0
GT [Y, ZERO]Y_POS;  // Y > 0
AND [X_POS, Y_POS]BOTH;  // X > 0 AND Y > 0
~ATH(BOTH)
{
    PRINT both positive;
    BOTH.DIE();
}
```

the OR-result's runtime check evaluates its two deps on every
`ath_is_alive` call; this is unavoidable since "at least one alive"
cannot be cached. AND-results, by contrast, cache the first dead
observation and become a constant-time check thereafter

### 12.4 Why there is no `NOT`

a NOT verdict is not provided. the reason is structural: such a
verdict would have to be born dead when its operand is alive, then
become alive the moment the operand died. resurrection is forbidden.
(§3, §6)

negation is expressed in two places instead:

- at an observation site, use `~ATH(!V)` or `BRANCH(!V)`. both
  re-check the condition at each observation and produce the inverse
  truth value without materializing a NOT-object
- when the negated verdict must be combined with `AND` or `OR`, use
  the contrapositive comparison primitive. for "X < Y is false AND
  Z != 0," write `GE [X, Y]V1; NE [Z, ZERO]V2; AND [V1, V2]V;`
  rather than trying to negate `LT`. this is why `LE`, `GE`, `NE`
  are primitive — they fill the gap that `NOT` would otherwise need
  to bridge

---

## 13. Strings

a string is a right-nested cons-list of character atoms,
terminated by `NULL` (§4.6)

- the empty string is `NULL`
- a non-empty string with first character `c` and tail `t` is
  `BIFURCATE [ATOM(c), t] S;`

a character atom is an alive object allocated by the runtime, exactly
one per distinct byte value (0..255). two strings sharing a character
share the same atom by pointer identity

strings enter a program through `INPUT`, `READ`, `TO_STRING`, or the
`TEXT` statement (§13.3), and are written out through `print $VAR`
interpolation, `WRITE`, or `APPEND`

### 13.1 `INPUT` and `print $VAR`

```ath
INPUT LINE;
PRINT $LINE;
THIS.DIE();
```

`INPUT VAR;` reads a single line from standard input, strips the
trailing line feed (and a preceding `\r` if present), encodes the
remaining bytes as a cons-list, and binds `VAR` to it (§4.4.7). on
end-of-file or read error, the line is treated as empty. lines longer
than the implementation's input buffer (at least 4096 bytes) are
returned in successive `INPUT` calls

`PRINT` does double duty (§2, §4.4.6). its payload is raw literal text,
but a `$VAR` marker interpolates the object bound to `VAR`. if `VAR`
holds a number, it renders as its decimal form (`TO_STRING`). 
otherwise the runtime walks `VAR`'s right-spine as a string,
writing the byte represented by each left-half atom and terminating at
the first dead cell, `NULL`, or non-character left half. a whole `PRINT`
emits one trailing line feed, no matter how many literal and interpolated
parts it has

```ath
IMPORT NUMBER 42 AS N;
INPUT NAME;
PRINT Hello, $NAME! Your number is $N.;
```

### 13.2 String operations

surfaced by the parser; the rest are stdlib
shims brought in by `IMPORTF`

the first wave — access, measurement, search-and-edit:

| Surface form                             | Operation     |
|------------------------------------------|---------------|
| `LENGTH [S, _]N;`                        | `length`      |
| `CONCAT [A, B]R;`                        | `concat`      |
| `FIND [HAY, NEEDLE]IDX;`                 | `find`        |
| `REPLACE [S, PAIR]R;` (compose-pair)     | `replace`     |
| `REPLACE_ALL [S, PAIR]R;` (compose-pair) | `replace_all` |
| `S[N] X;`                                | subscript     |
| `S[I..J] X;`                             | slice         |

the second wave — predicates (returning verdicts), transforms, and
structural reshaping. each is a stdlib shim over the two-operand ABI:

| Surface form               | Operation                        | Yields                           |
|----------------------------|----------------------------------|----------------------------------|
| `STREQ [A, B]V;`           | `streq`                          | verdict: `A` byte-equals `B`     |
| `STARTSWITH [HAY, PRE]V;`  | `startswith`                     | verdict: `HAY` begins with `PRE` |
| `ENDSWITH [HAY, SUF]V;`    | `endswith`                       | verdict: `HAY` ends with `SUF`   |
| `STRLT [A, B]V;`           | `strlt`                          | verdict: `A` < `B` (byte order)  |
| `STRGT [A, B]V;`           | `strgt`                          | verdict: `A` > `B` (byte order)  |
| `LOWER [S, _]R;`           | `lower`                          | `S` with `A`–`Z` lowercased      |
| `UPPER [S, _]R;`           | `upper`                          | `S` with `a`–`z` uppercased      |
| `TRIM [S, _]R;`            | `trim`                           | `S` without outer whitespace     |
| `LSTRIP [S, _]R;`          | `lstrip`                         | `S` without leading whitespace   |
| `RSTRIP [S, _]R;`          | `rstrip`                         | `S` without trailing whitespace  |
| `SPLIT [S, SEP]LIST;`      | `split`                          | cons-list of substrings          |
| `JOIN [LIST, SEP]R;`       | `join`                           | substrings joined by `SEP`       |
| `CONTAINS [HAY, NEEDLE]V;` | `contains`                       | verdict: `NEEDLE` occurs in `HAY`|
| `COUNT [HAY, NEEDLE]N;`    | `count`                          | # of non-overlapping occurrences |
| `RFIND [HAY, NEEDLE]IDX;`  | `rfind`                          | index of the *last* occurrence   |
| `REPEAT [S, N]R;`          | `repeat`                         | `S` repeated `N` times           |
| `REVERSE [S, _]R;`         | `reverse`                        | `S` with characters reversed     |
| `PAD_LEFT [S, N]R;`        | `pad_left`                       | `S` space-padded to width `N`    |
| `PAD_RIGHT [S, N]R;`       | `pad_right`                      | `S` space-padded to width `N`    |
| `ORD [A, _]N;`             | `ord`                            | code (0..255) of char atom `A`   |
| `CHR [N, _]S;`             | `chr`                            | length-1 string for code `N`     |
| `COMPARE [A, B]N;`         | `compare`                        | three-way `-1`/`0`/`1`           |
| `CHAR_AT [S, N]STR;`       | `char_at`                        | Nth char as a length-1 string    |
| `FIND_FROM [S, PAIR]IDX;`  | `find_from`                      | find from an offset (packed)     |
| `CAPITALIZE [S, _]R;`      | `capitalize`                     | first char up, rest down         |
| `TITLE [S, _]R;`           | `title`                          | titlecase each word              |
| `STRIP_CHARS [S, CHARS]R;` | `strip_chars`                    | strip a custom char set          |
| `… [S, CHARS] R;`          | `lstrip_chars`/`rstrip_chars`    | one-sided custom strip           |
| `… [S, PAIR] R;`           | `pad_left_with`/`pad_right_with` | pad with a custom fill char      |

`CONTAINS` is the verdict companion to `FIND`: where `FIND` is born-dead
when the needle is absent, `CONTAINS` simply yields a dead verdict, and
`COUNT` yields a live `0`

`RFIND` is `FIND` from the right

`REPEAT` and `PAD` take a number payload as their second operand; padding
uses spaces and never truncates

`ORD` and `CHR` bridge a character atom (the value `S[N]` yields) and its byte code: subscript a string
to get an atom, `ORD` it to a number, `CHR` a number back to a length-1
string. `S[N]` returns an independent snapshot of the character, so
`ORD` of it is unaffected by what happens to other strings sharing that
character

`COMPARE` collapses `STRLT`/`STREQ`/`STRGT` into one `-1`/`0`/`1` sort
key. 

`CHAR_AT` is the string-valued cousin of `S[N]` (atom). 

`FIND_FROM` (needle + start), `CLAMP` (lo +
hi), and `PAD_LEFT_WITH`/`PAD_RIGHT_WITH` (width + fill) — pack the pair
with `ENTANGLE` just like `REPLACE` (§13.2.3). 

`STRIP_CHARS` and its one-sided variants take the strip set as a plain string

the predicates feed a `BRANCH` the same as numeric comparisons.
a verdict is alive when the relation holds, dead otherwise.
every string starts and ends with the empty string, so `STARTSWITH`
and `ENDSWITH` against an empty (`NULL`) operand are always alive. the
transforms return a fresh string and pass `NULL` (or any name) as the
ignored second operand, exactly like the unary arithmetic.
`SPLIT` born-dies on an empty separator; `JOIN` of an empty list is
`NULL`. the two are inverses when the separator does not occur inside
any element

`LENGTH` returns the number of right-spine cells walked before
hitting `NULL` or a dead cell. `LENGTH` of `NULL` is `0`, not dead —
the empty string is a valid string with a well-defined length

`CONCAT` allocates a fresh cons-list containing every character atom
of `A` followed by every character atom of `B`, terminated by `NULL`.
it is born dead if either operand is dead

#### 13.2.1 Subscript and slice

```ath
IMPORT NUMBER 0 AS IDX;
S[IDX] C;
```

`S[N] X;` walks `S`'s right-spine `N.value` steps and binds
`X` to the left half of the resulting cell. for a string this yields
the Nth character's atom, not a length-1 string

`S[I..J] X;` builds a fresh cons-list of the elements in the
half-open range `[I, J)`, terminated by `NULL`. for strings this is a
substring; for other right-spine shapes it is a sublist. the slice
is born dead on out-of-range indices, on operands without payloads,
or when `I > J`

both forms install dependencies: killing the source or any index
operand invalidates the result at the next observation

the bracket-form syntax is disambiguated by the contents between the
brackets:

- `S[N] X;` — exactly one identifier: subscript
- `S[I..J] X;` — two identifiers separated by `..`: slice
- `S [L, R]X;` — two identifiers separated by `,`: function call

the single bracket form distinguishes from decomposition (`BIFURCATE
S[L, R];`) by the presence of the `BIFURCATE` keyword

#### 13.2.2 Wrapping an atom in a string

to turn an atom into a printable single-character string attach it to right spine

```ath
S[IDX] C;
BIFURCATE [C, NULL] STR;
PRINT $STR;
```

#### 13.2.3 The compose-pair pattern

`REPLACE` and `REPLACE_ALL` conceptually take three arguments —
source, needle, replacement — but the builtin ABI accepts only two.
the needle and replacement are packed into a single composite, which
the builtin decomposes internally. the recommended packer is
`ENTANGLE`:

```ath
IMPORTF <entangle> AS ENTANGLE;

ENTANGLE [NEEDLE, REPLACEMENT]PAIR;
REPLACE [S, PAIR]R;
```

`ENTANGLE` does the same composition as `BIFURCATE [L, R] V;` and
additionally installs both operands as dependencies of the result
(§12.1). killing `NEEDLE` or `REPLACEMENT` after the `REPLACE` call
then invalidates `PAIR` on the next observation, which in turn
invalidates `R` through the standard dep chain

the same packing convention is used by `ath_slice` internally for
its range endpoints (§13.2.1), but the user never writes that
composition — the slice statement emits it

##### Plain `BIFURCATE` as the no-deps alternative

```ath
BIFURCATE [NEEDLE, REPLACEMENT] PAIR;
REPLACE [S, PAIR]R;
```

`BIFURCATE` composition does **not** install deps. `PAIR` has no
internal dependency on `NEEDLE` or `REPLACEMENT`; killing either
after the call does not propagate death to `R`. this form is
correct when the carrier composite must outlive its operands. for
the typical search-and-replace use case, `ENTANGLE` is the right
choice; `BIFURCATE` is the advanced alternative

#### 13.2.4 Empty needles

`FIND` with an empty `NEEDLE` returns `0` (the empty string is
notionally a prefix at position 0). `REPLACE` and `REPLACE_ALL` with
an empty `NEEDLE` return a born-dead result: "replace nothing with
something" is deliberately undefined. use `CONCAT` to prepend or
append

### 13.3 Building strings: the `TEXT` statement

the `TEXT` statement is the surface form for constructing strings in
source code (§4.4.24). it has two forms — a single string literal
(primitive) and an interpolation (sugar) — distinguished only by the
number and kinds of parts before `AS`

#### 13.3.1 Primitive form

```ath
TEXT "hello world" AS GREETING;
PRINT $GREETING;
```

`TEXT "..." AS VAR;` decodes the string literal (with the §2.3
escapes applied) and binds `VAR` to the resulting cons-list of
character atoms. an empty literal `TEXT "" AS VAR;` binds `VAR` to
`NULL` (which is the empty string per §13)

source-level newlines inside the literal are taken literally too —
either embed them directly or use the `\n` escape:

```ath
TEXT "first line\nsecond line" AS TWO;
PRINT $TWO;
```

#### 13.3.2 Interpolation form

```ath
IMPORT NUMBER 42 AS N;
TEXT "value: " N " (end)" AS MSG;
PRINT $MSG;
```

a `TEXT` statement may contain any sequence of STRING literals and
identifiers before `AS`. each part is reduced to a string and the
parts are concatenated left to right

- a STRING part contributes its decoded byte sequence
- an IDENT part is read and coerced: if it carries an int64
  payload (a number), the runtime calls `TO_STRING` to produce its
  decimal representation; otherwise the value is treated as already
  a string (or an existing cons-list) and passed through

the final value installs operand dependencies via `ath_concat`'s dep
machinery (§12.1): killing any of the source identifiers after the
`TEXT` statement runs invalidates `MSG` at the next observation

#### 13.3.3 Single-IDENT case

```ath
TEXT N AS M;
```

equivalent to `TO_STRING [N, NULL]M;` when `N` carries a payload.
when `N` is already a string, `M` becomes a pointer-alias of `N` (no
copy). rarely useful on its own, but consistent with the
interpolation rule

#### 13.3.4 What `TEXT` is and is not

`TEXT` is the only source-level way to introduce a string value.
it is **not** the same as `PRINT`:

| Form        | Source contains              | Result                       |
|-------------|------------------------------|------------------------------|
| `PRINT TEXT;` | raw bytes up to `;`        | written to stdout immediately, no value bound |
| `TEXT "..." AS V;` | a string literal      | a string-cons-list bound to V |

to print a constructed string, interpolate it: `PRINT $V;`. to emit a
fixed literal that needs no value, write the text directly: `print
hello;`. a single `print` mixes both — `print Result: $V done;` — so
reach for interpolation whenever any part of the line is dynamic

### 13.4 Lists

a string is a right-nested cons-list of character atoms; a list is
the same shape with arbitrary elements. build one with `BIFURCATE`,
head first:

```ath
IMPORT NUMBER 8 AS N8;
IMPORT NUMBER 3 AS N3;
IMPORT NUMBER 4 AS N4;
BIFURCATE [N8, NULL] L1;  // [8]
BIFURCATE [N3, L1] L2;  // [3, 8]
BIFURCATE [N4, L2] LIST;  // [4, 3, 8]
```

a family of stdlib shims folds and slices number lists (§4.8.6):

| Surface call            | Result                                  |
|-------------------------|-----------------------------------------|
| `SUM [LIST, _]N;`      | Σ of element payloads (empty → `0`)     |
| `PRODUCT [LIST, _]N;`  | Π of element payloads (empty → `1`)     |
| `MAXIMUM [LIST, _]N;`  | greatest element (empty → dead)         |
| `MINIMUM [LIST, _]N;`  | least element (empty → dead)            |
| `MEMBER [LIST, X]V;`   | verdict: some element payload equals `X`|
| `TAKE [LIST, N]R;`     | fresh list of the first `N` elements    |
| `DROP [LIST, N]R;`     | fresh list of all but the first `N`     |
| `ALL_OF [LIST, _]V;`   | verdict: every element alive (n-ary AND)|
| `ANY_OF [LIST, _]V;`   | verdict: some element alive (n-ary OR)  |

`ALL_OF`/`ANY_OF` read each element as a lifetime rather than a
payload: they fold the `AND`/`OR` verdicts (§12) over the list, so the
result is dep-tracked — killing an element later invalidates the
combined verdict. use them for "wait for all / any of these." (there is
no `none_of`: a verdict that came alive as its operand died would break
mortality)

these read each element's payload, so they work on number lists and
are born-dead on a string. `LENGTH`, `S[N]`, and `S[I..J]` already apply to any list.
there is no `map`/`filter`/`reduce` — ~ATH has no first-class
functions to pass — so list processing stays at the level of these
fixed folds plus `SPLIT`/`JOIN` (§13.2) for strings

---

## 14. `BRANCH` and `CLONE`

`BRANCH` is a one-shot dispatch on an object's liveness. `CLONE`
copies an object so it can be inspected without destroying the
original. they are typically used together

### 14.1 `BRANCH`

```ath
BRANCH(V)
{
    statements run when V is alive
}
ELSE
{
    statements run when V is DEAD
}
```

the `ELSE` keyword is optional sugar; `BRANCH(V) { } { }` and
`BRANCH(V) { } ELSE { }` parse identically. the else clause itself is
optional; omitting it skips the dispatch when `V` is dead

after whichever body runs (or after the skipped dispatch), the
runtime calls `ath_die` on `V`. `V` is therefore guaranteed dead on
exit from a `BRANCH`, regardless of which arm ran (§4.4.16)

the inverted form swaps which arm runs:

```ath
BRANCH(!V)
{
    runs when V is DEAD
}
ELSE
{
    runs when V is alive
}
```

`V` is still consumed

if a body rebinds `V`, the post-dispatch kill reads the *current*
binding and kills that. a `THIS.DIE(...)` inside a body returns from
the activation immediately, and the post-dispatch kill never runs

### 14.2 `CLONE`

```ath
CLONE V AS W;
```

`CLONE` allocates a fresh object `W` that copies, field by field,
from `V`'s current binding at clone time (§4.4.17):

- `alive` — `W` reflects `V`'s **currently observable** liveness, not
  the raw alive bit. the runtime evaluates `V` through a pure
  non-mutating check (the same predicate `ath_is_alive` uses, minus
  the side effects) and stores the result. this matters when `V`'s
  upstream operands have died since `V` was last directly observed —
  the clone captures the dep-walk result rather than `V`'s stale bit
- `left`, `right` — pointer-copied; the deeper structure is shared
- `num_kind`, `num` — full numeric payload copy
- `deadline_s`, `watch_path`, `is_oneshot`, `awaiting_signal`,
  `dep_mode` — every intrinsic lifetime extension and the
  dep-evaluation mode are copied. a clone of a one-shot is itself a
  one-shot; a clone of a file watcher watches the same path; a clone
  of a deadlined object dies at the same deadline

the clone of a one-shot is *not* consumed by the act of cloning.
the non-mutating refresh used for the alive bit does not trip
`is_oneshot`. so `CLONE V AS W;` on a fresh one-shot leaves `V`
unfired and produces `W` as an independent fresh one-shot

`W` does *not* copy `V`'s `dep1`/`dep2`, and does not copy the
`owns_path` flag (§16). `W` is independent: killing one of `V`
or `W` does not affect the other. because `dep_mode` is copied but
the deps are not, an OR-mode clone with no deps degenerates to a
plain alive/dead object that trusts its captured bit — the snapshot
is frozen at clone time and won't re-evaluate as upstream operands
change later

cloning `NULL` yields a fresh born-dead object

### 14.3 Non-destructive checking

the canonical pattern for inspecting an object without killing it:

```ath
CLONE V AS VCHECK;
BRANCH(VCHECK)
{
    body
}
ELSE
{
    body
}
// V is still alive (assuming it was) and untouched
```

`VCHECK` is consumed by the `BRANCH`. `V` is unaffected

this is the *only* way to consume a comparison verdict without
losing the underlying operands — the verdict itself can be cloned and
the clone passed to `BRANCH`

---

## 15. Time, sleep, timers, randomness

the runtime exposes a monotonic clock, a sleep primitive, a deadline
allocator, and a uniform random source

### 15.1 `NOW`

```ath
IMPORTF <now> AS NOW;
NOW [NULL, NULL]T;
```

`NOW` returns a fresh number-payload object whose value is monotonic
milliseconds since the system's monotonic-clock origin (typically
boot). successive calls within an activation observe non-decreasing
values. the result is **not** dep-tracked against its operands — both
are ignored — and the operands are conventionally `NULL`

the zero point is not a wall-clock epoch. `NOW` is useful only for
measuring elapsed time:

```ath
NOW [NULL, NULL]T0;
// ... work ...
NOW [NULL, NULL]T1;
SUB [T1, T0]ELAPSED;
```

### 15.2 `SLEEP`

```ath
SLEEP N;
```

`SLEEP N;` blocks the current activation for `N.value` milliseconds
(§4.4.18). if `N` is `NULL`, dead, lacks a payload, or carries a
non-positive value, `SLEEP` returns immediately as a no-op

the implementation uses `nanosleep`; interrupted sleeps may return
early. the duration is read at the start of the call, so changing
`N`'s binding mid-sleep has no effect

`SLEEP` does not consume `N` and has no return value

### 15.3 `TIMER`

```ath
TIMER N AS T;
```

`TIMER` allocates a fresh alive object whose deadline is set to
`N.value` milliseconds from now, and binds `T` to it (§4.4.19). `T`
becomes observably dead when the deadline is reached

`T` is independent of `N`: no dependency is installed, so killing
`N` after the `TIMER` call does not affect `T`. the duration is
consumed at allocation time

the combination of `TIMER` with `~ATH` gives a bounded loop:

```ath
IMPORT NUMBER 5000 AS FIVE_SEC;
IMPORT NUMBER 1000 AS ONE_SEC;
TIMER FIVE_SEC AS T;
~ATH(T)
{
    PRINT still running;
    SLEEP ONE_SEC;
}
PRINT timed out;
```

### 15.4 `RANDOM`

```ath
IMPORTF <random> AS RANDOM;
IMPORT NUMBER 0 AS LO;
IMPORT NUMBER 100 AS HI;
RANDOM [LO, HI]R;
```

`RANDOM` returns a fresh number-payload object whose value is
uniformly distributed over the half-open interval
`[LO.value, HI.value)`. it is born dead if either operand is dead,
lacks a payload, or if `LO.value >= HI.value`

the random source is seeded once at runtime startup, either from
`ATH_SEED` or from the wall clock. subsequent draws within a single
process consume that sequence; setting `ATH_SEED` makes the entire
sequence reproducible

`RANDOM`'s result is not dep-tracked against its bounds: once
drawn, killing `LO` or `HI` does not invalidate the result

---

## 16. File I/O

four statements interact with the filesystem:

- `READ "PATH" AS VAR;` — slurp a file into a string-cons-list and
  take ownership
- `WRITE SRC TO "PATH" [AS VERDICT];` — truncate-and-write a string
- `APPEND SRC TO "PATH" [AS VERDICT];` — append a string
- `CLOSE VAR;` — release ownership and kill

`WATCH "PATH" AS V;` (§11.4) is also file-related but is observational
only and was covered earlier

### 16.1 `READ`

```ath
READ "input.txt" AS F;
PRINT $F;
F.DIE();
```

`READ "PATH" AS VAR;` (§4.4.20) opens `PATH` for reading (relative to
the program's current working directory) and binds `VAR` to a
cons-list containing the file's bytes as character atoms. on any
failure — missing file, permission denied, I/O error — `VAR` is bound
to a born-dead object

the head of the cons-list is a wrapper object carrying:

- `watch_path` set to a copy of `PATH`. every `ath_is_alive` call on
  `VAR` (or any object that inherits `VAR`'s deps) runs
  `access(F_OK)` and dies if the file is gone
- `owns_path` set to 1. this flags the wrapper as the owner of the
  file

the wrapper is non-interned even under `--compose intern`, two
`READ` calls on the same path return distinct wrappers

the combination of `watch_path` and `owns_path` triggers a special
rule: when `VAR` is killed directly via `V.DIE()` or consumed by
`BRANCH` (§14.1), the runtime calls `unlink(PATH)` *before* flipping
the alive bit (§4.7.1). the example above therefore **deletes
`input.txt`** when `F.DIE()` runs

passive deaths — deadline expiration, dependency propagation,
watch-path observation, signal arrival, one-shot consumption — do not
unlink. the unlink trigger fires only on direct kills of a still-alive
owner

to release the file without deleting it, use `CLOSE` (§16.5)

the `owns_path` flag does not propagate. composing the wrapper
with anything else (`BIFURCATE`, `CONCAT`, `REPLACE`, …) yields a
result that observes the file through dependency tracking but does
not own it; cloning the wrapper produces a non-owning copy. only the
original wrapper, the object returned by `READ`, owns the file

### 16.2 `WRITE`

```ath
WRITE S TO "out.txt";
```

`WRITE SRC TO "PATH" [AS VERDICT];` (§4.4.21) opens `PATH` for
writing, truncating any existing file, walks `SRC`'s right-spine
writing each character atom's byte, and closes the file. the walk
follows the same termination rules as `print $VAR` interpolation: it stops at the
first `NULL`, dead cell, or non-character left half

if `SRC` is `NULL` or dead at entry, the file is created and left
empty

the optional `as VERDICT` clause binds `VERDICT` to a fresh object
that is alive iff every step succeeded (open, every write, close).
on any I/O failure the verdict is born dead. without the clause,
the verdict is allocated internally and discarded

```ath
WRITE S TO "out.txt" AS OK;
~ATH(!OK)
{
    PRINT write failed;
    BIFURCATE [NULL, NULL] OK;
}
```

the contextual marker `TO` is recognized only in this position.
outside `WRITE` and `APPEND`, `TO` is a normal identifier

### 16.3 `APPEND`

```ath
APPEND S TO "out.txt" [AS VERDICT];
```

identical to `WRITE` (§16.2) except the file is opened for appending:
existing contents are preserved and new bytes are added after.
failure semantics and the optional verdict are the same

### 16.4 `CLOSE`

```ath
CLOSE F;
```

`CLOSE VAR;` (§4.4.23) clears `VAR`'s `owns_path` flag, then sets its
`alive` field to false. the cleared flag means the kill does not
trigger `unlink`; the file persists. if `VAR` was already dead, the
statement is a no-op

on an object that does not own a file, `CLOSE` is indistinguishable
from `VAR.DIE();`

### 16.5 Inspecting a read result without deleting

because `BRANCH(V)` consumes its subject, running a `READ`-result
through `BRANCH` deletes the file. to check without releasing, clone
first (§14.2):

```ath
READ "config.txt" AS F;
// F is still alive and still owns the file
CLONE F AS FCHECK;
BRANCH(FCHECK)
{
    PRINT $F;
}
ELSE
{
    PRINT no config;
}
```
---

## 17. Errors

there are two categories

**compile-time errors** (§6.1):

- lexical: unterminated `/* */`, unterminated `"..."`, missing space
  after `PRINT`, illegal character
- syntactic: any deviation from the grammar in §3
- reference to an unbound name in a read position. the check is
  syntactic and conservative: a name is in scope if introduced by
  some preceding statement in the same block or an enclosing block
- reference to an unknown function name in a function-call statement
- binding `NULL` (in any case variant of the surface form, since
  `NULL` is case-sensitive — `null` is a different name)
- file-not-found or parse error in an `IMPORTF` target
- an `import number` literal that does not fit signed 64-bit range

missing C symbols declared by `import builtin` surface as **link-time**
errors, not compile-time. the compiler trusts the symbol will be
resolved when the runtime archive is linked

`WATCH` paths are not validated at compile time; missing files cause
the watching object to be born dead at runtime

**run-time behavior** (§6.2):

there are no runtime errors. a program that passes the compile-time
checks either runs to completion or runs forever. the runtime cannot
crash, abort, or print a diagnostic. every operation in §4.4 is
defined when its source operand is `NULL`, so even reads of variables
whose introducing statement was on an unexecuted path are
well-defined: they read `NULL`

---

## 18. Composition modes

the runtime ships two archives that differ only in their
implementation of `ath_compose` (§4.4.3, §5.2):

- `libath_fresh.a` — `ath_compose` always allocates a new object
- `libath_intern.a` — `ath_compose` hash-conses by the raw pointer
  pair `(left, right)`. two composes of the same pair return the
  same object; killing it kills every name that observed it

the CLI flag `--compose fresh|intern` selects the archive at link
time. `fresh` is the default

the two modes are observably identical for programs that do not
depend on the distinctness or sharing of composites.

the difference is visible only in programs that compose the same
operand pair twice and then kill one result:

```ath
IMPORT A A;
IMPORT B B;
BIFURCATE [A, B] X;
BIFURCATE [A, B] Y;
// under fresh: Y is still alive
// under intern: Y is dead (X and Y are the same object)
X.DIE();
```

a program that relies on either behavior is not portable across
modes. the recommended default is `fresh`

---

## 19. Concurrency: actors, channels, nurseries

~ATH has optional *cooperative* concurrency built on the same liveness
machinery you already know. One thread, no locks, no preemption: actors
run one at a time and hand control back only at explicit points (`yield`,
`recv`, `join`, and `sleep` inside an actor). A program that never spawns
behaves exactly as before (§7 of `SPEC.md`).

An **actor** is just an `importf` function running on its own stack.
`spawn` starts one and binds a *handle* that is alive while the actor
runs:

```ath
SPAWN WORKER 1 AS A;   // run WORKER with ARGS = 1
JOIN A;                // drive the scheduler until A finishes
```

Actors talk over **channels** — closeable FIFO mailboxes. The consumer
drains a channel by looping on the received message: `recv` keeps
returning buffered messages, and once the channel is closed *and* empty it
returns a dead object, which ends the loop:

```ath
// consumer.ath — ARGS is the channel
RECV FROM ARGS AS M;
~ATH(M) {
    PRINT $M;
    RECV FROM ARGS AS M;
}
THIS.DIE();
```

```ath
// main.ath
IMPORTF "consumer.ath" AS CONSUMER;
CHANNEL AS C;
SPAWN CONSUMER C AS A;
SEND 10 TO C;
SEND 20 TO C;
CLOSE C;        // close == die; the consumer stops at EOF
JOIN A;
PRINT done;
THIS.DIE();
```

A **nursery** supervises a group. `spawn ... into N` scopes a child to it;
`join N` waits for the whole group; `N.DIE()` cancels the subtree — each
child notices at its next `recv`/`yield` and unwinds:

```ath
NURSERY AS N;
SPAWN WORKER 1 INTO N AS A;
SPAWN WORKER 2 INTO N AS B;
JOIN N;         // alive while any child runs; dies when all finish
```

"close == die" and "alive while any child runs" are not new mechanisms —
they are the ordinary liveness model (§6, §11) applied to mailboxes and
supervision. Cancellation is cooperative: an actor that never yields can't
be cancelled. Scheduling is deterministic (FIFO spawn order, FIFO
mailboxes, round-robin), so actor programs produce the same output under
both composition modes. The concurrency statements run in compiled
programs only — the REPL reports a clear error for them.

Runnable examples live under `examples/actors/`, from basic to advanced:
`hello_actor`, `yield_interleave`, `mailbox`, `producer_consumer`, `fan_out`,
`fork_join`, `ping_pong`, `supervisor_cancel`, `pipeline`, and `actor_sleep`.

---

## 20. Where to look next

- `SPEC.md` is the authoritative reference. section numbers cited
  throughout this tutorial point into it
- `examples/` is organized by topic — `basics/`, `object_model/`,
  `control_flow/`, `numbers/`, `strings/`, `liveness/`, `io/`,
  concurrency (under `actors/`), and `programs/` (showcase apps like
  `calculator`, `maze`, `sudoku`, `brainfuck`). See `examples/README.md`
  for the index
- `stdlib/` shows how runtime builtins are wrapped as ordinary `~ATH`
  function shims via `import builtin`
- `runtime/ath_runtime.h` is the C ABI that the compiler emits calls
  against. the two implementation files `compose_fresh.c` and
  `compose_intern.c` differ only in their `ath_compose`
  implementation
- `tests/test_conformance.py` runs every example under both
  composition modes and is the executable specification of expected
  output
