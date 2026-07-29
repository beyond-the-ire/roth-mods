#ifndef HWR_CHROME_H
#define HWR_CHROME_H

#include "roth_sdk.h"

#include <stddef.h>
#include <stdint.h>

/* The rects the engine covers with its own window chrome, taken from the
 * engine's own announcement of them.
 *
 * The presentation layer infers "this pixel is still world" from the live
 * engine pixel being unchanged from the plane the mod seeded there. That test
 * is necessary and not sufficient, and no choice of seeded index can make it
 * sufficient. Window chrome is unconditionally opaque -- the image blitter
 * (das_assets.c) has no transparency test on any of its paths -- so a chrome
 * pixel that happens to be written with the index already seeded there reads as
 * untouched world and the full-resolution world shows through the window. The
 * drop-shadow border is worse: it is a read-modify-write over the framebuffer
 * (blit_2d.c:174/219/121), and a dark index that darkens to itself leaves the
 * byte equal to the baseline whatever the baseline holds, so the outer rings of
 * the shadow leak heavily and no index substitution can ever reach them.
 *
 * Every window announces the rect it is about to cover BEFORE it covers it: it
 * saves the pixels under that rect first (save_framebuffer_region,
 * blit_2d.c:1655), and the rect it saves already includes the four-pixel drop
 * shadow -- menu_hud_ui.c:1435/1464 save (box - 4, box - 4, w + 8, h + 8), and
 * dialogue_ui.c:1378 does the same for the inspect close-up. Recording those
 * rects and making the submitted baseline differ from the live framebuffer
 * wherever the two would otherwise agree inside one closes the window
 * completely, which is what the original's windows are.
 *
 * The rect set follows the engine's own save-under lifecycle:
 *   arm      save_framebuffer_region returns a pool handle (blit_2d.c:1655)
 *   re-arm   blit_save_region re-captures the same rect (renderer.c:11474);
 *            the corner portrait saves once and re-saves every frame after
 *            (menu_hud_ui.c:265-271)
 *   cover    the window draws
 *   uncover  blit_descriptor_rows copies the saved pixels back
 *            (renderer.c:1414) -- the world is world again from that moment
 *   release  pool_free_handle frees the block and zeroes the handle slot
 *            (renderer.c:10440-10448); free_das_cache_handle (das_assets.c:685)
 *            is the uncover-then-release pair every window closes through
 */
enum { HWR_CHROME_MAX_RECTS = 32 };

struct hwr_chrome_rect {
    uint32_t handle;
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    int covered;
};

struct hwr_chrome {
    struct hwr_chrome_rect rects[HWR_CHROME_MAX_RECTS];
    unsigned count;
    uint8_t *plane;
    size_t plane_capacity;
    int overflow_logged;
};

/* HWR_CHROME_RECTS=0 records nothing and stamps nothing, so every submitted
 * baseline is the plain quantized plane and the frame is bit-identical to the
 * behaviour before window rects existed. */
int hwr_chrome_enabled(void);

/* A window has announced the rect it is about to cover. x/width are aligned and
 * y/height doubled exactly as the save routine does it (blit_2d.c:1658-1661),
 * so the recorded rect is the region the engine really saved. */
void hwr_chrome_record(struct hwr_chrome *chrome, const struct roth_api_v1 *api,
                       uint32_t handle, uint32_t x, uint32_t y,
                       uint32_t width, uint32_t height);

/* The window re-captured what is under it and is about to draw again. */
void hwr_chrome_cover(struct hwr_chrome *chrome, uint32_t handle);

/* A window frame has been drawn over the rect (x, y, w, h) plus its four-pixel
 * drop shadow, so every recorded rect that CONTAINS it holds chrome again. This
 * is the other half of the cover signal and it is needed: the open/close zoom
 * animation puts the saved pixels back after every step and then redraws the
 * frame from the SAME save-under handle (menu_hud_ui.c:962-972), so the restore
 * alone would leave a standing window looking uncovered forever. */
void hwr_chrome_cover_draw(struct hwr_chrome *chrome,
                           const struct roth_api_v1 *api, int32_t x, int32_t y,
                           int32_t width, int32_t height);

/* The saved pixels have been put back: the rect is world again. */
void hwr_chrome_uncover(struct hwr_chrome *chrome, uint32_t handle);

/* The save-under block has been freed; the handle means nothing from here. */
void hwr_chrome_release(struct hwr_chrome *chrome, uint32_t handle);

/* Forget every rect. Dropping a rect only ever costs the old leak inside a
 * window that is still standing -- it can never leave world hidden -- so this
 * is the safe direction for any state change the rect set cannot follow. */
void hwr_chrome_reset(struct hwr_chrome *chrome);

void hwr_chrome_clear(struct hwr_chrome *chrome);

/* The baseline to submit for this frame.
 *
 * Returns base unchanged when nothing needs closing -- no rect, or no pixel
 * inside one where the live framebuffer and the baseline agree -- and otherwise
 * an internal copy in which exactly those agreeing pixels carry a flipped low
 * bit. That value is provably unequal to the live pixel it was derived from,
 * because it was derived from it, so the reveal test cannot fire there.
 *
 * Only the agreeing pixels are touched, and that is deliberate: a pixel the
 * chrome already differs from is left describing the world, so a window whose
 * pixels are put back before the page is published reveals its world again with
 * no help from the rect set.
 *
 * Call with the framebuffer in the state the page about to be published holds,
 * i.e. from the present entry. Anywhere else the comparison is against pixels
 * the engine has not drawn its chrome into yet and would stamp the whole rect. */
const uint8_t *hwr_chrome_baseline(struct hwr_chrome *chrome,
                                   const struct roth_api_v1 *api,
                                   const struct roth_world_frame_v1 *frame,
                                   const uint8_t *base);

#endif
