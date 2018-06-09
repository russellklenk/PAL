/**
 * @summary Implement the PAL entry points from pal_time.h.
 */
#include "pal_win32_time.h"

PAL_API(pal_uint64_t)
PAL_TimestampInTicks
(
    void
)
{
    LARGE_INTEGER ticks;
    QueryPerformanceCounter(&ticks);
    return (pal_uint64_t) ticks.QuadPart;
}

PAL_API(pal_uint64_t)
PAL_TimestampCountsPerSecond
(
    void
)
{
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return (pal_uint64_t) frequency.QuadPart;
}

PAL_API(pal_uint64_t)
PAL_TimestampDeltaNanoseconds
(
    pal_uint64_t ts_enter, 
    pal_uint64_t ts_leave
)
{
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    /* scale the tick value by the nanoseconds-per-second multiplier 
     * before scaling back down by ticks-per-second to avoid loss of precision.
     */
    return (1000000000ULL * (ts_leave - ts_enter)) / (pal_uint64_t) frequency.QuadPart;
}

PAL_API(pal_sint64_t)
PAL_FILETIMEToUnixTime
(
    pal_sint64_t filetime
)
{   /* 10000000 is the number of 100ns intervals in one second.
     * 11644473600 is the number of seconds between Jan 1 1601 00:00 and 
     * Jan 1 1970 00:00 UTC (the epoch difference.)
     */
    return filetime / 10000000LL - 11644473600LL;
}

PAL_API(pal_sint64_t)
PAL_UnixTimeToFILETIME
(
    pal_sint64_t unixtime
)
{   /* 10000000 is the number of 100ns intervals in one second.
     * 116444736000000000 is the number of 100ns intervals between 
     * Jan 1 1601 00:00 and Jan 1 1970 00:00 UTC (the epoch difference.)
     */
    return (unixtime * 10000000LL) + 116444736000000000LL;
}
