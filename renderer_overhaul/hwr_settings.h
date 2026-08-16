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

/* How a room's authored brightness turns into light.
 *
 * The first law is one multiplicative gain on the whole authored range. It
 * cannot separate "how dark is a room with its lights off" from "how bright is
 * one with them on", because it scales both by the same factor: the RATIO
 * between two sectors survives, and the DIFFERENCE -- which is the thing an eye
 * reads -- shrinks with the darkness. A world dark enough to want a lamp is one
 * where nothing a light switch or a candle does to a map's own brightness is
 * visible any more.
 *
 * The second normalizes the authored byte between two anchors taken from the
 * map's own sectors and places the result between two output levels, so the two
 * questions are two dials and the map's brightness steps keep their share of the
 * range whatever darkness the world is tuned to. The first stays as it was, to
 * the bit, for anyone who wants the picture they already tuned.
 *
 * Both of those, though, are functions of the SECTOR: every surface of a room
 * comes out at one brightness, so the far end of a corridor is painted exactly
 * as brightly as the wall in front of the player and no dial can separate them.
 * And the value they read was never a brightness. The game stores each room's
 * number as a REACH -- it says how far you can see into that room before it goes
 * black, and at the player's own feet nearly every room of a map is drawn at the
 * same brightness whatever its number is.
 *
 * The third law reads it as that. The room's brightness becomes the game's own
 * shading at the surface's own distance, read through the curve its colour
 * tables are built on, so a room's light RUNS OUT over the distance the map
 * authored for it. With every dial at its own default that is the original
 * picture; turning the reach down darkens the dim rooms long before it touches
 * the lit ones, which is the one thing the other two cannot do. */
enum hwr_room_curve {
    HWR_ROOM_CURVE_LEGACY = 0,
    HWR_ROOM_CURVE_ANCHORED,
    HWR_ROOM_CURVE_ENGINE
};

/* Where the two byte anchors come from: the map's own sectors, or two numbers
 * somebody wrote down. Fitting them is the honest default -- one pair of numbers
 * cannot describe a cellar and a courtyard -- and the written pair is the escape
 * hatch for a map the fit reads wrongly. */
enum hwr_room_anchor {
    HWR_ROOM_ANCHOR_AUTO = 0,
    HWR_ROOM_ANCHOR_MANUAL
};

/* The quality tier of the sun and moon light's shadow map: how finely it
 * resolves and what the soft edge may cost. Low is the depth map at its
 * small size with hard edges; Medium is the size where window bars resolve;
 * High is twice that with the wide softness ring. There is no shadowless
 * beam: the depth map is the beam's only containment, so the tier below Low
 * is the light's own switch. What CASTS is not a question any row asks:
 * texture cut-outs are the only law -- a grate's holes are holes in the
 * shadow itself, and the translucent range filters the beam by the same
 * per-texel rule the eye-ray pass reads off the map's own blend tables. */
enum hwr_sun_quality {
    HWR_SUN_QUALITY_LOW = 0,
    HWR_SUN_QUALITY_MEDIUM,
    HWR_SUN_QUALITY_HIGH
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
    /* Which of the files in the game configs folder describes this game's
     * maps, as a place in the list the folder scan builds. Nought is the first
     * of them and always means none, whatever the folder holds. The place is a
     * within-a-session number and never leaves the store: the settings file
     * carries the file's NAME, so files may be added to or taken out of the
     * folder without renumbering anybody's selection. */
    int32_t game_config;
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
    /* Which law the authored byte turns into light through, and the two output
     * levels the second one places it between: what a sector at the unlit anchor
     * gives off, and what one at the lit anchor does. Those two are the darkness
     * dial and the "the lights work" dial, and neither touches the other. The
     * gain above survives as the master scale over both. */
    enum hwr_room_curve room_light_curve;
    /* How far a room's own light carries, as a multiplier on the reach the map
     * authored for it. It is the darkness dial of the law above that reads the
     * map's number as a reach: one is the map exactly as written, and lower
     * pulls every room's darkness in toward the player -- a dim room reaching
     * only a few paces while a lit one still carries down its whole length. */
    float room_light_reach;
    /* And how softly that light ends, in the shade ramp's own steps. A room's
     * light running out at a finite distance arrives at nothing with a slope
     * and then stops, and the corner between the two is what an eye reads as a
     * wall around the player. One step is the largest height that is inert at
     * the map read as written, so the setting costs the original's own picture
     * nothing and acts exactly where the reach above has pulled a room's light
     * in. */
    float room_light_shoulder;
    float room_light_dark;
    float room_light_lit;
    /* The shape of the response between them. One is straight in the exponent's
     * own space, which is where the authored byte already lives. */
    float room_light_shape;
    /* Where the two INPUT anchors come from, the pair used when they are
     * written rather than fitted, and how narrow or wide a fitted pair may be.
     * The upper bound is what carries the promise: with a window no wider than
     * this, a nudge of a few bytes is always a visible share of the whole
     * dark-to-lit swing, on every map and at every darkness. The lower bound
     * stops a map whose sectors are nearly all one value from turning a
     * one-byte rounding difference into the entire range. */
    enum hwr_room_anchor room_light_anchor;
    uint32_t room_light_byte_dark;
    uint32_t room_light_byte_lit;
    uint32_t room_light_window_min;
    uint32_t room_light_window_max;
    /* How much of the map's own unshaded class survives. A surface the author
     * marked light zero is fullbright at every distance and no light command can
     * ever touch it, which is exactly right indoors and can read as glaring in a
     * world tuned dark. One keeps it exactly as authored. */
    float room_light_unshaded_gain;
    /* The original caps how dark a bright sector's surfaces may get, purely
     * because it had thirty-two steps to spend. Dropping that cap is what lets
     * a lit room read as lit beside a dark one; this puts it back. */
    int room_light_ceiling_authentic;
    /* Where the light total stops adding up. Nothing downstream carries a value
     * past the picture's own brightest back into the range, so a cut there does
     * not merely bound two bright surfaces -- it paints them the same value. One
     * is that cut, which is what this ships at and what it has always been;
     * below one the light past it is eased toward the brightest instead, and the
     * order between two rooms that both overrun survives. */
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
    /* How far ahead of the eye the pool's own centre sits, as a share of that
     * reach. The original's candle is a cone about the direction the player is
     * looking, so a light aimed forward is the faithful reading of it and a
     * point sitting on top of him was the simplification. */
    float player_light_forward;
    enum hwr_light_scope player_light_scope;
    /* Every map carries a word saying how strong its own lantern was meant to
     * be, which the game itself never looks at. This is how much notice to take
     * of it: at nothing the two rows above are the whole answer, at one the
     * map's word is read whole. */
    float player_light_glow_scale;
    /* And the half of that light the map itself already had a word for. Rooms
     * the map marks candle-lit are drawn by the game at their own depth law LESS
     * a fixed number of shade rows, and those rows are the candle rather than
     * the room: the game's own near-field brightness in such a room IS the light
     * the player is holding. This is how much of that the carried light
     * supplies -- at full it is the game's own candle exactly, and turned down
     * a candle-lit room is left with only what it states for itself -- and how
     * much of what it supplies is shaped like a pool at the eye rather than
     * filling the room evenly the way the game's screen-space one does. */
    float player_light_candle;
    float player_light_candle_pool;
    /* How softly that candlelight's cone goes over from light to dark at its
     * edge. Zero is the game's own transition. */
    float player_light_penumbra;
    /* The flame in that light: whether it moves at all, how far the pool of
     * light wanders about where you are looking, and how quickly it moves.
     * The wander is the game's own flicker -- the original moves the pool, it
     * does not pulse the room -- and it is held inside the game's own
     * amplitude, so it reads as a flame rather than a fault. */
    int player_light_flicker;
    float player_light_flicker_amount;
    float player_light_flicker_speed;
    /* Props and figures keep their own brightness dial and their own shaded
     * half, so a dark room never makes what is standing in it unfindable. */
    float sprite_light_gain;
    float sprite_light_wrap;

    /* The lights that are NOT at the eye: a torch on a wall, a candle on a
     * table, a lit sigil. Which objects are those is not a list somebody keeps
     * -- it is the map's own mark for a thing that gives light rather than
     * takes it, so a map nobody has ever seen brings its own.
     *
     * The family enable and the class behind it, which are two switches on
     * purpose: one is "no lights of this kind at all", the other is "not from
     * that source", and a later source is a third switch rather than a change
     * to either. */
    int point_light;
    int point_light_objects;
    /* The second source: the FACES a game config grants a light -- lava, a
     * glowing rune, a lit sigil painted onto a wall. Nothing is granted until
     * somebody writes one down, so this switch costs an untuned game nothing;
     * what it buys a tuned one is a lake of lava lighting its own cavern. */
    int point_light_glow_faces;
    /* How many the world pass carries at once. It is a quality dial rather
     * than a limit somebody should have to think about: what is dropped is
     * always the light contributing least where the eye is. */
    uint32_t point_light_max;
    /* And how many of THOSE cast real shadows, worth-most first: nought is
     * the whole feature off, and anything at or past the row above is every
     * light there is. It is bounded by its own storage rather than by that
     * row on purpose -- somebody who wants all of them writes one big number
     * and never has to come back when the row above moves.
     *
     * A game config may pin one light's shadows on, or refuse them, per
     * light; this count is what every light it says nothing about follows. */
    uint32_t point_light_shadows;
    /* The strength and the reach every one of them starts at, before the map's
     * own word about either. The reach is where the pool ENDS, not where it
     * becomes hard to see, so it also bounds how far a light with nothing to
     * stop it can spill. */
    float point_light_intensity;
    float point_light_range;
    /* WHAT COLOUR THEY THROW IS NOT A SETTING and has no field here. Every one
     * of them takes its colour off the thing that gives the light -- a torch
     * off its own flame, a granted surface off its own picture -- and a
     * particular thing is recoloured by naming it in a game config, which is a
     * statement about that thing rather than about all of them. Two rows used
     * to sit here: one colour for every light, and a file-only switch that
     * chose between it and the reading. The reading is the law, so the switch
     * chose nothing anybody wanted and the visible colour under it moved
     * nothing anybody could see -- a settings row that reads as broken. Both
     * are gone, keys and all. */
    /* How much of the map's own candle word these take, exactly as the light
     * the player carries takes it: at nothing the two rows above are the whole
     * answer, at one the map's word is read whole. */
    float point_light_glow_scale;
    /* The containments, which are not shadows and are not sold as any.
     *
     * How many doorways from the room the eye is in a light may still be
     * seeded from WHERE THE FRAME HAS NO DRAWN SET -- with one, whether a
     * light's own room is on the screen is the containment and this stands
     * down, because a light going out as somebody walks away from the room it
     * is lighting would be a light following the player rather than one
     * standing in a room; how far above a room's ceiling and below its floor
     * its own lights may reach before they are faded out, which is what stops a
     * torch lighting the storey above it; and how long a light dropped for want
     * of a slot takes to go out, so a set that changes never does it in one
     * frame. */
    uint32_t point_light_hops;
    float point_light_z_slack;
    float point_light_fade;

    /* The sun and moon: one directional light per map, entering wherever the
     * map is open to the sky, gated per pixel by a light-space depth shadow
     * map rendered from the light. Lit only on maps with sky faces -- a
     * sealed map builds nothing -- and, like the whole lighting family, only
     * under the Modern shading law. The colour, strength and direction here
     * are the fallback when no game config names the map; direction tuning
     * is per-map work by nature, since the maps were authored with no sun. */
    int sun_light;
    uint32_t sun_color;
    float sun_intensity;
    float sun_elevation_deg;
    float sun_yaw_deg;
    /* Objects -- trees, statues, characters -- cast as upright cards faced
     * to the light, rough silhouettes accepted. The off position is an
     * escape hatch, not the default. */
    int sun_object_shadows;
    enum hwr_sun_quality sun_shadow_quality;
    /* Edge softness as a real penumbra radius, hard to soft; the default
     * rests barely off hard, at the single bilinear tap's own footprint. */
    float sun_shadow_softness;
    /* The moving shadow window's side, in world units: resolution where the
     * player is, clamped-to-lit beyond it. */
    float sun_shadow_span;
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
    HWR_CHANGE_TRUECOLOR        = 1u << 11,
    /* No hook reacts to the game config picker: the look layer notices it
     * moving in the settings pump instead, so that a newly chosen file's
     * overrides are in force BEFORE the hooks that consume them run. The row
     * carries a bit of its own anyway, because the table's rule is that every
     * row does and the mask keeps its meaning that way. */
    HWR_CHANGE_GAME_CONFIG      = 1u << 12
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
/* Which game config is chosen, by its place in the list the folder scan built.
 * The look layer writes it when the folder is listed again and the file that
 * was chosen has moved place, so a selection survives a file being added to or
 * taken out of the folder. A place the list does not hold is refused by the
 * row, exactly as any other value out of range is. */
void hwr_settings_set_game_config(int32_t slot);

#endif
