#ifndef HWR_GL_H
#define HWR_GL_H

#include <stdint.h>

#include <stddef.h>

enum hwr_gl_material_flags {
    HWR_GL_MATERIAL_MASKED      = 1u << 0,
    HWR_GL_MATERIAL_DIRECT      = 1u << 1,
    HWR_GL_MATERIAL_SKY         = 1u << 2,
    HWR_GL_MATERIAL_FLAT        = 1u << 3,
    HWR_GL_MATERIAL_TRANSLUCENT = 1u << 4,
    HWR_GL_MATERIAL_HFLIP       = 1u << 5,
    /* A sky cap that may occlude: it stands on its wall's topmost edge. */
    HWR_GL_MATERIAL_SKY_OCCLUDER = 1u << 6,
    /* A degenerate EDGE_MAP face whose sky column reaches both viewport edges. */
    HWR_GL_MATERIAL_SKY_FULL_HEIGHT = 1u << 7
};

enum hwr_gl_wall_wrap_flags {
    HWR_GL_WALL_WRAP_ALONG    = 1u << 0,
    HWR_GL_WALL_WRAP_VERTICAL = 1u << 1,
    /* A one-sided wall whose texture carries the block transparency bit.  The
     * engine's masked span writer leaves index-0 texels untouched and blends
     * high-bit texels with the destination pixel; behind a one-sided wall that
     * destination is never repainted, so both read as the previous frame. */
    HWR_GL_WALL_STALE_MASK    = 1u << 2,
    /* All four wd_project corner words of this (sub-)quad saturated the shade
     * ramp (== 0x1fff).  The engine's counter==4 fast path skips per-texel
     * shading and solid-fills the block with the map fill index; the shader
     * outputs u_fill_index directly for these fragments. */
    HWR_GL_WALL_SOLID_FILL    = 1u << 3,
    /* A palette-colour (untextured) wall part drawn through the span driver's
     * solid-colour BLEND writer.  That writer differs from the textured one in
     * two ways this bit selects for: it blends EVERY pixel -- there is no
     * high-bit gate and no index-0 skip -- and it indexes the blend table by
     * the SOURCE colour first and the destination pixel second, the transpose
     * of the textured rule. */
    HWR_GL_WALL_SOLID_BLEND   = 1u << 4,
    /* The surface's normal is not in its vertices: it is an upright normal
     * facing the eye, derived where the fragment is. A sprite is a standing
     * figure rather than a card, so it is lit as one from wherever it is seen,
     * and no emitter has to re-author a direction every frame. */
    HWR_GL_NORMAL_BILLBOARD   = 1u << 5,
    /* The surface never enters a lighting composition at all: a ground shadow
     * (a darkening decal, which a light would turn into a bright oval), a sky
     * cap, and the colourless plane a mirror owns depth at. */
    HWR_GL_NORMAL_UNLIT       = 1u << 6
};

/* Non-wall billboard marker: the fixed-orientation quad carries an owner-wall
 * identity for the guarded redraw, but must not replace the wall's recorded
 * identity during its normal depth-tested pass. */
#define HWR_GL_WALL_OWNER_OVERLAY (1u << 7)

/* High bits of flat_mapping_flags are available to non-flat geometry. Mesh
 * faces use perspective-correct wall_s/wall_top as direct atlas coordinates,
 * bypassing the wall-column and world-plane mapping drivers. */
#define HWR_GL_MAPPING_MESH 0x80000000u
enum hwr_gl_translucent_run_flags {
    HWR_GL_TRANSLUCENT_RUN_FLOOR_EXCEPTION = 1u << 0,
    /* A world mid-platform surface that has to win over the flat of its own
     * sector. Unlike an object part it is redrawn with the depth test set to
     * GREATER rather than switched off, so the redraw covers exactly the
     * fragments the ordinary pass rejected and a translucent slab is never
     * blended into the same pixel twice. */
    HWR_GL_TRANSLUCENT_RUN_SLAB_EXCEPTION = 1u << 1
};

/* One contiguous vertex range inside the opaque range, redrawn by the
 * owner-identity exception stage. */
struct hwr_gl_exception_run {
    uint32_t first;
    uint32_t count;
};

#define HWR_GL_SLAB_EXCEPTION_MAX 64u

struct hwr_gl_vertex {
    int16_t x;
    int16_t y;
    int16_t z;
    /* Packed wall endpoint: shade row in the high byte and interpolation
     * fraction in the low byte.  For a wall this is the BOTTOM-edge corner word
     * (screen row at the column's bottom); for a tinted flat it is the
     * per-vertex candle shade word.  Zero for non-shaded geometry. */
    uint16_t wall_shade_endpoint;
    /* Companion TOP-edge corner word (row<<8)|frac for a wall.  The tinted-wall
     * sub-row shade interpolates bilinearly between this and the bottom-edge
     * word down each column.  Zero for flats and non-shaded geometry. */
    uint16_t wall_shade_endpoint_top;
    /* The surface normal, octahedral, in the two bytes the alignment before the
     * first float already reserved -- so a law that needs a direction costs the
     * format nothing at all. Written only where the shading law asks for one;
     * the reserved (0,0) pair is what every other vertex carries, and it says
     * the fragment has no normal rather than one pointing somewhere. */
    uint8_t normal_x;
    uint8_t normal_y;
    float wall_s;
    float wall_top;
    uint32_t texture_offset;
    uint16_t texture_width;
    uint16_t texture_height;
    uint16_t aux;
    uint8_t pan_u;
    uint8_t pan_v;
    uint8_t scale;
    uint8_t sector_flags;
    uint8_t light;
    uint8_t material_flags;
    uint8_t direct_index;
    /* The original wall driver selects masked or linear sampling separately
     * for the along-wall and vertical texture axes. */
    uint8_t wall_wrap_flags;
    /* Per-face tinted-wall depth-bias adjust (0..16): shade rows the engine's
     * flags&0x40 wall-driver block subtracts from the candle bias. Zero for
     * non-wall geometry. */
    uint8_t wall_tint_adjust;
    /* Legacy per-triangle escape for a tinted flat with a corner outside the
     * drawn area: the shader then evaluates the candle radial per fragment
     * instead of interpolating that corner's row. Only the restore switch emits
     * it -- a view-clipped flat has no such corner left. Zero otherwise. */
    uint8_t flat_taint;
    /* Sub-unit world remainder of the position, added to the wrapped integer
     * position in the vertex shader. A view-clipped flat corner falls between
     * authored world units, and near the bottom of the frame one world unit is
     * several pixels tall, so the remainder is what holds that corner on the
     * frame edge. Zero on every vertex that sits on an authored coordinate,
     * where adding it changes nothing. */
    float pos_frac_x;
    float pos_frac_y;
    /* Per-flat affine span state after the original scanline-range setup. */
    uint32_t flat_guard;
    uint32_t flat_guard_step;
    int32_t flat_guard_origin_y;
    uint32_t flat_mapping_flags;
    /* Wall-column state staged after projection and viewport clipping. */
    uint32_t wall_source_numerator;
    uint32_t wall_source_denominator;
    uint32_t wall_source_step;
    int32_t wall_denominator_step;
    uint32_t wall_top_accum;
    uint32_t wall_bottom_accum;
    int32_t wall_top_step;
    int32_t wall_bottom_step;
    int32_t wall_first_x;
    uint32_t wall_along_extent;
    uint32_t wall_vertical_extent;
    uint32_t wall_span_valid;
    int32_t wall_vertical_origin;
    uint32_t wall_vertical_seeded;
    /* Optional 16.16 framebuffer-X portal interval. Equal endpoints disable
     * the fragment gate. Dedicated fields keep flat mapping state intact. */
    uint32_t portal_clip_left;
    uint32_t portal_clip_right;
};

/* The normal is meant to be free, and free means the format did not move: the
 * two bytes were already there as padding and the record is the size it always
 * was. Held here rather than in a comment because the attribute pointers below
 * are written against these offsets. */
_Static_assert(sizeof(struct hwr_gl_vertex) == 128,
               "the vertex record must stay 128 bytes");
_Static_assert(offsetof(struct hwr_gl_vertex, wall_shade_endpoint) == 6 &&
               offsetof(struct hwr_gl_vertex, wall_shade_endpoint_top) == 8,
               "the two shade endpoint words must stay adjacent, in that order");
_Static_assert(offsetof(struct hwr_gl_vertex, normal_x) == 10 &&
               offsetof(struct hwr_gl_vertex, normal_y) == 11 &&
               offsetof(struct hwr_gl_vertex, wall_s) == 12,
               "the normal must occupy the pad the alignment already reserved");

struct hwr_gl_camera {
    int16_t view_offset_x;
    int16_t view_offset_y;
    int16_t eye_z;
    int16_t yaw_sin;
    int16_t yaw_cos;
    uint16_t focal_x;
    uint16_t focal_y;
    int32_t center_x;
    int32_t horizon;
    /* The view centre row BEFORE the pitch shift. The engine's horizon is
     * this row plus the pitch term, but the candle origin's anchor is this
     * row alone (renderer.c:13411-13426 against the horizon staging in
     * render_world.c), so pitching the view slides the world under a
     * screen-fixed pool and the pool follows the gaze onto the near floor
     * or ceiling. The modern cone's fixed centre anchors here for the same
     * reason; level, it is the horizon exactly. */
    int32_t cone_center_y;
    /* Engine-space projection retained when the target framebuffer differs
     * from the engine viewport. Portal windows and intrinsic object sizes are
     * authored in this source space. */
    uint16_t source_focal_x;
    uint16_t source_focal_y;
    int32_t source_center_x;
    int32_t source_horizon;
    int32_t source_cone_center_y;
    uint16_t source_width;
    uint16_t source_height;
    int16_t shade_origin_x;
    int16_t shade_origin_y;
    /* The candle origin in the engine's own screen window. The scaled pair
     * above drives the shader's per-fragment radial, which evaluates at each
     * fragment and so carries no window scale into its answer; the CPU corner
     * law samples at the drawn area's BOUNDARY, so it has to measure in the
     * window the engine defined the law in or its radial arm grows with the
     * target aspect. */
    int16_t source_shade_origin_x;
    int16_t source_shade_origin_y;
    uint8_t shade_level;
    uint8_t tint_shade_level;
    uint8_t global_light_bias;
    uint8_t glow_phase;
    /* Low byte of the map's g_das_palette_remap_prefix: the flat palette index a
     * solid-filled (all-corners-saturated) tinted wall block outputs directly. */
    uint8_t solid_fill_index;
    uint16_t flat_edge_bias;
    /* The same staged word, signed, for the wall model: the original writes
     * ONE bias into its wall and flat edge projectors alike each frame -- the
     * map's own candle word plus nothing when the player stands in the
     * candle-lit class and minus thirteen when he does not -- so a candle-lit
     * room seen from outside the class is drawn markedly darker. Negative in
     * exactly that state. */
    int16_t wall_edge_bias;
    /* And the map's word alone, without the player-state term: the candle's
     * own amplitude, which is the poolable half of what the player's light
     * supplies to a candle-lit room. */
    int16_t candle_word;
    float shade_depth_scale;
    uint32_t sky_texture_offset;
    uint32_t sky_projection_offset;
    uint16_t sky_source_offset;
    uint16_t sky_texture_height;
    int16_t sky_start_row;
    uint16_t sky_yaw;
    uint8_t sky_double_scanline;
    uint8_t sky_valid;
    /* Source-view metadata used while preparing alternate views.  The GL
     * projection only consumes x_flip; yaw and reflected remain available to
     * the scene builder that derives each view. */
    uint16_t yaw;
    uint8_t x_flip;
    uint8_t reflected;
};

struct hwr_gl_scene_view {
    const struct hwr_gl_vertex *vertices;
    size_t sky_vertices;
    /* Primary-view mirror planes are appended after the translucent range and
     * submitted early as an invisible depth barrier. */
    size_t mirror_depth_vertices;
    size_t opaque_vertices;
    size_t flat_vertices;
    size_t translucent_vertices;
    const uint32_t *translucent_runs;
    const uint8_t *translucent_run_flags;
    size_t translucent_run_count;
    /* Opaque 3D object-mesh vertices between world faces and billboards. */
    size_t opaque_mesh_vertices;
    size_t opaque_billboard_vertices;
    size_t shadow_vertices;
    size_t shadow_run_count;
    /* Main translucent runs carrying either exception flag. */
    size_t translucent_exception_run_count;
    /* Opaque mid-platform surfaces redrawn over the flat of their own sector. */
    const struct hwr_gl_exception_run *slab_runs;
    size_t slab_run_count;
    int floor_exception;
    struct hwr_gl_camera camera;
    /* Horizontal framebuffer interval owned by this view. Equal endpoints
     * select the full target. */
    int32_t clip_left;
    int32_t clip_right;
};

/* Where the true-colour world image is kept. Eight bits per channel is exact
 * for the authentic picture; the floating-point format is the precision a law
 * that resolves colour continuously is composed at, and is offered only where
 * the driver proves the attachment completes. Automatic reads the shading law:
 * eight bits for the original picture, floating point for any other. */
enum hwr_gl_color_format {
    HWR_GL_COLOR_AUTO = 0,
    HWR_GL_COLOR_RGBA8,
    HWR_GL_COLOR_RGBA16F
};

/* Which law the enhanced program resolves a shaded fragment with. Authentic
 * floors the shade quantity to one colormap row, which is the picture the game
 * has always drawn; smooth keeps that quantity continuous and mixes the two rows
 * it falls between, and changes nothing else about it -- the same depth law and
 * the same candle, jitter, corners and all, so flooring it lands back on the
 * authentic row; modern leaves the ramp behind and multiplies the texel's own
 * colour by the light that reaches it. The authentic program carries none of
 * these branches: the law is selected by a uniform inside the enhanced program
 * alone. */
enum hwr_gl_shading_mode {
    HWR_GL_SHADING_AUTHENTIC = 0,
    HWR_GL_SHADING_SMOOTH,
    HWR_GL_SHADING_MODERN
};

/* The rig the modern law composes with, as the settings layer worked it out.
 * Colours are the values a person picked, in the display-referred space they
 * picked them in; the intensity beside each one is light energy and multiplies
 * whichever space the composition is running in. Keeping the two apart is what
 * lets the working space be decided in one place -- the shader -- rather than
 * baked into a product here. */
/* Which law turns a room's authored brightness into light.
 *
 * The first scales the byte by one gain. The second places it between two
 * anchors taken from the map's own sectors, so a light switch keeps its share
 * of the range however dark the world is set. The third reads the byte for what
 * the engine reads it as -- a REACH, the distance at which that room's light
 * runs out -- and takes the room's brightness from the engine's own shade at
 * the fragment's depth. Only the third can tell the far end of a corridor from
 * the wall in front of the player: the other two are functions of the sector
 * alone and give both the same number.
 *
 * Zero is the oldest of them, which is also what a rig nobody has filled in
 * resolves to, so a caller that knows nothing of any of this gets the picture
 * it always got. */
enum hwr_gl_room_curve {
    HWR_GL_ROOM_CURVE_LEGACY = 0,
    HWR_GL_ROOM_CURVE_ANCHORED,
    HWR_GL_ROOM_CURVE_ENGINE
};

struct hwr_gl_light {
    /* The authored sector byte, as self-illumination. */
    float room_gain;
    float room_gamma;
    /* Which of the three laws above the byte is read through. */
    int room_curve;
    /* How far a room's own light carries, as a multiplier on the reach its
     * authored byte states. One is the map read exactly as written, which under
     * the engine law is the original's own picture; below one every room goes
     * dark nearer its own beginning. Read by that law alone, and it divides by
     * it, so it is never zero. */
    float room_reach;
    /* And how softly it ends, in the ramp's own steps. A room's light running
     * out at a finite distance arrives at nothing with a slope and stops
     * there, and the corner between the two is the rim a person reads as a
     * wall; above nothing the last of that light decays toward nothing rather
     * than arriving at it. One step is the largest height the row law's own
     * ceiling never reaches at a reach of one, so it costs the original's
     * picture nothing at all. */
    float room_shoulder;
    /* The two input anchors, already taken to the exponent above, and the two
     * output levels the byte is placed between. The anchors are worked out where
     * the map is known and the rows are known, so the fragment does one subtract,
     * one divide and one mix over what it did before. */
    float room_anchor_low;
    float room_anchor_high;
    float room_dark;
    float room_lit;
    /* The response shaping between the two levels. One is straight. */
    float room_shape;
    /* How far the map's unshaded class is pulled down into the room's own light.
     * It is the COMPLEMENT of the row a person sets, which reads "how much of
     * that class survives as authored", for one reason: a rig nobody has filled
     * in must be the picture that class always had, and that is this at zero.
     * At zero the class never reaches the light rig at all. */
    float room_unshaded_pull;
    /* Where the light total is held: the floor is the darkest anything is
     * allowed to be, and the ceiling is where it stops adding up -- a KNEE the
     * light is eased into rather than a cut, so two surfaces past it keep their
     * order. At one and above there is nothing left to ease into and it is the
     * cut it always was, to the bit. */
    float room_floor;
    float world_ceiling;
    /* Put the original's darkness ceiling -- a bright sector's surfaces never
     * fading past its own clip row -- back. */
    int room_ceiling_authentic;
    /* How much of the original's depth law survives, zero to one. */
    float distance_shading;
    float ambient_color[3];
    float ambient_intensity;
    float lamp_color[3];
    float lamp_intensity;
    float lamp_range;
    /* How far along the view axis the pool's own centre sits, as a share of
     * that reach. The original's candle is a cone about the direction the
     * player is looking rather than a point on top of him, so forward is the
     * faithful reading; nothing is the pool at the eye. */
    float lamp_forward;
    /* Nonzero lights every face rather than only the ones the map marked. */
    int lamp_scope_all;
    /* How much of the map's own candle word the two above take: zero ignores
     * it and one reads it whole. */
    float lamp_glow_scale;
    /* The other half of the light a person carries, and the half the map itself
     * already had a word for.
     *
     * A room whose faces the map marks candle-lit is drawn by the original at
     * its own depth law LESS a constant number of rows, and that constant is the
     * candle's amplitude rather than anything the room authored. So the room
     * term says where a room's light starts to go, and this says how much of
     * that room's candle the light in the hand supplies: at one it is the map's
     * own number of rows and the sum is the original's picture exactly; at
     * nothing the room is left with what it states for itself. The light's own
     * switch puts it out, and the flame and the map's lantern word are spent
     * into it, so a flicker reaches the near field the original lights this way.
     *
     * And how much of what it supplies is shaped like a pool at the eye. The
     * original's candle is measured in SCREEN space and so fills a candle-lit
     * room evenly at every distance; at nothing this is that exactly, and wound
     * up the amount falls off with the light's own reach and with which way a
     * surface faces. */
    float candle_amount;
    float candle_pool;
    /* How softly the candle's cone goes over from light to dark. The engine's
     * cone edge is where the screen radial has spent the map's own candle word
     * -- fourteen degrees off the view axis on every map the game ships -- and
     * the row law crosses it at one pace. This spreads that crossing over a
     * band about the edge: the radial's slope is eased where the edge lives,
     * the core inside the band is untouched, and past it the flanks keep a
     * bounded share of the light they lost. Zero is the engine's own edge,
     * exactly. */
    float cone_penumbra;
    /* The flame in that light -- whether it moves, how far it may swing, and
     * how quickly its wave runs. Zero for the first is exactly no flame, which
     * is what leaves the light the rows describe untouched to the bit. */
    int lamp_flicker;
    float lamp_flicker_amount;
    float lamp_flicker_speed;
    /* Props: their own gain, and how far light wraps past their terminator so a
     * standing figure's shaded half never goes fully black. */
    float sprite_gain;
    float sprite_wrap;
    /* How wide the band is that a light which is not at the eye fades out
     * across, above its own room's ceiling and below its own floor. The window
     * itself is per light and travels with it; this is the softness of its two
     * edges, and it is the same softness for all of them because it is one
     * row. Zero is a hard edge. */
    float point_z_edge;
};

/* How many lights that are NOT at the eye the world pass carries at once.
 *
 * The array rides a uniform BLOCK now rather than uniform registers: four
 * four-component slots a light, 4 KB at this ceiling, against the 16 KB
 * block every GL 3.3 driver guarantees -- the plain-uniform arithmetic
 * that once capped this at sixteen (1024 components, most of them spent
 * here) died with the restructure. What still bounds it is the SHADOW
 * side: the six-face shadow tiles stand in one texture column at 512
 * texels a tile, and sixty-four of them is the 32768-texel dimension the
 * mainstream drivers ship (a smaller driver stands fewer tiles and says
 * so at run time, never fewer lights). The family keeps ONE ceiling for
 * both rows because a shadow count past the light count names nothing --
 * so the ceiling is the atlas's own, stated here once. */
#define HWR_GL_POINT_LIGHT_MAX 64u

/* One light in the world, as a seeding class handed it over.
 *
 * The position is in the WORLD's own coordinates and is transformed into each
 * view's own space as that view binds, by the same transform the geometry stage
 * runs on every vertex. That is what makes a mirror free: a reflected view
 * carries its own camera, so a light in it is lit from the reflected side by
 * construction rather than by a reflection rule written twice. */
struct hwr_gl_point_light {
    float world_x;
    float world_y;
    float world_z;
    /* Where the pool ends, in world units. The falloff is windowed rather than a
     * true inverse square, so this is where the light reaches nothing rather
     * than where it becomes imperceptible. */
    float range;
    /* The colour it throws and the energy behind it, kept apart for the same
     * reason the rig above keeps them apart: one is a colour somebody picked and
     * the other is light. */
    float color[3];
    float intensity;
    /* The vertical window its contribution is held inside, in world Z: the
     * owner sector's own floor and ceiling with slack either side. A fragment's
     * world Z is exactly reconstructible from the view position, so this costs
     * no vertex data -- it is what stops a torch lighting the room above it. */
    float z_lo;
    float z_hi;
    /* What a game config SAID about this light casting shadows, which is a
     * three-state answer and not a switch: nothing at all (follow the
     * settings row -- the ordinary case, every light in an undescribed
     * game), always (this one is worth a map whatever the row says), never
     * (this one is not, whatever the row says). The seeding carries the
     * word; who actually gets a tile is the shadow layer's own law. */
    int shadow_request;
    /* WHICH THING'S OWN PICTURE THIS LIGHT IS, as the map's own object number
     * -- the number the community map editor shows as "Object: N" -- or
     * HWR_GL_LIGHT_NO_SOURCE where no drawn thing is the source of it.
     *
     * It rides the light for one reason: a thing that gives light stands a
     * unit or two off whatever it is mounted on while its light is pushed
     * clear of that same surface, so the thing's own card is BETWEEN its light
     * and its mounting -- and a card that shadows the surface it decorates is
     * the sigil putting itself out. The shadow layer therefore leaves a
     * light's own card out of that light's own map, and it can only know which
     * card that is if the light says so. Nothing else reads it.
     *
     * A number rather than a pointer or an array index, because it names the
     * same thing across frames and across a rebuild of the object list: what
     * the seeding read and what the shadow layer walks are one map's own
     * numbering. */
    uint32_t source_object;
    /* What this light is WORTH to the picture: the number the seeding ranked
     * the frame's lights by, at full strength (see hwr_light_score). It rides
     * the light because the shadow layer's own hysteresis is a comparison
     * against it, and a second copy of the ranking arithmetic there would be
     * a second law to drift. Zero from anything that did not rank. */
    float score;
    /* Which atlas tile this light's shadow maps live in, or -1. Assigned by
     * the point-shadow layer after the frame's lights are chosen; -1 is what
     * every light carries where nothing casts. */
    int shadow_tile;
};

/* A light no drawn thing is the source of: one a game config hangs at three
 * numbers of its own, one a granted surface glows with, the one at the eye.
 * The same value the object list carries for a thing no map numbers, which is
 * asserted where both headers are in scope (hwr_pshadow.c) rather than left to
 * agree by eye. */
#define HWR_GL_LIGHT_NO_SOURCE 0xffffffffu

/* The three states of the answer above. */
#define HWR_GL_SHADOW_FOLLOW 0   /* nothing said: the settings row decides */
#define HWR_GL_SHADOW_ALWAYS 1   /* pinned: a tile before the row's own picks */
#define HWR_GL_SHADOW_NEVER (-1) /* refused: no tile, whatever the row says */

/* One caster vertex of the sun/moon light-space depth map.
 *
 * A compact format of its own, in its own array object, drawn by its own small
 * program: the load-bearing sixteen-attribute main vertex body carries the
 * whole eye picture and is not touched for casting. World position, the texel
 * coordinates the masked cut-out needs, and where those texels live in the one
 * resident texture store -- nothing else, because a depth map has no colour,
 * no shading and no screen. */
struct hwr_gl_sun_vertex {
    float x;
    float y;
    float z;
    /* Texel coordinates in the atlas entry's own space: `u` runs over the
     * entry's first (row, stride-making) axis and wraps by its height, `v`
     * over the second and wraps by its width -- the same iy*w+ix addressing
     * every other sampler of the store uses. Only a masked caster reads them. */
    float u;
    float v;
    uint32_t offset;
    uint16_t width;
    uint16_t height;
    uint8_t flags;
    /* The caster's own plane normal, packed as every receiver normal is
     * (hwr_normal_encode). The conservative line pass reads it to write
     * each edge texel the caster's PLANE depth at that texel's centre --
     * the same value the fill rasterizer writes at covered texels -- so an
     * edge texel never stands shallower than the plane the carried lookup
     * references expect. Zero on both bytes says no plane is carried and
     * the line keeps its own interpolated depth. */
    uint8_t normal_x;
    uint8_t normal_y;
    uint8_t pad0;
};

_Static_assert(sizeof(struct hwr_gl_sun_vertex) == 32,
               "the sun caster vertex must stay 32 bytes");

enum hwr_gl_sun_vertex_flags {
    /* The surface's texture carries the block-transparency bit: its index-0
     * texels are holes and both passes discard them -- a hole is not a
     * surface at all. */
    HWR_GL_SUN_MASKED = 1u << 0,
    /* An object card: its outline IS its identity -- a card without its
     * index-0 discard is a rectangle, not a rough shadow. */
    HWR_GL_SUN_CARD = 1u << 1,
    /* The translucent range: a surface whose texels decide per texel what
     * the beam does, by the same rule the eye-ray transmittance pass reads
     * off the map's own blend tables -- an index-0 texel of a masked
     * surface is a hole (full light), a blend-range texel (high bit set)
     * filters the beam by the fitted pass weight through the gobo plane,
     * and every other texel is opaque paint: it writes depth and blocks the
     * beam whole, exactly as a masked caster's paint does. Carried WITHOUT
     * a texture (width 0), it is the palette-colour solid-blend part: the
     * whole footprint filters by the fitted weight and owns no depth. */
    HWR_GL_SUN_TRANSLUCENT = 1u << 2
};

/* The directional key light and the light-space depth map that gates it,
 * published whole once per frame by the layer that owns the policy (the
 * caster set, the moving window, the tiers). This layer only executes it:
 * with `active` clear -- which is every frame until something publishes one --
 * the key slot stays at its zero rest and no pass, texture or program below
 * exists. */
struct hwr_gl_sun {
    int active;
    /* World-space unit direction of travel of the light, sky toward ground
     * (world z up, so dir[2] < 0). Each view turns its negation into its own
     * basis as it binds, exactly as the point-light positions are. */
    float dir[3];
    float color[3];
    float intensity;
    /* world -> [0,1]^3 light space, column-major: xy the depth-map texel
     * coordinate, z the depth the compare runs against. */
    float lookup[16];
    /* world -> light clip (-1..1), column-major: what the caster pass
     * rasterises through. The two matrices describe ONE window; both are
     * built where the window is decided so they cannot drift apart. */
    float clip[16];
    /* Depth-map side in texels. A change re-takes the storage. */
    int resolution;
    /* Nonzero selects the LINEAR depth compare -- the free 2x2 PCF, the
     * barely-soft edge. Zero is the hard edge. */
    int soft;
    /* The softness radius: how many ring taps the fragment takes around the
     * centre one, and how far out they stand, in light-space texels. Zero
     * taps is the single centre tap -- the free 2x2 PCF -- to the bit, which
     * is what the barely-soft default resolves to. */
    int soft_taps;
    float soft_radius;
    /* The lookup bias CEILING in [0,1] depth units: the flat constant the
     * first milestone read everywhere, kept as the worst case the law below
     * may never exceed. */
    float bias;
    /* The receiver-slope law's two staged terms: the floor every compare
     * keeps (quantization plus a world unit), and the per-texel slope scale
     * the fragment multiplies its own light-space depth gradient by. Both
     * zero means the law is absent and the ceiling alone is the bias --
     * which is the first milestone's arithmetic, kept reachable so the
     * offline twin can hold the corner leak against it. */
    float bias_floor;
    float bias_slope;
};

/* THE POINT-LIGHT SHADOWS' execution side: one two-dimensional R32F atlas of
 * cube-topology face maps (three faces across, two down, one such tile per
 * shadowed light), rendered from the same caster vertex format the sun uses,
 * by the same caster program compiled once more under a perspective define.
 * The policy -- which lights, which tiles, when a tile is stale -- lives in
 * hwr_pshadow.c; this layer only executes it. The law both sides compile is
 * hwr_point_math.h.
 *
 * The number below is the CEILING the storage is built for, not the number of
 * tiles that stand: the atlas is allocated to the `tiles` the policy
 * publishes, which is what the settings row asks for held to how many lights
 * there can be. */
#define HWR_GL_PSHADOW_MAX 64u

struct hwr_gl_pshadow_light {
    float world[3];
    /* The far plane of the tile's six faces, and the unit the stored radial
     * distances are normalized by: the light's own reach. */
    float range;
    int tile;
    /* Render this tile's faces this frame. Off, the tile keeps what it
     * holds -- which is the whole caching story: a static scene rebuilds
     * nothing and the per-frame cost is the lookups alone. */
    int rebuild;
    /* This light's own object cards, borrowed until the frame's scene render
     * returns; read only where rebuild says so. */
    const struct hwr_gl_sun_vertex *cards;
    size_t card_count;
};

struct hwr_gl_pshadow {
    int active;
    /* One face's texels on a side; a change re-takes the atlas storage. */
    int resolution;
    /* How many tiles the atlas is to hold -- the shadowed-light count in
     * force, which is a settings row and therefore moves only where a
     * settings change is safe. A change re-takes the storage exactly as a
     * resolution change does, and every tile is stale afterwards: the policy
     * layer asks each one back through the readiness ask, so no tile that
     * was one light's can be read as another's. */
    int tiles;
    /* The static caster slice every rebuilt tile draws -- the sun's own,
     * borrowed for the frame -- and the policy layer's generation stamp for
     * its CONTENT: the pointer can survive a rebuild, so equality of bytes
     * is claimed by the number rather than the address. */
    const struct hwr_gl_sun_vertex *static_casters;
    size_t static_count;
    uint32_t static_generation;
    size_t count;
    struct hwr_gl_pshadow_light lights[HWR_GL_PSHADOW_MAX];
};

/* Publishes the frame's shadowed lights. NULL or active==0 stands the whole
 * machinery down: no atlas, no programs, and the lookup multiplies by one. */
void hwr_gl_pshadow_set(const struct hwr_gl_pshadow *ps);
/* Copies one tile's face maps back for the offline twin, row-major floats,
 * the atlas layout of hwr_point_math.h: 3*resolution across, 2*resolution
 * down. Returns 0 where no atlas stands. */
int hwr_gl_pshadow_read_tile(int tile, float *out, int resolution);
/* Whether the last scene render had a live atlas at all. */
int hwr_gl_pshadow_map_valid(void);
/* Whether one tile holds a rendered map. The policy layer keeps asking for
 * a rebuild until this says so, which is what covers a frame the execution
 * stood down for (no context yet, another shading law) and a lost context:
 * a stamp alone would call the tile current and never render it. */
int hwr_gl_pshadow_tile_ready(int tile);
/* How many tiles one atlas can stand at this face resolution: the compile
 * ceiling held to the driver's own largest texture dimension. The policy
 * budgets within it, so a driver that stands fewer tiles sheds shadow
 * maps and never lights. Answers the compile ceiling before a context is
 * up. */
int hwr_gl_pshadow_tile_ceiling(int resolution);

/* Publishes the frame's sun. NULL or active==0 stands the whole machinery
 * down; the key uniforms then keep the zero rest they have always had. */
void hwr_gl_set_sun(const struct hwr_gl_sun *sun);
/* Replaces the static caster slice (the map's own geometry). Uploaded once
 * and drawn every sun render until replaced; count 0 clears it. The slice
 * arrives in two runs and the split index says where they meet: the depth
 * pass draws [0, gobo_first) -- everything that BLOCKS, cut-outs included
 * -- and the gobo pass draws [gobo_first, count), the translucent range
 * that FILTERS. A depth caster never writes the gobo: its occlusion is
 * already whole in the depth map, and the gobo plane has no depth of its
 * own, so a masked caster writing there would darken its own lit face --
 * the self-shadow the card law's half-texel push exists to prevent.
 * Returns 0 when no context exists yet, in which case the caller retries
 * next frame. */
int hwr_gl_sun_upload_static(const struct hwr_gl_sun_vertex *vertices,
                             size_t count, size_t gobo_first);
/* Hands over this frame's dynamic caster slice (object cards, mesh props).
 * The array must stay valid until the frame's scene render returns; it is
 * streamed into the pass's own buffer there. */
void hwr_gl_sun_set_dynamic(const struct hwr_gl_sun_vertex *vertices,
                            size_t count);
/* Copies the light-space depth map of the last sun render (row-major floats
 * in [0,1], resolution*resolution). The offline twin compares its own CPU
 * projection against exactly these texels. Returns 0 when no map stands. */
int hwr_gl_sun_read_depth(float *out, int resolution);
/* Copies the light-space transmittance plane of the last sun render, the
 * same shape: 1 where the beam passes whole, the pass weight where a pane
 * dims it, 0 under opaque paint. Returns 0 when no gobo was rendered. */
int hwr_gl_sun_read_gobo(float *out, int resolution);
/* Whether the last scene render laid a sun depth map down at all -- the
 * sealed-map gate reads it: a map with no sky face must never build one. */
int hwr_gl_sun_map_valid(void);

/* What the settings layer has worked out about the true-colour foundation. */
struct hwr_gl_truecolor {
    /* The foundation has to exist: a dependent enhancement is asking for it. */
    int required;
    /* Nothing is moving the picture yet, so the composition takes its neutral
     * early-out and the colour beside each index is exactly the colour that
     * index resolves to. */
    int neutral;
    /* Every multiplicative dial in one number: the bring-up gain and the
     * picture's brightness are the same operation on the same colour, so the
     * settings layer hands over their product rather than making the shader
     * carry two uniforms that do one thing. */
    float gain;
    /* The picture's contrast, a pivot around mid grey applied after the gain.
     * One leaves the colour where the gain put it. */
    float contrast;
    enum hwr_gl_color_format format;
    /* The shading law in force. Authentic is the original picture and is what
     * every neutral state resolves to. */
    enum hwr_gl_shading_mode shading_mode;
    /* What the modern law composes with. Read only by that law: the other two
     * never look at it, so it costs the original picture nothing to carry. */
    struct hwr_gl_light light;
    /* Dither the world image where it is quantized into screen colours. The
     * renderer holds it off whenever the picture is neutral, whatever this says:
     * a picture that is the authentic picture must come out as the authentic
     * bytes, and half a level of pattern is still a moved byte. */
    int dither;
};

/* The palette a frame is presented through, and the palette-level effects the
 * engine is running over it.
 *
 * The authentic path resolves the palette-index target through `rgba`, which
 * already carries every effect -- that is how the effects have always reached
 * the world image. The true-colour path composes in the UNTINTED palette and
 * applies the effects to the colour afterwards, because a lighting term has to
 * be evaluated against the colours the map authored rather than against a
 * damage flash, and because the index plane the host composition contract needs
 * stays keyed on one palette while a flash changes the DAC every frame.
 *
 * base_rgba may be NULL, which says the effects in force cannot be reproduced
 * in colour space; the authentic resolve then stands, exactly as before. */
struct hwr_gl_palette {
    const uint8_t *rgba;       /* 1024 bytes: the palette with effects applied */
    const uint8_t *base_rgba;  /* 1024 bytes: the same palette without them */
    /* The engine's pain accumulator, zero when no damage flash is in force. */
    int32_t damage_level;
    /* Non-zero while the inventory's desaturation is held over the image. */
    int32_t grayscale;
};

int hwr_gl_init(void);
/* Creates the texture store and every GL object drawn through it. `reserve` is
 * the buffer size to allocate (at least texture_size); the slack past
 * texture_size is room for a later per-map block to be written straight into
 * the resident buffer. `prefix_size` is the length of the leading run that
 * outlives a map change -- the store self-test anchors its probe inside that
 * run, so the probe keeps describing bytes no suffix write can have moved.
 * Pass 0 for a store with no reusable prefix. */
int hwr_gl_scene_upload(const uint8_t *texture_bytes, size_t texture_size,
                        size_t reserve, size_t prefix_size,
                        const uint8_t colormap[0x2000],
                        const uint8_t tint[0x2000],
                        const uint8_t glow[0x200],
                        const uint8_t blend[0x10000]);
/* Writes a new tail into the resident texture store, leaving the prefix -- and
 * every GL object built around it -- exactly as uploaded, and refreshes the
 * per-map colour tables. `offset` is where the tail begins; offset + size must
 * fit the reserve the store was created with. Returns 0 when no resident store
 * can take the write, in which case the caller falls back to a full upload. */
int hwr_gl_scene_update_store(size_t offset, const uint8_t *bytes, size_t size,
                              const uint8_t colormap[0x2000],
                              const uint8_t tint[0x2000],
                              const uint8_t glow[0x200],
                              const uint8_t blend[0x10000]);
/* Bytes the resident texture store can hold, or 0 when there is none. */
size_t hwr_gl_scene_store_reserve(void);
/* Rewrites a run of the uploaded texture store in place, for textures whose
 * pixels change over time. Returns 0 when the store is not ready to be
 * rewritten yet, in which case the caller should try again next frame. */
int hwr_gl_scene_update_texels(uint32_t offset, const uint8_t *bytes,
                               uint32_t size);
/* Views are composited in array order into one palette-index target, with
 * reflected views preceding the primary view. Each view gets independent
 * geometry, depth, owner-floor identity, camera state, and an optional
 * horizontal clip. The palette update, persistent-index setup, final resolve,
 * and readback occur once around the whole ordered sequence. */
const uint32_t *hwr_gl_scene_render(const struct hwr_gl_scene_view *views,
                                    size_t view_count,
                                    const struct hwr_gl_palette *palette,
                                    uint32_t width, uint32_t height);
/* Re-resolve the palette-index attachment the last scene render left behind
 * through a new palette and read the result back, without redrawing any
 * geometry. Palette-level effects are engine state that keeps changing while
 * the world stands still, so a held image follows them through this rather
 * than through a pixel transform that cannot be undone. Returns NULL when no
 * resolvable target of the requested size is present. */
const uint32_t *hwr_gl_scene_resolve(const struct hwr_gl_palette *palette,
                                     uint32_t width, uint32_t height);
/* Publishes what the settings layer has worked out about the foundation. The
 * objects it needs come into existence on the first frame that asks for them
 * and go on the first frame that does not, so nothing is allocated, linked, or
 * released from a settings hook. */
void hwr_gl_set_truecolor(const struct hwr_gl_truecolor *state);
/* Whether the last frame drew through the true-colour program, which is what
 * decides whether the submitted canvas can hold a colour the palette has no
 * entry for. */
int hwr_gl_truecolor_active(void);
/* Whether the emitters are to author surface normals. They are, exactly when a
 * shading law that reads one has been asked for; with the original law in force
 * the answer is no and every vertex leaving an emitter is byte for byte the
 * vertex it was before normals existed. */
int hwr_gl_shading_normals(void);
/* Moves the light the player carries on, once per rendered frame, and works out
 * what this map's own candle word does to it. Both answers are one number a
 * frame rather than a term per pixel, so every view of a frame -- a mirror's
 * included -- is lit by the same flame with nothing said about reflections.
 *
 * `candle_glow` is the map's word, zero where there is none. `world_clock` is
 * the engine's own animation clock: a flame moves on the frames the world's
 * time moves and holds where it stops, exactly as an animated surface holds its
 * frame. `now_ns` is the mod's own monotonic clock, which is what the flame
 * actually runs on -- real time rather than a count of ticks, or it would
 * stutter at the cadence being replaced.
 *
 * With the original or the continuous law in force, or with no flame asked for,
 * this leaves the light exactly the light the rows describe. */
void hwr_gl_lamp_advance(uint16_t candle_glow, uint16_t world_clock,
                         uint64_t now_ns);
/* Puts the flame back where it starts and forgets both clocks. */
void hwr_gl_lamp_reset(void);
/* What a map's own candle word makes of a light's strength and its reach, at
 * whatever weight is being given to that word. Both come back at exactly one
 * where the map has no such word or nobody is reading it.
 *
 * It is out here, rather than inside the light it was written for, because the
 * lights that are NOT at the eye take the same word through the same recipe: a
 * map's own authoring should move the light the player carries and the torches
 * on its walls together, and two copies of one recipe are two things that can
 * drift apart without either side noticing. */
void hwr_gl_glow_factors(uint16_t candle_glow, float weight,
                         float *intensity, float *range);
/* Hands over the lights that are not at the eye for the frame about to be
 * submitted. The set is replaced whole, once a frame, and every view of that
 * frame -- a mirror's included -- is lit from it, so a light is a property of
 * the world rather than of the view that happens to see it. Anything past the
 * cap above is dropped; a frame that publishes nothing, which is every frame
 * until something seeds one, leaves the pass with no such light at all. */
void hwr_gl_set_point_lights(const struct hwr_gl_point_light *lights,
                             size_t count);
/* Whether a colour format can be used here. Auto and eight-bit are always
 * available; a format the driver has refused is not. */
int hwr_gl_color_format_available(enum hwr_gl_color_format format);
/* The format the world image is actually being kept in, which is what automatic
 * resolved to and what the driver accepted. Auto means there is no colour world
 * image right now. */
enum hwr_gl_color_format hwr_gl_color_format_in_use(void);
void hwr_gl_scene_clear(void);
/* Hands over the pixels the palette-index target should start from instead of a
 * flat clear, in the same orientation as a scene readback. The next scene render
 * of matching dimensions consumes them, once. Returns 0 on failure, in which
 * case the flat clear stands. */
int hwr_gl_set_index_seed(const uint8_t *indices, uint32_t width, uint32_t height);
/* Declares the standing palette-index target no longer a valid starting point,
 * so the next scene render lays down a pending seed -- or, with none pending, a
 * flat clear -- instead of persisting into whatever is already there. Safe to
 * call before the renderer exists. */
void hwr_gl_invalidate_index_target(void);
/* Declares whether the frame about to be submitted carries any surface that
 * shows the previous frame through its own holes. Only such a surface needs the
 * previous frame kept in colour as well as in palette indices, and keeping it
 * costs one full-frame copy, so a frame that says no does not pay it. Never
 * called means yes: the copy is a cost, not a correctness question. */
void hwr_gl_set_stale_mask(int present);
void hwr_gl_shutdown(void);
const char *hwr_gl_last_error(void);
/* The version and renderer the driver reports, in one line, once a context
 * exists. Empty until then. */
const char *hwr_gl_description(void);
unsigned hwr_gl_drain_errors(void);
/* Copies the palette-index attachment of the last scene render (row-major,
 * same orientation as the submitted frame). Returns 0 on failure. */
int hwr_gl_read_scene_indices(uint8_t *out, uint32_t width, uint32_t height);
/* Overrides the debug visualisation mode (0=off, 1=shade rows, 2=raw texel
 * bytes, 10 = which class a lighting law sorts the fragment into, 11..13 = the
 * three components of the surface normal it would use); -1 re-reads the
 * environment default on the next frame. */
void hwr_gl_set_debug_vis(int mode);

#endif
