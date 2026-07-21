#ifndef HWR_VISIBILITY_H
#define HWR_VISIBILITY_H

#include "hwr_gl.h"
#include "hwr_world.h"
#include "roth_sdk.h"

#include <stdint.h>

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

#endif
