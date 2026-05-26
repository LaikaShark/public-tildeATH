# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

`athc` is a compiler for **~ATH**, the esoteric language from Homestuck. The implementable dialect is roughly drocta's; the surface syntax adds Homestuck flourishes (`~ATH(!V)` inversion, `EXECUTE(...)` postfix, multi-word `import`). The authoritative reference is `SPEC.md` (language) and `TUTORIAL.md` (user-facing). When changing language behavior, update both.

## Common commands

```sh
# Build the C runtime (two flavors, see Architecture).
make runtime          # builds runtime/libath_fresh.a and libath_intern.a
make test-runtime     # runs runtime/test_runtime under both modes
make clean

# Python tests (lexer/parser/sema/diagnostics + end-to-end + conformance).
pytest                            # full suite
pytest tests/test_parser.py       # one file
pytest tests/test_conformance.py -k hello   # one case

# Compile a .ath program to a native binary.
python -m athc.cli examples/hello.ath -o hello
./hello

# Interactive REPL (drives the real runtime via ctypes against libath_*.so).
python -m athc.cli --repl

# Useful flags:
python -m athc.cli prog.ath --emit-ir              # print LLVM IR, no link
python -m athc.cli prog.ath --emit-obj prog.o      # stop after codegen
python -m athc.cli prog.ath --compose intern       # hash-cons ath_compose
python -m athc.cli prog.ath -D 'mayfly:0.1:1.0'    # add a lifetime library entry
python -m athc.cli prog.ath --runtime path/to.a --cc clang
```

The conformance suite shells out to `python -m athc.cli` and runs the resulting binaries, so it depends on the runtime archives — it will invoke `make runtime` for you if the `.a` is missing, but a stale runtime won't be rebuilt automatically. `make clean && make runtime` after C changes.

## Architecture

Two halves talk through a fixed C ABI (`runtime/ath_runtime.h`):

**Python compiler (`athc/`)** is a straightforward `lexer → parser → sema → codegen` pipeline:
- `lexer.py` / `parser.py` — recursive-descent; keywords and function names are case-insensitive, variable identifiers are case-sensitive. The `signal` token in `watch signal SIGUSR1 as V` is a **contextual** keyword (still an `IDENT`), not a reserved word.
- `ast.py` — every node carries `line`/`col`; `Program` carries `source_path` so errors can be tagged with the originating file.
- `loader.py` — resolves `importf` recursively. Returns `(main_program, function_table, source_registry)`. The registry maps resolved `Path` → source text and is what `cli.py` consults to render diagnostics for any file in the import graph.
- `sema.py` — name resolution, arity, scoping. `SemaError` carries `path` so multi-file programs report the right file.
- `diagnostics.py` — `render_diagnostic(...)` formats `path:line:col: error: msg` plus a caret-pointed source snippet. All user-visible errors in `cli.py` go through it; don't `print(e)` directly.
- `codegen.py` — emits LLVM IR via `llvmlite`. `Codegen` holds module-wide state; `FunctionEmitter` emits per-function bodies. User functions are forward-declared before any body is emitted so recursion and mutual recursion work. `--define-lifetime` entries are registered in main's prologue via `ath_register_lifetime`.

**C runtime (`runtime/`)** is split so the two composition disciplines share everything else:
- `runtime_common.c` — `ath_alloc_*`, `ath_decompose`, `ath_die`, `ath_is_alive`, I/O, lifetime library, file watching (`access(F_OK)`), POSIX signal handling (`sigaction` + a sticky `volatile sig_atomic_t` array indexed by signum).
- `compose_fresh.c` — `ath_compose` always allocates.
- `compose_intern.c` — `ath_compose` hash-conses by raw `(left, right)` pointer pair into a 4096-slot chained hash table, so structurally equal composites share storage and die together.
- `libath_fresh.a` = `runtime_common.o + compose_fresh.o`; `libath_intern.a` = `runtime_common.o + compose_intern.o`. The CLI's `--compose` flag picks the archive.
- `test_runtime.c` is built twice; `-DATH_INTERN_MODE` gates assertions that only hold in intern mode (e.g. `compose(a,b) == compose(a,b)`).

**Null-safety contract** (documented in `ath_runtime.h`): every runtime entry point treats a C null pointer as `ath_NULL`. The codegen relies on this — unbound variable slots are zero-initialized, and reading them must not crash.

**Lifetime model** (`SPEC.md §4.7`, `runtime_common.c`): `ath_obj` carries optional `deadline_s`, `watch_path`, `is_oneshot`, and `awaiting_signal` fields. `ath_is_alive` checks them in order and flips `alive=0` on the first failing condition (deadline passed, file gone, oneshot consumed, signal received). The `once` library entry is the `is_oneshot` path. The Python-side library registry (built-ins + `--define-lifetime` entries) is mirrored into the runtime by `ath_register_lifetime` calls in main's prologue.

## Conventions specific to this repo

- Commit messages: one-line subject only, no body, no AI coauthorship trailers.
- New language features land in `SPEC.md` and `TUTORIAL.md` alongside the implementation. The spec is the source of truth — if the implementation disagrees with it, update one of them deliberately.
- The conformance suite (`tests/test_conformance.py`) is parametrized over both `--compose` modes. Any new example should produce identical output in both, since composition discipline is not supposed to be observable from a well-behaved program.
- The two known idioms used throughout the examples: `BIFURCATE NULL[J,V];` rebinds `V` to a dead object (to break out of a `~ATH(V)` loop), and `BIFURCATE [NULL,NULL]V;` rebinds `V` to a fresh live object (to escape an inverted `~ATH(!V)` loop).
