#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "patch.h"
#include "buildable_bridges.h"

#if defined(_WIN64)
#error The Total Annihilation patch loader and this hook must be built for 32-bit x86.
#endif

#define YARDMAP_DOT_HANDLER       ((BYTE *)0x0042CFCA)
#define YARDMAP_DOT_HANDLER_JUMP  ((BYTE *)0x0042CFD4)
#define YARDMAP_EQUALS_CASE       ((BYTE *)0x0042D1A7)

#define MOVEMENT_CELL_EVALUATOR   ((BYTE *)0x0047DFC0)
#define PATH_GRID_QUERY            ((BYTE *)0x0040D7B0)
#define CAN_ATTACH_UNIT_TO_PIECE    ((BYTE *)0x0047DB70)
#define UNITS_FIX_YPOS             ((BYTE *)0x0048A870)

#define MOVE_UNIT_CAN_ATTACH_CALL       ((BYTE *)0x0043D912)
#define FIX_YPOS_CALL_CREATE_UNIT       ((BYTE *)0x00486109)
#define FIX_YPOS_CALL_CREATE_NETWORK    ((BYTE *)0x00486306)
#define FIX_YPOS_CALL_UPDATE_UNITS      ((BYTE *)0x0048AFB0)
#define FIX_YPOS_CALL_SEND_MOVE         ((BYTE *)0x0048BA4C)

#define PARSE_LAVAWORLD_CALL             ((BYTE *)0x0043657B)
#define TDF_GET_INT                      ((BYTE *)0x004C46C0)
#define WATER_DAMAGE_CALL                ((BYTE *)0x0048AF32)
#define UNITS_MAKE_DAMAGE                ((BYTE *)0x00489BB0)

#define TA_DYNMEM_POINTER_ADDRESS       ((BYTE **)0x00511DE8)

#define DYN_MAP_WIDTH_OFFSET            0x14233
#define DYN_MAP_HEIGHT_OFFSET           0x14237
#define DYN_FEATURE_COUNT_OFFSET        0x14253
#define DYN_FEATURE_DEFS_OFFSET         0x1426F
#define DYN_VISIBILITY_MAP_OFFSET       0x14273
#define DYN_WATER_LEVEL_OFFSET          0x1427F
#define DYN_PLOT_MAP_OFFSET             0x14287
#define DYN_UNIT_ARRAY_OFFSET           0x14357

#define PLOT_RECORD_SIZE                0x0D
#define PLOT_UNIT_ID_OFFSET             0x00
#define PLOT_MAX_HEIGHT_OFFSET          0x05
#define PLOT_MIN_HEIGHT_OFFSET          0x06
#define PLOT_FEATURE_ID_OFFSET          0x08
#define PLOT_FEATURE_BACK_X_OFFSET      0x0A
#define PLOT_FEATURE_BACK_Z_OFFSET      0x0B
#define PLOT_FLAGS_OFFSET               0x0C
#define PLOT_BRIDGE_FLAG                0x02

/* A deck-aligned bridge model places its walkable top at its Y origin. */
#define BRIDGE_DECK_SURFACE_OFFSET      0

#define UNIT_POSITION_X_OFFSET          0x06A
#define UNIT_POSITION_Y_OFFSET          0x06E
#define UNIT_POSITION_Z_OFFSET          0x072
#define UNIT_DEFINITION_OFFSET          0x092
#define UNIT_RUNTIME_FLAGS_OFFSET       0x110
#define UNIT_BUILDING_FLAG              0x20000000u
#define UNIT_PITCH_OFFSET               0x064
#define UNIT_ROLL_OFFSET                0x068

#define UNITDEF_FLAGS_OFFSET            0x241
#define UNITDEF_CAN_FLY_FLAG            0x00000800u
#define UNITDEF_CAN_HOVER_FLAG          0x00001000u
#define UNITDEF_FLOATER_FLAG            0x00080000u
#define UNITDEF_FOOTPRINT_OFFSET         0x14A
#define UNITDEF_MAX_WATER_DEPTH_OFFSET   0x1BE
#define UNITDEF_MIN_WATER_DEPTH_OFFSET   0x1C0
#define UNITDEF_LAND_SLOPE_OFFSET        0x228
#define UNITDEF_WATER_SLOPE_OFFSET       0x229
#define UNITDEF_TERRAIN_CHECK_OFFSET     0x22F

#define MOVEMENT_MAX_WATER_DEPTH_OFFSET 0x08
#define MOVEMENT_MIN_WATER_DEPTH_OFFSET 0x0A
#define MOVEMENT_LAND_HARD_SLOPE        0x0C
#define MOVEMENT_LAND_SOFT_SLOPE        0x0D
#define MOVEMENT_WATER_HARD_SLOPE       0x0E
#define MOVEMENT_WATER_SOFT_SLOPE       0x0F
#define MOVEMENT_UNIT_CLEARANCE_OFFSET  0x1C

#define PATHFINDER_MOVEMENT_CLASS_OFFSET 0x64
#define PATHFINDER_VISIBILITY_BIT_OFFSET 0x78

#define FEATURE_RECORD_SIZE             0x100
#define FEATURE_BLOCKING_FLAG_OFFSET    0x0FE
#define FEATURE_BLOCKING_FLAG           0x40

#define UNIT_ARRAY_RECORD_SIZE          0x118
#define UNIT_OBJECT_CLEARANCE_OFFSET    0x26

#define UNITDEF_YARDMAP_OFFSET 0x14E
#define YARDMAP_BRIDGE_CELL    0x01
#define YARDMAP_EQUALS_CHAR    0x3D

typedef void (__stdcall *units_fix_ypos_fn)(BYTE *unit);
typedef unsigned int (__stdcall *can_attach_unit_fn)(
    BYTE *unit_definition,
    int unit_id,
    uint32_t packed_plot_position,
    int occupancy_type);
typedef int (__thiscall *tdf_get_int_fn)(
    BYTE *tdf_file,
    const char *name,
    int default_value);
typedef void (__stdcall *units_make_damage_fn)(
    BYTE *source_unit,
    BYTE *target_unit,
    int damage,
    int damage_type,
    int unused);

static const units_fix_ypos_fn original_units_fix_ypos =
    (units_fix_ypos_fn)UNITS_FIX_YPOS;
static const can_attach_unit_fn original_can_attach_unit =
    (can_attach_unit_fn)CAN_ATTACH_UNIT_TO_PIECE;
static const tdf_get_int_fn original_tdf_get_int =
    (tdf_get_int_fn)TDF_GET_INT;
static const units_make_damage_fn original_units_make_damage =
    (units_make_damage_fn)UNITS_MAKE_DAMAGE;

/*
 * This OTA policy is read from [GlobalHeader] alongside lavaworld. Missing
 * keys intentionally preserve bridge transit on every existing map.
 */
static BOOL g_map_is_lava_world = FALSE;
static BOOL g_map_bridges_override_impassable_terrain = TRUE;
static BOOL g_bridge_traversal_enabled = FALSE;

static uint16_t read_u16(const BYTE *address)
{
    uint16_t value;
    memcpy(&value, address, sizeof(value));
    return value;
}

static int16_t read_i16(const BYTE *address)
{
    int16_t value;
    memcpy(&value, address, sizeof(value));
    return value;
}

static uint32_t read_u32(const BYTE *address)
{
    uint32_t value;
    memcpy(&value, address, sizeof(value));
    return value;
}

static int32_t read_i32(const BYTE *address)
{
    int32_t value;
    memcpy(&value, address, sizeof(value));
    return value;
}

static BYTE *read_pointer(const BYTE *address)
{
    return (BYTE *)(uintptr_t)read_u32(address);
}

static void write_u16(BYTE *address, uint16_t value)
{
    memcpy(address, &value, sizeof(value));
}

static void write_i32(BYTE *address, int32_t value)
{
    memcpy(address, &value, sizeof(value));
}

static BOOL call_targets(const BYTE *call_site, const BYTE *target)
{
    return call_site[0] == 0xE8 &&
        call_site + 5 + read_i32(call_site + 1) == target;
}

static BOOL plot_feature_blocks(
    const BYTE *dynmem,
    const BYTE *plot,
    int map_width)
{
    uint16_t feature_id = read_u16(plot + PLOT_FEATURE_ID_OFFSET);
    BYTE *feature_defs;

    if (feature_id == 0xFFFF)
        return FALSE;

    if (feature_id >= 0xFFFB)
    {
        if (feature_id != 0xFFFE)
            return TRUE;

        plot -= ((unsigned int)plot[PLOT_FEATURE_BACK_X_OFFSET] *
                (unsigned int)map_width +
            (unsigned int)plot[PLOT_FEATURE_BACK_Z_OFFSET]) *
            PLOT_RECORD_SIZE;
        feature_id = read_u16(plot + PLOT_FEATURE_ID_OFFSET);
        if (feature_id >= 0xFFFB)
            return FALSE;
    }
    else if ((int)feature_id >=
        read_i32(dynmem + DYN_FEATURE_COUNT_OFFSET))
    {
        return TRUE;
    }

    feature_defs = read_pointer(dynmem + DYN_FEATURE_DEFS_OFFSET);
    return feature_defs == NULL ||
        (feature_defs[(unsigned int)feature_id * FEATURE_RECORD_SIZE +
            FEATURE_BLOCKING_FLAG_OFFSET] & FEATURE_BLOCKING_FLAG) != 0;
}

static BOOL plot_unit_blocks(
    const BYTE *dynmem,
    const BYTE *plot,
    const BYTE *movement_class)
{
    uint16_t unit_id = read_u16(plot + PLOT_UNIT_ID_OFFSET);
    BYTE *unit_array;
    BYTE *unit_object;

    if (unit_id == 0)
        return FALSE;

    unit_array = read_pointer(dynmem + DYN_UNIT_ARRAY_OFFSET);
    if (unit_array == NULL)
        return TRUE;

    unit_object = read_pointer(
        unit_array + (unsigned int)unit_id * UNIT_ARRAY_RECORD_SIZE);
    return unit_object == NULL ||
        read_u32(unit_object + UNIT_OBJECT_CLEARANCE_OFFSET) <
            read_u32(movement_class + MOVEMENT_UNIT_CLEARANCE_OFFSET);
}

static BOOL movement_uses_bridge_surface(const BYTE *movement_class)
{
    /* Naval classes require positive water depth and remain below bridges. */
    return movement_class != NULL &&
        read_i16(movement_class + MOVEMENT_MIN_WATER_DEPTH_OFFSET) <= 0;
}

static BOOL bridge_surface_supplies_terrain(void)
{
    return !g_map_is_lava_world ||
        g_map_bridges_override_impassable_terrain;
}

static BOOL unit_definition_has_bridge_surface(const BYTE *definition)
{
    const BYTE *yardmap;
    int footprint_width;
    int footprint_height;
    int row;

    if (definition == NULL)
        return FALSE;

    footprint_width = read_i16(definition + UNITDEF_FOOTPRINT_OFFSET);
    footprint_height = read_i16(
        definition + UNITDEF_FOOTPRINT_OFFSET + 2);
    yardmap = read_pointer(definition + UNITDEF_YARDMAP_OFFSET);
    if (yardmap == NULL || footprint_width <= 0 || footprint_height <= 0)
        return FALSE;

    for (row = 0; row < footprint_height; ++row)
    {
        int column;
        for (column = 0; column < footprint_width; ++column)
        {
            if ((yardmap[row * footprint_width + column] &
                    YARDMAP_BRIDGE_CELL) != 0)
            {
                return TRUE;
            }
        }
    }

    return FALSE;
}

static BOOL unit_is_on_bridge_surface(
    const BYTE *unit,
    const BYTE *definition)
{
    BYTE *dynmem;
    BYTE *plot_map;
    int map_width;
    int map_height;
    int plot_x;
    int plot_z;
    uint32_t definition_flags;

    if (!g_bridge_traversal_enabled ||
        !bridge_surface_supplies_terrain() ||
        unit == NULL || definition == NULL)
    {
        return FALSE;
    }

    definition_flags = read_u32(definition + UNITDEF_FLAGS_OFFSET);
    if ((definition_flags & (UNITDEF_CAN_FLY_FLAG |
            UNITDEF_CAN_HOVER_FLAG | UNITDEF_FLOATER_FLAG)) != 0 ||
        read_i16(definition + UNITDEF_MIN_WATER_DEPTH_OFFSET) > 0)
    {
        return FALSE;
    }

    dynmem = *TA_DYNMEM_POINTER_ADDRESS;
    if (dynmem == NULL)
        return FALSE;

    map_width = read_i32(dynmem + DYN_MAP_WIDTH_OFFSET);
    map_height = read_i32(dynmem + DYN_MAP_HEIGHT_OFFSET);
    plot_x = (int16_t)(read_i32(unit + UNIT_POSITION_X_OFFSET) >> 16) >> 4;
    plot_z = (int16_t)(read_i32(unit + UNIT_POSITION_Z_OFFSET) >> 16) >> 4;
    if (plot_x < 0 || plot_z < 0 ||
        plot_x >= map_width || plot_z >= map_height)
    {
        return FALSE;
    }

    plot_map = read_pointer(dynmem + DYN_PLOT_MAP_OFFSET);
    return plot_map != NULL &&
        (plot_map[(plot_z * map_width + plot_x) * PLOT_RECORD_SIZE +
            PLOT_FLAGS_OFFSET] & PLOT_BRIDGE_FLAG) != 0;
}

/*
 * Parse the new map property while TA is reading lavaworld from the same
 * [GlobalHeader] section. The wrapper returns lavaworld unchanged.
 */
static int __fastcall bridge_parse_lavaworld_option(
    BYTE *tdf_file,
    void *unused_edx,
    const char *name,
    int default_value)
{
    int lava_world;

    (void)unused_edx;

    lava_world = original_tdf_get_int(tdf_file, name, default_value);
    g_map_is_lava_world = lava_world != 0;
    g_map_bridges_override_impassable_terrain =
        original_tdf_get_int(
            tdf_file,
            "bridgesoverrideimpassableterrain",
            1) != 0;
    return lava_world;
}

/*
 * Acid-water damage is applied solely from AutoHealAndAimLoop after checking
 * the target unit against sea level. A bridge deck can span the damaging
 * liquid without touching it, so suppress only that environmental call for
 * bridge definitions and conventional ground units supported by bridge deck.
 */
static void __stdcall bridge_water_damage(
    BYTE *source_unit,
    BYTE *target_unit,
    int damage,
    int damage_type,
    int unused)
{
    BYTE *definition = target_unit == NULL ? NULL :
        read_pointer(target_unit + UNIT_DEFINITION_OFFSET);

    if (damage_type == 0x0B &&
        (unit_definition_has_bridge_surface(definition) ||
            unit_is_on_bridge_surface(target_unit, definition)))
    {
        return;
    }

    original_units_make_damage(
        source_unit,
        target_unit,
        damage,
        damage_type,
        unused);
}

static BOOL bridge_footprint_contains_surface(
    const BYTE *dynmem,
    const BYTE *movement_class,
    int x,
    int z)
{
    BYTE *plot;
    int map_width;
    int map_height;
    int footprint_width;
    int footprint_height;
    int row;

    if (dynmem == NULL ||
        !bridge_surface_supplies_terrain() ||
        !movement_uses_bridge_surface(movement_class))
        return FALSE;

    map_width = read_i32(dynmem + DYN_MAP_WIDTH_OFFSET);
    map_height = read_i32(dynmem + DYN_MAP_HEIGHT_OFFSET);
    footprint_width = read_i16(movement_class + 4);
    footprint_height = read_i16(movement_class + 6);
    if (footprint_width <= 0 || footprint_height <= 0 ||
        x < 0 || z < 0 || x + footprint_width >= map_width ||
        z + footprint_height >= map_height)
    {
        return FALSE;
    }

    plot = read_pointer(dynmem + DYN_PLOT_MAP_OFFSET);
    if (plot == NULL)
        return FALSE;
    plot += (z * map_width + x) * PLOT_RECORD_SIZE;

    for (row = 0; row < footprint_height; ++row)
    {
        int column;
        for (column = 0; column < footprint_width; ++column)
        {
            if ((plot[PLOT_FLAGS_OFFSET] & PLOT_BRIDGE_FLAG) != 0)
                return TRUE;
            plot += PLOT_RECORD_SIZE;
        }
        plot += (map_width - footprint_width) * PLOT_RECORD_SIZE;
    }

    return FALSE;
}

/*
 * TA gives both flat bridge deck and ordinary traversable water the preferred
 * packed-grid value (3). The A* search therefore has no reason to select the
 * deck for an amphibious ground unit. Identify an ordinary submerged footprint
 * immediately beside bridge deck so the query can return the still-passable
 * edge/slow value (1), which adds one orthogonal-step cost to that water cell.
 */
static BOOL ordinary_water_footprint_touches_bridge(
    const BYTE *dynmem,
    const BYTE *movement_class,
    int x,
    int z)
{
    BYTE *plot_map;
    int map_width;
    int map_height;
    int footprint_width;
    int footprint_height;
    int water_level;
    int row;

    if (dynmem == NULL ||
        !bridge_surface_supplies_terrain() ||
        !movement_uses_bridge_surface(movement_class))
    {
        return FALSE;
    }

    map_width = read_i32(dynmem + DYN_MAP_WIDTH_OFFSET);
    map_height = read_i32(dynmem + DYN_MAP_HEIGHT_OFFSET);
    footprint_width = read_i16(movement_class + 4);
    footprint_height = read_i16(movement_class + 6);
    if (footprint_width <= 0 || footprint_height <= 0 ||
        x < 0 || z < 0 || x + footprint_width >= map_width ||
        z + footprint_height >= map_height)
    {
        return FALSE;
    }

    plot_map = read_pointer(dynmem + DYN_PLOT_MAP_OFFSET);
    if (plot_map == NULL)
        return FALSE;

    water_level = dynmem[DYN_WATER_LEVEL_OFFSET];
    if (plot_map[((z + footprint_height / 2) * map_width +
            x + footprint_width / 2) * PLOT_RECORD_SIZE +
            PLOT_MIN_HEIGHT_OFFSET] >= water_level)
    {
        return FALSE;
    }

    for (row = 0; row < footprint_height; ++row)
    {
        int column;
        const BYTE *plot = plot_map +
            ((z + row) * map_width + x) * PLOT_RECORD_SIZE;

        for (column = 0; column < footprint_width; ++column)
        {
            if ((plot[PLOT_FLAGS_OFFSET] & PLOT_BRIDGE_FLAG) != 0)
                return FALSE;
            plot += PLOT_RECORD_SIZE;
        }
    }

    for (row = z - 1; row <= z + footprint_height; ++row)
    {
        int column;
        for (column = x - 1; column <= x + footprint_width; ++column)
        {
            const BYTE *plot;

            if (column < 0 || row < 0 ||
                column >= map_width || row >= map_height ||
                (column >= x && column < x + footprint_width &&
                    row >= z && row < z + footprint_height))
            {
                continue;
            }

            plot = plot_map +
                (row * map_width + column) * PLOT_RECORD_SIZE;
            if ((plot[PLOT_FLAGS_OFFSET] & PLOT_BRIDGE_FLAG) != 0)
                return TRUE;
        }
    }

    return FALSE;
}

static unsigned int __stdcall bridge_movement_cell_evaluator(
    BYTE *movement_class,
    int x,
    int z,
    int footprint_width,
    int footprint_height)
{
    BYTE *dynmem = *TA_DYNMEM_POINTER_ADDRESS;
    BYTE *plot;
    int map_width;
    int map_height;
    unsigned int result = 3;
    int row;

    if (dynmem == NULL || movement_class == NULL)
        return 0;

    map_width = read_i32(dynmem + DYN_MAP_WIDTH_OFFSET);
    map_height = read_i32(dynmem + DYN_MAP_HEIGHT_OFFSET);
    if (x < 0 || z < 0 || x + footprint_width >= map_width ||
        z + footprint_height >= map_height)
    {
        return 0;
    }

    plot = read_pointer(dynmem + DYN_PLOT_MAP_OFFSET);
    if (plot == NULL)
        return 0;
    plot += (z * map_width + x) * PLOT_RECORD_SIZE;

    for (row = 0; row < footprint_height; ++row)
    {
        int column;
        for (column = 0; column < footprint_width; ++column)
        {
            int minimum_height;
            int maximum_height;
            unsigned int slope;
            int water_level;

            if (plot_feature_blocks(dynmem, plot, map_width) ||
                plot_unit_blocks(dynmem, plot, movement_class))
            {
                return 0;
            }

            /* A bridge supplies a level, solid movement surface here. */
            if ((plot[PLOT_FLAGS_OFFSET] & PLOT_BRIDGE_FLAG) != 0 &&
                bridge_surface_supplies_terrain() &&
                movement_uses_bridge_surface(movement_class))
            {
                plot += PLOT_RECORD_SIZE;
                continue;
            }

            minimum_height = plot[PLOT_MIN_HEIGHT_OFFSET];
            maximum_height = plot[PLOT_MAX_HEIGHT_OFFSET];
            water_level = dynmem[DYN_WATER_LEVEL_OFFSET];

            if (minimum_height <
                    water_level - read_i16(
                        movement_class + MOVEMENT_MAX_WATER_DEPTH_OFFSET) ||
                maximum_height >
                    water_level - read_i16(
                        movement_class + MOVEMENT_MIN_WATER_DEPTH_OFFSET))
            {
                return 0;
            }

            slope = (BYTE)(maximum_height - minimum_height);
            if (minimum_height < water_level)
            {
                if (slope > movement_class[MOVEMENT_WATER_SOFT_SLOPE])
                {
                    if (slope > movement_class[MOVEMENT_WATER_HARD_SLOPE])
                        return 0;
                    result = 1;
                }
            }
            else if (slope > movement_class[MOVEMENT_LAND_SOFT_SLOPE])
            {
                if (slope > movement_class[MOVEMENT_LAND_HARD_SLOPE])
                    return 0;
                result = 1;
            }

            plot += PLOT_RECORD_SIZE;
        }
        plot += (map_width - footprint_width) * PLOT_RECORD_SIZE;
    }

    return result;
}

/*
 * Reproduce the incremental grid builder's four perimeter checks.  The core
 * footprint decides whether a location is blocked; an obstructed perimeter
 * only downgrades a clear location from the preferred value (3) to the
 * edge/slow value (1).
 */
static unsigned int bridge_movement_grid_cell_result(
    BYTE *movement_class,
    int x,
    int z)
{
    int footprint_width;
    int footprint_height;
    unsigned int result;

    if (movement_class == NULL)
        return 0;

    footprint_width = read_i16(movement_class + 4);
    footprint_height = read_i16(movement_class + 6);
    if (footprint_width <= 0 || footprint_height <= 0)
        return 0;

    result = bridge_movement_cell_evaluator(
        movement_class,
        x,
        z,
        footprint_width,
        footprint_height);
    if (result <= 1)
        return result;

    if (bridge_movement_cell_evaluator(
            movement_class,
            x - 1,
            z - 1,
            footprint_width + 1,
            1) != 3 ||
        bridge_movement_cell_evaluator(
            movement_class,
            x + footprint_width,
            z - 1,
            1,
            footprint_height + 1) != 3 ||
        bridge_movement_cell_evaluator(
            movement_class,
            x,
            z + footprint_height,
            footprint_width + 1,
            1) != 3 ||
        bridge_movement_cell_evaluator(
            movement_class,
            x - 1,
            z,
            1,
            footprint_height + 1) != 3)
    {
        return 1;
    }

    return 3;
}

/*
 * Ground-unit motion has a second terrain gate after route creation.  When a
 * unit crosses a plot boundary, MovingUnit? calls CanAttachUnitToPiece and
 * clamps the unit back into its previous plot if the new footprint fails the
 * unit definition's depth/slope tests.  Re-evaluate only that failed moving-
 * unit call, treating bridge plots as supported deck while retaining every
 * ordinary plot, feature, and unit-occupancy test.
 */
static unsigned int __stdcall bridge_can_attach_moving_unit(
    BYTE *unit_definition,
    int unit_id,
    uint32_t packed_plot_position,
    int occupancy_type)
{
    BYTE *dynmem;
    BYTE *plot;
    int map_width;
    int map_height;
    int x;
    int z;
    int footprint_width;
    int footprint_height;
    int row;
    BOOL saw_bridge = FALSE;
    unsigned int original_result;

    original_result = original_can_attach_unit(
        unit_definition,
        unit_id,
        packed_plot_position,
        occupancy_type);
    if (original_result != 0 ||
        unit_definition == NULL ||
        occupancy_type != 1 ||
        unit_definition[UNITDEF_TERRAIN_CHECK_OFFSET] == 0 ||
        read_i16(unit_definition + UNITDEF_MIN_WATER_DEPTH_OFFSET) > 0 ||
        !bridge_surface_supplies_terrain())
    {
        return original_result;
    }

    dynmem = *TA_DYNMEM_POINTER_ADDRESS;
    if (dynmem == NULL)
        return 0;

    map_width = read_i32(dynmem + DYN_MAP_WIDTH_OFFSET);
    map_height = read_i32(dynmem + DYN_MAP_HEIGHT_OFFSET);
    x = (int16_t)(packed_plot_position & 0xFFFFu);
    z = (int16_t)(packed_plot_position >> 16);
    footprint_width = read_i16(unit_definition + UNITDEF_FOOTPRINT_OFFSET);
    footprint_height = read_i16(
        unit_definition + UNITDEF_FOOTPRINT_OFFSET + 2);
    if (x < 0 || z < 0 ||
        footprint_width <= 0 || footprint_height <= 0 ||
        x + footprint_width >= map_width ||
        z + footprint_height >= map_height)
    {
        return 0;
    }

    plot = read_pointer(dynmem + DYN_PLOT_MAP_OFFSET);
    if (plot == NULL)
        return 0;
    plot += (z * map_width + x) * PLOT_RECORD_SIZE;

    for (row = 0; row < footprint_height; ++row)
    {
        int column;
        for (column = 0; column < footprint_width; ++column)
        {
            uint16_t occupying_unit = read_u16(
                plot + PLOT_UNIT_ID_OFFSET);

            if (plot_feature_blocks(dynmem, plot, map_width) ||
                (occupying_unit != 0 && occupying_unit != (uint16_t)unit_id))
            {
                return 0;
            }

            if ((plot[PLOT_FLAGS_OFFSET] & PLOT_BRIDGE_FLAG) != 0)
            {
                saw_bridge = TRUE;
            }
            else
            {
                int minimum_height = plot[PLOT_MIN_HEIGHT_OFFSET];
                int maximum_height = plot[PLOT_MAX_HEIGHT_OFFSET];
                int slope = maximum_height - minimum_height;
                int water_level = dynmem[DYN_WATER_LEVEL_OFFSET];

                if (minimum_height <
                        water_level - read_i16(unit_definition +
                            UNITDEF_MAX_WATER_DEPTH_OFFSET) ||
                    maximum_height >
                        water_level - read_i16(unit_definition +
                            UNITDEF_MIN_WATER_DEPTH_OFFSET))
                {
                    return 0;
                }

                if (slope > unit_definition[UNITDEF_LAND_SLOPE_OFFSET] &&
                    (minimum_height >= water_level ||
                        slope > unit_definition[
                            UNITDEF_WATER_SLOPE_OFFSET]))
                {
                    return 0;
                }
            }

            plot += PLOT_RECORD_SIZE;
        }
        plot += (map_width - footprint_width) * PLOT_RECORD_SIZE;
    }

    return saw_bridge ? 1u : 0u;
}

/*
 * The pathfinder consults this packed two-bit grid after incremental grid
 * rebuilds. Keep the original visibility and cache behavior, but recover from
 * a stale terrain-blocked value when the queried movement footprint contains
 * bridge deck, including mixed shore/deck transition positions.
 *
 * A jump from a thiscall function can target a fastcall function with an
 * unused EDX argument: ECX still carries `this`, and the two original stack
 * arguments remain in the correct positions.
 */
static unsigned int __fastcall bridge_path_grid_query(
    BYTE *pathfinder,
    void *unused_edx,
    unsigned int x,
    unsigned int z)
{
    BYTE *dynmem;
    BYTE *movement_class;
    BYTE *visibility_map;
    BYTE *packed_grid;
    int movement_width;
    int movement_height;
    int visibility_x;
    int visibility_z;
    int visibility_width;
    unsigned int result;
    unsigned int shift;

    (void)unused_edx;

    if (pathfinder == NULL)
        return 0;

    dynmem = *TA_DYNMEM_POINTER_ADDRESS;
    movement_class = read_pointer(
        pathfinder + PATHFINDER_MOVEMENT_CLASS_OFFSET);
    if (dynmem == NULL || movement_class == NULL)
        return 0;

    movement_width = read_i32(movement_class + 0x10);
    movement_height = read_i32(movement_class + 0x14);
    if (x >= (unsigned int)movement_width ||
        z >= (unsigned int)movement_height)
    {
        return 0;
    }

    visibility_x = ((int)x >> 1) +
        (read_i16(movement_class + 4) >> 2);
    visibility_z = ((int)z >> 1) +
        (read_i16(movement_class + 6) >> 2);
    visibility_width = read_i32(dynmem + DYN_MAP_WIDTH_OFFSET) >> 1;
    if (visibility_x < 0 || visibility_z < 0 ||
        visibility_x >= visibility_width ||
        visibility_z >=
            (read_i32(dynmem + DYN_MAP_HEIGHT_OFFSET) >> 1))
    {
        return 0;
    }

    visibility_map = read_pointer(dynmem + DYN_VISIBILITY_MAP_OFFSET);
    if (visibility_map == NULL)
        return 0;
    if ((read_u16(visibility_map +
            (visibility_z * visibility_width + visibility_x) * 2) &
            (1u << (pathfinder[PATHFINDER_VISIBILITY_BIT_OFFSET] & 0x1F))) == 0)
    {
        return 2;
    }

    packed_grid = read_pointer(movement_class + 0x18);
    if (packed_grid == NULL)
        return 0;
    shift = (z & 0x0F) << 1;
    result = read_u32(packed_grid +
        (((z >> 4) * (unsigned int)movement_width + x) * 4));
    result = (result >> shift) & 3;

    /*
     * A shore-to-bridge transition necessarily contains a mixture of normal
     * terrain and bridge cells.  Requiring every cell to be bridge-marked
     * makes that transition disconnected.  If this footprint contains at
     * least one bridge cell, re-evaluate every constituent cell: bridge cells
     * use the deck, while ordinary cells retain the original movement-class
     * depth, slope, feature, and unit tests.
     */
    if (result == 0 && bridge_footprint_contains_surface(
            dynmem, movement_class, (int)x, (int)z))
    {
        result = bridge_movement_grid_cell_result(
            movement_class,
            (int)x,
            (int)z);
    }

    /* Prefer deck without making the adjacent water route impassable. */
    if (result == 3 && ordinary_water_footprint_touches_bridge(
            dynmem, movement_class, (int)x, (int)z))
    {
        result = 1;
    }

    return result;
}

static void __stdcall bridge_units_fix_ypos(BYTE *unit)
{
    BYTE *definition;
    BYTE *dynmem;
    BYTE *plot_map;
    BYTE *plot;
    uint32_t definition_flags;
    int map_x;
    int map_z;
    int plot_x;
    int plot_z;
    int map_width;
    int map_height;
    int32_t original_y;
    int32_t water_y;
    int32_t deck_y;
    BOOL original_will_recompute;

    /*
     * UNITS_FixYPos is called from a periodic update path even when it has no
     * position work to do. Remember whether this invocation will consume the
     * moving-unit height-dirty bit so the deck offset cannot accumulate on
     * later no-op calls.
     */
    original_will_recompute = unit != NULL &&
        (read_u32(unit + UNIT_RUNTIME_FLAGS_OFFSET) & 0x00010000u) != 0;

    original_units_fix_ypos(unit);

    if (!original_will_recompute || unit == NULL ||
        !bridge_surface_supplies_terrain() ||
        (read_u32(unit + UNIT_RUNTIME_FLAGS_OFFSET) &
            UNIT_BUILDING_FLAG) != 0)
    {
        return;
    }

    definition = read_pointer(unit + UNIT_DEFINITION_OFFSET);
    if (definition == NULL)
        return;

    definition_flags = read_u32(definition + UNITDEF_FLAGS_OFFSET);
    if ((definition_flags & (UNITDEF_CAN_FLY_FLAG |
            UNITDEF_CAN_HOVER_FLAG | UNITDEF_FLOATER_FLAG)) != 0)
    {
        return;
    }

    dynmem = *TA_DYNMEM_POINTER_ADDRESS;
    if (dynmem == NULL)
        return;

    map_width = read_i32(dynmem + DYN_MAP_WIDTH_OFFSET);
    map_height = read_i32(dynmem + DYN_MAP_HEIGHT_OFFSET);
    map_x = (int16_t)(read_i32(unit + UNIT_POSITION_X_OFFSET) >> 16);
    map_z = (int16_t)(read_i32(unit + UNIT_POSITION_Z_OFFSET) >> 16);
    plot_x = map_x >> 4;
    plot_z = map_z >> 4;
    if (plot_x < 0 || plot_z < 0 || plot_x >= map_width ||
        plot_z >= map_height)
    {
        return;
    }

    plot_map = read_pointer(dynmem + DYN_PLOT_MAP_OFFSET);
    if (plot_map == NULL)
        return;
    plot = plot_map + (plot_z * map_width + plot_x) * PLOT_RECORD_SIZE;
    if ((plot[PLOT_FLAGS_OFFSET] & PLOT_BRIDGE_FLAG) == 0)
        return;

    /*
     * The bridge building's placement plane is supported at no lower than the
     * water surface. A deck-aligned bridge model puts its visible top at that
     * same Y origin and extends its thickness downward. Start with the
     * original terrain-derived height, clamp it to water, and place the unit
     * on that shared top plane.
     */
    original_y = read_i32(unit + UNIT_POSITION_Y_OFFSET);
    water_y = (int32_t)dynmem[DYN_WATER_LEVEL_OFFSET] << 16;
    deck_y = (original_y < water_y ? water_y : original_y) +
        ((int32_t)BRIDGE_DECK_SURFACE_OFFSET << 16);
    write_i32(unit + UNIT_POSITION_Y_OFFSET, deck_y);
    write_u16(unit + UNIT_PITCH_OFFSET, 0);
    write_u16(unit + UNIT_ROLL_OFFSET, 0);
}

#if defined(_MSC_VER)

__declspec(naked) static void yardmap_dot_or_bridge_cell(void)
{
    __asm
    {
        mov ecx, dword ptr [ebp + UNITDEF_YARDMAP_OFFSET]
        cmp byte ptr [esi], YARDMAP_EQUALS_CHAR
        jne write_dot
        mov byte ptr [ecx + eax], YARDMAP_BRIDGE_CELL
        ret

    write_dot:
        mov byte ptr [ecx + eax], 0
        ret
    }
}

#else

__attribute__((naked)) static void yardmap_dot_or_bridge_cell(void)
{
    __asm__ volatile(
        "mov ecx, dword ptr [ebp + 0x14e]\n"
        "cmp byte ptr [esi], 0x3d\n"
        "jne 1f\n"
        "mov byte ptr [ecx + eax], 0x1\n"
        "ret\n"
        "1:\n"
        "mov byte ptr [ecx + eax], 0x0\n"
        "ret\n");
}

#endif

BOOL buildable_bridges_install(
    BOOL enable_parser,
    BOOL enable_traversal,
    char *error,
    size_t error_size)
{
    static const BYTE expected_dot_handler[] = {
        0x8B, 0x8D, 0x4E, 0x01, 0x00, 0x00, 0xC6, 0x04, 0x01, 0x00
    };
    static const BYTE expected_dot_handler_jump[] = { 0xEB, 0x6A };
    static const BYTE expected_equals_case = 0x0A;
    static const BYTE expected_movement_evaluator[] = {
        0x8B, 0x54, 0x24, 0x08, 0x83, 0xEC, 0x08, 0x85,
        0xD2, 0x53, 0x55, 0x56, 0x57, 0x0F, 0x8C, 0x10
    };
    static const BYTE expected_path_grid_query[] = {
        0x51, 0x8B, 0x54, 0x24, 0x08, 0x53, 0x55, 0x56,
        0x57, 0x8B, 0x79, 0x64, 0x89, 0x4C, 0x24, 0x10
    };
    static const BYTE *fix_ypos_calls[] = {
        FIX_YPOS_CALL_CREATE_UNIT,
        FIX_YPOS_CALL_CREATE_NETWORK,
        FIX_YPOS_CALL_UPDATE_UNITS,
        FIX_YPOS_CALL_SEND_MOVE
    };
    size_t call_index;

    if (error && error_size)
        error[0] = '\0';

    if (!enable_parser && !enable_traversal)
        return TRUE;

    if (sizeof(void *) != 4u)
    {
        if (error && error_size)
            sprintf_s(error, error_size, "Buildable bridges require a 32-bit build.");
        return FALSE;
    }

    if (enable_traversal && !enable_parser)
    {
        if (error && error_size)
            sprintf_s(error, error_size,
                "BridgeTraversal=Yes requires BuildableBridges=Yes.");
        return FALSE;
    }

    if (enable_parser &&
        (memcmp(YARDMAP_DOT_HANDLER,
                expected_dot_handler,
                sizeof(expected_dot_handler)) != 0 ||
            memcmp(YARDMAP_DOT_HANDLER_JUMP,
                expected_dot_handler_jump,
                sizeof(expected_dot_handler_jump)) != 0 ||
            *YARDMAP_EQUALS_CASE != expected_equals_case))
    {
        if (error && error_size)
            sprintf_s(error, error_size,
                "The yardmap parser does not match the supported TotalA.exe. "
                "Buildable bridges were not installed.");
        return FALSE;
    }

    if (enable_parser &&
        !call_targets(WATER_DAMAGE_CALL, UNITS_MAKE_DAMAGE))
    {
        if (error && error_size)
            sprintf_s(error, error_size,
                "The water-damage call site does not match the supported "
                "TotalA.exe. Bridge acid protection was not installed.");
        return FALSE;
    }

    if (enable_traversal &&
        memcmp(MOVEMENT_CELL_EVALUATOR,
            expected_movement_evaluator,
            sizeof(expected_movement_evaluator)) != 0)
    {
        if (error && error_size)
            sprintf_s(error, error_size,
                "The movement evaluator does not match the supported "
                "TotalA.exe. Bridge traversal was not installed.");
        return FALSE;
    }

    if (enable_traversal &&
        memcmp(PATH_GRID_QUERY,
            expected_path_grid_query,
            sizeof(expected_path_grid_query)) != 0)
    {
        if (error && error_size)
            sprintf_s(error, error_size,
                "The path-grid query does not match the supported "
                "TotalA.exe. Bridge traversal was not installed.");
        return FALSE;
    }

    if (enable_traversal &&
        !call_targets(PARSE_LAVAWORLD_CALL, TDF_GET_INT))
    {
        if (error && error_size)
            sprintf_s(error, error_size,
                "The map-option call site does not match the supported "
                "TotalA.exe. Bridge traversal was not installed.");
        return FALSE;
    }

    if (enable_traversal)
    {
        if (!call_targets(
                MOVE_UNIT_CAN_ATTACH_CALL,
                CAN_ATTACH_UNIT_TO_PIECE))
        {
            if (error && error_size)
                sprintf_s(error, error_size,
                    "The moving-unit terrain call site does not match the "
                    "supported TotalA.exe. Bridge traversal was not installed.");
            return FALSE;
        }

        for (call_index = 0;
            call_index < sizeof(fix_ypos_calls) / sizeof(fix_ypos_calls[0]);
            ++call_index)
        {
            if (!call_targets(fix_ypos_calls[call_index], UNITS_FIX_YPOS))
            {
                if (error && error_size)
                    sprintf_s(error, error_size,
                        "A unit-height call site does not match the supported "
                        "TotalA.exe. Bridge traversal was not installed.");
                return FALSE;
            }
        }
    }

    g_bridge_traversal_enabled = enable_traversal;

    if (enable_parser)
    {
        (void)patch_call(
            (char *)YARDMAP_DOT_HANDLER,
            (char *)yardmap_dot_or_bridge_cell);
        patch_clear(
            (char *)YARDMAP_DOT_HANDLER + 5,
            (char)0x90,
            (char *)YARDMAP_DOT_HANDLER + sizeof(expected_dot_handler));
        (void)patch_setbyte(YARDMAP_EQUALS_CASE, 0x00);
        (void)patch_call(
            (char *)WATER_DAMAGE_CALL,
            (char *)bridge_water_damage);
    }

    if (enable_traversal)
    {
        (void)patch_call(
            (char *)PARSE_LAVAWORLD_CALL,
            (char *)bridge_parse_lavaworld_option);
        patch_ljmp(
            (char *)MOVEMENT_CELL_EVALUATOR,
            (char *)bridge_movement_cell_evaluator);
        patch_ljmp(
            (char *)PATH_GRID_QUERY,
            (char *)bridge_path_grid_query);
        (void)patch_call(
            (char *)MOVE_UNIT_CAN_ATTACH_CALL,
            (char *)bridge_can_attach_moving_unit);
        for (call_index = 0;
            call_index < sizeof(fix_ypos_calls) / sizeof(fix_ypos_calls[0]);
            ++call_index)
        {
            (void)patch_call(
                (char *)fix_ypos_calls[call_index],
                (char *)bridge_units_fix_ypos);
        }
    }

    FlushInstructionCache(
        GetCurrentProcess(),
        NULL,
        0);

    return TRUE;
}
