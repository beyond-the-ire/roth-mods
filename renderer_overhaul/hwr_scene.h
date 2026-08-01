#ifndef HWR_SCENE_H
#define HWR_SCENE_H

#include "hwr_billboards.h"
#include "hwr_das.h"
#include "hwr_gl.h"
#include "hwr_meshes.h"
#include "hwr_objects.h"
#include "hwr_visibility.h"
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
    HWR_SURFACE_DOOR,
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

/* World coordinates are modular int16: the only place one becomes linear is the
 * view transform's 16-bit add (renderer.c, transform_map_vertices_to_view),
 * which the vertex shader mirrors.  A sector loop may therefore be authored
 * across the wrap, so loop points are held unwrapped into a wider type; casting
 * a component back to int16 recovers the authored word exactly. */
struct hwr_point {
    int32_t x;
    int32_t y;
};

struct hwr_mesh_surface {
    size_t first_vertex;
    size_t vertex_count;
    size_t sector_index;
    /* A door can be admitted through either of its two sectors. Ordinary
     * surfaces leave this at SIZE_MAX. */
    size_t neighbor_sector_index;
    /* Raw wall-record offset for wall surfaces; UINT16_MAX for flats. Mirror
     * records identify their aperture with this exact runtime offset. */
    uint16_t wall_offset;
    uint8_t door_index;
    uint8_t door_state_flags;
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
    /* Owner-surface identity, carried on flats only (walls publish theirs
     * through the flat-span attribute, which a flat needs for its span state).
     * A flat that a mid-platform has to paint over publishes owner_identity
     * instead of the default height-derived one, and that platform's own
     * surface carries the same value in exception_identity so its depth-off
     * redraw is confined to the flat of its OWN sector. Zero on everything
     * else, which is what keeps the default identity in place. */
    uint32_t owner_identity;
    uint32_t exception_identity;
    /* A flat's own sector loop, kept beside its triangulation so the per-frame
     * emit can clip the POLYGON (which is what the engine shades the corners of)
     * rather than the triangles it was cut into. Zero-length on walls. */
    size_t first_loop_point;
    size_t loop_point_count;
};

#define HWR_REFLECTION_MAX 16u

struct hwr_scene_reflection {
    struct hwr_gl_vertex *vertices;
    size_t vertex_count;
    size_t vertex_capacity;
    uint32_t *runs;
    uint8_t *run_flags;
    size_t run_count;
    size_t run_capacity;
    struct hwr_gl_camera camera;
    size_t sky_count;
    size_t mirror_depth_count;
    size_t opaque_count;
    size_t flat_count;
    size_t translucent_count;
    size_t opaque_mesh_count;
    size_t opaque_billboard_count;
    size_t shadow_count;
    size_t shadow_run_count;
    size_t translucent_exception_run_count;
    struct hwr_gl_exception_run slab_runs[HWR_GL_SLAB_EXCEPTION_MAX];
    size_t slab_run_count;
    size_t target_sector_index;
    uint16_t wall_offset;
    uint8_t renderable;
    /* Source aperture before the reflected rasterizer's destination X-flip.
     * The mirror portal walk uses these engine screen-X columns. */
    int32_t source_clip_left;
    int32_t source_clip_right;
    int32_t clip_left;
    int32_t clip_right;
};

struct hwr_scene_mesh {
    struct hwr_mesh_vertex *vertices;
    struct hwr_mesh_surface *surfaces;
    /* Sector loops, one run per flat surface (see hwr_mesh_surface). */
    struct hwr_point *loop_points;
    size_t loop_point_count;
    size_t loop_point_capacity;
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
    uint8_t *translucent_run_flags;
    size_t translucent_run_capacity;
    /* Opaque mid-platform ranges staged this view, for the owner-identity
     * redraw. A view carrying more than the array holds keeps the extras on the
     * ordinary depth-tested path, which is what they have today. */
    struct hwr_gl_exception_run slab_runs[HWR_GL_SLAB_EXCEPTION_MAX];
    size_t slab_run_count;
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
    /* The engine's original-frustum result is retained while a wider portal
     * walk is built. It is both a continuous central-view oracle and the
     * authoritative center of the final visibility product. */
    uint8_t *oracle_visible;
    size_t oracle_visible_count;
    /* Per-object screen-X portal window(s) (HWR_PORTAL_CLIP), recovered from
     * the software renderer's type-1 worklist entries. spans is indexed like
     * the live object array; portal_clip_valid gates the clip. */
    struct hwr_sector_xspan *spans;
    size_t span_count;
    int portal_clip_valid;
    /* Type-4 primary-view face-chain window unions, indexed by live door.
     * Reflected views reuse this storage for their reconstructed sector-window
     * union. */
    struct hwr_sector_xspan door_spans[HWR_DOOR_MAX];
    int door_portal_clip_valid;
    /* The windows stock draws each sector's faces under this frame, lifted from
     * its ordered draw list's face chains. They are the bound its wall driver
     * measures the corner clip against, so the candle corner law reads them.
     * They are DATA ONLY: nothing in the emit path is positioned, clipped or
     * counted from them -- they select the columns a shade word is sampled at.
     * sector_portal_valid is 0 when nothing could be recovered, which every
     * reader takes as "the engine frame". */
    struct hwr_sector_xspan *sector_spans;
    size_t sector_span_count;
    int sector_portal_valid;
    /* Reflected walks retain every narrowed screen-X window per reached
     * sector. World geometry uses these directly; object sources receive a
     * per-object copy in spans above. */
    struct hwr_sector_xspan *reflection_spans;
    size_t reflection_span_count;
    int reflection_clip_valid;
    /* Primary-view sector windows reconstructed for a frustum that differs
     * from the engine viewport. The original center remains engine-owned;
     * these windows clip geometry newly exposed in the side regions. */
    struct hwr_sector_xspan *wide_spans;
    size_t wide_span_count;
    int wide_clip_valid;
    int wide_oracle_error_logged;
    /* Reflected subviews are rebuilt from the engine's authoritative per-frame
     * mirror list. Their streams persist as reusable scratch so drawing several
     * apertures does not allocate every frame. */
    struct hwr_scene_reflection reflections[HWR_REFLECTION_MAX];
    size_t reflection_count;
    /* Object meshes and camera-facing billboards share the scene atlas. Opaque
     * meshes follow world faces so they own coplanar painter ties. */
    struct hwr_meshes object_meshes;
    /* The camera-facing billboard layer. Its sprite frames are packed into the
     * same texel store as the world textures at upload, and its per-frame quads
     * are streamed after the world surfaces. Archive pointers are retained from
     * the rebuild so the emitter can resolve object graphic ids each frame. */
    struct hwr_billboards billboards;
    struct hwr_das_archive *map_archive;
    struct hwr_das_archive *shared_archive;
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
                      const struct hwr_objects *objects,
                      const struct roth_api_v1 *api, int debug);
const uint32_t *hwr_scene_render(struct hwr_scene *scene,
                                 const struct hwr_world *world,
                                 const struct hwr_objects *objects,
                                 const struct roth_api_v1 *api,
                                 uint32_t width, uint32_t height,
                                 uint32_t source_width,
                                 uint32_t source_height);
void hwr_scene_clear(struct hwr_scene *scene);
/* Drops the record of what the resident texture store holds, so the next
 * rebuild packs and uploads all of it. Called whenever anything the store was
 * packed from stops being trustworthy. */
void hwr_scene_store_forget(void);
const char *hwr_scene_error(const struct hwr_scene *scene);

#endif
