#ifndef HWR_LOOK_H
#define HWR_LOOK_H

#include "hwr_schema.h"
#include "hwr_settings.h"

/* The game config: what a particular game's places are lit like, described in
 * a JSON file inside the mod's own `game_configs` folder.
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

/* Reads the game config, if there is one. */
void hwr_look_load(const char *plugin_dir);
/* Asks for the file to be read again at the next safe point. */
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

void hwr_look_clear(void);

#endif
