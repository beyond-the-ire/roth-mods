#ifndef HWR_CONFIG_H
#define HWR_CONFIG_H

#include <stddef.h>
#include <stdint.h>

struct hwr_settings;

/* The one start-up order the game config picker needs, in one place so that
 * nothing that stands in for the renderer can drift from it:
 *
 *   1  the name of the chosen game config is PEEKED out of the settings file,
 *      before anything parses it;
 *   2  the game configs folder is listed, with that name kept a place in the
 *      list even where the folder does not hold it -- otherwise one file
 *      somebody moved would mark the whole settings file invalid and take every
 *      other setting down with it;
 *   3  the settings file is read, and the selection parses because its name is
 *      in the list;
 *   4  and only where there was no settings file AT ALL -- a fresh install --
 *      the base game's own config is chosen, with one line saying so.
 *
 * The chosen file itself is read afterwards, by hwr_look_load. */
void hwr_config_open(const char *plugin_dir);

void hwr_config_load(const char *plugin_dir);
/* Whether the last hwr_config_load found no settings file at all, which is a
 * different thing from finding one with errors in it: the first is a fresh
 * install and the second is a file somebody has edited. Reloads do not answer
 * it -- by the time one happens the question has already been settled. */
int hwr_config_first_run(void);
/* One key's value read straight out of the file, without parsing the rest of
 * it -- the same trick the writer already uses to see which keys the file it is
 * replacing carried. Answers 1 where the key is in the file, INCLUDING where
 * its value is empty, which is a value in its own right. */
int hwr_config_peek(const char *plugin_dir, const char *key, char *out,
                    size_t out_size);
/* Watches the file for a direct edit. The settings overlay's Reload From File
 * is what a person uses, so this is off unless it is asked for. */
void hwr_config_poll(void);

/* Ask for one parse and apply of the configuration file at the next safe
 * point. The request has a single slot, so repeated asks collapse into the one
 * that is still pending. */
void hwr_config_request_reload(void);
/* Carries out a pending request: read, parse, and commit the file as a whole.
 * Called from the render safe point and the game tick, never from a timer or
 * compose callback. */
void hwr_config_drain_request(void);

/* Writes the settings to the configuration file through a temporary file and
 * an atomic replace, so a failed write leaves the previous file untouched.
 * Returns 0 when the file now holds the settings, and otherwise fills err with
 * what stopped it. */
int hwr_config_save(const struct hwr_settings *settings, char *err,
                    size_t err_size);

/* How the last load, reload, or save turned out, in words the settings overlay
 * can put on its status line. The serial advances once per outcome, so a reader
 * can tell a new one from the one it has already shown. */
struct hwr_config_outcome {
    uint32_t serial;
    int error;
    char text[160];
};
void hwr_config_last_outcome(struct hwr_config_outcome *out);

/* Keeps the configuration file as it stands beside itself, with a marker saying
 * a change is waiting to be answered. A session that ends before the answer
 * comes starts on the kept copy, so a display change nobody could see to
 * confirm cannot outlive the session that made it. Returns 0 when the copy is
 * in place, and otherwise fills err. */
int hwr_config_capture_bytes(char *err, size_t err_size);
/* Puts the kept copy back and drops it. Returns 0 when the file is the kept
 * copy again. The caller asks for a reload afterwards, exactly as it would
 * after a save: putting a configuration back is only writing one. */
int hwr_config_restore_bytes(char *err, size_t err_size);
/* Drops the kept copy: the change it stood behind was confirmed. */
void hwr_config_release_bytes(void);
int hwr_config_holding_bytes(void);

#endif
