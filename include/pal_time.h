/**
 * @summary Define the PAL types and API entry points used for reading high-
 * resolution timestamp values.
 */
#ifndef __PAL_TIME_H__
#define __PAL_TIME_H__

#ifndef __PAL_H__
#include "pal.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* @summary Retrieve a timestamp value read from the system high-resolution timer.
 * The high resolution timer starts counting at system boot time.
 * The timestamp can be converted to seconds by dividing by the value returned by PAL_TimestampCountsPerSecond.
 * @return A timestamp value ready from the system high-resolution timer.
 */
PAL_API(pal_uint64_t)
PAL_TimestampInTicks
(
    void
);

/* @summary Retrieve the frequency of the system high-resolution timer.
 * @return The high-resolution timer frequency, in counts per-second.
 */
PAL_API(pal_uint64_t)
PAL_TimestampCountsPerSecond
(
    void
);

/* @summary Retrieve the elapsed time, in nanoseconds, between two timestamps.
 * @param ts_enter The timestamp taken at the beginning of the measured interval.
 * @param ts_leave The timestamp taken at the end of the measured interval.
 * @return The elapsed time, in nanoseconds, between the two timestamps.
 */
PAL_API(pal_uint64_t)
PAL_TimestampDeltaNanoseconds
(
    pal_uint64_t ts_enter, 
    pal_uint64_t ts_leave
);

/* @summary Convert a date and time value specified in the FILETIME format to a Unix timestamp.
 * @param filetime The date and time value specified in the FILETIME format.
 * @return The timestamp value in Unix timestamp format.
 */
PAL_API(pal_sint64_t)
PAL_FILETIMEToUnixTime
(
    pal_sint64_t filetime
);

/* @summary Convert a date and time value specified in the Unix timestamp format to the FILETIME format.
 * @param unixtime The date and time value specified in the Unix timestamp format.
 * @return The timestamp value in the FILETIME format.
 */
PAL_API(pal_sint64_t)
PAL_UnixTimeToFILETIME
(
    pal_sint64_t unixtime
);

#ifdef __cplusplus
}; /* extern "C" */
#endif

/* @summary Include the appropriate platform-specific header.
 */
#if   PAL_TARGET_PLATFORM == PAL_PLATFORM_WIN32 || PAL_TARGET_PLATFORM == PAL_PLATFORM_WINRT
    #include "pal_win32_time.h"
#elif PAL_TARGET_PLATFORM == PAL_PLATFORM_LINUX || PAL_TARGET_PLATFORM == PAL_PLATFORM_ANDROID
    #include "pal_linux_time.h"
#elif PAL_TARGET_PLATFORM == PAL_PLATFORM_MACOS || PAL_TARGET_PLATFORM == PAL_PLATFORM_IOS
    #include "pal_apple_time.h"
#else
    #error   pal_time.h: No implementation of the abstraction layer for your platform.
#endif

#endif /* __PAL_TIME_H__ */
