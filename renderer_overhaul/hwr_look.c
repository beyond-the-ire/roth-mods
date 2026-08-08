#include "hwr_look.h"

#include "hwr_json.h"
#include "hwr_util.h"

#include <stdarg.h>
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
/* How many places one file may describe. One file describes one game, and this
 * is room for half again as many places as the game that ships has. Every
 * place gets a table of its own whether it fills one or not, so this number is
 * paid for whether or not a file uses it; raising it is worth doing when the
 * tables are grown to fit the file rather than declared to fit the cap. */
#define HWR_LOOK_MAX_MAPS 64u
#define HWR_LOOK_NAME 64u
/* How many graphics one scope may say something about. A place has a handful
 * of things that give light; this is room for one that is nothing but. */
#define HWR_LOOK_MAX_LIGHTS 64u
/* The shape this build reads. A file that names a later one is read anyway and
 * says so, which is what lets an older build be honest about a newer file
 * rather than guess at it. */
#define HWR_LOOK_FORMAT 1
/* Where the descriptions live, and the one that is read. The folder is listed
 * and picked from on the settings screen; until then it is this name. */
#define HWR_LOOK_FOLDER "game_configs"
#define HWR_LOOK_FILE "roth.json"
/* How long a place in the file may be named. Deep enough for every chain the
 * shape has and short enough to say in one line. */
#define HWR_LOOK_WHERE 256u

struct hwr_look_values {
    struct hwr_row_set present;
    union hwr_value value[HWR_LOOK_MAX_ROWS];
};

/* What one graphic gives off, kept with each field's own presence so that
 * composing two scopes is per field rather than per entry. */
struct hwr_look_light_entry {
    uint16_t graphic_id;
    struct hwr_look_light light;
};

struct hwr_look_lights {
    struct hwr_look_light_entry items[HWR_LOOK_MAX_LIGHTS];
    size_t count;
};

/* One answer, whether it is the one every place gets or one place's own: they
 * have the same shape, and that is the whole of the composition rule. */
struct hwr_look_scope {
    struct hwr_look_values values;
    struct hwr_look_lights lights;
};

struct hwr_look_map {
    char name[HWR_LOOK_NAME];
    struct hwr_look_scope scope;
};

struct hwr_look_file {
    /* What the file calls the game it describes, which is its title. */
    char title[HWR_LOOK_NAME];
    struct hwr_look_scope everywhere;
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

/* ------------------------------------------------------- what is said ---- */

/* One thing in the file that could not be used. The place is a chain of names
 * rather than a line, because a name is what a person searches an edited file
 * for and because a value in a JSON file has no line of its own once it has
 * been read -- the reader answers for the file's shape, this answers for its
 * meaning. */
static void hwr_look_note(int report, const char *where, const char *format,
                          ...)
{
    char what[192];
    va_list args;

    if (!report)
        return;
    va_start(args, format);
    vsnprintf(what, sizeof what, format, args);
    va_end(args);
    fprintf(stderr, "[renderer_overhaul] %s: %s: %s; ignored\n", g_look_path,
            where, what);
}

/* One place in the file named the way a person searches an edited one: the
 * chain of names that reaches it, joined with dots. Written by hand rather
 * than by a format so that a long name is cut to fit here, where the cutting
 * is plainly the point, rather than somewhere a compiler has to be told
 * about. */
static void hwr_look_where(char out[HWR_LOOK_WHERE], const char *where,
                           const char *name)
{
    size_t used = 0;
    size_t i;

    if (where != NULL) {
        for (i = 0; where[i] != 0 && used + 1u < HWR_LOOK_WHERE; i++)
            out[used++] = where[i];
        if (used + 1u < HWR_LOOK_WHERE)
            out[used++] = '.';
    }
    if (name != NULL) {
        for (i = 0; name[i] != 0 && used + 1u < HWR_LOOK_WHERE; i++)
            out[used++] = name[i];
    }
    out[used] = 0;
}

/* ------------------------------------------------------------ the model -- */

/* A map's name reduced to what it is matched on: no directory and no
 * extension, whichever side it came from. Both the names in the file and the
 * name the engine holds go through here, so STUDY1, study1.raw and
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

/* The entry for one graphic in one scope, created where there is not one.
 * NULL means the scope is already carrying as many graphics as it may. */
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

/* One scope's entries over another's, FIELD by field. A map raising the
 * strength of a graphic the game-wide answer already coloured keeps that
 * colour, which is the same rule the settings rows compose by and the reason a
 * field a scope says nothing about is left out rather than written down. */
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

/* ---------------------------------------------------- reading the file --- */

/* Six hexadecimal digits, with or without the hash a person writes in front of
 * them. JSON has no comment character for one to collide with, so both
 * spellings are a colour here and the shipped file writes the hash. */
static int hwr_look_color(const char *text, uint32_t *out)
{
    uint32_t value = 0;
    size_t i;

    if (text == NULL)
        return 0;
    if (text[0] == '#')
        text++;
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

/* Documentation, at any level: a member of this name is what the file says
 * about itself and never something it says to the renderer. The reader knows
 * nothing about it -- the skip is here, so a block that one day wants a real
 * field of that name opts out in one place. */
static int hwr_look_is_notes(const char *name)
{
    return name != NULL && strcmp(name, "notes") == 0;
}

/* One key of a `settings` block against the settings table. A file may only
 * carry rows that say a map is allowed to override them, so there is no second
 * list of keys to keep in step with the first. */
static void hwr_look_read_setting(const struct hwr_json_doc *doc, int node,
                                  struct hwr_look_values *into,
                                  const char *where, int report)
{
    const char *key = hwr_json_name(doc, node);
    const struct hwr_setting_desc *desc;
    union hwr_value parsed;
    char message[128];
    char text[192];
    int index;

    if (key == NULL)
        return;
    desc = hwr_schema_find(key);
    if (desc == NULL || (desc->flags & HWR_SF_MAP_OVERRIDE) == 0) {
        hwr_look_note(report, where, "'%s' is not a look setting", key);
        return;
    }
    index = hwr_look_row_index(desc);
    if (index < 0) {
        hwr_look_note(report, where, "'%s' cannot be overridden", key);
        return;
    }
    if (hwr_json_type(doc, node) == HWR_JSON_OBJECT ||
        hwr_json_type(doc, node) == HWR_JSON_ARRAY) {
        hwr_look_note(report, where, "'%s' is a single value, not a list or a "
                                     "block", key);
        return;
    }
    /* Written back out as it stands and handed to the table's own reader, so
     * every bound, every token and every complaint comes from the one table
     * and the file and the settings screen can only ever agree. */
    hwr_json_render(doc, node, text, sizeof text);
    message[0] = 0;
    if (!hwr_schema_parse(desc, text, &parsed, message, sizeof message)) {
        hwr_look_note(report, where, "'%s': %s", key, message);
        return;
    }
    hwr_look_set(into, index, parsed);
}

static void hwr_look_read_settings(const struct hwr_json_doc *doc, int object,
                                   struct hwr_look_values *into,
                                   const char *where, int report)
{
    int child;

    if (hwr_json_type(doc, object) != HWR_JSON_OBJECT) {
        hwr_look_note(report, where, "the settings are a block of key and "
                                     "value");
        return;
    }
    for (child = hwr_json_first(doc, object); child >= 0;
         child = hwr_json_next(doc, child)) {
        if (hwr_look_is_notes(hwr_json_name(doc, child)))
            continue;
        hwr_look_read_setting(doc, child, into, where, report);
    }
}

/* What one graphic gives off. Every field is separately present or absent, and
 * absent means saying nothing about it rather than answering nought. */
static void hwr_look_read_light(const struct hwr_json_doc *doc, int node,
                                struct hwr_look_lights *into,
                                const char *where, int report)
{
    const char *key = hwr_json_name(doc, node);
    struct hwr_look_light_entry *entry;
    unsigned long graphic;
    char inside[HWR_LOOK_WHERE];
    char *end = NULL;
    int off = 0;
    int child;

    if (key == NULL)
        return;
    graphic = strtoul(key, &end, 0);
    if (end == key || *end != 0 || graphic > 0xffffuL) {
        hwr_look_note(report, where, "'%s' is not a graphic number", key);
        return;
    }
    hwr_look_where(inside, where, key);
    if (hwr_json_type(doc, node) != HWR_JSON_OBJECT) {
        hwr_look_note(report, inside, "a light is a block of what it gives "
                                      "off");
        return;
    }
    entry = hwr_look_light_entry(into, (uint16_t)graphic);
    if (entry == NULL) {
        hwr_look_note(report, inside, "that is more lights than one place may "
                                      "describe");
        return;
    }
    child = hwr_json_member(doc, node, "off");
    if (child >= 0 && !hwr_json_as_bool(doc, child, &off)) {
        hwr_look_note(report, inside, "'off' is true or false");
        off = 0;
    }
    /* `off` is the whole entry rather than a field: a thing the map marks
     * unshaded that is plainly not a light is refused outright, and nothing
     * else in the entry is read. */
    if (off) {
        entry->light.off = 1;
        return;
    }
    for (child = hwr_json_first(doc, node); child >= 0;
         child = hwr_json_next(doc, child)) {
        const char *field = hwr_json_name(doc, child);
        double number = 0.0;
        uint32_t rgb = 0;

        if (field == NULL || hwr_look_is_notes(field) ||
            strcmp(field, "off") == 0)
            continue;
        if (strcmp(field, "strength") == 0) {
            if (!hwr_json_as_number(doc, child, &number)) {
                hwr_look_note(report, inside, "the strength is a number");
                continue;
            }
            entry->light.has_intensity = 1;
            entry->light.intensity = (float)number;
        } else if (strcmp(field, "reach") == 0) {
            if (!hwr_json_as_number(doc, child, &number)) {
                hwr_look_note(report, inside, "the reach is a number");
                continue;
            }
            entry->light.has_range = 1;
            entry->light.range = (float)number;
        } else if (strcmp(field, "colour") == 0) {
            if (!hwr_look_color(hwr_json_as_text(doc, child), &rgb)) {
                hwr_look_note(report, inside, "the colour is six hexadecimal "
                                              "digits, such as \"#ffb46e\"");
                continue;
            }
            entry->light.has_color = 1;
            entry->light.color = rgb;
        } else {
            hwr_look_note(report, inside, "'%s' is not something a light gives "
                                          "off", field);
        }
    }
}

static void hwr_look_read_lights(const struct hwr_json_doc *doc, int object,
                                 struct hwr_look_lights *into,
                                 const char *where, int report)
{
    char inside[HWR_LOOK_WHERE];
    int child;

    if (hwr_json_type(doc, object) != HWR_JSON_OBJECT) {
        hwr_look_note(report, where, "the lights are a block of what things "
                                     "give off");
        return;
    }
    for (child = hwr_json_first(doc, object); child >= 0;
         child = hwr_json_next(doc, child)) {
        const char *name = hwr_json_name(doc, child);

        if (name == NULL || hwr_look_is_notes(name))
            continue;
        if (strcmp(name, "graphics") == 0) {
            int light;

            hwr_look_where(inside, where, "graphics");
            if (hwr_json_type(doc, child) != HWR_JSON_OBJECT) {
                hwr_look_note(report, inside, "the graphics are a block keyed "
                                              "by the graphic a thing is drawn "
                                              "with");
                continue;
            }
            for (light = hwr_json_first(doc, child); light >= 0;
                 light = hwr_json_next(doc, light)) {
                if (hwr_look_is_notes(hwr_json_name(doc, light)))
                    continue;
                hwr_look_read_light(doc, light, into, inside, report);
            }
        } else {
            hwr_look_note(report, where, "'%s' is not something this build "
                                         "reads", name);
        }
    }
}

/* One answer, whichever it is: the one every place gets, or one place's own. */
static void hwr_look_read_scope(const struct hwr_json_doc *doc, int object,
                                struct hwr_look_scope *into, const char *where,
                                int report)
{
    char inside[HWR_LOOK_WHERE];
    int child;

    if (hwr_json_type(doc, object) != HWR_JSON_OBJECT) {
        hwr_look_note(report, where, "a place is described by a block");
        return;
    }
    for (child = hwr_json_first(doc, object); child >= 0;
         child = hwr_json_next(doc, child)) {
        const char *name = hwr_json_name(doc, child);

        if (name == NULL || hwr_look_is_notes(name))
            continue;
        if (strcmp(name, "settings") == 0) {
            hwr_look_where(inside, where, "settings");
            hwr_look_read_settings(doc, child, &into->values, inside, report);
        } else if (strcmp(name, "lights") == 0) {
            hwr_look_where(inside, where, "lights");
            hwr_look_read_lights(doc, child, &into->lights, inside, report);
        } else {
            hwr_look_note(report, where, "'%s' is not something this build "
                                         "reads", name);
        }
    }
}

/* The whole file into a model of its own. Returns 0 where there was nothing to
 * make a model of, which costs the file rather than a key: a description that
 * is not a description has no half worth keeping. */
static int hwr_look_build(struct hwr_look_file *file,
                          const struct hwr_json_doc *doc, int report)
{
    int root = hwr_json_root(doc);
    int child;

    if (hwr_json_type(doc, root) != HWR_JSON_OBJECT) {
        if (report)
            fprintf(stderr, "[renderer_overhaul] %s: a game config is a block "
                            "with braces around it; nothing was read from it\n",
                    g_look_path);
        return 0;
    }
    for (child = hwr_json_first(doc, root); child >= 0;
         child = hwr_json_next(doc, child)) {
        const char *name = hwr_json_name(doc, child);

        if (name == NULL || hwr_look_is_notes(name))
            continue;
        if (strcmp(name, "format") == 0) {
            double shape = 0.0;

            if (!hwr_json_as_number(doc, child, &shape)) {
                hwr_look_note(report, "format", "the shape number is a number");
                continue;
            }
            if (shape > (double)HWR_LOOK_FORMAT && report)
                fprintf(stderr, "[renderer_overhaul] %s: this was written for "
                                "a later shape of the file than this build "
                                "reads; reading it anyway\n", g_look_path);
        } else if (strcmp(name, "for") == 0) {
            const char *title = hwr_json_as_text(doc, child);

            if (title == NULL) {
                hwr_look_note(report, "for", "the name of the game is a piece "
                                             "of text");
                continue;
            }
            snprintf(file->title, sizeof file->title, "%s", title);
        } else if (strcmp(name, "everywhere") == 0) {
            hwr_look_read_scope(doc, child, &file->everywhere, "everywhere",
                                report);
        } else if (strcmp(name, "maps") == 0) {
            int entry;

            if (hwr_json_type(doc, child) != HWR_JSON_OBJECT) {
                hwr_look_note(report, "maps", "the maps are a block keyed by "
                                              "the name of a map");
                continue;
            }
            for (entry = hwr_json_first(doc, child); entry >= 0;
                 entry = hwr_json_next(doc, entry)) {
                const char *written = hwr_json_name(doc, entry);
                const struct hwr_look_map *found;
                char reduced[HWR_LOOK_NAME];
                char where[HWR_LOOK_WHERE];

                if (written == NULL || hwr_look_is_notes(written))
                    continue;
                hwr_look_map_name(reduced, HWR_LOOK_NAME, written);
                hwr_look_where(where, "maps", written);
                if (reduced[0] == 0) {
                    hwr_look_note(report, where, "that is not the name of a "
                                                 "map");
                    continue;
                }
                /* Two spellings of one name are one map, so the second is laid
                 * over the first rather than losing it. */
                found = hwr_look_find_map(file, reduced);
                if (found == NULL) {
                    if (file->map_count >= HWR_LOOK_MAX_MAPS) {
                        hwr_look_note(report, where, "that is more maps than "
                                                     "one file may describe");
                        continue;
                    }
                    found = &file->maps[file->map_count];
                    snprintf(file->maps[file->map_count].name, HWR_LOOK_NAME,
                             "%s", reduced);
                    file->map_count++;
                }
                hwr_look_read_scope(doc, entry,
                                    &file->maps[(size_t)(found - file->maps)]
                                        .scope,
                                    where, report);
            }
        } else {
            hwr_look_note(report, "the file", "'%s' is not something this "
                                              "build reads", name);
        }
    }
    return 1;
}

/* The file's own bytes, or nothing. The existence check is here rather than in
 * the reader because a file that is not there and a file that cannot be read
 * are different answers to this layer -- one is silence and the other is worth
 * saying -- and the reader gives one answer to both.
 *
 * Which is why nothing comes back two ways: `why` says whether there was no
 * such file or whether there was one and it defeated us. Answering the second
 * as the first would take a person's overrides away and say nothing, which is
 * the one thing the distinction exists to prevent. */
static char *hwr_look_slurp(size_t *out_length, const char **why)
{
    /* One byte past the cap, so a file that is too big is SEEN to be too big
     * rather than read to the cap and parsed as though it ended there. */
    const size_t ceiling = HWR_JSON_MAX_BYTES + 1u;
    size_t capacity = 0;
    size_t filled = 0;
    char *buffer = NULL;
    FILE *stream;

    *out_length = 0;
    *why = NULL;
    stream = fopen(g_look_path, "rb");
    if (stream == NULL)
        return NULL;
    /* Grown as it is read rather than sized from what the file says it is: a
     * size read off a file is a size the file chose. */
    for (;;) {
        size_t got;

        if (filled == capacity) {
            size_t grown = capacity == 0 ? 8192u : capacity * 2u;
            char *bigger;

            if (grown > ceiling)
                grown = ceiling;
            if (grown == capacity)
                break;
            bigger = (char *)realloc(buffer, grown);
            if (bigger == NULL) {
                free(buffer);
                fclose(stream);
                *why = "there was not enough memory to read it";
                return NULL;
            }
            buffer = bigger;
            capacity = grown;
        }
        got = fread(buffer + filled, 1, capacity - filled, stream);
        if (got == 0)
            break;
        filled += got;
    }
    if (ferror(stream)) {
        free(buffer);
        fclose(stream);
        *why = "it could not be read to its end";
        return NULL;
    }
    fclose(stream);
    if (buffer == NULL) {
        /* An empty file is a file, and the reader has its own word for one. */
        buffer = (char *)malloc(1u);
        if (buffer == NULL) {
            *why = "there was not enough memory to read it";
            return NULL;
        }
        buffer[0] = 0;
    }
    *out_length = filled;
    return buffer;
}

/* The whole file into a temporary model, so a file being edited can never
 * leave half its overrides standing.
 *
 *   1  the file was read and the model built
 *   0  there is no such file
 *  -1  there is one and it could not be read, which costs the file and
 *      nothing else: whatever was in force stays in force
 *
 * The middle answer is the quiet one. The last is not: JSON has no line-level
 * recovery, because one missing brace moves every following line into a
 * different place, so a file with one is reported whole and loaded not at
 * all. */
static int hwr_look_read(struct hwr_look_file *file, uint64_t *content_hash,
                         int report)
{
    struct hwr_json_doc doc;
    uint64_t hash = UINT64_C(1469598103934665603);
    const char *why = NULL;
    size_t length = 0;
    char *bytes;
    size_t i;
    int built;

    memset(file, 0, sizeof *file);
    bytes = hwr_look_slurp(&length, &why);
    if (bytes == NULL) {
        if (why == NULL)
            return 0;
        if (report)
            fprintf(stderr, "[renderer_overhaul] %s: %s\n", g_look_path, why);
        return -1;
    }
    for (i = 0; i < length; i++) {
        hash ^= (unsigned char)bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    memset(&doc, 0, sizeof doc);
    if (!hwr_json_parse(&doc, bytes, length, g_look_path)) {
        free(bytes);
        if (report)
            hwr_json_report(&doc);
        hwr_json_free(&doc);
        return -1;
    }
    free(bytes);
    if (report)
        hwr_json_report(&doc);
    built = hwr_look_build(file, &doc, report);
    hwr_json_free(&doc);
    if (!built) {
        memset(file, 0, sizeof *file);
        return -1;
    }
    *content_hash = hash;
    return 1;
}

/* ---------------------------------------------------- what is in force --- */

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
    hwr_look_merge(out, &file->everywhere.values);
    hwr_look_lights_merge(out_lights, &file->everywhere.lights);
    map = hwr_look_find_map(file, map_name);
    if (map != NULL) {
        hwr_look_merge(out, &map->scope.values);
        hwr_look_lights_merge(out_lights, &map->scope.lights);
    }
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

/* Whatever was in force came from a file that is not being read, so it is
 * published away rather than dropped where it stands: emptying the set in
 * silence would leave every row that had an override holding it until something
 * else happened to move. On a first load there is nothing to publish and this
 * costs nothing. */
static void hwr_look_publish_nothing(void)
{
    struct hwr_look_values empty;
    struct hwr_look_lights no_lights;

    memset(&empty, 0, sizeof empty);
    memset(&no_lights, 0, sizeof no_lights);
    hwr_look_publish(&empty, &no_lights);
}

static void hwr_look_say_what_was_read(const struct hwr_look_file *file)
{
    if (file->title[0] != 0)
        fprintf(stderr, "[renderer_overhaul] game config %s (%s): %u map(s) "
                        "described\n",
                g_look_path, file->title, (unsigned)file->map_count);
    else
        fprintf(stderr, "[renderer_overhaul] game config %s: %u map(s) "
                        "described\n",
                g_look_path, (unsigned)file->map_count);
}

void hwr_look_load(const char *plugin_dir)
{
    struct hwr_look_file *next;
    uint64_t hash = 0;
    int read;

    if (plugin_dir == NULL)
        plugin_dir = "";
    snprintf(g_look_path, sizeof g_look_path, "%s/%s/%s", plugin_dir,
             HWR_LOOK_FOLDER, HWR_LOOK_FILE);
    next = (struct hwr_look_file *)malloc(sizeof *next);
    if (next == NULL)
        return;
    read = hwr_look_read(next, &hash, 1);
    if (read < 0) {
        /* The file said so itself while it was being read. Whatever was in
         * force is what a person had before they broke it, and it stands. */
        free(next);
        return;
    }
    if (read == 0) {
        free(next);
        memset(&g_look, 0, sizeof g_look);
        g_look_present = 0;
        g_look_map[0] = 0;
        hwr_look_publish_nothing();
        fprintf(stderr, "[renderer_overhaul] no game config at %s; the "
                        "settings in force describe every map\n", g_look_path);
        return;
    }
    g_look = *next;
    free(next);
    g_look_present = 1;
    g_look_hash = hash;
    g_look_candidate_polls = 0;
    hwr_look_say_what_was_read(&g_look);
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
    /* A file that is not there and a file half written are both nothing to ask
     * for: the first is already answered for and the second will be read again
     * in thirty frames, whole. */
    if (hwr_look_read(probe, &hash, 0) <= 0) {
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
    int read;

    if (!g_look_reload_pending)
        return;
    g_look_reload_pending = 0;
    next = (struct hwr_look_file *)malloc(sizeof *next);
    if (next == NULL)
        return;
    read = hwr_look_read(next, &hash, 1);
    if (read < 0) {
        free(next);
        return;
    }
    if (read == 0) {
        free(next);
        if (g_look_present) {
            /* The file was there and is not any more: the game-wide answer goes
             * with it rather than standing over a file nobody has. */
            memset(&g_look, 0, sizeof g_look);
            g_look_present = 0;
            hwr_look_publish_nothing();
            fprintf(stderr, "[renderer_overhaul] the game config %s is gone; "
                            "the settings in force describe every map\n",
                    g_look_path);
        }
        return;
    }
    g_look = *next;
    free(next);
    g_look_present = 1;
    g_look_hash = hash;
    g_look_candidate_polls = 0;
    fprintf(stderr, "[renderer_overhaul] game config reloaded\n");
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
