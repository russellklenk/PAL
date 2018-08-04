/**
 * @summary Implement the PAL entry points from pal_string.h.
 */
#include "pal_win32_string.h"
#include "pal_win32_memory.h"

/* @summary Define the data associated with the memory requirements for a string table.
 */
typedef struct PAL_STRING_TABLE_SIZE_INFO {
    pal_uint32_t                 BucketCount;                  /* The number of hash buckets required for the hash table. */
    pal_uint32_t                 DataCommitSize;               /* The number of bytes to commit for the string data storage block. */
    pal_uint32_t                 DataReserveSize;              /* The number of bytes to reserve for the string data storage block. */
    pal_uint32_t                 HashCommitSize;               /* The number of bytes to commit for the hash chunk storage block. */
    pal_uint32_t                 HashReserveSize;              /* The number of bytes to reserve for the hash chunk storage block. */
    pal_uint32_t                 TableCommitSize;              /* The number of bytes to commit from the main table storage block. */
    pal_uint32_t                 TableReserveSize;             /* The number of bytes to reserve for the main table storage block. */
    pal_uint32_t                 HashCommitCount;              /* The number of PAL_STRING_HASH_CHUNK records committed in the initial allocation. */
    pal_uint32_t                 HashReserveCount;             /* The number of PAL_STRING_HASH_CHUNK records reserved in the initial allocation. */
    pal_uint32_t                 StringCommitCount;            /* The number of PAL_STRING_DATA_ENTRY records committed in the initial allocation. */
    pal_uint32_t                 StringReserveCount;           /* The number of PAL_STRING_DATA_ENTRY records reserved in the initial allocation. */
} PAL_STRING_TABLE_SIZE_INFO;

/* @summary Calculate the next power-of-two value greater than or equal to a given value.
 * @param n The input value.
 * @return The next power-of-two value greater than or equal to n.
 */
static pal_uint32_t
PAL_StringNextPow2GreaterOrEqual
(
    pal_uint32_t n
)
{
    pal_uint32_t i, k;
    --n;
    for (i = 1, k = sizeof(pal_uint32_t) * 8; i < k; i <<= 1)
    {
        n |= n >> i;
    }
    return n+1;
}

/* @summary Compute the length of a UTF-8 encoded, nul-terminated string.
 * @param str The string.
 * @param cb On return, this location stores the string length, including the nul, in bytes.
 * @param cc On return, this location stores the string length, not including the nul, in characters.
 */
static void
PAL_StrlenUtf8
(
    void const  *str, 
    pal_uint32_t *cb, 
    pal_uint32_t *cc
)
{
    pal_uint8_t const *s =(pal_uint8_t const*) str;
    pal_uint32_t      nb = 0;
    pal_uint32_t      nc = 0;
    pal_uint8_t       cp;

    if (str != NULL) {
        while ((cp = *s++)) {
            if((cp & 0xC0) != 0x80) {
                nc++;
            }
            nb++;
        }
        PAL_Assign(cb, nb+1);
        PAL_Assign(cc, nc);
    } else {
        PAL_Assign(cb, 1);
        PAL_Assign(cc, 0);
    }
}

/* @summary Compute the length of a UTF-16 encoded, nul-terminated string.
 * @param str The string.
 * @param cb On return, this location stores the string length, including the nul, in bytes.
 * @param cc On return, this location stores the string length, not including the nul, in characters.
 */
static void
PAL_StrlenUtf16
(
    void const  *str, 
    pal_uint32_t *cb, 
    pal_uint32_t *cc
)
{
    pal_uint16_t const *s =(pal_uint16_t const*) str;
    pal_uint32_t       nb = 0;
    pal_uint32_t       nc = 0;
    pal_uint16_t       cp;

    if (str != NULL) {
        while ((cp = *s++)) {
            nb += 2; nc++;
            if (cp >= 0xD800 && cp <= 0xDBFF) { 
                cp  =*s;
                if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    nb += 2;
                    s++;
                }
            }
        }
        PAL_Assign(cb, nb+2);
        PAL_Assign(cc, nc);
    } else {
        PAL_Assign(cb, 2);
        PAL_Assign(cc, 0);
    }
}

/* @summary Compute the length of a UTF-32 encoded, nul-terminated string.
 * @param str The string.
 * @param cb On return, this location stores the string length, including the nul, in bytes.
 * @param cc On return, this location stores the string length, not including the nul, in characters.
 */
static void
PAL_StrlenUtf32
(
    void const  *str, 
    pal_uint32_t *cb, 
    pal_uint32_t *cc
)
{
    pal_uint32_t const *s =(pal_uint32_t const*) str;
    pal_uint32_t       nb = 0;
    pal_uint32_t       nc = 0;
    pal_uint32_t       cp;

    if (str != NULL) {
        while ((cp = *s++)) {
            nb += 4; nc++;
        }
        PAL_Assign(cb, nb+4);
        PAL_Assign(cc, nc);
    } else {
        PAL_Assign(cb, 4);
        PAL_Assign(cc, 0);
    }
}

/* @summary Determine the memory allocation attributes for a given string table configuration.
 * @param size On return, this structure stores the memory allocation attributes for the string table.
 * @param init Configuration data specifying the attributes of the string table.
 */
static void
PAL_StringTableQueryMemorySize
(
    struct PAL_STRING_TABLE_SIZE_INFO *size, 
    struct PAL_STRING_TABLE_INIT      *init
)
{
    pal_uint32_t   num_buckets_base =(init->MaxStringCount + (PAL_STRING_HASH_CHUNK_CAPACITY-1)) / PAL_STRING_HASH_CHUNK_CAPACITY;
    pal_uint32_t   num_buckets_pow2 = PAL_StringNextPow2GreaterOrEqual(num_buckets_base);
    pal_uint32_t   data_size_commit = 0;
    pal_uint32_t  data_size_reserve = 0;
    pal_uint32_t   hash_size_commit = 0;
    pal_uint32_t  hash_size_reserve = 0;
    pal_uint32_t  table_size_commit = 0;
    pal_uint32_t table_size_reserve = 0;
    pal_uint32_t      commit_chunks = 0;
    pal_uint32_t     reserve_chunks = 0;
    pal_uint32_t     commit_entries = 0;
    pal_uint32_t    reserve_entries = 0;
    pal_uint32_t       entry_offset = 0;
    SYSTEM_INFO             sysinfo;

    /* retrieve operating system page size and allocation granularity */
    GetNativeSystemInfo(&sysinfo);

    /* calculate sizes and offsets for the main table storage block */
    table_size_commit  += PAL_AllocationSizeType (PAL_STRING_TABLE);
    table_size_commit  += PAL_AllocationSizeArray(PAL_STRING_HASH_CHUNK*, num_buckets_pow2);
    entry_offset        = table_size_commit;
    table_size_reserve  = table_size_commit;
    table_size_commit  += PAL_AllocationSizeArray(PAL_STRING_DATA_ENTRY , init->InitialCapacity);
    table_size_reserve += PAL_AllocationSizeArray(PAL_STRING_DATA_ENTRY , init->MaxStringCount);
    table_size_commit   = PAL_AlignUp(table_size_commit , sysinfo.dwPageSize);
    table_size_reserve  = PAL_AlignUp(table_size_reserve, sysinfo.dwPageSize);
    commit_entries      =(table_size_commit  - entry_offset) / sizeof(PAL_STRING_DATA_ENTRY);
    reserve_entries     =(table_size_reserve - entry_offset) / sizeof(PAL_STRING_DATA_ENTRY);
    
    /* calculate the reservation and initial commit size for the hash block */
    hash_size_commit  = PAL_AllocationSizeArray(PAL_STRING_HASH_CHUNK, num_buckets_pow2);
    hash_size_reserve = PAL_AllocationSizeArray(PAL_STRING_HASH_CHUNK, num_buckets_pow2*2);
    hash_size_commit  = PAL_AlignUp(hash_size_commit , sysinfo.dwPageSize);
    hash_size_reserve = PAL_AlignUp(hash_size_reserve, sysinfo.dwPageSize);
    commit_chunks     = hash_size_commit  / sizeof(PAL_STRING_HASH_CHUNK);
    reserve_chunks    = hash_size_reserve / sizeof(PAL_STRING_HASH_CHUNK);

    /* calculate the reservation and initial commit size for the data block */
    data_size_commit  = PAL_AlignUp(init->DataCommitSize, sysinfo.dwPageSize);
    data_size_reserve = PAL_AlignUp(init->MaxDataSize   , sysinfo.dwPageSize);

    size->BucketCount        = num_buckets_pow2;
    size->DataCommitSize     = data_size_commit;
    size->DataReserveSize    = data_size_reserve;
    size->HashCommitSize     = hash_size_commit;
    size->HashReserveSize    = hash_size_reserve;
    size->TableCommitSize    = table_size_commit;
    size->TableReserveSize   = table_size_reserve;
    size->HashCommitCount    = commit_chunks;
    size->HashReserveCount   = reserve_chunks;
    size->StringCommitCount  = commit_entries;
    size->StringReserveCount = reserve_entries;
}

PAL_API(pal_usize_t)
PAL_NativeStringLengthBytes
(
    pal_char_t const *str
)
{
    pal_usize_t len = 0;

    if (str == NULL) {
        return 0;
    }
    if (SUCCEEDED(StringCbLengthW(str, STRSAFE_MAX_CCH * sizeof(pal_char_t), &len))) {
        return (len + sizeof(pal_char_t));
    }
    return 0;
}

PAL_API(pal_usize_t)
PAL_NativeStringLengthChars
(
    pal_char_t const *str
)
{
    pal_usize_t len = 0;

    if (str == NULL) {
        return 0;
    }
    if (SUCCEEDED(StringCchLengthW(str, STRSAFE_MAX_CCH, &len))) {
        return (len);
    }
    return 0;
}

PAL_API(int)
PAL_NativeStringCompareCs
(
    pal_char_t const *str1, 
    pal_char_t const *str2
)
{
    if (str1 == str2) {
        return 0;
    }
    /* lstrcmpW is exported by KERNEL32.DLL - uses CompareStringEx */
    return lstrcmpW(str1, str2);
}

PAL_API(int)
PAL_NativeStringCompareCi
(
    pal_char_t const *str1, 
    pal_char_t const *str2
)
{
    if (str1 == str2) {
        return 0;
    }
    /* lstrcmpiW is exported by KERNEL32.DLL - uses CompareStringEx */
    return lstrcmpiW(str1, str2);
}

PAL_API(int)
PAL_StringConvertUtf8ToNative
(
    pal_utf8_t const *utf8_str, 
    pal_char_t     *native_buf, 
    pal_usize_t     native_max, 
    pal_usize_t    *native_len
)
{
    pal_char_t *output = (pal_char_t*) native_buf;
    int         outcch = (int        )(native_max / sizeof(pal_char_t));
    int         nchars = 0;

    if (utf8_str == NULL) {
        if (output && native_max >= sizeof(pal_char_t))
            *output = L'\0';
        *native_len = sizeof(pal_char_t);
        return 0;
    }
    if ((nchars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_str, -1, output, outcch)) == 0) {
        if (output && native_max >= sizeof(pal_char_t))
            *output = L'\0';
        *native_len = sizeof(pal_char_t);
        return -1;
    }
    *native_len = (pal_usize_t)(nchars * sizeof(pal_char_t));
    return 0;
}

PAL_API(int)
PAL_StringConvertNativeToUtf8
(
    pal_char_t const *native_str, 
    pal_utf8_t         *utf8_buf, 
    pal_usize_t         utf8_max, 
    pal_usize_t        *utf8_len
)
{
    pal_char_t const *native = (pal_char_t const*) native_str;
    pal_utf8_t       *output = (pal_utf8_t      *) utf8_buf;
    int                outcb = (int              )(utf8_max / sizeof(pal_utf8_t));
    int               nbytes = 0;

    if (native_str == NULL) {
        if (output && utf8_max >= sizeof(pal_utf8_t))
            *output = '\0';
        *utf8_len = sizeof(pal_utf8_t);
        return 0;
    }
    if ((nbytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, native, -1, output, outcb, NULL, NULL)) == 0) {
        if (output && utf8_max >= sizeof(pal_utf8_t))
            *output = '\0';
        *utf8_len = sizeof(pal_utf8_t);
        return -1;
    }
    *utf8_len = (pal_usize_t) nbytes;
    return 0;
}

PAL_API(pal_uint32_t)
PAL_StringHash32_Utf8
(
    pal_utf8_t const *str, 
    pal_uint32_t   *len_b, 
    pal_uint32_t   *len_c
)
{   /* FNV1 with MurmurHash3 finalizer */
    pal_uint8_t const    *s =(pal_uint8_t const*) str;
    pal_uint32_t        h32 = 2166136261U;
    pal_uint32_t         cc = 0;
    pal_uint32_t         cb = 0;
    pal_uint8_t          cp;
    while ((cp = *s++) != 0) {
        h32 =(16777619U * h32) + cp;
        cb += sizeof(pal_uint8_t);
        if ((cp & 0xC0) != 0x80) {
            cc++;
        }
    }
    PAL_Assign(len_b, cb+sizeof(pal_uint8_t));
    PAL_Assign(len_c, cc);
    h32 ^= h32 >> 16;
    h32 *= 0x85EBCA6BU;
    h32 ^= h32 >> 13;
    h32 *= 0xC2B2AE35U;
    h32 ^= h32 >> 16;
    return h32;
}

PAL_API(pal_uint32_t)
PAL_StringHash32_Utf16
(
    pal_utf16_t const *str, 
    pal_uint32_t    *len_b, 
    pal_uint32_t    *len_c
)
{   /* FNV1 with MurmurHash3 finalizer */
    pal_uint16_t const *s =(pal_uint16_t const*) str;
    pal_uint32_t      h32 = 2166136261U;
    pal_uint32_t       cc = 0;
    pal_uint32_t       cb = 0;
    pal_uint16_t       cp;
    while ((cp = *s++) != 0) {
        h32 =(16777619U * h32) + cp;
        cb += sizeof(pal_uint16_t);
        cc++;
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            cp  =*s;
            if (cp >= 0xDC00 && cp <= 0xDFFF) {
                cb += sizeof(pal_uint16_t);
                s++;
            }
        }
    }
    PAL_Assign(len_b, cb+sizeof(pal_uint16_t));
    PAL_Assign(len_c, cc);
    h32 ^= h32 >> 16;
    h32 *= 0x85EBCA6BU;
    h32 ^= h32 >> 13;
    h32 *= 0xC2B2AE35U;
    h32 ^= h32 >> 16;
    return h32;
}

PAL_API(pal_uint32_t)
PAL_StringHash32_Utf32
(
    pal_utf32_t const *str, 
    pal_uint32_t    *len_b, 
    pal_uint32_t    *len_c
)
{   /* FNV1 with MurmurHash3 finalizer */
    pal_uint32_t const *s =(pal_uint32_t const*) str;
    pal_uint32_t      h32 = 2166136261U;
    pal_uint32_t       cc = 0;
    pal_uint32_t       cb = 0;
    pal_uint32_t       cp;
    while ((cp = *s++) != 0) {
        h32 =(16777619U * h32) + cp;
        cb += sizeof(pal_uint32_t);
        cc++;
    }
    PAL_Assign(len_b, cb+sizeof(pal_uint32_t));
    PAL_Assign(len_c, cc);
    h32 ^= h32 >> 16;
    h32 *= 0x85EBCA6BU;
    h32 ^= h32 >> 13;
    h32 *= 0xC2B2AE35U;
    h32 ^= h32 >> 16;
    return h32;
}

PAL_API(struct PAL_STRING_TABLE*)
PAL_StringTableCreate
(
    struct PAL_STRING_TABLE_INIT *init
)
{
    PAL_STRING_TABLE          *table = NULL;
    PAL_STRING_DATA_ENTRY   *strings = NULL;
    PAL_STRING_HASH_CHUNK    *chunks = NULL;
    PAL_STRING_HASH_CHUNK      *head = NULL;
    PAL_STRING_HASH_CHUNK      *next = NULL;
    PAL_STRING_HASH_CHUNK  **buckets = NULL;
    pal_uint8_t          *table_addr = NULL;
    pal_uint8_t           *hash_addr = NULL;
    pal_uint8_t           *data_addr = NULL;
    DWORD                 error_code = ERROR_SUCCESS;
    pal_uint32_t                i, n;
    PAL_MEMORY_ARENA           arena;
    PAL_MEMORY_ARENA_INIT arena_init;
    PAL_STRING_TABLE_SIZE_INFO sizes;

    if (init->HashFunction == NULL) {
        assert(init->HashFunction != NULL);
        error_code = ERROR_INVALID_PARAMETER;
        return NULL;
    }
    if (init->DataCommitSize >= init->MaxDataSize) {
        assert(init->MaxDataSize >= init->DataCommitSize);
        error_code = ERROR_INVALID_PARAMETER;
        return NULL;
    }
    if (init->InitialCapacity >= init->MaxStringCount) {
        assert(init->MaxStringCount >= init->InitialCapacity);
        error_code = ERROR_INVALID_PARAMETER;
        return NULL;
    }

    /* initialize the three primary memory allocations */
    PAL_StringTableQueryMemorySize(&sizes, init);
    if ((table_addr =(pal_uint8_t *) VirtualAlloc(NULL, sizes.TableReserveSize, MEM_RESERVE, PAGE_NOACCESS)) == NULL) {
        error_code  = GetLastError();
        goto cleanup_and_fail;
    }
    if ((hash_addr  =(pal_uint8_t *) VirtualAlloc(NULL, sizes.HashReserveSize , MEM_RESERVE, PAGE_NOACCESS)) == NULL) {
        error_code  = GetLastError();
        goto cleanup_and_fail;
    }
    if ((data_addr  =(pal_uint8_t *) VirtualAlloc(NULL, sizes.DataReserveSize , MEM_RESERVE, PAGE_NOACCESS)) == NULL) {
        error_code  = GetLastError();
        goto cleanup_and_fail;
    }

    /* commit memory for the table - committed pages are zero-initialized */
    if (VirtualAlloc(table_addr, sizes.TableCommitSize, MEM_COMMIT, PAGE_READWRITE) != table_addr) {
        error_code  = GetLastError();
        goto cleanup_and_fail;
    }
    if (VirtualAlloc(hash_addr , sizes.HashCommitSize , MEM_COMMIT, PAGE_READWRITE) != hash_addr) {
        error_code  = GetLastError();
        goto cleanup_and_fail;
    }
    if (VirtualAlloc(data_addr , sizes.DataCommitSize , MEM_COMMIT, PAGE_READWRITE) != data_addr) {
        error_code  = GetLastError();
        goto cleanup_and_fail;
    }
    
    /* initialize a memory arena to sub-allocate from the table allocation */
    arena_init.AllocatorName = __FUNCTION__;
    arena_init.AllocatorType = PAL_MEMORY_ALLOCATOR_TYPE_HOST;
    arena_init.MemoryStart   =(pal_uint64_t) table_addr;
    arena_init.MemorySize    =(pal_uint64_t) sizes.TableReserveSize;
    arena_init.UserData      = NULL;
    arena_init.UserDataSize  = 0;
    if (PAL_MemoryArenaCreate(&arena, &arena_init) != 0) {
        error_code = ERROR_ARENA_TRASHED;
        goto cleanup_and_fail;
    }
    table   = PAL_MemoryArenaAllocateHostType (&arena, PAL_STRING_TABLE);
    buckets = PAL_MemoryArenaAllocateHostArray(&arena, PAL_STRING_HASH_CHUNK*, sizes.BucketCount);
    strings = PAL_MemoryArenaAllocateHostArray(&arena, PAL_STRING_DATA_ENTRY , sizes.StringReserveCount);
    table->HashString         = init->HashFunction;
    table->StringList         = strings;
    table->HashBuckets        = buckets;
    table->StringDataBase     = data_addr;
    table->StringDataNext     = 0;
    table->StringCount        = 0;
    table->DataCommitSize     = sizes.DataCommitSize;
    table->DataReserveSize    = sizes.DataReserveSize;
    table->HashBucketCount    = sizes.BucketCount;
    table->StringCommitCount  = sizes.StringCommitCount;
    table->StringReserveCount = sizes.StringReserveCount;
    table->HashCommitCount    = sizes.HashCommitCount;
    table->HashReserveCount   = sizes.HashReserveCount;
    table->HashDataBase       = hash_addr;

    /* initialize hash chunk free list */
    for (i = 0, n = sizes.HashCommitCount, head = NULL, chunks = (PAL_STRING_HASH_CHUNK*) hash_addr; i < n; ++i) {
        next = &chunks[n-i-1];
        next->NextChunk = head;
        next->ItemCount = 0;
        head = next;
    }
    table->HashFreeList = head;
    return table;

cleanup_and_fail:
    if (data_addr) {
        VirtualFree(data_addr , 0, MEM_RELEASE);
    }
    if (hash_addr) {
        VirtualFree(hash_addr , 0, MEM_RELEASE);
    }
    if (table_addr) {
        VirtualFree(table_addr, 0, MEM_RELEASE);
    }
    return NULL;
}

PAL_API(void)
PAL_StringTableDelete
(
    struct PAL_STRING_TABLE *table
)
{
    if (table) {
        VirtualFree(table->StringDataBase, 0, MEM_RELEASE);
        VirtualFree(table->HashDataBase, 0, MEM_RELEASE);
        VirtualFree(table, 0, MEM_RELEASE);
    }
}

PAL_API(void)
PAL_StringTableReset
(
    struct PAL_STRING_TABLE *table
)
{
    PAL_STRING_HASH_CHUNK **buckets = table->HashBuckets;
    PAL_STRING_HASH_CHUNK    *chunk;
    pal_uint32_t               i, n;

    table->StringDataNext = 0;
    table->StringCount    = 0;
    for (i = 0, n = table->HashBucketCount; i < n; ++i) {
        while ((chunk = buckets[i]) != NULL) {
            buckets[i]          = chunk->NextChunk;
            chunk->NextChunk    = table->HashFreeList;
            chunk->ItemCount    = 0;
            table->HashFreeList = chunk;
        }
    }
}

PAL_API(void*)
PAL_StringTableIntern
(
    struct PAL_STRING_TABLE *table, 
    void const                *str, 
    pal_uint32_t         char_type
)
{
    pal_uint32_t const  GROW_SIZE = 64UL * 1024; /* 64KB */
    pal_uint32_t const  ALIGNMENT = 4;
    pal_uint32_t             i, n;
    pal_uint32_t             hash;
    pal_uint32_t            len_b;
    pal_uint32_t            len_c;
    pal_uint32_t          nb_need;
    pal_uint32_t           nb_pad;
    pal_uint32_t           eindex;
    pal_uint32_t           bindex;
    pal_uint32_t          eoffset;
    pal_uint32_t          *hashes;
    PAL_STRING_HASH_CHUNK *bucket;
    PAL_STRING_DATA_ENTRY  *entry;
    pal_uint8_t             *sptr;

    if (str != NULL) {
        hash    = table->HashString(str, &len_b, &len_c);
        eoffset = table->StringDataNext;
        eindex  = table->StringCount;
        bindex  = hash & table->HashBucketCount;
        bucket  = table->HashBuckets[bindex];
        while (bucket != NULL) {
            for (i = 0, n = bucket->ItemCount, hashes = bucket->EntryHash; i < n; ++i) {
                if (hashes[i] == hash) {
                    entry = &table->StringList[bucket->EntryIndex[i]];
                    if (entry->StringInfo.ByteLength    == len_b && 
                        entry->StringInfo.CharLength    == len_c && 
                        entry->StringInfo.CharacterType == char_type) {
                        sptr = table->StringDataBase + entry->ByteOffset;
                        if (PAL_CompareMemory(str, sptr, len_b) == 0) {
                            return sptr;
                        }
                    }
                }
            }
            bucket = bucket->NextChunk;
        }

        /* the string doesn't exist in the table - intern it */
        bucket   = table->HashBuckets[bindex];
        eoffset  = table->StringDataNext; /* byte offset in storage block */
        eindex   = table->StringCount;    /* index in StringList array    */
        nb_need  = sizeof(pal_uint32_t);  /* entry index in storage block */
        nb_need += len_b;                 /* string data, including nul   */
        nb_pad   = PAL_AlignUp(eoffset + nb_need, ALIGNMENT) - (eoffset + nb_need);
        nb_need += nb_pad;

        if (table->StringCount == table->StringCommitCount) {
            /* commit an additional 64KB of string entry data */
            pal_uint32_t commit_size = GROW_SIZE;
            pal_uint32_t  num_commit = table->StringReserveCount - table->StringCommitCount;
            pal_uint32_t   nb_commit = num_commit * sizeof(PAL_STRING_DATA_ENTRY);
            if (nb_commit > commit_size) { /* commit a max of 64KB at once */
                nb_commit = commit_size;
                num_commit= nb_commit / sizeof(PAL_STRING_DATA_ENTRY);
            }
            if (VirtualAlloc(&table->StringList[table->StringCount], nb_commit, MEM_COMMIT, PAGE_READWRITE) == NULL) {
                return NULL;
            }
            table->StringCommitCount += num_commit;
        }
        if ((eoffset + nb_need) >= table->DataCommitSize) {
            /* commit additional storage block data */
            pal_uint32_t  commit_size = GROW_SIZE;
            if (nb_need > commit_size) {
                commit_size = nb_need;
                commit_size = PAL_AlignUp(commit_size, GROW_SIZE);
                if((table->DataCommitSize + commit_size) > table->DataReserveSize) {
                    /* clamp to maximum data size */
                    commit_size = table->DataReserveSize - table->DataCommitSize;
                }
                if (commit_size < nb_need) {
                    /* not enough data storage */
                    return NULL;
                }
            }
            if (VirtualAlloc(&table->StringDataBase[table->DataCommitSize], commit_size, MEM_COMMIT, PAGE_READWRITE) == NULL) {
                return NULL;
            }
            table->DataCommitSize += commit_size;
        }

        /* get the hash chunk where the item will be inserted */
        if (bucket == NULL || bucket->ItemCount == PAL_STRING_HASH_CHUNK_CAPACITY) {
            if (table->HashFreeList == NULL) {
                /* commit an additional block of hash chunks */
                pal_uint32_t   commit_size = GROW_SIZE;
                pal_uint32_t    num_commit = commit_size / sizeof(PAL_STRING_HASH_CHUNK);
                if((table->HashCommitCount + num_commit) > table->HashReserveCount) {
                    num_commit = table->HashReserveCount - table->HashCommitCount;
                    commit_size= num_commit * sizeof(PAL_STRING_HASH_CHUNK);
                }
                if (VirtualAlloc(&table->HashDataBase[table->HashCommitCount*sizeof(PAL_STRING_HASH_CHUNK)], commit_size, MEM_COMMIT, PAGE_READWRITE) == NULL) {
                    return NULL;
                }
                for (i = table->HashCommitCount, n = num_commit; i < n; ++i) {
                    PAL_STRING_HASH_CHUNK *chunk =(PAL_STRING_HASH_CHUNK*) &table->HashDataBase[i * sizeof(PAL_STRING_HASH_CHUNK)];
                    chunk->NextChunk     = table->HashFreeList;
                    chunk->ItemCount     = 0;
                    table->HashFreeList  = chunk;
                }
                table->HashCommitCount += num_commit;
            }
            /* take a chunk from the head of the free list.
             * insert it at the head of the hash bucket list. */
            bucket = table->HashFreeList;
            table->HashFreeList        = bucket->NextChunk;
            bucket->NextChunk          = table->HashBuckets[bindex];
            bucket->ItemCount          = 0;
            table->HashBuckets[bindex] = bucket;
        }

        /* append the item to the hash chunk */
        bucket->EntryHash [bucket->ItemCount] = hash;
        bucket->EntryIndex[bucket->ItemCount] = eindex;
        bucket->ItemCount++;

        /* cache the string information in the table */
        entry = &table->StringList[eindex];
        entry->StringInfo.ByteLength       = len_b;
        entry->StringInfo.CharLength       = len_c;
        entry->StringInfo.CharacterType    = char_type;
        entry->ByteOffset = eoffset + sizeof(pal_uint32_t);
        table->StringCount++;

        /* copy the string data to the storage block */
        table->StringDataNext = eoffset + nb_need;
        sptr  = table->StringDataBase + eoffset;
        sptr += PAL_Write_ui32(sptr, eindex, 0);
        PAL_CopyMemory(sptr, str, len_b);
        sptr += len_b;
        for(i = 0; i < nb_pad; ++i) {
            *sptr++ = 0;
        }
        return table->StringDataBase + eoffset + sizeof(pal_uint32_t);
    }
    return NULL;
}

PAL_API(int)
PAL_StringTableInfo
(
    struct PAL_STRING_INFO   *info, 
    struct PAL_STRING_TABLE *table, 
    void const                *str
)
{
    pal_uint8_t const *addr =(pal_uint8_t const*)str;
    pal_uint8_t   *addr_min = table->StringDataBase;
    pal_uint8_t   *addr_max = table->StringDataBase + table->StringDataNext;
    pal_uint32_t      index;
    PAL_STRING_INFO      *i;
    if (addr  >= addr_min && addr < addr_max) {
        index  =*(pal_uint32_t*)(addr-sizeof(pal_uint32_t));
        assert(index < table->StringCount); 
        i = &table->StringList[index].StringInfo;
        info->ByteLength     = i->ByteLength;
        info->CharLength     = i->CharLength;
        info->CharacterType  = i->CharacterType;
        return  0;
    } else {
        info->ByteLength     = 0;
        info->CharLength     = 0;
        info->CharacterType  = PAL_STRING_CHAR_TYPE_UNKNOWN;
        return -1;
    }
}

