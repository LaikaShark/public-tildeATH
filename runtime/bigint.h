#ifndef ATH_BIGINT_H
#define ATH_BIGINT_H

#include <stddef.h>
#include <stdint.h>

/* Minimal arbitrary-precision signed integer (SPEC §4.8, bignum phase).
 * Sign-magnitude: magnitude is a little-endian array of base-2^32 limbs,
 * normalized so the top limb is non-zero and sign is 0 iff the value is 0.
 *
 * Like every runtime allocation, bigints and their limb arrays are
 * malloc'd and never freed — the allocate-and-leak model of the rest of
 * the runtime (objects are never freed either). Constructors return NULL
 * only on allocation failure or malformed input; callers map NULL to a
 * born-dead result. All operands are read-only. */
typedef struct {
    int       sign;    /* -1, 0, +1; 0 iff the value is zero */
    size_t    len;     /* significant limbs (0 iff zero)     */
    uint32_t *limbs;   /* little-endian base 2^32; NULL iff len==0 */
} ath_bigint;

ath_bigint *ath_big_from_i64(int64_t v);
ath_bigint *ath_big_from_decimal(const char *s); /* optional '-'; NULL if malformed */
ath_bigint *ath_big_copy(const ath_bigint *a);

ath_bigint *ath_big_add(const ath_bigint *a, const ath_bigint *b);
ath_bigint *ath_big_sub(const ath_bigint *a, const ath_bigint *b);
ath_bigint *ath_big_mul(const ath_bigint *a, const ath_bigint *b);
ath_bigint *ath_big_neg(const ath_bigint *a);
ath_bigint *ath_big_abs(const ath_bigint *a);

/* Truncated division (quotient toward zero, remainder takes the sign of a).
 * Returns 0 on success and writes *q and/or *r (either may be NULL to skip);
 * returns nonzero if b is zero (then *q,*r are untouched). */
int ath_big_divmod(const ath_bigint *a, const ath_bigint *b,
                   ath_bigint **q, ath_bigint **r);

int    ath_big_cmp(const ath_bigint *a, const ath_bigint *b); /* -1/0/1 */
int    ath_big_is_zero(const ath_bigint *a);
int    ath_big_fits_i64(const ath_bigint *a, int64_t *out);   /* 1 if it fits */
double ath_big_to_double(const ath_bigint *a);
char  *ath_big_to_decimal(const ath_bigint *a);               /* malloc'd string */

#endif /* ATH_BIGINT_H */
