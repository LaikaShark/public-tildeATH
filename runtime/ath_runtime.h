#ifndef ATH_RUNTIME_H
#define ATH_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

/* SPEC §4.8 numeric payload discriminant. NONE = no payload; INT selects
 * num.i (int64); FLOAT selects num.f (IEEE-754 double); BIG is reserved for
 * a future arbitrary-precision phase. */
typedef enum {
    ATH_NUM_NONE = 0,
    ATH_NUM_INT,
    ATH_NUM_FLOAT,
    ATH_NUM_BIG /* reserved */
} ath_num_kind;

typedef struct ath_obj {
    int alive;
    struct ath_obj *left;
    struct ath_obj *right;
    /* Optional lifetime extensions (SPEC §4.7). Any may be unused.
     * deadline_s:      monotonic seconds at which this object becomes dead.
     *                  0.0 means "no deadline; lives until explicitly killed."
     * watch_path:      NUL-terminated filesystem path. NULL means "no watch".
     *                  When set, the object becomes dead as soon as access()
     *                  fails on the path.
     * is_oneshot:      if nonzero, ath_is_alive returns 1 exactly once and
     *                  then sets alive=0 — used by the `once` library entry.
     * awaiting_signal: if nonzero, this object's liveness is tied to a
     *                  pending POSIX signal of that number. ath_is_alive
     *                  flips alive=0 once the signal has been received. */
    double deadline_s;
    const char *watch_path;
    int is_oneshot;
    int awaiting_signal;
    /* SPEC §4.7 ext 5. Set only by ath_alloc_read_file. When set and
     * watch_path is non-NULL, an explicit ath_die call on a still-alive
     * object unlinks watch_path before flipping alive to 0. Passive
     * deaths via the other extensions do NOT trigger unlink. Not copied
     * by ath_clone; cleared by ath_close. */
    int owns_path;
    /* SPEC §4.8 numeric payload (tagged). num_kind selects the active union
     * member: ATH_NUM_NONE = no payload, ATH_NUM_INT = num.i (int64),
     * ATH_NUM_FLOAT = num.f (double). Use ath_has_value() to test presence. */
    ath_num_kind num_kind;
    union { int64_t i; double f; } num;
    /* SPEC §4.8.1 dependency tracking. ath_is_alive returns 0 if any non-null
     * dep is dead. Installed by ath_inherit_lifetime; never written elsewhere. */
    struct ath_obj *dep1;
    struct ath_obj *dep2;
    /* SPEC §4.8.3 dep evaluation mode. 0 (DEP_AND) is the default: result is
     * dead if any non-null dep is dead. 1 (DEP_OR) flips dep1/dep2 to
     * disjunctive: result stays alive until both deps are dead. Set only by
     * ath_or; everything else leaves it at 0. */
    int dep_mode;
    /* §4.6 character identity. is_char is nonzero iff this object carries a
     * character code (0..255) in char_code. Set by ath_char_atom and copied
     * by ath_clone, so a *snapshot* of a character atom (e.g. the result of
     * the subscript form S[N], §4.4.15) is still recognized as that character
     * by ath_atom_to_char without being the canonical table pointer. This is
     * what lets S[N] return a fresh, dependency-carrying object instead of
     * mutating — and thereby globally poisoning — the shared atom. Fields are
     * appended at the end of the struct so the {alive,left,right} prefix the
     * codegen models stays at fixed offsets. */
    int is_char;
    int char_code;
    /* §4.4.12 extended watch sources. watch_pid > 0 ties liveness to a
     * running process (dies when kill(pid,0) reports ESRCH). mtime_path,
     * when non-NULL, ties liveness to a file's modification time: the
     * object dies once stat() reports a different mtime than the
     * (mtime_sec, mtime_nsec) captured at allocation, or the file is gone.
     * Both are monotonic — process exit and the first mtime change are
     * permanent. Appended after the codegen-modeled prefix. */
    int watch_pid;
    const char *mtime_path;
    int64_t mtime_sec;
    int64_t mtime_nsec;
} ath_obj;

#define ATH_DEP_AND 0
#define ATH_DEP_OR  1

/* True iff o carries a numeric payload (INT or FLOAT). Null-safe. Replaces
 * the former `o->has_value` flag (SPEC §4.8). */
static inline int ath_has_value(const ath_obj *o) {
    return o != NULL && o->num_kind != ATH_NUM_NONE;
}

extern ath_obj *ath_NULL;

/* Null-safety contract:
 *   - Every function below tolerates a C-null-pointer input as if it were
 *     ath_NULL. This lets reads of unbound variable slots (which the codegen
 *     initializes to null) flow through the runtime safely.
 *   - ath_decompose and ath_die also short-circuit on the ath_NULL singleton
 *     so it is never mutated. Decomposing ath_NULL yields (ath_NULL, ath_NULL).
 *   - ath_compose with null/ath_NULL operands is well-defined: it builds a
 *     fresh composite whose halves are whatever was passed. */

ath_obj *ath_alloc_alive(void);
ath_obj *ath_compose(ath_obj *l, ath_obj *r);
void     ath_decompose(ath_obj *v, ath_obj **l_out, ath_obj **r_out);
void     ath_die(ath_obj *v);
int      ath_is_alive(ath_obj *v);
void     ath_print(const char *text, size_t len);

/* Newline-free print primitives for the unified `print` statement
 * (SPEC §4.4.6): one `print` emits its parts then exactly one trailing
 * line feed. ath_print_bytes writes `len` raw bytes; ath_print_obj_raw
 * walks an object as a string (§4.6) — both WITHOUT a trailing newline.
 * ath_print / ath_print_obj are these plus a line feed. */
void     ath_print_bytes(const char *text, size_t len);
void     ath_print_obj_raw(ath_obj *s);

/* String I/O. Strings are cons-lists of character atoms per SPEC §4.6. */
ath_obj *ath_input_line(void);
void     ath_print_obj(ath_obj *s);
ath_obj *ath_char_atom(int c);

/* Build a fresh string cons-list from `len` bytes at `bytes`. The
 * resulting cons-list is right-nested and terminated with ath_NULL.
 * Used by the codegen for the `text` statement's literal parts
 * (SPEC §4.4.25). An empty input returns ath_NULL. */
ath_obj *ath_string_from_bytes(const char *bytes, size_t len);

/* Coerce an arbitrary value to a string-like object for `text`
 * interpolation (SPEC §4.4.25): payload-bearing operands run through
 * ath_to_string; everything else (existing cons-lists, NULL, generic
 * composites) is returned unchanged. The returned object is suitable
 * for handing directly to ath_concat. */
ath_obj *ath_coerce_string(ath_obj *v);

/* Lifetime allocators (SPEC §4.7). */
ath_obj *ath_alloc_with_lifetime(double min_s, double max_s);
ath_obj *ath_alloc_watching_file(const char *path);
ath_obj *ath_alloc_watching_signal(int signum);
ath_obj *ath_alloc_watching_signal_by_name(const char *name);
/* Extended watch sources (SPEC §4.4.12). ath_alloc_watching_pid ties
 * liveness to a running process (n's payload is the pid); born dead if the
 * pid is absent, non-positive, or out of range. ath_alloc_watching_mtime
 * ties liveness to a file's modification time, captured at allocation;
 * born dead if the path is missing. */
ath_obj *ath_alloc_watching_pid(ath_obj *n);
ath_obj *ath_alloc_watching_mtime(const char *path);
ath_obj *ath_alloc_oneshot(void);
ath_obj *ath_alloc_from_library(const char *name);
int      ath_library_lookup(const char *name, double *min_out, double *max_out);

/* Register a user-defined library entry (SPEC §5.3). Subsequent calls to
 * ath_alloc_from_library with this name use the [min_s, max_s] range,
 * overriding any built-in entry of the same name. Intended to be called
 * from main's prologue by the compiler in response to --define-lifetime.
 * The `name` pointer must remain valid for the lifetime of the program. */
void     ath_register_lifetime(const char *name, double min_s, double max_s);

/* Numeric payload and arithmetic (SPEC §4.8). All arithmetic helpers return
 * a fresh object that inherits the lifetimes of their operands via
 * ath_inherit_lifetime. On overflow, divide-by-zero, dead operand, or
 * missing payload the result is born dead (alive=0, num_kind=NONE). */
ath_obj *ath_alloc_number(int64_t v);     /* ATH_NUM_INT payload   */
ath_obj *ath_alloc_float(double v);       /* ATH_NUM_FLOAT payload */
void     ath_inherit_lifetime(ath_obj *result, ath_obj *a, ath_obj *b);
ath_obj *ath_add(ath_obj *x, ath_obj *y);
ath_obj *ath_sub(ath_obj *x, ath_obj *y);
ath_obj *ath_mul(ath_obj *x, ath_obj *y);
ath_obj *ath_div(ath_obj *x, ath_obj *y);
ath_obj *ath_mod(ath_obj *x, ath_obj *y);
ath_obj *ath_to_string(ath_obj *x, ath_obj *unused);
ath_obj *ath_parse(ath_obj *s, ath_obj *unused);

/* Comparisons as verdicts (SPEC §4.8.3). Each returns an object alive iff
 * the comparison holds, with lifetime inherited from both operands. */
ath_obj *ath_lt(ath_obj *x, ath_obj *y);
ath_obj *ath_eq(ath_obj *x, ath_obj *y);
ath_obj *ath_gt(ath_obj *x, ath_obj *y);
ath_obj *ath_le(ath_obj *x, ath_obj *y);
ath_obj *ath_ge(ath_obj *x, ath_obj *y);
ath_obj *ath_ne(ath_obj *x, ath_obj *y);

/* Logical combinators over verdicts (SPEC §4.8.3). Born dead if both
 * (AND: either) operands are dead at call. AND uses ath_inherit_lifetime
 * (conjunctive deps). OR sets dep_mode=ATH_DEP_OR so the result stays
 * alive until both deps are dead. NOT is intentionally absent: it would
 * require a dead->alive transition (forbidden by §4.1); express negation
 * at the observation site via `~ATH(!V)` or `BRANCH(!V)`. */
ath_obj *ath_and(ath_obj *x, ath_obj *y);
ath_obj *ath_or(ath_obj *x, ath_obj *y);

/* Compose with dep propagation (SPEC §4.8.4). Equivalent to
 *   ath_compose(x, y) followed by ath_inherit_lifetime(result, x, y).
 * Used to build composite carriers (notably the (needle, replacement)
 * pair consumed by ath_replace) whose lifetimes must track their
 * constituent operands. BIFURCATE compose remains available for the
 * no-dep case. */
ath_obj *ath_entangle(ath_obj *x, ath_obj *y);

/* String operations on cons-list-structured objects (SPEC §4.8.4).
 * ath_index and ath_slice are also invoked by the subscript and
 * range-subscript statement codegen (§4.4.15, §4.4.16). All install
 * operand dependencies on results via ath_inherit_lifetime. */
ath_obj *ath_length(ath_obj *s, ath_obj *unused);
ath_obj *ath_concat(ath_obj *a, ath_obj *b);
ath_obj *ath_index(ath_obj *s, ath_obj *n);
ath_obj *ath_slice(ath_obj *s, ath_obj *range);
/* Search and replace (SPEC §4.8.4). ath_replace and ath_replace_all
 * decompose pair into (needle, replacement). All three install operand
 * deps on results via ath_inherit_lifetime — within the compose-pair
 * dep-tracking caveat documented in §4.8.4. */
ath_obj *ath_find(ath_obj *hay, ath_obj *needle);
ath_obj *ath_replace(ath_obj *s, ath_obj *pair);
ath_obj *ath_replace_all(ath_obj *s, ath_obj *pair);

/* String predicates (verdicts) and transforms (SPEC §4.8.4 extensions).
 * Verdicts (streq/startswith/endswith/strlt/strgt): alive iff the
 *   relation holds, with operand deps installed. Born dead on
 *   malformed string (a non-character left half partway through the
 *   walk). Empty-prefix/empty-suffix are alive — every string starts
 *   and ends with the empty string.
 * Transforms (lower/upper/trim/lstrip/rstrip): return a fresh
 *   cons-list with the operand installed as a dep. Whitespace for
 *   stripping is space, tab, LF, CR.
 * SPLIT: produce a cons-list of cons-list strings, split by sep.
 *   Empty sep is born dead. Trailing sep yields a trailing empty
 *   element.
 * JOIN: walk LIST's right-spine, concat each element interleaved with
 *   sep. Empty LIST returns NULL. */
ath_obj *ath_streq(ath_obj *a, ath_obj *b);
ath_obj *ath_startswith(ath_obj *hay, ath_obj *prefix);
ath_obj *ath_endswith(ath_obj *hay, ath_obj *suffix);
ath_obj *ath_strlt(ath_obj *a, ath_obj *b);
ath_obj *ath_strgt(ath_obj *a, ath_obj *b);
ath_obj *ath_lower(ath_obj *s, ath_obj *unused);
ath_obj *ath_upper(ath_obj *s, ath_obj *unused);
ath_obj *ath_trim(ath_obj *s, ath_obj *unused);
ath_obj *ath_lstrip(ath_obj *s, ath_obj *unused);
ath_obj *ath_rstrip(ath_obj *s, ath_obj *unused);
ath_obj *ath_split(ath_obj *s, ath_obj *sep);
ath_obj *ath_join(ath_obj *list, ath_obj *sep);

/* String search/measure, construction, and atom bridge (SPEC §4.8.4
 * second-wave extensions, group 2).
 * Search (contains/count/rfind): CONTAINS is a verdict; COUNT and RFIND
 *   carry an int64 payload. Empty needle: contained everywhere (CONTAINS
 *   alive), born dead for COUNT, matches at len(HAY) for RFIND.
 * Construct (repeat/reverse/pad_left/pad_right): fresh cons-list with the
 *   operand(s) installed as deps. REPEAT and the PAD ops take a number
 *   payload as the second operand; N<0 or no payload is dead. Padding
 *   uses the space character and is a no-op widening past len(S).
 * Atom bridge (ord/chr): ORD maps a character atom (e.g. from S[N]) to
 *   its 0..255 code; CHR maps a 0..255 payload back to a length-1
 *   string. Out-of-range or malformed input is born dead. */
ath_obj *ath_contains(ath_obj *hay, ath_obj *needle);
ath_obj *ath_count(ath_obj *hay, ath_obj *needle);
ath_obj *ath_rfind(ath_obj *hay, ath_obj *needle);
ath_obj *ath_repeat(ath_obj *s, ath_obj *n);
ath_obj *ath_reverse(ath_obj *s, ath_obj *unused);
ath_obj *ath_pad_left(ath_obj *s, ath_obj *n);
ath_obj *ath_pad_right(ath_obj *s, ath_obj *n);
ath_obj *ath_ord(ath_obj *a, ath_obj *unused);
ath_obj *ath_chr(ath_obj *n, ath_obj *unused);

/* Numeric second-wave builtins (SPEC §4.8.2 extensions). All take int64
 * payloads and are born dead on a non-payload or dead operand. POW
 * rejects negative exponents; ABS/NEG/GCD reject INT64_MIN (no positive
 * representation); SHL/SHR require a 0..63 shift; CLAMP packs (LO, HI) as
 * a pair and rejects LO > HI. */
ath_obj *ath_pow(ath_obj *x, ath_obj *y);
ath_obj *ath_abs(ath_obj *x, ath_obj *unused);
ath_obj *ath_neg(ath_obj *x, ath_obj *unused);
ath_obj *ath_min(ath_obj *x, ath_obj *y);
ath_obj *ath_max(ath_obj *x, ath_obj *y);
ath_obj *ath_gcd(ath_obj *x, ath_obj *y);
ath_obj *ath_sign(ath_obj *x, ath_obj *unused);
ath_obj *ath_band(ath_obj *x, ath_obj *y);
ath_obj *ath_bor(ath_obj *x, ath_obj *y);
ath_obj *ath_bxor(ath_obj *x, ath_obj *y);
ath_obj *ath_bnot(ath_obj *x, ath_obj *unused);
ath_obj *ath_shl(ath_obj *x, ath_obj *y);
ath_obj *ath_shr(ath_obj *x, ath_obj *y);
ath_obj *ath_clamp(ath_obj *x, ath_obj *pair);

/* Float conversions and rounding (§4.8.2). */
ath_obj *ath_int_to_float(ath_obj *x, ath_obj *unused);
ath_obj *ath_float_to_int(ath_obj *x, ath_obj *unused);
ath_obj *ath_floor(ath_obj *x, ath_obj *unused);
ath_obj *ath_ceil(ath_obj *x, ath_obj *unused);
ath_obj *ath_round(ath_obj *x, ath_obj *unused);

/* Float transcendentals (§4.8.2). Each returns a FLOAT; out-of-domain
 * inputs yield live nan/inf. */
ath_obj *ath_sqrt(ath_obj *x, ath_obj *unused);
ath_obj *ath_cbrt(ath_obj *x, ath_obj *unused);
ath_obj *ath_exp(ath_obj *x, ath_obj *unused);
ath_obj *ath_log(ath_obj *x, ath_obj *unused);
ath_obj *ath_log2(ath_obj *x, ath_obj *unused);
ath_obj *ath_log10(ath_obj *x, ath_obj *unused);
ath_obj *ath_sin(ath_obj *x, ath_obj *unused);
ath_obj *ath_cos(ath_obj *x, ath_obj *unused);
ath_obj *ath_tan(ath_obj *x, ath_obj *unused);
ath_obj *ath_asin(ath_obj *x, ath_obj *unused);
ath_obj *ath_acos(ath_obj *x, ath_obj *unused);
ath_obj *ath_atan(ath_obj *x, ath_obj *unused);
ath_obj *ath_atan2(ath_obj *y, ath_obj *x);
ath_obj *ath_hypot(ath_obj *x, ath_obj *y);

/* String polish builtins (SPEC §4.8.4 extensions). COMPARE is the
 * three-way (-1/0/1) form of the string verdicts. CHAR_AT returns a
 * length-1 string (vs S[N]'s bare atom). FIND_FROM packs (NEEDLE, START).
 * CAPITALIZE/TITLE are case transforms; the STRIP_CHARS family strips a
 * custom character set; the PAD_*_WITH ops pad with a custom fill
 * character packed as (WIDTH, FILL). */
ath_obj *ath_compare(ath_obj *a, ath_obj *b);
ath_obj *ath_char_at(ath_obj *s, ath_obj *n);
ath_obj *ath_find_from(ath_obj *s, ath_obj *pair);
ath_obj *ath_capitalize(ath_obj *s, ath_obj *unused);
ath_obj *ath_title(ath_obj *s, ath_obj *unused);
ath_obj *ath_strip_chars(ath_obj *s, ath_obj *chars);
ath_obj *ath_lstrip_chars(ath_obj *s, ath_obj *chars);
ath_obj *ath_rstrip_chars(ath_obj *s, ath_obj *chars);
ath_obj *ath_pad_left_with(ath_obj *s, ath_obj *pair);
ath_obj *ath_pad_right_with(ath_obj *s, ath_obj *pair);

/* Generic cons-list operations (SPEC §4.8.6). These walk the right-spine
 * of any list and read each element's int64 payload. SUM/PRODUCT fold the
 * payloads (identity 0 / 1; empty list yields the identity); MAXIMUM/
 * MINIMUM born-die on an empty list; MEMBER is a verdict over payload
 * equality; TAKE/DROP return a fresh sublist. A non-payload or dead
 * element born-dies the aggregates, so they reject strings (whose elements
 * are character atoms). */
ath_obj *ath_sum(ath_obj *list, ath_obj *unused);
ath_obj *ath_product(ath_obj *list, ath_obj *unused);
ath_obj *ath_maximum(ath_obj *list, ath_obj *unused);
ath_obj *ath_minimum(ath_obj *list, ath_obj *unused);
ath_obj *ath_member(ath_obj *list, ath_obj *x);
ath_obj *ath_take(ath_obj *list, ath_obj *n);
ath_obj *ath_drop(ath_obj *list, ath_obj *n);

/* n-ary lifetime combinators (SPEC §4.8.6): the list generalizations of
 * the AND/OR verdicts (§4.8.3). ALL_OF is alive iff every element is alive
 * (empty → alive); ANY_OF iff some element is alive (empty → dead). Both
 * fold ath_and/ath_or into a dep-tracked tree, so element deaths propagate
 * to the result. */
ath_obj *ath_all_of(ath_obj *list, ath_obj *unused);
ath_obj *ath_any_of(ath_obj *list, ath_obj *unused);

/* Iteration count for the `repeat N { ... }` loop (SPEC §4.4.26): N's
 * non-negative int64 payload, or 0 if N is dead/payload-less/negative. */
int64_t ath_count_of(ath_obj *n);

/* Shallow snapshot clone (SPEC §4.4.18). Copies every field of v except
 * dep1/dep2 and owns_path, which are zeroed. Independent identity —
 * killing one of (v, result) does not kill the other, and the clone
 * never owns the file even if v did. */
ath_obj *ath_clone(ath_obj *v);

/* File I/O (SPEC §4.4.21-24, §4.7 ext 5). ath_alloc_read_file slurps
 * the file into a cons-list whose head is a non-interned wrapper
 * carrying watch_path and owns_path=1. ath_write_file and
 * ath_append_file return a fresh verdict object (alive on success).
 * ath_close disowns + kills v without unlinking. ath_die unlinks
 * watch_path when called on a still-alive owner. */
ath_obj *ath_alloc_read_file(const char *path);
ath_obj *ath_write_file(ath_obj *s, const char *path);
ath_obj *ath_append_file(ath_obj *s, const char *path);
void     ath_close(ath_obj *v);

/* Time and randomness (SPEC §4.4.19, §4.4.20, §4.8.5). All durations
 * are int64 milliseconds. ath_sleep_ms is a no-op on dead/no-payload
 * n. ath_alloc_timer_ms binds a fresh alive object with a deadline;
 * the duration argument is not dep-tracked on the result. */
void     ath_sleep_ms(ath_obj *n);
ath_obj *ath_alloc_timer_ms(ath_obj *n);
ath_obj *ath_now(ath_obj *a, ath_obj *b);
ath_obj *ath_random_range(ath_obj *lo, ath_obj *hi);

_Noreturn void ath_halt(void);

#endif
