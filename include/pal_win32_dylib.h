/**
 * @summary Define the platform-specific types and other internal bits for the 
 * Microsoft Windows Desktop platform.
 */
#ifndef __PAL_WIN32_DYLIB_H__
#define __PAL_WIN32_DYLIB_H__

#ifndef __PAL_DYLIB_H__
#include "pal_dylib.h"
#endif

#ifndef PAL_NO_INCLUDES
#include <Windows.h>
#endif

/* @summary Define the data associated with an exe or dll loaded into the process address space.
 */
typedef struct PAL_MODULE {
    HMODULE                            Handle;                 /* The Win32 module handle.  */
} PAL_MODULE;

#endif /* __PAL_WIN32_DYLIB_H__ */
