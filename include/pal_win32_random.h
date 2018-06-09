/**
 * @summary Define the platform-specific types and other internal bits for the 
 * Microsoft Windows Desktop platform.
 */
#ifndef __PAL_WIN32_RANDOM_H__
#define __PAL_WIN32_RANDOM_H__

#ifndef __PAL_RANDOM_H__
#include "pal_random.h"
#endif

#ifndef PAL_NO_INCLUDES
#include <Windows.h>
#endif

/* @summary Define the data used to manage the state of a psuedo-random number generator.
 * The PRNG cannot be accessed by multiple threads simultaneously.
 * The PRNG should NOT be used for cryptographic applications.
 * The PRNG implementation is consistent across platforms. 
 * The implemented algorithm is WELL512.
 */
typedef struct PAL_RANDOM {
    pal_uint32_t                       State[16];              /* The psuedo-random number generator state. */
    pal_uint32_t                       Index;                  /* The current index into the state array. */
    pal_uint32_t                       Pad[15];                /* Padding out to a cacheline boundary. */
} PAL_RANDOM;

#endif /* __PAL_WIN32_RANDOM_H__ */
