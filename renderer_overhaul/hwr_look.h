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
 *   [map.<DAS name>]    one map's own keys, matched on the archive basename
 *                       case-insensitively with the extension stripped
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
void hwr_look_select_map(const char *archive_basename);
/* The value a row is worth right now: its override where the current map has
 * one, and the store's own value everywhere else. */
union hwr_value hwr_look_effective(const struct hwr_setting_desc *desc,
                                   const struct hwr_settings *settings);
/* Whether the current map overrides this row, for anything that wants to say
 * so. */
int hwr_look_overrides(const struct hwr_setting_desc *desc);
void hwr_look_clear(void);

#endif
