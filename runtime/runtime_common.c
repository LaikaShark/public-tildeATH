#define _POSIX_C_SOURCE 200809L

#include "ath_runtime.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

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

// ath_compose lives in compose_fresh.c or compose_intern.c

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
    // If v owns its watched path and is still alive, unlink file before clearing alive bit; errors ignored
    if (v->alive && v->owns_path && v->watch_path) {
        unlink(v->watch_path);
    }
    // Release the socket fd on death (idempotent; no-op for non-sockets).
    if (v->sock_fd > 0) {
        ath_sock_teardown(v);
    }
    v->alive = 0;
}

#define ATH_MAX_SIGNAL 64
static volatile sig_atomic_t ath_signal_received[ATH_MAX_SIGNAL];

// Pure non-mutating observation: 1 iff v observed alive now, without flipping alive bits or consuming one-shots
// Dep walks recurse through ath_observe, so never have side effects
static int ath_observe(ath_obj *v) {
    if (v == NULL) return 0;
    if (!v->alive) return 0;
    if (v->deadline_s > 0.0 && ath_now_s() >= v->deadline_s) return 0;
    if (v->watch_path != NULL && access(v->watch_path, F_OK) != 0) return 0;
    if (v->awaiting_signal > 0 && v->awaiting_signal < ATH_MAX_SIGNAL
        && ath_signal_received[v->awaiting_signal]) return 0;
    // Extended watches: process exit and file-mtime change; read-only syscalls keep ath_observe non-mutating
    if (v->watch_pid > 0 && kill(v->watch_pid, 0) != 0 && errno == ESRCH)
        return 0;
    if (v->mtime_path != NULL) {
        struct stat mst;
        // gone
        if (stat(v->mtime_path, &mst) != 0) return 0;
        // changed
        if ((int64_t)mst.st_mtim.tv_sec != v->mtime_sec
            || (int64_t)mst.st_mtim.tv_nsec != v->mtime_nsec) return 0;
    }
    // Socket peer-close/error is detected by the I/O paths (which latch sock_eof); observe just
    // reads the flag so it stays non-mutating and syscall-free in the hot path.
    if (v->sock_fd > 0 && v->sock_eof) return 0;
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
        // Cache dead-finding so future observations early-return
        if (v->alive) v->alive = 0;
        return 0;
    }
    // One-shot: this direct observation honors alive verdict then flips bit; dep walks don't consume transitively
    if (v->is_oneshot) v->alive = 0;
    return 1;
}

// Non-mutating liveness: reports the same verdict as ath_is_alive without flipping the cached
// bit or consuming a one-shot. For tools (e.g. REPL inspection) that must observe without
// disturbing the object. NULL is dead.
int ath_observe_alive(ath_obj *v) {
    return ath_observe(v);
}

void ath_print_bytes(const char *text, size_t len) {
    if (len > 0) {
        fwrite(text, 1, len, stdout);
    }
}

void ath_print(const char *text, size_t len) {
    ath_print_bytes(text, len);
    fputc('\n', stdout);
}

#define ATH_CHAR_TABLE_SIZE 256
static ath_obj *ath_char_table[ATH_CHAR_TABLE_SIZE] = { 0 };

ath_obj *ath_char_atom(int c) {
    unsigned int idx = (unsigned int)c & 0xFFu;
    if (ath_char_table[idx] == NULL) {
        ath_obj *atom = ath_alloc_alive();
        atom->is_char = 1;
        atom->char_code = (int)idx;
        ath_char_table[idx] = atom;
    }
    return ath_char_table[idx];
}

// Returns 0..255 char code of o, or -1 if not a character
// Identity carried by is_char/char_code, not table-pointer equality, so a clone of an atom is same character
static int ath_atom_to_char(ath_obj *o) {
    if (o != NULL && o->is_char) return o->char_code;
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

ath_obj *ath_build_argv(int argc, char **argv) {
    ath_obj *acc = ath_NULL;
    for (int i = argc - 1; i >= 1; i--) {
        ath_obj *s = ath_string_from_bytes(argv[i], strlen(argv[i]));
        acc = ath_compose(s, acc);
    }
    return acc;
}

ath_obj *ath_coerce_string(ath_obj *v) {
    if (v == NULL || v == ath_NULL) return ath_NULL;
    // Payload-bearing operands become their decimal representation; result inherits v as a dep
    if (ath_has_value(v)) return ath_to_string(v, NULL);
    // Cons-lists, generic composites, char atoms pass through unchanged
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

// Forward decl: numeric rendering shared with ath_to_string
static int ath_number_to_buf(const ath_obj *x, char *buf, size_t cap);

void ath_print_obj_raw(ath_obj *s) {
    // Numeric payload renders as decimal; strings/char atoms carry no payload and fall to cons-list walk
    if (s != NULL && s != ath_NULL && ath_is_alive(s) && ath_has_value(s)) {
        // decimal may exceed a fixed buf
        if (s->num_kind == ATH_NUM_BIG) {
            char *d = ath_big_to_decimal(s->num.b);
            if (d != NULL) { fwrite(d, 1, strlen(d), stdout); free(d); }
            return;
        }
        char buf[64];
        int n = ath_number_to_buf(s, buf, sizeof(buf));
        if (n > 0) fwrite(buf, 1, (size_t)n, stdout);
        return;
    }
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
}

void ath_print_obj(ath_obj *s) {
    ath_print_obj_raw(s);
    fputc('\n', stdout);
}

// Each entry gives range [min_s, max_s] in seconds; import-from-library draws uniform random sample as lifetime
// Match is case-insensitive on the joined import metadata words
typedef struct {
    const char *name;
    double min_s;
    double max_s;
} ath_lifetime_entry;

static const ath_lifetime_entry ath_library[] = {
    // Effectively zero lifetime
    {"instant",          0.0,            0.0},
    {"muzzle flash",     0.0005,         0.002},
    {"tick",             0.001,          0.01},
    {"flash",            0.05,           0.5},
    {"blink",            0.1,            0.4},

    // Low-variance exact-unit entries
    {"second",           1.0,            1.0},
    {"minute",           60.0,           60.0},
    {"hour",             3600.0,         3600.0},
    {"day",              86400.0,        86400.0},
    {"week",             604800.0,       604800.0},
    {"year",             31557600.0,     31557600.0},

    // Short-lived natural phenomena
    {"spark",            0.1,            2.0},
    {"soap bubble",      2.0,            30.0},
    {"smoke ring",       5.0,            60.0},
    {"snowflake",        60.0,           600.0},
    {"ice cube",         900.0,          7200.0},

    // Living things
    // 5 min - 1 day
    {"mayfly",           300.0,          86400.0},
    // 8 - 50 h
    {"fruit fly",        28800.0,        180000.0},
    // 1 - 3 days
    {"fly",              86400.0,        259200.0},
    // 3 - 14 days
    {"banana",           259200.0,       1209600.0},
    // 12 h - 7 days
    {"daisy",            43200.0,        604800.0},
    // 7 - 30 days
    {"rose",             604800.0,       2592000.0},
    // 7 - 28 days
    {"moth",             604800.0,       2419200.0},
    // 1 - 3 years
    {"mouse",            31536000.0,     94608000.0},
    // 3 - 40 years
    {"goldfish",         94608000.0,     1262304000.0},
    // 10 - 18 years
    {"dog",              315360000.0,    567648000.0},
    // 50 - 120 years
    {"human",            1576800000.0,   3787344000.0},

    // Geological / astronomical
    // 1k - 3.5k yr
    {"sequoia",          31536000000.0,   110376000000.0},
    // 4k - 8k yr
    {"pyramid",          126144000000.0,  252288000000.0},
    // ~100M - 1B yr
    {"continent",        3.0e15,          3.0e16},
    // 1B - 10B yr
    {"star",             3.0e16,          3.2e17},
    // 10B - 1T yr
    {"red dwarf",        3.0e17,          3.0e19},
    // 100B - 1T yr
    {"galaxy",           3.0e18,          3.0e19},
    // googol-ish yr
    {"black hole",       3.0e90,          3.0e100},
    // baryon decay
    {"proton",           3.0e37,          3.0e41},
    // heat death
    {"universe",         3.0e100,         3.0e110},
    // effectively inf
    {"forever",          1.0e308,         1.0e308},

    // Explicit high-variance ranges
    // 5 orders of mag
    {"lightning",        0.0001,          10.0},
    // arbitrary
    {"campaign",         0.0,             100.0},
    // 6 orders
    {"experiment",       1.0,             1000000.0},
    // 100 - 1M years
    {"empire",           3.15e9,          3.15e13},

    // Whimsical
    // 80 - 100 years
    {"author",           2.52e9,          3.15e9},
    // 10-100 ns
    {"meson",            1e-8,            1e-7},

    // sentinel
    {NULL, 0.0, 0.0}
};

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
    // User-registered entries take precedence over built-ins
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
        // clamp to avoid double overflow on extreme inputs
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
    // Special non-time-based library entries
    if (strcasecmp(name, "once") == 0) {
        return ath_alloc_oneshot();
    }
    double min_s, max_s;
    if (ath_library_lookup(name, &min_s, &max_s)) {
        return ath_alloc_with_lifetime(min_s, max_s);
    }
    return ath_alloc_alive();
}

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
    // Install once per signum: re-installing is harmless (single-threaded, idempotent handler)
    // but unnecessary, and matters once several actors watch the same signal.
    static char installed[ATH_MAX_SIGNAL];
    if (signum <= 0 || signum >= ATH_MAX_SIGNAL || installed[signum]) return;
    installed[signum] = 1;
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

ath_obj *ath_alloc_watching_pid(ath_obj *n) {
    ath_obj *o = ath_alloc_alive();
    if (n == NULL || !ath_is_alive(n) || n->num_kind != ATH_NUM_INT
        || n->num.i <= 0 || n->num.i > INT_MAX) {
        o->alive = 0;
        return o;
    }
    o->watch_pid = (int)n->num.i;
    // Born dead if process already gone
    if (kill(o->watch_pid, 0) != 0 && errno == ESRCH) o->alive = 0;
    return o;
}

ath_obj *ath_alloc_watching_mtime(const char *path) {
    ath_obj *o = ath_alloc_alive();
    if (path == NULL) {
        o->alive = 0;
        return o;
    }
    struct stat st;
    // missing -> born dead
    if (stat(path, &st) != 0) {
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
    o->mtime_path = copy;
    o->mtime_sec = (int64_t)st.st_mtim.tv_sec;
    o->mtime_nsec = (int64_t)st.st_mtim.tv_nsec;
    return o;
}

static ath_obj *ath_alloc_dead_number(void);

ath_obj *ath_alloc_number(int64_t v) {
    ath_obj *o = ath_alloc_alive();
    o->num_kind = ATH_NUM_INT;
    o->num.i = v;
    return o;
}

ath_obj *ath_alloc_float(double v) {
    ath_obj *o = ath_alloc_alive();
    o->num_kind = ATH_NUM_FLOAT;
    o->num.f = v;
    return o;
}

ath_obj *ath_alloc_bignum(ath_bigint *b) {
    if (b == NULL) return ath_alloc_dead_number();
    // BIG is sticky: once bignum it stays one through arithmetic (no demotion to INT), reaching arbitrary precision
    ath_obj *o = ath_alloc_alive();
    o->num_kind = ATH_NUM_BIG;
    o->num.b = b;
    return o;
}

ath_obj *ath_alloc_bignum_from_decimal(const char *s) {
    return ath_alloc_bignum(ath_big_from_decimal(s));
}

void ath_inherit_lifetime(ath_obj *result, ath_obj *a, ath_obj *b) {
    if (result == NULL || result == ath_NULL) return;
    // Skip self-references and immortal NULL; neither carries useful dependency info
    if (a != NULL && a != ath_NULL && a != result) {
        result->dep1 = a;
    }
    if (b != NULL && b != ath_NULL && b != result) {
        result->dep2 = b;
    }
}

// Born-dead result for failed arithmetic; num_kind stays NONE
static ath_obj *ath_alloc_dead_number(void) {
    ath_obj *o = (ath_obj *)calloc(1, sizeof(ath_obj));
    if (!o) {
        fputs("ath: out of memory\n", stderr);
        exit(1);
    }
    // alive=0, num_kind=ATH_NUM_NONE are calloc defaults
    return o;
}

// Both operands must be alive at call time and carry a payload; 1 if usable, 0 if born-dead result needed
static int ath_operands_usable(ath_obj *x, ath_obj *y) {
    if (x == NULL || !ath_is_alive(x) || !ath_has_value(x)) return 0;
    if (y == NULL || !ath_is_alive(y) || !ath_has_value(y)) return 0;
    return 1;
}

// Numeric-tower helpers. Precedence FLOAT > BIG > INT: int64 when both INT, bigint when either BIG, double when either FLOAT
static double ath_as_double(const ath_obj *o) {
    if (o->num_kind == ATH_NUM_FLOAT) return o->num.f;
    if (o->num_kind == ATH_NUM_BIG)   return ath_big_to_double(o->num.b);
    return (double)o->num.i;
}

// View INT or BIG operand as bigint; INT promotes to fresh bigint, BIG returns stored value (read-only, never freed)
static ath_bigint *ath_as_bigint(const ath_obj *o) {
    return o->num_kind == ATH_NUM_BIG ? o->num.b : ath_big_from_i64(o->num.i);
}

static int ath_either_float(const ath_obj *x, const ath_obj *y) {
    return x->num_kind == ATH_NUM_FLOAT || y->num_kind == ATH_NUM_FLOAT;
}

static int ath_either_big(const ath_obj *x, const ath_obj *y) {
    return x->num_kind == ATH_NUM_BIG || y->num_kind == ATH_NUM_BIG;
}

// True iff o is FLOAT; int-only ops born-die rather than promote
static int ath_is_float(const ath_obj *o) {
    return o != NULL && o->num_kind == ATH_NUM_FLOAT;
}

// True iff o is BIG; int-only ops born-die on it too
static int ath_is_big(const ath_obj *o) {
    return o != NULL && o->num_kind == ATH_NUM_BIG;
}

// Operand integer-only ops cannot accept: usable but not plain int64 (FLOAT or BIG)
static int ath_not_plain_int(const ath_obj *o) {
    return o != NULL && o->num_kind != ATH_NUM_INT;
}

ath_obj *ath_add(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_alloc_dead_number();
    ath_obj *out;
    if (ath_either_float(x, y)) {
        out = ath_alloc_float(ath_as_double(x) + ath_as_double(y));
    } else if (ath_either_big(x, y)) {
        out = ath_alloc_bignum(ath_big_add(ath_as_bigint(x), ath_as_bigint(y)));
    } else {
        int64_t r;
        if (__builtin_add_overflow(x->num.i, y->num.i, &r))
            return ath_alloc_dead_number();
        out = ath_alloc_number(r);
    }
    ath_inherit_lifetime(out, x, y);
    return out;
}

ath_obj *ath_sub(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_alloc_dead_number();
    ath_obj *out;
    if (ath_either_float(x, y)) {
        out = ath_alloc_float(ath_as_double(x) - ath_as_double(y));
    } else if (ath_either_big(x, y)) {
        out = ath_alloc_bignum(ath_big_sub(ath_as_bigint(x), ath_as_bigint(y)));
    } else {
        int64_t r;
        if (__builtin_sub_overflow(x->num.i, y->num.i, &r))
            return ath_alloc_dead_number();
        out = ath_alloc_number(r);
    }
    ath_inherit_lifetime(out, x, y);
    return out;
}

ath_obj *ath_mul(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_alloc_dead_number();
    ath_obj *out;
    if (ath_either_float(x, y)) {
        out = ath_alloc_float(ath_as_double(x) * ath_as_double(y));
    } else if (ath_either_big(x, y)) {
        out = ath_alloc_bignum(ath_big_mul(ath_as_bigint(x), ath_as_bigint(y)));
    } else {
        int64_t r;
        if (__builtin_mul_overflow(x->num.i, y->num.i, &r))
            return ath_alloc_dead_number();
        out = ath_alloc_number(r);
    }
    ath_inherit_lifetime(out, x, y);
    return out;
}

ath_obj *ath_div(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_alloc_dead_number();
    ath_obj *out;
    if (ath_either_float(x, y)) {
        // True division; x/0.0 yields inf or nan, live values (number produced, just not finite)
        out = ath_alloc_float(ath_as_double(x) / ath_as_double(y));
    } else if (ath_either_big(x, y)) {
        ath_bigint *q;
        // big / 0
        if (ath_big_divmod(ath_as_bigint(x), ath_as_bigint(y), &q, NULL))
            return ath_alloc_dead_number();
        out = ath_alloc_bignum(q);
    } else {
        if (y->num.i == 0) return ath_alloc_dead_number();
        // INT64_MIN / -1 overflows two's-complement
        if (x->num.i == INT64_MIN && y->num.i == -1)
            return ath_alloc_dead_number();
        out = ath_alloc_number(x->num.i / y->num.i);
    }
    ath_inherit_lifetime(out, x, y);
    return out;
}

ath_obj *ath_mod(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_alloc_dead_number();
    ath_obj *out;
    if (ath_either_float(x, y)) {
        // fmod(x, 0.0) is nan, a live value
        out = ath_alloc_float(fmod(ath_as_double(x), ath_as_double(y)));
    } else if (ath_either_big(x, y)) {
        ath_bigint *r;
        // big mod 0
        if (ath_big_divmod(ath_as_bigint(x), ath_as_bigint(y), NULL, &r))
            return ath_alloc_dead_number();
        out = ath_alloc_bignum(r);
    } else {
        if (y->num.i == 0) return ath_alloc_dead_number();
        if (x->num.i == INT64_MIN && y->num.i == -1)
            return ath_alloc_dead_number();
        out = ath_alloc_number(x->num.i % y->num.i);
    }
    ath_inherit_lifetime(out, x, y);
    return out;
}

// Render double as shortest round-tripping decimal (repr policy): fixed-point for exponent in [-4,16), else scientific
// nan/inf print as "nan"/"inf"/"-inf"; integer-looking fixed-point gets forced ".0"; cutoff from canonical "%e"
static int ath_format_double(char *buf, size_t cap, double v) {
    if (isnan(v)) return snprintf(buf, cap, "nan");
    if (isinf(v)) return snprintf(buf, cap, v < 0 ? "-inf" : "inf");
    if (v == 0.0) return snprintf(buf, cap, signbit(v) ? "-0.0" : "0.0");

    char tmp[64];
    snprintf(tmp, sizeof tmp, "%.16e", v);
    const char *epos = strchr(tmp, 'e');
    int exp10 = epos ? atoi(epos + 1) : 0;

    if (exp10 >= -4 && exp10 < 16) {
        // Fixed point: fewest decimal places that round-trip
        for (int d = 0; d <= 17; d++) {
            snprintf(buf, cap, "%.*f", d, v);
            if (strtod(buf, NULL) == v) break;
        }
        if (!strchr(buf, '.')) {
            size_t len = strlen(buf);
            if (len + 2 < cap) {
                buf[len] = '.'; buf[len + 1] = '0'; buf[len + 2] = '\0';
            }
        }
    } else {
        // Scientific: fewest mantissa digits that round-trip; always has 'e', stays distinct from int
        for (int p = 0; p <= 17; p++) {
            snprintf(buf, cap, "%.*e", p, v);
            if (strtod(buf, NULL) == v) break;
        }
    }
    return (int)strlen(buf);
}

// Render numeric payload (INT decimal or FLOAT) into buf; caller guarantees payload. Returns byte length
static int ath_number_to_buf(const ath_obj *x, char *buf, size_t cap) {
    if (x->num_kind == ATH_NUM_FLOAT) {
        return ath_format_double(buf, cap, x->num.f);
    }
    return snprintf(buf, cap, "%lld", (long long)x->num.i);
}

ath_obj *ath_to_string(ath_obj *x, ath_obj *unused) {
    (void)unused;
    // No payload -> empty string
    if (x == NULL || !ath_is_alive(x) || !ath_has_value(x)) {
        return ath_NULL;
    }
    char sbuf[64];
    // BIG decimal can exceed sbuf
    char *heap = NULL;
    const char *s;
    int n;
    if (x->num_kind == ATH_NUM_BIG) {
        heap = ath_big_to_decimal(x->num.b);
        if (heap == NULL) return ath_NULL;
        s = heap;
        n = (int)strlen(heap);
    } else {
        n = ath_number_to_buf(x, sbuf, sizeof(sbuf));
        s = sbuf;
    }
    if (n <= 0) { free(heap); return ath_NULL; }
    ath_obj *acc = ath_NULL;
    for (int i = n; i > 0; i--) {
        ath_obj *c = ath_char_atom((unsigned char)s[i - 1]);
        acc = ath_compose(c, acc);
    }
    free(heap);
    ath_inherit_lifetime(acc, x, NULL);
    return acc;
}

// Walk string-cons-list into flat byte buffer; -1 if chain has non-character atom (malformed), else byte count
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

// Alive verdict with deps installed
static ath_obj *ath_verdict_true(ath_obj *x, ath_obj *y) {
    ath_obj *v = ath_alloc_alive();
    ath_inherit_lifetime(v, x, y);
    return v;
}

// Dead-on-arrival verdict; no deps recorded
static ath_obj *ath_verdict_false(void) {
    ath_obj *v = (ath_obj *)calloc(1, sizeof(ath_obj));
    if (!v) {
        fputs("ath: out of memory\n", stderr);
        exit(1);
    }
    return v;
}

// Numeric comparisons. FLOAT promotes to double (IEEE NaN: ordered false, != true), BIG to bigint, else int64
// Across kinds makes 2 == 2.0 and 2 == big(2) true
#define ATH_CMP(name, op)                                                   \
    ath_obj *name(ath_obj *x, ath_obj *y) {                                 \
        if (!ath_operands_usable(x, y)) return ath_verdict_false();         \
        int res = ath_either_float(x, y)                                    \
                      ? (ath_as_double(x) op ath_as_double(y))              \
                      : ath_either_big(x, y)                                \
                          ? (ath_big_cmp(ath_as_bigint(x),                  \
                                         ath_as_bigint(y)) op 0)            \
                          : (x->num.i op y->num.i);                         \
        return res ? ath_verdict_true(x, y) : ath_verdict_false();          \
    }
ATH_CMP(ath_lt, <)
ATH_CMP(ath_eq, ==)
ATH_CMP(ath_gt, >)
ATH_CMP(ath_le, <=)
ATH_CMP(ath_ge, >=)
ATH_CMP(ath_ne, !=)
#undef ATH_CMP

// AND: alive iff both operands alive at every observation; default conjunctive dep machinery
// Born dead if either already dead at call (shortcut keeps born-dead invariant uniform)
ath_obj *ath_and(ath_obj *x, ath_obj *y) {
    int x_alive = (x != NULL) && ath_is_alive(x);
    int y_alive = (y != NULL) && ath_is_alive(y);
    if (!x_alive || !y_alive) return ath_verdict_false();
    ath_obj *v = ath_alloc_alive();
    ath_inherit_lifetime(v, x, y);
    return v;
}

// ENTANGLE: compose two objects into fresh composite, install both as deps on result
// Compose + ath_inherit_lifetime in one call; propagates operand death into the carrier
ath_obj *ath_entangle(ath_obj *x, ath_obj *y) {
    ath_obj *p = ath_compose(x, y);
    ath_inherit_lifetime(p, x, y);
    return p;
}

// OR: alive iff at least one operand alive at every observation; dep_mode=ATH_DEP_OR walks deps disjunctively
// Born dead only if both already dead at call
ath_obj *ath_or(ath_obj *x, ath_obj *y) {
    int x_alive = (x != NULL) && ath_is_alive(x);
    int y_alive = (y != NULL) && ath_is_alive(y);
    if (!x_alive && !y_alive) return ath_verdict_false();
    ath_obj *v = ath_alloc_alive();
    // Install both deps unconditionally so OR walk sees every input, even one dead at call; skip self and NULL
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
    ath_obj *out;
    if (strpbrk(buf, ".eE") != NULL) {
        // Float syntax ('.', 'e', 'E') -> parse as double
        double d = strtod(buf, &end);
        if (end == buf || *end != '\0') return ath_alloc_dead_number();
        if (errno == ERANGE) return ath_alloc_dead_number();
        out = ath_alloc_float(d);
    } else {
        long long v = strtoll(buf, &end, 10);
        if (end == buf || *end != '\0') return ath_alloc_dead_number();
        if (errno == ERANGE) return ath_alloc_dead_number();
        out = ath_alloc_number((int64_t)v);
    }
    ath_inherit_lifetime(out, s, NULL);
    return out;
}

// Born-dead generic object (no payload, no deps)
static ath_obj *ath_alloc_dead(void) {
    ath_obj *o = (ath_obj *)calloc(1, sizeof(ath_obj));
    if (!o) {
        fputs("ath: out of memory\n", stderr);
        exit(1);
    }
    return o;
}

// Walk right-spine counting elements; stops at NULL, ath_NULL, or dead cell. Returns count
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
    // LENGTH(NULL) = 0 (empty string is real); dead non-NULL cell stops walk, counting so far
    int64_t n = ath_spine_length(s);
    ath_obj *out = ath_alloc_number(n);
    ath_inherit_lifetime(out, s, NULL);
    return out;
}

// Walk right-spine collecting heads into array; returns count collected (<= cap), fewer on early dead/NULL terminator
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

// Fresh non-interned cons cell; always unique (unlike hash-consing ath_compose)
// Result owned solely by producing op, so installing operands as deps can't overwrite interned input's deps or cycle
static ath_obj *ath_cons_fresh(ath_obj *head, ath_obj *tail) {
    ath_obj *cell = ath_alloc_alive();
    cell->left = head;
    cell->right = tail;
    return cell;
}

// Build fresh right-nested cons-list terminated with ath_NULL from array of heads; ath_NULL on empty
static ath_obj *ath_build_spine(ath_obj **heads, int64_t n) {
    ath_obj *acc = ath_NULL;
    for (int64_t i = n; i > 0; i--) {
        acc = ath_cons_fresh(heads[i - 1], acc);
    }
    return acc;
}

ath_obj *ath_concat(ath_obj *a, ath_obj *b) {
    // NULL/ath_NULL operands are empty string (alive); non-NULL dead operand is real failure
    int a_empty = (a == NULL || a == ath_NULL);
    int b_empty = (b == NULL || b == ath_NULL);
    if (!a_empty && !ath_is_alive(a)) return ath_alloc_dead();
    if (!b_empty && !ath_is_alive(b)) return ath_alloc_dead();

    int64_t na = a_empty ? 0 : ath_spine_length(a);
    int64_t nb = b_empty ? 0 : ath_spine_length(b);
    int64_t total = na + nb;
    // overflow guard
    if (total < 0) return ath_alloc_dead();

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
    if (n == NULL || !ath_is_alive(n) || n->num_kind != ATH_NUM_INT) return ath_alloc_dead();
    if (n->num.i < 0) return ath_alloc_dead();
    int64_t target = n->num.i;
    int64_t i = 0;
    ath_obj *cur = s;
    while (cur != NULL && cur != ath_NULL && ath_is_alive(cur)) {
        ath_obj *l, *r;
        ath_decompose(cur, &l, &r);
        if (i == target) {
            // Return fresh snapshot, not the element: head is canonical/shared, deps on it would kill shared value
            // Clone copies identity (payload, char code) into independent object depending on S and N
            ath_obj *view = ath_clone(l);
            ath_inherit_lifetime(view, s, n);
            return view;
        }
        i++;
        cur = r;
    }
    // out of range
    return ath_alloc_dead();
}

ath_obj *ath_slice(ath_obj *s, ath_obj *range) {
    if (s == NULL || !ath_is_alive(s)) return ath_alloc_dead();
    if (range == NULL || range == ath_NULL || !ath_is_alive(range))
        return ath_alloc_dead();
    ath_obj *i_obj, *j_obj;
    ath_decompose(range, &i_obj, &j_obj);
    if (i_obj == NULL || !ath_is_alive(i_obj) || i_obj->num_kind != ATH_NUM_INT)
        return ath_alloc_dead();
    if (j_obj == NULL || !ath_is_alive(j_obj) || j_obj->num_kind != ATH_NUM_INT)
        return ath_alloc_dead();
    int64_t i = i_obj->num.i;
    int64_t j = j_obj->num.i;
    if (i < 0 || j < 0 || i > j) return ath_alloc_dead();

    // Walk to position i
    ath_obj *cur = s;
    for (int64_t k = 0; k < i; k++) {
        if (cur == NULL || cur == ath_NULL || !ath_is_alive(cur))
            return ath_alloc_dead();
        ath_obj *l, *r;
        ath_decompose(cur, &l, &r);
        (void)l;
        cur = r;
    }
    // Collect j - i elements
    int64_t want = j - i;
    if (want == 0) {
        // Empty slice (i == j) is dead by design
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
        // walk hit end early - out of range
        free(buf);
        return ath_alloc_dead();
    }
    ath_obj *out = ath_build_spine(buf, collected);
    free(buf);
    ath_inherit_lifetime(out, s, range);
    return out;
}

ath_obj *ath_clone(ath_obj *v) {
    ath_obj *w = (ath_obj *)calloc(1, sizeof(ath_obj));
    if (!w) {
        fputs("ath: out of memory\n", stderr);
        exit(1);
    }
    if (v == NULL || v == ath_NULL) {
        // Cloning NULL yields born-dead object; calloc already gave alive=0, no payload
        return w;
    }
    // Snapshot every observable field; alive bit from non-mutating ath_observe reflects currently observable liveness
    // Matters for OR-mode verdicts (dep1/dep2 deliberately not copied); one-shots not consumed by this refresh
    w->alive = ath_observe(v);
    w->left = v->left;
    w->right = v->right;
    w->deadline_s = v->deadline_s;
    w->watch_path = v->watch_path;
    w->is_oneshot = v->is_oneshot;
    w->awaiting_signal = v->awaiting_signal;
    // Process/mtime watches are mortality conditions; clone inherits them. mtime_path shared by pointer (never freed)
    w->watch_pid = v->watch_pid;
    w->mtime_path = v->mtime_path;
    w->mtime_sec = v->mtime_sec;
    w->mtime_nsec = v->mtime_nsec;
    w->num_kind = v->num_kind;
    w->num = v->num;
    w->dep_mode = v->dep_mode;
    w->is_char = v->is_char;
    w->char_code = v->char_code;
    // dep1, dep2, owns_path stay zeroed by calloc; clone never an owner
    // dep_mode copied but no deps installed: OR-mode clone trusts its captured alive bit
    return w;
}

static int64_t ath_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
}

void ath_sleep_ms(ath_obj *n) {
    if (n == NULL || !ath_is_alive(n) || n->num_kind != ATH_NUM_INT) return;
    if (n->num.i <= 0) return;
    // Inside a coroutine, park (yield with a deadline) instead of blocking the whole
    // scheduler; at top level keep the original blocking nanosleep.
    if (ath_in_actor()) {
        ath_park_until(ath_now_s() + (double)n->num.i / 1000.0);
        return;
    }
    struct timespec ts;
    ts.tv_sec = (time_t)(n->num.i / 1000);
    ts.tv_nsec = (long)((n->num.i % 1000) * 1000000);
    nanosleep(&ts, NULL);
}

ath_obj *ath_alloc_timer_ms(ath_obj *n) {
    // Bad duration -> born dead
    if (n == NULL || !ath_is_alive(n) || n->num_kind != ATH_NUM_INT || n->num.i <= 0) {
        ath_obj *dead = (ath_obj *)calloc(1, sizeof(ath_obj));
        if (!dead) { fputs("ath: out of memory\n", stderr); exit(1); }
        return dead;
    }
    ath_obj *o = ath_alloc_alive();
    double deadline = ath_now_s() + (double)n->num.i / 1000.0;
    if (!(deadline > 0.0) || deadline >= 1.0e308) deadline = 1.0e308;
    o->deadline_s = deadline;
    return o;
}

ath_obj *ath_now(ath_obj *a, ath_obj *b) {
    (void)a; (void)b;
    return ath_alloc_number(ath_now_ms());
}

ath_obj *ath_random_range(ath_obj *lo, ath_obj *hi) {
    if (lo == NULL || !ath_is_alive(lo) || lo->num_kind != ATH_NUM_INT) goto dead;
    if (hi == NULL || !ath_is_alive(hi) || hi->num_kind != ATH_NUM_INT) goto dead;
    if (lo->num.i >= hi->num.i) goto dead;

    // Two rand() calls for ~62 bits entropy; modulo bias negligible for spans well below 2^62
    uint64_t r = ((uint64_t)(rand() & 0x7fffffff) << 31)
               | (uint64_t)(rand() & 0x7fffffff);
    uint64_t span = (uint64_t)(hi->num.i - lo->num.i);
    int64_t result = lo->num.i + (int64_t)(r % span);
    return ath_alloc_number(result);

dead: {
        ath_obj *d = (ath_obj *)calloc(1, sizeof(ath_obj));
        if (!d) { fputs("ath: out of memory\n", stderr); exit(1); }
        return d;
    }
}

// Born-dead generic object; the "failed read/write/etc." return
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

    // Slurp whole file
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

    // Empty file: return ath_NULL (matches TEXT "" semantics)
    if (sz == 0) {
        free(buf);
        return ath_NULL;
    }

    // Build cons-list right-to-left; tail uses regular intern-mode-aware compose path
    ath_obj *tail = ath_NULL;
    for (long i = sz; i > 1; i--) {
        ath_obj *c = ath_char_atom((unsigned char)buf[i - 1]);
        tail = ath_compose(c, tail);
    }

    // Head: fresh non-interned wrapper carrying file ownership; explicit left/right bypasses hash-cons, unique per call
    ath_obj *head = ath_alloc_alive();
    head->left = ath_char_atom((unsigned char)buf[0]);
    head->right = tail;

    // Strdup path so caller can free its argument
    size_t plen = strlen(path);
    char *pcopy = (char *)malloc(plen + 1);
    if (!pcopy) { free(buf); fputs("ath: out of memory\n", stderr); exit(1); }
    memcpy(pcopy, path, plen + 1);
    head->watch_path = pcopy;
    head->owns_path = 1;

    free(buf);
    return head;
}

// Walk s as string, writing each char atom byte via fputc into fp; 0 on success, -1 on write failure or malformed
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
    // Disown file (so ath_die won't unlink) before killing
    v->owns_path = 0;
    // Release the socket fd if this handle is a socket (idempotent; no-op otherwise).
    if (v->sock_fd > 0) {
        ath_sock_teardown(v);
    }
    v->alive = 0;
}

// Slurp string cons-list into heap NUL-terminated buffer; 0 on success filling out_buf and out_len (caller frees)
// -1 on malformed string or alloc failure; NULL/dead s yields successful empty buffer
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

// Public wrapper over ath_string_slurp for net.c (string -> heap bytes; caller frees).
int ath_string_to_bytes(ath_obj *s, char **out_buf, size_t *out_len) {
    return ath_string_slurp(s, out_buf, out_len);
}

ath_obj *ath_alloc_read_file_obj(ath_obj *path_obj) {
    char *buf = NULL;
    size_t len = 0;
    if (ath_string_slurp(path_obj, &buf, &len) != 0) return ath_alloc_dead_obj();
    ath_obj *r = ath_alloc_read_file(buf);
    free(buf);
    return r;
}

ath_obj *ath_write_file_obj(ath_obj *s, ath_obj *path_obj) {
    char *buf = NULL;
    size_t len = 0;
    if (ath_string_slurp(path_obj, &buf, &len) != 0) return ath_alloc_dead_obj();
    ath_obj *r = ath_write_file(s, buf);
    free(buf);
    return r;
}

ath_obj *ath_append_file_obj(ath_obj *s, ath_obj *path_obj) {
    char *buf = NULL;
    size_t len = 0;
    if (ath_string_slurp(path_obj, &buf, &len) != 0) return ath_alloc_dead_obj();
    ath_obj *r = ath_append_file(s, buf);
    free(buf);
    return r;
}

static int ath_mkdir_recursive(const char *path) {
    if (path == NULL || *path == '\0') return -1;
    char *tmp = strdup(path);
    if (!tmp) return -1;
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    int rc = mkdir(tmp, 0755);
    free(tmp);
    return (rc == 0 || errno == EEXIST) ? 0 : -1;
}

int ath_mkdir(const char *path) {
    return ath_mkdir_recursive(path);
}

int ath_mkdir_obj(ath_obj *path_obj) {
    char *buf = NULL;
    size_t len = 0;
    if (ath_string_slurp(path_obj, &buf, &len) != 0) return -1;
    int rc = ath_mkdir(buf);
    free(buf);
    return rc;
}

ath_obj *ath_listdir(const char *path) {
    if (path == NULL) return ath_NULL;
    DIR *d = opendir(path);
    if (!d) return ath_NULL;
    // Collect entries into a temporary array, then build right-to-left
    struct dirent *ent;
    size_t cap = 64, count = 0;
    char **entries = (char **)malloc(cap * sizeof(char *));
    if (!entries) { closedir(d); return ath_NULL; }
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        if (count >= cap) {
            cap *= 2;
            char **tmp = (char **)realloc(entries, cap * sizeof(char *));
            if (!tmp) break;
            entries = tmp;
        }
        entries[count++] = strdup(ent->d_name);
    }
    closedir(d);
    ath_obj *acc = ath_NULL;
    for (size_t i = count; i > 0; i--) {
        ath_obj *s = ath_string_from_bytes(entries[i - 1], strlen(entries[i - 1]));
        acc = ath_compose(s, acc);
        free(entries[i - 1]);
    }
    free(entries);
    return acc;
}

ath_obj *ath_listdir_obj(ath_obj *path_obj) {
    char *buf = NULL;
    size_t len = 0;
    if (ath_string_slurp(path_obj, &buf, &len) != 0) return ath_NULL;
    ath_obj *r = ath_listdir(buf);
    free(buf);
    return r;
}

// Build fresh right-nested cons-list from byte buffer; NULL on empty
static ath_obj *ath_buf_to_string(const char *buf, size_t n) {
    ath_obj *acc = ath_NULL;
    for (size_t i = n; i > 0; i--) {
        ath_obj *c = ath_char_atom((unsigned char)buf[i - 1]);
        acc = ath_cons_fresh(c, acc);
    }
    return acc;
}

ath_obj *ath_find(ath_obj *hay, ath_obj *needle) {
    // NULL hay/needle are empty strings (alive); non-NULL dead operands are real failures
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

    // Empty needle matches at position 0; empty is prefix of every string
    if (nlen == 0) {
        free(hbuf); free(nbuf);
        ath_obj *idx = ath_alloc_number(0);
        ath_inherit_lifetime(idx, hay, needle);
        return idx;
    }

    // Naive substring search
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

// Common path for replace and replace_all; all_occurrences selects mode
static ath_obj *ath_replace_impl(ath_obj *s, ath_obj *pair, int all_occurrences) {
    if (s == NULL || (s != ath_NULL && !ath_is_alive(s))) return ath_alloc_dead();
    if (pair == NULL || pair == ath_NULL || !ath_is_alive(pair)) return ath_alloc_dead();

    // Decompose pair -> needle, replacement
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

    // Empty needle is dead for replace/replace_all
    if (nlen == 0) {
        free(sbuf); free(nbuf); free(rbuf);
        return ath_alloc_dead();
    }

    // Worst-case output length: every byte becomes a replacement match
    size_t max_out = slen;
    if (rlen > nlen) {
        // Each match grows output by (rlen-nlen); at most slen/nlen matches for REPLACE_ALL, 1 for REPLACE
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
        // Try to match at position i
        if (i + nlen <= slen && memcmp(sbuf + i, nbuf, nlen) == 0) {
            memcpy(out + oi, rbuf, rlen);
            oi += rlen;
            i += nlen;
            found_any = 1;
            if (!all_occurrences) {
                // Copy remainder verbatim
                if (i < slen) memcpy(out + oi, sbuf + i, slen - i);
                oi += slen - i;
                // exit loop
                i = slen + 1;
            }
        } else if (i < slen) {
            out[oi++] = sbuf[i++];
        } else {
            // loop ends
            i++;
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
    return ath_replace_impl(s, pair, 0);
}

ath_obj *ath_replace_all(ath_obj *s, ath_obj *pair) {
    return ath_replace_impl(s, pair, 1);
}

// Lockstep comparison: 1 if both strings yield same char codes until both terminate, 0 on mismatch/length mismatch
// -1 on malformed string; NULL and dead cells end the walk on that side
static int ath_string_atoms_eq(ath_obj *a, ath_obj *b) {
    while (1) {
        int a_end = (a == NULL || a == ath_NULL || !ath_is_alive(a));
        int b_end = (b == NULL || b == ath_NULL || !ath_is_alive(b));
        if (a_end && b_end) return 1;
        if (a_end || b_end) return 0;
        ath_obj *al, *ar, *bl, *br;
        ath_decompose(a, &al, &ar);
        ath_decompose(b, &bl, &br);
        int ca = ath_atom_to_char(al);
        int cb = ath_atom_to_char(bl);
        if (ca < 0 || cb < 0) return -1;
        // Compare by char code, not pointer: a snapshot of an atom is same char without being same object
        if (ca != cb) return 0;
        a = ar;
        b = br;
    }
}

ath_obj *ath_streq(ath_obj *a, ath_obj *b) {
    int r = ath_string_atoms_eq(a, b);
    if (r != 1) return ath_verdict_false();
    return ath_verdict_true(a, b);
}

// 1 if hay begins with prefix (byte-by-byte), 0 if not, -1 if malformed; empty prefix is always a prefix
static int ath_string_starts_with(ath_obj *hay, ath_obj *prefix) {
    while (1) {
        int p_end = (prefix == NULL || prefix == ath_NULL || !ath_is_alive(prefix));
        if (p_end) return 1;
        int h_end = (hay == NULL || hay == ath_NULL || !ath_is_alive(hay));
        if (h_end) return 0;
        ath_obj *hl, *hr, *pl, *pr;
        ath_decompose(hay, &hl, &hr);
        ath_decompose(prefix, &pl, &pr);
        int ch = ath_atom_to_char(hl);
        int cp = ath_atom_to_char(pl);
        if (ch < 0 || cp < 0) return -1;
        // compare by code, not pointer
        if (ch != cp) return 0;
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
    // Empty suffix: always alive
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

// Buffer-based lex comparison: -1, 0, +1; sets *err on malformed string
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

// Apply per-byte transform to s; NULL/dead/empty/malformed source returns NULL. Result inherits s as a dep
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

// Generic strip: side==0 trim both, side==-1 lstrip, side==+1 rstrip
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

// SPLIT: accumulate runs until sep matches, emit each as fresh string; trailing sep yields trailing empty element
// Empty sep is born dead. Result: right-nested cons-list whose left halves are themselves cons-list strings
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

    // Two-pass: first scan finds split positions, second builds cons-list right-to-left
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
            // Final run from run_start..slen
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

// JOIN: for each cell walk left half into output, append SEP if not last cell; empty LIST -> NULL, empty SEP allowed
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

    // Two-pass: gather element buffers, then concat
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
            if (nb) bufs = nb;
            size_t *nl = (size_t *)realloc(lens, sizeof(size_t) * cap);
            if (nl) lens = nl;
            if (!nb || !nl) {
                for (size_t k = 0; k < count; k++) free(bufs[k]);
                free(bufs); free(lens); free(pbuf);
                free(ebuf);
                return ath_alloc_dead();
            }
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

// CONTAINS: verdict alive iff NEEDLE occurs anywhere in HAY; empty needle in every string; dead/malformed -> dead
ath_obj *ath_contains(ath_obj *hay, ath_obj *needle) {
    int hay_empty = (hay == NULL || hay == ath_NULL);
    int needle_empty = (needle == NULL || needle == ath_NULL);
    if (!hay_empty && !ath_is_alive(hay)) return ath_verdict_false();
    if (!needle_empty && !ath_is_alive(needle)) return ath_verdict_false();

    char *hbuf = NULL, *nbuf = NULL;
    size_t hlen = 0, nlen = 0;
    if (ath_string_slurp(hay, &hbuf, &hlen) != 0) return ath_verdict_false();
    if (ath_string_slurp(needle, &nbuf, &nlen) != 0) {
        free(hbuf);
        return ath_verdict_false();
    }
    // empty needle is always present
    int found = (nlen == 0);
    for (size_t i = 0; !found && i + nlen <= hlen; i++) {
        if (memcmp(hbuf + i, nbuf, nlen) == 0) found = 1;
    }
    free(hbuf); free(nbuf);
    return found ? ath_verdict_true(hay, needle) : ath_verdict_false();
}

// COUNT: int64 payload = non-overlapping occurrences of NEEDLE in HAY; empty needle born dead, zero matches -> 0
ath_obj *ath_count(ath_obj *hay, ath_obj *needle) {
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
    // empty needle born dead
    if (nlen == 0) {
        free(hbuf); free(nbuf);
        return ath_alloc_dead_number();
    }
    int64_t n = 0;
    for (size_t i = 0; i + nlen <= hlen; ) {
        if (memcmp(hbuf + i, nbuf, nlen) == 0) { n++; i += nlen; }
        else i++;
    }
    free(hbuf); free(nbuf);
    ath_obj *r = ath_alloc_number(n);
    ath_inherit_lifetime(r, hay, needle);
    return r;
}

// RFIND: int64 payload = index of LAST occurrence of NEEDLE in HAY; empty needle matches at len(HAY)
ath_obj *ath_rfind(ath_obj *hay, ath_obj *needle) {
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
    // empty needle matches at the end
    if (nlen == 0) {
        free(hbuf); free(nbuf);
        ath_obj *idx = ath_alloc_number((int64_t)hlen);
        ath_inherit_lifetime(idx, hay, needle);
        return idx;
    }
    int found = 0;
    size_t pos = 0;
    for (size_t i = 0; i + nlen <= hlen; i++) {
        if (memcmp(hbuf + i, nbuf, nlen) == 0) { found = 1; pos = i; }
    }
    free(hbuf); free(nbuf);
    if (!found) return ath_alloc_dead_number();
    ath_obj *idx = ath_alloc_number((int64_t)pos);
    ath_inherit_lifetime(idx, hay, needle);
    return idx;
}

// REPEAT: fresh cons-list = S concatenated with itself N times; N==0 -> NULL, N<0 or no payload -> dead
ath_obj *ath_repeat(ath_obj *s, ath_obj *n) {
    if (n == NULL || !ath_is_alive(n) || n->num_kind != ATH_NUM_INT) return ath_alloc_dead();
    if (n->num.i < 0) return ath_alloc_dead();
    int s_empty = (s == NULL || s == ath_NULL);
    if (!s_empty && !ath_is_alive(s)) return ath_alloc_dead();
    if (n->num.i == 0 || s_empty) return ath_NULL;

    char *buf = NULL;
    size_t len = 0;
    if (ath_string_slurp(s, &buf, &len) != 0) return ath_alloc_dead();
    if (len == 0) { free(buf); return ath_NULL; }
    // overflow guard
    if ((size_t)n->num.i > ((size_t)-1) / len) {
        free(buf);
        return ath_alloc_dead();
    }
    size_t total = len * (size_t)n->num.i;
    char *out = (char *)malloc(total);
    if (!out) { free(buf); fputs("ath: out of memory\n", stderr); exit(1); }
    for (int64_t k = 0; k < n->num.i; k++) {
        memcpy(out + (size_t)k * len, buf, len);
    }
    free(buf);
    ath_obj *result = ath_buf_to_string(out, total);
    free(out);
    ath_inherit_lifetime(result, s, n);
    return result;
}

// REVERSE: fresh cons-list with characters of S reversed; NULL/dead/malformed source -> NULL
ath_obj *ath_reverse(ath_obj *s, ath_obj *unused) {
    (void)unused;
    if (s == NULL || s == ath_NULL) return ath_NULL;
    if (!ath_is_alive(s)) return ath_NULL;
    char *buf = NULL;
    size_t len = 0;
    if (ath_string_slurp(s, &buf, &len) != 0) return ath_NULL;
    if (len == 0) { free(buf); return ath_NULL; }
    for (size_t i = 0, j = len - 1; i < j; i++, j--) {
        char t = buf[i]; buf[i] = buf[j]; buf[j] = t;
    }
    ath_obj *result = ath_buf_to_string(buf, len);
    free(buf);
    ath_inherit_lifetime(result, s, NULL);
    return result;
}

// Generic space-padding to width N; on_left selects side. S already >= N -> fresh copy. N<0 or no payload -> dead
static ath_obj *ath_pad_impl(ath_obj *s, ath_obj *n, int on_left) {
    if (n == NULL || !ath_is_alive(n) || n->num_kind != ATH_NUM_INT) return ath_alloc_dead();
    if (n->num.i < 0) return ath_alloc_dead();
    int s_empty = (s == NULL || s == ath_NULL);
    if (!s_empty && !ath_is_alive(s)) return ath_alloc_dead();

    char *buf = NULL;
    size_t len = 0;
    if (!s_empty && ath_string_slurp(s, &buf, &len) != 0) return ath_alloc_dead();
    size_t width = (size_t)n->num.i;
    size_t pad = (len >= width) ? 0 : (width - len);
    size_t total = len + pad;
    if (total == 0) { free(buf); return ath_NULL; }
    char *out = (char *)malloc(total);
    if (!out) { free(buf); fputs("ath: out of memory\n", stderr); exit(1); }
    if (on_left) {
        memset(out, ' ', pad);
        if (len > 0) memcpy(out + pad, buf, len);
    } else {
        if (len > 0) memcpy(out, buf, len);
        memset(out + len, ' ', pad);
    }
    free(buf);
    ath_obj *result = ath_buf_to_string(out, total);
    free(out);
    ath_inherit_lifetime(result, s, n);
    return result;
}

ath_obj *ath_pad_left(ath_obj *s, ath_obj *n)  { return ath_pad_impl(s, n, 1); }
ath_obj *ath_pad_right(ath_obj *s, ath_obj *n) { return ath_pad_impl(s, n, 0); }

// ORD: int64 payload = char code (0..255) of single character atom A; non-atom or dead A -> dead
ath_obj *ath_ord(ath_obj *a, ath_obj *unused) {
    (void)unused;
    if (a == NULL || a == ath_NULL || !ath_is_alive(a)) return ath_alloc_dead_number();
    int c = ath_atom_to_char(a);
    if (c < 0) return ath_alloc_dead_number();
    ath_obj *r = ath_alloc_number((int64_t)c);
    ath_inherit_lifetime(r, a, NULL);
    return r;
}

// CHR: length-1 string whose single char has code N (0..255); out-of-range/no-payload/dead -> dead. Inverse of ORD
ath_obj *ath_chr(ath_obj *n, ath_obj *unused) {
    (void)unused;
    if (n == NULL || !ath_is_alive(n) || n->num_kind != ATH_NUM_INT) return ath_alloc_dead();
    // Char codes are integral 0..255: FLOAT or BIG code is born dead
    if (n->num_kind != ATH_NUM_INT) return ath_alloc_dead();
    if (n->num.i < 0 || n->num.i > 255) return ath_alloc_dead();
    ath_obj *atom = ath_char_atom((int)n->num.i);
    ath_obj *result = ath_cons_fresh(atom, ath_NULL);
    ath_inherit_lifetime(result, n, NULL);
    return result;
}

// Single number operand usable iff alive and payload-bearing
static int ath_num_usable(ath_obj *x) {
    return x != NULL && ath_is_alive(x) && ath_has_value(x);
}

// POW: X raised to Y. Integer exponents only must be >= 0; overflow or negative exponent born dead. 0^0 == 1
ath_obj *ath_pow(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_alloc_dead_number();
    ath_obj *r;
    if (ath_either_float(x, y)) {
        // Float pow handles negative/fractional exponents; out-of-domain yields nan, a live value
        r = ath_alloc_float(pow(ath_as_double(x), ath_as_double(y)));
    } else if (ath_either_big(x, y)) {
        // Bignum exponentiation born dead in this phase (unbounded results)
        return ath_alloc_dead_number();
    } else {
        if (y->num.i < 0) return ath_alloc_dead_number();
        int64_t base = x->num.i, exp = y->num.i, result = 1;
        while (exp > 0) {
            if (exp & 1) {
                if (__builtin_mul_overflow(result, base, &result))
                    return ath_alloc_dead_number();
            }
            exp >>= 1;
            if (exp > 0 && __builtin_mul_overflow(base, base, &base))
                return ath_alloc_dead_number();
        }
        r = ath_alloc_number(result);
    }
    ath_inherit_lifetime(r, x, y);
    return r;
}

// ABS: magnitude of X; INT64_MIN has no positive representation -> dead, FLOAT via fabs
ath_obj *ath_abs(ath_obj *x, ath_obj *unused) {
    (void)unused;
    if (!ath_num_usable(x)) return ath_alloc_dead_number();
    ath_obj *r;
    if (ath_is_float(x)) {
        r = ath_alloc_float(fabs(x->num.f));
    } else if (ath_is_big(x)) {
        r = ath_alloc_bignum(ath_big_abs(x->num.b));
    } else {
        if (x->num.i == INT64_MIN) return ath_alloc_dead_number();
        r = ath_alloc_number(x->num.i < 0 ? -x->num.i : x->num.i);
    }
    ath_inherit_lifetime(r, x, NULL);
    return r;
}

// NEG: arithmetic negation; INT64_MIN overflows int path -> dead, FLOAT and BIG negate directly
ath_obj *ath_neg(ath_obj *x, ath_obj *unused) {
    (void)unused;
    if (!ath_num_usable(x)) return ath_alloc_dead_number();
    ath_obj *r;
    if (ath_is_float(x)) {
        r = ath_alloc_float(-x->num.f);
    } else if (ath_is_big(x)) {
        r = ath_alloc_bignum(ath_big_neg(x->num.b));
    } else {
        if (x->num.i == INT64_MIN) return ath_alloc_dead_number();
        r = ath_alloc_number(-x->num.i);
    }
    ath_inherit_lifetime(r, x, NULL);
    return r;
}

// MIN / MAX of two payloads; promote to FLOAT if either operand is FLOAT
ath_obj *ath_min(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_alloc_dead_number();
    ath_obj *r;
    if (ath_either_float(x, y)) {
        double a = ath_as_double(x), b = ath_as_double(y);
        r = ath_alloc_float(a < b ? a : b);
    } else if (ath_either_big(x, y)) {
        ath_obj *pick = ath_big_cmp(ath_as_bigint(x), ath_as_bigint(y)) <= 0 ? x : y;
        r = ath_alloc_bignum(ath_big_copy(ath_as_bigint(pick)));
    } else {
        r = ath_alloc_number(x->num.i < y->num.i ? x->num.i : y->num.i);
    }
    ath_inherit_lifetime(r, x, y);
    return r;
}
ath_obj *ath_max(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_alloc_dead_number();
    ath_obj *r;
    if (ath_either_float(x, y)) {
        double a = ath_as_double(x), b = ath_as_double(y);
        r = ath_alloc_float(a > b ? a : b);
    } else if (ath_either_big(x, y)) {
        ath_obj *pick = ath_big_cmp(ath_as_bigint(x), ath_as_bigint(y)) >= 0 ? x : y;
        r = ath_alloc_bignum(ath_big_copy(ath_as_bigint(pick)));
    } else {
        r = ath_alloc_number(x->num.i > y->num.i ? x->num.i : y->num.i);
    }
    ath_inherit_lifetime(r, x, y);
    return r;
}

// GCD of magnitudes (Euclid); gcd(0,0)==0, INT64_MIN -> dead (magnitude unrepresentable), FLOAT operand born dead
ath_obj *ath_gcd(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_alloc_dead_number();
    if (ath_not_plain_int(x) || ath_not_plain_int(y)) return ath_alloc_dead_number();
    int64_t a = x->num.i, b = y->num.i;
    if (a == INT64_MIN || b == INT64_MIN) return ath_alloc_dead_number();
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b != 0) { int64_t t = a % b; a = b; b = t; }
    ath_obj *r = ath_alloc_number(a);
    ath_inherit_lifetime(r, x, y);
    return r;
}

// SIGN: -1, 0, or +1; FLOAT input yields FLOAT -1.0/0.0/1.0 (nan -> 0)
ath_obj *ath_sign(ath_obj *x, ath_obj *unused) {
    (void)unused;
    if (!ath_num_usable(x)) return ath_alloc_dead_number();
    ath_obj *r;
    if (ath_is_float(x)) {
        double v = x->num.f;
        r = ath_alloc_float((double)((v > 0) - (v < 0)));
    } else if (ath_is_big(x)) {
        // -1 or +1 (big is never 0)
        r = ath_alloc_number(x->num.b->sign);
    } else {
        r = ath_alloc_number((x->num.i > 0) - (x->num.i < 0));
    }
    ath_inherit_lifetime(r, x, NULL);
    return r;
}

// Bitwise ops over two's-complement int64 payload; any FLOAT operand born dead
ath_obj *ath_band(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_alloc_dead_number();
    if (ath_not_plain_int(x) || ath_not_plain_int(y)) return ath_alloc_dead_number();
    ath_obj *r = ath_alloc_number(x->num.i & y->num.i);
    ath_inherit_lifetime(r, x, y);
    return r;
}
ath_obj *ath_bor(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_alloc_dead_number();
    if (ath_not_plain_int(x) || ath_not_plain_int(y)) return ath_alloc_dead_number();
    ath_obj *r = ath_alloc_number(x->num.i | y->num.i);
    ath_inherit_lifetime(r, x, y);
    return r;
}
ath_obj *ath_bxor(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_alloc_dead_number();
    if (ath_not_plain_int(x) || ath_not_plain_int(y)) return ath_alloc_dead_number();
    ath_obj *r = ath_alloc_number(x->num.i ^ y->num.i);
    ath_inherit_lifetime(r, x, y);
    return r;
}
ath_obj *ath_bnot(ath_obj *x, ath_obj *unused) {
    (void)unused;
    if (!ath_num_usable(x)) return ath_alloc_dead_number();
    if (ath_not_plain_int(x)) return ath_alloc_dead_number();
    ath_obj *r = ath_alloc_number(~x->num.i);
    ath_inherit_lifetime(r, x, NULL);
    return r;
}

// SHL / SHR: shift by 0..63, out-of-range born dead. SHL unsigned (avoids UB), SHR arithmetic; FLOAT operand born dead
ath_obj *ath_shl(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_alloc_dead_number();
    if (ath_not_plain_int(x) || ath_not_plain_int(y)) return ath_alloc_dead_number();
    if (y->num.i < 0 || y->num.i > 63) return ath_alloc_dead_number();
    ath_obj *r = ath_alloc_number((int64_t)((uint64_t)x->num.i << y->num.i));
    ath_inherit_lifetime(r, x, y);
    return r;
}
ath_obj *ath_shr(ath_obj *x, ath_obj *y) {
    if (!ath_operands_usable(x, y)) return ath_alloc_dead_number();
    if (ath_not_plain_int(x) || ath_not_plain_int(y)) return ath_alloc_dead_number();
    if (y->num.i < 0 || y->num.i > 63) return ath_alloc_dead_number();
    ath_obj *r = ath_alloc_number(x->num.i >> y->num.i);
    ath_inherit_lifetime(r, x, y);
    return r;
}

// CLAMP: confine X to [LO, HI] packed as pair (LO, HI); born dead if X/pair unusable or LO>HI; FLOAT promotes
ath_obj *ath_clamp(ath_obj *x, ath_obj *pair) {
    if (!ath_num_usable(x)) return ath_alloc_dead_number();
    if (pair == NULL || pair == ath_NULL || !ath_is_alive(pair))
        return ath_alloc_dead_number();
    ath_obj *lo, *hi;
    ath_decompose(pair, &lo, &hi);
    if (!ath_num_usable(lo) || !ath_num_usable(hi)) return ath_alloc_dead_number();
    ath_obj *r;
    if (ath_is_float(x) || ath_is_float(lo) || ath_is_float(hi)) {
        double v = ath_as_double(x);
        double lo_d = ath_as_double(lo), hi_d = ath_as_double(hi);
        if (lo_d > hi_d) return ath_alloc_dead_number();
        if (v < lo_d) v = lo_d; else if (v > hi_d) v = hi_d;
        r = ath_alloc_float(v);
    } else if (ath_is_big(x) || ath_is_big(lo) || ath_is_big(hi)) {
        if (ath_big_cmp(ath_as_bigint(lo), ath_as_bigint(hi)) > 0)
            return ath_alloc_dead_number();
        ath_obj *v = x;
        if (ath_big_cmp(ath_as_bigint(v), ath_as_bigint(lo)) < 0) v = lo;
        else if (ath_big_cmp(ath_as_bigint(v), ath_as_bigint(hi)) > 0) v = hi;
        r = ath_alloc_bignum(ath_big_copy(ath_as_bigint(v)));
    } else {
        if (lo->num.i > hi->num.i) return ath_alloc_dead_number();
        int64_t v = x->num.i;
        if (v < lo->num.i) v = lo->num.i;
        else if (v > hi->num.i) v = hi->num.i;
        r = ath_alloc_number(v);
    }
    ath_inherit_lifetime(r, x, pair);
    return r;
}

// INT_TO_BIGNUM: promote into sticky BIG; INT becomes bignum, BIG passes through, integral FLOAT converted exactly
// Non-integral/non-finite FLOAT born dead. Sticky BIG stays big through arithmetic (e.g. factorial accumulator)
ath_obj *ath_int_to_bignum(ath_obj *x, ath_obj *unused) {
    (void)unused;
    if (!ath_num_usable(x)) return ath_alloc_dead_number();
    ath_bigint *b;
    if (x->num_kind == ATH_NUM_BIG) {
        b = ath_big_copy(x->num.b);
    } else if (x->num_kind == ATH_NUM_FLOAT) {
        double v = x->num.f;
        if (!isfinite(v) || trunc(v) != v) return ath_alloc_dead_number();
        char buf[512];
        snprintf(buf, sizeof(buf), "%.0f", v);
        b = ath_big_from_decimal(buf);
    } else {
        b = ath_big_from_i64(x->num.i);
    }
    ath_obj *r = ath_alloc_bignum(b);
    ath_inherit_lifetime(r, x, NULL);
    return r;
}

// INT_TO_FLOAT: reinterpret payload as FLOAT (already-float passes through); dead/missing operand born dead
ath_obj *ath_int_to_float(ath_obj *x, ath_obj *unused) {
    (void)unused;
    if (!ath_num_usable(x)) return ath_alloc_dead_number();
    ath_obj *r = ath_alloc_float(ath_as_double(x));
    ath_inherit_lifetime(r, x, NULL);
    return r;
}

// FLOAT_TO_INT: truncate toward zero to int64 (int passes through); nan or out-of-int64-range born dead
ath_obj *ath_float_to_int(ath_obj *x, ath_obj *unused) {
    (void)unused;
    if (!ath_num_usable(x)) return ath_alloc_dead_number();
    ath_obj *r;
    if (x->num_kind == ATH_NUM_FLOAT) {
        double v = trunc(x->num.f);
        if (isnan(v) || v < -9.2233720368547758e18 || v >= 9.2233720368547758e18)
            return ath_alloc_dead_number();
        r = ath_alloc_number((int64_t)v);
    } else if (x->num_kind == ATH_NUM_BIG) {
        // BIG is always out of int64 range, can't become an int
        return ath_alloc_dead_number();
    } else {
        r = ath_alloc_number(x->num.i);
    }
    ath_inherit_lifetime(r, x, NULL);
    return r;
}

// FLOOR / CEIL / ROUND: round FLOAT to whole-valued FLOAT (int passes through); ROUND is round-half-away-from-zero
#define ATH_ROUNDOP(name, fn)                                               \
    ath_obj *name(ath_obj *x, ath_obj *unused) {                            \
        (void)unused;                                                       \
        if (!ath_num_usable(x)) return ath_alloc_dead_number();             \
        ath_obj *r = (x->num_kind == ATH_NUM_FLOAT)                         \
                         ? ath_alloc_float(fn(x->num.f))                    \
                         : ath_alloc_number(x->num.i);                      \
        ath_inherit_lifetime(r, x, NULL);                                   \
        return r;                                                           \
    }
ATH_ROUNDOP(ath_floor, floor)
ATH_ROUNDOP(ath_ceil, ceil)
ATH_ROUNDOP(ath_round, round)
#undef ATH_ROUNDOP

// Each reads operand(s) as double (INT promotes) and returns FLOAT; born dead only on dead/absent operand
// Out-of-domain inputs (sqrt(-1), log(0)) produce live IEEE result (nan / -inf)
#define ATH_FLOATFN1(name, fn)                                              \
    ath_obj *name(ath_obj *x, ath_obj *unused) {                            \
        (void)unused;                                                       \
        if (!ath_num_usable(x)) return ath_alloc_dead_number();             \
        ath_obj *r = ath_alloc_float(fn(ath_as_double(x)));                 \
        ath_inherit_lifetime(r, x, NULL);                                   \
        return r;                                                           \
    }
ATH_FLOATFN1(ath_sqrt, sqrt)
ATH_FLOATFN1(ath_cbrt, cbrt)
ATH_FLOATFN1(ath_exp, exp)
ATH_FLOATFN1(ath_log, log)
ATH_FLOATFN1(ath_log2, log2)
ATH_FLOATFN1(ath_log10, log10)
ATH_FLOATFN1(ath_sin, sin)
ATH_FLOATFN1(ath_cos, cos)
ATH_FLOATFN1(ath_tan, tan)
ATH_FLOATFN1(ath_asin, asin)
ATH_FLOATFN1(ath_acos, acos)
ATH_FLOATFN1(ath_atan, atan)
#undef ATH_FLOATFN1

#define ATH_FLOATFN2(name, fn)                                              \
    ath_obj *name(ath_obj *x, ath_obj *y) {                                 \
        if (!ath_operands_usable(x, y)) return ath_alloc_dead_number();     \
        ath_obj *r = ath_alloc_float(fn(ath_as_double(x), ath_as_double(y)));\
        ath_inherit_lifetime(r, x, y);                                      \
        return r;                                                           \
    }
ATH_FLOATFN2(ath_atan2, atan2)
ATH_FLOATFN2(ath_hypot, hypot)
#undef ATH_FLOATFN2

// COMPARE: int64 -1/0/1 by byte-lexicographic order (three-way form of strlt/streq/strgt); dead/malformed -> dead
ath_obj *ath_compare(ath_obj *a, ath_obj *b) {
    if (a != NULL && a != ath_NULL && !ath_is_alive(a)) return ath_alloc_dead_number();
    if (b != NULL && b != ath_NULL && !ath_is_alive(b)) return ath_alloc_dead_number();
    int err = 0;
    int c = ath_string_lexcmp(a, b, &err);
    if (err) return ath_alloc_dead_number();
    ath_obj *r = ath_alloc_number(c);
    ath_inherit_lifetime(r, a, b);
    return r;
}

// CHAR_AT: Nth character of S as length-1 string (vs S[N] bare atom); N negative/no-payload/out-of-range or dead S -> dead
ath_obj *ath_char_at(ath_obj *s, ath_obj *n) {
    if (n == NULL || !ath_is_alive(n) || n->num_kind != ATH_NUM_INT || n->num.i < 0)
        return ath_alloc_dead();
    int s_empty = (s == NULL || s == ath_NULL);
    if (!s_empty && !ath_is_alive(s)) return ath_alloc_dead();
    char *buf = NULL;
    size_t len = 0;
    if (ath_string_slurp(s, &buf, &len) != 0) return ath_alloc_dead();
    if ((size_t)n->num.i >= len) { free(buf); return ath_alloc_dead(); }
    ath_obj *atom = ath_char_atom((unsigned char)buf[n->num.i]);
    free(buf);
    ath_obj *result = ath_cons_fresh(atom, ath_NULL);
    ath_inherit_lifetime(result, s, n);
    return result;
}

// FIND_FROM: first index of NEEDLE in S at/after START, pair packs (NEEDLE, START); empty needle matches at min(START, len)
ath_obj *ath_find_from(ath_obj *s, ath_obj *pair) {
    int s_empty = (s == NULL || s == ath_NULL);
    if (!s_empty && !ath_is_alive(s)) return ath_alloc_dead_number();
    if (pair == NULL || pair == ath_NULL || !ath_is_alive(pair))
        return ath_alloc_dead_number();
    ath_obj *needle, *start_obj;
    ath_decompose(pair, &needle, &start_obj);
    if (start_obj == NULL || !ath_is_alive(start_obj) || start_obj->num_kind != ATH_NUM_INT)
        return ath_alloc_dead_number();
    int64_t start = start_obj->num.i;
    if (start < 0) return ath_alloc_dead_number();

    char *hbuf = NULL, *nbuf = NULL;
    size_t hlen = 0, nlen = 0;
    if (ath_string_slurp(s, &hbuf, &hlen) != 0) return ath_alloc_dead_number();
    if (ath_string_slurp(needle, &nbuf, &nlen) != 0) {
        free(hbuf);
        return ath_alloc_dead_number();
    }
    // empty needle matches at the clamped start
    if (nlen == 0) {
        free(hbuf); free(nbuf);
        int64_t pos = start <= (int64_t)hlen ? start : (int64_t)hlen;
        ath_obj *idx = ath_alloc_number(pos);
        ath_inherit_lifetime(idx, s, pair);
        return idx;
    }
    for (size_t i = (size_t)start; i + nlen <= hlen; i++) {
        if (memcmp(hbuf + i, nbuf, nlen) == 0) {
            free(hbuf); free(nbuf);
            ath_obj *idx = ath_alloc_number((int64_t)i);
            ath_inherit_lifetime(idx, s, pair);
            return idx;
        }
    }
    free(hbuf); free(nbuf);
    return ath_alloc_dead_number();
}

// CAPITALIZE: first character uppercased, rest lowercased; NULL in, NULL out
ath_obj *ath_capitalize(ath_obj *s, ath_obj *unused) {
    (void)unused;
    if (s == NULL || s == ath_NULL) return ath_NULL;
    if (!ath_is_alive(s)) return ath_NULL;
    char *buf = NULL;
    size_t len = 0;
    if (ath_string_slurp(s, &buf, &len) != 0) return ath_NULL;
    if (len == 0) { free(buf); return ath_NULL; }
    buf[0] = (char)ath_xform_upper((unsigned char)buf[0]);
    for (size_t i = 1; i < len; i++) buf[i] = (char)ath_xform_lower((unsigned char)buf[i]);
    ath_obj *result = ath_buf_to_string(buf, len);
    free(buf);
    ath_inherit_lifetime(result, s, NULL);
    return result;
}

// TITLE: first character of each whitespace-delimited word uppercased, all others lowercased
ath_obj *ath_title(ath_obj *s, ath_obj *unused) {
    (void)unused;
    if (s == NULL || s == ath_NULL) return ath_NULL;
    if (!ath_is_alive(s)) return ath_NULL;
    char *buf = NULL;
    size_t len = 0;
    if (ath_string_slurp(s, &buf, &len) != 0) return ath_NULL;
    if (len == 0) { free(buf); return ath_NULL; }
    int at_word_start = 1;
    for (size_t i = 0; i < len; i++) {
        if (ath_is_strip_space((unsigned char)buf[i])) {
            at_word_start = 1;
        } else {
            buf[i] = (char)(at_word_start
                ? ath_xform_upper((unsigned char)buf[i])
                : ath_xform_lower((unsigned char)buf[i]));
            at_word_start = 0;
        }
    }
    ath_obj *result = ath_buf_to_string(buf, len);
    free(buf);
    ath_inherit_lifetime(result, s, NULL);
    return result;
}

// Strip characters in the CHARS set; side: 0 both, -1 left, +1 right. Empty CHARS strips nothing (returns copy)
static ath_obj *ath_strip_chars_impl(ath_obj *s, ath_obj *chars, int side) {
    if (s == NULL || s == ath_NULL) return ath_NULL;
    if (!ath_is_alive(s)) return ath_NULL;
    int chars_empty = (chars == NULL || chars == ath_NULL);
    if (!chars_empty && !ath_is_alive(chars)) return ath_NULL;
    char *buf = NULL;
    size_t len = 0;
    if (ath_string_slurp(s, &buf, &len) != 0) return ath_NULL;
    char *cbuf = NULL;
    size_t clen = 0;
    if (!chars_empty && ath_string_slurp(chars, &cbuf, &clen) != 0) {
        free(buf);
        return ath_NULL;
    }
    size_t lo = 0, hi = len;
    if (clen > 0) {
        if (side <= 0)
            while (lo < hi && memchr(cbuf, (unsigned char)buf[lo], clen)) lo++;
        if (side >= 0)
            while (hi > lo && memchr(cbuf, (unsigned char)buf[hi - 1], clen)) hi--;
    }
    ath_obj *result = ath_buf_to_string(buf + lo, hi - lo);
    free(buf); free(cbuf);
    ath_inherit_lifetime(result, s, chars);
    return result;
}

ath_obj *ath_strip_chars(ath_obj *s, ath_obj *chars)  { return ath_strip_chars_impl(s, chars,  0); }
ath_obj *ath_lstrip_chars(ath_obj *s, ath_obj *chars) { return ath_strip_chars_impl(s, chars, -1); }
ath_obj *ath_rstrip_chars(ath_obj *s, ath_obj *chars) { return ath_strip_chars_impl(s, chars, +1); }

// Pad S to width with custom fill, pair (WIDTH, FILL); fill is first char of FILL, empty FILL born dead. No-op when S already wide
static ath_obj *ath_pad_with_impl(ath_obj *s, ath_obj *pair, int on_left) {
    int s_empty = (s == NULL || s == ath_NULL);
    if (!s_empty && !ath_is_alive(s)) return ath_alloc_dead();
    if (pair == NULL || pair == ath_NULL || !ath_is_alive(pair)) return ath_alloc_dead();
    ath_obj *width_obj, *fill_obj;
    ath_decompose(pair, &width_obj, &fill_obj);
    if (width_obj == NULL || !ath_is_alive(width_obj) || width_obj->num_kind != ATH_NUM_INT)
        return ath_alloc_dead();
    if (width_obj->num.i < 0) return ath_alloc_dead();

    char *fbuf = NULL;
    size_t flen = 0;
    if (ath_string_slurp(fill_obj, &fbuf, &flen) != 0) return ath_alloc_dead();
    // empty fill
    if (flen == 0) { free(fbuf); return ath_alloc_dead(); }
    char fill = fbuf[0];
    free(fbuf);

    char *buf = NULL;
    size_t len = 0;
    if (!s_empty && ath_string_slurp(s, &buf, &len) != 0) return ath_alloc_dead();
    size_t width = (size_t)width_obj->num.i;
    size_t pad = (len >= width) ? 0 : (width - len);
    size_t total = len + pad;
    if (total == 0) { free(buf); return ath_NULL; }
    char *out = (char *)malloc(total);
    if (!out) { free(buf); fputs("ath: out of memory\n", stderr); exit(1); }
    if (on_left) {
        memset(out, fill, pad);
        if (len > 0) memcpy(out + pad, buf, len);
    } else {
        if (len > 0) memcpy(out, buf, len);
        memset(out + len, fill, pad);
    }
    free(buf);
    ath_obj *result = ath_buf_to_string(out, total);
    free(out);
    ath_inherit_lifetime(result, s, pair);
    return result;
}

ath_obj *ath_pad_left_with(ath_obj *s, ath_obj *pair)  { return ath_pad_with_impl(s, pair, 1); }
ath_obj *ath_pad_right_with(ath_obj *s, ath_obj *pair) { return ath_pad_with_impl(s, pair, 0); }

// Fold payloads of LIST elements; every element must be alive and payload-bearing else born dead (so SUM over string dies)
// Identity 0 for sum, 1 for product; empty list yields identity; overflow born dead
static ath_obj *ath_fold_num(ath_obj *list, int is_product) {
    // Accumulate int64 (overflow-checked) until FLOAT element appears, then promote to double
    int is_float = 0;
    int64_t acc_i = is_product ? 1 : 0;
    double acc_f = is_product ? 1.0 : 0.0;
    ath_obj *cur = list;
    while (cur != NULL && cur != ath_NULL && ath_is_alive(cur)) {
        ath_obj *l, *r;
        ath_decompose(cur, &l, &r);
        // BIG fold elements: phase limit
        if (l == NULL || !ath_is_alive(l) || !ath_has_value(l)
                || l->num_kind == ATH_NUM_BIG)
            return ath_alloc_dead_number();
        if (!is_float && l->num_kind == ATH_NUM_FLOAT) {
            acc_f = (double)acc_i;
            is_float = 1;
        }
        if (is_float) {
            double v = ath_as_double(l);
            acc_f = is_product ? acc_f * v : acc_f + v;
        } else if (is_product) {
            if (__builtin_mul_overflow(acc_i, l->num.i, &acc_i))
                return ath_alloc_dead_number();
        } else {
            if (__builtin_add_overflow(acc_i, l->num.i, &acc_i))
                return ath_alloc_dead_number();
        }
        cur = r;
    }
    ath_obj *out = is_float ? ath_alloc_float(acc_f) : ath_alloc_number(acc_i);
    ath_inherit_lifetime(out, list, NULL);
    return out;
}

ath_obj *ath_sum(ath_obj *list, ath_obj *unused)     { (void)unused; return ath_fold_num(list, 0); }
ath_obj *ath_product(ath_obj *list, ath_obj *unused) { (void)unused; return ath_fold_num(list, 1); }

// MAXIMUM / MINIMUM element payload; empty list born dead (no extremum); non-payload or dead element born dead
static ath_obj *ath_extremum(ath_obj *list, int is_max) {
    int seen = 0, is_float = 0;
    int64_t best_i = 0;
    double best_f = 0.0;
    ath_obj *cur = list;
    while (cur != NULL && cur != ath_NULL && ath_is_alive(cur)) {
        ath_obj *l, *r;
        ath_decompose(cur, &l, &r);
        // BIG fold elements: phase limit
        if (l == NULL || !ath_is_alive(l) || !ath_has_value(l)
                || l->num_kind == ATH_NUM_BIG)
            return ath_alloc_dead_number();
        if (!is_float && l->num_kind == ATH_NUM_FLOAT) {
            best_f = (double)best_i;
            is_float = 1;
        }
        if (is_float) {
            double v = ath_as_double(l);
            if (!seen) { best_f = v; seen = 1; }
            else if (is_max ? (v > best_f) : (v < best_f)) best_f = v;
        } else {
            int64_t v = l->num.i;
            if (!seen) { best_i = v; seen = 1; }
            else if (is_max ? (v > best_i) : (v < best_i)) best_i = v;
        }
        cur = r;
    }
    if (!seen) return ath_alloc_dead_number();
    ath_obj *out = is_float ? ath_alloc_float(best_f) : ath_alloc_number(best_i);
    ath_inherit_lifetime(out, list, NULL);
    return out;
}

ath_obj *ath_maximum(ath_obj *list, ath_obj *unused) { (void)unused; return ath_extremum(list, 1); }
ath_obj *ath_minimum(ath_obj *list, ath_obj *unused) { (void)unused; return ath_extremum(list, 0); }

// MEMBER: verdict alive iff some element of LIST carries payload equal to X's; X must have payload, non-payload elements skipped
ath_obj *ath_member(ath_obj *list, ath_obj *x) {
    if (x == NULL || !ath_is_alive(x) || !ath_has_value(x)) return ath_verdict_false();
    ath_obj *cur = list;
    while (cur != NULL && cur != ath_NULL && ath_is_alive(cur)) {
        ath_obj *l, *r;
        ath_decompose(cur, &l, &r);
        if (l != NULL && ath_is_alive(l) && ath_has_value(l)) {
            // Compare numerically with tower promotion; exact in int64 when both plain ints
            int eq = ath_either_float(l, x)
                         ? (ath_as_double(l) == ath_as_double(x))
                         : ath_either_big(l, x)
                             ? (ath_big_cmp(ath_as_bigint(l),
                                            ath_as_bigint(x)) == 0)
                             : (l->num.i == x->num.i);
            if (eq) return ath_verdict_true(list, x);
        }
        cur = r;
    }
    return ath_verdict_false();
}

// TAKE: fresh list of first N elements (all of LIST when N >= length); N<0 or no payload -> dead, N==0 -> NULL, dead LIST -> dead
ath_obj *ath_take(ath_obj *list, ath_obj *n) {
    if (n == NULL || !ath_is_alive(n) || n->num_kind != ATH_NUM_INT || n->num.i < 0)
        return ath_alloc_dead();
    if (list != NULL && list != ath_NULL && !ath_is_alive(list)) return ath_alloc_dead();
    if (n->num.i == 0) return ath_NULL;
    int64_t len = ath_spine_length(list);
    if (len == 0) return ath_NULL;
    ath_obj **buf = (ath_obj **)calloc((size_t)len, sizeof(ath_obj *));
    if (!buf) { fputs("ath: out of memory\n", stderr); exit(1); }
    int64_t got = ath_collect_spine(list, buf, len);
    int64_t take = n->num.i < got ? n->num.i : got;
    ath_obj *out = ath_build_spine(buf, take);
    free(buf);
    ath_inherit_lifetime(out, list, n);
    return out;
}

// DROP: fresh list of all but first N elements; N<0 or no payload -> dead, N >= length -> NULL, dead LIST -> dead
ath_obj *ath_drop(ath_obj *list, ath_obj *n) {
    if (n == NULL || !ath_is_alive(n) || n->num_kind != ATH_NUM_INT || n->num.i < 0)
        return ath_alloc_dead();
    if (list != NULL && list != ath_NULL && !ath_is_alive(list)) return ath_alloc_dead();
    int64_t len = ath_spine_length(list);
    if (n->num.i >= len) return ath_NULL;
    ath_obj **buf = (ath_obj **)calloc((size_t)len, sizeof(ath_obj *));
    if (!buf) { fputs("ath: out of memory\n", stderr); exit(1); }
    int64_t got = ath_collect_spine(list, buf, len);
    ath_obj *out = ath_build_spine(buf + n->num.i, got - n->num.i);
    free(buf);
    ath_inherit_lifetime(out, list, n);
    return out;
}

// ALL_OF: verdict alive iff every element of LIST alive (n-ary AND); folds ath_and from always-alive identity into dep-tracked tree
// Empty list vacuously alive
ath_obj *ath_all_of(ath_obj *list, ath_obj *unused) {
    (void)unused;
    // vacuous-true identity
    ath_obj *acc = ath_alloc_alive();
    ath_obj *cur = list;
    while (cur != NULL && cur != ath_NULL && ath_is_alive(cur)) {
        ath_obj *l, *r;
        ath_decompose(cur, &l, &r);
        acc = ath_and(acc, l);
        cur = r;
    }
    return acc;
}

// ANY_OF: verdict alive iff some element of LIST alive (n-ary OR); folds ath_or from dead identity, dep-tracked. Empty list dead
ath_obj *ath_any_of(ath_obj *list, ath_obj *unused) {
    (void)unused;
    // vacuous-false identity
    ath_obj *acc = ath_alloc_dead();
    ath_obj *cur = list;
    while (cur != NULL && cur != ath_NULL && ath_is_alive(cur)) {
        ath_obj *l, *r;
        ath_decompose(cur, &l, &r);
        acc = ath_or(acc, l);
        cur = r;
    }
    return acc;
}

// Iteration count for loop/every loops: N's count if alive, payload-bearing, non-negative; else 0
// FLOAT floored toward zero; BIG clamps to INT64_MAX when positive
int64_t ath_count_of(ath_obj *n) {
    if (n == NULL || !ath_is_alive(n) || !ath_has_value(n)) return 0;
    if (n->num_kind == ATH_NUM_FLOAT) {
        double v = floor(n->num.f);
        // negative or nan -> 0
        if (!(v >= 0.0)) return 0;
        // clamp beyond int64 range
        if (v >= 9.2e18) return INT64_MAX;
        return (int64_t)v;
    }
    if (n->num_kind == ATH_NUM_BIG) {
        int64_t v;
        // sticky small bignum
        if (ath_big_fits_i64(n->num.b, &v))
            return v < 0 ? 0 : v;
        return n->num.b->sign > 0 ? INT64_MAX : 0;
    }
    if (n->num.i < 0) return 0;
    return n->num.i;
}

_Noreturn void ath_halt(void) {
    fflush(stdout);
    exit(0);
}
