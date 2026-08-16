#ifndef HWR_LIGHTS_H
#define HWR_LIGHTS_H

#include "hwr_gl.h"

#include <stddef.h>

struct hwr_das_archive;
struct hwr_meshes;
struct hwr_object;
struct hwr_objects;
struct hwr_world;
struct roth_api_v1;

/* Where a light that is not at the eye comes from.
 *
 * There is one array of lights and one falloff law; a class is a way of finding
 * lights in a map, not a second kind of light. That is what lets a later class
 * arrive as a seeder alone, with nothing said to the shader -- and it is why the
 * classes are named here even where nothing seeds them yet.
 *
 * WHAT EVERY SEEDER OWES THE LIGHT IT MAKES, whichever class it is: the thing
 * the light is the light OF (hwr_gl_point_light's source_object), which is a
 * map's own object number or HWR_GL_LIGHT_NO_SOURCE where nothing drawn is
 * behind it. It has to be SAID rather than left at what a clearing leaves,
 * because nought is a real object of every map: a class that forgot it would
 * quietly take some other thing's card out of its shadow map. */
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
    /* A light a game config hangs at three numbers of its own, tied to no
     * object and to no face. Every other class FINDS lights the map already
     * has; this one is the only one where somebody puts a light where the map
     * has none, which is why it is a class of its own rather than a corner of
     * the object class: what names a light across frames is its class and its
     * source number, and a light with no object behind it would otherwise be
     * indistinguishable from a torch that happened to stand in the same spot.
     *
     * It has no row of its own to be asked for. Every other class answers a
     * question about the world -- do the map's torches light it, does its lava
     * -- and a person can reasonably want one and not the other. A free-standing
     * light exists only because somebody wrote it down, so the only honest
     * switch for it is the whole rig's, and the way to be rid of one is to
     * delete it. */
    HWR_LIGHT_CLASS_PLACED,
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
    /* THE COLOUR IS NOT ASKED FOR, because there is nothing to ask: every light
     * takes its colour off the thing that gives it, and where a game config
     * names one for a particular thing that name is the last word. Neither is
     * a choice the rows can make, so neither is a field here. */
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
    /* How many of the lights that stand cast real shadows, worth-most first.
     * Nought is the whole feature off; a value at or past `max` is every
     * light that stands, which is what makes the row uncapped upward.
     *
     * The seeding does nothing with it at all -- it is the shadow layer that
     * picks and sizes its atlas to it (hwr_pshadow.c). It is carried here
     * because one place works out what the rows ask for, and because a
     * light's own word about shadows is read off its game config entry right
     * beside the other three things an entry can say. */
    unsigned shadow_lights;
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
     * colour AND its height. Absent archives leave every light the chosen
     * colour, which is exactly what the other colour source is, and leave it
     * standing at its object's own anchor. */
    const struct roth_api_v1 *api;
    struct hwr_das_archive *map_archive;
    struct hwr_das_archive *shared_archive;
    uint16_t view_yaw;
    /* AND THE VIEW'S OWN ACROSS AXIS, which is the frame a camera-facing card
     * is drawn in: the engine's own (sin, cos) at 16384, exactly the pair the
     * emitter spans such a card with (hwr_billboards.c). A game config's
     * sideways nudge on a thing drawn as a billboard is spent along it, since
     * "across the card" is a statement about the card and a card that turns to
     * face the eye turns its across axis with it. Both nought is no view staged
     * at all, and then a sideways nudge on a billboard has no frame to be in
     * and is not spent -- the same fallback the height law takes when no
     * projection is staged. */
    int16_t view_yaw_sin;
    int16_t view_yaw_cos;
    /* The VERTICAL half of the sprite-sizing law's projection match
     * (hwr_sprite_span.h), which is what turns a sprite's texels into the
     * height it stands in the world. The seeding needs it because a thing's
     * light is emitted from the middle of the sprite it is drawn as. Nought
     * means no projection is staged, and then a light stays at the anchor.
     *
     * Handed over rather than read here, because it is a fact about the VIEW --
     * the engine's own perspective scale against the canvas the mod draws into
     * -- and the seeding takes no view of its own. */
    float sprite_vscale;
    /* AND THE PACKED GEOMETRY OF THE THINGS THAT ARE NOT DRAWN AS SPRITES AT
     * ALL. A 3D prop -- a chandelier, a candelabrum, a brazier -- has no sprite
     * frame to be sized against, so the height law above answers nothing about
     * one and its light would stand at the map's own anchor. For a thing HUNG
     * from a ceiling that anchor is IN the ceiling, which is the one place a
     * light may not be (see the emit point below). The pack is where the
     * thing's own vertical extent is written down, so it is handed over for the
     * same reason the sprite scale is: the seeding takes no geometry of its own.
     * Absent -- an older caller, a map with no props -- leaves such a light at
     * its anchor, which is where it stood before this existed. */
    const struct hwr_meshes *meshes;
    /* The sectors the engine's own portal walk drew, and whether that answer
     * is a real cull set at all. A light that REACHES no sector the walk drew
     * is a light nothing on the screen is lit by -- which is a question about
     * where the light gets to and never about whether the thing giving it is
     * itself in view, since a lamp behind a pillar still lights the wall. */
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
 * hysteresis are kept in, the colours read off sprites, and the granted faces
 * of the map that was loaded. Called where a map is staged -- a new map's
 * sector 12 is not the old map's sector 12, and its archives are not the old
 * map's archives either. */
void hwr_lights_reset(void);

/* Whether this object is a light ONLY because a game config said so: the map
 * itself does not mark it, and a grant names it and does not refuse it.
 *
 * Asked by the DRAWING as well as by the seeding, and that is the whole reason
 * it is here rather than folded into the seeder: a thing that lights a room has
 * to LOOK lit, exactly as the things the map marks itself do, or a granted torch
 * is a dark sprite with a halo round it. One function, so the two answers cannot
 * drift into two rules.
 *
 * Answers 0 wherever the object class is not being seeded at all -- which is
 * every shading law but the modern one, and the modern one with the family or
 * its object row switched off. Nothing a game config says may move a picture
 * drawn by the other two laws, and this is where that is held. */
int hwr_lights_object_promoted(const struct hwr_world *world,
                               const struct hwr_object *object);

/* Works out, for the map now staged, where the faces a game config grants a
 * light actually are.
 *
 * THIS IS A FACT ABOUT THE MAP, and it is taken ONCE, here, rather than on
 * every frame: which faces carry a granted texture and where the centroid of
 * each room's worth of them lies does not change because somebody took a step.
 * Walking every wall, flat and slab of a map sixty times a second to find out
 * again would be the whole cost of the feature, and there would be nothing to
 * show for it. What a frame does instead is the containment test over the small
 * set this leaves behind.
 *
 * ONE LIGHT PER ROOM PER TEXTURE, at the centroid of that room's granted faces
 * and pushed off each face along its own normal. A lava lake is hundreds of
 * faces sharing one texture; one light each would fill the whole array with
 * near-identical lights and starve every other light in the room.
 *
 * Called with the world a map load has just snapshotted, and with the two
 * archives its pictures live in: a grant that states no colour takes one off the
 * texture's own texels, which is a read of the picture rather than of the map.
 * Absent archives leave such a grant with no colour to give, and it says so. A
 * map with nothing granted -- which is every map until somebody describes one --
 * costs no walk and no read at all. */
void hwr_lights_stage_glow_faces(const struct hwr_world *world,
                                 struct hwr_das_archive *map_archive,
                                 struct hwr_das_archive *shared_archive);

/* And the same for the free-standing lights a game config hangs in this map:
 * which ROOM each one is in.
 *
 * A light needs a room for the same three containments every other light is
 * held by -- the portal walk, its room's floor and ceiling, and the doorway
 * count of last resort -- and a light that hangs at three numbers has no room
 * written beside it. Which room contains a point is a fact about the MAP, so it
 * is worked out ONCE here, exactly as the granted faces are, by asking each
 * room's own wall loop whether the point is inside it. That is the same
 * question the community map editor asks when somebody drops an object into a
 * room, so a light hung in the editor's sector 12 belongs to sector 12 here.
 *
 * A light inside no room at all is not seeded and says so once: three numbers
 * outside the building are a typing mistake, and a light nothing contains would
 * be a light with no containments. */
void hwr_lights_stage_placed(const struct hwr_world *world);

/* How many times that walk has been taken.
 *
 * A claim that a frame does not walk a map looking for granted faces is a claim
 * about something that did NOT happen, and only a counter can state one. */
unsigned long hwr_lights_glow_face_walks(void);
/* And the same count for the free-standing lights, for the same reason. */
unsigned long hwr_lights_placed_walks(void);

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

/* And the colour one WORLD TEXTURE throws, which is a different law because a
 * wall is a different picture.
 *
 * A sprite is cut out of its background, so the mask already says which texels
 * are the thing and an average over what is left is the thing's own colour. A
 * wall texture is cut out of nothing -- every texel is the surface -- so the
 * same average over lava returns the colour of the rock it is set in. What is
 * wanted is the GLOW rather than the stone around it, so the answer is taken
 * over the texture's BRIGHTEST texels alone: every texel at least half as bright
 * as the brightest one in the picture, each weighted by the square of the
 * engine's own green-weighted luminance, normalized so the strongest channel is
 * one. Lava answers orange, a cold rune answers blue, and a wall that is one
 * flat colour answers that colour, since then every texel is its own brightest.
 *
 * Returns nonzero when there was any colour to find; a texture that is entirely
 * the palette's own black leaves `out` alone. */
int hwr_lights_texture_color(const uint8_t *pixels, size_t count,
                             const uint8_t palette[768], float out[3]);

/* How many lights the last build considered before the cap, and how many it
 * dropped at each containment. Diagnostic: the seeding is where a light leaks
 * from, so what it refused is worth being able to read. */
struct hwr_lights_stats {
    unsigned long marked;      /* the map's own unshaded mark */
    unsigned long no_sector;   /* nothing to belong to */
    unsigned long overlay;     /* a reflection-only artefact */
    unsigned long refused;     /* the look file says this is not a light */
    unsigned long unseen;      /* the walk drew no room this light reaches */
    unsigned long too_far;     /* past the hop cap */
    unsigned long capped;      /* more candidates than slots */
    unsigned long fading;      /* still going out from an earlier frame */
    /* A light that asked to follow its room's own switch, in a room whose
     * fixtures are out and which has finished going out. Counted apart from
     * every other refusal because it is the only one that is not about the
     * light: the light is exactly where it was and exactly what it was, and
     * the room turned it off. */
    unsigned long unlit;
    /* A thing the map does NOT mark that a game config named, and thereby made
     * a light. Counted apart from the marked ones because the two answer
     * different questions: one is what the map itself says, the other is what
     * somebody wrote over it. */
    unsigned long promoted;
};
const struct hwr_lights_stats *hwr_lights_last_stats(void);

#endif
