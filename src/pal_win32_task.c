/**
 * @summary Implement the PAL task scheduler for the Win32 platform.
 */
#include "pal_win32_task.h"
#include "pal_win32_thread.h"
#include "pal_win32_memory.h"

/* @summary Define a structure for maintaining counts of the various pool types.
 */
typedef struct PAL_TASK_POOL_TYPE_COUNTS {
    pal_uint32_t MainThreadPoolCount; /* */
    pal_uint32_t AioWorkerPoolCount;  /* */
    pal_uint32_t CpuWorkerPoolCount;  /* */
    pal_uint32_t UserThreadPoolCount; /* */
    pal_uint32_t StealPoolCount;      /* */
    pal_uint32_t TotalPoolCount;      /* */
} PAL_TASK_POOL_TYPE_COUNTS;

/* @summary Implement a default thread initialization function.
 * @param task_scheduler The task scheduler that owns the worker thread.
 * @param thread_task_pool The PAL_TASK_POOL allocated to the worker thread.
 * @param init_context Opaque data supplied when the worker thread pool was created.
 * @param thread_context On return, the function should update this value to point to any data to be associated with the thread.
 * @return Zero if initialization completes successfully, or -1 if initialization failed.
 */
static int
PAL_DefaultTaskWorkerInit
(
    struct PAL_TASK_SCHEDULER *task_scheduler, 
    struct PAL_TASK_POOL    *thread_task_pool, 
    pal_uintptr_t                init_context, 
    pal_uintptr_t             *thread_context
)
{
    PAL_UNUSED_ARG(task_scheduler);
    PAL_UNUSED_ARG(thread_task_pool);
    PAL_UNUSED_ARG(init_context);
    PAL_Assign(thread_context, 0);
    return 0;
}

/* @summary Compute the required size for a fully-committed PAL_TASK_POOL.
 * @param page_size The operating system page size, in bytes.
 * @return The number of bytes required to store the data for a PAL_TASK_POOL.
 */
static pal_usize_t
PAL_TaskPoolQueryMemorySize
(
    pal_usize_t page_size
)
{
    pal_usize_t required_size = 0;
    required_size  = PAL_AllocationSizeType (PAL_TASK_POOL);
    required_size += PAL_AllocationSizeArray(pal_uint64_t , PAL_TASKID_MAX_TASK_POOLS / 64);
    required_size  = PAL_AlignUp(required_size, page_size); /* user data */
    required_size += PAL_AllocationSizeArray(pal_uint16_t , PAL_TASKID_MAX_SLOTS_PER_POOL);
    required_size += PAL_AllocationSizeArray(PAL_TASKID   , PAL_TASKID_MAX_SLOTS_PER_POOL);
    required_size += PAL_AllocationSizeArray(PAL_TASK_DATA, PAL_TASKID_MAX_SLOTS_PER_POOL);
    required_size  = PAL_AlignUp(required_size, page_size); /* pools are page-aligned */
    return required_size;
}

/* @summary Compute the required memory reservation size for a PAL_TASK_SCHEDULER.
 * @param counts The set of task pool counts to update.
 * @param init The PAL_TASK_SCHEDULER_INIT defining the scheduler attributes.
 * @param page_size The operating system page size, in bytes.
 * @param pool_size The reservation size for a single PAL_TASK_POOL, in bytes.
 * @return The number of bytes required to store the task scheduler data.
 */
static pal_usize_t
PAL_TaskSchedulerQueryMemorySize
(
    struct PAL_TASK_POOL_TYPE_COUNTS *counts, 
    struct PAL_TASK_SCHEDULER_INIT     *init, 
    pal_usize_t                    page_size, 
    pal_usize_t                    pool_size
)
{
    pal_usize_t required_size = 0;
    pal_uint32_t   main_count = 0;
    pal_uint32_t    aio_count = 0;
    pal_uint32_t    cpu_count = 0;
    pal_uint32_t   user_count = 0;
    pal_uint32_t  steal_count = 0;
    pal_uint32_t         i, n;

    /* figure out how many of each pool type are required */
    for (i = 0, n  = init->PoolTypeCount; i < n; ++i) {
        switch (init->TaskPoolTypes[i].PoolTypeId) {
            case PAL_TASK_POOL_TYPE_ID_MAIN:
                main_count += init->TaskPoolTypes[i].PoolCount;
                break;
            case PAL_TASK_POOL_TYPE_ID_AIO_WORKER:
                aio_count  += init->TaskPoolTypes[i].PoolCount;
                break;
            case PAL_TASK_POOL_TYPE_ID_CPU_WORKER:
                cpu_count  += init->TaskPoolTypes[i].PoolCount;
                break;
            default:
                user_count += init->TaskPoolTypes[i].PoolCount;
                break;
        }
        if (init->TaskPoolTypes[i].PoolFlags & PAL_TASK_POOL_FLAG_STEAL) {
            steal_count += init->TaskPoolTypes[i].PoolCount;
        }
    }
    counts->MainThreadPoolCount =  main_count;
    counts->AioWorkerPoolCount  =   aio_count;
    counts->CpuWorkerPoolCount  =   cpu_count;
    counts->UserThreadPoolCount =  user_count;
    counts->StealPoolCount      = steal_count;
    counts->TotalPoolCount      =  main_count + aio_count + cpu_count + user_count;

    /* determine the memory requirement for the scheduler */
    required_size  = PAL_AllocationSizeType (PAL_TASK_SCHEDULER);
    required_size += PAL_AllocationSizeArray(PAL_TASK_POOL*         , counts->TotalPoolCount);
    required_size += PAL_AllocationSizeArray(PAL_TASK_POOL*         , counts->TotalPoolCount);
    required_size += PAL_AllocationSizeArray(pal_uint32_t           , counts->TotalPoolCount);
    required_size += PAL_AllocationSizeArray(PAL_TASK_POOL_FREE_LIST,   init->PoolTypeCount);
    required_size += PAL_AllocationSizeArray(pal_uint32_t           ,   init->PoolTypeCount);
    /* ... */
    required_size  = PAL_AlignUp(required_size , page_size);
    /* include the memory requirement for all task pools */
    required_size += pool_size * counts->TotalPoolCount;
    return required_size;
}

PAL_API(struct PAL_TASK_SCHEDULER*)
PAL_TaskSchedulerCreate
(
    struct PAL_TASK_SCHEDULER_INIT *init
)
{
    PAL_TASK_SCHEDULER              *scheduler = NULL;
    pal_uint8_t                      *base_ptr = NULL;
    pal_uint8_t                     *pool_base = NULL;
    pal_usize_t                  required_size = 0;
    pal_usize_t                    commit_size = 0;
    pal_usize_t                      pool_size = 0;
    pal_uint32_t                     page_size = 0;
    pal_uint32_t              steal_pool_index = 0;
    pal_uint32_t             global_pool_index = 0;
    pal_uint32_t               type_pool_index = 0;
    pal_uint32_t               type_pool_count = 0;
    pal_uint32_t                    type_index = 0;
    pal_uint32_t                    type_count = 0;
    DWORD                            error_res = ERROR_SUCCESS;
    SYSTEM_INFO                        sysinfo;
    PAL_MEMORY_ARENA               sched_arena;
    PAL_MEMORY_ARENA_INIT scheduler_arena_init;
    PAL_TASK_POOL_TYPE_COUNTS           counts;
    pal_uint32_t                          i, n;
    pal_uint32_t                          j, m;

    if (init->AioWorkerInitFunc == NULL && init->AioWorkerThreadCount > 0) {
        init->AioWorkerInitFunc  = PAL_DefaultTaskWorkerInit;
    }
    if (init->CpuWorkerInitFunc == NULL && init->CpuWorkerThreadCount > 0) {
        init->CpuWorkerInitFunc  = PAL_DefaultTaskWorkerInit;
    }

    GetNativeSystemInfo(&sysinfo); 
    page_size = sysinfo.dwPageSize;

    /* determine the amount of memory that has to be reserved and make the main allocation */
    pool_size     =  PAL_TaskPoolQueryMemorySize(page_size);
    required_size =  PAL_TaskSchedulerQueryMemorySize(&counts, init, page_size, pool_size);
    if ((base_ptr = (pal_uint8_t*) VirtualAlloc(NULL, required_size, MEM_RESERVE, PAGE_NOACCESS)) == NULL) {
        error_res =  GetLastError();
        goto cleanup_and_fail;
    }
    /* determine the amount of memory that has to be pre-committed */
    commit_size   =  PAL_AllocationSizeType (PAL_TASK_SCHEDULER);
    commit_size  +=  PAL_AllocationSizeArray(PAL_TASK_POOL *        , counts.TotalPoolCount);
    commit_size  +=  PAL_AllocationSizeArray(PAL_TASK_POOL *        , counts.TotalPoolCount);
    commit_size  +=  PAL_AllocationSizeArray(pal_uint32_t           , counts.TotalPoolCount);
    commit_size  +=  PAL_AllocationSizeArray(PAL_TASK_POOL_FREE_LIST,  init->PoolTypeCount);
    commit_size  +=  PAL_AllocationSizeArray(pal_uint32_t           ,  init->PoolTypeCount);
    /* ... */
    commit_size   =  PAL_AlignUp(commit_size, page_size);
    if (VirtualAlloc(base_ptr, commit_size, MEM_COMMIT, PAGE_READWRITE) != base_ptr) {
        error_res =  GetLastError();
        goto cleanup_and_fail;
    }

    /* create an arena for sub-allocating the scheduler data */
    scheduler_arena_init.AllocatorName = __FUNCTION__" Scheduler Arena";
    scheduler_arena_init.AllocatorType = PAL_MEMORY_ALLOCATOR_TYPE_HOST;
    scheduler_arena_init.MemoryStart   =(pal_uint64_t) base_ptr;
    scheduler_arena_init.MemorySize    =(pal_uint64_t) commit_size;
    scheduler_arena_init.UserData      = NULL;
    scheduler_arena_init.UserDataSize  = 0;
    if (PAL_MemoryArenaCreate(&scheduler_arena, &scheduler_arena_init) < 0) {
        error_res = ERROR_OUT_OF_MEMORY;
        goto cleanup_and_fail;
    }
    scheduler                = PAL_MemoryArenaAllocateHostType (&scheduler_arena, PAL_TASK_SCHEDULER);
    scheduler->StealPoolSet  = PAL_MemoryArenaAllocateHostArray(&scheduler_arena, PAL_TASK_POOL*, counts.TotalPoolCount);
    scheduler->TaskPoolList  = PAL_MemoryArenaAllocateHostArray(&scheduler_arena, PAL_TASK_POOL*, counts.TotalPoolCount);
    scheduler->PoolThreadId  = PAL_MemoryArenaAllocateHostArray(&scheduler_arena, pal_uint32_t  , counts.TotalPoolCount);
    scheduler->TaskPoolCount = counts.TotalPoolCount;
    scheduler->PoolTypeCount = init->PoolTypeCount;
    scheduler->PoolFreeList  = PAL_MemoryArenaAllocateHostArray(&scheduler_arena, PAL_TASK_POOL_FREE_LIST, init->PoolTypeCount);
    scheduler->PoolTypeIds   = PAL_MemoryArenaAllocateHostArray(&scheduler_arena, pal_uint32_t           , init->PoolTypeCount);
    scheduler->TaskPoolBase  = base_ptr + commit_size;
    pool_base                = base_ptr + commit_size;

    /* initialize the task pools */
    for (type_index = 0, type_count = init->PoolTypeCount; type_index < type_count; ++i) {
        PAL_TASK_POOL_INIT    *type =&init->TaskPoolTypes[type_index];
        pal_uint32_t     chunk_size = 1024;
        pal_uint32_t    chunk_count =(type->PreCommitTasks + (chunk_size-1)) / chunk_size;
        pal_uint32_t      user_size = 0;
        pal_uint32_     user_offset = 0;

        /* determine the commit size for this pool type */
        commit_size  = PAL_AllocationSizeType (PAL_TASK_POOL);
        commit_size += PAL_AllocationSizeArray(pal_uint64_t , PAL_TASKID_MAX_TASK_POOLS / 64);
        user_offset  =(pal_uint32_t) commit_size;
        commit_size  = PAL_AlignUp(commit_size,page_size);
        user_size    =(pal_uint32_t)(commit_size - user_offset);
        commit_size += PAL_AllocationSizeArray(pal_uint16_t , PAL_TASKID_MAX_SLOTS_PER_POOL);
        commit_size += PAL_AllocationSizeArray(PAL_TASKID   , PAL_TASKID_MAX_SLOTS_PER_POOL);
        commit_size += PAL_AllocationSizeArray(PAL_TASK_DATA, chunk_count * chunk_size);
        commit_size  = PAL_AlignUp(commit_size,page_size);

        /* initialize the free list for this type */
        scheduler->PoolFreeList[type_index].FreeListHead  = NULL;
        scheduler->PoolFreeList[type_index].PoolTypeId    = type->PoolTypeId;
        scheduler->PoolFreeList[type_index].PoolTypeFree  = 0;
        scheduler->PoolFreeList[type_index].PoolTypeTotal = type->PoolCount;
        scheduler->PoolFreeList[type_index].PoolTypeFlags = type->PoolFlags;
        scheduler->PoolTypeIds [type_index] = type->PoolTypeId;

        /* create all task pools of this type */
        for (type_pool_index = 0, type_pool_count = type->PoolCount; type_pool_index < type_pool_count; ++type_pool_index, ++global_pool_index) {
            pal_uint8_t                *pool_addr;
            PAL_TASK_POOL                   *pool;
            PAL_MEMORY_ARENA           pool_arena;
            PAL_MEMORY_ARENA_INIT pool_arena_init;

            /* commit the required memory */
            pool_addr = pool_base + (pool_index * pool_size);
            if (VirtualAlloc(pool_addr, commit_size, MEM_COMMIT, PAGE_READWRITE) == NULL) {
                error_res =  GetLastError();
                goto cleanup_and_fail;
            }
            /* create an arena to sub-allocate from the committed space */
            pool_arena_init.AllocatorName = __FUNCTION__" Pool Arena";
            pool_arena_init.AllocatorType = PAL_MEMORY_ALLOCATOR_TYPE_HOST;
            pool_arena_init.MemoryStart   =(pal_uint64_t) pool_addr;
            pool_arena_init.MemorySize    =(pal_uint64_t) commit_size;
            pool_arena_init.UserData      = NULL;
            pool_arena_init.UserDataSize  = 0;
            if (PAL_MemoryArenaCreate(&pool_arena, &pool_arena_init) < 0) {
                error_res = ERROR_OUT_OF_MEMORY;
                goto cleanup_and_fail;
            }
            /* finally allocate and initialize the task pool.
             * note that the order of allocations here matter. */
            pool = PAL_MemoryArenaAllocateHostType (&pool_arena, PAL_TASK_POOL);
            pool->TaskScheduler     = scheduler;
            pool->NextFreePool      = scheduler->PoolFreeList[type_index].FreeListHead;
            pool->StealBitSet       = PAL_MemoryArenaAllocateHostArray(&pool_arena, pal_uint64_t , PAL_TASKID_MAX_TASK_POOLS / 64);
            pool->UserDataBuffer    = PAL_MemoryArenaAllocateHostArray(&pool_arena, pal_uint8_t  , user_size);
            pool->AllocSlotIds      = PAL_MemoryArenaAllocateHostArray(&pool_arena, pal_uint16_t , PAL_TASKID_MAX_SLOTS_PER_POOL);
            pool->ReadyTaskIds      = PAL_MemoryArenaAllocateHostArray(&pool_arena, PAL_TASKID   , PAL_TASKID_MAX_SLOTS_PER_POOL);
            pool->TaskSlotData      = PAL_MemoryArenaAllocateHostArray(&pool_arena, PAL_TASK_DATA, chunk_count * chunk_size);
            pool->CommitCount       = chunk_count;
            pool->PoolTypeId        = type->PoolTypeId;
            pool->OsThreadId        = 0;
            pool->PoolIndex         = global_pool_index;
            pool->PoolFlags         = type->PoolFlags;
            pool->UserDataSize      = user_size;
            pool->StealPoolSet      = scheduler->StealPoolSet;
            pool->StealWordMask     = 1ULL << (global_pool_index & 63); /* using 64-bit words */
            pool->StealWordIndex    = global_pool_index / 64;           /* using 64-bit words */
            pool->StealPoolCount    = counts.StealPoolCount;
            pool->StealPublishIndex =(global_pool_index + 1) % steal_pool_count;
            pool->StealConsumeIndex = 0;
            pool->AllocCount        = 0;
            pool->AllocNext         = 0;
            pool->Reserved8         = 0;
            pool->FreeCount         = chunk_count * chunk_size;
            pool->ReadyPublicPos    = 0;
            pool->ReadyPrivatePos   = 0;
            /* update the scheduler pool binding table */
            scheduler->TaskPoolList[global_pool_index] = pool;
            scheduler->PoolThreadId[global_pool_index] = 0;
            /* push the task pool onto the free list */
            scheduler->PoolFreeList[type_index].FreeListHead = pool;
            scheduler->PoolFreeList[type_index].PoolTypeFree++;
            /* append the pool to the steal pool set if necessary */
        }
    }

cleanup_and_fail:
    if (base_address) {
        VirtualFree(base_address, 0, MEM_RELEASE);
    }
    return -1;
}

PAL_API(void)
PAL_TaskSchedulerDelete
(
    struct PAL_TASK_SCHEDULER *scheduler
)
{
}

