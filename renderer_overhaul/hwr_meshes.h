#ifndef HWR_MESHES_H
#define HWR_MESHES_H

#include "hwr_das.h"
#include "hwr_gl.h"
#include "hwr_objects.h"
#include "hwr_world.h"

#include <stddef.h>
#include <stdint.h>

struct hwr_sector_xspan;

enum hwr_mesh_face_kind {
    HWR_MESH_FACE_TRIANGLE = 0,
    HWR_MESH_FACE_QUAD,
    HWR_MESH_FACE_PIN
};

struct hwr_mesh_local_vertex {
    int16_t x;
    int16_t vertical;
    int16_t y;
};

struct hwr_mesh_face {
    uint16_t texture_id;
    uint16_t extent_u;
    uint16_t extent_v;
    uint16_t flags;
    uint16_t vertex[4];
    uint32_t material_index;
    uint8_t packed_control[4];
    uint8_t packed_a[4];
    uint8_t packed_b[4];
    uint8_t subtexture;
    uint8_t scale;
    uint8_t shade_seed;
    uint8_t kind;
};

struct hwr_mesh_resource {
    size_t first_vertex;
    size_t vertex_count;
    size_t first_face;
    size_t face_count;
    uint8_t present;
};

struct hwr_mesh_material {
    uint32_t base_offset;
    uint32_t frame_bytes;
    size_t entry_id;
    uint16_t logical_id;
    uint16_t width;
    uint16_t height;
    uint16_t qflags;
    uint16_t frame_count;
    uint16_t frame_index;
    uint16_t last_tick;
    int16_t origin_x;
    int16_t origin_y;
    uint8_t source;
    uint8_t subtexture;
    uint8_t child_flip;
    uint8_t rate;
    uint8_t timer;
    uint8_t animation_started;
};

struct hwr_meshes {
    struct hwr_mesh_resource *map_dir;
    struct hwr_mesh_resource *shared_dir;
    size_t map_dir_count;
    size_t shared_dir_count;
    struct hwr_mesh_local_vertex *vertices;
    size_t vertex_count;
    size_t vertex_capacity;
    struct hwr_mesh_face *faces;
    size_t face_count;
    size_t face_capacity;
    struct hwr_mesh_material *materials;
    size_t material_count;
    size_t material_capacity;
    struct hwr_gl_vertex *opaque;
    size_t opaque_count;
    size_t opaque_capacity;
    struct hwr_gl_vertex *translucent;
    size_t translucent_count;
    size_t translucent_capacity;
    uint32_t *runs;
    size_t run_count;
    size_t run_capacity;
    struct hwr_das_archive *map_archive;
    struct hwr_das_archive *shared_archive;
    size_t stat_resources;
    size_t stat_objects;
    size_t stat_faces;
    size_t stat_pins;
    size_t stat_culled;
    size_t stat_atlas_miss;
    size_t stat_invalid;
    int built;
    char error[192];
};

int hwr_meshes_enabled(void);

int hwr_meshes_pack(struct hwr_meshes *meshes,
                    struct hwr_das_archive *map_archive,
                    struct hwr_das_archive *shared_archive,
                    uint8_t **bytes, size_t *size, size_t *capacity);

int hwr_meshes_emit(struct hwr_meshes *meshes,
                    const struct hwr_objects *objects,
                    const struct hwr_world *world,
                    const struct hwr_gl_camera *camera,
                    const uint8_t *visible, size_t visible_count,
                    int pvs_valid,
                    const struct hwr_sector_xspan *spans, size_t span_count,
                    const struct roth_api_v1 *api);

void hwr_meshes_clear(struct hwr_meshes *meshes);
const char *hwr_meshes_error(const struct hwr_meshes *meshes);

#endif
