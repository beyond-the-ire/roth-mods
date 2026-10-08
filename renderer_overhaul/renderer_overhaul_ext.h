/* renderer_overhaul_ext.h -- the interface Renderer Overhaul publishes to other mods.
 *
 * This is the one file another mod copies. It depends on roth_sdk.h alone.
 *
 * WHY IT EXISTS. While the renderer is drawing the world, the engine's own world pixels are
 * never painted, and the 8-bit frame a compose callback sees is only a stand-in the host
 * compares against: a pixel left exactly as the engine published it shows the renderer's
 * full-color world, and a pixel that was changed shows that 8-bit value instead. Two things a
 * mod could do to the software picture therefore stop working, and this interface is how to
 * ask the renderer for them:
 *
 *   - decorating one object as it is drawn (a tint, a glow, a different size);
 *   - darkening the world behind a translucent panel.
 *
 * FINDING IT (SDK 0.8, api->interfaces). The renderer publishes the table in on_load; look
 * it up from on_game_ram_ready or later and keep working when it is absent:
 *
 *     uint32_t version = 0;
 *     const struct hwr_ext_v1 *ext = api->interfaces
 *         ? api->interfaces->lookup(api, HWR_EXT_NAME, &version) : NULL;
 *
 * A NULL answer means the renderer is not installed. Check ext->struct_size before calling a
 * member a later revision appended. Every record below is plain data with a fixed layout.
 *
 * `owner` identifies the calling mod so two mods never overwrite each other's requests. Pass
 * any address that is yours alone and stays the same, such as your roth_api_v1 pointer.
 */
#ifndef RENDERER_OVERHAUL_EXT_H
#define RENDERER_OVERHAUL_EXT_H

#include "roth_sdk.h"

#include <stdint.h>

#define HWR_EXT_NAME    "ire.roth.renderer_overhaul.ext"
#define HWR_EXT_VERSION 1u

/* Whether the renderer's world is the one on screen, and where it is. */
struct hwr_ext_world_state_v1 {
    /* Nonzero while a world image drawn by the renderer is on screen, live or held under a
     * menu. Zero whenever the engine's own picture is showing: the renderer is switched off,
     * has fallen back, or an engine screen owns the display. */
    uint32_t live;
    /* The rectangle of the 8-bit frame the world shows through, in that frame's pixels. Only
     * meaningful while live is nonzero. */
    int32_t  x;
    int32_t  y;
    uint32_t w;
    uint32_t h;
};

/* How one object is to be drawn this frame. */
struct hwr_ext_object_look_v1 {
    /* The object record's address, as the engine's actor pool (slot + 4) and its draw list
     * (entry + 8) hold it. Records move when a monster changes sector, so resolve this every
     * frame and never keep one. */
    uint32_t object;
    /* Steps along the engine's own sprite scale: +1 doubles, -1 halves, 0 leaves it alone. A
     * sprite that cannot take the step is drawn at its own size. */
    int8_t   size_step;
    /* 0 = no tint; 1..100 = how far the sprite is pulled toward tint_rgb, keeping its
     * light and dark. */
    uint8_t  tint_strength;
    /* 0 = no glow; 1..255 = how bright the glow is right now. The caller owns the pulse:
     * send a new level each frame. */
    uint8_t  glow_level;
    /* Width of the halo around the sprite, in pixels of a 320x240 screen (so 2 is four pixels at
     * 640x480): 0..48, or 0..255 for a bloom (below). 0 brightens the sprite alone. */
    uint8_t  glow_size;
    uint32_t tint_rgb;      /* 0x00RRGGBB */
    uint32_t glow_rgb;      /* 0x00RRGGBB */
    /* Where the sweeping highlight is in its cycle, 0..65535. The caller owns the motion. */
    uint16_t shine_phase;
    uint8_t  shine;         /* 0 = none, nonzero = the sweep is drawn */
    /* Nonzero makes the halo a bloom: a burst of light that fades smoothly from the sprite out to
     * glow_size, is measured from the sprite's box rather than its outline, and is drawn over
     * whatever stands in front of the sprite. At 255 it fills the screen. */
    uint8_t  bloom;
};

/* A rectangle of world to darken. */
struct hwr_ext_dim_v1 {
    /* Nonzero darkens the whole world image, including the parts of a widescreen picture
     * that lie outside the 8-bit frame; x, y, w and h are then ignored. */
    uint32_t whole_world;
    /* Otherwise the rectangle, in pixels of the 8-bit frame. The part outside the world
     * rectangle is ignored. */
    int32_t  x;
    int32_t  y;
    uint32_t w;
    uint32_t h;
    /* 0..255: how much light is taken away. 0 changes nothing, 255 is black. */
    uint32_t strength;
};

#define HWR_EXT_DIM_SLOTS 8u

struct hwr_ext_v1 {
    uint32_t struct_size;

    /* Fill *out with the current state. Callable from any context, the compose tick
     * included. Returns out->live. */
    int32_t (ROTH_CDECL *world_state)(struct hwr_ext_world_state_v1 *out);

    /* Replace the owner's complete set of object looks. GAME context.
     *
     * A set is used for the next world image the renderer draws and then dropped, so send
     * the whole set every frame, before the world is drawn; a wrap of
     * setup_frame_render_context is the natural place. A mod that stops sending leaves
     * nothing behind. look_size is sizeof(struct hwr_ext_object_look_v1) as the caller
     * built it. Returns the number of looks accepted (at most 64 per owner).
     *
     * The software renderer is not affected. A mod that decorates the software picture too
     * can keep doing so unconditionally: while the renderer owns the world the engine paints
     * no world pixels, so there is nothing for that code to find. */
    int32_t (ROTH_CDECL *submit_object_looks)(const void *owner,
                                              const struct hwr_ext_object_look_v1 *looks,
                                              uint32_t count, uint32_t look_size);

    /* Set one of the owner's dim rectangles (slot 0..HWR_EXT_DIM_SLOTS-1); NULL clears it.
     * GAME context or the compose tick. A rectangle stays until it is cleared, and is
     * applied only while world_state() reports live.
     *
     * The other half of the bargain is the caller's: inside the world rectangle, leave
     * every pixel that should show the darkened world untouched, and darken 8-bit pixels
     * yourself only outside it or while live is zero. Returns nonzero when the slot was
     * taken. */
    int32_t (ROTH_CDECL *set_world_dim)(const void *owner, uint32_t slot,
                                        const struct hwr_ext_dim_v1 *dim);
};

#endif
