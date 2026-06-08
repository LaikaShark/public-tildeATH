#ifndef ATH_BIGINT_H
#define ATH_BIGINT_H

#include <stddef.h>
#include <stdint.h>

// Arbitrary-precision signed integer. Sign-magnitude, little-endian base-2^32
// limbs, normalized: top limb nonzero, sign 0 iff value 0.
// Allocate-and-leak, never freed. Constructors return NULL on alloc failure or
// malformed input; callers map NULL to born-dead. Operands read-only.
typedef struct {
    // -1, 0, +1; 0 iff value zero
    int       sign;
    // significant limbs (0 iff zero)
    size_t    len;
    // little-endian base 2^32; NULL iff len==0
    uint32_t *limbs;
} ath_bigint;

ath_bigint *ath_big_from_i64(int64_t v);
// optional '-'; NULL if malformed
ath_bigint *ath_big_from_decimal(const char *s);
ath_bigint *ath_big_copy(const ath_bigint *a);

ath_bigint *ath_big_add(const ath_bigint *a, const ath_bigint *b);
ath_bigint *ath_big_sub(const ath_bigint *a, const ath_bigint *b);
ath_bigint *ath_big_mul(const ath_bigint *a, const ath_bigint *b);
ath_bigint *ath_big_neg(const ath_bigint *a);
ath_bigint *ath_big_abs(const ath_bigint *a);

// Truncated division: quotient toward zero, remainder takes sign of a.
// Returns 0 on success, writes *q and/or *r (NULL to skip); nonzero if b zero
// (then *q,*r untouched).
int ath_big_divmod(const ath_bigint *a, const ath_bigint *b,
                   ath_bigint **q, ath_bigint **r);

// -1/0/1
int    ath_big_cmp(const ath_bigint *a, const ath_bigint *b);
int    ath_big_is_zero(const ath_bigint *a);
// 1 if it fits
int    ath_big_fits_i64(const ath_bigint *a, int64_t *out);
double ath_big_to_double(const ath_bigint *a);
// malloc'd string
char  *ath_big_to_decimal(const ath_bigint *a);

#endif
