#ifndef HWR_SETTINGS_H
#define HWR_SETTINGS_H

#include <stdint.h>

enum hwr_window_mode {
    HWR_WINDOW_DEFAULT = 0,
    HWR_WINDOW_WINDOWED,
    HWR_WINDOW_BORDERLESS
};

enum hwr_present_mode {
    HWR_PRESENT_FIFO = 0,
    HWR_PRESENT_IMMEDIATE,
    HWR_PRESENT_ADAPTIVE
};

enum hwr_pitch_mode {
    HWR_PITCH_HORIZON_SHIFT = 0,
    HWR_PITCH_TRUE_ROTATION
};

enum hwr_render_mode {
    HWR_RENDER_AUTHENTIC = 0,
    HWR_RENDER_ENHANCED
};

struct hwr_settings {
    int enabled;
    int debug;
    uint32_t solid_color;
    uint32_t internal_w;
    uint32_t internal_h;
    enum hwr_window_mode window_mode;
    enum hwr_present_mode present_mode;
    uint32_t min_present_interval_us;

    /* Distance-shading calibration multiplier applied to the view depth. */
    float shade_depth_scale;

    /* Reserved for renderer features that are not active yet. */
    float fov_deg;
    enum hwr_pitch_mode pitch_mode;
    enum hwr_render_mode render_mode;
};

enum hwr_settings_change {
    HWR_CHANGE_ENABLED          = 1u << 0,
    HWR_CHANGE_SOLID_COLOR      = 1u << 1,
    HWR_CHANGE_INTERNAL_SIZE    = 1u << 2,
    HWR_CHANGE_WINDOW_MODE      = 1u << 3,
    HWR_CHANGE_PRESENT_MODE     = 1u << 4,
    HWR_CHANGE_PRESENT_INTERVAL = 1u << 5,
    HWR_CHANGE_FOV              = 1u << 6,
    HWR_CHANGE_PITCH_MODE       = 1u << 7,
    HWR_CHANGE_RENDER_MODE      = 1u << 8,
    HWR_CHANGE_DEBUG            = 1u << 9,
    HWR_CHANGE_SHADE_SCALE      = 1u << 10,
    HWR_CHANGE_ALL              = (1u << 11) - 1u
};

typedef void (*hwr_settings_apply_fn)(uint32_t mask,
                                      const struct hwr_settings *settings,
                                      void *userdata);

void hwr_settings_init_defaults(void);
const struct hwr_settings *hwr_settings_get(void);
int hwr_settings_register_apply(hwr_settings_apply_fn apply, void *userdata);
void hwr_settings_apply_pending(void);

void hwr_settings_set_enabled(int enabled);
void hwr_settings_set_debug(int debug);
void hwr_settings_set_shade_depth_scale(float scale);
void hwr_settings_set_solid_color(uint32_t color);
void hwr_settings_set_internal_size(uint32_t width, uint32_t height);
void hwr_settings_set_window_mode(enum hwr_window_mode mode);
void hwr_settings_set_present_mode(enum hwr_present_mode mode);
void hwr_settings_set_min_present_interval_us(uint32_t interval_us);
void hwr_settings_set_fov_deg(float fov_deg);
void hwr_settings_set_pitch_mode(enum hwr_pitch_mode mode);
void hwr_settings_set_render_mode(enum hwr_render_mode mode);

#endif
