/**
 * @summary Define the PAL types and API entry points for working with files 
 * and filesystem paths.
 */
#ifndef __PAL_FILE_H__
#define __PAL_FILE_H__

#ifndef __PAL_H__
#include "pal.h"
#endif

/* @summary Forward-declare the types exported by this module.
 * The type definitions are included in the platform-specific header.
 */
struct  PAL_FILE;
struct  PAL_FILE_INFO;
struct  PAL_PATH_PARTS;
struct  PAL_FILE_ENUMERATOR;
struct  PAL_FILE_ENUMERATOR_INIT;
struct  PAL_FILE_STAT_DATA;
struct  PAL_FILE_STAT_RESULT;
struct  PAL_FILE_OPEN_DATA;
struct  PAL_FILE_OPEN_RESULT;
struct  PAL_FILE_READ_DATA;
struct  PAL_FILE_READ_RESULT;
struct  PAL_FILE_CREATE_DATA;
struct  PAL_FILE_CREATE_RESULT;
struct  PAL_FILE_WRITE_DATA;
struct  PAL_FILE_WRITE_RESULT;
struct  PAL_FILE_FLUSH_DATA;
struct  PAL_FILE_FLUSH_RESULT;
struct  PAL_FILE_CLOSE_DATA;
struct  PAL_FILE_CLOSE_RESULT;

#if 0
/* @summary Define the signature for the callback function invoked when a file stat task has completed.
 * @param args A PAL_TASK_ARGS instance specifying data associated with the task and data used to spawn additional tasks.
 * @param result Information returned by the operation.
 */
typedef void (*PAL_FileStat_Func)
(
    struct PAL_TASK_ARGS          *args, 
    struct PAL_FILE_STAT_RESULT *result
);

/* @summary Define the signature for the callback function invoked when a file open task has completed.
 * @param args A PAL_TASK_ARGS instance specifying data associated with the task and data used to spawn additional tasks.
 * @param result Information returned by the operation.
 */
typedef void (*PAL_FileOpen_Func)
(
    struct PAL_TASK_ARGS          *args, 
    struct PAL_FILE_OPEN_RESULT *result
);

/* @summary Define the signature for the callback function invoked when a file create task has completed.
 * @param args A PAL_TASK_ARGS instance specifying data associated with the task and data used to spawn additional tasks.
 * @param result Information returned by the operation.
 */
typedef void (*PAL_FileCreate_Func)
(
    struct PAL_TASK_ARGS            *args, 
    struct PAL_FILE_CREATE_RESULT *result
);

/* @summary Define the signature for the callback function invoked when a file read task has completed.
 * @param args A PAL_TASK_ARGS instance specifying data associated with the task and data used to spawn additional tasks.
 * @param result Information returned by the operation.
 */
typedef void (*PAL_FileRead_Func)
(
    struct PAL_TASK_ARGS          *args, 
    struct PAL_FILE_READ_RESULT *result
);

/* @summary Define the signature for the callback function invoked when a file write task has completed.
 * @param args A PAL_TASK_ARGS instance specifying data associated with the task and data used to spawn additional tasks.
 * @param result Information returned by the operation.
 */
typedef void (*PAL_FileWrite_Func)
(
    struct PAL_TASK_ARGS           *args, 
    struct PAL_FILE_WRITE_RESULT *result
);

/* @summary Define the signature for the callback function invoked when a file flush task has completed.
 * @param args A PAL_TASK_ARGS instance specifying data associated with the task and data used to spawn additional tasks.
 * @param result Information returned by the operation.
 */
typedef void (*PAL_FileFlush_Func)
(
    struct PAL_TASK_ARGS           *args, 
    struct PAL_FILE_FLUSH_RESULT *result
);

/* @summary Define the signature for the callback function invoked when a file close task has completed.
 * @param args A PAL_TASK_ARGS instance specifying data associated with the task and data used to spawn additional tasks.
 * @param result Information returned by the operation.
 */
typedef void (*PAL_FileClose_Func)
(
    struct PAL_TASK_ARGS           *args, 
    struct PAL_FILE_CLOSE_RESULT *result
);
#endif /* DISABLED */

/* @summary Define the signature for the callback function invoked when a file or directory is encountered during filesystem enumeration.
 * @param fsenum The PAL_FILE_ENUMERATOR performing the enumeration and invoking the callback.
 * @param absolute_path A nul-terminated string specifying the absolute path of the file or directory.
 * @param relative_path A nul-terminated string specifying the path of the file or directory, relative to the starting point of enumeration.
 * @param entry_name A nul-terminated string specifying the file or directory name, including the extension (if any).
 * @param entry_info A PAL_FILE_INFO specifying attributes such as the creation and last write time.
 * @param context The opaque context data specified when the file enumerator was created.
 * @return Non-zero to continue enumeration, or zero to stop enumeration.
 */
typedef int  (*PAL_FileEnum_Func)
(
    struct PAL_FILE_ENUMERATOR *fsenum, 
    pal_char_t          *absolute_path, 
    pal_char_t          *relative_path, 
    pal_char_t             *entry_name, 
    struct PAL_FILE_INFO   *entry_info, 
    pal_uintptr_t              context
);

#ifdef __cplusplus
extern "C" {
#endif

/* @summary Parse a path string, in place, into its constituient parts.
 * @param parts The PAL_PATH_PARTS to populate with the start and end of each portion of the path string.
 * @param path_buf The buffer to parse, containing a native path string.
 * @param path_end A pointer to the last character of the input buffer to parse, or NULL to scan for a nul terminator.
 * @return Zero if the path was successfully parsed, or -1 if an error occurred.
 */
PAL_API(int)
PAL_PathParse
(
    struct PAL_PATH_PARTS *parts, 
    pal_char_t         *path_buf, 
    pal_char_t         *path_end
);

/* @summary Given an open file, retrieve the absolute native path of the file.
 * @param buffer The destination buffer to receive the absolute native path of the file.
 * @param buffer_size The maximum number of bytes that can be written to the destination buffer.
 * @param buffer_end If non-NULL, on return this location points to the trailing nul in the destination buffer.
 * @param buffer_need If non-NULL, on return this location is updated with the number of bytes required to store the path string.
 * @param file The open file to query.
 * @return Zero if the path was retrieved successfully, or -1 if an error occurred.
 */
PAL_API(int)
PAL_PathForFile
(
    pal_char_t       *buffer, 
    pal_usize_t  buffer_size,
    pal_char_t  **buffer_end, 
    pal_usize_t *buffer_need, 
    struct PAL_FILE    *file
);

/* @summary Append one path fragment to another.
 * @param buffer The buffer containing the existing nul-terminated path fragment, to which the new fragment will be appended.
 * @param buffer_size The maximum number of bytes that can be written to the destination buffer.
 * @param buffer_end If non-NULL, on return this location points to the trailing nul in the destination buffer.
 * @param buffer_need If non-NULL, on return this location is updated with the number of bytes required to store the path string with the appended fragment.
 * @param append The nul-terminated path fragment to append to the existing path fragment in buffer.
 * @return Zero if the path fragment was successfully appended, or -1 if an error occurred.
 */
PAL_API(int)
PAL_PathAppend
(
    pal_char_t       *buffer, 
    pal_usize_t  buffer_size,
    pal_char_t  **buffer_end, 
    pal_usize_t *buffer_need, 
    pal_char_t const *append
);

/* @summary Change the file extension of a path.
 * @param buffer The buffer containing the existing nul-terminated path including filename whose extension will be changed.
 * @param buffer_size The maximum number of bytes that can be written to the destination buffer.
 * @param buffer_end If non-NULL, on return this location points to the trailing nul in the destination buffer.
 * @param buffer_need If non-NULL, on return this location is updated with the number of bytes required to store the path string with the updated extension.
 * @param new_extension The nul-terminated file extension to apply to the existing filename in buffer.
 * @return Zero if the file extension was successfully changed, or -1 if an error occurred.
 */
PAL_API(int)
PAL_PathChangeExtension
(
    pal_char_t              *buffer, 
    pal_usize_t         buffer_size,
    pal_char_t         **buffer_end, 
    pal_usize_t        *buffer_need, 
    pal_char_t const *new_extension
);

/* @summary Append a file extension to a path.
 * @param buffer The buffer containing the existing nul-terminated path including filename to which the file extension will be appended.
 * @param buffer_size The maximum number of bytes that can be written to the destination buffer.
 * @param buffer_end If non-NULL, on return this location points to the trailing nul in the destination buffer.
 * @param buffer_need If non-NULL, on return this location is updated with the number of bytes required to store the path string with the appended extension.
 * @param extension The nul-terminated file extension to append to the existing filename in buffer.
 * @return Zero if the file extension was successfully appended, or -1 if an error occurred.
 */
PAL_API(int)
PAL_PathAppendExtension
(
    pal_char_t          *buffer, 
    pal_usize_t     buffer_size,
    pal_char_t     **buffer_end, 
    pal_usize_t    *buffer_need, 
    pal_char_t const *extension
);

/* @summary Ensures that each directory in a given path string exists. If the directory does not exist, it is created.
 * @param path A nul-terminated native path string specifying the directory tree.
 * @return Zero if the directory tree exists or was created, or -1 if an error occurred.
 */
PAL_API(int)
PAL_DirectoryCreate
(
    pal_char_t *path
);

/* @summary Determine the amount of memory required to initialize a filesystem enumerator object.
 * @return The minimum number of bytes required to succesfully initialize a filesystem enumerator object.
 */
PAL_API(pal_usize_t)
PAL_FileEnumeratorQueryMemorySize
(
    void
);

/* @summary Initialize a filesystem enumerator using the given configuration.
 * The root directory is locked and cannot be deleted until the enumerator is deleted.
 * @param fsenum The PAL_FILE_ENUMERATOR to initialize.
 * @param init Data used to configure the filesystem enumerator.
 * @return Zero if the filesystem enumerator is successfully initialized, or -1 if an error occurred.
 */
PAL_API(int)
PAL_FileEnumeratorCreate
(
    struct PAL_FILE_ENUMERATOR    *fsenum, 
    struct PAL_FILE_ENUMERATOR_INIT *init
);

/* @summary Free resources and unlock the root directory associated with a filesystem enumerator.
 * @param fsenum The filesystem enumerator to delete.
 */
PAL_API(void)
PAL_FileEnumeratorDelete
(
    struct PAL_FILE_ENUMERATOR *fsenum
);

/* @summary Execute a filesystem enumeration operation. User callbacks are invoked for each file and directory encountered.
 * @param fsenum The filesystem enumerator to execute.
 * @return Zero if enumeration completed successfully, or -1 if an error occurred.
 */
PAL_API(int)
PAL_FileEnumeratorExecute
(
    struct PAL_FILE_ENUMERATOR *fsenum
);

/* @summary Determine whether a given file handle is valid.
 * @param file The file handle to query.
 * @return Non-zero if the specified file handle is valid, or zero if the file handle is invalid.
 */
PAL_API(int)
PAL_FileHandleIsValid
(
    struct PAL_FILE *file
);

/* @summary Determine whether a filesystem entry is a file.
 * @param ent_info Information about a filesystem entry.
 * @return Non-zero of the specified information is associated with a file, or zero if the information is associated with some other type of entity.
 */
PAL_API(int)
PAL_IsFile
(
    struct PAL_FILE_INFO *ent_info
);

/* @summary Determine whether a filesystem entry is a directory.
 * @param ent_info Information about a filesystem entry.
 * @return Non-zero of the specified information is associated with a directory, or zero if the information is associated with some other type of entity.
 */
PAL_API(int)
PAL_IsDirectory
(
    struct PAL_FILE_INFO *ent_info
);

/* @summary Given a path to a filesystem entry, retrieve basic information about the file or directory.
 * @param result On return, the result structure is populated with information about the file system entity.
 * @param info Information about the file or directory to query. The Callback field is ignored.
 * @return Zero if the file system data was successfully retrieved, or non-zero if the operation failed.
 */
PAL_API(int)
PAL_FileStatPath
(
    struct PAL_FILE_STAT_RESULT *result, 
    struct PAL_FILE_STAT_DATA     *info
);

/* @summary Given a valid file handle, retrieve up-to-date basic information such as the file size.
 * @param ent_info On return, this structure is updated with information about the file.
 * @param file The file handle to query.
 * @return Zero if the file system data was successfully retrieved, or non-zero if the operation failed.
 */
PAL_API(int)
PAL_FileStatHandle
(
    struct PAL_FILE_INFO *ent_info,
    struct PAL_FILE          *file
);

/* @summary Open a file. The PAL_FILE_OPEN_HINT_PREALLOCATE flag is ignored - to pre-allocate a file, use PAL_FileCreate.
 * @param result On return, this structure is updated with information about the result of the operation.
 * @param data Information about the file to open. The Callback field is ignored.
 * @return Zero if the file was successfully opened, or non-zero if the operation failed.
 */
PAL_API(int)
PAL_FileOpen
(
    struct PAL_FILE_OPEN_RESULT *result, 
    struct PAL_FILE_OPEN_DATA     *data
);

/* @summary Create a file, optionally pre-allocating storage space for efficient sequential writing.
 * If the file exists, its current contents are destroyed. The file is opened with write access.
 * @param result On return, this structure is updated with information about the result of the operation.
 * @param data Information about the file to create. The Callback field is ignored.
 * @return Zero if the file was successfully created, or non-zero if the operation failed.
 */
PAL_API(int)
PAL_FileCreate
(
    struct PAL_FILE_CREATE_RESULT *result, 
    struct PAL_FILE_CREATE_DATA     *data
);

/* @summary Synchronously read data from a file into a host memory buffer.
 * @param result On return, this structure is updated with information about the result of the operation.
 * @param data Information about the operation to perform. The Callback field is ignored.
 * @return Zero if the read operation completed successfully, or non-zero if the operation failed.
 */
PAL_API(int)
PAL_FileRead
(
    struct PAL_FILE_READ_RESULT *result, 
    struct PAL_FILE_READ_DATA     *data
);

/* @summary Synchronously write data from a host memory buffer to a file.
 * @param result On return, this structure is updated with information about the result of the operation.
 * @param data Information about the operation to perform. The Callback field is ignored.
 * @return Zero if the write operation completed successfully, or non-zero if the operation failed.
 */
PAL_API(int)
PAL_FileWrite
(
    struct PAL_FILE_WRITE_RESULT *result, 
    struct PAL_FILE_WRITE_DATA     *data
);

/* @summary Flush any buffered writes for a given file to disk.
 * @param result On return, this structure is updated with information about the result of the operation.
 * @param data Information about the operation to perform. The Callback field is ignored.
 * @return Zero if the flush operation completed successfully, or non-zero if the operation failed.
 */
PAL_API(int)
PAL_FileFlush
(
    struct PAL_FILE_FLUSH_RESULT *result, 
    struct PAL_FILE_FLUSH_DATA     *data
);

/* @summary Close a file opened with PAL_FileOpen or PAL_FileCreate.
 * @param result On return, this structure is updated with information about the result of the operation.
 * @param data Information about the operation to perform. The Callback field is ignored.
 * @return Zero if the close operation completed successfully, or non-zero if the operation failed.
 */
PAL_API(int)
PAL_FileClose
(
    struct PAL_FILE_CLOSE_RESULT *result, 
    struct PAL_FILE_CLOSE_DATA     *data
);

#if 0
/* @summary Define the task entry point for a PAL_FileStatPath operation.
 * The filesystem operation executes asynchronously on the task worker pool. The worker thread is blocked for the duration of the operation.
 * The task data should point to an instance of PAL_FILE_STAT_DATA. Results are returned through the callback on the thread executing the task.
 * @param args A PAL_TASK_ARGS instance specifying data associated with the task and data used to spawn additional tasks.
 */
PAL_API(void)
PAL_FileStatPath_TaskMain
(
    struct PAL_TASK_ARGS *args
);

/* @summary Define the task entry point for a PAL_FileOpen operation.
 * The file is opened for asynchronous access. The synchronous I/O functions cannot be used.
 * The filesystem operation executes asynchronously on the task worker pool. The worker thread is blocked for the duration of the operation.
 * The task data should point to an instance of PAL_FILE_OPEN_DATA. Results are returned through the callback on the thread executing the task.
 * The returned file handle supports asynchronous read and write operations executed on the task pool.
 * @param args A PAL_TASK_ARGS instance specifying data associated with the task and data used to spawn additional tasks.
 */
PAL_API(void)
PAL_FileOpen_TaskMain
(
    struct PAL_TASK_ARGS *args
);

/* @summary Define the task entry point for a PAL_FileCreate operation.
 * The file is opened for asynchronous access. The synchronous I/O functions cannot be used.
 * The filesystem operation executes asynchronously on the task worker pool. The worker thread is blocked for the duration of the operation.
 * The task data should point to an instance of PAL_FILE_CREATE_DATA. Results are returned through the callback on the thread executing the task.
 * @param args A PAL_TASK_ARGS instance specifying data associated with the task and data used to spawn additional tasks.
 */
PAL_API(void)
PAL_FileCreate_TaskMain
(
    struct PAL_TASK_ARGS *args
);

/* @summary Define the task entry point for a PAL_FileRead operation.
 * The filesystem operation executes asynchronously on the task worker pool. 
 * The I/O operation may execute synchronously, in which case the worker thread is blocked for the duration of the operation.
 * If the I/O operation can be executed truly asynchronously, the worker thread submits the operation and returns. The task does not complete until some later point in time.
 * The task data should point to an instance of PAL_FILE_READ_DATA. Results are returned through the callback on the thread completing the task.
 * @param args A PAL_TASK_ARGS instance specifying data associated with the task and data used to spawn additional tasks.
 */
PAL_API(void)
PAL_FileRead_TaskMain
(
    struct PAL_TASK_ARGS *args
);

/* @summary Define the task entry point for a PAL_FileWrite operation.
 * The filesystem operation executes asynchronously on the task worker pool. 
 * The I/O operation may execute synchronously, in which case the worker thread is blocked for the duration of the operation.
 * If the I/O operation can be executed truly asynchronously, the worker thread submits the operation and returns. The task does not complete until some later point in time.
 * The task data should point to an instance of PAL_FILE_WRITE_DATA. Results are returned through the callback on the thread completing the task.
 * @param args A PAL_TASK_ARGS instance specifying data associated with the task and data used to spawn additional tasks.
 */
PAL_API(void)
PAL_FileWrite_TaskMain
(
    struct PAL_TASK_ARGS *args
);

/* @summary Define the task entry point for a PAL_FileFlush operation.
 * The filesystem operation executes asynchronously on the task worker pool. The worker thread is blocked for the duration of the operation.
 * The task data should point to an instance of PAL_FILE_FLUSH_DATA. Results are returned through the callback on the thread executing the task.
 * @param args A PAL_TASK_ARGS instance specifying data associated with the task and data used to spawn additional tasks.
 */
PAL_API(void)
PAL_FileFlush_TaskMain
(
    struct PAL_TASK_ARGS *args
);

/* @summary Define the task entry point for a PAL_FileClose operation.
 * The filesystem operation executes asynchronously on the task worker pool. The worker thread is blocked for the duration of the operation.
 * The task data should point to an instance of PAL_FILE_CLOSE_DATA. Results are returned through the callback on the thread executing the task.
 * @param args A PAL_TASK_ARGS instance specifying data associated with the task and data used to spawn additional tasks.
 */
PAL_API(void)
PAL_FileClose_TaskMain
(
    struct PAL_TASK_ARGS *args
);
#endif /* DISABLED */

#ifdef __cplusplus
}; /* extern "C" */
#endif

/* @summary Include the appropriate platform-specific header.
 */
#if   PAL_TARGET_PLATFORM == PAL_PLATFORM_WIN32 || PAL_TARGET_PLATFORM == PAL_PLATFORM_WINRT
    #include "pal_win32_file.h"
#elif PAL_TARGET_PLATFORM == PAL_PLATFORM_LINUX || PAL_TARGET_PLATFORM == PAL_PLATFORM_ANDROID
    #include "pal_linux_file.h"
#elif PAL_TARGET_PLATFORM == PAL_PLATFORM_MACOS || PAL_TARGET_PLATFORM == PAL_PLATFORM_IOS
    #include "pal_apple_file.h"
#else
    #error   pal_file.h: No implementation of the abstraction layer for your platform.
#endif

#endif /* __PAL_FILE_H__ */
