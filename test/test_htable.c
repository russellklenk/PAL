/**
 * test_htable.c: Implements test routines for the PAL_HANDLE_TABLE and related 
 * functionality. This file contains tests for both correct functionality 
 * and also for performance measurement.
 */
#include <stdio.h>
#include <Windows.h>

#include "pal_time.h"
#include "pal_memory.h"

#define STREAM_TYPE1 0
#define STREAM_TYPE2 1
#define TABLE_TYPE   3

typedef struct DATA_TYPE1 {
    pal_uint32_t Field;
} DATA_TYPE1;

typedef struct DATA_TYPE2 {
    char const  *Name;
    char         Blob[200];
} DATA_TYPE2;

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

static void
PrintCOMMIT
(
    struct PAL_HANDLE_TABLE *table
)
{
    pal_uint64_t word;
    pal_uint32_t  rem;
    pal_uint32_t  bit;
    pal_uint32_t i, n;

    printf("COMMIT   : ");
    for (i = 0, rem = table->CommitCount, n = (table->CommitCount+1) / PAL_HANDLE_CHUNK_WORD_BITS; i < n; ++i)
    {
        word = table->ChunkCommit[i];
        if (word == PAL_HANDLE_CHUNK_WORD_ALL_SET)
        {   /* all bits are set - don't print all 64 of them */
            printf("64x1");
        }
        else if (word == 0ULL)
        {   /* no bits are set - don't print all 64 of them */
            printf("64x0");
        }
        else
        {   /* some bits are set, some are clear */
            if (rem < 8)
            {   /* print at least one byte */
                rem = 8;
            }
            for (bit = 0; bit < PAL_HANDLE_CHUNK_WORD_BITS && rem > 0; ++bit, --rem)
            {
                printf("%I64u", word & 1);
                word >>= 1;
            }
        }
        if (i != (n-1)) printf("|");
    }
    printf("\r\n");
}

static void
PrintSTATUS
(
    struct PAL_HANDLE_TABLE *table
)
{
    pal_uint64_t word;
    pal_uint32_t  rem;
    pal_uint32_t  bit;
    pal_uint32_t i, n;

    printf("STATUS   : ");
    for (i = 0, rem = table->CommitCount, n = (table->CommitCount+1) / PAL_HANDLE_CHUNK_WORD_BITS; i < n; ++i)
    {
        word = table->ChunkStatus[i];
        if (word == PAL_HANDLE_CHUNK_WORD_ALL_SET)
        {   /* all bits are set - don't print all 64 of them */
            printf("64x1");
        }
        else if (word == 0ULL)
        {   /* no bits are set - don't print all 64 of them */
            printf("64x0");
        }
        else
        {   /* some bits are set, some are clear */
            if (rem < 8)
            {   /* print at least one byte */
                rem = 8;
            }
            for (bit = 0; bit < PAL_HANDLE_CHUNK_WORD_BITS && rem > 0; ++bit, --rem)
            {
                printf("%I64u", word & 1);
                word >>= 1;
            }
        }
        if (i != (n-1)) printf("|");
    }
    printf("\r\n");
}

static void
PrintCHUNK
(
    struct PAL_HANDLE_TABLE *table, 
    pal_uint32_t             index
)
{
    pal_uint8_t   *data = PAL_HandleTableGetChunkData (table, index);
    pal_uint32_t *state = PAL_HandleTableGetChunkState(table, index);
    pal_uint32_t *dense = PAL_HandleTableGetChunkDense(table, index);
    pal_uint32_t  count = PAL_HandleTableGetChunkItemCount(table, index);
    pal_uint32_t      i;

    printf("CHUNK%04u: %04u/%04u @ %p\r\n", index, count, PAL_HANDLE_CHUNK_CAPACITY, data);
    if (count > 0)
    {
        for (i = 0; i < count; ++i)
        {
            pal_uint32_t h = dense[i];
            pal_uint32_t s = state[PAL_HandleValueGetStateIndex(h)];
            printf("  S%04u: %01u/%03u/%04u/%04u/%02u (L/N/C/D/G)\r\n", i, PAL_HandleStateGetLive(s), 0                              , 0                               , PAL_HandleStateGetDenseIndex(s), PAL_HandleStateGetGeneration(s));
            printf("  D%04u: %01u/%03u/%04u/%04u/%02u (V/N/C/S/G)\r\n", i, PAL_HandleValueGetLive(h), PAL_HandleValueGetNamespace(h), PAL_HandleValueGetChunkIndex(h), PAL_HandleValueGetStateIndex(h), PAL_HandleValueGetGeneration(h));
        }
        printf("\r\n");
    }
}

static void
PrintTABLE
(
    struct PAL_HANDLE_TABLE *table
)
{
    pal_uint64_t  word;
    pal_uint32_t  mask;
    pal_uint32_t   bit;
    pal_uint32_t     i;

    PrintCOMMIT(table);
    PrintSTATUS(table);
    for (i = 0; i < PAL_HANDLE_CHUNK_WORD_COUNT; ++i)
    {
        if ((word = table->ChunkCommit[i]) != 0)
        {
            for (bit = 0, mask = 1; bit < PAL_HANDLE_CHUNK_WORD_BITS; ++bit)
            {
                if (word & mask)
                {
                    PrintCHUNK(table, (i << PAL_HANDLE_CHUNK_WORD_SHIFT) + bit);
                }
                mask <<= 1;
            }
        }
    }
    printf("\r\n");
}

static int
VerifyTABLE
(
    struct PAL_HANDLE_TABLE *table
)
{
    pal_uint32_t    *state;
    pal_uint32_t    *dense;
    pal_uint32_t word, bit;
    pal_uint32_t   i, j, n;

    for (i = 0; i < 1024; ++i)
    {
        n    = PAL_HandleTableGetChunkItemCount(table, i);
        word = i >> PAL_HANDLE_CHUNK_WORD_SHIFT;
        bit  = i  & PAL_HANDLE_CHUNK_WORD_MASK;
        if (table->ChunkCommit[word] & (1ULL << bit))
        {   /* this chunk is committed - validate it */
            state = PAL_HandleTableGetChunkState(table, i);
            dense = PAL_HandleTableGetChunkDense(table, i);
            /* validate the live portion of the chunk.
             * the live portion is densely packed, items [0, n).
             * dense[j] contains the handle value.
             * the handle value contains the state value index.
             * the state value should point back at the dense value index.
             */
            for (j = 0; j < n; ++j)
            {
                pal_uint32_t h = dense[j];
                pal_uint32_t s = state[PAL_HandleValueGetStateIndex(h)];
                if (PAL_HandleValueGetLive(h) == 0 || PAL_HandleStateGetLive(s) == 0)
                {   /* the handle and the state should both indicate they're live */
                    assert(PAL_HandleValueGetLive(h) != 0);
                    assert(PAL_HandleStateGetLive(s) != 0);
                    return 0;
                }
                if (PAL_HandleValueGetGeneration(h) != PAL_HandleStateGetGeneration(s))
                {   /* the handle and state generation should match */
                    assert(PAL_HandleValueGetGeneration(h) == PAL_HandleStateGetGeneration(s));
                    return 0;
                }
                if (PAL_HandleStateGetDenseIndex(s) != j)
                {   /* the state value should point to the dense slot */
                    assert(PAL_HandleStateGetDenseIndex(s) == j);
                    return 0;
                }
            }
            /* validate the inactive portion of the chunk.
             * items [n, 1024) in dense contain a list of free state values.
             */
            for (j = n; j < 1024; ++j)
            {
                pal_uint32_t h = dense[j];
                pal_uint32_t s = state[h];
                if (PAL_HandleValueGetLive(h) != 0 || PAL_HandleStateGetLive(s) != 0)
                {   /* the handle and the state should both say they're dead */
                    assert(PAL_HandleValueGetLive(h) == 0);
                    assert(PAL_HandleStateGetLive(s) == 0);
                    return 0;
                }
            }
            /* validate the status bit for the chunk - clear means full */
            if (table->ChunkStatus[word] & (1ULL << bit))
            {   /* the bit is set - the chunk count had better be < 1024 */
                if (n >= 1024)
                {   assert(PAL_HandleTableGetChunkItemCount(table, i) < 1024);
                    return 0;
                }
            }
            else
            {   /* the bit is clear - the chunk count had better be 1024 */
                if (n != 1024)
                {   assert(PAL_HandleTableGetChunkItemCount(table, i) == 1024);
                    return 0;
                }
            }
        }
    }
    return 1;
}

static int
FTest_Generation
(
    struct PAL_HANDLE_TABLE *table
)
{   /* this test creates and deletes a handle 16 times in the same slot.
     * it ensures that the generation portion of the handle is set correctly, 
     * and also that the generation value wraps around 0..15 back to 0.
     */
    PAL_HANDLE   id;
    pal_uint32_t  i;

    PAL_HandleTableReset(table);
    for (i = 0; i < 16; ++i)
    {
        PAL_HandleTableCreateIds(table, &id, 1);
        if (PAL_HandleValueGetGeneration(id) != (i & 15))
        {   assert(PAL_HandleValueGetGeneration(id) == (i & 15));
            return 0;
        }
        PAL_HandleTableDeleteIds(table, &id, 1);
    }
    return VerifyTABLE(table);
}

static int
FTest_AllocateFullChunk1x1
(
    struct PAL_HANDLE_TABLE *table
)
{   /* this test allocates all handles from the first chunk, one at a time, 
     * and then deletes them, one at a time.
     * it validates that the count is correct (= 1024).
     * it validates that the status bit is clear (= chunk full).
     * it validates that the handles have the components set correctly.
     */
    PAL_HANDLE  ids[1024];
    pal_uint32_t    count;
    pal_uint64_t   status;
    pal_uint32_t        i;

    PAL_HandleTableReset(table);
    for (i = 0; i < 1024; ++i)
    {
        if (PAL_HandleTableCreateIds(table, &ids[i], 1) != 0)
        {   assert(0 && "PAL_HandleTableCreateIds(1) failed");
            return 0;
        }
        if ((count = PAL_HandleTableGetChunkItemCount(table, 0)) != (i + 1))
        {   assert(count == (i + 1) && "Chunk count not correct");
            return 0;
        }
        status = table->ChunkStatus[0];
        if (i != 1023 && status != 1)
        {   assert(status == 1 && "Chunk status not correct (1)");
            return 0;
        }
        if (i == 1023 && status != 0)
        {   assert(status == 0 && "Chunk status not correct (0)");
            return 0;
        }
        if (PAL_HandleValueGetLive(ids[i]) != 1)
        {   assert(PAL_HandleValueGetLive(ids[i]) == 1);
            return 0;
        }
        if (PAL_HandleValueGetNamespace(ids[i]) != TABLE_TYPE)
        {   assert(PAL_HandleValueGetNamespace(ids[i]) == TABLE_TYPE);
            return 0;
        }
        if (PAL_HandleValueGetChunkIndex(ids[i]) != 0)
        {   assert(PAL_HandleValueGetChunkIndex(ids[i]) == 0);
            return 0;
        }
    }
    (void) PAL_HandleTableValidateIds(table, ids, 1024);
    for (i = 0; i < 1024; ++i)
    {
        PAL_HandleTableDeleteIds(table, &ids[i], 1);
        if ((count = PAL_HandleTableGetChunkItemCount(table, 0)) != (1024 - i - 1))
        {   assert(count == (1024 - i - 1) && "Chunk count not correct");
            return 0;
        }
        if ((status = table->ChunkStatus[0]) != 1)
        {   assert(status == 1 && "Chunk status not correct");
            return 0;
        }
    }
    if ((count = PAL_HandleTableGetChunkItemCount(table, 0)) != 0)
    {   assert(count == 0 && "Chunk count not correct (end)");
        return 0;
    }
    if ((status = table->ChunkStatus[0]) != 1)
    {   assert(status == 1 && "Chunk status not correct (end)");
        return 0;
    }
    return VerifyTABLE(table);
}

static int
FTest_AllocateFullChunk
(
    struct PAL_HANDLE_TABLE *table
)
{   /* this test allocates all handles from the first chunk.
     * it validates that the count is correct (= 1024).
     * it validates that the status bit is clear (= chunk full).
     * it validates that the handles have the components set correctly.
     */
    PAL_HANDLE       ids[1024];
    pal_uint32_t         count;
    pal_uint64_t        status;
    pal_uint32_t             i;

    PAL_HandleTableReset(table);
    if (PAL_HandleTableCreateIds(table, ids, 1024) != 0)
    {   assert(0 && "PAL_HandleTableCreateIds(1024) failed");
        return 0;
    }
    if ((count = PAL_HandleTableGetChunkItemCount(table, 0)) != 1024)
    {   assert(count == 1024 && "Chunk count should be 1024");
        return 0;
    }
    if ((status = table->ChunkStatus[0]) != 0)
    {   assert(status == 0 && "Chunk status bit should be clear (full)");
        return 0;
    }
    for (i = 0; i < 1024; ++i)
    {
        if (PAL_HandleValueGetLive(ids[i]) != 1)
        {   assert(PAL_HandleValueGetLive(ids[i]) == 1);
            return 0;
        }
        if (PAL_HandleValueGetNamespace(ids[i]) != TABLE_TYPE)
        {   assert(PAL_HandleValueGetNamespace(ids[i]) == TABLE_TYPE);
            return 0;
        }
        if (PAL_HandleValueGetChunkIndex(ids[i]) != 0)
        {   assert(PAL_HandleValueGetChunkIndex(ids[i]) == 0);
            return 0;
        }
    }
    PAL_HandleTableValidateIds(table, ids, 1024);
    PAL_HandleTableDeleteIds(table, ids, 1024);
    return 1;
}

static int
FTest_AllocateEntireRange
(
    struct PAL_HANDLE_TABLE *table
)
{
    PAL_HANDLE ids[1024];
    pal_uint64_t  commit;
    pal_uint64_t  status;
    pal_uint32_t   count;
    pal_uint32_t    mask;
    pal_uint32_t     bit;
    pal_uint32_t       i;

    PAL_HandleTableReset(table);
    for (i = 0; i < 1024; ++i)
    {
        bit    = i & PAL_HANDLE_CHUNK_WORD_MASK;
        mask   = 1UL << bit;
        status = table->ChunkStatus[i >> PAL_HANDLE_CHUNK_WORD_SHIFT];
        commit = table->ChunkCommit[i >> PAL_HANDLE_CHUNK_WORD_SHIFT];
        if ((count = PAL_HandleTableGetChunkItemCount(table, i)) != 0)
        {   assert(count == 0 && "Chunk should be empty");
            return 0;
        }
        if (PAL_HandleTableCreateIds(table, ids, 1024) != 0)
        {   assert(0 && "PAL_HandleTableCreateIds failed");
            return 0;
        }
        PAL_HandleTableValidateIds(table, ids, 1024);
        status = table->ChunkStatus[i >> PAL_HANDLE_CHUNK_WORD_SHIFT];
        commit = table->ChunkCommit[i >> PAL_HANDLE_CHUNK_WORD_SHIFT];
        if ((commit & mask) == 0)
        {   assert((commit & mask) != 0 && "Chunk should be committed");
            return 0;
        }
        if ((status & mask) != 0)
        {   assert((status & mask) == 0 && "Chunk should not be available");
            return 0;
        }
        if ((count = PAL_HandleTableGetChunkItemCount(table, i)) != 1024)
        {   assert(count == 1024 && "Chunk should be full");
            return 0;
        }
    }
    return VerifyTABLE(table);
}

static int
FTest_FullStateValidation
(
    struct PAL_HANDLE_TABLE *table
)
{   /* this test allocates handles, one by one, validating the table after each allocation.
     * it then deletes each handle, one at a time, and validates the table after each deletion.
     */
    PAL_HANDLE *ids = (PAL_HANDLE*) malloc(PAL_HANDLE_TABLE_MAX_OBJECT_COUNT * sizeof(PAL_HANDLE)); /* 4MB */
    pal_uint32_t  n = 65; /* the number of chunks you want to validate */
    pal_uint32_t  i;

    PAL_HandleTableReset(table);
    /* allocate one at a time */
    for (i = 0; i < n; ++i)
    {   /* allocate a single ID and verify the table state */
        if (PAL_HandleTableCreateIds(table, &ids[i], 1) != 0)
        {   assert(0 && "PAL_HandleTableCreateIds(1) failed");
            free(ids);
            return 0;
        }
        VerifyTABLE(table);
    }
    /* delete even items */
    for (i = 0; i < n; ++i)
    {
        if ((i & 1) == 0)
        {
            PAL_HandleTableDeleteIds(table, &ids[i], 1);
            VerifyTABLE(table);
        }
    }
    /* delete odd items */
    for (i = 0; i < n; ++i)
    {
        if ((i & 1) == 1)
        {
            PAL_HandleTableDeleteIds(table, &ids[i], 1);
            VerifyTABLE(table);
        }
    }
    free(ids);
    return 1;
}

static void
PTest_CreateFullChunk
(
    struct PAL_HANDLE_TABLE *table
)
{
    PAL_HANDLE  ids[1024];
    pal_uint64_t   ns_min = ~0ULL;
    pal_uint64_t   ns_max =  0ULL;
    pal_uint64_t   ns_sum =  0ULL;
    pal_uint64_t   ns_cur;
    pal_uint64_t      tss;
    pal_uint64_t      tse;
    pal_uint32_t        i;

    PAL_HandleTableReset(table);
    for (i = 0; i < 10000; ++i)
    {
        tss    = PAL_TimestampInTicks();
        PAL_HandleTableCreateIds(table, ids, 1024);
        tse    = PAL_TimestampInTicks();
        ns_cur = PAL_TimestampDeltaNanoseconds(tss, tse);
        PAL_HandleTableDeleteIds(table, ids, 1024);
        if (ns_cur < ns_min) ns_min = ns_cur;
        if (ns_cur > ns_max) ns_max = ns_cur;
        ns_sum += ns_cur;
    }
    printf("PTest_CreateFullChunk     : %I64uns %I64uns %I64uns\r\n", ns_min, ns_max, ns_sum / 10000);
}

static void
PTest_LargeAllocation
(
    struct PAL_HANDLE_TABLE *table
)
{
    PAL_HANDLE       *big = (PAL_HANDLE*) malloc(20000 * sizeof(PAL_HANDLE));
    PAL_HANDLE  mid1[300];
    PAL_HANDLE  mid2[400];
    PAL_HANDLE  mid3[200];
    pal_uint64_t      tss;
    pal_uint64_t      tse;
    pal_uint64_t   ns_min = ~0ULL;
    pal_uint64_t   ns_max =  0ULL;
    pal_uint64_t   ns_sum =  0ULL;
    pal_uint64_t   ns_cur =  0ULL;
    pal_uint32_t        i =  0;

    PAL_HandleTableReset(table);
    for (i = 0; i < 10000; ++i)
    {
        PAL_HandleTableCreateIds(table, mid1, 300);
        //VerifyTABLE(table);
        tss    = PAL_TimestampInTicks();
        PAL_HandleTableCreateIds(table, big , 20000);
        tse    = PAL_TimestampInTicks();
        ns_cur = PAL_TimestampDeltaNanoseconds(tss, tse);
        //VerifyTABLE(table);
        PAL_HandleTableCreateIds(table, mid2, 400);
        //VerifyTABLE(table);
        PAL_HandleTableCreateIds(table, mid3, 200);
        //VerifyTABLE(table);
        
        PAL_HandleTableValidateIds(table, mid1, 300);
        PAL_HandleTableValidateIds(table, big , 20000);
        PAL_HandleTableValidateIds(table, mid2, 400);
        PAL_HandleTableValidateIds(table, mid3, 200);

        PAL_HandleTableDeleteIds(table, big , 20000);
        //VerifyTABLE(table);
        PAL_HandleTableDeleteIds(table, mid3, 200);
        //VerifyTABLE(table);
        PAL_HandleTableDeleteIds(table, mid1, 300);
        //VerifyTABLE(table);
        PAL_HandleTableDeleteIds(table, mid2, 400);
        //VerifyTABLE(table);
        if (ns_cur < ns_min) ns_min = ns_cur;
        if (ns_cur > ns_max) ns_max = ns_cur;
        ns_sum += ns_cur;
    }
    printf("PTest_LargeAllocation     : %I64uns %I64uns %I64uns\r\n", ns_min, ns_max, ns_sum / 10000);
    free(big);
}

static void
PTest_LargeDeletion
(
    struct PAL_HANDLE_TABLE *table
)
{
    PAL_HANDLE       *big = (PAL_HANDLE*) malloc(20000 * sizeof(PAL_HANDLE));
    PAL_HANDLE  mid1[300];
    PAL_HANDLE  mid2[400];
    PAL_HANDLE  mid3[200];
    pal_uint64_t      tss;
    pal_uint64_t      tse;
    pal_uint64_t   ns_min = ~0ULL;
    pal_uint64_t   ns_max =  0ULL;
    pal_uint64_t   ns_sum =  0ULL;
    pal_uint64_t   ns_cur =  0ULL;
    pal_uint32_t        i =  0;

    PAL_HandleTableReset(table);
    for (i = 0; i < 10000; ++i)
    {
        PAL_HandleTableCreateIds(table, mid1, 300);
        //VerifyTABLE(table);
        PAL_HandleTableCreateIds(table, big , 20000);
        //VerifyTABLE(table);
        PAL_HandleTableCreateIds(table, mid2, 400);
        //VerifyTABLE(table);
        PAL_HandleTableCreateIds(table, mid3, 200);
        //VerifyTABLE(table);

        PAL_HandleTableValidateIds(table, mid1, 300);
        PAL_HandleTableValidateIds(table, big , 20000);
        PAL_HandleTableValidateIds(table, mid2, 400);
        PAL_HandleTableValidateIds(table, mid3, 200);

        tss    = PAL_TimestampInTicks();
        PAL_HandleTableDeleteIds(table, big , 20000);
        tse    = PAL_TimestampInTicks();
        ns_cur = PAL_TimestampDeltaNanoseconds(tss, tse);
        //VerifyTABLE(table);
        PAL_HandleTableDeleteIds(table, mid3, 200);
        //VerifyTABLE(table);
        PAL_HandleTableDeleteIds(table, mid1, 300);
        //VerifyTABLE(table);
        PAL_HandleTableDeleteIds(table, mid2, 400);
        //VerifyTABLE(table);
        if (ns_cur < ns_min) ns_min = ns_cur;
        if (ns_cur > ns_max) ns_max = ns_cur;
        ns_sum += ns_cur;
    }
    printf("PTest_LargeDeletion       : %I64uns %I64uns %I64uns\r\n", ns_min, ns_max, ns_sum / 10000);
    free(big);
}

int main
(
    int    argc, 
    char **argv
)
{
    PAL_HANDLE_TABLE     table;
    PAL_HANDLE_TABLE_INIT init; /* Namespace, InitialCommit, TableFlags, DataLayout */
    PAL_MEMORY_LAYOUT   layout;
    int                    res;

    PAL_UNUSED_ARG(argc);
    PAL_UNUSED_ARG(argv);

    /* create the data stream layout used by this module.
     * there are two streams, tightly packed:
     * [DATA_TYPE1][DATA_TYPE1]...[DATA_TYPE1] (STREAM_TYPE1)
     * [DATA_TYPE2][DATA_TYPE2]...[DATA_TYPE2] (STREAM_TYPE2)
     */
    PAL_MemoryLayoutInit(&layout);
    PAL_MemoryLayoutAdd (&layout, DATA_TYPE1);
    PAL_MemoryLayoutAdd (&layout, DATA_TYPE2);

    /* initialize an empty PAL_HANDLE_TABLE */
    PAL_ZeroMemory(&init, sizeof(PAL_HANDLE_TABLE_INIT));
    init.Namespace      = TABLE_TYPE;
    init.InitialCommit  = 1;
    init.TableFlags     = PAL_HANDLE_TABLE_FLAG_IDENTITY | PAL_HANDLE_TABLE_FLAG_STORAGE;
    init.DataLayout     = &layout; /* copied into the PAL_HANDLE_TABLE */
    if (PAL_HandleTableCreate(&table, &init) != 0)
    {
        printf("ERROR: Failed to create the handle table.\r\n");
        return -1;
    }

    /* functional tests */
    res = FTest_Generation(&table);           printf("FTest_Generation          : %d\r\n", res);
    res = FTest_AllocateFullChunk(&table);    printf("FTest_AllocateFullChunk   : %d\r\n", res);
    res = FTest_AllocateFullChunk1x1(&table); printf("FTest_AllocateFullChunk1x1: %d\r\n", res);
    res = FTest_AllocateEntireRange(&table);  printf("FTest_AllocateEntireRange : %d\r\n", res);
    res = FTest_FullStateValidation(&table);  printf("FTest_FullStateValidation : %d\r\n", res);

    PTest_CreateFullChunk(&table);
    PTest_LargeAllocation(&table);
    PTest_LargeDeletion  (&table);

    PAL_HandleTableDelete(&table);
    return 0;
}
