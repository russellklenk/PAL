/**
 * @summary Implement the PAL entry points from pal_string.h.
 */
#include "pal_win32_string.h"

PAL_API(pal_usize_t)
PAL_NativeStringLengthBytes
(
    pal_char_t const *str
)
{
    pal_usize_t len = 0;

    if (str == NULL)
        return 0;
    if (SUCCEEDED(StringCbLengthW(str, STRSAFE_MAX_CCH * sizeof(pal_char_t), &len)))
        return (len + sizeof(pal_char_t));
    else
        return 0;
}

PAL_API(pal_usize_t)
PAL_NativeStringLengthChars
(
    pal_char_t const *str
)
{
    pal_usize_t len = 0;

    if (str == NULL)
        return 0;
    if (SUCCEEDED(StringCchLengthW(str, STRSAFE_MAX_CCH, &len)))
        return (len);
    else
        return 0;
}

PAL_API(int)
PAL_NativeStringCompareCs
(
    pal_char_t const *str1, 
    pal_char_t const *str2
)
{
    if (str1 == str2)
        return 0;
    // TODO: replace CRT function wcscmp
    return wcscmp(str1, str2);
}

PAL_API(int)
PAL_NativeStringCompareCi
(
    pal_char_t const *str1, 
    pal_char_t const *str2
)
{
    if (str1 == str2)
        return 0;
    // TODO: replace CRT function _wcsicmp
    return _wcsicmp(str1, str2);
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

    if (utf8_str == NULL)
    {   /* make sure the output is nul-terminated */
        if (output && native_max >= sizeof(pal_char_t))
            *output = L'\0';
        *native_len = sizeof(pal_char_t);
        return 0;
    }
    if ((nchars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_str, -1, output, outcch)) == 0)
    {   /* the conversion could not be performed */
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

    if (native_str == NULL)
    {   /* make sure the output is nul-terminated */
        if (output && utf8_max >= sizeof(pal_utf8_t))
            *output = '\0';
        *utf8_len = sizeof(pal_utf8_t);
        return 0;
    }
    if ((nbytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, native, -1, output, outcb, NULL, NULL)) == 0)
    {   /* the conversion could not be performed */
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
    pal_uint32_t     *len
)
{   /* FNV1 with MurmurHash3 finalizer */
    pal_uint8_t const *iter =(pal_uint8_t const*) str;
    pal_uint32_t        h32 = 2166136261U;
    pal_uint32_t         cb = 0;
    while (*iter) {
        h32 =(16777619U * h32) + (*iter++);
        cb += sizeof(pal_uint8_t);
    }
    if (len)
    {   /* include an extra byte for the nul */
       *len = cb + sizeof(pal_uint8_t);
    }
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
    pal_uint32_t      *len
)
{   /* FNV1 with MurmurHash3 finalizer */
    pal_uint16_t const *iter =(pal_uint16_t const*) str;
    pal_uint32_t         h32 = 2166136261U;
    pal_uint32_t          cb = 0;
    while (*iter) {
        h32 =(16777619U * h32) + (*iter++);
        cb += sizeof(pal_uint16_t);
    }
    if (len)
    {   /* include an extra word for the nul */
       *len = cb + sizeof(pal_uint16_t);
    }
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
    pal_uint32_t      *len
)
{   /* FNV1 with MurmurHash3 finalizer */
    pal_uint32_t const *iter =(pal_uint32_t const*) str;
    pal_uint32_t         h32 = 2166136261U;
    pal_uint32_t          cb = 0;
    while (*iter) {
        h32 =(16777619U * h32) + (*iter++);
        cb += sizeof(pal_uint32_t);
    }
    if (len)
    {   /* include an extra dword for the nul */
       *len = cb + sizeof(pal_uint32_t);
    }
    h32 ^= h32 >> 16;
    h32 *= 0x85EBCA6BU;
    h32 ^= h32 >> 13;
    h32 *= 0xC2B2AE35U;
    h32 ^= h32 >> 16;
    return h32;
}
