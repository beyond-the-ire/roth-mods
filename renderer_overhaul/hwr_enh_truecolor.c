#include "hwr_enh_truecolor.h"

#include "hwr_gl.h"
#include "hwr_look.h"
#include "hwr_schema.h"
#include "hwr_settings.h"

#include <stddef.h>

/* The row the foundation is named by. Nothing else in the tree knows this
 * spelling: a dependent names it through depends_on_key and this module resolves
 * it once. */
#define HWR_TRUECOLOR_ROW "truecolor_force"
/* The multiplicative bring-up dial and the two picture dials. The rows the
 * settings panel previews live are named here rather than there, so the panel
 * stays a table walk and this module keeps deciding what the world image is
 * drawn with. */
#define HWR_GAIN_ROW "world_gain"
#define HWR_BRIGHTNESS_ROW "world_brightness"
#define HWR_CONTRAST_ROW "world_contrast"

/* The settings panel's draft while it is open, and whether there is one. Only
 * the picture dials are read out of it: everything else takes effect when it is
 * applied, as it always has. */
static struct hwr_settings g_preview;
static int g_preview_active;

static int hwr_truecolor_previewed(const struct hwr_setting_desc *desc)
{
    if (!g_preview_active || desc == NULL)
        return 0;
    return desc == hwr_schema_find(HWR_BRIGHTNESS_ROW) ||
           desc == hwr_schema_find(HWR_CONTRAST_ROW);
}

/* What a row is worth to the renderer right now: the panel's draft while a
 * picture dial is being moved, the map's override where the look configuration
 * gives one, and the value in force otherwise. */
static union hwr_value hwr_truecolor_effective(
    const struct hwr_setting_desc *desc, const struct hwr_settings *settings)
{
    if (hwr_truecolor_previewed(desc))
        return hwr_schema_get(&g_preview, desc);
    return hwr_look_effective(desc, settings);
}

static float hwr_truecolor_row_float(const char *key,
                                     const struct hwr_settings *settings,
                                     float fallback)
{
    const struct hwr_setting_desc *desc = hwr_schema_find(key);

    if (desc == NULL)
        return fallback;
    return hwr_truecolor_effective(desc, settings).f;
}

static enum hwr_gl_color_format hwr_truecolor_gl_format(
    enum hwr_truecolor_format format)
{
    switch (format) {
    case HWR_TRUECOLOR_FORMAT_RGBA8:
        return HWR_GL_COLOR_RGBA8;
    case HWR_TRUECOLOR_FORMAT_RGBA16F:
        return HWR_GL_COLOR_RGBA16F;
    case HWR_TRUECOLOR_FORMAT_AUTO:
        break;
    }
    return HWR_GL_COLOR_AUTO;
}

int hwr_truecolor_format_offered(void)
{
    /* Eight bits per channel is always available -- it is the format the
     * foundation is exact in -- so the row is worth offering unless the driver
     * has already refused everything else it lists. */
    return hwr_gl_color_format_available(HWR_GL_COLOR_RGBA16F) ||
           hwr_gl_color_format_available(HWR_GL_COLOR_RGBA8);
}

static void hwr_truecolor_apply(uint32_t mask,
                                const struct hwr_settings *settings,
                                void *userdata)
{
    struct hwr_gl_truecolor state;
    size_t count = hwr_schema_count();
    size_t i;
    int foundation;

    (void)userdata;
    if ((mask & HWR_CHANGE_TRUECOLOR) == 0)
        return;
    foundation = hwr_schema_index_of(hwr_schema_find(HWR_TRUECOLOR_ROW));
    state.required = settings->truecolor_force != 0;
    state.neutral = 1;
    state.gain = 1.0f;
    state.contrast = 1.0f;
    state.format = hwr_truecolor_gl_format(settings->truecolor_format);
    for (i = 0; foundation >= 0 && i < count; i++) {
        const struct hwr_setting_desc *desc = hwr_schema_at(i);
        union hwr_value value;

        if (desc == NULL || hwr_schema_depends_on(desc) != foundation)
            continue;
        /* What the row is worth here: the panel's draft while a picture dial is
         * being moved, and the map's override where the look configuration
         * gives one. */
        value = hwr_truecolor_effective(desc, settings);
        if (hwr_schema_is_original(desc, value))
            continue;
        state.required = 1;
        if (desc->type != HWR_VT_BOOL)
            state.neutral = 0;
    }
    /* One gain: the bring-up dial and the picture's brightness multiply the
     * same colour, so the shader is handed their product. Contrast is the pivot
     * that follows it. */
    state.gain = hwr_truecolor_row_float(HWR_GAIN_ROW, settings, 1.0f) *
                 hwr_truecolor_row_float(HWR_BRIGHTNESS_ROW, settings, 1.0f);
    state.contrast = hwr_truecolor_row_float(HWR_CONTRAST_ROW, settings, 1.0f);
    hwr_gl_set_truecolor(&state);
}

void hwr_truecolor_preview(const struct hwr_settings *draft)
{
    const struct hwr_setting_desc *brightness =
        hwr_schema_find(HWR_BRIGHTNESS_ROW);
    const struct hwr_setting_desc *contrast = hwr_schema_find(HWR_CONTRAST_ROW);

    if (draft == NULL)
        return;
    if (g_preview_active && brightness != NULL && contrast != NULL &&
        hwr_schema_get(&g_preview, brightness).f ==
            hwr_schema_get(draft, brightness).f &&
        hwr_schema_get(&g_preview, contrast).f ==
            hwr_schema_get(draft, contrast).f)
        return;
    g_preview = *draft;
    g_preview_active = 1;
    /* The same road the look layer takes when a map changes what a row is worth
     * without the store moving: raise the bit and let the pump apply it at a
     * point where allocating and linking are safe. */
    hwr_settings_mark_changed(HWR_CHANGE_TRUECOLOR);
}

void hwr_truecolor_preview_clear(void)
{
    if (!g_preview_active)
        return;
    g_preview_active = 0;
    hwr_settings_mark_changed(HWR_CHANGE_TRUECOLOR);
}

int hwr_enh_truecolor_register(void)
{
    return hwr_settings_register_apply(hwr_truecolor_apply, NULL);
}
