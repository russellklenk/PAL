/**
 * @summary Implement the PAL task scheduler for the Win32 platform.
 */
#include "pal_win32_task.h"
#include "pal_win32_thread.h"
#include "pal_win32_memory.h"

/* @summary Define various constants related to the PAL_TASK_DATA::StateTag field.
 * The state tag maintains several bits of state data packed into a 32-bit integer value.
 *
 * The state tag consists of the following components:
 * 31......................|.|....|.....0
 *   UUUUUUUUUUUUUUUUUUUUUU|C|PPPP|GGGGG
 * Starting from the most-significant bit:
 * - U represents an unused bit.
 * - C is set if the task has been cancelled, or is clear otherwise.
 * - P tracks the number of values in the PAL_TASK_DATA::Permits array.
 * - G specifies the current generation value for the task data slot.
 *   When a task transitions to a completed state, the generation is incremented.
 */
#ifndef PAL_TASK_STATE_TAG_CONSTANTS
#   define PAL_TASK_STATE_TAG_CONSTANTS            
#   define PAL_TASK_STATE_TAG_CANCL_BITS           1
#   define PAL_TASK_STATE_TAG_NPERM_BITS           4
#   define PAL_TASK_STATE_TAG_GENER_BITS           PAL_TASKID_GENER_BITS
#   define PAL_TASK_STATE_TAG_GENER_SHIFT          0
#   define PAL_TASK_STATE_TAG_NPERM_SHIFT         (PAL_TASK_STATE_TAG_GENER_SHIFT + PAL_TASK_STATE_TAG_GENER_BITS)
#   define PAL_TASK_STATE_TAG_CANCL_SHIFT         (PAL_TASK_STATE_TAG_NPERM_SHIFT + PAL_TASK_STATE_TAG_NPERM_BITS)
#   define PAL_TASK_STATE_TAG_GENER_MASK         ((1UL << PAL_TASK_STATE_TAG_GENER_BITS) - 1)
#   define PAL_TASK_STATE_TAG_NPERM_MASK         ((1UL << PAL_TASK_STATE_TAG_NPERM_BITS) - 1)
#   define PAL_TASK_STATE_TAG_CANCL_MASK         ((1UL << PAL_TASK_STATE_TAG_CANCL_BITS) - 1)
#   define PAL_TASK_STATE_TAG_GENER_MASK_PACKED   (PAL_TASK_STATE_TAG_GENER_MASK << PAL_TASK_STATE_TAG_GENER_SHIFT)
#   define PAL_TASK_STATE_TAG_NPERM_MASK_PACKED   (PAL_TASK_STATE_TAG_NPERM_MASK << PAL_TASK_STATE_TAG_NPERM_SHIFT)
#   define PAL_TASK_STATE_TAG_CANCL_MASK_PACKED   (PAL_TASK_STATE_TAG_CANCL_MASK << PAL_TASK_STATE_TAG_CANCL_SHIFT)
#   define PAL_TASK_STATE_TAG_MAX_PERMITS_LISTS  ((1UL << PAL_TASK_STATE_TAG_NPERM_BITS) - 1)
#   define PAL_TASK_NOT_CANCELLED                  0
#   define PAL_TASK_CANCELLED                      1
#endif

/* @summary Define the value sent to the completion port and received in the lpCompletionKey parameter of GetQueuedCompletionStatus when the thread should terminate.
 */
#ifndef PAL_COMPLETION_KEY_SHUTDOWN
#   define PAL_COMPLETION_KEY_SHUTDOWN             PAL_TASKID_NONE
#endif

/* @summary Define the value sent to the completion port and received in the lpCompletionKey parameter of GetQueuedCompletionStatus when the thread receives a task ID.
 */
#ifndef PAL_COMPLETION_KEY_TASKID
#   define PAL_COMPLETION_KEY_TASKID              (~(ULONG_PTR) 1)
#endif

/* @summary Define the number of permit lists committed when the task pool permit list storage needs to grow.
 * Each PAL_PERMITS_LIST is 128 bytes, so committing a single chunk commits 16KB, up to a maximum of 8MB.
 */
#ifndef PAL_PERMIT_LIST_CHUNK_SIZE
#   define PAL_PERMIT_LIST_CHUNK_SIZE              128U
#endif

/* @summary Define the maximum number of permit list chunks that can be committed for a task pool.
 */
#ifndef PAL_PERMIT_LIST_CHUNK_COUNT
#   define PAL_PERMIT_LIST_CHUNK_COUNT            (PAL_TASKID_MAX_SLOTS_PER_POOL / PAL_PERMIT_LIST_CHUNK_SIZE)
#endif

/* @summary Define the number of slots committed when the task pool data storage needs to grow.
 * Each PAL_TASK_DATA is 256 bytes, so committing a single chunk commits 256KB, up to a maximum of 16MB.
 */
#ifndef PAL_TASK_DATA_CHUNK_SIZE
#   define PAL_TASK_DATA_CHUNK_SIZE                1024U
#endif

/* @summary Define the maximum number of task data chunks that can be committed for a task pool.
 */
#ifndef PAL_TASK_DATA_CHUNK_COUNT
#   define PAL_TASK_DATA_CHUNK_COUNT              (PAL_TASKID_MAX_SLOTS_PER_POOL / PAL_TASK_DATA_CHUNK_SIZE)
#endif

/* @summary Check a task state tag to determine whether the task has been marked as cancelled.
 * @param _tag The state tag to query.
 * @return Non-zero if the task has its cancellation bit set.
 */
#ifndef PAL_TaskStateTagIsTaskCancelled
#define PAL_TaskStateTagIsTaskCancelled(_tag)                                  \
    (((_tag) & PAL_TASK_STATE_TAG_CANCL_MASK_PACKED) != 0)
#endif

/* @summary Retrieve the number of items in the PAL_TASK_DATA::Permits array from a task state tag.
 * @param _tag The state tag to query.
 * @return The number of valid entries in the PAL_TASK_DATA::Permits array.
 */
#ifndef PAL_TaskStateTagGetPermitsCount
#define PAL_TaskStateTagGetPermitsCount(_tag)                                  \
    (((_tag) & PAL_TASK_STATE_TAG_NPERM_MASK_PACKED) >> PAL_TASK_STATE_TAG_NPERM_SHIFT)
#endif

/* @summary Update a task state tag, setting the number of entries in the PAL_TASK_DATA::Permits array and preserving the remaining values.
 * @param _tag The state tag value.
 * @param _n The number of entries in the Permits array.
 * @return The updated tag value.
 */
#ifndef PAL_TaskStateTagUpdatePermitsCount
#define PAL_TaskStateTagUpdatePermitsCount(_tag, _n)                           \
    (((_tag) & ~PAL_TASK_STATE_TAG_NPERM_MASK_PACKED) | (((_n) & PAL_TASK_STATE_TAG_NPERM_MASK) << PAL_TASK_STATE_TAG_NPERM_SHIFT))
#endif

/* @summary Retrieve the task slot generation value from a task state tag.
 * @param _tag The state tag to query.
 * @return The generation value of the task data slot.
 */
#ifndef PAL_TaskStateTagGetGeneration
#define PAL_TaskStateTagGetGeneration(_tag)                                    \
    (((_tag) & PAL_TASK_STATE_TAG_GENER_MASK_PACKED) >> PAL_TASK_STATE_TAG_GENER_SHIFT)
#endif

/* @summary Construct a task state tag value from its constituent parts.
 * @param _cancl The cancellation status of the task. This value should be 1 for cancelled, or 0 for not cancelled.
 * @param _nperm The number of items in the PAL_TASK_DATA::Permits array.
 * @param _gener The generation value for the task data slot.
 * @return The packed task state tag value.
 */
#ifndef PAL_TaskStateTagPack
#define PAL_TaskStateTagPack(_cancl, _nperm, _gener)                           \
   ((((_cancl) & PAL_TASK_STATE_TAG_CANCL_MASK) << PAL_TASK_STATE_TAG_CANCL_SHIFT) | \
    (((_nperm) & PAL_TASK_STATE_TAG_NPERM_MASK) << PAL_TASK_STATE_TAG_NPERM_SHIFT) | \
    (((_gener) & PAL_TASK_STATE_TAG_GENER_MASK) << PAL_TASK_STATE_TAG_GENER_SHIFT))
#endif

/* @summary Extract the PAL_TASK_DATA array index value from a freelist value.
 * @param _flval The freelist value read from PAL_TASK_POOL::AllocSlotIds.
 * @return The zero-based index of the free entry in PAL_TASK_POOL::TaskSlotData.
 */
#ifndef PAL_TaskSlotGetSlotIndex
#define PAL_TaskSlotGetSlotIndex(_flval)                                       \
    (((_flval) & 0xFFFF0000UL) >> 16)
#endif

/* @summary Extract the slot generation from a freelist value.
 * @param _flval The freelist value read from PAL_TASK_POOL::AllocSlotIds.
 * @return The generation value to assign to the next ID using the associated entry in PAL_TASK_POOL::TaskSlotData.
 */
#ifndef PAL_TaskSlotGetSlotGeneration
#define PAL_TaskSlotGetSlotGeneration(_flval)                                  \
    (((_flval) & 0x0000FFFFUL))
#endif

/* @summary Generate a packed freelist value from a slot index and generation.
 * @param _slot The zero-based index of the free entry in PAL_TASK_POOL::TaskSlotData.
 * @param _gen The next generation value to assign to the slot.
 * @return The value to write to the PAL_TASK_POOL::AllocSlotIds array.
 */
#ifndef PAL_TaskSlotPack
#define PAL_TaskSlotPack(_slot, _gen)                                          \
    (((_slot) << 16) | (_gen))
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

/* @summary Define the result values that can be returned from PAL_TaskSchedulerParkWorker.
 */
typedef enum PAL_TASK_SCHEDULER_PARK_RESULT {
    PAL_TASK_SCHEDULER_PARK_RESULT_SHUTDOWN   =  0, /* The thread was woken up because the scheduler is being shutdown. */
    PAL_TASK_SCHEDULER_PARK_RESULT_TRY_STEAL  =  1, /* The scheduler produced a list of at least one pool with available tasks; the thread should attempt to steal some work. */
    PAL_TASK_SCHEDULER_PARK_RESULT_WAKE_TASK  =  2, /* The thread was woken up and should check the task pool's WakeupTaskId field for a task to execute. */
} PAL_TASK_SCHEDULER_PARK_RESULT;

/* @summary Define the result values that can be returned from PAL_TaskSchedulerWakeWorker.
 */
typedef enum PAL_TASK_SCHEDULER_WAKE_RESULT {
    PAL_TASK_SCHEDULER_WAKE_RESULT_QUEUED     =  0, /* The scheduler queued the ready-to-run task for later processing. */
    PAL_TASK_SCHEDULER_WAKE_RESULT_WAKEUP     =  1, /* The scheduler woke a waiting thread to process the task. */
} PAL_TASK_SCHEDULER_WAKE_RESULT;

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
    void                        *init_context,
    void                     **thread_context
)
{
    PAL_UNUSED_ARG(task_scheduler);
    PAL_UNUSED_ARG(thread_task_pool);
    PAL_UNUSED_ARG(init_context);
    PAL_Assign(thread_context, NULL);
    return 0;
}

/* @summary Convert a permit list pointer into the corresponding array index.
 * @param owner_pool The PAL_TASK_POOL from which the permits list was allocated.
 * The PAL_PERMITS_LIST::PoolIndex field for p_list must match the PAL_TASK_POOL::PoolIndex field for owner_pool.
 * @param p_list The permits list for which the array index will be calculated.
 * @return The zero-based array index of p_list within the PermitListData field of owner_pool.
 */
static PAL_INLINE pal_uint32_t
PAL_TaskPoolGetSlotIndexForPermitList
(
    struct PAL_TASK_POOL *owner_pool, 
    struct PAL_PERMITS_LIST  *p_list
)
{
    pal_uint8_t *p_base = (pal_uint8_t*) owner_pool->PermitListData;
    pal_uint8_t *p_addr = (pal_uint8_t*) p_list;
    return (pal_uint32_t)((p_addr - p_base) / sizeof(PAL_PERMITS_LIST));
}

/* @summary Return a task slot to the free list for the owning task pool.
 * This operation is performed during final completion of a task; that is, when the task and all of its child tasks have completed.
 * @param owner_pool The PAL_TASK_POOL from which the task slot was allocated.
 * @param slot_index The zero-based index of the PAL_TASK_DATA slot to mark as available.
 * @param generation The generation value associated with the PAL_TASK_DATA slot.
 */
static void
PAL_TaskPoolMakeTaskSlotFree /* this only does the return - not the generation update */
(
    struct PAL_TASK_POOL *owner_pool,
    pal_uint32_t          slot_index,
    pal_uint32_t          generation
)
{
    pal_uint32_t      packed = PAL_TaskSlotPack(slot_index, generation);
    pal_uint32_t *free_slots = owner_pool->AllocSlotIds;
    pal_uint64_t   write_pos = owner_pool->SlotFreeCount;
    pal_uint64_t       value;

    _ReadWriteBarrier(); /* load-acquire for owner_pool->SlotFreeCount */

    for ( ; ; ) {
        /* write the slot_index to the AllocSlotIds list */
        free_slots[write_pos & 0xFFFF] = packed;
        /* attempt to update and 'claim' the free count */
        if ((value =(pal_uint64_t)_InterlockedCompareExchange64((volatile LONGLONG*)&owner_pool->SlotFreeCount, (LONGLONG)(write_pos+1), (LONGLONG) write_pos)) == write_pos) {
            /* the count was successfully updated */
            return;
        }
        /* try again with a new write position */
        write_pos = value;
    }
}

/* @summary Return a permits list to the free list for the owning task pool.
 * This operation is performed during final completion of a task that prevents one or more tasks from running.
 * @param owner_pool The PAL_TASK_POOL from which the permits list was allocated.
 * @param list_index The zero-based index of the PAL_PERMITS_LIST being returned to the free pool.
 * This index can be calculated using PAL_TaskPoolGetSlotIndexForPermitList.
 */
static void
PAL_TaskPoolMakePermitsListFree /* this only does the return - not the generation update */
(
    struct PAL_TASK_POOL *owner_pool, 
    pal_uint32_t          list_index
)
{
    pal_uint32_t *free_slots = owner_pool->PermitSlotIds;
    pal_uint64_t   write_pos = owner_pool->PermitFreeCount;
    pal_uint64_t       value;

    _ReadWriteBarrier(); /* load-acquire for owner_pool->PermitFreeCount */

    for ( ; ; ) {
        /* write the slot index to the PermitSlotIds list */
        free_slots[write_pos & 0xFFFF] = list_index;
        /* attempt to update and 'claim' the free count */
        if ((value =(pal_uint64_t)_InterlockedCompareExchange64((volatile LONGLONG*)&owner_pool->PermitFreeCount, (LONGLONG)(write_pos+1), (LONGLONG) write_pos)) == write_pos) {
            /* the free count was successfully updated */
            return;
        }
        /* try again with a new write position */
        write_pos = value;
    }
}

/* @summary Commit one or more additional chunks of task slot data, backing it with physical memory.
 * This operation is performed when allocating task IDs during PAL_TaskCreate.
 * @param thread_pool The PAL_TASK_POOL bound to the thread executing PAL_TaskCreate.
 * @param slots_needed The number of additional task data slots required by the call to PAL_TaskCreate.
 */
static void
PAL_TaskPoolCommitTaskDataChunks
(
    struct PAL_TASK_POOL *thread_pool, 
    pal_uint32_t         slots_needed
)
{
    if (thread_pool->MaxTaskSlots < PAL_TASKID_MAX_SLOTS_PER_POOL) {
        void           *base_addr =&thread_pool->TaskSlotData[thread_pool->MaxTaskSlots];
        pal_uint32_t  chunks_need =(slots_needed + (PAL_TASK_DATA_CHUNK_SIZE-1)) / PAL_TASK_DATA_CHUNK_SIZE;
        pal_uint32_t  slots_added = chunks_need  *  PAL_TASK_DATA_CHUNK_SIZE;
        pal_usize_t  commit_bytes = slots_added  *  sizeof(PAL_TASK_DATA);
        pal_uint32_t      new_pos = thread_pool->MaxTaskSlots;
        pal_uint32_t      new_max = thread_pool->MaxTaskSlots + slots_added;
        if (VirtualAlloc(base_addr, commit_bytes, MEM_COMMIT, PAGE_READWRITE) == base_addr) {
            thread_pool->MaxTaskSlots = new_max;
            do { /* add the new slots to the free list */
                PAL_TaskPoolMakeTaskSlotFree(thread_pool, new_pos++, 0);
            } while (new_pos < new_max);
        }
    }
}

/* @summary Commit one or more additional chunks of task slot data, backing it with physical memory.
 * This operation is performed when publishing tasks with dependencies during PAL_TaskPublish.
 * @param thread_pool The PAL_TASK_POOL bound to the thread executing PAL_TaskPublish.
 * @param lists_needed The number of additional permit lists required by the call to PAL_TaskPublish.
 */
static void
PAL_TaskPoolCommitPermitsListChunks
(
    struct PAL_TASK_POOL *thread_pool, 
    pal_uint32_t         lists_needed
)
{
    if (thread_pool->MaxPermitLists < PAL_TASKID_MAX_SLOTS_PER_POOL) {
        void           *base_addr =&thread_pool->PermitListData[thread_pool->MaxPermitLists];
        pal_uint32_t  chunks_need =(lists_needed + (PAL_PERMIT_LIST_CHUNK_SIZE-1)) / PAL_PERMIT_LIST_CHUNK_SIZE;
        pal_uint32_t  lists_added = chunks_need  *  PAL_PERMIT_LIST_CHUNK_SIZE;
        pal_usize_t  commit_bytes = lists_added  *  sizeof(PAL_PERMITS_LIST);
        pal_uint32_t      new_pos = thread_pool->MaxPermitLists;
        pal_uint32_t      new_max = thread_pool->MaxPermitLists + lists_added;
        if (VirtualAlloc(base_addr, commit_bytes, MEM_COMMIT, PAGE_READWRITE) == base_addr) {
            thread_pool->MaxPermitLists = new_max;
            do { /* add the new slots to the free list */
                PAL_TaskPoolMakePermitsListFree(thread_pool, new_pos++);
            } while (new_pos < new_max);
        }
    }
}

/* @summary Allocate one or more permits lists from a task pool.
 * This operation is performed when publishing tasks and converting dependencies into permits.
 * The calling thread is blocked until all permits lists can be returned.
 * @param owner_pool The PAL_TASK_POOL that is publishing the potentially blocked tasks.
 * @param list_array An array to be filled with pointers to list_count permit lists.
 * @param list_count The number of PAL_PERMITS_LIST objects to allocate from the pool.
 * @return Zero if the permits lists were successfully allocated, or non-zero if an error occurred.
 */
static int
PAL_TaskPoolAllocatePermitsLists
(
    struct PAL_TASK_POOL     *owner_pool, 
    struct PAL_PERMITS_LIST **list_array, 
    pal_uint32_t              list_count
)
{
    PAL_PERMITS_LIST *p_base = owner_pool->PermitListData;
    pal_uint32_t  *slot_list = owner_pool->PermitSlotIds;
    pal_uint64_t   alloc_max = owner_pool->PermitAllocCount;
    pal_uint64_t   alloc_cur = owner_pool->PermitAllocNext;
    pal_uint32_t write_index = 0;
    pal_uint32_t  freelist_v;

    if (list_count <= PAL_TASKID_MAX_SLOTS_PER_POOL) {
        for ( ; ; ) {
            if (alloc_cur == alloc_max) {
                /* refill the claimed set */
                alloc_cur  =(pal_uint64_t)_InterlockedExchange64((volatile LONGLONG*) &owner_pool->PermitAllocCount, (LONGLONG) owner_pool->PermitFreeCount);
                alloc_max  = owner_pool->PermitAllocCount;
                if (alloc_cur == alloc_max) {
                    if (owner_pool->MaxPermitLists < PAL_TASKID_MAX_SLOTS_PER_POOL) {
                        /* commit additional permit lists, if possible */
                        PAL_TaskPoolCommitPermitsListChunks(owner_pool, list_count - write_index);
                    } else {
                        /* no choice but to wait for some work to finish and try again */
                        YieldProcessor();
                    }
                }
            }
            while (alloc_cur != alloc_max) {
                /* allocate from the claimed set */
                freelist_v = slot_list[(alloc_cur++) & 0xFFFF];
                list_array[write_index++] = &p_base[freelist_v];
                if (write_index == list_count) {
                    owner_pool->PermitAllocNext = alloc_cur;
                    return 0;
                }
            }
        }
    } else {
        /* attempt to allocate too many permits lists */
        return -1;
    }
}

/* @summary Attempt to publish a permits list to a task.
 * @param blocking_task The PAL_TASK_DATA associated with the task that is preventing the tasks in the permits list from running.
 * @param permits_list The PAL_PERMITS_LIST specifying the task identifiers of the tasks that cannot run until the blocking task completes.
 * @param blocking_task_id The identifier of the blocking task.
 * @return One if the blocking task has not completed and the permits list was published, or zero otherwise.
 */
static pal_uint32_t
PAL_TaskDataPublishPermitsList
(
    struct PAL_TASK_DATA   *blocking_task, 
    struct PAL_PERMITS_LIST *permits_list, 
    PAL_TASKID           blocking_task_id
)
{
    pal_uint32_t    task_gen = PAL_TaskIdGetGeneration(blocking_task_id);
    pal_uint32_t   state_tag = blocking_task->StateTag;
    PAL_PERMITS_LIST **slots = blocking_task->Permits;
    pal_uint32_t    data_num = PAL_TaskStateTagGetPermitsCount(state_tag);
    pal_uint32_t    data_gen = PAL_TaskStateTagGetGeneration(state_tag);
    pal_uint32_t     new_tag;
    pal_uint32_t       value;

    _ReadWriteBarrier(); /* load-acquire for blocking_task->StateTag */

    for ( ; ; ) {
        if (task_gen == data_gen && data_num <= PAL_TASK_STATE_TAG_MAX_PERMITS_LISTS) {
            /* write to the next available slot index */
            slots[data_num] = permits_list;
            new_tag = PAL_TaskStateTagUpdatePermitsCount(state_tag, data_num+1);
            /* attempt to update and 'claim' the slot */
            /* this races with the following operations:
             * 1. A concurrent PAL_TaskDataPublishPermitsList to blocking_task_id from another thread. This operation claims a slot in the Permits array and updates the permit count of StateTag.
             * 2. A concurrent final completion of blocking_task_id on another thread. This operation claims the Permits array and updates the generation of StateTag.
             * 3. A concurrent cancellation of blocking_task_id on another thread. This operation sets the cancel bit of StateTag.
             */
            if ((value =(pal_uint32_t)_InterlockedCompareExchange((volatile LONG*)&blocking_task->StateTag, (LONG) new_tag, (LONG) state_tag)) == state_tag) {
                /* the permits list was successfully published */
                return 1;
            }
            /* the blocking_task->StateTag was updated - try again */
            data_num  = PAL_TaskStateTagGetPermitsCount(value);
            data_gen  = PAL_TaskStateTagGetGeneration(value);
            state_tag = value;
        } else {
            /* the generations differ - blocking_task_id has already completed */
            /* if the following assert fires, that means that the task structure 
             * needs to be examined - there are too many tasks depending on the 
             * completion of blocking_task_id. if possible, batch-publish tasks. */
            assert(data_num <= PAL_TASK_STATE_TAG_MAX_PERMITS_LISTS);
            return 0;
        }
    }
}

/* @summary Push the task ID of a ready-to-run task onto the private end of the ready-to-run deque.
 * This thread can only be called by the thread that owns the supplied PAL_TASK_POOL.
 * @param worker_pool The PAL_TASK_POOL bound to the thread that made the task ready-to-run.
 * @param task_id The identifier of the ready-to-run task.
 */
static void
PAL_TaskPoolPushReadyTask
(
    struct PAL_TASK_POOL *worker_pool,
    PAL_TASKID                task_id
)
{   /* TODO: make this a push(N) operation */
    PAL_TASKID  *stor = worker_pool->ReadyTaskIds;
    pal_sint64_t mask = PAL_TASKID_MAX_SLOTS_PER_POOL - 1;
    pal_sint64_t  pos = worker_pool->ReadyPrivatePos;
    stor[pos & mask]  = task_id;
    _ReadWriteBarrier();
    worker_pool->ReadyPrivatePos = pos + 1;
}

/* @summary Attempt to take the task ID of a ready-to-run task from the private end of the ready-to-run deque.
 * This function can only be called by the thread that owns the supplied PAL_TASK_POOL.
 * @param worker_pool The PAL_TASK_POOL bound to the thread calling the function.
 * @return The task identifier, or PAL_TASKID_NONE.
 */
static PAL_TASKID
PAL_TaskPoolTakeReadyTask
(
    struct PAL_TASK_POOL *worker_pool
)
{
    PAL_TASKID  *stor = worker_pool->ReadyTaskIds;
    pal_sint64_t mask = PAL_TASKID_MAX_SLOTS_PER_POOL - 1;
    pal_sint64_t  pos = worker_pool->ReadyPrivatePos  - 1;
    pal_sint64_t  top;
    PAL_TASKID    res = PAL_TASKID_NONE;

    _InterlockedExchange64(&worker_pool->ReadyPrivatePos, pos);
    top = worker_pool->ReadyPublicPos;

    if (top <= pos) {
        res  = stor[pos & mask];
        if (top != pos) {
            /* there's at least one more item in the deque - no need to race */
            return res;
        }
        /* this was the final item in the deque - race a concurrent steal */
        if (_InterlockedCompareExchange64(&worker_pool->ReadyPublicPos, top+1, top) == top) {
            /* this thread won the race */
            worker_pool->ReadyPrivatePos = top + 1;
            return res;
        } else {
            /* this thread lost the race */
            worker_pool->ReadyPrivatePos = top + 1;
            return PAL_TASKID_NONE;
        }
    } else {
        /* the deque is currently empty */
        worker_pool->ReadyPrivatePos = top;
        return PAL_TASKID_NONE;
    }
}

/* @summary Attempt to steal a ready-to-run task from the ready queue owned by another thread.
 * This function can be called by any thread in the system.
 * @param victim_pool The PAL_TASK_POOL to attempt to steal from.
 * @return The task identifer of the stolen task, or PAL_TASKID_NONE.
 */
static PAL_TASKID
PAL_TaskPoolStealReadyTask
(
    struct PAL_TASK_POOL *victim_pool
)
{
    PAL_TASKID  *stor = victim_pool->ReadyTaskIds;
    pal_sint64_t mask = PAL_TASKID_MAX_SLOTS_PER_POOL - 1;
    pal_sint64_t  top = victim_pool->ReadyPublicPos;
    _ReadWriteBarrier();
    pal_sint64_t  pos = victim_pool->ReadyPrivatePos;
    PAL_TASKID    res = PAL_TASKID_NONE;
    if (top < pos) {
        res = stor[top & mask];
        if (_InterlockedCompareExchange64(&victim_pool->ReadyPublicPos, top+1, top) == top) {
            /* this thread claims the item */
            return res;
        } else {
            /* this thread lost the race */
            return PAL_TASKID_NONE;
        }
    }
    return PAL_TASKID_NONE;
}

static void
PAL_TaskPoolCompleteTask
(
    struct PAL_TASK_SCHEDULER  *scheduler,
    struct PAL_TASK_POOL *completion_pool,
    struct PAL_TASK_POOL      *owner_pool,
    struct PAL_TASK_DATA       *task_data,
    pal_uint32_t               pool_index,
    pal_uint32_t               slot_index,
    pal_uint32_t               generation
)
{
    PAL_UNUSED_ARG(scheduler);
    PAL_UNUSED_ARG(completion_pool);
    PAL_UNUSED_ARG(owner_pool);
    PAL_UNUSED_ARG(task_data);
    PAL_UNUSED_ARG(pool_index);
    PAL_UNUSED_ARG(slot_index);
    PAL_UNUSED_ARG(generation);
#if 0
    PAL_TASK_POOL **pool_list = scheduler->TaskPoolList;
    PAL_TASKID   *permit_list = task_data->PermitTasks;
    pal_uint32_t  ready_count = 0;
    pal_sint32_t   work_count;
    pal_sint32_t permit_count;
    PAL_TASKID ready_list[15];  /* limited by the size of the permit list */
    pal_sint32_t            i;

    if ((work_count = _InterlockedExchangeAdd((volatile LONG*)&task_data->WorkCount, -1)) == 1) {
        /* the task and all of its child tasks have completed */
        for (i = 0, permit_count = _InterlockedExchange((volatile LONG*)&task_data->PermitCount, -1); i < permit_count; ++i) {
        }
        if (ready_count > 0) {
            /* flush tasks that are now ready-to-run.
             * wake worker or add to queue for completion_pool. */
        }
        if (task_data->PublicData.ParentId != PAL_TASKID_NONE) {
            /* TODO: bubble completion up to the parent task */
        }
        /* TODO: should mark slot free? or require explicit delete? */
        PAL_TaskPoolMakeTaskSlotFree(owner_pool, slot_index, (generation+1) & PAL_TASKID_GENER_MASK);
    }
#endif
}

/* @summary Attempt to park (put to sleep) a worker thread.
 * Worker threads are only put into a wait state when there's no work available.
 * This function should be called when a worker runs out of work in its local ready-to-run queue.
 * @param scheduler The PAL_TASK_SCHEDULER managing the scheduler state.
 * @param worker_pool The PAL_TASK_POOL owned by the calling thread, which will be parked if no work is available.
 * @param steal_list An array of values to populate with candidate victim pool indices.
 * @param max_steal_list The maximum number of items that can be written to steal_list.
 * @param num_steal_list On return, the number of valid entries in the steal list is stored in this location.
 * @return One of the values of the PAL_TASK_SCHEDULER_PARK_RESULT enumeration.
 */
static pal_sint32_t
PAL_TaskSchedulerParkWorker
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
    DWORD            wait_rc = WAIT_OBJECT_0;
    pal_uint32_t        i, n;

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
                    /* no events have been published, and we didn't find any victim candidates */
                    goto park_thread;
                }
            }
        } while (n_steal == 0);
    }
    PAL_Assign(num_steal_list, n_steal);
    return PAL_TASK_SCHEDULER_PARK_RESULT_TRY_STEAL;

park_thread:
    if ((wait_rc = WaitForSingleObject(worker_pool->ParkSemaphore, INFINITE)) == WAIT_OBJECT_0) {
        if (scheduler->ShutdownSignal) {
            /* woken due to shutdown */
            PAL_Assign(num_steal_list, 0);
            return PAL_TASK_SCHEDULER_PARK_RESULT_SHUTDOWN;
        } else {
            /* woken due to work item */
            PAL_Assign(num_steal_list, 0);
            return PAL_TASK_SCHEDULER_PARK_RESULT_WAKE_TASK;
        }
    } else {
        /* some error occurred while waiting */
        PAL_Assign(num_steal_list, 0);
        return PAL_TASK_SCHEDULER_PARK_RESULT_SHUTDOWN;
    }
}

/* @summary Potentially wake up a waiting thread to process a ready-to-run task.
 * If no workers are waiting, the task is queued for later processing.
 * This function should be called when a thread makes a task ready-to-run via a publish or complete operation.
 * @param scheduler The PAL_TASK_SCHEDULER managing the scheduler state.
 * @param worker_pool The PAL_TASK_POOL owned by the calling thread, which executed the publish or complete operation.
 * @param give_task The identifier of the task that is ready-to-run.
 * @return One of the values of the PAL_TASK_SCHEDULER_WAKE_RESULT enumeration.
 */
static pal_sint32_t
PAL_TaskSchedulerWakeWorker /* to be called when a worker makes a task ready-to-run via publish or complete */
(
    struct PAL_TASK_SCHEDULER *scheduler,
    struct PAL_TASK_POOL    *worker_pool,
    PAL_TASKID                 give_task
)
{
    pal_uint32_t *stack = scheduler->ParkedPoolIds;
    pal_sint32_t    tos = scheduler->ParkedPoolToS;
    pal_uint32_t    idx;
    pal_sint32_t  value;

    _ReadWriteBarrier();

    for ( ; ; ) {
        if (tos == 0) {
            /* no workers are waiting; push the task onto the local ready-to-run queue */
            PAL_TaskPoolPushReadyTask(worker_pool, give_task);
            scheduler->TaskPoolERTR[worker_pool->PoolIndex]++;
            _InterlockedIncrement64((volatile LONGLONG*)&scheduler->ReadyEventCount);
            return PAL_TASK_SCHEDULER_WAKE_RESULT_QUEUED;
        }
        idx = stack[tos-1];
        if ((value =(pal_sint32_t)_InterlockedCompareExchange((volatile LONG*)&scheduler->ParkedPoolToS, tos-1, tos)) == tos) {
            /* successfully popped a worker, assign to mail slot */
            PAL_TASK_POOL *woken  = scheduler->TaskPoolList[idx];
            woken->WakeupTaskId   = give_task;
            ReleaseSemaphore(woken->ParkSemaphore, 1, NULL);
            return PAL_TASK_SCHEDULER_WAKE_RESULT_WAKEUP;
        } else {
            /* try again */
            tos = value;
        }
    }
}

/* @summary Attempt to steal work from another thread's ready-to-run queue.
 * @param scheduler The PAL_TASK_SCHEDULER managing the set of PAL_TASK_POOL instances.
 * @param worker_pool The PAL_TASK_POOL bound to the thread that is attempting to steal work.
 * @param steal_list An array of task pool indicies representing the set of victim candidates.
 * @param num_steal_list The number of valid entries in the steal_list array.
 * @param start_index The zero-based index within steal_list at which the first steal attempt should be made.
 * @param next_start On return, this location is updated with the next value to supply for start_index.
 * @return A valid task ID, or PAL_TASKID_NONE if no work could be stolen.
 */
static PAL_TASKID
PAL_TaskSchedulerStealWork
(
    struct PAL_TASK_SCHEDULER *scheduler,
    pal_uint32_t const       *steal_list,
    pal_uint32_t const    num_steal_list,
    pal_uint32_t             start_index,
    pal_uint32_t             *next_start
)
{
    PAL_TASK_POOL  **pool_list = scheduler->TaskPoolList;
    PAL_TASK_POOL *victim_pool = NULL;
    PAL_TASKID     stolen_task = PAL_TASKID_NONE;
    pal_uint32_t          i, n;

    if (start_index >= num_steal_list) {
        /* reset to the start of the list */
        start_index  = 0;
    }
    /* attempt to steal work from one of the candidate victims */
    for (i = start_index, n = num_steal_list; i < n; ++i) {
        victim_pool = pool_list[steal_list[i]];
        stolen_task = PAL_TaskPoolStealReadyTask(victim_pool);
        if (stolen_task != PAL_TASKID_NONE) {
            /* the steal was successful */
            _InterlockedDecrement((volatile LONG*)&scheduler->TaskPoolERTR[steal_list[i]]);
            return stolen_task;
        }
    }
    /* processed the entire steal_list with no success */
    PAL_Assign(next_start, 0);
    return PAL_TASKID_NONE;
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
    PAL_TASK_POOL        **pool_list = scheduler->TaskPoolList;
    PAL_TASK_POOL       *thread_pool = NULL;
    PAL_TASK_DATA         *task_data = NULL;
    void                 *thread_ctx = NULL;
    unsigned int           exit_code = 0;
    PAL_TASKID          current_task = PAL_TASKID_NONE;
    pal_sint32_t        pool_type_id = PAL_TASK_POOL_TYPE_ID_CPU_WORKER;
    pal_uint32_t     pool_bind_flags = PAL_TASK_POOL_BIND_FLAGS_NONE;
    pal_sint32_t         wake_reason = PAL_TASK_SCHEDULER_PARK_RESULT_WAKE_TASK;
    pal_uint32_t         steal_count = 0;
    pal_uint32_t         steal_index = 0;
    PAL_TASK_ARGS          task_args;
    pal_uint32_t          pool_index;
    pal_uint32_t          slot_index;
    pal_uint32_t          generation;
    pal_uint32_t       steal_list[4];

    /* set the thread name for diagnostics tools */
    PAL_ThreadSetName("CPU Worker");

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

    /* set constant fields of PAL_TASK_ARGS */
    task_args.TaskScheduler = scheduler;
    task_args.ExecutionPool = thread_pool;
    task_args.ThreadContext = thread_ctx;
    task_args.ThreadId      = thread_pool->OsThreadId;
    task_args.ThreadIndex   = thread_pool->PoolIndex;
    task_args.ThreadCount   = scheduler->TaskPoolCount;

    /* thread initialization has completed */
    SetEvent(init->ReadySignal); init = NULL;

    __try {
        while (scheduler->ShutdownSignal == 0) {
            /* wait for work to become available */
            switch ((wake_reason = PAL_TaskSchedulerParkWorker(scheduler, thread_pool, steal_list, PAL_CountOf(steal_list), &steal_count))) {
                case PAL_TASK_SCHEDULER_PARK_RESULT_SHUTDOWN:
                    { current_task = PAL_TASKID_NONE;
                    } break;
                case PAL_TASK_SCHEDULER_PARK_RESULT_TRY_STEAL:
                    { current_task = PAL_TaskSchedulerStealWork(scheduler, steal_list, steal_count, steal_index, &steal_index);
                    } break;
                case PAL_TASK_SCHEDULER_PARK_RESULT_WAKE_TASK:
                    { current_task = thread_pool->WakeupTaskId;
                    } break;
                default:
                    { current_task = PAL_TASKID_NONE;
                    } break;
            }
            while (current_task != PAL_TASKID_NONE) {
                /* set up and execute the task */
                pool_index = PAL_TaskIdGetTaskPoolIndex(current_task);
                slot_index = PAL_TaskIdGetTaskSlotIndex(current_task);
                generation = PAL_TaskIdGetGeneration(current_task);
                task_data  =&pool_list[pool_index]->TaskSlotData[slot_index];
                task_args.TaskArguments = task_data->Arguments;
                task_args.TaskId = current_task;
                task_data->PublicData.TaskMain(&task_args);
                if (task_data->PublicData.CompletionType == PAL_TASK_COMPLETION_TYPE_AUTOMATIC) {
                    /* TODO: PAL_TaskComplete */
                }

                /* executing the task may have produced one or more additional
                 * work items in the ready-to-run deque for this thread.
                 * keep executing tasks until the queue drains.
                 */
                current_task = PAL_TaskPoolTakeReadyTask(thread_pool);
            }
        }
    }
    __finally {
        PAL_TaskSchedulerReleaseTaskPool(scheduler, thread_pool);
        return exit_code;
    }
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

    /* set the thread name for diagnostics tools */
    PAL_ThreadSetName("I/O Worker");

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
        owake += nwake;
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
    reserve_size += PAL_AllocationSizeArray(pal_uint32_t    , PAL_TASKID_MAX_SLOTS_PER_POOL); /* PermitSlotIds */
    reserve_size += PAL_AllocationSizeArray(pal_uint32_t    , PAL_TASKID_MAX_SLOTS_PER_POOL); /* AllocSlotIds  */
    reserve_size += PAL_AllocationSizeArray(PAL_TASKID      , PAL_TASKID_MAX_SLOTS_PER_POOL); /* ReadyTaskIds  */
    commit_size   = reserve_size;
    reserve_size += PAL_AllocationSizeArray(PAL_PERMITS_LIST, PAL_TASKID_MAX_SLOTS_PER_POOL); /* PermitListData */
    reserve_size += PAL_AllocationSizeArray(PAL_TASK_DATA   , PAL_TASKID_MAX_SLOTS_PER_POOL); /* TaskSlotData */
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
        pal_uint32_t     chunk_size = PAL_TASK_DATA_CHUNK_SIZE;
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
            pool->UserDataBuffer    = PAL_MemoryArenaAllocateHostArray(&pool_arena, pal_uint8_t     , pool_size.UserDataSize);
            pool->PermitSlotIds     = PAL_MemoryArenaAllocateHostArray(&pool_arena, pal_uint32_t    , PAL_TASKID_MAX_SLOTS_PER_POOL);
            pool->AllocSlotIds      = PAL_MemoryArenaAllocateHostArray(&pool_arena, pal_uint32_t    , PAL_TASKID_MAX_SLOTS_PER_POOL);
            pool->ReadyTaskIds      = PAL_MemoryArenaAllocateHostArray(&pool_arena, PAL_TASKID      , PAL_TASKID_MAX_SLOTS_PER_POOL);
            pool->TaskSlotData      = PAL_MemoryArenaAllocateHostArray(&pool_arena, PAL_TASK_DATA   , chunk_size * chunk_count);
            pool->PermitListData    = PAL_MemoryArenaAllocateHostArray(&pool_arena, PAL_PERMITS_LIST, PAL_TASKID_MAX_SLOTS_PER_POOL);
            pool->ParkSemaphore     = NULL;
            pool->PoolIndex         = global_pool_index;
            pool->PoolFlags         = type->PoolFlags;
            pool->TaskPoolList      = scheduler->TaskPoolList;
            pool->NextFreePool      = scheduler->PoolFreeList[type_index].FreeListHead;
            pool->MaxTaskSlots      = chunk_count * chunk_size;
            pool->MaxPermitLists    = 0; /* none pre-committed */
            pool->SlotAllocNext     = 0;
            pool->PermitAllocNext   = 0;
            pool->UserDataSize      = pool_size.UserDataSize;
            pool->WakeupTaskId      = PAL_TASKID_NONE;
            pool->OsThreadId        = 0;
            pool->PoolTypeId        = type->PoolTypeId;
            pool->SlotAllocCount    = 0;
            pool->PermitAllocCount  = 0;
            pool->SlotFreeCount     = chunk_count * chunk_size;
            pool->PermitFreeCount   = 0;
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
        PAL_PERMITS_LIST *permit_data = task_pool->PermitListData;
        PAL_TASK_DATA      *task_data = task_pool->TaskSlotData;
        pal_uint32_t   *data_slot_ids = task_pool->AllocSlotIds;
        pal_uint32_t *permit_slot_ids = task_pool->PermitSlotIds;

        if ((bind_flags & PAL_TASK_POOL_BIND_FLAG_MANUAL) == 0) {
            PAL_TaskSchedulerBindPoolToThread(scheduler, task_pool, GetCurrentThreadId());
        } else {
            task_pool->OsThreadId = 0;
        }

        /* initialize all of the task state and free lists.
         * note that this is potentially zeroing megabytes of memory.
         */
        PAL_ZeroMemory(task_data, task_pool->MaxTaskSlots * sizeof(PAL_TASK_DATA));
        for (i = 0, n = task_pool->MaxTaskSlots; i < n; ++i) {
            data_slot_ids[i] = PAL_TaskSlotPack (i , 0);
        }
        PAL_ZeroMemory(permit_data, task_pool->MaxPermitLists * sizeof(PAL_PERMITS_LIST));
        for (i = 0, n = task_pool->MaxPermitLists; i < n; ++i) {
            permit_slot_ids[i] = i;
        }

        /* initialize the queues and counters */
        task_pool->SlotAllocNext    = 0;
        task_pool->PermitAllocNext  = 0;
        task_pool->WakeupTaskId     = PAL_TASKID_NONE;
        task_pool->SlotAllocCount   = 0;
        task_pool->PermitAllocCount = 0;
        task_pool->ReadyPublicPos   = 0;
        task_pool->ReadyPrivatePos  = 0;
       _InterlockedExchange64((volatile LONGLONG*) &task_pool->SlotFreeCount  , (LONGLONG) task_pool->MaxTaskSlots);
       _InterlockedExchange64((volatile LONGLONG*) &task_pool->PermitFreeCount, (LONGLONG) task_pool->MaxPermitLists);
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

PAL_API(int)
PAL_TaskCreate
(
    struct PAL_TASK_POOL *thread_pool,
    PAL_TASKID          *task_id_list,
    pal_uint32_t           task_count, 
    PAL_TASKID            parent_task
)
{
    pal_uint32_t write_index = 0;
    pal_uint32_t  pool_index = thread_pool->PoolIndex;
    pal_uint32_t  *slot_list = thread_pool->AllocSlotIds;
    pal_uint64_t   alloc_max = thread_pool->SlotAllocCount;
    pal_uint64_t   alloc_cur = thread_pool->SlotAllocNext;
    pal_uint32_t  slot_index;
    pal_uint32_t  generation;
    pal_uint32_t  freelist_v;

    if (task_count <= PAL_TASKID_MAX_SLOTS_PER_POOL) {
        for ( ; ; ) {
            if (alloc_cur == alloc_max) {
                /* refill the claimed set */
                alloc_cur =(pal_uint64_t)_InterlockedExchange64((volatile LONGLONG*) &thread_pool->SlotAllocCount, (LONGLONG) thread_pool->SlotFreeCount);
                alloc_max = thread_pool->SlotAllocCount;
                if (alloc_cur == alloc_max) {
                    if (thread_pool->MaxTaskSlots < PAL_TASKID_MAX_SLOTS_PER_POOL) {
                        /* commit one or more additional chunks of task data */
                        PAL_TaskPoolCommitTaskDataChunks(thread_pool, task_count - write_index);
                    } else {
                        /* no choice but to wait for some work to finish and try again */
                        YieldProcessor();
                    }
                }
            }
            while (alloc_cur != alloc_max) {
                /* allocate from the claimed set */
                freelist_v = slot_list[(alloc_cur++) & 0xFFFF];
                slot_index = PAL_TaskSlotGetSlotIndex(freelist_v);
                generation = PAL_TaskSlotGetSlotGeneration(freelist_v);
                task_id_list[write_index++] = PAL_TaskIdPack(pool_index, slot_index, generation);
                if (write_index == task_count) {
                    thread_pool->SlotAllocNext = alloc_cur;
                    goto alloc_complete;
                }
            }
        }

alloc_complete:
        if (parent_task != PAL_TASKID_NONE) {
            /* increment the outstanding work counter on the parent task.
             * the caller must still use PAL_TaskGetData to set the ParentId field for each child. */
            pal_uint32_t   parent_pool = PAL_TaskIdGetTaskPoolIndex(parent_task);
            pal_uint32_t   parent_slot = PAL_TaskIdGetTaskSlotIndex(parent_task);
            PAL_TASK_DATA *parent_data =&thread_pool->TaskPoolList [parent_pool]->TaskSlotData[parent_slot];
            _InterlockedExchangeAdd((volatile LONG*) &parent_data->WorkCount, (LONG) task_count);
        }
        return  0;
    } else {
        /* attempt to allocate too many task IDs */
        return -1;
    }
}

PAL_API(struct PAL_TASK*)
PAL_TaskGetData
(
    struct PAL_TASK_POOL *thread_pool,
    PAL_TASKID                task_id,
    void              **argument_data,
    pal_usize_t   *argument_data_size
)
{
    PAL_TASK_DATA  *task_data = NULL;
    PAL_TASK_POOL  *task_pool = NULL;
    PAL_TASK_POOL **pool_list = thread_pool->TaskPoolList;
    pal_uint32_t   pool_index = PAL_TaskIdGetTaskPoolIndex(task_id);
    pal_uint32_t   slot_index = PAL_TaskIdGetTaskSlotIndex(task_id);

    if (PAL_TaskIdGetValid(task_id)) {
        task_pool = pool_list[pool_index];
        task_data =&task_pool->TaskSlotData[slot_index];
        PAL_Assign(argument_data, task_data->Arguments);
        PAL_Assign(argument_data_size, sizeof(task_data->Arguments));
        return &task_data->PublicData;
    } else {
        /* the task identifier is invalid */
        PAL_Assign(argument_data, NULL);
        PAL_Assign(argument_data_size, 0);
        return NULL;
    }
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
