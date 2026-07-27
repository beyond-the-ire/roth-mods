#include "debug_input.h"

static uint32_t debug_mode;
static uint32_t debug_generation;

void debug_input_init(const struct debug_config *config)
{
    __atomic_store_n(&debug_mode, (uint32_t)config->initial_mode,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&debug_generation, 1, __ATOMIC_RELEASE);
}

uint8_t debug_input_scancode(const struct debug_config *config, uint8_t scancode)
{
    uint8_t make;

    if (!config->enabled || (scancode & 0x80u) != 0)
        return scancode;
    make = scancode & 0x7fu;
    if (make == config->mode_key) {
        uint32_t current = __atomic_load_n(&debug_mode, __ATOMIC_RELAXED);
        uint32_t next = current == DEBUG_LAYOUT_ALL
                      ? DEBUG_LAYOUT_OFF : current + 1u;

        __atomic_store_n(&debug_mode, next, __ATOMIC_RELAXED);
        __atomic_fetch_add(&debug_generation, 1, __ATOMIC_RELEASE);
        return 0;
    }
    return scancode;
}

enum debug_layout_mode debug_input_mode(void)
{
    return (enum debug_layout_mode)
        __atomic_load_n(&debug_mode, __ATOMIC_ACQUIRE);
}

uint32_t debug_input_generation(void)
{
    return __atomic_load_n(&debug_generation, __ATOMIC_ACQUIRE);
}
