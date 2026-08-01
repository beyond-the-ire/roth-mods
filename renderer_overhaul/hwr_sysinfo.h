#ifndef HWR_SYSINFO_H
#define HWR_SYSINFO_H

#include <stddef.h>
#include <stdint.h>

/* What the renderer and the host are actually doing, as opposed to what the
 * settings ask for. The reported-state rows of the schema read their text from
 * here, and the settings overlay asks here for the value a row is running at
 * when it differs from the value in force.
 *
 * The renderer's own state arrives through hwr_sysinfo_publish, called once per
 * game tick from the entry file, so this module needs none of that file's
 * statics and that file exports none of them. Everything else is read from the
 * host through the SDK, feature-detected before it is called. */

struct roth_api_v1;
struct hwr_setting_desc;
struct hwr_settings;

struct hwr_sysinfo_state {
    const char *version;
    /* The hardware renderer is the thing on screen. */
    int hardware_active;
    /* A canvas of the mod's own is published, so the sizes below are real. */
    int canvas_published;
    uint32_t canvas_w;
    uint32_t canvas_h;
    uint32_t viewport_w;
    uint32_t viewport_h;
    uint32_t composite_mode;
    const char *owner_name;
    /* Why hardware rendering is not in use, when it failed outright. */
    const char *gl_error;
    uint32_t present_max_w;
    uint32_t present_max_h;
    int present_caps_valid;
    int map_loaded;
};

/* GAME thread, once per tick. */
void hwr_sysinfo_publish(const struct hwr_sysinfo_state *state);
/* The last thing the settings overlay said, kept so a message that has scrolled
 * out of the status line is still readable. */
void hwr_sysinfo_note_message(const char *text);

/* The schema's reported-state rows. */
int hwr_sysinfo_read_version(const struct roth_api_v1 *api, char *out,
                             size_t out_size);
int hwr_sysinfo_read_renderer(const struct roth_api_v1 *api, char *out,
                              size_t out_size);
int hwr_sysinfo_read_canvas(const struct roth_api_v1 *api, char *out,
                            size_t out_size);
int hwr_sysinfo_read_viewport(const struct roth_api_v1 *api, char *out,
                              size_t out_size);
int hwr_sysinfo_read_composition(const struct roth_api_v1 *api, char *out,
                                 size_t out_size);
int hwr_sysinfo_read_window_mode(const struct roth_api_v1 *api, char *out,
                                 size_t out_size);
int hwr_sysinfo_read_present_mode(const struct roth_api_v1 *api, char *out,
                                  size_t out_size);
int hwr_sysinfo_read_present_interval(const struct roth_api_v1 *api, char *out,
                                      size_t out_size);
int hwr_sysinfo_read_request_result(const struct roth_api_v1 *api, char *out,
                                    size_t out_size);
int hwr_sysinfo_read_display_modes(const struct roth_api_v1 *api, char *out,
                                   size_t out_size);
int hwr_sysinfo_read_present_limit(const struct roth_api_v1 *api, char *out,
                                   size_t out_size);
int hwr_sysinfo_read_graphics(const struct roth_api_v1 *api, char *out,
                              size_t out_size);
int hwr_sysinfo_read_message(const struct roth_api_v1 *api, char *out,
                             size_t out_size);

/* What the row is running at, when that is not what the settings hold. Returns
 * 1 and fills out; returns 0 when the running value is the settings value or
 * cannot be known. */
int hwr_sysinfo_active_text(const struct roth_api_v1 *api,
                            const struct hwr_setting_desc *desc,
                            const struct hwr_settings *saved, char *out,
                            size_t out_size);

#endif
