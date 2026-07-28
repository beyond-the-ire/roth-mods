#include "hwr_config.h"

#include "hwr_settings.h"
#include "hwr_util.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HWR_CONFIG_POLL_FRAMES 30u

struct hwr_config_state {
    char path[2600];
    uint64_t active_hash;
    uint64_t candidate_hash;
    uint32_t candidate_polls;
    uint32_t frames_until_poll;
};

static struct hwr_config_state g_config;

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

static int hwr_parse_float(const char *text, float maximum, float *value)
{
    char *end;
    double parsed;

    errno = 0;
    parsed = strtod(text, &end);
    while (*end != 0 && isspace((unsigned char)*end))
        end++;
    if (errno != 0 || end == text || *end != 0 ||
        parsed < 0.0 || parsed > maximum)
        return 0;
    *value = (float)parsed;
    return 1;
}

static int hwr_apply_config_value(struct hwr_settings *settings,
                                  const char *key, const char *value)
{
    uint32_t number;
    int boolean;
    float real;

    if (hwr_ascii_equal_ci(key, "enabled")) {
        if (!hwr_parse_bool(value, &boolean))
            return 0;
        settings->enabled = boolean;
    } else if (hwr_ascii_equal_ci(key, "debug")) {
        if (!hwr_parse_bool(value, &boolean))
            return 0;
        settings->debug = boolean;
    } else if (hwr_ascii_equal_ci(key, "solid_color")) {
        /* Retired: there is no manufactured world frame to colour any more.
         * The key is still accepted so an existing configuration file keeps
         * loading as a whole. */
        if (!hwr_parse_u32(value, 16, 0x00ffffffu, &number))
            return 0;
    } else if (hwr_ascii_equal_ci(key, "internal_w")) {
        if (!hwr_parse_u32(value, 10, 16384u, &number))
            return 0;
        settings->internal_w = number;
    } else if (hwr_ascii_equal_ci(key, "internal_h")) {
        if (!hwr_parse_u32(value, 10, 16384u, &number))
            return 0;
        settings->internal_h = number;
    } else if (hwr_ascii_equal_ci(key, "window_mode")) {
        if (hwr_ascii_equal_ci(value, "default"))
            settings->window_mode = HWR_WINDOW_DEFAULT;
        else if (hwr_ascii_equal_ci(value, "windowed"))
            settings->window_mode = HWR_WINDOW_WINDOWED;
        else if (hwr_ascii_equal_ci(value, "borderless"))
            settings->window_mode = HWR_WINDOW_BORDERLESS;
        else
            return 0;
    } else if (hwr_ascii_equal_ci(key, "present_mode")) {
        if (hwr_ascii_equal_ci(value, "fifo"))
            settings->present_mode = HWR_PRESENT_FIFO;
        else if (hwr_ascii_equal_ci(value, "immediate"))
            settings->present_mode = HWR_PRESENT_IMMEDIATE;
        else if (hwr_ascii_equal_ci(value, "adaptive"))
            settings->present_mode = HWR_PRESENT_ADAPTIVE;
        else
            return 0;
    } else if (hwr_ascii_equal_ci(key, "min_present_interval_us")) {
        if (!hwr_parse_u32(value, 10, 10000000u, &number))
            return 0;
        settings->min_present_interval_us = number;
    } else if (hwr_ascii_equal_ci(key, "fps_cap")) {
        if (!hwr_parse_u32(value, 10, 1000000u, &number))
            return 0;
        settings->min_present_interval_us =
            number == 0 ? 0 : 1000000u / number;
    } else if (hwr_ascii_equal_ci(key, "shade_depth_scale")) {
        if (!hwr_parse_float(value, 4096.0f, &real) || real <= 0.0f)
            return 0;
        settings->shade_depth_scale = real;
    } else if (hwr_ascii_equal_ci(key, "fov_deg")) {
        if (!hwr_parse_float(value, 170.0f, &real) ||
            (real != 0.0f && real < 30.0f))
            return 0;
        settings->fov_deg = real;
    } else if (hwr_ascii_equal_ci(key, "pitch_mode")) {
        if (hwr_ascii_equal_ci(value, "horizon_shift"))
            settings->pitch_mode = HWR_PITCH_HORIZON_SHIFT;
        else if (hwr_ascii_equal_ci(value, "true_rotation"))
            settings->pitch_mode = HWR_PITCH_TRUE_ROTATION;
        else
            return 0;
    } else if (hwr_ascii_equal_ci(key, "render_mode")) {
        if (hwr_ascii_equal_ci(value, "authentic"))
            settings->render_mode = HWR_RENDER_AUTHENTIC;
        else if (hwr_ascii_equal_ci(value, "enhanced"))
            settings->render_mode = HWR_RENDER_ENHANCED;
        else
            return 0;
    } else {
        return -1;
    }
    return 1;
}

static uint64_t hwr_config_hash_bytes(
    uint64_t hash, const unsigned char *bytes, size_t count)
{
    size_t i;
    for (i = 0; i < count; i++) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

/* Parse into a complete temporary settings object. A malformed file is never
 * allowed to leave the live store half-updated. */
static int hwr_config_read(struct hwr_settings *settings,
                           uint64_t *content_hash, int report_errors)
{
    char line[512];
    FILE *file;
    unsigned int line_number = 0;
    uint64_t hash = UINT64_C(1469598103934665603);
    int valid = 1;

    hwr_settings_defaults(settings);
    file = fopen(g_config.path, "r");
    if (file == NULL)
        return 0;

    while (fgets(line, sizeof line, file) != NULL) {
        char *comment;
        char *equals;
        char *key;
        char *value;
        int result;
        size_t length;

        line_number++;
        length = strlen(line);
        hash = hwr_config_hash_bytes(
            hash, (const unsigned char *)line, length);
        if (length != 0 && line[length - 1] != '\n') {
            int extra = fgetc(file);
            if (extra != EOF) {
                unsigned char byte = (unsigned char)extra;
                hash = hwr_config_hash_bytes(hash, &byte, 1);
                while (extra != '\n' && (extra = fgetc(file)) != EOF) {
                    byte = (unsigned char)extra;
                    hash = hwr_config_hash_bytes(hash, &byte, 1);
                }
                if (report_errors)
                    fprintf(stderr,
                            "[renderer_overhaul] %s:%u: line is too long\n",
                            g_config.path, line_number);
                valid = 0;
                continue;
            }
        }
        comment = strchr(line, '#');
        if (comment != NULL)
            *comment = 0;
        key = hwr_trim(line);
        if (*key == 0)
            continue;
        equals = strchr(key, '=');
        if (equals == NULL) {
            if (report_errors)
                fprintf(stderr,
                        "[renderer_overhaul] %s:%u: expected key=value\n",
                        g_config.path, line_number);
            valid = 0;
            continue;
        }
        *equals = 0;
        value = hwr_trim(equals + 1);
        key = hwr_trim(key);
        result = hwr_apply_config_value(settings, key, value);
        if (result == 0) {
            if (report_errors)
                fprintf(stderr,
                        "[renderer_overhaul] %s:%u: invalid value for '%s'\n",
                        g_config.path, line_number, key);
            valid = 0;
        } else if (result < 0) {
            if (report_errors)
                fprintf(stderr,
                        "[renderer_overhaul] %s:%u: unknown setting '%s'\n",
                        g_config.path, line_number, key);
            valid = 0;
        }
    }
    if (ferror(file))
        valid = 0;
    fclose(file);
    *content_hash = hash;
    return valid ? 1 : -1;
}

void hwr_config_load(const char *plugin_dir)
{
    struct hwr_settings settings;
    uint64_t hash = 0;
    int result;

    memset(&g_config, 0, sizeof g_config);
    if (plugin_dir == NULL)
        plugin_dir = "";
    snprintf(g_config.path, sizeof g_config.path,
             "%s/renderer_overhaul.cfg", plugin_dir);
    result = hwr_config_read(&settings, &hash, 1);
    if (result == 0)
        return;
    g_config.active_hash = hash;
    if (result < 0) {
        fprintf(stderr,
                "[renderer_overhaul] configuration contains errors; keeping defaults\n");
        return;
    }
    hwr_settings_replace(&settings);
}

void hwr_config_poll(void)
{
    struct hwr_settings settings;
    struct hwr_settings confirmed;
    uint64_t hash = 0;
    uint64_t confirmed_hash = 0;
    int result;
    int confirmed_result;

    if (g_config.path[0] == 0)
        return;
    if (g_config.frames_until_poll != 0) {
        g_config.frames_until_poll--;
        return;
    }
    g_config.frames_until_poll = HWR_CONFIG_POLL_FRAMES;

    result = hwr_config_read(&settings, &hash, 0);
    if (result == 0)
        return;
    if (hash == g_config.active_hash) {
        g_config.candidate_polls = 0;
        return;
    }
    if (hash != g_config.candidate_hash) {
        g_config.candidate_hash = hash;
        g_config.candidate_polls = 1;
        return;
    }
    if (g_config.candidate_polls++ < 1)
        return;

    /* Require the changed file to survive two polls, then read it once more
     * while reporting errors. This avoids applying a direct editor's
     * truncate-and-rewrite intermediate state. */
    confirmed_result =
        hwr_config_read(&confirmed, &confirmed_hash, 1);
    if (confirmed_result == 0)
        return;
    if (confirmed_hash != hash) {
        g_config.candidate_hash = confirmed_hash;
        g_config.candidate_polls = 1;
        return;
    }
    g_config.active_hash = confirmed_hash;
    g_config.candidate_polls = 0;
    if (confirmed_result < 0) {
        fprintf(stderr,
                "[renderer_overhaul] configuration reload rejected; keeping current settings\n");
        return;
    }
    hwr_settings_replace(&confirmed);
    fprintf(stderr, "[renderer_overhaul] configuration reloaded\n");
}
