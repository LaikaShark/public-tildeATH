#ifndef ATH_RUNTIME_H
#define ATH_RUNTIME_H

#include <stddef.h>

typedef struct ath_obj {
    int alive;
    struct ath_obj *left;
    struct ath_obj *right;
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

_Noreturn void ath_halt(void);

#endif
