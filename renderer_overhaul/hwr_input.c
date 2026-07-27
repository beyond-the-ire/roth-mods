#include "hwr_input.h"

#include "hwr_gl.h"
#include "hwr_projection.h"

#include <limits.h>
#include <string.h>

#define VA_G_MOUSE_X                    0x707b3u
#define VA_G_MOUSE_Y                    0x707b7u
#define VA_G_PIXEL_EXTENT_SCALE         0x707bbu
#define VA_G_SAVEUNDER_CURSOR           0x76878u
#define VA_G_CURSOR_HALF_X              0x7e8b8u
#define VA_G_CURSOR_HALF_Y              0x7e8bcu
#define VA_G_CURSOR_ACCUM_X             0x7e8c0u
#define VA_G_CURSOR_ACCUM_Y             0x7e8c4u
#define VA_G_MOUSE_RELATIVE_MODE        0x7e8d0u
#define VA_G_VIEW_PARAMS                0x85270u
#define VA_G_PERSPECTIVE_SCALE          0x85288u
#define VA_G_SCREEN_WIDTH               0x85498u
#define VA_G_SCREEN_HEIGHT              0x854a0u
#define VA_G_RENDER_WIDTH               0x90bf2u
#define VA_G_RENDER_HEIGHT              0x90bf6u
#define VA_G_INVERSE_FOCAL_X            0x90994u
#define VA_G_INVERSE_FOCAL_Y            0x90996u
#define VA_G_FOCAL_Y                    0x90998u
#define VA_G_FOCAL_X                    0x9099cu
#define VA_G_SPRITE_VIEW_ANGLE          0x909fau
#define VA_G_VIEW_ASPECT_SCALE          0x90a14u

static int32_t hwr_round_i32(double value)
{
    if (value <= (double)INT32_MIN)
        return INT32_MIN;
    if (value >= (double)INT32_MAX)
        return INT32_MAX;
    return (int32_t)(value >= 0.0 ? value + 0.5 : value - 0.5);
}

static uint16_t hwr_pick_focal(uint16_t target_focal,
                               uint32_t source_extent,
                               uint32_t target_extent)
{
    uint64_t scaled;
    if (target_focal == 0 || source_extent == 0 || target_extent == 0)
        return 0;
    scaled = (uint64_t)target_focal * source_extent + target_extent / 2u;
    scaled /= target_extent;
    if (scaled == 0 || scaled > UINT16_MAX)
        return 0;
    return (uint16_t)scaled;
}

int hwr_input_prepare_ui_route(const struct roth_api_v1 *api,
                               uint32_t target_width,
                               uint32_t target_height,
                               int centered_engine_layer,
                               struct hwr_input_route *route)
{
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t source_width;
    uint32_t source_height;
    double canvas_x;
    double canvas_y;

    if (api == NULL || api->game_ram == NULL || route == NULL ||
        target_width < 2u || target_height < 2u)
        return 0;
    memset(route, 0, sizeof *route);
    screen_width = api->game_ram->u32(VA_G_SCREEN_WIDTH);
    screen_height = api->game_ram->u32(VA_G_SCREEN_HEIGHT);
    source_width = api->game_ram->u16(VA_G_RENDER_WIDTH);
    source_height = api->game_ram->u16(VA_G_RENDER_HEIGHT);
    if (screen_width < 2u || screen_height < 2u ||
        screen_width > INT16_MAX || screen_height > INT16_MAX ||
        source_width < 2u || source_height < 2u)
        return 0;

    route->world_x = (int32_t)api->game_ram->u32(VA_G_MOUSE_X);
    route->world_y = (int32_t)api->game_ram->u32(VA_G_MOUSE_Y);
    route->hud_x = route->world_x;
    route->hud_y = route->world_y;
    route->source_width = source_width;
    route->source_height = source_height;
    route->target_width = target_width;
    route->target_height = target_height;

    canvas_x = (double)route->world_x * target_width / screen_width;
    canvas_y = (double)route->world_y * target_height / screen_height;
    if (centered_engine_layer) {
        double engine_width;
        double engine_height;
        double engine_left;
        double engine_top;
        double scale_x = (double)target_width / screen_width;
        double scale_y = (double)target_height / screen_height;
        double scale = scale_x < scale_y ? scale_x : scale_y;

        engine_width = screen_width * scale;
        engine_height = screen_height * scale;
        engine_left = ((double)target_width - engine_width) * 0.5;
        engine_top = ((double)target_height - engine_height) * 0.5;
        if (canvas_x >= engine_left &&
            canvas_x < engine_left + engine_width &&
            canvas_y >= engine_top &&
            canvas_y < engine_top + engine_height) {
            route->hud_x = hwr_round_i32(
                (canvas_x - engine_left) * screen_width / engine_width);
            route->hud_y = hwr_round_i32(
                (canvas_y - engine_top) * screen_height / engine_height);
        } else {
            /* A positive off-screen point misses signed top/left tests as well
             * as the ordinary bounded HUD hit boxes. */
            route->hud_x = (int32_t)screen_width + 64;
            route->hud_y = (int32_t)screen_height + 64;
        }
    }
    route->active = 1;
    return 1;
}

int hwr_input_prepare_route(const struct roth_api_v1 *api,
                            uint32_t target_width,
                            uint32_t target_height,
                            int centered_engine_layer,
                            float fov_deg,
                            struct hwr_input_route *route)
{
    struct hwr_gl_camera camera;
    uint32_t source_width;
    uint32_t source_height;

    if (!hwr_input_prepare_ui_route(
            api, target_width, target_height, centered_engine_layer, route))
        return 0;
    source_width = route->source_width;
    source_height = route->source_height;
    memset(&camera, 0, sizeof camera);
    camera.focal_x = api->game_ram->u16(VA_G_FOCAL_X);
    camera.focal_y = api->game_ram->u16(VA_G_FOCAL_Y);
    camera.center_x = (int32_t)(source_width / 2u);
    camera.horizon = (int32_t)(source_height / 2u);
    if (camera.focal_x == 0 || camera.focal_y == 0 ||
        !hwr_projection_scale_camera(
            &camera, source_width, source_height,
            target_width, target_height, fov_deg)) {
        route->active = 0;
        return 0;
    }
    route->pick_focal_x =
        hwr_pick_focal(camera.focal_x, source_width, target_width);
    route->pick_focal_y =
        hwr_pick_focal(camera.focal_y, source_height, target_height);
    if (route->pick_focal_x == 0 || route->pick_focal_y == 0) {
        route->active = 0;
        return 0;
    }
    return 1;
}

void hwr_input_set_hud_mouse(const struct roth_api_v1 *api,
                             const struct hwr_input_route *route)
{
    if (api == NULL || api->game_ram == NULL || route == NULL || !route->active)
        return;
    api->game_ram->set_u32(VA_G_MOUSE_X, (uint32_t)route->hud_x);
    api->game_ram->set_u32(VA_G_MOUSE_Y, (uint32_t)route->hud_y);
}

void hwr_input_set_world_mouse(const struct roth_api_v1 *api,
                               const struct hwr_input_route *route)
{
    if (api == NULL || api->game_ram == NULL || route == NULL || !route->active)
        return;
    api->game_ram->set_u32(VA_G_MOUSE_X, (uint32_t)route->world_x);
    api->game_ram->set_u32(VA_G_MOUSE_Y, (uint32_t)route->world_y);
}

void hwr_input_restore_world_cursor(const struct roth_api_v1 *api,
                                    const struct hwr_input_route *route)
{
    int32_t scale;
    int32_t hotspot_x;
    int32_t hotspot_y;
    int32_t half_x;
    int32_t half_y;

    if (api == NULL || api->game_ram == NULL || route == NULL || !route->active)
        return;
    hwr_input_set_world_mouse(api, route);
    if (api->game_ram->u8(VA_G_MOUSE_RELATIVE_MODE) != 0)
        return;
    scale = (int16_t)api->game_ram->u16(VA_G_PIXEL_EXTENT_SCALE);
    if (scale <= 0)
        return;
    hotspot_x = (int32_t)api->game_ram->u32(VA_G_SAVEUNDER_CURSOR + 0x10u);
    hotspot_y = (int32_t)api->game_ram->u32(VA_G_SAVEUNDER_CURSOR + 0x14u);
    half_x = (int32_t)api->game_ram->u32(VA_G_CURSOR_HALF_X);
    half_y = (int32_t)api->game_ram->u32(VA_G_CURSOR_HALF_Y);
    api->game_ram->set_u32(
        VA_G_SAVEUNDER_CURSOR + 0x8u,
        (uint32_t)(route->world_x - hotspot_x));
    api->game_ram->set_u32(
        VA_G_SAVEUNDER_CURSOR + 0xcu,
        (uint32_t)(route->world_y - hotspot_y));
    api->game_ram->set_u32(
        VA_G_CURSOR_ACCUM_X,
        (uint32_t)(route->world_x * scale - half_x));
    api->game_ram->set_u32(
        VA_G_CURSOR_ACCUM_Y,
        (uint32_t)(route->world_y * scale - half_y));
}

int hwr_input_begin_pick_projection(const struct roth_api_v1 *api,
                                    const struct hwr_input_route *route,
                                    struct hwr_pick_projection *saved)
{
    volatile uint16_t *camera_focal_x;
    volatile uint16_t *camera_focal_y;
    uintptr_t camera_record;

    if (api == NULL || api->game_ram == NULL || route == NULL ||
        saved == NULL || !route->active)
        return 0;
    memset(saved, 0, sizeof *saved);
    camera_record = (uintptr_t)api->game_ram->u32(VA_G_VIEW_PARAMS + 0x4u);
    if (camera_record < 0x1000u)
        return 0;
    camera_focal_x = (volatile uint16_t *)(camera_record + 0x6u);
    camera_focal_y = (volatile uint16_t *)(camera_record + 0x8u);
    saved->camera_record = camera_record;
    saved->camera_focal_x = *camera_focal_x;
    saved->camera_focal_y = *camera_focal_y;
    saved->sprite_view_angle = api->game_ram->u16(VA_G_SPRITE_VIEW_ANGLE);
    saved->view_aspect_scale = api->game_ram->u16(VA_G_VIEW_ASPECT_SCALE);
    saved->focal_x = api->game_ram->u16(VA_G_FOCAL_X);
    saved->inverse_focal_x = api->game_ram->u16(VA_G_INVERSE_FOCAL_X);
    saved->focal_y = api->game_ram->u16(VA_G_FOCAL_Y);
    saved->inverse_focal_y = api->game_ram->u16(VA_G_INVERSE_FOCAL_Y);
    saved->focal_x_u32 = api->game_ram->u32(VA_G_VIEW_PARAMS + 0xcu);
    saved->focal_x_q8 = api->game_ram->u32(VA_G_VIEW_PARAMS + 0x10u);
    saved->focal_y_u32 = api->game_ram->u32(VA_G_VIEW_PARAMS + 0x14u);
    saved->focal_y_q8 = api->game_ram->u32(VA_G_PERSPECTIVE_SCALE);

    *camera_focal_x = route->pick_focal_x;
    *camera_focal_y = route->pick_focal_y;
    api->game_ram->set_u16(
        VA_G_SPRITE_VIEW_ANGLE,
        (uint16_t)(((uint32_t)0x800u * route->pick_focal_x) / 0x10u));
    api->game_ram->set_u16(
        VA_G_VIEW_ASPECT_SCALE,
        (uint16_t)(((uint32_t)0x2000u * route->pick_focal_x) /
                   route->pick_focal_y));
    api->game_ram->set_u16(VA_G_FOCAL_X, route->pick_focal_x);
    api->game_ram->set_u16(
        VA_G_INVERSE_FOCAL_X,
        (uint16_t)(0x40000u / route->pick_focal_x));
    api->game_ram->set_u16(VA_G_FOCAL_Y, route->pick_focal_y);
    api->game_ram->set_u16(
        VA_G_INVERSE_FOCAL_Y,
        (uint16_t)(0x40000u / route->pick_focal_y));
    api->game_ram->set_u32(VA_G_VIEW_PARAMS + 0xcu, route->pick_focal_x);
    api->game_ram->set_u32(
        VA_G_VIEW_PARAMS + 0x10u, (uint32_t)route->pick_focal_x << 8);
    api->game_ram->set_u32(VA_G_VIEW_PARAMS + 0x14u, route->pick_focal_y);
    api->game_ram->set_u32(
        VA_G_PERSPECTIVE_SCALE, (uint32_t)route->pick_focal_y << 8);
    saved->active = 1;
    return 1;
}

void hwr_input_end_pick_projection(const struct roth_api_v1 *api,
                                   struct hwr_pick_projection *saved)
{
    volatile uint16_t *camera_focal_x;
    volatile uint16_t *camera_focal_y;

    if (api == NULL || api->game_ram == NULL || saved == NULL || !saved->active)
        return;
    camera_focal_x =
        (volatile uint16_t *)(saved->camera_record + 0x6u);
    camera_focal_y =
        (volatile uint16_t *)(saved->camera_record + 0x8u);
    *camera_focal_x = saved->camera_focal_x;
    *camera_focal_y = saved->camera_focal_y;
    api->game_ram->set_u16(
        VA_G_SPRITE_VIEW_ANGLE, saved->sprite_view_angle);
    api->game_ram->set_u16(
        VA_G_VIEW_ASPECT_SCALE, saved->view_aspect_scale);
    api->game_ram->set_u16(VA_G_FOCAL_X, saved->focal_x);
    api->game_ram->set_u16(
        VA_G_INVERSE_FOCAL_X, saved->inverse_focal_x);
    api->game_ram->set_u16(VA_G_FOCAL_Y, saved->focal_y);
    api->game_ram->set_u16(
        VA_G_INVERSE_FOCAL_Y, saved->inverse_focal_y);
    api->game_ram->set_u32(
        VA_G_VIEW_PARAMS + 0xcu, saved->focal_x_u32);
    api->game_ram->set_u32(
        VA_G_VIEW_PARAMS + 0x10u, saved->focal_x_q8);
    api->game_ram->set_u32(
        VA_G_VIEW_PARAMS + 0x14u, saved->focal_y_u32);
    api->game_ram->set_u32(
        VA_G_PERSPECTIVE_SCALE, saved->focal_y_q8);
    saved->active = 0;
}
