#define _POSIX_C_SOURCE 200809L

#include "ath_runtime.h"

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

    /* Composition identity under the active discipline (SPEC §4.4.3). */
    ath_obj *bc2 = ath_compose(b, c);
#ifdef ATH_INTERN_MODE
    /* Intern: same operands -> same canonical object. */
    assert(bc == bc2);
#else
    /* Fresh: every call allocates a distinct composite. */
    assert(bc != bc2);
#endif
    assert(ath_is_alive(bc2));

    /* Killing a composite affects its halves and siblings differently
     * depending on whether the sibling is the same object. */
    ath_die(bc);
    assert(!ath_is_alive(bc));
    assert(ath_is_alive(b));
    assert(ath_is_alive(c));
#ifdef ATH_INTERN_MODE
    /* Intern: bc2 IS bc, so it died too. */
    assert(!ath_is_alive(bc2));
#else
    /* Fresh: bc2 is a distinct composite, still alive. */
    assert(ath_is_alive(bc2));
#endif

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

    /* Null-safety: operations on a C null pointer behave as if it were NULL. */
    assert(!ath_is_alive(NULL));
    ath_die(NULL); /* must not crash */

    ath_obj *nl, *nr;
    ath_decompose(NULL, &nl, &nr);
    assert(nl == ath_NULL && nr == ath_NULL);

    /* Decomposing ath_NULL yields (ath_NULL, ath_NULL) and must not mutate it. */
    ath_obj *saved_left = ath_NULL->left;
    ath_obj *saved_right = ath_NULL->right;
    ath_obj *nl2, *nr2;
    ath_decompose(ath_NULL, &nl2, &nr2);
    assert(nl2 == ath_NULL && nr2 == ath_NULL);
    assert(ath_NULL->left == saved_left);
    assert(ath_NULL->right == saved_right);

    /* Killing ath_NULL keeps it dead and does not touch its fields. */
    ath_die(ath_NULL);
    assert(!ath_is_alive(ath_NULL));
    assert(ath_NULL->left == saved_left);
    assert(ath_NULL->right == saved_right);

    /* Compose with null/NULL operands builds a fresh alive composite. */
    ath_obj *cn = ath_compose(NULL, ath_NULL);
    assert(ath_is_alive(cn));
    assert(cn->left == NULL);
    assert(cn->right == ath_NULL);

    /* Character atoms are canonical per character code. */
    assert(ath_char_atom('a') == ath_char_atom('a'));
    assert(ath_char_atom('a') != ath_char_atom('b'));
    assert(ath_is_alive(ath_char_atom('z')));
    /* Same code via differently-typed inputs gives same atom. */
    assert(ath_char_atom('A') == ath_char_atom(0x41));

    /* PRINT2 round-trip: build "Hi" and verify it prints "Hi\n". */
    ath_obj *str = ath_compose(
        ath_char_atom('H'),
        ath_compose(ath_char_atom('i'), ath_NULL));
    fputs("expect Hi: ", stdout);
    ath_print_obj(str);

    /* PRINT2 of NULL emits just a newline. */
    fputs("expect blank: ", stdout);
    ath_print_obj(ath_NULL);

    /* PRINT2 of a null pointer also emits just a newline (null-safety). */
    fputs("expect blank: ", stdout);
    ath_print_obj(NULL);

    /* PRINT2 stops at the first unrecognized left half. */
    ath_obj *garbage = ath_compose(ath_char_atom('X'),
                                   ath_compose(ath_alloc_alive(), ath_NULL));
    fputs("expect X: ", stdout);
    ath_print_obj(garbage);

    /* --- Library lookup --- */
    double lo, hi;
    assert(ath_library_lookup("fly", &lo, &hi));
    assert(lo == 86400.0 && hi == 259200.0);

    assert(ath_library_lookup("FLY", &lo, &hi));     /* case-insensitive */
    assert(lo == 86400.0 && hi == 259200.0);

    assert(ath_library_lookup("soap bubble", &lo, &hi));
    assert(lo == 2.0 && hi == 30.0);

    assert(!ath_library_lookup("not a real concept", &lo, &hi));
    assert(!ath_library_lookup(NULL, &lo, &hi));

    /* --- Lifetime allocation --- */

    /* "instant" lifetime: born dead. */
    ath_obj *inst = ath_alloc_from_library("instant");
    assert(!ath_is_alive(inst));

    /* "tick" (1-10 ms) is alive at first, dead after a 50ms sleep. */
    ath_obj *tick = ath_alloc_from_library("tick");
    assert(ath_is_alive(tick));
    struct timespec wait = {0, 50 * 1000 * 1000};   /* 50 ms */
    nanosleep(&wait, NULL);
    assert(!ath_is_alive(tick));

    /* A bare "alive" allocation has no deadline and stays alive across sleeps. */
    ath_obj *forever = ath_alloc_alive();
    assert(ath_is_alive(forever));
    nanosleep(&wait, NULL);
    assert(ath_is_alive(forever));

    /* Library names not in the table fall through to a plain alive object. */
    ath_obj *anon = ath_alloc_from_library("garbage_garbage_garbage");
    assert(ath_is_alive(anon));

    /* --- File watching --- */

    /* Nonexistent path: born dead. */
    ath_obj *missing = ath_alloc_watching_file("/tmp/ath_test_definitely_not_here_xyz");
    assert(!ath_is_alive(missing));

    /* Create a temp file, watch it, delete it, observe death. */
    const char *tmppath = "/tmp/ath_test_watch_target";
    unlink(tmppath);  /* clean slate */
    FILE *fp = fopen(tmppath, "w");
    assert(fp != NULL);
    fputs("ok\n", fp);
    fclose(fp);

    ath_obj *watcher = ath_alloc_watching_file(tmppath);
    assert(ath_is_alive(watcher));

    unlink(tmppath);
    assert(!ath_is_alive(watcher));

    /* Even if the file is recreated, the watcher stays dead (one-way death). */
    fp = fopen(tmppath, "w");
    assert(fp != NULL);
    fclose(fp);
    assert(!ath_is_alive(watcher));
    unlink(tmppath);

    /* --- One-shot --- */

    /* "once" library entry: alive on first observation, dead afterward. */
    ath_obj *o1 = ath_alloc_from_library("once");
    assert(ath_is_alive(o1));
    assert(!ath_is_alive(o1));
    assert(!ath_is_alive(o1));

    /* Case-insensitive name. */
    ath_obj *o2 = ath_alloc_from_library("ONCE");
    assert(ath_is_alive(o2));
    assert(!ath_is_alive(o2));

    /* Explicit kill before observation makes the body never run. */
    ath_obj *o3 = ath_alloc_oneshot();
    ath_die(o3);
    assert(!ath_is_alive(o3));

    /* --- User-defined lifetime entries --- */

    /* A brand-new name resolves once registered. */
    assert(!ath_library_lookup("tortoise", &lo, &hi));
    ath_register_lifetime("tortoise", 50.0, 150.0);
    assert(ath_library_lookup("tortoise", &lo, &hi));
    assert(lo == 50.0 && hi == 150.0);

    /* User entries override built-ins of the same name. */
    assert(ath_library_lookup("fly", &lo, &hi));
    assert(lo == 86400.0);  /* original built-in */
    ath_register_lifetime("fly", 0.001, 0.002);
    assert(ath_library_lookup("fly", &lo, &hi));
    assert(lo == 0.001 && hi == 0.002);  /* overridden */

    /* Case-insensitive match against user entries too. */
    assert(ath_library_lookup("TORTOISE", &lo, &hi));
    assert(lo == 50.0);

    /* --- Signal watching --- */

    /* Watching a recognized signal is alive until the signal is received. */
    ath_obj *sig_watcher = ath_alloc_watching_signal_by_name("SIGUSR1");
    assert(ath_is_alive(sig_watcher));
    /* raise() delivers synchronously; the handler runs before raise returns. */
    raise(SIGUSR1);
    assert(!ath_is_alive(sig_watcher));

    /* Subsequent allocations watching the same signal are immediately dead
     * because the signal-received flag is sticky. */
    ath_obj *sig_late = ath_alloc_watching_signal_by_name("SIGUSR1");
    assert(!ath_is_alive(sig_late));

    /* Unknown signal name -> born dead with a stderr warning. */
    fputs("(expect one warning below) ", stderr);
    ath_obj *bad_sig = ath_alloc_watching_signal_by_name("NOTASIGNAL");
    assert(!ath_is_alive(bad_sig));

    /* Case-insensitive name lookup. */
    ath_obj *sig_case = ath_alloc_watching_signal_by_name("sigusr2");
    assert(ath_is_alive(sig_case));
    raise(SIGUSR2);
    assert(!ath_is_alive(sig_case));

    fputs("runtime test: all checks passed\n", stdout);
    return 0;
}
