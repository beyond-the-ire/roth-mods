#ifndef HWR_VISIBILITY_H
#define HWR_VISIBILITY_H

#include "hwr_gl.h"
#include "hwr_world.h"
#include "roth_sdk.h"

#include <stddef.h>
#include <stdint.h>

struct hwr_objects;

/* The most disjoint screen-X windows retained for one object. An object visible
 * through several open portals has one exact worklist window per visit; beyond
 * this cap that object falls back to no clip. */
#define HWR_SECTOR_CLIP_MAX 32u

/* A set of visible screen-X portal windows. The store is indexed per object;
 * `count` == 0 means "no exact window captured" and is read as NO CLIP. Bounds
 * are in the engine's screen-X projection space. */
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

/* Rebuilds the software renderer's mirror-only portal walk. Unlike the primary
 * visibility pass, the engine's reflected tree is no longer present when the
 * hardware callback runs, so it is recovered from static map topology. The
 * root is the mirror record's target sector and [clip_left, clip_right] is the
 * aperture in the reflected camera's pre-X-flip projection space.
 *
 * On success visible[] contains only sectors reached through that aperture.
 * Returns 0 for invalid inputs or a failed root visit; callers should retain
 * only the root sector in that case, never expose the whole map behind a
 * mirror. */
int hwr_compute_reflection_pvs(const struct hwr_world *world,
                               const struct hwr_gl_camera *camera,
                               size_t root_sector,
                               int clip_left, int clip_right,
                               uint8_t *visible,
                               struct hwr_sector_xspan *spans,
                               size_t span_count);

/* Returns non-zero unless the sector portal-window clip for billboards is
 * switched off through HWR_PORTAL_CLIP=0. Read once and cached. When off, the
 * billboard layer never narrows a sprite to its sector's screen-X window
 * (identical to the layer's earlier behaviour). */
int hwr_portal_clip_enabled(void);

/* Recovers the exact per-object screen-X windows already computed by the
 * software renderer. Each type-1 low-arena worklist node stores its appearance
 * record pointer and packed window; matching that pointer to the live object
 * list avoids substituting the camera portal tree's sector-wide intervals.
 * `spans` is indexed like objects->items and must have at least objects->count
 * entries. Unmatched objects retain count 0 and therefore fail open. */
int hwr_compute_object_portal_windows(
    const struct hwr_world *world, const struct hwr_objects *objects,
    const struct roth_api_v1 *api, struct hwr_sector_xspan *spans,
    size_t span_count);

/* Recovers type-4 worklist membership for the post-tick primary hinged-door
 * snapshot. Entries are matched by exact pool-record pointer. Their ranges are
 * admission evidence only; stock does not scissor door geometry to them. */
int hwr_compute_door_portal_windows(
    const struct hwr_world *world, const struct roth_api_v1 *api,
    struct hwr_sector_xspan *spans, size_t span_count);

#endif
