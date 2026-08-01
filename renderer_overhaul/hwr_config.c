#include "hwr_config.h"

#include "hwr_schema.h"
#include "hwr_settings.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
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

/* One key=value pair against the schema: 1 accepted, 0 invalid value,
 * -1 unknown key. Every bound, token, and transform comes from the table, so
 * the file and the settings overlay can only ever agree. */
static int hwr_apply_config_value(struct hwr_settings *settings,
                                  const char *key, const char *value)
{
    const struct hwr_setting_desc *desc = hwr_schema_find(key);
    union hwr_value parsed;

    if (desc == NULL)
        return -1;
    if (!hwr_schema_parse(desc, value, &parsed, NULL, 0))
        return 0;
    /* A retired key is parsed for its file's sake and then discarded. */
    if (desc->exposure != HWR_EXP_RETIRED)
        hwr_schema_set(settings, desc, parsed);
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
            /* An unknown key costs its own line and nothing else: a file
             * written by a later build must not take every setting it shares
             * with this one down with it. */
            if (report_errors)
                fprintf(stderr,
                        "[renderer_overhaul] %s:%u: unknown setting '%s'; ignored\n",
                        g_config.path, line_number, key);
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
