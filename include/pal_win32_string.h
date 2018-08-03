/**
 * @summary Define the platform-specific types and other internal bits for the 
 * Microsoft Windows Desktop platform.
 */
#ifndef __PAL_WIN32_STRING_H__
#define __PAL_WIN32_STRING_H__

#ifndef __PAL_STRING_H__
#include "pal_string.h"
#endif

#ifndef PAL_NO_INCLUDES
#include <Windows.h>
#include <strsafe.h>
#endif

/* @summary Define the character type identifier for OS-native characters.
 */
#ifndef PAL_STRING_CHAR_TYPE_NATIVE
#   define PAL_STRING_CHAR_TYPE_NATIVE                         PAL_STRING_CHAR_TYPE_UTF16
#endif

/* @summary Define the capacity of a PAL_STRING_HASH_CHUNK.
 */
#ifndef PAL_STRING_HASH_CHUNK_CAPACITY
#   define PAL_STRING_HASH_CHUNK_CAPACITY                      30
#endif

/* @summary Define the data associated with a single string interned in a string table.
 */
typedef struct PAL_STRING_DATA_ENTRY {                         /* 8 bytes */
    PAL_STRING_INFO               StringInfo;                  /* Size and character type information about the string. */
    pal_uint32_t                  ByteOffset;                  /* The byte offset of the start of the string data within the data chunk. */
} PAL_STRING_DATA_ENTRY;

/* @summary Define the data associated with a chunk in the hash list.
 * The hash table used to speed lookups within a string table consists of a fixed-length array of buckets.
 * Each bucket is simply a pointer to the head of a singly-linked list of PAL_STRING_HASH_CHUNK instances. 
 * Each bucket is anticipated to have either zero, one or two hash chunks, though the actual number per-bucket is not limited.
 * The data entries themselves are stored in a separate contiguous array maintained by the string table.
 * The Lookup(hash) consists of:
 * 1. Map hash to a bucket index.
 * 2. for each PAL_STRING_HASH_CHUNK in the bucket, perform a linear search of EntryHash.
 *    if a match is found at position i, check the PAL_STRING_TABLE::EntryList[bucket->EntryIndex[i]].
 *    if no match is found, check the next PAL_STRING_HASH_CHUNK in the bucket.
 */
typedef struct PAL_STRING_HASH_CHUNK {                         /* 256 bytes */
#   define CAP PAL_STRING_HASH_CHUNK_CAPACITY
    struct PAL_STRING_HASH_CHUNK *NextChunk;                   /* The next chunk in the list. */
    pal_uint32_t                  ItemCount;                   /* The number of items stored in the chunk. */
    pal_uint32_t                  Reserved;                    /* Reserved for future use. Set to zero. */
    pal_uint32_t                  EntryHash [CAP];             /* An array of 32-bit hash values for each item in the chunk. */
    pal_uint32_t                  EntryIndex[CAP];             /* An array of 32-bit index values into the string table entry list. */
#   undef  CAP
} PAL_STRING_HASH_CHUNK;

/* @summary Define the data associated with a string table.
 * A string table stores a single unique copy of a string. Strings are always stored nul-terminated.
 */
typedef struct PAL_STRING_TABLE {
    PAL_StringHash32_Func         HashString;                  /* The callback function used to hash a string. */
    struct PAL_STRING_DATA_ENTRY *StringList;                  /* An array of StringCapacity string descriptors. */
    struct PAL_STRING_HASH_CHUNK**HashBuckets;                 /* An array of BucketCount pointers to hash chunks storing the content of each hash bucket. */
    pal_uint8_t                  *StringDataBase;              /* The base address of the memory block used to store string data. DataCommitSize bytes are valid. */
    pal_uint32_t                  StringDataNext;              /* The offset of the next byte to return from the string data block. */
    pal_uint32_t                  StringCount;                 /* The number of strings interned in the table (also the number of valid entries in the StringList array). */
    pal_uint32_t                  DataCommitSize;              /* The number of bytes committed for storing string data. */
    pal_uint32_t                  DataReserveSize;             /* The number of bytes reserved for storing string data. */
    pal_uint32_t                  HashBucketCount;             /* The number of hash buckets. This defines the dimension of the HashBuckets array. */
    pal_uint32_t                  StringCommitCount;           /* The number of committed PAL_STRING_DATA_ENTRY items in the StringList array. */
    pal_uint32_t                  StringReserveCount;          /* The maximum number of PAL_STRING_DATA_ENTRY items in the StringList array. */
    pal_uint32_t                  HashCommitCount;             /* The number of PAL_STRING_HASH_CHUNK items committed. */
    pal_uint32_t                  HashReserveCount;            /* The number of PAL_STRING_HASH_CHUNK items */
    pal_uint8_t                  *HashDataBase;                /* The base address of the memory block used to store hash bucket data. */
    struct PAL_STRING_HASH_CHUNK *HashFreeList;                /* A pointer to the head of the hash chunk free list, or NULL if the free list is empty. */
} PAL_STRING_TABLE;

#endif /* __PAL_WIN32_STRING_H__ */

