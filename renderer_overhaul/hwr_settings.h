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

/* Storage the true-colour world image is kept in. Eight bits per channel is
 * exact for the authentic picture and is what auto selects; the floating-point
 * format is headroom a later lighting term can ask for, and is offered only
 * where the driver proves it round-trips. */
enum hwr_truecolor_format {
    HWR_TRUECOLOR_FORMAT_AUTO = 0,
    HWR_TRUECOLOR_FORMAT_RGBA8,
    HWR_TRUECOLOR_FORMAT_RGBA16F
};

struct hwr_settings {
    int enabled;
    int debug;
    uint32_t internal_w;
    uint32_t internal_h;
    enum hwr_window_mode window_mode;
    enum hwr_present_mode present_mode;
    uint32_t min_present_interval_us;

    /* Distance-shading calibration multiplier applied to the view depth. */
    float shade_depth_scale;

    float fov_deg;

    /* Reserved for a later camera mode. */
    enum hwr_pitch_mode pitch_mode;

    /* Set-1 make scancode of the key that opens the settings overlay. */
    uint32_t overlay_key;

    /* The true-colour foundation. It is not a setting anybody chooses: it is
     * required whenever a dependent enhancement contributes, and this switch
     * only forces it on so that its picture can be verified against the
     * authentic one. */
    int truecolor_force;
    enum hwr_truecolor_format truecolor_format;
    /* Uniform gain on the world image, and the foundation's first dependent.
     * One means authentic and takes the neutral early-out. */
    float world_gain;
};

enum hwr_settings_change {
    HWR_CHANGE_ENABLED          = 1u << 0,
    HWR_CHANGE_INTERNAL_SIZE    = 1u << 2,
    HWR_CHANGE_WINDOW_MODE      = 1u << 3,
    HWR_CHANGE_PRESENT_MODE     = 1u << 4,
    HWR_CHANGE_PRESENT_INTERVAL = 1u << 5,
    HWR_CHANGE_FOV              = 1u << 6,
    HWR_CHANGE_PITCH_MODE       = 1u << 7,
    HWR_CHANGE_DEBUG            = 1u << 9,
    HWR_CHANGE_SHADE_SCALE      = 1u << 10,
    HWR_CHANGE_TRUECOLOR        = 1u << 11
};

typedef void (*hwr_settings_apply_fn)(uint32_t mask,
                                      const struct hwr_settings *settings,
                                      void *userdata);

void hwr_settings_defaults(struct hwr_settings *settings);
void hwr_settings_init_defaults(void);
const struct hwr_settings *hwr_settings_get(void);
void hwr_settings_replace(const struct hwr_settings *settings);
int hwr_settings_register_apply(hwr_settings_apply_fn apply, void *userdata);
void hwr_settings_apply_pending(void);
/* Raises change bits without a value in the store having moved. The look layer
 * needs it: a map's overrides change what a row is worth without changing the
 * global value the store keeps. */
void hwr_settings_mark_changed(uint32_t bits);

void hwr_settings_set_enabled(int enabled);
void hwr_settings_set_debug(int debug);
void hwr_settings_set_shade_depth_scale(float scale);
void hwr_settings_set_internal_size(uint32_t width, uint32_t height);
void hwr_settings_set_window_mode(enum hwr_window_mode mode);
void hwr_settings_set_present_mode(enum hwr_present_mode mode);
void hwr_settings_set_min_present_interval_us(uint32_t interval_us);
void hwr_settings_set_fov_deg(float fov_deg);
void hwr_settings_set_pitch_mode(enum hwr_pitch_mode mode);
void hwr_settings_set_overlay_key(uint32_t scancode);

#endif
