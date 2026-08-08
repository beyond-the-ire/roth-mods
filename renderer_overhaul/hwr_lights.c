#include "hwr_lights.h"

#include "hwr_das.h"
#include "hwr_look.h"
#include "hwr_objects.h"
#include "hwr_sprites.h"
#include "hwr_world.h"

#include <math.h>
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
 *   the portal walk   a light in a room the engine's own walk did not draw is
 *                     not seeded at all. This is free -- the walk has already
 *                     run -- and it removes the worst class outright: light
 *                     from a room that cannot be seen from here. It is also the
 *                     only one of the four that decides whether a light EXISTS,
 *                     which is a ruling rather than an implementation: a light
 *                     whose room is on the screen is lighting that room, however
 *                     far away the person looking at it happens to be standing.
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

static int hwr_light_sprite_color(const struct hwr_lights_frame *frame,
                                  const struct hwr_object *object,
                                  float out[3])
{
    struct hwr_sprite_lookup lookup;
    struct hwr_sprite_frame sprite;
    struct hwr_light_color_entry *slot;
    size_t i;

    if (frame->api == NULL || frame->world == NULL ||
        (frame->map_archive == NULL && frame->shared_archive == NULL))
        return 0;
    if (hwr_sprites_resolve(frame->api, frame->map_archive,
                            frame->shared_archive, object->graphic_id,
                            object->facing, frame->view_yaw, 0,
                            object->backref, &lookup) != 0)
        return 0;
    if (lookup.archive == NULL)
        return 0;
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
     * identity is where it is and what it is drawn with. */
    uint16_t graphic_id;
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

void hwr_lights_reset(void)
{
    memset(g_retained, 0, sizeof g_retained);
    g_retained_count = 0;
    g_last_ns = 0;
    memset(g_color_cache, 0, sizeof g_color_cache);
    g_color_cache_count = 0;
    memset(&g_stats, 0, sizeof g_stats);
}

/* ------------------------------------------------------------ the candidates */

struct hwr_light_candidate {
    struct hwr_gl_point_light light;
    uint16_t graphic_id;
    uint16_t sector_index;
    int16_t world_x;
    int16_t world_y;
    int16_t world_z;
    float score;
    float level;
    int stood;
};

/* Room for every candidate the cap could ever have to choose between. A map
 * with more lights in view than this keeps the first of them, in the map's own
 * order, which is stable from frame to frame -- so an overflow is a set that is
 * smaller than it might be, never a set that churns. */
#define HWR_LIGHT_CANDIDATE_MAX 64u

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

        if (held->used && held->graphic_id == candidate->graphic_id &&
            held->sector_index == candidate->sector_index &&
            held->world_x == candidate->world_x &&
            held->world_y == candidate->world_y &&
            held->world_z == candidate->world_z)
            return held;
    }
    return NULL;
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
    struct hwr_look_light placed;
    int by_place;
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
    /* THE MARK, and the whole of the seeding rule: an object whose composed
     * shade byte is zero is the one the map's own author declared never to be
     * darkened, which is the torches, the candles and the lit sigils. It is
     * composed by the shared rule rather than by a copy of it here, because a
     * mark read from a transcription can drift away from the mark the emitter
     * draws by without either side noticing. */
    if (hwr_object_composed_light(object->seed, sector->light) != 0)
        return 0;
    g_stats.marked++;
    /* And the game config's word, which is the one place a person overrules the
     * map: a fullbright decal that is plainly not a light is refused here.
     *
     * It is asked in the two spellings it has, and in the order it ranks them:
     * what a KIND of thing gives off, and then what THIS ONE does. A placed
     * thing is the most specific statement anybody made about a light, so it is
     * the last word -- and `off` is the whole entry rather than a field, so the
     * most specific table naming a thing at all is the one that answers for
     * whether it is a light. That is what lets one brazier of a kind be turned
     * off with every other brazier of that kind left alight, and one hearth be
     * called a light in a map whose decals of that graphic are not. */
    (void)hwr_look_light_for(object->graphic_id, &named);
    by_place = hwr_look_light_at(object->world_x, object->world_y,
                                 object->world_z, &placed);
    if (by_place ? placed.off : named.off) {
        g_stats.refused++;
        return 0;
    }
    /* CONTAINMENT: the portal walk. A light whose room the engine did not draw
     * is a light in a room that cannot be seen from here. */
    if (frame->pvs_valid && frame->visible != NULL &&
        object->sector_index < frame->visible_count &&
        !frame->visible[object->sector_index]) {
        g_stats.unseen++;
        return 0;
    }
    memset(out, 0, sizeof *out);
    out->graphic_id = object->graphic_id;
    out->sector_index = object->sector_index;
    out->world_x = object->world_x;
    out->world_y = object->world_y;
    out->world_z = object->world_z;
    out->light.world_x = (float)object->world_x;
    out->light.world_y = (float)object->world_y;
    out->light.world_z = (float)object->world_z;
    /* The map's own candle word, through exactly the recipe the light the
     * player carries takes it through -- named rather than copied, so the two
     * cannot drift. */
    hwr_gl_glow_factors(frame->candle_glow, g_settings.glow_scale,
                        &glow_intensity, &glow_range);
    out->light.intensity = g_settings.intensity * glow_intensity;
    out->light.range = g_settings.range * glow_range;
    out->light.color[0] = g_settings.color[0];
    out->light.color[1] = g_settings.color[1];
    out->light.color[2] = g_settings.color[2];
    if (g_settings.color_from_sprite) {
        float sprite_color[3];

        /* A sprite that has no colour to give -- one cut entirely away, or one
         * drawn in the palette's own black -- leaves the chosen colour
         * standing rather than turning the light black. */
        if (hwr_light_sprite_color(frame, object, sprite_color)) {
            out->light.color[0] = sprite_color[0];
            out->light.color[1] = sprite_color[1];
            out->light.color[2] = sprite_color[2];
        }
    }
    /* What the game config says about this particular graphic, over all of it --
     * and then what it says about THIS ONE over that, FIELD by field, so a
     * hearth may be made stronger than every other brazier of its kind without
     * restating the colour its kind was already given. */
    if (by_place) {
        if (placed.has_intensity) {
            named.has_intensity = 1;
            named.intensity = placed.intensity;
        }
        if (placed.has_range) {
            named.has_range = 1;
            named.range = placed.range;
        }
        if (placed.has_color) {
            named.has_color = 1;
            named.color = placed.color;
        }
    }
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

static size_t hwr_lights_seed_objects(const struct hwr_lights_frame *frame,
                                      struct hwr_light_candidate *candidates,
                                      size_t max)
{
    const uint8_t *hop = NULL;
    size_t count = 0;
    size_t i;

    if (frame->objects == NULL || !frame->objects->valid)
        return 0;
    /* THE GAME CONFIG'S OWN PLACED THINGS, offered every object the map holds
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
     * Taken ONCE PER MAP rather than once per frame, and not taken at all on a
     * map with nothing to check, which is every map until somebody tunes one. */
    if (hwr_look_objects_pending()) {
        for (i = 0; i < frame->objects->count; i++) {
            const struct hwr_object *object = &frame->objects->items[i];

            hwr_look_object_seen(object->world_x, object->world_y,
                                 object->world_z, object->graphic_id);
        }
        hwr_look_objects_settled();
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
     * set either: the gate above lets a room past the end of it through, so
     * standing the last resort down on one would leave those rooms with no
     * containment at all. Named here rather than assumed. */
    if (frame->camera_sector >= 0 &&
        !(frame->pvs_valid && frame->visible != NULL &&
          frame->visible_count >= frame->world->sector_count))
        hop = hwr_lights_hop_map(frame->world, frame->camera_sector,
                                 g_settings.hops);
    for (i = 0; i < frame->objects->count && count < max; i++) {
        const struct hwr_object *object = &frame->objects->items[i];
        struct hwr_light_candidate candidate;

        if (!hwr_light_from_object(frame, object, &candidate))
            continue;
        if (hop != NULL && hop[candidate.sector_index] == 0xffu) {
            g_stats.too_far++;
            continue;
        }
        candidates[count++] = candidate;
    }
    return count;
}

size_t hwr_lights_build(const struct hwr_lights_frame *frame,
                        struct hwr_gl_point_light *out, size_t max)
{
    struct hwr_light_candidate candidates[HWR_LIGHT_CANDIDATE_MAX];
    struct hwr_light_retained next[HWR_LIGHT_RETAIN_MAX];
    size_t next_count = 0;
    size_t candidate_count = 0;
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
    /* Two caps, and the difference between them is where a light goes out.
     *
     * The first is how many lights are CHOSEN, which is the quality row. The
     * second is how many the pass can carry at all. A light that loses its
     * place among the chosen goes out over a moment rather than in one frame,
     * and it spends that moment in the room between the two -- so the row keeps
     * meaning "how many light this scene" while nothing ever vanishes mid-step.
     * At the highest quality setting the two are the same number and there is
     * no such room, which is stated rather than worked around: somebody asking
     * for every slot to be a chosen light has asked for exactly that. */
    hard = max < HWR_GL_POINT_LIGHT_MAX ? max : HWR_GL_POINT_LIGHT_MAX;
    cap = g_settings.max;
    if (cap > hard)
        cap = hard;
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
        candidates[i].score = hwr_light_score(frame, &candidates[i].light,
                                              1.0f, candidates[i].stood);
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
    if (candidate_count > cap)
        g_stats.capped = (unsigned long)(candidate_count - cap);

    for (i = 0; i < candidate_count && next_count < HWR_LIGHT_RETAIN_MAX; i++) {
        struct hwr_light_candidate *candidate = &candidates[i];
        struct hwr_light_retained *held = &next[next_count];
        int chosen = i < cap;
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
        held->graphic_id = candidate->graphic_id;
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
