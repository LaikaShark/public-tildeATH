#define _POSIX_C_SOURCE 200809L

#include "ath_runtime.h"

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
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

    /* ath_print_obj round-trip: build "Hi" and verify it prints "Hi\n". */
    ath_obj *str = ath_compose(
        ath_char_atom('H'),
        ath_compose(ath_char_atom('i'), ath_NULL));
    fputs("expect Hi: ", stdout);
    ath_print_obj(str);

    /* ath_print_obj of NULL emits just a newline. */
    fputs("expect blank: ", stdout);
    ath_print_obj(ath_NULL);

    /* ath_print_obj of a null pointer also emits just a newline (null-safety). */
    fputs("expect blank: ", stdout);
    ath_print_obj(NULL);

    /* ath_print_obj stops at the first unrecognized left half. */
    ath_obj *garbage = ath_compose(ath_char_atom('X'),
                                   ath_compose(ath_alloc_alive(), ath_NULL));
    fputs("expect X: ", stdout);
    ath_print_obj(garbage);

    /* ath_print_obj_raw is the newline-free variant used by `print $var`
     * interpolation: two raw writes share one line, the final \n is the
     * caller's. */
    fputs("expect HiHi: ", stdout);
    ath_print_obj_raw(str);
    ath_print_obj_raw(str);
    ath_print_bytes("\n", 1);

    /* A numeric payload renders as its decimal form, so `print $N` prints
     * the number (§4.4.6). */
    fputs("expect 42: ", stdout);
    ath_print_obj(ath_alloc_number(42));
    fputs("expect -7: ", stdout);
    ath_print_obj(ath_alloc_number(-7));
    fputs("expect 3.5: ", stdout);
    ath_print_obj(ath_alloc_float(3.5));

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
    assert(ath_has_value(n3) && n3->num_kind == ATH_NUM_INT && n3->num.i == 3);
    assert(ath_has_value(n4) && n4->num.i == 4);

    /* Float payload allocator stores a double under the FLOAT tag (§4.8). */
    ath_obj *f25 = ath_alloc_float(2.5);
    assert(ath_is_alive(f25));
    assert(ath_has_value(f25) && f25->num_kind == ATH_NUM_FLOAT && f25->num.f == 2.5);

    ath_obj *sum = ath_add(n3, n4);
    assert(ath_is_alive(sum));
    assert(ath_has_value(sum) && sum->num.i == 7);

    ath_obj *diff = ath_sub(n3, n4);
    assert(ath_is_alive(diff));
    assert(diff->num.i == -1);

    ath_obj *prod = ath_mul(n3, n4);
    assert(prod->num.i == 12);

    ath_obj *quot = ath_div(ath_alloc_number(20), ath_alloc_number(6));
    assert(quot->num.i == 3);

    ath_obj *rem = ath_mod(ath_alloc_number(20), ath_alloc_number(6));
    assert(rem->num.i == 2);

    /* Overflow → born dead. */
    ath_obj *big = ath_alloc_number(INT64_MAX);
    ath_obj *overflow = ath_add(big, ath_alloc_number(1));
    assert(!ath_is_alive(overflow));
    assert(!ath_has_value(overflow));

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
    assert(abc->num.i == 6);
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
    assert(ath_has_value(parsed) && parsed->num.i == 42);

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
    assert(neg_parsed->num.i == -7);

    /* --- Comparisons (SPEC §4.8.3) --- */

    ath_obj *five = ath_alloc_number(5);
    ath_obj *seven = ath_alloc_number(7);

    /* True verdict: alive, no payload. */
    ath_obj *v_lt = ath_lt(five, seven);
    assert(ath_is_alive(v_lt));
    assert(!ath_has_value(v_lt));

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
    assert(ath_has_value(len_hi) && len_hi->num.i == 2);

    /* LENGTH(NULL) = 0 (empty string is real). */
    ath_obj *len_empty = ath_length(ath_NULL, ath_NULL);
    assert(ath_is_alive(len_empty));
    assert(len_empty->num.i == 0);

    /* INDEX returns a fresh snapshot carrying the character's identity
     * (not the canonical atom pointer — see ath_index), so identity is
     * checked by character code, not by pointer. */
    ath_obj *idx0 = ath_index(s_hi, ath_alloc_number(0));
    assert(ath_is_alive(idx0));
    assert(idx0 != ath_char_atom('H'));            /* a distinct object */
    assert(idx0->is_char && idx0->char_code == 'H');
    ath_obj *idx1 = ath_index(s_hi, ath_alloc_number(1));
    assert(idx1->is_char && idx1->char_code == 'i');

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
    assert(ath_length(hi_bang, ath_NULL)->num.i == 3);

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
    assert(ath_length(ell, ath_NULL)->num.i == 3);

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
    /* ...but killing src must NOT poison the shared 'A' atom: src_idx is
     * a snapshot, so the canonical atom every other string uses stays
     * alive (regression for the ath_index atom-poisoning bug). */
    assert(ath_is_alive(ath_char_atom('A')));
    assert(src_idx != ath_char_atom('A'));

    /* --- Clone (SPEC §4.4.18) --- */

    /* Number clone preserves payload. */
    ath_obj *orig = ath_alloc_number(99);
    ath_obj *copy = ath_clone(orig);
    assert(copy != orig);                  /* independent identity */
    assert(ath_is_alive(copy));
    assert(ath_has_value(copy) && copy->num.i == 99);

    /* Killing the clone does not kill the original. */
    ath_die(copy);
    assert(!ath_is_alive(copy));
    assert(ath_is_alive(orig));
    assert(orig->num.i == 99);

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
    assert(!ath_has_value(null_clone));

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

    /* Print the content to confirm it reads "hello". */
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

    /* --- Search and replace (SPEC §4.8.4) --- */

    /* Build a haystack "hello world". */
    {
        const char *hay_str = "hello world";
        ath_obj *hay = ath_NULL;
        for (size_t i = strlen(hay_str); i > 0; i--) {
            hay = ath_compose(ath_char_atom((unsigned char)hay_str[i - 1]), hay);
        }
        ath_obj *needle = ath_compose(ath_char_atom('w'),
                          ath_compose(ath_char_atom('o'),
                          ath_compose(ath_char_atom('r'), ath_NULL)));

        /* FIND match → number at position 6. */
        ath_obj *idx = ath_find(hay, needle);
        assert(ath_is_alive(idx));
        assert(ath_has_value(idx) && idx->num.i == 6);

        /* FIND no-match → dead. */
        ath_obj *miss = ath_compose(ath_char_atom('z'), ath_NULL);
        ath_obj *idx_miss = ath_find(hay, miss);
        assert(!ath_is_alive(idx_miss));

        /* FIND with empty needle → 0 (empty is a prefix of everything). */
        ath_obj *idx_empty = ath_find(hay, ath_NULL);
        assert(ath_is_alive(idx_empty));
        assert(idx_empty->num.i == 0);

        /* FIND with empty haystack and non-empty needle → dead. */
        ath_obj *idx_e = ath_find(ath_NULL, needle);
        assert(!ath_is_alive(idx_e));

        /* FIND with dead operand → dead. */
        ath_obj *dead_hay = ath_compose(ath_char_atom('x'), ath_NULL);
        ath_die(dead_hay);
        assert(!ath_is_alive(ath_find(dead_hay, needle)));

        /* REPLACE first occurrence: "hello world" with "wor"→"WOR" gives
         * "hello WORld". */
        ath_obj *repl_str = ath_compose(ath_char_atom('W'),
                            ath_compose(ath_char_atom('O'),
                            ath_compose(ath_char_atom('R'), ath_NULL)));
        ath_obj *pair = ath_compose(needle, repl_str);
        ath_obj *rep = ath_replace(hay, pair);
        assert(ath_is_alive(rep));
        fputs("expect hello WORld: ", stdout);
        ath_print_obj(rep);

        /* REPLACE no-match → dead. */
        ath_obj *miss_pair = ath_compose(miss, repl_str);
        assert(!ath_is_alive(ath_replace(hay, miss_pair)));

        /* REPLACE empty needle → dead (per §4.8.4). */
        ath_obj *empty_pair = ath_compose(ath_NULL, repl_str);
        assert(!ath_is_alive(ath_replace(hay, empty_pair)));

        /* REPLACE_ALL: "abracadabra" with "a"→"A" gives "AbrAcAdAbrA". */
        const char *abra = "abracadabra";
        ath_obj *abr = ath_NULL;
        for (size_t i = strlen(abra); i > 0; i--) {
            abr = ath_compose(ath_char_atom((unsigned char)abra[i - 1]), abr);
        }
        ath_obj *aN = ath_compose(ath_char_atom('a'), ath_NULL);
        ath_obj *AR = ath_compose(ath_char_atom('A'), ath_NULL);
        ath_obj *all_pair = ath_compose(aN, AR);
        ath_obj *all_rep = ath_replace_all(abr, all_pair);
        assert(ath_is_alive(all_rep));
        fputs("expect AbrAcAdAbrA: ", stdout);
        ath_print_obj(all_rep);

        /* REPLACE_ALL no-match → dead. */
        assert(!ath_is_alive(ath_replace_all(abr, miss_pair)));

        /* REPLACE_ALL with empty replacement: deletes all 'a's. */
        ath_obj *empty_repl_pair = ath_compose(aN, ath_NULL);
        ath_obj *deleted = ath_replace_all(abr, empty_repl_pair);
        assert(ath_is_alive(deleted));
        fputs("expect brcdbr: ", stdout);
        ath_print_obj(deleted);

        /* REPLACE_ALL where replacement is longer than needle. */
        ath_obj *xy = ath_compose(ath_char_atom('X'),
                       ath_compose(ath_char_atom('Y'),
                       ath_compose(ath_char_atom('Z'), ath_NULL)));
        ath_obj *grow_pair = ath_compose(aN, xy);
        ath_obj *grown = ath_replace_all(abr, grow_pair);
        assert(ath_is_alive(grown));
        fputs("expect XYZbrXYZcXYZdXYZbrXYZ: ", stdout);
        ath_print_obj(grown);
    }

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
    assert(sum_clone->num.i == 7);
    /* Killing the original sum's operand kills sum_ab via deps, but the
     * clone is unaffected. */
    ath_die(a_num);
    assert(!ath_is_alive(sum_ab));               /* dep propagated */
    assert(ath_is_alive(sum_clone));             /* clone is its own snapshot */

    /* --- Time and randomness (SPEC §4.4.19, §4.4.20, §4.8.5) --- */

    /* NOW returns a fresh number-payload object. */
    ath_obj *t0 = ath_now(ath_NULL, ath_NULL);
    assert(ath_is_alive(t0));
    assert(ath_has_value(t0) && t0->num.i >= 0);

    /* NOW is monotonic — a later reading is >= an earlier one. */
    ath_obj *t1 = ath_now(ath_NULL, ath_NULL);
    assert(t1->num.i >= t0->num.i);

    /* sleep advances NOW by approximately the requested interval. */
    ath_obj *fifty = ath_alloc_number(50);  /* 50 ms */
    int64_t before = ath_now(ath_NULL, ath_NULL)->num.i;
    ath_sleep_ms(fifty);
    int64_t after = ath_now(ath_NULL, ath_NULL)->num.i;
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
        assert(ath_has_value(r));
        assert(r->num.i >= 10 && r->num.i < 20);
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

    /* --- String predicates, transforms, split/join (SPEC §4.8.4) ------ */
    {
        #define MKS(lit) ath_string_from_bytes(lit, sizeof(lit) - 1)

        /* STREQ: byte-identical → alive; empty == empty. */
        assert(ath_is_alive(ath_streq(MKS("hello"), MKS("hello"))));
        assert(!ath_is_alive(ath_streq(MKS("hello"), MKS("help"))));
        assert(!ath_is_alive(ath_streq(MKS("hi"), MKS("hit"))));   /* prefix, shorter */
        assert(ath_is_alive(ath_streq(ath_NULL, ath_NULL)));
        assert(!ath_is_alive(ath_streq(MKS("x"), ath_NULL)));

        /* STARTSWITH: empty prefix is always a prefix. */
        assert(ath_is_alive(ath_startswith(MKS("apple"), MKS("app"))));
        assert(!ath_is_alive(ath_startswith(MKS("apple"), MKS("ple"))));
        assert(ath_is_alive(ath_startswith(MKS("apple"), ath_NULL)));
        assert(ath_is_alive(ath_startswith(ath_NULL, ath_NULL)));
        assert(!ath_is_alive(ath_startswith(ath_NULL, MKS("x"))));

        /* ENDSWITH: empty suffix is always a suffix. */
        assert(ath_is_alive(ath_endswith(MKS("apple"), MKS("ple"))));
        assert(!ath_is_alive(ath_endswith(MKS("apple"), MKS("app"))));
        assert(ath_is_alive(ath_endswith(MKS("apple"), ath_NULL)));
        assert(!ath_is_alive(ath_endswith(ath_NULL, MKS("x"))));

        /* STRLT / STRGT: byte order; equal is neither. */
        assert(ath_is_alive(ath_strlt(MKS("abc"), MKS("abd"))));
        assert(ath_is_alive(ath_strlt(MKS("abc"), MKS("abcd"))));   /* prefix < longer */
        assert(!ath_is_alive(ath_strlt(MKS("abc"), MKS("abc"))));
        assert(!ath_is_alive(ath_strlt(MKS("abd"), MKS("abc"))));
        assert(ath_is_alive(ath_strgt(MKS("abd"), MKS("abc"))));
        assert(ath_is_alive(ath_strgt(MKS("abcd"), MKS("abc"))));
        assert(!ath_is_alive(ath_strgt(MKS("abc"), MKS("abc"))));

        /* Dead operand → dead verdict. */
        ath_obj *dead_s = MKS("z");
        ath_die(dead_s);
        assert(!ath_is_alive(ath_streq(dead_s, MKS("z"))));
        assert(!ath_is_alive(ath_strlt(dead_s, MKS("z"))));
        assert(!ath_is_alive(ath_endswith(dead_s, MKS("z"))));

        /* LOWER / UPPER: verify via STREQ against the expected result. */
        assert(ath_is_alive(ath_streq(ath_lower(MKS("HeLLo123"), ath_NULL), MKS("hello123"))));
        assert(ath_is_alive(ath_streq(ath_upper(MKS("HeLLo123"), ath_NULL), MKS("HELLO123"))));
        assert(ath_lower(ath_NULL, ath_NULL) == ath_NULL);   /* NULL in, NULL out */

        /* TRIM / LSTRIP / RSTRIP. Whitespace = space, tab, LF, CR. */
        assert(ath_is_alive(ath_streq(ath_trim(MKS("  hi \t\n"), ath_NULL), MKS("hi"))));
        assert(ath_is_alive(ath_streq(ath_lstrip(MKS("  hi  "), ath_NULL), MKS("hi  "))));
        assert(ath_is_alive(ath_streq(ath_rstrip(MKS("  hi  "), ath_NULL), MKS("  hi"))));
        assert(ath_trim(MKS("   "), ath_NULL) == ath_NULL);  /* all whitespace → empty */

        /* SPLIT: element count via LENGTH, element value via INDEX+STREQ. */
        ath_obj *parts = ath_split(MKS("a,b,c"), MKS(","));
        assert(ath_is_alive(parts));
        assert(ath_length(parts, ath_NULL)->num.i == 3);
        ath_obj *p0 = ath_index(parts, ath_alloc_number(0));
        assert(ath_is_alive(ath_streq(p0, MKS("a"))));
        ath_obj *trailing = ath_split(MKS("a,b,"), MKS(","));  /* trailing sep */
        assert(ath_length(trailing, ath_NULL)->num.i == 3);
        assert(!ath_is_alive(ath_split(MKS("abc"), ath_NULL)));  /* empty sep born dead */

        /* JOIN: inverse of SPLIT when sep is absent from elements. */
        assert(ath_is_alive(ath_streq(ath_join(parts, MKS(",")), MKS("a,b,c"))));
        assert(ath_is_alive(ath_streq(ath_join(parts, MKS("-")), MKS("a-b-c"))));
        assert(ath_join(ath_NULL, MKS(",")) == ath_NULL);  /* empty list → NULL */

        #undef MKS
    }

    /* --- Search/measure, construct, atom bridge (SPEC §4.8.4 group 2) -- */
    {
        #define MKS(lit) ath_string_from_bytes(lit, sizeof(lit) - 1)

        /* CONTAINS: present → alive; absent → dead; empty needle alive.
         * Uses 'q' for the absent needle, not 'z' — the in-flight block
         * above killed the canonical "z" string, which under interning
         * would make any later MKS("z") a dead operand. */
        assert(ath_is_alive(ath_contains(MKS("abracadabra"), MKS("cad"))));
        assert(!ath_is_alive(ath_contains(MKS("hello"), MKS("q"))));
        assert(ath_is_alive(ath_contains(MKS("hello"), ath_NULL)));     /* empty needle */
        assert(ath_is_alive(ath_contains(ath_NULL, ath_NULL)));
        assert(!ath_is_alive(ath_contains(ath_NULL, MKS("x"))));

        /* COUNT: non-overlapping; 0 matches is a live 0; empty needle dead. */
        assert(ath_count(MKS("abracadabra"), MKS("a"))->num.i == 5);
        assert(ath_count(MKS("aaaa"), MKS("aa"))->num.i == 2);          /* non-overlapping */
        ath_obj *zero = ath_count(MKS("hello"), MKS("q"));
        assert(ath_is_alive(zero) && zero->num.i == 0);
        assert(!ath_is_alive(ath_count(MKS("hello"), ath_NULL)));       /* empty needle */

        /* RFIND: last index; empty needle → len; absent → dead. */
        assert(ath_rfind(MKS("abracadabra"), MKS("a"))->num.i == 10);
        assert(ath_rfind(MKS("hello"), MKS("l"))->num.i == 3);
        assert(ath_rfind(MKS("hello"), ath_NULL)->num.i == 5);         /* empty → len */
        assert(!ath_is_alive(ath_rfind(MKS("hello"), MKS("q"))));       /* absent */

        /* REPEAT: S x N; N==0 → empty; N<0 / no payload → dead. */
        assert(ath_is_alive(ath_streq(ath_repeat(MKS("ab"), ath_alloc_number(3)), MKS("ababab"))));
        assert(ath_repeat(MKS("ab"), ath_alloc_number(0)) == ath_NULL);
        assert(!ath_is_alive(ath_repeat(MKS("ab"), ath_alloc_number(-1))));
        assert(!ath_is_alive(ath_repeat(MKS("ab"), ath_alloc_alive())));  /* no payload */

        /* REVERSE: characters reversed; NULL in, NULL out. */
        assert(ath_is_alive(ath_streq(ath_reverse(MKS("hello"), ath_NULL), MKS("olleh"))));
        assert(ath_reverse(ath_NULL, ath_NULL) == ath_NULL);

        /* PAD_LEFT / PAD_RIGHT: pad to width; no-op when already wide. */
        assert(ath_is_alive(ath_streq(ath_pad_left(MKS("ab"), ath_alloc_number(5)), MKS("   ab"))));
        assert(ath_is_alive(ath_streq(ath_pad_right(MKS("ab"), ath_alloc_number(5)), MKS("ab   "))));
        assert(ath_is_alive(ath_streq(ath_pad_left(MKS("hello"), ath_alloc_number(3)), MKS("hello"))));
        assert(!ath_is_alive(ath_pad_left(MKS("ab"), ath_alloc_number(-1))));

        /* ORD / CHR: round-trip the byte code of a character atom. */
        assert(ath_ord(ath_char_atom('Q'), ath_NULL)->num.i == 'Q');
        assert(ath_is_alive(ath_streq(ath_chr(ath_alloc_number('Q'), ath_NULL), MKS("Q"))));
        /* chr('q') → "q"; its first head is the atom; ord → 'q'. */
        ath_obj *q_str = ath_chr(ath_alloc_number('q'), ath_NULL);
        ath_obj *q_atom = ath_index(q_str, ath_alloc_number(0));
        assert(ath_ord(q_atom, ath_NULL)->num.i == 'q');
        assert(!ath_is_alive(ath_chr(ath_alloc_number(256), ath_NULL)));  /* out of range */
        assert(!ath_is_alive(ath_chr(ath_alloc_number(-1), ath_NULL)));
        assert(!ath_is_alive(ath_ord(MKS("ab"), ath_NULL)));  /* not a single atom */

        #undef MKS
    }

    /* --- Numeric second-wave builtins (SPEC §4.8.2 extensions) -------- */
    {
        #define N(v) ath_alloc_number(v)

        assert(ath_pow(N(2), N(10))->num.i == 1024);
        assert(ath_pow(N(2), N(0))->num.i == 1);
        assert(ath_pow(N(0), N(0))->num.i == 1);
        assert(ath_pow(N(7), N(1))->num.i == 7);
        assert(!ath_is_alive(ath_pow(N(2), N(-1))));        /* negative exponent */
        assert(!ath_is_alive(ath_pow(N(2), N(63))));        /* overflow */

        assert(ath_abs(N(-5), ath_NULL)->num.i == 5);
        assert(ath_abs(N(5), ath_NULL)->num.i == 5);
        assert(!ath_is_alive(ath_abs(N(INT64_MIN), ath_NULL)));
        assert(ath_neg(N(5), ath_NULL)->num.i == -5);
        assert(!ath_is_alive(ath_neg(N(INT64_MIN), ath_NULL)));

        assert(ath_min(N(3), N(7))->num.i == 3);
        assert(ath_max(N(3), N(7))->num.i == 7);

        assert(ath_gcd(N(12), N(18))->num.i == 6);
        assert(ath_gcd(N(0), N(0))->num.i == 0);
        assert(ath_gcd(N(-12), N(18))->num.i == 6);

        assert(ath_sign(N(-3), ath_NULL)->num.i == -1);
        assert(ath_sign(N(0), ath_NULL)->num.i == 0);
        assert(ath_sign(N(9), ath_NULL)->num.i == 1);

        assert(ath_band(N(12), N(18))->num.i == 0);
        assert(ath_bor(N(12), N(18))->num.i == 30);
        assert(ath_bxor(N(12), N(10))->num.i == 6);
        assert(ath_bnot(N(0), ath_NULL)->num.i == -1);
        assert(ath_shl(N(3), N(2))->num.i == 12);
        assert(ath_shr(N(12), N(2))->num.i == 3);
        assert(ath_shr(N(-8), N(1))->num.i == -4);          /* arithmetic shift */
        assert(!ath_is_alive(ath_shl(N(1), N(64))));        /* shift out of range */
        assert(!ath_is_alive(ath_shr(N(1), N(-1))));

        /* CLAMP with the (LO, HI) pair. */
        ath_obj *lohi = ath_compose(N(0), N(18));
        assert(ath_clamp(N(100), lohi)->num.i == 18);
        assert(ath_clamp(N(-5), lohi)->num.i == 0);
        assert(ath_clamp(N(9), lohi)->num.i == 9);
        assert(!ath_is_alive(ath_clamp(N(5), ath_compose(N(18), N(0)))));  /* lo > hi */

        /* Dead / no-payload operand → dead. */
        ath_obj *dn = N(1);
        ath_die(dn);
        assert(!ath_is_alive(ath_min(dn, N(2))));
        assert(!ath_is_alive(ath_abs(ath_alloc_alive(), ath_NULL)));

        #undef N
    }

    /* --- Float payloads and the numeric tower (SPEC §4.8.2) ----------- */
    {
        #define I(v) ath_alloc_number(v)
        #define F(v) ath_alloc_float(v)
        #define IS_F(o) ((o)->num_kind == ATH_NUM_FLOAT)

        /* Mixed int/float promotes to float; int/int stays int. */
        ath_obj *s1 = ath_add(I(2), F(1.5));
        assert(ath_is_alive(s1) && IS_F(s1) && s1->num.f == 3.5);
        ath_obj *s2 = ath_add(F(1.5), I(2));
        assert(IS_F(s2) && s2->num.f == 3.5);
        ath_obj *s3 = ath_add(I(2), I(3));
        assert(s3->num_kind == ATH_NUM_INT && s3->num.i == 5);
        assert(ath_mul(F(2.0), F(2.5))->num.f == 5.0);
        assert(ath_sub(F(1.0), I(3))->num.f == -2.0);

        /* div: float is TRUE division; mod is fmod. */
        assert(ath_div(I(7), F(2.0))->num.f == 3.5);
        assert(ath_div(I(7), I(2))->num.i == 3);            /* int trunc unchanged */
        assert(ath_mod(F(5.5), F(2.0))->num.f == 1.5);

        /* nan / inf are LIVE values (§4.8.2). */
        ath_obj *inf = ath_div(F(1.0), F(0.0));
        assert(ath_is_alive(inf) && IS_F(inf) && isinf(inf->num.f));
        ath_obj *nan_ = ath_mod(F(1.0), F(0.0));
        assert(ath_is_alive(nan_) && isnan(nan_->num.f));

        /* Comparisons promote across kinds: 2 == 2.0, 2.5 < 3. */
        assert(ath_is_alive(ath_eq(I(2), F(2.0))));
        assert(!ath_is_alive(ath_eq(I(2), F(2.5))));
        assert(ath_is_alive(ath_lt(F(2.5), I(3))));
        assert(ath_is_alive(ath_gt(I(3), F(2.5))));

        /* Tower unary/binary ops. */
        assert(ath_abs(F(-2.5), ath_NULL)->num.f == 2.5);
        assert(ath_neg(F(2.5), ath_NULL)->num.f == -2.5);
        assert(ath_min(F(1.5), I(2))->num.f == 1.5);
        assert(ath_max(F(1.5), I(2))->num.f == 2.0);
        assert(ath_sign(F(-9.0), ath_NULL)->num.f == -1.0);
        assert(ath_pow(F(9.0), F(0.5))->num.f == 3.0);
        ath_obj *cl = ath_clamp(F(5.5), ath_compose(F(0.0), F(2.0)));
        assert(IS_F(cl) && cl->num.f == 2.0);

        /* Integer-only ops born-die on a FLOAT operand. */
        assert(!ath_is_alive(ath_band(F(1.0), I(2))));
        assert(!ath_is_alive(ath_gcd(F(4.0), I(6))));
        assert(!ath_is_alive(ath_shl(F(1.0), I(2))));
        assert(!ath_is_alive(ath_bnot(F(1.0), ath_NULL)));

        /* List folds promote when any element is float. */
        ath_obj *mixed = ath_compose(I(1), ath_compose(F(2.5), ath_compose(I(3), ath_NULL)));
        ath_obj *ms = ath_sum(mixed, ath_NULL);
        assert(IS_F(ms) && ms->num.f == 6.5);
        ath_obj *allint = ath_compose(I(1), ath_compose(I(2), ath_NULL));
        assert(ath_sum(allint, ath_NULL)->num_kind == ATH_NUM_INT);  /* stays int */

        /* to_string: shortest round-trip, forced .0, nan/inf names. */
        #define SEQ(o, lit) \
            ath_is_alive(ath_streq((o), ath_string_from_bytes(lit, sizeof(lit) - 1)))
        assert(SEQ(ath_to_string(F(3.5), ath_NULL), "3.5"));
        assert(SEQ(ath_to_string(F(3.0), ath_NULL), "3.0"));   /* forced .0 */
        assert(SEQ(ath_to_string(I(42), ath_NULL), "42"));     /* int unchanged */
        assert(SEQ(ath_to_string(ath_div(F(1.0), F(0.0)), ath_NULL), "inf"));
        assert(SEQ(ath_to_string(ath_neg(ath_div(F(1.0), F(0.0)), ath_NULL), ath_NULL), "-inf"));
        assert(SEQ(ath_to_string(ath_mod(F(1.0), F(0.0)), ath_NULL), "nan"));
        /* repr-style fixed/scientific policy: ordinary magnitudes are fixed,
         * extremes are scientific. */
        assert(SEQ(ath_to_string(F(2500.0), ath_NULL), "2500.0"));
        assert(SEQ(ath_to_string(F(0.0001), ath_NULL), "0.0001"));
        assert(SEQ(ath_to_string(F(-2500.0), ath_NULL), "-2500.0"));
        assert(SEQ(ath_to_string(F(1e16), ath_NULL), "1e+16"));
        assert(SEQ(ath_to_string(F(1e-5), ath_NULL), "1e-05"));
        assert(SEQ(ath_to_string(F(0.0), ath_NULL), "0.0"));

        /* parse: float syntax → float, plain digits → int. */
        ath_obj *pf = ath_parse(ath_string_from_bytes("2.5", 3), ath_NULL);
        assert(IS_F(pf) && pf->num.f == 2.5);
        ath_obj *pintv = ath_parse(ath_string_from_bytes("42", 2), ath_NULL);
        assert(pintv->num_kind == ATH_NUM_INT && pintv->num.i == 42);

        /* conversions: int_to_float, float_to_int (trunc), floor/ceil/round. */
        assert(ath_int_to_float(I(5), ath_NULL)->num.f == 5.0);
        assert(ath_float_to_int(F(3.9), ath_NULL)->num.i == 3);
        assert(ath_float_to_int(F(-3.9), ath_NULL)->num.i == -3);
        assert(ath_floor(F(3.9), ath_NULL)->num.f == 3.0);
        assert(ath_ceil(F(3.1), ath_NULL)->num.f == 4.0);
        assert(ath_round(F(2.5), ath_NULL)->num.f == 3.0);
        assert(!ath_is_alive(ath_float_to_int(ath_div(F(1.0), F(0.0)), ath_NULL)));

        /* count_of floors a FLOAT toward an int64 count. */
        assert(ath_count_of(F(3.9)) == 3);
        assert(ath_count_of(F(-1.0)) == 0);

        /* Transcendentals: FLOAT result; INT operands promote. Use a small
         * tolerance for results libm need not round exactly. */
        #define NEAR(o, want) (IS_F(o) && fabs((o)->num.f - (want)) < 1e-9)
        assert(ath_sqrt(F(16.0), ath_NULL)->num.f == 4.0);   /* sqrt exact here */
        assert(NEAR(ath_sqrt(I(16), ath_NULL), 4.0));        /* INT promotes */
        assert(NEAR(ath_cbrt(F(27.0), ath_NULL), 3.0));
        assert(NEAR(ath_exp(F(0.0), ath_NULL), 1.0));
        assert(NEAR(ath_log(F(1.0), ath_NULL), 0.0));
        assert(NEAR(ath_log2(F(8.0), ath_NULL), 3.0));
        assert(NEAR(ath_log10(F(1000.0), ath_NULL), 3.0));
        assert(NEAR(ath_sin(F(0.0), ath_NULL), 0.0));
        assert(NEAR(ath_cos(F(0.0), ath_NULL), 1.0));
        assert(NEAR(ath_atan(F(0.0), ath_NULL), 0.0));
        assert(NEAR(ath_hypot(I(3), I(4)), 5.0));
        assert(NEAR(ath_atan2(F(0.0), F(1.0)), 0.0));
        #undef NEAR
        /* Out-of-domain inputs are live nan/inf, not born dead. */
        ath_obj *snan = ath_sqrt(F(-1.0), ath_NULL);
        assert(ath_is_alive(snan) && isnan(snan->num.f));
        ath_obj *lninf = ath_log(F(0.0), ath_NULL);
        assert(ath_is_alive(lninf) && isinf(lninf->num.f) && lninf->num.f < 0);
        /* Dead/absent operand → born dead. */
        ath_obj *dx = F(1.0); ath_die(dx);
        assert(!ath_is_alive(ath_sqrt(dx, ath_NULL)));
        #undef SEQ

        #undef I
        #undef F
        #undef IS_F
    }

    /* --- String polish builtins (SPEC §4.8.4 extensions) -------------- */
    {
        #define MKS(lit) ath_string_from_bytes(lit, sizeof(lit) - 1)

        /* COMPARE: -1/0/1 by byte order. */
        assert(ath_compare(MKS("abc"), MKS("abc"))->num.i == 0);
        assert(ath_compare(MKS("abc"), MKS("abd"))->num.i == -1);
        assert(ath_compare(MKS("abd"), MKS("abc"))->num.i == 1);
        assert(ath_compare(MKS("ab"), MKS("abc"))->num.i == -1);  /* prefix < longer */

        /* CHAR_AT: length-1 string; out-of-range / negative → dead. */
        assert(ath_is_alive(ath_streq(ath_char_at(MKS("hello"), ath_alloc_number(1)), MKS("e"))));
        assert(!ath_is_alive(ath_char_at(MKS("hello"), ath_alloc_number(5))));
        assert(!ath_is_alive(ath_char_at(MKS("hello"), ath_alloc_number(-1))));

        /* FIND_FROM: (NEEDLE, START) pair. */
        assert(ath_find_from(MKS("abracadabra"), ath_compose(MKS("a"), ath_alloc_number(1)))->num.i == 3);
        assert(ath_find_from(MKS("abracadabra"), ath_compose(MKS("a"), ath_alloc_number(0)))->num.i == 0);
        assert(!ath_is_alive(ath_find_from(MKS("hello"), ath_compose(MKS("q"), ath_alloc_number(0)))));  /* absent ('q' untouched) */
        assert(ath_find_from(MKS("hello"), ath_compose(ath_NULL, ath_alloc_number(2)))->num.i == 2);  /* empty needle */

        /* CAPITALIZE / TITLE. */
        assert(ath_is_alive(ath_streq(ath_capitalize(MKS("hELLO"), ath_NULL), MKS("Hello"))));
        assert(ath_capitalize(ath_NULL, ath_NULL) == ath_NULL);
        assert(ath_is_alive(ath_streq(ath_title(MKS("hello world"), ath_NULL), MKS("Hello World"))));

        /* STRIP_CHARS family. Uses '#' as the strip set — the canonical
         * "x" string is killed earlier in this run (dead_hay), which under
         * interning would make the result observe a dead dep operand. */
        assert(ath_is_alive(ath_streq(ath_strip_chars(MKS("##hi##"), MKS("#")), MKS("hi"))));
        assert(ath_is_alive(ath_streq(ath_lstrip_chars(MKS("##hi##"), MKS("#")), MKS("hi##"))));
        assert(ath_is_alive(ath_streq(ath_rstrip_chars(MKS("##hi##"), MKS("#")), MKS("##hi"))));
        assert(ath_is_alive(ath_streq(ath_strip_chars(MKS("hi"), ath_NULL), MKS("hi"))));  /* empty set */

        /* PAD_*_WITH: (WIDTH, FILL) pair; empty fill → dead; no-op if wide. */
        assert(ath_is_alive(ath_streq(ath_pad_left_with(MKS("ab"), ath_compose(ath_alloc_number(5), MKS("*"))), MKS("***ab"))));
        assert(ath_is_alive(ath_streq(ath_pad_right_with(MKS("ab"), ath_compose(ath_alloc_number(5), MKS("*"))), MKS("ab***"))));
        assert(ath_is_alive(ath_streq(ath_pad_left_with(MKS("hello"), ath_compose(ath_alloc_number(3), MKS("*"))), MKS("hello"))));
        assert(!ath_is_alive(ath_pad_left_with(MKS("ab"), ath_compose(ath_alloc_number(5), ath_NULL))));  /* empty fill */

        #undef MKS
    }

    /* --- Generic cons-list operations (SPEC §4.8.6) ------------------- */
    {
        #define N(v) ath_alloc_number(v)
        /* Build the number list [4, 3, 8]. */
        ath_obj *list = ath_compose(N(4), ath_compose(N(3), ath_compose(N(8), ath_NULL)));

        assert(ath_sum(list, ath_NULL)->num.i == 15);
        assert(ath_product(list, ath_NULL)->num.i == 96);
        assert(ath_maximum(list, ath_NULL)->num.i == 8);
        assert(ath_minimum(list, ath_NULL)->num.i == 3);

        /* Empty list: sum 0, product 1, extremum dead. */
        assert(ath_sum(ath_NULL, ath_NULL)->num.i == 0);
        assert(ath_product(ath_NULL, ath_NULL)->num.i == 1);
        assert(!ath_is_alive(ath_maximum(ath_NULL, ath_NULL)));

        /* A string is not a number list: its elements are char atoms. */
        assert(!ath_is_alive(ath_sum(ath_string_from_bytes("hi", 2), ath_NULL)));

        /* MEMBER by payload equality. */
        assert(ath_is_alive(ath_member(list, N(3))));
        assert(!ath_is_alive(ath_member(list, N(99))));
        assert(!ath_is_alive(ath_member(list, ath_alloc_alive())));  /* X no payload */

        /* TAKE / DROP build fresh sublists. */
        ath_obj *t2 = ath_take(list, N(2));            /* [4, 3] */
        assert(ath_length(t2, ath_NULL)->num.i == 2);
        assert(ath_sum(t2, ath_NULL)->num.i == 7);
        assert(ath_take(list, N(0)) == ath_NULL);
        assert(ath_length(ath_take(list, N(99)), ath_NULL)->num.i == 3);  /* N >= len → all */
        assert(!ath_is_alive(ath_take(list, N(-1))));

        ath_obj *d2 = ath_drop(list, N(2));            /* [8] */
        assert(ath_sum(d2, ath_NULL)->num.i == 8);
        assert(ath_length(d2, ath_NULL)->num.i == 1);
        assert(ath_drop(list, N(3)) == ath_NULL);      /* N >= len → empty */
        assert(ath_length(ath_drop(list, N(0)), ath_NULL)->num.i == 3);   /* whole copy */

        /* n-ary lifetime combinators ALL_OF / ANY_OF. */
        ath_obj *x = ath_alloc_alive();
        ath_obj *y = ath_alloc_alive();
        ath_obj *xy = ath_compose(x, ath_compose(y, ath_NULL));
        ath_obj *allv = ath_all_of(xy, ath_NULL);
        ath_obj *anyv = ath_any_of(xy, ath_NULL);
        assert(ath_is_alive(allv));
        assert(ath_is_alive(anyv));
        /* Empty list: ALL_OF vacuously alive, ANY_OF dead. */
        assert(ath_is_alive(ath_all_of(ath_NULL, ath_NULL)));
        assert(!ath_is_alive(ath_any_of(ath_NULL, ath_NULL)));
        /* Kill one element: deaths propagate through the fold. */
        ath_die(y);
        assert(!ath_is_alive(allv));   /* dep on y now dead */
        assert(ath_is_alive(anyv));    /* x still alive */
        ath_die(x);
        assert(!ath_is_alive(anyv));   /* both dead */

        #undef N
    }

    /* --- Extended watch sources: pid + mtime (SPEC §4.4.12) ----------- */
    {
        /* pid watch: a running child is alive; once it exits and is
         * reaped, the watcher dies. */
        pid_t child = fork();
        if (child == 0) {
            struct timespec ts = {5, 0};
            nanosleep(&ts, NULL);
            _exit(0);
        }
        assert(child > 0);
        ath_obj *pw = ath_alloc_watching_pid(ath_alloc_number(child));
        assert(ath_is_alive(pw));               /* child running */
        /* A clone inherits the pid watch (§4.4.18 "same intrinsic
         * mortality"), so it dies with the process too. */
        ath_obj *pw_clone = ath_clone(pw);
        assert(ath_is_alive(pw_clone));
        kill(child, SIGKILL);
        int status;
        waitpid(child, &status, 0);             /* reap (avoid zombie) */
        assert(!ath_is_alive(pw));              /* child gone */
        assert(!ath_is_alive(pw_clone));        /* clone inherited the watch */

        /* Born dead on a bad pid payload. */
        assert(!ath_is_alive(ath_alloc_watching_pid(ath_alloc_number(0))));
        assert(!ath_is_alive(ath_alloc_watching_pid(ath_alloc_alive())));  /* no payload */

        /* mtime watch: alive while unchanged, dead once mtime differs. */
        const char *mp = "/tmp/ath_test_mtime_target";
        unlink(mp);
        FILE *mf = fopen(mp, "w");
        assert(mf != NULL);
        fputs("x\n", mf);
        fclose(mf);
        ath_obj *mw = ath_alloc_watching_mtime(mp);
        assert(ath_is_alive(mw));               /* unchanged */
        ath_obj *mw_clone = ath_clone(mw);      /* inherits the mtime watch */
        assert(ath_is_alive(mw_clone));
        struct timeval tv[2];
        tv[0].tv_sec = 1000; tv[0].tv_usec = 0;
        tv[1].tv_sec = 1000; tv[1].tv_usec = 0;
        assert(utimes(mp, tv) == 0);            /* force a different mtime */
        assert(!ath_is_alive(mw));              /* changed → dead */
        assert(!ath_is_alive(mw_clone));        /* clone sees the change too */

        /* A watcher on a since-deleted file is dead too. */
        ath_obj *mw2 = ath_alloc_watching_mtime(mp);
        assert(ath_is_alive(mw2));
        unlink(mp);
        assert(!ath_is_alive(mw2));             /* gone → dead */

        /* Missing path → born dead. */
        assert(!ath_is_alive(ath_alloc_watching_mtime("/tmp/ath_no_such_file_xyz_q")));
    }

    fputs("runtime test: all checks passed\n", stdout);
    return 0;
}
