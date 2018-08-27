/**
 * @summary Implement the PAL entry points from pal_thread.h.
 */
#include "pal_win32_thread.h"
#include "pal_win32_memory.h"

/* @summary Determine whether a given value is a power of two or not.
 * @param _value The value to check.
 * @return Non-zero if the value is a power-of-two, or zero otherwise.
 */
#ifndef PAL_IsPowerOfTwo
#define PAL_IsPowerOfTwo(_value)                                              \
    (((_value) & ((_value)-1)) == 0)
#endif

/* @summary The Win32 API entry point for the SetThreadDescription function, used to set the name of a thread.
 * The name will appear in the debugger as well as any captured event trace logs.
 * This function is available on Windows 10 Creators Update and later.
 * @param hThread A handle for the thread for which you want to set the description. The handle must have THREAD_SET_LIMITED_INFORMATION access.
 * @param lpThreadDescription A nul-terminated Unicode string that specifies the description of the thread.
 * @return If the function succeeds, the return value is the HRESULT that denotes a successful operation. If the function fails, the return value is an HRESULT that denotes the error.
 * Used the SUCCEEDED and FAILED macros to check the return value.
 */
typedef HRESULT (WINAPI *PAL_Win32_SetThreadDescription_Fn)
(
    HANDLE              hThread, 
    LPCWSTR lpThreadDescription
);

/* @summary Define the structure used as a payload for the exception used to set a thread name pre-Win10 Creators Update.
 * This technique is documented under How To: Set a Thread Name in Native Code on MSDN:
 * https://msdn.microsoft.com/en-us/library/xcb2z8hs.aspx
 */
#pragma pack(push,8)
typedef struct PAL_WIN32_THREADNAME_INFO {
    DWORD                   dwType;           /* must be 0x1000 */
    LPCSTR                  szName;           /* the name to set */
    DWORD                   dwThreadID;       /* thread ID, or 0xFFFFFFFFUL to use the calling thread ID */
    DWORD                   dwFlags;          /* reserved for future use, set to 0 */
} PAL_WIN32_THREADNAME_INFO;
#pragma pack(pop)

/* @summary Define the data associated with a single item in an MPMC concurrent queue.
 */
typedef struct PAL_MPMC_CELL_U32 {
    pal_uint32_t            Sequence;         /* The sequence number assigned to the cell. */
    pal_uint32_t            Value;            /* The 32-bit integer value stored in the cell. */
} PAL_MPMC_CELL_U32;

/* @summary Define the data passed to the operating system thread entry point as the argp parameter.
 */
typedef struct PAL_WIN32_THREAD_INIT {
    struct PAL_THREAD_POOL *ThreadPool;       /* The PAL_THREAD_POOL that owns the thread. */
    PAL_THREAD_FUNC         UserCallbacks;    /* The set of callbacks to invoke for various operations. */
    void                   *PoolContext;      /* Opaque data supplied by the application and available to each thread. */
    HANDLE                  ReadySignal;      /* A manual-reset event to be signaled by the thread when it has successfully initialized. */
    HANDLE                  ErrorSignal;      /* A manual-reset event to be signaled by the thread if it encounters an error during initialization. */
    HANDLE                  LaunchSignal;     /* A manual-reset event to be waited on by the thread before calling the thread main callback. */
    pal_uint32_t            ThreadIndex;      /* The zero-based index uniquely identifying the thread within the thread pool. */
    pal_uint32_t            ThreadCount;      /* The total number of threads in the thread pool. */
} PAL_WIN32_THREAD_INIT;

/* @summary Implement a replacement for the strcmp function.
 * This function is not optimized and should not be used in performance-critical code.
 * @param str_a Pointer to a nul-terminated string. This value cannot be NULL.
 * @param str_b Pointer to a nul-terminated string. This value cannot be NULL.
 * @return Zero if the two strings match. A positive value if the first non-matching character in str_a > the corresponding character in str_b. A negative value if the first non-matching character str_a < the corresponding character in str_b.
 */
static int
PAL_StringCompare
(
    char const *str_a, 
    char const *str_b
)
{
    unsigned char const *a = (unsigned char const *) str_a;
    unsigned char const *b = (unsigned char const *) str_b;
    while (*a && (*a == *b))
    {
        ++a;
        ++b;
    }
    return (*a - *b);
}

/* @summary Calculate the number of bits set in a processor affinity mask.
 * @param processor_mask The processor affinity mask to check.
 * @return The number of bits set in the mask.
 */
static pal_uint32_t
PAL_CountSetBitsInProcessorMask
(
    ULONG_PTR processor_mask
)
{
    pal_uint32_t         i;
    pal_uint32_t set_count = 0;
    pal_uint32_t max_shift = sizeof(ULONG_PTR) * 8 - 1;
    ULONG_PTR     test_bit =((ULONG_PTR)1) << max_shift;
    for (i = 0; i <= max_shift; ++i)
    {
        set_count +=(processor_mask & test_bit) ? 1 : 0;
        test_bit >>= 1;
    }
    return set_count;
}

/* @summary Attempt to claim a resource protected by a semaphore.
 * @param sem The PAL_SEMAPHORE protecting the resource.
 * @return 1 if a resource was claimed, or 0 if no resources are available.
 */
static PAL_INLINE int
PAL_SemaphoreTryWait
(
    struct PAL_SEMAPHORE *sem
)
{
    pal_sint32_t value;
    pal_sint32_t count = sem->ResourceCount;
    _ReadWriteBarrier();
    while (count > 0)
    {
        if ((value = _InterlockedCompareExchange((volatile LONG*)&sem->ResourceCount, count-1, count)) == count)
        {   /* success */
            return 1;
        }
        else
        {   /* try again */
            count = value;
        }
    }
    return 0;
}

/* @summary Attempt to claim a resource protected by a semaphore. If no resources are available, put the calling thread into a wait state.
 * @param sem The PAL_SEMAPHORE protecting the resource.
 */
static PAL_INLINE void
PAL_SemaphoreWaitNoSpin
(
    struct PAL_SEMAPHORE *sem
)
{
    if (_InterlockedExchangeAdd((volatile LONG*)&sem->ResourceCount, -1) < 1)
        WaitForSingleObject(sem->OSSemaphore, INFINITE);
}

/* @summary Define a default thread pool thread initialization routine.
 * @param init Information about the thread. The initialization routine should set PAL_THREAD_INIT::ThreadContext to point to any data associated with the thread.
 * @return Zero if the thread initialization routine completes successfully, or non-zero if an error occurs.
 */
static pal_uint32_t
PAL_ThreadPool_DefaultThreadInit
(
    struct PAL_THREAD_INIT *init
)
{
    init->ThreadContext = NULL;
    return 0;
}

/* @summary Implement the thread entry point for a thread pool worker thread.
 * @param argp A pointer to a PAL_WIN32_THREAD_INIT structure.
 * @return The thread exit code.
 */
static unsigned int __stdcall
PAL_ThreadPoolThreadEntry
(
    void *argp
)
{
    PAL_WIN32_THREAD_INIT *init =(PAL_WIN32_THREAD_INIT*) argp;
    PAL_ThreadInit_Func th_init = init->UserCallbacks.Init;
    PAL_ThreadMain_Func th_main = init->UserCallbacks.Main;
    HANDLE               launch = init->LaunchSignal;
    DWORD               wait_rc = WAIT_OBJECT_0;
    pal_uint32_t      exit_code = 0;
    PAL_THREAD_INIT   th_public;

    __try {
        /* copy data into the public type */
        th_public.ThreadPool    = init->ThreadPool;
        th_public.PoolContext   = init->PoolContext;
        th_public.ThreadContext = NULL; /* set by th_init */
        th_public.ThreadIndex   = init->ThreadIndex;
        th_public.ThreadCount   = init->ThreadCount;

        /* run the thread initialization routine.
         * these run serialized, one at a time.
         */
        if ((exit_code = th_init(&th_public)) != 0) {
            SetEvent(init->ErrorSignal);
            return exit_code;
        }
        PAL_ThreadPoolSetThreadContext(init->ThreadPool, init->ThreadIndex, th_public.ThreadContext);

        /* indicate that the thread has initialized successfully.
         * do not access the fields of the init structure past this point.
         */
        SetEvent(init->ReadySignal); init = NULL;

        /* wait until the thread pool is launched */
        if ((wait_rc  = WaitForSingleObject(launch, INFINITE)) == WAIT_OBJECT_0) {
            /* execute the user thread entry point */
            exit_code = th_main(&th_public);
        } else {
            /* waiting for the launch signal failed */
            exit_code = GetLastError();
        }
    } __finally {
        /* TODO: cleanup */
    }
    return exit_code;
}

/* @summary Determine the amount of memory required to initialize a thread pool.
 * @param init Data specifying attributes of the thread pool.
 * @return The minimum number of bytes required to create a thread pool.
 */
static pal_usize_t
PAL_ThreadPoolQueryMemorySize
(
    struct PAL_THREAD_POOL_INIT *init
)
{
    pal_usize_t required_size = 0;
    required_size += PAL_AllocationSizeType (PAL_THREAD_POOL);
    required_size += PAL_AllocationSizeArray(unsigned int, init->ThreadCount);
    required_size += PAL_AllocationSizeArray(HANDLE      , init->ThreadCount);
    required_size += PAL_AllocationSizeArray(void *      , init->ThreadCount);
    return required_size;
}

PAL_API(pal_uint32_t)
PAL_ThreadGetId
(
    void
)
{
    return (pal_uint32_t) GetCurrentThreadId();
}

PAL_API(void)
PAL_ThreadSetName
(
    char const *name
)
{
    PAL_WIN32_THREADNAME_INFO info;
    PAL_Win32_SetThreadDescription_Fn  SetThreadDescription_Func = NULL;
    DWORD    nwords =(DWORD)(sizeof(PAL_WIN32_THREADNAME_INFO) / sizeof(ULONG_PTR));
    HANDLE kernel32 = GetModuleHandleW(L"Kernel32.dll");

#pragma warning(push)
#pragma warning(disable: 6320 6322)
    __try {
        info.dwType     = 0x1000;
        info.szName     = name;
        info.dwThreadID = GetCurrentThreadId();
        info.dwFlags    = 0;
        RaiseException(0x406d1388, 0, nwords, (ULONG_PTR*)&info);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
#pragma warning(pop)

    /* Windows 10 Creators Update provides a new API for this */
    if ((SetThreadDescription_Func = (PAL_Win32_SetThreadDescription_Fn) GetProcAddress(kernel32, "SetThreadDescription")) != NULL)
    {   /* ...but of course it requires the thread name to be a wide string */
        wchar_t wide_name[129];
        size_t      nchars = 0;
        mbstowcs_s(&nchars, wide_name, 129, name, _TRUNCATE);
        SetThreadDescription_Func(GetCurrentThread(), wide_name);
    }
    /* for linux: pthread_setname_np https://stackoverflow.com/questions/2369738/can-i-set-the-name-of-a-thread-in-pthreads-linux */
}

PAL_API(void)
PAL_ThreadSleep
(
    pal_uint64_t duration_ns
)
{
    pal_uint32_t whole_ms = (pal_uint32_t)(duration_ns / 1000000ULL);
    Sleep(whole_ms);
}

PAL_API(void)
PAL_ThreadYield
(
    void
)
{
    YieldProcessor();
}

PAL_API(int)
PAL_CpuInfoQuery
(
    struct PAL_CPU_INFO *cpu_info
)
{   /* this function supports systems with up to 64 cores */
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION *lpibuf = NULL;
    pal_usize_t                             i, n;
    pal_uint32_t                     num_threads = 0;
    DWORD                            buffer_size = 0;
    int                                  regs[4] ={0, 0, 0, 0};

    PAL_ZeroMemory(cpu_info, sizeof(PAL_CPU_INFO));

#if PAL_TARGET_ARCHITECTURE == PAL_ARCHITECTURE_X64
    /* retrieve the CPU vendor string using the __cpuid intrinsic */
    __cpuid(regs, 0);
    *((int*) &cpu_info->VendorName[0]) = regs[1];
    *((int*) &cpu_info->VendorName[4]) = regs[3];
    *((int*) &cpu_info->VendorName[8]) = regs[2];
         if (!PAL_StringCompare(cpu_info->VendorName, "AuthenticAMD")) cpu_info->PreferAMD        = 1;
    else if (!PAL_StringCompare(cpu_info->VendorName, "GenuineIntel")) cpu_info->PreferIntel      = 1;
    else if (!PAL_StringCompare(cpu_info->VendorName, "KVMKVMKVMKVM")) cpu_info->IsVirtualMachine = 1;
    else if (!PAL_StringCompare(cpu_info->VendorName, "Microsoft Hv")) cpu_info->IsVirtualMachine = 1;
    else if (!PAL_StringCompare(cpu_info->VendorName, "VMwareVMware")) cpu_info->IsVirtualMachine = 1;
    else if (!PAL_StringCompare(cpu_info->VendorName, "XenVMMXenVMM")) cpu_info->IsVirtualMachine = 1;
    else if (!PAL_StringCompare(cpu_info->VendorName, " lrpepyh vr" )) cpu_info->IsVirtualMachine = 1;
    else if (!PAL_StringCompare(cpu_info->VendorName, "bhyve bhyve" )) cpu_info->IsVirtualMachine = 1;
#else
    /* non-x86/x64 such as ARM or PPC - CPUID is not available */
    UNREFERENCED_PARAMETER(regs);
    cpu_info->PreferAMD        = 0;
    cpu_info->PreferIntel      = 0;
    cpu_info->IsVirtualMachine = 0;
#endif

    /* inspect the CPU topology. this requires scratch memory. */
    GetLogicalProcessorInformation(NULL, &buffer_size);
    if ((lpibuf = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION*) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, buffer_size)) == NULL)
    {   /* failed to allocate the required amount of memory */
        PAL_ZeroMemory(cpu_info, sizeof(PAL_CPU_INFO));
        return -1;
    }
    if (GetLogicalProcessorInformation(lpibuf, &buffer_size))
    {   /* at least one SYSTEM_LOGICAL_PROCESSOR_INFORMATION was returned */
        for (i = 0, n = buffer_size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION); i < n; ++i)
        {
            switch (lpibuf[i].Relationship)
            {
                case RelationProcessorCore:
                    { num_threads = PAL_CountSetBitsInProcessorMask(lpibuf[i].ProcessorMask);
                      cpu_info->HardwareThreads += num_threads;
                      cpu_info->ThreadsPerCore = num_threads;
                      cpu_info->PhysicalCores++;
                    } break;
                case RelationNumaNode:
                    { cpu_info->NumaNodes++;
                    } break;
                case RelationCache:
                    { if (lpibuf[i].Cache.Level == 1 && lpibuf[i].Cache.Type == CacheData)
                      { /* L1 cache information */
                          cpu_info->CacheSizeL1     = lpibuf[i].Cache.Size;
                          cpu_info->CacheLineSizeL1 = lpibuf[i].Cache.LineSize;
                      }
                      if (lpibuf[i].Cache.Level == 2 && lpibuf[i].Cache.Type == CacheUnified)
                      { /* L2 cache information */
                          cpu_info->CacheSizeL2     = lpibuf[i].Cache.Size;
                          cpu_info->CacheLineSizeL2 = lpibuf[i].Cache.LineSize;
                      }
                    } break;
                case RelationProcessorPackage:
                    { cpu_info->PhysicalCPUs++;
                    } break;
                default:
                    { /* unknown relationship type - skip */
                    } break;
            }
        }
        HeapFree(GetProcessHeap(), 0, lpibuf);
        return 0;
    }
    else
    {   /* the call to GetLogicalProcessorInformation failed */
        PAL_ZeroMemory(cpu_info, sizeof(PAL_CPU_INFO));
        HeapFree(GetProcessHeap(), 0, lpibuf);
        return -1;
    }
}

PAL_API(int)
PAL_SemaphoreCreate
(
    struct PAL_SEMAPHORE *sem, 
    pal_sint32_t        value
)
{
    if ((sem->OSSemaphore = CreateSemaphore(NULL, 0, LONG_MAX, NULL)) == NULL)
        return GetLastError();
    sem->ResourceCount = value;
    sem->SpinCount     = PAL_DEFAULT_SPIN_COUNT;
    return 0;
}

PAL_API(void)
PAL_SemaphoreDelete
(
    struct PAL_SEMAPHORE *sem
)
{
    if (sem->OSSemaphore != NULL)
    {
        CloseHandle(sem->OSSemaphore);
        sem->OSSemaphore  = NULL;
    }
}

PAL_API(pal_uint32_t)
PAL_SemaphoreSetSpinCount
(
    struct PAL_SEMAPHORE *sem, 
    pal_uint32_t   spin_count
)
{
    pal_uint32_t old_count = sem->SpinCount;
    sem->SpinCount = spin_count;
    return old_count;
}

PAL_API(void)
PAL_SemaphoreWait
(
    struct PAL_SEMAPHORE *sem
)
{
    pal_uint32_t spin_count = sem->SpinCount;
    while (spin_count > 0)
    {
        if (PAL_SemaphoreTryWait(sem))
            return;
        --spin_count;
    }
    PAL_SemaphoreWaitNoSpin(sem);
}

PAL_API(void)
PAL_SemaphorePostOne
(
    struct PAL_SEMAPHORE *sem
)
{
    if (_InterlockedExchangeAdd((volatile LONG*)&sem->ResourceCount, +1) < 0)
        ReleaseSemaphore(sem->OSSemaphore, 1, 0);
}

PAL_API(void)
PAL_SemaphorePostMany
(
    struct PAL_SEMAPHORE *sem, 
    pal_sint32_t   post_count
)
{
    pal_sint32_t old_count = _InterlockedExchangeAdd((volatile LONG*)&sem->ResourceCount, post_count);
    if (old_count < 0)
    {
        pal_sint32_t num_waiters = -old_count;
        pal_sint32_t num_to_wake = (num_waiters < post_count) ? num_waiters : post_count; /* min(num_waiters, post_count) */
        ReleaseSemaphore(sem->OSSemaphore, num_to_wake, 0);
    }
}

PAL_API(int)
PAL_MutexCreate
(
    struct PAL_MUTEX *mtx
)
{
    return PAL_SemaphoreCreate(&mtx->Semaphore, 1);
}

PAL_API(void)
PAL_MutexDelete
(
    struct PAL_MUTEX *mtx
)
{
    PAL_SemaphoreDelete(&mtx->Semaphore);
}

PAL_API(pal_uint32_t)
PAL_MutexSetSpinCount
(
    struct PAL_MUTEX   *mtx, 
    pal_uint32_t spin_count
)
{
    return PAL_SemaphoreSetSpinCount(&mtx->Semaphore, spin_count);
}

PAL_API(void)
PAL_MutexAcquire
(
    struct PAL_MUTEX *mtx
)
{
    PAL_SemaphoreWait(&mtx->Semaphore);
}

PAL_API(void)
PAL_MutexRelease
(
    struct PAL_MUTEX *mtx
)
{
    PAL_SemaphorePostOne(&mtx->Semaphore);
}

PAL_API(int)
PAL_RWLockCreate
(
    struct PAL_RWLOCK *rwl
)
{
    InitializeSRWLock(&rwl->SRWLock);
    return 0;
}

PAL_API(void)
PAL_RWLockDelete
(
    struct PAL_RWLOCK *rwl
)
{
    UNREFERENCED_PARAMETER(rwl);
}

PAL_API(void)
PAL_RWLockAcquireWriter
(
    struct PAL_RWLOCK *rwl
)
{
    AcquireSRWLockExclusive(&rwl->SRWLock);
}

PAL_API(void)
PAL_RWLockReleaseWriter
(
    struct PAL_RWLOCK *rwl
)
{
    ReleaseSRWLockExclusive(&rwl->SRWLock);
}

PAL_API(void)
PAL_RWLockAcquireReader
(
    struct PAL_RWLOCK *rwl
)
{
    AcquireSRWLockShared(&rwl->SRWLock);
}

PAL_API(void)
PAL_RWLockReleaseReader
(
    struct PAL_RWLOCK *rwl
)
{
    ReleaseSRWLockShared(&rwl->SRWLock);
}

PAL_API(int)
PAL_MonitorCreate
(
    struct PAL_MONITOR *mon
)
{
    InitializeConditionVariable(&mon->CondVar);
    InitializeSRWLock(&mon->SRWLock);
    return 0;
}

PAL_API(void)
PAL_MonitorDelete
(
    struct PAL_MONITOR *mon
)
{
    UNREFERENCED_PARAMETER(mon);
}

PAL_API(void)
PAL_MonitorAcquire
(
    struct PAL_MONITOR *mon
)
{
    AcquireSRWLockExclusive(&mon->SRWLock);
}

PAL_API(void)
PAL_MonitorRelease
(
    struct PAL_MONITOR *mon
)
{
    ReleaseSRWLockExclusive(&mon->SRWLock);
}

PAL_API(void)
PAL_MonitorReleaseAndWait
(
    struct PAL_MONITOR *mon
)
{
    (void) SleepConditionVariableSRW(&mon->CondVar, &mon->SRWLock, INFINITE, 0);
}

PAL_API(void)
PAL_MonitorSignal
(
    struct PAL_MONITOR *mon
)
{
    WakeConditionVariable(&mon->CondVar);
}

PAL_API(void)
PAL_MonitorBroadcast
(
    struct PAL_MONITOR *mon
)
{
    WakeAllConditionVariable(&mon->CondVar);
}

PAL_API(int)
PAL_BarrierCreate
(
    struct PAL_BARRIER *barrier, 
    pal_sint32_t   thread_count
)
{   assert(thread_count > 0);
    PAL_SemaphoreCreate(&barrier->Mutex       , 1);
    PAL_SemaphoreCreate(&barrier->SemaphoreIn , 0);
    PAL_SemaphoreCreate(&barrier->SemaphoreOut, 0);
    barrier->InsideCount = 0;
    barrier->ThreadCount = thread_count;
    return 0;
}

PAL_API(void)
PAL_BarrierDelete
(
    struct PAL_BARRIER *barrier
)
{
    PAL_SemaphoreDelete(&barrier->SemaphoreOut);
    PAL_SemaphoreDelete(&barrier->SemaphoreIn);
    PAL_SemaphoreDelete(&barrier->Mutex);
}

PAL_API(pal_uint32_t)
PAL_BarrierSetSpinCount
(
    struct PAL_BARRIER *barrier, 
    pal_uint32_t     spin_count
)
{
    pal_uint32_t old_count = PAL_SemaphoreSetSpinCount(&barrier->Mutex, spin_count);
    PAL_SemaphoreSetSpinCount(&barrier->SemaphoreIn , spin_count);
    PAL_SemaphoreSetSpinCount(&barrier->SemaphoreOut, spin_count);
    return old_count;
}

PAL_API(void)
PAL_BarrierEnter
(
    struct PAL_BARRIER *barrier
)
{
    PAL_SemaphoreWait(&barrier->Mutex);
    barrier->InsideCount += 1;
    if (barrier->InsideCount == barrier->ThreadCount)
    {
        PAL_SemaphorePostMany(&barrier->SemaphoreIn, barrier->ThreadCount);
    }
    PAL_SemaphorePostOne(&barrier->Mutex);
    PAL_SemaphoreWait(&barrier->SemaphoreIn);
}

PAL_API(void)
PAL_BarrierLeave
(
    struct PAL_BARRIER *barrier
)
{
    PAL_SemaphoreWait(&barrier->Mutex);
    barrier->InsideCount -= 1;
    if (barrier->InsideCount == 0)
    {
        PAL_SemaphorePostMany(&barrier->SemaphoreOut, barrier->ThreadCount);
    }
    PAL_SemaphorePostOne(&barrier->Mutex);
    PAL_SemaphoreWait(&barrier->SemaphoreOut);
}

PAL_API(int)
PAL_EventCountCreate
(
    struct PAL_EVENTCOUNT *ec
)
{
    PAL_MonitorCreate(&ec->Monitor);
    ec->Counter = 0;
    return 0;
}

PAL_API(void)
PAL_EventCountDelete
(
    struct PAL_EVENTCOUNT *ec
)
{
    PAL_MonitorDelete(&ec->Monitor);
}

PAL_API(pal_sint32_t)
PAL_EventCountPrepareWait
(
    struct PAL_EVENTCOUNT *ec
)
{
    return _InterlockedOr((volatile LONG*)&ec->Counter, 1);
}

PAL_API(void)
PAL_EventCountPerformWait
(
    struct PAL_EVENTCOUNT *ec, 
    pal_sint32_t        token
)
{
    pal_sint32_t value;
    PAL_MonitorAcquire(&ec->Monitor);
    value = ec->Counter;
    _ReadWriteBarrier();
    if ((value & ~1) == (token & ~1))
        PAL_MonitorReleaseAndWait(&ec->Monitor);
    PAL_MonitorRelease(&ec->Monitor);
}

PAL_API(void)
PAL_EventCountSignal
(
    struct PAL_EVENTCOUNT *ec
)
{
    pal_sint32_t val;
    pal_sint32_t key = _InterlockedAdd((volatile LONG*)&ec->Counter, 0);
    if (key & 1)
    {
        PAL_MonitorAcquire(&ec->Monitor);
        while ((val = _InterlockedCompareExchange((volatile LONG*)&ec->Counter, (key+2) & ~1, key)) != key)
        {
            key = val;
        }
        PAL_MonitorRelease(&ec->Monitor);
        PAL_MonitorBroadcast(&ec->Monitor);
    }
}

PAL_API(pal_usize_t)
PAL_SPSCQueueQueryMemorySize_u32
(
    pal_uint32_t capacity
)
{   assert(capacity >= 2);
    assert(PAL_IsPowerOfTwo(capacity));
    return((capacity + 1) * sizeof(pal_uint32_t));
}

PAL_API(int)
PAL_SPSCQueueCreate_u32
(
    struct PAL_SPSC_QUEUE_U32 *spsc_queue, 
    struct PAL_SPSC_QUEUE_INIT      *init
)
{
    pal_usize_t bytes_required = 0;

    if (init->Capacity < 2)
    {   assert(init->Capacity >= 2);
        PAL_ZeroMemory(spsc_queue, sizeof(PAL_SPSC_QUEUE_U32));
        return -1;
    }
    if (PAL_IsPowerOfTwo(init->Capacity) == 0)
    {   assert(PAL_IsPowerOfTwo(init->Capacity));
        PAL_ZeroMemory(spsc_queue, sizeof(PAL_SPSC_QUEUE_U32));
        return -1;
    }
    if (init->MemoryStart == NULL || init->MemorySize < (3 * sizeof(pal_uint32_t)))
    {   assert(init->MemoryStart != NULL);
        assert(init->MemorySize  >=(3 * sizeof(pal_uint32_t)));
        PAL_ZeroMemory(spsc_queue, sizeof(PAL_SPSC_QUEUE_U32));
        return -1;
    }

    bytes_required = PAL_SPSCQueueQueryMemorySize_u32(init->Capacity);
    if (init->MemorySize < bytes_required)
    {   assert(init->MemorySize >= bytes_required);
        PAL_ZeroMemory(spsc_queue, sizeof(PAL_SPSC_QUEUE_U32));
        return -1;
    }

    spsc_queue->Storage     =(pal_uint32_t*) init->MemoryStart;
    spsc_queue->StorageMask =(pal_uint32_t ) init->Capacity - 1;
    spsc_queue->Capacity    =(pal_uint32_t ) init->Capacity;
    spsc_queue->MemoryStart =(void        *) init->MemoryStart;
    spsc_queue->MemorySize  =(pal_uint64_t ) init->MemorySize;
    spsc_queue->EnqueuePos  = 0;
    spsc_queue->DequeuePos  = 0;
    return 0;
}

PAL_API(int)
PAL_SPSCQueuePush_u32
(
    struct PAL_SPSC_QUEUE_U32 *spsc, 
    pal_uint32_t               item
)
{
    pal_uint32_t *stor = spsc->Storage;           /* constant */
    pal_uint32_t  mask = spsc->StorageMask;       /* constant */
    pal_uint32_t  nmax = spsc->Capacity;          /* constant */
    pal_uint32_t  epos = spsc->EnqueuePos;        /* load-acquire */
    _ReadWriteBarrier();
    if ((epos - spsc->DequeuePos) != nmax)
    {   /* there's at least one slot available in the queue */
        stor[epos & mask] = item;
        _ReadWriteBarrier();
        spsc->EnqueuePos  = epos + 1;             /* store-release */
        return 1;
    }
    return 0;
}

PAL_API(int)
PAL_SPSCQueueTake_u32
(
    struct PAL_SPSC_QUEUE_U32 *spsc, 
    pal_uint32_t              *item
)
{
    pal_uint32_t *stor = spsc->Storage;     /* constant */
    pal_uint32_t  mask = spsc->StorageMask; /* constant */
    pal_uint32_t  dpos = spsc->DequeuePos;  /* load-relaxed */
    pal_uint32_t  epos = spsc->EnqueuePos;  /* load-acquire */
    _ReadWriteBarrier();
    if ((epos - dpos) != 0)
    {
       *item = stor[dpos & mask];
        _ReadWriteBarrier();
        spsc->DequeuePos = dpos + 1;        /* store-release */
        return 1;
    }
    return 0;
}

PAL_API(pal_usize_t)
PAL_SPMCQueueQueryMemorySize_u32
(
    pal_uint32_t capacity
)
{   assert(capacity >= 2);
    assert(PAL_IsPowerOfTwo(capacity));
    return(capacity * sizeof(pal_uint32_t));
}

PAL_API(int)
PAL_SPMCQueueCreate_u32
(
    struct PAL_SPMC_QUEUE_U32 *spmc_queue, 
    struct PAL_SPMC_QUEUE_INIT      *init
)
{
    pal_usize_t bytes_required = 0;

    if (init->Capacity < 2)
    {   assert(init->Capacity >= 2);
        PAL_ZeroMemory(spmc_queue, sizeof(PAL_SPMC_QUEUE_U32));
        return -1;
    }
    if (PAL_IsPowerOfTwo(init->Capacity) == 0)
    {   assert(PAL_IsPowerOfTwo(init->Capacity));
        PAL_ZeroMemory(spmc_queue, sizeof(PAL_SPMC_QUEUE_U32));
        return -1;
    }
    if (init->MemoryStart == NULL)
    {   assert(init->MemoryStart != NULL);
        PAL_ZeroMemory(spmc_queue, sizeof(PAL_SPMC_QUEUE_U32));
        return -1;
    }

    bytes_required = PAL_SPMCQueueQueryMemorySize_u32(init->Capacity);
    if (init->MemorySize < bytes_required)
    {   assert(init->MemorySize >= bytes_required);
        PAL_ZeroMemory(spmc_queue, sizeof(PAL_SPMC_QUEUE_U32));
        return -1;
    }

    spmc_queue->Storage     =(pal_uint32_t*) init->MemoryStart;
    spmc_queue->StorageMask =(pal_uint32_t ) init->Capacity - 1;
    spmc_queue->Capacity    =(pal_uint32_t ) init->Capacity;
    spmc_queue->MemoryStart =(void        *) init->MemoryStart;
    spmc_queue->MemorySize  =(pal_uint64_t ) init->MemorySize;
    spmc_queue->PublicPos   = 0;
    spmc_queue->PrivatePos  = 0;
    return 0;
}

PAL_API(int)
PAL_SPMCQueuePush_u32
(
    struct PAL_SPMC_QUEUE_U32 *spmc, 
    pal_uint32_t               item
)
{
    pal_uint32_t *stor = spmc->Storage;     /* constant */
    pal_sint64_t  mask = spmc->StorageMask; /* constant */
    pal_sint64_t   pos = spmc->PrivatePos;  /* no concurrent take can happen */
    stor[pos & mask]   = item;
    _ReadWriteBarrier();                    /* make item visible before spmc->PrivatePos is updated */
    spmc->PrivatePos   = pos + 1;
    return 1;
}

PAL_API(int)
PAL_SPMCQueueTake_u32
(
    struct PAL_SPMC_QUEUE_U32 *spmc, 
    pal_uint32_t              *item
)
{
    pal_uint32_t *stor = spmc->Storage;             /* constant */
    pal_sint64_t  mask = spmc->StorageMask;         /* constant */
    pal_sint64_t   pos = spmc->PrivatePos - 1;      /* no concurrent push operation can happen */
    pal_sint64_t   top;

    _InterlockedExchange64(&spmc->PrivatePos, pos); /* also acts as a memory barrier */
    top = spmc->PublicPos;

    if (top <= pos)
    {   /* the deque is currently non-empty */
       *item = stor[pos & mask];
        if (top != pos)
        {   /* there's at least one more item in the deque - no need to race */
            return 1;
        }
        /* this was the final item in the deque - race a concurrent steal to claim it */
        if (_InterlockedCompareExchange64(&spmc->PublicPos, top+1, top) == top)
        {   /* this thread won the race */
            spmc->PrivatePos = top + 1;
            return 1;
        }
        else
        {   /* this thread lost the race */
            spmc->PrivatePos = top + 1;
            return 0;
        }
    }
    else
    {   /* the deque is currently empty */
        spmc->PrivatePos = top;
        return 0;
    }
}

PAL_API(int)
PAL_SPMCQueueSteal_u32
(
    struct PAL_SPMC_QUEUE_U32 *spmc, 
    pal_uint32_t              *item
)
{
    pal_uint32_t *stor = spmc->Storage;      /* constant */
    pal_sint64_t  mask = spmc->StorageMask;  /* constant */
    pal_sint64_t   top = spmc->PublicPos;    /* load-acquire */
    _ReadWriteBarrier();
    pal_sint64_t   pos = spmc->PrivatePos;   /* read-only to see if queue is non-empty */
    if (top < pos)
    {   /* the deque is currently non-empty */
       *item = stor[top & mask];
        /* race with other threads to claim the item */
        return (_InterlockedCompareExchange64(&spmc->PublicPos, top+1, top) == top) ? 1 : 0; /* store-release */
    }
    return 0;
}

PAL_API(pal_usize_t)
PAL_MPMCQueueQueryMemorySize_u32
(
    pal_uint32_t capacity
)
{   assert(capacity >= 2);
    assert(PAL_IsPowerOfTwo(capacity));
    return(capacity * sizeof(PAL_MPMC_CELL_U32));
}

PAL_API(int)
PAL_MPMCQueueCreate_u32
(
    struct PAL_MPMC_QUEUE_U32 *mpmc_queue, 
    struct PAL_MPMC_QUEUE_INIT      *init
)
{
    pal_usize_t bytes_required = 0;
    pal_uint32_t          i, n;

    if (init->Capacity < 2)
    {   assert(init->Capacity >= 2);
        PAL_ZeroMemory(mpmc_queue, sizeof(PAL_MPMC_QUEUE_U32));
        return -1;
    }
    if (PAL_IsPowerOfTwo(init->Capacity) == 0)
    {   assert(PAL_IsPowerOfTwo(init->Capacity));
        PAL_ZeroMemory(mpmc_queue, sizeof(PAL_MPMC_QUEUE_U32));
        return -1;
    }
    if (init->MemoryStart == NULL)
    {   assert(init->MemoryStart != NULL);
        PAL_ZeroMemory(mpmc_queue, sizeof(PAL_MPMC_QUEUE_U32));
        return -1;
    }

    bytes_required = PAL_MPMCQueueQueryMemorySize_u32(init->Capacity);
    if (init->MemorySize < bytes_required)
    {   assert(init->MemorySize >= bytes_required);
        PAL_ZeroMemory(mpmc_queue, sizeof(PAL_MPMC_QUEUE_U32));
        return -1;
    }

    mpmc_queue->Storage     =(PAL_MPMC_CELL_U32*) init->MemoryStart;
    mpmc_queue->StorageMask =(pal_uint32_t      ) init->Capacity - 1;
    mpmc_queue->Capacity    =(pal_uint32_t      ) init->Capacity;
    mpmc_queue->MemoryStart =(void             *) init->MemoryStart;
    mpmc_queue->MemorySize  =(pal_uint64_t      ) init->MemorySize;
    mpmc_queue->EnqueuePos  = 0;
    mpmc_queue->DequeuePos  = 0;
    for (i = 0, n = init->Capacity; i < n; ++i)
    {   /* set the sequence number for each cell */
        mpmc_queue->Storage[i].Sequence = i;
    }
    return 0;
}

PAL_API(int)
PAL_MPMCQueuePush_u32
(
    struct PAL_MPMC_QUEUE_U32 *mpmc, 
    pal_uint32_t               item
)
{
    PAL_MPMC_CELL_U32 *cell;
    PAL_MPMC_CELL_U32 *stor = mpmc->Storage;      /* constant */
    pal_uint32_t       mask = mpmc->StorageMask;  /* constant */
    pal_uint32_t        pos = mpmc->EnqueuePos;
    pal_uint32_t        res;
    pal_uint32_t        seq;
    pal_sint64_t       diff;
    _ReadWriteBarrier();
    for ( ; ; )
    {
        cell = &stor[pos & mask];
        seq  = cell->Sequence;
        _ReadWriteBarrier();
        diff =(pal_sint64_t) seq - (pal_sint64_t) pos;
        if (diff == 0)
        {   /* the queue is not full, attempt to claim this slot */
            if ((res = (pal_uint32_t)_InterlockedCompareExchange((volatile LONG*)&mpmc->EnqueuePos, pos+1, pos)) == pos)
            {   /* the slot was successfully claimed */
                break;
            }
            else
            {   /* update pos and try again */
                pos = res;
            }
        }
        else if (diff < 0)
        {   /* the queue is full */
            return 0;
        }
        else
        {   /* another producer claimed this slot, try again */
            pos = mpmc->EnqueuePos;
            _ReadWriteBarrier();
        }
    }
    cell->Value    = item;
    _ReadWriteBarrier();
    cell->Sequence = pos + 1;
    return 1;
}

PAL_API(int)
PAL_MPMCQueueTake_u32
(
    struct PAL_MPMC_QUEUE_U32 *mpmc, 
    pal_uint32_t              *item
)
{
    PAL_MPMC_CELL_U32 *cell;
    PAL_MPMC_CELL_U32 *stor = mpmc->Storage;
    pal_uint32_t       mask = mpmc->StorageMask;
    pal_uint32_t        pos = mpmc->DequeuePos;
    pal_uint32_t        res;
    pal_uint32_t        seq;
    pal_sint64_t       diff;
    _ReadWriteBarrier();
    for ( ; ; )
    {
        cell = &stor[pos & mask];
        seq  = cell->Sequence;
        _ReadWriteBarrier();
        diff =(pal_sint64_t) seq - (pal_sint64_t)(pos + 1);
        if (diff == 0)
        {   /* the queue is not empty, attempt to claim this slot */
            if ((res = (pal_uint32_t)_InterlockedCompareExchange((volatile LONG*)&mpmc->DequeuePos, pos+1, pos)) == pos)
            {   /* the slot was successfully claimed */
                break;
            }
            else
            {   /* try again */
                pos = res;
            }
        }
        else if (diff < 0)
        {   /* the queue is empty */
            return 0;
        }
        else
        {   /* another consumer claimed this slot, try again */
            pos = mpmc->DequeuePos;
            _ReadWriteBarrier();
        }
    }
   *item = cell->Value;
    _ReadWriteBarrier();
    cell->Sequence = pos + mask + 1;
    return 1;
}

PAL_API(struct PAL_THREAD_POOL*)
PAL_ThreadPoolCreate
(
    struct PAL_THREAD_POOL_INIT *init
)
{
    HANDLE                      ready = NULL;
    HANDLE                      error = NULL;
    HANDLE                     launch = NULL;
    pal_uint8_t            *base_addr = NULL;
    pal_usize_t         required_size = 0;
    PAL_THREAD_POOL             *pool = NULL;
    PAL_MEMORY_ARENA            arena;
    PAL_MEMORY_ARENA_INIT  arena_init;
    PAL_WIN32_THREAD_INIT thread_init;
    pal_uint32_t                 i, n;
    pal_uint32_t        cleanup_count;
    HANDLE                wait_set[2];
    DWORD                     wait_rc = WAIT_OBJECT_0;
    DWORD                  error_code = ERROR_SUCCESS;

    cleanup_count  = 0;
    required_size  = PAL_ThreadPoolQueryMemorySize(init);
    if ((base_addr =(pal_uint8_t *) VirtualAlloc(NULL, required_size, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE)) == NULL) {
        error_code = GetLastError();
        goto cleanup_and_fail;
    }
    if ((ready  = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL) {
        error_code = GetLastError();
        goto cleanup_and_fail;
    }
    if ((error  = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL) {
        error_code = GetLastError();
        goto cleanup_and_fail;
    }
    if ((launch = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL) {
        error_code = GetLastError();
        goto cleanup_and_fail;
    }
    wait_set[0] = ready;
    wait_set[1] = error;

    /* ensure thread initialization functions are specified */
    for (i = 0, n = init->ThreadCount; i < n; ++i) {
        if (init->ThreadFuncs[i].Init == NULL) {
            init->ThreadFuncs[i].Init  = PAL_ThreadPool_DefaultThreadInit;
        }
        if (init->ThreadFuncs[i].Main == NULL) {
            error_code = ERROR_INVALID_FUNCTION;
            goto cleanup_and_fail;
        }
    }

    /* initialize a memory arena to sub-allocate from the main allocation */
    arena_init.AllocatorName = __FUNCTION__;
    arena_init.AllocatorType = PAL_MEMORY_ALLOCATOR_TYPE_HOST;
    arena_init.MemoryStart   =(pal_uint64_t) base_addr;
    arena_init.MemorySize    =(pal_uint64_t) required_size;
    arena_init.UserData      = NULL;
    arena_init.UserDataSize  = 0;
    if (PAL_MemoryArenaCreate(&arena, &arena_init) != 0) {
        error_code = ERROR_ARENA_TRASHED;
        goto cleanup_and_fail;
    }

    pool = PAL_MemoryArenaAllocateHostType(&arena, PAL_THREAD_POOL);
    pool->OsThreadIds     = PAL_MemoryArenaAllocateHostArray(&arena, unsigned int, init->ThreadCount);
    pool->OsThreadHandles = PAL_MemoryArenaAllocateHostArray(&arena, HANDLE      , init->ThreadCount);
    pool->ThreadContext   = PAL_MemoryArenaAllocateHostArray(&arena, void *      , init->ThreadCount);
    pool->PoolContext     = init->PoolContext;
    pool->LaunchSignal    = launch;
    pool->ShouldShutdown  = 0;
    pool->ThreadCount     = init->ThreadCount;
    for (i = 0, n = init->ThreadCount; i < n; ++i) {
        HANDLE     thread = NULL;
        unsigned int   id = 0;

        thread_init.ThreadPool           = pool;
        thread_init.UserCallbacks.Init   = init->ThreadFuncs[i].Init;
        thread_init.UserCallbacks.Main   = init->ThreadFuncs[i].Main;
        thread_init.PoolContext          = init->PoolContext;
        thread_init.ReadySignal          = ready;
        thread_init.ErrorSignal          = error;
        thread_init.LaunchSignal         = launch;
        thread_init.ThreadIndex          = i;
        thread_init.ThreadCount          = n;
        ResetEvent(ready); ResetEvent(error);
        if ((thread = (HANDLE)_beginthreadex(NULL, 0, PAL_ThreadPoolThreadEntry, &thread_init, 0, &id)) == NULL) {
            error_code = GetLastError();
            goto cleanup_and_fail;
        }
        pool->OsThreadIds    [cleanup_count] = id;
        pool->OsThreadHandles[cleanup_count] = thread;
        cleanup_count++;

        /* wait for initialization to complete */
        if ((wait_rc = WaitForMultipleObjects(PAL_CountOf(wait_set), wait_set, FALSE, INFINITE)) != WAIT_OBJECT_0) {
            GetExitCodeThread(thread, &error_code);
            goto cleanup_and_fail;
        }
    }
    CloseHandle(error);
    CloseHandle(ready);
    return pool;

cleanup_and_fail:
    if (launch) {
        if (pool && cleanup_count) {
            /* signal threads to terminate */
            pool->ShouldShutdown = 1;
            /* unblock any waiting threads */
            SetEvent(launch);
            /* wait for all threads to exit */
            WaitForMultipleObjects(cleanup_count, pool->OsThreadHandles, TRUE, INFINITE);
            /* close thread handles */
            for (i = 0; i < cleanup_count; ++i) {
                CloseHandle(pool->OsThreadHandles[i]);
            }
        }
        CloseHandle(launch);
    }
    if (error) {
        CloseHandle(error);
    }
    if (ready) {
        CloseHandle(ready);
    }
    if (base_addr != NULL) {
        VirtualFree(base_addr, 0, MEM_RELEASE);
    }
    return NULL;
}

PAL_API(pal_uint32_t)
PAL_ThreadPoolDelete
(
    struct PAL_THREAD_POOL *pool
)
{
    if (pool) {
        HANDLE    *thread_handles = pool->OsThreadHandles;
        pal_uint32_t thread_count = pool->ThreadCount;
        pal_uint32_t    exit_code = 0;
        pal_uint32_t            i;

        /* signal threads to terminate */
        pool->ShouldShutdown = 1;
        /* unblock any waiting threads */
        SetEvent(pool->LaunchSignal);
        /* wait for all threads to die */
        WaitForMultipleObjects(thread_count, thread_handles, TRUE, INFINITE);
        /* clean up thread handles */
        for (i = 0; i < thread_count; ++i) {
            DWORD thread_exit = 0;
            GetExitCodeThread(thread_handles[i], &thread_exit);
            CloseHandle(thread_handles[i]);
            if (thread_exit != 0 && exit_code == 0) {
                /* save the first non-zero exit code */
                exit_code = thread_exit;
            }
        }
        CloseHandle(pool->LaunchSignal);
        VirtualFree(pool, 0, MEM_RELEASE);
        return exit_code;
    } else {
        /* the pool handle is invalid */
        return 0;
    }
}

PAL_API(int)
PAL_ThreadPoolLaunch
(
    struct PAL_THREAD_POOL *pool
)
{
    SetEvent(pool->LaunchSignal);
    return 0;
}

PAL_API(void)
PAL_ThreadPoolSignalShutdown
(
    struct PAL_THREAD_POOL *pool
)
{
    pool->ShouldShutdown = 1;
    SetEvent(pool->LaunchSignal);
}

PAL_API(int)
PAL_ThreadPoolShouldShutdown
(
    struct PAL_THREAD_POOL *pool
)
{
    return pool->ShouldShutdown;
}

PAL_API(pal_uint32_t)
PAL_ThreadPoolGetThreadCount
(
    struct PAL_THREAD_POOL *pool
)
{
    return pool->ThreadCount;
}

PAL_API(void*)
PAL_ThreadPoolGetPoolContext
(
    struct PAL_THREAD_POOL *pool
)
{
    return pool->PoolContext;
}

PAL_API(void*)
PAL_ThreadPoolSetThreadContext
(
    struct PAL_THREAD_POOL *pool, 
    pal_uint32_t    thread_index, 
    void                *context
)
{
    void  *curr_value = pool->ThreadContext[thread_index];
    pool->ThreadContext[thread_index] = context;
    return curr_value;
}

PAL_API(void*)
PAL_ThreadPoolGetThreadContext
(
    struct PAL_THREAD_POOL *pool, 
    pal_uint32_t    thread_index
)
{
    return pool->ThreadContext[thread_index];
}

