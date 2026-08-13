#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "roth_sdk.h"

#include "hwr_gl.h"
#include "hwr_lights.h"
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
#include "hwr_chrome.h"
#include "hwr_enh_truecolor.h"
#include "hwr_look.h"
#include "hwr_ownership.h"
#include "hwr_sun.h"
#include "hwr_pshadow.h"
#include "hwr_overlay.h"
#include "hwr_sysinfo.h"

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
/* The map's own file name, which is the only thing that says which map is
 * loaded: one texture archive serves as many as twenty-nine maps, so the
 * archive name cannot stand in for it. The engine writes the incoming map's
 * name here before the geometry it names is read, hands this very buffer to the
 * loader as the file to open, and builds the map's own state file from the same
 * string. */
#define VA_G_MAP_RAW_NAME              0x701ecu
#define VA_G_GDV_PALETTE_MODE          0x91de6u
#define VA_G_MOUSE_X                   0x707b3u
#define VA_G_MOUSE_Y                   0x707b7u

/* One spelling of the version, for the plugin record and for the settings
 * overlay that shows it. */
#define HWR_PLUGIN_VERSION "0.1.0"

struct hwr_viewport {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
};

struct hwr_render_settings {
    int enabled;
    uint32_t internal_w;
    uint32_t internal_h;
};

static const struct roth_api_v1 *g_api;
/* The present channel identifies the owner by the api-table pointer that first submits a frame.
 * Override callbacks and lifecycle callbacks are handed different api-table copies for the same
 * plugin, so every submit/withdraw must use one consistent token or the channel reports a foreign
 * owner. The world pass owns the channel, so its api pointer is the canonical identity. */
static const struct roth_api_v1 *g_channel_api;
static struct hwr_render_settings g_render_settings = { 1, 0, 0 };
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
/* The descriptor's geometry outlives its pixels. Handing the screen back to an
 * engine routine drops the image but not the canvas it was published on, so
 * the frames that keep that canvas alive while the engine draws can be built
 * without waiting for a new render. */
static int g_last_frame_geometry_valid;
/* Raised by any submission, cleared by the present that consumes it: the
 * present seam only steps in for presents the render path did not already
 * supply a frame for. */
static int g_submitted_for_present;
static struct hwr_ownership g_ownership;
static uint32_t g_present_max_w;
static uint32_t g_present_max_h;
static int g_present_caps_checked;
static int g_present_caps_valid;
static int g_channel_owned;
static struct hwr_input_route g_input_route;
static int g_input_route_active;
/* Raised for as long as this mod has the engine mouse rewritten into
 * engine-layer coordinates for an engine routine to read. The value in game RAM
 * is in full-canvas coordinates everywhere else, and localizing a value that is
 * already local moves the point -- which matters because the timer-driven poll
 * can arrive inside any of these windows. */
static volatile sig_atomic_t g_engine_mouse_localized;
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
/* Said once: a name the mod cannot read is a standing condition, and it would
 * otherwise be said again at every map load for the rest of the session. */
static int g_map_name_error_logged;
static int g_selfheal_retry;
static uint32_t g_selfheal_tick;
/* A map load only records a pending reload here; the geometry re-snapshot runs
 * at the render-dispatch safe point (see hwr_render_world_view_pass), never
 * inline from a notification, so g_world's arrays can never be freed or
 * reallocated while hwr_scene_build_mesh is walking them.  Several loads before
 * the next dispatch coalesce into one reload of whatever is staged then. */
static int g_map_reload_pending;
/* Frames the settings panel's rect stays announced after the panel is gone. */
static int g_overlay_rect_held;
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
/* The rects the engine covers with window chrome, so the submitted baseline can
 * be made unmatchable inside them and an opaque window stays opaque. */
static struct hwr_chrome g_chrome;
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
/* The light-state instrument: HWR_LIGHT_DUMP=<path> appends what the engine is
 * doing to the sector light and flags bytes, and to the set of objects the map
 * marks unshaded. HWR_LIGHT_DUMP_EVERY=<n> thins it to one look per n frames; a
 * look that finds nothing moved writes nothing, so watching every frame is what
 * it does by default and a ramp is seen step by step. */
static char g_light_dump_path[2600];
static uint32_t g_light_dump_counter;
static uint32_t g_light_dump_every = 1;
/* DAS-file persistence. The shared DAS is the same file for the whole game, and
 * the map DAS is shared by every map that names it -- the engine itself skips
 * its own DAS swap when the basename is unchanged -- so a transition that keeps
 * a basename leaves that file open and decoded rather than tearing it down,
 * re-opening it, and decoding it all over again. These hold the basenames of
 * the DAS files currently open (empty when none is). */
static char g_shared_open_basename[81];
static char g_map_open_basename[81];
/* The mod's own count of map loads. The engine clears its render-active latch
 * when it stages a map and raises it again at the top of the world pass, so the
 * render wrap sees exactly one zero per load and counts it here; the reload
 * that answers a given count runs once. Also the identity the timing report
 * labels a build with, so a build per transition is what the log shows. */
static uint32_t g_map_epoch;
static uint32_t g_map_epoch_loaded;
/* The host's map notification arrives from the frame pump, a tick after the
 * engine staged the map, so the render boundary above has normally answered it
 * already. It is kept as a backstop and consumed at the same safe point. */
static int g_map_event_pending;
/* Shared-DAS pre-warm: the decode is pure file + CPU work with no dependency on
 * a map, so it is done a little at a time while the menu owns the screen rather
 * than all at once inside the first transition. */
static int g_shared_prewarm_done;
static size_t g_shared_prewarm_cursor;

static void hwr_reload_map_data(const struct roth_api_v1 *api, uint32_t generation);
static void hwr_prewarm_shared_das(const struct roth_api_v1 *api);

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

static void hwr_map_data_clear(int keep_shared, int keep_map)
{
    hwr_objects_clear(&g_objects);
    hwr_objects_set_enabled(&g_objects, g_objects_enabled);
    memset(&g_sprite_census, 0, sizeof g_sprite_census);
    /* The sun's casters and census describe the map being torn down, and
     * the point-light shadow tiles with them. */
    hwr_sun_reset();
    hwr_pshadow_reset();
    hwr_world_clear(&g_world);
    /* A DAS file is closed only when it is not being carried across the
     * transition; keeping it open preserves its decoded payloads -- which the
     * per-frame sprite emitter re-dereferences and the mesh scan would
     * otherwise read and decode all over again -- and lets everything packed
     * out of it stay in the texture store. */
    if (!keep_map) {
        hwr_das_close(&g_map_archive);
        g_map_open_basename[0] = 0;
    }
    if (!keep_shared) {
        hwr_das_close(&g_shared_archive);
        g_shared_open_basename[0] = 0;
    }
}

/* One of the engine's file-name buffers, reduced to the name itself. The names
 * it keeps are written with the directory they live in, and that directory is
 * not part of what a name identifies. */
static int hwr_read_basename(const struct roth_api_v1 *api,
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

/* The name of the map being drawn, which is what a look file names a map by.
 * The buffer it comes from is scratch the engine also uses for other file
 * names, so a name that is not a plain file name is refused rather than matched
 * against: an unrecognized map costs its own overrides, a wrong one would put
 * another map's look on the screen. */
static int hwr_read_map_basename(const struct roth_api_v1 *api, char name[81])
{
    const char *cursor;

    if (hwr_read_basename(api, VA_G_MAP_RAW_NAME, name) != 0)
        return -1;
    for (cursor = name; *cursor != 0; cursor++) {
        unsigned char character = (unsigned char)*cursor;

        if (character <= 0x20u || character >= 0x7fu)
            return -1;
    }
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

/* The one place settings work happens: carry out a pending configuration
 * request, hand the resulting changes to the apply hooks, then make the window
 * requests those hooks queued. Every caller has to be non-reentrant with the
 * world render, which is why only the render boundary and the game tick call
 * it -- never a timer, compose, or present callback. */
static void hwr_settings_pump(const struct roth_api_v1 *api)
{
    hwr_config_drain_request();
    /* The picker having moved is one comparison, and it is taken here rather
     * than in an apply hook: the look's overrides have to be in force before
     * the hooks that consume them run, and a hook publishing them would invert
     * exactly that order. */
    hwr_look_follow_selection();
    /* The game config is the second writer into the same store, so it is read
     * at the same safe point and its overrides are in force before the hooks
     * that consume them run. */
    hwr_look_drain_request();
    hwr_settings_apply_pending();
    /* A response that fits a scale to the map's own authored brightness may
     * have been asked for after the map was staged -- by a look file, or by the
     * settings screen. The count it reads is a per-map quantity taken while the
     * map is being staged, so one owed is taken here rather than left until the
     * next map. Where nothing reads it this is one comparison. */
    if (hwr_world_light_census_due() && g_world.valid) {
        hwr_world_take_light_census(g_world.sectors, g_world.sector_count,
                                    &g_world.light_census);
        hwr_truecolor_set_map_lighting(g_world.light_census.counted,
                                       g_world.light_census.low,
                                       g_world.light_census.middle,
                                       g_world.light_census.high);
        hwr_settings_apply_pending();
    }
    hwr_apply_window_requests(api);
}

/* Stop compositing. The descriptor's geometry survives: an engine routine that
 * takes the screen gives it back, and the canvas it is given back on has to be
 * the one it was taken from. */
static void hwr_withdraw_if_owned(const struct roth_api_v1 *api)
{
    int32_t result;
    g_last_frame_valid = 0;
    g_gdv_palette_effect_active = 0;
    g_ownership.grayscale_held = 0;
    g_ownership.present_valid = 0;
    if (!g_channel_owned || api->present == NULL || g_channel_api == NULL)
        return;
    result = api->present->withdraw_world_frame(g_channel_api);
    if (result == ROTH_OK || result == ROTH_ERR_NOT_OWNER)
        g_channel_owned = 0;
}

/* The original renderer is on screen and stays there until something proves it
 * does not have to be. */
static void hwr_enter_software(const struct roth_api_v1 *api,
                               const char *reason)
{
    hwr_withdraw_if_owned(api);
    g_last_frame_geometry_valid = 0;
    hwr_ownership_enter(&g_ownership, HWR_OWNER_SOFTWARE, reason);
}

static void hwr_fail_gl(const struct roth_api_v1 *api)
{
    g_gl_permanently_failed = 1;
    hwr_enter_software(api, "hardware rendering failed");
    if (!g_gl_error_logged) {
        fprintf(stderr, "[renderer_overhaul] OpenGL disabled: %s\n", hwr_gl_last_error());
        g_gl_error_logged = 1;
    }
}

/* The single point the present channel is fed from, so every published frame
 * carries the same descriptor discipline. */
static int hwr_submit_frame(const struct roth_api_v1 *api,
                            struct roth_world_frame_v1 *frame)
{
    int32_t result;

    if (api->present == NULL || g_channel_api == NULL)
        return 0;
    result = api->present->submit_world_frame(g_channel_api, frame);
    if (result != ROTH_OK) {
        if (!g_submit_error_logged) {
            fprintf(stderr, "[renderer_overhaul] world frame submit failed: rc=%d (%ux%u viewport %d,%d %ux%u)\n",
                    (int)result, frame->w, frame->h, frame->vx, frame->vy,
                    frame->vw, frame->vh);
            g_submit_error_logged = 1;
        }
        if (result == ROTH_ERR_NOT_OWNER) {
            g_channel_owned = 0;
            g_last_frame_valid = 0;
        }
        return 0;
    }
    g_channel_owned = 1;
    return 1;
}

/* Submit with the window rects closed. The plane the engine framebuffer was
 * seeded from stays exactly what it was -- only the copy handed to the present
 * channel carries the stamp -- so everything else that reads the baseline (the
 * grayscale remap, the save thumbnail) keeps seeing the plane that describes
 * the world. Only the colour-bearing baseline is stamped; the whole-viewport
 * unmatchable plane the engine-owned frames carry is already opaque everywhere
 * and has nothing to close. */
static int hwr_submit_frame_closed(const struct roth_api_v1 *api,
                                   struct roth_world_frame_v1 *frame)
{
    struct roth_world_frame_v1 closed;

    if (frame->engine_base_pixels != g_composite.engine_base)
        return hwr_submit_frame(api, frame);
    closed = *frame;
    closed.engine_base_pixels =
        hwr_chrome_baseline(&g_chrome, api, frame, g_composite.engine_base);
    return hwr_submit_frame(api, &closed);
}

/* A wide source owns the whole canvas and the engine's own picture is centred
 * over it; an equal-aspect source is composited straight into the viewport. */
static uint64_t hwr_composite_mode_for(uint32_t width, uint32_t height,
                                       const struct hwr_viewport *viewport)
{
    return (uint64_t)width * (uint64_t)viewport->height !=
           (uint64_t)height * (uint64_t)viewport->width
        ? ROTH_COMPOSITE_ENGINE_CENTERED_OVER_CANVAS
        : ROTH_COMPOSITE_ENGINE_COLORKEY_OVER_WORLD;
}

static void hwr_frame_viewport(const struct roth_world_frame_v1 *frame,
                               struct hwr_viewport *viewport)
{
    viewport->x = frame->vx;
    viewport->y = frame->vy;
    viewport->width = frame->vw;
    viewport->height = frame->vh;
}

/* Keep the canvas alive over a frame the engine is drawing itself. The whole
 * viewport is handed to the engine layer -- no pixel of the original's own
 * output is copied, replaced, or reinterpreted -- and only the area outside it
 * comes from the mod: what the hardware renderer last drew there while the
 * original is simply holding its last frame, black while a cutscene sequence
 * owns the screen and the original has no surround at all.
 *
 * Built at the present seam and nowhere else: the plane that keeps the engine
 * layer opaque is derived from the framebuffer, and only there is the
 * framebuffer in the exact state the page flip is about to publish. */
static int hwr_submit_engine_passthrough(const struct roth_api_v1 *api,
                                         int hold_surround)
{
    struct roth_world_frame_v1 frame;
    struct hwr_viewport viewport;
    const uint32_t *pixels;

    if (!g_ownership.hardware_established || !g_last_frame_geometry_valid ||
        g_last_frame.w == 0 || g_last_frame.h == 0)
        return 0;
    if (hold_surround && g_last_frame_valid)
        pixels = (const uint32_t *)g_last_frame.pixels;
    else
        pixels = hwr_ownership_blank_canvas(&g_ownership, g_last_frame.w,
                                            g_last_frame.h);
    if (pixels == NULL)
        return 0;

    hwr_frame_viewport(&g_last_frame, &viewport);
    frame = g_last_frame;
    frame.pixels = pixels;
    frame.composite_mode =
        hwr_composite_mode_for(frame.w, frame.h, &viewport);
    if (!hwr_composite_engine_shadow(&g_composite, api, &frame))
        return 0;
    return hwr_submit_frame(api, &frame);
}

/* Palette-level effects are live engine state and the engine drives them
 * whether or not the world is being redrawn, so a held image has to follow
 * them. Resolving the standing palette-index image through a rebuilt palette
 * reproduces exactly what a render with that palette would have produced, and
 * needs no transform of the pixels that could not be undone. */
static void hwr_refresh_held_palette(const struct roth_api_v1 *api)
{
    uint8_t palette[1024];
    uint8_t base_palette[1024];
    struct hwr_gl_palette published;
    const uint32_t *pixels;
    const uint8_t *lut = NULL;
    int32_t level;

    if (!g_last_frame_valid || !g_ownership.present_valid ||
        g_gdv_palette_effect_active || g_composite.effect_active != 0)
        return;
    level = hwr_ownership_damage_level(api);
    if (level == g_ownership.present_damage &&
        g_ownership.grayscale_held == g_ownership.present_grayscale)
        return;
    if (g_ownership.grayscale_held) {
        lut = hwr_ownership_grayscale_lut(api);
        if (lut == NULL)
            return;
    }
    if (hwr_world_refresh_lut_state(&g_world, api) != 0)
        return;
    hwr_composite_publish_palette(g_world.luts.palette, lut, level, palette);
    memset(&published, 0, sizeof published);
    published.rgba = palette;
    published.damage_level = level;
    published.grayscale = lut != NULL;
    /* The colour-space twin needs the untinted palette to compose against, and
     * it can only stand in for the engine's desaturation while that table is
     * the one the engine's own rule produces from this palette. Where it is
     * not, the palette-index resolve reproduces the table by construction. */
    if (lut == NULL || hwr_composite_grayscale_reproducible(lut)) {
        hwr_composite_base_rgba(base_palette);
        published.base_rgba = base_palette;
    }
    pixels = hwr_gl_scene_resolve(&published, g_last_frame.w, g_last_frame.h);
    if (pixels == NULL) {
        /* Leave the published palette describing the pixels that are still
         * standing, so everything reducing that image keeps agreeing with it. */
        hwr_composite_publish_palette(
            g_world.luts.palette,
            g_ownership.present_grayscale ? hwr_ownership_grayscale_lut(api)
                                          : NULL,
            g_ownership.present_damage, NULL);
        return;
    }
    g_last_frame.pixels = pixels;
    g_ownership.present_damage = level;
    g_ownership.present_grayscale = g_ownership.grayscale_held;
}

/* The world pass publishes its baseline before the engine draws the chrome for
 * the same page, so a window standing over the world is not in the plane the
 * host compares against. The present entry runs with the framebuffer in the
 * state the page about to be published holds, which is where the rects can be
 * closed exactly -- so republish the same image from here, and only when a
 * window really needs closing, since republishing copies the frame again. */
static void hwr_close_windows_on_published_frame(const struct roth_api_v1 *api)
{
    struct roth_world_frame_v1 frame;
    const uint8_t *plane;

    if (!g_render_settings.enabled || g_gl_permanently_failed ||
        api->present == NULL || g_channel_api == NULL || !g_channel_owned ||
        !g_last_frame_valid || g_ownership.state != HWR_OWNER_HARDWARE ||
        g_last_frame.engine_base_pixels != g_composite.engine_base)
        return;
    plane = hwr_chrome_baseline(&g_chrome, api, &g_last_frame,
                                g_composite.engine_base);
    if (plane == g_composite.engine_base)
        return;
    frame = g_last_frame;
    frame.engine_base_pixels = plane;
    hwr_submit_frame(api, &frame);
}

/* Every present the render path did not already supply a frame for. A world
 * frame only reaches the screen paired with an engine page flip, and the host
 * drops one that stops being refreshed, so the mod's publishing cadence has to
 * be the engine's present cadence rather than a timer of its own. */
/* The rect the settings panel stands in, handed to the chrome set so the
 * baseline that travels with the frame closes it. Called right after the panel
 * is drawn, so the first frame it is composed onto is already submitted with
 * the rect closed. */
static void hwr_announce_overlay_rect(void)
{
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;

    if (!hwr_overlay_rect(&x, &y, &width, &height))
        return;
    hwr_chrome_set_overlay_rect(&g_chrome, 1, x, y, width, height);
    g_overlay_rect_held = 2;
}

/* And the other end of it, once per present: a rect is kept one present past
 * the panel coming down, so a cleared rect can never reach a frame that still
 * carries the panel's pixels. */
static void hwr_retire_overlay_rect(void)
{
    if (g_overlay_rect_held == 0)
        return;
    if (hwr_overlay_rect(NULL, NULL, NULL, NULL))
        return;
    if (--g_overlay_rect_held == 0)
        hwr_chrome_set_overlay_rect(&g_chrome, 0, 0, 0, 0, 0);
}

static void hwr_present_seam(const struct roth_api_v1 *api)
{
    int supplied;

    if (g_world_render_active)
        return;
    hwr_announce_overlay_rect();
    hwr_retire_overlay_rect();
    supplied = g_submitted_for_present;
    g_submitted_for_present = 0;
    if (supplied) {
        hwr_close_windows_on_published_frame(api);
        return;
    }
    if (!g_render_settings.enabled || g_gl_permanently_failed ||
        api->present == NULL || g_channel_api == NULL ||
        !g_last_frame_geometry_valid ||
        g_ownership.state == HWR_OWNER_SOFTWARE ||
        g_ownership.state == HWR_OWNER_ENGINE_SCREEN)
        return;

    /* A cutscene sequence draws its own screen and has no surround of its own,
     * so the area outside it is black; an ordinary transition is the original
     * leaving its last frame standing, so the area outside keeps what was
     * there. */
    if (hwr_ownership_engine_sequence(api)) {
        if (hwr_submit_engine_passthrough(api, 0))
            hwr_ownership_enter(&g_ownership, HWR_OWNER_PASSTHROUGH,
                                "a cutscene sequence owns the screen");
        return;
    }
    if (g_ownership.state == HWR_OWNER_PASSTHROUGH) {
        hwr_submit_engine_passthrough(api, 1);
        return;
    }
    if (!g_channel_owned || !g_last_frame_valid)
        return;
    hwr_refresh_held_palette(api);
    if (hwr_submit_frame_closed(api, &g_last_frame))
        hwr_ownership_enter(&g_ownership, HWR_OWNER_FROZEN,
                            "the world pass is idle");
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
 * engine scales the image across the whole screen, so the slice depends on
 * where the viewport falls within it, and the slice is laid down at the
 * viewport's own proportions with black beyond it: the original painted a
 * backdrop over its screen and nowhere else, so view area the target gains
 * beyond that screen has no backdrop to show. */
static void hwr_submit_backdrop_seed(const struct roth_api_v1 *api,
                                     const struct hwr_viewport *viewport,
                                     uint32_t output_width,
                                     uint32_t output_height)
{
    /* Re-seeding happens at every map change, every return to gameplay, and
     * every inventory close, always at the canvas size, so the working buffer
     * is grown once and kept. */
    static uint8_t *indices;
    static size_t indices_bytes;
    uint32_t screen_width = api->game_ram->u32(VA_G_SCREEN_PITCH);
    uint32_t screen_height = api->game_ram->u32(VA_G_SCREEN_HEIGHT);
    size_t plane = (size_t)output_width * output_height;

    g_backdrop_seed_pending = 0;
    if (!g_backdrop.valid || screen_width == 0 || screen_height == 0 ||
        plane == 0 || plane / output_width != output_height)
        return;
    if (plane > indices_bytes) {
        uint8_t *grown = (uint8_t *)realloc(indices, plane);
        if (grown == NULL)
            return;
        indices = grown;
        indices_bytes = plane;
    }
    if (hwr_backdrop_seed_indices(&g_backdrop, screen_width, screen_height,
                                  (uint32_t)viewport->x, (uint32_t)viewport->y,
                                  viewport->width, viewport->height,
                                  output_width, output_height, indices) == 0)
        hwr_gl_set_index_seed(indices, output_width, output_height);
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
    if (g_light_dump_path[0] != 0 &&
        ((g_light_dump_counter++) % g_light_dump_every) == 0)
        hwr_objects_dump_lights(&g_objects, &g_world, g_light_dump_path);
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
    int world_was_valid;
    int refresh_result;
    int hardware_ready;
    int submitted;

    hwr_frame_perf_begin(&frame_perf);

    /* File I/O and settings application belong at this non-reentrant render
     * boundary, not in the timer-driven per-frame callback. */
    hwr_config_poll();
    hwr_look_poll();
    hwr_settings_pump(api);
    g_wrap_serial++;
    /* THE LOAD BOUNDARY, read before the original renderer runs. The map loader
     * clears the engine's render-active latch and this very pass raises it
     * again as its first statement, so a zero here means the engine has staged
     * a map since the mod last drew, and it means it exactly once. Recording it
     * on this dispatch is what lets one transition cost one rebuild: the host's
     * own map notification arrives a tick later, by which time this has already
     * answered it. */
    if (hwr_ownership_map_staged(api)) {
        g_map_epoch++;
        g_map_reload_pending = 1;
    }
    hwr_frame_perf_mark(&frame_perf, HWR_FRAME_PERF_PREFLIGHT);

    if (!g_render_settings.enabled || g_gl_permanently_failed) {
        roth_next_render_world_view_pass(chain, eax_record);
        hwr_enter_software(api, "hardware rendering is not in use");
        return;
    }
    if (api->game_ram->u8(VA_G_VIEW_SCALE_FLAGS) != 0) {
        roth_next_render_world_view_pass(chain, eax_record);
        hwr_enter_software(api, "scaled viewport mode");
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

    /* Decided before the engine draws, never after: the only frame the mod may
     * take away from the original renderer is one it already knows it can put
     * a complete hardware scene in place of. A map change moves the geometry
     * the snapshot points at and clears the render latch, and a pending reload
     * or scene rebuild says the same thing from the mod's own side; any of
     * them and the engine keeps this frame. */
    hardware_ready = g_compare_path[0] == 0 && g_world.valid && g_scene.valid &&
                     !g_scene_rebuild_pending && !g_map_reload_pending &&
                     !hwr_ownership_world_stale(api, g_world.sector_section,
                                                g_world.vertex_section);
    g_suppress_active = hardware_ready;
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
        hwr_enter_software(api, "the world viewport is unusable");
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
        hwr_enter_software(api, "the requested canvas exceeds the host limits");
        return;
    }
    if (g_last_frame_valid &&
        (g_last_frame.w != render_width || g_last_frame.h != render_height)) {
        g_last_frame_valid = 0;
        g_ownership.present_valid = 0;
    }
    /* Safe point for a deferred map reload: nothing consumes g_world yet on this
     * dispatch, and (single game thread) no other dispatch's mesh build is in
     * flight, so re-snapshotting here cannot free geometry under
     * hwr_scene_build_mesh.  It runs before the refresh/render below, so the
     * first frame after a load still renders the new map -- or fails closed
     * through the same invalid-snapshot path as an inline reload would have. */
    if (g_map_event_pending) {
        g_map_event_pending = 0;
        /* The notification says a map was staged; the boundary above says which
         * dispatch that was. It only has work of its own when the snapshot
         * demonstrably does not describe the geometry the engine is about to
         * draw -- the same proof the self-heal path below acts on. */
        if (!g_map_reload_pending &&
            hwr_ownership_world_stale(api, g_world.sector_section,
                                      g_world.vertex_section)) {
            g_map_epoch++;
            g_map_reload_pending = 1;
        }
    }
    if (g_map_reload_pending) {
        g_map_reload_pending = 0;
        hwr_reload_map_data(api, g_map_epoch);
        g_map_epoch_loaded = g_map_epoch;
    }
    if (g_backdrop_seed_pending)
        hwr_submit_backdrop_seed(api, &viewport,
                                 render_width, render_height);
    hwr_perf_phase("seed");
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
        hwr_perf_phase("refresh");
        if (g_scene_rebuild_pending) {
            int rebuild_result;
            unsigned upload_errors;
            /* A rebuild that no map load asked for -- a texture id first seen
             * during play -- reports its cost the same way a transition does,
             * instead of being the one blocking rebuild nothing measures. */
            if (!hwr_perf_active())
                hwr_perf_begin(g_map_epoch);
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
        /* The sun's world-side work -- the caster slices and the moving
         * window -- as its own measured phase, before the views consume what
         * it publishes. The GL half of the pass reports beside the frame
         * perf line's other GPU submits. */
        if (g_scene.valid)
            hwr_scene_sun_frame(&g_scene, &g_world, &g_objects, api);
        hwr_frame_perf_mark(&frame_perf, HWR_FRAME_PERF_SUN);
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
        hwr_enter_software(api, "comparison capture");
        return;
    }
    if (pixels == NULL) {
        /* Nothing to publish. The engine drew this frame itself unless the
         * pre-render decision said it would not have to, and that only fails
         * when the hardware path itself broke -- the one case that goes back to
         * the original renderer for good rather than papering over a frame.
         * Otherwise the canvas is carried by the present seam, which sees the
         * engine's finished frame instead of a half-drawn one. */
        if (hardware_ready) {
            hwr_fail_gl(api);
            return;
        }
        hwr_ownership_enter(&g_ownership, HWR_OWNER_PASSTHROUGH,
                            "the world is not renderable yet");
        hwr_frame_perf_mark(&frame_perf, HWR_FRAME_PERF_FINALIZE);
        hwr_frame_perf_finish(&frame_perf);
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
        hwr_composite_mode_for(render_width, render_height, &viewport);
    frame.transparent_index = 0;
    frame.flags = 0;
    g_gdv_palette_effect_active = 0;
    /* A true-colour canvas can hold a colour the palette has no entry for, so
     * the baseline is quantized through the filled cube rather than through the
     * exact-match read the authentic canvas has always used. */
    hwr_composite_set_enhanced(&g_composite, hwr_gl_truecolor_active());
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
            hwr_enter_software(api, "no usable engine baseline");
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
        /* Deliberately the plain submit: the baseline has just been written
         * into the framebuffer, so the two agree everywhere and there is no
         * chrome yet to tell apart. Windows standing over this frame are
         * closed at the present entry, where the page is finished. */
        submitted = hwr_submit_frame(api, &frame);
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
    if (submitted) {
        g_submitted_for_present = 1;
        g_last_frame = frame;
        g_last_frame_valid = 1;
        g_last_frame_geometry_valid = 1;
        g_ownership.hardware_established = 1;
        g_ownership.grayscale_held = 0;
        g_ownership.present_grayscale = 0;
        g_ownership.present_damage = g_world.luts.damage_flash_level;
        g_ownership.present_valid = 1;
        hwr_ownership_enter(&g_ownership, HWR_OWNER_HARDWARE,
                            "the world is renderable");
        if (g_dump_path[0] != 0 && ((g_dump_counter++) & 31u) == 0)
            hwr_dump_frame(pixels, render_width, render_height);
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
        hwr_thumbnail_capture(&g_composite, api, &g_last_frame, &g_world, dst,
                              src, lut))
        return;
    roth_next_capture_screen_thumbnail(chain, dst, src, lut);
}

static void ROTH_CDECL hwr_grayscale_background_view(
    struct roth_chain *chain, const struct roth_api_v1 *api)
{
    const uint8_t *lut;

    /* This builds the authentic engine table and desaturates all already-drawn
     * 8bpp chrome. The index baseline follows it so the two keep agreeing, and
     * the held world image follows it through the presentation palette -- the
     * same route its live palette effects take, so the two never have to be
     * applied over one another. */
    roth_next_grayscale_background_view(chain);
    if (!g_render_settings.enabled || !g_last_frame_valid ||
        g_channel_api == NULL || api->present == NULL ||
        g_ownership.grayscale_held)
        return;
    lut = hwr_ownership_grayscale_lut(api);
    if (lut == NULL ||
        !hwr_composite_grayscale_baseline(&g_composite, lut, &g_last_frame)) {
        /* The authentic call has already remapped the engine framebuffer.
         * Reveal that safe 8bpp fallback instead of leaving a color RGBA
         * frame underneath grayscale chrome. */
        hwr_withdraw_if_owned(api);
        return;
    }
    g_ownership.grayscale_held = 1;
    hwr_refresh_held_palette(api);
    if (!g_ownership.present_grayscale || !hwr_submit_frame(api, &g_last_frame))
        hwr_withdraw_if_owned(api);
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
    g_engine_mouse_localized = 0;
    projection_changed = hwr_input_begin_pick_projection(
        api, &g_input_route, &saved);
    result = roth_next_render_world_view(chain, es, fs, gs);
    if (projection_changed)
        hwr_input_end_pick_projection(api, &saved);
    hwr_input_set_hud_mouse(api, &g_input_route);
    g_engine_mouse_localized = 1;
    return result;
}

/* Cursor mapping follows the canvas, not the image on it: the host maps the
 * pointer across whatever canvas is being composited, and that keeps being the
 * mod's while an engine screen is passed through it. Keying these on the
 * published canvas rather than on holding a hardware image is what keeps the
 * option lists between cutscenes clickable. */
static int hwr_canvas_published(void)
{
    return g_render_settings.enabled && g_channel_owned &&
           g_last_frame_geometry_valid;
}

/* What the settings panel is told about the renderer around it. Filling a small
 * value struct keeps the entry file's statics private and gives the overlay one
 * consistent view per pump. With the panel closed only the first two fields are
 * read, and the pointer -- the one part with any work in it -- is left out. */
static void hwr_fill_overlay_runtime(const struct roth_api_v1 *api,
                                     struct hwr_overlay_runtime *runtime,
                                     int with_cursor)
{
    struct hwr_input_route route;

    memset(runtime, 0, sizeof *runtime);
    runtime->version = HWR_PLUGIN_VERSION;
    /* A cutscene sequence owns the screen outright, and a map being staged owns
     * the next frame or two. Everything else -- the main menu, gameplay, the
     * inventory, and hardware rendering switched off -- is the panel's to open
     * over. */
    runtime->screen_available = g_ownership.state != HWR_OWNER_ENGINE_SCREEN &&
                                !g_map_reload_pending && !g_map_event_pending;
    if (!with_cursor || api == NULL || api->game_ram == NULL)
        return;
    /* A poll can arrive while an engine routine is reading the localized mouse
     * this mod wrote for it, and what is in game RAM then says nothing about
     * where the pointer is: localizing it a second time would move the panel's
     * hit point away from the pointer the player is aiming with, and reading it
     * raw would move it the other way. The pointer the panel already has is the
     * one it keeps until a poll can be trusted -- the engine cursor moves at
     * most a frame's worth between them. */
    if (g_engine_mouse_localized)
        return;
    if (hwr_canvas_published() &&
        g_last_frame.composite_mode ==
            ROTH_COMPOSITE_ENGINE_CENTERED_OVER_CANVAS &&
        hwr_input_prepare_ui_draw_route(api, g_last_frame.w, g_last_frame.h,
                                        &route)) {
        /* The engine layer is centred on a wider canvas, so the pointer the
         * host publishes is in canvas space and the panel is not. The draw
         * route localises it without parking an off-layer point, so a pointer
         * out in a wing simply misses the panel. */
        runtime->cursor_x = route.hud_x;
        runtime->cursor_y = route.hud_y;
    } else {
        runtime->cursor_x = (int32_t)api->game_ram->u32(VA_G_MOUSE_X);
        runtime->cursor_y = (int32_t)api->game_ram->u32(VA_G_MOUSE_Y);
    }
    runtime->cursor_valid = 1;
}

/* And what it is told about the renderer's own state, which the reported-state
 * rows read. Published from here so those rows need none of this file's
 * statics and this file exports none of them. */
static void hwr_publish_sysinfo(void)
{
    struct hwr_sysinfo_state state;

    memset(&state, 0, sizeof state);
    state.version = HWR_PLUGIN_VERSION;
    state.hardware_active = g_render_settings.enabled &&
                            !g_gl_permanently_failed &&
                            (g_ownership.state == HWR_OWNER_HARDWARE ||
                             g_ownership.state == HWR_OWNER_FROZEN);
    state.canvas_published = hwr_canvas_published() &&
                             g_last_frame_geometry_valid;
    if (state.canvas_published) {
        state.canvas_w = g_last_frame.w;
        state.canvas_h = g_last_frame.h;
        state.viewport_w = g_last_frame.vw;
        state.viewport_h = g_last_frame.vh;
        state.composite_mode = g_last_frame.composite_mode;
    }
    state.owner_name = hwr_ownership_name(g_ownership.state);
    state.gl_error = g_gl_permanently_failed ? hwr_gl_last_error() : NULL;
    state.present_max_w = g_present_max_w;
    state.present_max_h = g_present_max_h;
    state.present_caps_valid = g_present_caps_valid;
    state.map_loaded = g_map_epoch_loaded != 0;
    hwr_sysinfo_publish(&state);
}

static void ROTH_CDECL hwr_run_gameplay_frame(
    struct roth_chain *chain, const struct roth_api_v1 *api)
{
    int centered;

    if (!hwr_canvas_published()) {
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
    g_engine_mouse_localized = 1;
    roth_next_run_gameplay_frame(chain);
    hwr_input_restore_world_cursor(api, &g_input_route);
    g_engine_mouse_localized = 0;
    g_input_route_active = 0;
}

/* Inventory and dialogue run outside run_gameplay_frame, but their hit tests
 * still consume engine-layer coordinates. Keep the published cursor in
 * full-canvas space and localize only the authentic hit-test call. */
static void ROTH_CDECL hwr_handle_cursor_click(
    struct roth_chain *chain, const struct roth_api_v1 *api)
{
    struct hwr_input_route route;

    if (!hwr_canvas_published() ||
        g_last_frame.composite_mode !=
            ROTH_COMPOSITE_ENGINE_CENTERED_OVER_CANVAS ||
        !hwr_input_prepare_ui_route(
            api, g_last_frame.w, g_last_frame.h, 1, &route)) {
        roth_next_handle_cursor_click(chain);
        return;
    }
    hwr_input_set_hud_mouse(api, &route);
    g_engine_mouse_localized = 1;
    roth_next_handle_cursor_click(chain);
    hwr_input_restore_world_cursor(api, &route);
    g_engine_mouse_localized = 0;
}

/* The held inventory icon is drawn by the engine straight at the engine mouse
 * (draw_current_mouse_cursor_sprite, blit_2d.c:1179-1210), so it lands in the
 * 4:3 chrome layer while the published cursor lives in full-canvas space and
 * the two separate horizontally. Localize the mouse for the draw the same way
 * the hit test is localized. The coordinate stays unclamped on purpose: the
 * engine pins the icon to its own screen edges (blit_2d.c:1188-1191), so a
 * cursor out in a wing leaves the icon at the near border rather than the far
 * one. */
static void ROTH_CDECL hwr_draw_current_mouse_cursor_sprite(
    struct roth_chain *chain, const struct roth_api_v1 *api)
{
    struct hwr_input_route route;

    if (!hwr_canvas_published() ||
        g_last_frame.composite_mode !=
            ROTH_COMPOSITE_ENGINE_CENTERED_OVER_CANVAS ||
        !hwr_input_prepare_ui_draw_route(
            api, g_last_frame.w, g_last_frame.h, &route)) {
        roth_next_draw_current_mouse_cursor_sprite(chain);
        return;
    }
    hwr_input_set_hud_mouse(api, &route);
    g_engine_mouse_localized = 1;
    roth_next_draw_current_mouse_cursor_sprite(chain);
    hwr_input_restore_world_cursor(api, &route);
    g_engine_mouse_localized = 0;
}

/* The engine's save-under seam. Every window saves the pixels it is about to
 * cover before it covers them, and the rect it saves is the window plus its
 * four-pixel drop shadow (menu_hud_ui.c:1435/1464, dialogue_ui.c:1378,
 * inventory.c:1882) -- so this is the announcement of exactly where the engine
 * is opaque, taken from the engine itself rather than guessed at. */
static uint32_t ROTH_CDECL hwr_save_framebuffer_region(
    struct roth_chain *chain, const struct roth_api_v1 *api,
    uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t *cf_out)
{
    uint32_t handle =
        roth_next_save_framebuffer_region(chain, x, y, width, height, cf_out);
    hwr_chrome_record(&g_chrome, api, handle, x, y, width, height);
    return handle;
}

/* Re-capturing what is under a window is the engine's way of drawing it again
 * over fresh content (menu_hud_ui.c:270). */
static uint32_t *ROTH_CDECL hwr_blit_save_region(
    struct roth_chain *chain, const struct roth_api_v1 *api, uint32_t *pp)
{
    uint32_t *result = roth_next_blit_save_region(chain, pp);
    (void)api;
    hwr_chrome_cover(&g_chrome, (uint32_t)(uintptr_t)pp);
    return result;
}

/* Putting the saved pixels back makes the region world again, so it stops being
 * closed from here (renderer.c:1414). */
static void ROTH_CDECL hwr_blit_descriptor_rows(
    struct roth_chain *chain, const struct roth_api_v1 *api, uint32_t eax)
{
    (void)api;
    roth_next_blit_descriptor_rows(chain, eax);
    hwr_chrome_uncover(&g_chrome, eax);
}

/* Freeing the save-under block retires the handle for good; the pool zeroes the
 * slot as it goes (renderer.c:10440-10448). */
static uint32_t ROTH_CDECL hwr_pool_free_handle(
    struct roth_chain *chain, const struct roth_api_v1 *api,
    uint32_t *pool, uint32_t *handle)
{
    uint32_t result = roth_next_pool_free_handle(chain, pool, handle);
    (void)api;
    hwr_chrome_release(&g_chrome, (uint32_t)(uintptr_t)handle);
    return result;
}

/* The window-frame draw. Watching it is what keeps a standing window closed
 * across the open/close zoom, which restores its own save-under after every
 * step and then draws the next frame from the same handle. */
static void ROTH_CDECL hwr_draw_popup_shadow_border_smc(
    struct roth_chain *chain, const struct roth_api_v1 *api,
    int32_t x, int32_t y, int32_t width, int32_t height)
{
    roth_next_draw_popup_shadow_border_smc(chain, x, y, width, height);
    hwr_chrome_cover_draw(&g_chrome, api, x, y, width, height);
}

/* Apply the exact brightness multiplier the GDV fade has just written to the
 * VGA palette. Returns one when a frame was submitted, zero when no hardware
 * frame is active, and minus one after falling back to authentic 8bpp. */
static int hwr_submit_gdv_brightness(
    const struct roth_api_v1 *api, uint32_t multiplier)
{
    /* Only meaningful while the mod still holds an image: once an engine
     * routine has taken the screen there is nothing of the mod's on it to
     * fade. */
    if (!g_last_frame_valid || g_channel_api == NULL ||
        api->present == NULL ||
        g_ownership.state == HWR_OWNER_ENGINE_SCREEN)
        return 0;
    if (!hwr_composite_apply_brightness(
            &g_composite, &g_last_frame, multiplier)) {
        hwr_withdraw_if_owned(api);
        return -1;
    }
    if (!hwr_submit_frame(api, &g_last_frame)) {
        hwr_withdraw_if_owned(api);
        return -1;
    }
    return 1;
}

/* Video playback publishes its own display surface instead of drawing into the
 * engine framebuffer, so nothing in game memory describes what is on screen and
 * the only correct answer is to stop compositing for its duration. Handing the
 * screen back leaves the canvas description standing, so the sequence's own
 * screen transitions are presented on the canvas they started from. */
static uint32_t ROTH_CDECL hwr_play_gdv_cutscene(
    struct roth_chain *chain, const struct roth_api_v1 *api,
    uint32_t eax, uint32_t edx, uint32_t ebx, uint32_t ecx)
{
    uint32_t result;
    int had_canvas = g_last_frame_geometry_valid;

    hwr_ownership_enter(&g_ownership, HWR_OWNER_ENGINE_SCREEN,
                        "video playback owns the display");
    hwr_withdraw_if_owned(api);
    result = roth_next_play_gdv_cutscene(chain, eax, edx, ebx, ecx);
    hwr_ownership_enter(&g_ownership,
                        had_canvas ? HWR_OWNER_PASSTHROUGH : HWR_OWNER_SOFTWARE,
                        "video playback finished");
    return result;
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

/* The original's void law is "never clear the view": a pixel no surface covers
 * keeps what the previous frame left there. The one thing that does restart it
 * is this routine, which paints the backdrop image over the WHOLE screen -- not
 * just the frame around the view -- and is reached on a map load, on every
 * return to gameplay, and on closing the inventory. Re-arming the seed here
 * gives the canvas the same restart at the same moments, so how old a persisted
 * pixel can get is bounded exactly as it is in the original. The target is
 * invalidated either way: with no usable backdrop image the honest starting
 * point is a flat one, not the previous map. */
static void ROTH_CDECL hwr_load_backdrop_raw(
    struct roth_chain *chain, const struct roth_api_v1 *api)
{
    (void)api;
    roth_next_load_backdrop_raw(chain);
    g_backdrop_seed_pending = g_backdrop.valid;
    hwr_gl_invalidate_index_target();
}

/* The present entry: called immediately before every page flip, from every
 * screen the engine drives, including the blocking screen-slide loops that no
 * per-frame callback reaches. Running after the chain reads the framebuffer in
 * its final state for this present, and the submission still lands before the
 * flip that publishes it. */
static void ROTH_CDECL hwr_flush_dirty_rects(
    struct roth_chain *chain, const struct roth_api_v1 *api)
{
    roth_next_flush_dirty_rects(chain);
    hwr_present_seam(api);
    /* The window regions that came through this present still covered belong to
     * windows that are standing, not to widgets that put their pixels back
     * before the page is published. Only the former can be closed by what they
     * painted rather than by what their pixels happen to equal. */
    hwr_chrome_settle(&g_chrome);
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
        const char *light_dump = getenv("HWR_LIGHT_DUMP");
        const char *light_every = getenv("HWR_LIGHT_DUMP_EVERY");
        if (dump != NULL)
            snprintf(g_dump_path, sizeof g_dump_path, "%s", dump);
        if (compare != NULL)
            snprintf(g_compare_path, sizeof g_compare_path, "%s", compare);
        g_objects_enabled = !(objects != NULL && objects[0] == '0');
        if (obj_dump != NULL)
            snprintf(g_obj_dump_path, sizeof g_obj_dump_path, "%s", obj_dump);
        if (light_dump != NULL)
            snprintf(g_light_dump_path, sizeof g_light_dump_path, "%s",
                     light_dump);
        if (light_every != NULL) {
            long every = strtol(light_every, NULL, 10);

            g_light_dump_every = every > 0 ? (uint32_t)every : 1u;
        }
    }
    hwr_objects_set_enabled(&g_objects, g_objects_enabled);
    hwr_ownership_init(&g_ownership);
    hwr_settings_init_defaults();
    /* A hook that never registers is a subsystem that silently never applies,
     * and one of these decides what the world is drawn with -- so a failure
     * here hands the screen back to the original renderer rather than running
     * with a setting nothing carries out. */
    if (hwr_settings_register_apply(hwr_render_settings_apply, NULL) != 0 ||
        hwr_settings_register_apply(hwr_data_settings_apply, NULL) != 0 ||
        hwr_settings_register_apply(hwr_gl_settings_apply, NULL) != 0 ||
        hwr_settings_register_apply(hwr_window_settings_apply, NULL) != 0 ||
        hwr_enh_truecolor_register() != 0 ||
        hwr_sun_register() != 0) {
        fprintf(stderr, "[renderer_overhaul] settings apply-hook registration"
                        " failed; the original renderer keeps the screen\n");
        g_gl_permanently_failed = 1;
    }
    /* The game configs folder is listed BEFORE the settings file is parsed, so
     * that the file's own choice of one is a value the picker row holds -- even
     * where the file it names has been moved away. The chosen file itself is
     * read straight afterwards. */
    hwr_config_open(g_plugin_dir);
    hwr_look_load(g_plugin_dir);
    hwr_overlay_init();
}

/* The keystroke seam, in the tick. Everything it can do is flip a flag and note
 * the key; the panel itself runs on the game thread. */
static uint8_t ROTH_CDECL hwr_on_scancode(const struct roth_api_v1 *api,
                                           uint8_t scancode)
{
    (void)api;
    return hwr_overlay_scancode(scancode);
}

/* The compose seam, in the tick: the drawn panel onto the frame that is about
 * to be presented, and nothing else. */
static void ROTH_CDECL hwr_on_compose_tick(const struct roth_api_v1 *api,
                                            uint8_t *pixels, uint32_t width,
                                            uint32_t height)
{
    hwr_overlay_compose(api, pixels, width, height);
}

static void ROTH_CDECL hwr_on_frame_game(const struct roth_api_v1 *api)
{
    struct hwr_overlay_runtime runtime;

    if (g_world_render_active)
        return;
    /* The world pass is not dispatched while a menu owns the screen, so this
     * is the safe point that answers a settings request made there. */
    hwr_settings_pump(api);
    if (hwr_overlay_is_open())
        hwr_publish_sysinfo();
    hwr_fill_overlay_runtime(api, &runtime, hwr_overlay_is_open());
    hwr_overlay_pump(api, &runtime);
    hwr_announce_overlay_rect();
    hwr_prewarm_shared_das(api);

    if (g_wrap_serial != g_watchdog_serial) {
        g_watchdog_serial = g_wrap_serial;
        g_watchdog_stall_count = 0;
    } else {
        if (g_watchdog_stall_count < UINT32_MAX)
            g_watchdog_stall_count++;
        /* Backstop for the few page flips that are not preceded by the present
         * entry; the state machine still decides what such a present carries. */
        if (g_watchdog_stall_count >= 2)
            hwr_present_seam(api);
    }
}

/* Opens the shared DAS and decodes a little of it per tick, before any map has
 * been loaded. Its name comes from the configuration and is settled from
 * startup, its contents do not depend on which map is coming, and decoding is
 * idempotent with the map load's own walk -- so the work that would otherwise
 * land inside the first transition is done while the menu owns the screen
 * instead. Runs on the game thread with no render in flight, and stops for good
 * once the whole file is decoded or the name cannot be resolved. */
static void hwr_prewarm_shared_das(const struct roth_api_v1 *api)
{
    /* Entries decoded per tick: enough to finish during a menu, small enough
     * that a tick never shows the cost. */
    enum { HWR_PREWARM_BATCH = 24 };
    size_t limit;

    if (g_shared_prewarm_done || g_plugin_dir[0] == 0 ||
        !hwr_das_persist_enabled() || !g_objects_enabled ||
        g_map_epoch_loaded != 0 || g_map_reload_pending || g_map_event_pending)
        return;
    if (g_shared_archive.entries == NULL) {
        char shared_name[81];
        char root[2600];
        char shared_path[2600];
        if (hwr_read_basename(api, VA_G_SHARED_DAS_NAME, shared_name) != 0) {
            g_shared_prewarm_done = 1;
            return;
        }
        snprintf(root, sizeof root, "%s", g_plugin_dir);
        if (hwr_parent_path(root) != 0 || hwr_parent_path(root) != 0 ||
            hwr_find_archive_path(root, shared_name, shared_path,
                                  sizeof shared_path) != 0 ||
            hwr_das_open(&g_shared_archive, shared_path) != 0) {
            g_shared_prewarm_done = 1;
            return;
        }
        snprintf(g_shared_open_basename, sizeof g_shared_open_basename, "%s",
                 shared_name);
        g_shared_prewarm_cursor = 0;
    }
    limit = g_shared_prewarm_cursor + HWR_PREWARM_BATCH;
    if (limit > g_shared_archive.entry_count)
        limit = g_shared_archive.entry_count;
    while (g_shared_prewarm_cursor < limit)
        hwr_das_decode(&g_shared_archive, g_shared_prewarm_cursor++);
    if (g_shared_prewarm_cursor >= g_shared_archive.entry_count)
        g_shared_prewarm_done = 1;
}

static void ROTH_CDECL hwr_on_mouse_poll(const struct roth_api_v1 *api,
                                          struct roth_mouse_poll_v1 *poll)
{
    if (g_world_render_active) {
        /* The timer that polls the mouse can land anywhere, including inside
         * the frame this mod is building, and nothing that draws or calls the
         * engine may run there. The click still has to be taken and still has
         * to be hidden from the game: dropping the poll outright loses presses
         * the player made, and lets the ones it loses through to gameplay --
         * where a held button is what puts the engine into the mouselook mode
         * the panel reads its pointer from. Taking the buttons here and leaving
         * the press to the next pump costs nothing and loses nothing. */
        if (hwr_overlay_is_open())
            hwr_overlay_mouse_capture(api, poll);
        return;
    }
    if (hwr_overlay_is_open()) {
        struct hwr_overlay_runtime runtime;

        /* The panel takes the pointer here and hides it from the game, and
         * pumps from here as well: the screens it is most likely to be open
         * over drive this seam far more reliably than a per-frame callback. */
        hwr_publish_sysinfo();
        hwr_fill_overlay_runtime(api, &runtime, 1);
        hwr_overlay_mouse(api, poll, &runtime);
        hwr_announce_overlay_rect();
    }
    if (g_wrap_serial != g_poll_serial) {
        g_poll_serial = g_wrap_serial;
        g_poll_stall_count = 0;
        return;
    }
    if (g_poll_stall_count < UINT32_MAX)
        g_poll_stall_count++;
    /* Same backstop as the frame pump: the present entry carries the ordinary
     * cadence, and the throttle keeps this cheap where it does not. */
    if (g_poll_stall_count >= 4 && ((g_poll_throttle++) & 3u) == 0)
        hwr_present_seam(api);
}

/* (Re)loads every mod-side map dependency: archives, geometry snapshot, colour
 * tables. Called from the map notification AND from the render wrap's self-heal
 * path — some in-gameplay map transitions replace the geometry sections without
 * a detectable load boundary, and the per-frame refresh guard is what notices. */
static void hwr_reload_map_data(const struct roth_api_v1 *api, uint32_t generation)
{
    char map_name[81];
    char shared_name[81];
    char look_name[81];
    char root[2600];
    char map_path[2600];
    char shared_path[2600];
    char failure[256];
    int keep_shared = 0;
    int keep_map = 0;

    hwr_perf_begin(generation);
    hwr_settings_apply_pending();
    failure[0] = 0;
    /* Read the DAS names before any teardown, so the persistence decision can
     * be taken while the files that answer it are still open. */
    if (hwr_read_basename(api, VA_G_MAP_DAS_NAME, map_name) != 0 ||
        hwr_read_basename(api, VA_G_SHARED_DAS_NAME, shared_name) != 0) {
        snprintf(failure, sizeof failure, "DAS name is unavailable");
        hwr_look_select_map(NULL);
        hwr_settings_apply_pending();
        goto failed;
    }
    /* The map-change safe point is where the look file's per-map overrides come
     * into force, so a previous map's overrides can never survive into the next
     * one: the layer is recomposed from scratch for the map being loaded. The
     * map is named by its own file, never by the archive it shares with as many
     * as twenty-eight others. Where that name cannot be trusted the global look
     * stands alone, which is what an unlisted map gets anyway. */
    if (hwr_read_map_basename(api, look_name) == 0) {
        hwr_look_select_map(look_name);
    } else {
        hwr_look_select_map(NULL);
        if (!g_map_name_error_logged) {
            fprintf(stderr, "[renderer_overhaul] the map name is unreadable; "
                            "per-map look overrides are not in force\n");
            g_map_name_error_logged = 1;
        }
    }
    hwr_settings_apply_pending();
    keep_shared = hwr_das_persist_enabled() &&
                  g_shared_archive.entries != NULL &&
                  g_shared_open_basename[0] != 0 &&
                  strcmp(shared_name, g_shared_open_basename) == 0;
    keep_map = hwr_das_persist_enabled() &&
               g_map_archive.entries != NULL &&
               g_map_open_basename[0] != 0 &&
               strcmp(map_name, g_map_open_basename) == 0;
    hwr_map_data_clear(keep_shared, keep_map);
    /* A load rebuilds what the save-under blocks are carved out of, and no
     * window survives one anyway. Forgetting the rects only ever costs the
     * closing of a window still standing; it can never hide world. */
    hwr_chrome_reset(&g_chrome);
    hwr_perf_phase("clear");
    g_scene_rebuild_pending = 1;
    g_scene_error_logged = 0;
    g_backdrop_seed_pending = 0;
    /* Persisted pixels belong to the map that drew them. Invalidating here as
     * well as at the backdrop repaint covers a missing or unusable backdrop
     * image: the next map then starts from a flat target rather than from the
     * previous map's picture. */
    hwr_gl_invalidate_index_target();
    if (g_plugin_dir[0] == 0) {
        snprintf(failure, sizeof failure, "plugin directory was not captured");
        goto failed;
    }
    snprintf(root, sizeof root, "%s", g_plugin_dir);
    if (hwr_parent_path(root) != 0 || hwr_parent_path(root) != 0) {
        snprintf(failure, sizeof failure, "plugin directory has no game-root parent");
        goto failed;
    }
    if ((!keep_map &&
         hwr_find_archive_path(root, map_name, map_path, sizeof map_path) != 0) ||
        (!keep_shared &&
         hwr_find_archive_path(root, shared_name, shared_path,
                               sizeof shared_path) != 0)) {
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

    /* A DAS being carried across the transition is already open with the
     * matching basename; only a fresh (or changed) one is (re)opened here. */
    if (!keep_map) {
        if (hwr_das_open(&g_map_archive, map_path) != 0) {
            snprintf(failure, sizeof failure, "map DAS: %.220s",
                     hwr_das_error(&g_map_archive));
            goto failed;
        }
        snprintf(g_map_open_basename, sizeof g_map_open_basename, "%s",
                 map_name);
    }
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
    /* The map's own authored brightness, on its way to the light rig. The room
     * response places a sector between two anchors and this is where those
     * anchors come from; it is taken from the SNAPSHOT, so the scale is the
     * map's authoring rather than whatever its switches happen to be doing. */
    hwr_truecolor_set_map_lighting(g_world.light_census.counted,
                                   g_world.light_census.low,
                                   g_world.light_census.middle,
                                   g_world.light_census.high);
    /* And what the same metadata says about the light carried THROUGH it: the
     * shade level its candle-lit surfaces are drawn at, which is the distance a
     * carried light with no reach written for it is given. */
    hwr_truecolor_set_map_candle(g_world.luts.tint_shade_level);
    hwr_settings_apply_pending();
    /* A fresh sector table means the light instrument's held state describes a
     * map that is no longer loaded, so the next look writes a census. */
    hwr_objects_dump_lights_reset();
    /* And the same for the lights that are not at the eye: what stood in the
     * old map's sector 12 is not what stands in this one's, and the colours
     * read off the old map's archive are not this map's colours. Both are
     * forgotten rather than carried, so nothing here fades out of a room that
     * no longer exists. */
    hwr_lights_reset();
    /* And the granted surfaces of the map now staged, taken ONCE, HERE.
     *
     * Which faces carry a texture the game config grants a light, and where the
     * centroid of each room's worth of them lies, is a fact about the MAP. It
     * is taken beside the geometry it is read out of, so a frame's whole share
     * of the work is the containment test over the small set it leaves behind
     * -- and a map nothing is granted on costs no walk at all. What it does not
     * follow is a texture a script swaps mid-map: the answer is the map as it
     * was staged, which is what "once per map" means. The two archives go with
     * it because a grant that states no colour takes one off the texture's own
     * texels, and the texels live in the pictures rather than in the map. */
    hwr_lights_stage_glow_faces(&g_world, &g_map_archive, &g_shared_archive);
    /* And the free-standing lights the game config hangs in this map, for the
     * same reason and in the same place: which ROOM each one is in is a fact
     * about the map's own footprints, so it is answered once beside the geometry
     * it is read out of rather than sixty times a second. */
    hwr_lights_stage_placed(&g_world);
    /* And the sun's own per-map fact: the sky-face census that decides
     * whether the key light exists on this map at all. A sealed map answers
     * zero and the whole machinery stands down for its duration; the caster
     * mesh itself is built lazily at the first frame that wants it, so the
     * census here builds nothing. */
    hwr_sun_stage_map(&g_world);
    /* A new map's tiles describe nothing; the shadow ledger starts over. */
    hwr_pshadow_reset();
    /* Pre-decode every sprite frame both archives hold: all of the shared sprite
     * set, and the per-map archive's object-graphic tail (ids at and above the
     * bias). The census feeds the object-layer stats line. Decoding is idempotent
     * with the scene upload's own lazy decode. */
    memset(&g_sprite_census, 0, sizeof g_sprite_census);
    if (g_objects_enabled) {
        /* A DAS carried across the transition still holds every payload it
         * decoded last time, so this walk only tallies them; it decodes just
         * the entries the previous map's texture pack released. */
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
        fprintf(stderr, "  color tables: palette=768 colormap=8192 tint=8192 glow=512 blend=65536 damage=%ld shade=%u/%u\n",
                (long)g_world.luts.damage_flash_level,
                g_world.luts.shade_level,
                g_world.luts.tint_shade_level);
    }
    g_luts_recheck_pending = 1;
    return;

failed:
    fprintf(stderr, "[renderer_overhaul] map data unavailable (gen=%u): %s\n",
            generation, failure);
    hwr_map_data_clear(0, 0);
    g_scene_rebuild_pending = 1;
    /* Nothing is loaded and the load boundary that asked for this has been
     * consumed, so arm the slow retry rather than wait for the next one. */
    g_selfheal_retry = 1;
}

static void ROTH_CDECL hwr_on_map_loaded(const struct roth_api_v1 *api,
                                          const struct roth_map_event_v1 *event)
{
    /* Do NOT reload inline: this event is raised from the engine's per-frame
     * int33 pump and can land while a world render (hwr_scene_build_mesh) is on
     * the stack, so an inline hwr_world_snapshot would free and reallocate
     * g_world's geometry under the running mesh builder -- a use-after-free.
     * Defer the re-snapshot to the render dispatch's safe point, keeping only
     * the newest generation.
     *
     * The published image is deliberately NOT taken down here. Loading a map
     * touches no screen state at all in the original -- the last presented
     * frame simply stands until the new map's first one is flipped -- so the
     * transition is carried by leaving that frame standing and handing the
     * viewport to the engine, not by taking the screen away. */
    (void)api;
    (void)event;
    g_map_event_pending = 1;
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
    /* The canvas description itself is no longer valid, so this is the one
     * event that gives the screen back completely. The window rects describe
     * framebuffer positions in the mode that is going away. */
    hwr_chrome_reset(&g_chrome);
    hwr_enter_software(api, "the video mode changed");
}

static void ROTH_CDECL hwr_on_unload(const struct roth_api_v1 *api)
{
    hwr_enter_software(api, "the renderer is unloading");
    hwr_overlay_shutdown();
    hwr_map_data_clear(0, 0);
    hwr_objects_clear(&g_objects);
    hwr_scene_clear(&g_scene);
    hwr_gl_shutdown();
    hwr_composite_clear(&g_composite);
    hwr_chrome_clear(&g_chrome);
    hwr_ownership_clear(&g_ownership);
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
    failures += roth_override(registrar,
                              ROTH_FN_draw_current_mouse_cursor_sprite,
                              hwr_draw_current_mouse_cursor_sprite, 0) != 0;
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
    failures += roth_override(registrar, ROTH_FN_play_gdv_cutscene,
                              hwr_play_gdv_cutscene, 0) != 0;
    failures += roth_override(registrar, ROTH_FN_flush_dirty_rects,
                              hwr_flush_dirty_rects, 0) != 0;
    failures += roth_override(registrar, ROTH_FN_load_backdrop_raw,
                              hwr_load_backdrop_raw, 0) != 0;
    failures += roth_override(registrar, ROTH_FN_save_framebuffer_region,
                              hwr_save_framebuffer_region, 0) != 0;
    failures += roth_override(registrar, ROTH_FN_blit_save_region,
                              hwr_blit_save_region, 0) != 0;
    failures += roth_override(registrar, ROTH_FN_blit_descriptor_rows,
                              hwr_blit_descriptor_rows, 0) != 0;
    failures += roth_override(registrar, ROTH_FN_pool_free_handle,
                              hwr_pool_free_handle, 0) != 0;
    failures += roth_override(registrar, ROTH_FN_draw_popup_shadow_border_smc,
                              hwr_draw_popup_shadow_border_smc, 0) != 0;
    if (failures != 0)
        fprintf(stderr, "[renderer_overhaul] %d override registration(s) failed\n", failures);
}

static const struct roth_plugin_info_v1 RENDERER_OVERHAUL = {
    .abi_major = ROTH_ABI_MAJOR,
    .abi_minor = ROTH_ABI_MINOR,
    .struct_size = sizeof(struct roth_plugin_info_v1),
    .id = "ire.roth.renderer_overhaul",
    .name = "Renderer Overhaul",
    .version = HWR_PLUGIN_VERSION,
    .sdk_req_major = ROTH_SDK_MAJOR,
    .sdk_req_minor = ROTH_SDK_MINOR,
    .api_use = ROTH_API_USE_GAME_RAM | ROTH_API_USE_ENGINE |
               ROTH_API_USE_COMPOSE | ROTH_API_USE_INPUT,
    .on_load = hwr_on_load,
    .on_frame_game = hwr_on_frame_game,
    .on_compose_tick = hwr_on_compose_tick,
    .on_unload = hwr_on_unload,
    .on_register_overrides = hwr_on_register_overrides,
    .on_scancode = hwr_on_scancode,
    .on_mouse_poll = hwr_on_mouse_poll,
    .on_map_loaded = hwr_on_map_loaded,
    .on_video_mode_changed = hwr_on_video_mode_changed
};

ROTH_PLUGIN_EXPORT const struct roth_plugin_info_v1 *ROTH_CDECL roth_plugin_query_v1(void)
{
    return &RENDERER_OVERHAUL;
}
