/**
 * @summary Define the platform-specific types and other internal bits for the 
 * Microsoft Windows Desktop platform. Since Windows is primarily a desktop 
 * platform, and devices tend to have larger amounts of memory than embedded 
 * or mobile systems, data sizes are larger then they might be on those 
 * platforms - tradeoffs must be weighed per-platform.
 */
#ifndef __PAL_WIN32_TASK_H__
#define __PAL_WIN32_TASK_H__

#ifndef __PAL_TASK_H__
#include "pal_task.h"
#endif

#ifndef PAL_NO_INCLUDES
#include <Windows.h>
#endif

/* @summary Define the layout of the data associated with a task.
 * All of the data is grouped together in order to support dynamic extension of task storage capacity.
 * On Microsoft Windows platforms, 256 bytes of storage are allocated for each task.
 * The public data appears at the start of each PAL_TASK_DATA.
 * When PAL_TaskPublish receives a task, the list of dependencies is converted into a list of permits.
 */
typedef struct PAL_CACHELINE_ALIGN PAL_TASK_DATA {
    PAL_TASK                       PublicData;                 /* The set of data that must be supplied by the code creating the task. */
    pal_uint8_t                    Arguments[96];              /* Data used for storing per-task arguments. */
    /* ---- */
    pal_uint32_t                   Generation;                 /* The generation value associated with the task data slot. This value is used to detect expired task IDs. */
    pal_sint32_t                   WaitCount;                  /* The number of other tasks that must complete before this task can start. When this value is zero, the task is ready-to-run. */
    pal_sint32_t                   WorkCount;                  /* The number of work items that must complete before this task can complete. This value starts at 1, and adds 1 for each child task. */
    pal_uint8_t                    InternalPad[52];            /* Padding used to separate internal data from the permits list. */
    /* ---- */
    pal_sint32_t                   PermitCount;                /* The number of valid entries in the PermitTasks array. This value is zero if the task has not completed and there are no permits, or -1 if the task has completed. */
    PAL_TASKID                     PermitTasks[15];            /* The identifiers of the tasks whose WaitCount should be decreased when this task completes. */
} PAL_TASK_DATA;

/* @summary Define the data associated with a task pool, which is a pre-allocated block of task data.
 * Each PAL_TASK_POOL is bound to a thread. Only the bound thread may create tasks within the pool, 
 * though other threads may run, complete and delete those tasks. The task pool uses the virtual memory
 * system to limit memory usage. Each task pool can accomodate up to the limit of 65536 tasks.
 */
typedef struct PAL_CACHELINE_ALIGN PAL_TASK_POOL {
    struct PAL_TASK_SCHEDULER     *TaskScheduler;            /* The PAL_TASK_SCHEDULER that manages the task pool memory and the pool of worker threads used to execute tasks. */
    struct PAL_TASK_POOL          *NextFreePool;             /* A pointer to the next available pool of the same type. This value is only valid if the pool is not in use, otherwise it is set to NULL. */
    struct PAL_TASK_DATA          *TaskSlotData;             /* A pointer to the start of the memory block used to store task data. Some portion of this data may only be reserved and not committed. */
    PAL_TASKID                    *ReadyTaskIds;             /* The storage for a single-producer, multi-consumer concurrent queue storing the IDs of ready-to-run tasks. Pre-committed to maximum capacity. */
    pal_uint16_t                  *AllocSlotIds;             /* The storage for a multi-producer, single-consumer concurrent queue storing the slot indices of available slots in TaskSlotData. */
    pal_uint64_t                  *StealBitSet;              /* Pointer to an array of 16 64-bit words (1024 bits) used to indicate when another task pool might have tasks available to steal. */
    pal_uint32_t                   CommitCount;              /* The number of currently committed chunks of PAL_TASK_DATA. Each chunk is 1024 tasks. */
    pal_sint32_t                   PoolTypeId;               /* One of the values of the PAL_TASK_POOL_TYPE_ID enumeration. Used mainly for debugging purposes. */
    pal_uint32_t                   OsThreadId;               /* The operating system thread identifier of the thread bound to the task pool. */
    pal_uint32_t                   PoolIndex;                /* The zero-based index of the task pool in the scheduler's task pool array. */
    /* ---- */
    pal_uint32_t                   PoolFlags;                /* One or more bitwise ORd values from the PAL_TASK_POOL_FLAGS enumeration specifying the set of operations that can be performed by the thread that owns the pool. */
    pal_uint32_t                   UserDataSize;             /* The size of the user data section, in bytes. */
    pal_uint8_t                   *UserDataBuffer;           /* Pointer to a per-pool buffer that can be used to store pool-local data. */
    struct PAL_TASK_POOL          *StealPoolSet;             /* An array of PAL_TASK_POOL instances to which task steal notifications can be published. Maintained by the PAL_TASK_SCHEDULER. */
    pal_uint64_t                   StealWordMask;            /* The bitmask used to signal when this task pool has a task available to steal. Constant. */
    pal_uint32_t                   StealWordIndex;           /* The zero-based index of the word to modify in the target pool's StealBitSet when this task pool has a task available to steal. Constant. */
    pal_uint32_t                   StealPoolCount;           /* The number of valid items in the StealPoolSet array. */
    pal_uint32_t                   StealPublishIndex;        /* The zero-based index of the next pool to which a steal notification will be published when this task pool has a task available to steal. */
    pal_uint32_t                   StealConsumeIndex;        /* The zero-based index of the next 64-bit word in StealBitSet to read and check for steal notification when this pool is out of ready-to-run work. */
    pal_uint64_t                   AllocCount;               /* The counter used to track the consumer position in the AllocSlotIds list. */
    pal_uint64_t                   AllocNext;                /* An internal counter used to allocate task slots from the available range. Always <= AllocCount. */
    /* ---- */
    pal_uint8_t                    FreePad[56];              /* Padding used to separate the task slot free counter from other data to prevent false sharing. */
    pal_uint64_t                   FreeCount;                /* The counter used to track the producer position in the AllocSlotIds list. */
    /* ---- */
    pal_uint8_t                    ReadyPublicPad[56];       /* Padding used to separate the private position (push, take) in the ready-to-run queue from the steal data. */
    pal_sint64_t                   ReadyPublicPos;           /* A monotonically-increasing integer representing the position of the next dequeue operation in the ready-to-run queue. */
    /* ---- */
    pal_uint8_t                    ReadyPrivatePad[56];      /* Padding used to separate the public position (steal) in the ready-to-run queue from other shared data. */
    pal_sint64_t                   ReadyPrivatePos;          /* A monotonically-increasing integer representing the position of the next enqueue operation in the ready-to-run queue. */
    /* ---- */
} PAL_TASK_POOL;

/* MEMORY LAYOUT for PAL_TASK_POOL:
 * base: 
 *     | [PAL_TASK_POOL 320 bytes]
 *     | [StealBitSet 128 bytes]
 *     | [UserDataStorage 4KB-448 bytes] [64 byte alignment]
 *     | [AllocSlotIds 128KB] [64 byte alignment]
 *     | [ReadyTaskIds 256KB] [64 byte alignment]
 *     | <--- TaskSlotData points here (base+388KB)
 *     | [TaskSlotData committed (CommitCount * 1024 * 256) bytes]
 *     | [TaskSlotData reserved ((64 - CommitCount) * 1024 * 256 bytes]
 * end - (base + 388KB + 16MB)
 */

/* @summary Define the data associated with the task pool free list for a particular task pool type ID.
 * The free list is internally synchronized; the reader-writer lock is always acquired in exclusive mode.
 */
typedef struct PAL_TASK_POOL_FREE_LIST {
    SRWLOCK                          TypeLock;                 /* The lock used to synchronize access to the free list. */
    struct PAL_TASK_POOL            *FreeListHead;             /* The head of the free list. If the list is empty, this value is NULL. */
    pal_sint32_t                     PoolTypeId;               /* One of the values of the PAL_TASK_POOL_TYPE_ID enumeration. */
    pal_uint32_t                     PoolTypeFree;             /* The number of items available on the free list. */
    pal_uint32_t                     PoolTypeTotal;            /* The total number of task pools of the type. */
    pal_uint32_t                     PoolTypeFlags;            /* One or more bitwise ORd values from the PAL_TASK_POOL_FLAGS enumeration. */
} PAL_TASK_POOL_FREE_LIST;

/* @summary Define the data associated with the task scheduler. The task scheduler manages:
 * 1. The set of task pools, each of which is bound to a thread and used to create tasks.
 * 2. A pool of worker threads used to execute potentially blocking I/O operations.
 * 3. A pool of worker threads used to execute non-blocking CPU work.
 * 4. Synchronization objects used to put worker threads to sleep and wake them up.
 * 5. A global work queue to which any thread can publish work items to be executed on worker threads.
 */
typedef struct PAL_TASK_SCHEDULER {
    struct PAL_TASK_POOL           **StealPoolSet;             /* An array of TaskPoolCount pointers, of which StealPoolCount are valid, to each task pool that has the PAL_TASK_POOL_FLAG_STEAL attribute. */
    struct PAL_TASK_POOL           **TaskPoolList;             /* An array of TaskPoolCount pointers to each task pool. Addressible by PAL_TASK_POOL::PoolIndex. */
    pal_uint32_t                    *PoolThreadId;             /* An array specifying the operating system thread ID bound to each task pool. */
    pal_uint32_t                     TaskPoolCount;            /* The total number of PAL_TASK_POOL instances, across all types, managed by the scheduler. */
    pal_uint32_t                     StealPoolCount;           /* The number of valid entries in the StealPoolSet array. */
    pal_uint32_t                     PoolTypeCount;            /* The number of distinct task pool types. */
    struct PAL_TASK_POOL_FREE_LIST  *PoolFreeList;             /* An array containing the free list for each task pool type. */
    pal_uint32_t                    *PoolTypeIds;              /* An array specifying the PAL_TASK_POOL_TYPE_ID for each free list. */
    pal_uint8_t                     *TaskPoolBase;             /* The base address of the first PAL_TASK_POOL. */
#if 0
    unsigned int                  *OsThreadIds;                /* */
    HANDLE                        *OsThreadHandles;            /* */
    HANDLE                        *WorkerReadyEvents;          /* */
    HANDLE                        *WorkerErrorEvents;          /* */
    HANDLE                         IoCompletionPort;           /* */
    HANDLE                         TerminateSignal;            /* */
    pal_uint32_t                   CpuWorkerCount;             /* */
    pal_uint32_t                   AioWorkerCount;             /* */
    pal_uint32_t                   ActiveThreadCount;          /* */
    pal_uint32_t                   MaxAsyncIoRequests;         /* */
    /* ---- */
    struct PAL_AIO_REQUEST        *IoRequestStorage;           /* */
    struct PAL_AIO_REQUEST        *IoRequestFreeList;          /* */
    CRITICAL_SECTION               IoRequestFreeListLock;      /* */
    HANDLE                         IoRequestSemaphore;         /* TODO: should be lightweight semaphore */
#endif
    /* TODO: global queue, scheduling data */
} PAL_TASK_SCHEDULER;

/* MEMORY LAYOUT for PAL_TASK_SCHEDULER:
 * base: 
 *     | [PAL_TASK_SCHEDULER xxx bytes]
 *     | [GlobalWorkQueueData 512KB]
 *     | [IoRequestStorage xxxKB]
 *     | [OsThreadIds xxx bytes]
 *     | [OsThreadHandles xxx bytes]
 *     | [WorkerReadyEvents xxx bytes]
 *     | [WorkerErrorEvents xxx bytes]
 *     | [WorkerSleepSemaphores xxx bytes]
 *     | [WorkerWaitQueue xxx bytes]
 *     | <--- TaskPoolData points here
 *     | [TaskPoolData array (TaskPoolCount * TaskPoolReserveSize)]
 *     | 
 * end -
 */

#if 0
typedef struct PAL_AIO_REQUEST {
} PAL_AIO_REQUEST;
#endif

#ifdef NOTES
// Decided we can keep up to 256 bytes of data per-task slot.
// First is some metadata, publicly exposed:
// - ParentID
// - EntryPoint
// - CompletionType
// - Plenty of room for other stuff here.
// Next is internal data:
// - WaitCount
// - WorkCount
// - PermitCount
// - Generation
// - Has stolen tasks? (can avoid atomic updates if false)
// - Plenty of room for other stuff here as well.
// Next is permits list:
// - Up to 32 permits can be stored
// Finally per-task data:
// - Up to 64 bytes are available (16 32-bit values or 8 64-bit values)
// At 256 bytes/task slot, a task pool of 4K tasks is 1MB, not including queues.
// At 256 bytes/task slot, a maxed-out task pool is 16MB, not including queues.
// Queues add a fixed overhead of 384KB/task pool.
// With 64 task pools at max capacity, we're looking at ~1GB of storage.
// The public metadata and internal data can easily be combined into a single cacheline.
// This would bring the storage requirement down to 192 bytes/task slot.
// A maxed out task pool would then be 12MB/48MB/768MB compared with 16MB/64MB/1024MB.
// - n.b. these numbers are 64K task pool / 4K task pool w/64 threads / 64K task pool w/64 threads.
// Personally, I don't think that the extra space is going to matter when running at that scale.
// I'd rather have the extra space for growth and adding new features.
#endif

#endif /* __PAL_WIN32_TASK_H__ */
