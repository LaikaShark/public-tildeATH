#define _POSIX_C_SOURCE 200809L

#include "ath_runtime.h"

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

_Noreturn void ath_halt(void) {
    fflush(stdout);
    exit(0);
}
