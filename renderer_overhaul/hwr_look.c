#include "hwr_look.h"

#include "hwr_util.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HWR_LOOK_POLL_FRAMES 30u
/* The table index is carried in a sixty-four bit mask, exactly as the settings
 * file's own bookkeeping carries it, so a row past that cannot be overridden.
 * The table is nowhere near that long and a row that ever is says so in a log
 * line rather than being silently dropped. */
#define HWR_LOOK_MAX_ROWS 64u
#define HWR_LOOK_MAX_PROFILES 16u
#define HWR_LOOK_MAX_MAPS 64u
#define HWR_LOOK_NAME 64u

struct hwr_look_values {
    uint64_t present;
    union hwr_value value[HWR_LOOK_MAX_ROWS];
};

struct hwr_look_profile {
    char name[HWR_LOOK_NAME];
    struct hwr_look_values values;
};

struct hwr_look_map {
    char name[HWR_LOOK_NAME];
    char profile[HWR_LOOK_NAME];
    struct hwr_look_values values;
};

struct hwr_look_file {
    struct hwr_look_values global;
    struct hwr_look_profile profiles[HWR_LOOK_MAX_PROFILES];
    size_t profile_count;
    struct hwr_look_map maps[HWR_LOOK_MAX_MAPS];
    size_t map_count;
};

static char g_look_path[2600];
static struct hwr_look_file g_look;
static int g_look_present;
static struct hwr_look_values g_effective;
static char g_look_map[HWR_LOOK_NAME];
static uint64_t g_look_hash;
static uint64_t g_look_candidate_hash;
static uint32_t g_look_candidate_polls;
static uint32_t g_look_frames_until_poll;
static int g_look_reload_pending;

/* A section prefix, spelled in whatever case the file spelled it. */
static int hwr_look_prefix_ci(const char *text, const char *prefix)
{
    size_t i;

    for (i = 0; prefix[i] != 0; i++) {
        if (tolower((unsigned char)text[i]) != (unsigned char)prefix[i])
            return 0;
    }
    return 1;
}

static char *hwr_look_trim(char *text)
{
    char *end;

    while (*text != 0 && isspace((unsigned char)*text))
        text++;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]))
        *--end = 0;
    return text;
}

/* A map's name reduced to what it is matched on: no directory and no
 * extension, whichever side it came from. Both the section names in the file
 * and the name the engine holds go through here, so STUDY1, study1.raw and
 * m\STUDY1.RAW are one map however either side spells it. */
static void hwr_look_map_name(char *out, size_t out_size, const char *name)
{
    const char *base;
    const char *cursor;
    size_t i;

    out[0] = 0;
    if (name == NULL)
        return;
    base = name;
    for (cursor = name; *cursor != 0; cursor++) {
        if (*cursor == '/' || *cursor == '\\' || *cursor == ':')
            base = cursor + 1;
    }
    snprintf(out, out_size, "%s", base);
    for (i = 0; out[i] != 0; i++) {
        if (out[i] == '.') {
            out[i] = 0;
            break;
        }
    }
}

static int hwr_look_row_index(const struct hwr_setting_desc *desc)
{
    int index = hwr_schema_index_of(desc);

    if (index < 0 || (unsigned)index >= HWR_LOOK_MAX_ROWS)
        return -1;
    return index;
}

static void hwr_look_set(struct hwr_look_values *values, int index,
                         union hwr_value value)
{
    values->present |= UINT64_C(1) << index;
    values->value[index] = value;
}

static void hwr_look_merge(struct hwr_look_values *into,
                           const struct hwr_look_values *from)
{
    unsigned i;

    for (i = 0; i < HWR_LOOK_MAX_ROWS; i++) {
        if ((from->present & (UINT64_C(1) << i)) != 0)
            hwr_look_set(into, (int)i, from->value[i]);
    }
}

static const struct hwr_look_profile *hwr_look_find_profile(
    const struct hwr_look_file *file, const char *name)
{
    size_t i;

    for (i = 0; i < file->profile_count; i++) {
        if (hwr_ascii_equal_ci(file->profiles[i].name, name))
            return &file->profiles[i];
    }
    return NULL;
}

static const struct hwr_look_map *hwr_look_find_map(
    const struct hwr_look_file *file, const char *name)
{
    size_t i;

    if (name == NULL || name[0] == 0)
        return NULL;
    for (i = 0; i < file->map_count; i++) {
        if (hwr_ascii_equal_ci(file->maps[i].name, name))
            return &file->maps[i];
    }
    return NULL;
}

/* One line's key against the settings table. A look file may only carry rows
 * that say a map is allowed to override them, so there is no second list of
 * keys to keep in step with the first. */
static int hwr_look_accept_key(const char *key, const char *value,
                               struct hwr_look_values *into,
                               unsigned line_number, int report)
{
    const struct hwr_setting_desc *desc = hwr_schema_find(key);
    union hwr_value parsed;
    char message[128];
    int index;

    if (desc == NULL || (desc->flags & HWR_SF_MAP_OVERRIDE) == 0) {
        if (report)
            fprintf(stderr, "[renderer_overhaul] %s:%u: '%s' is not a look "
                            "setting; ignored\n",
                    g_look_path, line_number, key);
        return 0;
    }
    index = hwr_look_row_index(desc);
    if (index < 0) {
        if (report)
            fprintf(stderr, "[renderer_overhaul] %s:%u: '%s' cannot be "
                            "overridden; ignored\n",
                    g_look_path, line_number, key);
        return 0;
    }
    message[0] = 0;
    if (!hwr_schema_parse(desc, value, &parsed, message, sizeof message)) {
        if (report)
            fprintf(stderr, "[renderer_overhaul] %s:%u: '%s': %s; ignored\n",
                    g_look_path, line_number, key, message);
        return 0;
    }
    hwr_look_set(into, index, parsed);
    return 1;
}

/* Whole file into a temporary object, so a file being edited can never leave
 * half its overrides standing. Returns 1 when the file was read (whatever its
 * lines said), 0 when there is no file to read. */
static int hwr_look_read(struct hwr_look_file *file, uint64_t *content_hash,
                         int report)
{
    char line[512];
    FILE *stream;
    unsigned line_number = 0;
    uint64_t hash = UINT64_C(1469598103934665603);
    struct hwr_look_values *section = &file->global;
    struct hwr_look_map *map_section = NULL;

    memset(file, 0, sizeof *file);
    stream = fopen(g_look_path, "r");
    if (stream == NULL)
        return 0;
    while (fgets(line, sizeof line, stream) != NULL) {
        char *comment;
        char *text;
        char *equals;
        size_t i;
        size_t length = strlen(line);

        line_number++;
        for (i = 0; i < length; i++) {
            hash ^= (unsigned char)line[i];
            hash *= UINT64_C(1099511628211);
        }
        comment = strchr(line, '#');
        if (comment != NULL)
            *comment = 0;
        text = hwr_look_trim(line);
        if (*text == 0)
            continue;
        if (*text == '[') {
            char *close = strchr(text, ']');
            char *name;

            if (close == NULL) {
                if (report)
                    fprintf(stderr, "[renderer_overhaul] %s:%u: expected "
                                    "[section]; ignored\n",
                            g_look_path, line_number);
                continue;
            }
            *close = 0;
            name = hwr_look_trim(text + 1);
            map_section = NULL;
            if (hwr_ascii_equal_ci(name, "look")) {
                section = &file->global;
            } else if (hwr_look_prefix_ci(name, "profile.")) {
                if (file->profile_count >= HWR_LOOK_MAX_PROFILES) {
                    if (report)
                        fprintf(stderr, "[renderer_overhaul] %s:%u: too many "
                                        "profiles; ignored\n",
                                g_look_path, line_number);
                    section = NULL;
                    continue;
                }
                section = &file->profiles[file->profile_count].values;
                snprintf(file->profiles[file->profile_count].name,
                         HWR_LOOK_NAME, "%s", name + 8);
                file->profile_count++;
            } else if (hwr_look_prefix_ci(name, "map.")) {
                if (file->map_count >= HWR_LOOK_MAX_MAPS) {
                    if (report)
                        fprintf(stderr, "[renderer_overhaul] %s:%u: too many "
                                        "maps; ignored\n",
                                g_look_path, line_number);
                    section = NULL;
                    continue;
                }
                map_section = &file->maps[file->map_count];
                section = &map_section->values;
                hwr_look_map_name(map_section->name, HWR_LOOK_NAME, name + 4);
                file->map_count++;
            } else {
                if (report)
                    fprintf(stderr, "[renderer_overhaul] %s:%u: '%s' is not a "
                                    "look section; ignored\n",
                            g_look_path, line_number, name);
                section = NULL;
            }
            continue;
        }
        equals = strchr(text, '=');
        if (equals == NULL) {
            if (report)
                fprintf(stderr, "[renderer_overhaul] %s:%u: expected "
                                "key=value; ignored\n",
                        g_look_path, line_number);
            continue;
        }
        *equals = 0;
        {
            char *key = hwr_look_trim(text);
            char *value = hwr_look_trim(equals + 1);

            if (section == NULL)
                continue;
            if (map_section != NULL && hwr_ascii_equal_ci(key, "profile")) {
                snprintf(map_section->profile, HWR_LOOK_NAME, "%s", value);
                continue;
            }
            hwr_look_accept_key(key, value, section, line_number, report);
        }
    }
    fclose(stream);
    *content_hash = hash;
    return 1;
}

/* The overrides in force for one map, lowest precedence first. */
static void hwr_look_compose(const struct hwr_look_file *file,
                             const char *map_name,
                             struct hwr_look_values *out)
{
    const struct hwr_look_map *map;

    memset(out, 0, sizeof *out);
    if (!g_look_present)
        return;
    hwr_look_merge(out, &file->global);
    map = hwr_look_find_map(file, map_name);
    if (map == NULL)
        return;
    if (map->profile[0] != 0) {
        const struct hwr_look_profile *profile =
            hwr_look_find_profile(file, map->profile);

        if (profile != NULL)
            hwr_look_merge(out, &profile->values);
        else
            fprintf(stderr, "[renderer_overhaul] %s: map '%s' selects the "
                            "unknown profile '%s'; ignored\n",
                    g_look_path, map->name, map->profile);
    }
    hwr_look_merge(out, &map->values);
}

/* Publishes a freshly composed override set and reports what moved, so the rows
 * that changed value are applied and nothing else is disturbed. */
static void hwr_look_publish(const struct hwr_look_values *next)
{
    uint32_t changed = 0;
    unsigned i;

    for (i = 0; i < HWR_LOOK_MAX_ROWS; i++) {
        uint64_t bit = UINT64_C(1) << i;
        int had = (g_effective.present & bit) != 0;
        int has = (next->present & bit) != 0;
        const struct hwr_setting_desc *desc;

        if (!had && !has)
            continue;
        desc = hwr_schema_at(i);
        if (desc == NULL)
            continue;
        if (had && has &&
            memcmp(&g_effective.value[i], &next->value[i],
                   sizeof next->value[i]) == 0)
            continue;
        changed |= desc->change_bit;
    }
    g_effective = *next;
    if (changed != 0)
        hwr_settings_mark_changed(changed);
}

void hwr_look_load(const char *plugin_dir)
{
    uint64_t hash = 0;

    memset(&g_look, 0, sizeof g_look);
    memset(&g_effective, 0, sizeof g_effective);
    g_look_present = 0;
    g_look_map[0] = 0;
    if (plugin_dir == NULL)
        plugin_dir = "";
    snprintf(g_look_path, sizeof g_look_path, "%s/renderer_look.cfg",
             plugin_dir);
    if (!hwr_look_read(&g_look, &hash, 1)) {
        fprintf(stderr, "[renderer_overhaul] no look file at %s; the settings "
                        "in force describe every map\n", g_look_path);
        return;
    }
    g_look_present = 1;
    g_look_hash = hash;
    fprintf(stderr, "[renderer_overhaul] look file %s: %u profile(s), "
                    "%u map override(s)\n",
            g_look_path, (unsigned)g_look.profile_count,
            (unsigned)g_look.map_count);
    hwr_look_select_map(NULL);
}

void hwr_look_request_reload(void)
{
    if (g_look_path[0] == 0)
        return;
    g_look_reload_pending = 1;
}

void hwr_look_poll(void)
{
    static int watch = -1;
    struct hwr_look_file *probe;
    uint64_t hash = 0;

    if (watch < 0) {
        const char *setting = getenv("HWR_CFG_WATCH");
        watch = setting != NULL && setting[0] != '0' && setting[0] != 0 ? 1 : 0;
    }
    if (!watch || g_look_path[0] == 0)
        return;
    if (g_look_frames_until_poll != 0) {
        g_look_frames_until_poll--;
        return;
    }
    g_look_frames_until_poll = HWR_LOOK_POLL_FRAMES;
    probe = (struct hwr_look_file *)malloc(sizeof *probe);
    if (probe == NULL)
        return;
    if (!hwr_look_read(probe, &hash, 0)) {
        free(probe);
        return;
    }
    free(probe);
    if (hash == g_look_hash) {
        g_look_candidate_polls = 0;
        return;
    }
    if (hash != g_look_candidate_hash) {
        g_look_candidate_hash = hash;
        g_look_candidate_polls = 1;
        return;
    }
    /* Two polls of the same content before asking for it, so an editor's
     * truncate-and-rewrite intermediate state is never what gets loaded. */
    if (g_look_candidate_polls++ < 1)
        return;
    g_look_reload_pending = 1;
}

void hwr_look_drain_request(void)
{
    struct hwr_look_file *next;
    uint64_t hash = 0;

    if (!g_look_reload_pending)
        return;
    g_look_reload_pending = 0;
    next = (struct hwr_look_file *)malloc(sizeof *next);
    if (next == NULL)
        return;
    if (!hwr_look_read(next, &hash, 1)) {
        free(next);
        if (g_look_present) {
            /* The file was there and is not any more: the global look goes with
             * it rather than standing over a file nobody has. */
            struct hwr_look_values empty;

            memset(&empty, 0, sizeof empty);
            memset(&g_look, 0, sizeof g_look);
            g_look_present = 0;
            hwr_look_publish(&empty);
            fprintf(stderr, "[renderer_overhaul] look file %s is gone; the "
                            "settings in force describe every map\n",
                    g_look_path);
        }
        return;
    }
    g_look = *next;
    free(next);
    g_look_present = 1;
    g_look_hash = hash;
    g_look_candidate_polls = 0;
    fprintf(stderr, "[renderer_overhaul] look file reloaded\n");
    hwr_look_select_map(g_look_map[0] != 0 ? g_look_map : NULL);
}

void hwr_look_select_map(const char *map_name)
{
    struct hwr_look_values next;

    hwr_look_map_name(g_look_map, HWR_LOOK_NAME, map_name);
    hwr_look_compose(&g_look, g_look_map, &next);
    hwr_look_publish(&next);
}

union hwr_value hwr_look_effective(const struct hwr_setting_desc *desc,
                                   const struct hwr_settings *settings)
{
    int index = hwr_look_row_index(desc);

    if (index >= 0 && (g_effective.present & (UINT64_C(1) << index)) != 0)
        return g_effective.value[index];
    return hwr_schema_get(settings, desc);
}

int hwr_look_overrides(const struct hwr_setting_desc *desc)
{
    int index = hwr_look_row_index(desc);

    return index >= 0 &&
           (g_effective.present & (UINT64_C(1) << index)) != 0;
}

void hwr_look_clear(void)
{
    memset(&g_look, 0, sizeof g_look);
    memset(&g_effective, 0, sizeof g_effective);
    g_look_present = 0;
    g_look_path[0] = 0;
    g_look_map[0] = 0;
    g_look_hash = 0;
    g_look_candidate_hash = 0;
    g_look_candidate_polls = 0;
    g_look_reload_pending = 0;
}
