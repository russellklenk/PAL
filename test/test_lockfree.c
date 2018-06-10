/**
 * @summary Implement test routines for the concurrent queues in pal_thread.h.
 */
#include "pal_memory.h"
#include "pal_thread.h"

#include <process.h>
#include <stdio.h>

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

typedef unsigned int (__stdcall *Thread_Func)(void *thread_init);

typedef struct THREAD_POOL {
    pal_uint32_t        ActiveThreads;
    unsigned int       *OsThreadIds;
    HANDLE             *OsThreadHandle;
    HANDLE             *WorkerReady;
    HANDLE             *WorkerError;
    HANDLE              TerminateSignal;
    pal_uintptr_t       ContextData;
    void               *MemoryStart;
    pal_uint64_t        MemorySize;
} THREAD_POOL;

typedef struct THREAD_POOL_INIT {
    pal_uint32_t        ThreadCount;       /* The number of threads in the pool */
    Thread_Func        *ThreadEntry;       /* An array of ThreadCount entry points */
    pal_uintptr_t       ContextData;       /* Any data you want to be available to all threads */
    void               *MemoryStart;       /* The block of memory the thread pool will use for its data */
    pal_usize_t         MemorySize;        /* The size of the memory block pointed to by MemoryStart */
} THREAD_POOL_INIT;

typedef struct THREAD_INIT {
    THREAD_POOL        *ThreadPool;        /* The pool that owns the thread */
    HANDLE              ReadySignal;       /* Manual-reset event to be signaled by the thread when it is ready-to-run */
    HANDLE              ErrorSignal;       /* Manual-reset event to be signaled by the thread if an error occurs */
    HANDLE              TerminateSignal;   /* Manual-reset event to be signaled by the pool when it's time to shutdown */
    pal_uintptr_t       ContextData;       /* The value specified in THREAD_POOL_INIT::ContextData */
} THREAD_INIT;

typedef struct MPSC_STEAL_U16 {
    pal_uint32_t        LastWritePos;      /* The index of the last-written value. Updated by each producer. */
    pal_uint32_t        PublishCount;      /* */
    pal_uint32_t        ConsumeCount;      /* */
    pal_uint16_t        Data[32];          /* Storage. The oldest items are overwritten. Must be initialized with valid data. */
} MPSC_STEAL_U16;

typedef struct SPSC_QUEUE_DATA {
    PAL_MEMORY_ARENA   *GlobalArena;
    PAL_MEMORY_ARENA   *ScratchArena;
    PAL_SPSC_QUEUE_U32 *SharedQueue;
    HANDLE              GoSignal;
    HANDLE              DrainSignal;       /* Manual-reset event to be signaled by the pusher thread when it has finished all iterations */
    pal_uint32_t        IterationCount;
    pal_uint32_t        OpsPerIteration;
    pal_uint32_t      **CountsData;        /* An array of pointers to arrays of 32-bit unsigned integer values used to count how many times an item has been seen - one array per-thread */
} SPSC_QUEUE_DATA;

typedef struct SPMC_QUEUE_DATA {
    PAL_MEMORY_ARENA   *GlobalArena;
    PAL_MEMORY_ARENA   *ScratchArena;
    PAL_SPMC_QUEUE_U32 *SharedQueue;
    HANDLE              GoSignal;
    HANDLE              DrainSignal;       /* Manual-reset event to be signaled by the pusher thread when it has finished all iterations */
    pal_uint32_t        IterationCount;
    pal_uint32_t        OpsPerIteration;
    pal_uint32_t      **CountsData;        /* An array of pointers to arrays of 32-bit unsigned integer values used to count how many times an item has been seen - one array per-thread */
} SPMC_QUEUE_DATA;

typedef struct MPMC_QUEUE_DATA {
    PAL_MEMORY_ARENA   *GlobalArena;
    PAL_MEMORY_ARENA   *ScratchArena;
    PAL_MPMC_QUEUE_U32 *SharedQueue;
    HANDLE              GoSignal;
    HANDLE              DrainSignal;       /* Manual-reset event to be signaled by the pusher thread when it has finished all iterations */
    pal_uint32_t        IterationCount;
    pal_uint32_t        OpsPerIteration;
    pal_uint32_t      **CountsData;        /* An array of pointers to arrays of 32-bit unsigned integer values used to count how many times an item has been seen - one array per-thread */
} MPMC_QUEUE_DATA;

#if 0
static void
TST_MPSCStealPush_u16
(
    struct MPSC_STEAL_U16 *mpsc, 
    pal_uint16_t           item
)
{
    pal_uint32_t current_pos = mpsc->LastWritePos;
    pal_uint32_t   write_pos =(mpsc->LastWritePos + 1) & 31;
    pal_uint32_t       value;
    _ReadWriteBarrier();
    for ( ; ; )
    {   /* attempt to publish an item */
        mpsc->Data[write_pos] = item;
        /* update the last write position */
        if ((value =(pal_uint32_t)_InterlockedCompareExchange((volatile LONG*)&mpsc->LastWritePos, write_pos, current_pos)) == current_pos)
        {   /* the item was published successfully */
            mpsc->PublishCount++;
            break;
        }
        else
        {   /* failed - try again */
            current_pos = value;
            write_pos   =(value + 1) & 31;
        }
    }
}

static int
TST_MPSCStealTake_u16
(
    struct MPSC_STEAL_U16 *mpsc, 
    pal_uint16_t          *item
)
{
    pal_uint32_t publish_count = mpsc->PublishCount;
    pal_uint32_t consume_count = mpsc->ConsumeCount;
    pal_uint32_t   current_pos = mpsc->LastWritePos;
    pal_uint32_t         value;
    pal_uint16_t             v;
    _ReadWriteBarrier();

    if (publish_count > consume_count)
    {   /* there is at least one item available */
       *item = mpsc->Data[current_pos];
        mpsc->ConsumeCount = (publish_count-consume_count) > 32 : (publish_count-32) : (consume_count+1);
        return 1;
    }
    return 0;
}

static int
TST_SPMCQueuePush_u32
(
    struct PAL_SPMC_QUEUE_U32 *spmc, 
    pal_uint32_t               item
)
{
    pal_uint32_t *stor = spmc->Storage;     /* constant */
    pal_sint64_t  mask = spmc->StorageMask; /* constant */
    pal_sint64_t   pos = spmc->PrivatePos;  /* no concurrent take can happen */
    stor[pos & mask]   = item;
#if PAL_TARGET_ARCHITECTURE == PAL_ARCHITECTURE_X64 
    _ReadWriteBarrier();                    /* make sure item has been written before updating PrivatePos */
#else
    _MemoryBarrier();                       /* make sure item has been written before updating PrivatePos */
#endif
    spmc->PrivatePos = pos + 1;             /* make the new item visible */
    return 1;
}

static int
TST_SPMCQueueTake_u32
(
    struct PAL_SPMC_QUEUE_U32 *spmc, 
    pal_uint32_t              *item
)
{
    pal_uint32_t *stor = spmc->Storage;             /* constant */
    pal_sint64_t  mask = spmc->StorageMask;         /* constant */
    pal_sint64_t   pos = spmc->PrivatePos - 1;      /* no concurrent push operation can happen */
    pal_sint64_t   top = 0;
    int            res = 1;

    _InterlockedExchange64(&spmc->PrivatePos, pos); /* acts as a barrier */
    top = spmc->PublicPos;

    if (top <= pos)
    {   /* the deque is currently non-empty */
       *item = stor[pos & mask];
        if (top != pos)
        {   /* there's at least one more item in the deque - no need to race */
            return 1;
        }
        /* this was the final item in the deque - race a concurrent steal to claim it */
        if (_InterlockedCompareExchange64((volatile LONGLONG*) &spmc->PublicPos, top + 1, top) != top)
        {   /* this thread lost the race to a concurrent steal */
            res = 0;
        }
        spmc->PrivatePos = top + 1;
        return res;
    }
    else
    {   /* the deque is currently empty */
        spmc->PrivatePos = top;
        return 0;
    }
}

static int
TST_SPMCQueueSteal_u32
(
    struct PAL_SPMC_QUEUE_U32 *spmc, 
    pal_uint32_t              *item
)
{
    pal_uint32_t *stor = spmc->Storage;      /* constant */
    pal_sint64_t  mask = spmc->StorageMask;  /* constant */
    pal_sint64_t   top = spmc->PublicPos;
    _ReadWriteBarrier();
    pal_sint64_t   pos = spmc->PrivatePos;   /* read-only to see if queue is non-empty */
    if (top < pos)
    {   /* the deque is currently non-empty */
       *item = stor[top & mask];
        /* race with other threads to claim the item */
        if (_InterlockedCompareExchange64(&spmc->PublicPos, top + 1, top) == top)
        {   /* this thread won the race and claimed the item */
            return 1;
        }
    }
    return 0;
}
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

static unsigned int __stdcall
ThreadMain_Skeleton
(
    void *argp
)
{
    THREAD_INIT       *init =(THREAD_INIT*) argp;
    THREAD_POOL       *pool = init->ThreadPool;
    HANDLE             term = init->TerminateSignal;
    HANDLE            error = init->ErrorSignal;
    void           *context =(void*) init->ContextData;
    pal_uint32_t self_index = pool->ActiveThreads;
    unsigned int  exit_code = 0;

    PAL_UNUSED_LOCAL(pool);
    PAL_UNUSED_LOCAL(error);
    PAL_UNUSED_LOCAL(context);
    PAL_UNUSED_LOCAL(self_index);

    /* TODO: the thread should perform any initialization here.
     * call SetEvent(error) if anything fails during initialization.
     * call PAL_SetCurrentThreadName to give a name for the debugger.
     */

    /* indicate that the thread has initialized and is ready-to-run.
     * do not under any circumstances access init after this point.
     */
    SetEvent(init->ReadySignal); init = NULL;

    __try
    {
        for ( ; ; )
        {
            /* TODO: the thread should do its work here */
            if (WaitForSingleObject(term, 0) == WAIT_OBJECT_0)
            {   /* shutdown was signaled */
                break;
            }
        }
    }
    __finally
    {   /* TODO: the thread should perform cleanup here */
        return exit_code;
    }
}

static unsigned int __stdcall
ThreadMain_SPSCQueuePusher
(
    void *argp
)
{
    THREAD_INIT        *init =(THREAD_INIT*) argp;
    THREAD_POOL        *pool = init->ThreadPool;
    HANDLE              term = init->TerminateSignal;
    HANDLE             error = init->ErrorSignal;
    SPSC_QUEUE_DATA *context =(SPSC_QUEUE_DATA *) init->ContextData;
    PAL_SPSC_QUEUE_U32 *spsc = context->SharedQueue;
    HANDLE                go = context->GoSignal;
    HANDLE             drain = context->DrainSignal;
    HANDLE       wait_set[2] ={ go, term };
    DWORD            wait_rc = 0;
    unsigned int   exit_code = 0;
    pal_uint32_t  self_index = pool->ActiveThreads;
    pal_uint32_t     *counts = context->CountsData[self_index];
    pal_uint32_t        i, n;
    pal_uint32_t        j, m;

    PAL_UNUSED_LOCAL(pool);
    PAL_UNUSED_LOCAL(error);

    /* the thread should perform any initialization here.
     * call SetEvent(error) if anything fails during initialization.
     * call PAL_SetCurrentThreadName to give a name for the debugger.
     */
    PAL_SetCurrentThreadName("SPSCQueuePusher");
    PAL_ZeroMemory(counts, context->OpsPerIteration * sizeof(pal_uint32_t));

    /* indicate that the thread has initialized and is ready-to-run.
     * do not under any circumstances access init after this point.
     */
    SetEvent(init->ReadySignal); init = NULL;

    __try
    {
        if ((wait_rc = WaitForMultipleObjects(2, wait_set, FALSE, INFINITE)) == (WAIT_OBJECT_0+0))
        {   /* the race is on - all threads have been released */
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
        }
    }
    __finally
    {   /* perform cleanup here */
        SetEvent(drain);
    }

finish:
    return exit_code;
}

static unsigned int __stdcall
ThreadMain_SPSCQueueTaker
(
    void *argp
)
{
    THREAD_INIT        *init =(THREAD_INIT*) argp;
    THREAD_POOL        *pool = init->ThreadPool;
    HANDLE              term = init->TerminateSignal;
    HANDLE             error = init->ErrorSignal;
    SPSC_QUEUE_DATA *context =(SPSC_QUEUE_DATA *) init->ContextData;
    PAL_SPSC_QUEUE_U32 *spsc = context->SharedQueue;
    HANDLE                go = context->GoSignal;
    HANDLE             drain = context->DrainSignal;
    HANDLE       wait_set[2] ={ go, term };
    DWORD            wait_rc = 0;
    unsigned int   exit_code = 0;
    pal_uint32_t  self_index = pool->ActiveThreads;
    pal_uint32_t     *counts = context->CountsData[self_index];
    pal_uint32_t        item;

    PAL_UNUSED_LOCAL(pool);
    PAL_UNUSED_LOCAL(error);

    /* the thread should perform any initialization here.
     * call SetEvent(error) if anything fails during initialization.
     * call PAL_SetCurrentThreadName to give a name for the debugger.
     */
    PAL_SetCurrentThreadName("SPSCQueueTaker");
    PAL_ZeroMemory(counts, context->OpsPerIteration * sizeof(pal_uint32_t));

    /* indicate that the thread has initialized and is ready-to-run.
     * do not under any circumstances access init after this point.
     */
    SetEvent(init->ReadySignal); init = NULL;

    __try
    {
        if ((wait_rc = WaitForMultipleObjects(2, wait_set, FALSE, INFINITE)) == (WAIT_OBJECT_0+0))
        {   /* the race is on - all threads have been released */
            for ( ; ; )
            {
                if (PAL_SPSCQueueTake_u32(spsc, &item))
                {
                    counts[item]++;
                }
                else
                {   /* the queue is empty - has the pusher finished? */
                    if (WaitForSingleObject(drain, 0) == WAIT_OBJECT_0)
                    {   /* the queue has drained, we're done */
                        break;
                    }
                }
            }
        }
    }
    __finally
    {   /* the thread should perform cleanup here */
    }
    return exit_code;
}

static unsigned int __stdcall
ThreadMain_SPMCQueuePusher
(
    void *argp
)
{
    THREAD_INIT        *init =(THREAD_INIT*) argp;
    THREAD_POOL        *pool = init->ThreadPool;
    HANDLE              term = init->TerminateSignal;
    HANDLE             error = init->ErrorSignal;
    SPMC_QUEUE_DATA *context =(SPMC_QUEUE_DATA *) init->ContextData;
    PAL_SPMC_QUEUE_U32 *spmc = context->SharedQueue;
    HANDLE                go = context->GoSignal;
    HANDLE             drain = context->DrainSignal;
    HANDLE       wait_set[2] ={ go, term };
    DWORD            wait_rc = 0;
    unsigned int   exit_code = 0;
    pal_uint32_t  self_index = pool->ActiveThreads;
    pal_uint32_t     *counts = context->CountsData[self_index];
    pal_uint32_t        i, n;
    pal_uint32_t        j, m;

    PAL_UNUSED_LOCAL(pool);
    PAL_UNUSED_LOCAL(error);

    /* the thread should perform any initialization here.
     * call SetEvent(error) if anything fails during initialization.
     * call PAL_SetCurrentThreadName to give a name for the debugger.
     */
    PAL_SetCurrentThreadName("SPMCQueuePusher");
    PAL_ZeroMemory(counts, context->OpsPerIteration * sizeof(pal_uint32_t));

    /* indicate that the thread has initialized and is ready-to-run.
     * do not under any circumstances access init after this point.
     */
    SetEvent(init->ReadySignal); init = NULL;

    __try
    {
        if ((wait_rc = WaitForMultipleObjects(2, wait_set, FALSE, INFINITE)) == (WAIT_OBJECT_0+0))
        {   /* the race is on - all threads have been released */
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
        }
    }
    __finally
    {   /* perform cleanup here */
        SetEvent(drain);
    }

finish:
    return exit_code;
}

static unsigned int __stdcall
ThreadMain_SPMCQueuePushTaker
(
    void *argp
)
{
    THREAD_INIT        *init =(THREAD_INIT*) argp;
    THREAD_POOL        *pool = init->ThreadPool;
    HANDLE              term = init->TerminateSignal;
    HANDLE             error = init->ErrorSignal;
    SPMC_QUEUE_DATA *context =(SPMC_QUEUE_DATA *) init->ContextData;
    PAL_SPMC_QUEUE_U32 *spmc = context->SharedQueue;
    HANDLE                go = context->GoSignal;
    HANDLE             drain = context->DrainSignal;
    HANDLE       wait_set[2] ={ go, term };
    DWORD            wait_rc = 0;
    unsigned int   exit_code = 0;
    pal_uint32_t  self_index = pool->ActiveThreads;
    pal_uint32_t     *counts = context->CountsData[self_index];
    pal_uint32_t        item;
    pal_uint32_t        i, n;
    pal_uint32_t        j, m;

    PAL_UNUSED_LOCAL(pool);
    PAL_UNUSED_LOCAL(error);

    /* the thread should perform any initialization here.
     * call SetEvent(error) if anything fails during initialization.
     * call PAL_SetCurrentThreadName to give a name for the debugger.
     */
    PAL_SetCurrentThreadName("SPMCQueuePushTaker");
    PAL_ZeroMemory(counts, context->OpsPerIteration * sizeof(pal_uint32_t));

    /* indicate that the thread has initialized and is ready-to-run.
     * do not under any circumstances access init after this point.
     */
    SetEvent(init->ReadySignal); init = NULL;

    __try
    {
        if ((wait_rc = WaitForMultipleObjects(2, wait_set, FALSE, INFINITE)) == (WAIT_OBJECT_0+0))
        {   /* the race is on - all threads have been released */
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
        }
    }
    __finally
    {   /* the thread should perform cleanup here */
        SetEvent(drain);
    }

finish:
    return exit_code;
}

static unsigned int __stdcall
ThreadMain_SPMCQueueStealer
(
    void *argp
)
{
    THREAD_INIT        *init =(THREAD_INIT*) argp;
    THREAD_POOL        *pool = init->ThreadPool;
    HANDLE              term = init->TerminateSignal;
    HANDLE             error = init->ErrorSignal;
    SPMC_QUEUE_DATA *context =(SPMC_QUEUE_DATA *) init->ContextData;
    PAL_SPMC_QUEUE_U32 *spmc = context->SharedQueue;
    HANDLE                go = context->GoSignal;
    HANDLE             drain = context->DrainSignal;
    HANDLE       wait_set[2] ={ go, term };
    DWORD            wait_rc = 0;
    unsigned int   exit_code = 0;
    pal_uint32_t  self_index = pool->ActiveThreads;
    pal_uint32_t     *counts = context->CountsData[self_index];
    pal_uint32_t        item;

    PAL_UNUSED_LOCAL(pool);
    PAL_UNUSED_LOCAL(error);

    /* the thread should perform any initialization here.
     * call SetEvent(error) if anything fails during initialization.
     * call PAL_SetCurrentThreadName to give a name for the debugger.
     */
    PAL_SetCurrentThreadName("SPMCQueueStealer");
    PAL_ZeroMemory(counts, context->OpsPerIteration * sizeof(pal_uint32_t));

    /* indicate that the thread has initialized and is ready-to-run.
     * do not under any circumstances access init after this point.
     */
    SetEvent(init->ReadySignal); init = NULL;

    __try
    {
        if ((wait_rc = WaitForMultipleObjects(2, wait_set, FALSE, INFINITE)) == (WAIT_OBJECT_0+0))
        {   /* the race is on - all threads have been released */
            for ( ; ; )
            {
                if (PAL_SPMCQueueSteal_u32(spmc, &item))
                {
                    counts[item]++;
                }
                else
                {   /* the queue is empty - has the pusher finished? */
                    if (WaitForSingleObject(drain, 0) == WAIT_OBJECT_0)
                    {   /* the queue has drained, we're done */
                        break;
                    }
                }
            }
        }
    }
    __finally
    {   /* the thread should perform cleanup here */
    }
    return exit_code;
}

static unsigned int __stdcall
ThreadMain_MPMCQueuePusher
(
    void *argp
)
{
    THREAD_INIT        *init =(THREAD_INIT*) argp;
    THREAD_POOL        *pool = init->ThreadPool;
    HANDLE              term = init->TerminateSignal;
    HANDLE             error = init->ErrorSignal;
    MPMC_QUEUE_DATA *context =(MPMC_QUEUE_DATA *) init->ContextData;
    PAL_MPMC_QUEUE_U32 *mpmc = context->SharedQueue;
    HANDLE                go = context->GoSignal;
    HANDLE             drain = context->DrainSignal;
    HANDLE       wait_set[2] ={ go, term };
    DWORD            wait_rc = 0;
    unsigned int   exit_code = 0;
    pal_uint32_t  self_index = pool->ActiveThreads;
    pal_uint32_t     *counts = context->CountsData[self_index];
    pal_uint32_t        i, n;
    pal_uint32_t        j, m;

    PAL_UNUSED_LOCAL(pool);
    PAL_UNUSED_LOCAL(error);

    /* the thread should perform any initialization here.
     * call SetEvent(error) if anything fails during initialization.
     * call PAL_SetCurrentThreadName to give a name for the debugger.
     */
    PAL_SetCurrentThreadName("MPMCQueuePusher");
    PAL_ZeroMemory(counts, context->OpsPerIteration * sizeof(pal_uint32_t));

    /* indicate that the thread has initialized and is ready-to-run.
     * do not under any circumstances access init after this point.
     */
    SetEvent(init->ReadySignal); init = NULL;

    __try
    {
        if ((wait_rc = WaitForMultipleObjects(2, wait_set, FALSE, INFINITE)) == (WAIT_OBJECT_0+0))
        {   /* the race is on - all threads have been released */
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
        }
    }
    __finally
    {   /* perform cleanup here */
        SetEvent(drain);
    }

finish:
    return exit_code;
}

static unsigned int __stdcall
ThreadMain_MPMCQueueTaker
(
    void *argp
)
{
    THREAD_INIT        *init =(THREAD_INIT*) argp;
    THREAD_POOL        *pool = init->ThreadPool;
    HANDLE              term = init->TerminateSignal;
    HANDLE             error = init->ErrorSignal;
    MPMC_QUEUE_DATA *context =(MPMC_QUEUE_DATA *) init->ContextData;
    PAL_MPMC_QUEUE_U32 *mpmc = context->SharedQueue;
    HANDLE                go = context->GoSignal;
    HANDLE             drain = context->DrainSignal;
    HANDLE       wait_set[2] ={ go, term };
    DWORD            wait_rc = 0;
    unsigned int   exit_code = 0;
    pal_uint32_t  self_index = pool->ActiveThreads;
    pal_uint32_t     *counts = context->CountsData[self_index];
    pal_uint32_t        item;

    PAL_UNUSED_LOCAL(pool);
    PAL_UNUSED_LOCAL(error);

    /* the thread should perform any initialization here.
     * call SetEvent(error) if anything fails during initialization.
     * call PAL_SetCurrentThreadName to give a name for the debugger.
     */
    PAL_SetCurrentThreadName("MPMCQueueTaker");
    PAL_ZeroMemory(counts, context->OpsPerIteration * sizeof(pal_uint32_t));

    /* indicate that the thread has initialized and is ready-to-run.
     * do not under any circumstances access init after this point.
     */
    SetEvent(init->ReadySignal); init = NULL;

    __try
    {
        if ((wait_rc = WaitForMultipleObjects(2, wait_set, FALSE, INFINITE)) == (WAIT_OBJECT_0+0))
        {   /* the race is on - all threads have been released */
            for ( ; ; )
            {
                if (PAL_MPMCQueueTake_u32(mpmc, &item))
                {
                    counts[item]++;
                }
                else
                {   /* the queue is empty - has the pusher finished? */
                    if (WaitForSingleObject(drain, 0) == WAIT_OBJECT_0)
                    {   /* the queue has drained, we're done */
                        break;
                    }
                }
            }
        }
    }
    __finally
    {   /* the thread should perform cleanup here */
    }
    return exit_code;
}

static pal_usize_t
APP__ThreadPoolQueryMemorySize
(
    pal_uint32_t thread_count
)
{
    pal_usize_t required_size = 0;
    required_size += PAL_AllocationSizeArray(unsigned int, thread_count); /* OsThreadIds */
    required_size += PAL_AllocationSizeArray(HANDLE      , thread_count); /* OsThreadHandle */
    required_size += PAL_AllocationSizeArray(HANDLE      , thread_count); /* WorkerReady */
    required_size += PAL_AllocationSizeArray(HANDLE      , thread_count); /* WorkerError */
    return required_size;
}

static int
APP__ThreadPoolLaunch
(
    struct THREAD_POOL      *pool, 
    struct THREAD_POOL_INIT *init
)
{
    PAL_MEMORY_ARENA           arena;
    PAL_MEMORY_ARENA_INIT arena_init;
    pal_usize_t        required_size;
    pal_uint32_t       cleanup_count;
    pal_uint32_t                i, n;
    HANDLE                      term;

    PAL_ZeroMemory(pool  , sizeof(THREAD_POOL));
    PAL_ZeroMemory(&arena, sizeof(PAL_MEMORY_ARENA));

    cleanup_count  = 0;
    required_size  = APP__ThreadPoolQueryMemorySize(init->ThreadCount);
    if (init->MemoryStart == NULL || init->MemorySize < required_size)
    {   assert(init->MemoryStart != NULL);
        assert(init->MemorySize  >= required_size);
        return -1;
    }

    if ((term = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {
        goto cleanup_and_fail;
    }

    arena_init.AllocatorName = __FUNCTION__;
    arena_init.AllocatorType = PAL_MEMORY_ALLOCATOR_TYPE_HOST;
    arena_init.MemoryStart   =(pal_uint64_t) init->MemoryStart;
    arena_init.MemorySize    =(pal_uint64_t) init->MemorySize;
    arena_init.UserData      = NULL;
    arena_init.UserDataSize  = 0;
    if (PAL_MemoryArenaCreate(&arena, &arena_init) != 0)
    {
        goto cleanup_and_fail;
    }
    PAL_ZeroMemory(init->MemoryStart, init->MemorySize);

    pool->ActiveThreads   = 0;
    pool->OsThreadIds     = PAL_MemoryArenaAllocateHostArray(&arena, unsigned int, init->ThreadCount);
    pool->OsThreadHandle  = PAL_MemoryArenaAllocateHostArray(&arena, HANDLE      , init->ThreadCount);
    pool->WorkerReady     = PAL_MemoryArenaAllocateHostArray(&arena, HANDLE      , init->ThreadCount);
    pool->WorkerError     = PAL_MemoryArenaAllocateHostArray(&arena, HANDLE      , init->ThreadCount);
    pool->TerminateSignal = term;
    pool->ContextData     = init->ContextData;
    pool->MemoryStart     = init->MemoryStart;
    pool->MemorySize      = init->MemorySize;

    for (i = 0, n = init->ThreadCount; i < n; ++i)
    {
        HANDLE           thread = NULL;
        HANDLE            ready = NULL;
        HANDLE            error = NULL;
        unsigned int  thread_id = 0;
        DWORD           wait_rc = 0;
        HANDLE      wait_set[2];
        THREAD_INIT thread_init;

        if ((ready = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
        {
            goto cleanup_and_fail;
        }
        if ((error = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
        {
            CloseHandle(ready);
            goto cleanup_and_fail;
        }

        thread_init.ThreadPool      = pool;
        thread_init.ReadySignal     = ready;
        thread_init.ErrorSignal     = error;
        thread_init.TerminateSignal = term;
        thread_init.ContextData     = init->ContextData;
        if ((thread = (HANDLE)_beginthreadex(NULL, 0, init->ThreadEntry[i], &thread_init, 0, &thread_id)) == NULL)
        {
            CloseHandle(error);
            CloseHandle(ready);
            goto cleanup_and_fail;
        }

        pool->OsThreadIds   [pool->ActiveThreads] = thread_id;
        pool->OsThreadHandle[pool->ActiveThreads] = thread;
        pool->WorkerReady   [pool->ActiveThreads] = ready;
        pool->WorkerError   [pool->ActiveThreads] = error;
        cleanup_count++;

        wait_set[0]  = ready;
        wait_set[1]  = error;
        if ((wait_rc = WaitForMultipleObjects(2, wait_set, FALSE, INFINITE)) != (WAIT_OBJECT_0+0))
        {   /* error was signaled or wait was aborted */
            goto cleanup_and_fail;
        }

        /* this thread has been successfully initialized */
        pool->ActiveThreads++;
    }
    return 0;

cleanup_and_fail:
    if (cleanup_count > 0)
    {
        SetEvent(term);
        WaitForMultipleObjects(pool->ActiveThreads, pool->OsThreadHandle, TRUE, INFINITE);
        for (i = 0; i < cleanup_count; ++i)
        {
            CloseHandle(pool->WorkerError[i]);
            CloseHandle(pool->WorkerReady[i]);
            CloseHandle(pool->OsThreadHandle[i]);
        }
    }
    PAL_ZeroMemory(pool, sizeof(THREAD_POOL));
    CloseHandle(term);
    return -1;
}

static void
APP__ThreadPoolTerminate
(
    struct THREAD_POOL *pool
)
{
    if (pool->ActiveThreads > 0)
    {
        pal_uint32_t i, n;
        SetEvent(pool->TerminateSignal);
        WaitForMultipleObjects(pool->ActiveThreads, pool->OsThreadHandle, TRUE, INFINITE);
        for (i = 0, n = pool->ActiveThreads; i < n; ++i)
        {
            CloseHandle(pool->WorkerError[i]);
            CloseHandle(pool->WorkerReady[i]);
            CloseHandle(pool->OsThreadHandle[i]);
        }
        pool->ActiveThreads = 0;
    }
    if (pool->TerminateSignal)
    {
        CloseHandle(pool->TerminateSignal);
        pool->TerminateSignal = NULL;
    }
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
    HANDLE                            go = NULL;
    HANDLE                         drain = NULL;
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_SPSC_QUEUE_INIT             init;
    PAL_SPSC_QUEUE_U32              spsc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, j;
    pal_uint32_t              *counts[2]; /* dimension must match NUM_THREADS */
    Thread_Func                 entry[2]; /* dimension must match NUM_THREADS */
    THREAD_POOL                     pool;
    THREAD_POOL_INIT           pool_init;
    SPSC_QUEUE_DATA            work_data;
    int                           result;

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create two manual reset events to coordinate threads */
    if ((go = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   assert(0 && "CreateEvent(go) failed");
        result = TEST_FAIL;
        goto cleanup;
    }
    if ((drain = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   assert(0 && "CreateEvent(drain) failed");
        result = TEST_FAIL;
        goto cleanup;
    }

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
    work_data.GoSignal        = go;
    work_data.DrainSignal     = drain;
    work_data.IterationCount  = NUM_ITERS;
    work_data.OpsPerIteration = OPS_PER_ITER;
    work_data.CountsData      = counts;

    /* create a thread pool with two threads - one pusher and one taker */
    entry[0] = ThreadMain_SPSCQueuePusher;
    entry[1] = ThreadMain_SPSCQueueTaker;
    required_size = APP__ThreadPoolQueryMemorySize(NUM_THREADS);
    pool_init.ThreadCount = NUM_THREADS;
    pool_init.ThreadEntry = entry;
    pool_init.ContextData =(pal_uintptr_t) &work_data;
    pool_init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    pool_init.MemorySize  = required_size;
    if (APP__ThreadPoolLaunch(&pool, &pool_init) != 0)
    {   assert(0 && "Failed to launch the thread pool");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* let everyone run */
    SetEvent(go);

    /* wait until all threads exit */
    WaitForMultipleObjects(NUM_THREADS, pool.OsThreadHandle, TRUE, INFINITE);

    /* terminate the thread pool (this will return immediately) */
    APP__ThreadPoolTerminate(&pool);

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
    if (drain) CloseHandle(drain);
    if (go) CloseHandle(go);
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
    HANDLE                            go = NULL;
    HANDLE                         drain = NULL;
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_SPMC_QUEUE_INIT             init;
    PAL_SPMC_QUEUE_U32              spmc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, j;
    pal_uint32_t              *counts[2]; /* dimension must match NUM_THREADS */
    Thread_Func                 entry[2]; /* dimension must match NUM_THREADS */
    THREAD_POOL                     pool;
    THREAD_POOL_INIT           pool_init;
    SPMC_QUEUE_DATA            work_data;
    int                           result;

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create two manual reset events to coordinate threads */
    if ((go = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   assert(0 && "CreateEvent(go) failed");
        result = TEST_FAIL;
        goto cleanup;
    }
    if ((drain = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   assert(0 && "CreateEvent(drain) failed");
        result = TEST_FAIL;
        goto cleanup;
    }

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
    work_data.GoSignal        = go;
    work_data.DrainSignal     = drain;
    work_data.IterationCount  = NUM_ITERS;
    work_data.OpsPerIteration = OPS_PER_ITER;
    work_data.CountsData      = counts;

    /* create a thread pool with two threads - one pusher and one stealer */
    entry[0] = ThreadMain_SPMCQueuePusher;
    entry[1] = ThreadMain_SPMCQueueStealer;
    required_size = APP__ThreadPoolQueryMemorySize(NUM_THREADS);
    pool_init.ThreadCount = NUM_THREADS;
    pool_init.ThreadEntry = entry;
    pool_init.ContextData =(pal_uintptr_t) &work_data;
    pool_init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    pool_init.MemorySize  = required_size;
    if (APP__ThreadPoolLaunch(&pool, &pool_init) != 0)
    {   assert(0 && "Failed to launch the thread pool");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* let everyone run */
    SetEvent(go);

    /* wait until all threads exit */
    WaitForMultipleObjects(NUM_THREADS, pool.OsThreadHandle, TRUE, INFINITE);

    /* terminate the thread pool (this will return immediately) */
    APP__ThreadPoolTerminate(&pool);

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
    if (drain) CloseHandle(drain);
    if (go) CloseHandle(go);
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
    HANDLE                            go = NULL;
    HANDLE                         drain = NULL;
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_SPMC_QUEUE_INIT             init;
    PAL_SPMC_QUEUE_U32              spmc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, j;
    pal_uint32_t              *counts[8]; /* dimension must match NUM_THREADS */
    Thread_Func                 entry[8]; /* dimension must match NUM_THREADS */
    THREAD_POOL                     pool;
    THREAD_POOL_INIT           pool_init;
    SPMC_QUEUE_DATA            work_data;
    int                           result;

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create two manual reset events to coordinate threads */
    if ((go = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   assert(0 && "CreateEvent(go) failed");
        result = TEST_FAIL;
        goto cleanup;
    }
    if ((drain = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   assert(0 && "CreateEvent(drain) failed");
        result = TEST_FAIL;
        goto cleanup;
    }

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
    work_data.GoSignal        = go;
    work_data.DrainSignal     = drain;
    work_data.IterationCount  = NUM_ITERS;
    work_data.OpsPerIteration = OPS_PER_ITER;
    work_data.CountsData      = counts;

    /* create a thread pool with eight threads - one pusher and seven stealers */
    entry[0] = ThreadMain_SPMCQueuePusher;
    entry[1] = ThreadMain_SPMCQueueStealer;
    entry[2] = ThreadMain_SPMCQueueStealer;
    entry[3] = ThreadMain_SPMCQueueStealer;
    entry[4] = ThreadMain_SPMCQueueStealer;
    entry[5] = ThreadMain_SPMCQueueStealer;
    entry[6] = ThreadMain_SPMCQueueStealer;
    entry[7] = ThreadMain_SPMCQueueStealer;
    required_size = APP__ThreadPoolQueryMemorySize(NUM_THREADS);
    pool_init.ThreadCount = NUM_THREADS;
    pool_init.ThreadEntry = entry;
    pool_init.ContextData =(pal_uintptr_t) &work_data;
    pool_init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    pool_init.MemorySize  = required_size;
    if (APP__ThreadPoolLaunch(&pool, &pool_init) != 0)
    {   assert(0 && "Failed to launch the thread pool");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* let everyone run */
    SetEvent(go);

    /* wait until all threads exit */
    WaitForMultipleObjects(NUM_THREADS, pool.OsThreadHandle, TRUE, INFINITE);

    /* terminate the thread pool (this will return immediately) */
    APP__ThreadPoolTerminate(&pool);

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
    if (drain) CloseHandle(drain);
    if (go) CloseHandle(go);
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
    HANDLE                            go = NULL;
    HANDLE                         drain = NULL;
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_SPMC_QUEUE_INIT             init;
    PAL_SPMC_QUEUE_U32              spmc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, j;
    pal_uint32_t              *counts[2]; /* dimension must match NUM_THREADS */
    Thread_Func                 entry[2]; /* dimension must match NUM_THREADS */
    THREAD_POOL                     pool;
    THREAD_POOL_INIT           pool_init;
    SPMC_QUEUE_DATA            work_data;
    int                           result;

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create two manual reset events to coordinate threads */
    if ((go = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   assert(0 && "CreateEvent(go) failed");
        result = TEST_FAIL;
        goto cleanup;
    }
    if ((drain = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   assert(0 && "CreateEvent(drain) failed");
        result = TEST_FAIL;
        goto cleanup;
    }

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
    work_data.GoSignal        = go;
    work_data.DrainSignal     = drain;
    work_data.IterationCount  = NUM_ITERS;
    work_data.OpsPerIteration = OPS_PER_ITER;
    work_data.CountsData      = counts;

    /* create a thread pool with two threads - one pusher and one stealer */
    entry[0] = ThreadMain_SPMCQueuePushTaker;
    entry[1] = ThreadMain_SPMCQueueStealer;
    required_size = APP__ThreadPoolQueryMemorySize(NUM_THREADS);
    pool_init.ThreadCount = NUM_THREADS;
    pool_init.ThreadEntry = entry;
    pool_init.ContextData =(pal_uintptr_t) &work_data;
    pool_init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    pool_init.MemorySize  = required_size;
    if (APP__ThreadPoolLaunch(&pool, &pool_init) != 0)
    {   assert(0 && "Failed to launch the thread pool");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* let everyone run */
    SetEvent(go);

    /* wait until all threads exit */
    WaitForMultipleObjects(NUM_THREADS, pool.OsThreadHandle, TRUE, INFINITE);

    /* terminate the thread pool (this will return immediately) */
    APP__ThreadPoolTerminate(&pool);

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
    if (drain) CloseHandle(drain);
    if (go) CloseHandle(go);
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
    HANDLE                            go = NULL;
    HANDLE                         drain = NULL;
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_SPMC_QUEUE_INIT             init;
    PAL_SPMC_QUEUE_U32              spmc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, j;
    pal_uint32_t              *counts[8]; /* dimension must match NUM_THREADS */
    Thread_Func                 entry[8]; /* dimension must match NUM_THREADS */
    THREAD_POOL                     pool;
    THREAD_POOL_INIT           pool_init;
    SPMC_QUEUE_DATA            work_data;
    int                           result;

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create two manual reset events to coordinate threads */
    if ((go = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   assert(0 && "CreateEvent(go) failed");
        result = TEST_FAIL;
        goto cleanup;
    }
    if ((drain = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   assert(0 && "CreateEvent(drain) failed");
        result = TEST_FAIL;
        goto cleanup;
    }

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
    work_data.GoSignal        = go;
    work_data.DrainSignal     = drain;
    work_data.IterationCount  = NUM_ITERS;
    work_data.OpsPerIteration = OPS_PER_ITER;
    work_data.CountsData      = counts;

    /* create a thread pool with eight threads - one pusher and seven stealers */
    entry[0] = ThreadMain_SPMCQueuePushTaker;
    entry[1] = ThreadMain_SPMCQueueStealer;
    entry[2] = ThreadMain_SPMCQueueStealer;
    entry[3] = ThreadMain_SPMCQueueStealer;
    entry[4] = ThreadMain_SPMCQueueStealer;
    entry[5] = ThreadMain_SPMCQueueStealer;
    entry[6] = ThreadMain_SPMCQueueStealer;
    entry[7] = ThreadMain_SPMCQueueStealer;
    required_size = APP__ThreadPoolQueryMemorySize(NUM_THREADS);
    pool_init.ThreadCount = NUM_THREADS;
    pool_init.ThreadEntry = entry;
    pool_init.ContextData =(pal_uintptr_t) &work_data;
    pool_init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    pool_init.MemorySize  = required_size;
    if (APP__ThreadPoolLaunch(&pool, &pool_init) != 0)
    {   assert(0 && "Failed to launch the thread pool");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* let everyone run */
    SetEvent(go);

    /* wait until all threads exit */
    WaitForMultipleObjects(NUM_THREADS, pool.OsThreadHandle, TRUE, INFINITE);

    /* terminate the thread pool (this will return immediately) */
    APP__ThreadPoolTerminate(&pool);

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
    if (drain) CloseHandle(drain);
    if (go) CloseHandle(go);
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
    HANDLE                            go = NULL;
    HANDLE                         drain = NULL;
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_MPMC_QUEUE_INIT             init;
    PAL_MPMC_QUEUE_U32              mpmc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, j;
    pal_uint32_t              *counts[2]; /* dimension must match NUM_THREADS */
    Thread_Func                 entry[2]; /* dimension must match NUM_THREADS */
    THREAD_POOL                     pool;
    THREAD_POOL_INIT           pool_init;
    MPMC_QUEUE_DATA            work_data;
    int                           result;

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create two manual reset events to coordinate threads */
    if ((go = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   assert(0 && "CreateEvent(go) failed");
        result = TEST_FAIL;
        goto cleanup;
    }
    if ((drain = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   assert(0 && "CreateEvent(drain) failed");
        result = TEST_FAIL;
        goto cleanup;
    }

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
    work_data.GoSignal        = go;
    work_data.DrainSignal     = drain;
    work_data.IterationCount  = NUM_ITERS;
    work_data.OpsPerIteration = OPS_PER_ITER;
    work_data.CountsData      = counts;

    /* create a thread pool with two threads - one pusher and one stealer */
    entry[0] = ThreadMain_MPMCQueuePusher;
    entry[1] = ThreadMain_MPMCQueueTaker;
    required_size = APP__ThreadPoolQueryMemorySize(NUM_THREADS);
    pool_init.ThreadCount = NUM_THREADS;
    pool_init.ThreadEntry = entry;
    pool_init.ContextData =(pal_uintptr_t) &work_data;
    pool_init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    pool_init.MemorySize  = required_size;
    if (APP__ThreadPoolLaunch(&pool, &pool_init) != 0)
    {   assert(0 && "Failed to launch the thread pool");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* let everyone run */
    SetEvent(go);

    /* wait until all threads exit */
    WaitForMultipleObjects(NUM_THREADS, pool.OsThreadHandle, TRUE, INFINITE);

    /* terminate the thread pool (this will return immediately) */
    APP__ThreadPoolTerminate(&pool);

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
    if (drain) CloseHandle(drain);
    if (go) CloseHandle(go);
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
    HANDLE                            go = NULL;
    HANDLE                         drain = NULL;
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_MPMC_QUEUE_INIT             init;
    PAL_MPMC_QUEUE_U32              mpmc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, j;
    pal_uint32_t              *counts[3]; /* dimension must match NUM_THREADS */
    Thread_Func                 entry[3]; /* dimension must match NUM_THREADS */
    THREAD_POOL                     pool;
    THREAD_POOL_INIT           pool_init;
    MPMC_QUEUE_DATA            work_data;
    int                           result;

    PAL_UNUSED_LOCAL(NUM_PUSHERS);
    PAL_UNUSED_LOCAL(NUM_TAKERS);

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create two manual reset events to coordinate threads */
    if ((go = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   assert(0 && "CreateEvent(go) failed");
        result = TEST_FAIL;
        goto cleanup;
    }
    if ((drain = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   assert(0 && "CreateEvent(drain) failed");
        result = TEST_FAIL;
        goto cleanup;
    }

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
    work_data.GoSignal        = go;
    work_data.DrainSignal     = drain;
    work_data.IterationCount  = NUM_ITERS;
    work_data.OpsPerIteration = OPS_PER_ITER;
    work_data.CountsData      = counts;

    /* create a thread pool with two threads - one pusher and one stealer */
    entry[0] = ThreadMain_MPMCQueuePusher;
    entry[1] = ThreadMain_MPMCQueuePusher;
    entry[2] = ThreadMain_MPMCQueueTaker;
    required_size = APP__ThreadPoolQueryMemorySize(NUM_THREADS);
    pool_init.ThreadCount = NUM_THREADS;
    pool_init.ThreadEntry = entry;
    pool_init.ContextData =(pal_uintptr_t) &work_data;
    pool_init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    pool_init.MemorySize  = required_size;
    if (APP__ThreadPoolLaunch(&pool, &pool_init) != 0)
    {   assert(0 && "Failed to launch the thread pool");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* let everyone run */
    SetEvent(go);

    /* wait until all threads exit */
    WaitForMultipleObjects(NUM_THREADS, pool.OsThreadHandle, TRUE, INFINITE);

    /* terminate the thread pool (this will return immediately) */
    APP__ThreadPoolTerminate(&pool);

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
    if (drain) CloseHandle(drain);
    if (go) CloseHandle(go);
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
    HANDLE                            go = NULL;
    HANDLE                         drain = NULL;
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_MPMC_QUEUE_INIT             init;
    PAL_MPMC_QUEUE_U32              mpmc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, j;
    pal_uint32_t              *counts[3]; /* dimension must match NUM_THREADS */
    Thread_Func                 entry[3]; /* dimension must match NUM_THREADS */
    THREAD_POOL                     pool;
    THREAD_POOL_INIT           pool_init;
    MPMC_QUEUE_DATA            work_data;
    int                           result;

    PAL_UNUSED_LOCAL(NUM_PUSHERS);
    PAL_UNUSED_LOCAL(NUM_TAKERS);

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create two manual reset events to coordinate threads */
    if ((go = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   assert(0 && "CreateEvent(go) failed");
        result = TEST_FAIL;
        goto cleanup;
    }
    if ((drain = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   assert(0 && "CreateEvent(drain) failed");
        result = TEST_FAIL;
        goto cleanup;
    }

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
    work_data.GoSignal        = go;
    work_data.DrainSignal     = drain;
    work_data.IterationCount  = NUM_ITERS;
    work_data.OpsPerIteration = OPS_PER_ITER;
    work_data.CountsData      = counts;

    /* create a thread pool with two threads - one pusher and one stealer */
    entry[0] = ThreadMain_MPMCQueuePusher;
    entry[1] = ThreadMain_MPMCQueueTaker;
    entry[2] = ThreadMain_MPMCQueueTaker;
    required_size = APP__ThreadPoolQueryMemorySize(NUM_THREADS);
    pool_init.ThreadCount = NUM_THREADS;
    pool_init.ThreadEntry = entry;
    pool_init.ContextData =(pal_uintptr_t) &work_data;
    pool_init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    pool_init.MemorySize  = required_size;
    if (APP__ThreadPoolLaunch(&pool, &pool_init) != 0)
    {   assert(0 && "Failed to launch the thread pool");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* let everyone run */
    SetEvent(go);

    /* wait until all threads exit */
    WaitForMultipleObjects(NUM_THREADS, pool.OsThreadHandle, TRUE, INFINITE);

    /* terminate the thread pool (this will return immediately) */
    APP__ThreadPoolTerminate(&pool);

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
    if (drain) CloseHandle(drain);
    if (go) CloseHandle(go);
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
    HANDLE                            go = NULL;
    HANDLE                         drain = NULL;
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    PAL_MPMC_QUEUE_INIT             init;
    PAL_MPMC_QUEUE_U32              mpmc;
    pal_usize_t            required_size;
    pal_uint32_t                    i, j;
    pal_uint32_t              *counts[8]; /* dimension must match NUM_THREADS */
    Thread_Func                 entry[8]; /* dimension must match NUM_THREADS */
    THREAD_POOL                     pool;
    THREAD_POOL_INIT           pool_init;
    MPMC_QUEUE_DATA            work_data;
    int                           result;

    PAL_UNUSED_LOCAL(NUM_PUSHERS);
    PAL_UNUSED_LOCAL(NUM_TAKERS);

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    /* create two manual reset events to coordinate threads */
    if ((go = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   assert(0 && "CreateEvent(go) failed");
        result = TEST_FAIL;
        goto cleanup;
    }
    if ((drain = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   assert(0 && "CreateEvent(drain) failed");
        result = TEST_FAIL;
        goto cleanup;
    }

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
    work_data.GoSignal        = go;
    work_data.DrainSignal     = drain;
    work_data.IterationCount  = NUM_ITERS;
    work_data.OpsPerIteration = OPS_PER_ITER;
    work_data.CountsData      = counts;

    /* create a thread pool with two threads - one pusher and one stealer */
    entry[0] = ThreadMain_MPMCQueuePusher;
    entry[1] = ThreadMain_MPMCQueuePusher;
    entry[2] = ThreadMain_MPMCQueueTaker;
    entry[3] = ThreadMain_MPMCQueueTaker;
    entry[4] = ThreadMain_MPMCQueueTaker;
    entry[5] = ThreadMain_MPMCQueueTaker;
    entry[6] = ThreadMain_MPMCQueueTaker;
    entry[7] = ThreadMain_MPMCQueueTaker;
    required_size = APP__ThreadPoolQueryMemorySize(NUM_THREADS);
    pool_init.ThreadCount = NUM_THREADS;
    pool_init.ThreadEntry = entry;
    pool_init.ContextData =(pal_uintptr_t) &work_data;
    pool_init.MemoryStart = PAL_MemoryArenaAllocateHostArrayRaw(global_arena, sizeof(pal_uint8_t), PAL_CACHELINE_SIZE, required_size);
    pool_init.MemorySize  = required_size;
    if (APP__ThreadPoolLaunch(&pool, &pool_init) != 0)
    {   assert(0 && "Failed to launch the thread pool");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* let everyone run */
    SetEvent(go);

    /* wait until all threads exit */
    WaitForMultipleObjects(NUM_THREADS, pool.OsThreadHandle, TRUE, INFINITE);

    /* terminate the thread pool (this will return immediately) */
    APP__ThreadPoolTerminate(&pool);

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
    if (drain) CloseHandle(drain);
    if (go) CloseHandle(go);
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
