#ifndef COLOR_FOCUS_FIX_H
#define COLOR_FOCUS_FIX_H

#include <stddef.h>
#include <windows.h>

BOOL color_focus_fix_install(
    BOOL enable_primary,
    BOOL enable_linked,
    char *error,
    size_t error_size);

#endif
