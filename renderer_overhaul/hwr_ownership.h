#ifndef HWR_OWNERSHIP_H
#define HWR_OWNERSHIP_H

#include "roth_sdk.h"

#include <stddef.h>
#include <stdint.h>

/* Which layer owns the picture on screen right now.
 *
 * The original never paints a transition: a map change, a save load, or a
 * pause simply stops producing new frames and the last presented one keeps
 * standing.  A hardware renderer that swaps in its own pixels therefore needs
 * an explicit model of who is drawing, because the two failure modes it can
 * introduce -- suppressing an engine frame it cannot replace, and publishing a
 * frame that no longer belongs to what the engine is showing -- are both
 * invisible from inside a single render dispatch. */
enum hwr_owner_state {
    /* The original renderer draws and presents on its own; no world frame is
     * published.  Boot, a disabled or failed renderer, scaled viewport modes. */
    HWR_OWNER_SOFTWARE = 0,
    /* The engine owns the content of its own framebuffer and the mod only
     * keeps the canvas alive around it: every pixel inside the engine viewport
     * comes from the engine layer, unchanged. */
    HWR_OWNER_PASSTHROUGH,
    /* Normal play: the world spans are suppressed and replaced by the
     * hardware image. */
    HWR_OWNER_HARDWARE,
    /* The engine has stopped drawing the world (menu, inventory, dialogue)
     * but still composes and presents; the last hardware image is held, with
     * live palette state re-applied to it. */
    HWR_OWNER_FROZEN,
    /* An engine routine publishes a display surface the mod cannot see (the
     * video decoder writes its own frames, not the engine framebuffer), so the
     * only correct answer is to stop compositing entirely. */
    HWR_OWNER_ENGINE_SCREEN
};

struct hwr_ownership {
    int state;
    /* Set once the mod has produced a real hardware frame.  Until then it must
     * never claim the present channel: the canvas would widen under the
     * original renderer's own screens. */
    int hardware_established;
    /* Set once the engine has desaturated its own screen behind a panel, so
     * the held world image has to be presented through the same remap. */
    int grayscale_held;
    /* Palette state the standing world image was resolved with. Comparing it
     * against live engine state is what tells a held image it has fallen
     * behind, without having to inspect a single pixel. */
    int32_t present_damage;
    int present_grayscale;
    int present_valid;
    int log;
    uint32_t transitions;
    uint32_t *blank;
    size_t blank_pixels;
};

void hwr_ownership_init(struct hwr_ownership *own);
void hwr_ownership_clear(struct hwr_ownership *own);

const char *hwr_ownership_name(int state);
void hwr_ownership_enter(struct hwr_ownership *own, int state,
                         const char *reason);

/* True when the map geometry the snapshot was taken from is no longer the map
 * the engine is about to draw, or when a load cleared the render-active latch
 * since the last dispatch.  Both are readable before the engine draws, which
 * is what lets suppression stay off for a frame the mod cannot replace. */
int hwr_ownership_world_stale(const struct roth_api_v1 *api,
                              const void *sector_section,
                              const void *vertex_section);

/* True while a cutscene/fullscreen-image sequence holds the screen: the engine
 * has a backed-up gameplay screen or the overlay latch is up.  These are the
 * states whose transitions are the screen slides, and whose surround is black
 * in the original's own aspect. */
int hwr_ownership_engine_sequence(const struct roth_api_v1 *api);

/* Live pain accumulator.  The engine clamps it well past a byte and drives the
 * DAC from it every time it changes, so it is read whole and read late. */
int32_t hwr_ownership_damage_level(const struct roth_api_v1 *api);

/* The engine's grayscale remap table, or NULL when it has not been built. */
const uint8_t *hwr_ownership_grayscale_lut(const struct roth_api_v1 *api);

/* An opaque black canvas of the requested size, allocated once and reused.
 * Returns NULL when it cannot be provided. */
const uint32_t *hwr_ownership_blank_canvas(struct hwr_ownership *own,
                                           uint32_t width, uint32_t height);

#endif
