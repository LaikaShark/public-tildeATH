# athc — a compiler for `~ATH`

`~ATH` ("until they hang" / "infinite loop") is the esoteric programming
language from Andrew Hussie's *Homestuck*. **athc** is a real, working
compiler for an implementable dialect of it: it lexes, parses, and lowers
`~ATH` source to native executables through [LLVM](https://llvm.org/), backed
by a small C runtime.

The language is built on a single idea — the **object**, which is either alive
or dead — and a handful of statements that allocate objects, split and rejoin
them, ask whether they are still alive, and kill them. Control flow is
*liveness*: a `~ATH(V)` loop runs as long as the object `V` is alive, and a
program ends when it kills `THIS`, the object standing for the program itself.

```ath
PRINT Hello, ~ATH!;
THIS.DIE();
```

```
$ python -m athc.cli examples/hello.ath -o hello
$ ./hello
Hello, ~ATH!
```

A loop that counts, then lets the program die:

```ath
IMPORTF <add> AS ADD;
IMPORTF <lt> AS LT;

IMPORT NUMBER 0 AS I;
IMPORT NUMBER 5 AS N;
IMPORT NUMBER 1 AS ONE;

LT[I, N] RUNNING;
~ATH(RUNNING)
{
    PRINT counting $I;
    ADD[I, ONE] I;
    LT[I, N] RUNNING;
}
THIS.DIE();
```

The verdict object `RUNNING` is alive exactly while `I < N`; the loop re-reads
it each pass and exits the moment it dies. There are no expressions, no infix
operators, and no runtime errors — every program either finishes or runs
forever.

## Requirements

- **Python ≥ 3.10** with [`llvmlite`](https://pypi.org/project/llvmlite/) (`>= 0.42`)
- A C compiler — **`clang`** by default (override with `--cc`)
- **`make`** and a POSIX C toolchain to build the runtime

## Quickstart

```sh
# 1. install the Python compiler (a virtualenv is recommended)
pip install -e ".[dev]"

# 2. build the C runtime (two flavors — see "Composition modes")
make runtime

# 3. compile and run a program
python -m athc.cli examples/hello.ath -o hello
./hello

# 4. or explore interactively in the REPL
python -m athc.cli --repl
```

The REPL drives the *real* runtime through `ctypes`, so behavior matches a
compiled program exactly; bindings persist across lines and meta-commands like
`:inspect`, `:env`, and `:compose` help you poke at objects.

### Useful flags

| Flag | Effect |
|------|--------|
| `--emit-ir` | print the LLVM IR and stop (no link) |
| `--emit-obj prog.o` | stop after codegen, write the object file |
| `--compose fresh\|intern` | pick the composition discipline (default `fresh`) |
| `-D 'name:min:max'` | register an extra lifetime-library entry for this build |
| `--runtime PATH` | link against a specific runtime archive |
| `--cc COMPILER` | C compiler for the link step |

## How it works

Two halves talk through a fixed C ABI (`runtime/ath_runtime.h`):

- **`athc/`** — the Python compiler: a straightforward
  `lexer → parser → loader → sema → codegen` pipeline that emits LLVM IR via
  `llvmlite`, then links it against the runtime with `clang`. Diagnostics carry
  carets and "did-you-mean" suggestions.
- **`runtime/`** — the C runtime: object allocation and the liveness model,
  the numeric tower (int64 → arbitrary-precision bignum → IEEE-754 double),
  strings as cons-lists of character atoms, lifetime extensions (deadlines,
  file/pid/signal/mtime watches), and file I/O.
- **`stdlib/`** — ~ATH shim files (one per builtin) that expose the runtime's C
  functions as ordinary callable `~ATH` functions, brought in with
  `IMPORTF <name>`.

## Composition modes

`BIFURCATE [L, R] V` composes two objects into one. The runtime ships two
implementations, selected at link time:

- **`fresh`** (default) — every compose allocates a new object.
- **`intern`** (`--compose intern`) — composes are hash-consed by `(left,
  right)`, so structurally equal composites share storage and die together.

A well-behaved program produces identical output under both; the conformance
suite runs every example both ways and requires byte-for-byte agreement.

## Documentation

- **[`TUTORIAL.md`](TUTORIAL.md)** — a guided introduction, start here.
- **[`SPEC.md`](SPEC.md)** — the authoritative language reference.
- **[`STYLE.md`](STYLE.md)** — the house code style used by the examples.

## Examples

[`examples/`](examples/) holds runnable programs for every feature: arithmetic,
strings, lists, comparisons, lifetimes and watches, file I/O, and the REPL.
Highlights include a [Brainfuck interpreter](examples/brainfuck.ath), a
[maze generator + solver](examples/maze.ath), a
[Sudoku solver](examples/sudoku.ath), and an
[expression calculator](examples/calculator.ath). Multi-file programs (e.g.
[`fizzbuzz/`](examples/fizzbuzz), built from mutually-recursive functions) live
in their own subdirectories.

## Testing

```sh
make test-runtime    # C runtime unit tests, under both compose modes
pytest               # lexer / parser / sema / diagnostics + end-to-end conformance
```

The conformance suite (`tests/test_conformance.py`) compiles each example and
checks its output under both `fresh` and `intern`.

## Editor support

Vim/Neovim syntax highlighting is in [`editors/vim/`](editors/vim/) — see its
README for install steps.

## License

TODO — choose and add a `LICENSE` file before publishing. `~ATH` originates
from *Homestuck* by Andrew Hussie; this project is an independent
implementation of the language idea.
