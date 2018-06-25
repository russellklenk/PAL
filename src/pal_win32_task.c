/**
 * @summary Implement the PAL task scheduler for the Win32 platform.
 */
#include "pal_win32_task.h"
#include "pal_win32_thread.h"
#include "pal_win32_memory.h"

#include <process.h>

#ifndef PAL_COMPLETION_KEY_SHUTDOWN
#   define PAL_COMPLETION_KEY_SHUTDOWN    PAL_TASKID_NONE
#endif

#ifndef PAL_COMPLETION_KEY_TASKID
#   define PAL_COMPLETION_KEY_TASKID     (~(ULONG_PTR) 1)
#endif

#ifdef PAL_TASKID_SHUTDOWN
#   define PAL_TASKID_SHUTDOWN            PAL_TASKID_NONE
#endif

#ifndef PAL_TASK_CHUNK_SIZE
#   define PAL_TASK_CHUNK_SIZE            1024U
#endif

/* @summary Define the signature of a thread entry point.
 */
typedef unsigned int (__stdcall *Win32_ThreadMain_Func)(void*);

/* @summary Define a structure for maintaining counts of the various pool types.
 */
typedef struct PAL_TASK_POOL_TYPE_COUNTS {
    pal_uint32_t               MainThreadPoolCount; /* The number of task pools with the PAL_TASK_POOL_TYPE_ID_MAIN PoolTypeId. */
    pal_uint32_t               AioWorkerPoolCount;  /* The number of task pools with the PAL_TASK_POOL_TYPE_ID_AIO_WORKER PoolTypeId. */
    pal_uint32_t               CpuWorkerPoolCount;  /* The number of task pools with the PAL_TASK_POOL_TYPE_ID_CPU_WORKER PoolTypeId. */
    pal_uint32_t               UserThreadPoolCount; /* The number of task pools with PoolTypeId other than the three listed above. */
    pal_uint32_t               TotalPoolCount;      /* The total number of task pools requested by the application. */
} PAL_TASK_POOL_TYPE_COUNTS;

/* @summary Define the data associated with the memory requirements for a PAL_TASK_SCHEDULER.
 */
typedef struct PAL_TASK_SCHEDULER_SIZE_INFO {
    pal_uint64_t               ReserveSize;         /* The number of bytes of virtual memory to reserve. */
    pal_uint32_t               CommitSize;          /* The number of bytes of virtual memory to commit up-front. */
    pal_uint32_t               TaskPoolOffset;      /* The byte offset of the start of the task pool data from the allocation base. */
} PAL_TASK_SCHEDULER_SIZE_INFO;

/* @summary Define the data associated with the memory requirements for a PAL_TASK_POOL.
 */
typedef struct PAL_TASK_POOL_SIZE_INFO {
    pal_uint32_t               ReserveSize;         /* The number of bytes of virtual memory to reserve. */
    pal_uint32_t               CommitSize;          /* The number of bytes of virtual memory to commit up-front, not including the per-pool-type commitment. */
    pal_uint32_t               UserDataSize;        /* The size of the user data area, in bytes. */
    pal_uint32_t               UserDataOffset;      /* The byte offset of the start of the user data area from the task pool base address. */
} PAL_TASK_POOL_SIZE_INFO;

/* @summary Define the data passed to a CPU worker thread at startup.
 * Note that this data is only valid until the thread sets either the ReadySignal or ErrorSignal events.
 */
typedef struct PAL_CPU_WORKER_THREAD_INIT {
    struct PAL_TASK_SCHEDULER *TaskScheduler;       /* The PAL_TASK_SCHEDULER that owns the worker thread. */
    PAL_TaskWorkerInit_Func    ThreadInit;          /* The user-supplied function used to perform any application-specific per-thread initialization. */
    void                      *CreateContext;       /* Opaque data supplied by the application to be passed through to the ThreadInit function. */
    HANDLE                     ReadySignal;         /* A manual-reset event to be signaled when the thread has successfully initialized. */
    HANDLE                     ErrorSignal;         /* A manual-reset event to be signaled when the thread has failed during startup. */
    HANDLE                     ParkSemaphore;       /* The semaphore handle that should be assigned to the PAL_TASK_POOL acquired by the worker thread. */
} PAL_CPU_WORKER_THREAD_INIT;

/* @summary Define the data passed to an I/O worker thread at startup.
 * Note that this data is only valid until the thread sets either the ReadySignal or ErrorSignal events.
 */
typedef struct PAL_AIO_WORKER_THREAD_INIT {
    struct PAL_TASK_SCHEDULER *TaskScheduler;       /* The PAL_TASK_SCHEDULER that owns the worker thread. */
    PAL_TaskWorkerInit_Func    ThreadInit;          /* The user-supplied function used to perform any application-specific per-thread initialization. */
    void                      *CreateContext;       /* Opaque data supplied by the application to be passed through to the ThreadInit function. */
    HANDLE                     ReadySignal;         /* A manual-reset event to be signaled when the thread has successfully initialized. */
    HANDLE                     ErrorSignal;         /* A manual-reset event to be signaled when the thread has failed during startup. */
    HANDLE                     CompletionPort;      /* The I/O completion port to monitor for work and completion notifications. */
} PAL_AIO_WORKER_THREAD_INIT;

typedef enum PAL_TASK_SCHEDULER_PARK_RESULT {
    PAL_TASK_SCHEDULER_PARK_RESULT_SHUTDOWN   =  0, /* The thread was woken up because the scheduler is being shutdown. */
    PAL_TASK_SCHEDULER_PARK_RESULT_TRY_STEAL  =  1, /* The scheduler produced a list of at least one pool with available tasks; the thread should attempt to steal some work. */
    PAL_TASK_SCHEDULER_PARK_RESULT_WAKE_TASK  =  2, /* The thread was woken up and should check the task pool's WakeupTaskId field for a task to execute. */
} PAL_TASK_SCHEDULER_PARK_RESULT;

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

#if 0
static int
PAL_TaskSchedulerWakeWorker /* to be called when a worker makes a task ready-to-run via publish or complete */
(
    struct PAL_TASK_SCHEDULER *scheduler, 
    struct PAL_TASK_POOL    *worker_pool, 
    PAL_TASKID                 give_task
)
{
    pal_uint32_t tos = scheduler->ParkedThreadTOS;
    pal_uint32_t idx;
    _ReadWriteBarrier();
    for ( ; ; ) {
        if (tos == 0) {
            /* should add to RTR queue and inc estimated count for pool idx and event count */
            scheduler->TaskPoolERTR[worker_pool->PoolIndex]++;
            PAL_TaskPoolPublishReadyTask(worker_pool, give_task);
            return NO_WORKERS_WAITING;
        }
        idx = scheduler->ParkedThreadIds[tos-1];
        if ((value = CAS(&scheduler->ParkedThreadTOS, tos-1, tos)) == tos) {
            /* successfully popped a worker, assign to mail slot */
            PAL_THREAD_POOL *woke_pool = scheduler->TaskPoolList[idx];
            woke_pool->WakeTaskId = give_task;
            PAL_SemaphorePostOne(&woke_pool->ParkSem);
            return WORKER_WOKEN;
        } else {
            /* CAS failed; try again */
            tos = value;
        }
    }
    /* waiting threads are maintained on a LIFO.
     * if any threads are waiting, pop one and assign it give_task.
     * then sem_post to wake it, and return a value indicating the task was assigned.
     * if no threads are waiting, return a value indicating the task was not assigned; 
     * the caller should add the task to its RTR queue (or just do that here?) */
}

static int
PAL_TaskSchedulerParkWorker /* to be called when a worker runs out of work in its local RTR deque */
(
    struct PAL_TASK_SCHEDULER *scheduler, 
    struct PAL_TASK_POOL    *worker_pool, 
    pal_uint32_t             *steal_list, 
    pal_uint32_t          max_steal_list, 
    pal_uint32_t         *num_steal_list
)
{
    pal_uint32_t event_count = scheduler->ReadyEventCount;
    pal_uint32_t     n_steal = 0;
    _ReadWriteBarrier();
    if (max_steal_list > 0) {
        do {
            for (i = 0, n = scheduler->TaskPoolCount; i < n; ++i) {
                if (scheduler->TaskPoolERTR[i] > THRESHOLD) {
                    steal_list[n_steal++] = i;
                    if (n_steal == max_steal_list)
                        break;
                }
            }
            if (n_steal == 0) {
                if ((value = CAS(&scheduler->ReadyEventCount, event_count, event_count)) == event_count) {
                    goto park_thread;
                }
            }
        } while (n_steal == 0);
    }
    if (n_steal > 0) {
        return WORKER_STEAL;
    }

park_thread:
    PAL_SemaphoreWait(&worker_pool->ParkSem);
    if (scheduler->ShutdownSignal) {
        /* woken due to shutdown */
        return WORKER_SHUTDOWN;
    } else {
        /* woken due to work item */
        return CHECK_MAILSLOT;
    }
    /* snapshot the publish event count.
     * run through the set of estimated RTR queue counts.
     * any queue with RTR count > threshold should get added to steal_list.
     * if no targets meet the threshold, attempt to CAS the event count with itself.
     * if the CAS succeeds, no events have been published and the worker should park.
     * if the CAS fails, at least one event has been published, so run through again.
     * if the steal_list has at least one target in it, return a code to the caller 
     * to indicate that it should attempt to steal from the listed pools. */
}
#endif

static pal_sint32_t
PAL_TaskSchedulerParkWorker /* to be called when a worker runs out of work in its local RTR deque */
(
    struct PAL_TASK_SCHEDULER *scheduler, 
    struct PAL_TASK_POOL    *worker_pool, 
    pal_uint32_t             *steal_list, 
    pal_uint32_t          max_steal_list, 
    pal_uint32_t         *num_steal_list
)
{
    pal_sint32_t  *est_count = scheduler->TaskPoolERTR;
    pal_uint64_t event_count = scheduler->ReadyEventCount;
    pal_uint64_t       value = scheduler->ReadyEventCount;
    pal_uint32_t  pool_count = scheduler->TaskPoolCount;
    pal_uint32_t     n_steal = 0;
    pal_sint32_t   THRESHOLD = 1;

    _ReadWriteBarrier();
    if (max_steal_list > 0) {
        do {
            for (i = 0, n = pool_count; i < n; ++i) {
                if (est_count[i] > THRESHOLD) {
                    steal_list[n_steal++] = i;
                    if (n_steal == max_steal_list)
                        break;
                }
            }
            if (n_steal == 0) {
                if ((value = (pal_uint64_t)_InterlockedCompareExchange64((volatile LONGLONG*) &scheduler->ReadyEventCount, (LONGLONG) event_count, (LONGLONG) event_count)) == event_count) {
                    goto park_thread;
                }
            }
        } while (n_steal == 0);
    }
    if (n_steal > 0) {
        return PAL_TASK_SCHEDULER_PARK_RESULT_TRY_STEAL;
    }

park_thread:
    PAL_SemaphoreWait(&worker_pool->ParkSem);
    if (scheduler->ShutdownSignal) {
        /* woken due to shutdown */
        return PAL_TASK_SCHEDULER_PARK_RESULT_SHUTDOWN;
    } else {
        /* woken due to work item */
        return PAL_TASK_SCHEDULER_PARK_RESULT_WAKE_TASK;
    }
}

/* @summary Implement the entry point for a worker thread that processes primarily non-blocking work.
 * @param argp A pointer to a PAL_CPU_WORKER_THREAD_INIT structure.
 * The data pointed to is valid only until the thread signals the Ready or Error event.
 * @return Zero if the thread terminates normally.
 */
static unsigned int __stdcall
PAL_CpuWorkerThreadMain
(
    void *argp
)
{
    PAL_CPU_WORKER_THREAD_INIT *init =(PAL_CPU_WORKER_THREAD_INIT*) argp;
    PAL_TASK_SCHEDULER    *scheduler = init->TaskScheduler;
    PAL_TASK_POOL       *thread_pool = NULL;
    void                 *thread_ctx = NULL;
    pal_sint32_t        pool_type_id = PAL_TASK_POOL_TYPE_ID_CPU_WORKER;
    pal_uint32_t     pool_bind_flags = PAL_TASK_POOL_BIND_FLAGS_NONE;
    unsigned int           exit_code = 0;
    pal_sint32_t         wake_reason;

    /* acquire a pool of type CPU_WORKER - if this fails, execution cannot proceed */
    if ((thread_pool = PAL_TaskSchedulerAcquireTaskPool(scheduler, pool_type_id, pool_bind_flags)) == NULL) {
        SetEvent(init->ErrorSignal);
        return 1;
    }
    /* set the park semaphore for the task pool bound to the thread */
    thread_pool->ParkSemaphore = init->ParkSemaphore;

    /* run the application-supplied initialization function */
    if (init->ThreadInit(scheduler, thread_pool, init->CreateContext, &thread_ctx) != 0) {
        PAL_TaskSchedulerReleaseTaskPool(scheduler, thread_pool);
        SetEvent(init->ErrorSignal);
        return 2;
    }

    /* thread initialization has completed */
    SetEvent(init->ReadySignal); init = NULL;

    __try {
        while (scheduler->ShutdownSignal == 0) {
            /* do work */
        }
    } 
    __finally {
        PAL_TaskSchedulerReleaseTaskPool(scheduler, thread_pool);
        return exit_code;
    }
static pal_sint32_t
PAL_TaskSchedulerParkWorker /* to be called when a worker runs out of work in its local RTR deque */
(
    struct PAL_TASK_SCHEDULER *scheduler, 
    struct PAL_TASK_POOL    *worker_pool, 
    pal_uint32_t             *steal_list, 
    pal_uint32_t          max_steal_list, 
    pal_uint32_t         *num_steal_list
)
}

/* @summary Implement the entry point for a worker thread that processes blocking or asynchronous I/O work.
 * @param argp A pointer to a PAL_AIO_WORKER_THREAD_INIT structure.
 * The data pointed to is valid only until the thread signals the Ready or Error event.
 * @return Zero if the thread terminates normally.
 */
static unsigned int __stdcall
PAL_AioWorkerThreadMain
(
    void *argp
)
{
    PAL_AIO_WORKER_THREAD_INIT *init =(PAL_AIO_WORKER_THREAD_INIT*) argp;
    PAL_TASK_SCHEDULER    *scheduler = init->TaskScheduler;
    PAL_TASK_POOL       *thread_pool = NULL;
    void                 *thread_ctx = NULL;
    OVERLAPPED                   *ov = NULL;
    HANDLE                      iocp = init->CompletionPort;
    pal_sint32_t        pool_type_id = PAL_TASK_POOL_TYPE_ID_AIO_WORKER;
    pal_uint32_t     pool_bind_flags = PAL_TASK_POOL_BIND_FLAGS_NONE;
    ULONG_PTR                    key = 0;
    DWORD                     nbytes = 0;
    unsigned int           exit_code = 0;

    /* acquire a pool of type AIO_WORKER - if this fails, execution cannot proceed */
    if ((thread_pool = PAL_TaskSchedulerAcquireTaskPool(scheduler, pool_type_id, pool_bind_flags)) == NULL) {
        SetEvent(init->ErrorSignal);
        return 1;
    }

    /* run the application-supplied initialization function */
    if (init->ThreadInit(scheduler, thread_pool, init->CreateContext, &thread_ctx) != 0) {
        PAL_TaskSchedulerReleaseTaskPool(scheduler, thread_pool);
        SetEvent(init->ErrorSignal);
        return 2;
    }

    /* thread initialization has completed */
    SetEvent(init->ReadySignal); init = NULL;

    __try {
        while (scheduler->ShutdownSignal == 0) {
            /* wait for events on the I/O completion port */
            if (GetQueuedCompletionStatus(iocp, &nbytes, &key, &ov, INFINITE)) {
                if (key == PAL_COMPLETION_KEY_TASKID) {
                    /* execute a task; nbytes is the task ID */
                } else if (ov != NULL) {
                    /* an asynchronous I/O request has completed */
                } else if (key == PAL_COMPLETION_KEY_SHUTDOWN) {
                    break;
                }
            }
        }
    } 
    __finally {
        PAL_TaskSchedulerReleaseTaskPool(scheduler, thread_pool);
        return exit_code;
    }
}

/* @summary Notify all worker threads that they should exit and wait. All thread handles are closed.
 * @param scheduler The PAL_TASK_SCHEDULER managing the thread pool.
 */
static void
PAL_TaskSchedulerTerminateWorkers
(
    struct PAL_TASK_SCHEDULER *scheduler
)
{
    HANDLE        iocp = scheduler->IoCompletionPort;
    HANDLE        *sem = scheduler->WorkerParkSemaphores;
    pal_uint32_t   aio = scheduler->AioWorkerCount;
    pal_uint32_t owake = 0;
    pal_uint32_t nwake;
    pal_uint32_t  i, n;

    /* signal any active threads to terminate.
     * they'll pick this up after they finish their current task.
     */
    scheduler->ShutdownSignal = 1;

    /* wake up any threads that might be sleeping */
    for (i = 0, n = scheduler->ActiveThreadCount; i < n; ++i) {
        if (i < aio) {
            PostQueuedCompletionStatus(iocp, 0, PAL_COMPLETION_KEY_SHUTDOWN, NULL);
        } else {
            ReleaseSemaphore(sem[i-aio], 1, NULL);
        }
    }

    /* wait for threads to exit */
    while (owake < n) {
        if (n - owake > 64) {
            nwake = 64;
        } else {
            nwake = n - owake;
        }
        WaitForMultipleObjects(nwake, &scheduler->OsWorkerThreadHandles[owake], TRUE, INFINITE);
    }

    /* close thread handles */
    for (i = 0, n = scheduler->ActiveThreadCount; i < n; ++i) {
        CloseHandle(scheduler->OsWorkerThreadHandles[i]);
    }
}

/* @summary Compute the required size for a fully-committed PAL_TASK_POOL.
 * @param page_size The operating system page size, in bytes.
 */
static void
PAL_TaskPoolQueryMemorySize
(
    struct PAL_TASK_POOL_SIZE_INFO *pool_size, 
    pal_uint32_t                    page_size
)
{
    pal_uint32_t reserve_size = 0;
    pal_uint32_t  commit_size = 0;
    pal_uint32_t udata_offset = 0;
    pal_uint32_t   udata_size = 0;
    reserve_size  = PAL_AllocationSizeType(PAL_TASK_POOL);
    udata_offset  = reserve_size;
    reserve_size  = PAL_AlignUp(reserve_size , page_size); /* user data */
    udata_size    = reserve_size - udata_offset;
    reserve_size += PAL_AllocationSizeArray(pal_uint16_t , PAL_TASKID_MAX_SLOTS_PER_POOL); /* AllocSlotIds */
    reserve_size += PAL_AllocationSizeArray(PAL_TASKID   , PAL_TASKID_MAX_SLOTS_PER_POOL); /* ReadyTaskIds */
    commit_size   = reserve_size;
    reserve_size += PAL_AllocationSizeArray(PAL_TASK_DATA, PAL_TASKID_MAX_SLOTS_PER_POOL); /* TaskSlotData */
    reserve_size  = PAL_AlignUp(reserve_size , page_size); /* pools are page-aligned */
    pool_size->ReserveSize    = reserve_size;
    pool_size->CommitSize     = commit_size;
    pool_size->UserDataSize   = udata_size;
    pool_size->UserDataOffset = udata_offset;
}

/* @summary Compute the required memory reservation size for a PAL_TASK_SCHEDULER.
 * @param size_info On return, the reserve and commit sizes are stored here.
 * @param count_info On return, the number of task pools of each type is stored here.
 * @param pool_size A PAL_TASK_POOL_SIZE_INFO populated by a prior call to PAL_TaskPoolQueryMemorySize.
 * @param init The PAL_TASK_SCHEDULER_INIT defining the scheduler attributes.
 * @param page_size The operating system page size, in bytes.
 */
static void
PAL_TaskSchedulerQueryMemorySize
(
    struct PAL_TASK_SCHEDULER_SIZE_INFO *size_info, 
    struct PAL_TASK_POOL_TYPE_COUNTS   *count_info, 
    struct PAL_TASK_POOL_SIZE_INFO      *pool_size, 
    struct PAL_TASK_SCHEDULER_INIT           *init, 
    pal_uint32_t                         page_size
)
{
    pal_uint64_t reserve_size = 0;
    pal_uint32_t  commit_size = 0;
    pal_uint32_t  pool_offset = 0;
    pal_uint32_t   main_count = 0;
    pal_uint32_t    aio_count = 0;
    pal_uint32_t    cpu_count = 0;
    pal_uint32_t   user_count = 0;
    pal_uint32_t thread_count = 0;
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
    }
    count_info->MainThreadPoolCount = main_count;
    count_info->AioWorkerPoolCount  =  aio_count;
    count_info->CpuWorkerPoolCount  =  cpu_count;
    count_info->UserThreadPoolCount = user_count;
    count_info->TotalPoolCount      = main_count + aio_count + cpu_count + user_count;
    thread_count  = init->CpuWorkerThreadCount + init->AioWorkerThreadCount;

    /* determine the memory requirement for the scheduler */
    reserve_size  = PAL_AllocationSizeType (PAL_TASK_SCHEDULER);
    reserve_size += PAL_AllocationSizeArray(PAL_TASK_POOL*         , count_info->TotalPoolCount); /* TaskPoolList  */
    reserve_size += PAL_AllocationSizeArray(pal_sint32_t           , count_info->TotalPoolCount); /* TaskPoolERTR  */
    reserve_size += PAL_AllocationSizeArray(pal_uint32_t           , count_info->TotalPoolCount); /* PoolThreadId  */
    reserve_size += PAL_AllocationSizeArray(pal_uint32_t           , init->CpuWorkerThreadCount); /* ParkedPoolIds */
    reserve_size += PAL_AllocationSizeArray(PAL_TASK_POOL_FREE_LIST, init->PoolTypeCount);        /* PoolFreeList  */
    reserve_size += PAL_AllocationSizeArray(pal_sint32_t           , init->PoolTypeCount);        /* PoolTypeIds   */
    reserve_size += PAL_AllocationSizeArray(unsigned int           , thread_count);               /* OsWorkerThreadIds */
    reserve_size += PAL_AllocationSizeArray(HANDLE                 , thread_count);               /* OsWorkerThreadHandles */
    reserve_size += PAL_AllocationSizeArray(HANDLE                 , init->CpuWorkerThreadCount); /* WorkerParkSemaphores */
    /* ... */
    reserve_size  = PAL_AlignUp(reserve_size, page_size);
    commit_size   =(pal_uint32_t) reserve_size;
    pool_offset   =(pal_uint32_t) reserve_size;
    /* include the memory requirement for all task pools */
    reserve_size += pool_size->ReserveSize * count_info->TotalPoolCount;
    size_info->ReserveSize    = reserve_size;
    size_info->CommitSize     = commit_size;
    size_info->TaskPoolOffset = pool_offset;
}

PAL_API(struct PAL_TASK_SCHEDULER*)
PAL_TaskSchedulerCreate
(
    struct PAL_TASK_SCHEDULER_INIT *init
)
{
    HANDLE                                 sem = NULL;
    HANDLE                                iocp = NULL;
    HANDLE                               error = NULL;
    HANDLE                               ready = NULL;
    PAL_TASK_SCHEDULER              *scheduler = NULL;
    pal_uint8_t                      *base_ptr = NULL;
    pal_uint8_t                     *pool_base = NULL;
    pal_uint32_t                     page_size = 0;
    pal_uint32_t             worker_init_count = 0;
    pal_uint32_t           worker_thread_count = 0;
    pal_uint32_t             global_pool_index = 0;
    pal_uint32_t               type_pool_index = 0;
    pal_uint32_t               type_pool_count = 0;
    pal_uint32_t                    type_index = 0;
    pal_uint32_t                    type_count = 0;
    pal_uint32_t                     sem_count = 0;
    DWORD                            error_res = ERROR_SUCCESS;
    SYSTEM_INFO                        sysinfo;
    PAL_MEMORY_ARENA           scheduler_arena;
    PAL_MEMORY_ARENA_INIT scheduler_arena_init;
    PAL_TASK_SCHEDULER_SIZE_INFO    sched_size;
    PAL_TASK_POOL_SIZE_INFO          pool_size;
    PAL_TASK_POOL_TYPE_COUNTS           counts;
    PAL_AIO_WORKER_THREAD_INIT aio_thread_init;
    PAL_CPU_WORKER_THREAD_INIT cpu_thread_init;
    pal_uint32_t                          i, n;

    if (init->AioWorkerInitFunc == NULL && init->AioWorkerThreadCount > 0) {
        init->AioWorkerInitFunc  = PAL_DefaultTaskWorkerInit;
    }
    if (init->CpuWorkerInitFunc == NULL && init->CpuWorkerThreadCount > 0) {
        init->CpuWorkerInitFunc  = PAL_DefaultTaskWorkerInit;
    }

    /* determine the total number of worker threads being requested */
    worker_thread_count = init->CpuWorkerThreadCount + init->AioWorkerThreadCount;

    /* create the I/O completion ports and the thread startup events */
    if ((error = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL) {
        error_res = GetLastError();
        goto cleanup_and_fail;
    }
    if ((ready = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL) {
        error_res = GetLastError();
        goto cleanup_and_fail;
    }
    if ((iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, worker_thread_count+1)) == NULL) {
        error_res = GetLastError();
        goto cleanup_and_fail;
    }

    /* determine the amount of memory that has to be reserved and make the main allocation */
    GetNativeSystemInfo(&sysinfo); 
    page_size     =  sysinfo.dwPageSize;
    PAL_TaskPoolQueryMemorySize(&pool_size, page_size);
    PAL_TaskSchedulerQueryMemorySize(&sched_size, &counts, &pool_size, init, page_size);
    if ((base_ptr = (pal_uint8_t*) VirtualAlloc(NULL, sched_size.ReserveSize, MEM_RESERVE, PAGE_NOACCESS)) == NULL) {
        error_res =  GetLastError();
        goto cleanup_and_fail;
    }
    /* commit only the required memory */
    if (VirtualAlloc(base_ptr, sched_size.CommitSize, MEM_COMMIT, PAGE_READWRITE) != base_ptr) {
        error_res =  GetLastError();
        goto cleanup_and_fail;
    }

    /* create an arena for sub-allocating the scheduler data */
    scheduler_arena_init.AllocatorName = __FUNCTION__" Scheduler Arena";
    scheduler_arena_init.AllocatorType = PAL_MEMORY_ALLOCATOR_TYPE_HOST;
    scheduler_arena_init.MemoryStart   =(pal_uint64_t) base_ptr;
    scheduler_arena_init.MemorySize    =(pal_uint64_t) sched_size.ReserveSize;
    scheduler_arena_init.UserData      = NULL;
    scheduler_arena_init.UserDataSize  = 0;
    if (PAL_MemoryArenaCreate(&scheduler_arena, &scheduler_arena_init) < 0) {
        error_res = ERROR_ARENA_TRASHED;
        goto cleanup_and_fail;
    }
    pool_base                       = base_ptr + sched_size.TaskPoolOffset;
    scheduler                       = PAL_MemoryArenaAllocateHostType (&scheduler_arena, PAL_TASK_SCHEDULER);
    scheduler->TaskPoolList         = PAL_MemoryArenaAllocateHostArray(&scheduler_arena, PAL_TASK_POOL*, counts.TotalPoolCount);
    scheduler->TaskPoolERTR         = PAL_MemoryArenaAllocateHostArray(&scheduler_arena, pal_sint32_t  , counts.TotalPoolCount);
    scheduler->PoolThreadId         = PAL_MemoryArenaAllocateHostArray(&scheduler_arena, pal_uint32_t  , counts.TotalPoolCount);
    scheduler->TaskPoolCount        = counts.TotalPoolCount;
    scheduler->PoolTypeCount        = init->PoolTypeCount;
    scheduler->ParkedPoolIds        = PAL_MemoryArenaAllocateHostArray(&scheduler_arena, pal_uint32_t           , init->CpuWorkerThreadCount);
    scheduler->PoolFreeList         = PAL_MemoryArenaAllocateHostArray(&scheduler_arena, PAL_TASK_POOL_FREE_LIST, init->PoolTypeCount);
    scheduler->PoolTypeIds          = PAL_MemoryArenaAllocateHostArray(&scheduler_arena, pal_sint32_t           , init->PoolTypeCount);
    scheduler->TaskPoolBase         = pool_base;
    scheduler->OsWorkerThreadIds    = PAL_MemoryArenaAllocateHostArray(&scheduler_arena, unsigned int  , worker_thread_count);
    scheduler->OsWorkerThreadHandles= PAL_MemoryArenaAllocateHostArray(&scheduler_arena, HANDLE        , worker_thread_count);
    scheduler->WorkerParkSemaphores = PAL_MemoryArenaAllocateHostArray(&scheduler_arena, HANDLE        , init->CpuWorkerThreadCount);
    scheduler->IoCompletionPort     = iocp;
    scheduler->Reserved1            = 0;
    scheduler->Reserved2            = 0;
    scheduler->ShutdownSignal       = 0;
    scheduler->CpuWorkerCount       = init->CpuWorkerThreadCount;
    scheduler->AioWorkerCount       = init->AioWorkerThreadCount;
    scheduler->ActiveThreadCount    = 0;
    scheduler->ReadyEventCount      = 0;
    scheduler->ParkedPoolToS        = 0;

    /* initialize the task pools */
    for (type_index = 0, type_count = init->PoolTypeCount; type_index < type_count; ++type_index) {
        PAL_TASK_POOL_INIT    *type =&init->TaskPoolTypes [type_index];
        pal_uint32_t     chunk_size = PAL_TASK_CHUNK_SIZE;
        pal_uint32_t    chunk_count =(type->PreCommitTasks + (chunk_size-1)) / chunk_size;
        pal_usize_t     commit_size = pool_size.CommitSize;

        /* determine the commit size for this specific pool type */
        commit_size += PAL_AllocationSizeArray(PAL_TASK_DATA, chunk_size * chunk_count);
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
            pool_addr = pool_base + (global_pool_index * pool_size.ReserveSize);
            if (VirtualAlloc(pool_addr, commit_size, MEM_COMMIT, PAGE_READWRITE) == NULL) {
                error_res =  GetLastError();
                goto cleanup_and_fail;
            }
            /* create an arena to sub-allocate from the committed space */
            pool_arena_init.AllocatorName = __FUNCTION__" Pool Arena";
            pool_arena_init.AllocatorType = PAL_MEMORY_ALLOCATOR_TYPE_HOST;
            pool_arena_init.MemoryStart   =(pal_uint64_t) pool_addr;
            pool_arena_init.MemorySize    =(pal_uint64_t) pool_size.ReserveSize;
            pool_arena_init.UserData      = NULL;
            pool_arena_init.UserDataSize  = 0;
            if (PAL_MemoryArenaCreate(&pool_arena, &pool_arena_init) < 0) {
                error_res = ERROR_ARENA_TRASHED;
                goto cleanup_and_fail;
            }
            /* finally allocate and initialize the task pool.
             * note that the order of allocations here matter. */
            pool = PAL_MemoryArenaAllocateHostType(&pool_arena, PAL_TASK_POOL);
            pool->TaskScheduler     = scheduler;
            pool->NextFreePool      = scheduler->PoolFreeList[type_index].FreeListHead;
            pool->UserDataBuffer    = PAL_MemoryArenaAllocateHostArray(&pool_arena, pal_uint8_t  , pool_size.UserDataSize);
            pool->AllocSlotIds      = PAL_MemoryArenaAllocateHostArray(&pool_arena, pal_uint16_t , PAL_TASKID_MAX_SLOTS_PER_POOL);
            pool->ReadyTaskIds      = PAL_MemoryArenaAllocateHostArray(&pool_arena, PAL_TASKID   , PAL_TASKID_MAX_SLOTS_PER_POOL);
            pool->TaskSlotData      = PAL_MemoryArenaAllocateHostArray(&pool_arena, PAL_TASK_DATA, chunk_size * chunk_count);
            pool->ParkSemaphore     = NULL;
            pool->OsThreadId        = 0;
            pool->PoolTypeId        = type->PoolTypeId;
            pool->PoolIndex         = global_pool_index;
            pool->PoolFlags         = type->PoolFlags;
            pool->UserDataSize      = pool_size.UserDataSize;
            pool->WakeupTaskId      = PAL_TASKID_NONE;
            pool->TaskPoolList      = scheduler->TaskPoolList;
            pool->Reserved1         = 0;
            pool->Reserved2         = 0;
            pool->Reserved3         = 0;
            pool->CommitCount       = chunk_count;
            pool->AllocCount        = 0;
            pool->AllocNext         = 0;
            pool->FreeCount         = chunk_count * chunk_size;
            pool->ReadyPublicPos    = 0;
            pool->ReadyPrivatePos   = 0;
            /* update the scheduler pool binding table */
            scheduler->TaskPoolList[global_pool_index] = pool;
            scheduler->TaskPoolERTR[global_pool_index] = 0;
            scheduler->PoolThreadId[global_pool_index] = 0;
            /* push the task pool onto the free list */
            scheduler->PoolFreeList[type_index].FreeListHead = pool;
            scheduler->PoolFreeList[type_index].PoolTypeFree++;
        }
    }

    /* launch the worker thread pool */
    aio_thread_init.TaskScheduler  = scheduler;
    aio_thread_init.ThreadInit     = init->AioWorkerInitFunc;
    aio_thread_init.CreateContext  = init->CreateContext;
    aio_thread_init.ReadySignal    = ready;
    aio_thread_init.ErrorSignal    = error;
    aio_thread_init.CompletionPort = iocp;
    cpu_thread_init.TaskScheduler  = scheduler;
    cpu_thread_init.ThreadInit     = init->CpuWorkerInitFunc;
    cpu_thread_init.CreateContext  = init->CreateContext;
    cpu_thread_init.ReadySignal    = ready;
    cpu_thread_init.ErrorSignal    = error;
    cpu_thread_init.ParkSemaphore  = NULL;
    for (i = 0, n = worker_thread_count; i < n; ++i) {
        Win32_ThreadMain_Func  entry_point = NULL;
        HANDLE                      worker = NULL;
        void                         *argp = NULL;
        unsigned int             thread_id = 0;
        unsigned                stack_size = 0;
        DWORD const           THREAD_READY = 0;
        DWORD const           THREAD_ERROR = 1;
        DWORD const             WAIT_COUNT = 2;
        DWORD                      wait_rc = 0;
        HANDLE                 wait_set[2];

        if (i < init->AioWorkerThreadCount) {
            entry_point = PAL_AioWorkerThreadMain;
            stack_size  = init->AioWorkerStackSize;
            argp =(void*)&aio_thread_init;
        } else {
            /* creating a CPU worker thread - needs a park semaphore */
            entry_point = PAL_CpuWorkerThreadMain;
            stack_size  = init->CpuWorkerStackSize;
            if ((sem = CreateSemaphore(NULL, 0, 1, NULL)) == NULL) {
                error_res = GetLastError();
                goto cleanup_and_fail;
            }
            scheduler->WorkerParkSemaphores[sem_count++] = sem;
            cpu_thread_init.ParkSemaphore = sem;
            argp = (void*)&cpu_thread_init;
        }

        /* launch the worker thread */
        ResetEvent(ready); ResetEvent(error);
        if ((worker = (HANDLE)_beginthreadex(NULL, stack_size, entry_point, argp, 0, &thread_id)) == NULL) {
            error_res = GetLastError();
            goto cleanup_and_fail;
        }
        scheduler->OsWorkerThreadHandles[worker_init_count] = worker;
        scheduler->OsWorkerThreadIds[worker_init_count] = thread_id;
        scheduler->ActiveThreadCount++;
        worker_init_count++;

        /* wait for it to become ready */
        wait_set[THREAD_READY] = ready;
        wait_set[THREAD_ERROR] = error;
        if ((wait_rc = WaitForMultipleObjects(WAIT_COUNT, wait_set, FALSE, INFINITE)) != (WAIT_OBJECT_0+THREAD_READY)) {
            error_res = GetLastError();
            goto cleanup_and_fail;
        }
    }

    /* cleanup */
    CloseHandle(ready);
    CloseHandle(error);
    return scheduler;

cleanup_and_fail:
    if (worker_init_count) {
        PAL_TaskSchedulerTerminateWorkers(scheduler);
    }
    for (i = 0; i < sem_count; ++i) {
        CloseHandle(scheduler->WorkerParkSemaphores[i]);
    }
    if (base_ptr) {
        VirtualFree(base_ptr, 0, MEM_RELEASE);
    }
    if (iocp) {
        CloseHandle(iocp);
    }
    if (ready) {
        CloseHandle(ready);
    }
    if (error) {
        CloseHandle(error);
    }
    return NULL;
}

PAL_API(void)
PAL_TaskSchedulerDelete
(
    struct PAL_TASK_SCHEDULER *scheduler
)
{
    if (scheduler) {
        pal_uint32_t i, n;
        PAL_TaskSchedulerTerminateWorkers(scheduler);
        for (i = 0, n = scheduler->CpuWorkerCount; i < n; ++i) {
            CloseHandle(scheduler->WorkerParkSemaphores[i]);
        }
        if (scheduler->IoCompletionPort) {
            CloseHandle(scheduler->IoCompletionPort);
        }
        VirtualFree(scheduler, 0, MEM_RELEASE);
    }
}

PAL_API(struct PAL_TASK_POOL*)
PAL_TaskSchedulerAcquireTaskPool
(
    struct PAL_TASK_SCHEDULER *scheduler, 
    pal_sint32_t            pool_type_id, 
    pal_uint32_t              bind_flags
)
{
    PAL_TASK_POOL_FREE_LIST *free_list = NULL;
    PAL_TASK_POOL           *task_pool = NULL;
    pal_sint32_t const   *type_id_list = scheduler->PoolTypeIds;
    pal_uint32_t            free_count = 0;
    pal_uint32_t                  i, n;

    for (i = 0, n = scheduler->PoolTypeCount; i < n; ++i) {
        if (pool_type_id == type_id_list[i]) {
            free_list = &scheduler->PoolFreeList[i];
            break;
        }
    }
    if (free_list == NULL) {
        /* invalid type ID */
        return NULL;
    }
    AcquireSRWLockExclusive(&free_list->TypeLock); {
        if ((task_pool = free_list->FreeListHead) != NULL) {
            free_list->FreeListHead = task_pool->NextFreePool;
            task_pool->NextFreePool = NULL;
        }
    } ReleaseSRWLockExclusive(&free_list->TypeLock);
    
    if (task_pool != NULL) {
        PAL_TASK_DATA *task_data = task_pool->TaskSlotData;
        pal_uint16 _t  *slot_ids = task_pool->AllocSlotIds;

        if ((bind_flags & PAL_TASK_POOL_BIND_FLAG_MANUAL) == 0) {
            PAL_TaskSchedulerBindPoolToThread(scheduler, task_pool, PAL_GetCurrentThreadId());
        } else {
            task_pool->OsThreadId = 0;
        }

        /* initialize all of the task state and free lists */
        free_count   = task_pool->CommitCount * PAL_TASK_CHUNK_SIZE;
        PAL_ZeroMemory(task_data , free_count * sizeof(PAL_TASK_DATA));
        for (i = 0; i < free_count; ++i) {
            slot_ids[i] = (pal_uint16_t) i;
        }

        /* initialize the queues and counters */
        task_pool->WakeupTaskId    = PAL_TASKID_NONE;
        task_pool->AllocCount      = 0;
        task_pool->AllocNext       = 0;
        task_pool->ReadyPublicPos  = 0;
        task_pool->ReadyPrivatePos = 0;
       _InterlockedExchange64((volatile LONGLONG*) &task_pool->FreeCount, (LONGLONG) free_count);
    }
    return task_pool;
}

PAL_API(void)
PAL_TaskSchedulerReleaseTaskPool
(
    struct PAL_TASK_SCHEDULER *scheduler, 
    struct PAL_TASK_POOL      *task_pool
)
{
    if (task_pool != NULL) { 
        PAL_TASK_POOL_FREE_LIST *free_list = NULL;
        pal_sint32_t const   *type_id_list = scheduler->PoolTypeIds;
        pal_sint32_t          pool_type_id = task_pool->PoolTypeId;
        pal_uint32_t                  i, n;

        for (i = 0, n = scheduler->PoolTypeCount; i < n; ++i) {
            if (pool_type_id == type_id_list[i]) {
                free_list = &scheduler->PoolFreeList[i];
                break;
            }
        }
        if (free_list == NULL) {
            return;
        }
        /* clear the thread bindings */
        task_pool->OsThreadId = 0;
        scheduler->PoolThreadId[task_pool->PoolIndex] = 0;
        /* return the pool to the free list */
        AcquireSRWLockExclusive(&free_list->TypeLock); {
            task_pool->NextFreePool = free_list->FreeListHead;
            free_list->FreeListHead = task_pool;
        } ReleaseSRWLockExclusive(&free_list->TypeLock);
    }
}

PAL_API(int)
PAL_TaskSchedulerBindPoolToThread
(
    struct PAL_TASK_SCHEDULER *scheduler, 
    struct PAL_TASK_POOL           *pool, 
    pal_uint32_t            os_thread_id
)
{
    pool->OsThreadId = os_thread_id;
    scheduler->PoolThreadId[pool->PoolIndex] = os_thread_id;
    return 0;
}

