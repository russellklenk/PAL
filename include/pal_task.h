/**
 * @summary Define the PAL types and API entry points implementing a task system
 * for executing CPU work and I/O on a thread pool.
 */
#ifndef __PAL_TASK_H__
#define __PAL_TASK_H__

#ifndef __PAL_H__
#include "pal.h"
#endif

/* @summary Define various constants related to task identifiers.
 * The task identifier is treated as an opaque 32-bit integer value.
 *
 * A task identifier consists of the following components:
 * 31.|..........|................|.....0
 *   V|PPPPPPPPPP|IIIIIIIIIIIIIIII|GGGGG
 * Starting from the most-significant bit:
 * - V is set if the task identifier is valid, or clear otherwise.
 * - P is the zero-based index of the PAL_TASK_POOL bound to the thread that created the task.
 *     This tells you where the task data lives.
 * - I is the zero-based index of the data slot within the PAL_TASK_POOL.
 * - G is a generation counter to differentiate task data slots that have been reused.
 */
#ifndef PAL_TASKID_CONSTANTS
#   define PAL_TASKID_CONSTANTS
#   define PAL_TASKID_NONE                    0UL
#   define PAL_TASKID_INVALID                 0UL
#   define PAL_TASKID_INDEX_BITS              16
#   define PAL_TASKID_TPOOL_BITS              10
#   define PAL_TASKID_GENER_BITS              5
#   define PAL_TASKID_VALID_BITS              1
#   define PAL_TASKID_GENER_SHIFT             0
#   define PAL_TASKID_INDEX_SHIFT            (PAL_TASKID_GENER_SHIFT + PAL_TASKID_GENER_BITS)
#   define PAL_TASKID_TPOOL_SHIFT            (PAL_TASKID_INDEX_SHIFT + PAL_TASKID_INDEX_BITS)
#   define PAL_TASKID_VALID_SHIFT            (PAL_TASKID_TPOOL_SHIFT + PAL_TASKID_TPOOL_BITS)
#   define PAL_TASKID_GENER_MASK            ((1UL << PAL_TASKID_GENER_BITS) - 1)
#   define PAL_TASKID_INDEX_MASK            ((1UL << PAL_TASKID_INDEX_BITS) - 1)
#   define PAL_TASKID_TPOOL_MASK            ((1UL << PAL_TASKID_TPOOL_BITS) - 1)
#   define PAL_TASKID_VALID_MASK            ((1UL << PAL_TASKID_VALID_BITS) - 1)
#   define PAL_TASKID_GENER_MASK_PACKED      (PAL_TASKID_GENER_MASK << PAL_TASKID_GENER_SHIFT)
#   define PAL_TASKID_INDEX_MASK_PACKED      (PAL_TASKID_INDEX_MASK << PAL_TASKID_INDEX_SHIFT)
#   define PAL_TASKID_TPOOL_MASK_PACKED      (PAL_TASKID_TPOOL_MASK << PAL_TASKID_TPOOL_SHIFT)
#   define PAL_TASKID_VALID_MASK_PACKED      (PAL_TASKID_VALID_MASK << PAL_TASKID_VALID_SHIFT)
#endif

/* @summary Forward-declare the types exported by this module.
 * The type definitions are included in the platform-specific header.
 */
struct  PAL_TASK;
struct  PAL_TASK_ARGS;
struct  PAL_TASK_DATA;
struct  PAL_TASK_POOL;
struct  PAL_TASK_POOL_INIT;
struct  PAL_TASK_POOL_MANAGER;
struct  PAL_TASK_POOL_MANAGER_INIT;
struct  PAL_TASK_WORKER_POOL;
struct  PAL_TASK_WORKER_POOL_INIT;

/* @summary Define a type used to represent a task identifier.
 * Task identifiers are allocated from and associated with a PAL_TASK_POOL bound to a specific OS thread.
 */
typedef pal_uint32_t  PAL_TASKID;

/* @summary Define the signature for the callback function invoked to perform initialization for an task system worker thread.
 * The callback should allocate any per-thread data it needs and return a pointer to that data in the thread_context parameter.
 * @param worker_thread_pool The task worker thread pool that owns the worker thread.
 * @param thread_task_pool The PAL_TASK_POOL allocated to the worker thread.
 * @param worker_pool_context Opaque data supplied when the worker thread pool was created.
 * @param thread_context On return, the function should update this value to point to any data to be associated with the thread.
 * @return Zero if initialization completes successfully, or -1 if initialization failed.
 */
typedef int  (*PAL_TaskWorkerInit_Func)
(
    struct PAL_TASK_WORKER_POOL *worker_thread_pool, 
    struct PAL_TASK_POOL          *thread_task_pool, 
    pal_uintptr_t               worker_pool_context, 
    pal_uintptr_t                   *thread_context
);

/* @summary Define the signature for the entry point of a task.
 * @param args A PAL_TASK_ARGS instance specifying data associated with the task and data used to spawn additional tasks.
 */
typedef void (*PAL_TaskMain_Func)
(
    struct PAL_TASK_ARGS *args
);

/* @summary Define the data supplied to a task during execution.
 * This data can be used to spawn additional root or child tasks.
 */
typedef struct PAL_TASK_ARGS {
    struct PAL_TASK_WORKER_POOL  *WorkerPool;           /* The thread pool used for executing tasks. Used to wait for a task to complete. */
    struct PAL_TASK_POOL         *ExecutionPool;        /* The PAL_TASK_POOL bound to the thread executing the task. Used to spawn new tasks. */
    void                         *TaskArguments;        /* The argument data associated with the specific task that is executing. */
    void                         *ThreadContext;        /* Opaque data associated with the thread that is executing the task (the result stored in the thread_context argument set by PAL_TaskWorkerInit_Func). */
    PAL_TASKID                    TaskId;               /* The unique identifier for the task. */
    pal_uint32_t                  ThreadId;             /* The operating system identifier of the thread executing the task. */
    pal_uint32_t                  ThreadIndex;          /* The zero-based index of the pool bound to the thread that is executing the task. */
    pal_uint32_t                  ThreadCount;          /* The total number of threads that execute and/or define tasks. */
} PAL_TASK_ARGS;

/* @summary Define the data that represents a task. 
 * The TaskId field is populated by the call to PAL_TaskCreate. 
 * The application must set the ParentId, TaskMain and TaskData fields.
 */
typedef struct PAL_TASK {
    PAL_TaskMain_Func             TaskMain;             /* The function to run when the task is executed. */
    PAL_TASKID                    ParentId;             /* The identifier of the parent task, if any. If the task is a root task with no parent, specify PAL_TASKID_NONE. */
    PAL_TASKID                    TaskId;               /* The unique identifier for the task. The application should not modify this field. */
    pal_uint8_t                   TaskData[48];         /* Data specific to the task passed to TaskMain as the PAL_TASK_ARGS::TaskArguments field. This data has 16-byte alignment. */
} PAL_TASK;

/* @summary Define the data used to describe a type of task pool.
 * A task pool maintains all of the storage for tasks, and is bound to a single thread.
 * The bound thread is the only thread that can create tasks using the pool.
 */
typedef struct PAL_TASK_POOL_INIT {
    pal_sint32_t                  PoolTypeId;           /* One of the values of the PAL_TASK_POOL_TYPE_ID enumeration. */
    pal_uint32_t                  PoolCount;            /* The number of pools of this type that are required. */
    pal_uint32_t                  PoolFlags;            /* One or more bitwise OR'd values from the PAL_TASK_POOL_FLAGS enumeration. */
    pal_uint32_t                  PreCommitTasks;       /* The number of tasks that should be pre-committed with backing physical memory. */
} PAL_TASK_POOL_INIT;

/* @summary Define the data used to describe the types of task pools to allocate as part of a task pool storage object.
 * The task pool storage manages the data for a set of application task pools.
 */
typedef struct PAL_TASK_POOL_MANAGER_INIT {
    struct PAL_TASK_POOL_INIT    *TaskPoolTypes;        /* An array of PoolTypeCount PAL_TASK_POOL_INIT structures, one per pool type. */
    pal_uint32_t                  PoolTypeCount;        /* The number of pool types. */
} PAL_TASK_POOL_MANAGER_INIT;

/* @summary Define the data used to create a pool of threads used for executing tasks within the task system.
 * The I/O worker threads are used for executing asynchronous I/O requests and running user I/O completion callbacks.
 * The CPU worker threads are used for executing non-blocking CPU work tasks.
 */
typedef struct PAL_TASK_WORKER_POOL_INIT {
    struct PAL_TASK_POOL_MANAGER *PoolManager;          /* A pointer to the PAL_TASK_POOL_MANAGER object from which the worker threads retrieve their task pools. */
    void                         *PoolContext;          /* Opaque data associated with the worker pool and available to all worker threads. */
    PAL_TaskWorkerInit_Func       ThreadInitFunc;       /* The function used to perform any application-specific setup for each worker thread, such as allocating thread-local storage. */
    pal_uint32_t                  IoWorkerCount;        /* The number of I/O worker threads to create. This value should be at least one. */
    pal_uint32_t                  IoWorkerStackSize;    /* The minimum number of bytes of stack memory to allocate for each I/O worker thread, or PAL_WORKER_STACK_SIZE_DEFAULT. */
    pal_uint32_t                  CpuWorkerCount;       /* The number of CPU worker threads to create. This value is usually set to the number of logical CPUs. */
    pal_uint32_t                  CpuWorkerStackSize;   /* The minimum number of bytes of stack memory to allocate for each CPU worker thread, or PAL_WORKER_STACK_SIZE_DEFAULT. */
    pal_uint32_t                  MaxAsyncIoRequests;   /* The maximum number of asynchronous I/O requests that can be executing at any one time. */
} PAL_TASK_WORKER_POOL_INIT;

/* @summary Define the supported completion types for a task.
 */
typedef enum PAL_TASKID_COMPLETION_TYPE {
    PAL_TASKID_COMPLETION_TYPE_AUTOMATIC =  0,          /* The task is automatically-completed (PAL_TaskComplete is called after the task entry point returns). */
    PAL_TASKID_COMPLETION_TYPE_INTERNAL  =  1,          /* The task is internally-completed (PAL_TaskComplete is called from the task entry point). */
    PAL_TASKID_COMPLETION_TYPE_EXTERNAL  =  2,          /* The task is completed by an external event and explicit call to PAL_TaskComplete. External tasks are never made ready-to-run. */
} PAL_TASKID_COMPLETION_TYPE;

/* @summary Define the recognized identifiers for types of task pools.
 */
typedef enum PAL_TASK_POOL_TYPE_ID {
    PAL_TASK_POOL_TYPE_ID_MAIN           =  0,          /* The task pool type is for use by the main application thread. */
    PAL_TASK_POOL_TYPE_ID_IO_WORKER      =  1,          /* The task pool type is for use by an I/O worker thread within a PAL_TASK_WORKER_POOL. */
    PAL_TASK_POOL_TYPE_ID_CPU_WORKER     =  2,          /* The task pool type is for use by a CPU worker thread within a PAL_TASK_WORKER_POOL. */
    PAL_TASK_POOL_TYPE_ID_USER_WORKER    =  3,          /* The task pool type is used by a thread external to the task system (managed wholly by the application). */
} PAL_TASK_POOL_TYPE_ID;

/* @summary Define various flag values that can be bitwise OR'd to represent the operations that can be performed by a thread that owns a particular type of task pool.
 */
typedef enum PAL_TASK_POOL_FLAGS {
    PAL_TASK_POOL_FLAGS_NONE             = (0UL <<  0), /* The task pool cannot do anything. */
    PAL_TASK_POOL_FLAG_CREATE            = (1UL <<  0), /* The thread that owns the task pool can create new tasks. */
    PAL_TASK_POOL_FLAG_EXECUTE           = (1UL <<  1), /* The thread that owns the task pool can execute tasks. */
    PAL_TASK_POOL_FLAG_COMPLETE          = (1UL <<  2), /* The thread that owns the task pool can complete tasks. */
    PAL_TASK_POOL_FLAG_STEAL             = (1UL <<  3), /* The thread that owns the task pool can steal work from other threads. */
} PAL_TASK_POOL_FLAGS;

#ifdef __cplusplus
extern "C" {
#endif

/* @summary Create a PAL_TASK_POOL_MANAGER from which individual task pools can be acquired.
 * @param init Data used to configure the task pool manager.
 * @return A pointer to the PAL_TASK_POOL_MANAGER, or NULL if an error occurred.
 */
PAL_API(struct PAL_TASK_POOL_MANAGER*)
PAL_TaskPoolManagerCreate
(
    struct PAL_TASK_POOL_MANAGER_INIT *init
);

/* @summary Free resources associated with a PAL_TASK_POOL_MANAGER. All task pools are invalidated.
 * @param manager The PAL_TASK_POOL_MANAGER to delete.
 */
PAL_API(void)
PAL_TaskPoolManagerDelete
(
    struct PAL_TASK_POOL_MANAGER *manager
);

/* @summary Acquire a task pool of a particular type from the task pool manager.
 * This function is internally synchronized. The calling thread may block.
 * @param manager The PAL_TASK_POOL_MANAGER to query.
 * @param pool_type_id One of the values of the PAL_TASK_POOL_TYPE_ID enumeration specifying the type of pool to acquire.
 * @param bind_flags One or more bitwise ORd values from the PAL_TASK_POOL_BIND_FLAGS enumeration.
 * @return A pointer to the PAL_TASK_POOL, or NULL if an error occurred.
 */
PAL_API(struct PAL_TASK_POOL*)
PAL_TaskPoolManagerAcquireTaskPool
(
    struct PAL_TASK_POOL_MANAGER *manager, 
    pal_sint32_t             pool_type_id, 
    pal_uint32_t               bind_flags
);

/* @summary Release a task pool back to the task pool manager.
 * This function is internally synchronized. The calling thread may block.
 * @param manager The PAL_TASK_POOL_MANAGER from which the pool was acquired.
 * @param pool The PAL_TASK_POOL to return to the pool manager.
 */
PAL_API(void)
PAL_TaskPoolManagerReleaseTaskPool
(
    struct PAL_TASK_POOL_MANAGER *manager, 
    struct PAL_TASK_POOL            *pool
);

/* @summary Explicitly bind a task pool to an operating system thread.
 * This function is internally synchronized. The calling thread may block.
 * @param manager The PAL_TASK_POOL_MANAGER from which the pool was acquired.
 * @param pool The PAL_TASK_POOL being associated with an operating system thread.
 * @param os_thread_id The operating system identifier of the thread that will own the task pool.
 * @return Zero if the operation is successful, or non-zero if an error occurred.
 */
PAL_API(int)
PAL_TaskPoolManagerBindPoolToThread
(
    struct PAL_TASK_POOL_MANAGER *manager, 
    struct PAL_TASK_POOL            *pool, 
    pal_uint32_t             os_thread_id
);

/* @summary Create one or more tasks. This step allocates the task IDs. The tasks cannot execute until they are published.
 * For each returned task ID, the application must call PAL_TaskGetData to retrieve the PAL_TASK within the task pool.
 * With the PAL_TASK, the application can set the task parent, entry point routine and any store any argument data.
 * Once the PAL_TASK is initialized, the task ID can be supplied to PAL_TaskPublish to make the task runnable.
 * @param thread_pool The PAL_TASK_POOL bound to the thread calling PAL_TaskCreate.
 * @param task_id_list The array that will store the generated task ID(s).
 * @param task_count The number of tasks to create.
 * @return Zero if all task_count items in task_list are successfully initialized, or non-zero if an error occurred.
 */
PAL_API(int)
PAL_TaskCreate
(
    struct PAL_TASK_POOL *thread_pool, 
    PAL_TASKID          *task_id_list, 
    pal_uint32_t           task_count
);

/* @summary Delete a single task after it has finished executing. 
 * Application code does not have to delete tasks explicitly; they are deleted automatically upon completion.
 * After deletion, the task ID is invalidated and will not be recognized as a valid task.
 * Batch deletion is not supported since a thread can only execute a single task at a time.
 * @param thread_pool The PAL_TASK_POOL bound to the thread calling PAL_TaskDelete.
 * @param task_id The identifier of the task to delete.
 */
PAL_API(void)
PAL_TaskDelete
(
    struct PAL_TASK_POOL *thread_pool, 
    PAL_TASKID                task_id
);

/* @summary Retrieve the PAL_TASK data associated with a task identifier.
 * This allows the application to set the task entry point, parent and any argument data.
 * The calling thread must be the same thread that allocated the task ID using PAL_TaskCreate.
 * @param thread_pool The PAL_TASK_POOL bound to the calling thread. This must be the same thread that created the task ID.
 * @param task_id The identifier of the task to retrieve.
 * @return A pointer to the PAL_TASK representing the user data associated with the task, or NULL.
 */
PAL_API(struct PAL_TASK*)
PAL_TaskGetData
(
    struct PAL_TASK_POOL *thread_pool, 
    PAL_TASKID                task_id
);

/* @summary Publish one or more tasks. If all dependencies are satisfied, the tasks are made ready-to-run.
 * It is possible that a ready-to-run task may start or even finish executing before this call returns.
 * @param thread_pool The PAL_TASK_POOL bound to the calling thread. This must be the same thread that created the task ID(s).
 * @param task_id_list The array of task IDs to publish. Each task must have had its data set.
 * @param task_count The number of IDs specified in task_id_list.
 * @param dependency_list An array of task IDs for tasks that must complete before any of the tasks in task_id_list can run.
 * If the task(s) in task_id_list should be made ready-to-run immediately, specify NULL for dependency_list and 0 for dependency_count.
 * The dependency list applies to all tasks specified in task_id_list.
 * @param dependency_count The number of task IDs specified in dependency_list.
 * @return Zero if all tasks in task_id_list were successfully published, or non-zero if an error occurred.
 */
PAL_API(int)
PAL_TaskPublish
(
    struct PAL_TASK_POOL *thread_pool, 
    PAL_TASKID          *task_id_list, 
    pal_uint32_t           task_count, 
    PAL_TASKID       *dependency_list, 
    pal_uint32_t     dependency_count
);

#ifdef __cplusplus
}; /* extern "C" */
#endif

/* @summary Include the appropriate platform-specific header.
 */
#if   PAL_TARGET_PLATFORM == PAL_PLATFORM_WIN32 || PAL_TARGET_PLATFORM == PAL_PLATFORM_WINRT
    #include "pal_win32_task.h"
#elif PAL_TARGET_PLATFORM == PAL_PLATFORM_LINUX || PAL_TARGET_PLATFORM == PAL_PLATFORM_ANDROID
    #include "pal_linux_task.h"
#elif PAL_TARGET_PLATFORM == PAL_PLATFORM_MACOS || PAL_TARGET_PLATFORM == PAL_PLATFORM_IOS
    #include "pal_apple_task.h"
#else
    #error   pal_task.h: No implementation of the abstraction layer for your platform.
#endif

#endif /* __PAL_TASK_H__ */
