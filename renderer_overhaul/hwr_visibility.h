#ifndef HWR_VISIBILITY_H
#define HWR_VISIBILITY_H

#include "hwr_gl.h"
#include "hwr_world.h"
#include "roth_sdk.h"

#include <stddef.h>
#include <stdint.h>

/* The most disjoint screen-X windows one sector's billboards are clipped to. A
 * sector visible through several open portals contributes one window per portal
 * visit; beyond this cap the sector falls back to no clip (fail open). */
#define HWR_SECTOR_CLIP_MAX 8u

/* Per-sector visible screen-X portal window(s), lifted from the renderer's own
 * per-node interval chains. `count` == 0 means "no window captured" and is read
 * as NO CLIP (full width) so a sprite is never wrongly blanked. Bounds are in
 * the engine's screen-X space (the Xscale/Xcenter projection); the billboard
 * emitter maps them to framebuffer-X. */
struct hwr_sector_xspan {
    int16_t lo[HWR_SECTOR_CLIP_MAX];
    int16_t hi[HWR_SECTOR_CLIP_MAX];
    uint8_t count;
};

/* Returns non-zero unless the per-frame visibility pass is switched off through
 * the HWR_PVS environment kill-switch. The value is read once and cached;
 * setting HWR_PVS=0 restores the plain stream-everything behaviour, in which
 * every sector's surfaces are submitted each frame. */
int hwr_pvs_enabled(void);

/* Reads the renderer's own per-frame sector visibility: the software renderer
 * still runs its full world walk under the mod, leaving an ordered draw list in
 * the map-geometry section. This lifts the set of sectors that list actually
 * draws -- exactly the sectors the engine kept after its portal walk and
 * interval merge -- so a sector stacked over or under the camera's own but not
 * reached along the sight path no longer contributes surfaces.
 *
 * visible[] must hold world->sector_count bytes; on success it is filled with 1
 * for every drawn sector and 0 elsewhere. Returns 1 when the result is a valid
 * cull set, or 0 when the pass fell back to all-visible (disabled, no map, an
 * empty or implausible list, or an offset that could not be mapped to a
 * sector). Never crashes and never blanks the world. */
int hwr_compute_pvs(const struct hwr_world *world,
                    const struct hwr_gl_camera *camera,
                    const struct roth_api_v1 *api,
                    uint8_t *visible);

/* Returns non-zero unless the sector portal-window clip for billboards is
 * switched off through HWR_PORTAL_CLIP=0. Read once and cached. When off, the
 * billboard layer never narrows a sprite to its sector's screen-X window
 * (identical to the layer's earlier behaviour). */
int hwr_portal_clip_enabled(void);

/* Lifts each drawn sector's visible screen-X portal window(s) from the same
 * per-node interval chains hwr_compute_pvs walks, capturing the +4/+6 interval
 * words it discards. `spans` must hold `span_count` entries (>= sector_count);
 * every entry is reset to count 0 first. Overlapping intervals for one sector
 * are merged; a sector that overflows HWR_SECTOR_CLIP_MAX distinct windows falls
 * back to count 0 (no clip). Returns 1 when a usable window set was produced, or
 * 0 when the pass fell back to no-clip (disabled, no map, an empty or implausible
 * list, or an offset that could not be mapped). Never crashes. */
int hwr_compute_portal_windows(const struct hwr_world *world,
                               const struct roth_api_v1 *api,
                               struct hwr_sector_xspan *spans,
                               size_t span_count);

#endif
