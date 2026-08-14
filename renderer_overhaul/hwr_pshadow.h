#ifndef HWR_PSHADOW_H
#define HWR_PSHADOW_H

#include "hwr_scene.h"

#include <stddef.h>

/* The point-light shadows' policy side: which of the frame's lights carry a
 * shadow map, which atlas tile each owns, and when a tile's contents have
 * gone stale. The GL layer executes what this publishes and owns none of the
 * policy; the law both sides compile is hwr_point_math.h.
 *
 * ============================= WHO CASTS ==================================
 *
 * A COUNT, not a list. The settings row says how many of the lights burning
 * in the picture cast real shadows, and the ones that do are the ones worth
 * most where the player is standing -- the same ranking that decides which
 * lights burn at all, so shadows join the picture outward from the strongest
 * and leave outward from the weakest. Nought is the off position for the
 * whole feature; a count at or past the light limit means every light the
 * frame publishes casts, which is how "all of them" is spelled.
 *
 * A game config may still overrule the count for one light, and that word is
 * three-state rather than a switch:
 *
 *   nothing said     this light is one of the count's own candidates, which
 *                    is the ordinary case and every light in an undescribed
 *                    game.
 *   "shadows": true  PINNED: it takes a tile before the count's picks are
 *                    made, because somebody stood in the place and said this
 *                    lamp is the one that matters. It is not free -- it
 *                    spends one of the count's tiles -- so a count of four
 *                    with two pinned lights leaves two for the ranking.
 *   "shadows": false refused, whatever the count says and wherever the light
 *                    stands in the ranking.
 *
 * At nought nothing casts, a pinned light included: the row is the player's
 * own way out of a creator's choice, and a switch that spared somebody else's
 * pins would not be one.
 *
 * THE HYSTERESIS. Walking moves the ranking, and the light holding the last
 * tile would otherwise trade it with the next light every few steps -- a
 * shadow blinking on and off is worse than either light having one. So a
 * light that HOLDS a tile keeps it until a challenger clearly outranks it, by
 * the light array's own quarter margin. What it costs is a tile sitting with
 * the light that has it rather than with one worth a fraction more, which is
 * a trade nobody can see; what it buys is a set of shadows that stops moving
 * where the ranking is close.
 *
 * THE ATLAS IS SIZED TO THE COUNT, and both numbers it is sized from are
 * settings -- the count and the light limit -- so the storage moves where a
 * settings change is safe and never while somebody is walking. A change
 * re-takes it, and every tile is stale afterwards: the ledger is released
 * whole rather than partly, because what a tile HELD died with the storage
 * and a tile index that comes back must be rendered rather than believed.
 *
 * The caching story, which is most of the point: a tile is rendered when its
 * STAMP moves -- the static slice's own content stamp, folded with the
 * light's position and reach and with the bytes of its card slice -- and at
 * no other time. A static scene therefore renders its maps once per map (or
 * per door/lift change, which moves the slice stamp exactly as the sun's
 * rebuild does) and pays per frame for the lookups alone; an animated torch
 * near a shadowed light moves the card bytes and refreshes just that tile.
 *
 * What casts here: the sun's own static slice, whole -- every wall, flat,
 * slab and door, the masked cut-out and translucent-paint laws included --
 * plus the light's neighbourhood of object CARDS, each faced to this light.
 * What does not, stated rather than omitted: decoded 3D mesh props (the open
 * edge here; their emitter is the sun's and has not been aimed at a point
 * yet), and the light's OWN source object unless it is wall art
 * -- a free-standing emitter's card contains its own light and could only
 * shadow the whole room, while wall art stands clear of its light by the
 * wall-clearance law and its bracket's shadow is honest. */

/* One frame's whole shadow policy: reads the count in force and each light's
 * own word off the built array, picks who casts, assigns tiles, keeps them
 * current, publishes to the GL layer and writes each light's tile back into
 * the array (minus one everywhere no map stands). Called after the frame's
 * lights are built and before they are handed to the GL layer. */
void hwr_pshadow_frame(struct hwr_scene *scene, const struct hwr_world *world,
                       const struct hwr_objects *objects,
                       const struct roth_api_v1 *api,
                       const struct hwr_gl_camera *camera,
                       struct hwr_gl_point_light *lights, size_t count);

/* Forgets every tile (a new map's tiles describe nothing). Called where a
 * map is staged or torn down, beside the sun's own. */
void hwr_pshadow_reset(void);

/* How many tile renders the policy has requested since it came up. The
 * caching claim -- a static scene renders its maps once -- is a claim about
 * something that did NOT happen, and only a counter can state one. */
unsigned long hwr_pshadow_tile_builds(void);

#endif
