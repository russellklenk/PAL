/**
 * test_string.c: Implements test routines for the PAL_STRING_TABLE and related 
 * functionality. This file contains tests for both correct functionality 
 * and also for performance measurement.
 */
#include <stdio.h>
#include <Windows.h>

#include "pal_time.h"
#include "pal_memory.h"
#include "pal_string.h"

#include "pal_win32_time.c"
#include "pal_win32_memory.c"
#include "pal_win32_string.c"

static int
FTest_AllocationBehavior
(
    void
)
{
    PAL_STRING_TABLE    *table = NULL;
    PAL_STRING_TABLE_INIT init = {0};
    char                 *str1 = NULL;
    char                 *str2 = NULL;
    char                 *str3 = NULL;
    char                 *str4 = NULL;
    char                 *str5 = NULL;
    int                 result = 1;

    PAL_ZeroMemory(&init , sizeof(PAL_STRING_TABLE_INIT));
    init.HashFunction    = PAL_StringHash32_Utf8;
    init.MaxDataSize     = 8192;
    init.DataCommitSize  = 0;
    init.MaxStringCount  = 128;
    init.InitialCapacity = 0;
    if ((table = PAL_StringTableCreate(&init)) == NULL) {
        assert(0 && "PAL_StringTableCreate failed");
        return 0;
    }
    if ((str1 = PAL_StringTableInternUtf8(table, "A")) == NULL) {
    }
    if ((str2 = PAL_StringTableInternUtf8(table, "BB")) == NULL) {
    }
    if ((str3 = PAL_StringTableInternUtf8(table, "CCC")) == NULL) {
    }
    if ((str4 = PAL_StringTableInternUtf8(table, "DDDD")) == NULL) {
    }
    if ((str5 = PAL_StringTableInternUtf8(table, "A")) == NULL) {
    }
    if (str1 != str5) {
    }

    PAL_StringTableDelete(table);
    return result;
}

int main
(
    int    argc, 
    char **argv
)
{
    int res = 0;

    PAL_UNUSED_ARG(argc);
    PAL_UNUSED_ARG(argv);

    res = FTest_AllocationBehavior(); printf("FTest_AllocationBehavior: %d\r\n", res);
    return res;
}

