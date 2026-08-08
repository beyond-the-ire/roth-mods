#include "hwr_overlay_draw.h"

#include "hwr_chrome.h"

#include <stdlib.h>
#include <string.h>

/* Named globals the panel is built from, at their fixed addresses. */
#define VA_G_RELOC_BASE           0x7f56cu
#define VA_G_PALETTE_RGB_PTR      0x85488u
#define VA_G_LINE_DOUBLING_FLAG   0x90cbdu
#define VA_G_CURSOR_SPRITE_HEADER 0x7e950u

/* The engine's nine-slice window frame, as show_message_box assembles it. */
#define HWR_FRAME_LEFT   0x22u
#define HWR_FRAME_TOP    0x0fu
#define HWR_FRAME_RIGHT  8u
#define HWR_FRAME_BOTTOM 8u
#define HWR_FRAME_STEP   0x0bu
#define HWR_FRAME_BASE   0x17u

/* Image-pool slots: the frame's own tiles, the ornate spine, and the scroll
 * arrows in their active and greyed forms. */
#define HWR_SLOT_CORNER_TL   0x2b8u
#define HWR_SLOT_TOP_RUN     0x2a0u
#define HWR_SLOT_CORNER_TR   0x318u
#define HWR_SLOT_RIGHT_STEP  0x2a8u
#define HWR_SLOT_CORNER_BL   0x320u
#define HWR_SLOT_BOTTOM_RUN  0x2c8u
#define HWR_SLOT_CORNER_BR   0x2b0u
#define HWR_SLOT_SPINE_FOOT  0x0d8u
#define HWR_SLOT_SPINE       0x0d0u
#define HWR_SLOT_ARROW_UP_ON   0x0a8u
#define HWR_SLOT_ARROW_UP_OFF  0x0b0u
#define HWR_SLOT_ARROW_DN_ON   0x0b8u
#define HWR_SLOT_ARROW_DN_OFF  0x0c0u

/* The spine tile's descriptor height is a stale leftover; this is the row count
 * it really encodes, and blitting past it over-reads the pool. */
#define HWR_SPINE_W 40u
#define HWR_SPINE_H 150u

/* Literal colours, used until the frame art can be sampled for the real ones. */
#define HWR_COLOR_TEXT_DEF    0x20u
#define HWR_COLOR_GOLD_HI_DEF 0x70u
#define HWR_COLOR_GOLD_MID_DEF 0x75u
#define HWR_COLOR_GOLD_LO_DEF 0x9eu
#define HWR_COLOR_PANEL_DEF   0x01u
/* The warning colour's own literal: the head of this game's red ramp, which is
 * also what the sampling below finds in its palette. The two agreeing is worth
 * having -- the warning never changes colour under somebody's eye as the art
 * comes up -- but the sampling is what makes it a red the GAME holds rather
 * than a number picked here, which is what a palette nobody has seen needs. */
#define HWR_COLOR_WARN_DEF    0x67u

/* A palette entry is six bits per channel, so a red at full strength alone is
 * worth 63 * 54 by the luminance weights below. Half of that is the floor: a
 * red darker than that reads as a shadow rather than as a warning, and would
 * be worse than the literal it replaced. */
#define HWR_WARN_LUMINANCE_FLOOR (32u * 54u)

/* Working room for one sampled tile. */
#define HWR_SCRATCH_W 128u
#define HWR_SCRATCH_H 128u

struct hwr_overlay_surface {
    uint8_t *pixels;
    uint8_t *mask;
    uint32_t w;
    uint32_t h;
    uint32_t x0;
    uint32_t y0;
    uint32_t x1;
    uint32_t y1;
};

static uint8_t *g_storage;
static struct hwr_overlay_surface g_surfaces[2];
static int g_published = -1;
static int g_readers[2];
static int g_cursor_visible;
static int32_t g_cursor_x;
static int32_t g_cursor_y;

static struct hwr_overlay_colors g_colors = {
    HWR_COLOR_GOLD_HI_DEF, HWR_COLOR_GOLD_MID_DEF, HWR_COLOR_GOLD_LO_DEF,
    HWR_COLOR_TEXT_DEF, HWR_COLOR_PANEL_DEF, HWR_COLOR_WARN_DEF
};
static uint32_t g_colors_base;

static uint8_t g_spine[HWR_SPINE_W * HWR_SPINE_H];
static uint32_t g_spine_w;
static uint32_t g_spine_h;
static uint32_t g_spine_base;

static uint8_t g_scratch[HWR_SCRATCH_W * HWR_SCRATCH_H];

static uint32_t g_arrow_w;
static uint32_t g_arrow_h;
static uint32_t g_arrow_base;

int hwr_overlay_draw_init(void)
{
    size_t plane = HWR_OVERLAY_MAX_PIXELS;

    if (g_storage != NULL)
        return 0;
    g_storage = (uint8_t *)calloc(plane * 4u, 1);
    if (g_storage == NULL)
        return -1;
    g_surfaces[0].pixels = g_storage;
    g_surfaces[0].mask = g_storage + plane;
    g_surfaces[1].pixels = g_storage + plane * 2u;
    g_surfaces[1].mask = g_storage + plane * 3u;
    __atomic_store_n(&g_published, -1, __ATOMIC_RELEASE);
    __atomic_store_n(&g_readers[0], 0, __ATOMIC_RELAXED);
    __atomic_store_n(&g_readers[1], 0, __ATOMIC_RELAXED);
    return 0;
}

void hwr_overlay_draw_shutdown(void)
{
    __atomic_store_n(&g_published, -1, __ATOMIC_RELEASE);
    free(g_storage);
    g_storage = NULL;
    memset(g_surfaces, 0, sizeof g_surfaces);
    g_spine_w = 0;
    g_spine_h = 0;
    g_spine_base = 0;
    g_arrow_w = 0;
    g_arrow_h = 0;
    g_arrow_base = 0;
    g_colors_base = 0;
}

int hwr_overlay_draw_begin(uint32_t width, uint32_t height,
                           struct hwr_overlay_canvas *canvas)
{
    int current;
    int back;

    if (g_storage == NULL || canvas == NULL || width < 64u || height < 64u ||
        (uint64_t)width * height > HWR_OVERLAY_MAX_PIXELS)
        return 0;
    current = __atomic_load_n(&g_published, __ATOMIC_ACQUIRE);
    back = current == 0 ? 1 : 0;
    /* The compose tick is still inside this surface; the frame it is showing is
     * the one that was current a moment ago, so leaving it alone costs nothing
     * but this redraw. */
    if (__atomic_load_n(&g_readers[back], __ATOMIC_ACQUIRE) != 0)
        return 0;
    g_surfaces[back].w = width;
    g_surfaces[back].h = height;
    canvas->pixels = g_surfaces[back].pixels;
    canvas->mask = g_surfaces[back].mask;
    canvas->w = width;
    canvas->h = height;
    return 1;
}

void hwr_overlay_draw_publish(uint32_t x0, uint32_t y0, uint32_t x1,
                              uint32_t y1)
{
    int current;
    int back;

    if (g_storage == NULL)
        return;
    current = __atomic_load_n(&g_published, __ATOMIC_ACQUIRE);
    back = current == 0 ? 1 : 0;
    g_surfaces[back].x0 = x0;
    g_surfaces[back].y0 = y0;
    g_surfaces[back].x1 = x1;
    g_surfaces[back].y1 = y1;
    __atomic_store_n(&g_published, back, __ATOMIC_RELEASE);
}

void hwr_overlay_draw_withdraw(void)
{
    __atomic_store_n(&g_published, -1, __ATOMIC_RELEASE);
}

void hwr_overlay_draw_set_cursor(int visible, int32_t x, int32_t y)
{
    g_cursor_x = x;
    g_cursor_y = y;
    __atomic_store_n(&g_cursor_visible, visible, __ATOMIC_RELEASE);
}

uint32_t hwr_overlay_draw_line_scale(const struct roth_api_v1 *api)
{
    if (api == NULL || api->game_ram == NULL)
        return 1u;
    return api->game_ram->u8(VA_G_LINE_DOUBLING_FLAG) != 0 ? 2u : 1u;
}

/* ------------------------------- primitives ------------------------------- */

static int hwr_overlay_clip(const struct hwr_overlay_canvas *canvas,
                            uint32_t *x0, uint32_t *y0, uint32_t *x1,
                            uint32_t *y1)
{
    if (canvas == NULL || canvas->pixels == NULL)
        return 0;
    if (*x1 > canvas->w)
        *x1 = canvas->w;
    if (*y1 > canvas->h)
        *y1 = canvas->h;
    return *x0 < *x1 && *y0 < *y1;
}

void hwr_overlay_draw_clear(const struct hwr_overlay_canvas *canvas,
                            uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    uint32_t y;

    if (!hwr_overlay_clip(canvas, &x0, &y0, &x1, &y1))
        return;
    for (y = y0; y < y1; y++) {
        size_t row = (size_t)y * canvas->w + x0;
        memset(canvas->pixels + row, 0, x1 - x0);
        memset(canvas->mask + row, 0, x1 - x0);
    }
}

void hwr_overlay_draw_fill(const struct hwr_overlay_canvas *canvas,
                           uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                           uint8_t color)
{
    uint32_t y;

    if (!hwr_overlay_clip(canvas, &x0, &y0, &x1, &y1))
        return;
    for (y = y0; y < y1; y++) {
        size_t row = (size_t)y * canvas->w + x0;
        memset(canvas->pixels + row, color, x1 - x0);
        memset(canvas->mask + row, 1, x1 - x0);
    }
}

void hwr_overlay_draw_hline(const struct hwr_overlay_canvas *canvas,
                            uint32_t x, uint32_t y, uint32_t length,
                            uint8_t color)
{
    hwr_overlay_draw_fill(canvas, x, y, x + length, y + 1u, color);
}

void hwr_overlay_draw_border(const struct hwr_overlay_canvas *canvas,
                             uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                             uint8_t color)
{
    if (x1 <= x0 || y1 <= y0)
        return;
    hwr_overlay_draw_fill(canvas, x0, y0, x1, y0 + 1u, color);
    hwr_overlay_draw_fill(canvas, x0, y1 - 1u, x1, y1, color);
    hwr_overlay_draw_fill(canvas, x0, y0, x0 + 1u, y1, color);
    hwr_overlay_draw_fill(canvas, x1 - 1u, y0, x1, y1, color);
}

void hwr_overlay_draw_finish(const struct hwr_overlay_canvas *canvas,
                             uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                             uint8_t background)
{
    uint32_t y;

    if (background == HWR_CHROME_PANEL_TOKEN)
        background ^= 1u;
    if (!hwr_overlay_clip(canvas, &x0, &y0, &x1, &y1))
        return;
    for (y = y0; y < y1; y++) {
        size_t row = (size_t)y * canvas->w;
        uint32_t x;

        for (x = x0; x < x1; x++) {
            if (canvas->mask[row + x] == 0 && canvas->pixels[row + x] == 0)
                canvas->pixels[row + x] = background;
            /* The one index the panel promises never to publish. The nearest
             * neighbour stands in for it, which is a colour nobody can tell
             * from the one it replaces on the handful of pixels of frame art
             * that ever hold it. */
            if (canvas->pixels[row + x] == HWR_CHROME_PANEL_TOKEN)
                canvas->pixels[row + x] = HWR_CHROME_PANEL_TOKEN ^ 1u;
            canvas->mask[row + x] = 1;
        }
    }
}

/* --------------------------------- colours -------------------------------- */

/* The warning red, off the palette rather than off a tile: the index whose red
 * most exceeds BOTH of its other channels, above a luminance floor. The frame
 * art has no red in it -- it is gold from edge to edge -- so the tile the golds
 * are drawn from cannot answer this, and the palette can: every game whose art
 * has a fire in it carries a saturated red somewhere in its ramp, and this
 * finds that one rather than trusting a number written here. */
static void hwr_overlay_sample_warn(const uint8_t *palette)
{
    uint32_t strongest = 0;
    uint8_t warn = 0;
    uint32_t i;

    for (i = 1; i < 256u; i++) {
        uint32_t red = palette[i * 3u];
        uint32_t green = palette[i * 3u + 1u];
        uint32_t blue = palette[i * 3u + 2u];
        uint32_t other = green > blue ? green : blue;
        uint32_t luminance;
        uint32_t excess;

        if (red <= other)
            continue;
        excess = red - other;
        luminance = red * 54u + green * 183u + blue * 19u;
        if (luminance < HWR_WARN_LUMINANCE_FLOOR)
            continue;
        if (excess > strongest) {
            strongest = excess;
            warn = (uint8_t)i;
        }
    }
    /* A palette with no red in it at all keeps the literal: a warning drawn in
     * whatever came nearest to red would be a warning nobody reads as one. */
    if (warn != 0)
        g_colors.warn = warn;
}

/* The frame art's own golds: the brightest, the most common, and the darkest of
 * the warm indices the top-run tile is drawn from. */
static void hwr_overlay_sample_colors(const struct roth_api_v1 *api)
{
    uint32_t base;
    uint32_t descriptor;
    const uint8_t *palette;
    uint32_t width;
    uint32_t height;
    uint32_t counts[256];
    uint32_t brightest = 0;
    uint32_t darkest = 0xffffffffu;
    uint32_t most = 0;
    uint8_t hi = 0;
    uint8_t lo = 0;
    uint8_t mid = 0;
    uint32_t i;

    if (api == NULL || api->game_ram == NULL || api->engine == NULL)
        return;
    base = api->game_ram->u32(VA_G_RELOC_BASE);
    if (base == 0 || base == g_colors_base)
        return;
    palette = (const uint8_t *)(uintptr_t)api->game_ram->u32(
        VA_G_PALETTE_RGB_PTR);
    if (palette == NULL)
        return;
    /* The warning red is the palette's own business and is taken first: it is
     * wanted as soon as there is a palette, and it must not wait on the frame
     * art the golds need -- which is a separate thing that may not be loaded
     * yet, and which this deliberately does not stand or fall with. */
    hwr_overlay_sample_warn(palette);
    descriptor = api->engine->resolve_reloc_ptr(HWR_SLOT_TOP_RUN);
    if (descriptor == 0)
        return;
    width = *(const uint16_t *)(uintptr_t)(descriptor + 4u);
    height = *(const uint16_t *)(uintptr_t)(descriptor + 6u);
    if (width == 0 || height == 0 || width > HWR_SCRATCH_W ||
        height > HWR_SCRATCH_H)
        return;
    memset(g_scratch, 0, (size_t)width * height);
    api->engine->blit_reloc_das_image((uint32_t)(uintptr_t)g_scratch,
                                      HWR_SLOT_TOP_RUN, width);
    memset(counts, 0, sizeof counts);
    for (i = 0; i < width * height; i++)
        counts[g_scratch[i]]++;
    for (i = 1; i < 256u; i++) {
        uint32_t luminance;

        if (counts[i] < 4u)
            continue;
        /* Warm indices only: the frame's gold, not the art behind it. */
        if (palette[i * 3u] < palette[i * 3u + 2u])
            continue;
        luminance = (uint32_t)palette[i * 3u] * 54u +
                    (uint32_t)palette[i * 3u + 1u] * 183u +
                    (uint32_t)palette[i * 3u + 2u] * 19u;
        if (luminance > brightest) {
            brightest = luminance;
            hi = (uint8_t)i;
        }
        if (luminance < darkest) {
            darkest = luminance;
            lo = (uint8_t)i;
        }
        if (counts[i] > most) {
            most = counts[i];
            mid = (uint8_t)i;
        }
    }
    if (hi != 0)
        g_colors.gold_hi = hi;
    if (mid != 0)
        g_colors.gold_mid = mid;
    if (lo != 0)
        g_colors.gold_lo = lo;
    g_colors_base = base;
}

const struct hwr_overlay_colors *hwr_overlay_draw_colors(
    const struct roth_api_v1 *api)
{
    hwr_overlay_sample_colors(api);
    return &g_colors;
}

/* ---------------------------------- text ---------------------------------- */

uint32_t hwr_overlay_draw_measure(const struct roth_api_v1 *api,
                                  const char *text)
{
    int32_t width;

    if (text == NULL)
        return 0;
    if (api == NULL || api->engine == NULL)
        return (uint32_t)strlen(text) * 8u;
    width = api->engine->measure_control_text_width(text);
    return width > 0 ? (uint32_t)width : 0;
}

size_t hwr_overlay_draw_fit(const struct roth_api_v1 *api, const char *text,
                            uint32_t max_width)
{
    char buffer[192];
    size_t length;

    if (text == NULL)
        return 0;
    length = strlen(text);
    if (length > sizeof buffer - 1u)
        length = sizeof buffer - 1u;
    memcpy(buffer, text, length);
    buffer[length] = 0;
    while (length != 0 && hwr_overlay_draw_measure(api, buffer) > max_width)
        buffer[--length] = 0;
    return length;
}

void hwr_overlay_draw_text(const struct roth_api_v1 *api,
                           const struct hwr_overlay_canvas *canvas,
                           uint32_t x, uint32_t y, uint32_t max_width,
                           uint8_t color, const char *text)
{
    /* The colour prefix the font routine reads, then the trimmed string. */
    char buffer[194];
    size_t length;
    uint32_t scale;
    uint32_t rows;

    if (canvas == NULL || canvas->pixels == NULL || text == NULL || x == 0 ||
        y == 0 || x >= canvas->w || y >= canvas->h)
        return;
    scale = hwr_overlay_draw_line_scale(api);
    /* The tallest glyph plus the row the outline prepass reaches into. */
    rows = 18u * scale;
    if (canvas->h - y < rows || canvas->w - x < 2u)
        return;
    if (max_width > canvas->w - x - 1u)
        max_width = canvas->w - x - 1u;
    length = hwr_overlay_draw_fit(api, text, max_width);
    if (length == 0)
        return;
    buffer[0] = 0x01;
    buffer[1] = (char)color;
    memcpy(buffer + 2, text, length);
    buffer[2 + length] = 0;
    if (api == NULL || api->engine == NULL)
        return;
    /* The pen starts one pixel in from the destination, so (x, y) is the slack
     * the outline prepass writes into. */
    api->engine->draw_text_to_buffer(
        (uint32_t)(uintptr_t)buffer,
        (uint32_t)(uintptr_t)(canvas->pixels + (size_t)y * canvas->w + x),
        canvas->w, scale == 2u ? 1u : 0u);
}

/* ---------------------------------- frame --------------------------------- */

void hwr_overlay_draw_frame_bands(uint32_t *left, uint32_t *top,
                                  uint32_t *right, uint32_t *bottom)
{
    if (left != NULL)
        *left = HWR_FRAME_LEFT;
    if (top != NULL)
        *top = HWR_FRAME_TOP;
    if (right != NULL)
        *right = HWR_FRAME_RIGHT;
    if (bottom != NULL)
        *bottom = HWR_FRAME_BOTTOM;
}

uint32_t hwr_overlay_draw_frame_snap(uint32_t height)
{
    uint32_t count;
    uint32_t snapped;

    if (height < HWR_FRAME_BASE + HWR_FRAME_STEP)
        return height;
    count = (height - HWR_FRAME_BASE) / HWR_FRAME_STEP;
    if (count < 1u)
        count = 1u;
    snapped = count * HWR_FRAME_STEP + HWR_FRAME_BASE;
    return snapped <= height ? snapped : height;
}

static void hwr_overlay_tile(const struct roth_api_v1 *api,
                             const struct hwr_overlay_canvas *canvas,
                             int32_t x, int32_t y, uint32_t slot)
{
    if (x < 0 || y < 0 || (uint32_t)x >= canvas->w || (uint32_t)y >= canvas->h)
        return;
    api->engine->blit_reloc_das_image(
        (uint32_t)(uintptr_t)(canvas->pixels +
                              (size_t)(uint32_t)y * canvas->w + (uint32_t)x),
        slot, canvas->w);
}

/* The ornate column is one self-contained tile that is repeated down the left
 * band, mirrored on alternate repeats so the joins do not read as a seam. */
static void hwr_overlay_cache_spine(const struct roth_api_v1 *api)
{
    uint32_t base = api->game_ram->u32(VA_G_RELOC_BASE);
    uint32_t descriptor;
    uint32_t width;
    uint16_t *height_field;
    uint16_t saved;

    if (base == 0) {
        g_spine_w = 0;
        g_spine_h = 0;
        return;
    }
    if (g_spine_w != 0 && g_spine_base == base)
        return;
    g_spine_w = 0;
    g_spine_h = 0;
    descriptor = api->engine->resolve_reloc_ptr(HWR_SLOT_SPINE);
    if (descriptor == 0)
        return;
    width = *(const uint16_t *)(uintptr_t)(descriptor + 4u);
    if (width == 0 || width > HWR_SPINE_W)
        return;
    height_field = (uint16_t *)(uintptr_t)(descriptor + 6u);
    saved = *height_field;
    memset(g_spine, 0, (size_t)width * HWR_SPINE_H);
    *height_field = (uint16_t)HWR_SPINE_H;
    api->engine->blit_reloc_das_image((uint32_t)(uintptr_t)g_spine,
                                      HWR_SLOT_SPINE, width);
    *height_field = saved;
    g_spine_w = width;
    g_spine_h = HWR_SPINE_H;
    g_spine_base = base;
}

int hwr_overlay_draw_frame(const struct roth_api_v1 *api,
                           const struct hwr_overlay_canvas *canvas,
                           uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                           uint8_t background)
{
    int32_t bx;
    int32_t by;
    int32_t bw;
    int32_t bh;
    int32_t remaining;
    int32_t tile_x;
    int32_t row_y;
    int32_t count;
    int32_t i;

    if (api == NULL || api->game_ram == NULL || api->engine == NULL ||
        canvas == NULL || canvas->pixels == NULL ||
        api->game_ram->u32(VA_G_RELOC_BASE) == 0)
        return 0;
    /* The line-doubled modes draw the art at half the vertical resolution the
     * frame assumes, so the plain panel stands in for it there. */
    if (hwr_overlay_draw_line_scale(api) != 1u)
        return 0;
    bx = (int32_t)x0;
    by = (int32_t)y0;
    bw = (int32_t)(x1 - x0);
    bh = (int32_t)(y1 - y0);
    if (bw < 0x4a || bh < 0x1f || x1 > canvas->w || y1 > canvas->h)
        return 0;

    hwr_overlay_cache_spine(api);
    if (g_spine_w != 0 && g_spine_h != 0) {
        uint32_t band = (uint32_t)(bh - 3);
        uint32_t period = 2u * g_spine_h;
        uint32_t r;

        for (r = 0; r < band; r++) {
            uint32_t y = (uint32_t)by + r;
            uint32_t phase = r % period;
            uint32_t source = phase < g_spine_h ? phase
                                                : period - 1u - phase;
            const uint8_t *row = g_spine + (size_t)source * g_spine_w;
            uint8_t *destination =
                canvas->pixels + (size_t)y * canvas->w + (uint32_t)bx;
            uint32_t c;

            if (y >= canvas->h)
                break;
            for (c = 0; c < g_spine_w && (uint32_t)bx + c < canvas->w; c++)
                destination[c] = row[c] != 0 ? row[c] : background;
        }
    }
    hwr_overlay_tile(api, canvas, bx, by + bh - 3, HWR_SLOT_SPINE_FOOT);

    hwr_overlay_tile(api, canvas, bx + 0x22, by, HWR_SLOT_CORNER_TL);
    remaining = bw - 0x47;
    tile_x = bx + 0x3f;
    while (remaining >= 0x33) {
        hwr_overlay_tile(api, canvas, tile_x, by, HWR_SLOT_TOP_RUN);
        remaining -= 0x33;
        tile_x += 0x33;
    }
    if (remaining > 0)
        hwr_overlay_tile(api, canvas, tile_x - 0x33 + remaining, by,
                         HWR_SLOT_TOP_RUN);
    hwr_overlay_tile(api, canvas, bx + bw - 8, by, HWR_SLOT_CORNER_TR);

    row_y = by + 0xf;
    count = ((int32_t)bh - (int32_t)HWR_FRAME_BASE) / (int32_t)HWR_FRAME_STEP;
    if (count < 1)
        count = 1;
    for (i = 0; i < count; i++) {
        hwr_overlay_tile(api, canvas, bx + bw - 8, row_y, HWR_SLOT_RIGHT_STEP);
        row_y += (int32_t)HWR_FRAME_STEP;
    }
    row_y += 1;
    hwr_overlay_tile(api, canvas, bx + 0x21, row_y, HWR_SLOT_CORNER_BL);
    if (row_y >= 0 && (uint32_t)row_y < canvas->h) {
        /* The bottom run is stretched by handing the blitter a packed
         * width-and-pitch, exactly as the engine's own window does. */
        uint32_t destination = (uint32_t)(uintptr_t)(
            canvas->pixels + (size_t)(uint32_t)row_y * canvas->w +
            (uint32_t)(bx + 0x23));
        api->engine->blit_reloc_das_image(
            destination, HWR_SLOT_BOTTOM_RUN,
            (((uint32_t)(bw - 0x2a)) << 16) + canvas->w);
    }
    hwr_overlay_tile(api, canvas, bx + bw - 8, row_y, HWR_SLOT_CORNER_BR);
    return 1;
}

/* --------------------------------- arrows --------------------------------- */

void hwr_overlay_draw_arrow_size(const struct roth_api_v1 *api, uint32_t *width,
                                 uint32_t *height)
{
    uint32_t base;

    if (width == NULL || height == NULL)
        return;
    *width = 12u;
    *height = 16u;
    if (api == NULL || api->game_ram == NULL || api->engine == NULL)
        return;
    base = api->game_ram->u32(VA_G_RELOC_BASE);
    if (base != 0 && base != g_arrow_base) {
        uint32_t descriptor =
            api->engine->resolve_reloc_ptr(HWR_SLOT_ARROW_UP_OFF);

        g_arrow_w = 0;
        g_arrow_h = 0;
        if (descriptor != 0) {
            uint32_t w = *(const uint16_t *)(uintptr_t)(descriptor + 4u);
            uint32_t h = *(const uint16_t *)(uintptr_t)(descriptor + 6u);

            if (w != 0 && w <= 64u && h != 0 && h <= 64u) {
                g_arrow_w = w;
                g_arrow_h = h;
            }
        }
        g_arrow_base = base;
    }
    if (g_arrow_w != 0 && g_arrow_h != 0) {
        *width = g_arrow_w;
        *height = g_arrow_h;
    }
}

void hwr_overlay_draw_arrow(const struct roth_api_v1 *api,
                            const struct hwr_overlay_canvas *canvas,
                            uint32_t x, uint32_t y, uint32_t width,
                            uint32_t height, int up, int active,
                            const struct hwr_overlay_colors *colors)
{
    uint32_t base = 0;

    if (canvas == NULL || canvas->pixels == NULL || colors == NULL ||
        x + width + 1u > canvas->w || y + height + 1u > canvas->h)
        return;
    if (api != NULL && api->game_ram != NULL)
        base = api->game_ram->u32(VA_G_RELOC_BASE);
    if (base != 0 && api->engine != NULL &&
        hwr_overlay_draw_line_scale(api) == 1u) {
        uint32_t slot = up ? (active ? HWR_SLOT_ARROW_UP_ON
                                     : HWR_SLOT_ARROW_UP_OFF)
                           : (active ? HWR_SLOT_ARROW_DN_ON
                                     : HWR_SLOT_ARROW_DN_OFF);

        hwr_overlay_tile(api, canvas, (int32_t)x, (int32_t)y, slot);
        return;
    }
    /* No image pool: a plain triangle in the same two states. */
    hwr_overlay_draw_fill(canvas, x, y, x + width, y + height, colors->panel);
    {
        uint8_t color = active ? colors->gold_hi : colors->gold_lo;
        uint32_t middle = x + width / 2u;
        uint32_t r;

        for (r = 0; r < height; r++) {
            uint32_t half = up ? (r * (width / 2u)) / height
                               : ((height - 1u - r) * (width / 2u)) / height;
            uint32_t left = middle > half ? middle - half : x;
            uint32_t right = middle + half + 1u;

            if (right > x + width)
                right = x + width;
            hwr_overlay_draw_fill(canvas, left, y + r, right, y + r + 1u,
                                  color);
        }
    }
}

/* --------------------------------- compose -------------------------------- */

static void hwr_overlay_compose_cursor(const struct roth_api_v1 *api,
                                       uint8_t *pixels, uint32_t width,
                                       uint32_t height)
{
    uint8_t sprite[2 + 32 * 32];
    uint32_t sprite_w;
    uint32_t sprite_h;
    uint32_t scale;
    int32_t cx;
    int32_t cy;
    uint32_t sy;

    if (!__atomic_load_n(&g_cursor_visible, __ATOMIC_ACQUIRE))
        return;
    cx = g_cursor_x;
    cy = g_cursor_y;
    if (cx < 0 || cy < 0 || (uint32_t)cx >= width || (uint32_t)cy >= height)
        return;
    if (api == NULL || api->game_ram == NULL ||
        api->game_ram->read_block(VA_G_CURSOR_SPRITE_HEADER, sprite,
                                  sizeof sprite) != 0) {
        sprite[0] = 0;
        sprite[1] = 0;
    }
    sprite_w = sprite[0];
    sprite_h = sprite[1];
    scale = (width == 320u && height >= 400u) ? 2u : 1u;
    if (sprite_w == 0 || sprite_h == 0 || sprite_w > 32u || sprite_h > 32u) {
        /* The engine has no cursor art loaded: a small cross still says where
         * the pointer is. */
        int32_t d;

        for (d = -3; d <= 3; d++) {
            int32_t px = cx + d;
            int32_t py = cy + d;

            if (px >= 0 && (uint32_t)px < width)
                pixels[(size_t)(uint32_t)cy * width + (uint32_t)px] =
                    g_colors.gold_hi;
            if (py >= 0 && (uint32_t)py < height)
                pixels[(size_t)(uint32_t)py * width + (uint32_t)cx] =
                    g_colors.gold_hi;
        }
        return;
    }
    for (sy = 0; sy < sprite_h; sy++) {
        uint32_t repeat;

        for (repeat = 0; repeat < scale; repeat++) {
            uint32_t y = (uint32_t)cy + sy * scale + repeat;
            uint32_t sx;

            if (y >= height)
                return;
            for (sx = 0; sx < sprite_w; sx++) {
                uint8_t value = sprite[2 + sy * sprite_w + sx];
                uint32_t x = (uint32_t)cx + sx;

                if (value == HWR_CHROME_PANEL_TOKEN)
                    value ^= 1u;
                if (value != 0 && x < width)
                    pixels[(size_t)y * width + x] = value;
            }
        }
    }
}

void hwr_overlay_draw_compose(const struct roth_api_v1 *api, uint8_t *pixels,
                              uint32_t width, uint32_t height)
{
    unsigned attempt;

    if (pixels == NULL || g_storage == NULL || width < 64u || height < 64u)
        return;
    for (attempt = 0; attempt < 2u; attempt++) {
        const struct hwr_overlay_surface *surface;
        uint32_t x0;
        uint32_t y0;
        uint32_t x1;
        uint32_t y1;
        uint32_t y;
        int index = __atomic_load_n(&g_published, __ATOMIC_ACQUIRE);

        if (index < 0 || index > 1)
            break;
        __atomic_fetch_add(&g_readers[index], 1, __ATOMIC_ACQUIRE);
        if (index != __atomic_load_n(&g_published, __ATOMIC_ACQUIRE)) {
            __atomic_fetch_sub(&g_readers[index], 1, __ATOMIC_RELEASE);
            continue;
        }
        surface = &g_surfaces[index];
        /* A surface drawn for another frame size describes nothing about this
         * one, so it is refused rather than stretched. */
        if (surface->w != width || surface->h != height) {
            __atomic_fetch_sub(&g_readers[index], 1, __ATOMIC_RELEASE);
            break;
        }
        x0 = surface->x0;
        y0 = surface->y0;
        x1 = surface->x1 > width ? width : surface->x1;
        y1 = surface->y1 > height ? height : surface->y1;
        for (y = y0; y < y1; y++) {
            const uint8_t *source = surface->pixels + (size_t)y * width;
            const uint8_t *mask = surface->mask + (size_t)y * width;
            uint8_t *destination = pixels + (size_t)y * width;
            uint32_t x;

            for (x = x0; x < x1; x++) {
                if (mask[x] != 0)
                    destination[x] = source[x];
            }
        }
        __atomic_fetch_sub(&g_readers[index], 1, __ATOMIC_RELEASE);
        break;
    }
    hwr_overlay_compose_cursor(api, pixels, width, height);
}
