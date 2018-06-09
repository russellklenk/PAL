/**
 * @summary Define the PAL types and API entry points used for working with 
 * C-style string data.
 */
#ifndef __PAL_STRING_H__
#define __PAL_STRING_H__

#ifndef __PAL_H__
#include "pal.h"
#endif

/* @summary Forward-declare the types exported by this module.
 * The type definitions are included in the platform-specific header.
 */
struct  PAL_STRING_TABLE_ENTRY;
struct  PAL_STRING_TABLE_BUCKET;
struct  PAL_STRING_TABLE_INDEX;
struct  PAL_STRING_TABLE;
struct  PAL_STRING_TABLE_INIT;

/* @summary Define the signature used for a string hashing function.
 * The hashing function is given a nul-terminated string and returns a 32-bit value.
 * @param str The nul-terminated string to hash.
 * @param cb On return, this location must be updated with the length of the string, in bytes, including the trailing nul.
 * @return The 32-bit hash of the string data.
 */
typedef pal_uint32_t (*PAL_StringHash32_Func)(void const *str, pal_uint32_t *cb);

#ifdef __cplusplus
extern "C" {
#endif

/* @summary Determine the length of a nul-terminated string encoded using the native character encoding of the host OS.
 * @param str The nul-terminated string to examine.
 * @return The length of the specified string, in bytes, including the nul terminator.
 */
PAL_API(pal_usize_t)
PAL_NativeStringLengthBytes
(
    pal_char_t const *str
);

/* @summary Determine the length of a nul-terminated string encoded using the native character encoding of the host OS.
 * @param str The nul-terminated string to examine.
 * @return The length of the specified string, in characters, not including the nul terminator.
 */
PAL_API(pal_usize_t)
PAL_NativeStringLengthChars
(
    pal_char_t const *str
);

/* @summary Compare two strings. The strings are encoded using the native character encoding of the host OS.
 * @param str1 Pointer to a nul-terminated string.
 * @param str2 Pointer to a nul-terminated string.
 * @return Zero if the strings match, less than zero if the first non-matching codepoint in str1 is less than the same codepoint in str2, or greater than zero if the first non-matching codepoint in str1 is greater than the corresponding codepoint in str2.
 */
PAL_API(int)
PAL_NativeStringCompareCs
(
    pal_char_t const *str1, 
    pal_char_t const *str2
);

/* @summary Compare two strings, ignoring case. The strings are encoded using the native character encoding of the host OS.
 * @param str1 Pointer to a nul-terminated string.
 * @param str2 Pointer to a nul-terminated string.
 * @return Zero if the strings match, less than zero if the first non-matching codepoint in str1 is less than the same codepoint in str2, or greater than zero if the first non-matching codepoint in str1 is greater than the corresponding codepoint in str2.
 */
PAL_API(int)
PAL_NativeStringCompareCi
(
    pal_char_t const *str1, 
    pal_char_t const *str2
);

/* @summary Convert a UTF-8 encoded string to the native character encoding of the host OS (PAL_OSCHAR).
 * @param utf8_str Pointer to a nul-terminated UTF-8 encoded string to convert.
 * @param native_buf Pointer to the buffer to receive the nul-terminated native-encoded string.
 * @param native_max The maximum number of bytes that can be written to the native_buf buffer. Specify 0 to determine the required length of the buffer.
 * @param native_len On return, this location is updated with the number of bytes required to store the converted string, including the nul terminator.
 * @return Zero if the conversion could be performed, or -1 if the conversion could not be performed.
 */
PAL_API(int)
PAL_StringConvertUtf8ToNative
(
    pal_utf8_t const *utf8_str, 
    pal_char_t     *native_buf, 
    pal_usize_t     native_max, 
    pal_usize_t    *native_len
);

/* @summary Convert a native-encoded string (PAL_OSCHAR) to UTF-8 encoding.
 * @param native_str Pointer to a nul-terminated string using the encoding of the host OS.
 * @param utf8_buf Pointer to the buffer to receive the UTF-8 encoded string.
 * @param utf8_max The maximum number of bytes that can be written to the utf8_buf buffer. Specify 0 to determine the required length of the buffer.
 * @param utf8_len On return, this location is updated with the number of bytes required to store the converted string, including the nul terminator.
 * @return Zero if the conversion could be performed, or -1 if the conversion could not be performed.
 */
PAL_API(int)
PAL_StringConvertNativeToUtf8
(
    pal_char_t const *native_str, 
    pal_utf8_t         *utf8_buf, 
    pal_usize_t         utf8_max, 
    pal_usize_t        *utf8_len
);

/* @summary Compute a 32-bit hash value of a nul-terminated UTF-8 encoded string.
 * @param str The nul-terminated string to hash.
 * @param len On return, this location is updated with the number of bytes of string data, including the nul.
 * @return A 32-bit hash value for the input string.
 */
PAL_API(pal_uint32_t)
PAL_StringHash32_Utf8
(
    pal_utf8_t const *str, 
    pal_uint32_t     *len
);

/* @summary Compute a 32-bit hash value of a nul-terminated UTF-16 encoded string.
 * @param str The nul-terminated string to hash.
 * @param len On return, this location is updated with the number of bytes of string data, including the nul.
 * @return A 32-bit hash value for the input string.
 */
PAL_API(pal_uint32_t)
PAL_StringHash32_Utf16
(
    pal_utf16_t const *str, 
    pal_uint32_t      *len
);

/* @summary Compute a 32-bit hash value of a nul-terminated UTF-32 encoded string.
 * @param str The nul-terminated string to hash.
 * @param len On return, this location is updated with the number of bytes of string data, including the nul.
 * @return A 32-bit hash value for the input string.
 */
PAL_API(pal_uint32_t)
PAL_StringHash32_Utf32
(
    pal_utf32_t const *str, 
    pal_uint32_t      *len
);

/* @summary Construct a new string table with the given attributes.
 * @param table The PAL_STRING_TABLE to initialize.
 * @param init Data used to configure the string table.
 * @return Zero if the table is successfully created, or -1 if an error occurred.
 */
PAL_API(int)
PAL_StringTableCreate
(
    struct PAL_STRING_TABLE     *table, 
    struct PAL_STRING_TABLE_INIT *init
);

/* @summary Free all resources associated with a string table instance.
 * @param table The PAL_STRING_TABLE to delete.
 */
PAL_API(void)
PAL_StringTableDelete
(
    struct PAL_STRING_TABLE *table
);

/* @summary Remove all interned data from a string table.
 * @param table The PAL_STRING_TABLE to reset.
 */
PAL_API(void)
PAL_StringTableReset
(
    struct PAL_STRING_TABLE *table
);

/* @summary Intern a string within a string table if it doesn't already exist.
 * @param table The PAL_STRING_TABLE managing the string data.
 * @param str The nul-terminated string to intern.
 * @return A pointer to the interned string, or NULL if the input string is NULL or the data cannot be interned.
 * If the string was already present in the table, the existing copy is returned.
 */
PAL_API(void*)
PAL_StringTableIntern
(
    struct PAL_STRING_TABLE *table, 
    void const                *str
);

#ifdef __cplusplus
}; /* extern "C" */
#endif

/* @summary Include the appropriate platform-specific header.
 */
#if   PAL_TARGET_PLATFORM == PAL_PLATFORM_WIN32 || PAL_TARGET_PLATFORM == PAL_PLATFORM_WINRT
    #include "pal_win32_string.h"
#elif PAL_TARGET_PLATFORM == PAL_PLATFORM_LINUX || PAL_TARGET_PLATFORM == PAL_PLATFORM_ANDROID
    #include "pal_linux_string.h"
#elif PAL_TARGET_PLATFORM == PAL_PLATFORM_MACOS || PAL_TARGET_PLATFORM == PAL_PLATFORM_IOS
    #include "pal_apple_string.h"
#else
    #error   pal_string.h: No implementation of the abstraction layer for your platform.
#endif

#endif /* __PAL_STRING_H__ */
