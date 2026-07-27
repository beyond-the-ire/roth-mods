#include "debug_config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct debug_key_name {
    const char *name;
    uint8_t scancode;
};

static const struct debug_key_name DEBUG_KEYS[] = {
    {"esc", 0x01},
    {"1", 0x02}, {"2", 0x03}, {"3", 0x04}, {"4", 0x05}, {"5", 0x06},
    {"6", 0x07}, {"7", 0x08}, {"8", 0x09}, {"9", 0x0a}, {"0", 0x0b},
    {"minus", 0x0c}, {"equals", 0x0d}, {"backspace", 0x0e}, {"tab", 0x0f},
    {"q", 0x10}, {"w", 0x11}, {"e", 0x12}, {"r", 0x13}, {"t", 0x14},
    {"y", 0x15}, {"u", 0x16}, {"i", 0x17}, {"o", 0x18}, {"p", 0x19},
    {"lbracket", 0x1a}, {"rbracket", 0x1b}, {"enter", 0x1c}, {"ctrl", 0x1d},
    {"a", 0x1e}, {"s", 0x1f}, {"d", 0x20}, {"f", 0x21}, {"g", 0x22},
    {"h", 0x23}, {"j", 0x24}, {"k", 0x25}, {"l", 0x26},
    {"semicolon", 0x27}, {"quote", 0x28}, {"grave", 0x29}, {"lshift", 0x2a},
    {"backslash", 0x2b}, {"z", 0x2c}, {"x", 0x2d}, {"c", 0x2e},
    {"v", 0x2f}, {"b", 0x30}, {"n", 0x31}, {"m", 0x32},
    {"comma", 0x33}, {"period", 0x34}, {"slash", 0x35}, {"rshift", 0x36},
    {"alt", 0x38}, {"space", 0x39}, {"capslock", 0x3a},
    {"f1", 0x3b}, {"f2", 0x3c}, {"f3", 0x3d}, {"f4", 0x3e},
    {"f5", 0x3f}, {"f6", 0x40}, {"f7", 0x41}, {"f8", 0x42},
    {"f9", 0x43}, {"f10", 0x44}, {"f11", 0x57}, {"f12", 0x58}
};

static int debug_ascii_equal_ci(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static char *debug_trim(char *text)
{
    char *end;

    while (isspace((unsigned char)*text))
        text++;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return text;
}

static int debug_parse_bool(const char *text, int *value)
{
    if (debug_ascii_equal_ci(text, "1") ||
        debug_ascii_equal_ci(text, "true") ||
        debug_ascii_equal_ci(text, "yes") ||
        debug_ascii_equal_ci(text, "on")) {
        *value = 1;
        return 0;
    }
    if (debug_ascii_equal_ci(text, "0") ||
        debug_ascii_equal_ci(text, "false") ||
        debug_ascii_equal_ci(text, "no") ||
        debug_ascii_equal_ci(text, "off")) {
        *value = 0;
        return 0;
    }
    return -1;
}

static int debug_parse_key(const char *text, uint8_t *scancode)
{
    size_t i;

    for (i = 0; i < sizeof(DEBUG_KEYS) / sizeof(DEBUG_KEYS[0]); i++) {
        if (debug_ascii_equal_ci(text, DEBUG_KEYS[i].name)) {
            *scancode = DEBUG_KEYS[i].scancode;
            return 0;
        }
    }
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        char *end;
        unsigned long parsed;

        errno = 0;
        parsed = strtoul(text, &end, 16);
        if (errno == 0 && *end == '\0' && parsed > 0 && parsed <= 0x7f) {
            *scancode = (uint8_t)parsed;
            return 0;
        }
    }
    return -1;
}

static int debug_parse_anchor(const char *text, enum debug_anchor *anchor)
{
    if (debug_ascii_equal_ci(text, "top_left"))
        *anchor = DEBUG_ANCHOR_TOP_LEFT;
    else if (debug_ascii_equal_ci(text, "top_right"))
        *anchor = DEBUG_ANCHOR_TOP_RIGHT;
    else if (debug_ascii_equal_ci(text, "bottom_left"))
        *anchor = DEBUG_ANCHOR_BOTTOM_LEFT;
    else if (debug_ascii_equal_ci(text, "bottom_right"))
        *anchor = DEBUG_ANCHOR_BOTTOM_RIGHT;
    else
        return -1;
    return 0;
}

static int debug_parse_layout(const char *text,
                              enum debug_layout_mode *mode)
{
    if (debug_ascii_equal_ci(text, "off"))
        *mode = DEBUG_LAYOUT_OFF;
    else if (debug_ascii_equal_ci(text, "performance"))
        *mode = DEBUG_LAYOUT_PERFORMANCE;
    else if (debug_ascii_equal_ci(text, "inspector"))
        *mode = DEBUG_LAYOUT_INSPECTOR;
    else if (debug_ascii_equal_ci(text, "detailed"))
        *mode = DEBUG_LAYOUT_DETAILED;
    else if (debug_ascii_equal_ci(text, "all"))
        *mode = DEBUG_LAYOUT_ALL;
    else
        return -1;
    return 0;
}

const char *debug_layout_mode_name(enum debug_layout_mode mode)
{
    switch (mode) {
    case DEBUG_LAYOUT_PERFORMANCE: return "PERFORMANCE";
    case DEBUG_LAYOUT_INSPECTOR: return "INSPECTOR";
    case DEBUG_LAYOUT_DETAILED: return "DETAILED";
    case DEBUG_LAYOUT_ALL: return "ALL";
    default: return "OFF";
    }
}

void debug_config_defaults(struct debug_config *config)
{
    memset(config, 0, sizeof(*config));
    config->enabled = 1;
    config->initial_mode = DEBUG_LAYOUT_PERFORMANCE;
    config->show_present_rate = 1;
    config->show_tick_rate = 1;
    config->show_compose_rate = 1;
    config->show_game_rate = 0;
    config->show_frame_intervals = 1;
    config->show_window_state = 0;
    config->show_map = 1;
    config->show_player_location = 1;
    config->show_view = 1;
    config->show_cursor = 1;
    config->show_sector_details = 1;
    config->show_face_details = 1;
    config->update_hz = 4;
    config->anchor = DEBUG_ANCHOR_TOP_LEFT;
    config->background = 1;
    config->mode_key = 0x57;
}

static int debug_apply_bool(const char *key, const char *value, int *field)
{
    int parsed;

    if (debug_parse_bool(value, &parsed) != 0)
        return -1;
    *field = parsed;
    (void)key;
    return 0;
}

void debug_config_load(struct debug_config *config, const char *plugin_dir)
{
    char path[2600];
    char line[512];
    FILE *file;
    unsigned line_number = 0;

    debug_config_defaults(config);
    snprintf(path, sizeof(path), "%s/debug_overlay.cfg", plugin_dir);
    file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr,
                "[mods:debug_overlay] %s not found; using built-in defaults\n",
                path);
        return;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *comment;
        char *equals;
        char *key;
        char *value;
        int result = 0;

        line_number++;
        comment = strchr(line, '#');
        if (comment != NULL)
            *comment = '\0';
        key = debug_trim(line);
        if (*key == '\0')
            continue;
        equals = strchr(key, '=');
        if (equals == NULL) {
            fprintf(stderr,
                    "[mods:debug_overlay] %s:%u: expected key=value; ignored\n",
                    path, line_number);
            continue;
        }
        *equals = '\0';
        value = debug_trim(equals + 1);
        key = debug_trim(key);

        if (debug_ascii_equal_ci(key, "enabled"))
            result = debug_apply_bool(key, value, &config->enabled);
        else if (debug_ascii_equal_ci(key, "initial_mode"))
            result = debug_parse_layout(value, &config->initial_mode);
        else if (debug_ascii_equal_ci(key, "show_present_rate"))
            result = debug_apply_bool(key, value, &config->show_present_rate);
        else if (debug_ascii_equal_ci(key, "show_tick_rate"))
            result = debug_apply_bool(key, value, &config->show_tick_rate);
        else if (debug_ascii_equal_ci(key, "show_compose_rate"))
            result = debug_apply_bool(key, value, &config->show_compose_rate);
        else if (debug_ascii_equal_ci(key, "show_game_rate"))
            result = debug_apply_bool(key, value, &config->show_game_rate);
        else if (debug_ascii_equal_ci(key, "show_frame_intervals"))
            result = debug_apply_bool(key, value, &config->show_frame_intervals);
        else if (debug_ascii_equal_ci(key, "show_window_state"))
            result = debug_apply_bool(key, value, &config->show_window_state);
        else if (debug_ascii_equal_ci(key, "show_map"))
            result = debug_apply_bool(key, value, &config->show_map);
        else if (debug_ascii_equal_ci(key, "show_player_location"))
            result = debug_apply_bool(key, value, &config->show_player_location);
        else if (debug_ascii_equal_ci(key, "show_view"))
            result = debug_apply_bool(key, value, &config->show_view);
        else if (debug_ascii_equal_ci(key, "show_cursor"))
            result = debug_apply_bool(key, value, &config->show_cursor);
        else if (debug_ascii_equal_ci(key, "show_sector_details"))
            result = debug_apply_bool(key, value,
                                      &config->show_sector_details);
        else if (debug_ascii_equal_ci(key, "show_face_details"))
            result = debug_apply_bool(key, value, &config->show_face_details);
        else if (debug_ascii_equal_ci(key, "background"))
            result = debug_apply_bool(key, value, &config->background);
        else if (debug_ascii_equal_ci(key, "anchor"))
            result = debug_parse_anchor(value, &config->anchor);
        else if (debug_ascii_equal_ci(key, "mode_key"))
            result = debug_parse_key(value, &config->mode_key);
        else if (debug_ascii_equal_ci(key, "update_hz")) {
            char *end;
            unsigned long parsed;

            errno = 0;
            parsed = strtoul(value, &end, 10);
            if (errno != 0 || *value == '\0' || *end != '\0')
                result = -1;
            else {
                if (parsed < 1)
                    parsed = 1;
                if (parsed > 20)
                    parsed = 20;
                config->update_hz = (uint32_t)parsed;
            }
        } else {
            fprintf(stderr,
                    "[mods:debug_overlay] %s:%u: unknown setting '%s'; ignored\n",
                    path, line_number, key);
            continue;
        }

        if (result != 0) {
            fprintf(stderr,
                    "[mods:debug_overlay] %s:%u: invalid value '%s' for %s; "
                    "keeping the safe value\n",
                    path, line_number, value, key);
        }
    }
    fclose(file);
}
