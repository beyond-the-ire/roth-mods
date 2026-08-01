#ifndef HWR_OVERLAY_H
#define HWR_OVERLAY_H

#include "roth_sdk.h"

#include <stdint.h>

/* The consumer settings panel: what is open, what has focus, and what the
 * player is editing. It knows the settings only through the schema table, knows
 * nothing about the configuration file format, and draws through
 * hwr_overlay_draw.
 *
 * Three contexts, and the split is the point:
 *   TICK_ISR  hwr_overlay_scancode  records keys and flips the open flag
 *   TICK_ISR  hwr_overlay_compose   copies the drawn panel onto the frame
 *   GAME      hwr_overlay_pump      everything else, including every engine call
 */

/* What the panel needs to know about the renderer around it. The entry file
 * fills this in before each pump, so the overlay needs none of its statics. */
struct hwr_overlay_runtime {
    const char *version;
    /* The screen is the mod's own to draw over: no cutscene sequence owns it
     * and no map is being staged. */
    int screen_available;
    /* The engine's own pointer, localised to the engine layer. */
    int cursor_valid;
    int32_t cursor_x;
    int32_t cursor_y;
};

void hwr_overlay_init(void);
void hwr_overlay_shutdown(void);

/* TICK_ISR: one keystroke, before the engine sees it. Returns the scancode to
 * pass on, or 0 to swallow it. Touches nothing but overlay state. */
uint8_t hwr_overlay_scancode(uint8_t scancode);
/* TICK_ISR: copy the panel onto the engine frame. */
void hwr_overlay_compose(const struct roth_api_v1 *api, uint8_t *pixels,
                         uint32_t width, uint32_t height);

/* GAME: carry out everything the two tick callbacks only recorded, then draw. */
void hwr_overlay_pump(const struct roth_api_v1 *api,
                      const struct hwr_overlay_runtime *runtime);
/* GAME: one int33 poll. Hides the mouse from the game while the panel is up,
 * then pumps, so the panel keeps up with the pointer on screens that drive no
 * per-frame callback of their own. */
void hwr_overlay_mouse(const struct roth_api_v1 *api,
                       struct roth_mouse_poll_v1 *poll,
                       const struct hwr_overlay_runtime *runtime);
/* GAME, but from a poll the caller cannot pump on: take the buttons and hide
 * them from the game, and nothing else. The button state is a level the next
 * pump turns into a press, so a poll taken here loses no click. */
void hwr_overlay_mouse_capture(const struct roth_api_v1 *api,
                               struct roth_mouse_poll_v1 *poll);

int hwr_overlay_is_open(void);
/* Where the panel stands, in framebuffer coordinates. Returns 0 for nowhere. */
int hwr_overlay_rect(uint32_t *x, uint32_t *y, uint32_t *width,
                     uint32_t *height);

#endif
