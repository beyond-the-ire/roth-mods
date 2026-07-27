#include "hwr_settings.h"

#include <stddef.h>
#include <string.h>

#define HWR_MAX_APPLY_HOOKS 8u

struct hwr_apply_hook {
    hwr_settings_apply_fn apply;
    void *userdata;
};

static struct hwr_settings g_settings;
static struct hwr_apply_hook g_hooks[HWR_MAX_APPLY_HOOKS];
static uint32_t g_hook_count;
static uint32_t g_dirty;

void hwr_settings_defaults(struct hwr_settings *settings)
{
    if (settings == NULL)
        return;
    memset(settings, 0, sizeof *settings);
    settings->enabled = 1;
    /* Shown only while the world is not renderable, such as during a map
     * change. Black is unobtrusive there; a marker colour reads as a flash. */
    settings->solid_color = 0x000000u;
    settings->window_mode = HWR_WINDOW_DEFAULT;
    settings->present_mode = HWR_PRESENT_FIFO;
    settings->pitch_mode = HWR_PITCH_HORIZON_SHIFT;
    settings->render_mode = HWR_RENDER_AUTHENTIC;
    settings->shade_depth_scale = 1.0f;
}

void hwr_settings_init_defaults(void)
{
    hwr_settings_defaults(&g_settings);
    g_hook_count = 0;
    g_dirty = HWR_CHANGE_ALL;
}

const struct hwr_settings *hwr_settings_get(void)
{
    return &g_settings;
}

void hwr_settings_replace(const struct hwr_settings *settings)
{
    if (settings == NULL)
        return;
    hwr_settings_set_enabled(settings->enabled);
    hwr_settings_set_debug(settings->debug);
    hwr_settings_set_shade_depth_scale(settings->shade_depth_scale);
    hwr_settings_set_solid_color(settings->solid_color);
    hwr_settings_set_internal_size(
        settings->internal_w, settings->internal_h);
    hwr_settings_set_window_mode(settings->window_mode);
    hwr_settings_set_present_mode(settings->present_mode);
    hwr_settings_set_min_present_interval_us(
        settings->min_present_interval_us);
    hwr_settings_set_fov_deg(settings->fov_deg);
    hwr_settings_set_pitch_mode(settings->pitch_mode);
    hwr_settings_set_render_mode(settings->render_mode);
}

int hwr_settings_register_apply(hwr_settings_apply_fn apply, void *userdata)
{
    if (apply == NULL || g_hook_count >= HWR_MAX_APPLY_HOOKS)
        return -1;
    g_hooks[g_hook_count].apply = apply;
    g_hooks[g_hook_count].userdata = userdata;
    g_hook_count++;
    return 0;
}

void hwr_settings_apply_pending(void)
{
    uint32_t i;
    uint32_t mask = g_dirty;

    if (mask == 0)
        return;
    g_dirty = 0;
    for (i = 0; i < g_hook_count; i++)
        g_hooks[i].apply(mask, &g_settings, g_hooks[i].userdata);
}

void hwr_settings_set_enabled(int enabled)
{
    enabled = enabled != 0;
    if (g_settings.enabled != enabled) {
        g_settings.enabled = enabled;
        g_dirty |= HWR_CHANGE_ENABLED;
    }
}

void hwr_settings_set_shade_depth_scale(float scale)
{
    if (scale <= 0.0f || scale > 4096.0f)
        return;
    if (g_settings.shade_depth_scale != scale) {
        g_settings.shade_depth_scale = scale;
        g_dirty |= HWR_CHANGE_SHADE_SCALE;
    }
}

void hwr_settings_set_debug(int debug)
{
    debug = debug != 0;
    if (g_settings.debug != debug) {
        g_settings.debug = debug;
        g_dirty |= HWR_CHANGE_DEBUG;
    }
}

void hwr_settings_set_solid_color(uint32_t color)
{
    color &= 0x00ffffffu;
    if (g_settings.solid_color != color) {
        g_settings.solid_color = color;
        g_dirty |= HWR_CHANGE_SOLID_COLOR;
    }
}

void hwr_settings_set_internal_size(uint32_t width, uint32_t height)
{
    if (g_settings.internal_w != width || g_settings.internal_h != height) {
        g_settings.internal_w = width;
        g_settings.internal_h = height;
        g_dirty |= HWR_CHANGE_INTERNAL_SIZE;
    }
}

void hwr_settings_set_window_mode(enum hwr_window_mode mode)
{
    if (g_settings.window_mode != mode) {
        g_settings.window_mode = mode;
        g_dirty |= HWR_CHANGE_WINDOW_MODE;
    }
}

void hwr_settings_set_present_mode(enum hwr_present_mode mode)
{
    if (g_settings.present_mode != mode) {
        g_settings.present_mode = mode;
        g_dirty |= HWR_CHANGE_PRESENT_MODE;
    }
}

void hwr_settings_set_min_present_interval_us(uint32_t interval_us)
{
    if (g_settings.min_present_interval_us != interval_us) {
        g_settings.min_present_interval_us = interval_us;
        g_dirty |= HWR_CHANGE_PRESENT_INTERVAL;
    }
}

void hwr_settings_set_fov_deg(float fov_deg)
{
    if (fov_deg != 0.0f && (fov_deg < 30.0f || fov_deg > 170.0f))
        return;
    if (g_settings.fov_deg != fov_deg) {
        g_settings.fov_deg = fov_deg;
        g_dirty |= HWR_CHANGE_FOV;
    }
}

void hwr_settings_set_pitch_mode(enum hwr_pitch_mode mode)
{
    if (g_settings.pitch_mode != mode) {
        g_settings.pitch_mode = mode;
        g_dirty |= HWR_CHANGE_PITCH_MODE;
    }
}

void hwr_settings_set_render_mode(enum hwr_render_mode mode)
{
    if (g_settings.render_mode != mode) {
        g_settings.render_mode = mode;
        g_dirty |= HWR_CHANGE_RENDER_MODE;
    }
}
