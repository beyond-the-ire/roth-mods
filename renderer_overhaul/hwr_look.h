#ifndef HWR_LOOK_H
#define HWR_LOOK_H

#include "hwr_schema.h"
#include "hwr_settings.h"

/* The look configuration: one global look plus per-map overrides, in a plain
 * text file beside the settings file.
 *
 *   [look]              the global look; every key defaults to what the
 *                       settings registry already holds, so an absent file
 *                       means "no overrides" rather than "defaults"
 *   [profile.<name>]    a reusable named block a map can select
 *   [map.<map name>]    one map's own keys, matched on the name of the map's
 *                       own file -- STUDY1, STUDY2 -- case-insensitively, with
 *                       any directory and any extension stripped from both
 *                       sides. Maps sharing a texture archive are still
 *                       different maps here, which is what makes a look a
 *                       map's own.
 *
 * Precedence, lowest to highest: the registry value, the global look, the
 * profile a map selects, the map's own keys. A map nobody listed gets the
 * global look, silently, so community and custom maps are covered by
 * construction.
 *
 * A key is accepted when the settings table has a row of that name carrying
 * HWR_SF_MAP_OVERRIDE, and its value is parsed by that row. There is no key
 * table here: adding a look key is adding one flag to one row, and the two can
 * never drift apart. A line this file cannot use costs that line and a note,
 * never the file -- it is written by hand, unlike the settings file.
 *
 * And one section of a different kind, because it is keyed by something that is
 * not a settings row:
 *
 *   [lights]            what particular THINGS give off, keyed by the graphic
 *   [profile.X.lights]  the map draws them with. A settings row is one value
 *   [map.X.lights]      for a whole map; this says a brazier is brighter than
 *                       a candle, and that one fullbright decal is not a light
 *                       at all. It follows exactly the same three-section
 *                       precedence chain, field by field.
 */

/* Reads the file beside the settings file, if there is one. */
void hwr_look_load(const char *plugin_dir);
/* Asks for the file to be read again at the next safe point. */
void hwr_look_request_reload(void);
/* The optional development watcher, on the same switch the settings file uses. */
void hwr_look_poll(void);
/* Carries out a pending request. Called from the settings pump, so a reload can
 * never land inside a render. */
void hwr_look_drain_request(void);
/* Chooses the map whose overrides are in force. NULL or an unknown name leaves
 * the global look standing. Raises the change bits of every row whose effective
 * value moved, so the next pump applies them. */
void hwr_look_select_map(const char *map_name);
/* The value a row is worth right now: its override where the current map has
 * one, and the store's own value everywhere else. */
union hwr_value hwr_look_effective(const struct hwr_setting_desc *desc,
                                   const struct hwr_settings *settings);
/* Whether the current map overrides this row, for anything that wants to say
 * so. */
int hwr_look_overrides(const struct hwr_setting_desc *desc);

/* What the look file says about one object graphic's own light.
 *
 * Every field is separately present or absent, because the file writes a
 * dash for one it is saying nothing about and because the three sections
 * compose FIELD BY FIELD: a map may make its torches brighter without
 * restating the colour the global section already gave them. `off` is the
 * whole entry rather than a field: a thing the map marks unshaded that is
 * plainly not a light -- a glowing decal, a bright pickup -- is refused
 * outright, and nothing else in the entry matters. */
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
