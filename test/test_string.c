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

int main
(
    int    argc, 
    char **argv
)
{
    PAL_UNUSED_ARG(argc);
    PAL_UNUSED_ARG(argv);
    return 0;
}

