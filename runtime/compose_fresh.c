// Composition discipline FRESH: each ath_compose allocates a new alive object.
// Two calls with the same operands produce two distinct objects.

#include "ath_runtime.h"

ath_obj *ath_compose(ath_obj *l, ath_obj *r) {
    ath_obj *o = ath_alloc_alive();
    o->left = l;
    o->right = r;
    return o;
}
