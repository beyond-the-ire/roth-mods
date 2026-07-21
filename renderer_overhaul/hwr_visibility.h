#ifndef HWR_VISIBILITY_H
#define HWR_VISIBILITY_H

#include "hwr_gl.h"
#include "hwr_world.h"
#include "roth_sdk.h"

#include <stdint.h>

/* Returns non-zero unless the portal-reachable visibility pass is switched off
 * through the HWR_PVS=0 environment kill-switch. The value is read once and
 * cached; setting HWR_PVS=0 restores the plain stream-everything behaviour, in
 * which every sector's surfaces are submitted each frame. */
int hwr_pvs_enabled(void);

/* Floods the sector graph from the camera's own sector outward through the
 * portal openings that fall inside a shrinking screen-space X window, the same
 * way the engine walks its render tree: a far sector that the flood cannot
 * reach through a narrow opening is left out of the set. The window is only
 * ever narrowed horizontally, never by height, so the result is a conservative
 * superset of what the engine actually draws -- it can keep a sector the engine
 * would drop, but never drops one the engine keeps.
 *
 * visible[] must hold world->sector_count bytes; on success it is filled with 1
 * for every reached sector and 0 elsewhere. Returns 1 when the result is a
 * valid cull set, or 0 when the pass fell back to all-visible (disabled, no
 * seed sector, or a degenerate camera). Never crashes and never blanks the
 * world. */
int hwr_compute_pvs(const struct hwr_world *world,
                    const struct hwr_gl_camera *camera,
                    const struct roth_api_v1 *api,
                    uint8_t *visible);

#endif
