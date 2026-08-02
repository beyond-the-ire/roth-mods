#ifndef HWR_WORLD_H
#define HWR_WORLD_H

#include "roth_sdk.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct hwr_sector {
    uint8_t raw[0x1a];
    uint16_t section_offset;
    int16_t ceiling_z;
    int16_t floor_z;
    int16_t ceiling_flat_id;
    int16_t floor_flat_id;
    uint8_t flags;
    uint8_t light;
    int8_t texture_map_override;
    uint8_t wall_count;
    uint16_t wall_offset;
    uint16_t ceiling_pan;
    uint16_t floor_pan;
    int16_t sentinel;
    uint16_t texture_flip_flags;
    uint16_t slab_offset;
    size_t first_wall;
    size_t slab_index;
};

struct hwr_wall {
    uint8_t raw[0x0c];
    uint8_t mapping_raw[0x0c];
    uint16_t section_offset;
    uint16_t vertex_a_offset;
    uint16_t vertex_b_offset;
    uint16_t mapping_offset;
    int16_t extent;
    uint16_t middle_id;
    uint16_t upper_id;
    uint16_t lower_id;
    uint8_t mapping_flags;
    uint8_t scale;
    uint8_t pan_u;
    uint8_t pan_v;
    uint16_t portal_link_offset;
    uint16_t neighbor_sector_offset;
    size_t sector_index;
    size_t neighbor_sector_index;
    size_t vertex_a_index;
    size_t vertex_b_index;
};

struct hwr_vertex {
    uint8_t raw[0x0c];
    uint16_t section_offset;
    int16_t world_x;
    int16_t world_y;
};

struct hwr_slab {
    uint8_t raw[0x0d];
    uint16_t section_offset;
    uint16_t lower_surface_id;
    int16_t lower_z;
    uint16_t lower_aux;
    uint16_t upper_surface_id;
    int16_t upper_z;
    uint16_t upper_aux;
    uint8_t flags;
};

#define HWR_DOOR_MAX 6u
#define HWR_DOOR_SURFACE_COUNT 4u

enum hwr_world_refresh_flags {
    HWR_WORLD_REFRESH_TEXTURES = 1u << 0
};

struct hwr_door_surface {
    int16_t texture_id;
    uint16_t extent;
    uint16_t vertical_extent;
    uint16_t draw_flags;
    uint8_t shade_mode;
    uint8_t scale;
};

/* One post-tick copy of a primary hinged-door record. Pool records compact
 * when a door closes, so the renderer retains values only, never live pointers. */
struct hwr_door {
    int16_t corner_x[4];
    int16_t corner_y[4];
    struct hwr_door_surface surfaces[HWR_DOOR_SURFACE_COUNT];
    uint32_t record_ptr;
    uint16_t owner_sector_offset;
    uint16_t neighbor_sector_offset;
    size_t owner_sector_index;
    size_t neighbor_sector_index;
    uint8_t state_flags;
    int8_t angle;
};

struct hwr_luts {
    uint8_t palette[768];
    uint8_t colormap[0x2000];
    uint8_t tint[0x2000];
    uint8_t glow[0x200];
    uint8_t blend[0x10000];
    /* The pain accumulator is a signed dword the engine clamps to 0x164
     * (apply_direct_damage_to_player, weapon_combat.c:34-38), so a heavy hit
     * carries it far past a byte and the low byte alone reads as a light one. */
    int32_t damage_flash_level;
    uint16_t shade_level;
    uint16_t tint_shade_level;
    /* Per-map solid-fill colour word (g_das_palette_remap_prefix, loaded from the
     * DAS file right after the palette). Its low byte is the flat palette index
     * the engine's all-corners-saturated wall block solid-fills with. */
    uint16_t fill_index;
    /* The map's own word about how strong its lantern was meant to be, taken
     * from the metadata tail every map carries (init_map_lighting_from_metadata
     * and its startup twin copy it into a global on every load, and nothing in
     * the game reads it back). Zero means this map has no usable word, which is
     * what a light reading it as an authoring hint treats as no hint at all. */
    uint16_t candle_glow;
    int valid;
};

struct hwr_world {
    struct hwr_sector *sectors;
    struct hwr_wall *walls;
    struct hwr_vertex *vertices;
    struct hwr_slab *slabs;
    uint16_t *sector_offset_to_index;
    uint16_t *vertex_offset_to_index;
    size_t sector_count;
    size_t wall_count;
    size_t vertex_count;
    size_t slab_count;
    size_t portal_count;
    /* Static wall/vertex topology index (CSR): for vertex v, the walls incident
     * to it are vertex_wall_indices[vertex_wall_offsets[v] .. offsets[v+1]).
     * Built once per snapshot; it depends only on the map, not the camera, so
     * the per-frame mesh build looks up a wall's endpoint neighbours instead of
     * rescanning every wall.  Walls are listed in ascending index order. */
    size_t *vertex_wall_offsets;
    uint32_t *vertex_wall_indices;
    /* Map sections live in engine heap allocations outside the plugin accessor
     * window; the engine publishes their addresses in globals, and the plugin
     * shares the process address space, so they are read through these pointers. */
    const uint8_t *sector_section;
    const uint8_t *vertex_section;
    uint16_t first_vertex_offset;
    uint16_t world_span_colorkey;
    uint16_t sky_fat_index;
    /* Union of every live world texture id observed since this map snapshot.
     * The scene upload uses the union so a scripted material can switch back
     * and forth without rebuilding the GPU texture store every frame. */
    uint8_t texture_refs[0x1600 / 8];
    struct hwr_door doors[HWR_DOOR_MAX];
    size_t door_count;
    struct hwr_luts luts;
    int valid;
    char error[192];
};

/* Rebuilds all mod-owned geometry arrays from the currently loaded map. */
int hwr_world_snapshot(struct hwr_world *world, const struct roth_api_v1 *api);

/* Refreshes mutable geometry, material ids, and surface state. Returns a
 * hwr_world_refresh_flags mask, zero when unchanged, or -1 on failure. */
int hwr_world_refresh(struct hwr_world *world, const struct roth_api_v1 *api);

/* Copies the engine-staged palette and color lookup tables into mod-owned memory. */
int hwr_world_snapshot_luts(struct hwr_world *world, const struct roth_api_v1 *api);

/* Re-reads all color tables and reports whether they changed since the
 * snapshot (1 = changed and updated, 0 = identical, -1 = failure). */
int hwr_world_resnapshot_luts(struct hwr_world *world,
                              const struct roth_api_v1 *api);

/* Refreshes the small per-frame palette-effect and map shade inputs. */
int hwr_world_refresh_lut_state(struct hwr_world *world,
                                const struct roth_api_v1 *api);

/* Releases all geometry arrays and resets the snapshot. */
void hwr_world_clear(struct hwr_world *world);

/* Prints geometry, texture-reference, coordinate, and mapping statistics. */
void hwr_world_summary(const struct hwr_world *world, FILE *file);

const char *hwr_world_error(const struct hwr_world *world);

/* Count and indexed accessors return zero/NULL when the snapshot is absent. */
size_t hwr_world_sector_count(const struct hwr_world *world);
size_t hwr_world_wall_count(const struct hwr_world *world);
size_t hwr_world_vertex_count(const struct hwr_world *world);
size_t hwr_world_slab_count(const struct hwr_world *world);
const struct hwr_sector *hwr_world_sector(const struct hwr_world *world, size_t index);
const struct hwr_wall *hwr_world_wall(const struct hwr_world *world, size_t index);
const struct hwr_vertex *hwr_world_vertex(const struct hwr_world *world, size_t index);
const struct hwr_slab *hwr_world_slab(const struct hwr_world *world, size_t index);

#endif
