#include "hwr_thumbnail.h"
#include "hwr_composite.h"

#include <stddef.h>
#include <stdint.h>

#define VA_G_SCREEN_PITCH  0x85498u
#define VA_G_SCREEN_HEIGHT 0x854a0u

/* Match the present channel's engine-baseline-over-world composition before
 * reducing the frame. Every engine palette index, including zero, is opaque
 * when it differs from the untouched baseline. */
static uint8_t hwr_thumbnail_composite_sample(
    const struct roth_api_v1 *api,
    const struct roth_world_frame_v1 *frame,
    const uint32_t *world_pixels, uint32_t world_x, uint32_t world_y,
    const uint32_t palette[256], const uint8_t *engine, uint32_t engine_pitch,
    uint32_t viewport_x, uint32_t viewport_y)
{
    uint8_t world = hwr_composite_palette_index(
        world_pixels[(size_t)world_y * frame->w + world_x], palette);
    uint32_t engine_x;
    uint32_t engine_y;
    uint8_t chrome;

    if (!hwr_composite_source_to_engine(
            api, frame, world_x, world_y, &engine_x, &engine_y))
        return world;
    chrome = engine[(size_t)engine_y * engine_pitch + engine_x];
    if (engine_x < viewport_x || engine_x >= viewport_x + frame->vw ||
        engine_y < viewport_y || engine_y >= viewport_y + frame->vh)
        return chrome;
    if (frame->engine_base_pixels != NULL) {
        uint8_t baseline = frame->engine_base_pixels[
            (size_t)(engine_y - viewport_y) *
                frame->engine_base_pitch_bytes +
            (engine_x - viewport_x)];
        return chrome == baseline ? world : chrome;
    }
    return chrome == (uint8_t)frame->transparent_index ? world : chrome;
}

int hwr_thumbnail_capture(const struct roth_api_v1 *api,
                          const struct roth_world_frame_v1 *frame,
                          const struct hwr_world *world,
                          uint32_t dst, uint32_t src, uint32_t lut)
{
    const uint32_t *source;
    const uint8_t *engine;
    const uint8_t *blend;
    uint8_t *output;
    uint32_t palette[256];
    uint32_t pitch;
    uint32_t screen_height;
    uint32_t viewport_x;
    uint32_t viewport_y;
    uint32_t hstep;
    uint32_t vstep;
    uint32_t yacc = 0;
    uint32_t y;
    unsigned i;

    if (api == NULL || api->game_ram == NULL || frame == NULL ||
        world == NULL || !world->valid || !world->luts.valid ||
        frame->pixels == NULL || frame->w < 2u || frame->h < 2u ||
        frame->vx < 0 || frame->vy < 0 || frame->vw == 0 || frame->vh == 0 ||
        frame->transparent_index > 255u ||
        dst < 0x1000u || src < 0x1000u || lut < 0x1000u)
        return 0;
    pitch = api->game_ram->u32(VA_G_SCREEN_PITCH);
    screen_height = api->game_ram->u32(VA_G_SCREEN_HEIGHT);
    viewport_x = (uint32_t)frame->vx;
    viewport_y = (uint32_t)frame->vy;
    if (pitch == 0 || pitch > 4096u || screen_height == 0 ||
        screen_height > 4096u ||
        viewport_x >= pitch || frame->vw > pitch - viewport_x ||
        viewport_y >= screen_height ||
        frame->vh > screen_height - viewport_y)
        return 0;
    source = (const uint32_t *)frame->pixels;
    engine = (const uint8_t *)(uintptr_t)src;
    blend = (const uint8_t *)(uintptr_t)lut;
    output = (uint8_t *)(uintptr_t)dst;
    for (i = 0; i < 256u; i++)
        palette[i] = hwr_composite_palette_argb(world, i);

    output[0] = 2u;
    output[1] = 0u;
    output[2] = 0u;
    output[3] = 0u;
    output[4] = 0x4eu;
    output[5] = 0u;
    output[6] = 0x38u;
    output[7] = 0u;
    output += 8;

    hstep = ((frame->w - 1u) << 16) / 0x4eu;
    vstep = ((frame->h - 1u) << 16) / 0x38u;
    for (y = 0; y < 0x38u; y++) {
        uint32_t source_y = yacc >> 16;
        uint32_t xacc = 0;
        uint32_t x;
        if (source_y >= frame->h - 1u)
            source_y = frame->h - 2u;
        for (x = 0; x < 0x4eu; x++) {
            uint32_t source_x = xacc >> 16;
            uint8_t top_mix;
            uint8_t bottom_mix;
            uint8_t i00;
            uint8_t i01;
            uint8_t i10;
            uint8_t i11;

            if (source_x >= frame->w - 1u)
                source_x = frame->w - 2u;
            i00 = hwr_thumbnail_composite_sample(
                api, frame, source, source_x, source_y, palette, engine, pitch,
                viewport_x, viewport_y);
            i01 = hwr_thumbnail_composite_sample(
                api, frame, source, source_x + 1u, source_y, palette, engine,
                pitch,
                viewport_x, viewport_y);
            i10 = hwr_thumbnail_composite_sample(
                api, frame, source, source_x, source_y + 1u, palette, engine,
                pitch,
                viewport_x, viewport_y);
            i11 = hwr_thumbnail_composite_sample(
                api, frame, source, source_x + 1u, source_y + 1u, palette,
                engine, pitch, viewport_x, viewport_y);
            top_mix = blend[(uint32_t)i01 << 8 | i00];
            bottom_mix = blend[(uint32_t)i11 << 8 | i10];
            *output++ = blend[(uint32_t)top_mix << 8 | bottom_mix];
            xacc += hstep;
        }
        yacc += vstep;
    }
    return 1;
}
