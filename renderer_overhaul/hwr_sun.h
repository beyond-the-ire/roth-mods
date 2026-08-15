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
/* How many vertices the static slice holds, which is the measurable form of
 * "these surfaces cast and those do not": the offline gate moves the game
 * config's open-ceiling list and holds the slice to what that list says. */
unsigned long hwr_sun_static_vertices(void);
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

/* THE STATIC CASTER SLICE, for anything else that shadows: one slice, one
 * builder, one classify -- the point-light shadow tiles draw exactly what
 * the sun draws, holes and dims included, because a wall is the same wall
 * whichever light it blocks. Ensures the slice is current to the map's own
 * mutable facts (the same hash the sun's frame keeps it current by; a
 * sealed map builds it here even though the sun never will) and hands it
 * over borrowed, with a stamp that moves exactly when its CONTENT does.
 * Returns 0 with everything cleared where there is nothing to build from. */
int hwr_sun_acquire_static(const struct hwr_scene *scene,
                           const struct hwr_world *world,
                           const struct hwr_gl_sun_vertex **out,
                           size_t *count, uint32_t *stamp);

/* Where one prop's caster triangles are to be written: hands back room for
 * `count` vertices, or NULL where there is none. The two callers differ in
 * nothing else -- the sun grows its single dynamic slice, a point light grows
 * the tile it is filling -- which is why the geometry itself is written once. */
typedef struct hwr_gl_sun_vertex *(*hwr_sun_caster_reserve)(void *context,
                                                            size_t count);

/* ONE DECODED 3D PROP AS CASTER GEOMETRY, for anything that shadows: the same
 * class law for every light, because a tree blocks a lamp exactly as it blocks
 * the moon. Takes a thing and the DAS block its graphic RESOLVED to; answers
 * whether that block is a 3D prop at all, and where it is, writes its faces
 * through `reserve`.
 *
 * The class law, stated once and compiled by both callers: a face carrying the
 * DAS transparency flag casts its texture, whose index-0 texels are holes in
 * the shadow exactly as a sprite card's are (a tree's leaves); a face without
 * it casts solid (a trunk); a textured face the eye never draws casts nothing;
 * pins are camera-facing decorations and cast nothing.
 *
 * Returns 1 where the thing IS a 3D prop -- emitted, or the pack holding no
 * geometry for it, and either way the caller's card path must not also draw it
 * -- 0 where it is an ordinary sprite the caller should card, and -1 where the
 * reserve refused. */
int hwr_sun_emit_mesh_caster(const struct hwr_scene *scene,
                             const struct hwr_object *object,
                             struct hwr_das_archive *archive, size_t entry_id,
                             const struct roth_api_v1 *api,
                             hwr_sun_caster_reserve reserve, void *context);

#endif
