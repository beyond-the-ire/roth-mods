#ifndef HWR_GL_H
#define HWR_GL_H

#include <stdint.h>

#include <stddef.h>

enum hwr_gl_material_flags {
    HWR_GL_MATERIAL_MASKED      = 1u << 0,
    HWR_GL_MATERIAL_DIRECT      = 1u << 1,
    HWR_GL_MATERIAL_SKY         = 1u << 2,
    HWR_GL_MATERIAL_FLAT        = 1u << 3,
    HWR_GL_MATERIAL_TRANSLUCENT = 1u << 4,
    HWR_GL_MATERIAL_HFLIP       = 1u << 5,
    /* A sky cap that may occlude: it stands on its wall's topmost edge. */
    HWR_GL_MATERIAL_SKY_OCCLUDER = 1u << 6,
    /* A degenerate EDGE_MAP face whose sky column reaches both viewport edges. */
    HWR_GL_MATERIAL_SKY_FULL_HEIGHT = 1u << 7
};

enum hwr_gl_wall_wrap_flags {
    HWR_GL_WALL_WRAP_ALONG    = 1u << 0,
    HWR_GL_WALL_WRAP_VERTICAL = 1u << 1,
    /* A one-sided wall whose texture carries the block transparency bit.  The
     * engine's masked span writer leaves index-0 texels untouched and blends
     * high-bit texels with the destination pixel; behind a one-sided wall that
     * destination is never repainted, so both read as the previous frame. */
    HWR_GL_WALL_STALE_MASK    = 1u << 2,
    /* All four wd_project corner words of this (sub-)quad saturated the shade
     * ramp (== 0x1fff).  The engine's counter==4 fast path skips per-texel
     * shading and solid-fills the block with the map fill index; the shader
     * outputs u_fill_index directly for these fragments. */
    HWR_GL_WALL_SOLID_FILL    = 1u << 3
};

/* Non-wall billboard marker: the fixed-orientation quad carries an owner-wall
 * identity for the guarded redraw, but must not replace the wall's recorded
 * identity during its normal depth-tested pass. */
#define HWR_GL_WALL_OWNER_OVERLAY (1u << 7)

/* High bits of flat_mapping_flags are available to non-flat geometry. Mesh
 * faces use perspective-correct wall_s/wall_top as direct atlas coordinates,
 * bypassing the wall-column and world-plane mapping drivers. */
#define HWR_GL_MAPPING_MESH 0x80000000u
enum hwr_gl_translucent_run_flags {
    HWR_GL_TRANSLUCENT_RUN_FLOOR_EXCEPTION = 1u << 0
};

struct hwr_gl_vertex {
    int16_t x;
    int16_t y;
    int16_t z;
    /* Packed wall endpoint: shade row in the high byte and interpolation
     * fraction in the low byte.  For a wall this is the BOTTOM-edge corner word
     * (screen row at the column's bottom); for a tinted flat it is the
     * per-vertex candle shade word.  Zero for non-shaded geometry. */
    uint16_t wall_shade_endpoint;
    /* Companion TOP-edge corner word (row<<8)|frac for a wall.  The tinted-wall
     * sub-row shade interpolates bilinearly between this and the bottom-edge
     * word down each column.  Zero for flats and non-shaded geometry. */
    uint16_t wall_shade_endpoint_top;
    float wall_s;
    float wall_top;
    uint32_t texture_offset;
    uint16_t texture_width;
    uint16_t texture_height;
    uint16_t aux;
    uint8_t pan_u;
    uint8_t pan_v;
    uint8_t scale;
    uint8_t sector_flags;
    uint8_t light;
    uint8_t material_flags;
    uint8_t direct_index;
    /* The original wall driver selects masked or linear sampling separately
     * for the along-wall and vertical texture axes. */
    uint8_t wall_wrap_flags;
    /* Per-face tinted-wall depth-bias adjust (0..16): shade rows the engine's
     * flags&0x40 wall-driver block subtracts from the candle bias. Zero for
     * non-wall geometry. */
    uint8_t wall_tint_adjust;
    /* Set on all three vertices of a tinted flat triangle that straddles the near
     * plane, so the shader falls back to the per-pixel candle radial rather than
     * interpolating a garbage near-clamped vertex row. Zero otherwise. */
    uint8_t flat_taint;
    /* Per-flat affine span state after the original scanline-range setup. */
    uint32_t flat_guard;
    uint32_t flat_guard_step;
    int32_t flat_guard_origin_y;
    uint32_t flat_mapping_flags;
    /* Wall-column state staged after projection and viewport clipping. */
    uint32_t wall_source_numerator;
    uint32_t wall_source_denominator;
    uint32_t wall_source_step;
    int32_t wall_denominator_step;
    uint32_t wall_top_accum;
    uint32_t wall_bottom_accum;
    int32_t wall_top_step;
    int32_t wall_bottom_step;
    int32_t wall_first_x;
    uint32_t wall_along_extent;
    uint32_t wall_vertical_extent;
    uint32_t wall_span_valid;
    int32_t wall_vertical_origin;
    uint32_t wall_vertical_seeded;
    /* Optional 16.16 framebuffer-X portal interval. Equal endpoints disable
     * the fragment gate. Dedicated fields keep flat mapping state intact. */
    uint32_t portal_clip_left;
    uint32_t portal_clip_right;
};

struct hwr_gl_camera {
    int16_t view_offset_x;
    int16_t view_offset_y;
    int16_t eye_z;
    int16_t yaw_sin;
    int16_t yaw_cos;
    uint16_t focal_x;
    uint16_t focal_y;
    int32_t center_x;
    int32_t horizon;
    /* Engine-space projection retained when the target framebuffer differs
     * from the engine viewport. Portal windows and intrinsic object sizes are
     * authored in this source space. */
    uint16_t source_focal_x;
    uint16_t source_focal_y;
    int32_t source_center_x;
    int32_t source_horizon;
    uint16_t source_width;
    uint16_t source_height;
    int16_t shade_origin_x;
    int16_t shade_origin_y;
    uint8_t shade_level;
    uint8_t tint_shade_level;
    uint8_t global_light_bias;
    uint8_t glow_phase;
    /* Low byte of the map's g_das_palette_remap_prefix: the flat palette index a
     * solid-filled (all-corners-saturated) tinted wall block outputs directly. */
    uint8_t solid_fill_index;
    uint16_t flat_edge_bias;
    float shade_depth_scale;
    uint32_t sky_texture_offset;
    uint32_t sky_projection_offset;
    uint16_t sky_source_offset;
    uint16_t sky_texture_height;
    int16_t sky_start_row;
    uint16_t sky_yaw;
    uint8_t sky_double_scanline;
    uint8_t sky_valid;
    /* Source-view metadata used while preparing alternate views.  The GL
     * projection only consumes x_flip; yaw and reflected remain available to
     * the scene builder that derives each view. */
    uint16_t yaw;
    uint8_t x_flip;
    uint8_t reflected;
};

struct hwr_gl_scene_view {
    const struct hwr_gl_vertex *vertices;
    size_t sky_vertices;
    /* Primary-view mirror planes are appended after the translucent range and
     * submitted early as an invisible depth barrier. */
    size_t mirror_depth_vertices;
    size_t opaque_vertices;
    size_t flat_vertices;
    size_t translucent_vertices;
    const uint32_t *translucent_runs;
    const uint8_t *translucent_run_flags;
    size_t translucent_run_count;
    /* Opaque 3D object-mesh vertices between world faces and billboards. */
    size_t opaque_mesh_vertices;
    size_t opaque_billboard_vertices;
    size_t shadow_vertices;
    size_t shadow_run_count;
    size_t translucent_billboard_run_count;
    int floor_exception;
    struct hwr_gl_camera camera;
    /* Horizontal framebuffer interval owned by this view. Equal endpoints
     * select the full target. */
    int32_t clip_left;
    int32_t clip_right;
};

int hwr_gl_init(void);
const uint32_t *hwr_gl_render_frame(uint32_t width, uint32_t height,
                                    uint32_t color);
int hwr_gl_scene_upload(const uint8_t *texture_bytes, size_t texture_size,
                        const uint8_t colormap[0x2000],
                        const uint8_t tint[0x2000],
                        const uint8_t glow[0x200],
                        const uint8_t blend[0x10000]);
/* Rewrites a run of the uploaded texture store in place, for textures whose
 * pixels change over time. Returns 0 when the store is not ready to be
 * rewritten yet, in which case the caller should try again next frame. */
int hwr_gl_scene_update_texels(uint32_t offset, const uint8_t *bytes,
                               uint32_t size);
/* Views are composited in array order into one palette-index target, with
 * reflected views preceding the primary view. Each view gets independent
 * geometry, depth, owner-floor identity, camera state, and an optional
 * horizontal clip. The palette update, persistent-index setup, final resolve,
 * and readback occur once around the whole ordered sequence. */
const uint32_t *hwr_gl_scene_render(const struct hwr_gl_scene_view *views,
                                    size_t view_count,
                                    const uint8_t palette_rgba[1024],
                                    uint32_t width, uint32_t height);
void hwr_gl_scene_clear(void);
/* Hands over the pixels the palette-index target should start from instead of a
 * flat clear, in the same orientation as a scene readback. The next scene render
 * of matching dimensions consumes them, once. Returns 0 on failure, in which
 * case the flat clear stands. */
int hwr_gl_set_index_seed(const uint8_t *indices, uint32_t width, uint32_t height);
void hwr_gl_shutdown(void);
const char *hwr_gl_last_error(void);
unsigned hwr_gl_drain_errors(void);
/* Copies the palette-index attachment of the last scene render (row-major,
 * same orientation as the submitted frame). Returns 0 on failure. */
int hwr_gl_read_scene_indices(uint8_t *out, uint32_t width, uint32_t height);
/* Overrides the debug visualisation mode (0=off, 1=shade rows, 2=raw texel
 * bytes); -1 re-reads the environment default on the next frame. */
void hwr_gl_set_debug_vis(int mode);

#endif
