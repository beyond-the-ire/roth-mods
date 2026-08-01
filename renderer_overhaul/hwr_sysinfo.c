#include "hwr_sysinfo.h"

#include "hwr_gl.h"
#include "hwr_schema.h"
#include "hwr_settings.h"
#include "roth_sdk.h"

#include <stdio.h>
#include <string.h>

static struct hwr_sysinfo_state g_state;
static char g_message[160];

void hwr_sysinfo_publish(const struct hwr_sysinfo_state *state)
{
    if (state == NULL)
        return;
    g_state = *state;
}

void hwr_sysinfo_note_message(const char *text)
{
    snprintf(g_message, sizeof g_message, "%s", text != NULL ? text : "");
}

/* Every host method is feature-detected against the table the host built before
 * it is called, so a host that predates one of them reports "unavailable"
 * instead of taking the game down. */
static int hwr_sysinfo_window_state(const struct roth_api_v1 *api,
                                    struct roth_window_state_v1 *state)
{
    if (api == NULL || api->window == NULL ||
        api->window->struct_size <
            offsetof(struct roth_window_v1, get_window_state) +
                sizeof(api->window->get_window_state))
        return 0;
    memset(state, 0, sizeof *state);
    state->struct_size = sizeof *state;
    return api->window->get_window_state(api, state) == ROTH_OK;
}

static int hwr_sysinfo_display_modes(const struct roth_api_v1 *api)
{
    if (api == NULL || api->window == NULL ||
        api->window->struct_size <
            offsetof(struct roth_window_v1, get_display_mode) +
                sizeof(api->window->get_display_mode))
        return -1;
    return api->window->get_display_mode_count(api);
}

/* The words a row's own table already has for a value, so the reported state
 * and the setting that asks for it are spelled the same way. */
static const char *hwr_sysinfo_enum_label(const char *key, int32_t value)
{
    const struct hwr_setting_desc *desc = hwr_schema_find(key);
    size_t i;

    if (desc == NULL)
        return "unknown";
    for (i = 0; i < desc->value_count; i++) {
        if (desc->values[i].value != value)
            continue;
        return desc->values[i].label != NULL ? desc->values[i].label
                                             : desc->values[i].token;
    }
    return "unknown";
}

/* The host names window modes for itself; the settings name the two a person
 * can ask for. "Host default" is a request to leave the window alone, so it is
 * never something the host is in. */
static const char *hwr_sysinfo_window_mode_text(uint32_t mode)
{
    switch (mode) {
    case ROTH_WINMODE_WINDOWED:
        return hwr_sysinfo_enum_label("window_mode", HWR_WINDOW_WINDOWED);
    case ROTH_WINMODE_BORDERLESS_FULLSCREEN:
        return hwr_sysinfo_enum_label("window_mode", HWR_WINDOW_BORDERLESS);
    case ROTH_WINMODE_EXCLUSIVE_FULLSCREEN:
        return "Exclusive fullscreen";
    default:
        break;
    }
    return "unknown";
}

static const char *hwr_sysinfo_present_mode_text(uint32_t mode)
{
    switch (mode) {
    case ROTH_PRESENT_FIFO:
        return hwr_sysinfo_enum_label("present_mode", HWR_PRESENT_FIFO);
    case ROTH_PRESENT_IMMEDIATE:
        return hwr_sysinfo_enum_label("present_mode", HWR_PRESENT_IMMEDIATE);
    case ROTH_PRESENT_ADAPTIVE:
        return hwr_sysinfo_enum_label("present_mode", HWR_PRESENT_ADAPTIVE);
    default:
        break;
    }
    return "unknown";
}

static const char *hwr_sysinfo_result_text(int32_t result)
{
    switch (result) {
    case ROTH_OK:
        return "accepted";
    case ROTH_ERR_INVALID:
        return "refused as invalid";
    case ROTH_ERR_NOMEM:
        return "refused: the host had no room for it";
    case ROTH_ERR_NOT_OWNER:
        return "refused: another plugin owns the window";
    case ROTH_ERR_UNSUPPORTED:
        return "refused: this system does not support it";
    case ROTH_ERR_RANGE:
        return "refused: out of range";
    default:
        break;
    }
    return "refused";
}

int hwr_sysinfo_read_version(const struct roth_api_v1 *api, char *out,
                             size_t out_size)
{
    (void)api;
    snprintf(out, out_size, "%s",
             g_state.version != NULL ? g_state.version : "unavailable");
    return 1;
}

int hwr_sysinfo_read_renderer(const struct roth_api_v1 *api, char *out,
                              size_t out_size)
{
    (void)api;
    if (g_state.gl_error != NULL && g_state.gl_error[0] != 0) {
        snprintf(out, out_size, "Not in use: %s", g_state.gl_error);
        return 1;
    }
    snprintf(out, out_size, "%s (%s)",
             g_state.hardware_active ? "In use" : "Not in use",
             g_state.owner_name != NULL ? g_state.owner_name : "unknown");
    return 1;
}

int hwr_sysinfo_read_canvas(const struct roth_api_v1 *api, char *out,
                            size_t out_size)
{
    (void)api;
    if (!g_state.canvas_published || g_state.canvas_w == 0 ||
        g_state.canvas_h == 0) {
        snprintf(out, out_size, "The original renderer is drawing");
        return 1;
    }
    snprintf(out, out_size, "%u x %u", g_state.canvas_w, g_state.canvas_h);
    return 1;
}

int hwr_sysinfo_read_viewport(const struct roth_api_v1 *api, char *out,
                              size_t out_size)
{
    (void)api;
    if (g_state.viewport_w == 0 || g_state.viewport_h == 0) {
        snprintf(out, out_size, "unavailable");
        return 1;
    }
    snprintf(out, out_size, "%u x %u", g_state.viewport_w, g_state.viewport_h);
    return 1;
}

int hwr_sysinfo_read_composition(const struct roth_api_v1 *api, char *out,
                                 size_t out_size)
{
    (void)api;
    if (!g_state.canvas_published) {
        snprintf(out, out_size, "None");
        return 1;
    }
    switch (g_state.composite_mode) {
    case ROTH_COMPOSITE_ENGINE_COLORKEY_OVER_WORLD:
        snprintf(out, out_size, "Engine over the world");
        return 1;
    case ROTH_COMPOSITE_WORLD_ONLY:
        snprintf(out, out_size, "World only");
        return 1;
    case ROTH_COMPOSITE_ENGINE_CENTERED_OVER_CANVAS:
        snprintf(out, out_size, "Engine centered over the canvas");
        return 1;
    default:
        break;
    }
    snprintf(out, out_size, "unknown");
    return 1;
}

int hwr_sysinfo_read_window_mode(const struct roth_api_v1 *api, char *out,
                                 size_t out_size)
{
    struct roth_window_state_v1 state;

    if (!hwr_sysinfo_window_state(api, &state))
        return 0;
    snprintf(out, out_size, "%s",
             hwr_sysinfo_window_mode_text(state.window_mode));
    return 1;
}

int hwr_sysinfo_read_present_mode(const struct roth_api_v1 *api, char *out,
                                  size_t out_size)
{
    struct roth_window_state_v1 state;

    if (!hwr_sysinfo_window_state(api, &state))
        return 0;
    snprintf(out, out_size, "%s",
             hwr_sysinfo_present_mode_text(state.present_mode));
    return 1;
}

int hwr_sysinfo_read_present_interval(const struct roth_api_v1 *api, char *out,
                                      size_t out_size)
{
    struct roth_window_state_v1 state;

    if (!hwr_sysinfo_window_state(api, &state))
        return 0;
    if (state.min_present_interval_us == 0) {
        snprintf(out, out_size, "Uncapped");
        return 1;
    }
    snprintf(out, out_size, "%u us (about %u a second)",
             state.min_present_interval_us,
             1000000u / state.min_present_interval_us);
    return 1;
}

int hwr_sysinfo_read_request_result(const struct roth_api_v1 *api, char *out,
                                    size_t out_size)
{
    struct roth_window_state_v1 state;

    if (!hwr_sysinfo_window_state(api, &state))
        return 0;
    if (state.generation == 0) {
        snprintf(out, out_size, "Nothing has been requested");
        return 1;
    }
    snprintf(out, out_size, "%s",
             hwr_sysinfo_result_text(state.last_request_result));
    return 1;
}

int hwr_sysinfo_read_display_modes(const struct roth_api_v1 *api, char *out,
                                   size_t out_size)
{
    int count = hwr_sysinfo_display_modes(api);
    uint32_t best_w = 0;
    uint32_t best_h = 0;
    uint32_t best_refresh = 0;
    int i;

    if (count < 0)
        return 0;
    if (count == 0) {
        snprintf(out, out_size, "None reported");
        return 1;
    }
    for (i = 0; i < count; i++) {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t refresh = 0;

        if (api->window->get_display_mode(api, i, &width, &height,
                                          &refresh) != ROTH_OK)
            continue;
        if ((uint64_t)width * height <= (uint64_t)best_w * best_h)
            continue;
        best_w = width;
        best_h = height;
        best_refresh = refresh;
    }
    if (best_w == 0 || best_h == 0) {
        snprintf(out, out_size, "%d", count);
        return 1;
    }
    /* The host reports refresh in millihertz. */
    snprintf(out, out_size, "%d, up to %u x %u at %u Hz", count, best_w, best_h,
             (best_refresh + 500u) / 1000u);
    return 1;
}

int hwr_sysinfo_read_present_limit(const struct roth_api_v1 *api, char *out,
                                   size_t out_size)
{
    (void)api;
    if (!g_state.present_caps_valid)
        return 0;
    snprintf(out, out_size, "%u x %u", g_state.present_max_w,
             g_state.present_max_h);
    return 1;
}

int hwr_sysinfo_read_graphics(const struct roth_api_v1 *api, char *out,
                              size_t out_size)
{
    const char *description = hwr_gl_description();

    (void)api;
    if (description == NULL || description[0] == 0) {
        if (g_state.gl_error != NULL && g_state.gl_error[0] != 0) {
            snprintf(out, out_size, "%s", g_state.gl_error);
            return 1;
        }
        return 0;
    }
    snprintf(out, out_size, "%s", description);
    return 1;
}

int hwr_sysinfo_read_message(const struct roth_api_v1 *api, char *out,
                             size_t out_size)
{
    (void)api;
    if (g_message[0] == 0)
        return 0;
    snprintf(out, out_size, "%s", g_message);
    return 1;
}

/* Which settings can be running at something other than what they ask for, and
 * where the running value comes from. This is the one place in the mod that
 * knows a particular setting by name, because "what is this setting actually
 * doing" is the one question a table of ranges and labels cannot answer. */
int hwr_sysinfo_active_text(const struct roth_api_v1 *api,
                            const struct hwr_setting_desc *desc,
                            const struct hwr_settings *saved, char *out,
                            size_t out_size)
{
    struct roth_window_state_v1 state;

    if (desc == NULL || desc->key == NULL || saved == NULL || out == NULL)
        return 0;
    if (strcmp(desc->key, "enabled") == 0) {
        if (!saved->enabled || g_state.hardware_active)
            return 0;
        if (g_state.gl_error != NULL && g_state.gl_error[0] != 0)
            snprintf(out, out_size, "not in use: %s", g_state.gl_error);
        else
            snprintf(out, out_size, "not in use: %s",
                     g_state.owner_name != NULL ? g_state.owner_name
                                                : "the engine is drawing");
        return 1;
    }
    if (strcmp(desc->key, "internal_w") == 0) {
        /* The resolution is one control over two rows, so the width row
         * carries the size the renderer settled on and the height row carries
         * nothing of its own. */
        if (!g_state.canvas_published || g_state.canvas_w == 0 ||
            g_state.canvas_h == 0)
            return 0;
        if (g_state.canvas_w == saved->internal_w &&
            g_state.canvas_h == saved->internal_h)
            return 0;
        if (saved->internal_w == 0 && saved->internal_h == 0)
            return 0;
        snprintf(out, out_size, "%u x %u", g_state.canvas_w, g_state.canvas_h);
        return 1;
    }
    if (strcmp(desc->key, "window_mode") == 0) {
        uint32_t wanted;

        if (saved->window_mode == HWR_WINDOW_DEFAULT ||
            !hwr_sysinfo_window_state(api, &state))
            return 0;
        wanted = saved->window_mode == HWR_WINDOW_BORDERLESS
               ? (uint32_t)ROTH_WINMODE_BORDERLESS_FULLSCREEN
               : (uint32_t)ROTH_WINMODE_WINDOWED;
        if (state.window_mode == wanted)
            return 0;
        snprintf(out, out_size, "%s",
                 hwr_sysinfo_window_mode_text(state.window_mode));
        return 1;
    }
    if (strcmp(desc->key, "present_mode") == 0) {
        if (!hwr_sysinfo_window_state(api, &state))
            return 0;
        if (state.present_mode == (uint32_t)saved->present_mode)
            return 0;
        snprintf(out, out_size, "%s",
                 hwr_sysinfo_present_mode_text(state.present_mode));
        return 1;
    }
    if (strcmp(desc->key, "fps_cap") == 0) {
        if (!hwr_sysinfo_window_state(api, &state))
            return 0;
        if (state.min_present_interval_us == saved->min_present_interval_us)
            return 0;
        if (state.min_present_interval_us == 0)
            snprintf(out, out_size, "uncapped");
        else
            snprintf(out, out_size, "%u",
                     1000000u / state.min_present_interval_us);
        return 1;
    }
    return 0;
}
