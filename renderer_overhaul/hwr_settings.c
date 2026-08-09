#include "hwr_settings.h"

#include "hwr_schema.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Room for the baseline hooks plus one per enhancement module. Overflow is
 * reported here rather than left to a caller that ignores the return, because a
 * hook that is never registered is a subsystem that silently never applies. */
#define HWR_MAX_APPLY_HOOKS 16u

struct hwr_apply_hook {
    hwr_settings_apply_fn apply;
    void *userdata;
};

static struct hwr_settings g_settings;
static struct hwr_apply_hook g_hooks[HWR_MAX_APPLY_HOOKS];
static uint32_t g_hook_count;
static uint32_t g_dirty;

void hwr_settings_defaults(struct hwr_settings *settings)
{
    if (settings == NULL)
        return;
    hwr_schema_defaults(settings);
}

void hwr_settings_init_defaults(void)
{
    hwr_settings_defaults(&g_settings);
    g_hook_count = 0;
    /* Every bit the table can raise, so the first apply reaches every hook. */
    g_dirty = hwr_schema_all_change_bits();
}

const struct hwr_settings *hwr_settings_get(void)
{
    return &g_settings;
}

/* The comparison the setters used to spell out one field at a time. */
static int hwr_value_equal(const struct hwr_setting_desc *desc,
                           union hwr_value a, union hwr_value b)
{
    switch (desc->type) {
    case HWR_VT_BOOL:
        return (a.b != 0) == (b.b != 0);
    case HWR_VT_U32:
    case HWR_VT_COLOR:
        return a.u == b.u;
    case HWR_VT_F32:
        return a.f == b.f;
    case HWR_VT_ENUM:
        return a.e == b.e;
    }
    return 0;
}

/* One field into the live store: the descriptor decides what is in range and
 * which change bit a real change raises, so the file, the store, and the
 * settings overlay cannot drift apart. A value outside the descriptor's range
 * leaves the field alone. */
static void hwr_settings_commit(const struct hwr_setting_desc *desc,
                                union hwr_value value)
{
    union hwr_value current;

    if (desc == NULL || desc->offset == HWR_SCHEMA_NO_FIELD)
        return;
    if (!hwr_schema_validate(desc, value, NULL, 0))
        return;
    current = hwr_schema_get(&g_settings, desc);
    if (hwr_value_equal(desc, current, value))
        return;
    hwr_schema_set(&g_settings, desc, value);
    g_dirty |= desc->change_bit;
}

static void hwr_settings_commit_key(const char *key, union hwr_value value)
{
    hwr_settings_commit(hwr_schema_find(key), value);
}

void hwr_settings_replace(const struct hwr_settings *settings)
{
    size_t count = hwr_schema_count();
    size_t i;

    if (settings == NULL)
        return;
    for (i = 0; i < count; i++) {
        const struct hwr_setting_desc *desc = hwr_schema_at(i);
        /* An alias is a second spelling of another row's field, so committing
         * it would only repeat that row's work. */
        if (desc->exposure == HWR_EXP_ALIAS ||
            desc->offset == HWR_SCHEMA_NO_FIELD)
            continue;
        hwr_settings_commit(desc, hwr_schema_get(settings, desc));
    }
}

int hwr_settings_register_apply(hwr_settings_apply_fn apply, void *userdata)
{
    if (apply == NULL)
        return -1;
    if (g_hook_count >= HWR_MAX_APPLY_HOOKS) {
        fprintf(stderr,
                "[renderer_overhaul] settings apply-hook table is full at %u hooks\n",
                (unsigned)HWR_MAX_APPLY_HOOKS);
        return -1;
    }
    g_hooks[g_hook_count].apply = apply;
    g_hooks[g_hook_count].userdata = userdata;
    g_hook_count++;
    return 0;
}

void hwr_settings_mark_changed(uint32_t bits)
{
    g_dirty |= bits;
}

void hwr_settings_apply_pending(void)
{
    uint32_t i;
    uint32_t mask = g_dirty;

    if (mask == 0)
        return;
    g_dirty = 0;
    for (i = 0; i < g_hook_count; i++)
        g_hooks[i].apply(mask, &g_settings, g_hooks[i].userdata);
}

void hwr_settings_set_enabled(int enabled)
{
    union hwr_value value;

    memset(&value, 0, sizeof value);
    value.b = enabled != 0;
    hwr_settings_commit_key("enabled", value);
}

void hwr_settings_set_shade_depth_scale(float scale)
{
    union hwr_value value;

    memset(&value, 0, sizeof value);
    value.f = scale;
    hwr_settings_commit_key("shade_depth_scale", value);
}

void hwr_settings_set_debug(int debug)
{
    union hwr_value value;

    memset(&value, 0, sizeof value);
    value.b = debug != 0;
    hwr_settings_commit_key("debug", value);
}

void hwr_settings_set_internal_size(uint32_t width, uint32_t height)
{
    union hwr_value value;

    memset(&value, 0, sizeof value);
    value.u = width;
    hwr_settings_commit_key("internal_w", value);
    value.u = height;
    hwr_settings_commit_key("internal_h", value);
}

void hwr_settings_set_window_mode(enum hwr_window_mode mode)
{
    union hwr_value value;

    memset(&value, 0, sizeof value);
    value.e = (int32_t)mode;
    hwr_settings_commit_key("window_mode", value);
}

void hwr_settings_set_present_mode(enum hwr_present_mode mode)
{
    union hwr_value value;

    memset(&value, 0, sizeof value);
    value.e = (int32_t)mode;
    hwr_settings_commit_key("present_mode", value);
}

void hwr_settings_set_min_present_interval_us(uint32_t interval_us)
{
    union hwr_value value;

    memset(&value, 0, sizeof value);
    value.u = interval_us;
    hwr_settings_commit_key("min_present_interval_us", value);
}

void hwr_settings_set_fov_deg(float fov_deg)
{
    union hwr_value value;

    memset(&value, 0, sizeof value);
    value.f = fov_deg;
    hwr_settings_commit_key("fov_deg", value);
}

void hwr_settings_set_pitch_mode(enum hwr_pitch_mode mode)
{
    union hwr_value value;

    memset(&value, 0, sizeof value);
    value.e = (int32_t)mode;
    hwr_settings_commit_key("pitch_mode", value);
}

/* The look layer's own writer into the store. It is a place in the list the
 * folder scan built, so it is committed through the row like every other value
 * and a place the list does not hold is refused rather than stored. */
void hwr_settings_set_game_config(int32_t slot)
{
    union hwr_value value;

    memset(&value, 0, sizeof value);
    value.e = slot;
    hwr_settings_commit_key("game_config", value);
}

/* The settings overlay reads its key straight from the store when it handles a
 * scancode, so the row carries no change bit and no hook reacts to it. */
void hwr_settings_set_overlay_key(uint32_t scancode)
{
    union hwr_value value;

    memset(&value, 0, sizeof value);
    value.e = (int32_t)scancode;
    hwr_settings_commit_key("overlay_key", value);
}
