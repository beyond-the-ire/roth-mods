#ifndef HWR_COMPOSITE_H
#define HWR_COMPOSITE_H

#include "hwr_world.h"
#include "roth_sdk.h"

#include <stddef.h>
#include <stdint.h>

struct hwr_composite {
    uint8_t *engine_base;
    size_t engine_base_capacity;
    uint8_t *engine_shadow;
    size_t engine_shadow_capacity;
    uint32_t *engine_source_x;
    size_t engine_source_x_capacity;
    uint32_t *engine_source_y;
    size_t engine_source_y_capacity;
    uint16_t *palette_cube;
    uint32_t palette_cube_palette[256];
    int palette_cube_valid;
    uint32_t *effect_pixels;
    size_t effect_capacity;
    uint32_t *source_pixels;
    size_t source_capacity;
    uint32_t source_w;
    uint32_t source_h;
    int source_valid;
    int effect_active;
};

/* Build the viewport-sized engine-index baseline corresponding to the submitted
 * RGBA world, write it into the engine framebuffer before chrome is drawn, and
 * attach it to frame for host copy-in. */
int hwr_composite_prepare(struct hwr_composite *composite,
                          const struct roth_api_v1 *api,
                          struct roth_world_frame_v1 *frame,
                          const struct hwr_world *world);

/* Attach a baseline that no live engine pixel can equal, so the host draws the
 * engine layer opaquely over the whole viewport. The submitted world then only
 * supplies the canvas outside the engine's own picture, which is what keeps
 * the canvas stable while the engine owns the content it is drawing. */
int hwr_composite_engine_shadow(struct hwr_composite *composite,
                                const struct roth_api_v1 *api,
                                struct roth_world_frame_v1 *frame);

/* Carry the engine's grayscale remap into the attached index baseline, which
 * the engine has just applied to its own framebuffer. The world image itself
 * follows through the presentation palette, not through a pixel pass. */
int hwr_composite_grayscale_baseline(struct hwr_composite *composite,
                                     const uint8_t lut[256],
                                     const struct roth_world_frame_v1 *frame);

/* Scale a frozen world frame by the engine's VGA fade multiplier. The original
 * RGBA pixels are captured lazily on the first step and reused for the complete
 * fade so consecutive steps never compound rounding. */
int hwr_composite_apply_brightness(
    struct hwr_composite *composite,
    struct roth_world_frame_v1 *frame,
    uint32_t multiplier);

/* Map a submitted-world sample into the engine framebuffer region drawn over
 * it. Returns zero when a centered wide-canvas sample lies outside that layer. */
int hwr_composite_source_to_engine(const struct roth_api_v1 *api,
                                   const struct roth_world_frame_v1 *frame,
                                   uint32_t source_x, uint32_t source_y,
                                   uint32_t *engine_x, uint32_t *engine_y);

/* Build the palette the world image is presented through, from the engine's
 * live palette plus whatever palette-level effects are in force, and publish it
 * as the reference every consumer of the submitted pixels shares. Passing a
 * grayscale table folds the engine's index remap into the same table, so an
 * effect never has to be run over the pixels twice. rgba_out, when given,
 * receives the 256-entry RGBA table the renderer resolves through. */
void hwr_composite_publish_palette(const uint8_t palette_rgb[768],
                                   const uint8_t *grayscale_lut,
                                   int32_t damage_level,
                                   uint8_t rgba_out[1024]);

/* The published palette entry for an engine index. */
uint32_t hwr_composite_present_argb(unsigned index);

uint8_t hwr_composite_palette_index(uint32_t pixel,
                                    const uint32_t palette[256]);

void hwr_composite_clear(struct hwr_composite *composite);

#endif
