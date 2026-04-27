#ifndef ATH_RUNTIME_H
#define ATH_RUNTIME_H

#include <stddef.h>

typedef struct ath_obj {
    int alive;
    struct ath_obj *left;
    struct ath_obj *right;
    /* Optional lifetime extensions (SPEC §4.7). Any may be unused.
     * deadline_s: monotonic seconds at which this object becomes dead.
     *             0.0 means "no deadline; lives until explicitly killed."
     * watch_path: NUL-terminated filesystem path. NULL means "no watch".
     *             When set, the object becomes dead as soon as access()
     *             fails on the path.
     * is_oneshot: if nonzero, ath_is_alive returns 1 exactly once and
     *             then sets alive=0 — used by the `once` library entry. */
    double deadline_s;
    const char *watch_path;
    int is_oneshot;
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
ath_obj *ath_alloc_oneshot(void);
ath_obj *ath_alloc_from_library(const char *name);
int      ath_library_lookup(const char *name, double *min_out, double *max_out);

_Noreturn void ath_halt(void);

#endif
