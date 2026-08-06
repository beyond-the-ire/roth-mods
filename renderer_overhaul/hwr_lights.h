#ifndef HWR_LIGHTS_H
#define HWR_LIGHTS_H

#include "hwr_gl.h"

#include <stddef.h>

struct hwr_das_archive;
struct hwr_objects;
struct hwr_world;
struct roth_api_v1;

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

/* What the rows in force ask of the seeding.
 *
 * The classes are a mask rather than a row per class here, because what asks is
 * the settings layer and what the seeding answers is the world: a class nobody
 * asked for costs the walk nothing at all. The mask is also where the shading
 * law is spent -- these lights exist only under the modern one -- so a frame
 * drawn with the original picture does not walk a map looking for torches. */
struct hwr_lights_settings {
    unsigned classes;
    /* How many may stand at once, never more than the pass carries. */
    unsigned max;
    /* What one of them starts at, before the map's own word and before the
     * look file's word about that particular graphic. */
    float intensity;
    float range;
    int color_from_sprite;
    float color[3];
    /* How much of the map's own candle word the two above take. */
    float glow_scale;
    /* The containments. Doorways from the eye's own room a light may still be
     * seeded across WHERE THERE IS NO DRAWN SET -- with one, the drawn set is
     * the containment and this stands down; how far past its room's floor and
     * ceiling a light reaches before it is faded out; and how long a light
     * dropped for want of a slot takes to go out. */
    unsigned hops;
    float z_slack;
    float fade_seconds;
};

/* Publishes what the rows ask for. Held whole rather than read per frame, for
 * the same reason the rig is: the settings layer works it out where a settings
 * change is safe, and the frame that follows reads one object. */
void hwr_lights_set_settings(const struct hwr_lights_settings *settings);
const struct hwr_lights_settings *hwr_lights_get_settings(void);

/* What one frame's lights are built from.
 *
 * Everything here is the WORLD as this frame found it. The seeding takes no
 * view of its own: a light belongs to the frame rather than to the view that
 * happens to see it, and each view puts it into its own space as it binds. The
 * containments are therefore measured against the PRIMARY view -- which is the
 * one the player is standing in, and the one a mirror is a reflection of. */
struct hwr_lights_frame {
    const struct hwr_world *world;
    const struct hwr_objects *objects;
    /* The sprite a thing is drawn with, which is where its light gets its
     * colour. Absent archives leave every light the chosen colour, which is
     * exactly what the other colour source is. */
    const struct roth_api_v1 *api;
    struct hwr_das_archive *map_archive;
    struct hwr_das_archive *shared_archive;
    uint16_t view_yaw;
    /* The sectors the engine's own portal walk drew, and whether that answer
     * is a real cull set at all. A light in a sector the walk did not reach is
     * a light in a room that cannot be seen. */
    const uint8_t *visible;
    size_t visible_count;
    int pvs_valid;
    /* The room the eye is in, or -1 where the engine has not said. The hop cap
     * is counted from here -- and only where the three above give no cull set to
     * contain by, because where a light stands is a fact about the map and where
     * the eye stands is not. */
    int camera_sector;
    /* The primary view's own world-to-view offset and eye height, which is all
     * a distance to the eye needs: the rotation that follows cannot change a
     * length. */
    int16_t view_offset_x;
    int16_t view_offset_y;
    int16_t eye_z;
    /* This map's own word about how strong its lantern was meant to be. */
    uint16_t candle_glow;
    /* The frame clock, for the fade. Zero means no clock and no fade. */
    uint64_t now_ns;
};

/* Every light this frame has, from every class it was asked for, into `out`.
 * Returns how many were written, never more than `max`. */
size_t hwr_lights_build(const struct hwr_lights_frame *frame,
                        struct hwr_gl_point_light *out, size_t max);

/* Forgets what stood last frame: the retained set that the fade and the
 * hysteresis are kept in, and the colours read off sprites. Called where a map
 * is staged -- a new map's sector 12 is not the old map's sector 12, and its
 * archives are not the old map's archives either. */
void hwr_lights_reset(void);

/* The colour one decoded sprite frame throws, through a map's own palette:
 * the average of its texels weighted by the square of the engine's own
 * green-weighted luminance, normalized so the strongest channel is one.
 *
 * Weighted that way because a torch is mostly the dark bracket it is mounted
 * on and a little flame, and it is the flame that gives the light: an unweighted
 * average of a torch sprite is the colour of wood. Index zero is the masked-out
 * texel every sprite is cut from and is never part of the answer.
 *
 * Returns nonzero when there was any colour to find; a frame that is entirely
 * cut away, or entirely black, leaves `out` alone and is not a light's colour. */
int hwr_lights_frame_color(const uint8_t *pixels, size_t count,
                           const uint8_t palette[768], float out[3]);

/* How many lights the last build considered before the cap, and how many it
 * dropped at each containment. Diagnostic: the seeding is where a light leaks
 * from, so what it refused is worth being able to read. */
struct hwr_lights_stats {
    unsigned long marked;      /* the map's own unshaded mark */
    unsigned long no_sector;   /* nothing to belong to */
    unsigned long overlay;     /* a reflection-only artefact */
    unsigned long refused;     /* the look file says this is not a light */
    unsigned long unseen;      /* the portal walk did not draw its room */
    unsigned long too_far;     /* past the hop cap */
    unsigned long capped;      /* more candidates than slots */
    unsigned long fading;      /* still going out from an earlier frame */
};
const struct hwr_lights_stats *hwr_lights_last_stats(void);

#endif
