/**
 * @summary Implement test routines for the functionality provided by 
 * pal_task.h.
 */
#include <stddef.h>
#include <stdint.h>
#include <process.h>
#include <stdio.h>

#include "pal_memory.h"
#include "pal_thread.h"
#include "pal_task.h"
#include "pal_time.h"

#include "pal_win32_memory.c"
#include "pal_win32_thread.c"
#include "pal_win32_task.c"
#include "pal_win32_time.c"

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

/* @summary Define the values returned from various tests.
 */
#ifndef TEST_PASS_FAIL
#define TEST_PASS_FAIL
#define TEST_PASS                   1
#define TEST_FAIL                   0
#endif

/* @summary Define values passed to CreateTaskScheduler to force a single worker thread only, or allow the number of threads to be based on the number of logical processors.
 */
#ifndef FORCE_SINGLE_CORE
#define FORCE_SINGLE_CORE           1
#define ALLOW_MULTI_CORE            0
#endif

/* @summary Define values passed to CreateTaskScheduler to print or not print the host CPU attributes and scheduler configuration.
 */
#ifndef PRINT_CONFIGURATION
#define PRINT_CONFIGURATION         1
#define DONT_PRINT_CONFIGURATION    0
#endif

/* @summary Wrap a synchronization object that the host can wait on until another thread signals.
 */
typedef struct WIN32_SIGNAL {
    HANDLE                Event;   /* A manual-reset event. */
} WIN32_SIGNAL;

/* @summary Data associated with tasks that write timestamp values into an array to evaluate execution or completion order.
 */
typedef struct TIMESTAMP_TASK_DATA {
    struct WIN32_SIGNAL  *Signal;  /* The event to signal when TsCount reaches Trigger. */
    pal_uint64_t         *TsArray; /* The array of timestamp values. */
    pal_sint32_t         *TsCount; /* The number of timestamps written to TsArray. Shared! */
    pal_uint32_t          TsIndex; /* The zero-based index to write to in TsArray. */
    pal_sint32_t          Trigger; /* When TsCount reaches this value, signal the signal. */
} TIMESTAMP_TASK_DATA;

static int
CreateSignal
(
    struct WIN32_SIGNAL *signal
)
{
    if((signal->Event = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL) {
        signal->Event = NULL;
        return -1;
    }
    return 0;
}

static void
DeleteSignal
(
    struct WIN32_SIGNAL *signal
)
{
    CloseHandle(signal->Event);
    signal->Event = NULL;
}

static void
WaitSignal
(
    struct WIN32_SIGNAL *signal
)
{
    WaitForSingleObject(signal->Event, INFINITE);
}

static void
SignalSignal
(
    struct WIN32_SIGNAL *signal
)
{
    SetEvent(signal->Event);
}

static pal_sint32_t
AtomicIncrement32
(
    pal_sint32_t volatile *addr
)
{
    return (pal_sint32_t)_InterlockedIncrement((volatile LONG*) addr);
}

/* @summary Define the entry point or completion callback that writes the current timestamp into an array.
 * This can be used to evaluate the execution or completion order of a set of tasks.
 * @param args Data associated with task execution. TaskArguments points to a TIMESTAMP_TASK_DATA.
 */
static void
Task_WriteTimestamp
(
    struct PAL_TASK_ARGS *args
)
{
    TIMESTAMP_TASK_DATA   *argp  =(TIMESTAMP_TASK_DATA*) args->TaskArguments;
    pal_sint32_t          count;

    argp->TsArray[argp->TsIndex] = PAL_TimestampInTicks();
    printf("Task_WriteTimestamp(%u)\r\n", argp->TsIndex);
    if ((count = AtomicIncrement32(argp->TsCount)) == argp->Trigger) {
        printf("Task_WriteTimestamp(%u) triggers\r\n",argp->TsIndex);
        SignalSignal(argp->Signal);
    }
}

#if 0
static pal_uint32_t
ThreadMain_Sample
(
    struct PAL_THREAD_INIT *init
)
{
    void           *context =(void*) PAL_ThreadPoolGetPoolContext(init->ThreadPool);
    pal_uint32_t self_index = init->ThreadIndex;
    pal_uint32_t  exit_code = 0;

    PAL_SetThreadName("SampleThread");
    /* other simple init here */

    /* do work here */

finish:
    return exit_code;
}
#endif

/* @summary Define the the callback function invoked to perform initialization for a task system asynchronous I/O worker thread.
 * The callback should allocate any per-thread data it needs and return a pointer to that data in the out_thread_context parameter.
 * @param task_scheduler The task scheduler that owns the worker thread.
 * @param thread_task_pool The PAL_TASK_POOL allocated to the worker thread.
 * @param init_context Opaque data supplied when the worker thread pool was created.
 * @param out_thread_context On return, the function should update this value to point to any data to be associated with the thread.
 * @return Zero if initialization completes successfully, or -1 if initialization failed.
 */
static int
AioWorkerThreadInit
(
    struct PAL_TASK_SCHEDULER   *scheduler, 
    struct PAL_TASK_POOL *thread_task_pool, 
    void                     *init_context, 
    void              **out_thread_context
)
{
    PAL_UNUSED_ARG(scheduler);
    PAL_UNUSED_ARG(thread_task_pool);
    PAL_UNUSED_ARG(init_context);
    PAL_Assign(out_thread_context, NULL);
    return 0;
}

/* @summary Define the the callback function invoked to perform initialization for a task system CPU worker thread.
 * The callback should allocate any per-thread data it needs and return a pointer to that data in the out_thread_context parameter.
 * @param task_scheduler The task scheduler that owns the worker thread.
 * @param thread_task_pool The PAL_TASK_POOL allocated to the worker thread.
 * @param init_context Opaque data supplied when the worker thread pool was created.
 * @param out_thread_context On return, the function should update this value to point to any data to be associated with the thread.
 * @return Zero if initialization completes successfully, or -1 if initialization failed.
 */
static int
CpuWorkerThreadInit
(
    struct PAL_TASK_SCHEDULER   *scheduler, 
    struct PAL_TASK_POOL *thread_task_pool, 
    void                     *init_context, 
    void              **out_thread_context
)
{
    PAL_UNUSED_ARG(scheduler);
    PAL_UNUSED_ARG(thread_task_pool);
    PAL_UNUSED_ARG(init_context);
    PAL_Assign(out_thread_context, NULL);
    return 0;
}

/* @summary Perform all initialization neccessary to create a task scheduler instance.
 * @param print_config Specify non-zero to print the task scheduler configuration.
 * @return A pointer to the task scheduler instance, or NULL if an error occurred.
 */
static struct PAL_TASK_SCHEDULER*
CreateTaskScheduler
(
    int force_single_core, 
    int      print_config
)
{
    PAL_TASK_SCHEDULER_INIT scheduler_init;
    PAL_TASK_POOL_INIT       pool_types[3];
    PAL_CPU_INFO                  host_cpu;

    if (PAL_CpuInfoQuery(&host_cpu) != 0) {
        printf("ERROR: Failed to query host CPU information.\r\n");
        return NULL;
    }
   
    if (print_config) {
        printf("Using host CPU configuration:\r\n");
        printf("  NUMA Nodes       : %u\r\n"      , host_cpu.NumaNodes);
        printf("  Physical CPUs    : %u\r\n"      , host_cpu.PhysicalCPUs);
        printf("  Physical Cores   : %u\r\n"      , host_cpu.PhysicalCores);
        printf("  Hardware Threads : %u\r\n"      , host_cpu.HardwareThreads);
        printf("  Threads-per-Core : %u\r\n"      , host_cpu.ThreadsPerCore);
        printf("  L1 Cache Size    : %u bytes\r\n", host_cpu.CacheSizeL1);
        printf("  L1 Cacheline Size: %u bytes\r\n", host_cpu.CacheLineSizeL1);
        printf("  L2 Cache Size    : %u bytes\r\n", host_cpu.CacheSizeL2);
        printf("  L2 Cacheline Size: %u bytes\r\n", host_cpu.CacheLineSizeL2);
        printf("  Prefer AMD?      : %d\r\n"      , host_cpu.PreferAMD);
        printf("  Prefer Intel?    : %d\r\n"      , host_cpu.PreferIntel);
        printf("  Virtual Machine? : %d\r\n"      , host_cpu.IsVirtualMachine);
        printf("  CPU Vendor Name  : %s\r\n"      , host_cpu.VendorName);
        printf("  Force Single Core: %d\r\n"      , force_single_core);
        printf("\r\n");
    }

    /* there's only one main pool for the driver application thread */
    pool_types[0].PoolTypeId        = PAL_TASK_POOL_TYPE_ID_MAIN;
    pool_types[0].PoolCount         = 1;
    pool_types[0].PoolFlags         = PAL_TASK_POOL_FLAG_CREATE  | 
                                      PAL_TASK_POOL_FLAG_PUBLISH | 
                                      PAL_TASK_POOL_FLAG_EXECUTE | 
                                      PAL_TASK_POOL_FLAG_COMPLETE;
    pool_types[0].PreCommitTasks    = 1024;

    /* the AIO workers are intended to run on hyperthreads and not be very active */
    pool_types[1].PoolTypeId        = PAL_TASK_POOL_TYPE_ID_AIO_WORKER;
    pool_types[1].PoolCount         = host_cpu.HardwareThreads / host_cpu.ThreadsPerCore;
    pool_types[1].PoolFlags         = PAL_TASK_POOL_FLAG_CREATE  | 
                                      PAL_TASK_POOL_FLAG_PUBLISH | 
                                      PAL_TASK_POOL_FLAG_EXECUTE | 
                                      PAL_TASK_POOL_FLAG_COMPLETE;
    pool_types[1].PreCommitTasks    = 1024;

    /* the CPU workers are intended to run the vast majority of the work */
    if (host_cpu.PhysicalCores > 1 && force_single_core == 0) {
        /* running on a multi-core system */
        pool_types[2].PoolTypeId    = PAL_TASK_POOL_TYPE_ID_CPU_WORKER;
        pool_types[2].PoolCount     = host_cpu.HardwareThreads - 1;
        pool_types[1].PoolFlags     = PAL_TASK_POOL_FLAG_CREATE   | 
                                      PAL_TASK_POOL_FLAG_PUBLISH  | 
                                      PAL_TASK_POOL_FLAG_EXECUTE  | 
                                      PAL_TASK_POOL_FLAG_COMPLETE | 
                                      PAL_TASK_POOL_FLAG_STEAL;
        pool_types[2].PreCommitTasks= 1024;
    } else {
        /* running on a single-core system */
        pool_types[2].PoolTypeId    = PAL_TASK_POOL_TYPE_ID_CPU_WORKER;
        pool_types[2].PoolCount     = 1;
        pool_types[1].PoolFlags     = PAL_TASK_POOL_FLAG_CREATE   | 
                                      PAL_TASK_POOL_FLAG_PUBLISH  | 
                                      PAL_TASK_POOL_FLAG_EXECUTE  | 
                                      PAL_TASK_POOL_FLAG_COMPLETE | 
                                      PAL_TASK_POOL_FLAG_STEAL;
        pool_types[2].PreCommitTasks= 1024;
    }

    /* define the task scheduler configuration */
    scheduler_init.AioWorkerInitFunc    = AioWorkerThreadInit;
    scheduler_init.AioWorkerThreadCount = pool_types[1].PoolCount;
    scheduler_init.AioWorkerStackSize   = PAL_THREAD_STACK_SIZE_DEFAULT;
    scheduler_init.CpuWorkerInitFunc    = CpuWorkerThreadInit;
    scheduler_init.CpuWorkerThreadCount = pool_types[2].PoolCount;
    scheduler_init.CpuWorkerStackSize   = PAL_THREAD_STACK_SIZE_DEFAULT;
    scheduler_init.MaxAsyncIoRequests   = 1024;
    scheduler_init.PoolTypeCount        = PAL_CountOf(pool_types);
    scheduler_init.TaskPoolTypes        = pool_types;
    scheduler_init.CreateContext        = NULL;
    if (print_config) {
        printf("Task Scheduler Configuration:\r\n");
        printf("  AIO Worker Count : %u\r\n", scheduler_init.AioWorkerThreadCount);
        printf("  CPU Worker Count : %u\r\n", scheduler_init.CpuWorkerThreadCount);
        printf("  Max I/O Requests : %u\r\n", scheduler_init.MaxAsyncIoRequests);
        printf("  Pool Type Count  : %u\r\n", scheduler_init.PoolTypeCount);
        printf("\r\n");
    }
    return PAL_TaskSchedulerCreate(&scheduler_init);
}

/* @summary Shut down all worker threads and free resources associated with a task scheduler instance.
 * @param scheduler The PAL_TASK_SCHEDULER to delete.
 */
static void
DeleteTaskScheduler
(
    struct PAL_TASK_SCHEDULER *scheduler
)
{   /* calling thread is blocked until all workers exit */
    PAL_TaskSchedulerDelete(scheduler);
}

/* @summary Ensure that permit lists can be allocated and freed from the same thread.
 * @param global_arena The global application memory arena.
 * @param scratch_arena The application scratch memory arena.
 * @return Either TEST_PASS or TEST_FAIL.
 */
static int
FTest_PermitListAllocFree
(
    struct PAL_MEMORY_ARENA  *global_arena, 
    struct PAL_MEMORY_ARENA *scratch_arena
)
{
    pal_uint32_t const        LOOP_ITERS = PAL_PERMIT_LIST_CHUNK_SIZE / 16;
    pal_uint32_t              list_index = 0;
    PAL_TASK_SCHEDULER            *sched = NULL;
    PAL_TASK_POOL             *main_pool = NULL;
    PAL_PERMITS_LIST          *lists[16];
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    pal_uint32_t              i, j, n, x;
    int                           result;

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    if ((sched = CreateTaskScheduler(FORCE_SINGLE_CORE, 0)) == NULL) {
        assert(0 && "CreateTaskScheduler failed");
        result = TEST_FAIL;
        goto cleanup;
    }
    if ((main_pool = PAL_TaskSchedulerAcquireTaskPool(sched, PAL_TASK_POOL_TYPE_ID_MAIN, 0)) == NULL) {
        assert(0 && "PAL_TaskSchedulerAcquireTaskPool(MAIN) failed");
        result = TEST_FAIL;
        goto cleanup;
    }

    for (x = 0; x < 1000; ++x) {
        /* pass 1 allocates a chunk and fills it */
        for (i = 0; i < LOOP_ITERS; ++i) {
            if (PAL_TaskPoolAllocatePermitsLists(main_pool, lists, PAL_CountOf(lists)) != 0) {
                assert(0 && "PAL_TaskPoolAllocatePermitsLists failed");
                result = TEST_FAIL;
                goto cleanup;
            }
            /* set a piece of data uniquely identifying the list */
            for (j = 0 , n = PAL_CountOf(lists); j < n; ++j) {
                if (lists[j] == NULL) {
                    assert(lists[j] != NULL);
                    result = TEST_FAIL;
                    goto cleanup;
                }
                lists[j]->PoolIndex = (i * 16) + j;
            }
            /* delete each permits list, returning it to the free pool */
            for (j = 0 , n = PAL_CountOf(lists); j < n; ++j) {
                list_index = PAL_TaskPoolGetSlotIndexForPermitList(main_pool, lists[j]);
                PAL_TaskPoolMakePermitsListFree(main_pool, list_index);
            }
        }
        /* pass 2 allocates the same chunk and verifies it */
        for (i = 0; i < LOOP_ITERS; ++i) {
            if (PAL_TaskPoolAllocatePermitsLists(main_pool, lists, PAL_CountOf(lists)) != 0) {
                assert(0 && "PAL_TaskPoolAllocatePermitsLists failed");
                result = TEST_FAIL;
                goto cleanup;
            }
            /* set a piece of data uniquely identifying the list */
            for (j = 0 , n = PAL_CountOf(lists); j < n; ++j) {
                if (lists[j] == NULL) {
                    assert(lists[j] != NULL);
                    result = TEST_FAIL;
                    goto cleanup;
                }
                if (lists[j]->PoolIndex != ((i * 16) + j)) {
                    assert(lists[j]->PoolIndex == ((i * 16) + j) && "Permit Lists not recycled in expected order");
                    result = TEST_FAIL;
                    goto cleanup;
                }
            }
            /* delete each permits list, returning it to the free pool */
            for (j = 0 , n = PAL_CountOf(lists); j < n; ++j) {
                list_index = PAL_TaskPoolGetSlotIndexForPermitList(main_pool, lists[j]);
                PAL_TaskPoolMakePermitsListFree(main_pool, list_index);
            }
        }
    }

cleanup:
    DeleteTaskScheduler(sched);
    PAL_MemoryArenaResetToMarker(scratch_arena, scratch_mark);
    PAL_MemoryArenaResetToMarker(global_arena, global_mark);
    return result;
}

/* @summary Create and run a single task through to completion. The task is set to autocomplete.
 * @param global_arena The global application memory arena.
 * @param scratch_arena The application scratch memory arena.
 * @return Either TEST_PASS or TEST_FAIL.
 */
static int
FTest_RunOneThread_NoDeps_Auto
(
    struct PAL_MEMORY_ARENA  *global_arena, 
    struct PAL_MEMORY_ARENA *scratch_arena
)
{   /* this test exercises the lifecycle of a single task.
     */
    WIN32_SIGNAL                  signal = {0};
    TIMESTAMP_TASK_DATA       *task_args = NULL;
    PAL_TASK                       *task = NULL;
    PAL_TASK_SCHEDULER            *sched = NULL;
    PAL_TASK_POOL             *main_pool = NULL;
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    pal_uint64_t           timestamps[1];
    pal_usize_t           task_args_size;
    pal_sint32_t          complete_count;
    PAL_TASKID                   task_id;
    int                           result;

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    if ((sched = CreateTaskScheduler(FORCE_SINGLE_CORE, 0)) == NULL) {
        assert(0 && "CreateTaskScheduler failed");
        result = TEST_FAIL;
        goto cleanup;
    }
    if ((main_pool = PAL_TaskSchedulerAcquireTaskPool(sched, PAL_TASK_POOL_TYPE_ID_MAIN, 0)) == NULL) {
        assert(0 && "PAL_TaskSchedulerAcquireTaskPool(MAIN) failed");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* zero the timestamps array */
    PAL_ZeroMemory(timestamps, sizeof(timestamps));
    if (CreateSignal(&signal) != 0) {
        assert(0 && "CreateSignal failed");
        result = TEST_FAIL;
        goto cleanup;
    }
    complete_count = 0;

    /* step 1 is to create a task ID */
    if (PAL_TaskCreate(main_pool, &task_id, 1, PAL_TASKID_NONE) != 0) {
        assert(0 && "PAL_TaskCreate failed");
        result = TEST_FAIL;
        goto cleanup;
    }
    if (PAL_TaskIdGetValid(task_id) == 0) {
        assert(0 && "PAL_TaskCreate returned an invalid task ID");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* step 2 performs task setup.
     * PAL_TaskGetData returns three pieces of information to you:
     * 1. A pointer to the PAL_TASK, which you must fill out.
     * 2. A pointer to the task-local data buffer, which you can use to specify per-task data.
     * 3. The maximum number of bytes that can be written to the task-local data buffer.
     */
    if ((task = PAL_TaskGetData(main_pool, task_id, &task_args, &task_args_size)) == NULL) {
        assert(0 && "PAL_TaskGetData failed");
        result = TEST_FAIL;
        goto cleanup;
    }
    if (task_args == NULL || task_args_size == 0) {
        assert(0 && "PAL_TaskGetData returned invalid task-local buffer");
        result = TEST_FAIL;
        goto cleanup;
    }
    /* ALL fields of PAL_TASK must be set by your code.
     * PAL_TASK::TaskMain specifies the function to run when the task executes.
     * PAL_TASK::TaskId specifies the task identifier being assigned to the task.
     * PAL_TASK::ParentId specifies the task identifier of the parent task. 
     * The ParentId field is -not- set for you during PAL_TaskCreate; you must set it here.
     * PAL_TASK::TaskFlags is reserved for future use and should be set to zero.
     * PAL_TASK::CompletionType indicates how the task will be completed. Usually you set this 
     * to PAL_TASK_COMPLETION_TYPE_AUTOMATIC, which means that the task system will automatically
     * call PAL_TaskComplete after TaskMain returns. You can also set it to either one of 
     * PAL_TASK_COMPLETION_TYPE_INTERNAL, which means that your TaskMain calls PAL_TaskComplete, 
     * or PAL_TASK_COMPLETION_TYPE_EXTERNAL, which means that some callback outside of the task 
     * system will call PAL_TaskComplete based on some event, such as an asynchronous I/O event.
     */
    task->TaskMain       = PAL_TaskMain_NoOp;
    task->TaskComplete   = Task_WriteTimestamp;
    task->TaskId         = task_id;         /* must be the ID supplied to PAL_TaskGetData */
    task->ParentId       = PAL_TASKID_NONE; /* must be the ID supplied as parent_id in PAL_TaskCreate */
    task->CompletionType = PAL_TASK_COMPLETION_TYPE_AUTOMATIC;
    task->TaskFlags      = 0;
    task_args->Signal    =&signal;
    task_args->TsArray   = timestamps;
    task_args->TsCount   =&complete_count;
    task_args->TsIndex   = 0;
    task_args->Trigger   = 1;

    /* step 3 publishes the task, which potentially makes the task ready-to-run.
     * a task becomes ready-to-run once all of its dependencies have completed. 
     * dependencies allow you to specify a general ordering in which tasks will 
     * run; for example, if task A computes some data that task B uses, you would 
     * make task B dependent on task A, which guarantees that task B will not start
     * running before task A completes.
     */
    if (PAL_TaskPublish(main_pool, &task_id, 1, NULL, 0) != 0) {
        assert(0 && "PAL_TaskPublish failed");
        result = TEST_FAIL;
        goto cleanup;
    }

    WaitSignal(&signal);

cleanup:
    DeleteSignal(&signal);
    DeleteTaskScheduler(sched);
    PAL_MemoryArenaResetToMarker(scratch_arena, scratch_mark);
    PAL_MemoryArenaResetToMarker(global_arena, global_mark);
    return result;
}

/* @summary Create and run a two tasks, a parent and a child, through to completion.
 * The tasks are set to autocomplete. The child should always finish before the parent.
 * @param global_arena The global application memory arena.
 * @param scratch_arena The application scratch memory arena.
 * @return Either TEST_PASS or TEST_FAIL.
 */
static int
FTest_RunOneThread_NoDeps_WithExternChild_Auto
(
    struct PAL_MEMORY_ARENA  *global_arena, 
    struct PAL_MEMORY_ARENA *scratch_arena
)
{
    WIN32_SIGNAL                  signal = {0};
    TIMESTAMP_TASK_DATA       *task_args = NULL;
    PAL_TASK                *parent_task = NULL;
    PAL_TASK                 *child_task = NULL;
    PAL_TASK_SCHEDULER            *sched = NULL;
    PAL_TASK_POOL             *main_pool = NULL;
    PAL_MEMORY_ARENA_MARKER  global_mark;
    PAL_MEMORY_ARENA_MARKER scratch_mark;
    pal_uint64_t           timestamps[2]; /* 0 = PARENT, 1 = CHILD */
    pal_usize_t           task_args_size;
    pal_sint32_t          complete_count;
    PAL_TASKID            parent_task_id;
    PAL_TASKID             child_task_id;
    int                           result;

    pal_uint32_t const        PARENT = 0;
    pal_uint32_t const         CHILD = 1;
    pal_uint32_t const    WAIT_COUNT = 2;

    result       = TEST_PASS;
    global_mark  = PAL_MemoryArenaMark(global_arena);
    scratch_mark = PAL_MemoryArenaMark(scratch_arena);

    if ((sched = CreateTaskScheduler(FORCE_SINGLE_CORE, 0)) == NULL) {
        assert(0 && "CreateTaskScheduler failed");
        result = TEST_FAIL;
        goto cleanup;
    }
    if ((main_pool = PAL_TaskSchedulerAcquireTaskPool(sched, PAL_TASK_POOL_TYPE_ID_MAIN, 0)) == NULL) {
        assert(0 && "PAL_TaskSchedulerAcquireTaskPool(MAIN) failed");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* initialize the timestamps array to zero */
    PAL_ZeroMemory(timestamps, sizeof(timestamps));
    if (CreateSignal(&signal) != 0) {
        assert(0 && "CreateSignal failed");
        result = TEST_FAIL;
        goto cleanup;
    }
    complete_count = 0;

    /* step 1 is to create a task ID for the parent task */
    if (PAL_TaskCreate(main_pool, &parent_task_id, 1, PAL_TASKID_NONE) != 0) {
        assert(0 && "PAL_TaskCreate failed (parent)");
        result = TEST_FAIL;
        goto cleanup;
    }
    if (PAL_TaskIdGetValid(parent_task_id) == 0) {
        assert(0 && "PAL_TaskCreate returned an invalid task ID (parent)");
        result = TEST_FAIL;
        goto cleanup;
    }
    /* then do the same for the child task */
    if (PAL_TaskCreate(main_pool, &child_task_id, 1, parent_task_id) != 0) {
        assert(0 && "PAL_TaskCreate failed (child)");
        result = TEST_FAIL;
        goto cleanup;
    }
    if (PAL_TaskIdGetValid(child_task_id) == 0) {
        assert(0 && "PAL_TaskCreate returned an invalid task ID (child)");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* perform task setup for the parent task */
    if ((parent_task = PAL_TaskGetData(main_pool, parent_task_id, &task_args, &task_args_size)) == NULL) {
        assert(0 && "PAL_TaskGetData failed (parent)");
        result = TEST_FAIL;
        goto cleanup;
    }
    parent_task->TaskMain       = PAL_TaskMain_NoOp;
    parent_task->TaskComplete   = Task_WriteTimestamp;
    parent_task->TaskId         = parent_task_id;  /* must be the ID supplied to PAL_TaskGetData */
    parent_task->ParentId       = PAL_TASKID_NONE; /* must be the ID supplied as parent_id in PAL_TaskCreate */
    parent_task->CompletionType = PAL_TASK_COMPLETION_TYPE_AUTOMATIC;
    parent_task->TaskFlags      = 0;
    task_args->Signal           =&signal;
    task_args->TsArray          = timestamps;
    task_args->TsCount          =&complete_count;
    task_args->TsIndex          = PARENT;
    task_args->Trigger          = WAIT_COUNT;

    /* publish the parent task, making it ready to run.
     * for this test, the parent will run before the child.
     */
    if (PAL_TaskPublish(main_pool, &parent_task_id, 1, NULL, 0) != 0) {
        assert(0 && "PAL_TaskPublish failed (child)");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* even though the parent task has been publish, and may have already run, 
     * it should not finish until the child task has finished. set up the child.
     */
    if ((child_task = PAL_TaskGetData(main_pool, child_task_id, &task_args, &task_args_size)) == NULL) {
        assert(0 && "PAL_TaskGetData failed (child)");
        result = TEST_FAIL;
        goto cleanup;
    }
    child_task->TaskMain       = PAL_TaskMain_NoOp;
    child_task->TaskComplete   = Task_WriteTimestamp;
    child_task->TaskId         = child_task_id;   /* must be the ID supplied to PAL_TaskGetData */
    child_task->ParentId       = parent_task_id;  /* must be the ID supplied as parent_id in PAL_TaskCreate */
    child_task->CompletionType = PAL_TASK_COMPLETION_TYPE_AUTOMATIC;
    child_task->TaskFlags      = 0;
    task_args->Signal           =&signal;
    task_args->TsArray          = timestamps;
    task_args->TsCount          =&complete_count;
    task_args->TsIndex          = CHILD;
    task_args->Trigger          = WAIT_COUNT;
    /* publish the child task to allow it to run */
    if (PAL_TaskPublish(main_pool, &child_task_id, 1, NULL, 0) != 0) {
        assert(0 && "PAL_TaskPublish failed (child)");
        result = TEST_FAIL;
        goto cleanup;
    }

    /* wait for all tasks to complete */
    WaitSignal(&signal);

    /* verify that the CHILD completed before the PARENT */
    if (timestamps[CHILD] >= timestamps[PARENT]) {
        assert(timestamps[CHILD] < timestamps[PARENT]);
        result = TEST_FAIL;
        goto cleanup;
    }

cleanup:
    DeleteSignal(&signal);
    DeleteTaskScheduler(sched);
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
    pal_usize_t                  global_size = Megabytes(16);
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
    res = FTest_PermitListAllocFree                     (&global_arena, &scratch_arena); printf("FTest_PermitListAllocFree                     : %d\r\n", res);
    res = FTest_RunOneThread_NoDeps_Auto                (&global_arena, &scratch_arena); printf("FTest_RunOneThread_NoDeps_Auto                : %d\r\n", res);
    res = FTest_RunOneThread_NoDeps_WithExternChild_Auto(&global_arena, &scratch_arena); printf("FTest_RunOneThread_NoDeps_WithExternChild_Auto: %d\r\n", res);

cleanup_and_exit:
    PAL_HostMemoryRelease(&scratch_alloc);
    PAL_HostMemoryRelease(&global_alloc);
    return exit_code;
}
