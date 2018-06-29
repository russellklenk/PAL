/**
 * @summary Implement test routines for the concurrent queues in pal_thread.h.
 */
#include <process.h>
#include <stdio.h>

#include "pal_memory.h"
#include "pal_thread.h"

#include "pal_win32_memory.c"
#include "pal_win32_thread.c"

/* @summary Specify a size in kilobytes.
 * @param _kb The size in kilobytes.
 * @return The size in bytes, as a 32-bit unsigned integer value.
 */
#ifndef Kilobytes
#define Kilobytes(_kb)                                                         \
    ((_kb) * 1024UL)
#endif

/* @summary Specify a size in megabytes.
 * @param _mb The size in megabytes.
 * @return The size in bytes, as a 32-bit unsigned integer value.
 */
#ifndef Megabytes
#define Megabytes(_mb)                                                         \
    ((_mb) * 1024UL * 1024UL)
#endif

/* @summary Retrieve the application global memory arena.
 * @param _s A pointer to the structure to query, which must have a GlobalArena field.
 * @return A pointer to the PAL_MEMORY_ARENA used to manage the global memory.
 */
#ifndef APP__GlobalMemory
#define APP__GlobalMemory(_s)                                                  \
    ((_s)->GlobalArena)
#endif

/* @summary Retrieve the application scratch memory arena.
 * @param _s A pointer to the structure to query, which must have a GlobalArena field.
 * @return A pointer to the PAL_MEMORY_ARENA used to manage the scratch memory.
 */
#ifndef APP__ScratchMemory
#define APP__ScratchMemory(_s)                                                 \
    ((_s)->ScratchArena)
#endif

#ifndef TEST_PASS_FAIL
#define TEST_PASS_FAIL
#define TEST_PASS    1
#define TEST_FAIL    0
#endif

typedef struct MPSC_STEAL_U16 {
    pal_uint32_t          LastWritePos;      /* The index of the last-written value. Updated by each producer. */
    pal_uint32_t          PublishCount;      /* */
    pal_uint32_t          ConsumeCount;      /* */
    pal_uint16_t          Data[32];          /* Storage. The oldest items are overwritten. Must be initialized with valid data. */
} MPSC_STEAL_U16;

typedef struct SPSC_QUEUE_DATA {
    PAL_MEMORY_ARENA     *GlobalArena;
    PAL_MEMORY_ARENA     *ScratchArena;
    PAL_SPSC_QUEUE_U32   *SharedQueue;
    pal_uint32_t volatile DrainSignal;       /* Manual-reset event to be signaled by the pusher thread when it has finished all iterations */
    pal_uint32_t          IterationCount;
    pal_uint32_t          OpsPerIteration;
    pal_uint32_t        **CountsData;        /* An array of pointers to arrays of 32-bit unsigned integer values used to count how many times an item has been seen - one array per-thread */
} SPSC_QUEUE_DATA;

typedef struct SPMC_QUEUE_DATA {
    PAL_MEMORY_ARENA     *GlobalArena;
    PAL_MEMORY_ARENA     *ScratchArena;
    PAL_SPMC_QUEUE_U32   *SharedQueue;
    pal_uint32_t volatile DrainSignal;       /* Manual-reset event to be signaled by the pusher thread when it has finished all iterations */
    pal_uint32_t          IterationCount;
    pal_uint32_t          OpsPerIteration;
    pal_uint32_t        **CountsData;        /* An array of pointers to arrays of 32-bit unsigned integer values used to count how many times an item has been seen - one array per-thread */
} SPMC_QUEUE_DATA;

typedef struct MPMC_QUEUE_DATA {
    PAL_MEMORY_ARENA     *GlobalArena;
    PAL_MEMORY_ARENA     *ScratchArena;
    PAL_MPMC_QUEUE_U32   *SharedQueue;
    pal_uint32_t volatile DrainSignal;       /* Manual-reset event to be signaled by the pusher thread when it has finished all iterations */
    pal_uint32_t          IterationCount;
    pal_uint32_t          OpsPerIteration;
    pal_uint32_t        **CountsData;        /* An array of pointers to arrays of 32-bit unsigned integer values used to count how many times an item has been seen - one array per-thread */
} MPMC_QUEUE_DATA;

static pal_uint32_t
ThreadMain_SPSCQueuePusher
(
    struct PAL_THREAD_INIT *init
)
{
    SPSC_QUEUE_DATA *context =(SPSC_QUEUE_DATA *) PAL_ThreadPoolGetPoolContext(init->ThreadPool);
    PAL_SPSC_QUEUE_U32 *spsc = context->SharedQueue;
    pal_uint32_t     *counts = context->CountsData[init->ThreadIndex];
    pal_uint32_t   exit_code = 0;
    pal_uint32_t        i, n;
    pal_uint32_t        j, m;

    /* the thread should perform any initialization here.
     * call PAL_ThreadSetName to give a name for the debugger.
     */
    PAL_ThreadSetName("SPSCQueuePusher");
    PAL_ZeroMemory(counts, context->OpsPerIteration * sizeof(pal_uint32_t));
    context->DrainSignal = 0;

    for (i = 0, n = context->IterationCount; i < n; ++i)
    {
        for (j = 0, m = context->OpsPerIteration; j < m; ++j)
        {
            if (PAL_SPSCQueuePush_u32(spsc, j) == 0)
            {   assert(0 && "PAL_SPSCQueuePush_u32 failed");
                goto finish;
            }
        }
    }

finish:
    context->DrainSignal = 1;
    return exit_code;
}

static pal_uint32_t
ThreadMain_SPSCQueueTaker
(
    struct PAL_THREAD_INIT *init
)
{
    SPSC_QUEUE_DATA *context =(SPSC_QUEUE_DATA *) PAL_ThreadPoolGetPoolContext(init->ThreadPool);
    PAL_SPSC_QUEUE_U32 *spsc = context->SharedQueue;
    pal_uint32_t     *counts = context->CountsData[init->ThreadIndex];
    pal_uint32_t   exit_code = 0;
    pal_uint32_t        item;

    /* the thread should perform any initialization here.
     * call PAL_ThreadSetName to give a name for the debugger.
     */
    PAL_ThreadSetName("SPSCQueueTaker");
    PAL_ZeroMemory(counts, context->OpsPerIteration * sizeof(pal_uint32_t));
    context->DrainSignal = 0;

    for ( ; ; )
    {
        if (PAL_SPSCQueueTake_u32(spsc, &item))
        {
            counts[item]++;
        }
        else
        {   /* the queue is empty - has the pusher finished? */
            if (context->DrainSignal)
            {   /* the queue has drained, we're done */
                break;
            }
        }
    }
    return exit_code;
}

static pal_uint32_t
ThreadMain_SPMCQueuePusher
(
    struct PAL_THREAD_INIT *init
)
{
    SPMC_QUEUE_DATA *context =(SPMC_QUEUE_DATA *) PAL_ThreadPoolGetPoolContext(init->ThreadPool);
    PAL_SPMC_QUEUE_U32 *spmc = context->SharedQueue;
    pal_uint32_t     *counts = context->CountsData[init->ThreadIndex];
    pal_uint32_t   exit_code = 0;
    pal_uint32_t        i, n;
    pal_uint32_t        j, m;

    /* the thread should perform any initialization here.
     * call PAL_ThreadSetName to give a name for the debugger.
     */
    PAL_ThreadSetName("SPMCQueuePusher");
    PAL_ZeroMemory(counts, context->OpsPerIteration * sizeof(pal_uint32_t));
    context->DrainSignal = 0;

    for (i = 0, n = context->IterationCount; i < n; ++i)
    {
        for (j = 0, m = context->OpsPerIteration; j < m; /* */)
        {
            if (PAL_SPMCQueuePush_u32(spmc, j++) == 0)
            {   assert(0 && "PAL_SPMCQueuePush_u32 failed");
                goto finish;
            }
            if (PAL_SPMCQueuePush_u32(spmc, j++) == 0)
            {   assert(0 && "PAL_SPMCQueuePush_u32 failed");
                goto finish;
            }
        }
    }

finish:
    context->DrainSignal = 1;
    return exit_code;
}

static pal_uint32_t
ThreadMain_SPMCQueuePushTaker
(
    struct PAL_THREAD_INIT *init
)
{
    SPMC_QUEUE_DATA *context =(SPMC_QUEUE_DATA *) PAL_ThreadPoolGetPoolContext(init->ThreadPool);
    PAL_SPMC_QUEUE_U32 *spmc = context->SharedQueue;
    pal_uint32_t     *counts = context->CountsData[init->ThreadIndex];
    pal_uint32_t   exit_code = 0;
    pal_uint32_t        item;
    pal_uint32_t        i, n;
    pal_uint32_t        j, m;

    /* the thread should perform any initialization here.
     * call PAL_ThreadSetName to give a name for the debugger.
     */
    PAL_ThreadSetName("SPMCQueuePushTaker");
    PAL_ZeroMemory(counts, context->OpsPerIteration * sizeof(pal_uint32_t));
    context->DrainSignal = 0;

    for (i = 0, n = context->IterationCount; i < n; ++i)
    {
        for (j = 0, m = context->OpsPerIteration; j < m; /* */)
        {
            if (PAL_SPMCQueuePush_u32(spmc, j++) == 0)
            {   assert(0 && "PAL_SPMCQueuePush_u32 failed");
                goto finish;
            }
            if (PAL_SPMCQueuePush_u32(spmc, j++) == 0)
            {   assert(0 && "PAL_SPMCQueuePush_u32 failed");
                goto finish;
            }
        }
        for (j = 0, m = context->OpsPerIteration; j < m; ++j)
        {   /* take one - this may fail due to concurrent steals making the queue empty */
            if (PAL_SPMCQueueTake_u32(spmc, &item))
            {
                counts[item]++;
            }
        }
    }

finish:
    context->DrainSignal = 1;
    return exit_code;
}

static pal_uint32_t
ThreadMain_SPMCQueueStealer
(
    struct PAL_THREAD_INIT *init
)
{
    SPMC_QUEUE_DATA *context =(SPMC_QUEUE_DATA *) PAL_ThreadPoolGetPoolContext(init->ThreadPool);
    PAL_SPMC_QUEUE_U32 *spmc = context->SharedQueue;
    pal_uint32_t     *counts = context->CountsData[init->ThreadIndex];
    pal_uint32_t   exit_code = 0;
    pal_uint32_t        item;

    /* the thread should perform any initialization here.
     * call PAL_ThreadSetName to give a name for the debugger.
     */
    PAL_ThreadSetName("SPMCQueueStealer");
    PAL_ZeroMemory(counts, context->OpsPerIteration * sizeof(pal_uint32_t));

    for ( ; ; )
    {
        if (PAL_SPMCQueueSteal_u32(spmc, &item))
        {
            counts[item]++;
        }
        else
        {   /* the queue is empty - has the pusher finished? */
            if (context->DrainSignal)
            {   /* the queue has drained, we're done */
                break;
            }
        }
    }
    return exit_code;
}

static pal_uint32_t
ThreadMain_MPMCQueuePusher
(
    struct PAL_THREAD_INIT *init
)
{
    MPMC_QUEUE_DATA *context =(MPMC_QUEUE_DATA *) PAL_ThreadPoolGetPoolContext(init->ThreadPool);
    PAL_MPMC_QUEUE_U32 *mpmc = context->SharedQueue;
    pal_uint32_t     *counts = context->CountsData[init->ThreadIndex];
    pal_uint32_t   exit_code = 0;
    pal_uint32_t        i, n;
    pal_uint32_t        j, m;

    /* the thread should perform any initialization here.
     * call PAL_ThreadSetName to give a name for the debugger.
     */
    PAL_ThreadSetName("MPMCQueuePusher");
    PAL_ZeroMemory(counts, context->OpsPerIteration * sizeof(pal_uint32_t));
    context->DrainSignal = 0;

    for (i = 0, n = context->IterationCount; i < n; ++i)
    {
        for (j = 0, m = context->OpsPerIteration; j < m; /* */)
        {
            if (PAL_MPMCQueuePush_u32(mpmc, j++) == 0)
            {   assert(0 && "PAL_MPMCQueuePush_u32 failed");
                goto finish;
            }
        }
    }

finish:
    context->DrainSignal = 1;
    return exit_code;
}

static pal_uint32_t
ThreadMain_MPMCQueueTaker
(
    struct PAL_THREAD_INIT *init
)
{
    MPMC_QUEUE_DATA *context =(MPMC_QUEUE_DATA *) PAL_ThreadPoolGetPoolContext(init->ThreadPool);
    PAL_MPMC_QUEUE_U32 *mpmc = context->SharedQueue;
    pal_uint32_t     *counts = context->CountsData[init->ThreadIndex];
    pal_uint32_t   exit_code = 0;
    pal_uint32_t        item;

    /* the thread should perform any initialization here.
     * call PAL_ThreadSetName to give a name for the debugger.
     */
    PAL_ThreadSetName("MPMCQueueTaker");
    PAL_ZeroMemory(counts, context->OpsPerIteration * sizeof(pal_uint32_t));

    for ( ; ; )
    {
        if (PAL_MPMCQueueTake_u32(mpmc, &item))
        {
            counts[item]++;
        }
        else
        {   /* the queue is empty - has the pusher finished? */
            if (context->DrainSignal)
            {   /* the queue has drained, we're done */
                break;
            }
        }
    }
    return exit_code;
}

static int
FTest_SPSCQueue_u32_PushTakeIsFIFO
(
    struct PAL_MEMORY_ARENA  *global_arena, 
    struct PAL_MEMORY_ARENA *scratch_arena
)
{   /* this test ensures that the SPSC concurrent queue produces items in last-in, first-out
     * order when using Push and Take operations.
     */
    pal_uint32_t const          CAPACITY = 16;
#ifdef FULL_VERIFICATION
    pal_uint32_t const         NUM_ITERS = 0xFFFFFFFFUL / CAPACITY; /* this takes considerable time */
#else
    pal_uint32_t const         NUM_ITERS = 10;
#endif
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_SPSC_QUEUE_INIT             init;
    PAL_SPSC_QUEUE_U32              spsc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, n;
    pal_uint32_t                    j, m;
    pal_uint32_t                    item;
    int                           result;

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create a queue for test purposes */
    required_size    = PAL_SPSCQueueQueryMemorySize_u32(CAPACITY);
    init.Capacity    = CAPACITY;
    init.UsageFlags  = 0;
    init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    init.MemorySize  = required_size;
    if (PAL_SPSCQueueCreate_u32(&spsc, &init) != 0)
    {   assert(0 && "PAL_SPSCQueueCreate_u32 failed");
        result = TEST_FAIL;
        goto cleanup;
    }

    for (j = 0, m = NUM_ITERS; j < m; ++j)
    {
        /* push items with values 0..15 */
        for (i = 0, n = CAPACITY; i < n; ++i)
        {
            if (PAL_SPSCQueuePush_u32(&spsc, i) == 0)
            {   assert(0 && "PAL_SPSCQueuePush_u32 failed");
                result = TEST_FAIL;
                goto cleanup;
            }
        }
        /* now take items 15..0 */
        for (i = 0, n = CAPACITY; i < n; ++i)
        {
            if (PAL_SPSCQueueTake_u32(&spsc, &item) == 0)
            {   assert(0 && "PAL_SPSCQueueTake_u32 failed");
                result = TEST_FAIL;
                goto cleanup;
            }
            if (item != i)
            {   assert(item == n && "PAL_SPSCQueueTake_u32 is not FIFO");
                result = TEST_FAIL;
                goto cleanup;
            }
        }
    }

cleanup:
    PAL_MemoryArenaResetToMarker(scratch_arena, scratch_mark);
    PAL_MemoryArenaResetToMarker(global_arena, global_mark);
    return result;
}

static int
FTest_SPSCQueue_u32_PushFailsWhenFull
(
    struct PAL_MEMORY_ARENA  *global_arena, 
    struct PAL_MEMORY_ARENA *scratch_arena
)
{   /* this test ensures that PAL_SPSCQueuePush_u32 returns 0 when the queue is full.
     */
    pal_uint32_t const          CAPACITY = 16;
#ifdef FULL_VERIFICATION
    pal_uint32_t const         NUM_ITERS = 0xFFFFFFFFUL / CAPACITY; /* this takes considerable time */
#else
    pal_uint32_t const         NUM_ITERS = 10;
#endif
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_SPSC_QUEUE_INIT             init;
    PAL_SPSC_QUEUE_U32              spsc;
    pal_usize_t            required_size;
    pal_uint32_t                    item;
    pal_uint32_t                    i, n;
    pal_uint32_t                    j, m;
    int                           result;

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create a queue for test purposes */
    required_size    = PAL_SPSCQueueQueryMemorySize_u32(CAPACITY);
    init.Capacity    = CAPACITY;
    init.UsageFlags  = 0;
    init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    init.MemorySize  = required_size;
    if (PAL_SPSCQueueCreate_u32(&spsc, &init) != 0)
    {   assert(0 && "PAL_SPSCQueueCreate_u32 failed");
        result = TEST_FAIL;
        goto cleanup;
    }

    for (j = 0, m = NUM_ITERS; j < m; ++j)
    {
        /* push items with values 0..15 */
        for (i = 0, n = CAPACITY; i < n; ++i)
        {
            if (PAL_SPSCQueuePush_u32(&spsc, i) == 0)
            {   assert(0 && "PAL_SPSCQueuePush_u32 failed");
                result = TEST_FAIL;
                goto cleanup;
            }
        }
        /* now attempt to push an item into the full queue */
        if (PAL_SPSCQueuePush_u32(&spsc, CAPACITY+1) != 0)
        {   assert(0 && "PAL_SPSCQueuePush_u32 didn't return 0 for full queue");
            result = TEST_FAIL;
            goto cleanup;
        }
        /* now take all items from the queue to drain it */
        for (i = 0, n = CAPACITY; i < n; ++i)
        {
            if (PAL_SPSCQueueTake_u32(&spsc, &item) == 0)
            {   assert(0 && "PAL_SPSCQueueTake_u32 failed");
                result = TEST_FAIL;
                goto cleanup;
            }
            if (item != i)
            {   assert(item == n && "PAL_SPSCQueueTake_u32 is not FIFO");
                result = TEST_FAIL;
                goto cleanup;
            }
        }
    }

cleanup:
    PAL_MemoryArenaResetToMarker(scratch_arena, scratch_mark);
    PAL_MemoryArenaResetToMarker(global_arena, global_mark);
    return result;
}

static int
FTest_SPSCQueue_u32_TakeFailsWhenEmpty
(
    struct PAL_MEMORY_ARENA  *global_arena, 
    struct PAL_MEMORY_ARENA *scratch_arena
)
{   /* this test ensures that PAL_SPSCQueueTake_u32 returns 0 when the queue is drained.
     */
    pal_uint32_t const          CAPACITY = 16;
#ifdef FULL_VERIFICATION
    pal_uint32_t const         NUM_ITERS = 0xFFFFFFFFUL / CAPACITY; /* this takes considerable time */
#else
    pal_uint32_t const         NUM_ITERS = 10;
#endif
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_SPSC_QUEUE_INIT             init;
    PAL_SPSC_QUEUE_U32              spsc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, n;
    pal_uint32_t                    j, m;
    pal_uint32_t                    item;
    int                           result;

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create a queue for test purposes */
    required_size    = PAL_SPSCQueueQueryMemorySize_u32(CAPACITY);
    init.Capacity    = CAPACITY;
    init.UsageFlags  = 0;
    init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    init.MemorySize  = required_size;
    if (PAL_SPSCQueueCreate_u32(&spsc, &init) != 0)
    {   assert(0 && "PAL_SPSCQueueCreate_u32 failed");
        result = TEST_FAIL;
        goto cleanup;
    }

    for (j = 0, m = NUM_ITERS; j < m; ++j)
    {
        /* push items with values 0..15 */
        for (i = 0, n = CAPACITY; i < n; ++i)
        {
            if (PAL_SPSCQueuePush_u32(&spsc, i) == 0)
            {   assert(0 && "PAL_SPSCQueuePush_u32 failed");
                result = TEST_FAIL;
                goto cleanup;
            }
        }
        /* now take items 0..15 */
        for (i = 0, n = CAPACITY; i < n; ++i)
        {
            if (PAL_SPSCQueueTake_u32(&spsc, &item) == 0)
            {   assert(0 && "PAL_SPSCQueueTake_u32 failed");
                result = TEST_FAIL;
                goto cleanup;
            }
            if (item != i)
            {   assert(item == i && "PAL_SPSCQueueTake_u32 is not FIFO");
                result = TEST_FAIL;
                goto cleanup;
            }
        }
        /* now attempt to take an item from the empty queue */
        if (PAL_SPSCQueueTake_u32(&spsc, &item) != 0)
        {   assert(0 && "PAL_SPSCQueueTake_u32 didn't return 0 for empty queue");
            result = TEST_FAIL;
            goto cleanup;
        }
    }

cleanup:
    PAL_MemoryArenaResetToMarker(scratch_arena, scratch_mark);
    PAL_MemoryArenaResetToMarker(global_arena, global_mark);
    return result;
}

static int
FTest_SPSCQueue_u32_ConcurrentPushTake
(
    struct PAL_MEMORY_ARENA  *global_arena, 
    struct PAL_MEMORY_ARENA *scratch_arena
)
{   /* this test ensures that a PAL_SPSC_QUEUE_U32 behaves correctly in the face of concurrent 
     * push operations on one thread and take operations on another thread. specifically, no 
     * items should be lost - each item that is pushed should be seen by a take.
     */
    pal_uint32_t const       NUM_THREADS = 2;
    pal_uint32_t const      OPS_PER_ITER = 4096;
    pal_uint32_t const         NUM_ITERS = 16384;
    pal_uint32_t const          CAPACITY =(pal_uint32_t) PAL_NextPow2GreaterOrEqual(OPS_PER_ITER * NUM_ITERS); /* ensure the queue never fills */
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_SPSC_QUEUE_INIT             init;
    PAL_SPSC_QUEUE_U32              spsc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, j;
    pal_uint32_t              *counts[2]; /* dimension must match NUM_THREADS */
    PAL_THREAD_FUNC             entry[2]; /* dimension must match NUM_THREADS */
    PAL_THREAD_POOL                *pool;
    PAL_THREAD_POOL_INIT       pool_init;
    SPSC_QUEUE_DATA            work_data;
    int                           result;

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create a queue for test purposes */
    required_size    = PAL_SPSCQueueQueryMemorySize_u32(CAPACITY);
    init.Capacity    = CAPACITY;
    init.UsageFlags  = 0;
    init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    init.MemorySize  = required_size;
    if (PAL_SPSCQueueCreate_u32(&spsc, &init) != 0)
    {   assert(0 && "PAL_SPSCQueueCreate_u32 failed");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* initialize the count arrays - one for each thread */
    for (i = 0; i < NUM_THREADS; ++i)
    {
        if ((counts[i] = PAL_MemoryArenaAllocateHostArray(global_arena, pal_uint32_t, OPS_PER_ITER)) == NULL)
        {   assert(0 && "Failed to allocate per-thread counts array");
            result = TEST_FAIL;
            goto cleanup;
        }
    }

    /* initialize the SPMC_QUEUE_DATA used as the thread pool context and available to all threads */
    work_data.GlobalArena     = global_arena;
    work_data.ScratchArena    = scratch_arena;
    work_data.SharedQueue     =&spsc;
    work_data.DrainSignal     = 0;
    work_data.IterationCount  = NUM_ITERS;
    work_data.OpsPerIteration = OPS_PER_ITER;
    work_data.CountsData      = counts;

    /* create a thread pool with two threads - one pusher and one taker */
    entry[0].Init = NULL; entry[0].Main = ThreadMain_SPSCQueuePusher;
    entry[1].Init = NULL; entry[1].Main = ThreadMain_SPSCQueueTaker;
    pool_init.PoolContext =&work_data;
    pool_init.ThreadFuncs = entry;
    pool_init.ThreadCount = NUM_THREADS;
    if ((pool = PAL_ThreadPoolCreate(&pool_init)) == NULL)
    {   assert(0 && "Failed to launch the thread pool");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* let everyone run */
    PAL_ThreadPoolLaunch(pool);

    /* wait until all threads exit */
    PAL_ThreadPoolDelete(pool);

    /* finally, validate results */
    for (i = 0; i < OPS_PER_ITER; ++i)
    {   /* sum the number of items item i was seen across all threads */
        pal_uint32_t sum = 0;
        for (j = 0; j < NUM_THREADS; ++j)
            sum += counts[j][i];
        /* make sure the total matches the number of iterations */
        if (sum != NUM_ITERS)
        {   assert(sum == NUM_ITERS && "Detected a lost item");
            result = TEST_FAIL;
            goto cleanup;
        }
    }

cleanup:
    PAL_MemoryArenaResetToMarker(scratch_arena, scratch_mark);
    PAL_MemoryArenaResetToMarker(global_arena, global_mark);
    return result;
}

static int
FTest_SPMCQueue_u32_PushTakeIsLIFO
(
    struct PAL_MEMORY_ARENA  *global_arena, 
    struct PAL_MEMORY_ARENA *scratch_arena
)
{   /* this test ensures that the SPMC concurrent queue produces items in last-in, first-out
     * order when using Push and Take operations (which operate only on the private end of 
     * the queue. only the thread that owns the queue can execute Push and Take operations.
     */
    pal_uint32_t const          CAPACITY = 16;
#ifdef FULL_VERIFICATION
    pal_uint32_t const         NUM_ITERS = 0xFFFFFFFFUL / CAPACITY; /* this takes considerable time */
#else
    pal_uint32_t const         NUM_ITERS = 10;
#endif
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_SPMC_QUEUE_INIT             init;
    PAL_SPMC_QUEUE_U32              spmc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, n;
    pal_uint32_t                    j, m;
    pal_uint32_t                    item;
    int                           result;

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create a queue for test purposes */
    required_size    = PAL_SPMCQueueQueryMemorySize_u32(CAPACITY);
    init.Capacity    = CAPACITY;
    init.UsageFlags  = 0;
    init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    init.MemorySize  = required_size;
    if (PAL_SPMCQueueCreate_u32(&spmc, &init) != 0)
    {   assert(0 && "PAL_SPMCQueueCreate_u32 failed");
        result = TEST_FAIL;
        goto cleanup;
    }

    for (j = 0, m = NUM_ITERS; j < m; ++j)
    {
        /* push items with values 0..15 */
        for (i = 0, n = CAPACITY; i < n; ++i)
        {
            if (PAL_SPMCQueuePush_u32(&spmc, i) == 0)
            {   assert(0 && "PAL_SPMCQueuePush_u32 failed");
                result = TEST_FAIL;
                goto cleanup;
            }
        }
        /* now take items 15..0 */
        for (i = 0, n = CAPACITY; i < n; ++i)
        {
            if (PAL_SPMCQueueTake_u32(&spmc, &item) == 0)
            {   assert(0 && "PAL_SPMCQueueTake_u32 failed");
                result = TEST_FAIL;
                goto cleanup;
            }
            if (item != (n-i-1))
            {   assert(item == (n-i-1) && "PAL_SPMCQueueTake_u32 is not LIFO");
                result = TEST_FAIL;
                goto cleanup;
            }
        }
    }

cleanup:
    PAL_MemoryArenaResetToMarker(scratch_arena, scratch_mark);
    PAL_MemoryArenaResetToMarker(global_arena, global_mark);
    return result;
}

static int
FTest_SPMCQueue_u32_PushStealIsFIFO /* FIFO-ish when concurrent */
(
    struct PAL_MEMORY_ARENA  *global_arena, 
    struct PAL_MEMORY_ARENA *scratch_arena
)
{   /* this test ensures that the SPMC concurrent queue produces items in first-in, first-out
     * order when using Push and Steal operations (which modify both ends of the queue.)
     * only the thread that owns the queue can perform Push operations, but any thread can 
     * perform Steal operations.
     */
    pal_uint32_t const          CAPACITY = 16;
#ifdef FULL_VERIFICATION
    pal_uint32_t const         NUM_ITERS = 0xFFFFFFFFUL / CAPACITY; /* this takes considerable time */
#else
    pal_uint32_t const         NUM_ITERS = 10;
#endif
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_SPMC_QUEUE_INIT             init;
    PAL_SPMC_QUEUE_U32              spmc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, n;
    pal_uint32_t                    j, m;
    pal_uint32_t                    item;
    int                           result;

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create a queue for test purposes */
    required_size    = PAL_SPMCQueueQueryMemorySize_u32(CAPACITY);
    init.Capacity    = CAPACITY;
    init.UsageFlags  = 0;
    init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    init.MemorySize  = required_size;
    if (PAL_SPMCQueueCreate_u32(&spmc, &init) != 0)
    {   assert(0 && "PAL_SPMCQueueCreate_u32 failed");
        result = TEST_FAIL;
        goto cleanup;
    }

    for (j = 0, m = NUM_ITERS; j < m; ++j)
    {
        /* push items with values 0..15 */
        for (i = 0, n = CAPACITY; i < n; ++i)
        {
            if (PAL_SPMCQueuePush_u32(&spmc, i) == 0)
            {   assert(0 && "PAL_SPMCQueuePush_u32 failed");
                result = TEST_FAIL;
                goto cleanup;
            }
        }
        /* now steal items 0..15 */
        for (i = 0, n = CAPACITY; i < n; ++i)
        {
            if (PAL_SPMCQueueSteal_u32(&spmc, &item) == 0)
            {   assert(0 && "PAL_SPMCQueueSteal_u32 failed");
                result = TEST_FAIL;
                goto cleanup;
            }
            if (item != i)
            {   assert(item == i && "PAL_SPMCQueueSteal_u32 is not FIFO");
                result = TEST_FAIL;
                goto cleanup;
            }
        }
    }

cleanup:
    PAL_MemoryArenaResetToMarker(scratch_arena, scratch_mark);
    PAL_MemoryArenaResetToMarker(global_arena, global_mark);
    return result;
}

static int
FTest_MPMCQueue_u32_PushTakeIsFIFO /* FIFO-ish when concurrent */
(
    struct PAL_MEMORY_ARENA  *global_arena, 
    struct PAL_MEMORY_ARENA *scratch_arena
)
{   /* this test ensures that the MPMC concurrent queue produces items in first-in, first-out
     * order when using Push and Take operations (which modify both ends of the queue.)
     */
    pal_uint32_t const          CAPACITY = 16;
#ifdef FULL_VERIFICATION
    pal_uint32_t const         NUM_ITERS = 0xFFFFFFFFUL / CAPACITY; /* this takes considerable time */
#else
    pal_uint32_t const         NUM_ITERS = 10;
#endif
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_MPMC_QUEUE_INIT             init;
    PAL_MPMC_QUEUE_U32              mpmc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, n;
    pal_uint32_t                    j, m;
    pal_uint32_t                    item;
    int                           result;

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create a queue for test purposes */
    required_size    = PAL_MPMCQueueQueryMemorySize_u32(CAPACITY);
    init.Capacity    = CAPACITY;
    init.UsageFlags  = 0;
    init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    init.MemorySize  = required_size;
    if (PAL_MPMCQueueCreate_u32(&mpmc, &init) != 0)
    {   assert(0 && "PAL_MPMCQueueCreate_u32 failed");
        result = TEST_FAIL;
        goto cleanup;
    }

    for (j = 0, m = NUM_ITERS; j < m; ++j)
    {
        /* push items with values 0..15 */
        for (i = 0, n = CAPACITY; i < n; ++i)
        {
            if (PAL_MPMCQueuePush_u32(&mpmc, i) == 0)
            {   assert(0 && "PAL_MPMCQueuePush_u32 failed");
                result = TEST_FAIL;
                goto cleanup;
            }
        }
        /* now steal items 0..15 */
        for (i = 0, n = CAPACITY; i < n; ++i)
        {
            if (PAL_MPMCQueueTake_u32(&mpmc, &item) == 0)
            {   assert(0 && "PAL_MPMCQueueTake_u32 failed");
                result = TEST_FAIL;
                goto cleanup;
            }
            if (item != i)
            {   assert(item == i && "PAL_MPMCQueueTake_u32 is not FIFO");
                result = TEST_FAIL;
                goto cleanup;
            }
        }
    }

cleanup:
    PAL_MemoryArenaResetToMarker(scratch_arena, scratch_mark);
    PAL_MemoryArenaResetToMarker(global_arena, global_mark);
    return result;
}

static int
FTest_SPMCQueue_u32_TakeFailsWhenEmpty
(
    struct PAL_MEMORY_ARENA  *global_arena, 
    struct PAL_MEMORY_ARENA *scratch_arena
)
{   /* this test ensures that PAL_SPMCQueueTake_u32 returns 0 when the queue is drained.
     */
    pal_uint32_t const          CAPACITY = 16;
#ifdef FULL_VERIFICATION
    pal_uint32_t const         NUM_ITERS = 0xFFFFFFFFUL / CAPACITY; /* this takes considerable time */
#else
    pal_uint32_t const         NUM_ITERS = 10;
#endif
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_SPMC_QUEUE_INIT             init;
    PAL_SPMC_QUEUE_U32              spmc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, n;
    pal_uint32_t                    j, m;
    pal_uint32_t                    item;
    int                           result;

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create a queue for test purposes */
    required_size    = PAL_SPMCQueueQueryMemorySize_u32(CAPACITY);
    init.Capacity    = CAPACITY;
    init.UsageFlags  = 0;
    init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    init.MemorySize  = required_size;
    if (PAL_SPMCQueueCreate_u32(&spmc, &init) != 0)
    {   assert(0 && "PAL_SPMCQueueCreate_u32 failed");
        result = TEST_FAIL;
        goto cleanup;
    }

    for (j = 0, m = NUM_ITERS; j < m; ++j)
    {
        /* push items with values 0..15 */
        for (i = 0, n = CAPACITY; i < n; ++i)
        {
            if (PAL_SPMCQueuePush_u32(&spmc, i) == 0)
            {   assert(0 && "PAL_SPMCQueuePush_u32 failed");
                result = TEST_FAIL;
                goto cleanup;
            }
        }
        /* now take items 15..0 */
        for (i = 0, n = CAPACITY; i < n; ++i)
        {
            if (PAL_SPMCQueueTake_u32(&spmc, &item) == 0)
            {   assert(0 && "PAL_SPMCQueueTake_u32 failed");
                result = TEST_FAIL;
                goto cleanup;
            }
            if (item != (n-i-1))
            {   assert(item == (n-i-1) && "PAL_SPMCQueueTake_u32 is not LIFO");
                result = TEST_FAIL;
                goto cleanup;
            }
        }
        /* now attempt to take an item from the empty queue */
        if (PAL_SPMCQueueTake_u32(&spmc, &item) != 0)
        {   assert(0 && "PAL_SPMCQueueTake_u32 didn't return 0 for empty queue");
            result = TEST_FAIL;
            goto cleanup;
        }
    }

cleanup:
    PAL_MemoryArenaResetToMarker(scratch_arena, scratch_mark);
    PAL_MemoryArenaResetToMarker(global_arena, global_mark);
    return result;
}

static int
FTest_SPMCQueue_u32_StealFailsWhenEmpty
(
    struct PAL_MEMORY_ARENA  *global_arena, 
    struct PAL_MEMORY_ARENA *scratch_arena
)
{   /* this test ensures that PAL_SPMCQueueSteal_u32 returns 0 when the queue is drained.
     */
    pal_uint32_t const          CAPACITY = 16;
#ifdef FULL_VERIFICATION
    pal_uint32_t const         NUM_ITERS = 0xFFFFFFFFUL / CAPACITY; /* this takes considerable time */
#else
    pal_uint32_t const         NUM_ITERS = 10;
#endif
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_SPMC_QUEUE_INIT             init;
    PAL_SPMC_QUEUE_U32              spmc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, n;
    pal_uint32_t                    j, m;
    pal_uint32_t                    item;
    int                           result;

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create a queue for test purposes */
    required_size    = PAL_SPMCQueueQueryMemorySize_u32(CAPACITY);
    init.Capacity    = CAPACITY;
    init.UsageFlags  = 0;
    init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    init.MemorySize  = required_size;
    if (PAL_SPMCQueueCreate_u32(&spmc, &init) != 0)
    {   assert(0 && "PAL_SPMCQueueCreate_u32 failed");
        result = TEST_FAIL;
        goto cleanup;
    }

    for (j = 0, m = NUM_ITERS; j < m; ++j)
    {
        /* push items with values 0..15 */
        for (i = 0, n = CAPACITY; i < n; ++i)
        {
            if (PAL_SPMCQueuePush_u32(&spmc, i) == 0)
            {   assert(0 && "PAL_SPMCQueuePush_u32 failed");
                result = TEST_FAIL;
                goto cleanup;
            }
        }
        /* now steal items 0..15 */
        for (i = 0, n = CAPACITY; i < n; ++i)
        {
            if (PAL_SPMCQueueSteal_u32(&spmc, &item) == 0)
            {   assert(0 && "PAL_SPMCQueueSteal_u32 failed");
                result = TEST_FAIL;
                goto cleanup;
            }
            if (item != i)
            {   assert(item == i && "PAL_SPMCQueueSteal_u32 is not FIFO");
                result = TEST_FAIL;
                goto cleanup;
            }
        }
        /* now attempt to steal an item from the empty queue */
        if (PAL_SPMCQueueSteal_u32(&spmc, &item) != 0)
        {   assert(0 && "PAL_SPMCQueueSteal_u32 didn't return 0 for empty queue");
            result = TEST_FAIL;
            goto cleanup;
        }
    }

cleanup:
    PAL_MemoryArenaResetToMarker(scratch_arena, scratch_mark);
    PAL_MemoryArenaResetToMarker(global_arena, global_mark);
    return result;
}

static int
FTest_MPMCQueue_u32_TakeFailsWhenEmpty
(
    struct PAL_MEMORY_ARENA  *global_arena, 
    struct PAL_MEMORY_ARENA *scratch_arena
)
{   /* this test ensures that PAL_MPMCQueueTake_u32 returns 0 when the queue is drained.
     */
    pal_uint32_t const          CAPACITY = 16;
#ifdef FULL_VERIFICATION
    pal_uint32_t const         NUM_ITERS = 0xFFFFFFFFUL / CAPACITY; /* this takes considerable time */
#else
    pal_uint32_t const         NUM_ITERS = 10;
#endif
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_MPMC_QUEUE_INIT             init;
    PAL_MPMC_QUEUE_U32              mpmc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, n;
    pal_uint32_t                    j, m;
    pal_uint32_t                    item;
    int                           result;

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create a queue for test purposes */
    required_size    = PAL_MPMCQueueQueryMemorySize_u32(CAPACITY);
    init.Capacity    = CAPACITY;
    init.UsageFlags  = 0;
    init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    init.MemorySize  = required_size;
    if (PAL_MPMCQueueCreate_u32(&mpmc, &init) != 0)
    {   assert(0 && "PAL_MPMCQueueCreate_u32 failed");
        result = TEST_FAIL;
        goto cleanup;
    }

    for (j = 0, m = NUM_ITERS; j < m; ++j)
    {
        /* push items with values 0..15 */
        for (i = 0, n = CAPACITY; i < n; ++i)
        {
            if (PAL_MPMCQueuePush_u32(&mpmc, i) == 0)
            {   assert(0 && "PAL_MPMCQueuePush_u32 failed");
                result = TEST_FAIL;
                goto cleanup;
            }
        }
        /* now take items 0..15 */
        for (i = 0, n = CAPACITY; i < n; ++i)
        {
            if (PAL_MPMCQueueTake_u32(&mpmc, &item) == 0)
            {   assert(0 && "PAL_MPMCQueueTake_u32 failed");
                result = TEST_FAIL;
                goto cleanup;
            }
            if (item != i)
            {   assert(item == i && "PAL_MPMCQueueTake_u32 is not FIFO");
                result = TEST_FAIL;
                goto cleanup;
            }
        }
        /* now attempt to take an item from the empty queue */
        if (PAL_MPMCQueueTake_u32(&mpmc, &item) != 0)
        {   assert(0 && "PAL_MPMCQueueTake_u32 didn't return 0 for empty queue");
            result = TEST_FAIL;
            goto cleanup;
        }
    }

cleanup:
    PAL_MemoryArenaResetToMarker(scratch_arena, scratch_mark);
    PAL_MemoryArenaResetToMarker(global_arena, global_mark);
    return result;
}

static int
FTest_SPMCQueue_u32_ConcurrentPushSteal
(
    struct PAL_MEMORY_ARENA  *global_arena, 
    struct PAL_MEMORY_ARENA *scratch_arena
)
{   /* this test ensures that a PAL_SPMC_QUEUE_U32 behaves correctly in the face of concurrent 
     * push operations on one thread and steal operations on another thread. specifically, no 
     * items should be lost - each item that is pushed should be seen by a steal.
     */
    pal_uint32_t const       NUM_THREADS = 2;
    pal_uint32_t const      OPS_PER_ITER = 4096;
    pal_uint32_t const         NUM_ITERS = 16384;
    pal_uint32_t const          CAPACITY =(pal_uint32_t) PAL_NextPow2GreaterOrEqual(OPS_PER_ITER * NUM_ITERS); /* ensure queue never fills */
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_SPMC_QUEUE_INIT             init;
    PAL_SPMC_QUEUE_U32              spmc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, j;
    pal_uint32_t              *counts[2]; /* dimension must match NUM_THREADS */
    PAL_THREAD_FUNC             entry[2]; /* dimension must match NUM_THREADS */
    PAL_THREAD_POOL                *pool;
    PAL_THREAD_POOL_INIT       pool_init;
    SPMC_QUEUE_DATA            work_data;
    int                           result;

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create a queue for test purposes */
    required_size    = PAL_SPMCQueueQueryMemorySize_u32(CAPACITY);
    init.Capacity    = CAPACITY;
    init.UsageFlags  = 0;
    init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    init.MemorySize  = required_size;
    if (PAL_SPMCQueueCreate_u32(&spmc, &init) != 0)
    {   assert(0 && "PAL_SPMCQueueCreate_u32 failed");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* initialize the count arrays - one for each thread */
    for (i = 0; i < NUM_THREADS; ++i)
    {
        if ((counts[i] = PAL_MemoryArenaAllocateHostArray(global_arena, pal_uint32_t, OPS_PER_ITER)) == NULL)
        {   assert(0 && "Failed to allocate per-thread counts array");
            result = TEST_FAIL;
            goto cleanup;
        }
    }

    /* initialize the SPMC_QUEUE_DATA used as the thread pool context and available to all threads */
    work_data.GlobalArena     = global_arena;
    work_data.ScratchArena    = scratch_arena;
    work_data.SharedQueue     =&spmc;
    work_data.DrainSignal     = 0;
    work_data.IterationCount  = NUM_ITERS;
    work_data.OpsPerIteration = OPS_PER_ITER;
    work_data.CountsData      = counts;

    /* create a thread pool with two threads - one pusher and one stealer */
    entry[0].Init = NULL; entry[0].Main = ThreadMain_SPMCQueuePusher;
    entry[1].Init = NULL; entry[1].Main = ThreadMain_SPMCQueueStealer;
    pool_init.PoolContext =&work_data;
    pool_init.ThreadFuncs = entry;
    pool_init.ThreadCount = NUM_THREADS;
    if ((pool = PAL_ThreadPoolCreate(&pool_init)) == NULL)
    {   assert(0 && "Failed to launch the thread pool");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* let everyone run */
    PAL_ThreadPoolLaunch(pool);

    /* wait until all threads exit */
    PAL_ThreadPoolDelete(pool);

    /* finally, validate results */
    for (i = 0; i < OPS_PER_ITER; ++i)
    {   /* sum the number of items item i was seen across all threads */
        pal_uint32_t sum = 0;
        for (j = 0; j < NUM_THREADS; ++j)
            sum += counts[j][i];
        /* make sure the total matches the number of iterations */
        if (sum != NUM_ITERS)
        {   assert(sum == NUM_ITERS && "Detected a lost item");
            result = TEST_FAIL;
            goto cleanup;
        }
    }

cleanup:
    PAL_MemoryArenaResetToMarker(scratch_arena, scratch_mark);
    PAL_MemoryArenaResetToMarker(global_arena, global_mark);
    return result;
}

static int
FTest_SPMCQueue_u32_ConcurrentPushStealMulti
(
    struct PAL_MEMORY_ARENA  *global_arena, 
    struct PAL_MEMORY_ARENA *scratch_arena
)
{   /* this test ensures that a PAL_SPMC_QUEUE_U32 behaves correctly in the face of concurrent 
     * push operations on one thread and steal operations on another thread. specifically, no 
     * items should be lost - each item that is pushed should be seen by a steal.
     */
    pal_uint32_t const       NUM_THREADS = 8;
    pal_uint32_t const      OPS_PER_ITER = 4096;
    pal_uint32_t const         NUM_ITERS = 16384;
    pal_uint32_t const          CAPACITY =(pal_uint32_t) PAL_NextPow2GreaterOrEqual(OPS_PER_ITER * NUM_ITERS);
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_SPMC_QUEUE_INIT             init;
    PAL_SPMC_QUEUE_U32              spmc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, j;
    pal_uint32_t              *counts[8]; /* dimension must match NUM_THREADS */
    PAL_THREAD_FUNC             entry[8]; /* dimension must match NUM_THREADS */
    PAL_THREAD_POOL                *pool;
    PAL_THREAD_POOL_INIT       pool_init;
    SPMC_QUEUE_DATA            work_data;
    int                           result;

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create a queue for test purposes */
    required_size    = PAL_SPMCQueueQueryMemorySize_u32(CAPACITY);
    init.Capacity    = CAPACITY;
    init.UsageFlags  = 0;
    init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    init.MemorySize  = required_size;
    if (PAL_SPMCQueueCreate_u32(&spmc, &init) != 0)
    {   assert(0 && "PAL_SPMCQueueCreate_u32 failed");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* initialize the count arrays - one for each thread */
    for (i = 0; i < NUM_THREADS; ++i)
    {
        if ((counts[i] = PAL_MemoryArenaAllocateHostArray(global_arena, pal_uint32_t, OPS_PER_ITER)) == NULL)
        {   assert(0 && "Failed to allocate per-thread counts array");
            result = TEST_FAIL;
            goto cleanup;
        }
    }

    /* initialize the SPMC_QUEUE_DATA used as the thread pool context and available to all threads */
    work_data.GlobalArena     = global_arena;
    work_data.ScratchArena    = scratch_arena;
    work_data.SharedQueue     =&spmc;
    work_data.DrainSignal     = 0;
    work_data.IterationCount  = NUM_ITERS;
    work_data.OpsPerIteration = OPS_PER_ITER;
    work_data.CountsData      = counts;

    /* create a thread pool with eight threads - one pusher and seven stealers */
    entry[0].Init = NULL; entry[0].Main = ThreadMain_SPMCQueuePusher;
    entry[1].Init = NULL; entry[1].Main = ThreadMain_SPMCQueueStealer;
    entry[2].Init = NULL; entry[2].Main = ThreadMain_SPMCQueueStealer;
    entry[3].Init = NULL; entry[3].Main = ThreadMain_SPMCQueueStealer;
    entry[4].Init = NULL; entry[4].Main = ThreadMain_SPMCQueueStealer;
    entry[5].Init = NULL; entry[5].Main = ThreadMain_SPMCQueueStealer;
    entry[6].Init = NULL; entry[6].Main = ThreadMain_SPMCQueueStealer;
    entry[7].Init = NULL; entry[7].Main = ThreadMain_SPMCQueueStealer;
    pool_init.PoolContext =&work_data;
    pool_init.ThreadFuncs = entry;
    pool_init.ThreadCount = NUM_THREADS;
    if ((pool = PAL_ThreadPoolCreate(&pool_init)) == NULL)
    {   assert(0 && "Failed to launch the thread pool");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* let everyone run */
    PAL_ThreadPoolLaunch(pool);

    /* wait until all threads exit */
    PAL_ThreadPoolDelete(pool);

    /* finally, validate results */
    for (i = 0; i < OPS_PER_ITER; ++i)
    {   /* sum the number of items item i was seen across all threads */
        pal_uint32_t sum = 0;
        for (j = 0; j < NUM_THREADS; ++j)
            sum += counts[j][i];
        /* make sure the total matches the number of iterations */
        if (sum != NUM_ITERS)
        {
            if (sum > NUM_ITERS)
            {   /* concurrent steal operations produced the same item */
                assert(sum == NUM_ITERS && "Duplicate item: Concurrent steal conflict");
            }
            else
            {   /* concurrent push and steal resulted in an item being lost */
                assert(sum == NUM_ITERS && "Dropped item: Concurrent push+steal conflict");
            }
            result = TEST_FAIL;
            goto cleanup;
        }
    }

cleanup:
    PAL_MemoryArenaResetToMarker(scratch_arena, scratch_mark);
    PAL_MemoryArenaResetToMarker(global_arena, global_mark);
    return result;
}

static int
FTest_SPMCQueue_u32_ConcurrentTakeSteal
(
    struct PAL_MEMORY_ARENA  *global_arena, 
    struct PAL_MEMORY_ARENA *scratch_arena
)
{   /* this test ensures that a PAL_SPMC_QUEUE_U32 behaves correctly in the face of concurrent 
     * take operations on one thread and steal operations on another thread. specifically, no 
     * items should be lost - each item that is pushed should be seen by either a steal or a take.
     */
    pal_uint32_t const       NUM_THREADS = 2;
    pal_uint32_t const      OPS_PER_ITER = 4096;
    pal_uint32_t const         NUM_ITERS = 16384;
    pal_uint32_t const          CAPACITY =(pal_uint32_t) PAL_NextPow2GreaterOrEqual(OPS_PER_ITER * NUM_ITERS); /* ensure queue never fills */
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_SPMC_QUEUE_INIT             init;
    PAL_SPMC_QUEUE_U32              spmc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, j;
    pal_uint32_t              *counts[2]; /* dimension must match NUM_THREADS */
    PAL_THREAD_FUNC             entry[2]; /* dimension must match NUM_THREADS */
    PAL_THREAD_POOL                *pool;
    PAL_THREAD_POOL_INIT       pool_init;
    SPMC_QUEUE_DATA            work_data;
    int                           result;

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create a queue for test purposes */
    required_size    = PAL_SPMCQueueQueryMemorySize_u32(CAPACITY);
    init.Capacity    = CAPACITY;
    init.UsageFlags  = 0;
    init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    init.MemorySize  = required_size;
    if (PAL_SPMCQueueCreate_u32(&spmc, &init) != 0)
    {   assert(0 && "PAL_SPMCQueueCreate_u32 failed");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* initialize the count arrays - one for each thread */
    for (i = 0; i < NUM_THREADS; ++i)
    {
        if ((counts[i] = PAL_MemoryArenaAllocateHostArray(global_arena, pal_uint32_t, OPS_PER_ITER)) == NULL)
        {   assert(0 && "Failed to allocate per-thread counts array");
            result = TEST_FAIL;
            goto cleanup;
        }
    }

    /* initialize the SPMC_QUEUE_DATA used as the thread pool context and available to all threads */
    work_data.GlobalArena     = global_arena;
    work_data.ScratchArena    = scratch_arena;
    work_data.SharedQueue     =&spmc;
    work_data.DrainSignal     = 0;
    work_data.IterationCount  = NUM_ITERS;
    work_data.OpsPerIteration = OPS_PER_ITER;
    work_data.CountsData      = counts;

    /* create a thread pool with two threads - one pusher and one stealer */
    entry[0].Init = NULL; entry[0].Main = ThreadMain_SPMCQueuePushTaker;
    entry[1].Init = NULL; entry[1].Main = ThreadMain_SPMCQueueStealer;
    pool_init.PoolContext =&work_data;
    pool_init.ThreadFuncs = entry;
    pool_init.ThreadCount = NUM_THREADS;
    if ((pool = PAL_ThreadPoolCreate(&pool_init)) == NULL)
    {   assert(0 && "Failed to launch the thread pool");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* let everyone run */
    PAL_ThreadPoolLaunch(pool);

    /* wait until all threads exit */
    PAL_ThreadPoolDelete(pool);

    /* finally, validate results */
    for (i = 0; i < OPS_PER_ITER; ++i)
    {   /* sum the number of items item i was seen across all threads */
        pal_uint32_t sum = 0;
        for (j = 0; j < NUM_THREADS; ++j)
            sum += counts[j][i];
        /* make sure the total matches the number of iterations */
        if (sum != NUM_ITERS)
        {   assert(sum == NUM_ITERS && "Detected a lost item");
            if (sum > NUM_ITERS)
            {   
                assert(sum == NUM_ITERS && "Item seen twice - conflict between take and steal");
            }
            if (sum < NUM_ITERS)
            {
                assert(sum == NUM_ITERS && "Item disappeared - conflict between take and steal");
            }
            result = TEST_FAIL;
            goto cleanup;
        }
    }

cleanup:
    PAL_MemoryArenaResetToMarker(scratch_arena, scratch_mark);
    PAL_MemoryArenaResetToMarker(global_arena, global_mark);
    return result;
}

static int
FTest_SPMCQueue_u32_ConcurrentTakeStealMulti
(
    struct PAL_MEMORY_ARENA  *global_arena, 
    struct PAL_MEMORY_ARENA *scratch_arena
)
{   /* this test ensures that a PAL_SPMC_QUEUE_U32 behaves correctly in the face of concurrent 
     * push operations on one thread and steal operations on another thread. specifically, no 
     * items should be lost - each item that is pushed should be seen by a steal.
     */
    pal_uint32_t const       NUM_THREADS = 8;
    pal_uint32_t const      OPS_PER_ITER = 4096;
    pal_uint32_t const         NUM_ITERS = 16384;
    pal_uint32_t const          CAPACITY =(pal_uint32_t) PAL_NextPow2GreaterOrEqual(OPS_PER_ITER * NUM_ITERS);
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_SPMC_QUEUE_INIT             init;
    PAL_SPMC_QUEUE_U32              spmc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, j;
    pal_uint32_t              *counts[8]; /* dimension must match NUM_THREADS */
    PAL_THREAD_FUNC             entry[8]; /* dimension must match NUM_THREADS */
    PAL_THREAD_POOL                *pool;
    PAL_THREAD_POOL_INIT       pool_init;
    SPMC_QUEUE_DATA            work_data;
    int                           result;

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create a queue for test purposes */
    required_size    = PAL_SPMCQueueQueryMemorySize_u32(CAPACITY);
    init.Capacity    = CAPACITY;
    init.UsageFlags  = 0;
    init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    init.MemorySize  = required_size;
    if (PAL_SPMCQueueCreate_u32(&spmc, &init) != 0)
    {   assert(0 && "PAL_SPMCQueueCreate_u32 failed");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* initialize the count arrays - one for each thread */
    for (i = 0; i < NUM_THREADS; ++i)
    {
        if ((counts[i] = PAL_MemoryArenaAllocateHostArray(global_arena, pal_uint32_t, OPS_PER_ITER)) == NULL)
        {   assert(0 && "Failed to allocate per-thread counts array");
            result = TEST_FAIL;
            goto cleanup;
        }
    }

    /* initialize the SPMC_QUEUE_DATA used as the thread pool context and available to all threads */
    work_data.GlobalArena     = global_arena;
    work_data.ScratchArena    = scratch_arena;
    work_data.SharedQueue     =&spmc;
    work_data.DrainSignal     = 0;
    work_data.IterationCount  = NUM_ITERS;
    work_data.OpsPerIteration = OPS_PER_ITER;
    work_data.CountsData      = counts;

    /* create a thread pool with eight threads - one pusher and seven stealers */
    entry[0].Init = NULL; entry[0].Main = ThreadMain_SPMCQueuePushTaker;
    entry[1].Init = NULL; entry[1].Main = ThreadMain_SPMCQueueStealer;
    entry[2].Init = NULL; entry[2].Main = ThreadMain_SPMCQueueStealer;
    entry[3].Init = NULL; entry[3].Main = ThreadMain_SPMCQueueStealer;
    entry[4].Init = NULL; entry[4].Main = ThreadMain_SPMCQueueStealer;
    entry[5].Init = NULL; entry[5].Main = ThreadMain_SPMCQueueStealer;
    entry[6].Init = NULL; entry[6].Main = ThreadMain_SPMCQueueStealer;
    entry[7].Init = NULL; entry[7].Main = ThreadMain_SPMCQueueStealer;
    pool_init.PoolContext =&work_data;
    pool_init.ThreadFuncs = entry;
    pool_init.ThreadCount = NUM_THREADS;
    if ((pool = PAL_ThreadPoolCreate(&pool_init)) == NULL)
    {   assert(0 && "Failed to launch the thread pool");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* let everyone run */
    PAL_ThreadPoolLaunch(pool);

    /* wait until all threads exit */
    PAL_ThreadPoolDelete(pool);

    /* finally, validate results */
    for (i = 0; i < OPS_PER_ITER; ++i)
    {   /* sum the number of items item i was seen across all threads */
        pal_uint32_t sum = 0;
        for (j = 0; j < NUM_THREADS; ++j)
            sum += counts[j][i];
        /* make sure the total matches the number of iterations */
        if (sum != NUM_ITERS)
        {
            if (sum > NUM_ITERS)
            {   /* concurrent take/steal operations produced the same item */
                assert(sum == NUM_ITERS && "Duplicate item: Concurrent take+steal conflict");
            }
            else
            {   /* concurrent take and steal resulted in an item being lost */
                assert(sum == NUM_ITERS && "Dropped item: Concurrent take+steal conflict");
            }
            result = TEST_FAIL;
            goto cleanup;
        }
    }

cleanup:
    PAL_MemoryArenaResetToMarker(scratch_arena, scratch_mark);
    PAL_MemoryArenaResetToMarker(global_arena, global_mark);
    return result;
}

static int
FTest_MPMCQueue_u32_ConcurrentPushTake_SPSC
(
    struct PAL_MEMORY_ARENA  *global_arena, 
    struct PAL_MEMORY_ARENA *scratch_arena
)
{   /* this test ensures that a PAL_MPMC_QUEUE_U32 behaves correctly in the face of concurrent 
     * push operations on one thread and take operations on another thread. specifically, no 
     * items should be lost - each item that is pushed should be seen by a take.
     */
    pal_uint32_t const       NUM_THREADS = 2;
    pal_uint32_t const      OPS_PER_ITER = 4096;
    pal_uint32_t const         NUM_ITERS = 16384;
    pal_uint32_t const          CAPACITY =(pal_uint32_t) PAL_NextPow2GreaterOrEqual(OPS_PER_ITER * NUM_ITERS); /* ensure queue never fills */
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_MPMC_QUEUE_INIT             init;
    PAL_MPMC_QUEUE_U32              mpmc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, j;
    pal_uint32_t              *counts[2]; /* dimension must match NUM_THREADS */
    PAL_THREAD_FUNC             entry[2]; /* dimension must match NUM_THREADS */
    PAL_THREAD_POOL                *pool;
    PAL_THREAD_POOL_INIT       pool_init;
    MPMC_QUEUE_DATA            work_data;
    int                           result;

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create a queue for test purposes */
    required_size    = PAL_MPMCQueueQueryMemorySize_u32(CAPACITY);
    init.Capacity    = CAPACITY;
    init.UsageFlags  = 0;
    init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    init.MemorySize  = required_size;
    if (PAL_MPMCQueueCreate_u32(&mpmc, &init) != 0)
    {   assert(0 && "PAL_MPMCQueueCreate_u32 failed");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* initialize the count arrays - one for each thread */
    for (i = 0; i < NUM_THREADS; ++i)
    {
        if ((counts[i] = PAL_MemoryArenaAllocateHostArray(global_arena, pal_uint32_t, OPS_PER_ITER)) == NULL)
        {   assert(0 && "Failed to allocate per-thread counts array");
            result = TEST_FAIL;
            goto cleanup;
        }
    }

    /* initialize the MPMC_QUEUE_DATA used as the thread pool context and available to all threads */
    work_data.GlobalArena     = global_arena;
    work_data.ScratchArena    = scratch_arena;
    work_data.SharedQueue     =&mpmc;
    work_data.DrainSignal     = 0;
    work_data.IterationCount  = NUM_ITERS;
    work_data.OpsPerIteration = OPS_PER_ITER;
    work_data.CountsData      = counts;

    /* create a thread pool with two threads - one pusher and one stealer */
    entry[0].Init = NULL; entry[0].Main = ThreadMain_MPMCQueuePusher;
    entry[1].Init = NULL; entry[1].Main = ThreadMain_MPMCQueueTaker;
    pool_init.PoolContext =&work_data;
    pool_init.ThreadFuncs = entry;
    pool_init.ThreadCount = NUM_THREADS;
    if ((pool = PAL_ThreadPoolCreate(&pool_init)) == NULL)
    {   assert(0 && "Failed to launch the thread pool");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* let everyone run */
    PAL_ThreadPoolLaunch(pool);

    /* wait until all threads exit */
    PAL_ThreadPoolDelete(pool);

    /* finally, validate results */
    for (i = 0; i < OPS_PER_ITER; ++i)
    {   /* sum the number of items item i was seen across all threads */
        pal_uint32_t sum = 0;
        for (j = 0; j < NUM_THREADS; ++j)
            sum += counts[j][i];
        /* make sure the total matches the number of iterations */
        if (sum != NUM_ITERS)
        {   assert(sum == NUM_ITERS && "Detected a lost item");
            result = TEST_FAIL;
            goto cleanup;
        }
    }

cleanup:
    PAL_MemoryArenaResetToMarker(scratch_arena, scratch_mark);
    PAL_MemoryArenaResetToMarker(global_arena, global_mark);
    return result;
}

static int
FTest_MPMCQueue_u32_ConcurrentPushTake_MPSC
(
    struct PAL_MEMORY_ARENA  *global_arena, 
    struct PAL_MEMORY_ARENA *scratch_arena
)
{   /* this test ensures that a PAL_MPMC_QUEUE_U32 behaves correctly in the face of concurrent 
     * push operations on one thread and take operations on another thread. specifically, no 
     * items should be lost - each item that is pushed should be seen by a take.
     */
    pal_uint32_t const       NUM_THREADS = 3;
    pal_uint32_t const       NUM_PUSHERS = 2;
    pal_uint32_t const        NUM_TAKERS = 1;
    pal_uint32_t const      OPS_PER_ITER = 4096;
    pal_uint32_t const         NUM_ITERS = 16384;
    pal_uint32_t const          CAPACITY =(pal_uint32_t) PAL_NextPow2GreaterOrEqual(OPS_PER_ITER * NUM_ITERS * NUM_PUSHERS); /* ensure queue never fills */
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_MPMC_QUEUE_INIT             init;
    PAL_MPMC_QUEUE_U32              mpmc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, j;
    pal_uint32_t              *counts[3]; /* dimension must match NUM_THREADS */
    PAL_THREAD_FUNC             entry[3]; /* dimension must match NUM_THREADS */
    PAL_THREAD_POOL               *pool;
    PAL_THREAD_POOL_INIT      pool_init;
    MPMC_QUEUE_DATA            work_data;
    int                           result;

    PAL_UNUSED_LOCAL(NUM_PUSHERS);
    PAL_UNUSED_LOCAL(NUM_TAKERS);

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create a queue for test purposes */
    required_size    = PAL_MPMCQueueQueryMemorySize_u32(CAPACITY);
    init.Capacity    = CAPACITY;
    init.UsageFlags  = 0;
    init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    init.MemorySize  = required_size;
    if (PAL_MPMCQueueCreate_u32(&mpmc, &init) != 0)
    {   assert(0 && "PAL_MPMCQueueCreate_u32 failed");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* initialize the count arrays - one for each thread */
    for (i = 0; i < NUM_THREADS; ++i)
    {
        if ((counts[i] = PAL_MemoryArenaAllocateHostArray(global_arena, pal_uint32_t, OPS_PER_ITER)) == NULL)
        {   assert(0 && "Failed to allocate per-thread counts array");
            result = TEST_FAIL;
            goto cleanup;
        }
    }

    /* initialize the MPMC_QUEUE_DATA used as the thread pool context and available to all threads */
    work_data.GlobalArena     = global_arena;
    work_data.ScratchArena    = scratch_arena;
    work_data.SharedQueue     =&mpmc;
    work_data.DrainSignal     = 0;
    work_data.IterationCount  = NUM_ITERS;
    work_data.OpsPerIteration = OPS_PER_ITER;
    work_data.CountsData      = counts;

    /* create a thread pool with two threads - one pusher and one stealer */
    entry[0].Init = NULL; entry[0].Main = ThreadMain_MPMCQueuePusher;
    entry[1].Init = NULL; entry[1].Main = ThreadMain_MPMCQueuePusher;
    entry[2].Init = NULL; entry[2].Main = ThreadMain_MPMCQueueTaker;
    pool_init.PoolContext =&work_data;
    pool_init.ThreadFuncs = entry;
    pool_init.ThreadCount = NUM_THREADS;
    if ((pool = PAL_ThreadPoolCreate(&pool_init)) == NULL)
    {   assert(0 && "Failed to launch the thread pool");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* let everyone run */
    PAL_ThreadPoolLaunch(pool);

    /* wait until all threads exit */
    PAL_ThreadPoolDelete(pool);

    /* finally, validate results */
    for (i = 0; i < OPS_PER_ITER; ++i)
    {   /* sum the number of items item i was seen across all threads */
        pal_uint32_t sum = 0;
        for (j = 0; j < NUM_THREADS; ++j)
            sum += counts[j][i];
        /* make sure the total matches the number of iterations */
        if (sum != (NUM_ITERS * NUM_PUSHERS))
        {   assert(sum == (NUM_ITERS * NUM_PUSHERS) && "Detected a lost item");
            result = TEST_FAIL;
            goto cleanup;
        }
    }

cleanup:
    PAL_MemoryArenaResetToMarker(scratch_arena, scratch_mark);
    PAL_MemoryArenaResetToMarker(global_arena, global_mark);
    return result;
}

static int
FTest_MPMCQueue_u32_ConcurrentPushTake_SPMC
(
    struct PAL_MEMORY_ARENA  *global_arena, 
    struct PAL_MEMORY_ARENA *scratch_arena
)
{   /* this test ensures that a PAL_MPMC_QUEUE_U32 behaves correctly in the face of concurrent 
     * push operations on one thread and take operations on another thread. specifically, no 
     * items should be lost - each item that is pushed should be seen by a take.
     */
    pal_uint32_t const       NUM_THREADS = 3;
    pal_uint32_t const       NUM_PUSHERS = 1;
    pal_uint32_t const        NUM_TAKERS = 2;
    pal_uint32_t const      OPS_PER_ITER = 4096;
    pal_uint32_t const         NUM_ITERS = 16384;
    pal_uint32_t const          CAPACITY =(pal_uint32_t) PAL_NextPow2GreaterOrEqual(OPS_PER_ITER * NUM_ITERS * NUM_PUSHERS); /* ensure queue never fills */
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_MPMC_QUEUE_INIT             init;
    PAL_MPMC_QUEUE_U32              mpmc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, j;
    pal_uint32_t              *counts[3]; /* dimension must match NUM_THREADS */
    PAL_THREAD_FUNC             entry[3]; /* dimension must match NUM_THREADS */
    PAL_THREAD_POOL                *pool;
    PAL_THREAD_POOL_INIT       pool_init;
    MPMC_QUEUE_DATA            work_data;
    int                           result;

    PAL_UNUSED_LOCAL(NUM_PUSHERS);
    PAL_UNUSED_LOCAL(NUM_TAKERS);

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create a queue for test purposes */
    required_size    = PAL_MPMCQueueQueryMemorySize_u32(CAPACITY);
    init.Capacity    = CAPACITY;
    init.UsageFlags  = 0;
    init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    init.MemorySize  = required_size;
    if (PAL_MPMCQueueCreate_u32(&mpmc, &init) != 0)
    {   assert(0 && "PAL_MPMCQueueCreate_u32 failed");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* initialize the count arrays - one for each thread */
    for (i = 0; i < NUM_THREADS; ++i)
    {
        if ((counts[i] = PAL_MemoryArenaAllocateHostArray(global_arena, pal_uint32_t, OPS_PER_ITER)) == NULL)
        {   assert(0 && "Failed to allocate per-thread counts array");
            result = TEST_FAIL;
            goto cleanup;
        }
    }

    /* initialize the MPMC_QUEUE_DATA used as the thread pool context and available to all threads */
    work_data.GlobalArena     = global_arena;
    work_data.ScratchArena    = scratch_arena;
    work_data.SharedQueue     =&mpmc;
    work_data.DrainSignal     = 0;
    work_data.IterationCount  = NUM_ITERS;
    work_data.OpsPerIteration = OPS_PER_ITER;
    work_data.CountsData      = counts;

    /* create a thread pool with two threads - one pusher and one stealer */
    entry[0].Init = NULL; entry[0].Main = ThreadMain_MPMCQueuePusher;
    entry[1].Init = NULL; entry[1].Main = ThreadMain_MPMCQueueTaker;
    entry[2].Init = NULL; entry[2].Main = ThreadMain_MPMCQueueTaker;
    pool_init.PoolContext =&work_data;
    pool_init.ThreadFuncs = entry;
    pool_init.ThreadCount = NUM_THREADS;
    if ((pool = PAL_ThreadPoolCreate(&pool_init)) == NULL)
    {   assert(0 && "Failed to launch the thread pool");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* let everyone run */
    PAL_ThreadPoolLaunch(pool);

    /* wait until all threads exit */
    PAL_ThreadPoolDelete(pool);

    /* finally, validate results */
    for (i = 0; i < OPS_PER_ITER; ++i)
    {   /* sum the number of items item i was seen across all threads */
        pal_uint32_t sum = 0;
        for (j = 0; j < NUM_THREADS; ++j)
            sum += counts[j][i];
        /* make sure the total matches the number of iterations */
        if (sum != (NUM_ITERS * NUM_PUSHERS))
        {   assert(sum == (NUM_ITERS * NUM_PUSHERS) && "Detected a lost item");
            result = TEST_FAIL;
            goto cleanup;
        }
    }

cleanup:
    PAL_MemoryArenaResetToMarker(scratch_arena, scratch_mark);
    PAL_MemoryArenaResetToMarker(global_arena, global_mark);
    return result;
}

static int
FTest_MPMCQueue_u32_ConcurrentPushTake_MPMC
(
    struct PAL_MEMORY_ARENA  *global_arena, 
    struct PAL_MEMORY_ARENA *scratch_arena
)
{   /* this test ensures that a PAL_MPMC_QUEUE_U32 behaves correctly in the face of concurrent 
     * push operations on one thread and take operations on another thread. specifically, no 
     * items should be lost - each item that is pushed should be seen by a take.
     */
    pal_uint32_t const       NUM_THREADS = 8;
    pal_uint32_t const       NUM_PUSHERS = 2;
    pal_uint32_t const        NUM_TAKERS = 6;
    pal_uint32_t const      OPS_PER_ITER = 4096;
    pal_uint32_t const         NUM_ITERS = 16384;
    pal_uint32_t const          CAPACITY =(pal_uint32_t) PAL_NextPow2GreaterOrEqual(OPS_PER_ITER * NUM_ITERS * NUM_PUSHERS); /* ensure queue never fills */
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_MPMC_QUEUE_INIT             init;
    PAL_MPMC_QUEUE_U32              mpmc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, j;
    pal_uint32_t              *counts[8]; /* dimension must match NUM_THREADS */
    PAL_THREAD_FUNC             entry[8]; /* dimension must match NUM_THREADS */
    PAL_THREAD_POOL                *pool;
    PAL_THREAD_POOL_INIT       pool_init;
    MPMC_QUEUE_DATA            work_data;
    int                           result;

    PAL_UNUSED_LOCAL(NUM_PUSHERS);
    PAL_UNUSED_LOCAL(NUM_TAKERS);

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create a queue for test purposes */
    required_size    = PAL_MPMCQueueQueryMemorySize_u32(CAPACITY);
    init.Capacity    = CAPACITY;
    init.UsageFlags  = 0;
    init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    init.MemorySize  = required_size;
    if (PAL_MPMCQueueCreate_u32(&mpmc, &init) != 0)
    {   assert(0 && "PAL_MPMCQueueCreate_u32 failed");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* initialize the count arrays - one for each thread */
    for (i = 0; i < NUM_THREADS; ++i)
    {
        if ((counts[i] = PAL_MemoryArenaAllocateHostArray(global_arena, pal_uint32_t, OPS_PER_ITER)) == NULL)
        {   assert(0 && "Failed to allocate per-thread counts array");
            result = TEST_FAIL;
            goto cleanup;
        }
    }

    /* initialize the MPMC_QUEUE_DATA used as the thread pool context and available to all threads */
    work_data.GlobalArena     = global_arena;
    work_data.ScratchArena    = scratch_arena;
    work_data.SharedQueue     =&mpmc;
    work_data.DrainSignal     = 0;
    work_data.IterationCount  = NUM_ITERS;
    work_data.OpsPerIteration = OPS_PER_ITER;
    work_data.CountsData      = counts;

    /* create a thread pool with two threads - one pusher and one stealer */
    entry[0].Init = NULL; entry[0].Main = ThreadMain_MPMCQueuePusher;
    entry[1].Init = NULL; entry[1].Main = ThreadMain_MPMCQueuePusher;
    entry[2].Init = NULL; entry[2].Main = ThreadMain_MPMCQueueTaker;
    entry[3].Init = NULL; entry[3].Main = ThreadMain_MPMCQueueTaker;
    entry[4].Init = NULL; entry[4].Main = ThreadMain_MPMCQueueTaker;
    entry[5].Init = NULL; entry[5].Main = ThreadMain_MPMCQueueTaker;
    entry[6].Init = NULL; entry[6].Main = ThreadMain_MPMCQueueTaker;
    entry[7].Init = NULL; entry[7].Main = ThreadMain_MPMCQueueTaker;
    pool_init.PoolContext =&work_data;
    pool_init.ThreadFuncs = entry;
    pool_init.ThreadCount = NUM_THREADS;
    if ((pool = PAL_ThreadPoolCreate(&pool_init)) == NULL)
    {   assert(0 && "Failed to launch the thread pool");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* let everyone run */
    PAL_ThreadPoolLaunch(pool);

    /* wait until all threads exit */
    PAL_ThreadPoolDelete(pool);

    /* finally, validate results */
    for (i = 0; i < OPS_PER_ITER; ++i)
    {   /* sum the number of items item i was seen across all threads */
        pal_uint32_t sum = 0;
        for (j = 0; j < NUM_THREADS; ++j)
            sum += counts[j][i];
        /* make sure the total matches the number of iterations */
        if (sum != (NUM_ITERS * NUM_PUSHERS))
        {   assert(sum == (NUM_ITERS * NUM_PUSHERS) && "Detected a lost item");
            result = TEST_FAIL;
            goto cleanup;
        }
    }

cleanup:
    PAL_MemoryArenaResetToMarker(scratch_arena, scratch_mark);
    PAL_MemoryArenaResetToMarker(global_arena, global_mark);
    return result;
}

int main
(
    int    argc, 
    char **argv
)
{
    PAL_HOST_MEMORY_ALLOCATION  global_alloc;
    PAL_HOST_MEMORY_ALLOCATION scratch_alloc;
    PAL_MEMORY_ARENA_INIT  global_arena_init;
    PAL_MEMORY_ARENA_INIT scratch_arena_init;
    PAL_MEMORY_ARENA            global_arena;
    PAL_MEMORY_ARENA           scratch_arena;

    pal_uint32_t                 alloc_flags = PAL_HOST_MEMORY_ALLOCATION_FLAGS_READWRITE;
    pal_sint32_t                   exit_code = ERROR_SUCCESS;
    pal_usize_t                  global_size = Megabytes(2048);
    pal_usize_t                 scratch_size = Kilobytes(128);
    int                                  res;

    PAL_UNUSED_ARG(argc);
    PAL_UNUSED_ARG(argv);

    /* zero-initialize everything for easy cleanup */
    PAL_ZeroMemory(&scratch_alloc, sizeof(PAL_HOST_MEMORY_ALLOCATION));
    PAL_ZeroMemory(&global_alloc , sizeof(PAL_HOST_MEMORY_ALLOCATION));

    /* create a scratch memory arena used for short-lived allocations (function lifetime) */
    if (PAL_HostMemoryReserveAndCommit(&scratch_alloc, scratch_size, scratch_size, alloc_flags) != 0)
    {   /* failed to allocate scratch memory, execution cannot continue */
        exit_code = GetLastError();
        goto cleanup_and_exit;
    }
    scratch_arena_init.AllocatorName = "Application scratch memory";
    scratch_arena_init.AllocatorType = PAL_MEMORY_ALLOCATOR_TYPE_HOST;
    scratch_arena_init.MemoryStart   =(pal_uint64_t) scratch_alloc.BaseAddress;
    scratch_arena_init.MemorySize    =(pal_uint64_t) scratch_alloc.BytesCommitted;
    scratch_arena_init.UserData      = NULL;
    scratch_arena_init.UserDataSize  = 0;
    PAL_MemoryArenaCreate(&scratch_arena, &scratch_arena_init);

    /* create a global memory arena used for long-lived allocations (test/application lifetime) */
    if (PAL_HostMemoryReserveAndCommit(&global_alloc, global_size, global_size, alloc_flags) != 0)
    {   /* failed to allocate global memory, execution cannot continue */
        exit_code = GetLastError();
        goto cleanup_and_exit;
    }
    global_arena_init.AllocatorName = "Application global memory";
    global_arena_init.AllocatorType = PAL_MEMORY_ALLOCATOR_TYPE_HOST;
    global_arena_init.MemoryStart   =(pal_uint64_t) global_alloc.BaseAddress;
    global_arena_init.MemorySize    =(pal_uint64_t) global_alloc.BytesCommitted;
    global_arena_init.UserData      = NULL;
    global_arena_init.UserDataSize  = 0;
    PAL_MemoryArenaCreate(&global_arena, &global_arena_init);

    /* execute functional tests */
    res = FTest_SPSCQueue_u32_PushTakeIsFIFO          (&global_arena, &scratch_arena); printf("FTest_SPSCQueue_u32_PushTakeIsFIFO          : %d\r\n", res);
    res = FTest_SPSCQueue_u32_PushFailsWhenFull       (&global_arena, &scratch_arena); printf("FTest_SPSCQueue_u32_PushFailsWhenFull       : %d\r\n", res);
    res = FTest_SPSCQueue_u32_TakeFailsWhenEmpty      (&global_arena, &scratch_arena); printf("FTest_SPSCQueue_u32_TakeFailsWhenEmpty      : %d\r\n", res);
    res = FTest_SPSCQueue_u32_ConcurrentPushTake      (&global_arena, &scratch_arena); printf("FTest_SPSCQueue_u32_ConcurrentPushTake      : %d\r\n", res);
    res = FTest_SPMCQueue_u32_PushTakeIsLIFO          (&global_arena, &scratch_arena); printf("FTest_SPMCQueue_u32_PushTakeIsLIFO          : %d\r\n", res);
    res = FTest_SPMCQueue_u32_PushStealIsFIFO         (&global_arena, &scratch_arena); printf("FTest_SPMCQueue_u32_PushStealIsFIFO         : %d\r\n", res);
    res = FTest_SPMCQueue_u32_TakeFailsWhenEmpty      (&global_arena, &scratch_arena); printf("FTest_SPMCQueue_u32_TakeFailsWhenEmpty      : %d\r\n", res);
    res = FTest_SPMCQueue_u32_StealFailsWhenEmpty     (&global_arena, &scratch_arena); printf("FTest_SPMCQueue_u32_StealFailsWhenEmpty     : %d\r\n", res);
    res = FTest_SPMCQueue_u32_ConcurrentPushSteal     (&global_arena, &scratch_arena); printf("FTest_SPMCQueue_u32_ConcurrentPushSteal     : %d\r\n", res);
    res = FTest_SPMCQueue_u32_ConcurrentPushStealMulti(&global_arena, &scratch_arena); printf("FTest_SPMCQueue_u32_ConcurrentPushStealMulti: %d\r\n", res);
    res = FTest_SPMCQueue_u32_ConcurrentTakeSteal     (&global_arena, &scratch_arena); printf("FTest_SPMCQueue_u32_ConcurrentTakeSteal     : %d\r\n", res);
    res = FTest_SPMCQueue_u32_ConcurrentTakeStealMulti(&global_arena, &scratch_arena); printf("FTest_SPMCQueue_u32_ConcurrentTakeStealMulti: %d\r\n", res);
    res = FTest_MPMCQueue_u32_PushTakeIsFIFO          (&global_arena, &scratch_arena); printf("FTest_MPMCQueue_u32_PushTakeIsFIFO          : %d\r\n", res);
    res = FTest_MPMCQueue_u32_TakeFailsWhenEmpty      (&global_arena, &scratch_arena); printf("FTest_MPMCQueue_u32_TakeFailsWhenEmpty      : %d\r\n", res);
    res = FTest_MPMCQueue_u32_ConcurrentPushTake_SPSC (&global_arena, &scratch_arena); printf("FTest_MPMCQueue_u32_ConcurrentPushTake_SPSC : %d\r\n", res);
    res = FTest_MPMCQueue_u32_ConcurrentPushTake_MPSC (&global_arena, &scratch_arena); printf("FTest_MPMCQueue_u32_ConcurrentPushTake_MPSC : %d\r\n", res);
    res = FTest_MPMCQueue_u32_ConcurrentPushTake_SPMC (&global_arena, &scratch_arena); printf("FTest_MPMCQueue_u32_ConcurrentPushTake_SPMC : %d\r\n", res);
    res = FTest_MPMCQueue_u32_ConcurrentPushTake_MPMC (&global_arena, &scratch_arena); printf("FTest_MPMCQueue_u32_ConcurrentPushTake_MPMC : %d\r\n", res);

cleanup_and_exit:
    PAL_HostMemoryRelease(&scratch_alloc);
    PAL_HostMemoryRelease(&global_alloc);
    return exit_code;
}
