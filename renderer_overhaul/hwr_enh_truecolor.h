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

#endif
