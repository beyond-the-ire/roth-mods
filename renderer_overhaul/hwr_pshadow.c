#include "hwr_pshadow.h"

#include "hwr_gl.h"
#include "hwr_lights.h"
#include "hwr_point_math.h"
#include "hwr_sprites.h"
#include "hwr_sun.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The policy of the point-light shadows; the design is stated in the header.
 * What this file holds is who casts -- the count in force, each light's own
 * word over it, and the hysteresis that keeps a tile where it is -- the tile
 * ledger (which light owns which atlas tile and what STAMP its contents were
 * rendered at), and the per-light moving emitter, which is the sun's own law
 * aimed at a point: the card sizing arithmetic is the billboard emitter's, the
 * card geometry is hwr_sunm_card out of the shared header, and the axis is the
 * horizontal perpendicular of the line from THIS light to the object
 * (hwr_psm_card_axis) rather than one direction for the whole sky. A decoded
 * 3D prop is aimed at nothing -- it is real geometry, and the same triangles
 * serve every light that sees them -- so it goes through the shared prop
 * emitter unchanged; what is decided here about a prop is only whether it is
 * near enough to this light to matter. */

_Static_assert((unsigned)HWR_PSM_MAX_LIGHTS == (unsigned)HWR_GL_PSHADOW_MAX,
               "the policy and execution tile ceilings must agree");
_Static_assert((unsigned)HWR_PSM_MAX_LIGHTS <=
                   (unsigned)HWR_GL_POINT_LIGHT_MAX,
               "the tile ceiling can never stand above the light array's "
               "own: a light with no slot in the picture has nothing to "
               "cast into");
_Static_assert((uint32_t)HWR_GL_LIGHT_NO_SOURCE ==
                   (uint32_t)HWR_OBJECT_NO_INDEX,
               "a light with no source and a thing no map numbers say the "
               "same word: the card walk compares one against the other");

/* THE MARGIN a challenger must beat the light holding a tile by, which is the
 * light array's own quarter (hwr_light_score's standing bonus) rather than a
 * second number: one law about how much better is better, spelled once in
 * each of the two places a set of lights settles. */
#define HWR_PS_HYSTERESIS 1.25f

/* Engine globals, read the same way the sun's card emitter reads them. */
#define VA_G_SINCOS_TABLE 0x72080u
#define VA_G_VIEW_PARAMS_XSCALE 0x8527cu
#define VA_G_PERSPECTIVE_SCALE 0x85288u

/* The resolved DAS block's class bits the emitters key on. */
#define HWR_PS_MESH_FLAG 0x8000u
#define HWR_PS_DRAW_NOTHING_FLAG 0x20u

struct hwr_ps_tile {
    int used;
    /* The light's identity across frames: where it hangs and how far it
     * reaches, exactly the numbers the seeding derives deterministically. */
    float x;
    float y;
    float z;
    float range;
    /* The stamp the tile's contents were last rendered at; zero is none. */
    uint32_t stamp;
    /* Whether this tile's light was seen this frame (scratch). */
    int seen;
    /* This light's card slice, owned here and borrowed by the GL layer for
     * the frame of a rebuild. */
    struct hwr_gl_sun_vertex *cards;
    size_t card_count;
    size_t card_capacity;
};

static struct hwr_ps_tile g_tiles[HWR_PSM_MAX_LIGHTS];
static unsigned long g_tile_builds;
/* How many tiles the ledger is keeping, which is the count in force as it
 * stood when the ledger was last released. A published count that differs
 * re-takes the atlas underneath, so the ledger goes with it. */
static size_t g_budget;

/* HWR_PSHADOW_UNCACHED=1 defeats the tile cache -- every tile every frame --
 * which exists for one purpose: measuring what the cache is worth. -1 until
 * first read. */
static int g_uncached = -1;

static int hwr_pshadow_uncached(void)
{
    if (g_uncached < 0) {
        const char *value = getenv("HWR_PSHADOW_UNCACHED");

        g_uncached = value != NULL && value[0] != '0' && value[0] != 0;
    }
    return g_uncached;
}

unsigned long hwr_pshadow_tile_builds(void)
{
    return g_tile_builds;
}

/* The ledger, released whole: no tile is anybody's and no tile's contents are
 * believed. Every path that reaches it is one where what a tile HOLDS has
 * stopped meaning anything -- a new map, a torn-down renderer, or a count
 * change that re-takes the atlas storage underneath. */
static void hwr_ps_forget_tiles(void)
{
    size_t i;

    for (i = 0; i < HWR_PSM_MAX_LIGHTS; i++) {
        free(g_tiles[i].cards);
        memset(&g_tiles[i], 0, sizeof g_tiles[i]);
    }
    g_budget = 0;
}

/* The frame's prop arena, released with the ledger: its storage outlives a
 * frame but its contents never do, and a torn-down renderer keeps neither. */
static void hwr_ps_forget_props(void);

void hwr_pshadow_reset(void)
{
    hwr_ps_forget_tiles();
    hwr_ps_forget_props();
    hwr_gl_pshadow_set(NULL);
}

/* HOW MANY LIGHTS CAST, this frame: what the settings row asks for, held to
 * how many lights there may be at all (the light-count row -- a shadow count
 * past the light count names nothing), and to the tiles one atlas can stand
 * on this driver. A row at or past the light-count row is "all of them"
 * rather than a number, and that is what makes the row uncapped upward
 * without the storage being. */
static size_t hwr_pshadow_budget(void)
{
    const struct hwr_lights_settings *settings = hwr_lights_get_settings();
    size_t want;
    size_t cap;
    int fit;

    if (settings == NULL)
        return 0;
    want = (size_t)settings->shadow_lights;
    cap = (size_t)settings->max;
    if (cap > HWR_PSM_MAX_LIGHTS)
        cap = HWR_PSM_MAX_LIGHTS;
    fit = hwr_gl_pshadow_tile_ceiling(HWR_PSM_RESOLUTION);
    if (fit >= 0 && cap > (size_t)fit) {
        /* The driver stands fewer tiles than the dial may ask: said once,
         * because the help text promises it is said at all. */
        static int said;

        if (!said && want > (size_t)fit) {
            fprintf(stderr,
                    "[renderer_overhaul] point shadows: this driver's "
                    "texture size stands %d shadow maps at most; the row "
                    "is held there\n", fit);
            said = 1;
        }
        cap = (size_t)fit;
    }
    return want < cap ? want : cap;
}

/* Whether this light is holding a tile in the ledger, and which. What names a
 * light across frames here is where it hangs and how far it reaches -- the
 * same four numbers the seeding derives deterministically, and the same ones
 * the stamp is folded from. */
static struct hwr_ps_tile *hwr_ps_held(const struct hwr_gl_point_light *light,
                                       size_t budget)
{
    size_t i;

    for (i = 0; i < budget; i++) {
        if (g_tiles[i].used && g_tiles[i].x == light->world_x &&
            g_tiles[i].y == light->world_y &&
            g_tiles[i].z == light->world_z &&
            g_tiles[i].range == light->range)
            return &g_tiles[i];
    }
    return NULL;
}

static uint32_t hwr_ps_mix(uint32_t hash, const void *bytes, size_t size)
{
    const uint8_t *p = (const uint8_t *)bytes;
    size_t i;

    for (i = 0; i < size; i++) {
        hash ^= p[i];
        hash *= 16777619u;
    }
    return hash;
}

static int32_t hwr_ps_sincos(const struct roth_api_v1 *api, uint32_t angle)
{
    return (int16_t)api->game_ram->u16(VA_G_SINCOS_TABLE +
                                       (angle & 0x1ffu) * 2u);
}

/* THE CARD-WIDTH MARGIN of the reach cull: a thing whose ANCHOR stands just
 * outside the pool can still put its picture inside it, so the cull is given
 * the width of the widest card the emitter below builds. */
#define HWR_PS_CARD_MARGIN 512.0f

static struct hwr_gl_sun_vertex *hwr_ps_grow(struct hwr_ps_tile *tile,
                                             size_t extra)
{
    struct hwr_gl_sun_vertex *slot;

    if (tile->card_count + extra > tile->card_capacity) {
        size_t next = tile->card_capacity != 0 ? tile->card_capacity * 2u
                                               : 64u;
        struct hwr_gl_sun_vertex *grown;

        while (next < tile->card_count + extra)
            next *= 2u;
        grown = (struct hwr_gl_sun_vertex *)realloc(
            tile->cards, next * sizeof *grown);
        if (grown == NULL)
            return NULL;
        tile->cards = grown;
        tile->card_capacity = next;
    }
    slot = tile->cards + tile->card_count;
    tile->card_count += extra;
    return slot;
}

/* ======================= THE FRAME'S 3D PROPS ============================
 *
 * ONE PROP IS ONE PIECE OF GEOMETRY, whichever light is looking at it: unlike
 * a card, which is an upright quad turned to face the light it casts for,
 * a tree is a tree from every side. So the frame's props are emitted ONCE --
 * here, before any tile is filled -- and every tile within reach of one takes
 * a COPY of the bytes it already has. What that saves is not the copy: it is
 * the emission (four tiles turning the same trunk by the same facing and
 * working out the same face planes four times) and the HASH (the stamp folding
 * the same hundred kilobytes four times). On the game's own thickest outdoor
 * map at the shipping shadow count, that is most of what props cost.
 *
 * The currency story is unchanged in substance: what a tile's stamp folds for
 * a prop is a hash OF THE PROP'S OWN BYTES, taken once here rather than four
 * times there -- so a prop that moves, turns, or steps a texture frame still
 * moves the stamp of exactly the tiles that can see it.
 *
 * The arena lives across frames so its storage is taken once; what it holds is
 * this frame's, and nothing reads it after the frame that filled it. */
struct hwr_ps_prop {
    /* Which thing this is, by the map's own number: the identity refusal is
     * spent per tile, because a prop is left out of ITS OWN light's map and
     * stands in every other. */
    uint32_t map_index;
    /* Where it stands, and how far its own placed geometry carries from
     * there: a broad tree just outside a pool still hangs branches into it,
     * so the cull is given each prop's own reach rather than one number for
     * every prop of the map. */
    float x;
    float y;
    float reach;
    size_t first;
    size_t count;
    uint32_t digest;
};

static struct hwr_gl_sun_vertex *g_prop_vertices;
static size_t g_prop_vertex_count;
static size_t g_prop_vertex_capacity;
static struct hwr_ps_prop *g_props;
static size_t g_prop_count;
static size_t g_prop_capacity;

static struct hwr_gl_sun_vertex *hwr_ps_prop_reserve(void *context,
                                                     size_t count)
{
    struct hwr_gl_sun_vertex *slot;

    (void)context;
    if (g_prop_vertex_count + count > g_prop_vertex_capacity) {
        size_t next = g_prop_vertex_capacity != 0 ? g_prop_vertex_capacity * 2u
                                                  : 1024u;
        struct hwr_gl_sun_vertex *grown;

        while (next < g_prop_vertex_count + count)
            next *= 2u;
        grown = (struct hwr_gl_sun_vertex *)realloc(g_prop_vertices,
                                                    next * sizeof *grown);
        if (grown == NULL)
            return NULL;
        g_prop_vertices = grown;
        g_prop_vertex_capacity = next;
    }
    slot = g_prop_vertices + g_prop_vertex_count;
    g_prop_vertex_count += count;
    return slot;
}

static void hwr_ps_forget_props(void)
{
    free(g_prop_vertices);
    free(g_props);
    g_prop_vertices = NULL;
    g_props = NULL;
    g_prop_vertex_count = 0;
    g_prop_vertex_capacity = 0;
    g_prop_count = 0;
    g_prop_capacity = 0;
}

/* HOW FAR PAST ITS OWN ANCHOR THE WIDEST PROP OF THIS MAP REACHES, which is
 * what the build's own cull must allow a prop before it may refuse one: how
 * far a prop carries is not known until it has been placed, so the coarse
 * test that decides whether to place it at all is given the widest prop the
 * pack holds. Read off the pack's own local vertices -- every prop's geometry
 * is in that one array -- as a radius, which the facing rotation cannot
 * change, and held until the pack is rebuilt (a map change, a DAS change).
 * Zero where the map has no props at all, which is most maps. */
static float hwr_ps_pack_reach(const struct hwr_meshes *meshes)
{
    static const struct hwr_mesh_local_vertex *seen;
    static size_t seen_count;
    static float reach;
    double furthest = 0.0;
    size_t i;

    if (meshes == NULL || meshes->vertices == NULL ||
        meshes->vertex_count == 0)
        return 0.0f;
    if (meshes->vertices == seen && meshes->vertex_count == seen_count)
        return reach;
    for (i = 0; i < meshes->vertex_count; i++) {
        double x = (double)meshes->vertices[i].x;
        double y = (double)meshes->vertices[i].y;
        double square = x * x + y * y;

        if (square > furthest)
            furthest = square;
    }
    seen = meshes->vertices;
    seen_count = meshes->vertex_count;
    reach = (float)sqrt(furthest);
    return reach;
}

/* This frame's props: every thing NO SHADOWED LIGHT COULD SEE left where it
 * stands, and every other emitted through the shared law with its own bytes
 * hashed and its own reach measured. Nothing here is aimed at any light --
 * which is the point -- but a map's far corners are still not walked for
 * lamps that cannot reach them. */
static void hwr_ps_build_props(const struct hwr_scene *scene,
                               const struct hwr_objects *objects,
                               const struct roth_api_v1 *api,
                               const struct hwr_gl_camera *camera,
                               const struct hwr_gl_point_light *lights,
                               const size_t *wanted, size_t wanted_count)
{
    float bound;
    size_t i;

    g_prop_vertex_count = 0;
    g_prop_count = 0;
    /* A map whose pack holds no prop geometry at all -- which is most maps --
     * is not walked: there is nothing here for the walk to find. */
    if (objects == NULL || !objects->valid || api == NULL ||
        api->game_ram == NULL || !scene->object_meshes.built ||
        scene->object_meshes.vertex_count == 0)
        return;
    bound = hwr_ps_pack_reach(&scene->object_meshes);
    for (i = 0; i < objects->count; i++) {
        const struct hwr_object *object = &objects->items[i];
        struct hwr_sprite_lookup lookup;
        struct hwr_ps_prop *prop;
        size_t first = g_prop_vertex_count;
        double furthest = 0.0;
        uint32_t digest;
        int wanted_by = 0;
        size_t v;

        if (object->sector_index == (uint16_t)HWR_OBJECT_NO_SECTOR)
            continue;
        if ((object->flags & HWR_OBJECT_FLAG_REFLECTION_ONLY) != 0)
            continue;
        for (v = 0; v < wanted_count && !wanted_by; v++) {
            const struct hwr_gl_point_light *light = &lights[wanted[v]];
            float dx = (float)object->world_x - light->world_x;
            float dy = (float)object->world_y - light->world_y;

            wanted_by = sqrtf(dx * dx + dy * dy) <= light->range + bound;
        }
        if (!wanted_by)
            continue;
        if (hwr_sprites_resolve(api, scene->map_archive,
                                scene->shared_archive, object->graphic_id,
                                object->facing, camera->yaw, 0,
                                object->backref, &lookup) != 0)
            continue;
        if (lookup.archive == NULL ||
            lookup.entry_id >= lookup.archive->entry_count)
            continue;
        if (hwr_sun_emit_mesh_caster(scene, object, lookup.archive,
                                     lookup.entry_id, api,
                                     hwr_ps_prop_reserve, NULL) <= 0 ||
            g_prop_vertex_count == first)
            continue;
        if (g_prop_count + 1u > g_prop_capacity) {
            size_t next = g_prop_capacity != 0 ? g_prop_capacity * 2u : 32u;
            struct hwr_ps_prop *grown = (struct hwr_ps_prop *)realloc(
                g_props, next * sizeof *grown);

            if (grown == NULL) {
                g_prop_vertex_count = first;
                return;
            }
            g_props = grown;
            g_prop_capacity = next;
        }
        /* Its own reach, off the bytes just written: the furthest its placed
         * geometry stands from the thing's own anchor. */
        for (v = first; v < g_prop_vertex_count; v++) {
            double dx = (double)g_prop_vertices[v].x - (double)object->world_x;
            double dy = (double)g_prop_vertices[v].y - (double)object->world_y;
            double square = dx * dx + dy * dy;

            if (square > furthest)
                furthest = square;
        }
        digest = hwr_ps_mix(2166136261u, g_prop_vertices + first,
                            (g_prop_vertex_count - first) *
                                sizeof *g_prop_vertices);
        prop = &g_props[g_prop_count++];
        prop->map_index = object->map_index;
        prop->x = (float)object->world_x;
        prop->y = (float)object->world_y;
        prop->reach = (float)sqrt(furthest);
        prop->first = first;
        prop->count = g_prop_vertex_count - first;
        prop->digest = digest;
    }
}

/* The props of one tile: every one of the frame's within this light's reach,
 * copied in beside its cards -- EXCEPT the one thing this light is the light
 * OF, by the same identity rule its card is refused under. Answers the hash
 * of what was taken, which is what the tile's stamp folds for them. */
static uint32_t hwr_ps_take_props(struct hwr_ps_tile *tile,
                                  uint32_t source_object)
{
    uint32_t taken = 2166136261u;
    size_t i;

    for (i = 0; i < g_prop_count; i++) {
        const struct hwr_ps_prop *prop = &g_props[i];
        float dx = prop->x - tile->x;
        float dy = prop->y - tile->y;
        struct hwr_gl_sun_vertex *out;

        if (source_object != (uint32_t)HWR_OBJECT_NO_INDEX &&
            prop->map_index == source_object)
            continue;
        if (sqrtf(dx * dx + dy * dy) > tile->range + prop->reach)
            continue;
        out = hwr_ps_grow(tile, prop->count);
        if (out == NULL)
            break;
        memcpy(out, g_prop_vertices + prop->first,
               prop->count * sizeof *out);
        taken = hwr_ps_mix(taken, &prop->digest, sizeof prop->digest);
    }
    return taken;
}

/* The moving slice of one shadowed light: every object within its reach --
 * an ordinary thing as one upright quad faced to THIS light, a decoded 3D prop
 * as its own geometry through the shared emitter -- EXCEPT the one thing this
 * light is the light OF, which never stands in its own map (see the header).
 * The identity refusal is spent before the class is even asked, so it holds
 * for a prop exactly as it holds for a card: a lit statue does not shadow
 * itself out of its own pool.
 *
 * The sizing arithmetic below is the billboard emitter's own law, the same
 * lines the sun's card emitter carries (hwr_sun.c documents each term); what
 * differs is stated at each difference. The sprite FRAME is resolved with
 * the view's own yaw rather than a per-object light yaw -- the card then
 * carries the side the player sees, which for a cut-out silhouette is the
 * approximation this round accepts and states. */
static void hwr_ps_build_cards(struct hwr_ps_tile *tile,
                               const struct hwr_scene *scene,
                               const struct hwr_objects *objects,
                               const struct roth_api_v1 *api,
                               const struct hwr_gl_camera *camera,
                               uint32_t source_object)
{
    const struct hwr_billboards *bb = &scene->billboards;
    double xscale;
    double pscale;
    double hscale;
    double vscale;
    size_t i;

    tile->card_count = 0;
    if (objects == NULL || !objects->valid || api == NULL ||
        api->game_ram == NULL)
        return;
    xscale = (double)(int32_t)api->game_ram->u32(VA_G_VIEW_PARAMS_XSCALE);
    pscale = (double)(int32_t)api->game_ram->u32(VA_G_PERSPECTIVE_SCALE);
    if (camera->source_focal_x == 0 || camera->source_focal_y == 0 ||
        xscale <= 0.0 || pscale <= 0.0)
        return;
    hscale = xscale / ((double)camera->source_focal_x * 256.0);
    vscale = pscale / ((double)camera->source_focal_y * 256.0);
    for (i = 0; i < objects->count; i++) {
        const struct hwr_object *object = &objects->items[i];
        struct hwr_sprite_lookup lookup;
        struct hwr_sprite_frame frame;
        const struct hwr_bb_slot *slot = NULL;
        uint32_t offset;
        uint16_t tw;
        uint16_t th;
        uint16_t fa;
        int16_t origin_x = 0;
        double famod;
        unsigned cl;
        double half_w;
        double vext_d;
        double disp_d;
        float z_top;
        float z_bottom;
        float axis[2];
        float center_x;
        float center_y;
        float dx;
        float dy;
        float flat;
        int wall_art;
        struct hwr_gl_sun_vertex *out;
        static const float no_dir[3] = { 0.0f, 0.0f, 0.0f };

        if (object->sector_index == (uint16_t)HWR_OBJECT_NO_SECTOR)
            continue;
        if ((object->flags & HWR_OBJECT_FLAG_REFLECTION_ONLY) != 0)
            continue;
        /* THIS LIGHT'S OWN THING, WHICH NEVER CASTS INTO THIS LIGHT'S OWN
         * MAP. Under any circumstance, wall art included: the emit point
         * stands clear of whatever the thing is mounted on while the thing
         * itself stands a unit or two off it, so its card is BETWEEN its own
         * light and its mounting and would shadow the very surface it
         * decorates -- a ward on a door putting its own door out. It is left
         * out HERE, in this light's slice alone, so the same card still casts
         * in every other light's map: a sigil is a thing in the room, and a
         * lamp across the hall throws its silhouette like anything else's. */
        if (source_object != (uint32_t)HWR_OBJECT_NO_INDEX &&
            object->map_index == source_object)
            continue;
        /* Reach cull, with a margin for the card's own width: a caster wholly
         * outside the light's pool shadows nothing the pool lights. (A prop
         * is culled by its OWN reach where the props are taken, since a card's
         * width says nothing about a tree's.) */
        dx = (float)object->world_x - tile->x;
        dy = (float)object->world_y - tile->y;
        flat = sqrtf(dx * dx + dy * dy);
        if (flat > tile->range + HWR_PS_CARD_MARGIN)
            continue;
        if (hwr_sprites_resolve(api, scene->map_archive,
                                scene->shared_archive, object->graphic_id,
                                object->facing, camera->yaw, 0,
                                object->backref, &lookup) != 0)
            continue;
        if (lookup.archive == NULL ||
            lookup.entry_id >= lookup.archive->entry_count)
            continue;
        if (lookup.klass == HWR_DAS_CLASS_ANIMATED) {
            int live = hwr_sprites_live_frame_index(api, lookup.archive_id);

            if (live >= 0)
                lookup.frame_index = (uint16_t)live;
        }
        /* A DECODED 3D PROP IS NOT A CARD and is not built here: it casts its
         * own geometry, the same for every light, and the frame emitted it
         * once before any tile was filled. This tile takes its copy after the
         * cards are done. */
        if ((lookup.archive->entries[lookup.entry_id].payload_flags &
             0xff00u) == HWR_PS_MESH_FLAG)
            continue;
        if (hwr_sprites_frame(&lookup, &frame) != 0)
            continue;
        if ((frame.flags & (uint16_t)HWR_PS_MESH_FLAG) != 0)
            continue;
        if ((frame.flags & (uint16_t)HWR_PS_DRAW_NOTHING_FLAG) != 0)
            continue;
        /* From here the thing is a sprite and its card is cut from the atlas,
         * so a frame with no atlas packed has no cards -- exactly where the
         * sun's own walk asks the same question, and after the props, which
         * are cut from nothing. */
        if (!bb->built)
            continue;
        if (lookup.archive == bb->map_archive &&
            lookup.entry_id < bb->map_dir_count)
            slot = &bb->map_dir[lookup.entry_id];
        else if (lookup.archive == bb->shared_archive &&
                 lookup.entry_id < bb->shared_dir_count)
            slot = &bb->shared_dir[lookup.entry_id];
        if (slot == NULL || !slot->present)
            continue;
        if (slot->klass == HWR_DAS_CLASS_ANIMATED) {
            uint16_t index = lookup.frame_index < slot->frame_count
                           ? lookup.frame_index
                           : (uint16_t)(slot->frame_count != 0
                                            ? slot->frame_count - 1u : 0u);

            offset = slot->base_offset + (uint32_t)index * slot->frame_bytes;
            tw = slot->width;
            th = slot->height;
            fa = slot->flags;
        } else if (slot->klass == HWR_DAS_CLASS_GROUPED) {
            const struct hwr_bb_child *child = NULL;
            uint16_t c;

            for (c = 0; c < slot->child_count; c++) {
                const struct hwr_bb_child *candidate =
                    &bb->children[slot->child_first + c];

                if (candidate->payload_offset ==
                    (uint32_t)lookup.child_offset) {
                    child = candidate;
                    break;
                }
            }
            if (child == NULL) {
                offset = slot->base_offset;
                tw = slot->width;
                th = slot->height;
                fa = slot->flags;
            } else {
                offset = child->atlas_offset;
                tw = child->width;
                th = child->height;
                fa = child->flags;
            }
        } else {
            offset = slot->base_offset;
            tw = slot->width;
            th = slot->height;
            fa = slot->flags;
        }
        if (tw == 0 || th == 0)
            continue;
        if (lookup.klass == HWR_DAS_CLASS_PLAIN ||
            lookup.klass == HWR_DAS_CLASS_ANIMATED) {
            const struct hwr_das_entry *entry =
                &lookup.archive->entries[lookup.entry_id];

            origin_x = (int16_t)(uint16_t)(entry->size_prefix & 0xffffu);
        }
        if (lookup.flip)
            origin_x = (int16_t)(-origin_x);
        famod = 1.0;
        cl = 7;
        if ((fa & 0x80u) != 0) {
            if ((fa & 0x6000u) != 0) {
                unsigned t = (unsigned)((((uint32_t)fa << 3) |
                                         ((uint32_t)fa >> 13)) & 3u);

                famod = (double)(1u << t);
                cl = 7u + t;
            } else {
                famod = 0.5;
                cl = 6;
            }
        }
        half_w = (double)((uint32_t)((uint32_t)th * 2u) << cl) * hscale;
        vext_d = 2.0 * (double)tw * famod * vscale;
        {
            int16_t origin_y = 0;

            if ((lookup.klass == HWR_DAS_CLASS_PLAIN ||
                 lookup.klass == HWR_DAS_CLASS_ANIMATED) &&
                (object->flags9 & 0x80u) == 0)
                origin_y = (int16_t)(uint16_t)(
                    lookup.archive->entries[lookup.entry_id].size_prefix >>
                    16);
            disp_d = (2.0 * (double)(fa & 0xfu) + (double)origin_y) * vscale;
        }
        if ((fa & 0x10u) != 0) {
            z_top = (float)((double)object->world_z + disp_d);
            z_bottom = (float)((double)z_top - vext_d);
        } else {
            z_bottom = (float)((double)object->world_z - disp_d);
            z_top = (float)((double)z_bottom + vext_d);
        }
        wall_art = (object->flags9 & 0x80u) != 0;
        if (wall_art) {
            /* Pinned to its wall in the world: the authored axis, exactly
             * the sun's rule. It is drawn flat on its wall for every light
             * that sees it, and the only light it does not cast for at all
             * is its own, which was refused above. */
            uint32_t phi = 2u * (uint32_t)object->facing + 0x80u;

            axis[0] = (float)hwr_ps_sincos(api, phi) / 16384.0f;
            axis[1] = (float)hwr_ps_sincos(api, phi + 0x80u) / 16384.0f;
        } else {
            /* Faced to THIS light -- the billboard law with the light
             * standing in for the eye, per object. An object standing (in
             * plan) on the light has no honest facing, and an object whose
             * card would CONTAIN the light could only shadow the whole room:
             * both are skipped. That is a geometric refusal about ANY card,
             * a co-located second thing included, and not a stand-in for the
             * identity rule above -- which is why the two are kept apart. */
            if (!hwr_psm_card_axis(tile->x, tile->y,
                                   (float)object->world_x,
                                   (float)object->world_y, axis))
                continue;
            if (flat < (float)half_w + 4.0f)
                continue;
        }
        center_x = (float)object->world_x + (float)origin_x * axis[0];
        center_y = (float)object->world_y + (float)origin_x * axis[1];
        out = hwr_ps_grow(tile, 6u);
        if (out == NULL)
            return;
        hwr_sunm_card(center_x, center_y, axis, (float)half_w, z_top,
                      z_bottom, no_dir, 0.0f, offset, tw, th,
                      lookup.flip != 0, out);
    }
}

void hwr_pshadow_frame(struct hwr_scene *scene, const struct hwr_world *world,
                       const struct hwr_objects *objects,
                       const struct roth_api_v1 *api,
                       const struct hwr_gl_camera *camera,
                       struct hwr_gl_point_light *lights, size_t count)
{
    struct hwr_gl_pshadow out;
    const struct hwr_gl_sun_vertex *statics;
    size_t static_count;
    uint32_t static_stamp;
    size_t wanted[HWR_PSM_MAX_LIGHTS];
    size_t wanted_count = 0;
    size_t budget = hwr_pshadow_budget();
    size_t i;
    size_t k;

    /* A count that moved re-takes the atlas storage underneath, so nothing a
     * tile holds survives one and the ledger is released whole. It happens on
     * a settings change and nowhere else -- both numbers the count is worked
     * out from are rows -- which is what keeps the storage still while
     * somebody is walking. */
    if (budget != g_budget) {
        hwr_ps_forget_tiles();
        g_budget = budget;
    }
    for (i = 0; i < count; i++)
        lights[i].shadow_tile = -1;
    if (budget == 0 || scene == NULL || world == NULL || camera == NULL) {
        hwr_gl_pshadow_set(NULL);
        return;
    }
    /* WHO CASTS, in two passes over the frame's lights.
     *
     * The PINNED first, in the array's own order: a game config that says
     * "shadows": true about a light has said which lamp matters, and that
     * outranks a ranking. It spends one of the count's tiles rather than
     * standing beside them -- the count is what the picture can afford, and a
     * pin that came free would make the row a suggestion.
     *
     * Then the REST fill what is left, worth-most first. The array arrives in
     * the seeding's own ranking order, so first-come is nearest-and-strongest
     * already; what is added here is the hysteresis, which lifts a light that
     * is HOLDING a tile by the quarter margin and re-sorts. A light refused by
     * its config entry is in neither pass. */
    for (i = 0; i < count && wanted_count < budget; i++) {
        if (lights[i].shadow_request == HWR_GL_SHADOW_ALWAYS)
            wanted[wanted_count++] = i;
    }
    if (wanted_count < budget) {
        struct {
            size_t index;
            float score;
        } run[HWR_PSM_MAX_LIGHTS];
        size_t run_count = 0;
        size_t room = budget - wanted_count;

        for (i = 0; i < count && run_count < HWR_PSM_MAX_LIGHTS; i++) {
            float score;

            if (lights[i].shadow_request != HWR_GL_SHADOW_FOLLOW)
                continue;
            score = lights[i].score;
            if (hwr_ps_held(&lights[i], budget) != NULL)
                score *= HWR_PS_HYSTERESIS;
            run[run_count].index = i;
            run[run_count].score = score;
            run_count++;
        }
        /* Sorted by what each is worth, and STABLY, for the reason the
         * seeding's own sort is: the array's order breaks a tie and that
         * order does not move between frames. A light array with no scores at
         * all -- an offline rig handing lights over directly -- is all ties,
         * and the array's order is then the whole ranking. */
        for (i = 1; i < run_count; i++) {
            size_t j = i;

            while (j > 0 && run[j - 1].score < run[j].score) {
                size_t index = run[j - 1].index;
                float score = run[j - 1].score;

                run[j - 1].index = run[j].index;
                run[j - 1].score = run[j].score;
                run[j].index = index;
                run[j].score = score;
                j--;
            }
        }
        for (i = 0; i < run_count && i < room; i++)
            wanted[wanted_count++] = run[i].index;
    }
    if (wanted_count == 0) {
        hwr_gl_pshadow_set(NULL);
        return;
    }
    if (!hwr_sun_acquire_static(scene, world, &statics, &static_count,
                                &static_stamp)) {
        hwr_gl_pshadow_set(NULL);
        return;
    }
    /* THE FRAME'S PROPS, once, before any tile is filled: they are the same
     * geometry for every light, so what differs per tile is only which of them
     * are near enough (and the one a light is the light of). */
    hwr_ps_build_props(scene, objects, api, camera, lights, wanted,
                       wanted_count);
    /* The tile ledger: a light that stood last frame keeps its tile (its
     * contents are the whole cache), a tile whose light is gone is free, a
     * new light takes a free one and starts from nothing. */
    for (i = 0; i < budget; i++)
        g_tiles[i].seen = 0;
    memset(&out, 0, sizeof out);
    out.active = 1;
    out.resolution = HWR_PSM_RESOLUTION;
    out.tiles = (int)budget;
    out.static_casters = statics;
    out.static_count = static_count;
    out.static_generation = static_stamp;
    for (k = 0; k < wanted_count; k++) {
        struct hwr_gl_point_light *light = &lights[wanted[k]];
        struct hwr_ps_tile *tile = NULL;
        uint32_t stamp;
        uint32_t props;
        size_t cards;
        int rebuild;

        for (i = 0; i < budget; i++) {
            if (g_tiles[i].used && !g_tiles[i].seen &&
                g_tiles[i].x == light->world_x &&
                g_tiles[i].y == light->world_y &&
                g_tiles[i].z == light->world_z &&
                g_tiles[i].range == light->range) {
                tile = &g_tiles[i];
                break;
            }
        }
        if (tile == NULL) {
            /* A free tile first; failing that, evict one whose light was not
             * seen this frame. Never one already claimed this frame. */
            for (i = 0; i < budget; i++) {
                if (!g_tiles[i].used) {
                    tile = &g_tiles[i];
                    break;
                }
            }
            if (tile == NULL) {
                for (i = 0; i < budget; i++) {
                    if (!g_tiles[i].seen) {
                        tile = &g_tiles[i];
                        break;
                    }
                }
            }
            if (tile == NULL)
                continue;
            tile->used = 1;
            tile->x = light->world_x;
            tile->y = light->world_y;
            tile->z = light->world_z;
            tile->range = light->range;
            tile->stamp = 0;
        }
        tile->seen = 1;
        /* The card slice is rebuilt every frame -- a walk over the objects
         * within reach, a few quads -- because its BYTES are part of the
         * stamp: an animated torch stepping a frame moves them and refreshes
         * exactly this tile, and a still room hashes to what it was. */
        hwr_ps_build_cards(tile, scene, objects, api, camera,
                           light->source_object);
        cards = tile->card_count;
        /* Then this light's share of the frame's props, appended: their bytes
         * go to the execution layer with the cards, and what the stamp folds
         * for them is the hash of the hashes taken when they were emitted. */
        props = hwr_ps_take_props(tile, light->source_object);
        stamp = static_stamp;
        stamp = hwr_ps_mix(stamp, &tile->x, sizeof tile->x);
        stamp = hwr_ps_mix(stamp, &tile->y, sizeof tile->y);
        stamp = hwr_ps_mix(stamp, &tile->z, sizeof tile->z);
        stamp = hwr_ps_mix(stamp, &tile->range, sizeof tile->range);
        if (cards != 0)
            stamp = hwr_ps_mix(stamp, tile->cards,
                               cards * sizeof tile->cards[0]);
        stamp = hwr_ps_mix(stamp, &props, sizeof props);
        if (stamp == 0u)
            stamp = 1u;
        /* Stale by stamp, never rendered, or the measurement switch: the
         * readiness ask is what covers a frame the execution stood down
         * for and a lost context -- a stamp alone would call such a tile
         * current and never render it. */
        rebuild = stamp != tile->stamp ||
                  !hwr_gl_pshadow_tile_ready((int)(tile - g_tiles)) ||
                  hwr_pshadow_uncached();
        if (rebuild) {
            tile->stamp = stamp;
            g_tile_builds++;
        }
        light->shadow_tile = (int)(tile - g_tiles);
        out.lights[out.count].world[0] = light->world_x;
        out.lights[out.count].world[1] = light->world_y;
        out.lights[out.count].world[2] = light->world_z;
        out.lights[out.count].range = light->range;
        out.lights[out.count].tile = light->shadow_tile;
        out.lights[out.count].rebuild = rebuild;
        out.lights[out.count].cards = tile->cards;
        out.lights[out.count].card_count = tile->card_count;
        out.count++;
    }
    /* A tile whose light went away keeps its bytes but loses its claim, so
     * the same light coming back starts fresh rather than trusting a map
     * whose atlas rows may since have been painted by another. */
    for (i = 0; i < budget; i++) {
        if (g_tiles[i].used && !g_tiles[i].seen) {
            g_tiles[i].used = 0;
            g_tiles[i].stamp = 0;
        }
    }
    hwr_gl_pshadow_set(out.count != 0 ? &out : NULL);
}
