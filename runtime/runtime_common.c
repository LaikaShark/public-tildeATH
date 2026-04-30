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
    v->alive = 0;
}

#define ATH_MAX_SIGNAL 64
static volatile sig_atomic_t ath_signal_received[ATH_MAX_SIGNAL];

int ath_is_alive(ath_obj *v) {
    if (v == NULL) {
        return 0;
    }
    if (!v->alive) {
        return 0;
    }
    /* Deadline-based lifetime: object dies once now >= deadline. */
    if (v->deadline_s > 0.0 && ath_now_s() >= v->deadline_s) {
        v->alive = 0;
        return 0;
    }
    /* File-watch lifetime: object dies once access() fails. */
    if (v->watch_path != NULL && access(v->watch_path, F_OK) != 0) {
        v->alive = 0;
        return 0;
    }
    /* Signal-watch lifetime: object dies once the awaited signal arrives. */
    if (v->awaiting_signal > 0 && v->awaiting_signal < ATH_MAX_SIGNAL
        && ath_signal_received[v->awaiting_signal]) {
        v->alive = 0;
        return 0;
    }
    /* Dependency-inherited lifetime (§4.8.1): if any installed dep is dead,
     * this object is dead too. Recursive — dep chains propagate. The walk
     * terminates because deps point to earlier-allocated objects. */
    if (v->dep1 != NULL && !ath_is_alive(v->dep1)) {
        v->alive = 0;
        return 0;
    }
    if (v->dep2 != NULL && !ath_is_alive(v->dep2)) {
        v->alive = 0;
        return 0;
    }
    /* One-shot: alive for this single observation, dead thereafter. */
    if (v->is_oneshot) {
        v->alive = 0;
    }
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

/* Build a fresh right-nested cons-list terminated with ath_NULL from an
 * array of element heads. Returns ath_NULL on empty input. */
static ath_obj *ath_build_spine(ath_obj **heads, int64_t n) {
    ath_obj *acc = ath_NULL;
    for (int64_t i = n; i > 0; i--) {
        acc = ath_compose(heads[i - 1], acc);
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
    /* Snapshot: copy alive bit and every observable field. Skip dep1/dep2
     * — the clone is independent of v's upstream operand chain. */
    w->alive = v->alive;
    w->left = v->left;
    w->right = v->right;
    w->deadline_s = v->deadline_s;
    w->watch_path = v->watch_path;
    w->is_oneshot = v->is_oneshot;
    w->awaiting_signal = v->awaiting_signal;
    w->has_value = v->has_value;
    w->value = v->value;
    /* dep1 and dep2 stay zeroed by calloc. */
    return w;
}

_Noreturn void ath_halt(void) {
    fflush(stdout);
    exit(0);
}
