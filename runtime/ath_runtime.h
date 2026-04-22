#ifndef ATH_RUNTIME_H
#define ATH_RUNTIME_H

#include <stddef.h>

typedef struct ath_obj {
    int alive;
    struct ath_obj *left;
    struct ath_obj *right;
} ath_obj;

extern ath_obj *ath_NULL;

ath_obj *ath_alloc_alive(void);
ath_obj *ath_compose(ath_obj *l, ath_obj *r);
void     ath_decompose(ath_obj *v, ath_obj **l_out, ath_obj **r_out);
void     ath_die(ath_obj *v);
int      ath_is_alive(ath_obj *v);
void     ath_print(const char *text, size_t len);
_Noreturn void ath_halt(void);

#endif
