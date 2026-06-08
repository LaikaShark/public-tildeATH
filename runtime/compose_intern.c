// Composition discipline INTERN: ath_compose hash-consed by raw pointer-pair
// (left, right). Two calls with same operands return the same object, alive or
// dead. Killing an interned composite affects every variable that observed it.

#include "ath_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct intern_entry {
    ath_obj *left;
    ath_obj *right;
    ath_obj *result;
    struct intern_entry *next;
} intern_entry;

// power of 2
#define ATH_INTERN_TABLE_SIZE 4096
static intern_entry *ath_intern_table[ATH_INTERN_TABLE_SIZE];

static size_t intern_hash(ath_obj *l, ath_obj *r) {
    // SplitMix64-flavored mix of two pointer values
    uintptr_t a = (uintptr_t)l;
    uintptr_t b = (uintptr_t)r;
    uintptr_t h = a + 0x9E3779B97F4A7C15ULL;
    h ^= h >> 30;
    h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 27;
    h += b * 0x94D049BB133111EBULL;
    h ^= h >> 31;
    return (size_t)h & (ATH_INTERN_TABLE_SIZE - 1);
}

ath_obj *ath_compose(ath_obj *l, ath_obj *r) {
    size_t idx = intern_hash(l, r);
    for (intern_entry *e = ath_intern_table[idx]; e; e = e->next) {
        if (e->left == l && e->right == r) {
            return e->result;
        }
    }
    ath_obj *o = ath_alloc_alive();
    o->left = l;
    o->right = r;
    intern_entry *ne = (intern_entry *)malloc(sizeof(intern_entry));
    if (!ne) {
        fputs("ath: out of memory (intern table)\n", stderr);
        exit(1);
    }
    ne->left = l;
    ne->right = r;
    ne->result = o;
    ne->next = ath_intern_table[idx];
    ath_intern_table[idx] = ne;
    return o;
}
