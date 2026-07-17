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
    uint8_t wall_count;
    uint16_t wall_offset;
    uint16_t ceiling_pan;
    uint16_t floor_pan;
    int16_t sentinel;
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

struct hwr_luts {
    uint8_t palette[768];
    uint8_t colormap[0x2000];
    uint8_t tint[0x2000];
    uint8_t glow[0x200];
    uint8_t blend[0x10000];
    uint8_t damage_flash_level;
    uint16_t shade_level;
    uint16_t tint_shade_level;
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
    /* Map sections live in engine heap allocations outside the plugin accessor
     * window; the engine publishes their addresses in globals, and the plugin
     * shares the process address space, so they are read through these pointers. */
    const uint8_t *sector_section;
    const uint8_t *vertex_section;
    uint16_t first_vertex_offset;
    uint16_t world_span_colorkey;
    struct hwr_luts luts;
    int valid;
    char error[192];
};

/* Rebuilds all mod-owned geometry arrays from the currently loaded map. */
int hwr_world_snapshot(struct hwr_world *world, const struct roth_api_v1 *api);

/* Refreshes mutable heights, sector bytes, slab heights, and vertex world coordinates. */
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
