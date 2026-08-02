#ifndef HWR_LIGHTS_H
#define HWR_LIGHTS_H

#include "hwr_gl.h"

#include <stddef.h>

struct hwr_objects;
struct hwr_world;

/* Where a light that is not at the eye comes from.
 *
 * There is one array of lights and one falloff law; a class is a way of finding
 * lights in a map, not a second kind of light. That is what lets a later class
 * arrive as a seeder alone, with nothing said to the shader -- and it is why the
 * classes are named here even where nothing seeds them yet. */
enum hwr_light_class {
    /* An object the map's own author marked as giving light rather than taking
     * it: a torch, a candle, a lit sigil. The mark is the composed shade byte
     * reaching zero, which is the engine's own unshaded class. */
    HWR_LIGHT_CLASS_OBJECT = 0,
    /* A sector whose faces the map marks emissive. The mod carries that mark at
     * sector granularity, so one lit window makes its whole sector glow -- which
     * is why this class is one somebody asks for rather than one they get. */
    HWR_LIGHT_CLASS_GLOW_FACE,
    /* The player's own weapon, for the frame it fires on. */
    HWR_LIGHT_CLASS_MUZZLE,
    /* A face open to the sky, admitting whatever is up there. A window is a
     * light rather than a direction: a sealed cellar has no such face and so
     * has nothing to seed from, rather than being attenuated toward nothing. */
    HWR_LIGHT_CLASS_ENVIRONMENT,
    HWR_LIGHT_CLASS_COUNT
};

/* What one frame's lights are built from, and which classes are asked for.
 *
 * The classes are a mask rather than a row per class here, because what asks is
 * the settings layer and what this answers is the world: a class nobody asked
 * for costs the walk nothing at all. */
struct hwr_lights_frame {
    const struct hwr_world *world;
    const struct hwr_objects *objects;
    unsigned classes;
};

/* Every light this frame has, from every class it was asked for, into `out`.
 * Returns how many were written, never more than `max`. */
size_t hwr_lights_build(const struct hwr_lights_frame *frame,
                        struct hwr_gl_point_light *out, size_t max);

#endif
