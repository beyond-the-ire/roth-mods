#ifndef HWR_SCENE_H
#define HWR_SCENE_H

#include "hwr_das.h"
#include "hwr_gl.h"
#include "hwr_world.h"

#include <stddef.h>
#include <stdint.h>

enum hwr_surface_kind {
    HWR_SURFACE_SOLID_MIDDLE = 0,
    HWR_SURFACE_PORTAL_UPPER,
    HWR_SURFACE_PORTAL_LOWER,
    HWR_SURFACE_PORTAL_MIDDLE,
    HWR_SURFACE_PORTAL_SLAB,
    HWR_SURFACE_CEILING,
    HWR_SURFACE_FLOOR,
    HWR_SURFACE_SLAB_LOWER,
    HWR_SURFACE_SLAB_UPPER,
    HWR_SURFACE_KIND_COUNT
};

enum hwr_surface_flags {
    HWR_SURFACE_MASKED       = 1u << 0,
    HWR_SURFACE_DIRECT       = 1u << 1,
    HWR_SURFACE_SKY          = 1u << 2,
    HWR_SURFACE_FLAT         = 1u << 3,
    HWR_SURFACE_FACE_DOWN    = 1u << 4,
    HWR_SURFACE_TRANSLUCENT  = 1u << 5
};

enum hwr_wall_corner_role {
    HWR_WALL_CORNER_NONE   = 0,
    HWR_WALL_CORNER_END_B  = 1u << 0,
    HWR_WALL_CORNER_BOTTOM = 1u << 1
};

struct hwr_mesh_vertex {
    int16_t x;
    int16_t y;
    int16_t z;
    int16_t reserved;
    float wall_s;
    float wall_top;
};

struct hwr_mesh_surface {
    size_t first_vertex;
    size_t vertex_count;
    size_t sector_index;
    enum hwr_surface_kind kind;
    int16_t texture_id;
    uint16_t extent;
    uint16_t aux;
    uint8_t pan_u;
    uint8_t pan_v;
    uint8_t scale;
    uint8_t sector_flags;
    uint8_t light;
    uint8_t mapping_flags;
    uint8_t flags;
};

struct hwr_scene_mesh {
    struct hwr_mesh_vertex *vertices;
    struct hwr_mesh_surface *surfaces;
    size_t vertex_count;
    size_t vertex_capacity;
    size_t surface_count;
    size_t surface_capacity;
    size_t kind_counts[HWR_SURFACE_KIND_COUNT];
    uint8_t texture_refs[0x1600 / 8];
    size_t texture_ref_count;
    size_t skipped_flats;
    int valid;
    char error[192];
};

struct hwr_camera {
    int16_t view_offset_x;
    int16_t view_offset_y;
    int16_t eye_z;
    int16_t yaw_sin;
    int16_t yaw_cos;
    uint16_t focal_x;
    uint16_t focal_y;
    int32_t center_x;
    int32_t horizon;
    float pitch;
};

struct hwr_scene {
    struct hwr_scene_mesh mesh;
    struct {
        uint32_t offset;
        uint16_t width;
        uint16_t height;
        uint16_t qflags;
        uint8_t direct;
        uint8_t direct_index;
        uint8_t flip_toggle;
        /* Placeholder archive resources mark surfaces another renderer owns
         * (doors); those faces are dropped rather than filled. */
        uint8_t skip;
    } textures[0x1600];
    struct hwr_gl_vertex *stream_vertices;
    size_t stream_capacity;
    uint32_t sky_texture_offset;
    uint32_t sky_projection_offset;
    uint16_t sky_texture_height;
    uint8_t sky_row_bias;
    uint8_t sky_valid;
    int uploaded;
    int valid;
    int fallback_logged;
    char error[192];
};

int hwr_scene_build_mesh(struct hwr_scene_mesh *mesh,
                         const struct hwr_world *world, int debug);
void hwr_scene_mesh_clear(struct hwr_scene_mesh *mesh);
int hwr_scene_mesh_references(const struct hwr_scene_mesh *mesh, uint16_t id);

int hwr_scene_rebuild(struct hwr_scene *scene, const struct hwr_world *world,
                      struct hwr_das_archive *map_archive,
                      struct hwr_das_archive *shared_archive,
                      const struct roth_api_v1 *api, int debug);
const uint32_t *hwr_scene_render(struct hwr_scene *scene,
                                 const struct hwr_world *world,
                                 const struct roth_api_v1 *api,
                                 uint32_t width, uint32_t height);
void hwr_scene_clear(struct hwr_scene *scene);
const char *hwr_scene_error(const struct hwr_scene *scene);

#endif
