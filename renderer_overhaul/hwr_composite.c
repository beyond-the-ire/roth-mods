#include "hwr_composite.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define VA_G_SCREEN_PITCH       0x85498u
#define VA_G_SCREEN_HEIGHT      0x854a0u
#define VA_G_FRAMEBUFFER_PTR    0x90a98u
#define VA_G_LINE_DOUBLING_FLAG 0x90cbdu
#define VA_G_VIEW_GRAYSCALE_LUT 0x7f254u

#define HWR_PALETTE_CUBE_SIZE (64u * 64u * 64u)

struct hwr_canvas_mapping {
    double x;
    double y;
    double w;
    double h;
    uint32_t screen_w;
    uint32_t screen_h;
};

struct hwr_palette_hash {
    uint32_t palette[256];
    uint16_t slots[512];
};

static int hwr_arithmetic_shift_8(int value)
{
    return value >= 0 ? value / 256 : -((-value + 255) / 256);
}

/* The palette every consumer of the submitted world image shares: the renderer
 * resolves through it, the index baseline is quantized against it, and the save
 * thumbnail reduces through it. It is published rather than recomputed because
 * palette-level effects are live engine state, and a held image outlives the
 * render that produced it. */
static uint32_t g_present_argb[256];
static int g_present_valid;

/* Palette indices the engine writes into the framebuffer as LITERALS while it
 * draws chrome over the seeded plane.
 *
 * The presentation layer can only infer "this pixel is still world" by finding
 * the live engine pixel unchanged from the plane the mod seeded there. That is
 * a necessary condition, not a sufficient one: a chrome pixel written with the
 * same index the mod happened to seed is indistinguishable from an untouched
 * one, and the full-resolution world is revealed through opaque chrome.
 *
 * Index 0 is the whole set, and it is the dominant one by construction. Popup
 * and message-box interiors are a per-row memset of literal 0
 * (clear_framebuffer_rect, renderer.c), and the held-item icon writes a literal
 * 0 outline byte on both sides of every drawn run
 * (draw_translucent_icon_spans, blit_2d.c). Index 0 is also what the world's
 * black seam texels between texture cells quantize to, so the
 * misclassification is STRUCTURED -- it traces those seams as lines rather
 * than speckling -- which is exactly the artifact this avoids. */
static const uint8_t g_reserved_indices[] = { 0u };

/* index -> the index the baseline may emit in its place */
static uint8_t g_present_substitute[256];

static int hwr_index_reserved(unsigned index)
{
    unsigned i;
    for (i = 0; i < sizeof g_reserved_indices; i++)
        if (g_reserved_indices[i] == index)
            return 1;
    return 0;
}

/* One 6-bit palette step per channel, in the published 0..252 scale. A
 * substitute farther away than this would visibly recolour the plane the
 * engine's read-modify-write chrome effects blend against, so beyond it the
 * reserved index is kept and the artifact is preferred to a wrong colour. */
#define HWR_SUBSTITUTE_TOLERANCE 48u

/* Steer the baseline away from the reserved indices without changing the colour
 * it stands for. Every retail palette carries index 0 several times over, so on
 * stock data an exact duplicate is always found and the substitution is
 * colour-exact; the tolerance only decides what third-party data gets. */
static void hwr_composite_build_substitute(void)
{
    unsigned r;
    unsigned i;

    for (i = 0; i < 256u; i++)
        g_present_substitute[i] = (uint8_t)i;
    for (r = 0; r < sizeof g_reserved_indices; r++) {
        unsigned reserved = g_reserved_indices[r];
        uint32_t target = g_present_argb[reserved];
        uint32_t best_distance = UINT32_MAX;
        unsigned best = reserved;
        int tr = (int)((target >> 16) & 0xffu);
        int tg = (int)((target >> 8) & 0xffu);
        int tb = (int)(target & 0xffu);
        for (i = 0; i < 256u; i++) {
            int dr;
            int dg;
            int db;
            uint32_t distance;
            if (hwr_index_reserved(i))
                continue;
            dr = tr - (int)((g_present_argb[i] >> 16) & 0xffu);
            dg = tg - (int)((g_present_argb[i] >> 8) & 0xffu);
            db = tb - (int)(g_present_argb[i] & 0xffu);
            distance = (uint32_t)(dr * dr + dg * dg + db * db);
            if (distance < best_distance) {
                best_distance = distance;
                best = i;
                if (distance == 0)
                    break;
            }
        }
        if (best_distance <= HWR_SUBSTITUTE_TOLERANCE)
            g_present_substitute[reserved] = (uint8_t)best;
    }
}

void hwr_composite_publish_palette(const uint8_t palette_rgb[768],
                                   const uint8_t *grayscale_lut,
                                   int32_t damage_level,
                                   uint8_t rgba_out[1024])
{
    int fade = 0xff - (int)damage_level;
    unsigned i;

    if (palette_rgb == NULL)
        return;
    /* upload_palette_dac (video_display.c:344-362) uploads the palette
     * untouched while the pain accumulator is zero, and otherwise collapses it
     * toward dark red with fade = 0xff - counter held at zero from below. The
     * accumulator runs to 0x164, so a heavy hit saturates the tint instead of
     * coming back out of it. */
    if (damage_level != 0 && fade < 0)
        fade = 0;
    for (i = 0; i < 256u; i++) {
        unsigned source = grayscale_lut != NULL ? grayscale_lut[i] : i;
        int r = palette_rgb[source * 3u];
        int g = palette_rgb[source * 3u + 1u];
        int b = palette_rgb[source * 3u + 2u];
        if (damage_level != 0) {
            r = hwr_arithmetic_shift_8((r - 0x2d) * fade) + 0x2d;
            g = (g * fade) >> 8;
            b = (b * fade) >> 8;
        }
        g_present_argb[i] = 0xff000000u |
                            ((uint32_t)(uint8_t)(r << 2) << 16) |
                            ((uint32_t)(uint8_t)(g << 2) << 8) |
                            (uint32_t)(uint8_t)(b << 2);
        if (rgba_out != NULL) {
            rgba_out[i * 4u] = (uint8_t)(r << 2);
            rgba_out[i * 4u + 1u] = (uint8_t)(g << 2);
            rgba_out[i * 4u + 2u] = (uint8_t)(b << 2);
            rgba_out[i * 4u + 3u] = 0xffu;
        }
    }
    g_present_valid = 1;
    hwr_composite_build_substitute();
}

uint32_t hwr_composite_present_argb(unsigned index)
{
    return g_present_valid && index < 256u ? g_present_argb[index]
                                           : 0xff000000u;
}

uint8_t hwr_composite_palette_index(uint32_t pixel,
                                    const uint32_t palette[256])
{
    unsigned i;
    unsigned best = 0;
    uint32_t best_distance = UINT32_MAX;
    int pr = (int)((pixel >> 16) & 0xffu);
    int pg = (int)((pixel >> 8) & 0xffu);
    int pb = (int)(pixel & 0xffu);

    pixel |= 0xff000000u;
    for (i = 0; i < 256u; i++) {
        int dr;
        int dg;
        int db;
        uint32_t distance;
        if (palette[i] == pixel)
            return (uint8_t)i;
        dr = pr - (int)((palette[i] >> 16) & 0xffu);
        dg = pg - (int)((palette[i] >> 8) & 0xffu);
        db = pb - (int)(palette[i] & 0xffu);
        distance = (uint32_t)(dr * dr + dg * dg + db * db);
        if (distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    return (uint8_t)best;
}

static uint32_t hwr_palette_hash_slot(uint32_t pixel)
{
    return ((pixel ^ (pixel >> 13)) * 2654435761u) & 511u;
}

static void hwr_palette_hash_build(struct hwr_palette_hash *map)
{
    unsigned i;
    memset(map, 0, sizeof *map);
    for (i = 0; i < 256u; i++) {
        uint32_t slot;
        map->palette[i] = hwr_composite_present_argb(i);
        slot = hwr_palette_hash_slot(map->palette[i]);
        while (map->slots[slot] != 0 &&
               map->palette[map->slots[slot] - 1u] != map->palette[i])
            slot = (slot + 1u) & 511u;
        if (map->slots[slot] == 0)
            map->slots[slot] = (uint16_t)(i + 1u);
    }
}

static uint32_t hwr_palette_cube_slot(uint32_t pixel)
{
    return (((pixel >> 18) & 0x3fu) << 12) |
           (((pixel >> 10) & 0x3fu) << 6) |
           ((pixel >> 2) & 0x3fu);
}

static int hwr_palette_cube_build(struct hwr_composite *composite,
                                  const struct hwr_palette_hash *map)
{
    unsigned i;
    if (composite->palette_cube == NULL) {
        composite->palette_cube = (uint16_t *)calloc(
            HWR_PALETTE_CUBE_SIZE, sizeof *composite->palette_cube);
        if (composite->palette_cube == NULL)
            return 0;
    } else if (composite->palette_cube_valid &&
               memcmp(composite->palette_cube_palette, map->palette,
                      sizeof composite->palette_cube_palette) == 0) {
        return 1;
    } else {
        memset(composite->palette_cube, 0,
               HWR_PALETTE_CUBE_SIZE * sizeof *composite->palette_cube);
    }
    for (i = 0; i < 256u; i++) {
        uint32_t slot = hwr_palette_cube_slot(map->palette[i]);
        if (composite->palette_cube[slot] == 0)
            composite->palette_cube[slot] = (uint16_t)(i + 1u);
    }
    memcpy(composite->palette_cube_palette, map->palette,
           sizeof composite->palette_cube_palette);
    composite->palette_cube_valid = 1;
    return 1;
}

static uint8_t hwr_palette_cube_index(const struct hwr_composite *composite,
                                      const struct hwr_palette_hash *map,
                                      uint32_t pixel)
{
    uint16_t value;
    pixel |= 0xff000000u;
    value = composite->palette_cube[hwr_palette_cube_slot(pixel)];
    if (value != 0 && map->palette[value - 1u] == pixel)
        return (uint8_t)(value - 1u);
    return hwr_composite_palette_index(pixel, map->palette);
}

static int hwr_canvas_mapping_read(const struct roth_api_v1 *api,
                                   const struct roth_world_frame_v1 *frame,
                                   struct hwr_canvas_mapping *mapping)
{
    uint32_t screen_w;
    uint32_t screen_h;
    uint32_t aspect_w;
    uint32_t aspect_h;

    if (api == NULL || api->game_ram == NULL || frame == NULL ||
        mapping == NULL || frame->w == 0 || frame->h == 0)
        return 0;
    screen_w = api->game_ram->u32(VA_G_SCREEN_PITCH);
    screen_h = api->game_ram->u32(VA_G_SCREEN_HEIGHT);
    if (screen_w == 0 || screen_h == 0 ||
        screen_w > 4096u || screen_h > 4096u)
        return 0;
    aspect_w = screen_w;
    aspect_h = api->game_ram->u8(VA_G_LINE_DOUBLING_FLAG) != 0
             ? screen_h / 2u : screen_h;
    if (aspect_h == 0)
        return 0;

    mapping->screen_w = screen_w;
    mapping->screen_h = screen_h;
    if (frame->composite_mode !=
        ROTH_COMPOSITE_ENGINE_CENTERED_OVER_CANVAS) {
        mapping->x = (double)frame->vx;
        mapping->y = (double)frame->vy;
        mapping->w = (double)frame->vw;
        mapping->h = (double)frame->vh;
        return 1;
    }

    if ((uint64_t)frame->w * aspect_h <
        (uint64_t)frame->h * aspect_w) {
        mapping->w = frame->w;
        mapping->h = (double)aspect_h * frame->w / aspect_w;
        mapping->x = 0.0;
        mapping->y = ((double)frame->h - mapping->h) * 0.5;
    } else {
        mapping->h = frame->h;
        mapping->w = (double)aspect_w * frame->h / aspect_h;
        mapping->x = ((double)frame->w - mapping->w) * 0.5;
        mapping->y = 0.0;
    }
    return mapping->w > 0.0 && mapping->h > 0.0;
}

static uint32_t hwr_clamp_source(double coordinate, uint32_t extent)
{
    if (coordinate <= 0.0)
        return 0;
    if (coordinate >= (double)extent)
        return extent - 1u;
    return (uint32_t)coordinate;
}

static int hwr_engine_to_source(const struct hwr_canvas_mapping *mapping,
                                const struct roth_world_frame_v1 *frame,
                                uint32_t engine_x, uint32_t engine_y,
                                uint32_t *source_x, uint32_t *source_y)
{
    double x;
    double y;

    if (frame->composite_mode ==
        ROTH_COMPOSITE_ENGINE_CENTERED_OVER_CANVAS) {
        x = mapping->x +
            ((double)engine_x + 0.5) * mapping->w / mapping->screen_w;
        y = mapping->y +
            ((double)engine_y + 0.5) * mapping->h / mapping->screen_h;
    } else {
        x = ((double)(engine_x - (uint32_t)frame->vx) + 0.5) *
            frame->w / frame->vw;
        y = ((double)(engine_y - (uint32_t)frame->vy) + 0.5) *
            frame->h / frame->vh;
    }
    *source_x = hwr_clamp_source(x, frame->w);
    *source_y = hwr_clamp_source(y, frame->h);
    return 1;
}

int hwr_composite_source_to_engine(const struct roth_api_v1 *api,
                                   const struct roth_world_frame_v1 *frame,
                                   uint32_t source_x, uint32_t source_y,
                                   uint32_t *engine_x, uint32_t *engine_y)
{
    struct hwr_canvas_mapping mapping;
    double x;
    double y;

    if (frame == NULL || engine_x == NULL || engine_y == NULL ||
        source_x >= frame->w || source_y >= frame->h ||
        frame->vx < 0 || frame->vy < 0 ||
        !hwr_canvas_mapping_read(api, frame, &mapping))
        return 0;
    if (frame->composite_mode ==
        ROTH_COMPOSITE_ENGINE_CENTERED_OVER_CANVAS) {
        double sx = (double)source_x + 0.5;
        double sy = (double)source_y + 0.5;
        if (sx < mapping.x || sx >= mapping.x + mapping.w ||
            sy < mapping.y || sy >= mapping.y + mapping.h)
            return 0;
        x = (sx - mapping.x) * mapping.screen_w / mapping.w;
        y = (sy - mapping.y) * mapping.screen_h / mapping.h;
    } else {
        x = frame->vx + ((double)source_x + 0.5) * frame->vw / frame->w;
        y = frame->vy + ((double)source_y + 0.5) * frame->vh / frame->h;
    }
    if (x < 0.0 || y < 0.0 ||
        x >= mapping.screen_w || y >= mapping.screen_h)
        return 0;
    *engine_x = (uint32_t)x;
    *engine_y = (uint32_t)y;
    return 1;
}

int hwr_composite_prepare(struct hwr_composite *composite,
                          const struct roth_api_v1 *api,
                          struct roth_world_frame_v1 *frame,
                          const struct hwr_world *world)
{
    struct hwr_canvas_mapping mapping;
    struct hwr_palette_hash palette;
    const uint32_t *source;
    uint8_t *engine;
    uintptr_t framebuffer;
    uint32_t pitch;
    uint32_t screen_height;
    size_t required;
    uint32_t x;
    uint32_t y;

    if (composite == NULL || api == NULL || api->game_ram == NULL ||
        frame == NULL || world == NULL || !world->valid || !world->luts.valid ||
        frame->pixels == NULL || frame->vx < 0 || frame->vy < 0 ||
        frame->vw == 0 || frame->vh == 0 ||
        !hwr_canvas_mapping_read(api, frame, &mapping))
        return 0;
    pitch = api->game_ram->u32(VA_G_SCREEN_PITCH);
    screen_height = api->game_ram->u32(VA_G_SCREEN_HEIGHT);
    if ((uint32_t)frame->vx >= pitch ||
        frame->vw > pitch - (uint32_t)frame->vx ||
        (uint32_t)frame->vy >= screen_height ||
        frame->vh > screen_height - (uint32_t)frame->vy)
        return 0;
    framebuffer = (uintptr_t)api->game_ram->u32(VA_G_FRAMEBUFFER_PTR);
    if (framebuffer == 0)
        return 0;

    required = (size_t)frame->vw * frame->vh;
    if (required > composite->engine_base_capacity) {
        uint8_t *next = (uint8_t *)realloc(composite->engine_base, required);
        if (next == NULL)
            return 0;
        composite->engine_base = next;
        composite->engine_base_capacity = required;
    }
    if (frame->vw > composite->engine_source_x_capacity) {
        uint32_t *next = (uint32_t *)realloc(
            composite->engine_source_x,
            (size_t)frame->vw * sizeof(uint32_t));
        if (next == NULL)
            return 0;
        composite->engine_source_x = next;
        composite->engine_source_x_capacity = frame->vw;
    }
    if (frame->vh > composite->engine_source_y_capacity) {
        uint32_t *next = (uint32_t *)realloc(
            composite->engine_source_y,
            (size_t)frame->vh * sizeof(uint32_t));
        if (next == NULL)
            return 0;
        composite->engine_source_y = next;
        composite->engine_source_y_capacity = frame->vh;
    }
    source = (const uint32_t *)frame->pixels;
    hwr_palette_hash_build(&palette);
    if (!hwr_palette_cube_build(composite, &palette))
        return 0;
    /* The mapping is separable: source X depends only on engine X and source Y
     * only on engine Y. Compute each coordinate once per row/column rather than
     * repeating the same floating-point projection for every engine pixel. */
    for (x = 0; x < frame->vw; x++) {
        uint32_t ignored_y;
        hwr_engine_to_source(
            &mapping, frame, (uint32_t)frame->vx + x,
            (uint32_t)frame->vy, &composite->engine_source_x[x], &ignored_y);
    }
    for (y = 0; y < frame->vh; y++) {
        const uint32_t *source_row;
        uint32_t ignored_x;
        hwr_engine_to_source(
            &mapping, frame, (uint32_t)frame->vx,
            (uint32_t)frame->vy + y, &ignored_x,
            &composite->engine_source_y[y]);
        source_row = source +
            (size_t)composite->engine_source_y[y] * frame->w;
        for (x = 0; x < frame->vw; x++) {
            /* The substitution is what keeps the plane distinguishable from the
             * chrome drawn over it. Both copies below -- the one written into
             * the engine framebuffer and the one submitted as the baseline --
             * come from this array, so they can never disagree. */
            composite->engine_base[(size_t)y * frame->vw + x] =
                g_present_substitute[
                    hwr_palette_cube_index(
                        composite, &palette,
                        source_row[composite->engine_source_x[x]])];
        }
    }

    engine = (uint8_t *)framebuffer;
    for (y = 0; y < frame->vh; y++) {
        memcpy(engine + (size_t)((uint32_t)frame->vy + y) * pitch +
                   (uint32_t)frame->vx,
               composite->engine_base + (size_t)y * frame->vw,
               frame->vw);
    }
    frame->engine_base_pixels = composite->engine_base;
    frame->engine_base_pitch_bytes = frame->vw;
    composite->source_valid = 0;
    composite->effect_active = 0;
    return 1;
}

int hwr_composite_engine_shadow(struct hwr_composite *composite,
                                const struct roth_api_v1 *api,
                                struct roth_world_frame_v1 *frame)
{
    uintptr_t framebuffer;
    uint32_t pitch;
    uint32_t screen_height;
    size_t required;
    uint32_t y;
    uint32_t x;

    if (composite == NULL || api == NULL || api->game_ram == NULL ||
        frame == NULL || frame->vx < 0 || frame->vy < 0 ||
        frame->vw == 0 || frame->vh == 0)
        return 0;
    pitch = api->game_ram->u32(VA_G_SCREEN_PITCH);
    screen_height = api->game_ram->u32(VA_G_SCREEN_HEIGHT);
    if (pitch == 0 || pitch > 4096u || screen_height == 0 ||
        screen_height > 4096u ||
        (uint32_t)frame->vx >= pitch ||
        frame->vw > pitch - (uint32_t)frame->vx ||
        (uint32_t)frame->vy >= screen_height ||
        frame->vh > screen_height - (uint32_t)frame->vy)
        return 0;
    framebuffer = (uintptr_t)api->game_ram->u32(VA_G_FRAMEBUFFER_PTR);
    if (framebuffer == 0)
        return 0;

    required = (size_t)frame->vw * frame->vh;
    if (required > composite->engine_shadow_capacity) {
        uint8_t *next = (uint8_t *)realloc(composite->engine_shadow, required);
        if (next == NULL)
            return 0;
        composite->engine_shadow = next;
        composite->engine_shadow_capacity = required;
    }
    /* Flipping a bit of each live index yields a plane no live pixel can match,
     * whatever the engine has drawn, so the reveal test never fires and the
     * engine layer stays opaque across the whole viewport. */
    for (y = 0; y < frame->vh; y++) {
        const uint8_t *row = (const uint8_t *)framebuffer +
            (size_t)((uint32_t)frame->vy + y) * pitch + (uint32_t)frame->vx;
        uint8_t *out = composite->engine_shadow + (size_t)y * frame->vw;
        for (x = 0; x < frame->vw; x++)
            out[x] = (uint8_t)(row[x] ^ 1u);
    }
    frame->engine_base_pixels = composite->engine_shadow;
    frame->engine_base_pitch_bytes = frame->vw;
    return 1;
}

int hwr_composite_grayscale_baseline(struct hwr_composite *composite,
                                     const uint8_t lut[256],
                                     const struct roth_world_frame_v1 *frame)
{
    size_t pixels;
    size_t i;

    if (composite == NULL || lut == NULL || frame == NULL ||
        composite->engine_base == NULL ||
        frame->engine_base_pixels != composite->engine_base)
        return 0;
    pixels = (size_t)frame->vw * frame->vh;
    if (pixels > composite->engine_base_capacity)
        return 0;
    /* The remap has to be exactly the engine's, because the engine has just run
     * the same table over the same pixels of its own framebuffer -- substituting
     * afterwards would make the two disagree and hide the world entirely.  It
     * needs no substitution: the engine builds this table as 0x4f minus a
     * green-weighted luminance of each 6-bit palette entry
     * (build_view_grayscale_lut, video_display.c), and that luminance is
     * ((63*4 + 63 + 63*8) * 0x500) >> 16 = 15 at most, so every entry it can
     * ever produce lies in 0x40..0x4f and none of them is a reserved index. */
    for (i = 0; i < pixels; i++)
        composite->engine_base[i] = lut[composite->engine_base[i]];
    return 1;
}

int hwr_composite_apply_brightness(
    struct hwr_composite *composite,
    struct roth_world_frame_v1 *frame,
    uint32_t multiplier)
{
    size_t pixels;
    size_t i;

    if (composite == NULL || frame == NULL || frame->pixels == NULL ||
        frame->w == 0 || frame->h == 0 || multiplier > 0x100u)
        return 0;
    pixels = (size_t)frame->w * frame->h;
    if (pixels > composite->source_capacity) {
        uint32_t *next = (uint32_t *)realloc(
            composite->source_pixels, pixels * sizeof(uint32_t));
        if (next == NULL)
            return 0;
        composite->source_pixels = next;
        composite->source_capacity = pixels;
        composite->source_valid = 0;
    }
    if (!composite->source_valid ||
        composite->source_w != frame->w ||
        composite->source_h != frame->h) {
        memcpy(composite->source_pixels, frame->pixels,
               pixels * sizeof(uint32_t));
        composite->source_w = frame->w;
        composite->source_h = frame->h;
        composite->source_valid = 1;
    }
    if (pixels > composite->effect_capacity) {
        uint32_t *next = (uint32_t *)realloc(
            composite->effect_pixels, pixels * sizeof(uint32_t));
        if (next == NULL)
            return 0;
        composite->effect_pixels = next;
        composite->effect_capacity = pixels;
    }
    for (i = 0; i < pixels; i++) {
        uint32_t source = composite->source_pixels[i];
        uint32_t r = ((source >> 16) & 0xffu) * multiplier >> 8;
        uint32_t g = ((source >> 8) & 0xffu) * multiplier >> 8;
        uint32_t b = (source & 0xffu) * multiplier >> 8;
        composite->effect_pixels[i] =
            (source & 0xff000000u) | (r << 16) | (g << 8) | b;
    }
    frame->pixels = composite->effect_pixels;
    composite->effect_active = 2;
    return 1;
}

void hwr_composite_clear(struct hwr_composite *composite)
{
    if (composite == NULL)
        return;
    free(composite->engine_base);
    free(composite->engine_shadow);
    free(composite->engine_source_x);
    free(composite->engine_source_y);
    free(composite->palette_cube);
    free(composite->effect_pixels);
    free(composite->source_pixels);
    memset(composite, 0, sizeof *composite);
}
