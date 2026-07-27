#include "debug_metrics.h"

#include <limits.h>
#include <string.h>

struct debug_timing_mailbox {
    uint32_t sequence;
    uint32_t tick_serial;
    uint32_t present_serial;
    uint32_t now_ns_lo;
    uint32_t now_ns_hi;
};

static struct debug_timing_mailbox debug_mailbox;
static uint32_t debug_compose_count;
static uint32_t debug_game_count;
static uint32_t debug_interval_min_ns;
static uint32_t debug_interval_max_ns;
static uint64_t debug_present_last_ns;

static uint32_t debug_base_present;
static uint32_t debug_base_tick;
static uint32_t debug_base_compose;
static uint32_t debug_base_game;
static uint64_t debug_base_ns;
static int debug_have_base;
static int debug_initial_rendered;

static uint64_t debug_join_u64(uint32_t low, uint32_t high)
{
    return ((uint64_t)high << 32) | low;
}

static uint32_t debug_rate_x10(uint32_t delta, uint64_t elapsed_ns)
{
    uint64_t value;

    if (elapsed_ns == 0)
        return 0;
    value = ((uint64_t)delta * 10000000000ull + elapsed_ns / 2) / elapsed_ns;
    if (value > UINT32_MAX)
        return UINT32_MAX;
    return (uint32_t)value;
}

static void debug_atomic_min_u32(uint32_t *target, uint32_t value)
{
    uint32_t current = __atomic_load_n(target, __ATOMIC_RELAXED);

    while (value < current &&
           !__atomic_compare_exchange_n(target, &current, value, 1,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}

static void debug_atomic_max_u32(uint32_t *target, uint32_t value)
{
    uint32_t current = __atomic_load_n(target, __ATOMIC_RELAXED);

    while (value > current &&
           !__atomic_compare_exchange_n(target, &current, value, 1,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}

void debug_metrics_reset(void)
{
    memset(&debug_mailbox, 0, sizeof(debug_mailbox));
    __atomic_store_n(&debug_compose_count, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&debug_game_count, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&debug_interval_min_ns, UINT32_MAX, __ATOMIC_RELAXED);
    __atomic_store_n(&debug_interval_max_ns, 0, __ATOMIC_RELAXED);
    debug_present_last_ns = 0;
    debug_base_present = 0;
    debug_base_tick = 0;
    debug_base_compose = 0;
    debug_base_game = 0;
    debug_base_ns = 0;
    debug_have_base = 0;
    debug_initial_rendered = 0;
}

void debug_metrics_on_present(const struct roth_present_timing_v1 *timing)
{
    uint64_t now_ns;
    uint64_t interval;

    if (timing == NULL ||
        timing->struct_size < offsetof(struct roth_present_timing_v1, now_ns_hi) +
                              sizeof(timing->now_ns_hi))
        return;

    now_ns = debug_join_u64(timing->now_ns_lo, timing->now_ns_hi);
    if (debug_present_last_ns != 0 && now_ns >= debug_present_last_ns) {
        interval = now_ns - debug_present_last_ns;
        if (interval > UINT32_MAX)
            interval = UINT32_MAX;
        debug_atomic_min_u32(&debug_interval_min_ns, (uint32_t)interval);
        debug_atomic_max_u32(&debug_interval_max_ns, (uint32_t)interval);
    }
    debug_present_last_ns = now_ns;

    __atomic_fetch_add(&debug_mailbox.sequence, 1, __ATOMIC_ACQ_REL);
    debug_mailbox.tick_serial = timing->tick_serial;
    debug_mailbox.present_serial = timing->present_serial;
    debug_mailbox.now_ns_lo = timing->now_ns_lo;
    debug_mailbox.now_ns_hi = timing->now_ns_hi;
    __atomic_fetch_add(&debug_mailbox.sequence, 1, __ATOMIC_RELEASE);
}

void debug_metrics_on_compose(void)
{
    __atomic_fetch_add(&debug_compose_count, 1, __ATOMIC_RELAXED);
}

void debug_metrics_on_game(void)
{
    __atomic_fetch_add(&debug_game_count, 1, __ATOMIC_RELAXED);
}

static int debug_read_mailbox(struct debug_timing_mailbox *snapshot)
{
    unsigned attempt;

    for (attempt = 0; attempt < 8; attempt++) {
        uint32_t before = __atomic_load_n(&debug_mailbox.sequence, __ATOMIC_ACQUIRE);
        uint32_t after;

        if ((before & 1u) != 0)
            continue;
        snapshot->tick_serial = debug_mailbox.tick_serial;
        snapshot->present_serial = debug_mailbox.present_serial;
        snapshot->now_ns_lo = debug_mailbox.now_ns_lo;
        snapshot->now_ns_hi = debug_mailbox.now_ns_hi;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        after = __atomic_load_n(&debug_mailbox.sequence, __ATOMIC_ACQUIRE);
        if (before == after && (after & 1u) == 0 && after != 0)
            return 0;
    }
    return -1;
}

int debug_metrics_update(struct debug_metrics_view *view,
                         uint32_t update_hz,
                         int force)
{
    struct debug_timing_mailbox timing;
    uint64_t now_ns;
    uint64_t elapsed_ns;
    uint64_t refresh_ns;
    uint32_t compose;
    uint32_t game;
    uint32_t min_ns;
    uint32_t max_ns;

    if (debug_read_mailbox(&timing) != 0) {
        if (!debug_initial_rendered || force) {
            memset(view, 0, sizeof(*view));
            debug_initial_rendered = 1;
            return 1;
        }
        return 0;
    }

    now_ns = debug_join_u64(timing.now_ns_lo, timing.now_ns_hi);
    compose = __atomic_load_n(&debug_compose_count, __ATOMIC_RELAXED);
    game = __atomic_load_n(&debug_game_count, __ATOMIC_RELAXED);

    if (update_hz < 1)
        update_hz = 1;
    if (update_hz > 20)
        update_hz = 20;
    refresh_ns = 1000000000ull / update_hz;

    if (!debug_have_base) {
        debug_base_present = timing.present_serial;
        debug_base_tick = timing.tick_serial;
        debug_base_compose = compose;
        debug_base_game = game;
        debug_base_ns = now_ns;
        debug_have_base = 1;
        debug_initial_rendered = 1;
        memset(view, 0, sizeof(*view));
        return 1;
    }

    elapsed_ns = now_ns - debug_base_ns;
    if (force && elapsed_ns < refresh_ns)
        return 1;
    if (!force && elapsed_ns < refresh_ns)
        return 0;
    if (elapsed_ns == 0)
        return force ? 1 : 0;

    view->present_rate_x10 =
        debug_rate_x10(timing.present_serial - debug_base_present, elapsed_ns);
    view->tick_rate_x10 =
        debug_rate_x10(timing.tick_serial - debug_base_tick, elapsed_ns);
    view->compose_rate_x10 =
        debug_rate_x10(compose - debug_base_compose, elapsed_ns);
    view->game_rate_x10 =
        debug_rate_x10(game - debug_base_game, elapsed_ns);
    if (timing.present_serial != debug_base_present) {
        uint64_t average =
            elapsed_ns / (uint32_t)(timing.present_serial - debug_base_present);
        view->average_interval_us = (uint32_t)((average + 500) / 1000);
    } else {
        view->average_interval_us = 0;
    }
    view->rates_valid = 1;

    min_ns = __atomic_exchange_n(&debug_interval_min_ns, UINT32_MAX,
                                 __ATOMIC_ACQ_REL);
    max_ns = __atomic_exchange_n(&debug_interval_max_ns, 0, __ATOMIC_ACQ_REL);
    if (min_ns != UINT32_MAX && max_ns != 0) {
        view->minimum_interval_us =
            (uint32_t)(((uint64_t)min_ns + 500u) / 1000u);
        view->maximum_interval_us =
            (uint32_t)(((uint64_t)max_ns + 500u) / 1000u);
        view->intervals_valid = 1;
    } else {
        view->intervals_valid = 0;
    }

    debug_base_present = timing.present_serial;
    debug_base_tick = timing.tick_serial;
    debug_base_compose = compose;
    debug_base_game = game;
    debug_base_ns = now_ns;
    return 1;
}
