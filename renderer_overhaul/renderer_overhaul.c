#include "roth_sdk.h"

#include "hwr_gl.h"
#include "hwr_das.h"
#include "hwr_settings.h"
#include "hwr_world.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

#define VA_G_SCREEN_PITCH              0x85498u
#define VA_G_SCREEN_HEIGHT             0x854a0u
#define VA_G_VIEW_X                    0x85ce0u
#define VA_G_VIEW_Y                    0x85ce4u
#define VA_G_FRAMEBUFFER_PTR           0x90a98u
#define VA_G_RENDER_WIDTH              0x90bf2u
#define VA_G_RENDER_HEIGHT             0x90bf6u
#define VA_G_VIEW_SCALE_FLAGS          0x90bd4u
#define VA_G_LINE_DOUBLING_FLAG        0x90cbdu
#define VA_G_MAP_DAS_NAME              0x7023cu
#define VA_G_SHARED_DAS_NAME           0x702dcu

struct hwr_viewport {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
};

struct hwr_render_settings {
    int enabled;
    uint32_t solid_color;
    uint32_t internal_w;
    uint32_t internal_h;
};

static const struct roth_api_v1 *g_api;
/* The present channel identifies the owner by the api-table pointer that first submits a frame.
 * Override callbacks and lifecycle callbacks are handed different api-table copies for the same
 * plugin, so every submit/withdraw must use one consistent token or the channel reports a foreign
 * owner. The world pass owns the channel, so its api pointer is the canonical identity. */
static const struct roth_api_v1 *g_channel_api;
static struct hwr_render_settings g_render_settings = { 1, 0xff8800u, 0, 0 };
static struct hwr_settings g_window_settings;
static uint32_t g_window_pending;
static uint32_t g_wrap_serial;
static uint32_t g_watchdog_serial;
static uint32_t g_watchdog_stall_count;
static uint32_t g_poll_serial;
static uint32_t g_poll_stall_count;
static uint32_t g_poll_throttle;
static struct roth_world_frame_v1 g_last_frame;
static int g_last_frame_valid;
static uint32_t g_present_max_w;
static uint32_t g_present_max_h;
static int g_present_caps_checked;
static int g_present_caps_valid;
static int g_channel_owned;
static int g_suppress_active;
static int g_gl_permanently_failed;
static int g_gl_error_logged;
static int g_scale_mode_logged;
static int g_first_frame_applied;
static int g_missing_window_logged;
static int g_viewport_error_logged;
static int g_sentinel_error_logged;
static int g_submit_error_logged;
static int g_debug_enabled;
static char g_plugin_dir[2600];
static struct hwr_world g_world;
static struct hwr_das_archive g_map_archive;
static struct hwr_das_archive g_shared_archive;

static int hwr_ascii_equal_ci(const char *a, const char *b)
{
    while (*a != 0 && *b != 0) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static char *hwr_trim(char *text)
{
    char *end;
    while (*text != 0 && isspace((unsigned char)*text))
        text++;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]))
        *--end = 0;
    return text;
}

static int hwr_parse_u32(const char *text, int base, uint32_t maximum,
                         uint32_t *value)
{
    char *end;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, base);
    while (*end != 0 && isspace((unsigned char)*end))
        end++;
    if (errno != 0 || end == text || *end != 0 || parsed > maximum)
        return 0;
    *value = (uint32_t)parsed;
    return 1;
}

static int hwr_parse_bool(const char *text, int *value)
{
    if (hwr_ascii_equal_ci(text, "1") || hwr_ascii_equal_ci(text, "true") ||
        hwr_ascii_equal_ci(text, "yes") || hwr_ascii_equal_ci(text, "on")) {
        *value = 1;
        return 1;
    }
    if (hwr_ascii_equal_ci(text, "0") || hwr_ascii_equal_ci(text, "false") ||
        hwr_ascii_equal_ci(text, "no") || hwr_ascii_equal_ci(text, "off")) {
        *value = 0;
        return 1;
    }
    return 0;
}

static int hwr_parse_float(const char *text, float *value)
{
    char *end;
    double parsed;

    errno = 0;
    parsed = strtod(text, &end);
    while (*end != 0 && isspace((unsigned char)*end))
        end++;
    if (errno != 0 || end == text || *end != 0 || parsed < 0.0 || parsed > 360.0)
        return 0;
    *value = (float)parsed;
    return 1;
}

static int hwr_apply_config_value(const char *key, const char *value)
{
    const struct hwr_settings *settings = hwr_settings_get();
    uint32_t number;
    int boolean;
    float real;

    if (hwr_ascii_equal_ci(key, "enabled")) {
        if (!hwr_parse_bool(value, &boolean))
            return 0;
        hwr_settings_set_enabled(boolean);
    } else if (hwr_ascii_equal_ci(key, "debug")) {
        if (!hwr_parse_bool(value, &boolean))
            return 0;
        hwr_settings_set_debug(boolean);
    } else if (hwr_ascii_equal_ci(key, "solid_color")) {
        if (!hwr_parse_u32(value, 16, 0x00ffffffu, &number))
            return 0;
        hwr_settings_set_solid_color(number);
    } else if (hwr_ascii_equal_ci(key, "internal_w")) {
        if (!hwr_parse_u32(value, 10, 16384u, &number))
            return 0;
        hwr_settings_set_internal_size(number, settings->internal_h);
    } else if (hwr_ascii_equal_ci(key, "internal_h")) {
        if (!hwr_parse_u32(value, 10, 16384u, &number))
            return 0;
        hwr_settings_set_internal_size(settings->internal_w, number);
    } else if (hwr_ascii_equal_ci(key, "window_mode")) {
        if (hwr_ascii_equal_ci(value, "default"))
            hwr_settings_set_window_mode(HWR_WINDOW_DEFAULT);
        else if (hwr_ascii_equal_ci(value, "windowed"))
            hwr_settings_set_window_mode(HWR_WINDOW_WINDOWED);
        else if (hwr_ascii_equal_ci(value, "borderless"))
            hwr_settings_set_window_mode(HWR_WINDOW_BORDERLESS);
        else
            return 0;
    } else if (hwr_ascii_equal_ci(key, "present_mode")) {
        if (hwr_ascii_equal_ci(value, "fifo"))
            hwr_settings_set_present_mode(HWR_PRESENT_FIFO);
        else if (hwr_ascii_equal_ci(value, "immediate"))
            hwr_settings_set_present_mode(HWR_PRESENT_IMMEDIATE);
        else if (hwr_ascii_equal_ci(value, "adaptive"))
            hwr_settings_set_present_mode(HWR_PRESENT_ADAPTIVE);
        else
            return 0;
    } else if (hwr_ascii_equal_ci(key, "min_present_interval_us")) {
        if (!hwr_parse_u32(value, 10, 10000000u, &number))
            return 0;
        hwr_settings_set_min_present_interval_us(number);
    } else if (hwr_ascii_equal_ci(key, "fps_cap")) {
        if (!hwr_parse_u32(value, 10, 1000000u, &number))
            return 0;
        hwr_settings_set_min_present_interval_us(number == 0 ? 0 : 1000000u / number);
    } else if (hwr_ascii_equal_ci(key, "fov_deg")) {
        if (!hwr_parse_float(value, &real))
            return 0;
        hwr_settings_set_fov_deg(real);
    } else if (hwr_ascii_equal_ci(key, "pitch_mode")) {
        if (hwr_ascii_equal_ci(value, "horizon_shift"))
            hwr_settings_set_pitch_mode(HWR_PITCH_HORIZON_SHIFT);
        else if (hwr_ascii_equal_ci(value, "true_rotation"))
            hwr_settings_set_pitch_mode(HWR_PITCH_TRUE_ROTATION);
        else
            return 0;
    } else if (hwr_ascii_equal_ci(key, "render_mode")) {
        if (hwr_ascii_equal_ci(value, "authentic"))
            hwr_settings_set_render_mode(HWR_RENDER_AUTHENTIC);
        else if (hwr_ascii_equal_ci(value, "enhanced"))
            hwr_settings_set_render_mode(HWR_RENDER_ENHANCED);
        else
            return 0;
    } else {
        return -1;
    }
    return 1;
}

static void hwr_load_config(const struct roth_api_v1 *api)
{
    char path[2600];
    char line[512];
    FILE *file;
    unsigned int line_number = 0;

    snprintf(path, sizeof path, "%s/renderer_overhaul.cfg", api->plugin_dir());
    file = fopen(path, "r");
    if (file == NULL)
        return;

    while (fgets(line, sizeof line, file) != NULL) {
        char *comment;
        char *equals;
        char *key;
        char *value;
        int result;

        line_number++;
        comment = strchr(line, '#');
        if (comment != NULL)
            *comment = 0;
        key = hwr_trim(line);
        if (*key == 0)
            continue;
        equals = strchr(key, '=');
        if (equals == NULL) {
            fprintf(stderr, "[renderer_overhaul] %s:%u: expected key=value; line ignored\n",
                    path, line_number);
            continue;
        }
        *equals = 0;
        value = hwr_trim(equals + 1);
        key = hwr_trim(key);
        result = hwr_apply_config_value(key, value);
        if (result == 0)
            fprintf(stderr, "[renderer_overhaul] %s:%u: invalid value for '%s'; line ignored\n",
                    path, line_number, key);
        else if (result < 0)
            fprintf(stderr, "[renderer_overhaul] %s:%u: unknown setting '%s'; line ignored\n",
                    path, line_number, key);
    }
    fclose(file);
}

static void hwr_render_settings_apply(uint32_t mask,
                                      const struct hwr_settings *settings,
                                      void *userdata)
{
    (void)userdata;
    if (mask & HWR_CHANGE_ENABLED)
        g_render_settings.enabled = settings->enabled;
    if (mask & HWR_CHANGE_SOLID_COLOR)
        g_render_settings.solid_color = settings->solid_color;
    if (mask & HWR_CHANGE_INTERNAL_SIZE) {
        g_render_settings.internal_w = settings->internal_w;
        g_render_settings.internal_h = settings->internal_h;
    }
}

static void hwr_data_settings_apply(uint32_t mask,
                                    const struct hwr_settings *settings,
                                    void *userdata)
{
    (void)userdata;
    if ((mask & HWR_CHANGE_DEBUG) != 0)
        g_debug_enabled = settings->debug;
}

static void hwr_map_data_clear(void)
{
    hwr_world_clear(&g_world);
    hwr_das_close(&g_map_archive);
    hwr_das_close(&g_shared_archive);
}

static int hwr_read_archive_basename(const struct roth_api_v1 *api,
                                     uint32_t address, char name[81])
{
    char *base;
    char *cursor;
    if (api->game_ram->read_block(address, name, 80) != 0)
        return -1;
    name[80] = 0;
    base = name;
    for (cursor = name; *cursor != 0; cursor++) {
        if (*cursor == '/' || *cursor == '\\' || *cursor == ':')
            base = cursor + 1;
    }
    if (*base == 0)
        return -1;
    if (base != name)
        memmove(name, base, strlen(base) + 1u);
    return 0;
}

static int hwr_parent_path(char *path)
{
    char *cursor;
    char *last = NULL;
    size_t length = strlen(path);
    while (length != 0 && (path[length - 1] == '/' || path[length - 1] == '\\'))
        path[--length] = 0;
    for (cursor = path; *cursor != 0; cursor++) {
        if (*cursor == '/' || *cursor == '\\')
            last = cursor;
    }
    if (last == NULL)
        return -1;
    *last = 0;
    return 0;
}

static int hwr_path_join(char *destination, size_t capacity,
                         const char *left, const char *right)
{
    int result = snprintf(destination, capacity, "%s/%s", left, right);
    return result >= 0 && (size_t)result < capacity ? 0 : -1;
}

static int hwr_regular_file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return 0;
    fclose(file);
    return 1;
}

static int hwr_find_archive_path(const char *root, const char *name,
                                 char *path, size_t capacity)
{
    char directory[2600];
    if (hwr_path_join(directory, sizeof directory, root, "M") != 0 ||
        hwr_path_join(path, capacity, directory, name) != 0)
        return -1;
    if (hwr_regular_file_exists(path))
        return 0;
#ifdef _WIN32
    {
        WIN32_FIND_DATAA data;
        HANDLE search;
        char pattern[2600];
        if (hwr_path_join(pattern, sizeof pattern, directory, "*") != 0)
            return -1;
        search = FindFirstFileA(pattern, &data);
        if (search == INVALID_HANDLE_VALUE)
            return -1;
        do {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
                hwr_ascii_equal_ci(data.cFileName, name)) {
                int result = hwr_path_join(path, capacity, directory, data.cFileName);
                FindClose(search);
                return result;
            }
        } while (FindNextFileA(search, &data));
        FindClose(search);
    }
#else
    {
        DIR *stream = opendir(directory);
        struct dirent *entry;
        if (stream == NULL)
            return -1;
        while ((entry = readdir(stream)) != NULL) {
            if (hwr_ascii_equal_ci(entry->d_name, name)) {
                int result = hwr_path_join(path, capacity, directory, entry->d_name);
                closedir(stream);
                return result;
            }
        }
        closedir(stream);
    }
#endif
    return -1;
}

static void hwr_print_das_summary(const char *label,
                                  const struct hwr_das_archive *archive,
                                  const struct hwr_das_census *census)
{
    fprintf(stderr,
            "  %s DAS: entries=%u plain=%u animated=%u space=%u dollar=%u other=%u decoded=%u frames=%u failures=%u\n",
            label, (unsigned)archive->entry_count,
            (unsigned)census->classes[HWR_DAS_CLASS_PLAIN],
            (unsigned)census->classes[HWR_DAS_CLASS_ANIMATED],
            (unsigned)census->classes[HWR_DAS_CLASS_PLACEHOLDER_SPACE],
            (unsigned)census->classes[HWR_DAS_CLASS_PLACEHOLDER_DOLLAR],
            (unsigned)census->classes[HWR_DAS_CLASS_OTHER],
            (unsigned)census->decoded_textures,
            (unsigned)census->animation_frames, (unsigned)census->failures);
}

static void hwr_gl_settings_apply(uint32_t mask,
                                  const struct hwr_settings *settings,
                                  void *userdata)
{
    (void)userdata;
    if (mask & HWR_CHANGE_INTERNAL_SIZE) {
        hwr_gl_resize(settings->internal_w, settings->internal_h);
        g_last_frame_valid = 0;   /* the cached frame references the old readback buffer */
    }
}

static void hwr_window_settings_apply(uint32_t mask,
                                      const struct hwr_settings *settings,
                                      void *userdata)
{
    const uint32_t window_mask = HWR_CHANGE_WINDOW_MODE |
                                 HWR_CHANGE_PRESENT_MODE |
                                 HWR_CHANGE_PRESENT_INTERVAL;
    (void)userdata;
    if ((mask & window_mask) != 0) {
        g_window_settings = *settings;
        g_window_pending |= mask & window_mask;
    }
}

static void hwr_apply_window_requests(const struct roth_api_v1 *api)
{
    uint32_t pending = g_window_pending;
    int32_t result;

    if (pending == 0)
        return;
    if (api->window == NULL) {
        if (!g_missing_window_logged) {
            fprintf(stderr, "[renderer_overhaul] host window controls are unavailable\n");
            g_missing_window_logged = 1;
        }
        g_window_pending = 0;
        return;
    }

    if ((pending & HWR_CHANGE_WINDOW_MODE) != 0 &&
        g_window_settings.window_mode != HWR_WINDOW_DEFAULT) {
        uint32_t mode = g_window_settings.window_mode == HWR_WINDOW_BORDERLESS
                      ? ROTH_WINMODE_BORDERLESS_FULLSCREEN : ROTH_WINMODE_WINDOWED;
        result = api->window->set_window_mode(api, mode);
        if (result != ROTH_OK)
            fprintf(stderr, "[renderer_overhaul] window-mode request failed: %d\n", (int)result);
    }
    if ((pending & HWR_CHANGE_PRESENT_MODE) != 0) {
        uint32_t mode = ROTH_PRESENT_FIFO;
        if (g_window_settings.present_mode == HWR_PRESENT_IMMEDIATE)
            mode = ROTH_PRESENT_IMMEDIATE;
        else if (g_window_settings.present_mode == HWR_PRESENT_ADAPTIVE)
            mode = ROTH_PRESENT_ADAPTIVE;
        result = api->window->set_present_mode(api, mode);
        if (result != ROTH_OK)
            fprintf(stderr, "[renderer_overhaul] present-mode request failed: %d\n", (int)result);
    }
    if ((pending & HWR_CHANGE_PRESENT_INTERVAL) != 0) {
        result = api->window->set_min_present_interval_us(
            api, g_window_settings.min_present_interval_us);
        if (result != ROTH_OK)
            fprintf(stderr, "[renderer_overhaul] present-interval request failed: %d\n", (int)result);
    }
    g_window_pending = 0;
}

static void hwr_withdraw_if_owned(const struct roth_api_v1 *api)
{
    int32_t result;
    g_last_frame_valid = 0;
    if (!g_channel_owned || api->present == NULL || g_channel_api == NULL)
        return;
    result = api->present->withdraw_world_frame(g_channel_api);
    if (result == ROTH_OK || result == ROTH_ERR_NOT_OWNER)
        g_channel_owned = 0;
}

/* While a menu or dialog is up the engine freezes the world instead of repainting it, so the
 * last submitted frame must stay composited. The host drops a stale frame after a fixed number
 * of presents as crash protection; periodic re-submission of the identical frame keeps the
 * frozen backdrop visible. Real transitions (video-mode change, map load) withdraw instead. */
static void hwr_resubmit_if_stalled(const struct roth_api_v1 *api)
{
    int32_t result;
    if (!g_channel_owned || !g_last_frame_valid || api->present == NULL ||
        g_channel_api == NULL)
        return;
    result = api->present->submit_world_frame(g_channel_api, &g_last_frame);
    if (result == ROTH_ERR_NOT_OWNER) {
        g_channel_owned = 0;
        g_last_frame_valid = 0;
    }
}

static void hwr_fail_gl(const struct roth_api_v1 *api)
{
    g_gl_permanently_failed = 1;
    hwr_withdraw_if_owned(api);
    if (!g_gl_error_logged) {
        fprintf(stderr, "[renderer_overhaul] OpenGL disabled: %s\n", hwr_gl_last_error());
        g_gl_error_logged = 1;
    }
}

/* The world-viewport rect in final framebuffer coordinates. Use the STABLE per-frame viewport
 * dimensions (origin + render size), NOT g_view_bound_left/right/top/bottom: those are transient
 * per-surface clip registers the engine rewrites for every wall, portal, and mirror span, so
 * reading them after the render leaves only the last surface's clip window. Filling and submitting
 * that sliver would leave the rest of the frame's software walls exposed. */
static int hwr_read_viewport(const struct roth_api_v1 *api,
                             struct hwr_viewport *viewport)
{
    uint32_t width = api->game_ram->u16(VA_G_RENDER_WIDTH);
    uint32_t height = api->game_ram->u16(VA_G_RENDER_HEIGHT);
    uint32_t pitch = api->game_ram->u32(VA_G_SCREEN_PITCH);
    uint32_t screen_height = api->game_ram->u32(VA_G_SCREEN_HEIGHT);
    uint32_t origin_x = api->game_ram->u32(VA_G_VIEW_X);
    uint64_t origin_y = api->game_ram->u32(VA_G_VIEW_Y);

    if (api->game_ram->u8(VA_G_LINE_DOUBLING_FLAG) != 0)
        origin_y *= 2u;
    if (width == 0 || height == 0 || pitch == 0 || screen_height == 0 ||
        origin_x >= pitch || origin_y >= screen_height)
        return 0;
    if (origin_x + width > pitch)
        width = pitch - origin_x;
    if (origin_y + height > screen_height)
        height = (uint32_t)(screen_height - origin_y);
    viewport->x = (int32_t)origin_x;
    viewport->y = (int32_t)origin_y;
    viewport->width = width;
    viewport->height = height;
    return width != 0 && height != 0;
}

/* The engine's linear back buffer is host-allocated outside the game_ram accessor range; the
 * engine and the host present path both write through this same stored pointer every frame, so
 * the guards here are structural (null, wrap-around, insane dimensions), not an address window. */
static int hwr_fill_sentinel(const struct roth_api_v1 *api,
                             const struct hwr_viewport *viewport)
{
    uint32_t pitch = api->game_ram->u32(VA_G_SCREEN_PITCH);
    uint32_t screen_height = api->game_ram->u32(VA_G_SCREEN_HEIGHT);
    uintptr_t framebuffer = (uintptr_t)api->game_ram->u32(VA_G_FRAMEBUFFER_PTR);
    uint64_t first_offset = (uint64_t)(uint32_t)viewport->y * pitch +
                            (uint32_t)viewport->x;
    uint64_t last_end_offset = (uint64_t)((uint32_t)viewport->y + viewport->height - 1u) * pitch +
                               (uint32_t)viewport->x + viewport->width;
    uint32_t row;

    if (framebuffer == 0 || pitch == 0 || pitch > 4096u || screen_height > 4096u ||
        first_offset > UINTPTR_MAX - framebuffer ||
        last_end_offset > UINTPTR_MAX - framebuffer)
        return 0;

    for (row = 0; row < viewport->height; row++) {
        uintptr_t address = framebuffer + first_offset + (uint64_t)row * pitch;
        memset((void *)address, 0, viewport->width);
    }
    return 1;
}

static void hwr_check_present_caps(const struct roth_api_v1 *api)
{
    struct roth_present_caps_v1 caps;
    int32_t result;

    if (g_present_caps_checked)
        return;
    g_present_caps_checked = 1;
    if (api->present == NULL)
        return;
    memset(&caps, 0, sizeof caps);
    caps.struct_size = sizeof caps;
    result = api->present->get_present_caps(api, &caps);
    if (result == ROTH_OK && (caps.formats & ROTH_PIXFMT_ARGB8888) != 0) {
        g_present_max_w = caps.max_w;
        g_present_max_h = caps.max_h;
        g_present_caps_valid = 1;
    } else {
        fprintf(stderr, "[renderer_overhaul] present caps unusable: rc=%d formats=0x%x\n",
                (int)result, caps.formats);
    }
}

static void ROTH_CDECL hwr_render_world_view_pass(struct roth_chain *chain,
                                                   const struct roth_api_v1 *api,
                                                   uint32_t eax_record)
{
    struct hwr_viewport viewport;
    struct roth_world_frame_v1 frame;
    const uint32_t *pixels;
    uint32_t render_width;
    uint32_t render_height;
    int32_t result;

    g_wrap_serial++;
    hwr_settings_apply_pending();

    if (!g_render_settings.enabled || g_gl_permanently_failed) {
        roth_next_render_world_view_pass(chain, eax_record);
        hwr_withdraw_if_owned(api);
        return;
    }
    if (api->game_ram->u8(VA_G_VIEW_SCALE_FLAGS) != 0) {
        roth_next_render_world_view_pass(chain, eax_record);
        hwr_withdraw_if_owned(api);
        if (!g_scale_mode_logged) {
            fprintf(stderr, "[renderer_overhaul] stock rendering retained for scaled viewport modes\n");
            g_scale_mode_logged = 1;
        }
        return;
    }
    if (api->present == NULL) {
        g_gl_permanently_failed = 1;
        roth_next_render_world_view_pass(chain, eax_record);
        if (!g_gl_error_logged) {
            fprintf(stderr, "[renderer_overhaul] full-color present channel is unavailable\n");
            g_gl_error_logged = 1;
        }
        return;
    }

    g_suppress_active = 1;
    roth_next_render_world_view_pass(chain, eax_record);
    g_suppress_active = 0;

    if (!hwr_read_viewport(api, &viewport)) {
        if (!g_viewport_error_logged) {
            fprintf(stderr, "[renderer_overhaul] world viewport rejected: render %ux%u origin %u,%u pitch=%u screen_h=%u\n",
                    api->game_ram->u16(VA_G_RENDER_WIDTH), api->game_ram->u16(VA_G_RENDER_HEIGHT),
                    api->game_ram->u32(VA_G_VIEW_X), api->game_ram->u32(VA_G_VIEW_Y),
                    api->game_ram->u32(VA_G_SCREEN_PITCH),
                    api->game_ram->u32(VA_G_SCREEN_HEIGHT));
            g_viewport_error_logged = 1;
        }
        hwr_withdraw_if_owned(api);
        return;
    }
    if (!hwr_fill_sentinel(api, &viewport)) {
        if (!g_sentinel_error_logged) {
            fprintf(stderr, "[renderer_overhaul] sentinel fill rejected: fb=0x%x arena=0x%x size=0x%x viewport=%d,%d %ux%u\n",
                    api->game_ram->u32(VA_G_FRAMEBUFFER_PTR),
                    (unsigned)(uintptr_t)api->game_ram->to_ptr(0),
                    api->game_ram->size(),
                    viewport.x, viewport.y, viewport.width, viewport.height);
            g_sentinel_error_logged = 1;
        }
        hwr_withdraw_if_owned(api);
        return;
    }
    if (!hwr_gl_init()) {
        hwr_fail_gl(api);
        return;
    }
    g_channel_api = api;   /* the world pass is the canonical channel-ownership identity */

    render_width = g_render_settings.internal_w != 0
                 ? g_render_settings.internal_w : viewport.width;
    render_height = g_render_settings.internal_h != 0
                  ? g_render_settings.internal_h : viewport.height;
    hwr_check_present_caps(api);
    if (g_present_caps_valid &&
        (render_width > g_present_max_w || render_height > g_present_max_h)) {
        hwr_withdraw_if_owned(api);
        return;
    }
    pixels = hwr_gl_render_frame(render_width, render_height,
                                 g_render_settings.solid_color);
    if (pixels == NULL) {
        hwr_fail_gl(api);
        return;
    }

    memset(&frame, 0, sizeof frame);
    frame.struct_size = sizeof frame;
    frame.pixels = pixels;
    frame.w = render_width;
    frame.h = render_height;
    frame.pitch_bytes = 0;
    frame.format = ROTH_PIXFMT_ARGB8888;
    frame.vx = viewport.x;
    frame.vy = viewport.y;
    frame.vw = viewport.width;
    frame.vh = viewport.height;
    frame.composite_mode = ROTH_COMPOSITE_ENGINE_COLORKEY_OVER_WORLD;
    frame.transparent_index = 0;
    frame.flags = 0;
    result = api->present->submit_world_frame(g_channel_api, &frame);
    if (result == ROTH_OK) {
        g_channel_owned = 1;
        g_last_frame = frame;
        g_last_frame_valid = 1;
    } else {
        if (!g_submit_error_logged) {
            fprintf(stderr, "[renderer_overhaul] world frame submit failed: rc=%d (%ux%u viewport %d,%d %ux%u)\n",
                    (int)result, frame.w, frame.h, frame.vx, frame.vy, frame.vw, frame.vh);
            g_submit_error_logged = 1;
        }
        if (result == ROTH_ERR_NOT_OWNER) {
            g_channel_owned = 0;
            g_last_frame_valid = 0;
        }
    }
}

/* Suppress every world-span leaf while inside our render wrap. Gating on the subpass byte alone
 * is not enough: portal, mirror, and door edge surfaces (the secondary-surface pass) reach the
 * wall driver with the subpass byte set non-zero, so a subpass-gated no-op lets those seam edges
 * paint through. The interaction-cursor classify pass runs in a separate frame stage outside the
 * wrap (suppress flag clear), so it still records its pick from the real leaf. */
static void ROTH_CDECL hwr_draw_world_surface_spans(struct roth_chain *chain,
                                                     const struct roth_api_v1 *api,
                                                     uint32_t ecx_entry,
                                                     uint32_t gs_base,
                                                     uint32_t es_base,
                                                     uint32_t fs_base)
{
    (void)api;
    if (g_suppress_active)
        return;
    roth_next_draw_world_surface_spans(chain, ecx_entry, gs_base, es_base, fs_base);
}

static void ROTH_CDECL hwr_draw_scaled_sprite_spans(struct roth_chain *chain,
                                                     const struct roth_api_v1 *api,
                                                     uint32_t esi,
                                                     uint32_t gs_base,
                                                     uint32_t es_base,
                                                     uint32_t fs_base,
                                                     uint16_t es_sel,
                                                     uint16_t fs_sel)
{
    (void)api;
    if (g_suppress_active)
        return;
    roth_next_draw_scaled_sprite_spans(chain, esi, gs_base, es_base, fs_base,
                                       es_sel, fs_sel);
}

static void ROTH_CDECL hwr_draw_floorceil_surface(struct roth_chain *chain,
                                                   const struct roth_api_v1 *api,
                                                   uint32_t esi,
                                                   uint32_t gs_base,
                                                   uint32_t fs_base,
                                                   uint32_t es_fb_base,
                                                   uint32_t blend_base,
                                                   uint16_t es_sel,
                                                   uint16_t fs_sel)
{
    (void)api;
    if (g_suppress_active)
        return;
    roth_next_draw_floorceil_surface(chain, esi, gs_base, fs_base, es_fb_base,
                                     blend_base, es_sel, fs_sel);
}

static void ROTH_CDECL hwr_render_world_sprite(struct roth_chain *chain,
                                                const struct roth_api_v1 *api,
                                                uint32_t esi_record,
                                                uint16_t es,
                                                uint16_t fs,
                                                uint16_t gs)
{
    (void)api;
    if (g_suppress_active)
        return;
    roth_next_render_world_sprite(chain, esi_record, es, fs, gs);
}

static void ROTH_CDECL hwr_on_load(const struct roth_api_v1 *api)
{
    const char *plugin_dir;
    g_api = api;
    plugin_dir = api->plugin_dir();
    if (plugin_dir == NULL)
        plugin_dir = "";
    snprintf(g_plugin_dir, sizeof g_plugin_dir, "%s", plugin_dir);
    hwr_settings_init_defaults();
    if (hwr_settings_register_apply(hwr_render_settings_apply, NULL) != 0 ||
        hwr_settings_register_apply(hwr_data_settings_apply, NULL) != 0 ||
        hwr_settings_register_apply(hwr_gl_settings_apply, NULL) != 0 ||
        hwr_settings_register_apply(hwr_window_settings_apply, NULL) != 0)
        fprintf(stderr, "[renderer_overhaul] settings apply-hook registration failed\n");
    hwr_load_config(api);
}

static void ROTH_CDECL hwr_on_frame_game(const struct roth_api_v1 *api)
{
    if (!g_first_frame_applied) {
        hwr_settings_apply_pending();
        g_first_frame_applied = 1;
    }
    hwr_apply_window_requests(api);

    if (g_wrap_serial != g_watchdog_serial) {
        g_watchdog_serial = g_wrap_serial;
        g_watchdog_stall_count = 0;
    } else {
        if (g_watchdog_stall_count < UINT32_MAX)
            g_watchdog_stall_count++;
        if (g_watchdog_stall_count >= 2)
            hwr_resubmit_if_stalled(api);
    }
}

static void ROTH_CDECL hwr_on_mouse_poll(const struct roth_api_v1 *api,
                                          struct roth_mouse_poll_v1 *poll)
{
    (void)poll;
    if (g_wrap_serial != g_poll_serial) {
        g_poll_serial = g_wrap_serial;
        g_poll_stall_count = 0;
        return;
    }
    if (g_poll_stall_count < UINT32_MAX)
        g_poll_stall_count++;
    /* Menus poll the mouse while the frame pump may idle, so this seam also carries the
     * frozen-frame re-submission; the throttle keeps the copied frames cheap. */
    if (g_poll_stall_count >= 4 && ((g_poll_throttle++) & 3u) == 0)
        hwr_resubmit_if_stalled(api);
}

static void ROTH_CDECL hwr_on_map_loaded(const struct roth_api_v1 *api,
                                          const struct roth_map_event_v1 *event)
{
    struct hwr_das_census map_census;
    struct hwr_das_census shared_census;
    char map_name[81];
    char shared_name[81];
    char root[2600];
    char map_path[2600];
    char shared_path[2600];
    char failure[256];

    hwr_withdraw_if_owned(api);
    hwr_settings_apply_pending();
    hwr_map_data_clear();
    failure[0] = 0;
    if (hwr_read_archive_basename(api, VA_G_MAP_DAS_NAME, map_name) != 0 ||
        hwr_read_archive_basename(api, VA_G_SHARED_DAS_NAME, shared_name) != 0) {
        snprintf(failure, sizeof failure, "archive name is unavailable");
        goto failed;
    }
    if (g_plugin_dir[0] == 0) {
        snprintf(failure, sizeof failure, "plugin directory was not captured");
        goto failed;
    }
    snprintf(root, sizeof root, "%s", g_plugin_dir);
    if (hwr_parent_path(root) != 0 || hwr_parent_path(root) != 0) {
        snprintf(failure, sizeof failure, "plugin directory has no game-root parent");
        goto failed;
    }
    if (hwr_find_archive_path(root, map_name, map_path, sizeof map_path) != 0 ||
        hwr_find_archive_path(root, shared_name, shared_path, sizeof shared_path) != 0) {
        snprintf(failure, sizeof failure, "cannot resolve DAS files '%s' and '%s'", map_name,
                 shared_name);
        goto failed;
    }
    if (hwr_das_open(&g_map_archive, map_path) != 0) {
        snprintf(failure, sizeof failure, "map DAS: %.220s", hwr_das_error(&g_map_archive));
        goto failed;
    }
    if (hwr_das_open(&g_shared_archive, shared_path) != 0) {
        snprintf(failure, sizeof failure, "shared DAS: %.217s",
                 hwr_das_error(&g_shared_archive));
        goto failed;
    }
    hwr_das_build_census(&g_map_archive, &map_census);
    hwr_das_build_census(&g_shared_archive, &shared_census);
    if (map_census.failures != 0 || shared_census.failures != 0) {
        snprintf(failure, sizeof failure, "DAS decode failures: map=%u shared=%u",
                 (unsigned)map_census.failures, (unsigned)shared_census.failures);
        goto failed;
    }
    if (hwr_world_snapshot(&g_world, api) != 0) {
        snprintf(failure, sizeof failure, "geometry: %.220s", hwr_world_error(&g_world));
        goto failed;
    }
    if (hwr_world_snapshot_luts(&g_world, api) != 0) {
        snprintf(failure, sizeof failure, "color tables: %.216s", hwr_world_error(&g_world));
        goto failed;
    }
    if (g_debug_enabled) {
        fprintf(stderr, "[renderer_overhaul] map data (gen=%u map=%s shared=%s)\n",
                event->generation, map_name, shared_name);
        hwr_world_summary(&g_world, stderr);
        hwr_print_das_summary("map", &g_map_archive, &map_census);
        hwr_print_das_summary("shared", &g_shared_archive, &shared_census);
        fprintf(stderr, "  color tables: palette=768 colormap=8192 tint=8192 glow=512 blend=65536 damage=%u shade=%u/%u\n",
                g_world.luts.damage_flash_level, g_world.luts.shade_level,
                g_world.luts.tint_shade_level);
    }
    return;

failed:
    fprintf(stderr, "[renderer_overhaul] map data unavailable (gen=%u): %s\n",
            event->generation, failure);
    hwr_map_data_clear();
}

static void ROTH_CDECL hwr_on_video_mode_changed(const struct roth_api_v1 *api,
                                                  const struct roth_video_event_v1 *event)
{
    static int logged;
    if (!logged) {
        fprintf(stderr, "[renderer_overhaul] video mode change (gen=%u %ux%u): world frame withdrawn until the next render\n",
                event->generation, event->pixel_w, event->pixel_h);
        logged = 1;
    }
    hwr_withdraw_if_owned(api);
}

static void ROTH_CDECL hwr_on_unload(const struct roth_api_v1 *api)
{
    hwr_withdraw_if_owned(api);
    hwr_map_data_clear();
    hwr_gl_shutdown();
    g_channel_api = NULL;
    g_api = NULL;
    g_plugin_dir[0] = 0;
}

static void ROTH_CDECL hwr_on_register_overrides(const struct roth_api_v1 *api,
                                                  struct roth_registrar_v1 *registrar)
{
    int failures = 0;
    (void)api;
    failures += roth_override(registrar, ROTH_FN_render_world_view_pass,
                              hwr_render_world_view_pass, 0) != 0;
    failures += roth_override(registrar, ROTH_FN_draw_world_surface_spans,
                              hwr_draw_world_surface_spans, 0) != 0;
    failures += roth_override(registrar, ROTH_FN_draw_scaled_sprite_spans,
                              hwr_draw_scaled_sprite_spans, 0) != 0;
    failures += roth_override(registrar, ROTH_FN_draw_floorceil_surface,
                              hwr_draw_floorceil_surface, 0) != 0;
    failures += roth_override(registrar, ROTH_FN_render_world_sprite,
                              hwr_render_world_sprite, 0) != 0;
    if (failures != 0)
        fprintf(stderr, "[renderer_overhaul] %d override registration(s) failed\n", failures);
}

static const struct roth_plugin_info_v1 RENDERER_OVERHAUL = {
    .abi_major = ROTH_ABI_MAJOR,
    .abi_minor = ROTH_ABI_MINOR,
    .struct_size = sizeof(struct roth_plugin_info_v1),
    .id = "ire.roth.renderer_overhaul",
    .name = "Renderer Overhaul",
    .version = "0.1.0",
    .sdk_req_major = ROTH_SDK_MAJOR,
    .sdk_req_minor = ROTH_SDK_MINOR,
    .api_use = ROTH_API_USE_GAME_RAM,
    .on_load = hwr_on_load,
    .on_frame_game = hwr_on_frame_game,
    .on_unload = hwr_on_unload,
    .on_register_overrides = hwr_on_register_overrides,
    .on_mouse_poll = hwr_on_mouse_poll,
    .on_map_loaded = hwr_on_map_loaded,
    .on_video_mode_changed = hwr_on_video_mode_changed
};

ROTH_PLUGIN_EXPORT const struct roth_plugin_info_v1 *ROTH_CDECL roth_plugin_query_v1(void)
{
    return &RENDERER_OVERHAUL;
}
