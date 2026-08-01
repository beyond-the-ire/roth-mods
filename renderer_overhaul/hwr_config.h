#ifndef HWR_CONFIG_H
#define HWR_CONFIG_H

#include <stddef.h>
#include <stdint.h>

struct hwr_settings;

void hwr_config_load(const char *plugin_dir);
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
