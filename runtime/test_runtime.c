#include "ath_runtime.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    /* NULL is born dead and stays dead. */
    assert(!ath_is_alive(ath_NULL));

    /* Fresh allocation is alive. */
    ath_obj *a = ath_alloc_alive();
    assert(ath_is_alive(a));

    /* Death is permanent. */
    ath_die(a);
    assert(!ath_is_alive(a));
    ath_die(a);
    assert(!ath_is_alive(a));

    /* Compose builds a live composite with the given halves. */
    ath_obj *b = ath_alloc_alive();
    ath_obj *c = ath_alloc_alive();
    ath_obj *bc = ath_compose(b, c);
    assert(ath_is_alive(bc));
    assert(bc->left == b);
    assert(bc->right == c);

    /* Fresh mode: two composes of structurally equal operands are distinct. */
    ath_obj *bc2 = ath_compose(b, c);
    assert(bc != bc2);
    assert(ath_is_alive(bc2));

    /* Killing a composite does not affect its halves or its siblings. */
    ath_die(bc);
    assert(!ath_is_alive(bc));
    assert(ath_is_alive(b));
    assert(ath_is_alive(c));
    assert(ath_is_alive(bc2));

    /* Decompose on a leaf lazily allocates two fresh alive halves. */
    ath_obj *leaf = ath_alloc_alive();
    assert(leaf->left == NULL && leaf->right == NULL);
    ath_obj *l, *r;
    ath_decompose(leaf, &l, &r);
    assert(l && r && l != r);
    assert(ath_is_alive(l) && ath_is_alive(r));

    /* Decomposing the same leaf again yields the same halves. */
    ath_obj *l2, *r2;
    ath_decompose(leaf, &l2, &r2);
    assert(l == l2 && r == r2);

    /* Killing a half does not kill the parent or sibling. */
    ath_die(l);
    assert(!ath_is_alive(l));
    assert(ath_is_alive(leaf));
    assert(ath_is_alive(r));

    /* Print writes payload then newline. */
    const char *msg = "runtime print check";
    ath_print(msg, strlen(msg));
    ath_print("", 0);

    fputs("runtime test: all checks passed\n", stdout);
    return 0;
}
