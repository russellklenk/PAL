/**
 * @summary Define the PAL types and API entry points related to memory 
 * management. This includes routines commonly provided by the C runtime 
 * library such as memset, along with custom memory allocators that rely 
 * on the OS Virtual Memory manager, hashing functions, functions for reading 
 * and writing primitive data types with specific endianess and performing 
 * endian conversions, and functions work working with memory streams.
 */
#ifndef __PAL_MEMORY_H__
#define __PAL_MEMORY_H__

#ifndef __PAL_H__
#include "pal.h"
#endif

/* @summary Swap the bytes in a two-byte value.
 * @param _v The value to byte swap.
 * @return The byte swapped value.
 */
#ifndef PAL_ByteSwap2
#define PAL_ByteSwap2(_v)                                                      \
    ( (((_v) >> 8) & 0x00FF) |                                                 \
      (((_v) << 8) & 0xFF00) )
#endif

/* @summary Swap the bytes in a four-byte value.
 * @param _v The value to byte swap.
 * @return The byte swapped value.
 */
#ifndef PAL_ByteSwap4
#define PAL_ByteSwap4(_v)                                                      \
    ( (((_v) >> 24) & 0x000000FF) |                                            \
      (((_v) >>  8) & 0x0000FF00) |                                            \
      (((_v) <<  8) & 0x00FF0000) |                                            \
      (((_v) << 24) & 0xFF000000) )
#endif

/* @summary Swap the bytes in an eight-byte value.
 * @param _v The value to byte swap.
 * @return The byte swapped value.
 */
#ifndef PAL_ByteSwap8
#define PAL_ByteSwap8(_v)                                                      \
    ( (((_v) >> 56) & 0x00000000000000FFULL) |                                 \
      (((_v) >> 40) & 0x000000000000FF00ULL) |                                 \
      (((_v) >> 24) & 0x0000000000FF0000ULL) |                                 \
      (((_v) >>  8) & 0x00000000FF000000ULL) |                                 \
      (((_v) <<  8) & 0x000000FF00000000ULL) |                                 \
      (((_v) << 24) & 0x0000FF0000000000ULL) |                                 \
      (((_v) << 40) & 0x00FF000000000000ULL) |                                 \
      (((_v) << 56) & 0xFF00000000000000ULL) )
#endif


/* @summary Forward-declare the types exported by this module.
 * The type definitions are included in the platform-specific header.
 */
struct  PAL_MEMORY_BLOCK;
struct  PAL_MEMORY_ARENA;
struct  PAL_MEMORY_ARENA_INIT;
struct  PAL_MEMORY_ARENA_MARKER;
struct  PAL_MEMORY_ALLOCATOR;
struct  PAL_MEMORY_ALLOCATOR_INIT;
struct  PAL_MEMORY_ALLOCATOR_LEVEL;
struct  PAL_MEMORY_VIEW;
struct  PAL_MEMORY_LAYOUT;
struct  PAL_HOST_MEMORY_POOL;
struct  PAL_HOST_MEMORY_POOL_INIT;
struct  PAL_HOST_MEMORY_ALLOCATION;
struct  PAL_DYNAMIC_BUFFER;
struct  PAL_DYNAMIC_BUFFER_INIT;
struct  PAL_HANDLE_TABLE;
struct  PAL_HANDLE_TABLE_INIT;
struct  PAL_HANDLE_TABLE_CHUNK;
struct  PAL_HANDLE_TABLE_VISITOR_INIT;

/* @summary Define a type used to represent a generic object identifier.
 * The objects are managed by a PAL_HANDLE_TABLE instance. An object may have its data spread across one or more PAL_HANDLE_TABLE instances.
 * The same handle is used to identify the same object across all PAL_HANDLE_TABLE instances.
 */
typedef pal_uint32_t  PAL_HANDLE;

/* @summary Define the signature for a callback function invoked once for each non-empty chunk in a PAL_HANDLE_TABLE during a visit operation.
 * @param table The PAL_HANDLE_TABLE on which the visit operation is being performed.
 * @param chunk A PAL_HANDLE_TABLE_CHUNK describing a single chunk visited.
 * @param view A PAL_MEMORY_VIEW initialized for the specified chunk that can be used to access the chunk data streams.
 * @param context Opaque data passed through to the callback by the application.
 * @return Non-zero to continue enumeration, or zero to stop enumeration.
 */
typedef int  (*PAL_HandleTableChunkVisitor_Func)
(
    struct PAL_HANDLE_TABLE       *table, 
    struct PAL_HANDLE_TABLE_CHUNK *chunk, 
    struct PAL_MEMORY_VIEW         *view, 
    pal_uintptr_t                context
);

#ifdef __cplusplus
extern "C" {
#endif

/* @summary Scan a 64-bit machine word, starting from least-significant to most-significant bit, for the first set bit.
 * @param value The 64-bit value to search.
 * @param bit If the value contains a set bit, the zero-based index of the bit is stored at this location.
 * @return Non-zero if a set bit is found in the value, or zero of no set bit is found.
 * MSVC: _BitScanForward64. GCC: __builtin_ffs (different semantics).
 */
PAL_API(int)
PAL_BitScan_ui64_lsb
(
    pal_uint64_t value, 
    pal_uint32_t  *bit
);

/* @summary Scan a 64-bit machine word, starting from most-significant to least-significant bit, for the first set bit.
 * @param value The 64-bit value to search.
 * @param bit If the value contains a set bit, the zero-based index of the bit is stored at this location.
 * @return Non-zero if a set bit is found in the value, or zero of no set bit is found.
 * MSVC: _BitScanReverse64. GCC: __builtin_ffs (different semantics).
 */
PAL_API(int)
PAL_BitScan_ui64_msb
(
    pal_uint64_t value, 
    pal_uint32_t  *bit
);

/* @summary Mix the bits in a 32-bit value.
 * @param input The input value.
 * @return The input value with its bits mixed.
 */
PAL_API(pal_uint32_t)
PAL_BitsMix32
(
    pal_uint32_t input
);

/* @summary Mix the bits in a 64-bit value.
 * @param input The input value.
 * @return The input value with its bits mixed.
 */
PAL_API(pal_uint64_t)
PAL_BitsMix64
(
    pal_uint64_t input
);

/* @summary Compute a 32-bit non-cryptographic hash of some data.
 * @param data The data to hash.
 * @param length The number of bytes of data to hash.
 * @param seed An initial value used to seed the hash.
 * @return A 32-bit unsigned integer computed from the data.
 */
PAL_API(pal_uint32_t)
PAL_HashData32
(
    void const   *data, 
    pal_usize_t length, 
    pal_uint32_t  seed
);

/* @summary Compute a 64-bit non-cryptographic hash of some data.
 * @param data The data to hash.
 * @param length The number of bytes of data to hash.
 * @param seed An initial value used to seed the hash.
 * @return A 64-bit unsigned integer computed from the data.
 */
PAL_API(pal_uint64_t)
PAL_HashData64
(
    void const   *data, 
    pal_usize_t length, 
    pal_uint64_t  seed 
);

/* @summary Determine the endianess of the host CPU.
 * @return One of PAL_ENDIANESS_LSB_FIRST or PAL_ENDIANESS_MSB_FIRST.
 */
PAL_API(int)
PAL_EndianessQuery
(
    void
);

/* @summary Read a signed 8-bit integer value from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The signed 8-bit integer value at the specified location.
 */
PAL_API(pal_sint8_t)
PAL_Read_si8
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read an unsigned 8-bit integer value from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The unsigned 8-bit integer value at the specified location.
 */
PAL_API(pal_uint8_t)
PAL_Read_ui8
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read a signed 16-bit integer value from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The signed 16-bit integer value at the specified location.
 */
PAL_API(pal_sint16_t)
PAL_Read_si16
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read a signed 16-bit integer value stored in big endian format (MSB first) from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The signed 16-bit integer value at the specified location.
 */
PAL_API(pal_sint16_t)
PAL_Read_si16_msb
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read a signed 16-bit integer value stored in little endian format (LSB first) from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The signed 16-bit integer value at the specified location.
 */
PAL_API(pal_sint16_t)
PAL_Read_si16_lsb
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read an unsigned 16-bit integer value from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The unsigned 16-bit integer value at the specified location.
 */
PAL_API(pal_uint16_t)
PAL_Read_ui16
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read an unsigned 16-bit integer value stored in big endian format (MSB first) from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The unsigned 16-bit integer value at the specified location.
 */
PAL_API(pal_uint16_t)
PAL_Read_ui16_msb
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read an unsigned 16-bit integer value stored in little endian format (LSB first) from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The unsigned 16-bit integer value at the specified location.
 */
PAL_API(pal_uint16_t)
PAL_Read_ui16_lsb
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read a signed 32-bit integer value from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The signed 32-bit integer value at the specified location.
 */
PAL_API(pal_sint32_t)
PAL_Read_si32
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read a signed 32-bit integer value stored in big endian format (MSB first) from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The signed 32-bit integer value at the specified location.
 */
PAL_API(pal_sint32_t)
PAL_Read_si32_msb
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read a signed 32-bit integer value stored in little endian format (LSB first) from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The signed 32-bit integer value at the specified location.
 */
PAL_API(pal_sint32_t)
PAL_Read_si32_lsb
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read an unsigned 32-bit integer value from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The unsigned 32-bit integer value at the specified location.
 */
PAL_API(pal_uint32_t)
PAL_Read_ui32
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read an unsigned 32-bit integer value stored in big endian format (MSB first) from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The unsigned 32-bit integer value at the specified location.
 */
PAL_API(pal_uint32_t)
PAL_Read_ui32_msb
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read an unsigned 32-bit integer value stored in little endian format (LSB first) from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The unsigned 32-bit integer value at the specified location.
 */
PAL_API(pal_uint32_t)
PAL_Read_ui32_lsb
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read a signed 64-bit integer value from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The signed 64-bit integer value at the specified location.
 */
PAL_API(pal_sint64_t)
PAL_Read_si64
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read a signed 64-bit integer value stored in big endian format (MSB first) from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The signed 64-bit integer value at the specified location.
 */
PAL_API(pal_sint64_t)
PAL_Read_si64_msb
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read a signed 64-bit integer value stored in little endian format (LSB first) from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The signed 64-bit integer value at the specified location.
 */
PAL_API(pal_sint64_t)
PAL_Read_si64_lsb
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read an unsigned 64-bit integer value from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The unsigned 64-bit integer value at the specified location.
 */
PAL_API(pal_uint64_t)
PAL_Read_ui64
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read an unsigned 64-bit integer value stored in big endian format (MSB first) from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The unsigned 64-bit integer value at the specified location.
 */
PAL_API(pal_uint64_t)
PAL_Read_ui64_msb
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read an unsigned 64-bit integer value stored in little endian format (LSB first) from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The unsigned 64-bit integer value at the specified location.
 */
PAL_API(pal_uint64_t)
PAL_Read_ui64_lsb
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read a 32-bit floating-point value from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The 32-bit floating-point value at the specified location.
 */
PAL_API(pal_float32_t)
PAL_Read_f32
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read a 32-bit floating-point value stored in big endian format (MSB first) from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The 32-bit floating-point value at the specified location.
 */
PAL_API(pal_float32_t)
PAL_Read_f32_msb
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read a 32-bit floating-point value stored in little endian format (LSB first) from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The 32-bit floating-point value at the specified location.
 */
PAL_API(pal_float32_t)
PAL_Read_f32_lsb
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read a 64-bit floating-point value from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The 64-bit floating-point value at the specified location.
 */
PAL_API(pal_float64_t)
PAL_Read_f64
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read a 64-bit floating-point value stored in big endian format (MSB first) from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The 64-bit floating-point value at the specified location.
 */
PAL_API(pal_float64_t)
PAL_Read_f64_msb
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Read a 64-bit floating-point value stored in little endian format (LSB first) from a memory location.
 * @param addr A pointer to the buffer to read from.
 * @param offset The byte offset in the buffer at which the value is located.
 * @return The 64-bit floating-point value at the specified location.
 */
PAL_API(pal_float64_t)
PAL_Read_f64_lsb
(
    void           *addr, 
    pal_ptrdiff_t offset
);

/* @summary Write a signed 8-bit integer value to a memory location.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_si8
(
    void           *addr, 
    pal_sint8_t    value,
    pal_ptrdiff_t offset
);

/* @summary Write an unsigned 8-bit integer value to a memory location.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_ui8
(
    void           *addr, 
    pal_uint8_t    value,
    pal_ptrdiff_t offset
);

/* @summary Write a signed 16-bit integer value to a memory location.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_si16
(
    void           *addr, 
    pal_sint16_t   value,
    pal_ptrdiff_t offset
);

/* @summary Write a signed 16-bit integer value to a memory location. The value is written in big-endian (MSB first) format.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_si16_msb
(
    void           *addr, 
    pal_sint16_t   value,
    pal_ptrdiff_t offset
);

/* @summary Write a signed 16-bit integer value to a memory location. The value is written in little-endian (LSB first) format.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_si16_lsb
(
    void           *addr, 
    pal_sint16_t   value,
    pal_ptrdiff_t offset
);

/* @summary Write an unsigned 16-bit integer value to a memory location.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_ui16
(
    void           *addr, 
    pal_uint16_t   value,
    pal_ptrdiff_t offset
);

/* @summary Write an unsigned 16-bit integer value to a memory location. The value is written in big-endian (MSB first) format.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_ui16_msb
(
    void           *addr, 
    pal_uint16_t   value,
    pal_ptrdiff_t offset
);

/* @summary Write an unsigned 16-bit integer value to a memory location. The value is written in little-endian (LSB first) format.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_ui16_lsb
(
    void           *addr, 
    pal_uint16_t   value,
    pal_ptrdiff_t offset
);

/* @summary Write a signed 32-bit integer value to a memory location.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_si32
(
    void           *addr, 
    pal_sint32_t   value,
    pal_ptrdiff_t offset
);

/* @summary Write a signed 32-bit integer value to a memory location. The value is written in big-endian (MSB first) format.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_si32_msb
(
    void           *addr, 
    pal_sint32_t   value,
    pal_ptrdiff_t offset
);

/* @summary Write a signed 32-bit integer value to a memory location. The value is written in little-endian (LSB first) format.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_si32_lsb
(
    void           *addr, 
    pal_sint32_t   value,
    pal_ptrdiff_t offset
);

/* @summary Write an unsigned 32-bit integer value to a memory location.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_ui32
(
    void           *addr, 
    pal_uint32_t   value,
    pal_ptrdiff_t offset
);

/* @summary Write an unsigned 32-bit integer value to a memory location. The value is written in big-endian (MSB first) format.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_ui32_msb
(
    void           *addr, 
    pal_uint32_t   value,
    pal_ptrdiff_t offset
);

/* @summary Write an unsigned 32-bit integer value to a memory location. The value is written in little-endian (LSB first) format.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_ui32_lsb
(
    void           *addr, 
    pal_uint32_t   value,
    pal_ptrdiff_t offset
);

/* @summary Write a signed 64-bit integer value to a memory location.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_si64
(
    void           *addr, 
    pal_sint64_t   value,
    pal_ptrdiff_t offset
);

/* @summary Write a signed 64-bit integer value to a memory location. The value is written in big-endian (MSB first) format.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_si64_msb
(
    void           *addr, 
    pal_sint64_t   value,
    pal_ptrdiff_t offset
);

/* @summary Write a signed 64-bit integer value to a memory location. The value is written in little-endian (LSB first) format.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_si64_lsb
(
    void           *addr, 
    pal_sint64_t   value,
    pal_ptrdiff_t offset
);

/* @summary Write an unsigned 64-bit integer value to a memory location.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_ui64
(
    void           *addr, 
    pal_uint64_t   value,
    pal_ptrdiff_t offset
);

/* @summary Write an unsigned 64-bit integer value to a memory location. The value is written in big-endian (MSB first) format.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_ui64_msb
(
    void           *addr, 
    pal_uint64_t   value,
    pal_ptrdiff_t offset
);

/* @summary Write an unsigned 64-bit integer value to a memory location. The value is written in little-endian (LSB first) format.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_ui64_lsb
(
    void           *addr, 
    pal_uint64_t   value,
    pal_ptrdiff_t offset
);

/* @summary Write a 32-bit floating-point value to a memory location.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_f32
(
    void           *addr, 
    pal_float32_t  value,
    pal_ptrdiff_t offset
);

/* @summary Write a 32-bit floating-point value to a memory location. The value is written in big-endian (MSB first) format.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_f32_msb
(
    void           *addr, 
    pal_float32_t  value,
    pal_ptrdiff_t offset
);

/* @summary Write a 32-bit floating-point value to a memory location. The value is written in little-endian (LSB first) format.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_f32_lsb
(
    void           *addr, 
    pal_float32_t  value,
    pal_ptrdiff_t offset
);

/* @summary Write a 64-bit floating-point value to a memory location.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_f64
(
    void           *addr, 
    pal_float64_t  value,
    pal_ptrdiff_t offset
);

/* @summary Write a 64-bit floating-point value to a memory location. The value is written in big-endian (MSB first) format.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_f64_msb
(
    void           *addr, 
    pal_float64_t  value,
    pal_ptrdiff_t offset
);

/* @summary Write a 64-bit floating-point value to a memory location. The value is written in little-endian (LSB first) format.
 * @param addr A pointer to the buffer to write to.
 * @param value The value to write.
 * @param offset The byte offset in the buffer at which the value will be written.
 * @return The number of bytes written.
 */
PAL_API(pal_usize_t)
PAL_Write_f64_lsb
(
    void           *addr, 
    pal_float64_t  value,
    pal_ptrdiff_t offset
);

/* @summary Allocate general-purpose heap memory on the host.
 * This function is intended to be a wrapper for malloc or a suitable replacement.
 * @param len The minimum number of bytes to allocate.
 * @return A pointer to the allocated block, or NULL.
 */
PAL_API(void*)
PAL_HeapMemoryAllocateHost
(
    pal_usize_t len
);

/* @summary Change the size of a memory block allocated from the general-purpose heap.
 * This function is intended to be a wrapper for realloc or a suitable replacement.
 * If necessary, the memory block is moved to a new location.
 * @param addr The address of an existing allocation.
 * @param len The desired minimum number of bytes in the memory block.
 * @return A pointer to the reallocated memory block, which may be the same as addr or may point to a new location. Returns NULL if the request cannot be satisfied.
 */
PAL_API(void*)
PAL_HeapMemoryReallocateHost
(
    void      *addr, 
    pal_usize_t len
);

/* @summary Free a block of memory allocated from the general-purpose heap.
 * @param addr A pointer returned by a prior call to PAL_HeapMemoryAllocateHost or PAL_HeapMemoryReallocateHost.
 */
PAL_API(void)
PAL_HeapMemoryFreeHost
(
    void *addr
);

/* @summary Zero a block of memory.
 * @param dst The starting address of the block to zero-fill.
 * @param len The number of bytes to zero-fill.
 */
PAL_API(void)
PAL_ZeroMemory
(
    void       *dst, 
    pal_usize_t len
);

/* @summary Zero a block of memory, ensuring the operation is not optimized away.
 * @param dst The starting address of the block to zero-fill.
 * @param len The number of bytes to zero-fill.
 */
PAL_API(void)
PAL_ZeroMemorySecure
(
    void       *dst, 
    pal_usize_t len
);

/* @summary Copy a non-overlapping region of memory.
 * @param dst The address of the first byte to write.
 * @param src The address of the first byte to read.
 * @param len The number of bytes to copy from the source to the destination.
 */
PAL_API(void)
PAL_CopyMemory
(
    void       * PAL_RESTRICT dst, 
    void const * PAL_RESTRICT src, 
    pal_usize_t               len
);

/* @summary Copy a possibly-overlapping region of memory.
 * @param dst The address of the first byte to write.
 * @param src The address of the first byte to read.
 * @param len The number of bytes to copy from the source to the destination.
 */
PAL_API(void)
PAL_MoveMemory
(
    void       *dst, 
    void const *src,
    pal_usize_t len
);

/* @summary Set bytes in a memory block to given value.
 * @param dst The address of the first byte to write.
 * @param len The number of bytes to write.
 * @param val The value to write to each byte in the memory region.
 */
PAL_API(void)
PAL_FillMemory
(
    void       *dst, 
    pal_usize_t len, 
    pal_uint8_t val
);

/* @summary Compare the contents of two memory blocks.
 * @param ptr1 A pointer to a block of memory.
 * @param ptr2 A pointer to a block of memory.
 * @param len The number of bytes to compare.
 * @return Zero if the first len bytes of the memory blocks match, less than zero if the first non-matching byte in ptr1 is less than the same byte in ptr2, or greater than zero if the first non-matching byte in ptr1 is greater than the corresponding byte in ptr2.
 */
PAL_API(int)
PAL_CompareMemory
(
    void const *ptr1, 
    void const *ptr2, 
    pal_usize_t  len
);

/* @summary Initialize a pre-allocated pool of host memory allocation nodes.
 * @param pool The PAL_HOST_MEMORY_POOL to initialize.
 * @param init The attributes of the pool.
 * @return Zero if the pool is initialized successfully, or -1 if an error occurred.
 */
PAL_API(int)
PAL_HostMemoryPoolCreate
(
    struct PAL_HOST_MEMORY_POOL      *pool, 
    struct PAL_HOST_MEMORY_POOL_INIT *init
);

/* @summary Free all memory allocated to a host memory allocation pool.
 * @param pool The PAL_HOST_MEMORY_POOL to delete. All existing allocations are invalidated.
 */
PAL_API(void)
PAL_HostMemoryPoolDelete
(
    struct PAL_HOST_MEMORY_POOL *pool
);

/* @summary Move a host memory allocation pool to another location, for example, when you've created the PAL_HOST_MEMORY_POOL in a stack variable, and want to 'move' it into a structure field.
 * @param dst The PAL_HOST_MEMORY_POOL to which the source pool will be moved. This value does not need to be initialized.
 * @param src The PAL_HOST_MEMORY_POOL representing the source pool. After the call returns, this pool is invalid.
 */
PAL_API(void)
PAL_HostMemoryPoolMove
(
    struct PAL_HOST_MEMORY_POOL *dst, 
    struct PAL_HOST_MEMORY_POOL *src
);

/* @summary Reserve, and optionally commit, address space within a process.
 * @param pool The PAL_HOST_MEMORY_POOL from which the memory allocation will be acquired.
 * @param reserve_size The number of bytes of process address space to reserve. This value is rounded up to the nearest multiple of the system virtual memory page size.
 * @param commit_size The number of bytes of preocess address space to commit. This value is rounded up to the nearest multiple of the system virtual memory page size.
 * @param alloc_flags One or more of the values of PAL_HOST_MEMORY_ALLOCATION_FLAGS, or 0 if no special behavior is desired, in which case the memory is allocated as readable, writable, and ends with a guard page.
 * @return The PAL_HOST_MEMORY_ALLOCATION representing the allocated address space. The returned address is aligned to a multiple of the system virtual memory allocation granularity. The call returns NULL if the allocation fails.
 */
PAL_API(struct PAL_HOST_MEMORY_ALLOCATION*)
PAL_HostMemoryPoolAllocate
(
    struct PAL_HOST_MEMORY_POOL *pool, 
    pal_usize_t          reserve_size, 
    pal_usize_t           commit_size, 
    pal_uint32_t          alloc_flags
);

/* @summary Release all address space reserved for a single allocation and return it to the memory pool.
 * @param pool The pool to which the allocation will be returned. This must be the same pool from which the allocation was acquired.
 * @param alloc The host memory allocation to return.
 */
PAL_API(void)
PAL_HostMemoryPoolRelease
(
    struct PAL_HOST_MEMORY_POOL        *pool, 
    struct PAL_HOST_MEMORY_ALLOCATION *alloc
);

/* @summary Invalidate all existing allocations from a host memory pool and return them to the pool, without deleting the pool itself.
 * @param pool The PAL_HOST_MEMORY_POOL to reset to empty.
 */
PAL_API(void)
PAL_HostMemoryPoolReset
(
    struct PAL_HOST_MEMORY_POOL *pool
);

/* @summary Reserve and optionally commit address space within a process. Call PAL_HostMemoryRelease if the allocation already holds address space.
 * @param alloc The PAL_HOST_MEMORY_ALLOCATION to initialize.
 * @param reserve_size The amount of process address space to reserve, in bytes. This value is rounded up to the next multiple of the operating system page size.
 * @param commit_size The amount of process address space to commit, in bytes. This value is rounded up to the next multiple of the operating system page size, unless it is zero.
 * @param alloc_flags One or more PAL_HOST_MEMORY_ALLOCATION_FLAGS specifying the access and behavior of the memory region.
 * @return Zero if the allocation is initialized successfully, or -1 if an error occurred.
 */
PAL_API(int)
PAL_HostMemoryReserveAndCommit
(
    struct PAL_HOST_MEMORY_ALLOCATION *alloc, 
    pal_usize_t                 reserve_size, 
    pal_usize_t                  commit_size, 
    pal_uint32_t                 alloc_flags
);

/* @summary Increase the amount of committed address space within an existing allocation. The commit size cannot exceed the reservation size.
 * @param alloc The PAL_HOST_MEMORY_ALLOCATION to modify.
 * @param commit_size The total amount of process address space within the allocation that should be committed.
 * @return Zero if at least the specified amount of address space is committed within the allocation, or -1 if an error occurred.
 */
PAL_API(int)
PAL_HostMemoryIncreaseCommitment
(
    struct PAL_HOST_MEMORY_ALLOCATION *alloc, 
    pal_usize_t                  commit_size
);

/* @summary Flush the CPU instruction cache after writing dynamically-generated code to a memory block.
 * @param alloc The PAL_HOST_MEMORY_ALLOCATION representing the memory block containing the dynamically-generated code.
 */
PAL_API(void)
PAL_HostMemoryFlush
(
    struct PAL_HOST_MEMORY_ALLOCATION *alloc
);

/* @summary Decommit and release the process address space associated with a host memory allocation.
 * @param alloc The PAL_HOST_MEMORY_ALLOCATION representing the process address space to release.
 */
PAL_API(void)
PAL_HostMemoryRelease
(
    struct PAL_HOST_MEMORY_ALLOCATION *alloc
);

/* @summary Determine whether a PAL_MEMORY_BLOCK specifies a valid allocation.
 * @param block The PAL_MEMORY_BLOCK to examine.
 * @return Non-zero if the memory block specifies a valid allocation, or zero if the memory block specifies an invalid allocation.
 */
PAL_API(int)
PAL_MemoryBlockIsValid
(
    struct PAL_MEMORY_BLOCK *block
);

/* @summary Determine whether old_block and new_block point to the same memory location.
 * This function is useful after a realloc operation is performed.
 * @param old_block The PAL_MEMORY_BLOCK representing an existing allocation.
 * @param new_block The PAL_MEMORY_BLOCK representing the modified allocation.
 * @return Non-zero if the memory block was relocated, or zero if the memory block was not relocated.
 */
PAL_API(int)
PAL_MemoryBlockDidMove
(
    struct PAL_MEMORY_BLOCK *old_block, 
    struct PAL_MEMORY_BLOCK *new_block
);

/* @summary Initialize a memory arena allocator around an externally-managed memory block.
 * @param arena The memory arena allocator to initialize.
 * @param memory A pointer to the start of the memory block to sub-allocate from.
 * @param memory_size The size of the memory block, in bytes.
 * @return Zero if the memory arena is successfully created, or -1 if an error occurs.
 */
PAL_API(int)
PAL_MemoryArenaCreate
(
    struct PAL_MEMORY_ARENA     *arena, 
    struct PAL_MEMORY_ARENA_INIT *init
);

/* @summary Sub-allocate memory from an arena.
 * @param arena The PAL_MEMORY_ARENA from which the memory is being requested.
 * @param size The minimum number of bytes to allocate from the arena.
 * @param alignment The desired alignment of the returned address or offset, in bytes. This must be a non-zero power-of-two.
 * @param block On return, information about the allocated memory block is copied to this location. Required.
 * @return Zero if the allocation request was successful, or -1 if the allocation request failed.
 */
PAL_API(int)
PAL_MemoryArenaAllocate
(
    struct PAL_MEMORY_ARENA *arena, 
    pal_usize_t               size, 
    pal_usize_t          alignment, 
    struct PAL_MEMORY_BLOCK *block
);

/* @summary Sub-allocate memory from an arena.
 * @param arena The memory arena from which the memory will be allocated.
 * @param size The minimum number of bytes to allocate.
 * @param alignment The required alignment of the returned address, in bytes. This value must be zero, or a power-of-two.
 * @param block On return, information about the allocated memory block is copied to this location.
 * @return A pointer to the start of a memory block of at least size bytes, or NULL.
 */
PAL_API(void*)
PAL_MemoryArenaAllocateHost
(
    struct PAL_MEMORY_ARENA *arena, 
    pal_usize_t               size, 
    pal_usize_t          alignment, 
    struct PAL_MEMORY_BLOCK *block
);

/* @summary Sub-allocate memory from an arena.
 * @param arena The memory arena from which the memory will be allocated.
 * @param size The minimum number of bytes to allocate.
 * @param alignment The required alignment of the returned address, in bytes. This value must be zero, or a power-of-two.
 * @return A pointer to the start of a memory block of at least size bytes, or NULL.
 */
PAL_API(void*)
PAL_MemoryArenaAllocateHostNoBlock
(
    struct PAL_MEMORY_ARENA *arena, 
    pal_usize_t               size, 
    pal_usize_t          alignment
);

/* @summary Retrieve a marker that can be used to reset a memory arena back to a given point.
 * @param arena The memory arena allocator to query.
 * @return A marker value that can roll back the allocator to its state at the time of the call, invalidating all allocations made from that point forward.
 */
PAL_API(struct PAL_MEMORY_ARENA_MARKER)
PAL_MemoryArenaMark
(
    struct PAL_MEMORY_ARENA *arena
);

/* @summary Roll back a memory arena allocator to a given point in time.
 * @param arena The memory arena allocator to roll back.
 * @param marker A marker obtained by a prior call to the ArenaMark function, or 0 to invalidate all existing allocations made from the arena.
 */
PAL_API(void)
PAL_MemoryArenaResetToMarker
(
    struct PAL_MEMORY_ARENA        *arena, 
    struct PAL_MEMORY_ARENA_MARKER marker
);

/* @summary Invalidate all allocations made from a memory arena.
 * @param arena The memory arena allocator to roll back.
 */
PAL_API(void)
PAL_MemoryArenaReset
(
    struct PAL_MEMORY_ARENA *arena
);

/* @summary Determine the number of bytes that need to be allocated to store memory allocator state data.
 * @param allocation_size_min A power of two, greater than zero, specifying the smallest size allocation that can be returned by the allocator.
 * @param allocation_size_max A power of two, greater than zero, specifying the largest size allocation that can be returned by the allocator.
 * @return The number of bytes that must be allocated by the caller for storing allocator state data.
 */
PAL_API(pal_usize_t)
PAL_MemoryAllocatorQueryHostMemorySize
(
    pal_usize_t allocation_size_min, 
    pal_usize_t allocation_size_max
);

/* @summary Initialize a general-purpose memory allocator.
 * @param alloc The PAL_MEMORY_ALLOCATOR to initialize.
 * @param init The attributes used to configure the memory allocator.
 * @return Zero if the allocator is successfully initialized, or -1 if an error occurred.
 */
PAL_API(int)
PAL_MemoryAllocatorCreate
(
    struct PAL_MEMORY_ALLOCATOR     *alloc, 
    struct PAL_MEMORY_ALLOCATOR_INIT *init
);

/* @summary Allocate memory from a general-purpose allocator.
 * @param alloc The PAL_MEMORY_ALLOCATOR from which the memory will be allocated.
 * @param size The minimum number of bytes to allocate.
 * @param alignment The desired alignment of the returned address or offset, in bytes. This must be a non-zero power-of-two.
 * @param block On return, information about the allocated memory block is copied to this location. Required.
 * @return Zero if the allocation request was successful, or -1 if the allocation request failed.
 */
PAL_API(int)
PAL_MemoryAllocatorAlloc
(
    struct PAL_MEMORY_ALLOCATOR *alloc, 
    pal_usize_t                   size,
    pal_usize_t              alignment, 
    struct PAL_MEMORY_BLOCK     *block
);

/* @summary Allocate host memory from a general-purpose allocator managing host memory.
 * @param alloc The PAL_MEMORY_ALLOCATOR from which the memory will be allocated.
 * @param size The minimum number of bytes to allocate.
 * @param alignment The desired alignment of the returned address or offset, in bytes. This must be a non-zero power-of-two.
 * @param block On return, information about the allocated memory block is copied to this location. Required.
 * @return A pointer to the host memory, or NULL.
 */
PAL_API(void*)
PAL_MemoryAllocatorHostAlloc
(
    struct PAL_MEMORY_ALLOCATOR *alloc, 
    pal_usize_t                   size,
    pal_usize_t              alignment, 
    struct PAL_MEMORY_BLOCK     *block
);

/* @summary Grow or shrink a memory block to meet a desired size.
 * This function differs from realloc in that the caller must compare the BlockOffset or HostAddress returned in new_block to the BlockOffset or HostAddress in existing.
 * If the new_block specifies a different memory location, the caller must copy data from the old location to the new location. 
 * It is still valid to access the memory at the existing location even though the block has been marked free.
 * @param alloc The PAL_MEMORY_ALLOCATOR that returned the existing block.
 * @param existing The PAL_MEMORY_BLOCK representing the existing allocation.
 * @param new_size The new required minimum allocation size, in bytes.
 * @param alignment The required alignment of the returned address or offset, in bytes. This must be a non-zero power-of-two.
 * @param new_block On return, information about the allocated memory block is copied to this location. Required.
 * @return Zero if the allocation request was successful, or -1 if the allocation request failed.
 */
PAL_API(int)
PAL_MemoryAllocatorRealloc
(
    struct PAL_MEMORY_ALLOCATOR *                  alloc, 
    struct PAL_MEMORY_BLOCK     * PAL_RESTRICT  existing, 
    pal_usize_t                                 new_size, 
    pal_usize_t                                alignment, 
    struct PAL_MEMORY_BLOCK     * PAL_RESTRICT new_block
);

/* @summary Grow or shrink a memory block to meet a desired size.
 * @param alloc The PAL_MEMORY_ALLOCATOR that returned the existing block.
 * @param existing The PAL_MEMORY_BLOCK representing the existing allocation.
 * @param new_size The new required minimum allocation size, in bytes.
 * @param alignment The required alignment of the returned address or offset, in bytes. This must be a non-zero power-of-two.
 * @param new_block On return, information about the allocated memory block is copied to this location. Required.
 * @return A pointer to the host memory, which may or may not be the same as existing->HostAddress, or NULL if the reallocation request failed.
 */
PAL_API(void*)
PAL_MemoryAllocatorHostRealloc
(
    struct PAL_MEMORY_ALLOCATOR *                  alloc, 
    struct PAL_MEMORY_BLOCK     * PAL_RESTRICT  existing, 
    pal_usize_t                                 new_size, 
    pal_usize_t                                alignment, 
    struct PAL_MEMORY_BLOCK     * PAL_RESTRICT new_block
);

/* @summary Free a general-purpose memory allocation.
 * @param alloc The PAL_MEMORY_ALLOCATOR that returned the existing block.
 * @param existing The PAL_MEMORY_BLOCK representing the allocation to free.
 */
PAL_API(void)
PAL_MemoryAllocatorFree
(
    struct PAL_MEMORY_ALLOCATOR *alloc, 
    struct PAL_MEMORY_BLOCK     *block
);

/* @summary Free a general-purpose host memory allocation.
 * @param alloc The PAL_MEMORY_ALLOCATOR that returned the existing block.
 * @param existing The host-visible address representing the allocation to free.
 */
PAL_API(void)
PAL_MemoryAllocatorHostFree
(
    struct PAL_MEMORY_ALLOCATOR *alloc, 
    void                     *existing
);

/* @summary Invalidate all existing allocations and reset a memory allocator to its initial state.
 * @param alloc The PAL_MEMORY_ALLOCATOR to reset.
 */
PAL_API(void)
PAL_MemoryAllocatorReset
(
    struct PAL_MEMORY_ALLOCATOR *alloc
);

/* @summary Create a memory view used to access a memory block with a defined layout.
 * @param view The PAL_MEMORY_VIEW to initialize.
 * @param layout The memory layout describing the stream alignment and stride.
 * @param base_address The base address of the memory block for which the view is being created.
 * @param chunk_size The maximum number of data elements that can be stored in the memory block.
 * @param item_count The number of valid elements in the memory block.
 * @return Zero of the memory view is successfully initialized, or -1 if an error occurred.
 */
PAL_API(int)
PAL_MemoryViewInit
(
    struct PAL_MEMORY_VIEW     *view, 
    struct PAL_MEMORY_LAYOUT *layout, 
    void               *base_address, 
    pal_uint32_t          chunk_size, 
    pal_uint32_t          item_count
);

/* @summary Initialize a PAL_MEMORY_LAYOUT to empty. Use the PAL_MemoryLayoutDefineStream function or PAL_MemoryLayoutAdd macro to define data streams.
 * @param layout The PAL_MEMORY_LAYOUT to initialize.
 */
PAL_API(void)
PAL_MemoryLayoutInit
(
    struct PAL_MEMORY_LAYOUT *layout
);

/* @summary Copy a PAL_MEMORY_LAYOUT.
 * @param dst The destination PAL_MEMORY_LAYOUT.
 * @param src The source PAL_MEMORY_LAYOUT.
 */
PAL_API(void)
PAL_MemoryLayoutCopy
(
    struct PAL_MEMORY_LAYOUT * PAL_RESTRICT dst, 
    struct PAL_MEMORY_LAYOUT * PAL_RESTRICT src
);

/* @summary Declare a data stream within a PAL_MEMORY_LAYOUT. Streams should be defined in the order they appear.
 * Typically the PAL_MemoryLayoutAdd(layout, type) macro is used to call this function.
 * @param layout The PAL_MEMORY_LAYOUT to which the data stream definition will be added.
 * @param item_size The size of a single data element in the stream, in bytes.
 * @param stream_align The required alignment of the start of the data stream, in bytes. This value must be a power of two greater than zero.
 * @return Zero if the memory layout was updated, or -1 if an error occurred.
 */
PAL_API(int)
PAL_MemoryLayoutDefineStream
(
    struct PAL_MEMORY_LAYOUT *layout, 
    pal_usize_t            item_size, 
    pal_usize_t         stream_align
);

/* @summary Calculate the maximum number of bytes required to store a data chunk with the specified layout.
 * @param layout The PAL_MEMORY_LAYOUT defining the item size and stream alignment requirements.
 * @param item_count The maximum number of items in each stream.
 * @return The maximum number of bytes required to store the data with the specified attributes.
 */
PAL_API(pal_usize_t)
PAL_MemoryLayoutComputeSize
(
    struct PAL_MEMORY_LAYOUT *layout, 
    pal_usize_t           item_count
);

/* @summary Construct a new dynamic buffer with the given attributes.
 * @param buffer The PAL_DYNAMIC_BUFFER to initialize.
 * @param init Data used to configure the dynamic buffer.
 * @return Zero if the dynamic buffer is successfully initialized, or -1 if an error occurred.
 */
PAL_API(int)
PAL_DynamicBufferCreate
(
    struct PAL_DYNAMIC_BUFFER    *buffer, 
    struct PAL_DYNAMIC_BUFFER_INIT *init
);

/* @summary Free resources associated with a dynamic buffer.
 * @param buffer The PAL_DYNAMIC_BUFFER to delete.
 */
PAL_API(void)
PAL_DynamicBufferDelete
(
    struct PAL_DYNAMIC_BUFFER *buffer
);

/* @summary Ensure that a dynamic buffer can accomodate the given number of elements.
 * If necessary, the buffer commitment is increased to accomodate the given number of elements.
 * @param buffer The PAL_DYNAMIC_BUFFER to query and possibly grow.
 * @param capacity_in_elements The total number of elements that can be stored in the buffer.
 * @return Zero if the dynamic buffer can accomodate the given number of elements, or -1 if the buffer cannot hold the desired number of elements.
 */
PAL_API(int)
PAL_DynamicBufferEnsure
(
    struct PAL_DYNAMIC_BUFFER *buffer, 
    pal_usize_t  capacity_in_elements
);

/* @summary Decommit as much memory as possible within a dynamic buffer.
 * Generally, the buffer is not modified after shrinking, but this is not required.
 * @param buffer The PAL_DYNAMIC_BUFFER to shrink.
 * @return Zero if the shrink operation completed successfully, or -1 if the shrink operation failed.
 */
PAL_API(int)
PAL_DynamicBufferShrink
(
    struct PAL_DYNAMIC_BUFFER *buffer
);

/* @summary Grow or shrink a dynamic buffer to have the specified size, resizing the underlying storage if necessary.
 * @param buffer The PAL_DYNAMIC_BUFFER to resize.
 * @param size_in_elements The desired size of the buffer, in elements.
 * @return Zero if the resize operation completed successfully, or -1 if the resize operation failed.
 */
PAL_API(int)
PAL_DynamicBufferResize
(
    struct PAL_DYNAMIC_BUFFER *buffer, 
    pal_usize_t      size_in_elements
);

/* @summary Reset a dynamic buffer to empty, without decommitting any memory.
 * @param buffer The PAL_DYNAMIC_BUFFER to reset.
 */
PAL_API(void)
PAL_DynamicBufferReset
(
    struct PAL_DYNAMIC_BUFFER *buffer
);

/* @summary Retrieve a pointer to the first element stored in the buffer.
 * @param buffer The PAL_DYNAMIC_BUFFER to query.
 * @return A pointer to the first element stored in the buffer.
 */
PAL_API(pal_uint8_t*)
PAL_DynamicBufferBegin
(
    struct PAL_DYNAMIC_BUFFER *buffer
);

/* @summary Retrieve a pointer to one-past the last element stored in the buffer. Do not dereference the returned pointer.
 * @param buffer The PAL_DYNAMIC_BUFFER to query.
 * @return A pointer to one-past the last element stored in the buffer.
 */
PAL_API(pal_uint8_t*)
PAL_DynamicBufferEnd
(
    struct PAL_DYNAMIC_BUFFER *buffer
);

/* @summary Retrieve the address of the i'th element in the buffer.
 * @param buffer The PAL_DYNAMIC_BUFFER to query.
 * @param index The zero-based index of the element whose address will be returned.
 * @return The address of the specified element.
 */
PAL_API(pal_uint8_t*)
PAL_DynamicBufferElementAddress
(
    struct PAL_DYNAMIC_BUFFER *buffer, 
    pal_usize_t                 index
);

/* @summary Retrieve the number of elements currently stored in the buffer.
 * @param buffer The PAL_DYNAMIC_BUFFER to query.
 * @return The number of elements stored in the buffer.
 */
PAL_API(pal_usize_t)
PAL_DynamicBufferCount
(
    struct PAL_DYNAMIC_BUFFER *buffer
);

/* @summary Append one or more elements to the end of the buffer. If necessary, the buffer capacity is increased.
 * @param buffer The PAL_DYNAMIC_BUFFER to modify.
 * @param src A pointer to the start of the first element to append to the buffer.
 * @param element_count The number of elements to read from src and copy into the buffer.
 * @param element_size The size of a single element in the src buffer, where the total number of bytes to copy is calculated as element_size * element_count.
 * @return Zero if the append operation is successful, or -1 if an error occurred.
 */
PAL_API(int)
PAL_DynamicBufferAppend
(
    struct PAL_DYNAMIC_BUFFER *buffer, 
    void const                   *src, 
    pal_usize_t         element_count, 
    pal_usize_t          element_size
);

/* @summary Remove one or more elements from the end of the buffer. The buffer capacity remains unchanged.
 * @param buffer The PAL_DYNAMIC_BUFFER to modify.
 * @param new_element_count The number of elements stored in the buffer. This must be less than or equal to the value returned by PAL_DynamicBufferCount().
 * @return Zero if the truncation operation is successful, or -1 if an error occurred.
 */
PAL_API(int)
PAL_DynamicBufferTruncate
(
    struct PAL_DYNAMIC_BUFFER *buffer, 
    pal_usize_t     new_element_count
);

/* @summary Construct a new handle table with the given attributes.
 * This is an O(N) operation where N is the capacity of the table.
 * @param table The PAL_HANDLE_TABLE to initialize.
 * @param init Data used to configure the handle table.
 * @return Zero if the handle table is successfully initialized, or -1 if an error occurred.
 */
PAL_API(int)
PAL_HandleTableCreate
(
    struct PAL_HANDLE_TABLE     *table, 
    struct PAL_HANDLE_TABLE_INIT *init
);

/* @summary Free resources associated with a handle table.
 * @param table The PAL_HANDLE_TABLE to delete.
 */
PAL_API(void)
PAL_HandleTableDelete
(
    struct PAL_HANDLE_TABLE *table
);

/* @summary Reset a handle table to empty, invalidating all current handles.
 * This is an O(N) operation where N is the capacity of the table.
 * @param table The PAL_HANDLE_TABLE to reset.
 */
PAL_API(void)
PAL_HandleTableReset
(
    struct PAL_HANDLE_TABLE *table
);

/* @summary Validate a set of handle values in order to detect double-deletion.
 * This function can be called prior to calling other functions that work with handle values to ensure the handles are all valid.
 * In debug builds, when an invalid handle is detected, an assert will fire.
 * @param table The PAL_HANDLE_TABLE that created the handles being checked.
 * This is the table from which a call to PAL_HandleTableCreateIds returned the handles, which is not necessarily the same table on which the operation is being performed.
 * For example, you might have tables A and B. The handles are created by table A and then additional data associated with each handle is stored in table B. This function wants table A.
 * @param handles The set of PAL_HANDLE values to validate.
 * @param count The number of PAL_HANDLE values specified in the handles array.
 * @return Zero if all handles are valid, or non-zero if the handles array contains one or more invalid handles.
 */
PAL_API(int)
PAL_HandleTableValidateIds
(
    struct PAL_HANDLE_TABLE *table, 
    PAL_HANDLE            *handles, 
    pal_usize_t              count
);

/* @summary Reset an entire chunk, invalidating all live handles. Intended to be used during table cleanup.
 * The specified PAL_HANDLE_TABLE must have been the table that allocated the IDs.
 * @param table The PAL_HANDLE_TABLE managing the chunk to be reset.
 * @param chunk_index The zero-based index of the chunk to reset.
 */
PAL_API(void)
PAL_HandleTableDeleteChunkIds
(
    struct PAL_HANDLE_TABLE *table, 
    pal_uint32_t       chunk_index
);

/* @summary Reset an entire chunk, removing all live handles. Intended to be used during table cleanup.
 * The specified PAL_HANDLE_TABLE must not be the table that allocated the IDs (use PAL_HandleTableDeleteChunkIds instead in this case.)
 * @param table The PAL_HANDLE_TABLE managing the chunk to be reset.
 * @param chunk_index The zero-based index of the chunk to reset.
 */
PAL_API(void)
PAL_HandleTableRemoveChunkIds
(
    struct PAL_HANDLE_TABLE *table, 
    pal_uint32_t       chunk_index
);

/* @summary Visit all committed and non-empty chunks in the table, invoking a user-defined callback for each chunk.
 * @param table The PAL_HANDLE_TABLE to walk over.
 * @param config Data used to configure the operation.
 * @return Zero if the operation visited all non-empty chunks in the table, or non-zero if the operation was cancelled by the callback.
 */
PAL_API(int)
PAL_HandleTableVisit
(
    struct PAL_HANDLE_TABLE               *table, 
    struct PAL_HANDLE_TABLE_VISITOR_INIT *config
);

/* @summary Allocate one or more objects, generating new handles to identify them.
 * The operation fails if the table cannot allocate the requested number of objects.
 * The operation fails if the table was not created with the PAL_HANDLE_TABLE_FLAG_IDENTITY attribute.
 * @param table The PAL_HANDLE_TABLE from which the object handles will be allocated.
 * @param handles The array of count handles to populate with allocated object handles.
 * @param count The number of objects to allocate.
 * @return Zero if count objects were successfully allocated, or non-zero if an error occurred.
 */
PAL_API(int)
PAL_HandleTableCreateIds
(
    struct PAL_HANDLE_TABLE *table, 
    PAL_HANDLE            *handles, 
    pal_usize_t              count
);

/* @summary Delete one or more objects and invalidate their associated handles.
 * The operation fails if the table was not created with the PAL_HANDLE_TABLE_FLAG_IDENTITY attribute.
 * @param table The PAL_HANDLE_TABLE from which the object handles were allocated.
 * @param handles The array of count handles specifying the objects to delete.
 * @param count The number of objects to delete.
 */
PAL_API(void)
PAL_HandleTableDeleteIds
(
    struct PAL_HANDLE_TABLE *table, 
    PAL_HANDLE            *handles, 
    pal_usize_t              count
);

/* @summary Allocate storage for one or more externally-created object handles.
 * @param table The PAL_HANDLE_TABLE to update with the specified handle set.
 * @param handles The array of count handles specifying the objects to insert.
 * @param count The number of object handles to insert.
 * @return Zero if all object handles were successfully inserted, or non-zero if an error occurred.
 */
PAL_API(int)
PAL_HandleTableInsertIds
(
    struct PAL_HANDLE_TABLE *table, 
    PAL_HANDLE            *handles, 
    pal_usize_t              count
);

/* @summary Remove one or more object handles from a table without invalidating the object handles.
 * This operation may re-pack objects to maintain dense storage within the table.
 * @param table The PAL_HANDLE_TABLE from which the object handles will be removed.
 * @param handles The array of count handles specifying the object handles to remove.
 * @param count The number of object handles to remove.
 */
PAL_API(void)
PAL_HandleTableRemoveIds
(
    struct PAL_HANDLE_TABLE *table, 
    PAL_HANDLE            *handles, 
    pal_usize_t              count
);

/* @summary Retrieve the PAL_HANDLE_TABLE_CHUNK describing a chunk of objects within a handle table.
 * @param table The PAL_HANDLE_TABLE to query.
 * @param chunk The PAL_HANDLE_TABLE_CHUNK to populate with information about the table chunk.
 * @param index The zero-based index of the chunk to describe.
 * @param view Pointer to a PAL_MEMORY_VIEW to initialize for reading data from the chunk.
 * @return The input chunk pointer, if the index was valid and the chunk was populated, or NULL if the chunk is invalid.
 */
PAL_API(struct PAL_HANDLE_TABLE_CHUNK*)
PAL_HandleTableGetChunkForIndex
(
    struct PAL_HANDLE_TABLE       *table, 
    struct PAL_HANDLE_TABLE_CHUNK *chunk, 
    pal_uint32_t                   index, 
    struct PAL_MEMORY_VIEW         *view
);

/* @summary Retrieve the PAL_HANDLE_TABLE_CHUNK containing the data associated with a PAL_HANDLE.
 * @param table The PAL_HANDLE_TABLE to query.
 * @param chunk The PAL_HANDLE_TABLE_CHUNK to populate with information about the table chunk.
 * @param handle The PAL_HANDLE identifying the object to access.
 * @param index On return, the zero-based index of the object within the chunk is stored at this location.
 * @param view Pointer to a PAL_MEMORY_VIEW to initialize for reading data from the chunk.
 * @return The input chunk pointer, if the handle was valid and the chunk was populated, or NULL if the handle or chunk is invalid.
 */
PAL_API(struct PAL_HANDLE_TABLE_CHUNK*)
PAL_HandleTableGetChunkForHandle
(
    struct PAL_HANDLE_TABLE       *table, 
    struct PAL_HANDLE_TABLE_CHUNK *chunk, 
    PAL_HANDLE                    handle, 
    pal_uint32_t                  *index, 
    struct PAL_MEMORY_VIEW         *view
);

#ifdef __cplusplus
}; /* extern "C" */
#endif

/* @summary Include the appropriate platform-specific header.
 */
#if   PAL_TARGET_PLATFORM == PAL_PLATFORM_WIN32 || PAL_TARGET_PLATFORM == PAL_PLATFORM_WINRT
    #include "pal_win32_memory.h"
#elif PAL_TARGET_PLATFORM == PAL_PLATFORM_LINUX || PAL_TARGET_PLATFORM == PAL_PLATFORM_ANDROID
    #include "pal_linux_memory.h"
#elif PAL_TARGET_PLATFORM == PAL_PLATFORM_MACOS || PAL_TARGET_PLATFORM == PAL_PLATFORM_IOS
    #include "pal_apple_memory.h"
#else
    #error   pal_memory.h: No implementation of the abstraction layer for your platform.
#endif

#endif /* __PAL_MEMORY_H__ */
