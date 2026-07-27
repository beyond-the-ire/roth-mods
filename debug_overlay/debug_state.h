#ifndef DEBUG_STATE_H
#define DEBUG_STATE_H

#include <stdint.h>

#include "roth_sdk.h"

struct debug_state_view {
    int raw_map_valid;
    char raw_map_name[81];
    int das_valid;
    char das_name[81];

    int player_valid;
    int16_t player_x;
    int16_t player_y;
    int16_t player_z;
    uint16_t player_sector_offset;
    int sector_index_valid;
    uint32_t player_sector_index;
    int sector_details_valid;
    int16_t sector_roof_height;
    int16_t sector_floor_height;
    uint8_t sector_glow;
    int16_t sector_id;
    uint16_t sector_texture_bottom;
    uint16_t sector_texture_top;
    int mid_platform_valid;
    int16_t mid_platform_roof_height;
    int16_t mid_platform_floor_height;
    uint16_t mid_platform_roof_texture;
    uint16_t mid_platform_floor_texture;
    uint16_t player_yaw;
    int32_t requested_pitch;
    int32_t applied_pitch;

    int video_valid;
    uint32_t pixel_w;
    uint32_t pixel_h;
    uint32_t aspect_w;
    uint32_t aspect_h;

    int window_valid;
    struct roth_window_state_v1 window;

    uint8_t cursor_type;
    uint8_t cursor_surface_kind;
    uint16_t cursor_das_entry;
    uint16_t cursor_texture_v;
    uint16_t cursor_texture_u;
    uint16_t cursor_face_record_offset;
    uint16_t cursor_sector_record_offset;
    uint16_t cursor_draw_flags;
    uint32_t cursor_render_record_ref;
    int cursor_face_index_valid;
    uint32_t cursor_face_index;
    int cursor_sector_index_valid;
    uint32_t cursor_sector_index;
    int cursor_face_textures_valid;
    uint16_t cursor_face_texture_bottom;
    uint16_t cursor_face_texture_middle;
    uint16_t cursor_face_texture_top;
};

void debug_state_reset(void);
void debug_state_on_video_mode(const struct roth_video_event_v1 *event);
void debug_state_sample(const struct roth_api_v1 *api,
                        struct debug_state_view *view);

#endif
