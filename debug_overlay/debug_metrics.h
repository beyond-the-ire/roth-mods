#ifndef DEBUG_METRICS_H
#define DEBUG_METRICS_H

#include <stdint.h>

#include "roth_sdk.h"

struct debug_metrics_view {
    int rates_valid;
    int intervals_valid;
    uint32_t present_rate_x10;
    uint32_t tick_rate_x10;
    uint32_t compose_rate_x10;
    uint32_t game_rate_x10;
    uint32_t average_interval_us;
    uint32_t minimum_interval_us;
    uint32_t maximum_interval_us;
};

void debug_metrics_reset(void);
void debug_metrics_on_present(const struct roth_present_timing_v1 *timing);
void debug_metrics_on_compose(void);
void debug_metrics_on_game(void);
int debug_metrics_update(struct debug_metrics_view *view,
                         uint32_t update_hz,
                         int force);

#endif
