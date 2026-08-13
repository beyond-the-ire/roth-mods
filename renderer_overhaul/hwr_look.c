#include "hwr_look.h"

#include "hwr_json.h"
#include "hwr_util.h"

#include <dirent.h>
#include <stdarg.h>
#include <stddef.h>
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
/* How many entries one keyed grant table may hold in one scope -- the sprites
 * a place says something about, and the textures it says something about, which
 * are two tables of the same shape keyed by two different sixteen-bit numbers.
 * A place has a handful of things that give light; this is room for one that is
 * nothing but, and a game has a lava, a rune and a sigil or two rather than
 * sixty of them. */
#define HWR_LOOK_MAX_LIGHTS 64u
/* And how many OBJECTS one map may name. A hand-tuned map picks out the few
 * lights that carry a room -- the hearth, the brazier at the top of the stairs
 * -- so this is far past what tuning takes, and it is the same number as the
 * sprites above so that neither is the one somebody has to remember. Every
 * place carries a table of this size whether it fills one or not, which is what
 * it costs; it is the same bargain the sprites table already makes. */
#define HWR_LOOK_MAX_OBJECTS 64u
/* And how many FACES one map may name, which is the same table for the map's
 * own geometry that the one above is for the things standing on it. Same size
 * for the same reason. */
#define HWR_LOOK_MAX_FACES 64u
/* And how many FREE-STANDING lights one map may hang in the air. These are not
 * a tuning of something the map already carries -- each is a light somebody put
 * where the map has none -- so a map that wants many is a map being re-lit
 * rather than adjusted, and this is generous room for that. */
#define HWR_LOOK_MAX_PLACED 64u
/* And how many CEILINGS one place may call open sky. This is an escape hatch
 * for the handful of ceilings a map paints its own way, not a second way of
 * describing a map's roof, so a place that wants many of them is a place where
 * the reading itself is wrong. */
#define HWR_LOOK_MAX_SKY_CEILINGS 16u
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
 * the sprites table is keyed by the sprite a thing is drawn with and the
 * textures table by the texture a face is painted with, and what makes them two
 * tables is where they are read from and what consults them, never their shape.
 * One shape is one merge rule, and a rule written once cannot compose one table
 * differently from the other.
 *
 * `from_map` is the one thing about an entry that is not what it says: whether
 * the MAP's own section contributed to it. A game-wide entry is written once
 * for a whole game and is expected to name nothing on most of its maps; an
 * entry written under a map names something its author believed was in that
 * map. Only the second is worth a word when the map turns out not to carry
 * it. */
struct hwr_look_light_entry {
    uint16_t id;
    uint8_t from_map;
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
 *   THE ID -- `id` -- the record's own +0x0e word, which the editor shows as
 *   `ID:` and which a creator types to gather objects into a group for the
 *   command system. It is deliberately SHARED, so an entry keyed by it means
 *   EVERY object carrying it, and that is a feature: the six torches of a
 *   corridor given one ID are tuned in one line. Nought is refused -- it is the
 *   default every object carries and the command system's own word for "the
 *   source object", so it addresses everything and nothing.
 *
 * An entry carries one or the other and never both.
 *
 * AN ENTRY MAKES A LIGHT. Where the map itself marks a thing unshaded it is a
 * light already and the entry tunes it; where the map does not, the entry makes
 * it one, and the colour is read off the thing's own picture by the same law
 * that colours the ones the map marks. That rule lives where the light is
 * seeded; what lives here is only what the file said. */
struct hwr_look_object_entry {
    /* Which spelling: 0 an object number, 1 an ID. */
    int by_id;
    /* The number itself. An object number is a map-wide index and an ID is a
     * sixteen-bit word; one field holds either, because what tells them apart
     * is `by_id` rather than their range. */
    uint32_t key;
    struct hwr_look_light light;
};

struct hwr_look_objects {
    struct hwr_look_object_entry items[HWR_LOOK_MAX_OBJECTS];
    size_t count;
};

/* And exactly the same pair for one FACE of the map, because the editor offers
 * exactly the same pair for one:
 *
 *   THE FACE NUMBER -- `index` -- the position of the face record in the map's
 *   own flat face array, which the editor shows as `Face: N`. It names one
 *   wall.
 *
 *   THE FACE ID -- `id` -- the word at the end of an extended texture-mapping
 *   record, which the editor shows as the read-only `Face ID` and which the map
 *   command system addresses a face by. One mapping record may be shared by
 *   many faces, so it names a GROUP; nought is the default a face with none of
 *   its own carries and is refused, exactly as an object's is.
 *
 * A named face emits by itself: there is no map mark for a face the way there
 * is for a thing. A face grant that states no colour takes one off the texture
 * the face is actually painted with, by the same law a texture grant does. */
struct hwr_look_face_entry {
    int by_id;
    uint32_t key;
    struct hwr_look_light light;
};

struct hwr_look_faces {
    struct hwr_look_face_entry items[HWR_LOOK_MAX_FACES];
    size_t count;
};

/* A FREE-STANDING light: one that belongs to no object at all, hung at three
 * numbers somebody chose.
 *
 * This is what `at` addresses now, and it is a different thing from tuning a
 * light the map already has. Nothing is being described here -- there is no
 * object to read a sprite off, no sprite to take a colour from and no map
 * mark saying this is a light -- so the colour is REQUIRED, exactly as it is
 * for a granted texture and for the same reason: there is nothing to derive it
 * from. An entry with no colour lights nothing and says so.
 *
 * The three numbers held here are the GAME'S OWN, straight out of the file that
 * wrote them: the same space the map is measured in and the same space
 * HWR_OBJ_DUMP prints.
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

/* WHAT ONE PLACE SAYS ABOUT THE SKY'S OWN LIGHT that is not a settings row.
 *
 * The typed fields of the `environment_lighting` block -- whether it is lit at
 * all, its colour, its strength, how high it stands and which way it faces,
 * and the two shadow escapes -- are every one of them a row of the settings
 * table that a map is allowed to answer for, so they are TRANSLATED onto those
 * rows as the file is read and are held in `values` with every other override.
 * One merge, one precedence chain, one publish.
 *
 * These two have no row to be translated onto:
 *
 *   THE KIND is a label. Whether a place calls its sky light the sun or the
 *   moon decides which word is used for it and nothing else -- the colour and
 *   the strength are what it actually looks like -- so it is a word this layer
 *   carries rather than a value anything computes with.
 *
 *   THE OPEN CEILINGS are a LIST, and that makes them the one thing in this
 *   file that does not compose field by field: a list is one answer rather
 *   than a bag of fields, so a map's list REPLACES the one above it. Its
 *   presence is its own flag because an EMPTY list is a real statement -- no
 *   ceiling of this map is open to the sky -- and is a different thing from
 *   saying nothing and leaving the renderer's own reading standing. */
struct hwr_look_env {
    int has_kind;
    int kind;
    int has_ceilings;
    uint16_t ceilings[HWR_LOOK_MAX_SKY_CEILINGS];
    size_t ceiling_count;
};

/* One answer, whether it is the one every place gets or one place's own: they
 * have the same shape, and that is the whole of the composition rule.
 *
 * The two exceptions are the tables keyed by something that has no meaning
 * outside one map -- an object's own number or ID, and three numbers in a
 * building. Both are read only under a map; under `everywhere` each is refused
 * with a note. */
struct hwr_look_scope {
    struct hwr_look_values values;
    struct hwr_look_lights sprites;
    struct hwr_look_lights textures;
    struct hwr_look_objects objects;
    struct hwr_look_faces faces;
    struct hwr_look_placed placed;
    struct hwr_look_env env;
    /* Which rows of `values` this place's `environment_lighting` block wrote,
     * which is bookkeeping about the READING rather than part of the answer:
     * it is what lets one row written both ways in one place resolve to the
     * block whichever order the two were written in, and say so once. It is
     * never merged and never published. */
    struct hwr_row_set env_rows;
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
static struct hwr_look_lights g_effective_sprites;
static struct hwr_look_lights g_effective_textures;
static struct hwr_look_objects g_effective_objects;
static struct hwr_look_faces g_effective_faces;
static struct hwr_look_placed g_effective_placed;
static struct hwr_look_env g_effective_env;
/* And the same for the ceilings a place calls open sky, which the sky light
 * builds its casters against: a number that moves when the list does and at no
 * other time, so a map is worked out once rather than once a frame. */
static uint32_t g_sky_generation = 1u;
/* A number that moves when anything the map's own geometry is lit FROM does --
 * the granted textures, the named faces -- and at no other time. What reads it
 * works a map out ONCE and then wants to know, in one comparison per frame,
 * whether that answer is still the answer. It starts at one so that nought is
 * free to mean nothing has been worked out yet. */
static uint32_t g_glow_generation = 1u;
/* And the same for the free-standing lights, which are worked out against the
 * map's own rooms once when it is staged for exactly the same reason. */
static uint32_t g_placed_generation = 1u;
/* What has been SAID about each entry on the map in force, and whether the
 * map's own objects have been offered yet. Kept beside the entries rather than
 * in them, because what an entry is and what has been said about it are two
 * different things: one is the file's answer and moves when the file does, the
 * other is a conversation that starts again on every map.
 *
 * `matched` is whether anything DRAWN on the map answered to the entry's key,
 * and `hidden` whether the only thing that answered was a record the map
 * numbers and never draws. Two flags rather than one because "no such object"
 * and "there is one and it is hidden" are two different sentences, and a person
 * who has typed a number needs whichever of them is true. */
static unsigned char g_object_matched[HWR_LOOK_MAX_OBJECTS];
static unsigned char g_object_hidden[HWR_LOOK_MAX_OBJECTS];
/* And the same pair for the sprite blanket, which is the third table that can
 * name an object. A blanket is keyed by a PICTURE rather than by a number out
 * of this map's own lists, so "nothing here is drawn with it" is the only thing
 * that can be wrong with one -- and a blanket that quietly does nothing is
 * exactly as hard to find as a number that quietly does nothing. */
static unsigned char g_sprite_matched[HWR_LOOK_MAX_LIGHTS];
static unsigned char g_sprite_told[HWR_LOOK_MAX_LIGHTS];
static int g_objects_settled;
static char g_look_map[HWR_LOOK_NAME];
static uint64_t g_look_hash;
static uint64_t g_look_candidate_hash;
static uint32_t g_look_candidate_polls;
static uint32_t g_look_frames_until_poll;
static int g_look_reload_pending;
/* The content the watcher has ALREADY asked for, which is not the same thing as
 * the content in force: a file that settles at something that cannot be loaded
 * -- one missing brace -- leaves what was in force standing, so the hash in
 * force never becomes this one. Without it the same broken save would be asked
 * for again at every poll, and the sentence saying what is wrong with it would
 * be printed every second for as long as it stayed broken. */
static uint64_t g_look_asked_hash;
/* What the watcher saw settle, so the read that carries the request out can
 * tell a file that changed AGAIN in between from the one it was asked to load.
 * A request that came from anywhere else carries no expectation. */
static int g_look_reload_expects_hash;
static uint64_t g_look_reload_hash;

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

/* One grant laid over another, FIELD by field and `off` left alone. A map
 * raising the strength of a sprite the game-wide answer already coloured keeps
 * that colour, which is the same rule the settings rows compose by and the
 * reason a field a scope says nothing about is left out rather than written
 * down.
 *
 * Written ONCE and exported, so that the two scopes, the three tables that can
 * name an object and the three that can name a face all compose by one rule and
 * none of them can quietly grow one of its own. */
void hwr_look_light_over(struct hwr_look_light *into,
                         const struct hwr_look_light *from)
{
    if (into == NULL || from == NULL)
        return;
    if (from->has_intensity) {
        into->has_intensity = 1;
        into->intensity = from->intensity;
    }
    if (from->has_range) {
        into->has_range = 1;
        into->range = from->range;
    }
    if (from->has_color) {
        into->has_color = 1;
        into->color = from->color;
    }
    if (from->has_shadows) {
        into->has_shadows = 1;
        into->shadows = from->shadows;
    }
}

/* One scope's keyed entries over another's. `from_map` says whether this is the
 * map's own section rather than the answer every place gets, which is the one
 * thing an entry remembers about where it came from. */
static void hwr_look_lights_merge(struct hwr_look_lights *into,
                                  const struct hwr_look_lights *from,
                                  int from_map)
{
    size_t i;

    for (i = 0; i < from->count; i++) {
        const struct hwr_look_light_entry *source = &from->items[i];
        struct hwr_look_light_entry *target =
            hwr_look_light_entry(into, source->id);

        if (target == NULL)
            continue;
        if (from_map)
            target->from_map = 1u;
        if (source->light.off)
            target->light.off = 1;
        hwr_look_light_over(&target->light, &source->light);
    }
}

/* One place's sky light over another's -- the two parts of it that are not
 * settings rows, because the rest is composed with every other overridden row.
 *
 * The KIND composes field by field like everything else. The CEILINGS do not,
 * and this is the one place in the file where that is true: a list is one
 * answer rather than a bag of fields, so a place that writes one REPLACES the
 * list above it whole. Saying "the map adds two more" would leave nobody able
 * to take one away. */
static void hwr_look_env_over(struct hwr_look_env *into,
                              const struct hwr_look_env *from)
{
    if (from->has_kind) {
        into->has_kind = 1;
        into->kind = from->kind;
    }
    if (from->has_ceilings) {
        into->has_ceilings = 1;
        into->ceiling_count = from->ceiling_count;
        memcpy(into->ceilings, from->ceilings, sizeof into->ceilings);
    }
}

/* The entry for one address in one scope, created where there is not one.
 * Two entries written for one address are two statements about one thing, so
 * the second is laid over the first rather than standing beside it -- and an
 * object number and an ID that happen to be the same number are two different
 * addresses, which is why the spelling is part of the match. NULL
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

/* And the same for one FACE of the map, by the same rule and for the same
 * reasons: a face number and a Face ID that happen to be the same number are
 * two different addresses. */
static struct hwr_look_face_entry *hwr_look_face_entry(
    struct hwr_look_faces *faces, int by_id, uint32_t key)
{
    size_t i;

    for (i = 0; i < faces->count; i++) {
        if (faces->items[i].by_id == by_id && faces->items[i].key == key)
            return &faces->items[i];
    }
    if (faces->count >= HWR_LOOK_MAX_FACES)
        return NULL;
    memset(&faces->items[faces->count], 0, sizeof faces->items[0]);
    faces->items[faces->count].by_id = by_id;
    faces->items[faces->count].key = key;
    return &faces->items[faces->count++];
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

/* ---------------------------------------- the sky's own light, translated ---
 *
 * THE TRANSLATION TABLE: one field of the `environment_lighting` block, one
 * row of the settings table. Every one of these rows carries the map-override
 * flag, so each of them could equally be written in `settings` under its own
 * name -- the block is a friendlier SPELLING of the same statement, and this
 * is the whole of what makes it one. Both spellings then land in the same
 * override set, so there is one merge, one precedence chain and one publish.
 *
 * THE ROW IS NAMED BY THE FIELD IT WRITES, and that is what keeps this table
 * honest: `offsetof` is a compile-time name, so a row this table names and the
 * settings no longer carry does not quietly stop arriving -- IT FAILS TO
 * BUILD. The key handed to the settings table is the same identifier
 * stringified, so the two halves of an entry cannot drift into two spellings
 * either. Which row a key finds is still the settings table's own answer,
 * asked for by name at the moment the file is read.
 *
 * `kind` and `sky_ceilings` are not here: neither is a row, and both live in
 * the model beside the light grants. */
#define HWR_LOOK_ENV_ROW(field) #field, offsetof(struct hwr_settings, field)

struct hwr_look_env_row {
    /* What the block calls it. */
    const char *field;
    /* The settings row it writes, and that row's own field in the settings, so
     * that a row found by name can be held to the one this table meant. */
    const char *key;
    size_t offset;
};

static const struct hwr_look_env_row HWR_LOOK_ENV_TABLE[] = {
    { "on",             HWR_LOOK_ENV_ROW(sun_light) },
    { "colour",         HWR_LOOK_ENV_ROW(sun_color) },
    { "strength",       HWR_LOOK_ENV_ROW(sun_intensity) },
    { "elevation",      HWR_LOOK_ENV_ROW(sun_elevation_deg) },
    { "direction",      HWR_LOOK_ENV_ROW(sun_yaw_deg) },
    { "object_shadows", HWR_LOOK_ENV_ROW(sun_object_shadows) },
    { "shadow_quality", HWR_LOOK_ENV_ROW(sun_shadow_quality) }
};

/* Seven fields, seven rows. Written down so that growing the block by a field
 * that has no row, or losing one, is a decision somebody has to make on
 * purpose rather than a line that slips in. */
_Static_assert(sizeof HWR_LOOK_ENV_TABLE / sizeof HWR_LOOK_ENV_TABLE[0] == 7u,
               "the environment lighting block translates onto seven rows");

/* One key of a `settings` block against the settings table. A file may only
 * carry rows that say a map is allowed to override them, so there is no second
 * list of keys to keep in step with the first.
 *
 * `claimed` is the set of rows this place's `environment_lighting` block has
 * already answered for, where there is one: a row written both ways in one
 * place resolves to the block, whichever order the two were written in, and
 * says so once. */
static void hwr_look_read_setting(const struct hwr_json_doc *doc, int node,
                                  struct hwr_look_values *into,
                                  const struct hwr_row_set *claimed,
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
    if (claimed != NULL && hwr_row_set_has(claimed, index)) {
        hwr_look_note(report, where, "'%s' is what the environment lighting "
                                     "here already says; the environment "
                                     "lighting is the answer in force", key);
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
                                   struct hwr_look_scope *into,
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
        hwr_look_read_setting(doc, child, &into->values, &into->env_rows, where,
                              report);
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
    if (strcmp(field, "shadows") == 0) {
        int shadows = 0;

        if (!hwr_json_as_bool(doc, child, &shadows)) {
            hwr_look_note(report, where, "'shadows' is true or false");
            return 1;
        }
        into->has_shadows = 1;
        into->shadows = shadows;
        return 1;
    }
    return 0;
}

/* A TEXTURE NUMBER: the whole number the map's own geometry carries for a face
 * or a flat, which the editor prints in decimal and which is written here in
 * decimal. Any base is read, because a file written by hand in hexadecimal is a
 * file that meant a number rather than a file with a mistake in it -- but
 * decimal is what everything this mod prints and ships uses, so decimal is what
 * a person ever has to know. Answers 0 for anything that is not a number. */
static int hwr_look_texture_number(const char *text, uint16_t *out)
{
    unsigned long texture;
    char *end = NULL;

    if (text == NULL || text[0] == 0)
        return 0;
    texture = strtoul(text, &end, 0);
    if (end == text || *end != 0 || texture > 0xffffuL)
        return 0;
    *out = (uint16_t)texture;
    return 1;
}

/* A SPRITE, IN EITHER OF THE TWO NUMBERS THE EDITOR SHOWS FOR ONE.
 *
 * The object panel shows a PAIR -- Texture Index, Texture Source -- and "34/0"
 * is that panel read straight across. The picture-picking window shows ONE
 * number for the same picture, which is the position of that picture in the
 * archive it lives in: the map's own archive holds its object pictures from
 * 4096, and the shared archive holds its own from 0. So Texture Index 112 with
 * Texture Source 0 is picture 4208, and both spellings are accepted here
 * because both are printed by the same program for the same thing.
 *
 *   source 0  ->  4096 + index      the map's own archive
 *   source 1  ->  4352 + index      the map's own archive
 *   source 2  ->     0 + index      the shared archive
 *   source 3  ->   256 + index      the shared archive
 *
 * The four ranges do not overlap, so the single number names exactly one pair
 * and the conversion is exact both ways. A number between 512 and 4095 is a
 * WORLD texture rather than an object picture and names no sprite at all.
 *
 * The map record carries the pair as one sixteen-bit word -- the index in its
 * low byte, the source in its high byte -- and that word is what everything
 * downstream of here uses, so both spellings are folded into it once, here, and
 * they can never disagree. */
#define HWR_LOOK_SPRITE_MAP_FIRST 4096uL
#define HWR_LOOK_SPRITE_MAP_LIMIT 4608uL
#define HWR_LOOK_SPRITE_SHARED_LIMIT 512uL

static int hwr_look_sprite_number(const char *text, uint16_t *out)
{
    unsigned long index;
    unsigned long source;
    char *end = NULL;

    if (text == NULL || text[0] == 0)
        return 0;
    index = strtoul(text, &end, 0);
    if (end == text)
        return 0;
    /* THE SINGLE NUMBER, which is the picture's own place in its archive. The
     * shared archive's pictures keep the high half of the graphic word, which
     * is what the two sources above 1 mean. */
    if (*end == 0) {
        if (index < HWR_LOOK_SPRITE_SHARED_LIMIT) {
            *out = (uint16_t)(index + HWR_LOOK_SPRITE_SHARED_LIMIT);
            return 1;
        }
        if (index >= HWR_LOOK_SPRITE_MAP_FIRST &&
            index < HWR_LOOK_SPRITE_MAP_LIMIT) {
            *out = (uint16_t)(index - HWR_LOOK_SPRITE_MAP_FIRST);
            return 1;
        }
        return 0;
    }
    if (*end != '/' || index > 0xffuL)
        return 0;
    text = end + 1;
    source = strtoul(text, &end, 0);
    if (end == text || *end != 0 || source > 0xffuL)
        return 0;
    *out = (uint16_t)(index | (source << 8));
    return 1;
}

/* WHICH CEILINGS THIS PLACE CALLS OPEN SKY. A list rather than a block, and
 * the one thing in the file that replaces rather than composes: a map's list is
 * the whole answer where it writes one.
 *
 * An id is written as a number or as a piece of text in any base, exactly as a
 * texture is, because it IS a texture number -- the one a sector's ceiling
 * carries. An empty list is kept: it says that no ceiling of this map is open,
 * which is a different statement from writing no list at all. */
static void hwr_look_read_sky_ceilings(const struct hwr_json_doc *doc,
                                       int node, struct hwr_look_env *into,
                                       const char *where, int report)
{
    uint16_t ids[HWR_LOOK_MAX_SKY_CEILINGS];
    size_t count = 0;
    int child;

    if (hwr_json_type(doc, node) != HWR_JSON_ARRAY) {
        hwr_look_note(report, where, "'sky_ceilings' is a list of the ceiling "
                                     "texture numbers that are open to the "
                                     "sky, written [1, 2]");
        return;
    }
    for (child = hwr_json_first(doc, node); child >= 0;
         child = hwr_json_next(doc, child)) {
        double number = 0.0;
        uint16_t id = 0;

        if (count >= HWR_LOOK_MAX_SKY_CEILINGS) {
            hwr_look_note(report, where, "that is more open ceilings than one "
                                         "place may name");
            return;
        }
        if (hwr_json_as_number(doc, child, &number)) {
            if (number < 0.0 || number > 65535.0 ||
                number != (double)(long)number) {
                hwr_look_note(report, where, "a ceiling is a whole texture "
                                             "number between 0 and 65535");
                return;
            }
            id = (uint16_t)number;
        } else if (!hwr_look_texture_number(hwr_json_as_text(doc, child),
                                            &id)) {
            hwr_look_note(report, where, "a ceiling is a texture number, "
                                         "written 1 or \"1\"");
            return;
        }
        ids[count++] = id;
    }
    /* Written in one go at the end: a list that could not be read whole leaves
     * whatever was there standing, rather than replacing it with the half of
     * itself that parsed. */
    memcpy(into->ceilings, ids, count * sizeof ids[0]);
    into->ceiling_count = count;
    into->has_ceilings = 1;
}

/* THE SKY'S OWN LIGHT, as one place describes it. Every typed field goes
 * straight onto the settings row the table above pairs it with, read by that
 * row's own reader -- so every bound, every token and every complaint is the
 * settings screen's own, exactly as a key written in `settings` gets. */
static void hwr_look_read_env(const struct hwr_json_doc *doc, int object,
                              struct hwr_look_scope *into, const char *where,
                              int report)
{
    int child;

    if (hwr_json_type(doc, object) != HWR_JSON_OBJECT) {
        hwr_look_note(report, where, "the environment lighting is a block of "
                                     "what the sky's own light is like");
        return;
    }
    for (child = hwr_json_first(doc, object); child >= 0;
         child = hwr_json_next(doc, child)) {
        const char *name = hwr_json_name(doc, child);
        const struct hwr_setting_desc *desc;
        union hwr_value parsed;
        char message[128];
        char text[192];
        size_t entry;
        int index;

        if (name == NULL || hwr_look_is_notes(name))
            continue;
        if (strcmp(name, "kind") == 0) {
            const char *word = hwr_json_as_text(doc, child);

            /* A LABEL AND NOTHING ELSE. What the light looks like is its
             * colour and its strength; this decides which word is used for
             * it, so that a place lit by the moon is not called the sun. */
            if (word != NULL && strcmp(word, "sun") == 0) {
                into->env.has_kind = 1;
                into->env.kind = HWR_LOOK_SKY_SUN;
            } else if (word != NULL && strcmp(word, "moon") == 0) {
                into->env.has_kind = 1;
                into->env.kind = HWR_LOOK_SKY_MOON;
            } else {
                hwr_look_note(report, where, "'kind' is \"sun\" or \"moon\"");
            }
            continue;
        }
        if (strcmp(name, "sky_ceilings") == 0) {
            hwr_look_read_sky_ceilings(doc, child, &into->env, where, report);
            continue;
        }
        for (entry = 0; entry < sizeof HWR_LOOK_ENV_TABLE /
                                sizeof HWR_LOOK_ENV_TABLE[0]; entry++) {
            if (strcmp(name, HWR_LOOK_ENV_TABLE[entry].field) == 0)
                break;
        }
        if (entry == sizeof HWR_LOOK_ENV_TABLE /
                     sizeof HWR_LOOK_ENV_TABLE[0]) {
            hwr_look_note(report, where, "'%s' is not something the "
                                         "environment lighting says", name);
            continue;
        }
        desc = hwr_schema_find(HWR_LOOK_ENV_TABLE[entry].key);
        index = desc != NULL ? hwr_look_row_index(desc) : -1;
        if (desc == NULL || index < 0 ||
            desc->offset != HWR_LOOK_ENV_TABLE[entry].offset ||
            (desc->flags & HWR_SF_MAP_OVERRIDE) == 0) {
            /* The one complaint here that is not about the file: this build's
             * own table names a setting this build's own settings do not
             * answer for. Said in those words, so that nobody goes looking for
             * the mistake in what they wrote. */
            hwr_look_note(report, where, "'%s' has nothing to write to in this "
                                         "build; this is a fault in the mod "
                                         "rather than in the file", name);
            continue;
        }
        if (hwr_json_type(doc, child) == HWR_JSON_OBJECT ||
            hwr_json_type(doc, child) == HWR_JSON_ARRAY) {
            hwr_look_note(report, where, "'%s' is a single value, not a list "
                                         "or a block", name);
            continue;
        }
        hwr_json_render(doc, child, text, sizeof text);
        message[0] = 0;
        if (!hwr_schema_parse(desc, text, &parsed, message, sizeof message)) {
            hwr_look_note(report, where, "'%s': %s", name, message);
            continue;
        }
        /* THE BLOCK IS THE ANSWER IN FORCE where this place writes the same
         * value both ways. Said once, whichever of the two was written
         * first: the settings reader refuses a row this set already holds,
         * and this overwrites one the settings reader got to first. */
        if (hwr_row_set_has(&into->values.present, index) &&
            !hwr_row_set_has(&into->env_rows, index))
            hwr_look_note(report, where, "'%s' is what '%s' in the settings "
                                         "here already says; the environment "
                                         "lighting is the answer in force",
                          name, HWR_LOOK_ENV_TABLE[entry].key);
        hwr_look_set(&into->values, index, parsed);
        hwr_row_set_add(&into->env_rows, index);
    }
}

/* What one keyed thing gives off: one sprite, or one texture.
 *
 * `noun` is what the complaints call the thing, and `by_pair` which of the two
 * key spellings this table takes. The reading is otherwise the same for both,
 * and deliberately so: two readers would be two places for the three fields to
 * be spelled and one place for them to drift. */
static void hwr_look_read_light(const struct hwr_json_doc *doc, int node,
                                struct hwr_look_lights *into,
                                const char *where, int report,
                                const char *noun, int by_pair)
{
    const char *key = hwr_json_name(doc, node);
    struct hwr_look_light_entry *entry;
    char inside[HWR_LOOK_WHERE];
    uint16_t graphic = 0;
    int child;

    if (key == NULL)
        return;
    if (by_pair ? !hwr_look_sprite_number(key, &graphic)
                : !hwr_look_texture_number(key, &graphic)) {
        if (by_pair)
            hwr_look_note(report, where, "'%s' is not a sprite; a sprite is "
                                         "either the texture and source "
                                         "numbers the editor shows for an "
                                         "object, written \"34/0\", or the "
                                         "single picture number it shows when "
                                         "you pick one, such as \"4130\"", key);
        else if (strchr(key, '/') != NULL)
            /* THE ONE MISTAKE WORTH TEACHING RATHER THAN REFUSING. The pair
             * belongs to an OBJECT and is what `sprites` is keyed by; a texture
             * is the single number a surface is painted with. The two tables
             * look alike enough that writing one key in the other's block is
             * the natural mistake, and "not a texture number" alone does not
             * tell anybody which of the two they have. */
            hwr_look_note(report, where, "'%s' is a sprite, not a texture: "
                                         "the pair names an OBJECT. Textures "
                                         "are the single numbers surfaces are "
                                         "painted with; move this line into "
                                         "'sprites'", key);
        else
            hwr_look_note(report, where, "'%s' is not a texture number", key);
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
 * sees.
 *
 * THE THREE NUMBERS ARE THE GAME'S OWN, exactly as the map file carries them
 * and exactly as HWR_OBJ_DUMP prints them: what is written here is what the
 * world is measured in, and nothing is flipped, scaled or reinterpreted on the
 * way in. The community editor happens to DISPLAY X with the opposite sign, so
 * an X copied off its position box has to be flipped before it is written here
 * -- but that is a fact about the editor's screen, and this layer is not the
 * place to guess which of the two a number came from. */
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
 * number: an object's number and its ID are counts of things, and neither
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

/* One object of the map: which one, and what it gives off.
 *
 * THE ADDRESS IS THE EDITOR'S. `index` is the object number the community ROTH
 * Editor shows as `Object: N`; `id` is what it shows as `ID:` and names every
 * object carrying it. One or the other, never both -- an entry carrying both is
 * two different addresses written as one, and there is no reading of it that is
 * not a guess.
 *
 * The address is read BEFORE `off` is asked about, which is the one place the
 * "nothing else in the entry is read" rule has an edge worth stating: which
 * object it is is what ADDRESSES the entry rather than what it gives off, so an
 * entry that turns one thing off is still held against what is actually there.
 * An entry nobody can check is an entry that goes stale in silence. */
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
                                      "or a group of them by their ID, not "
                                      "both");
        return;
    }
    if (by_index < 0 && by_id < 0) {
        hwr_look_note(report, inside, "a placed object is addressed by its "
                                      "number, written \"index\": 12, or by its "
                                      "ID, written \"id\": 7 -- both are the "
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
        if (!hwr_look_read_whole(doc, by_id, 0xffffu, &key, "ID", inside,
                                 report))
            return;
        /* NOUGHT IS NOT AN ADDRESS. It is the default every object carries
         * until a creator types one, and the command system's own word for "the
         * object this command came from" -- so an entry keyed by it would name
         * most of the map and mean nothing by it. */
        if (key == 0u) {
            hwr_look_note(report, inside, "an ID of 0 is the default that most "
                                          "objects carry, so it addresses "
                                          "nothing here");
            return;
        }
        by_tag = 1;
    }
    entry = hwr_look_object_entry(into, by_tag, key);
    if (entry == NULL) {
        hwr_look_note(report, inside, "that is more objects than one map may "
                                      "describe");
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
            strcmp(field, "index") == 0 || strcmp(field, "id") == 0 ||
            strcmp(field, "off") == 0)
            continue;
        if (!hwr_look_grant_field(doc, child, &entry->light, inside, report))
            hwr_look_note(report, inside, "'%s' is not something a light gives "
                                          "off", field);
    }
}

/* The objects one map picks out. A LIST rather than a block, because one entry
 * may name a whole group of objects and because a number is not a name. */
static void hwr_look_read_objects(const struct hwr_json_doc *doc, int array,
                                  struct hwr_look_objects *into,
                                  const char *where, int report)
{
    size_t index = 0;
    int child;

    if (hwr_json_type(doc, array) != HWR_JSON_ARRAY) {
        hwr_look_note(report, where, "the objects are a list, because each "
                                     "names an object by a number rather than "
                                     "by a name");
        return;
    }
    for (child = hwr_json_first(doc, array); child >= 0;
         child = hwr_json_next(doc, child))
        hwr_look_read_object(doc, child, into, index++, where, report);
}

/* One face of the map: which one, and what it gives off. The same two spellings
 * the objects list takes, read by the same rules and refusing the same things,
 * because the editor offers the same pair for a face and a person reading them
 * off one panel must not have to learn two grammars. */
static void hwr_look_read_face(const struct hwr_json_doc *doc, int node,
                               struct hwr_look_faces *into, size_t index,
                               const char *where, int report)
{
    struct hwr_look_face_entry *entry;
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
        hwr_look_note(report, inside, "one face is a block of which face it is "
                                      "and what it gives off");
        return;
    }
    by_index = hwr_json_member(doc, node, "index");
    by_id = hwr_json_member(doc, node, "id");
    if (by_index >= 0 && by_id >= 0) {
        hwr_look_note(report, inside, "an entry names one face by its number "
                                      "or a group of them by their ID, not "
                                      "both");
        return;
    }
    if (by_index < 0 && by_id < 0) {
        hwr_look_note(report, inside, "a face is addressed by its number, "
                                      "written \"index\": 2430, or by its ID, "
                                      "written \"id\": 11 -- both are the "
                                      "numbers the map editor shows for it");
        return;
    }
    if (by_index >= 0) {
        if (!hwr_look_read_whole(doc, by_index, 0xffffffu, &key,
                                 "face number", inside, report))
            return;
    } else {
        if (!hwr_look_read_whole(doc, by_id, 0xffffu, &key, "ID", inside,
                                 report))
            return;
        /* NOUGHT IS NOT AN ADDRESS here either: it is what every face whose
         * mapping record carries no ID of its own reads as, which is most of a
         * map. */
        if (key == 0u) {
            hwr_look_note(report, inside, "an ID of 0 is what a face with none "
                                          "of its own carries, so it addresses "
                                          "nothing here");
            return;
        }
        by_tag = 1;
    }
    entry = hwr_look_face_entry(into, by_tag, key);
    if (entry == NULL) {
        hwr_look_note(report, inside, "that is more faces than one map may "
                                      "describe");
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
            strcmp(field, "index") == 0 || strcmp(field, "id") == 0 ||
            strcmp(field, "off") == 0)
            continue;
        if (!hwr_look_grant_field(doc, child, &entry->light, inside, report))
            hwr_look_note(report, inside, "'%s' is not something a light gives "
                                          "off", field);
    }
}

static void hwr_look_read_faces(const struct hwr_json_doc *doc, int array,
                                struct hwr_look_faces *into,
                                const char *where, int report)
{
    size_t index = 0;
    int child;

    if (hwr_json_type(doc, array) != HWR_JSON_ARRAY) {
        hwr_look_note(report, where, "the faces are a list, because each names "
                                     "a face by a number rather than by a "
                                     "name");
        return;
    }
    for (child = hwr_json_first(doc, array); child >= 0;
         child = hwr_json_next(doc, child))
        hwr_look_read_face(doc, child, into, index++, where, report);
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
        if (strcmp(name, "sprites") == 0) {
            int light;

            /* A BLANKET over every object drawn with one sprite, keyed by the
             * texture and source pair the editor's object panel shows. Read in
             * every scope: a sprite is the same sprite wherever it is drawn. */
            hwr_look_where(inside, where, "sprites");
            if (hwr_json_type(doc, child) != HWR_JSON_OBJECT) {
                hwr_look_note(report, inside, "the sprites are a block keyed "
                                              "by the texture and source "
                                              "numbers the editor shows for an "
                                              "object, written \"34/0\", or by "
                                              "the single picture number it "
                                              "shows when you pick one");
                continue;
            }
            for (light = hwr_json_first(doc, child); light >= 0;
                 light = hwr_json_next(doc, light)) {
                if (hwr_look_is_notes(hwr_json_name(doc, light)))
                    continue;
                hwr_look_read_light(doc, light, &into->sprites, inside, report,
                                    "sprite", 1);
            }
        } else if (strcmp(name, "textures") == 0) {
            int texture;

            /* And a BLANKET over the map's own geometry: every wall band and
             * every floor and ceiling painted with one texture -- lava, a rune,
             * a lit sigil. Read in every scope for the same reason: a texture
             * is the same texture wherever it is painted, and a game whose lava
             * glows wants it to glow everywhere. */
            hwr_look_where(inside, where, "textures");
            if (hwr_json_type(doc, child) != HWR_JSON_OBJECT) {
                hwr_look_note(report, inside, "the textures are a block keyed "
                                              "by the number of a texture the "
                                              "map paints with");
                continue;
            }
            for (texture = hwr_json_first(doc, child); texture >= 0;
                 texture = hwr_json_next(doc, texture)) {
                if (hwr_look_is_notes(hwr_json_name(doc, texture)))
                    continue;
                hwr_look_read_light(doc, texture, &into->textures, inside,
                                    report, "texture", 0);
            }
        } else if (strcmp(name, "objects") == 0) {
            hwr_look_where(inside, where, "objects");
            if (!placed) {
                hwr_look_note(report, inside, "an object's number and its ID "
                                              "belong to ONE map, so an object "
                                              "is described under a map rather "
                                              "than everywhere");
                continue;
            }
            hwr_look_read_objects(doc, child, &into->objects, inside, report);
        } else if (strcmp(name, "faces") == 0) {
            hwr_look_where(inside, where, "faces");
            if (!placed) {
                hwr_look_note(report, inside, "a face's number and its ID "
                                              "belong to ONE map, so a face is "
                                              "described under a map rather "
                                              "than everywhere");
                continue;
            }
            hwr_look_read_faces(doc, child, &into->faces, inside, report);
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
            hwr_look_read_settings(doc, child, into, inside, report);
        } else if (strcmp(name, "lights") == 0) {
            hwr_look_where(inside, where, "lights");
            hwr_look_read_lights(doc, child, into, inside, report, placed);
        } else if (strcmp(name, "environment_lighting") == 0) {
            /* Read in EVERY scope: the sky is the same sky over a whole game,
             * and what changes per map is which way its openings face -- which
             * is exactly the field a map writes and the game-wide answer
             * mostly does not. */
            hwr_look_where(inside, where, "environment_lighting");
            hwr_look_read_env(doc, child, into, inside, report);
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
 * `content_hash` is written for a file that was READ, whether or not it went on
 * to parse. That is what lets the watcher tell one broken save from the next
 * one and say what is wrong with each exactly once, instead of either saying it
 * every second or never saying it at all.
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
    *content_hash = hash;
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
    return 1;
}

/* ---------------------------------------------------- what is in force --- */

/* The overrides in force for one map, lowest precedence first. */
static void hwr_look_compose(const struct hwr_look_file *file,
                             const char *map_name,
                             struct hwr_look_values *out,
                             struct hwr_look_lights *out_sprites,
                             struct hwr_look_lights *out_textures,
                             struct hwr_look_objects *out_objects,
                             struct hwr_look_faces *out_faces,
                             struct hwr_look_placed *out_placed,
                             struct hwr_look_env *out_env)
{
    const struct hwr_look_map *map;

    memset(out, 0, sizeof *out);
    memset(out_sprites, 0, sizeof *out_sprites);
    memset(out_textures, 0, sizeof *out_textures);
    memset(out_objects, 0, sizeof *out_objects);
    memset(out_faces, 0, sizeof *out_faces);
    memset(out_placed, 0, sizeof *out_placed);
    memset(out_env, 0, sizeof *out_env);
    if (!g_look_present)
        return;
    hwr_look_merge(out, &file->everywhere.values);
    hwr_look_lights_merge(out_sprites, &file->everywhere.sprites, 0);
    hwr_look_lights_merge(out_textures, &file->everywhere.textures, 0);
    hwr_look_env_over(out_env, &file->everywhere.env);
    map = hwr_look_find_map(file, map_name);
    if (map != NULL) {
        hwr_look_merge(out, &map->scope.values);
        hwr_look_lights_merge(out_sprites, &map->scope.sprites, 1);
        hwr_look_lights_merge(out_textures, &map->scope.textures, 1);
        hwr_look_env_over(out_env, &map->scope.env);
        /* And the three tables that come from ONE scope: the map's own objects
         * and faces, which are the last word, and its free-standing lights.
         * There is no game-wide table of any of them to compose with, because a
         * number in one map's list and a position inside one building name
         * nothing anywhere else. */
        *out_objects = map->scope.objects;
        *out_faces = map->scope.faces;
        *out_placed = map->scope.placed;
    }
}

/* Publishes a freshly composed override set and reports what moved, so the rows
 * that changed value are applied and nothing else is disturbed. */
static void hwr_look_publish(const struct hwr_look_values *next,
                             const struct hwr_look_lights *next_sprites,
                             const struct hwr_look_lights *next_textures,
                             const struct hwr_look_objects *next_objects,
                             const struct hwr_look_faces *next_faces,
                             const struct hwr_look_placed *next_placed,
                             const struct hwr_look_env *next_env)
{
    uint32_t changed = 0;
    size_t count = hwr_schema_count();
    size_t i;
    int glow_moved;
    int placed_moved;
    int sky_moved;

    /* The sprites table is not a row and raises no row's bit of its own, so it
     * raises the rig's: what it changes is what the lights that are not at the
     * eye are worth, which is the same thing every row of that family changes.
     * The objects, the faces, the textures and the placed lights are the same
     * table said more precisely, so they raise the same bit. */
    glow_moved =
        next_textures->count != g_effective_textures.count ||
        memcmp(next_textures->items, g_effective_textures.items,
               next_textures->count * sizeof next_textures->items[0]) != 0 ||
        next_faces->count != g_effective_faces.count ||
        memcmp(next_faces->items, g_effective_faces.items,
               next_faces->count * sizeof next_faces->items[0]) != 0;
    placed_moved =
        next_placed->count != g_effective_placed.count ||
        memcmp(next_placed->items, g_effective_placed.items,
               next_placed->count * sizeof next_placed->items[0]) != 0;
    /* And the ceilings the place in force calls open sky, which are the one
     * part of the sky's own light that is not a row. What reads them builds a
     * map's casters against them ONCE, so this comparison is what says that
     * answer is stale -- and it is exact rather than raised on every reload,
     * because a list that did not move is a map that does not need walking
     * again. */
    sky_moved =
        next_env->has_ceilings != g_effective_env.has_ceilings ||
        next_env->ceiling_count != g_effective_env.ceiling_count ||
        memcmp(next_env->ceilings, g_effective_env.ceilings,
               next_env->ceiling_count * sizeof next_env->ceilings[0]) != 0;
    if (next_sprites->count != g_effective_sprites.count ||
        memcmp(next_sprites->items, g_effective_sprites.items,
               next_sprites->count * sizeof next_sprites->items[0]) != 0 ||
        glow_moved || placed_moved ||
        next_objects->count != g_effective_objects.count ||
        memcmp(next_objects->items, g_effective_objects.items,
               next_objects->count * sizeof next_objects->items[0]) != 0 ||
        sky_moved || next_env->has_kind != g_effective_env.has_kind ||
        next_env->kind != g_effective_env.kind)
        changed |= HWR_CHANGE_TRUECOLOR;
    g_effective_sprites = *next_sprites;
    /* And the one thing a seeding that works a whole map out ONCE has to be
     * told about: what the map's own geometry is lit from moving under it.
     * Raised only where it actually moved, because what reads this is deciding
     * whether to walk a map again. */
    if (glow_moved && ++g_glow_generation == 0u)
        g_glow_generation = 1u;         /* never nought: nought means unstaged */
    g_effective_textures = *next_textures;
    g_effective_faces = *next_faces;
    /* The free-standing lights carry a generation for the same reason: which
     * room each one hangs in is a fact about the map, worked out once when it is
     * staged, and this is what says that answer is stale. */
    if (placed_moved && ++g_placed_generation == 0u)
        g_placed_generation = 1u;
    if (sky_moved && ++g_sky_generation == 0u)
        g_sky_generation = 1u;
    g_effective_placed = *next_placed;
    g_effective_objects = *next_objects;
    g_effective_env = *next_env;
    /* A new set of objects is a new conversation about them: what has been said
     * is about the map in force, and the map in force has just been decided. */
    memset(g_object_matched, 0, sizeof g_object_matched);
    memset(g_object_hidden, 0, sizeof g_object_hidden);
    memset(g_sprite_matched, 0, sizeof g_sprite_matched);
    memset(g_sprite_told, 0, sizeof g_sprite_told);
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
    struct hwr_look_lights no_sprites;
    struct hwr_look_lights no_textures;
    struct hwr_look_objects no_objects;
    struct hwr_look_faces no_faces;
    struct hwr_look_placed no_placed;
    struct hwr_look_env no_env;

    memset(&empty, 0, sizeof empty);
    memset(&no_sprites, 0, sizeof no_sprites);
    memset(&no_textures, 0, sizeof no_textures);
    memset(&no_objects, 0, sizeof no_objects);
    memset(&no_faces, 0, sizeof no_faces);
    memset(&no_placed, 0, sizeof no_placed);
    memset(&no_env, 0, sizeof no_env);
    hwr_look_publish(&empty, &no_sprites, &no_textures, &no_objects, &no_faces,
                     &no_placed, &no_env);
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
    g_look_asked_hash = 0;
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
 * wants and what one missing brace must never be allowed to undo.
 *
 * `expect`, where the watcher passes one, is the content it saw settle: a file
 * that has changed again between settling and being read is left to settle once
 * more rather than loaded half written. */
static void hwr_look_take_selection(int report, const uint64_t *expect)
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
    /* Whatever was there has now been asked for, loadable or not: this is what
     * keeps one broken save to one sentence. */
    if (read != 0)
        g_look_asked_hash = hash;
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
    if (expect != NULL && hash != *expect) {
        /* It changed again between settling and being read. Let it settle once
         * more rather than loading a file nobody has seen twice. */
        free(next);
        g_look_candidate_hash = hash;
        g_look_candidate_polls = 1;
        g_look_asked_hash = g_look_hash;
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
    /* THE COMPLETE REAPPLY. A reload is a person asking for what the file
     * says NOW, and half of what it says about a map was worked out against
     * the world when the map was staged: which room each free-standing
     * light hangs in, which faces carry a granted texture. Re-composing the
     * tables without re-staging that state left the world missing lights
     * the file grants until the next map load. So every reload path --
     * the selection moved, Reload From File, the reload button, the dev
     * watcher -- ends here, and here the map-scoped light state is marked
     * stale unconditionally: the lights layer re-stages it at the next
     * frame, against the world as it stands. One walk of the map per
     * button press is what it costs. */
    if (++g_glow_generation == 0u)
        g_glow_generation = 1u;
    if (++g_placed_generation == 0u)
        g_placed_generation = 1u;
}

void hwr_look_load(const char *plugin_dir)
{
    if (plugin_dir != NULL)
        snprintf(g_look_dir, sizeof g_look_dir, "%s", plugin_dir);
    hwr_look_note_retired_file();
    g_look_map[0] = 0;
    hwr_look_take_selection(1, NULL);
}

void hwr_look_follow_selection(void)
{
    if (hwr_settings_get()->game_config == g_look_followed)
        return;
    /* One comparison per pump, and deliberately not an apply hook: the look's
     * overrides have to be in force BEFORE the hooks that consume them run,
     * and publishing from inside one would invert that order.
     *
     * ONLY the picker moving asks for a read. A settings Apply that leaves
     * the row where it was is not a request to re-read the game config --
     * re-reading on every write of the store was asked for once and refused:
     * a person who edited the file mid-map presses the reload button beside
     * the picker, and that button is the whole of the contract. */
    g_look_followed = hwr_settings_get()->game_config;
    g_look_reload_pending = 1;
}

void hwr_look_request_reload(void)
{
    if (g_look_dir[0] == 0)
        return;
    g_look_reload_pending = 1;
    g_look_reload_expects_hash = 0;
}

/* THE FILE WATCHER, a development convenience behind HWR_CFG_WATCH=1 and off
 * otherwise -- the director's ruling, said plainly: the shipped build must
 * not poll the disk and re-read the file on its own. A person who edited the
 * chosen file presses the reload button beside the picker, or Reload From
 * File; with the switch set, saving the file is picked up within about a
 * second, on the same terms those buttons have.
 *
 * Every thirty frames, and never oftener: a read of a file this size is
 * nothing, but a read per frame would be sixty of them a second for as long as
 * the game is up.
 *
 * A changed file must be seen TWICE with the same content before it is asked
 * for, so an editor's truncate-and-rewrite intermediate state -- which is a
 * different file for a few milliseconds -- is never what gets loaded. */
void hwr_look_poll(void)
{
    static int watch = -1;
    struct hwr_look_file *probe;
    uint64_t hash = 0;
    int read;

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
    read = hwr_look_read(probe, &hash, 0);
    free(probe);
    /* A file that is not there is already answered for: the selection keeps its
     * place in the list, and what was in force stays in force until somebody
     * puts the file back. */
    if (read == 0)
        return;
    /* The content in force, and the content already asked for -- which are two
     * different things, because a file that cannot be loaded never becomes the
     * one in force. Either way there is nothing here to ask for again. */
    if (hash == g_look_hash || hash == g_look_asked_hash) {
        g_look_candidate_polls = 0;
        return;
    }
    if (hash != g_look_candidate_hash) {
        g_look_candidate_hash = hash;
        g_look_candidate_polls = 1;
        return;
    }
    if (g_look_candidate_polls++ < 1)
        return;
    /* Asking is all the watcher does. The reading happens where every other
     * reading happens -- the settings pump's safe point -- so a file can never
     * land in the middle of a frame, and a file that turns out to be broken
     * says so THERE, once, in the same sentence Reload From File would get. */
    g_look_reload_pending = 1;
    g_look_reload_expects_hash = 1;
    g_look_reload_hash = hash;
}

void hwr_look_drain_request(void)
{
    uint64_t expected;
    int expects;

    if (!g_look_reload_pending)
        return;
    g_look_reload_pending = 0;
    expects = g_look_reload_expects_hash;
    expected = g_look_reload_hash;
    g_look_reload_expects_hash = 0;
    /* Whatever the picker names, read again. The folder itself is listed by the
     * settings file's own reload, which runs first in the pump and is what
     * Reload From File asks for beside this -- so by the time this runs, a file
     * put in the folder while the game was running is already in the picker. A
     * request that came from the file watcher instead is about the CONTENT of
     * the chosen file, and the folder has not moved for it. */
    hwr_look_take_selection(1, expects ? &expected : NULL);
}

void hwr_look_select_map(const char *map_name)
{
    struct hwr_look_values next;
    struct hwr_look_lights next_sprites;
    struct hwr_look_lights next_textures;
    struct hwr_look_objects next_objects;
    struct hwr_look_faces next_faces;
    struct hwr_look_placed next_placed;
    struct hwr_look_env next_env;
    char reduced[HWR_LOOK_NAME];

    /* Reduced into a buffer of its own FIRST: the reload path passes the
     * held name back in as `map_name`, and the reducer writes its output's
     * first byte before it reads its input -- reducing straight into
     * g_look_map truncated the held name to nothing, the compose below then
     * found no map, and one mid-map re-read silently dropped the whole map
     * scope: every placed light, every grant and every per-map dial a person
     * had written. That was the wipe. */
    hwr_look_map_name(reduced, HWR_LOOK_NAME, map_name);
    memcpy(g_look_map, reduced, HWR_LOOK_NAME);
    hwr_look_compose(&g_look, g_look_map, &next, &next_sprites, &next_textures,
                     &next_objects, &next_faces, &next_placed, &next_env);
    hwr_look_publish(&next, &next_sprites, &next_textures, &next_objects,
                     &next_faces, &next_placed, &next_env);
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

int hwr_look_light_for_sprite(uint16_t graphic_id, struct hwr_look_light *out)
{
    size_t i;

    if (out == NULL)
        return 0;
    memset(out, 0, sizeof *out);
    for (i = 0; i < g_effective_sprites.count; i++) {
        if (g_effective_sprites.items[i].id != graphic_id)
            continue;
        *out = g_effective_sprites.items[i].light;
        return 1;
    }
    return 0;
}

/* Which slot of each of the three tables names one object, or -1. Written once
 * and used twice -- by the composition below and by the report that has to say
 * WHICH entry did nothing -- so a note can never blame a table other than the
 * one that answered. */
static int hwr_look_object_slots(uint32_t map_index, uint16_t object_id,
                                 uint16_t graphic_id, int *sprite_slot,
                                 int *tag_slot, int *number_slot)
{
    size_t i;

    *sprite_slot = -1;
    *tag_slot = -1;
    *number_slot = -1;
    for (i = 0; i < g_effective_sprites.count; i++) {
        if (g_effective_sprites.items[i].id == graphic_id) {
            *sprite_slot = (int)i;
            break;
        }
    }
    for (i = 0; i < g_effective_objects.count; i++) {
        const struct hwr_look_object_entry *entry =
            &g_effective_objects.items[i];

        /* Nought is refused where the file is read, so nothing here can be
         * keyed by it -- but an object carrying no ID of its own must not
         * match one either. */
        if (entry->by_id) {
            if (object_id != 0u && entry->key == (uint32_t)object_id)
                *tag_slot = (int)i;
        } else if (entry->key == map_index) {
            *number_slot = (int)i;
        }
    }
    return *sprite_slot >= 0 || *tag_slot >= 0 || *number_slot >= 0;
}

int hwr_look_object_grant(uint32_t map_index, uint16_t object_id,
                          uint16_t graphic_id, struct hwr_look_light *out)
{
    int sprite_slot;
    int tag_slot;
    int number_slot;

    if (out == NULL)
        return 0;
    memset(out, 0, sizeof *out);
    if (!hwr_look_object_slots(map_index, object_id, graphic_id, &sprite_slot,
                               &tag_slot, &number_slot))
        return 0;
    /* LEAST SPECIFIC FIRST, field by field: a kind is every object of that
     * sprite in the game, an ID is the ones somebody grouped by hand, and a
     * number is exactly one thing. */
    if (sprite_slot >= 0)
        hwr_look_light_over(out,
                            &g_effective_sprites.items[sprite_slot].light);
    if (tag_slot >= 0)
        hwr_look_light_over(out, &g_effective_objects.items[tag_slot].light);
    if (number_slot >= 0)
        hwr_look_light_over(out, &g_effective_objects.items[number_slot].light);
    /* AND `off` WHOLE, FROM THE MOST SPECIFIC TABLE NAMING IT. It is the entry
     * rather than a field, so one object of a refused kind may be called a
     * light again, and one member of a group may be refused. */
    if (number_slot >= 0)
        out->off = g_effective_objects.items[number_slot].light.off;
    else if (tag_slot >= 0)
        out->off = g_effective_objects.items[tag_slot].light.off;
    else
        out->off = g_effective_sprites.items[sprite_slot].light.off;
    return 1;
}

int hwr_look_light_for_texture(uint16_t texture_id, struct hwr_look_light *out)
{
    size_t i;

    if (out == NULL)
        return 0;
    memset(out, 0, sizeof *out);
    for (i = 0; i < g_effective_textures.count; i++) {
        if (g_effective_textures.items[i].id != texture_id)
            continue;
        *out = g_effective_textures.items[i].light;
        return 1;
    }
    return 0;
}

size_t hwr_look_texture_count(void)
{
    return g_effective_textures.count;
}

int hwr_look_texture_at(size_t index, uint16_t *texture_id, int *map_scoped,
                        struct hwr_look_light *out)
{
    if (index >= g_effective_textures.count || texture_id == NULL ||
        map_scoped == NULL || out == NULL)
        return 0;
    *texture_id = g_effective_textures.items[index].id;
    *map_scoped = g_effective_textures.items[index].from_map != 0u;
    *out = g_effective_textures.items[index].light;
    return 1;
}

uint32_t hwr_look_glow_generation(void)
{
    return g_glow_generation;
}

size_t hwr_look_face_count(void)
{
    return g_effective_faces.count;
}

int hwr_look_face_at(size_t index, int *by_id, uint32_t *key,
                     struct hwr_look_light *out)
{
    if (index >= g_effective_faces.count || by_id == NULL || key == NULL ||
        out == NULL)
        return 0;
    *by_id = g_effective_faces.items[index].by_id;
    *key = g_effective_faces.items[index].key;
    *out = g_effective_faces.items[index].light;
    return 1;
}

int hwr_look_face_grant(uint32_t face_index, uint16_t face_tag,
                        struct hwr_look_light *out)
{
    int tag_slot = -1;
    int number_slot = -1;
    size_t i;

    if (out == NULL)
        return 0;
    memset(out, 0, sizeof *out);
    for (i = 0; i < g_effective_faces.count; i++) {
        const struct hwr_look_face_entry *entry = &g_effective_faces.items[i];

        if (entry->by_id) {
            if (face_tag != 0u && entry->key == (uint32_t)face_tag)
                tag_slot = (int)i;
        } else if (entry->key == face_index) {
            number_slot = (int)i;
        }
    }
    if (tag_slot < 0 && number_slot < 0)
        return 0;
    if (tag_slot >= 0)
        hwr_look_light_over(out, &g_effective_faces.items[tag_slot].light);
    if (number_slot >= 0)
        hwr_look_light_over(out, &g_effective_faces.items[number_slot].light);
    out->off = number_slot >= 0
             ? g_effective_faces.items[number_slot].light.off
             : g_effective_faces.items[tag_slot].light.off;
    return 1;
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

/* ------------------------------------------------ the sky's own light --- */

int hwr_look_sky_kind(void)
{
    return g_effective_env.has_kind ? g_effective_env.kind : HWR_LOOK_SKY_SUN;
}

int hwr_look_sky_ceilings_stated(void)
{
    return g_effective_env.has_ceilings;
}

size_t hwr_look_sky_ceiling_count(void)
{
    return g_effective_env.has_ceilings ? g_effective_env.ceiling_count : 0u;
}

int hwr_look_sky_ceiling(uint16_t flat_id)
{
    size_t i;

    if (!g_effective_env.has_ceilings)
        return 0;
    for (i = 0; i < g_effective_env.ceiling_count; i++) {
        if (g_effective_env.ceilings[i] == flat_id)
            return 1;
    }
    return 0;
}

uint32_t hwr_look_sky_generation(void)
{
    return g_sky_generation;
}

int hwr_look_objects_pending(void)
{
    return !g_objects_settled &&
           (g_effective_objects.count != 0u ||
            g_effective_sprites.count != 0u);
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
    }
    /* AND THE SPRITE BLANKET, which is the third table that can name an object
     * and the only one whose key is not a number out of this map's own lists. A
     * blanket is written against the picture rather than against the map, so a
     * MAP'S OWN blanket naming a picture nothing here is drawn with is a line
     * its author believed applied to something in front of them -- and today
     * that mistake looks exactly like a value with no effect. */
    for (i = 0; i < g_effective_sprites.count; i++) {
        if (g_effective_sprites.items[i].id == graphic_id)
            g_sprite_matched[i] = 1u;
    }
}

void hwr_look_object_hidden(uint32_t map_index)
{
    size_t i;

    if (g_objects_settled)
        return;
    for (i = 0; i < g_effective_objects.count; i++) {
        const struct hwr_look_object_entry *entry =
            &g_effective_objects.items[i];

        /* Only a NUMBER can name one: a hidden record is never read, so its
         * ID is not a thing this layer has ever seen. */
        if (!entry->by_id && entry->key == map_index)
            g_object_hidden[i] = 1u;
    }
}

void hwr_look_objects_settled(uint32_t numbered)
{
    size_t i;

    if (g_objects_settled)
        return;
    g_objects_settled = 1;
    for (i = 0; i < g_effective_objects.count; i++) {
        const struct hwr_look_object_entry *entry =
            &g_effective_objects.items[i];

        if (g_object_matched[i])
            continue;
        /* A number nothing answers to is a typed number that will never do
         * anything, and one that says nothing is one nobody finds. Which
         * sentence is true matters: "there is no object 70" sends a person
         * looking for a renumbering that never happened, when the truth was
         * that object 70 is there and hidden. */
        if (!entry->by_id && g_object_hidden[i])
            fprintf(stderr, "[renderer_overhaul] %s: object %lu on %s is hidden "
                            "and never drawn; it cannot be a light\n",
                    g_look_path, (unsigned long)entry->key, g_look_map);
        else if (entry->by_id)
            fprintf(stderr, "[renderer_overhaul] %s: no object with ID %lu on "
                            "%s\n",
                    g_look_path, (unsigned long)entry->key, g_look_map);
        else if (numbered != 0u && entry->key >= numbered)
            fprintf(stderr, "[renderer_overhaul] %s: %s numbers no object %lu; "
                            "its objects are numbered 0 to %lu\n",
                    g_look_path, g_look_map, (unsigned long)entry->key,
                    (unsigned long)(numbered - 1u));
        else
            fprintf(stderr, "[renderer_overhaul] %s: no object %lu on %s\n",
                    g_look_path, (unsigned long)entry->key, g_look_map);
    }
    /* AND THE SPRITE BLANKET WRITTEN UNDER THIS MAP, held to the same standard
     * as a texture written under one and for the same reason: an author who
     * writes a picture number under a map believed that picture was in front of
     * them, so a map that draws nothing with it is worth a sentence. A blanket
     * written game-wide stays quiet, because a game-wide line is written once
     * for a whole game and is MEANT to name nothing on most of its maps.
     *
     * The picture is named back in BOTH the spellings it may have been written
     * in, so that whichever one somebody typed is in the sentence they read. */
    for (i = 0; i < g_effective_sprites.count; i++) {
        const struct hwr_look_light_entry *entry = &g_effective_sprites.items[i];
        unsigned long single;

        if (g_sprite_matched[i] || g_sprite_told[i] || !entry->from_map)
            continue;
        g_sprite_told[i] = 1u;
        single = entry->id < HWR_LOOK_SPRITE_SHARED_LIMIT
               ? (unsigned long)entry->id + HWR_LOOK_SPRITE_MAP_FIRST
               : (unsigned long)entry->id - HWR_LOOK_SPRITE_SHARED_LIMIT;
        fprintf(stderr, "[renderer_overhaul] %s: nothing on %s is drawn with "
                        "the sprite %u/%u, which is picture %lu, so the light "
                        "granted to it lights nothing here\n",
                g_look_path, g_look_map, (unsigned)(entry->id & 0xffu),
                (unsigned)(entry->id >> 8), single);
    }
}

void hwr_look_clear(void)
{
    memset(&g_look, 0, sizeof g_look);
    memset(&g_effective, 0, sizeof g_effective);
    memset(&g_effective_sprites, 0, sizeof g_effective_sprites);
    if ((g_effective_textures.count != 0u || g_effective_faces.count != 0u) &&
        ++g_glow_generation == 0u)
        g_glow_generation = 1u;
    memset(&g_effective_textures, 0, sizeof g_effective_textures);
    memset(&g_effective_faces, 0, sizeof g_effective_faces);
    if (g_effective_placed.count != 0u && ++g_placed_generation == 0u)
        g_placed_generation = 1u;
    memset(&g_effective_placed, 0, sizeof g_effective_placed);
    if (g_effective_env.has_ceilings && ++g_sky_generation == 0u)
        g_sky_generation = 1u;
    memset(&g_effective_env, 0, sizeof g_effective_env);
    memset(&g_effective_objects, 0, sizeof g_effective_objects);
    memset(g_object_matched, 0, sizeof g_object_matched);
    memset(g_object_hidden, 0, sizeof g_object_hidden);
    memset(g_sprite_matched, 0, sizeof g_sprite_matched);
    memset(g_sprite_told, 0, sizeof g_sprite_told);
    g_objects_settled = 0;
    g_look_present = 0;
    g_look_path[0] = 0;
    g_look_dir[0] = 0;
    g_look_map[0] = 0;
    g_look_hash = 0;
    g_look_candidate_hash = 0;
    g_look_candidate_polls = 0;
    g_look_reload_pending = 0;
    g_look_asked_hash = 0;
    g_look_reload_expects_hash = 0;
    g_look_reload_hash = 0;
    g_look_frames_until_poll = 0;
    /* The list back to none alone, and the selection with it. The row's value
     * is a place in that list, so a selection left standing over a list that
     * no longer holds it would be a value the row itself refuses -- which is a
     * settings file that cannot be saved. */
    hwr_look_slots_reset();
    hwr_settings_set_game_config(0);
    g_look_followed = 0;
}
