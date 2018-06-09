/**
 * @summary The main header file for the Platform Abstraction Layer (PAL).
 * This file detects the current platform and compiler and includes the 
 * appropriate platform-specific headers.
 */
#ifndef __PAL_H__
#define __PAL_H__

#ifndef PAL_NO_INCLUDES
#include <assert.h>
#include <stddef.h>
#include <uchar.h>
#endif

/* @summary If __STDC_UTF_16__ is defined (in uchar.h) then use existing 
 * char16_t. Otherwise, define it ourselves to be a 16-bit unsigned integer.
 */
#ifndef __STDC_UTF_16__
    typedef unsigned short                    char16_t;
#endif

/* @summary If __STDC_UTF_32__ is defined (in uchar.h) then use existing 
 * char32_t. Otherwise, define it ourselves to be a 32-bit unsigned integer.
 */
#ifndef __STDC_UTF_32__
    typedef unsigned int                      char32_t;
#endif

/* @summary Define values used to identify the current target platform.
 */
#ifndef PAL_PLATFORM_CONSTANTS
    #define PAL_PLATFORM_CONSTANTS
    #define PAL_PLATFORM_UNKNOWN              0
    #define PAL_PLATFORM_iOS                  1
    #define PAL_PLATFORM_ANDROID              2
    #define PAL_PLATFORM_WIN32                3
    #define PAL_PLATFORM_WINRT                4
    #define PAL_PLATFORM_MACOS                5
    #define PAL_PLATFORM_LINUX                6
#endif

/* @summary Define values used to identify the current compiler.
 * GNUC covers gcc, clang and Intel.
 */
#ifndef PAL_COMPILER_CONSTANTS
    #define PAL_COMPILER_CONSTANTS
    #define PAL_COMPILER_UNKNOWN              0
    #define PAL_COMPILER_MSVC                 1
    #define PAL_COMPILER_GNUC                 2
#endif

/* @summary Define values used to identify the target processor architecture.
 * Only 64-bit architectures are supported, due to reliance on 64-bit atomic operations.
 */
#ifndef PAL_ARCHITECTURE_CONSTANTS
    #define PAL_ARCHITECTURE_CONSTANTS
    #define PAL_ARCHITECTURE_UNKNOWN          0
    #define PAL_ARCHITECTURE_X64              1
    #define PAL_ARCHITECTURE_ARM64            2
    #define PAL_ARCHITECTURE_PPC              3
#endif

/* @summary Define values used to identify the endianess of the target system.
 */
#ifndef PAL_ENDIANESS_CONSTANTS
    #define PAL_ENDIANESS_CONSTANTS
    #define PAL_ENDIANESS_UNKNOWN             0
    #define PAL_ENDIANESS_LSB_FIRST           1
    #define PAL_ENDIANESS_MSB_FIRST           2
#endif

/* @summary Define values used to specify the ordering of operations.
 * PAL_ATOMIC_SYNC_RELAXED implies no inter-thread ordering constraints.
 * PAL_ATOMIC_SYNC_CONSUME imposes a happens-before constraint from a release-store and prevents load-hoisting.
 * PAL_ATOMIC_SYNC_ACQUIRE imposes a happens-before constraint from a release-store and prevents load-hoisting.
 * PAL_ATOMIC_SYNC_RELEASE imposes a happens-before constraint to an acquire-load and prevents store-sinking.
 * PAL_ATOMIC_SYNC_ACQ_REL combines ACQUIRE and RELEASE, so loads won't be hoisted and stores won't be sunk.
 * PAL_ATOMIC_SYNC_SEQ_CST enforces total ordering with all other SEQ_CST operations.
 */
#ifndef PAL_ATOMIC_SYNC_CONSTANTS
    #define PAL_ATOMIC_SYNC_CONSTANTS
    #ifdef _MSC_VER
        #define PAL_ATOMIC_SYNC_RELAXED       0
        #define PAL_ATOMIC_SYNC_CONSUME       1
        #define PAL_ATOMIC_SYNC_ACQUIRE       2
        #define PAL_ATOMIC_SYNC_RELEASE       3
        #define PAL_ATOMIC_SYNC_ACQ_REL       4
        #define PAL_ATOMIC_SYNC_SEQ_CST       5
    #elif defined(__GNUC__)
        #define PAL_ATOMIC_SYNC_RELAXED       __ATOMIC_RELAXED
        #define PAL_ATOMIC_SYNC_CONSUME       __ATOMIC_CONSUME
        #define PAL_ATOMIC_SYNC_ACQUIRE       __ATOMIC_ACQUIRE
        #define PAL_ATOMIC_SYNC_RELEASE       __ATOMIC_RELEASE
        #define PAL_ATOMIC_SYNC_ACQ_REL       __ATOMIC_ACQ_REL
        #define PAL_ATOMIC_SYNC_SEQ_CST       __ATOMIC_SEQ_CST
    #else
        #error pal.h: Need to define PAL_ATOMIC_SYNC_ constants for your platform.
    #endif
#endif

/* @summary Define the size of a processor cacheline for the target architecture.
 * All modern CPUs have 64-byte cachelines.
 */
#ifndef PAL_CACHELINE_SIZE
    #define PAL_CACHELINE_SIZE                64
#endif

/* @summary The PAL_CACHELINE_ALIGN macro can be used to tag a structure that should begin on a cacheline boundary.
 */
#ifndef PAL_CACHELINE_ALIGN
    #define PAL_CACHELINE_ALIGN               PAL_STRUCT_ALIGN(PAL_CACHELINE_SIZE)
#endif

/* @summary Define the maximum amount of opaque "user data" bytes that can be stored with a memory allocator.
 */
#ifndef PAL_MEMORY_ALLOCATOR_MAX_USER
    #define PAL_MEMORY_ALLOCATOR_MAX_USER     64
#endif

/* @summary Define the maximum number of power-of-two buckets supported in the general-purpose memory allocator.
 * This is the maximum number of power-of-two steps between the minimum and maximum allocation size.
 */
#ifndef PAL_MEMORY_ALLOCATOR_MAX_LEVELS
    #define PAL_MEMORY_ALLOCATOR_MAX_LEVELS   16
#endif

/* @summary Define the default spin count associated with PAL synchronization primitives.
 * This value should be tuned for each individual synchronization object.
 */
#ifndef PAL_DEFAULT_SPIN_COUNT
    #define PAL_DEFAULT_SPIN_COUNT            4096
#endif

/* @summary Define the size, in bytes, of the seed data for the psuedo-random number generator implementation.
 */
#ifndef PAL_PRNG_SEED_SIZE
    #define PAL_PRNG_SEED_SIZE                (16 * sizeof(pal_uint32_t))
#endif

/* @summary Define the default stack size for a task pool compute worker thread.
 * The stack size is specified in bytes. The default is 64KB.
 */
#ifndef PAL_WORKER_STACK_SIZE_DEFAULT
    #define PAL_WORKER_STACK_SIZE_DEFAULT     (64 * 1024)
#endif

/* @summary The PAL_TARGET_COMPILER preprocessor value can be used to specify or 
 * query the current compiler.
 */
#ifndef PAL_TARGET_COMPILER
    #define PAL_TARGET_COMPILER               PAL_COMPILER_UNKNOWN
#endif

/* @summary The PAL_TARGET_PLATFORM preprocessor value can be used to specify or 
 * query the current target platform.
 */
#ifndef PAL_TARGET_PLATFORM
    #define PAL_TARGET_PLATFORM               PAL_PLATFORM_UNKNOWN
#endif

/* @summary The PAL_TARGET_ARCHITECTURE preprocessor value can be used to specify
 * or query the current target processor architecture.
 */
#ifndef PAL_TARGET_ARCHITECTURE
    #define PAL_TARGET_ARCHITECTURE           PAL_ARCHITECTURE_UNKNOWN
#endif

/* @summary The PAL_SYSTEM_ENDIANESS preprocessor value can be used to specify or 
 * query the endianess of the host system. Default to little endian since most 
 * processor architectures these days are configurable. GCC defines the __BYTE_ORDER__
 * preprocessor value that can be used to test at compile time.
 */
#ifndef PAL_SYSTEM_ENDIANESS
    #if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
        #define PAL_SYSTEM_ENDIANESS          PAL_ENDIANESS_MSB_FIRST
    #else
        #define PAL_SYSTEM_ENDIANESS          PAL_ENDIANESS_LSB_FIRST
    #endif
#endif

/* @summary Perform compiler detection based on preprocessor directives.
 */
#if PAL_TARGET_COMPILER == PAL_COMPILER_UNKNOWN
    #if   defined(_MSC_VER)
        #undef  PAL_TARGET_COMPILER
        #define PAL_TARGET_COMPILER           PAL_COMPILER_MSVC
    #elif defined(__GNUC__)
        #undef  PAL_TARGET_COMPILER
        #define PAL_TARGET_COMPILER           PAL_COMPILER_GNUC
    #else
        #error  pal.h: Failed to detect target compiler. Update compiler detection.
    #endif
#endif

/* @summary Perform processor architecture detection based on preprocessor directives.
 */
#if PAL_TARGET_ARCHITECTURE == PAL_ARCHITECTURE_UNKNOWN
    #if   defined(__aarch64__) || defined(_M_ARM64)
        #undef  PAL_TARGET_ARCHITECTURE
        #define PAL_TARGET_ARCHITECTURE       PAL_ARCHITECTURE_ARM64
    #elif defined(__arm__) || defined(_M_ARM)
        #error  pal.h: Only 64-bit ARM platforms are supported.
    #elif defined(__amd64__) || defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
        #undef  PAL_TARGET_ARCHITECTURE
        #define PAL_TARGET_ARCHITECTURE       PAL_ARCHITECTURE_X64
    #elif defined(__i386__) || defined(__i486__) || defined(__i586__) || defined(__i686__) || defined(_M_IX86) || defined(_X86_)
        #error  pal.h: Only 64-bit x86 platforms are supported.
    #elif defined(__ppc__) || defined(__powerpc__) || defined(__PPC__)
        #undef  PAL_TARGET_ARCHITECTURE
        #define PAL_TARGET_ARCHITECTURE       PAL_ARCHITECTURE_PPC
    #else
        #error  pal.h: Failed to detect target architecture. Update architecture detection.
    #endif
#endif

/* @summary Perform platform detection based on preprocessor directives.
 */
#if PAL_TARGET_PLATFORM == PAL_PLATFORM_UNKNOWN
    #if   defined(ANDROID)
        #undef  PAL_TARGET_PLATFORM
        #define PAL_TARGET_PLATFORM           PAL_PLATFORM_ANDROID
    #elif defined(__APPLE__)
        #include <TargetConditionals.h>
        #if   defined(TARGET_OS_IPHONE) || defined(TARGET_IPHONE_SIMULATOR)
            #undef  PAL_TARGET_PLATFORM
            #define PAL_TARGET_PLATFORM       PAL_PLATFORM_iOS
        #else
            #undef  PAL_TARGET_PLATFORM
            #define PAL_TARGET_PLATFORM       PAL_PLATFORM_MACOS
        #endif
    #elif defined(_WIN32) || defined(_WIN64) || defined(__cplusplus_winrt)
        #if   defined(__cplusplus_winrt)
            #undef  PAL_TARGET_PLATFORM
            #define PAL_TARGET_PLATFORM       PAL_PLATFORM_WINRT
        #else
            #undef  PAL_TARGET_PLATFORM
            #define PAL_TARGET_PLATFORM       PAL_PLATFORM_WIN32
        #endif
    #elif defined(__linux__) || defined(__gnu_linux__)
            #undef  PAL_TARGET_PLATFORM
            #define PAL_TARGET_PLATFORM       PAL_PLATFORM_LINUX
    #else
            #error  pal.h: Failed to detect target platform. Update platform detection.
    #endif
#endif

/* @summary Abstract away some commonly-used compiler directives.
 */
#if   PAL_TARGET_COMPILER == PAL_COMPILER_MSVC
    #define PAL_NEVER_INLINE                  __declspec(noinline)
    #define PAL_FORCE_INLINE                  __forceinline
    #define PAL_STRUCT_ALIGN(_x)              __declspec(align(_x))
    #define PAL_ALIGN_OF(_x)                  __alignof(_x)
    #define PAL_RESTRICT                      __restrict
    #define PAL_SHARED_EXPORT                 __declspec(dllexport)
    #define PAL_SHARED_IMPORT                 __declspec(dllimport)
    #define PAL_OFFSET_OF(_type, _field)      offsetof(_type, _field)
    #define PAL_UNUSED_ARG(_x)                (void)(_x)
    #define PAL_UNUSED_LOCAL(_x)              (void)(_x)
    #ifdef __cplusplus
        #define PAL_INLINE                    inline
    #else
        #define PAL_INLINE                  
    #endif
#elif PAL_TARGET_COMPILER == PAL_COMPILER_GNUC
    #define PAL_NEVER_INLINE                  __attribute__((noinline))
    #define PAL_FORCE_INLINE                  __attribute__((always_inline))
    #define PAL_STRUCT_ALIGN(_x)              __attribute__((aligned(_x)))
    #define PAL_ALIGN_OF(_x)                  __alignof__(_x)
    #define PAL_RESTRICT                      __restrict
    #define PAL_SHARED_EXPORT                 
    #define PAL_SHARED_IMPORT                 
    #define PAL_UNUSED_ARG(_x)                (void)(sizeof(_x))
    #define PAL_UNUSED_LOCAL(_x)              (void)(sizeof(_x))
    #define PAL_OFFSET_OF(_type, _field)      offsetof(_type, _field)
    #ifdef __cplusplus
        #define PAL_INLINE                    inline
    #else
        #define PAL_INLINE                  
    #endif
#endif

/* @summary Define standard sized types for platforms that don't provide an stdint.h.
 * Since the PAL is designed to be 64-bit only, size_t etc. are defined to be 64-bits.
 */
#ifndef PAL_STANDARD_TYPES
    #define PAL_STANDARD_TYPES
    #define PAL_WORDSIZE_BITS                 64
    #define PAL_WORDSIZE_BYTES                8
    #define PAL_WORDSIZE_SHIFT                6
    #define PAL_WORDSIZE_MASK                (PAL_WORDSIZE_BITS-1)
    #define PAL_WORDSIZE_ZERO                 0ULL
    #define PAL_WORDSIZE_ONE                  1ULL
    #define PAL_WORDSIZE_MAX                (~PAL_WORDSIZE_ZERO)
    typedef char                              pal_utf8_t;
    typedef char16_t                          pal_utf16_t;
    typedef char32_t                          pal_utf32_t;
    typedef signed   char                     pal_sint8_t;
    typedef unsigned char                     pal_uint8_t;
    typedef signed   short                    pal_sint16_t;
    typedef unsigned short                    pal_uint16_t;
    typedef signed   int                      pal_sint32_t;
    typedef unsigned int                      pal_uint32_t;
    typedef signed   long long                pal_sint64_t;
    typedef unsigned long long                pal_uint64_t;
    typedef float                             pal_float32_t;
    typedef double                            pal_float64_t;
    typedef signed   long long                pal_offset_t;
    typedef signed   long long                pal_ssize_t;
    typedef unsigned long long                pal_usize_t;
    typedef signed   long long                pal_sintptr_t;
    typedef unsigned long long                pal_uintptr_t;
    typedef signed   long long                pal_ptrdiff_t;
    #if   PAL_TARGET_PLATFORM == PAL_PLATFORM_WIN32 || PAL_TARGET_PLATFORM == PAL_PLATFORM_WINRT
    typedef pal_utf16_t                       pal_char_t;
    #else
    typedef pal_utf8_t                        pal_char_t;
    #endif /* PAL_TARGET_PLATFORM */
#endif /* !defined (PAL_STANDARD_TYPES) */

/* @summary #define PAL_STATIC to make all function declarations and definitions
 * static. This is useful if the library implementation needs to be included 
 * several times within a project.
 */
#ifdef  PAL_STATIC
    #define PAL_API(_rt)                      static _rt
#else
    #define PAL_API(_rt)                      extern _rt
#endif

/* @summary Assign a value to an output argument.
 * @param _dst A pointer to the destination location.
 * @param _val The value to assign to the destination location.
 */
#ifndef PAL_Assign
#define PAL_Assign(_dst, _val)                                                 \
    if ((_dst)) *(_dst) = (_val) 
#endif


#endif /* !defined(__PAL_H__) */
