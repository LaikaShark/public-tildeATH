#include "ath_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define ATH_CHAR_TABLE_SIZE 256
static ath_obj *ath_char_table[ATH_CHAR_TABLE_SIZE] = { 0 };

ath_obj *ath_char_atom(int c) {
    unsigned int idx = (unsigned int)c & 0xFFu;
    if (ath_char_table[idx] == NULL) {
        ath_char_table[idx] = ath_alloc_alive();
    }
    return ath_char_table[idx];
}

static int ath_atom_to_char(ath_obj *o) {
    for (int i = 0; i < ATH_CHAR_TABLE_SIZE; i++) {
        if (ath_char_table[i] == o) {
            return i;
        }
    }
    return -1;
}

ath_obj *ath_input_line(void) {
    char buf[4096];
    if (fgets(buf, sizeof(buf), stdin) == NULL) {
        return ath_NULL;
    }
    size_t n = strlen(buf);
    if (n > 0 && buf[n - 1] == '\n') {
        buf[--n] = '\0';
    }
    if (n > 0 && buf[n - 1] == '\r') {
        buf[--n] = '\0';
    }
    ath_obj *acc = ath_NULL;
    for (size_t i = n; i > 0; i--) {
        ath_obj *c = ath_char_atom((unsigned char)buf[i - 1]);
        acc = ath_compose(c, acc);
    }
    return acc;
}

void ath_print_obj(ath_obj *s) {
    while (s != NULL && s != ath_NULL && ath_is_alive(s)) {
        ath_obj *l, *r;
        ath_decompose(s, &l, &r);
        int ch = ath_atom_to_char(l);
        if (ch < 0) {
            break;
        }
        fputc(ch, stdout);
        s = r;
    }
    fputc('\n', stdout);
}

_Noreturn void ath_halt(void) {
    fflush(stdout);
    exit(0);
}
