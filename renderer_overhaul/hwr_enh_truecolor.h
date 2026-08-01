#ifndef HWR_ENH_TRUECOLOR_H
#define HWR_ENH_TRUECOLOR_H

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

#endif
