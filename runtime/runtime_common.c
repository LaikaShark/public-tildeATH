#define _POSIX_C_SOURCE 200809L

#include "ath_runtime.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

static ath_obj ath_NULL_storage = { .alive = 0 };
ath_obj *ath_NULL = &ath_NULL_storage;

static double ath_now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static __attribute__((constructor)) void ath_seed_rng(void) {
    const char *env = getenv("ATH_SEED");
    unsigned int seed;
    if (env && *env) {
        seed = (unsigned int)strtoul(env, NULL, 10);
    } else {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        seed = (unsigned int)(ts.tv_sec ^ ts.tv_nsec);
    }
    srand(seed);
}

ath_obj *ath_alloc_alive(void) {
    ath_obj *o = (ath_obj *)calloc(1, sizeof(ath_obj));
    if (!o) {
        fputs("ath: out of memory\n", stderr);
        exit(1);
    }
    o->alive = 1;
    o->deadline_s = 0.0;
    o->watch_path = NULL;
    return o;
}

/* ath_compose lives in compose_fresh.c or compose_intern.c. */

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
    /* §4.7 ext 5 / §4.7.1: if v owns its watched path and is still
     * alive, unlink the file before clearing the alive bit. Errors are
     * silently ignored — an externally-deleted file is fine. */
    if (v->alive && v->owns_path && v->watch_path) {
        unlink(v->watch_path);
    }
    v->alive = 0;
}

#define ATH_MAX_SIGNAL 64
static volatile sig_atomic_t ath_signal_received[ATH_MAX_SIGNAL];

/* Pure, non-mutating observation. Returns 1 iff `v` would be observed
 * alive *right now* — by checking the same conditions ath_is_alive
 * checks, but without flipping any alive bits and without consuming
 * one-shots. Recursive walks through deps use ath_observe themselves,
 * so a dep walk never has side effects.
 *
 * Used by ath_clone to snapshot observable liveness instead of the
 * possibly-stale raw alive bit, and by ath_is_alive to decompose the
 * decision-making half of liveness from the bit-flipping half. */
static int ath_observe(ath_obj *v) {
    if (v == NULL) return 0;
    if (!v->alive) return 0;
    if (v->deadline_s > 0.0 && ath_now_s() >= v->deadline_s) return 0;
    if (v->watch_path != NULL && access(v->watch_path, F_OK) != 0) return 0;
    if (v->awaiting_signal > 0 && v->awaiting_signal < ATH_MAX_SIGNAL
        && ath_signal_received[v->awaiting_signal]) return 0;
    if (v->dep_mode == ATH_DEP_OR) {
        int d1_present = (v->dep1 != NULL);
        int d2_present = (v->dep2 != NULL);
        if (d1_present || d2_present) {
            int d1_alive = d1_present && ath_observe(v->dep1);
            int d2_alive = d2_present && ath_observe(v->dep2);
            if (!d1_alive && !d2_alive) return 0;
            return 1;
        }
    } else {
        if (v->dep1 != NULL && !ath_observe(v->dep1)) return 0;
        if (v->dep2 != NULL && !ath_observe(v->dep2)) return 0;
    }
    return 1;
}

int ath_is_alive(ath_obj *v) {
    if (v == NULL) return 0;
    int alive = ath_observe(v);
    if (!alive) {
        /* Cache the dead-finding so future observations early-return. */
        if (v->alive) v->alive = 0;
        return 0;
    }
    /* One-shot: this single direct observation honors the alive verdict
     * and then flips the bit. Subsequent direct observations see alive=0
     * and short-circuit at the top of ath_observe. Dep walks recurse
     * through ath_observe (not ath_is_alive), so one-shots are *not*
     * consumed transitively — only the direct observation site fires
     * them. */
    if (v->is_oneshot) v->alive = 0;
    return 1;
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

ath_obj *ath_string_from_bytes(const char *bytes, size_t len) {
    if (bytes == NULL || len == 0) return ath_NULL;
    ath_obj *acc = ath_NULL;
    for (size_t i = len; i > 0; i--) {
        ath_obj *c = ath_char_atom((unsigned char)bytes[i - 1]);
        acc = ath_compose(c, acc);
    }
    return acc;
}

ath_obj *ath_coerce_string(ath_obj *v) {
    if (v == NULL || v == ath_NULL) return ath_NULL;
    /* Payload-bearing operands become their decimal representation. The
     * resulting string inherits v as a dep via ath_to_string. */
    if (v->has_value) return ath_to_string(v, NULL);
    /* Anything else — existing cons-lists, generic composites, char
     * atoms — is passed through unchanged. The text-statement codegen
     * will feed it to ath_concat alongside literal parts. */
    return v;
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

/* --- Lifetime library (SPEC §4.7, §5.3) ----------------------------------
 *
 * Each entry gives a range [min_s, max_s] in seconds. import-from-library
 * draws a uniform random sample in that range and uses it as the object's
 * lifetime. Entries span ~0 seconds to ~10^110 seconds (heat-death scales)
 * and a mix of low- and high-variance ranges. Match is case-insensitive
 * on the joined `import NAME... VAR;` metadata words.
 */

typedef struct {
    const char *name;
    double min_s;
    double max_s;
} ath_lifetime_entry;

static const ath_lifetime_entry ath_library[] = {
    /* Effectively zero lifetime. */
    {"instant",          0.0,            0.0},
    {"muzzle flash",     0.0005,         0.002},
    {"tick",             0.001,          0.01},
    {"flash",            0.05,           0.5},
    {"blink",            0.1,            0.4},

    /* Low-variance "exact unit" entries. */
    {"second",           1.0,            1.0},
    {"minute",           60.0,           60.0},
    {"hour",             3600.0,         3600.0},
    {"day",              86400.0,        86400.0},
    {"week",             604800.0,       604800.0},
    {"year",             31557600.0,     31557600.0},

    /* Short-lived natural phenomena. */
    {"spark",            0.1,            2.0},
    {"soap bubble",      2.0,            30.0},
    {"smoke ring",       5.0,            60.0},
    {"snowflake",        60.0,           600.0},
    {"ice cube",         900.0,          7200.0},

    /* Living things. */
    {"mayfly",           300.0,          86400.0},      /* 5 min - 1 day */
    {"fruit fly",        28800.0,        180000.0},     /* 8 - 50 h */
    {"fly",              86400.0,        259200.0},     /* 1 - 3 days */
    {"banana",           259200.0,       1209600.0},    /* 3 - 14 days */
    {"daisy",            43200.0,        604800.0},     /* 12 h - 7 days */
    {"rose",             604800.0,       2592000.0},    /* 7 - 30 days */
    {"moth",             604800.0,       2419200.0},    /* 7 - 28 days */
    {"mouse",            31536000.0,     94608000.0},   /* 1 - 3 years */
    {"goldfish",         94608000.0,     1262304000.0}, /* 3 - 40 years */
    {"dog",              315360000.0,    567648000.0},  /* 10 - 18 years */
    {"human",            1576800000.0,   3787344000.0}, /* 50 - 120 years */

    /* Geological / astronomical. */
    {"sequoia",          31536000000.0,   110376000000.0},   /* 1k - 3.5k yr */
    {"pyramid",          126144000000.0,  252288000000.0},   /* 4k - 8k yr */
    {"continent",        3.0e15,          3.0e16},           /* ~100M - 1B yr */
    {"star",             3.0e16,          3.2e17},           /* 1B - 10B yr */
    {"red dwarf",        3.0e17,          3.0e19},           /* 10B - 1T yr */
    {"galaxy",           3.0e18,          3.0e19},           /* 100B - 1T yr */
    {"black hole",       3.0e90,          3.0e100},          /* googol-ish yr */
    {"proton",           3.0e37,          3.0e41},           /* baryon decay */
    {"universe",         3.0e100,         3.0e110},          /* heat death */
    {"forever",          1.0e308,         1.0e308},          /* effectively inf */

    /* Explicit high-variance ranges. */
    {"lightning",        0.0001,          10.0},       /* 5 orders of mag */
    {"campaign",         0.0,             100.0},      /* arbitrary */
    {"experiment",       1.0,             1000000.0},  /* 6 orders */
    {"empire",           3.15e9,          3.15e13},    /* 100 - 1M years */

    /* Homestuck-flavored. */
    {"author",           2.52e9,          3.15e9},     /* 80 - 100 years */
    {"meson",            1e-8,            1e-7},       /* 10-100 ns */

    /* sentinel */
    {NULL, 0.0, 0.0}
};

/* --- User-defined library entries (SPEC §5.3) ---------------------------- */

#define ATH_MAX_USER_LIFETIMES 64
static ath_lifetime_entry ath_user_library[ATH_MAX_USER_LIFETIMES];
static int ath_user_library_count = 0;

void ath_register_lifetime(const char *name, double min_s, double max_s) {
    if (name == NULL) return;
    if (ath_user_library_count >= ATH_MAX_USER_LIFETIMES) {
        fputs("ath: too many --define-lifetime entries (max 64); ignoring\n",
              stderr);
        return;
    }
    ath_user_library[ath_user_library_count].name = name;
    ath_user_library[ath_user_library_count].min_s = min_s;
    ath_user_library[ath_user_library_count].max_s = max_s;
    ath_user_library_count++;
}

int ath_library_lookup(const char *name, double *min_out, double *max_out) {
    if (name == NULL) return 0;
    /* User-registered entries take precedence over built-ins. */
    for (int i = 0; i < ath_user_library_count; i++) {
        if (strcasecmp(ath_user_library[i].name, name) == 0) {
            if (min_out) *min_out = ath_user_library[i].min_s;
            if (max_out) *max_out = ath_user_library[i].max_s;
            return 1;
        }
    }
    for (const ath_lifetime_entry *e = ath_library; e->name; e++) {
        if (strcasecmp(e->name, name) == 0) {
            if (min_out) *min_out = e->min_s;
            if (max_out) *max_out = e->max_s;
            return 1;
        }
    }
    return 0;
}

ath_obj *ath_alloc_with_lifetime(double min_s, double max_s) {
    ath_obj *o = ath_alloc_alive();
    double sample = min_s;
    if (max_s > min_s) {
        double u = (double)rand() / (double)RAND_MAX;
        sample = min_s + u * (max_s - min_s);
    }
    if (sample <= 0.0) {
        o->alive = 0;
    } else {
        double deadline = ath_now_s() + sample;
        /* clamp to avoid double overflow weirdness on extreme inputs */
        if (!(deadline > 0.0) || deadline >= 1.0e308) {
            deadline = 1.0e308;
        }
        o->deadline_s = deadline;
    }
    return o;
}

ath_obj *ath_alloc_oneshot(void) {
    ath_obj *o = ath_alloc_alive();
    o->is_oneshot = 1;
    return o;
}

ath_obj *ath_alloc_from_library(const char *name) {
    if (name == NULL) return ath_alloc_alive();
    /* Special non-time-based library entries. */
    if (strcasecmp(name, "once") == 0) {
        return ath_alloc_oneshot();
    }
    double min_s, max_s;
    if (ath_library_lookup(name, &min_s, &max_s)) {
        return ath_alloc_with_lifetime(min_s, max_s);
    }
    return ath_alloc_alive();
}

/* --- Signal watching (SPEC §4.7) ----------------------------------------- */

typedef struct { const char *name; int signum; } ath_signal_entry;

static const ath_signal_entry ath_signal_names[] = {
    {"SIGHUP",  SIGHUP},
    {"SIGINT",  SIGINT},
    {"SIGQUIT", SIGQUIT},
    {"SIGUSR1", SIGUSR1},
    {"SIGUSR2", SIGUSR2},
    {"SIGPIPE", SIGPIPE},
    {"SIGALRM", SIGALRM},
    {"SIGTERM", SIGTERM},
    {"SIGCHLD", SIGCHLD},
    {NULL, 0}
};

static int ath_signal_lookup(const char *name) {
    if (name == NULL) return -1;
    for (const ath_signal_entry *e = ath_signal_names; e->name; e++) {
        if (strcasecmp(e->name, name) == 0) {
            return e->signum;
        }
    }
    return -1;
}

static void ath_signal_handler(int signum) {
    if (signum > 0 && signum < ATH_MAX_SIGNAL) {
        ath_signal_received[signum] = 1;
    }
}

static void ath_install_signal_watcher(int signum) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = ath_signal_handler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(signum, &sa, NULL);
}

ath_obj *ath_alloc_watching_signal(int signum) {
    ath_obj *o = ath_alloc_alive();
    if (signum <= 0 || signum >= ATH_MAX_SIGNAL) {
        o->alive = 0;
        return o;
    }
    ath_install_signal_watcher(signum);
    o->awaiting_signal = signum;
    if (ath_signal_received[signum]) {
        o->alive = 0;
    }
    return o;
}

ath_obj *ath_alloc_watching_signal_by_name(const char *name) {
    int signum = ath_signal_lookup(name);
    if (signum < 0) {
        if (name) {
            fprintf(stderr, "ath: unknown signal name '%s'; allocating born-dead\n", name);
        }
        ath_obj *o = ath_alloc_alive();
        o->alive = 0;
        return o;
    }
    return ath_alloc_watching_signal(signum);
}

ath_obj *ath_alloc_watching_file(const char *path) {
    ath_obj *o = ath_alloc_alive();
    if (path == NULL) {
        o->alive = 0;
        return o;
    }
    size_t len = strlen(path);
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        fputs("ath: out of memory\n", stderr);
        exit(1);
    }
    memcpy(copy, path, len + 1);
    o->watch_path = copy;
    if (access(copy, F_OK) != 0) {
        o->alive = 0;
    }
    return o;
}

/* --- Numeric payload and arithmetic (SPEC §4.8) ------------------------- */

ath_obj *ath_alloc_number(int64_t v) {
    ath_obj *o = ath_alloc_alive();
    o->has_value = 1;
    o->value = v;
    return o;
}

void ath_inherit_lifetime(ath_obj *result, ath_obj *a, ath_obj *b) {
    if (result == NULL || result == ath_NULL) return;
    /* Skip self-references and the immortal NULL — neither carries useful
     * dependency info. */
    if (a != NULL && a != ath_NULL && a != result) {
        result->dep1 = a;
    }
    if (b != NULL && b != ath_NULL && b != result) {
        result->dep2 = b;
    }
}

/* Born-dead result for failed arithmetic. has_value stays 0. */
static ath_obj *ath_alloc_dead_number(void) {
    ath_obj *o = (ath_obj *)calloc(1, sizeof(ath_obj));
    if (!o) {
        fputs("ath: out of memory\n", stderr);
        exit(1);
    }
    /* alive=0, has_value=0 are the calloc defaults. */
    return o;
}

/* Both operands must be (a) alive at call time and (b) carry a payload.
 * Returns 1 if usable, 0 if a born-dead result should be produced. */
static int ath_operands_usable(ath_obj *x, ath_obj *y) {
    if (x == NULL || !ath_is_alive(x) || !x->has_value) return 0;
    if (y == NULL || !ath_is_alive(y) || !y->has_value) return 0;
    return 1;
}

ath_obj *ath_add(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_alloc_dead_number();
    int64_t r;
    if (__builtin_add_overflow(x->value, y->value, &r)) {
        return ath_alloc_dead_number();
    }
    ath_obj *out = ath_alloc_number(r);
    ath_inherit_lifetime(out, x, y);
    return out;
}

ath_obj *ath_sub(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_alloc_dead_number();
    int64_t r;
    if (__builtin_sub_overflow(x->value, y->value, &r)) {
        return ath_alloc_dead_number();
    }
    ath_obj *out = ath_alloc_number(r);
    ath_inherit_lifetime(out, x, y);
    return out;
}

ath_obj *ath_mul(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_alloc_dead_number();
    int64_t r;
    if (__builtin_mul_overflow(x->value, y->value, &r)) {
        return ath_alloc_dead_number();
    }
    ath_obj *out = ath_alloc_number(r);
    ath_inherit_lifetime(out, x, y);
    return out;
}

ath_obj *ath_div(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_alloc_dead_number();
    if (y->value == 0) return ath_alloc_dead_number();
    /* INT64_MIN / -1 overflows two's-complement. */
    if (x->value == INT64_MIN && y->value == -1) return ath_alloc_dead_number();
    ath_obj *out = ath_alloc_number(x->value / y->value);
    ath_inherit_lifetime(out, x, y);
    return out;
}

ath_obj *ath_mod(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_alloc_dead_number();
    if (y->value == 0) return ath_alloc_dead_number();
    if (x->value == INT64_MIN && y->value == -1) return ath_alloc_dead_number();
    ath_obj *out = ath_alloc_number(x->value % y->value);
    ath_inherit_lifetime(out, x, y);
    return out;
}

ath_obj *ath_to_string(ath_obj *x, ath_obj *unused) {
    (void)unused;
    if (x == NULL || !ath_is_alive(x) || !x->has_value) {
        /* No payload → empty string. */
        return ath_NULL;
    }
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%lld", (long long)x->value);
    if (n <= 0) return ath_NULL;
    ath_obj *acc = ath_NULL;
    for (int i = n; i > 0; i--) {
        ath_obj *c = ath_char_atom((unsigned char)buf[i - 1]);
        acc = ath_compose(c, acc);
    }
    ath_inherit_lifetime(acc, x, NULL);
    return acc;
}

/* Walk a string-cons-list into a flat byte buffer. Returns -1 if the chain
 * contains a non-character atom (malformed string), else byte count. */
static int ath_string_to_buf(ath_obj *s, char *buf, size_t cap) {
    size_t n = 0;
    while (s != NULL && s != ath_NULL && ath_is_alive(s) && n + 1 < cap) {
        ath_obj *l, *r;
        ath_decompose(s, &l, &r);
        int ch = ath_atom_to_char(l);
        if (ch < 0) return -1;
        buf[n++] = (char)ch;
        s = r;
    }
    buf[n] = '\0';
    return (int)n;
}

/* --- Comparisons as verdicts (SPEC §4.8.3) ------------------------------ */

/* Alive verdict with deps installed. */
static ath_obj *ath_verdict_true(ath_obj *x, ath_obj *y) {
    ath_obj *v = ath_alloc_alive();
    ath_inherit_lifetime(v, x, y);
    return v;
}

/* Dead-on-arrival verdict; no deps recorded. */
static ath_obj *ath_verdict_false(void) {
    ath_obj *v = (ath_obj *)calloc(1, sizeof(ath_obj));
    if (!v) {
        fputs("ath: out of memory\n", stderr);
        exit(1);
    }
    return v;
}

ath_obj *ath_lt(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_verdict_false();
    return x->value < y->value ? ath_verdict_true(x, y) : ath_verdict_false();
}

ath_obj *ath_eq(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_verdict_false();
    return x->value == y->value ? ath_verdict_true(x, y) : ath_verdict_false();
}

ath_obj *ath_gt(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_verdict_false();
    return x->value > y->value ? ath_verdict_true(x, y) : ath_verdict_false();
}

ath_obj *ath_le(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_verdict_false();
    return x->value <= y->value ? ath_verdict_true(x, y) : ath_verdict_false();
}

ath_obj *ath_ge(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_verdict_false();
    return x->value >= y->value ? ath_verdict_true(x, y) : ath_verdict_false();
}

ath_obj *ath_ne(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_verdict_false();
    return x->value != y->value ? ath_verdict_true(x, y) : ath_verdict_false();
}

/* --- Logical combinators over verdicts (SPEC §4.8.3) ------------------- */

/* AND: alive iff both operands alive at every observation. Uses the
 * default conjunctive dep machinery. Born dead if either operand is
 * already dead at call (the first ath_is_alive walk catches this, but
 * we shortcut to keep the born-dead invariant uniform with the
 * comparison primitives). */
ath_obj *ath_and(ath_obj *x, ath_obj *y) {
    int x_alive = (x != NULL) && ath_is_alive(x);
    int y_alive = (y != NULL) && ath_is_alive(y);
    if (!x_alive || !y_alive) return ath_verdict_false();
    ath_obj *v = ath_alloc_alive();
    ath_inherit_lifetime(v, x, y);
    return v;
}

/* ENTANGLE: compose two objects into a fresh composite, then install
 * both as deps on the result (§4.8.4). Equivalent to BIFURCATE
 * composition followed by ath_inherit_lifetime, in one call. Used by
 * the compose-pair pattern (notably (needle, replacement) for REPLACE)
 * to propagate operand death into the carrier. The composite's halves
 * are L and R, matching plain compose; the only added effect is the
 * dep installation. */
ath_obj *ath_entangle(ath_obj *x, ath_obj *y) {
    ath_obj *p = ath_compose(x, y);
    ath_inherit_lifetime(p, x, y);
    return p;
}

/* OR: alive iff at least one operand alive at every observation. Sets
 * dep_mode=ATH_DEP_OR so ath_is_alive walks both deps disjunctively.
 * Born dead only if both operands are already dead at call. */
ath_obj *ath_or(ath_obj *x, ath_obj *y) {
    int x_alive = (x != NULL) && ath_is_alive(x);
    int y_alive = (y != NULL) && ath_is_alive(y);
    if (!x_alive && !y_alive) return ath_verdict_false();
    ath_obj *v = ath_alloc_alive();
    /* Install both deps unconditionally so the OR walk sees every input,
     * even one that's already dead at call time (so a dynamic revival of
     * "the other is alive" still works). Skip self and NULL per the
     * ath_inherit_lifetime convention. */
    if (x != NULL && x != ath_NULL && x != v) v->dep1 = x;
    if (y != NULL && y != ath_NULL && y != v) v->dep2 = y;
    v->dep_mode = ATH_DEP_OR;
    return v;
}

ath_obj *ath_parse(ath_obj *s, ath_obj *unused) {
    (void)unused;
    if (s == NULL || !ath_is_alive(s)) return ath_alloc_dead_number();
    char buf[64];
    int n = ath_string_to_buf(s, buf, sizeof(buf));
    if (n <= 0) return ath_alloc_dead_number();
    char *end;
    errno = 0;
    long long v = strtoll(buf, &end, 10);
    if (end == buf || *end != '\0') return ath_alloc_dead_number();
    if (errno == ERANGE) return ath_alloc_dead_number();
    ath_obj *out = ath_alloc_number((int64_t)v);
    ath_inherit_lifetime(out, s, NULL);
    return out;
}

/* --- String operations (SPEC §4.8.4) ----------------------------------- */

/* Born-dead generic object (no payload, no deps). */
static ath_obj *ath_alloc_dead(void) {
    ath_obj *o = (ath_obj *)calloc(1, sizeof(ath_obj));
    if (!o) {
        fputs("ath: out of memory\n", stderr);
        exit(1);
    }
    return o;
}

/* Walk the right-spine counting elements. Stops at NULL, ath_NULL,
 * or a dead cell. Returns the count. */
static int64_t ath_spine_length(ath_obj *s) {
    int64_t n = 0;
    while (s != NULL && s != ath_NULL && ath_is_alive(s)) {
        ath_obj *l, *r;
        ath_decompose(s, &l, &r);
        (void)l;
        n++;
        s = r;
    }
    return n;
}

ath_obj *ath_length(ath_obj *s, ath_obj *unused) {
    (void)unused;
    /* §4.8.4: LENGTH(NULL) = 0 (empty string is real). Only fails on a
     * dead non-NULL cell, which we treat as "walk stops, count so far". */
    int64_t n = ath_spine_length(s);
    ath_obj *out = ath_alloc_number(n);
    ath_inherit_lifetime(out, s, NULL);
    return out;
}

/* Walk the right-spine collecting heads into an array. Returns the number
 * of elements actually collected (≤ cap). On a dead/NULL terminator before
 * reaching cap, fewer elements are collected. */
static int64_t ath_collect_spine(ath_obj *s, ath_obj **heads, int64_t cap) {
    int64_t n = 0;
    while (s != NULL && s != ath_NULL && ath_is_alive(s) && n < cap) {
        ath_obj *l, *r;
        ath_decompose(s, &l, &r);
        heads[n++] = l;
        s = r;
    }
    return n;
}

/* Allocate a fresh, non-interned cons cell. Unlike ath_compose, which
 * hash-conses in intern mode, this always yields a unique object. A
 * string *result* built from these cells is owned solely by the op
 * that produced it, so installing its operands as deps
 * (ath_inherit_lifetime) can never overwrite an interned input's deps
 * or form a dep cycle with one. This is what SPEC §4.8.4 means by
 * "concatenated, sliced, and replaced results allocate fresh cons
 * cells" — the property must hold for split/join and the transforms
 * too, else `join(split(s))` would alias `s` under interning and
 * back-edge the dependency graph. Char atoms stay canonical. */
static ath_obj *ath_cons_fresh(ath_obj *head, ath_obj *tail) {
    ath_obj *cell = ath_alloc_alive();
    cell->left = head;
    cell->right = tail;
    return cell;
}

/* Build a fresh right-nested cons-list terminated with ath_NULL from an
 * array of element heads. Returns ath_NULL on empty input. */
static ath_obj *ath_build_spine(ath_obj **heads, int64_t n) {
    ath_obj *acc = ath_NULL;
    for (int64_t i = n; i > 0; i--) {
        acc = ath_cons_fresh(heads[i - 1], acc);
    }
    return acc;
}

ath_obj *ath_concat(ath_obj *a, ath_obj *b) {
    /* NULL/ath_NULL operands are the empty string (alive). A non-NULL
     * dead operand is a real failure. */
    int a_empty = (a == NULL || a == ath_NULL);
    int b_empty = (b == NULL || b == ath_NULL);
    if (!a_empty && !ath_is_alive(a)) return ath_alloc_dead();
    if (!b_empty && !ath_is_alive(b)) return ath_alloc_dead();

    int64_t na = a_empty ? 0 : ath_spine_length(a);
    int64_t nb = b_empty ? 0 : ath_spine_length(b);
    int64_t total = na + nb;
    if (total < 0) return ath_alloc_dead();  /* overflow guard */

    if (total == 0) return ath_NULL;

    ath_obj **buf = (ath_obj **)calloc((size_t)total, sizeof(ath_obj *));
    if (!buf) {
        fputs("ath: out of memory\n", stderr);
        exit(1);
    }
    int64_t actual = 0;
    if (na > 0) actual += ath_collect_spine(a, buf + actual, na);
    if (nb > 0) actual += ath_collect_spine(b, buf + actual, nb);
    ath_obj *out = ath_build_spine(buf, actual);
    free(buf);
    ath_inherit_lifetime(out, a, b);
    return out;
}

ath_obj *ath_index(ath_obj *s, ath_obj *n) {
    if (s == NULL || !ath_is_alive(s)) return ath_alloc_dead();
    if (n == NULL || !ath_is_alive(n) || !n->has_value) return ath_alloc_dead();
    if (n->value < 0) return ath_alloc_dead();
    int64_t target = n->value;
    int64_t i = 0;
    ath_obj *cur = s;
    while (cur != NULL && cur != ath_NULL && ath_is_alive(cur)) {
        ath_obj *l, *r;
        ath_decompose(cur, &l, &r);
        if (i == target) {
            ath_inherit_lifetime(l, s, n);
            return l;
        }
        i++;
        cur = r;
    }
    return ath_alloc_dead();  /* out of range */
}

ath_obj *ath_slice(ath_obj *s, ath_obj *range) {
    if (s == NULL || !ath_is_alive(s)) return ath_alloc_dead();
    if (range == NULL || range == ath_NULL || !ath_is_alive(range))
        return ath_alloc_dead();
    ath_obj *i_obj, *j_obj;
    ath_decompose(range, &i_obj, &j_obj);
    if (i_obj == NULL || !ath_is_alive(i_obj) || !i_obj->has_value)
        return ath_alloc_dead();
    if (j_obj == NULL || !ath_is_alive(j_obj) || !j_obj->has_value)
        return ath_alloc_dead();
    int64_t i = i_obj->value;
    int64_t j = j_obj->value;
    if (i < 0 || j < 0 || i > j) return ath_alloc_dead();

    /* Walk to position i. */
    ath_obj *cur = s;
    for (int64_t k = 0; k < i; k++) {
        if (cur == NULL || cur == ath_NULL || !ath_is_alive(cur))
            return ath_alloc_dead();
        ath_obj *l, *r;
        ath_decompose(cur, &l, &r);
        (void)l;
        cur = r;
    }
    /* Collect j - i elements. */
    int64_t want = j - i;
    if (want == 0) {
        /* §4.8.4: empty slice (i == j) is dead, by design. */
        return ath_alloc_dead();
    }
    ath_obj **buf = (ath_obj **)calloc((size_t)want, sizeof(ath_obj *));
    if (!buf) {
        fputs("ath: out of memory\n", stderr);
        exit(1);
    }
    int64_t collected = 0;
    while (collected < want && cur != NULL && cur != ath_NULL && ath_is_alive(cur)) {
        ath_obj *l, *r;
        ath_decompose(cur, &l, &r);
        buf[collected++] = l;
        cur = r;
    }
    if (collected < want) {
        /* walk hit the end early — out of range */
        free(buf);
        return ath_alloc_dead();
    }
    ath_obj *out = ath_build_spine(buf, collected);
    free(buf);
    ath_inherit_lifetime(out, s, range);
    return out;
}

/* --- Shallow clone (SPEC §4.4.18) --------------------------------------- */

ath_obj *ath_clone(ath_obj *v) {
    ath_obj *w = (ath_obj *)calloc(1, sizeof(ath_obj));
    if (!w) {
        fputs("ath: out of memory\n", stderr);
        exit(1);
    }
    if (v == NULL || v == ath_NULL) {
        /* Cloning NULL yields a born-dead object. calloc already gave us
         * alive=0 and no payload; just return it. */
        return w;
    }
    /* Snapshot: copy every observable field. The alive bit is set from
     * ath_observe(v) — a non-mutating refresh — so the clone reflects v's
     * *currently observable* liveness rather than the possibly-stale raw
     * bit. This matters for objects whose upstream operands have changed
     * since v was last directly observed (notably OR-mode verdicts, whose
     * dep1/dep2 are deliberately not copied below). One-shots are *not*
     * consumed by this refresh — ath_observe never trips is_oneshot. */
    w->alive = ath_observe(v);
    w->left = v->left;
    w->right = v->right;
    w->deadline_s = v->deadline_s;
    w->watch_path = v->watch_path;
    w->is_oneshot = v->is_oneshot;
    w->awaiting_signal = v->awaiting_signal;
    w->has_value = v->has_value;
    w->value = v->value;
    w->dep_mode = v->dep_mode;
    /* dep1, dep2, owns_path stay zeroed by calloc. The clone is never
     * an owner — §4.7 ext 5. With dep_mode copied but no deps installed,
     * an OR-mode clone degenerates to trusting its captured alive bit;
     * see SPEC §4.4.18. */
    return w;
}

/* --- Time, sleep, randomness (SPEC §4.4.19, §4.4.20, §4.8.5) ----------- */

static int64_t ath_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
}

void ath_sleep_ms(ath_obj *n) {
    if (n == NULL || !ath_is_alive(n) || !n->has_value) return;
    if (n->value <= 0) return;
    struct timespec ts;
    ts.tv_sec = (time_t)(n->value / 1000);
    ts.tv_nsec = (long)((n->value % 1000) * 1000000);
    nanosleep(&ts, NULL);
}

ath_obj *ath_alloc_timer_ms(ath_obj *n) {
    /* Bad duration → born dead. */
    if (n == NULL || !ath_is_alive(n) || !n->has_value || n->value <= 0) {
        ath_obj *dead = (ath_obj *)calloc(1, sizeof(ath_obj));
        if (!dead) { fputs("ath: out of memory\n", stderr); exit(1); }
        return dead;
    }
    ath_obj *o = ath_alloc_alive();
    double deadline = ath_now_s() + (double)n->value / 1000.0;
    if (!(deadline > 0.0) || deadline >= 1.0e308) deadline = 1.0e308;
    o->deadline_s = deadline;
    return o;
}

ath_obj *ath_now(ath_obj *a, ath_obj *b) {
    (void)a; (void)b;
    return ath_alloc_number(ath_now_ms());
}

ath_obj *ath_random_range(ath_obj *lo, ath_obj *hi) {
    if (lo == NULL || !ath_is_alive(lo) || !lo->has_value) goto dead;
    if (hi == NULL || !ath_is_alive(hi) || !hi->has_value) goto dead;
    if (lo->value >= hi->value) goto dead;

    /* Combine two rand() calls for ~62 bits of entropy, more than enough
     * for any practical span. Modulo bias is negligible for spans well
     * below 2^62. */
    uint64_t r = ((uint64_t)(rand() & 0x7fffffff) << 31)
               | (uint64_t)(rand() & 0x7fffffff);
    uint64_t span = (uint64_t)(hi->value - lo->value);
    int64_t result = lo->value + (int64_t)(r % span);
    return ath_alloc_number(result);

dead: {
        ath_obj *d = (ath_obj *)calloc(1, sizeof(ath_obj));
        if (!d) { fputs("ath: out of memory\n", stderr); exit(1); }
        return d;
    }
}

/* --- File I/O (SPEC §4.4.21-24, §4.7 ext 5) ----------------------------- */

/* Born-dead generic object — used as the "failed read/write/etc." return. */
static ath_obj *ath_alloc_dead_obj(void) {
    ath_obj *o = (ath_obj *)calloc(1, sizeof(ath_obj));
    if (!o) {
        fputs("ath: out of memory\n", stderr);
        exit(1);
    }
    return o;
}

ath_obj *ath_alloc_read_file(const char *path) {
    if (path == NULL) return ath_alloc_dead_obj();
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) return ath_alloc_dead_obj();

    /* Slurp the whole file. */
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return ath_alloc_dead_obj(); }
    long sz = ftell(fp);
    if (sz < 0) { fclose(fp); return ath_alloc_dead_obj(); }
    rewind(fp);
    char *buf = NULL;
    if (sz > 0) {
        buf = (char *)malloc((size_t)sz);
        if (!buf) { fclose(fp); fputs("ath: out of memory\n", stderr); exit(1); }
        size_t got = fread(buf, 1, (size_t)sz, fp);
        if (got != (size_t)sz) { free(buf); fclose(fp); return ath_alloc_dead_obj(); }
    }
    fclose(fp);

    /* Build the cons-list right-to-left. The tail uses the regular
     * compose path (which is intern-mode-aware). */
    ath_obj *tail = ath_NULL;
    for (long i = sz; i > 1; i--) {
        ath_obj *c = ath_char_atom((unsigned char)buf[i - 1]);
        tail = ath_compose(c, tail);
    }

    /* Allocate the head as a fresh non-interned wrapper carrying the
     * file ownership. Using ath_alloc_alive and setting left/right
     * explicitly bypasses ath_compose's hash-cons in intern mode, so
     * the head pointer is unique per call. */
    ath_obj *head = ath_alloc_alive();
    if (sz > 0) {
        head->left = ath_char_atom((unsigned char)buf[0]);
        head->right = tail;
    } else {
        /* Empty file: head represents an empty string but still owns
         * the path. left=right=NULL keeps existing lazy-half semantics. */
    }

    /* Strdup the path so the caller can free its argument if it likes. */
    size_t plen = strlen(path);
    char *pcopy = (char *)malloc(plen + 1);
    if (!pcopy) { free(buf); fputs("ath: out of memory\n", stderr); exit(1); }
    memcpy(pcopy, path, plen + 1);
    head->watch_path = pcopy;
    head->owns_path = 1;

    free(buf);
    return head;
}

/* Internal: walk s as a string per §4.6, writing each char atom byte
 * via fputc into fp. Returns 0 on success, -1 on write failure or
 * malformed string termination. */
static int ath_emit_string(ath_obj *s, FILE *fp) {
    while (s != NULL && s != ath_NULL && ath_is_alive(s)) {
        ath_obj *l, *r;
        ath_decompose(s, &l, &r);
        int ch = ath_atom_to_char(l);
        if (ch < 0) return -1;
        if (fputc(ch, fp) == EOF) return -1;
        s = r;
    }
    return 0;
}

static ath_obj *ath_write_file_mode(ath_obj *s, const char *path,
                                     const char *mode) {
    if (path == NULL) return ath_alloc_dead_obj();
    FILE *fp = fopen(path, mode);
    if (fp == NULL) return ath_alloc_dead_obj();
    int rc = ath_emit_string(s, fp);
    if (fclose(fp) != 0) return ath_alloc_dead_obj();
    if (rc != 0) return ath_alloc_dead_obj();
    return ath_alloc_alive();
}

ath_obj *ath_write_file(ath_obj *s, const char *path) {
    return ath_write_file_mode(s, path, "wb");
}

ath_obj *ath_append_file(ath_obj *s, const char *path) {
    return ath_write_file_mode(s, path, "ab");
}

void ath_close(ath_obj *v) {
    if (v == NULL || v == ath_NULL) return;
    /* §4.4.24: disown the file (so ath_die won't unlink) before killing. */
    v->owns_path = 0;
    v->alive = 0;
}

/* --- Search and replace (SPEC §4.8.4) ----------------------------------- */

/* Slurp a string cons-list into a heap-allocated NUL-terminated buffer.
 * Returns 0 on success and fills *out_buf and *out_len. The caller must
 * free *out_buf. Returns -1 on malformed string (non-character atom) or
 * an allocation failure; *out_buf is unset in that case. NULL or dead s
 * yields a successful empty buffer. */
static int ath_string_slurp(ath_obj *s, char **out_buf, size_t *out_len) {
    int64_t len = ath_spine_length(s);
    if (len < 0) return -1;
    size_t cap = (size_t)len + 1;
    char *buf = (char *)malloc(cap);
    if (!buf) return -1;
    int n = ath_string_to_buf(s, buf, cap);
    if (n < 0) {
        free(buf);
        return -1;
    }
    *out_buf = buf;
    *out_len = (size_t)n;
    return 0;
}

/* Build a fresh right-nested cons-list from a byte buffer. NULL on empty. */
static ath_obj *ath_buf_to_string(const char *buf, size_t n) {
    ath_obj *acc = ath_NULL;
    for (size_t i = n; i > 0; i--) {
        ath_obj *c = ath_char_atom((unsigned char)buf[i - 1]);
        acc = ath_cons_fresh(c, acc);
    }
    return acc;
}

ath_obj *ath_find(ath_obj *hay, ath_obj *needle) {
    /* NULL hay/needle are treated as empty strings (alive); non-NULL
     * dead operands are real failures. */
    int hay_empty = (hay == NULL || hay == ath_NULL);
    int needle_empty = (needle == NULL || needle == ath_NULL);
    if (!hay_empty && !ath_is_alive(hay)) return ath_alloc_dead_number();
    if (!needle_empty && !ath_is_alive(needle)) return ath_alloc_dead_number();

    char *hbuf = NULL, *nbuf = NULL;
    size_t hlen = 0, nlen = 0;
    if (ath_string_slurp(hay, &hbuf, &hlen) != 0) return ath_alloc_dead_number();
    if (ath_string_slurp(needle, &nbuf, &nlen) != 0) {
        free(hbuf);
        return ath_alloc_dead_number();
    }

    /* Empty needle matches at position 0 — empty is a prefix of every
     * string (§4.8.4 "Empty needle"). */
    if (nlen == 0) {
        free(hbuf); free(nbuf);
        ath_obj *idx = ath_alloc_number(0);
        ath_inherit_lifetime(idx, hay, needle);
        return idx;
    }

    /* Naive substring search. */
    for (size_t i = 0; i + nlen <= hlen; i++) {
        if (memcmp(hbuf + i, nbuf, nlen) == 0) {
            free(hbuf); free(nbuf);
            ath_obj *idx = ath_alloc_number((int64_t)i);
            ath_inherit_lifetime(idx, hay, needle);
            return idx;
        }
    }
    free(hbuf); free(nbuf);
    return ath_alloc_dead_number();
}

/* Common path for replace and replace_all. all_occurrences selects mode. */
static ath_obj *ath_replace_impl(ath_obj *s, ath_obj *pair, int all_occurrences) {
    if (s == NULL || (s != ath_NULL && !ath_is_alive(s))) return ath_alloc_dead();
    if (pair == NULL || pair == ath_NULL || !ath_is_alive(pair)) return ath_alloc_dead();

    /* Decompose pair → needle, replacement. */
    ath_obj *needle, *repl;
    ath_decompose(pair, &needle, &repl);

    char *sbuf = NULL, *nbuf = NULL, *rbuf = NULL;
    size_t slen = 0, nlen = 0, rlen = 0;
    if (ath_string_slurp(s, &sbuf, &slen) != 0) return ath_alloc_dead();
    if (ath_string_slurp(needle, &nbuf, &nlen) != 0) {
        free(sbuf);
        return ath_alloc_dead();
    }
    if (ath_string_slurp(repl, &rbuf, &rlen) != 0) {
        free(sbuf); free(nbuf);
        return ath_alloc_dead();
    }

    /* §4.8.4: empty needle is dead for replace/replace_all. */
    if (nlen == 0) {
        free(sbuf); free(nbuf); free(rbuf);
        return ath_alloc_dead();
    }

    /* Worst-case output length: every byte becomes a replacement match. */
    size_t max_out = slen;
    if (rlen > nlen) {
        /* Each match grows output by (rlen - nlen). At most slen/nlen
         * matches in REPLACE_ALL; cap is then slen + (slen/nlen)*(rlen-nlen).
         * For single REPLACE the worst case is slen - nlen + rlen. */
        size_t max_matches = all_occurrences ? (slen / nlen) : 1;
        max_out = slen + max_matches * (rlen - nlen);
    }
    char *out = (char *)malloc(max_out > 0 ? max_out : 1);
    if (!out) {
        free(sbuf); free(nbuf); free(rbuf);
        return ath_alloc_dead();
    }

    size_t i = 0, oi = 0;
    int found_any = 0;
    while (i <= slen) {
        /* Try to match at position i. */
        if (i + nlen <= slen && memcmp(sbuf + i, nbuf, nlen) == 0) {
            memcpy(out + oi, rbuf, rlen);
            oi += rlen;
            i += nlen;
            found_any = 1;
            if (!all_occurrences) {
                /* Copy the remainder verbatim. */
                if (i < slen) memcpy(out + oi, sbuf + i, slen - i);
                oi += slen - i;
                i = slen + 1;  /* exit loop */
            }
        } else if (i < slen) {
            out[oi++] = sbuf[i++];
        } else {
            i++;  /* loop ends */
        }
    }

    if (!found_any) {
        free(sbuf); free(nbuf); free(rbuf); free(out);
        return ath_alloc_dead();
    }

    ath_obj *result = ath_buf_to_string(out, oi);
    free(sbuf); free(nbuf); free(rbuf); free(out);
    ath_inherit_lifetime(result, s, pair);
    return result;
}

ath_obj *ath_replace(ath_obj *s, ath_obj *pair) {
    return ath_replace_impl(s, pair, /* all_occurrences = */ 0);
}

ath_obj *ath_replace_all(ath_obj *s, ath_obj *pair) {
    return ath_replace_impl(s, pair, /* all_occurrences = */ 1);
}

/* --- String predicates and transforms (SPEC §4.8.4 extensions) -------- */

/* Walk-by-atom-pointer comparison: returns 1 if both strings yield the
 * same atom pointers in lockstep until both terminate. Returns 0 on
 * mismatch or length mismatch. Returns -1 on malformed string (a left
 * half that is not a recognized character atom). NULL and dead cells
 * end the walk on that side. */
static int ath_string_atoms_eq(ath_obj *a, ath_obj *b) {
    while (1) {
        int a_end = (a == NULL || a == ath_NULL || !ath_is_alive(a));
        int b_end = (b == NULL || b == ath_NULL || !ath_is_alive(b));
        if (a_end && b_end) return 1;
        if (a_end || b_end) return 0;
        ath_obj *al, *ar, *bl, *br;
        ath_decompose(a, &al, &ar);
        ath_decompose(b, &bl, &br);
        if (ath_atom_to_char(al) < 0) return -1;
        if (ath_atom_to_char(bl) < 0) return -1;
        if (al != bl) return 0;
        a = ar;
        b = br;
    }
}

ath_obj *ath_streq(ath_obj *a, ath_obj *b) {
    int r = ath_string_atoms_eq(a, b);
    if (r != 1) return ath_verdict_false();
    return ath_verdict_true(a, b);
}

/* Returns 1 if `hay` begins with `prefix` (atom-equal byte-by-byte),
 * 0 if not, -1 if either is malformed. Empty `prefix` is always a
 * prefix. */
static int ath_string_starts_with(ath_obj *hay, ath_obj *prefix) {
    while (1) {
        int p_end = (prefix == NULL || prefix == ath_NULL || !ath_is_alive(prefix));
        if (p_end) return 1;
        int h_end = (hay == NULL || hay == ath_NULL || !ath_is_alive(hay));
        if (h_end) return 0;
        ath_obj *hl, *hr, *pl, *pr;
        ath_decompose(hay, &hl, &hr);
        ath_decompose(prefix, &pl, &pr);
        if (ath_atom_to_char(hl) < 0) return -1;
        if (ath_atom_to_char(pl) < 0) return -1;
        if (hl != pl) return 0;
        hay = hr;
        prefix = pr;
    }
}

ath_obj *ath_startswith(ath_obj *hay, ath_obj *prefix) {
    int r = ath_string_starts_with(hay, prefix);
    if (r != 1) return ath_verdict_false();
    return ath_verdict_true(hay, prefix);
}

ath_obj *ath_endswith(ath_obj *hay, ath_obj *suffix) {
    /* Empty suffix: always alive. */
    if (suffix == NULL || suffix == ath_NULL) return ath_verdict_true(hay, suffix);
    if (hay != NULL && hay != ath_NULL && !ath_is_alive(hay)) return ath_verdict_false();
    if (!ath_is_alive(suffix)) return ath_verdict_false();

    char *hbuf = NULL, *sbuf = NULL;
    size_t hlen = 0, slen = 0;
    if (ath_string_slurp(hay, &hbuf, &hlen) != 0) return ath_verdict_false();
    if (ath_string_slurp(suffix, &sbuf, &slen) != 0) {
        free(hbuf);
        return ath_verdict_false();
    }
    int ok = (slen <= hlen) && (memcmp(hbuf + hlen - slen, sbuf, slen) == 0);
    free(hbuf); free(sbuf);
    if (!ok) return ath_verdict_false();
    return ath_verdict_true(hay, suffix);
}

/* Buffer-based lex comparison. Returns -1, 0, +1 in usual sense.
 * Returns -2 on malformed string. */
static int ath_string_lexcmp(ath_obj *a, ath_obj *b, int *err) {
    *err = 0;
    char *abuf = NULL, *bbuf = NULL;
    size_t alen = 0, blen = 0;
    if (ath_string_slurp(a, &abuf, &alen) != 0) { *err = 1; return 0; }
    if (ath_string_slurp(b, &bbuf, &blen) != 0) { free(abuf); *err = 1; return 0; }
    size_t n = alen < blen ? alen : blen;
    int c = (n == 0) ? 0 : memcmp(abuf, bbuf, n);
    if (c == 0) {
        if (alen < blen) c = -1;
        else if (alen > blen) c = 1;
    }
    free(abuf); free(bbuf);
    return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

ath_obj *ath_strlt(ath_obj *a, ath_obj *b) {
    if (a != NULL && a != ath_NULL && !ath_is_alive(a)) return ath_verdict_false();
    if (b != NULL && b != ath_NULL && !ath_is_alive(b)) return ath_verdict_false();
    int err = 0;
    int c = ath_string_lexcmp(a, b, &err);
    if (err) return ath_verdict_false();
    if (c >= 0) return ath_verdict_false();
    return ath_verdict_true(a, b);
}

ath_obj *ath_strgt(ath_obj *a, ath_obj *b) {
    if (a != NULL && a != ath_NULL && !ath_is_alive(a)) return ath_verdict_false();
    if (b != NULL && b != ath_NULL && !ath_is_alive(b)) return ath_verdict_false();
    int err = 0;
    int c = ath_string_lexcmp(a, b, &err);
    if (err) return ath_verdict_false();
    if (c <= 0) return ath_verdict_false();
    return ath_verdict_true(a, b);
}

/* Apply a per-byte transform to `s`. The transform returns the
 * replacement byte. A NULL/dead/empty source returns NULL; a malformed
 * source returns ath_NULL too (transform stops at the bad atom and we
 * fall back to "empty"). The result inherits `s` as a dep. */
static ath_obj *ath_map_bytes(ath_obj *s, ath_obj *unused, int (*xform)(int)) {
    (void)unused;
    if (s == NULL || s == ath_NULL) return ath_NULL;
    if (!ath_is_alive(s)) return ath_NULL;
    char *buf = NULL;
    size_t len = 0;
    if (ath_string_slurp(s, &buf, &len) != 0) return ath_NULL;
    if (len == 0) { free(buf); return ath_NULL; }
    for (size_t i = 0; i < len; i++) buf[i] = (char)xform((unsigned char)buf[i]);
    ath_obj *result = ath_buf_to_string(buf, len);
    free(buf);
    ath_inherit_lifetime(result, s, NULL);
    return result;
}

static int ath_xform_lower(int c) {
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int ath_xform_upper(int c) {
    return (c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c;
}

ath_obj *ath_lower(ath_obj *s, ath_obj *unused) {
    return ath_map_bytes(s, unused, ath_xform_lower);
}

ath_obj *ath_upper(ath_obj *s, ath_obj *unused) {
    return ath_map_bytes(s, unused, ath_xform_upper);
}

static int ath_is_strip_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* Generic strip: side==0 trim both, side==-1 lstrip, side==+1 rstrip. */
static ath_obj *ath_strip_impl(ath_obj *s, ath_obj *unused, int side) {
    (void)unused;
    if (s == NULL || s == ath_NULL) return ath_NULL;
    if (!ath_is_alive(s)) return ath_NULL;
    char *buf = NULL;
    size_t len = 0;
    if (ath_string_slurp(s, &buf, &len) != 0) return ath_NULL;
    size_t lo = 0, hi = len;
    if (side <= 0) {
        while (lo < hi && ath_is_strip_space((unsigned char)buf[lo])) lo++;
    }
    if (side >= 0) {
        while (hi > lo && ath_is_strip_space((unsigned char)buf[hi - 1])) hi--;
    }
    ath_obj *result = ath_buf_to_string(buf + lo, hi - lo);
    free(buf);
    ath_inherit_lifetime(result, s, NULL);
    return result;
}

ath_obj *ath_trim(ath_obj *s, ath_obj *unused)   { return ath_strip_impl(s, unused,  0); }
ath_obj *ath_lstrip(ath_obj *s, ath_obj *unused) { return ath_strip_impl(s, unused, -1); }
ath_obj *ath_rstrip(ath_obj *s, ath_obj *unused) { return ath_strip_impl(s, unused, +1); }

/* SPLIT: walk `s` byte-by-byte, accumulate the current run until `sep`
 * is matched, emit the accumulated run as a fresh string into the
 * output list, continue past `sep`. A trailing `sep` yields a trailing
 * empty-string element. An empty `sep` is born dead — "split nothing"
 * is undefined, matching REPLACE's empty-needle rule.
 *
 * Result is a right-nested cons-list whose left halves are themselves
 * cons-list strings. Terminated by ath_NULL. */
ath_obj *ath_split(ath_obj *s, ath_obj *sep) {
    int s_empty = (s == NULL || s == ath_NULL);
    if (sep == NULL || sep == ath_NULL) return ath_alloc_dead();
    if (!ath_is_alive(sep)) return ath_alloc_dead();
    if (!s_empty && !ath_is_alive(s)) return ath_alloc_dead();

    char *sbuf = NULL, *pbuf = NULL;
    size_t slen = 0, plen = 0;
    if (ath_string_slurp(s, &sbuf, &slen) != 0) return ath_alloc_dead();
    if (ath_string_slurp(sep, &pbuf, &plen) != 0) {
        free(sbuf);
        return ath_alloc_dead();
    }
    if (plen == 0) {
        free(sbuf); free(pbuf);
        return ath_alloc_dead();
    }

    /* Two-pass: first scan finds split positions, second builds the
     * cons-list right-to-left. */
    size_t cap = 8;
    size_t *starts = (size_t *)malloc(sizeof(size_t) * cap);
    size_t *ends = (size_t *)malloc(sizeof(size_t) * cap);
    if (!starts || !ends) {
        free(sbuf); free(pbuf); free(starts); free(ends);
        return ath_alloc_dead();
    }
    size_t parts = 0;
    size_t i = 0, run_start = 0;
    while (i <= slen) {
        if (i + plen <= slen && memcmp(sbuf + i, pbuf, plen) == 0) {
            if (parts >= cap) {
                cap *= 2;
                size_t *ns = (size_t *)realloc(starts, sizeof(size_t) * cap);
                size_t *ne = (size_t *)realloc(ends, sizeof(size_t) * cap);
                if (!ns || !ne) {
                    free(sbuf); free(pbuf); free(ns ? ns : starts);
                    free(ne ? ne : ends);
                    return ath_alloc_dead();
                }
                starts = ns; ends = ne;
            }
            starts[parts] = run_start;
            ends[parts] = i;
            parts++;
            i += plen;
            run_start = i;
        } else if (i < slen) {
            i++;
        } else {
            /* Final run from run_start..slen */
            if (parts >= cap) {
                cap *= 2;
                size_t *ns = (size_t *)realloc(starts, sizeof(size_t) * cap);
                size_t *ne = (size_t *)realloc(ends, sizeof(size_t) * cap);
                if (!ns || !ne) {
                    free(sbuf); free(pbuf); free(ns ? ns : starts);
                    free(ne ? ne : ends);
                    return ath_alloc_dead();
                }
                starts = ns; ends = ne;
            }
            starts[parts] = run_start;
            ends[parts] = slen;
            parts++;
            i++;
        }
    }

    ath_obj *list = ath_NULL;
    for (size_t k = parts; k > 0; k--) {
        size_t a = starts[k - 1], b = ends[k - 1];
        ath_obj *elem = ath_buf_to_string(sbuf + a, b - a);
        list = ath_cons_fresh(elem, list);
    }
    free(sbuf); free(pbuf); free(starts); free(ends);
    ath_inherit_lifetime(list, s, sep);
    return list;
}

/* JOIN: walk LIST's right-spine; for each cell, walk the left half as
 * a string into the output, then append SEP if not the last cell. An
 * empty LIST returns NULL. An empty SEP is permitted and yields a
 * concatenation with no separators. */
ath_obj *ath_join(ath_obj *list, ath_obj *sep) {
    int list_empty = (list == NULL || list == ath_NULL);
    if (!list_empty && !ath_is_alive(list)) return ath_alloc_dead();
    int sep_empty = (sep == NULL || sep == ath_NULL);
    if (!sep_empty && !ath_is_alive(sep)) return ath_alloc_dead();
    if (list_empty) return ath_NULL;

    char *pbuf = NULL;
    size_t plen = 0;
    if (!sep_empty) {
        if (ath_string_slurp(sep, &pbuf, &plen) != 0) return ath_alloc_dead();
    }

    /* Two-pass: gather element buffers, then concat. */
    size_t cap = 8, count = 0;
    char **bufs = (char **)malloc(sizeof(char *) * cap);
    size_t *lens = (size_t *)malloc(sizeof(size_t) * cap);
    if (!bufs || !lens) {
        free(pbuf); free(bufs); free(lens);
        return ath_alloc_dead();
    }
    size_t total = 0;
    ath_obj *cur = list;
    while (cur != NULL && cur != ath_NULL && ath_is_alive(cur)) {
        ath_obj *l, *r;
        ath_decompose(cur, &l, &r);
        char *ebuf = NULL;
        size_t elen = 0;
        if (ath_string_slurp(l, &ebuf, &elen) != 0) {
            for (size_t k = 0; k < count; k++) free(bufs[k]);
            free(bufs); free(lens); free(pbuf);
            return ath_alloc_dead();
        }
        if (count >= cap) {
            cap *= 2;
            char **nb = (char **)realloc(bufs, sizeof(char *) * cap);
            size_t *nl = (size_t *)realloc(lens, sizeof(size_t) * cap);
            if (!nb || !nl) {
                for (size_t k = 0; k < count; k++) free(bufs[k]);
                free(nb ? nb : bufs); free(nl ? nl : lens); free(pbuf);
                free(ebuf);
                return ath_alloc_dead();
            }
            bufs = nb; lens = nl;
        }
        bufs[count] = ebuf;
        lens[count] = elen;
        total += elen;
        count++;
        cur = r;
    }
    if (count > 1) total += plen * (count - 1);

    char *out = (count > 0) ? (char *)malloc(total + 1) : NULL;
    if (count > 0 && !out) {
        for (size_t k = 0; k < count; k++) free(bufs[k]);
        free(bufs); free(lens); free(pbuf);
        return ath_alloc_dead();
    }
    size_t oi = 0;
    for (size_t k = 0; k < count; k++) {
        memcpy(out + oi, bufs[k], lens[k]);
        oi += lens[k];
        if (k + 1 < count && plen > 0) {
            memcpy(out + oi, pbuf, plen);
            oi += plen;
        }
        free(bufs[k]);
    }
    free(bufs); free(lens); free(pbuf);

    if (count == 0 || oi == 0) {
        free(out);
        return ath_NULL;
    }
    ath_obj *result = ath_buf_to_string(out, oi);
    free(out);
    ath_inherit_lifetime(result, list, sep);
    return result;
}

_Noreturn void ath_halt(void) {
    fflush(stdout);
    exit(0);
}
