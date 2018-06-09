/**
 * @summary Define the platform-specific types and other internal bits for the 
 * Microsoft Windows Desktop platform.
 */
#ifndef __PAL_WIN32_MEMORY_H__
#define __PAL_WIN32_MEMORY_H__

#ifndef __PAL_MEMORY_H__
#include "pal_memory.h"
#endif

#ifndef PAL_NO_INCLUDES
#include <Windows.h>
#endif

/* @summary Define the data representing a pool of host memory allocations. Each pool can be accessed from a single thread only. 
 */
typedef struct PAL_HOST_MEMORY_POOL {
    char const                        *PoolName;               /* A nul-terminated string specifying the name of the pool. This value is used for debugging only. */
    struct PAL_HOST_MEMORY_ALLOCATION *FreeList;               /* A pointer to the first free _PAL_HOST_MEMORY_ALLOCATION node, or NULL of the free list is empty. */
    pal_uint32_t                       Capacity;               /* The maximum number of allocations that can be made from the pool. */
    pal_uint32_t                       OsPageSize;             /* The size of the OS virtual memory manager page, in bytes. */
    pal_uint32_t                       MinAllocationSize;      /* The minimum number of bytes that can be associated with any individual allocation. */
    pal_uint32_t                       MinCommitIncrease;      /* The minimum number of bytes that a memory commitment can increase by. */
    pal_uint64_t                       MaxTotalCommitment;     /* The maximum number of bytes of process address space that can be committed across all allocations in the pool. */
    pal_uint64_t                       PoolTotalCommitment;    /* The number of bytes currently committed across all active allocations in the pool */
    pal_uint32_t                       OsGranularity;          /* The allocation granularity (alignment) of allocations made through the OS virtual memory manager, in bytes. */
    struct PAL_HOST_MEMORY_ALLOCATION *NodeList;               /* An array of Capacity PAL_HOST_MEMORY_ALLOCATION nodes representing the preallocated node pool. */
} PAL_HOST_MEMORY_POOL;

/* @summary Define the data used to configure a host memory pool.
 */
typedef struct PAL_HOST_MEMORY_POOL_INIT {
    char const                        *PoolName;               /* A nul-terminated string specifying the name of the pool.  */
    pal_uint32_t                       PoolCapacity;           /* The maximum number of allocations that can be made from the pool. */
    pal_uint32_t                       MinAllocationSize;      /* The minimum number of bytes that can be associated with any individual allocation. */
    pal_uint32_t                       MinCommitIncrease;      /* The minimum number of bytes that a memory commitment can increase by. */
    pal_uint64_t                       MaxTotalCommitment;     /* The maximum number of bytes of process address space that can be committed across all allocations in the pool. */
} PAL_HOST_MEMORY_POOL_INIT;

/* @summary Define the data associated with a single host memory allocation. The allocation represents a single allocation from the host virtual memory system.
 */
typedef struct PAL_HOST_MEMORY_ALLOCATION {
    struct PAL_HOST_MEMORY_POOL       *SourcePool;             /* The host memory pool from which the allocation was made. */
    struct PAL_HOST_MEMORY_ALLOCATION *NextAllocation;         /* A field used by the host memory pool for free list management. The application may use this field for its own purposes for the lifetime of the allocation. */
    pal_uint8_t                       *BaseAddress;            /* The address of the first accessible (committed) byte of the allocation. */
    pal_uint64_t                       BytesReserved;          /* The number of bytes of process address space reserved for the allocation. */
    pal_uint64_t                       BytesCommitted;         /* The number of bytes of process address space committed to the allocation. */
    pal_uint32_t                       AllocationFlags;        /* One or more values from the PAL_HOST_MEMORY_ALLOCATION_FLAGS enumeration. */
    pal_uint32_t                       NextFreeOffset;         /* The byte offset of the next available sub-region, for allocations <= 4GB. */
} PAL_HOST_MEMORY_ALLOCATION;

/* @summary Define the data returned from a memory allocation request.
 */
typedef struct PAL_MEMORY_BLOCK {
    pal_uint64_t                       SizeInBytes;            /* The size of the memory block, in bytes. */
    pal_uint64_t                       BlockOffset;            /* The allocation offset. This field is set for host and device allocations. */
    void                              *HostAddress;            /* The host-visible memory address. This field is set for host allocations only. */
    pal_uint32_t                       AllocatorType;          /* One of PAL_MEMORY_ALLOCATOR_TYPE indicating whether the block is a host or device allocation. */
    pal_uint32_t                       AllocationTag;          /* Reserved for future use. Set to Zero. */
} PAL_MEMORY_BLOCK;

/* @summary Define the data returned from a memory allocator index size query.
 */
typedef struct PAL_MEMORY_INDEX_SIZE {
    pal_uint64_t                       SplitIndexSize;         /* The size of the split index data, in bytes. This value is always an even multiple of the machine word size. */
    pal_uint64_t                       StatusIndexSize;        /* The size of the status index data, in bytes. This value is always an even multiple of the machine word size. */
    pal_uint64_t                       TotalIndexSize;         /* The total required size for all index data, in bytes. This value is always an even multiple of the machine word size. */
    pal_uint32_t                       MinBitIndex;            /* The zero-based index of the set bit corresponding to the minimum allocation size. */
    pal_uint32_t                       MaxBitIndex;            /* The zero-based index of the set bit corresponding to the maximum allocation size. */
    pal_uint32_t                       LevelCount;             /* The number of powers of two between the minimum and maximum allocation size. */
} PAL_MEMORY_INDEX_SIZE;

/* @summary Define the data returned from a memory allocator free block query. */
typedef struct PAL_MEMORY_BLOCK_INFO {
    pal_uint64_t                       BlockOffset;            /* The byte offset of the start of the block from the start of the managed memory range. */
    pal_uint32_t                       LevelShift;             /* The zero-based index of the set bit for the level. BlockSize = 1 << LevelShift, RelativeIndex = BlockOffset >> LevelShift. */
    pal_uint32_t                       BlockIndex;             /* The absolute block index. The relative index can be computed as BlockOffset / BlockSize. */
} PAL_MEMORY_BLOCK_INFO;

/* @summary Define the data associated with a memory arena allocator.
 * An arena allocator can manage any amount of memory, but supports only allocation and freeing back to a marked point in time.
 * PAL implementations can allocate a memory block using the native platform allocator, and then sub-allocate using an arena allocator.
 */
typedef struct PAL_MEMORY_ARENA {
#define NUSER                          PAL_MEMORY_ALLOCATOR_MAX_USER
    char const                        *AllocatorName;          /* A nul-terminated string specifying the name of the allocator. Used for debugging purposes only. */
    pal_uint32_t                       AllocatorType;          /* One of PAL_MEMORY_ALLOCATOR_TYPE indicating whether this is a host or device memory allocator. */
    pal_uint32_t                       AllocatorTag;           /* A tag value that gets assigned to each memory allocation. */
    pal_uint64_t                       MemoryStart;            /* The address or offset of the start of the memory block from which sub-allocations are returned. */
    pal_uint64_t                       MemorySize;             /* The size of the memory block from which sub-allocations are returned, in bytes. */
    pal_uint64_t                       NextOffset;             /* The byte offset, relative to the start of the associated memory range, of the next free byte. */
    pal_uint64_t                       MaximumOffset;          /* The maximum value of NextOffset. NextOffset is always in [0, MaximumOffset]. */
    pal_uint64_t                       Reserved1;              /* Reserved for future use. Set to zero. */
    pal_uint64_t                       Reserved2;              /* Reserved for future use. Set to zero. */
    pal_uint8_t                        UserData[NUSER];        /* Extra storage for data the user wants to associate with the allocator instance. */
#undef  NUSER
} PAL_MEMORY_ARENA;

/* @summary Define the data used to configure a memory arena allocator when it is initialized.
 * Use the PAL_MemoryArenaAllocatorInit or PAL_MemoryArenaAllocatorInitNamed macros for easy static initialization of this structure for simple host memory allocators.
 * PAL_MEMORY_ARENA_INIT arena_init = PAL_MemoryArenaAllocatorInit(mem, memsize);
 */
typedef struct PAL_MEMORY_ARENA_INIT {
    char const                        *AllocatorName;          /* A nul-terminated string specifying the name of the allocator. Used for debugging purposes only. */
    pal_uint32_t                       AllocatorType;          /* One of PAL_MEMORY_ALLOCATOR_TYPE indicating whether this is a host or device memory allocator. */
    pal_uint64_t                       MemoryStart;            /* The address or offset of the start of the memory block from which sub-allocations are returned. */
    pal_uint64_t                       MemorySize;             /* The size of the memory block from which sub-allocations are returned, in bytes. */
    void                              *UserData;               /* The user data to be copied into the allocator instance. */
    pal_uint64_t                       UserDataSize;           /* The number of bytes of user data to copy into the allocator instance. */
} PAL_MEMORY_ARENA_INIT;

/* @summary Define the data associated with a memory arena marker, which represents the state of an arena allocator at a given point in time.
 * The arena can be reset back to a marker, which invalidates all allocations made since the marked point in a single operation.
 */
typedef struct PAL_MEMORY_ARENA_MARKER {
    PAL_MEMORY_ARENA                  *Arena;                  /* The PAL_MEMORY_ARENA from which the marker was obtained. */
    pal_uint64_t                       Offset;                 /* The value of PAL_MEMORY_ARENA::NextOffset at the time the marker was obtained. */
} PAL_MEMORY_ARENA_MARKER;

/* @summary Define the data pre-computed for a single level in the memory allocator. 
 * Each level is a power of two in size, and maintains equally-sized blocks half the size of the level above it.
 */
typedef struct PAL_MEMORY_ALLOCATOR_LEVEL {
    pal_uint64_t                       BlockSize;              /* The size of the blocks at this level, in bytes. */
    pal_uint32_t                       BlockCount;             /* The number of blocks at this level. */
    pal_uint32_t                       LevelBit;               /* The zero-based index of the bit that is set for blocks on this level. */
    pal_uint32_t                       FirstBlockIndex;        /* The zero-based index of the first block in this level. */
    pal_uint32_t                       FinalBlockIndex;        /* The zero-based index of the final block in this level. */
    pal_uint32_t                       WordIndex0;             /* The zero-based index of the word containing the first bit in the split and status index for this level. */
    pal_uint32_t                       WordIndexN;             /* The zero-based index of the word containing the final bit in the split and status index for this level. */
    pal_uint64_t                       WordMask0;              /* The mask value used to search only bits belonging to this level in the first word of the split and status index. */
    pal_uint64_t                       WordMaskN;              /* The mask value used to search only bits belonging to this level in the final word of the split and status index. */
} PAL_MEMORY_ALLOCATOR_LEVEL;

/* @summary Define the data associated with a general-purpose memory allocator based on a power-of-two allocation scheme.
 * PAL_MEMORY_ALLOCATOR_MAX_LEVELS defines the maximum number of power-of-two steps between the minimum and maximum size.
 * If aligned allocations will be required, the minimum block size should be set to the required alignment.
 * The PAL_MEMORY_ALLOCATOR supports general-purpose allocation operations such as malloc, realloc and free.
 * See http://bitsquid.blogspot.com/2015/08/allocation-adventures-3-buddy-allocator.html
 */
typedef struct PAL_MEMORY_ALLOCATOR {
    #define NLVL                       PAL_MEMORY_ALLOCATOR_MAX_LEVELS
    #define NUSER                      PAL_MEMORY_ALLOCATOR_MAX_USER
    char const                        *AllocatorName;          /* A nul-terminated string specifying the name of the allocator. Used for debugging purposes only. */
    pal_uint32_t                       AllocatorType;          /* One of PAL_MEMORY_ALLOCATOR_TYPE indicating whether this is a host or device memory allocator. */
    pal_uint32_t                       LevelCount;             /* The total number of levels used by the allocator, with level 0 representing the largest level. */
    pal_uint64_t                       MemoryStart;            /* The address or offset of the start of the memory block from which sub-allocations are returned. */
    pal_uint64_t                       MemorySize;             /* The size of the memory block from which sub-allocations are returned, in bytes. */
    pal_uint64_t                       AllocationSizeMin;      /* The size of the smallest memory block that can be returned by the buddy allocator, in bytes. */
    pal_uint64_t                       AllocationSizeMax;      /* The size of the largest memory block that can be returned by the buddy allocator, in bytes. */
    pal_uint64_t                       BytesReserved;          /* The number of bytes marked as reserved. These bytes can never be allocated to the application. */
    pal_uint64_t                      *SplitIndex;             /* An array of 1 << (LevelCount-1) bits with each bit set if the block at bit index i has been split. */
    pal_uint64_t                      *StatusIndex;            /* An array of 1 << (LevelCount) bits with each bit set if the block at bit index i is available. */
    pal_uint32_t                       FreeCount[NLVL];        /* The number of entries in the free list for each level. LevelCount entries are valid. */
    PAL_MEMORY_ALLOCATOR_LEVEL         LevelInfo[NLVL];        /* Precomputed information about each level. LevelCount entries are valid. */
    void                              *StateData;              /* The caller-allocated memory to be used for storing allocator state data. */
    pal_uint64_t                       StateDataSize;          /* The number of bytes of state data available for use by the allocator instance. */
    pal_uint64_t                       SplitIndexSize;         /* The size of the split index data, in bytes */
    pal_uint64_t                       StatusIndexSize;        /* The size of the status index data, in bytes */
    pal_uint8_t                        UserData[NUSER];        /* Extra storage for data the user wants to associate with the allocator instance. */
    #undef  NLVL
    #undef  NUSER
} PAL_MEMORY_ALLOCATOR;

/* @summary Define the data used to configure a general-purpose memory allocator when it is initialized.
 */
typedef struct PAL_MEMORY_ALLOCATOR_INIT {
    char const                        *AllocatorName;          /* A nul-terminated string specifying the name of the allocator. Used for debugging purposes only. */
    pal_uint32_t                       AllocatorType;          /* One of PAL_MEMORY_ALLOCATOR_TYPE indicating whether this is a host or device memory allocator. */
    pal_uint64_t                       AllocationSizeMin;      /* The size of the smallest memory block that can be returned from the allocator, in bytes. */
    pal_uint64_t                       AllocationSizeMax;      /* The size of the largest memory block that can be returned from the allocator, in bytes. */
    pal_uint64_t                       BytesReserved;          /* The number of bytes marked as reserved. These bytes can never be allocated to the application. */
    pal_uint64_t                       MemoryStart;            /* The address or offset of the start of the memory block from which sub-allocations are returned. */
    pal_uint64_t                       MemorySize;             /* The size of the memory block from which sub-allocations are returned, in bytes. */
    void                              *StateData;              /* The caller-allocated memory to be used for storing allocator state data. This value must be non-NULL. */
    pal_uint64_t                       StateDataSize;          /* The number of bytes of state data available for use by the allocator instance. This value must be at least the size returned by PAL_MemoryAllocatorQueryHostMemorySize. */
    void                              *UserData;               /* The user data to be copied into the allocator instance. */
    pal_uint64_t                       UserDataSize;           /* The number of bytes of user data to copy into the allocator instance. */
} PAL_MEMORY_ALLOCATOR_INIT;

/* @summary Define a stream-oriented view into a block of memory.
 */
typedef struct PAL_MEMORY_VIEW {
    pal_uint32_t                       StreamCount;            /* The number of data streams defined in the layout. */
    pal_uint32_t                       ElementCount;           /* The maximum number of valid data elements in the memory block. */
    pal_uint8_t                       *Stream[8];              /* Pointers to the properly aligned start of each stream in the memory block. */
    pal_uint32_t                       Stride[8];              /* The stride between individual elements in each data stream, in bytes. */
} PAL_MEMORY_VIEW;

/* @summary Define the layout of a memory block containing one or more data streams.
 * Each data stream contains the same number of elements of the same type, tightly packed into a contiguous array:
 * LOW......(memory address).......HIGH  LOW  (memory address)
 * STREAM1: TYPEA|TYPEA|TYPEA|...|TYPEA   .
 * STREAM2: TYPEB|TYPEB|TYPEB|...|TYPEB   .
 * ...                                   ...
 * STREAMN: TYPEx|TYPEx|TYPEx|...|TYPEx  HIGH (memory address)
 * This data setup is useful for processing data using SIMD instructions.
 * The PAL_MEMORY_LAYOUT is used to initialize a PAL_MEMORY_VIEW into a particular memory block.
 * Use PAL_MemoryLayoutInit to initialize the PAL_MEMORY_LAYOUT, followed by one or more calls to PAL_MemoryLayoutAdd to define the data streams.
 */
typedef struct PAL_MEMORY_LAYOUT {
    pal_uint32_t                       StreamCount;            /* The number of data streams defined in the layout. */
    pal_uint32_t                       StreamSize [8];         /* The size of each data element, in bytes, for each stream. Use PAL_SizeOf(type) to set these values. */
    pal_uint32_t                       StreamAlign[8];         /* The required alignment of each data element, in bytes, for each stream. Use PAL_AlignOf(type) to set these values. */
} PAL_MEMORY_LAYOUT;

/* @summary Define the data associated with a dynamic buffer.
 */
typedef struct PAL_DYNAMIC_BUFFER {
    pal_uint8_t                       *BaseAddress;            /* The base address of the memory reservation. */
    pal_uint8_t                       *EndAddress;             /* The address of the one-past last element in the buffer. */
    pal_usize_t                        ElementCount;           /* The number of elements that are currently stored in the buffer. */
    pal_usize_t                        ElementCapacity;        /* The number of elements that can be stored in the buffer without increasing the commitment. */
    pal_usize_t                        ElementCountMax;        /* The maximum number of elements that can be stored in the current memory reservation. */
    pal_uint32_t                       ElementCountGrow;       /* The minimum number of elements that the buffer will grow to accomodate during a PAL_DynamicBufferEnsure operation. */
    pal_uint32_t                       ElementAlignment;       /* The required alignment of an individual element, in bytes. */
    pal_uint32_t                       ElementBaseSize;        /* The size of a single element, in bytes. */
    pal_uint32_t                       ElementStride;          /* The number of bytes between two consecutive elements in the buffer. */
    pal_uint32_t                       OsPageSize;             /* The size of the OS virtual memory manager page, in bytes. */
    pal_uint32_t                       OsGranularity;          /* The allocation granularity (alignment) of allocations made through the OS virtual memory manager, in bytes. */
} PAL_DYNAMIC_BUFFER;

/* @summary Define the data used to configure a dynamic buffer.
 */
typedef struct PAL_DYNAMIC_BUFFER_INIT {
    pal_uint32_t                       ElementSize;            /* The size of a single element, in bytes. Use PAL_SizeOf(type) to retrieve the size of the type. */
    pal_uint32_t                       ElementAlign;           /* The required alignment of individual elements, in bytes. Use PAL_AlignOf(type) to retrieve the necessary alignment. */
    pal_uint32_t                       InitialCommitment;      /* The number of bytes to commit when the buffer is first created. Use PAL_AllocationSizeArray(type, count) to set this field. */
    pal_uint32_t                       MinCommitIncrease;      /* The minimum number of bytes that the memory commitment can increase by during a single PAL_DynamicBufferEnsure operation. */
    pal_uint64_t                       MaxTotalCommitment;     /* The maximum number of bytes of process address space that can be committed for the buffer. This specifies the allocation reservation size. */
} PAL_DYNAMIC_BUFFER_INIT;

/* @summary Define the data associated with a handle state value.
 * The handle state is a sparse array. The handle value stores the index into this array, which is in turn used to look up the index in the dense array.
 */
typedef struct PAL_HANDLE_STATE_BITS {
    pal_uint32_t                       Generation :  4;        /* The generation value, used to detect expired handles that reuse the same slot. */
    pal_uint32_t                       DenseIndex : 10;        /* The zero-based index of the item in the dense storage array. */
    pal_uint32_t                       Unused     : 17;        /* These bits are currently unused. */
    pal_uint32_t                       Live       :  1;        /* This bit is set if the entry is associated with a valid handle. */
} PAL_HANDLE_STATE_BITS;

/* @summary Define the data associated with a handle value.
 * The handle array (and any associated data) is stored tightly-packed. The unused portion of the array maintains a list of available indices in the sparse state array.
 */
typedef struct PAL_HANDLE_VALUE_BITS {
    pal_uint32_t                       Generation :  4;        /* The generation value, used to detect expired handles that reuse the same slot. */
    pal_uint32_t                       Namespace  :  7;        /* An application-defined value used to specify a namespace or type. */
    pal_uint32_t                       StateIndex : 10;        /* The zero-based index of the item in the sparse state array. */
    pal_uint32_t                       ChunkIndex : 10;        /* The zero-based index of the chunk that stores the item data. */
    pal_uint32_t                       Valid      :  1;        /* This bit is clear for invalid handle values. */
} PAL_HANDLE_VALUE_BITS;

/* @summary Define the data associated with a table of "objects" identified externally using a 32-bit integer ID.
 * The object can exist in name only (i.e. it is only a handle) or it can have some data stored in the table directly.
 * The table itself is broken up into independent "chunks", which allows the table to grow as-needed up to the maximum size.
 * A chunk is independent of all other chunks. The PAL_HANDLE and data within a chunk are densely-packed, allowing for efficient iteration over the items within a chunk.
 * Access to the table must be externally synchronized.
 */
typedef struct PAL_HANDLE_TABLE {
    pal_uint8_t                       *BaseAddress;            /* The address of the memory block returned by the virtual memory manager. */
    pal_uint64_t                      *ChunkCommit;            /* A bitvector of 1024 bits (128 bytes) where a set bit at index N (0 <= N < 1024) indicates that chunk N is committed with backing physical memory. */
    pal_uint64_t                      *ChunkStatus;            /* A bitvector of 1024 bits (128 bytes) where a set bit at index N (0 <= N < 1024) indicates that chunk N has at least one available slot. */
    pal_uint16_t                      *ChunkCounts;            /* An array of 1024 16-bit integers storing the number of used slots for each chunk. */
    pal_uint32_t                      *ChunkInit;              /* An array of 1024 32-bit integer values used to initialize a chunk free list. */
    pal_uint32_t                       ChunkSize;              /* The size of a single committed chunk, in bytes. */
    pal_uint32_t                       DataSize;               /* The size of a single data element, in bytes. */
    pal_uint32_t                       CommitCount;            /* The number of currently committed chunks. */
    pal_uint32_t                       Namespace;              /* The namespace or "object type" value for handles allocated by the table. */
    pal_uint32_t                       TableFlags;             /* One or more values of the PAL_HANDLE_TABLE_FLAGS enumeration. */
    pal_uint32_t                       OsPageSize;             /* The size of the OS virtual memory manager page, in bytes. */
    PAL_MEMORY_LAYOUT                  DataLayout;             /* The layout of the data elements stored within the table. */
} PAL_HANDLE_TABLE;

/* @summary Define the data used to configure a PAL_HANDLE_TABLE.
 */
typedef struct PAL_HANDLE_TABLE_INIT {
    pal_uint32_t                       Namespace;              /* The namespace or "object type" value for handles allocated by the table. */
    pal_uint32_t                       InitialCommit;          /* The number of objects to initially allocate memory for. If Flags specifies PAL_HANDLE_TABLE_FLAG_PREALLOCATE, TableCapacity is committed up-front. */
    pal_uint32_t                       TableFlags;             /* One or more bitwise-ORd values of the PAL_HANDLE_TABLE_FLAGS enumeration. */
    struct PAL_MEMORY_LAYOUT          *DataLayout;             /* Pointer to a PAL_MEMORY_LAYOUT specifying the layout for data items stored within the table. Set to NULL if the table does not store any data internally. */
} PAL_HANDLE_TABLE_INIT;

/* @summary Define the data describing an independent chunk of objects within a handle table.
 * The actual in-memory layout is different from the layout described by this structure, and the chunk object data (pointed to by the Data field) is treated as opaque.
 */
typedef struct PAL_HANDLE_TABLE_CHUNK {
    PAL_HANDLE                        *IdList;                 /* A pointer to the start of the 1024-element (Count are in use) handle array for the chunk. */
    pal_uint8_t                       *Data;                   /* A pointer to the start of the data array for the chunk, or NULL of the table does not contain internal data. */
    pal_uint32_t                       Index;                  /* The zero-based index of the chunk within the table. This is always in [0, 1024). */
    pal_uint32_t                       Count;                  /* The number of valid items in the chunk. This is always in [0, 1024]. */
} PAL_HANDLE_TABLE_CHUNK;

/* @summary Define the data used to configure an operation that invoked a caller-supplied function for each non-empty chunk in a PAL_HANDLE_TABLE.
 */
typedef struct PAL_HANDLE_TABLE_VISITOR_INIT {
    PAL_HandleTableChunkVisitor_Func   Callback;               /* The callback function to invoke for each non-empty chunk in the table. */
    pal_uintptr_t                      Context;                /* Opaque data to be passed through to each invocation of the Callback. */
    pal_uint32_t                       Flags;                  /* One or more bitwise OR'd values of the PAL_HANDLE_TABLE_VISITOR_FLAGS enumeration. */
} PAL_HANDLE_TABLE_VISITOR_INIT;

#endif /* __PAL_WIN32_MEMORY_H__ */
