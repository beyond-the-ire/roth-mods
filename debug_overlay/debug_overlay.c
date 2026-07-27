#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "roth_sdk.h"

#include "debug_config.h"
#include "debug_draw.h"
#include "debug_input.h"
#include "debug_metrics.h"
#include "debug_state.h"

static struct debug_config debug_config;
static struct debug_metrics_view debug_metrics;
static struct debug_state_view debug_state;
static uint32_t debug_last_input_generation;
static int debug_force_refresh;

static void debug_on_load(const struct roth_api_v1 *api)
{
    debug_config_load(&debug_config, api->plugin_dir());
    if (!debug_config.enabled) {
        fprintf(stderr, "[mods:debug_overlay] disabled by configuration\n");
        return;
    }
    if (debug_draw_init() != 0) {
        fprintf(stderr,
                "[mods:debug_overlay] unable to allocate overlay buffers; disabled\n");
        debug_config.enabled = 0;
        return;
    }
    debug_metrics_reset();
    debug_state_reset();
    debug_input_init(&debug_config);
    debug_last_input_generation = debug_input_generation();
    debug_force_refresh = 1;
    fprintf(stderr,
            "[mods:debug_overlay] enabled: refresh %u Hz, "
            "mode key 0x%02x, initial mode %s\n",
            debug_config.update_hz,
            debug_config.mode_key,
            debug_layout_mode_name(debug_config.initial_mode));
}

static void debug_on_present(
    const struct roth_api_v1 *api,
    const struct roth_present_timing_v1 *timing)
{
    (void)api;
    if (!debug_config.enabled)
        return;
    debug_metrics_on_present(timing);
}

static void debug_on_frame(const struct roth_api_v1 *api)
{
    uint32_t input_generation;
    int force;

    if (!debug_config.enabled)
        return;
    debug_metrics_on_game();
    debug_state_sample(api, &debug_state);

    input_generation = debug_input_generation();
    force = debug_force_refresh ||
            input_generation != debug_last_input_generation;
    if (debug_metrics_update(&debug_metrics, debug_config.update_hz, force)) {
        debug_draw_render(&debug_config, &debug_metrics, &debug_state,
                          debug_input_mode());
        debug_force_refresh = 0;
        debug_last_input_generation = input_generation;
    }
}

static void debug_on_compose(const struct roth_api_v1 *api,
                             uint8_t *pixels,
                             uint32_t width,
                             uint32_t height)
{
    (void)api;
    if (!debug_config.enabled)
        return;
    debug_metrics_on_compose();
    debug_draw_compose(pixels, width, height, debug_config.anchor);
}

static uint8_t debug_on_scancode(const struct roth_api_v1 *api, uint8_t scancode)
{
    (void)api;
    return debug_input_scancode(&debug_config, scancode);
}

static void debug_on_map_loaded(const struct roth_api_v1 *api,
                                const struct roth_map_event_v1 *event)
{
    (void)api;
    (void)event;
    if (debug_config.enabled)
        debug_force_refresh = 1;
}

static void debug_on_video_mode(
    const struct roth_api_v1 *api,
    const struct roth_video_event_v1 *event)
{
    (void)api;
    if (!debug_config.enabled)
        return;
    debug_state_on_video_mode(event);
    debug_force_refresh = 1;
}

static void debug_on_unload(const struct roth_api_v1 *api)
{
    (void)api;
    if (debug_config.enabled)
        debug_draw_shutdown();
}

static const struct roth_plugin_info_v1 DEBUG_OVERLAY = {
    .abi_major = ROTH_ABI_MAJOR,
    .abi_minor = ROTH_ABI_MINOR,
    .struct_size = sizeof(struct roth_plugin_info_v1),
    .id = "ire.roth.debug_overlay",
    .name = "Debug Overlay",
    .version = "1.0.0",
    .sdk_req_major = ROTH_SDK_MAJOR,
    .sdk_req_minor = ROTH_SDK_MINOR,
    .api_use = ROTH_API_USE_GAME_RAM |
               ROTH_API_USE_COMPOSE |
               ROTH_API_USE_INPUT,
    .on_load = debug_on_load,
    .on_game_ram_ready = NULL,
    .on_frame_game = debug_on_frame,
    .on_compose_tick = debug_on_compose,
    .on_audio = NULL,
    .on_unload = debug_on_unload,
    .on_register_overrides = NULL,
    .on_scancode = debug_on_scancode,
    .on_mouse_poll = NULL,
    .on_map_loaded = debug_on_map_loaded,
    .on_palette_changed = NULL,
    .on_video_mode_changed = debug_on_video_mode,
    .on_present_pace = debug_on_present
};

ROTH_PLUGIN_EXPORT const struct roth_plugin_info_v1 *roth_plugin_query_v1(void)
{
    return &DEBUG_OVERLAY;
}
