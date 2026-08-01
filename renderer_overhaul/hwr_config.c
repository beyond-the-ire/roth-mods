#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "hwr_config.h"

#include "hwr_schema.h"
#include "hwr_settings.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#define HWR_CONFIG_POLL_FRAMES 30u
/* Column the generated comment blocks wrap at, matching the shipped file. */
#define HWR_CONFIG_COMMENT_WIDTH 78u

struct hwr_config_state {
    char path[2600];
    uint64_t active_hash;
    uint64_t candidate_hash;
    uint32_t candidate_polls;
    uint32_t frames_until_poll;
    /* One reload slot: a second request before the first is carried out
     * replaces it instead of queueing behind it. */
    int reload_pending;
    /* The content the watcher saw settle, so the reload can tell a file that
     * changed again from the one it was asked to load. A request that did not
     * come from the watcher carries no expectation. */
    int reload_expects_hash;
    uint64_t reload_hash;
    /* One note per retired key per session, by table index. */
    uint64_t retired_noted;
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

static int hwr_config_row_index(const struct hwr_setting_desc *desc)
{
    size_t count = hwr_schema_count();
    size_t i;

    for (i = 0; i < count && i < 64; i++) {
        if (hwr_schema_at(i) == desc)
            return (int)i;
    }
    return -1;
}

/* One key=value pair against the schema: 1 accepted, 0 invalid value,
 * -1 unknown key. Every bound, token, and transform comes from the table, so
 * the file and the settings overlay can only ever agree. */
static int hwr_apply_config_value(struct hwr_settings *settings,
                                  const char *key, const char *value,
                                  const struct hwr_setting_desc **matched)
{
    const struct hwr_setting_desc *desc = hwr_schema_find(key);
    union hwr_value parsed;

    if (matched != NULL)
        *matched = desc;
    if (desc == NULL)
        return -1;
    if (!hwr_schema_parse(desc, value, &parsed, NULL, 0))
        return 0;
    /* A retired key is parsed for its file's sake and then discarded. */
    if (desc->exposure != HWR_EXP_RETIRED)
        hwr_schema_set(settings, desc, parsed);
    return 1;
}

/* A retired key costs its owner one line of explanation, once. */
static void hwr_note_retired(const struct hwr_setting_desc *desc,
                             unsigned int line_number)
{
    int index = hwr_config_row_index(desc);

    if (index < 0 || (g_config.retired_noted & (UINT64_C(1) << index)) != 0)
        return;
    g_config.retired_noted |= UINT64_C(1) << index;
    fprintf(stderr,
            "[renderer_overhaul] %s:%u: '%s' is retired and has no effect; %s\n",
            g_config.path, line_number, desc->key, desc->help);
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
        const struct hwr_setting_desc *desc;
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
        result = hwr_apply_config_value(settings, key, value, &desc);
        if (result > 0 && desc->exposure == HWR_EXP_RETIRED && report_errors) {
            hwr_note_retired(desc, line_number);
        } else if (result == 0) {
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
    if (result == 0) {
        /* Not a failure: the built-in defaults are a complete configuration,
         * and the file can be written from the settings themselves. */
        fprintf(stderr,
                "[renderer_overhaul] no configuration file at %s; using defaults\n",
                g_config.path);
        return;
    }
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
    uint64_t hash = 0;
    int result;

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

    /* Require the changed file to survive two polls before asking for it, so a
     * direct editor's truncate-and-rewrite intermediate state is never the
     * content that gets loaded. Asking is all the watcher does: the request
     * below is the one path from the file into the store. */
    g_config.reload_pending = 1;
    g_config.reload_expects_hash = 1;
    g_config.reload_hash = hash;
}

void hwr_config_request_reload(void)
{
    if (g_config.path[0] == 0)
        return;
    g_config.reload_pending = 1;
    g_config.reload_expects_hash = 0;
}

void hwr_config_drain_request(void)
{
    struct hwr_settings settings;
    uint64_t hash = 0;
    uint64_t expected;
    int expects_hash;
    int result;

    if (!g_config.reload_pending)
        return;
    g_config.reload_pending = 0;
    expects_hash = g_config.reload_expects_hash;
    expected = g_config.reload_hash;
    g_config.reload_expects_hash = 0;

    /* Read the file once more, this time reporting what is wrong with it. */
    result = hwr_config_read(&settings, &hash, 1);
    if (result == 0) {
        if (!expects_hash)
            fprintf(stderr,
                    "[renderer_overhaul] configuration file is unavailable: %s\n",
                    g_config.path);
        return;
    }
    if (expects_hash && hash != expected) {
        /* It changed again between settling and being read, so let it settle
         * once more rather than loading a file nobody has seen twice. */
        g_config.candidate_hash = hash;
        g_config.candidate_polls = 1;
        return;
    }
    g_config.active_hash = hash;
    g_config.candidate_polls = 0;
    if (result < 0) {
        fprintf(stderr,
                "[renderer_overhaul] configuration reload rejected; keeping current settings\n");
        return;
    }
    hwr_settings_replace(&settings);
    fprintf(stderr, "[renderer_overhaul] configuration reloaded\n");
}

static int hwr_config_sync(FILE *file)
{
#ifdef _WIN32
    return _commit(_fileno(file));
#else
    return fsync(fileno(file));
#endif
}

/* An atomic replace, which a plain rename is not on Windows: there it fails
 * outright once the destination exists. */
static int hwr_config_replace_file(const char *temp_path, const char *path)
{
#ifdef _WIN32
    if (!MoveFileExA(temp_path, path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return -1;
    return 0;
#else
    if (rename(temp_path, path) != 0)
        return -1;
    return 0;
#endif
}

/* Which keys the file being replaced carries, by table index. An advanced key
 * somebody wrote by hand is kept; one they never asked for is not added. */
static uint64_t hwr_config_present_keys(void)
{
    char line[512];
    FILE *file;
    uint64_t present = 0;

    file = fopen(g_config.path, "r");
    if (file == NULL)
        return 0;
    while (fgets(line, sizeof line, file) != NULL) {
        char *comment = strchr(line, '#');
        char *equals;
        int index;

        if (comment != NULL)
            *comment = 0;
        equals = strchr(line, '=');
        if (equals == NULL)
            continue;
        *equals = 0;
        index = hwr_config_row_index(hwr_schema_find(hwr_trim(line)));
        if (index >= 0)
            present |= UINT64_C(1) << index;
    }
    fclose(file);
    return present;
}

static int hwr_config_write_comment(FILE *file, const char *text)
{
    size_t start = 0;

    while (text[start] != 0) {
        size_t end = start;
        size_t last_space = start;

        while (text[end] != 0 &&
               end - start < HWR_CONFIG_COMMENT_WIDTH - 2u) {
            if (text[end] == ' ')
                last_space = end;
            end++;
        }
        if (text[end] != 0 && last_space > start)
            end = last_space;
        if (fprintf(file, "# %.*s\n", (int)(end - start), text + start) < 0)
            return -1;
        start = end;
        while (text[start] == ' ')
            start++;
    }
    return 0;
}

static int hwr_config_write_row(FILE *file, const struct hwr_settings *settings,
                                const struct hwr_setting_desc *desc)
{
    char text[128];

    if (!hwr_schema_format(desc, hwr_schema_get(settings, desc), text,
                           sizeof text))
        return -1;
    if (fputc('\n', file) == EOF)
        return -1;
    if (desc->help != NULL && hwr_config_write_comment(file, desc->help) != 0)
        return -1;
    if (fprintf(file, "%s=%s\n", desc->key, text) < 0)
        return -1;
    return 0;
}

/* The file is the table in category order: every consumer row with its help as
 * its comment, then the advanced rows the previous file carried. Retired keys
 * are never written, so the first save is also what removes them. */
static int hwr_config_write_stream(FILE *file,
                                   const struct hwr_settings *settings,
                                   uint64_t keep_advanced)
{
    size_t count = hwr_schema_count();
    size_t i;
    int category;
    int wrote_advanced_header = 0;

    if (fprintf(file,
                "# Renderer Overhaul settings, written by the renderer.\n"
                "# Lines are key=value, keys are case-insensitive, and '#' starts\n"
                "# a comment.\n") < 0)
        return -1;
    for (category = (int)HWR_CAT_DISPLAY; category <= (int)HWR_CAT_SYSTEM;
         category++) {
        for (i = 0; i < count; i++) {
            const struct hwr_setting_desc *desc = hwr_schema_at(i);

            if (desc->key == NULL || (int)desc->category != category)
                continue;
            /* An alias is the spelling a person uses for the field its
             * canonical row owns, so it is the one that gets written. */
            if (desc->exposure != HWR_EXP_CONSUMER &&
                desc->exposure != HWR_EXP_ALIAS)
                continue;
            if (hwr_config_write_row(file, settings, desc) != 0)
                return -1;
        }
    }
    for (i = 0; i < count; i++) {
        const struct hwr_setting_desc *desc = hwr_schema_at(i);
        int index = (int)i;

        if (desc->key == NULL || desc->exposure != HWR_EXP_ADVANCED)
            continue;
        if (index >= 64 || (keep_advanced & (UINT64_C(1) << index)) == 0)
            continue;
        if (!wrote_advanced_header) {
            if (fprintf(file, "\n# Kept from the previous file. A later line "
                              "wins where two keys\n# describe the same "
                              "thing.\n") < 0)
                return -1;
            wrote_advanced_header = 1;
        }
        if (hwr_config_write_row(file, settings, desc) != 0)
            return -1;
    }
    return 0;
}

int hwr_config_save(const struct hwr_settings *settings)
{
    char temp_path[2620];
    uint64_t keep_advanced;
    FILE *file;
    int written;

    if (settings == NULL || g_config.path[0] == 0)
        return -1;
    keep_advanced = hwr_config_present_keys();
    snprintf(temp_path, sizeof temp_path, "%s.tmp", g_config.path);
    file = fopen(temp_path, "wb");
    if (file == NULL) {
        fprintf(stderr, "[renderer_overhaul] cannot write %s: %s\n", temp_path,
                strerror(errno));
        return -1;
    }
    written = hwr_config_write_stream(file, settings, keep_advanced) == 0;
    if (written && fflush(file) != 0)
        written = 0;
    if (written && hwr_config_sync(file) != 0)
        written = 0;
    if (fclose(file) != 0)
        written = 0;
    if (!written) {
        remove(temp_path);
        fprintf(stderr, "[renderer_overhaul] cannot write %s\n", temp_path);
        return -1;
    }
    /* Until this succeeds the previous file is still the whole configuration,
     * and if it fails it stays that way. */
    if (hwr_config_replace_file(temp_path, g_config.path) != 0) {
        remove(temp_path);
        fprintf(stderr, "[renderer_overhaul] cannot replace %s\n",
                g_config.path);
        return -1;
    }
    return 0;
}
