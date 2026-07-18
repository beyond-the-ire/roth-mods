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
    HWR_GL_MATERIAL_HFLIP       = 1u << 5
};

enum hwr_gl_wall_wrap_flags {
    HWR_GL_WALL_WRAP_ALONG    = 1u << 0,
    HWR_GL_WALL_WRAP_VERTICAL = 1u << 1
};

struct hwr_gl_vertex {
    int16_t x;
    int16_t y;
    int16_t z;
    /* Packed wall endpoint: shade row in the high byte and interpolation
     * fraction in the low byte. It is zero for non-wall geometry. */
    uint16_t wall_shade_endpoint;
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
    int16_t shade_origin_x;
    int16_t shade_origin_y;
    uint8_t shade_level;
    uint8_t tint_shade_level;
    uint8_t global_light_bias;
    uint8_t glow_phase;
    uint16_t flat_edge_bias;
    float shade_depth_scale;
};

int hwr_gl_init(void);
const uint32_t *hwr_gl_render_frame(uint32_t width, uint32_t height,
                                    uint32_t color);
void hwr_gl_resize(uint32_t width, uint32_t height);
int hwr_gl_scene_upload(const uint8_t *texture_bytes, size_t texture_size,
                        const uint8_t colormap[0x2000],
                        const uint8_t tint[0x2000],
                        const uint8_t glow[0x200],
                        const uint8_t blend[0x10000]);
const uint32_t *hwr_gl_scene_render(const struct hwr_gl_vertex *vertices,
                                    size_t opaque_vertices,
                                    size_t translucent_vertices,
                                    const struct hwr_gl_camera *camera,
                                    const uint8_t palette_rgba[1024],
                                    uint32_t width, uint32_t height);
void hwr_gl_scene_clear(void);
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
