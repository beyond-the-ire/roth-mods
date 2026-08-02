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
    /* Engine-space projection retained when the target framebuffer differs
     * from the engine viewport. Portal windows and intrinsic object sizes are
     * authored in this source space. */
    uint16_t source_focal_x;
    uint16_t source_focal_y;
    int32_t source_center_x;
    int32_t source_horizon;
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
 * it falls between; modern leaves the ramp behind and multiplies the texel's own
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
struct hwr_gl_light {
    /* The authored sector byte, as self-illumination. */
    float room_gain;
    float room_gamma;
    /* Where the light total is held: the floor is the darkest anything is
     * allowed to be, the ceiling is where it clips. */
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
    /* Nonzero lights every face rather than only the ones the map marked. */
    int lamp_scope_all;
    /* Props: their own gain, and how far light wraps past their terminator so a
     * standing figure's shaded half never goes fully black. */
    float sprite_gain;
    float sprite_wrap;
};

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
