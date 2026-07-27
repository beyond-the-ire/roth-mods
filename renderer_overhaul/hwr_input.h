#ifndef HWR_INPUT_H
#define HWR_INPUT_H

#include "roth_sdk.h"

#include <stdint.h>

struct hwr_input_route {
    int active;
    int32_t world_x;
    int32_t world_y;
    int32_t hud_x;
    int32_t hud_y;
    uint32_t source_width;
    uint32_t source_height;
    uint32_t target_width;
    uint32_t target_height;
    uint16_t pick_focal_x;
    uint16_t pick_focal_y;
};

struct hwr_pick_projection {
    uintptr_t camera_record;
    uint16_t camera_focal_x;
    uint16_t camera_focal_y;
    uint16_t sprite_view_angle;
    uint16_t view_aspect_scale;
    uint16_t focal_x;
    uint16_t inverse_focal_x;
    uint16_t focal_y;
    uint16_t inverse_focal_y;
    uint32_t focal_x_u32;
    uint32_t focal_x_q8;
    uint32_t focal_y_u32;
    uint32_t focal_y_q8;
    int active;
};

int hwr_input_prepare_route(const struct roth_api_v1 *api,
                            uint32_t target_width,
                            uint32_t target_height,
                            int centered_engine_layer,
                            float fov_deg,
                            struct hwr_input_route *route);
int hwr_input_prepare_ui_route(const struct roth_api_v1 *api,
                               uint32_t target_width,
                               uint32_t target_height,
                               int centered_engine_layer,
                               struct hwr_input_route *route);
void hwr_input_set_hud_mouse(const struct roth_api_v1 *api,
                             const struct hwr_input_route *route);
void hwr_input_set_world_mouse(const struct roth_api_v1 *api,
                               const struct hwr_input_route *route);
void hwr_input_restore_world_cursor(const struct roth_api_v1 *api,
                                    const struct hwr_input_route *route);
int hwr_input_begin_pick_projection(const struct roth_api_v1 *api,
                                    const struct hwr_input_route *route,
                                    struct hwr_pick_projection *saved);
void hwr_input_end_pick_projection(const struct roth_api_v1 *api,
                                   struct hwr_pick_projection *saved);

#endif
