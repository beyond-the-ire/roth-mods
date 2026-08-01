#ifndef HWR_CONFIG_H
#define HWR_CONFIG_H

struct hwr_settings;

void hwr_config_load(const char *plugin_dir);
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
 * Returns 0 when the file now holds the settings. */
int hwr_config_save(const struct hwr_settings *settings);

#endif
