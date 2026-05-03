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

    /* --- Numeric payload and arithmetic (SPEC §4.8) --- */

    ath_obj *n3 = ath_alloc_number(3);
    ath_obj *n4 = ath_alloc_number(4);
    assert(ath_is_alive(n3));
    assert(n3->has_value && n3->value == 3);
    assert(n4->has_value && n4->value == 4);

    ath_obj *sum = ath_add(n3, n4);
    assert(ath_is_alive(sum));
    assert(sum->has_value && sum->value == 7);

    ath_obj *diff = ath_sub(n3, n4);
    assert(ath_is_alive(diff));
    assert(diff->value == -1);

    ath_obj *prod = ath_mul(n3, n4);
    assert(prod->value == 12);

    ath_obj *quot = ath_div(ath_alloc_number(20), ath_alloc_number(6));
    assert(quot->value == 3);

    ath_obj *rem = ath_mod(ath_alloc_number(20), ath_alloc_number(6));
    assert(rem->value == 2);

    /* Overflow → born dead. */
    ath_obj *big = ath_alloc_number(INT64_MAX);
    ath_obj *overflow = ath_add(big, ath_alloc_number(1));
    assert(!ath_is_alive(overflow));
    assert(!overflow->has_value);

    /* Multiplication overflow. */
    ath_obj *mulover = ath_mul(big, ath_alloc_number(2));
    assert(!ath_is_alive(mulover));

    /* Division by zero → born dead. */
    ath_obj *divzero = ath_div(ath_alloc_number(5), ath_alloc_number(0));
    assert(!ath_is_alive(divzero));

    /* INT64_MIN / -1 special case. */
    ath_obj *intmin_div = ath_div(ath_alloc_number(INT64_MIN), ath_alloc_number(-1));
    assert(!ath_is_alive(intmin_div));

    /* Dead operand → born dead result. */
    ath_obj *dead_op = ath_alloc_number(5);
    ath_die(dead_op);
    ath_obj *from_dead = ath_add(dead_op, ath_alloc_number(1));
    assert(!ath_is_alive(from_dead));

    /* --- Lifetime inheritance --- */

    ath_obj *x = ath_alloc_number(10);
    ath_obj *y = ath_alloc_number(20);
    ath_obj *xy = ath_add(x, y);
    assert(ath_is_alive(xy));
    /* Kill an operand — derived value dies on the next observation. */
    ath_die(x);
    assert(!ath_is_alive(xy));
    /* Recheck a few times: still dead. */
    assert(!ath_is_alive(xy));

    /* Chained: (an+bn)+cn dies if any of an, bn, cn dies. */
    ath_obj *an = ath_alloc_number(1);
    ath_obj *bn = ath_alloc_number(2);
    ath_obj *cn2 = ath_alloc_number(3);
    ath_obj *ab = ath_add(an, bn);
    ath_obj *abc = ath_add(ab, cn2);
    assert(ath_is_alive(abc));
    assert(abc->value == 6);
    ath_die(bn);   /* bn is a transitive dep via ab */
    assert(!ath_is_alive(abc));

    /* --- TO_STRING / PARSE round-trip --- */

    ath_obj *s = ath_to_string(ath_alloc_number(-12345), ath_NULL);
    fputs("expect -12345: ", stdout);
    ath_print_obj(s);

    /* Empty string for missing payload. */
    ath_obj *empty = ath_to_string(ath_NULL, ath_NULL);
    assert(empty == ath_NULL);

    /* Parse a string back into a number. */
    ath_obj *parse_src = ath_compose(ath_char_atom('4'),
                          ath_compose(ath_char_atom('2'), ath_NULL));
    ath_obj *parsed = ath_parse(parse_src, ath_NULL);
    assert(ath_is_alive(parsed));
    assert(parsed->has_value && parsed->value == 42);

    /* Malformed string → born dead. */
    ath_obj *bad_src = ath_compose(ath_char_atom('4'),
                        ath_compose(ath_char_atom('z'), ath_NULL));
    ath_obj *bad_parse = ath_parse(bad_src, ath_NULL);
    assert(!ath_is_alive(bad_parse));

    /* Negative parse. */
    ath_obj *neg_src = ath_compose(ath_char_atom('-'),
                        ath_compose(ath_char_atom('7'), ath_NULL));
    ath_obj *neg_parsed = ath_parse(neg_src, ath_NULL);
    assert(ath_is_alive(neg_parsed));
    assert(neg_parsed->value == -7);

    /* --- Comparisons (SPEC §4.8.3) --- */

    ath_obj *five = ath_alloc_number(5);
    ath_obj *seven = ath_alloc_number(7);

    /* True verdict: alive, no payload. */
    ath_obj *v_lt = ath_lt(five, seven);
    assert(ath_is_alive(v_lt));
    assert(!v_lt->has_value);

    /* False verdict: dead. */
    ath_obj *v_lt_false = ath_lt(seven, five);
    assert(!ath_is_alive(v_lt_false));

    /* Equality */
    ath_obj *v_eq_true = ath_eq(five, ath_alloc_number(5));
    assert(ath_is_alive(v_eq_true));
    ath_obj *v_eq_false = ath_eq(five, seven);
    assert(!ath_is_alive(v_eq_false));

    /* Greater */
    ath_obj *v_gt_true = ath_gt(seven, five);
    assert(ath_is_alive(v_gt_true));
    ath_obj *v_gt_false = ath_gt(five, seven);
    assert(!ath_is_alive(v_gt_false));

    /* Self-comparison: LT and GT both false, EQ true. */
    assert(!ath_is_alive(ath_lt(five, five)));
    assert(!ath_is_alive(ath_gt(five, five)));
    assert(ath_is_alive(ath_eq(five, five)));

    /* Negative comparisons work. */
    ath_obj *neg = ath_alloc_number(-3);
    assert(ath_is_alive(ath_lt(neg, five)));
    assert(!ath_is_alive(ath_gt(neg, five)));

    /* Verdict lifetime inherits from operands — kill an operand and the
     * verdict dies on the next observation. */
    ath_obj *lhs = ath_alloc_number(1);
    ath_obj *rhs = ath_alloc_number(2);
    ath_obj *v_inh = ath_lt(lhs, rhs);
    assert(ath_is_alive(v_inh));
    ath_die(rhs);
    assert(!ath_is_alive(v_inh));

    /* No-payload operand → born dead verdict (compares like dead operand). */
    ath_obj *no_payload = ath_alloc_alive();
    assert(!ath_is_alive(ath_lt(no_payload, five)));
    assert(!ath_is_alive(ath_eq(five, no_payload)));
    assert(!ath_is_alive(ath_gt(no_payload, no_payload)));

    /* NULL operands → born dead. */
    assert(!ath_is_alive(ath_lt(ath_NULL, five)));
    assert(!ath_is_alive(ath_eq(five, ath_NULL)));

    /* Dead operand → born dead. */
    ath_obj *dead_five = ath_alloc_number(5);
    ath_die(dead_five);
    assert(!ath_is_alive(ath_lt(dead_five, seven)));

    /* --- String operations (SPEC §4.8.4) --- */

    /* Build "Hi" via cons. */
    ath_obj *s_hi = ath_compose(ath_char_atom('H'),
                    ath_compose(ath_char_atom('i'), ath_NULL));

    /* LENGTH */
    ath_obj *len_hi = ath_length(s_hi, ath_NULL);
    assert(ath_is_alive(len_hi));
    assert(len_hi->has_value && len_hi->value == 2);

    /* LENGTH(NULL) = 0 (empty string is real). */
    ath_obj *len_empty = ath_length(ath_NULL, ath_NULL);
    assert(ath_is_alive(len_empty));
    assert(len_empty->value == 0);

    /* INDEX */
    ath_obj *idx0 = ath_index(s_hi, ath_alloc_number(0));
    assert(ath_is_alive(idx0));
    assert(idx0 == ath_char_atom('H'));   /* atom identity */
    ath_obj *idx1 = ath_index(s_hi, ath_alloc_number(1));
    assert(idx1 == ath_char_atom('i'));

    /* INDEX out of range → dead. */
    ath_obj *idx_oob = ath_index(s_hi, ath_alloc_number(5));
    assert(!ath_is_alive(idx_oob));

    /* INDEX with negative index → dead. */
    ath_obj *idx_neg = ath_index(s_hi, ath_alloc_number(-1));
    assert(!ath_is_alive(idx_neg));

    /* INDEX with no-payload → dead. */
    ath_obj *idx_no_payload = ath_index(s_hi, ath_alloc_alive());
    assert(!ath_is_alive(idx_no_payload));

    /* CONCAT */
    ath_obj *world = ath_compose(ath_char_atom('!'), ath_NULL);
    ath_obj *hi_bang = ath_concat(s_hi, world);
    fputs("expect Hi!: ", stdout);
    ath_print_obj(hi_bang);
    assert(ath_length(hi_bang, ath_NULL)->value == 3);

    /* CONCAT with empty operand. */
    ath_obj *just_hi = ath_concat(s_hi, ath_NULL);
    fputs("expect Hi: ", stdout);
    ath_print_obj(just_hi);
    ath_obj *just_world = ath_concat(ath_NULL, world);
    fputs("expect !: ", stdout);
    ath_print_obj(just_world);

    /* CONCAT of two empties returns ath_NULL. */
    ath_obj *empty_cat = ath_concat(ath_NULL, ath_NULL);
    assert(empty_cat == ath_NULL);

    /* SLICE */
    /* "Hello" = H e l l o */
    ath_obj *hello = ath_compose(ath_char_atom('H'),
                     ath_compose(ath_char_atom('e'),
                     ath_compose(ath_char_atom('l'),
                     ath_compose(ath_char_atom('l'),
                     ath_compose(ath_char_atom('o'), ath_NULL)))));

    ath_obj *range_1_4 = ath_compose(ath_alloc_number(1), ath_alloc_number(4));
    ath_obj *ell = ath_slice(hello, range_1_4);
    fputs("expect ell: ", stdout);
    ath_print_obj(ell);
    assert(ath_length(ell, ath_NULL)->value == 3);

    /* Slice [0..5] should be the whole string. */
    ath_obj *range_0_5 = ath_compose(ath_alloc_number(0), ath_alloc_number(5));
    ath_obj *whole = ath_slice(hello, range_0_5);
    fputs("expect Hello: ", stdout);
    ath_print_obj(whole);

    /* Out-of-range slice → dead. */
    ath_obj *range_2_10 = ath_compose(ath_alloc_number(2), ath_alloc_number(10));
    ath_obj *oob = ath_slice(hello, range_2_10);
    assert(!ath_is_alive(oob));

    /* I > J → dead. */
    ath_obj *range_3_1 = ath_compose(ath_alloc_number(3), ath_alloc_number(1));
    ath_obj *bad_range = ath_slice(hello, range_3_1);
    assert(!ath_is_alive(bad_range));

    /* Empty slice [2..2] → dead by design. */
    ath_obj *range_2_2 = ath_compose(ath_alloc_number(2), ath_alloc_number(2));
    ath_obj *empty_slice = ath_slice(hello, range_2_2);
    assert(!ath_is_alive(empty_slice));

    /* Lifetime inheritance: kill source, derived string dies. */
    ath_obj *src = ath_compose(ath_char_atom('A'),
                   ath_compose(ath_char_atom('B'), ath_NULL));
    ath_obj *src_len = ath_length(src, ath_NULL);
    ath_obj *src_idx = ath_index(src, ath_alloc_number(0));
    /* Mark src dead by killing its own cell; deps on derived results
     * should propagate. */
    ath_die(src);
    assert(!ath_is_alive(src_len));
    assert(!ath_is_alive(src_idx));

    /* --- Clone (SPEC §4.4.18) --- */

    /* Number clone preserves payload. */
    ath_obj *orig = ath_alloc_number(99);
    ath_obj *copy = ath_clone(orig);
    assert(copy != orig);                  /* independent identity */
    assert(ath_is_alive(copy));
    assert(copy->has_value && copy->value == 99);

    /* Killing the clone does not kill the original. */
    ath_die(copy);
    assert(!ath_is_alive(copy));
    assert(ath_is_alive(orig));
    assert(orig->value == 99);

    /* Killing the original does not kill a clone made before the kill. */
    ath_obj *copy2 = ath_clone(orig);
    assert(ath_is_alive(copy2));
    ath_die(orig);
    assert(!ath_is_alive(orig));
    assert(ath_is_alive(copy2));

    /* Cloning a dead object yields a dead clone. */
    ath_obj *dead_clone = ath_clone(orig);
    assert(!ath_is_alive(dead_clone));

    /* Cloning NULL yields a dead, payload-less object. */
    ath_obj *null_clone = ath_clone(ath_NULL);
    assert(!ath_is_alive(null_clone));
    assert(!null_clone->has_value);

    /* Lifetime extensions are preserved. */
    ath_obj *oneshot = ath_alloc_oneshot();
    ath_obj *oneshot_copy = ath_clone(oneshot);
    /* Each oneshot has its own first-observation flip; they are independent. */
    assert(ath_is_alive(oneshot));
    assert(!ath_is_alive(oneshot));       /* original used up */
    assert(ath_is_alive(oneshot_copy));   /* clone still has its one observation */
    assert(!ath_is_alive(oneshot_copy));

    /* --- File I/O (SPEC §4.4.21-24, §4.7 ext 5) --- */

    /* Set up a fixture file. */
    const char *rpath = "/tmp/ath_test_read";
    unlink(rpath);
    {
        FILE *fp = fopen(rpath, "wb");
        assert(fp != NULL);
        fputs("hello\n", fp);
        fclose(fp);
    }

    /* read on existing file returns an alive, owning string. */
    ath_obj *fr = ath_alloc_read_file(rpath);
    assert(ath_is_alive(fr));
    assert(fr->owns_path);
    assert(fr->watch_path != NULL);
    assert(strcmp(fr->watch_path, rpath) == 0);

    /* PRINT2 the content to confirm it reads "hello". */
    fputs("expect hello: ", stdout);
    ath_print_obj(fr);

    /* read of nonexistent file is born dead. */
    ath_obj *rmiss = ath_alloc_read_file("/tmp/ath_test_definitely_missing_xyz");
    assert(!ath_is_alive(rmiss));

    /* Clone clears ownership. */
    ath_obj *rclone = ath_clone(fr);
    assert(ath_is_alive(rclone));
    assert(rclone->watch_path != NULL);
    assert(rclone->owns_path == 0);     /* clone never owns */

    /* Killing the clone does not delete the file. */
    ath_die(rclone);
    assert(access(rpath, F_OK) == 0);   /* file still there */

    /* close disowns + kills without deleting the file. */
    ath_close(fr);
    assert(!ath_is_alive(fr));
    assert(access(rpath, F_OK) == 0);   /* file persists after close */

    /* Read again, then explicit kill deletes the file. */
    ath_obj *fr2 = ath_alloc_read_file(rpath);
    assert(ath_is_alive(fr2));
    ath_die(fr2);
    assert(access(rpath, F_OK) != 0);   /* file is gone */

    /* write: create a file from a known string. */
    const char *wpath = "/tmp/ath_test_write";
    unlink(wpath);
    ath_obj *fsrc = ath_compose(ath_char_atom('h'),
                   ath_compose(ath_char_atom('i'),
                   ath_compose(ath_char_atom('\n'), ath_NULL)));
    ath_obj *wv = ath_write_file(fsrc, wpath);
    assert(ath_is_alive(wv));            /* verdict alive on success */
    {
        FILE *fp = fopen(wpath, "rb");
        assert(fp != NULL);
        char buf[16] = {0};
        size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
        fclose(fp);
        assert(n == 3);
        assert(memcmp(buf, "hi\n", 3) == 0);
    }

    /* write does NOT make verdict an owner: killing verdict does not
     * delete the file. */
    ath_die(wv);
    assert(access(wpath, F_OK) == 0);

    /* append adds more bytes. */
    ath_obj *more = ath_compose(ath_char_atom('!'),
                    ath_compose(ath_char_atom('\n'), ath_NULL));
    ath_obj *av = ath_append_file(more, wpath);
    assert(ath_is_alive(av));
    {
        FILE *fp = fopen(wpath, "rb");
        assert(fp != NULL);
        char buf[16] = {0};
        size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
        fclose(fp);
        assert(n == 5);
        assert(memcmp(buf, "hi\n!\n", 5) == 0);
    }

    /* write to a path in a nonexistent directory → dead verdict. */
    ath_obj *fail = ath_write_file(fsrc, "/tmp/no_such_dir_xyz/x");
    assert(!ath_is_alive(fail));

    /* read-after-delete: the watch_path observation flips alive to 0
     * but DOES NOT trigger another unlink (file is gone). */
    ath_obj *fr3 = ath_alloc_read_file(wpath);
    assert(ath_is_alive(fr3));
    unlink(wpath);
    assert(!ath_is_alive(fr3));            /* watch_path observed gone */
    /* And killing r3 now is a no-op (alive=0 prevents the unlink check). */
    ath_die(fr3);
    /* Nothing to assert — would have crashed if it tried to unlink a NULL. */

    /* Clone of a derived value does NOT inherit its deps. */
    ath_obj *a_num = ath_alloc_number(3);
    ath_obj *b_num = ath_alloc_number(4);
    ath_obj *sum_ab = ath_add(a_num, b_num);     /* sum has deps on a, b */
    ath_obj *sum_clone = ath_clone(sum_ab);      /* clone is independent */
    assert(ath_is_alive(sum_clone));
    assert(sum_clone->value == 7);
    /* Killing the original sum's operand kills sum_ab via deps, but the
     * clone is unaffected. */
    ath_die(a_num);
    assert(!ath_is_alive(sum_ab));               /* dep propagated */
    assert(ath_is_alive(sum_clone));             /* clone is its own snapshot */

    /* --- Time and randomness (SPEC §4.4.19, §4.4.20, §4.8.5) --- */

    /* NOW returns a fresh number-payload object. */
    ath_obj *t0 = ath_now(ath_NULL, ath_NULL);
    assert(ath_is_alive(t0));
    assert(t0->has_value && t0->value >= 0);

    /* NOW is monotonic — a later reading is >= an earlier one. */
    ath_obj *t1 = ath_now(ath_NULL, ath_NULL);
    assert(t1->value >= t0->value);

    /* sleep advances NOW by approximately the requested interval. */
    ath_obj *fifty = ath_alloc_number(50);  /* 50 ms */
    int64_t before = ath_now(ath_NULL, ath_NULL)->value;
    ath_sleep_ms(fifty);
    int64_t after = ath_now(ath_NULL, ath_NULL)->value;
    assert(after - before >= 40);  /* allow a bit of slack below */
    assert(after - before <= 200); /* and a bit of slack above */

    /* sleep on dead/no-payload is a no-op (no observable hang). */
    ath_sleep_ms(NULL);
    ath_sleep_ms(ath_NULL);
    ath_obj *dead_dur = ath_alloc_number(100);
    ath_die(dead_dur);
    ath_sleep_ms(dead_dur);     /* dead duration → no-op */
    ath_sleep_ms(ath_alloc_number(0));   /* zero → no-op */
    ath_sleep_ms(ath_alloc_number(-5));  /* negative → no-op */

    /* TIMER allocates an alive object that becomes dead after the deadline. */
    ath_obj *short_dur = ath_alloc_number(30);  /* 30 ms */
    ath_obj *timer = ath_alloc_timer_ms(short_dur);
    assert(ath_is_alive(timer));
    /* Killing the duration parameter does NOT affect the timer. */
    ath_die(short_dur);
    assert(ath_is_alive(timer));
    /* Wait past the deadline — timer becomes observably dead. */
    struct timespec wait_50 = {0, 50 * 1000 * 1000};
    nanosleep(&wait_50, NULL);
    assert(!ath_is_alive(timer));

    /* TIMER with bad input → born dead. */
    ath_obj *bad_timer = ath_alloc_timer_ms(ath_NULL);
    assert(!ath_is_alive(bad_timer));
    ath_obj *zero_timer = ath_alloc_timer_ms(ath_alloc_number(0));
    assert(!ath_is_alive(zero_timer));

    /* RANDOM returns a value in [LO, HI). Multiple draws should land in
     * range. */
    ath_obj *r_lo = ath_alloc_number(10);
    ath_obj *r_hi = ath_alloc_number(20);
    for (int i = 0; i < 100; i++) {
        ath_obj *r = ath_random_range(r_lo, r_hi);
        assert(ath_is_alive(r));
        assert(r->has_value);
        assert(r->value >= 10 && r->value < 20);
    }

    /* RANDOM with LO >= HI → dead. */
    ath_obj *bad_r1 = ath_random_range(ath_alloc_number(5), ath_alloc_number(5));
    assert(!ath_is_alive(bad_r1));
    ath_obj *bad_r2 = ath_random_range(ath_alloc_number(10), ath_alloc_number(3));
    assert(!ath_is_alive(bad_r2));

    /* RANDOM with dead operand → dead. */
    ath_obj *dead_lo = ath_alloc_number(0);
    ath_die(dead_lo);
    assert(!ath_is_alive(ath_random_range(dead_lo, r_hi)));

    /* RANDOM with no-payload → dead. */
    assert(!ath_is_alive(ath_random_range(ath_alloc_alive(), r_hi)));

    fputs("runtime test: all checks passed\n", stdout);
    return 0;
}
