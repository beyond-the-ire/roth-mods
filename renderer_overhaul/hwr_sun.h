#ifndef HWR_SUN_H
#define HWR_SUN_H

#include "hwr_scene.h"

#include <stddef.h>

/* The sun/moon key light's world side: the per-map sky census that decides
 * whether the feature exists here at all, the static caster mesh built from
 * the map's own geometry, the dynamic caster slice of object cards rebuilt
 * every frame, and the camera-centred texel-snapped moving window. The GL
 * layer executes what this module publishes and owns none of the policy.
 *
 * The whole family lives under the Modern shading law and behind its own
 * rows; a map with no sky face builds nothing and runs nothing, which is the
 * sealed-cellar answer arriving by construction rather than by a table. */

/* Registers the settings hook that keeps the rows' answer current. Called
 * once at plugin start, like the true-colour hook beside it. */
int hwr_sun_register(void);

/* Takes the sky-face census of the map now staged and forgets everything
 * built for the previous one. Called where a map is staged, beside the other
 * per-map walks; it builds nothing itself. */
void hwr_sun_stage_map(const struct hwr_world *world);

/* Forgets the staged map entirely (the map data was torn down). */
void hwr_sun_reset(void);

/* The census answer: EDGE_MAP faces of the staged map. Zero is a sealed map
 * and stands the whole machinery down. */
size_t hwr_sun_sky_faces(void);

/* How many times the static caster mesh has been built. A claim that a
 * sealed map builds nothing is a claim about something that did not happen,
 * and only a counter can state one. */
unsigned long hwr_sun_static_builds(void);
/* How many vertices of the static slice are palette-colour solid-blend
 * DIMS (translucent with no texels): the offline gate holds the class to
 * the map's own walls. */
unsigned long hwr_sun_static_dims(void);

/* One frame's whole sun work: ensures the static slice is current, rebuilds
 * the dynamic slice, advances the moving window, and publishes everything to
 * the GL layer. Called once per rendered frame, before the views are drawn,
 * with the PRIMARY view's camera -- the window follows the player, and every
 * view of the frame reads the one map. */
void hwr_sun_frame(struct hwr_scene *scene, const struct hwr_world *world,
                   const struct hwr_objects *objects,
                   const struct roth_api_v1 *api,
                   const struct hwr_gl_camera *camera);

#endif
