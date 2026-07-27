#ifndef HWR_COMPOSITE_H
#define HWR_COMPOSITE_H

#include "hwr_world.h"
#include "roth_sdk.h"

#include <stddef.h>
#include <stdint.h>

struct hwr_composite {
    uint8_t *engine_base;
    size_t engine_base_capacity;
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

/* Apply the engine's already-built grayscale LUT to both the wide RGBA world
 * and its engine-index baseline. The caller resubmits the updated frame. */
int hwr_composite_grayscale(struct hwr_composite *composite,
                            const struct roth_api_v1 *api,
                            struct roth_world_frame_v1 *frame,
                            const struct hwr_world *world);

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

uint32_t hwr_composite_palette_argb(const struct hwr_world *world,
                                    unsigned index);
uint8_t hwr_composite_palette_index(uint32_t pixel,
                                    const uint32_t palette[256]);

void hwr_composite_clear(struct hwr_composite *composite);

#endif
