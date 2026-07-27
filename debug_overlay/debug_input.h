#ifndef DEBUG_INPUT_H
#define DEBUG_INPUT_H

#include <stdint.h>

#include "debug_config.h"

void debug_input_init(const struct debug_config *config);
uint8_t debug_input_scancode(const struct debug_config *config, uint8_t scancode);
enum debug_layout_mode debug_input_mode(void);
uint32_t debug_input_generation(void);

#endif
