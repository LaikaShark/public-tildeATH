#include "ath_runtime.h"

#include <stdio.h>
#include <stdlib.h>

static ath_obj ath_NULL_storage = { 0, NULL, NULL };
ath_obj *ath_NULL = &ath_NULL_storage;

ath_obj *ath_alloc_alive(void) {
    ath_obj *o = (ath_obj *)calloc(1, sizeof(ath_obj));
    if (!o) {
        fputs("ath: out of memory\n", stderr);
        exit(1);
    }
    o->alive = 1;
    return o;
}

ath_obj *ath_compose(ath_obj *l, ath_obj *r) {
    ath_obj *o = ath_alloc_alive();
    o->left = l;
    o->right = r;
    return o;
}

void ath_decompose(ath_obj *v, ath_obj **l_out, ath_obj **r_out) {
    if (v == NULL || v == ath_NULL) {
        *l_out = ath_NULL;
        *r_out = ath_NULL;
        return;
    }
    if (v->left == NULL) {
        v->left = ath_alloc_alive();
        v->right = ath_alloc_alive();
    }
    *l_out = v->left;
    *r_out = v->right;
}

void ath_die(ath_obj *v) {
    if (v == NULL || v == ath_NULL) {
        return;
    }
    v->alive = 0;
}

int ath_is_alive(ath_obj *v) {
    if (v == NULL) {
        return 0;
    }
    return v->alive;
}

void ath_print(const char *text, size_t len) {
    if (len > 0) {
        fwrite(text, 1, len, stdout);
    }
    fputc('\n', stdout);
}

_Noreturn void ath_halt(void) {
    fflush(stdout);
    exit(0);
}
