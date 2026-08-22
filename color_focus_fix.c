#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "patch.h"
#include "color_focus_fix.h"

#if defined(_WIN64)
#error The Total Annihilation patch loader and this hook must be built for 32-bit x86.
#endif

#define DRAW_FOCUS_CALL_PRIMARY   ((BYTE *)0x004A947B)
#define DRAW_FOCUS_CALL_LINKED    ((BYTE *)0x004A9505)
#define DRAW_FOCUS_ORIGINAL_ADDR  ((void *)0x004A16F0)

#define UI_CONTROL_ARRAY_OWNER_OFFSET 0x18
#define UI_CONTROL_ARRAY_OFFSET       0x04
#define UI_CONTROL_COUNT_OFFSET       0xB6

#define GUI_CONTROL_SIZE        0x15B
#define GUI_CONTROL_TYPE_OFFSET 0x00
#define GUI_CONTROL_NAME_OFFSET 0x02
#define GUI_CONTROL_WIDTH       0x17
#define GUI_CONTROL_HEIGHT      0x19

#define GUI_CONTROL_TYPE_GAF 6
#define PLAYER_COLOR_SIZE     20

typedef void (WINAPI *TA_DRAW_FOCUS)(void *ui, int control_index, int style);

static TA_DRAW_FOCUS const g_draw_focus =
    (TA_DRAW_FOCUS)DRAW_FOCUS_ORIGINAL_ADDR;

static BOOL is_player_color_control(void *ui, int control_index)
{
    BYTE *owner;
    BYTE *controls;
    BYTE *control;
    int last_control_index;
    char player_number;

    if (!ui || control_index < 0)
        return FALSE;

    owner = *(BYTE **)((BYTE *)ui + UI_CONTROL_ARRAY_OWNER_OFFSET);
    if (!owner)
        return FALSE;

    controls = *(BYTE **)(owner + UI_CONTROL_ARRAY_OFFSET);
    if (!controls)
        return FALSE;

    last_control_index = (int)*(SHORT *)(controls + UI_CONTROL_COUNT_OFFSET);
    if (control_index > last_control_index)
        return FALSE;

    control = controls + (size_t)control_index * GUI_CONTROL_SIZE;
    if (control[GUI_CONTROL_TYPE_OFFSET] != GUI_CONTROL_TYPE_GAF)
        return FALSE;

    if (*(SHORT *)(control + GUI_CONTROL_WIDTH) != PLAYER_COLOR_SIZE ||
        *(SHORT *)(control + GUI_CONTROL_HEIGHT) != PLAYER_COLOR_SIZE)
        return FALSE;

    if (memcmp(control + GUI_CONTROL_NAME_OFFSET, "Color", 5) != 0)
        return FALSE;

    player_number = (char)control[GUI_CONTROL_NAME_OFFSET + 5];
    return player_number >= '0' && player_number <= '9';
}

static void WINAPI draw_focus_without_player_color_halo(
    void *ui,
    int control_index,
    int style)
{
    if (is_player_color_control(ui, control_index))
        return;

    g_draw_focus(ui, control_index, style);
}

BOOL color_focus_fix_install(
    BOOL enable_primary,
    BOOL enable_linked,
    char *error,
    size_t error_size)
{
    static const BYTE expected_primary_call[] = { 0xE8, 0x70, 0x82, 0xFF, 0xFF };
    static const BYTE expected_linked_call[] = { 0xE8, 0xE6, 0x81, 0xFF, 0xFF };
    static const BYTE expected_focus_entry[] = { 0x83, 0xEC, 0x10, 0xBA, 0x01, 0x00, 0x00, 0x00 };

    if (error && error_size)
        error[0] = '\0';

    if (!enable_primary && !enable_linked)
        return TRUE;

    if (sizeof(void *) != 4u)
    {
        if (error && error_size)
            sprintf_s(error, error_size, "The player-color focus fix requires a 32-bit build.");
        return FALSE;
    }

    if ((enable_primary &&
            memcmp(DRAW_FOCUS_CALL_PRIMARY, expected_primary_call, sizeof(expected_primary_call)) != 0) ||
        (enable_linked &&
            memcmp(DRAW_FOCUS_CALL_LINKED, expected_linked_call, sizeof(expected_linked_call)) != 0))
    {
        if (error && error_size)
            sprintf_s(error, error_size,
                "Unexpected focus-render calls in TotalA.exe. The player-color fix was not installed.");
        return FALSE;
    }

    if (memcmp(DRAW_FOCUS_ORIGINAL_ADDR, expected_focus_entry, sizeof(expected_focus_entry)) != 0)
    {
        if (error && error_size)
            sprintf_s(error, error_size,
                "The focus renderer does not match the supported TotalA.exe.");
        return FALSE;
    }

    if (enable_primary)
    {
        (void)patch_call(
            (char *)DRAW_FOCUS_CALL_PRIMARY,
            (char *)draw_focus_without_player_color_halo);
        FlushInstructionCache(
            GetCurrentProcess(),
            DRAW_FOCUS_CALL_PRIMARY,
            sizeof(expected_primary_call));
    }

    if (enable_linked)
    {
        (void)patch_call(
            (char *)DRAW_FOCUS_CALL_LINKED,
            (char *)draw_focus_without_player_color_halo);
        FlushInstructionCache(
            GetCurrentProcess(),
            DRAW_FOCUS_CALL_LINKED,
            sizeof(expected_linked_call));
    }

    return TRUE;
}
