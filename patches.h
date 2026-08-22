#ifndef PATCHES_H 
#define PATCHES_H 

typedef struct {
    DWORD start;
    DWORD end;
}PATCH_OFFSET;

typedef struct {
    BOOL player_color_focus_primary;
    BOOL player_color_focus_linked;
    BOOL buildable_bridges;
    BOOL bridge_traversal;
} PATCH_RUNTIME_OPTIONS;

#define GET_MEM_ADDRESS(a) \
    ((DWORD)GetModuleHandleA(NULL) + (a) + ( \
    (a) >= 0x0FF600 + 0x10A00 ? -(int)GetModuleHandleA(NULL) - (a) : \
    (a) >= 0x0FF600 ? 0x1A00 : \
    (a) >= 0x0FAE00 ? 0x1200 : \
    (a) >= 0x000400 ? 0x0C00 : -(int)GetModuleHandleA(NULL) - (a)))

#define GET_PHYS_OFFSET(a) ( \
    (a) >= 0x00501000 ? (a) - (DWORD)GetModuleHandleA(NULL) - 0x1A00 : \
    (a) >= 0x004FC000 ? (a) - (DWORD)GetModuleHandleA(NULL) - 0x1200 : \
    (a) >= 0x00401000 ? (a) - (DWORD)GetModuleHandleA(NULL) - 0x0C00 : \
    0)

extern char g_patches_debug_str[];

int patches_apply(HMODULE mod);
const PATCH_RUNTIME_OPTIONS *patches_get_runtime_options(void);

#endif
