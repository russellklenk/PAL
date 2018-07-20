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
#include <process.h>
#endif

/* @summary Define the data associated with a permits list, which is a list of task identifiers that share the same set of dependencies.
 * When the final dependency task completes, that is, the WaitCount field drops to zero, it allows all tasks in the permits list to run.
 * The permits list is allocated internally when necessary during a call to PAL_TaskPublish, and each task in the dependency list references the permits list.
 */
typedef struct PAL_CACHELINE_ALIGN PAL_PERMITS_LIST {          /* 128 bytes */
    pal_sint32_t                   WaitCount;                  /* The number of tasks that must complete before the tasks in the permits list become ready-to-run. */
    pal_uint32_t                   PoolIndex;                  /* The index of the PAL_TASK_POOL from which the permits list was allocated. */
    PAL_TASKID                     TaskList[30];               /* The list of tasks to be made ready-to-run when WaitCount reaches zero. The list terminates at the first PAL_TASKID_NONE entry. */
} PAL_PERMITS_LIST;

/* @summary Define the layout of the data associated with a task.
 * All of the data is grouped together in order to support dynamic extension of task storage capacity.
 * On Microsoft Windows platforms, 256 bytes of storage are allocated for each task.
 * The public data appears at the start of each PAL_TASK_DATA.
 * When PAL_TaskPublish receives a task, the list of dependencies is converted into a list of permits.
 */
typedef struct PAL_CACHELINE_ALIGN PAL_TASK_DATA {             /* 256 bytes */
    PAL_TASK                       PublicData;                 /* The set of data that must be supplied by the code creating the task. */
    OVERLAPPED                     Overlapped;                 /* An OVERLAPPED structure used by asynchronous I/O requests. */
    /* ---- */
    pal_uint8_t                    Arguments[64];              /* Data used for storing per-task arguments. */
    /* ---- */
    pal_uint32_t                   StateTag;                   /* A packed tag value representing the state of the task. It combines the completion state, cancellation state, generation and permits ref count. */
    pal_sint32_t                   WorkCount;                  /* The number of work items that must complete before this task can complete. This value starts at 1, and adds 1 for each child task. */
    struct PAL_PERMITS_LIST       *Permits[15];                /* The set of permits lists associated with the task. This specifies the set of tasks that depend on completion of this task. */
} PAL_TASK_DATA;

/* @summary Define the data associated with a task pool, which is a pre-allocated block of task data.
 * Each PAL_TASK_POOL is bound to a thread. Only the bound thread may create tasks within the pool, 
 * though other threads may run, complete and delete those tasks. The task pool uses the virtual memory
 * system to limit memory usage. Each task pool can accomodate up to the limit of 65536 tasks.
 */
typedef struct PAL_CACHELINE_ALIGN PAL_TASK_POOL {             /* 384 bytes */
    struct PAL_TASK_SCHEDULER     *TaskScheduler;              /* The PAL_TASK_SCHEDULER that manages the task pool memory and the pool of worker threads used to execute tasks. */
    struct PAL_TASK_POOL         **TaskPoolList;               /* The scheduler's list of task pools. Used when attempting to steal work. */
    struct PAL_PERMITS_LIST       *PermListData;               /* A pointer to the start of the memory block used to store permit list data. Some portion of this data may only be reserved and not committed. */
    struct PAL_TASK_DATA          *TaskSlotData;               /* A pointer to the start of the memory block used to store task data. Some portion of this data may only be reserved and not committed. */
    PAL_TASKID                    *ReadyTaskIds;               /* The storage for a single-producer, multi-consumer concurrent queue storing the IDs of ready-to-run tasks. Pre-committed to maximum capacity. */
    pal_uint32_t                  *PermSlotIds;                /* The storage for a multi-producer, single-consumer concurrent queue storing the slot indices of available permit lists in PermitListData. */
    pal_uint32_t                  *FreeSlotIds;                /* The storage for a multi-producer, single-consumer concurrent queue storing the slot indices of available slots in TaskSlotData. */
    pal_uint32_t                   PoolIndex;                  /* The zero-based index of the task pool in the scheduler's task pool array. */
    pal_uint32_t                   PoolFlags;                  /* One or more bitwise ORd values from the PAL_TASK_POOL_FLAGS enumeration specifying the set of operations that can be performed by the thread that owns the pool. */
    /* ---- */                                                 /* Data used when putting worker threads to sleep (parking) or waking them up. */
    struct PAL_TASK_POOL          *NextParkPool;               /* A pointer to the next parked task pool, or NULL. */
    HANDLE                         ParkSemaphore;              /* The scheduler semaphore used to park the worker thread. Used only by CPU worker threads. */
    pal_uint64_t                   ParkEventCount;             /* The value of PAL_TASK_SCHEDULER::ParkEventCount last seen by the thread bound to this task pool. */
    pal_uint64_t                   WakeEventCount;             /* The value of PAL_TASK_SCHEDULER::WakeEventCount last seen by the thread bound to this task pool. */
    pal_uint32_t                   WakePoolId;                 /* The pool index of the PAL_TASK_POOL that woke up the worker thread. */
    pal_uint32_t                   Reserved1;                  /* Reserved for future use. Set to zero. */
    pal_uintptr_t                  Reserved2;                  /* Reserved for future use. Set to zero. */
    pal_uintptr_t                  Reserved3;                  /* Reserved for future use. Set to zero. */
    struct PAL_TASK_POOL          *NextFreePool;               /* A pointer to the next free pool of the same pool type, or NULL. */
    /* ---- */                                                 /* Data used when allocating task data slots or permits lists during TaskCreate and TaskPublish. */
    pal_uint32_t                   MaxTaskSlots;               /* The number of currently committed (backed by physical memory) instances of PAL_TASK_DATA. */
    pal_uint32_t                   MaxPermLists;               /* The number of currently committed (backed by physical memory) instances of PAL_TASK_DATA. */
    pal_uint64_t                   SlotAllocCount;             /* The counter used to track the consumer position in the FreeSlotIds list. */
    pal_uint64_t                   SlotAllocNext;              /* An internal counter used to allocate task slots from the available range. Always <= SlotAllocCount. */
    pal_uint64_t                   PermAllocCount;             /* The counter used to track the consumer position in the PermSlotIds list. */
    pal_uint64_t                   PermAllocNext;              /* An internal counter used to allocate permit list slots from the available range. Always <= PermAllocCount. */
    pal_uint8_t                   *UserDataBuffer;             /* Pointer to a per-pool buffer that can be used to store pool-local data. */
    pal_uint32_t                   UserDataSize;               /* The size of the user data section, in bytes. */
    pal_sint32_t                   PoolTypeId;                 /* One of the values of the PAL_TASK_POOL_TYPE_ID enumeration. Used mainly for debugging purposes. */
    pal_uint32_t                   OsThreadId;                 /* The operating system thread identifier of the thread bound to the task pool. */
    pal_uint32_t                   Reserved4;                  /* Reserved for future use. Set to zero. */
    /* ---- */                                                 /* Data used when returning task data or permit lists slots to the free lists during TaskComplete. */
    pal_uint64_t                   SlotFreeCount;              /* The counter used to track the producer position in the FreeSlotIds list. */
    pal_uint64_t                   PermFreeCount;              /* The counter used to track the producer position in the PermSlotIds list. */
    pal_uint8_t                    FreePad[48];                /* Padding used to separate the task slot free counter from other data to prevent false sharing. */
    /* ---- */
    pal_sint64_t                   ReadyPubPos;                /* A monotonically-increasing integer representing the position of the next dequeue operation in the ready-to-run queue. */
    pal_uint8_t                    ReadyPubPad[56];            /* Padding used to separate the private position (push, take) in the ready-to-run queue from the steal data. */
    /* ---- */
    pal_sint64_t                   ReadyPrvPos;                /* A monotonically-increasing integer representing the position of the next enqueue operation in the ready-to-run queue. */
    pal_uint8_t                    ReadyPrvPad[56];            /* Padding used to separate the public position (steal) in the ready-to-run queue from other shared data. */
    /* ---- */
} PAL_TASK_POOL;

/* MEMORY LAYOUT for PAL_TASK_POOL:
 * base: 
 *     | [PAL_TASK_POOL 448 bytes]
 *     | [UserDataStorage 4KB-448 bytes] [64 byte alignment]
 *     | [PermitSlotIds 128KB] [64 byte alignment]
 *     | [AllocSlotIds  128KB] [64 byte alignment]
 *     | [ReadyTaskIds  256KB] [64 byte alignment]
 *     | <--- TaskSlotData points here (base + 516KB)
 *     | [TaskSlotData committed (MaxTaskSlots * 256) bytes]
 *     | [TaskSlotData reserved ((65536 - MaxTaskSlots) * 256) bytes]
 *     | <--- PermitListData points here (base + 516KB + 16MB)
 *     | [PermitListData committed (MaxPermitLists * 128) bytes]
 *     | [PermitListData reserved ((65536 - MaxPermitLists) * 128) bytes]
 * end - (base + 516KB + 16MB + 8MB)
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
    struct PAL_TASK_POOL           **TaskPoolList;             /* An array of TaskPoolCount pointers to each task pool. Addressible by PAL_TASK_POOL::PoolIndex. */
    pal_uint32_t                    *PoolThreadId;             /* An array specifying the operating system thread ID bound to each task pool. */
    pal_uint32_t                     TaskPoolCount;            /* The total number of PAL_TASK_POOL instances, across all types, managed by the scheduler. */
    pal_uint32_t                     PoolTypeCount;            /* The number of distinct task pool types. */
    struct PAL_TASK_POOL_FREE_LIST  *PoolFreeList;             /* An array containing the free list for each task pool type. */
    pal_sint32_t                    *PoolTypeIds;              /* An array specifying the PAL_TASK_POOL_TYPE_ID for each free list. */
    pal_uint8_t                     *TaskPoolBase;             /* The base address of the first PAL_TASK_POOL. */
    HANDLE                           IoCompletionPort;         /* The I/O completion port used by the I/O worker threads. */
    pal_uint32_t                     CpuWorkerCount;           /* The number of worker threads used to execute non-blocking CPU work. */
    pal_uint32_t                     AioWorkerCount;           /* The number of worker threads used to execute blocking and non-blocking I/O work. */
    /* ---- */
    unsigned int                    *OsWorkerThreadIds;        /* An array of CpuWorkerCount+AioWorkerCount operating system identifiers for the worker threads. */
    HANDLE                          *OsWorkerThreadHandles;    /* An array of CpuWorkerCount+AioWorkerCount handles for the worker threads. */
    HANDLE                          *WorkerParkSemaphores;     /* An array of CpuWorkerCount binary semaphores used to park and wake worker threads. */
    pal_uint32_t                     ActiveThreadCount;        /* The number of active worker threads. */
    pal_uint32_t                     MainPoolIndex;            /* The zero-based index of the first task pool with type PAL_TASK_POOL_TYPE_ID_MAIN. */
    pal_uint32_t volatile            ShutdownSignal;           /* A flag set to non-zero when the scheduler is being terminated. */
    pal_uint32_t                     Reserved1;                /* Reserved for future use. Set to zero. */
    pal_uintptr_t                    Reserved2;                /* Reserved for future use. Set to zero. */
    pal_uintptr_t                    Reserved3;                /* Reserved for future use. Set to zero. */
    pal_uintptr_t                    Reserved4;                /* Reserved for future use. Set to zero. */
    /* ---- */
    pal_uint64_t                     WakeEventCount;           /* A monotonically-increasing integer value tracking the number of tasks made ready-to-run. */
    pal_uint32_t                     StealPoolSet[8];          /* An array containing the indices of the last eight task pools to publish events. */
    pal_uint8_t                      WakeEventPad[24];         /* Padding used to separate the ReadyEventCount from adjacent data. */
    /* ---- */
    SRWLOCK                          ParkStateLock;            /* Used to synchronize access to the parked worker state. */
    pal_uint64_t                     ParkEventCount;           /* A monotonically-increasing integer value incremented whenever a worker thread is parked. */
    struct PAL_TASK_POOL            *ParkedWorkers;            /* The head of the singly-linked list of parked worker threads. */
    pal_uint8_t                      ParkStatePad[40];         /* Padding used to separate the parked worker state from adjacent data. */
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
