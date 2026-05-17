# Scoping: #5 Float payloads (roadmap, phase 1 of float/bignum)

Planning doc — **not** the spec. Once a phase lands, fold its decisions into
`SPEC.md` §4.8 and `TUTORIAL.md`, then trim that phase out of here.

## Locked decisions (2026-06-02)

1. **Scope:** IEEE-754 `double` payloads **first**; bignum deferred to a later
   phase. The representation chosen here reserves a slot for bignum so phase 2
   does not re-break the ABI.
2. **Coercion:** **numeric tower / promotion.** A binary op with one int and
   one float operand promotes the int to float and yields a float. int⊕int
   stays int.
3. **Representation:** **tagged kind + union.** Replace the lone `value` slot
   with a discriminant and a union; `has_value` collapses into the tag.

## 1. Representation change

Current (`runtime/ath_runtime.h`):
```c
int     has_value;   /* nonzero iff value set */
int64_t value;       /* signed 64-bit         */
```

Target:
```c
typedef enum { ATH_NUM_NONE = 0, ATH_NUM_INT, ATH_NUM_FLOAT,
               ATH_NUM_BIG /* reserved, phase 2 */ } ath_num_kind;

ath_num_kind num_kind;               /* NONE = no payload (was has_value==0) */
union { int64_t i; double f; /* ath_big *b; */ } num;
```

- **`has_value` → `num_kind != ATH_NUM_NONE`.** Provide a static inline
  `ath_has_value(o)` so the ~97 call sites migrate mechanically.
- **Field order / ABI:** the payload lives *after* the `{alive,left,right}`
  codegen-modeled prefix (§5.1), so codegen is unaffected by the union swap.
  The only codegen-visible surface is the ABI entry points (§4 below).
- `ath_clone` already copies the whole struct via `calloc`+field copy — extend
  to copy `num_kind`+`num` (one union word). Verify the §4.6 char-atom snapshot
  path still works (chars set `is_char`, not a numeric kind).

## 2. Promotion rules (the tower)

A helper pair drives every binary numeric op:
```c
/* Are both operands alive + numeric? Out-params give each operand's kind. */
static int ath_num_operands(ath_obj *x, ath_obj *y, ...);
/* Promote: if either is FLOAT, read both as double; else both as int64. */
```
- int ⊕ int  → int64 path (existing overflow-checked logic, unchanged result).
- int ⊕ float, float ⊕ int, float ⊕ float → double path; result is FLOAT.
- A `NONE`/dead operand → born dead (existing `ath_operands_usable` rule,
  generalized to the tag).
- **Float results never "overflow"** to dead — they go to `±inf`/`nan` per IEEE.
  Decision to pin in spec: is `nan`/`inf` a live value or born-dead? Recommend
  **live** (failure-as-death is for *operations that cannot produce a number*;
  inf/nan are numbers). Revisit in the per-op table below for div-by-zero.

## 3. Builtin classification

| Group | Builtins | Float behavior |
|---|---|---|
| Tower arith | add, sub, mul, neg, abs, min, max, sign, clamp, pow | promote; float result when any float operand |
| Division | div | int⊕int = trunc-toward-zero (today); any float = true division; **float `/0` → ±inf/nan (live), int `/0` → dead (today)** |
| Modulo | mod | int path today; float path via `fmod` (or born-die — pick in spec) |
| Comparisons | eq, ne, lt, gt, le, ge, compare | promote, compare numerically; **eq across kinds**: `2 == 2.0` true under promotion |
| Int-only (bitwise/gcd) | band, bor, bxor, bnot, shl, shr, gcd | **born-die on any FLOAT operand** (no bit pattern semantics exposed) |
| List folds | sum, product, maximum, minimum | promote element-wise; float if any element float; empty-list identity stays int `0`/`1` |
| Codec | to_string, parse | to_string: `%g`-style canonical float (round-trippable, no trailing zeros); parse: detect `.`/`e` → FLOAT else INT |

New builtins to add (small shims): `int_to_float`, `float_to_int` (trunc),
`floor`, `ceil`, `round`, maybe `sqrt`. Phase-1 minimal: the two conversions +
`floor`/`ceil`/`round`; defer transcendentals unless asked.

## 4. ABI / entry-point changes

- **New:** `ath_obj *ath_alloc_float(double v);` (mirrors `ath_alloc_number`).
- `ath_count_of` (loop/every counts): a FLOAT payload must **floor toward an
  int64 count** (or born-0?). Recommend `floor`, clamp ≥0, matching int rule.
- `ath_char_atom((int)n->value)` in `ath_chr` / index-of-char: float index →
  truncate or born-die. Recommend **born-die** (char codes are integral).
- `sleep`/`timer`/`now`/`random_range` read payloads as int64 ms. A float there
  → truncate to int64 ms (lossy but harmless) or born-die. Recommend truncate.
- `ath_alloc_dead_number()` → generalize to clear the tag (`NONE`).

## 5. Frontend (lexer / parser / codegen)

- **Lexer:** new `FLOAT` token. Scan digits; on `.` **followed by a digit**,
  continue as float (then optional `eE[+-]?digits`). `.` followed by `.` is
  still `DOTDOT` (slice), `.` followed by non-digit is still `.DIE`. So
  `N[1..3]` slices, `3.14` is a float, `3.die` stays a method — all
  disambiguated by one char of lookahead. Add `1e10` (no dot) as float too.
  Bound check: reject non-finite literals; floats don't have the int64 range
  check.
- **Parser/AST:** `import number <FLOAT|INT> as VAR;` — extend `import-number`
  to accept either literal; tag the `ImportNumberStmt` with kind. (Keeps surface
  minimal vs. adding an `import float` keyword. Grammar §3 + §4.4.14 update.)
- **Codegen:** `_emit_import_number` branches on literal kind → `f_alloc_number`
  (i64 const) or new `f_alloc_float` (double const). Everything else flows
  through unchanged ABI calls.

## 6. Invariants to preserve (regression risks)

- **"No runtime crash" (§6.2):** float ops must not trap. nan/inf are fine;
  no FP exceptions enabled. div-by-zero on floats yields inf/nan, not SIGFPE.
- **Both compose modes identical output (CLAUDE.md):** payloads are
  composition-independent; verify nonetheless in conformance.
- **Char atoms unaffected:** `is_char`/`char_code` are a separate axis from
  `num_kind`; a char atom has `num_kind == NONE`.
- **Determinism of `to_string`:** pick one float format and freeze it (the
  conformance suite diffs exact stdout). `%g` with enough precision for
  round-trip, or `%.17g` trimmed — decide and lock.

## 7. Touchpoint inventory

| Area | File | Scale |
|---|---|---|
| Struct + ABI decls | `runtime/ath_runtime.h` | union swap + 1 new decl |
| Numeric core | `runtime/runtime_common.c` | ~82 `->value` sites; ~13 core arith/cmp fns rewritten to dispatch; folds; codec |
| has_value migration | `runtime/runtime_common.c` | ~97 sites → `ath_has_value()` inline |
| Lexer | `athc/lexer.py` | FLOAT token + scan |
| Parser/AST | `athc/parser.py`, `athc/ast.py` | import-number kind |
| Codegen | `athc/codegen.py` | alloc_float fn + import-number branch |
| Stdlib | `stdlib/*.ath` | mostly unchanged (call same builtins); add ~5 new shims |
| Spec/Tutorial | `SPEC.md` §4.8, §3, §2.2; `TUTORIAL.md` | new tables, literal grammar |
| Tests | `tests/`, `runtime/test_runtime.c` | float arith, promotion, mixed, codec, both compose modes |

## 8. Phasing

- **P1a — representation:** struct union + `ath_has_value`/`ath_num_*` helpers +
  `ath_alloc_float`; keep all behavior int-identical. Green = no regressions.
- **P1b — frontend:** FLOAT lexer token, import-number kind, codegen branch.
  Can construct a float payload end-to-end; `to_string` it.
- **P1c — tower arith + compare:** rewrite the ~13 core fns to promote; pin
  div/mod/bitwise rules; folds.
- **P1d — codec + conversions:** float `to_string`/`parse`, `int_to_float`/
  `float_to_int`/`floor`/`ceil`/`round` shims.
- **P1e — spec/tutorial/tests/conformance** alongside each, per CLAUDE.md.

Each phase keeps `make test-runtime` (both modes) + `pytest` green.

## 9. Deferred sub-questions (resolve at the phase that hits them)

- nan/inf liveness + `to_string` rendering (`"nan"`/`"inf"`?).
- float `mod` semantics: `fmod` vs born-die.
- float in `loop N` count / `chr` index / sleep-ms: truncate vs born-die
  (recommended defaults above).
- exact float `to_string` format string (round-trip vs shortest).
- Phase 2 (bignum): memory model — runtime currently `calloc`s and never frees;
  bigint digit arrays need a freeing or arena story, and overflow-auto-promote
  (int→big) would touch every overflow path. Out of phase-1 scope.
