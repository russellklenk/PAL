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

#include "pal_win32_memory.c"
#include "pal_win32_thread.c"
#include "pal_win32_task.c"

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

#ifndef TEST_PASS_FAIL
#define TEST_PASS_FAIL
#define TEST_PASS    1
#define TEST_FAIL    0
#endif

#ifndef FORCE_SINGLE_CORE
#define FORCE_SINGLE_CORE    1
#endif

#ifndef ALLOW_MULTI_CORE
#define ALLOW_MULTI_CORE     0
#endif

#ifndef PRINT_CONFIGURATION
#define PRINT_CONFIGURATION  1
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

static void
DeleteTaskScheduler
(
    struct PAL_TASK_SCHEDULER *scheduler
)
{   /* calling thread is blocked until all workers exit */
    PAL_TaskSchedulerDelete(scheduler);
}

int main
(
    int    argc, 
    char **argv
)
{
    PAL_TASK_SCHEDULER                *sched;
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

    PAL_TASK_POOL *main_pool = NULL;
    PAL_TASKID *id_list = NULL;
    pal_uint32_t i, j, n;
#if 0
    int                                  res;
#endif

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

    sched = CreateTaskScheduler(ALLOW_MULTI_CORE, PRINT_CONFIGURATION);
    id_list = PAL_MemoryArenaAllocateHostArray(&global_arena, PAL_TASKID, 1024);
    main_pool = PAL_TaskSchedulerAcquireTaskPool(sched, PAL_TASK_POOL_TYPE_ID_MAIN, 0);
    for (j = 0; j < 1024; ++j) {
        PAL_TaskCreate(main_pool, id_list, 1024);
        for (i = 0, n = 1024; i < n; ++i) {
            PAL_TaskDelete(main_pool, id_list[i]);
        }
    }
    DeleteTaskScheduler(sched);

    /* execute functional tests */
#if 0
    res = FTest_SPSCQueue_u32_PushTakeIsFIFO          (&global_arena, &scratch_arena); printf("FTest_SPSCQueue_u32_PushTakeIsFIFO          : %d\r\n", res);
#endif

cleanup_and_exit:
    PAL_HostMemoryRelease(&scratch_alloc);
    PAL_HostMemoryRelease(&global_alloc);
    return exit_code;
}
