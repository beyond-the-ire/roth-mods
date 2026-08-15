#include "hwr_lights.h"

#include "hwr_das.h"
#include "hwr_look.h"
#include "hwr_objects.h"
#include "hwr_point_math.h"
#include "hwr_sprite_span.h"
#include "hwr_sprites.h"
#include "hwr_world.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The derived-light set: one array, one falloff law, and a seeder per class.
 *
 * The enumerator is a switch over the classes rather than a call per class on
 * purpose. A class that lands later -- the weapon's own flash, a window open to
 * the sky -- is then one case here and one seeder beside it, with nothing said
 * about the array, the per-view transform, or the shader. The switch names every
 * class the header declares and carries no default, so a class added to that
 * enumeration cannot be forgotten here: the build stops until it is answered.
 *
 * WHAT THIS DOES NOT DO, said plainly because it is the honest limit of the
 * round rather than an omission: none of these lights casts a shadow. The plane
 * the light the player carries reads is exact for one structural reason -- that
 * light sits AT THE EYE, so the ray it travels is the ray the view travels and
 * what stands in the way is what the eye is already looking through. A torch on
 * a wall shares no ray with the view and that plane says nothing about it.
 * There is no screen-space shortcut; the honest one is a depth pass from each
 * light, which is a shadow map and is not this.
 *
 * So these lights LEAK, and four containments are what is shipped instead of
 * occlusion. Each of them is cheap and each of them is absolute:
 *
 *   the portal walk   a light that reaches nothing the engine's own walk drew
 *                     is not seeded at all. This is nearly free -- the walk has
 *                     already run -- and it removes the worst class outright:
 *                     light from a room that cannot be seen from here. It is
 *                     also the only one of the four that decides whether a light
 *                     EXISTS, which is a ruling rather than an implementation: a
 *                     light lighting a room that is on the screen is lighting
 *                     that room, however far away the person looking at it
 *                     happens to be standing AND WHEREVER THE THING GIVING IT
 *                     HAPPENS TO BE HIDDEN. What is asked is where the light
 *                     GETS TO, never where it comes from; the walk that answers
 *                     it is written out at the containment itself.
 *   the hop cap       and where there is no drawn set at all -- the visibility
 *                     pass switched off, or a draw list that could not be read
 *                     -- a light more than so many doorways from the room the
 *                     eye is in is dropped instead. It is the containment of
 *                     last resort and nothing more: how far the eye is from a
 *                     light orders the CAP and never decides existence.
 *   the world-Z window  a light reaches only its own room's floor and ceiling
 *                     with slack either side, so a torch cannot light the
 *                     storey above it. The fragment reconstructs its own world
 *                     height exactly, so this costs no vertex data.
 *   the bounded reach the falloff is windowed rather than a true inverse
 *                     square, so the reach is where the pool ENDS rather than
 *                     where it stops being noticeable.
 *
 * What survives all four is a torch beside a doorway spilling onto the wall on
 * the far side of it, and a light in the same room as a thin partition lighting
 * both faces of it. Those are shadow cases and they are not answered here.
 *
 * And what the LIGHTS DO NOT READ, which is a ruling rather than an oversight:
 * nothing here consults the room's own brightness byte or the mark the engine
 * sets and clears as a switch is thrown. A torch is a torch whether or not the
 * room's fixtures are lit; a room brightening does not brighten the candle
 * standing in it. The two mechanisms are independent, and the gates hold them
 * to it. */

/* ------------------------------------------------------------- what is asked */

/* THE COLOUR A LIGHT IS LEFT WITH WHERE THE THING THAT GIVES IT HAS NONE TO
 * GIVE: a sprite cut entirely away, or one drawn in nothing but the palette's
 * own black. It is a fallback and not a setting -- a light nobody can read a
 * colour off is a rare mistake in the art rather than a taste anybody holds --
 * and a light of no colour at all would be a black light, which is worse than
 * a warm one. The number is the one the colour row that used to sit in the
 * settings shipped at, so nothing that was ever lit by it changes colour. */
#define HWR_LIGHT_UNREADABLE_R (255.0f / 255.0f)
#define HWR_LIGHT_UNREADABLE_G (180.0f / 255.0f)
#define HWR_LIGHT_UNREADABLE_B (110.0f / 255.0f)

static struct hwr_lights_settings g_settings;
static struct hwr_lights_stats g_stats;

void hwr_lights_set_settings(const struct hwr_lights_settings *settings)
{
    if (settings == NULL) {
        memset(&g_settings, 0, sizeof g_settings);
        return;
    }
    g_settings = *settings;
    if (g_settings.max > HWR_GL_POINT_LIGHT_MAX)
        g_settings.max = HWR_GL_POINT_LIGHT_MAX;
}

const struct hwr_lights_settings *hwr_lights_get_settings(void)
{
    return &g_settings;
}

const struct hwr_lights_stats *hwr_lights_last_stats(void)
{
    return &g_stats;
}

/* WHAT A GAME CONFIG ENTRY SAID about one light casting shadows, in the three
 * states the shadow layer reads: nothing said (the settings row's own count
 * decides), always (pinned ahead of the count's picks), never (refused
 * whatever the count says).
 *
 * ONE function, because all three seeding classes ask it and two copies of a
 * three-state rule are two rules. The absent state is a real answer here and
 * not a default -- which is exactly what the pair of fields in the entry is
 * for -- so it is spelled out rather than left to a zero. */
static int hwr_light_shadow_request(const struct hwr_look_light *named)
{
    if (named == NULL || !named->has_shadows)
        return HWR_GL_SHADOW_FOLLOW;
    return named->shadows ? HWR_GL_SHADOW_ALWAYS : HWR_GL_SHADOW_NEVER;
}

/* --------------------------------------------------------- colour off a sprite
 *
 * A light's colour is read off the thing that emits it, once, and kept. The
 * decoded frames stay on the CPU, so this is a pass over a few hundred bytes
 * per distinct sprite and never a per-frame cost. */

int hwr_lights_frame_color(const uint8_t *pixels, size_t count,
                           const uint8_t palette[768], float out[3])
{
    double sum[3] = { 0.0, 0.0, 0.0 };
    double weight_total = 0.0;
    double peak;
    size_t i;
    int c;

    if (pixels == NULL || palette == NULL || out == NULL || count == 0u)
        return 0;
    for (i = 0; i < count; i++) {
        unsigned index = pixels[i];
        double r;
        double g;
        double b;
        double luminance;
        double weight;

        /* Index zero is the cut-away texel every sprite is masked with. It is
         * not black art, it is absence, and averaging it in would drag every
         * light toward the palette's own zero entry. */
        if (index == 0u)
            continue;
        r = (double)palette[index * 3u];
        g = (double)palette[index * 3u + 1u];
        b = (double)palette[index * 3u + 2u];
        /* The engine's own green-weighted luminance of a six-bit entry
         * (build_view_grayscale_lut, video_display.c), squared: what gives the
         * light is the bright part of the sprite, not the bracket it hangs on. */
        luminance = r * 4.0 + g * 8.0 + b;
        weight = luminance * luminance;
        if (weight <= 0.0)
            continue;
        sum[0] += r * weight;
        sum[1] += g * weight;
        sum[2] += b * weight;
        weight_total += weight;
    }
    if (weight_total <= 0.0)
        return 0;
    peak = 0.0;
    for (c = 0; c < 3; c++) {
        sum[c] /= weight_total;
        if (sum[c] > peak)
            peak = sum[c];
    }
    if (peak <= 0.0)
        return 0;
    /* Normalized so the strongest channel is one: what a colour row carries is
     * a hue, and how much light there is of it is the strength beside it. A
     * dim sprite must not also mean a dim light. */
    for (c = 0; c < 3; c++)
        out[c] = (float)(sum[c] / peak);
    return 1;
}

/* THE OTHER PICTURE, AND WHY IT NEEDS A LAW OF ITS OWN.
 *
 * The reading above works because a sprite is a CUT-OUT: index zero says "not
 * the thing", so what is averaged is the torch and never the wall behind it. A
 * world texture is cut out of nothing. Every texel of it is the surface, so the
 * same average over lava returns the colour of the rock the lava sits in --
 * which is grey, and which is not what anybody meant by granting the lava a
 * light.
 *
 * So the answer is taken over THE BRIGHT PART ONLY: every texel at least half as
 * bright as the brightest texel in the picture, each weighted by the square of
 * the same luminance. Lava's glow outvotes its crust; a cold rune's line
 * outvotes the stone it is cut into; a wall painted one flat colour answers that
 * colour, because there every texel is as bright as the brightest.
 *
 * Half, rather than some tuned fraction, because it is the one threshold that
 * needs no defending: it is the point at which a texel is contributing as much
 * light as the brightest thing in the picture is. */
int hwr_lights_texture_color(const uint8_t *pixels, size_t count,
                             const uint8_t palette[768], float out[3])
{
    double sum[3] = { 0.0, 0.0, 0.0 };
    double weight_total = 0.0;
    double brightest = 0.0;
    double peak;
    size_t i;
    int c;

    if (pixels == NULL || palette == NULL || out == NULL || count == 0u)
        return 0;
    /* What the brightest texel of this picture is worth. Taken over the whole
     * of it first, because the threshold below is a fact about the picture
     * rather than a number chosen for one texture. */
    for (i = 0; i < count; i++) {
        unsigned index = pixels[i];
        double luminance = (double)palette[index * 3u] * 4.0 +
                           (double)palette[index * 3u + 1u] * 8.0 +
                           (double)palette[index * 3u + 2u];

        if (luminance > brightest)
            brightest = luminance;
    }
    if (!(brightest > 0.0))
        return 0;
    for (i = 0; i < count; i++) {
        unsigned index = pixels[i];
        double r = (double)palette[index * 3u];
        double g = (double)palette[index * 3u + 1u];
        double b = (double)palette[index * 3u + 2u];
        double luminance = r * 4.0 + g * 8.0 + b;

        /* THE BRIGHT PART. Written as a doubling rather than as a halving so
         * that the comparison is exact in the integers the palette is stored
         * in and cannot land differently on two machines. */
        if (luminance * 2.0 < brightest)
            continue;
        sum[0] += r * luminance * luminance;
        sum[1] += g * luminance * luminance;
        sum[2] += b * luminance * luminance;
        weight_total += luminance * luminance;
    }
    if (weight_total <= 0.0)
        return 0;
    peak = 0.0;
    for (c = 0; c < 3; c++) {
        sum[c] /= weight_total;
        if (sum[c] > peak)
            peak = sum[c];
    }
    if (peak <= 0.0)
        return 0;
    /* Normalized exactly as a sprite's is: a colour row carries a hue, and how
     * much light there is of it is the strength beside it. */
    for (c = 0; c < 3; c++)
        out[c] = (float)(sum[c] / peak);
    return 1;
}

/* A colour triple back into the six digits a grant carries one in, so that a
 * derived colour and a written one are the same kind of thing everywhere
 * downstream of here and nothing has to know which it got. */
static uint32_t hwr_light_color_to_rgb(const float color[3])
{
    uint32_t packed = 0;
    int c;

    for (c = 0; c < 3; c++) {
        double level = (double)color[c] * 255.0;
        long step = lround(level < 0.0 ? 0.0 : (level > 255.0 ? 255.0 : level));

        packed = (packed << 8) | (uint32_t)step;
    }
    return packed;
}

/* THE TEXELS ONE WORLD TEXTURE IS DRAWN FROM, out of whichever archive holds
 * it. The split is the same one every other reader of these ids uses -- below
 * the shared archive's first id the map's own, above it the shared one -- so a
 * granted texture is found exactly where the surface carrying it is found.
 *
 * A texture nobody has asked for yet is decoded here; one that is already
 * decoded is not decoded again. Directory placeholders and anything else with
 * no pixels of its own answer nothing rather than guessing. */
static int hwr_light_texture_pixels(struct hwr_das_archive *map_archive,
                                    struct hwr_das_archive *shared_archive,
                                    uint16_t texture_id,
                                    const uint8_t **out_pixels,
                                    size_t *out_count)
{
    struct hwr_das_archive *owner = NULL;
    const struct hwr_das_entry *entry;
    size_t entry_id = 0;

    *out_pixels = NULL;
    *out_count = 0;
    if (hwr_sprites_locate(map_archive, shared_archive, texture_id, &owner,
                           &entry_id) != 0 || owner == NULL)
        return 0;
    if (hwr_das_decode(owner, entry_id) != 0)
        return 0;
    entry = hwr_das_entry(owner, entry_id);
    if (entry == NULL)
        return 0;
    if (entry->resource_class == HWR_DAS_CLASS_PLAIN) {
        if (entry->raw_payload == NULL || entry->raw_payload_size < 6u ||
            entry->width == 0u || entry->height == 0u)
            return 0;
        *out_count = (size_t)entry->width * (size_t)entry->height;
        if (*out_count > entry->raw_payload_size - 6u) {
            *out_count = 0;
            return 0;
        }
        *out_pixels = entry->raw_payload + 6u;
        return 1;
    }
    if (entry->resource_class == HWR_DAS_CLASS_ANIMATED) {
        /* The frame it is drawn at when the map is staged, which is the frame
         * an animation is authored to read as. A light whose colour crawled
         * with the animation would be a light nobody wrote. */
        if (entry->frame_count == 0u || entry->frames == NULL ||
            entry->frames[0].pixels == NULL)
            return 0;
        *out_count = (size_t)entry->frames[0].width *
                     (size_t)entry->frames[0].height;
        *out_pixels = entry->frames[0].pixels;
        return *out_count != 0u;
    }
    return 0;
}

/* What a granted surface throws where the file did not say: the colour of the
 * texture's own bright part, in the six digits a grant carries one in. Answers 0
 * where there is no picture to read -- no archive, no such texture, or a texture
 * with no colour in it at all. */
static int hwr_light_texture_grant_color(const struct hwr_world *world,
                                         struct hwr_das_archive *map_archive,
                                         struct hwr_das_archive *shared_archive,
                                         uint16_t texture_id, uint32_t *out)
{
    const uint8_t *pixels = NULL;
    size_t count = 0;
    float color[3];

    if (world == NULL)
        return 0;
    if (!hwr_light_texture_pixels(map_archive, shared_archive, texture_id,
                                  &pixels, &count))
        return 0;
    if (!hwr_lights_texture_color(pixels, count, world->luts.palette, color))
        return 0;
    *out = hwr_light_color_to_rgb(color);
    return 1;
}

/* One resolved leaf's colour, kept by what identifies that leaf rather than by
 * the object that asked: many torches share one sprite, and a sprite is the
 * same sprite whichever of them is looking at it. */
#define HWR_LIGHT_COLOR_CACHE 32u

struct hwr_light_color_entry {
    uint32_t serial;
    uint32_t entry_id;
    uint32_t frame_index;
    uint32_t child_offset;
    float color[3];
    int found;
    int used;
};

static struct hwr_light_color_entry g_color_cache[HWR_LIGHT_COLOR_CACHE];
static size_t g_color_cache_count;

/* WHICH SPRITE ONE THING IS DRAWN AS, resolved through the engine's own face
 * resolver. Two things about a light are read off it -- the colour it throws and
 * how tall it stands -- so it is resolved ONCE per object per frame and handed
 * to both, rather than each of them asking. */
static int hwr_light_sprite_of(const struct hwr_lights_frame *frame,
                               const struct hwr_object *object,
                               struct hwr_sprite_lookup *out)
{
    if (frame->api == NULL || frame->world == NULL ||
        (frame->map_archive == NULL && frame->shared_archive == NULL))
        return 0;
    if (hwr_sprites_resolve(frame->api, frame->map_archive,
                            frame->shared_archive, object->graphic_id,
                            object->facing, frame->view_yaw, 0,
                            object->backref, out) != 0)
        return 0;
    return out->archive != NULL;
}

static int hwr_light_sprite_color(const struct hwr_lights_frame *frame,
                                  const struct hwr_sprite_lookup *lookup_in,
                                  float out[3])
{
    struct hwr_sprite_lookup lookup = *lookup_in;
    struct hwr_sprite_frame sprite;
    struct hwr_light_color_entry *slot;
    size_t i;

    for (i = 0; i < g_color_cache_count; i++) {
        slot = &g_color_cache[i];
        if (slot->used && slot->serial == lookup.archive->open_serial &&
            slot->entry_id == (uint32_t)lookup.entry_id &&
            slot->frame_index == (uint32_t)lookup.frame_index &&
            slot->child_offset == (uint32_t)lookup.child_offset) {
            if (slot->found) {
                out[0] = slot->color[0];
                out[1] = slot->color[1];
                out[2] = slot->color[2];
            }
            return slot->found;
        }
    }
    if (hwr_sprites_frame(&lookup, &sprite) != 0)
        return 0;
    slot = NULL;
    if (g_color_cache_count < HWR_LIGHT_COLOR_CACHE)
        slot = &g_color_cache[g_color_cache_count++];
    if (slot != NULL) {
        memset(slot, 0, sizeof *slot);
        slot->used = 1;
        slot->serial = lookup.archive->open_serial;
        slot->entry_id = (uint32_t)lookup.entry_id;
        slot->frame_index = (uint32_t)lookup.frame_index;
        slot->child_offset = (uint32_t)lookup.child_offset;
    }
    {
        float color[3];
        int found = hwr_lights_frame_color(
            sprite.pixels, (size_t)sprite.width * (size_t)sprite.height,
            frame->world->luts.palette, color);

        if (slot != NULL) {
            slot->found = found;
            slot->color[0] = color[0];
            slot->color[1] = color[1];
            slot->color[2] = color[2];
        }
        if (!found)
            return 0;
        out[0] = color[0];
        out[1] = color[1];
        out[2] = color[2];
    }
    return 1;
}

/* --------------------------------------------------------------- the hop cap
 *
 * How many doorways a sector is from the room the eye is in, counted over the
 * map's own topology. Nothing about the camera but its room enters it, so it is
 * a flood over static data with one queue and one depth array.
 *
 * Only reached where the frame has no drawn set to contain by; see the seeding
 * below for why that is the whole of its job. */

static uint8_t *g_hop_depth;
static uint32_t *g_hop_queue;
static size_t g_hop_capacity;

static int hwr_lights_hop_reserve(size_t sectors)
{
    if (sectors == 0u)
        return 0;
    if (g_hop_capacity < sectors) {
        uint8_t *depth = (uint8_t *)realloc(g_hop_depth, sectors);
        uint32_t *queue;

        if (depth == NULL)
            return 0;
        g_hop_depth = depth;
        queue = (uint32_t *)realloc(g_hop_queue, sectors * sizeof *queue);
        if (queue == NULL)
            return 0;
        g_hop_queue = queue;
        g_hop_capacity = sectors;
    }
    return 1;
}

/* 0xff for a sector the flood never reached. The flood stops at the cap, so a
 * map's far side is never walked to be thrown away. */
static const uint8_t *hwr_lights_hop_map(const struct hwr_world *world,
                                         int camera_sector, unsigned hops)
{
    size_t head = 0;
    size_t tail = 0;

    if (world == NULL || !hwr_lights_hop_reserve(world->sector_count))
        return NULL;
    memset(g_hop_depth, 0xff, world->sector_count);
    if (camera_sector < 0 || (size_t)camera_sector >= world->sector_count)
        return NULL;
    g_hop_depth[camera_sector] = 0;
    g_hop_queue[tail++] = (uint32_t)camera_sector;
    while (head < tail) {
        uint32_t index = g_hop_queue[head++];
        const struct hwr_sector *sector = &world->sectors[index];
        uint8_t depth = g_hop_depth[index];
        size_t w;

        if (depth >= hops)
            continue;
        for (w = 0; w < sector->wall_count; w++) {
            size_t wall_index = sector->first_wall + w;
            size_t neighbor;

            if (wall_index >= world->wall_count)
                break;
            neighbor = world->walls[wall_index].neighbor_sector_index;
            if (neighbor == SIZE_MAX || neighbor >= world->sector_count)
                continue;
            if (g_hop_depth[neighbor] != 0xffu)
                continue;
            g_hop_depth[neighbor] = (uint8_t)(depth + 1u);
            g_hop_queue[tail++] = (uint32_t)neighbor;
        }
    }
    return g_hop_depth;
}

/* ------------------------------------------------------- what stood last frame
 *
 * The retained set is what makes a light set stop flickering. Two things are
 * kept in it and nothing else:
 *
 *   HYSTERESIS  a light that was standing scores a quarter higher than one that
 *               was not, so a pair either side of the last slot settles on one
 *               of them instead of alternating between them every frame.
 *   THE FADE    a light dropped FOR WANT OF A SLOT goes out over a moment
 *               rather than in one frame, and one taken back up comes up the
 *               same way.
 *
 * A light dropped by a CONTAINMENT is not faded: it is gone that frame. That
 * is deliberate and it is the one place the two ideas conflict. A fade is a
 * light still contributing after the rule that removed it said it must not, and
 * the rule that removed it is the rule that keeps light on the right side of a
 * wall. Smoothness loses. */

#define HWR_LIGHT_RETAIN_MAX (HWR_GL_POINT_LIGHT_MAX * 2u)

struct hwr_light_retained {
    /* What names this light across frames. A torch does not move, so its own
     * identity is where it is and what it is drawn with.
     *
     * WHICH CLASS SEEDED IT is part of that name and not decoration. A glow
     * face has no graphic at all, and the number beside it is a texture rather
     * than a graphic; without the class to tell the two apart, a torch and a
     * granted patch of floor that happened to share a room, a number and a
     * position would be one light to the fade and would trade a level between
     * them. */
    uint8_t source;             /* an enum hwr_light_class */
    uint16_t source_id;         /* its graphic, or the texture it is painted with */
    uint16_t sector_index;
    int16_t world_x;
    int16_t world_y;
    int16_t world_z;
    uint8_t used;
    uint8_t stood;      /* it was in last frame's published set */
    float level;        /* 0 going out, 1 fully up */
    struct hwr_gl_point_light light;
};

static struct hwr_light_retained g_retained[HWR_LIGHT_RETAIN_MAX];
static size_t g_retained_count;
static uint64_t g_last_ns;

/* ------------------------------------------------------- the granted faces
 *
 * What a game config's `surfaces` table amounts to on one map: for each room
 * and each granted texture painted anywhere in it, ONE light, at the centroid
 * of that room's granted faces, pushed off each of them along the way it faces.
 *
 * It is worked out ONCE, when the map is staged. Which faces carry a granted
 * texture is a fact about the map; a frame only has to ask which of the handful
 * that leaves behind is in a room being drawn.
 *
 * The merge is stated rather than discovered. A lava lake is hundreds of faces
 * sharing one texture, and one light per face would fill the whole array with
 * near-identical lights a foot apart and starve every other light in the room
 * -- while looking, from the middle of the lake, exactly like one light. */

/* How many surfaces one map may have granted at once. The look layer's own
 * table is this size, so this is the whole of what it can hand over. */
#define HWR_GLOW_GRANT_MAX 64u
/* How many distinct granted textures ONE room may be found to carry. A room
 * with more than this many different glowing surfaces in it is not a room. */
#define HWR_GLOW_SECTOR_TEXTURES 16u
/* And how many (room, texture) lights one map may leave behind. A cavern
 * network whose every chamber holds lava reaches a few hundred, so this is
 * generous rather than tight; past it the map says so once and keeps the rooms
 * it found first, in the map's own order, which is stable. */
#define HWR_GLOW_FACE_MAX 512u
/* And how many faces one map may NAME. The look layer's own table is this size,
 * so this is the whole of what it can hand over. */
#define HWR_GLOW_NAMED_MAX 64u
/* How far off its own surface a light is placed, in world units.
 *
 * Small on purpose: it exists so that a light is never inside the wall or under
 * the floor it came from, not to move the light into the room. A candle-lit
 * room spends one shade row every thirty-two world units, so this is half a row
 * -- clear of the surface, and too little to carry a light through anything
 * thicker than a pane. */
#define HWR_GLOW_FACE_OFFSET 16
/* The shadow lookup's grazing floor stands on this clearance: every mounted
 * light is exactly one clearance off its own wall, and that is the plane
 * distance below which the lookup declines to shadow (hwr_point_math.h states
 * why). The tie is asserted rather than left to agree by eye. */
_Static_assert(HWR_GLOW_FACE_OFFSET == HWR_PSM_GRAZE_NEAR_WORLD,
               "the grazing floor is the emit law's own wall clearance: a "
               "mounted light stands one clearance off its wall, and that is "
               "the plane the lookup must decline");
/* Where a map's own archive stops holding the pictures its SURFACES are painted
 * with and starts holding the ones its OBJECTS are drawn with. Nothing is ever
 * painted with a number at or above the first, so a texture grant written on one
 * is a `sprites` line in the wrong table -- which is worth saying rather than
 * merely missing. The pair is the same one the game config's own reader folds a
 * single picture number by. */
#define HWR_GLOW_OBJECT_PICTURE_FIRST 4096u
#define HWR_GLOW_OBJECT_PICTURE_LIMIT 4608u

struct hwr_glow_grant {
    uint16_t texture_id;
    /* Whether the MAP's own section said any of this, and whether any face or
     * flat of the map turned out to carry the texture. The pair is what decides
     * whether a grant nothing carries is worth a word: a game-wide entry is
     * written once for a whole game and is expected to name nothing on most of
     * its maps, and an entry written under a map names a texture its author
     * believed was in that map. */
    uint8_t map_scoped;
    uint8_t found;
    struct hwr_look_light light;
};

/* ONE LIGHT the map's own geometry gives off, wherever it came from: the middle
 * of a room's faces painted with one granted texture, or one face somebody named
 * on its own. Both are the same thing to everything downstream -- a position, a
 * room and a colour -- so they are one array and one class, and the seeding
 * below is the only place that has to know which is which.
 *
 * The composed grant is carried HERE rather than referenced through the grant
 * table, because a named face's answer is its own: whatever the texture blanket
 * said, then its ID, then its number. */
struct hwr_glow_face {
    uint16_t sector_index;
    /* What names this light across frames for the fade and the hysteresis: the
     * texture for one the blanket found, the face number for one somebody
     * named. */
    uint16_t source_id;
    int16_t world_x;
    int16_t world_y;
    int16_t world_z;
    struct hwr_look_light light;
};

static struct hwr_glow_grant g_glow_grants[HWR_GLOW_GRANT_MAX];
static size_t g_glow_grant_count;
static struct hwr_glow_face g_glow_faces[HWR_GLOW_FACE_MAX];
static size_t g_glow_face_count;
/* Which set of granted surfaces the staged answer was worked out from. Nought
 * means nothing has been worked out; the look layer's own generation is never
 * nought, so the two can never be confused. */
static uint32_t g_glow_face_generation;
/* AND THE MAP AS IT STOOD WHEN THAT ANSWER WAS WORKED OUT, which is not the
 * same thing as the map that was loaded: see the stamp below. */
static uint32_t g_glow_face_stamp;
static unsigned long g_glow_face_walks;

/* WHAT THE GRANTED SET IS WORKED OUT FROM, in one number.
 *
 * Which faces carry a granted texture is a fact about the map -- but the map's
 * own surfaces MOVE while it is played. A material command repaints a face, an
 * animated surface steps its face through a set of pictures, a lift carries a
 * floor up and a scripted mover walks a corner across the room. A candidate set
 * worked out once at load and never again answers for the map as it was staged
 * and for no later moment: a face that turns INTO the granted texture never
 * lights, and one that turns out of it lights forever. That is exactly the
 * complaint this answers.
 *
 * So the same fields the staging walk READS are stamped here, and the staging
 * is taken again when the stamp moves: sector heights and flat ids, slab
 * surfaces and heights, wall texture ids, and the corners the faces run
 * between. Those are what decide both WHICH faces are granted and WHERE each
 * one's light stands. Nothing else is in it -- pan bytes scroll texels rather
 * than move a face, and the doors carry no faces this walk ever reaches.
 *
 * It is the same shape of answer the sun's caster mesh keeps for the same
 * reason (hwr_sun.c) and it is paid for on the same terms: only a map with
 * something granted on it is ever stamped, which is every map until somebody
 * writes a texture or a face into a game config, and a frame in which nothing
 * moved costs one comparison after it. */
static uint32_t hwr_glow_mix(uint32_t stamp, int32_t value)
{
    stamp ^= (uint32_t)value & 0xffffu;
    return stamp * 16777619u;
}

static uint32_t hwr_glow_surface_stamp(const struct hwr_world *world)
{
    uint32_t stamp = 2166136261u;
    size_t i;

    for (i = 0; i < world->sector_count; i++) {
        const struct hwr_sector *sector = &world->sectors[i];

        stamp = hwr_glow_mix(stamp, sector->floor_flat_id);
        stamp = hwr_glow_mix(stamp, sector->ceiling_flat_id);
        stamp = hwr_glow_mix(stamp, sector->floor_z);
        stamp = hwr_glow_mix(stamp, sector->ceiling_z);
    }
    for (i = 0; i < world->slab_count; i++) {
        const struct hwr_slab *slab = &world->slabs[i];

        stamp = hwr_glow_mix(stamp, (int32_t)slab->lower_surface_id);
        stamp = hwr_glow_mix(stamp, (int32_t)slab->upper_surface_id);
        stamp = hwr_glow_mix(stamp, slab->lower_z);
        stamp = hwr_glow_mix(stamp, slab->upper_z);
    }
    for (i = 0; i < world->wall_count; i++) {
        const struct hwr_wall *wall = &world->walls[i];

        stamp = hwr_glow_mix(stamp, (int32_t)wall->middle_id);
        stamp = hwr_glow_mix(stamp, (int32_t)wall->upper_id);
        stamp = hwr_glow_mix(stamp, (int32_t)wall->lower_id);
    }
    for (i = 0; i < world->vertex_count; i++) {
        stamp = hwr_glow_mix(stamp, world->vertices[i].world_x);
        stamp = hwr_glow_mix(stamp, world->vertices[i].world_y);
    }
    return stamp;
}

/* ------------------------------------------------- the free-standing lights
 *
 * What a game config's `placed` list amounts to on one map: for each entry, ONE
 * light where it was hung, in whichever of the map's rooms contains that point.
 *
 * The room is worked out ONCE, when the map is staged, for the same reason the
 * granted faces are: which room a point is in is a fact about the map, and
 * asking it again sixty times a second would be the whole cost of the feature.
 *
 * Nothing else about these is worked out at all. They are not found, they are
 * not derived and they are not composed with anything -- each is exactly what
 * somebody wrote down, which is why the colour is required and why there is no
 * `off`. */

/* How many free-standing lights one map may hang. The look layer's own table is
 * this size, so this is the whole of what it can hand over. */
#define HWR_PLACED_MAX 64u

struct hwr_placed_light {
    uint16_t sector_index;
    int16_t world_x;
    int16_t world_y;
    int16_t world_z;
    struct hwr_look_light light;
};

static struct hwr_placed_light g_placed[HWR_PLACED_MAX];
static size_t g_placed_count;
/* Which set of free-standing lights the staged answer was worked out from.
 * Nought means nothing has been worked out. */
static uint32_t g_placed_generation;
static unsigned long g_placed_walks;

void hwr_lights_reset(void)
{
    memset(g_retained, 0, sizeof g_retained);
    g_retained_count = 0;
    g_last_ns = 0;
    memset(g_color_cache, 0, sizeof g_color_cache);
    g_color_cache_count = 0;
    memset(&g_stats, 0, sizeof g_stats);
    /* And the granted faces of the map that was loaded. A new map's sector 12
     * is not the old map's sector 12, so an answer worked out over one map is
     * not an answer about the other. The WALK COUNT is not forgotten: it is a
     * count of something that happened, and a reset does not unhappen it. */
    memset(g_glow_grants, 0, sizeof g_glow_grants);
    g_glow_grant_count = 0;
    memset(g_glow_faces, 0, sizeof g_glow_faces);
    g_glow_face_count = 0;
    g_glow_face_generation = 0;
    g_glow_face_stamp = 0;
    /* And the rooms the free-standing lights were found to hang in, which are
     * rooms of the map that was loaded and of no other. */
    memset(g_placed, 0, sizeof g_placed);
    g_placed_count = 0;
    g_placed_generation = 0;
}

unsigned long hwr_lights_glow_face_walks(void)
{
    return g_glow_face_walks;
}

unsigned long hwr_lights_placed_walks(void)
{
    return g_placed_walks;
}

/* Sixteen bits, the way the map is authored in them: a coordinate that runs off
 * one end comes back on the other, so a difference across the wrap is a short
 * one rather than a map-wide one. */
static int16_t hwr_glow_wrap16(int32_t value)
{
    int32_t wrapped = value & 0xffff;

    return (int16_t)(wrapped >= 0x8000 ? wrapped - 0x10000 : wrapped);
}

/* A height, held to the sixteen bits a light's position is kept in. Heights are
 * NOT modular the way the map's own x and y are -- a ceiling near the top of the
 * range pushed past it is at the top of the range, not at the bottom -- so this
 * is a clamp and the one above is a wrap, and the difference is deliberate. */
static int16_t hwr_glow_clamp16(int32_t value)
{
    if (value < -32768)
        return -32768;
    if (value > 32767)
        return 32767;
    return (int16_t)value;
}

/* Which grant a face's texture id is under, or -1 for none.
 *
 * The id is first held to the same rule the world's own texture census applies:
 * a negative flat id is the engine's solid-colour sentinel rather than a
 * picture, an id past the archives addresses nothing, and the world span colour
 * key is the marker a portal's empty middle band carries. None of the three is
 * a surface somebody can have painted lava onto. */
static int hwr_glow_grant_of(const struct hwr_world *world,
                             const struct hwr_glow_grant *grants,
                             size_t grant_count, int32_t id)
{
    uint16_t texture_id;
    size_t i;

    if (id < 0 || id >= 0x1600)
        return -1;
    texture_id = (uint16_t)id;
    if (texture_id == world->world_span_colorkey)
        return -1;
    for (i = 0; i < grant_count; i++) {
        if (grants[i].texture_id == texture_id)
            return (int)i;
    }
    return -1;
}

/* One room's running answer for one granted texture. The positions are summed
 * RELATIVE TO THE FIRST ONE, in the sixteen bits the map is authored in, so a
 * room that straddles the wrap has its centroid inside itself rather than on
 * the far side of the world. */
struct hwr_glow_accum {
    int grant;
    int16_t anchor_x;
    int16_t anchor_y;
    int32_t sum_x;
    int32_t sum_y;
    int32_t sum_z;
    uint32_t count;
};

static void hwr_glow_accumulate(struct hwr_glow_accum *accum, size_t *used,
                                int grant, int16_t x, int16_t y, int16_t z)
{
    struct hwr_glow_accum *slot;
    size_t i;

    for (i = 0; i < *used; i++) {
        if (accum[i].grant != grant)
            continue;
        slot = &accum[i];
        slot->sum_x += hwr_glow_wrap16((int32_t)x - slot->anchor_x);
        slot->sum_y += hwr_glow_wrap16((int32_t)y - slot->anchor_y);
        slot->sum_z += z;
        slot->count++;
        return;
    }
    if (*used >= HWR_GLOW_SECTOR_TEXTURES)
        return;
    slot = &accum[(*used)++];
    slot->grant = grant;
    slot->anchor_x = x;
    slot->anchor_y = y;
    slot->sum_x = 0;
    slot->sum_y = 0;
    slot->sum_z = z;
    slot->count = 1;
}

/* The middle of a room's own footprint: the mean of the corners its wall loop
 * runs through. It is what a flat's light stands over, because a floor has no
 * one point that is more its centre than any other. */
static int hwr_glow_sector_centroid(const struct hwr_world *world,
                                    const struct hwr_sector *sector,
                                    int16_t *out_x, int16_t *out_y)
{
    int16_t anchor_x = 0;
    int16_t anchor_y = 0;
    int32_t sum_x = 0;
    int32_t sum_y = 0;
    uint32_t counted = 0;
    size_t w;

    for (w = 0; w < sector->wall_count; w++) {
        size_t wall_index = sector->first_wall + w;
        const struct hwr_vertex *vertex;

        if (wall_index >= world->wall_count)
            break;
        if (world->walls[wall_index].vertex_a_index >= world->vertex_count)
            continue;
        vertex = &world->vertices[world->walls[wall_index].vertex_a_index];
        if (counted == 0u) {
            anchor_x = vertex->world_x;
            anchor_y = vertex->world_y;
        }
        sum_x += hwr_glow_wrap16((int32_t)vertex->world_x - anchor_x);
        sum_y += hwr_glow_wrap16((int32_t)vertex->world_y - anchor_y);
        counted++;
    }
    if (counted == 0u)
        return 0;
    *out_x = hwr_glow_wrap16(anchor_x + sum_x / (int32_t)counted);
    *out_y = hwr_glow_wrap16(anchor_y + sum_y / (int32_t)counted);
    return 1;
}

/* WHERE ONE FACE'S OWN LIGHT STANDS: the middle of its two corners, pushed
 * clear along the way the face looks, at the height between its room's floor
 * and ceiling.
 *
 * Which of a segment's two perpendiculars that is comes from the ENGINE'S OWN
 * visibility rule rather than from where anybody is standing: (dy, -dx) points
 * at every eye allowed to see the face, which is the interior of the room that
 * owns it. Taking the other one would put every wall light inside the wall.
 *
 * Written once and used twice -- by the texture blanket, which accumulates
 * these into one light per room, and by a face somebody named, which keeps its
 * own -- so the two cannot place a light by different laws. Returns 0 where the
 * face has no geometry to stand on. */
static int hwr_glow_face_point(const struct hwr_world *world,
                               const struct hwr_sector *sector,
                               const struct hwr_wall *wall, int16_t *out_x,
                               int16_t *out_y, int16_t *out_z)
{
    const struct hwr_vertex *a;
    const struct hwr_vertex *b;
    int32_t dx;
    int32_t dy;
    double length;

    if (wall->vertex_a_index >= world->vertex_count ||
        wall->vertex_b_index >= world->vertex_count)
        return 0;
    a = &world->vertices[wall->vertex_a_index];
    b = &world->vertices[wall->vertex_b_index];
    dx = hwr_glow_wrap16((int32_t)b->world_x - a->world_x);
    dy = hwr_glow_wrap16((int32_t)b->world_y - a->world_y);
    length = sqrt((double)dx * dx + (double)dy * dy);
    if (!(length > 0.0))
        return 0;
    *out_x = hwr_glow_wrap16(
        (int32_t)a->world_x + dx / 2 +
        (int32_t)lround((double)dy / length * HWR_GLOW_FACE_OFFSET));
    *out_y = hwr_glow_wrap16(
        (int32_t)a->world_y + dy / 2 +
        (int32_t)lround((double)-dx / length * HWR_GLOW_FACE_OFFSET));
    *out_z = (int16_t)(((int32_t)sector->floor_z +
                        (int32_t)sector->ceiling_z) / 2);
    return 1;
}

void hwr_lights_stage_glow_faces(const struct hwr_world *world,
                                 struct hwr_das_archive *map_archive,
                                 struct hwr_das_archive *shared_archive)
{
    size_t textures = hwr_look_texture_count();
    size_t named = hwr_look_face_count();
    /* The two things a named face needs remembered across the walk: what it
     * addresses, and whether anything on this map answered to it. */
    int named_by_id[HWR_GLOW_NAMED_MAX];
    uint32_t named_key[HWR_GLOW_NAMED_MAX];
    unsigned char named_matched[HWR_GLOW_NAMED_MAX];
    unsigned char named_told[HWR_GLOW_NAMED_MAX];
    int said_full = 0;
    size_t i;
    size_t s;

    g_glow_grant_count = 0;
    g_glow_face_count = 0;
    g_glow_face_generation = hwr_look_glow_generation();
    g_glow_face_stamp = 0;
    if (named > HWR_GLOW_NAMED_MAX)
        named = HWR_GLOW_NAMED_MAX;
    memset(named_by_id, 0, sizeof named_by_id);
    memset(named_key, 0, sizeof named_key);
    memset(named_matched, 0, sizeof named_matched);
    memset(named_told, 0, sizeof named_told);
    if (world == NULL || !world->valid || (textures == 0u && named == 0u))
        return;
    /* THE MAP AS IT STANDS AT THIS MOMENT, taken before the walk that reads it,
     * so what was worked out and what it was worked out FROM are the same
     * moment. */
    g_glow_face_stamp = hwr_glow_surface_stamp(world);
    for (i = 0; i < textures && g_glow_grant_count < HWR_GLOW_GRANT_MAX; i++) {
        uint16_t texture_id = 0;
        int map_scoped = 0;
        struct hwr_look_light light;

        if (!hwr_look_texture_at(i, &texture_id, &map_scoped, &light))
            continue;
        /* Refused outright, exactly as a sprite is: whatever the map paints
         * with it, this is not a light. */
        if (light.off)
            continue;
        /* AND THE ONE NUMBER THAT IS A MISTAKE RATHER THAN A MISS, said where
         * the key is read rather than where the walk ends.
         *
         * A map's own archive keeps the pictures its walls, floors and ceilings
         * are painted with below 4096, and the pictures its OBJECTS are drawn
         * with from 4096 up. So a texture number in that upper range names an
         * object's picture, which no surface anywhere can be painted with --
         * this is not a texture that happens to be missing from one map, it is
         * a `sprites` line written in the wrong table, and it is wrong on every
         * map in the game. Said whatever the scope, because it can never be
         * right and can therefore never fire on a good case; and said here, so
         * that a person gets THIS sentence rather than a complaint about the
         * colour of a picture that was never the point. */
        if (texture_id >= HWR_GLOW_OBJECT_PICTURE_FIRST &&
            texture_id < HWR_GLOW_OBJECT_PICTURE_LIMIT) {
            fprintf(stderr, "[renderer_overhaul] the texture %u is the number "
                            "of a picture an OBJECT is drawn with rather than "
                            "one a surface is painted with, so nothing is "
                            "painted with it anywhere; a light for the thing "
                            "standing there is written in 'sprites'\n",
                    (unsigned)texture_id);
            continue;
        }
        /* AND THE COLOUR, WHERE THE FILE DID NOT STATE ONE: off the texture's
         * OWN TEXELS, by the law written beside the reading -- its brightest
         * texels alone, so lava is lit by its glow and not by the rock the glow
         * is set in. A wall is not a cut-out the way a sprite is, which is why
         * it takes a law of its own rather than the sprite's.
         *
         * A picture that cannot be read at all -- no archive, no such texture,
         * nothing but the palette's own black in it -- leaves the grant with no
         * colour to give, and that is said once rather than lit with a colour
         * nobody chose. */
        if (!light.has_color) {
            uint32_t derived = 0;

            if (hwr_light_texture_grant_color(world, map_archive,
                                              shared_archive, texture_id,
                                              &derived)) {
                light.has_color = 1;
                light.color = derived;
            } else {
                fprintf(stderr, "[renderer_overhaul] the texture %u is granted "
                                "a light with no colour, and its own picture "
                                "could not be read to take one from, so "
                                "nothing is lit from it\n",
                        (unsigned)texture_id);
                continue;
            }
        }
        g_glow_grants[g_glow_grant_count].texture_id = texture_id;
        g_glow_grants[g_glow_grant_count].map_scoped = (uint8_t)(map_scoped != 0);
        g_glow_grants[g_glow_grant_count].found = 0;
        g_glow_grants[g_glow_grant_count].light = light;
        g_glow_grant_count++;
    }
    for (i = 0; i < named; i++) {
        struct hwr_look_light light;

        if (!hwr_look_face_at(i, &named_by_id[i], &named_key[i], &light))
            named_key[i] = 0xffffffffu;         /* addresses nothing */
    }
    if (g_glow_grant_count == 0u && named == 0u)
        return;
    /* Past this line a map is actually walked, and that is the whole of what
     * this counter is for. */
    g_glow_face_walks++;
    for (s = 0; s < world->sector_count && s <= 0xffffu; s++) {
        const struct hwr_sector *sector = &world->sectors[s];
        struct hwr_glow_accum accum[HWR_GLOW_SECTOR_TEXTURES];
        size_t used = 0;
        int16_t centre_x = 0;
        int16_t centre_y = 0;
        int grant;
        size_t w;

        if (!hwr_glow_sector_centroid(world, sector, &centre_x, &centre_y))
            continue;
        /* THE FLATS. A ceiling faces down into its room and a floor faces up
         * into it, so each is pushed off along the only direction it has. They
         * are not faces and carry no number of their own, so only the texture
         * blanket ever reaches them -- which is exactly what makes a blanket a
         * blanket. */
        grant = hwr_glow_grant_of(world, g_glow_grants, g_glow_grant_count,
                                  (int32_t)sector->ceiling_flat_id);
        if (grant >= 0) {
            g_glow_grants[grant].found = 1u;
            hwr_glow_accumulate(accum, &used, grant, centre_x, centre_y,
                                hwr_glow_clamp16((int32_t)sector->ceiling_z -
                                                 HWR_GLOW_FACE_OFFSET));
        }
        grant = hwr_glow_grant_of(world, g_glow_grants, g_glow_grant_count,
                                  (int32_t)sector->floor_flat_id);
        if (grant >= 0) {
            g_glow_grants[grant].found = 1u;
            hwr_glow_accumulate(accum, &used, grant, centre_x, centre_y,
                                hwr_glow_clamp16((int32_t)sector->floor_z +
                                                 HWR_GLOW_FACE_OFFSET));
        }
        /* AND THE SLAB, which is a block sharing the room's own footprint: its
         * underside faces down and its top faces up, like the two above. */
        if (sector->slab_index < world->slab_count) {
            const struct hwr_slab *slab = &world->slabs[sector->slab_index];

            grant = hwr_glow_grant_of(world, g_glow_grants, g_glow_grant_count,
                                      (int32_t)(int16_t)slab->lower_surface_id);
            if (grant >= 0) {
                g_glow_grants[grant].found = 1u;
                hwr_glow_accumulate(accum, &used, grant, centre_x, centre_y,
                                    hwr_glow_clamp16((int32_t)slab->lower_z -
                                                     HWR_GLOW_FACE_OFFSET));
            }
            grant = hwr_glow_grant_of(world, g_glow_grants, g_glow_grant_count,
                                      (int32_t)(int16_t)slab->upper_surface_id);
            if (grant >= 0) {
                g_glow_grants[grant].found = 1u;
                hwr_glow_accumulate(accum, &used, grant, centre_x, centre_y,
                                    hwr_glow_clamp16((int32_t)slab->upper_z +
                                                     HWR_GLOW_FACE_OFFSET));
            }
        }
        /* THE WALLS, which are the faces: all three of a wall's bands for the
         * blanket, and the whole face for anybody who named it. */
        for (w = 0; w < sector->wall_count; w++) {
            size_t wall_index = sector->first_wall + w;
            const struct hwr_wall *wall;
            struct hwr_look_light face_light;
            int32_t ids[3];
            int16_t push_x = 0;
            int16_t push_y = 0;
            int16_t push_z = 0;
            int blanket = -1;
            int is_named;
            size_t k;
            size_t e;

            if (wall_index >= world->wall_count)
                break;
            wall = &world->walls[wall_index];
            ids[0] = (int32_t)(int16_t)wall->middle_id;
            ids[1] = (int32_t)(int16_t)wall->upper_id;
            ids[2] = (int32_t)(int16_t)wall->lower_id;
            for (k = 0; k < 3u; k++) {
                grant = hwr_glow_grant_of(world, g_glow_grants,
                                          g_glow_grant_count, ids[k]);
                if (grant < 0)
                    continue;
                g_glow_grants[grant].found = 1u;
                if (blanket < 0)
                    blanket = grant;
            }
            /* WHAT THE MAP CALLS THIS FACE, and whether anybody named it.
             * `world->walls[i]` IS the editor's `Face: i` -- the flat face
             * array of the file is the sector-ordered walk on every map the
             * game and the editor produce -- so no re-keying is needed here,
             * only the statement that they are the same number. */
            for (e = 0; e < named; e++) {
                if (named_by_id[e]
                    ? (wall->face_tag != 0u &&
                       named_key[e] == (uint32_t)wall->face_tag)
                    : named_key[e] == (uint32_t)wall_index)
                    named_matched[e] = 1u;
            }
            is_named = hwr_look_face_grant((uint32_t)wall_index,
                                           wall->face_tag, &face_light);
            if (!is_named) {
                if (blanket < 0)
                    continue;
                if (!hwr_glow_face_point(world, sector, wall, &push_x, &push_y,
                                         &push_z))
                    continue;
                for (k = 0; k < 3u; k++) {
                    grant = hwr_glow_grant_of(world, g_glow_grants,
                                              g_glow_grant_count, ids[k]);
                    if (grant >= 0)
                        hwr_glow_accumulate(accum, &used, grant, push_x,
                                            push_y, push_z);
                }
                continue;
            }
            /* NAMED, so it keeps its own light and is taken OUT of the room's
             * accumulation: the most specific statement anybody made about a
             * face is the one in force, and a face somebody picked out is not a
             * face to be averaged into the middle of the room with the rest.
             * `off` takes it out and leaves nothing behind. */
            if (face_light.off)
                continue;
            {
                struct hwr_look_light light;

                memset(&light, 0, sizeof light);
                if (blanket >= 0)
                    hwr_look_light_over(&light, &g_glow_grants[blanket].light);
                hwr_look_light_over(&light, &face_light);
                /* AND THE SAME DERIVATION FOR A FACE, off the texture the face
                 * is actually PAINTED with: its middle band first, then the two
                 * bands above and below it, which is the order the face itself
                 * is read in. A face is a painted surface exactly as a flat is,
                 * so a face grant that states no colour is no more helpless
                 * than a texture grant that states none. */
                if (!light.has_color) {
                    uint32_t derived = 0;

                    for (k = 0; k < 3u; k++) {
                        if (ids[k] < 0 || ids[k] >= 0x1600)
                            continue;
                        if (hwr_light_texture_grant_color(
                                world, map_archive, shared_archive,
                                (uint16_t)ids[k], &derived)) {
                            light.has_color = 1;
                            light.color = derived;
                            break;
                        }
                    }
                }
                if (!light.has_color) {
                    /* Said once per entry rather than once per face: one ID may
                     * name thirty faces, and thirty copies of one sentence is
                     * not thirty pieces of news. */
                    for (e = 0; e < named; e++) {
                        if (!named_matched[e] || named_told[e])
                            continue;
                        named_told[e] = 1u;
                        if (named_by_id[e])
                            fprintf(stderr, "[renderer_overhaul] the faces "
                                            "with ID %lu are granted a light "
                                            "with no colour, and the pictures "
                                            "they are painted with could not "
                                            "be read to take one from, so "
                                            "nothing is lit from them\n",
                                    (unsigned long)named_key[e]);
                        else
                            fprintf(stderr, "[renderer_overhaul] the face %lu "
                                            "is granted a light with no "
                                            "colour, and the picture it is "
                                            "painted with could not be read to "
                                            "take one from, so nothing is lit "
                                            "from it\n",
                                    (unsigned long)named_key[e]);
                    }
                    continue;
                }
                if (!hwr_glow_face_point(world, sector, wall, &push_x, &push_y,
                                         &push_z))
                    continue;
                if (g_glow_face_count >= HWR_GLOW_FACE_MAX) {
                    if (!said_full) {
                        said_full = 1;
                        fprintf(stderr, "[renderer_overhaul] this map has more "
                                        "lit faces and rooms than %u; the rest "
                                        "are not lit from\n",
                                (unsigned)HWR_GLOW_FACE_MAX);
                    }
                    break;
                }
                g_glow_faces[g_glow_face_count].sector_index = (uint16_t)s;
                g_glow_faces[g_glow_face_count].source_id =
                    (uint16_t)wall_index;
                g_glow_faces[g_glow_face_count].world_x = push_x;
                g_glow_faces[g_glow_face_count].world_y = push_y;
                g_glow_faces[g_glow_face_count].world_z = push_z;
                g_glow_faces[g_glow_face_count].light = light;
                g_glow_face_count++;
            }
        }
        /* ONE LIGHT PER (ROOM, TEXTURE), at the mean of what was found. */
        for (i = 0; i < used; i++) {
            struct hwr_glow_face *face;

            if (g_glow_face_count >= HWR_GLOW_FACE_MAX) {
                if (!said_full) {
                    said_full = 1;
                    fprintf(stderr, "[renderer_overhaul] this map has more "
                                    "lit faces and rooms than %u; the rest are "
                                    "not lit from\n",
                            (unsigned)HWR_GLOW_FACE_MAX);
                }
                break;
            }
            face = &g_glow_faces[g_glow_face_count++];
            face->sector_index = (uint16_t)s;
            face->source_id = g_glow_grants[accum[i].grant].texture_id;
            face->world_x = hwr_glow_wrap16(
                accum[i].anchor_x + accum[i].sum_x / (int32_t)accum[i].count);
            face->world_y = hwr_glow_wrap16(
                accum[i].anchor_y + accum[i].sum_y / (int32_t)accum[i].count);
            face->world_z = (int16_t)(accum[i].sum_z / (int32_t)accum[i].count);
            face->light = g_glow_grants[accum[i].grant].light;
        }
        /* Full is full: the rooms found first are kept, in the map's own order,
         * and the rest of the map is not walked for rooms there is no room
         * for. */
        if (said_full)
            break;
    }
    /* AND WHAT THE MAP TURNED OUT NOT TO HOLD, said once, here, because only a
     * finished walk can say it.
     *
     * A texture written under a MAP is a texture its author believed was in
     * that map, so a map that carries it nowhere is worth a sentence. One
     * written game-wide is written once for a whole game and is expected to
     * name nothing on most of its maps, so it stays quiet -- otherwise a line
     * about lava would be printed on every corridor in the game. */
    for (i = 0; i < g_glow_grant_count; i++) {
        if (g_glow_grants[i].found || !g_glow_grants[i].map_scoped)
            continue;
        fprintf(stderr, "[renderer_overhaul] no face or flat of this map is "
                        "painted with the texture %u, so the light granted to "
                        "it lights nothing here\n",
                (unsigned)g_glow_grants[i].texture_id);
    }
    for (i = 0; i < named; i++) {
        if (named_matched[i] || named_key[i] == 0xffffffffu)
            continue;
        if (named_by_id[i])
            fprintf(stderr, "[renderer_overhaul] no face of this map carries "
                            "the ID %lu\n", (unsigned long)named_key[i]);
        else
            fprintf(stderr, "[renderer_overhaul] this map has no face %lu; its "
                            "faces are numbered 0 to %lu\n",
                    (unsigned long)named_key[i],
                    (unsigned long)(world->wall_count != 0u
                                    ? world->wall_count - 1u : 0u));
    }
}

/* Whether a point is inside one room's own footprint.
 *
 * The crossing count over the room's wall loop, which is the same question the
 * community map editor asks when somebody drops an object into a room -- so a
 * light hung in the editor's sector 12 belongs to sector 12 here, rather than to
 * whichever room some other rule happened to pick.
 *
 * Worked in the sixteen bits the map is authored in, without wrapping: a room
 * that straddles the wrap is not a room anybody builds, and pretending to handle
 * one would be arithmetic nobody could check. The crossing test is written
 * without a division, so a wall lying flat along the ray cannot divide by
 * nothing -- it is skipped by the half-open comparison above it, which is also
 * what makes a point on a shared edge belong to exactly one of the two rooms. */
static int hwr_placed_inside(const struct hwr_world *world,
                             const struct hwr_sector *sector,
                             int16_t at_x, int16_t at_y)
{
    int32_t px = at_x;
    int32_t py = at_y;
    int inside = 0;
    size_t w;

    for (w = 0; w < sector->wall_count; w++) {
        size_t wall_index = sector->first_wall + w;
        const struct hwr_wall *wall;
        const struct hwr_vertex *a;
        const struct hwr_vertex *b;
        int32_t ax;
        int32_t ay;
        int32_t bx;
        int32_t by;
        int64_t across;
        int64_t along;

        if (wall_index >= world->wall_count)
            break;
        wall = &world->walls[wall_index];
        if (wall->vertex_a_index >= world->vertex_count ||
            wall->vertex_b_index >= world->vertex_count)
            continue;
        a = &world->vertices[wall->vertex_a_index];
        b = &world->vertices[wall->vertex_b_index];
        ax = a->world_x;
        ay = a->world_y;
        bx = b->world_x;
        by = b->world_y;
        if ((ay > py) == (by > py))
            continue;                   /* the ray does not cross this edge */
        across = (int64_t)(bx - ax) * (int64_t)(py - ay);
        along = (int64_t)(by - ay) * (int64_t)(px - ax);
        if (by > ay ? along < across : along > across)
            inside = !inside;
    }
    return inside;
}

/* Which of the map's rooms a point is in, or -1 where none of them is.
 *
 * Rooms can share a footprint -- a gallery over a hall -- so where more than one
 * answers, the one whose floor and ceiling the point is BETWEEN is the one it is
 * in. Where none of them holds it at that height, the first room that holds it
 * at all is taken: a light a few units under a floor is a light somebody meant
 * for that room, and refusing it would be refusing over a rounding. */
static int hwr_placed_room_of(const struct hwr_world *world, int16_t at_x,
                              int16_t at_y, int16_t at_z)
{
    int fallback = -1;
    size_t s;

    for (s = 0; s < world->sector_count && s <= 0xffffu; s++) {
        const struct hwr_sector *sector = &world->sectors[s];
        int32_t low;
        int32_t high;

        if (!hwr_placed_inside(world, sector, at_x, at_y))
            continue;
        if (fallback < 0)
            fallback = (int)s;
        low = sector->floor_z;
        high = sector->ceiling_z;
        if (low > high) {
            int32_t swap = low;

            low = high;
            high = swap;
        }
        if ((int32_t)at_z >= low && (int32_t)at_z <= high)
            return (int)s;
    }
    return fallback;
}

void hwr_lights_stage_placed(const struct hwr_world *world)
{
    size_t wanted = hwr_look_placed_count();
    size_t i;

    g_placed_count = 0;
    g_placed_generation = hwr_look_placed_generation();
    if (world == NULL || !world->valid || wanted == 0u)
        return;
    /* Past this line a map is actually walked, and that is the whole of what
     * this counter is for. */
    g_placed_walks++;
    for (i = 0; i < wanted && g_placed_count < HWR_PLACED_MAX; i++) {
        struct hwr_look_light light;
        int16_t at_x = 0;
        int16_t at_y = 0;
        int16_t at_z = 0;
        int room;

        if (!hwr_look_placed_at(i, &at_x, &at_y, &at_z, &light))
            continue;
        /* THE ONE FIELD A LIGHT HANGING IN THE AIR CANNOT ANSWER FOR ITSELF.
         * Every other light in the mod takes its colour from something that is
         * there -- the sprite a torch is drawn with, the entry a surface was
         * granted by -- and this one has nothing there at all. Rather than give
         * every free-standing light in the game the one torch colour and say
         * nothing about it, the file is asked for the colour and told when it
         * did not give one. */
        if (!light.has_color) {
            fprintf(stderr, "[renderer_overhaul] the light at (%d, %d, %d) is "
                            "given no colour; a light standing on its own has "
                            "nothing to take one from, so it lights nothing\n",
                    at_x, at_y, at_z);
            continue;
        }
        room = hwr_placed_room_of(world, at_x, at_y, at_z);
        if (room < 0) {
            fprintf(stderr, "[renderer_overhaul] the light at (%d, %d, %d) is "
                            "inside no room this map has, so nothing holds "
                            "it\n", at_x, at_y, at_z);
            continue;
        }
        g_placed[g_placed_count].sector_index = (uint16_t)room;
        g_placed[g_placed_count].world_x = at_x;
        g_placed[g_placed_count].world_y = at_y;
        g_placed[g_placed_count].world_z = at_z;
        g_placed[g_placed_count].light = light;
        g_placed_count++;
    }
}

/* ------------------------------------------------------------ the candidates */

struct hwr_light_candidate {
    struct hwr_gl_point_light light;
    uint8_t source;
    uint16_t source_id;
    uint16_t sector_index;
    int16_t world_x;
    int16_t world_y;
    int16_t world_z;
    /* What it is worth, and what it is worth FOR THE CHOOSING: the second
     * carries the standing bonus that keeps a set of lights from churning,
     * and the first is the plain number.
     *
     * They are kept apart because only one of them is a fact about the light.
     * The bonus is a fact about the CHOICE this frame is making, and it is
     * the plain worth that is published -- a published light that changed the
     * frame after it arrived, for no reason but having arrived, would be a
     * light that never settles. */
    float worth;
    float score;
    float level;
    int stood;
    /* Whether this light is one the scene WANTS burning this frame.
     *
     * It is not the same question as whether it is a light: a light whose room
     * has just been switched off is still every bit the light it was, and the
     * only thing that changed is that it should not be standing. That is why it
     * is a field of the candidate rather than a refusal in the class seeder --
     * a refusal would take the candidate out of the array, and a light that is
     * not in the array cannot walk its ramp down. An unwanted candidate is
     * carried, ranked LAST, never chosen, and therefore taken down the one fade
     * every other light uses. */
    int wanted;
};

/* Room for every candidate the cap could ever have to choose between: twice
 * the light array's own ceiling, so the ranking still chooses even where a
 * map offers more than the picture carries. A map with more lights in view
 * than this keeps the first of them, in the map's own order, which is stable
 * from frame to frame -- so an overflow is a set that is smaller than it
 * might be, never a set that churns. */
#define HWR_LIGHT_CANDIDATE_MAX (HWR_GL_POINT_LIGHT_MAX * 2u)

static float hwr_light_wrap16(int32_t value)
{
    int32_t wrapped = value & 0xffff;

    return (float)(wrapped >= 0x8000 ? wrapped - 0x10000 : wrapped);
}

/* How far a light is from the eye, in world units. The world-to-view add wraps
 * in sixteen bits and a light on the far side of that wrap is near rather than
 * far; the rotation that would follow cannot change a length, so it is not
 * done. */
static float hwr_light_eye_distance(const struct hwr_lights_frame *frame,
                                    const struct hwr_gl_point_light *light)
{
    float across = hwr_light_wrap16((int32_t)light->world_x +
                                    frame->view_offset_x);
    float along = hwr_light_wrap16((int32_t)light->world_y +
                                   frame->view_offset_y);
    float up = (float)frame->eye_z - light->world_z;

    return sqrtf(across * across + along * along + up * up);
}

/* What a candidate is worth to the picture: its own strength, falling away with
 * how far the eye is standing from it. A light at the eye is worth all of it and
 * one twice its own reach away is worth a third, so the cap drops the light
 * contributing least rather than the last one the walk happened to find.
 *
 * It falls away rather than running out ON PURPOSE. A rule that reached nothing
 * past some distance would leave every light past it worth exactly the same --
 * nothing -- and the order among them would then be the order the map happened
 * to list them in. That is fine until a room has twelve torches in it and eight
 * slots, at which point the eight kept are the eight the map wrote down first
 * rather than the eight nearest the eye. This has no such flat, at any
 * distance. */
static float hwr_light_score(const struct hwr_lights_frame *frame,
                             const struct hwr_gl_point_light *light,
                             float level, int stood)
{
    float reach = light->range + g_settings.range;
    float distance;
    float score;

    if (!(reach > 0.0f))
        return 0.0f;
    distance = hwr_light_eye_distance(frame, light);
    if (distance < 0.0f)
        distance = 0.0f;
    score = light->intensity * level * (reach / (reach + distance));
    if (stood)
        score *= 1.25f;
    return score;
}

static struct hwr_light_retained *hwr_light_find_retained(
    const struct hwr_light_candidate *candidate)
{
    size_t i;

    for (i = 0; i < g_retained_count; i++) {
        struct hwr_light_retained *held = &g_retained[i];

        if (held->used && held->source == candidate->source &&
            held->source_id == candidate->source_id &&
            held->sector_index == candidate->sector_index &&
            held->world_x == candidate->world_x &&
            held->world_y == candidate->world_y &&
            held->world_z == candidate->world_z)
            return held;
    }
    return NULL;
}

/* WHETHER THE MAP ITSELF MARKS ONE OBJECT AS GIVING LIGHT: its shade byte
 * composed against its room's brightness by the engine's own rule, and nought.
 *
 * ONE place in the seeder asks it, because two would be two places for a copy
 * of the arithmetic to grow. The rule itself is named rather than transcribed:
 * a mark read from a transcription can drift away from the mark the emitter
 * draws by without either side noticing. */
static int hwr_light_object_marked(const struct hwr_world *world,
                                   const struct hwr_object *object)
{
    uint8_t sector_light = 0;

    if (world != NULL &&
        object->sector_index != (uint16_t)HWR_OBJECT_NO_SECTOR &&
        object->sector_index < world->sector_count)
        sector_light = world->sectors[object->sector_index].light;
    return hwr_object_composed_light(object->seed, sector_light) == 0;
}

/* WHETHER ONE ROOM'S OWN FIXTURES ARE LIT RIGHT NOW.
 *
 * THE ENGINE'S OWN WORD, and there is exactly one: bit 2 of the sector's flags
 * byte -- the byte at +0x0a of a geometry record, which is what
 * `hwr_parse_sector` reads into `flags` and what every refresh re-reads. CLEAR
 * is lit; SET is the candle-tinted class, which is the engine's way of saying
 * this room's own fixtures are out and what is in the player's hand is what
 * lights it.
 *
 * WHY IT IS A LATCH RATHER THAN A THRESHOLD ON THE BRIGHTNESS BYTE. A light
 * switch in a map is a command record the engine ramps: `tick_light_switch`
 * pushes a signed step into every driven sector's BRIGHTNESS byte (+0x0b) each
 * frame through `apply_light_delta_to_record_list`, and when the ramp is spent
 * it stamps this FLAG through `apply_flag_mask_to_record_list` -- setting bit 2
 * where the ramp was going down and clearing it where it was going up. The
 * brightness byte alone cannot answer the question: the authored step is a
 * dozen or two out of two hundred and fifty-five and it differs per switch, so
 * the same byte value is a lit room in one place and an unlit one in the next,
 * and there is nothing written down anywhere that says where a given room's
 * swing began. The flag is the engine's own answer to exactly the question
 * being asked, it is stamped by the same handler, and every light switch in
 * this game carries the finalize bit that stamps it -- so the latch is never
 * skipped.
 *
 * It is the same bit the room's own shading reads, which is the whole point: a
 * light that follows it goes out exactly when the room the mod would otherwise
 * have drawn dark goes dark.
 *
 * A room nothing ever switches simply answers the same thing for ever, and a
 * light in one either always burns or never does. Both are honest: a room the
 * map authored candle-lit is a room whose fixtures ARE out. */
#define HWR_LIGHT_SECTOR_FIXTURES_OUT 0x02u

static int hwr_light_sector_lit(const struct hwr_world *world,
                                size_t sector_index)
{
    if (world == NULL || sector_index >= world->sector_count)
        return 1;
    return (world->sectors[sector_index].flags &
            HWR_LIGHT_SECTOR_FIXTURES_OUT) == 0;
}

/* And what one grant's word about that comes to, over the room's own state:
 * a grant that says nothing burns always, which is what every light did before
 * the field existed. Written once so the three classes cannot grow three
 * readings of one field. */
static int hwr_light_wanted(const struct hwr_world *world,
                            size_t sector_index,
                            const struct hwr_look_light *named)
{
    if (named == NULL || !named->has_only_when_lit || !named->only_when_lit)
        return 1;
    return hwr_light_sector_lit(world, sector_index);
}

/* AND WHETHER A GAME CONFIG MADE ONE, which is the same question asked of the
 * other side. Exported, because the DRAWING has to ask it too: a thing that
 * lights the room around it and is itself drawn dark is a halo with nothing in
 * the middle of it, and the map's own lights have never looked like that.
 *
 * Every refusal the seeding makes before it looks at a grant is made here in
 * the same order, so the two can only ever agree -- and the family switch is
 * the first of them, which is what keeps every other shading law byte for byte
 * where it was. */
int hwr_lights_object_promoted(const struct hwr_world *world,
                               const struct hwr_object *object)
{
    struct hwr_look_light named;

    if (world == NULL || object == NULL)
        return 0;
    if ((g_settings.classes & (1u << (unsigned)HWR_LIGHT_CLASS_OBJECT)) == 0u)
        return 0;
    if (object->sector_index == (uint16_t)HWR_OBJECT_NO_SECTOR ||
        object->sector_index >= world->sector_count)
        return 0;
    if ((object->flags & HWR_OBJECT_FLAG_REFLECTION_ONLY) != 0)
        return 0;
    if (hwr_light_object_marked(world, object))
        return 0;                    /* the map already says so; nothing to add */
    if (!hwr_look_object_grant(object->map_index, object->object_id,
                               object->graphic_id, &named))
        return 0;
    if (named.off)
        return 0;
    /* The entry's own word on the drawing, composed like every field: absent
     * means true -- what a grant has always done -- and false keeps the
     * ordinary shading law on the sprite while the light it gives stands.
     * The light is then its own nearest light, which is what lights it. */
    return named.has_lit == 0 || named.lit != 0;
}

/* ------------------------------------------------- where a thing's light is
 *
 * A thing that gives light is DRAWN as a sprite standing in the room, and its
 * light has to come from where that sprite is rather than from the point the
 * map happens to hang it by. Two things follow from that, and they are the
 * whole of this section.
 */

/* THE MIDDLE OF THE SPRITE, up its own height from the anchor.
 *
 * A map record's z is where the sprite is HUNG -- its feet for a thing standing
 * on the floor, its head for one hung from a bracket -- so a light at that
 * number is a light at the bottom or the top of the flame rather than in it. A
 * torch lit from its own feet washes the floor under it and leaves the wall
 * beside the flame dark, which is not what a torch does.
 *
 * How tall the sprite stands is the emitter's own sizing law, NAMED rather than
 * copied (hwr_sprite_span.h): the same frame, the same block modifier, the same
 * anchor branch and the same projection match the quad is drawn with. A middle
 * worked out from a transcription would leave the light out of the sprite the
 * first time either copy moved.
 *
 * It is taken at the frame the light's COLOUR is taken at, and for the same
 * reason: a height that crawled with an animation would be a light nobody
 * wrote. Answers 0 where the frame carries no projection to size against or the
 * sprite cannot be read, and then the anchor stands -- which is where the light
 * was before any of this. */
static int hwr_light_sprite_middle(const struct hwr_lights_frame *frame,
                                   const struct hwr_sprite_lookup *lookup,
                                   const struct hwr_object *object,
                                   float *out_z)
{
    struct hwr_sprite_frame sprite;
    int16_t origin_x = 0;
    int16_t origin_y = 0;
    int32_t z_top = 0;
    int32_t z_bottom = 0;

    if (!(frame->sprite_vscale > 0.0f))
        return 0;
    if (hwr_sprites_frame(lookup, &sprite) != 0 || sprite.width == 0u)
        return 0;
    hwr_sprite_origin(lookup, &origin_x, &origin_y);
    hwr_sprite_vertical_span((int32_t)object->world_z, object->flags9,
                             sprite.flags, sprite.width, origin_y,
                             (double)frame->sprite_vscale, &z_top, &z_bottom);
    *out_z = (float)(((double)z_top + (double)z_bottom) * 0.5);
    return 1;
}

/* AND CLEAR OF THE SURFACE IT IS MOUNTED ON.
 *
 * A sconce, a bracket torch or a lit sigil is drawn FLAT ON A WALL -- the
 * engine's own rotated path, record +9 bit 0x80 (hwr_objects.h) -- and the map
 * hangs it two to eight units off that wall's plane. A light there lights the
 * wall it is on at grazing incidence and nowhere else: the floor, the ceiling
 * and every other wall of the room take it, and the one surface the flame is
 * actually against stays dark, which reads as a torch mounted on a shadow.
 *
 * WHICH wall it is mounted on is the nearest face of its own room, and the
 * direction to push is that face's own interior normal -- (dy, -dx), the
 * engine's own visibility rule, which is the same normal a granted face's light
 * is pushed along and is measured POSITIVE for every marked object of every
 * shipped map. Its own facing is deliberately NOT used for the direction: the
 * authored facing is anti-parallel to the room's normal as often as it is
 * parallel, so it says which way the art is turned rather than which side of
 * the wall the room is on.
 *
 * The push is a CLEARANCE rather than a shove: the light is moved along that
 * normal until it is HWR_GLOW_FACE_OFFSET clear of the face -- the same clear
 * a granted face's own light is given -- and a thing already that clear does
 * not move at all. A rotated sprite standing in the open, which is what a
 * hanging banner in the middle of a hall is, is therefore left exactly where it
 * is: nothing is mounted on a wall it is nowhere near.
 *
 * Worked in the sixteen bits the map is authored in without wrapping, exactly
 * as the room test below it is: a room that straddles the wrap is not a room
 * anybody builds, and pretending to handle one would be arithmetic nobody could
 * check. Answers 0 where nothing moved. */
static int hwr_light_wall_clearance(const struct hwr_world *world,
                                    const struct hwr_object *object,
                                    float *x, float *y)
{
    const struct hwr_sector *sector;
    double near_distance = 0.0;
    double near_normal_x = 0.0;
    double near_normal_y = 0.0;
    double near_signed = 0.0;
    int found = 0;
    size_t w;

    if ((object->flags9 & HWR_OBJECT_FLAGS9_ROTATED) == 0)
        return 0;
    if (world == NULL || object->sector_index >= world->sector_count)
        return 0;
    sector = &world->sectors[object->sector_index];
    for (w = 0; w < sector->wall_count; w++) {
        size_t wall_index = sector->first_wall + w;
        const struct hwr_wall *wall;
        const struct hwr_vertex *a;
        const struct hwr_vertex *b;
        double dx;
        double dy;
        double length;
        double along;
        double distance;
        double normal_x;
        double normal_y;
        double from_x;
        double from_y;

        if (wall_index >= world->wall_count)
            break;
        wall = &world->walls[wall_index];
        if (wall->vertex_a_index >= world->vertex_count ||
            wall->vertex_b_index >= world->vertex_count)
            continue;
        a = &world->vertices[wall->vertex_a_index];
        b = &world->vertices[wall->vertex_b_index];
        dx = (double)b->world_x - (double)a->world_x;
        dy = (double)b->world_y - (double)a->world_y;
        length = sqrt(dx * dx + dy * dy);
        if (!(length > 0.0))
            continue;
        from_x = (double)object->world_x - (double)a->world_x;
        from_y = (double)object->world_y - (double)a->world_y;
        /* To the SEGMENT, so the wall it stands against is the one it is
         * against rather than whichever infinite line passes nearest. */
        along = (from_x * dx + from_y * dy) / (length * length);
        if (along < 0.0)
            along = 0.0;
        else if (along > 1.0)
            along = 1.0;
        distance = sqrt((from_x - along * dx) * (from_x - along * dx) +
                        (from_y - along * dy) * (from_y - along * dy));
        if (found && !(distance < near_distance))
            continue;
        normal_x = dy / length;
        normal_y = -dx / length;
        found = 1;
        near_distance = distance;
        near_normal_x = normal_x;
        near_normal_y = normal_y;
        /* And to its PLANE, which is what the light has to be clear of. */
        near_signed = from_x * normal_x + from_y * normal_y;
    }
    if (!found || near_signed >= (double)HWR_GLOW_FACE_OFFSET)
        return 0;
    *x = (float)((double)*x +
                 near_normal_x * ((double)HWR_GLOW_FACE_OFFSET - near_signed));
    *y = (float)((double)*y +
                 near_normal_y * ((double)HWR_GLOW_FACE_OFFSET - near_signed));
    return 1;
}

/* One object into one light, with everything the rows and the map and the look
 * file have to say about it already spent. Returns 0 when the object is not a
 * light at all. */
static int hwr_light_from_object(const struct hwr_lights_frame *frame,
                                 const struct hwr_object *object,
                                 struct hwr_light_candidate *out)
{
    const struct hwr_world *world = frame->world;
    const struct hwr_sector *sector;
    struct hwr_look_light named;
    struct hwr_sprite_lookup lookup;
    int resolved;
    int granted;
    int marked;
    float glow_intensity = 1.0f;
    float glow_range = 1.0f;

    /* Nothing to belong to: a particle and the flying pickup are drawn in the
     * world without being of it, so there is no room whose floor and ceiling
     * would hold their light and no sector for the walk to have drawn. */
    if (object->sector_index == (uint16_t)HWR_OBJECT_NO_SECTOR ||
        object->sector_index >= world->sector_count) {
        g_stats.no_sector++;
        return 0;
    }
    /* The reflected walk's own synthetic player. It exists for a mirror and is
     * not a thing standing in the room. */
    if ((object->flags & HWR_OBJECT_FLAG_REFLECTION_ONLY) != 0) {
        g_stats.overlay++;
        return 0;
    }
    sector = &world->sectors[object->sector_index];
    /* THE GAME CONFIG'S WORD, ASKED FIRST -- and that order is the ruling.
     *
     * The three tables that can name one object are composed by the look layer
     * in the order they rank, least specific first: what a KIND of thing gives
     * off, keyed by the sprite; what a GROUP of them does, keyed by the ID a
     * creator typed to gather them; and what THIS ONE does, keyed by its own
     * object number, which names exactly one thing and is therefore the last
     * word. `off` comes whole from the most specific of the three naming it.
     *
     * It is asked BEFORE the map's own mark rather than after, because A GRANT
     * CREATES A LIGHT. Asked after, a file could only ever dim, recolour or
     * refuse what the map had already marked -- so an entry naming the one
     * candelabra a map forgot to mark did nothing, and said nothing about
     * doing nothing. */
    granted = hwr_look_object_grant(object->map_index, object->object_id,
                                    object->graphic_id, &named);
    /* THE MAP'S OWN MARK: an object whose composed shade byte is zero is the
     * one the map's own author declared never to be darkened, which is the
     * torches, the candles and the lit sigils. It is composed by the shared
     * rule rather than by a copy of it here, because a mark read from a
     * transcription can drift away from the mark the emitter draws by without
     * either side noticing. */
    marked = hwr_light_object_marked(world, object);
    if (!marked) {
        /* PROMOTION. A thing the map never lit is made a light by any grant
         * naming it that does not refuse it -- the colour included, because a
         * promoted thing has a sprite exactly as a marked one does and the
         * colour is read off it by the same law. A grant that states a colour
         * of its own is still the last word; a grant that states none is not an
         * entry that does nothing. */
        if (!granted || named.off)
            return 0;
        g_stats.promoted++;
    } else {
        g_stats.marked++;
        /* And the one place a person overrules the map the other way: a
         * fullbright decal that is plainly not a light is refused here. */
        if (granted && named.off) {
            g_stats.refused++;
            return 0;
        }
    }
    /* The containments are spent on the finished candidate rather than here:
     * what the drawn set is asked about is the light's REACH, and how far this
     * light carries is not settled until the lines below have run. */
    memset(out, 0, sizeof *out);
    out->source = (uint8_t)HWR_LIGHT_CLASS_OBJECT;
    out->source_id = object->graphic_id;
    out->sector_index = object->sector_index;
    /* WHETHER THE ROOM WANTS IT BURNING. A thing stands in its own room, so
     * that room's live word is the one it follows. */
    out->wanted = hwr_light_wanted(world, object->sector_index, &named);
    /* WHAT THE GAME CONFIG SAID about this one casting shadows, carried as
     * the three-state answer it is: nothing said leaves it to the settings
     * row's own count, and true and false are a pin and a refusal that the
     * count does not overrule. Minus one is the tile of every light no map
     * stands for, which is every light until the shadow layer has picked. */
    out->light.shadow_request = hwr_light_shadow_request(&named);
    out->light.shadow_tile = -1;
    /* AND WHICH THING THIS LIGHT IS THE LIGHT OF, by the map's own object
     * number. The shadow layer leaves a light's own card out of its own map --
     * a ward on a door stands between its light and the door -- and this is
     * how it knows which card. A thing no map numbers (a particle, the flying
     * pickup) carries the object list's own sentinel, which is the same
     * "nothing to leave out" every other class says. */
    out->light.source_object = object->map_index;
    /* WHAT NAMES THIS LIGHT ACROSS FRAMES is where the THING stands -- the
     * record's own three numbers -- and not where its light is emitted from.
     * The two are different on purpose (see the emit point below), and a name
     * made of the emit point would make a light that stood still into a
     * different light every time the canvas or the animation moved it, which is
     * the fade and the hysteresis starting again for nothing. */
    out->world_x = object->world_x;
    out->world_y = object->world_y;
    out->world_z = object->world_z;
    /* THE EMIT POINT: the middle of the sprite, clear of whatever it is mounted
     * on. Both are worked out beside the object record they belong to; where
     * neither has anything to say, this is the anchor, which is where the light
     * stood before either existed. */
    resolved = hwr_light_sprite_of(frame, object, &lookup);
    out->light.world_x = (float)object->world_x;
    out->light.world_y = (float)object->world_y;
    out->light.world_z = (float)object->world_z;
    if (resolved)
        (void)hwr_light_sprite_middle(frame, &lookup, object,
                                      &out->light.world_z);
    (void)hwr_light_wall_clearance(world, object, &out->light.world_x,
                                   &out->light.world_y);
    /* The map's own candle word, through exactly the recipe the light the
     * player carries takes it through -- named rather than copied, so the two
     * cannot drift. */
    hwr_gl_glow_factors(frame->candle_glow, g_settings.glow_scale,
                        &glow_intensity, &glow_range);
    out->light.intensity = g_settings.intensity * glow_intensity;
    out->light.range = g_settings.range * glow_range;
    /* THE COLOUR OFF THE SPRITE -- ONE LAW, FOR EVERY THING THAT GIVES LIGHT,
     * AND THE ONLY ONE.
     *
     * A sprite is drawn as a cut-out, so an average over what is not cut away is
     * the colour of the thing itself: a torch answers with its flame and a cold
     * sigil with its own light. That is as true of a thing a game config made a
     * light as of one the map marked, so the reading is the same reading and the
     * promoted case is not a case here at all. There is no row to turn it off
     * with: one colour for every light in the game is a picture nobody asked
     * for, and a thing that wants a colour of its own is NAMED in a game
     * config, where the statement is about that thing. */
    {
        float sprite_color[3];

        /* A sprite that has no colour to give -- one cut entirely away, or one
         * drawn in the palette's own black -- leaves the fallback standing
         * rather than turning the light black. */
        out->light.color[0] = HWR_LIGHT_UNREADABLE_R;
        out->light.color[1] = HWR_LIGHT_UNREADABLE_G;
        out->light.color[2] = HWR_LIGHT_UNREADABLE_B;
        if (resolved && hwr_light_sprite_color(frame, &lookup, sprite_color)) {
            out->light.color[0] = sprite_color[0];
            out->light.color[1] = sprite_color[1];
            out->light.color[2] = sprite_color[2];
        }
    }
    /* And what the game config says, over all of it -- already composed in the
     * order the three tables rank, so this is the one place the answer is
     * spent rather than a fourth place it could be composed differently. */
    if (named.has_intensity)
        out->light.intensity = named.intensity;
    if (named.has_range)
        out->light.range = named.range;
    if (named.has_color) {
        out->light.color[0] = (float)((named.color >> 16) & 0xffu) / 255.0f;
        out->light.color[1] = (float)((named.color >> 8) & 0xffu) / 255.0f;
        out->light.color[2] = (float)(named.color & 0xffu) / 255.0f;
    }
    /* CONTAINMENT: the world-Z window. Its own room's floor and ceiling with
     * slack either side, which the fragment fades it out across. The pair is
     * ordered here so a map with a ceiling under its floor cannot invert it. */
    {
        float low = (float)sector->floor_z;
        float high = (float)sector->ceiling_z;

        if (low > high) {
            float swap = low;

            low = high;
            high = swap;
        }
        out->light.z_lo = low - g_settings.z_slack;
        out->light.z_hi = high + g_settings.z_slack;
    }
    /* CONTAINMENT: the reach. A light with no reach lights nothing, which is
     * what a reach of nothing has to mean rather than a division by zero. */
    if (!(out->light.range > 0.0f))
        return 0;
    if (!(out->light.intensity > 0.0f))
        return 0;
    return 1;
}

/* ------------------------------------------------------------- the enumerator */

/* CONTAINMENT: THE DRAWN SET, ASKED OF WHAT THE LIGHT REACHES.
 *
 * A light's life hangs on whether anything it REACHES is on the screen. It
 * never hangs on whether the point it is emitted from is, and the difference
 * between those two sentences is a bug somebody met while playing: stand behind
 * a pillar so the lamp itself is hidden and the whole pool it casts goes out,
 * while the wall it was lighting is still right there in front of you.
 *
 * The mechanism was the room a light is IN. A map's rooms are convex cells
 * rather than rooms in the everyday sense, so the cell holding a light is
 * routinely a sliver -- an alcove thirty-two units deep in the south wall of the
 * hall it lights, which is exactly where the reported lamp hangs. That sliver
 * leaves the engine's drawn set the moment its own opening is off the screen or
 * behind a pillar, several times a turn, while the hall it opens onto is drawn
 * the whole time. Asking whether the SLIVER was drawn therefore deleted a light
 * standing twenty units from a fully drawn wall it was lighting.
 *
 * So the question asked here is the other one: IS ANY ROOM THIS LIGHT REACHES ON
 * THE SCREEN. Its own room first, which is the common case and costs one array
 * read; and where that room is not drawn, the light's own pool is followed out
 * of it through the map's own openings.
 *
 * Three things bound that walk, and each of them is what keeps this a
 * containment rather than the leak the containment was introduced to stop:
 *
 *   ONLY THROUGH OPENINGS  a wall with no room on the other side is not crossed.
 *                     Light gets out of a room the way it really does, through
 *                     the gaps, and never through the stone.
 *   ONLY WITHIN THE REACH  an opening farther from the light than its own pool
 *                     ENDS is not crossed. This is what stops the walk at
 *                     arm's length: a light in a distant room the engine did
 *                     not draw reaches no drawn room and dies exactly as it
 *                     did before, which is the sigil leak this whole
 *                     containment exists for.
 *   ONLY INSIDE THE WINDOW  a room whose own floor and ceiling do not meet the
 *                     light's world-Z window is not entered, because the
 *                     fragment would fade the light to nothing there anyway. A
 *                     torch still cannot light the storey above it.
 *
 * The first drawn room ends the walk, so the answer is reached at the first
 * opening in the good case and the whole flood is only ever paid for by a light
 * that is about to be refused.
 *
 * What this admits that the old rule did not is a light spilling out of a room
 * that is off the screen onto a drawn surface WITHIN ITS OWN REACH, through a
 * real opening. That is not a leak; that is the light, and refusing it was the
 * complaint. */

static uint32_t *g_reach_stamp;
static uint32_t *g_reach_queue;
static size_t g_reach_capacity;
/* Which walk a stamp belongs to. A generation rather than a clear, so a light
 * whose own room is drawn costs no pass over the map's rooms at all. */
static uint32_t g_reach_generation;

static int hwr_lights_reach_reserve(size_t sectors)
{
    if (sectors == 0u)
        return 0;
    if (g_reach_capacity < sectors) {
        uint32_t *stamp = (uint32_t *)realloc(g_reach_stamp,
                                              sectors * sizeof *stamp);
        uint32_t *queue;

        if (stamp == NULL)
            return 0;
        g_reach_stamp = stamp;
        queue = (uint32_t *)realloc(g_reach_queue, sectors * sizeof *queue);
        if (queue == NULL)
            return 0;
        g_reach_queue = queue;
        /* Grown store, so every stamp in it is meaningless: cleared whole and
         * the generation started again, which cannot be mistaken for a mark. */
        memset(g_reach_stamp, 0, sectors * sizeof *g_reach_stamp);
        g_reach_capacity = sectors;
        g_reach_generation = 0u;
    }
    return 1;
}

/* Whether the drawn set says this room is on the screen.
 *
 * A set that does not answer for a room is NOT a set refusing it: the answer
 * past its end is the same fail-open the per-candidate gate has always made, and
 * the doorway count is what holds those rooms instead. */
static int hwr_lights_room_drawn(const struct hwr_lights_frame *frame,
                                 size_t sector)
{
    return sector >= frame->visible_count || frame->visible[sector] != 0;
}

/* How far a point is from an opening, in the plan, squared -- what is wanted is
 * a comparison against a reach, and a square root would only be undone. The
 * whole segment rather than its middle: an opening is wide, and light gets
 * through the near end of it. */
static float hwr_lights_wall_distance_sq(const struct hwr_world *world,
                                         const struct hwr_wall *wall,
                                         float at_x, float at_y)
{
    const struct hwr_vertex *a;
    const struct hwr_vertex *b;
    float ax;
    float ay;
    float dx;
    float dy;
    float len_sq;
    float along;
    float px;
    float py;

    if (wall->vertex_a_index >= world->vertex_count ||
        wall->vertex_b_index >= world->vertex_count)
        return -1.0f;               /* an opening with no corners: unmeasurable */
    a = &world->vertices[wall->vertex_a_index];
    b = &world->vertices[wall->vertex_b_index];
    ax = (float)a->world_x;
    ay = (float)a->world_y;
    dx = (float)b->world_x - ax;
    dy = (float)b->world_y - ay;
    len_sq = dx * dx + dy * dy;
    along = 0.0f;
    if (len_sq > 0.0f) {
        along = ((at_x - ax) * dx + (at_y - ay) * dy) / len_sq;
        if (along < 0.0f)
            along = 0.0f;
        if (along > 1.0f)
            along = 1.0f;
    }
    px = ax + along * dx;
    py = ay + along * dy;
    return (at_x - px) * (at_x - px) + (at_y - py) * (at_y - py);
}

static int hwr_lights_reach_drawn(const struct hwr_lights_frame *frame,
                                  const struct hwr_light_candidate *candidate)
{
    const struct hwr_world *world = frame->world;
    float reach_sq;
    size_t head = 0;
    size_t tail = 0;

    /* No drawn set at all -- the visibility pass switched off, or a draw list
     * that could not be read. Then every room is a room that might be on the
     * screen and the doorway count is the whole containment. */
    if (!frame->pvs_valid || frame->visible == NULL)
        return 1;
    /* Its own room, which is the answer nearly every frame and the whole reason
     * this costs nothing to ask. */
    if (hwr_lights_room_drawn(frame, candidate->sector_index))
        return 1;
    if (!(candidate->light.range > 0.0f) ||
        !hwr_lights_reach_reserve(world->sector_count))
        return 0;
    reach_sq = candidate->light.range * candidate->light.range;
    if (++g_reach_generation == 0u) {
        memset(g_reach_stamp, 0, g_reach_capacity * sizeof *g_reach_stamp);
        g_reach_generation = 1u;
    }
    g_reach_stamp[candidate->sector_index] = g_reach_generation;
    g_reach_queue[tail++] = candidate->sector_index;
    while (head < tail) {
        uint32_t index = g_reach_queue[head++];
        const struct hwr_sector *sector = &world->sectors[index];
        size_t w;

        if (sector->first_wall > world->wall_count ||
            sector->wall_count > world->wall_count - sector->first_wall)
            continue;
        for (w = 0; w < sector->wall_count; w++) {
            const struct hwr_wall *wall = &world->walls[sector->first_wall + w];
            size_t next = wall->neighbor_sector_index;
            const struct hwr_sector *neighbor;
            float low;
            float high;
            float distance_sq;

            /* Stone. Light does not cross it, and neither does this. */
            if (next == SIZE_MAX || next >= world->sector_count)
                continue;
            if (g_reach_stamp[next] == g_reach_generation)
                continue;
            neighbor = &world->sectors[next];
            low = (float)neighbor->floor_z;
            high = (float)neighbor->ceiling_z;
            if (low > high) {
                float swap = low;

                low = high;
                high = swap;
            }
            /* The world-Z window, which is the same window the fragment fades
             * across: a room the light could only reach outside it is a room the
             * light does not reach. */
            if (high < candidate->light.z_lo || low > candidate->light.z_hi)
                continue;
            distance_sq = hwr_lights_wall_distance_sq(
                world, wall, candidate->light.world_x,
                candidate->light.world_y);
            if (distance_sq < 0.0f || distance_sq > reach_sq)
                continue;
            if (hwr_lights_room_drawn(frame, next))
                return 1;
            g_reach_stamp[next] = g_reach_generation;
            g_reach_queue[tail++] = (uint32_t)next;
        }
    }
    return 0;
}

/* CONTAINMENT: the hop cap, worked out once for the frame rather than per
 * candidate -- AND ONLY WHERE THERE IS NO DRAWN SET TO CONTAIN BY.
 *
 * Which room a light is in is a fact about the map. Which room the eye is in
 * is a fact about where somebody is standing, and it changes every time they
 * take a step. So the two are not the same kind of rule: the drawn set says
 * whether a light's room is ON THE SCREEN, and a light whose room is on the
 * screen has to be lighting it. Counting doorways from the eye instead
 * DELETES that light -- while its room is still in view -- and what that
 * looks like is a pool on a wall going out as you walk away from it. A light
 * that follows the player is not a containment; it is a different bug.
 *
 * The doorway count is therefore the containment of LAST RESORT. Where the
 * engine's own walk gave an answer, that answer is the containment and this
 * one stands down. Where it did not -- the visibility pass switched off, or
 * a draw list that could not be read -- every room is a room that might be
 * on the screen, and then the eye's own room is the only thing left to count
 * from. A camera whose room the engine has not named leaves even that
 * unenforced: refusing every light because one word was not read would be a
 * blackout, not a containment.
 *
 * Rooting the walk at the light rather than at the eye is NOT the fix and is
 * worth saying so once: the portal graph is walked both ways, so the count
 * between two rooms is the same number whichever end it starts from. What
 * had to change is what that number is allowed to decide.
 *
 * A cull set that does not answer for every room of the map is not a drawn
 * set either: the per-candidate gate lets a room past the end of it through, so
 * standing the last resort down on one would leave those rooms with no
 * containment at all. Named here rather than assumed.
 *
 * Written once for every class, because a containment that meant one thing for
 * a torch and another for a lit floor would not be a containment. */
static const uint8_t *hwr_lights_hop_containment(
    const struct hwr_lights_frame *frame)
{
    const uint8_t *hop = NULL;

    if (frame->camera_sector >= 0 &&
        !(frame->pvs_valid && frame->visible != NULL &&
          frame->visible_count >= frame->world->sector_count))
        hop = hwr_lights_hop_map(frame->world, frame->camera_sector,
                                 g_settings.hops);
    return hop;
}

/* BOTH OF THEM, ONCE, over a finished candidate.
 *
 * It is asked here rather than inside each class's own seeder for two reasons.
 * A containment that meant one thing for a torch and another for a lit floor
 * would not be a containment -- and the drawn set is now asked of the light's
 * REACH, which is not known until the map, the rows and the game config have all
 * had their say about how far this particular light carries. */
static int hwr_lights_contained(const struct hwr_lights_frame *frame,
                                const struct hwr_light_candidate *candidate,
                                const uint8_t *hop)
{
    /* THE ROOM'S OWN SWITCH, asked first because it is the cheapest and the
     * most fundamental: a light whose room has its fixtures out is not a light
     * that is anywhere, so there is nothing to ask the drawn set about.
     *
     * It is a DROP only once the light is fully out. While anything is still
     * held for it the candidate is carried -- unwanted, ranked last, never
     * chosen -- so that the ramp below can walk it down. Dropping it the frame
     * the switch flipped would take it out of the array, and a light that is
     * not in the array cannot fade: it would pop. Once the ramp is spent
     * nothing is held for it any more, and from then on it costs a slot in
     * nobody's array and a place in nobody's ranking. */
    if (!candidate->wanted && hwr_light_find_retained(candidate) == NULL) {
        g_stats.unlit++;
        return 0;
    }
    if (!hwr_lights_reach_drawn(frame, candidate)) {
        g_stats.unseen++;
        return 0;
    }
    if (hop != NULL && hop[candidate->sector_index] == 0xffu) {
        g_stats.too_far++;
        return 0;
    }
    return 1;
}

static size_t hwr_lights_seed_objects(const struct hwr_lights_frame *frame,
                                      struct hwr_light_candidate *candidates,
                                      size_t max)
{
    const uint8_t *hop = NULL;
    size_t count = 0;
    size_t i;

    if (frame->objects == NULL || !frame->objects->valid)
        return 0;
    /* THE GAME CONFIG'S OWN NAMED THINGS, offered every object the map holds
     * before any of them is judged.
     *
     * EVERY object, not only the ones that become lights: an object refused as
     * not a light, or standing in a room the walk did not draw, is still an
     * object standing there, and an entry addressing one must never be reported
     * as addressing nothing. That is why this is a walk of its own rather than a
     * line inside the seeding below -- the seeding's own answer is about
     * whether a thing lights the room, and this question is about whether it
     * exists.
     *
     * The picture each one is DRAWN with is offered too, because the sprite
     * blanket is addressed by that rather than by a number out of this map's own
     * lists, and a blanket a map draws nothing with is as silent a mistake as a
     * number nothing answers to.
     *
     * And the records the map numbers and never draws are offered too, so that
     * "there is no object 70" is never said about an object 70 that is there and
     * hidden.
     *
     * Taken ONCE PER MAP rather than once per frame, and not taken at all on a
     * map with nothing to check, which is every map until somebody tunes one. */
    if (hwr_look_objects_pending()) {
        for (i = 0; i < frame->objects->count; i++) {
            const struct hwr_object *object = &frame->objects->items[i];

            hwr_look_object_seen(object->map_index, object->object_id,
                                 object->graphic_id);
        }
        for (i = 0; i < frame->objects->hidden_count; i++)
            hwr_look_object_hidden(frame->objects->hidden[i]);
        hwr_look_objects_settled(frame->objects->numbered);
    }
    hop = hwr_lights_hop_containment(frame);
    for (i = 0; i < frame->objects->count && count < max; i++) {
        const struct hwr_object *object = &frame->objects->items[i];
        struct hwr_light_candidate candidate;

        if (!hwr_light_from_object(frame, object, &candidate))
            continue;
        if (!hwr_lights_contained(frame, &candidate, hop))
            continue;
        candidates[count++] = candidate;
    }
    return count;
}

/* One granted face-set into one light. Everything the map, the rows and the
 * game config have to say about it is already spent: what is left is where it
 * stands, what it is worth, and the containments.
 *
 * WHAT THIS DOES NOT DO, and the ruling behind it: nothing here changes how the
 * granted surface is DRAWN. Whether a face renders fullbright stays the map's
 * own glow flag and the engine's own glow colours, exactly as it was before
 * anybody wrote a grant. A grant adds a light to a room; it is not a second way
 * of painting the thing that gives it, and inventing one would put the
 * authentic picture at risk for nothing. */
static int hwr_light_from_glow_face(const struct hwr_lights_frame *frame,
                                    const struct hwr_glow_face *face,
                                    struct hwr_light_candidate *out)
{
    const struct hwr_world *world = frame->world;
    const struct hwr_sector *sector;
    float glow_intensity = 1.0f;
    float glow_range = 1.0f;

    if (face->sector_index >= world->sector_count)
        return 0;
    sector = &world->sectors[face->sector_index];
    /* The containments are spent on the finished candidate; see the object
     * seeder above for why they cannot be spent here. */
    memset(out, 0, sizeof *out);
    out->source = (uint8_t)HWR_LIGHT_CLASS_GLOW_FACE;
    out->source_id = face->source_id;
    out->sector_index = face->sector_index;
    /* WHETHER THE ROOM WANTS IT BURNING. A granted surface's light is one per
     * ROOM per texture and was gathered out of that room's own faces, so it has
     * a governing room as plainly as a torch does. */
    out->wanted = hwr_light_wanted(world, face->sector_index, &face->light);
    out->light.shadow_request = hwr_light_shadow_request(&face->light);
    out->light.shadow_tile = -1;
    /* A PAINTED SURFACE IS NOT A THING THE MAP NUMBERS, so there is no card of
     * its own to leave out of its own map -- and a granted face's light has
     * none by construction, since the card slice is objects and nothing else. */
    out->light.source_object = HWR_GL_LIGHT_NO_SOURCE;
    out->world_x = face->world_x;
    out->world_y = face->world_y;
    out->world_z = face->world_z;
    out->light.world_x = (float)face->world_x;
    out->light.world_y = (float)face->world_y;
    out->light.world_z = (float)face->world_z;
    /* The map's own candle word, through exactly the recipe every other light
     * takes it through -- named rather than copied, so the two cannot drift. */
    hwr_gl_glow_factors(frame->candle_glow, g_settings.glow_scale,
                        &glow_intensity, &glow_range);
    out->light.intensity = g_settings.intensity * glow_intensity;
    out->light.range = g_settings.range * glow_range;
    /* The colour is the file's, always: a grant without one is refused where
     * the map is staged, so by here there is one. */
    out->light.color[0] = (float)((face->light.color >> 16) & 0xffu) / 255.0f;
    out->light.color[1] = (float)((face->light.color >> 8) & 0xffu) / 255.0f;
    out->light.color[2] = (float)(face->light.color & 0xffu) / 255.0f;
    if (face->light.has_intensity)
        out->light.intensity = face->light.intensity;
    if (face->light.has_range)
        out->light.range = face->light.range;
    /* CONTAINMENT: the world-Z window, its own room's floor and ceiling with
     * slack either side. Ordered here so a map with a ceiling under its floor
     * cannot invert it. */
    {
        float low = (float)sector->floor_z;
        float high = (float)sector->ceiling_z;

        if (low > high) {
            float swap = low;

            low = high;
            high = swap;
        }
        out->light.z_lo = low - g_settings.z_slack;
        out->light.z_hi = high + g_settings.z_slack;
    }
    /* CONTAINMENT: the reach. */
    if (!(out->light.range > 0.0f))
        return 0;
    if (!(out->light.intensity > 0.0f))
        return 0;
    return 1;
}

static size_t hwr_lights_seed_glow_faces(const struct hwr_lights_frame *frame,
                                         struct hwr_light_candidate *candidates,
                                         size_t max)
{
    const uint8_t *hop;
    size_t count = 0;
    size_t i;

    /* WHERE THE GRANTED FACES ARE IS NOT ASKED HERE. It was worked out when the
     * map was staged, and these are the two comparisons that say so.
     *
     * The first is what was GRANTED: somebody editing the game config and it
     * being read again, which moves the look layer's own generation.
     *
     * The second is the MAP ITSELF: a face repainted by a material command, an
     * animated surface stepping onto or off the granted picture, a lift that
     * carried the floor its light stands over. Those move the map's own surfaces
     * under an answer worked out when it was staged, and a set that did not
     * follow them would light a face that no longer carries the texture and
     * leave dark one that just took it up. A face animating between a granted
     * picture and an ungranted one therefore pulses with its own animation,
     * which is what the map is doing.
     *
     * Both are worked out only where something is granted at all -- which is
     * every map until somebody describes one -- and a frame in which nothing
     * moved walks no wall. */
    if (g_glow_face_generation != hwr_look_glow_generation()) {
        hwr_lights_stage_glow_faces(frame->world, frame->map_archive,
                                    frame->shared_archive);
    } else if (hwr_look_texture_count() != 0u || hwr_look_face_count() != 0u) {
        uint32_t stamp = hwr_glow_surface_stamp(frame->world);

        if (stamp != g_glow_face_stamp)
            hwr_lights_stage_glow_faces(frame->world, frame->map_archive,
                                        frame->shared_archive);
    }
    if (g_glow_face_count == 0u)
        return 0;
    hop = hwr_lights_hop_containment(frame);
    for (i = 0; i < g_glow_face_count && count < max; i++) {
        struct hwr_light_candidate candidate;

        if (!hwr_light_from_glow_face(frame, &g_glow_faces[i], &candidate))
            continue;
        if (!hwr_lights_contained(frame, &candidate, hop))
            continue;
        candidates[count++] = candidate;
    }
    return count;
}

/* One free-standing light into one light. There is nothing to compose here and
 * that is the point: no map mark to read, no kind to fall back on, no sprite to
 * take a colour from. What the file said IS the light, and the only things this
 * adds are the containments every other class is held by.
 *
 * The strength and the reach fall to the settings rows where the entry says
 * nothing, exactly as a torch's do, so a person may hang a light and tune the
 * whole rig afterwards without going back through the file. */
static int hwr_light_from_placed(const struct hwr_lights_frame *frame,
                                 const struct hwr_placed_light *placed,
                                 struct hwr_light_candidate *out)
{
    const struct hwr_world *world = frame->world;
    const struct hwr_sector *sector;
    float glow_intensity = 1.0f;
    float glow_range = 1.0f;

    if (placed->sector_index >= world->sector_count)
        return 0;
    sector = &world->sectors[placed->sector_index];
    /* The containments are spent on the finished candidate; see the object
     * seeder above for why they cannot be spent here. */
    memset(out, 0, sizeof *out);
    out->source = (uint8_t)HWR_LIGHT_CLASS_PLACED;
    out->light.shadow_request = hwr_light_shadow_request(&placed->light);
    out->light.shadow_tile = -1;
    /* Nothing numbers a light that belongs to nothing, so what tells two of them
     * apart across frames is where each hangs -- which is exactly what the
     * retained set already matches on. The source number is left at nought
     * rather than invented, and the CLASS is what keeps it from colliding with a
     * torch of graphic nought standing in the same place. */
    out->source_id = 0u;
    /* And for the same reason it has no card of its own to leave out of its
     * own map: a light somebody hung at three numbers is drawn by nothing. */
    out->light.source_object = HWR_GL_LIGHT_NO_SOURCE;
    out->sector_index = placed->sector_index;
    /* WHETHER THE ROOM WANTS IT BURNING. The room a free-standing light hangs
     * in was worked out when the map was staged; its LIVE state is read here,
     * every frame, because that is the thing a switch moves. */
    out->wanted = hwr_light_wanted(world, placed->sector_index, &placed->light);
    out->world_x = placed->world_x;
    out->world_y = placed->world_y;
    out->world_z = placed->world_z;
    out->light.world_x = (float)placed->world_x;
    out->light.world_y = (float)placed->world_y;
    out->light.world_z = (float)placed->world_z;
    /* The map's own candle word, through exactly the recipe every other light
     * takes it through -- named rather than copied, so the two cannot drift. */
    hwr_gl_glow_factors(frame->candle_glow, g_settings.glow_scale,
                        &glow_intensity, &glow_range);
    out->light.intensity = g_settings.intensity * glow_intensity;
    out->light.range = g_settings.range * glow_range;
    /* The colour is the file's, always: an entry without one is refused where
     * the map is staged, so by here there is one. */
    out->light.color[0] = (float)((placed->light.color >> 16) & 0xffu) / 255.0f;
    out->light.color[1] = (float)((placed->light.color >> 8) & 0xffu) / 255.0f;
    out->light.color[2] = (float)(placed->light.color & 0xffu) / 255.0f;
    if (placed->light.has_intensity)
        out->light.intensity = placed->light.intensity;
    if (placed->light.has_range)
        out->light.range = placed->light.range;
    /* CONTAINMENT: the world-Z window, its own room's floor and ceiling with
     * slack either side. Ordered here so a map with a ceiling under its floor
     * cannot invert it. */
    {
        float low = (float)sector->floor_z;
        float high = (float)sector->ceiling_z;

        if (low > high) {
            float swap = low;

            low = high;
            high = swap;
        }
        out->light.z_lo = low - g_settings.z_slack;
        out->light.z_hi = high + g_settings.z_slack;
    }
    /* CONTAINMENT: the reach. */
    if (!(out->light.range > 0.0f))
        return 0;
    if (!(out->light.intensity > 0.0f))
        return 0;
    return 1;
}

static size_t hwr_lights_seed_placed(const struct hwr_lights_frame *frame,
                                     struct hwr_light_candidate *candidates,
                                     size_t max)
{
    const uint8_t *hop;
    size_t count = 0;
    size_t i;

    /* WHICH ROOM EACH ONE HANGS IN IS NOT ASKED HERE. It was worked out when the
     * map was staged, and this is the one comparison that says so: the only
     * thing that can move the answer without a map load is somebody editing the
     * game config and asking for it to be read again, and that moves the look
     * layer's own generation. */
    if (g_placed_generation != hwr_look_placed_generation())
        hwr_lights_stage_placed(frame->world);
    if (g_placed_count == 0u)
        return 0;
    hop = hwr_lights_hop_containment(frame);
    for (i = 0; i < g_placed_count && count < max; i++) {
        struct hwr_light_candidate candidate;

        if (!hwr_light_from_placed(frame, &g_placed[i], &candidate))
            continue;
        if (!hwr_lights_contained(frame, &candidate, hop))
            continue;
        candidates[count++] = candidate;
    }
    return count;
}

size_t hwr_lights_build(const struct hwr_lights_frame *frame,
                        struct hwr_gl_point_light *out, size_t max)
{
    /* Both scratch arrays are FILE STATICS rather than locals, which is what
     * they became when the ceiling grew: together they are a hundred and
     * twenty kilobytes, and a stack frame that size in a plugin loaded into
     * somebody else's thread is a thing to be asked for rather than assumed.
     * The builder is not re-entrant and never was, and every entry either one
     * is read at is one this call wrote. */
    static struct hwr_light_candidate candidates[HWR_LIGHT_CANDIDATE_MAX];
    static struct hwr_light_retained next[HWR_LIGHT_RETAIN_MAX];
    size_t next_count = 0;
    size_t candidate_count = 0;
    size_t wanted_count = 0;
    size_t count = 0;
    size_t cap;
    size_t hard;
    unsigned class_index;
    float step;
    size_t i;

    memset(&g_stats, 0, sizeof g_stats);
    if (frame == NULL || out == NULL || max == 0u || frame->world == NULL ||
        !frame->world->valid) {
        hwr_lights_reset();
        return 0;
    }
    /* THE CAP IS THE LIGHT-COUNT DIAL, held to the pass's own carrying bound.
     *
     * The dial ("how many light sources at once") is the player's, by the
     * director's ruling: the limits of the machine are the player's to
     * spend, so the row runs to the array's own honest ceiling -- the
     * uniform block's, two hundred and fifty-six -- and the DEFAULT is set
     * where a light-filled hall stands whole. It was the sixteen the old
     * hard ceiling allowed, which is the number that ended the popping-pools
     * report (a room with seventeen marked lights once showed eight pools
     * and nine dark ones under an eight-row cap; a light whose pool is in
     * view and unlit is not a quality trade, it is a wrong picture); it is
     * thirty-two now, because sixteen was the old ceiling wearing a
     * default's clothes rather than a number anybody chose. Within the dial every candidate is
     * published worth-most first, and the SHADOW spending is the row below,
     * which the shadow layer holds to this dial in turn -- a shadow count
     * past the light count names nothing.
     *
     * What crossing the dial costs is stated rather than hidden: with the
     * whole allowance chosen there is no spare slot for a light on its way
     * out, so in a scene holding more candidates than the dial the one that
     * loses the last place goes out in a frame rather than over a moment.
     * That light is by construction the least contributing of the set, and
     * the standing bonus below keeps the boundary from trading places on a
     * whim. A light entering the set still comes up its ramp. A dial of
     * nought is no dial at all -- an offline rig that published no rows --
     * and reads as the carrying bound, because the row's own floor is one. */
    hard = max < HWR_GL_POINT_LIGHT_MAX ? max : HWR_GL_POINT_LIGHT_MAX;
    cap = hard;
    if (g_settings.max != 0u && (size_t)g_settings.max < cap)
        cap = (size_t)g_settings.max;
    for (class_index = 0; class_index < (unsigned)HWR_LIGHT_CLASS_COUNT;
         class_index++) {
        if ((g_settings.classes & (1u << class_index)) == 0u)
            continue;
        switch ((enum hwr_light_class)class_index) {
        case HWR_LIGHT_CLASS_OBJECT:
            /* Seeded from the map's own unshaded mark. */
            candidate_count += hwr_lights_seed_objects(
                frame, candidates + candidate_count,
                HWR_LIGHT_CANDIDATE_MAX - candidate_count);
            break;
        case HWR_LIGHT_CLASS_GLOW_FACE:
            /* Seeded from the faces a game config grants a light, one per room
             * per texture, at the centroid of that room's own granted faces --
             * a set worked out when the map was staged rather than now. */
            candidate_count += hwr_lights_seed_glow_faces(
                frame, candidates + candidate_count,
                HWR_LIGHT_CANDIDATE_MAX - candidate_count);
            break;
        case HWR_LIGHT_CLASS_MUZZLE:
            /* One frame at the player, when the weapon fires. */
            break;
        case HWR_LIGHT_CLASS_ENVIRONMENT:
            /* Seeded at the faces a map leaves open to the sky. */
            break;
        case HWR_LIGHT_CLASS_PLACED:
            /* The lights a game config hangs where the map has none. Their
             * rooms were worked out when the map was staged, exactly as the
             * granted faces' were. */
            candidate_count += hwr_lights_seed_placed(
                frame, candidates + candidate_count,
                HWR_LIGHT_CANDIDATE_MAX - candidate_count);
            break;
        case HWR_LIGHT_CLASS_COUNT:
            /* Not a class: the loop above never reaches it. */
            break;
        }
    }

    /* How far a level may move this frame. A fade of nothing is no fade: a
     * light arrives at full and leaves at once, which is what somebody who set
     * the row to zero asked for. */
    step = 1.0f;
    if (g_settings.fade_seconds > 0.0f && frame->now_ns != 0u &&
        g_last_ns != 0u && frame->now_ns > g_last_ns) {
        double seconds = (double)(frame->now_ns - g_last_ns) / 1e9;

        step = (float)(seconds / (double)g_settings.fade_seconds);
        if (step > 1.0f)
            step = 1.0f;
        if (step < 0.0f)
            step = 0.0f;
    } else if (g_settings.fade_seconds > 0.0f && frame->now_ns != 0u &&
               g_last_ns == 0u) {
        /* The first frame after a reset has no interval to measure, and a light
         * held at nothing until a second frame arrives would be a blink. */
        step = 1.0f;
    }
    if (frame->now_ns != 0u)
        g_last_ns = frame->now_ns;

    /* What each candidate was standing at, which is what the fade and the
     * hysteresis are. A candidate nothing was holding starts from nothing and
     * comes up; one that was held keeps its level.
     *
     * The RANKING is at full strength whatever the level is. Ranking a light by
     * what it is currently emitting would let a light on its way out out-score
     * the light replacing it, and the two would then trade places for ever: what
     * decides which lights matter is what they are worth, and the level is only
     * how far along it is toward being worth it. */
    for (i = 0; i < candidate_count; i++) {
        const struct hwr_light_retained *held =
            hwr_light_find_retained(&candidates[i]);

        candidates[i].level = held != NULL ? held->level : 0.0f;
        candidates[i].stood = held != NULL && held->stood;
        candidates[i].worth = hwr_light_score(frame, &candidates[i].light,
                                              1.0f, 0);
        /* AND WHAT IT IS WORTH FOR THE CHOOSING: nothing at all where the room
         * does not want it burning. Every candidate the room DOES want scores
         * above nothing -- a light with no reach or no strength was refused
         * before it got here -- so an unwanted one sorts strictly below all of
         * them and can never take a place from a light that is standing. What
         * is left of it is the way out: it is carried, it is not chosen, and it
         * walks down the ramp below. Its plain worth is untouched, because that
         * is a fact about the light rather than about this frame's choice. */
        candidates[i].score = candidates[i].wanted
                            ? hwr_light_score(frame, &candidates[i].light,
                                              1.0f, candidates[i].stood)
                            : 0.0f;
        wanted_count += (size_t)(candidates[i].wanted != 0);
    }
    /* Sorted by what each is worth, and STABLY: the map's own order breaks a
     * tie, and that order does not move between frames. An unstable sort here
     * would be a light set that churns on two equal torches, which is the
     * flicker this whole family began as a complaint about. */
    for (i = 1; i < candidate_count; i++) {
        struct hwr_light_candidate item = candidates[i];
        size_t j = i;

        while (j > 0 && candidates[j - 1].score < item.score) {
            candidates[j] = candidates[j - 1];
            j--;
        }
        candidates[j] = item;
    }
    /* What the cap DROPPED, counted over the candidates that were asking for a
     * place. One on its way out is not asking: it is already going. */
    if (wanted_count > cap)
        g_stats.capped = (unsigned long)(wanted_count - cap);

    for (i = 0; i < candidate_count && next_count < HWR_LIGHT_RETAIN_MAX; i++) {
        struct hwr_light_candidate *candidate = &candidates[i];
        struct hwr_light_retained *held = &next[next_count];
        int chosen = candidate->wanted && i < cap;
        float level = candidate->level;

        /* Toward one where it is chosen and toward nothing where it is not.
         * The two ends are the same ramp, so a light that comes up and goes
         * down again traces the same path both ways. */
        if (chosen) {
            level += step;
            if (level > 1.0f)
                level = 1.0f;
        } else {
            level -= step;
            if (level < 0.0f)
                level = 0.0f;
        }
        if (!chosen && level <= 0.0f)
            continue;                       /* out, and no longer held */
        memset(held, 0, sizeof *held);
        held->used = 1;
        held->source = candidate->source;
        held->source_id = candidate->source_id;
        held->sector_index = candidate->sector_index;
        held->world_x = candidate->world_x;
        held->world_y = candidate->world_y;
        held->world_z = candidate->world_z;
        held->level = level;
        held->light = candidate->light;
        held->stood = (uint8_t)(chosen ? 1 : 0);
        next_count++;
        if (!chosen)
            g_stats.fading++;
        /* A chosen light always has a slot -- there are never more chosen than
         * the pass carries. One on its way out takes what is left over, and
         * where nothing is left over it is simply not drawn: the cap is a cap. */
        if (count >= hard)
            continue;
        out[count] = candidate->light;
        out[count].intensity = candidate->light.intensity * level;
        /* And what it is WORTH, which rides the light because the shadow
         * layer ranks by it too: the tile a light holds is kept or lost by
         * the same number the slot it stands in is, and one arithmetic
         * published once is the only way the two orders cannot drift.
         *
         * The PLAIN worth, without the standing bonus the choosing above
         * spends: the shadow layer keeps a hysteresis of its own, about
         * holding a TILE, and a light already carrying one bonus into that
         * comparison would be held twice for one reason. */
        out[count].score = candidate->worth;
        count++;
    }
    if (next_count != 0)
        memcpy(g_retained, next, next_count * sizeof g_retained[0]);
    if (next_count < HWR_LIGHT_RETAIN_MAX)
        memset(&g_retained[next_count], 0,
               (HWR_LIGHT_RETAIN_MAX - next_count) * sizeof g_retained[0]);
    g_retained_count = next_count;
    return count;
}
