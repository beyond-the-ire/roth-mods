#include "hwr_schema.h"

#include "hwr_sysinfo.h"
#include "hwr_util.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The enum-backed fields are plain C enumerations and the overlay key is a
 * scancode: every one of them is a four-byte object, which is what the generic
 * accessors below copy in and out. */
_Static_assert(sizeof(enum hwr_window_mode) == sizeof(int32_t),
               "enum fields must be four bytes wide");
_Static_assert(sizeof(enum hwr_present_mode) == sizeof(int32_t),
               "enum fields must be four bytes wide");
_Static_assert(sizeof(enum hwr_pitch_mode) == sizeof(int32_t),
               "enum fields must be four bytes wide");
_Static_assert(sizeof(int) == sizeof(int32_t),
               "boolean fields must be four bytes wide");

static const struct hwr_enum_value HWR_WINDOW_MODE_VALUES[] = {
    { "default",    HWR_WINDOW_DEFAULT,    "Host default" },
    { "windowed",   HWR_WINDOW_WINDOWED,   "Windowed" },
    { "borderless", HWR_WINDOW_BORDERLESS, "Borderless fullscreen" }
};

static const struct hwr_enum_value HWR_PRESENT_MODE_VALUES[] = {
    { "fifo",      HWR_PRESENT_FIFO,      "Vertical synchronization" },
    { "immediate", HWR_PRESENT_IMMEDIATE, "No synchronization" },
    { "adaptive",  HWR_PRESENT_ADAPTIVE,  "Adaptive synchronization" }
};

static const struct hwr_enum_value HWR_PITCH_MODE_VALUES[] = {
    { "horizon_shift", HWR_PITCH_HORIZON_SHIFT, "Authentic horizon shift" },
    { "true_rotation", HWR_PITCH_TRUE_ROTATION, "True rotation" }
};

/* Retired, and kept only so that a configuration file written for an earlier
 * build still parses. Neither token reaches a field. */
static const struct hwr_enum_value HWR_RENDER_MODE_VALUES[] = {
    { "authentic", 0, "Authentic" },
    { "enhanced",  1, "Enhanced" }
};

/* Set-1 make scancodes, spelled the way the other first-party mods spell them
 * so one key name means one key across the whole mods folder. */
static const struct hwr_enum_value HWR_KEY_VALUES[] = {
    {"esc", 0x01, NULL},
    {"1", 0x02, NULL}, {"2", 0x03, NULL}, {"3", 0x04, NULL},
    {"4", 0x05, NULL}, {"5", 0x06, NULL}, {"6", 0x07, NULL},
    {"7", 0x08, NULL}, {"8", 0x09, NULL}, {"9", 0x0a, NULL},
    {"0", 0x0b, NULL},
    {"minus", 0x0c, NULL}, {"equals", 0x0d, NULL},
    {"backspace", 0x0e, NULL}, {"tab", 0x0f, NULL},
    {"q", 0x10, NULL}, {"w", 0x11, NULL}, {"e", 0x12, NULL},
    {"r", 0x13, NULL}, {"t", 0x14, NULL}, {"y", 0x15, NULL},
    {"u", 0x16, NULL}, {"i", 0x17, NULL}, {"o", 0x18, NULL},
    {"p", 0x19, NULL},
    {"lbracket", 0x1a, NULL}, {"rbracket", 0x1b, NULL},
    {"enter", 0x1c, NULL}, {"ctrl", 0x1d, NULL},
    {"a", 0x1e, NULL}, {"s", 0x1f, NULL}, {"d", 0x20, NULL},
    {"f", 0x21, NULL}, {"g", 0x22, NULL}, {"h", 0x23, NULL},
    {"j", 0x24, NULL}, {"k", 0x25, NULL}, {"l", 0x26, NULL},
    {"semicolon", 0x27, NULL}, {"quote", 0x28, NULL},
    {"grave", 0x29, NULL}, {"lshift", 0x2a, NULL},
    {"backslash", 0x2b, NULL},
    {"z", 0x2c, NULL}, {"x", 0x2d, NULL}, {"c", 0x2e, NULL},
    {"v", 0x2f, NULL}, {"b", 0x30, NULL}, {"n", 0x31, NULL},
    {"m", 0x32, NULL},
    {"comma", 0x33, NULL}, {"period", 0x34, NULL}, {"slash", 0x35, NULL},
    {"rshift", 0x36, NULL}, {"alt", 0x38, NULL}, {"space", 0x39, NULL},
    {"capslock", 0x3a, NULL},
    {"f1", 0x3b, NULL}, {"f2", 0x3c, NULL}, {"f3", 0x3d, NULL},
    {"f4", 0x3e, NULL}, {"f5", 0x3f, NULL}, {"f6", 0x40, NULL},
    {"f7", 0x41, NULL}, {"f8", 0x42, NULL}, {"f9", 0x43, NULL},
    {"f10", 0x44, NULL}, {"f11", 0x57, NULL}, {"f12", 0x58, NULL}
};

/* The frame-rate cap and the present interval are two spellings of one field:
 * the relationship lives here and nowhere else. */
static void hwr_fps_cap_to_field(struct hwr_settings *settings,
                                 union hwr_value value)
{
    settings->min_present_interval_us =
        value.u == 0 ? 0u : 1000000u / value.u;
}

static union hwr_value hwr_fps_cap_from_field(
    const struct hwr_settings *settings)
{
    union hwr_value value;

    memset(&value, 0, sizeof value);
    value.u = settings->min_present_interval_us == 0
            ? 0u : 1000000u / settings->min_present_interval_us;
    return value;
}

#define HWR_COUNT_OF(table) (sizeof (table) / sizeof (table)[0])

static const struct hwr_setting_desc HWR_SCHEMA[] = {
    {
        .key = "enabled",
        .type = HWR_VT_BOOL,
        .category = HWR_CAT_RENDERING,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_ENABLED,
        .offset = offsetof(struct hwr_settings, enabled),
        .flags = HWR_SF_RESTART_FREE,
        .def = { .b = 1 },
        .original = { .b = 1 },
        .depends_on = -1,
        .label = "Hardware rendering",
        .help = "Turn this off to leave the original renderer completely "
                "untouched."
    },
    {
        .key = "internal_w",
        .type = HWR_VT_U32,
        .category = HWR_CAT_DISPLAY,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_INTERNAL_SIZE,
        .offset = offsetof(struct hwr_settings, internal_w),
        .flags = HWR_SF_ZERO_IS_AUTO | HWR_SF_NEEDS_CONFIRM |
                 HWR_SF_RESTART_FREE,
        .def = { .u = 1280u },
        .original = { .u = 640u },
        .min = { .u = 0u },
        .max = { .u = 16384u },
        .step = { .u = 16u },
        .page_step = { .u = 160u },
        .depends_on = -1,
        .widget = HWR_WIDGET_RESOLUTION_PAIR,
        .label = "Render width",
        .help = "World render width in pixels. Zero follows the engine "
                "viewport.",
        .auto_label = "Follow engine"
    },
    {
        .key = "internal_h",
        .type = HWR_VT_U32,
        .category = HWR_CAT_DISPLAY,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_INTERNAL_SIZE,
        .offset = offsetof(struct hwr_settings, internal_h),
        .flags = HWR_SF_ZERO_IS_AUTO | HWR_SF_NEEDS_CONFIRM |
                 HWR_SF_RESTART_FREE,
        .def = { .u = 720u },
        .original = { .u = 480u },
        .min = { .u = 0u },
        .max = { .u = 16384u },
        .step = { .u = 16u },
        .page_step = { .u = 160u },
        .depends_on = -1,
        .widget = HWR_WIDGET_RESOLUTION_PAIR,
        .label = "Render height",
        .help = "World render height in pixels. Zero follows the engine "
                "viewport.",
        .auto_label = "Follow engine"
    },
    {
        .key = "window_mode",
        .type = HWR_VT_ENUM,
        .category = HWR_CAT_DISPLAY,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_WINDOW_MODE,
        .offset = offsetof(struct hwr_settings, window_mode),
        .flags = HWR_SF_NEEDS_CONFIRM | HWR_SF_RESTART_FREE,
        .def = { .e = HWR_WINDOW_DEFAULT },
        .original = { .e = HWR_WINDOW_DEFAULT },
        .values = HWR_WINDOW_MODE_VALUES,
        .value_count = HWR_COUNT_OF(HWR_WINDOW_MODE_VALUES),
        .depends_on = -1,
        .label = "Window mode",
        .help = "How the game window is presented. The host default leaves "
                "the window mode alone."
    },
    {
        .key = "present_mode",
        .type = HWR_VT_ENUM,
        .category = HWR_CAT_DISPLAY,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_PRESENT_MODE,
        .offset = offsetof(struct hwr_settings, present_mode),
        .flags = HWR_SF_RESTART_FREE,
        .def = { .e = HWR_PRESENT_FIFO },
        .original = { .e = HWR_PRESENT_FIFO },
        .values = HWR_PRESENT_MODE_VALUES,
        .value_count = HWR_COUNT_OF(HWR_PRESENT_MODE_VALUES),
        .depends_on = -1,
        .label = "Synchronization",
        .help = "Vertical synchronization used when the host presents a "
                "frame. Adaptive is used only where the host supports it."
    },
    {
        /* The frame-rate cap is the spelling a player uses for the present
         * interval, so it is an ordinary consumer row that happens to reach its
         * field through a transform. Being a second spelling is what to_field
         * and from_field say; it is not a visibility class, and this is the only
         * frame-rate control the settings overlay has to offer. */
        .key = "fps_cap",
        .type = HWR_VT_U32,
        .category = HWR_CAT_DISPLAY,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_PRESENT_INTERVAL,
        .offset = HWR_SCHEMA_NO_FIELD,
        .flags = HWR_SF_ZERO_IS_AUTO | HWR_SF_RESTART_FREE,
        .def = { .u = 0u },
        .original = { .u = 0u },
        .min = { .u = 0u },
        .max = { .u = 1000000u },
        .step = { .u = 5u },
        .page_step = { .u = 30u },
        .depends_on = -1,
        .label = "Frame-rate cap",
        .help = "Maximum host presents per second. Zero is uncapped.",
        .auto_label = "Uncapped",
        .to_field = hwr_fps_cap_to_field,
        .from_field = hwr_fps_cap_from_field
    },
    {
        .key = "min_present_interval_us",
        .type = HWR_VT_U32,
        .category = HWR_CAT_DISPLAY,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_PRESENT_INTERVAL,
        .offset = offsetof(struct hwr_settings, min_present_interval_us),
        .flags = HWR_SF_ZERO_IS_AUTO | HWR_SF_RESTART_FREE,
        .def = { .u = 0u },
        .original = { .u = 0u },
        .min = { .u = 0u },
        .max = { .u = 10000000u },
        .depends_on = -1,
        .label = "Minimum present interval",
        .help = "A direct interval in microseconds, as an alternative to the "
                "frame-rate cap."
    },
    {
        .key = "fov_deg",
        .type = HWR_VT_F32,
        .category = HWR_CAT_VIEW,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_LIVE,
        .change_bit = HWR_CHANGE_FOV,
        .offset = offsetof(struct hwr_settings, fov_deg),
        .flags = HWR_SF_ZERO_IS_AUTO | HWR_SF_RESTART_FREE,
        .def = { .f = 0.0f },
        .original = { .f = 0.0f },
        .min = { .f = 30.0f },
        .max = { .f = 170.0f },
        .step = { .f = 1.0f },
        .page_step = { .f = 10.0f },
        .depends_on = -1,
        .label = "Field of view",
        .help = "Horizontal field of view at the original 4:3 aspect. Zero "
                "preserves the original projection; wider output aspects gain "
                "horizontal view area either way.",
        .auto_label = "Original"
    },
    {
        .key = "pitch_mode",
        .type = HWR_VT_ENUM,
        .category = HWR_CAT_VIEW,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_NONE,
        .change_bit = HWR_CHANGE_PITCH_MODE,
        .offset = offsetof(struct hwr_settings, pitch_mode),
        .flags = 0,
        .def = { .e = HWR_PITCH_HORIZON_SHIFT },
        .original = { .e = HWR_PITCH_HORIZON_SHIFT },
        .values = HWR_PITCH_MODE_VALUES,
        .value_count = HWR_COUNT_OF(HWR_PITCH_MODE_VALUES),
        .depends_on = -1,
        .label = "Pitch mode",
        .help = "Reserved for a later camera mode. This build uses the "
                "authentic horizon shift."
    },
    {
        .key = "debug",
        .type = HWR_VT_BOOL,
        .category = HWR_CAT_RENDERING,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_DEBUG,
        .offset = offsetof(struct hwr_settings, debug),
        .flags = HWR_SF_RESTART_FREE,
        .def = { .b = 0 },
        .original = { .b = 0 },
        .depends_on = -1,
        .label = "Diagnostic logging",
        .help = "Print map geometry and texture archive statistics when a map "
                "loads."
    },
    {
        .key = "shade_depth_scale",
        .type = HWR_VT_F32,
        .category = HWR_CAT_RENDERING,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_LIVE,
        .change_bit = HWR_CHANGE_SHADE_SCALE,
        .offset = offsetof(struct hwr_settings, shade_depth_scale),
        .flags = HWR_SF_EXCLUSIVE_MIN | HWR_SF_RESTART_FREE,
        .def = { .f = 1.0f },
        .original = { .f = 1.0f },
        .min = { .f = 0.0f },
        .max = { .f = 4096.0f },
        .depends_on = -1,
        .label = "Distance shading scale",
        .help = "Distance-shading calibration multiplier applied to the view "
                "depth."
    },
    {
        .key = "overlay_key",
        .type = HWR_VT_ENUM,
        .category = HWR_CAT_RENDERING,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_LIVE,
        .change_bit = 0,
        .offset = offsetof(struct hwr_settings, overlay_key),
        .flags = HWR_SF_RESTART_FREE,
        .def = { .e = 0x58 },
        .original = { .e = 0x58 },
        .values = HWR_KEY_VALUES,
        .value_count = HWR_COUNT_OF(HWR_KEY_VALUES),
        .depends_on = -1,
        .widget = HWR_WIDGET_KEY_CAPTURE,
        .label = "Settings overlay key",
        .help = "Key that opens the renderer settings overlay."
    },
    {
        /* Retired: authentic rendering is what every enhancement being off
         * produces, so there is no whole-renderer mode to select. The key is
         * still accepted, and a retired row's help is the clause the note
         * about it carries. */
        .key = "render_mode",
        .type = HWR_VT_ENUM,
        .category = HWR_CAT_RENDERING,
        .exposure = HWR_EXP_RETIRED,
        .apply_kind = HWR_APPLY_NONE,
        .change_bit = 0,
        .offset = HWR_SCHEMA_NO_FIELD,
        .flags = 0,
        .values = HWR_RENDER_MODE_VALUES,
        .value_count = HWR_COUNT_OF(HWR_RENDER_MODE_VALUES),
        .depends_on = -1,
        .label = "Render mode",
        .help = "enhancements are individual settings, each with its own key"
    },
    {
        /* Retired: there is no manufactured world frame to colour any more.
         * The key is still accepted so an existing configuration file keeps
         * loading as a whole. */
        .key = "solid_color",
        .type = HWR_VT_COLOR,
        .category = HWR_CAT_RENDERING,
        .exposure = HWR_EXP_RETIRED,
        .apply_kind = HWR_APPLY_NONE,
        .change_bit = 0,
        .offset = HWR_SCHEMA_NO_FIELD,
        .flags = 0,
        .min = { .rgb = 0x00000000u },
        .max = { .rgb = 0x00ffffffu },
        .depends_on = -1,
        .label = "Solid color",
        .help = "the world frame it coloured is drawn from the map itself now"
    },
    /* Reported state. These rows hold no value of their own and no key: each
     * one asks the renderer or the host what is actually happening and answers
     * in words, which is what makes the System section a table walk like every
     * other section rather than a hand-written screen. */
    {
        .key = NULL,
        .category = HWR_CAT_SYSTEM,
        .exposure = HWR_EXP_READONLY,
        .apply_kind = HWR_APPLY_NONE,
        .offset = HWR_SCHEMA_NO_FIELD,
        .depends_on = -1,
        .label = "Plugin version",
        .help = "Which build of the renderer is running.",
        .read_info = hwr_sysinfo_read_version
    },
    {
        .key = NULL,
        .category = HWR_CAT_SYSTEM,
        .exposure = HWR_EXP_READONLY,
        .apply_kind = HWR_APPLY_NONE,
        .offset = HWR_SCHEMA_NO_FIELD,
        .depends_on = -1,
        .label = "Hardware rendering",
        .help = "Whether the hardware renderer is drawing the world, and what "
                "is drawing it if not.",
        .read_info = hwr_sysinfo_read_renderer
    },
    {
        .key = NULL,
        .category = HWR_CAT_SYSTEM,
        .exposure = HWR_EXP_READONLY,
        .apply_kind = HWR_APPLY_NONE,
        .offset = HWR_SCHEMA_NO_FIELD,
        .depends_on = -1,
        .label = "Render size",
        .help = "The size the world is actually being rendered at, which can "
                "differ from the requested size the display section holds.",
        .read_info = hwr_sysinfo_read_canvas
    },
    {
        .key = NULL,
        .category = HWR_CAT_SYSTEM,
        .exposure = HWR_EXP_READONLY,
        .apply_kind = HWR_APPLY_NONE,
        .offset = HWR_SCHEMA_NO_FIELD,
        .depends_on = -1,
        .label = "Engine view",
        .help = "The size of the engine's own world view, which the interface "
                "is drawn around.",
        .read_info = hwr_sysinfo_read_viewport
    },
    {
        .key = NULL,
        .category = HWR_CAT_SYSTEM,
        .exposure = HWR_EXP_READONLY,
        .apply_kind = HWR_APPLY_NONE,
        .offset = HWR_SCHEMA_NO_FIELD,
        .depends_on = -1,
        .label = "Composition",
        .help = "How the world image and the engine's own screen are put "
                "together.",
        .read_info = hwr_sysinfo_read_composition
    },
    {
        .key = NULL,
        .category = HWR_CAT_SYSTEM,
        .exposure = HWR_EXP_READONLY,
        .apply_kind = HWR_APPLY_NONE,
        .offset = HWR_SCHEMA_NO_FIELD,
        .depends_on = -1,
        .label = "Window mode in use",
        .help = "The window mode the host is in, which is what the requested "
                "mode became.",
        .read_info = hwr_sysinfo_read_window_mode
    },
    {
        .key = NULL,
        .category = HWR_CAT_SYSTEM,
        .exposure = HWR_EXP_READONLY,
        .apply_kind = HWR_APPLY_NONE,
        .offset = HWR_SCHEMA_NO_FIELD,
        .depends_on = -1,
        .label = "Synchronization in use",
        .help = "The synchronization the host is presenting with. Adaptive is "
                "only used where the system provides it.",
        .read_info = hwr_sysinfo_read_present_mode
    },
    {
        .key = NULL,
        .category = HWR_CAT_SYSTEM,
        .exposure = HWR_EXP_READONLY,
        .apply_kind = HWR_APPLY_NONE,
        .offset = HWR_SCHEMA_NO_FIELD,
        .depends_on = -1,
        .label = "Present interval in use",
        .help = "The shortest time the host leaves between two presents.",
        .read_info = hwr_sysinfo_read_present_interval
    },
    {
        .key = NULL,
        .category = HWR_CAT_SYSTEM,
        .exposure = HWR_EXP_READONLY,
        .apply_kind = HWR_APPLY_NONE,
        .offset = HWR_SCHEMA_NO_FIELD,
        .depends_on = -1,
        .label = "Last display request",
        .help = "What the host did with the most recent window or "
                "synchronization request.",
        .read_info = hwr_sysinfo_read_request_result
    },
    {
        .key = NULL,
        .category = HWR_CAT_SYSTEM,
        .exposure = HWR_EXP_READONLY,
        .apply_kind = HWR_APPLY_NONE,
        .offset = HWR_SCHEMA_NO_FIELD,
        .depends_on = -1,
        .label = "Display modes",
        .help = "How many modes this display reports, and the largest of "
                "them.",
        .read_info = hwr_sysinfo_read_display_modes
    },
    {
        .key = NULL,
        .category = HWR_CAT_SYSTEM,
        .exposure = HWR_EXP_READONLY,
        .apply_kind = HWR_APPLY_NONE,
        .offset = HWR_SCHEMA_NO_FIELD,
        .depends_on = -1,
        .label = "Largest render size",
        .help = "The largest world image this host will accept.",
        .read_info = hwr_sysinfo_read_present_limit
    },
    {
        .key = NULL,
        .category = HWR_CAT_SYSTEM,
        .exposure = HWR_EXP_READONLY,
        .apply_kind = HWR_APPLY_NONE,
        .offset = HWR_SCHEMA_NO_FIELD,
        .depends_on = -1,
        .label = "Graphics driver",
        .help = "The OpenGL version and driver the renderer is using.",
        .read_info = hwr_sysinfo_read_graphics
    },
    {
        .key = NULL,
        .category = HWR_CAT_SYSTEM,
        .exposure = HWR_EXP_READONLY,
        .apply_kind = HWR_APPLY_NONE,
        .offset = HWR_SCHEMA_NO_FIELD,
        .depends_on = -1,
        .label = "Last message",
        .help = "The last thing the settings screen reported, kept here once "
                "it has left the line below the settings.",
        .read_info = hwr_sysinfo_read_message
    }
};

size_t hwr_schema_count(void)
{
    return HWR_COUNT_OF(HWR_SCHEMA);
}

const struct hwr_setting_desc *hwr_schema_at(size_t index)
{
    if (index >= HWR_COUNT_OF(HWR_SCHEMA))
        return NULL;
    return &HWR_SCHEMA[index];
}

const struct hwr_setting_desc *hwr_schema_find(const char *key)
{
    size_t i;

    if (key == NULL)
        return NULL;
    for (i = 0; i < HWR_COUNT_OF(HWR_SCHEMA); i++) {
        if (HWR_SCHEMA[i].key != NULL &&
            hwr_ascii_equal_ci(key, HWR_SCHEMA[i].key))
            return &HWR_SCHEMA[i];
    }
    return NULL;
}

static void hwr_schema_error(char *err, size_t err_size, const char *format,
                             ...)
{
    va_list args;

    if (err == NULL || err_size == 0)
        return;
    va_start(args, format);
    vsnprintf(err, err_size, format, args);
    va_end(args);
}

static void hwr_schema_enum_list(const struct hwr_setting_desc *desc,
                                 char *out, size_t out_size)
{
    size_t i;
    size_t used = 0;

    if (out == NULL || out_size == 0)
        return;
    out[0] = 0;
    for (i = 0; i < desc->value_count; i++) {
        size_t length = strlen(desc->values[i].token);
        if (used + length + 3 >= out_size) {
            snprintf(out + used, out_size - used, "...");
            return;
        }
        if (used != 0) {
            out[used++] = ',';
            out[used++] = ' ';
        }
        memcpy(out + used, desc->values[i].token, length);
        used += length;
        out[used] = 0;
    }
}

static int hwr_parse_u32(const char *text, int base, uint32_t maximum,
                         uint32_t *value)
{
    char *end;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, base);
    while (*end != 0 && isspace((unsigned char)*end))
        end++;
    if (errno != 0 || end == text || *end != 0 || parsed > maximum)
        return 0;
    *value = (uint32_t)parsed;
    return 1;
}

static int hwr_parse_bool(const char *text, int *value)
{
    if (hwr_ascii_equal_ci(text, "1") || hwr_ascii_equal_ci(text, "true") ||
        hwr_ascii_equal_ci(text, "yes") || hwr_ascii_equal_ci(text, "on")) {
        *value = 1;
        return 1;
    }
    if (hwr_ascii_equal_ci(text, "0") || hwr_ascii_equal_ci(text, "false") ||
        hwr_ascii_equal_ci(text, "no") || hwr_ascii_equal_ci(text, "off")) {
        *value = 0;
        return 1;
    }
    return 0;
}

static int hwr_parse_double(const char *text, double *value)
{
    char *end;
    double parsed;

    errno = 0;
    parsed = strtod(text, &end);
    while (*end != 0 && isspace((unsigned char)*end))
        end++;
    if (errno != 0 || end == text || *end != 0)
        return 0;
    *value = parsed;
    return 1;
}

static void hwr_schema_range_error(const struct hwr_setting_desc *desc,
                                   char *err, size_t err_size)
{
    const char *zero = (desc->flags & HWR_SF_ZERO_IS_AUTO) != 0 ? "0 or " : "";

    if (desc->type == HWR_VT_F32)
        hwr_schema_error(err, err_size, "expected %s%s%g through %g", zero,
                         (desc->flags & HWR_SF_EXCLUSIVE_MIN) != 0
                             ? "more than " : "",
                         (double)desc->min.f, (double)desc->max.f);
    else
        hwr_schema_error(err, err_size, "expected %s%u through %u", zero,
                         desc->min.u, desc->max.u);
}

int hwr_schema_validate(const struct hwr_setting_desc *desc,
                        union hwr_value value, char *err, size_t err_size)
{
    char tokens[256];
    size_t i;

    if (desc == NULL)
        return 0;
    switch (desc->type) {
    case HWR_VT_BOOL:
        return 1;
    case HWR_VT_U32:
    case HWR_VT_COLOR:
        if ((desc->flags & HWR_SF_ZERO_IS_AUTO) != 0 && value.u == 0)
            return 1;
        if (value.u < desc->min.u || value.u > desc->max.u) {
            hwr_schema_range_error(desc, err, err_size);
            return 0;
        }
        return 1;
    case HWR_VT_F32:
        if ((desc->flags & HWR_SF_ZERO_IS_AUTO) != 0 && value.f == 0.0f)
            return 1;
        if (value.f >= desc->min.f && value.f <= desc->max.f &&
            ((desc->flags & HWR_SF_EXCLUSIVE_MIN) == 0 ||
             value.f > desc->min.f))
            return 1;
        hwr_schema_range_error(desc, err, err_size);
        return 0;
    case HWR_VT_ENUM:
        for (i = 0; i < desc->value_count; i++) {
            if (desc->values[i].value == value.e)
                return 1;
        }
        hwr_schema_enum_list(desc, tokens, sizeof tokens);
        hwr_schema_error(err, err_size, "expected one of: %s", tokens);
        return 0;
    }
    return 0;
}

int hwr_schema_validate_all(const struct hwr_settings *settings,
                            char *err, size_t err_size)
{
    size_t i;

    if (settings == NULL)
        return 0;
    for (i = 0; i < HWR_COUNT_OF(HWR_SCHEMA); i++) {
        const struct hwr_setting_desc *desc = &HWR_SCHEMA[i];
        char message[160];

        /* A row with no field of its own holds nothing to check, and a second
         * spelling is checked as the field it writes. */
        if (desc->offset == HWR_SCHEMA_NO_FIELD)
            continue;
        if (hwr_schema_validate(desc, hwr_schema_get(settings, desc), message,
                                sizeof message))
            continue;
        hwr_schema_error(err, err_size, "%s: %s", desc->key, message);
        return 0;
    }
    /* The rendered view never goes narrower than four by three. Focal length
     * follows the target height, so a canvas taller than that silently loses
     * horizontal view instead of gaining it, and nothing downstream puts it
     * back. */
    if (settings->internal_w != 0 && settings->internal_h != 0 &&
        (uint64_t)settings->internal_w * 3u <
            (uint64_t)settings->internal_h * 4u) {
        hwr_schema_error(err, err_size,
                         "the render size may not be narrower than 4:3; %u by %u is",
                         settings->internal_w, settings->internal_h);
        return 0;
    }
    return 1;
}

int hwr_schema_parse(const struct hwr_setting_desc *desc, const char *text,
                     union hwr_value *out, char *err, size_t err_size)
{
    union hwr_value value;
    char tokens[256];
    size_t i;

    if (desc == NULL || text == NULL || out == NULL)
        return 0;
    memset(&value, 0, sizeof value);
    switch (desc->type) {
    case HWR_VT_BOOL:
        if (!hwr_parse_bool(text, &value.b)) {
            hwr_schema_error(err, err_size,
                             "expected 1/0, true/false, yes/no, or on/off");
            return 0;
        }
        break;
    case HWR_VT_U32:
        if (!hwr_parse_u32(text, 10, UINT32_MAX, &value.u)) {
            hwr_schema_error(err, err_size, "expected a whole number");
            return 0;
        }
        break;
    case HWR_VT_COLOR:
        if (!hwr_parse_u32(text, 16, UINT32_MAX, &value.rgb)) {
            hwr_schema_error(err, err_size, "expected a hexadecimal color");
            return 0;
        }
        break;
    case HWR_VT_F32: {
        double parsed;
        if (!hwr_parse_double(text, &parsed)) {
            hwr_schema_error(err, err_size, "expected a number");
            return 0;
        }
        /* Range-checked before the narrowing cast, so a value that only a
         * double can tell from the limit is not rounded into range. */
        if (((desc->flags & HWR_SF_ZERO_IS_AUTO) == 0 || parsed != 0.0) &&
            (parsed < (double)desc->min.f || parsed > (double)desc->max.f)) {
            hwr_schema_range_error(desc, err, err_size);
            return 0;
        }
        value.f = (float)parsed;
        break;
    }
    case HWR_VT_ENUM:
        for (i = 0; i < desc->value_count; i++) {
            if (hwr_ascii_equal_ci(text, desc->values[i].token))
                break;
        }
        if (i == desc->value_count) {
            hwr_schema_enum_list(desc, tokens, sizeof tokens);
            hwr_schema_error(err, err_size, "expected one of: %s", tokens);
            return 0;
        }
        value.e = desc->values[i].value;
        break;
    }
    if (!hwr_schema_validate(desc, value, err, err_size))
        return 0;
    *out = value;
    return 1;
}

int hwr_schema_format(const struct hwr_setting_desc *desc,
                      union hwr_value value, char *out, size_t out_size)
{
    size_t i;
    int written = -1;

    if (desc == NULL || out == NULL || out_size == 0)
        return 0;
    out[0] = 0;
    switch (desc->type) {
    case HWR_VT_BOOL:
        written = snprintf(out, out_size, "%d", value.b != 0 ? 1 : 0);
        break;
    case HWR_VT_U32:
        written = snprintf(out, out_size, "%u", value.u);
        break;
    case HWR_VT_COLOR:
        written = snprintf(out, out_size, "%06x", value.rgb);
        break;
    case HWR_VT_F32: {
        double parsed = 0.0;
        /* The short spelling is the readable one; fall back to the spelling
         * that reproduces the stored value exactly when it does not. */
        written = snprintf(out, out_size, "%.6g", (double)value.f);
        if (written > 0 && (size_t)written < out_size &&
            (!hwr_parse_double(out, &parsed) || (float)parsed != value.f))
            written = snprintf(out, out_size, "%.9g", (double)value.f);
        break;
    }
    case HWR_VT_ENUM:
        for (i = 0; i < desc->value_count; i++) {
            if (desc->values[i].value == value.e) {
                written = snprintf(out, out_size, "%s", desc->values[i].token);
                break;
            }
        }
        break;
    }
    if (written <= 0 || (size_t)written >= out_size) {
        out[0] = 0;
        return 0;
    }
    return 1;
}

static void *hwr_schema_field(struct hwr_settings *settings,
                              const struct hwr_setting_desc *desc)
{
    if (settings == NULL || desc == NULL ||
        desc->offset == HWR_SCHEMA_NO_FIELD)
        return NULL;
    return (char *)settings + desc->offset;
}

union hwr_value hwr_schema_get(const struct hwr_settings *settings,
                               const struct hwr_setting_desc *desc)
{
    union hwr_value value;
    const void *field;
    int32_t raw;

    memset(&value, 0, sizeof value);
    if (settings == NULL || desc == NULL)
        return value;
    if (desc->from_field != NULL)
        return desc->from_field(settings);
    field = hwr_schema_field((struct hwr_settings *)settings, desc);
    if (field == NULL)
        return value;
    switch (desc->type) {
    case HWR_VT_BOOL:
        memcpy(&raw, field, sizeof raw);
        value.b = raw != 0;
        break;
    case HWR_VT_U32:
    case HWR_VT_COLOR:
        memcpy(&value.u, field, sizeof value.u);
        break;
    case HWR_VT_F32:
        memcpy(&value.f, field, sizeof value.f);
        break;
    case HWR_VT_ENUM:
        memcpy(&raw, field, sizeof raw);
        value.e = raw;
        break;
    }
    return value;
}

void hwr_schema_set(struct hwr_settings *settings,
                    const struct hwr_setting_desc *desc,
                    union hwr_value value)
{
    void *field;
    int32_t raw;

    if (settings == NULL || desc == NULL)
        return;
    if (desc->to_field != NULL) {
        desc->to_field(settings, value);
        return;
    }
    field = hwr_schema_field(settings, desc);
    if (field == NULL)
        return;
    switch (desc->type) {
    case HWR_VT_BOOL:
        raw = value.b != 0;
        memcpy(field, &raw, sizeof raw);
        break;
    case HWR_VT_U32:
    case HWR_VT_COLOR:
        memcpy(field, &value.u, sizeof value.u);
        break;
    case HWR_VT_F32:
        memcpy(field, &value.f, sizeof value.f);
        break;
    case HWR_VT_ENUM:
        raw = value.e;
        memcpy(field, &raw, sizeof raw);
        break;
    }
}

/* An alias row is a second spelling of another row's field, so the canonical
 * row is the one a preset writes. */
static void hwr_schema_preset(struct hwr_settings *settings, int original)
{
    size_t i;

    if (settings == NULL)
        return;
    memset(settings, 0, sizeof *settings);
    for (i = 0; i < HWR_COUNT_OF(HWR_SCHEMA); i++) {
        const struct hwr_setting_desc *desc = &HWR_SCHEMA[i];
        if (desc->exposure == HWR_EXP_ALIAS ||
            desc->offset == HWR_SCHEMA_NO_FIELD)
            continue;
        hwr_schema_set(settings, desc,
                       original ? desc->original : desc->def);
    }
}

void hwr_schema_defaults(struct hwr_settings *settings)
{
    hwr_schema_preset(settings, 0);
}

void hwr_schema_original(struct hwr_settings *settings)
{
    hwr_schema_preset(settings, 1);
}

uint32_t hwr_schema_all_change_bits(void)
{
    uint32_t mask = 0;
    size_t i;

    for (i = 0; i < HWR_COUNT_OF(HWR_SCHEMA); i++)
        mask |= HWR_SCHEMA[i].change_bit;
    return mask;
}
