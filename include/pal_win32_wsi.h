/**
 * @summary Define the platform-specific types and other internal bits for the
 * Microsoft Windows Desktop platform.
 */
#ifndef __PAL_WIN32_WSI_H__
#define __PAL_WIN32_WSI_H__

#ifndef __PAL_WSI_H__
#include "pal_wsi.h"
#endif

#ifndef PAL_NO_INCLUDES
#include <Windows.h>
#include <Shellapi.h>
#include <Dbt.h>
#   if WINVER >= 0x0603
#      include <ShellScalingAPI.h>
#   endif
#endif

/* @summary Define the internal data associated with a display attached to the host.
 */
typedef struct PAL_DISPLAY_DATA {
    HMONITOR                           MonitorHandle;          /* The handle identifying the display to the operating system. */
    DWORD                              DisplayOrdinal;         /* The display ordinal supplied to EnumDisplayDevices. */
    DWORD                              RefreshRateHz;          /* The display refresh rate, in Hertz. */
    UINT                               DisplayDpiX;            /* The horizontal dots-per-inch setting of the display. */
    UINT                               DisplayDpiY;            /* The vertical dots-per-inch setting of the display. */
    DEVMODE                            DisplayMode;            /* Information describing the current display mode. */
    DISPLAY_DEVICE                     DisplayInfo;            /* Information uniquely identifying the display to the operating system. */
} PAL_DISPLAY_DATA;

/* @summary Define the internal data associated with a window. This is a superset of the data tracked with PAL_WINDOW_STATE.
 */
typedef struct PAL_WINDOW_DATA {
    struct PAL_WINDOW_SYSTEM          *WindowSystem;           /* A pointer back to the owning WSI_WINDOW_SYSTEM. */
    HWND                               OsWindowHandle;         /* The operating system window handle. */
    HINSTANCE                          OsModuleHandle;         /* The HINSTANCE of the application that created the window. */
    HMONITOR                           DisplayHandle;          /* The HMONITOR of the display that owns the window. */
    pal_sint32_t                       DisplayPositionX;       /* The display top-left corner position, in virtual display space. */
    pal_sint32_t                       DisplayPositionY;       /* The display top-left corner position, in virtual display space. */
    pal_uint32_t                       DisplaySizeX;           /* The display width, in physical pixels. */
    pal_uint32_t                       DisplaySizeY;           /* The display height, in physical pixels. */
    pal_sint32_t                       WindowPositionX;        /* The window top-left corner position, in virtual display space. */
    pal_sint32_t                       WindowPositionY;        /* The window top-left corner position, in virtual display space. */
    pal_uint32_t                       WindowSizeLogicalX;     /* The window width, in logical pixels. */
    pal_uint32_t                       WindowSizeLogicalY;     /* The window height, in logical pixels. */
    pal_uint32_t                       WindowSizePhysicalX;    /* The window width, in physical pixels. */
    pal_uint32_t                       WindowSizePhysicalY;    /* The window height, in physical pixels. */
    pal_uint32_t                       ClientSizeLogicalX;     /* The window client area width, in logical pixels. */
    pal_uint32_t                       ClientSizeLogicalY;     /* The window client area height, in logical pixels. */
    pal_uint32_t                       ClientSizePhysicalX;    /* The window client area width, in physical pixels. */
    pal_uint32_t                       ClientSizePhysicalY;    /* The window client area height, in physical pixels. */
    pal_sint32_t                       RestorePositionX;       /* The last-known position of the window. Not updated for fullscreen windows. */
    pal_sint32_t                       RestorePositionY;       /* The last-known position of the window. Not updated for fullscreen windows. */
    pal_uint32_t                       RestoreSizeX;           /* The last-known size of the window client area. Not updated for fullscreen windows. */
    pal_uint32_t                       RestoreSizeY;           /* The last-known size of the window client area. Not updated for fullscreen windows. */
    DWORD                              RestoreStyle;           /* The window style saved before toggling windowed/fullscreen. */
    DWORD                              RestoreStyleEx;         /* The extended window style saved before toggling windowed/fullscreen. */
    pal_uint32_t                       DisplayDpiX;            /* The horizontal dots-per-inch setting of the display containing the window. */
    pal_uint32_t                       DisplayDpiY;            /* The vertical dots-per-inch setting of the display containing the window. */
    pal_uint32_t                       EventFlags;             /* One or more bitwise ORd PAL_WINDOW_EVENT_FLAGS indicating changes that have occurred since the previous update. */
    pal_uint32_t                       EventCount;             /* The number of events processed during the current update. */
    pal_uint32_t                       StatusFlags;            /* One or more bitwise ORd PAL_WINDOW_STATUS_FLAGS values. */
    pal_uint32_t                       CreateFlags;            /* The flag values used when the window was created. */
} PAL_WINDOW_DATA;

#endif /* __PAL_WIN32_WSI_H__ */

