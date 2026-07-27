#ifndef DEBUG_DRAW_H
#define DEBUG_DRAW_H

#include <stdint.h>

#include "debug_config.h"
#include "debug_metrics.h"
#include "debug_state.h"

int debug_draw_init(void);
void debug_draw_shutdown(void);
void debug_draw_render(const struct debug_config *config,
                       const struct debug_metrics_view *metrics,
                       const struct debug_state_view *state,
                       enum debug_layout_mode mode);
void debug_draw_compose(uint8_t *pixels, uint32_t width, uint32_t height,
                        enum debug_anchor anchor);

#endif
