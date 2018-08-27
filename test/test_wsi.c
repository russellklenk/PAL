/**
 * @summary Implements a basic hello world application.
 */
#include <stdio.h>
#include "pal_wsi.h"

#include "pal_win32_memory.c"
#include "pal_win32_dylib.c"
#include "pal_win32_time.c"
#include "pal_win32_wsi.c"

int main
(
    int         argc, 
    char const *argv[]
)
{
    PAL_WINDOW_SYSTEM *wsi = PAL_WindowSystemCreate();
    PAL_WINDOW      window = PAL_HANDLE_INVALID;
    PAL_WINDOW_STATE state;
    PAL_WINDOW_INIT   init;

    PAL_UNUSED_ARG(argc);
    PAL_UNUSED_ARG(argv);

    /* create the main application window */
    init.TargetDisplay = PAL_WindowSystemPrimaryDisplay(wsi, NULL);
    init.WindowTitle   = L"WSI Test Window";
    init.CreateFlags   = PAL_WINDOW_CREATE_FLAG_USE_DISPLAY;
    init.StyleFlags    = PAL_WINDOW_STYLE_FLAG_CHROME | PAL_WINDOW_STYLE_FLAG_RESIZABLE | PAL_WINDOW_STYLE_FLAG_CENTER;
    init.SizeX         = 800;
    init.SizeY         = 600;
    if ((window = PAL_WindowCreate(&state, wsi, &init)) == PAL_HANDLE_INVALID) {
        return -1;
    }

    for ( ; ; ) {
        /* update the window system to detect display changes */
        PAL_WindowSystemUpdate(wsi);
        /* retrieve the latest state for this particular window */
        PAL_WindowQueryState(&state, wsi, window);
        if (PAL_WindowIsClosed(&state)) {
            break;
        }
        Sleep(100);
    }

    PAL_WindowDelete(&state, wsi, window);
    PAL_WindowSystemDelete(wsi);
    return 0;
}
