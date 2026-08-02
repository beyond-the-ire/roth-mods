#include "hwr_look.h"

#include "hwr_util.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HWR_LOOK_POLL_FRAMES 30u
/* One override per row of the settings table, held by table position in the
 * same row set the settings file's own bookkeeping uses, so a row this file may
 * override is every row the table marks as one -- wherever in the table it
 * happens to sit. */
#define HWR_LOOK_MAX_ROWS HWR_ROW_SET_MAX
#define HWR_LOOK_MAX_PROFILES 16u
#define HWR_LOOK_MAX_MAPS 64u
#define HWR_LOOK_NAME 64u
/* How many graphics one section may say something about. A map has a handful
 * of things that give light; this is room for a map that is nothing but. */
#define HWR_LOOK_MAX_LIGHTS 64u

struct hwr_look_values {
    struct hwr_row_set present;
    union hwr_value value[HWR_LOOK_MAX_ROWS];
};

/* One `graphic = intensity, range, colour` line, kept with each field's own
 * presence so that composing two sections is per field rather than per line. */
struct hwr_look_light_entry {
    uint16_t graphic_id;
    struct hwr_look_light light;
};

struct hwr_look_lights {
    struct hwr_look_light_entry items[HWR_LOOK_MAX_LIGHTS];
    size_t count;
};

struct hwr_look_profile {
    char name[HWR_LOOK_NAME];
    struct hwr_look_values values;
    struct hwr_look_lights lights;
};

struct hwr_look_map {
    char name[HWR_LOOK_NAME];
    char profile[HWR_LOOK_NAME];
    struct hwr_look_values values;
    struct hwr_look_lights lights;
};

struct hwr_look_file {
    struct hwr_look_values global;
    struct hwr_look_lights lights;
    struct hwr_look_profile profiles[HWR_LOOK_MAX_PROFILES];
    size_t profile_count;
    struct hwr_look_map maps[HWR_LOOK_MAX_MAPS];
    size_t map_count;
};

static char g_look_path[2600];
static struct hwr_look_file g_look;
static int g_look_present;
static struct hwr_look_values g_effective;
static struct hwr_look_lights g_effective_lights;
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
    hwr_row_set_add(&values->present, index);
    values->value[index] = value;
}

static void hwr_look_merge(struct hwr_look_values *into,
                           const struct hwr_look_values *from)
{
    size_t count = hwr_schema_count();
    size_t i;

    for (i = 0; i < count; i++) {
        if (hwr_row_set_has(&from->present, (int)i))
            hwr_look_set(into, (int)i, from->value[i]);
    }
}

/* The entry for one graphic in one section, created where there is not one.
 * NULL means the section is already carrying as many graphics as it may. */
static struct hwr_look_light_entry *hwr_look_light_entry(
    struct hwr_look_lights *lights, uint16_t graphic_id)
{
    size_t i;

    for (i = 0; i < lights->count; i++) {
        if (lights->items[i].graphic_id == graphic_id)
            return &lights->items[i];
    }
    if (lights->count >= HWR_LOOK_MAX_LIGHTS)
        return NULL;
    memset(&lights->items[lights->count], 0, sizeof lights->items[0]);
    lights->items[lights->count].graphic_id = graphic_id;
    return &lights->items[lights->count++];
}

/* One section's entries over another's, FIELD by field. A map raising the
 * strength of a graphic the global section already coloured keeps that colour,
 * which is the same rule the settings rows compose by and the reason the file
 * can write a dash for a field it is saying nothing about. */
static void hwr_look_lights_merge(struct hwr_look_lights *into,
                                  const struct hwr_look_lights *from)
{
    size_t i;

    for (i = 0; i < from->count; i++) {
        const struct hwr_look_light_entry *source = &from->items[i];
        struct hwr_look_light_entry *target =
            hwr_look_light_entry(into, source->graphic_id);

        if (target == NULL)
            continue;
        if (source->light.off)
            target->light.off = 1;
        if (source->light.has_intensity) {
            target->light.has_intensity = 1;
            target->light.intensity = source->light.intensity;
        }
        if (source->light.has_range) {
            target->light.has_range = 1;
            target->light.range = source->light.range;
        }
        if (source->light.has_color) {
            target->light.has_color = 1;
            target->light.color = source->light.color;
        }
    }
}

/* A field of a lights line. A dash -- or nothing at all -- says this line is
 * not answering for that field, which is not the same as answering zero. */
static int hwr_look_light_field(const char *text, double *out)
{
    char *end = NULL;
    double value;

    if (text == NULL || text[0] == 0 || (text[0] == '-' && text[1] == 0))
        return 0;
    value = strtod(text, &end);
    if (end == text)
        return -1;
    while (*end != 0 && isspace((unsigned char)*end))
        end++;
    if (*end != 0)
        return -1;
    *out = value;
    return 1;
}

/* Six bare hexadecimal digits, the way every other colour in this file is
 * written: a hash starts a comment here, so a colour can never carry one. */
static int hwr_look_light_color(const char *text, uint32_t *out)
{
    uint32_t value = 0;
    size_t i;

    for (i = 0; i < 6u; i++) {
        int digit = text[i];

        if (digit >= '0' && digit <= '9')
            digit -= '0';
        else if (digit >= 'a' && digit <= 'f')
            digit -= 'a' - 10;
        else if (digit >= 'A' && digit <= 'F')
            digit -= 'A' - 10;
        else
            return 0;
        value = (value << 4) | (uint32_t)digit;
    }
    if (text[6] != 0)
        return 0;
    *out = value;
    return 1;
}

/* One `graphic = off` or `graphic = intensity, range, colour` line. The key is
 * the graphic id the map draws the thing with, in whatever base it is written
 * in, so the hexadecimal an editor shows can be pasted straight in. */
static int hwr_look_accept_light(char *key, char *value,
                                 struct hwr_look_lights *into,
                                 unsigned line_number, int report)
{
    struct hwr_look_light_entry *entry;
    struct hwr_look_light parsed;
    unsigned long graphic;
    char *end = NULL;
    char *cursor = value;
    int field;

    graphic = strtoul(key, &end, 0);
    if (end == key || *hwr_look_trim(end) != 0 || graphic > 0xffffuL) {
        if (report)
            fprintf(stderr, "[renderer_overhaul] %s:%u: '%s' is not a graphic "
                            "number; ignored\n",
                    g_look_path, line_number, key);
        return 0;
    }
    memset(&parsed, 0, sizeof parsed);
    if (hwr_ascii_equal_ci(value, "off")) {
        parsed.off = 1;
    } else {
        static const char *const names[3] = { "strength", "reach", "colour" };

        for (field = 0; field < 3; field++) {
            char *comma = strchr(cursor, ',');
            char *text;
            double number = 0.0;
            int have;

            if (comma != NULL)
                *comma = 0;
            text = hwr_look_trim(cursor);
            if (field == 2) {
                uint32_t rgb;

                if (text[0] != 0 &&
                    !(text[0] == '-' && text[1] == 0)) {
                    if (!hwr_look_light_color(text, &rgb)) {
                        if (report)
                            fprintf(stderr, "[renderer_overhaul] %s:%u: '%s': "
                                            "the colour is six hexadecimal "
                                            "digits; ignored\n",
                                    g_look_path, line_number, key);
                        return 0;
                    }
                    parsed.has_color = 1;
                    parsed.color = rgb;
                }
            } else {
                have = hwr_look_light_field(text, &number);
                if (have < 0) {
                    if (report)
                        fprintf(stderr, "[renderer_overhaul] %s:%u: '%s': the "
                                        "%s is not a number; ignored\n",
                                g_look_path, line_number, key, names[field]);
                    return 0;
                }
                if (have > 0) {
                    if (field == 0) {
                        parsed.has_intensity = 1;
                        parsed.intensity = (float)number;
                    } else {
                        parsed.has_range = 1;
                        parsed.range = (float)number;
                    }
                }
            }
            if (comma == NULL)
                break;
            cursor = comma + 1;
        }
    }
    entry = hwr_look_light_entry(into, (uint16_t)graphic);
    if (entry == NULL) {
        if (report)
            fprintf(stderr, "[renderer_overhaul] %s:%u: too many lights in one "
                            "section; ignored\n",
                    g_look_path, line_number);
        return 0;
    }
    if (parsed.off)
        entry->light.off = 1;
    if (parsed.has_intensity) {
        entry->light.has_intensity = 1;
        entry->light.intensity = parsed.intensity;
    }
    if (parsed.has_range) {
        entry->light.has_range = 1;
        entry->light.range = parsed.range;
    }
    if (parsed.has_color) {
        entry->light.has_color = 1;
        entry->light.color = parsed.color;
    }
    return 1;
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
    /* A section says either what a whole map looks like or what particular
     * things in it give off, never both, so exactly one of these is set while
     * any line is being read. */
    struct hwr_look_lights *lights_section = NULL;
    struct hwr_look_map *map_section = NULL;

    memset(file, 0, sizeof *file);
    stream = fopen(g_look_path, "r");
    if (stream == NULL)
        return 0;
    while (fgets(line, sizeof line, stream) != NULL) {
        char *text;
        char *equals;
        size_t i;
        size_t length = strlen(line);

        line_number++;
        for (i = 0; i < length; i++) {
            hash ^= (unsigned char)line[i];
            hash *= UINT64_C(1099511628211);
        }
        text = hwr_look_trim(hwr_strip_comment(line));
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
            lights_section = NULL;
            /* A trailing ".lights" turns whichever section this is into the
             * lights table of that same section, so [map.STUDY1.lights] is the
             * lights of STUDY1 rather than a map named "STUDY1.lights". It is
             * taken off BEFORE the name is read, because reducing the name
             * first would silently make one of those the other. */
            {
                size_t length = strlen(name);
                int wants_lights = 0;

                if (length >= 7u &&
                    hwr_ascii_equal_ci(name + length - 7u, ".lights")) {
                    name[length - 7u] = 0;
                    wants_lights = 1;
                }
                if (hwr_ascii_equal_ci(name, "lights")) {
                    name[0] = 0;
                    wants_lights = 1;
                }
                if (wants_lights && name[0] == 0) {
                    section = NULL;
                    lights_section = &file->lights;
                    continue;
                }
                if (wants_lights && hwr_look_prefix_ci(name, "profile.")) {
                    const struct hwr_look_profile *found =
                        hwr_look_find_profile(file, name + 8);

                    section = NULL;
                    if (found != NULL)
                        lights_section = &file->profiles[
                            (size_t)(found - file->profiles)].lights;
                    else if (report)
                        fprintf(stderr, "[renderer_overhaul] %s:%u: the lights "
                                        "of profile '%s', which has no section "
                                        "of its own; ignored\n",
                                g_look_path, line_number, name + 8);
                    continue;
                }
                if (wants_lights && hwr_look_prefix_ci(name, "map.")) {
                    char reduced[HWR_LOOK_NAME];
                    const struct hwr_look_map *found;

                    hwr_look_map_name(reduced, HWR_LOOK_NAME, name + 4);
                    found = hwr_look_find_map(file, reduced);
                    section = NULL;
                    if (found != NULL) {
                        lights_section = &file->maps[
                            (size_t)(found - file->maps)].lights;
                    } else if (file->map_count >= HWR_LOOK_MAX_MAPS) {
                        if (report)
                            fprintf(stderr, "[renderer_overhaul] %s:%u: too "
                                            "many maps; ignored\n",
                                    g_look_path, line_number);
                    } else {
                        /* A map whose only section is its lights is still a
                         * map: it gets one, with no row overrides in it. */
                        snprintf(file->maps[file->map_count].name,
                                 HWR_LOOK_NAME, "%s", reduced);
                        lights_section = &file->maps[file->map_count].lights;
                        file->map_count++;
                    }
                    continue;
                }
                if (wants_lights) {
                    if (report)
                        fprintf(stderr, "[renderer_overhaul] %s:%u: '%s' is "
                                        "not a look section; ignored\n",
                                g_look_path, line_number, name);
                    section = NULL;
                    continue;
                }
            }
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
                char reduced[HWR_LOOK_NAME];
                const struct hwr_look_map *found;

                hwr_look_map_name(reduced, HWR_LOOK_NAME, name + 4);
                found = hwr_look_find_map(file, reduced);
                /* A map whose lights arrived before its keys already has a
                 * section; a second one would leave the earlier one's lights
                 * behind the later one's keys. */
                if (found != NULL) {
                    map_section = &file->maps[(size_t)(found - file->maps)];
                    section = &map_section->values;
                    continue;
                }
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
                snprintf(map_section->name, HWR_LOOK_NAME, "%s", reduced);
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

            if (lights_section != NULL) {
                hwr_look_accept_light(key, value, lights_section, line_number,
                                      report);
                continue;
            }
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
                             struct hwr_look_values *out,
                             struct hwr_look_lights *out_lights)
{
    const struct hwr_look_map *map;

    memset(out, 0, sizeof *out);
    memset(out_lights, 0, sizeof *out_lights);
    if (!g_look_present)
        return;
    hwr_look_merge(out, &file->global);
    hwr_look_lights_merge(out_lights, &file->lights);
    map = hwr_look_find_map(file, map_name);
    if (map == NULL)
        return;
    if (map->profile[0] != 0) {
        const struct hwr_look_profile *profile =
            hwr_look_find_profile(file, map->profile);

        if (profile != NULL) {
            hwr_look_merge(out, &profile->values);
            hwr_look_lights_merge(out_lights, &profile->lights);
        } else {
            fprintf(stderr, "[renderer_overhaul] %s: map '%s' selects the "
                            "unknown profile '%s'; ignored\n",
                    g_look_path, map->name, map->profile);
        }
    }
    hwr_look_merge(out, &map->values);
    hwr_look_lights_merge(out_lights, &map->lights);
}

/* Publishes a freshly composed override set and reports what moved, so the rows
 * that changed value are applied and nothing else is disturbed. */
static void hwr_look_publish(const struct hwr_look_values *next,
                             const struct hwr_look_lights *next_lights)
{
    uint32_t changed = 0;
    size_t count = hwr_schema_count();
    size_t i;

    /* The lights table is not a row and raises no row's bit of its own, so it
     * raises the rig's: what it changes is what the lights that are not at the
     * eye are worth, which is the same thing every row of that family changes. */
    if (next_lights->count != g_effective_lights.count ||
        memcmp(next_lights->items, g_effective_lights.items,
               next_lights->count * sizeof next_lights->items[0]) != 0)
        changed |= HWR_CHANGE_TRUECOLOR;
    g_effective_lights = *next_lights;
    for (i = 0; i < count; i++) {
        int had = hwr_row_set_has(&g_effective.present, (int)i);
        int has = hwr_row_set_has(&next->present, (int)i);
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
    g_look_present = 0;
    g_look_map[0] = 0;
    if (plugin_dir == NULL)
        plugin_dir = "";
    snprintf(g_look_path, sizeof g_look_path, "%s/renderer_look.cfg",
             plugin_dir);
    if (!hwr_look_read(&g_look, &hash, 1)) {
        /* Whatever was in force came from a file that is not being read, so it
         * is published away rather than dropped where it stands: emptying the
         * set in silence would leave every row that had an override holding it
         * until something else happened to move. On the first load there is
         * nothing to publish and this costs nothing. */
        struct hwr_look_values empty;
        struct hwr_look_lights no_lights;

        memset(&empty, 0, sizeof empty);
        memset(&no_lights, 0, sizeof no_lights);
        hwr_look_publish(&empty, &no_lights);
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
            struct hwr_look_lights no_lights;

            memset(&empty, 0, sizeof empty);
            memset(&no_lights, 0, sizeof no_lights);
            memset(&g_look, 0, sizeof g_look);
            g_look_present = 0;
            hwr_look_publish(&empty, &no_lights);
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
    struct hwr_look_lights next_lights;

    hwr_look_map_name(g_look_map, HWR_LOOK_NAME, map_name);
    hwr_look_compose(&g_look, g_look_map, &next, &next_lights);
    hwr_look_publish(&next, &next_lights);
}

union hwr_value hwr_look_effective(const struct hwr_setting_desc *desc,
                                   const struct hwr_settings *settings)
{
    int index = hwr_look_row_index(desc);

    if (index >= 0 && hwr_row_set_has(&g_effective.present, index))
        return g_effective.value[index];
    return hwr_schema_get(settings, desc);
}

int hwr_look_overrides(const struct hwr_setting_desc *desc)
{
    int index = hwr_look_row_index(desc);

    return index >= 0 && hwr_row_set_has(&g_effective.present, index);
}

int hwr_look_light_for(uint16_t graphic_id, struct hwr_look_light *out)
{
    size_t i;

    if (out == NULL)
        return 0;
    memset(out, 0, sizeof *out);
    for (i = 0; i < g_effective_lights.count; i++) {
        if (g_effective_lights.items[i].graphic_id != graphic_id)
            continue;
        *out = g_effective_lights.items[i].light;
        return 1;
    }
    return 0;
}

void hwr_look_clear(void)
{
    memset(&g_look, 0, sizeof g_look);
    memset(&g_effective, 0, sizeof g_effective);
    memset(&g_effective_lights, 0, sizeof g_effective_lights);
    g_look_present = 0;
    g_look_path[0] = 0;
    g_look_map[0] = 0;
    g_look_hash = 0;
    g_look_candidate_hash = 0;
    g_look_candidate_polls = 0;
    g_look_reload_pending = 0;
}
