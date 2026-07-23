#include "hwr_billboards.h"

#include "hwr_sprites.h"

#include <stdlib.h>
#include <string.h>

/* View angle the direction resolver quantizes against (g_sprite_view_angle). */
#define VA_G_SPRITE_VIEW_ANGLE   0x909f8u
/* The engine's sincos table (g_sincos_table, 512 int16 entries scaled +/-16384;
 * table[k] = 16384*sin(2*pi*k/512), cos(k) = table[k + 0x80]). The rotated /
 * fixed-orientation object path indexes it to orient a flat face by its facing. */
#define VA_G_SINCOS_TABLE        0x72080u
/* The obj1 default texture record (g_default_shadow_texture_record, VA 0x29e5c):
 * the 7x32 translucent ground-shadow oval. Block header: +0xa flags (0x0404),
 * +0xc width (7), +0xe height (0x20), texels at +0x10. In-window low obj1. */
#define VA_G_SHADOW_RECORD       0x29e5cu
/* The engine's type-1 billboard projects its horizontal extent with this focal
 * (g_view_params_block+0xc) and its vertical extent with g_perspective_scale,
 * while the mod's world projection uses focal_x/focal_y. The size is matched by
 * scaling the engine's pre-focal extent by the ratio of the two focals, which is
 * exact and independent of depth. */
#define VA_G_VIEW_PARAMS_XSCALE  0x8527cu
#define VA_G_PERSPECTIVE_SCALE   0x85288u

/* Near-depth cull, in the same view-depth units hwr_wall_view_point produces
 * (world distance * 256); 0x1000 == the engine's object near plane and the
 * mod's own wall near plane. */
#define HWR_BB_NEAR_DEPTH        0x1000

/* Resolved-block flags: bit 0x8000 is the 3D-object (mesh) class (SKIP here);
 * bit 0x400 marks a masked / blend-capable variant. */
#define HWR_BB_MESH_FLAG         0x8000u
#define HWR_BB_BLEND_FLAG        0x0400u

/* A pending translucent quad, held until the whole set can be depth-sorted back
 * to front (the engine's sprite-queue order) before it joins the blend runs. */
struct hwr_bb_trans {
    int32_t depth;
    int16_t xl, yl, xr, yr, z_top, z_bottom;
    uint32_t offset;
    uint16_t tw, th;
    float smax, tmax;
    uint8_t material, light, sector_flags;
};

int hwr_billboards_enabled(void)
{
    static int state = -1;
    if (state < 0) {
        const char *setting = getenv("HWR_BILLBOARDS");
        state = setting != NULL && setting[0] == '0' ? 0 : 1;
    }
    return state;
}

/* HWR_SHADOWS=0 suppresses the ground-shadow ovals (kept in the atlas, just not
 * emitted), leaving every other billboard untouched. */
static int hwr_bb_shadows_enabled(void)
{
    static int state = -1;
    if (state < 0) {
        const char *setting = getenv("HWR_SHADOWS");
        state = setting != NULL && setting[0] == '0' ? 0 : 1;
    }
    return state;
}

/* HWR_BB_TRACE=1 logs a per-frame emit summary (throttled); =2 also logs the
 * per-object resolve/emit decision chain for the first objects of a frame. */
static int hwr_bb_trace_level(void)
{
    static int level = -1;
    if (level < 0) {
        const char *setting = getenv("HWR_BB_TRACE");
        level = setting != NULL && setting[0] >= '0' && setting[0] <= '9'
              ? setting[0] - '0' : 0;
    }
    return level;
}

void hwr_billboards_init(struct hwr_billboards *bb)
{
    if (bb != NULL)
        memset(bb, 0, sizeof *bb);
}

void hwr_billboards_clear(struct hwr_billboards *bb)
{
    if (bb == NULL)
        return;
    free(bb->map_dir);
    free(bb->shared_dir);
    free(bb->children);
    free(bb->opaque);
    free(bb->flat);
    free(bb->translucent);
    free(bb->runs);
    memset(bb, 0, sizeof *bb);
}

/* Appends one packed grouped child to the shared child table, growing it
 * geometrically. Returns the child's index, or (size_t)-1 on OOM. */
static size_t hwr_bb_add_child(struct hwr_billboards *bb, uint32_t payload_offset,
                               uint32_t atlas_offset, uint16_t width,
                               uint16_t height, uint16_t flags)
{
    struct hwr_bb_child *c;
    if (bb->child_count == bb->child_capacity) {
        size_t next = bb->child_capacity != 0 ? bb->child_capacity * 2u : 64u;
        struct hwr_bb_child *grown = (struct hwr_bb_child *)realloc(
            bb->children, next * sizeof *grown);
        if (grown == NULL)
            return (size_t)-1;
        bb->children = grown;
        bb->child_capacity = next;
    }
    c = &bb->children[bb->child_count];
    c->payload_offset = payload_offset;
    c->atlas_offset = atlas_offset;
    c->width = width;
    c->height = height;
    c->flags = flags;
    return bb->child_count++;
}

static int hwr_bb_fail(struct hwr_billboards *bb, const char *message)
{
    snprintf(bb->error, sizeof bb->error, "%s", message);
    return -1;
}

/* Appends raw bytes to the shared texel store, growing it geometrically. */
static int hwr_bb_append(uint8_t **bytes, size_t *size, size_t *capacity,
                         const uint8_t *src, size_t count)
{
    if (count == 0)
        return 0;
    if (*size > SIZE_MAX - count)
        return -1;
    if (*size + count > *capacity) {
        size_t next = *capacity != 0 ? *capacity : 4096u;
        uint8_t *grown;
        while (next < *size + count) {
            if (next > SIZE_MAX / 2u) {
                next = *size + count;
                break;
            }
            next *= 2u;
        }
        grown = (uint8_t *)realloc(*bytes, next);
        if (grown == NULL)
            return -1;
        *bytes = grown;
        *capacity = next;
    }
    memcpy(*bytes + *size, src, count);
    *size += count;
    return 0;
}

/* Packs one grouped child (pixels -> store, entry -> shared child table),
 * deduplicated by payload offset within this slot's child range. Returns 0 on
 * success (or a benign skip), -1 on OOM. */
static int hwr_bb_pack_group_child(struct hwr_billboards *bb,
                                   struct hwr_das_entry *entry,
                                   uint16_t child_word, uint8_t **bytes,
                                   size_t *size, size_t *capacity,
                                   size_t child_first)
{
    size_t child_offset;
    int flip = 0;
    struct hwr_das_texture_view view;
    size_t k;
    if (hwr_das_group_child_offset(entry, child_word, &child_offset, &flip) != 0 ||
        hwr_das_group_child_view(entry, child_offset, &view) != 0 ||
        view.pixels == NULL || view.pixel_count == 0 ||
        *size > UINT32_MAX - view.pixel_count)
        return 0;                                          /* skip malformed child */
    for (k = child_first; k < bb->child_count; k++)
        if (bb->children[k].payload_offset == (uint32_t)child_offset)
            return 0;                                      /* already packed (mirror pair) */
    {
        uint32_t atlas_offset = (uint32_t)*size;
        if (hwr_bb_append(bytes, size, capacity, view.pixels, view.pixel_count) != 0)
            return -1;
        if (hwr_bb_add_child(bb, (uint32_t)child_offset, atlas_offset,
                             view.width, view.height, view.flags) == (size_t)-1)
            return -1;
    }
    return 0;
}

/* Packs one entry's frame(s) into the store and records its atlas slot. */
static int hwr_bb_pack_entry(struct hwr_billboards *bb,
                             struct hwr_das_archive *archive, size_t id,
                             struct hwr_bb_slot *slot,
                             uint8_t **bytes, size_t *size, size_t *capacity)
{
    struct hwr_das_entry *entry;
    if (hwr_das_decode(archive, id) != 0)
        return 0;
    entry = &archive->entries[id];
    switch (entry->resource_class) {
    case HWR_DAS_CLASS_PLAIN: {
        size_t pixels = (size_t)entry->width * entry->height;
        if (entry->raw_payload == NULL || entry->raw_payload_size < 6u ||
            pixels == 0 || entry->raw_payload_size - 6u < pixels ||
            *size > UINT32_MAX - pixels)
            return 0;
        slot->base_offset = (uint32_t)*size;
        if (hwr_bb_append(bytes, size, capacity, entry->raw_payload + 6u, pixels) != 0)
            return -1;
        slot->width = entry->width;
        slot->height = entry->height;
        slot->flags = entry->payload_flags;
        slot->frame_count = 1;
        slot->klass = HWR_DAS_CLASS_PLAIN;
        slot->present = 1;
        return 0;
    }
    case HWR_DAS_CLASS_ANIMATED: {
        uint16_t w0, h0;
        size_t frame_bytes;
        size_t i;
        if (entry->frames == NULL || entry->frame_count == 0 ||
            entry->frames[0].pixels == NULL)
            return 0;
        w0 = entry->frames[0].width;
        h0 = entry->frames[0].height;
        frame_bytes = (size_t)w0 * h0;
        if (frame_bytes == 0)
            return 0;
        for (i = 0; i < entry->frame_count; i++)
            if (entry->frames[i].pixels == NULL ||
                entry->frames[i].width != w0 || entry->frames[i].height != h0)
                return 0;                                 /* non-uniform: leave unpacked */
        slot->base_offset = (uint32_t)*size;
        for (i = 0; i < entry->frame_count; i++) {
            if (*size > UINT32_MAX - frame_bytes)
                return 0;
            if (hwr_bb_append(bytes, size, capacity, entry->frames[i].pixels,
                              frame_bytes) != 0)
                return -1;
        }
        slot->width = w0;
        slot->height = h0;
        slot->flags = entry->payload_flags;
        slot->frame_count = (uint16_t)entry->frame_count;
        slot->frame_bytes = (uint32_t)frame_bytes;
        slot->klass = HWR_DAS_CLASS_ANIMATED;
        slot->present = 1;
        return 0;
    }
    case HWR_DAS_CLASS_GROUPED: {
        /* Pack EVERY directional child so the emitter can draw the resolved
         * facing (a grouped statue is view-directional; keeping only the first
         * child rendered every direction as direction 0 -- the "backside for
         * front" defect). The selector at payload+6 (== engine block+0x10)
         * selects directional (0x8000) coarse (8) / fine (0x2000 -> 16) child
         * words at payload+8+ci; a non-directional grouped block keeps just the
         * first child (part/actor selection is resolved elsewhere). The first
         * child also stands in as base_offset for an unmatched resolve. */
        uint16_t selector;
        size_t child_first = bb->child_count;
        uint32_t ci, ci_max;
        if (entry->raw_payload == NULL || entry->raw_payload_size < 10u)
            return 0;
        selector = (uint16_t)(entry->raw_payload[6] |
                              ((uint16_t)entry->raw_payload[7] << 8));
        ci_max = (selector & 0x8000u)
               ? ((selector & 0x2000u) ? 30u : 14u)        /* fine 16-dir / coarse 8-dir */
               : 0u;                                        /* non-directional: first child only */
        for (ci = 0; ci <= ci_max; ci += 2u) {
            size_t off = 8u + (size_t)ci;
            uint16_t child_word;
            if (off + 2u > entry->raw_payload_size)
                continue;
            child_word = (uint16_t)(entry->raw_payload[off] |
                                    ((uint16_t)entry->raw_payload[off + 1u] << 8));
            if (hwr_bb_pack_group_child(bb, entry, child_word, bytes, size,
                                        capacity, child_first) != 0)
                return -1;
        }
        if (bb->child_count == child_first)
            return 0;                                       /* no valid child packed */
        slot->child_first = (uint32_t)child_first;
        slot->child_count = (uint16_t)(bb->child_count - child_first);
        slot->base_offset = bb->children[child_first].atlas_offset;
        slot->width = bb->children[child_first].width;
        slot->height = bb->children[child_first].height;
        slot->flags = bb->children[child_first].flags;
        slot->frame_count = 1;
        slot->klass = HWR_DAS_CLASS_GROUPED;
        slot->present = 1;
        return 0;
    }
    default:
        return 0;
    }
}

static int hwr_bb_pack_archive(struct hwr_billboards *bb,
                               struct hwr_das_archive *archive, size_t first_id,
                               struct hwr_bb_slot **dir_out, size_t *dir_count_out,
                               uint8_t **bytes, size_t *size, size_t *capacity)
{
    struct hwr_bb_slot *dir;
    size_t count = archive != NULL ? archive->entry_count : 0;
    size_t id;
    *dir_out = NULL;
    *dir_count_out = 0;
    if (count == 0)
        return 0;
    dir = (struct hwr_bb_slot *)calloc(count, sizeof *dir);
    if (dir == NULL)
        return -1;
    for (id = first_id; id < count; id++) {
        if (hwr_bb_pack_entry(bb, archive, id, &dir[id], bytes, size, capacity) != 0) {
            free(dir);
            return -1;
        }
    }
    *dir_out = dir;
    *dir_count_out = count;
    return 0;
}

/* Packs the obj1 ground-shadow oval (VA 0x29e5c) into the atlas as bb->shadow. */
static int hwr_bb_pack_shadow(struct hwr_billboards *bb,
                              const struct roth_api_v1 *api,
                              uint8_t **bytes, size_t *size, size_t *capacity)
{
    uint16_t flags, w, h;
    size_t pixels, i;
    uint8_t *texels;
    memset(&bb->shadow, 0, sizeof bb->shadow);
    if (api == NULL || api->game_ram == NULL)
        return 0;
    flags = api->game_ram->u16(VA_G_SHADOW_RECORD + 0xau);
    w = api->game_ram->u16(VA_G_SHADOW_RECORD + 0xcu);
    h = api->game_ram->u16(VA_G_SHADOW_RECORD + 0xeu);
    if (w == 0 || h == 0 || w > 256u || h > 256u)
        return 0;                                             /* not staged / implausible */
    pixels = (size_t)w * h;
    if (*size > UINT32_MAX - pixels)
        return 0;
    texels = (uint8_t *)malloc(pixels);
    if (texels == NULL)
        return -1;
    for (i = 0; i < pixels; i++)
        texels[i] = api->game_ram->u8(VA_G_SHADOW_RECORD + 0x10u + (uint32_t)i);
    bb->shadow.base_offset = (uint32_t)*size;
    if (hwr_bb_append(bytes, size, capacity, texels, pixels) != 0) {
        free(texels);
        return -1;
    }
    free(texels);
    bb->shadow.width = w;
    bb->shadow.height = h;
    bb->shadow.flags = flags;                                 /* 0x0404 -> the 0x400 translucent path */
    bb->shadow.frame_count = 1;
    bb->shadow.klass = HWR_DAS_CLASS_PLAIN;
    bb->shadow.present = 1;
    return 0;
}

int hwr_billboards_pack(struct hwr_billboards *bb,
                        struct hwr_das_archive *map_archive,
                        struct hwr_das_archive *shared_archive,
                        const struct roth_api_v1 *api,
                        uint8_t **bytes, size_t *size, size_t *capacity)
{
    if (bb == NULL)
        return -1;
    free(bb->map_dir);
    free(bb->shared_dir);
    free(bb->children);
    bb->map_dir = NULL;
    bb->shared_dir = NULL;
    bb->children = NULL;
    bb->child_count = 0;
    bb->child_capacity = 0;
    bb->map_dir_count = 0;
    bb->shared_dir_count = 0;
    bb->map_archive = map_archive;
    bb->shared_archive = shared_archive;
    memset(&bb->shadow, 0, sizeof bb->shadow);
    bb->built = 0;
    if (!hwr_billboards_enabled())
        return 0;                                         /* kill switch: no atlas */
    /* Map object graphics occupy the archive tail from the id bias; the shared
     * (ADEMO) archive holds the game-wide sprite set from its start. */
    if (hwr_bb_pack_archive(bb, map_archive, HWR_SPRITE_ID_BIAS,
                            &bb->map_dir, &bb->map_dir_count,
                            bytes, size, capacity) != 0)
        return hwr_bb_fail(bb, "out of memory packing map sprite atlas");
    if (hwr_bb_pack_archive(bb, shared_archive, 0,
                            &bb->shared_dir, &bb->shared_dir_count,
                            bytes, size, capacity) != 0)
        return hwr_bb_fail(bb, "out of memory packing shared sprite atlas");
    if (hwr_bb_pack_shadow(bb, api, bytes, size, capacity) != 0)
        return hwr_bb_fail(bb, "out of memory packing the ground shadow");
    bb->built = 1;
    return 0;
}

static const struct hwr_bb_slot *hwr_bb_slot_of(const struct hwr_billboards *bb,
                                                const struct hwr_sprite_lookup *lookup)
{
    if (lookup->archive == bb->map_archive && lookup->entry_id < bb->map_dir_count)
        return &bb->map_dir[lookup->entry_id];
    if (lookup->archive == bb->shared_archive && lookup->entry_id < bb->shared_dir_count)
        return &bb->shared_dir[lookup->entry_id];
    return NULL;
}

/* View-space transform mirroring the world vertex projection (and the engine's
 * rotate_point_2d): view_x and depth in world-distance*256 units. */
static void hwr_bb_view_point(const struct hwr_gl_camera *camera,
                              int16_t x, int16_t y,
                              int32_t *view_x, int32_t *depth)
{
    int32_t dx = (int16_t)((uint16_t)x + (uint16_t)camera->view_offset_x);
    int32_t dy = (int16_t)((uint16_t)y + (uint16_t)camera->view_offset_y);
    int32_t vx = dx * camera->yaw_sin + dy * camera->yaw_cos;
    int32_t dp = dy * camera->yaw_sin - dx * camera->yaw_cos;
    *view_x = vx >= 0 ? vx >> 6 : -(int32_t)(((uint32_t)(-vx) + 63u) >> 6);
    *depth  = dp >= 0 ? dp >> 6 : -(int32_t)(((uint32_t)(-dp) + 63u) >> 6);
}

/* g_sincos_table lookup: returns 16384*sin(2*pi*k/512) as a signed value. */
static int32_t hwr_bb_sincos(const struct roth_api_v1 *api, uint32_t k)
{
    return (int16_t)api->game_ram->u16(VA_G_SINCOS_TABLE + (k & 0x1ffu) * 2u);
}

static int hwr_bb_reserve_verts(struct hwr_gl_vertex **arr, size_t count,
                                size_t *capacity, size_t need)
{
    if (count + need > *capacity) {
        size_t next = *capacity != 0 ? *capacity : 256u;
        struct hwr_gl_vertex *grown;
        while (next < count + need)
            next *= 2u;
        grown = (struct hwr_gl_vertex *)realloc(*arr, next * sizeof **arr);
        if (grown == NULL)
            return -1;
        *arr = grown;
        *capacity = next;
    }
    return 0;
}

/* Appends one camera-facing quad (two triangles) to a vertex list. The texture
 * is mapped exactly as a wall's image-fit texel path expects it: the horizontal
 * screen axis drives the sprite's along-storage row (frame.height), the vertical
 * axis its column (frame.width), so a billboard samples in the same orientation
 * a wall does. All wall/flat span state stays zero, which selects the shader's
 * simple textured branch. */
static void hwr_bb_write_quad(struct hwr_gl_vertex *dst,
                              int16_t xl, int16_t yl, int16_t xr, int16_t yr,
                              int16_t z_top, int16_t z_bottom,
                              uint32_t offset, uint16_t tw, uint16_t th,
                              float smax, float tmax,
                              uint8_t material, uint8_t light, uint8_t sector_flags)
{
    struct hwr_gl_vertex corner;
    struct hwr_gl_vertex tl, tr, bl, br;
    memset(&corner, 0, sizeof corner);
    corner.texture_offset = offset;
    corner.texture_width = tw;
    corner.texture_height = th;
    corner.material_flags = material;
    corner.light = light;
    corner.sector_flags = sector_flags;
    tl = tr = bl = br = corner;
    tl.x = xl; tl.y = yl; tl.z = z_top;    tl.wall_s = 0.0f; tl.wall_top = 0.0f;
    tr.x = xr; tr.y = yr; tr.z = z_top;    tr.wall_s = smax; tr.wall_top = 0.0f;
    bl.x = xl; bl.y = yl; bl.z = z_bottom; bl.wall_s = 0.0f; bl.wall_top = tmax;
    br.x = xr; br.y = yr; br.z = z_bottom; br.wall_s = smax; br.wall_top = tmax;
    dst[0] = tl; dst[1] = tr; dst[2] = br;
    dst[3] = tl; dst[4] = br; dst[5] = bl;
}

static int16_t hwr_bb_clamp_i16(int32_t v)
{
    if (v < INT16_MIN)
        return INT16_MIN;
    if (v > INT16_MAX)
        return INT16_MAX;
    return (int16_t)v;
}

static int hwr_bb_trans_cmp(const void *a, const void *b)
{
    const struct hwr_bb_trans *pa = (const struct hwr_bb_trans *)a;
    const struct hwr_bb_trans *pb = (const struct hwr_bb_trans *)b;
    /* Back to front: larger depth drawn first, matching depth_sort_sprite_queue. */
    if (pa->depth < pb->depth)
        return 1;
    if (pa->depth > pb->depth)
        return -1;
    return 0;
}

/* Appends the ground-shadow oval for one object to the pending translucent set.
 * The engine emits it (renderer.c:8068-8098, the +9 bit0 "mirror double") as a
 * type-1 billboard at the object's XY on the sector floor, textured with the
 * 0xffff default record; here it is the same camera-facing translucent quad,
 * sized from the shadow block (7x32 -> a wide 32x7 oval via the column-major
 * map) and anchored at the floor z. Returns 0 (incl. a benign skip), -1 on OOM. */
static int hwr_bb_push_shadow(struct hwr_billboards *bb, const struct hwr_object *o,
                              const struct hwr_world *world,
                              const struct hwr_gl_camera *camera, int32_t depth,
                              double hscale, double vscale,
                              struct hwr_bb_trans **pending, size_t *pending_count,
                              size_t *pending_capacity)
{
    uint16_t sw = bb->shadow.width, sh = bb->shadow.height, sfa = bb->shadow.flags;
    int32_t floor_z;
    uint8_t sflags = 0;                                     /* shadow shade seed = 0 (trace) */
    double half_w = (double)((uint32_t)((uint32_t)sh * 2u) << 7) * hscale; /* sfa&0x80=0 -> cl=7 */
    double vext_d = 2.0 * (double)sw * vscale;
    double disp_d = 2.0 * (double)(sfa & 0xfu) * vscale;
    double srx = half_w * (double)camera->yaw_sin / 16384.0;
    double sry = half_w * (double)camera->yaw_cos / 16384.0;
    int32_t szb, szt;
    struct hwr_bb_trans *t;
    if (o->sector_index != (uint16_t)HWR_OBJECT_NO_SECTOR &&
        o->sector_index < world->sector_count) {
        floor_z = world->sectors[o->sector_index].floor_z;
        sflags = world->sectors[o->sector_index].flags;
    } else {
        floor_z = o->world_z;
    }
    /* The engine anchors the oval straddling the floor and paints it whole over
     * the floor (painter order). The mod's depth test would clip the below-floor
     * half against the opaque floor, so the oval sits ON the floor (bottom edge
     * at floor z) and extends up -- the whole oval clears the floor and shows.
     * A small disp keeps its base a hair below to hug the contact line. */
    (void)disp_d;
    szb = floor_z;
    szt = szb + (int32_t)(vext_d + 0.5);
    if (*pending_count == *pending_capacity) {
        size_t next = *pending_capacity != 0 ? *pending_capacity * 2u : 128u;
        struct hwr_bb_trans *grown = (struct hwr_bb_trans *)realloc(
            *pending, next * sizeof *grown);
        if (grown == NULL)
            return -1;
        *pending = grown;
        *pending_capacity = next;
    }
    t = &(*pending)[(*pending_count)++];
    t->depth = depth;
    t->xl = hwr_bb_clamp_i16((int32_t)((double)o->world_x - srx));
    t->yl = hwr_bb_clamp_i16((int32_t)((double)o->world_y - sry));
    t->xr = hwr_bb_clamp_i16((int32_t)((double)o->world_x + srx));
    t->yr = hwr_bb_clamp_i16((int32_t)((double)o->world_y + sry));
    t->z_top = hwr_bb_clamp_i16(szt);
    t->z_bottom = hwr_bb_clamp_i16(szb);
    t->offset = bb->shadow.base_offset;
    t->tw = sw;
    t->th = sh;
    t->smax = (float)(2u * sh);
    t->tmax = (float)(2u * sw);
    t->material = (uint8_t)(HWR_GL_MATERIAL_MASKED | HWR_GL_MATERIAL_TRANSLUCENT);
    t->light = 0;                                          /* engine draws the shadow at shade 0 */
    t->sector_flags = sflags;
    return 0;
}

int hwr_billboards_emit(struct hwr_billboards *bb,
                        const struct hwr_objects *objects,
                        const struct hwr_world *world,
                        const struct hwr_gl_camera *camera,
                        const uint8_t *visible, size_t visible_count,
                        int pvs_valid,
                        const struct roth_api_v1 *api)
{
    uint16_t yaw;
    double focal_x, focal_y, xscale, pscale, hscale, vscale;
    struct hwr_bb_trans *pending = NULL;
    size_t pending_count = 0, pending_capacity = 0;
    size_t i;
    int trace = hwr_bb_trace_level();
    int bb_log = getenv("HWR_BB_LOG") != NULL;
    int shadows = hwr_bb_shadows_enabled();
    static uint32_t frame_no;
    int log_frame, throttle;
    size_t flat_seen = 0;

    if (bb == NULL)
        return 0;
    bb->opaque_count = 0;
    bb->flat_count = 0;
    bb->translucent_count = 0;
    bb->run_count = 0;
    bb->stat_emitted = 0;
    bb->stat_enumerated = 0;
    bb->stat_skipped_mesh = 0;
    if (!bb->built || objects == NULL || !objects->valid || camera == NULL ||
        world == NULL || api == NULL || api->game_ram == NULL)
        return 0;
    /* HWR_BB_LOG logs every frame (so the captured frame's state is readable);
     * HWR_BB_TRACE throttles to one detailed frame roughly every two seconds. */
    throttle = (frame_no % 120u) == 0u;
    log_frame = bb_log || (trace >= 1 && throttle);
    frame_no++;

    yaw = api->game_ram->u16(VA_G_SPRITE_VIEW_ANGLE);
    focal_x = (double)camera->focal_x;
    focal_y = (double)camera->focal_y;
    /* The engine's type-1 billboard projects its horizontal extent with
     * view_params_block+0xc and its vertical with g_perspective_scale, dividing
     * by g_view_clip_plane -- a view depth carried in a x256 fixed point (near
     * plane 0x1000 = 16 world units). The mod's world shader projects with
     * camera focal_x/focal_y against a x1 world depth (s*dy - c*dx). Matching the
     * engine's projected pixels therefore scales the pre-projection extent by
     * (engine_focal / mod_focal) and DIVIDES by the 256 view-depth fixed point:
     *   half_w_world = extent_h * Xscale / (focal_x * 256)
     *   vext_world   = extent_v * Pscale / (focal_y * 256)
     * (finalize_draw_list_entry renderer.c:7738-7768; proj64 :7284). Reading both
     * focals keeps it exact regardless of the Xscale:focal ratios. */
    xscale = (double)(int32_t)api->game_ram->u32(VA_G_VIEW_PARAMS_XSCALE);
    pscale = (double)(int32_t)api->game_ram->u32(VA_G_PERSPECTIVE_SCALE);
    if (focal_x <= 0.0 || focal_y <= 0.0 || xscale <= 0.0 || pscale <= 0.0)
        return 0;                                         /* no projection staged yet */
    hscale = xscale / (focal_x * 256.0);
    vscale = pscale / (focal_y * 256.0);


    for (i = 0; i < objects->count; i++) {
        const struct hwr_object *o = &objects->items[i];
        struct hwr_sprite_lookup lookup;
        struct hwr_sprite_frame frame;
        const struct hwr_bb_slot *slot;
        int32_t view_x, depth, corr;
        uint32_t offset;
        uint16_t tw, th, fa;
        uint8_t sector_light = 0, sector_flags = 0, seed, composed, material;
        double half_w, famod, vext_d, rx, ry, cx, cy;
        int32_t disp, vext, z_top, z_bottom;
        int16_t xl, yl, xr, yr, origin_x, origin_y;
        int translucent, do_trace, cl;

        bb->stat_enumerated++;
        if (o->flags & HWR_OBJECT_FLAG_FLAT)
            flat_seen++;
        do_trace = trace >= 2 && throttle && i < 260u;

        /* Per-visible-sector emission: an object whose owner sector the engine's
         * draw list did not draw is dropped, matching the engine's own gate.
         * Objects with no owning sector (particles, pickup overlay) are kept. */
        if (pvs_valid && o->sector_index != (uint16_t)HWR_OBJECT_NO_SECTOR &&
            o->sector_index < visible_count && !visible[o->sector_index]) {
            if (do_trace)
                fprintf(stderr, "[bb] o%zu id=%#06x sec=%u%s DROP pvs\n",
                        i, o->graphic_id, o->sector_index,
                        (o->flags & HWR_OBJECT_FLAG_FLAT) ? " FLAT" : "");
            continue;
        }

        hwr_bb_view_point(camera, o->world_x, o->world_y, &view_x, &depth);
        if (depth < HWR_BB_NEAR_DEPTH) {
            if (do_trace)
                fprintf(stderr, "[bb] o%zu id=%#06x depth=%d%s DROP near\n",
                        i, o->graphic_id, depth,
                        (o->flags & HWR_OBJECT_FLAG_FLAT) ? " FLAT" : "");
            continue;                                     /* near-depth cull */
        }
        corr = (int32_t)(((int64_t)view_x << 6) / depth); /* per-sprite screen-angle */

        /* Ground shadow: the engine draws a translucent oval under any object
         * whose map record has +9 bit0 (the type-1 "mirror double" gate,
         * renderer.c:8068). Emitted independent of the object's own sprite so a
         * shadow shows even if the sprite drops/meshes. */
        if (shadows && bb->shadow.present && (o->flags9 & 1u) != 0) {
            if (hwr_bb_push_shadow(bb, o, world, camera, depth, hscale, vscale,
                                   &pending, &pending_count, &pending_capacity) != 0)
                break;
            bb->stat_emitted++;
        }

        if (hwr_sprites_resolve(api, bb->map_archive, bb->shared_archive,
                                o->graphic_id, o->facing, yaw, corr,
                                o->backref, &lookup) != 0) {
            if (do_trace) {
                uint16_t st = api->game_ram->u16(0x86d30u +
                    (uint32_t)(uint16_t)(o->graphic_id + 0x1000u) * 2u);
                fprintf(stderr, "[bb] o%zu id=%#06x bref=%u status=%#06x%s DROP resolve\n",
                        i, o->graphic_id, o->backref, st,
                        (o->flags & HWR_OBJECT_FLAG_FLAT) ? " FLAT" : "");
            }
            continue;
        }
        if (lookup.klass == HWR_DAS_CLASS_ANIMATED) {
            int live = hwr_sprites_live_frame_index(api, lookup.archive_id);
            if (live >= 0)
                lookup.frame_index = (uint16_t)live;      /* mirror the live block */
            /* else the free-run fallback holds frame 0 */
        }
        if (hwr_sprites_frame(&lookup, &frame) != 0) {
            if (do_trace)
                fprintf(stderr, "[bb] o%zu id=%#06x nid=%#06x%s DROP frame\n",
                        i, o->graphic_id, lookup.archive_id,
                        (o->flags & HWR_OBJECT_FLAG_FLAT) ? " FLAT" : "");
            continue;
        }
        if ((frame.flags & HWR_BB_MESH_FLAG) != 0) {
            bb->stat_skipped_mesh++;                      /* 3D-object mesh: later */
            if (do_trace)
                fprintf(stderr, "[bb] o%zu id=%#06x nid=%#06x%s SKIP mesh\n",
                        i, o->graphic_id, lookup.archive_id,
                        (o->flags & HWR_OBJECT_FLAG_FLAT) ? " FLAT" : "");
            continue;
        }
        if ((frame.flags & 0x20u) != 0)
            continue;                                     /* resolved block +0xa bit5 = DRAW NOTHING
                                                           * (renderer.c:6292 hookblk) */

        slot = hwr_bb_slot_of(bb, &lookup);
        if (slot == NULL || !slot->present) {
            if (do_trace)
                fprintf(stderr, "[bb] o%zu id=%#06x nid=%#06x%s DROP atlas-miss\n",
                        i, o->graphic_id, lookup.archive_id,
                        (o->flags & HWR_OBJECT_FLAG_FLAT) ? " FLAT" : "");
            continue;
        }
        if (slot->klass == HWR_DAS_CLASS_ANIMATED) {
            /* Clamp out-of-range to the LAST frame (death-pose hold), matching
             * das_assets.c:1503 -- not frame 0, which flashed the alive sprite. */
            uint16_t index = lookup.frame_index < slot->frame_count
                           ? lookup.frame_index
                           : (uint16_t)(slot->frame_count ? slot->frame_count - 1u : 0u);
            offset = slot->base_offset + (uint32_t)index * slot->frame_bytes;
            tw = slot->width;
            th = slot->height;
            fa = slot->flags;
        } else if (slot->klass == HWR_DAS_CLASS_GROUPED) {
            /* Draw the resolved directional child, not the first-child stand-in. */
            const struct hwr_bb_child *child = NULL;
            uint16_t c;
            for (c = 0; c < slot->child_count; c++) {
                const struct hwr_bb_child *cand =
                    &bb->children[slot->child_first + c];
                if (cand->payload_offset == (uint32_t)lookup.child_offset) {
                    child = cand;
                    break;
                }
            }
            if (child == NULL) {                          /* fallback: first child */
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

        /* Shade seed compose (renderer.c:6180-6182): the +8 seed, biased by the
         * owner sector's light when both are nonzero, else passed through. The
         * composed value stands in as the billboard's effective light (a neutral
         * 0x80 seed reduces to the sector light, exactly like a wall). */
        if (o->sector_index != (uint16_t)HWR_OBJECT_NO_SECTOR &&
            o->sector_index < world->sector_count) {
            sector_light = world->sectors[o->sector_index].light;
            sector_flags = world->sectors[o->sector_index].flags;
        }
        seed = o->seed;
        composed = (seed != 0 && sector_light != 0)
                 ? (uint8_t)((uint8_t)(seed - 0x80u) + sector_light)
                 : seed;

        /* World size at 2 world units per texel -- the same density the wall
         * driver maps (texel = world_distance >> 1). Horizontal spans
         * frame.height texels (block+0xe), vertical frame.width (block+0xc); the
         * sprite's column-major storage means the on-screen width is its texture
         * height and vice versa. The block-modifier scale nibble multiplies both
         * axes (renderer.c:7723-7738's cl / w980 adjust: bit 0x80 with 0x6000 =
         * x2^t, bit 0x80 alone = x0.5). This world-space sizing sidesteps the
         * engine's screen-space billboard focals (view_params+0xc / perspective_
         * scale), which project against a x256 view-depth the mod's world shader
         * does not share; deriving from wall texel density is depth-independent
         * and keeps the sprite consistent with the geometry around it. */
        /* Pre-projection extents exactly as the engine stages them: horizontal
         * half a_h = (frame.height*2) << cl (renderer.c:7720/7738); vertical
         * ecxv = frame.width*2, block-modifier scaled (w980, :7723-7736). The
         * modifier nibble sets cl {6,7,7+t} and the vertical scale {0.5,1,2^t}. */
        /* Signed DAS origin prefix (FAT flag 8; hwr_das.c:527). The engine loads
         * the 4-byte prefix into the resolved block and applies it before the
         * anchor branch: low word = a flip-aware HORIZONTAL origin added to the
         * view X the quad centres on (renderer.c:5667-5669), high word = a
         * VERTICAL origin folded into the displacement (renderer.c:7758). It is
         * per-resolved-leaf; grouped children carry their own child data, not the
         * entry prefix, and the grouped (statue) path already matches at origin 0,
         * so the prefix is applied only to plain/animated leaves. */
        origin_x = 0;
        origin_y = 0;
        if (lookup.klass == HWR_DAS_CLASS_PLAIN ||
            lookup.klass == HWR_DAS_CLASS_ANIMATED) {
            const struct hwr_das_entry *le =
                &lookup.archive->entries[lookup.entry_id];
            origin_x = (int16_t)(uint16_t)(le->size_prefix & 0xffffu);
            origin_y = (int16_t)(uint16_t)(le->size_prefix >> 16);
        }
        if (lookup.flip)                            /* x-flip mirrors origin_x (renderer.c:5668) */
            origin_x = (int16_t)(-origin_x);

        famod = 1.0;
        cl = 7;
        if ((fa & 0x80u) != 0) {
            if ((fa & 0x6000u) != 0) {
                int t = (int)(((uint32_t)(fa << 3) | (fa >> 13)) & 3u);
                famod = (double)(1u << t);
                cl = 7 + t;
            } else {
                famod = 0.5;
                cl = 6;
            }
        }
        half_w = (double)((uint32_t)((uint32_t)th * 2u) << cl) * hscale;
        vext_d = 2.0 * (double)tw * famod * vscale;
        vext = (int32_t)(vext_d + 0.5);
        /* Low-nibble vertical displacement edxv = 2*(dl&0xf), plus the prefix's
         * vertical origin -- but NOT on the rotated path, which omits it
         * (renderer.c:7756-7758 vs 6129-6134). */
        {
            double disp_units = 2.0 * (double)(fa & 0xfu)
                              + ((o->flags9 & 0x80u) ? 0.0 : (double)origin_y);
            double disp_d = disp_units * vscale;
            disp = (int32_t)(disp_d + (disp_d >= 0.0 ? 0.5 : -0.5));
        }

        /* Vertical anchor + displacement (finalize_draw_list_entry,
         * renderer.c:7754-7768). The engine's vertical anchor is
         * eaxv = eye_z - objZ in screen-DOWN units, and the modifier byte's low
         * nibble is a displacement edxv = 2*(dl&0xf) [+ the block's authored
         * vertical bias, renderer.c:7758, not yet mirrored]:
         *   default 0x8N (bit 0x10 clear): eaxv_final = eaxv + edxv  (7760)
         *     -> feet (bottom edge) world z = objZ - edxv, head = feet + height.
         *   0x9N (bit 0x10 set): eaxv_final = eaxv - edxv + height    (7759)
         *     -> head (top edge)  world z = objZ + edxv, feet = head - height.
         * World z increases UPWARD (floor-visible test is eye_z > floor_z), so
         * z_top (the texture's v=0 head row) is the LARGER z. The displacement
         * sign therefore DIFFERS between the two anchors -- 0x9N adds, 0x8N
         * SUBTRACTS -- which is why wall items (0x9N) sat correctly while floor
         * items (0x8N) sat too high under a single-sign +disp. */
        if ((fa & 0x10u) != 0) {
            z_top = (int32_t)o->world_z + disp;      /* head at objZ + edxv */
            z_bottom = z_top - vext;
        } else {
            z_bottom = (int32_t)o->world_z - disp;   /* feet at objZ - edxv */
            z_top = z_bottom + vext;
        }

        /* Horizontal axis. A camera-facing object spans the camera-right world
         * vector (yaw_sin, yaw_cos)/16384. A FIXED-ORIENTATION face (map record
         * +9 bit0x80 -> the engine's rotated projection, rwss_rotated_tail
         * renderer.c:6093-6148) instead lies flat in the world, its horizontal
         * axis set by the authored facing. The engine builds the rotated edges
         * in VIEW space at angle 2*(facing+0x40) - yaw; undoing the camera
         * rotation, the yaw and the +0x40 (90 deg) term cancel to a world axis at
         * angle 2*facing + 0x80 -- independent of the camera, so the quad stays
         * pinned to the wall as the view moves. The half-extent is the same as
         * the billboard's (the engine's ext*|sincos| reaches the same 256*height
         * magnitude), so only the direction differs. */
        if ((o->flags9 & 0x80u) != 0) {
            uint32_t phi = 2u * (uint32_t)o->facing + 0x80u;
            double ax = (double)hwr_bb_sincos(api, phi);          /* sin(phi) */
            double ay = (double)hwr_bb_sincos(api, phi + 0x80u);  /* cos(phi) */
            rx = half_w * ax / 16384.0;
            ry = half_w * ay / 16384.0;
        } else {
            rx = half_w * (double)camera->yaw_sin / 16384.0;
            ry = half_w * (double)camera->yaw_cos / 16384.0;
        }
        /* Horizontal origin: the engine adds (origin_x<<8) to the view X the
         * quad centres on (renderer.c:5669 -> 7740). A world shift of origin_x
         * world units along camera-right (yaw_sin,yaw_cos)/16384 projects to
         * exactly that (a unit camera-right shift moves view X by 256). Applies
         * to both the camera-facing and the rotated centre. */
        cx = (double)o->world_x + (double)origin_x * (double)camera->yaw_sin / 16384.0;
        cy = (double)o->world_y + (double)origin_x * (double)camera->yaw_cos / 16384.0;
        xl = hwr_bb_clamp_i16((int32_t)(cx - rx));
        yl = hwr_bb_clamp_i16((int32_t)(cy - ry));
        xr = hwr_bb_clamp_i16((int32_t)(cx + rx));
        yr = hwr_bb_clamp_i16((int32_t)(cy + ry));

        /* Billboards always mask: palette index 0 is the sprite transparency
         * key (the engine's object/entity sprites render with a transparent
         * background even though the DAS block flag 0x400 is not set on them --
         * empirically observed in every object of the probed scenes). Drawing
         * them raw painted the index-0 background as opaque black. Blend-capable
         * variants (flag 0x400, rare) additionally join the translucent pass so
         * their high-bit texels blend; the common masked sprite stays in the
         * opaque pass with a texel-0 discard so it depth-occludes correctly. */
        material = (uint8_t)HWR_GL_MATERIAL_MASKED;
        translucent = (fa & HWR_BB_BLEND_FLAG) != 0;
        if (translucent)
            material |= (uint8_t)HWR_GL_MATERIAL_TRANSLUCENT;
        if (lookup.flip)
            material |= (uint8_t)HWR_GL_MATERIAL_HFLIP;

        if (bb_log || (trace >= 2 && throttle))
            fprintf(stderr,
                    "[bb] o%zu id=%#06x nid=%#06x %ux%u fa=%#06x fmod=%.2g half=%.0f vext=%d wxy=%d,%d..%d,%d z=%d..%d depth=%d mat=%#x face=%u%s EMIT%s\n",
                    i, o->graphic_id, lookup.archive_id, tw, th, fa, famod,
                    half_w, vext, xl, yl, xr, yr, z_top, z_bottom, depth, material,
                    o->facing,
                    (o->flags & HWR_OBJECT_FLAG_FLAT) ? " FLAT" : "",
                    translucent ? "(t)" : "");

        if (!translucent) {
            /* Raw variants are fully opaque: depth-test naturally in the opaque
             * pass. Fixed-orientation (rotated, f9 bit0x80) quads go to a
             * separate list so the GL layer can bias them a hair toward the
             * viewer -- they are coplanar with their wall and would z-fight it. */
            int is_flat = (o->flags9 & 0x80u) != 0;
            struct hwr_gl_vertex **dst = is_flat ? &bb->flat : &bb->opaque;
            size_t *cnt = is_flat ? &bb->flat_count : &bb->opaque_count;
            size_t *cap = is_flat ? &bb->flat_capacity : &bb->opaque_capacity;
            if (hwr_bb_reserve_verts(dst, *cnt, cap, 6) != 0)
                break;
            hwr_bb_write_quad(*dst + *cnt, xl, yl, xr, yr,
                              hwr_bb_clamp_i16(z_top), hwr_bb_clamp_i16(z_bottom),
                              offset, tw, th, (float)(2u * th), (float)(2u * tw),
                              material, composed, sector_flags);
            *cnt += 6;
            bb->stat_emitted++;
        } else {
            struct hwr_bb_trans *t;
            if (pending_count == pending_capacity) {
                size_t next = pending_capacity != 0 ? pending_capacity * 2u : 128u;
                struct hwr_bb_trans *grown = (struct hwr_bb_trans *)realloc(
                    pending, next * sizeof *grown);
                if (grown == NULL)
                    break;
                pending = grown;
                pending_capacity = next;
            }
            t = &pending[pending_count++];
            t->depth = depth;
            t->xl = xl; t->yl = yl; t->xr = xr; t->yr = yr;
            t->z_top = hwr_bb_clamp_i16(z_top);
            t->z_bottom = hwr_bb_clamp_i16(z_bottom);
            t->offset = offset;
            t->tw = tw; t->th = th;
            t->smax = (float)(2u * th);
            t->tmax = (float)(2u * tw);
            t->material = material;
            t->light = composed;
            t->sector_flags = sector_flags;
            bb->stat_emitted++;
        }
    }

    /* Depth-sort the masked/blend-capable quads back to front and lay them out,
     * one run each so the translucent pass refreshes its destination between a
     * stacked pair. */
    if (pending_count != 0) {
        qsort(pending, pending_count, sizeof *pending, hwr_bb_trans_cmp);
        if (hwr_bb_reserve_verts(&bb->translucent, bb->translucent_count,
                                 &bb->translucent_capacity, pending_count * 6u) == 0) {
            if (pending_count > bb->run_capacity) {
                uint32_t *runs = (uint32_t *)realloc(bb->runs,
                                                     pending_count * sizeof *runs);
                if (runs != NULL) {
                    bb->runs = runs;
                    bb->run_capacity = pending_count;
                }
            }
            if (pending_count <= bb->run_capacity) {
                for (i = 0; i < pending_count; i++) {
                    const struct hwr_bb_trans *t = &pending[i];
                    hwr_bb_write_quad(bb->translucent + bb->translucent_count,
                                      t->xl, t->yl, t->xr, t->yr, t->z_top, t->z_bottom,
                                      t->offset, t->tw, t->th, t->smax, t->tmax,
                                      t->material, t->light, t->sector_flags);
                    bb->translucent_count += 6;
                    bb->runs[bb->run_count++] = 6u;
                }
            }
        }
    }

    free(pending);
    if (log_frame)
        fprintf(stderr,
                "[bb] frame %u: emit=%zu enum=%zu skip-mesh=%zu opaque-v=%zu trans-v=%zu runs=%zu | cam vox=%d voy=%d eyez=%d yaw=%u sin=%d cos=%d | Xsc=%.0f Psc=%.0f fx=%.0f fy=%.0f hsc=%.4f vsc=%.4f\n",
                (unsigned)(frame_no - 1u), bb->stat_emitted, bb->stat_enumerated,
                bb->stat_skipped_mesh, bb->opaque_count, bb->translucent_count,
                bb->run_count, camera->view_offset_x, camera->view_offset_y,
                camera->eye_z, yaw, camera->yaw_sin, camera->yaw_cos,
                xscale, pscale, focal_x, focal_y, hscale, vscale);
    if (log_frame && flat_seen != 0)
        fprintf(stderr, "[bb] frame %u: flat(type-3) enumerated=%zu\n",
                (unsigned)(frame_no - 1u), flat_seen);
    return 0;
}
