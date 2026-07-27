#ifndef DEBUG_CONFIG_H
#define DEBUG_CONFIG_H

#include <stdint.h>

enum debug_anchor {
    DEBUG_ANCHOR_TOP_LEFT = 0,
    DEBUG_ANCHOR_TOP_RIGHT,
    DEBUG_ANCHOR_BOTTOM_LEFT,
    DEBUG_ANCHOR_BOTTOM_RIGHT
};

enum debug_layout_mode {
    DEBUG_LAYOUT_OFF = 0,
    DEBUG_LAYOUT_PERFORMANCE,
    DEBUG_LAYOUT_INSPECTOR,
    DEBUG_LAYOUT_DETAILED,
    DEBUG_LAYOUT_ALL
};

struct debug_config {
    int enabled;
    enum debug_layout_mode initial_mode;
    int show_present_rate;
    int show_tick_rate;
    int show_compose_rate;
    int show_game_rate;
    int show_frame_intervals;
    int show_window_state;
    int show_map;
    int show_player_location;
    int show_view;
    int show_cursor;
    int show_sector_details;
    int show_face_details;
    uint32_t update_hz;
    enum debug_anchor anchor;
    int background;
    uint8_t mode_key;
};

void debug_config_defaults(struct debug_config *config);
void debug_config_load(struct debug_config *config, const char *plugin_dir);
const char *debug_layout_mode_name(enum debug_layout_mode mode);

#endif
