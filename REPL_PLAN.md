# Plan: ~ATH REPL

Planning doc — **not** the spec. Trim once shipped. Task #14 (ergonomics, last
backlog item).

## Locked decision (2026-06-06)

Drive the **real C runtime via ctypes**. ~ATH compiles to native code with no
interpreter; rather than write a second semantics, the REPL loads the runtime as
a shared library and evaluates each statement by calling the same ABI functions
the LLVM codegen emits — just live, from Python. Fidelity is exact (it *is* the
runtime), state persists across lines (objects live in the runtime heap, which
never frees), and all ~70 builtins come for free. Rejected: recompile-per-line
(slow, side effects replay, no introspection) and a pure-Python interpreter
(large, drifts from the C runtime).

## 1. Shared-library build

The runtime is built only as static archives today. Add shared objects:

- Add `-fPIC` to `CFLAGS` (PIC objects still link fine into the existing
  executables/archives, so no second object set is needed).
- New Makefile targets `runtime/libath_fresh.so` and `libath_intern.so`:
  `$(CC) -shared <objs> -lm -o $@`. Add to `make runtime`.
- `make clean` removes the `.so`s.

## 2. ctypes FFI bindings — `athc/runtime_ffi.py`

`AthRuntime(compose="fresh"|"intern")` wraps `ctypes.CDLL(<.so>)`:

- **CRITICAL:** set `restype = c_void_p` on every function returning `ath_obj *`.
  ctypes defaults to `c_int`, which truncates 64-bit pointers — the #1 footgun.
- `ath_obj *` ↔ `c_void_p`; out-params (`ath_decompose(v, &l, &r)`) via
  `byref(c_void_p())`; the dead singleton via `c_void_p.in_dll(lib, "ath_NULL")`.
- Declare the ABI surface the evaluator needs: `ath_alloc_alive`,
  `ath_alloc_number/float/bignum_from_decimal`, `ath_compose`, `ath_decompose`,
  `ath_die`, `ath_is_alive`, `ath_print_bytes/print_obj_raw`, `ath_input_line`,
  `ath_string_from_bytes`, `ath_coerce_string`, `ath_to_string`, `ath_clone`,
  `ath_index/slice`, `ath_count_of`, `ath_alloc_from_library`,
  `ath_register_lifetime`, `ath_alloc_watching_*`, `ath_sleep_ms`,
  `ath_alloc_timer_ms`, `ath_alloc_read_file/write_file/append_file/close`, and
  every arithmetic/string/list/transcendental builtin (all `ath_obj*(ath_obj*,
  ath_obj*)` — one generic signature covers the lot).
- A couple of Python-side readers built on the ABI: `to_text(obj)` →
  `ath_to_string` then walk to a `str` (or use a small `ath_obj`-to-bytes
  helper); `is_alive(obj)`.
- **stdout buffering:** the runtime writes to the process stdout; flush after
  each evaluated line (or `setvbuf` unbuffered) so prints and prompts interleave
  correctly.

## 3. Statement evaluator — `athc/repl_eval.py`

An `Evaluator` holding `env: dict[str, c_void_p]` (var → `ath_obj*`), a function
registry (name → C builtin symbol *or* a user `Program`), and the registered
lifetime library. It mirrors codegen's `_emit_stmt` 24-way dispatch, calling the
FFI instead of emitting IR:

| Statement | Action via FFI |
|---|---|
| `import NAME… VAR` | lifetime-library lookup → `ath_alloc_from_library` / `ath_alloc_alive`; idempotent if bound |
| `import number N` | `ath_alloc_number` / `_float` / `_bignum_from_decimal` |
| `import builtin SYM as NAME` / `importf … as NAME` | register NAME → C symbol / loaded user `Program` (compile-time; loader resolves importf) |
| `BIFURCATE` (de/compose) | `ath_decompose` / `ath_compose` |
| `FN [L,R] V` / `FN A [L,R]` | builtin → call its C symbol; user fn → run its `Program` body in a fresh sub-env (THIS, NULL, ARGS=compose(L,R)) recursively, bind the return |
| `~ATH(V){…}` / `~ATH(!V)` | `while is_alive(env[V]) (xor inverted): run body`; run `EXECUTE(F)` on condition-exit |
| `loop N{…}` / `every N{…}` | `ath_count_of` countdown / interval loop (see §6 caps) |
| `V.DIE()` / `V.DIE(RET)` | `ath_die`; `THIS.DIE` raises a `_ThisDied(ret)` to unwind the current body |
| `print …` | literal parts → `ath_print_bytes`; `$VAR` → `ath_print_obj_raw`; one trailing newline |
| `INPUT VAR` | read a line from the REPL's input, `ath_string_from_bytes` (see §6) |
| `S[N]` / `S[I..J]` | `ath_index` / `ath_slice` |
| `BRANCH(V){…}[ELSE]{…}` | one-shot: pick branch by liveness, run it, then consume V |
| `CLONE V as W` | `ath_clone` |
| `text PART+ as V` | `ath_string_from_bytes` + `ath_coerce_string` folded with `ath_concat` |
| `watch …` | `ath_alloc_watching_file/signal/pid/mtime` |
| `sleep` / `TIMER` | `ath_sleep_ms` / `ath_alloc_timer_ms` |
| `read/write/append/close` | the file ABI |

- **Control flow via exceptions:** `_ThisDied(ret)` unwinds a function body (or a
  top-level line). Top-level `THIS.DIE()` ends the *current line*, not the
  session (§6).
- **Predefined names** per scope: `THIS` (fresh `ath_alloc_alive` per
  body/session), `NULL` (the singleton), `ARGS` (only inside a user fn).
- Lifetime library registered once at startup (the same entries codegen emits
  via `ath_register_lifetime`, incl. any `-D` from flags).

User functions reuse the loader (`importf` resolution) and recurse through the
same evaluator — no duplicated logic.

## 4. REPL loop + UX — `athc/repl.py` (`python -m athc.repl`, and `athc … --repl`)

- Prompt `~ATH> `; continuation `  ... ` for an unfinished block.
- **Multiline:** a line opening `~ATH(…){` / `BRANCH(…){` / `loop`/`every {`
  without its closing `}` keeps reading (detect via a parse attempt that hits
  "unexpected end of input"). A trailing `;` is required by the grammar; offer a
  lenient mode that appends one if missing (configurable; default lenient).
- **Meta-commands** (`:` prefix, never valid ~ATH):
  - `:inspect VAR` — liveness + kind + `to_string` value (e.g. `live · int · 10`).
  - `:env` — list bound variables and a one-word state each.
  - `:load FILE` — evaluate a file's statements into the session.
  - `:reset` — fresh environment. `:compose fresh|intern` — reload the other lib.
  - `:help`, `:quit` (also Ctrl-D).
- **Errors:** parse/sema errors on the entered line render through
  `render_diagnostic` (now with humanized tokens + suggestions from #13). There
  are no runtime errors — a failed op just yields a dead object, visible via
  `:inspect`.
- Wire `--repl` into `cli.py` (no source file → REPL; or an explicit flag).

## 5. Testing — `tests/test_repl.py`

- Drive the evaluator/REPL programmatically (feed a list of lines, capture
  stdout), assert outputs.
- **Differential test (the strong one):** for a set of snippets, assert the REPL's
  stdout equals the compiled-binary stdout (reuse the conformance compile-run
  helper). Since both use the same runtime, they must match — this guards the
  evaluator against codegen.
- Unit-test the FFI (`ath_alloc_number(5)` → `to_text` == "5"; `ath_NULL` not
  alive; a bignum round-trips).
- Cover meta-commands and multiline blocks.

## 6. Scope / limitations to pin

- **`every N {}`** is an infinite loop. In the REPL, cap it (e.g. run N
  iterations or until Ctrl-C / a `:` interrupt) and `log` that it was bounded —
  never hang the session. `loop N {}` is finite; run as-is (with a sanity cap +
  notice for absurd counts, since `count_of` can be `INT64_MAX` for a bignum).
- **`INPUT`** competes with the REPL's own stdin; route it through the REPL's
  reader rather than the raw `ath_input_line` (which would consume the next
  prompt's line).
- **`THIS.DIE()`** at top level ends the current *line*, not the session (use
  `:quit` to exit). Document this divergence from a compiled program.
- **Time/file/signal features work for real** (real clock, real fs) — a genuine
  fidelity win of the ctypes approach; no stubbing.

## 7. Phasing

- **R1 — FFI:** `.so` build + `runtime_ffi.py` + a smoke test (alloc/number/
  to_string/NULL). Shippable foundation.
- **R2 — core evaluator:** env + import/number, compose/decompose, print(+interp),
  funcall (builtin + user fn), DIE, `~ATH` loop, BRANCH, subscript/slice, clone,
  text. Covers the common pure subset.
- **R3 — REPL loop + UX:** prompt, multiline, meta-commands, error rendering,
  INPUT handling, `--repl` wiring.
- **R4 — remainder + docs + tests:** watch/sleep/timer/file ops, `loop`/`every`
  caps, differential tests, and a short TUTORIAL "Using the REPL" section.

Each phase keeps `pytest` green. Mostly Python; the only C-side change is the
`.so` Makefile targets (+ `-fPIC`).

## 8. Deferred

- Readline niceties (history file, tab-completion of var/function names) — nice,
  additive later.
- A `.so` is needed; if a platform can't build one, fall back to "compile a tiny
  throwaway program" — out of scope unless it bites.
