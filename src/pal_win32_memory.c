/**
 * @summary Implement the PAL entry points from pal_memory.h.
 */
#include "pal_win32_memory.h"

/* @summary Perform a bit rotation left by a certain amount on a 32-bit value.
 * @param _x The 32-bit value to rotate.
 * @param _r The number of bits to rotate by.
 * @return The resulting value.
 */
#ifndef PAL_rotl32
#define PAL_rotl32(_x, _r)                                                    \
    _rotl((_x), (_r))
#endif

/* @summary Perform a bit rotation left by a certain amount on a 64-bit value.
 * @param _x The 64-bit value to rotate.
 * @param _r The number of bits to rotate by.
 * @return The resulting value.
 */
#ifndef PAL_rotl64
#define PAL_rotl64(_x, _r)                                                    \
    _rotl64((_x), (_r))
#endif

/* @summary Determine whether a given value is a power of two or not.
 * @param _value The value to check.
 * @return Non-zero if the value is a power-of-two, or zero otherwise.
 */
#ifndef PAL_IsPowerOfTwo
#define PAL_IsPowerOfTwo(_value)                                              \
    (((_value) & ((_value)-1)) == 0)
#endif

/* @summary Scan a 64-bit machine word, starting from least-significant to most-significant bit, for the first set bit.
 * @param _value The 64-bit value to search.
 * @param _outbit If the value contains a set bit, the zero-based index of the bit is stored at this location.
 * @return Non-zero if a set bit is found in the value, or zero of no set bit is found.
 * MSVC: _BitScanForward64. GCC: __builtin_ffs (different semantics).
 */
#ifndef PAL_BitScan_ui64_lsb
#define PAL_BitScan_ui64_lsb(_value, _outbit)                                 \
    ((int)_BitScanForward64((unsigned long*)(_outbit), (unsigned __int64)(_value)))
#endif

/* @summary Scan a 64-bit machine word, starting from most-significant to least-significant bit, for the first set bit.
 * @param value The 64-bit value to search.
 * @param bit If the value contains a set bit, the zero-based index of the bit is stored at this location.
 * @return Non-zero if a set bit is found in the value, or zero of no set bit is found.
 * MSVC: _BitScanReverse64. GCC: __builtin_ffs (different semantics).
 */
#ifndef PAL_BitScan_ui64_msb
#define PAL_BitScan_ui64_msb(_value, _outbit)                                 \
    ((int)_BitScanReverse64((unsigned long*)(_outbit), (unsigned __int64)(_value)))
#endif

/* @summary Calculate the next power-of-two value greater than or equal to a given value.
 * @param n The input value.
 * @return The next power-of-two value greater than or equal to n.
 */
static pal_usize_t
PAL_NextPow2GreaterOrEqual
(
    pal_usize_t n
)
{
    pal_usize_t i, k;
    --n;
    for (i = 1, k = sizeof(pal_usize_t) * 8; i < k; i <<= 1)
    {
        n |= n >> i;
    }
    return n+1;
}

/* @summary Portably read a 32-bit value from a memory location which may or may not be properly aligned.
 * @param mem The memory location to read.
 * @return A 32-bit unsigned integer representing the value at address mem.
 */
static PAL_INLINE pal_uint32_t
PAL_ReadU32
(
    void const *mem
)
{
    pal_uint32_t val;
    memcpy(&val, mem, sizeof(val));
    return val;
}

/* @summary Portably read a 32-bit value from a memory location which may or may not be properly aligned.
 * @param mem The memory location to read.
 * @return A 32-bit unsigned integer representing the value at address mem.
 */
static PAL_INLINE pal_uint64_t
PAL_ReadU64
(
    void const *mem
)
{
    pal_uint64_t val;
    memcpy(&val, mem, sizeof(val));
    return val;
}

/* @summary XXH32_round from xxHash implemented for MSVC.
 * @param acc The 32-bit accumulator.
 * @param val The input value.
 * @return The updated accumulator.
 */
static PAL_INLINE pal_uint32_t
PAL_XXH32_round
(
    pal_uint32_t acc, 
    pal_uint32_t val
)
{
    acc += val * 2246822519U;
    acc  = PAL_rotl32(acc, 13);
    acc *= 2654435761U;
    return acc;
}

/* @summary XXH64_round from xxHash implemented for MSVC.
 * @param acc The 64-bit accumulator.
 * @param val The input value.
 * @return The updated accumulator.
 */
static PAL_INLINE pal_uint64_t
PAL_XXH64_round
(
    pal_uint64_t acc, 
    pal_uint64_t val
)
{
    acc += val * 14029467366897019727ULL;
    acc  = PAL_rotl64(acc, 31);
    acc *= 11400714785074694791ULL;
    return acc;
}

/* @summary XXH64_mergeRound from xxHash implemented for MSVC.
 * @param acc The 64-bit accumulator.
 * @param val The input value.
 * @return The updated accumulator.
 */
static PAL_INLINE pal_uint64_t
PAL_XXH64_merge
(
    pal_uint64_t acc, 
    pal_uint64_t val
)
{
    val  = PAL_XXH64_round(0, val);
    acc ^= val;
    acc  = acc * 11400714785074694791ULL + 9650029242287828579ULL;
    return acc;
}

/* @summary Reads a 16-bit value from a buffer and performs a byte swap operation on it before returning it as the final destination type (16-bit unsigned.)
 * @param addr A pointer to the buffer from which the value will be read.
 * @param offset The offset into the buffer at which to read the value.
 * @return The byte-swapped value.
 */
static PAL_INLINE pal_uint16_t
PAL_ReadSwap_i16
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
    pal_uint8_t   *src = ((pal_uint8_t *)addr) + offset;
    return PAL_ByteSwap2(*(pal_uint16_t*) src);
}

/* @summary Reads a 32-bit value from a buffer and performs a byte swap operation on it before returning it as the final destination type (32-bit unsigned.)
 * @param addr A pointer to the buffer from which the value will be read.
 * @param offset The offset into the buffer at which to read the value.
 * @return The byte-swapped value.
 */
static PAL_INLINE pal_uint32_t
PAL_ReadSwap_i32
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
    pal_uint8_t   *src = ((pal_uint8_t *)addr) + offset;
    return PAL_ByteSwap4(*(pal_uint32_t*) src);
}

/* @summary Reads a 64-bit value from a buffer and performs a byte swap operation on it before returning it as the final destination type (64-bit unsigned.)
 * @param addr A pointer to the buffer from which the value will be read.
 * @param offset The offset into the buffer at which to read the value.
 * @return The byte-swapped value.
 */
static PAL_INLINE pal_uint64_t
PAL_ReadSwap_i64
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
    pal_uint8_t   *src = ((pal_uint8_t *)addr) + offset;
    return PAL_ByteSwap8(*(pal_uint64_t*) src);
}

/* @summary Reads a 32-bit value from a buffer and performs a byte swap operation on it before returning it as the final destination type (32-bit floating point.)
 * @param addr A pointer to the buffer from which the value will be read.
 * @param offset The offset into the buffer at which to read the value.
 * @return The byte-swapped value.
 */
static PAL_INLINE pal_float32_t
PAL_ReadSwap_f32
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
    typedef union {
        pal_uint32_t  u32;
        pal_float32_t f32;
    } ui32_or_f32;

    ui32_or_f32  val;
    pal_uint8_t *src = ((pal_uint8_t *)addr) + offset;
    val.u32 = PAL_ByteSwap4(*(pal_uint32_t*) src);
    return val.f32;
}

/* @summary Reads a 64-bit value from a buffer and performs a byte swap operation on it before returning it as the final destination type (64-bit floating point.)
 * @param addr A pointer to the buffer from which the value will be read.
 * @param offset The offset into the buffer at which to read the value.
 * @return The byte-swapped value.
 */
static PAL_INLINE pal_float64_t
PAL_ReadSwap_f64
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
    typedef union {
        pal_uint64_t  u64;
        pal_float64_t f64;
    } ui64_or_f64;

    ui64_or_f64  val;
    pal_uint8_t *src = ((pal_uint8_t *)addr) + offset;
    val.u64 = PAL_ByteSwap8(*(pal_uint64_t*) src);
    return val.f64;
}

/* @summary Perform a byte swapping operation on a signed 16-bit integer value and write it to a memory location.
 * @param addr A pointer to the buffer where the value will be written.
 * @param value The value to write to the buffer.
 * @param offset The offset into the buffer at which to write the value.
 */
static PAL_INLINE void
PAL_SwapWrite_si16
(
    void           *addr,
    pal_sint16_t   value,
    pal_ptrdiff_t offset
)
{
    pal_uint8_t * dst = ((pal_uint8_t *) addr) + offset;
  *(pal_uint16_t*)dst = PAL_ByteSwap2(*(pal_uint16_t*) &value);
}

/* @summary Perform a byte swapping operation on an unsigned 16-bit integer value and write it to a memory location.
 * @param addr A pointer to the buffer where the value will be written.
 * @param value The value to write to the buffer.
 * @param offset The offset into the buffer at which to write the value.
 */
static PAL_INLINE void
PAL_SwapWrite_ui16
(
    void           *addr,
    pal_uint16_t   value,
    pal_ptrdiff_t offset
)
{
    pal_uint8_t * dst = ((pal_uint8_t *) addr) + offset;
  *(pal_uint16_t*)dst = PAL_ByteSwap2(value);
}

/* @summary Perform a byte swapping operation on a signed 32-bit integer value and write it to a memory location.
 * @param addr A pointer to the buffer where the value will be written.
 * @param value The value to write to the buffer.
 * @param offset The offset into the buffer at which to write the value.
 */
static PAL_INLINE void
PAL_SwapWrite_si32
(
    void           *addr,
    pal_sint32_t   value,
    pal_ptrdiff_t offset
)
{
    pal_uint8_t * dst = ((pal_uint8_t *) addr) + offset;
  *(pal_uint32_t*)dst = PAL_ByteSwap4(*(pal_uint32_t*) &value);
}

/* @summary Perform a byte swapping operation on an unsigned 32-bit integer value and write it to a memory location.
 * @param addr A pointer to the buffer where the value will be written.
 * @param value The value to write to the buffer.
 * @param offset The offset into the buffer at which to write the value.
 */
static PAL_INLINE void
PAL_SwapWrite_ui32
(
    void           *addr,
    pal_uint32_t   value,
    pal_ptrdiff_t offset
)
{
    pal_uint8_t * dst = ((pal_uint8_t *) addr) + offset;
  *(pal_uint32_t*)dst = PAL_ByteSwap4(value);
}

/* @summary Perform a byte swapping operation on a signed 64-bit integer value and write it to a memory location.
 * @param addr A pointer to the buffer where the value will be written.
 * @param value The value to write to the buffer.
 * @param offset The offset into the buffer at which to write the value.
 */
static PAL_INLINE void
PAL_SwapWrite_si64
(
    void           *addr,
    pal_sint64_t   value,
    pal_ptrdiff_t offset
)
{
    pal_uint8_t * dst = ((pal_uint8_t *) addr) + offset;
  *(pal_uint64_t*)dst = PAL_ByteSwap8(*(pal_uint64_t*) &value);
}

/* @summary Perform a byte swapping operation on an unsigned 64-bit integer value and write it to a memory location.
 * @param addr A pointer to the buffer where the value will be written.
 * @param value The value to write to the buffer.
 * @param offset The offset into the buffer at which to write the value.
 */
static PAL_INLINE void
PAL_SwapWrite_ui64
(
    void           *addr,
    pal_uint64_t   value,
    pal_ptrdiff_t offset
)
{
    pal_uint8_t * dst = ((pal_uint8_t *) addr) + offset;
  *(pal_uint64_t*)dst = PAL_ByteSwap8(value);
}

/* @summary Perform a byte swapping operation on a 32-bit floating point value and write it to a memory location.
 * @param addr A pointer to the buffer where the value will be written.
 * @param value The value to write to the buffer.
 * @param offset The offset into the buffer at which to write the value.
 */
static PAL_INLINE void
PAL_SwapWrite_f32
(
    void           *addr,
    pal_float32_t  value,
    pal_ptrdiff_t offset
)
{
    typedef union {
        pal_uint32_t  u32;
        pal_float32_t f32;
    } ui32_or_f32;

    ui32_or_f32   val = { .f32 = value };
    pal_uint8_t * dst = ((pal_uint8_t *) addr) + offset;
  *(pal_uint32_t*)dst = PAL_ByteSwap4(val.u32);
}

/* @summary Perform a byte swapping operation on a 64-bit floating point value and write it to a memory location.
 * @param addr A pointer to the buffer where the value will be written.
 * @param value The value to write to the buffer.
 * @param offset The offset into the buffer at which to write the value.
 */
static PAL_INLINE void
PAL_SwapWrite_f64
(
    void           *addr,
    pal_float64_t  value,
    pal_ptrdiff_t offset
)
{
    typedef union {
        pal_uint64_t  u64;
        pal_float64_t f64;
    } ui64_or_f64;

    ui64_or_f64   val = { .f64 = value };
    pal_uint8_t * dst = ((pal_uint8_t *) addr) + offset;
  *(pal_uint64_t*)dst = PAL_ByteSwap8(val.u64);
}

/* @summary Compute the number of levels and size of the state data required for a memory allocator with a given configuration.
 * @param info The PAL_MEMORY_INDEX_SIZE to populate.
 * @param allocation_size_min The minimum allocation size, in bytes. This must be a power-of-two, greater than zero.
 * @param allocation_size_max The maximum allocation size, in bytes. This must be a power-of-two, greater than the minimum allocation size.
 */
static void
PAL_MemoryAllocatorQueryMemoryIndexSize
(
    PAL_MEMORY_INDEX_SIZE     *info, 
    pal_usize_t allocation_size_min, 
    pal_usize_t allocation_size_max
)
{
    pal_uint32_t           max_bit = 0;
    pal_uint32_t           min_bit = 0;
    pal_uint32_t       level_count;
    pal_uint64_t  split_index_size; /* in machine words */
    pal_uint64_t status_index_size; /* in machine words */

    assert(allocation_size_min > 0);
    assert(allocation_size_max > 0);
    assert(allocation_size_min < allocation_size_max);

    /* determine the number of levels */
    PAL_BitScan_ui64_msb(allocation_size_min, &min_bit);
    PAL_BitScan_ui64_msb(allocation_size_max, &max_bit);
    level_count           = ((max_bit - min_bit)  + 1);
    /* calculate index sizes in words */
    split_index_size      = ((PAL_WORDSIZE_ONE << (level_count-1)) + PAL_WORDSIZE_MASK) >> PAL_WORDSIZE_SHIFT;
    status_index_size     = ((PAL_WORDSIZE_ONE << (level_count  )) + PAL_WORDSIZE_MASK) >> PAL_WORDSIZE_SHIFT;
    /* calculate index sizes in bytes */
    info->SplitIndexSize  = split_index_size  * PAL_WORDSIZE_BYTES;
    info->StatusIndexSize = status_index_size * PAL_WORDSIZE_BYTES;
    info->TotalIndexSize  =(status_index_size + split_index_size) * PAL_WORDSIZE_BYTES;
    info->MinBitIndex     = min_bit;
    info->MaxBitIndex     = max_bit;
    info->LevelCount      = level_count;
}

/* @summary Compute information about a single level in a memory allocator instance.
 * @param info The PAL_MEMORY_ALLOCATOR_LEVEL structure to populate.
 * @param level_index The zero-based index of the level to describe, with level 0 being the largest level.
 * @param level_bit The zero-based index of the bit that specifies the size of the level, with level 0 being AllocationSizeMax.
 * @param blocks_reserved The number of whole blocks that are reserved by the BytesReserved parameter. This argument should only be non-zero for the last level.
 */
static void
PAL_MemoryAllocatorDescribeLevel
(
    PAL_MEMORY_ALLOCATOR_LEVEL *info, 
    pal_uint32_t         level_index, 
    pal_uint32_t           level_bit, 
    pal_uint32_t     blocks_reserved
)
{
    pal_uint64_t block_size  = 1ULL << level_bit;
    pal_uint32_t block_count =(1UL  << level_index) - blocks_reserved;
    pal_uint32_t first_block =(1UL  << level_index) - 1;
    pal_uint32_t final_block =(first_block + block_count) - 1;
    pal_uint32_t word_index_0= first_block >> PAL_WORDSIZE_SHIFT;
    pal_uint32_t word_index_N= final_block >> PAL_WORDSIZE_SHIFT;
    pal_uint32_t word_bits_0;
    pal_uint32_t word_bits_N;
    pal_uint64_t word_mask_0;
    pal_uint64_t word_mask_N;

    /* the split and status index are stored as bitvectors, with each level 
     * tightly packed together. when searching for a free block, the status 
     * index is searched 32- or 64-bits at a time. it looks like this:
     * 0  1    2        3                4                                5
     * b|bb|bbbb|bbbbbbbb|bbbbbbbbbbbbbbbb|bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
     * where b is a 0 or 1 bit, and | denotes separation between the levels.
     * thus, it is possible that the bitvector for a single level spans 
     * machine words, which is inconvenient. pre-compute the mask values used
     * to access only the bits in the bitvector for the first and last words
     * of the level. in the first word, the bits will be stored in the more-
     * significant bits of the word, while in the final word the bits will 
     * be stored as the least-significant bits of the word.
     */
    if (block_count < PAL_WORDSIZE_BITS)
    {   /* the level doesn't fill an entire machine word in the index */
        word_bits_0 = block_count;
        word_bits_N = block_count;
    }
    else
    {   /* the level fills one or more machine words in the index.
           calculate the number of bits set in the first and final word. */
        word_bits_0 = PAL_WORDSIZE_BITS - (first_block & PAL_WORDSIZE_MASK);
        word_bits_N =(block_count - word_bits_0) & PAL_WORDSIZE_MASK;
    }
    if (word_bits_0 != PAL_WORDSIZE_BITS)
    {   /* compute the mask required to access only the level's bits in the first word.
           bits in the first word are stored in the more-significant bits of the word.
          (PAL_WORDSIZE_ONE << N) - 1 sets the lower N bits in the word. */
        word_mask_0 = ((PAL_WORDSIZE_ONE << word_bits_0) - 1) << (first_block & PAL_WORDSIZE_MASK);
    }
    else
    {   /* the level uses all bits in the first word */
        word_mask_0 = ~PAL_WORDSIZE_ZERO;
    }
    if (word_bits_N != PAL_WORDSIZE_BITS)
    {   /* compute the mask required to access only the level's bits in the final word */
        if (word_index_0 == word_index_N)
        {   /* all bits for the level fit in the first word, so use that mask */
            word_mask_N = word_mask_0;
        }
        else
        {   /* bits in the final word reside in the least-signficant bits of the word.
              (PAL_WORDSIZE_ONE << N) - 1 sets the lower N bits in the word */
            word_mask_N = ((PAL_WORDSIZE_ONE << word_bits_N) - 1);
        }
    }
    else
    {   /* the level uses all bits in the final word */
        word_mask_N = ~PAL_WORDSIZE_ZERO;
    }
    info->BlockSize       = block_size;
    info->BlockCount      = block_count;
    info->LevelBit        = level_bit;
    info->FirstBlockIndex = first_block;
    info->FinalBlockIndex = final_block;
    info->WordIndex0      = word_index_0;
    info->WordIndexN      = word_index_N;
    info->WordMask0       = word_mask_0;
    info->WordMaskN       = word_mask_N;
}

/* @summary Locate the first free memory block at a given level. The block with the lowest offset is returned.
 * @param info The PAL_MEMORY_BLOCK_INFO to populate with information about the block at the specified offset.
 * @param alloc The PAL_MEMORY_ALLOCATOR to query. This must be the same allocator that returned the block.
 * @param level The zero-based index of the level at which the block was allocated.
 * @return One if a free memory block is located at the specified level, or zero if the level has no free blocks.
 */
static int
PAL_MemoryAllocatorFindFreeMemoryBlockAtLevel
(
    PAL_MEMORY_BLOCK_INFO *block, 
    PAL_MEMORY_ALLOCATOR  *alloc, 
    pal_uint32_t           level
)
{
    PAL_MEMORY_ALLOCATOR_LEVEL *info =&alloc->LevelInfo[level];
    pal_uint64_t       *status_index = alloc->StatusIndex;
    pal_uint64_t           word_mask = info->WordMaskN;
    pal_uint32_t          word_index = info->WordIndexN;
    pal_uint32_t          first_word = info->WordIndex0;
    pal_uint32_t         local_index = 0;
    pal_uint32_t             set_bit = 0;

    /* this implementation prefers to return higher-address blocks first, so 
     * the index is searched from the last block to the first block, and words 
     * are searched from MSB->LSB. returning higher-address blocks first makes 
     * it easy to support the BytesReserved feature.
     */
    while (word_index > first_word)
    {   /* find the first set bit indicating a free block */
        if (PAL_BitScan_ui64_msb(status_index[word_index] & word_mask, &set_bit))
            goto found_free_block;
        /* check all bits in words other than the last word */
        word_mask = ~PAL_WORDSIZE_ZERO;
        word_index--;
    }
    
    /* still nothing, so check the first word */
    if (PAL_BitScan_ui64_msb(status_index[first_word] & info->WordMask0, &set_bit))
        goto found_free_block;

    /* no free blocks at this level */
    block->BlockOffset = 0;
    block->LevelShift  = 0;
    block->BlockIndex  = 0;
    return 0;

found_free_block:
    local_index        =(word_index << PAL_WORDSIZE_SHIFT) + set_bit - info->FirstBlockIndex;
    block->BlockOffset = info->BlockSize * local_index;
    block->LevelShift  = info->LevelBit;
    block->BlockIndex  = info->FirstBlockIndex + local_index;
    return 1;
}

/* @summary Retrieve the commit status for a chunk, indicating whether the chunk memory can be accessed.
 * @param table The PAL_HANDLE_TABLE to query.
 * @param chunk_index The zero-based index of the chunk to query.
 * @return Non-zero if the chunk address space is committed, or zero if the chunk address space is not committed.
 */
static PAL_INLINE pal_uint64_t
PAL_HandleTableGetChunkCommitStatus
(
    struct PAL_HANDLE_TABLE *table, 
    pal_uint32_t       chunk_index
)
{
    pal_uint32_t  bit = chunk_index  & PAL_HANDLE_CHUNK_WORD_MASK;
    pal_uint32_t word = chunk_index >> PAL_HANDLE_CHUNK_WORD_SHIFT;
    pal_uint64_t mask = 1ULL << bit;
    return (table->ChunkCommit[word] & mask);
}

/* @summary Mark a chunk as being committed, meaning that the chunk memory can be read and written.
 * @param table The PAL_HANDLE_TABLE to update.
 * @param chunk_index The zero-based index of the committed chunk.
 */
static PAL_INLINE void
PAL_HandleTableSetChunkCommitStatus
(
    struct PAL_HANDLE_TABLE *table, 
    pal_uint32_t       chunk_index
)
{
    pal_uint32_t  bit = chunk_index  & PAL_HANDLE_CHUNK_WORD_MASK;
    pal_uint32_t word = chunk_index >> PAL_HANDLE_CHUNK_WORD_SHIFT;
    pal_uint64_t mask = 1ULL << bit;
    table->ChunkCommit[word] |= mask;
}

/* @summary Mark a handle table chunk as having one or more slots available for use.
 * @param table The PAL_HANDLE_TABLE to update.
 * @param chunk_index The zero-based index of the available chunk.
 */
static PAL_INLINE void
PAL_HandleTableMarkChunkAvailable
(
    struct PAL_HANDLE_TABLE *table, 
    pal_uint32_t       chunk_index
)
{
    pal_uint32_t  bit = chunk_index  & PAL_HANDLE_CHUNK_WORD_MASK;
    pal_uint32_t word = chunk_index >> PAL_HANDLE_CHUNK_WORD_SHIFT;
    pal_uint64_t mask = 1ULL << bit;
    table->ChunkStatus[word] |= mask;
}

/* @summary Mark a handle table chunk as being completely full and unavailable for further use.
 * @param table The PAL_HANDLE_TABLE to update.
 * @param chunk_index The zero-based index of the full chunk.
 */
static PAL_INLINE void
PAL_HandleTableMarkChunkFull
(
    struct PAL_HANDLE_TABLE *table, 
    pal_uint32_t       chunk_index
)
{
    pal_uint32_t  bit = chunk_index  & PAL_HANDLE_CHUNK_WORD_MASK;
    pal_uint32_t word = chunk_index >> PAL_HANDLE_CHUNK_WORD_SHIFT;
    pal_uint64_t mask = 1ULL << bit;
    table->ChunkStatus[word] &=~mask;
}

/* @summary Retrieve the base memory address of the data for a chunk.
 * The caller must verify that the chunk stores data internally.
 * @param table The PAL_HANDLE_TABLE to query.
 * @param chunk_index The zero-based index of the chunk to query.
 * @return A pointer to the start of the chunk data block (which is also the base address of the chunk). 
 * The caller should have previously ensured that the chunk is committed prior to dereferencing any address within the address range.
 */
static PAL_INLINE pal_uint8_t*
PAL_HandleTableGetChunkData
(
    struct PAL_HANDLE_TABLE *table,
    pal_uint32_t       chunk_index
)
{
    return table->BaseAddress + (table->ChunkSize * chunk_index);
}

/* @summary Retrieve a pointer to the start of the sparse state array for a given chunk.
 * @param table The PAL_HANDLE_TABLE to query.
 * @param chunk_index The zero-based index of the chunk to query.
 * @return A pointer to the start of the sparse data array used to map handle values to dense indices.
 */
static PAL_INLINE pal_uint32_t*
PAL_HandleTableGetChunkState
(
    struct PAL_HANDLE_TABLE *table, 
    pal_uint32_t       chunk_index
)
{   /* chunk memory layout is [data][pad][state][dense].
     * the state and dense arrays are each PAL_HANDLE_CHUNK_CAPACITY 32-bit unsigned integers.
     * so we compute the address of the start of the next chunk, the subtract to get to the metadata. */
    pal_usize_t  chunk_size = table->ChunkSize;
    pal_usize_t  array_size = PAL_HANDLE_CHUNK_CAPACITY * sizeof(pal_uint32_t);
    pal_uint8_t *next_chunk =(chunk_size * chunk_index) + table->BaseAddress + chunk_size;
    return    (pal_uint32_t*)(next_chunk -(array_size   * 2));
}

/* @summary Retrieve a pointer to the start of the dense handle array for a given chunk.
 * @param table The PAL_HANDLE_TABLE to query.
 * @param chunk_index The zero-based index of the chunk to query.
 * @return A pointer to the start of the dense handle array specifying the live handles in the chunk.
 */
static PAL_INLINE pal_uint32_t*
PAL_HandleTableGetChunkDense
(
    struct PAL_HANDLE_TABLE *table, 
    pal_uint32_t       chunk_index
)
{   /* chunk memory layout is [data][pad][state][dense].
     * the state and dense arrays are each PAL_HANDLE_CHUNK_CAPACITY 32-bit unsigned integers.
     * so we compute the address of the start of the next chunk, the subtract to get to the metadata. */
    pal_usize_t  chunk_size = table->ChunkSize;
    pal_usize_t  array_size = PAL_HANDLE_CHUNK_CAPACITY * sizeof(pal_uint32_t);
    pal_uint8_t *next_chunk =(chunk_size * chunk_index) + table->BaseAddress + chunk_size;
    return    (pal_uint32_t*)(next_chunk - array_size);
}

/* @summary Commit a single chunk, making it possible to read and write the chunk address space.
 * This operation is used by PAL_HandleTableInsertIds, where the committed chunks within the table may be sparse.
 * @param table The PAL_HANDLE_TABLE to update.
 * @param chunk_index The zero-based index of the chunk to commit.
 * @return The base address of the chunk, or NULL if the chunk could not be committed.
 */
static pal_uint8_t*
PAL_HandleTableCommitChunk
(
    struct PAL_HANDLE_TABLE *table, 
    pal_uint32_t       chunk_index
)
{
    pal_uint32_t    *chunk_init = table->ChunkInit;
    pal_usize_t      chunk_size = table->ChunkSize;
    pal_usize_t      array_size = PAL_HANDLE_CHUNK_CAPACITY * sizeof(pal_uint32_t);
    pal_uint8_t     *chunk_addr = table->BaseAddress + (chunk_size * chunk_index);
    pal_uint8_t     *next_chunk = table->BaseAddress + (chunk_size * chunk_index) + chunk_size;
    pal_uint32_t   *chunk_dense =(pal_uint32_t*)(next_chunk - array_size);
    pal_uint32_t     chunk_word = chunk_index >> PAL_HANDLE_CHUNK_WORD_SHIFT;
    pal_uint32_t      chunk_bit = chunk_index  & PAL_HANDLE_CHUNK_WORD_MASK;
    pal_uint64_t     chunk_mask = 1ULL << chunk_bit;
    if (VirtualAlloc(chunk_addr, chunk_size, MEM_COMMIT, PAGE_READWRITE) != chunk_addr)
    {   /* failed to commit the address space */
        return NULL;
    }
    /* initialize the free list in the chunk's dense array.
     * VirtualAlloc zero-initializes the committed memory, so the state array is all set.
     * mark the chunk as being committed by setting the bit in ChunkCommit.
     * mark the chunk as having free slots by setting the bit in ChunkStatus.
     * reset the number of used slots in the chunk to zero.
     */
    PAL_CopyMemory(chunk_dense, chunk_init, array_size);
    table->ChunkCommit[chunk_word] |= chunk_mask;
    table->ChunkStatus[chunk_word] |= chunk_mask;
    table->ChunkCounts[chunk_index] = 0;
    table->CommitCount++;
    return chunk_addr;
}

/* @summary Ensure that the handle table can fulfill a request for a specific number of handles, committing one or more chunks if necessary.
 * This operation is used by PAL_HandleTableCreateIds, where the table fills prior to committing additional chunks.
 * @param table The PAL_HANDLE_TABLE to query and possibly update.
 * @param count The number of handles or IDs that are required.
 * @param index_list An array of values to be populated with the zero-based indices of the chunks satisfying the allocation request.
 * @param count_list An array of values to be populated with the number of items allocated from each chunk to satisfy the allocation request.
 * @param chunk_count Pointer to a 32-bit value that on return is set to the number of items written to the index_list and count_list arrays.
 * @return Zero if the table can accomodate the request, or non-zero if the table cannot accomodate the request.
 */
static int
PAL_HandleTableEnsure
(
    struct PAL_HANDLE_TABLE *table, 
    pal_usize_t              count, 
    pal_uint16_t       *index_list, 
    pal_uint16_t       *count_list, 
    pal_uint32_t      *chunk_count
)
{
    pal_usize_t   array_size = PAL_HANDLE_CHUNK_CAPACITY * sizeof(pal_uint32_t);
    pal_uint32_t *chunk_init = table->ChunkInit;
    pal_usize_t   chunk_size = table->ChunkSize;
    pal_uint64_t     *status = table->ChunkStatus;
    pal_uint64_t     *commit = table->ChunkCommit;
    pal_uint16_t     *counts = table->ChunkCounts;
    pal_uint8_t     *commitp = NULL;
    pal_uint8_t        *next = NULL;
    pal_uint32_t      *dense = NULL;
    pal_uint64_t     statusw = 0; /* the current word in ChunkStatus */
    pal_uint64_t     commitw = 0; /* the current word in ChunkCommit*/
    pal_uint64_t     bitmask = 0; /* one bit set for current chunk in bitvector word */
    pal_usize_t      ncommit = 0; /* the number of chunks to commit */
    pal_usize_t        avail = 0; /* the number of available slots */
    pal_uint32_t       chunk = 0; /* temporary storage for chunk index */
    pal_uint32_t        word = 0; /* the word index in the bitvector */
    pal_uint32_t         bit = 0; /* the bit index in the bitvector word */
    pal_uint16_t         out = 0; /* the index of the next item to write in index_list */
    pal_uint16_t         num = 0; /* the number of slots available in the current chunk */
    pal_uint32_t           i;

    /* scan through the status and commit bitvectors.
     * if a chunk has its status bit set, see how many free slots it has.
     * if the status bit is not set, check the commit bit. if the commit bit is not set, commit the chunk.
     */
    while (word < PAL_HANDLE_CHUNK_WORD_COUNT && avail < count)
    {   /* reset for the next group of chunks */
        statusw = status[word];
        commitw =~commit[word]; /* invert so we uncommitted chunks have set bits */

        /* if statusw == 0, no chunks in this group have free slots.
         * if commitw == 0, all chunks in this group are committed.
         * if both of these conditions hold, then this group of chunks can be skipped.
         * if statusw != 0, then at least one chunk has at least one free slot.
         * if commitw != 0, then at least one chunk in this group is not committed.
         */
        while (statusw && PAL_BitScan_ui64_lsb(statusw, &bit))
        {   /* a chunk has one or more free slots */
            bitmask = 1ULL << bit;
            chunk   =(word << PAL_HANDLE_CHUNK_WORD_SHIFT)+bit;
            num     = PAL_HANDLE_CHUNK_CAPACITY - counts[chunk];
            if ((avail + num) >= count)
            {   /* this chunk may have remaining free slots */
                index_list[out  ] =(pal_uint16_t) chunk;
                count_list[out++] =(pal_uint16_t)(count - avail);
                PAL_Assign(chunk_count, out);
                return 0;
            }
            else
            {   /* the allocation will consume all free slots in the chunk */
                index_list[out  ] =(pal_uint16_t) chunk;
                count_list[out++] =(pal_uint16_t) num;
                avail += num;
            }
            /* clear the (local) bit so it doesn't show up in the next search */
            statusw &= ~bitmask;
        }

        if (commitw != 0 && PAL_BitScan_ui64_lsb(commitw, &bit))
        {   /* there's at least one uncommitted chunk in this group.
             * this implies that any subsequent chunks are also not 
             * committed, because the table will fill completely 
             * before committing memory to additional chunks. since 
             * VirtualAlloc calls are expensive, try to minimize 
             * them by making just one call to commit memory. */
            chunk   =(word << PAL_HANDLE_CHUNK_WORD_SHIFT)+bit;
            bitmask = 1ULL << bit;

            if ((PAL_HANDLE_CHUNK_CAPACITY * (PAL_HANDLE_CHUNK_COUNT - chunk)) < (count - avail))
            {   /* there are not enough free slots in the table to accomodate the request*/
                PAL_Assign(chunk_count, 0);
                return -1;
            }

            next    = table->BaseAddress + (chunk_size * chunk);
            commitp = table->BaseAddress + (chunk_size * chunk);
            ncommit = ((count - avail) + (PAL_HANDLE_CHUNK_CAPACITY - 1)) / PAL_HANDLE_CHUNK_CAPACITY;
            if (VirtualAlloc(commitp, chunk_size * ncommit, MEM_COMMIT, PAGE_READWRITE) != commitp)
            {   /* failed to commit address space for the required number of chunks */
                PAL_Assign(chunk_count, 0);
                return -1;
            }

            for (i = 0; i < ncommit; ++i, ++chunk)
            {
                if ((avail + PAL_HANDLE_CHUNK_CAPACITY) < count)
                {   /* this entire chunk will be consumed by the allocation */
                    num = PAL_HANDLE_CHUNK_CAPACITY;
                }
                else
                {   /* this chunk will be partially consumed by the allocation */
                    num =(pal_uint16_t)(count - avail);
                }
                index_list[out  ] =(pal_uint16_t) chunk;
                count_list[out++] =(pal_uint16_t) num;
                avail += num;

                /* initialize the chunk state.
                 * VirtualAlloc zeroed the chunk state array.
                 * the chunk free list and count need to be set.  */
                next += chunk_size;
                dense =(pal_uint32_t*)(next - array_size);
                PAL_CopyMemory(dense, chunk_init, array_size);
                counts[chunk] = 0;
            }
            for (i = 0; i < ncommit; ++i)
            {
                status[word] |= bitmask;
                commit[word] |= bitmask;
                bit++;

                if (bit != PAL_HANDLE_CHUNK_WORD_BITS)
                {   /* same word, update set bit */
                    bitmask <<= 1;
                }
                else
                {   /* move to the next word */
                    bitmask = 1; bit = 0; ++word;
                }
            }
            table->CommitCount += (pal_uint32_t) ncommit; /* ncommit in [0, PAL_HANDLE_CHUNK_COUNT) */
            PAL_Assign(chunk_count, out);
            return 0;
        }

        /* check the next group of 64 chunks */
        word++;
    }
    PAL_Assign(chunk_count, 0);
    return -1;
}

/* @summary Create one or more handles within a given chunk.
 * The caller must ensure that the chunk is committed.
 * The caller must ensure that the chunk has enough space for the requested number of IDs.
 * @param table The PAL_HANDLE_TABLE to update.
 * @param handles The array to populate with generated handle values.
 * @param offset The zero-based offset within the handles array where the first handle should be written.
 * @param chunk The zero-based index of the chunk from which the handles should be allocated.
 * @param count The number of handles to allocate from the chunk.
 * @return The number of handles allocated from the chunk and written to the handles array. This value should be used to increment the offset value.
 */
static pal_uint32_t
PAL_HandleTableCreateIdsInChunk
(
    struct PAL_HANDLE_TABLE *table, 
    PAL_HANDLE            *handles, 
    pal_uint32_t            offset, 
    pal_uint32_t            nspace, 
    pal_uint32_t             chunk, 
    pal_uint16_t             count
)
{
    pal_uint32_t      *state = PAL_HandleTableGetChunkState(table, chunk);
    pal_uint32_t      *dense = PAL_HandleTableGetChunkDense(table, chunk);
    pal_uint16_t         num = PAL_HandleTableGetChunkItemCount(table, chunk);
    pal_uint16_t           n = 0;
    pal_uint32_t dense_index = 0;
    pal_uint32_t state_index = 0;
    pal_uint32_t state_value = 0;
    pal_uint32_t  generation = 0;
    pal_usize_t            i;

    if ((num + count) == PAL_HANDLE_CHUNK_CAPACITY)
    {   /* the chunk will be entirely used after this call */
        PAL_HandleTableMarkChunkFull(table, chunk);
    }
    PAL_HandleTableSetChunkItemCount(table, chunk, num + count);
    
    /* allocate new handles from the chunk */
    for (i = 0; i < count; ++i, ++num, ++n)
    {
        dense_index        = num;
        state_index        = dense[num];
        state_value        = state[state_index];
        generation         = PAL_HandleStateGetGeneration(state_value);
        dense[dense_index] = PAL_HandleValuePack(chunk, state_index, nspace, generation);
        state[state_index] = PAL_HANDLE_VALID_MASK_PACKED | (dense_index << PAL_HANDLE_INDEX_SHIFT) | (generation << PAL_HANDLE_GENER_SHIFT);
        handles[offset + n]= dense[dense_index];
    }
    return n;
}

/* @summary Insert one or more externally-created IDs within a chunk and allocate storage slots to them.
 * Insertion is orthagonal to creation - one table is responsible for creating IDs, but the data associated with those IDs may be stored across several tables.
 * @param table The PAL_HANDLE_TABLE to update.
 * @param handles The array of handles to insert into the chunk.
 * @param range_beg The zero-based index of the first ID to insert from the handles array (inclusive lower bound).
 * @param range_end The zero-based index within the handles array at which insertion will stop (exclusive upper bound).
 * @param chunk_index The zero-based index of the chunk into which the handles are being inserted.
 * @param chunk_count The number of elements stored in the chunk prior to performing any insertion.
 * @param state Pointer to the start of the state array for the chunk.
 * @param dense Pointer to the start of the dense array for the chunk.
 * @return The number of handles actually inserted into the chunk. Occupied slots are not overwritten.
 */
static pal_uint32_t
PAL_HandleTableInsertIdsInChunk
(
    struct PAL_HANDLE_TABLE *table, 
    PAL_HANDLE            *handles, 
    pal_usize_t          range_beg, 
    pal_usize_t          range_end, 
    pal_uint32_t       chunk_index, 
    pal_uint16_t       chunk_count, 
    pal_uint32_t            *state, 
    pal_uint32_t            *dense
)
{
    pal_uint32_t  state_value;
    pal_uint32_t  state_index;
    pal_uint32_t  dense_index;
    pal_uint32_t   generation;
    pal_uint32_t       handle;
    pal_uint16_t insert_count;

    if (PAL_HandleTableGetChunkCommitStatus(table, chunk_index) == 0)
    {   /* this chunk is not committed - commit it */
        if (PAL_HandleTableCommitChunk(table, chunk_index) != 0)
        {   /* failed to commit the chunk - cannot proceed */
            return 0;
        }
    }

    /* insert_count tracks the number of IDs actually inserted in the chunk */
    insert_count = 0;

    /* for each item in the set of handles, locate the corresponding state slot.
     * ensure that either nothing occupies that slot, or that the value stored 
     * there matches the handle. it is unclear how errors should be handled. 
     * also, expired handles cannot be detected, because this table isn't the 
     * table that generated the IDs. */
    while (range_beg != range_end)
    {
        handle        = handles[range_beg++];
        generation    = PAL_HandleValueGetGeneration(handle);
        state_index   = PAL_HandleValueGetStateIndex(handle);
        state_value   = state[state_index];
        dense_index   = chunk_count + insert_count;
        if (state_value == 0)
        {   /* this slot is unoccupied, so proceed with the insert */
            state[state_index] = PAL_HANDLE_VALID_MASK_PACKED | (dense_index << PAL_HANDLE_INDEX_SHIFT) | (generation << PAL_HANDLE_GENER_SHIFT);
            dense[dense_index] = handle;
            insert_count++;
        }
    }

    /* update the chunk status */
    PAL_HandleTableSetChunkItemCount(table, chunk_index, chunk_count + insert_count);
    return insert_count;
}

/* @summary Delete a single ID from a chunk. At most one data element is moved.
 * The caller is responsible for ensuring that the ID being deleted is currently valid.
 * @param table The PAL_HANDLE_TABLE that created the ID being deleted.
 * @param handles The array of handles being deleted.
 * @param delete_index The zero-based index of the specific ID within the handles array representing the ID to delete.
 * @param chunk_index The zero-based index of the chunk from which the ID was allocated.
 * @param chunk_count The number of items currently in the chunk. This value must be at least 1.
 * @param state Pointer to the start of the state array for the chunk.
 * @param dense Pointer to the start of the dense array for the chunk.
 * @param data The PAL_MEMORY_VIEW used to access the data stored within the chunk.
 * @return The number of IDs deleted from the chunk (always 1).
 */
static pal_uint16_t
PAL_HandleTableDeleteIdsInChunk_One
(
    struct PAL_HANDLE_TABLE *table, 
    PAL_HANDLE            *handles, 
    pal_usize_t       delete_index, 
    pal_uint32_t       chunk_index, 
    pal_uint16_t       chunk_count, 
    pal_uint32_t            *state, 
    pal_uint32_t            *dense, 
    struct PAL_MEMORY_VIEW   *data
)
{
    pal_uint32_t   last_dense = chunk_count - 1;
    pal_uint32_t stream_count = PAL_MemoryViewStreamCount(data);
    pal_uint32_t  state_index = PAL_HandleValueGetStateIndex(handles[delete_index]);
    pal_uint32_t  moved_value = dense[last_dense];
    pal_uint32_t  dense_index = PAL_HandleStateGetDenseIndex(state[state_index]);
    pal_uint32_t            i;

    /* clear the liveness bit and dense index portions of the state.
     * increment the generation value to detect expired handles.
     */
    state[state_index] = (state[state_index]+1) & PAL_HANDLE_GENER_MASK;

    /* if the deleted item is not the last slot in the dense array, 
     * then swap the last live item into the slot vacated by the 
     * just-deleted item in order to keep the dense array packed.
     */
    if (dense_index != last_dense)
    {
        pal_uint32_t moved_state = PAL_HandleValueGetStateIndex(moved_value);
        pal_uint32_t moved_gener = PAL_HandleValueGetGeneration(moved_value);
        state[moved_state] = PAL_HANDLE_VALID_MASK_PACKED | (dense_index << PAL_HANDLE_INDEX_SHIFT) | (moved_gener << PAL_HANDLE_GENER_SHIFT);
        dense[dense_index] = moved_value;
        for (i = 0; i < stream_count; ++i)
        {
            pal_usize_t s = data->Stride[i];
            void   *dst_p = PAL_MemoryViewStreamAt(void*, data, i, dense_index);
            void   *src_p = PAL_MemoryViewStreamAt(void*, data, i, last_dense);
            PAL_CopyMemory(dst_p, src_p, s);
        }
    }

    /* return state_index to the free list */
    dense[last_dense] = state_index;

    /* update the chunk state */
    PAL_HandleTableMarkChunkAvailable(table, chunk_index);
    PAL_HandleTableSetChunkItemCount (table, chunk_index, chunk_count-1);
    return 1;
}

/* @summary Remove a single ID from a chunk. At most one data element is moved.
 * The caller is responsible for ensuring that the ID being removed is valid and stored within the chunk.
 * @param table The PAL_HANDLE_TABLE that created the ID being deleted.
 * @param handles The array of handles being deleted.
 * @param remove_index The zero-based index of the specific ID within the handles array representing the ID to remove.
 * @param chunk_index The zero-based index of the chunk from which the ID was allocated.
 * @param chunk_count The number of items currently in the chunk. This value must be at least 1.
 * @param state Pointer to the start of the state array for the chunk.
 * @param dense Pointer to the start of the dense array for the chunk.
 * @param data The PAL_MEMORY_VIEW used to access the data stored within the chunk.
 * @return The number of IDs removed from the chunk (always 1.)
 */
static pal_uint16_t
PAL_HandleTableRemoveIdsInChunk_One
(
    struct PAL_HANDLE_TABLE *table, 
    PAL_HANDLE            *handles, 
    pal_usize_t       remove_index, 
    pal_uint32_t       chunk_index, 
    pal_uint16_t       chunk_count, 
    pal_uint32_t            *state, 
    pal_uint32_t            *dense, 
    struct PAL_MEMORY_VIEW   *data
)
{
    pal_uint32_t   last_dense = chunk_count - 1;
    pal_uint32_t       handle = handles[remove_index];
    pal_uint32_t stream_count = PAL_MemoryViewStreamCount(data);
    pal_uint32_t  state_index = PAL_HandleValueGetStateIndex(handle);
    pal_uint32_t  state_value = state[state_index];
    pal_uint32_t  moved_value = dense[last_dense];
    pal_uint32_t  dense_index = PAL_HandleStateGetDenseIndex(state_value);
    pal_uint32_t            i;

    /* completely wipe out the state value, to mark the slot as unoccupied */
    state[state_index] = 0;

    /* if the deleted item is not the last slot in the dense array, 
     * then swap the last live item into the slot vacated by the 
     * just-deleted item in order to keep the dense array packed.
     */
    if (dense_index != last_dense)
    {
        pal_uint32_t moved_state = PAL_HandleValueGetStateIndex(moved_value);
        pal_uint32_t moved_gener = PAL_HandleValueGetGeneration(moved_value);
        state[moved_state] = PAL_HANDLE_VALID_MASK_PACKED | (dense_index << PAL_HANDLE_INDEX_SHIFT) | (moved_gener << PAL_HANDLE_GENER_SHIFT);
        dense[dense_index] = moved_value;
        for (i = 0; i < stream_count; ++i)
        {
            pal_usize_t s = data->Stride[i];
            void   *dst_p = PAL_MemoryViewStreamAt(void*, data, i, dense_index);
            void   *src_p = PAL_MemoryViewStreamAt(void*, data, i, last_dense);
            PAL_CopyMemory(dst_p, src_p, s);
        }
    }

    /* update the chunk state */
    PAL_HandleTableSetChunkItemCount(table, chunk_index, chunk_count-1);
    return 1;
}

/* @summary Delete all remaining items from the chunk. No data elements are moved.
 * The number of items to be deleted should be at least one, and less than or equal to the chunk capacity (1024).
 * The caller is responsible for ensuring that no invalid handles are included in the handles array.
 * @param table The PAL_HANDLE_TABLE that allocated the IDs being deleted.
 * @param handles The array of handles to delete. Handles are deleted in a contiguous range starting from range_beg, up to but not including range_end.
 * @param range_beg The zero-based index of the first ID to delete from the handles array (inclusive lower bound).
 * @param range_end The zero-based index within the handles array at which deletion will stop (exclusive upper bound).
 * @param chunk_index The zero-based index of the chunk from which the ID was allocated.
 * @param state Pointer to the start of the state array for the chunk.
 * @param dense Pointer to the start of the dense array for the chunk.
 * @return The number of IDs deleted from the chunk.
 */
static pal_uint16_t
PAL_HandleTableDeleteIdsInChunk_All
(
    struct PAL_HANDLE_TABLE *table, 
    PAL_HANDLE            *handles, 
    pal_usize_t          range_beg, 
    pal_usize_t          range_end, 
    pal_uint32_t       chunk_index, 
    pal_uint32_t            *state, 
    pal_uint32_t            *dense
)
{
    pal_uint32_t  state_index;
    pal_uint32_t  state_value;
    pal_uint32_t  dense_index;
    pal_uint16_t delete_count = 0;

    while (range_beg != range_end)
    {
        state_index        = PAL_HandleValueGetStateIndex(handles[range_beg++]);
        state_value        = state[state_index];
        dense_index        = PAL_HandleStateGetDenseIndex(state_value);
        state[state_index] =(state_value+1) & PAL_HANDLE_GENER_MASK;
        dense[dense_index] = state_index;
        delete_count++;
    }

    /* update the chunk state */
    PAL_HandleTableMarkChunkAvailable(table, chunk_index);
    PAL_HandleTableSetChunkItemCount (table, chunk_index, 0);
    return delete_count;
}

/* @summary Remove all remaining items from the chunk. No data elements are moved.
 * The number of items to be removed should be at least one, and less than or equal to the chunk capacity (1024).
 * The caller is responsible for ensuring that no invalid handles are included in the handles array.
 * The caller is responsible for ensuring that all handles to be removed are actually present in the table.
 * @param table The PAL_HANDLE_TABLE that allocated the IDs being deleted.
 * @param handles The array of handles to remove. Handles are removed in a contiguous range starting from range_beg, up to but not including range_end.
 * @param range_beg The zero-based index of the first ID to remove from the handles array (inclusive lower bound).
 * @param range_end The zero-based index within the handles array at which removal will stop (exclusive upper bound).
 * @param chunk_index The zero-based index of the chunk being updated.
 * @param state Pointer to the start of the state array for the chunk.
 * @return The number of IDs removed from the chunk.
 */
static pal_uint16_t
PAL_HandleTableRemoveIdsInChunk_All
(
    struct PAL_HANDLE_TABLE *table, 
    PAL_HANDLE            *handles, 
    pal_usize_t          range_beg, 
    pal_usize_t          range_end, 
    pal_uint32_t       chunk_index, 
    pal_uint32_t            *state
)
{
    pal_uint32_t  state_index;
    pal_uint16_t remove_count = 0;

    while (range_beg != range_end)
    {
        state_index = PAL_HandleValueGetStateIndex(handles[range_beg++]);
        state[state_index] = 0; /* mark as unoccupied */
        remove_count++;
    }

    /* update the chunk state */
    PAL_HandleTableSetChunkItemCount(table, chunk_index, 0);
    return remove_count;
}

/* @summary Delete the full chunk. All 1024 IDs are allocated, and all will be deleted. No data elements are moved.
 * @param table The PAL_HANDLE_TABLE that allocated the IDs being deleted.
 * @param chunk_index The zero-based index of the chunk from which the ID was allocated.
 * @param state Pointer to the start of the state array for the chunk.
 * @param dense Pointer to the start of the dense array for the chunk.
 * @return The number of IDs deleted from the chunk (always 1024).
 */
static pal_uint16_t
PAL_HandleTableDeleteIdsInChunk_Full
(
    struct PAL_HANDLE_TABLE *table, 
    pal_uint32_t       chunk_index, 
    pal_uint32_t            *state, 
    pal_uint32_t            *dense
)
{
    pal_uint32_t chunk_word = chunk_index >> PAL_HANDLE_CHUNK_WORD_SHIFT;
    pal_uint32_t  chunk_bit = chunk_index  & PAL_HANDLE_CHUNK_WORD_MASK;
    pal_uint64_t chunk_mask = 1ULL << chunk_bit;
    pal_uint32_t          i;
    for (i = 0; i < PAL_HANDLE_CHUNK_CAPACITY; ++i)
    {
        state[i]  =(state[i] + 1) & PAL_HANDLE_GENER_MASK;
        dense[i]  = i;
    }
    table->ChunkStatus[chunk_word ] |= chunk_mask;
    table->ChunkCounts[chunk_index]  = 0;
    return PAL_HANDLE_CHUNK_CAPACITY;
}

/* @summary Remove the full chunk. All 1024 IDs are assigned, and all will be removed. No data elements are moved.
 * @param table The PAL_HANDLE_TABLE that allocated the IDs being deleted.
 * @param chunk_index The zero-based index of the chunk from which the ID was allocated.
 * @param state Pointer to the start of the state array for the chunk.
 * @return The number of IDs deleted from the chunk (always 1024).
 */
static pal_uint16_t
PAL_HandleTableRemoveIdsInChunk_Full
(
    struct PAL_HANDLE_TABLE *table, 
    pal_uint32_t       chunk_index, 
    pal_uint32_t            *state
)
{   /* mark all state entries as 0 (unoccupied) */
    PAL_ZeroMemory(state, PAL_HANDLE_CHUNK_CAPACITY * sizeof(pal_uint32_t));
    table->ChunkCounts[chunk_index] = 0;
    return PAL_HANDLE_CHUNK_CAPACITY;
}

/* @summary Delete one or more IDs from a chunk, where the resulting chunk has at least one remaining element. Remaining data elements are moved at most once.
 * The caller is responsible for ensuring that no invalid handles are included in the handles array.
 * @param table The PAL_HANDLE_TABLE that allocated the IDs being deleted.
 * @param handles The array of handles to delete. Handles are deleted in a contiguous range starting from range_beg, up to but not including range_end.
 * @param range_beg The zero-based index of the first ID to delete from the handles array (inclusive lower bound).
 * @param range_end The zero-based index within the handles array at which deletion will stop (exclusive upper bound).
 * @param chunk_index The zero-based index of the chunk from which the ID was allocated.
 * @param chunk_count The number of items currently in the chunk. This value must be at least 1.
 * @param state Pointer to the start of the state array for the chunk.
 * @param dense Pointer to the start of the dense array for the chunk.
 * @param data The PAL_MEMORY_VIEW used to access the data stored within the chunk.
 * @return The number of IDs deleted from the chunk.
 */
static pal_uint16_t
PAL_HandleTableDeleteIdsInChunk_Many
(
    struct PAL_HANDLE_TABLE *table, 
    PAL_HANDLE            *handles, 
    pal_usize_t          range_beg, 
    pal_usize_t          range_end, 
    pal_uint32_t       chunk_index, 
    pal_uint16_t       chunk_count, 
    pal_uint32_t            *state, 
    pal_uint32_t            *dense, 
    struct PAL_MEMORY_VIEW   *data
)
{
    pal_usize_t     range_cur = range_beg;
    pal_uint32_t   last_dense = chunk_count - 1;
    pal_uint32_t stream_count = PAL_MemoryViewStreamCount(data);
    pal_uint32_t  state_value;
    pal_uint32_t  state_index;
    pal_uint32_t  dense_index;
    pal_uint32_t  moved_index;
    pal_uint32_t  moved_value;
    pal_uint32_t  moved_gener;
    pal_uint16_t    dense_src;
    pal_uint16_t    dense_dst;
    pal_uint16_t delete_count;
    pal_uint16_t   move_count;
    pal_uint32_t            i;
    pal_usize_t          size;
    void               *src_p;
    void               *dst_p;

    pal_uint16_t       src_di[PAL_HANDLE_CHUNK_CAPACITY];
    pal_uint16_t       dst_di[PAL_HANDLE_CHUNK_CAPACITY];
    PAL_FillMemory(dst_di, PAL_HANDLE_CHUNK_CAPACITY * sizeof(pal_uint32_t), 0xFF);

    delete_count = 0;
    move_count   = 0;

    while (range_cur != range_end)
    {
        state_index = PAL_HandleValueGetStateIndex(handles[range_cur++]);
        state_value = state[state_index];
        dense_index = PAL_HandleStateGetDenseIndex(state_value);
        moved_value = dense[last_dense];
        moved_index = PAL_HandleValueGetStateIndex(moved_value);
        moved_gener = PAL_HandleValueGetGeneration(moved_value);

        /* clear the liveness and dense index assigned to the state */
        state [state_index] = (state_value+1) & PAL_HANDLE_GENER_MASK;

        /* mark the item as being deleted in the move table */
        dst_di[state_index] = 0xFFFF;

        /* if the deleted item is not the last slot in the dense array, 
         * then swap the last live item into the slot vacated by the 
         * just-deleted item in order to keep the dense array packed. 
         * for now, just swap the dense value and record the move.
         */
        if (dense_index != last_dense)
        {
            state[moved_index] = PAL_HANDLE_VALID_MASK_PACKED | (dense_index << PAL_HANDLE_INDEX_SHIFT) | (moved_gener << PAL_HANDLE_GENER_SHIFT);
            dense[dense_index] = dense[last_dense];
            if (dst_di[moved_index] != 0xFFFF)
            {   /* not the first move for state index 'moved' */
                dst_di[moved_index]  =(pal_uint16_t) dense_index;
            }
            else
            {   /* first move for state index 'moved' */
                src_di[moved_index]  =(pal_uint16_t) last_dense;
                dst_di[moved_index]  =(pal_uint16_t) dense_index;
                move_count++;
            }
        }

        /* return state_index to the free list */
        dense[last_dense--] = state_index;
        delete_count++;
    }
    if (move_count > 0)
    {   /* move all the data to its final location */
        while (range_beg != range_end)
        {
            state_index    = PAL_HandleValueGetStateIndex(handles[range_beg++]);
            dense_src      = src_di[state_index];
            dense_dst      = dst_di[state_index];
            if (dense_dst != 0xFFFF && dense_dst != dense_src)
            {   /* the item was moved - move data */
                for (i = 0; i < stream_count; ++i)
                {
                    size  = data->Stride[i];
                    dst_p = PAL_MemoryViewStreamAt(void*, data, i, dense_dst);
                    src_p = PAL_MemoryViewStreamAt(void*, data, i, dense_src);
                    PAL_CopyMemory(dst_p, src_p, size);
                }
            }
        }
    }
    /* update the chunk state */
    PAL_HandleTableMarkChunkAvailable(table, chunk_index);
    PAL_HandleTableSetChunkItemCount (table, chunk_index, chunk_count-delete_count);
    return delete_count;
}

/* @summary Remove one or more IDs from a chunk, where the resulting chunk has at least one remaining element. Remaining data elements are moved at most once.
 * The caller is responsible for ensuring that no invalid handles are included in the handles array, and that all included handles actually exist in the chunk.
 * @param table The PAL_HANDLE_TABLE that allocated the IDs being removed.
 * @param handles The array of handles to remove. Handles are removed in a contiguous range starting from range_beg, up to but not including range_end.
 * @param range_beg The zero-based index of the first ID to remove from the handles array (inclusive lower bound).
 * @param range_end The zero-based index within the handles array at which removal will stop (exclusive upper bound).
 * @param chunk_index The zero-based index of the chunk being updated.
 * @param chunk_count The number of items currently in the chunk. This value must be at least 1.
 * @param state Pointer to the start of the state array for the chunk.
 * @param dense Pointer to the start of the dense array for the chunk.
 * @param data The PAL_MEMORY_VIEW used to access the data stored within the chunk.
 * @return The number of IDs removed from the chunk.
 */
static pal_uint16_t
PAL_HandleTableRemoveIdsInChunk_Many
(
    struct PAL_HANDLE_TABLE *table, 
    PAL_HANDLE            *handles, 
    pal_usize_t          range_beg, 
    pal_usize_t          range_end, 
    pal_uint32_t       chunk_index, 
    pal_uint16_t       chunk_count, 
    pal_uint32_t            *state, 
    pal_uint32_t            *dense, 
    struct PAL_MEMORY_VIEW   *data
)
{
    pal_usize_t     range_cur = range_beg;
    pal_uint32_t   last_dense = chunk_count - 1;
    pal_uint32_t stream_count = PAL_MemoryViewStreamCount(data);
    pal_uint32_t  state_value;
    pal_uint32_t  state_index;
    pal_uint32_t  dense_index;
    pal_uint32_t  moved_index;
    pal_uint32_t  moved_value;
    pal_uint32_t  moved_gener;
    pal_uint16_t    dense_src;
    pal_uint16_t    dense_dst;
    pal_uint16_t remove_count;
    pal_uint16_t   move_count;
    pal_uint32_t            i;
    pal_usize_t          size;
    void               *src_p;
    void               *dst_p;

    pal_uint16_t       src_di[PAL_HANDLE_CHUNK_CAPACITY];
    pal_uint16_t       dst_di[PAL_HANDLE_CHUNK_CAPACITY];
    PAL_FillMemory(dst_di, PAL_HANDLE_CHUNK_CAPACITY * sizeof(pal_uint32_t), 0xFF);

    remove_count = 0;
    move_count   = 0;

    while (range_cur != range_end)
    {
        state_index = PAL_HandleValueGetStateIndex(handles[range_cur++]);
        state_value = state[state_index];
        dense_index = PAL_HandleStateGetDenseIndex(state_value);
        moved_value = dense[last_dense];
        moved_index = PAL_HandleValueGetStateIndex(moved_value);
        moved_gener = PAL_HandleValueGetGeneration(moved_value);

        /* mark the slot as being unoccupied */
        state [state_index] = 0;

        /* mark the item as being deleted in the move table */
        dst_di[state_index] = 0xFFFF;

        /* if the removed item is not the last slot in the dense array, 
         * then swap the last live item into the slot vacated by the 
         * just-removed item in order to keep the dense array packed. 
         * for now, just swap the dense value and record the move.
         */
        if (dense_index != last_dense)
        {
            state[moved_index] = PAL_HANDLE_VALID_MASK_PACKED | (dense_index << PAL_HANDLE_INDEX_SHIFT) | (moved_gener << PAL_HANDLE_GENER_SHIFT);
            dense[dense_index] = dense[last_dense];
            if (dst_di[moved_index] != 0xFFFF)
            {   /* not the first move for state index 'moved' */
                dst_di[moved_index]  =(pal_uint16_t) dense_index;
            }
            else
            {   /* first move for state index 'moved' */
                src_di[moved_index]  =(pal_uint16_t) last_dense;
                dst_di[moved_index]  =(pal_uint16_t) dense_index;
                move_count++;
            }
        }

        /* there's now one less item in the dense array */
        remove_count++;
        last_dense--;
    }
    if (move_count > 0)
    {   /* move all the data to its final location */
        while (range_beg != range_end)
        {
            state_index    = PAL_HandleValueGetStateIndex(handles[range_beg++]);
            dense_src      = src_di[state_index];
            dense_dst      = dst_di[state_index];
            if (dense_dst != 0xFFFF && dense_dst != dense_src)
            {   /* the item was moved - move data */
                for (i = 0; i < stream_count; ++i)
                {
                    size  = data->Stride[i];
                    dst_p = PAL_MemoryViewStreamAt(void*, data, i, dense_dst);
                    src_p = PAL_MemoryViewStreamAt(void*, data, i, dense_src);
                    PAL_CopyMemory(dst_p, src_p, size);
                }
            }
        }
    }
    /* update the chunk state */
    PAL_HandleTableSetChunkItemCount(table, chunk_index, chunk_count-remove_count);
    return remove_count;
}

PAL_API(pal_uint32_t)
PAL_BitsMix32
(
    pal_uint32_t input
)
{   /* the finalizer from 32-bit MurmurHash3 */
    input ^= input >> 16;
    input *= 0x85EBCA6BU;
    input ^= input >> 13;
    input *= 0xC2B2AE35U;
    input ^= input >> 16;
    return input;
}

PAL_API(pal_uint64_t)
PAL_BitsMix64
(
    pal_uint64_t input
)
{   /* the finalizer from x64 MurmurHash3 */
    input ^= input >> 33;
    input *= 0xFF51AFD7ED558CCDULL;
    input ^= input >> 33;
    input *= 0xC4CEB9FE1A85EC53ULL;
    input ^= input >> 33;
    return input;
}

PAL_API(pal_uint32_t)
PAL_HashData32
(
    void const   *data, 
    pal_usize_t length, 
    pal_uint32_t  seed
)
{   /* xxHash XXH32 */
    pal_uint8_t  const *p_itr = (pal_uint8_t const*) data;
    pal_uint8_t  const *p_end = (pal_uint8_t const*) data + length;
    pal_uint32_t const     c1 =  2654435761U;
    pal_uint32_t const     c2 =  2246822519U;
    pal_uint32_t const     c3 =  3266489917U;
    pal_uint32_t const     c4 =   668265263U;
    pal_uint32_t const     c5 =   374761393U;
    pal_uint32_t          h32;   /* output */

    if (data == NULL) {
        length = 0;
        p_itr  = p_end = (pal_uint8_t const*) (pal_usize_t) 16;
    }
    if (length > 16) {
        pal_uint8_t const * const p_limit = p_end - 16;
        pal_uint32_t                   v1 = seed  + c1 + c2;
        pal_uint32_t                   v2 = seed  + c2;
        pal_uint32_t                   v3 = seed  + 0;
        pal_uint32_t                   v4 = seed  - c1;
        do {
            v1 = PAL_XXH32_round(v1, PAL_ReadU32(p_itr)); p_itr += 4;
            v2 = PAL_XXH32_round(v2, PAL_ReadU32(p_itr)); p_itr += 4;
            v3 = PAL_XXH32_round(v3, PAL_ReadU32(p_itr)); p_itr += 4;
            v4 = PAL_XXH32_round(v4, PAL_ReadU32(p_itr)); p_itr += 4;
        } while (p_itr <= p_limit);
        h32 = PAL_rotl32(v1, 1) + PAL_rotl32(v2, 7) + PAL_rotl32(v3, 12) + PAL_rotl32(v4, 18);
    }
    else {
        h32 = seed + c5;
    }

    h32 += (pal_uint32_t) length;

    while (p_itr + 4 <= p_end) {
        h32   += PAL_ReadU32(p_itr)  * c3;
        h32    = PAL_rotl32(h32, 17) * c4;
        p_itr += 4;
    }
    while (p_itr < p_end) {
        h32   += (*p_itr) * c5;
        h32    = PAL_rotl32(h32, 11) * c1;
        p_itr++;
    }

    h32 ^= h32 >> 15;
    h32 *= c2;
    h32 ^= h32 >> 13;
    h32 *= c3;
    h32 ^= h32 >> 16;
    return h32;
}

PAL_API(pal_uint64_t)
PAL_HashData64
(
    void const   *data, 
    pal_usize_t length, 
    pal_uint64_t  seed 
)
{   /* xxHash XXH64 */
    pal_uint8_t  const *p_itr = (pal_uint8_t const*) data;
    pal_uint8_t  const *p_end = (pal_uint8_t const*) data + length;
    pal_uint64_t const     c1 =  11400714785074694791ULL;
    pal_uint64_t const     c2 =  14029467366897019727ULL;
    pal_uint64_t const     c3 =   1609587929392839161ULL;
    pal_uint64_t const     c4 =   9650029242287828579ULL;
    pal_uint64_t const     c5 =   2870177450012600261ULL;
    pal_uint64_t          h64;   /* output */

    if (data == NULL) {
        length = 0;
        p_itr  = p_end = (pal_uint8_t const*) (pal_usize_t) 32;
    }
    if (length > 32) {
        pal_uint8_t const * const p_limit = p_end - 32;
        pal_uint64_t                   v1 = seed  + c1 + c2;
        pal_uint64_t                   v2 = seed  + c2;
        pal_uint64_t                   v3 = seed  + 0;
        pal_uint64_t                   v4 = seed  - c1;
        do {
            v1 = PAL_XXH64_round(v1, PAL_ReadU64(p_itr)); p_itr += 8;
            v2 = PAL_XXH64_round(v2, PAL_ReadU64(p_itr)); p_itr += 8;
            v3 = PAL_XXH64_round(v3, PAL_ReadU64(p_itr)); p_itr += 8;
            v4 = PAL_XXH64_round(v4, PAL_ReadU64(p_itr)); p_itr += 8;
        } while (p_itr <= p_limit);

        h64 = PAL_rotl64(v1, 1) + PAL_rotl64(v2, 7) + PAL_rotl64(v3, 12) + PAL_rotl64(v4, 18);
        h64 = PAL_XXH64_merge(h64, v1);
        h64 = PAL_XXH64_merge(h64, v2);
        h64 = PAL_XXH64_merge(h64, v3);
        h64 = PAL_XXH64_merge(h64, v4);
    }
    else {
        h64 = seed + c5;
    }

    h64 += (pal_uint64_t) length;

    while (p_itr + 8 <= p_end) {
        pal_uint64_t const k1 = PAL_XXH64_round(0, PAL_ReadU64(p_itr));
        h64   ^= k1;
        h64    = PAL_rotl64(h64, 27) * c1 + c4;
        p_itr += 8;
    }
    if (p_itr + 4 <= p_end) {
        h64   ^= (pal_uint64_t)(PAL_ReadU32(p_itr)) * c1;
        h64    = PAL_rotl64(h64, 23) * c2 + c3;
        p_itr += 4;
    }
    while (p_itr < p_end) {
        h64 ^= (*p_itr) * c5;
        h64  = PAL_rotl64(h64, 11) * c1;
        p_itr++;
    }

    h64 ^= h64 >> 33;
    h64 *= c2;
    h64 ^= h64 >> 29;
    h64 *= c3;
    h64 ^= h64 >> 32;
    return h64;
}

PAL_API(int)
PAL_EndianessQuery
(
    void
)
{
    union PAL_EndianessTest_u
    { char         array[4];
      pal_sint32_t chars;
    } u;
    char c = 'a';
    u.array[0] = c++;
    u.array[1] = c++;
    u.array[2] = c++;
    u.array[3] = c++;
    return (u.chars == 0x61626364) ? PAL_ENDIANESS_MSB_FIRST : PAL_ENDIANESS_LSB_FIRST;
}

PAL_API(pal_sint8_t)
PAL_Read_si8
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
    return *(pal_sint8_t *)(((pal_uint8_t *) addr)+offset);
}

PAL_API(pal_uint8_t)
PAL_Read_ui8
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
    return *(pal_uint8_t *)(((pal_uint8_t *) addr)+offset);
}

PAL_API(pal_sint16_t)
PAL_Read_si16
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
    return *(pal_sint16_t*)(((pal_uint8_t *) addr)+offset);
}

PAL_API(pal_sint16_t)
PAL_Read_si16_msb
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_MSB_FIRST
    return *(pal_sint16_t*)(((pal_uint8_t *) addr)+offset);
#else
    return  (pal_sint16_t )PAL_ReadSwap_i16(addr, offset);
#endif
}

PAL_API(pal_sint16_t)
PAL_Read_si16_lsb
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_LSB_FIRST
    return *(pal_sint16_t*)(((pal_uint8_t *) addr)+offset);
#else
    return  (pal_sint16_t )PAL_ReadSwap_i16(addr, offset);
#endif
}

PAL_API(pal_uint16_t)
PAL_Read_ui16
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
    return *(pal_uint16_t*)(((pal_uint8_t *) addr)+offset);
}

PAL_API(pal_uint16_t)
PAL_Read_ui16_msb
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_MSB_FIRST
    return *(pal_uint16_t*)(((pal_uint8_t *) addr)+offset);
#else
    return  (pal_uint16_t )PAL_ReadSwap_i16(addr, offset);
#endif
}

PAL_API(pal_uint16_t)
PAL_Read_ui16_lsb
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_LSB_FIRST
    return *(pal_uint16_t*)(((pal_uint8_t *) addr)+offset);
#else
    return  (pal_uint16_t )PAL_ReadSwap_i16(addr, offset);
#endif
}

PAL_API(pal_sint32_t)
PAL_Read_si32
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
    return *(pal_sint32_t*)(((pal_uint8_t *) addr)+offset);
}

PAL_API(pal_sint32_t)
PAL_Read_si32_msb
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_MSB_FIRST
    return *(pal_sint32_t*)(((pal_uint8_t *) addr)+offset);
#else
    return  (pal_sint32_t )PAL_ReadSwap_i32(addr, offset);
#endif
}

PAL_API(pal_sint32_t)
PAL_Read_si32_lsb
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_LSB_FIRST
    return *(pal_sint32_t*)(((pal_uint8_t *) addr)+offset);
#else
    return  (pal_sint32_t )PAL_ReadSwap_i32(addr, offset);
#endif
}

PAL_API(pal_uint32_t)
PAL_Read_ui32
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
    return *(pal_uint32_t*)(((pal_uint8_t *) addr)+offset);
}

PAL_API(pal_uint32_t)
PAL_Read_ui32_msb
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_MSB_FIRST
    return *(pal_uint32_t*)(((pal_uint8_t *) addr)+offset);
#else
    return  (pal_uint32_t )PAL_ReadSwap_i32(addr, offset);
#endif
}

PAL_API(pal_uint32_t)
PAL_Read_ui32_lsb
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_LSB_FIRST
    return *(pal_uint32_t*)(((pal_uint8_t *) addr)+offset);
#else
    return  (pal_uint32_t )PAL_ReadSwap_i32(addr, offset);
#endif
}

PAL_API(pal_sint64_t)
PAL_Read_si64
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
    return *(pal_sint64_t*)(((pal_uint8_t *) addr)+offset);
}

PAL_API(pal_sint64_t)
PAL_Read_si64_msb
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_MSB_FIRST
    return *(pal_sint64_t*)(((pal_uint8_t *) addr)+offset);
#else
    return  (pal_sint64_t )PAL_ReadSwap_i64(addr, offset);
#endif
}

PAL_API(pal_sint64_t)
PAL_Read_si64_lsb
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_LSB_FIRST
    return *(pal_sint64_t*)(((pal_uint8_t *) addr)+offset);
#else
    return  (pal_sint64_t )PAL_ReadSwap_i64(addr, offset);
#endif
}

PAL_API(pal_uint64_t)
PAL_Read_ui64
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
    return *(pal_uint64_t*)(((pal_uint8_t *) addr)+offset);
}

PAL_API(pal_uint64_t)
PAL_Read_ui64_msb
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_MSB_FIRST
    return *(pal_uint64_t*)(((pal_uint8_t *) addr)+offset);
#else
    return  (pal_uint64_t )PAL_ReadSwap_i64(addr, offset);
#endif
}

PAL_API(pal_uint64_t)
PAL_Read_ui64_lsb
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_LSB_FIRST
    return *(pal_uint64_t*)(((pal_uint8_t *) addr)+offset);
#else
    return  (pal_uint64_t )PAL_ReadSwap_i64(addr, offset);
#endif
}

PAL_API(pal_float32_t)
PAL_Read_f32
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
    return *(pal_float32_t*)(((pal_uint8_t *) addr)+offset);
}

PAL_API(pal_float32_t)
PAL_Read_f32_msb
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_MSB_FIRST
    return *(pal_float32_t*)(((pal_uint8_t *) addr)+offset);
#else
    return  (pal_float32_t )PAL_ReadSwap_f32(addr, offset);
#endif
}

PAL_API(pal_float32_t)
PAL_Read_f32_lsb
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_LSB_FIRST
    return *(pal_float32_t*)(((pal_uint8_t *) addr)+offset);
#else
    return  (pal_float32_t )PAL_ReadSwap_f32(addr, offset);
#endif
}

PAL_API(pal_float64_t)
PAL_Read_f64
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
    return *(pal_float64_t*)(((pal_uint8_t *) addr)+offset);
}

PAL_API(pal_float64_t)
PAL_Read_f64_msb
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_MSB_FIRST
    return *(pal_float64_t*)(((pal_uint8_t *) addr)+offset);
#else
    return  (pal_float64_t )PAL_ReadSwap_f64(addr, offset);
#endif
}

PAL_API(pal_float64_t)
PAL_Read_f64_lsb
(
    void           *addr, 
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_LSB_FIRST
    return *(pal_float64_t*)(((pal_uint8_t *) addr)+offset);
#else
    return  (pal_float64_t )PAL_ReadSwap_f64(addr, offset);
#endif
}

PAL_API(pal_usize_t)
PAL_Write_si8
(
    void           *addr, 
    pal_sint8_t    value,
    pal_ptrdiff_t offset
)
{
    *(pal_sint8_t *)(((pal_uint8_t*) addr)+offset) = value;
    return sizeof(pal_sint8_t);
}

PAL_API(pal_usize_t)
PAL_Write_ui8
(
    void           *addr, 
    pal_uint8_t    value,
    pal_ptrdiff_t offset
)
{
    *(pal_uint8_t *)(((pal_uint8_t*) addr)+offset) = value;
    return sizeof(pal_uint8_t);
}

PAL_API(pal_usize_t)
PAL_Write_si16
(
    void           *addr, 
    pal_sint16_t   value,
    pal_ptrdiff_t offset
)
{
    *(pal_sint16_t*)(((pal_uint8_t*) addr)+offset) = value;
    return sizeof(pal_sint16_t);
}

PAL_API(pal_usize_t)
PAL_Write_si16_msb
(
    void           *addr, 
    pal_sint16_t   value,
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_MSB_FIRST
    *(pal_sint16_t*)(((pal_uint8_t*) addr)+offset) = value;
#else
    PAL_SwapWrite_si16(addr, value, offset);
#endif
    return sizeof(pal_sint16_t);
}

PAL_API(pal_usize_t)
PAL_Write_si16_lsb
(
    void           *addr, 
    pal_sint16_t   value,
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_LSB_FIRST
    *(pal_sint16_t*)(((pal_uint8_t*) addr)+offset) = value;
#else
    PAL_SwapWrite_si16(addr, value, offset);
#endif
    return sizeof(pal_sint16_t);
}

PAL_API(pal_usize_t)
PAL_Write_ui16
(
    void           *addr, 
    pal_uint16_t   value,
    pal_ptrdiff_t offset
)
{
    *(pal_uint16_t*)(((pal_uint8_t*) addr)+offset) = value;
    return sizeof(pal_uint16_t);
}

PAL_API(pal_usize_t)
PAL_Write_ui16_msb
(
    void           *addr, 
    pal_uint16_t   value,
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_MSB_FIRST
    *(pal_uint16_t*)(((pal_uint8_t*) addr)+offset) = value;
#else
    PAL_SwapWrite_ui16(addr, value, offset);
#endif
    return sizeof(pal_uint16_t);
}

PAL_API(pal_usize_t)
PAL_Write_ui16_lsb
(
    void           *addr, 
    pal_uint16_t   value,
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_LSB_FIRST
    *(pal_uint16_t*)(((pal_uint8_t*) addr)+offset) = value;
#else
    PAL_SwapWrite_ui16(addr, value, offset);
#endif
    return sizeof(pal_uint16_t);
}

PAL_API(pal_usize_t)
PAL_Write_si32
(
    void           *addr, 
    pal_sint32_t   value,
    pal_ptrdiff_t offset
)
{
    *(pal_sint32_t*)(((pal_uint8_t*) addr)+offset) = value;
    return sizeof(pal_sint32_t);
}

PAL_API(pal_usize_t)
PAL_Write_si32_msb
(
    void           *addr, 
    pal_sint32_t   value,
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_MSB_FIRST
    *(pal_sint32_t*)(((pal_uint8_t*) addr)+offset) = value;
#else
    PAL_SwapWrite_si32(addr, value, offset);
#endif
    return sizeof(pal_sint32_t);
}

PAL_API(pal_usize_t)
PAL_Write_si32_lsb
(
    void           *addr, 
    pal_sint32_t   value,
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_LSB_FIRST
    *(pal_sint32_t*)(((pal_uint8_t*) addr)+offset) = value;
#else
    PAL_SwapWrite_si32(addr, value, offset);
#endif
    return sizeof(pal_sint32_t);
}

PAL_API(pal_usize_t)
PAL_Write_ui32
(
    void           *addr, 
    pal_uint32_t   value,
    pal_ptrdiff_t offset
)
{
    *(pal_uint32_t*)(((pal_uint8_t*) addr)+offset) = value;
    return sizeof(pal_uint32_t);
}

PAL_API(pal_usize_t)
PAL_Write_ui32_msb
(
    void           *addr, 
    pal_uint32_t   value,
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_MSB_FIRST
    *(pal_uint32_t*)(((pal_uint8_t*) addr)+offset) = value;
#else
    PAL_SwapWrite_ui32(addr, value, offset);
#endif
    return sizeof(pal_uint32_t);
}

PAL_API(pal_usize_t)
PAL_Write_ui32_lsb
(
    void           *addr, 
    pal_uint32_t   value,
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_LSB_FIRST
    *(pal_uint32_t*)(((pal_uint8_t*) addr)+offset) = value;
#else
    PAL_SwapWrite_ui32(addr, value, offset);
#endif
    return sizeof(pal_uint32_t);
}

PAL_API(pal_usize_t)
PAL_Write_si64
(
    void           *addr, 
    pal_sint64_t   value,
    pal_ptrdiff_t offset
)
{
    *(pal_sint64_t*)(((pal_uint8_t*) addr)+offset) = value;
    return sizeof(pal_sint64_t);
}

PAL_API(pal_usize_t)
PAL_Write_si64_msb
(
    void           *addr, 
    pal_sint64_t   value,
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_MSB_FIRST
    *(pal_sint64_t*)(((pal_uint8_t*) addr)+offset) = value;
#else
    PAL_SwapWrite_si64(addr, value, offset);
#endif
    return sizeof(pal_sint64_t);
}

PAL_API(pal_usize_t)
PAL_Write_si64_lsb
(
    void           *addr, 
    pal_sint64_t   value,
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_LSB_FIRST
    *(pal_sint64_t*)(((pal_uint8_t*) addr)+offset) = value;
#else
    PAL_SwapWrite_si64(addr, value, offset);
#endif
    return sizeof(pal_sint64_t);
}

PAL_API(pal_usize_t)
PAL_Write_ui64
(
    void           *addr, 
    pal_uint64_t   value,
    pal_ptrdiff_t offset
)
{
    *(pal_uint64_t*)(((pal_uint8_t*) addr)+offset) = value;
    return sizeof(pal_uint64_t);
}

PAL_API(pal_usize_t)
PAL_Write_ui64_msb
(
    void           *addr, 
    pal_uint64_t   value,
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_MSB_FIRST
    *(pal_uint64_t*)(((pal_uint8_t*) addr)+offset) = value;
#else
    PAL_SwapWrite_ui64(addr, value, offset);
#endif
    return sizeof(pal_uint64_t);
}

PAL_API(pal_usize_t)
PAL_Write_ui64_lsb
(
    void           *addr, 
    pal_uint64_t   value,
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_LSB_FIRST
    *(pal_uint64_t*)(((pal_uint8_t*) addr)+offset) = value;
#else
    PAL_SwapWrite_ui64(addr, value, offset);
#endif
    return sizeof(pal_uint64_t);
}

PAL_API(pal_usize_t)
PAL_Write_f32
(
    void           *addr, 
    pal_float32_t  value,
    pal_ptrdiff_t offset
)
{
    *(pal_float32_t*)(((pal_uint8_t*) addr)+offset) = value;
    return sizeof(pal_float32_t);
}

PAL_API(pal_usize_t)
PAL_Write_f32_msb
(
    void           *addr, 
    pal_float32_t  value,
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_MSB_FIRST
    *(pal_float32_t*)(((pal_uint8_t*) addr)+offset) = value;
#else
    PAL_SwapWrite_f32(addr, value, offset);
#endif
    return sizeof(pal_float32_t);
}

PAL_API(pal_usize_t)
PAL_Write_f32_lsb
(
    void           *addr, 
    pal_float32_t  value,
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_LSB_FIRST
    *(pal_float32_t*)(((pal_uint8_t*) addr)+offset) = value;
#else
    PAL_SwapWrite_f32(addr, value, offset);
#endif
    return sizeof(pal_float32_t);
}

PAL_API(pal_usize_t)
PAL_Write_f64
(
    void           *addr, 
    pal_float64_t  value,
    pal_ptrdiff_t offset
)
{
    *(pal_float64_t*)(((pal_uint8_t*) addr)+offset) = value;
    return sizeof(pal_float64_t);
}

PAL_API(pal_usize_t)
PAL_Write_f64_msb
(
    void           *addr, 
    pal_float64_t  value,
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_MSB_FIRST
    *(pal_float64_t*)(((pal_uint8_t*) addr)+offset) = value;
#else
    PAL_SwapWrite_f64(addr, value, offset);
#endif
    return sizeof(pal_float64_t);
}

PAL_API(pal_usize_t)
PAL_Write_f64_lsb
(
    void           *addr, 
    pal_float64_t  value,
    pal_ptrdiff_t offset
)
{
#if PAL_SYSTEM_ENDIANESS == PAL_ENDIANESS_LSB_FIRST
    *(pal_float64_t*)(((pal_uint8_t*) addr)+offset) = value;
#else
    PAL_SwapWrite_f64(addr, value, offset);
#endif
    return sizeof(pal_float64_t);
}


PAL_API(void*)
PAL_HeapMemoryAllocateHost
(
    pal_usize_t len
)
{
    return malloc(len);
}

PAL_API(void*)
PAL_HeapMemoryReallocateHost
(
    void      *addr, 
    pal_usize_t len
)
{
    return realloc(addr, len);
}

PAL_API(void)
PAL_HeapMemoryFreeHost
(
    void *addr
)
{
    free(addr);
}

PAL_API(void)
PAL_ZeroMemory
(
    void       *dst, 
    pal_usize_t len
)
{
    ZeroMemory(dst, len);
}

PAL_API(void)
PAL_ZeroMemorySecure
(
    void       *dst, 
    pal_usize_t len
)
{
    (void) SecureZeroMemory(dst, len);
}

PAL_API(void)
PAL_CopyMemory
(
    void       * PAL_RESTRICT dst, 
    void const * PAL_RESTRICT src, 
    pal_usize_t               len
)
{
    CopyMemory(dst, src, len);
}

PAL_API(void)
PAL_MoveMemory
(
    void       *dst,
    void const *src, 
    pal_usize_t len
)
{
    MoveMemory(dst, src, len);
}

PAL_API(void)
PAL_FillMemory
(
    void       *dst, 
    pal_usize_t len, 
    pal_uint8_t val
)
{
    FillMemory(dst, len, val);
}

PAL_API(int)
PAL_CompareMemory
(
    void const *ptr1, 
    void const *ptr2, 
    pal_usize_t  len
)
{
    return memcmp(ptr1, ptr2, len);
}

PAL_API(int)
PAL_HostMemoryPoolCreate
(
    struct PAL_HOST_MEMORY_POOL      *pool, 
    struct PAL_HOST_MEMORY_POOL_INIT *init
)
{
    SYSTEM_INFO         sysinfo;
    pal_usize_t      total_size = 0;
    pal_usize_t actual_capacity = 0;
    pal_usize_t               i = 0;
    void                 *array = NULL;

    /* retrieve the OS page size and allocation granularity */
    GetNativeSystemInfo(&sysinfo);

    /* it doesn't make much sense to limit the pool size and waste memory so figure out how
       many nodes we can fit when the size is rounded up to the allocation granularity. */
    total_size      = PAL_AlignUp(init->PoolCapacity * sizeof(PAL_HOST_MEMORY_ALLOCATION), sysinfo.dwPageSize);
    actual_capacity = total_size / sizeof(PAL_HOST_MEMORY_ALLOCATION);

    /* allocate the memory for the node pool as one large contiguous block */
    if ((array = VirtualAlloc(NULL, total_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) == NULL)
    {
        ZeroMemory(pool, sizeof(PAL_HOST_MEMORY_POOL));
        return -1;
    }

    /* apply limits to the configuration arguments */
    if (init->MinAllocationSize == 0)
        init->MinAllocationSize  =(pal_uint32_t) sysinfo.dwPageSize;
    if (init->MinCommitIncrease == 0)
        init->MinCommitIncrease  =(pal_uint32_t) sysinfo.dwPageSize;

    /* initialize the fields of the pool structure */
    pool->PoolName               = init->PoolName;
    pool->FreeList               = NULL;
    pool->Capacity               =(pal_uint32_t) actual_capacity;
    pool->OsPageSize             =(pal_uint32_t) sysinfo.dwPageSize;
    pool->MinAllocationSize      = init->MinAllocationSize;
    pool->MinCommitIncrease      = init->MinCommitIncrease;
    pool->MaxTotalCommitment     = init->MaxTotalCommitment;
    pool->PoolTotalCommitment    = 0;
    pool->OsGranularity          =(pal_uint32_t) sysinfo.dwAllocationGranularity;
    pool->NodeList               =(PAL_HOST_MEMORY_ALLOCATION *) array;

    /* all pool nodes start out as free */
    for (i = 0; i < actual_capacity; ++i)
    {
        PAL_HOST_MEMORY_ALLOCATION *node = &pool->NodeList[i];
        node->SourcePool     = pool;
        node->NextAllocation = pool->FreeList;
        node->BaseAddress    = NULL;
        node->BytesReserved  = 0;
        node->BytesCommitted = 0;
        node->AllocationFlags= 0;
        node->NextFreeOffset = 0;
        pool->FreeList = node;
    }
    return 0;
}

PAL_API(void)
PAL_HostMemoryPoolDelete
(
    struct PAL_HOST_MEMORY_POOL *pool
)
{
    pal_usize_t i, n;

    /* free all of the individual allocations */
    for (i = 0, n = pool->Capacity; i < n; ++i)
    {
        PAL_HostMemoryRelease(&pool->NodeList[i]);
    }
    /* free the memory allocated for the pool */
    if (pool->NodeList != NULL)
    {
        VirtualFree(pool->NodeList, 0, MEM_RELEASE);
    }
    /* zero specific fields of the pool, but leave others (for post-mortem debugging) */
    pool->FreeList = NULL;
    pool->Capacity = 0;
    pool->NodeList = NULL;
}

PAL_API(void)
PAL_HostMemoryPoolMove
(
    struct PAL_HOST_MEMORY_POOL *dst, 
    struct PAL_HOST_MEMORY_POOL *src
)
{   
    pal_usize_t i, n;

    /* copy src to dst */
    PAL_CopyMemory(dst, src, sizeof(PAL_HOST_MEMORY_POOL));
    /* fix up the SourcePool pointers on each node to point to dst instead of src */
    for (i = 0, n = dst->Capacity; i < n; ++i)
    {
        dst->NodeList[i].SourcePool = dst;
    }
}

PAL_API(struct PAL_HOST_MEMORY_ALLOCATION*)
PAL_HostMemoryPoolAllocate
(
    struct PAL_HOST_MEMORY_POOL *pool, 
    pal_usize_t          reserve_size, 
    pal_usize_t           commit_size, 
    pal_uint32_t          alloc_flags
)
{
    if (pool->FreeList != NULL)
    {   /* attempt to initialize the object at the head of the free list */
        PAL_HOST_MEMORY_ALLOCATION *alloc = pool->FreeList;
        if (PAL_HostMemoryReserveAndCommit(alloc, reserve_size, commit_size, alloc_flags) < 0)
        {
            return NULL;
        }
        /* pop the object from the front of the free list */
        pool->FreeList = alloc->NextAllocation;
        alloc->NextAllocation = NULL;
        alloc->NextFreeOffset = 0;
        return alloc;
    }
    else
    {   /* no allocation nodes are available in the pool */
        SetLastError(ERROR_OUT_OF_STRUCTURES);
        return NULL;
    }
}

PAL_API(void)
PAL_HostMemoryPoolRelease
(
    struct PAL_HOST_MEMORY_POOL        *pool, 
    struct PAL_HOST_MEMORY_ALLOCATION *alloc
)
{
    if (alloc == NULL)
        return;
    if (alloc->BaseAddress != NULL)
    {   assert(alloc->SourcePool == pool);
        PAL_HostMemoryRelease(alloc);
        alloc->NextAllocation = pool->FreeList;
        pool->FreeList = alloc;
    }
}

PAL_API(void)
PAL_HostMemoryPoolReset
(
    struct PAL_HOST_MEMORY_POOL *pool
)
{   
    pal_usize_t i, n;

    /* clear the free list */
    pool->FreeList = NULL;
    /* return all nodes to the free list */
    for (i = 0, n = pool->Capacity; i < n; ++i)
    {
        PAL_HOST_MEMORY_ALLOCATION *node = &pool->NodeList[i];
        PAL_HostMemoryRelease(node);
        node->SourcePool     = pool;
        node->NextAllocation = pool->FreeList;
        node->BaseAddress    = NULL;
        node->BytesReserved  = 0;
        node->BytesCommitted = 0;
        node->AllocationFlags= 0;
        node->NextFreeOffset = 0;
        pool->FreeList = node;
    }
}

PAL_API(int)
PAL_HostMemoryReserveAndCommit
(
    struct PAL_HOST_MEMORY_ALLOCATION *alloc, 
    pal_usize_t                 reserve_size, 
    pal_usize_t                  commit_size, 
    pal_uint32_t                 alloc_flags
)
{
    SYSTEM_INFO       sysinfo;
    void                *base = NULL;
    pal_usize_t     page_size = 0;
    pal_usize_t         extra = 0;
    pal_usize_t   min_reserve = 0;
    DWORD              access = 0;
    DWORD               flags = MEM_RESERVE;

    if (alloc->SourcePool != NULL)
    {   /* use the values cached on the source pool */
        min_reserve = alloc->SourcePool->MinAllocationSize;
        page_size   = alloc->SourcePool->OsPageSize;
    }
    else
    {   /* query the OS for the page size and allocation granularity */
        GetNativeSystemInfo(&sysinfo);
        min_reserve = sysinfo.dwPageSize;
        page_size   = sysinfo.dwPageSize;
    }
    if (reserve_size < min_reserve)
    {   /* limit to the minimum allocation size */
        reserve_size = min_reserve;
    }
    if (commit_size > reserve_size)
    {   assert(commit_size <= reserve_size);
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }

    /* VMM allocations are rounded up to the next even multiple of the system 
     * page size, and have a starting address that is an even multiple of the
     * system allocation granularity (typically 64KB). */
    reserve_size = PAL_AlignUp(reserve_size, page_size);

    /* map PAL_HOST_MEMORY_ALLOCATION_FLAGS to Win32 access and protection flags */
    if (alloc_flags ==PAL_HOST_MEMORY_ALLOCATION_FLAGS_DEFAULT)
        alloc_flags = PAL_HOST_MEMORY_ALLOCATION_FLAGS_READWRITE;
    if (alloc_flags & PAL_HOST_MEMORY_ALLOCATION_FLAG_READ)
        access = PAGE_READONLY;
    if (alloc_flags & PAL_HOST_MEMORY_ALLOCATION_FLAG_WRITE)
        access = PAGE_READWRITE;
    if (alloc_flags & PAL_HOST_MEMORY_ALLOCATION_FLAG_EXECUTE)
    {   /* this also commits the entire reservation */
        access = PAGE_EXECUTE_READWRITE;
        commit_size = reserve_size;
    }

    /* determine whether additional space will be added for a trailing guard page */
    extra = (alloc_flags & PAL_HOST_MEMORY_ALLOCATION_FLAG_NOGUARD) ? 0 : page_size;

    /* is address space being committed in addition to being reserved? */
    if (commit_size > 0)
    {
        commit_size = PAL_AlignUp(commit_size, page_size);
        flags |= MEM_COMMIT;
    }
    if (alloc->SourcePool != NULL && alloc->SourcePool->MaxTotalCommitment != 0 && commit_size > 0)
    {   /* ensure that this request won't cause the pool limit to be exceeded */
        pal_uint64_t new_commit = alloc->SourcePool->PoolTotalCommitment + commit_size;
        if (new_commit > alloc->SourcePool->MaxTotalCommitment)
        {   SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return -1;
        }
    }

    /* reserve and possibly commit contiguous virtual address space */
    if ((base = VirtualAlloc(NULL, reserve_size + extra, flags, access)) == NULL)
    {
        return -1;
    }
    if (extra > 0)
    {   /* change the protection flags for the guard page only */
        if (VirtualAlloc((pal_uint8_t*) base + reserve_size, page_size, MEM_COMMIT, access | PAGE_GUARD) == NULL)
        {   DWORD error = GetLastError();
            VirtualFree(base, 0, MEM_RELEASE);
            SetLastError(error);
            return -1;
        }
    }

    /* if address space was committed, adjust the total commit on the source pool */
    if (alloc->SourcePool != NULL && commit_size > 0)
    {
        alloc->SourcePool->PoolTotalCommitment += commit_size;
    }

    /* the allocation process completed successfully */
    alloc->BaseAddress     =(pal_uint8_t*) base;
    alloc->BytesReserved   = reserve_size;
    alloc->BytesCommitted  = commit_size;
    alloc->AllocationFlags = alloc_flags;
    alloc->NextFreeOffset  = 0;
    return 0;
}

PAL_API(int)
PAL_HostMemoryIncreaseCommitment
(
    struct PAL_HOST_MEMORY_ALLOCATION *alloc, 
    pal_usize_t                  commit_size
)
{
    if (alloc->BytesReserved == 0)
    {   /* need to use PAL_HostMemoryReserveAndCommit instead */
        SetLastError(ERROR_INVALID_FUNCTION);
        return -1;
    }
    if (alloc->BytesCommitted < commit_size)
    {
        SYSTEM_INFO              sysinfo;
        pal_uint64_t max_commit_increase = alloc->BytesReserved - alloc->BytesCommitted;
        pal_uint64_t req_commit_increase = commit_size - alloc->BytesCommitted;
        pal_uint64_t old_bytes_committed = alloc->BytesCommitted;
        pal_uint64_t min_commit_increase = 0;
        pal_uint64_t new_bytes_committed = 0;
        pal_usize_t            page_size = 0;
        DWORD                     access = 0;

        if (alloc->SourcePool != NULL)
        {   /* use the minimum commit increase specified on the source pool */
            min_commit_increase = alloc->SourcePool->MinCommitIncrease;
            page_size = alloc->SourcePool->OsPageSize;
        }
        else
        {   /* query the OS for the system page size */
            GetNativeSystemInfo(&sysinfo);
            page_size = sysinfo.dwPageSize;
        }
        if (req_commit_increase < min_commit_increase)
        {   /* increase the commitment to the minimum value */
            req_commit_increase = min_commit_increase;
        }
        if (req_commit_increase > max_commit_increase)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return -1;
        }

        /* figure out how much we're asking for, taking into account that VMM allocations round up to a page size multiple */
        new_bytes_committed = PAL_AlignUp(old_bytes_committed + req_commit_increase, page_size);
        req_commit_increase = new_bytes_committed - old_bytes_committed;
        if (alloc->SourcePool != NULL && alloc->SourcePool->MaxTotalCommitment != 0)
        {   /* ensure that the pool limit isn't exceeded */
            if (alloc->SourcePool->PoolTotalCommitment + req_commit_increase > alloc->SourcePool->MaxTotalCommitment)
            {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return -1;
            }
        }

        /* convert the allocation flags into Win32 protection flags */
        if (alloc->AllocationFlags & PAL_HOST_MEMORY_ALLOCATION_FLAG_READ)
            access = PAGE_READONLY;
        if (alloc->AllocationFlags & PAL_HOST_MEMORY_ALLOCATION_FLAG_WRITE)
            access = PAGE_READWRITE;
        if (alloc->AllocationFlags & PAL_HOST_MEMORY_ALLOCATION_FLAG_EXECUTE)
            access = PAGE_EXECUTE_READWRITE;

        /* request that an additional portion of the pre-reserved address space be committed.
         * executable allocations are entirely committed up-front, so don't worry about that. */
        if (VirtualAlloc((pal_uint8_t*) alloc->BaseAddress, (SIZE_T) new_bytes_committed, MEM_COMMIT, access) == NULL)
        {
            return -1;
        }
        if (alloc->SourcePool != NULL)
        {
            alloc->SourcePool->PoolTotalCommitment += req_commit_increase;
        }
        alloc->BytesCommitted = new_bytes_committed;
        return 0;
    }
    else
    {   /* the requested commit amount has already been met */
        return 0;
    }
}

PAL_API(void)
PAL_HostMemoryFlush
(
    struct PAL_HOST_MEMORY_ALLOCATION *alloc
)
{
    if (alloc->AllocationFlags & PAL_HOST_MEMORY_ALLOCATION_FLAG_EXECUTE)
    {
        (void) FlushInstructionCache(GetCurrentProcess(), alloc->BaseAddress, (SIZE_T) alloc->BytesCommitted);
    }
}

PAL_API(void)
PAL_HostMemoryRelease
(
    struct PAL_HOST_MEMORY_ALLOCATION *alloc
)
{
    if (alloc->BaseAddress != NULL)
    {   /* free the entire range of address space */
        VirtualFree(alloc->BaseAddress, 0, MEM_RELEASE);
        /* update the total commitment on the source pool */
        if (alloc->SourcePool != NULL)
        {   assert(alloc->BytesCommitted <= alloc->SourcePool->PoolTotalCommitment);
            alloc->SourcePool->PoolTotalCommitment -= alloc->BytesCommitted;
        }
    }
    alloc->BaseAddress    = NULL;
    alloc->BytesReserved  = 0;
    alloc->BytesCommitted = 0;
    alloc->NextFreeOffset = 0;
}

PAL_API(int)
PAL_MemoryBlockIsValid
(
    struct PAL_MEMORY_BLOCK *block
)
{
    if (block->AllocatorType == PAL_MEMORY_ALLOCATOR_TYPE_HOST)
        return (block->HostAddress != NULL) ? 1 : 0;
    if (block->AllocatorType == PAL_MEMORY_ALLOCATOR_TYPE_DEVICE)
        return  1;
    else
        return  0;
}

PAL_API(int)
PAL_MemoryBlockDidMove
(
    struct PAL_MEMORY_BLOCK *old_block, 
    struct PAL_MEMORY_BLOCK *new_block
)
{
    return (new_block->BlockOffset == old_block->BlockOffset) ? 0 : 1;
}

PAL_API(int)
PAL_MemoryArenaCreate
(
    struct PAL_MEMORY_ARENA     *arena, 
    struct PAL_MEMORY_ARENA_INIT *init
)
{
    if (init->AllocatorType != PAL_MEMORY_ALLOCATOR_TYPE_HOST && 
        init->AllocatorType != PAL_MEMORY_ALLOCATOR_TYPE_DEVICE)
    {   assert(init->AllocatorType != PAL_MEMORY_ALLOCATOR_TYPE_INVALID);
        PAL_ZeroMemory(arena, sizeof(PAL_MEMORY_ARENA));
        return -1;
    }
    if (init->MemorySize == 0)
    {   assert(init->MemorySize != 0);
        PAL_ZeroMemory(arena, sizeof(PAL_MEMORY_ARENA));
        return -1;
    }
    if (init->UserDataSize > PAL_MEMORY_ALLOCATOR_MAX_USER)
    {   assert(init->UserDataSize <= PAL_MEMORY_ALLOCATOR_MAX_USER);
        PAL_ZeroMemory(arena, sizeof(PAL_MEMORY_ARENA));
        return -1;
    }

    PAL_ZeroMemory(arena, sizeof(PAL_MEMORY_ARENA));
    arena->AllocatorName = init->AllocatorName;
    arena->AllocatorType = init->AllocatorType;
    arena->AllocatorTag  = 0;
    arena->MemoryStart   = init->MemoryStart;
    arena->MemorySize    = init->MemorySize;
    arena->NextOffset    = 0;
    arena->MaximumOffset = init->MemorySize;
    if (init->UserDataSize > 0)
    {
        PAL_CopyMemory(arena->UserData, init->UserData, (pal_usize_t) init->UserDataSize);
    }
    return 0;
}

PAL_API(int)
PAL_MemoryArenaAllocate
(
    struct PAL_MEMORY_ARENA *arena, 
    pal_usize_t               size, 
    pal_usize_t          alignment, 
    struct PAL_MEMORY_BLOCK *block
)
{
    pal_uint64_t    base_address = arena->MemoryStart + arena->NextOffset;
    pal_uint64_t aligned_address = base_address != 0 ? PAL_AlignUp(base_address, alignment) : 0;
    pal_uint64_t     align_bytes = aligned_address - base_address;
    pal_uint64_t     alloc_bytes = size + align_bytes;
    pal_uint64_t      new_offset = arena->NextOffset + alloc_bytes;
    if (new_offset <= arena->MaximumOffset)
    {   /* the arena can satisfy the allocation */
        arena->NextOffset    = new_offset;
        block->SizeInBytes   = size;
        block->BlockOffset   = arena->NextOffset + align_bytes;
        block->HostAddress   = arena->AllocatorType == PAL_MEMORY_ALLOCATOR_TYPE_HOST ? (void*) (pal_uintptr_t) aligned_address : NULL;
        block->AllocatorType = arena->AllocatorType;
        block->AllocationTag = arena->AllocatorTag;
        return 0;
    }
    else
    {   /* the arena cannot satisfy the allocation */
        block->SizeInBytes   = 0;
        block->BlockOffset   = 0;
        block->HostAddress   = NULL;
        block->AllocatorType = PAL_MEMORY_ALLOCATOR_TYPE_INVALID;
        block->AllocationTag = 0;
        return -1;
    }
}

PAL_API(void*)
PAL_MemoryArenaAllocateHost
(
    struct PAL_MEMORY_ARENA *arena, 
    pal_usize_t               size, 
    pal_usize_t          alignment, 
    struct PAL_MEMORY_BLOCK *block
)
{
    PAL_MEMORY_BLOCK   dummy;
    if (block == NULL) block = &dummy;
    if (PAL_MemoryArenaAllocate(arena, size, alignment, block) == 0)
    {   /* the allocation request was satisfied */
        return block->HostAddress;
    }
    else return NULL;
}

PAL_API(void*)
PAL_MemoryArenaAllocateHostNoBlock
(
    struct PAL_MEMORY_ARENA *arena, 
    pal_usize_t               size, 
    pal_usize_t          alignment
)
{
    pal_uint64_t    base_address = arena->MemoryStart + arena->NextOffset;
    pal_uint64_t aligned_address = base_address != 0 ? PAL_AlignUp(base_address, alignment) : 0;
    pal_uint64_t     align_bytes = aligned_address - base_address;
    pal_uint64_t     alloc_bytes = size + align_bytes;
    pal_uint64_t      new_offset = arena->NextOffset + alloc_bytes;
    if (new_offset <= arena->MaximumOffset)
    {   /* the arena can satisfy the allocation */
        arena->NextOffset = new_offset;
        return (void*) aligned_address;
    }
    else
    {   /* the arena cannot satisfy the allocation */
        return NULL;
    }
}

PAL_API(struct PAL_MEMORY_ARENA_MARKER)
PAL_MemoryArenaMark
(
    struct PAL_MEMORY_ARENA *arena
)
{
    PAL_MEMORY_ARENA_MARKER m;
    m.Arena  = arena;
    m.Offset = arena->NextOffset;
    return m;
}

PAL_API(void)
PAL_MemoryArenaResetToMarker
(
    struct PAL_MEMORY_ARENA        *arena, 
    struct PAL_MEMORY_ARENA_MARKER marker
)
{   assert(marker.Arena  == arena);
    assert(marker.Offset <= arena->NextOffset);
    arena->NextOffset = marker.Offset;
}

PAL_API(void)
PAL_MemoryArenaReset
(
    struct PAL_MEMORY_ARENA *arena
)
{
    arena->NextOffset = 0;
}

PAL_API(pal_usize_t)
PAL_MemoryAllocatorQueryHostMemorySize
(
    pal_usize_t allocation_size_min, 
    pal_usize_t allocation_size_max
)
{
    PAL_MEMORY_INDEX_SIZE info;

    /* arguments must be powers of two greater than zero */
    if (!PAL_IsPowerOfTwo(allocation_size_min) || allocation_size_min == 0)
        return 0;
    if (!PAL_IsPowerOfTwo(allocation_size_max) || allocation_size_max <= allocation_size_min)
        return 0;

    PAL_MemoryAllocatorQueryMemoryIndexSize(&info, allocation_size_min, allocation_size_max);
    return (pal_usize_t) info.TotalIndexSize;
}

PAL_API(int)
PAL_MemoryAllocatorCreate
(
    struct PAL_MEMORY_ALLOCATOR     *alloc, 
    struct PAL_MEMORY_ALLOCATOR_INIT *init
)
{
    pal_usize_t       total_mem_size =(pal_usize_t )(init->MemorySize + init->BytesReserved);
    pal_uint8_t          *state_data =(pal_uint8_t*) init->StateData;
    pal_uint8_t         *split_index;
    pal_uint8_t        *status_index;
    pal_uint32_t           level_bit;
    pal_uint32_t         level_index;
    pal_uint32_t         level_count;
    PAL_MEMORY_INDEX_SIZE index_size;

    /* basic parameter validation */
    if (init->AllocatorType != PAL_MEMORY_ALLOCATOR_TYPE_HOST && 
        init->AllocatorType != PAL_MEMORY_ALLOCATOR_TYPE_DEVICE)
    {   assert(init->AllocatorType != PAL_MEMORY_ALLOCATOR_TYPE_INVALID);
        PAL_ZeroMemory(alloc, sizeof(PAL_MEMORY_ALLOCATOR));
        return -1;
    }
    if (init->MemorySize == 0)
    {   assert(init->MemorySize != 0);
        PAL_ZeroMemory(alloc, sizeof(PAL_MEMORY_ALLOCATOR));
        return -1;
    }
    if (!PAL_IsPowerOfTwo(total_mem_size) || total_mem_size == 0)
    {   assert(PAL_IsPowerOfTwo(total_mem_size) && "init->MemorySize+init->BytesReserved must be a power-of-two");
        PAL_ZeroMemory(alloc, sizeof(PAL_MEMORY_ALLOCATOR));
        return -1;
    }
    if (init->UserDataSize > PAL_MEMORY_ALLOCATOR_MAX_USER)
    {   assert(init->UserDataSize <= PAL_MEMORY_ALLOCATOR_MAX_USER);
        PAL_ZeroMemory(alloc, sizeof(PAL_MEMORY_ALLOCATOR));
        return -1;
    }
    if (init->StateData == NULL || init->StateDataSize == 0)
    {   assert(init->StateData != NULL);
        assert(init->StateDataSize > 0);
        PAL_ZeroMemory(alloc, sizeof(PAL_MEMORY_ALLOCATOR));
        return -1;
    }
    /* arguments must be powers of two greater than zero */
    if (!PAL_IsPowerOfTwo(init->AllocationSizeMin) || init->AllocationSizeMin < 16)
    {   assert(PAL_IsPowerOfTwo(init->AllocationSizeMin) && "AllocationSizeMin must be a power-of-two");
        assert(init->AllocationSizeMin >= 16);
        PAL_ZeroMemory(alloc, sizeof(PAL_MEMORY_ALLOCATOR));
        return -1;
    }
    if (!PAL_IsPowerOfTwo(init->AllocationSizeMax) || init->AllocationSizeMax < init->AllocationSizeMin)
    {   assert(PAL_IsPowerOfTwo(init->AllocationSizeMax) && "AllocationSizeMax must be a power-of-two");
        assert(init->AllocationSizeMin < init->AllocationSizeMax);
        PAL_ZeroMemory(alloc, sizeof(PAL_MEMORY_ALLOCATOR));
        return -1;
    }
    if (init->BytesReserved >= init->AllocationSizeMax)
    {   assert(init->BytesReserved < init->AllocationSizeMax);
        PAL_ZeroMemory(alloc, sizeof(PAL_MEMORY_ALLOCATOR));
        return -1;
    }

    /* determine the number of levels and ensure the count doesn't exceed the limit */
    PAL_MemoryAllocatorQueryMemoryIndexSize(&index_size, init->AllocationSizeMin, init->AllocationSizeMax);
    level_bit    = index_size.MaxBitIndex;
    level_count  = index_size.LevelCount;
    split_index  = state_data + index_size.StatusIndexSize;
    status_index = state_data;
    if (init->StateDataSize < index_size.TotalIndexSize)
    {   assert(init->StateDataSize >= index_size.TotalIndexSize);
        PAL_ZeroMemory(alloc, sizeof(PAL_MEMORY_ALLOCATOR));
        return -1;
    }
    if (level_count > PAL_MEMORY_ALLOCATOR_MAX_LEVELS)
    {   assert(level_count <= PAL_MEMORY_ALLOCATOR_MAX_LEVELS);
        PAL_ZeroMemory(alloc, sizeof(PAL_MEMORY_ALLOCATOR));
        return -1;
    }

    /* set up the allocator state */
    PAL_ZeroMemory(alloc, sizeof(PAL_MEMORY_ALLOCATOR));
    PAL_ZeroMemory(status_index, index_size.StatusIndexSize);
    PAL_ZeroMemory(split_index , index_size.SplitIndexSize);
    alloc->AllocatorName     = init->AllocatorName;
    alloc->AllocatorType     = init->AllocatorType;
    alloc->LevelCount        = level_count;
    alloc->MemoryStart       = init->MemoryStart;
    alloc->MemorySize        = init->MemorySize  + init->BytesReserved;
    alloc->AllocationSizeMin = init->AllocationSizeMin;
    alloc->AllocationSizeMax = init->AllocationSizeMax;
    alloc->BytesReserved     = init->BytesReserved;
    alloc->SplitIndex        =(pal_uint64_t*) split_index;
    alloc->StatusIndex       =(pal_uint64_t*) status_index;
    alloc->StateData         = init->StateData;
    alloc->StateDataSize     = init->StateDataSize;
    alloc->SplitIndexSize    = index_size.SplitIndexSize;
    alloc->StatusIndexSize   = index_size.StatusIndexSize;
    for (level_index = 0; level_index < level_count; ++level_index, --level_bit)
    {
        PAL_MemoryAllocatorDescribeLevel(&alloc->LevelInfo[level_index], level_index, level_bit, 0);
        alloc->FreeCount[level_index] = 0;
    }

    /* mark the level-0 block as being free */
    alloc->StatusIndex[0] |= 1;
    alloc->FreeCount  [0]  = 1;

    /* sometimes the requirement of AllocationSizeMax being a power-of-two leads to 
     * significant memory waste, so allow the caller to specify a BytesReserved 
     * value to mark a portion of memory as unusable and make use of non-pow2 memory chunks. */
    if (init->BytesReserved > 0)
    {   /* allocate small blocks until BytesReserved is met. 
           allocating the smallest block size ensures the least amount of waste.
           contiguous blocks will be allocated, starting from invalid high addresses
           down to alloc->MemoryStart+(alloc->MemorySize-alloc->BytesReserved). 
           this leaves the user allocations to take up all of the valid address space. */
        pal_uint64_t  level_size = alloc->LevelInfo[level_count-1].BlockSize;
        pal_uint32_t block_count =(pal_uint32_t)((init->BytesReserved + (level_size-1)) / level_size);
        pal_uint32_t block_index;
        PAL_MEMORY_BLOCK b;
        for (block_index = 0; block_index < block_count; ++block_index)
        {
            (void) PAL_MemoryAllocatorAlloc(alloc, (pal_usize_t) level_size, 0, &b);
            assert((pal_uint64_t) b.HostAddress >= (init->MemoryStart+init->MemorySize));
            assert(b.BlockOffset >= init->MemorySize);
        }
        /* update the LevelInfo for the last level so we don't even check the reserved words */
        PAL_MemoryAllocatorDescribeLevel(&alloc->LevelInfo[level_count-1], level_count-1, alloc->LevelInfo[level_count-1].LevelBit, block_count);
    }
    if (init->UserData != NULL && init->UserDataSize > 0)
    {
        PAL_CopyMemory(alloc->UserData, init->UserData, init->UserDataSize);
    }
    return 0;
}

PAL_API(int)
PAL_MemoryAllocatorAlloc
(
    struct PAL_MEMORY_ALLOCATOR *alloc, 
    pal_usize_t                   size,
    pal_usize_t              alignment, 
    struct PAL_MEMORY_BLOCK     *block
)
{
    pal_uint64_t pow2_size;
    pal_uint32_t level_idx;
    pal_uint32_t check_idx;
    pal_uint32_t bit_index;

    if (size < alignment)
    {   /* round up to the requested alignment */
        size = alignment;
    }
    if (size < alloc->AllocationSizeMin)
    {   /* round up to the minimum possible block size */
        size =(pal_usize_t)(alloc->AllocationSizeMin);
    }
    if (alignment > alloc->AllocationSizeMin)
    {   assert(alignment <= alloc->AllocationSizeMin);
        return -1;
    }
    if (size > (pal_usize_t)(alloc->MemorySize - alloc->BytesReserved))
    {   /* the allocation size exceeds the amount of memory actually available */
        PAL_ZeroMemory(block, sizeof(PAL_MEMORY_BLOCK));
        return -1;
    }

    /* round the requested allocation size up to the nearest power of two >= size */
    if ((pow2_size = PAL_NextPow2GreaterOrEqual(size)) > alloc->AllocationSizeMax)
    {   assert(pow2_size <= alloc->AllocationSizeMax);
        return -1;
    }

    /* figure out what level the specified size corresponds to */
    PAL_BitScan_ui64_msb(pow2_size, &bit_index);
    level_idx = alloc->LevelInfo[0].LevelBit - bit_index;
    check_idx = level_idx;

    /* search for a free block to satisfy the allocation request */
    for ( ; ; )
    {   /* if the free list is not empty, the allocation can be satisfied at this level */
        if (alloc->FreeCount[check_idx] > 0)
        {
            PAL_MEMORY_BLOCK_INFO block_info;
            pal_uint64_t        *split_index = alloc->SplitIndex;
            pal_uint64_t       *status_index = alloc->StatusIndex;
            pal_uint64_t    block_index_word;
            pal_uint64_t    block_index_mask;
            pal_uint64_t  child_index_word_l;
            pal_uint64_t  child_index_mask_l;
            pal_uint64_t  child_index_word_r;
            pal_uint64_t  child_index_mask_r;
            pal_uint32_t         block_index;
            pal_uint32_t       child_index_l;
            pal_uint32_t       child_index_r;
            pal_uint64_t        block_offset;
            pal_uint64_t          block_size;

            /* locate the first free memory block at level check_idx */
            PAL_MemoryAllocatorFindFreeMemoryBlockAtLevel(&block_info, alloc, check_idx);
            block_index_mask = PAL_WORDSIZE_ONE << (block_info.BlockIndex & PAL_WORDSIZE_MASK);
            block_index_word = block_info.BlockIndex >> PAL_WORDSIZE_SHIFT;
            block_offset     = block_info.BlockOffset;
            block_index      = block_info.BlockIndex;
            block_size       = 1ULL << block_info.LevelShift;

            while (check_idx < level_idx)
            {   /* split block at check_idx into two equally-sized smaller blocks at level check_idx+1 */
                child_index_l      = (block_index * 2) + 1;
                child_index_r      = (block_index * 2) + 2;
                child_index_mask_l = PAL_WORDSIZE_ONE << (child_index_l & PAL_WORDSIZE_MASK);
                child_index_mask_r = PAL_WORDSIZE_ONE << (child_index_r & PAL_WORDSIZE_MASK);
                child_index_word_l = child_index_l >> PAL_WORDSIZE_SHIFT;
                child_index_word_r = child_index_r >> PAL_WORDSIZE_SHIFT;

                /* update index data */
                status_index[child_index_word_l] |= child_index_mask_l; /* mark block child_index_l as available */
                status_index[child_index_word_r] |= child_index_mask_r; /* mark block child_index_r as available */
                status_index[block_index_word  ] &=~block_index_mask;   /* mark block block_index as unavailable */
                split_index [block_index_word  ] |= block_index_mask;   /* mark block block_index as split */
                alloc->FreeCount[check_idx+1] = 2;                      /* two free blocks produced at level check_idx+1 */
                alloc->FreeCount[check_idx]--;                          /* one block claimed at level check_idx */

                /* update the current block */
                block_index_mask = child_index_mask_r;
                block_index_word = child_index_word_r;
                block_index      = child_index_r;
                block_size     >>= 1;
                block_offset    += block_size;
                check_idx++;
            }

            /* claim the block at level level_idx */
            status_index[block_index_word] &= ~block_index_mask;
            alloc->FreeCount[level_idx]--;

            block->SizeInBytes   = block_size;
            block->BlockOffset   = block_offset;
            block->HostAddress   = alloc->AllocatorType == PAL_MEMORY_ALLOCATOR_TYPE_HOST ? ((pal_uint8_t*) (pal_uintptr_t) alloc->MemoryStart + block_offset) : NULL;
            block->AllocatorType = alloc->AllocatorType;
            return 0;
        }
        if (check_idx != 0)
        {   /* check the next larger level to see if there are any free blocks */
            check_idx--;
        }
        else
        {   /* there are no free blocks that can satisfy the request */
            PAL_ZeroMemory(block, sizeof(PAL_MEMORY_BLOCK));
            return -1;
        }
    }
}

PAL_API(void*)
PAL_MemoryAllocatorHostAlloc
(
    struct PAL_MEMORY_ALLOCATOR *alloc, 
    pal_usize_t                   size,
    pal_usize_t              alignment, 
    struct PAL_MEMORY_BLOCK     *block
)
{
    PAL_MemoryAllocatorAlloc(alloc, size, alignment, block);
    return block->HostAddress;
}

PAL_API(int)
PAL_MemoryAllocatorRealloc
(
    struct PAL_MEMORY_ALLOCATOR *                  alloc, 
    struct PAL_MEMORY_BLOCK     * PAL_RESTRICT  existing, 
    pal_usize_t                                 new_size, 
    pal_usize_t                                alignment, 
    struct PAL_MEMORY_BLOCK     * PAL_RESTRICT new_block
)
{
    pal_uint64_t    *status_index = alloc->StatusIndex;
    pal_uint64_t     *split_index = alloc->SplitIndex;
    pal_uint64_t     block_offset;
    pal_uint64_t      local_index;
    pal_uint64_t       block_size;
    pal_uint64_t    pow2_size_old;
    pal_uint64_t    pow2_size_new;
    pal_uint64_t block_index_mask;
    pal_uint64_t buddy_index_mask;
    pal_uint32_t    level_idx_old;
    pal_uint32_t    level_idx_new;
    pal_uint32_t      block_index;
    pal_uint32_t      buddy_index;
    pal_uint32_t block_index_word;
    pal_uint32_t buddy_index_word;
    pal_uint32_t    bit_index_old;
    pal_uint32_t    bit_index_new;
    pal_sint32_t   buddy_offset[2] = { -1, +1 };

    if (existing->SizeInBytes == 0)
    {   /* pass the call off to PAL_MemoryAllocatorAlloc; there is no existing allocation */
        return PAL_MemoryAllocatorAlloc(alloc, new_size, alignment, new_block);
    }
    if (new_size < alignment)
    {   /* round up to the requested alignment */
        new_size = alignment;
    }
    if (new_size < alloc->AllocationSizeMin)
    {   /* round up to the minimum possible block size */
        new_size =(pal_usize_t)(alloc->AllocationSizeMin);
    }
    if (alignment > alloc->AllocationSizeMin)
    {   assert(alignment <= alloc->AllocationSizeMin);
        PAL_ZeroMemory(new_block, sizeof(PAL_MEMORY_BLOCK));
        return -1;
    }
    if (new_size > (pal_usize_t)(alloc->MemorySize - alloc->BytesReserved))
    {   /* the allocation size exceeds the amount of memory actually available */
        PAL_ZeroMemory(new_block, sizeof(PAL_MEMORY_BLOCK));
        return -1;
    }

    /* round up to the block size closest to new_size */
    pow2_size_old = (pal_uint64_t) existing->SizeInBytes;
    if ((pow2_size_new = PAL_NextPow2GreaterOrEqual(new_size)) > alloc->AllocationSizeMax)
    {   assert(pow2_size_new <= alloc->AllocationSizeMax);
        PAL_ZeroMemory(new_block, sizeof(PAL_MEMORY_BLOCK));
        return -1;
    }
    /* figure out the level index each size corresponds to */
    PAL_BitScan_ui64_msb(pow2_size_old, &bit_index_old);
    PAL_BitScan_ui64_msb(pow2_size_new, &bit_index_new);
    level_idx_old = alloc->LevelInfo[0].LevelBit - bit_index_old;
    level_idx_new = alloc->LevelInfo[0].LevelBit - bit_index_new;

    /* there are four scenarios this routine has to account for:
     * 1. The new_size still fits in the same block. No re-allocation is performed.
     * 2. The new_size is larger than the old size, but fits in a block one level larger, and the buddy block is free. The existing block is promoted to the next-larger level.
     * 3. The new_size is smaller than the old size by one or more levels. The existing block is demoted to a smaller block.
     * 4. The new_size is larger than the old size by more than one level, or the buddy was not free. A new, larger block is allocated and the existing block is freed.
     */
    if (level_idx_new == level_idx_old)
    {   /* scenario #1 - the new block still fits in the old block */
        PAL_CopyMemory(new_block, existing, sizeof(PAL_MEMORY_BLOCK));
        return 0;
    }
    if (level_idx_new ==(level_idx_old-1))
    {   /* scenario #2 - the new block needs a block one size larger   */
        /* if the buddy block is free, then promote the existing block */
        /* compute the index, mask etc. for the existing block         */
        local_index      = existing->BlockOffset     >> alloc->LevelInfo[level_idx_old].LevelBit;
        block_index      =(pal_uint32_t) local_index  + alloc->LevelInfo[level_idx_old].FirstBlockIndex;
        buddy_index      = block_index + buddy_offset[block_index & 1];
        block_index_mask = PAL_WORDSIZE_ONE << (block_index & PAL_WORDSIZE_MASK);
        buddy_index_mask = PAL_WORDSIZE_ONE << (buddy_index & PAL_WORDSIZE_MASK);
        block_index_word = block_index >> PAL_WORDSIZE_SHIFT;
        buddy_index_word = buddy_index >> PAL_WORDSIZE_SHIFT;
        if (status_index[buddy_index_word] & buddy_index_mask)
        {   /* the buddy block is free, so the blocks can be merged */
            status_index[buddy_index_word] &= ~buddy_index_mask;
            alloc->FreeCount[level_idx_old]--;

            /* calculate parent index and mark it as 'not split' */
            block_index      =(block_index - 1) / 2;
            block_index_mask = PAL_WORDSIZE_ONE << (block_index & PAL_WORDSIZE_MASK);
            block_index_word = block_index >> PAL_WORDSIZE_SHIFT;
            split_index[block_index_word] &= ~block_index_mask;

            /* calculate the attributes of the new block */
            block_size   = alloc->LevelInfo[level_idx_new].BlockSize;
            block_offset =(block_index - alloc->LevelInfo[level_idx_new].FirstBlockIndex) * block_size;

            /* return the new block to the caller - the data may or may not need to move */
            new_block->SizeInBytes   = block_size;
            new_block->BlockOffset   = block_offset;
            new_block->HostAddress   = alloc->AllocatorType == PAL_MEMORY_ALLOCATOR_TYPE_HOST ? ((pal_uint8_t*) (pal_uintptr_t) alloc->MemoryStart + block_offset) : NULL;
            new_block->AllocatorType = alloc->AllocatorType;
            return 0;
        }
    }
    if (level_idx_new >  level_idx_old)
    {   /* scenario #3 - the new block is smaller; demote the existing block */
        /* this produces one or more new blocks, but preserves existing data */
        PAL_MEMORY_ALLOCATOR_LEVEL   *level_info = &alloc->LevelInfo[level_idx_old];
        /* compute the index, mask etc. for the existing block */
        block_size       = existing->SizeInBytes;
        block_offset     = existing->BlockOffset;
        local_index      = existing->BlockOffset    >> level_info->LevelBit;
        block_index      =(pal_uint32_t) local_index + level_info->FirstBlockIndex;
        block_index_mask = PAL_WORDSIZE_ONE << (block_index & PAL_WORDSIZE_MASK);
        block_index_word = block_index >> PAL_WORDSIZE_SHIFT;

        /* perform splits down to the new level, preserving the block offset */
        while (level_idx_old < level_idx_new)
        {   /* mark the parent block as having been split */
            split_index[block_index_word] |= block_index_mask;

            /* calculate the index/word/mask for the child block and its buddy */
            level_info       = &alloc->LevelInfo[++level_idx_old];
            local_index      = block_offset >> level_info->LevelBit;
            block_index      =(pal_uint32_t) local_index + level_info->FirstBlockIndex;
            buddy_index      = block_index + buddy_offset[block_index & 1];
            block_index_mask = PAL_WORDSIZE_ONE << (block_index & PAL_WORDSIZE_MASK);
            buddy_index_mask = PAL_WORDSIZE_ONE << (buddy_index & PAL_WORDSIZE_MASK);
            block_index_word = block_index >> PAL_WORDSIZE_SHIFT;
            buddy_index_word = buddy_index >> PAL_WORDSIZE_SHIFT;
            block_size     >>= 1;

            /* produce one free block at the child level */
            status_index[buddy_index_word] |= buddy_index_mask;
            alloc->FreeCount[level_idx_old]++;
        }

        /* return the new block to the caller - no data copying is required */
        new_block->SizeInBytes   = block_size;
        new_block->BlockOffset   = existing->BlockOffset;
        new_block->HostAddress   = existing->HostAddress;
        new_block->AllocatorType = existing->AllocatorType;
        return 0;
    }
    /* else, no choice but to allocate a new block, copy data, and free the old block */
    if (PAL_MemoryAllocatorAlloc(alloc, new_size, alignment, new_block) < 0)
    {   /* allocation of the new block failed */
        return -1;
    }
    /* mark the old block as being free */
    PAL_MemoryAllocatorFree(alloc, existing);
    return 0;
}

PAL_API(void*)
PAL_MemoryAllocatorHostRealloc
(
    struct PAL_MEMORY_ALLOCATOR *                  alloc, 
    struct PAL_MEMORY_BLOCK     * PAL_RESTRICT  existing, 
    pal_usize_t                                 new_size, 
    pal_usize_t                                alignment, 
    struct PAL_MEMORY_BLOCK     * PAL_RESTRICT new_block
)
{   assert(existing->AllocatorType == PAL_MEMORY_ALLOCATOR_TYPE_HOST);
    if (PAL_MemoryAllocatorRealloc(alloc, existing, new_size, alignment, new_block) == 0)
    {   /* the reallocation request was successful. does the data need to be moved? */
        if (new_block->HostAddress != existing->HostAddress)
        {   /* the memory location changed; the data needs to be copied */
            PAL_CopyMemory(new_block->HostAddress, existing->HostAddress, (pal_usize_t) new_block->SizeInBytes);
        }
        return new_block->HostAddress;
    }
    else return NULL;
}

PAL_API(void)
PAL_MemoryAllocatorFree
(
    struct PAL_MEMORY_ALLOCATOR *alloc, 
    struct PAL_MEMORY_BLOCK     *block
)
{
    pal_uint64_t       local_index;
    pal_uint64_t      *split_index = alloc->SplitIndex;
    pal_uint64_t     *status_index = alloc->StatusIndex;
    pal_uint32_t         bit_index;
    pal_uint32_t       level_index;
    pal_uint32_t       block_index;
    pal_uint32_t       buddy_index;
    pal_uint32_t      parent_index;
    pal_uint32_t  block_index_word;
    pal_uint32_t  buddy_index_word;
    pal_uint32_t parent_index_word;
    pal_uint64_t  buddy_index_mask;
    pal_uint64_t  block_index_mask;
    pal_uint64_t parent_index_mask;
    pal_sint32_t    buddy_offset[2] = { -1, +1 };

    PAL_BitScan_ui64_msb(block->SizeInBytes, &bit_index);
    level_index         = alloc->LevelInfo[0].LevelBit - bit_index;
    local_index         = block->BlockOffset >> alloc->LevelInfo[level_index].LevelBit;
    block_index         =(pal_uint32_t) local_index + alloc->LevelInfo[level_index].FirstBlockIndex;
    buddy_index         = block_index + buddy_offset[block_index & 1];
    parent_index        =(block_index - 1) / 2;
    block_index_mask    = PAL_WORDSIZE_ONE << ( block_index & PAL_WORDSIZE_MASK);
    buddy_index_mask    = PAL_WORDSIZE_ONE << ( buddy_index & PAL_WORDSIZE_MASK);
    parent_index_mask   = PAL_WORDSIZE_ONE << (parent_index & PAL_WORDSIZE_MASK);
    block_index_word    = block_index  >> PAL_WORDSIZE_SHIFT;
    buddy_index_word    = buddy_index  >> PAL_WORDSIZE_SHIFT;
    parent_index_word   = parent_index >> PAL_WORDSIZE_SHIFT;

    /* merge the free block with its buddy block if the buddy block is free */
    while (level_index > 0)
    {   /* make sure that the split bit is set for the parent block */
        if ((split_index[parent_index_word] & parent_index_mask) == 0)
        {   /* the parent block was not split - this is a double-free */
            return;
        }
        /* check the status bit for the buddy block */
        if ((status_index[buddy_index_word] & buddy_index_mask) == 0)
        {   /* the buddy block is not available - block and buddy cannot be merged */
            break;
        }

        /* the block and its buddy can be merged into a larger parent block */
        /* mark the buddy block as being unavailable, consume one free item */
        status_index[buddy_index_word] &=~buddy_index_mask;
        alloc->FreeCount[level_index]--;

        /* clear the split status on the parent block */
        split_index[parent_index_word] &= ~parent_index_mask;

        /* update the block and buddy index/word/mask to be that of the parent */
        block_index       = parent_index;
        block_index_mask  = parent_index_mask;
        block_index_word  = parent_index_word;
        buddy_index       = parent_index + buddy_offset[parent_index & 1];
        parent_index      =(parent_index - 1) / 2;
        buddy_index_mask  = PAL_WORDSIZE_ONE << ( buddy_index & PAL_WORDSIZE_MASK);
        parent_index_mask = PAL_WORDSIZE_ONE << (parent_index & PAL_WORDSIZE_MASK);
        buddy_index_word  = buddy_index  >> PAL_WORDSIZE_SHIFT;
        parent_index_word = parent_index >> PAL_WORDSIZE_SHIFT;

        /* attempt to merge at the next-larger level */
        level_index--;
    }

    /* mark the block as being free */
    status_index[block_index_word] |= block_index_mask;
    alloc->FreeCount[level_index]++;
}

PAL_API(void)
PAL_MemoryAllocatorHostFree
(
    struct PAL_MEMORY_ALLOCATOR *alloc, 
    void                     *existing
)
{   assert(alloc->AllocatorType == PAL_MEMORY_ALLOCATOR_TYPE_HOST);
    if (existing != NULL)
    {   /* calculate the offset of the host allocation from the start of the memory block */
        pal_uint64_t     block_offset =(pal_uint64_t) existing - alloc->MemoryStart;
        pal_uint64_t     *split_index = alloc->SplitIndex;
        pal_uint32_t      level_index = alloc->LevelCount - 1;
        pal_uint64_t       block_size = alloc->LevelInfo[level_index].BlockSize;
        pal_uint64_t      local_index = block_offset >> alloc->LevelInfo[level_index].LevelBit;
        pal_uint32_t      block_index =(pal_uint32_t) local_index + alloc->LevelInfo[level_index].FirstBlockIndex;
        pal_uint64_t block_index_mask;
        pal_uint32_t block_index_word;
        PAL_MEMORY_BLOCK        block;

        while (level_index > 0)
        {   /* check the parent level to see if it's been split */
            block_index      =(block_index - 1) / 2;
            block_index_mask = PAL_WORDSIZE_ONE << (block_index & PAL_WORDSIZE_MASK);
            block_index_word = block_index >> PAL_WORDSIZE_SHIFT;
            if (split_index[block_index_word] & block_index_mask)
            {   /* the parent block has been split, so the allocation came from level level_index */
                block.SizeInBytes   = block_size;
                block.BlockOffset   = block_offset;
                block.HostAddress   = existing;
                block.AllocatorType = PAL_MEMORY_ALLOCATOR_TYPE_HOST;
                PAL_MemoryAllocatorFree(alloc, &block);
                return;
            }
            /* the parent has not been split, so check the next-largest level */
            block_size <<= 1;
            level_index--;
        }
        if (block_offset == 0)
        {   /* this must be a level-0 allocation */
            block.SizeInBytes   = alloc->LevelInfo[0].BlockSize;
            block.BlockOffset   = block_offset;
            block.HostAddress   = existing;
            block.AllocatorType = PAL_MEMORY_ALLOCATOR_TYPE_HOST;
            PAL_MemoryAllocatorFree(alloc, &block);
        }
    }
}

PAL_API(void)
PAL_MemoryAllocatorReset
(
    struct PAL_MEMORY_ALLOCATOR *alloc
)
{
    PAL_MEMORY_ALLOCATOR_LEVEL *last_level_info = &alloc->LevelInfo[alloc->LevelCount-1];

    /* return the merge and split indexes to their initial state.
     * zero out all of the free list entries.
     * return level 0 block 0 to the free list.
     */
    PAL_ZeroMemory(alloc->StatusIndex, alloc->StatusIndexSize);
    PAL_ZeroMemory(alloc->SplitIndex , alloc->SplitIndexSize);
    PAL_ZeroMemory(alloc->FreeCount  , PAL_MEMORY_ALLOCATOR_MAX_LEVELS * sizeof(pal_uint32_t));

    /* reset the level descriptor for the last level, in case it was modified because BytesReserved > 0 */
    PAL_MemoryAllocatorDescribeLevel(last_level_info, alloc->LevelCount-1, last_level_info->LevelBit, 0);

    /* mark the level-0 block as being free */
    alloc->StatusIndex[0] |= 1;
    alloc->FreeCount  [0]  = 1;

    if (alloc->BytesReserved > 0)
    {   /* allocate small blocks until BytesReserved is met. 
         * allocating the smallest block size ensures the least amount of waste.
         * contiguous blocks will be allocated, starting from invalid high addresses
         * down to alloc->MemoryStart+(alloc->MemorySize-alloc->BytesReserved). 
         * this leaves the user allocations to take up all of the valid address space.
         */
        pal_uint64_t  level_size = alloc->LevelInfo[alloc->LevelCount-1].BlockSize;
        pal_uint32_t block_count =(pal_uint32_t)((alloc->BytesReserved + (level_size-1)) / level_size);
        pal_uint32_t block_index;
        PAL_MEMORY_BLOCK b;
        for (block_index = 0; block_index < block_count; ++block_index)
        {
            (void) PAL_MemoryAllocatorAlloc(alloc, (pal_usize_t) level_size, 0, &b);
            assert((pal_uint64_t) b.HostAddress >= (alloc->MemoryStart+(alloc->MemorySize-alloc->BytesReserved)));
            assert(b.BlockOffset >= (alloc->MemorySize-alloc->BytesReserved));
        }
        /* update the LevelInfo for the last level so we don't even check the reserved words */
        PAL_MemoryAllocatorDescribeLevel(last_level_info, alloc->LevelCount-1, last_level_info->LevelBit, block_count);
    }
}

PAL_API(int)
PAL_MemoryViewInit
(
    struct PAL_MEMORY_VIEW     *view, 
    struct PAL_MEMORY_LAYOUT *layout, 
    void               *base_address, 
    pal_uint32_t          chunk_size, 
    pal_uint32_t          item_count
)
{
    pal_uint8_t *base = (pal_uint8_t*) base_address;
    pal_uint32_t s, a;
    pal_uint32_t i, n;

    assert(base_address != NULL);
    assert(chunk_size   >= item_count);

    if (base_address != NULL && item_count > 0)
    {   /* initialize a valid layout */
        view->StreamCount    = layout->StreamCount;
        view->ElementCount   = item_count;
        for (i = 0, n = layout->StreamCount; i < n; ++i)
        {   /* calculate the base address for this data stream */
            s    = layout->StreamSize [i];
            a    = layout->StreamAlign[i];
            base =(pal_uint8_t*) PAL_AlignFor(base, a);
            /* initialize the view elements */
            view->Stream[i] = base;
            view->Stride[i] = s;
            /* move to the next stream */
            base += chunk_size * s;
        }
        return  0;
    }
    else
    {   /* initialize an invalid layout */
        view->StreamCount    = layout->StreamCount;
        view->ElementCount   = 0;
        for (i = 0, n = layout->StreamCount; i < n; ++i)
        {
            view->Stream[i]  = NULL;
            view->Stride[i]  = 0;
        }
        return -1;
    }
}

PAL_API(void)
PAL_MemoryLayoutInit
(
    struct PAL_MEMORY_LAYOUT *layout
)
{
    PAL_ZeroMemory(layout, sizeof(PAL_MEMORY_LAYOUT));
}

PAL_API(void)
PAL_MemoryLayoutCopy
(
    struct PAL_MEMORY_LAYOUT * PAL_RESTRICT dst, 
    struct PAL_MEMORY_LAYOUT * PAL_RESTRICT src
)
{
    pal_uint32_t i, n;
    for (i = 0, n = src->StreamCount; i < n; ++i)
    {
        dst->StreamSize [i] = src->StreamSize [i];
        dst->StreamAlign[i] = src->StreamAlign[i];
    }
    dst->StreamCount = n;
}

PAL_API(int)
PAL_MemoryLayoutDefineStream
(
    struct PAL_MEMORY_LAYOUT *layout, 
    pal_usize_t            item_size, 
    pal_usize_t         stream_align
)
{
    if (item_size <= 0)
    {   /* the element size must be greater than zero */
        assert(item_size > 0);
        return -1;
    }
    if (stream_align <= 0 || PAL_IsPowerOfTwo(stream_align) == 0)
    {   /* the alignment must be a power of two greater than zero */
        assert(stream_align > 0);
        assert(PAL_IsPowerOfTwo(stream_align));
        return -1;
    }
    if (layout->StreamCount == 8)
    {   /* too many data streams defined */
        assert(layout->StreamCount < 8 && "Increase the number of data streams in PAL_MEMORY_LAYOUT and PAL_MEMORY_VIEW");
        return -1;
    }
    layout->StreamSize [layout->StreamCount] = (pal_uint32_t) item_size;
    layout->StreamAlign[layout->StreamCount] = (pal_uint32_t) stream_align;
    layout->StreamCount++;
    return 0;
}

PAL_API(pal_usize_t)
PAL_MemoryLayoutComputeSize
(
    struct PAL_MEMORY_LAYOUT *layout, 
    pal_usize_t           item_count
)
{
    pal_usize_t required_size = 0;
    pal_uint32_t         i, n;
    for (i = 0, n = layout->StreamCount; i < n; ++i)
    {
        required_size += PAL_AllocationSizeArrayRaw(layout->StreamSize[i], layout->StreamAlign[i], item_count);
    }
    return required_size;
}

PAL_API(int)
PAL_DynamicBufferCreate
(
    struct PAL_DYNAMIC_BUFFER    *buffer, 
    struct PAL_DYNAMIC_BUFFER_INIT *init
)
{
    void                 *addr = NULL;
    pal_usize_t      grow_size = 0;
    pal_usize_t   reserve_size = 0;
    pal_usize_t    commit_size = 0;
    pal_usize_t element_stride = 0;
    SYSTEM_INFO        sysinfo;

    if (init->ElementSize == 0)
    {   /* each element must be at least one byte - use PAL_SizeOf(type) */
        assert(init->ElementSize != 0);
        return -1;
    }
    if (init->ElementAlign == 0 || PAL_IsPowerOfTwo(init->ElementAlign) == 0)
    {   /* the alignment must be a non-zero power of two - use PAL_AlignOf(type) */
        assert(init->ElementAlign != 0);
        assert(PAL_IsPowerOfTwo(init->ElementAlign));
        return -1;
    }
    if (init->InitialCommitment > init->MaxTotalCommitment)
    {   /* the initial commitment cannot exceed the maximum commitment */
        assert(init->InitialCommitment <= init->MaxTotalCommitment);
        return -1;
    }
    /* determine the number of bytes required to store one element */
    element_stride = PAL_AlignUp(init->ElementSize, init->ElementAlign);
    /* query the OS for the system page size and allocation granularity */
    GetNativeSystemInfo(&sysinfo);
    if (init->ElementAlign > sysinfo.dwAllocationGranularity)
    {   /* the desired alignment must be less than the allocation granularity */
        assert(init->ElementAlign <= sysinfo.dwAllocationGranularity);
        return -1;
    }
    /* round the reserve and initial commit sizes up to multiples of the element stride */
    reserve_size = PAL_AlignUp(init->MaxTotalCommitment, element_stride);
    commit_size  = PAL_AlignUp(init->InitialCommitment , element_stride);
    grow_size    = PAL_AlignUp(init->MinCommitIncrease , element_stride);
    /* round the reserve and initial commit sizes up to multiples of the page size */
    reserve_size = PAL_AlignUp(reserve_size, sysinfo.dwPageSize);
    commit_size  = PAL_AlignUp(commit_size , sysinfo.dwPageSize);
    grow_size    = PAL_AlignUp(grow_size   , sysinfo.dwPageSize);
    if (reserve_size > 0)
    {    /* make the initial address space reservation from the host OS */
        if ((addr = VirtualAlloc(NULL, (SIZE_T) reserve_size, MEM_RESERVE, PAGE_NOACCESS)) == NULL)
        {   /* the initial reservation failed - buffer creation fails */
            return -1;
        }
    }
    if (commit_size > 0)
    {   /* commit the requested portion of the reserved address space */
        if (VirtualAlloc(addr, (SIZE_T) commit_size, MEM_COMMIT, PAGE_READWRITE) != addr)
        {   /* the commit failed - buffer creation fails */
            VirtualFree(addr, 0, MEM_RELEASE);
            return -1;
        }
    }
    buffer->BaseAddress      =(pal_uint8_t*) addr;
    buffer->EndAddress       =(pal_uint8_t*) addr; /* the buffer is currently empty */
    buffer->ElementCount     = 0;
    buffer->ElementCapacity  =(pal_usize_t )(commit_size  / element_stride);
    buffer->ElementCountMax  =(pal_usize_t )(reserve_size / element_stride);
    buffer->ElementCountGrow =(pal_uint32_t)(grow_size    / element_stride);
    buffer->ElementAlignment =(pal_uint32_t) init->ElementAlign;
    buffer->ElementBaseSize  =(pal_uint32_t) init->ElementSize;
    buffer->ElementStride    =(pal_uint32_t) element_stride;
    buffer->OsPageSize       =(pal_uint32_t) sysinfo.dwPageSize;
    buffer->OsGranularity    =(pal_uint32_t) sysinfo.dwAllocationGranularity;
    return  0;
}

PAL_API(void)
PAL_DynamicBufferDelete
(
    struct PAL_DYNAMIC_BUFFER *buffer
)
{
    if (buffer->BaseAddress != NULL)
    {
        VirtualFree(buffer->BaseAddress, 0, MEM_RELEASE);
        buffer->BaseAddress  = NULL;
        buffer->EndAddress   = NULL;
    }
    buffer->ElementCount     = 0;
    buffer->ElementCapacity  = 0;
    buffer->ElementCountMax  = 0;
}

PAL_API(int)
PAL_DynamicBufferEnsure
(
    struct PAL_DYNAMIC_BUFFER *buffer, 
    pal_usize_t  capacity_in_elements
)
{
    pal_usize_t new_commit_size = 0;
    pal_usize_t    new_capacity = 0;
    if (capacity_in_elements <= buffer->ElementCapacity)
    {   /* existing capacity is sufficient */
        return  0;
    }
    if((buffer->ElementCapacity + buffer->ElementCountGrow) > capacity_in_elements)
    {   /* increase by the minimum amount */
        new_capacity = buffer->ElementCapacity + buffer->ElementCountGrow;
    }
    else
    {   /* increase by the desired amount */
        new_capacity = capacity_in_elements;
    }
    if (new_capacity > buffer->ElementCountMax)
    {   /* the desired capacity exceeds the maximum allowable capacity */
        if (capacity_in_elements <= buffer->ElementCountMax)
        {   /* clamp to the maximum capacity */
            new_capacity = buffer->ElementCountMax;
        }
        else
        {   /* the desired capacity exceeds the maximum allowable capacity */
            return -1;
        }
    }
    /* at this point, the amount of committed space needs to increase */
    new_commit_size = new_capacity * buffer->ElementStride;
    new_commit_size = PAL_AlignUp(new_commit_size, buffer->OsPageSize);
    if (VirtualAlloc(buffer->BaseAddress, (SIZE_T) new_commit_size, MEM_COMMIT, PAGE_READWRITE) != buffer->BaseAddress)
    {   /* increasing the amount of committed memory failed */
        return -1;
    }
    buffer->ElementCapacity = new_commit_size / buffer->ElementStride;
    return 0;
}

PAL_API(int)
PAL_DynamicBufferShrink
(
    struct PAL_DYNAMIC_BUFFER *buffer
)
{
    pal_usize_t committed_bytes = 0;
    pal_usize_t    needed_bytes = 0;
    /* calculate the number of bytes actually committed */
    committed_bytes = buffer->ElementCapacity * buffer->ElementStride;
    committed_bytes = PAL_AlignUp(committed_bytes, buffer->OsPageSize);
    /* calculate the number of bytes actually needed */
    needed_bytes = buffer->ElementCount * buffer->ElementStride;
    needed_bytes = PAL_AlignUp(needed_bytes, buffer->OsPageSize);
    /* is it even possible to shrink the amount of committed address space? */
    if (needed_bytes == committed_bytes)
    {   /* cannot shrink at all */
        return 0;
    }
    /* decommit address space - we can shrink by at least one page */
    if (VirtualFree(buffer->BaseAddress + needed_bytes, (committed_bytes - needed_bytes), MEM_DECOMMIT))
    {   /* the buffer was successfully shrunk */
        return 0;
    }
    else
    {   /* the decommit operation failed */
        return -1;
    }
}

PAL_API(int)
PAL_DynamicBufferResize
(
    struct PAL_DYNAMIC_BUFFER *buffer, 
    pal_usize_t      size_in_elements
)
{
    if (size_in_elements > buffer->ElementCapacity)
    {   /* ensure that the specified number of elements can be stored */
        if (PAL_DynamicBufferEnsure(buffer, size_in_elements) == 0)
        {   /* the capacity was successfully increased */
            buffer->EndAddress    = buffer->BaseAddress + (buffer->ElementStride * size_in_elements);
            buffer->ElementCount  = size_in_elements;
            return 0;
        }
        else
        {   /* the capacity could not be changed */
            return -1;
        }
    }
    if (size_in_elements < buffer->ElementCapacity)
    {   /* truncate the existing buffer and then shrink */
        PAL_DynamicBufferTruncate(buffer, size_in_elements);
    }
    return PAL_DynamicBufferShrink(buffer);
}

PAL_API(void)
PAL_DynamicBufferReset
(
    struct PAL_DYNAMIC_BUFFER *buffer
)
{
    buffer->EndAddress   = buffer->BaseAddress;
    buffer->ElementCount = 0;
}

PAL_API(pal_uint8_t*)
PAL_DynamicBufferBegin
(
    struct PAL_DYNAMIC_BUFFER *buffer
)
{
    return buffer->BaseAddress;
}

PAL_API(pal_uint8_t*)
PAL_DynamicBufferEnd
(
    struct PAL_DYNAMIC_BUFFER *buffer
)
{
    return buffer->EndAddress;
}

PAL_API(pal_uint8_t*)
PAL_DynamicBufferElementAddress
(
    struct PAL_DYNAMIC_BUFFER *buffer, 
    pal_usize_t                 index
)
{
    return buffer->BaseAddress +(buffer->ElementStride * index);
}

PAL_API(pal_usize_t)
PAL_DynamicBufferCount
(
    struct PAL_DYNAMIC_BUFFER *buffer
)
{
    return buffer->ElementCount;
}

PAL_API(int)
PAL_DynamicBufferAppend
(
    struct PAL_DYNAMIC_BUFFER *buffer, 
    void const                   *src, 
    pal_usize_t         element_count, 
    pal_usize_t          element_size
)
{
    if (PAL_DynamicBufferEnsure(buffer, buffer->ElementCount+element_count) == 0)
    {   /* append the data to the end of the buffer */
        if (element_size == buffer->ElementStride)
        {   /* copy the data all in one go */
            PAL_CopyMemory(buffer->EndAddress, src, element_count * element_size);
            buffer->EndAddress   += element_count * element_size;
            buffer->ElementCount += element_count;
            return 0;
        }
        else
        {   /* the stride is different - copy the data elements one at a time */
            pal_uint8_t *dst_ptr = buffer->EndAddress;
            pal_uint8_t *src_ptr =(pal_uint8_t*)  src;
            pal_usize_t  dstride =(pal_usize_t )  buffer->ElementStride;
            pal_usize_t  sstride =(pal_usize_t )  element_size;
            pal_usize_t        i;
            for (i = 0; i < element_count; ++i)
            {
                PAL_CopyMemory(dst_ptr, src_ptr, element_size);
                dst_ptr += dstride;
                src_ptr += sstride;
            }
            buffer->EndAddress    = dst_ptr;
            buffer->ElementCount += element_count;
            return 0;
        }
    }
    else
    {   /* the buffer has insufficient capacity and cannot be expanded */
        return -1;
    }
}

PAL_API(int)
PAL_DynamicBufferTruncate
(
    struct PAL_DYNAMIC_BUFFER *buffer, 
    pal_usize_t     new_element_count
)
{
    if (new_element_count <= buffer->ElementCount)
    {   /* truncate the buffer to new_element_count items */
        buffer->EndAddress   = buffer->BaseAddress + (new_element_count * buffer->ElementStride);
        buffer->ElementCount = new_element_count;
        return  0;
    }
    else
    {   /* the supplied new_element_count is invalid */
        assert(new_element_count <= buffer->ElementCount);
        return -1;
    }
}

PAL_API(int)
PAL_HandleTableCreate
(
    struct PAL_HANDLE_TABLE     *table, 
    struct PAL_HANDLE_TABLE_INIT *init
)
{
    pal_uint8_t            *chunk_addr = NULL;
    pal_uint8_t             *base_addr = NULL;
    pal_uint8_t             *meta_addr = NULL;
    pal_uint64_t         *chunk_commit = NULL;
    pal_uint64_t         *chunk_status = NULL;
    pal_uint16_t         *chunk_counts = NULL;
    pal_uint32_t          *chunk_state = NULL;
    pal_uint32_t          *chunk_dense = NULL;
    pal_uint32_t           *chunk_init = NULL;
    pal_usize_t           reserve_size = 0;
    pal_usize_t             chunk_size = 0;
    pal_usize_t             array_size = 0;
    pal_usize_t              meta_size = 0;
    pal_usize_t              data_size = 0;
    pal_uint32_t              n_commit = 0;
    PAL_MEMORY_ARENA             arena;
    PAL_MEMORY_ARENA_INIT   arena_init;
    SYSTEM_INFO                sysinfo;
    pal_uint32_t                     i;

    if (init->Namespace > PAL_HANDLE_NAMES_MAX)
    {   /* the namespace identifier is invalid */
        assert(init->Namespace >= PAL_HANDLE_NAMES_MIN);
        assert(init->Namespace <= PAL_HANDLE_NAMES_MAX);
        return -1;
    }
    if (init->InitialCommit > PAL_HANDLE_TABLE_MAX_OBJECT_COUNT)
    {   /* the table initial commit count is invalid */
        assert(init->InitialCommit <= PAL_HANDLE_TABLE_MAX_OBJECT_COUNT);
        return -1;
    }
    if (init->TableFlags & PAL_HANDLE_TABLE_FLAG_STORAGE)
    {
        if (init->DataLayout == NULL || init->DataLayout->StreamCount == 0)
        {   /* the caller must supply a valid PAL_MEMORY_LAYOUT */
            assert(init->DataLayout != NULL);
            assert(init->DataLayout->StreamCount > 0);
            return -1;
        }
    }

    /* retrieve the OS page size and allocation granularity */
    GetNativeSystemInfo(&sysinfo);

    /* the memory layout of the PAL_HANDLE_TABLE address space is:
     * [chunk0]...[chunkN][init][commit][status][counts]
     * where [chunk0] through [chunkN] are chunk_size bytes.
     * - the memory layout of each individual chunk is specified below.
     * where [init] is an array of PAL_HANDLE_CHUNK_CAPACITY 32-bit unsigned integers [0, PAL_HANDLE_CHUNK_CAPACITY-1].
     * - the contents of [init] are constant and used to initialize the free list of sparse indices stored in the chunk's dense array.
     * - the [init] block is 4KB (1 page) at a PAL_HANDLE_CHUNK_CAPACITY of 1024 slots.
     * where [commit] is an array of PAL_HANDLE_CHUNK_WORD_COUNT (16) 64-bit unsigned integers used as a bitvector.
     * - each bit of [commit] is set if the corresponding chunk is committed, or clear otherwise.
     * - the [commit] block is 128 bytes at a PAL_HANDLE_CHUNK_COUNT of 1024.
     * where [status] is an array of PAL_HANDLE_CHUNK_WORD_COUNT (16) 64-bit unsigned integers used as a bitvector.
     * - each bit of [status] is set of the corresponding chunk has at least one free slot, or clear if the chunk is completely full.
     * - the [status] block is 128 bytes at a PAL_HANDLE_CHUNK_COUNT of 1024.
     * where [counts] is an array of PAL_HANDLE_CHUNK_COUNT 16-bit unsigned integers [0, PAL_HANDLE_CHUNK_CAPACITY-1].
     * - each entry in [counts] specifies the number of used slots in that chunk.
     * - the [counts] block is 2048 bytes at a PAL_HANDLE_CHUNK_COUNT of 1024.
     *
     * basically there is an 8KB fixed overhead for an empty PAL_HANDLE_TABLE.
     *
     * the memory layout of an individual chunk is:
     * [data][state][dense].
     * where [data] is data_size bytes, which may be zero, but if non-zero, is padded out to a page boundary.
     * where [state] is PAL_HANDLE_CHUNK_CAPACITY 32-bit unsigned integers (array_size bytes).
     * - each entry in [state] has the high bit set if that item contains a valid slot.
     * - each entry in [state] has the generation bits set to the valid generation value.
     * - each entry in [state], if valid, specifies the index of the item in the [dense] array.
     * where [dense] is PAL_HANDLE_CHUNK_CAPACITY 32-bit unsigned integers (array_size bytes).
     * - look at [counts][chunk_index] (=N below) to retrieve the number of live slots in [dense].
     * - the first [0, N) slots in [dense] store the PAL_HANDLE of the entity.
     * - the non-live slots [N, PAL_HANDLE_CHUNK_CAPACITY-1) in [dense] store an index of a free slot in [state].
     */

    /* determine the size of a single chunk, in bytes */
    if (init->TableFlags & PAL_HANDLE_TABLE_FLAG_STORAGE)
    {   /* calculate the size of the data stream portion of each chunk */
        data_size  = PAL_MemoryLayoutComputeSize(init->DataLayout, PAL_HANDLE_CHUNK_CAPACITY);
        data_size  = PAL_AlignUp(data_size, sysinfo.dwPageSize);
    }
    /* account for the size of the sparse/dense overhead per-chunk */
    array_size   = PAL_HANDLE_CHUNK_CAPACITY * sizeof(pal_uint32_t);
    chunk_size   = data_size;      /* [data] */
    chunk_size  += array_size * 2; /* [sparse][dense] */
    chunk_size   = PAL_AlignUp(chunk_size, sysinfo.dwPageSize);

    /* calculate the size of the status tracking metadata */
    meta_size    = PAL_AllocationSizeArray(pal_uint64_t, PAL_HANDLE_CHUNK_WORD_COUNT); /* ChunkCommit bitvector */
    meta_size   += PAL_AllocationSizeArray(pal_uint64_t, PAL_HANDLE_CHUNK_WORD_COUNT); /* ChunkStatus bitvector */
    meta_size   += PAL_AllocationSizeArray(pal_uint32_t, PAL_HANDLE_CHUNK_CAPACITY);   /* ChunkInit   */
    meta_size   += PAL_AllocationSizeArray(pal_uint16_t, PAL_HANDLE_CHUNK_COUNT);      /* ChunkCounts */
    meta_size    = PAL_AlignUp(meta_size, sysinfo.dwPageSize);

    /* calculate the total amount of address space to reserve */
    reserve_size = meta_size + (chunk_size * PAL_HANDLE_CHUNK_COUNT);

    /* calculate the number of chunks to pre-commit (InitialCommit is specified in items) */
    n_commit     =(init->InitialCommit + PAL_HANDLE_CHUNK_CAPACITY - 1) / PAL_HANDLE_CHUNK_CAPACITY;

    /* reserve address space */
    if ((base_addr = (pal_uint8_t*) VirtualAlloc(NULL, reserve_size, MEM_RESERVE, PAGE_NOACCESS)) == NULL)
    {   /* failed to reserve the necessary address space */
        goto cleanup_and_fail;
    }

    /* commit address space for status tracking */
    chunk_addr = base_addr;
    meta_addr  = base_addr + (PAL_HANDLE_CHUNK_COUNT * chunk_size);
    if (VirtualAlloc(meta_addr, meta_size, MEM_COMMIT, PAGE_READWRITE) != meta_addr)
    {   /* failed to commit the necessary address space */
        goto cleanup_and_fail;
    }
    /* initialize the pointers to the state tracking arrays  */
    arena_init.AllocatorName = __FUNCTION__;
    arena_init.AllocatorType = PAL_MEMORY_ALLOCATOR_TYPE_HOST;
    arena_init.MemoryStart   =(pal_uint64_t) meta_addr;
    arena_init.MemorySize    =(pal_uint64_t) meta_size;
    arena_init.UserData      = NULL;
    arena_init.UserDataSize  = 0;
    if (PAL_MemoryArenaCreate(&arena, &arena_init) < 0)
    {   /* failed to initialize the memory arena */
        goto cleanup_and_fail;
    }
    chunk_commit  = PAL_MemoryArenaAllocateHostArray(&arena, pal_uint64_t, PAL_HANDLE_CHUNK_WORD_COUNT);
    chunk_status  = PAL_MemoryArenaAllocateHostArray(&arena, pal_uint64_t, PAL_HANDLE_CHUNK_WORD_COUNT);
    chunk_init    = PAL_MemoryArenaAllocateHostArray(&arena, pal_uint32_t, PAL_HANDLE_CHUNK_CAPACITY);
    chunk_counts  = PAL_MemoryArenaAllocateHostArray(&arena, pal_uint16_t, PAL_HANDLE_CHUNK_COUNT);

    /* initialize the array specifying the initial chunk freelist state */
    for (i = 0; i < PAL_HANDLE_CHUNK_CAPACITY; ++i)
    {
        chunk_init[i] = i;
    }

    if (n_commit > 0)
    {   /* pre-commit the first n_commit chunks */
        if (VirtualAlloc(base_addr, n_commit * chunk_size, MEM_COMMIT, PAGE_READWRITE) != base_addr)
        {   /* failed to commit the necessary address space */
            goto cleanup_and_fail;
        }
        for (i = 0; i < n_commit; ++i)
        {   /* initialize each pre-committed chunk.
             * - mark the chunk as being committed.
             * - mark the chunk as being available (has one or more free slots).
             * - initialize the chunk free list.
             */
            chunk_state = (pal_uint32_t*)((chunk_addr + chunk_size) - (array_size * 2));
            chunk_dense = (pal_uint32_t*)((chunk_addr + chunk_size) - (array_size * 1));
            chunk_commit[i >> PAL_HANDLE_CHUNK_WORD_SHIFT] |= 1ULL << (i & PAL_HANDLE_CHUNK_WORD_MASK);
            chunk_status[i >> PAL_HANDLE_CHUNK_WORD_SHIFT] |= 1ULL << (i & PAL_HANDLE_CHUNK_WORD_MASK);
            PAL_CopyMemory(chunk_dense, chunk_init, array_size);
            chunk_addr += chunk_size;
        }
    }

    /* set the fields of the PAL_HANDLE_TABLE */
    table->BaseAddress   = base_addr;
    table->ChunkCommit   = chunk_commit;
    table->ChunkStatus   = chunk_status;
    table->ChunkCounts   = chunk_counts;
    table->ChunkInit     = chunk_init;
    table->ChunkSize     =(pal_uint32_t) chunk_size;
    table->DataSize      =(pal_uint32_t) data_size;
    table->CommitCount   = n_commit;
    table->Namespace     = init->Namespace;
    table->TableFlags    = init->TableFlags;
    table->OsPageSize    = sysinfo.dwPageSize;
    if (init->TableFlags & PAL_HANDLE_TABLE_FLAG_STORAGE)
    {   /* copy over the data layout definition */
        PAL_MemoryLayoutCopy(&table->DataLayout, init->DataLayout);
    }
    else
    {   /* zero the data layout definition */
        PAL_ZeroMemory(&table->DataLayout, sizeof(PAL_MEMORY_LAYOUT));
    }
    return  0;

cleanup_and_fail:
    if (base_addr != NULL) VirtualFree(base_addr, 0, MEM_RELEASE);
    PAL_ZeroMemory(table, sizeof(PAL_HANDLE_TABLE));
    return -1;
}

PAL_API(void)
PAL_HandleTableDelete
(
    struct PAL_HANDLE_TABLE *table
)
{
    if (table->BaseAddress != NULL)
    {
        VirtualFree(table->BaseAddress, 0, MEM_RELEASE);
        table->BaseAddress  = NULL;
    }
}

PAL_API(void)
PAL_HandleTableReset
(
    struct PAL_HANDLE_TABLE *table
)
{
    pal_uint64_t     *commit = table->ChunkCommit;
    pal_uint64_t     *status = table->ChunkStatus;
    pal_uint16_t     *counts = table->ChunkCounts;
    pal_uint32_t state_index;
    pal_uint32_t     i, j, n;
    for (i = 0, n =table->CommitCount; i < PAL_HANDLE_CHUNK_COUNT && n > 0; ++i)
    {   /* if this chunk is committed, reset its free list */
        pal_uint32_t chunk_word = i >> PAL_HANDLE_CHUNK_WORD_SHIFT;
        pal_uint32_t  chunk_bit = i  & PAL_HANDLE_CHUNK_WORD_MASK;
        pal_uint64_t chunk_mask = 1ULL << chunk_bit;
        if (commit[chunk_word]  & chunk_mask)
        {   /* this chunk is committed; reset it */
            pal_uint32_t *s = PAL_HandleTableGetChunkState(table, i);
            pal_uint32_t *d = PAL_HandleTableGetChunkDense(table, i);
            pal_uint16_t  k = PAL_HandleTableGetChunkItemCount(table, i);
            for (j = 0; j < k; ++j)
            {
                state_index = PAL_HandleValueGetStateIndex(d[j]);
                s[state_index] =(s[state_index]+1) & PAL_HANDLE_GENER_MASK;
                d[j] = state_index;
            }
            status[chunk_word] |= chunk_mask;
            counts[i] = 0;
            n--;
        }
    }
}

PAL_API(int)
PAL_HandleTableValidateIds
(
    struct PAL_HANDLE_TABLE *table, 
    PAL_HANDLE            *handles, 
    pal_usize_t              count
)
{
    pal_uint32_t  *chunk_state;
    pal_uint32_t   chunk_index;
    pal_uint32_t   state_index;
    pal_uint32_t   state_value;
    pal_uint32_t   state_gener;
    pal_uint32_t    state_live;
    pal_uint32_t   handle_live;
    pal_uint32_t  handle_gener;
    pal_uint32_t  handle_value;
    pal_uint32_t handle_nspace;
    pal_uint32_t  table_nspace;
    pal_usize_t      range_beg;
    pal_usize_t      range_end;
    int                 result;

    if (count == 0)
    {   /* nothing to do */
        return 0;
    }
    
    /* load state for the first chunk */
    table_nspace = table->Namespace;
    chunk_index  = PAL_HandleValueGetChunkIndex(handles[0]);
    chunk_state  = PAL_HandleTableGetChunkState(table, chunk_index);
    range_beg    = 0;
    range_end    = 0;
    result       = 0;

    while (range_end < count)
    {   /* find the end of the run */
        while (range_end < count && PAL_HandleValueGetChunkIndex(handles[range_end]) == chunk_index)
            range_end++;

        do
        {   /* there's a range [range_beg, range_end) of handles with the same chunk */
            handle_value = handles[range_beg++];
            handle_live  = PAL_HandleValueGetLive(handle_value);
            handle_nspace= PAL_HandleValueGetNamespace(handle_value);
            handle_gener = PAL_HandleValueGetGeneration(handle_value);
            state_index  = PAL_HandleValueGetStateIndex(handle_value);
            state_value  = chunk_state[state_index];
            state_live   = PAL_HandleStateGetLive(state_value);
            state_gener  = PAL_HandleStateGetGeneration(state_value);
            /* validate the handle value */
            if (handle_live == 0)
            {   assert(handle_live == 1 && "Detected invalid handle value");
                result = -1;
                continue;
            }
            if (handle_nspace != table_nspace)
            {   assert(handle_nspace == table_nspace && "Handle did not come from the specified table");
                result = -1;
                continue;
            }
            /* validate the handle generation against the chunk state generation to detect expired handles */
            if (state_live == 0)
            {   assert(state_live == 1 && "Detected expired handle value");
                result = -1;
                continue;
            }
            if (state_gener != handle_gener)
            {   assert(state_gener == handle_gener && "Detected expired handle value");
                result = -1;
                continue;
            }
        } while (range_beg != range_end);

        if (range_end < count)
        {   /* load state for the next chunk */
            chunk_index = PAL_HandleValueGetChunkIndex(handles[range_end]);
            chunk_state = PAL_HandleTableGetChunkState(table, chunk_index);
            range_beg   = range_end;
            range_end   = range_end + 1;
        }
    }
    return result;
}

PAL_API(void)
PAL_HandleTableDeleteChunkIds
(
    struct PAL_HANDLE_TABLE *table, 
    pal_uint32_t       chunk_index
)
{
    pal_uint32_t        *state = PAL_HandleTableGetChunkState(table, chunk_index);
    pal_uint32_t        *dense = PAL_HandleTableGetChunkDense(table, chunk_index);
    pal_uint32_t         count = PAL_HandleTableGetChunkItemCount(table, chunk_index);
    pal_uint32_t   state_index;
    pal_uint32_t             i;
    for (i = 0; i < count; ++i)
    {
        state_index        = PAL_HandleValueGetStateIndex(dense[i]);
        state[state_index] =(state[state_index]+1) & PAL_HANDLE_GENER_MASK;
        dense[i]           = state_index;
    }
    PAL_HandleTableMarkChunkAvailable(table, chunk_index);
    PAL_HandleTableSetChunkItemCount (table, chunk_index, 0);
}

PAL_API(void)
PAL_HandleTableRemoveChunkIds
(
    struct PAL_HANDLE_TABLE *table, 
    pal_uint32_t       chunk_index
)
{
    pal_uint32_t        *state = PAL_HandleTableGetChunkState(table, chunk_index);
    pal_uint32_t        *dense = PAL_HandleTableGetChunkDense(table, chunk_index);
    pal_uint32_t         count = PAL_HandleTableGetChunkItemCount(table, chunk_index);
    pal_uint32_t   state_index;
    pal_uint32_t             i;
    for (i = 0; i < count; ++i)
    {
        state_index = PAL_HandleValueGetStateIndex(dense[i]);
        state[state_index] = 0; /* mark as unoccupied */
    }
    PAL_HandleTableSetChunkItemCount(table, chunk_index, 0);
}

PAL_API(int)
PAL_HandleTableVisit
(
    struct PAL_HANDLE_TABLE               *table, 
    struct PAL_HANDLE_TABLE_VISITOR_INIT *config
)
{
    PAL_HandleTableChunkVisitor_Func func = config->Callback;
    pal_uintptr_t                 context = config->Context;
    PAL_MEMORY_LAYOUT             *layout = PAL_HandleTableGetDataLayout(table);
    pal_uint64_t                  *commit = table->ChunkCommit;
    pal_uint16_t                  *counts = table->ChunkCounts;
    pal_uint64_t                  commitw = 0;
    pal_uint32_t                     word = 0;
    pal_uint32_t                      bit = 0;
    pal_uint32_t                  n_visit = 0;
    pal_uint32_t                 n_remain = table->CommitCount;
    pal_uint32_t                        i;
    pal_uint16_t                    chunk;

    PAL_HANDLE_TABLE_CHUNK  desc;
    PAL_MEMORY_VIEW         view;
    pal_uint16_t chunk_index[PAL_HANDLE_CHUNK_COUNT];
    pal_uint16_t chunk_count[PAL_HANDLE_CHUNK_COUNT];

    /* build a list of non-empty chunks */
    while (n_remain > 0 && word < PAL_HANDLE_CHUNK_WORD_COUNT)
    {   /* load the commit status for the next 64 chunks.
         * if all bits are 0, then no chunks are committed.
         */
        while ((commitw = commit[word]) != 0)
        {
            do
            {   /* find the next set bit */
                PAL_BitScan_ui64_lsb(commitw, &bit);
                /* clear the set bit in the local copy */
                commitw &= ~(1ULL << bit);
                /* determine the absolute index of the chunk */
                chunk =(pal_uint16_t) ((word << PAL_HANDLE_CHUNK_WORD_SHIFT) + bit);
                /* add non-empty chunks to the visit list */
                if (counts[chunk] != 0)
                {
                    chunk_index[n_visit] = chunk;
                    chunk_count[n_visit] = counts[chunk];
                    n_visit++;
                }
                /* n_remain is a count of committed chunks */
                n_remain--;
            } while (commitw != 0);
            word++;
        }
    }
    /* chunk_index, chunk_count and n_visit now specify a list of non-empty chunks.
     * gather data about the chunk and invoke the callback for each one.
     */
    for (i = 0; i < n_visit; ++i)
    {
        desc.IdList = PAL_HandleTableGetChunkDense(table, chunk_index[i]);
        desc.Data   = PAL_HandleTableGetChunkData (table, chunk_index[i]);
        desc.Index  = chunk_index[i];
        desc.Count  = chunk_count[i];
        PAL_MemoryViewInit(&view, layout, desc.Data, PAL_HANDLE_CHUNK_CAPACITY, chunk_count[i]);
        if (func(table, &desc, &view, context) == 0)
        {   /* the callback indicates enumeration should stop */
            return 1;
        }
    }
    return 0;
}

PAL_API(int)
PAL_HandleTableCreateIds
(
    struct PAL_HANDLE_TABLE *table, 
    PAL_HANDLE            *handles, 
    pal_usize_t              count
)
{
    pal_uint32_t            i = 0;
    pal_uint32_t       offset = 0;
    pal_uint32_t       nspace = table->Namespace;
    pal_uint32_t  chunk_count = 0;
    pal_uint16_t  index_list[PAL_HANDLE_CHUNK_COUNT];
    pal_uint16_t  count_list[PAL_HANDLE_CHUNK_COUNT];

    if (PAL_HandleTableEnsure(table, count, index_list, count_list, &chunk_count) != 0)
    {   /* insufficient capacity, or allocation failure */
        return -1;
    }
    for (i = 0; i < chunk_count; ++i)
    {   /* allocate as many IDs as possible within the chunk */
        offset += PAL_HandleTableCreateIdsInChunk(table, handles, offset, nspace, index_list[i], count_list[i]);
    }
    return 0;
}

PAL_API(void)
PAL_HandleTableDeleteIds
(
    struct PAL_HANDLE_TABLE *table, 
    PAL_HANDLE            *handles, 
    pal_usize_t              count
)
{
    PAL_MEMORY_LAYOUT *chunk_layout = PAL_HandleTableGetDataLayout(table);
    pal_uint8_t        *chunk_datap;
    pal_uint32_t       *chunk_state;
    pal_uint32_t       *chunk_dense;
    pal_uint32_t        chunk_index;
    pal_uint16_t        chunk_count;
    pal_usize_t        delete_count;
    pal_usize_t           range_beg;
    pal_usize_t           range_end;
    PAL_MEMORY_VIEW            view;

    if (count == 0)
    {   /* nothing to do */
        return;
    }

    /* load state for the first chunk */
    chunk_index  = PAL_HandleValueGetChunkIndex(handles[0]);
    chunk_datap  = PAL_HandleTableGetChunkData (table, chunk_index);
    chunk_state  = PAL_HandleTableGetChunkState(table, chunk_index);
    chunk_dense  = PAL_HandleTableGetChunkDense(table, chunk_index);
    chunk_count  = PAL_HandleTableGetChunkItemCount(table, chunk_index);
    range_beg    = 0;
    range_end    = 0;
    PAL_MemoryViewInit(&view, chunk_layout, chunk_datap, PAL_HANDLE_CHUNK_CAPACITY, chunk_count);

    while (range_end < count)
    {   /* find the end of the run */
        while (range_end < count && PAL_HandleValueGetChunkIndex(handles[range_end]) == chunk_index)
            range_end++;

        /* there's a range [range_beg, range_end) of values with the same chunk */
        delete_count = range_end - range_beg;

        /* use the appropriate delete function to minimize data movement */
        if (delete_count == PAL_HANDLE_CHUNK_CAPACITY)
        {   /* the entire chunk is allocated and is being deleted */
            PAL_HandleTableDeleteIdsInChunk_Full(table, chunk_index, chunk_state, chunk_dense);
        }
        else if (delete_count == chunk_count)
        {   /* the entire chunk is not allocated, but all allocated items are being deleted */
            PAL_HandleTableDeleteIdsInChunk_All (table, handles, range_beg, range_end, chunk_index, chunk_state, chunk_dense);
        }
        else if (delete_count == 1)
        {   /* only a single item is being deleted from the chunk */
            PAL_HandleTableDeleteIdsInChunk_One (table, handles, range_beg, chunk_index, chunk_count, chunk_state, chunk_dense, &view);
        }
        else
        {   /* multiple items are being deleted from the chunk, the chunk remains partially used */
            PAL_HandleTableDeleteIdsInChunk_Many(table, handles, range_beg, range_end, chunk_index, chunk_count, chunk_state, chunk_dense, &view);
        }

        if (range_end < count)
        {   /* load state for the next chunk */
            chunk_index = PAL_HandleValueGetChunkIndex(handles[range_end]);
            chunk_datap = PAL_HandleTableGetChunkData (table, chunk_index);
            chunk_state = PAL_HandleTableGetChunkState(table, chunk_index);
            chunk_dense = PAL_HandleTableGetChunkDense(table, chunk_index);
            chunk_count = PAL_HandleTableGetChunkItemCount(table, chunk_index);
            range_beg   = range_end;
            range_end   = range_end + 1;
            PAL_MemoryViewInit(&view, chunk_layout, chunk_datap, PAL_HANDLE_CHUNK_CAPACITY, chunk_count);
        }
    }
}

PAL_API(int)
PAL_HandleTableInsertIds
(
    struct PAL_HANDLE_TABLE *table, 
    PAL_HANDLE            *handles, 
    pal_usize_t              count
)
{
    PAL_MEMORY_LAYOUT *chunk_layout = PAL_HandleTableGetDataLayout(table);
    pal_uint8_t        *chunk_datap;
    pal_uint32_t       *chunk_state;
    pal_uint32_t       *chunk_dense;
    pal_uint32_t        chunk_index;
    pal_uint16_t        chunk_count;
    pal_usize_t           range_beg;
    pal_usize_t           range_end;
    PAL_MEMORY_VIEW            view;

    if (count == 0)
    {   /* nothing to do */
        return 0;
    }

    /* load state for the first chunk */
    chunk_index = PAL_HandleValueGetChunkIndex(handles[0]);
    chunk_datap = PAL_HandleTableGetChunkData (table, chunk_index);
    chunk_state = PAL_HandleTableGetChunkState(table, chunk_index);
    chunk_dense = PAL_HandleTableGetChunkDense(table, chunk_index);
    chunk_count = PAL_HandleTableGetChunkItemCount(table, chunk_index);
    range_beg   = 0;
    range_end   = 0;
    PAL_MemoryViewInit(&view, chunk_layout, chunk_datap, PAL_HANDLE_CHUNK_CAPACITY, chunk_count);

    while (range_end < count)
    {   /* find the end of the run */
        while (range_end < count && PAL_HandleValueGetChunkIndex(handles[range_end]) == chunk_index)
            range_end++;

        /* there's a range [range_beg, range_end) of values with the same chunk */
        /* TODO: should return an error if chunk commit failed */
        PAL_HandleTableInsertIdsInChunk(table, handles, range_beg, range_end, chunk_index, chunk_count, chunk_state, chunk_dense);

        if (range_end < count)
        {   /* load state for the next chunk */
            chunk_index = PAL_HandleValueGetChunkIndex(handles[range_end]);
            chunk_datap = PAL_HandleTableGetChunkData (table, chunk_index);
            chunk_state = PAL_HandleTableGetChunkState(table, chunk_index);
            chunk_dense = PAL_HandleTableGetChunkDense(table, chunk_index);
            chunk_count = PAL_HandleTableGetChunkItemCount(table, chunk_index);
            range_beg   = range_end;
            range_end   = range_end + 1;
            PAL_MemoryViewInit(&view, chunk_layout, chunk_datap, PAL_HANDLE_CHUNK_CAPACITY, chunk_count);
        }
    }
    return 0;
}

PAL_API(void)
PAL_HandleTableRemoveIds
(
    struct PAL_HANDLE_TABLE *table, 
    PAL_HANDLE            *handles, 
    pal_usize_t              count
)
{
    PAL_MEMORY_LAYOUT *chunk_layout = PAL_HandleTableGetDataLayout(table);
    pal_uint8_t        *chunk_datap;
    pal_uint32_t       *chunk_state;
    pal_uint32_t       *chunk_dense;
    pal_uint32_t        chunk_index;
    pal_uint16_t        chunk_count;
    pal_usize_t        remove_count;
    pal_usize_t           range_beg;
    pal_usize_t           range_end;
    PAL_MEMORY_VIEW            view;

    if (count == 0)
    {   /* nothing to do */
        return;
    }

    /* load state for the first chunk */
    chunk_index = PAL_HandleValueGetChunkIndex(handles[0]);
    chunk_datap = PAL_HandleTableGetChunkData (table, chunk_index);
    chunk_state = PAL_HandleTableGetChunkState(table, chunk_index);
    chunk_dense = PAL_HandleTableGetChunkDense(table, chunk_index);
    chunk_count = PAL_HandleTableGetChunkItemCount(table, chunk_index);
    range_beg   = 0;
    range_end   = 0;
    PAL_MemoryViewInit(&view, chunk_layout, chunk_datap, PAL_HANDLE_CHUNK_CAPACITY, chunk_count);

    while (range_end < count)
    {   /* find the end of the run */
        while (range_end < count && PAL_HandleValueGetChunkIndex(handles[range_end]) == chunk_index)
            range_end++;

        /* there's a range [range_beg, range_end) of values with the same chunk */
        remove_count = range_end - range_beg;

        /* use the appropriate remove function to minimize data movement */
        if (remove_count == PAL_HANDLE_CHUNK_CAPACITY)
        {   /* the entire chunk is allocated and is being deleted */
            PAL_HandleTableRemoveIdsInChunk_Full(table, chunk_index, chunk_state);
        }
        else if (remove_count == chunk_count)
        {   /* the entire chunk is not allocated, but all allocated items are being deleted */
            PAL_HandleTableRemoveIdsInChunk_All (table, handles, range_beg, range_end, chunk_index, chunk_state);
        }
        else if (remove_count == 1)
        {   /* only a single item is being removed from the chunk */
            PAL_HandleTableRemoveIdsInChunk_One (table, handles, range_beg, chunk_index, chunk_count, chunk_state, chunk_dense, &view);
        }
        else
        {   /* multiple items are being removed from the chunk, the chunk remains partially used */
            PAL_HandleTableRemoveIdsInChunk_Many(table, handles, range_beg, range_end, chunk_index, chunk_count, chunk_state, chunk_dense, &view);
        }

        if (range_end < count)
        {   /* load state for the next chunk */
            chunk_index = PAL_HandleValueGetChunkIndex(handles[range_end]);
            chunk_datap = PAL_HandleTableGetChunkData (table, chunk_index);
            chunk_state = PAL_HandleTableGetChunkState(table, chunk_index);
            chunk_dense = PAL_HandleTableGetChunkDense(table, chunk_index);
            chunk_count = PAL_HandleTableGetChunkItemCount(table, chunk_index);
            range_beg   = range_end;
            range_end   = range_end + 1;
            PAL_MemoryViewInit(&view, chunk_layout, chunk_datap, PAL_HANDLE_CHUNK_CAPACITY, chunk_count);
        }
    }
}

PAL_API(struct PAL_HANDLE_TABLE_CHUNK*)
PAL_HandleTableGetChunkForIndex
(
    struct PAL_HANDLE_TABLE       *table, 
    struct PAL_HANDLE_TABLE_CHUNK *chunk, 
    pal_uint32_t                   index, 
    struct PAL_MEMORY_VIEW         *view
)
{
    chunk->IdList = PAL_HandleTableGetChunkDense(table, index);
    chunk->Data   = PAL_HandleTableGetChunkData (table, index);
    chunk->Index  = index;
    chunk->Count  = PAL_HandleTableGetChunkItemCount(table, index);
    PAL_MemoryViewInit(view, PAL_HandleTableGetDataLayout(table), chunk->Data, PAL_HANDLE_CHUNK_CAPACITY, chunk->Count);
    return chunk;
}

PAL_API(struct PAL_HANDLE_TABLE_CHUNK*)
PAL_HandleTableGetChunkForHandle
(
    struct PAL_HANDLE_TABLE       *table, 
    struct PAL_HANDLE_TABLE_CHUNK *chunk, 
    PAL_HANDLE                    handle, 
    pal_uint32_t                  *index, 
    struct PAL_MEMORY_VIEW         *view
)
{
    PAL_MEMORY_LAYOUT *layout = PAL_HandleTableGetDataLayout (table);
    pal_uint32_t  chunk_index = PAL_HandleValueGetChunkIndex(handle);
    pal_uint32_t  state_index = PAL_HandleValueGetStateIndex(handle);
    pal_uint32_t   handle_gen = PAL_HandleValueGetGeneration(handle);
    pal_uint8_t         *data = PAL_HandleTableGetChunkData (table, chunk_index);
    pal_uint32_t       *state = PAL_HandleTableGetChunkState(table, chunk_index);
    pal_uint32_t       *dense = PAL_HandleTableGetChunkState(table, chunk_index);
    pal_uint32_t  state_value = state[state_index];
    pal_uint32_t   state_live = PAL_HandleStateGetLive(state_value);
    pal_uint32_t    state_gen = PAL_HandleStateGetGeneration(state_value);
    pal_uint32_t  dense_index = PAL_HandleStateGetDenseIndex(state_value);
    pal_uint16_t  chunk_count = PAL_HandleTableGetChunkItemCount(table, chunk_index);

    if (state_live && state_gen == handle_gen)
    {   /* the handle is valid */
        chunk->IdList = dense;
        chunk->Data   = data;
        chunk->Index  = chunk_index;
        chunk->Count  = chunk_count;
        PAL_Assign(index, dense_index);
        PAL_MemoryViewInit(view, layout, data, PAL_HANDLE_CHUNK_CAPACITY, chunk_count);
        return chunk;
    }
    return NULL;
}
