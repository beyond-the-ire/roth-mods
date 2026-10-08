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
    /* The enhanced plane's cube: the same grid with every empty cell filled
     * with the nearest palette entry, keyed on the palette WITHOUT the engine's
     * palette-level effects so a damage flash never invalidates it. It is read
     * with no verify and no fallback -- one fetch per sample, the same cost the
     * exact-match read has -- because a true-colour canvas holds colours the
     * palette has no entry for. */
    uint16_t *filled_cube;
    uint32_t filled_cube_palette[256];
    int filled_cube_valid;
    /* Raised while the submitted canvas may hold a colour the palette has no
     * entry for, which is what decides which of the two reads is right. */
    int enhanced;
    /* The read hwr_composite_quantize is currently making, chosen once per
     * reduction rather than per sample. */
    int quantize_filled;
    /* What the baseline quantizer actually did, reported rather than left to a
     * frame-rate report: a fall into the linear scan is a defect with a cause. */
    uint64_t cube_hits;
    uint64_t cube_misses;
    uint64_t linear_scans;
    unsigned quantize_frames;
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

/* Where a rectangle of the engine framebuffer lies on the submitted image, in
 * that image's pixels. The part of the rectangle outside the frame's viewport
 * is left out first: only there does the image show through the engine layer.
 * Returns zero when nothing of it is left. */
int hwr_composite_engine_rect_to_source(const struct roth_api_v1 *api,
                                        const struct roth_world_frame_v1 *frame,
                                        int32_t x, int32_t y,
                                        uint32_t w, uint32_t h,
                                        int32_t *source_x, int32_t *source_y,
                                        uint32_t *source_w, uint32_t *source_h);

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

/* The same table without the palette-level effects: the colours the map
 * authored, which is what the true-colour path composes in. */
void hwr_composite_base_rgba(uint8_t rgba_out[1024]);

/* Whether the engine's desaturation table is the one its own published rule
 * produces from the palette in force. It is built once per session from
 * whatever palette was live then, so on a later map it can describe a palette
 * that has since been replaced -- and a transform derived from the colour
 * cannot reproduce a table that does not describe the colours. Saying so is
 * what keeps the held image exact: the caller falls back to the palette-index
 * resolve, which reproduces the engine's own table by construction. */
int hwr_composite_grayscale_reproducible(const uint8_t lut[256]);

uint8_t hwr_composite_palette_index(uint32_t pixel,
                                    const uint32_t palette[256]);

/* Says whether the canvas being submitted may hold colours the palette has no
 * entry for, which decides how the index baseline is quantized. */
void hwr_composite_set_enhanced(struct hwr_composite *composite, int enhanced);

/* Chooses the read that describes the image about to be reduced, once, before
 * the samples start arriving. */
void hwr_composite_quantize_begin(struct hwr_composite *composite);

/* One canvas pixel to the engine index that stands for it, through whichever
 * cube is current. Everything that reduces the submitted image shares it, so
 * nothing is left holding the two-hundred-and-fifty-six-entry scan. */
uint8_t hwr_composite_quantize(struct hwr_composite *composite,
                               uint32_t pixel);

void hwr_composite_clear(struct hwr_composite *composite);

#endif
