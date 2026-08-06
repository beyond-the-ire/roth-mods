#ifndef HWR_ENH_TRUECOLOR_H
#define HWR_ENH_TRUECOLOR_H

#include <stdint.h>

/* The true-colour foundation's derived state.
 *
 * The foundation is not a setting: it is required when a dependent enhancement
 * contributes and off otherwise, so it appears in no menu and nothing turns it
 * on for the player. A row says it is a dependent by naming the foundation's
 * own row in depends_on_key; this module walks the table for those rows on
 * every apply and works out two things from them:
 *
 *   required -- the foundation has to exist at all. Any dependent that has left
 *               its Original value asks for it, and the advanced force switch
 *               (or the environment switch the renderer reads for itself) asks
 *               for it outright.
 *   neutral  -- nothing is moving the picture yet. A feature's own on/off row
 *               enables the foundation without changing a pixel; it is a
 *               dependent VALUE leaving its Original that clears this, and
 *               until it does the picture is the authentic picture, exactly.
 *
 * Registration is checked, and failing it takes the mod back to the original
 * renderer: a hook that never registers is a subsystem that silently never
 * applies, and this one decides what the world is drawn with.
 */

int hwr_enh_truecolor_register(void);
/* Whether the storage the true-colour row asks for can be offered here. Used by
 * the settings table, which asks before showing a row at all. */
int hwr_truecolor_format_offered(void);

/* The settings panel's live preview of the picture dials.
 *
 * A dial is worth seeing while it is being moved, so the panel hands its draft
 * over on every pump and this module draws the world image with the draft's
 * brightness and contrast instead of the ones in force. Nothing is written: the
 * store still holds what is in force, the file is still what Apply writes, and
 * the panel closing -- by Cancel, by throwing the draft away, or by the panel
 * coming down for any other reason -- puts the picture back on its own.
 *
 * Only the picture dials are previewed. Everything else takes effect when it is
 * applied, which is what the panel has always promised.
 *
 * Both calls run on the game thread, where the panel runs. They allocate
 * nothing and touch no GL object: they raise the foundation's change bit, and
 * the settings pump applies it at the render boundary, which is the only place
 * a program is linked or an attachment is created.
 */
struct hwr_settings;
void hwr_truecolor_preview(const struct hwr_settings *draft);
void hwr_truecolor_preview_clear(void);

/* What the map now staged was authored at: how many of its sectors carry a
 * brightness this scale is about, and the tenth, fiftieth and ninety-fifth
 * percentiles of those bytes.
 *
 * The room response places a sector between two anchors, and where those anchors
 * sit is a property of the map rather than of the player -- one pair of numbers
 * cannot serve a cellar and a courtyard. This is how the map's answer arrives.
 * A count of zero says there is no map to read, and the written anchors stand
 * alone.
 *
 * Called where a map is staged, on the game thread. It allocates nothing and
 * touches no GL object: it raises the foundation's change bit and the settings
 * pump works the rig out at the render boundary. Passing what is already held
 * does nothing at all, so a reload of the same map is free. */
void hwr_truecolor_set_map_lighting(uint32_t counted, uint8_t low,
                                    uint8_t middle, uint8_t high);

/* And what the map now staged says about the light a person carries through it:
 * the shade level its candle-lit surfaces are drawn at.
 *
 * That level is a distance -- the world units such a room spends on one shade
 * row -- so it is what the reach of a carried light is derived from wherever
 * nobody has written one. Called where a map is staged, on the game thread,
 * beside the call above and under the same rules: it allocates nothing,
 * touches no GL object, and passing what is already held does nothing at all. */
void hwr_truecolor_set_map_candle(uint16_t tint_shade_level);

/* The reach that map authored for its own candlelight, in world units, which is
 * what a carried light with no reach written for it is given. It is out here
 * rather than only inside the rig because the settings screen says what a row is
 * actually running at, and a row left at nothing is exactly such a row. */
float hwr_truecolor_candle_reach(void);

#endif
