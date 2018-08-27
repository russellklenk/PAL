/**
 * @summary Implement the PAL Window System Interface APIs for the Win32 desktop platform.
 */
#include "pal_win32_dylib.h"
#include "pal_win32_time.h"
#include "pal_win32_wsi.h"

#include <initguid.h>
#include <XInput.h>

/* @@@ NOTES:
 * Here's what you were thinking when you had to stop working on this module.
 * Each application tick would use PAL_InputCreate to allocate one or two PAL_INPUT objects.
 * The first PAL_INPUT would be used to gather events during the primary PAL_WindowSystemUpdate.
 * The second PAL_INPUT would be used to gather events during the late latch PAL_WindowSystemUpdate.
 * Calls to PAL_WindowSystemUpdate take a PAL_INPUT handle.
 * They acquire an exclusive lock for updating the input state.
 * Collisions should generally not happen ever; if they do happen, it will be during the late-latch update.
 * They copy the LatestState into the 'old' buffer of the PAL_WSI_INPUT_DATA.
 * They then process events for the notify window, which updates the LatestState buffer.
 * They copy the LatestState into the 'new' buffer of the PAL_WSI_INPUT_DATA.
 * The user code can then generate a PAL_INPUT_EVENTS for the PAL_INPUT (primary).
 * For the late latch, the same process is performed, except the late-latch PAL_INPUT is supplied. (needed???)
 * The user code can then generate a PAL_INPUT_EVENTS for the PAL_INPUT (late latch).
 * The user code can then merge the two PAL_INPUT_EVENTS if they need to - but I don't think they will.
 *
 * The memory layout for a PAL_WSI_INPUT_DEVICE_LIST is somewhat involved.
 * We want a single contiguous buffer that looks like this:
 * [PAL_WSI_INPUT_DEVICE_LIST] - struct instance at start of allocation.
 * [DeviceHandles array] - StateBase points to the start of this array.
 * [DeviceState array] - must be computed manually. beware of alignment.
 * Since the size varies for each type of device, the sizes and counts are computed once and stored.
 * The stored size does not include the size of the PAL_WSI_INPUT_DEVICE_LIST.
 * This is ideal because we can PAL_ZeroMemory(devices->StateBase, wsi->XXXXStateSize) 
 * and PAL_CopyMemory(devices->StateBase, ...);
 */

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

/* @summary Specify the maximum number of input devices of each type that can be attached to the system.
 */
#ifndef PAL_WSI_MAX_INPUT_DEVICES
#   define PAL_WSI_MAX_INPUT_DEVICES     8
#endif

/* @summary Specify the maximum number of keys that can be reported as down, pressed or released in a single update.
 */
#ifndef PAL_WSI_MAX_KEYS
#   define PAL_WSI_MAX_KEYS              256
#endif

/* @summary Define the maximum number of buttons that can be reported as down, pressed or released in a single update.
 */
#ifndef PAL_WSI_MAX_BUTTONS
#   define PAL_WSI_MAX_BUTTONS           32
#endif

/* @summary Define stream indices for the handle tables used by this module.
 */
#ifndef PAL_WINDOW_SYSTEM_STREAMS
#   define PAL_WINDOW_SYSTEM_STREAMS
/** PAL_WINDOW_SYSTEM::WindowTable **/
#   define PAL_WSS_WINDOW_DATA           0
/** PAL_WINDOW_SYSTEM::InputTable **/
#   define PAL_WSI_INPUT_DATA            0
#endif

/* @summary Helper macro to retrieve a pointer to the start of a device state structure.
 * @param _list A pointer to a PAL_WSI_INPUT_DEVICE_LIST.
 * @param _index The zero-based index of the device record to retrieve.
 * @param _size The size of a device record, in bytes.
 * @return A pointer to the start of the device record.
 */
#ifndef PAL_InputDeviceListState
#define PAL_InputDeviceListState(_list, _index, _size)                         \
    (((_list)->DeviceState) + ((_index) * (_size)))
#endif

/* @summary Retrieve a gamepad device state from a device list.
 * @param _list A pointer to a PAL_WSI_INPUT_DEVICE_LIST for gamepad devices.
 * @param _index The zero-based index of the device state record to retrieve.
 * @return A pointer to the PAL_WSI_GAMEPAD_STATE representing the state of the device.
 */
#ifndef PAL_GamepadDeviceState
#define PAL_GamepadDeviceState(_list, _index)                                  \
    ((PAL_WSI_GAMEPAD_STATE*) PAL_InputDeviceListState(_list, _index, sizeof(PAL_WSI_GAMEPAD_STATE)))
#endif

/* @summary Retrieve a pointer device state from a device list.
 * @param _list A pointer to a PAL_WSI_INPUT_DEVICE_LIST for pointer devices.
 * @param _index The zero-based index of the device state record to retrieve.
 * @return A pointer to the PAL_WSI_POINTER_STATE representing the state of the device.
 */
#ifndef PAL_PointerDeviceState
#define PAL_PointerDeviceState(_list, _index)                                  \
    ((PAL_WSI_POINTER_STATE*) PAL_InputDeviceListState(_list, _index, sizeof(PAL_WSI_POINTER_STATE)))
#endif

/* @summary Retrieve a keyboard device state from a device list.
 * @param _list A pointer to a PAL_WSI_INPUT_DEVICE_LIST for keyboard devices.
 * @param _index The zero-based index of the device state record to retrieve.
 * @return A pointer to the PAL_WSI_KEYBOARD_STATE representing the state of the device.
 */
#ifndef PAL_KeyboardDeviceState
#define PAL_KeyboardDeviceState(_list, _index)                                 \
    ((PAL_WSI_KEYBOARD_STATE*)PAL_InputDeviceListState(_list, _index, sizeof(PAL_WSI_KEYBOARD_STATE)))
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

/* @summary Forward-declare the XInput types.
 */
struct _XINPUT_STATE;
struct _XINPUT_VIBRATION;
struct _XINPUT_KEYSTROKE;
struct _XINPUT_CAPABILITIES;
struct _XINPUT_BATTERY_INFORMATION;

/* @summary Function pointer types for dynamically-loaded Win32 API entry points.
 */
typedef HRESULT (WINAPI *SetProcessDpiAwareness_Func     )(PROCESS_DPI_AWARENESS);
typedef HRESULT (WINAPI *GetDpiForMonitor_Func           )(HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT*);

/* @summary Declare the signatures for the functions loaded from XInput.
 */
typedef void    (WINAPI *XInputEnable_Func               )(BOOL);
typedef DWORD   (WINAPI *XInputGetState_Func             )(DWORD, struct _XINPUT_STATE*);
typedef DWORD   (WINAPI *XInputSetState_Func             )(DWORD, struct _XINPUT_VIBRATION*);
typedef DWORD   (WINAPI *XInputGetKeystroke_Func         )(DWORD, DWORD , struct _XINPUT_KEYSTROKE*);
typedef DWORD   (WINAPI *XInputGetCapabilities_Func      )(DWORD, DWORD , struct _XINPUT_CAPABILITIES*);
typedef DWORD   (WINAPI *XInputGetBatteryInformation_Func)(DWORD, BYTE  , struct _XINPUT_BATTERY_INFORMATION*);
typedef DWORD   (WINAPI *XInputGetAudioDeviceIds_Func    )(DWORD, LPWSTR, UINT*, LPWSTR, UINT*);

/* @summary Define the dispatch table used to access Win32 API functions that may or may not be present on the host.
 */
typedef struct PAL_WSI_WIN32_DISPATCH {
    SetProcessDpiAwareness_Func            SetProcessDpiAwareness;             /* The SetProcessDpiAwareness entry point. Never NULL. */
    GetDpiForMonitor_Func                  GetDpiForMonitor;                   /* The GetDpiForMonitor entry point. Never NULL. */
    PAL_MODULE                             ShcoreModule;                       /* The handle of the Shcore.dll loaded into the process address space, or NULL. */
} PAL_WSI_WIN32_DISPATCH;

/* @summary Define the dispatch table used to access XInput functions that may or may not be present on the host.
 */
typedef struct PAL_WSI_XINPUT_DISPATCH {
    XInputEnable_Func                      XInputEnable;                       /* The XInputEnable entry point. Never NULL. */
    XInputGetState_Func                    XInputGetState;                     /* The XInputGetState entry point. Never NULL. */
    XInputSetState_Func                    XInputSetState;                     /* The XInputSetState entry point. Never NULL. */
    XInputGetKeystroke_Func                XInputGetKeystroke;                 /* The XInputGetKeystroke entry point. Never NULL. */
    XInputGetCapabilities_Func             XInputGetCapabilities;              /* The XInputGetCapabilities entry point. Never NULL. */
    XInputGetBatteryInformation_Func       XInputGetBatteryInformation;        /* The XInputGetBatteryInformation entry point. Never NULL. */
    XInputGetAudioDeviceIds_Func           XInputGetAudioDeviceIds;            /* The XInputGetAudioDeviceIds entry point. Never NULL. */
    PAL_MODULE                             XInputModule;                       /* The module handle of the XInput DLL loaded into the process address space, or NULL. */
} PAL_WSI_XINPUT_DISPATCH;

/* @summary Define the state data associated with a gamepad device.
 */
typedef struct PAL_WSI_GAMEPAD_STATE {
    pal_uint32_t                           LTrigger;                           /* The left trigger value, in [0, 255]. */
    pal_uint32_t                           RTrigger;                           /* The right trigger value, in [0, 255]. */
    pal_uint32_t                           Buttons;                            /* An array of 32 bits representing the button states, where a set bit indicates a button in the down position. */
    pal_float32_t                          LStick[4];                          /* The left analog stick X, Y, magnitude and normalized magnitude. */
    pal_float32_t                          RStick[4];                          /* The right analog stick X, Y, magnitude and normalized magnitude. */
} PAL_WSI_GAMEPAD_STATE;

/* @summary Define the state data associated with a pointer device such as a mouse or a pen.
 */
typedef struct PAL_WSI_POINTER_STATE {
    pal_sint32_t                           Pointer[2];                         /* The absolute X and Y coordinates of the pointer in virtual display space. */
    pal_sint32_t                           Relative[3];                        /* The high definition relative X, Y and Z (wheel) values. The X and Y values are specified in mickeys. */
    pal_uint32_t                           Buttons;                            /* An array of 32 bits representing the button states, where a set bit indicates a button in the down state. */
    pal_uint32_t                           PointerFlags;                       /* One or more values of the PAL_WSI_POINTER_FLAGS enumeration used to specify post-processing to be performed on the pointer data. */
} PAL_WSI_POINTER_STATE;

/* @summary Define the state data associated with a keyboard device.
 */
typedef struct PAL_WSI_KEYBOARD_STATE {
    pal_uint32_t                           KeyState[8];                        /* An array of 256 bits mapping scan code to key state, where a set bit indicates a key in the down state. */
} PAL_WSI_KEYBOARD_STATE;

/* @summary Define the data associated with a list of input devices of a particular type.
 * Macros are used to access the DeviceState blob and retrieve the state data for each device.
 */
typedef struct PAL_WSI_INPUT_DEVICE_LIST {
    pal_uint8_t                           *StateBase;                          /* The base address of the DeviceHandle and DeviceState data arrays. */
    pal_uint32_t                           MaxDevices;                         /* The maximum number of devices of this particular type that can appear in the device list. */
    pal_uint32_t                           DeviceCount;                        /* The number of devices that actually appear in the list. */
    HANDLE                                *DeviceHandle;                       /* An array of MaxDevices handles, of which DeviceCount are actually valid, specifying the unique operating system identifier of each input device. */
    pal_uint8_t                           *DeviceState;                        /* A pointer to an array of MaxDevices objects, of which DeviceCount are actually value, specifying the state data associated with each input device. */
} PAL_WSI_INPUT_DEVICE_LIST;

/* @summary Define the data associated with an input device membership set computed from two device list snapshots.
 * This structure is a temporary structure allocated on the stack when computing an input snapshot.
 */
typedef struct PAL_WSI_INPUT_DEVICE_SET {
#   define CAPACITY                       (PAL_WSI_MAX_INPUT_DEVICES * 2)
    pal_uint32_t                           MaxDevices;                         /* The maximum number of devices of this particular type that can appear in the device list. */
    pal_uint32_t                           DeviceCount;                        /* The number of devices that actually appear in the set.*/
    HANDLE                                 DeviceIds [CAPACITY];               /* An array of MaxDevices handles, of which DeviceCount are actually valid, specifying the ... */
    pal_uint32_t                           Membership[CAPACITY];               /* An array of MaxDevices bitflags, of which DeviceCount are actually valid, specifying the device membership attributes. */
    pal_uint8_t                            PrevIndex [CAPACITY];               /* An array of MaxDevices index values, of which DeviceCount are actually valid, specifying the index of the device in the older device list. */
    pal_uint8_t                            CurrIndex [CAPACITY];               /* An array of MaxDevices index values, of which DeviceCount are actually valid, specifying the index of the device in the newer device list. */
#   undef  CAPACITY
} PAL_WSI_INPUT_DEVICE_SET;

/* @summary Define the data associated with a snapshot of the input devices attached to the system and their associated device state.
 */
typedef struct PAL_WSI_INPUT_DEVICE_STATE {
    pal_uint64_t                           LastUpdateTime;                     /* The timestamp, in ticks, at which this state data was last written. */
    pal_uint32_t                           GamepadPorts;                       /* A bit vector where a bit is set if the corresponding gamepad was connected. */
    pal_uint32_t                           Reserved;                           /* Reserved for future use. Set to zero. */
    PAL_WSI_INPUT_DEVICE_LIST              GamepadDeviceList;                  /* The list of gamepad devices attached to the system, including the observed state of each device. */
    PAL_WSI_INPUT_DEVICE_LIST              PointerDeviceList;                  /* The list of pointer devices attached to the system, including the observed state of each device. */
    PAL_WSI_INPUT_DEVICE_LIST              KeyboardDeviceList;                 /* The list of keyboard devices attached to the system, including the observed state of each device. */
} PAL_WSI_INPUT_DEVICE_STATE;

/* @summary Define the data associated with a PAL_INPUT handle. 
 */
typedef struct PAL_INPUT_DEVICE_DATA {
    struct PAL_WINDOW_SYSTEM              *WindowSystem;                       /* A pointer back to the window system interface that manages the input device data. */
    pal_uint32_t                           WriteIndex;                         /* The zero-based index into DeviceState of the current write buffer. */
    pal_uint32_t                           Reserved;                           /* Reserved for future use. Set to zero. */
    PAL_WSI_INPUT_DEVICE_STATE             DeviceState[2];                     /* The read and write snapshots of the input device state. The write buffer represents the newer state, while the read buffer represents the older state. */
} PAL_INPUT_DEVICE_DATA;

/* @summary Define the data associated with the host operating system window manager.
 * The window system interface is responsible for tracking active application windows and processing user input.
 * Swap the read and write buffers for the PAL_INPUT.
 * Copy LatestState into the current read buffer for the PAL_INPUT.
 * Update LatestState based on messages to NotifyWindow.
 * Copy LatestState into the current write buffer for the PAL_INPUT. 
 */
typedef struct PAL_WINDOW_SYSTEM {
    HWND                                   NotifyWindow;                       /* An invisible window used for receiving system and device change notifications. */
    PAL_DISPLAY_DATA                      *ActiveDisplays;                     /* An array of DisplayCount items specifying information about active displays. */
    pal_uint32_t                           DisplayCount;                       /* The number of displays attached to the system (as of the most recent update). */
    pal_uint32_t                           DisplayCapacity;                    /* The maximum number of displays for which information can be stored. */
    pal_uint32_t                           DisplayEventFlags;                  /* One or more bitwise OR'd PAL_DISPLAY_EVENT_FLAGS indicating changes that have occurred since the previous update. */
    pal_uint32_t                           Reserved;                           /* Reserved for future use. Set to zero. */
    PAL_DISPLAY_INFO                       PrimaryDisplay;                     /* Information about the primary display output. */
    pal_uint8_t                            DisplayPad[48];                     /* Padding to separate display data from input state data. */

    pal_uint32_t                           MaxGamepadDevices;                  /* The maximum number of gamepad devices for which input events will be reported. */
    pal_uint32_t                           MaxPointerDevices;                  /* The maximum number of pointer devices for which input events will be reported. */
    pal_uint32_t                           MaxKeyboardDevices;                 /* The maximum number of keyboard devices for which input events will be reported. */
    pal_uint32_t                           GamepadListSize;                    /* The size of a gamepad device list, in bytes, not including the size of the PAL_WSI_INPUT_DEVICE_LIST structure. */
    pal_uint32_t                           PointerListSize;                    /* The size of a pointer device list, in bytes, not including the size of the PAL_WSI_INPUT_DEVICE_LIST structure. */
    pal_uint32_t                           KeyboardListSize;                   /* The size of a keyboard device list, in bytes, not including the size of the PAL_WSI_INPUT_DEVICE_LIST structure. */
    pal_uint8_t                            InputPad[40];                       /* Padding to separate read-only input constants from input state data. */

    SRWLOCK                                InputUpdateLock;                    /* A reader-writer lock acquired in exclusive mode during a window system update. */
    pal_uint64_t                           LastUpdateTime;                     /* The timestamp (in ticks) of the most recent window system update. */
    pal_uint64_t                           LastGamepadPoll;                    /* The timestamp (in ticks) of the most recent poll of the full set of gamepad ports. */
    PAL_WSI_INPUT_DEVICE_STATE             LatestState;                        /* A copy of the most recent input device state observed by the system. */

    PAL_HANDLE_TABLE                       InputTable;                         /* Table mapping PAL_INPUT to PAL_INPUT_DEVICE_DATA. */
    PAL_HANDLE_TABLE                       WindowTable;                        /* Table mapping PAL_WINDOW to PAL_WINDOW_DATA. */
    PAL_WSI_WIN32_DISPATCH                 Win32Runtime;                       /* The dispatch table used to call dynamically-loaded Win32 entry points. */
    PAL_WSI_XINPUT_DISPATCH                XInputRuntime;                      /* The dispatch table used to call dynamically-loaded XInput entry points. */
    HDEVNOTIFY                             DeviceNotify;                       /* The device notification handle used to receive notification about display and GPU attach/removal. */
} PAL_WINDOW_SYSTEM;

/* @sumamry Define a wrapper around the data associated with an input device state object.
 */
typedef struct PAL_INPUT_OBJECT {
    PAL_INPUT                              Handle;                             /* The input object handle. */
    PAL_INPUT_DEVICE_DATA                 *DeviceData;                         /* The input device data stream. */
} PAL_INPUT_OBJECT;

/* @summary Define a wrapper around the data associated with a window object.
 */
typedef struct PAL_WINDOW_OBJECT {
    PAL_WINDOW                             Handle;                             /* The window object handle. */
    PAL_WINDOW_DATA                       *WindowData;                         /* The window data stream. */
} PAL_WINDOW_OBJECT;

/* @summary Define the object/handle namespaces utilized by the WSI module.
 */
typedef enum PAL_WINDOW_SYSTEM_NAMESPACE {
    PAL_WINDOW_SYSTEM_NAMESPACE_INPUT    = 1UL,                                /* The namespace for PAL_INPUT handles. */
    PAL_WINDOW_SYSTEM_NAMESPACE_WINDOW   = 2UL,                                /* The namespace for PAL_WINDOW handles. */
} PAL_WINDOW_SYSTEM_NAMESPACE;

/* @summary Define flags that can be bitwise OR'd to indicate the presence of an input device in an input device set.
 */
typedef enum PAL_WSI_DEVICE_SET_MEMBERSHIP {
    PAL_WSI_DEVICE_SET_MEMBERSHIP_NONE   =(0UL << 0),                          /* The device is not present in the previous snapshot. */
    PAL_WSI_DEVICE_SET_MEMBERSHIP_PREV   =(1UL << 0),                          /* The device is present in the previous snapshot. */
    PAL_WSI_DEVICE_SET_MEMBERSHIP_CURR   =(1UL << 1),                          /* The device is present in the current snapshot. */
} PAL_WSI_DEVICE_SET_MEMBERSHIP;

/* @summary Define flags that can be bitwise OR'd to indicate required post-processing on a pointer device position.
 */
typedef enum PAL_WSI_POINTER_FLAGS {
    PAL_WSI_POINTER_FLAGS_NONE           =(0UL << 0),                          /* No pointer post-processing is required. */
    PAL_WSI_POINTER_FLAG_ABSOLUTE        =(1UL << 0),                          /* The device only specifies absolute position. */
} PAL_WSI_POINTER_FLAGS;

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

/* @summary Define a stub no-op implementation of the XInputEnable API.
 * @param enable If enable is FALSE XInput will only send neutral data in response to XInputGetState.
 */
static void WINAPI
XInputEnable_Stub
(
    BOOL enable
)
{
    PAL_UNUSED_ARG(enable);
}

/* @summary Define a stub no-op implementation of the XInputGetState API.
 * @param user_index The index of the user's controller, in [0, 3].
 * @param state Pointer to an XINPUT_STATE structure that receives the current state of the controller.
 * @return ERROR_SUCCESS or ERROR_DEVICE_NOT_CONNECTED.
 */
static DWORD WINAPI
XInputGetState_Stub
(
    DWORD            user_index, 
    struct _XINPUT_STATE *state
)
{
    PAL_UNUSED_ARG(user_index);
    PAL_UNUSED_ARG(state);
    return ERROR_DEVICE_NOT_CONNECTED;
}

/* @summary Define a stub no-op implementation of the XInputSetState API.
 * @param user_index The index of the user's controller, in [0, 3].
 * @param vibration Pointer to an XINPUT_VIBRATION structure containing vibration information to send to the controller.
 * @return ERROR_SUCCESS or ERROR_DEVICE_NOT_CONNECTED.
 */
static DWORD WINAPI
XInputSetState_Stub
(
    DWORD                    user_index, 
    struct _XINPUT_VIBRATION *vibration
)
{
    PAL_UNUSED_ARG(user_index);
    PAL_UNUSED_ARG(vibration);
    return ERROR_DEVICE_NOT_CONNECTED;
}

/* @summary Define a stub no-op implementation of the XInputGetKeystroke API.
 * @param user_index The index of the user's controller, in [0, 3].
 * @param reserved This value is reserved for future use and should be set to zero.
 * @param keystroke Pointer to an XINPUT_KEYSTROKE structure that receives an input event.
 * @return ERROR_SUCCESS, ERROR_EMPTY or ERROR_DEVICE_NOT_CONNECTED.
 */
static DWORD WINAPI
XInputGetKeystroke_Stub
(
    DWORD                    user_index, 
    DWORD                      reserved, 
    struct _XINPUT_KEYSTROKE *keystroke
)
{
    PAL_UNUSED_ARG(user_index);
    PAL_UNUSED_ARG(reserved);
    PAL_UNUSED_ARG(keystroke);
    return ERROR_DEVICE_NOT_CONNECTED;
}

/* @summary Define a stub no-op implementation of the XInputGetKeystroke API.
 * @param user_index The index of the user's controller, in [0, 3].
 * @param flags Input flags that identify the controller type. Either 0 or XINPUT_FLAG_GAMEPAD.
 * @param capabilities Pointer to an XINPUT_CAPABILITIES structure that receives the controller capabilities.
 * @return ERROR_SUCCESS or ERROR_DEVICE_NOT_CONNECTED.
 */
static DWORD WINAPI
XInputGetCapabilities_Stub
(
    DWORD                          user_index, 
    DWORD                               flags, 
    struct _XINPUT_CAPABILITIES *capabilities
)
{
    PAL_UNUSED_ARG(user_index);
    PAL_UNUSED_ARG(flags);
    PAL_UNUSED_ARG(capabilities);
    return ERROR_DEVICE_NOT_CONNECTED;
}

/* @summary Define a stub no-op implementation of the XInputGetBatteryInformation API.
 * @param user_index The index of the user's controller, in [0, 3].
 * @param device_type Specifies which device associated with this user index should be queried. Must be BATTERY_DEVTYPE_GAMEPAD or BATTERY_DEVTYPE_HEADSET.
 * @param battery_info Pointer to an XINPUT_BATTERY_INFORMATION that receives battery information.
 * @return ERROR_SUCCESS or ERROR_DEVICE_NOT_CONNECTED.
 */
static DWORD WINAPI
XInputGetBatteryInformation_Stub
(
    DWORD                                 user_index, 
    BYTE                                 device_type, 
    struct _XINPUT_BATTERY_INFORMATION *battery_info
)
{
    PAL_UNUSED_ARG(user_index);
    PAL_UNUSED_ARG(device_type);
    PAL_UNUSED_ARG(battery_info);
    return ERROR_DEVICE_NOT_CONNECTED;
}

/* @summary Define a stub no-op implementation of the XInputGetAudioDeviceIds API.
 * @param user_index The index of the user's controller, in [0, 3].
 * @param render_device_id Windows Core Audio device ID string for render (speakers).
 * @param render_device_chars The size, in wide-chars, of the render device ID string buffer.
 * @param capture_device_id Windows Core Audio device ID string for capture (microphone).
 * @param capture_device_chars The size, in wide-chars, of the capture device ID string buffer.
 * @return ERROR_SUCCESS or ERROR_DEVICE_NOT_CONNECTED.
 */
static DWORD WINAPI
XInputGetAudioDeviceIds_Stub
(
    DWORD             user_index, 
    LPWSTR      render_device_id, 
    UINT    *render_device_chars, 
    LPWSTR     capture_device_id, 
    UINT   *capture_device_chars
)
{
    if (render_device_chars != NULL) {
       *render_device_chars = 0;
    }
    if (capture_device_chars != NULL) {
       *capture_device_chars = 0;
    }
    PAL_UNUSED_ARG(user_index);
    PAL_UNUSED_ARG(render_device_id);
    PAL_UNUSED_ARG(capture_device_id);
    return ERROR_DEVICE_NOT_CONNECTED;
}

/* @summary For a given RawInput keyboard packet, retrieve the virtual key identifier and the raw scan code.
 * @param vkey_code On return, this location is updated with the virtual key identifier.
 * @param scan_code On return, this location is updated with the raw scan code value.
 * @param key The RawInput keyboard packet to process.
 * @return Zero if the key data is part of an escaped sequence and should be ignored, or non-zero if the key data was retrieved.
 */
static int
PAL_GetVirtualKeyAndScanCode
(
    pal_uint32_t *vkey_code, 
    pal_uint32_t *scan_code,
    RAWKEYBOARD const  *key 
)
{
    pal_uint32_t vkey = key->VKey;
    pal_uint32_t scan = key->MakeCode;
    pal_uint32_t   e0 = key->Flags & RI_KEY_E0;

    if (vkey == 255) { /* discard fake keys; these are just part of an escaped sequence */
       *vkey_code = 0;
       *scan_code = 0;
        return 0;
    }
    if (vkey == VK_SHIFT) {   /* correct left/right shift */
        vkey  = MapVirtualKey(scan, MAPVK_VSC_TO_VK_EX);
    }
    if (vkey == VK_NUMLOCK) { /* correct PAUSE/BREAK and NUMLOCK. set the extended bit */
        scan  = MapVirtualKey(vkey, MAPVK_VK_TO_VSC) | 0x100;
    }
    if (key->Flags & RI_KEY_E1) {
        /* for escaped sequences, turn the virtual key into the correct scan code.
         * unfortunately, MapVirtualKey can't handle VK_PAUSE, so do that manually. */
        if (vkey != VK_PAUSE) scan = MapVirtualKey(vkey, MAPVK_VK_TO_VSC);
        else scan = 0x45;
    }
   /* map left/right versions of various keys */
    switch (vkey) {
        case VK_CONTROL:  /* left/right CTRL */
            vkey =  e0 ? VK_RCONTROL : VK_LCONTROL;
            break;
        case VK_MENU:     /* left/right ALT  */
            vkey =  e0 ? VK_RMENU : VK_LMENU;
            break;
        case VK_RETURN:
            vkey =  e0 ? VK_SEPARATOR : VK_RETURN;
            break;
        case VK_INSERT:
            vkey = !e0 ? VK_NUMPAD0 : VK_INSERT;
            break;
        case VK_DELETE:
            vkey = !e0 ? VK_DECIMAL : VK_DELETE;
            break;
        case VK_HOME:
            vkey = !e0 ? VK_NUMPAD7 : VK_HOME;
            break;
        case VK_END:
            vkey = !e0 ? VK_NUMPAD1 : VK_END;
            break;
        case VK_PRIOR:
            vkey = !e0 ? VK_NUMPAD9 : VK_PRIOR;
            break;
        case VK_NEXT:
            vkey = !e0 ? VK_NUMPAD3 : VK_NEXT;
            break;
        case VK_LEFT:
            vkey = !e0 ? VK_NUMPAD4 : VK_LEFT;
            break;
        case VK_RIGHT:
            vkey = !e0 ? VK_NUMPAD6 : VK_RIGHT;
            break;
        case VK_UP:
            vkey = !e0 ? VK_NUMPAD8 : VK_UP;
            break;
        case VK_DOWN:
            vkey = !e0 ? VK_NUMPAD2 : VK_DOWN;
            break;
        case VK_CLEAR:
            vkey = !e0 ? VK_NUMPAD5 : VK_CLEAR;
            break;
    }
   *vkey_code = vkey;
   *scan_code = scan;
    return 1;
}

/* @summary Retrieve the display name for a given virtual key code.
 * @param buffer The buffer to retrieve the nul-terminated key display name.
 * @param buffer_max_chars The maximum number of characters that can be written to buffer.
 * @param vkey_code The virtual key code to retrieve the display name for.
 * @return The length of the string written into the buffer, in characters, not including the nul.
 */
static pal_usize_t
PAL_CopyKeyDisplayName
(
    WCHAR                *buffer, 
    pal_usize_t buffer_max_chars, 
    pal_uint32_t       vkey_code
)
{
    pal_uint32_t scan_code = 0;
    pal_uint32_t        e0 = 0;

    if (vkey_code != VK_PAUSE) {
        if (vkey_code != VK_NUMLOCK) {   /* common case - map the virtual key code to the scan code */
            scan_code  = MapVirtualKey(vkey_code, MAPVK_VK_TO_VSC);
        } else { /* correct PAUSE/BREAK and NUMLOCK - set the extended bit */
            scan_code  = MapVirtualKey(vkey_code, MAPVK_VK_TO_VSC) | 0x100;
        }
    }
    else { /* correctly handle VK_PAUSE */
        scan_code = 0x45;
    }
    /* determine whether to set the extended-key flag */
    if (vkey_code == VK_RCONTROL || vkey_code == VK_RMENU  || vkey_code == VK_SEPARATOR || 
        vkey_code == VK_INSERT   || vkey_code == VK_DELETE || vkey_code == VK_HOME      || 
        vkey_code == VK_END      || vkey_code == VK_PRIOR  || vkey_code == VK_NEXT      || 
        vkey_code == VK_LEFT     || vkey_code == VK_RIGHT  || vkey_code == VK_UP        || 
        vkey_code == VK_DOWN     || vkey_code == VK_CLEAR) { /* set the extended key flag */
        e0 = 1;
    }
    return (pal_usize_t)GetKeyNameTextW((LONG)((scan_code << 16) | (e0 << 24)), buffer, (int) buffer_max_chars);
}

/* @summary Copy the state data and device count from one PAL_WSI_INPUT_DEVICE_LIST to another.
 * @param dst The destination PAL_WSI_INPUT_DEVICE_LIST.
 * @param src The source PAL_WSI_INPUT_DEVICE_LIST.
 * @param nbytes The size of the input device list state data, in bytes.
 */
static PAL_INLINE void
PAL_InputDeviceListCopy
(
    struct PAL_WSI_INPUT_DEVICE_LIST * PAL_RESTRICT dst, 
    struct PAL_WSI_INPUT_DEVICE_LIST * PAL_RESTRICT src, 
    pal_usize_t                                  nbytes
)
{
    dst->MaxDevices   = src->MaxDevices;
    dst->DeviceCount  = src->DeviceCount;
    PAL_CopyMemory(dst->StateBase, src->StateBase, nbytes);
}

/* @summary Locate a particular input device given its operating system identifier.
 * @param devices The input device list to search.
 * @param handle The operating system handle of the input device.
 * @param index On return, if the device is found, this location is updated with the zero-based index of the device.
 * @return Zero if the device is found, or non-zero if the device is not found.
 */
static int
PAL_InputDeviceListSearchByHandle
(
    struct PAL_WSI_INPUT_DEVICE_LIST *devices, 
    HANDLE                             handle, 
    pal_uint32_t                       *index
)
{
    pal_uint32_t i, n;
    HANDLE *devid = devices->DeviceHandle;
    for (i = 0, n = devices->DeviceCount; i < n; ++i) {
        if (devid[i] == handle) {
            PAL_Assign(index, i);
            return 0;
        }
    }
    return 1;
}

/* @summary Handle an input device being attached to the system. If the device was not previously known, add it to the device list.
 * @param devices The PAL_WSI_INPUT_DEVICE_LIST to search and possibly update.
 * @param handle The operating system identifer of the device that was attached.
 * @param default_state Pointer to a structure initialized with the default device state.
 * @param state_size The size of the device state record, in bytes.
 * @param device_index If the device was inserted into the list, or was already in the list, the device index is stored in this location on return.
 * @return Zero if the device attachment was handled, or non-zero if the device attachment could not be handled because the device list is full.
 */
static int
PAL_InputDeviceListHandleAttach
(
    struct PAL_WSI_INPUT_DEVICE_LIST *devices, 
    HANDLE                             handle, 
    void                       *default_state, 
    pal_usize_t                    state_size, 
    pal_uint32_t                *device_index
)
{
    pal_uint32_t i, n;
    HANDLE *devid = devices->DeviceHandle;
    for (i = 0, n = devices->DeviceCount; i < n; ++i) {
        if (devid[i] == handle) {
            PAL_Assign(device_index, i);
            return 0;
        }
    }
    /* add the device to the list if possible */
    if (n != devices->DeviceCount) {
        pal_uint8_t   *state = PAL_InputDeviceListState(devices, n, state_size);
        PAL_CopyMemory(state , default_state, state_size);
        devices->DeviceHandle[n] = handle;
        devices->DeviceCount = n + 1;
        PAL_Assign(device_index, n);
        return 0;
    }
    return -1;
}

/* @summary Handle an input device being removed from the system. If the device is known, it is removed from the device list.
 * @param devices The PAL_WSI_INPUT_DEVICE_LIST to query and possibly update.
 * @param handle The operating system identifier of the device that was removed.
 * @param state_size The size of the device state data, in bytes.
 */
static void
PAL_InputDeviceListHandleRemove
(
    struct PAL_WSI_INPUT_DEVICE_LIST *devices, 
    HANDLE                             handle, 
    pal_usize_t                    state_size
)
{
    pal_uint32_t i, n;
    HANDLE *devid = devices->DeviceHandle;
    for (i = 0, n = devices->DeviceCount; i < n; ++i) {
        if (devid[i] == handle) {
            if (i != (n-1)) {
                /* swap the last item into the slot occupied by item i */
                pal_uint8_t   *dst = PAL_InputDeviceListState(devices, i  , state_size);
                pal_uint8_t   *src = PAL_InputDeviceListState(devices, n-1, state_size);
                devices->DeviceHandle[i] = devices->DeviceHandle[n-1];
                PAL_CopyMemory(dst, src, state_size);
            } 
            devices->DeviceCount--;
            return;
        }
    }
}

/* @summary Initialize an input device set given two device list snapshots.
 * @param set The device set to populate.
 * @param prev The device list from the older state snapshot.
 * @param curr The device list from the newer state snapshot.
 */
static void
PAL_InputDeviceSetInit
(
    struct PAL_WSI_INPUT_DEVICE_SET   *set, 
    struct PAL_WSI_INPUT_DEVICE_LIST *prev, 
    struct PAL_WSI_INPUT_DEVICE_LIST *curr
)
{
    pal_uint32_t i, j, n;

    /* the set always has a fixed capacity */
    set->MaxDevices = PAL_CountOf(set->DeviceIds);

    /* initialize the set based on the older snapshot */
    for (i = 0, n = prev->DeviceCount; i < n; ++i) {
        set->DeviceIds [i] = prev->DeviceHandle[i];
        set->Membership[i] = PAL_WSI_DEVICE_SET_MEMBERSHIP_PREV;
        set->PrevIndex [i] =(pal_uint8_t) i;
        set->CurrIndex [i] =(pal_uint8_t) 0xFF;
        set->DeviceCount++;
    }
    /* initialize any unused entries */
    for (i = n, n = set->MaxDevices; i < n; ++i) {
        set->DeviceIds [i] = INVALID_HANDLE_VALUE;
        set->Membership[i] = PAL_WSI_DEVICE_SET_MEMBERSHIP_NONE;
        set->PrevIndex [i] =(pal_uint8_t) 0xFF;
        set->CurrIndex [i] =(pal_uint8_t) 0xFF;
    }
    /* update the state based on the newer snapshot */
    for (i = 0, n = curr->DeviceCount; i < n; ++i) {
        HANDLE id = curr->DeviceHandle[i];
        pal_uint32_t ix = set->DeviceCount;
        pal_uint32_t in = 1;
        for (j = 0;  j < ix; ++j) {
            if (set->DeviceIds[j] == id) {
                ix = j; /* update existing entry */
                in = 0; /* don't increment device count */
                break;
            }
        }
        set->DeviceIds [ix]  = id;
        set->Membership[ix] |= PAL_WSI_DEVICE_SET_MEMBERSHIP_CURR;
        set->CurrIndex [ix]  =(pal_uint8_t) i;
        set->DeviceCount    += in;
    }
}

/* @summary Apply scaled radial deadzone logic to an analog stick input.
 * @param stick_xymn A four-element array that will store the normalized x- and y-components of the input direction, the magnitude, and the normalized magnitude.
 * @param stick_x The x-axis component of the analog input.
 * @param stick_y The y-axis component of the analog input.
 * @param deadzone The deadzone size as a percentage of total input range (in [0, 1]).
 */
static void
PAL_ApplyInputScaledRadialDeadzone
(
    pal_float32_t  *stick_xymn,
    pal_sint16_t       stick_x, 
    pal_sint16_t       stick_y, 
    pal_float32_t     deadzone
)
{
    pal_float32_t  x = stick_x;
    pal_float32_t  y = stick_y;
    pal_float32_t  m =(pal_float32_t) sqrt(x * x + y * y);
    pal_float32_t nx = x / m;
    pal_float32_t ny = y / m;
    pal_float32_t  n;

    if (m < deadzone) {  /* drop the input; it falls within the deadzone */
        stick_xymn[0] = 0;
        stick_xymn[1] = 0;
        stick_xymn[2] = 0;
        stick_xymn[3] = 0;
    } else {  /* rescale the input into the non-dead space */
        n = (m - deadzone) / (1.0f - deadzone);
        stick_xymn[0] = nx * n;
        stick_xymn[1] = ny * n;
        stick_xymn[2] = m;
        stick_xymn[3] = n;
    }
}

/* @summary Process an XInput gamepad state packet to update the state of a gamepad device.
 * @param devices The PAL_WSI_INPUT_DEVICE_LIST to query and update.
 * @param port_index The zero-based index of the port to which the gamepad is attached.
 * @param input_packet The XInput gamepad state snapshot.
 * @param device_index On return, this location is updated with the zero-based index of the device in the device list.
 * @return Zero if the input packet is successfully processed, or non-zero if an error occurred.
 */
static int
PAL_ProcessGamepadInputPacket
(
    struct PAL_WSI_INPUT_DEVICE_LIST *devices, 
    DWORD                          port_index, 
    struct _XINPUT_STATE const  *input_packet, 
    pal_uint32_t                *device_index
)
{
    PAL_WSI_GAMEPAD_STATE   *state = NULL;
    XINPUT_GAMEPAD const  *gamepad =&input_packet->Gamepad;
    pal_uintptr_t         port_ptr =(uintptr_t) port_index;
    HANDLE                  device =(HANDLE   ) port_ptr;
    pal_uint32_t             index = 0;
    pal_float32_t       deadzone_l = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE  / 32767.0f;
    pal_float32_t       deadzone_r = XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE / 32767.0f;

    if (PAL_InputDeviceListSearchByHandle(devices, device, &index) == 0) { /* already-known device */
        state = PAL_GamepadDeviceState(devices, index);
    } else if (devices->DeviceCount != devices->MaxDevices) { /* this is a newly-seen device - attach it */
        index = devices->DeviceCount;
        state = PAL_GamepadDeviceState(devices, index);
        PAL_ZeroMemory(state, sizeof(PAL_WSI_GAMEPAD_STATE));
        devices->DeviceHandle[index] = device;
        devices->DeviceCount++;
    } else { /* too many gamepad devices attached */
        return -1;
    }

    /* update the input device state and apply deadzone logic */
    state->LTrigger = gamepad->bLeftTrigger  > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? gamepad->bLeftTrigger  : 0;
    state->RTrigger = gamepad->bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? gamepad->bRightTrigger : 0;
    state->Buttons  = gamepad->wButtons;
    PAL_ApplyInputScaledRadialDeadzone(state->LStick, gamepad->sThumbLX, gamepad->sThumbLY, deadzone_l);
    PAL_ApplyInputScaledRadialDeadzone(state->RStick, gamepad->sThumbRX, gamepad->sThumbRY, deadzone_r);
    PAL_Assign(device_index, index);
    return 0;
}

/* @summary Process a RawInput mouse packet to update the state of a pointer device.
 * @param devices The PAL_WSI_INPUT_DEVICE_LIST to query and update.
 * @param input_packet The RawInput mouse packet.
 * @param device_index On return, this location is updated with the zero-based index of the device in the device list.
 * @return Zero if the input packet is successfully processed, or non-zero if an error occurred.
 */
static int
PAL_ProcessMouseInputPacket
(
    struct PAL_WSI_INPUT_DEVICE_LIST *devices, 
    RAWINPUT const              *input_packet, 
    pal_uint32_t                *device_index
)
{
    RAWINPUTHEADER   const *header = &input_packet->header;
    RAWMOUSE         const  *mouse = &input_packet->data.mouse;
    PAL_WSI_POINTER_STATE   *state = NULL;
    pal_uint32_t             index = 0;
    POINT                   cursor;
    USHORT            button_flags;

    if (PAL_InputDeviceListSearchByHandle(devices, header->hDevice, &index) == 0) { /* already known */
        state = PAL_PointerDeviceState(devices, index);
    } else if (devices->DeviceCount != devices->MaxDevices) { /* newly attached */
        index = devices->DeviceCount;
        state = PAL_PointerDeviceState(devices, index);
        PAL_ZeroMemory(state, sizeof(PAL_WSI_POINTER_STATE));
        devices->DeviceHandle[index] = header->hDevice;
        devices->DeviceCount++;
    } else { /* too many pointer devices */
        return -1;
    }

    /* retrieve the current mouse cursor position, in pixels */
    GetCursorPos(&cursor);
    state->Pointer[0] = cursor.x;
    state->Pointer[1] = cursor.y;

    /* set the high-resolution device position values */
    if (mouse->usFlags & MOUSE_MOVE_ABSOLUTE)
    {   /* the device is a pen, touchscreen, etc. and specifies only absolute coordinates */
        state->Relative[0]  = mouse->lLastX;
        state->Relative[1]  = mouse->lLastY;
        state->PointerFlags = PAL_WSI_POINTER_FLAG_ABSOLUTE;
    }
    else
    {   /* the device has specified relative coordinates */
        state->Relative[0] += mouse->lLastX;
        state->Relative[1] += mouse->lLastY;
        state->PointerFlags = PAL_WSI_POINTER_FLAGS_NONE;
    }

    button_flags = mouse->usButtonFlags;
    if (button_flags & RI_MOUSE_WHEEL)
    {   /* mouse wheel data was supplied with the input packet */
        state->Relative[2] = (pal_sint16_t) mouse->usButtonData;
    }
    else
    {   /* no mouse wheel data was supplied */
        state->Relative[2] = (pal_sint16_t) 0;
    }
    /* rebuild the button state vector. Raw Input supports up to 5 buttons. */
    if (button_flags & RI_MOUSE_BUTTON_1_DOWN) state->Buttons |=  MK_LBUTTON;
    if (button_flags & RI_MOUSE_BUTTON_1_UP  ) state->Buttons &= ~MK_LBUTTON;
    if (button_flags & RI_MOUSE_BUTTON_2_DOWN) state->Buttons |=  MK_RBUTTON;
    if (button_flags & RI_MOUSE_BUTTON_2_UP  ) state->Buttons &= ~MK_RBUTTON;
    if (button_flags & RI_MOUSE_BUTTON_3_DOWN) state->Buttons |=  MK_MBUTTON;
    if (button_flags & RI_MOUSE_BUTTON_3_UP  ) state->Buttons &= ~MK_MBUTTON;
    if (button_flags & RI_MOUSE_BUTTON_4_DOWN) state->Buttons |=  MK_XBUTTON1;
    if (button_flags & RI_MOUSE_BUTTON_4_UP  ) state->Buttons &= ~MK_XBUTTON1;
    if (button_flags & RI_MOUSE_BUTTON_5_DOWN) state->Buttons |=  MK_XBUTTON2;
    if (button_flags & RI_MOUSE_BUTTON_5_UP  ) state->Buttons &= ~MK_XBUTTON2;
    PAL_Assign(device_index, index);
    return 0;
}

/* @summary Process a RawInput keyboard packet to update the state of a keyboard device.
 * @param devices The PAL_WSI_INPUT_DEVICE_LIST to query and update.
 * @param input_packet The RawInput keyboard packet.
 * @param device_index On return, this location is updated with the zero-based index of the device in the device list.
 * @return Zero if the input packet is successfully processed, or non-zero if an error occurred.
 */
static int
PAL_ProcessKeyboardInputPacket
(
    struct PAL_WSI_INPUT_DEVICE_LIST *devices, 
    RAWINPUT const              *input_packet, 
    pal_uint32_t                *device_index
)
{
    RAWINPUTHEADER   const *header = &input_packet->header;
    RAWKEYBOARD      const    *key = &input_packet->data.keyboard;
    PAL_WSI_KEYBOARD_STATE  *state = NULL;
    pal_uint32_t             index = 0;
    pal_uint32_t         vkey_code = 0;
    pal_uint32_t         scan_code = 0;

    if (PAL_InputDeviceListSearchByHandle(devices, header->hDevice, &index) == 0) { /* already known */
        state = PAL_KeyboardDeviceState(devices, index);
    } else if (devices->DeviceCount != devices->MaxDevices) { /* newly attached */
        index = devices->DeviceCount;
        state = PAL_KeyboardDeviceState(devices, index);
        PAL_ZeroMemory(state, sizeof(PAL_WSI_KEYBOARD_STATE));
        devices->DeviceHandle[index] = header->hDevice;
        devices->DeviceCount++;
    } else { /* too many keyboard devices attached */
        return -1;
    }

    if (!PAL_GetVirtualKeyAndScanCode(&vkey_code, &scan_code, key))
    {   /* discard fake keys; these are just part of an escaped sequence */
        PAL_Assign(device_index, index);
        return 0;
    }
    if ((key->Flags & RI_KEY_BREAK) == 0) {
        /* the key is currently pressed; set the bit corresponding to the virtual key code */
        state->KeyState[vkey_code >> 5] |= (1UL << (vkey_code & 0x1F));
    } else { /* the key was just released; clear the bit corresponding to the virtual key code */
        state->KeyState[vkey_code >> 5] &=~(1UL << (vkey_code & 0x1F));
    }
    PAL_Assign(device_index, index);
    return 0;
}

/* @summary Poll all XInput gamepads currently attached to the system and update the input device state.
 * @param devices The PAL_WSI_INPUT_DEVICE_LIST to query and possibly update.
 * @param ports_out On return, this value has one bit set for each attached gamepad.
 * @param ports_inp A bitvector specifying the gamepad ports to poll, or CORE_INPUT_ALL_GAMEPAD_PORTS to poll all ports.
 * @param disp The XInput dispatch table.
 * @return The number of gamepad devices attached to the system.
 */
static pal_uint32_t
PAL_PollXInputGamepads
(
    struct PAL_WSI_INPUT_DEVICE_LIST *devices,
    pal_uint32_t                   *ports_out,
    pal_uint32_t                    ports_inp, 
    struct PAL_WSI_XINPUT_DISPATCH    *xinput 
)
{
    XINPUT_STATE   state;
    DWORD         result = ERROR_SUCCESS;
    pal_uint32_t outbits = 0;
    pal_uint32_t   count = 0;
    pal_uint32_t       i;
    for (i = 0; i < XUSER_MAX_COUNT; ++i) {
        if (ports_inp & (1UL << i)) {
            if ((result  = xinput->XInputGetState(i, &state)) == ERROR_SUCCESS) {
                PAL_ProcessGamepadInputPacket(devices, i, &state, NULL);
                outbits |= (1UL << i);
                count++;
            }
        }
    }
   *ports_out = outbits;
    return count;
}

#if 0
/* @summary Given two gamepad state snapshots, generate events for buttons down, pressed and released.
 * @param events The gamepad events structure to populate.
 * @param prev The state snapshot for the device from the previous update.
 * @param curr The state snapshot for the device from the in-progress update.
 */
static void
PAL_GenerateGamepadInputEvents
(
    CORE_INPUT_GAMEPAD_EVENTS     *events, 
    CORE__INPUT_GAMEPAD_STATE const *prev, 
    CORE__INPUT_GAMEPAD_STATE const *curr
)
{
    uint32_t   max_events = events->MaxButtonEvents;
    uint32_t   curr_state = curr->Buttons;
    uint32_t   prev_state = prev->Buttons;
    uint32_t      changes =(curr_state ^ prev_state);
    uint32_t        downs =(changes    & curr_state);
    uint32_t          ups =(changes    &~curr_state);
    uint32_t        num_d = 0;
    uint32_t        num_p = 0;
    uint32_t        num_r = 0;
    uint32_t         mask;
    uint32_t       button;
    uint32_t         i, j;

    events->LeftTrigger         = (float) curr->LTrigger / (float) (255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
    events->RightTrigger        = (float) curr->RTrigger / (float) (255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
    events->LeftStick[0]        =  curr->LStick[0];
    events->LeftStick[1]        =  curr->LStick[1];
    events->LeftStickMagnitude  =  curr->LStick[3];
    events->RightStick[0]       =  curr->RStick[0];
    events->RightStick[1]       =  curr->RStick[1];
    events->RightStickMagnitude =  curr->RStick[3];
    for (i = 0; i < 32; ++i)
    {
        mask    = 1UL << i;
        button  = mask; /* XINPUT_GAMEPAD_x */

        if (num_d < max_events && (curr_state & mask) != 0)
        {   /* this button is currently pressed */
            events->ButtonsDown[num_d++] = (uint16_t) button;
        }
        if (num_p < max_events && (downs & mask) != 0)
        {   /* this button was just pressed */
            events->ButtonsPressed[num_p++] = (uint16_t) button;
        }
        if (num_r < max_events && (ups & mask) != 0)
        {   /* this button was just released */
            events->ButtonsReleased[num_r++] = (uint16_t) button;
        }
    }
    events->ButtonDownCount     = num_d;
    events->ButtonPressedCount  = num_p;
    events->ButtonReleasedCount = num_r;
}

/* @summary Build a device set and generate events related to device attachment and removal.
 * @param events The CORE_INPUT_EVENTS to update.
 * @param device_list_prev The gamepad device list from the previous update.
 * @param device_list_curr The gamepad device list from the current update.
 */
static void
CORE__GenerateGamepadDeviceEvents
(
    CORE_INPUT_EVENTS                 *events, 
    CORE__INPUT_DEVICE_LIST *device_list_prev, 
    CORE__INPUT_DEVICE_LIST *device_list_curr
)
{
    uint32_t                             i, n;
    CORE__INPUT_DEVICE_SET         device_set;
    CORE__DetermineInputDeviceSet(&device_set, device_list_prev, device_list_curr);
    events->GamepadAttachCount = 0;
    events->GamepadRemoveCount = 0;
    events->GamepadDeviceCount = 0;
    for (i = 0, n = device_set.DeviceCount; i < n; ++i)
    {
        switch (device_set.Membership[i])
        {
        case CORE__INPUT_DEVICE_SET_MEMBERSHIP_NONE:
            { /* skip */
            } break;
        case CORE__INPUT_DEVICE_SET_MEMBERSHIP_PREV:
            { /* the input device was just removed */
              events->GamepadRemoveList[events->GamepadRemoveCount] = (DWORD)((DWORD_PTR) device_set.DeviceIds[i]);
              events->GamepadRemoveCount++;
            } break;
        case CORE__INPUT_DEVICE_SET_MEMBERSHIP_CURR:
            { /* the input device was just attached */
              events->GamepadAttachList[events->GamepadAttachCount] = (DWORD)((DWORD_PTR) device_set.DeviceIds[i]);
              events->GamepadAttachCount++;
            } break;
        default:
            { /* the input device was present in both snapshots */
              CORE_INPUT_GAMEPAD_EVENTS *input_ev = &events->GamepadDeviceEvents[events->GamepadDeviceCount];
              CORE__INPUT_GAMEPAD_STATE *state_pp = CORE__GamepadDeviceListState(device_list_prev, device_set.PrevIndex[i]);
              CORE__INPUT_GAMEPAD_STATE *state_cp = CORE__GamepadDeviceListState(device_list_curr, device_set.CurrIndex[i]);
              events->GamepadDeviceIds[events->GamepadDeviceCount] = (DWORD)((DWORD_PTR) device_set.DeviceIds[i]);
              CORE__GenerateGamepadInputEvents(input_ev, state_pp, state_cp);
              events->GamepadDeviceCount++;
            } break;
        }
    }
}

/* @summary Given two pointer state snapshots, generate events for keys down, pressed and released.
 * @param events The keyboard events structure to populate.
 * @param prev The state snapshot for the device from the previous update.
 * @param curr The state snapshot for the device from the in-progress update.
 */
static void
CORE__GeneratePointerInputEvents
(
    CORE_INPUT_POINTER_EVENTS     *events, 
    CORE__INPUT_POINTER_STATE const *prev, 
    CORE__INPUT_POINTER_STATE const *curr
)
{
    uint32_t   max_events = events->MaxButtonEvents;
    uint32_t   curr_state = curr->Buttons;
    uint32_t   prev_state = prev->Buttons;
    uint32_t      changes =(curr_state ^ prev_state);
    uint32_t        downs =(changes    & curr_state);
    uint32_t          ups =(changes    &~curr_state);
    uint32_t        num_d = 0;
    uint32_t        num_p = 0;
    uint32_t        num_r = 0;
    uint32_t         mask;
    uint32_t       button;
    uint32_t         i, j;

    events->Cursor[0]  = curr->Pointer [0];
    events->Cursor[1]  = curr->Pointer [1];
    events->WheelDelta = curr->Relative[2];
    if (curr->Flags & CORE__INPUT_POINTER_FLAG_ABSOLUTE)
    {   /* calculate relative values as the delta between states */
        events->Mickeys[0] = curr->Relative[0] - prev->Relative[0];
        events->Mickeys[1] = curr->Relative[1] - prev->Relative[1];
    }
    else
    {   /* the driver specified relative values - copy them as-is */
        events->Mickeys[0] = curr->Relative[0];
        events->Mickeys[1] = curr->Relative[1];
    }
    for (i = 0; i < 32; ++i)
    {
        mask    = 1UL << i;
        button  = mask; /* MK_nBUTTON */

        if (num_d < max_events && (curr_state & mask) != 0)
        {   /* this button is currently pressed */
            events->ButtonsDown[num_d++] = (uint16_t) button;
        }
        if (num_p < max_events && (downs & mask) != 0)
        {   /* this button was just pressed */
            events->ButtonsPressed[num_p++] = (uint16_t) button;
        }
        if (num_r < max_events && (ups & mask) != 0)
        {   /* this button was just released */
            events->ButtonsReleased[num_r++] = (uint16_t) button;
        }
    }
    events->ButtonDownCount     = num_d;
    events->ButtonPressedCount  = num_p;
    events->ButtonReleasedCount = num_r;
}

/* @summary Build a device set and generate events related to device attachment and removal.
 * @param events The CORE_INPUT_EVENTS to update.
 * @param device_list_prev The pointer device list from the previous update.
 * @param device_list_curr The pointer device list from the current update.
 */
static void
CORE__GeneratePointerDeviceEvents
(
    CORE_INPUT_EVENTS                 *events, 
    CORE__INPUT_DEVICE_LIST *device_list_prev, 
    CORE__INPUT_DEVICE_LIST *device_list_curr
)
{
    uint32_t                             i, n;
    CORE__INPUT_DEVICE_SET         device_set;
    CORE__DetermineInputDeviceSet(&device_set, device_list_prev, device_list_curr);
    events->PointerAttachCount = 0;
    events->PointerRemoveCount = 0;
    events->PointerDeviceCount = 0;
    for (i = 0, n = device_set.DeviceCount; i < n; ++i)
    {
        switch (device_set.Membership[i])
        {
        case CORE__INPUT_DEVICE_SET_MEMBERSHIP_NONE:
            { /* skip */
            } break;
        case CORE__INPUT_DEVICE_SET_MEMBERSHIP_PREV:
            { /* the input device was just removed */
              events->PointerRemoveList[events->PointerRemoveCount] = device_set.DeviceIds[i];
              events->PointerRemoveCount++;
            } break;
        case CORE__INPUT_DEVICE_SET_MEMBERSHIP_CURR:
            { /* the input device was just attached */
              events->PointerAttachList[events->PointerAttachCount] = device_set.DeviceIds[i];
              events->PointerAttachCount++;
            } break;
        default:
            { /* the input device was present in both snapshots */
              CORE_INPUT_POINTER_EVENTS *input_ev = &events->PointerDeviceEvents[events->PointerDeviceCount];
              CORE__INPUT_POINTER_STATE *state_pp = CORE__PointerDeviceListState(device_list_prev, device_set.PrevIndex[i]);
              CORE__INPUT_POINTER_STATE *state_cp = CORE__PointerDeviceListState(device_list_curr, device_set.CurrIndex[i]);
              events->PointerDeviceIds[events->PointerDeviceCount] = device_set.DeviceIds[i];
              CORE__GeneratePointerInputEvents(input_ev, state_pp, state_cp);
              events->PointerDeviceCount++;
            } break;
        }
    }
}

/* @summary Copy pointer device information from the prior write buffer to the current write buffer.
 * @param dst The CORE__INPUT_DEVICE_LIST representing the new write buffer.
 * @param src The CORE__INPUT_DEVICE_LIST representing the previous write buffer.
 */
static void
CORE__ForwardPointerDeviceList
(
    CORE__INPUT_DEVICE_LIST *dst, 
    CORE__INPUT_DEVICE_LIST *src
)
{
    uint32_t i, n;

    if (dst != NULL && dst != src)
    {
        dst->DeviceCount = src->DeviceCount;
        CopyMemory(dst->DeviceHandle, src->DeviceHandle, src->DeviceCount * sizeof(HANDLE));
        CopyMemory(dst->DeviceState , src->DeviceState , src->DeviceCount * sizeof(CORE__INPUT_POINTER_STATE));
        for (i = 0, n = src->DeviceCount; i < n; ++i)
        {
            CORE__INPUT_POINTER_STATE *dstate = CORE__PointerDeviceListState(dst, i);
            dstate->Flags = CORE__INPUT_POINTER_FLAGS_NONE;
            dstate->Relative[0] = 0;
            dstate->Relative[1] = 0;
        }
    }
}

/* @summary Given two keyboard state snapshots, generate events for keys down, pressed and released.
 * @param events The keyboard events structure to populate.
 * @param prev The state snapshot for the device from the previous update.
 * @param curr The state snapshot for the device from the in-progress update.
 */
static void
CORE__GenerateKeyboardInputEvents
(
    CORE_INPUT_KEYBOARD_EVENTS     *events, 
    CORE__INPUT_KEYBOARD_STATE const *prev, 
    CORE__INPUT_KEYBOARD_STATE const *curr
)
{
    uint32_t   max_events = events->MaxKeyEvents;
    uint32_t        num_d = 0;
    uint32_t        num_p = 0;
    uint32_t        num_r = 0;
    uint32_t   curr_state;
    uint32_t   prev_state;
    uint32_t      changes;
    uint32_t        downs;
    uint32_t          ups;
    uint32_t         mask;
    uint32_t         vkey;
    uint32_t         i, j;

    for (i = 0; i < 8; ++i)
    {
        curr_state = curr->KeyState[i];
        prev_state = prev->KeyState[i];
        changes    =(curr_state ^ prev_state);
        downs      =(changes    & curr_state);
        ups        =(changes    &~curr_state);
        for (j = 0; j < 32; ++j)
        {
            mask = 1UL << j;
            vkey =  (i << 5) + j;

            if (num_d < max_events && (curr_state & mask) != 0)
            {   /* this key is currently pressed */
                events->KeysDown[num_d++] = (uint8_t) vkey;
            }
            if (num_p < max_events && (downs & mask) != 0)
            {   /* this key was just pressed */
                events->KeysPressed[num_p++] = (uint8_t) vkey;
            }
            if (num_r < max_events && (ups & mask) != 0)
            {   /* this key was just released */
                events->KeysReleased[num_r++] = (uint8_t) vkey;
            }
        }
    }
    events->KeyDownCount     = num_d;
    events->KeyPressedCount  = num_p;
    events->KeyReleasedCount = num_r;
}

/* @summary Build a device set and generate events related to device attachment and removal.
 * @param events The CORE_INPUT_EVENTS to update.
 * @param device_list_prev The keyboard device list from the previous update.
 * @param device_list_curr The keyboard device list from the current update.
 */
static void
CORE__GenerateKeyboardDeviceEvents
(
    CORE_INPUT_EVENTS                 *events, 
    CORE__INPUT_DEVICE_LIST *device_list_prev, 
    CORE__INPUT_DEVICE_LIST *device_list_curr
)
{
    uint32_t                             i, n;
    CORE__INPUT_DEVICE_SET         device_set;
    CORE__DetermineInputDeviceSet(&device_set, device_list_prev, device_list_curr);
    events->KeyboardAttachCount  = 0;
    events->KeyboardRemoveCount  = 0;
    events->KeyboardDeviceCount  = 0;
    for (i = 0, n = device_set.DeviceCount; i < n; ++i)
    {
        switch (device_set.Membership[i])
        {
        case CORE__INPUT_DEVICE_SET_MEMBERSHIP_NONE:
            { /* skip */
            } break;
        case CORE__INPUT_DEVICE_SET_MEMBERSHIP_PREV:
            { /* the input device was just removed */
              events->KeyboardRemoveList[events->KeyboardRemoveCount] = device_set.DeviceIds[i];
              events->KeyboardRemoveCount++;
            } break;
        case CORE__INPUT_DEVICE_SET_MEMBERSHIP_CURR:
            { /* the input device was just attached */
              events->KeyboardAttachList[events->KeyboardAttachCount] = device_set.DeviceIds[i];
              events->KeyboardAttachCount++;
            } break;
        default:
            { /* the input device was present in both snapshots */
              CORE_INPUT_KEYBOARD_EVENTS *input_ev = &events->KeyboardDeviceEvents[events->KeyboardDeviceCount];
              CORE__INPUT_KEYBOARD_STATE *state_pp = CORE__KeyboardDeviceListState(device_list_prev, device_set.PrevIndex[i]);
              CORE__INPUT_KEYBOARD_STATE *state_cp = CORE__KeyboardDeviceListState(device_list_curr, device_set.CurrIndex[i]);
              events->KeyboardDeviceIds[events->KeyboardDeviceCount] = device_set.DeviceIds[i];
              CORE__GenerateKeyboardInputEvents(input_ev, state_pp, state_cp);
              events->KeyboardDeviceCount++;
            } break;
        }
    }
}

/* @summary Copy keyboard device information from the prior write buffer to the current write buffer.
 * @param dst The CORE__INPUT_DEVICE_LIST representing the new write buffer.
 * @param src The CORE__INPUT_DEVICE_LIST representing the previous write buffer.
 */
static void
CORE__ForwardKeyboardDeviceList
(
    CORE__INPUT_DEVICE_LIST *dst, 
    CORE__INPUT_DEVICE_LIST *src
)
{
    if (dst != NULL && dst != src)
    {
        dst->DeviceCount = src->DeviceCount;
        CopyMemory(dst->DeviceHandle, src->DeviceHandle, src->DeviceCount * sizeof(HANDLE));
        CopyMemory(dst->DeviceState , src->DeviceState , src->DeviceCount * sizeof(CORE__INPUT_KEYBOARD_STATE));
    }
}
#endif

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
        wsi->Win32Runtime.GetDpiForMonitor(info->MonitorHandle, MDT_EFFECTIVE_DPI, &info->DisplayDpiX, &info->DisplayDpiY);
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

/* @summary Retrieve the position and size of the monitor containing a window.
 * @param data The PAL_WINDOW_DATA to update.
 * @param monitor The handle of the monitor containing the window.
 */
static void
PAL_WindowDataUpdateDisplayGeometry
(
    struct PAL_WINDOW_DATA *data, 
    HMONITOR             monitor
)
{
    PAL_WINDOW_SYSTEM *wsi = data->WindowSystem;
    PAL_DISPLAY_DATA *disp = wsi->ActiveDisplays;
    pal_uint32_t      i, n;

    for (i = 0, n = wsi->DisplayCount; i < n; ++i) {
        if (disp[i].MonitorHandle == monitor) {
            data->DisplayHandle    = monitor;
            data->DisplayPositionX = disp[i].DisplayMode.dmPosition.x;
            data->DisplayPositionY = disp[i].DisplayMode.dmPosition.y;
            data->DisplaySizeX     = disp[i].DisplayMode.dmPelsWidth;
            data->DisplaySizeY     = disp[i].DisplayMode.dmPelsHeight;
            return;
        }
    }
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
    state->StatusFlags         = data->StatusFlags;
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
                    data->EventFlags |= PAL_WINDOW_EVENT_FLAG_DESTROYED;
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

/* @summary Perform processing for the WM_DISPLAYCHANGE message. This message is sent to top-level windows when a display mode changes.
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

/* @summary Perform processing for the WM_CREATE message. This resizes the window so that its client area has the desired dimensions, accounting for DPI and window chrome.
 * @param data The PAL_WINDOW_DATA associated with the window that received the message.
 * @param hwnd The HWND of the window that received the message.
 * @param wparam The WPARAM value associated with the message.
 * @param lparam The LPARAM value associated with the message.
 * @return A message-specific value indicating that the message was processed.
 */
static LRESULT
PAL_WndProc_WSI_WM_CREATE
(
    struct PAL_WINDOW_DATA *data, 
    HWND                    hwnd, 
    WPARAM                wparam, 
    LPARAM                lparam
)
{
    PAL_WINDOW_SYSTEM *wsi = data->WindowSystem;
    HMONITOR       monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    DWORD            style =(DWORD)GetWindowLong(hwnd, GWL_STYLE);
    UINT             dpi_x = 0;
    UINT             dpi_y = 0;
    RECT                rc;

    wsi->Win32Runtime.GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y);

    if (data->DisplayHandle != monitor)
    {   /* save the update monitor attributes */
        data->EventFlags |= PAL_WINDOW_EVENT_FLAG_MONITOR_CHANGED;
        PAL_WindowDataUpdateDisplayGeometry(data, monitor);
    }

    /* save the DPI of the monitor on which the window was created */
    if (data->DisplayDpiX != dpi_x || data->DisplayDpiY != dpi_y)
    {   /* the DPI has changed */
        data->EventFlags |= PAL_WINDOW_EVENT_FLAG_DPI_CHANGED;
        data->DisplayDpiX = dpi_x;
        data->DisplayDpiY = dpi_y;
    }

    if (style != WS_POPUP)
    {   /* resize the window to account for any chrome and borders.
         * at the same time, convert from logical to physical pixels.
         */
        rc.left   = 0; rc.top = 0;
        rc.right  = PAL_LogicalToPhysicalPixels(data->RestoreSizeX, dpi_x);
        rc.bottom = PAL_LogicalToPhysicalPixels(data->RestoreSizeY, dpi_y);
        AdjustWindowRectEx(&rc , style, FALSE , GetWindowLong(hwnd, GWL_EXSTYLE));
        SetWindowPos(hwnd, NULL, 0,  0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOZORDER);
        /* this is a windowed window style */
        data->StatusFlags |= PAL_WINDOW_STATUS_FLAG_WINDOWED;
    }
    else
    {   /* this is a fullscreen window style */
        data->StatusFlags |= PAL_WINDOW_STATUS_FLAG_FULLSCREEN;
    }

    /* retrieve the current window geometry */
    PAL_WindowDataUpdateWindowGeometry(data, hwnd, dpi_x, dpi_y);

    /* the window was created, positioned and sized */
    data->EventFlags |= PAL_WINDOW_EVENT_FLAG_CREATED;
    data->EventFlags |= PAL_WINDOW_EVENT_FLAG_MONITOR_CHANGED;
    data->EventFlags |= PAL_WINDOW_EVENT_FLAG_POSITION_CHANGED;
    data->EventFlags |= PAL_WINDOW_EVENT_FLAG_SIZE_CHANGED;

    PAL_UNUSED_ARG(wparam);
    PAL_UNUSED_ARG(lparam);
    return 0;
}

/* @summary Perform processing for the WM_ACTIVATE message. This message is sent when the window becomes active or inactive.
 * @param data The PAL_WINDOW_DATA associated with the window that received the message.
 * @param hwnd The HWND of the window that received the message.
 * @param wparam The WPARAM value associated with the message.
 * @param lparam The LPARAM value associated with the message.
 * @return A message-specific value indicating that the message was processed.
 */
static LRESULT
PAL_WndProc_WSI_WM_ACTIVATE
(
    struct PAL_WINDOW_DATA *data, 
    HWND                    hwnd, 
    WPARAM                wparam,
    LPARAM                lparam
)
{
    int    active = LOWORD(wparam);
    int minimized = HIWORD(wparam);

    /* if active is non-zero, hwnd_active is the handle of the window being deactivated.
     * if active is zero, hwnd_active is the handle of the window being activated.
     */
    if (active == 0 && minimized)
    {   /* this window has just been de-activated and minimized */
        data->EventFlags |= PAL_WINDOW_EVENT_FLAG_DEACTIVATED;
        data->EventFlags |= PAL_WINDOW_EVENT_FLAG_HIDDEN;
    }
    if (active && minimized)
    {   /* this window has just been activated */
        data->EventFlags |= PAL_WINDOW_EVENT_FLAG_ACTIVATED;
        data->EventFlags |= PAL_WINDOW_EVENT_FLAG_SHOWN;
    }
    return DefWindowProc(hwnd, WM_ACTIVATE, wparam, lparam);
}

/* @summary Perform processing for the WM_DPICHANGED message. This message is sent when a window is moved to a display with a different DPI setting.
 * The window is resized to account for the DPI change and any window chrome.
 * @param data The PAL_WINDOW_DATA associated with the window that received the message.
 * @param hwnd The HWND of the window that received the message.
 * @param wparam The WPARAM value associated with the message.
 * @param lparam The LPARAM value associated with the message.
 * @return A message-specific value indicating that the message was processed.
 */
static LRESULT
PAL_WndProc_WSI_WM_DPICHANGED
(
    struct PAL_WINDOW_DATA *data, 
    HWND                    hwnd, 
    WPARAM                wparam,
    LPARAM                lparam
)
{
    HMONITOR   monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    pal_uint32_t flags = PAL_WINDOW_EVENT_FLAGS_NONE;
    UINT         dpi_x = LOWORD(wparam);
    UINT         dpi_y = HIWORD(wparam);
    DWORD        style =(DWORD) GetWindowLong(hwnd, GWL_STYLE);
    RECT         *sugg =(RECT*)(lparam); /* suggested new position and size */
    RECT            rc;

    if (data->DisplayHandle != monitor)
    {   /* save the update monitor attributes */
        data->EventFlags |= PAL_WINDOW_EVENT_FLAG_MONITOR_CHANGED;
        PAL_WindowDataUpdateDisplayGeometry(data, monitor);
    }
    data->DisplayDpiX = dpi_x;
    data->DisplayDpiY = dpi_y;

    if (style != WS_POPUP)
    {   /* resize the window to account for any chrome and borders.
         * at the same time, convert from logical to physical pixels.
         * use the window position suggested by the operating system.
         */
        if (sugg->left != data->WindowPositionX || sugg->top != data->WindowPositionY) {
            flags |= PAL_WINDOW_EVENT_FLAG_POSITION_CHANGED;
        }
        rc.left    = sugg->left; 
        rc.top     = sugg->top;
        rc.right   = sugg->left + PAL_LogicalToPhysicalPixels(data->RestoreSizeX, dpi_x);
        rc.bottom  = sugg->top  + PAL_LogicalToPhysicalPixels(data->RestoreSizeY, dpi_y);
        AdjustWindowRectEx(&rc , style, FALSE, GetWindowLong(hwnd, GWL_EXSTYLE));
        SetWindowPos(hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOACTIVATE | SWP_NOZORDER);
    }

    /* retrieve the current window geometry */
    PAL_WindowDataUpdateWindowGeometry(data, hwnd, dpi_x, dpi_y);

    /* update the window event flags */
    data->EventFlags |= PAL_WINDOW_EVENT_FLAG_DPI_CHANGED;
    data->EventFlags |= PAL_WINDOW_EVENT_FLAG_SIZE_CHANGED;
    data->EventFlags |= flags;
    return 0;
}

/* @summary Perform processing for the WM_CLOSE message. This message is sent when the user closes a window by clicking or a keyboard chord, or when the CloseWindow API is called.
 * @param data The PAL_WINDOW_DATA associated with the window that received the message.
 * @param hwnd The HWND of the window that received the message.
 * @param wparam The WPARAM value associated with the message.
 * @param lparam The LPARAM value associated with the message.
 * @return A message-specific value indicating that the message was processed.
 */
static LRESULT
PAL_WndProc_WSI_WM_CLOSE
(
    struct PAL_WINDOW_DATA *data, 
    HWND                    hwnd, 
    WPARAM                wparam,
    LPARAM                lparam
)
{   /* mark the window as being closed */
    data->StatusFlags |= PAL_WINDOW_STATUS_FLAG_CLOSED;
    data->EventFlags  |= PAL_WINDOW_EVENT_FLAG_CLOSED;
    /* make the window disappear, but do not destroy it */
    ShowWindow(hwnd, SW_HIDE);
    PAL_UNUSED_ARG(wparam);
    PAL_UNUSED_ARG(lparam);
    return 0;
}

/* @summary Perform processing for the WM_DESTROY message. This message is sent when the window is actually destroyed by calling the DestroyWindow API.
 * @param data The PAL_WINDOW_DATA associated with the window that received the message.
 * @param hwnd The HWND of the window that received the message.
 * @param wparam The WPARAM value associated with the message.
 * @param lparam The LPARAM value associated with the message.
 * @return A message-specific value indicating that the message was processed.
 */
static LRESULT
PAL_WndProc_WSI_WM_DESTROY
(
    struct PAL_WINDOW_DATA *data, 
    HWND                    hwnd, 
    WPARAM                wparam,
    LPARAM                lparam
)
{   /* mark the window as being destroyed */
    data->EventFlags |= PAL_WINDOW_EVENT_FLAG_DESTROYED;
    /* post WM_QUIT to terminate the message pump */
    PostQuitMessage(0);
    PAL_UNUSED_ARG(hwnd);
    PAL_UNUSED_ARG(wparam);
    PAL_UNUSED_ARG(lparam);
    return 0;
}

/* @summary Perform processing for the WM_MOVE message. This message is sent when the window has been moved on the desktop.
 * @param data The PAL_WINDOW_DATA associated with the window that received the message.
 * @param hwnd The HWND of the window that received the message.
 * @param wparam The WPARAM value associated with the message.
 * @param lparam The LPARAM value associated with the message.
 * @return A message-specific value indicating that the message was processed.
 */
static LRESULT
PAL_WndProc_WSI_WM_MOVE
(
    struct PAL_WINDOW_DATA *data, 
    HWND                    hwnd, 
    WPARAM                wparam,
    LPARAM                lparam
)
{
    PAL_WINDOW_SYSTEM *wsi = data->WindowSystem;
    HMONITOR       monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    UINT             dpi_x = 0;
    UINT             dpi_y = 0;

    wsi->Win32Runtime.GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y);

    if (data->DisplayHandle != monitor)
    {   /* save the update monitor attributes */
        data->EventFlags |= PAL_WINDOW_EVENT_FLAG_MONITOR_CHANGED;
        PAL_WindowDataUpdateDisplayGeometry(data, monitor);
    }
    if (data->DisplayDpiX != dpi_x || data->DisplayDpiY != dpi_y)
    {   /* save the DPI of the monitor on which the window is positioned */
        data->EventFlags  |= PAL_WINDOW_EVENT_FLAG_DPI_CHANGED;
        data->DisplayDpiX  = dpi_x;
        data->DisplayDpiY  = dpi_y;
    }

    /* update the window geometry to reflect the new location */
    PAL_WindowDataUpdateWindowGeometry(data, hwnd, dpi_x, dpi_y);

    /* mark the window has having been moved */
    data->EventFlags |= PAL_WINDOW_EVENT_FLAG_POSITION_CHANGED;
    PAL_UNUSED_ARG(wparam);
    PAL_UNUSED_ARG(lparam);
    return 0;
}

/* @summary Perform processing for the WM_SIZE message. This message is sent when the window has been resized.
 * @param data The PAL_WINDOW_DATA associated with the window that received the message.
 * @param hwnd The HWND of the window that received the message.
 * @param wparam The WPARAM value associated with the message.
 * @param lparam The LPARAM value associated with the message.
 * @return A message-specific value indicating that the message was processed.
 */
static LRESULT
PAL_WndProc_WSI_WM_SIZE
(
    struct PAL_WINDOW_DATA *data, 
    HWND                    hwnd, 
    WPARAM                wparam,
    LPARAM                lparam
)
{
    PAL_WINDOW_SYSTEM *wsi = data->WindowSystem;
    HMONITOR       monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    UINT             dpi_x = 0;
    UINT             dpi_y = 0;

    wsi->Win32Runtime.GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y);

    if (data->DisplayHandle != monitor)
    {   /* save the update monitor attributes */
        data->EventFlags |= PAL_WINDOW_EVENT_FLAG_MONITOR_CHANGED;
        PAL_WindowDataUpdateDisplayGeometry(data, monitor);
    }
    if (data->DisplayDpiX != dpi_x || data->DisplayDpiY != dpi_y)
    {   /* save the DPI of the monitor on which the window is positioned */
        data->EventFlags  |= PAL_WINDOW_EVENT_FLAG_DPI_CHANGED;
        data->DisplayDpiX  = dpi_x;
        data->DisplayDpiY  = dpi_y;
    }

    /* update the window geometry to reflect the new size */
    PAL_WindowDataUpdateWindowGeometry(data, hwnd, dpi_x, dpi_y);

    /* mark the window has having been moved */
    data->EventFlags |= PAL_WINDOW_EVENT_FLAG_SIZE_CHANGED;
    PAL_UNUSED_ARG(wparam);
    PAL_UNUSED_ARG(lparam);
    return 0;
}

/* @summary Perform processing for the WM_SHOWWINDOW message. This message is sent when the window is being shown or hidden.
 * @param data The PAL_WINDOW_DATA associated with the window that received the message.
 * @param hwnd The HWND of the window that received the message.
 * @param wparam The WPARAM value associated with the message.
 * @param lparam The LPARAM value associated with the message.
 * @return A message-specific value indicating that the message was processed.
 */
static LRESULT
PAL_WndProc_WSI_WM_SHOWWINDOW
(
    struct PAL_WINDOW_DATA *data, 
    HWND                    hwnd, 
    WPARAM                wparam,
    LPARAM                lparam
)
{
    if (wparam)
    {   /* the window is being shown */
        PAL_WINDOW_SYSTEM *wsi = data->WindowSystem;
        HMONITOR       monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        UINT             dpi_x = 0;
        UINT             dpi_y = 0;

        wsi->Win32Runtime.GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y);

        if (data->DisplayHandle != monitor)
        {   /* save the update monitor attributes */
            data->EventFlags |= PAL_WINDOW_EVENT_FLAG_MONITOR_CHANGED;
            PAL_WindowDataUpdateDisplayGeometry(data, monitor);
        }
        if (data->DisplayDpiX != dpi_x || data->DisplayDpiY != dpi_y)
        {   /* save the DPI of the monitor on which the window is positioned */
            data->EventFlags  |= PAL_WINDOW_EVENT_FLAG_DPI_CHANGED;
            data->DisplayDpiX  = dpi_x;
            data->DisplayDpiY  = dpi_y;
        }

        /* update the window geometry to reflect the new size */
        PAL_WindowDataUpdateWindowGeometry(data, hwnd, dpi_x, dpi_y);

        /* mark the window has having been moved */
        data->EventFlags |= PAL_WINDOW_EVENT_FLAG_SHOWN;
    }
    else
    {   /* the window is being hidden */
        data->EventFlags |= PAL_WINDOW_EVENT_FLAG_HIDDEN;
    }
    PAL_UNUSED_ARG(wparam);
    PAL_UNUSED_ARG(lparam);
    return DefWindowProc(hwnd, WM_SHOWWINDOW, wparam, lparam);
}

/* @summary Perform processing for the WM_DISPLAYCHANGE message. This message is sent to top-level windows when a display mode changes.
 * @param data The PAL_WINDOW_DATA associated with the window that received the message.
 * @param hwnd The HWND of the window that received the message.
 * @param wparam The WPARAM value associated with the message.
 * @param lparam The LPARAM value associated with the message.
 * @return A message-specific value indicating that the message was processed.
 */
static LRESULT CALLBACK
PAL_WndProc_WSI_WM_DISPLAYCHANGE
(
    struct PAL_WINDOW_DATA *data, 
    HWND                    hwnd, 
    WPARAM                wparam,
    LPARAM                lparam
)
{   /* wparam is the bit depth of the display */
    /* lparam low word is the display width, high word is the height */
    PAL_WINDOW_SYSTEM *wsi = data->WindowSystem;
    HMONITOR       monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    UINT             dpi_x = 0;
    UINT             dpi_y = 0;

    wsi->Win32Runtime.GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y);

    if (data->DisplayHandle != monitor)
    {   /* save the update monitor attributes */
        data->EventFlags |= PAL_WINDOW_EVENT_FLAG_MONITOR_CHANGED;
        PAL_WindowDataUpdateDisplayGeometry(data, monitor);
    }
    if (data->DisplayDpiX != dpi_x || data->DisplayDpiY != dpi_y)
    {   /* save the DPI of the monitor on which the window is positioned */
        data->EventFlags  |= PAL_WINDOW_EVENT_FLAG_DPI_CHANGED;
        data->DisplayDpiX  = dpi_x;
        data->DisplayDpiY  = dpi_y;
    }

    /* update the window geometry to reflect the new size */
    PAL_WindowDataUpdateWindowGeometry(data, hwnd, dpi_x, dpi_y);
    data->EventFlags |= PAL_WINDOW_EVENT_FLAG_MODE_CHANGED;
    return DefWindowProc(hwnd, WM_DISPLAYCHANGE, wparam, lparam);
}

/* @summary Perform processing for the WM_SYSCOMMAND message. This message is used to handle Alt+Enter to toggle fullscreen/windowed mode.
 * @param data The PAL_WINDOW_DATA associated with the window that received the message.
 * @param hwnd The HWND of the window that received the message.
 * @param wparam The WPARAM value associated with the message.
 * @param lparam The LPARAM value associated with the message.
 * @return A message-specific value indicating that the message was processed.
 */
static LRESULT
PAL_WndProc_WSI_WM_SYSCOMMAND
(
    struct PAL_WINDOW_DATA *data, 
    HWND                    hwnd, 
    WPARAM                wparam,
    LPARAM                lparam
)
{   /* MSDN says that the low four bits of wparam are used by the system.
     * mask off the low four bits of wparam prior to testing its value.
     */
    if ((wparam & 0xFFF0) == SC_KEYMENU)
    {   /* check for the character code for Enter/Return */
        if (lparam == VK_RETURN)
        {   /* Alt+Enter was pressed - toggle the window style */
            if (data->StatusFlags & PAL_WINDOW_STATUS_FLAG_WINDOWED)
            {   /* toggle to fullscreen */
                HMONITOR    monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO moninfo;
                RECT             rc;

                /* retrieve and save the current window position and size */
                GetWindowRect(hwnd, &rc);
                data->RestorePositionX    =(pal_sint32_t) rc.left;
                data->RestorePositionY    =(pal_sint32_t) rc.top;
                data->RestoreSizeX        =(pal_uint32_t)(rc.right - rc.left);
                data->RestoreSizeY        =(pal_uint32_t)(rc.bottom - rc.top);
                data->RestoreStyle        =(DWORD       ) GetWindowLong(hwnd, GWL_STYLE);
                data->RestoreStyleEx      =(DWORD       ) GetWindowLong(hwnd, GWL_EXSTYLE);

                /* retrieve information about the monitor containing the window */
                moninfo.cbSize = sizeof(MONITORINFO);
                GetMonitorInfo(monitor, &moninfo);

                /* switch the window style to a fullscreen style */
                rc = moninfo.rcMonitor;
                SetWindowLong (hwnd, GWL_STYLE, WS_POPUP);
                SetWindowPos  (hwnd, HWND_TOP , rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
                ShowWindow    (hwnd, SW_SHOW);
                InvalidateRect(hwnd, NULL, TRUE);
                
                /* note that the window is now in fullscreen mode */
                data->StatusFlags &=~PAL_WINDOW_STATUS_FLAG_WINDOWED;
                data->StatusFlags |= PAL_WINDOW_STATUS_FLAG_FULLSCREEN;
                data->EventFlags  |= PAL_WINDOW_EVENT_FLAG_GO_FULLSCREEN;
            }
            else
            {   /* toggle to windowed */
                SetWindowLong(hwnd, GWL_STYLE  , data->RestoreStyle);
                SetWindowLong(hwnd, GWL_EXSTYLE, data->RestoreStyleEx);
                SetWindowPos (hwnd, HWND_TOP, data->RestorePositionX, data->RestorePositionY, (int) data->RestoreSizeX, (int) data->RestoreSizeY, SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

                /* note that the window is now in windowed mode */
                data->StatusFlags |= PAL_WINDOW_STATUS_FLAG_WINDOWED;
                data->StatusFlags &=~PAL_WINDOW_STATUS_FLAG_FULLSCREEN;
                data->EventFlags  |= PAL_WINDOW_EVENT_FLAG_GO_WINDOWED;
            }
            return 0;
        }
    }
    return DefWindowProc(hwnd, WM_SYSCOMMAND, wparam, lparam);
}

/* @summary Implement the message callback for any window created and managed by the WSI.
 * @param hwnd The HWND of the window that received the message.
 * @param msg The Windows message identifier.
 * @param wparam The WPARAM value associated with the message.
 * @param lparam The LPARAM value associated with the message.
 * @return A message-specific value indicating the result of processing the message.
 */
static LRESULT CALLBACK
PAL_WndProc_WSI
(
    HWND     hwnd, 
    UINT      msg, 
    WPARAM wparam, 
    LPARAM lparam
)
{
    PAL_WINDOW_DATA *data = NULL;
    LRESULT        result = 0;

    /* WM_NCCREATE performs special handling to store the state associated with the window.
     * The handler for WM_NCCREATE executes before the call to CreateWindowEx returns.
     */
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCT *cs = (CREATESTRUCT*) lparam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) cs->lpCreateParams);
        ((PAL_WINDOW_DATA*) cs->lpCreateParams)->OsWindowHandle = hwnd;
        return DefWindowProc(hwnd, msg, wparam, lparam);
    }

    /* WndProc may receive several messages prior to receiving WM_NCCREATE.
     * These messages are processed by the default handler.
     */
    if ((data = (PAL_WINDOW_DATA*) GetWindowLongPtr(hwnd, GWLP_USERDATA)) == NULL)
    {   /* let the default handler process the message */
        return DefWindowProc(hwnd, msg, wparam, lparam);
    }

    /* Process the window messages the application is interested in.
     */
    switch (msg)
    {
        case WM_ACTIVATE:
            { /* the user is activating or deactivating the window */
              result = PAL_WndProc_WSI_WM_ACTIVATE(data, hwnd, wparam, lparam);
            } break;
        case WM_CREATE:
            { /* retrieve the monitor DPI and size the window appropriately */
              result = PAL_WndProc_WSI_WM_CREATE(data, hwnd, wparam, lparam);
            } break;
        case WM_CLOSE:
            { /* mark the window as closed but do not destroy the window */
              result = PAL_WndProc_WSI_WM_CLOSE(data, hwnd, wparam, lparam);
            } break;
        case WM_DESTROY:
            { /* post the WM_QUIT message that causes the main loop to terminate */
              result = PAL_WndProc_WSI_WM_DESTROY(data, hwnd, wparam, lparam);
            } break;
        case WM_SHOWWINDOW:
            { /* the window is being shown or hidden */
              result = PAL_WndProc_WSI_WM_SHOWWINDOW(data, hwnd, wparam, lparam);
            } break;
        case WM_WINDOWPOSCHANGING:
            { /* eat this message to prevent automatic resizing due to device loss */
            } break;
        case WM_MOVE:
            { /* the window has finished being moved */
              result = PAL_WndProc_WSI_WM_MOVE(data, hwnd, wparam, lparam);
            } break;
        case WM_SIZE:
            { /* the window has finished being sized */
              result = PAL_WndProc_WSI_WM_SIZE(data, hwnd, wparam, lparam);
            } break;
        case WM_DISPLAYCHANGE:
            { /* the display mode has been changed */
              result = PAL_WndProc_WSI_WM_DISPLAYCHANGE(data, hwnd, wparam, lparam);
            } break;
        case WM_SYSCOMMAND:
            { /* handle Alt+Enter to toggle between fullscreen and windowed */
              result = PAL_WndProc_WSI_WM_SYSCOMMAND(data, hwnd, wparam, lparam);
            } break;
        case WM_ERASEBKGND:
            { /* tell Windows we erased the background */
#if 0
              HDC dc =(HDC) wparam;
              RECT r; GetClientRect(hwnd, &r);
              FillRect(dc, &r, GetStockObject(BLACK_BRUSH));
#endif
              result = 1;
            } break;
        case WM_PAINT:
            { /* validate the entire client rectangle */
              ValidateRect(hwnd, NULL);
            } break;
        case WM_DPICHANGED:
            { /* retrieve the updated DPI settings and size the window appropriately */
              result = PAL_WndProc_WSI_WM_DPICHANGED(data, hwnd, wparam, lparam);
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
    DEV_BROADCAST_DEVICEINTERFACE   dnf;
    PAL_WSI_WIN32_DISPATCH   win32_disp;
    PAL_WSI_XINPUT_DISPATCH xinput_disp;
    PAL_MEMORY_ARENA              arena;
    PAL_MEMORY_ARENA_INIT    arena_init;
    PAL_MEMORY_LAYOUT      table_layout;
    PAL_MODULE                   shcore;
    PAL_MODULE                   xinput;
    SYSTEM_INFO                 sysinfo;
    MONITORINFO                 moninfo;
    WNDCLASSEX                 wndclass;
    POINT                            pt;
    RECT                             rc;
    WCHAR const             *class_name =L"PAL__NotifyClass";
    HINSTANCE                    module =(HINSTANCE) GetModuleHandleW(NULL);
    HMONITOR                    monitor = NULL;
    HDEVNOTIFY               notify_dev = NULL;
    HWND                     notify_wnd = NULL;
    PAL_WINDOW_SYSTEM              *wsi = NULL;
    pal_uint8_t              *base_addr = NULL;
    pal_usize_t           required_size = 0;
    pal_uint32_t const     MAX_DISPLAYS = 64;
    DWORD                    error_code = ERROR_SUCCESS;
    DWORD                         style = WS_POPUP;
    DWORD                      style_ex = WS_EX_NOACTIVATE;// | WS_EX_NOREDIRECTIONBITMAP;

    /* dynamically resolve entry points */
    PAL_ZeroMemory(&win32_disp , sizeof(PAL_WSI_WIN32_DISPATCH));
    PAL_ZeroMemory(&xinput_disp, sizeof(PAL_WSI_XINPUT_DISPATCH));

    if (PAL_ModuleLoad(&shcore, "Shcore.dll") == 0) {      /* shipped with Win8.1+ */
        PAL_RuntimeFunctionResolve(&win32_disp, &shcore, SetProcessDpiAwareness);
        PAL_RuntimeFunctionResolve(&win32_disp, &shcore, GetDpiForMonitor);
        win32_disp.ShcoreModule   = shcore;
    } else { /* no Shcore available on host */
        PAL_RuntimeFunctionSetStub(&win32_disp, SetProcessDpiAwareness);
        PAL_RuntimeFunctionSetStub(&win32_disp, GetDpiForMonitor);
    }

    if (PAL_ModuleLoad(&xinput, "xinput1_4.dll"  ) == 0 || /* shipped with Win8+ */
        PAL_ModuleLoad(&xinput, "xinput9_1_0.dll") == 0 || /* shipped with Vista */
        PAL_ModuleLoad(&xinput, "xinput1_3.dll"  )) {      /* shipped with June 2010 DirectX SDK */
        PAL_RuntimeFunctionResolve(&xinput_disp, &xinput, XInputEnable);
        PAL_RuntimeFunctionResolve(&xinput_disp, &xinput, XInputGetState);
        PAL_RuntimeFunctionResolve(&xinput_disp, &xinput, XInputSetState);
        PAL_RuntimeFunctionResolve(&xinput_disp, &xinput, XInputGetKeystroke);
        PAL_RuntimeFunctionResolve(&xinput_disp, &xinput, XInputGetCapabilities);
        PAL_RuntimeFunctionResolve(&xinput_disp, &xinput, XInputGetBatteryInformation);
        PAL_RuntimeFunctionResolve(&xinput_disp, &xinput, XInputGetAudioDeviceIds);
        xinput_disp.XInputModule  = xinput;
    } else { /* no XInput available on host */
        PAL_RuntimeFunctionSetStub(&xinput_disp, XInputEnable);
        PAL_RuntimeFunctionSetStub(&xinput_disp, XInputGetState);
        PAL_RuntimeFunctionSetStub(&xinput_disp, XInputSetState);
        PAL_RuntimeFunctionSetStub(&xinput_disp, XInputGetKeystroke);
        PAL_RuntimeFunctionSetStub(&xinput_disp, XInputGetCapabilities);
        PAL_RuntimeFunctionSetStub(&xinput_disp, XInputGetBatteryInformation);
        PAL_RuntimeFunctionSetStub(&xinput_disp, XInputGetAudioDeviceIds);
    }
    /* ... */

    /* SetProcessDpiAwareness must be called prior to calling any APIs that depend on DPI awareness */
    win32_disp.SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

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
    wsi->DisplayCount      = 0;
    wsi->DisplayCapacity   = MAX_DISPLAYS;
    wsi->DisplayEventFlags = PAL_DISPLAY_EVENT_FLAGS_NONE;
    /* ... */

    /* copy over the dispatch tables */
    PAL_CopyMemory(&wsi->Win32Runtime , &win32_disp , sizeof(PAL_WSI_WIN32_DISPATCH));
    PAL_CopyMemory(&wsi->XInputRuntime, &xinput_disp, sizeof(PAL_WSI_XINPUT_DISPATCH));
    
    /* enumerate displays attached to the system. 
     * this populates the ActiveDisplays array and sets DisplayCount.
     */
    PAL_EnumerateDisplays(wsi);

    /* initialize the handle table for window objects */
    PAL_MemoryLayoutInit(&table_layout);
    PAL_MemoryLayoutAdd (&table_layout, PAL_WINDOW_DATA);
    if (PAL_HandleTypeDefine(&wsi->WindowTable, &table_layout, 64, PAL_WINDOW_SYSTEM_NAMESPACE_WINDOW) != 0) {
        goto cleanup_and_fail;
    }

    /* create a hidden window to handle all system notifications 
     * and receive input device events for the application.
     */
    if (!GetClassInfoEx(module , class_name, &wndclass)) {
        wndclass.cbSize        = sizeof(WNDCLASSEX);
        wndclass.cbClsExtra    = 0;
        wndclass.cbWndExtra    = sizeof(PAL_WINDOW_SYSTEM*);
        wndclass.hInstance     = GetModuleHandle(NULL);
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

    /* the notification window is positioned on the main display and covers the work area.
     */
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
    PAL_ModuleUnload(&xinput);
    PAL_ModuleUnload(&shcore);
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
        /* unload manually-loaded DLLs from the process */
        PAL_ModuleUnload(&wsi->XInputRuntime.XInputModule);
        PAL_ModuleUnload(&wsi->Win32Runtime.ShcoreModule);
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

PAL_API(int)
PAL_WindowSystemQueryWindowDisplay
(
    struct PAL_DISPLAY_INFO *info, 
    struct PAL_WINDOW_SYSTEM *wsi, 
    PAL_WINDOW             window
)
{
    PAL_WINDOW_OBJECT obj;
    if (PAL_WindowObjectResolve(&obj, wsi, window) == 0) {
        PAL_WINDOW_DATA  *data = obj.WindowData;
        PAL_DISPLAY_DATA *disp = wsi->ActiveDisplays;
        HMONITOR       monitor = MonitorFromWindow(data->OsWindowHandle, MONITOR_DEFAULTTONEAREST);
        pal_uint32_t      i, n;

        for (i = 0, n = wsi->DisplayCount; i < n; ++i) {
            if (disp[i].MonitorHandle == monitor) {
                PAL_DisplayDataToDisplayInfo(info, &disp[i], i);
                return 0;
            }
        }
        PAL_ZeroMemory(info, sizeof(PAL_DISPLAY_INFO));
        return -1;
    } else {
        PAL_ZeroMemory(info, sizeof(PAL_DISPLAY_INFO));
        return -1;
    }
}

PAL_API(PAL_WINDOW)
PAL_WindowCreate
(
    struct PAL_WINDOW_STATE *state, 
    struct PAL_WINDOW_SYSTEM  *wsi, 
    struct PAL_WINDOW_INIT   *init
)
{
    HINSTANCE         module =(HINSTANCE) GetModuleHandle(NULL);
    HMONITOR         monitor = NULL;
    HWND                hwnd = NULL;
    WCHAR const  *class_name =L"PAL_WSI_WndClass";
    pal_sint32_t   virtual_x = init->PositionX;
    pal_sint32_t   virtual_y = init->PositionY;
    pal_uint32_t    dim_x_px = init->SizeX;
    pal_uint32_t    dim_y_px = init->SizeY;
    pal_uint32_t style_flags = init->StyleFlags;
    DWORD         error_code = ERROR_SUCCESS;
    DWORD           style_ex = 0;
    DWORD              style = 0;
    MONITORINFO      moninfo;
    WNDCLASSEX      wndclass;
    POINT                 pt;
    RECT                  rc;
    PAL_WINDOW_OBJECT    obj;

    if ((init->CreateFlags & PAL_WINDOW_CREATE_FLAG_USE_DISPLAY) && init->TargetDisplay == NULL) {
        assert(init->TargetDisplay != NULL);
        return PAL_HANDLE_INVALID;
    }

    /* register the window class if necessary */
    if (!GetClassInfoEx(module, class_name, &wndclass)) {
        wndclass.cbSize         = sizeof(WNDCLASSEX);
        wndclass.cbClsExtra     = 0;
        wndclass.cbWndExtra     = sizeof(PAL_WINDOW_DATA*);
        wndclass.hInstance      = module;
        wndclass.lpszClassName  = class_name;
        wndclass.lpszMenuName   = NULL;
        wndclass.lpfnWndProc    = PAL_WndProc_WSI;
        wndclass.hIcon          = LoadIcon  (0, IDI_APPLICATION);
        wndclass.hIconSm        = LoadIcon  (0, IDI_APPLICATION);
        wndclass.hCursor        = LoadCursor(0, IDC_ARROW);
        wndclass.style          = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        wndclass.hbrBackground  = NULL;
        if (!RegisterClassEx(&wndclass)) {
            error_code = GetLastError();
            return PAL_HANDLE_INVALID;
        }
    }

    /* figure out the display this window will be placed on */
    if (init->CreateFlags & PAL_WINDOW_CREATE_FLAG_USE_DISPLAY) {
        /* the supplied position indicates a relative position */
        monitor =(HMONITOR) init->TargetDisplay->DeviceId;
    } else {
        /* the supplied position indicates an absolute position */
        pt.x    = init->PositionX;
        pt.y    = init->PositionY;
        monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    }
    moninfo.cbSize = sizeof(MONITORINFO);
    GetMonitorInfo(monitor, &moninfo);

    /* validate and adjust style flags */
    if (style_flags == PAL_WINDOW_STYLE_FLAGS_NONE) {
        style_flags  = PAL_WINDOW_STYLE_FLAG_FULLSCREEN;
    }
    /* fullscreen windows do not have chrome and are not resizable */
    if (style_flags &  PAL_WINDOW_STYLE_FLAG_FULLSCREEN) {
        style_flags &=~PAL_WINDOW_STYLE_FLAG_CENTER;
        style_flags &=~PAL_WINDOW_STYLE_FLAG_CHROME;
        style_flags &=~PAL_WINDOW_STYLE_FLAG_RESIZABLE;
        virtual_x    =(pal_sint32_t) moninfo.rcMonitor.left;
        virtual_y    =(pal_sint32_t) moninfo.rcMonitor.top;
        dim_x_px     =(pal_uint32_t)(moninfo.rcMonitor.right - moninfo.rcMonitor.left);
        dim_y_px     =(pal_uint32_t)(moninfo.rcMonitor.bottom - moninfo.rcMonitor.top);
        rc = moninfo.rcMonitor;
    } else {
        rc = moninfo.rcWork;
    }

    /* adjust position and size to fit */
    if (dim_x_px == 0) {
        dim_x_px  =(rc.right - rc.left);
    }
    if (dim_y_px == 0) {
        dim_y_px  =(rc.bottom - rc.top);
    }
    if (style_flags & PAL_WINDOW_STYLE_FLAG_CENTER) {
        virtual_x = rc.left + (rc.right  - rc.left - dim_x_px) / 2;
        virtual_y = rc.top  + (rc.bottom - rc.top  - dim_y_px) / 2;
    }
    if ((virtual_x + dim_x_px) > ((pal_uint32_t)(rc.right - rc.left))) {
        dim_x_px =  (rc.right - rc.left) - virtual_x;
    }
    if ((virtual_y + dim_y_px) > ((pal_uint32_t)(rc.bottom - rc.top))) {
        dim_y_px =  (rc.bottom - rc.top) - virtual_y;
    }

    /* figure out the Win32 style flags */
    if (style_flags & PAL_WINDOW_STYLE_FLAG_CHROME) {
        /* create a resizable window with the normal chrome */
        style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        if ((style_flags & PAL_WINDOW_STYLE_FLAG_RESIZABLE) == 0) {
            style &= ~WS_THICKFRAME;
        }
    } else {
        /* create a borderless window */
        style = WS_POPUP;
    }

    /* acquire the window object */
    if (PAL_WindowObjectCreate(&obj, wsi) != 0) {
        return PAL_HANDLE_INVALID;
    }
    obj.WindowData->OsWindowHandle      = NULL;
    obj.WindowData->OsModuleHandle      = module;
    obj.WindowData->DisplayHandle       = monitor;
    obj.WindowData->DisplayPositionX    =(pal_sint32_t) moninfo.rcMonitor.left;
    obj.WindowData->DisplayPositionY    =(pal_sint32_t) moninfo.rcMonitor.top;
    obj.WindowData->DisplaySizeX        =(pal_uint32_t)(moninfo.rcMonitor.right - moninfo.rcMonitor.left);
    obj.WindowData->DisplaySizeY        =(pal_uint32_t)(moninfo.rcMonitor.bottom - moninfo.rcMonitor.top);
    obj.WindowData->WindowPositionX     = virtual_x;
    obj.WindowData->WindowPositionY     = virtual_y;
    obj.WindowData->WindowSizeLogicalX  = dim_x_px;
    obj.WindowData->WindowSizeLogicalY  = dim_y_px;
    obj.WindowData->WindowSizePhysicalX = dim_x_px;
    obj.WindowData->WindowSizePhysicalY = dim_y_px;
    obj.WindowData->ClientSizeLogicalX  = dim_x_px;
    obj.WindowData->ClientSizeLogicalY  = dim_y_px;
    obj.WindowData->ClientSizePhysicalX = dim_x_px;
    obj.WindowData->ClientSizePhysicalY = dim_y_px;
    obj.WindowData->RestorePositionX    = virtual_x;
    obj.WindowData->RestorePositionY    = virtual_y; 
    obj.WindowData->RestoreSizeX        = dim_x_px;
    obj.WindowData->RestoreSizeY        = dim_y_px;
    obj.WindowData->RestoreStyle        = style;
    obj.WindowData->RestoreStyleEx      = style_ex;
    obj.WindowData->DisplayDpiX         = USER_DEFAULT_SCREEN_DPI;
    obj.WindowData->DisplayDpiY         = USER_DEFAULT_SCREEN_DPI;
    obj.WindowData->EventFlags          = PAL_WINDOW_EVENT_FLAGS_NONE;
    obj.WindowData->EventCount          = 0;
    obj.WindowData->StatusFlags         = PAL_WINDOW_STATUS_FLAGS_NONE;
    obj.WindowData->CreateFlags         = style_flags;

    /* create the window */
    if ((hwnd = CreateWindowEx(style_ex, class_name, init->WindowTitle, style, virtual_x, virtual_y, dim_x_px, dim_y_px, NULL, NULL, module, obj.WindowData)) == NULL) {
        PAL_WindowObjectDelete(wsi, obj.Handle);
        return PAL_HANDLE_INVALID;
    }
    ShowWindow(hwnd, SW_SHOW);
    if (state) {
        PAL_CopyWindowDataToWindowState(state, obj.WindowData);
        state->EventCount = 1;
    }
    return obj.Handle;
}

PAL_API(void)
PAL_WindowDelete
(
    struct PAL_WINDOW_STATE *state, 
    struct PAL_WINDOW_SYSTEM  *wsi,
    PAL_WINDOW              window
)
{
    PAL_WINDOW_OBJECT obj;
    if (PAL_WindowObjectResolve(&obj, wsi, window) == 0) {
        PAL_WINDOW_DATA  *data = obj.WindowData;
        HWND              hwnd = obj.WindowData->OsWindowHandle;
        UINT               nev = 0;
        BOOL               ret;
        MSG                msg;

        /* if necessary, destroy the window and wait */
        data->EventFlags = PAL_WINDOW_EVENT_FLAGS_NONE;
        if (DestroyWindow(hwnd) != FALSE) {
            while ((ret  = GetMessage(&msg, hwnd, 0, 0)) != 0) {
                if (ret == -1 || msg.message == WM_QUIT) {
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
                nev++;
            }
        }
        if (state != NULL) {
            PAL_CopyWindowDataToWindowState(state, data);
            state->EventFlags |= PAL_WINDOW_EVENT_FLAG_DESTROYED;
            state->EventCount  = nev;
        }
        PAL_WindowObjectDelete(wsi, window);
    }
}

PAL_API(int)
PAL_WindowQueryState
(
    struct PAL_WINDOW_STATE *state, 
    struct PAL_WINDOW_SYSTEM  *wsi, 
    PAL_WINDOW              window
)
{
    PAL_WINDOW_OBJECT obj;
    if (PAL_WindowObjectResolve(&obj, wsi, window) == 0) {
        PAL_CopyWindowDataToWindowState(state, obj.WindowData);
        return 0;
    }
    return -1;
}

PAL_API(int)
PAL_WindowUpdateState
(
    struct PAL_WINDOW_STATE *state, 
    struct PAL_WINDOW_SYSTEM  *wsi, 
    PAL_WINDOW              window
)
{
    PAL_WINDOW_OBJECT obj;
    if (PAL_WindowObjectResolve(&obj, wsi, window) == 0) {
        PAL_WINDOW_DATA  *data = obj.WindowData;
        HWND              hwnd = obj.WindowData->OsWindowHandle;
        UINT               nev = 0;
        BOOL               ret;
        MSG                msg;

        /* process any queued window messages */
        while ((ret  = GetMessage(&msg, hwnd, 0, 0)) != 0) {
            if (ret == -1 || msg.message == WM_QUIT) {
                data->EventFlags |= PAL_WINDOW_EVENT_FLAG_DESTROYED;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            nev++;
        }
        /* return the updated state to the caller */
        PAL_CopyWindowDataToWindowState(state, data);
        state->EventCount = nev;
        return 0;
    }
    return -1;
}

PAL_API(int)
PAL_WindowIsClosed
(
    struct PAL_WINDOW_STATE *state
)
{
    return (state->StatusFlags & PAL_WINDOW_STATUS_FLAG_CLOSED) != 0;
}

PAL_API(int)
PAL_WindowIsFullscreen
(
    struct PAL_WINDOW_STATE *state
)
{
    return (state->StatusFlags & PAL_WINDOW_STATUS_FLAG_FULLSCREEN) != 0;
}

