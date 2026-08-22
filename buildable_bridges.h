#ifndef BUILDABLE_BRIDGES_H
#define BUILDABLE_BRIDGES_H

#include <stddef.h>
#include <windows.h>

BOOL buildable_bridges_install(
    BOOL enable_parser,
    BOOL enable_traversal,
    char *error,
    size_t error_size);

#endif
