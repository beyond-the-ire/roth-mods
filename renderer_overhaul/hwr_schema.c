#include "hwr_schema.h"

#include "hwr_enh_truecolor.h"
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
_Static_assert(sizeof(enum hwr_truecolor_format) == sizeof(int32_t),
               "enum fields must be four bytes wide");
_Static_assert(sizeof(enum hwr_shading_mode) == sizeof(int32_t),
               "enum fields must be four bytes wide");
_Static_assert(sizeof(enum hwr_shading_dither) == sizeof(int32_t),
               "enum fields must be four bytes wide");
_Static_assert(sizeof(enum hwr_light_scope) == sizeof(int32_t),
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

static const struct hwr_enum_value HWR_TRUECOLOR_FORMAT_VALUES[] = {
    { "auto",    HWR_TRUECOLOR_FORMAT_AUTO,    "Automatic" },
    { "rgba8",   HWR_TRUECOLOR_FORMAT_RGBA8,   "Eight bits per channel" },
    { "rgba16f", HWR_TRUECOLOR_FORMAT_RGBA16F, "Sixteen-bit floating point" }
};

static const struct hwr_enum_value HWR_SHADING_MODE_VALUES[] = {
    { "authentic", HWR_SHADING_AUTHENTIC, "Original" },
    { "smooth",    HWR_SHADING_SMOOTH,    "Smooth" },
    { "modern",    HWR_SHADING_MODERN,    "Modern" }
};

static const struct hwr_enum_value HWR_ROOM_CURVE_VALUES[] = {
    { "legacy",   HWR_ROOM_CURVE_LEGACY,   "One brightness dial" },
    { "anchored", HWR_ROOM_CURVE_ANCHORED, "Separate dark and lit levels" },
    { "engine",   HWR_ROOM_CURVE_ENGINE,   "A room's light runs out" }
};

static const struct hwr_enum_value HWR_ROOM_ANCHOR_VALUES[] = {
    { "auto",   HWR_ROOM_ANCHOR_AUTO,   "Fitted to the map" },
    { "manual", HWR_ROOM_ANCHOR_MANUAL, "The two values below" }
};

static const struct hwr_enum_value HWR_LIGHT_SCOPE_VALUES[] = {
    { "tinted", HWR_LIGHT_SCOPE_TINTED, "Only where the fixtures are out" },
    { "all",    HWR_LIGHT_SCOPE_ALL,    "Everywhere" }
};

static const struct hwr_enum_value HWR_POINT_COLOR_VALUES[] = {
    { "sprite", HWR_POINT_COLOR_SPRITE, "The colour of the thing itself" },
    { "fixed",  HWR_POINT_COLOR_FIXED,  "One colour for all of them" }
};

static const struct hwr_enum_value HWR_SHADING_DITHER_VALUES[] = {
    { "auto", HWR_SHADING_DITHER_AUTO, "Follow the shading" },
    { "off",  HWR_SHADING_DITHER_OFF,  "Off" },
    { "on",   HWR_SHADING_DITHER_ON,   "On" }
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
    {
        /* The true-colour foundation is derived state, not a choice: it is
         * required whenever a dependent enhancement contributes and off
         * otherwise, so it has no consumer row and appears in no menu. This one
         * forces it on with everything neutral, which is how its picture is
         * held against the authentic one. */
        .key = "truecolor_force",
        .type = HWR_VT_BOOL,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, truecolor_force),
        .flags = HWR_SF_RESTART_FREE,
        .def = { .b = 0 },
        .original = { .b = 0 },
        .depends_on = -1,
        .label = "Force true colour",
        .help = "Keep the world image in colour even with nothing asking for "
                "it. With everything neutral the picture is unchanged."
    },
    {
        .key = "truecolor_format",
        .type = HWR_VT_ENUM,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, truecolor_format),
        .flags = HWR_SF_RESTART_FREE,
        .def = { .e = HWR_TRUECOLOR_FORMAT_AUTO },
        .original = { .e = HWR_TRUECOLOR_FORMAT_AUTO },
        .values = HWR_TRUECOLOR_FORMAT_VALUES,
        .value_count = HWR_COUNT_OF(HWR_TRUECOLOR_FORMAT_VALUES),
        /* Deliberately not a dependent: choosing where the colour is kept must
         * never be what brings the colour into existence. */
        .depends_on = -1,
        .available = hwr_truecolor_format_offered,
        .label = "True-colour storage",
        .help = "How the colour world image is stored. Automatic uses eight "
                "bits per channel for the original picture, which is exact for "
                "it, and more precision than the screen has for any other "
                "shading, which is what keeps its darkest levels smooth."
    },
    {
        /* The shading law. Original is the picture the game has always drawn,
         * to the byte; anything else asks for the colour world image and, being
         * an enumeration rather than a switch, clears the neutral early-out --
         * which is exactly what a law that resolves colour between two rows
         * needs. Every later row of this family hangs off this one, so a dial
         * moved while the law is Original changes nothing and asks for
         * nothing. */
        .key = "shading_mode",
        .type = HWR_VT_ENUM,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, shading_mode),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE,
        .def = { .e = HWR_SHADING_AUTHENTIC },
        .original = { .e = HWR_SHADING_AUTHENTIC },
        .values = HWR_SHADING_MODE_VALUES,
        .value_count = HWR_COUNT_OF(HWR_SHADING_MODE_VALUES),
        .depends_on = -1,
        .depends_on_key = "truecolor_force",
        .label = "Shading",
        .help = "How the world's light and distance are shaded. Original steps "
                "through the thirty-two shades the map was authored with, "
                "exactly as the game always has. Smooth follows the same "
                "shades continuously, so a wall or a floor darkens evenly "
                "instead of in bands. Modern lights the world instead: each "
                "room lights its own surfaces as far as the map says its light "
                "carries, and a fill and the light you carry are added to it."
    },
    {
        /* Where the picture is finally quantized into screen colours, and the
         * one place near-black can be kept smooth: an ordered half-level pattern
         * spends the precision the world image was composed at instead of
         * letting a shallow ramp land as flat plateaus. It says something about
         * a screen rather than about a map, so no per-map override reaches it,
         * and it hangs off the shading law: moving it while the picture is the
         * original one changes nothing and asks for nothing. */
        .key = "shading_dither",
        .type = HWR_VT_ENUM,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, shading_dither),
        .flags = HWR_SF_RESTART_FREE,
        .def = { .e = HWR_SHADING_DITHER_AUTO },
        .original = { .e = HWR_SHADING_DITHER_AUTO },
        .values = HWR_SHADING_DITHER_VALUES,
        .value_count = HWR_COUNT_OF(HWR_SHADING_DITHER_VALUES),
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .label = "Shading dither",
        .help = "Whether the world image is dithered where it becomes screen "
                "colours, so that its darkest levels stay smooth instead of "
                "stepping. Automatic follows the shading: the original picture "
                "is left exactly as it is, and smooth shading is dithered."
    },
    {
        /* The foundation's first dependent, and the smallest one: at one it is
         * the neutral early-out and the picture is the authentic picture; at
         * anything else it asks for the foundation and darkens or brightens the
         * world in colour, without the thirty-two-step ladder. */
        .key = "world_gain",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, world_gain),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE,
        .def = { .f = 1.0f },
        .original = { .f = 1.0f },
        .min = { .f = 0.05f },
        .max = { .f = 4.0f },
        .step = { .f = 0.05f },
        .page_step = { .f = 0.25f },
        .depends_on = -1,
        .depends_on_key = "truecolor_force",
        .label = "World gain",
        .help = "Uniform brightness of the world image. One is the original."
    },
    {
        /* The picture dials, and the first enhancements a player is offered.
         * Both are global on purpose: they say what a screen wants, not what a
         * map is, so they carry no HWR_SF_MAP_OVERRIDE and the look file never
         * reaches them. Each is its own dependent of the foundation, so moving
         * one away from one asks for colour and moving both back to one returns
         * the world image to the authentic picture, exactly. */
        .key = "world_brightness",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, world_brightness),
        .flags = HWR_SF_RESTART_FREE,
        .def = { .f = 1.0f },
        .original = { .f = 1.0f },
        .min = { .f = 0.5f },
        .max = { .f = 1.5f },
        .step = { .f = 0.05f },
        .page_step = { .f = 0.25f },
        .depends_on = -1,
        .depends_on_key = "truecolor_force",
        .label = "Brightness",
        .help = "Brightness of the world image. One is the original picture, "
                "and the interface, the inventory and the menus keep theirs "
                "either way."
    },
    {
        .key = "world_contrast",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, world_contrast),
        .flags = HWR_SF_RESTART_FREE,
        .def = { .f = 1.0f },
        .original = { .f = 1.0f },
        .min = { .f = 0.5f },
        .max = { .f = 1.5f },
        .step = { .f = 0.05f },
        .page_step = { .f = 0.25f },
        .depends_on = -1,
        .depends_on_key = "truecolor_force",
        .label = "Contrast",
        .help = "Contrast of the world image, taken around mid grey and after "
                "the brightness. One is the original picture."
    },
    /* The light rig, which only the Modern shading has anything to do with.
     * Every row here names the shading law as its parent rather than the
     * foundation, so a dial moved while the shading is Original neither brings
     * the colour world image into existence nor moves a single byte of the
     * picture -- without that, a whole family of inert tuning dials would each
     * be an accidental trigger. Each also names Modern as the one value of that
     * law it is live at, rather than settling for "anything but Original":
     * Smooth is the original law with its steps taken out and reads none of
     * these, so a rig dial offered under Smooth would be a control that does
     * nothing. All of them may be set per map: how a place is lit belongs to
     * the place. */
    {
        .key = "room_light_gain",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, room_light_gain),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .f = 1.0f },
        .original = { .f = 1.0f },
        .min = { .f = 0.0f },
        .max = { .f = 4.0f },
        .step = { .f = 0.05f },
        .page_step = { .f = 0.25f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Room brightness",
        .help = "How much light a room gives off from the brightness it was "
                "built with. Lower makes the world darker everywhere without "
                "touching what the map says is bright and what is dim."
    },
    {
        /* Which law the row above is a dial ON. It is deliberately the first of
         * the family after the gain: everything below it either shapes the
         * anchored response or is inert under the other one. */
        .key = "room_light_curve",
        .type = HWR_VT_ENUM,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, room_light_curve),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .e = HWR_ROOM_CURVE_ENGINE },
        .original = { .e = HWR_ROOM_CURVE_ENGINE },
        .values = HWR_ROOM_CURVE_VALUES,
        .value_count = HWR_COUNT_OF(HWR_ROOM_CURVE_VALUES),
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "How a room's brightness reads",
        .help = "A map stores each room's brightness as the distance its own "
                "light carries, and the default reads it as exactly that: a "
                "room lights its surfaces until its light runs out, as the "
                "original does. The other two read it as one flat level for the "
                "whole room, either scaled by one dial or placed between two "
                "levels of their own."
    },
    {
        .key = "room_light_reach",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, room_light_reach),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .f = 1.0f },
        .original = { .f = 1.0f },
        /* The floor is a tenth rather than the 0.4 it began at: the dial's
         * whole use is a darker world than the map's, and 0.4 left a person
         * standing on the bottom of the range. A tenth of the map's reach is
         * three shade rows of span -- almost everything black almost at once
         * -- and past any dark anybody has asked for; the response divides by
         * the reach, so the floor stays well clear of nothing. */
        .min = { .f = 0.1f },
        .max = { .f = 2.0f },
        .step = { .f = 0.05f },
        .page_step = { .f = 0.25f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "How far a room's light carries",
        .help = "How far into a room its own light reaches, against the "
                "distance the map authored for it. One is the map exactly as "
                "written; lower is a darker world, and it darkens a dim room "
                "long before it touches a lit one."
    },
    {
        /* How the row above ENDS, and the reason a shortened reach draws a rim.
         *
         * A room's light running out at a finite distance arrives at nothing
         * with a slope and then stops: a straight line, a flat zero, and a
         * corner between them, which is exactly the shape an eye reads as an
         * edge. Above nothing the last of that light decays toward nothing
         * instead of arriving at it.
         *
         * One step is the default and it is the largest height that is INERT
         * at the map read as written: the row law's own ceiling of thirty-one
         * steps leaves a room's quantity a step above nothing and never below
         * it, so at a reach of one the easing is never reached and the
         * original's picture is unmoved to the bit. What this eases is what
         * the reach dial made. */
        .key = "room_light_shoulder",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, room_light_shoulder),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .f = 1.0f },
        .original = { .f = 1.0f },
        .min = { .f = 0.0f },
        .max = { .f = 4.0f },
        .step = { .f = 0.25f },
        .page_step = { .f = 1.0f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "How softly a room's light ends",
        .help = "A room's light has to stop somewhere, and stopping it dead "
                "draws a rim around you at the distance it stops. This fades "
                "the last of it out instead, in shades of the ramp. Zero is "
                "the hard stop."
    },
    {
        /* The two levels the anchored response mixes between, and the five rows
         * below them that say which authored values it mixes over. They are the
         * whole of that response and are read by nothing else, so under the one
         * that ships -- which reads a room's own value as the distance its light
         * carries -- they are off the settings screen and out of the settings
         * file, while a file that already carries one goes on being read and
         * goes on being written back. */
        .key = "room_light_dark",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, room_light_dark),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .f = 0.02f },
        .original = { .f = 0.02f },
        .min = { .f = 0.0f },
        .max = { .f = 1.0f },
        .step = { .f = 0.01f },
        .page_step = { .f = 0.05f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "How dark an unlit room is",
        .help = "What the dimmest rooms of a map give off by themselves. Read "
                "only where a room's brightness is placed between two levels; "
                "the response that ships reads it as a distance instead and "
                "never asks for this."
    },
    {
        .key = "room_light_lit",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, room_light_lit),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .f = 1.0f },
        .original = { .f = 1.0f },
        .min = { .f = 0.0f },
        /* The top of the range is where the whole authored byte range lands
         * under the exponent the other row holds: a sector at 255 is worth
         * (255/128) to the power of 2.2, which is a shade under 4.6. Stopping
         * below that would make the one setting of this family that reproduces
         * the older law exactly -- the whole authored range placed on the whole
         * output range -- impossible to write down. */
        .max = { .f = 5.0f },
        .step = { .f = 0.05f },
        .page_step = { .f = 0.25f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "How bright a lit room is",
        .help = "What the brightest rooms of a map give off by themselves. "
                "Read only where a room's brightness is placed between two "
                "levels; the response that ships reads it as a distance "
                "instead and never asks for this."
    },
    {
        .key = "room_light_shape",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, room_light_shape),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .f = 1.0f },
        .original = { .f = 1.0f },
        .min = { .f = 0.25f },
        .max = { .f = 4.0f },
        .step = { .f = 0.05f },
        .page_step = { .f = 0.25f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "How steep the falloff is",
        .help = "How steeply a room's light runs out over the distance it "
                "carries. One is the map's own falloff. Where a room's "
                "brightness is placed between two levels instead, this is "
                "where the middle of the range sits between them."
    },
    {
        .key = "room_light_anchor",
        .type = HWR_VT_ENUM,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, room_light_anchor),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .e = HWR_ROOM_ANCHOR_AUTO },
        .original = { .e = HWR_ROOM_ANCHOR_AUTO },
        .values = HWR_ROOM_ANCHOR_VALUES,
        .value_count = HWR_COUNT_OF(HWR_ROOM_ANCHOR_VALUES),
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Which brightness counts as dark",
        .help = "Where a room's brightness is placed between two levels, which "
                "values count as a map's dark and lit ends is read from the "
                "map itself. The other setting uses the two values below "
                "instead, for a map this reads wrongly. The response that "
                "ships reads no map this way and counts nothing."
    },
    {
        .key = "room_light_byte_dark",
        .type = HWR_VT_U32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, room_light_byte_dark),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .u = 112u },
        .original = { .u = 112u },
        .min = { .u = 0u },
        .max = { .u = 255u },
        .step = { .u = 1u },
        .page_step = { .u = 8u },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "The map value counted as dark",
        .help = "The brightness a map has to be built at for a room to read as "
                "unlit. Read only where a room's brightness is placed between "
                "two levels and the setting above is not reading the map, and "
                "then exactly as written."
    },
    {
        .key = "room_light_byte_lit",
        .type = HWR_VT_U32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, room_light_byte_lit),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .u = 208u },
        .original = { .u = 208u },
        .min = { .u = 0u },
        .max = { .u = 255u },
        .step = { .u = 1u },
        .page_step = { .u = 8u },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "The map value counted as lit",
        .help = "The brightness a map has to be built at for a room to read as "
                "fully lit. Read only where a room's brightness is placed "
                "between two levels and the setting above is not reading the "
                "map, and then exactly as written."
    },
    {
        .key = "room_light_window_min",
        .type = HWR_VT_U32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, room_light_window_min),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .u = 24u },
        .original = { .u = 24u },
        .min = { .u = 4u },
        .max = { .u = 128u },
        .step = { .u = 1u },
        .page_step = { .u = 8u },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Narrowest range read from a map",
        .help = "Most maps are built almost entirely at one brightness. This "
                "is how far apart the dark and lit ends are held anyway, so a "
                "map like that does not turn a single step into the whole "
                "range. Read only where a room's brightness is placed between "
                "two levels."
    },
    {
        .key = "room_light_window_max",
        .type = HWR_VT_U32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, room_light_window_max),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .u = 96u },
        .original = { .u = 96u },
        .min = { .u = 8u },
        .max = { .u = 255u },
        .step = { .u = 1u },
        .page_step = { .u = 8u },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Widest range read from a map",
        .help = "How far apart the dark and lit ends may be. Keeping them "
                "close is what leaves a light switch or a candle a visible "
                "share of the range on a map built across a wide spread. Read "
                "only where a room's brightness is placed between two levels."
    },
    {
        .key = "room_light_unshaded_gain",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, room_light_unshaded_gain),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .f = 1.0f },
        .original = { .f = 1.0f },
        .min = { .f = 0.0f },
        .max = { .f = 1.0f },
        .step = { .f = 0.05f },
        .page_step = { .f = 0.25f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Surfaces the map never darkens",
        .help = "Outdoor ground and other surfaces a map marks as never to be "
                "darkened stay fully bright however far away they are, which "
                "can glare in a dark world. One leaves them exactly as the map "
                "asks; lower pulls them down into the room's own light with "
                "everything else."
    },
    {
        .key = "room_light_gamma",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, room_light_gamma),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .f = 2.2f },
        .original = { .f = 2.2f },
        .min = { .f = 1.0f },
        .max = { .f = 3.0f },
        .step = { .f = 0.1f },
        .page_step = { .f = 0.5f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Room brightness curve",
        .help = "How steeply a room's built-in brightness turns into light. "
                "The map's values are shades rather than light, and this is "
                "what makes the difference between two nearby ones visible."
    },
    {
        /* The least a surface may be left showing, and it is a statement about
         * the SURFACE rather than about the light landing on it.
         *
         * Stated on the light it was a floor on a MULTIPLIER: a hundredth of
         * the light is a seventh of the display, and then a fraction of that
         * again depending on how dark the texture underneath happened to be --
         * so the number predicted nothing, and a dark texture in a dark room
         * read as black whatever it was set to. Stated on the surface a
         * hundredth is a hundredth of the picture's own brightest, two and a
         * half levels of two hundred and fifty-five, and every step of the row
         * is worth the same two and a half wherever it is taken.
         *
         * And it is a floor rather than a cut, for the reason the ceiling
         * stopped being one: a cut paints everything under it the same value,
         * which is the one thing a dark room must not do to be navigable. */
        .key = "room_light_floor",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, room_light_floor),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .f = 0.0f },
        .original = { .f = 0.0f },
        .min = { .f = 0.0f },
        .max = { .f = 0.25f },
        .step = { .f = 0.01f },
        .page_step = { .f = 0.05f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Never darker than",
        .help = "The least any surface is ever left showing, as a share of the "
                "brightest the picture can be drawn: a hundredth is about two "
                "levels out of two hundred and fifty-five, a tenth is about "
                "twenty-five. It lifts what has no light and leaves what has "
                "alone, so a dark room stays dark and stays readable. Zero is "
                "honest darkness."
    },
    {
        .key = "room_light_ceiling_authentic",
        .type = HWR_VT_BOOL,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings,
                           room_light_ceiling_authentic),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .b = 0 },
        .original = { .b = 0 },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Keep the original's darkness limit",
        .help = "The original stops a bright room's surfaces from ever fading "
                "past a certain shade. Keeping that limit preserves its "
                "authored feel; dropping it is what lets a lit room stand out "
                "against a dark one."
    },
    {
        .key = "world_light_ceiling",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, world_light_ceiling),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .f = 1.0f },
        .original = { .f = 1.0f },
        /* One is where light stops adding up at the picture's own brightest,
         * which is what this row always did and is what it ships at. Below one
         * it is a knee instead: the light past it is eased toward that
         * brightest rather than cut off at it, so two rooms that both overrun
         * stay two rooms. Above one there is nothing to keep -- an eight-bit
         * canvas has no headroom to spend, and every value past the brightest
         * arrives as the same white. */
        .min = { .f = 0.4f },
        .max = { .f = 1.0f },
        .step = { .f = 0.05f },
        .page_step = { .f = 0.1f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Where brightness levels off",
        .help = "Where the light landing on a surface stops adding up. One cuts "
                "it off at the brightest the picture can be drawn; lower eases "
                "it in instead, so two rooms brighter than that still read as "
                "two rooms rather than as one flat white."
    },
    {
        .key = "world_distance_shading",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, world_distance_shading),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .f = 0.0f },
        .original = { .f = 0.0f },
        .min = { .f = 0.0f },
        .max = { .f = 1.0f },
        .step = { .f = 0.05f },
        .page_step = { .f = 0.25f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Distance darkening",
        .help = "How much walking away from something darkens it all by "
                "itself, the way the original does. Zero lets the light run "
                "out on its own instead."
    },
    {
        /* The fill, and the three rows of it, off the settings screen.
         *
         * What it was for -- that what nothing else reaches is still faintly
         * visible -- is what the floor above does now, and does better: a fill
         * adds its own light everywhere, so it lifts a lit room by exactly as
         * much as it lifts a dark corner and washes the whole picture to buy
         * the corner; the floor lifts only what has nothing. Two dials for one
         * job, and this is the one that costs the picture. It stays reachable
         * for the one thing it can still say that the floor cannot -- what
         * COLOUR a darkness is -- and it stays out of the way. */
        .key = "ambient_light",
        .type = HWR_VT_BOOL,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, ambient_light),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .b = 0 },
        .original = { .b = 0 },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Ambient fill",
        .help = "A little light everywhere, so what nothing else reaches is "
                "still faintly visible."
    },
    {
        .key = "ambient_color",
        .type = HWR_VT_COLOR,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, ambient_color),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .rgb = 0x00666666u },
        .original = { .rgb = 0x00666666u },
        .min = { .rgb = 0x00000000u },
        .max = { .rgb = 0x00ffffffu },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Ambient colour",
        .help = "The colour of that fill. A neutral grey by day, something "
                "cooler at night."
    },
    {
        .key = "ambient_intensity",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, ambient_intensity),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .f = 0.0f },
        .original = { .f = 0.0f },
        .min = { .f = 0.0f },
        .max = { .f = 2.0f },
        .step = { .f = 0.05f },
        .page_step = { .f = 0.25f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Ambient strength",
        .help = "How much of that fill there is."
    },
    {
        /* The one row of this family whose default is not its Original.
         *
         * The original's own candle is a pool painted on the SCREEN, and the
         * room response that ships leaves it out on purpose: it reads a room's
         * value as the distance that room's light carries, evaluated on depth
         * alone. So a world lit that way has nothing at the eye until this is
         * on, and what a player selecting Modern gets by touching nothing
         * should be a lit world rather than one waiting for a second setting.
         *
         * Original still holds it off, which is what keeps that preset the
         * original picture -- the same split the render size ships with. */
        .key = "player_light",
        .type = HWR_VT_BOOL,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, player_light),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .b = 1 },
        .original = { .b = 0 },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Carried light",
        .help = "A real light where you are standing, in place of the "
                "original's pool on the screen. Surfaces facing you catch it "
                "and surfaces turned away do not."
    },
    {
        .key = "player_light_color",
        .type = HWR_VT_COLOR,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, player_light_color),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .rgb = 0x00ffede0u },
        .original = { .rgb = 0x00ffede0u },
        .min = { .rgb = 0x00000000u },
        .max = { .rgb = 0x00ffffffu },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Carried light colour",
        .help = "The colour of the light you carry. A warm white reads as a "
                "flame."
    },
    {
        .key = "player_light_intensity",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, player_light_intensity),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        /* What the light above is worth as it ships, and it is deliberately a
         * long way under what a light can be set to.
         *
         * A room read as the distance its own light carries is already drawn at
         * the brightest the picture HAS wherever that light still reaches --
         * the world image multiplies the map's own unshaded colour, so there is
         * no headroom above it anywhere near the player. A carried light strong
         * enough to be felt there would therefore not brighten the near field,
         * it would FLATTEN it: two rooms the map states differently, painted
         * the same white. This is the strongest value that paints nothing alike
         * that the room's own light separates by an amount an eye reads, and it
         * is where the light is worth having anyway -- in the dark, where the
         * room's own has run out. Original holds it at nothing. */
        .def = { .f = 0.15f },
        .original = { .f = 0.0f },
        .min = { .f = 0.0f },
        .max = { .f = 2.0f },
        .step = { .f = 0.05f },
        .page_step = { .f = 0.25f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Carried light strength",
        .help = "How bright the light you carry is. It shows most where a "
                "room's own light has run out; near you the picture is already "
                "as bright as it can be drawn."
    },
    {
        .key = "player_light_range",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, player_light_range),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        /* Nothing is the map's own answer rather than no reach at all.
         *
         * Every map states the shade level its candle-lit surfaces are drawn
         * at, and that level IS a distance: it says how many world units a
         * candle-lit room spends per shade row, so the range where such a room
         * has spent the whole ramp is the reach the map itself authored for the
         * light in it. One number, read off the map, in the units the map is
         * built in -- which no single value written here can be on every map.
         * A value written stands exactly as written, for the player who wants a
         * tighter pool than the map asks for. */
        .def = { .f = 0.0f },
        .original = { .f = 0.0f },
        .min = { .f = 0.0f },
        .max = { .f = 4000.0f },
        .step = { .f = 50.0f },
        .page_step = { .f = 200.0f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Carried light reach",
        .help = "How far the light you carry reaches before it runs out, in "
                "world units. Zero follows the reach the map itself authored "
                "for its own candlelight. Smaller is a tighter pool around you."
    },
    {
        /* Where that light is CARRIED, and it is the original's own geometry
         * rather than a liberty taken with it.
         *
         * The original's candle is measured on the screen, from an origin that
         * dances about the middle of the view, and the screen arm it measures
         * is an angle off the view axis: the row law adds 1024*tan(theta) to
         * the depth, so the candle is worth its whole amplitude straight ahead
         * and nothing at all past tan(theta) = C*unit/1024 -- a cone about the
         * direction the player is LOOKING, of half angle fourteen degrees, and
         * the same fourteen on every map the game ships, because C*unit is 256
         * at every shade level of it. A point of light sitting on top of the
         * player was the simplification; forward is what the original says.
         *
         * What moves is what the pool falls off FROM, not where the light is
         * held: a surface the player is facing is lit by the thing in his
         * hand, and treating the aim as a position would light everything past
         * it from behind and leave the wall two paces ahead of him dark.
         *
         * The default is the furthest aim that still leaves the ground at his
         * own feet with four fifths of the light a pool at the eye gives it
         * (0.660 against 0.788) while carrying half again as much onto the
         * wall he is walking toward (0.562 against 0.360 at five hundred
         * units). Original holds the same value: the light this shapes does
         * not exist at the Original at all, and where it does exist the
         * original's own candle is aimed. */
        .key = "player_light_forward",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, player_light_forward),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .f = 0.15f },
        .original = { .f = 0.15f },
        .min = { .f = 0.0f },
        .max = { .f = 0.5f },
        .step = { .f = 0.05f },
        .page_step = { .f = 0.10f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Carried light aims ahead",
        .help = "How far in front of you the light you carry sits, as a share "
                "of how far it reaches. The game's own candlelight is aimed "
                "where you are looking rather than pooled at your feet. Zero "
                "puts it back on top of you."
    },
    {
        .key = "player_light_scope",
        .type = HWR_VT_ENUM,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, player_light_scope),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .e = HWR_LIGHT_SCOPE_ALL },
        .original = { .e = HWR_LIGHT_SCOPE_ALL },
        .values = HWR_LIGHT_SCOPE_VALUES,
        .value_count = HWR_COUNT_OF(HWR_LIGHT_SCOPE_VALUES),
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Carried light reaches",
        .help = "A room whose own lamps and candles are out is marked as such "
                "while they are out, and the game clears that mark the moment "
                "someone turns the lights on. The light you carry is yours, so "
                "it lights everywhere you can see; the other setting confines "
                "it to the rooms left dark, which is dimmer in exactly the "
                "rooms that were just lit."
    },
    {
        .key = "player_light_glow_scale",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, player_light_glow_scale),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .f = 0.0f },
        .original = { .f = 0.0f },
        .min = { .f = 0.0f },
        .max = { .f = 2.0f },
        .step = { .f = 0.05f },
        .page_step = { .f = 0.25f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Follow the map's lantern",
        .help = "Every map says how strong its own lantern was meant to be. "
                "This is how much notice the light you carry takes of that: "
                "none leaves your own strength and reach alone, and the "
                "brighter a map asks for, the further its light throws."
    },
    {
        .key = "player_light_candle",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, player_light_candle),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        /* Full is the map read exactly as written, and that is where it rests.
         *
         * The game draws a candle-lit room at its own depth law less a fixed
         * number of shade rows, and those rows are the CANDLE: the amount is
         * staged from the view record and from whether the player is standing
         * in such a room at all, and it is the same word the wall and the flat
         * edge projectors both subtract. Handing it to the room instead made
         * every candle-lit room paint its own near field at the candle's
         * brightness whether or not there was a candle in it -- the one thing
         * no dial could reach, because the room term had no dial that WAS the
         * candle. At full this supplies exactly the rows the map states and the
         * picture is the original's; below it a candle-lit room keeps only what
         * it states for itself, which is the darkness this dial is for. */
        .def = { .f = 1.0f },
        .original = { .f = 1.0f },
        .min = { .f = 0.0f },
        .max = { .f = 2.0f },
        .step = { .f = 0.05f },
        .page_step = { .f = 0.25f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Your light lights candlelit rooms",
        .help = "Rooms lit by candlelight are lit by YOUR light in this game: "
                "the room says how far its own light carries and the candle "
                "you hold supplies the rest. This is how much of that your "
                "light gives. Full is the game's own candle. Turn it down and "
                "candlelit rooms go dark around you, leaving only the light "
                "the room itself has."
    },
    {
        .key = "player_light_candle_pool",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, player_light_candle_pool),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        /* Nothing is the game's own shape.
         *
         * Its candle is measured in SCREEN space -- the distance from a dancing
         * origin near the middle of the view -- so at the middle of the view it
         * is worth the same at every distance, and it FILLS a candle-lit room
         * rather than pooling in it. At nothing this is that, exactly, which is
         * what keeps the shipped picture the original's; wound up, the rows
         * above fall off with the light's own reach and with which way a surface
         * faces, which is a real pool at the eye and a picture the original
         * could not draw. */
        .def = { .f = 0.0f },
        .original = { .f = 0.0f },
        .min = { .f = 0.0f },
        .max = { .f = 1.0f },
        .step = { .f = 0.05f },
        .page_step = { .f = 0.25f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Pool your light instead of filling the room",
        .help = "The game's candlelight brightens a whole candlelit room "
                "evenly, however far away the wall is. This gathers it into a "
                "pool around you instead: bright at your feet, gone by the "
                "time your light runs out, and dimmer on surfaces facing away."
    },
    {
        .key = "player_light_flicker",
        .type = HWR_VT_BOOL,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, player_light_flicker),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .b = 0 },
        .original = { .b = 0 },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Carried light flickers",
        .help = "Lets the light you carry breathe the way a flame does. It is "
                "held well clear of going out, so it never gutters or strobes."
    },
    {
        .key = "player_light_flicker_amount",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, player_light_flicker_amount),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .f = 0.30f },
        .original = { .f = 0.30f },
        .min = { .f = 0.0f },
        .max = { .f = 1.0f },
        .step = { .f = 0.05f },
        .page_step = { .f = 0.25f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Flicker depth",
        .help = "How far the flame swings either side of the strength you "
                "set. It scales the whole of the flame's motion, and at "
                "nothing the flame is perfectly still."
    },
    {
        .key = "player_light_flicker_speed",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, player_light_flicker_speed),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        /* One is the flame's own pace now that the dial commands the whole of
         * its motion; the 1.8 it shipped at belongs to the older reading,
         * where the dial ran only the two small waves and the flame's real
         * movement -- the retargets -- ran at one fixed rate whatever it
         * said. */
        .def = { .f = 1.00f },
        .original = { .f = 1.00f },
        .min = { .f = 0.1f },
        .max = { .f = 5.0f },
        .step = { .f = 0.1f },
        .page_step = { .f = 0.5f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Flicker speed",
        .help = "How quickly the whole flame moves: the pace it wanders to "
                "new strengths, and how briskly it settles on each."
    },
    {
        .key = "sprite_light_gain",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, sprite_light_gain),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .f = 1.0f },
        .original = { .f = 1.0f },
        .min = { .f = 0.0f },
        .max = { .f = 4.0f },
        .step = { .f = 0.05f },
        .page_step = { .f = 0.25f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Item and figure brightness",
        .help = "How bright people, creatures and things you can pick up are: "
                "both the light they show of their own and the light you carry "
                "landing on them. Raise it if a dark room makes them hard to "
                "find."
    },
    {
        .key = "sprite_light_wrap",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, sprite_light_wrap),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .f = 0.5f },
        .original = { .f = 0.5f },
        .min = { .f = 0.0f },
        .max = { .f = 1.0f },
        .step = { .f = 0.05f },
        .page_step = { .f = 0.25f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Item and figure softness",
        .help = "How far light carries around a figure past the edge it "
                "would stop at, so the side away from you is shaded rather "
                "than black."
    },
    /* The lights that are not at the eye.
     *
     * Which things are lights is the map's own answer, not a list kept here:
     * the game already marks the torches, candles and lit sigils as things
     * never to be darkened, and that mark is what these read. So the family
     * needs no per-map table to work at all -- and the look file has one
     * anyway, for the map that wants a particular brazier brighter or a
     * fullbright decal left alone.
     *
     * Every row is a MAP_OVERRIDE and hangs off the shading law, like the rest
     * of the rig: moving any of them while the picture is the original one
     * changes nothing and asks for nothing. */
    {
        .key = "point_light",
        .type = HWR_VT_BOOL,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, point_light),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .b = 1 },
        .original = { .b = 1 },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Torches and candles give light",
        .help = "Lets the torches, candles and lit sigils a map places "
                "actually light what is around them. They cast no shadows, so "
                "one beside a doorway spills a little onto the far side of it."
    },
    {
        .key = "point_light_objects",
        .type = HWR_VT_BOOL,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, point_light_objects),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .b = 1 },
        .original = { .b = 1 },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "Light from things the map marks",
        .help = "Where those lights come from: the things a map marks as "
                "giving light of their own. Turning this off leaves the "
                "setting above on with nothing seeding it."
    },
    {
        .key = "point_light_max",
        .type = HWR_VT_U32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, point_light_max),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .u = 8u },
        .original = { .u = 8u },
        .min = { .u = 1u },
        .max = { .u = 16u },
        .step = { .u = 1u },
        .page_step = { .u = 4u },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "How many light at once",
        .help = "How many of them may light the picture at the same time. "
                "What is dropped is always whatever is contributing least "
                "where you are standing, and it fades out rather than "
                "vanishing."
    },
    {
        .key = "point_light_intensity",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, point_light_intensity),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .f = 1.0f },
        .original = { .f = 1.0f },
        .min = { .f = 0.0f },
        .max = { .f = 4.0f },
        .step = { .f = 0.05f },
        .page_step = { .f = 0.25f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "How bright a torch is",
        .help = "How much light one of them gives off."
    },
    {
        .key = "point_light_range",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, point_light_range),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .f = 450.0f },
        .original = { .f = 450.0f },
        .min = { .f = 100.0f },
        .max = { .f = 2000.0f },
        .step = { .f = 25.0f },
        .page_step = { .f = 100.0f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "How far a torch reaches",
        .help = "Where the pool around one of them ends, in world units. A "
                "fixed torch reads tighter than the light you carry, and a "
                "shorter reach is also less of it spilling where it should "
                "not."
    },
    {
        .key = "point_light_color_source",
        .type = HWR_VT_ENUM,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, point_light_color_source),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .e = HWR_POINT_COLOR_SPRITE },
        .original = { .e = HWR_POINT_COLOR_SPRITE },
        .values = HWR_POINT_COLOR_VALUES,
        .value_count = HWR_COUNT_OF(HWR_POINT_COLOR_VALUES),
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "What colour a torch throws",
        .help = "Reading it from the thing itself means fire throws "
                "fire-coloured light and a cold sigil throws cold light. One "
                "chosen colour is more uniform and more controllable."
    },
    {
        .key = "point_light_color",
        .type = HWR_VT_COLOR,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_CONSUMER,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, point_light_color),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .rgb = 0x00ffb46eu },
        .original = { .rgb = 0x00ffb46eu },
        .min = { .rgb = 0x00000000u },
        .max = { .rgb = 0x00ffffffu },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "The one torch colour",
        .help = "The colour used when the setting above is not reading it "
                "from the thing itself. Warmer than the light you carry, "
                "because a wall torch is fire and a carried lamp is not."
    },
    {
        .key = "point_light_glow_scale",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, point_light_glow_scale),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .f = 0.0f },
        .original = { .f = 0.0f },
        .min = { .f = 0.0f },
        .max = { .f = 2.0f },
        .step = { .f = 0.05f },
        .page_step = { .f = 0.25f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "How much the map's own word counts",
        .help = "Every map carries a word saying how strong its own lantern "
                "was meant to be. This is how much notice these lights take "
                "of it, exactly as the light you carry takes it."
    },
    {
        .key = "point_light_hops",
        .type = HWR_VT_U32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, point_light_hops),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .u = 1u },
        .original = { .u = 1u },
        .min = { .u = 0u },
        .max = { .u = 3u },
        .step = { .u = 1u },
        .page_step = { .u = 1u },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "How far away a light may be",
        .help = "How many doorways from the room you are in a light may still "
                "be lighting it. A light normally goes on lighting its own "
                "room for as long as that room is on screen whatever this "
                "says; it is the limit that comes into force only when the "
                "renderer cannot tell which rooms are being drawn."
    },
    {
        .key = "point_light_z_slack",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, point_light_z_slack),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .f = 64.0f },
        .original = { .f = 64.0f },
        .min = { .f = 0.0f },
        .max = { .f = 512.0f },
        .step = { .f = 8.0f },
        .page_step = { .f = 32.0f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "How far above and below a room",
        .help = "How far past its own room's floor and ceiling a light may "
                "still reach, in world units, before it fades out. It is what "
                "stops a torch lighting the storey above it."
    },
    {
        .key = "point_light_fade",
        .type = HWR_VT_F32,
        .category = HWR_CAT_ENHANCEMENTS,
        .exposure = HWR_EXP_ADVANCED,
        .apply_kind = HWR_APPLY_HOOK,
        .change_bit = HWR_CHANGE_TRUECOLOR,
        .offset = offsetof(struct hwr_settings, point_light_fade),
        .flags = HWR_SF_MAP_OVERRIDE | HWR_SF_RESTART_FREE |
                 HWR_SF_DEPENDS_ON_VALUE,
        .def = { .f = 0.25f },
        .original = { .f = 0.25f },
        .min = { .f = 0.0f },
        .max = { .f = 1.0f },
        .step = { .f = 0.05f },
        .page_step = { .f = 0.25f },
        .depends_on = -1,
        .depends_on_key = "shading_mode",
        .depends_on_value = { .e = HWR_SHADING_MODERN },
        .label = "How gently one comes and goes",
        .help = "How long, in seconds, a light takes to come up or go out "
                "when there is no longer room for it. Nothing appears or "
                "disappears in a single frame."
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
        .label = "World image storage in use",
        .help = "How the world image is being kept, which is what the storage "
                "setting's Automatic became. None means the world is drawn in "
                "the original palette, which is what the original picture "
                "needs.",
        .read_info = hwr_sysinfo_read_color_format
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

/* The bookkeeping that walks this table by position -- which keys a file
 * carried, which rows a map overrides, which rows a panel has already drawn --
 * holds one row per position in a set of a fixed width. The table is the only
 * thing that knows how long it is, so the table is where the two are held
 * together: a row added past the end of that width stops the build here rather
 * than falling outside every one of those sets in silence. */
_Static_assert(HWR_COUNT_OF(HWR_SCHEMA) <= HWR_ROW_SET_MAX,
               "the settings table has outgrown the row set that indexes it");

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

int hwr_schema_index_of(const struct hwr_setting_desc *desc)
{
    size_t i;

    if (desc == NULL)
        return -1;
    for (i = 0; i < HWR_COUNT_OF(HWR_SCHEMA); i++) {
        if (&HWR_SCHEMA[i] == desc)
            return (int)i;
    }
    return -1;
}

int hwr_schema_depends_on(const struct hwr_setting_desc *desc)
{
    if (desc == NULL)
        return -1;
    if (desc->depends_on_key != NULL)
        return hwr_schema_index_of(hwr_schema_find(desc->depends_on_key));
    return desc->depends_on;
}

int hwr_schema_value_same(const struct hwr_setting_desc *desc,
                          union hwr_value a, union hwr_value b)
{
    if (desc == NULL)
        return 1;
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
    return 1;
}

int hwr_schema_dependency_met(const struct hwr_setting_desc *desc,
                              const struct hwr_settings *settings)
{
    const struct hwr_setting_desc *parent;
    union hwr_value value;
    int index;

    if (desc == NULL || settings == NULL)
        return 1;
    index = hwr_schema_depends_on(desc);
    if (index < 0)
        return 1;
    parent = hwr_schema_at((size_t)index);
    if (parent == NULL)
        return 1;
    value = hwr_schema_get(settings, parent);
    if ((desc->flags & HWR_SF_DEPENDS_ON_VALUE) != 0)
        return hwr_schema_value_same(parent, value, desc->depends_on_value);
    /* A parent with two states is met by not being in its first one, which is
     * the off of a switch and the Original of everything else that has one. */
    switch (parent->type) {
    case HWR_VT_BOOL:
        return value.b != 0;
    case HWR_VT_U32:
    case HWR_VT_COLOR:
        return value.u != 0u;
    case HWR_VT_F32:
        return value.f != 0.0f;
    case HWR_VT_ENUM:
        return value.e != 0;
    }
    return 1;
}

int hwr_schema_is_original(const struct hwr_setting_desc *desc,
                           union hwr_value value)
{
    if (desc == NULL)
        return 1;
    return hwr_schema_value_same(desc, value, desc->original);
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

    /* A colour's range is six hexadecimal digits, and saying so in decimal
     * would be an answer nobody can act on. */
    if (desc->type == HWR_VT_COLOR)
        hwr_schema_error(err, err_size,
                         "expected a colour as six hexadecimal digits, such as "
                         "806040");
    else if (desc->type == HWR_VT_F32)
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
        /* A colour is written back as bare hexadecimal, and that is the
         * spelling a configuration file has to use -- a hash in one starts a
         * comment. The hash is still accepted here, because a value that
         * reaches this from anywhere else is written the way a person writes a
         * colour. */
        if (text[0] == '#')
            text++;
        if (!hwr_parse_u32(text, 16, UINT32_MAX, &value.rgb)) {
            hwr_schema_error(err, err_size,
                             "expected a colour as six hexadecimal digits, "
                             "such as 806040");
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
