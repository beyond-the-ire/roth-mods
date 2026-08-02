#include "hwr_lights.h"

/* The derived-light set: one array, one falloff law, and a seeder per class.
 *
 * The enumerator is a switch over the classes rather than a call per class on
 * purpose. A class that lands later -- the weapon's own flash, a window open to
 * the sky -- is then one case here and one seeder beside it, with nothing said
 * about the array, the per-view transform, or the shader. The switch names every
 * class the header declares and carries no default, so a class added to that
 * enumeration cannot be forgotten here: the build stops until it is answered.
 *
 * Nothing asks for a class yet, so every frame's answer is that there are no
 * such lights -- which is exactly what the pass drew before this existed. */
size_t hwr_lights_build(const struct hwr_lights_frame *frame,
                        struct hwr_gl_point_light *out, size_t max)
{
    size_t count = 0;
    unsigned class_index;

    if (frame == NULL || out == NULL || max == 0u)
        return 0;
    for (class_index = 0; class_index < (unsigned)HWR_LIGHT_CLASS_COUNT;
         class_index++) {
        if ((frame->classes & (1u << class_index)) == 0u)
            continue;
        switch ((enum hwr_light_class)class_index) {
        case HWR_LIGHT_CLASS_OBJECT:
            /* Seeded from the map's own unshaded mark. */
            break;
        case HWR_LIGHT_CLASS_GLOW_FACE:
            /* Seeded from the emissive faces of a sector, at their centroid. */
            break;
        case HWR_LIGHT_CLASS_MUZZLE:
            /* One frame at the player, when the weapon fires. */
            break;
        case HWR_LIGHT_CLASS_ENVIRONMENT:
            /* Seeded at the faces a map leaves open to the sky. */
            break;
        case HWR_LIGHT_CLASS_COUNT:
            /* Not a class: the loop above never reaches it. */
            break;
        }
    }
    return count;
}
