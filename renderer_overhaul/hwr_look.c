#include "hwr_look.h"

#include "hwr_json.h"
#include "hwr_util.h"

#include <dirent.h>
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
/* How many entries one keyed grant table may hold in one scope -- the graphics
 * a place says something about, and the surfaces it says something about, which
 * are two tables of the same shape keyed by two different sixteen-bit numbers.
 * A place has a handful of things that give light; this is room for one that is
 * nothing but, and a game has a lava, a rune and a sigil or two rather than
 * sixty of them. */
#define HWR_LOOK_MAX_LIGHTS 64u
/* And how many PLACED things one map may name. A hand-tuned map picks out the
 * few lights that carry a room -- the hearth, the brazier at the top of the
 * stairs -- so this is far past what tuning takes, and it is the same number as
 * the graphics above so that neither is the one somebody has to remember. Every
 * place carries a table of this size whether it fills one or not, which is what
 * it costs; it is the same bargain the graphics table already makes. */
#define HWR_LOOK_MAX_OBJECTS 64u
/* And how many FREE-STANDING lights one map may hang in the air. These are not
 * a tuning of something the map already carries -- each is a light somebody put
 * where the map has none -- so a map that wants many is a map being re-lit
 * rather than adjusted, and this is generous room for that. */
#define HWR_LOOK_MAX_PLACED 64u
/* The shape this build reads. A file that names a later one is read anyway and
 * says so, which is what lets an older build be honest about a newer file
 * rather than guess at it. */
#define HWR_LOOK_FORMAT 1
/* Where the descriptions live: a folder of them inside the mod's own folder,
 * listed on the settings screen and chosen from there. The base game's ships
 * as game_configs/roth.json, and that name is also the one a fresh install
 * with no settings file of its own starts on. */
#define HWR_LOOK_FOLDER "game_configs"
#define HWR_LOOK_EXTENSION ".json"
#define HWR_LOOK_SHIPPED "roth"
/* The file the layer read before the folder did, kept only to say that it is
 * no longer read. */
#define HWR_LOOK_RETIRED "renderer_look.cfg"
/* How long a place in the file may be named. Deep enough for every chain the
 * shape has and short enough to say in one line. */
#define HWR_LOOK_WHERE 256u
/* How many files the folder is listed down to. A game has one description and
 * a machine with map packs on it has a handful; this is far past either, and
 * it bounds the work a folder somebody emptied a download into can cost. */
#define HWR_LOOK_MAX_CONFIGS 64u
/* Those, plus none at the front of the list, plus the one place kept for a
 * file the settings file names and the folder does not hold. */
#define HWR_LOOK_MAX_SLOTS (HWR_LOOK_MAX_CONFIGS + 2u)
/* How long the name shown on the settings screen may be. Wide enough for the
 * longest name a file may carry plus what is added to one the folder does not
 * hold, so that a kept name is shown whole rather than cut off at the point it
 * stops being recognisable. */
#define HWR_LOOK_LABEL 144u
/* How long a game config's own name may be. Longer than any file anybody will
 * write, and deliberately at least as long as the settings file's own value
 * can carry: a name kept for a file the folder does not hold has to be kept
 * WHOLE, or the settings file would name one thing and the list hold another,
 * and one file somebody moved would cost every setting in it. */
#define HWR_LOOK_TOKEN 128u

struct hwr_look_values {
    struct hwr_row_set present;
    union hwr_value value[HWR_LOOK_MAX_ROWS];
};

/* What one KEYED thing gives off, kept with each field's own presence so that
 * composing two scopes is per field rather than per entry.
 *
 * The key is a sixteen-bit number and the table says nothing about which one:
 * the graphics table is keyed by the graphic a thing is drawn with and the
 * surfaces table by the texture a face is painted with, and what makes them two
 * tables is where they are read from and what consults them, never their shape.
 * One shape is one merge rule, and a rule written once cannot compose one table
 * differently from the other. */
struct hwr_look_light_entry {
    uint16_t id;
    struct hwr_look_light light;
};

struct hwr_look_lights {
    struct hwr_look_light_entry items[HWR_LOOK_MAX_LIGHTS];
    size_t count;
};

/* What ONE PLACED OBJECT of the map gives off.
 *
 * THE KEY IS THE ONE THE MAP EDITOR USES, and that is the whole of the ruling
 * behind this table. A person tuning a map has the community ROTH Editor open
 * beside the file; the two must name the same thing by the same number, or the
 * file is written against numbers nobody can look up.
 *
 * Two spellings, because the editor has two:
 *
 *   THE OBJECT NUMBER -- `index` -- the map-wide number over the per-sector
 *   object lists flattened in sector order, which is what the editor shows as
 *   `Object: N`. It names exactly one thing.
 *
 *   THE ID TAG -- `id` -- the record's own +0x0e word, which the editor shows
 *   as `ID:` and which a creator types to gather objects into a group for the
 *   command system. It is deliberately SHARED, so an entry keyed by it means
 *   EVERY object carrying it, and that is a feature: the six torches of a
 *   corridor tagged alike are tuned in one line. Nought is refused -- it is the
 *   untagged default and the command system's own word for "the source object",
 *   so it addresses everything and nothing.
 *
 * An entry carries one or the other and never both. The graphic beside it is a
 * CHECK rather than part of the key, in either spelling: an entry whose graphic
 * is not what the map draws is still applied and says so, which is what catches
 * a file gone stale after a map has been re-authored. */
struct hwr_look_object_entry {
    /* Which spelling: 0 an object number, 1 an id tag. */
    int by_id;
    /* The number itself. An object number is a map-wide index and an id tag is
     * a sixteen-bit word; one field holds either, because what tells them apart
     * is `by_id` rather than their range. */
    uint32_t key;
    int has_graphic;
    uint16_t graphic_id;
    struct hwr_look_light light;
};

struct hwr_look_objects {
    struct hwr_look_object_entry items[HWR_LOOK_MAX_OBJECTS];
    size_t count;
};

/* A FREE-STANDING light: one that belongs to no object at all, hung at three
 * numbers somebody chose.
 *
 * This is what `at` addresses now, and it is a different thing from tuning a
 * light the map already has. Nothing is being described here -- there is no
 * object to read a graphic off, no sprite to take a colour from and no map
 * mark saying this is a light -- so the colour is REQUIRED, exactly as it is
 * for a granted surface and for the same reason: there is nothing to derive it
 * from. An entry with no colour lights nothing and says so.
 *
 * There is no `off`. An entry here exists only because somebody wrote it, so
 * the way to be rid of one is to delete it; a switch for turning off a thing
 * that would not exist without you is a switch for nothing. */
struct hwr_look_placed_entry {
    int16_t world_x;
    int16_t world_y;
    int16_t world_z;
    struct hwr_look_light light;
};

struct hwr_look_placed {
    struct hwr_look_placed_entry items[HWR_LOOK_MAX_PLACED];
    size_t count;
};

/* One answer, whether it is the one every place gets or one place's own: they
 * have the same shape, and that is the whole of the composition rule.
 *
 * The two exceptions are the tables keyed by something that has no meaning
 * outside one map -- an object's own number or tag, and three numbers in a
 * building. Both are read only under a map; under `everywhere` each is refused
 * with a note. */
struct hwr_look_scope {
    struct hwr_look_values values;
    struct hwr_look_lights lights;
    struct hwr_look_lights surfaces;
    struct hwr_look_objects objects;
    struct hwr_look_placed placed;
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

static char g_look_dir[2048];
static char g_look_path[2600];
static struct hwr_look_file g_look;
static int g_look_present;
static struct hwr_look_values g_effective;
static struct hwr_look_lights g_effective_lights;
static struct hwr_look_lights g_effective_surfaces;
static struct hwr_look_objects g_effective_objects;
static struct hwr_look_placed g_effective_placed;
/* A number that moves when the granted surfaces do and at no other time. What
 * reads it works a map out ONCE and then wants to know, in one comparison per
 * frame, whether that answer is still the answer. It starts at one so that
 * nought is free to mean nothing has been worked out yet. */
static uint32_t g_surface_generation = 1u;
/* And the same for the free-standing lights, which are worked out against the
 * map's own rooms once when it is staged for exactly the same reason. */
static uint32_t g_placed_generation = 1u;
/* What has been SAID about each entry on the map in force, and whether the
 * map's own objects have been offered yet. Kept beside the entries rather than
 * in them, because what an entry is and what has been said about it are two
 * different things: one is the file's answer and moves when the file does, the
 * other is a conversation that starts again on every map.
 *
 * `matched` is whether anything on the map answered to the entry's key at all,
 * and `drawn` whether anything that answered was drawn with the graphic written
 * beside it. The two are separate because an id names a GROUP: the question for
 * an object number is whether THAT object is drawn with it, and the question for
 * a tag is whether ANY of the objects carrying it is -- so the second can only
 * be answered once the whole map has been offered. */
static unsigned char g_object_matched[HWR_LOOK_MAX_OBJECTS];
static unsigned char g_object_drawn[HWR_LOOK_MAX_OBJECTS];
static unsigned char g_object_told[HWR_LOOK_MAX_OBJECTS];
static int g_objects_settled;
static char g_look_map[HWR_LOOK_NAME];
static uint64_t g_look_hash;
static uint64_t g_look_candidate_hash;
static uint32_t g_look_candidate_polls;
static uint32_t g_look_frames_until_poll;
static int g_look_reload_pending;

/* The picker's own members: what the folder was found to hold, in the shape
 * the settings table takes an enumeration in. The token is the file's name
 * without its extension and is what the settings file carries; the label is
 * what the settings screen shows, which is the file's own `for` string where
 * it has one. Slot nought is none and is always there.
 *
 * The two arrays behind them hold the text, because the table keeps pointers
 * and a folder listing is not made of string literals. */
static struct hwr_enum_value g_slots[HWR_LOOK_MAX_SLOTS];
static char g_slot_token[HWR_LOOK_MAX_SLOTS][HWR_LOOK_TOKEN];
static char g_slot_label[HWR_LOOK_MAX_SLOTS][HWR_LOOK_LABEL];
/* Which slots are a name the settings file carries and the folder does not
 * answer for. The place is kept so that the settings file still parses whole
 * -- one missing description must never cost every other setting -- and so
 * that putting the file back restores the selection rather than having
 * silently forgotten it. */
static unsigned char g_slot_missing[HWR_LOOK_MAX_SLOTS];
static size_t g_slot_count;
/* The selection the layer has already acted on, so that the settings pump can
 * tell a picker that has moved from one that has not with one comparison. */
static int32_t g_look_followed;

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

/* ----------------------------------------------- the folder and the picker */

/* Whether a name can be written down in the settings file. That file is
 * key=value, '#' starts a comment in it and whitespace around a value is
 * trimmed, so a file name carrying either is a selection nobody could record
 * -- and a selection that cannot be recorded is worse than a file left out of
 * the list, because it would be lost on the next save without a word. */
static int hwr_look_plain_name(const char *name)
{
    size_t i;

    if (name == NULL || name[0] == 0)
        return 0;
    for (i = 0; name[i] != 0; i++) {
        char letter = name[i];

        if ((letter >= 'a' && letter <= 'z') ||
            (letter >= 'A' && letter <= 'Z') ||
            (letter >= '0' && letter <= '9') ||
            letter == '-' || letter == '_' || letter == '.')
            continue;
        return 0;
    }
    return 1;
}

/* The name a file is listed under: what it is called with its extension taken
 * off. Answers 0 for anything in the folder that is not one of ours, and for a
 * name too long to record. */
static int hwr_look_file_token(const char *file_name, char *out,
                               size_t out_size)
{
    size_t length = file_name != NULL ? strlen(file_name) : 0;
    size_t suffix = strlen(HWR_LOOK_EXTENSION);

    if (length <= suffix || length - suffix >= out_size)
        return 0;
    if (!hwr_ascii_equal_ci(file_name + length - suffix, HWR_LOOK_EXTENSION))
        return 0;
    memcpy(out, file_name, length - suffix);
    out[length - suffix] = 0;
    return 1;
}

/* How big a file is, or -1 where that cannot be told. Asked before a file is
 * read rather than after, so that one too large to be one of ours is left out
 * of the list for that reason instead of being reported as unreadable. */
static long hwr_look_file_size(const char *path)
{
    FILE *stream = fopen(path, "rb");
    long size = -1;

    if (stream == NULL)
        return -1;
    if (fseek(stream, 0, SEEK_END) == 0)
        size = ftell(stream);
    fclose(stream);
    return size;
}

/* What one file calls the game it describes, which is the name the settings
 * screen shows for it. A file that is not JSON at all is still listed and
 * simply has nothing to say here -- a broken file in the folder must never
 * cost the folder -- and what is wrong with it is reported if and when it is
 * the one chosen. */
static void hwr_look_read_title(const char *path, char *out, size_t out_size)
{
    struct hwr_json_doc doc;
    const char *title;

    out[0] = 0;
    memset(&doc, 0, sizeof doc);
    if (!hwr_json_parse_file(&doc, path)) {
        hwr_json_free(&doc);
        return;
    }
    title = hwr_json_as_text(&doc,
                             hwr_json_member(&doc, hwr_json_root(&doc), "for"));
    if (title != NULL)
        snprintf(out, out_size, "%s", title);
    hwr_json_free(&doc);
}

static void hwr_look_publish_slots(void)
{
    hwr_schema_set_enum_values("game_config", g_slots, g_slot_count);
}

/* The list back to none alone, which is what it holds where nothing has looked
 * in a folder at all. */
static void hwr_look_slots_reset(void)
{
    memset(g_slots, 0, sizeof g_slots);
    memset(g_slot_token, 0, sizeof g_slot_token);
    memset(g_slot_label, 0, sizeof g_slot_label);
    memset(g_slot_missing, 0, sizeof g_slot_missing);
    snprintf(g_slot_label[0], HWR_LOOK_LABEL, "None");
    g_slots[0].token = g_slot_token[0];
    g_slots[0].value = 0;
    g_slots[0].label = g_slot_label[0];
    g_slot_count = 1;
    hwr_look_publish_slots();
}

static int hwr_look_add_slot(const char *token, const char *label, int missing)
{
    size_t at = g_slot_count;

    if (at >= HWR_LOOK_MAX_SLOTS)
        return -1;
    snprintf(g_slot_token[at], HWR_LOOK_TOKEN, "%s", token);
    snprintf(g_slot_label[at], HWR_LOOK_LABEL, "%s", label);
    g_slots[at].token = g_slot_token[at];
    g_slots[at].value = (int32_t)at;
    g_slots[at].label = g_slot_label[at];
    g_slot_missing[at] = missing != 0 ? 1u : 0u;
    g_slot_count++;
    return (int)at;
}

static int hwr_look_slot_of(const char *token)
{
    size_t i;

    if (token == NULL)
        return -1;
    for (i = 0; i < g_slot_count; i++) {
        if (hwr_ascii_equal_ci(g_slot_token[i], token))
            return (int)i;
    }
    return -1;
}

/* Which file is chosen, by name. Nothing chosen and a place the list does not
 * hold are the same answer here: the empty name, which is none. */
static const char *hwr_look_selected_token(void)
{
    int32_t slot = hwr_settings_get()->game_config;

    if (slot < 0 || (size_t)slot >= g_slot_count)
        return "";
    return g_slot_token[slot];
}

static int hwr_look_selected_is_missing(void)
{
    int32_t slot = hwr_settings_get()->game_config;

    if (slot < 0 || (size_t)slot >= g_slot_count)
        return 0;
    return g_slot_missing[slot] != 0;
}

/* One line for the file this layer used to read, said once and only where one
 * is actually still sitting beside the plugin. */
static void hwr_look_note_retired_file(void)
{
    static int said;
    char path[2600];
    FILE *stream;

    if (said || g_look_dir[0] == 0)
        return;
    snprintf(path, sizeof path, "%s/%s", g_look_dir, HWR_LOOK_RETIRED);
    stream = fopen(path, "rb");
    if (stream == NULL)
        return;
    fclose(stream);
    said = 1;
    fprintf(stderr, "[renderer_overhaul] %s is no longer read; per-map looks "
                    "now live in the %s folder\n",
            HWR_LOOK_RETIRED, HWR_LOOK_FOLDER);
}

void hwr_look_scan(const char *plugin_dir, const char *keep)
{
    char names[HWR_LOOK_MAX_CONFIGS][HWR_LOOK_TOKEN];
    char keep_token[HWR_LOOK_TOKEN];
    char folder[2100];
    size_t count = 0;
    size_t left_out = 0;
    DIR *listing;
    size_t i;
    int slot;

    /* What is kept across a listing is the file's NAME, never the place it sat
     * in: a file added to the folder moves every place after it, and a
     * selection that followed a place would quietly become a different file. */
    if (keep != NULL)
        snprintf(keep_token, sizeof keep_token, "%s", keep);
    else
        snprintf(keep_token, sizeof keep_token, "%s",
                 hwr_look_selected_token());
    /* The folder is never written down from itself. A re-listing hands back
     * the one already known -- which is the same bytes this would be copying
     * onto -- and a copy of a thing onto itself is not a copy. */
    if (plugin_dir != NULL && plugin_dir != g_look_dir)
        snprintf(g_look_dir, sizeof g_look_dir, "%s", plugin_dir);
    hwr_look_slots_reset();
    snprintf(folder, sizeof folder, "%s/%s", g_look_dir, HWR_LOOK_FOLDER);
    listing = opendir(folder);
    if (listing != NULL) {
        const struct dirent *entry;

        while ((entry = readdir(listing)) != NULL) {
            char token[HWR_LOOK_TOKEN];
            char path[2600];
            long size;

            if (!hwr_look_file_token(entry->d_name, token, sizeof token))
                continue;
            if (!hwr_look_plain_name(token)) {
                fprintf(stderr, "[renderer_overhaul] %s/%s: a game config's "
                                "name may hold only letters, digits, '-', '_' "
                                "and '.', because it is what the settings file "
                                "records it as; left out\n",
                        folder, entry->d_name);
                continue;
            }
            snprintf(path, sizeof path, "%s/%s", folder, entry->d_name);
            size = hwr_look_file_size(path);
            if (size < 0 || (unsigned long)size > (unsigned long)
                                                     HWR_JSON_MAX_BYTES) {
                fprintf(stderr, "[renderer_overhaul] %s: a game config is a "
                                "few kilobytes; this one is not one of ours "
                                "and is left out\n", path);
                continue;
            }
            if (count >= HWR_LOOK_MAX_CONFIGS) {
                left_out++;
                continue;
            }
            snprintf(names[count], HWR_LOOK_TOKEN, "%s", token);
            count++;
        }
        closedir(listing);
    }
    if (left_out != 0)
        fprintf(stderr, "[renderer_overhaul] %s holds more than %u game "
                        "configs; %u of them are left out of the list\n",
                folder, (unsigned)HWR_LOOK_MAX_CONFIGS, (unsigned)left_out);
    /* Sorted by name, and the same way on every machine: a folder listing
     * arrives in whatever order the filesystem feels like, and a picker whose
     * order changes between runs is one nobody can find their file in. */
    for (i = 1; i < count; i++) {
        char moving[HWR_LOOK_TOKEN];
        size_t at = i;

        memcpy(moving, names[i], HWR_LOOK_TOKEN);
        while (at > 0 && hwr_ascii_before_ci(moving, names[at - 1u])) {
            memcpy(names[at], names[at - 1u], HWR_LOOK_TOKEN);
            at--;
        }
        memcpy(names[at], moving, HWR_LOOK_TOKEN);
    }
    for (i = 0; i < count; i++) {
        char path[2600];
        char label[HWR_LOOK_LABEL];

        snprintf(path, sizeof path, "%s/%.*s%s", folder,
                 (int)(HWR_LOOK_TOKEN - 1u), names[i], HWR_LOOK_EXTENSION);
        hwr_look_read_title(path, label, sizeof label);
        hwr_look_add_slot(names[i], label[0] != 0 ? label : names[i], 0);
    }
    /* A name the settings file carries that the folder does not answer for
     * keeps its place in the list. Refusing it outright would mark the whole
     * settings file invalid and take every other setting down with it, over
     * one file somebody moved; and forgetting it would mean putting the file
     * back does not put the selection back. */
    if (keep_token[0] != 0 && hwr_look_slot_of(keep_token) < 0) {
        char label[HWR_LOOK_LABEL];

        snprintf(label, sizeof label, "%s (missing)", keep_token);
        hwr_look_add_slot(keep_token, label, 1);
    }
    hwr_look_publish_slots();
    slot = hwr_look_slot_of(keep_token);
    hwr_settings_set_game_config(slot > 0 ? (int32_t)slot : 0);
    hwr_look_note_retired_file();
}

size_t hwr_look_config_count(void)
{
    return g_slot_count;
}

const char *hwr_look_config_token(size_t slot)
{
    return slot < g_slot_count ? g_slot_token[slot] : NULL;
}

const char *hwr_look_config_label(size_t slot)
{
    return slot < g_slot_count ? g_slot_label[slot] : NULL;
}

int hwr_look_config_is_missing(size_t slot)
{
    return slot < g_slot_count && g_slot_missing[slot] != 0;
}

int hwr_look_select_config(const char *token)
{
    int slot = hwr_look_slot_of(token);

    if (slot < 0)
        return 0;
    hwr_settings_set_game_config((int32_t)slot);
    return hwr_settings_get()->game_config == slot;
}

void hwr_look_select_shipped(void)
{
    int slot = hwr_look_slot_of(HWR_LOOK_SHIPPED);

    if (slot <= 0 || g_slot_missing[slot])
        return;
    if (!hwr_look_select_config(HWR_LOOK_SHIPPED))
        return;
    fprintf(stderr, "[renderer_overhaul] no settings file yet; the game config "
                    "%s describes this game's maps\n", g_slot_label[slot]);
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

/* The entry for one key in one scope's table, created where there is not one.
 * NULL means the scope is already carrying as many as it may. */
static struct hwr_look_light_entry *hwr_look_light_entry(
    struct hwr_look_lights *lights, uint16_t id)
{
    size_t i;

    for (i = 0; i < lights->count; i++) {
        if (lights->items[i].id == id)
            return &lights->items[i];
    }
    if (lights->count >= HWR_LOOK_MAX_LIGHTS)
        return NULL;
    memset(&lights->items[lights->count], 0, sizeof lights->items[0]);
    lights->items[lights->count].id = id;
    return &lights->items[lights->count++];
}

/* One scope's entries over another's, FIELD by field. A map raising the
 * strength of a graphic the game-wide answer already coloured keeps that
 * colour, which is the same rule the settings rows compose by and the reason a
 * field a scope says nothing about is left out rather than written down.
 *
 * Written once for both keyed tables, so a surface composes exactly as a
 * graphic does and neither can quietly grow a rule of its own. */
static void hwr_look_lights_merge(struct hwr_look_lights *into,
                                  const struct hwr_look_lights *from)
{
    size_t i;

    for (i = 0; i < from->count; i++) {
        const struct hwr_look_light_entry *source = &from->items[i];
        struct hwr_look_light_entry *target =
            hwr_look_light_entry(into, source->id);

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

/* The entry for one address in one scope, created where there is not one.
 * Two entries written for one address are two statements about one thing, so
 * the second is laid over the first rather than standing beside it -- and an
 * object number and an id tag that happen to be the same number are two
 * different addresses, which is why the spelling is part of the match. NULL
 * means the map is already carrying as many placed objects as it may. */
static struct hwr_look_object_entry *hwr_look_object_entry(
    struct hwr_look_objects *objects, int by_id, uint32_t key)
{
    size_t i;

    for (i = 0; i < objects->count; i++) {
        if (objects->items[i].by_id == by_id && objects->items[i].key == key)
            return &objects->items[i];
    }
    if (objects->count >= HWR_LOOK_MAX_OBJECTS)
        return NULL;
    memset(&objects->items[objects->count], 0, sizeof objects->items[0]);
    objects->items[objects->count].by_id = by_id;
    objects->items[objects->count].key = key;
    return &objects->items[objects->count++];
}

/* And one free-standing light, which IS keyed by where it hangs, because that
 * is the whole of what it is. NULL means the map is already carrying as many as
 * it may. */
static struct hwr_look_placed_entry *hwr_look_placed_entry(
    struct hwr_look_placed *placed, int16_t x, int16_t y, int16_t z)
{
    size_t i;

    for (i = 0; i < placed->count; i++) {
        if (placed->items[i].world_x == x && placed->items[i].world_y == y &&
            placed->items[i].world_z == z)
            return &placed->items[i];
    }
    if (placed->count >= HWR_LOOK_MAX_PLACED)
        return NULL;
    memset(&placed->items[placed->count], 0, sizeof placed->items[0]);
    placed->items[placed->count].world_x = x;
    placed->items[placed->count].world_y = y;
    placed->items[placed->count].world_z = z;
    return &placed->items[placed->count++];
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

/* Whether a grant is refused outright, wherever it is keyed from. `off` is the
 * whole entry rather than a field: a thing the map marks unshaded that is
 * plainly not a light is refused, and what it would have given off is not
 * read. */
static int hwr_look_grant_off(const struct hwr_json_doc *doc, int node,
                              const char *where, int report)
{
    int child = hwr_json_member(doc, node, "off");
    int off = 0;

    if (child >= 0 && !hwr_json_as_bool(doc, child, &off)) {
        hwr_look_note(report, where, "'off' is true or false");
        return 0;
    }
    return off;
}

/* One field of what a grant gives off. Every field is separately present or
 * absent, and absent means saying nothing about it rather than answering
 * nought.
 *
 * Answers 0 for a name that is none of these three, so a table with names of
 * its own -- where a thing stands, which graphic it should be -- answers for
 * those and lets this answer for the rest. The three fields are then spelled
 * once for both tables and cannot drift into two spellings. */
static int hwr_look_grant_field(const struct hwr_json_doc *doc, int child,
                                struct hwr_look_light *into, const char *where,
                                int report)
{
    const char *field = hwr_json_name(doc, child);
    double number = 0.0;
    uint32_t rgb = 0;

    if (field == NULL)
        return 0;
    if (strcmp(field, "strength") == 0) {
        if (!hwr_json_as_number(doc, child, &number)) {
            hwr_look_note(report, where, "the strength is a number");
            return 1;
        }
        into->has_intensity = 1;
        into->intensity = (float)number;
        return 1;
    }
    if (strcmp(field, "reach") == 0) {
        if (!hwr_json_as_number(doc, child, &number)) {
            hwr_look_note(report, where, "the reach is a number");
            return 1;
        }
        into->has_range = 1;
        into->range = (float)number;
        return 1;
    }
    if (strcmp(field, "colour") == 0) {
        if (!hwr_look_color(hwr_json_as_text(doc, child), &rgb)) {
            hwr_look_note(report, where, "the colour is six hexadecimal "
                                         "digits, such as \"#ffb46e\"");
            return 1;
        }
        into->has_color = 1;
        into->color = rgb;
        return 1;
    }
    return 0;
}

/* A sixteen-bit number, however it is spelled: a string in any base, which is
 * what a table keyed by one uses, or a bare number where one is written beside
 * a position. Answers 0 for anything that is not one. */
static int hwr_look_graphic_number(const char *text, uint16_t *out)
{
    unsigned long graphic;
    char *end = NULL;

    if (text == NULL || text[0] == 0)
        return 0;
    graphic = strtoul(text, &end, 0);
    if (end == text || *end != 0 || graphic > 0xffffuL)
        return 0;
    *out = (uint16_t)graphic;
    return 1;
}

/* What one keyed thing gives off: one graphic, or one surface.
 *
 * `noun` and `key_word` are what the complaints call the thing and its key --
 * a light keyed by a graphic, a surface keyed by a texture. The reading is
 * otherwise the same for both, and deliberately so: two readers would be two
 * places for the three fields to be spelled and one place for them to drift. */
static void hwr_look_read_light(const struct hwr_json_doc *doc, int node,
                                struct hwr_look_lights *into,
                                const char *where, int report,
                                const char *noun, const char *key_word)
{
    const char *key = hwr_json_name(doc, node);
    struct hwr_look_light_entry *entry;
    char inside[HWR_LOOK_WHERE];
    uint16_t graphic = 0;
    int child;

    if (key == NULL)
        return;
    if (!hwr_look_graphic_number(key, &graphic)) {
        hwr_look_note(report, where, "'%s' is not a %s number", key, key_word);
        return;
    }
    hwr_look_where(inside, where, key);
    if (hwr_json_type(doc, node) != HWR_JSON_OBJECT) {
        hwr_look_note(report, inside, "a %s is a block of what it gives off",
                      noun);
        return;
    }
    entry = hwr_look_light_entry(into, graphic);
    if (entry == NULL) {
        hwr_look_note(report, inside, "that is more %ss than one place may "
                                      "describe", noun);
        return;
    }
    if (hwr_look_grant_off(doc, node, inside, report)) {
        entry->light.off = 1;
        return;
    }
    for (child = hwr_json_first(doc, node); child >= 0;
         child = hwr_json_next(doc, child)) {
        const char *field = hwr_json_name(doc, child);

        if (field == NULL || hwr_look_is_notes(field) ||
            strcmp(field, "off") == 0)
            continue;
        if (!hwr_look_grant_field(doc, child, &entry->light, inside, report))
            hwr_look_note(report, inside, "'%s' is not something a light gives "
                                          "off", field);
    }
}

/* Where a free-standing light hangs. Three exact numbers, read as sixteen-bit
 * integers because that is the space the map itself is authored in: a position
 * that will not fit in one is a position inside no building, and saying so turns
 * a mistyped number into a complaint rather than into a light nobody ever
 * sees. */
static int hwr_look_read_at(const struct hwr_json_doc *doc, int node,
                            int16_t out[3], const char *where, int report)
{
    size_t read = 0;
    int child;

    if (hwr_json_type(doc, node) != HWR_JSON_ARRAY) {
        hwr_look_note(report, where, "'at' is where the light hangs, written "
                                     "[x, y, z]");
        return 0;
    }
    for (child = hwr_json_first(doc, node); child >= 0;
         child = hwr_json_next(doc, child)) {
        double number = 0.0;

        if (read >= 3u) {
            hwr_look_note(report, where, "'at' is three numbers: x, y and z");
            return 0;
        }
        if (!hwr_json_as_number(doc, child, &number)) {
            hwr_look_note(report, where, "'at' is three numbers: x, y and z");
            return 0;
        }
        if (number < -32768.0 || number > 32767.0 ||
            number != (double)(int)number) {
            hwr_look_note(report, where, "a position is a whole number between "
                                         "-32768 and 32767");
            return 0;
        }
        out[read++] = (int16_t)number;
    }
    if (read != 3u) {
        hwr_look_note(report, where, "'at' is three numbers: x, y and z");
        return 0;
    }
    return 1;
}

/* One whole number, 0 or more, read out of a member that must be a plain
 * number: an object's number and its id tag are counts of things, and neither
 * is written in hexadecimal by the editor a person reads them out of. */
static int hwr_look_read_whole(const struct hwr_json_doc *doc, int node,
                               uint32_t limit, uint32_t *out,
                               const char *word, const char *where, int report)
{
    double number = 0.0;

    if (!hwr_json_as_number(doc, node, &number) || number < 0.0 ||
        number > (double)limit || number != (double)(long)number) {
        hwr_look_note(report, where, "the %s is a whole number from 0 to %lu",
                      word, (unsigned long)limit);
        return 0;
    }
    *out = (uint32_t)number;
    return 1;
}

/* One placed object of the map: which one, optionally which graphic it should
 * be, and what it gives off.
 *
 * THE ADDRESS IS THE EDITOR'S. `index` is the object number the community ROTH
 * Editor shows as `Object: N`; `id` is the tag it shows as `ID:` and names every
 * object carrying it. One or the other, never both -- an entry carrying both is
 * two different addresses written as one, and there is no reading of it that is
 * not a guess.
 *
 * The address is read BEFORE `off` is asked about, which is the one place the
 * "nothing else in the entry is read" rule has an edge worth stating: which
 * object it is and which graphic it should be are what ADDRESS the entry rather
 * than what it gives off, so an entry that turns one thing off is still checked
 * against what is actually there. An entry nobody can check is an entry that
 * goes stale in silence. */
static void hwr_look_read_object(const struct hwr_json_doc *doc, int node,
                                 struct hwr_look_objects *into, size_t index,
                                 const char *where, int report)
{
    struct hwr_look_object_entry *entry;
    char inside[HWR_LOOK_WHERE];
    char label[24];
    uint32_t key = 0;
    int by_index;
    int by_id;
    int by_tag = 0;
    int child;

    snprintf(label, sizeof label, "%u", (unsigned)index);
    hwr_look_where(inside, where, label);
    if (hwr_json_type(doc, node) != HWR_JSON_OBJECT) {
        hwr_look_note(report, inside, "one placed object is a block of which "
                                      "object it is and what it gives off");
        return;
    }
    by_index = hwr_json_member(doc, node, "index");
    by_id = hwr_json_member(doc, node, "id");
    if (by_index >= 0 && by_id >= 0) {
        hwr_look_note(report, inside, "an entry names one object by its number "
                                      "or a group of them by their id tag, not "
                                      "both");
        return;
    }
    if (by_index < 0 && by_id < 0) {
        hwr_look_note(report, inside, "a placed object is addressed by its "
                                      "number, written \"index\": 12, or by its "
                                      "id tag, written \"id\": 7 -- both are the "
                                      "numbers the map editor shows for it");
        return;
    }
    if (by_index >= 0) {
        /* An object number is an index into the map's own object list. The
         * bound is what a map could conceivably number rather than a rule about
         * any map: what says an entry addresses nothing is the map itself. */
        if (!hwr_look_read_whole(doc, by_index, 0xffffffu, &key,
                                 "object number", inside, report))
            return;
    } else {
        if (!hwr_look_read_whole(doc, by_id, 0xffffu, &key, "id tag", inside,
                                 report))
            return;
        /* NOUGHT IS NOT AN ADDRESS. It is the untagged default every object
         * carries until a creator types one, and the command system's own word
         * for "the object this command came from" -- so an entry keyed by it
         * would name most of the map and mean nothing by it. */
        if (key == 0u) {
            hwr_look_note(report, inside, "an id tag of 0 is the untagged "
                                          "default that most objects carry, so "
                                          "it addresses nothing here");
            return;
        }
        by_tag = 1;
    }
    entry = hwr_look_object_entry(into, by_tag, key);
    if (entry == NULL) {
        hwr_look_note(report, inside, "that is more placed objects than one map "
                                      "may describe");
        return;
    }
    child = hwr_json_member(doc, node, "graphic");
    if (child >= 0) {
        const char *text = hwr_json_as_text(doc, child);
        char written[64];
        uint16_t graphic = 0;

        if (text == NULL) {
            hwr_json_render(doc, child, written, sizeof written);
            text = written;
        }
        if (!hwr_look_graphic_number(text, &graphic))
            hwr_look_note(report, inside, "the graphic is a number, such as "
                                          "\"0x0412\"");
        else {
            entry->has_graphic = 1;
            entry->graphic_id = graphic;
        }
    }
    if (hwr_look_grant_off(doc, node, inside, report)) {
        entry->light.off = 1;
        return;
    }
    for (child = hwr_json_first(doc, node); child >= 0;
         child = hwr_json_next(doc, child)) {
        const char *field = hwr_json_name(doc, child);

        if (field == NULL || hwr_look_is_notes(field) ||
            strcmp(field, "index") == 0 || strcmp(field, "id") == 0 ||
            strcmp(field, "graphic") == 0 || strcmp(field, "off") == 0)
            continue;
        if (!hwr_look_grant_field(doc, child, &entry->light, inside, report))
            hwr_look_note(report, inside, "'%s' is not something a light gives "
                                          "off", field);
    }
}

/* The placed objects one map picks out. A LIST rather than a block, because one
 * entry may name a whole group of objects and because a number is not a name. */
static void hwr_look_read_objects(const struct hwr_json_doc *doc, int array,
                                  struct hwr_look_objects *into,
                                  const char *where, int report)
{
    size_t index = 0;
    int child;

    if (hwr_json_type(doc, array) != HWR_JSON_ARRAY) {
        hwr_look_note(report, where, "the placed objects are a list, because "
                                     "each names an object by a number rather "
                                     "than by a name");
        return;
    }
    for (child = hwr_json_first(doc, array); child >= 0;
         child = hwr_json_next(doc, child))
        hwr_look_read_object(doc, child, into, index++, where, report);
}

/* One free-standing light: where it hangs and what it gives off.
 *
 * The colour is REQUIRED and is checked HERE rather than where the light is
 * seeded, so that a person writing the file is told by the file's own reading
 * of itself. There is nothing to derive a colour from -- no object, no sprite,
 * no map mark -- so a light with none is a light nobody chose the colour of,
 * and giving it a default would be the mod choosing. */
static void hwr_look_read_placed(const struct hwr_json_doc *doc, int node,
                                 struct hwr_look_placed *into, size_t index,
                                 const char *where, int report)
{
    struct hwr_look_placed_entry *entry;
    char inside[HWR_LOOK_WHERE];
    char label[24];
    int16_t at[3];
    int child;

    snprintf(label, sizeof label, "%u", (unsigned)index);
    hwr_look_where(inside, where, label);
    if (hwr_json_type(doc, node) != HWR_JSON_OBJECT) {
        hwr_look_note(report, inside, "one free-standing light is a block of "
                                      "where it hangs and what it gives off");
        return;
    }
    child = hwr_json_member(doc, node, "at");
    if (child < 0) {
        hwr_look_note(report, inside, "a free-standing light is addressed by "
                                      "where it hangs, written "
                                      "\"at\": [x, y, z]");
        return;
    }
    if (!hwr_look_read_at(doc, child, at, inside, report))
        return;
    entry = hwr_look_placed_entry(into, at[0], at[1], at[2]);
    if (entry == NULL) {
        hwr_look_note(report, inside, "that is more free-standing lights than "
                                      "one map may hang");
        return;
    }
    for (child = hwr_json_first(doc, node); child >= 0;
         child = hwr_json_next(doc, child)) {
        const char *field = hwr_json_name(doc, child);

        if (field == NULL || hwr_look_is_notes(field) ||
            strcmp(field, "at") == 0)
            continue;
        /* `off` is not among the fields, so it falls here with everything else
         * nobody knows: an entry that exists only because somebody wrote it is
         * got rid of by deleting it, and a switch for turning off a thing that
         * would not be there without you is a switch for nothing. */
        if (!hwr_look_grant_field(doc, child, &entry->light, inside, report))
            hwr_look_note(report, inside, "'%s' is not something a light gives "
                                          "off", field);
    }
}

/* The free-standing lights of one map. A list, keyed by nothing but where each
 * one hangs. */
static void hwr_look_read_placed_lights(const struct hwr_json_doc *doc,
                                        int array,
                                        struct hwr_look_placed *into,
                                        const char *where, int report)
{
    size_t index = 0;
    int child;

    if (hwr_json_type(doc, array) != HWR_JSON_ARRAY) {
        hwr_look_note(report, where, "the free-standing lights are a list, "
                                     "because each is addressed by where it "
                                     "hangs rather than by a name");
        return;
    }
    for (child = hwr_json_first(doc, array); child >= 0;
         child = hwr_json_next(doc, child))
        hwr_look_read_placed(doc, child, into, index++, where, report);
}

/* `placed` says whether this scope may address something that only exists in one
 * map -- an object of it, or a position inside it. Only a map's own scope may:
 * an object number is a number in ONE map's list, and the same three numbers
 * elsewhere are a different corner of a different building. */
static void hwr_look_read_lights(const struct hwr_json_doc *doc, int object,
                                 struct hwr_look_scope *into,
                                 const char *where, int report, int placed)
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
                hwr_look_read_light(doc, light, &into->lights, inside, report,
                                    "light", "graphic");
            }
        } else if (strcmp(name, "surfaces") == 0) {
            int surface;

            /* Keyed by the texture a FACE is painted with rather than by the
             * graphic a THING is drawn with -- lava, a rune, a lit sigil. It
             * is read in every scope, unlike the placed things below: a
             * texture is the same texture wherever it is painted, and a game
             * whose lava glows wants it to glow everywhere. */
            hwr_look_where(inside, where, "surfaces");
            if (hwr_json_type(doc, child) != HWR_JSON_OBJECT) {
                hwr_look_note(report, inside, "the surfaces are a block keyed "
                                              "by the texture a face is "
                                              "painted with");
                continue;
            }
            for (surface = hwr_json_first(doc, child); surface >= 0;
                 surface = hwr_json_next(doc, surface)) {
                if (hwr_look_is_notes(hwr_json_name(doc, surface)))
                    continue;
                hwr_look_read_light(doc, surface, &into->surfaces, inside,
                                    report, "surface", "texture");
            }
        } else if (strcmp(name, "objects") == 0) {
            hwr_look_where(inside, where, "objects");
            if (!placed) {
                hwr_look_note(report, inside, "an object's number and its id "
                                              "tag belong to ONE map, so a "
                                              "placed object is described under "
                                              "a map rather than everywhere");
                continue;
            }
            hwr_look_read_objects(doc, child, &into->objects, inside, report);
        } else if (strcmp(name, "placed") == 0) {
            /* The lights that belong to no object at all. Under a map only, for
             * the same reason: three numbers elsewhere are a different corner of
             * a different building. */
            hwr_look_where(inside, where, "placed");
            if (!placed) {
                hwr_look_note(report, inside, "a position is a position in ONE "
                                              "map, so a free-standing light is "
                                              "hung under a map rather than "
                                              "everywhere");
                continue;
            }
            hwr_look_read_placed_lights(doc, child, &into->placed, inside,
                                        report);
        } else {
            hwr_look_note(report, where, "'%s' is not something this build "
                                         "reads", name);
        }
    }
}

/* One answer, whichever it is: the one every place gets, or one place's own. */
static void hwr_look_read_scope(const struct hwr_json_doc *doc, int object,
                                struct hwr_look_scope *into, const char *where,
                                int report, int placed)
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
            hwr_look_read_lights(doc, child, into, inside, report, placed);
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
                                report, 0);
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
                                    where, report, 1);
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
                             struct hwr_look_lights *out_lights,
                             struct hwr_look_lights *out_surfaces,
                             struct hwr_look_objects *out_objects,
                             struct hwr_look_placed *out_placed)
{
    const struct hwr_look_map *map;

    memset(out, 0, sizeof *out);
    memset(out_lights, 0, sizeof *out_lights);
    memset(out_surfaces, 0, sizeof *out_surfaces);
    memset(out_objects, 0, sizeof *out_objects);
    memset(out_placed, 0, sizeof *out_placed);
    if (!g_look_present)
        return;
    hwr_look_merge(out, &file->everywhere.values);
    hwr_look_lights_merge(out_lights, &file->everywhere.lights);
    hwr_look_lights_merge(out_surfaces, &file->everywhere.surfaces);
    map = hwr_look_find_map(file, map_name);
    if (map != NULL) {
        hwr_look_merge(out, &map->scope.values);
        hwr_look_lights_merge(out_lights, &map->scope.lights);
        hwr_look_lights_merge(out_surfaces, &map->scope.surfaces);
        /* And the two tables that come from ONE scope: the map's own placed
         * objects, which are the last word, and its free-standing lights.
         * There is no game-wide table of either to compose with, because an
         * object number and a position outside the map they belong to name
         * nothing. */
        *out_objects = map->scope.objects;
        *out_placed = map->scope.placed;
    }
}

/* Publishes a freshly composed override set and reports what moved, so the rows
 * that changed value are applied and nothing else is disturbed. */
static void hwr_look_publish(const struct hwr_look_values *next,
                             const struct hwr_look_lights *next_lights,
                             const struct hwr_look_lights *next_surfaces,
                             const struct hwr_look_objects *next_objects,
                             const struct hwr_look_placed *next_placed)
{
    uint32_t changed = 0;
    size_t count = hwr_schema_count();
    size_t i;
    int surfaces_moved;
    int placed_moved;

    /* The lights table is not a row and raises no row's bit of its own, so it
     * raises the rig's: what it changes is what the lights that are not at the
     * eye are worth, which is the same thing every row of that family changes.
     * The placed things are the same table said more precisely, so they raise
     * the same bit, and so do the surfaces. */
    surfaces_moved =
        next_surfaces->count != g_effective_surfaces.count ||
        memcmp(next_surfaces->items, g_effective_surfaces.items,
               next_surfaces->count * sizeof next_surfaces->items[0]) != 0;
    placed_moved =
        next_placed->count != g_effective_placed.count ||
        memcmp(next_placed->items, g_effective_placed.items,
               next_placed->count * sizeof next_placed->items[0]) != 0;
    if (next_lights->count != g_effective_lights.count ||
        memcmp(next_lights->items, g_effective_lights.items,
               next_lights->count * sizeof next_lights->items[0]) != 0 ||
        surfaces_moved || placed_moved ||
        next_objects->count != g_effective_objects.count ||
        memcmp(next_objects->items, g_effective_objects.items,
               next_objects->count * sizeof next_objects->items[0]) != 0)
        changed |= HWR_CHANGE_TRUECOLOR;
    g_effective_lights = *next_lights;
    /* And the one thing a seeding that works a whole map out ONCE has to be
     * told about: the granted surfaces moving under it. Raised only where they
     * actually moved, because what reads this is deciding whether to walk a
     * map again. */
    if (surfaces_moved && ++g_surface_generation == 0u)
        g_surface_generation = 1u;      /* never nought: nought means unstaged */
    g_effective_surfaces = *next_surfaces;
    /* The free-standing lights carry a generation for the same reason: which
     * room each one hangs in is a fact about the map, worked out once when it is
     * staged, and this is what says that answer is stale. */
    if (placed_moved && ++g_placed_generation == 0u)
        g_placed_generation = 1u;
    g_effective_placed = *next_placed;
    g_effective_objects = *next_objects;
    /* A new set of placed objects is a new conversation about them: what has
     * been said is about the map in force, and the map in force has just been
     * decided. */
    memset(g_object_matched, 0, sizeof g_object_matched);
    memset(g_object_drawn, 0, sizeof g_object_drawn);
    memset(g_object_told, 0, sizeof g_object_told);
    g_objects_settled = 0;
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
    struct hwr_look_lights no_surfaces;
    struct hwr_look_objects no_objects;
    struct hwr_look_placed no_placed;

    memset(&empty, 0, sizeof empty);
    memset(&no_lights, 0, sizeof no_lights);
    memset(&no_surfaces, 0, sizeof no_surfaces);
    memset(&no_objects, 0, sizeof no_objects);
    memset(&no_placed, 0, sizeof no_placed);
    hwr_look_publish(&empty, &no_lights, &no_surfaces, &no_objects,
                     &no_placed);
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

/* Nothing is in force, and why. This is not an error in any of its three
 * spellings: a person who has chosen none has chosen it, a file the folder does
 * not hold keeps its place in the list so that putting it back puts the
 * selection back, and a file that has just been deleted is answered for the
 * same way. In all three every dial falls to the settings screen on every map,
 * and the settings file is NEVER rewritten over any of it. */
static void hwr_look_stand_down(int report)
{
    const char *token = hwr_look_selected_token();

    memset(&g_look, 0, sizeof g_look);
    g_look_present = 0;
    g_look_hash = 0;
    g_look_candidate_hash = 0;
    g_look_candidate_polls = 0;
    hwr_look_publish_nothing();
    if (!report)
        return;
    if (token[0] == 0)
        fprintf(stderr, "[renderer_overhaul] no game config is in force; the "
                        "settings in force describe every map\n");
    else if (hwr_look_selected_is_missing())
        fprintf(stderr, "[renderer_overhaul] the game config %s is not in the "
                        "%s folder; the settings in force describe every map\n",
                token, HWR_LOOK_FOLDER);
    else
        fprintf(stderr, "[renderer_overhaul] no game config at %s; the "
                        "settings in force describe every map\n", g_look_path);
}

/* Whatever the picker names, read into the live model. The selection is a NAME
 * and the file it names is read wherever the list says it stands, so a file
 * that was there when the folder was listed and is not there now, and one that
 * was never there, are one answer rather than two.
 *
 * A file that is there and cannot be read costs the FILE and nothing else:
 * whatever was in force stays in force, which is what a person editing one
 * wants and what one missing brace must never be allowed to undo. */
static void hwr_look_take_selection(int report)
{
    const char *token = hwr_look_selected_token();
    struct hwr_look_file *next;
    uint64_t hash = 0;
    int read;

    g_look_followed = hwr_settings_get()->game_config;
    if (token[0] == 0) {
        g_look_path[0] = 0;
        hwr_look_stand_down(report);
        return;
    }
    snprintf(g_look_path, sizeof g_look_path, "%s/%s/%.*s%s", g_look_dir,
             HWR_LOOK_FOLDER, (int)(HWR_LOOK_TOKEN - 1u), token,
             HWR_LOOK_EXTENSION);
    next = (struct hwr_look_file *)malloc(sizeof *next);
    if (next == NULL)
        return;
    read = hwr_look_read(next, &hash, report);
    if (read < 0) {
        /* The file said so itself while it was being read. Whatever was in
         * force is what a person had before they broke it, and it stands. */
        free(next);
        return;
    }
    if (read == 0) {
        free(next);
        hwr_look_stand_down(report);
        return;
    }
    g_look = *next;
    free(next);
    g_look_present = 1;
    g_look_hash = hash;
    g_look_candidate_polls = 0;
    if (report)
        hwr_look_say_what_was_read(&g_look);
    hwr_look_select_map(g_look_map[0] != 0 ? g_look_map : NULL);
}

void hwr_look_load(const char *plugin_dir)
{
    if (plugin_dir != NULL)
        snprintf(g_look_dir, sizeof g_look_dir, "%s", plugin_dir);
    hwr_look_note_retired_file();
    g_look_map[0] = 0;
    hwr_look_take_selection(1);
}

void hwr_look_follow_selection(void)
{
    if (hwr_settings_get()->game_config == g_look_followed)
        return;
    /* One comparison per pump, and deliberately not an apply hook: the look's
     * overrides have to be in force BEFORE the hooks that consume them run,
     * and publishing from inside one would invert that order. */
    g_look_followed = hwr_settings_get()->game_config;
    g_look_reload_pending = 1;
}

void hwr_look_request_reload(void)
{
    if (g_look_dir[0] == 0)
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
    if (!g_look_reload_pending)
        return;
    g_look_reload_pending = 0;
    /* Whatever the picker names, read again. The folder itself is listed by the
     * settings file's own reload, which runs first in the pump and is what
     * Reload From File asks for beside this -- so by the time this runs, a file
     * put in the folder while the game was running is already in the picker. A
     * request that came from the file watcher instead is about the CONTENT of
     * the chosen file, and the folder has not moved for it. */
    hwr_look_take_selection(1);
}

void hwr_look_select_map(const char *map_name)
{
    struct hwr_look_values next;
    struct hwr_look_lights next_lights;
    struct hwr_look_lights next_surfaces;
    struct hwr_look_objects next_objects;
    struct hwr_look_placed next_placed;

    hwr_look_map_name(g_look_map, HWR_LOOK_NAME, map_name);
    hwr_look_compose(&g_look, g_look_map, &next, &next_lights, &next_surfaces,
                     &next_objects, &next_placed);
    hwr_look_publish(&next, &next_lights, &next_surfaces, &next_objects,
                     &next_placed);
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

/* Both of the two below are read off the state the layer already keeps -- what
 * is in force and which place it is in force for -- so that the settings screen
 * can say a map answers for itself without the layer keeping a second copy of
 * the answer for it to read. */

int hwr_look_override_count(void)
{
    size_t count = hwr_schema_count();
    size_t i;
    int total = 0;

    for (i = 0; i < count; i++) {
        if (hwr_row_set_has(&g_effective.present, (int)i))
            total++;
    }
    return total;
}

const char *hwr_look_current_map(void)
{
    return g_look_map[0] != 0 ? g_look_map : NULL;
}

int hwr_look_light_for(uint16_t graphic_id, struct hwr_look_light *out)
{
    size_t i;

    if (out == NULL)
        return 0;
    memset(out, 0, sizeof *out);
    for (i = 0; i < g_effective_lights.count; i++) {
        if (g_effective_lights.items[i].id != graphic_id)
            continue;
        *out = g_effective_lights.items[i].light;
        return 1;
    }
    return 0;
}

int hwr_look_light_for_surface(uint16_t texture_id, struct hwr_look_light *out)
{
    size_t i;

    if (out == NULL)
        return 0;
    memset(out, 0, sizeof *out);
    for (i = 0; i < g_effective_surfaces.count; i++) {
        if (g_effective_surfaces.items[i].id != texture_id)
            continue;
        *out = g_effective_surfaces.items[i].light;
        return 1;
    }
    return 0;
}

size_t hwr_look_surface_count(void)
{
    return g_effective_surfaces.count;
}

int hwr_look_surface_at(size_t index, uint16_t *texture_id,
                        struct hwr_look_light *out)
{
    if (index >= g_effective_surfaces.count || texture_id == NULL ||
        out == NULL)
        return 0;
    *texture_id = g_effective_surfaces.items[index].id;
    *out = g_effective_surfaces.items[index].light;
    return 1;
}

uint32_t hwr_look_surface_generation(void)
{
    return g_surface_generation;
}

int hwr_look_light_for_object(uint32_t map_index, struct hwr_look_light *out)
{
    size_t i;

    if (out == NULL)
        return 0;
    memset(out, 0, sizeof *out);
    for (i = 0; i < g_effective_objects.count; i++) {
        const struct hwr_look_object_entry *entry =
            &g_effective_objects.items[i];

        if (entry->by_id || entry->key != map_index)
            continue;
        *out = entry->light;
        return 1;
    }
    return 0;
}

int hwr_look_light_for_object_id(uint16_t object_id, struct hwr_look_light *out)
{
    size_t i;

    if (out == NULL)
        return 0;
    /* Nought is refused where the file is read, so nothing here can be keyed by
     * it -- but an object carrying no tag must not go looking either, because a
     * table that never holds nought would answer nothing very slowly. */
    if (object_id == 0u) {
        memset(out, 0, sizeof *out);
        return 0;
    }
    memset(out, 0, sizeof *out);
    for (i = 0; i < g_effective_objects.count; i++) {
        const struct hwr_look_object_entry *entry =
            &g_effective_objects.items[i];

        if (!entry->by_id || entry->key != (uint32_t)object_id)
            continue;
        *out = entry->light;
        return 1;
    }
    return 0;
}

size_t hwr_look_placed_count(void)
{
    return g_effective_placed.count;
}

int hwr_look_placed_at(size_t index, int16_t *world_x, int16_t *world_y,
                       int16_t *world_z, struct hwr_look_light *out)
{
    if (index >= g_effective_placed.count || world_x == NULL ||
        world_y == NULL || world_z == NULL || out == NULL)
        return 0;
    *world_x = g_effective_placed.items[index].world_x;
    *world_y = g_effective_placed.items[index].world_y;
    *world_z = g_effective_placed.items[index].world_z;
    *out = g_effective_placed.items[index].light;
    return 1;
}

uint32_t hwr_look_placed_generation(void)
{
    return g_placed_generation;
}

int hwr_look_objects_pending(void)
{
    return !g_objects_settled && g_effective_objects.count != 0u;
}

void hwr_look_object_seen(uint32_t map_index, uint16_t object_id,
                          uint16_t graphic_id)
{
    size_t i;

    if (g_objects_settled)
        return;
    for (i = 0; i < g_effective_objects.count; i++) {
        const struct hwr_look_object_entry *entry =
            &g_effective_objects.items[i];

        if (entry->by_id) {
            if (object_id == 0u || entry->key != (uint32_t)object_id)
                continue;
        } else if (entry->key != map_index) {
            continue;
        }
        g_object_matched[i] = 1u;
        if (entry->has_graphic && entry->graphic_id == graphic_id)
            g_object_drawn[i] = 1u;
        /* THE GRAPHIC IS A CHECK RATHER THAN PART OF THE KEY, so an entry that
         * names the wrong one is applied and says so. That is what catches an
         * entry gone stale after a map has been re-authored -- the one place
         * addressing an object by its number is weak, because deleting an
         * object in the editor renumbers every object after it.
         *
         * The remark is made HERE only for an object NUMBER, which names one
         * thing and can therefore be judged on the spot. An id tag names a
         * GROUP, and the question for a group is whether ANY of its members is
         * drawn with the graphic -- which nothing can answer until the whole map
         * has been offered, so that one is made when it settles. */
        if (!entry->by_id && entry->has_graphic &&
            entry->graphic_id != graphic_id && !g_object_told[i]) {
            g_object_told[i] = 1u;
            fprintf(stderr, "[renderer_overhaul] %s: object %lu on %s is drawn "
                            "with %#06x, not the %#06x written beside it; the "
                            "entry is applied anyway\n",
                    g_look_path, (unsigned long)entry->key, g_look_map,
                    (unsigned)graphic_id, (unsigned)entry->graphic_id);
        }
    }
}

void hwr_look_objects_settled(void)
{
    size_t i;

    if (g_objects_settled)
        return;
    g_objects_settled = 1;
    for (i = 0; i < g_effective_objects.count; i++) {
        const struct hwr_look_object_entry *entry =
            &g_effective_objects.items[i];

        if (!g_object_matched[i]) {
            /* A number nothing answers to is a typed number that will never do
             * anything, and one that says nothing is one nobody finds. */
            if (entry->by_id)
                fprintf(stderr, "[renderer_overhaul] %s: no object tagged %lu "
                                "on %s\n",
                        g_look_path, (unsigned long)entry->key, g_look_map);
            else
                fprintf(stderr, "[renderer_overhaul] %s: no object %lu on %s\n",
                        g_look_path, (unsigned long)entry->key, g_look_map);
            continue;
        }
        /* And the group's own version of the graphic check, which only the
         * finished walk can answer: an id naming objects of which NOT ONE is
         * drawn with the graphic written beside it is an entry whose author was
         * describing something else. It is applied anyway, exactly as the
         * single object's is. */
        if (entry->by_id && entry->has_graphic && !g_object_drawn[i] &&
            !g_object_told[i]) {
            g_object_told[i] = 1u;
            fprintf(stderr, "[renderer_overhaul] %s: no object tagged %lu on %s "
                            "is drawn with the %#06x written beside it; the "
                            "entry is applied anyway\n",
                    g_look_path, (unsigned long)entry->key, g_look_map,
                    (unsigned)entry->graphic_id);
        }
    }
}

void hwr_look_clear(void)
{
    memset(&g_look, 0, sizeof g_look);
    memset(&g_effective, 0, sizeof g_effective);
    memset(&g_effective_lights, 0, sizeof g_effective_lights);
    if (g_effective_surfaces.count != 0u &&
        ++g_surface_generation == 0u)
        g_surface_generation = 1u;
    memset(&g_effective_surfaces, 0, sizeof g_effective_surfaces);
    if (g_effective_placed.count != 0u && ++g_placed_generation == 0u)
        g_placed_generation = 1u;
    memset(&g_effective_placed, 0, sizeof g_effective_placed);
    memset(&g_effective_objects, 0, sizeof g_effective_objects);
    memset(g_object_matched, 0, sizeof g_object_matched);
    memset(g_object_drawn, 0, sizeof g_object_drawn);
    memset(g_object_told, 0, sizeof g_object_told);
    g_objects_settled = 0;
    g_look_present = 0;
    g_look_path[0] = 0;
    g_look_dir[0] = 0;
    g_look_map[0] = 0;
    g_look_hash = 0;
    g_look_candidate_hash = 0;
    g_look_candidate_polls = 0;
    g_look_reload_pending = 0;
    /* The list back to none alone, and the selection with it. The row's value
     * is a place in that list, so a selection left standing over a list that
     * no longer holds it would be a value the row itself refuses -- which is a
     * settings file that cannot be saved. */
    hwr_look_slots_reset();
    hwr_settings_set_game_config(0);
    g_look_followed = 0;
}
