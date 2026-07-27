#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "roth_sdk.h"

#include "hwr_gl.h"
#include "hwr_backdrop.h"
#include "hwr_das.h"
#include "hwr_settings.h"
#include "hwr_scene.h"
#include "hwr_world.h"
#include "hwr_sprites.h"
#include "hwr_objects.h"
#include "hwr_perf.h"
#include "hwr_config.h"
#include "hwr_thumbnail.h"
#include "hwr_util.h"
#include "hwr_input.h"
#include "hwr_composite.h"

#include <signal.h>
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
#define VA_G_GDV_PALETTE_MODE          0x91de6u

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
static struct hwr_render_settings g_render_settings = { 1, 0x000000u, 0, 0 };
static struct hwr_settings g_window_settings;
static uint32_t g_window_pending;
static uint32_t g_wrap_serial;
static uint32_t g_watchdog_serial;
static uint32_t g_watchdog_stall_count;
static uint32_t g_poll_serial;
static uint32_t g_poll_stall_count;
static uint32_t g_poll_throttle;
/* The timer-driven mouse pump can nest a GAME callback inside this override.
 * Keep callbacks from re-submitting or reconfiguring buffers while a frame is
 * being built. */
static volatile sig_atomic_t g_world_render_active;
static struct roth_world_frame_v1 g_last_frame;
static int g_last_frame_valid;
static uint32_t g_present_max_w;
static uint32_t g_present_max_h;
static int g_present_caps_checked;
static int g_present_caps_valid;
static int g_channel_owned;
static struct hwr_input_route g_input_route;
static int g_input_route_active;
static int g_gdv_palette_effect_active;
static int g_suppress_active;
static int g_gl_permanently_failed;
static int g_gl_error_logged;
static int g_scale_mode_logged;
static int g_missing_window_logged;
static int g_viewport_error_logged;
static int g_sentinel_error_logged;
static int g_submit_error_logged;
static int g_internal_size_error_logged;
static int g_debug_enabled;
static int g_scene_rebuild_pending;
static int g_luts_recheck_pending;
static int g_scene_error_logged;
static int g_selfheal_retry;
static uint32_t g_selfheal_tick;
/* A map-load event only records a pending reload here; the geometry re-snapshot
 * runs at the render-dispatch safe point (see hwr_render_world_view_pass), never
 * inline from the event, so g_world's arrays can never be freed/reallocated
 * while hwr_scene_build_mesh is walking them.  Several events before the next
 * dispatch coalesce to the newest generation (an older staged map is superseded
 * by the newest load). */
static int g_map_reload_pending;
static uint32_t g_map_reload_generation;
/* Development aid: HWR_DUMP_FRAME=<path.ppm> periodically writes the submitted
 * world frame (atomically, via rename) for offline comparison against the
 * original renderer's output. */
static char g_dump_path[2600];
static uint32_t g_dump_counter;
/* Development aid: HWR_COMPARE=<path-prefix> renders BOTH ways each frame —
 * the original renderer draws normally (no suppression, no sentinel, no
 * submit) while the GL scene renders from the same live state — and
 * periodically writes <prefix>_soft.pgm / <prefix>_gl.pgm as raw palette
 * indices for pixel-exact offline comparison. */
static char g_compare_path[2560];
static uint32_t g_compare_counter;
static uint8_t *g_compare_buffer;
static char g_plugin_dir[2600];
static struct hwr_world g_world;
static struct hwr_das_archive g_map_archive;
static struct hwr_das_archive g_shared_archive;
static struct hwr_scene g_scene;
static struct hwr_backdrop g_backdrop;
static struct hwr_composite g_composite;
/* Raised when the backdrop still owes the index target its starting pixels. */
static int g_backdrop_seed_pending;
static int g_backdrop_error_logged;
/* Object data layer: the per-frame billboard enumeration, the pre-decoded sprite
 * census, and their controls. The layer produces no pixels yet; it stages the
 * data the billboard emitter will consume. HWR_OBJECTS=0 disables it entirely. */
static struct hwr_objects g_objects;
static struct hwr_sprite_census g_sprite_census;
static int g_objects_enabled = 1;
static int g_object_stats_pending;
static int g_billboard_stats_pending;
/* Development aid: HWR_OBJ_DUMP=<path> periodically writes the enumerated object
 * list (atomically, via rename) for offline inspection. */
static char g_obj_dump_path[2600];
static uint32_t g_obj_dump_counter;
/* Shared-archive (ADEMO) persistence. The shared DAS is byte-identical on every
 * map, so when a transition keeps the same shared basename the archive is left
 * open and decoded rather than torn down, re-opened, and re-decoded. Holds the
 * basename of the currently open shared archive (empty when none is open). */
static char g_shared_open_basename[81];

static void hwr_reload_map_data(const struct roth_api_v1 *api, uint32_t generation);

static void hwr_dump_frame(const uint32_t *pixels, uint32_t width, uint32_t height)
{
    char temp_path[2620];
    FILE *file;
    uint32_t i;
    snprintf(temp_path, sizeof temp_path, "%s.tmp", g_dump_path);
    file = fopen(temp_path, "wb");
    if (file == NULL)
        return;
    fprintf(file, "P6\n%u %u\n255\n", width, height);
    for (i = 0; i < width * height; i++) {
        uint8_t rgb[3];
        rgb[0] = (uint8_t)(pixels[i] >> 16);
        rgb[1] = (uint8_t)(pixels[i] >> 8);
        rgb[2] = (uint8_t)pixels[i];
        fwrite(rgb, 1, 3, file);
    }
    fclose(file);
    rename(temp_path, g_dump_path);
}

static void hwr_dump_pgm(const char *suffix, const uint8_t *bytes,
                         uint32_t width, uint32_t height)
{
    char path[2620];
    char temp_path[2640];
    FILE *file;
    snprintf(path, sizeof path, "%s_%s.pgm", g_compare_path, suffix);
    snprintf(temp_path, sizeof temp_path, "%s.tmp", path);
    file = fopen(temp_path, "wb");
    if (file == NULL)
        return;
    fprintf(file, "P5\n%u %u\n255\n", width, height);
    fwrite(bytes, 1, (size_t)width * height, file);
    fclose(file);
    rename(temp_path, path);
}

/* One atomic file per compare cycle: the soft, gl, and gl-texel planes stacked
 * vertically, with the cycle serial in a header comment so sweep tooling can
 * detect fresh sets. */
static void hwr_dump_compare_set(const uint8_t *soft, const uint8_t *gl,
                                 const uint8_t *gltex,
                                 uint32_t width, uint32_t height,
                                 uint32_t cycle)
{
    char path[2620];
    char temp_path[2640];
    FILE *file;
    snprintf(path, sizeof path, "%s_set.pgm", g_compare_path);
    snprintf(temp_path, sizeof temp_path, "%s.tmp", path);
    file = fopen(temp_path, "wb");
    if (file == NULL)
        return;
    fprintf(file, "P5\n# cycle %u\n%u %u\n255\n", cycle, width, height * 3u);
    fwrite(soft, 1, (size_t)width * height, file);
    fwrite(gl, 1, (size_t)width * height, file);
    fwrite(gltex, 1, (size_t)width * height, file);
    fclose(file);
    rename(temp_path, path);
}

/* Copies the software renderer's just-drawn viewport region (8bpp palette
 * indices) row by row from the engine's linear back buffer. */
static int hwr_copy_soft_indices(const struct roth_api_v1 *api,
                                 const struct hwr_viewport *viewport,
                                 uint8_t *out)
{
    uint32_t pitch = api->game_ram->u32(VA_G_SCREEN_PITCH);
    uint32_t screen_height = api->game_ram->u32(VA_G_SCREEN_HEIGHT);
    uintptr_t framebuffer = (uintptr_t)api->game_ram->u32(VA_G_FRAMEBUFFER_PTR);
    uint32_t row;
    if (framebuffer == 0 || pitch == 0 || pitch > 4096u || screen_height > 4096u)
        return 0;
    for (row = 0; row < viewport->height; row++) {
        uintptr_t address = framebuffer +
            ((uint64_t)((uint32_t)viewport->y + row)) * pitch +
            (uint32_t)viewport->x;
        memcpy(out + (size_t)row * viewport->width, (const void *)address,
               viewport->width);
    }
    return 1;
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

/* HWR_ATLAS_PERSIST=0 forces the shared archive + shared sprite atlas to be
 * rebuilt on every map transition (the pre-persistence behaviour). */
static int hwr_atlas_persist_enabled(void)
{
    static int state = -1;
    if (state < 0) {
        const char *setting = getenv("HWR_ATLAS_PERSIST");
        state = setting != NULL && setting[0] == '0' ? 0 : 1;
    }
    return state;
}

static void hwr_map_data_clear(int keep_shared)
{
    hwr_objects_clear(&g_objects);
    hwr_objects_set_enabled(&g_objects, g_objects_enabled);
    memset(&g_sprite_census, 0, sizeof g_sprite_census);
    hwr_world_clear(&g_world);
    hwr_das_close(&g_map_archive);
    /* The shared archive is closed only when it is not being carried across the
     * transition; keeping it open preserves its decoded payloads (which the
     * per-frame sprite emitter re-dereferences) and its packed atlas. */
    if (!keep_shared) {
        hwr_das_close(&g_shared_archive);
        g_shared_open_basename[0] = 0;
    }
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

/* Game content sits in fixed subdirectories of the game root, but the case of
 * both the directory entries and the names the engine asks for varies between
 * installs, so an exact join is tried first and the directory is scanned
 * case-insensitively when that misses. */
static int hwr_find_game_file(const char *root, const char *subdirectory,
                              const char *name, char *path, size_t capacity)
{
    char directory[2600];
    if (hwr_path_join(directory, sizeof directory, root, subdirectory) != 0 ||
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

static int hwr_find_archive_path(const char *root, const char *name,
                                 char *path, size_t capacity)
{
    return hwr_find_game_file(root, "M", name, path, capacity);
}

static void hwr_gl_settings_apply(uint32_t mask,
                                  const struct hwr_settings *settings,
                                  void *userdata)
{
    (void)settings;
    (void)userdata;
    if (mask & HWR_CHANGE_INTERNAL_SIZE)
        g_last_frame_valid = 0;   /* the cached frame references the old readback buffer */
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
    g_gdv_palette_effect_active = 0;
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

/* Hands the index target the slice of the backdrop the viewport sits on, so the
 * first world frame is drawn over that art instead of over a flat colour. The
 * engine scales the image across the whole screen, so the slice depends on where
 * the viewport falls within it. */
static void hwr_submit_backdrop_seed(const struct roth_api_v1 *api,
                                     const struct hwr_viewport *viewport,
                                     uint32_t output_width,
                                     uint32_t output_height)
{
    uint32_t screen_width = api->game_ram->u32(VA_G_SCREEN_PITCH);
    uint32_t screen_height = api->game_ram->u32(VA_G_SCREEN_HEIGHT);
    size_t plane = (size_t)output_width * output_height;
    uint8_t *indices;

    g_backdrop_seed_pending = 0;
    if (!g_backdrop.valid || screen_width == 0 || screen_height == 0)
        return;
    indices = (uint8_t *)malloc(plane);
    if (indices == NULL)
        return;
    if (hwr_backdrop_view_indices(&g_backdrop, screen_width, screen_height,
                                  (uint32_t)viewport->x, (uint32_t)viewport->y,
                                  viewport->width, viewport->height,
                                  output_width, output_height, indices) == 0)
        hwr_gl_set_index_seed(indices, output_width, output_height);
    free(indices);
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

/* Rebuilds the per-frame billboard enumeration from live state, dumps it on a
 * throttle when HWR_OBJ_DUMP is set, and emits the object-layer stats line once
 * after each map load. Runs at the render-dispatch safe point, after the world
 * refresh, so the objects and geometry are drawn from one consistent frame. */
static void hwr_run_object_layer(const struct roth_api_v1 *api)
{
    if (!g_objects_enabled)
        return;
    if (hwr_objects_rebuild(&g_objects, &g_world, api) != 0) {
        if (g_debug_enabled)
            fprintf(stderr, "[renderer_overhaul] object enumeration unavailable: %s\n",
                    g_objects.error);
        return;
    }
    if (g_object_stats_pending && g_debug_enabled) {
        fprintf(stderr,
                "[renderer_overhaul] object layer: objects=%zu sprite-textures=%zu sprite-frames=%zu decode-fails=%zu\n",
                g_objects.count,
                g_sprite_census.plain + g_sprite_census.grouped + g_sprite_census.animated,
                g_sprite_census.sprite_frames, g_sprite_census.failures);
        g_object_stats_pending = 0;
    }
    if (g_obj_dump_path[0] != 0 && ((g_obj_dump_counter++) % 30u) == 0)
        hwr_objects_dump(&g_objects, g_obj_dump_path, api, &g_map_archive,
                         &g_shared_archive);
}

static void hwr_render_world_view_pass_impl(struct roth_chain *chain,
                                            const struct roth_api_v1 *api,
                                            uint32_t eax_record)
{
    struct hwr_viewport viewport;
    struct roth_world_frame_v1 frame;
    struct hwr_frame_perf_sample frame_perf;
    const uint32_t *pixels;
    uint32_t render_width;
    uint32_t render_height;
    int32_t result;
    int world_was_valid;
    int refresh_result;

    hwr_frame_perf_begin(&frame_perf);

    /* File I/O and settings application belong at this non-reentrant render
     * boundary, not in the timer-driven per-frame callback. */
    hwr_config_poll();
    hwr_settings_apply_pending();
    hwr_apply_window_requests(api);
    g_wrap_serial++;
    hwr_frame_perf_mark(&frame_perf, HWR_FRAME_PERF_PREFLIGHT);

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

    if (g_compare_path[0] == 0)
        g_suppress_active = 1;
    roth_next_render_world_view_pass(chain, eax_record);
    g_suppress_active = 0;
    hwr_frame_perf_mark(&frame_perf, HWR_FRAME_PERF_ENGINE_WALK);

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
    if (!hwr_gl_init()) {
        hwr_fail_gl(api);
        return;
    }
    g_channel_api = api;   /* the world pass is the canonical channel-ownership identity */

    render_width = viewport.width;
    render_height = viewport.height;
    if (g_compare_path[0] == 0) {
        if (g_render_settings.internal_w != 0)
            render_width = g_render_settings.internal_w;
        if (g_render_settings.internal_h != 0)
            render_height = g_render_settings.internal_h;
        if (render_width < 2u || render_height < 2u) {
            if (!g_internal_size_error_logged) {
                fprintf(stderr,
                        "[renderer_overhaul] internal size %ux%u is too small; using %ux%u\n",
                        render_width, render_height,
                        viewport.width, viewport.height);
                g_internal_size_error_logged = 1;
            }
            render_width = viewport.width;
            render_height = viewport.height;
        } else {
            g_internal_size_error_logged = 0;
        }
    }
    hwr_check_present_caps(api);
    if (g_present_caps_valid &&
        (render_width > g_present_max_w || render_height > g_present_max_h)) {
        hwr_withdraw_if_owned(api);
        return;
    }
    if (g_last_frame_valid &&
        (g_last_frame.w != render_width || g_last_frame.h != render_height))
        g_last_frame_valid = 0;
    /* Safe point for a deferred map reload: nothing consumes g_world yet on this
     * dispatch, and (single game thread) no other dispatch's mesh build is in
     * flight, so re-snapshotting here cannot free geometry under
     * hwr_scene_build_mesh.  It runs before the refresh/render below, so the
     * first frame after a load still renders the new map -- or fails closed
     * through the same invalid-snapshot path as an inline reload would have. */
    if (g_map_reload_pending) {
        g_map_reload_pending = 0;
        hwr_reload_map_data(api, g_map_reload_generation);
    }
    if (g_backdrop_seed_pending)
        hwr_submit_backdrop_seed(api, &viewport,
                                 render_width, render_height);
    world_was_valid = g_world.valid;
    refresh_result = g_world.valid ? hwr_world_refresh(&g_world, api) : -1;
    if (refresh_result >= 0 &&
        hwr_world_refresh_lut_state(&g_world, api) == 0) {
        if ((refresh_result & HWR_WORLD_REFRESH_TEXTURES) != 0) {
            /* RAW texture commands have already settled the live geometry
             * records during roth_next. Extend the resident texture union
             * from those ids rather than invoking the remap hook again: that
             * hook may execute commands. Previously seen variants remain
             * resident, so repeated animation does not rebuild every tick. */
            g_scene_rebuild_pending = 1;
        }
        /* Enumerate this frame's billboards from the just-refreshed live state.
         * The layer stages data only; nothing consumes it in the render below
         * yet, so it never affects the current output. */
        hwr_run_object_layer(api);
        /* The engine finishes repointing its color tables after the load
         * event fires; the first staged frame is the earliest safe moment to
         * confirm the snapshot saw the final tables. */
        if (g_luts_recheck_pending) {
            int luts_changed = hwr_world_resnapshot_luts(&g_world, api);
            g_luts_recheck_pending = 0;
            if (luts_changed > 0) {
                if (g_debug_enabled)
                    fprintf(stderr, "[renderer_overhaul] color tables settled after load; re-uploading\n");
                g_scene_rebuild_pending = 1;
            }
        }
        if (g_scene_rebuild_pending) {
            int rebuild_result;
            unsigned upload_errors;
            /* The host's timer signal may interrupt driver calls mid-upload and
             * leave textures silently empty; keep it out of the one-time scene
             * upload, and verify the driver reported no errors afterwards. */
#ifndef _WIN32
            sigset_t block_set, old_set;
            sigemptyset(&block_set);
            sigaddset(&block_set, SIGALRM);
            sigprocmask(SIG_BLOCK, &block_set, &old_set);
#endif
            hwr_gl_drain_errors();
            rebuild_result = hwr_scene_rebuild(&g_scene, &g_world, &g_map_archive,
                                               &g_shared_archive, &g_objects, api,
                                               g_debug_enabled);
            upload_errors = hwr_gl_drain_errors();
#ifndef _WIN32
            sigprocmask(SIG_SETMASK, &old_set, NULL);
#endif
            if (rebuild_result != 0) {
                if (!g_scene_error_logged) {
                    fprintf(stderr, "[renderer_overhaul] GL scene unavailable: %s; using solid fallback\n",
                            hwr_scene_error(&g_scene));
                    g_scene_error_logged = 1;
                }
                g_scene_rebuild_pending = 0;
            } else if (upload_errors != 0) {
                fprintf(stderr, "[renderer_overhaul] scene upload reported %u driver error(s); retrying\n",
                        upload_errors);
                hwr_scene_clear(&g_scene);
            } else {
                g_scene_rebuild_pending = 0;
            }
        }
        hwr_frame_perf_mark(&frame_perf, HWR_FRAME_PERF_PREPARE);
        pixels = g_scene.valid
               ? hwr_scene_render(&g_scene, &g_world, &g_objects, api,
                                  render_width, render_height,
                                  viewport.width, viewport.height)
               : NULL;
        if (pixels == NULL && g_scene.valid && !g_scene_error_logged) {
            fprintf(stderr, "[renderer_overhaul] scene render failed: %s; using solid fallback\n",
                    hwr_gl_last_error());
            g_scene_error_logged = 1;
        }
        if (g_billboard_stats_pending && g_debug_enabled && pixels != NULL &&
            g_scene.valid) {
            fprintf(stderr,
                    "[renderer_overhaul] billboards=%zu/%zu skipped-mesh=%zu\n",
                    g_scene.billboards.stat_emitted,
                    g_scene.billboards.stat_enumerated,
                    g_scene.billboards.stat_skipped_mesh);
            g_billboard_stats_pending = 0;
        }
    } else {
        pixels = NULL;
        if (g_scene_rebuild_pending) {
            hwr_scene_clear(&g_scene);
            g_scene_rebuild_pending = 0;
        }
        /* Self-heal: some in-gameplay map transitions replace the geometry
         * sections without a detectable load boundary, so the refresh guard is
         * the only signal. The wrap runs only on fully staged frames, so an
         * immediate reload can succeed; keep a slow retry in case it did not. */
        if (world_was_valid ||
            (g_selfheal_retry && ++g_selfheal_tick >= 60)) {
            g_selfheal_tick = 0;
            hwr_reload_map_data(api, 0);
            g_selfheal_retry = !g_world.valid;
        }
        hwr_frame_perf_mark(&frame_perf, HWR_FRAME_PERF_PREPARE);
    }
    if (g_compare_path[0] == 0 && pixels == NULL)
        pixels = hwr_gl_render_frame(render_width, render_height,
                                     g_render_settings.solid_color);
    hwr_frame_perf_mark(&frame_perf, HWR_FRAME_PERF_RENDER);
    if (g_compare_path[0] != 0) {
        /* Compare mode: the software world is on screen; pair its indices
         * with the GL index attachment (plus a raw-texel plane and the LUTs
         * for shade-vs-texel attribution) and never submit. */
        if (pixels != NULL && g_scene.valid &&
            ((g_compare_counter++) & 15u) == 0) {
            size_t plane = (size_t)render_width * render_height;
            uint8_t *buffer = (uint8_t *)realloc(g_compare_buffer, plane * 3u);
            if (buffer != NULL) {
                uint8_t *soft = buffer;
                uint8_t *gl = buffer + plane;
                uint8_t *gltex = buffer + plane * 2u;
                int step = 0;
                g_compare_buffer = buffer;
                if (hwr_copy_soft_indices(api, &viewport, soft)) {
                    step = 1;
                    if (hwr_gl_read_scene_indices(gl, render_width, render_height)) {
                        step = 2;
                        /* The third plane normally carries raw texels; a
                         * numeric HWR_SHADE_VIS instead captures that probe,
                         * so surface identity can be read without a palette
                         * round trip. */
                        {
                            const char *probe = getenv("HWR_SHADE_VIS");
                            int probe_mode = probe != NULL && probe[0] > '0' &&
                                             probe[0] <= '9' ? probe[0] - '0' : 2;
                            hwr_gl_set_debug_vis(probe_mode);
                        }
                        if (hwr_scene_render(&g_scene, &g_world, &g_objects, api,
                                             render_width, render_height,
                                             viewport.width,
                                             viewport.height) != NULL) {
                            step = 3;
                            if (hwr_gl_read_scene_indices(gltex, render_width,
                                                          render_height))
                                step = 4;
                        }
                        hwr_gl_set_debug_vis(0);
                    }
                }
                if (step == 4) {
                    size_t nz_gl = 0;
                    size_t nz_tex = 0;
                    size_t nz_rgb = 0;
                    size_t i;
                    for (i = 0; i < plane; i++) {
                        nz_gl += gl[i] != 0;
                        nz_tex += gltex[i] != 0;
                        nz_rgb += (pixels[i] & 0xffffffu) != 0;
                    }
                    fprintf(stderr, "[renderer_overhaul] compare cycle %u gl_nz=%zu tex_nz=%zu rgb_nz=%zu\n",
                            g_compare_counter, nz_gl, nz_tex, nz_rgb);
                    hwr_dump_compare_set(soft, gl, gltex, render_width,
                                         render_height, g_compare_counter);
                    hwr_dump_pgm("cmap", g_world.luts.colormap, 256, 32);
                    hwr_dump_pgm("tint", g_world.luts.tint, 256, 32);
                } else {
                    fprintf(stderr, "[renderer_overhaul] compare cycle %u stalled at step %d\n",
                            g_compare_counter, step);
                }
            }
        }
        hwr_withdraw_if_owned(api);
        return;
    }
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
    frame.composite_mode =
        (uint64_t)render_width * (uint64_t)viewport.height !=
        (uint64_t)render_height * (uint64_t)viewport.width
            ? ROTH_COMPOSITE_ENGINE_CENTERED_OVER_CANVAS
            : ROTH_COMPOSITE_ENGINE_COLORKEY_OVER_WORLD;
    frame.transparent_index = 0;
    frame.flags = 0;
    g_gdv_palette_effect_active = 0;
    if (!hwr_composite_prepare(&g_composite, api, &frame, &g_world)) {
        /* ABI 0.6 hosts and exceptional allocation/mapping failures retain the
         * original index-0 colorkey path as a fail-safe. */
        frame.engine_base_pixels = NULL;
        frame.engine_base_pitch_bytes = 0;
        if (!hwr_fill_sentinel(api, &viewport)) {
            if (!g_sentinel_error_logged) {
                fprintf(stderr, "[renderer_overhaul] engine baseline and sentinel fallback rejected: fb=0x%x arena=0x%x size=0x%x viewport=%d,%d %ux%u\n",
                        api->game_ram->u32(VA_G_FRAMEBUFFER_PTR),
                        (unsigned)(uintptr_t)api->game_ram->to_ptr(0),
                        api->game_ram->size(),
                        viewport.x, viewport.y,
                        viewport.width, viewport.height);
                g_sentinel_error_logged = 1;
            }
            hwr_withdraw_if_owned(api);
            return;
        }
    }
    hwr_frame_perf_mark(&frame_perf, HWR_FRAME_PERF_COMPOSITE);
    {
        static uint64_t submit_sum;
        static uint64_t cadence_sum;
        static uint64_t previous_done;
        static unsigned submit_samples;
        uint64_t submit_start = getenv("HWR_FRAME_PERF") != NULL
                              ? hwr_perf_now_ns() : 0;
        uint64_t now;
        result = api->present->submit_world_frame(g_channel_api, &frame);
        hwr_frame_perf_mark(&frame_perf, HWR_FRAME_PERF_SUBMIT);
        if (submit_start != 0) {
            now = hwr_perf_now_ns();
            submit_sum += now - submit_start;
            if (previous_done != 0)
                cadence_sum += now - previous_done;
            previous_done = now;
            submit_samples++;
            if (submit_samples >= 60u) {
                fprintf(stderr,
                        "[renderer_overhaul] frame transport avg/%u:"
                        " host-copy=%.2fms cadence=%.2fms\n",
                        submit_samples,
                        (double)submit_sum /
                            ((double)submit_samples * 1.0e6),
                        (double)cadence_sum /
                            ((double)(submit_samples - 1u) * 1.0e6));
                submit_sum = 0;
                cadence_sum = 0;
                previous_done = 0;
                submit_samples = 0;
            }
        }
    }
    if (result == ROTH_OK) {
        g_channel_owned = 1;
        g_last_frame = frame;
        g_last_frame_valid = 1;
        if (g_dump_path[0] != 0 && ((g_dump_counter++) & 31u) == 0)
            hwr_dump_frame(pixels, render_width, render_height);
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
    hwr_frame_perf_mark(&frame_perf, HWR_FRAME_PERF_FINALIZE);
    hwr_frame_perf_finish(&frame_perf);
}

static void ROTH_CDECL hwr_render_world_view_pass(struct roth_chain *chain,
                                                   const struct roth_api_v1 *api,
                                                   uint32_t eax_record)
{
    g_world_render_active = 1;
    hwr_render_world_view_pass_impl(chain, api, eax_record);
    g_world_render_active = 0;
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

static void ROTH_CDECL hwr_capture_screen_thumbnail(
    struct roth_chain *chain, const struct roth_api_v1 *api,
    uint32_t dst, uint32_t src, uint32_t lut)
{
    if (g_last_frame_valid &&
        hwr_thumbnail_capture(api, &g_last_frame, &g_world, dst, src, lut))
        return;
    roth_next_capture_screen_thumbnail(chain, dst, src, lut);
}

static void ROTH_CDECL hwr_grayscale_background_view(
    struct roth_chain *chain, const struct roth_api_v1 *api)
{
    int32_t result;

    /* This builds the authentic engine LUT and remaps the baseline plus all
     * already-drawn 8bpp chrome. The hardware world is transformed only after
     * that succeeds, preserving the preceding color-thumbnail capture. */
    roth_next_grayscale_background_view(chain);
    if (!g_last_frame_valid || g_channel_api == NULL ||
        api->present == NULL)
        return;
    if (!hwr_composite_grayscale(
            &g_composite, api, &g_last_frame, &g_world)) {
        /* The authentic call has already remapped the engine framebuffer.
         * Reveal that safe 8bpp fallback instead of leaving a color RGBA
         * frame underneath grayscale chrome. */
        hwr_withdraw_if_owned(api);
        return;
    }
    result = api->present->submit_world_frame(g_channel_api, &g_last_frame);
    if (result == ROTH_OK) {
        g_channel_owned = 1;
    } else {
        if (result != ROTH_ERR_NOT_OWNER)
            fprintf(stderr,
                    "[renderer_overhaul] grayscale world submit failed: rc=%d\n",
                    (int)result);
        hwr_withdraw_if_owned(api);
    }
}

/* The centered 4:3 engine layer and the wide world occupy the same physical
 * canvas but use different coordinate systems. Gameplay UI sees coordinates
 * local to the centered engine layer; each nested world-pick pass temporarily
 * restores the full-canvas position and the matching wide projection. */
static uint32_t ROTH_CDECL hwr_render_world_view(
    struct roth_chain *chain, const struct roth_api_v1 *api,
    uint16_t es, uint16_t fs, uint16_t gs)
{
    struct hwr_pick_projection saved;
    uint32_t result;
    int projection_changed;

    if (!g_input_route_active)
        return roth_next_render_world_view(chain, es, fs, gs);
    hwr_input_set_world_mouse(api, &g_input_route);
    projection_changed = hwr_input_begin_pick_projection(
        api, &g_input_route, &saved);
    result = roth_next_render_world_view(chain, es, fs, gs);
    if (projection_changed)
        hwr_input_end_pick_projection(api, &saved);
    hwr_input_set_hud_mouse(api, &g_input_route);
    return result;
}

static void ROTH_CDECL hwr_run_gameplay_frame(
    struct roth_chain *chain, const struct roth_api_v1 *api)
{
    int centered;

    if (!g_render_settings.enabled || !g_last_frame_valid) {
        roth_next_run_gameplay_frame(chain);
        return;
    }
    centered =
        g_last_frame.composite_mode ==
        ROTH_COMPOSITE_ENGINE_CENTERED_OVER_CANVAS;
    if (!hwr_input_prepare_route(
            api, g_last_frame.w, g_last_frame.h, centered,
            hwr_settings_get()->fov_deg, &g_input_route)) {
        roth_next_run_gameplay_frame(chain);
        return;
    }
    g_input_route_active = 1;
    hwr_input_set_hud_mouse(api, &g_input_route);
    roth_next_run_gameplay_frame(chain);
    hwr_input_restore_world_cursor(api, &g_input_route);
    g_input_route_active = 0;
}

/* Inventory and dialogue run outside run_gameplay_frame, but their hit tests
 * still consume engine-layer coordinates. Keep the published cursor in
 * full-canvas space and localize only the authentic hit-test call. */
static void ROTH_CDECL hwr_handle_cursor_click(
    struct roth_chain *chain, const struct roth_api_v1 *api)
{
    struct hwr_input_route route;

    if (!g_render_settings.enabled || !g_last_frame_valid ||
        g_last_frame.composite_mode !=
            ROTH_COMPOSITE_ENGINE_CENTERED_OVER_CANVAS ||
        !hwr_input_prepare_ui_route(
            api, g_last_frame.w, g_last_frame.h, 1, &route)) {
        roth_next_handle_cursor_click(chain);
        return;
    }
    hwr_input_set_hud_mouse(api, &route);
    roth_next_handle_cursor_click(chain);
    hwr_input_restore_world_cursor(api, &route);
}

/* Apply the exact brightness multiplier the GDV fade has just written to the
 * VGA palette. Returns one when a frame was submitted, zero when no hardware
 * frame is active, and minus one after falling back to authentic 8bpp. */
static int hwr_submit_gdv_brightness(
    const struct roth_api_v1 *api, uint32_t multiplier)
{
    int32_t result;

    if (!g_last_frame_valid || g_channel_api == NULL ||
        api->present == NULL)
        return 0;
    if (!hwr_composite_apply_brightness(
            &g_composite, &g_last_frame, multiplier)) {
        hwr_withdraw_if_owned(api);
        return -1;
    }
    result = api->present->submit_world_frame(g_channel_api, &g_last_frame);
    if (result != ROTH_OK) {
        if (result != ROTH_ERR_NOT_OWNER)
            fprintf(stderr,
                    "[renderer_overhaul] palette-effect submit failed: rc=%d\n",
                    (int)result);
        hwr_withdraw_if_owned(api);
        return -1;
    }
    g_channel_owned = 1;
    return 1;
}

static void ROTH_CDECL hwr_gdv_clear_vga_palette(
    struct roth_chain *chain, const struct roth_api_v1 *api)
{
    int applied;

    roth_next_gdv_clear_vga_palette(chain);
    if (api->game_ram->u8(VA_G_GDV_PALETTE_MODE) != 1)
        return;
    applied = hwr_submit_gdv_brightness(api, 0);
    g_gdv_palette_effect_active = applied > 0;
}

static void ROTH_CDECL hwr_gdv_write_scaled_palette(
    struct roth_chain *chain, const struct roth_api_v1 *api, uint32_t level)
{
    int applied;

    roth_next_gdv_write_scaled_palette(chain, level);
    if (api->game_ram->u8(VA_G_GDV_PALETTE_MODE) != 1)
        return;
    applied = hwr_submit_gdv_brightness(
        api, (level << 2) & 0xffffu);
    g_gdv_palette_effect_active = applied > 0;
}

static void ROTH_CDECL hwr_upload_palette_dac(
    struct roth_chain *chain, const struct roth_api_v1 *api)
{
    roth_next_upload_palette_dac(chain);
    if (!g_gdv_palette_effect_active)
        return;
    hwr_submit_gdv_brightness(api, 0x100u);
    g_gdv_palette_effect_active = 0;
}

static void ROTH_CDECL hwr_on_load(const struct roth_api_v1 *api)
{
    const char *plugin_dir;
    g_api = api;
    plugin_dir = api->plugin_dir();
    if (plugin_dir == NULL)
        plugin_dir = "";
    snprintf(g_plugin_dir, sizeof g_plugin_dir, "%s", plugin_dir);
    {
        const char *dump = getenv("HWR_DUMP_FRAME");
        const char *compare = getenv("HWR_COMPARE");
        const char *objects = getenv("HWR_OBJECTS");
        const char *obj_dump = getenv("HWR_OBJ_DUMP");
        if (dump != NULL)
            snprintf(g_dump_path, sizeof g_dump_path, "%s", dump);
        if (compare != NULL)
            snprintf(g_compare_path, sizeof g_compare_path, "%s", compare);
        g_objects_enabled = !(objects != NULL && objects[0] == '0');
        if (obj_dump != NULL)
            snprintf(g_obj_dump_path, sizeof g_obj_dump_path, "%s", obj_dump);
    }
    hwr_objects_set_enabled(&g_objects, g_objects_enabled);
    hwr_settings_init_defaults();
    if (hwr_settings_register_apply(hwr_render_settings_apply, NULL) != 0 ||
        hwr_settings_register_apply(hwr_data_settings_apply, NULL) != 0 ||
        hwr_settings_register_apply(hwr_gl_settings_apply, NULL) != 0 ||
        hwr_settings_register_apply(hwr_window_settings_apply, NULL) != 0)
        fprintf(stderr, "[renderer_overhaul] settings apply-hook registration failed\n");
    hwr_config_load(g_plugin_dir);
}

static void ROTH_CDECL hwr_on_frame_game(const struct roth_api_v1 *api)
{
    if (g_world_render_active)
        return;
    hwr_settings_apply_pending();
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
    if (g_world_render_active)
        return;
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

/* (Re)loads every mod-side map dependency: archives, geometry snapshot, colour
 * tables. Called from the map notification AND from the render wrap's self-heal
 * path — some in-gameplay map transitions replace the geometry sections without
 * a detectable load boundary, and the per-frame refresh guard is what notices. */
static void hwr_reload_map_data(const struct roth_api_v1 *api, uint32_t generation)
{
    char map_name[81];
    char shared_name[81];
    char root[2600];
    char map_path[2600];
    char shared_path[2600];
    char failure[256];
    int keep_shared = 0;

    hwr_perf_begin(generation);
    hwr_settings_apply_pending();
    failure[0] = 0;
    /* Read the archive names before any teardown so the shared-archive
     * persistence decision can be taken while the old archive is still open. */
    if (hwr_read_archive_basename(api, VA_G_MAP_DAS_NAME, map_name) != 0 ||
        hwr_read_archive_basename(api, VA_G_SHARED_DAS_NAME, shared_name) != 0) {
        snprintf(failure, sizeof failure, "archive name is unavailable");
        goto failed;
    }
    keep_shared = hwr_atlas_persist_enabled() &&
                  g_shared_archive.entries != NULL &&
                  g_shared_open_basename[0] != 0 &&
                  strcmp(shared_name, g_shared_open_basename) == 0;
    hwr_map_data_clear(keep_shared);
    hwr_perf_phase("clear");
    g_scene_rebuild_pending = 1;
    g_scene_error_logged = 0;
    g_backdrop_seed_pending = 0;
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
    /* The backdrop is optional decoration: a missing or unreadable file costs
     * only the seed, so it never fails the map load. */
    if (hwr_backdrop_enabled() && !g_backdrop.valid) {
        char backdrop_path[2600];
        if (hwr_find_game_file(root, "DATA", "BACKDROP.RAW", backdrop_path,
                               sizeof backdrop_path) != 0) {
            if (!g_backdrop_error_logged) {
                fprintf(stderr, "[renderer_overhaul] backdrop not found under '%s/DATA'; void starts blank\n",
                        root);
                g_backdrop_error_logged = 1;
            }
        } else if (hwr_backdrop_load(&g_backdrop, backdrop_path) != 0) {
            if (!g_backdrop_error_logged) {
                fprintf(stderr, "[renderer_overhaul] backdrop unusable: %s; void starts blank\n",
                        hwr_backdrop_error(&g_backdrop));
                g_backdrop_error_logged = 1;
            }
        }
    }
    g_backdrop_seed_pending = g_backdrop.valid;

    if (hwr_das_open(&g_map_archive, map_path) != 0) {
        snprintf(failure, sizeof failure, "map DAS: %.220s", hwr_das_error(&g_map_archive));
        goto failed;
    }
    /* When the shared archive is being carried across the transition it is
     * already open with the matching basename; only a fresh (or changed) shared
     * archive is (re)opened here. */
    if (!keep_shared) {
        if (hwr_das_open(&g_shared_archive, shared_path) != 0) {
            snprintf(failure, sizeof failure, "shared DAS: %.217s",
                     hwr_das_error(&g_shared_archive));
            goto failed;
        }
        snprintf(g_shared_open_basename, sizeof g_shared_open_basename, "%s",
                 shared_name);
    }
    hwr_perf_phase("open");
    if (hwr_world_snapshot(&g_world, api) != 0) {
        snprintf(failure, sizeof failure, "geometry: %.220s", hwr_world_error(&g_world));
        goto failed;
    }
    if (hwr_world_snapshot_luts(&g_world, api) != 0) {
        snprintf(failure, sizeof failure, "color tables: %.216s", hwr_world_error(&g_world));
        goto failed;
    }
    hwr_perf_phase("geometry");
    /* Pre-decode every sprite frame both archives hold: all of the shared sprite
     * set, and the per-map archive's object-graphic tail (ids at and above the
     * bias). The census feeds the object-layer stats line. Decoding is idempotent
     * with the scene upload's own lazy decode. */
    memset(&g_sprite_census, 0, sizeof g_sprite_census);
    if (g_objects_enabled) {
        hwr_sprites_predecode(&g_shared_archive, 0, &g_sprite_census);
        hwr_perf_phase("predecode-shared");
        hwr_sprites_predecode(&g_map_archive, HWR_SPRITE_ID_BIAS, &g_sprite_census);
        hwr_perf_phase("predecode-map");
        g_object_stats_pending = 1;
        g_billboard_stats_pending = 1;
    }
    if (g_debug_enabled) {
        fprintf(stderr, "[renderer_overhaul] map data (gen=%u map=%s shared=%s)\n",
                generation, map_name, shared_name);
        hwr_world_summary(&g_world, stderr);
        fprintf(stderr, "  archives: map_entries=%u shared_entries=%u (referenced entries decode on scene upload)\n",
                (unsigned)g_map_archive.entry_count,
                (unsigned)g_shared_archive.entry_count);
        fprintf(stderr, "  color tables: palette=768 colormap=8192 tint=8192 glow=512 blend=65536 damage=%u shade=%u/%u\n",
                g_world.luts.damage_flash_level, g_world.luts.shade_level,
                g_world.luts.tint_shade_level);
    }
    g_luts_recheck_pending = 1;
    return;

failed:
    fprintf(stderr, "[renderer_overhaul] map data unavailable (gen=%u): %s\n",
            generation, failure);
    hwr_map_data_clear(0);
    g_scene_rebuild_pending = 1;
}

static void ROTH_CDECL hwr_on_map_loaded(const struct roth_api_v1 *api,
                                          const struct roth_map_event_v1 *event)
{
    /* Do NOT reload inline: this event is raised from the engine's per-frame
     * int33 pump and can land while a world render (hwr_scene_build_mesh) is on
     * the stack, so an inline hwr_world_snapshot would free and reallocate
     * g_world's geometry under the running mesh builder -- a use-after-free.
     * Withdraw the now-stale GL frame and defer the re-snapshot to the render
     * dispatch's safe point, keeping only the newest generation. */
    hwr_withdraw_if_owned(api);
    g_map_reload_generation = event->generation;
    g_map_reload_pending = 1;
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
    hwr_map_data_clear(0);
    hwr_objects_clear(&g_objects);
    hwr_scene_clear(&g_scene);
    hwr_gl_shutdown();
    hwr_composite_clear(&g_composite);
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
    failures += roth_override(registrar, ROTH_FN_render_world_view,
                              hwr_render_world_view, 0) != 0;
    failures += roth_override(registrar, ROTH_FN_run_gameplay_frame,
                              hwr_run_gameplay_frame, 0) != 0;
    failures += roth_override(registrar, ROTH_FN_handle_cursor_click,
                              hwr_handle_cursor_click, 0) != 0;
    failures += roth_override(registrar, ROTH_FN_draw_world_surface_spans,
                              hwr_draw_world_surface_spans, 0) != 0;
    failures += roth_override(registrar, ROTH_FN_draw_scaled_sprite_spans,
                              hwr_draw_scaled_sprite_spans, 0) != 0;
    failures += roth_override(registrar, ROTH_FN_draw_floorceil_surface,
                              hwr_draw_floorceil_surface, 0) != 0;
    failures += roth_override(registrar, ROTH_FN_render_world_sprite,
                              hwr_render_world_sprite, 0) != 0;
    failures += roth_override(registrar, ROTH_FN_capture_screen_thumbnail,
                              hwr_capture_screen_thumbnail, 0) != 0;
    failures += roth_override(registrar, ROTH_FN_grayscale_background_view,
                              hwr_grayscale_background_view, 0) != 0;
    failures += roth_override(registrar, ROTH_FN_gdv_clear_vga_palette,
                              hwr_gdv_clear_vga_palette, 0) != 0;
    failures += roth_override(registrar, ROTH_FN_gdv_write_scaled_palette,
                              hwr_gdv_write_scaled_palette, 0) != 0;
    failures += roth_override(registrar, ROTH_FN_upload_palette_dac,
                              hwr_upload_palette_dac, 0) != 0;
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
