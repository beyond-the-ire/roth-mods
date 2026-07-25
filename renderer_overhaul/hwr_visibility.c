#include "hwr_visibility.h"

#include "hwr_objects.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The software renderer walks the world every frame into a scratch arena that
 * the engine allocates once and publishes as a stored pointer. The upper half
 * of that arena is the node window the renderer's face pass addresses through a
 * selector; the mod reads it through the same window base. */
#define VA_G_RENDER_ARENA_PTR 0x8498cu
#define HWR_RENDER_ARENA_WINDOW 0x8000u

/* Within the node window, the renderer's per-frame ordered draw list is a u16
 * entry count here, then that many u16 node offsets immediately after. Each
 * entry points at a node in the same window. */
#define HWR_DRAW_LIST_COUNT   0xfc00u
#define HWR_DRAW_LIST_ENTRIES 0xfc02u

/* The node window is this many bytes; every offset the list yields is validated
 * against it before being dereferenced. */
#define HWR_SECTION_EXTENT 0x10000u

/* A node holds its face chain at +0x06; each chain link carries the drawn
 * sector's record offset at +0x02 and the next link at +0x00. A link whose
 * record offset is this value was coalesced away by the renderer's interval
 * merge -- its sector still draws through the surviving link that absorbed it,
 * so the link is skipped, not treated as a sector. */
#define HWR_NODE_FACE_HEAD 0x06u
#define HWR_NODE_LINK_NEXT 0x00u
#define HWR_NODE_LINK_REC  0x02u
#define HWR_NODE_REMOVED   0xffffu

/* A face chain cannot be longer than the node arena; this caps the walk so a
 * malformed link can never spin. */
#define HWR_CHAIN_LIMIT 0x2000u

/* The per-frame visibility gate is on by default. HWR_PVS=0 is the kill-switch:
 * it submits every sector's surfaces each frame instead of reading the
 * renderer's own drawn-sector set. */
int hwr_pvs_enabled(void)
{
    static int state = -1;
    if (state < 0) {
        const char *setting = getenv("HWR_PVS");
        state = setting != NULL && setting[0] == '0' ? 0 : 1;
    }
    return state;
}

/* Debug: keep the gate active but mark every sector visible, so the culling
 * plumbing can be exercised without changing what is drawn. */
static int hwr_pvs_all(void)
{
    static int state = -1;
    if (state < 0) {
        const char *setting = getenv("HWR_PVS_ALL");
        state = setting != NULL && setting[0] == '1' ? 1 : 0;
    }
    return state;
}

static uint16_t hwr_pvs_u16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t hwr_pvs_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

/* Reads a u16 from the section at `offset`; clears *ok and returns 0 when the
 * read would run past the section, so a stray offset can never dereference out
 * of bounds. */
static uint16_t hwr_pvs_section_u16(const uint8_t *section, uint32_t offset,
                                    int *ok)
{
    if (offset > HWR_SECTION_EXTENT - 2u) {
        *ok = 0;
        return 0;
    }
    return hwr_pvs_u16(section + offset);
}

int hwr_compute_pvs(const struct hwr_world *world,
                    const struct hwr_gl_camera *camera,
                    const struct roth_api_v1 *api,
                    uint8_t *visible)
{
    const uint8_t *section;
    uintptr_t arena;
    size_t sector_count;
    size_t marked = 0;
    uint32_t count;
    uint32_t entries_end;
    uint32_t k;
    int ok = 1;

    (void)camera;   /* the renderer's own walk supplies the set now */

    if (world == NULL || api == NULL || api->game_ram == NULL || visible == NULL)
        return 0;
    sector_count = world->sector_count;
    if (sector_count == 0)
        return 0;
    if (!hwr_pvs_enabled())
        return 0;                                /* kill-switch: all visible */
    if (hwr_pvs_all()) {
        memset(visible, 1, sector_count);
        return 1;
    }
    if (world->sector_offset_to_index == NULL)
        return 0;                                /* no map staged: all visible */
    /* The arena lives in engine heap outside the bounded accessor window; its
     * address is published in a pointer global, and the plugin shares the
     * process space, so the node window is read directly from base+window --
     * exactly how the world sections are reached. */
    arena = (uintptr_t)api->game_ram->u32(VA_G_RENDER_ARENA_PTR);
    if (arena < 0x1000u)
        return 0;                                /* arena not allocated yet */
    section = (const uint8_t *)(arena + HWR_RENDER_ARENA_WINDOW);

    count = hwr_pvs_section_u16(section, HWR_DRAW_LIST_COUNT, &ok);
    entries_end = HWR_DRAW_LIST_ENTRIES + count * 2u;
    /* No list, or one that cannot fit the section, means the renderer drew
     * nothing through the ordered path this frame -- a menu, a transition, or a
     * frame captured before the walk ran. Fall back to all-visible so no
     * geometry is dropped. */
    if (!ok || count == 0 || entries_end > HWR_SECTION_EXTENT)
        return 0;

    memset(visible, 0, sector_count);
    for (k = 0; k < count; k++) {
        uint32_t node = hwr_pvs_section_u16(section, HWR_DRAW_LIST_ENTRIES + k * 2u, &ok);
        uint32_t chain = hwr_pvs_section_u16(section, node + HWR_NODE_FACE_HEAD, &ok);
        uint32_t guard = 0;
        if (!ok)
            return 0;                            /* truncated list: fail open */
        /* Each node hangs a chain of face records; every surviving record names
         * the drawn sector at +0x02, and a coalesced-away record is skipped. */
        while (chain != 0 && guard++ < HWR_CHAIN_LIMIT) {
            uint16_t recid = hwr_pvs_section_u16(section, chain + HWR_NODE_LINK_REC, &ok);
            if (!ok)
                return 0;
            if (recid != HWR_NODE_REMOVED) {
                uint16_t index = world->sector_offset_to_index[recid];
                if (index == UINT16_MAX || index >= sector_count)
                    return 0;                    /* unmappable offset: fail open */
                if (!visible[index]) {
                    visible[index] = 1;
                    marked++;
                }
            }
            chain = hwr_pvs_section_u16(section, chain + HWR_NODE_LINK_NEXT, &ok);
            if (!ok)
                return 0;
        }
        if (guard >= HWR_CHAIN_LIMIT)
            return 0;                            /* runaway chain: fail open */
    }

    /* A list that produced no drawn sector (every chain empty) is not a real
     * cull result; fall back to all-visible rather than blank the world. */
    if (marked == 0)
        return 0;
    return 1;
}

/* The sector portal-window clip is on by default; HWR_PORTAL_CLIP=0 disables it,
 * leaving billboards unclipped exactly as before. */
int hwr_portal_clip_enabled(void)
{
    static int state = -1;
    if (state < 0) {
        const char *setting = getenv("HWR_PORTAL_CLIP");
        state = setting != NULL && setting[0] == '0' ? 0 : 1;
    }
    return state;
}

/* Poison sentinel for a sector that accumulated too many distinct windows; it is
 * normalised back to 0 (no clip) once the walk finishes. */
#define HWR_SPAN_POISON 0xffu

/* After widening interval `i`, absorb any other interval it now bridges so the
 * set stays a minimal list of disjoint windows. Bounded by HWR_SECTOR_CLIP_MAX. */
static void hwr_span_recoalesce(struct hwr_sector_xspan *s, unsigned i)
{
    unsigned j = 0;
    while (j < s->count) {
        if (j != i &&
            (int)s->lo[i] <= (int)s->hi[j] + 1 &&
            (int)s->lo[j] <= (int)s->hi[i] + 1) {
            if (s->lo[j] < s->lo[i]) s->lo[i] = s->lo[j];
            if (s->hi[j] > s->hi[i]) s->hi[i] = s->hi[j];
            s->count--;
            s->lo[j] = s->lo[s->count];   /* compact: last fills the hole */
            s->hi[j] = s->hi[s->count];
            if (i == s->count)            /* i itself was the moved last one */
                i = j;
            j = 0;                        /* restart: the merge may bridge more */
            continue;
        }
        j++;
    }
}

/* Merges one screen-X window into a sector's set, coalescing overlapping or
 * touching windows (a 1px gap still merges). Overflowing the cap poisons the
 * sector so it reads as no-clip. */
static void hwr_span_add(struct hwr_sector_xspan *s, int lo, int hi)
{
    unsigned i;
    if (s->count == HWR_SPAN_POISON)
        return;
    if (lo > hi) {
        int t = lo; lo = hi; hi = t;
    }
    for (i = 0; i < s->count; i++) {
        if (lo <= (int)s->hi[i] + 1 && (int)s->lo[i] <= hi + 1) {
            if (lo < (int)s->lo[i]) s->lo[i] = (int16_t)lo;
            if (hi > (int)s->hi[i]) s->hi[i] = (int16_t)hi;
            hwr_span_recoalesce(s, i);
            return;
        }
    }
    if (s->count >= HWR_SECTOR_CLIP_MAX) {
        s->count = HWR_SPAN_POISON;       /* fail open for this sector */
        return;
    }
    s->lo[s->count] = (int16_t)lo;
    s->hi[s->count] = (int16_t)hi;
    s->count++;
}

/* Reflections need their own portal walk because the primary software pass
 * overwrites the engine's reflected render tree before the hardware callback.
 * Keep the walk in the engine's pre-X-flip screen coordinates; only the final
 * GL aperture is flipped. */
#define HWR_REFLECTION_NEAR      0x1000
#define HWR_REFLECTION_BUDGET    0x0c65
#define HWR_REFLECTION_MAX_DEPTH 0x003c

struct hwr_reflection_pvs_ctx {
    const struct hwr_world *world;
    const struct hwr_gl_camera *camera;
    struct hwr_sector_xspan *windows;
    uint8_t *visible;
    int budget;
};

static int32_t hwr_reflection_shift_right_6(int32_t value)
{
    if (value >= 0)
        return value >> 6;
    return -(int32_t)(((uint32_t)(-value) + 63u) >> 6);
}

static void hwr_reflection_view_point(const struct hwr_gl_camera *camera,
                                      const struct hwr_vertex *vertex,
                                      int32_t *view_x, int32_t *depth)
{
    int32_t dx = (int16_t)((uint16_t)vertex->world_x +
                           (uint16_t)camera->view_offset_x);
    int32_t dy = (int16_t)((uint16_t)vertex->world_y +
                           (uint16_t)camera->view_offset_y);
    *view_x = hwr_reflection_shift_right_6(
        dx * camera->yaw_sin + dy * camera->yaw_cos);
    *depth = hwr_reflection_shift_right_6(
        dy * camera->yaw_sin - dx * camera->yaw_cos);
}

static int hwr_reflection_project_x(const struct hwr_gl_camera *camera,
                                    int32_t view_x, int32_t depth)
{
    int64_t projected =
        (int64_t)view_x * camera->focal_x / depth + camera->center_x;
    if (projected < -0x3ffe)
        return -0x3ffe;
    if (projected > 0x3ffe)
        return 0x3ffe;
    return (int)projected;
}

/* Returns the directed portal interval after clipping its endpoints against the
 * same 16-world-unit near plane used by the wall and object paths. */
static int hwr_reflection_portal_interval(
    const struct hwr_reflection_pvs_ctx *ctx, const struct hwr_wall *wall,
    int *out_a, int *out_b)
{
    const struct hwr_world *world = ctx->world;
    int32_t view_a, depth_a, view_b, depth_b;
    int64_t numerator;
    int32_t denominator;

    if (wall->vertex_a_index >= world->vertex_count ||
        wall->vertex_b_index >= world->vertex_count)
        return 0;
    hwr_reflection_view_point(ctx->camera,
                              &world->vertices[wall->vertex_a_index],
                              &view_a, &depth_a);
    hwr_reflection_view_point(ctx->camera,
                              &world->vertices[wall->vertex_b_index],
                              &view_b, &depth_b);
    if (depth_a < HWR_REFLECTION_NEAR &&
        depth_b < HWR_REFLECTION_NEAR)
        return 0;
    if (depth_a < HWR_REFLECTION_NEAR) {
        denominator = depth_b - depth_a;
        if (denominator <= 0)
            return 0;
        numerator = (int64_t)(view_b - view_a) *
                    (HWR_REFLECTION_NEAR - depth_a);
        view_a += (int32_t)(numerator / denominator);
        depth_a = HWR_REFLECTION_NEAR;
    } else if (depth_b < HWR_REFLECTION_NEAR) {
        denominator = depth_a - depth_b;
        if (denominator <= 0)
            return 0;
        numerator = (int64_t)(view_a - view_b) *
                    (HWR_REFLECTION_NEAR - depth_b);
        view_b += (int32_t)(numerator / denominator);
        depth_b = HWR_REFLECTION_NEAR;
    }
    *out_a = hwr_reflection_project_x(ctx->camera, view_a, depth_a);
    *out_b = hwr_reflection_project_x(ctx->camera, view_b, depth_b);
    return 1;
}

/* The stock coalescer merges overlapping reflected visits but retains windows
 * that merely touch. If the bounded local store fills, retain its exact prefix
 * and drop later windows (fail closed) instead of poisoning the sector into an
 * unrestricted draw. */
static int hwr_reflection_span_add(struct hwr_sector_xspan *span,
                                   int lo, int hi)
{
    unsigned i;
    if (lo > hi) {
        int swap = lo;
        lo = hi;
        hi = swap;
    }
    for (i = 0; i < span->count; i++) {
        if (lo <= span->hi[i] && span->lo[i] <= hi) {
            unsigned j = 0;
            if (lo < span->lo[i])
                span->lo[i] = (int16_t)lo;
            if (hi > span->hi[i])
                span->hi[i] = (int16_t)hi;
            while (j < span->count) {
                if (j != i && span->lo[i] <= span->hi[j] &&
                    span->lo[j] <= span->hi[i]) {
                    if (span->lo[j] < span->lo[i])
                        span->lo[i] = span->lo[j];
                    if (span->hi[j] > span->hi[i])
                        span->hi[i] = span->hi[j];
                    span->count--;
                    span->lo[j] = span->lo[span->count];
                    span->hi[j] = span->hi[span->count];
                    if (i == span->count)
                        i = j;
                    j = 0;
                    continue;
                }
                j++;
            }
            return 1;
        }
    }
    if (span->count >= HWR_SECTOR_CLIP_MAX)
        return 0;
    span->lo[span->count] = (int16_t)lo;
    span->hi[span->count] = (int16_t)hi;
    span->count++;
    return 1;
}

static void hwr_reflection_pvs_flood(struct hwr_reflection_pvs_ctx *ctx,
                                     size_t sector, size_t parent,
                                     int left, int right, int depth)
{
    const struct hwr_world *world = ctx->world;
    const struct hwr_sector *current;
    struct hwr_sector_xspan *window;
    size_t i;

    if (sector >= world->sector_count || left > right || ctx->budget <= 0)
        return;
    window = &ctx->windows[sector];
    if (!hwr_reflection_span_add(window, left, right))
        return;                                      /* bounded fail-closed */
    ctx->visible[sector] = 1;
    ctx->budget--;
    if (depth >= HWR_REFLECTION_MAX_DEPTH)
        return;

    current = &world->sectors[sector];
    if (current->first_wall > world->wall_count ||
        current->wall_count > world->wall_count - current->first_wall)
        return;
    for (i = 0; i < current->wall_count; i++) {
        const struct hwr_wall *wall =
            &world->walls[current->first_wall + i];
        size_t neighbor_index = wall->neighbor_sector_index;
        const struct hwr_sector *neighbor;
        int portal_left, portal_right;
        int next_left, next_right;

        if (neighbor_index == SIZE_MAX ||
            neighbor_index >= world->sector_count ||
            neighbor_index == sector || neighbor_index == parent)
            continue;
        neighbor = &world->sectors[neighbor_index];
        /* A portal only connects vertically overlapping, non-empty sectors. */
        if (neighbor->ceiling_z <= neighbor->floor_z ||
            neighbor->ceiling_z <= current->floor_z ||
            neighbor->floor_z >= current->ceiling_z)
            continue;
        if (!hwr_reflection_portal_interval(ctx, wall,
                                             &portal_left, &portal_right))
            continue;
        if (portal_left >= portal_right)
            continue;                                  /* back-facing */
        if (portal_left >= right || portal_right <= left)
            continue;
        next_left = portal_left > left ? portal_left : left;
        next_right = portal_right < right ? portal_right : right;
        if (next_left >= next_right)
            continue;
        hwr_reflection_pvs_flood(ctx, neighbor_index, sector,
                                 next_left, next_right, depth + 1);
    }
}

int hwr_compute_reflection_pvs(const struct hwr_world *world,
                               const struct hwr_gl_camera *camera,
                               size_t root_sector,
                               int clip_left, int clip_right,
                               uint8_t *visible,
                               struct hwr_sector_xspan *spans,
                               size_t span_count)
{
    struct hwr_reflection_pvs_ctx ctx;
    size_t sector_count;

    if (world == NULL || camera == NULL || visible == NULL || spans == NULL)
        return 0;
    sector_count = world->sector_count;
    if (sector_count == 0 || root_sector >= sector_count ||
        span_count < sector_count || camera->focal_x == 0 ||
        clip_left > clip_right)
        return 0;
    memset(visible, 0, sector_count);
    memset(spans, 0, sector_count * sizeof *spans);
    ctx.world = world;
    ctx.camera = camera;
    ctx.windows = spans;
    ctx.visible = visible;
    ctx.budget = HWR_REFLECTION_BUDGET;
    hwr_reflection_pvs_flood(&ctx, root_sector, root_sector,
                             clip_left, clip_right, 0);
    return visible[root_sector] != 0;
}

int hwr_compute_object_portal_windows(
    const struct hwr_world *world, const struct hwr_objects *objects,
    const struct roth_api_v1 *api, struct hwr_sector_xspan *spans,
    size_t span_count)
{
    const uint8_t *node_section;
    uintptr_t arena, arena_end, worklist_limit;
    size_t captured = 0;
    size_t s, oi;

    if (world == NULL || objects == NULL || api == NULL ||
        api->game_ram == NULL || spans == NULL)
        return 0;
    if (world->sector_count == 0 || objects->count == 0 ||
        span_count < objects->count || world->sector_section == NULL)
        return 0;
    if (!hwr_portal_clip_enabled())
        return 0;

    for (oi = 0; oi < objects->count; oi++)
        spans[oi].count = 0;

    arena = (uintptr_t)api->game_ram->u32(VA_G_RENDER_ARENA_PTR);
    if (arena < 0x1000u ||
        arena > UINTPTR_MAX - HWR_RENDER_ARENA_WINDOW)
        return 0;
    arena_end = arena + HWR_RENDER_ARENA_WINDOW;
    worklist_limit =
        (uintptr_t)api->game_ram->u32(VA_G_RENDER_ARENA_PTR + 4u);
    if (worklist_limit <= arena || worklist_limit > arena_end)
        return 0;
    node_section = (const uint8_t *)arena_end;

    /* Each live sector record points into the high node section. Its worklist
     * head is relative to the low arena; subsequent links are absolute host
     * pointers. Walking every sector list once visits the exact nodes the
     * software type-1 rasterizer consumes. */
    for (s = 0; s < world->sector_count; s++) {
        const struct hwr_sector *sector = &world->sectors[s];
        const uint8_t *record =
            world->sector_section + sector->section_offset;
        uint32_t wall_base = hwr_pvs_u16(record + 4u);
        uint32_t sector_node;
        uint32_t head_rel;
        uintptr_t node;
        uint32_t guard = 0;
        int ok = 1;

        if (wall_base < 8u)
            continue;
        sector_node = wall_base - 8u;
        head_rel = hwr_pvs_section_u16(
            node_section, sector_node + 4u, &ok);
        if (!ok)
            goto fail_open;
        if (head_rel == 0)
            continue;
        node = arena + head_rel;

        while (node != 0 && guard++ < HWR_CHAIN_LIMIT) {
            uint32_t entry;
            uint32_t appearance;
            uint32_t packed;
            uintptr_t next;

            if (node < arena || node >= worklist_limit ||
                node > arena_end - 12u)
                goto fail_open;
            entry = hwr_pvs_u32((const uint8_t *)node + 4u);
            next = (uintptr_t)hwr_pvs_u32((const uint8_t *)node);
            if ((uintptr_t)entry < arena ||
                (uintptr_t)entry >= worklist_limit ||
                (uintptr_t)entry > arena_end - 0x26u)
                goto fail_open;
            if (hwr_pvs_u16((const uint8_t *)(uintptr_t)entry + 0x12u) == 1u) {
                appearance =
                    hwr_pvs_u32((const uint8_t *)(uintptr_t)entry + 8u);
                for (oi = 0; oi < objects->count; oi++) {
                    if (objects->items[oi].appearance_ptr == appearance)
                        break;
                }
                if (oi < objects->count) {
                    int lo, hi;
                    packed = hwr_pvs_u32((const uint8_t *)node + 8u);
                    /* The packed dword is {right in low16, left in high16}. */
                    lo = (int)(int16_t)(uint16_t)(packed >> 16);
                    hi = (int)(int16_t)(uint16_t)packed;
                    if (lo < hi) {
                        hwr_span_add(&spans[oi], lo, hi);
                        captured++;
                    }
                }
            }
            node = next;
        }
        if (guard >= HWR_CHAIN_LIMIT)
            goto fail_open;
    }

    if (captured == 0)
        return 0;

    for (oi = 0; oi < objects->count; oi++)
        if (spans[oi].count == HWR_SPAN_POISON)
            spans[oi].count = 0;             /* overflow -> no clip */

    return 1;

fail_open:
    for (oi = 0; oi < objects->count; oi++)
        spans[oi].count = 0;
    return 0;
}
