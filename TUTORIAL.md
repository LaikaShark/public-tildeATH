# A Tutorial for `~ATH`

This tutorial walks you from "I just installed this" to "I can write
recursive functions over encoded numbers" in the dialect of `~ATH`
implemented by this compiler. It's not a reference — that's `SPEC.md`.
It's a *path* through the language.

`~ATH` ("til death") originates as a fictional esolang in *Homestuck*.
Programs in it consist of infinite loops bound to the lifespan of
imported "concepts" (the universe, an author, a meson, etc.), with loop
bodies that only run while the concept is alive. The implementable
variant — pioneered by drocta — drops the "wait for the universe to
actually end" requirement and replaces concepts with a Turing-complete
substrate of objects that live, die exactly once, and can be split and
combined. This compiler targets a custom hybrid of the two: the drocta
core, with the Homestuck surface (`~ATH(!U) { ... } EXECUTE(NULL);`,
multi-word `import dead grandmother G;`) layered on top.

If you've programmed before, **forget most of it**. There are no numbers,
no strings, no operators, no `if`, no `while`, and no return values in
the conventional sense. There are objects, the `~ATH` loop, and `.DIE`.
That's the whole computation model.

---

## 1. Compiling and running

You'll need the runtime built once:

```bash
make runtime
```

Then point the compiler at a source file:

```bash
.venv/bin/python -m athc.cli examples/hello.ath -o hello
./hello
```

Useful flags:

- `--emit-ir` — print LLVM IR to stdout (don't link)
- `--emit-obj PATH` — write the object file and stop
- `-D NAME:MIN:MAX` — register a custom library entry for this build
  (repeatable; see §10b.1)
- `--compose fresh|intern` — composition discipline (default `fresh`).
  Under `intern`, two `BIFURCATE [L,R] V;` calls with the same operands
  return *the same object* (hash-consing). Killing it kills every place
  it's referenced.

Programs run forever or terminate via `THIS.DIE();`. If you fork-bomb the
recursion, you'll need `Ctrl-C`.

---

## 2. Hello, world

```ath
print Hello, ~ATH!;
THIS.DIE();
```

Two statements. `print` writes its payload — every character between the
required single space after `print` and the next `;` — followed by a
newline. `THIS.DIE();` terminates the program.

Two surprises here:

- `print` has no quotes and no escape mechanism. The payload runs verbatim
  up to the next `;`. The semicolon is the terminator, period — you can't
  print a semicolon. (If you need real string output, see §6.)
- `THIS.DIE();` isn't a polite "please exit." It is *the only way* a
  top-level program ends besides falling off the end of source. We'll see
  why in a moment.

Try changing the message and recompile. Notice that `print` doesn't strip
whitespace, doesn't interpret `\n`, doesn't do anything clever.

---

## 3. Objects, life, and death

The single data type is the **object**. Every object has three fields:

```
{ alive: bool, left: object*, right: object* }
```

- `alive` is `true` when the object is allocated and becomes `false`
  exactly once, when something kills it. There is no resurrection.
- `left` and `right` are pointers to other objects, initially unset.

That's it. There are no integers, no strings, no booleans, no
references-to-something-else. Just nested pairs of living-or-dead nodes.

You make a new object with `import`:

```ath
import flavor A;
```

`flavor` is metadata (any IDENT — preserved for tooling, ignored at
runtime). `A` is the variable bound to a fresh **alive** object. From
now on the variable `A` lets you observe and mutate this object.

You kill it with `.DIE`:

```ath
A.DIE();
```

The object that `A` points to becomes dead. Any other variable still
pointing at that same object will also observe it as dead, but no other
object is touched. Death is a property of *objects*, not of variables.

There's exactly one **predefined dead object**, the global `NULL`. It is
born dead and stays dead. You cannot rebind the name `NULL` — that's a
compile-time error — but you can read it as much as you want.

The other predefined name is **`THIS`**: a fresh alive object specific
to this program's main activation. Killing `THIS` ends the program (see
§7). Functions get their own `THIS` (see §8).

---

## 4. The `~ATH` loop

`~ATH` ("til death") is the only loop. Pronounced *til-death*.

```ath
~ATH(V) {
    // body
}
```

The semantics:

1. Read what `V` currently points to.
2. If that object is alive, run the body. Loop.
3. If it's dead, exit.

Crucially, the variable is **re-read every iteration**. If the body
reassigns `V`, the next iteration checks the *new* target. That's how
you make these loops do anything useful: you arrange for the variable to
eventually point at something dead.

Trivially terminating loop:

```ath
import x V;
V.DIE();
~ATH(V) {
    print this never runs;
}
```

The check happens *before* the first iteration. `V` is dead, body
doesn't run.

Loop that runs exactly once:

```ath
import x V;
~ATH(V) {
    print exactly once;
    V.DIE();
}
```

First check: `V` alive, enter. Body kills `V`. Loop re-checks: `V` is
now dead, exit.

There is no `break`, no `continue`, no nested-loop labels. If you want
to "break early," you arrange for the variable to be dead by the next
check. There is no `if`, no `else`, no `switch`. Every conditional in the
language is structured as `~ATH(V) { ...; V.DIE(); }` — a loop that runs
once when `V` happens to be alive.

This is the whole control-flow story.

---

## 5. Bifurcation

How do you build structures, given that you only have objects?
You use **`BIFURCATE`**. It has two forms.

**Decompose** splits an object into its halves:

```ath
BIFURCATE V[L, R];
```

This reads what `V` points to, then binds `L` to its left half and `R`
to its right half. If the object had no halves yet (a fresh `import`),
the runtime allocates two fresh alive objects, sets them as the halves,
and returns those. Subsequent decompositions of the same object return
the same halves.

**Compose** is the inverse:

```ath
BIFURCATE [L, R] V;
```

Allocates a new alive object whose left half is `L`'s current object and
right half is `R`'s, then binds `V` to it. *Each call to compose
produces a freshly-allocated object* — two `BIFURCATE [L, R] V;`
statements with the same operands produce two distinct objects. (A
hash-cons / interning mode is reserved in the spec but not built in v1.)

Composing and decomposing are the only ways to navigate object
structure. Killing a composite doesn't kill its halves; killing a half
doesn't kill the composites it's part of. These are independent objects
that just happen to point at each other.

A small worked example:

```ath
import a A;
import b B;
BIFURCATE [A, B] C;       // C is a new composite, alive
BIFURCATE C[X, Y];        // X points to A's object, Y points to B's
C.DIE();                  // C is dead. A and B (and X, Y) are still alive.
```

A subtle point: in `BIFURCATE V[L, R]`, the reads happen before the
writes. So `BIFURCATE V[V, JUNK]` reads `V`'s halves, then rebinds `V`
to the left half — `V` now points "one level into" its former target.
This is the building block of iteration.

---

## 6. Strings and I/O

The only typed I/O is for **strings**, and strings are encoded as
linked lists of objects.

Each ASCII byte value `c` has a globally canonical alive object called
its **character atom**, allocated lazily by the runtime the first time
that byte is seen. A string is then a chain:

- the empty string is `NULL`,
- a non-empty string with first character `c` and tail `t` is
  `compose(ATOM(c), t)`.

So `"Hi"` is `compose(ATOM('H'), compose(ATOM('i'), NULL))`. The atom for
`'H'` is shared with every other string that contains `'H'`.

You read a line from stdin and emit a string:

```ath
INPUT line;
PRINT2 line;
```

`INPUT VAR;` reads one line of stdin, strips the trailing `\n` (and `\r`
if present), constructs the cons-list, and binds `VAR`. EOF or read
error yields the empty string (`NULL`).

`PRINT2 VAR;` walks the cons-list, looks up each left-half atom in the
character table, and writes the characters to stdout followed by a
newline. If the variable doesn't point at a well-formed string, walking
stops at the first unrecognized atom.

Running `examples/echo.ath`:

```ath
INPUT line;
PRINT2 line;
THIS.DIE();
```

```bash
$ echo "roundtrip" | ./echo
roundtrip
```

You can also construct strings by hand. A one-character string from an
atom you already have:

```ath
BIFURCATE [CH, NULL] single;
PRINT2 single;
```

This is how `examples/first_char` extracts and prints just the head atom
of a line.

---

## 7. THIS and program termination

Every activation — the main program and each function call — has its
own `THIS`. Killing `THIS` ends *the current activation*. For main, that
means the program exits. For a function, it means return-from-function.

There are two ways the program ends:

- `THIS.DIE();` runs in main, or
- Control falls off the bottom of the top-level statement list.

These have identical effects. The OS exit code is always `0`.

If you write `THIS.DIE();` *inside* an `~ATH` loop, the program ends
right there — the loop's surrounding code never runs.

```ath
~ATH(V) {
    print before;
    THIS.DIE();
    print never reached;
}
print also never reached;
```

`THIS.DIE();` is the universal "stop the world" within an activation.

---

## 8. Functions

A function in this dialect is a `.ath` file that takes one argument
object, has its own local environment, and returns one object. Functions
are imported from other files at compile time.

### 8.1 Declaring a function

`hello.ath`:

```ath
print hello from a function;
THIS.DIE();
```

That's it. The whole file is the function body. The function's local
environment, when it runs, starts with:

- `THIS` — a fresh alive object for this activation
- `NULL` — the global dead singleton
- `ARGS` — the object passed by the caller (only available inside a
  function, never in main)

### 8.2 Calling a function

You declare the function at compile time with `importf`, then call it
from any statement context.

`main.ath`:

```ath
importf "hello.ath" as HELLO;
import a A;
import b B;
HELLO [A, B] R;
print main is done;
THIS.DIE();
```

`importf "PATH" as NAME;` is a compile-time directive: the compiler
opens the file (relative to the directory of the file containing this
statement), parses it, and registers it under `NAME` (case-insensitive).
Like other keywords/function names, function names are case-insensitive
when called, even though regular identifiers are case-sensitive.

There are two ways to call:

```ath
FN [L, R] V;        // compose-arg form: arg = compose(L, R); V = result
FN A [B, C];        // decompose-result form: arg = A; (B, C) = decompose(result)
```

The compose-arg form is most natural for "pass two values, get one
back." The decompose-result form is for "pass one structured value, take
it apart on return." There's no zero-arg or two-arg call form — every
function takes one object and returns one object.

### 8.3 Returning a value

A function's "pending return value" defaults to `NULL`. The `.DIE`
statement has an optional argument:

```ath
V.DIE(R);
```

This first reads `R`'s current binding and stores it as the pending
return value, then kills `V`. The argument is read *before* the kill, so
`THIS.DIE(THIS);` returns the (still-live) THIS pointer — by the time
the caller sees it, that object is dead, but the pointer is still valid.

When `THIS.DIE();` (with or without arg) runs in a function, the
function returns. If the function falls off the end without ever calling
`THIS.DIE`, it returns the most recently stored pending value
(defaulting to `NULL`).

The identity function (`examples/identity/id.ath`) is the smallest
non-trivial function:

```ath
THIS.DIE(ARGS);
```

It returns its argument unchanged.

### 8.4 Mutual recursion

Functions can call any other function in the compilation unit, including
themselves and each other. `examples/fizzbuzz/` has three functions
cycling FIZZ → BUZZ → FIZZBUZZ → FIZZ. No special declarations needed —
all functions are forward-declared at codegen time, so cross-calls just
work.

---

## 9. An idiom cookbook

The language has only one control structure and no scalar types. Almost
every program is built from a handful of patterns. Once you recognize
them you can read `~ATH` code fluently.

### 9.1 The `~ATH(V) { ...; V.DIE(); }` if-then

Use a fresh `import`ed variable as a one-shot "did this branch fire?"
flag. The body runs at most once because you kill the flag at the end:

```ath
import flag F;
// ... maybe kill F under some condition ...
~ATH(F) {
    print branch taken;
    F.DIE();
}
```

This is the basis of every conditional in the language. When the flag
needs to *not* be killed (because something else relies on the
underlying object), use the next idiom instead.

### 9.2 Rebind to NULL (terminate without killing)

When the loop variable's underlying object is shared with other code —
e.g., across recursive call frames where everyone's `LEFT` points to
the same alive sentinel — killing it would corrupt other callers.
Instead, rebind the variable to `NULL`:

```ath
~ATH(LEFT) {
    // ...
    BIFURCATE NULL[JUNK, LEFT];      // LEFT now points to NULL (dead)
}
```

`BIFURCATE NULL[J, V]` decomposes the NULL singleton — which the
runtime guarantees yields `(NULL, NULL)` without ever mutating the
singleton — and binds `V` to that dead value. The original object that
`V` pointed at is unaffected.

This is the standard "break out of this loop" idiom inside recursive
functions. See `examples/countdown/countdown.ath`.

### 9.3 Rebind to alive

The mirror image: you want to make the loop variable *alive* again
(usually to escape an inverted `~ATH(!V)` loop). Compose two `NULL`s:

```ath
~ATH(!V) {
    print V is dead;
    BIFURCATE [NULL, NULL] V;        // V now points to a fresh alive composite
}
```

`compose(NULL, NULL)` returns a freshly-allocated alive object whose
halves happen to be the dead singleton. The original dead object that
`V` pointed at is unaffected.

### 9.4 Identity and pass-through

Smallest interesting function — `examples/identity/id.ath`:

```ath
THIS.DIE(ARGS);
```

Returns its input. Useful for testing the call mechanism, and as a
building block.

### 9.5 One-character string from an atom

If you decomposed a string and have a head character atom in `CH`, you
can build a single-character string by terminating with `NULL`:

```ath
BIFURCATE [CH, NULL] single;
PRINT2 single;
```

---

## 10. Encoding numbers

`~ATH` has no integers, but you can encode positive integers as object
structure. The convention this tutorial uses (drocta blog's convention,
also used by `examples/countdown` and `examples/addition`):

- `1` is `compose(NULL, anything)` — base case: the **left half is
  dead**.
- `n+1` is `compose(alive-sentinel, n)` — successor: alive-left,
  next-number-as-right.

So:

| n | encoding |
|---|---|
| 1 | `compose(NULL, X)` for any `X` |
| 2 | `compose(S, 1)` |
| 3 | `compose(S, 2)` |
| ... | ... |

Building 3 in source:

```ath
import sentinel S;
BIFURCATE [NULL, S] ONE;
BIFURCATE [S, ONE]  TWO;
BIFURCATE [S, TWO]  THREE;
```

Recursing on a number means decomposing it and checking whether the
left half is alive: alive means "successor, recurse on the right";
dead means "base case `n=1`, stop."

Here's the canonical recursive countdown — print `tick` exactly *n*
times for the encoded number `n`:

```ath
print tick;
BIFURCATE ARGS[LEFT, REST];
~ATH(LEFT) {
    COUNTDOWN REST [A, B];           // recurse on n-1
    BIFURCATE NULL[JUNK, LEFT];      // exit the if-then without killing the sentinel
}
THIS.DIE();
```

`print tick;` runs unconditionally — once per call. If `LEFT` is alive
(meaning the input was at least `2`), we recurse on `REST`. We use the
rebind-to-NULL idiom to exit the implicit if-then, because the alive
sentinel that `LEFT` points to is shared across every level of the
encoded number — killing it would break all the levels above.

`examples/addition/add.ath` extends this to arithmetic:

```ath
add(1, b) = b + 1
add(a, b) = add(a-1, b+1)         for a > 1
```

Combined: `examples/addition/main.ath` builds 2 and 3, calls `ADD`,
passes the result to `COUNTDOWN`, and prints `tick` five times.

The encoding is fragile in fresh-composition mode (the default): two
numbers that share an alive sentinel share *that pointer*. Killing the
sentinel anywhere kills it everywhere. Most programs avoid this by
never killing sentinels or shared atoms.

---

## 10b. Objects with real lifetimes

Up to this point, every object in your program has been the same shape:
born alive, dies only when you explicitly kill it. Two extensions break
that pattern.

### 10b.1 The lifetime library

The metadata before the variable in `import` isn't *only* cosmetic
anymore — it can match a name in the runtime's **lifetime library**
(`SPEC.md` §5.3). If it does, the imported object gets a randomly-chosen
lifetime drawn uniformly from the entry's `[min_s, max_s]` range.

```ath
import fly F;          // F dies in 1-3 days
import soap bubble B;  // B dies in 2-30 seconds
import instant I;      // I is born dead (lifetime [0, 0])
import sequoia S;      // S effectively immortal at human scales
```

The library spans the range from "zero lifetime" up to 10^110 seconds
(heat death). Highlights:

- **Born dead**: `instant`. Always.
- **Alive once, then dead**: `once`. Special non-time-based entry —
  `~ATH(V) { ... }` with V from `once` runs the body exactly one time.
  See §10b.3 below.
- **Sub-millisecond**: `muzzle flash`, `tick`. The shortest non-zero
  lifetimes — useful for timing experiments.
- **Low variance / exact**: `second`, `minute`, `hour`, `day`, `week`,
  `year`. Each fires at exactly the named interval.
- **Wildly variable**: `lightning` (0.1 ms to 10 s, five orders of
  magnitude), `campaign` (0 to 100 s), `experiment` (six OOM).
- **Living things**: `mayfly`, `fly`, `mouse`, `dog`, `human`, `sequoia`,
  in roughly biological ranges.
- **Cosmic**: `star` (1-10 billion years), `galaxy`, `black hole`,
  `proton`, `universe`, `forever`.
- **Homestuck flavor**: `author` (~lifespan of a human, 80-100 years),
  `meson` (10-100 ns).

Names not in the library still parse and run — they just produce a
plain alive object as before. So `import x V;` is unchanged.

Concept names are matched in full, including multi-word forms:
`import soap bubble B;` hits the library entry `soap bubble`;
`import dead bubble B;` doesn't (and falls through to plain alive).

For reproducible tests, set the `ATH_SEED` environment variable to a
decimal unsigned integer before running — the runtime seeds its RNG
from it.

You can also extend the library from the command line. `-D NAME:MIN:MAX`
(long form `--define-lifetime`) registers a new entry, repeatable. The
new entry is baked into that binary only:

```bash
athc -D "tortoise:50:150" prog.ath -o prog
```

makes `import tortoise T;` available with a 50-150 second lifetime.
Names with spaces work too: `-D "giant tortoise:50:150"` matches
`import giant tortoise T;`. User entries override built-ins of the same
name — so `-D fly:0:0` turns any program's `import fly F;` into a
born-dead object for that build, useful for testing.

The CLI flag can't register non-time-based entries (the `once`-style
flag); for those, you'd have to extend the runtime.

### 10b.2 The `once` entry — exactly-one execution

`once` is the library's lone non-time-based entry. An object imported
from `once` is alive for exactly one `ath_is_alive` observation; the
runtime flips it to dead atomically with that first observation. The
upshot, since `~ATH(V)` checks the variable before every iteration:

```ath
import once V;
~ATH(V) {
    print exactly one run;
}
print after;
THIS.DIE();
```

The first iteration's check returns alive — the body prints. The second
iteration's check returns dead — the loop exits. No need to kill `V`
yourself; no need to use the rebind-to-NULL idiom. The "run this
exactly once" pattern from §9.1 (`import flag F; ~ATH(F) { ...;
F.DIE(); }`) is now a one-liner: just use `once`.

`once` plays well with the rest of the system: explicit `.DIE()`
before the first observation makes the body never run; explicit
`.DIE()` is also fine after the observation (a no-op since the object
is already dead).

See `examples/once_runner.ath`.

### 10b.3 Watching signals

The `watch` statement has a second form that ties an object's life to a
POSIX signal:

```ath
watch signal SIGUSR1 as RUNNING;
~ATH(RUNNING) {
    print serving requests;
}
print received SIGUSR1, shutting down cleanly;
THIS.DIE();
```

The runtime installs a sticky-flag handler the first time you watch each
signal. As long as the signal hasn't arrived, the watched variable is
alive. When the process receives the signal, the next `ath_is_alive`
check observes the flag and the variable becomes dead — and stays dead
even if more of the same signal arrive later.

Supported names: `SIGHUP`, `SIGINT`, `SIGQUIT`, `SIGUSR1`, `SIGUSR2`,
`SIGPIPE`, `SIGALRM`, `SIGTERM`, `SIGCHLD`. Names outside this set
produce a born-dead object and a one-line stderr warning.

The keyword `signal` here is **contextual** — it's only special as the
second token after `watch`. Programs can still bind variables named
`signal` via `import`.

`examples/signal_handler/main.ath` is a runnable demo: start it in the
background, send SIGUSR1 with `kill -USR1`, watch it exit cleanly.

### 10b.4 Watching files

The `watch` statement ties an object's life to the existence of a file
on disk:

```ath
watch "target.txt" as F;
~ATH(F) {
    print still here;
}
print target is gone;
THIS.DIE();
```

The path resolves at runtime against the current working directory. If
the file is present at allocation, `F` is alive. As soon as the file is
removed (via `rm`, `mv`, anything that makes `access()` fail), the next
`ath_is_alive` check sees `F` as dead and the loop exits.

If the file was already missing when `watch` ran, `F` is born dead and
the loop never runs at all.

Death is one-way: even if the file reappears later, `F` stays dead.

See `examples/file_watcher/main.ath` for a runnable demo (touch the
file, start the program in the background, rm the file, watch it
exit).

This combines well with the library: `watch "lock.pid" as L;` plus
`~ATH(L) { import soap bubble B; ~ATH(B) { ... } }` gives you nested
external-condition loops — fire a body until either the file is removed
or roughly half a minute has elapsed, whichever comes first.

And it combines with signals: `watch signal SIGTERM as T;` plus
`watch "config.txt" as CFG;` plus an `import campaign C;` gives you a
loop that terminates on the first of {SIGTERM received, config file
deleted, random 0-100 second timeout}.

---

## 10c. Real numbers and the arithmetic stdlib

§10's cons-cell encoding is still valid — useful for understanding
the language structure, and for any place an object's *shape* should
carry meaning. For straight numerical work there is a more direct
option: `import number N as V;` binds an object that carries an
int64 payload.

```ath
import number 42 as N;
import number -7 as M;
```

Imported numbers are **eternal** by default — π doesn't decay — so
they stay alive unless you compose them with a mortal carrier or
kill them explicitly. To make a number mortal, give it a deadline by
composing with a carrier from the lifetime library:

```ath
import number 42 as N;
import mayfly M;             // 5 minutes – 1 day, random
BIFURCATE [N, M] MORTAL;     // MORTAL dies when M does
```

The arithmetic ops live in `stdlib/` and are brought in via the
**search-path form** of `importf`, written with angle brackets:

```ath
importf <add>       as ADD;
importf <sub>       as SUB;
importf <mul>       as MUL;
importf <div>       as DIV;
importf <mod>       as MOD;
importf <to_string> as TO_STRING;
importf <parse>     as PARSE;
```

The angle-bracket form walks `ATH_PATH` (colon-separated, like
`PATH`) and then the compiler-adjacent `stdlib/` directory. The
quoted form (`importf "lib/foo.ath" as F;`) still works for
project-local files relative to the importing file.

Each op uses the existing two-argument call form:

```ath
ADD [X, Y] R;        // R.value = X.value + Y.value
SUB [X, Y] R;
MUL [X, Y] R;
DIV [X, Y] R;        // truncating toward zero
MOD [X, Y] R;
TO_STRING [N, _] S;  // S is a string-cons-list of digit chars
PARSE [S, _] N;      // N gets the int64 parsed from S
```

`TO_STRING` and `PARSE` are unary; the second argument is ignored.
Convention is to pass `NULL` (or any in-scope name) as a placeholder.

### 10c.1 Failure is death

If anything goes wrong, the result is **born dead** — alive=0, no
payload. This includes:

- Either operand was already dead at the call site.
- Either operand has no int64 payload (a string head, a generic
  composite, `NULL`).
- Arithmetic overflow (e.g. `INT64_MAX + 1`, `INT64_MIN / -1`).
- Division or modulo by zero.
- `PARSE` on a malformed string ("banana") or one that overflows
  signed 64-bit range.

Born-dead results propagate: an `ADD` whose result feeds another
`ADD` yields a dead result, and so on. A complete arithmetic chain
dies as a unit on any single failure, with no crash and no need for
exception handling.

### 10c.2 Derived values inherit operand death

Results don't only die on overflow — they also die when their
*operands* die later. Each op records both operands as dependencies
of the result; killing either input flips the result to dead on the
next observation:

```ath
import number 10 as X;
import number 20 as Y;
ADD [X, Y] R;
~ATH(R) { print sum is alive; R.DIE(); }    // runs
X.DIE();
~ATH(R) { print still alive; R.DIE(); }     // never runs — R died with X
```

This propagates through chains: `(A+B)+C` dies if any of `A`, `B`,
`C` dies. It's the runtime's way of saying that a computation has
been invalidated by something outside the computation.

### 10c.3 A complete example

`examples/arithmetic/main.ath`:

```ath
importf <parse>     as PARSE;
importf <add>       as ADD;
importf <to_string> as TO_STRING;

INPUT LINE_X;
INPUT LINE_Y;

PARSE [LINE_X, NULL] X;
PARSE [LINE_Y, NULL] Y;
ADD   [X, Y]         SUM;

TO_STRING [SUM, NULL] OUT;
PRINT2 OUT;

THIS.DIE();
```

On `17\n25\n`, it prints `42`. On `banana\n5\n`, the parse fails,
the addition fails, the to_string fails, and the program prints an
empty line — death has propagated end-to-end without crashing.

### 10c.4 Adding your own builtin

The seven ops above are C functions in the runtime, exposed to ~ATH
via `import builtin`. If you want to wrap your own C function or add
a domain-specific op, write a stdlib file like:

```ath
// stdlib/double.ath  (or anywhere on ATH_PATH)
import builtin my_double as MY_DOUBLE;
BIFURCATE ARGS [X, IGNORED];
MY_DOUBLE [X, IGNORED] R;
THIS.DIE(R);
```

…and provide `ath_obj *my_double(ath_obj *, ath_obj *)` to the
linker. The signature is fixed: every builtin takes two `ath_obj *`
and returns one. See `SPEC.md` §4.4.13 for details.

---

## 10d. Comparisons and the verdict idiom

`~ATH` has no `if`. The way you condition on a number's *value*
(rather than its liveness) is to compute a **verdict** — an object
that is alive iff the comparison holds — and run it through `~ATH`:

```ath
importf <lt> as LT;
import number 17 as X;
import number 25 as Y;

LT [X, Y] V;
~ATH(V) {
    print X is less than Y;
    V.DIE();             // kill the verdict so the loop exits
}
```

Three primitives are shipped:

```ath
LT [X, Y] V;     // V alive iff X.value <  Y.value
EQ [X, Y] V;     // V alive iff X.value == Y.value
GT [X, Y] V;     // V alive iff X.value >  Y.value
```

The other three — `<=`, `>=`, `!=` — are expressible without new
primitives. `~ATH(!V)` inversion (§11.2) handles negation, and
swapping operands handles asymmetric flips:

```ath
GT [X, Y] V;   ~ATH(!V) { ... }    // X <= Y  (not greater)
LT [X, Y] V;   ~ATH(!V) { ... }    // X >= Y  (not less)
EQ [X, Y] V;   ~ATH(!V) { ... }    // X != Y  (not equal)
```

Verdicts inherit lifetimes the same way arithmetic results do: a
true verdict dies if either operand dies, so stale comparisons over
killed data are observed as false. False verdicts are born dead from
the start.

A verdict has no payload — there is no boolean value to extract. It
is observed only through `ath_is_alive`, which is to say through
`~ATH` (or through composition with another dep-tracking value).

Comparing two non-numeric objects — strings, generic composites,
`NULL` — always yields a born-dead verdict, since the verdict
machinery only looks at int64 payloads. Object identity comparison
("are X and Y the same object?") is a separate primitive and is
not currently in scope.

`examples/comparison/main.ath` reads two ints from stdin and runs
all three verdicts in sequence; on `3\n10\n` it prints `less`, on
`7\n7\n` it prints `equal`, on `10\n3\n` it prints `greater`.

---

## 10e. Manipulating strings (and any cons-list)

Strings are right-nested cons-lists of character atoms (§6). Four
operations work on them — two as function calls imported from
`stdlib/`, and two as dedicated bracket syntax.

### 10e.1 LENGTH and CONCAT

```ath
importf <length> as LENGTH;
importf <concat> as CONCAT;

INPUT A;
INPUT B;

LENGTH [A, NULL] LA;        // LA carries an int64 payload
CONCAT [A, B]    AB;        // AB is a fresh cons-list
```

- **`LENGTH`** walks the right-spine of its argument until it hits
  `NULL` or a dead cell, returning a number-payload object. The empty
  string (`NULL`) has length 0 — this is the one place where "absent"
  is explicitly *not* "failed."
- **`CONCAT`** copies A's elements then B's into a fresh chain
  terminated with `NULL`. A `NULL` operand is treated as the empty
  string (alive); a non-`NULL` dead operand fails per the usual
  rule. The result inherits both A and B as deps — killing either
  source kills the concatenation.

### 10e.2 Subscript: `S[N] X;`

A real new statement form. `S[N] X;` reads the Nth right-spine head
of `S` into `X`:

```ath
import number 0 as I0;
A[I0] HEAD_ATOM;            // first element of A
```

For strings, `S[N]` returns the Nth character **atom** — a single
object, not a length-1 string. To print it via `PRINT2` you have to
wrap it back into a one-element cons-list:

```ath
BIFURCATE [HEAD_ATOM, NULL] HEAD_STR;
PRINT2 HEAD_STR;            // prints the character followed by '\n'
```

The reason: subscript is deliberately generalized — `S[N]` works on
any right-nested object (lists of numbers, lists of objects, future
SPLIT results), not just strings. Returning a length-1 string would
be string-specific and break that generalization.

The result is born dead if `N` lacks an int64 payload, `N` is
negative, or the walk hits the end of `S` before reaching position
`N`. Out-of-range silently dies; the program doesn't crash.

### 10e.3 Range subscript: `S[I..J] X;`

End-exclusive slice into a fresh cons-list:

```ath
import number 1 as I1;
import number 4 as I4;
A[I1..I4] MID;              // chars 1, 2, 3 (J = 4 is exclusive)
PRINT2 MID;
```

- `[I..J]` is end-exclusive, like Python — `S[0..3]` is the first
  three elements.
- The result is born dead if either endpoint is negative or missing
  a payload, if `I > J`, if `I == J` (empty slice — deliberately
  failure-as-death so silent successes don't look like silent
  empties), or if the walk hits the end of `S` before reaching
  position `J`.
- The result inherits `S`, `I`, and `J` as deps. Killing any of
  them invalidates the slice on the next observation.

### 10e.4 Dispatch between bracket forms

Three statement forms now share the `IDENT '[' ... ']' IDENT ';'`
shape:

| Inside the brackets | Statement                   |
|---------------------|-----------------------------|
| `L, R`              | function call (§8.2)        |
| `N`                 | subscript (§10e.2)          |
| `I..J`              | slice (§10e.3)              |

The compiler dispatches on what follows the first inner identifier:
`,` is a funcall, `..` is a slice, `]` is a subscript. None of them
collide; you don't need to remember a precedence rule.

### 10e.5 A complete example

`examples/strings/main.ath` reads two lines from stdin and exercises
all four ops:

```ath
importf <length>    as LENGTH;
importf <concat>    as CONCAT;
importf <to_string> as TO_STRING;

INPUT A;
INPUT B;

LENGTH [A, NULL] LA;
TO_STRING [LA, NULL] LA_STR;
PRINT2 LA_STR;

import number 0 as I0;
A[I0] HEAD_ATOM;
BIFURCATE [HEAD_ATOM, NULL] HEAD_STR;
PRINT2 HEAD_STR;

import number 1 as I1;
import number 4 as I4;
A[I1..I4] MID;
PRINT2 MID;

CONCAT [A, B] AB;
PRINT2 AB;

THIS.DIE();
```

On `hello\nworld\n` it prints:

```
5
h
ell
helloworld
```

On `\nfoo\n` (empty first line), the LENGTH prints `0`, the
subscript and slice die silently (the empty string has no chars at
position 0 or in `[1..4]`), and CONCAT of empty + `foo` is `foo`:

```
0


foo
```

Death propagating quietly through a chain of operations is the
normal mode — programs read input, build derived values, and the
runtime keeps observing aliveness all the way through.

---

## 11. The Homestuck surface

Three features of the dialect are syntactic concessions to the original
*Homestuck* presentation of `~ATH`:

### 11.1 Multi-word `import`

The original comic has `import dead grandmother G;` — a "concept" with a
multi-word name. The compiler accepts any number of identifiers after
`import`, joining all but the last as the (purely cosmetic) name:

```ath
import dead grandmother G;       // name = "dead grandmother", var = G
import flavor A;                  // name = "flavor", var = A — also fine
```

At least two identifiers are required (one for the name, one for the
variable). `import G;` alone is rejected.

### 11.2 `!V` inversion

You can invert an `~ATH` condition with `!`:

```ath
~ATH(!V) {
    // runs while V is dead
}
```

Since objects can't come back to life, `~ATH(!V) { ... }` either skips
entirely (V alive at entry) or runs once-and-must-rebind-V-to-exit
(V dead at entry). See `examples/inverted_loop.ath`.

### 11.3 `EXECUTE(NULL);` postfix

The original surface attached an `EXECUTE(...)` clause to every `~ATH`
loop, naming what happens when the watched concept dies. The compiler
parses this for compatibility but treats the argument as a no-op (for
now — future versions may interpret it). Both forms are legal:

```ath
~ATH(V) { ... }                              // drocta-style
~ATH(V) { ... } EXECUTE(NULL);               // Homestuck-style
```

Putting all three together — `examples/homestuck_canonical.ath`:

```ath
import dead universe U;
U.DIE();
~ATH(!U) {
    print the universe has ended.;
    BIFURCATE [NULL, NULL] U;
} EXECUTE(NULL);
print and yet, the program continues.;
THIS.DIE();
```

---

## 12. What's deliberately missing

To save time hunting for features that aren't there:

- **No floats or booleans.** Numbers exist (§10c) but only as int64.
  Verdicts (§10d) carry no truth value — they are alive or dead, not
  `true` or `false`.
- **No conditionals.** No `if`, `?:`, `switch`. The only branching is
  `~ATH(V) { ... }`, which is a loop. Combined with comparison
  verdicts (§10d) it gives you an if-then idiom (§9.1).
- **No infix operators.** No `+`, `*`, `==`, `&&`. Arithmetic and
  comparison are function calls — `ADD [X, Y] R;`, `LT [X, Y] V;` —
  imported from `stdlib/` (§10c, §10d). There is `!` but it only
  prefixes a variable in an `~ATH` condition.
- **No early return or `break`.** Exit a function by killing its
  `THIS`; exit a loop by making its variable dead.
- **No expressions.** Every statement is structural manipulation.
  There is no "computed value" you can save except via variable
  bindings.
- **No string escapes, no string literals outside `print`.** Strings
  are cons-lists built up by composing character atoms (§6).
- **No top-level `ARGS`.** Only function bodies see `ARGS`.

Reasonable things you *might* expect but won't find:

- Cross-compilation-unit linking. Everything is one program plus its
  `importf`'d files (and the runtime), resolved at compile time.
- Substring search (`FIND`), in-place rewriting (`REPLACE`), and
  list-of-string operations (`SPLIT`, `JOIN`). These are planned for
  a later string-ops tier; current support is length, concat,
  subscript, slice (§10e).
- Re-running a dead object (it really is permanent).

---

## 13. The full file-watch + lifetime combination

For a final motivating example: a program that does work until either
its config file vanishes or 10 seconds elapse, whichever comes first.

```ath
watch "config.txt" as CFG;
import campaign T;            // 0-100 seconds, uniformly random

~ATH(CFG) {
    ~ATH(T) {
        print serving requests;
        // ... do work ...
    }
    print campaign over;
    CFG.DIE();                 // exit outer loop too
}
print shutting down;
THIS.DIE();
```

The outer loop is alive while the config file exists. The inner loop is
alive while the random `campaign` lifetime hasn't expired. The first to
fail terminates the corresponding loop. This is `~ATH` in its
Homestuck-original spirit: programs are infinite loops tied to the
lifespans of *real things* — a real file, a real (probabilistic)
duration — that the runtime observes from the outside.

---

## 14. Where to go from here

- **`SPEC.md`** — precise grammar, semantics, runtime ABI. Read this when
  the tutorial says something that surprises you and you want to know if
  the surprise is real or my fault.
- **`examples/`** — every program in this directory compiles and runs.
  `make test-runtime && .venv/bin/pytest tests/test_conformance.py -v`
  exercises them all.
- **The drocta interpreter** at
  <https://github.com/drocta/TILDE-ATH> — the reference Python
  implementation of the variant this compiler grew out of. Programs
  there mostly compile here with minor adjustments.
- **The Homestuck esolang wiki page** at <https://esolangs.org/wiki/~ATH>
  — for the original surface and a few impractical-on-purpose programs.

When in doubt, write the smallest possible program that exercises what
you're confused about and run it. The compiler is fast and the runtime
is null-safe — almost any sema-passing program is observable rather
than crashing, so experimentation is cheap.

Welcome to `~ATH`. Try not to kill `THIS` before you're ready.
