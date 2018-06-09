/**
 * @summary Define the platform-specific types and other internal bits for the 
 * Microsoft Windows Desktop platform.
 */
#ifndef __PAL_WIN32_FILE_H__
#define __PAL_WIN32_FILE_H__

#ifndef __PAL_FILE_H__
#include "pal_file.h"
#endif

#ifndef PAL_NO_INCLUDES
#include <Windows.h>
#include <strsafe.h>
#endif

/* @summary Define the data associated with an open file.
 */
typedef struct PAL_FILE {
    HANDLE                             Handle;                 /* The Win32 HANDLE associated with the file. */
} PAL_FILE;

/* @summary Define the data retrieved about a file when it is opened or stat'd.
 */
typedef struct PAL_FILE_INFO {
    pal_sint64_t                       FileSize;               /* The size of the file, in bytes. */
    pal_sint64_t                       CreationTime;           /* The file creation time, as a Unix timestamp. */
    pal_sint64_t                       AccessTime;             /* The file last access time, as a Unix timestamp. */
    pal_sint64_t                       WriteTime;              /* The file last write time, as a Unix timestamp.*/
    pal_uint32_t                       Alignment;              /* The required alignment for performing asynchronous I/O operations, in bytes. */
    pal_uint32_t                       Attributes;             /* File attributes as would be returned by stat(2). */
} PAL_FILE_INFO;

/* @summary Define the data associated with a native path string parsed in-place.
 */
typedef struct PAL_PATH_PARTS {
    pal_char_t                        *Root;                   /* Pointer to the first character of the root, share or drive portion of the path. */
    pal_char_t                        *RootEnd;                /* Pointer to the last character of the root, share or drive portion of the path. */
    pal_char_t                        *Path;                   /* Pointer to the first character of the directory portion of the path. */
    pal_char_t                        *PathEnd;                /* Pointer to the last character of the directory portion of the path. */
    pal_char_t                        *Filename;               /* Pointer to the first character of the filename portion of the path. */
    pal_char_t                        *FilenameEnd;            /* Pointer to the last character of the filename portion of the path. */
    pal_char_t                        *Extension;              /* Pointer to the first character of the extension portion of the path. */
    pal_char_t                        *ExtensionEnd;           /* Pointer to the last character of the extension portion of the path. */
    pal_uint32_t                       PathFlags;              /* One or more bitwise OR'd values of the PAL_PATH_FLAGS enumeration specifying path attributes and which components are present. */ 
} PAL_PATH_PARTS;

/* @summary Define the data associated with a filesystem enumerator object, used to report information about files and directories.
 */
typedef struct PAL_FILE_ENUMERATOR {
    HANDLE                             DirectoryHandle;        /* The Win32 handle for the root directory. */
    PAL_FileEnum_Func                  FileCallback;           /* The callback to invoke for files encountered during enumeration. */
    PAL_FileEnum_Func                  DirectoryCallback;      /* The callback to invoke for directories encountered during enumeration. */
    pal_uintptr_t                      OpaqueData;             /* A pointer-sized value reserved for application use. This value is passed through unchanged to the file and directory callbacks. */
    pal_char_t                        *AbsolutePath;           /* A nul-terminated string specifying the absolute path of the file or directory. */
    pal_char_t                        *RelativePath;           /* A pointer to the first character of the path in the AbsolutePath buffer for the portion of the path used the initial search location. */
    pal_char_t                        *SearchPath;             /* A pointer to the buffer used to specifythe search filter. */
    pal_char_t                        *SearchEnd;              /* A pointer to the '*' character at the end of the search filter. */
    pal_uint32_t                       SearchFlags;            /* One or more PAL_FILE_ENUMERATOR_FLAGS specifying the desired search behavior. */
    pal_uint32_t                       BasePathLength;         /* The length of the base path string, in characters. */
    void                              *MemoryStart;            /* A pointer to the start of the memory block allocated for the storage array. */
    pal_uint64_t                       MemorySize;             /* The total size of the memory block allocated for the storage array. */
} PAL_FILE_ENUMERATOR;

/* @summary Define the data used to configure a filesystem enumerator object.
 */
typedef struct PAL_FILE_ENUMERATOR_INIT {
    pal_char_t                        *StartPath;              /* A nul-terminated string specifying the path of the directory at which enumeration will begin. */
    PAL_FileEnum_Func                  FileCallback;           /* The callback to invoke for files encountered during enumeration. */
    PAL_FileEnum_Func                  DirectoryCallback;      /* The callback to invoke for directories encountered during enumeration. */
    pal_uint32_t                       SearchFlags;            /* One or more of PAL_FILE_ENUMERATOR_FLAGS specifying the desired search behavior. */
    pal_uint32_t                       Reserved;               /* Reserved for future use. Set to zero. */
    pal_uintptr_t                      OpaqueData;             /* A pointer-sized value reserved for application use. This value is passed through unchanged to the file and directory callbacks. */
    void                              *MemoryStart;            /* A pointer to the start of the memory block allocated for the storage array. */
    pal_uint64_t                       MemorySize;             /* The total size of the memory block allocated for the storage array. */
} PAL_FILE_ENUMERATOR_INIT;

/* @summary Define the data used to request information about a file or directory.
 * assert(sizeof(PAL_FILE_STAT_DATA) <= PAL_MAX_TASK_DATA_BYTES)
 */
typedef struct PAL_FILE_STAT_DATA {
    pal_char_t                        *Path;                   /* A nul-terminated string specifying the path to query. The string must remain valid for the duration of task execution. */
    PAL_FileStat_Func                  Callback;               /* The callback function to invoke when the operation has completed. */
    pal_uintptr_t                      OpaqueData;             /* A pointer-sized value reserved for application use. This value is passed through unchanged in the result structure. */
    pal_uint32_t                       OpaqueId;               /* A 32-bit value reserved for application use. This value is passed through unchanged in the result structure. */
} PAL_FILE_STAT_DATA;

/* @summary Define the data returned when a filesystem stat request completes.
 */
typedef struct PAL_FILE_STAT_RESULT {
    pal_char_t                        *Path;                   /* The nul-terminated string specifying the path that was queried. */
    PAL_FILE_INFO                      Info;                   /* The information returned by the stat operation. */
    pal_sint32_t                       Success;                /* This value is non-zero if the operation was successful, or zero if the stat operation failed. */
    pal_uint32_t                       ResultCode;             /* The OS result code returned by the stat operation. */
    pal_uintptr_t                      OpaqueData;             /* A pointer-sized value reserved for application use. This value is passed through unchanged in the result structure. */
    pal_uint32_t                       OpaqueId;               /* A 32-bit value reserved for application use. This value is passed through unchanged in the result structure. */
} PAL_FILE_STAT_RESULT;

/* @summary Define the data used to request that a file be opened.
 * assert(sizeof(PAL_FILE_OPEN_DATA) <= PAL_MAX_TASK_DATA_BYTES)
 */
typedef struct PAL_FILE_OPEN_DATA {
    pal_char_t                        *Path;                   /* A nul-terminated string specifying the path to open. The string must remain valid for the duration of task execution. */
    PAL_FileOpen_Func                  Callback;               /* The callback function to invoke when the operation has completed. */
    pal_uintptr_t                      OpaqueData;             /* A pointer-sized value reserved for application use. This value is passed through unchanged in the result structure. */
    pal_uint32_t                       OpenHints;              /* One or more PAL_FILE_OPEN_HINT_FLAGS providing hints about how the file will be used. */
    pal_uint32_t                       OpaqueId;               /* A 32-bit value reserved for application use. This value is passed through unchanged in the result structure. */
} PAL_FILE_OPEN_DATA;

/* @summary Define the data returned when a file open request completes.
 */
typedef struct PAL_FILE_OPEN_RESULT {
    pal_char_t                        *Path;                   /* The nul-terminated string specifying the path of the file that was opened. */
    PAL_FILE                           File;                   /* The file handle used to access the file. */
    PAL_FILE_INFO                      Info;                   /* The information returned by the stat operation. */
    pal_sint32_t                       Success;                /* This value is non-zero if the operation was successful, or zero if the operation failed. */
    pal_uint32_t                       ResultCode;             /* The OS result code returned by the operation. */
    pal_uintptr_t                      OpaqueData;             /* A pointer-sized value reserved for application use. This value is passed through unchanged in the result structure. */
    pal_uint32_t                       OpaqueId;               /* A 32-bit value reserved for application use. This value is passed through unchanged in the result structure. */
} PAL_FILE_OPEN_RESULT;

/* @summary Define the data used to request an asynchronous read from a file into a host memory buffer.
 * assert(sizeof(PAL_FILE_READ_DATA) <= PAL_MAX_TASK_DATA_BYTES)
 */
typedef struct PAL_FILE_READ_DATA {
    PAL_FILE                           File;                   /* The file to read from. */
    PAL_FileRead_Func                  Callback;               /* The callback function to invoke when the operation has completed. */
    void                              *Destination;            /* The destination buffer. This buffer must remain valid for the duration of task execution. */
    pal_sint64_t                       ReadOffset;             /* The byte offset within the source file at which to begin reading. */
    pal_uintptr_t                      OpaqueData;             /* A pointer-sized value reserved for application use. This value is passed through unchanged in the result structure. */
    pal_uint32_t                       OpaqueId;               /* A 32-bit value reserved for application use. This value is passed through unchanged in the result structure. */
    pal_uint32_t                       ReadAmount;             /* The number of bytes to read from the file. */
} PAL_FILE_READ_DATA;

/* @summary Define the data returned when a file read request completes.
 */
typedef struct PAL_FILE_READ_RESULT {
    PAL_FILE                           File;                   /* The file to read from. */
    void                              *Destination;            /* The destination buffer. This buffer must remain valid for the duration of task execution. */
    pal_sint64_t                       ReadOffset;             /* The byte offset within the source file at which to begin reading. */
    pal_uintptr_t                      OpaqueData;             /* A pointer-sized value reserved for application use. This value is passed through unchanged in the result structure. */
    pal_uint32_t                       OpaqueId;               /* A 32-bit value reserved for application use. This value is passed through unchanged in the result structure. */
    pal_uint32_t                       ReadAmount;             /* The number of bytes the application requested to read from the file. */
    pal_uint32_t                       TransferAmount;         /* The actual number of bytes read from the file into the destination buffer. */
    pal_sint32_t                       Success;                /* This value is non-zero if the operation was successful, or zero if the operation failed. */
    pal_uint32_t                       ResultCode;             /* The OS result code returned by the operation. */
} PAL_FILE_READ_RESULT;

/* @summary Define the data used to request an asynchronous write from a host memory buffer to a file.
 * assert(sizeof(PAL_FILE_WRITE_DATA) <= PAL_MAX_TASK_DATA_BYTES)
 */
typedef struct PAL_FILE_WRITE_DATA {
    PAL_FILE                           File;                   /* The file to write to. */
    PAL_FileWrite_Func                 Callback;               /* The callback function to invoke when the operation has completed. */
    void                              *Source;                 /* The source buffer. This buffer must remain valid for the duration of task execution. */
    pal_sint64_t                       WriteOffset;            /* The byte offset within the target file at which to begin writing. */
    pal_uintptr_t                      OpaqueData;             /* A pointer-sized value reserved for application use. This value is passed through unchanged in the result structure. */
    pal_uint32_t                       OpaqueId;               /* A 32-bit value reserved for application use. This value is passed through unchanged in the result structure. */
    pal_uint32_t                       WriteAmount;            /* The number of bytes to write to the file. */
} PAL_FILE_WRITE_DATA;

/* @summary Define the data returned when a file write request completes.
 */
typedef struct PAL_FILE_WRITE_RESULT {
    PAL_FILE                           File;                   /* The file to write to. */
    void                              *Source;                 /* The source buffer. This buffer must remain valid for the duration of task execution. */
    pal_sint64_t                       WriteOffset;            /* The byte offset within the target file at which to begin writing. */
    pal_uintptr_t                      OpaqueData;             /* A pointer-sized value reserved for application use. This value is passed through unchanged in the result structure. */
    pal_uint32_t                       OpaqueId;               /* A 32-bit value reserved for application use. This value is passed through unchanged in the result structure. */
    pal_uint32_t                       WriteAmount;            /* The number of bytes the application requested to write to the file. */
    pal_uint32_t                       TransferAmount;         /* The actual number of bytes written to the file from the source buffer. */
    pal_sint32_t                       Success;                /* This value is non-zero if the operation was successful, or zero if the operation failed. */
    pal_uint32_t                       ResultCode;             /* The OS result code returned by the operation. */
} PAL_FILE_WRITE_RESULT;

/* @summary Define the data used to request that a file be created with a pre-allocated size.
 * The file must then be written sequentially by the application for maximum performance.
 * assert(sizeof(PAL_FILE_CREATE_DATA) <= PAL_MAX_TASK_DATA_BYTES)
 */
typedef struct PAL_FILE_CREATE_DATA {
    pal_char_t                        *Path;                   /* A nul-terminated string specifying the path to open. The string must remain valid for the duration of task execution. */
    PAL_FileCreate_Func                Callback;               /* The callback function to invoke when the operation has completed. */
    pal_sint64_t                       DesiredSize;            /* The number of bytes to pre-allocate for the file data. */
    pal_uintptr_t                      OpaqueData;             /* A pointer-sized value reserved for application use. This value is passed through unchanged in the result structure. */
    pal_uint32_t                       OpenHints;              /* One or more PAL_FILE_OPEN_HINT_FLAGS providing hints about how the file will be used. */
    pal_uint32_t                       OpaqueId;               /* A 32-bit value reserved for application use. This value is passed through unchanged in the result structure. */
} PAL_FILE_CREATE_DATA;

/* @summary Define the data returned when a file create request completes.
 */
typedef struct PAL_FILE_CREATE_RESULT {
    pal_char_t                        *Path;                   /* The nul-terminated string specifying the path of the file that was opened. */
    PAL_FILE                           File;                   /* The file handle used to access the file. */
    PAL_FILE_INFO                      Info;                   /* The information returned by the stat operation. */
    pal_sint32_t                       Success;                /* This value is non-zero if the operation was successful, or zero if the operation failed. */
    pal_uint32_t                       ResultCode;             /* The OS result code returned by the operation. */
    pal_uintptr_t                      OpaqueData;             /* A pointer-sized value reserved for application use. This value is passed through unchanged in the result structure. */
    pal_uint32_t                       OpaqueId;               /* A 32-bit value reserved for application use. This value is passed through unchanged in the result structure. */
} PAL_FILE_CREATE_RESULT;

/* @summary Define the data used to request that any buffered writes to a file be flushed to disk.
 * assert(sizeof(PAL_FILE_FLUSH_DATA) <= PAL_MAX_TASK_DATA_BYTES)
 */
typedef struct PAL_FILE_FLUSH_DATA {
    PAL_FILE                           File;                   /* The file to flush. */
    PAL_FileFlush_Func                 Callback;               /* The callback function to invoke when the operation has completed. */
    pal_uintptr_t                      OpaqueData;             /* A pointer-sized value reserved for application use. This value is passed through unchanged in the result structure. */
    pal_uint32_t                       OpaqueId;               /* A 32-bit value reserved for application use. This value is passed through unchanged in the result structure. */
} PAL_FILE_FLUSH_DATA;

/* @summary Define the data returned when a file flush request completes.
 */
typedef struct PAL_FILE_FLUSH_RESULT {
    PAL_FILE                           File;                   /* The file to flush. */
    PAL_FILE_INFO                      Info;                   /* The information returned by the stat operation. */
    pal_sint32_t                       Success;                /* This value is non-zero if the operation was successful, or zero if the operation failed. */
    pal_uint32_t                       ResultCode;             /* The OS result code returned by the operation. */
    pal_uintptr_t                      OpaqueData;             /* A pointer-sized value reserved for application use. This value is passed through unchanged in the result structure. */
    pal_uint32_t                       OpaqueId;               /* A 32-bit value reserved for application use. This value is passed through unchanged in the result structure. */
} PAL_FILE_FLUSH_RESULT;

/* @summary Define the data used to request that a file handle be closed.
 * assert(sizeof(PAL_FILE_CLOSE_DATA) <= PAL_MAX_TASK_DATA_BYTES)
 */
typedef struct PAL_FILE_CLOSE_DATA {
    PAL_FILE                           File;                   /* The file to close. */
    PAL_FileClose_Func                 Callback;               /* The callback function to invoke when the operation has completed. */
    pal_uintptr_t                      OpaqueData;             /* A pointer-sized value reserved for application use. This value is passed through unchanged in the result structure. */
    pal_uint32_t                       OpaqueId;               /* A 32-bit value reserved for application use. This value is passed through unchanged in the result structure. */
} PAL_FILE_CLOSE_DATA;

/* @summary Define the data returned when a file close request completes.
 */
typedef struct PAL_FILE_CLOSE_RESULT {
    PAL_FILE                           File;                   /* The file to close. */
    pal_sint32_t                       Success;                /* This value is non-zero if the operation was successful, or zero if the operation failed. */
    pal_uint32_t                       ResultCode;             /* The OS result code returned by the operation. */
    pal_uintptr_t                      OpaqueData;             /* A pointer-sized value reserved for application use. This value is passed through unchanged in the result structure. */
    pal_uint32_t                       OpaqueId;               /* A 32-bit value reserved for application use. This value is passed through unchanged in the result structure. */
} PAL_FILE_CLOSE_RESULT;

#endif /* __PAL_WIN32_FILE_H__ */
