/**
 * @summary Implement the PAL_Module* APIs for the Win32 platform.
 */
#include "pal_win32_dylib.h"

PAL_API(int)
PAL_ModuleLoad
(
    struct PAL_MODULE *module, 
    char const          *path
)
{
    module->Handle = LoadLibraryA(path);
    return (module->Handle != NULL) ? 0 : -1;
}

PAL_API(void)
PAL_ModuleUnload
(
    struct PAL_MODULE *module
)
{
    if (module->Handle != NULL) {
        FreeLibrary(module->Handle);
        module->Handle = NULL;
    }
}

PAL_API(int)
PAL_ModuleIsValid
(
    struct PAL_MODULE *module
)
{
    return (module->Handle != NULL) ? 1 : 0;
}

PAL_API(void*)
PAL_ModuleResolveSymbol
(
    struct PAL_MODULE *module, 
    char const        *symbol
)
{
    return (void*) GetProcAddress(module->Handle, symbol);
}
