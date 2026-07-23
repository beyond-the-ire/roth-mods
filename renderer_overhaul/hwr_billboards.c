#include "hwr_billboards.h"

#include "hwr_sprites.h"

#include <stdlib.h>
#include <string.h>

/* View angle the direction resolver quantizes against (g_sprite_view_angle). */
#define VA_G_SPRITE_VIEW_ANGLE   0x909f8u
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
    free(bb->opaque);
    free(bb->translucent);
    free(bb->runs);
    memset(bb, 0, sizeof *bb);
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

/* Packs one entry's frame(s) into the store and records its atlas slot. */
static int hwr_bb_pack_entry(struct hwr_das_archive *archive, size_t id,
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
        /* Keep the first directional child as a stand-in; the mesh renderer
         * owns the full grouped/sub-image path. */
        uint16_t child_word;
        size_t child_offset;
        int flip = 0;
        struct hwr_das_texture_view view;
        if (entry->raw_payload == NULL || entry->raw_payload_size < 10u)
            return 0;
        child_word = (uint16_t)(entry->raw_payload[8] |
                                ((uint16_t)entry->raw_payload[9] << 8));
        if (hwr_das_group_child_offset(entry, child_word, &child_offset, &flip) != 0 ||
            hwr_das_group_child_view(entry, child_offset, &view) != 0 ||
            view.pixels == NULL || view.pixel_count == 0 ||
            *size > UINT32_MAX - view.pixel_count)
            return 0;
        slot->base_offset = (uint32_t)*size;
        if (hwr_bb_append(bytes, size, capacity, view.pixels, view.pixel_count) != 0)
            return -1;
        slot->width = view.width;
        slot->height = view.height;
        slot->flags = view.flags;
        slot->frame_count = 1;
        slot->klass = HWR_DAS_CLASS_GROUPED;
        slot->present = 1;
        return 0;
    }
    default:
        return 0;
    }
}

static int hwr_bb_pack_archive(struct hwr_das_archive *archive, size_t first_id,
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
        if (hwr_bb_pack_entry(archive, id, &dir[id], bytes, size, capacity) != 0) {
            free(dir);
            return -1;
        }
    }
    *dir_out = dir;
    *dir_count_out = count;
    return 0;
}

int hwr_billboards_pack(struct hwr_billboards *bb,
                        struct hwr_das_archive *map_archive,
                        struct hwr_das_archive *shared_archive,
                        const struct roth_api_v1 *api,
                        uint8_t **bytes, size_t *size, size_t *capacity)
{
    (void)api;
    if (bb == NULL)
        return -1;
    free(bb->map_dir);
    free(bb->shared_dir);
    bb->map_dir = NULL;
    bb->shared_dir = NULL;
    bb->map_dir_count = 0;
    bb->shared_dir_count = 0;
    bb->map_archive = map_archive;
    bb->shared_archive = shared_archive;
    bb->built = 0;
    if (!hwr_billboards_enabled())
        return 0;                                         /* kill switch: no atlas */
    /* Map object graphics occupy the archive tail from the id bias; the shared
     * (ADEMO) archive holds the game-wide sprite set from its start. */
    if (hwr_bb_pack_archive(map_archive, HWR_SPRITE_ID_BIAS,
                            &bb->map_dir, &bb->map_dir_count,
                            bytes, size, capacity) != 0)
        return hwr_bb_fail(bb, "out of memory packing map sprite atlas");
    if (hwr_bb_pack_archive(shared_archive, 0,
                            &bb->shared_dir, &bb->shared_dir_count,
                            bytes, size, capacity) != 0)
        return hwr_bb_fail(bb, "out of memory packing shared sprite atlas");
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

int hwr_billboards_emit(struct hwr_billboards *bb,
                        const struct hwr_objects *objects,
                        const struct hwr_world *world,
                        const struct hwr_gl_camera *camera,
                        const uint8_t *visible, size_t visible_count,
                        int pvs_valid,
                        const struct roth_api_v1 *api)
{
    uint16_t yaw;
    double focal_x, focal_y;
    struct hwr_bb_trans *pending = NULL;
    size_t pending_count = 0, pending_capacity = 0;
    size_t i;
    int trace = hwr_bb_trace_level();
    int bb_log = getenv("HWR_BB_LOG") != NULL;
    static uint32_t frame_no;
    int log_frame, throttle;

    if (bb == NULL)
        return 0;
    bb->opaque_count = 0;
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
    if (focal_x <= 0.0 || focal_y <= 0.0)
        return 0;                                         /* no projection staged yet */

    for (i = 0; i < objects->count; i++) {
        const struct hwr_object *o = &objects->items[i];
        struct hwr_sprite_lookup lookup;
        struct hwr_sprite_frame frame;
        const struct hwr_bb_slot *slot;
        int32_t view_x, depth, corr;
        uint32_t offset;
        uint16_t tw, th, fa;
        uint8_t sector_light = 0, sector_flags = 0, seed, composed, material;
        double half_w, famod, vext_d, rx, ry;
        int32_t disp, vext, z_top, z_bottom;
        int16_t xl, yl, xr, yr;
        int translucent, do_trace;

        bb->stat_enumerated++;
        do_trace = trace >= 2 && throttle && i < 260u;

        /* Per-visible-sector emission: an object whose owner sector the engine's
         * draw list did not draw is dropped, matching the engine's own gate.
         * Objects with no owning sector (particles, pickup overlay) are kept. */
        if (pvs_valid && o->sector_index != (uint16_t)HWR_OBJECT_NO_SECTOR &&
            o->sector_index < visible_count && !visible[o->sector_index]) {
            if (do_trace)
                fprintf(stderr, "[bb] o%zu id=%#06x sec=%u DROP pvs\n",
                        i, o->graphic_id, o->sector_index);
            continue;
        }

        hwr_bb_view_point(camera, o->world_x, o->world_y, &view_x, &depth);
        if (depth < HWR_BB_NEAR_DEPTH) {
            if (do_trace)
                fprintf(stderr, "[bb] o%zu id=%#06x depth=%d DROP near\n",
                        i, o->graphic_id, depth);
            continue;                                     /* near-depth cull */
        }
        corr = (int32_t)(((int64_t)view_x << 6) / depth); /* per-sprite screen-angle */

        if (hwr_sprites_resolve(api, bb->map_archive, bb->shared_archive,
                                o->graphic_id, o->facing, yaw, corr,
                                o->backref, &lookup) != 0) {
            if (do_trace) {
                uint16_t st = api->game_ram->u16(0x86d30u +
                    (uint32_t)(uint16_t)(o->graphic_id + 0x1000u) * 2u);
                fprintf(stderr, "[bb] o%zu id=%#06x bref=%u status=%#06x DROP resolve\n",
                        i, o->graphic_id, o->backref, st);
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
                fprintf(stderr, "[bb] o%zu id=%#06x nid=%#06x DROP frame\n",
                        i, o->graphic_id, lookup.archive_id);
            continue;
        }
        if ((frame.flags & HWR_BB_MESH_FLAG) != 0) {
            bb->stat_skipped_mesh++;                      /* 3D-object mesh: later */
            if (do_trace)
                fprintf(stderr, "[bb] o%zu id=%#06x nid=%#06x SKIP mesh\n",
                        i, o->graphic_id, lookup.archive_id);
            continue;
        }

        slot = hwr_bb_slot_of(bb, &lookup);
        if (slot == NULL || !slot->present) {
            if (do_trace)
                fprintf(stderr, "[bb] o%zu id=%#06x nid=%#06x DROP atlas-miss\n",
                        i, o->graphic_id, lookup.archive_id);
            continue;
        }
        if (slot->klass == HWR_DAS_CLASS_ANIMATED) {
            uint16_t index = lookup.frame_index < slot->frame_count
                           ? lookup.frame_index : 0;
            offset = slot->base_offset + (uint32_t)index * slot->frame_bytes;
        } else {
            offset = slot->base_offset;
        }
        tw = slot->width;
        th = slot->height;
        fa = slot->flags;
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
        famod = 1.0;
        if ((fa & 0x80u) != 0) {
            if ((fa & 0x6000u) != 0) {
                int t = (int)(((uint32_t)(fa << 3) | (fa >> 13)) & 3u);
                famod = (double)(1u << t);
            } else {
                famod = 0.5;
            }
        }
        half_w = (double)th * famod;                  /* full width = 2*frame.height */
        vext_d = 2.0 * (double)tw * famod;            /* full height = 2*frame.width */
        vext = (int32_t)(vext_d + 0.5);
        disp = (int32_t)(2.0 * (double)(fa & 0xfu) + 0.5);  /* low-nibble displacement */

        /* Vertical anchor from the modifier byte (renderer.c:7755-7770). World Z
         * increases UPWARD here (the floor is at a smaller z than the eye -- the
         * engine's floor-visible test is eye_z > floor_z), so the image TOP (the
         * texture's v=0 head row, z_top) is the LARGER z and the feet the
         * smaller. Bit 0x10 set = top (head) anchored at the object's Z; else
         * (default) bottom (feet) at the object's Z with the head extending up.
         * The low nibble raises the whole sprite. */
        if ((fa & 0x10u) != 0) {
            z_top = (int32_t)o->world_z + disp;
            z_bottom = z_top - vext;
        } else {
            z_bottom = (int32_t)o->world_z + disp;
            z_top = z_bottom + vext;
        }

        /* Camera-facing: the horizontal axis is the camera-right world vector
         * (yaw_sin, yaw_cos)/16384, centred on the object. */
        rx = half_w * (double)camera->yaw_sin / 16384.0;
        ry = half_w * (double)camera->yaw_cos / 16384.0;
        xl = hwr_bb_clamp_i16((int32_t)((double)o->world_x - rx));
        yl = hwr_bb_clamp_i16((int32_t)((double)o->world_y - ry));
        xr = hwr_bb_clamp_i16((int32_t)((double)o->world_x + rx));
        yr = hwr_bb_clamp_i16((int32_t)((double)o->world_y + ry));

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
                    "[bb] o%zu id=%#06x nid=%#06x %ux%u fa=%#06x fmod=%.2g half=%.0f vext=%d wxy=%d,%d..%d,%d z=%d..%d depth=%d mat=%#x EMIT%s\n",
                    i, o->graphic_id, lookup.archive_id, tw, th, fa, famod,
                    half_w, vext, xl, yl, xr, yr, z_top, z_bottom, depth, material,
                    translucent ? "(t)" : "");

        if (!translucent) {
            /* Raw variants are fully opaque: depth-test naturally in the opaque
             * pass. */
            if (hwr_bb_reserve_verts(&bb->opaque, bb->opaque_count,
                                     &bb->opaque_capacity, 6) != 0)
                break;
            hwr_bb_write_quad(bb->opaque + bb->opaque_count, xl, yl, xr, yr,
                              hwr_bb_clamp_i16(z_top), hwr_bb_clamp_i16(z_bottom),
                              offset, tw, th, (float)(2u * th), (float)(2u * tw),
                              material, composed, sector_flags);
            bb->opaque_count += 6;
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
                "[bb] frame %u: emit=%zu enum=%zu skip-mesh=%zu opaque-v=%zu trans-v=%zu runs=%zu | cam vox=%d voy=%d eyez=%d yaw=%u sin=%d cos=%d\n",
                (unsigned)(frame_no - 1u), bb->stat_emitted, bb->stat_enumerated,
                bb->stat_skipped_mesh, bb->opaque_count, bb->translucent_count,
                bb->run_count, camera->view_offset_x, camera->view_offset_y,
                camera->eye_z, yaw, camera->yaw_sin, camera->yaw_cos);
    return 0;
}
