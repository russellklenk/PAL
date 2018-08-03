/**
 * @summary Define the PAL types and API entry points used for working with 
 * C-style string data.
 */
#ifndef __PAL_STRING_H__
#define __PAL_STRING_H__

#ifndef __PAL_H__
#include "pal.h"
#endif

/* @summary Intern a UTF-8 encoded string in a string table.
 * @param _table The PAL_STRING_TABLE in which the string will be interned.
 * @param _str A nul-terminated, UTF-8 encoded string to intern.
 * @return A pointer to the interned string data.
 */
#ifndef PAL_StringTableInternUtf8
#define PAL_StringTableInternUtf8(_table, _str)                                \
    (pal_utf8_t*) PAL_StringTableIntern((_table), (_str), PAL_STRING_CHAR_TYPE_UTF8)
#endif

/* @summary Intern a UTF-16 encoded string in a string table.
 * @param _table The PAL_STRING_TABLE in which the string will be interned.
 * @param _str A nul-terminated, UTF-16 encoded string to intern.
 * @return A pointer to the interned string data.
 */
#ifndef PAL_StringTableInternUtf16
#define PAL_StringTableInternUtf16(_table, _str)                               \
    (pal_utf16_t*) PAL_StringTableIntern((_table), (_str), PAL_STRING_CHAR_TYPE_UTF16)
#endif

/* @summary Intern a UTF-32 encoded string in a string table.
 * @param _table The PAL_STRING_TABLE in which the string will be interned.
 * @param _str A nul-terminated, UTF-32 encoded string to intern.
 * @return A pointer to the interned string data.
 */
#ifndef PAL_StringTableInternUtf32
#define PAL_StringTableInternUtf32(_table, _str)                               \
    (pal_utf32_t*) PAL_StringTableIntern((_table), (_str), PAL_STRING_CHAR_TYPE_UTF32)
#endif

/* @summary Intern a native-encoded string in a string table.
 * @param _table The PAL_STRING_TABLE in which the string will be interned.
 * @param _str A nul-terminated, native encoded string to intern.
 * @return A pointer to the interned string data.
 */
#ifndef PAL_StringTableInternNative
#define PAL_StringTableInternNative(_table, _str)                              \
    (pal_char_t *) PAL_StringTableIntern((_table), (_str), PAL_STRING_CHAR_TYPE_NATIVE)
#endif

/* @summary Forward-declare the types exported by this module.
 * The type definitions are included in the platform-specific header.
 */
struct  PAL_STRING_INFO;
struct  PAL_STRING_TABLE;
struct  PAL_STRING_TABLE_INIT;

/* @summary Define the signature used for a string hashing function.
 * The hashing function is given a nul-terminated string and returns a 32-bit value.
 * @param str The nul-terminated string to hash.
 * @param len_b On return, this location must be updated with the length of the string, in bytes, including the trailing nul.
 * @param len_c On return, this location must be updated with the length of the string, in characters, not including the trailing nul.
 * @return The 32-bit hash of the string data.
 */
typedef pal_uint32_t (*PAL_StringHash32_Func)(void const *str, pal_uint32_t *len_b, pal_uint32_t *len_c);

/* @summary Define data describing the attributes of an interned string.
 */
typedef struct PAL_STRING_INFO {
    pal_uint32_t                 ByteLength;                   /* The length of the string, including the terminating nul, in bytes. */
    pal_uint32_t                 CharLength;                   /* The length of the string, not including the terminating nul, in characters. */
    pal_sint32_t                 CharacterType;                /* One of the values of the PAL_STRING_CHAR_TYPE enumeration, specifying the character encoding. */
} PAL_STRING_INFO;

/* @summary Define the data used to configure a string table instance.
 * A string table has two limits - a maximum total data size across all strings, and a maximum number of strings.
 * The maximum string count is used to determine the size of the lookup table used when performing intern operations.
 */
typedef struct PAL_STRING_TABLE_INIT {
    PAL_StringHash32_Func        HashFunction;                 /* The function used to produce a 32-bit hash value given a nul-terminated string. */
    pal_uint32_t                 MaxDataSize;                  /* The maximum amount of memory that can be committed for storing string data, in bytes. */
    pal_uint32_t                 DataCommitSize;               /* The amount of memory, in bytes, to commit for the string data chunk during table creation. */
    pal_uint32_t                 MaxStringCount;               /* The maximum number of strings the table can hold. */
    pal_uint32_t                 InitialCapacity;              /* The number of strings expected to be interned in the table. */
} PAL_STRING_TABLE_INIT;

/* @summary Define the character type encodings for strings interned within a string table.
 * The platform header additionally defines PAL_STRING_CHAR_TYPE_NATIVE to one of the types in this enum.
 * A string table may contain a strings with a mix of character encodings; for example it may contain UTF8 and UTF16 strings.
 */
typedef enum PAL_STRING_CHAR_TYPE {
    PAL_STRING_CHAR_TYPE_UNKNOWN =  0,                         /* The character data is stored using an unknown encoding. */
    PAL_STRING_CHAR_TYPE_UTF8    =  1,                         /* The character data is stored using UTF-8 encoding. */
    PAL_STRING_CHAR_TYPE_UTF16   =  2,                         /* The character data is stored using UTF-16 encoding. */
    PAL_STRING_CHAR_TYPE_UTF32   =  3,                         /* The character data is stored using UTF-32 encoding. */
    /* PAL_STRING_CHAR_TYPE_NATIVE  */
} PAL_STRING_CHAR_TYPE;

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
 * @param len_b On return, this location is updated with the number of bytes of string data, including the nul.
 * @param len_c On return, this location is updated with the number of characters in the string, not including the nul.
 * @return A 32-bit hash value for the input string.
 */
PAL_API(pal_uint32_t)
PAL_StringHash32_Utf8
(
    pal_utf8_t const *str, 
    pal_uint32_t   *len_b, 
    pal_uint32_t   *len_c
);

/* @summary Compute a 32-bit hash value of a nul-terminated UTF-16 encoded string.
 * @param str The nul-terminated string to hash.
 * @param len_b On return, this location is updated with the number of bytes of string data, including the nul.
 * @param len_c On return, this location is updated with the number of characters in the string, not including the nul.
 * @return A 32-bit hash value for the input string.
 */
PAL_API(pal_uint32_t)
PAL_StringHash32_Utf16
(
    pal_utf16_t const *str, 
    pal_uint32_t    *len_b, 
    pal_uint32_t    *len_c
);

/* @summary Compute a 32-bit hash value of a nul-terminated UTF-32 encoded string.
 * @param str The nul-terminated string to hash.
 * @param len_b On return, this location is updated with the number of bytes of string data, including the nul.
 * @param len_c On return, this location is updated with the number of characters in the string, not including the nul.
 * @return A 32-bit hash value for the input string.
 */
PAL_API(pal_uint32_t)
PAL_StringHash32_Utf32
(
    pal_utf32_t const *str, 
    pal_uint32_t    *len_b, 
    pal_uint32_t    *len_c
);

/* @summary Construct a new string table with the given attributes.
 * @param init Data used to configure the string table.
 * @return Zero if the table is successfully created, or -1 if an error occurred.
 */
PAL_API(struct PAL_STRING_TABLE*)
PAL_StringTableCreate
(
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
 * @param char_type One of the values of the PAL_STRING_CHAR_TYPE enumeration specifying the string encoding.
 * @return A pointer to the interned string, or NULL if the input string is NULL or the data cannot be interned.
 * If the string was already present in the table, the existing copy is returned.
 */
PAL_API(void*)
PAL_StringTableIntern
(
    struct PAL_STRING_TABLE *table, 
    void const                *str, 
    pal_uint32_t         char_type
);

/* @summary Retrieve information (length and encoding) for a particular string interned in a string table.
 * @param info The PAL_STRING_INFO structure to populate with information about the supplied string.
 * @param table The PAL_STRING_TABLE in which the supplied string is interned.
 * @param str A pointer to the start of the interned string. This must be a pointer to the first byte of the string.
 * @return Zero if the operation is successful, or non-zero if an error occurred (for example, str is not interned within table).
 */
PAL_API(int)
PAL_StringTableInfo
(
    struct PAL_STRING_INFO   *info, 
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

