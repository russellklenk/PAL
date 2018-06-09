/**
 * @summary Define the PAL types and API entry points implementing a task system
 * for executing CPU work and I/O on a thread pool.
 */
#ifndef __PAL_TASK_H__
#define __PAL_TASK_H__

#ifndef __PAL_H__
#include "pal.h"
#endif

/* @summary Forward-declare the types exported by this module.
 * The type definitions are included in the platform-specific header.
 */
struct  PAL_TASK_ARGS;
struct  PAL_TASK_DATA;
struct  PAL_TASK_POOL;
struct  PAL_TASK_POOL_INIT;
struct  PAL_TASK_POOL_STORAGE;
struct  PAL_TASK_POOL_STORAGE_INIT;
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

#ifdef __cplusplus
extern "C" {
#endif

#if 0
/* @summary Inspect one or more task pool configurations and perform validation checks against them.
 * @param type_configs An array of type_count PAL_TASK_POOL_INIT structures defining the configuration for each type of task pool.
 * @param type_results An array of type_count integers where the validation results for each PAL_TASK_POOL_INIT will be written.
 * @param type_count The number of elements in the type_configs and type_results arrays.
 * @param global_result On return, any non type-specific validation error is written to this location.
 * @return Zero if the task pool configurations are all valid, or -1 if one or more problems were detected.
 */
PAL_API(int)
PAL_TaskPoolConfigurationValidate
(
    struct PAL_TASK_POOL_INIT *type_configs,
    pal_sint32_t              *type_results, 
    pal_uint32_t                 type_count, 
    pal_sint32_t             *global_result
);

/* @summary Determine the amount of memory required to initialize a task pool storage object with the given configuration.
 * @param type_configs An array of type_count PAL_TASK_POOL_INIT structures defining the configuration for each task pool type.
 * @param type_count The number of elements in the type_configs array.
 * @return The minimum number of bytes required to successfully initialize a task pool storage object with the given configuration.
 */
PAL_API(pal_usize_t)
PAL_TaskPoolStorageQueryMemorySize
(
    struct PAL_TASK_POOL_INIT *type_configs, 
    pal_uint32_t                 type_count
);

/* @summary Initialize a task pool storage blob.
 * @param storage The PAL_TASK_POOL_STORAGE to initialize.
 * @param init Data used to configure the storage pool.
 * @return Zero if the storage object is successfully initialized, or -1 if an error occurred.
 */
PAL_API(int)
PAL_TaskPoolStorageCreate
(
    struct PAL_TASK_POOL_STORAGE   *storage, 
    struct PAL_TASK_POOL_STORAGE_INIT *init
);

/* @summary Free all resources associated with a task pool storage object.
 * @param storage The PAL_TASK_POOL_STORAGE object to delete.
 */
PAL_API(void)
PAL_TaskPoolStorageDelete
(
    struct PAL_TASK_POOL_STORAGE *storage
);

/* @summary Query a task pool storage object for the total number of task pools it manages.
 * @param storage The PAL_TASK_POOL_STORAGE object to query.
 * @return The total number of task pools managed by the storage object.
 */
PAL_API(pal_uint32_t)
PAL_TaskPoolStorageQueryTotalPoolCount
(
    struct PAL_TASK_POOL_STORAGE *storage
);

/* @summary Acquire a task pool and bind it to the calling thread.
 * This function is safe to call from multiple threads simultaneously.
 * This function should not be called from performance-critical code, as it may block.
 * @param storage The PAL_TASK_POOL_STORAGE object from which the pool should be acquired.
 * @param pool_type_id One of PAL_TASK_POOL_ID, or an application-defined value specifying the pool type to acquire.
 * @param prng_seed Initial seed data for the pseudo-random number generator used to select victim tasks. If this value is NULL, random seed data is used.
 * @param prng_seed_size The number of bytes of seed data supplied. This must be either 0, or at least PAL_PRNG_SEED_SIZE.
 * @return A pointer to the task pool object, or NULL if no pool of the specified type could be acquired.
 */
PAL_API(struct PAL_TASK_POOL*)
PAL_TaskPoolStorageAcquireTaskPool
(
    struct PAL_TASK_POOL_STORAGE *storage, 
    pal_uint32_t             pool_type_id, 
    void                       *prng_seed, 
    pal_usize_t            prng_seed_size
);

/* @summary Release a task pool back to the storage object it was allocated from.
 * This function is safe to call from multiple threads simultaneously.
 * This function should not be called from performance-critical code, as it may block.
 * @param pool The task pool object to release.
 */
PAL_API(void)
PAL_TaskPoolStorageReleaseTaskPool
(
    struct PAL_TASK_POOL *pool
);

/* @summary Query a task pool for the maximum number of simultaneously-active tasks.
 * @param pool The PAL_TASK_POOL to query.
 * @return The maximum number of uncompleted tasks that can be defined against the pool.
 */
PAL_API(pal_uint32_t)
PAL_TaskPoolQueryMaxActiveTasks
(
    struct PAL_TASK_POOL *pool
);

/* @summary Retrieve the operating system identifier of the thread that owns a task pool.
 * @param pool The PAL_TASK_POOL to query.
 * @return The operating system identifier of the thread that owns the task pool.
 */
PAL_API(pal_uint32_t)
PAL_TaskPoolQueryBoundThreadId
(
    struct PAL_TASK_POOL *pool
);

/* @summary Retrieve a value indicating the pool type for a given PAL_TASK_POOL.
 * @param pool The PAL_TASK_POOL to query.
 * @return One of the values of the PAL_TASK_POOL_ID enumeration, or a user-defined value, indicating the pool type.
 */
PAL_API(pal_uint32_t)
PAL_TaskPoolQueryPoolType
(
    struct PAL_TASK_POOL *pool
);

/* @summary Retrieve a zero-based index of a task pool within the pool storage. 
 * This value remains constant for the lifetime of the PAL_TASK_POOL_STORAGE.
 * This value can be used to uniquely identify the pool bound to a thread.
 * @param pool The PAL_TASK_POOL to query.
 * @return The zero-based index of that task pool within its associated PAL_TASK_POOL_STORAGE.
 */
PAL_API(pal_uint32_t)
PAL_TaskPoolQueryPoolIndex
(
    struct PAL_TASK_POOL *pool
);

/* @summary Retrieve the total number of task pools in the PAL_TASK_POOL_STORAGE from which a pool was allocated.
 * This value remains constant for the lifetime of the PAL_TASK_POOL_STORAGE.
 * @param pool The PAL_TASK_POOL to query.
 * @return The total number of task pools defined for the PAL_TASK_POOL_STORAGE.
 */
PAL_API(pal_uint32_t)
PAL_TaskPoolQueryPoolCount
(
    struct PAL_TASK_POOL *pool
);

/* @summary Wait until a task pool indicates that it has tasks available to steal.
 * If external threads have indicated they have tasks available to steal, the calling thread does not block.
 * If no tasks are available to steal, the calling thread blocks until a thread indicates work availability.
 * By the time the function returns, the steal notification may or may not still be valid.
 * @param pool The PAL_TASK_POOL allocated to the calling thread.
 * @return A pointer to the task pool owned by the thread that may have tasks available to steal.
 * The returned value may be the same as the pool owned by the calling thread, in which case the function should be called again.
 */
PAL_API(struct PAL_TASK_POOL*)
PAL_TaskPoolWaitToStealTasks
(
    struct PAL_TASK_POOL *pool
);

/* @summary Publish a notification that a task pool has one or more tasks available to steal.
 * @param pool The PAL_TASK_POOL allocated to the calling thread, which currently has at least one task available to steal.
 */
PAL_API(void)
PAL_TaskPoolNotifyTasksToSteal
(
    struct PAL_TASK_POOL *pool
);

/* @summary Initialize a PAL_TASK_INIT structure for an internally-completed root task.
 * @param init The PAL_TASK_INIT to initialize.
 * @param task_main The task entry point.
 * @param task_args Optional argument data to associate with the task, or NULL. 
 * @param args_size The size of the argument data, in bytes. The maximum size is PAL_MAX_TASK_DATA_BYTES.
 * @param task_deps An optional list of task IDs for tasks that must complete before this new task can run.
 * @param deps_count The number of task IDs specified in the dependency list.
 * @return Zero if the task data is valid, or -1 if an error occurred.
 */
PAL_API(int)
PAL_TaskInitInternallyCompleted
(
    struct PAL_TASK_INIT   *init, 
    PAL_TaskMain_Func  task_main, 
    void              *task_args, 
    pal_usize_t        args_size, 
    PAL_TASK_ID       *task_deps, 
    pal_usize_t       deps_count
);

/* @summary Initialize a PAL_TASK_INIT structure for an internally-completed child task.
 * @param init The PAL_TASK_INIT to initialize.
 * @param parent_id The identifier of the parent task.
 * @param task_main The task entry point.
 * @param task_args Optional argument data to associate with the task, or NULL. 
 * @param args_size The size of the argument data, in bytes. The maximum size is PAL_MAX_TASK_DATA_BYTES.
 * @param task_deps An optional list of task IDs for tasks that must complete before this new task can run.
 * @param deps_count The number of task IDs specified in the dependency list.
 * @return Zero if the task data is valid, or -1 if an error occurred.
 */
PAL_API(int)
PAL_TaskInitInternallyCompletedChild
(
    struct PAL_TASK_INIT  *init, 
    PAL_TASK_ID       parent_id,
    PAL_TaskMain_Func task_main, 
    void             *task_args, 
    pal_usize_t       args_size, 
    PAL_TASK_ID      *task_deps, 
    pal_usize_t      deps_count
);

/* @summary Initialize a PAL_TASK_INIT structure for an externally-completed root task.
 * @param init The PAL_TASK_INIT to initialize.
 * @param task_main The task entry point.
 * @param task_args Optional argument data to associate with the task, or NULL. 
 * @param args_size The size of the argument data, in bytes. The maximum size is PAL_MAX_TASK_DATA_BYTES.
 * @return Zero if the task data is valid, or -1 if an error occurred.
 */
PAL_API(int)
PAL_TaskInitExternallyCompleted
(
    struct PAL_TASK_INIT   *init, 
    PAL_TaskMain_Func  task_main, 
    void              *task_args, 
    pal_usize_t        args_size
);

/* @summary Initialize a PAL_TASK_INIT structure for an externally-completed child task.
 * @param init The PAL_TASK_INIT to initialize.
 * @param parent_id The identifier of the parent task.
 * @param task_main The task entry point.
 * @param task_args Optional argument data to associate with the task, or NULL. 
 * @param args_size The size of the argument data, in bytes. The maximum size is PAL_MAX_TASK_DATA_BYTES.
 * @return Zero if the task data is valid, or -1 if an error occurred.
 */
PAL_API(int)
PAL_TaskInitExternallyCompletedChild
(
    struct PAL_TASK_INIT  *init, 
    PAL_TASK_ID       parent_id,
    PAL_TaskMain_Func task_main, 
    void             *task_args, 
    pal_usize_t       args_size
);

/* @summary Define a new task. 
 * The task may start executing immediately, but cannot complete until PAL_TaskLaunch is called with the returned task ID.
 * The calling thread will block if the specified task pool does not have any available task slots.
 * @param pool The PAL_TASK_POOL owned by the calling thread. The task data is allocated from this pool.
 * @param init Information about the task to create.
 * @return The identifier of the new task, or PAL_INVALID_TASK_ID if the task could not be defined.
 */
PAL_API(PAL_TASK_ID)
PAL_TaskDefine
(
    struct PAL_TASK_POOL *pool, 
    struct PAL_TASK_INIT *init
);

/* @summary Launch a task, indicating that it has been fully-defined, and allow the task to complete.
 * @param pool The PAL_TASK_POOL owned by the calling thread. This should be the same task pool passed to PAL_TaskDefine.
 * @param task_id The identifier of the task to launch, returned by a prior call to PAL_TaskDefine.
 * @return The number of tasks made ready-to-run, or -1 if an error occurred. This value may be zero if the task dependencies have not been satisfied.
 */
PAL_API(int)
PAL_TaskLaunch
(
    struct PAL_TASK_POOL *pool, 
    PAL_TASK_ID        task_id
);

/* @summary Indicate completion of a task.
 * @param pool The PAL_TASK_POOL owned by the thread that completed the task. This may be different than the task pool for the thread that defined the task.
 * @param task_id The identifier of the completed task.
 * @return The number of tasks made ready-to-run by the completion of this task, or -1 if an error occurred.
 */
PAL_API(int)
PAL_TaskComplete
(
    struct PAL_TASK_POOL *pool, 
    PAL_TASK_ID        task_id
);

/* @summary Wait for a given task to complete. While waiting, the calling thread executes available tasks.
 * @param worker_pool The PAL_TASK_WORKER_POOL used to execute tasks.
 * @param thread_pool The PAL_TASK_POOL owned by the calling thread.
 * @param wait_id The identifier of the task to wait for.
 * @param context Opaque data to pass through to each task as it executes.
 */
PAL_API(void)
PAL_TaskWait
(
    struct PAL_TASK_WORKER_POOL *worker_pool, 
    struct PAL_TASK_POOL        *thread_pool, 
    PAL_TASK_ID                      wait_id, 
    void                            *context
);

/* @summary Immediately execute a task on the calling thread, and then execute available tasks until it completes.
 * This is useful for I/O tasks, for example, during the main loop of a parser.
 * @param worker_pool The PAL_TASK_WORKER_POOL used to execute tasks.
 * @param thread_pool The PAL_TASK_POOL owned by the calling thread.
 * @param task_id The identifier of the task to execute and then wait for. This must be an externally-completed task.
 * @param context Opaque data to pass through to each task as it executes.
 */
PAL_API(void)
PAL_TaskExecuteExternalAndWait
(
    struct PAL_TASK_WORKER_POOL *worker_pool, 
    struct PAL_TASK_POOL        *thread_pool, 
    PAL_TASK_ID                      task_id, 
    void                            *context
);

/* @summary Determine the amount of memory required to initialize a task worker thread pool with the given configuration.
 * @param worker_count_compute The number of compute worker threads in the pool.
 * @param worker_count_io The number of I/O worker threads in the pool.
 * @param max_async_io_requests The maximum number of concurrent active asynchronous I/O requests.
 * @return The minimum number of bytes required to successfully initialize a task worker thread pool with the given configuration.
 */
PAL_API(pal_usize_t)
PAL_TaskWorkerPoolQueryMemorySize
(
    pal_uint32_t worker_count_compute, 
    pal_uint32_t worker_count_io, 
    pal_uint32_t max_async_io_requests
);

/* @summary Retrieve the application-defined data associated with a task worker thread pool.
 * @param pool The task worker thread pool to query.
 * @return The application-defined data associated with the task worker thread pool.
 */
PAL_API(pal_uintptr_t)
PAL_TaskWorkerPoolQueryUserContext
(
    struct PAL_TASK_WORKER_POOL *pool
);

/* @summary Initialize and launch a pool of worker threads to execute tasks.
 * @param pool On return, this location is updated with a pointer to the thread pool object.
 * @param init Data used to configure the pool of worker threads.
 * @return Zero if the thread pool is successfully initialized, or -1 if an error occurred.
 */
PAL_API(int)
PAL_TaskWorkerPoolLaunch
(
    struct PAL_TASK_WORKER_POOL      *pool, 
    struct PAL_TASK_WORKER_POOL_INIT *init
);

/* @summary Stop all threads and free all resources associated with a pool of task worker threads.
 * @param pool The PAL_TASK_WORKER_POOL to terminate and delete.
 */
PAL_API(void)
PAL_TaskWorkerPoolTerminate
(
    struct PAL_TASK_WORKER_POOL *pool
);
#endif /* DISABLED */

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
