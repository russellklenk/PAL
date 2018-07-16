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
#   define PAL_TASK_MAX_PERMITS_PER_LIST           30
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

/* @summary Atomically load a 32-bit unsigned integer value, increment it, and store the result back to the same address.
 * @param _addr The address containing the value to modify.
 * @return The resulting incremented value.
 */
#ifndef PAL_AtomicIncrement_u32
#define PAL_AtomicIncrement_u32(_addr)                                         \
    (pal_uint32_t)_InterlockedIncrement((volatile LONG*)&(_addr))
#endif

/* @summary Atomically load a 64-bit unsigned integer value, increment it, and store the result back to the same address.
 * @param _addr The address containing the value to modify.
 * @return The resulting incremented value.
 */
#ifndef PAL_AtomicIncrement_u64
#define PAL_AtomicIncrement_u64(_addr)                                         \
    (pal_uint64_t)_InterlockedIncrement64((volatile LONGLONG*)&(_addr))
#endif

/* @summary Atomically load a 32-bit signed integer value, decrement it, and store the result back to the same address.
 * @param _addr The address containing the value to modify.
 * @return The resulting decremented value.
 */
#ifndef PAL_AtomicDecrement_s32
#define PAL_AtomicDecrement_s32(_addr)                                         \
    (pal_sint32_t)_InterlockedDecrement((volatile LONG*)&(_addr))
#endif

/* @summary Atomically load a 64-bit unsigned integer value, decrement it, and store the result back to the same address.
 * @param _addr The address containing the value to modify.
 * @return The resulting decremented value.
 */
#ifndef PAL_AtomicDecrement_u64
#define PAL_AtomicDecrement_u64(_addr)                                         \
    (pal_uint64_t)_InterlockedDecrement64((volatile LONGLONG*)&(_addr))
#endif

/* @summary Atomically load a 64-bit signed integer value, store a new value to the memory location, and return the original value.
 * @param _addr The address containing the value to modify.
 * @param _val The value to store at the given address.
 * @return The value stored at the address prior to the update.
 */
#ifndef PAL_AtomicExchange_s64
#define PAL_AtomicExchange_s64(_addr, _val)                                    \
    (pal_sint64_t)_InterlockedExchange64((volatile LONGLONG*)&(_addr), (LONGLONG)(_val))
#endif

/* @summary Atomically load a 64-bit unsigned integer value, store a new value to the memory location, and return the original value.
 * @param _addr The address containing the value to modify.
 * @param _val The value to store at the given address.
 * @return The value stored at the address prior to the update.
 */
#ifndef PAL_AtomicExchange_u64
#define PAL_AtomicExchange_u64(_addr, _val)                                    \
    (pal_uint64_t)_InterlockedExchange64((volatile LONGLONG*)&(_addr), (LONGLONG)(_val))
#endif

/* @summary Atomically load a 32-bit unsigned integer value, compare it with an expected value, and if they match, store another value in its place.
 * @param _addr The address containing the value to modify.
 * @param _expect The value the caller expects to be stored at the given address.
 * @param _desire The value the caller wants to store at the given address if the current value matches the expected value.
 * @return The value that was loaded from the given address.
 */
#ifndef PAL_AtomicCompareAndSwap_u32
#define PAL_AtomicCompareAndSwap_u32(_addr, _expect, _desire)                  \
    (pal_uint32_t)_InterlockedCompareExchange((volatile LONG*)&(_addr), (LONG)(_desire), (LONG)(_expect))
#endif

/* @summary Atomically load a 64-bit signed integer value, compare it with an expected value, and if they match, store another value in its place.
 * @param _addr The address containing the value to modify.
 * @param _expect The value the caller expects to be stored at the given address.
 * @param _desire The value the caller wants to store at the given address if the current value matches the expected value.
 * @return The value that was loaded from the given address.
 */
#ifndef PAL_AtomicCompareAndSwap_s64
#define PAL_AtomicCompareAndSwap_s64(_addr, _expect, _desire)                  \
    (pal_sint64_t)_InterlockedCompareExchange64((volatile LONGLONG*)&(_addr), (LONGLONG)(_desire), (LONGLONG)(_expect))
#endif

/* @summary Atomically load a 64-bit unsigned integer value, compare it with an expected value, and if they match, store another value in its place.
 * @param _addr The address containing the value to modify.
 * @param _expect The value the caller expects to be stored at the given address.
 * @param _desire The value the caller wants to store at the given address if the current value matches the expected value.
 * @return The value that was loaded from the given address.
 */
#ifndef PAL_AtomicCompareAndSwap_u64
#define PAL_AtomicCompareAndSwap_u64(_addr, _expect, _desire)                  \
    (pal_uint64_t)_InterlockedCompareExchange64((volatile LONGLONG*)&(_addr), (LONGLONG)(_desire), (LONGLONG)(_expect))
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

/* @summary Initialize a PAL_TASK_ARGS structure in preparation for invoking a task callback.
 * @param args The PAL_TASK_ARGS instance to initialize.
 * @param data The PAL_TASK_DATA associated with the task. Specify NULL if no task is being executed or completed.
 * @param context The PAL_TASK_CALLBACK_CONTEXT associated with the thread that will execute the callback.
 * @param task_id The identifier of the task being executed or completed. Specify PAL_TASKID_NONE of no task is being executed or completed.
 */
static void
PAL_TaskArgsInit
(
    struct PAL_TASK_ARGS                *args, 
    struct PAL_TASK_DATA                *data, 
    struct PAL_TASK_CALLBACK_CONTEXT *context, 
    PAL_TASKID                        task_id
)
{
    args->TaskScheduler = context->TaskScheduler;
    args->CallbackPool  = context->CallbackPool;
    args->TaskArguments = data->Arguments;
    args->ThreadContext = context->ThreadContext;
    args->TaskId        = task_id;
    args->ThreadId      = context->ThreadId;
    args->ThreadIndex   = context->ThreadIndex;
    args->ThreadCount   = context->ThreadCount;
}

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
    pal_uint32_t    packed = PAL_TaskSlotPack(slot_index, generation);
    pal_uint64_t write_pos =(PAL_AtomicIncrement_u64(owner_pool->SlotFreeCount) - 1) & 0xFFFFULL;
    owner_pool->AllocSlotIds[write_pos] = packed;
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
    pal_uint64_t  write_pos =(PAL_AtomicIncrement_u64(owner_pool->PermitFreeCount) - 1) & 0xFFFFULL;
    owner_pool->PermitSlotIds[write_pos] = list_index;
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
                alloc_cur  = PAL_AtomicExchange_u64(owner_pool->PermitAllocCount, owner_pool->PermitFreeCount);
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
            if ((value = PAL_AtomicCompareAndSwap_u32(blocking_task->StateTag, state_tag, new_tag)) == state_tag) {
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
 * This function can only be called by the thread that owns the supplied PAL_TASK_POOL.
 * @param worker_pool The PAL_TASK_POOL bound to the thread that made the task ready-to-run.
 * @param task_id The identifier of the ready-to-run task.
 */
static void
PAL_TaskPoolPushReadyTask
(
    struct PAL_TASK_POOL *worker_pool,
    PAL_TASKID                task_id
)
{
    PAL_TASKID  *stor = worker_pool->ReadyTaskIds;
    pal_sint64_t mask = PAL_TASKID_MAX_SLOTS_PER_POOL - 1;
    pal_sint64_t  pos = worker_pool->ReadyPrivatePos;
    stor[pos & mask]  = task_id;
    _ReadWriteBarrier();
    worker_pool->ReadyPrivatePos = pos + 1;
}

/* @summary Push one or more ready-to-run task IDs onto the private end of the ready-to-run deque.
 * @param This function can only be called by the thread that owns the supplied task pool.
 * @param worker_pool The PAL_TASK_POOL bound to the thread that made the task ready-to-run.
 * @param task_list The list of task IDs that are ready-to-run.
 * @param task_count The number of tasks in the task_list.
 */
static void
PAL_TaskPoolPushReadyTaskList
(
    struct PAL_TASK_POOL *worker_pool, 
    PAL_TASKID             *task_list, 
    pal_uint32_t           task_count
)
{
    PAL_TASKID  *stor = worker_pool->ReadyTaskIds;
    pal_sint64_t mask = PAL_TASKID_MAX_SLOTS_PER_POOL - 1;
    pal_sint64_t  pos = worker_pool->ReadyPrivatePos;
    pal_sint64_t    p;
    pal_uint32_t    i;
    for (i = 0, p = pos; i < task_count; ++i, ++p) {
        stor[p & mask] = task_list[i];
    }
    _ReadWriteBarrier();
    worker_pool->ReadyPrivatePos = p;
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

    PAL_AtomicExchange_s64(worker_pool->ReadyPrivatePos, pos);
    top = worker_pool->ReadyPublicPos;

    if (top <= pos) {
        res  = stor[pos & mask];
        if (top != pos) {
            /* there's at least one more item in the deque - no need to race */
            return res;
        }
        /* this was the final item in the deque - race a concurrent steal */
        if (PAL_AtomicCompareAndSwap_s64(worker_pool->ReadyPublicPos, top, top+1) == top) {
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
        if (PAL_AtomicCompareAndSwap_s64(victim_pool->ReadyPublicPos, top, top+1) == top) {
            /* this thread claims the item */
            return res;
        } else {
            /* this thread lost the race */
            return PAL_TASKID_NONE;
        }
    }
    return PAL_TASKID_NONE;
}

/* @summary Attempt to park (put to sleep) a worker thread.
 * Worker threads are only put into a wait state when there's no work available.
 * This function should be called when a worker runs out of work in its local ready-to-run queue.
 * @param scheduler The PAL_TASK_SCHEDULER managing the scheduler state.
 * @param worker_pool The PAL_TASK_POOL owned by the calling thread, which will be parked if no work is available.
 * @param steal_list An array of values to populate with candidate victim pool indices. This array should be able to store eight items.
 * @return One of the values of the PAL_TASK_SCHEDULER_PARK_RESULT enumeration.
 */
static pal_sint32_t
PAL_TaskSchedulerParkWorker
(
    struct PAL_TASK_SCHEDULER *scheduler, 
    struct PAL_TASK_POOL    *worker_pool, 
    pal_uint32_t             *steal_list
)
{
    pal_uint64_t event_count = scheduler->ReadyEventCount;
    PAL_PARKED_WORKER  *slot = NULL;
    DWORD            wait_rc;

    if (PAL_AtomicExchange_u64(worker_pool->ReadyEventCount, scheduler->ReadyEventCount) != event_count) {
        /* one or more tasks have been published - attempt to steal work from another thread */
        PAL_CopyMemory(steal_list, scheduler->StealPoolSet, sizeof(scheduler->StealPoolSet));
        return PAL_TASK_SCHEDULER_PARK_RESULT_TRY_STEAL;
    } else {
        AcquireSRWLockExclusive(&scheduler->ParkStateLock);
        /* make the park visible to wake operations */
        PAL_AtomicIncrement_u64(scheduler->ParkEventCount);
        /* prepare to park the worker thread */
        slot = &scheduler->ParkedWorkers[scheduler->ParkedWorkerCount];
        slot->ParkSemaphore = worker_pool->ParkSemaphore;
        slot->WakeupTaskId  =&worker_pool->WakeupTaskId;
        /* double-check that no tasks have been published */
        if (PAL_AtomicExchange_u64(worker_pool->ReadyEventCount, scheduler->ReadyEventCount) == event_count) {
            /* no task has been published where the waker hasn't seen the park - put the worker to sleep */
            scheduler->ParkedWorkerCount++;
            ReleaseSRWLockExclusive(&scheduler->ParkStateLock);
            if ((wait_rc = WaitForSingleObject(worker_pool->ParkSemaphore, INFINITE)) == WAIT_OBJECT_0) {
                if (scheduler->ShutdownSignal) {
                    return PAL_TASK_SCHEDULER_PARK_RESULT_SHUTDOWN;
                } else {
                    return PAL_TASK_SCHEDULER_PARK_RESULT_WAKE_TASK;
                }
            } else {
                /* an error occurred during the wait */
                return PAL_TASK_SCHEDULER_PARK_RESULT_SHUTDOWN;
            }
        } else {
            /* a task has been published during the park operation - don't actually sleep */
            ReleaseSRWLockExclusive(&scheduler->ParkStateLock);
            PAL_CopyMemory(steal_list, scheduler->StealPoolSet, sizeof(scheduler->StealPoolSet));
            return PAL_TASK_SCHEDULER_PARK_RESULT_TRY_STEAL;
        }
    }
}

/* @summary Potentially wake up one or more waiting threads to process ready-to-run tasks.
 * If no workers are waiting, or more tasks are available than workers are waiting, tasks are queued for later processing.
 * This function should be called when a thread makes one or more tasks ready-to-run via a publish or complete operation.
 * @param scheduler The PAL_TASK_SCHEDULER managing the scheduler state.
 * @param worker_pool The PAL_TASK_POOL owned by the calling thread, which executed the publish or complete operation.
 * @param give_tasks The array of ready-to-run task identifiers.
 * @param task_count The number of items in the give_tasks array. This value must be greater than zero.
 * @return One of the values of the PAL_TASK_SCHEDULER_WAKE_RESULT enumeration.
 */
static pal_sint32_t
PAL_TaskSchedulerWakeWorkers
(
    struct PAL_TASK_SCHEDULER *scheduler, 
    struct PAL_TASK_POOL    *worker_pool, 
    PAL_TASKID               *give_tasks, 
    pal_uint32_t              task_count
)
{
    pal_uint32_t  pool_index = worker_pool->PoolIndex;
    pal_uint64_t event_count = scheduler->ParkEventCount;
    pal_uint64_t  slot_index;
    pal_uint32_t    n_queued;
    pal_uint32_t     n_woken;
    pal_uint32_t     i, t, w;

    /* claim a slot in StealPoolSet */
    slot_index = (PAL_AtomicIncrement_u64(scheduler->ReadyEventCount) - 1) & 7;
    scheduler->StealPoolSet[slot_index] = pool_index;

    /* determine whether to wake up parked threads or not */
    if (PAL_AtomicExchange_u64(worker_pool->ParkEventCount, scheduler->ParkEventCount) == event_count) {
        /* no threads have gone to sleep since this thread last made work available */
        if (worker_pool->ParkedWorkerCount == 0) {
            /* no threads waiting - queue all tasks */
            PAL_TaskPoolPushReadyTaskList(worker_pool, give_tasks, task_count);
            return PAL_TASK_SCHEDULER_WAKE_RESULT_QUEUED;
        }
    }
    /* potentially wake up worker threads */
    AcquireSRWLockExclusive(&scheduler->ParkStateLock); {
        worker_pool->ParkEventCount    = scheduler->ParkEventCount;
        worker_pool->ParkedWorkerCount = scheduler->ParkedWorkerCount;
        if (task_count > scheduler->ParkedWorkerCount) {
            /* wake up all parked worker threads */
            n_woken    = scheduler->ParkedWorkerCount;
            n_queued   = task_count - scheduler->ParkedWorkerCount;
            scheduler->ParkedWorkerCount = 0;
            /* queue tasks to hopefully prevent other threads from parking */
            PAL_TaskPoolPushReadyTaskList(worker_pool, give_tasks, n_queued);
        } else {
            /* wake up a portion of parked worker threads */
            scheduler->ParkedWorkerCount -= task_count;
            n_woken    = task_count;
            n_queued   = 0;
        }
        for (i = 0, t = n_queued, w = n_woken - 1; i < n_woken; ++i) {
            PAL_PARKED_WORKER *worker = &scheduler->ParkedWorkers[w--];
            PAL_Assign(worker->WakeupTaskId, give_tasks[t++]);
            ReleaseSemaphore(worker->ParkSemaphore, 1, NULL);
        }
    } ReleaseSRWLockExclusive(&scheduler->ParkStateLock);
    return PAL_TASK_SCHEDULER_WAKE_RESULT_WAKEUP;
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
            return stolen_task;
        }
    }
    /* processed the entire steal_list with no success */
    PAL_Assign(next_start, 0);
    return PAL_TASKID_NONE;
}

/* @summary Register completion of a task. This may make additional tasks ready to run.
 * @param ctx The PAL_TASK_CALLBACK_CONTEXT associated with the calling thread.
 * @param owner_pool The PAL_TASK_POOL bound to the thread that created the task.
 * @param task_data The PAL_TASK_DATA associated with the completed task.
 * @param task_id The identifier of the completed task.
 * @param slot_index The zero-based index of the task data slot associated with the completed task.
 * @param generation The generation value of the task data slot associated with the completed task.
 */
static void
PAL_TaskPoolCompleteTask
(
    struct PAL_TASK_CALLBACK_CONTEXT *ctx, 
    struct PAL_TASK_POOL      *owner_pool,
    struct PAL_TASK_DATA       *task_data,
    PAL_TASKID                    task_id, 
    pal_uint32_t               slot_index,
    pal_uint32_t               generation
)
{
    if (PAL_AtomicDecrement_s32(task_data->WorkCount) == -1) {
        /* this task should transition to the completed state.
         * no other thread can process the completion, but this thread may have 
         * to race other threads that might be updating the task permits.
         * the state tag for a completed task clears the cancellation bit, 
         * sets the number of permits to zero, and increments the generation.
         */
        PAL_TASK_SCHEDULER *scheduler = ctx->TaskScheduler;
        pal_uint32_t          new_gen =(generation + 1) & PAL_TASK_STATE_TAG_GENER_MASK;
        pal_uint32_t         done_tag = new_gen << PAL_TASK_STATE_TAG_GENER_SHIFT;
        pal_uint32_t        state_tag = task_data->StateTag;
        pal_uint32_t      num_permits;
        pal_uint32_t            value;
        pal_uint32_t             i, j;
        PAL_TASKID        parent_task;
        PAL_TASK_ARGS            args;
        
        _ReadWriteBarrier();
        
        for ( ; ; ) {
            /* since we have the task data, reset the work count for when this slot is reused */
            task_data->WorkCount = 0;
            /* atomically update the state tag, which transitions the task state to completed */
            if ((value = PAL_AtomicCompareAndSwap_u32(task_data->StateTag, state_tag, done_tag)) == state_tag) {
                /* the task is transitioned to the completed state */
                break;
            }
            /* another thread attempted to modify the task state - try again */
            state_tag = value;
        }

        /* run the task completion callback */
        PAL_TaskArgsInit(&args, task_data, ctx, task_id);
        task_data->PublicData.TaskComplete(&args);

        /* bubble completion up to the parent task */
        if ((parent_task = task_data->PublicData.ParentId) != PAL_TASKID_NONE) {
            pal_uint32_t   parent_pidx = PAL_TaskIdGetTaskPoolIndex(parent_task);
            pal_uint32_t   parent_slot = PAL_TaskIdGetTaskSlotIndex(parent_task);
            pal_uint32_t   parent_genr = PAL_TaskIdGetGeneration(parent_task);
            PAL_TASK_POOL *parent_pool = scheduler->TaskPoolList[parent_pidx];
            PAL_TASK_DATA *parent_data =&scheduler->TaskPoolList[parent_pidx]->TaskSlotData[parent_slot];
            PAL_TaskPoolCompleteTask(ctx, parent_pool, parent_data, parent_task, parent_slot, parent_genr);
        }

        /* process any associated permits lists */
        for (i = 0, num_permits = PAL_TaskStateTagGetPermitsCount(state_tag); i < num_permits; ++i) {
            PAL_PERMITS_LIST *p = task_data->Permits[i];
            if (PAL_AtomicIncrement_u32(p->WaitCount) == 0) {
                /* all tasks in p->TaskList are now ready-to-run */
                PAL_TASK_POOL *permit_owner = scheduler->TaskPoolList[p->PoolIndex];
                pal_uint32_t   permit_index = PAL_TaskPoolGetSlotIndexForPermitList(permit_owner, p);
                for (j = 0; p->TaskList[j] != PAL_TASKID_NONE; ++j) { /* empty */ }
                PAL_TaskSchedulerWakeWorkers(scheduler, ctx->CallbackPool, p->TaskList, j);
                PAL_TaskPoolMakePermitsListFree(permit_owner, permit_index);
            }
        }

        /* allow the task data slot to be reused */
        PAL_TaskPoolMakeTaskSlotFree(owner_pool, slot_index, new_gen);
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
    PAL_TASK_POOL        **pool_list = scheduler->TaskPoolList;
    PAL_TASK_POOL       *thread_pool = NULL;
    PAL_TASK_DATA         *task_data = NULL;
    void                 *thread_ctx = NULL;
    unsigned int           exit_code = 0;
    PAL_TASKID          current_task = PAL_TASKID_NONE;
    pal_sint32_t        pool_type_id = PAL_TASK_POOL_TYPE_ID_CPU_WORKER;
    pal_uint32_t     pool_bind_flags = PAL_TASK_POOL_BIND_FLAGS_NONE;
    pal_sint32_t         wake_reason = PAL_TASK_SCHEDULER_PARK_RESULT_WAKE_TASK;
    pal_uint32_t         steal_count = 8;
    pal_uint32_t         steal_index = 0;
    PAL_TASK_CALLBACK_CONTEXT   exec;
    PAL_TASK_ARGS          task_args;
    pal_uint32_t          pool_index;
    pal_uint32_t          slot_index;
    pal_uint32_t          generation;
    pal_uint32_t       steal_list[8];

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

    /* initialize the PAL_TASK_CALLBACK_CONTEXT and PAL_TASK_ARGS */
    PAL_TaskCallbackContextInit(&exec, scheduler, thread_pool, thread_ctx);
    PAL_TaskArgsInit(&task_args, NULL, &exec, PAL_TASKID_NONE);

    /* thread initialization has completed */
    SetEvent(init->ReadySignal); init = NULL;

    __try {
        while (scheduler->ShutdownSignal == 0) {
            /* wait for work to become available */
            switch ((wake_reason = PAL_TaskSchedulerParkWorker(scheduler, thread_pool, steal_list))) {
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
                task_args.TaskId        = current_task;
                task_data->PublicData.TaskMain(&task_args);
                if (task_data->PublicData.CompletionType == PAL_TASK_COMPLETION_TYPE_AUTOMATIC) {
                    PAL_TaskPoolCompleteTask(&exec, pool_list[pool_index], task_data, current_task, slot_index, generation);
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
    PAL_TASK_CALLBACK_CONTEXT   exec;
    PAL_TASK_ARGS          task_args;

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

    /* initialize the PAL_TASK_CALLBACK_CONTEXT and PAL_TASK_ARGS */
    PAL_TaskCallbackContextInit(&exec, scheduler, thread_pool, thread_ctx);
    PAL_TaskArgsInit(&task_args, NULL, &exec, PAL_TASKID_NONE);

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
    reserve_size += PAL_AllocationSizeArray(pal_uint32_t           , count_info->TotalPoolCount); /* PoolThreadId  */
    reserve_size += PAL_AllocationSizeArray(PAL_TASK_POOL_FREE_LIST, init->PoolTypeCount);        /* PoolFreeList  */
    reserve_size += PAL_AllocationSizeArray(pal_sint32_t           , init->PoolTypeCount);        /* PoolTypeIds   */
    reserve_size += PAL_AllocationSizeArray(unsigned int           , thread_count);               /* OsWorkerThreadIds */
    reserve_size += PAL_AllocationSizeArray(HANDLE                 , thread_count);               /* OsWorkerThreadHandles */
    reserve_size += PAL_AllocationSizeArray(HANDLE                 , init->CpuWorkerThreadCount); /* WorkerParkSemaphores */
    reserve_size += PAL_AllocationSizeArray(PAL_PARKED_WORKER      , init->CpuWorkerThreadCount); /* ParkedWorkers */
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
    scheduler->TaskPoolList         = PAL_MemoryArenaAllocateHostArray(&scheduler_arena, PAL_TASK_POOL*         , counts.TotalPoolCount);
    scheduler->PoolThreadId         = PAL_MemoryArenaAllocateHostArray(&scheduler_arena, pal_uint32_t           , counts.TotalPoolCount);
    scheduler->TaskPoolCount        = counts.TotalPoolCount;
    scheduler->PoolTypeCount        = init->PoolTypeCount;
    scheduler->PoolFreeList         = PAL_MemoryArenaAllocateHostArray(&scheduler_arena, PAL_TASK_POOL_FREE_LIST, init->PoolTypeCount);
    scheduler->PoolTypeIds          = PAL_MemoryArenaAllocateHostArray(&scheduler_arena, pal_sint32_t           , init->PoolTypeCount);
    scheduler->TaskPoolBase         = pool_base;
    scheduler->IoCompletionPort     = iocp;
    scheduler->CpuWorkerCount       = init->CpuWorkerThreadCount;
    scheduler->AioWorkerCount       = init->AioWorkerThreadCount;
    scheduler->OsWorkerThreadIds    = PAL_MemoryArenaAllocateHostArray(&scheduler_arena, unsigned int           , worker_thread_count);
    scheduler->OsWorkerThreadHandles= PAL_MemoryArenaAllocateHostArray(&scheduler_arena, HANDLE                 , worker_thread_count);
    scheduler->WorkerParkSemaphores = PAL_MemoryArenaAllocateHostArray(&scheduler_arena, HANDLE                 , init->CpuWorkerThreadCount);
    scheduler->ShutdownSignal       = 0;
    scheduler->ActiveThreadCount    = 0;
    scheduler->Reserved1            = 0;
    scheduler->Reserved2            = 0;
    scheduler->Reserved3            = 0;
    scheduler->Reserved4            = 0;
    scheduler->ReadyEventCount      = 0;
    scheduler->ParkEventCount       = 0;
    scheduler->ParkedWorkers        = PAL_MemoryArenaAllocateHostArray(&scheduler_arena, PAL_PARKED_WORKER      , init->CpuWorkerThreadCount);
    scheduler->ParkedWorkerCount    = 0;
    scheduler->Reserved5            = 0;
    InitializeSRWLock(&scheduler->ParkStateLock);

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
        InitializeSRWLock(&scheduler->PoolFreeList[type_index].TypeLock);
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
        task_pool->SlotAllocNext      = 0;
        task_pool->PermitAllocNext    = 0;
        task_pool->WakeupTaskId       = PAL_TASKID_NONE;
        task_pool->SlotAllocCount     = 0;
        task_pool->PermitAllocCount   = 0;
        task_pool->ParkEventCount     =~0ULL;
        task_pool->ReadyEventCount    =~0ULL;
        task_pool->ParkedWorkerCount  =~0ULL;
        task_pool->ReadyPublicPos     = 0;
        task_pool->ReadyPrivatePos    = 0;
        PAL_AtomicExchange_u64(task_pool->SlotFreeCount  , task_pool->MaxTaskSlots);
        PAL_AtomicExchange_u64(task_pool->PermitFreeCount, task_pool->MaxPermitLists);
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
PAL_TaskCallbackContextInit
(
    struct PAL_TASK_CALLBACK_CONTEXT *context, 
    struct PAL_TASK_SCHEDULER      *scheduler, 
    struct PAL_TASK_POOL         *thread_pool, 
    void                      *thread_context
)
{
    if (context && scheduler && thread_pool) {
        if (thread_pool->OsThreadId == 0) {
            assert(thread_pool->OsThreadId != 0 && "Use PAL_TaskSchedulerBindPoolToThread to bind to an OS thread");
            PAL_ZeroMemory(context, sizeof(PAL_TASK_CALLBACK_CONTEXT));
            return -1;
        }
        context->TaskScheduler = scheduler;
        context->CallbackPool  = thread_pool;
        context->ThreadContext = thread_context;
        context->ThreadId      = thread_pool->OsThreadId;
        context->ThreadIndex   = thread_pool->PoolIndex;
        context->ThreadCount   = scheduler->TaskPoolCount;
        return 0;
    }
    return -1;
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
                alloc_cur = PAL_AtomicExchange_u64(thread_pool->SlotAllocCount, thread_pool->SlotFreeCount);
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

PAL_API(int)
PAL_TaskPublish
(
    struct PAL_TASK_POOL *thread_pool, 
    PAL_TASKID          *task_id_list, 
    pal_uint32_t           task_count, 
    PAL_TASKID       *dependency_list, 
    pal_uint32_t     dependency_count
)
{
    PAL_TASK_SCHEDULER *scheduler = thread_pool->TaskScheduler;
    pal_uint32_t             i, j;

    if (dependency_count == 0) {
        /* these tasks are immediately ready-to-run */
        PAL_TaskSchedulerWakeWorkers(scheduler, thread_pool, task_id_list, task_count);
    } else {
        /* allocate a permits list from thread_pool */
        PAL_TASKID       *start_task = task_id_list;
        pal_uint32_t      pool_index = thread_pool->PoolIndex;
        pal_uint32_t    remain_count = task_count;
        pal_uint32_t      wait_count = 0;
        pal_uint32_t      list_count;
        PAL_PERMITS_LIST *list_array[PAL_TASK_STATE_TAG_MAX_PERMITS_LISTS];

        if (task_count <= PAL_TASK_MAX_PERMITS_PER_LIST) {
            /* in the common case, there's only one permits list needed */
            list_count  = 1;
        } else {
            /* many tasks are being published at once - produce multiple lists */
            list_count = (task_count +(PAL_TASK_MAX_PERMITS_PER_LIST-1)) / PAL_TASK_MAX_PERMITS_PER_LIST;
            if (list_count > PAL_TASK_STATE_TAG_MAX_PERMITS_LISTS) {
                /* too many tasks depend on these dependencies */
                return -1;
            }
        }
        if (PAL_TaskPoolAllocatePermitsLists(thread_pool, list_array, list_count) != 0) {
            /* failed to allocate the necessary number of permit lists */
            return -1;
        }

        /* populate the permit lists */
        for (i = 0; i < list_count; ++i) {
            pal_uint32_t    num_task;
            
            if (remain_count >= PAL_TASK_MAX_PERMITS_PER_LIST) {
                /* the task list in this permits list is full */
                num_task = PAL_TASK_MAX_PERMITS_PER_LIST;
            } else {
                /* the task list in this permits list is partial - 'null terminate' */
                num_task = remain_count;
                list_array[i]->TaskList[remain_count] = PAL_TASKID_NONE;
            }

            list_array[i]->WaitCount =-(pal_sint32_t) dependency_count;
            list_array[i]->PoolIndex =  pool_index;
            PAL_CopyMemory(list_array[i]->TaskList, start_task, num_task);

            start_task   += PAL_TASK_MAX_PERMITS_PER_LIST;
            remain_count -= num_task;
        }

        /* publish each permits list to each blocking task */
        for (i = 0; i < dependency_count; ++i) {
            pal_uint32_t   blocking_pidx = PAL_TaskIdGetTaskPoolIndex(dependency_list[i]);
            pal_uint32_t   blocking_sidx = PAL_TaskIdGetTaskSlotIndex(dependency_list[i]);
            PAL_TASK_DATA *blocking_task =&scheduler->TaskPoolList[blocking_pidx]->TaskSlotData[blocking_sidx];
            for (j = 0; j < list_count; ++j) {
                wait_count += PAL_TaskDataPublishPermitsList(blocking_task, list_array[j], dependency_list[i]);
            }
        }
        if (wait_count != dependency_count) {
            /* some, but not all dependencies have completed.
             * race with the blocking tasks to ready these tasks. */
            pal_uint32_t completed_count = dependency_count - wait_count;
            pal_uint32_t    permit_index;
            if (_InterlockedExchangeAdd((volatile LONG*)&list_array[0]->WaitCount, (LONG) completed_count) == 0) {
                /* all dependencies have completed */
                for (i = 0; i < list_count; ++i) {
                    permit_index = PAL_TaskPoolGetSlotIndexForPermitList(thread_pool, list_array[i]);
                    PAL_TaskPoolMakePermitsListFree(thread_pool, permit_index);
                }
                wait_count = 0;
            }
        }

        if (wait_count == 0) {
            /* all dependencies completed */
            PAL_TaskSchedulerWakeWorkers(scheduler, thread_pool, task_id_list, task_count);
        }
    }
    return 0;
}

#if 0
PAL_API(void)
PAL_TaskWait
(
    struct PAL_TASK_CALLBACK_CONTEXT *thread_context, 
    PAL_TASKID                             wait_task
)
{
    if (PAL_TaskIdGetValid(wait_task)) {
        PAL_TASK_SCHEDULER *sched = thread_context->TaskScheduler;
        PAL_TASK_POOL **pool_list = thread_context->TaskScheduler->TaskPoolList;
        pal_uint32_t    wait_pidx = PAL_TaskIdGetTaskPoolIndex(wait_task);
        pal_uint32_t    wait_slot = PAL_TaskIdGetTaskSlotIndex(wait_task);
        pal_uint32_t   generation = PAL_TaskIdGetGeneration(wait_task);
        pal_uint32_t  current_gen;
        pal_uint32_t    state_tag;
        PAL_TASK_DATA  *wait_data;
        PAL_TASK_DATA  *task_data;
        PAL_TASK_ARGS        args;

        /* retrieve the data for the task being waited on */
        wait_data   =&pool_list[wait_pidx]->TaskSlotData[wait_slot];
        state_tag   = wait_data->StateTag;
        _ReadWriteBarrier(); /* load-acquire wait_data->StateTag */
        current_gen = PAL_TaskStateTagGetGeneration(state_tag);

        /* when the wait_task is completed, the generation value in its 
         * PAL_TASK_DATA::StateTag field is updated. therefore, while 
         * the generation values remain the same, execute other work. 
         */
        while (current_gen == generation) {
            /* remember to check sched->ShutdownSignal */
        }
    }
}
#endif

PAL_API(void)
PAL_TaskComplete
(
    struct PAL_TASK_CALLBACK_CONTEXT *thread_context, 
    PAL_TASKID                        completed_task
)
{
    PAL_TASK_POOL     **pool_list = thread_context->TaskScheduler->TaskPoolList;
    pal_uint32_t        task_pidx = PAL_TaskIdGetTaskPoolIndex(completed_task);
    pal_uint32_t        task_slot = PAL_TaskIdGetTaskSlotIndex(completed_task);
    pal_uint32_t       generation = PAL_TaskIdGetGeneration(completed_task);
    PAL_TASK_POOL     *owner_pool = pool_list[task_pidx];
    PAL_TASK_DATA      *task_data =&pool_list[task_pidx]->TaskSlotData[task_slot];
    PAL_TaskPoolCompleteTask(thread_context, owner_pool, task_data, completed_task, task_slot, generation);
}

PAL_API(void)
PAL_TaskMain_NoOp
(
    struct PAL_TASK_ARGS *args
)
{
    PAL_UNUSED_ARG(args);
}

PAL_API(void)
PAL_TaskComplete_NoOp
(
    struct PAL_TASK_ARGS *args
)
{
    PAL_UNUSED_ARG(args);
}

