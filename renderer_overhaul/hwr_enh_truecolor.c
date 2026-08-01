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
    const struct hwr_setting_desc *gain_desc;
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
    state.format = hwr_truecolor_gl_format(settings->truecolor_format);
    for (i = 0; foundation >= 0 && i < count; i++) {
        const struct hwr_setting_desc *desc = hwr_schema_at(i);
        union hwr_value value;

        if (desc == NULL || hwr_schema_depends_on(desc) != foundation)
            continue;
        /* What the row is worth here, which is the map's override where the
         * look configuration gives it one. */
        value = hwr_look_effective(desc, settings);
        if (hwr_schema_is_original(desc, value))
            continue;
        state.required = 1;
        if (desc->type != HWR_VT_BOOL)
            state.neutral = 0;
    }
    gain_desc = hwr_schema_find("world_gain");
    if (gain_desc != NULL)
        state.gain = hwr_look_effective(gain_desc, settings).f;
    hwr_gl_set_truecolor(&state);
}

int hwr_enh_truecolor_register(void)
{
    return hwr_settings_register_apply(hwr_truecolor_apply, NULL);
}
