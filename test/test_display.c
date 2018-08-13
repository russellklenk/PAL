/**
 * @summary Implements a basic hello world application.
 */
#include <stdio.h>
#include "pal_display.h"

#include "pal_win32_memory.c"
#include "pal_win32_display.c"

int main
(
    int         argc, 
    char const *argv[]
)
{
    PAL_WINDOW_SYSTEM *wsi = PAL_WindowSystemCreate();
    DWORD              tss = timeGetTime();
    DWORD              tse = tss;

    (void) argc;
    (void) argv;
    printf("Hello, world!\r\n");

    for ( ; ; ) {
        tse = timeGetTime();
        if ((tse - tss) > 30000) {
            break;
        }
        PAL_WindowSystemUpdate(wsi);
        if (wsi->DisplayEventFlags != 0) {
            printf("DEF: 0x%08X\r\n", wsi->DisplayEventFlags);
        }
    }

    PAL_WindowSystemDelete(wsi);

    return 0;
}
