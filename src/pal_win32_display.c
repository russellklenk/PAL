/**
 * @summary Implement the PAL_Display* APIs for the Win32 desktop platform.
 */
#include <initguid.h>
#include "pal_win32_display.h"

/* @summary WM_DPICHANGED is defined for Windows 8.1 and later only.
 */
#ifndef WM_DPICHANGED
#   define WM_DPICHANGED                 0x02E0
#endif

/* @summary WS_EX_NOREDIRECTIONBITMAP is defined for Windows 8 and later only.
 */
#ifndef WS_EX_NOREDIRECTIONBITMAP
#   define WS_EX_NOREDIRECTIONBITMAP     0x00200000L
#endif

/* @summary Define stream indices for the handle tables used by this module.
 */
#ifndef PAL_WINDOW_SYSTEM_STREAMS
#   define PAL_WINDOW_SYSTEM_STREAMS
/** PAL_WINDOW_SYSTEM::WindowTable **/
#   define PAL_WSS_WINDOW_DATA           0
#endif

/* @summary The device interface class GUID for monitor devices.
 * Found under registry key HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Class.
 */
DEFINE_GUID(
    MonitorDeviceInterfaceClassGuid, 
    0x4d36e96e, 
    0xe325, 
    0x11ce, 
    0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18);

/* @summary The device interface class GUID for display adapters (GPUs).
 * Found under registry key HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Class.
 */
DEFINE_GUID(
    DisplayAdapterDeviceInterfaceClassGuid, 
    0x4d36e968, 
    0xe325, 
    0x11ce, 
    0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18);

/* @summary Define constants not available prior to Windows 8.1.
 */
#if WINVER < 0x0603
typedef enum MONITOR_DPI_TYPE {
    MDT_EFFECTIVE_DPI              = 0, 
    MDT_ANGULAR_DPI                = 1,
    MDT_RAW_DPI                    = 2,
    MDT_DEFAULT                    = MDT_EFFECTIVE_DPI
} MONITOR_DPI_TYPE;

typedef enum PROCESS_DPI_AWARENESS {
    PROCESS_DPI_UNAWARE            = 0,
    PROCESS_SYSTEM_DPI_AWARE       = 1,
    PROCESS_PER_MONITOR_DPI_AWARE  = 2
} PROCESS_DPI_AWARENESS;
#endif /* WINVER < 0x0603 (Windows 8.1) */

/* @summary Define a wrapper around the data associated with a window object.
 */
typedef struct PAL_WINDOW_OBJECT {
    PAL_WINDOW                             Handle;              /* The window object handle. */
    PAL_WINDOW_DATA                       *WindowData;          /* The window data stream. */
} PAL_WINDOW_OBJECT;

/* @summary Define the object/handle namespaces utilized by the WSI module.
 */
typedef enum PAL_WINDOW_SYSTEM_NAMESPACE {
    PAL_WINDOW_SYSTEM_NAMESPACE_WINDOW   = 1UL,                 /* The namespace for PAL_WINDOW handles. */
} PAL_WINDOW_SYSTEM_NAMESPACE;

/* @summary Function pointer types for dynamically-loaded Win32 API entry points.
 */
typedef HRESULT (WINAPI           *SetProcessDpiAwareness_Func)(PROCESS_DPI_AWARENESS);
typedef HRESULT (WINAPI           *GetDpiForMonitor_Func      )(HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT*);
static SetProcessDpiAwareness_Func SetProcessDpiAwareness_Fn  = NULL;
static GetDpiForMonitor_Func       GetDpiForMonitor_Fn        = NULL;

/* @summary Implement SetProcessDpiAwareness for systems prior to Windows 8.1.
 * @param level One of the values of the PROCESS_DPI_AWARENESS enumeration.
 * @return S_OK or E_ACCESSDENIED.
 */
static HRESULT WINAPI SetProcessDpiAwareness_Stub
(
    PROCESS_DPI_AWARENESS level
)
{
    PAL_UNUSED_ARG(level);
    if (SetProcessDPIAware()) {
        return S_OK;
    } else {
        return E_ACCESSDENIED;
    }
}

/* @summary Implement GetDpiForMonitor for systems prior to Windows 8.1.
 * @param monitor The handle of the monitor to query.
 * @param type The type must be MDT_EFFECTIVE_DPI.
 * @param dpi_x On return, the horizontal DPI is stored in this location.
 * @param dpi_y On return, the vertical DPI is stored in this location.
 * @return Either S_OK or E_INVALIDARG.
 */
static HRESULT WINAPI GetDpiForMonitor_Stub
(
    HMONITOR      monitor, 
    MONITOR_DPI_TYPE type, 
    UINT           *dpi_x, 
    UINT           *dpi_y
)
{
    if (type == MDT_EFFECTIVE_DPI) {
        HDC screen_dc = GetDC(NULL);
        DWORD   h_dpi = GetDeviceCaps(screen_dc, LOGPIXELSX);
        DWORD   v_dpi = GetDeviceCaps(screen_dc, LOGPIXELSY);
        ReleaseDC(NULL, screen_dc);
       *dpi_x = h_dpi; *dpi_y = v_dpi;
        PAL_UNUSED_ARG(monitor);
        return S_OK;
    } else {
       *dpi_x = USER_DEFAULT_SCREEN_DPI; *dpi_y = USER_DEFAULT_SCREEN_DPI; 
        return E_INVALIDARG;
    }
}

/* @summary Convert from physical to logical pixels.
 * @param dimension The value to convert, specified in physical pixels.
 * @param dots_per_inch The DPI value of the display.
 * @return The corresponding dimention specified in logical pixels.
 */
static PAL_INLINE pal_uint32_t
PAL_PhysicalToLogicalPixels
(
    pal_uint32_t     dimension, 
    pal_uint32_t dots_per_inch
)
{
    return (dimension * USER_DEFAULT_SCREEN_DPI) / dots_per_inch;
}

/* @summary Convert from logical to physical pixels.
 * @param dimension The value to convert, specified in logical pixels.
 * @param dots_per_inch The DPI value of the display.
 * @return The corresponding dimension specified in physical pixels.
 */
static PAL_INLINE pal_uint32_t
PAL_LogicalToPhysicalPixels
(
    pal_uint32_t     dimension, 
    pal_uint32_t dots_per_inch
)
{
    return (dimension * dots_per_inch) / USER_DEFAULT_SCREEN_DPI;
}

/* @summary Extract data from a PAL_DISPLAY_DATA structure into a public PAL_DISPLAY_INFO structure.
 * @param info The PAL_DISPLAY_INFO to populate.
 * @param data The internal PAL_DISPLAY_DATA.
 * @param index The zero-based index of the display record.
 */
static void
PAL_DisplayDataToDisplayInfo
(
    struct PAL_DISPLAY_INFO *info, 
    struct PAL_DISPLAY_DATA *data, 
    pal_uint32_t            index
)
{
    info->DeviceId         =(pal_uintptr_t) data->MonitorHandle;
    info->DeviceIndex      =(pal_uint32_t ) index;
    info->DisplayDpiX      =(pal_uint32_t ) data->DisplayDpiX;
    info->DisplayDpiY      =(pal_uint32_t ) data->DisplayDpiY;
    info->DisplayPositionX =(pal_sint32_t ) data->DisplayMode.dmPosition.x;
    info->DisplayPositionY =(pal_sint32_t ) data->DisplayMode.dmPosition.y;
    info->DisplaySizeX     =(pal_uint32_t ) data->DisplayMode.dmPelsWidth;
    info->DisplaySizeY     =(pal_uint32_t ) data->DisplayMode.dmPelsHeight;
    info->RefreshRateHz    =(pal_uint32_t ) data->RefreshRateHz;
}

/* @summary Enumerate the set of displays attached to the system and available for use.
 * This populates the PAL_WINDOW_SYSTEM::ActiveDisplays array and updates the display count.
 * @param wsi The PAL_WINDOW_SYSTEM managing the list of active displays.
 */
static void
PAL_EnumerateDisplays
(
    struct PAL_WINDOW_SYSTEM *wsi
)
{
    PAL_DISPLAY_DATA *info;
    DEVMODE             dm;
    DISPLAY_DEVICE      dd;
    RECT    monitor_bounds;
    DWORD          ordinal;
    DWORD    display_count;
    HDC                 dc;
    int                 hz;

    /* retrieve the default refresh rate */
    dc = GetDC(NULL);
    hz = GetDeviceCaps(dc, VREFRESH);
    ReleaseDC(NULL, dc);

    /* enumerate attached displays */
    ZeroMemory(&dm, sizeof(DEVMODE)); dm.dmSize = sizeof(DEVMODE);
    ZeroMemory(&dd, sizeof(DISPLAY_DEVICE)); dd.cb = sizeof(DISPLAY_DEVICE);
    for (ordinal = 0, display_count = 0; EnumDisplayDevices(NULL, ordinal, &dd, 0); ++ordinal) {
        /* ignore pseudo-displays and displays not attached to the desktop */
        if ((dd.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) != 0) {
            continue;
        }
        if ((dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0) {
            continue;
        }

        /* retrieve display orientation and geometry */
        if (!EnumDisplaySettingsEx(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm, 0)) {
            if (!EnumDisplaySettingsEx(dd.DeviceName, ENUM_REGISTRY_SETTINGS, &dm, 0)) {
                continue;
            }
        }
        if ((dm.dmFields & DM_POSITION) == 0 || (dm.dmFields & DM_PELSWIDTH) == 0 || (dm.dmFields & DM_PELSHEIGHT) == 0) {
            continue;
        }
        if (dm.dmDisplayFrequency == 0 || dm.dmDisplayFrequency == 1) {
            /* retrieve the default refresh rate */
            dc = GetDC(NULL);
            hz =(int) GetDeviceCaps(dc, VREFRESH);
        } else {
            /* use the rate specified by the DEVMODE */
            hz =(int) dm.dmDisplayFrequency;
        }
        monitor_bounds.left   = (LONG) dm.dmPosition.x;
        monitor_bounds.top    = (LONG) dm.dmPosition.y;
        monitor_bounds.right  = (LONG)(dm.dmPosition.x + dm.dmPelsWidth);
        monitor_bounds.bottom = (LONG)(dm.dmPosition.y + dm.dmPelsHeight);
        info                  = &wsi->ActiveDisplays[display_count++];
        info->DisplayOrdinal  = ordinal;
        info->MonitorHandle   = MonitorFromRect(&monitor_bounds, MONITOR_DEFAULTTONEAREST);
        GetDpiForMonitor_Fn(info->MonitorHandle, MDT_EFFECTIVE_DPI, &info->DisplayDpiX, &info->DisplayDpiY);
        CopyMemory(&info->DisplayMode, &dm, dm.dmSize);
        CopyMemory(&info->DisplayInfo, &dd, dd.cb);
        if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) {
            PAL_DisplayDataToDisplayInfo(&wsi->PrimaryDisplay, info, display_count-1);
        }
        if (display_count == wsi->DisplayCapacity) {
            break;
        }
    }
    wsi->DisplayCount = display_count;
}

/* @summary Process any messages sent to the top-level notification window.
 * If any display changes have occurred, re-enumerate displays attached to the system.
 * @param wsi The window system interface to update.
 * @return Non-zero if the WM_QUIT message is received, or zero otherwise.
 */
static int
PAL_WindowSystemProcessDisplayUpdates
(
    struct PAL_WINDOW_SYSTEM *wsi
)
{
    HWND hwnd = wsi->NotifyWindow;
    int   res = 0;
    BOOL  ret;
    MSG   msg;

    /* retrieve and dispatch any waiting messages */
    while ((ret = PeekMessage(&msg, hwnd, 0, 0, PM_REMOVE)) != 0) {
        if (ret == -1 || msg.message == WM_QUIT) {
            res = 1;
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    /* re-enumerate displays if any changes occurred */
    if (wsi->DisplayEventFlags != PAL_DISPLAY_EVENT_FLAGS_NONE) {
        wsi->DisplayEventFlags  = PAL_DISPLAY_EVENT_FLAGS_NONE;
        PAL_EnumerateDisplays(wsi);
    }
    return res;
}

/* @summary Update the window position and size, and the window client size.
 * @param data The PAL_WINDOW_DATA to update.
 * @param hwnd The handle of the associated window.
 * @param dpi_x The horizontal dots-per-inch setting of the display containing the window.
 * @param dpi_y The vertical dots-per-inch setting of the display containing the window.
 */
static void
PAL_WindowDataUpdateWindowGeometry
(
    struct PAL_WINDOW_DATA *data, 
    HWND                    hwnd, 
    pal_uint32_t           dpi_x, 
    pal_uint32_t           dpi_y
)
{
    RECT                  rc;
    pal_uint32_t  physical_w;
    pal_uint32_t  physical_h;

    GetWindowRect(hwnd, &rc);
    physical_w = (pal_uint32_t)(rc.right - rc.left);
    physical_h = (pal_uint32_t)(rc.bottom - rc.top);

    data->WindowPositionX     =(pal_sint32_t) rc.left;
    data->WindowPositionY     =(pal_sint32_t) rc.top;
    data->WindowSizePhysicalX = physical_w;
    data->WindowSizePhysicalY = physical_h;
    data->WindowSizeLogicalX  = PAL_PhysicalToLogicalPixels(physical_w, dpi_x);
    data->WindowSizeLogicalY  = PAL_PhysicalToLogicalPixels(physical_h, dpi_y);

    GetClientRect(hwnd, &rc);
    physical_w = (pal_uint32_t)(rc.right - rc.left);
    physical_h = (pal_uint32_t)(rc.bottom - rc.top);

    data->ClientSizePhysicalX = physical_w;
    data->ClientSizePhysicalY = physical_h;
    data->ClientSizeLogicalX  = PAL_PhysicalToLogicalPixels(physical_w, dpi_x);
    data->ClientSizeLogicalY  = PAL_PhysicalToLogicalPixels(physical_h, dpi_y);
}

/* @summary Update a PAL_WINDOW_STATE structure with data for a window.
 * @param state The destination PAL_WINDOW_STATE.
 * @param data The source PAL_WINDOW_DATA.
 */
static void
PAL_CopyWindowDataToWindowState
(
    struct PAL_WINDOW_STATE *state, 
    struct PAL_WINDOW_DATA   *data
)
{
    if (state == NULL) {
        return;
    }
    if (data  == NULL) {
        ZeroMemory(state, sizeof(PAL_WINDOW_STATE));
        state->UpdateTime = PAL_TimestampInTicks();
        return;
    }
    state->UpdateTime          = PAL_TimestampInTicks();
    state->EventFlags          = data->EventFlags;
    state->EventCount          =(data->EventFlags != 0);
    state->DisplayDpiX         = data->DisplayDpiX;
    state->DisplayDpiY         = data->DisplayDpiY;
    state->DisplayPositionX    = data->DisplayPositionX;
    state->DisplayPositionY    = data->DisplayPositionY;
    state->DisplaySizeX        = data->DisplaySizeX;
    state->DisplaySizeY        = data->DisplaySizeY;
    state->WindowPositionX     = data->WindowPositionX;
    state->WindowPositionY     = data->WindowPositionY;
    state->WindowSizeLogicalX  = data->WindowSizeLogicalX;
    state->WindowSizeLogicalY  = data->WindowSizeLogicalY;
    state->WindowSizePhysicalX = data->WindowSizePhysicalX;
    state->WindowSizePhysicalY = data->WindowSizePhysicalY;
    state->ClientSizeLogicalX  = data->ClientSizeLogicalX;
    state->ClientSizeLogicalY  = data->ClientSizeLogicalY;
    state->ClientSizePhysicalX = data->ClientSizePhysicalX;
    state->ClientSizePhysicalY = data->ClientSizePhysicalY;
}

/* @summary Utility function to define a handle type.
 * @param table The PAL_HANDLE_TABLE to construct.
 * @param layout The memory layout of the data streams associated with handles of the given type.
 * @param initial_commit The minimum number of objects for which backing memory will be committed.
 * @param handle_namespace The handle type namespace, used to distinguish for example a PAL_DISPLAY from a PAL_WINDOW.
 * @return Zero if the handle type is successfully initialized, or non-zero if an error occurred.
 */
static int
PAL_HandleTypeDefine
(
    struct PAL_HANDLE_TABLE     *table, 
    struct PAL_MEMORY_LAYOUT   *layout, 
    pal_uint32_t        initial_commit, 
    pal_uint32_t      handle_namespace
)
{
    PAL_HANDLE_TABLE_INIT init;
    init.Namespace      = handle_namespace;
    init.InitialCommit  = initial_commit;
    init.TableFlags     = PAL_HANDLE_TABLE_FLAG_IDENTITY | PAL_HANDLE_TABLE_FLAG_STORAGE;
    init.DataLayout     = layout;
    return PAL_HandleTableCreate(table, &init);
}

/* @summary Create a new PAL_WINDOW_OBJECT.
 * @param obj The PAL_WINDOW_OBJECT to populate.
 * @param wsi The PAL_WINDOW_SYSTEM managing the window object.
 * @return Zero if the object is created successfully, or non-zero if an error occurred.
 */
static int
PAL_WindowObjectCreate
(
    struct PAL_WINDOW_OBJECT *obj, 
    struct PAL_WINDOW_SYSTEM *wsi
)
{
    PAL_WINDOW            handle = PAL_HANDLE_INVALID;
    PAL_HANDLE_TABLE_CHUNK chunk;
    PAL_MEMORY_VIEW         view;
    pal_uint32_t           index;

    if (PAL_HandleTableCreateIds(&wsi->WindowTable, &handle, 1) != 0) {
        goto error_exit;
    }
    if (PAL_HandleTableGetChunkForHandle(&wsi->WindowTable, &chunk, handle, &index, &view) == NULL) {
        goto error_exit;
    }
    obj->Handle     = handle;
    obj->WindowData = PAL_MemoryViewStreamAt(PAL_WINDOW_DATA, &view, PAL_WSS_WINDOW_DATA, index);
    return  0;

error_exit:
    obj->Handle     = PAL_HANDLE_INVALID;
    obj->WindowData = NULL;
    return -1;
}

/* @summary Resolve a PAL_WINDOW handle into a PAL_WINDOW_OBJECT instance.
 * @param obj The PAL_WINDOW_OBJECT to populate with data for the object.
 * @param wsi The PAL_WINDOW_SYSTEM managing the PAL_WINDOW.
 * @param handle The handle to resolve.
 * @return Zero if the object is successfully resolved, or non-zero if the handle was invalid.
 */
static PAL_INLINE int
PAL_WindowObjectResolve
(
    struct PAL_WINDOW_OBJECT *obj, 
    struct PAL_WINDOW_SYSTEM *wsi, 
    PAL_WINDOW             handle
)
{
    PAL_HANDLE_TABLE_CHUNK chunk;
    PAL_MEMORY_VIEW         view;
    pal_uint32_t           index;
    if (PAL_HandleTableGetChunkForHandle(&wsi->WindowTable, &chunk, handle, &index, &view) != NULL) {
        obj->Handle     = handle;
        obj->WindowData = PAL_MemoryViewStreamAt(PAL_WINDOW_DATA, &view, PAL_WSS_WINDOW_DATA, index);
        return  0;
    } else {
        obj->Handle     = PAL_HANDLE_INVALID;
        obj->WindowData = NULL;
        return -1;
    }
}

/* @summary Free resources associated with a PAL_WINDOW object and invalidate the handle.
 * @param wsi The PAL_WINDOW_SYSTEM managing the window object.
 * @param handle The PAL_WINDOW handle of the object to delete.
 */
static void
PAL_WindowObjectDelete
(
    struct PAL_WINDOW_SYSTEM *wsi, 
    PAL_WINDOW             handle
)
{
    PAL_HandleTableDeleteIds(&wsi->WindowTable, &handle, 1);
}

/* @summary Destroy a single packed chunk of window objects.
 * @param table The PAL_HANDLE_TABLE managing the window object data.
 * @param chunk The PAL_HANDLE_TABLE_CHUNK describing the packed chunk of window objects.
 * @param view The PAL_MEMORY_VIEW used to access the PAL_WINDOW_DATA within the chunk.
 * @param context An opaque pointer-sized value that contains the address of the PAL_WINDOW_SYSTEM managing the windows.
 * @return This function always returns 1 to keep enumerating chunks.
 */
static int
PAL_DestroyWindowChunk
(
    struct PAL_HANDLE_TABLE       *table, 
    struct PAL_HANDLE_TABLE_CHUNK *chunk, 
    struct PAL_MEMORY_VIEW         *view, 
    pal_uintptr_t                context
)
{
    PAL_WINDOW_DATA *data = NULL;
    pal_uint32_t     i, n;
    HWND             hwnd;
    BOOL              ret;
    MSG               msg;

    /* destroy all windows in the chunk */
    for (i = 0, n = chunk->Count; i < n; ++i) {
        data = PAL_MemoryViewStreamAt(PAL_WINDOW_DATA, view, PAL_WSS_WINDOW_DATA, i);
        hwnd = data->OsWindowHandle;
        if (DestroyWindow(hwnd) != FALSE) {
            while ((ret = GetMessage(&msg, hwnd, 0, 0)) != 0) {
                if (ret == -1 || msg.message == WM_QUIT) {
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            data->OsWindowHandle = NULL;
        }
    }

    /* invalidate all window handles in the chunk */
    PAL_HandleTableDeleteChunkIds(table, chunk->Index);
    PAL_UNUSED_ARG(context); /* PAL_WINDOW_SYSTEM**/
    return 1;
}

/* @summary Poll for and process events for a single packed chunk of window objects.
 * @param table The PAL_HANDLE_TABLE managing the window object data.
 * @param chunk The PAL_HANDLE_TABLE_CHUNK describing the packed chunk of window objects.
 * @param view The PAL_MEMORY_VIEW used to access the PAL_WINDOW_DATA within the chunk.
 * @param context An opaque pointer-sized value that contains the address of the PAL_WINDOW_SYSTEM managing the windows.
 * @return This function always returns 1 to keep enumerating chunks.
 */
static int
PAL_UpdateWindowChunk
(
    struct PAL_HANDLE_TABLE       *table, 
    struct PAL_HANDLE_TABLE_CHUNK *chunk, 
    struct PAL_MEMORY_VIEW         *view, 
    pal_uintptr_t                context
)
{
    PAL_WINDOW_SYSTEM   *wsi =(PAL_WINDOW_SYSTEM*) context;
    PAL_WINDOW_DATA    *data = NULL;
    pal_uint32_t event_count = 0;
    pal_uint32_t        i, n;
    HWND                hwnd;
    BOOL                 ret;
    MSG                  msg;

    /* destroy all windows in the chunk */
    for (i = 0, n = chunk->Count; i < n; ++i) {
        data = PAL_MemoryViewStreamAt(PAL_WINDOW_DATA, view, PAL_WSS_WINDOW_DATA, i);
        hwnd = data->OsWindowHandle;

        data->EventFlags = PAL_WINDOW_EVENT_FLAGS_NONE;
        data->EventCount = 0;
        event_count      = 0;

        while ((ret  = PeekMessage(&msg, hwnd, 0, 0, PM_REMOVE)) != 0) {
            if (ret == -1 || msg.message == WM_QUIT) {
                event_count++;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            event_count++;
        }

        data->EventCount = event_count;
    }
    PAL_UNUSED_ARG(table);
    PAL_UNUSED_ARG(wsi);
    return 1;
}

/* @summary For each window, poll for and dispatch events.
 * @param wsi The window system interface to update.
 */
static void
PAL_WindowSystemProcessWindowUpdates
(
    struct PAL_WINDOW_SYSTEM *wsi
)
{
    PAL_HANDLE_TABLE_VISITOR_INIT visitor;
    visitor.Callback = PAL_UpdateWindowChunk;
    visitor.Context  =(pal_uintptr_t) wsi;
    visitor.Flags    = 0;
    PAL_HandleTableVisit(&wsi->WindowTable, &visitor);
}

/* @summary Perform processing for the WM_DEVMODECHANGE message. This message is sent to top-level windows when a display mode changes.
 * @param hwnd The HWND of the window that received the message.
 * @param msg The Windows message identifier.
 * @param wparam The WPARAM value associated with the message.
 * @param lparam The LPARAM value associated with the message.
 * @return A message-specific value indicating the result of processing the message.
 */
static LRESULT CALLBACK
PAL_WndProc_Notify_WM_DISPLAYCHANGE
(
    struct PAL_WINDOW_SYSTEM *wsi, 
    HWND                     hwnd, 
    WPARAM                 wparam, 
    LPARAM                 lparam
)
{   /* wparam is the bit depth of the display */
    /* lparam low word is the display width, high word is the height */
    wsi->DisplayEventFlags |= PAL_DISPLAY_EVENT_FLAG_MODE_CHANGE;
    return DefWindowProc(hwnd, WM_DISPLAYCHANGE, wparam, lparam);
}

/* @summary Perform processing for the WM_DEVICECHANGE message. This message is sent to top-level windows when a device is attached to or removed from the system.
 * @param hwnd The HWND of the window that received the message.
 * @param msg The Windows message identifier.
 * @param wparam The WPARAM value associated with the message.
 * @param lparam The LPARAM value associated with the message.
 * @return A message-specific value indicating the result of processing the message.
 */
static LRESULT CALLBACK
PAL_WndProc_Notify_WM_DEVICECHANGE
(
    struct PAL_WINDOW_SYSTEM *wsi, 
    HWND                     hwnd, 
    WPARAM                 wparam, 
    LPARAM                 lparam
)
{
    if (wparam == DBT_DEVICEARRIVAL) {
        DEV_BROADCAST_HDR   *hdr = (DEV_BROADCAST_HDR*) lparam;
        if (hdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
            DEV_BROADCAST_DEVICEINTERFACE *dev =(DEV_BROADCAST_DEVICEINTERFACE*) hdr;
            if (IsEqualGUID(&dev->dbcc_classguid, &MonitorDeviceInterfaceClassGuid)) {
                wsi->DisplayEventFlags |= PAL_DISPLAY_EVENT_FLAG_DISPLAY_ATTACH;
            } else if (IsEqualGUID(&dev->dbcc_classguid, &DisplayAdapterDeviceInterfaceClassGuid)) {
                wsi->DisplayEventFlags |= PAL_DISPLAY_EVENT_FLAG_GPU_ATTACH;
            }
        }
    }
    if (wparam == DBT_DEVICEREMOVECOMPLETE) {
        DEV_BROADCAST_HDR   *hdr = (DEV_BROADCAST_HDR*) lparam;
        if (hdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
            DEV_BROADCAST_DEVICEINTERFACE *dev =(DEV_BROADCAST_DEVICEINTERFACE*) hdr;
            if (IsEqualGUID(&dev->dbcc_classguid, &MonitorDeviceInterfaceClassGuid)) {
                wsi->DisplayEventFlags |= PAL_DISPLAY_EVENT_FLAG_DISPLAY_REMOVE;
            } else if (IsEqualGUID(&dev->dbcc_classguid, &DisplayAdapterDeviceInterfaceClassGuid)) {
                wsi->DisplayEventFlags |= PAL_DISPLAY_EVENT_FLAG_GPU_REMOVE;
            }
        }
    }
    return DefWindowProc(hwnd, WM_DEVICECHANGE, wparam, lparam);
}

/* @summary Implement the message callback for the top-level WSI notification window.
 * @param hwnd The HWND of the window that received the message.
 * @param msg The Windows message identifier.
 * @param wparam The WPARAM value associated with the message.
 * @param lparam The LPARAM value associated with the message.
 * @return A message-specific value indicating the result of processing the message.
 */
static LRESULT CALLBACK
PAL_WndProc_Notify
(
    HWND     hwnd, 
    UINT      msg, 
    WPARAM wparam, 
    LPARAM lparam
)
{
    PAL_WINDOW_SYSTEM *wsi = NULL;
    LRESULT         result = 0;

    if (msg == WM_NCCREATE) {
        CREATESTRUCT     *cs =(CREATESTRUCT *) lparam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) cs->lpCreateParams);
        ((PAL_WINDOW_SYSTEM*) cs->lpCreateParams)->NotifyWindow = hwnd;
        return DefWindowProc(hwnd, msg, wparam, lparam);
    }
    /* several messages may be received prior to WM_NCCREATE */
    if ((wsi =(PAL_WINDOW_SYSTEM*) GetWindowLongPtr(hwnd, GWLP_USERDATA)) == NULL) {
        return DefWindowProc(hwnd, msg, wparam, lparam);
    }
    /* process the critical messages - wsi is a valid pointer */
    switch (msg) {
        case WM_DESTROY:
            { /* post WM_QUIT to indicate that the window system interface has terminated */
              PostQuitMessage(0);
            } break;
        case WM_ERASEBKGND:
            { /* tell Windows the background was erased */
              result = 1;
            } break;
        case WM_PAINT:
            { /* validate the entire client rectangle */
              ValidateRect(hwnd, NULL);
            } break;
        case WM_DEVICECHANGE:
            { result = PAL_WndProc_Notify_WM_DEVICECHANGE(wsi, hwnd, wparam, lparam);
            } break;
        case WM_DISPLAYCHANGE:
            { result = PAL_WndProc_Notify_WM_DISPLAYCHANGE(wsi, hwnd, wparam, lparam);
            } break;
        default:
            { /* pass the message to the default handler */
              result = DefWindowProc(hwnd, msg, wparam, lparam);
            } break;
    }
    return result;
}

PAL_API(struct PAL_WINDOW_SYSTEM*)
PAL_WindowSystemCreate
(
    void
)
{
    DEV_BROADCAST_DEVICEINTERFACE dnf;
    PAL_MEMORY_ARENA            arena;
    PAL_MEMORY_ARENA_INIT  arena_init;
    PAL_MEMORY_LAYOUT    table_layout;
    SYSTEM_INFO               sysinfo;
    MONITORINFO               moninfo;
    WNDCLASSEX               wndclass;
    POINT                          pt;
    RECT                           rc;
    WCHAR const           *class_name =L"PAL__NotifyClass";
    HINSTANCE                  module =(HINSTANCE) GetModuleHandleW(NULL);
    HMODULE                    shcore = NULL;
    HMONITOR                  monitor = NULL;
    HDEVNOTIFY             notify_dev = NULL;
    HWND                   notify_wnd = NULL;
    PAL_WINDOW_SYSTEM            *wsi = NULL;
    pal_uint8_t            *base_addr = NULL;
    pal_usize_t         required_size = 0;
    pal_uint32_t const   MAX_DISPLAYS = 64;
    DWORD                  error_code = ERROR_SUCCESS;
    DWORD                       style = WS_POPUP;
    DWORD                    style_ex = WS_EX_NOACTIVATE;// | WS_EX_NOREDIRECTIONBITMAP;

    /* dynamically resolve Windows API entry points from Shcore.dll */
    if ((shcore = LoadLibraryW(L"Shcore.dll")) == NULL) {
        SetProcessDpiAwareness_Fn = SetProcessDpiAwareness_Stub;
        GetDpiForMonitor_Fn       = GetDpiForMonitor_Stub;
    } else {
        if((SetProcessDpiAwareness_Fn =(SetProcessDpiAwareness_Func) GetProcAddress(shcore, "SetProcessDpiAwareness")) == NULL) {
            SetProcessDpiAwareness_Fn = SetProcessDpiAwareness_Stub;
        }
        if((GetDpiForMonitor_Fn =(GetDpiForMonitor_Func) GetProcAddress(shcore, "GetDpiForMonitor")) == NULL) {
            GetDpiForMonitor_Fn = GetDpiForMonitor_Stub;
        }
    }
    /* ... */

    SetProcessDpiAwareness_Fn(PROCESS_PER_MONITOR_DPI_AWARE);

    /* retrieve the OS page size and determine the amount of memory required */
    GetNativeSystemInfo(&sysinfo);
    required_size  = PAL_AllocationSizeType (PAL_WINDOW_SYSTEM);
    required_size += PAL_AllocationSizeArray(PAL_DISPLAY_DATA, MAX_DISPLAYS);
    /* ... */
    required_size  = PAL_AlignUp(required_size, sysinfo.dwPageSize);

    /* allocate memory for the window system data */
    if ((base_addr =(pal_uint8_t *) VirtualAlloc(NULL, required_size, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE)) == NULL) {
        error_code = GetLastError();
        goto cleanup_and_fail;
    }

    /* initialize a memory arena to sub-allocate from the main allocation */
    arena_init.AllocatorName = __FUNCTION__;
    arena_init.AllocatorType = PAL_MEMORY_ALLOCATOR_TYPE_HOST;
    arena_init.MemoryStart   =(pal_uint64_t) base_addr;
    arena_init.MemorySize    =(pal_uint64_t) required_size;
    arena_init.UserData      = NULL;
    arena_init.UserDataSize  = 0;
    if (PAL_MemoryArenaCreate(&arena, &arena_init) != 0) {
        error_code = ERROR_ARENA_TRASHED;
        goto cleanup_and_fail;
    }

    /* initialize the window system data */
    wsi                    = PAL_MemoryArenaAllocateHostType (&arena, PAL_WINDOW_SYSTEM);
    wsi->ActiveDisplays    = PAL_MemoryArenaAllocateHostArray(&arena, PAL_DISPLAY_DATA, MAX_DISPLAYS);
    wsi->NotifyWindow      = NULL;
    wsi->DeviceNotify      = NULL;
    wsi->ShcoreDll         = shcore;
    wsi->DisplayCount      = 0;
    wsi->DisplayCapacity   = MAX_DISPLAYS;
    wsi->DisplayEventFlags = PAL_DISPLAY_EVENT_FLAGS_NONE;
    PAL_EnumerateDisplays(wsi);

    /* initialize the handle table for window objects */
    PAL_MemoryLayoutInit(&table_layout);
    PAL_MemoryLayoutAdd (&table_layout, PAL_WINDOW_DATA);
    if (PAL_HandleTypeDefine(&wsi->WindowTable, &table_layout, 64, PAL_WINDOW_SYSTEM_NAMESPACE_WINDOW) != 0) {
        goto cleanup_and_fail;
    }

    /* create a hidden window to handle all system notifications */
    if (!GetClassInfoEx(module , class_name, &wndclass)) {
        wndclass.cbSize        = sizeof(WNDCLASSEX);
        wndclass.cbClsExtra    = 0;
        wndclass.cbWndExtra    = sizeof(PAL_WINDOW_SYSTEM*);
        wndclass.hInstance     = module;
        wndclass.lpszClassName = class_name;
        wndclass.lpszMenuName  = NULL;
        wndclass.lpfnWndProc   = PAL_WndProc_Notify;
        wndclass.hIcon         = LoadIcon  (0, IDI_APPLICATION);
        wndclass.hIconSm       = LoadIcon  (0, IDI_APPLICATION);
        wndclass.hCursor       = LoadCursor(0, IDC_ARROW);
        wndclass.style         = CS_CLASSDC;
        wndclass.hbrBackground = NULL;
        if (RegisterClassEx(&wndclass) == FALSE) {
            error_code = GetLastError();
            goto cleanup_and_fail;
        }
    }

    pt.x = pt.y = 0;
    moninfo.cbSize = sizeof(MONITORINFO);
    monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfo(monitor, &moninfo); rc = moninfo.rcWork;
    if ((notify_wnd = CreateWindowEx(style_ex, class_name, L"PAL__Notify", style, 0, 0, rc.right-rc.left, rc.bottom-rc.top, NULL, NULL, module, wsi)) == NULL) {
        error_code  = GetLastError();
        goto cleanup_and_fail;
    }

    /* register the window to receive notifications when devices are attached or removed */
    ZeroMemory(&dnf, sizeof(DEV_BROADCAST_DEVICEINTERFACE));
    dnf.dbcc_size  = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    dnf.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    if ((notify_dev = RegisterDeviceNotification(notify_wnd, &dnf, DEVICE_NOTIFY_WINDOW_HANDLE|DEVICE_NOTIFY_ALL_INTERFACE_CLASSES)) == NULL) {
        error_code  = GetLastError();
        goto cleanup_and_fail;
    }
    wsi->DeviceNotify = notify_dev;
    return wsi;

cleanup_and_fail:
    if (notify_dev) {
        UnregisterDeviceNotification(notify_dev);
    }
    if (notify_wnd) {
        DestroyWindow(notify_wnd);
    }
    if (wsi) {
        PAL_HandleTableDelete(&wsi->WindowTable);
    }
    if (base_addr) {
        VirtualFree(base_addr, 0, MEM_RELEASE);
    }
    if (shcore) {
        FreeLibrary(shcore);
    }
    return NULL;
}

PAL_API(void)
PAL_WindowSystemDelete
(
    struct PAL_WINDOW_SYSTEM *wsi
)
{
    if (wsi) {
        /* destroy all application windows */
        PAL_HANDLE_TABLE_VISITOR_INIT visitor;
        visitor.Callback = PAL_DestroyWindowChunk;
        visitor.Context  =(pal_uintptr_t) wsi;
        visitor.Flags    = 0;
        PAL_HandleTableVisit (&wsi->WindowTable, &visitor);
        PAL_HandleTableDelete(&wsi->WindowTable);
        /* unregister device notification events */
        if (wsi->DeviceNotify) {
            UnregisterDeviceNotification(wsi->DeviceNotify);
        }
        /* destroy the notification window */
        if (wsi->NotifyWindow) {
            HWND hwnd = wsi->NotifyWindow;
            BOOL  ret;
            MSG   msg;
            if (DestroyWindow(hwnd) != FALSE) {
                while ((ret = GetMessage(&msg, hwnd, 0, 0)) != 0) {
                    if (ret == -1 || msg.message == WM_QUIT) {
                        break;
                    }
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }
        }
        /* unload Shcore.dll from the process */
        if (wsi->ShcoreDll) {
            FreeLibrary(wsi->ShcoreDll);
        }
        /* free the WSI memory block */
        VirtualFree(wsi, 0, MEM_RELEASE);
    }
}

PAL_API(void)
PAL_WindowSystemUpdate
(
    struct PAL_WINDOW_SYSTEM *wsi
)
{
    PAL_WindowSystemProcessDisplayUpdates(wsi);
    PAL_WindowSystemProcessWindowUpdates(wsi);
}

PAL_API(pal_uint32_t)
PAL_WindowSystemDisplayCount
(
    struct PAL_WINDOW_SYSTEM *wsi
)
{
    return wsi->DisplayCount;
}

PAL_API(struct PAL_DISPLAY_INFO*)
PAL_WindowSystemPrimaryDisplay
(
    struct PAL_WINDOW_SYSTEM *wsi,
    pal_uint32_t   *primary_index
)
{
    if (wsi->DisplayCount > 0) {
        PAL_Assign(primary_index, wsi->PrimaryDisplay.DeviceIndex);
        return &wsi->PrimaryDisplay;
    } else {
        PAL_Assign(primary_index, 0);
        return NULL;
    }
}

PAL_API(int)
PAL_WindowSystemQueryDisplayInfo
(
    struct PAL_DISPLAY_INFO *info, 
    struct PAL_WINDOW_SYSTEM *wsi, 
    pal_uint32_t    display_index
)
{
    if (display_index < wsi->DisplayCount) {
        PAL_DisplayDataToDisplayInfo(info, &wsi->ActiveDisplays[display_index], display_index);
        return  0;
    } else {
        PAL_ZeroMemory(info, sizeof(PAL_DISPLAY_INFO));
        return -1;
    }
}

