#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "roth_sdk.h"

#include "../debug_metrics.h"

static void publish(uint32_t present_serial, uint32_t tick_serial,
                    uint64_t now_ns)
{
    struct roth_present_timing_v1 timing;

    memset(&timing, 0, sizeof(timing));
    timing.struct_size = sizeof(timing);
    timing.present_serial = present_serial;
    timing.tick_serial = tick_serial;
    timing.now_ns_lo = (uint32_t)now_ns;
    timing.now_ns_hi = (uint32_t)(now_ns >> 32);
    debug_metrics_on_present(&timing);
}

int main(void)
{
    struct debug_metrics_view view;
    unsigned i;

    memset(&view, 0, sizeof(view));
    debug_metrics_reset();
    assert(debug_metrics_update(&view, 4, 1) == 1);
    assert(view.rates_valid == 0);

    publish(100, 50, 1000000000ull);
    assert(debug_metrics_update(&view, 4, 0) == 1);
    assert(view.rates_valid == 0);

    for (i = 1; i <= 4; i++) {
        publish(100u + i, 50u + i / 2u,
                1000000000ull + (uint64_t)i * 62500000ull);
        debug_metrics_on_compose();
        debug_metrics_on_game();
    }
    assert(debug_metrics_update(&view, 4, 0) == 1);
    assert(view.rates_valid != 0);
    assert(view.intervals_valid != 0);
    assert(view.present_rate_x10 == 160);
    assert(view.tick_rate_x10 == 80);
    assert(view.compose_rate_x10 == 160);
    assert(view.game_rate_x10 == 160);
    assert(view.average_interval_us == 62500);
    assert(view.minimum_interval_us == 62500);
    assert(view.maximum_interval_us == 62500);
    return 0;
}
