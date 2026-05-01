#ifndef ATH_RUNTIME_H
#define ATH_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

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
    /* SPEC §4.8 numeric payload. has_value is nonzero iff value is set. */
    int has_value;
    int64_t value;
    /* SPEC §4.8.1 dependency tracking. ath_is_alive returns 0 if any non-null
     * dep is dead. Installed by ath_inherit_lifetime; never written elsewhere. */
    struct ath_obj *dep1;
    struct ath_obj *dep2;
} ath_obj;

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

/* String I/O. Strings are cons-lists of character atoms per SPEC §4.6. */
ath_obj *ath_input_line(void);
void     ath_print_obj(ath_obj *s);
ath_obj *ath_char_atom(int c);

/* Lifetime allocators (SPEC §4.7). */
ath_obj *ath_alloc_with_lifetime(double min_s, double max_s);
ath_obj *ath_alloc_watching_file(const char *path);
ath_obj *ath_alloc_watching_signal(int signum);
ath_obj *ath_alloc_watching_signal_by_name(const char *name);
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
 * missing payload the result is born dead (alive=0, has_value=0). */
ath_obj *ath_alloc_number(int64_t v);
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

/* String operations on cons-list-structured objects (SPEC §4.8.4).
 * ath_index and ath_slice are also invoked by the subscript and
 * range-subscript statement codegen (§4.4.15, §4.4.16). All install
 * operand dependencies on results via ath_inherit_lifetime. */
ath_obj *ath_length(ath_obj *s, ath_obj *unused);
ath_obj *ath_concat(ath_obj *a, ath_obj *b);
ath_obj *ath_index(ath_obj *s, ath_obj *n);
ath_obj *ath_slice(ath_obj *s, ath_obj *range);

/* Shallow snapshot clone (SPEC §4.4.18). Copies every field of v except
 * dep1/dep2, which are zeroed. Independent identity — killing one of
 * (v, result) does not kill the other. */
ath_obj *ath_clone(ath_obj *v);

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
