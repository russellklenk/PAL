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
    PAL_WINDOW_SYSTEM          *wsi = NULL;
    PAL_INPUT                 input = PAL_HANDLE_INVALID;
    PAL_WINDOW               window = PAL_HANDLE_INVALID;
    PAL_WINDOW_STATE          state;
    PAL_WINDOW_INIT     window_init;
    PAL_WINDOW_SYSTEM_INIT wsi_init;

    PAL_UNUSED_ARG(argc);
    PAL_UNUSED_ARG(argv);
    
    /* initialize the window system interface */
    wsi_init.MaxInputSnapshots  = 6;
    wsi_init.MaxGamepadDevices  = 4;
    wsi_init.MaxPointerDevices  = 1;
    wsi_init.MaxKeyboardDevices = 1;
    if ((wsi = PAL_WindowSystemCreate(&wsi_init)) == NULL) {
        return -1;
    } 

    /* create the main application window */
    window_init.TargetDisplay = PAL_WindowSystemPrimaryDisplay(wsi, NULL);
    window_init.WindowTitle   = L"WSI Test Window";
    window_init.CreateFlags   = PAL_WINDOW_CREATE_FLAG_USE_DISPLAY;
    window_init.StyleFlags    = PAL_WINDOW_STYLE_FLAG_CHROME | PAL_WINDOW_STYLE_FLAG_RESIZABLE | PAL_WINDOW_STYLE_FLAG_CENTER;
    window_init.SizeX         = 800;
    window_init.SizeY         = 600;
    if ((window = PAL_WindowCreate(&state, wsi, &window_init)) == PAL_HANDLE_INVALID) {
        return -1;
    }
    
    /* acquire an input state object.
     * since there's only one thread in this example, and no late-latch input, 
     * only one input state object is required. 
     */
    if ((input = PAL_InputAcquire(wsi)) == PAL_HANDLE_INVALID) { 
        return -1;
    }

    for ( ; ; ) {
        /* update the window system to detect display changes */
        PAL_WindowSystemUpdate(wsi, input);
        /* retrieve the latest state for this particular window */
        PAL_WindowQueryState(&state, wsi, window);
        /* TODO: generate an input event snapshot */
        if (PAL_WindowIsClosed(&state)) {
            break;
        }
        Sleep(100);
    }

    PAL_WindowDelete(&state, wsi, window);
    PAL_WindowSystemDelete(wsi);
    return 0;
}
