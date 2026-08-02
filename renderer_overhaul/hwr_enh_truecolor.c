#include "hwr_enh_truecolor.h"

#include "hwr_gl.h"
#include "hwr_look.h"
#include "hwr_schema.h"
#include "hwr_settings.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* The row the foundation is named by. Nothing else in the tree knows this
 * spelling: a dependent names it through depends_on_key and this module resolves
 * it once. */
#define HWR_TRUECOLOR_ROW "truecolor_force"
/* And where the colour is kept, which is the other row a panel must not preview
 * live: choosing it decides which attachment exists. */
#define HWR_FORMAT_ROW "truecolor_format"
/* The multiplicative bring-up dial and the two picture dials. The rows the
 * settings panel previews live are named here rather than there, so the panel
 * stays a table walk and this module keeps deciding what the world image is
 * drawn with. */
#define HWR_GAIN_ROW "world_gain"
#define HWR_BRIGHTNESS_ROW "world_brightness"
#define HWR_CONTRAST_ROW "world_contrast"
/* The shading law. It is a dependent like any other -- the walk below is what
 * brings the foundation up for it -- and the renderer is told which law to
 * resolve with separately, because that is not a picture dial. */
#define HWR_SHADING_ROW "shading_mode"
/* Where the picture is quantized, which is a property of the screen rather than
 * of the law. It hangs off the law all the same, so it is asked for the law's
 * value once the law is known. */
#define HWR_DITHER_ROW "shading_dither"
/* The light rig the modern law composes with. Every one of these reaches the
 * renderer through the same road as the dials above, so a map's own answer for
 * how it is lit is what the shader is handed. */
#define HWR_ROOM_GAIN_ROW "room_light_gain"
#define HWR_ROOM_GAMMA_ROW "room_light_gamma"
#define HWR_ROOM_FLOOR_ROW "room_light_floor"
/* The anchored room response: which law, the two levels it places a sector
 * between, its shaping, and where the two input anchors come from. */
#define HWR_ROOM_CURVE_ROW "room_light_curve"
#define HWR_ROOM_DARK_ROW "room_light_dark"
#define HWR_ROOM_LIT_ROW "room_light_lit"
#define HWR_ROOM_SHAPE_ROW "room_light_shape"
#define HWR_ROOM_ANCHOR_ROW "room_light_anchor"
#define HWR_ROOM_BYTE_DARK_ROW "room_light_byte_dark"
#define HWR_ROOM_BYTE_LIT_ROW "room_light_byte_lit"
#define HWR_ROOM_WINDOW_MIN_ROW "room_light_window_min"
#define HWR_ROOM_WINDOW_MAX_ROW "room_light_window_max"
#define HWR_ROOM_UNSHADED_ROW "room_light_unshaded_gain"
#define HWR_ROOM_CEILING_ROW "room_light_ceiling_authentic"
#define HWR_WORLD_CEILING_ROW "world_light_ceiling"
#define HWR_DISTANCE_ROW "world_distance_shading"
#define HWR_AMBIENT_ROW "ambient_light"
#define HWR_AMBIENT_COLOR_ROW "ambient_color"
#define HWR_AMBIENT_INTENSITY_ROW "ambient_intensity"
#define HWR_PLAYER_LIGHT_ROW "player_light"
#define HWR_PLAYER_COLOR_ROW "player_light_color"
#define HWR_PLAYER_INTENSITY_ROW "player_light_intensity"
#define HWR_PLAYER_RANGE_ROW "player_light_range"
#define HWR_PLAYER_SCOPE_ROW "player_light_scope"
#define HWR_PLAYER_GLOW_ROW "player_light_glow_scale"
#define HWR_PLAYER_FLICKER_ROW "player_light_flicker"
#define HWR_PLAYER_FLICKER_AMOUNT_ROW "player_light_flicker_amount"
#define HWR_PLAYER_FLICKER_SPEED_ROW "player_light_flicker_speed"
#define HWR_SPRITE_GAIN_ROW "sprite_light_gain"
#define HWR_SPRITE_WRAP_ROW "sprite_light_wrap"

/* What the map now loaded says about the brightness it was authored at: the
 * count of its lit sectors and three order statistics of their bytes. Zero
 * sectors means no map has been staged (or none of its sectors carries a byte
 * this scale is about), and the fit then stands on the written anchors alone.
 *
 * It is held here rather than asked for where it is used because it moves once
 * per map while the rig is worked out on every settings change, and because the
 * map is a different question from the rows: the rows say what a person wants,
 * this says what a place IS. */
static uint32_t g_map_light_counted;
static uint8_t g_map_light_low;
static uint8_t g_map_light_middle;
static uint8_t g_map_light_high;

/* The settings panel's draft while it is open, and whether there is one. Only
 * the picture dials are read out of it: everything else takes effect when it is
 * applied, as it always has. */
static struct hwr_settings g_preview;
static int g_preview_active;

/* Which rows the open panel is allowed to show live. Everything this module
 * decides the world image with, save the two that decide what EXISTS: forcing
 * the foundation and choosing the format both create or release a colour
 * attachment, and that is work for the settings pump at the render boundary
 * rather than for a panel callback. The shading law is previewed, because
 * telling the renderer which law to resolve with is a uniform; the objects that
 * law needs still come into existence on the first frame that asks for them. */
static int hwr_truecolor_previewed(const struct hwr_setting_desc *desc)
{
    if (!g_preview_active || desc == NULL)
        return 0;
    if (desc->change_bit != HWR_CHANGE_TRUECOLOR)
        return 0;
    return desc != hwr_schema_find(HWR_TRUECOLOR_ROW) &&
           desc != hwr_schema_find(HWR_FORMAT_ROW);
}

/* What a row is worth to the renderer right now: the map's override where the
 * look configuration gives one, and otherwise the panel's draft while a dial is
 * being moved or the value in force when no panel is open.
 *
 * The draft stands in for the settings rather than over the map, which is the
 * same precedence a saved configuration meets: the look file's answer for a map
 * wins wherever it gives one. Previewing over the map instead would make every
 * opening of the settings screen a visible change of light in any room a map
 * describes for itself -- and would then put it back on Apply, which is the one
 * thing a preview must never do. */
static union hwr_value hwr_truecolor_effective(
    const struct hwr_setting_desc *desc, const struct hwr_settings *settings)
{
    if (hwr_truecolor_previewed(desc))
        return hwr_look_effective(desc, &g_preview);
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

static uint32_t hwr_truecolor_row_u32(const char *key,
                                      const struct hwr_settings *settings,
                                      uint32_t fallback)
{
    const struct hwr_setting_desc *desc = hwr_schema_find(key);

    if (desc == NULL)
        return fallback;
    return hwr_truecolor_effective(desc, settings).u;
}

static int32_t hwr_truecolor_row_enum(const char *key,
                                      const struct hwr_settings *settings,
                                      int32_t fallback)
{
    const struct hwr_setting_desc *desc = hwr_schema_find(key);

    if (desc == NULL)
        return fallback;
    return hwr_truecolor_effective(desc, settings).e;
}

static int hwr_truecolor_row_bool(const char *key,
                                  const struct hwr_settings *settings,
                                  int fallback)
{
    const struct hwr_setting_desc *desc = hwr_schema_find(key);

    if (desc == NULL)
        return fallback;
    return hwr_truecolor_effective(desc, settings).b != 0;
}

/* A colour row as the three values the shader multiplies, in the display-
 * referred space the person who chose it was looking at. The working space is
 * decided in one place -- the shader -- so nothing is converted here. */
static void hwr_truecolor_row_color(const char *key,
                                    const struct hwr_settings *settings,
                                    uint32_t fallback, float out[3])
{
    const struct hwr_setting_desc *desc = hwr_schema_find(key);
    uint32_t rgb = fallback;

    if (desc != NULL)
        rgb = hwr_truecolor_effective(desc, settings).rgb;
    out[0] = (float)((rgb >> 16) & 0xffu) / 255.0f;
    out[1] = (float)((rgb >> 8) & 0xffu) / 255.0f;
    out[2] = (float)(rgb & 0xffu) / 255.0f;
}

static enum hwr_gl_shading_mode hwr_truecolor_gl_shading(
    enum hwr_shading_mode mode)
{
    switch (mode) {
    case HWR_SHADING_SMOOTH:
        return HWR_GL_SHADING_SMOOTH;
    case HWR_SHADING_MODERN:
        return HWR_GL_SHADING_MODERN;
    case HWR_SHADING_AUTHENTIC:
        break;
    }
    return HWR_GL_SHADING_AUTHENTIC;
}

/* The two INPUT anchors of the anchored response, in authored bytes.
 *
 * Fitted, they are the map's own tenth and ninety-fifth percentiles, held
 * between a narrowest and a widest window about its median. Both bounds earn
 * their place on the shipped maps rather than in theory:
 *
 *   - most maps are built almost entirely at ONE value, so their tenth and
 *     ninety-fifth percentiles are the same byte and the raw window is zero
 *     wide. Without the lower bound a one-byte difference would be the entire
 *     dark-to-lit range and every room would read as fully lit or fully unlit.
 *   - a few are built across a wide spread. Without the upper bound the few
 *     bytes a light switch or a candle moves would be a vanishing share of that
 *     spread, which is the whole complaint this response exists to answer.
 *
 * Written, they are used exactly as written: that is what an escape hatch for a
 * map the fit reads wrongly has to be, and it is also the only way to express
 * the identity case -- the whole authored range mapped onto the whole output
 * range, which is the older law written in the newer one's terms.
 *
 * Returns with `lit` strictly above `dark` whatever it was handed, including a
 * map with no lit sectors at all and a pair somebody wrote backwards. */
static void hwr_truecolor_anchor_bytes(const struct hwr_settings *settings,
                                       float *dark, float *lit)
{
    long low = (long)hwr_truecolor_row_u32(HWR_ROOM_BYTE_DARK_ROW, settings,
                                           112u);
    long high = (long)hwr_truecolor_row_u32(HWR_ROOM_BYTE_LIT_ROW, settings,
                                            208u);

    if (hwr_truecolor_row_enum(HWR_ROOM_ANCHOR_ROW, settings,
                               HWR_ROOM_ANCHOR_AUTO) == HWR_ROOM_ANCHOR_AUTO &&
        g_map_light_counted != 0) {
        long narrowest = (long)hwr_truecolor_row_u32(HWR_ROOM_WINDOW_MIN_ROW,
                                                     settings, 24u);
        long widest = (long)hwr_truecolor_row_u32(HWR_ROOM_WINDOW_MAX_ROW,
                                                  settings, 96u);
        long middle = (long)g_map_light_middle;
        long width;

        low = (long)g_map_light_low;
        high = (long)g_map_light_high;
        if (widest < narrowest)
            widest = narrowest;
        width = high - low;
        /* Both bounds are enforced about the map's MEDIAN rather than about the
         * middle of the raw window: the median is where the map's mass is, and
         * widening about it keeps the commonest sector where it was instead of
         * dragging it toward whichever tail happened to be longer. */
        if (width < narrowest || width > widest) {
            long wanted = width < narrowest ? narrowest : widest;

            low = middle - wanted / 2;
            high = low + wanted;
            /* A median near either end of the byte range cannot be the centre
             * of the window as well, so the window slides rather than shrinks;
             * only a window wider than the range itself gives up width. */
            if (low < 0) {
                high -= low;
                low = 0;
            }
            if (high > 255) {
                low -= high - 255;
                high = 255;
                if (low < 0)
                    low = 0;
            }
        }
    }
    if (high <= low)
        high = low + 1;
    *dark = (float)low;
    *lit = (float)high;
}

/* The rig the modern law composes with, as the rows in force describe it.
 *
 * A switch that is off is spent here rather than in the shader: an unlit fill
 * is one whose strength is zero, which is the same expression with nothing in
 * it, and it keeps the fragment tail free of a branch per light per pixel. */
static void hwr_truecolor_light(const struct hwr_settings *settings,
                                struct hwr_gl_light *light)
{
    memset(light, 0, sizeof *light);
    light->room_gain = hwr_truecolor_row_float(HWR_ROOM_GAIN_ROW, settings,
                                               1.0f);
    light->room_gamma = hwr_truecolor_row_float(HWR_ROOM_GAMMA_ROW, settings,
                                                2.2f);
    light->room_floor = hwr_truecolor_row_float(HWR_ROOM_FLOOR_ROW, settings,
                                                0.0f);
    /* The anchored response, worked out here in full: the shader is handed two
     * numbers it can subtract and divide, not a map and a table of rows. The
     * input anchors are taken to the same exponent the authored byte is, which
     * is what makes the normalization a normalization -- both sides of the
     * fraction are in one space, and it is the space the map's own steps are
     * perceptually even in. */
    light->room_anchored =
        hwr_truecolor_row_enum(HWR_ROOM_CURVE_ROW, settings,
                               HWR_ROOM_CURVE_ANCHORED) ==
        HWR_ROOM_CURVE_ANCHORED;
    {
        float dark_byte;
        float lit_byte;

        hwr_truecolor_anchor_bytes(settings, &dark_byte, &lit_byte);
        light->room_anchor_low = powf(dark_byte / 128.0f, light->room_gamma);
        light->room_anchor_high = powf(lit_byte / 128.0f, light->room_gamma);
    }
    light->room_dark = hwr_truecolor_row_float(HWR_ROOM_DARK_ROW, settings,
                                               0.02f);
    light->room_lit = hwr_truecolor_row_float(HWR_ROOM_LIT_ROW, settings, 1.0f);
    light->room_shape = hwr_truecolor_row_float(HWR_ROOM_SHAPE_ROW, settings,
                                                1.0f);
    /* The row says how much of the unshaded class survives as authored; the rig
     * carries how far it is pulled off that, so a rig nobody filled in is the
     * picture that class always had. */
    light->room_unshaded_pull =
        1.0f - hwr_truecolor_row_float(HWR_ROOM_UNSHADED_ROW, settings, 1.0f);
    light->world_ceiling = hwr_truecolor_row_float(HWR_WORLD_CEILING_ROW,
                                                   settings, 1.0f);
    light->room_ceiling_authentic =
        hwr_truecolor_row_bool(HWR_ROOM_CEILING_ROW, settings, 0);
    light->distance_shading = hwr_truecolor_row_float(HWR_DISTANCE_ROW,
                                                      settings, 0.0f);
    hwr_truecolor_row_color(HWR_AMBIENT_COLOR_ROW, settings, 0x00666666u,
                            light->ambient_color);
    light->ambient_intensity =
        hwr_truecolor_row_bool(HWR_AMBIENT_ROW, settings, 0)
            ? hwr_truecolor_row_float(HWR_AMBIENT_INTENSITY_ROW, settings, 0.0f)
            : 0.0f;
    hwr_truecolor_row_color(HWR_PLAYER_COLOR_ROW, settings, 0x00ffede0u,
                            light->lamp_color);
    light->lamp_intensity =
        hwr_truecolor_row_bool(HWR_PLAYER_LIGHT_ROW, settings, 0)
            ? hwr_truecolor_row_float(HWR_PLAYER_INTENSITY_ROW, settings, 0.0f)
            : 0.0f;
    light->lamp_range = hwr_truecolor_row_float(HWR_PLAYER_RANGE_ROW, settings,
                                                800.0f);
    /* The light the player carries is a light the player carries: it reaches
     * whatever it can see. The mark it used to be confined to is the engine's
     * live word for "this room's own fixtures are out", which it clears the
     * moment a switch raises the room -- so confining the carried light to it
     * put the light out in exactly the rooms that had just been lit. */
    light->lamp_scope_all =
        hwr_truecolor_row_enum(HWR_PLAYER_SCOPE_ROW, settings,
                               HWR_LIGHT_SCOPE_ALL) == HWR_LIGHT_SCOPE_ALL;
    light->lamp_glow_scale = hwr_truecolor_row_float(HWR_PLAYER_GLOW_ROW,
                                                     settings, 0.0f);
    /* The flame's switch is carried rather than spent into a strength the way
     * the fills above are: what it turns off is a clock, not a term, and the
     * two dials beside it describe the shape of a flame rather than how much of
     * one there is -- the same reading as the lamp's own reach, which is what
     * it is whether or not the lamp is lit. */
    light->lamp_flicker =
        hwr_truecolor_row_bool(HWR_PLAYER_FLICKER_ROW, settings, 0);
    light->lamp_flicker_amount =
        hwr_truecolor_row_float(HWR_PLAYER_FLICKER_AMOUNT_ROW, settings, 0.30f);
    light->lamp_flicker_speed =
        hwr_truecolor_row_float(HWR_PLAYER_FLICKER_SPEED_ROW, settings, 1.80f);
    light->sprite_gain = hwr_truecolor_row_float(HWR_SPRITE_GAIN_ROW, settings,
                                                 1.0f);
    light->sprite_wrap = hwr_truecolor_row_float(HWR_SPRITE_WRAP_ROW, settings,
                                                 0.5f);
    /* A reach of nothing would divide the falloff by zero; the row's own
     * minimum is well clear of it and this is what holds a value that arrived
     * some other way. */
    if (!(light->lamp_range > 1.0f))
        light->lamp_range = 1.0f;
}

/* Automatic follows the law: the original picture is a palette picture and every
 * byte of it is already a colour the map authored, so there is nothing for a
 * pattern to recover and it is left alone. Any other law composes a colour
 * between two of them, and the levels it lands on near black are finer than the
 * canvas it is read back through. */
static int hwr_truecolor_dither_wanted(enum hwr_shading_dither choice,
                                       enum hwr_gl_shading_mode mode)
{
    switch (choice) {
    case HWR_SHADING_DITHER_OFF:
        return 0;
    case HWR_SHADING_DITHER_ON:
        return 1;
    case HWR_SHADING_DITHER_AUTO:
        break;
    }
    return mode != HWR_GL_SHADING_AUTHENTIC;
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
    /* The law the world image is resolved with, through the same road as the
     * dials so a map's own choice reaches it. Its effect on whether the
     * foundation exists at all is the walk above, which needs no help: an
     * enumeration off its Original both requires the foundation and clears the
     * neutral early-out, which is what a law that resolves colour between two
     * rows has to have. */
    state.shading_mode = hwr_truecolor_gl_shading(
        (enum hwr_shading_mode)hwr_truecolor_row_enum(
            HWR_SHADING_ROW, settings, HWR_SHADING_AUTHENTIC));
    state.dither = hwr_truecolor_dither_wanted(
        (enum hwr_shading_dither)hwr_truecolor_row_enum(
            HWR_DITHER_ROW, settings, HWR_SHADING_DITHER_AUTO),
        state.shading_mode);
    /* Read whatever the law is: the rig costs nothing to derive, only the
     * modern law reads it, and deriving it unconditionally keeps one road from
     * the rows to the renderer rather than two. */
    hwr_truecolor_light(settings, &state.light);
    hwr_gl_set_truecolor(&state);
}

void hwr_truecolor_set_map_lighting(uint32_t counted, uint8_t low,
                                    uint8_t middle, uint8_t high)
{
    if (counted == g_map_light_counted && low == g_map_light_low &&
        middle == g_map_light_middle && high == g_map_light_high)
        return;
    g_map_light_counted = counted;
    g_map_light_low = low;
    g_map_light_middle = middle;
    g_map_light_high = high;
    /* The same road the look layer takes when a map changes what a row is worth
     * without the store moving: raise the bit and let the pump work the rig out
     * again where doing so is safe. */
    hwr_settings_mark_changed(HWR_CHANGE_TRUECOLOR);
}

void hwr_truecolor_preview(const struct hwr_settings *draft)
{
    if (draft == NULL)
        return;
    /* Whether anything the panel is allowed to show live has moved. The whole
     * draft is compared rather than a named pair, so a row added to the family
     * is previewed by being in the family. */
    if (g_preview_active && memcmp(&g_preview, draft, sizeof g_preview) == 0)
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
