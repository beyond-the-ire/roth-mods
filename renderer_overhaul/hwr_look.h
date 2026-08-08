#ifndef HWR_LOOK_H
#define HWR_LOOK_H

#include "hwr_schema.h"
#include "hwr_settings.h"

/* The game config: what a particular game's places are lit like, described in
 * a JSON file inside the mod's own `game_configs` folder.
 *
 * The folder holds as many of them as anybody likes -- one per game, or two
 * for one game and a choice between them -- and which is in force is a row on
 * the settings screen whose members are the files themselves. What the settings
 * file records is the file's NAME, so files may be added or taken away without
 * renumbering anybody's selection, and the empty name is a real value meaning
 * none.
 *
 *   {
 *     "format": 1,
 *     "for": "Realms of the Haunting",
 *     "notes": "How this game's places are lit.",
 *     "everywhere": { ...one place... },
 *     "maps": { "STUDY1": { ...one place... } }
 *   }
 *
 * `everywhere` and each entry under `maps` have the SAME shape, and that is
 * the whole of the composition rule: a map's answer is laid over the game's
 * answer, field by field. A map nobody listed gets `everywhere` alone,
 * silently, so community and custom maps are covered by construction. An
 * absent file means "no overrides" rather than "defaults" -- every dial falls
 * to the settings screen on every map.
 *
 * A map is matched on the name of the map's own file -- STUDY1, STUDY2 --
 * case-insensitively, with any directory and any extension stripped from both
 * sides. Maps sharing a texture archive are still different maps here, which
 * is what makes a description a map's own.
 *
 * One place says two kinds of thing:
 *
 *   "settings"  the dials, spelled exactly as the settings file spells them. A
 *               key is accepted when the settings table has a row of that name
 *               that says a map may override it, and its value is read by that
 *               row. There is no key table here: adding one is adding one flag
 *               to one row, and the two can never drift apart.
 *
 *   "lights"    what particular THINGS give off, under "graphics", keyed by
 *               the graphic the map draws them with. A settings row is one
 *               value for a whole map; this says a brazier is brighter than a
 *               candle, and that one fullbright decal is not a light at all.
 *               Each field is separately present or absent, so a map may raise
 *               a strength without restating a colour.
 *
 *               And under "objects", ONE PLACED OBJECT of the map rather than a
 *               kind of them -- or a group of them -- addressed the way the
 *               community ROTH Editor addresses it: by the object number it
 *               shows as "Object: N", or by the tag it shows as "ID:". That is
 *               the most specific statement anybody can make about a light, so
 *               it is the last word: the hearth in one room burns hotter than
 *               every other brazier of its kind. Objects are described under a
 *               map and never under "everywhere" -- an object's number is a
 *               number in ONE map's own list.
 *
 *               And under "placed", a light tied to NO object: three numbers
 *               somebody chose, because the map has nothing standing there to
 *               tune. It states its own colour, because there is nothing to
 *               read one off. Under a map only, for the same reason as above.
 *
 *               And under "surfaces", what a PAINTED SURFACE gives off, keyed
 *               by the texture the map's own geometry carries: lava, a glowing
 *               rune, a lit sigil painted onto a wall. A grant there seeds a
 *               light; it never changes how the surface itself is drawn, which
 *               stays the map's own word and the engine's own colours.
 *
 * A member named exactly `notes`, at any level, is documentation -- a piece of
 * text or a list of them -- and is never read as data.
 *
 * What a bad file costs, in two answers, because they differ: a file that is
 * not JSON at all costs the WHOLE file, reported once, with whatever was in
 * force left standing -- one missing brace moves every following line into a
 * different place, so half a file read would look like an answer. Anything
 * else -- an unknown key, a value out of range, a colour that is not six
 * hexadecimal digits -- costs its own key and a note, and the rest of the file
 * is read.
 */

/* Lists the game configs folder and fills the settings table's `game_config`
 * row from what is in it: none first, then one entry per file, sorted by name
 * so the list reads the same on every machine and every run.
 *
 * `keep` is the name the settings file carries, peeked out of it BEFORE it is
 * parsed -- which is the whole reason this runs first. A name the folder does
 * not answer for keeps a place of its own in the list, so that the settings
 * file still parses whole (one file somebody moved must never cost every other
 * setting) and so that putting the file back puts the selection back. NULL
 * keeps whatever is chosen now, which is what a re-listing wants.
 *
 * A name is kept across a listing, never a place: files may be added or taken
 * away without renumbering anybody's selection. */
void hwr_look_scan(const char *plugin_dir, const char *keep);
/* The list as it stands, for anything that has to say what is in it. */
size_t hwr_look_config_count(void);
const char *hwr_look_config_token(size_t slot);
const char *hwr_look_config_label(size_t slot);
int hwr_look_config_is_missing(size_t slot);
/* Chooses one by name, case-insensitively. Answers 0 where the list holds no
 * such name and leaves the selection alone. */
int hwr_look_select_config(const char *token);
/* Chooses the base game's own config, for the one case that calls for it: a
 * fresh install with no settings file at all. It is not the row's default --
 * a default naming a file somebody deleted would be a value with no place in
 * the list, which would read back as its own opposite. */
void hwr_look_select_shipped(void);

/* Reads whatever the picker names, if it names anything. */
void hwr_look_load(const char *plugin_dir);
/* Notices the picker moving in the settings pump, which is where the layer
 * asks for the newly chosen file. Deliberately not an apply hook: the look's
 * overrides are in force before the hooks that consume them run, and
 * publishing from inside one would invert that order. */
void hwr_look_follow_selection(void);
/* Asks for the folder to be listed again and the chosen file read again at the
 * next safe point. */
void hwr_look_request_reload(void);
/* The optional development watcher, on the same switch the settings file uses. */
void hwr_look_poll(void);
/* Carries out a pending request. Called from the settings pump, so a reload can
 * never land inside a render. */
void hwr_look_drain_request(void);
/* Chooses the map whose overrides are in force. NULL or an unknown name leaves
 * the game-wide answer standing. Raises the change bits of every row whose
 * effective value moved, so the next pump applies them. */
void hwr_look_select_map(const char *map_name);
/* The value a row is worth right now: its override where the current map has
 * one, and the store's own value everywhere else. */
union hwr_value hwr_look_effective(const struct hwr_setting_desc *desc,
                                   const struct hwr_settings *settings);
/* Whether the current map overrides this row, for anything that wants to say
 * so. */
int hwr_look_overrides(const struct hwr_setting_desc *desc);
/* How many rows the map in force answers for. Zero where it answers for none,
 * which is also the answer where no game config is in force at all. */
int hwr_look_override_count(void);
/* The map whose answers are in force, by the name it is matched on, or NULL
 * where no map has been chosen. The settings screen names it rather than
 * saying "this map": a warning about a place nobody can identify reads as a
 * fault in the mod. */
const char *hwr_look_current_map(void);

/* What the game config says about one object graphic's own light.
 *
 * Every field is separately present or absent, because a field the file says
 * nothing about is simply left out and because the two scopes compose FIELD BY
 * FIELD: a map may make its torches brighter without restating the colour the
 * game-wide answer already gave them. `off` is the whole entry rather than a
 * field: a thing the map marks unshaded that is plainly not a light -- a
 * glowing decal, a bright pickup -- is refused outright, and nothing else in
 * the entry is read. */
struct hwr_look_light {
    int off;
    int has_intensity;
    float intensity;
    int has_range;
    float range;
    int has_color;
    uint32_t color;
};

/* Fills `out` from the entry in force for this graphic and returns nonzero
 * when there is one. A graphic nobody named leaves `out` cleared, which is
 * every field absent -- the settings rows answer for it whole. */
int hwr_look_light_for(uint16_t graphic_id, struct hwr_look_light *out);

/* The same for ONE PLACED OBJECT of the map, addressed the way the community
 * ROTH Editor addresses it -- because a person tuning a map has that editor open
 * beside the file, and a file written against numbers nobody can look up is a
 * file nobody can write.
 *
 * Two spellings, in the order they rank:
 *
 *   ITS OBJECT NUMBER -- the map-wide index over the per-sector object lists
 *   flattened in sector order, which the editor shows as `Object: N`. It names
 *   exactly one thing, so it is the most specific statement anybody can make and
 *   it is the last word.
 *
 *   ITS ID TAG -- the record's own +0x0e word, which the editor shows as `ID:`.
 *   A creator types it to gather objects into a group for the command system, so
 *   it is deliberately SHARED: an entry keyed by it answers for EVERY object
 *   carrying it, which is what lets the torches of one corridor be tuned in one
 *   line. Nought is not an address -- it is the untagged default -- and is
 *   refused where the file is read.
 *
 * A number nobody named leaves `out` cleared. Whether the entry AGREES with what
 * is there is not asked here: the graphic written beside a number is a check on
 * the entry rather than part of its key, and the checking is done once against
 * the map's own objects rather than once per frame per light. */
int hwr_look_light_for_object(uint32_t map_index, struct hwr_look_light *out);
int hwr_look_light_for_object_id(uint16_t object_id,
                                 struct hwr_look_light *out);

/* And the FREE-STANDING lights: the ones tied to no object at all, hung at three
 * numbers somebody chose because the map has nothing standing there to tune.
 *
 * A list rather than a lookup, because nothing in the world asks for one -- the
 * seeding walks them exactly as it walks the granted surfaces, and for the same
 * reason: which room each hangs in is a fact about the map, worked out once when
 * it is staged.
 *
 * The colour is REQUIRED, for the same reason a granted surface's is: there is
 * nothing to derive one from. A torch has a sprite a colour can be read off; a
 * light hanging in the air has no sprite, no object and no map mark, so a
 * default here would be the mod choosing a colour and calling it the author's.
 * An entry with none lights nothing and says so. */
size_t hwr_look_placed_count(void);
int hwr_look_placed_at(size_t index, int16_t *world_x, int16_t *world_y,
                       int16_t *world_z, struct hwr_look_light *out);
/* Moves when the free-standing lights do and at no other time, so which room
 * each hangs in is worked out once per map rather than once a frame. Never
 * nought, so nought is free to mean nothing has been worked out yet. */
uint32_t hwr_look_placed_generation(void);

/* And the same for one SURFACE, keyed by the sixteen-bit logical texture id the
 * map's own geometry carries for a face -- the id in a sector's ceiling and
 * floor, in a wall's three bands and in a slab's two.
 *
 * A texture nobody named leaves `out` cleared. What a grant here does is SEED A
 * LIGHT and nothing else: whether the face itself is drawn fullbright stays the
 * map's own glow flag and the engine's own glow colours, so a grant adds a
 * light to a room and never a second way of painting the thing that gives it. */
int hwr_look_light_for_surface(uint16_t texture_id, struct hwr_look_light *out);

/* The granted surfaces as a list, for the one caller that wants all of them
 * rather than one: the seeding walks a whole map once looking for the faces
 * that carry them, and asking that walk to name every texture in the game would
 * be the wrong way round. */
size_t hwr_look_surface_count(void);
int hwr_look_surface_at(size_t index, uint16_t *texture_id,
                        struct hwr_look_light *out);
/* A number that moves when the granted surfaces do and at no other time.
 *
 * Which faces of a map carry a granted texture is a fact about the MAP, worked
 * out once when it is staged rather than once a frame. This is what lets the
 * one thing that could invalidate that answer without a map load -- somebody
 * editing the file and asking for it to be read again -- be noticed with one
 * integer compare instead of a walk. It is never zero, so zero is free to mean
 * "nothing has been worked out yet". */
uint32_t hwr_look_surface_generation(void);

/* Whether the map's own objects are still to be offered to the object table.
 * Answers 0 once they have been -- and 0 on every map that has no entries to
 * check, which is what keeps this off the frame's own cost. */
int hwr_look_objects_pending(void);
/* One of the map's own objects, by its number, its id tag and the graphic it is
 * drawn with. EVERY object is offered, not only the ones that become lights: an
 * object refused as not a light is still an object standing there, and an entry
 * addressing one must not be reported as addressing nothing.
 *
 * This is where an entry keyed by an object NUMBER is checked against the
 * graphic written beside it, and says so where the two disagree -- the entry
 * being applied anyway. An entry keyed by an id TAG is checked when the walk
 * settles instead, because the question for a group is whether ANY of its
 * members is drawn with that graphic, and no single object answers that. */
void hwr_look_object_seen(uint32_t map_index, uint16_t object_id,
                          uint16_t graphic_id);
/* The map holds nothing else. Every entry nothing answered to says so, once per
 * entry per map, so a mistyped number is visible rather than silent -- and every
 * id tag whose whole group is drawn with something else says so too. */
void hwr_look_objects_settled(void);

void hwr_look_clear(void);

#endif
