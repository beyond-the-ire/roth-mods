#ifndef HWR_PSHADOW_H
#define HWR_PSHADOW_H

#include "hwr_scene.h"

#include <stddef.h>

/* The point-light shadow PROTOTYPE's policy side: which of the frame's
 * lights carry a shadow map, which atlas tile each owns, and when a tile's
 * contents have gone stale. The GL layer executes what this publishes and
 * owns none of the policy; the law both sides compile is hwr_point_math.h.
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
 * What does not, stated rather than omitted: decoded 3D mesh props (the
 * prototype's open edge; their emitter is the sun's and has not been aimed
 * at a point yet), and the light's OWN source object unless it is wall art
 * -- a free-standing emitter's card contains its own light and could only
 * shadow the whole room, while wall art stands clear of its light by the
 * wall-clearance law and its bracket's shadow is honest. */

/* One frame's whole shadow policy: reads the shadow requests off the built
 * light array, assigns tiles, keeps them current, publishes to the GL layer
 * and writes each light's tile back into the array (minus one everywhere no
 * map stands). Called after the frame's lights are built and before they
 * are handed to the GL layer. */
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
