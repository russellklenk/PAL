/**
 * @summary Define the PAL types and API entry points used for dynamically 
 * loading code into the process address space.
 */
#ifndef __PAL_DYLIB_H__
#define __PAL_DYLIB_H__

#ifndef __PAL_H__
#include "pal.h"
#endif

/* @summary Helper macro for populating a dispatch table with functions loaded at runtime.
 * If the function is not found, the entry point is updated to point to a stub implementation provided by the caller.
 * This macro relies on specific naming conventions:
 * - The signature must be BlahBlahBlah_Func where BlahBlahBlah corresponds to the _func argument.
 * - The dispatch table field must be BlahBlahBlah where BlahBlahBlah corresponds to the _func argument.
 * - The stub function must be named BlahBlahBlah_Stub where BlahBlahBlah corresponds to the _func argument.
 * @param _disp A pointer to the dispatch table to populate.
 * @param _module A pointer to the PAL_MODULE representing the module loaded into the process address space.
 * @param _func The name of the function to dynamically load.
 */
#ifndef PAL_RuntimeFunctionResolve
#define PAL_RuntimeFunctionResolve(_disp, _module, _func)                       \
    for (;;) {                                                                  \
        (_disp)->_func=(_func##_Func)PAL_ModuleResolveSymbol((_module), #_func);\
        if ((_disp)->_func == NULL) {                                           \
            (_disp)->_func  = _func##_Stub;                                     \
        }                                                                       \
        break;                                                                  \
    }
#endif

/* @summary Set a runtime-resolved function entry point to point at the stub implementation provided by the application.
 * @param _disp A pointer to the dispatch table to populate.
 * @param _func The name of the function to dynamically load.
 */
#ifndef PAL_RuntimeFunctionSetStub
#define PAL_RuntimeFunctionSetStub(_disp, _func)                               \
    (_disp)->_func=(_func##_Func) _func##_Stub
#endif

/* @summary Determine whether a runtime-resolved function was resolved to its stub implementation, meaning that it is not implemented on the host.
 * @param _disp A pointer to the dispatch table.
 * @param _func The name of the function to check.
 * @return Non-zero if the dispatch table entry for _func points to the stub implementation.
 */
#ifndef PAL_RuntimeFunctionIsMissing
#define PAL_RuntimeFunctionIsMissing(_disp, _func)                             \
    (_disp)->_func == _func##_Stub
#endif

/* @summary Define a general signature for a dynamically loaded function. 
 * Code will have to case the function pointer to the specific type.
 */
typedef int (*PAL_Func)(void);

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
PAL_API(PAL_Func)
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
