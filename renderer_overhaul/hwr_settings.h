#ifndef HWR_SETTINGS_H
#define HWR_SETTINGS_H

#include <stdint.h>

enum hwr_window_mode {
    HWR_WINDOW_DEFAULT = 0,
    HWR_WINDOW_WINDOWED,
    HWR_WINDOW_BORDERLESS
};

enum hwr_present_mode {
    HWR_PRESENT_FIFO = 0,
    HWR_PRESENT_IMMEDIATE,
    HWR_PRESENT_ADAPTIVE
};

enum hwr_pitch_mode {
    HWR_PITCH_HORIZON_SHIFT = 0,
    HWR_PITCH_TRUE_ROTATION
};

/* Storage the true-colour world image is kept in. Eight bits per channel is
 * exact for the authentic picture and is what auto selects for it; a shading law
 * that resolves colour continuously is composed at more precision than the
 * eight-bit canvas it is finally quantized into, so auto selects the
 * floating-point format for one where the driver offers it. */
enum hwr_truecolor_format {
    HWR_TRUECOLOR_FORMAT_AUTO = 0,
    HWR_TRUECOLOR_FORMAT_RGBA8,
    HWR_TRUECOLOR_FORMAT_RGBA16F
};

/* Which law the world's shading is resolved with. Authentic is the original
 * picture, byte for byte: the shade quantity is floored to one of the thirty-two
 * colormap rows the map authored, exactly as it always has been. Smooth keeps
 * the same quantity continuous and resolves the colour between the two rows it
 * falls between, so the authored ramp is followed without the steps. Modern
 * stops walking the ramp altogether: the sector's authored brightness becomes
 * light the surface gives off, a fill and the light the player carries are added
 * to it, and distance no longer darkens anything by itself. */
enum hwr_shading_mode {
    HWR_SHADING_AUTHENTIC = 0,
    HWR_SHADING_SMOOTH,
    HWR_SHADING_MODERN
};

/* Which faces the light the player carries reaches. Everything it can see is
 * the answer, because a light somebody is holding does not consult the room.
 *
 * The other value confines it to the faces carrying the candle-tinted mark, and
 * that mark is worth being exact about: it is not the author's word about where
 * a carried light belongs, it is the engine's LIVE word about whether a room's
 * own fixtures are out. A light switch clears it across the room it raises and
 * sets it across the room it lowers, so confining the carried light to it puts
 * that light out in precisely the rooms that have just been lit. The value stays
 * available for anyone who wants the original's own scoping; it is neither the
 * default nor the faithful reading. */
enum hwr_light_scope {
    HWR_LIGHT_SCOPE_TINTED = 0,
    HWR_LIGHT_SCOPE_ALL
};

/* Whether the world image is dithered where it is finally turned into screen
 * colours. Near-black is where eight bits run out: a ramp too shallow to change
 * a level from one pixel to the next lands as flat plateaus with a visible step
 * between them, and an ordered pattern of half a level spends the precision the
 * image was composed at on breaking those plateaus up instead. Automatic follows
 * the shading law -- the original picture is a palette picture and is left
 * exactly alone, a continuous law is dithered. */
enum hwr_shading_dither {
    HWR_SHADING_DITHER_AUTO = 0,
    HWR_SHADING_DITHER_OFF,
    HWR_SHADING_DITHER_ON
};

struct hwr_settings {
    int enabled;
    int debug;
    uint32_t internal_w;
    uint32_t internal_h;
    enum hwr_window_mode window_mode;
    enum hwr_present_mode present_mode;
    uint32_t min_present_interval_us;

    /* Distance-shading calibration multiplier applied to the view depth. */
    float shade_depth_scale;

    float fov_deg;

    /* Reserved for a later camera mode. */
    enum hwr_pitch_mode pitch_mode;

    /* Set-1 make scancode of the key that opens the settings overlay. */
    uint32_t overlay_key;

    /* The true-colour foundation. It is not a setting anybody chooses: it is
     * required whenever a dependent enhancement contributes, and this switch
     * only forces it on so that its picture can be verified against the
     * authentic one. */
    int truecolor_force;
    enum hwr_truecolor_format truecolor_format;
    /* The shading law, and the first enhancement that changes what the world
     * image is rather than how bright it is. Authentic asks for nothing and is
     * the original picture; every other value is a dependent of the foundation
     * and clears its neutral early-out. */
    enum hwr_shading_mode shading_mode;
    /* Where the world image is dithered. It hangs off the shading law rather
     * than off the foundation: it says what a screen needs of a picture that is
     * already departing, and it never brings a departure into existence. */
    enum hwr_shading_dither shading_dither;
    /* Uniform gain on the world image, and the foundation's first dependent.
     * One means authentic and takes the neutral early-out. */
    float world_gain;
    /* The picture dials: brightness multiplies the lit colour, contrast pivots
     * what is left around mid grey. They are one global choice about a screen
     * rather than a property of a map, so no per-map override reaches them, and
     * both at one is the authentic picture exactly. */
    float world_brightness;
    float world_contrast;

    /* The light rig the modern law composes with. Every one of these hangs off
     * the shading law rather than off the foundation, so moving any of them
     * while the law is the original one changes nothing and asks for nothing --
     * which is what keeps a whole family of inert tuning dials from being an
     * accidental trigger. All of them are per-map overridable: what a room is
     * lit like is a property of the map, unlike the picture dials above. */
    /* The authored sector byte as the surface's own illumination: the gain
     * scales it, the exponent is what makes the map's common brightness steps
     * visible at all, and the floor is how dark a surface is allowed to get
     * where nothing reaches it. */
    float room_light_gain;
    float room_light_gamma;
    float room_light_floor;
    /* The original caps how dark a bright sector's surfaces may get, purely
     * because it had thirty-two steps to spend. Dropping that cap is what lets
     * a lit room read as lit beside a dark one; this puts it back. */
    int room_light_ceiling_authentic;
    /* Where the light total clips. Above one is headroom a floating-point world
     * image can hold and an eight-bit one cannot. */
    float world_light_ceiling;
    /* How much of the original's distance darkening survives. Zero is none:
     * distance does not darken a surface, light runs out. */
    float world_distance_shading;
    /* The fill: one colour everywhere, so nothing the other lights miss is
     * absolutely black unless it is meant to be. */
    int ambient_light;
    uint32_t ambient_color;
    float ambient_intensity;
    /* The light the player carries, at the eye. It replaces the original's
     * screen-space pool with a real one: the surface's own facing decides how
     * much of it lands, and it ends where the range says. */
    int player_light;
    uint32_t player_light_color;
    float player_light_intensity;
    float player_light_range;
    enum hwr_light_scope player_light_scope;
    /* Every map carries a word saying how strong its own lantern was meant to
     * be, which the game itself never looks at. This is how much notice to take
     * of it: at nothing the two rows above are the whole answer, at one the
     * map's word is read whole. */
    float player_light_glow_scale;
    /* The flame in that light: whether it moves at all, how far it may swing
     * either side of its strength, and how quickly its wave runs. It is held
     * well clear of going out, so it reads as a flame rather than a fault. */
    int player_light_flicker;
    float player_light_flicker_amount;
    float player_light_flicker_speed;
    /* Props and figures keep their own brightness dial and their own shaded
     * half, so a dark room never makes what is standing in it unfindable. */
    float sprite_light_gain;
    float sprite_light_wrap;
};

enum hwr_settings_change {
    HWR_CHANGE_ENABLED          = 1u << 0,
    HWR_CHANGE_INTERNAL_SIZE    = 1u << 2,
    HWR_CHANGE_WINDOW_MODE      = 1u << 3,
    HWR_CHANGE_PRESENT_MODE     = 1u << 4,
    HWR_CHANGE_PRESENT_INTERVAL = 1u << 5,
    HWR_CHANGE_FOV              = 1u << 6,
    HWR_CHANGE_PITCH_MODE       = 1u << 7,
    HWR_CHANGE_DEBUG            = 1u << 9,
    HWR_CHANGE_SHADE_SCALE      = 1u << 10,
    HWR_CHANGE_TRUECOLOR        = 1u << 11
};

typedef void (*hwr_settings_apply_fn)(uint32_t mask,
                                      const struct hwr_settings *settings,
                                      void *userdata);

void hwr_settings_defaults(struct hwr_settings *settings);
void hwr_settings_init_defaults(void);
const struct hwr_settings *hwr_settings_get(void);
void hwr_settings_replace(const struct hwr_settings *settings);
int hwr_settings_register_apply(hwr_settings_apply_fn apply, void *userdata);
void hwr_settings_apply_pending(void);
/* Raises change bits without a value in the store having moved. The look layer
 * needs it: a map's overrides change what a row is worth without changing the
 * global value the store keeps. */
void hwr_settings_mark_changed(uint32_t bits);

void hwr_settings_set_enabled(int enabled);
void hwr_settings_set_debug(int debug);
void hwr_settings_set_shade_depth_scale(float scale);
void hwr_settings_set_internal_size(uint32_t width, uint32_t height);
void hwr_settings_set_window_mode(enum hwr_window_mode mode);
void hwr_settings_set_present_mode(enum hwr_present_mode mode);
void hwr_settings_set_min_present_interval_us(uint32_t interval_us);
void hwr_settings_set_fov_deg(float fov_deg);
void hwr_settings_set_pitch_mode(enum hwr_pitch_mode mode);
void hwr_settings_set_overlay_key(uint32_t scancode);

#endif
