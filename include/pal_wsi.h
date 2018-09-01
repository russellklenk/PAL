/**
 * @summary Define the PAL types and API entry points used for working the 
 * native windowing system on the host.
 */
#ifndef __PAL_WSI_H__
#define __PAL_WSI_H__

#ifndef PAL_NO_INCLUDES
#   include "pal.h"
#   include "pal_time.h"
#   include "pal_memory.h"
#endif

/* @summary Forward-declare the types exported by this module.
 * The type definitions are included in the platform-specific header.
 */
struct  PAL_WINDOW_INIT;
struct  PAL_WINDOW_STATE;
struct  PAL_WINDOW_SYSTEM;
struct  PAL_DISPLAY_INFO;
struct  PAL_DISPLAY_STATE;

/* @summary Objects in the window system interface are opaque 32-bit handles.
 */
typedef PAL_HANDLE PAL_INPUT;
typedef PAL_HANDLE PAL_WINDOW;

/* @summary Define the data used to configure the window system interface.
 * The window system interface (WSI) interacts with windows, displays and user input devices.
 */
typedef struct PAL_WINDOW_SYSTEM_INIT {
    pal_uint32_t                       MaxInputSnapshots;      /* The maximum number of PAL_INPUT objects the application will create. */
    pal_uint32_t                       MaxGamepadDevices;      /* The maximum number of gamepad devices from which the application will accept user input. */
    pal_uint32_t                       MaxPointerDevices;      /* The maximum number of pointing devices from which the application will accept user input. */
    pal_uint32_t                       MaxKeyboardDevices;     /* The maximum number of keyboard devices from which the application will accept user input. */
} PAL_WINDOW_SYSTEM_INIT;

/* @summary Define the information returned about a display attached to the system.
 */
typedef struct PAL_DISPLAY_INFO {
    pal_uintptr_t                      DeviceId;               /* Opaque data uniquely identifying the device. */
    pal_uint32_t                       DeviceIndex;            /* The index of the device within the window system interface. The device index may change between updates. */
    pal_uint32_t                       DisplayDpiX;            /* The horizontal dots-per-inch setting of the display containing the window. */
    pal_uint32_t                       DisplayDpiY;            /* The vertical dots-per-inch setting of the display containing the window. */
    pal_sint32_t                       DisplayPositionX;       /* The display top-left corner position, in virtual display space. */
    pal_sint32_t                       DisplayPositionY;       /* The display top-left corner position, in virtual display space. */
    pal_uint32_t                       DisplaySizeX;           /* The display width, in physical pixels. */
    pal_uint32_t                       DisplaySizeY;           /* The display height, in physical pixels. */
    pal_uint32_t                       RefreshRateHz;          /* The display refresh rate, in Hertz. */
} PAL_DISPLAY_INFO;

/* @summary Define the data used to create and configure a window.
 */
typedef struct PAL_WINDOW_INIT {
    struct PAL_DISPLAY_INFO           *TargetDisplay;          /* The target display. If specified, window position and size are clamped to the display. */
    pal_char_t const                  *WindowTitle;            /* A nul-terminated string specifying the window title. */
    pal_uint32_t                       CreateFlags;            /* One or more bitwise-OR'd values of the PAL_WINDOW_CREATE_FLAGS enumeration. */
    pal_uint32_t                       StyleFlags;             /* One or more bitwise-OR'd values of the PAL_WINDOW_STYLE enumeration. */
    pal_sint32_t                       PositionX;              /* The x-coordinate of the upper-left corner of the window in virtual display space. If zero, and a target display is specified, this value is taken from the target display. */
    pal_sint32_t                       PositionY;              /* The y-coordinate of the upper-left corner of the window in virtual display space. If zero, and a target display is specified, this value is taken from the target display. */
    pal_uint32_t                       SizeX;                  /* The horizontal dimension of the window client area in logical pixels. If zero, and a target display is specified, this value is taken from the target display. */
    pal_uint32_t                       SizeY;                  /* The vertical dimension of the window client area in logical pixels. If zero, and a target display is specified, this value is taken from the target display. */
} PAL_WINDOW_INIT;

/* @summary Define the state data associated with a single window.
 * The window state is returned by PAL_WindowCreate, PAL_WindowDelete and PAL_WindowPollEvents.
 */
typedef struct PAL_WINDOW_STATE {
    pal_uint64_t                       UpdateTime;             /* The nanosecond timestamp at which the state was last updated. */
    pal_uint32_t                       EventFlags;             /* One or more bitwise OR'd PAL_WINDOW_EVENT_FLAGS indicating changes that have occurred since the previous update. */
    pal_uint32_t                       EventCount;             /* The number of events that were processed since the previous update. */
    pal_uint32_t                       StatusFlags;            /* One or more bitwise OR'd PAL_WINDOW_STATUS_FLAGS specifying the current window state. */
    pal_uint32_t                       DisplayDpiX;            /* The horizontal dots-per-inch setting of the display containing the window. */
    pal_uint32_t                       DisplayDpiY;            /* The vertical dots-per-inch setting of the display containing the window. */
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
} PAL_WINDOW_STATE;

/* @summary Define the set of flags that can be bitwise OR'd in PAL_WINDOW_STATE::EventFlags to represent events that have occured since the previous update.
 */
typedef enum PAL_WINDOW_EVENT_FLAGS {
    PAL_WINDOW_EVENT_FLAGS_NONE            = (0UL  <<  0),     /* No events have occurred since the previous update. */
    PAL_WINDOW_EVENT_FLAG_CREATED          = (1UL  <<  0),     /* The window has been created. This event is set only by PAL_WindowCreate. */
    PAL_WINDOW_EVENT_FLAG_CLOSED           = (1UL  <<  1),     /* The window has been closed.*/        
    PAL_WINDOW_EVENT_FLAG_DESTROYED        = (1UL  <<  2),     /* The window has been destroyed. This event is set only by PAL_WindowDestroy.*/
    PAL_WINDOW_EVENT_FLAG_SHOWN            = (1UL  <<  3),     /* The window has become visible. */
    PAL_WINDOW_EVENT_FLAG_HIDDEN           = (1UL  <<  4),     /* The window has become hidden. */
    PAL_WINDOW_EVENT_FLAG_ACTIVATED        = (1UL  <<  5),     /* The window has become activated. */
    PAL_WINDOW_EVENT_FLAG_DEACTIVATED      = (1UL  <<  6),     /* The window has become deactivated. */
    PAL_WINDOW_EVENT_FLAG_MONITOR_CHANGED  = (1UL  <<  7),     /* The window has been moved to a different monitor. */
    PAL_WINDOW_EVENT_FLAG_DPI_CHANGED      = (1UL  <<  8),     /* The DPI setting of the display containing the window has changed. */
    PAL_WINDOW_EVENT_FLAG_MODE_CHANGED     = (1UL  <<  9),     /* The active display resolution or refresh rate has changed. */
    PAL_WINDOW_EVENT_FLAG_POSITION_CHANGED = (1UL  << 10),     /* The window position has changed. */
    PAL_WINDOW_EVENT_FLAG_SIZE_CHANGED     = (1UL  << 11),     /* The window size has changed. */
    PAL_WINDOW_EVENT_FLAG_GO_FULLSCREEN    = (1UL  << 12),     /* The window style has changed to that of a fullscreen window. */
    PAL_WINDOW_EVENT_FLAG_GO_WINDOWED      = (1UL  << 13),     /* The window style has changed to that of an ordinary window. */
} PAL_WINDOW_EVENT_FLAGS;

/* @summary Define the set of flags that can be bitwise OR'd in PAL_DISPLAY_STATE::EventFlags to represent events that have occurred since the previous update.
 */
typedef enum PAL_DISPLAY_EVENT_FLAGS {
    PAL_DISPLAY_EVENT_FLAGS_NONE           = (0UL  <<  0),     /* No events have occurred since the previous update. */
    PAL_DISPLAY_EVENT_FLAG_DISPLAY_ATTACH  = (1UL  <<  0),     /* One or more displays were attached to the system. */
    PAL_DISPLAY_EVENT_FLAG_DISPLAY_REMOVE  = (1UL  <<  1),     /* One or more displays were removed from the system. */
    PAL_DISPLAY_EVENT_FLAG_GPU_ATTACH      = (1UL  <<  2),     /* One or more display adapters were attached to the system. */
    PAL_DISPLAY_EVENT_FLAG_GPU_REMOVE      = (1UL  <<  3),     /* One or more display adapters were removed from the system. */
    PAL_DISPLAY_EVENT_FLAG_MODE_CHANGE     = (1UL  <<  4),     /* The attributes of one or more displays have been updated. */
    PAL_DISPLAY_EVENT_FLAG_PRESENT_MODE    = (1UL  <<  5),     /* The system is in presentation mode. */
} PAL_DISPLAY_EVENT_FLAGS;

/* @summary Define the set of flags that can be bitwise OR'd in PAL_WINDOW_INIT::CreateFlags to control window creation.
 */
typedef enum PAL_WINDOW_CREATE_FLAGS { 
    PAL_WINDOW_CREATE_FLAGS_NONE           = (0UL  <<  0),     /* No special behavior is requested. */
    PAL_WINDOW_CREATE_FLAG_USE_POSITION    = (1UL  <<  0),     /* The window should be created at the position specified in the PAL_WINDOW_INIT structure. */
    PAL_WINDOW_CREATE_FLAG_USE_SIZE        = (1UL  <<  1),     /* The window should be created to have a client area with the size specified in the PAL_WINDOW_INIT structure. */
    PAL_WINDOW_CREATE_FLAG_USE_DISPLAY     = (1UL  <<  2),     /* The window should be created on the display specified in the PAL_WINDOW_INIT structure. */
} PAL_WINDOW_CREATE_FLAGS;

/* @summary Define the set of flags that can be bitwise-ORd in PAL_WINDOW_INIT::StyleFlags to control window appearance.
 */
typedef enum PAL_WINDOW_STYLE_FLAGS {
    PAL_WINDOW_STYLE_FLAGS_NONE            = (0UL  <<  0),     /* No special flags are specified. Use defaults. */
    PAL_WINDOW_STYLE_FLAG_CHROME           = (1UL  <<  0),     /* The window should have the standard chrome. Not supported for fullscreen windows. */
    PAL_WINDOW_STYLE_FLAG_CENTER           = (1UL  <<  1),     /* The window should be centered on its containing display. Not supported for fullscreen windows. */
    PAL_WINDOW_STYLE_FLAG_RESIZABLE        = (1UL  <<  2),     /* The window should be resizable. Not supported for fullscreen windows. */
    PAL_WINDOW_STYLE_FLAG_FULLSCREEN       = (1UL  <<  3),     /* The window should be borderless, with no chrome, and cover the entire display. */
} PAL_WINDOW_STYLE_FLAGS;

/* @summary Define window status flags. 
 */
typedef enum PAL_WINDOW_STATUS_FLAGS {
    PAL_WINDOW_STATUS_FLAGS_NONE           = (0UL  <<  0),     /* The window is operating normally with no special status. */
    PAL_WINDOW_STATUS_FLAG_CLOSED          = (1UL  <<  0),     /* The window has been closed. */
    PAL_WINDOW_STATUS_FLAG_FULLSCREEN      = (1UL  <<  1),     /* The window currently uses the fullscreen style. */
    PAL_WINDOW_STATUS_FLAG_WINDOWED        = (1UL  <<  2),     /* The window currently uses the windowed style. */
} PAL_WINDOW_STATUS_FLAGS;

#ifdef __cplusplus
extern "C" {
#endif

/* @summary Create a object used to interact with the windowing system on the host.
 * @param init Data used to configure the window system interface.
 * @return A pointer to the window system, or NULL.
 */
PAL_API(struct PAL_WINDOW_SYSTEM*)
PAL_WindowSystemCreate
(
    struct PAL_WINDOW_SYSTEM_INIT *init
);

/* @summary Free resources associated with the window system interface. All window and display handles are invalidated.
 * @param wsi The window system interface to delete.
 */
PAL_API(void)
PAL_WindowSystemDelete
(
    struct PAL_WINDOW_SYSTEM *wsi
);

/* @summary Create an object representing the state of input devices attached to the system.
 * The PAL_INPUT handle can be supplied to functions that might update the state of the input devices.
 * Calls to this function must be externally synchronized.
 * @param wsi The window system interface from which the input object will be allocated.
 * @return The handle of the input snapshot, or PAL_HANDLE_INVALID.
 */
PAL_API(PAL_INPUT)
PAL_InputCreate
(
    struct PAL_WINDOW_SYSTEM *wsi
);

/* @summary Delete an object representing the state of input devices attached to the system.
 * Calls to this function must be externally synchronized.
 * @param wsi The window system interface from which the input object was allocated.
 * @param input The handle of the input object to delete.
 */
PAL_API(void)
PAL_InputDelete
(
    struct PAL_WINDOW_SYSTEM *wsi, 
    PAL_INPUT               input
);

/* @summary Process pending window system events for all windows.
 * @param wsi The window system interface to update.
 */
PAL_API(void)
PAL_WindowSystemUpdate
(
    struct PAL_WINDOW_SYSTEM *wsi
);

/* @summary Query the system for the number of displays attached to the current desktop session.
 * @return The number of displays attached to the current desktop session.
 */
PAL_API(pal_uint32_t)
PAL_WindowSystemDisplayCount
(
    struct PAL_WINDOW_SYSTEM *wsi
);

/* @summary Retrieve the display information for the primary display output.
 * @param wsi The window system interface to query.
 * @param primary_index On return, this location is updated with the index of the primary display.
 * @return A structure managed by the window system interface describing the primary display.
 */
PAL_API(struct PAL_DISPLAY_INFO*)
PAL_WindowSystemPrimaryDisplay
(
    struct PAL_WINDOW_SYSTEM *wsi,
    pal_uint32_t   *primary_index
);

/* @summary Retrieve information about a specific display output attached to the system.
 * @param info Pointer to a PAL_DISPLAY_INFO structure to be filled with display attributes.
 * @param wsi The window system interface to query.
 * @param display_index The zero-based index of the display output whose attributes are being queried.
 * @return Zero if the display information was written to the info buffer, or non-zero if an error occurred.
 */
PAL_API(int)
PAL_WindowSystemQueryDisplayInfo
(
    struct PAL_DISPLAY_INFO *info, 
    struct PAL_WINDOW_SYSTEM *wsi, 
    pal_uint32_t    display_index
);

/* @summary Retrieve information about the display output containing the majority of an application window.
 * @param info Pointer to a PAL_DISPLAY_INFO structure to be filled with display attributes.
 * @param wsi The window system interface to query.
 * @param window The handle of the window associated with the query.
 * @return Zero if the display information was written to the info buffer, or non-zero if an error occurred.
 */
PAL_API(int)
PAL_WindowSystemQueryWindowDisplay
(
    struct PAL_DISPLAY_INFO *info, 
    struct PAL_WINDOW_SYSTEM *wsi, 
    PAL_WINDOW             window
);

/* @summary Create an application window to display output and receive user input.
 * @param state Pointer to a PAL_WINDOW_STATE structure to receive the window state.
 * @param wsi The window system interface used to create the window.
 * @param init Data used to specify window attributes and configure window creation.
 * @return The window handle, or PAL_HANDLE_INVALID if the window could not be created.
 */
PAL_API(PAL_WINDOW)
PAL_WindowCreate
(
    struct PAL_WINDOW_STATE *state, 
    struct PAL_WINDOW_SYSTEM  *wsi, 
    struct PAL_WINDOW_INIT   *init
);

/* @summary Destroy an application window.
 * @param state Pointer to a PAL_WINDOW_STATE structure to receive the window state.
 * @param wsi The window system interface used to create the window.
 * @param window The handle of the window to destroy, returned by a prior call to PAL_WindowCreate.
 */
PAL_API(void)
PAL_WindowDelete
(
    struct PAL_WINDOW_STATE *state, 
    struct PAL_WINDOW_SYSTEM  *wsi, 
    PAL_WINDOW              window
);

/* @summary Retrieve the most recent state data associated with an application window.
 * This routine differs from PAL_WindowUpdateState in that it does not first process waiting messages.
 * @param state Pointer to a PAL_WINDOW_STATE structure to receive the window state.
 * @param wsi The window system interface used to create the window.
 * @param window The handle of the window.
 * @return Zero if the window state was updated, or non-zero if an error occurred.
 */
PAL_API(int)
PAL_WindowQueryState
(
    struct PAL_WINDOW_STATE *state, 
    struct PAL_WINDOW_SYSTEM  *wsi, 
    PAL_WINDOW              window
);

/* @summary Process any waiting messages and then retrieve the state data for an application window.
 * Window messages are generally processed during the window system update. This routine is intended to perform a "late latch" style update to reduce input latency.
 * @param state Pointer to a PAL_WINDOW_STATE structure to receive the window state.
 * @param wsi The window system interface used to create the window.
 * @param window The handle of the window.
 * @return Zero if the window state was updated, or non-zero if an error occurred.
 */
PAL_API(int)
PAL_WindowUpdateState
(
    struct PAL_WINDOW_STATE *state, 
    struct PAL_WINDOW_SYSTEM  *wsi, 
    PAL_WINDOW              window
);

/* @summary Determine whether a window has been closed based on a state snapshot.
 * @param state The window state snapshot.
 * @return Non-zero if the window is closed, or zero if the window is not closed.
 */
PAL_API(int)
PAL_WindowIsClosed
(
    struct PAL_WINDOW_STATE *state
);

/* @summary Determine whether a window is a fullscreen-style window based on a state snapshot.
 * @param state The window state snapshot.
 * @return Non-zero if the window is fullscreen, or zero if the window is not fullscreen.
 */
PAL_API(int)
PAL_WindowIsFullscreen
(
    struct PAL_WINDOW_STATE *state
);

#ifdef __cplusplus
}; /* extern "C" */
#endif

/* @summary Include the appropriate platform-specific header.
 */
#if   PAL_TARGET_PLATFORM == PAL_PLATFORM_WIN32 || PAL_TARGET_PLATFORM == PAL_PLATFORM_WINRT
    #include "pal_win32_wsi.h"
#elif PAL_TARGET_PLATFORM == PAL_PLATFORM_UWP
    #include "pal_uwp_wsi.h"
#elif PAL_TARGET_PLATFORM == PAL_PLATFORM_XCB
    #include "pal_xcb_wsi.h"
#elif PAL_TARGET_PLATFORM == PAL_PLATFORM_ANDROID
    #include "pal_android_wsi.h"
#elif PAL_TARGET_PLATFORM == PAL_PLATFORM_IOS
    #include "pal_ios_wsi.h"
#elif PAL_TARGET_PLATFORM == PAL_PLATFORM_MACOS
    #include "pal_macos_wsi.h"
#else
    #error    pal_wsi.h: No implementation of the abstraction layer for your platform.
#endif

#endif /* __PAL_WSI_H__ */

