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
    HWR_SURFACE_TRANSLUCENT  = 1u << 5,
    /* A screen-space sky quad emitted directly above a sky wall's top edge. It
     * carries HWR_GL_MATERIAL_SKY so the geometry fragment shader samples the
     * parallax skybox. Sky caps are streamed into their own range and drawn in
     * a depth-off background pass before opaque geometry, which then overwrites
     * the sky where a nearer surface projects, matching the engine. */
    HWR_SURFACE_SKY_CAP      = 1u << 6,
    /* Set on a sky cap standing on the wall's TOPMOST edge — the one place sky
     * genuinely begins. Only these may act as depth occluders: a cap emitted
     * above a lower part (a sill beneath an arched opening) has its base below
     * that opening, so occluding with it would hide what is seen through the
     * opening. Colour coverage still uses every cap. */
    HWR_SURFACE_SKY_OCCLUDER = 1u << 7
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
    /* Reused wall-edge cut scratch (cuts_a | cuts_b), sized once per build to
     * hold the worst-case cut count for the current map instead of a fresh
     * allocation per emitted wall. */
    int16_t *cut_scratch;
    size_t cut_scratch_capacity;
    int valid;
    char error[192];
};

enum hwr_animation_kind {
    /* Frames stored as a base image plus a chain of difference streams. The
     * block carries its own step rate and advances as the world is drawn. */
    HWR_ANIMATION_DELTA = 0,
    /* Frames stored as independent run-length encoded sub-images, selected
     * outright from the global animation clock. */
    HWR_ANIMATION_SUBIMAGE
};

/* One animated texture's playback state. The renderer runs its own clocks off
 * the engine's global timers rather than reading the engine's per-block
 * animation state, so a texture keeps time whether or not the engine drew a
 * surface that uses it. */
struct hwr_scene_animation {
    /* frame_count images of frame_bytes each, packed back to back. */
    uint8_t *frames;
    uint32_t frame_bytes;
    /* Where this texture's pixels sit in the uploaded texture store. */
    uint32_t offset;
    uint16_t frame_count;
    uint16_t frame_index;
    uint16_t uploaded_index;
    /* Sub-image: clock ticks each frame is held for. */
    uint16_t divisor;
    /* Delta: the render tick this texture was last stepped on, so a texture
     * used by many surfaces still advances only once per frame. */
    uint16_t last_tick;
    /* Delta: the block's step-rate byte and its running frame-time countdown. */
    uint8_t rate;
    uint8_t timer;
    uint8_t kind;
    uint8_t started;
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
    /* Vertex counts of the depth-sorted translucent surfaces, in draw order,
     * so the renderer can refresh its destination snapshot between them. */
    uint32_t *translucent_runs;
    size_t translucent_run_capacity;
    struct hwr_scene_animation *animations;
    size_t animation_count;
    uint32_t sky_texture_offset;
    uint32_t sky_projection_offset;
    uint16_t sky_source_offset;
    uint16_t sky_texture_height;
    uint8_t sky_row_bias;
    uint8_t sky_valid;
    /* Portal-reachable visibility (HWR_PVS). visible[sector] is 1 when the
     * camera's flood reached that sector; visible_count is the allocated length
     * and pvs_valid gates the cull (0 = every surface passes, the plain
     * stream-everything behaviour). */
    uint8_t *visible;
    size_t visible_count;
    int pvs_valid;
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
