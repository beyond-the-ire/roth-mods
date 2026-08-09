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
 *   "lights"    five tables, and every number in every one of them is DECIMAL
 *               and is the number the community ROTH Editor prints for the
 *               same thing:
 *
 *               "sprites"  a BLANKET over every object drawn with one sprite,
 *                          keyed by either of the two numbers the editor shows
 *                          for one picture: the texture and source pair of its
 *                          object panel, "34/0", or the single picture number
 *                          its picking window shows, "4130". They are the same
 *                          key and the same picture.
 *
 *               "textures" a BLANKET over every wall band and every floor and
 *                          ceiling painted with one texture, keyed by that
 *                          texture's own number: lava, a glowing rune, a lit
 *                          sigil. Written game-wide as readily as under a map,
 *                          because a texture is the same texture wherever it
 *                          is painted.
 *
 *               "objects"  ONE object of one map, or one GROUP of them, by
 *                          the `Object: N` number or the `ID:` the editor
 *                          shows beside it.
 *
 *               "faces"    ONE face of one map, or one group of them, by the
 *                          `Face: N` number or the `Face ID`.
 *
 *               "placed"   a light tied to NO object at all: three numbers
 *                          somebody chose, in the GAME'S own coordinates --
 *                          the ones HWR_OBJ_DUMP prints.
 *
 *               The last three are written under a map and never under
 *               `everywhere`: a number in one map's own list, and three
 *               numbers inside one building, name nothing anywhere else.
 *
 *               A GRANT CREATES A LIGHT. A thing the map itself marks unshaded
 *               is a light already and a grant tunes it; a thing the map does
 *               NOT mark is made one by any grant naming it, and is DRAWN as a
 *               light too rather than left a dark sprite with a halo round it.
 *
 *               THE COLOUR IS DERIVED WHERE IT IS NOT WRITTEN. A thing is
 *               drawn with a picture and a surface is painted with one, so
 *               either can be read: a sprite by the law that already colours
 *               the lights a map marks, a texture by the brightest part of its
 *               own picture. Only `placed` has nothing to read, and only
 *               `placed` therefore requires the colour to be written.
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
 * publishing from inside one would invert that order.
 *
 * THE FILE IS READ FROM DISK EVERY TIME the selection moves, including a move
 * away from a file and straight back to it. The model in memory is what was
 * read last time, and a person who has just edited a file and reached for the
 * picker is asking for what is on disk now.
 *
 * A move away and back again inside ONE draft nets to no change in the row at
 * all, and that case is answered too: wherever the settings store has been
 * written -- applied, or reloaded from its own file -- and the row came out
 * where it went in, the chosen file is read again anyway. That is one read of
 * a small file when somebody presses a button, against an edit that would
 * otherwise silently do nothing. */
void hwr_look_follow_selection(void);
/* Asks for the folder to be listed again and the chosen file read again at the
 * next safe point. */
void hwr_look_request_reload(void);
/* THE FILE WATCHER, which always runs: saving the chosen game config file puts
 * it in force within about a second, with no button pressed and no map change.
 * Called once a frame; it reads the file on every thirtieth call, and a changed
 * file must be seen TWICE with the same content before it is asked for, so an
 * editor's truncate-and-rewrite intermediate never loads. All it does is ASK --
 * the reading happens at the settings pump's own safe point, where a file that
 * turns out to be broken keeps what was in force and says so once. */
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

/* What the game config says about one light.
 *
 * Every field is separately present or absent, because a field the file says
 * nothing about is simply left out and because the two scopes compose FIELD BY
 * FIELD: a map may make its torches brighter without restating the colour the
 * game-wide answer already gave them. `off` is the whole entry rather than a
 * field: a thing that is plainly not a light -- a glowing decal, a bright
 * pickup -- is refused outright, and nothing else in the entry is read. */
struct hwr_look_light {
    int off;
    int has_intensity;
    float intensity;
    int has_range;
    float range;
    int has_color;
    uint32_t color;
};

/* One grant laid over another, FIELD by field, with `off` left to the caller
 * because `off` belongs to the most specific table naming a thing rather than
 * to the composition. Written once so that no two families can grow a
 * composition rule of their own. */
void hwr_look_light_over(struct hwr_look_light *into,
                         const struct hwr_look_light *from);

/* THE SPRITE BLANKET: what every object drawn with one sprite gives off,
 * keyed here by the sixteen-bit graphic the map's own record carries, which is
 * the editor's texture index in its low byte and its texture source in its
 * high byte. The file writes the pair; this takes the number the record does.
 *
 * A sprite nobody named leaves `out` cleared, which is every field absent --
 * the settings rows answer for it whole. */
int hwr_look_light_for_sprite(uint16_t graphic_id, struct hwr_look_light *out);

/* EVERYTHING THE FILE SAYS ABOUT ONE OBJECT OF THE MAP, the three tables that
 * can name it composed least specific first:
 *
 *   its SPRITE -- every object of that kind, everywhere;
 *   its ID -- the group a creator gathered by hand, which the editor shows as
 *   `ID:` and which is deliberately shared;
 *   its OBJECT NUMBER -- the `Object: N` of the editor, which names exactly one
 *   thing and is therefore the last word.
 *
 * Composed FIELD by field, so a hearth may be made stronger than the group it
 * shares an ID with without restating the colour its kind already carries; `off`
 * is taken whole from the MOST SPECIFIC table naming it, so one object of a
 * refused kind may be made a light again.
 *
 * Returns 0 where nothing names it at all, leaving `out` cleared. */
int hwr_look_object_grant(uint32_t map_index, uint16_t object_id,
                          uint16_t graphic_id, struct hwr_look_light *out);

/* THE TEXTURE BLANKET, keyed by the sixteen-bit texture number the map's own
 * geometry carries -- in a sector's ceiling and floor, in a wall's three bands
 * and in a slab's two. Every face and every flat painted with it emits.
 *
 * What a grant here does is SEED A LIGHT and nothing else: whether the surface
 * itself is drawn fullbright stays the map's own glow flag and the engine's own
 * glow colours, so a grant adds a light to a room and never a second way of
 * painting the thing that gives it.
 *
 * A grant that states no colour takes one off the texture's own texels -- its
 * brightest ones, so lava is lit by its glow rather than by the rock the glow
 * is set in. */
int hwr_look_light_for_texture(uint16_t texture_id, struct hwr_look_light *out);

/* The granted textures as a list, for the one caller that wants all of them
 * rather than one: the seeding walks a whole map once looking for the faces and
 * flats that carry them, and asking that walk to name every texture in the game
 * would be the wrong way round.
 *
 * `map_scoped` is whether the MAP's own section said any of this, which is what
 * decides whether a texture no face on this map carries is worth a word: a
 * game-wide entry is written once for every map in the game and is silent on
 * the ones that do not use it, and an entry written under a map names a texture
 * its author believed was there. */
size_t hwr_look_texture_count(void);
int hwr_look_texture_at(size_t index, uint16_t *texture_id, int *map_scoped,
                        struct hwr_look_light *out);

/* THE NAMED FACES of one map: the editor's `Face: N`, which is the position of
 * the face record in the map's own flat face array, and the editor's read-only
 * `Face ID`, which is the one a mapping record carries and which several faces
 * may share -- exactly the pair the objects table offers, for exactly the same
 * reasons.
 *
 * Composed the same way and in the same order: whatever the texture blanket
 * says about the texture that face is painted with, then the ID, then the
 * number. `off` comes whole from the most specific of the two named here, and
 * the caller lays the blanket under what this returns -- and where none of them
 * states a colour, the face's own texture is read for one.
 *
 * Returns 0 where neither names it, leaving `out` cleared. */
size_t hwr_look_face_count(void);
int hwr_look_face_at(size_t index, int *by_id, uint32_t *key,
                     struct hwr_look_light *out);
int hwr_look_face_grant(uint32_t face_index, uint16_t face_tag,
                        struct hwr_look_light *out);

/* A number that moves when anything a map's own geometry is lit FROM does --
 * the granted textures, the named faces -- and at no other time.
 *
 * Which faces of a map carry a granted texture, and which of them somebody
 * named, are facts about the MAP, worked out once when it is staged rather than
 * once a frame. This is what lets the one thing that could invalidate that
 * answer without a map load -- somebody editing the file and asking for it to
 * be read again -- be noticed with one integer compare instead of a walk. It is
 * never zero, so zero is free to mean "nothing has been worked out yet". */
uint32_t hwr_look_glow_generation(void);

/* And the FREE-STANDING lights: the ones tied to no object at all, hung at three
 * numbers somebody chose because the map has nothing standing there to tune.
 *
 * A list rather than a lookup, because nothing in the world asks for one -- the
 * seeding walks them exactly as it walks the granted textures, and for the same
 * reason: which room each hangs in is a fact about the map, worked out once when
 * it is staged.
 *
 * The three numbers are THE GAME'S OWN, X included: the same space the map file
 * is written in and the same space HWR_OBJ_DUMP prints, with nothing flipped or
 * reinterpreted on the way in. The community editor DISPLAYS X with the
 * opposite sign, so a number copied off its position box has to be flipped
 * before it is written -- but which of the two a number came from is not
 * something this layer can know, so it is not something it guesses at.
 *
 * The colour is REQUIRED here and nowhere else, and this is the one table where
 * there is genuinely nothing to derive it from: a torch has a sprite a colour
 * can be read off and lava has a texture, while a light hanging in the air has
 * no picture of any kind, so a default here would be the mod choosing a colour
 * and calling it the author's. An entry with none lights nothing and says so. */
size_t hwr_look_placed_count(void);
int hwr_look_placed_at(size_t index, int16_t *world_x, int16_t *world_y,
                       int16_t *world_z, struct hwr_look_light *out);
/* Moves when the free-standing lights do and at no other time, so which room
 * each hangs in is worked out once per map rather than once a frame. Never
 * nought, so nought is free to mean nothing has been worked out yet. */
uint32_t hwr_look_placed_generation(void);

/* Whether the map's own objects are still to be offered to the object table.
 * Answers 0 once they have been -- and 0 on every map that has nothing to check,
 * which is what keeps this off the frame's own cost. */
int hwr_look_objects_pending(void);
/* One of the map's own objects: its number, its ID, and the sprite it is drawn
 * with. EVERY object is offered, not only the ones that become lights: an object
 * refused as not a light is still an object standing there, and an entry
 * addressing one must not be reported as addressing nothing.
 *
 * All three tables that can name an object are answered by this: the two keyed
 * by the map's own numbers, and the sprite blanket, which is keyed by the
 * picture the object is drawn with and is otherwise the one table nothing could
 * ever say was missed. */
void hwr_look_object_seen(uint32_t map_index, uint16_t object_id,
                          uint16_t graphic_id);
/* One of the map's own records that is numbered and never drawn. Offered
 * separately because "this map has no object 70" and "object 70 is hidden" are
 * different sentences and a person who typed 70 needs the true one. */
void hwr_look_object_hidden(uint32_t map_index);
/* The map holds nothing else, and it numbers `numbered` records in all. Every
 * entry nothing answered to says so, once per entry per map, so a mistyped
 * number is visible rather than silent -- and a number past the end of the
 * map's own list is told that it is past the end rather than merely missed. A
 * sprite blanket written UNDER THIS MAP that nothing here is drawn with says so
 * too; one written game-wide stays quiet, because a game-wide line is written
 * once for a whole game and is meant to name nothing on most of its maps. */
void hwr_look_objects_settled(uint32_t numbered);

void hwr_look_clear(void);

#endif
