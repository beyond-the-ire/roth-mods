#include "hwr_visibility.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The section offset of the sector the camera currently stands in. The engine
 * keeps this resolved for us, so the flood always seeds from the correct sector
 * even when the player stands exactly on a sector boundary. */
#define VA_G_PLAYER_SECTOR 0x90c12u

/* Near plane. The wall renderer clamps forward depth to 16.0 in view space
 * before it takes a screen-X column; the flood clips portal edges at the same
 * value so the two agree on which portals are in front of the eye. */
#define HWR_PVS_NEAR 16.0

/* Total sector-expansion budget: a hard anti-runaway bound only. Window
 * narrowing terminates the flood far below this in every real map. */
#define HWR_PVS_BUDGET 0xc65

/* Recursion depth cap, matching the engine's own limit on how deep the portal
 * walk descends. */
#define HWR_PVS_MAX_DEPTH 0x3c

/* Half-open bound for the initial "full viewport and beyond" X window. No
 * viewport width is threaded in here, so the seed window is made wider than any
 * projected screen-X can reach; this is a deliberate safe superset (it can only
 * reach more sectors, never fewer) and still narrows correctly at every portal.
 * Projected screen-X stays far below this and the bounds are never summed, so
 * there is no overflow. */
#define HWR_PVS_WINDOW_INF (INT32_MAX / 2)

struct hwr_pvs_ctx {
    const struct hwr_world *world;
    const struct hwr_gl_camera *camera;
    double sine;
    double cosine;
    double focal_x;
    int center_x;
    uint8_t *visible;
    long budget;
};

/* HWR_PVS: on unless explicitly set to 0. */
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

/* Projects a world vertex into the same view space the wall renderer uses:
 * view_x is the lateral offset and depth is the forward distance. */
static void hwr_pvs_project(const struct hwr_pvs_ctx *ctx,
                            const struct hwr_vertex *vertex,
                            double *view_x, double *depth)
{
    int dx = (int16_t)((uint16_t)vertex->world_x +
                       (uint16_t)ctx->camera->view_offset_x);
    int dy = (int16_t)((uint16_t)vertex->world_y +
                       (uint16_t)ctx->camera->view_offset_y);
    *view_x = ctx->sine * (double)dx + ctx->cosine * (double)dy;
    *depth = ctx->sine * (double)dy - ctx->cosine * (double)dx;
}

/* Projects a view-space point (already at or in front of the near plane) to a
 * screen-X column. This matches the wall renderer's screen_x expression; it is
 * left unclamped so the flood sees a portal's true horizontal span. */
static int hwr_pvs_screen_x(const struct hwr_pvs_ctx *ctx,
                            double view_x, double depth)
{
    return (int)(view_x * ctx->focal_x / depth) + ctx->center_x;
}

/* Resolves a portal wall's screen-X interval [xa, xb], clipping the edge to the
 * near plane exactly as the wall renderer does. Returns 0 when the whole edge
 * is behind the near plane (the portal is skipped). */
static int hwr_pvs_portal_interval(const struct hwr_pvs_ctx *ctx,
                                   const struct hwr_wall *wall,
                                   int *out_xa, int *out_xb)
{
    const struct hwr_world *world = ctx->world;
    const struct hwr_vertex *va;
    const struct hwr_vertex *vb;
    double view_a, depth_a;
    double view_b, depth_b;

    if (wall->vertex_a_index >= world->vertex_count ||
        wall->vertex_b_index >= world->vertex_count)
        return 0;
    va = &world->vertices[wall->vertex_a_index];
    vb = &world->vertices[wall->vertex_b_index];
    hwr_pvs_project(ctx, va, &view_a, &depth_a);
    hwr_pvs_project(ctx, vb, &view_b, &depth_b);

    if (depth_a < HWR_PVS_NEAR && depth_b < HWR_PVS_NEAR)
        return 0;
    if (depth_a < HWR_PVS_NEAR) {
        double t = (HWR_PVS_NEAR - depth_a) / (depth_b - depth_a);
        view_a += t * (view_b - view_a);
        depth_a = HWR_PVS_NEAR;
    } else if (depth_b < HWR_PVS_NEAR) {
        double t = (HWR_PVS_NEAR - depth_b) / (depth_a - depth_b);
        view_b += t * (view_a - view_b);
        depth_b = HWR_PVS_NEAR;
    }
    *out_xa = hwr_pvs_screen_x(ctx, view_a, depth_a);
    *out_xb = hwr_pvs_screen_x(ctx, view_b, depth_b);
    return 1;
}

/* Recurses through the portals of `sector` that fall inside the current X
 * window [left, right] (left < right), narrowing a copy of the window at each
 * opening -- the mod-side distillation of the engine's portal render-tree
 * walk. */
static void hwr_pvs_flood(struct hwr_pvs_ctx *ctx, size_t sector,
                          size_t parent, int left, int right, int depth)
{
    const struct hwr_world *world = ctx->world;
    const struct hwr_sector *sec;
    size_t first;
    size_t count;
    size_t i;

    if (ctx->budget <= 0)
        return;
    ctx->budget--;
    ctx->visible[sector] = 1;
    if (depth > HWR_PVS_MAX_DEPTH)
        return;

    sec = &world->sectors[sector];
    first = sec->first_wall;
    count = sec->wall_count;
    for (i = 0; i < count; i++) {
        const struct hwr_wall *wall = &world->walls[first + i];
        size_t neighbor = wall->neighbor_sector_index;
        int xa;
        int xb;
        int new_left;
        int new_right;

        if (neighbor == SIZE_MAX || neighbor >= world->sector_count)
            continue;                            /* solid wall: no opening */
        if (!hwr_pvs_portal_interval(ctx, wall, &xa, &xb))
            continue;                            /* portal behind the near plane */
        if (xa >= xb)
            continue;                            /* back-facing portal */
        if (xa >= right)
            continue;                            /* entirely right of the window */
        if (xb <= left)
            continue;                            /* entirely left of the window */
        new_left = xa > left ? xa : left;        /* max(left, xa) */
        new_right = xb < right ? xb : right;     /* min(right, xb) */
        if (new_left >= new_right)
            continue;                            /* narrowed to nothing */
        if (neighbor == parent || neighbor == sector)
            continue;                            /* skip the parent and self */
        hwr_pvs_flood(ctx, neighbor, sector, new_left, new_right, depth + 1);
    }
}

int hwr_compute_pvs(const struct hwr_world *world,
                    const struct hwr_gl_camera *camera,
                    const struct roth_api_v1 *api,
                    uint8_t *visible)
{
    struct hwr_pvs_ctx ctx;
    size_t sector_count;
    uint16_t seed_offset;
    uint16_t seed_index;

    if (world == NULL || camera == NULL || api == NULL ||
        api->game_ram == NULL || visible == NULL)
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
    /* A degenerate camera (no focal length) collapses every portal to a single
     * column; fall back to all-visible rather than blanking the world. */
    if (camera->focal_x == 0 || world->sector_offset_to_index == NULL)
        return 0;

    seed_offset = api->game_ram->u16(VA_G_PLAYER_SECTOR);
    seed_index = world->sector_offset_to_index[seed_offset];
    if (seed_index == UINT16_MAX || seed_index >= sector_count)
        return 0;                                /* seed maps to no sector */

    memset(visible, 0, sector_count);
    ctx.world = world;
    ctx.camera = camera;
    ctx.sine = (double)camera->yaw_sin / 16384.0;
    ctx.cosine = (double)camera->yaw_cos / 16384.0;
    ctx.focal_x = (double)camera->focal_x;
    ctx.center_x = (int)camera->center_x;
    ctx.visible = visible;
    ctx.budget = HWR_PVS_BUDGET;

    hwr_pvs_flood(&ctx, seed_index, seed_index,
                  -HWR_PVS_WINDOW_INF, HWR_PVS_WINDOW_INF, 0);
    return 1;
}
