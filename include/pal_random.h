/**
 * @summary Define the PAL types and API entry points used for generating 
 * random numbers.
 */
#ifndef __PAL_RANDOM_H__
#define __PAL_RANDOM_H__

#ifndef __PAL_H__
#include "pal.h"
#endif

/* @summary Forward-declare the types exported by this module.
 * The type definitions are included in the platform-specific header.
 */
struct  PAL_RANDOM;

#ifdef __cplusplus
extern "C" {
#endif

/* @summary Initialize a psuedo-random number generator instance.
 * @param prng The pseudo-random number generator to initialize.
 */
PAL_API(void)
PAL_RandomInit
(
    struct PAL_RANDOM *prng
);

/* @summary Seed a pseudo-random number generator.
 * @param prng The pseudo-random number generator to seed.
 * @param seed_data The seed data, which must be at least PAL_PRNG_SEED_SIZE bytes.
 * @param seed_size The size of the seed data, in bytes, which must be at least PAL_PRNG_SEED_SIZE.
 * @return Zero if the PRNG is successfully seeded, or -1 if an error occurs.
 */
PAL_API(int)
PAL_RandomSeed
(
    struct PAL_RANDOM *prng, 
    void const   *seed_data, 
    pal_usize_t   seed_size
);

/* @summary Retrieve a 32-bit random unsigned integer in the range [0, UINT32_MAX].
 * @param prng The pseudo-random number generator state.
 * @return A value in [0, UINT32_MAX].
 */
PAL_API(pal_uint32_t)
PAL_Random_u32
(
    struct PAL_RANDOM *prng
);

/* @summary Retrieve a 32-bit random unsigned integer in the range [min_value, max_value).
 * @param prng The psuedo-random number generator state.
 * @param min_value The inclusive lower-bound of the range. The maximum allowable value is UINT32_MAX-1 (4294967294).
 * @param max_value The exclusive upper-bound of the range. The maximum allowable value is UINT32_MAX (4294967295).
 * @return A uniformly-distributed random value in the range [min_value, max_value).
 */
PAL_API(pal_uint32_t)
PAL_RandomInRange_u32
(
    struct PAL_RANDOM *prng, 
    pal_uint32_t  min_value, 
    pal_uint32_t  max_value
);

#ifdef __cplusplus
}; /* extern "C" */
#endif

/* @summary Include the appropriate platform-specific header.
 */
#if   PAL_TARGET_PLATFORM == PAL_PLATFORM_WIN32 || PAL_TARGET_PLATFORM == PAL_PLATFORM_WINRT
    #include "pal_win32_random.h"
#elif PAL_TARGET_PLATFORM == PAL_PLATFORM_LINUX || PAL_TARGET_PLATFORM == PAL_PLATFORM_ANDROID
    #include "pal_linux_random.h"
#elif PAL_TARGET_PLATFORM == PAL_PLATFORM_MACOS || PAL_TARGET_PLATFORM == PAL_PLATFORM_IOS
    #include "pal_apple_random.h"
#else
    #error   pal_random.h: No implementation of the abstraction layer for your platform.
#endif

#endif /* __PAL_RANDOM_H__ */
