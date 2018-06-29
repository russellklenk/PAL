/**
 * @summary Define the PAL types and API entry points for interfacing with the 
 * platform threading APIs. Additionally, define some useful lock-free types
 * for communicating between threads and querying the host CPU layout.
 */
#ifndef __PAL_THREAD_H__
#define __PAL_THREAD_H__

#ifndef __PAL_H__
#include "pal.h"
#endif

/* @summary Forward-declare the types exported by this module.
 * The type definitions are included in the platform-specific header.
 */
struct  PAL_CPU_INFO;
struct  PAL_SEMAPHORE;
struct  PAL_MUTEX;
struct  PAL_RWLOCK;
struct  PAL_MONITOR;
struct  PAL_BARRIER;
struct  PAL_EVENTCOUNT;
struct  PAL_SPSC_QUEUE_U32;
struct  PAL_SPSC_QUEUE_INIT;
struct  PAL_SPMC_QUEUE_U32;
struct  PAL_SPMC_QUEUE_INIT;
struct  PAL_MPMC_QUEUE_U32;
struct  PAL_MPMC_QUEUE_INIT;
struct  PAL_THREAD_INIT;
struct  PAL_THREAD_FUNC;
struct  PAL_THREAD_POOL;
struct  PAL_THREAD_POOL_INIT;

/* @summary Define the signature for the callback implementing the thread initialization routine.
 * Thread initialization within a thread pool is serialized, such that thread 1's initialization routinue will complete before thread 2's initialization routine begins to run.
 * All thread initialization routines must complete successfully before any thread entry point will run. 
 * @param init Information about the thread. The initialization routine should set PAL_THREAD_INIT::ThreadContext to point to any data associated with the thread.
 * @return Zero if the thread initialization routine completes successfully, or non-zero if an error occurs.
 */
typedef pal_uint32_t (*PAL_ThreadInit_Func)
(
    struct PAL_THREAD_INIT *init
);

/* @summary Define the signature for the callback implementing the thread body.
 * The thread entry point runs after the thread initialization routine has run for all threads in the pool.
 * All threads within the pool will begin running at approximately the same time (given hardware limits).
 * @param init Information about the thread, including information returned by the initialization routine.
 * @return The thread exit code. This value is typically zero to indicate a normal exit.
 */
typedef pal_uint32_t (*PAL_ThreadMain_Func)
(
    struct PAL_THREAD_INIT *init
);

/* @summary Define the data used to configure a single-producer, single-consumer bounded concurrent queue.
 */
typedef struct PAL_SPSC_QUEUE_INIT {
    pal_uint32_t                       Capacity;               /* The queue capacity, in items. This value must be a power-of-two greater than zero. */
    pal_uint32_t                       UsageFlags;             /* Currently unused. Set to 0. */
    void                              *MemoryStart;            /* Pointer to the start of the memory block allocated for the storage array. */
    pal_uint64_t                       MemorySize;             /* The size of the memory block allocated for the storage array, in bytes. */
} PAL_SPSC_QUEUE_INIT;

/* @summary Define the data used to configure a single-producer, multiple-consumer bounded concurrent queue.
 */
typedef struct PAL_SPMC_QUEUE_INIT {
    pal_uint32_t                       Capacity;               /* The queue capacity, in items. This value must be a power-of-two greater than zero. */
    pal_uint32_t                       UsageFlags;             /* Currently unused. Set to 0. */
    void                              *MemoryStart;            /* Pointer to the start of the memory block allocated for the storage array. */
    pal_uint64_t                       MemorySize;             /* The size of the memory block allocated for the storage array, in bytes. */
} PAL_SPMC_QUEUE_INIT;

/* @summary Define the data used to configure a multiple-producer, multiple-consumer bounded concurrent queue.
 */
typedef struct PAL_MPMC_QUEUE_INIT {
    pal_uint32_t                       Capacity;               /* The queue capacity, in items. This value must be a power-of-two greater than zero. */
    pal_uint32_t                       UsageFlags;             /* Currently unused. Set to 0. */
    void                              *MemoryStart;            /* Pointer to the start of the memory block allocated for the storage array. */
    pal_uint64_t                       MemorySize;             /* The size of the memory block allocated for the storage array, in bytes. */
} PAL_MPMC_QUEUE_INIT;

/* @summary Define the data associated with thread initialization.
 * When this structure is passed into PAL_ThreadInit_Func, the ThreadContext field is NULL.
 * The thread initialization routine should set the ThreadContext field to point to data to pass through the thread entry point.
 * When this structure is passed into PAL_ThreadMain_Func, the ThreadContext field has the value set by the thread initialization routine.
 */
typedef struct PAL_THREAD_INIT {
    struct PAL_THREAD_POOL            *ThreadPool;             /* The thread pool that owns the thread. */
    void                              *PoolContext;            /* Opaque data supplied by the application in the PAL_THREAD_POOL_INIT::PoolContext field. */
    void                              *ThreadContext;          /* Additional data to be associated with each individual thread in the pool. */
    pal_uint32_t                       ThreadIndex;            /* The zero-based index of the thread within the pool. This value uniquely identifies the thread. */
    pal_uint32_t                       ThreadCount;            /* The total number of threads in the pool. */
} PAL_THREAD_INIT;

/* @summary Define the callbacks associated with each thread in a thread pool.
 */
typedef struct PAL_THREAD_FUNC {
    PAL_ThreadInit_Func                Init;                   /* The thread initialization routinue. */
    PAL_ThreadMain_Func                Main;                   /* The thread entry point routinue. */
} PAL_THREAD_FUNC;

/* @summary Define the data used to create a thread pool.
 * Thread pools are primarily useful for testing. Generally, if the application needs to execute work across threads, it should use the task system instead.
 */
typedef struct PAL_THREAD_POOL_INIT {
    void                              *PoolContext;            /* Opaque data to be supplied to each thread in the pool during initialization and execution. */
    struct PAL_THREAD_FUNC            *ThreadFuncs;            /* An array of ThreadCount PAL_THREAD_FUNC structures specifying the initialization and entry point routines for each thread. */
    pal_uint32_t                       ThreadCount;            /* The number of threads in the thread pool. */
} PAL_THREAD_POOL_INIT;

/* @summary Define various flags that can be bitwise OR'd to control the behavior of an SPSC concurrent queue.
 */
typedef enum PAL_SPSC_QUEUE_USAGE_FLAGS {
    PAL_SPSC_QUEUE_USAGE_FLAGS_NONE            = (0UL <<  0),  /* No special usage flags are specified. */
} PAL_SPSC_QUEUE_USAGE_FLAGS;

/* @summary Define various flags that can be bitwise OR'd to control the behavior of an SPMC concurrent queue.
 */
typedef enum PAL_SPMC_QUEUE_USAGE_FLAGS {
    PAL_SPMC_QUEUE_USAGE_FLAGS_NONE            = (0UL <<  0),  /* No special usage flags are specified. */
} PAL_SPMC_QUEUE_USAGE_FLAGS;

/* @summary Define various flags that can be bitwise OR'd to control the behavior of an MPMC concurrent queue.
 */
typedef enum PAL_MPMC_QUEUE_USAGE_FLAGS {
    PAL_MPMC_QUEUE_USAGE_FLAGS_NONE            = (0UL <<  0),  /* No special usage flags are specified. */
} PAL_MPMC_QUEUE_USAGE_FLAGS;

#ifdef __cplusplus
extern "C" {
#endif

/* @summary Retrieve the operating system identifier of the calling thread.
 * @return The operating system identifier of the calling thread.
 */
PAL_API(pal_uint32_t)
PAL_ThreadGetId
(
    void
);

/* @summary Assign a string identifier to the calling thread.
 * This function is useful for diagnostics only and may not have any effect on some platforms.
 * @param name A nul-terminated string literal specifying the thread name.
 */
PAL_API(void)
PAL_ThreadSetName
(
    char const *name
);

/* @summary Put the calling thread to sleep for some length of time.
 * @param duration_ns The minimum amount of time the thread should sleep, in nanoseconds.
 */
PAL_API(void)
PAL_ThreadSleep
(
    pal_uint64_t duration_ns
);

/* @summary Cause the calling thread to yield to a ready-to-run thread determined by the operating system.
 * This operation may have no effect on some processors, or if there is no ready-to-run thread.
 */
PAL_API(void)
PAL_ThreadYield
(
    void
);

/* @summary Query the basic attributes of the host CPU(s).
 * This function allocates a small amount of scratch memory and frees it prior to returning.
 * @param info The structure to update with information about the host CPU layout and attributes.
 * @return Zero if the host CPU information was read and returned, or non-zero if an error occurred.
 */
PAL_API(int)
PAL_CpuInfoQuery
(
    struct PAL_CPU_INFO *info
);

/* @summary Create a new semaphore object initialized with the specified number of available resources.
 * @param sem The PAL_SEMAPHORE object to initialize.
 * @param value The number of resources initially available.
 * @return Zero if the semaphore is successfully created, or non-zero if an error occurred.
 */
PAL_API(int)
PAL_SemaphoreCreate
(
    struct PAL_SEMAPHORE *sem, 
    pal_sint32_t        value
);

/* @summary Free the resources associated with a semaphore object.
 * @param sem The PAL_SEMAPHORE object to delete.
 */
PAL_API(void)
PAL_SemaphoreDelete
(
    struct PAL_SEMAPHORE *sem
);

/* @summary Set the spin count associated with a semaphore object. The spin count controls the number of times the semaphore will spin in userspace trying to acquire a resource before putting the thread to sleep.
 * @param sem The PAL_SEMAPHORE object to update.
 * @param spin_count The desired spin count, which must be greater than or equal to zero. You may use PAL_DEFAULT_SPIN_COUNT if you don't know.
 * @return The previously set spin count.
 */
PAL_API(pal_uint32_t)
PAL_SemaphoreSetSpinCount
(
    struct PAL_SEMAPHORE *sem, 
    pal_uint32_t   spin_count
);

/* @summary Claim a single resource from a semaphore. If the semaphore's internal count is zero or negative, the calling thread is blocked until a resource becomes available.
 * @param sem The PAL_SEMAPHORE object to wait on.
 */
PAL_API(void)
PAL_SemaphoreWait
(
    struct PAL_SEMAPHORE *sem
);

/* @summary Make available a single resource on a semaphore. If the semaphore's count is zero or negative, a single waiting thread is woken.
 * @param sem The PAL_SEMAPHORE object to post the resource to.
 */
PAL_API(void)
PAL_SemaphorePostOne
(
    struct PAL_SEMAPHORE *sem
);

/* @summary Make available N resources on a semaphore. If the semaphore's count is zero or negative, one or more waiting threads are woken.
 * @param sem The PAL_SEMAPHORE object to post the resources to.
 * @param post_count The number of resources made available.
 */
PAL_API(void)
PAL_SemaphorePostMany
(
    struct PAL_SEMAPHORE *sem, 
    pal_sint32_t   post_count
);

/* @summary Create a mutex object, which provides access to a protected resource for one thread at a time.
 * @param mtx The PAL_MUTEX object to initialize.
 * @return Zero if the mutex is successfully created, or non-zero if an error occurred.
 */
PAL_API(int)
PAL_MutexCreate
(
    struct PAL_MUTEX *mtx
);

/* @summary Free the resources associated with a mutex object.
 * @param mtx The PAL_MUTEX object to delete.
 */
PAL_API(void)
PAL_MutexDelete
(
    struct PAL_MUTEX *mtx
);

/* @summary Set the spin count associated with a mutex object. The spin count controls the number of times the mutex will spin in userspace trying to acquire access to the protected resource before putting the thread to sleep.
 * @param mtx The PAL_MUTEX object to update.
 * @param spin_count The desired spin count, which must be greater than or equal to zero. You may use PAL_DEFAULT_SPIN_COUNT if you don't know.
 * @return The previously set spin count.
 */
PAL_API(pal_uint32_t)
PAL_MutexSetSpinCount
(
    struct PAL_MUTEX   *mtx, 
    pal_uint32_t spin_count
);

/* @summary Attempt to acquire access to the protected resource. If the resource is not available, the calling thread may be put into a wait state.
 * @param mtx The PAL_MUTEX guarding the protected resource.
 */
PAL_API(void)
PAL_MutexAcquire
(
    struct PAL_MUTEX *mtx
);

/* @summary Relinquish access to the protected resource. This may wake one waiting thread.
 * @param mtx The PAL_MUTEX guarding the protected resource.
 */
PAL_API(void)
PAL_MutexRelease
(
    struct PAL_MUTEX *mtx
);

/* @summary Create a reader-writer lock object, which allows a single writer OR multiple concurrent readers.
 * @param rwl The PAL_RWLOCK object to initialize.
 * @return Zero if the reader-writer lock is successfully initialized, or non-zero if an error occurs.
 */
PAL_API(int)
PAL_RWLockCreate
(
    struct PAL_RWLOCK *rwl
);

/* @summary Free the resources associated with a reader-writer lock object
 * @param rwl The PAL_RWLOCK to delete.
 */
PAL_API(void)
PAL_RWLockDelete
(
    struct PAL_RWLOCK *rwl
);

/* @summary Acquire read-write access (exclusive) to the protected resource. The calling thread may block until the resource becomes available.
 * @param rwl The PAL_RWLOCK to acquire for exclusive access.
 */
PAL_API(void)
PAL_RWLockAcquireWriter
(
    struct PAL_RWLOCK *rwl
);

/* @summary Relinquish access to the protected resource. This may wake one or more waiting threads.
 * @param rwl The PAL_RWLOCK guarding the protected resource.
 */
PAL_API(void)
PAL_RWLockReleaseWriter
(
    struct PAL_RWLOCK *rwl
);

/* @summary Acquire read-only (shared) access to the protected resource. The calling thread may block until any concurrent writes complete.
 * @param rwl The PAL_RWLOCK to acquire for shared access.
 */
PAL_API(void)
PAL_RWLockAcquireReader
(
    struct PAL_RWLOCK *rwl
);

/* @summary Relinquish access to the protected resource. This may wake one or more waiting threads.
 * @param rwl The PAL_RWLOCK guarding the protected resource.
 */
PAL_API(void)
PAL_RWLockReleaseReader
(
    struct PAL_RWLOCK *rwl
);

/* @summary Create a monitor synchronization object.
 * @param mon The PAL_MONITOR object to initialize.
 * @return Zero if the object is successfully initialized, or non-zero if an error occurs.
 */
PAL_API(int)
PAL_MonitorCreate
(
    struct PAL_MONITOR *mon
);

/* @summary Free resources associated with a monitor synchronization object.
 * @param mon The PAL_MONITOR object to delete.
 */
PAL_API(void)
PAL_MonitorDelete
(
    struct PAL_MONITOR *mon
);

/* @summary Acquire exclusive access to the protected resource. The calling thread may block until the resource becomes available.
 * @param mon The PAL_MONITOR object guarding the protected resource.
 */
PAL_API(void)
PAL_MonitorAcquire
(
    struct PAL_MONITOR *mon
);

/* @summary Release exclusive access to the protected resource. This may wake one or more waiting threads.
 * @param mon The PAL_MONITOR object guarding the protected resource.
 */
PAL_API(void)
PAL_MonitorRelease
(
    struct PAL_MONITOR *mon
);

/* @summary Release exclusive access to the protected resource and immediately begin waiting, as an atomic operation.
 * @param mon The PAL_MONITOR object guarding the protected resource.
 */
PAL_API(void)
PAL_MonitorReleaseAndWait
(
    struct PAL_MONITOR *mon
);

/* @summary Signal a single waiting thread to check the condition.
 * @param mon The PAL_MONITOR object guarding the protected resource.
 */
PAL_API(void)
PAL_MonitorSignal
(
    struct PAL_MONITOR *mon
);

/* @summary Signal all waiting threads to check the condition.
 * @param mon The PAL_MONITOR object guarding the protected resource.
 */
PAL_API(void)
PAL_MonitorBroadcast
(
    struct PAL_MONITOR *mon
);

/* @summary Create a new barrier to synchronize a given number of threads.
 * @param barrier The PAL_BARRIER to initialize.
 * @param thread_count The number of threads that will participate in the synchronization operation.
 * @return Zero if the barrier is successfully initialized, or non-zero if an error occurred.
 */
PAL_API(int)
PAL_BarrierCreate
(
    struct PAL_BARRIER *barrier, 
    pal_sint32_t   thread_count
);

/* @summary Free the resources associated with a barrier.
 * @param barrier The PAL_BARRIER to destroy.
 */
PAL_API(void)
PAL_BarrierDelete
(
    struct PAL_BARRIER *barrier
);

/* @summary Set the spin count associated with a barrier object. The spin count controls the number of times the mutex will spin in userspace trying to acquire access to the protected resource before putting the thread to sleep.
 * @param barrier The PAL_BARRIER object to update.
 * @param spin_count The desired spin count, which must be greater than or equal to zero. You may use PAL_DEFAULT_SPIN_COUNT if you don't know.
 * @return The previously set spin count.
 */
PAL_API(pal_uint32_t)
PAL_BarrierSetSpinCount
(
    struct PAL_BARRIER *barrier, 
    pal_uint32_t     spin_count
);

/* @summary Indicate that a thread wishes to enter the barrier.
 * @param barrier The PAL_BARRIER used to synchronize the thread group.
 */
PAL_API(void)
PAL_BarrierEnter
(
    struct PAL_BARRIER *barrier
);

/* @summary Indicate that a thread wishes to leave the barrier.
 * @param barrier The PAL_BARRIER used to synchronize the thread group.
 */
PAL_API(void)
PAL_BarrierLeave
(
    struct PAL_BARRIER *barrier
);

/* @summary Create an eventcount synchronization object.
 * @param ec The PAL_EVENTCOUNT object to initialize.
 * @return Zero if the eventcount is successfully initialized, or non-zero if an error occurred.
 */
PAL_API(int)
PAL_EventCountCreate
(
    struct PAL_EVENTCOUNT *ec
);

/* @summary Free resources associated with an eventcount synchronization object.
 * @param ec The PAL_EVENTCOUNT object to delete.
 */
PAL_API(void)
PAL_EventCountDelete
(
    struct PAL_EVENTCOUNT *ec
);

/* @summary Retrieve a token representing the eventcount state at the current point in time, in preparation for calling PAL_WaitEventCount.
 * @param ec The PAL_EVENTCOUNT object to update.
 * @return A token to pass to PAL_WaitEventCount.
 */
PAL_API(pal_sint32_t)
PAL_EventCountPrepareWait
(
    struct PAL_EVENTCOUNT *ec
);

/* @summary Potentially put the calling thread to sleep while waiting for a resource. If a resource is available, the wait returns immediately.
 * This compares the provided token to the eventcount Counter field; if they match, the calling thread is put to sleep until the eventcount is signaled.
 * @param ec The PAL_EVENTCOUNT to wait on.
 * @param token A token value returned from a prior call to PAL_EventCountPrepareWait.
 */
PAL_API(void)
PAL_EventCountPerformWait
(
    struct PAL_EVENTCOUNT *ec, 
    pal_sint32_t        token
);

/* @summary Wakes all threads waiting on the eventcount and increment the internal counter. If no waiters are registered, this call is a no-op.
 * @param ec The PAL_EVENTCOUNT to signal.
 */
PAL_API(void)
PAL_EventCountSignal
(
    struct PAL_EVENTCOUNT *ec
);

/* @summary Calculate the amount of memory required for an SPSC concurrent queue of a given capacity.
 * @param capacity The capacity of the queue. This value must be a power-of-two greater than 2.
 * @return The minimum number of bytes required to successfully initialize a queue with the specified capacity.
 */
PAL_API(pal_usize_t)
PAL_SPSCQueueQueryMemorySize_u32
(
    pal_uint32_t capacity
);

/* @summary Initialize a single-producer, single-consumer concurrent queue of 32-bit integer values.
 * @param spsc_queue The SPSC queue object to initialize.
 * @param init Configuration data for the queue.
 * @return Zero if the queue is successfully initialized, or non-zero if an error occurred.
 */
PAL_API(int)
PAL_SPSCQueueCreate_u32
(
    struct PAL_SPSC_QUEUE_U32 *spsc_queue, 
    struct PAL_SPSC_QUEUE_INIT      *init
);

/* @summary Attempt to push a 32-bit unsigned integer value onto the back of the queue.
 * This function can be called by the producer thread only.
 * @param spsc_queue The SPSC queue object to receive the value.
 * @param item The value being enqueued.
 * @return Non-zero if the value was enqueued, or zero if the queue was full.
 */
PAL_API(int)
PAL_SPSCQueuePush_u32
(
    struct PAL_SPSC_QUEUE_U32 *spsc_queue, 
    pal_uint32_t                     item
);

/* @summary Attempt to take a 32-bit unsigned integer value from the front of the queue.
 * This function can be called by the consumer thread only.
 * @param spsc_queue The SPSC queue object from which the oldest value should be retrieved.
 * @param item If the function returns non-zero, on return this location stores the dequeued value.
 * @return Non-zero if a value was dequeued, or zero if the queue was empty.
 */
PAL_API(int)
PAL_SPSCQueueTake_u32
(
    struct PAL_SPSC_QUEUE_U32 *spsc_queue, 
    pal_uint32_t                    *item
);

/* @summary Calculate the amount of memory required for an SPMC concurrent queue of a given capacity.
 * @param capacity The capacity of the queue. This value must be a power-of-two greater than 2.
 * @return The minimum number of bytes required to successfully initialize a queue with the specified capacity.
 */
PAL_API(pal_usize_t)
PAL_SPMCQueueQueryMemorySize_u32
(
    pal_uint32_t capacity
);

/* @summary Initialize a single-producer, multiple-consumer concurrent queue of 32-bit integer values.
 * @param spmc_queue The SPMC queue object to initialize.
 * @param init Configuration data for the queue.
 * @return Zero if the queue is successfully initialized, or non-zero if an error occurred.
 */
PAL_API(int)
PAL_SPMCQueueCreate_u32
(
    struct PAL_SPMC_QUEUE_U32 *spmc_queue, 
    struct PAL_SPMC_QUEUE_INIT      *init
);

/* @summary Attempt to push a 32-bit unsigned integer value onto the back of the private end of the queue.
 * This function can be called by the producer thread only.
 * @param mpmc_queue The MPMC queue object to receive the value.
 * @param item The value being enqueued.
 * @return Non-zero if the value was enqueued, or zero if the queue was full.
 */
PAL_API(int)
PAL_SPMCQueuePush_u32
(
    struct PAL_SPMC_QUEUE_U32 *spmc_queue, 
    pal_uint32_t                     item
);

/* @summary Attempt to take a 32-bit unsigned integer value from the back of the private end of the queue.
 * This function can be called by the producer thread only.
 * @param spmc_queue The SPMC queue object from which the most recent value should be retrieved.
 * @param item If the function returns non-zero, on return this location stores the dequeued value.
 * @return Non-zero if a value was dequeued, or zero if the queue was empty.
 */
PAL_API(int)
PAL_SPMCQueueTake_u32
(
    struct PAL_SPMC_QUEUE_U32 *spmc_queue, 
    pal_uint32_t                    *item
);

/* @summary Attempt to take a 32-bit unsigned integer value from the front of the public end of the queue.
 * This function can be called by any consumer thread.
 * @param spmc_queue The SPMC queue object from which the most recent value should be retrieved.
 * @param item If the function returns non-zero, on return this location stores the dequeued value.
 * @return Non-zero if a value was dequeued, or zero if the queue was empty.
 */
PAL_API(int)
PAL_SPMCQueueSteal_u32
(
    struct PAL_SPMC_QUEUE_U32 *spmc_queue, 
    pal_uint32_t                    *item
);

/* @summary Calculate the amount of memory required for an MPMC concurrent queue of a given capacity.
 * @param capacity The capacity of the queue. This value must be a power-of-two greater than 2.
 * @return The minimum number of bytes required to successfully initialize a queue with the specified capacity.
 */
PAL_API(pal_usize_t)
PAL_MPMCQueueQueryMemorySize_u32
(
    pal_uint32_t capacity
);

/* @summary Initialize a multiple-producer, multiple-consumer concurrent queue of 32-bit integer values.
 * @param mpmc_queue The MPMC queue object to initialize.
 * @param init Configuration data for the queue.
 * @return Zero if the queue is successfully initialized, or non-zero if an error occurred.
 */
PAL_API(int)
PAL_MPMCQueueCreate_u32
(
    struct PAL_MPMC_QUEUE_U32 *mpmc_queue, 
    struct PAL_MPMC_QUEUE_INIT      *init
);

/* @summary Attempt to push a 32-bit unsigned integer value onto the back of the queue.
 * This function can be called by any producer or consumer thread.
 * @param mpmc_queue The MPMC queue object to receive the value.
 * @param item The value being enqueued.
 * @return Non-zero if the value was enqueued, or zero if the queue was full.
 */
PAL_API(int)
PAL_MPMCQueuePush_u32
(
    struct PAL_MPMC_QUEUE_U32 *mpmc_queue, 
    pal_uint32_t                     item
);

/* @summary Attempt to take a 32-bit unsigned integer value from the front of the queue.
 * This function can be called by any producer or consumer thread.
 * @param mpmc_queue The MPMC queue object from which the oldest value should be retrieved.
 * @param item If the function returns non-zero, on return this location stores the dequeued value.
 * @return Non-zero if a value was dequeued, or zero if the queue was empty.
 */
PAL_API(int)
PAL_MPMCQueueTake_u32
(
    struct PAL_MPMC_QUEUE_U32 *mpmc_queue, 
    pal_uint32_t                    *item
);

/* @summary Create a thread pool and run all initialization routines.
 * When this function returns, all threads are initialized, but are waiting for a call to PAL_ThreadPoolLaunch.
 * @param init Data used to configure the thread pool.
 * @return The thread pool, or NULL if an error occurred.
 */
PAL_API(struct PAL_THREAD_POOL*)
PAL_ThreadPoolCreate
(
    struct PAL_THREAD_POOL_INIT *init
);

/* @summary Wait for all threads in a thread pool to exit, and free any resources associated with the pool.
 * The application is responsible for signaling all threads to terminate, if necessary.
 * The calling thread will block until all threads have exited.
 * @param pool The thread pool to stop and delete.
 * @return Zero if all threads in the pool exited normally with an exit code of zero, or the first non-zero exit code.
 */
PAL_API(pal_uint32_t)
PAL_ThreadPoolDelete
(
    struct PAL_THREAD_POOL *pool
);

/* @summary Launch all threads in the pool, calling the thread entry point for each.
 * @param pool The thread pool to launch.
 * @return Zero if the thread pool has launched successfully, or non-zero if an error occurred.
 */
PAL_API(int)
PAL_ThreadPoolLaunch
(
    struct PAL_THREAD_POOL *pool
);

/* @summary Signal all threads in a thread pool that they should shutdown.
 * The calling thread does not wait until all threads shut down.
 * @param pool The thread pool to signal.
 */
PAL_API(void)
PAL_ThreadPoolSignalShutdown
(
    struct PAL_THREAD_POOL *pool
);

/* @summary Query the thread pool shutdown status. 
 * This function is intended to be called from a worker thread entry point.
 * The calling thread does not block while checking the shutdown status.
 * @param pool The thread pool that owns the calling thread.
 * @return Non-zero if the thread should shut down.
 */
PAL_API(int)
PAL_ThreadPoolShouldShutdown
(
    struct PAL_THREAD_POOL *pool
);

/* @summary Retrieve the number of threads in a thread pool.
 * @param pool The thread pool to query.
 * @return The number of worker threads in the pool.
 */
PAL_API(pal_uint32_t)
PAL_ThreadPoolGetThreadCount
(
    struct PAL_THREAD_POOL *pool
);

/* @summary Retrieve the context data associated with a thread pool.
 * @param pool The thread pool to query.
 * @return The context value associated with the pool at initialization time.
 */
PAL_API(void*)
PAL_ThreadPoolGetPoolContext
(
    struct PAL_THREAD_POOL *pool
);

/* @summary Retrieve the thread-local context data for a thread.
 * @param pool The thread pool that owns the worker thread.
 * @param thread_index The zero-based index of the worker thread.
 * @param context The value to associate with the thread context slot.
 * @return The value previously associated with the thread context slot.
 */
PAL_API(void*)
PAL_ThreadPoolSetThreadContext
(
    struct PAL_THREAD_POOL *pool, 
    pal_uint32_t    thread_index, 
    void                *context
);

/* @summary Retrieve the thread-local context data for a thread. 
 * Calls to this function must be externally synchronized.
 * @param pool The thread pool that owns the worker thread.
 * @param thread_index The zero-based index of the worker thread.
 * @return The value associated with the thread context slot.
 */
PAL_API(void*)
PAL_ThreadPoolGetThreadContext
(
    struct PAL_THREAD_POOL *pool, 
    pal_uint32_t    thread_index
);

#ifdef __cplusplus
}; /* extern "C" */
#endif

/* @summary Include the appropriate platform-specific header.
 */
#if   PAL_TARGET_PLATFORM == PAL_PLATFORM_WIN32 || PAL_TARGET_PLATFORM == PAL_PLATFORM_WINRT
    #include "pal_win32_thread.h"
#elif PAL_TARGET_PLATFORM == PAL_PLATFORM_LINUX || PAL_TARGET_PLATFORM == PAL_PLATFORM_ANDROID
    #include "pal_linux_thread.h"
#elif PAL_TARGET_PLATFORM == PAL_PLATFORM_MACOS || PAL_TARGET_PLATFORM == PAL_PLATFORM_IOS
    #include "pal_apple_thread.h"
#else
    #error   pal_thread.h: No implementation of the abstraction layer for your platform.
#endif

#endif /* __PAL_THREAD_H__ */
