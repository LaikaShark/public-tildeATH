/* Composition discipline: FRESH (SPEC §4.4.3). Each call to ath_compose
 * allocates a new alive object. Two ath_compose(l, r) calls with the
 * same operands produce two distinct objects. */

#include "ath_runtime.h"

ath_obj *ath_compose(ath_obj *l, ath_obj *r) {
    ath_obj *o = ath_alloc_alive();
    o->left = l;
    o->right = r;
    return o;
}
