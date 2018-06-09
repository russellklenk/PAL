/**
 * @summary Implement the PAL entry points from pal_random.h.
 */
#include "pal_win32_random.h"

PAL_API(void)
PAL_RandomInit
(
    struct PAL_RANDOM *prng
)
{
    prng->Index = 0;
}

PAL_API(int)
PAL_RandomSeed
(
    struct PAL_RANDOM *prng, 
    void const   *seed_data, 
    pal_usize_t   seed_size
)
{   
    pal_usize_t            i;
    pal_usize_t            n = PAL_PRNG_SEED_SIZE / sizeof(pal_uint32_t);
    pal_uint32_t const *seed =(pal_uint32_t const*) seed_data;

    assert(seed_data != NULL);
    assert(seed_size >= PAL_PRNG_SEED_SIZE);
    (void) seed_size;
    for (i = 0; i < n; ++i) {
        prng->State[i] = seed[i];
    }
    prng->Index = 0;
    return 0;
}

PAL_API(pal_uint32_t)
PAL_Random_u32
(
    struct PAL_RANDOM *prng
)
{
    pal_uint32_t *s = prng->State;
    pal_uint32_t  n = prng->Index;
    pal_uint32_t  a = s[n];
    pal_uint32_t  b = 0;
    pal_uint32_t  c = s[(n + 13) & 15];
    pal_uint32_t  d = 0;
    b           = a ^ c ^ (a << 16) ^ (c << 15);
    c           = s[(n + 9)   & 15];
    c          ^= (c >> 11);
    a           = s[n] = b ^ c;
    d           = a ^ ((a << 5) & 0xDA442D24UL);
    n           = (n + 15) & 15;
    a           = s[n];
    s[n]        = a ^ b ^ d ^ (a << 2) ^ (b << 18) ^ (c << 28);
    prng->Index = n;
    return s[n];
}

PAL_API(pal_uint32_t)
PAL_RandomInRange_u32
(
    struct PAL_RANDOM *prng, 
    pal_uint32_t  min_value, 
    pal_uint32_t  max_value
)
{
    pal_uint64_t r = max_value - min_value; /* size of request range [min, max) */
    pal_uint64_t u = 0xFFFFFFFFUL;          /* PRNG inclusive upper bound */
    pal_uint64_t n = u + 1;                 /* size of PRNG range [0, UINT32_MAX] */
    pal_uint64_t i = n / r;                 /* # times whole of 'r' fits in 'n' */
    pal_uint64_t m = r * i;                 /* largest integer multiple of 'r'<='n' */
    pal_uint64_t x = 0;                     /* raw value from PRNG */
    do {
        x = PAL_Random_u32(prng);           /* x in [0, UINT32_MAX] */
    } while (x >= m);
    x /= i;                                 /* x -> [0, r) and [0, UINT32_MAX] */
    return (pal_uint32_t)(x + min_value);   /* x -> [min, max) */
}
