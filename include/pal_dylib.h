/**
 * @summary Define the PAL types and API entry points used for dynamically 
 * loading code into the process address space.
 */
#ifndef __PAL_DYLIB_H__
#define __PAL_DYLIB_H__

#ifndef __PAL_H__
#include "pal.h"
#endif

/* @summary Forward-declare the types exported by this module.
 * The type definitions are included in the platform-specific header.
 */
struct  PAL_MODULE;

#ifdef __cplusplus
extern "C" {
#endif

/* @summary Attempt to load a named module into the process address space.
 * @param module On return, the handle of the loaded module is written to this location.
 * @param path The path and filename of the module to load. 
 * @return Zero if the module is successfully loaded, or non-zero if the module could not be loaded.
 */
PAL_API(int)
PAL_ModuleLoad
(
    struct PAL_MODULE *module, 
    char const          *path
);

/* @summary Decrement the reference count on a loaded module. If the module reference count reaches zero, the module is unloaded from the process address space.
 * @param module The handle of the module to unload, returned by a prior call to PAL_ModuleLoad.
 */
PAL_API(void)
PAL_ModuleUnload
(
    struct PAL_MODULE *module
);

/* @summary Determine whether a PAL_MODULE represents a valid module handle.
 * @param module The module handle to inspect.
 * @return Non-zero if the module handle is valid, or zero if the module handle is invalid.
 */
PAL_API(int)
PAL_ModuleIsValid
(
    struct PAL_MODULE *module
);

/* @summary Resolve a function within a module loaded into the process address space.
 * @param module THe handle of the module that defines the symbol.
 * @param symbol A nul-terminated ANSI string specifying the mangled name of the exported symbol.
 * @return The address of the symbol within the process address space, or NULL if no symbol with the specified name was found.
 */
PAL_API(void*)
PAL_ModuleResolveSymbol
(
    struct PAL_MODULE *module, 
    char const        *symbol
);

#ifdef __cplusplus
}; /* extern "C" */
#endif

/* @summary Include the appropriate platform-specific header.
 */
#if   PAL_TARGET_PLATFORM == PAL_PLATFORM_WIN32 || PAL_TARGET_PLATFORM == PAL_PLATFORM_WINRT
    #include "pal_win32_dylib.h"
#elif PAL_TARGET_PLATFORM == PAL_PLATFORM_LINUX || PAL_TARGET_PLATFORM == PAL_PLATFORM_ANDROID
    #include "pal_linux_dylib.h"
#elif PAL_TARGET_PLATFORM == PAL_PLATFORM_MACOS || PAL_TARGET_PLATFORM == PAL_PLATFORM_IOS
    #include "pal_apple_dylib.h"
#else
    #error   pal_dylib.h: No implementation of the abstraction layer for your platform.
#endif

#endif /* __PAL_DYLIB_H__ */
