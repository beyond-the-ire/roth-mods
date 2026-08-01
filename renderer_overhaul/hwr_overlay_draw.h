#ifndef HWR_OVERLAY_DRAW_H
#define HWR_OVERLAY_DRAW_H

#include "roth_sdk.h"

#include <stdint.h>

/* The pixels of the settings overlay: a pair of engine-sized 8-bit surfaces the
 * game thread draws into and the compose tick copies onto the engine frame,
 * plus the engine chrome, font, and colours the panel is built from.
 *
 * Every routine here except hwr_overlay_draw_compose runs on the GAME thread
 * and may call the engine. hwr_overlay_draw_compose runs in the compositor tick
 * and reads nothing but the published surface, the engine shade table, and the
 * cursor sprite. */

/* The largest engine frame the overlay draws into (640 by 480 hi-res). */
#define HWR_OVERLAY_MAX_PIXELS (640u * 480u)

struct hwr_overlay_canvas {
    uint8_t *pixels;
    /* One byte per pixel: nonzero where the surface owns the pixel. What the
     * panel leaves clear is dimmed rather than replaced, so the panel may hold
     * palette index zero as an ordinary colour. */
    uint8_t *mask;
    uint32_t w;
    uint32_t h;
};

struct hwr_overlay_colors {
    uint8_t gold_hi;   /* bright frame gold: focus, active tab, values */
    uint8_t gold_mid;  /* dominant frame gold: rules and borders */
    uint8_t gold_lo;   /* dark frame gold: fills and disabled text */
    uint8_t text;      /* body text */
    uint8_t panel;     /* the frame's own background */
};

int hwr_overlay_draw_init(void);
void hwr_overlay_draw_shutdown(void);

/* Take the surface that is not on screen. Returns 0 when the frame size is
 * unusable or the compose tick is still reading the surface. */
int hwr_overlay_draw_begin(uint32_t width, uint32_t height,
                           struct hwr_overlay_canvas *canvas);
/* Hand the drawn surface to the compose tick, with the rect it occupies. */
void hwr_overlay_draw_publish(uint32_t x0, uint32_t y0, uint32_t x1,
                              uint32_t y1);
/* Nothing to draw any more. */
void hwr_overlay_draw_withdraw(void);
/* Where the compose tick draws the overlay's own cursor, or nothing when the
 * engine is already drawing one. */
void hwr_overlay_draw_set_cursor(int visible, int32_t x, int32_t y);

/* Compositor tick: copy the published panel onto the engine frame. */
void hwr_overlay_draw_compose(const struct roth_api_v1 *api, uint8_t *pixels,
                              uint32_t width, uint32_t height);

/* The panel's colours, sampled from the engine's own frame art the first time
 * the art is available, with literal fallbacks until then. */
const struct hwr_overlay_colors *hwr_overlay_draw_colors(
    const struct roth_api_v1 *api);

/* Vertical metrics double in the line-doubled hi-res mode, where the font is
 * drawn twice as tall. */
uint32_t hwr_overlay_draw_line_scale(const struct roth_api_v1 *api);

void hwr_overlay_draw_clear(const struct hwr_overlay_canvas *canvas,
                            uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1);
void hwr_overlay_draw_fill(const struct hwr_overlay_canvas *canvas,
                           uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                           uint8_t color);
void hwr_overlay_draw_border(const struct hwr_overlay_canvas *canvas,
                             uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                             uint8_t color);
void hwr_overlay_draw_hline(const struct hwr_overlay_canvas *canvas,
                            uint32_t x, uint32_t y, uint32_t length,
                            uint8_t color);
/* The last thing done to a panel before it is published.
 *
 * Every pixel of the rect becomes the surface's own, which gives anything the
 * engine drew there through its own blitter -- text, frame art, arrows -- the
 * ownership the surface's own drawing has, and turns what nothing drew into the
 * panel background. It also honours the one contract the presentation layer
 * needs from a panel: no published pixel carries HWR_CHROME_PANEL_TOKEN, so the
 * token closes the rect whatever the panel happens to hold. */
void hwr_overlay_draw_finish(const struct hwr_overlay_canvas *canvas,
                             uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                             uint8_t background);

/* Engine-font text. The pen lands one pixel in from (x, y), so a caller leaves
 * a pixel of slack above and to the left. The text is trimmed to max_width. */
void hwr_overlay_draw_text(const struct roth_api_v1 *api,
                           const struct hwr_overlay_canvas *canvas,
                           uint32_t x, uint32_t y, uint32_t max_width,
                           uint8_t color, const char *text);
/* The width the engine font needs for a string, in pixels. */
uint32_t hwr_overlay_draw_measure(const struct roth_api_v1 *api,
                                  const char *text);
/* How much of a string fits in max_width, in characters. */
size_t hwr_overlay_draw_fit(const struct roth_api_v1 *api, const char *text,
                            uint32_t max_width);

/* The engine's own nine-slice window frame with its ornate left spine, drawn
 * around the panel. Returns 0 when the engine's image pool is not loaded, which
 * is when the plain bordered panel stands in for it. */
int hwr_overlay_draw_frame(const struct roth_api_v1 *api,
                           const struct hwr_overlay_canvas *canvas,
                           uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                           uint8_t background);
/* The frame's fixed band widths, so the caller's layout and the art agree. */
void hwr_overlay_draw_frame_bands(uint32_t *left, uint32_t *top,
                                  uint32_t *right, uint32_t *bottom);
/* The row quantisation the nine-slice frame's right column steps in. */
uint32_t hwr_overlay_draw_frame_snap(uint32_t height);

/* The engine's own scroll arrows. */
void hwr_overlay_draw_arrow_size(const struct roth_api_v1 *api, uint32_t *width,
                                 uint32_t *height);
void hwr_overlay_draw_arrow(const struct roth_api_v1 *api,
                            const struct hwr_overlay_canvas *canvas,
                            uint32_t x, uint32_t y, uint32_t width,
                            uint32_t height, int up, int active,
                            const struct hwr_overlay_colors *colors);

#endif
