# athc — a compiler for `~ATH`

`~ATH` ("Till Death") is an esolang built around one feature, an 
object is alive or dead, and control flow is based on the liveness of objects. a `~ATH(V)` loop
runs while `V` is alive and the program ends when it kills `THIS`, the object
standing for the program itself

**athc** lexes, parses, and lowers `~ATH` to native executables through
LLVM over a small C runtime

```ath
PRINT Hello, ~ATH!;
THIS.DIE();
```

```
$ python -m athc.cli examples/basics/hello.ath -o hello
$ ./hello
Hello, ~ATH!
```

count, then die:

```ath
IMPORTF <add> AS ADD;
IMPORTF <lt> AS LT;

IMPORT NUMBER 0 AS I;
IMPORT NUMBER 5 AS N;
IMPORT NUMBER 1 AS ONE;

LT [I, N]RUNNING;
~ATH(RUNNING)
{
    PRINT counting $I;
    ADD [I, ONE]I;
    LT [I, N]RUNNING;
}
THIS.DIE();
```

`RUNNING` is alive while `I < N`; the loop re-reads it each pass

## Requirements

- python ≥ 3.10 with [`llvmlite`](https://pypi.org/project/llvmlite/) ≥ 0.42
- a C compiler — `gcc` by default (`--cc` / `$CC` to override)
- `make` and a POSIX C toolchain
- [`pipx`](https://pipx.pypa.io/) to install the `athc` command

## Install

`pipx install` builds the C runtime and bundles it, putting `athc` on your
`PATH` so it runs from any directory:

```sh
# install pipx if you don't have it
sudo apt install pipx        # or: python3 -m pip install --user pipx
python3 -m pipx ensurepath   # add pipx's bin dir to PATH (restart shell after)

pipx install .               # from a repo checkout

athc examples/basics/hello.ath -o hello && ./hello
athc --repl                  # interactive
```

`make` and a C compiler must be present at install time — the runtime is
compiled and packaged into the install.

## Quickstart (development)

```sh
pip install -e ".[dev]"   # the Python compiler (use a virtualenv)
make runtime              # the C runtime (both compose modes)

python -m athc.cli examples/basics/hello.ath -o hello && ./hello
python -m athc.cli --repl # interactive
```

the REPL drives the real runtime through `ctypes`, so behavior matches a
compiled program. use `:inspect`, `:env`, and `:compose` to poke at
objects

### Flags

| Flag | Effect |
|------|--------|
| `--emit-ir` | print LLVM IR and stop (no link) |
| `--emit-obj prog.o` | stop after codegen, write the object file |
| `--compose fresh\|intern` | composition discipline (default `fresh`) |
| `-D 'name:min:max'` | register an extra lifetime-library entry |
| `--runtime PATH` | link a specific runtime archive |
| `--cc COMPILER` | C compiler for the link step |

## Layout

two halves over a fixed C ABI (`runtime/ath_runtime.h`):

- **`athc/`** — the Python compiler, emitting LLVM IR linked with `gcc`
- **`runtime/`** — the C runtime: allocation, liveness, file I/O, concurrency, networking
- **`stdlib/`** — one `~ATH` shim per builtin, imported via `IMPORTF <name>`

## Composition modes

`BIFURCATE [L, R] V` composes two objects into one. selected at link time:

- **`fresh`** (default) — every compose allocates
- **`intern`** (`--compose intern`) — composes are hash-consed by `(left,
  right)`, structurally equal composites share storage and die together

a well-behaved program produces identical output under both

## Documentation

- **[`TUTORIAL.md`](TUTORIAL.md)** - start here!
- **[`SPEC.md`](SPEC.md)** - the technical language reference

## Examples

[`examples/`](examples/) has runnable programs for every feature (also used to test everything). highlights:
- [Brainfuck interpreter](examples/programs/brainfuck.ath)
- [maze generator + solver](examples/programs/maze.ath)
- [Sudoku solver](examples/programs/sudoku.ath)
- [expression calculator](examples/programs/calculator.ath)
- [TCP chat server](examples/net/chat_tcp/)
- [actor ping-pong](examples/actors/ping_pong/)

## Testing

```sh
make test-runtime    # C runtime units, both compose modes
pytest               # lexer/parser/sema/diagnostics + conformance
```

`tests/test_conformance.py` compiles each example and checks its output under
both `fresh` and `intern`

## Editor Syntax
Vim/Neovim syntax highlighting: [`editors/vim/`](editors/vim/) 

(if you use something besides vim and make one let us know and we'll add it)
