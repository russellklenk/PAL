/**
 * @summary Implement the PAL task scheduler for the Win32 platform.
 */
#include "pal_win32_task.h"
#include "pal_win32_thread.h"
#include "pal_win32_memory.h"

/* @summary Compute the required size for a fully-committed PAL_TASK_POOL.
 * @param page_size The operating system page size, in bytes.
 * @return The number of bytes required to store the data for a PAL_TASK_POOL.
 */
static pal_usize_t
PAL_TaskPoolQueryMemorySize
(
    pal_usize_t page_size
)
{
    pal_usize_t required_size = 0;
    required_size  = PAL_AllocationSizeType (PAL_TASK_POOL);
    required_size += PAL_AllocationSizeArray(pal_uint64_t , PAL_TASKID_MAX_TASK_POOLS / 64);
    required_size  = PAL_AlignUp(required_size, page_size); /* user data */
    required_size += PAL_AllocationSizeArray(pal_uint16_t , PAL_TASKID_MAX_SLOTS_PER_POOL);
    required_size += PAL_AllocationSizeArray(PAL_TASKID   , PAL_TASKID_MAX_SLOTS_PER_POOL);
    required_size += PAL_AllocationSizeArray(PAL_TASK_DATA, PAL_TASKID_MAX_SLOTS_PER_POOL);
    required_size  = PAL_AlignUp(required_size, page_size); /* pools are page-aligned */
    return required_size;
}

/* @summary Compute the required memory reservation size for a PAL_TASK_SCHEDULER.
 * @param init The PAL_TASK_SCHEDULER_INIT defining the scheduler attributes.
 * @param page_size The operating system page size, in bytes.
 * @param main_type_count On return, the number of PAL_TASK_POOLs of required type ID PAL_TASK_POOL_TYPE_ID_MAIN are stored in this location.
 * @param aio_worker_type_count On return, the number of PAL_TASK_POOLs of required type ID PAL_TASK_POOL_TYPE_ID_AIO_WORKER are stored in this location.
 * @param cpu_worker_type_count On return, the number of PAL_TASK_POOLs of required type ID PAL_TASK_POOL_TYPE_ID_CPU_WORKER are stored in this location.
 * @return The number of bytes required to store the task scheduler data.
 */
static pal_usize_t
PAL_TaskSchedulerQueryMemorySize
(
    struct PAL_TASK_SCHEDULER_INIT *init, 
    pal_usize_t                page_size, 
    pal_uint32_t        *main_type_count, 
    pal_uint32_t  *aio_worker_type_count, 
    pal_uint32_t  *cpu_worker_type_count
)
{
    pal_usize_t required_size = 0;
    pal_usize_t     pool_size = 0;
    pal_uint32_t         i, n;

    PAL_Assign(main_type_count, 0);
    PAL_Assign(aio_worker_type_count, 0);
    PAL_Assign(cpu_worker_type_count, 0);
    
    required_size  = PAL_AllocationSizeType (PAL_TASK_SCHEDULER);
    /* ... */
    required_size  = PAL_AlignUp(required_size , page_size);
    pool_size      = PAL_TaskPoolQueryMemorySize(page_size);
    for (i = 0, n  = init->PoolTypeCount; i < n; ++i) {
        if (init->TaskPoolTypes[i].PoolTypeId == PAL_TASK_POOL_TYPE_ID_MAIN) {
            PAL_Assign(main_type_count, init->TaskPoolTypes[i].PoolCount);
        }
        if (init->TaskPoolTypes[i].PoolTypeId == PAL_TASK_POOL_TYPE_ID_AIO_WORKER) {
            PAL_Assign(aio_worker_type_count, init->TaskPoolTypes[i].PoolCount);
        }
        if (init->TaskPoolTypes[i].PoolTypeId == PAL_TASK_POOL_TYPE_ID_CPU_WORKER) {
            PAL_Assign(cpu_worker_type_count, init->TaskPoolTypes[i].PoolCount);
        }
        required_size += pool_size * init->TaskPoolTypes[i].PoolCount;
    }
    return required_size;
}

#if 0
PAL_API(struct PAL_TASK_SCHEDULER*)
PAL_TaskSchedulerCreate
(
    struct PAL_TASK_SCHEDULER_INIT *init
)
{
}

PAL_API(void)
PAL_TaskSchedulerDelete
(
    struct PAL_TASK_SCHEDULER *scheduler
)
{
}
#endif

