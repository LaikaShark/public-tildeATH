/* Minimal arbitrary-precision signed integer (SPEC §4.8, bignum phase).
 * Sign-magnitude, base-2^32 little-endian limbs. Schoolbook algorithms —
 * correctness over speed; sizes are bounded by source literals and the
 * results of explicit bignum arithmetic. Allocate-and-leak, like the rest
 * of the runtime. */

#include "bigint.h"

#include <stdlib.h>
#include <string.h>

#define LIMB_BASE 4294967296.0 /* 2^32 */

/* --- allocation / normalization ------------------------------------- */

static ath_bigint *big_alloc(size_t len) {
    ath_bigint *b = (ath_bigint *)malloc(sizeof *b);
    if (!b) return NULL;
    b->sign = 0;
    b->len = len;
    b->limbs = NULL;
    if (len) {
        b->limbs = (uint32_t *)calloc(len, sizeof(uint32_t));
        if (!b->limbs) { free(b); return NULL; }
    }
    return b;
}

/* Strip leading zero limbs; a zero magnitude forces sign 0. Never forces a
 * non-zero sign — magnitude builders set sign before calling. */
static void big_normalize(ath_bigint *b) {
    while (b->len > 0 && b->limbs[b->len - 1] == 0) b->len--;
    if (b->len == 0) b->sign = 0;
}

ath_bigint *ath_big_copy(const ath_bigint *a) {
    ath_bigint *r = big_alloc(a->len);
    if (!r) return NULL;
    r->sign = a->sign;
    if (a->len) memcpy(r->limbs, a->limbs, a->len * sizeof(uint32_t));
    return r;
}

/* --- magnitude primitives (sign ignored) ---------------------------- */

static int mag_cmp(const uint32_t *a, size_t an, const uint32_t *b, size_t bn) {
    if (an != bn) return an < bn ? -1 : 1;
    for (size_t i = an; i-- > 0;) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

/* |a| + |b|, result sign set to +1 (caller adjusts). */
static ath_bigint *mag_add(const uint32_t *a, size_t an,
                           const uint32_t *b, size_t bn) {
    if (an < bn) { const uint32_t *t = a; a = b; b = t;
                   size_t tn = an; an = bn; bn = tn; }
    ath_bigint *r = big_alloc(an + 1);
    if (!r) return NULL;
    uint64_t carry = 0;
    for (size_t i = 0; i < an; i++) {
        uint64_t s = (uint64_t)a[i] + (i < bn ? b[i] : 0) + carry;
        r->limbs[i] = (uint32_t)s;
        carry = s >> 32;
    }
    r->limbs[an] = (uint32_t)carry;
    r->sign = 1;
    big_normalize(r);
    return r;
}

/* |a| - |b|, requires |a| >= |b|. Result sign +1 (caller adjusts). */
static ath_bigint *mag_sub(const uint32_t *a, size_t an,
                           const uint32_t *b, size_t bn) {
    ath_bigint *r = big_alloc(an);
    if (!r) return NULL;
    int64_t borrow = 0;
    for (size_t i = 0; i < an; i++) {
        int64_t d = (int64_t)a[i] - (int64_t)(i < bn ? b[i] : 0) - borrow;
        if (d < 0) { d += ((int64_t)1 << 32); borrow = 1; } else borrow = 0;
        r->limbs[i] = (uint32_t)d;
    }
    r->sign = 1;
    big_normalize(r);
    return r;
}

static ath_bigint *mag_mul(const uint32_t *a, size_t an,
                           const uint32_t *b, size_t bn) {
    if (an == 0 || bn == 0) return ath_big_from_i64(0);
    ath_bigint *r = big_alloc(an + bn);
    if (!r) return NULL;
    for (size_t i = 0; i < an; i++) {
        uint64_t carry = 0;
        for (size_t j = 0; j < bn; j++) {
            uint64_t cur = (uint64_t)r->limbs[i + j]
                         + (uint64_t)a[i] * b[j] + carry;
            r->limbs[i + j] = (uint32_t)cur;
            carry = cur >> 32;
        }
        r->limbs[i + bn] += (uint32_t)carry;
    }
    r->sign = 1;
    big_normalize(r);
    return r;
}

/* (|r| << 1) | bit, magnitude. */
static ath_bigint *mag_shl1_or(const ath_bigint *r, int bit) {
    ath_bigint *res = big_alloc(r->len + 1);
    if (!res) return NULL;
    uint32_t carry = bit ? 1u : 0u;
    for (size_t i = 0; i < r->len; i++) {
        uint32_t v = r->limbs[i];
        res->limbs[i] = (v << 1) | carry;
        carry = v >> 31;
    }
    res->limbs[r->len] = carry;
    res->sign = 1;
    big_normalize(res);
    return res;
}

/* --- constructors --------------------------------------------------- */

ath_bigint *ath_big_from_i64(int64_t v) {
    ath_bigint *b = big_alloc(2);
    if (!b) return NULL;
    uint64_t mag;
    if (v < 0)      { b->sign = -1; mag = -(uint64_t)v; } /* -(uint64_t)INT64_MIN ok */
    else if (v > 0) { b->sign =  1; mag = (uint64_t)v; }
    else            { b->sign =  0; mag = 0; }
    b->limbs[0] = (uint32_t)mag;
    b->limbs[1] = (uint32_t)(mag >> 32);
    big_normalize(b);
    return b;
}

ath_bigint *ath_big_from_decimal(const char *s) {
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    if (*s == '\0') return NULL;
    ath_bigint *acc = ath_big_from_i64(0);
    ath_bigint *ten = ath_big_from_i64(10);
    if (!acc || !ten) return NULL;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return NULL;
        ath_bigint *m = ath_big_mul(acc, ten);
        ath_bigint *d = ath_big_from_i64(*s - '0');
        if (!m || !d) return NULL;
        acc = ath_big_add(m, d);
        if (!acc) return NULL;
    }
    if (sign < 0 && acc->sign != 0) acc->sign = -1;
    return acc;
}

/* --- signed arithmetic ---------------------------------------------- */

ath_bigint *ath_big_add(const ath_bigint *a, const ath_bigint *b) {
    if (a->sign == 0) return ath_big_copy(b);
    if (b->sign == 0) return ath_big_copy(a);
    if (a->sign == b->sign) {
        ath_bigint *r = mag_add(a->limbs, a->len, b->limbs, b->len);
        if (r && r->sign != 0) r->sign = a->sign;
        return r;
    }
    int c = mag_cmp(a->limbs, a->len, b->limbs, b->len);
    if (c == 0) return ath_big_from_i64(0);
    ath_bigint *r;
    if (c > 0) { r = mag_sub(a->limbs, a->len, b->limbs, b->len);
                 if (r && r->sign != 0) r->sign = a->sign; }
    else       { r = mag_sub(b->limbs, b->len, a->limbs, a->len);
                 if (r && r->sign != 0) r->sign = b->sign; }
    return r;
}

ath_bigint *ath_big_sub(const ath_bigint *a, const ath_bigint *b) {
    ath_bigint nb = { -b->sign, b->len, b->limbs }; /* read-only negated view */
    return ath_big_add(a, &nb);
}

ath_bigint *ath_big_mul(const ath_bigint *a, const ath_bigint *b) {
    ath_bigint *r = mag_mul(a->limbs, a->len, b->limbs, b->len);
    if (r && r->sign != 0) r->sign = (a->sign * b->sign < 0) ? -1 : 1;
    return r;
}

ath_bigint *ath_big_neg(const ath_bigint *a) {
    ath_bigint *r = ath_big_copy(a);
    if (r) r->sign = -r->sign;
    return r;
}

ath_bigint *ath_big_abs(const ath_bigint *a) {
    ath_bigint *r = ath_big_copy(a);
    if (r && r->sign < 0) r->sign = 1;
    return r;
}

int ath_big_divmod(const ath_bigint *a, const ath_bigint *b,
                   ath_bigint **qout, ath_bigint **rout) {
    if (b->sign == 0) return 1; /* division by zero */
    /* Binary long division over magnitudes. */
    ath_bigint *q = big_alloc(a->len ? a->len : 1);
    ath_bigint *r = ath_big_from_i64(0);
    if (!q || !r) return 1;
    size_t abits = a->len * 32;
    for (size_t i = abits; i-- > 0;) {
        int bit = (int)((a->limbs[i / 32] >> (i % 32)) & 1u);
        ath_bigint *nr = mag_shl1_or(r, bit);
        if (!nr) return 1;
        if (mag_cmp(nr->limbs, nr->len, b->limbs, b->len) >= 0) {
            ath_bigint *sub = mag_sub(nr->limbs, nr->len, b->limbs, b->len);
            if (!sub) return 1;
            r = sub;
            q->limbs[i / 32] |= (1u << (i % 32));
        } else {
            r = nr;
        }
    }
    q->sign = 1; big_normalize(q);
    big_normalize(r);
    if (q->sign != 0) q->sign = (a->sign * b->sign < 0) ? -1 : 1;
    if (r->sign != 0) r->sign = a->sign; /* truncated: remainder takes a's sign */
    if (qout) *qout = q;
    if (rout) *rout = r;
    return 0;
}

/* --- comparisons / conversions -------------------------------------- */

int ath_big_cmp(const ath_bigint *a, const ath_bigint *b) {
    if (a->sign != b->sign) return a->sign < b->sign ? -1 : 1;
    if (a->sign == 0) return 0;
    int c = mag_cmp(a->limbs, a->len, b->limbs, b->len);
    return a->sign > 0 ? c : -c;
}

int ath_big_is_zero(const ath_bigint *a) { return a->sign == 0; }

int ath_big_fits_i64(const ath_bigint *a, int64_t *out) {
    if (a->sign == 0) { if (out) *out = 0; return 1; }
    if (a->len > 2) return 0;
    uint64_t mag = a->limbs[0];
    if (a->len > 1) mag |= (uint64_t)a->limbs[1] << 32;
    if (a->sign > 0) {
        if (mag > (uint64_t)INT64_MAX) return 0;
        if (out) *out = (int64_t)mag;
    } else {
        if (mag > (uint64_t)INT64_MAX + 1) return 0;
        if (out) *out = (mag == (uint64_t)INT64_MAX + 1)
                            ? INT64_MIN : -(int64_t)mag;
    }
    return 1;
}

double ath_big_to_double(const ath_bigint *a) {
    double d = 0.0;
    for (size_t i = a->len; i-- > 0;) d = d * LIMB_BASE + (double)a->limbs[i];
    return a->sign < 0 ? -d : d;
}

/* Divide a magnitude (in place) by a small divisor, returning the remainder. */
static uint32_t mag_divmod_small(uint32_t *a, size_t n, uint32_t d) {
    uint64_t rem = 0;
    for (size_t i = n; i-- > 0;) {
        uint64_t cur = (rem << 32) | a[i];
        a[i] = (uint32_t)(cur / d);
        rem = cur % d;
    }
    return (uint32_t)rem;
}

char *ath_big_to_decimal(const ath_bigint *a) {
    if (a->sign == 0) {
        char *z = (char *)malloc(2);
        if (z) { z[0] = '0'; z[1] = '\0'; }
        return z;
    }
    size_t n = a->len;
    uint32_t *tmp = (uint32_t *)malloc(n * sizeof(uint32_t));
    if (!tmp) return NULL;
    memcpy(tmp, a->limbs, n * sizeof(uint32_t));
    size_t tn = n;
    /* Each limb is < 2^32 < 10^10, so n limbs hold < 10^(10n) — n*10 digits
     * is a safe upper bound. */
    size_t cap = n * 10 + 3;
    char *digits = (char *)malloc(cap);
    if (!digits) { free(tmp); return NULL; }
    size_t di = 0;
    while (tn > 0) {
        uint32_t rem = mag_divmod_small(tmp, tn, 10u);
        digits[di++] = (char)('0' + rem);
        while (tn > 0 && tmp[tn - 1] == 0) tn--;
    }
    char *out = (char *)malloc(di + 2);
    if (!out) { free(tmp); free(digits); return NULL; }
    size_t oi = 0;
    if (a->sign < 0) out[oi++] = '-';
    for (size_t i = di; i-- > 0;) out[oi++] = digits[i];
    out[oi] = '\0';
    free(tmp);
    free(digits);
    return out;
}
