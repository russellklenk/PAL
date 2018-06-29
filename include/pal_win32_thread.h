/**
 * @summary Define the platform-specific types and other internal bits for the 
 * Microsoft Windows Desktop platform.
 */
#ifndef __PAL_WIN32_THREAD_H__
#define __PAL_WIN32_THREAD_H__

#ifndef __PAL_THREAD_H__
#include "pal_thread.h"
#endif

#ifndef PAL_NO_INCLUDES
#include <Windows.h>
#include <process.h>
#endif

/* @summary Define the data describing the CPU layout of the host system.
 */
typedef struct PAL_CPU_INFO {
    pal_uint32_t                       NumaNodes;              /* The number of NUMA nodes in the host system. */
    pal_uint32_t                       PhysicalCPUs;           /* The total number of physical CPUs in the host system. */
    pal_uint32_t                       PhysicalCores;          /* The total number of physical cores across all CPUs. */
    pal_uint32_t                       HardwareThreads;        /* The total number of hardware threads across all CPUs. */
    pal_uint32_t                       ThreadsPerCore;         /* The number of hardware threads per physical core. */
    pal_uint32_t                       CacheSizeL1;            /* The total size of the smallest L1 data cache, in bytes. */
    pal_uint32_t                       CacheLineSizeL1;        /* The size of a single cache line in the L1 data cache, in bytes. */
    pal_uint32_t                       CacheSizeL2;            /* The total size of the smallest L2 data cache, in bytes. */
    pal_uint32_t                       CacheLineSizeL2;        /* The size of a single cache line in the L2 data cache, in bytes. */
    pal_sint32_t                       PreferAMD;              /* Non-zero if AMD implementations are preferred. */
    pal_sint32_t                       PreferIntel;            /* Non-zero if Intel implementations are preferred. */
    pal_sint32_t                       IsVirtualMachine;       /* Non-zero if the system appears to be a virtual machine. */
    char                               VendorName[16];         /* The nul-terminated CPUID vendor string. */
} PAL_CPU_INFO;

/* @summary Define the data associated with a semaphore. The semaphore is guaranteed to stay in userspace unless a thread must be awakened or put to sleep.
 */
typedef struct PAL_SEMAPHORE {
    HANDLE                             OSSemaphore;            /* The Win32 semaphore object. */
    pal_sint32_t                       ResourceCount;          /* The number of available resources. If this value is zero, or less than zero, one or more threads may be waiting. */
    pal_uint32_t                       SpinCount;              /* The number of times to spin while attempting to acquire a resource before entering a wait state. */
} PAL_SEMAPHORE;

/* @summary Define the data associated with a mutex. The mutex is guaranteed to stay in userspace unless a thread must be awakened or put to sleep.
 * The mutex is implemented as a binary semaphore.
 */
typedef struct PAL_MUTEX {
    PAL_SEMAPHORE                      Semaphore;              /* The semaphore used to implement the mutex. */
} PAL_MUTEX;

/* @summary Define the data associated with a reader-writer lock.
 * A reader-writer lock allows either a single writer thread to access the protected resource, or multiple concurrent read-only threads to access the protected resource, but not both.
 */
typedef struct PAL_RWLOCK {
    SRWLOCK                            SRWLock;                /* The Win32 slim reader-writer lock. */
} PAL_RWLOCK;

/* @summary Define the data associated with a monitor synchronization object.
 * A monitor provides mutual exclusion and the ability to wait for a condition to become satisfied.
 */
typedef struct PAL_MONITOR {
    CONDITION_VARIABLE                 CondVar;                /* The Win32 condition variable object. */
    SRWLOCK                            SRWLock;                /* The Win32 slim reader-writer lock. */
} PAL_MONITOR;

/* @summary Define the data associated with a barrier synchronization object, which blocks one or more threads until all threads have entered the barrier.
 * The region between PAL_BarrierEnter and PAL_BarrierLeave acts as a type of critical section; no thread will enter the region until all threads are ready to enter the region, and no thread will exit the region until all threads are ready to exit the region.
 */
typedef struct PAL_BARRIER {
    PAL_SEMAPHORE                      Mutex;                  /* The mutex protecting the critical section. */
    PAL_SEMAPHORE                      SemaphoreIn;            /* The semaphore associated with thread entry. */
    PAL_SEMAPHORE                      SemaphoreOut;           /* The semaphore associated with thread exit. */
    pal_sint32_t                       InsideCount;            /* The number of threads currently waiting at the barrier. */
    pal_sint32_t                       ThreadCount;            /* The number of threads executing the action. */
} PAL_BARRIER;

/* @summary Define the data associated with an eventcount synchronization object.
 * Register your interest in waiting using PAL_EventCountPrepareWait, but only actually wait if nothing else has signaled the eventcount in the meantime.
 * A waiter only needs to wait if there's actually no event in the queue.
 * See http://cbloomrants.blogspot.com/2011/07/07-08-11-who-ordered-event-count.html
 * See http://cbloomrants.blogspot.com/2011/07/07-08-11-event-count-and-condition.html
 */
typedef struct PAL_EVENTCOUNT {
    PAL_MONITOR                        Monitor;                /* The monitor object used for synchronization. */
    pal_sint32_t                       Counter;                /* The number of times the eventcount has been signaled. */
} PAL_EVENTCOUNT;


/* @summary Define the data associated with a fixed-size, SPSC concurrent queue of 32-bit unsigned integers.
 * The queue capacity must be a power-of-two.
 * The queue supports PUSH and TAKE operations.
 * The thread that owns the queue (the producer) can perform PUSH operations.
 * The other thread (the consumer) can only perform TAKE operations.
 */
typedef struct PAL_SPSC_QUEUE_U32 {
    pal_uint32_t                      *Storage;                /* Storage for queue items. Fixed-size, power-of-two capacity. */
    pal_uint32_t                       StorageMask;            /* The mask used to map EnqueuePos and DequeuePos into the storage array. */
    pal_uint32_t                       Capacity;               /* The maximum number of items that can be stored in the queue. */
    void                              *MemoryStart;            /* Pointer to the start of the memory block allocated for the storage array. */
    pal_uint64_t                       MemorySize;             /* The size of the memory block allocated for the storage array, in bytes. */
    pal_uint8_t                        PadShared[32];          /* Separate the shared data from the EnqueuePos. */
    pal_uint32_t                       EnqueuePos;             /* A monotonically-increasing integer representing the position of the next enqueue operation in the storage array. */
    pal_uint8_t                        PadEnqueue[60];         /* Separate the EnqueuePos from the DequeuePos. */
    pal_uint32_t                       DequeuePos;             /* A monotonically-increasing integer representing the position of the next dequeue operation in the storage array. */
    pal_uint8_t                        PadDequeue[60];         /* Separate the DequeuePos from any subsequent data. */
} PAL_SPSC_QUEUE_U32;

/* @summary Define the data associated with a fixed-size, SPMC concurrent queue of 32-bit unsigned integers.
 * The queue capacity must be a power-of-two.
 * The queue is actually a double-ended queue, supporting the operations PUSH, TAKE and STEAL.
 * The thread that owns the queue (the producer) can perform PUSH and TAKE operations.
 * Other threads (the consumers) can only perform STEAL operations.
 */
typedef struct PAL_SPMC_QUEUE_U32 {
    pal_uint32_t                      *Storage;                /* Storage for queue items. Fixed-size, power-of-two capacity. */
    pal_uint32_t                       StorageMask;            /* The mask used to map EnqueuePos and DequeuePos into the storage array. */
    pal_uint32_t                       Capacity;               /* The maximum number of items that can be stored in the queue. */
    void                              *MemoryStart;            /* Pointer to the start of the memory block allocated for the storage array. */
    pal_uint64_t                       MemorySize;             /* The size of the memory block allocated for the storage array, in bytes. */
    pal_uint8_t                        PadShared[32];          /* Separate the shared data from the PrivatePos. */
    pal_sint64_t                       PrivatePos;             /* A monotonically-increasing integer representing the position of the next enqueue operation in the storage array. */
    pal_uint8_t                        PadPrivate[56];         /* Separate the PrivatePos from the PublicPos. */
    pal_sint64_t                       PublicPos;              /* A monotonically-increasing integer representing the position of the next dequeue operation in the storage array. */
    pal_uint8_t                        PadPublic[56];          /* Separate the PublicPos from any subsequent data. */
} PAL_SPMC_QUEUE_U32;

/* @summary Define the data associated with a fixed-size, MPMC concurrent queue. 
 * The queue capacity must be a power-of-two.
 * The queue supports PUSH and TAKE operations.
 * Any thread may perform either a PUSH or a TAKE operation.
 * The queue implementation (C++) is originally by Dmitry Vyukov and is available here:
 * http://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue
 */
typedef struct PAL_MPMC_QUEUE_U32 {
    struct PAL_MPMC_CELL_U32          *Storage;                /* Storage for queue items. Fixed-size, power-of-two capacity. */
    pal_uint32_t                       StorageMask;            /* The mask used to map EnqueuePos and DequeuePos into the storage array. */
    pal_uint32_t                       Capacity;               /* The maximum number of items that can be stored in the queue. */
    void                              *MemoryStart;            /* Pointer to the start of the memory block allocated for the storage array. */
    pal_uint64_t                       MemorySize;             /* The size of the memory block allocated for the storage array, in bytes. */
    pal_uint8_t                        PadShared[32];          /* Separate the shared data from the EnqueuePos. */
    pal_uint32_t                       EnqueuePos;             /* A monotonically-increasing integer representing the position of the next enqueue operation in the storage array. */
    pal_uint8_t                        PadEnqueue[60];         /* Separate the EnqueuePos from the DequeuePos. */
    pal_uint32_t                       DequeuePos;             /* A monotonically-increasing integer representing the position of the next dequeue operation in the storage array. */
    pal_uint8_t                        PadDequeue[60];         /* Separate the DequeuePos from any subsequent data. */
} PAL_MPMC_QUEUE_U32;

/* @summary Define the data associated with a thread pool.
 */
typedef struct PAL_THREAD_POOL {
    unsigned int                      *OsThreadIds;            /* An array of ThreadCount operating system thread identifiers. */
    HANDLE                            *OsThreadHandles;        /* An array of ThreadCount operating system thread handles. */
    void                             **ThreadContext;          /* An array of ThreadCount pointers to per-thread context data.*/
    void                              *PoolContext;            /* Opaque data supplied by the application and associated with the thread pool. */
    HANDLE                             LaunchSignal;           /* A manual-reset event used to synchronize thread launch. */
    LONG volatile                      ShouldShutdown;         /* A boolean flag that becomes non-zero when threads should stop running. */
    pal_uint32_t                       ThreadCount;            /* The total number of threads in the pool. */
} PAL_THREAD_POOL;

#endif /* __PAL_WIN32_THREAD_H__ */
