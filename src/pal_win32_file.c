/**
 * @summary Implement the API entry points from pal_file.h.
 */
#include "pal_win32_file.h"
#include "pal_win32_memory.h"

/* @summary Define the maximum number of characters, including the terminating nul, that can appear in a long-form filesystem path.
 */
#ifndef PAL_MAX_LONG_PATH_CHARS
#define PAL_MAX_LONG_PATH_CHARS   32768
#endif

/* @summary Define the maximum number of bytes that can be consumed by a long-form filesystem path.
 */
#ifndef PAL_MAX_LONG_PATH_BYTES
#define PAL_MAX_LONG_PATH_BYTES   (PAL_MAX_LONG_PATH_CHARS * sizeof(WCHAR))
#endif

/* @summary Convert a FILETIME structure to a Unix timestamp.
 * @param ft The FILETIME to convert.
 * @return The FILETIME timestamp expressed as a Unix timestamp.
 */
static PAL_INLINE pal_sint64_t
PAL_FILETIMEToUnixTime
(
    FILETIME ft
)
{
    ULARGE_INTEGER ui;
    ui.LowPart   = ft.dwLowDateTime;
    ui.HighPart  = ft.dwHighDateTime;
    return (pal_sint64_t)((ui.QuadPart / 10000000LL) - 11644473600LL);
}

/* @summary Convert a FILETIME expressed as a raw timestamp to a Unix timestamp.
 * @param filetime The FILETIME timestamp to convert.
 * @return The FILETIME timestamp expressed as a Unix timestamp.
 */
static PAL_INLINE pal_sint64_t
PAL_FILETIMEToUnixTimeRaw
(
    pal_sint64_t filetime
)
{   /* 10000000 is the number of 100ns intervals in one second.
     * 11644473600 is the number of seconds between Jan 1 1601 00:00 and 
     * Jan 1 1970 00:00 UTC (the epoch difference.)
     */
    return filetime / 10000000LL - 11644473600LL;
}

/* @summary Figure out the starting and ending points of the directory path, filename and extension information in a path string.
 * @param buf The zero-terminated path string to parse. Any forward slashes are converted to backslashes.
 * @param buf_end A pointer to the zero terminator character of the input path string. This value must be valid.
 * @param parts The OS_PATH_PARTS to update. The Root, RootEnd and PathFlags fields must be set.
 * @return This function always returns zero to indicate success.
 */
static int
PAL_ExtractNativePathParts
(
    struct PAL_PATH_PARTS *parts,
    pal_char_t         *path_buf, 
    pal_char_t         *path_end
)
{   /* initialize the components of the path parts to known values */
    parts->Path         = parts->RootEnd;
    parts->PathEnd      = parts->RootEnd;
    parts->Filename     = parts->RootEnd;
    parts->FilenameEnd  = parts->RootEnd;
    parts->Extension    = path_end;
    parts->ExtensionEnd = path_end;

    while (parts->FilenameEnd < path_end)
    {
        if (parts->FilenameEnd[0] == L'/')
            parts->FilenameEnd[0]  = L'\\';
        if (parts->FilenameEnd[0] == L'\\')
        {   /* encountered a path separator.
             * update the end of the directory path string.
             * reset the filename string to be zero-length.
             */
            parts->PathEnd     = parts->FilenameEnd;
            parts->PathFlags  |= PAL_PATH_FLAG_PATH;
            parts->Filename    = parts->FilenameEnd + 1;
            parts->FilenameEnd = parts->FilenameEnd + 1;
        }
        else
        {   /* this is a regular character; consider it part of the filename */
            parts->FilenameEnd++;
        }
    }
    if (parts->Path[0] == L'\\')
    {   /* skip the leading path separator */
        if (parts->Path == parts->PathEnd)
        {   /* there is no actual path component; this is something like "C:\" */
            parts->PathFlags &= ~PAL_PATH_FLAG_PATH;
            parts->PathEnd++;
        }
        parts->Path++;
    }

    if (parts->Filename != parts->FilenameEnd)
    {   /* figure out whether this last bit is a filename or part of the directory path */
        WCHAR *iter  = path_end;
        while (iter >= parts->Filename)
        {   /* consider 'a.b', '.a.b' and 'a.' to be a filename, but not '.a' */
            if (*iter == L'.' && iter != parts->Filename)
            {
                parts->FilenameEnd = iter;
                parts->Extension   = iter + 1;
                parts->PathFlags  |= PAL_PATH_FLAG_FILENAME;
                parts->PathFlags  |= PAL_PATH_FLAG_EXTENSION;
            }
            iter--;
        }
        if ((parts->PathFlags & PAL_PATH_FLAG_FILENAME) == 0)
        {   /* consider 'a' and '.a' to be part of the directory path information */
            parts->PathEnd     = parts->FilenameEnd;
            parts->PathFlags  |= PAL_PATH_FLAG_PATH;
            parts->Filename    = path_end;
            parts->FilenameEnd = path_end;
        }
    }
    else
    {   /* if there's no filename present, make sure that it points to an empty string */
        parts->Filename    = path_end;
        parts->FilenameEnd = path_end;
    }
    UNREFERENCED_PARAMETER(path_buf);
    return 0;
}

/* @summary Perform a possibly recursive search of a filesystem directory.
 * @param fsenum The PAL_FILE_ENUMERATOR associated with the search.
 * @param pathend Pointer to the nul at the end of the callers fsenum->AbsolutePath buffer.
 * @param srchend Pointer to the nul at the end of the callers fsenum->SearchPath buffer.
 * @param dirname A nul-terminated string specifying the name of the subdirectory to search.
 * @return Zero if the search completed successfully, greater than zero if the search was halted explicitly, or less than zero if the search was halted due to an error.
 */
static int
PAL_FileEnumeratorSearch
(
    struct PAL_FILE_ENUMERATOR *fsenum, 
    pal_char_t                *pathend,
    pal_char_t                *srchend,
    pal_char_t                *dirname
)
{
    HANDLE             fhand = INVALID_HANDLE_VALUE;
    pal_usize_t       dirlen = wcslen(dirname);
    pal_char_t      *newpend = pathend;
    pal_char_t      *newsend = srchend;
    PAL_FileEnum_Func  filfn = fsenum->FileCallback;
    PAL_FileEnum_Func  dirfn = fsenum->DirectoryCallback;
    pal_uintptr_t    context = fsenum->OpaqueData;
    pal_uint32_t     do_dirs = fsenum->SearchFlags & PAL_FILE_ENUMERATOR_FLAG_DIRECTORIES;
    pal_uint32_t    do_files = fsenum->SearchFlags & PAL_FILE_ENUMERATOR_FLAG_FILES;
    pal_uint32_t  do_recurse = fsenum->SearchFlags & PAL_FILE_ENUMERATOR_FLAG_RECURSIVE;
    int               result = 0;
    ULARGE_INTEGER     fsize;
    WIN32_FIND_DATAW   fdata;
    PAL_FILE_INFO      finfo;

    if (dirlen > 0)
    {   /* append the directory name to the end of the absolute and search path */
        if (newpend[-1] != L'\\' && newpend[-1] != L'/')
            *newpend++   = L'\\';
        if (newsend[-1] != L'\\' && newsend[-1] != L'/')
            *newsend++   = L'\\';
        PAL_CopyMemory(newpend, dirname, (dirlen+1) * sizeof(pal_char_t));
        PAL_CopyMemory(newsend, dirname, (dirlen+1) * sizeof(pal_char_t));
    }
    /* append the filter pattern to the filter string - use a wildcard to find everything */
    newpend     = newpend + dirlen; *newpend++ = L'\\'; *newpend = L'\0';
    newsend     = newsend + dirlen; *newsend++ = L'\\'; *newsend++ = L'*'; *newsend = L'\0';
    /* open the search and process all entries under the current directory */
    if ((fhand  = FindFirstFileExW(fsenum->SearchPath, FindExInfoBasic, &fdata, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH)) == INVALID_HANDLE_VALUE)
    {    /* the find could not be opened - perhaps the user doesn't have permissions.
            restore the path buffers to their original states. */
       *pathend = L'\0';
       *srchend = L'\0';
        return -1;
    }
    do
    {   /* process the current result */
        if (fdata.dwFileAttributes & FILE_ATTRIBUTE_DEVICE)
            continue;
        if (fdata.dwFileAttributes & FILE_ATTRIBUTE_VIRTUAL)
            continue;
        if (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {    /* always skip . and .. */
            if (fdata.cFileName[0] == L'.' && fdata.cFileName[1] == L'\0')
                continue;
            if (fdata.cFileName[0] == L'.' && fdata.cFileName[1] == L'.' && fdata.cFileName[2] == L'\0')
                continue;
            if (do_dirs)
            {   /* populate the PAL_FILE_INFO and report */
                finfo.FileSize     = 0;
                finfo.CreationTime = PAL_FILETIMEToUnixTime(fdata.ftCreationTime);
                finfo.AccessTime   = PAL_FILETIMEToUnixTime(fdata.ftLastAccessTime);
                finfo.WriteTime    = PAL_FILETIMEToUnixTime(fdata.ftLastWriteTime);
                finfo.Alignment    = 0;
                finfo.Attributes   = fdata.dwFileAttributes;
                (void) StringCchCatW(newpend, PAL_MAX_LONG_PATH_CHARS, fdata.cFileName);
                if (dirfn(fsenum, fsenum->AbsolutePath, fsenum->RelativePath, fdata.cFileName, &finfo, context) == 0)
                {   /* the caller wants to stop enumeration */
                   *pathend = L'\0'; *srchend = L'\0';
                    FindClose(fhand);
                    return 0;
                } *newpend = L'\0';
            }
            if (do_recurse)
            {   /* recurse into this subdirectory - newsend-1 makes it point at the * */
                if ((result = PAL_FileEnumeratorSearch(fsenum, newpend, newsend-1, fdata.cFileName)) != 0)
                {   /* the search failed or was halted somewhere down below. clean up. */
                   *pathend = L'\0'; *srchend = L'\0';
                    FindClose(fhand);
                    return result;
                }
            }
        }
        else if (do_files) 
        {   /* populate the PAL_FILE_INFO and report */
            fsize.LowPart      = fdata.nFileSizeLow;
            fsize.HighPart     = fdata.nFileSizeHigh;
            finfo.FileSize     =(pal_sint64_t) fsize.QuadPart;
            finfo.CreationTime = PAL_FILETIMEToUnixTime(fdata.ftCreationTime);
            finfo.AccessTime   = PAL_FILETIMEToUnixTime(fdata.ftLastAccessTime);
            finfo.WriteTime    = PAL_FILETIMEToUnixTime(fdata.ftLastWriteTime);
            finfo.Alignment    = 4096;
            finfo.Attributes   = fdata.dwFileAttributes;
            (void) StringCchCatW(newpend, PAL_MAX_LONG_PATH_CHARS, fdata.cFileName);
            if (filfn(fsenum, fsenum->AbsolutePath, fsenum->RelativePath, fdata.cFileName, &finfo, context) == 0)
            {   /* the caller wants to stop enumeration */
               *pathend = L'\0'; *srchend = L'\0';
                FindClose(fhand);
                return 0;
            } *newpend = L'\0';
        }
    } while (FindNextFile(fhand, &fdata));
    /* GetLastError should be ERROR_NO_MORE_FILES */
    /* restore the path strings to their original values */
   *pathend = L'\0'; *srchend = L'\0';
    FindClose(fhand);
    return 0;
}

PAL_API(int)
PAL_PathParse
(
    struct PAL_PATH_PARTS *parts, 
    pal_char_t         *path_buf, 
    pal_char_t         *path_end
)
{
    pal_usize_t inp_chars = 0; /* the number of characters in string buf, not including nul terminator */
    pal_usize_t inp_bytes = 0; /* the number of bytes in string buf, including nul terminator */

    /* initialize the output to valid zero-length strings */
    parts->Root      = path_buf; parts->RootEnd      = path_end;
    parts->Path      = path_end; parts->PathEnd      = path_end;
    parts->Filename  = path_end; parts->FilenameEnd  = path_end;
    parts->Extension = path_end; parts->ExtensionEnd = path_end;

    /* figure out the length of the input path string */
    if (path_end != NULL && path_end > path_buf)
    {   /* path_end points to the nul of the existing string */
        inp_bytes = ((pal_uint8_t*) path_end) - ((pal_uint8_t*)path_buf);
        inp_chars = (path_end - 1 - path_buf);
    }
    else if (path_buf != NULL)
    {   /* determine the length of the input by scanning for the zero terminator */
        (void) StringCchLengthW(path_buf, PAL_MAX_LONG_PATH_CHARS, &inp_chars);
        path_end  = path_buf  + inp_chars;
        inp_bytes =(inp_chars + 1) * sizeof(pal_char_t); 
    }
    if (path_buf == NULL || inp_chars < 1)
    {   /* the input buffer is invalid; there's nothing to parse */
        parts->PathFlags = PAL_PATH_FLAGS_INVALID;
        return -1;
    }
    if (inp_chars >= 3)
    {
        if (path_buf[0] == L'\\' && path_buf[1] == L'\\')
        {   /* absolute path; may be device, UNC, long device, long UNC, or long DOS */
            if ((inp_chars >= 5) && (path_buf[2] == L'?') && (path_buf[3] == L'\\'))
            {   /* may be long UNC or long DOS */
                if ((inp_chars >= 6) && ((path_buf[4] >= L'A' && path_buf[4] <= L'Z') || (path_buf[4] >= L'a' && path_buf[4] <= L'z')) && (path_buf[5] == L':'))
                {   /* long DOS path */
                    parts->Root      = path_buf + 4;
                    parts->RootEnd   = path_buf + 6;
                    parts->PathFlags = PAL_PATH_FLAG_ABSOLUTE | PAL_PATH_FLAG_LONG | PAL_PATH_FLAG_ROOT;
                    return PAL_ExtractNativePathParts(parts, path_buf, path_end);
                }
                else if ((inp_chars >= 6) && (path_buf[4] == L'.' && path_buf[5] == L'\\'))
                {   /* long device path */
                    parts->Root      = path_buf + 6;
                    parts->RootEnd   = path_buf + 6;
                    parts->PathFlags = PAL_PATH_FLAG_ABSOLUTE | PAL_PATH_FLAG_LONG | PAL_PATH_FLAG_DEVICE | PAL_PATH_FLAG_ROOT;
                    goto scan_for_end_of_root;
                }
                else
                {   /* long UNC path */
                    parts->Root      = path_buf + 4;
                    parts->RootEnd   = path_buf + 4; 
                    parts->PathFlags = PAL_PATH_FLAG_ABSOLUTE | PAL_PATH_FLAG_LONG | PAL_PATH_FLAG_NETWORK | PAL_PATH_FLAG_ROOT;
                    goto scan_for_end_of_root;
                }
            }
            else if ((inp_chars >= 5) && (path_buf[2] == L'.') && (path_buf[3] == L'\\'))
            {   /* device path, limit MAX_PATH characters */
                parts->Root      = path_buf + 4;
                parts->RootEnd   = path_buf + 4;
                parts->PathFlags = PAL_PATH_FLAG_ABSOLUTE | PAL_PATH_FLAG_DEVICE | PAL_PATH_FLAG_ROOT;
                goto scan_for_end_of_root;
            }
            else
            {   /* UNC path, limit MAX_PATH characters */
                parts->Root      = path_buf + 2;
                parts->RootEnd   = path_buf + 2;
                parts->PathFlags = PAL_PATH_FLAG_ABSOLUTE | PAL_PATH_FLAG_NETWORK | PAL_PATH_FLAG_ROOT;
                goto scan_for_end_of_root;
            }
        }
        else if (path_buf[0] == L'\\' || path_buf[0] == L'/')
        {   /* absolute path, with a root of '\' (MSDN says this is valid?) */
            if (path_buf[0] == L'/')
                path_buf[0] = L'\\';
            parts->Root      = path_buf;
            parts->RootEnd   = path_buf + 1;
            parts->PathFlags = PAL_PATH_FLAG_ABSOLUTE | PAL_PATH_FLAG_ROOT;
            return PAL_ExtractNativePathParts(parts, path_buf, path_end);
        }
        else if (((path_buf[0] >= L'A' && path_buf[0] <= L'Z') || (path_buf[0] >= L'a' && path_buf[0] <= L'z')) && (path_buf[1] == L':'))
        {   /* absolute DOS path with a drive letter root */
            parts->Root      = path_buf;
            parts->RootEnd   = path_buf + 2;
            parts->PathFlags = PAL_PATH_FLAG_ABSOLUTE | PAL_PATH_FLAG_ROOT;
            return PAL_ExtractNativePathParts(parts, path_buf, path_end);
        }
        else
        {   /* assume that this is a relative path */
            parts->Root      = path_buf;
            parts->RootEnd   = path_buf;
            parts->PathFlags = PAL_PATH_FLAG_RELATIVE;
            return PAL_ExtractNativePathParts(parts, path_buf, path_end);
        }
    }
    else if (inp_chars == 2)
    {   /* C:, .., .\, aa, .a, etc. */
        if (((path_buf[0] >= L'A' && path_buf[0] <= L'Z') || (path_buf[0] >= L'a' && path_buf[0] <= L'z')) && (path_buf[1] == L':'))
        {   /* absolute DOS path with drive letter root; no path information */
            parts->Root      = path_buf;
            parts->RootEnd   = path_buf + 2;
            parts->PathFlags = PAL_PATH_FLAG_ABSOLUTE | PAL_PATH_FLAG_ROOT;
            return 0;
        }
        /* else, assume relative path, directory path info only */
        parts->Root      = path_buf;
        parts->RootEnd   = path_buf;
        parts->Path      = path_buf;
        parts->PathFlags = PAL_PATH_FLAG_RELATIVE | PAL_PATH_FLAG_PATH;
        if (path_buf[0] == L'.' && (path_buf[1] == L'\\' || path_buf[1] == L'/'))
        {   /* relative path, directory path info only */
            if (path_buf[1] == L'/')
                path_buf[1] = L'\\';
            parts->PathEnd = path_buf + 1;
        }
        else
        {   /* assume this is a relative directory path */
            parts->PathEnd = path_buf + 2;
        }
        return 0;
    }
    else
    {   /* /, ., a, etc. */
        if (path_buf[0] == L'/')
            path_buf[0] = L'\\';
        if (path_buf[0] == L'\\')
        {   /* treat this as an absolute path, the root of the filesystem */
            parts->Root      = path_buf;
            parts->RootEnd   = path_buf;
            parts->Path      = path_buf;
            parts->PathEnd   = path_buf + 1;
            parts->PathFlags = PAL_PATH_FLAG_ABSOLUTE | PAL_PATH_FLAG_PATH;
        }
        else
        {   /* assume this is a relative path, directory info only */
            parts->Root      = path_buf;
            parts->RootEnd   = path_buf;
            parts->Path      = path_buf;
            parts->PathEnd   = path_buf + 1;
            parts->PathFlags = PAL_PATH_FLAG_RELATIVE | PAL_PATH_FLAG_PATH;
        }
        return 0;
    }

scan_for_end_of_root:
    while (parts->RootEnd < path_end)
    {
        if (parts->RootEnd[0] == L'\\')
            break;
        if (parts->RootEnd[0] == L'/')
        {
            parts->RootEnd[0] = L'\\';
            break;
        }
        parts->RootEnd++;
    }
    if (parts->RootEnd == path_end)
    {   /* no additional components will be found */
        return 0;
    }
    return PAL_ExtractNativePathParts(parts, path_buf, path_end);
}

PAL_API(int)
PAL_PathForFile
(
    pal_char_t       *buffer, 
    pal_usize_t  buffer_size,
    pal_char_t  **buffer_end, 
    pal_usize_t *buffer_need, 
    struct PAL_FILE    *file
)
{
    pal_usize_t chars_written = 0;
    pal_usize_t  buffer_chars = buffer_size / sizeof(WCHAR);
    HANDLE             handle = file->Handle;

    if (buffer_chars >= 1 && buffer != NULL)
    {   /* GetFinalPathNameByHandle expects cchFilePath to not include the nul */
        buffer_chars--;
    }
    /* GetFinalPathNameByHandle returns the required buffer length, in WCHARs, including nul-terminator */
    if ((chars_written = GetFinalPathNameByHandleW(handle, buffer, (DWORD) buffer_chars, VOLUME_NAME_DOS | FILE_NAME_NORMALIZED)) != 0)
    {   /* it's possible that the caller was just asking for the buffer size */
        if (buffer == NULL || buffer_size == 0)
        {   /* the caller was just asking for the buffer size - chars_written includes the nul */
            if (buffer_need != NULL)
               *buffer_need  =(chars_written * sizeof(WCHAR));
            return 0;
        }
        if (buffer_end != NULL)
        {   /* chars_written does NOT include the nul */
           *buffer_end  = buffer + chars_written;
        }
        if (buffer_need != NULL)
        {   /* chars_written does NOT include the nul */
           *buffer_need  =(chars_written+1) * sizeof(WCHAR); 
        }
        return 0;
    }
    else
    {   /* GetFinalPathNameByHandle failed */
        if (buffer != NULL && buffer_chars >= 1) *buffer = L'\0';
        if (buffer_end != NULL) *buffer_end = buffer;
        return -1;
    }
}

PAL_API(int)
PAL_PathAppend
(
    pal_char_t       *buffer, 
    pal_usize_t  buffer_size,
    pal_char_t  **buffer_end, 
    pal_usize_t *buffer_need, 
    pal_char_t const *append
)
{
    pal_char_t const *app_end = NULL;
    pal_char_t       *inp_end = NULL;
    pal_usize_t     inp_chars = 0; /* the number of characters in string buf, not including nul terminator */
    pal_usize_t     inp_bytes = 0; /* the number of bytes in string buf, including nul terminator */
    pal_usize_t     app_chars = 0; /* the number of characters in string append, not including nul terminator */
    pal_usize_t     app_bytes = 0; /* the number of bytes in string append, including nul terminator */
    pal_usize_t     sep_chars = 0; /* 1 if a path separator is needed */
    pal_usize_t  bytes_needed = 0;

    if (buffer_end != NULL && *buffer_end != NULL && *buffer_end > buffer)
    {   /* *buf_end points to the zero-terminator of the existing string */
        inp_end    = *buffer_end;
        inp_bytes  =((pal_uint8_t*) *buffer_end) - ((pal_uint8_t*) buffer);
        inp_chars  =(*buffer_end - buffer);
    }
    else if (buffer != NULL && buffer_size > sizeof(pal_char_t))
    {   /* determine the length of the input by scanning for the nul terminator */
        if (SUCCEEDED(StringCchLengthW(buffer, PAL_MAX_LONG_PATH_CHARS, &inp_chars)))
        {
            if (buffer_end != NULL)
               *buffer_end  = buffer + inp_chars;
            inp_end   = buffer + inp_chars;
            inp_bytes =(inp_chars + 1) * sizeof(pal_char_t); 
        }
        else
        {   /* unable to determine the length of the input string */
            if (buffer_end  != NULL)
               *buffer_end   = buffer;
            if (buffer_need != NULL)
               *buffer_need  = 0;
            return -1;
        }
    }
    else
    {   /* no input buffer was supplied, which is unusual */
        if (buffer_end  != NULL)
           *buffer_end   = NULL;
        if (buffer_need != NULL)
           *buffer_need  = 0;
        return -1;
    }
    if (append != NULL)
    {   /* retieve the length of the string to append */
        if (SUCCEEDED(StringCchLengthW(append, PAL_MAX_LONG_PATH_CHARS, &app_chars)))
        {
            app_end   = append + app_chars;
            app_bytes =(app_chars + 1) * sizeof(pal_char_t);
        }
        else
        {   /* unable to determine the length of the fragment string */
            if (buffer_end  != NULL)
               *buffer_end   = inp_end;
            if (buffer_need != NULL)
               *buffer_need  = inp_bytes;
            return -1;
        }
    }
    else
    {   // there's no string to append, so just return data for the existing buffer.
        if (buffer_end  != NULL)
           *buffer_end   = inp_end;
        if (buffer_need != NULL)
           *buffer_need  = inp_bytes;
        return 0;
    }
    if (*(inp_end - 1) != L'\\' && *(inp_end - 1) != L'/')
    {   /* a trailing directory separator is required */
        sep_chars = 1;
    }
    else if (*(inp_end - 1) == L'/')
    {   /* normalize the trailing separator to the OS-preferred format */
        *(inp_end - 1) = L'\\';
    }
    
    /* we have enough information to determine the number of bytes required in the destination buffer */
    bytes_needed    =(inp_chars + sep_chars + app_chars + 1) * sizeof(pal_char_t);
    if (buffer_size < bytes_needed)
    {   /* insufficient space in the input buffer - this is an expected outcome */
        if (buffer_end  != NULL)
           *buffer_end   = inp_end;
        if (buffer_need != NULL)
           *buffer_need  = bytes_needed;
        return 0;
    }
    if (sep_chars > 0)
    {   /* append a directory separator character */
        *inp_end++ = L'\\';
    }
    /* append the fragment to the native path string */
    while (append < app_end)
    {
        if (*append != L'/')
        {   /* append the character to the destination buffer as-is */
            *inp_end++ = *append++;
        }
        else
        {   /* convert to the preferred native path separator character */
            *inp_end++ = L'\\';
            ++append;
        }
    }
    /* nul-terminate the string */
    *inp_end = L'\0';
    /* all finished; set output parameters */
    if (buffer_end  != NULL)
       *buffer_end   = inp_end;
    if (buffer_need != NULL)
       *buffer_need  = bytes_needed; 
    return 0;
}

PAL_API(int)
PAL_PathChangeExtension
(
    pal_char_t              *buffer, 
    pal_usize_t         buffer_size,
    pal_char_t         **buffer_end, 
    pal_usize_t        *buffer_need, 
    pal_char_t const *new_extension
)
{
    pal_char_t const *ext_end = NULL;
    pal_char_t       *inp_end = NULL;
    pal_char_t       *inp_ext = NULL;
    pal_char_t       *inp_itr = NULL;
    pal_usize_t     inp_chars = 0; /* the number of characters in string buf, not including nul terminator */
    pal_usize_t     inp_bytes = 0; /* the number of bytes in string buf, including nul terminator */
    pal_usize_t     ext_chars = 0; /* the number of characters in string append, not including nul terminator */
    pal_usize_t     ext_bytes = 0; /* the number of bytes in string append, including nul terminator */
    pal_usize_t     sep_chars = 0; /* 1 if an extension separator is needed */
    pal_usize_t  bytes_needed = 0;

    if (buffer_end != NULL && *buffer_end != NULL && *buffer_end > buffer)
    {   /* *buf_end points to the zero-terminator of the existing string */
        inp_end    = *buffer_end;
        inp_bytes  =((pal_uint8_t*) *buffer_end) - ((pal_uint8_t*) buffer);
        inp_chars  =(*buffer_end - buffer);
    }
    else if (buffer != NULL && buffer_size > sizeof(pal_char_t))
    {   /* determine the length of the input by scanning for the nul terminator */
        if (SUCCEEDED(StringCchLengthW(buffer, PAL_MAX_LONG_PATH_CHARS, &inp_chars)))
        {
            if (buffer_end != NULL)
               *buffer_end  = buffer + inp_chars;
            inp_end   = buffer + inp_chars;
            inp_bytes =(inp_chars + 1) * sizeof(pal_char_t); 
        }
        else
        {   /* unable to determine the length of the input string */
            if (buffer_end  != NULL)
               *buffer_end   = buffer;
            if (buffer_need != NULL)
               *buffer_need  = 0; 
            return -1;
        }
    }
    else
    {   /* no input buffer was supplied, which is unusual */
        if (buffer_end  != NULL)
           *buffer_end   = NULL;
        if (buffer_need != NULL)
           *buffer_need  = 0; 
        return -1;
    }

    /* find the first period after the last path separator */
    inp_itr = inp_end - 1;
    while (inp_itr > buffer)
    {
        if (*inp_itr == L'.')
        {   // save the position of the extension separator.
            inp_ext = inp_itr;
        }
        else if (*inp_itr == L'\\' || *inp_itr == L'/')
        {   // found a path separator; terminate the search.
            break;
        }
        inp_itr--;
    }
    if (inp_ext == NULL)
    {   /* the input string currently has no extension, so append one */
        inp_ext = inp_end;
    }

    if (new_extension == NULL || *new_extension == L'\0')
    {   /* the current extension is being removed; truncate the existing string */
        if (buffer_end  != NULL)
           *buffer_end   = inp_ext;
        if (buffer_need != NULL)
           *buffer_need  =((pal_uint8_t*) inp_ext) - ((pal_uint8_t*) buffer); 
        *inp_ext = L'\0';
        return 0;
    }
    else
    {   /* a new extension is being appended; retrieve its length */
        if (SUCCEEDED(StringCchLengthW(new_extension, PAL_MAX_LONG_PATH_CHARS, &ext_chars)))
        {
            ext_end   = new_extension  + ext_chars;
            ext_bytes =(ext_chars + 1) * sizeof(pal_char_t);
        }
        else
        {   /* unable to determine the length of the new extension string */
            if (buffer_end  != NULL)
               *buffer_end   = inp_end;
            if (buffer_need != NULL)
               *buffer_need  = inp_bytes; 
            return -1;
        }
    }
    if (*new_extension != L'.')
    {   /* a leading extension separator is required */
        sep_chars = 1;
    }

    /* we have enough information to determine the number of bytes required in the destination buffer */
    bytes_needed    =(inp_chars - (inp_end - inp_ext) + sep_chars + ext_chars + 1) * sizeof(pal_char_t);
    if (buffer_size < bytes_needed)
    {   /* insufficient space in the input buffer - this is an expected occurrence */
        if (buffer_end  != NULL)
           *buffer_end   = inp_end;
        if (buffer_need != NULL)
           *buffer_need  = bytes_needed; 
        return 0;
    }
    if (sep_chars > 0)
    {   /* append an extension separator character */
        *inp_ext++ = L'.';
    }
    /* append the extension to the native path string, including the nul-terminator */
    PAL_CopyMemory(inp_ext, new_extension, ext_bytes);
    inp_ext += ext_chars;
    /* all finished; set output parameters */
    if (buffer_end  != NULL) 
       *buffer_end   = inp_ext;
    if (buffer_need != NULL)
       *buffer_need  = bytes_needed; 
    return 0;
}

PAL_API(int)
PAL_PathAppendExtension
(
    pal_char_t          *buffer, 
    pal_usize_t     buffer_size,
    pal_char_t     **buffer_end, 
    pal_usize_t    *buffer_need, 
    pal_char_t const *extension
)
{
    pal_char_t const *ext_end = NULL;
    pal_char_t       *inp_end = NULL;
    pal_usize_t     inp_chars = 0; /* the number of characters in string buf, not including nul terminator */
    pal_usize_t     inp_bytes = 0; /* the number of bytes in string buf, including nul terminator */
    pal_usize_t     ext_chars = 0; /* the number of characters in string append, not including nul terminator */
    pal_usize_t     ext_bytes = 0; /* the number of bytes in string append, including nul terminator */
    pal_usize_t     sep_chars = 0; /* 1 if an extension separator is needed */
    pal_usize_t  bytes_needed = 0;

    if (buffer_end != NULL && *buffer_end != NULL && *buffer_end > buffer)
    {   /* *buffer_end points to the nul-terminator of the existing string */
        inp_end    = *buffer_end;
        inp_bytes  =((pal_uint8_t*) *buffer_end) - ((pal_uint8_t*) buffer);
        inp_chars  =(*buffer_end - buffer);
    }
    else if (buffer != NULL && buffer_size > sizeof(pal_char_t))
    {   /* determine the length of the input by scanning for the nul terminator */
        if (SUCCEEDED(StringCchLengthW(buffer, PAL_MAX_LONG_PATH_CHARS, &inp_chars)))
        {
            if (buffer_end  != NULL)
               *buffer_end   = buffer + inp_chars;
            inp_end   = buffer + inp_chars;
            inp_bytes =(inp_chars + 1) * sizeof(pal_char_t); 
        }
        else
        {   /* unable to determine the length of the input string */
            if (buffer_end  != NULL)
               *buffer_end   = buffer;
            if (buffer_need != NULL)
               *buffer_need  = 0; 
            return -1;
        }
    }
    else
    {   /* no input buffer was supplied, which is unusual */
        if (buffer_end  != NULL)
           *buffer_end   = NULL;
        if (buffer_need != NULL)
           *buffer_need  = 0; 
        return -1;
    }

    if (extension == NULL || *extension == L'\0')
    {   /* no extension was supplied; don't make any changes */
        if (buffer_end  != NULL)
           *buffer_end   = inp_end;
        if (buffer_need != NULL)
           *buffer_need  = inp_bytes; 
        return 0;
    }
    else
    {   // a new extension is being appended; retrieve its length.
        if (SUCCEEDED(StringCchLengthW(extension, PAL_MAX_LONG_PATH_CHARS, &ext_chars)))
        {
            ext_end   = extension + ext_chars;
            ext_bytes =(ext_chars + 1) * sizeof(pal_char_t);
        }
        else
        {   /* unable to determine the length of the new extension string */
            if (buffer_end  != NULL)
               *buffer_end   = inp_end;
            if (buffer_need != NULL)
               *buffer_need  = inp_bytes; 
            return -1;
        }
    }
    if (*extension != L'.')
    {   /* a leading extension separator is required */
        sep_chars = 1;
    }

    /* we have enough information to determine the number of bytes required in the destination buffer */
    bytes_needed    =(inp_chars + sep_chars + ext_chars + 1) * sizeof(pal_char_t);
    if (buffer_size < bytes_needed)
    {   /* insufficient space in the input buffer - this is an expected occurrence */
        if (buffer_end  != NULL)
           *buffer_end   = inp_end;
        if (buffer_need != NULL)
           *buffer_need  = bytes_needed; 
        return 0;
    }
    if (sep_chars > 0)
    {   /* append an extension separator character */
        *inp_end++ = L'.';
    }
    /* append the extension to the native path string, including the nul-terminator */
    PAL_CopyMemory(inp_end, extension, ext_bytes);
    inp_end += ext_chars;
    /* all finished; set output parameters */
    if (buffer_end  != NULL)
       *buffer_end   = inp_end;
    if (buffer_need != NULL)
       *buffer_need  = bytes_needed; 
    return 0;
}

PAL_API(int)
PAL_DirectoryCreate
(
    pal_char_t *path
)
{
    pal_char_t *path_itr = NULL;
    pal_char_t *path_end = NULL;
    pal_char_t  *dir_end = NULL;
    PAL_PATH_PARTS parts;

    /* parse the input path into its constituient parts */
    if (PAL_PathParse(&parts, path, NULL) < 0)
    {   /* the input path cannot be parsed */
        return -1;
    }
    if (parts.PathFlags & PAL_PATH_FLAG_RELATIVE)
    {   /* a relative path was supplied - this isn't supported (not thread safe) */
        return -1;
    }

    /* build up the path one directory at a time - 
     * PAL_PathParse normalized the directory separators to '\'
     * find each one and temporarily replace it with a nul
     */
    path_itr = parts.RootEnd;
    path_end = parts.PathEnd;
    dir_end  = parts.Path;
    do
    {   /* find the next directory separator */
        int restore = 0;
        while (dir_end <= path_end)
        {
            if (*dir_end == L'\\')
            {   /* replace it with a nul */
               *dir_end = L'\0';
                restore = 1;
                break;
            }
            dir_end++;
        }
        /* attempt to create the directory */
        if (CreateDirectoryW(path, NULL) == FALSE && GetLastError() != ERROR_ALREADY_EXISTS)
        {   /* the directory could not be created */
            if (restore)
            {   /* restore the path string and fail */
                *dir_end = L'\\';
            }
            return -1;
        }
        if (restore)
        {   /* restore the path string and continue scanning */
           *dir_end++ = L'\\';
        }
    } while (dir_end < path_end);

    return 0;
}

PAL_API(pal_usize_t)
PAL_FileEnumeratorQueryMemorySize
(
    void
)
{   /* a filesystem enumerator requires two long path buffers - 
       one for the AbsolutePath, and one to use as a temporary buffer for 
       holding the search string (C:\path\to\search\*). this is 128KB total.*/
    return (pal_usize_t)(PAL_MAX_LONG_PATH_BYTES * 2);
}

PAL_API(int)
PAL_FileEnumeratorCreate
(
    struct PAL_FILE_ENUMERATOR    *fsenum, 
    struct PAL_FILE_ENUMERATOR_INIT *init
)
{
    PAL_MEMORY_ARENA           arena;
    PAL_MEMORY_ARENA_INIT arena_init;
    pal_usize_t        required_size;
    pal_char_t             *abs_path = NULL;
    pal_char_t             *flt_path = NULL;
    HANDLE                        fd = INVALID_HANDLE_VALUE;
    DWORD                     access = GENERIC_READ;
    DWORD                      share = FILE_SHARE_READ | FILE_SHARE_WRITE;
    DWORD                     create = OPEN_EXISTING;
    DWORD                      flags = FILE_FLAG_BACKUP_SEMANTICS;
    DWORD                     nchars = 0;

    if (init->StartPath == NULL)
    {   /* the caller must supply an initial path */
        assert(init->StartPath != NULL);
        PAL_ZeroMemory(fsenum, sizeof(PAL_FILE_ENUMERATOR));
        fsenum->DirectoryHandle = INVALID_HANDLE_VALUE;
        return -1;
    }
    if (init->FileCallback == NULL && (init->SearchFlags & PAL_FILE_ENUMERATOR_FLAG_FILES))
    {   /* the caller must supply a file callback */
        assert(init->FileCallback != NULL);
        PAL_ZeroMemory(fsenum, sizeof(PAL_FILE_ENUMERATOR));
        fsenum->DirectoryHandle = INVALID_HANDLE_VALUE;
        return -1;
    }
    if (init->DirectoryCallback == NULL && (init->SearchFlags & PAL_FILE_ENUMERATOR_FLAG_DIRECTORIES))
    {   /* the caller must supply a directory callback */
        assert(init->DirectoryCallback != NULL);
        PAL_ZeroMemory(fsenum, sizeof(PAL_FILE_ENUMERATOR));
        fsenum->DirectoryHandle = INVALID_HANDLE_VALUE;
        return -1;
    }
    if (init->MemoryStart == NULL || init->MemorySize == 0)
    {   /* the caller must supply memory for the storage object data */
        assert(init->MemoryStart != NULL);
        assert(init->MemorySize > 0);
        PAL_ZeroMemory(fsenum, sizeof(PAL_FILE_ENUMERATOR));
        fsenum->DirectoryHandle = INVALID_HANDLE_VALUE;
        return -1;
    }

    required_size = PAL_FileEnumeratorQueryMemorySize();
    if (init->MemorySize < required_size)
    {   /* the caller must supply sufficient memory for the storage object */
        assert(init->MemorySize >= required_size);
        PAL_ZeroMemory(fsenum, sizeof(PAL_FILE_ENUMERATOR));
        fsenum->DirectoryHandle = INVALID_HANDLE_VALUE;
        return -1;
    }

    /* initialize the path buffers to point to the supplied memory */
    arena_init.AllocatorName = __FUNCTION__;
    arena_init.AllocatorType = PAL_MEMORY_ALLOCATOR_TYPE_HOST;
    arena_init.MemoryStart   =(pal_uint64_t) init->MemoryStart;
    arena_init.MemorySize    =(pal_uint64_t) init->MemorySize;
    arena_init.UserData      = NULL;
    arena_init.UserDataSize  = 0;
    if (PAL_MemoryArenaCreate(&arena, &arena_init) < 0)
    {   /* failed to initialize the memory arena */
        PAL_ZeroMemory(fsenum, sizeof(PAL_FILE_ENUMERATOR));
        fsenum->DirectoryHandle = INVALID_HANDLE_VALUE;
        return -1;
    }
    abs_path = PAL_MemoryArenaAllocateHostArray(&arena, pal_char_t, PAL_MAX_LONG_PATH_CHARS);
    flt_path = PAL_MemoryArenaAllocateHostArray(&arena, pal_char_t, PAL_MAX_LONG_PATH_CHARS);

    /* open the directory for read access, but don't allow anyone else to delete it */
    if ((fd = CreateFileW(init->StartPath, access, share, NULL, create, flags, NULL)) != INVALID_HANDLE_VALUE)
    {   /* retrieve the absolute path of the search start directory */
        if ((nchars = GetFinalPathNameByHandleW(fd, abs_path, PAL_MAX_LONG_PATH_CHARS-1, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS)) == 0)
        {   /* the path name could not be retrieved - perhaps a permissions issue */
            PAL_ZeroMemory(fsenum, sizeof(PAL_FILE_ENUMERATOR));
            fsenum->DirectoryHandle = INVALID_HANDLE_VALUE;
            CloseHandle(fd);
            return -1;
        }
        /* copy that path to the filter path buffer - include the nul */
        PAL_CopyMemory(flt_path, abs_path, (nchars+1) * sizeof(pal_char_t));
        /* append a second nul to the AbsolutePath buffer - RelativePath will point to that */
       *(abs_path+nchars+1)        = L'\0'; 
        fsenum->DirectoryHandle    = fd;
        fsenum->FileCallback       = init->FileCallback;
        fsenum->DirectoryCallback  = init->DirectoryCallback;
        fsenum->OpaqueData         = init->OpaqueData;
        fsenum->AbsolutePath       = abs_path;
        fsenum->RelativePath       = abs_path + nchars + 1;
        fsenum->SearchPath         = flt_path;
        fsenum->SearchEnd          = flt_path + nchars;
        fsenum->SearchFlags        = init->SearchFlags;
        fsenum->BasePathLength     =(pal_uint32_t) nchars;
        fsenum->MemoryStart        = init->MemoryStart;
        fsenum->MemorySize         = init->MemorySize;
        return 0;
    }
    else
    {   /* could not open the directory - it may not exist */
        PAL_ZeroMemory(fsenum, sizeof(PAL_FILE_ENUMERATOR));
        fsenum->DirectoryHandle = INVALID_HANDLE_VALUE;
        return -1;
    }
}

PAL_API(void)
PAL_FileEnumeratorDelete
(
    struct PAL_FILE_ENUMERATOR *fsenum
)
{
    if (fsenum->DirectoryHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(fsenum->DirectoryHandle);
        fsenum->DirectoryHandle = INVALID_HANDLE_VALUE;
    }
}

PAL_API(int)
PAL_FileEnumeratorExecute
(
    struct PAL_FILE_ENUMERATOR *fsenum
)
{
    if (fsenum->DirectoryHandle == INVALID_HANDLE_VALUE)
    {   /* the enumerator is invalid */
        return -1;
    }
    if (fsenum->AbsolutePath == NULL || fsenum->SearchPath == NULL)
    {   /* the enumerator is invalid */
        return -1;
    }
    return PAL_FileEnumeratorSearch(fsenum, fsenum->RelativePath-1, fsenum->SearchEnd, L"");
}

PAL_API(int)
PAL_FileHandleIsValid
(
    struct PAL_FILE *file
)
{
    if (file != NULL && file->Handle != INVALID_HANDLE_VALUE)
        return 1;
    else
        return 0;
}

PAL_API(int)
PAL_IsFile
(
    struct PAL_FILE_INFO *ent_info
)
{
    return (ent_info->Attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

PAL_API(int)
PAL_IsDirectory
(
    struct PAL_FILE_INFO *ent_info
)
{
    return (ent_info->Attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

PAL_API(int)
PAL_FileStatPath
(
    struct PAL_FILE_STAT_RESULT *result, 
    struct PAL_FILE_STAT_DATA     *data
)
{
    PAL_FILE file;
    HANDLE     fd = INVALID_HANDLE_VALUE;
    DWORD  access = GENERIC_READ;
    DWORD   share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    DWORD  create = OPEN_EXISTING;
    DWORD   flags = FILE_FLAG_BACKUP_SEMANTICS;
    int        rc = 0;

    if ((fd = CreateFileW(data->Path, access, share, NULL, create, flags, NULL)) != INVALID_HANDLE_VALUE)
    {   /* the file or directory at least exists */
        file.Handle = fd;
        if (PAL_FileStatHandle(&result->Info, &file) == 0)
        {
            result->Success    = 1;
            result->ResultCode = ERROR_SUCCESS;
            rc = 0;
        }
        else
        {
            result->Success    = 0;
            result->ResultCode = GetLastError();
            rc =-1;
        }
        CloseHandle(fd);

        /* set the remaining result fields */
        result->Path       = data->Path;
        result->OpaqueData = data->OpaqueData;
        result->OpaqueId   = data->OpaqueId;
        return rc;
    }
    else
    {
        result->Path       = data->Path;
        result->Success    = 0;
        result->ResultCode = GetLastError();
        result->OpaqueData = data->OpaqueData;
        result->OpaqueId   = data->OpaqueId;
        PAL_ZeroMemory(&result->Info, sizeof(PAL_FILE_INFO));
        return -1;
    }
}

PAL_API(int)
PAL_FileStatHandle
(
    struct PAL_FILE_INFO *result, 
    struct PAL_FILE        *file
)
{
    FILE_BASIC_INFO     fbi;
    FILE_STANDARD_INFO  fsi;
#if WINVER >= 0x0602 || _WIN32_WINNT >= 0x0602
    FILE_ALIGNMENT_INFO fai;
#endif

    HANDLE fd = file->Handle;
    int    rc = 0; 
    DWORD const DEFAULT_SECTOR_SIZE = 4096;

    /* retrieve the current size of the file, in bytes */
    if (GetFileInformationByHandleEx(fd, FileStandardInfo, &fsi, sizeof(FILE_STANDARD_INFO)))
    {   /* save the file size for the user */
        result->FileSize = fsi.EndOfFile.QuadPart;
    }
    else
    {   /* the file size could not be retrieved */
        result->FileSize = 0; rc = -1;
    }
    /* retrieve the attributes of the file or directory */
    if (GetFileInformationByHandleEx(fd, FileBasicInfo, &fbi, sizeof(FILE_BASIC_INFO)))
    {   /* save the file attribute information for the user */
        result->CreationTime = PAL_FILETIMEToUnixTimeRaw(fbi.CreationTime.QuadPart);
        result->AccessTime   = PAL_FILETIMEToUnixTimeRaw(fbi.LastAccessTime.QuadPart);
        result->WriteTime    = PAL_FILETIMEToUnixTimeRaw(fbi.LastWriteTime.QuadPart);
        result->Attributes   =(pal_uint32_t) fbi.FileAttributes;
    }
    else
    {   /* the basic file information could not be retrieved */
        result->CreationTime = result->AccessTime = result->WriteTime = result->Attributes = 0; rc = -1;
    }
    /* retrieve the required alignment for unbuffered reads and writes */
#if WINVER >= 0x0602 || _WIN32_WINNT >= 0x0602
    if (GetFileInformationByHandleEx(fd, FileAlignmentInfo /* 0x11 */, &fai, sizeof(FILE_ALIGNMENT_INFO)))
    {   /* save the required alignment for the user */
        result->Alignment =(pal_uint32_t) fai.AlignmentRequirement;
    }
    else
    {   /* retrieving the information failed - use the default large sector size */
        result->Alignment = DEFAULT_SECTOR_SIZE; rc = -1;
    }
#else   /* pre-Windows 8 - use the default large sector size */
    result->Alignment = DEFAULT_SECTOR_SIZE;
#endif
    return rc;
}

PAL_API(int)
PAL_FileOpen
(
    struct PAL_FILE_OPEN_RESULT *result, 
    struct PAL_FILE_OPEN_DATA     *data
)
{
    HANDLE          fd = INVALID_HANDLE_VALUE;
    DWORD       access = 0; /* dwDesiredAccess */
    DWORD        share = 0; /* dwShareMode */
    DWORD       create = 0; /* dwCreationDisposition */
    DWORD        flags = 0; /* dwFlagsAndAttributes */
    pal_uint32_t hints = data->OpenHints;

    /* build access, share, create and flags.
     * this code block is structured so that WRITE overrides/extends READ, etc. 
     * the order of the code here matters, so beware of that when changing it. */
    if (hints & PAL_FILE_OPEN_HINT_FLAG_OVERWRITE)
    {   /* this implies write access */
        hints |= PAL_FILE_OPEN_HINT_FLAG_WRITE;
    }
    if (hints & PAL_FILE_OPEN_HINT_FLAG_ASYNCHRONOUS)
    {   /* open for asynchronous access */
        flags  = FILE_FLAG_OVERLAPPED;
    }
    if (hints & PAL_FILE_OPEN_HINT_FLAG_READ)
    {
        access = GENERIC_READ;
        share  = FILE_SHARE_READ;
        create = OPEN_EXISTING;
    }
    if (hints & PAL_FILE_OPEN_HINT_FLAG_WRITE)
    {
        access |= GENERIC_WRITE;
        flags  |= FILE_ATTRIBUTE_NORMAL;
        if (hints & PAL_FILE_OPEN_HINT_FLAG_OVERWRITE)
        {   /* the file is always re-created */
            create = CREATE_ALWAYS;
        }
        else
        {   /* the file is created if it doesn't already exist */
            create = OPEN_ALWAYS;
        }
        if (hints & PAL_FILE_OPEN_HINT_FLAG_TEMPORARY)
        {   /* temporary files are deleted on close; the cache manager tries to prevent writes to disk */
            flags |= FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE;
            share |= FILE_SHARE_DELETE;
        }
    }
    if (hints & PAL_FILE_OPEN_HINT_FLAG_SEQUENTIAL)
    {   /* tell the cache manager to optimize for sequential access */
        flags |= FILE_FLAG_SEQUENTIAL_SCAN;
    }
    else
    {   /* assume that the file will be accessed randomly */
        flags |= FILE_FLAG_RANDOM_ACCESS;
    }
    if (hints & PAL_FILE_OPEN_HINT_FLAG_UNCACHED)
    {   /* use unbuffered I/O - reads must be performed in sector-size multiples.
           user buffers must start on an address that is a sector-size multiple. */
        flags |= FILE_FLAG_NO_BUFFERING;
    }
    if (hints & PAL_FILE_OPEN_HINT_FLAG_WRITE_THROUGH)
    {   /* writes are immediately flushed to disk */
        flags |= FILE_FLAG_WRITE_THROUGH;
    }

    if ((fd = CreateFileW(data->Path, access, share, NULL, create, flags, NULL)) == INVALID_HANDLE_VALUE)
    {   /* the file could not be opened or created */
        result->Path = data->Path;
        result->File.Handle = fd;
        result->Success     = 0;
        result->ResultCode  = GetLastError();
        result->OpaqueData  = data->OpaqueData;
        result->OpaqueId    = data->OpaqueId;
        PAL_ZeroMemory(&result->Info, sizeof(PAL_FILE_INFO));
        return -1;
    }

    result->Path = data->Path;
    result->File.Handle = fd;
    result->Success     = 1;
    result->ResultCode  = GetLastError();
    result->OpaqueData  = data->OpaqueData;
    result->OpaqueId    = data->OpaqueId;
    PAL_FileStatHandle(&result->Info, &result->File);
    return 0;
}

PAL_API(int)
PAL_FileCreate
(
    struct PAL_FILE_CREATE_RESULT *result, 
    struct PAL_FILE_CREATE_DATA     *data
)
{
    HANDLE          fd = INVALID_HANDLE_VALUE;
    DWORD       access = 0; /* dwDesiredAccess */
    DWORD        share = 0; /* dwShareMode */
    DWORD       create = 0; /* dwCreationDisposition */
    DWORD        flags = 0; /* dwFlagsAndAttributes */
    pal_uint32_t hints = data->OpenHints | PAL_FILE_OPEN_HINT_FLAG_OVERWRITE;

    /* build access, share, create and flags.
     * this code block is structured so that WRITE overrides/extends READ, etc. 
     * the order of the code here matters, so beware of that when changing it. */
    if (hints & PAL_FILE_OPEN_HINT_FLAG_OVERWRITE)
    {   /* this implies write access */
        hints |= PAL_FILE_OPEN_HINT_FLAG_WRITE;
    }
    if (hints & PAL_FILE_OPEN_HINT_FLAG_ASYNCHRONOUS)
    {   /* open for asynchronous access */
        flags  = FILE_FLAG_OVERLAPPED;
    }
    if (hints & PAL_FILE_OPEN_HINT_FLAG_READ)
    {
        access = GENERIC_READ;
        share  = FILE_SHARE_READ;
        create = OPEN_EXISTING;
    }
    if (hints & PAL_FILE_OPEN_HINT_FLAG_WRITE)
    {
        access |= GENERIC_WRITE;
        flags  |= FILE_ATTRIBUTE_NORMAL;
        if (hints & PAL_FILE_OPEN_HINT_FLAG_OVERWRITE)
        {   /* the file is always re-created */
            create = CREATE_ALWAYS;
        }
        else
        {   /* the file is created if it doesn't already exist */
            create = OPEN_ALWAYS;
        }
        if (hints & PAL_FILE_OPEN_HINT_FLAG_TEMPORARY)
        {   /* temporary files are deleted on close; the cache manager tries to prevent writes to disk */
            flags |= FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE;
            share |= FILE_SHARE_DELETE;
        }
    }
    if (hints & PAL_FILE_OPEN_HINT_FLAG_SEQUENTIAL)
    {   /* tell the cache manager to optimize for sequential access */
        flags |= FILE_FLAG_SEQUENTIAL_SCAN;
    }
    else
    {   /* assume that the file will be accessed randomly */
        flags |= FILE_FLAG_RANDOM_ACCESS;
    }
    if (hints & PAL_FILE_OPEN_HINT_FLAG_UNCACHED)
    {   /* use unbuffered I/O - reads must be performed in sector-size multiples.
           user buffers must start on an address that is a sector-size multiple. */
        flags |= FILE_FLAG_NO_BUFFERING;
    }
    if (hints & PAL_FILE_OPEN_HINT_FLAG_WRITE_THROUGH)
    {   /* writes are immediately flushed to disk */
        flags |= FILE_FLAG_WRITE_THROUGH;
    }

    if ((fd = CreateFileW(data->Path, access, share, NULL, create, flags, NULL)) == INVALID_HANDLE_VALUE)
    {   /* the file could not be opened or created */
        result->Path = data->Path;
        result->File.Handle = fd;
        result->Success     = 0;
        result->ResultCode  = GetLastError();
        result->OpaqueData  = data->OpaqueData;
        result->OpaqueId    = data->OpaqueId;
        PAL_ZeroMemory(&result->Info, sizeof(PAL_FILE_INFO));
        return -1;
    }
    if (hints & PAL_FILE_OPEN_HINT_FLAG_PREALLOCATE)
    {   /* preallocate storage for the data, which can significantly improve write performance
         * BUT THE FILE MUST BE WRITTEN SEQUENTIALLY or it will be very slow for large files 
         * because the unwritten space must be zero-filled for security purposes. 
         * failures are non-fatal since this is an optimization.
         */
        if (data->DesiredSize > 0)
        {   LARGE_INTEGER   new_ptr; new_ptr.QuadPart = data->DesiredSize;
            if (SetFilePointerEx(fd, new_ptr, NULL, FILE_BEGIN))
            {   /* set the end-of-file marker to the new location */
                if (SetEndOfFile(fd))
                {   /* move the file pointer back to the start of the file */
                    new_ptr.QuadPart = 0;
                    SetFilePointerEx(fd, new_ptr, NULL, FILE_BEGIN);
                }
            }
        }
    }

    result->Path = data->Path;
    result->File.Handle = fd;
    result->Success     = 1;
    result->ResultCode  = GetLastError();
    result->OpaqueData  = data->OpaqueData;
    result->OpaqueId    = data->OpaqueId;
    PAL_FileStatHandle(&result->Info, &result->File);
    return 0;
}

PAL_API(int)
PAL_FileRead
(
    struct PAL_FILE_READ_RESULT *result, 
    struct PAL_FILE_READ_DATA     *data
)
{
    LARGE_INTEGER offset;
    HANDLE            fd = data->File.Handle;
    DWORD         amount = data->ReadAmount;
    DWORD    transferred = 0;

    offset.QuadPart = data->ReadOffset;
    if (!SetFilePointerEx(fd, offset, NULL, FILE_BEGIN))
    {   /* could not seek to the specified offset */
        result->File           = data->File;
        result->Destination    = data->Destination;
        result->ReadOffset     = data->ReadOffset;
        result->OpaqueData     = data->OpaqueData;
        result->OpaqueId       = data->OpaqueId;
        result->ReadAmount     = data->ReadAmount;
        result->TransferAmount = 0;
        result->Success        = 0;
        result->ResultCode     = GetLastError();
        return -1;
    }
    if (ReadFile(fd, data->Destination, amount, &transferred, NULL))
    {   /* the read was successful - note EOF will return true w/transferred = 0*/
        result->File           = data->File;
        result->Destination    = data->Destination;
        result->ReadOffset     = data->ReadOffset;
        result->OpaqueData     = data->OpaqueData;
        result->OpaqueId       = data->OpaqueId;
        result->ReadAmount     = amount;
        result->TransferAmount = transferred;
        result->Success        = 1;
        result->ResultCode     = GetLastError();
        return 0;
    }
    else
    {   /* the read operation failed for some reason (not EOF) */
        result->File           = data->File;
        result->Destination    = data->Destination;
        result->ReadOffset     = data->ReadOffset;
        result->OpaqueData     = data->OpaqueData;
        result->OpaqueId       = data->OpaqueId;
        result->ReadAmount     = data->ReadAmount;
        result->TransferAmount = transferred;
        result->Success        = 0;
        result->ResultCode     = GetLastError();
        return -1;
    }
}

PAL_API(int)
PAL_FileWrite
(
    struct PAL_FILE_WRITE_RESULT *result, 
    struct PAL_FILE_WRITE_DATA     *data
)
{
    LARGE_INTEGER offset;
    HANDLE            fd = data->File.Handle;
    DWORD         amount = data->WriteAmount;
    DWORD    transferred = 0;

    offset.QuadPart = data->WriteOffset;
    if (!SetFilePointerEx(fd, offset, NULL, FILE_BEGIN))
    {   /* could not seek to the specified offset */
        result->File           = data->File;
        result->Source         = data->Source;
        result->WriteOffset    = data->WriteOffset;
        result->OpaqueData     = data->OpaqueData;
        result->OpaqueId       = data->OpaqueId;
        result->WriteAmount    = data->WriteAmount;
        result->TransferAmount = 0;
        result->Success        = 0;
        result->ResultCode     = GetLastError();
        return -1;
    }
    if (WriteFile(fd, data->Source, amount, &transferred, NULL))
    {   /* the write was successful (but may not have actually made it to disk yet) */
        result->File           = data->File;
        result->Source         = data->Source;
        result->WriteOffset    = data->WriteOffset;
        result->OpaqueData     = data->OpaqueData;
        result->OpaqueId       = data->OpaqueId;
        result->WriteAmount    = amount;
        result->TransferAmount = transferred;
        result->Success        = 1;
        result->ResultCode     = GetLastError();
        return 0;
    }
    else
    {   /* the write operation failed for some reason (not EOF) */
        result->File           = data->File;
        result->Source         = data->Source;
        result->WriteOffset    = data->WriteOffset;
        result->OpaqueData     = data->OpaqueData;
        result->OpaqueId       = data->OpaqueId;
        result->WriteAmount    = data->WriteAmount;
        result->TransferAmount = transferred;
        result->Success        = 0;
        result->ResultCode     = GetLastError();
        return -1;
    }
}

PAL_API(int)
PAL_FileFlush
(
    struct PAL_FILE_FLUSH_RESULT *result, 
    struct PAL_FILE_FLUSH_DATA     *data
)
{
    if (FlushFileBuffers(data->File.Handle))
    {   /* the flush operation completes successfully */
        result->File       = data->File;
        result->Success    = 1;
        result->ResultCode = GetLastError();
        result->OpaqueData = data->OpaqueData;
        result->OpaqueId   = data->OpaqueId;
        PAL_FileStatHandle(&result->Info, &data->File);
        return 0;
    }
    else
    {   /* the flush operation failed for some reason */
        result->File       = data->File;
        result->Success    = 0;
        result->ResultCode = GetLastError();
        result->OpaqueData = data->OpaqueData;
        result->OpaqueId   = data->OpaqueId;
        PAL_ZeroMemory(&result->Info, sizeof(PAL_FILE_INFO));
        return -1;
    }
}

PAL_API(int)
PAL_FileClose
(
    struct PAL_FILE_CLOSE_RESULT *result, 
    struct PAL_FILE_CLOSE_DATA     *data
)
{
    if (CloseHandle(data->File.Handle))
    {   /* the close operation completes successfully */
        result->File       = data->File;
        result->Success    = 1;
        result->ResultCode = GetLastError();
        result->OpaqueData = data->OpaqueData;
        result->OpaqueId   = data->OpaqueId;
        return 0;
    }
    else
    {   /* the close operation failed for some reason */
        result->File       = data->File;
        result->Success    = 0;
        result->ResultCode = GetLastError();
        result->OpaqueData = data->OpaqueData;
        result->OpaqueId   = data->OpaqueId;
        return -1;
    }
}

#if 0
PAL_API(void)
PAL_FileStatPath_TaskMain
(
    struct PAL_TASK_ARGS *args
)
{
    PAL_FILE_STAT_RESULT rdata;
    PAL_FILE_STAT_DATA   *data =(PAL_FILE_STAT_DATA *) args->TaskData;
    assert(data->Path     != NULL);
    assert(data->Callback != NULL);
    PAL_FileStatPath(&rdata, data);
    data->Callback(args, &rdata);
    PAL_TaskComplete(args->TaskPool, args->TaskId);
}

PAL_API(void)
PAL_FileOpen_TaskMain
(
    struct PAL_TASK_ARGS *args
)
{
    PAL_FILE_OPEN_RESULT rdata;
    PAL_FILE_OPEN_DATA   *data =(PAL_FILE_OPEN_DATA *) args->TaskData;
    PAL_TASK_WORKER_POOL *pool = args->WorkerPool;
    
    assert(data->Path     != NULL);
    assert(data->Callback != NULL);

    if (pool != NULL && pool->IoCompletionPort != NULL)
    {    /* this routine always opens the file for asynchronous access */
        data->OpenHints |= PAL_FILE_OPEN_HINT_FLAG_ASYNCHRONOUS;
    }
    if (PAL_FileOpen(&rdata, data) == 0)
    {
        if (pool != NULL && pool->IoCompletionPort != NULL)
        {    /* additionally associate the handle with the worker pool I/O completion port */
            if (CreateIoCompletionPort(rdata.File.Handle, pool->IoCompletionPort, 0, 0) == pool->IoCompletionPort)
            {   /* immediately complete requests that execute synchronously; don't notify the completion port */
                SetFileCompletionNotificationModes(rdata.File.Handle, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);
            } /* if the file could not be associated with the completion port, reads will complete synchronously */
        }
    }
    data->Callback(args, &rdata);
    PAL_TaskComplete(args->TaskPool, args->TaskId);
}

PAL_API(void)
PAL_FileCreate_TaskMain
(
    struct PAL_TASK_ARGS *args
)
{
    PAL_FILE_CREATE_RESULT rdata;
    PAL_FILE_CREATE_DATA *data =(PAL_FILE_CREATE_DATA *) args->TaskData;
    PAL_TASK_WORKER_POOL *pool = args->WorkerPool;
    
    assert(data->Path     != NULL);
    assert(data->Callback != NULL);
    
    if (pool != NULL && pool->IoCompletionPort != NULL)
    {    /* this routine always opens the file for asynchronous access */
        data->OpenHints |= PAL_FILE_OPEN_HINT_FLAG_ASYNCHRONOUS;
    }
    if (PAL_FileCreate(&rdata, data) == 0)
    {
        if (pool != NULL && pool->IoCompletionPort != NULL)
        {    /* additionally associate the handle with the worker pool I/O completion port */
            if (CreateIoCompletionPort(rdata.File.Handle, pool->IoCompletionPort, 0, 0) == pool->IoCompletionPort)
            {   /* immediately complete requests that execute synchronously; don't notify the completion port */
                SetFileCompletionNotificationModes(rdata.File.Handle, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);
            } /* if the file could not be associated with the completion port, reads will complete synchronously */
        }
    }
    data->Callback(args, &rdata);
    PAL_TaskComplete(args->TaskPool, args->TaskId);
}

PAL_API(void)
PAL_FileRead_TaskMain
(
    struct PAL_TASK_ARGS *args
)
{
    PAL_FILE_READ_RESULT rdata;
    PAL_FILE_READ_DATA   *data =(PAL_FILE_READ_DATA*) args->TaskData;
    PAL_ASYNCIO_REQUEST   *req = NULL;
    HANDLE                  fd = data->File.Handle;
    LONGLONG        abs_offset = data->ReadOffset;
    DWORD               amount = data->ReadAmount;
    DWORD          transferred = 0;

    req = PAL_TaskIoWorkerAcquireIoRequest(args->WorkerPool);
    req->File     = fd;
    req->Type     = PAL_ASYNCIO_REQUEST_TYPE_FILE_READ;
    req->State    = PAL_ASYNCIO_REQUEST_STATE_SUBMITTED;
    req->TaskId   = args->TaskId;
    req->Reserved = 0;
    req->Overlapped.Offset     =(DWORD) (abs_offset        & 0xFFFFFFFFUL);
    req->Overlapped.OffsetHigh =(DWORD)((abs_offset >> 32) & 0xFFFFFFFFUL);
    if (ReadFile(fd, data->Destination, amount, &transferred, &req->Overlapped))
    {   /* the read completed synchronously - likely the data was in-cache */
        PAL_TaskIoWorkerReleaseIoRequest(args->WorkerPool, req);
        rdata.File           = data->File;
        rdata.Destination    = data->Destination;
        rdata.ReadOffset     = abs_offset;
        rdata.OpaqueData     = data->OpaqueData;
        rdata.OpaqueId       = data->OpaqueId;
        rdata.ReadAmount     = amount;
        rdata.TransferAmount = transferred;
        rdata.Success        = 1;
        rdata.ResultCode     = GetLastError();
        data->Callback(args, &rdata);
        PAL_TaskComplete(args->TaskPool, args->TaskId);
        return;
    }
    /* the read could have failed, or it could be completing asynchronously */
    switch ((rdata.ResultCode = GetLastError()))
    {
    case ERROR_IO_PENDING:
        { /* the request will complete asynchronously.
           * do not invoke the callback or complete the task. */
        } return;
    case ERROR_HANDLE_EOF:
        { /* end-of-file was reached. complete the task. */
          PAL_TaskIoWorkerReleaseIoRequest(args->WorkerPool, req);
          rdata.File           = data->File;
          rdata.Destination    = data->Destination;
          rdata.ReadOffset     = abs_offset;
          rdata.OpaqueData     = data->OpaqueData;
          rdata.OpaqueId       = data->OpaqueId;
          rdata.ReadAmount     = amount;
          rdata.TransferAmount = transferred;
          rdata.Success        = 1;
          data->Callback(args, &rdata);
          PAL_TaskComplete(args->TaskPool, args->TaskId);
        } return;
    default:
        { /* an actual error occurred. complete the task. */
          PAL_TaskIoWorkerReleaseIoRequest(args->WorkerPool, req);
          rdata.File           = data->File;
          rdata.Destination    = data->Destination;
          rdata.ReadOffset     = abs_offset;
          rdata.OpaqueData     = data->OpaqueData;
          rdata.OpaqueId       = data->OpaqueId;
          rdata.ReadAmount     = amount;
          rdata.TransferAmount = 0;
          rdata.Success        = 0;
          data->Callback(args, &rdata);
          PAL_TaskComplete(args->TaskPool, args->TaskId);
        } return;
    }
}

PAL_API(void)
PAL_FileWrite_TaskMain
(
    struct PAL_TASK_ARGS *args
)
{
    PAL_FILE_WRITE_RESULT rdata;
    PAL_FILE_WRITE_DATA   *data =(PAL_FILE_WRITE_DATA*) args->TaskData;
    PAL_ASYNCIO_REQUEST    *req = NULL;
    HANDLE                   fd = data->File.Handle;
    LONGLONG         abs_offset = data->WriteOffset;
    DWORD                amount = data->WriteAmount;
    DWORD           transferred = 0;

    req = PAL_TaskIoWorkerAcquireIoRequest(args->WorkerPool);
    req->File     = fd;
    req->Type     = PAL_ASYNCIO_REQUEST_TYPE_FILE_WRITE;
    req->State    = PAL_ASYNCIO_REQUEST_STATE_SUBMITTED;
    req->TaskId   = args->TaskId;
    req->Reserved = 0;
    req->Overlapped.Offset     =(DWORD) (abs_offset        & 0xFFFFFFFFUL);
    req->Overlapped.OffsetHigh =(DWORD)((abs_offset >> 32) & 0xFFFFFFFFUL);
    if (WriteFile(fd, data->Source, amount, &transferred, &req->Overlapped))
    {   /* the write completed synchronously */
        PAL_TaskIoWorkerReleaseIoRequest(args->WorkerPool, req);
        rdata.File           = data->File;
        rdata.Source         = data->Source;
        rdata.WriteOffset    = abs_offset;
        rdata.OpaqueData     = data->OpaqueData;
        rdata.OpaqueId       = data->OpaqueId;
        rdata.WriteAmount    = amount;
        rdata.TransferAmount = transferred;
        rdata.Success        = 1;
        rdata.ResultCode     = GetLastError();
        data->Callback(args, &rdata);
        PAL_TaskComplete(args->TaskPool, args->TaskId);
        return;
    }
    /* the write could have failed, or it could be completing asynchronously */
    switch ((rdata.ResultCode = GetLastError()))
    {
    case ERROR_IO_PENDING:
        { /* the request will complete asynchronously.
           * do not invoke the callback or complete the task. */
        } return;
    default:
        { /* an actual error occurred. complete the task. */
          PAL_TaskIoWorkerReleaseIoRequest(args->WorkerPool, req);
          rdata.File           = data->File;
          rdata.Source         = data->Source;
          rdata.WriteOffset    = abs_offset;
          rdata.OpaqueData     = data->OpaqueData;
          rdata.OpaqueId       = data->OpaqueId;
          rdata.WriteAmount    = amount;
          rdata.TransferAmount = 0;
          rdata.Success        = 0;
          data->Callback(args, &rdata);
          PAL_TaskComplete(args->TaskPool, args->TaskId);
        } return;
    }
}

PAL_API(void)
PAL_FileFlush_TaskMain
(
    struct PAL_TASK_ARGS *args
)
{
    PAL_FILE_FLUSH_RESULT rdata;
    PAL_FILE_FLUSH_DATA   *data =(PAL_FILE_FLUSH_DATA *) args->TaskData;
    assert(data->Callback != NULL);
    PAL_FileFlush(&rdata, data);
    data->Callback(args, &rdata);
    PAL_TaskComplete(args->TaskPool, args->TaskId);
}

PAL_API(void)
PAL_FileClose_TaskMain
(
    struct PAL_TASK_ARGS *args
)
{
    PAL_FILE_CLOSE_RESULT rdata;
    PAL_FILE_CLOSE_DATA   *data =(PAL_FILE_CLOSE_DATA *) args->TaskData;
    assert(data->Callback != NULL);
    PAL_FileClose(&rdata, data);
    data->Callback(args, &rdata);
    PAL_TaskComplete(args->TaskPool, args->TaskId);
}
#endif /* DISABLED */
