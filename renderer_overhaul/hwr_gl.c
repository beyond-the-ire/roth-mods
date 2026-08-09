#include "hwr_gl.h"
#include "hwr_perf.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#else
#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#ifndef GL_BGRA
#define GL_BGRA 0x80e1
#endif
#ifndef GL_UNSIGNED_INT_8_8_8_8_REV
#define GL_UNSIGNED_INT_8_8_8_8_REV 0x8367
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8d40
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER 0x8d41
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8ce0
#endif
#ifndef GL_COLOR_ATTACHMENT1
#define GL_COLOR_ATTACHMENT1 0x8ce1
#endif
#ifndef GL_COLOR_ATTACHMENT2
#define GL_COLOR_ATTACHMENT2 0x8ce2
#endif
#ifndef GL_COLOR_ATTACHMENT3
#define GL_COLOR_ATTACHMENT3 0x8ce3
#endif
#ifndef GL_R32UI
#define GL_R32UI 0x8236
#endif
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_RED
#define GL_RED 0x1903
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8cd5
#endif
#ifndef GL_MAX_TEXTURE_BUFFER_SIZE
#define GL_MAX_TEXTURE_BUFFER_SIZE 0x8c2b
#endif
#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881a
#endif
#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT 0x140b
#endif
#ifndef GL_DEPTH_COMPONENT16
#define GL_DEPTH_COMPONENT16 0x81a5
#endif
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81a6
#endif
#ifndef GL_DEPTH_CLAMP
#define GL_DEPTH_CLAMP 0x864f
#endif
#ifndef GL_TEXTURE_COMPARE_MODE
#define GL_TEXTURE_COMPARE_MODE 0x884c
#endif
#ifndef GL_TEXTURE_COMPARE_FUNC
#define GL_TEXTURE_COMPARE_FUNC 0x884d
#endif
#ifndef GL_COMPARE_REF_TO_TEXTURE
#define GL_COMPARE_REF_TO_TEXTURE 0x884e
#endif
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER 0x812d
#endif
#ifndef GL_TEXTURE_BORDER_COLOR
#define GL_TEXTURE_BORDER_COLOR 0x1004
#endif

typedef GLenum (APIENTRY *hwr_gl_get_error_fn)(void);
typedef const GLubyte *(APIENTRY *hwr_gl_get_string_fn)(GLenum);
typedef void (APIENTRY *hwr_gl_viewport_fn)(GLint, GLint, GLsizei, GLsizei);
typedef void (APIENTRY *hwr_gl_scissor_fn)(GLint, GLint, GLsizei, GLsizei);
typedef void (APIENTRY *hwr_gl_clear_color_fn)(GLclampf, GLclampf, GLclampf, GLclampf);
typedef void (APIENTRY *hwr_gl_clear_fn)(GLbitfield);
typedef void (APIENTRY *hwr_gl_read_pixels_fn)(GLint, GLint, GLsizei, GLsizei,
                                               GLenum, GLenum, GLvoid *);
typedef void (APIENTRY *hwr_gl_read_buffer_fn)(GLenum);
typedef void (APIENTRY *hwr_gl_gen_framebuffers_fn)(GLsizei, GLuint *);
typedef void (APIENTRY *hwr_gl_delete_framebuffers_fn)(GLsizei, const GLuint *);
typedef void (APIENTRY *hwr_gl_bind_framebuffer_fn)(GLenum, GLuint);
typedef void (APIENTRY *hwr_gl_gen_renderbuffers_fn)(GLsizei, GLuint *);
typedef void (APIENTRY *hwr_gl_delete_renderbuffers_fn)(GLsizei, const GLuint *);
typedef void (APIENTRY *hwr_gl_bind_renderbuffer_fn)(GLenum, GLuint);
typedef void (APIENTRY *hwr_gl_renderbuffer_storage_fn)(GLenum, GLenum, GLsizei, GLsizei);
typedef void (APIENTRY *hwr_gl_framebuffer_renderbuffer_fn)(GLenum, GLenum, GLenum, GLuint);
typedef GLenum (APIENTRY *hwr_gl_check_framebuffer_status_fn)(GLenum);
typedef void (APIENTRY *hwr_gl_gen_textures_fn)(GLsizei, GLuint *);
typedef void (APIENTRY *hwr_gl_delete_textures_fn)(GLsizei, const GLuint *);
typedef void (APIENTRY *hwr_gl_bind_texture_fn)(GLenum, GLuint);
typedef void (APIENTRY *hwr_gl_tex_parameter_i_fn)(GLenum, GLenum, GLint);
typedef void (APIENTRY *hwr_gl_tex_image_2d_fn)(GLenum, GLint, GLint, GLsizei,
                                                GLsizei, GLint, GLenum, GLenum,
                                                const GLvoid *);
typedef void (APIENTRY *hwr_gl_tex_sub_image_2d_fn)(GLenum, GLint, GLint, GLint,
                                                    GLsizei, GLsizei, GLenum,
                                                    GLenum, const GLvoid *);
typedef void (APIENTRY *hwr_gl_draw_arrays_fn)(GLenum, GLint, GLsizei);
typedef void (APIENTRY *hwr_gl_capability_fn)(GLenum);
typedef void (APIENTRY *hwr_gl_depth_func_fn)(GLenum);
typedef void (APIENTRY *hwr_gl_depth_mask_fn)(GLboolean);
typedef void (APIENTRY *hwr_gl_blend_func_fn)(GLenum, GLenum);
typedef void (APIENTRY *hwr_gl_polygon_offset_fn)(GLfloat, GLfloat);
typedef void (APIENTRY *hwr_gl_polygon_mode_fn)(GLenum, GLenum);
typedef void (APIENTRY *hwr_gl_copy_tex_sub_image_2d_fn)(GLenum, GLint, GLint,
                                                         GLint, GLint, GLint,
                                                         GLsizei, GLsizei);
typedef void (APIENTRY *hwr_gl_pixel_store_i_fn)(GLenum, GLint);
typedef void (APIENTRY *hwr_gl_get_tex_image_fn)(GLenum, GLint, GLenum, GLenum,
                                                 GLvoid *);
typedef void (APIENTRY *hwr_gl_get_integer_v_fn)(GLenum, GLint *);
typedef void (APIENTRY *hwr_gl_tex_parameter_fv_fn)(GLenum, GLenum,
                                                    const GLfloat *);

struct hwr_gl_functions {
    hwr_gl_get_error_fn get_error;
    hwr_gl_get_string_fn get_string;
    hwr_gl_viewport_fn viewport;
    hwr_gl_scissor_fn scissor;
    hwr_gl_clear_color_fn clear_color;
    hwr_gl_clear_fn clear;
    hwr_gl_read_pixels_fn read_pixels;
    /* Which attachment a copy or a read takes its pixels from. The frame's
     * copies all take the palette-index plane, which is where it is left. */
    hwr_gl_read_buffer_fn read_buffer;
    hwr_gl_gen_framebuffers_fn gen_framebuffers;
    hwr_gl_delete_framebuffers_fn delete_framebuffers;
    hwr_gl_bind_framebuffer_fn bind_framebuffer;
    hwr_gl_gen_renderbuffers_fn gen_renderbuffers;
    hwr_gl_delete_renderbuffers_fn delete_renderbuffers;
    hwr_gl_bind_renderbuffer_fn bind_renderbuffer;
    hwr_gl_renderbuffer_storage_fn renderbuffer_storage;
    hwr_gl_framebuffer_renderbuffer_fn framebuffer_renderbuffer;
    hwr_gl_check_framebuffer_status_fn check_framebuffer_status;
    hwr_gl_gen_textures_fn gen_textures;
    hwr_gl_delete_textures_fn delete_textures;
    hwr_gl_bind_texture_fn bind_texture;
    hwr_gl_tex_parameter_i_fn tex_parameter_i;
    hwr_gl_tex_image_2d_fn tex_image_2d;
    hwr_gl_tex_sub_image_2d_fn tex_sub_image_2d;
    PFNGLACTIVETEXTUREPROC active_texture;
    PFNGLGENBUFFERSPROC gen_buffers;
    PFNGLDELETEBUFFERSPROC delete_buffers;
    PFNGLBINDBUFFERPROC bind_buffer;
    PFNGLBUFFERDATAPROC buffer_data;
    PFNGLBUFFERSUBDATAPROC buffer_sub_data;
    PFNGLTEXBUFFERPROC tex_buffer;
    PFNGLCREATESHADERPROC create_shader;
    PFNGLSHADERSOURCEPROC shader_source;
    PFNGLCOMPILESHADERPROC compile_shader;
    PFNGLGETSHADERIVPROC get_shader_iv;
    PFNGLGETSHADERINFOLOGPROC get_shader_info_log;
    PFNGLDELETESHADERPROC delete_shader;
    PFNGLCREATEPROGRAMPROC create_program;
    PFNGLATTACHSHADERPROC attach_shader;
    PFNGLLINKPROGRAMPROC link_program;
    PFNGLGETPROGRAMIVPROC get_program_iv;
    PFNGLGETPROGRAMINFOLOGPROC get_program_info_log;
    PFNGLDELETEPROGRAMPROC delete_program;
    PFNGLUSEPROGRAMPROC use_program;
    PFNGLGETUNIFORMLOCATIONPROC get_uniform_location;
    PFNGLUNIFORM1IPROC uniform_1i;
    PFNGLUNIFORM2IPROC uniform_2i;
    PFNGLUNIFORM1FPROC uniform_1f;
    PFNGLUNIFORM2FPROC uniform_2f;
    PFNGLUNIFORM3FPROC uniform_3f;
    PFNGLUNIFORM2FVPROC uniform_2fv;
    PFNGLUNIFORM4FVPROC uniform_4fv;
    PFNGLGENVERTEXARRAYSPROC gen_vertex_arrays;
    PFNGLDELETEVERTEXARRAYSPROC delete_vertex_arrays;
    PFNGLBINDVERTEXARRAYPROC bind_vertex_array;
    PFNGLENABLEVERTEXATTRIBARRAYPROC enable_vertex_attrib_array;
    PFNGLVERTEXATTRIBPOINTERPROC vertex_attrib_pointer;
    PFNGLVERTEXATTRIBIPOINTERPROC vertex_attrib_i_pointer;
    PFNGLFRAMEBUFFERTEXTURE2DPROC framebuffer_texture_2d;
    hwr_gl_draw_arrays_fn draw_arrays;
    hwr_gl_capability_fn enable;
    hwr_gl_capability_fn disable;
    hwr_gl_depth_func_fn depth_func;
    hwr_gl_depth_mask_fn depth_mask;
    hwr_gl_blend_func_fn blend_func;
    hwr_gl_polygon_offset_fn polygon_offset;
    hwr_gl_polygon_mode_fn polygon_mode;
    PFNGLCLEARBUFFERUIVPROC clear_buffer_uiv;
    PFNGLCLEARBUFFERFVPROC clear_buffer_fv;
    PFNGLDRAWBUFFERSPROC draw_buffers;
    hwr_gl_copy_tex_sub_image_2d_fn copy_tex_sub_image_2d;
    hwr_gl_pixel_store_i_fn pixel_store_i;
    hwr_gl_get_tex_image_fn get_tex_image;
    PFNGLGETBUFFERSUBDATAPROC get_buffer_sub_data;
    hwr_gl_get_integer_v_fn get_integer_v;
    PFNGLGETUNIFORMIVPROC get_uniform_iv;
    PFNGLUNIFORMMATRIX4FVPROC uniform_matrix_4fv;
    hwr_gl_tex_parameter_fv_fn tex_parameter_fv;
};

static struct hwr_gl_functions g_gl;
static GLuint g_fbo;
static GLuint g_color_buffer;
static GLuint g_index_fbo;
static int g_void_persist = -1;
/* Set whenever the index target is (re)created, so it starts from a
 * defined state before any frame is allowed to persist into it. */
static int g_index_target_dirty = 1;
/* Pixels waiting to be laid into the index target in place of the flat clear,
 * and the size they were built for. */
static uint8_t *g_index_seed;
static uint32_t g_index_seed_width;
static uint32_t g_index_seed_height;
/* Whether the holding buffer currently carries a seed waiting to be laid down.
 * The buffer itself outlives each seed, because the canvas size does not change
 * between them and re-taking canvas-sized storage at every map change, return
 * to gameplay, and inventory close is pure churn. */
static int g_index_seed_ready;
static GLuint g_index_texture;
/* Second colour attachment of the index FBO: per-pixel owner-floor identity of
 * the opaque winner (FLOOR_TAG | (floor_z & 0xffff) for a flat floor, else 0),
 * read by the floor-exception passes so an object may overlay its OWN sector
 * floor while walls and nearer geometry still occlude it. */
static GLuint g_floor_id_texture;
static GLuint g_index_copy_texture;
/* Snapshot of the index target as the frame STARTS (the persisted previous
 * frame), for surfaces whose engine counterpart reads pixels nothing repaints
 * this frame.  g_index_copy_texture cannot serve: it is recopied mid-frame,
 * after opaque geometry, for the translucent pass. */
static GLuint g_stale_copy_texture;
static GLuint g_depth_buffer;
static GLuint g_texture_buffer;
static GLuint g_texture_buffer_texture;
static GLuint g_colormap_texture;
static GLuint g_tint_texture;
static GLuint g_glow_texture;
static GLuint g_blend_texture;
static GLuint g_palette_texture;
static GLuint g_scene_vbo;
static GLuint g_scene_vao;
static GLuint g_resolve_vao;
static GLuint g_geometry_program;
static GLuint g_sky_program;
static GLuint g_resolve_program;
static GLuint g_probe_program;
/* The true-colour foundation. The enhanced world program is the authentic
 * source compiled a second time with the define set; it, the colour attachment
 * it writes, and the passes that prime and resolve that attachment come into
 * existence only when the foundation is required, and nothing on the authentic
 * path knows they exist. */
static GLuint g_truecolor_program;
static GLuint g_truecolor_resolve_program;
static GLuint g_index_restore_program;
static GLuint g_color_texture;
/* The colour twin of g_stale_copy_texture: the same snapshot of the same
 * moment, in the space the world image is composed in. A surface that shows
 * the previous frame through its holes has to show the previous frame's own
 * COLOUR, not the colour its index resolves to under a palette -- those are
 * the same thing under the original law and are not under any other, where the
 * second one is a hole of full daylight in a darkened room. Carried only while
 * the picture is a composed one and the frame has a surface that reads it. */
static GLuint g_stale_color_texture;
/* Whether that snapshot describes this frame's starting plane, which is what
 * the world pass is told before it decides to sample it. */
static int g_stale_color_valid;
/* What the frame was told about surfaces that read the previous frame, before
 * it was told anything: assume there are some. The copy is a cost, never a
 * correctness question, so the answer nobody has given yet is the safe one. */
static int g_stale_mask_present = 1;
/* One channel per pixel: how much of the light the player carries got past
 * whatever the eye is looking THROUGH there. The light sits at the eye, so the
 * ray it travels is the ray the view travels, and a plane of what the view
 * passed through is that light's own shadow map -- exactly, for every surface
 * the opaque pass drew. Written by one pass over the see-through geometry and
 * read where the lamp is composed. */
static GLuint g_transmit_texture;
/* How much of what is behind it a blended texel lets through: the weight the
 * map's own blend table gives its destination, measured off that table and the
 * palette it is authored against rather than assumed. A half until a table has
 * been measured, which is where the engine's own tables sit. */
static float g_transmit_blend = 0.5f;
static int g_transmit_blend_pending = 1;
/* The blend table as the map handed it over, kept because the weight above is a
 * property of the table and the palette together and the palette arrives with
 * the frame rather than with the map. */
static uint8_t g_blend_bytes[0x10000];
static int g_blend_bytes_valid;
/* The sun/moon key light and its light-space depth map: the frame's published
 * state, the caster geometry it is rendered from, and the GL objects that
 * exist only while a frame has published one. The dynamic slice is a borrowed
 * pointer valid for the frame it was handed over in -- it is streamed into the
 * pass's own buffer at the render and never read after it. */
static struct hwr_gl_sun g_sun;
static GLuint g_sun_program;
static GLuint g_sun_line_program;
static GLuint g_sun_fbo;
static GLuint g_sun_depth_texture;
/* The transmittance plane beside it -- the gobo -- allocated only on the
 * frames the texels row asks for one (R8, a quarter of the depth map's
 * bytes) and freed the moment it stops. */
static GLuint g_sun_gobo_texture;
static int g_sun_gobo_valid;
static int g_sun_depth_resolution;
static GLuint g_sun_static_vao;
static GLuint g_sun_static_vbo;
static size_t g_sun_static_count;
/* Where the slice's two runs meet: the depth pass draws the whole slice
 * (the translucent range keeps depth only at its opaque paint), the gobo
 * pass [gobo_first, count) -- the translucent range alone. */
static size_t g_sun_static_gobo_first;
/* The static slice waiting for a context, so a caster set built before the
 * first frame is not lost: uploaded on the first sun render that finds it. */
static struct hwr_gl_sun_vertex *g_sun_static_pending;
static size_t g_sun_static_pending_count;
static size_t g_sun_static_pending_gobo_first;
static int g_sun_static_pending_valid;
static GLuint g_sun_dynamic_vao;
static GLuint g_sun_dynamic_vbo;
static const struct hwr_gl_sun_vertex *g_sun_dynamic;
static size_t g_sun_dynamic_count;
/* Whether a depth map was laid down for the frame being rendered. The lookup
 * is gated on it, so a frame the pass stood down for reads factor one
 * everywhere -- and the sealed-map gate reads it from outside. */
static int g_sun_map_valid;
/* Set once the sun program has failed to come up, so it is never retried. */
static int g_sun_failed;

/* Whether the colour plane still describes the same picture the index plane
 * does. The colour target persists exactly as the index target does -- that is
 * what makes the void, the wings and the previous frame a stale-masked wall
 * shows coherent in colour space -- so it is primed from the index plane only
 * where the index plane itself restarted, or where what a colour MEANS has
 * changed underneath it. */
static int g_color_prime_pending = 1;
/* The law the standing colour plane was composed under, so a law that moves
 * without the plane restarting is one of those changes. */
static enum hwr_gl_shading_mode g_color_prime_mode = HWR_GL_SHADING_AUTHENTIC;
/* The palette WITHOUT the engine's palette-level effects. The enhanced program
 * composes in it and the enhanced resolve applies the effects to the colour
 * afterwards, so the colours a map authored are what a lighting term is
 * evaluated against and the index plane stays keyed on one palette while a
 * damage flash changes the DAC every frame. */
static GLuint g_base_palette_texture;
/* Cached HWR_TRUECOLOR gate; -1 until first read. */
static int g_truecolor_env = -1;
/* Cached HWR_SHADE_RAMP gate; -1 until first read. */
static int g_shade_ramp_env = -1;
/* Cached gates for the three parts of the still, round, per-fragment candle.
 * Each one names the part of the original pool it departs from -- the light
 * position that moves, the metric with corners, the sampling at polygon corners
 * -- so what the still pool owes to each can be told apart.
 *
 * Three answers rather than two: HWR_SHADE_PART_UNREAD until the handle has
 * been looked at, then -1 where nothing asked either way and the law that is
 * running decides, and 0 or 1 where something asked outright. Which law asks
 * for which part is hwr_shade_part_wanted() below. */
#define HWR_SHADE_PART_UNREAD (-2)
static int g_shade_origin_fixed_env = HWR_SHADE_PART_UNREAD;
static int g_shade_radial_round_env = HWR_SHADE_PART_UNREAD;
static int g_shade_per_fragment_env = HWR_SHADE_PART_UNREAD;
/* And the handle that puts the light the player carries back through every
 * see-through surface whole, which is where it was before the pass that stops
 * it existed; -1 until first read. */
static int g_transmit_env = -1;
/* The working space a non-authentic law composes in; -1 until first read. The
 * smooth law resolves colour between two entries of an authored ramp, which is a
 * display-space operation by definition, so it stays where the palette lives and
 * this is off. A law that ADDS light has to add it where light adds, and this is
 * the handle that holds the round trip against the picture it must not move
 * before there is a term depending on it. */
static int g_shade_linear_env = -1;
/* And whether the same handle was set to zero outright, which is what turns the
 * round trip OFF for a law that would otherwise compose in linear because it has
 * to. Kept apart from the value above so that "nobody asked" and "somebody
 * refused" stay two different answers. */
static int g_shade_linear_denied_env = -1;
/* Raised while this frame renders through the enhanced program. */
static int g_truecolor_active;
/* Set once the foundation has failed to come up, so it is never retried. */
static int g_truecolor_failed;
static int g_truecolor_logged;
/* What the settings layer last asked for. */
static struct hwr_gl_truecolor g_truecolor_want = {
    .required = 0,
    .neutral = 1,
    .gain = 1.0f,
    .contrast = 1.0f,
    .format = HWR_GL_COLOR_AUTO,
    .shading_mode = HWR_GL_SHADING_AUTHENTIC,
    /* The rig at the values its rows hold, so the state is a sane rig from the
     * first frame rather than one whose reach is zero. Nothing reads it until a
     * law that composes with it is asked for. */
    .light = { .room_gain = 1.0f, .room_gamma = 2.2f, .world_ceiling = 1.0f,
               /* The engine law at the rows' own values, which is the map read
                * exactly as authored. The anchored law's own state is carried
                * beside it at the pair the fit falls back to while no map has
                * been read -- the written anchors, taken to the exponent -- so
                * selecting it needs nothing filled in first. */
               .room_curve = HWR_GL_ROOM_CURVE_ENGINE, .room_reach = 1.0f,
               .room_anchor_low = 0.745448649f,
               .room_anchor_high = 2.90989566f, .room_dark = 0.02f,
               .room_lit = 1.0f, .room_shape = 1.0f,
               .ambient_color = { 0.4f, 0.4f, 0.4f },
               .lamp_color = { 1.0f, 237.0f / 255.0f, 224.0f / 255.0f },
               .lamp_range = 800.0f, .sprite_gain = 1.0f, .sprite_wrap = 0.5f },
    .dither = 0
};
/* The colour format in use, and the ones this driver has refused. */
static enum hwr_gl_color_format g_truecolor_format = HWR_GL_COLOR_AUTO;
static int g_truecolor_refused_rgba16f;
/* The palette-level effects this frame's resolve has to reproduce: the damage
 * flash's per-channel fade (negative while none is in force) and the inventory
 * desaturation. */
static int g_effect_fade = -1;
static int g_effect_grayscale;
/* The untinted palette as it was last uploaded, so the few pixels the sampling
 * self-test writes straight into the index plane can be given the colour they
 * resolve to without a pass that would have to sample the plane it writes. */
static uint8_t g_base_palette_bytes[1024];
static int g_base_palette_valid;
/* The acceptance instrument: HWR_TRUECOLOR_COMPARE=<path-prefix> renders the
 * frame's view set twice -- once through the authentic program, once through
 * the enhanced one -- from identical state, and counts differing canvas pixels.
 * HWR_TRUECOLOR_COMPARE_BREAK=1 detunes the enhanced arm so the instrument has
 * to report a non-zero count; an instrument that has never failed has not been
 * shown to work. */
static char g_truecolor_compare_path[2048];
static int g_truecolor_compare_state = -1;
static int g_truecolor_break;
static uint32_t *g_truecolor_compare_pixels;
static size_t g_truecolor_compare_bytes;
static unsigned g_truecolor_compare_frames;
static unsigned g_truecolor_compare_bad_frames;
static unsigned g_truecolor_compare_dumps;
static uint64_t g_truecolor_compare_bad_pixels;
static unsigned g_truecolor_compare_max_delta;
static int g_scene_uploaded;
/* Reference data for the sampling self-test: a rare state has been observed
 * where every lookup-table fetch returns zero (the world renders black while
 * solid-fill surfaces stay visible). The self-test splits "the uploaded data
 * is gone" from "the data is intact but sampling it fails". */
static uint8_t g_verify_colormap[0x2000];
static uint32_t g_verify_store_hash;
/* Set only while the recorded hash still describes the whole store. A store
 * whose tail has been rewritten in place no longer has one, and says so rather
 * than reporting a mismatch it caused itself. */
static int g_verify_store_hash_valid;
static size_t g_verify_store_size;
/* Bytes the texture-store buffer object was allocated with. The used length is
 * g_verify_store_size; the difference is headroom that a per-map tail is
 * written into without disturbing anything before it. */
static size_t g_store_reserve;
static int g_verify_probe_offset;
static uint8_t g_verify_probe_texel;
static uint8_t g_verify_first_texel;
static uint8_t g_verify_last_texel;
static int g_verify_pending;
static int g_verify_last_status = -1;
static unsigned g_render_count;
static int g_debug_vis = -1;   /* HWR_SHADE_VIS=1: render the shade-row map */
static int g_sky_occlude = -1; /* HWR_SKY_OCCLUDE=0 disables the sky occlusion pass */
static uint32_t g_fbo_width;
static uint32_t g_fbo_height;
static uint32_t *g_pixels;
static size_t g_pixels_bytes;
static int g_initialized;
static char g_error[192] = "OpenGL is not initialized";
/* What the driver calls itself, for the settings overlay to report. */
static char g_description[192];

#ifdef _WIN32

#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

typedef HGLRC (WINAPI *hwr_wgl_create_context_attribs_fn)(HDC, HGLRC, const int *);

static const char g_window_class[] = "ROTHC_RendererOverhaul_GL";
static HINSTANCE g_instance;
static HWND g_window;
static HDC g_dc;
static HGLRC g_context;

static void hwr_set_platform_error(const char *what)
{
    snprintf(g_error, sizeof g_error, "%s (Win32 error %lu)",
             what, (unsigned long)GetLastError());
}

static void *hwr_platform_get_proc(const char *name)
{
    PROC proc = wglGetProcAddress(name);
    uintptr_t value = (uintptr_t)proc;
    if (value == 0 || value == 1 || value == 2 || value == 3 || value == UINTPTR_MAX) {
        HMODULE module = GetModuleHandleA("opengl32.dll");
        proc = module != NULL ? GetProcAddress(module, name) : NULL;
    }
    return (void *)(uintptr_t)proc;
}

static int hwr_platform_init(void)
{
    WNDCLASSA wc;
    PIXELFORMATDESCRIPTOR pfd;
    HGLRC legacy;
    int pixel_format;
    hwr_wgl_create_context_attribs_fn create_context_attribs;
    const int context_attributes[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };

    g_instance = GetModuleHandleA(NULL);
    memset(&wc, 0, sizeof wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = g_instance;
    wc.lpszClassName = g_window_class;
    if (RegisterClassA(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        hwr_set_platform_error("RegisterClassA failed");
        return 0;
    }

    g_window = CreateWindowExA(0, g_window_class, "", WS_OVERLAPPEDWINDOW,
                               0, 0, 1, 1, NULL, NULL, g_instance, NULL);
    if (g_window == NULL) {
        hwr_set_platform_error("CreateWindowExA failed");
        return 0;
    }
    g_dc = GetDC(g_window);
    if (g_dc == NULL) {
        hwr_set_platform_error("GetDC failed");
        return 0;
    }

    memset(&pfd, 0, sizeof pfd);
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;
    pixel_format = ChoosePixelFormat(g_dc, &pfd);
    if (pixel_format == 0 || !SetPixelFormat(g_dc, pixel_format, &pfd)) {
        hwr_set_platform_error("setting the OpenGL pixel format failed");
        return 0;
    }

    legacy = wglCreateContext(g_dc);
    if (legacy == NULL || !wglMakeCurrent(g_dc, legacy)) {
        hwr_set_platform_error("creating the bootstrap OpenGL context failed");
        if (legacy != NULL)
            wglDeleteContext(legacy);
        return 0;
    }
    create_context_attribs = (hwr_wgl_create_context_attribs_fn)
        hwr_platform_get_proc("wglCreateContextAttribsARB");
    if (create_context_attribs == NULL) {
        snprintf(g_error, sizeof g_error, "WGL_ARB_create_context is unavailable");
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(legacy);
        return 0;
    }
    g_context = create_context_attribs(g_dc, NULL, context_attributes);
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(legacy);
    if (g_context == NULL || !wglMakeCurrent(g_dc, g_context)) {
        hwr_set_platform_error("creating an OpenGL 3.3 core context failed");
        return 0;
    }
    return 1;
}

static int hwr_platform_make_current(void)
{
    return g_dc != NULL && g_context != NULL && wglMakeCurrent(g_dc, g_context);
}

static void hwr_platform_shutdown(void)
{
    wglMakeCurrent(NULL, NULL);
    if (g_context != NULL) {
        wglDeleteContext(g_context);
        g_context = NULL;
    }
    if (g_dc != NULL && g_window != NULL) {
        ReleaseDC(g_window, g_dc);
        g_dc = NULL;
    }
    if (g_window != NULL) {
        DestroyWindow(g_window);
        g_window = NULL;
    }
    if (g_instance != NULL)
        UnregisterClassA(g_window_class, g_instance);
    g_instance = NULL;
}

#else

#define EGL_PLATFORM_DEVICE_EXT 0x313f
#define EGL_CONTEXT_MAJOR_VERSION_KHR 0x3098
#define EGL_CONTEXT_MINOR_VERSION_KHR 0x30fb
#define EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR 0x30fd
#define EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR 0x00000001

typedef void *EGLDeviceEXT;
typedef EGLBoolean (*hwr_egl_query_devices_fn)(EGLint, EGLDeviceEXT *, EGLint *);
typedef EGLDisplay (*hwr_egl_get_platform_display_fn)(EGLenum, void *, const EGLint *);
typedef const char *(*hwr_egl_query_device_string_fn)(EGLDeviceEXT, EGLint);

static EGLDisplay g_display = EGL_NO_DISPLAY;
static EGLContext g_context = EGL_NO_CONTEXT;
static EGLSurface g_surface = EGL_NO_SURFACE;

static int hwr_has_extension(const char *extensions, const char *wanted)
{
    size_t n;
    const char *p;
    if (extensions == NULL || wanted == NULL || strchr(wanted, ' ') != NULL)
        return 0;
    n = strlen(wanted);
    p = extensions;
    while ((p = strstr(p, wanted)) != NULL) {
        if ((p == extensions || p[-1] == ' ') && (p[n] == 0 || p[n] == ' '))
            return 1;
        p += n;
    }
    return 0;
}

static int hwr_initialize_display(EGLDisplay display)
{
    EGLint major;
    EGLint minor;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor))
        return 0;
    g_display = display;
    return 1;
}

static int hwr_try_device_display(void)
{
    hwr_egl_query_devices_fn query_devices;
    hwr_egl_get_platform_display_fn get_platform_display;
    hwr_egl_query_device_string_fn query_device_string;
    EGLDeviceEXT devices[16];
    EGLint count = 0;
    EGLint i;
    int pass;

    query_devices = (hwr_egl_query_devices_fn)eglGetProcAddress("eglQueryDevicesEXT");
    get_platform_display = (hwr_egl_get_platform_display_fn)
        eglGetProcAddress("eglGetPlatformDisplayEXT");
    query_device_string = (hwr_egl_query_device_string_fn)
        eglGetProcAddress("eglQueryDeviceStringEXT");
    if (query_devices == NULL || get_platform_display == NULL ||
        !query_devices((EGLint)(sizeof devices / sizeof devices[0]), devices, &count))
        return 0;
    /*
     * EGL device enumeration may include Mesa's CPU rasterizer alongside real
     * DRM devices.  Try every non-software device first, preserving the
     * implementation's preferred GPU order, and retain software as a last
     * resort for systems with no working hardware EGL stack.
     */
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < count; i++) {
            const char *device_extensions =
                query_device_string != NULL
                    ? query_device_string(devices[i], EGL_EXTENSIONS)
                    : NULL;
            int software = hwr_has_extension(device_extensions,
                                             "EGL_MESA_device_software");
            EGLDisplay display;
            if ((pass == 0 && software) || (pass != 0 && !software))
                continue;
            display = get_platform_display(EGL_PLATFORM_DEVICE_EXT, devices[i], NULL);
            if (hwr_initialize_display(display))
                return 1;
        }
    }
    return 0;
}

static void hwr_set_egl_error(const char *what)
{
    snprintf(g_error, sizeof g_error, "%s (EGL error 0x%x)",
             what, (unsigned int)eglGetError());
}

static void *hwr_platform_get_proc(const char *name)
{
    return (void *)eglGetProcAddress(name);
}

static int hwr_platform_init(void)
{
    EGLConfig config;
    EGLint config_count = 0;
    const EGLint config_attributes[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    const EGLint context_attributes[] = {
        EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
        EGL_CONTEXT_MINOR_VERSION_KHR, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
        EGL_NONE
    };
    const EGLint pbuffer_attributes[] = {
        EGL_WIDTH, 1,
        EGL_HEIGHT, 1,
        EGL_NONE
    };
    const char *extensions;

    /*
     * Prefer an enumerated render device to EGL_DEFAULT_DISPLAY.  On systems
     * with both a hardware vendor stack and Mesa installed, the 32-bit
     * default-display path can successfully initialize Mesa's llvmpipe even
     * though a hardware EGL device is available.  The device path above
     * explicitly demotes EGL_MESA_device_software candidates.
     */
    if (!hwr_try_device_display() &&
        !hwr_initialize_display(eglGetDisplay(EGL_DEFAULT_DISPLAY))) {
        hwr_set_egl_error("no EGL display could be initialized");
        return 0;
    }
    if (!eglBindAPI(EGL_OPENGL_API)) {
        hwr_set_egl_error("binding the EGL OpenGL API failed");
        return 0;
    }
    if (!eglChooseConfig(g_display, config_attributes, &config, 1, &config_count) ||
        config_count < 1) {
        hwr_set_egl_error("no EGL OpenGL pbuffer configuration is available");
        return 0;
    }
    g_context = eglCreateContext(g_display, config, EGL_NO_CONTEXT, context_attributes);
    if (g_context == EGL_NO_CONTEXT) {
        hwr_set_egl_error("creating an OpenGL 3.3 core context failed");
        return 0;
    }

    g_surface = eglCreatePbufferSurface(g_display, config, pbuffer_attributes);
    if (g_surface == EGL_NO_SURFACE) {
        extensions = eglQueryString(g_display, EGL_EXTENSIONS);
        if (!hwr_has_extension(extensions, "EGL_KHR_surfaceless_context")) {
            hwr_set_egl_error("creating the EGL pbuffer surface failed");
            return 0;
        }
    }
    if (!eglMakeCurrent(g_display, g_surface, g_surface, g_context)) {
        hwr_set_egl_error("making the EGL context current failed");
        return 0;
    }
    return 1;
}

static int hwr_platform_make_current(void)
{
    return g_display != EGL_NO_DISPLAY && g_context != EGL_NO_CONTEXT &&
           eglMakeCurrent(g_display, g_surface, g_surface, g_context);
}

static void hwr_platform_shutdown(void)
{
    if (g_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_surface != EGL_NO_SURFACE)
            eglDestroySurface(g_display, g_surface);
        if (g_context != EGL_NO_CONTEXT)
            eglDestroyContext(g_display, g_context);
        eglTerminate(g_display);
    }
    g_surface = EGL_NO_SURFACE;
    g_context = EGL_NO_CONTEXT;
    g_display = EGL_NO_DISPLAY;
}

#endif

#define HWR_LOAD_GL(member, type, symbol)                                      \
    do {                                                                        \
        g_gl.member = (type)hwr_platform_get_proc(symbol);                      \
        if (g_gl.member == NULL) {                                              \
            snprintf(g_error, sizeof g_error, "required OpenGL entry point %s is unavailable", symbol); \
            return 0;                                                           \
        }                                                                       \
    } while (0)

static int hwr_load_gl_functions(void)
{
    HWR_LOAD_GL(get_error, hwr_gl_get_error_fn, "glGetError");
    HWR_LOAD_GL(get_string, hwr_gl_get_string_fn, "glGetString");
    HWR_LOAD_GL(viewport, hwr_gl_viewport_fn, "glViewport");
    HWR_LOAD_GL(scissor, hwr_gl_scissor_fn, "glScissor");
    HWR_LOAD_GL(clear_color, hwr_gl_clear_color_fn, "glClearColor");
    HWR_LOAD_GL(clear, hwr_gl_clear_fn, "glClear");
    HWR_LOAD_GL(read_pixels, hwr_gl_read_pixels_fn, "glReadPixels");
    HWR_LOAD_GL(read_buffer, hwr_gl_read_buffer_fn, "glReadBuffer");
    HWR_LOAD_GL(gen_framebuffers, hwr_gl_gen_framebuffers_fn, "glGenFramebuffers");
    HWR_LOAD_GL(delete_framebuffers, hwr_gl_delete_framebuffers_fn, "glDeleteFramebuffers");
    HWR_LOAD_GL(bind_framebuffer, hwr_gl_bind_framebuffer_fn, "glBindFramebuffer");
    HWR_LOAD_GL(gen_renderbuffers, hwr_gl_gen_renderbuffers_fn, "glGenRenderbuffers");
    HWR_LOAD_GL(delete_renderbuffers, hwr_gl_delete_renderbuffers_fn, "glDeleteRenderbuffers");
    HWR_LOAD_GL(bind_renderbuffer, hwr_gl_bind_renderbuffer_fn, "glBindRenderbuffer");
    HWR_LOAD_GL(renderbuffer_storage, hwr_gl_renderbuffer_storage_fn, "glRenderbufferStorage");
    HWR_LOAD_GL(framebuffer_renderbuffer, hwr_gl_framebuffer_renderbuffer_fn, "glFramebufferRenderbuffer");
    HWR_LOAD_GL(check_framebuffer_status, hwr_gl_check_framebuffer_status_fn, "glCheckFramebufferStatus");
    HWR_LOAD_GL(gen_textures, hwr_gl_gen_textures_fn, "glGenTextures");
    HWR_LOAD_GL(delete_textures, hwr_gl_delete_textures_fn, "glDeleteTextures");
    HWR_LOAD_GL(bind_texture, hwr_gl_bind_texture_fn, "glBindTexture");
    HWR_LOAD_GL(tex_parameter_i, hwr_gl_tex_parameter_i_fn, "glTexParameteri");
    HWR_LOAD_GL(tex_image_2d, hwr_gl_tex_image_2d_fn, "glTexImage2D");
    HWR_LOAD_GL(tex_sub_image_2d, hwr_gl_tex_sub_image_2d_fn, "glTexSubImage2D");
    HWR_LOAD_GL(active_texture, PFNGLACTIVETEXTUREPROC, "glActiveTexture");
    HWR_LOAD_GL(gen_buffers, PFNGLGENBUFFERSPROC, "glGenBuffers");
    HWR_LOAD_GL(delete_buffers, PFNGLDELETEBUFFERSPROC, "glDeleteBuffers");
    HWR_LOAD_GL(bind_buffer, PFNGLBINDBUFFERPROC, "glBindBuffer");
    HWR_LOAD_GL(buffer_data, PFNGLBUFFERDATAPROC, "glBufferData");
    HWR_LOAD_GL(buffer_sub_data, PFNGLBUFFERSUBDATAPROC, "glBufferSubData");
    HWR_LOAD_GL(tex_buffer, PFNGLTEXBUFFERPROC, "glTexBuffer");
    HWR_LOAD_GL(create_shader, PFNGLCREATESHADERPROC, "glCreateShader");
    HWR_LOAD_GL(shader_source, PFNGLSHADERSOURCEPROC, "glShaderSource");
    HWR_LOAD_GL(compile_shader, PFNGLCOMPILESHADERPROC, "glCompileShader");
    HWR_LOAD_GL(get_shader_iv, PFNGLGETSHADERIVPROC, "glGetShaderiv");
    HWR_LOAD_GL(get_shader_info_log, PFNGLGETSHADERINFOLOGPROC, "glGetShaderInfoLog");
    HWR_LOAD_GL(delete_shader, PFNGLDELETESHADERPROC, "glDeleteShader");
    HWR_LOAD_GL(create_program, PFNGLCREATEPROGRAMPROC, "glCreateProgram");
    HWR_LOAD_GL(attach_shader, PFNGLATTACHSHADERPROC, "glAttachShader");
    HWR_LOAD_GL(link_program, PFNGLLINKPROGRAMPROC, "glLinkProgram");
    HWR_LOAD_GL(get_program_iv, PFNGLGETPROGRAMIVPROC, "glGetProgramiv");
    HWR_LOAD_GL(get_program_info_log, PFNGLGETPROGRAMINFOLOGPROC, "glGetProgramInfoLog");
    HWR_LOAD_GL(delete_program, PFNGLDELETEPROGRAMPROC, "glDeleteProgram");
    HWR_LOAD_GL(use_program, PFNGLUSEPROGRAMPROC, "glUseProgram");
    HWR_LOAD_GL(get_uniform_location, PFNGLGETUNIFORMLOCATIONPROC, "glGetUniformLocation");
    HWR_LOAD_GL(uniform_1i, PFNGLUNIFORM1IPROC, "glUniform1i");
    HWR_LOAD_GL(uniform_2i, PFNGLUNIFORM2IPROC, "glUniform2i");
    HWR_LOAD_GL(uniform_1f, PFNGLUNIFORM1FPROC, "glUniform1f");
    HWR_LOAD_GL(uniform_2f, PFNGLUNIFORM2FPROC, "glUniform2f");
    HWR_LOAD_GL(uniform_3f, PFNGLUNIFORM3FPROC, "glUniform3f");
    HWR_LOAD_GL(uniform_2fv, PFNGLUNIFORM2FVPROC, "glUniform2fv");
    HWR_LOAD_GL(uniform_4fv, PFNGLUNIFORM4FVPROC, "glUniform4fv");
    HWR_LOAD_GL(gen_vertex_arrays, PFNGLGENVERTEXARRAYSPROC, "glGenVertexArrays");
    HWR_LOAD_GL(delete_vertex_arrays, PFNGLDELETEVERTEXARRAYSPROC, "glDeleteVertexArrays");
    HWR_LOAD_GL(bind_vertex_array, PFNGLBINDVERTEXARRAYPROC, "glBindVertexArray");
    HWR_LOAD_GL(enable_vertex_attrib_array, PFNGLENABLEVERTEXATTRIBARRAYPROC, "glEnableVertexAttribArray");
    HWR_LOAD_GL(vertex_attrib_pointer, PFNGLVERTEXATTRIBPOINTERPROC, "glVertexAttribPointer");
    HWR_LOAD_GL(vertex_attrib_i_pointer, PFNGLVERTEXATTRIBIPOINTERPROC, "glVertexAttribIPointer");
    HWR_LOAD_GL(framebuffer_texture_2d, PFNGLFRAMEBUFFERTEXTURE2DPROC, "glFramebufferTexture2D");
    HWR_LOAD_GL(draw_arrays, hwr_gl_draw_arrays_fn, "glDrawArrays");
    HWR_LOAD_GL(enable, hwr_gl_capability_fn, "glEnable");
    HWR_LOAD_GL(disable, hwr_gl_capability_fn, "glDisable");
    HWR_LOAD_GL(depth_func, hwr_gl_depth_func_fn, "glDepthFunc");
    HWR_LOAD_GL(depth_mask, hwr_gl_depth_mask_fn, "glDepthMask");
    HWR_LOAD_GL(blend_func, hwr_gl_blend_func_fn, "glBlendFunc");
    HWR_LOAD_GL(polygon_offset, hwr_gl_polygon_offset_fn, "glPolygonOffset");
    HWR_LOAD_GL(polygon_mode, hwr_gl_polygon_mode_fn, "glPolygonMode");
    HWR_LOAD_GL(clear_buffer_uiv, PFNGLCLEARBUFFERUIVPROC, "glClearBufferuiv");
    HWR_LOAD_GL(clear_buffer_fv, PFNGLCLEARBUFFERFVPROC, "glClearBufferfv");
    HWR_LOAD_GL(draw_buffers, PFNGLDRAWBUFFERSPROC, "glDrawBuffers");
    HWR_LOAD_GL(copy_tex_sub_image_2d, hwr_gl_copy_tex_sub_image_2d_fn, "glCopyTexSubImage2D");
    HWR_LOAD_GL(pixel_store_i, hwr_gl_pixel_store_i_fn, "glPixelStorei");
    HWR_LOAD_GL(get_tex_image, hwr_gl_get_tex_image_fn, "glGetTexImage");
    HWR_LOAD_GL(get_buffer_sub_data, PFNGLGETBUFFERSUBDATAPROC, "glGetBufferSubData");
    HWR_LOAD_GL(get_integer_v, hwr_gl_get_integer_v_fn, "glGetIntegerv");
    HWR_LOAD_GL(get_uniform_iv, PFNGLGETUNIFORMIVPROC, "glGetUniformiv");
    HWR_LOAD_GL(uniform_matrix_4fv, PFNGLUNIFORMMATRIX4FVPROC,
                "glUniformMatrix4fv");
    HWR_LOAD_GL(tex_parameter_fv, hwr_gl_tex_parameter_fv_fn,
                "glTexParameterfv");
    return 1;
}

#undef HWR_LOAD_GL

static const char g_geometry_vertex_body[] =
    "layout(location=0) in ivec3 a_pos;\n"
    "layout(location=1) in vec2 a_wall;\n"
    "layout(location=2) in uint a_offset;\n"
    "layout(location=3) in uvec2 a_dims;\n"
    "layout(location=4) in uint a_aux;\n"
    "layout(location=5) in uvec4 a_pan_scale_flags;\n"
    "layout(location=6) in uvec4 a_light_material;\n"
    /* The BOTTOM-edge and TOP-edge corner words are adjacent in the record, so
     * one two-component attribute carries the pair and the slot the second one
     * used to hold is free for the normal below. The two halves are read exactly
     * where they were read before -- .x is the bottom word, .y the top -- and
     * both programs declare this the same way, because they share one array
     * object and a format is a property of the array, not of a program. */
    "layout(location=7) in uvec2 a_wall_shade;\n"
    /* .x = the tinted-wall depth-bias adjust, .y = the legacy flat escape. */
    "layout(location=13) in uvec2 a_tint_adjust_taint;\n"
    "layout(location=14) in vec2 a_pos_frac;\n"
    "layout(location=8) in uvec4 a_flat_span;\n"
    "layout(location=9) in uvec4 a_wall_source;\n"
    "layout(location=10) in uvec4 a_wall_rows;\n"
    "layout(location=11) in uvec4 a_wall_span;\n"
    "layout(location=12) in uvec4 a_wall_vertical;\n"
    "uniform ivec2 u_view_offset;\n"
    "uniform int u_eye_z;\n"
    "uniform int u_x_flip;\n"
    "uniform vec2 u_basis;\n"
    "uniform ivec2 u_basis_fixed;\n"
    "uniform vec2 u_focal;\n"
    "uniform vec2 u_center;\n"
    "uniform ivec2 u_shade_origin;\n"
    "uniform int u_flat_edge_bias;\n"
    "uniform vec2 u_viewport;\n"
    "out vec3 v_world;\n"
    "out float v_depth_fixed;\n"
    "out vec2 v_wall;\n"
    "flat out uint v_offset;\n"
    "flat out uvec2 v_dims;\n"
    "flat out uint v_aux;\n"
    "flat out uvec4 v_pan_scale_flags;\n"
    "flat out uvec4 v_light_material;\n"
    "flat out uvec4 v_flat_span;\n"
    "flat out uvec4 v_wall_source;\n"
    "flat out uvec4 v_wall_rows;\n"
    "flat out uvec4 v_wall_span;\n"
    "flat out uvec4 v_wall_vertical;\n"
    "noperspective out float v_wall_shade_endpoint;\n"
    "noperspective out float v_wall_shade_top;\n"
    "flat out uint v_wall_tint_adjust;\n"
    "flat out uint v_flat_taint;\n"
    /* The normal declaration and everything that reads it exist only in the
     * enhanced program, which is why this stage is chunked the way the fragment
     * stage already is: one body compiled twice, and the authentic program's
     * preprocessed text carries no normal at all. What the two programs then
     * share is the geometry, which is the point -- the array object is one
     * object, and the picture the off state draws is held to the byte. */
    "#ifdef HWR_TRUECOLOR\n"
    "layout(location=15) in uvec2 a_normal;\n"
    /* View space, in the basis the fragment tail reconstructs its own position
     * in: x to the right, y forward, z downward from the eye. A face has one
     * normal, so it is not interpolated. All zero says the vertex carries none,
     * which is every class a light never touches and every vertex of the
     * original picture. */
    "flat out vec3 v_normal;\n"
    /* And the same normal in WORLD space, untouched, for the one law that
     * runs in world space: the sun lookup. Zero says the vertex carries
     * none. */
    "flat out vec3 v_normal_world;\n"
    /* The fold's inverse. Zero counts as positive on both branches, so an
     * axis-aligned direction keeps the fold it was packed on; the language's own
     * sign() answers zero for zero and would lose it. */
    "vec3 hwr_normal_decode(uvec2 packed_normal) {\n"
    "  vec2 p=(vec2(packed_normal)-128.0)/127.0;\n"
    "  vec3 n=vec3(p,1.0-abs(p.x)-abs(p.y));\n"
    "  if (n.z<0.0)\n"
    "    n=vec3((1.0-abs(p.y))*(p.x>=0.0 ? 1.0 : -1.0),\n"
    "           (1.0-abs(p.x))*(p.y>=0.0 ? 1.0 : -1.0), n.z);\n"
    "  return normalize(n);\n"
    "}\n"
    "#endif\n"
    "int wrap16(int v) { int w=v & 65535; return w >= 32768 ? w-65536 : w; }\n"
    "void main() {\n"
    "  int dx=wrap16(a_pos.x+u_view_offset.x);\n"
    "  int dy=wrap16(a_pos.y+u_view_offset.y);\n"
    /* The world->view transform is a 16-bit add, so the wrap runs on the whole
     * units and the sub-unit remainder is added afterwards.  It is zero for
     * every vertex authored on a world coordinate, leaving those unchanged. */
    "  float wx=float(dx)+a_pos_frac.x, wy=float(dy)+a_pos_frac.y;\n"
    "  float s=u_basis.x, c=u_basis.y;\n"
    "  float vx=s*wx+c*wy;\n"
    "  if (u_x_flip != 0) vx=-vx;\n"
    "  float depth=s*wy-c*wx;\n"
    "  float zr=float(u_eye_z-a_pos.z);\n"  /* the engine's vertical span term is (eye - z) */
    "  float clip_x=(2.0*u_focal.x/u_viewport.x)*vx+"
    "(2.0*u_center.x/u_viewport.x-1.0)*depth;\n"
    "  float clip_y=(2.0*u_focal.y/u_viewport.y)*zr+"
    "(2.0*u_center.y/u_viewport.y-1.0)*depth;\n"
    /* The engine's degenerate EDGE_MAP path emits a viewport-height column.
     * Pin the synthetic curtain to both clip-space Y edges while preserving
     * the projected wall X and depth used by the selective occlusion pass. */
    "  if ((a_light_material.y & 128u)!=0u)"
    " clip_y=a_pos.z==32767 ? -depth : depth;\n"
    "  const float n=16.0, f=20000.0;\n"
    "  float clip_z=((f+n)/(f-n))*depth-(2.0*f*n/(f-n));\n"
    "  gl_Position=vec4(clip_x,clip_y,clip_z,depth);\n"
    "  v_world=vec3(a_pos); v_depth_fixed=depth; v_wall=a_wall;\n"
    "  v_offset=a_offset; v_dims=a_dims; v_aux=a_aux;\n"
    "  v_pan_scale_flags=a_pan_scale_flags; v_light_material=a_light_material;\n"
    "  v_flat_span=a_flat_span;\n"
    "  v_wall_source=a_wall_source; v_wall_rows=a_wall_rows;\n"
    "  v_wall_span=a_wall_span;\n"
    "  v_wall_vertical=a_wall_vertical;\n"
    "  v_wall_shade_endpoint=float(a_wall_shade.x);\n"
    "  v_wall_shade_top=float(a_wall_shade.y);\n"
    "  v_wall_tint_adjust=a_tint_adjust_taint.x;\n"
    "  v_flat_taint=a_tint_adjust_taint.y;\n"
    "#ifdef HWR_TRUECOLOR\n"
    /* The normal is authored in world space and turned into the view's own
     * space by the same basis and the same mirror the position takes: a
     * direction skips the translation, and a mirror is its own inverse
     * transpose, so flipping the sideways component is all a reflected view
     * needs. World z is up and the view's third axis points down from the eye,
     * which is the axis the fragment tail rebuilds its position on. */
    "  if (a_normal.x==0u && a_normal.y==0u) {\n"
    "    v_normal=vec3(0.0);\n"
    "    v_normal_world=vec3(0.0);\n"
    "  } else {\n"
    "    vec3 wn=hwr_normal_decode(a_normal);\n"
    "    float rx=s*wn.x+c*wn.y;\n"
    "    if (u_x_flip != 0) rx=-rx;\n"
    "    v_normal=vec3(rx,s*wn.y-c*wn.x,-wn.z);\n"
    "    v_normal_world=wn;\n"
    "  }\n"
    "#endif\n"
    "}\n";

/* The version directive and the optional true-colour define are separate source
 * chunks so ONE body per stage can be compiled twice: once with an empty define
 * chunk, whose concatenated text is the authentic program and carries no trace
 * of anything the enhancement adds, and once with the define, which adds a
 * colour output inside emit(), a surface normal, and nothing else. */
static const char g_glsl_version[] = "#version 330 core\n";
static const char g_truecolor_define[] = "#define HWR_TRUECOLOR 1\n";

static const char g_geometry_fragment_body[] =
    "layout(location=0) out uint out_index;\n"
    "layout(location=1) out uint out_floor_id;\n"
    "uniform usamplerBuffer u_texels;\n"
    "uniform usampler2D u_colormap;\n"
    "uniform usampler2D u_tint;\n"
    "uniform usampler2D u_glow;\n"
    "uniform usampler2D u_blend;\n"
    "uniform usampler2D u_dst;\n"
    "uniform usampler2D u_stale;\n"
    "int dbg_ix=0, dbg_iy=0;\n"
    "uniform int u_shade_level;\n"
    "uniform int u_tint_shade_level;\n"
    "uniform int u_light_bias;\n"
    "uniform int u_glow_phase;\n"
    "uniform ivec2 u_view_offset;\n"
    "uniform int u_eye_z;\n"
    "uniform int u_x_flip;\n"
    "uniform ivec2 u_basis_fixed;\n"
    "uniform ivec2 u_focal_i;\n"
    "uniform ivec2 u_center_i;\n"
    "uniform vec2 u_focal;\n"
    "uniform vec2 u_center;\n"
    "uniform vec2 u_viewport;\n"
    "uniform ivec2 u_source_viewport;\n"
    "uniform ivec2 u_source_focal;\n"
    "uniform ivec2 u_source_center;\n"
    "uniform ivec2 u_shade_origin;\n"
    "uniform int u_flat_edge_bias;\n"
    "uniform float u_depth_scale;\n"
    "uniform int u_fill_index;\n"
    "uniform int u_wd_bias;\n"
    "uniform int u_blend_pass;\n"
    /* Owner-surface depth exception. When u_floor_exception!=0 (the exception
     * re-draw passes) a fragment survives only where the opaque winner recorded
     * in u_floor_id equals the object's owner-floor or owner-wall id (carried
     * per-vertex in v_flat_span.x); a zero id means "does not participate". */
    "uniform int u_floor_exception;\n"
    "uniform usampler2D u_floor_id;\n"
    /* Per-wall sky caps sample the parallax skybox in screen space, exactly as
     * the retired full-screen prepass did; these carry the same inputs. */
    "uniform int u_sky_valid;\n"
    "uniform int u_sky_texture_offset;\n"
    "uniform int u_sky_source_offset;\n"
    "uniform int u_sky_projection_offset;\n"
    "uniform int u_sky_texture_height;\n"
    "uniform int u_sky_start_row;\n"
    "uniform int u_sky_yaw;\n"
    "uniform int u_sky_double_scanline;\n"
    /* The colour pass accepts both directed sides, as before.  The optional
     * occlusion pass discards the rejected directed side before its EDGE_MAP
     * sky can consume column extent. */
    "uniform int u_sky_occlusion_pass;\n"
    "in vec3 v_world;\n"
    "in float v_depth_fixed;\n"
    "in vec2 v_wall;\n"
    "flat in uint v_offset;\n"
    "flat in uvec2 v_dims;\n"
    "flat in uint v_aux;\n"
    "flat in uvec4 v_pan_scale_flags;\n"
    "flat in uvec4 v_light_material;\n"
    "flat in uvec4 v_flat_span;\n"
    "flat in uvec4 v_wall_source;\n"
    "flat in uvec4 v_wall_rows;\n"
    "flat in uvec4 v_wall_span;\n"
    "flat in uvec4 v_wall_vertical;\n"
    "noperspective in float v_wall_shade_endpoint;\n"
    "noperspective in float v_wall_shade_top;\n"
    "flat in uint v_wall_tint_adjust;\n"
    "flat in uint v_flat_taint;\n"
    "int imul_shift2(int a,int b) { return int(uint(a)*uint(b)) >> 2; }\n"
    "int signed16(uint v) { int n=int(v & 65535u); return n>=32768 ? n-65536 : n; }\n"
    "int source_column() {\n"
    "  int column=int(gl_FragCoord.x);\n"
    "  return u_x_flip != 0 ? 2*u_center_i.x-column : column;\n"
    "}\n"
    "uint wall_quotient(int column) {\n"
    "  uint numerator=v_wall_source.x, denominator=v_wall_source.y;\n"
    "  uint numerator_step=v_wall_source.z; int denominator_step=int(v_wall_source.w);\n"
    "  if (column>0 && (numerator_step==0u || uint(column)<=(0xffffffffu-numerator)/numerator_step)) {\n"
    "    numerator+=numerator_step*uint(column);\n"
    "    denominator+=uint(denominator_step*column);\n"
    "  } else {\n"
    "    for (int i=0;i<column;i++) {\n"
    "      uint old=numerator; numerator+=numerator_step;\n"
    "      if (numerator<old) {\n"
    "        denominator_step>>=1; numerator_step>>=1; denominator>>=1;\n"
    "        numerator=(numerator>>1)|0x80000000u;\n"
    "      }\n"
    "      denominator+=uint(denominator_step);\n"
    "    }\n"
    "  }\n"
    "  return denominator!=0u ? numerator/denominator : 0u;\n"
    "}\n"
    "uint sample_texel() {\n"
    "  uint flags=v_light_material.y;\n"
    "  if ((flags & 4u)!=0u) return v_light_material.z;\n"
    "  if ((flags & 2u)!=0u) return v_light_material.z;\n"
    "  int w=int(v_dims.x), h=int(v_dims.y); int ix,iy;\n"
    "  if ((v_flat_span.w & 0x80000000u)!=0u) {\n"
    "    iy=int(floor(v_wall.x)) % h; if (iy<0) iy+=h;\n"
    "    ix=int(floor(v_wall.y)) % w; if (ix<0) ix+=w;\n"
    "    if ((flags & 32u)!=0u) iy=(h-1)-iy;\n"
    "    return texelFetch(u_texels,int(v_offset)+iy*w+ix).r;\n"
    "  }\n"
    "  if ((flags & 8u)!=0u) {\n"
    "    int scale=int(v_pan_scale_flags.z);\n"
    /* The flat driver's density class is JOINT over both dimensions — a
     * texture qualifies only when it is square and its side is a whole
     * multiple of 256 — and the DAS transparency bit splits that class in
     * two.  An opaque square-256 flat gets the density boost (half the world
     * units per texel, and one extra step of perspective shift); a
     * transparent one keeps the base density and the base shift.  The pan
     * byte lands at full weight for the whole class either way, so the pan
     * and world/perspective shifts are separate terms. */
    "    bool square256=w!=0 && w==h && (w & 255)==0;\n"
    "    bool transparent=(flags & 1u)!=0u;\n"
    "    int dimension_shift=(square256 && !transparent) ? 1 : 0;\n"
    "    int pan_shift=square256 ? 1 : 0;\n"
    "    int row=int(gl_FragCoord.y), column=source_column();\n"
    "    int height_delta=abs(u_eye_z-int(v_world.z));\n"
    "    int remaining=int(v_flat_span.z)>u_center_i.y ? row-u_center_i.y : u_center_i.y-row;\n"
    "    uint dividend=uint(height_delta << (4-scale+dimension_shift))*uint(u_focal_i.y);\n"
    "    uint perspective=remaining>1 ? dividend/uint(remaining) : dividend;\n"
    "    uint guard_distance=uint(abs(row-int(v_flat_span.z)));\n"
    "    uint guard=v_flat_span.x-v_flat_span.y*guard_distance;\n"
    "    uint held_rows=0u;\n"
    "    if ((guard >> 16)<2u && v_flat_span.y!=0u) {\n"
    "      uint delta=0x20000u-guard;\n"
    "      held_rows=delta/v_flat_span.y;\n"
    "      if (delta%v_flat_span.y!=0u) held_rows++;\n"
    "      guard+=v_flat_span.y*held_rows;\n"
    "    }\n"
    "    int guard_hi=max(int(guard >> 16),2);\n"
    "    int step_u=(-(u_basis_fixed.x << 12))/guard_hi;\n"
    "    int step_v=(u_basis_fixed.y << 12)/guard_hi;\n"
    "    uint flat_flags=v_flat_span.w;\n"
    "    if ((held_rows & 1u)!=0u) {\n"
    "      if ((flat_flags & 2u)!=0u) step_u=-step_u;\n"
    "      if ((flat_flags & 4u)!=0u) step_v=-step_v;\n"
    "    }\n"
    "    int screen_rel=column-u_center_i.x;\n"
    "    uint accum_u=uint(imul_shift2(int(perspective),u_basis_fixed.y));\n"
    "    uint accum_v=uint(imul_shift2(int(perspective),u_basis_fixed.x));\n"
    "    accum_u+=(uint(step_u)&0xffffffu)*uint(screen_rel);\n"
    "    accum_v+=(uint(step_v)&0xffffffu)*uint(screen_rel);\n"
    "    accum_u+=uint(u_view_offset.x & 65535) << uint(16-scale+dimension_shift);\n"
    "    accum_v-=uint(u_view_offset.y & 65535) << uint(16-scale+dimension_shift);\n"
    "    accum_u+=uint(v_aux & 255u) << uint(15+pan_shift);\n"
    "    if (square256) accum_v-=uint(v_aux >> 8) << uint(15+pan_shift);\n"
    "    else accum_v+=uint(v_aux >> 8) << uint(15+pan_shift);\n"
    "    if ((flat_flags & 2u)!=0u) accum_u=0u-accum_u;\n"
    "    if ((flat_flags & 4u)!=0u) accum_v=0u-accum_v;\n"
    "    ix=int((accum_u >> 16) & uint(w-1));\n"
    "    iy=int((accum_v >> 16) & uint(h-1));\n"
    /* Flats follow one world-plane mapping (the map format's floor UV rule),
     * applied to both axes with the SAME density: the flat driver's branch is
     * on the joint square-256 class and the transparency bit, never on one
     * axis alone.  With base = 2^scale world units, a flat covers base world
     * units per texel, halved only for the opaque square-256 case; the pan
     * byte lands at full weight for the whole square-256 class and at HALF
     * weight otherwise.  The u axis is world-x negated plus pan_x; the v axis
     * is world-y minus pan_y (both axes SUBTRACT pan_y).  The software
     * renderer evaluates that plane at the span's INTEGER row/column — not at
     * the fragment's pixel-center world position, which sits half a pixel away
     * (several texels of drift at flat depths) — so reconstruct the world
     * point from row/column + camera instead of sampling v_world.  Beyond ~3
     * texels per pixel the original's integer span walk aliases differently
     * than any exact evaluation; keep the accumulator result there for the
     * non-square-256 case (the square-256 class is precise enough to evaluate
     * everywhere). */
    "    bool pow2_dims=(w & (w-1))==0 && (h & (h-1))==0;\n"
    "    if (pow2_dims && w<=256 && h<=256) {\n"
    "      float fdepth=float(u_eye_z-int(floor(v_world.z+0.5)))*u_focal.y/float(row-u_center_i.y);\n"
    "      float w2t=float(1 << scale)/float(1 << dimension_shift);\n"
    "      float pan_mul=0.5*float(1 << pan_shift);\n"
    "      if (square256 || fdepth < 3.0*u_focal.x*w2t) {\n"
    "        float fvx=float(screen_rel)*fdepth/u_focal.x;\n"
    "        float fsin=float(u_basis_fixed.x)*(1.0/16384.0);\n"
    "        float fcos=float(u_basis_fixed.y)*(1.0/16384.0);\n"
    "        float wxf=fsin*fvx-fcos*fdepth-float(u_view_offset.x);\n"
    "        float wyf=fcos*fvx+fsin*fdepth-float(u_view_offset.y);\n"
    "        float uu=-wxf/w2t+float(v_aux & 255u)*pan_mul;\n"
    "        float vv= wyf/w2t-float(v_aux >> 8)*pan_mul;\n"
    "        if ((flat_flags & 2u)!=0u) uu=-uu;\n"
    "        if ((flat_flags & 4u)!=0u) vv=-vv;\n"
    "        ix=int(floor(uu)) & (w-1);\n"
    "        iy=int(floor(vv)) & (h-1);\n"
    "      }\n"
    "    }\n"
    "  } else {\n"
    /* Wall texture coordinates come from the original integer column
     * accumulators. The staged state is consumed before each advance. */
    "    if (v_wall_span.w==0u) {\n"
    "      float wall_s=max(v_wall.x-0.5*dFdx(v_wall.x),0.0);\n"
    "      int base=((int(floor(wall_s))+int(v_pan_scale_flags.x)*2) & 65535) >> 1;\n"
    "      if ((flags & 32u)!=0u) base=((h-1)-base) & 65535;\n"
    "      iy=(v_light_material.w & 1u)!=0u ? base & (h-1) : base % h;\n"
    "      float wall_v=max(v_wall.y-0.5*dFdy(v_wall.y),0.0);\n"
    "      int vertical=(int(floor(wall_v)) & 65535) >> 1;\n"
    "      if ((v_flat_span.w & 128u)!=0u)\n"
    "        vertical=(vertical+w-int(v_wall_span.z >> 1)-int(v_pan_scale_flags.y)) & 65535;\n"
    "      else vertical=(vertical+int(v_pan_scale_flags.y)) & 65535;\n"
    "      ix=(v_light_material.w & 2u)!=0u ? vertical & (w-1) : vertical % w;\n"
    "      dbg_ix=ix; dbg_iy=iy;\n"
    "      return texelFetch(u_texels,int(v_offset)+iy*w+ix).r;\n"
    "    }\n"
    "    int column=int(gl_FragCoord.x)-int(v_wall_span.x);\n"
    "    uint mapped_s=wall_quotient(column);\n"
    "    int base=int((mapped_s+v_pan_scale_flags.x*2u) & 65535u) >> 1;\n"
    "    if ((flags & 32u)!=0u) base=((h-1)-base) & 65535;\n"
    /* Non-power-of-two wall classes stage 0xffff as their accumulator mask,
     * an identity: the engine tiles them at the span level instead, so the
     * coordinate wraps modulo the real dimension.  In range the modulo is a
     * no-op; past the edge it starts the next tile rather than spilling into
     * the bytes of the adjacent stored row. */
    "    iy=(v_light_material.w & 1u)!=0u ? base & (h-1) : base % h;\n"
    "    uint top_accum=v_wall_rows.x+v_wall_rows.z*uint(column);\n"
    "    uint bottom_accum=v_wall_rows.y+v_wall_rows.w*uint(column);\n"
    "    int top=signed16(top_accum>>16), bottom=signed16(bottom_accum>>16);\n"
    "    int row=int(gl_FragCoord.y), count=max(bottom-top+1,1);\n"
    "    uint vertical_step=(v_wall_span.z<<16)/uint(2*count-1);\n"
    "    uint vertical_seed=0u;\n"
    "    if (v_wall_vertical.y!=0u) {\n"
    "      int seed=int(vertical_step)*(top-u_center_i.y)-int(v_wall_vertical.x);\n"
    /* The engine stores the seed as a sub-texel phase in the low 16 bits with
     * its integer part clamped to <=15; a larger whole-texel seed never
     * survives.  Mirror that so a tall wall's first row samples the same texel. */
    "      if (seed>0) { uint us=uint(seed); uint hi=us>>16u; if (hi>15u) hi=15u;"
    " vertical_seed=(hi<<16u)|(us & 0xffffu); }\n"
    "    }\n"
    "    int row_offset=clamp(row-top,0,count-1);\n"
    "    int vertical=int((vertical_seed+vertical_step*uint(row_offset))>>16);\n"
    "    if ((v_flat_span.w & 128u)!=0u)\n"
    "      vertical=(vertical+w-int(v_wall_span.z >> 1)-int(v_pan_scale_flags.y)) & 65535;\n"
    "    else vertical=(vertical+int(v_pan_scale_flags.y)) & 65535;\n"
    "    ix=(v_light_material.w & 2u)!=0u ? vertical & (w-1) : vertical % w;\n"
    "    dbg_iy=1000+iy;\n"
    "  }\n"
    "  dbg_ix=ix; if (dbg_iy<1000) dbg_iy=iy;\n"
    "  return texelFetch(u_texels,int(v_offset)+iy*w+ix).r;\n"
    "}\n"
    "int wrap16(int v) { int w=v & 65535; return w >= 32768 ? w-65536 : w; }\n"
    /* Effective face light mirrors the engine's sector byte plus the weapon
     * flash, and the flash is added only when the authored byte is nonzero. */
    "int effective_light() {\n"
    "  int authored=int(v_light_material.x); int light=authored;\n"
    "  if (authored!=0) light=(light+u_light_bias) & 255;\n"
    "  return light;\n"
    "}\n"
    "int shade_row(int selected_level, bool tinted) {\n"
    "  int level=selected_level; int shr=5+level;\n"
    "  int light=effective_light();\n"
    /* A face whose effective light resolves to 0 -- authored 0, or the flashed
     * sum wrapping to 0 -- is drawn by the engine's UNSHADED mapper: colormap
     * row 0, at every distance and for every surface class, because the column
     * dispatcher takes that branch before any wall/flat/tint shade math.  Row 0
     * is the fullbright colormap row. */
    "  if (light==0) return 0;\n"
    "  int axv=light==0 ? 0 : light-128;\n"
    "  int bias=8+axv;\n"
    "  int scale=light==0 ? 32 : 32-(max(axv+4,0) >> 2);\n"
    "  int transformed=int(floor(max(v_depth_fixed*u_depth_scale,0.0)*256.0));\n"
    "  int depth=(transformed >> 8) & 65535;\n"  /* the span vertex depth word: world units */
    "  bool flat_surface=(v_light_material.y & 8u)!=0u; int r;\n"
    "  if (flat_surface) {\n"
    "    if (tinted) {\n"
    /* The engine evaluates the flat shade ONLY at the corners of the polygon it
     * rasterises -- the sector loop after it has been clipped to the drawn area
     * -- and interpolates linearly in screen space between them.  Read the
     * CPU-computed per-corner row carried by the noperspective wall endpoint
     * varying (stored for flats in the wall high-byte format) and truncate it
     * per fragment; flats clamp to 0..31.  The per-corner word carries the
     * engine's 8-bit sub-row fraction in its low byte, so flooring the
     * interpolated word per fragment lands a band boundary at the engine's
     * sub-pixel height instead of snapping to the whole row.
     *
     * Evaluating the same law PER FRAGMENT instead is not a refinement of this,
     * it is a different surface: the law is convex where it is not clamped, so a
     * chord between two corner samples runs ABOVE it everywhere in between, and
     * the per-fragment field is uniformly brighter by however much unsampled
     * interior the polygon has.  Sampling the corner set the engine samples --
     * no denser, no sparser -- is the whole point.
     *
     * The per-fragment radial below survives only for the restore switch and for
     * direct-mapped mesh flats, which carry no preprojected corner word. */
    "      if (v_flat_taint==0u &&"
    " (v_flat_span.w & 0x80000000u)==0u)"
    " return clamp(int(floor(v_wall_shade_endpoint/256.0)),0,31);\n"
    /* The per-fragment radial.  The edge term measures from the jittered shade
     * origin (the light position); the jitter is the flicker. */
    "      int sx=int(262144.0/u_focal.x), sy=int(262144.0/u_focal.y);\n"
    "      int px=(abs(wrap16(source_column()-u_shade_origin.x)) * sx) >> 8;\n"
    "      int py=(abs(wrap16(int(gl_FragCoord.y)-u_shade_origin.y)) * sy) >> 8;\n"
    "      int edge_depth=(depth+max(px,py)) & 65535;\n"
    "      r=(edge_depth >> shr)-bias-u_flat_edge_bias;\n"
    "      r=((r+32768) & 65535)-32768;\n"
    "      return r<=0 ? 1 : min(r,31);\n"
    "    }\n"
    /* The engine's untinted-flat row is min(r, clip_scale, 31) with r<=0 -> row
     * 0, so a dim sector's floor tops out at its clip scale exactly like the wall
     * path below; clip_scale 0 pins the plane to row 0. */
    "    r=(depth >> shr)-bias;\n"
    "    r=((r+32768) & 65535)-32768;\n"
    "    return clamp(r,0,min(scale,31));\n"
    "  }\n"
    /* A tinted wall's row is the engine's per-corner wd_project shade.  Where the
     * column DDA above cannot run (degenerate span) it is evaluated PER PIXEL
     * here.  The mod's wall surfaces are not split into the engine's short
     * portal-clipped segments, so interpolating two corner words across a whole
     * wall underestimates the Chebyshev candle radial toward the screen edges
     * (the +16-row over-lightness).  The radial and depth reuse the tinted-flat
     * structure; the wall subtracts the candle bias AND wd_project's own bias
     * (wdbias, 8 at shade level 0). */
    "  if (tinted) {\n"
    /* Reproduce wd_project's per-corner shade words and the engine's column DDA.
     * v_wall_shade_top / v_wall_shade_endpoint are the TOP / BOTTOM edge words
     * (row<<8)|frac, each carried linear-in-X (both endpoints of a side hold the
     * same word, and the wall's left/right screen edges are vertical columns, so
     * the noperspective interpolation is exactly the engine's top-edge and
     * bottom-edge lerps with no triangle-diagonal seam).  Down the column, mix
     * the two words by the fraction of the way from the wall's projected top row
     * to its bottom row (the same per-column top/bottom accumulators the texel
     * path uses), then floor the interpolated word -- WITH its fraction -- to the
     * shade row.  That is the engine's seed24 + k*q >> 16 DDA in floating point:
     * row = floor(V/256).  A degenerate-span wall (no valid column DDA) falls to
     * the per-pixel radial below. */
    "    if (v_wall_span.w!=0u) {\n"
    "      int scol=int(gl_FragCoord.x)-int(v_wall_span.x); if (scol<0) scol=0;\n"
    "      uint ta=v_wall_rows.x+v_wall_rows.z*uint(scol);\n"
    "      uint ba=v_wall_rows.y+v_wall_rows.w*uint(scol);\n"
    "      float stop=float(signed16(ta>>16u));\n"
    "      float sbot=float(signed16(ba>>16u));\n"
    "      float cnt=sbot-stop+1.0;\n"
    "      float t=cnt>0.5 ? clamp((floor(gl_FragCoord.y)-stop)/cnt,0.0,1.0) : 0.0;\n"
    "      float V=mix(v_wall_shade_top,v_wall_shade_endpoint,t);\n"
    "      return clamp(int(floor(V/256.0)),0,31);\n"
    "    }\n"
    "    {\n"
    "      int sx=int(262144.0/u_focal.x), sy=int(262144.0/u_focal.y);\n"
    "      int px=(abs(wrap16(source_column()-u_shade_origin.x)) * sx) >> 8;\n"
    "      int py=(abs(wrap16(int(gl_FragCoord.y)-u_shade_origin.y)) * sy) >> 8;\n"
    "      int edge_depth=(depth+max(px,py)) & 65535;\n"
    /* The engine's flags&0x40 wall block darkens the tinted bias by a per-face
     * amount (effective_bias = bias - adjust) so receding walls drop up to 16
     * rows the interpolated corner word missed. */
    "      int adj=int(v_wall_tint_adjust);\n"
    "      r=(edge_depth >> shr)-(bias-adj)-u_wd_bias;\n"
    "      r=((r+32768) & 65535)-32768;\n"
    /* wd_project (renderer.c:4338) clamps a wall row at the sector's clip_scale
     * ceiling BEFORE the 31 clamp, so a dim sector's darkest wall tops out below
     * 31 and stays dimly visible instead of crushing to near-black.
     * clip_scale==scale here (both 32-(max(light-124,0)>>2)). */
    "      int cap=min(scale,31);\n"
    "      return r<=0 ? 1 : min(r,cap);\n"
    "    }\n"
    "  }\n"
    "  int dividend=2 << level;\n"
    /* The wall span divisors are the 16-bit vertex depth words, already in
     * world units, so the column ratio reduces to depth/(16*dividend). */
    "  int depth_q=depth/(16*dividend);\n"
    "  r=depth_q-bias; r=((r+32768) & 65535)-32768;\n"
    "  return clamp(r,0,min(scale,31));\n"
    "}\n"
    /* The software seed tests the pre-halved wall quotient. Its reciprocal
     * endpoint accumulator is not available here, so use the same depth word;
     * integer reciprocal boundaries are the remaining exceptional columns. */
    "int dithered_row(int row, int selected_level, bool tinted) {\n"
    /* An effective-light-0 face is fullbright at every distance, so the depth
     * checker must not darken its row 0. */
    "  if (effective_light()==0) return row;\n"
    "  if (tinted || (v_light_material.y & 8u)!=0u) return row;\n"
    "  int transformed=int(floor(max(v_depth_fixed*u_depth_scale,0.0)*256.0));\n"
    "  int depth=(transformed >> 8) & 65535;\n"
    "  int q=depth/(8*(2 << selected_level));\n"
    "  int checker=(int(gl_FragCoord.x)+int(gl_FragCoord.y)) & 1;\n"
    "  return ((q & 1)==0 && checker!=0) ? max(row-1,0) : row;\n"
    "}\n"
    /* The continuous twin of shade_row(), line for line: the same selection, the
     * same terms, the same order, with the truncation to a whole row removed and
     * every clamp taken on the float instead of on the integer it produced.
     *
     * Two properties follow from that and are what the smooth law rests on.
     * Flooring this lands on exactly the row shade_row() returns -- each divisor
     * is a power of two, so the float quotient floors to the shift it replaces,
     * and every bias subtracted from it is a whole number, which commutes with
     * the floor -- so the colour at a whole row is the authentic colour, bit for
     * bit. And the 16-bit wrap the row law applies to its intermediate cannot
     * reach: the depth word is 16 bits, the shift is at least five, and the
     * biases are bytes, so the term it wraps never leaves +/-2200.
     *
     * The tinted paths already carry their shade as a fraction (the corner words
     * hold the engine's own sub-row phase in their low byte), so for those this
     * twin is the authentic expression with the floor lifted off it. */
    "#ifdef HWR_TRUECOLOR\n"
    /* The three parts of the candle a law may draw differently from the
     * original one, each its own uniform so each can be asked for on its own.
     * The smooth law asks for none of them and the modern law asks for all
     * three; which law asks for what is hwr_shade_part_wanted(). */
    "uniform int u_shade_fixed_origin;\n"
    "uniform int u_shade_round;\n"
    "uniform int u_shade_per_fragment;\n"
    /* The flame's wander of the fixed origin, in the origin's own screen
     * units, zero at rest. The engine's candle is measured from an origin the
     * flame moves about the view centre; the modern law's fixed centre is the
     * place that dance is a dance around, and this is the dance given back to
     * it -- one offset a frame, paced and sized by the flicker dials, staged
     * beside the origin it moves. The laws that read the engine's own origin
     * never see it. */
    "uniform ivec2 u_cone_wander;\n"
    /* The screen row the fixed centre sits on: the view centre BEFORE the
     * pitch shift. The engine adds its pitch term to the horizon but not to
     * the candle origin's anchor (renderer.c:13411-13426 against the horizon
     * staging), so the pool stays with the gaze while the world pitches
     * under it -- looking down lights the near floor, looking up the
     * ceiling. Level, this row IS the horizon. */
    "uniform int u_cone_center_y;\n"
    /* How softly the candle's cone goes over from light to dark. Zero is the
     * engine's own edge, exactly, and it is the whole dial's identity: the
     * softened radial below is not evaluated at all. */
    "uniform float u_cone_penumbra;\n"
    /* The map's own candle amplitude, declared here because the softened
     * cone term reads it for the edge it eases about; what it MEANS is said
     * where its sibling u_candle_state lives, beside the candle rows. */
    "uniform int u_candle_word;\n"
    /* The candle term the tinted law adds to depth, in the depth word's units.
     *
     * The engine measures it from a light position that lives in screen space
     * and is moved every seventh tick by a random step around the view centre,
     * and that dance is the flicker. A law whose light does not dance measures
     * from the view centre itself -- the place the dance is a dance around --
     * which leaves the pool where it always was, without inventing a light
     * position the map never had. That centre is the UNPITCHED one: the
     * engine adds its pitch shift to the horizon and never to this anchor,
     * so the pool is screen-fixed and pitching the view slides the world
     * under it -- the light follows the gaze onto the near floor or
     * ceiling, which an anchor at the pitched horizon would not do (it
     * would stay glued to the world's horizon and leave the gaze
     * unlit). The flicker switch puts the dance back:
     * u_cone_wander is the flame's own paced wander of that centre, zero at
     * rest, so a still flame is the fixed centre to the bit and a lit one
     * moves the pool the way the engine's own candle moves.
     *
     * The engine's distance is the larger of the two axis terms, which makes a
     * pool with straight edges and corners; the length of the pair makes it
     * round. The axis terms keep the engine's own per-axis focal scales, so a
     * pool is as wide as the projection makes it either way.
     *
     * The sixteen-bit wrap the engine's intermediate carries stays on the
     * square metric, which is what makes that arm the engine's law whole. The
     * round metric does not take it: a term the wrap reaches is one the row law
     * would have clamped into darkness, and wrapping it lands a bright ring in
     * the dark instead. */
    "float candle_edge_depth(int depth) {\n"
    "  ivec2 origin=u_shade_fixed_origin!=0"
    " ? ivec2(u_center_i.x,u_cone_center_y)+u_cone_wander"
    " : u_shade_origin;\n"
    "  int sx=int(262144.0/u_focal.x), sy=int(262144.0/u_focal.y);\n"
    "  int px=(abs(wrap16(source_column()-origin.x)) * sx) >> 8;\n"
    "  int py=(abs(wrap16(int(gl_FragCoord.y)-origin.y)) * sy) >> 8;\n"
    "  if (u_shade_round!=0)\n"
    "    return float(depth)+length(vec2(float(px),float(py)));\n"
    "  return float((depth+max(px,py)) & 65535);\n"
    "}\n"
    /* The same term with the cone's transition softened, for the modern room
     * law alone -- the laws that reproduce the engine read the line above and
     * never this one, which is what keeps their identity out of the dial's
     * reach.
     *
     * The engine's cone edge is where the radial has spent the map's own
     * candle word: radial = word * unit, fourteen degrees off the view axis
     * on every map the game ships, and the row law crosses it at one pace --
     * a pace the eye reads as a flashlight's rim. The penumbra eases
     * the radial's SLOPE across a band about that edge, half the edge's own
     * angle to either side, with a bump that returns to slope one at both
     * ends so the band has no seam: the core inside it is the engine's to
     * the bit, the slope at the edge itself falls as far as a quarter at the
     * dial's full, and past the band the flanks keep the bounded rows the
     * easing left them -- half the word at full, a lantern's spill rather
     * than a second picture. At a penumbra of nothing the line above is the
     * whole answer, exactly. */
    "float cone_edge_depth(int depth, int shr) {\n"
    "  if (u_cone_penumbra<=0.0) return candle_edge_depth(depth);\n"
    "  ivec2 origin=u_shade_fixed_origin!=0"
    " ? ivec2(u_center_i.x,u_cone_center_y)+u_cone_wander"
    " : u_shade_origin;\n"
    "  int sx=int(262144.0/u_focal.x), sy=int(262144.0/u_focal.y);\n"
    "  int px=(abs(wrap16(source_column()-origin.x)) * sx) >> 8;\n"
    "  int py=(abs(wrap16(int(gl_FragCoord.y)-origin.y)) * sy) >> 8;\n"
    "  float radial=u_shade_round!=0 ? length(vec2(float(px),float(py)))"
    " : float(max(px,py));\n"
    "  float edge=float(u_candle_word)*float(1 << shr);\n"
    "  if (edge>0.0) {\n"
    "    float w=0.5*edge;\n"
    "    float t=clamp((radial-(edge-w))/(2.0*w),0.0,1.0);\n"
    "    radial-=0.75*u_cone_penumbra*2.0*w*t*t*(2.0-1.3333333*t);\n"
    "  }\n"
    "  if (u_shade_round!=0) return float(depth)+radial;\n"
    "  return float((depth+int(radial)) & 65535);\n"
    "}\n"
    "float shade_amount(int selected_level, bool tinted) {\n"
    "  int level=selected_level; int shr=5+level;\n"
    "  int light=effective_light();\n"
    "  if (light==0) return 0.0;\n"
    "  int axv=light==0 ? 0 : light-128;\n"
    "  int bias=8+axv;\n"
    "  int scale=light==0 ? 32 : 32-(max(axv+4,0) >> 2);\n"
    "  int transformed=int(floor(max(v_depth_fixed*u_depth_scale,0.0)*256.0));\n"
    "  int depth=(transformed >> 8) & 65535;\n"
    "  bool flat_surface=(v_light_material.y & 8u)!=0u; float r;\n"
    "  if (flat_surface) {\n"
    "    if (tinted) {\n"
    /* The engine samples a flat's candle only at the corners of the polygon it
     * rasterises and interpolates between them, which is what splits a room's
     * pool into two lobes that slide as the light position moves. The smooth
     * law samples exactly there, because that shape is the original picture's
     * and only the quantizer is being taken out of it. A law that asks to be
     * read at the fragment gets the whole pool, in one piece, wherever the
     * fragment is -- and uniformly brighter than the chord between two corners,
     * because the law is convex where it is not clamped. */
    "      if (u_shade_per_fragment==0 && v_flat_taint==0u &&"
    " (v_flat_span.w & 0x80000000u)==0u)"
    " return clamp(v_wall_shade_endpoint/256.0,0.0,31.0);\n"
    "      r=candle_edge_depth(depth)/float(1 << shr)-float(bias)"
    "-float(u_flat_edge_bias);\n"
    /* The row law's low clamp is one, not zero, on both candle paths: a term at
     * or below zero is drawn at row 1.  Clamping the float to 1.0 reproduces
     * that for every fraction below it as well. */
    "      return clamp(r,1.0,31.0);\n"
    "    }\n"
    "    r=float(depth)/float(1 << shr)-float(bias);\n"
    "    return clamp(r,0.0,float(min(scale,31)));\n"
    "  }\n"
    "  if (tinted) {\n"
    /* And a wall's candle is seeded at the corners of its segment and walked
     * down each column, which is the same corner sampling one axis at a time,
     * and the same reason for keeping it. The per-fragment form below is the
     * law those corner words were made from, evaluated where it is being
     * drawn. */
    "    if (u_shade_per_fragment==0 && v_wall_span.w!=0u) {\n"
    "      int scol=int(gl_FragCoord.x)-int(v_wall_span.x); if (scol<0) scol=0;\n"
    "      uint ta=v_wall_rows.x+v_wall_rows.z*uint(scol);\n"
    "      uint ba=v_wall_rows.y+v_wall_rows.w*uint(scol);\n"
    "      float stop=float(signed16(ta>>16u));\n"
    "      float sbot=float(signed16(ba>>16u));\n"
    "      float cnt=sbot-stop+1.0;\n"
    "      float t=cnt>0.5 ? clamp((floor(gl_FragCoord.y)-stop)/cnt,0.0,1.0) : 0.0;\n"
    "      float V=mix(v_wall_shade_top,v_wall_shade_endpoint,t);\n"
    "      return clamp(V/256.0,0.0,31.0);\n"
    "    }\n"
    "    {\n"
    "      int adj=int(v_wall_tint_adjust);\n"
    "      r=candle_edge_depth(depth)/float(1 << shr)-float(bias-adj)"
    "-float(u_wd_bias);\n"
    "      return clamp(r,1.0,float(min(scale,31)));\n"
    "    }\n"
    "  }\n"
    "  int dividend=2 << level;\n"
    "  r=float(depth)/float(16*dividend)-float(bias);\n"
    "  return clamp(r,0.0,float(min(scale,31)));\n"
    "}\n"
    /* The room's own light, as the map states it: a REACH rather than a level.
     *
     * The authored sector byte is the engine's depth bias -- bias = light - 120
     * -- and its whole job is to say WHERE a room starts going dark, not how
     * bright its surfaces are. At the player's feet nearly every sector of a map
     * is drawn at its class's brightest row whatever its byte says; the byte
     * only separates two rooms once you back away from them. So a room term that
     * reads the byte alone reads the one place the byte says nothing, and the
     * quantity to read instead is the engine's own shade.
     *
     * This is shade_amount() with the candle's own amplitude taken out of it:
     * the same class selection, the same divisor, the same bias, the same
     * screen radial, the same receding-wall penalty, the same clip ceiling and
     * the same clamps. Only the staged candle word leaves.
     *
     * The radial and the penalty STAY, because they are the candle-lit CLASS's
     * own metric rather than the candle's light. The original keeps both with
     * no candle in hand at all: standing in a standard sector it stages
     * C = word - 13 (renderer.c:9385-9391, collision_physics.c:1774-1778) --
     * a NEGATIVE candle -- and still measures every candle-lit face by
     * (depth + radial)/unit - bias + adjust - C (renderer.c:12285-12301,
     * 4322-4340). A room of this class goes dark by two distances at once:
     * how far away a surface is, and how far it sits from the middle of the
     * view -- a cone about the view axis, roughly fourteen degrees to its
     * edge, that darkens the ground at the player's own feet and everything
     * off to the sides. Dropping the radial here drew all of that at the
     * view-centre value: brighter than the original everywhere off the axis,
     * by up to half the display range at the screen's edge.
     *
     * The radial is read through cone_edge_depth() -- candle_edge_depth's own
     * fixed centre and round metric, the parts this law already declares for
     * its shade rows, with the penumbra dial's softened transition in front of
     * them -- so the room darkens by the same measure the rows beside it are
     * taken by, and at a penumbra of nothing by exactly the term it always
     * was. The round metric is at most a sixth of a row wider than the
     * engine's square one on either screen axis and +5.2 rows at the extreme
     * corner, which is the documented cost of a pool without square corners.
     * The receding-wall penalty (v_wall_tint_adjust) applies to wall
     * fragments; the engine's flat projector has no such term, and a flat's
     * byte is 8 there.
     *
     * The candle-lit class's OWN rows arrive as `candle`, from the light the
     * player carries, rather than being spent here. That constant is the
     * candle's own amplitude -- the word the engine stages for its wall and
     * flat edge projectors alike, from the view record and the player's own
     * sector -- so a room whose faces the map marks candle-lit keeps its
     * near-field brightness only while there is a light in the hand supplying
     * it. What is left here is the room's own authored statement, its class's
     * whole metric, and the floor of one row, which is taken on the SUM
     * exactly where the engine takes it.
     *
     * The standard class needs no branch between wall and flat: the standard
     * wall's divisor 16*(2<<level) IS the flat's shift 1<<(5+level), the same
     * number written two ways. */
    "float room_amount(int selected_level, bool tinted, float candle) {\n"
    "  int level=selected_level; int shr=5+level;\n"
    "  int light=effective_light();\n"
    "  if (light==0) return 0.0;\n"
    "  int axv=light-128;\n"
    "  int bias=8+axv;\n"
    "  int scale=32-(max(axv+4,0) >> 2);\n"
    "  int transformed=int(floor(max(v_depth_fixed*u_depth_scale,0.0)*256.0));\n"
    "  int depth=(transformed >> 8) & 65535;\n"
    /* The candle-lit class's floor is taken the way the row law takes it: a term
     * at or below the floor is that floor whatever the sector's clip ceiling
     * says, rather than the two clamped in the other order. The two agree
     * everywhere but at a sector bright enough for its ceiling to fall below one
     * row, where the original draws the near field one row down and everything
     * past it fullbright -- and reproducing the original includes reproducing
     * that. */
    "  if (tinted) {\n"
    "    int adj=(v_light_material.y & 8u)!=0u ? 0 : int(v_wall_tint_adjust);\n"
    "    float r=cone_edge_depth(depth,shr)/float(1 << shr)-float(bias-adj)"
    "-candle;\n"
    "    return r<1.0 ? 1.0 : min(r,float(min(scale,31)));\n"
    "  }\n"
    "  float r=float(depth)/float(1 << shr)-float(bias);\n"
    "  return clamp(r,0.0,float(min(scale,31)));\n"
    "}\n"
    "flat in vec3 v_normal;\n"
    "flat in vec3 v_normal_world;\n"
    /* The fragment's position relative to the eye, in world units, in the view's
     * own space -- which is the space every reflected view already lives in, so
     * a term written against it is right in a mirror with no reflection rule.
     * The projection is invertible and the depth varying is perspective correct,
     * so this is the exact position rather than an interpolation of one, and it
     * costs no vertex bytes and no CPU work. */
    "vec3 view_position() {\n"
    "  float d=v_depth_fixed;\n"
    "  return vec3((gl_FragCoord.x-u_center.x)*d/u_focal.x, d,\n"
    "              (gl_FragCoord.y-u_center.y)*d/u_focal.y);\n"
    "}\n"
    /* What a lighting law is to use as this fragment's normal. Three answers:
     * the class says no light reaches here; the class says the surface is a
     * standing figure, so the normal is upright and faces the eye wherever the
     * fragment is; or the emitter authored one and it is in the vertices. All
     * zero means there is none, and every law has to read that as unlit rather
     * than as a direction. */
    "vec3 surface_normal() {\n"
    "  uint cls=v_light_material.w;\n"
    "  if ((cls & 64u)!=0u) return vec3(0.0);\n"
    "  if ((cls & 32u)!=0u) {\n"
    "    vec2 h=view_position().xy;\n"
    "    float len=length(h);\n"
    "    return len>0.0 ? vec3(-h/len,0.0) : vec3(0.0,-1.0,0.0);\n"
    "  }\n"
    "  return v_normal;\n"
    "}\n"
    /* One component of that normal as a palette index, for reading a class's
     * normals back off a rendered frame: 128 is zero, 1 and 255 are the two
     * ends, and 0 is reserved for a fragment no light reaches, which is the
     * value the index would never otherwise take. */
    "uint normal_component_index(vec3 n, int axis) {\n"
    "  if (n==vec3(0.0)) return 0u;\n"
    "  float c=axis==0 ? n.x : (axis==1 ? n.y : n.z);\n"
    "  return uint(clamp(floor(c*127.0+128.5),1.0,255.0));\n"
    "}\n"
    /* Which of the five classes a lighting law sorts this fragment into, as a
     * palette index, for reading the sorting back off a rendered frame. Four of
     * the five are the ones where a one-line mistake makes something glow: a
     * face the author declared unshaded, a face the map marked for the light the
     * player carries, an emissive face, and a surface no light may reach at all.
     * The fifth is everything else. */
    "uint light_class() {\n"
    "  uint face=v_pan_scale_flags.w;\n"
    "  if ((v_light_material.w & 64u)!=0u) return 4u;\n"
    "  if (effective_light()==0) return 0u;\n"
    "  if ((face & 64u)!=0u && (u_glow_phase & 73)!=0) return 2u;\n"
    "  return (face & 2u)!=0u ? 1u : 3u;\n"
    "}\n"
    "#endif\n"
    "uniform int u_debug_vis;\n"
    "uniform int u_depth_only;\n"
    /* Every index write in main() goes through emit(). The authentic build
     * writes the palette index and nothing else, exactly as the assignments it
     * replaced did. The true-colour build writes the same index and, beside it,
     * the colour that index already resolves to -- so with every dependent at
     * its neutral value the resolved canvas is the authentic canvas, byte for
     * byte, rather than an approximation of it. Later lighting features move
     * u_gain / u_add and clear u_light_neutral; they never touch this
     * structure, and they never touch any index-space mechanism. */
    "#ifndef HWR_TRUECOLOR\n"
    "void emit(uint index) { out_index=index; }\n"
    "#else\n"
    "layout(location=2) out vec4 out_color;\n"
    /* The plane the transmittance pass writes. It joins the draw buffers for
     * that pass alone; every other pass leaves it out, so nothing else this
     * program writes can reach it. */
    "layout(location=3) out float out_transmit;\n"
    "uniform sampler2D u_palette;\n"
    /* The previous frame, in colour, and whether there is one. Sampled at the
     * same pixel and from the same moment as the palette-index snapshot beside
     * it, so the two never describe two different frames. */
    "uniform sampler2D u_stale_color;\n"
    "uniform int u_stale_color_valid;\n"
    "uniform vec3 u_gain;\n"
    "uniform vec3 u_add;\n"
    "uniform float u_contrast;\n"
    "uniform int u_light_neutral;\n"
    /* The shading law, and the bisect handle that resolves the smooth law at its
     * lower row instead of between two -- which is the form the smooth picture's
     * exactness is measured in. */
    "uniform int u_shading_mode;\n"
    "uniform int u_shade_ramp;\n"
    /* The working space the composition runs in. The palette holds display-
     * referred colour and the smooth law reads BETWEEN two entries of it, which
     * is a display-space operation: linearizing for it would move colours the
     * map authored for no gain, so it composes where the palette lives and this
     * is zero. A law that adds light rather than reading a ramp has to add it in
     * linear, one conversion in and one back out; the pair is its own inverse,
     * which is what makes turning it on with nothing else moving leave the
     * picture exactly where it was. */
    "uniform int u_light_linear;\n"
    "vec3 to_linear(vec3 c) { return pow(max(c,0.0),vec3(2.2)); }\n"
    "vec3 to_display(vec3 c) { return pow(max(c,0.0),vec3(1.0/2.2)); }\n"
    /* A colour somebody chose, in the space the composition is running in.
     * Every colour a light carries arrives display-referred -- that is the space
     * it was picked in and the space it is written down in -- and the one place
     * that decides where light is added is here. */
    "vec3 light_space(vec3 c) { return u_light_linear!=0 ? to_linear(c) : c; }\n"
    /* The modern law's rig. The authored sector byte, the fill, the light at the
     * eye, and the direction slot a later key light fills; each colour and its
     * strength stay apart so the strength scales light rather than a colour. */
    "uniform vec2 u_room_light;\n"          /* gain, exponent */
    /* The authored byte placed between two anchors instead of scaled by the
     * gain alone: which law is in force, the two INPUT anchors (already taken to
     * the exponent, on the side that knows the map), the two OUTPUT levels they
     * emit, and the shaping between them.
     *
     * The reason this is not one multiplier is arithmetic rather than taste. One
     * gain scales the unlit base and the lit peak by the same factor, so the
     * ratio between two sectors survives and their DIFFERENCE -- which is what
     * an eye reads -- collapses with the darkness. Maps are authored bright, so
     * the gain a dark world needs is exactly the gain that flattens every light
     * switch and every candle in it. Normalizing between the map's own anchors
     * instead makes a step of a few bytes a fixed share of the whole dark-to-lit
     * swing, and that share does not depend on how dark the world is set. */
    /* Which of the three laws is in force: 0 the gain alone, 1 the anchored
     * pair, 2 the engine's own. Zero is what a rig nobody has filled in
     * resolves to, so the oldest law is still the one a caller that knows
     * nothing of any of this gets. */
    "uniform int u_room_curve;\n"
    "uniform vec2 u_room_anchor;\n"         /* unlit, lit -- in the exponent's space */
    "uniform vec2 u_room_level;\n"          /* what those two emit */
    "uniform float u_room_shape;\n"
    /* How far a room's own light carries, as a multiplier on the reach the map
     * authored. One is the map read as written, which is the original's own
     * picture; below one every room goes dark nearer its own start, so a dim
     * room is black long before a bright one has noticed. It is the darkness
     * dial of the engine law, and it is a dial no setting of the older ones
     * could be: one gain scales a lit room and an unlit one by the same factor,
     * so it moves the whole picture down without ever separating them. */
    "uniform float u_room_reach;\n"
    /* And how softly that light ends, in the ramp's own steps. Nothing is the
     * straight cut this replaced; one step is the largest height the row law's
     * own ceiling never reaches at the map read as written, so it is the
     * largest that leaves the original's picture where it is. */
    "uniform float u_room_shoulder;\n"
    /* How far the author's unshaded class is pulled down into the room's own
     * light. Zero leaves it exactly fullbright and it never arrives here. */
    "uniform float u_room_unshaded;\n"
    /* The two ends of what a fragment may receive: the darkest anything is
     * allowed to be, and where light stops adding up -- the second as the knee
     * light_knee() eases into rather than a cut, which at one is the same
     * picture the cut drew. */
    "uniform vec2 u_light_limits;\n"
    "uniform int u_room_ceiling_authentic;\n"
    "uniform float u_distance_shading;\n"
    "uniform vec3 u_ambient_color;\n"
    "uniform float u_ambient_intensity;\n"
    "uniform vec3 u_lamp_color;\n"
    "uniform float u_lamp_intensity;\n"
    "uniform float u_lamp_range;\n"
    /* And how far along the view axis it is carried, as a share of that reach.
     * Nothing puts it on top of the player, which is where the pool was; the
     * original's own candle is a cone about the direction he is looking. */
    "uniform float u_lamp_forward;\n"
    "uniform int u_lamp_scope_all;\n"
    /* And the other half of that light, which is the half the map already had a
     * word for. How much of a candle-lit room's OWN candle the light in the
     * player's hand supplies, and how much of what it supplies is shaped like a
     * pool at the eye rather than filling the room the way the original's does.
     * The flame and the map's own lantern word are already spent into the
     * first, so a fragment reads one number for both. */
    "uniform float u_candle_amount;\n"
    "uniform float u_candle_pool;\n"
    /* The two halves of the engine's staged word, split where the split
     * can use them: the map's own candle amplitude (poolable, the light in
     * the hand -- u_candle_word, declared up with the cone terms that also
     * read it), and the player-state term -- zero with the player standing
     * in the candle-lit class, minus thirteen rows times the candle dial
     * when he is not, exactly the step the original draws at a doorway
     * between the classes. The state is a statement about the whole room,
     * so no pool shapes it. */
    "uniform float u_candle_state;\n"
    /* What the light the player carries had to pass through to reach each
     * pixel, written by the pass above and read here; whether this frame has
     * such a plane at all; whether this draw is that pass; and how much of what
     * is behind it a blended texel lets through, which is the weight the map's
     * own blend table gives its destination. */
    "uniform sampler2D u_transmit;\n"
    "uniform int u_transmit_valid;\n"
    "uniform int u_transmit_pass;\n"
    "uniform float u_transmit_blend;\n"
    /* And whether the draw this fragment belongs to is one the pass cast from.
     * It is the DRAW that decides it, never the material: the pass casts from
     * everything in the translucent range, and what a surface is made of is not
     * what says whether it is in it. */
    "uniform int u_transmit_receive;\n"
    "uniform vec2 u_sprite_light;\n"        /* gain, wrap */
    /* The sun/moon slot: bound, added, and zero. An enhancement ships with its
     * own key or it does not ship, so there is no row behind these yet -- what
     * is here is the shape the later one lands in, which costs one multiply of
     * zero and no restructuring. The direction is in the view's own space, so a
     * reflected view is lit from the reflected side by construction. */
    "uniform vec3 u_key_dir;\n"
    "uniform vec3 u_key_color;\n"
    "uniform float u_key_intensity;\n"
    /* The light-space depth map that gates the key term, and the constant
     * world-to-light transform it is read through. The transform is one
     * matrix for every view of the frame because it is in WORLD space --
     * which reflected views share, since they draw the same world geometry --
     * so a mirror needs no rule of its own here either. */
    "uniform int u_sun_valid;\n"
    "uniform mat4 u_sun_matrix;\n"
    "uniform sampler2DShadow u_sun_shadow;\n"
    "uniform float u_sun_bias;\n"
    /* The receiver-slope bias law's two staged terms, and the softness ring.
     * Slope zero stands the law down and the ceiling above is the whole
     * bias, which is the first milestone's arithmetic kept reachable. */
    "uniform float u_sun_bias_floor;\n"
    "uniform float u_sun_bias_slope;\n"
    "uniform int u_sun_soft;\n"
    "uniform float u_sun_texel;\n"         /* one depth-map texel, in uv */
    "uniform int u_sun_taps;\n"
    "uniform float u_sun_radius;\n"        /* ring radius, in lookup uv */
    /* The transmittance plane in the light's own space -- the gobo: 1 where
     * the beam passes whole, the pass weight where a pane filters it; the
     * translucent range's opaque paint blocks through the depth map and
     * this plane holds the whole beam over it. */
    "uniform sampler2D u_sun_gobo;\n"
    "uniform int u_sun_gobo_valid;\n"
    /* A neighbour tap, held to what its column can honestly say about THIS
     * receiver. The position snaps to its texel's centre first, so the
     * carried reference and the stored texel describe the same point. A
     * dark answer stands: something in that column blocks the light above
     * the receiver's depth. A LIT answer is evidence only under the band
     * rule of the sampling law (hwr_sun_math.h): the column's first caster
     * must be the receiver's own surface -- the compare one band deeper
     * separates that from a column that crossed into ground no surface
     * models (the void past the map's skin, solid rock into the next
     * room), whose average was the dim hairline of 'outside' light along
     * the seams of the director's save. A refused tap falls back to the
     * fragment's own carried compare; a genuine penumbra keeps both
     * halves, its lit taps reading the receiver's own surface inside the
     * band. */
    "float sun_tap(vec2 uv, float ref, vec2 grad, vec2 at, float own,\n"
    "              float band) {\n"
    "  vec2 c=u_sun_texel>0.0?(floor(uv/u_sun_texel)+0.5)*u_sun_texel:uv;\n"
    "  float carry=clamp(dot(grad,c-at),-u_sun_bias,u_sun_bias);\n"
    "  float r=clamp(ref+carry,0.0,1.0);\n"
    "  float lit=texture(u_sun_shadow,vec3(c,r));\n"
    "  float beyond=texture(u_sun_shadow,vec3(c,clamp(r+band,0.0,1.0)));\n"
    "  return lit*mix(1.0,own,beyond);\n"
    "}\n"
    /* How much of the key light reaches this fragment: the depth compare at
     * the fragment's own world position -- the four-texel blend when the
     * softness dial stands off its rest, ring taps beyond it -- times what
     * the translucent range let through. Outside the moving window the
     * answer clamps to LIT -- at the default span that edge is past the
     * engine's own solid-fill distances, and the room's reach law carries
     * the far field.
     *
     * This is the executable half of THE SAMPLING LAW stated whole at the
     * top of hwr_sun_math.h. In one line: every compare's reference is
     * carried along the receiver's own plane to the exact texel centre it
     * reads, and the bias pays for arithmetic alone -- the floor for a
     * receiver whose plane is known (every world surface, via its packed
     * normal), the derivative slope law for the plane-less classes (a
     * standing figure's card), the ceiling where either degenerates. The
     * own-texel carry below is what retired the slope slack here: with the
     * reference exact at the texel centre, a wall facing the light and a
     * grazed floor alike need no slack a joint could leak through. */
    "float sun_factor() {\n"
    "  if (u_sun_valid==0) return 1.0;\n"
    "  vec3 p=(u_sun_matrix*vec4(v_world,1.0)).xyz;\n"
    "  if (p.x<0.0 || p.x>1.0 || p.y<0.0 || p.y>1.0) return 1.0;\n"
    "  float bias=u_sun_bias;\n"
    "  vec2 grad=vec2(0.0);\n"
    "  bool planar=false;\n"
    "  if (u_sun_bias_slope>0.0) {\n"
    "    vec3 nw=v_normal_world;\n"
    "    if (nw!=vec3(0.0)) {\n"
    "      vec3 ga=vec3(u_sun_matrix[0].x,u_sun_matrix[1].x,u_sun_matrix[2].x);\n"
    "      vec3 gb=vec3(u_sun_matrix[0].y,u_sun_matrix[1].y,u_sun_matrix[2].y);\n"
    "      vec3 gc=vec3(u_sun_matrix[0].z,u_sun_matrix[1].z,u_sun_matrix[2].z);\n"
    "      vec3 t1=cross(nw,gb);\n"
    "      vec3 t2=cross(nw,ga);\n"
    "      float au=dot(ga,t1);\n"
    "      float bv=dot(gb,t2);\n"
    "      if (abs(au)>1e-12 && abs(bv)>1e-12) {\n"
    "        grad=vec2(dot(gc,t1)/au,dot(gc,t2)/bv);\n"
    "        bias=min(u_sun_bias_floor,u_sun_bias);\n"
    "        planar=true;\n"
    "      }\n"
    "    }\n"
    "    if (!planar && nw==vec3(0.0)) {\n"
    "      vec3 px=dFdx(p); vec3 py=dFdy(p);\n"
    "      float det=px.x*py.y-py.x*px.y;\n"
    "      if (abs(det)>1e-16) {\n"
    "        float du=(py.y*px.z-px.y*py.z)/det;\n"
    "        float dv=(px.x*py.z-py.x*px.z)/det;\n"
    "        grad=vec2(du,dv);\n"
    "        bias=min((abs(du)+abs(dv))*u_sun_bias_slope+u_sun_bias_floor,\n"
    "                 u_sun_bias);\n"
    "      }\n"
    "    }\n"
    "  }\n"
    "  float ref=clamp(p.z-bias,0.0,1.0);\n"
    /* The fragment's own column -- the NEAREST compare reads the texel
     * containing p.xy, so the reference is carried to that texel's CENTRE
     * exactly as the soft taps carry theirs: for a plane-carrying receiver
     * the compare is then exact at the point compared, and the floor is
     * the whole of the slack. The own carry obeys the band rule too: where
     * the own texel's centre stands past the receiver's physical edge (a
     * fragment hard against a joint, its texel centre over the joint), the
     * carried plane extrapolates into open sky above the very ceiling that
     * seals the joint -- the lit answer lands far beyond the band, and the
     * compare retakes UNCARRIED at the fragment itself, which is the
     * honest local question. That extrapolated own was the last bright
     * strip on the director's pillar. */
    "  float band=2.0*bias+u_sun_bias_floor;\n"
    "  float own;\n"
    "  if (planar && u_sun_texel>0.0) {\n"
    "    vec2 oc=(floor(p.xy/u_sun_texel)+0.5)*u_sun_texel;\n"
    "    float ocarry=clamp(dot(grad,oc-p.xy),-u_sun_bias,u_sun_bias);\n"
    "    float ro=clamp(ref+ocarry,0.0,1.0);\n"
    "    float o1=texture(u_sun_shadow,vec3(p.xy,ro));\n"
    "    float ob=texture(u_sun_shadow,vec3(p.xy,clamp(ro+band,0.0,1.0)));\n"
    "    own=o1*mix(1.0,texture(u_sun_shadow,vec3(p.xy,ref)),ob);\n"
    "  } else {\n"
    "    own=texture(u_sun_shadow,vec3(p.xy,ref));\n"
    "  }\n"
    "  float s;\n"
    "  if (u_sun_soft==0 || u_sun_texel<=0.0) {\n"
    "    s=own;\n"
    "  } else {\n"
    /* The barely-soft blend, by hand: the four nearest texels each compared
     * against the reference carried onto them, mixed by the fragment's own
     * position between their centres -- the same picture the hardware
     * filter draws on a plane, without the texel of bias slack it would
     * need at a shading joint. */
    "    vec2 t=p.xy/u_sun_texel-0.5;\n"
    "    vec2 f=fract(t);\n"
    "    vec2 base=(t-f+0.5)*u_sun_texel;\n"
    "    float s00=sun_tap(base,ref,grad,p.xy,own,band);\n"
    "    float s10=sun_tap(base+vec2(u_sun_texel,0.0),ref,grad,p.xy,own,band);\n"
    "    float s01=sun_tap(base+vec2(0.0,u_sun_texel),ref,grad,p.xy,own,band);\n"
    "    float s11=sun_tap(base+vec2(u_sun_texel),ref,grad,p.xy,own,band);\n"
    "    s=mix(mix(s00,s10,f.x),mix(s01,s11,f.x),f.y);\n"
    "  }\n"
    "  if (u_sun_taps>0) {\n"
    "    const vec2 ring[8]=vec2[8](vec2(0.7071,0.7071),vec2(-0.7071,0.7071),\n"
    "      vec2(0.7071,-0.7071),vec2(-0.7071,-0.7071),vec2(1.0,0.0),\n"
    "      vec2(-1.0,0.0),vec2(0.0,1.0),vec2(0.0,-1.0));\n"
    "    for (int i=0;i<u_sun_taps;i++)\n"
    "      s+=sun_tap(p.xy+ring[i]*u_sun_radius,ref,grad,p.xy,own,band);\n"
    "    s/=float(u_sun_taps+1);\n"
    "  }\n"
    "  if (u_sun_gobo_valid!=0) s*=texture(u_sun_gobo,p.xy).r;\n"
    "  return s;\n"
    "}\n"
    /* The lights that are NOT at the eye: a torch on a wall, a candle on a
     * table, a lit window. One array, whatever seeded them, so a class added
     * later is a seeder rather than a second lighting path -- and every one of
     * them is in the VIEW's own space, which is the same reason the key
     * direction above is: a reflected view carries its own camera and the term
     * is evaluated against it, so a mirror needs no rule of its own.
     *
     * How many are live is a uniform, and it is zero where nothing seeded one:
     * a map with no torch in view costs this exactly one comparison. */
    "uniform vec4 u_point_a[16];\n"         /* position.xyz, where it ends */
    "uniform vec4 u_point_b[16];\n"         /* colour.rgb, energy */
    "uniform vec2 u_point_z[16];\n"         /* the world-Z window it stays in */
    "uniform int u_point_count;\n"
    /* How softly that window's two edges fade, in world units. */
    "uniform float u_point_z_edge;\n"
    /* The three classes the modern law must not touch, and why each one would be
     * a disaster if it did.
     *
     * A surface the emitter marked unlit is a ground shadow above all: a
     * DARKENING decal, drawn at light zero, which any light at all would turn
     * into a bright oval under the figure it belongs to.
     *
     * A face whose effective light is zero is the original's unshaded class --
     * fullbright at every distance, by the author's own instruction. Leaving it
     * to the index it already resolved is that, exactly, rather than nearly, and
     * it is exact whatever the ceiling above is set to.
     *
     * The other two are refused before they ever arrive: a sky cap returns
     * before any shading runs, and a glow face resolves through its own table
     * and never reaches the branch this is called from. */
    "bool modern_lit() {\n"
    "  if ((v_light_material.w & 64u)!=0u) return false;\n"
    "  if (effective_light()!=0) return true;\n"
    /* The unshaded class is refused whole while nothing is pulling it down,
     * which is what keeps it fullbright to the byte rather than nearly. It is
     * only where somebody has asked for that class to come down into a dark
     * world that it arrives at the rig at all. */
    "  return u_room_unshaded>0.0;\n"
    "}\n"
    /* How much of the light the player carries reaches this fragment.
     *
     * The light is at the eye, so the ray it travels is the ray the view
     * travels: what stands between the two is exactly what the eye is looking
     * through, and the plane the pass before this one wrote says how much of
     * the light each of those pixels let past. For a fragment the opaque pass
     * drew this is not an approximation -- only surfaces nearer than the opaque
     * winner are in that plane, and the opaque winner is the fragment being
     * lit.
     *
     * A surface the pass CAST from keeps the whole light: its own transmittance
     * is in that pixel, and reading it would be a surface shadowing itself.
     * That leaves the light on a see-through surface to the one in front of it,
     * which is the bound this form is chosen at. */
    "float lamp_transmittance() {\n"
    "  if (u_transmit_valid==0 || u_transmit_receive==0) return 1.0;\n"
    "  return texelFetch(u_transmit,ivec2(gl_FragCoord.xy),0).r;\n"
    "}\n"
    /* How a light ENDS.
     *
     * A quantity that runs out at a finite distance arrives at nothing with a
     * slope, and the slope stops there: the value is a straight line and then
     * it is a flat zero, and the corner between the two is a first-derivative
     * discontinuity. An eye reads exactly that as an edge -- it is what a Mach
     * band is made of -- so a room whose light runs out draws a rim around the
     * player at the distance it runs out at, and the rim is the picture's only
     * feature there. It is the "black fog wall at the bounds of the light
     * bubble" said in arithmetic.
     *
     * Below the shoulder's own height the quantity decays toward nothing
     * instead of arriving at it: an exponential, leaving the identity with the
     * identity's own slope so there is no corner where the two meet, and with
     * a slope proportional to what is left so it vanishes with it. The value
     * is never exactly zero and does not need to be -- it is under a step of
     * an eight-bit canvas long before the room's own reach is spent.
     *
     * A shoulder of nothing is max(x,0), to the bit, which is the expression
     * this replaced. */
    "float light_shoulder(float x, float s) {\n"
    "  if (s<=0.0 || x>=s) return max(x,0.0);\n"
    "  return s*exp((x-s)/s);\n"
    "}\n"
    /* How a light in the WORLD falls off, in the one place all three of them
     * say it: the light at the eye, the candle-lit class's own rows, and every
     * torch on a wall.
     *
     * It is a windowed quadratic and it stays one. The same corner the room's
     * own reach draws is formally here too -- the display value is the linear
     * one to the one over gamma, whose slope at nothing is unbounded -- but
     * the window is squared, so what is left over the last stretch is below a
     * step of the canvas long before the bound arrives: for the strength the
     * rows ship at, the last 0.65% of the reach is under one level of 255,
     * which is eight world units of a reach of 1280. There is no edge there to
     * ease, and a shoulder wide enough to be worth having would leave a
     * PEDESTAL at the reach instead -- the bound cuts, and what it cuts is
     * then visible. The two cannot be had at once from one falloff, and the
     * bound is the one worth keeping, because it is what says how far a light
     * with nothing in its way can spill. */
    "float light_falloff(float r, float reach) {\n"
    "  float a=max(1.0-r/reach,0.0);\n"
    "  return a*a;\n"
    "}\n"
    /* The least light a surface may be left with, as a floor rather than as a
     * cut.
     *
     * A cut has the ceiling's own fault upside down (and the ceiling stopped
     * being one for exactly this reason): everything under it is painted the
     * SAME value, so the darkest part of a world becomes one flat wash with no
     * order in it at all -- which is the opposite of what a floor is asked
     * for. What makes a dark room navigable is not that it is above black, it
     * is that two surfaces in it are still two surfaces.
     *
     * So the floor is added where there is nothing and vanishes where there is
     * light: it is exactly f where the surface receives none, it is the
     * surface itself wherever the surface is well past f, it is strictly
     * increasing everywhere between, and it has no corner anywhere. A floor of
     * nothing is max(x,0), to the bit. */
    "vec3 light_floor(vec3 x, float f) {\n"
    "  x=max(x,vec3(0.0));\n"
    "  if (f<=0.0) return x;\n"
    "  return f*exp(-x/f)+x;\n"
    "}\n"
    /* What a surface actually shows: its own colour, times the light that
     * reaches it, held at or above the least a person is willing to lose.
     *
     * The floor is stated in the space a person READS -- a hundredth means a
     * hundredth of the picture's own brightest, which is two and a half levels
     * of two hundred and fifty-five, and every step of the row is worth the
     * same two and a half wherever it is taken. Stated on the light instead it
     * meant nothing anybody could predict: a hundredth of the light is a
     * seventh of the display, and then a fraction of that again depending on
     * how dark the texture underneath happened to be.
     *
     * A floor of nothing is the multiply this replaced, to the bit. */
    "vec3 lit_surface(vec3 art, vec3 total) {\n"
    "  float f=u_light_limits.x;\n"
    "  if (f>0.0 && u_light_linear!=0) f=pow(f,u_room_light.y);\n"
    "  return light_floor(art*total,f);\n"
    "}\n"
    /* Where the light the player carries actually sits.
     *
     * The original's candle is measured on the SCREEN, from an origin that
     * dances about the middle of the view, and the screen arm it measures is
     * an angle off the view axis: the row law adds 1024*tan(theta) to the
     * depth, so the candle is worth its whole amplitude straight ahead and
     * nothing at all past tan(theta) = C*unit/1024 -- a cone about the
     * direction the player is LOOKING, of half angle fourteen degrees, and the
     * same fourteen on every map because C*unit is 256 at every shade level
     * the game ships. Aiming the pool forward is therefore the original's own
     * semantics rather than a departure from them; a point of light sitting on
     * top of the player was the simplification.
     *
     * A cone with no distance falloff is not a thing this rig can hold, so
     * what it takes from the original is the AIM: the pool's own centre moves
     * along the view axis, as a share of the light's own reach, and what falls
     * off with distance falls off from THERE. So the light carries further
     * down the corridor the player is facing and less to either side of him,
     * which is what aiming a lantern does.
     *
     * What does NOT move is the direction the light arrives from. A light the
     * player carries is at the player, and a surface he is looking at is a
     * surface facing him; treating the aim as a position instead would light
     * everything past the aim from behind and leave the wall two paces in
     * front of him unlit, which is not a lantern, it is a lamp standing in the
     * room. The aim shapes the pool; the eye still holds the light. */
    "vec3 lamp_position() {\n"
    "  return vec3(0.0,u_lamp_forward*u_lamp_range,0.0);\n"
    "}\n"
    /* How much of a light that is NOT at the eye reaches this fragment, given
     * where the fragment is and which way it faces.
     *
     * It is the light at the eye's own term, line for line, with three
     * differences and no fourth: the direction runs from the fragment TO the
     * light instead of to the eye; there is no transmittance factor, because
     * the plane that carries one is exact only for a light sharing the view's
     * ray and a torch on a wall shares none; and the vertical window below.
     *
     * The window is the one containment that costs nothing. The projection
     * defines the view position's third component as the drop from the eye, so
     * the fragment's own height in the world is the eye's height less it --
     * exactly, with no new vertex data and no new varying. Each light carries
     * its own room's floor and ceiling with slack either side, and outside that
     * band it fades to nothing over the slack. That is what stops a torch
     * lighting the storey above it, and it is not a shadow and is not sold as
     * one. */
    "vec3 point_light_term(int i, vec3 p, vec3 n, bool figure) {\n"
    "  vec3 d=u_point_a[i].xyz-p;\n"
    "  float r=length(d);\n"
    "  float reach=u_point_a[i].w;\n"
    "  if (r>=reach || reach<=0.0) return vec3(0.0);\n"
    "  vec3 l=r>0.0 ? d/r : vec3(0.0,-1.0,0.0);\n"
    "  float ndl=dot(n,l);\n"
    "  float gain=1.0;\n"
    /* A standing figure is not a wall here either: the same wrap and the same
     * dial, because what makes a figure a figure is the figure, not which
     * light is falling on it. */
    "  if (figure) {\n"
    "    ndl=(ndl+u_sprite_light.y)/(1.0+u_sprite_light.y);\n"
    "    gain=u_sprite_light.x;\n"
    "  }\n"
    "  if (ndl<=0.0) return vec3(0.0);\n"
    "  float zw=float(u_eye_z)-p.z;\n"
    "  float edge=max(u_point_z_edge,0.03125);\n"
    "  float window=smoothstep(u_point_z[i].x,u_point_z[i].x+edge,zw)*\n"
    "               (1.0-smoothstep(u_point_z[i].y-edge,u_point_z[i].y,zw));\n"
    "  if (window<=0.0) return vec3(0.0);\n"
    /* The same falloff the light at the eye takes, from the one place all of
     * them take it, so the reach is where the pool ENDS rather than where it
     * stops being visible -- which is also what makes the reach a bound on the
     * leaking. */
    "  return light_space(u_point_b[i].rgb)*u_point_b[i].w*ndl*gain*\n"
    "         light_falloff(r,reach)*window;\n"
    "}\n"
    /* Where the light landing on a surface stops adding up, as a knee rather
     * than as a cut.
     *
     * Nothing downstream maps a value above the picture's own brightest back
     * into the range: the world image is multiplied by the texel's own colour
     * and read back into eight bits, so everything past that arrives as the same
     * white. A cut therefore does not merely stop the brightest surfaces getting
     * brighter, it makes them EQUAL -- two rooms a map states differently, and a
     * near wall and a far one, painted the same value with no way to tell them
     * apart. The room's own light running out keeps most of a world clear of
     * that, and a person turning the whole rig up walks straight into it.
     *
     * So above the threshold the total is eased toward one instead of clipped
     * at it: the curve leaves the identity with the same slope it arrives with,
     * approaches one from below without meeting it, and rises the whole way --
     * strictly, for as long as the arithmetic can still hold the gap it leaves
     * below one. That is the property that matters, because an ORDER that
     * survives is what lets two bright rooms still read as two rooms.
     *
     * At a threshold of one and above there is nothing left to ease into and the
     * curve IS the cut, to the bit: the branch below returns the threshold, and
     * the picture a threshold of one draws is exactly the picture the cut drew.
     * That is the identity this is written around, since one is what the row
     * holds as it ships. */
    "float light_knee(float x, float k) {\n"
    "  if (x<=k) return x;\n"
    "  float h=1.0-k;\n"
    "  if (h<=0.0) return k;\n"
    "  return k+h*(1.0-exp(-(x-k)/h));\n"
    "}\n"
    /* The rows the light a person carries takes off a candle-lit room's shade.
     *
     * The original's candle-lit law is its depth law less a CONSTANT number of
     * rows and less a screen-radial term measured from an origin that dances --
     * and that constant is the candle's own amplitude, staged for the wall and
     * the flat edge projectors alike from the view record and the player's own
     * sector. The room never authored it; the light in the hand did. So it is
     * spent from here, where a light's own switch, strength, flame and the map's
     * own lantern word can all reach it, rather than from the room's term where
     * nothing could.
     *
     * Its amount is the map's own number of rows at full, so at full the sum of
     * this and the room IS the original's candle-lit picture, term for term, and
     * a person who turns the light down or out is left with what the room itself
     * states. That is the whole of the split: the same picture at rest, and a
     * dial on the half of it the map says belongs to the candle.
     *
     * Its SHAPE is the one departure and it is a dial of its own. The original's
     * candle is measured in screen space, so at the middle of the view it is
     * worth the same at every distance -- it fills a candle-lit room rather than
     * pooling in it. At nothing this reproduces that exactly, which is why that
     * is where it rests; wound up, the amount falls off with the light's own
     * reach and with which way a surface faces, which is a pool at the eye and
     * is a picture the original could not draw. */
    "float candle_rows(bool tinted) {\n"
    "  if (!tinted || u_candle_amount<=0.0) return 0.0;\n"
    "  float rows=float(u_candle_word)*u_candle_amount;\n"
    "  if (u_candle_pool<=0.0) return rows+u_candle_state;\n"
    "  vec3 p=view_position();\n"
    /* The same falloff and the same reach the light's other half takes, so the
     * two describe one light rather than two -- and measured from where the
     * light is AIMED, which is a little way along the view axis, while the
     * direction it arrives from is still the eye that holds it. */
    "  float dist=length(p-lamp_position());\n"
    "  float eye=length(p);\n"
    "  float shape=light_falloff(dist,u_lamp_range);\n"
    "  vec3 n=surface_normal();\n"
    /* A surface with no direction of its own -- a mesh flat the emitter left
     * without one -- takes the distance term alone rather than nothing at all:
     * an absent normal is a missing answer, not an answer of "away". */
    "  if (n!=vec3(0.0)) {\n"
    "    vec3 l=eye>0.0 ? -p/eye : vec3(0.0,-1.0,0.0);\n"
    "    float ndl=dot(n,l);\n"
    "    if ((v_light_material.w & 32u)!=0u)\n"
    "      ndl=(ndl+u_sprite_light.y)/(1.0+u_sprite_light.y);\n"
    "    shape*=max(ndl,0.0);\n"
    "  }\n"
    "  return rows*mix(1.0,shape,u_candle_pool)+u_candle_state;\n"
    "}\n"
    /* What reaches this fragment, as a multiplier on the colour of its texel.
     *
     * The authored byte is a ratio in the palette's own perceptual space, so the
     * exponent is what turns it into light -- without it the map's common step
     * from 118 to 128 is a few percent and invisible. The weapon flash is free:
     * the engine adds it to the byte and the byte is what this reads, so a flash
     * is a smooth brightening instead of a jump between two rows. So are light
     * switches: the engine mutates the sector's byte and the mod re-reads it
     * every frame.
     *
     * `amount` is the continuous shade the original's own depth law would have
     * produced. Nothing uses it unless the distance dial is moved off zero,
     * which is the point: distance does not darken a surface, light runs out. */
    "vec3 modern_light(bool tinted, float amount, int level) {\n"
    "  int light=effective_light();\n"
    /* A figure is a person, a creature or something to be picked up, and what a
     * dark room must not do is make one unfindable. Its own dial is on the light
     * it MAKES of its own as well as the light it takes from the one the player
     * carries: an environment can be very dark while the things standing in it
     * stay readable, which is the whole reason the two are separate dials rather
     * than one. The fill is not in it -- that is the room's light, not the
     * figure's -- and a figure the author declared unshaded never arrives here
     * at all. */
    "  bool figure=(v_light_material.w & 32u)!=0u;\n"
    /* What the room gives off, by one of three laws. Only the newest of them
     * reads where the surface IS; the two below it are functions of the sector
     * and are left expression for expression as they were, which is what keeps
     * them reproducible to the bit rather than to a tolerance. */
    "  float room;\n"
    /* The engine's own law, which is a windowed power falloff with a per-sector
     * reach: the room's shade read through the ramp the colormap is built on,
     * which is (1 - row/32) to the gamma for every table the game ships. At
     * gain one, reach one, shape one and the candle-lit class's own rows carried
     * whole by the light in the player's hand, this IS the original's picture --
     * the same quantity, the same ramp, with the thirty-two steps taken out of
     * it -- so the law has a known-good starting point and every dial is a
     * departure from a picture rather than from a guess.
     *
     * The shaping row multiplies the exponent here rather than bending a
     * fraction between two levels: on this law the exponent IS the shape of the
     * falloff, and one leaves the map's own. */
    "  if (u_room_curve==2) {\n"
    /* And how that light ENDS, which is the shoulder above. The room's own
     * quantity runs out at the reach the map authored, and the shoulder's own
     * height is stated in the ramp's own steps -- so it scales with the reach
     * the way the picture does, and one step is the largest height that is
     * INERT at the map read as written: the row law's own ceiling of
     * thirty-one rows leaves the quantity at a step above nothing there and
     * never below it, so at a reach of one the branch above is never taken and
     * the picture is the original's to the bit. It is the shortening that
     * makes the rim, and this is a property of the shortening. */
    "    float span=32.0*u_room_reach;\n"
    "    float rows=room_amount(level,tinted,candle_rows(tinted));\n"
    "    room=pow(light_shoulder(1.0-rows/span,u_room_shoulder/span),\n"
    "             u_room_light.y*u_room_shape);\n"
    "  } else {\n"
    /* The authored byte, taken to the exponent: a ratio in the palette's own
     * perceptual space turned into light. The oldest law ends here, and the
     * anchored one places that between two levels. Both are written exactly as
     * they were written, one indent further in and not one operation
     * different. */
    "    float u=pow(float(light)/128.0,u_room_light.y);\n"
    "    room=u;\n"
    "    if (u_room_curve==1) {\n"
    /* Where this sector sits between the map's own unlit and lit ends, as a
     * fraction of the whole swing between them. A sector outside the pair is at
     * one end of it: a map's darkest corners are all "unlit" and its brightest
     * all "lit", which is what the two levels are for. */
    "      float x=clamp((u-u_room_anchor.x)/"
    "max(u_room_anchor.y-u_room_anchor.x,1e-4),0.0,1.0);\n"
    "      if (u_room_shape!=1.0) x=pow(x,u_room_shape);\n"
    "      room=mix(u_room_level.x,u_room_level.y,x);\n"
    "    }\n"
    "  }\n"
    "  vec3 total=vec3(u_room_light.x*room"
    "*(figure ? u_sprite_light.x : 1.0));\n"
    "  total+=light_space(u_ambient_color)*u_ambient_intensity;\n"
    "  vec3 n=surface_normal();\n"
    "  if (n!=vec3(0.0)) {\n"
    /* The light at the eye, which by default reaches everything it can see.
     *
     * The mark it can be confined to instead is not the author's word about
     * where a carried light matters: it is the engine's LIVE word about whether
     * a room's own fixtures are lit, cleared across a room the moment a switch
     * finishes raising it and set again when one lowers it. Confining the
     * carried light to it therefore put that light out in exactly the rooms
     * that had just been lit, which is the one case where the room's own byte
     * cannot make up the difference. The mark is still read, because it says
     * something true and useful; it is no longer what the carried light asks. */
    "    if (u_lamp_scope_all!=0 || tinted) {\n"
    "      vec3 p=view_position();\n"
    "      float dist=length(p-lamp_position());\n"
    "      float eye=length(p);\n"
    "      vec3 l=eye>0.0 ? -p/eye : vec3(0.0,-1.0,0.0);\n"
    "      float ndl=dot(n,l);\n"
    "      float gain=1.0;\n"
    /* A standing figure is not a wall: light wraps past the edge it would stop
     * at, so its far side is shaded rather than black, and it keeps its own
     * brightness dial so a dark room never makes it unfindable. */
    "      if (figure) {\n"
    "        ndl=(ndl+u_sprite_light.y)/(1.0+u_sprite_light.y);\n"
    "        gain=u_sprite_light.x;\n"
    "      }\n"
    /* A windowed quadratic rather than a true inverse square, so the reach is
     * exactly where the pool ends rather than where it becomes imperceptible.
     *
     * The plane that says what the light had to pass through is the EYE's ray,
     * and the light is now a little way along it. What stands between the
     * light and a fragment is therefore a subset of what stands between the
     * eye and it -- the plane can over-shadow and never under-shadow, and only
     * for something nearer than the pool's own centre. */
    "      total+=light_space(u_lamp_color)*u_lamp_intensity*"
    "max(ndl,0.0)*gain*light_falloff(dist,u_lamp_range)*lamp_transmittance();\n"
    "    }\n"
    /* The directional key -- the sun or the moon -- inside the same normal
     * branch as every other light, which is the whole of its class rule: the
     * gate that refuses a ground shadow, a sky cap and a glow face for the
     * lamp refuses them here too. World-anchored on purpose: turning the head
     * sweeps the candle cone and must not move a sunbeam. A standing figure
     * takes the same wrap and the same dial every light gives it. At an
     * intensity of exactly nothing the branch is never entered and the total
     * is what it always was, to the bit. */
    "    if (u_key_intensity>0.0) {\n"
    "      float kndl=dot(n,u_key_dir);\n"
    "      float kgain=1.0;\n"
    "      if (figure) {\n"
    "        kndl=(kndl+u_sprite_light.y)/(1.0+u_sprite_light.y);\n"
    "        kgain=u_sprite_light.x;\n"
    "      }\n"
    "      if (kndl>0.0)\n"
    "        total+=light_space(u_key_color)*u_key_intensity*kndl*kgain*"
    "sun_factor();\n"
    "    }\n"
    /* And the lights that are not at the eye. They are inside this branch on
     * purpose and it is the whole of their class rule: the one normal every law
     * here reads is zero for the surfaces no light may reach -- a ground shadow
     * above all, which is a DARKENING decal that any light at all would turn
     * into a bright oval under the figure it belongs to. A torch is refused by
     * the same gate that already refuses the light the player carries, so this
     * round introduces no class rule of its own. The count is zero wherever
     * nothing seeded a light, which is every frame of every other law. */
    "    for (int i=0;i<u_point_count;i++)\n"
    "      total+=point_light_term(i,view_position(),n,figure);\n"
    "  }\n"
    /* The original's depth law, at whatever weight is asked for. Its shade runs
     * from nothing to the whole ramp, so what survives is the ramp read the
     * other way round and taken to the same exponent the authored byte is. */
    "  if (u_distance_shading>0.0)\n"
    "    total*=mix(1.0,pow(max(1.0-amount/32.0,0.0),u_room_light.y),\n"
    "               u_distance_shading);\n"
    /* And the original's darkness ceiling, which is a floor on brightness: a
     * bright sector's surfaces never fade past its own clip row. It is a
     * thirty-two-step artifact and is dropped unless it is asked for. */
    "  if (u_room_ceiling_authentic!=0) {\n"
    "    float rows=float(min(32-(max(light-128+4,0) >> 2),31));\n"
    "    total=max(total,vec3(pow(max(1.0-rows/32.0,0.0),u_room_light.y)));\n"
    "  }\n"
    /* The author's unshaded class, on its way down into the room. It is reached
     * only where something is pulling it -- with nothing pulling, the class never
     * gets here -- and what it is pulled away from is the fullbright the author
     * asked for, so the two ends meet: at rest this is exactly the light that
     * class always had, and pulled the whole way it is lit like any other
     * surface of its sector. */
    "  if (light==0) total=mix(vec3(1.0),total,u_room_unshaded);\n"
    /* And the far end of the total, which is the knee above -- at the
     * threshold the row ships at, the clip this was written as, operation for
     * operation.
     *
     * The near end is no longer here. A floor on the LIGHT is a floor on a
     * multiplier: what it promises is a share of the colour a surface already
     * has, so a dark texture in a dark room keeps whatever fraction of dark it
     * started with and a person reads it as black anyway. What a floor is
     * asked for is that nothing is lost, and that is a statement about the
     * SURFACE. It is taken there, in lit_surface(), where it can be one. */
    "  return vec3(light_knee(total.r,u_light_limits.y),\n"
    "              light_knee(total.g,u_light_limits.y),\n"
    "              light_knee(total.b,u_light_limits.y));\n"
    "}\n"
    /* The unlit texel of the ordinary tail. Every other class is unlit in the
     * original and stays unlit here, and says so by leaving this unset. */
    "uint g_albedo_index=256u;\n"
    /* The colour a law that resolves between two rows arrived at, which no
     * single index can carry. The index beside it is still the row the fragment
     * floors to, so every index-space mechanism -- the blend tables, the stale
     * plane, the host's own composition -- keeps reading what it always read.
     * Unset means the index is the whole answer, which is every class the
     * original does not shade and every fragment in the authentic law. */
    "vec3 g_shade_color=vec3(0.0);\n"
    "bool g_shade_color_set=false;\n"
    /* Whether that colour is already in the space the composition runs in. A law
     * that READS between two authored entries hands over a display-referred
     * colour and the conversion below is its; a law that ADDS light has already
     * done the conversion to add in, and converting it back only to convert it
     * forward again would spend two more of them for nothing. */
    "bool g_shade_linear=false;\n"
    /* And whether it is a FINISHED colour rather than a shaded texel: a colour
     * this frame is copying from an earlier one, which the picture dials were
     * already spent on when it was drawn. Composing it again would spend them
     * twice, so it is handed to the canvas as it stands. */
    "bool g_shade_color_final=false;\n"
    /* What the light rig made of this fragment, kept because a blend below may
     * need it after the index it produced has already been decided. Unset means
     * no law lit this fragment, which is every class the modern law refuses and
     * every fragment of the other two laws. */
    "vec3 g_light_total=vec3(1.0);\n"
    "bool g_light_total_set=false;\n"
    "void emit(uint index) {\n"
    "  out_index=index;\n"
    "  vec4 lit=texelFetch(u_palette,ivec2(int(index),0),0);\n"
    "  if (u_light_neutral!=0) { out_color=lit; return; }\n"
    "  if (g_shade_color_final) {\n"
    "    out_color=vec4(max(g_shade_color,0.0),lit.a);\n"
    "    return;\n"
    "  }\n"
    "  uint albedo_index=g_albedo_index<256u ? g_albedo_index : index;\n"
    "  vec3 albedo=texelFetch(u_palette,ivec2(int(albedo_index),0),0).rgb;\n"
    "  vec3 base=g_shade_color_set ? g_shade_color : lit.rgb;\n"
    "  vec3 shaded=u_light_linear!=0\n"
    "      ? to_display((g_shade_linear ? base : to_linear(base))*u_gain\n"
    "                   +to_linear(albedo)*u_add)\n"
    "      : base*u_gain+albedo*u_add;\n"
    /* The picture's contrast: a pivot around mid grey, after everything that
     * decides how much light a surface got. The comparison is uniform across
     * the draw and it is what keeps one exactly one -- taking half away and
     * putting it back does not always round to the colour it started from, and
     * a dial at its original must not move a single byte. */
    "  if (u_contrast!=1.0) shaded=(shaded-0.5)*u_contrast+0.5;\n"
    /* Below black has no meaning in this space and would poison any later term
     * that adds to it. The top is left to the target's own range, which is the
     * headroom the floating-point format exists for. */
    "  out_color=vec4(max(shaded,0.0),lit.a);\n"
    "}\n"
    "#endif\n"
    "void main() {\n"
    "  uint material=v_light_material.y;\n"
    /* Portal-clipped geometry retains its original world vertices and texture
     * interpolation. Exact framebuffer-X bounds travel as dedicated 16.16
     * values, so walls, flats, meshes, and billboards share the same gate. */
    "  if (v_wall_vertical.w>v_wall_vertical.z) {\n"
    "    uint fx=uint(floor(gl_FragCoord.x*65536.0));\n"
    "    if (fx<v_wall_vertical.z || fx>=v_wall_vertical.w) discard;\n"
    "  }\n"
    /* Depth-only two is the pass that lays the opaque winner's depth down for
     * the transmittance test below, and a masked surface owns depth only where
     * its texture is not a hole -- so it takes the same discard the colour pass
     * takes, or the wall seen through the hole would not be the winner there.
     * Depth-only one is the mirror plane, which is deliberately not masked. */
    "#ifdef HWR_TRUECOLOR\n"
    "  if (u_depth_only==2 && (material & 1u)!=0u && sample_texel()==0u)\n"
    "    discard;\n"
    "#endif\n"
    /* A scheduled primary mirror is colourless but still owns depth at its
     * plane. This early exit deliberately bypasses texture masks and shading;
     * glDrawBuffers(GL_NONE) discards both outputs while depth is retained. */
    "  if (u_depth_only!=0) { emit(0u); out_floor_id=0u; return; }\n"
    /* The transmittance pass: how much of the light the player carries survives
     * this fragment on its way to whatever stands behind it.
     *
     * A hole is not a surface at all -- it discards, and the plane keeps the
     * whole light it was cleared to. A texel the blend tables decide passes the
     * light the map's own table gives what is behind it. Everything else is a
     * surface the light does not get through, whether it is the opaque texel of
     * a see-through sprite or a pane with no transparency in it.
     *
     * A ground shadow, a sky cap and every other surface its emitter marked as
     * one no light reaches is not an occluder either: it is a decal drawn onto
     * something, and darkening what is behind it would be the same darkness
     * twice. */
    "#ifdef HWR_TRUECOLOR\n"
    "  if (u_transmit_pass!=0) {\n"
    "    uint through=sample_texel();\n"
    "    if ((material & 1u)!=0u && through==0u) discard;\n"
    "    if ((v_light_material.w & 64u)!=0u) discard;\n"
    "    out_transmit=((v_light_material.w & 16u)!=0u ||\n"
    "                  ((material & 16u)!=0u && (through & 128u)!=0u))\n"
    "        ? u_transmit_blend : 0.0;\n"
    "    return;\n"
    "  }\n"
    "#endif\n"
    /* Opaque winner identity. World flats publish FLOOR_TAG | z as before,
     * except the few that a mid-platform of their own sector has to paint over:
     * those carry a sector-scoped identity in v_wall_source.x (a flat has no
     * wall-column state) so the platform's redraw cannot spill onto a
     * neighbouring flat that sits at the same height. Ordinary world walls and
     * fixed-orientation wall art carry WALL_TAG | owner-sector in
     * v_flat_span.x. Fixed wall art is marked as an overlay in
     * v_pan_scale_flags.w and, like camera-facing floor objects, deliberately
     * publishes zero. This preserves the host identity for the guarded redraw
     * and the shadow gate across ordinary object silhouettes. */
    "  out_floor_id=(material & 8u)!=0u &&"
    " (v_flat_span.w & 0x80000000u)==0u ?"
    " (v_wall_source.x!=0u ? v_wall_source.x :"
    " (0x40000000u | (uint(int(round(v_world.z))) & 0xffffu))) :"
    " ((v_flat_span.x & 0xc0000000u)==0x80000000u &&"
    " (v_pan_scale_flags.w & 0x80u)==0u ?"
    " v_flat_span.x : 0u);\n"
    /* Exception re-draw: keep only fragments over the matching opaque owner
     * surface. A nearer object, door, or wall from another sector records zero
     * or a different id and therefore remains an occluder. A flat spends
     * v_flat_span on its span state, so it carries the identity it must find
     * in v_wall_source.y instead. */
    "  if (u_floor_exception!=0) {\n"
    "    bool world_flat=(material & 8u)!=0u &&"
    " (v_flat_span.w & 0x80000000u)==0u;\n"
    "    uint fid=world_flat ? v_wall_source.y : v_flat_span.x;\n"
    "    if (fid==0u) discard;\n"
    "    if (texelFetch(u_floor_id,ivec2(gl_FragCoord.xy),0).r != fid) discard;\n"
    "  }\n"
    /* Surface-identity probes must reveal the OPAQUE winner behind a
     * translucent surface, not the translucent surface itself.  The
     * translucent pass writes colour with depth-write off, so under any probe
     * mode (2 and up) drop translucent fragments and let the recorded index
     * stay whatever opaque geometry owns the pixel. */
    /* The class and normal modes are the exception: what they ask about is the
     * surface that is there, and the two classes a light must never reach -- a
     * ground shadow and a see-through pane -- are both translucent, so dropping
     * them would hide exactly the fragments worth checking. */
    "  if (u_debug_vis>=2 && u_debug_vis<10 && (material & 16u)!=0u) discard;\n"
    /* Sky-cap fragment: fill the column with the screen-space skybox texel and
     * stop.  The math is a verbatim port of the retired sky prepass shader; the
     * cap geometry is what limits it to the region above each sky wall and lets
     * the depth test occlude the geometry behind the wall. */
    "  if ((material & 4u)!=0u) {\n"
    /* Engine-visible directed walls project vertex A left of B.  Because the
     * shader intentionally uses the engine's downward-positive screen Y, the
     * resulting cap winding is clockwise in GL window coordinates; therefore
     * GL's nominal front face is the side the engine rejects. */
    "    if (u_sky_occlusion_pass!=0 && gl_FrontFacing) discard;\n"
    /* Only a cap standing on its wall's TOPMOST edge may occlude.  A cap above
     * a lower part (a sill under an arched opening) sits below that opening, so
     * letting it write depth would hide the geometry seen through the arch. */
    "    if (u_sky_occlusion_pass!=0 && (material & 64u)==0u) discard;\n"
    "    if (u_sky_valid==0) discard;\n"
    /* A sky cap answers the class and the normal modes too, and its answer to
     * both is that no light reaches it -- which is what the class bit its
     * emitter set says. */
    "#ifdef HWR_TRUECOLOR\n"
    "    if (u_debug_vis==10) { emit(light_class()); return; }\n"
    "    if (u_debug_vis>=11 && u_debug_vis<=13)"
    " { emit(normal_component_index(surface_normal(),u_debug_vis-11)); return; }\n"
    "#endif\n"
    "    int tx=source_column(); int ty=int(gl_FragCoord.y);\n"
    "    float source_x=float(u_source_center.x)+"
    " (float(tx)-u_center.x)*float(u_source_focal.x)/u_focal.x;\n"
    "    float source_y=float(u_source_center.y)+"
    " (float(ty)-u_center.y)*float(u_source_focal.y)/u_focal.y;\n"
    "    int sx=int(floor(source_x));"
    " int sy=clamp(int(floor(source_y)),0,u_source_viewport.y-1);\n"
    "    int scolumn;\n"
    "    if (sx>=0 && sx<u_source_viewport.x) {\n"
    "      scolumn=int(texelFetch(u_texels,u_sky_projection_offset+sx).r);\n"
    "    } else {\n"
    /* The packed engine table covers only the original viewport. Continue its
     * defining perspective law for newly exposed columns. The table stores
     * the low byte of a 1024-unit turn (the 256-column sky repeats four times
     * per turn). Anchor the continuation to the live endpoint because the
     * engine's fixed-point table fill can differ from the analytic value by a
     * texel at the boundary. */
    "      float angle=atan((float(u_source_center.x)-source_x)/"
    " float(u_source_focal.x));\n"
    "      int edge_x=sx<0 ? 0 : u_source_viewport.x-1;\n"
    "      int edge_column=int(texelFetch(u_texels,"
    " u_sky_projection_offset+edge_x).r);\n"
    "      float edge_angle=atan((float(u_source_center.x)-float(edge_x))/"
    " float(u_source_focal.x));\n"
    "      scolumn=(edge_column+int(round((angle-edge_angle)*"
    " 162.97466172610084))) & 255;\n"
    "    }\n"
    "    scolumn=(scolumn-(u_sky_yaw << 1)) & 255;\n"
    "    int srow=u_sky_start_row+(u_sky_double_scanline!=0 ? (sy >> 1) : sy);\n"
    "    srow=clamp(srow,0,u_sky_texture_height-1);\n"
    "    uint stexel=texelFetch(u_texels,\n"
    "        u_sky_texture_offset+u_sky_source_offset+srow*256+scolumn).r;\n"
    "    emit(u_debug_vis==1 ? 255u : stexel);\n"
    "    return;\n"
    "  }\n"
    /* An all-corners-saturated tinted block (flagged on the wall-wrap attribute
     * bit 3) skips texel sampling and shading entirely and outputs the map fill
     * index, exactly like the engine's fast path -- which the engine takes for a
     * whole floor or ceiling polygon as readily as for a wall block.  Only in
     * normal rendering -- every debug_vis mode falls through so texel, offset,
     * material and shade-row attribution keep working on these surfaces. */
    /* A law that resolves between rows has a real, smoothly varying shade for
     * this block, so the fast path is the one thing it cannot keep: it would
     * paint one flat index over a surface the law has an answer for everywhere.
     * The decision is per fragment and nothing on the CPU changes -- the block
     * is already submitted and its texel is already sampleable. */
    "  if (u_debug_vis==0 &&"
    " (v_light_material.w & 8u)!=0u\n"
    "#ifdef HWR_TRUECOLOR\n"
    "      && u_shading_mode==0\n"
    "#endif\n"
    "     ) {\n"
    "    emit(uint(u_fill_index) & 255u);\n"
    "    return;\n"
    "  }\n"
    "  uint texel=sample_texel();\n"
    "  if ((material & 1u)!=0u && texel==0u) discard;\n"
    /* Mode 2: the raw pre-colormap texel byte, for offline texel-vs-shade
     * mismatch attribution. */
    "  if (u_debug_vis==2) { emit(texel & 255u); return; }\n"
    /* Modes 4 and 5 report which surface won a pixel: the low and high bytes
     * of its texture's byte offset in the store, which identifies the texture
     * uniquely. Mode 6 reports the material bits, which give the surface kind.
     * Capturing 4 and 5 together names the surface behind any pixel. */
    "  if (u_debug_vis==4) { emit(v_offset & 255u); return; }\n"
    "  if (u_debug_vis==5) { emit((v_offset >> 8) & 255u); return; }\n"
    "  if (u_debug_vis==6) { emit(material & 255u); return; }\n"
    "  if (u_debug_vis==7) { emit(v_dims.x & 255u); return; }\n"
    "  if (u_debug_vis==8) { emit(uint(dbg_ix) & 255u); return; }\n"
    "  if (u_debug_vis==9) { emit(uint(dbg_iy) & 255u); return; }\n"
    /* Mode 10 reports which class a lighting law sorts the fragment into, and
     * modes 11, 12 and 13 the three components of the normal it would use here,
     * one per capture. They exist only where a lighting law does: in the
     * authentic program they fall through and the fragment is shaded. */
    "#ifdef HWR_TRUECOLOR\n"
    "  if (u_debug_vis==10) { emit(light_class()); return; }\n"
    "  if (u_debug_vis>=11 && u_debug_vis<=13)"
    " { emit(normal_component_index(surface_normal(),u_debug_vis-11)); return; }\n"
    "#endif\n"
    /* A one-sided wall with a block-transparent texture: the engine's masked
     * writer skips index-0 texels, and no traversal ever paints behind a
     * one-sided wall, so the skipped pixel keeps what the PREVIOUS frame left
     * there.  Copying that stale pixel -- rather than discarding -- both shows
     * the engine's void smear and still occludes this frame's geometry behind
     * the wall, which the engine never draws. */
    "  if ((v_light_material.w & 4u)!=0u && texel==0u) {\n"
    /* The hole shows the previous frame, so under a law that composes colour it
     * shows the previous frame's COLOUR. The index beside it is still the index
     * that pixel held, so the plane the host composes against and everything
     * else in index space keeps reading exactly what it read before. */
    "#ifdef HWR_TRUECOLOR\n"
    "    if (u_stale_color_valid!=0) {\n"
    "      g_shade_color="
    "texelFetch(u_stale_color,ivec2(gl_FragCoord.xy),0).rgb;\n"
    "      g_shade_color_set=true;\n"
    "      g_shade_color_final=true;\n"
    "    }\n"
    "#endif\n"
    "    emit(texelFetch(u_stale,ivec2(gl_FragCoord.xy),0).r);\n"
    "    return;\n"
    "  }\n"
    "  uint mapped; uint face_flags=v_pan_scale_flags.w;\n"
    "  if ((material & 4u)!=0u) mapped=texel;\n"
    "  else if ((face_flags & 64u)!=0u && (u_glow_phase & 73)!=0)"
    " mapped=texelFetch(u_glow,ivec2(int(texel),1),0).r;\n"
    "  else { bool tinted=(face_flags & 2u)!=0u;"
    " int level=tinted ? u_tint_shade_level : u_shade_level;\n"
    "#ifdef HWR_TRUECOLOR\n"
    /* The smooth law. The shade quantity stays continuous, the index is still
     * the row it floors to, and the colour is the ramp read between that row and
     * the next -- two authored colours, mixed by the fraction the law landed
     * between them. At a whole row the mix is the lower colour and the picture is
     * the authentic picture exactly, which is what makes this arm auditable
     * against that one surface by surface.
     *
     * And that is its ONLY departure. The shade quantity it resolves is the
     * original's own, on every class: the same depth law, the same candle, the
     * same light position walking every seventh tick, the same shade read at the
     * corners the engine reads it at. Flooring it lands on the row the original
     * law returns, everywhere, which is the exactness claim stated as strongly
     * as it can be stated.
     *
     * The half-row dither is not carried over: it is a recovery trick for the
     * quantization this law does not perform, and against a continuous shade it
     * is only noise. */
    "    if (u_shading_mode!=0) {\n"
    "      float amount=shade_amount(level,tinted);\n"
    "      int row=int(floor(amount)); int next=min(row+1,31);\n"
    "      float between=u_shade_ramp!=0 ? amount-float(row) : 0.0;\n"
    "      if (u_debug_vis==1) { emit(uint(row)|(tinted ? 64u : 0u)); return; }\n"
    "      uint low=tinted ?"
    " texelFetch(u_tint,ivec2(int(texel),row),0).r :"
    " texelFetch(u_colormap,ivec2(int(texel),row),0).r;\n"
    "      g_shade_color_set=true;\n"
    "      mapped=low;\n"
    /* The modern law. The index stays the row the fragment floors to, exactly as
     * the ramp law leaves it -- every index-space mechanism in the frame keeps
     * reading what it always read -- and only the COLOUR beside it is decided
     * differently: the texel's own colour, multiplied by the light that reaches
     * it, rather than a place between two rows of an authored ramp.
     *
     * A class the rig refuses leaves the colour unset, which is the index's own
     * colour and therefore the authentic answer for it, byte for byte.
     *
     * The two laws are exclusive, which is what keeps the ramp's second table
     * lookup and its second palette lookup off the modern law: it has no use for
     * the row above, only for the row it recorded. */
    "      if (u_shading_mode==2) {\n"
    "        if (modern_lit()) {\n"
    "          vec3 art=light_space("
    "texelFetch(u_palette,ivec2(int(texel),0),0).rgb);\n"
    "          g_light_total=modern_light(tinted,amount,level);\n"
    "          g_light_total_set=true;\n"
    "          g_shade_color=lit_surface(art,g_light_total);\n"
    "          g_shade_linear=u_light_linear!=0;\n"
    "        } else {\n"
    "          g_shade_color_set=false;\n"
    "        }\n"
    "      } else {\n"
    "        uint high=tinted ?"
    " texelFetch(u_tint,ivec2(int(texel),next),0).r :"
    " texelFetch(u_colormap,ivec2(int(texel),next),0).r;\n"
    "        g_shade_color=mix(texelFetch(u_palette,ivec2(int(low),0),0).rgb,\n"
    "                          texelFetch(u_palette,ivec2(int(high),0),0).rgb,"
    "between);\n"
    "      }\n"
    "    } else\n"
    "#endif\n"
    "    { int row=shade_row(level,tinted);\n"
    "    row=dithered_row(row,level,tinted);\n"
    "    if (u_debug_vis==1) { emit(uint(row)|(tinted ? 64u : 0u)); return; }\n"
    "    mapped=tinted ?"
    " texelFetch(u_tint,ivec2(int(texel),row),0).r :"
    " texelFetch(u_colormap,ivec2(int(texel),row),0).r; } }\n"
    "  if (u_debug_vis==1) { emit(255u); return; }\n"  /* direct/glow marker */
    /* Whether a blend table below decided this fragment's index. Where one did,
     * the index is the whole answer and the colour is read from it, exactly as
     * it is today: a blend is an index-space operation and the tables are
     * authored in index space. */
    "  bool blended=false;\n"
    /* A palette-colour surface on the solid-colour blend writer: EVERY pixel it
     * covers blends, with no high-bit gate and no index-0 skip, and the blend
     * table is indexed by the source colour first and the destination second --
     * the transpose of the textured rule below.  The table is uploaded
     * row-major, so texel (x,y) is table[y*256+x] and table[(src<<8)|dst] is
     * (x,y) = (dst,src).  The two branches are exclusive: a solid colour with
     * its high bit set must not also take the textured path and blend twice. */
    "  if (u_blend_pass!=0 && (v_light_material.w & 16u)!=0u) {\n"
    "    uint dst=texelFetch(u_dst,ivec2(gl_FragCoord.xy),0).r;\n"
    "    mapped=texelFetch(u_blend,ivec2(int(dst),int(mapped)),0).r;\n"
    "    blended=true;\n"
    "  } else if (u_blend_pass!=0 && (material & 16u)!=0u &&"
    " (texel & 128u)!=0u) {\n"
    "    uint dst=texelFetch(u_dst,ivec2(gl_FragCoord.xy),0).r;\n"
    "    mapped=texelFetch(u_blend,ivec2(int(mapped),int(dst)),0).r;\n"
    "    blended=true;\n"
    "  }\n"
    /* The stale wall's high-bit texels blend against the same previous-frame
     * pixel its holes show, per the engine's masked writer. */
    "  if ((v_light_material.w & 4u)!=0u && (texel & 128u)!=0u) {\n"
    "    uint stale=texelFetch(u_stale,ivec2(gl_FragCoord.xy),0).r;\n"
    "    mapped=texelFetch(u_blend,ivec2(int(mapped),int(stale)),0).r;\n"
    "    blended=true;\n"
    "  }\n"
    "#ifdef HWR_TRUECOLOR\n"
    "  g_albedo_index=texel;\n"
    /* A blend is an index-space operation and it stays one: the tables are
     * authored in index space, the destination they read is a plane of indices,
     * and the index this fragment writes is the one they produced. What the
     * blended colour then owes to the surface in front is the light that
     * reaches THAT surface -- so the blended colour is taken back into the
     * working space and multiplied by it.
     *
     * The error in that is bounded and named: the surface behind may have had a
     * light of its own, and where the two share a sector there is none at all.
     * The exact form is a colour twin of the per-run destination snapshot, which
     * doubles the most expensive copies in the frame for it.
     *
     * A fragment no law lit -- every fragment of the original and the smooth
     * laws, and every class the modern law refuses -- keeps what it has always
     * had: the colour its blended index resolves to. */
    "  if (blended) {\n"
    "    if (g_light_total_set) {\n"
    "      g_shade_color=lit_surface(light_space("
    "texelFetch(u_palette,ivec2(int(mapped),0),0).rgb),g_light_total);\n"
    "      g_shade_linear=u_light_linear!=0;\n"
    "    } else {\n"
    "      g_shade_color_set=false;\n"
    "    }\n"
    "  }\n"
    "#endif\n"
    "  emit(mapped);\n"
    "}\n";

static const char g_resolve_vertex_shader[] =
    "#version 330 core\n"
    "void main() {\n"
    "  vec2 p=vec2((gl_VertexID << 1) & 2,gl_VertexID & 2);\n"
    "  gl_Position=vec4(p*2.0-1.0,0.0,1.0);\n"
    "}\n";

static const char g_sky_fragment_shader[] =
    "#version 330 core\n"
    "layout(location=0) out uint out_index;\n"
    "uniform usamplerBuffer u_texels;\n"
    "uniform ivec2 u_source_viewport;\n"
    "uniform ivec2 u_source_focal;\n"
    "uniform ivec2 u_source_center;\n"
    "uniform vec2 u_focal;\n"
    "uniform vec2 u_center;\n"
    "uniform int u_texture_offset;\n"
    "uniform int u_source_offset;\n"
    "uniform int u_projection_offset;\n"
    "uniform int u_texture_height;\n"
    "uniform int u_start_row;\n"
    "uniform int u_yaw;\n"
    "uniform int u_double_scanline;\n"
    "uniform int u_debug_vis;\n"
    "void main() {\n"
    "  int tx=int(gl_FragCoord.x), ty=int(gl_FragCoord.y);\n"
    "  float source_x=float(u_source_center.x)+"
    " (float(tx)-u_center.x)*float(u_source_focal.x)/u_focal.x;\n"
    "  float source_y=float(u_source_center.y)+"
    " (float(ty)-u_center.y)*float(u_source_focal.y)/u_focal.y;\n"
    "  int sx=int(floor(source_x));"
    " int sy=clamp(int(floor(source_y)),0,u_source_viewport.y-1);\n"
    "  int column;\n"
    "  if (sx>=0 && sx<u_source_viewport.x)"
    " column=int(texelFetch(u_texels,u_projection_offset+sx).r);\n"
    "  else {\n"
    "    float angle=atan((float(u_source_center.x)-source_x)/"
    " float(u_source_focal.x));\n"
    "    int edge_x=sx<0 ? 0 : u_source_viewport.x-1;\n"
    "    int edge_column=int(texelFetch(u_texels,"
    " u_projection_offset+edge_x).r);\n"
    "    float edge_angle=atan((float(u_source_center.x)-float(edge_x))/"
    " float(u_source_focal.x));\n"
    "    column=(edge_column+int(round((angle-edge_angle)*"
    " 162.97466172610084))) & 255;\n"
    "  }\n"
    "  column=(column-(u_yaw << 1)) & 255;\n"
    "  int row=u_start_row+(u_double_scanline!=0 ? (sy >> 1) : sy);\n"
    "  row=clamp(row,0,u_texture_height-1);\n"
    "  uint texel=texelFetch(u_texels,u_texture_offset+u_source_offset+row*256+column).r;\n"
    "  out_index=u_debug_vis==1 ? 255u : texel;\n"
    "}\n";

static const char g_resolve_fragment_shader[] =
    "#version 330 core\n"
    "layout(location=0) out vec4 out_color;\n"
    "uniform usampler2D u_index; uniform sampler2D u_palette;\n"
    "uniform int u_debug_vis;\n"
    "void main() { uint i=texelFetch(u_index,ivec2(gl_FragCoord.xy),0).r;\n"
    "  if (u_debug_vis==1) {\n"          /* shade-row map: gray=plain, red=tint */
    "    if (i==255u) { out_color=vec4(0.0,0.0,1.0,1.0); return; }\n"
    "    float v=1.0-float(i & 31u)/31.0*0.9;\n"
    "    out_color=(i & 64u)!=0u ? vec4(v,0.0,0.0,1.0) : vec4(v,v,v,1.0);\n"
    "    return;\n"
    "  }\n"
    "  out_color=texelFetch(u_palette,ivec2(int(i),0),0); }\n";

/* The true-colour resolve. The geometry pass has already written the colour the
 * palette index resolves to, so with no effect in force this is a copy. The
 * engine's two palette-level effects reach the world image here instead of
 * through the palette the world was composed in.
 *
 * Both are authored on the six-bit value the DAC is loaded with, and both are
 * reproduced on that value here, so a colour that IS a palette colour comes out
 * as exactly the byte the authentic resolve produces -- which is what the
 * acceptance instrument measures. A colour that is not one is quantized to six
 * bits by the same arithmetic, which is what the hardware did to it anyway
 * while the effect was running.
 *
 * Debug visualisations are index planes by definition and keep the authentic
 * resolve above. */
static const char g_truecolor_resolve_fragment_shader[] =
    "#version 330 core\n"
    "layout(location=0) out vec4 out_color;\n"
    "uniform sampler2D u_source;\n"
    /* The untinted palette: the desaturation ramp is entries 0x40..0x4f of it. */
    "uniform sampler2D u_palette;\n"
    "uniform int u_damage_fade;\n"
    "uniform int u_grayscale;\n"
    /* Whether this resolve dithers. Off is the picture as it was composed, to
     * the byte, and is what every neutral frame gets whatever the setting says. */
    "uniform int u_dither;\n"
    /* The eight-bit value the colour stands for. The clamp is for a colour a
     * later term has taken outside the displayable range in a floating-point
     * target; it changes nothing for one that is inside it. */
    "int byte_of(float v) { return clamp(int(v*255.0+0.5),0,255); }\n"
    /* An ordered screen-space pattern in [0,1) -- the interleaved gradient, three
     * instructions and no visible tiling. Added ahead of a quantizer, a ramp too
     * shallow to change a level per pixel breaks into a fine pattern instead of
     * into flat plateaus with a step between them, and it costs nothing where the
     * ramp is steep enough to change levels on its own.
     *
     * It is taken to each quantizer's own convention, so that the mean lands
     * exactly where the composition put it either way: the eight-bit write below
     * rounds to nearest and wants the pattern centred on zero, and the six-bit
     * collapse truncates and wants it unsigned.
     *
     * And it is spent inside a margin rather than across the whole step. A colour
     * that IS one the map authored has to come out as the byte that colour
     * resolves to, and a floating-point world image holds such a colour about an
     * eighth of a level away from it -- so a pattern reaching a full half step
     * could round an exact colour onto its neighbour, which is the one thing this
     * is not allowed to do. The margin below is that distance in canvas levels,
     * and each quantizer keeps a step less twice the margin measured in ITS OWN
     * steps: the eight-bit write gives up a third of its span for it, and the
     * six-bit collapse, whose step is four levels wide, gives up almost nothing.
     * Either way the span stays centred, so the mean does not move. */
    "float dither_unit() {\n"
    "  return fract(52.9829189*fract(dot(gl_FragCoord.xy,"
    "vec2(0.06711056,0.00583715))));\n"
    "}\n"
    "const float DITHER_MARGIN=0.15;\n"
    /* The engine takes its damage-flash product down by a signed shift of eight,
     * which rounds toward negative infinity; integer division rounds toward
     * zero, so the negative case is spelled out. */
    "int shift8(int v) { return v>=0 ? v/256 : -((-v+255)/256); }\n"
    "void main() {\n"
    "  vec4 texel=texelFetch(u_source,ivec2(gl_FragCoord.xy),0);\n"
    "  float pattern=u_dither!=0 ? dither_unit() : 0.0;\n"
    /* The plain resolve, which is a copy: the only quantizer left is the write
     * into the eight-bit canvas the frame is read back through, and that is
     * where a floating-point world image spends the precision it was composed
     * at. */
    "  if (u_damage_fade<0 && u_grayscale==0) {\n"
    "    float fine=u_dither!=0 ?"
    " (pattern-0.5)*(1.0-2.0*DITHER_MARGIN)/255.0 : 0.0;\n"
    "    out_color=vec4(texel.rgb+fine,texel.a);\n"
    "    return;\n"
    "  }\n"
    /* An effect is running, and the value it is authored on is six bits: that
     * collapse is now the coarsest quantizer in the frame and is where the
     * pattern has to be spent, not after it. */
    "  float coarse=u_dither!=0 ?"
    " ((pattern-0.5)*(1.0-0.5*DITHER_MARGIN)+0.5)*4.0/255.0 : 0.0;\n"
    "  int r6=byte_of(texel.r+coarse)>>2;\n"
    "  int g6=byte_of(texel.g+coarse)>>2;\n"
    "  int b6=byte_of(texel.b+coarse)>>2;\n"
    /* The inventory desaturation: the engine's green-weighted luminance of the
     * six-bit entry, read off the same sixteen-entry ramp it remaps into. */
    "  if (u_grayscale!=0) {\n"
    "    int lum=((r6*4+b6+g6*8)*0x500)>>16;\n"
    "    vec4 grey=texelFetch(u_palette,ivec2(0x4f-lum,0),0);\n"
    "    r6=byte_of(grey.r)>>2; g6=byte_of(grey.g)>>2; b6=byte_of(grey.b)>>2;\n"
    "  }\n"
    /* The damage flash, per channel, collapsing toward dark red. */
    "  if (u_damage_fade>=0) {\n"
    "    r6=shift8((r6-0x2d)*u_damage_fade)+0x2d;\n"
    "    g6=(g6*u_damage_fade)/256;\n"
    "    b6=(b6*u_damage_fade)/256;\n"
    "  }\n"
    "  out_color=vec4(float((r6<<2)&255)/255.0,float((g6<<2)&255)/255.0,\n"
    "                 float((b6<<2)&255)/255.0,texel.a);\n"
    "}\n";

/* Writes a palette-index plane back into the index target. Only the acceptance
 * instrument uses it: it restores the frame's starting index target between the
 * authentic and the true-colour arm so both arms start from identical state. */
static const char g_index_restore_fragment_shader[] =
    "#version 330 core\n"
    "layout(location=0) out uint out_index;\n"
    "uniform usampler2D u_source;\n"
    "void main()"
    " { out_index=texelFetch(u_source,ivec2(gl_FragCoord.xy),0).r; }\n";

/* Fetches known texels from the texture store and the colormap through the
 * same texture units the world pass uses; pixel 0 receives the store byte at
 * the configured offset, pixel 1 the colormap byte, pixel 2 the first store
 * byte, pixel 3 the last store byte. */
static const char g_probe_fragment_shader[] =
    "#version 330 core\n"
    "layout(location=0) out uint out_index;\n"
    "uniform usamplerBuffer u_texels;\n"
    "uniform usampler2D u_colormap;\n"
    "uniform int u_offset;\n"
    "uniform int u_last;\n"
    "void main() {\n"
    "  int x=int(gl_FragCoord.x);\n"
    "  if (x==0) out_index=texelFetch(u_texels,u_offset).r;\n"
    "  else if (x==1) out_index=texelFetch(u_colormap,ivec2(128,16),0).r;\n"
    "  else if (x==2) out_index=texelFetch(u_texels,0).r;\n"
    "  else out_index=texelFetch(u_texels,u_last).r;\n"
    "}\n";

/* The sun caster program: an orthographic render of the caster mesh, from
 * the light. No colour, no shading, no screen. In its depth pass the one
 * thing the fragment stage does is the masked-texel discard, which is what
 * makes a grate's holes holes in the shadow map itself and an object card's
 * outline the outline of its shadow; in its gobo pass it writes what each
 * see-through texel lets past -- the eye-ray transmittance recipe, verbatim,
 * re-aimed along the sun. */
/* The caster program's two stages, version-less bodies compiled twice: the
 * FILL program plain, and the LINE program under SUN_LINE_DEPTH, whose
 * fragments replace the line's own interpolated depth with the caster
 * PLANE's depth at the pixel centre (hwr_sun_math.h states why: a line
 * sample lands up to half a texel off the centre, and on a steep caster
 * that misplacement wrote edge texels shallower than the plane the carried
 * lookup references expect). */
static const char g_sun_vertex_shader[] =
    "layout(location=0) in vec3 a_pos;\n"
    "layout(location=1) in vec2 a_uv;\n"
    "layout(location=2) in uint a_offset;\n"
    "layout(location=3) in uvec2 a_dims;\n"
    "layout(location=4) in uint a_flags;\n"
    "layout(location=5) in uvec2 a_normal;\n"
    "uniform mat4 u_matrix;\n"
    "out vec2 v_uv;\n"
    "flat out uint v_offset;\n"
    "flat out uvec2 v_dims;\n"
    "flat out uint v_flags;\n"
    "flat out uvec2 v_normal;\n"
    "out vec3 v_wpos;\n"
    "void main() {\n"
    "  gl_Position=u_matrix*vec4(a_pos,1.0);\n"
    "  v_uv=a_uv; v_offset=a_offset; v_dims=a_dims; v_flags=a_flags;\n"
    "  v_normal=a_normal; v_wpos=a_pos;\n"
    "}\n";

static const char g_sun_fragment_shader[] =
    "uniform usamplerBuffer u_texels;\n"
    /* Which of the two passes this draw is, and the weight a blend-range
     * texel writes -- the same least-squares fit of the map's own blend
     * table the eye-ray plane multiplies by. */
    "uniform int u_gobo_pass;\n"
    "uniform float u_gobo_blend;\n"
    "in vec2 v_uv;\n"
    "flat in uint v_offset;\n"
    "flat in uvec2 v_dims;\n"
    "flat in uint v_flags;\n"
    "flat in uvec2 v_normal;\n"
    "in vec3 v_wpos;\n"
    "layout(location=0) out vec4 out_gobo;\n"
    "#ifdef SUN_LINE_DEPTH\n"
    "uniform mat4 u_matrix;\n"
    "uniform float u_res;\n"
    /* The same octahedral unpack the world stages run. */
    "vec3 sun_normal_decode(uvec2 pn) {\n"
    "  vec2 p=(vec2(pn)-128.0)/127.0;\n"
    "  vec3 n=vec3(p,1.0-abs(p.x)-abs(p.y));\n"
    "  if (n.z<0.0)\n"
    "    n=vec3((1.0-abs(p.y))*(p.x>=0.0 ? 1.0 : -1.0),\n"
    "           (1.0-abs(p.x))*(p.y>=0.0 ? 1.0 : -1.0), n.z);\n"
    "  return normalize(n);\n"
    "}\n"
    /* The caster plane's depth at THIS pixel's centre -- held NO SHALLOWER
     * than the line's own sample -- in window units, plus the write-side
     * units offset the fill pass takes from polygon offset (gl_FragDepth
     * bypasses that path). The max is load-bearing on both sides: the
     * plane-at-centre term is what keeps an interior edge from writing a
     * misplaced shallow sample onto its own plane's texel, and the
     * line's-own-sample term is what keeps the plane from being written
     * where it EXTRAPOLATES past the caster's physical edge -- a wall's
     * top-edge line otherwise stamps phantom wall above the joint, and the
     * own-texel carry then agrees with its own phantom: that agreement was
     * the last thin line on the director's pillar. Falls back to the
     * line's own depth where no plane is carried or it degenerates. */
    "float sun_line_depth() {\n"
    "  if (v_normal.x==0u && v_normal.y==0u) return gl_FragCoord.z;\n"
    "  vec3 n=sun_normal_decode(v_normal);\n"
    "  vec3 ga=vec3(u_matrix[0].x,u_matrix[1].x,u_matrix[2].x);\n"
    "  vec3 gb=vec3(u_matrix[0].y,u_matrix[1].y,u_matrix[2].y);\n"
    "  vec3 gc=vec3(u_matrix[0].z,u_matrix[1].z,u_matrix[2].z);\n"
    "  vec3 t1=cross(n,gb);\n"
    "  vec3 t2=cross(n,ga);\n"
    "  float au=dot(ga,t1);\n"
    "  float bv=dot(gb,t2);\n"
    "  if (abs(au)<=1e-12 || abs(bv)<=1e-12) return gl_FragCoord.z;\n"
    "  vec3 p=(u_matrix*vec4(v_wpos,1.0)).xyz;\n"
    "  vec2 c=gl_FragCoord.xy*(2.0/u_res)-1.0;\n"
    "  float zc=p.z+(dot(gc,t1)/au)*(c.x-p.x)+(dot(gc,t2)/bv)*(c.y-p.y);\n"
    "  return clamp(max(zc*0.5+0.5,gl_FragCoord.z)+4.0/16777216.0,\n"
    "               0.0,1.0);\n"
    "}\n"
    "#endif\n"
    /* The per-texel law of the translucent range, both passes reading the
     * SAME classes the eye-ray transmittance pass reads: an index-0 texel of
     * a masked surface is a hole and is discarded everywhere -- full light;
     * a blend-range texel (high bit set, the texel the map's own blend
     * tables pass) filters -- no depth, the gobo multiplies the fitted
     * weight; every other texel is opaque paint -- it writes DEPTH and
     * blocks the beam whole, exactly as a masked caster's paint does, and
     * the gobo pass leaves it alone so the paint's own sun-side face keeps
     * its light (blocking-behind is the depth map's job; a zero written
     * here would blacken the very face the paint stands on). */
    "void main() {\n"
    "#ifdef SUN_LINE_DEPTH\n"
    "  gl_FragDepth=sun_line_depth();\n"
    "#endif\n"
    "  int w=int(v_dims.x), h=int(v_dims.y);\n"
    "  uint texel=255u;\n"
    "  if ((v_flags & 7u)!=0u && w>0 && h>0) {\n"
    "    int iy=int(floor(v_uv.x)) % h; if (iy<0) iy+=h;\n"
    "    int ix=int(floor(v_uv.y)) % w; if (ix<0) ix+=w;\n"
    "    texel=texelFetch(u_texels,int(v_offset)+iy*w+ix).r;\n"
    "  }\n"
    "  if ((v_flags & 1u)!=0u && texel==0u) discard;\n"
    "  if (u_gobo_pass!=0) {\n"
    /* The gobo pass draws the translucent range alone: blend-range texels
     * multiply the weight in, opaque paint is discarded -- its block is
     * already whole in the depth map. */
    "    if ((texel & 128u)==0u) discard;\n"
    "    out_gobo=vec4(u_gobo_blend,u_gobo_blend,u_gobo_blend,1.0);\n"
    "    return;\n"
    "  }\n"
    "  out_gobo=vec4(0.0);\n"
    /* The depth pass: a translucent surface owns depth only at its opaque
     * paint -- its blend-range texels pass light and are discarded here. */
    "  if ((v_flags & 4u)!=0u && (texel & 128u)!=0u) discard;\n"
    "}\n";

static GLuint hwr_compile_shader_sources(GLenum type,
                                         const char *const *sources,
                                         GLsizei count)
{
    GLuint shader = g_gl.create_shader(type);
    GLint status = 0;
    if (shader == 0)
        return 0;
    g_gl.shader_source(shader, count, sources, NULL);
    g_gl.compile_shader(shader);
    g_gl.get_shader_iv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        GLchar log[512];
        GLsizei length = 0;
        g_gl.get_shader_info_log(shader, sizeof log, &length, log);
        snprintf(g_error, sizeof g_error, "GLSL compile failed: %.*s", (int)length, log);
        g_gl.delete_shader(shader);
        return 0;
    }
    return shader;
}

static GLuint hwr_link_program_sources(const char *const *vertex_sources,
                                       GLsizei vertex_count,
                                       const char *const *fragment_sources,
                                       GLsizei fragment_count)
{
    GLuint vertex = hwr_compile_shader_sources(GL_VERTEX_SHADER,
                                               vertex_sources, vertex_count);
    GLuint fragment;
    GLuint program;
    GLint status = 0;
    if (vertex == 0)
        return 0;
    fragment = hwr_compile_shader_sources(GL_FRAGMENT_SHADER, fragment_sources,
                                          fragment_count);
    if (fragment == 0) {
        g_gl.delete_shader(vertex);
        return 0;
    }
    program = g_gl.create_program();
    g_gl.attach_shader(program, vertex);
    g_gl.attach_shader(program, fragment);
    g_gl.link_program(program);
    g_gl.delete_shader(vertex);
    g_gl.delete_shader(fragment);
    g_gl.get_program_iv(program, GL_LINK_STATUS, &status);
    if (!status) {
        GLchar log[512];
        GLsizei length = 0;
        g_gl.get_program_info_log(program, sizeof log, &length, log);
        snprintf(g_error, sizeof g_error, "GLSL link failed: %.*s", (int)length, log);
        g_gl.delete_program(program);
        return 0;
    }
    return program;
}

static GLuint hwr_link_program(const char *vertex_source,
                               const char *fragment_source)
{
    return hwr_link_program_sources(&vertex_source, 1, &fragment_source, 1);
}

/* The world pass's program, built from the shared bodies with the given define
 * chunk. An empty chunk yields the authentic program, whose two stages then
 * carry nothing the enhanced one adds. The vertex stage is chunked for the same
 * reason the fragment stage already was: it has declarations only one of the two
 * programs may see, and a define chunk is how one body serves both. */
static GLuint hwr_link_geometry_program(const char *define_chunk)
{
    const char *vertex_sources[3];
    const char *sources[3];
    vertex_sources[0] = g_glsl_version;
    vertex_sources[1] = define_chunk;
    vertex_sources[2] = g_geometry_vertex_body;
    sources[0] = g_glsl_version;
    sources[1] = define_chunk;
    sources[2] = g_geometry_fragment_body;
    return hwr_link_program_sources(vertex_sources, 3, sources, 3);
}

static void hwr_delete_texture(GLuint *texture)
{
    if (*texture != 0) {
        g_gl.delete_textures(1, texture);
        *texture = 0;
    }
}

/* The shader programs are built from compile-time-constant source, so they are
 * linked once and kept alive for the context's lifetime rather than relinked on
 * every scene upload (each relink is a compile + link of two shaders). They are
 * deliberately NOT torn down with the per-scene buffers below; only the GL
 * shutdown path releases them. The true-colour set is absent unless the
 * foundation has been asked for, and releasing a zero program is a no-op. */
static void hwr_destroy_programs(void)
{
    if (g_geometry_program != 0)
        g_gl.delete_program(g_geometry_program);
    if (g_sky_program != 0)
        g_gl.delete_program(g_sky_program);
    if (g_resolve_program != 0)
        g_gl.delete_program(g_resolve_program);
    if (g_probe_program != 0)
        g_gl.delete_program(g_probe_program);
    if (g_truecolor_program != 0)
        g_gl.delete_program(g_truecolor_program);
    if (g_truecolor_resolve_program != 0)
        g_gl.delete_program(g_truecolor_resolve_program);
    if (g_index_restore_program != 0)
        g_gl.delete_program(g_index_restore_program);
    if (g_sun_program != 0)
        g_gl.delete_program(g_sun_program);
    if (g_sun_line_program != 0)
        g_gl.delete_program(g_sun_line_program);
    g_geometry_program = 0;
    g_sky_program = 0;
    g_resolve_program = 0;
    g_probe_program = 0;
    g_truecolor_program = 0;
    g_truecolor_resolve_program = 0;
    g_index_restore_program = 0;
    g_sun_program = 0;
    g_sun_line_program = 0;
    g_sun_failed = 0;
}

/* Releases every GL object the sun machinery holds. The published state and
 * any pending static slice survive: they describe the world, not the context,
 * and the next frame that can lays everything down again. */
static void hwr_sun_destroy_objects(void)
{
    if (g_sun_static_vao != 0)
        g_gl.delete_vertex_arrays(1, &g_sun_static_vao);
    if (g_sun_dynamic_vao != 0)
        g_gl.delete_vertex_arrays(1, &g_sun_dynamic_vao);
    if (g_sun_static_vbo != 0)
        g_gl.delete_buffers(1, &g_sun_static_vbo);
    if (g_sun_dynamic_vbo != 0)
        g_gl.delete_buffers(1, &g_sun_dynamic_vbo);
    if (g_sun_fbo != 0)
        g_gl.delete_framebuffers(1, &g_sun_fbo);
    hwr_delete_texture(&g_sun_depth_texture);
    hwr_delete_texture(&g_sun_gobo_texture);
    g_sun_static_vao = 0;
    g_sun_dynamic_vao = 0;
    g_sun_static_vbo = 0;
    g_sun_dynamic_vbo = 0;
    g_sun_fbo = 0;
    g_sun_depth_resolution = 0;
    g_sun_static_count = 0;
    g_sun_static_gobo_first = 0;
    g_sun_map_valid = 0;
    g_sun_gobo_valid = 0;
}

/* The caster vertex format, bound once per array object against the record
 * hwr_gl.h declares. Both slices are the one format; each array points into
 * its own buffer. */
static void hwr_sun_bind_attributes(void)
{
    const GLsizei stride = (GLsizei)sizeof(struct hwr_gl_sun_vertex);

    g_gl.enable_vertex_attrib_array(0);
    g_gl.vertex_attrib_pointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                               (const void *)0);
    g_gl.enable_vertex_attrib_array(1);
    g_gl.vertex_attrib_pointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                               (const void *)offsetof(struct hwr_gl_sun_vertex,
                                                      u));
    g_gl.enable_vertex_attrib_array(2);
    g_gl.vertex_attrib_i_pointer(2, 1, GL_UNSIGNED_INT, stride,
                                 (const void *)offsetof(
                                     struct hwr_gl_sun_vertex, offset));
    g_gl.enable_vertex_attrib_array(3);
    g_gl.vertex_attrib_i_pointer(3, 2, GL_UNSIGNED_SHORT, stride,
                                 (const void *)offsetof(
                                     struct hwr_gl_sun_vertex, width));
    g_gl.enable_vertex_attrib_array(4);
    g_gl.vertex_attrib_i_pointer(4, 1, GL_UNSIGNED_BYTE, stride,
                                 (const void *)offsetof(
                                     struct hwr_gl_sun_vertex, flags));
    g_gl.enable_vertex_attrib_array(5);
    g_gl.vertex_attrib_i_pointer(5, 2, GL_UNSIGNED_BYTE, stride,
                                 (const void *)offsetof(
                                     struct hwr_gl_sun_vertex, normal_x));
}

/* Brings the sun pass's own objects up: the program, the two array objects,
 * and the depth map at the published resolution. A failure marks the pass
 * failed and stands it down for the context's lifetime rather than retrying
 * a broken link sixty times a second. Returns 1 when everything stands. */
static int hwr_sun_ensure_objects(void)
{
    int resolution = g_sun.resolution;

    if (g_sun_failed)
        return 0;
    if (resolution < 64 || resolution > 8192)
        return 0;
    if (g_sun_program == 0 || g_sun_line_program == 0) {
        static const char version[] = "#version 330 core\n";
        static const char line_define[] = "#define SUN_LINE_DEPTH 1\n";
        const char *vs[3];
        const char *fs[3];

        vs[0] = version;
        vs[1] = "";
        vs[2] = g_sun_vertex_shader;
        fs[0] = version;
        fs[1] = "";
        fs[2] = g_sun_fragment_shader;
        if (g_sun_program == 0)
            g_sun_program = hwr_link_program_sources(vs, 3, fs, 3);
        vs[1] = line_define;
        fs[1] = line_define;
        if (g_sun_line_program == 0)
            g_sun_line_program = hwr_link_program_sources(vs, 3, fs, 3);
        if (g_sun_program == 0 || g_sun_line_program == 0) {
            fprintf(stderr, "[renderer_overhaul] sun caster program: %s\n",
                    g_error);
            g_sun_failed = 1;
            return 0;
        }
    }
    if (g_sun_static_vao == 0) {
        g_gl.gen_vertex_arrays(1, &g_sun_static_vao);
        g_gl.gen_buffers(1, &g_sun_static_vbo);
        g_gl.bind_vertex_array(g_sun_static_vao);
        g_gl.bind_buffer(GL_ARRAY_BUFFER, g_sun_static_vbo);
        hwr_sun_bind_attributes();
    }
    if (g_sun_dynamic_vao == 0) {
        g_gl.gen_vertex_arrays(1, &g_sun_dynamic_vao);
        g_gl.gen_buffers(1, &g_sun_dynamic_vbo);
        g_gl.bind_vertex_array(g_sun_dynamic_vao);
        g_gl.bind_buffer(GL_ARRAY_BUFFER, g_sun_dynamic_vbo);
        hwr_sun_bind_attributes();
    }
    if (g_sun_fbo == 0)
        g_gl.gen_framebuffers(1, &g_sun_fbo);
    if (g_sun_depth_texture == 0 || g_sun_depth_resolution != resolution) {
        static const GLfloat lit_border[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        static const GLenum no_buffers[1] = { GL_NONE };
        GLenum status;

        hwr_delete_texture(&g_sun_depth_texture);
        /* A resolution change re-takes the gobo with it: the plane below is
         * rebuilt at the new size by its own block. */
        hwr_delete_texture(&g_sun_gobo_texture);
        g_gl.gen_textures(1, &g_sun_depth_texture);
        g_gl.active_texture(GL_TEXTURE0 + 11);
        g_gl.bind_texture(GL_TEXTURE_2D, g_sun_depth_texture);
        /* 24-bit depth: the map's quantum is part of the lookup law's floor
         * (hwr_sunm_bias_floor), and at 16 bits that quantum -- an eighth of
         * a world unit at the default window, five of them under the caster
         * pass's units offset -- was a whole visible strip of light at every
         * concave joint on its own. At 24 bits the quantum term is dust. */
        g_gl.tex_image_2d(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                          (GLsizei)resolution, (GLsizei)resolution, 0,
                          GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
        /* Beyond the window's edge the lookup clamps to LIT: the border holds
         * the far plane, so the compare passes wherever the window ends. The
         * shader carries the same rule explicitly; the border is what keeps
         * the free 2x2 PCF from bleeding the edge texel outward. */
        g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                             GL_CLAMP_TO_BORDER);
        g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                             GL_CLAMP_TO_BORDER);
        g_gl.tex_parameter_fv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR,
                              lit_border);
        g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
                             GL_COMPARE_REF_TO_TEXTURE);
        g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC,
                             GL_LEQUAL);
        g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        g_gl.bind_framebuffer(GL_FRAMEBUFFER, g_sun_fbo);
        g_gl.framebuffer_texture_2d(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                    GL_TEXTURE_2D, g_sun_depth_texture, 0);
        g_gl.draw_buffers(1, no_buffers);
        g_gl.read_buffer(GL_NONE);
        status = g_gl.check_framebuffer_status(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            fprintf(stderr, "[renderer_overhaul] sun depth framebuffer "
                            "incomplete (0x%x)\n", (unsigned)status);
            hwr_delete_texture(&g_sun_depth_texture);
            g_sun_depth_resolution = 0;
            g_sun_failed = 1;
            g_gl.active_texture(GL_TEXTURE0);
            return 0;
        }
        g_sun_depth_resolution = resolution;
        g_gl.active_texture(GL_TEXTURE0);
    }
    /* The gobo plane, alive exactly as long as the sun machinery is: R8 at
     * the depth map's own resolution (a quarter of its bytes), colour
     * attachment 0 of the same framebuffer -- each pass says with its draw
     * buffers whether it writes it. Its lifecycle follows the sun being
     * live, and hwr_sun_destroy_objects takes it down with everything
     * else. */
    if (g_sun_gobo_texture == 0) {
        g_gl.gen_textures(1, &g_sun_gobo_texture);
        g_gl.active_texture(GL_TEXTURE0 + 12);
        g_gl.bind_texture(GL_TEXTURE_2D, g_sun_gobo_texture);
        g_gl.tex_image_2d(GL_TEXTURE_2D, 0, GL_R8, (GLsizei)resolution,
                          (GLsizei)resolution, 0, GL_RED,
                          GL_UNSIGNED_BYTE, NULL);
        g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                             GL_CLAMP_TO_EDGE);
        g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                             GL_CLAMP_TO_EDGE);
        g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                             GL_LINEAR);
        g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                             GL_LINEAR);
        g_gl.bind_framebuffer(GL_FRAMEBUFFER, g_sun_fbo);
        g_gl.framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                    GL_TEXTURE_2D, g_sun_gobo_texture, 0);
        g_gl.active_texture(GL_TEXTURE0);
    }
    /* The compare stays NEAREST at every softness: the soft edge is blended
     * by hand in the fragment stage, each texel compared against the
     * reference carried onto it -- the hardware filter blends compares of
     * ONE reference, which at a shading joint is a texel of bias slack, and
     * slack there is a strip of light. */
    g_gl.active_texture(GL_TEXTURE0 + 11);
    g_gl.bind_texture(GL_TEXTURE_2D, g_sun_depth_texture);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    g_gl.active_texture(GL_TEXTURE0);
    return 1;
}

void hwr_gl_set_sun(const struct hwr_gl_sun *sun)
{
    if (sun == NULL || !sun->active) {
        memset(&g_sun, 0, sizeof g_sun);
        return;
    }
    g_sun = *sun;
}

int hwr_gl_sun_upload_static(const struct hwr_gl_sun_vertex *vertices,
                             size_t count, size_t gobo_first)
{
    /* Held first, so a slice built before the context exists is laid down by
     * the first sun render rather than lost. The holding copy is the caller's
     * bytes; an empty slice clears it. */
    if (count != 0 && vertices == NULL)
        return 0;
    if (count > (size_t)INT32_MAX / sizeof(struct hwr_gl_sun_vertex))
        return 0;
    if (gobo_first > count)
        return 0;
    free(g_sun_static_pending);
    g_sun_static_pending = NULL;
    g_sun_static_pending_count = 0;
    if (count != 0) {
        g_sun_static_pending = (struct hwr_gl_sun_vertex *)malloc(
            count * sizeof *vertices);
        if (g_sun_static_pending == NULL)
            return 0;
        memcpy(g_sun_static_pending, vertices, count * sizeof *vertices);
        g_sun_static_pending_count = count;
    }
    g_sun_static_pending_gobo_first = count != 0 ? gobo_first : 0;
    g_sun_static_pending_valid = 1;
    return 1;
}

void hwr_gl_sun_set_dynamic(const struct hwr_gl_sun_vertex *vertices,
                            size_t count)
{
    g_sun_dynamic = vertices;
    g_sun_dynamic_count = vertices != NULL ? count : 0;
}

int hwr_gl_sun_read_depth(float *out, int resolution)
{
    if (!g_initialized || out == NULL || !g_sun_map_valid ||
        g_sun_depth_texture == 0 || resolution != g_sun_depth_resolution ||
        !hwr_platform_make_current())
        return 0;
    g_gl.active_texture(GL_TEXTURE0 + 11);
    g_gl.bind_texture(GL_TEXTURE_2D, g_sun_depth_texture);
    g_gl.pixel_store_i(GL_PACK_ALIGNMENT, 1);
    g_gl.get_tex_image(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT, out);
    g_gl.active_texture(GL_TEXTURE0);
    return hwr_gl_drain_errors() == 0;
}

int hwr_gl_sun_read_gobo(float *out, int resolution)
{
    if (!g_initialized || out == NULL || !g_sun_gobo_valid ||
        g_sun_gobo_texture == 0 || resolution != g_sun_depth_resolution ||
        !hwr_platform_make_current())
        return 0;
    g_gl.active_texture(GL_TEXTURE0 + 12);
    g_gl.bind_texture(GL_TEXTURE_2D, g_sun_gobo_texture);
    g_gl.pixel_store_i(GL_PACK_ALIGNMENT, 1);
    g_gl.get_tex_image(GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT, out);
    g_gl.active_texture(GL_TEXTURE0);
    return hwr_gl_drain_errors() == 0;
}

int hwr_gl_sun_map_valid(void)
{
    return g_sun_map_valid;
}

static void hwr_set_sampler(GLuint program, const char *name, GLint unit);
static void hwr_uniform_1i(GLuint program, const char *name, GLint value);
static void hwr_uniform_1f(GLuint program, const char *name, GLfloat value);

/* Whether the frame about to be rendered wants the sun machinery at all. The
 * key term lives inside the modern law alone, so any other law -- and any
 * frame with the light at zero -- stands the whole pass down. */
static int hwr_sun_wanted(void)
{
    return g_truecolor_active &&
           g_truecolor_want.shading_mode == HWR_GL_SHADING_MODERN &&
           g_sun.active && g_sun.intensity > 0.0f;
}

/* Lays the light-space depth map down for this frame: the static slice as it
 * stands, the dynamic slice as it was handed over. Leaves no state behind
 * that the world pass does not set for itself. */
static void hwr_sun_render_map(void)
{
    /* The dynamic slice is a one-frame borrow; it is consumed here whatever
     * happens next, so a frame the pass stands down for cannot leave a
     * pointer standing into an array the owner has moved on from. */
    const struct hwr_gl_sun_vertex *dynamic = g_sun_dynamic;
    size_t dynamic_count = g_sun_dynamic_count;

    g_sun_dynamic = NULL;
    g_sun_dynamic_count = 0;
    g_sun_map_valid = 0;
    g_sun_gobo_valid = 0;
    if (!hwr_sun_wanted())
        return;
    if (!hwr_sun_ensure_objects())
        return;
    if (g_sun_static_pending_valid) {
        g_gl.bind_buffer(GL_ARRAY_BUFFER, g_sun_static_vbo);
        g_gl.buffer_data(GL_ARRAY_BUFFER,
                         (GLsizeiptr)(g_sun_static_pending_count *
                                      sizeof *g_sun_static_pending),
                         g_sun_static_pending, GL_STATIC_DRAW);
        g_sun_static_count = g_sun_static_pending_count;
        g_sun_static_gobo_first = g_sun_static_pending_gobo_first;
        free(g_sun_static_pending);
        g_sun_static_pending = NULL;
        g_sun_static_pending_count = 0;
        g_sun_static_pending_valid = 0;
    }
    if (g_sun_static_count == 0 && dynamic_count == 0)
        return;
    if (dynamic_count != 0) {
        g_gl.bind_buffer(GL_ARRAY_BUFFER, g_sun_dynamic_vbo);
        g_gl.buffer_data(GL_ARRAY_BUFFER,
                         (GLsizeiptr)(dynamic_count * sizeof *dynamic),
                         dynamic, GL_STREAM_DRAW);
    }
    g_gl.bind_framebuffer(GL_FRAMEBUFFER, g_sun_fbo);
    g_gl.viewport(0, 0, (GLsizei)g_sun_depth_resolution,
                  (GLsizei)g_sun_depth_resolution);
    g_gl.disable(GL_SCISSOR_TEST);
    g_gl.disable(GL_BLEND);
    g_gl.enable(GL_DEPTH_TEST);
    g_gl.depth_func(GL_LESS);
    g_gl.depth_mask(GL_TRUE);
    {
        static const GLenum no_buffers[1] = { GL_NONE };

        g_gl.draw_buffers(1, no_buffers);
    }
    g_gl.clear(GL_DEPTH_BUFFER_BIT);
    g_gl.use_program(g_sun_program);
    hwr_set_sampler(g_sun_program, "u_texels", 0);
    {
        GLint location = g_gl.get_uniform_location(g_sun_program, "u_matrix");
        if (location >= 0)
            g_gl.uniform_matrix_4fv(location, 1, GL_FALSE, g_sun.clip);
    }
    hwr_uniform_1i(g_sun_program, "u_gobo_pass", 0);
    g_gl.active_texture(GL_TEXTURE0);
    g_gl.bind_texture(GL_TEXTURE_2D, 0);
    g_gl.bind_texture(GL_TEXTURE_BUFFER, g_texture_buffer_texture);
    /* A units-only offset against the depth map's own quantization, and the
     * depth clamp that pancakes the roof above the window onto the near
     * plane so it always counts. Both are this pass's own and are taken
     * back off. The slope-scaled factor the first milestone ran here is
     * deliberately gone: it pushed a grazed caster's stored depth a full
     * texel's slope deeper, and that slack -- summed with the flat lookup
     * bias -- was the strip of light leaking in where a wall meets the
     * ceiling that shades it. Acne cover now lives at the lookup, where the
     * receiver-slope law spends exactly the fragment's own footprint. */
    g_gl.enable(GL_POLYGON_OFFSET_FILL);
    g_gl.polygon_offset(0.0f, 4.0f);
    g_gl.enable(GL_DEPTH_CLAMP);
    /* The depth pass draws the WHOLE static slice -- the translucent range
     * included, whose fragments keep depth only at their fully-opaque paint
     * (the shader's per-texel law): a window's frame blocks the beam exactly
     * as a wall does, while its glass and its holes leave the map alone. */
    if (g_sun_static_count != 0) {
        g_gl.bind_vertex_array(g_sun_static_vao);
        g_gl.draw_arrays(GL_TRIANGLES, 0, (GLsizei)g_sun_static_count);
    }
    if (dynamic_count != 0) {
        g_gl.bind_vertex_array(g_sun_dynamic_vao);
        g_gl.draw_arrays(GL_TRIANGLES, 0, (GLsizei)dynamic_count);
    }
    /* CONSERVATIVE EDGE COVERAGE: the same slice once more with the
     * triangles drawn as lines. A polygon's rasterized footprint holds only
     * the texels whose CENTRES it covers, so at every caster's edge the
     * final sub-texel band of light columns crosses the caster in geometry
     * yet finds no sample of it in the map -- and where that edge meets the
     * surface it shades (a ceiling's rim on the wall below it), that band
     * came back as a texel-tall line of sky along the joint: the pillar-top
     * line on the director's save, at every quality tier. The pass runs its
     * own program: each line fragment writes the caster PLANE's depth at
     * its pixel's centre -- the value the fill rasterizer writes at covered
     * texels -- because the line's own samples land up to half a texel off
     * the centre, and on a steep caster that misplacement wrote edge texels
     * shallower than the plane the carried lookup references expect: a
     * texel-wide false shadow along every mesh edge. The cutout law is
     * untouched -- both programs discard the same masked texels. */
    if (g_gl.polygon_mode != NULL && g_sun_line_program != 0) {
        g_gl.use_program(g_sun_line_program);
        hwr_set_sampler(g_sun_line_program, "u_texels", 0);
        {
            GLint location =
                g_gl.get_uniform_location(g_sun_line_program, "u_matrix");

            if (location >= 0)
                g_gl.uniform_matrix_4fv(location, 1, GL_FALSE, g_sun.clip);
        }
        hwr_uniform_1i(g_sun_line_program, "u_gobo_pass", 0);
        hwr_uniform_1f(g_sun_line_program, "u_res",
                       (GLfloat)g_sun_depth_resolution);
        g_gl.polygon_mode(GL_FRONT_AND_BACK, GL_LINE);
        if (g_sun_static_count != 0) {
            g_gl.bind_vertex_array(g_sun_static_vao);
            g_gl.draw_arrays(GL_TRIANGLES, 0, (GLsizei)g_sun_static_count);
        }
        if (dynamic_count != 0) {
            g_gl.bind_vertex_array(g_sun_dynamic_vao);
            g_gl.draw_arrays(GL_TRIANGLES, 0, (GLsizei)dynamic_count);
        }
        g_gl.polygon_mode(GL_FRONT_AND_BACK, GL_FILL);
        g_gl.use_program(g_sun_program);
    }
    /* The gobo pass: clear the plane to "everything passes", then multiply
     * in the TRANSLUCENT range with the depth test held LESS against the
     * map just laid down -- only what stands sun-side of the opaque winner
     * filters the beam, and only at its blend-range texels: opaque paint is
     * discarded here (its block is already whole in the depth map, and a
     * zero written at it would blacken its own sun-side face), holes are
     * discarded everywhere. Multiplicative blending is the eye-ray pass's
     * own law re-aimed along the sun. The masked depth casters and the
     * object cards never draw here: their occlusion is already whole in
     * the depth map, and this plane has no depth of its own, so their
     * writes could only darken their OWN lit faces (a card dimming the
     * very sprite it stands for -- the self-shadow the bias law exists to
     * prevent). What the eye-ray pass solves with its receive flag, this
     * pass solves by the range and the texels it draws. */
    if (g_sun_gobo_texture != 0) {
        static const GLenum gobo_only[1] = { GL_COLOR_ATTACHMENT0 };
        static const GLfloat clear_gobo[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

        g_gl.draw_buffers(1, gobo_only);
        g_gl.clear_buffer_fv(GL_COLOR, 0, clear_gobo);
        g_gl.depth_mask(GL_FALSE);
        g_gl.enable(GL_BLEND);
        g_gl.blend_func(GL_ZERO, GL_SRC_COLOR);
        g_gl.polygon_offset(0.0f, 0.0f);
        g_gl.disable(GL_POLYGON_OFFSET_FILL);
        hwr_uniform_1i(g_sun_program, "u_gobo_pass", 1);
        hwr_uniform_1f(g_sun_program, "u_gobo_blend", g_transmit_blend);
        if (g_sun_static_count > g_sun_static_gobo_first) {
            g_gl.bind_vertex_array(g_sun_static_vao);
            g_gl.draw_arrays(GL_TRIANGLES,
                             (GLint)g_sun_static_gobo_first,
                             (GLsizei)(g_sun_static_count -
                                       g_sun_static_gobo_first));
        }
        hwr_uniform_1i(g_sun_program, "u_gobo_pass", 0);
        g_gl.disable(GL_BLEND);
        g_gl.depth_mask(GL_TRUE);
        g_gl.enable(GL_POLYGON_OFFSET_FILL);
        g_sun_gobo_valid = 1;
    }
    g_gl.disable(GL_DEPTH_CLAMP);
    g_gl.polygon_offset(0.0f, 0.0f);
    g_gl.disable(GL_POLYGON_OFFSET_FILL);
    g_gl.bind_vertex_array(0);
    g_sun_map_valid = 1;
}

/* Links the four always-present programs on first use (or after a shutdown); a
 * no-op once they are live. Returns 1 when all four are ready, 0 (with the set
 * cleared) on a link failure so the caller falls back to the solid renderer. */
static int hwr_gl_ensure_programs(void)
{
    if (g_geometry_program != 0 && g_sky_program != 0 &&
        g_resolve_program != 0 && g_probe_program != 0)
        return 1;
    hwr_destroy_programs();
    g_geometry_program = hwr_link_geometry_program("");
    g_sky_program = hwr_link_program(g_resolve_vertex_shader,
                                     g_sky_fragment_shader);
    g_resolve_program = hwr_link_program(g_resolve_vertex_shader,
                                         g_resolve_fragment_shader);
    g_probe_program = hwr_link_program(g_resolve_vertex_shader,
                                       g_probe_fragment_shader);
    if (g_geometry_program == 0 || g_sky_program == 0 ||
        g_resolve_program == 0 || g_probe_program == 0) {
        hwr_destroy_programs();
        return 0;
    }
    return 1;
}

static void hwr_destroy_scene_objects(void)
{
    if (g_scene_vao != 0)
        g_gl.delete_vertex_arrays(1, &g_scene_vao);
    if (g_resolve_vao != 0)
        g_gl.delete_vertex_arrays(1, &g_resolve_vao);
    if (g_scene_vbo != 0)
        g_gl.delete_buffers(1, &g_scene_vbo);
    if (g_texture_buffer != 0)
        g_gl.delete_buffers(1, &g_texture_buffer);
    g_scene_vao = 0;
    g_resolve_vao = 0;
    g_scene_vbo = 0;
    g_texture_buffer = 0;
    hwr_delete_texture(&g_texture_buffer_texture);
    hwr_delete_texture(&g_colormap_texture);
    hwr_delete_texture(&g_tint_texture);
    hwr_delete_texture(&g_glow_texture);
    hwr_delete_texture(&g_blend_texture);
    hwr_delete_texture(&g_palette_texture);
    /* The untinted palette goes with it and comes back with the foundation. */
    hwr_delete_texture(&g_base_palette_texture);
    g_scene_uploaded = 0;
    g_store_reserve = 0;
    g_verify_store_size = 0;
    g_verify_store_hash_valid = 0;
}

static void hwr_destroy_framebuffer(void)
{
    hwr_delete_texture(&g_index_texture);
    hwr_delete_texture(&g_floor_id_texture);
    hwr_delete_texture(&g_index_copy_texture);
    hwr_delete_texture(&g_stale_copy_texture);
    hwr_delete_texture(&g_color_texture);
    hwr_delete_texture(&g_stale_color_texture);
    hwr_delete_texture(&g_transmit_texture);
    g_stale_color_valid = 0;
    if (g_depth_buffer != 0) {
        g_gl.delete_renderbuffers(1, &g_depth_buffer);
        g_depth_buffer = 0;
    }
    if (g_index_fbo != 0) {
        g_gl.delete_framebuffers(1, &g_index_fbo);
        g_index_fbo = 0;
    }
    if (g_color_buffer != 0) {
        g_gl.delete_renderbuffers(1, &g_color_buffer);
        g_color_buffer = 0;
    }
    if (g_fbo != 0) {
        g_gl.delete_framebuffers(1, &g_fbo);
        g_fbo = 0;
    }
    g_fbo_width = 0;
    g_fbo_height = 0;
}

static int hwr_create_framebuffer(uint32_t width, uint32_t height)
{
    size_t bytes;
    uint32_t *new_pixels;

    if (width == 0 || height == 0 || width > 16384u || height > 16384u ||
        (uint64_t)width * (uint64_t)height > (uint64_t)SIZE_MAX / sizeof(uint32_t)) {
        snprintf(g_error, sizeof g_error, "invalid OpenGL render size %ux%u", width, height);
        return 0;
    }
    hwr_destroy_framebuffer();
    g_gl.gen_framebuffers(1, &g_fbo);
    g_gl.bind_framebuffer(GL_FRAMEBUFFER, g_fbo);
    g_gl.gen_renderbuffers(1, &g_color_buffer);
    g_gl.bind_renderbuffer(GL_RENDERBUFFER, g_color_buffer);
    g_gl.renderbuffer_storage(GL_RENDERBUFFER, GL_RGBA8, (GLsizei)width, (GLsizei)height);
    g_gl.framebuffer_renderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_RENDERBUFFER, g_color_buffer);
    if (g_gl.check_framebuffer_status(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        snprintf(g_error, sizeof g_error, "the OpenGL framebuffer is incomplete");
        hwr_destroy_framebuffer();
        return 0;
    }

    g_index_target_dirty = 1;
    g_gl.gen_framebuffers(1, &g_index_fbo);
    g_gl.bind_framebuffer(GL_FRAMEBUFFER, g_index_fbo);
    g_gl.gen_textures(1, &g_index_texture);
    g_gl.bind_texture(GL_TEXTURE_2D, g_index_texture);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g_gl.tex_image_2d(GL_TEXTURE_2D, 0, GL_R8UI, (GLsizei)width,
                      (GLsizei)height, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, NULL);
    g_gl.framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D, g_index_texture, 0);
    /* The owner-floor identity attachment. R32UI holds FLOOR_TAG | (floor_z &
     * 0xffff) for a visible floor fragment, zero everywhere else. Sampled (never
     * written) by the floor-exception passes, so there is no feedback while it
     * is a live draw buffer of the world pass. */
    g_gl.gen_textures(1, &g_floor_id_texture);
    g_gl.bind_texture(GL_TEXTURE_2D, g_floor_id_texture);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g_gl.tex_image_2d(GL_TEXTURE_2D, 0, GL_R32UI, (GLsizei)width,
                      (GLsizei)height, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, NULL);
    g_gl.framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
                                GL_TEXTURE_2D, g_floor_id_texture, 0);
    g_gl.gen_renderbuffers(1, &g_depth_buffer);
    g_gl.bind_renderbuffer(GL_RENDERBUFFER, g_depth_buffer);
    g_gl.renderbuffer_storage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                              (GLsizei)width, (GLsizei)height);
    g_gl.framebuffer_renderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, g_depth_buffer);
    if (g_gl.check_framebuffer_status(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        snprintf(g_error, sizeof g_error, "the palette-index framebuffer is incomplete");
        hwr_destroy_framebuffer();
        return 0;
    }
    g_gl.gen_textures(1, &g_index_copy_texture);
    g_gl.bind_texture(GL_TEXTURE_2D, g_index_copy_texture);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g_gl.tex_image_2d(GL_TEXTURE_2D, 0, GL_R8UI, (GLsizei)width,
                      (GLsizei)height, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, NULL);
    g_gl.gen_textures(1, &g_stale_copy_texture);
    g_gl.bind_texture(GL_TEXTURE_2D, g_stale_copy_texture);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g_gl.tex_image_2d(GL_TEXTURE_2D, 0, GL_R8UI, (GLsizei)width,
                      (GLsizei)height, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, NULL);

    bytes = (size_t)width * (size_t)height * sizeof(uint32_t);
    if (bytes > g_pixels_bytes) {
        new_pixels = (uint32_t *)realloc(g_pixels, bytes);
        if (new_pixels == NULL) {
            snprintf(g_error, sizeof g_error, "allocating the OpenGL readback buffer failed");
            hwr_destroy_framebuffer();
            return 0;
        }
        g_pixels = new_pixels;
        g_pixels_bytes = bytes;
    }
    g_fbo_width = width;
    g_fbo_height = height;
    return 1;
}

int hwr_gl_init(void)
{
    const GLubyte *version;
    const GLubyte *renderer;

    if (g_initialized)
        return 1;
    if (!hwr_platform_init()) {
        hwr_platform_shutdown();
        return 0;
    }
    if (!hwr_load_gl_functions()) {
        hwr_platform_shutdown();
        memset(&g_gl, 0, sizeof g_gl);
        return 0;
    }
    version = g_gl.get_string(GL_VERSION);
    renderer = g_gl.get_string(GL_RENDERER);
    if (version == NULL || renderer == NULL) {
        snprintf(g_error, sizeof g_error, "OpenGL did not report a version and renderer");
        hwr_platform_shutdown();
        memset(&g_gl, 0, sizeof g_gl);
        return 0;
    }
    g_initialized = 1;
    snprintf(g_description, sizeof g_description, "%s / %s",
             (const char *)version, (const char *)renderer);
    fprintf(stderr, "[renderer_overhaul] GL %s / %s\n",
            (const char *)version, (const char *)renderer);
    return 1;
}

const char *hwr_gl_description(void)
{
    return g_description;
}

static void hwr_texture_2d_u8(GLuint *texture, GLsizei width, GLsizei height,
                              GLenum internal_format, GLenum format,
                              const void *pixels)
{
    g_gl.gen_textures(1, texture);
    g_gl.bind_texture(GL_TEXTURE_2D, *texture);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g_gl.tex_image_2d(GL_TEXTURE_2D, 0, (GLint)internal_format, width, height,
                      0, format, GL_UNSIGNED_BYTE, pixels);
}

static void hwr_set_sampler(GLuint program, const char *name, GLint unit)
{
    GLint location = g_gl.get_uniform_location(program, name);
    if (location >= 0)
        g_gl.uniform_1i(location, unit);
}

/* HWR_TRUECOLOR=1 forces the true-colour foundation on. It is the only thing
 * that asks for it today: there is no consumer setting, and with the switch
 * absent no enhanced program is compiled, no colour attachment exists, and the
 * world pass runs the instruction stream it has always run. */
static int hwr_truecolor_env(void)
{
    if (g_truecolor_env < 0) {
        const char *setting = getenv("HWR_TRUECOLOR");
        g_truecolor_env =
            setting != NULL && setting[0] != '0' && setting[0] != 0 ? 1 : 0;
    }
    return g_truecolor_env;
}

/* HWR_SHADE_RAMP=0 resolves the smooth law at the row it floors to instead of
 * between that row and the next. It is the bisect handle for the ramp
 * interpolation alone -- the law, the clamps and every other difference the mode
 * makes stay exactly where they are -- so what the smooth arm is at a whole row
 * can be held against the authentic arm and measured. */
static int hwr_shade_ramp_env(void)
{
    if (g_shade_ramp_env < 0) {
        const char *setting = getenv("HWR_SHADE_RAMP");
        g_shade_ramp_env = setting != NULL && setting[0] == '0' ? 0 : 1;
    }
    return g_shade_ramp_env;
}

/* And the three parts of the still, round, per-fragment candle. Each switch
 * decides one part of the pool on its own -- the light position, the metric,
 * where the law is sampled -- and leaves the other two where the running law
 * puts them, so what each one is worth can be measured by itself. Like the ramp
 * switch above they are bisect handles rather than settings: a consumer chooses
 * the law, not the parts of it, and a handle nobody set answers -1 so the law
 * keeps its own. */
static int hwr_shade_part_env(const char *name, int *cache)
{
    if (*cache == HWR_SHADE_PART_UNREAD) {
        const char *setting = getenv(name);

        *cache = setting == NULL || setting[0] == 0 ? -1
               : (setting[0] == '0' ? 0 : 1);
    }
    return *cache;
}

static int hwr_shade_origin_fixed_env(void)
{
    return hwr_shade_part_env("HWR_SHADE_ORIGIN_FIXED",
                              &g_shade_origin_fixed_env);
}

static int hwr_shade_radial_round_env(void)
{
    return hwr_shade_part_env("HWR_SHADE_RADIAL_ROUND",
                              &g_shade_radial_round_env);
}

static int hwr_shade_per_fragment_env(void)
{
    return hwr_shade_part_env("HWR_SHADE_PER_FRAGMENT",
                              &g_shade_per_fragment_env);
}

/* And which law draws which pool, where no handle has spoken.
 *
 * The smooth law draws the original's candle whole: the light position the
 * engine walks every seventh tick, the larger of the two axis terms, and the
 * shade sampled at the corners the engine samples it at. Its one departure from
 * the original picture is the quantizer, and the pool is not the quantizer --
 * the jitter, the corners of the pool and its two lobes are the original's own
 * character, and a law offered as "the same picture without the bands" has no
 * business taking them.
 *
 * The modern law replaces the candle outright with a lamp at the eye, and the
 * row it still records is the one every index-space mechanism in the frame
 * reads; measuring that row from where the lamp is, round, and at the fragment
 * is what keeps the index under a still light consistent with the colour beside
 * it. So it asks for all three.
 *
 * The original law reads none of this: it resolves one authored row per
 * fragment through its own arithmetic and never calls the continuous twin. */
static int hwr_shade_part_wanted(int asked, enum hwr_gl_shading_mode mode)
{
    if (asked >= 0)
        return asked;
    return mode == HWR_GL_SHADING_MODERN;
}

/* HWR_LAMP_TRANSMIT=0 lets the light the player carries through every
 * see-through surface whole, which is where it was before the pass that stops
 * it existed. The same kind of handle as the three above: what the eye can see
 * through is part of the law the mode selects, not a choice of its own. */
static int hwr_transmit_env(void)
{
    if (g_transmit_env < 0) {
        const char *setting = getenv("HWR_LAMP_TRANSMIT");
        g_transmit_env = setting != NULL && setting[0] == '0' ? 0 : 1;
    }
    return g_transmit_env;
}

/* Unlike the three above this one is off unless it is asked for: the law that
 * needs a linear working space is not the one shipping here, and the smooth law
 * composes where the palette lives on purpose. */
static int hwr_shade_linear_env(void)
{
    if (g_shade_linear_env < 0) {
        const char *setting = getenv("HWR_SHADE_LINEAR");
        g_shade_linear_env =
            setting != NULL && setting[0] != 0 && setting[0] != '0';
    }
    return g_shade_linear_env;
}

static int hwr_shade_linear_denied_env(void)
{
    if (g_shade_linear_denied_env < 0) {
        const char *setting = getenv("HWR_SHADE_LINEAR");
        g_shade_linear_denied_env = setting != NULL && setting[0] == '0';
    }
    return g_shade_linear_denied_env;
}

/* The space each law is composed in. The modern law ADDS light, and light adds
 * in linear, so it is composed there unless the handle refuses outright -- which
 * is the bisect control for the round trip on its own, the same rig composed
 * where the palette lives. The smooth law READS between two authored entries,
 * which is a display-space operation, so it stays where the palette lives unless
 * the handle asks. The original law never enters either: it resolves one
 * authored colour per fragment and there is nothing to add it to. */
static int hwr_shade_linear_wanted(enum hwr_gl_shading_mode mode)
{
    if (mode == HWR_GL_SHADING_AUTHENTIC)
        return 0;
    if (mode == HWR_GL_SHADING_MODERN)
        return !hwr_shade_linear_denied_env();
    return hwr_shade_linear_env();
}

/* ---------------------------------------------------------- the carried light
 *
 * Two things move the light the player carries that no row can say on its own:
 * the map's own word about how strong its lantern was meant to be, and the
 * flame in it. Both are worked out here, once per rendered frame -- the word
 * and the flame's brightness as plain multipliers on the strength and the
 * reach the settings layer handed over, and the flame's WANDER as the one
 * screen offset the cone's origin is measured from, which is what the
 * original's own flicker is: the pool dances about the view centre, it does
 * not breathe in place.
 *
 * Doing it here rather than in the fragment tail is the whole design. A flame
 * is one number and one offset a frame, not a term per pixel; a mirror is lit
 * by the same flame as the view it hangs in without a word being said about
 * reflections; and with nothing asking, the multipliers are exactly one and
 * the offset exactly nought, so the light the rows describe reaches the
 * shader as the bits the rows describe. */

/* The step the flame is advanced in. Fixing it is what makes the flame a
 * function of how much of the world's time has passed and of nothing else: the
 * frame rate decides how often it is looked at, never how fast it runs, and the
 * rate the flame is retargeted at cannot saturate on a slow frame. */
#define HWR_LAMP_STEP 0.0166666667f
/* The most of a stopped world's time a resumed one may spend at once. A menu
 * held open for a minute owes a minute; spending it would be a minute of flame
 * in one frame, so the debt is forgiven past a quarter of a second. */
#define HWR_LAMP_CATCHUP 0.25f
/* The flame: retargets a second, how fast the light reaches for the target it
 * was given, the two waves under it, and the bounds it is held between. The
 * floor is the whole difference between a candle and a fault -- it is what
 * stops the light ever guttering out.
 *
 * The two dials own the whole of it. Speed scales every RATE -- the retarget
 * lottery, the waves' pace and the reach toward each target -- and depth
 * scales every AMPLITUDE, the waves' beside the targets'. It was not always
 * so: the lottery first ran at a fixed fourteen a second with the speed dial
 * on the waves alone, which put the flame's real motion where no dial could
 * reach it and its energy in the band an eye reads as a strobe. Three a
 * second is where a flame lives; the wave sizes are stated at a depth of ONE
 * and the shipped depth of 0.3 lands them where the old fixed sizes sat, so
 * the default flame moves as it always did, minus the strobe. */
#define HWR_LAMP_RETARGET_RATE 3.0f
#define HWR_LAMP_SMOOTHING 6.0f
#define HWR_LAMP_WAVE_A_RATE 2.1f
#define HWR_LAMP_WAVE_A_SIZE 0.20f
#define HWR_LAMP_WAVE_B_RATE 3.7f
#define HWR_LAMP_WAVE_B_SIZE 0.12f
#define HWR_LAMP_WAVE_B_PHASE 1.3f
#define HWR_LAMP_FLOOR 0.3f
#define HWR_LAMP_CEILING 1.4f
#define HWR_LAMP_TWO_PI 6.2831853072f
/* Where the flame's own sequence starts. It is a fixed number rather than
 * anything the clock or the machine supplies: a flame nobody can predict and a
 * flame that starts in the same place every run look exactly alike, and only
 * one of them can be measured. */
#define HWR_LAMP_SEED 0x9e3779b9u
/* The pool's wander, which is what the flame's flicker IS in the original:
 * every seventh tick the engine steps a sixteen-bit sequence and moves the
 * candle's screen origin a random step about the view centre, a sixteenth of
 * the focal to either side per axis (renderer.c:13411-13426) -- the pool
 * DANCES, it does not breathe. The modern law's cone measures from the fixed
 * centre, so the wander is that dance given back to it: the same paced
 * lottery and the same chase the flame runs, driving a two-axis offset in
 * units of the engine's own amplitude. The depth dial is how far the pool
 * wanders -- one is the engine's own sixteenth -- and the speed dial is the
 * pace, so the two say of the pool exactly what they say of the flame. The
 * chase is what makes it a wander rather than the original's teleport, which
 * is the one deliberate departure. The waves are the flame's own idea sized
 * for a position: two incommensurate rates, low enough that the fastest
 * shipped pace keeps every wave under the band an eye reads as strobe. Its
 * sequence is its own, seeded apart from the flame's, so the flame the gates
 * banked walks the walk it always walked. */
#define HWR_LAMP_WANDER_A_RATE 1.7f
#define HWR_LAMP_WANDER_A_SIZE 0.25f
#define HWR_LAMP_WANDER_B_RATE 2.9f
#define HWR_LAMP_WANDER_B_SIZE 0.18f
#define HWR_LAMP_WANDER_B_PHASE 2.1f
#define HWR_LAMP_WANDER_SEED 0x7f4a7c15u

/* What the map's word does to the strength and to the reach. */
static float g_lamp_glow_intensity = 1.0f;
static float g_lamp_glow_range = 1.0f;
/* The flame: what is in force, what it is reaching for, the two waves' own
 * phases, the time it has been given but not yet spent, and its sequence. */
static float g_lamp_flicker = 1.0f;
static float g_lamp_flicker_target = 1.0f;
static float g_lamp_wave_a;
static float g_lamp_wave_b;
static float g_lamp_carry;
/* How many steps the flame has been given, which is the only clock it has: a
 * flame that is not moving is one this is not counting, whatever real time is
 * doing beside it. */
static uint32_t g_lamp_steps;
static uint32_t g_lamp_random_state = HWR_LAMP_SEED;
/* The pool's wander: where the cone's origin stands, where it is reaching
 * for, the two waves' own phases, and its own sequence. All of it is nought
 * at rest, which is the identity every still picture stands on. */
static float g_lamp_wander_x;
static float g_lamp_wander_y;
static float g_lamp_wander_target_x;
static float g_lamp_wander_target_y;
static float g_lamp_wander_wave_a;
static float g_lamp_wander_wave_b;
static uint32_t g_lamp_wander_random_state = HWR_LAMP_WANDER_SEED;
/* The two clocks the flame is driven by: real time, which is what it runs on,
 * and the world's own, which is what says whether time is passing at all. */
static uint64_t g_lamp_last_ns;
static uint64_t g_lamp_owed_ns;
static uint16_t g_lamp_last_world_clock;
static int g_lamp_clocks_valid;

static float hwr_lamp_clamp(float value, float low, float high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

/* The flame's own sequence, in nought to one. */
static float hwr_lamp_random(void)
{
    g_lamp_random_state = g_lamp_random_state * 1664525u + 1013904223u;
    return (float)((g_lamp_random_state >> 8) & 0xffffffu) / 16777216.0f;
}

/* And the wander's, apart from it on purpose: the wander draws a different
 * number of times a step than the flame does, so sharing one sequence would
 * move every banked flame the moment the wander stood beside it. */
static float hwr_lamp_wander_random(void)
{
    g_lamp_wander_random_state =
        g_lamp_wander_random_state * 1664525u + 1013904223u;
    return (float)((g_lamp_wander_random_state >> 8) & 0xffffffu) /
           16777216.0f;
}

/* The map's word about its own lantern.
 *
 * The word is map metadata the engine loads into a global and then never reads,
 * so it is free for a mod to take as the authoring hint it plainly is. It is a
 * value rather than a flag: twenty-eight of the game's forty-four maps hold
 * eight and the rest hold twelve, fifteen, sixteen or thirty-two. Strength
 * follows it and reach follows its square root, so a map asking for twice the
 * light gets twice the light over about half again the distance rather than a
 * pool of twice the width -- which is how a brighter flame actually behaves.
 *
 * The dial in front of it is how much of that is wanted: at nothing the word is
 * ignored and the rows are the whole answer, at one it is read whole. The
 * bounds are the ones the reading was worked out with, and the brightest maps in
 * the game land exactly on both of them. */
void hwr_gl_glow_factors(uint16_t candle_glow, float weight,
                         float *intensity, float *range)
{
    float factor;

    if (intensity != NULL)
        *intensity = 1.0f;
    if (range != NULL)
        *range = 1.0f;
    if (candle_glow == 0u || weight == 0.0f)
        return;
    factor = (float)candle_glow / 6.0f;
    if (factor < 0.25f)
        factor = 0.25f;
    if (intensity != NULL)
        *intensity = hwr_lamp_clamp(1.0f + (factor - 1.0f) * weight, 0.25f,
                                    4.0f);
    if (range != NULL)
        *range = hwr_lamp_clamp(1.0f + (sqrtf(factor) - 1.0f) * weight,
                                0.3125f, 2.0f);
}

static void hwr_lamp_glow(uint16_t candle_glow, float weight)
{
    hwr_gl_glow_factors(candle_glow, weight, &g_lamp_glow_intensity,
                        &g_lamp_glow_range);
}

/* One step of the flame. It is reached for rather than jumped to, which is what
 * keeps a retarget from reading as a switch being thrown, and it is held between
 * its bounds BEFORE it is reached for so the light in force can never leave
 * them. The two waves carry their own phases rather than a shared clock, so
 * neither loses precision however long a session runs.
 *
 * Speed multiplies the lottery, the reach and the waves' pace alike, so one
 * dial slows or hurries the whole flame; depth multiplies the waves beside
 * the targets, so a depth of nothing is a flame that is perfectly still. */
static void hwr_lamp_flicker_step(float amount, float speed)
{
    float wave;
    float desired;

    g_lamp_steps++;
    g_lamp_wave_a += HWR_LAMP_STEP * speed * HWR_LAMP_WAVE_A_RATE;
    g_lamp_wave_b += HWR_LAMP_STEP * speed * HWR_LAMP_WAVE_B_RATE;
    while (g_lamp_wave_a >= HWR_LAMP_TWO_PI)
        g_lamp_wave_a -= HWR_LAMP_TWO_PI;
    while (g_lamp_wave_b >= HWR_LAMP_TWO_PI)
        g_lamp_wave_b -= HWR_LAMP_TWO_PI;
    if (hwr_lamp_random() < HWR_LAMP_STEP * HWR_LAMP_RETARGET_RATE * speed)
        g_lamp_flicker_target =
            1.0f + (hwr_lamp_random() * 2.0f - 1.0f) * amount;
    wave = (sinf(g_lamp_wave_a) * HWR_LAMP_WAVE_A_SIZE +
            sinf(g_lamp_wave_b + HWR_LAMP_WAVE_B_PHASE) *
                HWR_LAMP_WAVE_B_SIZE) * amount;
    desired = hwr_lamp_clamp(g_lamp_flicker_target + wave, HWR_LAMP_FLOOR,
                             HWR_LAMP_CEILING);
    g_lamp_flicker += (desired - g_lamp_flicker) *
                      (1.0f - expf(-HWR_LAMP_STEP * HWR_LAMP_SMOOTHING *
                                   speed));
}

/* One step of the pool's wander, beside the flame's and on the same clock.
 * The lottery picks where the pool reaches for next -- a point in the
 * engine's own box, depth one being the whole of it -- and the chase walks
 * there, so the pool wanders where the original's teleports. The desired
 * point is held inside the engine's box BEFORE it is reached for, exactly as
 * the flame is held between its bounds, so the origin can never leave the
 * amplitude the original owns. Depth multiplies every amplitude and speed
 * every rate, so a depth of nothing is an origin that is exactly still: the
 * targets and the waves are nought, the chase reaches for nought, and nought
 * it stands at from reset. */
static void hwr_lamp_wander_step(float amount, float speed)
{
    float chase = 1.0f - expf(-HWR_LAMP_STEP * HWR_LAMP_SMOOTHING * speed);
    float desired_x;
    float desired_y;

    g_lamp_wander_wave_a += HWR_LAMP_STEP * speed * HWR_LAMP_WANDER_A_RATE;
    g_lamp_wander_wave_b += HWR_LAMP_STEP * speed * HWR_LAMP_WANDER_B_RATE;
    while (g_lamp_wander_wave_a >= HWR_LAMP_TWO_PI)
        g_lamp_wander_wave_a -= HWR_LAMP_TWO_PI;
    while (g_lamp_wander_wave_b >= HWR_LAMP_TWO_PI)
        g_lamp_wander_wave_b -= HWR_LAMP_TWO_PI;
    if (hwr_lamp_wander_random() <
        HWR_LAMP_STEP * HWR_LAMP_RETARGET_RATE * speed) {
        g_lamp_wander_target_x =
            (hwr_lamp_wander_random() * 2.0f - 1.0f) * amount;
        g_lamp_wander_target_y =
            (hwr_lamp_wander_random() * 2.0f - 1.0f) * amount;
    }
    desired_x = hwr_lamp_clamp(g_lamp_wander_target_x +
                                   sinf(g_lamp_wander_wave_a) *
                                       HWR_LAMP_WANDER_A_SIZE * amount,
                               -1.0f, 1.0f);
    desired_y = hwr_lamp_clamp(g_lamp_wander_target_y +
                                   sinf(g_lamp_wander_wave_b +
                                        HWR_LAMP_WANDER_B_PHASE) *
                                       HWR_LAMP_WANDER_B_SIZE * amount,
                               -1.0f, 1.0f);
    g_lamp_wander_x += (desired_x - g_lamp_wander_x) * chase;
    g_lamp_wander_y += (desired_y - g_lamp_wander_y) * chase;
}

void hwr_gl_lamp_reset(void)
{
    g_lamp_flicker = 1.0f;
    g_lamp_flicker_target = 1.0f;
    g_lamp_wave_a = 0.0f;
    g_lamp_wave_b = 0.0f;
    g_lamp_carry = 0.0f;
    g_lamp_steps = 0;
    g_lamp_random_state = HWR_LAMP_SEED;
    g_lamp_wander_x = 0.0f;
    g_lamp_wander_y = 0.0f;
    g_lamp_wander_target_x = 0.0f;
    g_lamp_wander_target_y = 0.0f;
    g_lamp_wander_wave_a = 0.0f;
    g_lamp_wander_wave_b = 0.0f;
    g_lamp_wander_random_state = HWR_LAMP_WANDER_SEED;
    g_lamp_last_ns = 0;
    g_lamp_owed_ns = 0;
    g_lamp_last_world_clock = 0;
    g_lamp_clocks_valid = 0;
}

void hwr_gl_lamp_advance(uint16_t candle_glow, uint16_t world_clock,
                         uint64_t now_ns)
{
    const struct hwr_gl_light *light = &g_truecolor_want.light;
    float owed;

    hwr_lamp_glow(candle_glow, light->lamp_glow_scale);
    /* No flame is asked for, or the law in force has no light at the eye to put
     * one in. Either way what reaches the shader is the strength the rows hold,
     * multiplied by exactly one. */
    if (g_truecolor_want.shading_mode != HWR_GL_SHADING_MODERN ||
        light->lamp_flicker == 0) {
        hwr_gl_lamp_reset();
        return;
    }
    if (!g_lamp_clocks_valid) {
        g_lamp_clocks_valid = 1;
        g_lamp_last_ns = now_ns;
        g_lamp_last_world_clock = world_clock;
        return;
    }
    /* Real time is what the flame runs on, and every frame's worth of it is
     * counted -- but it is only SPENT on the frames the world's own clock has
     * moved. A frame drawn while the world is held owes its time and pays
     * nothing, which is what holds the flame still behind a menu the same way an
     * animated surface holds its frame. */
    if (now_ns > g_lamp_last_ns)
        g_lamp_owed_ns += now_ns - g_lamp_last_ns;
    g_lamp_last_ns = now_ns;
    if (world_clock == g_lamp_last_world_clock)
        return;
    g_lamp_last_world_clock = world_clock;
    owed = (float)((double)g_lamp_owed_ns / 1000000000.0);
    g_lamp_owed_ns = 0;
    g_lamp_carry += owed < HWR_LAMP_CATCHUP ? owed : HWR_LAMP_CATCHUP;
    while (g_lamp_carry >= HWR_LAMP_STEP) {
        g_lamp_carry -= HWR_LAMP_STEP;
        hwr_lamp_flicker_step(light->lamp_flicker_amount,
                              light->lamp_flicker_speed);
        hwr_lamp_wander_step(light->lamp_flicker_amount,
                             light->lamp_flicker_speed);
    }
}

/* The lights that are not at the eye, as the frame about to be drawn found
 * them. Nothing is kept from one frame to the next: the set is what this frame
 * seeded, so a light whose source has gone is gone with it rather than held by
 * this array. The shader declares the arrays at sixteen and this is what fills
 * them, so the two are held together here rather than by reading. */
_Static_assert(HWR_GL_POINT_LIGHT_MAX == 16u,
               "the world shader declares the light arrays at sixteen");

static struct hwr_gl_point_light g_point_lights[HWR_GL_POINT_LIGHT_MAX];
static size_t g_point_light_count;

void hwr_gl_set_point_lights(const struct hwr_gl_point_light *lights,
                             size_t count)
{
    if (lights == NULL)
        count = 0;
    if (count > HWR_GL_POINT_LIGHT_MAX)
        count = HWR_GL_POINT_LIGHT_MAX;
    if (count != 0)
        memcpy(g_point_lights, lights, count * sizeof *g_point_lights);
    g_point_light_count = count;
}

void hwr_gl_set_truecolor(const struct hwr_gl_truecolor *state)
{
    if (state == NULL)
        return;
    g_truecolor_want = *state;
}

int hwr_gl_truecolor_active(void)
{
    return g_truecolor_active;
}

/* Read from what the settings layer has ASKED for rather than from what the
 * last frame linked, because the emitters run before the frame does. Both
 * conditions are deliberate: a law that reads a normal cannot be in force
 * without the foundation under it, and with nothing asking, the vertices an
 * emitter produces have to be the vertices it produced before this existed. */
int hwr_gl_shading_normals(void)
{
    return g_truecolor_want.required != 0 &&
           g_truecolor_want.shading_mode != HWR_GL_SHADING_AUTHENTIC;
}

int hwr_gl_color_format_available(enum hwr_gl_color_format format)
{
    if (format == HWR_GL_COLOR_RGBA16F)
        return !g_truecolor_refused_rgba16f;
    return 1;
}

/* Whether the frame stops the light the player carries at what it can see
 * through. Every condition is a reason there would be nothing to stop: no
 * composed picture, no plane to write it in, a law that has no such light, the
 * handle held off, or the light itself turned all the way down. */
static int hwr_transmit_wanted(void)
{
    return g_truecolor_active && g_transmit_texture != 0 &&
           g_truecolor_want.shading_mode == HWR_GL_SHADING_MODERN &&
           g_truecolor_want.light.lamp_intensity > 0.0f &&
           g_truecolor_want.light.lamp_range > 0.0f &&
           hwr_transmit_env();
}

/* The weight the map's own blend table gives its destination: how much of what
 * is behind a blended texel survives it, and therefore how much of the light
 * the player carries has to reach that far. Least squares over every pair the
 * table holds, in the palette's own colours -- which is the space the table was
 * authored in and the only space its "half of each" means anything in.
 *
 * The textured writer's rule is the one measured: the table is uploaded row
 * major, so its texel (source, destination) is the index that pair resolves to.
 * The solid-colour writer reads the transpose of it, and on a table that mixes
 * its two arguments evenly the two answers are one answer. */
static void hwr_transmit_measure_blend(void)
{
    double numerator = 0.0;
    double denominator = 0.0;
    unsigned destination;

    if (!g_blend_bytes_valid || !g_base_palette_valid)
        return;
    for (destination = 0; destination < 256u; destination++) {
        unsigned source;

        for (source = 0; source < 256u; source++) {
            unsigned mixed = g_blend_bytes[destination * 256u + source];
            unsigned channel;

            for (channel = 0; channel < 3u; channel++) {
                double behind = (double)g_base_palette_bytes[destination * 4u +
                                                             channel];
                double front = (double)g_base_palette_bytes[source * 4u +
                                                            channel];
                double result = (double)g_base_palette_bytes[mixed * 4u +
                                                             channel];

                numerator += (behind - front) * (result - front);
                denominator += (behind - front) * (behind - front);
            }
        }
    }
    g_transmit_blend_pending = 0;
    if (denominator <= 0.0)
        return;
    numerator /= denominator;
    if (numerator < 0.0)
        numerator = 0.0;
    if (numerator > 1.0)
        numerator = 1.0;
    g_transmit_blend = (float)numerator;
}

enum hwr_gl_color_format hwr_gl_color_format_in_use(void)
{
    return g_color_texture != 0 ? g_truecolor_format : HWR_GL_COLOR_AUTO;
}

/* What the world image is to be kept in, once automatic has been read. Eight
 * bits per channel is exact for the authentic picture and is what it keeps; a
 * law that resolves colour between two authored rows composes levels finer than
 * the canvas it is read back through, and keeping them is what the resolve's
 * dither then has to spend. A driver that has refused the floating-point format
 * once is never asked again. */
static enum hwr_gl_color_format hwr_truecolor_wanted_format(void)
{
    enum hwr_gl_color_format wanted = g_truecolor_want.format;

    if (wanted != HWR_GL_COLOR_AUTO)
        return wanted;
    if (g_truecolor_want.shading_mode != HWR_GL_SHADING_AUTHENTIC &&
        !g_truecolor_refused_rgba16f)
        return HWR_GL_COLOR_RGBA16F;
    return HWR_GL_COLOR_RGBA8;
}

/* Attaches a colour target of the format resolved above and reports whether the
 * framebuffer took it. A driver that will not take the floating-point one is
 * answered with eight bits, which is what the picture asking for automatic would
 * have been given anyway wherever the original law is in force. */
static int hwr_truecolor_attach(uint32_t width, uint32_t height,
                                enum hwr_gl_color_format format)
{
    GLenum internal_format = GL_RGBA8;
    GLenum type = GL_UNSIGNED_BYTE;

    if (format == HWR_GL_COLOR_RGBA16F) {
        internal_format = GL_RGBA16F;
        type = GL_HALF_FLOAT;
    }
    g_gl.active_texture(GL_TEXTURE0);
    g_gl.gen_textures(1, &g_color_texture);
    g_gl.bind_texture(GL_TEXTURE_2D, g_color_texture);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g_gl.tex_image_2d(GL_TEXTURE_2D, 0, (GLint)internal_format, (GLsizei)width,
                      (GLsizei)height, 0, GL_RGBA, type, NULL);
    g_gl.bind_texture(GL_TEXTURE_2D, 0);
    g_gl.framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2,
                                GL_TEXTURE_2D, g_color_texture, 0);
    if (g_gl.check_framebuffer_status(GL_FRAMEBUFFER) ==
        GL_FRAMEBUFFER_COMPLETE && hwr_gl_drain_errors() == 0) {
        /* And its twin, which is the same picture one frame ago. It is a plain
         * texture rather than an attachment -- nothing draws into it -- and it
         * matches the target's own precision, because a snapshot that rounded
         * the colour it copied would make the previous frame a slightly
         * different frame. */
        g_gl.active_texture(GL_TEXTURE0);
        g_gl.gen_textures(1, &g_stale_color_texture);
        g_gl.bind_texture(GL_TEXTURE_2D, g_stale_color_texture);
        g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                             GL_CLAMP_TO_EDGE);
        g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                             GL_CLAMP_TO_EDGE);
        g_gl.tex_image_2d(GL_TEXTURE_2D, 0, (GLint)internal_format,
                          (GLsizei)width, (GLsizei)height, 0, GL_RGBA, type,
                          NULL);
        g_gl.bind_texture(GL_TEXTURE_2D, 0);
        g_stale_color_valid = 0;
        /* And the plane that says what the light the player carries got past on
         * its way to each pixel. One channel and eight bits of it: what it
         * holds is a fraction of a light, and it joins the draw buffers for the
         * one pass that writes it and no other.
         *
         * It is an attachment of the same framebuffer, so a frame that cannot
         * have it keeps everything else: the transmittance is refused and the
         * light goes through what it always went through. */
        g_gl.active_texture(GL_TEXTURE0);
        g_gl.gen_textures(1, &g_transmit_texture);
        g_gl.bind_texture(GL_TEXTURE_2D, g_transmit_texture);
        g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                             GL_CLAMP_TO_EDGE);
        g_gl.tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                             GL_CLAMP_TO_EDGE);
        g_gl.tex_image_2d(GL_TEXTURE_2D, 0, GL_R8, (GLsizei)width,
                          (GLsizei)height, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
        g_gl.bind_texture(GL_TEXTURE_2D, 0);
        g_gl.framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3,
                                    GL_TEXTURE_2D, g_transmit_texture, 0);
        if (g_gl.check_framebuffer_status(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE || hwr_gl_drain_errors() != 0) {
            g_gl.framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3,
                                        GL_TEXTURE_2D, 0, 0);
            hwr_delete_texture(&g_transmit_texture);
            fprintf(stderr, "[renderer_overhaul] this driver will not take the"
                            " one-channel plane the light at the eye is stopped"
                            " in; it passes through everything\n");
        }
        return 1;
    }
    g_gl.framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2,
                                GL_TEXTURE_2D, 0, 0);
    hwr_delete_texture(&g_color_texture);
    return 0;
}

/* Gives back everything the foundation owns, on the first frame nothing is
 * asking for it any more. The authentic path then runs the instruction stream
 * it has always run, with no colour attachment in existence. */
static void hwr_truecolor_release(void)
{
    if (g_color_texture != 0 && g_index_fbo != 0) {
        g_gl.bind_framebuffer(GL_FRAMEBUFFER, g_index_fbo);
        g_gl.framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2,
                                    GL_TEXTURE_2D, 0, 0);
        g_gl.framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3,
                                    GL_TEXTURE_2D, 0, 0);
    }
    hwr_delete_texture(&g_color_texture);
    hwr_delete_texture(&g_stale_color_texture);
    hwr_delete_texture(&g_transmit_texture);
    g_stale_color_valid = 0;
    g_color_prime_pending = 1;
    hwr_delete_texture(&g_base_palette_texture);
    if (g_truecolor_program != 0)
        g_gl.delete_program(g_truecolor_program);
    if (g_truecolor_resolve_program != 0)
        g_gl.delete_program(g_truecolor_resolve_program);
    g_truecolor_program = 0;
    g_truecolor_resolve_program = 0;
    g_truecolor_format = HWR_GL_COLOR_AUTO;
    g_truecolor_logged = 0;
}

/* Links the enhanced program and creates the colour attachment on the first
 * frame the foundation is required, and reports whether the foundation is
 * usable. A failure is permanent for the process and leaves the authentic path
 * exactly as it was. */
static int hwr_truecolor_ensure(uint32_t width, uint32_t height)
{
    if (g_truecolor_failed)
        return 0;
    if (g_truecolor_program == 0) {
        g_truecolor_program = hwr_link_geometry_program(g_truecolor_define);
        if (g_truecolor_program == 0) {
            fprintf(stderr, "[renderer_overhaul] the true-colour program did"
                            " not link: %s\n", g_error);
            g_truecolor_failed = 1;
            return 0;
        }
        g_gl.use_program(g_truecolor_program);
        hwr_set_sampler(g_truecolor_program, "u_texels", 0);
        hwr_set_sampler(g_truecolor_program, "u_colormap", 1);
        hwr_set_sampler(g_truecolor_program, "u_tint", 2);
        hwr_set_sampler(g_truecolor_program, "u_glow", 3);
        hwr_set_sampler(g_truecolor_program, "u_blend", 4);
        hwr_set_sampler(g_truecolor_program, "u_dst", 5);
        hwr_set_sampler(g_truecolor_program, "u_stale", 6);
        hwr_set_sampler(g_truecolor_program, "u_floor_id", 7);
        /* The world pass already spends units 0-7; the palette joins on 8, the
         * previous frame's colour on 9 and what the light at the eye got past
         * on 10. */
        hwr_set_sampler(g_truecolor_program, "u_palette", 8);
        hwr_set_sampler(g_truecolor_program, "u_stale_color", 9);
        hwr_set_sampler(g_truecolor_program, "u_transmit", 10);
        g_gl.use_program(0);
    }
    if (g_truecolor_resolve_program == 0) {
        g_truecolor_resolve_program =
            hwr_link_program(g_resolve_vertex_shader,
                             g_truecolor_resolve_fragment_shader);
        if (g_truecolor_resolve_program == 0) {
            fprintf(stderr, "[renderer_overhaul] the true-colour resolve did"
                            " not link: %s\n", g_error);
            g_truecolor_failed = 1;
            return 0;
        }
        g_gl.use_program(g_truecolor_resolve_program);
        hwr_set_sampler(g_truecolor_resolve_program, "u_source", 0);
        hwr_set_sampler(g_truecolor_resolve_program, "u_palette", 1);
        g_gl.use_program(0);
    }
    if (g_base_palette_texture == 0) {
        g_gl.active_texture(GL_TEXTURE0);
        hwr_texture_2d_u8(&g_base_palette_texture, 256, 1, GL_RGBA8, GL_RGBA,
                          NULL);
        g_gl.bind_texture(GL_TEXTURE_2D, 0);
    }
    /* The law can move without a restart, and with it the precision the colour
     * has to be kept at. The attachment is the only thing that has to be rebuilt
     * for it: the frame primes it from the standing index plane before it draws,
     * so the frame after the change composes the same picture into the new one
     * and nothing has to be carried across. */
    if (g_color_texture != 0 &&
        g_truecolor_format != hwr_truecolor_wanted_format()) {
        if (g_index_fbo != 0) {
            g_gl.bind_framebuffer(GL_FRAMEBUFFER, g_index_fbo);
            g_gl.framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2,
                                        GL_TEXTURE_2D, 0, 0);
            g_gl.framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3,
                                        GL_TEXTURE_2D, 0, 0);
        }
        hwr_delete_texture(&g_color_texture);
        hwr_delete_texture(&g_stale_color_texture);
        hwr_delete_texture(&g_transmit_texture);
        g_stale_color_valid = 0;
        g_truecolor_format = HWR_GL_COLOR_AUTO;
        g_truecolor_logged = 0;
    }
    if (g_color_texture == 0) {
        enum hwr_gl_color_format wanted = hwr_truecolor_wanted_format();

        if (g_index_fbo == 0 || width != g_fbo_width || height != g_fbo_height)
            return 0;
        g_gl.bind_framebuffer(GL_FRAMEBUFFER, g_index_fbo);
        hwr_gl_drain_errors();
        if (!hwr_truecolor_attach(width, height, wanted)) {
            if (wanted == HWR_GL_COLOR_RGBA16F) {
                fprintf(stderr, "[renderer_overhaul] this driver will not take "
                                "a floating-point world image; using eight bits"
                                " per channel\n");
                g_truecolor_refused_rgba16f = 1;
                wanted = HWR_GL_COLOR_RGBA8;
            }
            if (!hwr_truecolor_attach(width, height, wanted)) {
                snprintf(g_error, sizeof g_error,
                         "the true-colour attachment leaves the framebuffer"
                         " incomplete");
                fprintf(stderr, "[renderer_overhaul] %s\n", g_error);
                g_truecolor_failed = 1;
                return 0;
            }
        }
        g_truecolor_format = wanted;
        /* A colour target with nothing in it describes no picture at all, so
         * the frame that creates one primes it whatever else is true. */
        g_color_prime_pending = 1;
        if (!g_truecolor_logged) {
            unsigned channel_bytes = wanted == HWR_GL_COLOR_RGBA16F ? 8u : 4u;
            fprintf(stderr, "[renderer_overhaul] true-colour foundation active"
                            " (%ux%u %s, %luKB)\n", width, height,
                    wanted == HWR_GL_COLOR_RGBA16F ? "RGBA16F" : "RGBA8",
                    (unsigned long)(((uint64_t)width * height * channel_bytes)
                                    / 1024u));
            g_truecolor_logged = 1;
        }
    }
    return 1;
}

static uint32_t hwr_hash_bytes(const uint8_t *bytes, size_t count)
{
    uint32_t hash = 2166136261u;
    size_t i;
    for (i = 0; i < count; i++)
        hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

/* HWR_VERIFY_STORE=1 enables the whole-store self-test: an FNV hash of the
 * uploaded texel store plus, on the first frame after an upload, a full read-
 * back and re-hash. Both are linear passes over the multi-MB store (the readback
 * also stalls the pipeline), so they are off by default; the cheap per-frame
 * probe/colormap/sampler checks always run and still catch a corrupted store. */
static int hwr_gl_store_verify_enabled(void)
{
    static int state = -1;
    if (state < 0) {
        const char *setting = getenv("HWR_VERIFY_STORE");
        state = setting != NULL && setting[0] != '0' && setting[0] != 0 ? 1 : 0;
    }
    return state;
}

int hwr_gl_scene_upload(const uint8_t *texture_bytes, size_t texture_size,
                        size_t reserve, size_t prefix_size,
                        const uint8_t colormap[0x2000],
                        const uint8_t tint[0x2000],
                        const uint8_t glow[0x200],
                        const uint8_t blend[0x10000])
{
    const size_t stride = sizeof(struct hwr_gl_vertex);
    if (!g_initialized || texture_bytes == NULL || texture_size == 0 ||
        texture_size > (size_t)PTRDIFF_MAX || !hwr_platform_make_current()) {
        snprintf(g_error, sizeof g_error, "scene texture upload inputs are invalid");
        return 0;
    }
    if (reserve < texture_size)
        reserve = texture_size;
    if (reserve > (size_t)PTRDIFF_MAX)
        reserve = texture_size;
    if (prefix_size > texture_size)
        prefix_size = 0;
    hwr_destroy_scene_objects();
    if (!hwr_gl_ensure_programs())
        return 0;
    {
        /* The headroom is only worth having if the driver will hold it: never
         * ask a buffer texture for more texels than it can address. */
        GLint max_texels = 0;
        g_gl.get_integer_v(GL_MAX_TEXTURE_BUFFER_SIZE, &max_texels);
        if (max_texels > 0 && reserve > (size_t)max_texels)
            reserve = texture_size > (size_t)max_texels ? texture_size
                                                        : (size_t)max_texels;
    }

    g_gl.gen_buffers(1, &g_texture_buffer);
    g_gl.bind_buffer(GL_TEXTURE_BUFFER, g_texture_buffer);
    /* Allocate the headroom, then write the bytes that exist: a later map's
     * tail is written into the slack rather than replacing the whole store. */
    g_gl.buffer_data(GL_TEXTURE_BUFFER, (GLsizeiptr)reserve, NULL,
                     GL_STATIC_DRAW);
    g_gl.buffer_sub_data(GL_TEXTURE_BUFFER, 0, (GLsizeiptr)texture_size,
                         texture_bytes);
    g_store_reserve = reserve;
    g_gl.gen_textures(1, &g_texture_buffer_texture);
    g_gl.bind_texture(GL_TEXTURE_BUFFER, g_texture_buffer_texture);
    g_gl.tex_buffer(GL_TEXTURE_BUFFER, GL_R8UI, g_texture_buffer);
    g_gl.pixel_store_i(GL_UNPACK_ALIGNMENT, 1);
    hwr_texture_2d_u8(&g_colormap_texture, 256, 32, GL_R8UI,
                      GL_RED_INTEGER, colormap);
    hwr_texture_2d_u8(&g_tint_texture, 256, 32, GL_R8UI,
                      GL_RED_INTEGER, tint);
    hwr_texture_2d_u8(&g_glow_texture, 256, 2, GL_R8UI,
                      GL_RED_INTEGER, glow);
    hwr_texture_2d_u8(&g_blend_texture, 256, 256, GL_R8UI,
                      GL_RED_INTEGER, blend);
    /* And a copy of that table on this side, because how much of what is behind
     * a blended surface survives it is a property of the table and the palette
     * together, and the palette arrives with the frame rather than with the
     * map. */
    if (blend != NULL) {
        memcpy(g_blend_bytes, blend, sizeof g_blend_bytes);
        g_blend_bytes_valid = 1;
    } else {
        g_blend_bytes_valid = 0;
    }
    g_transmit_blend_pending = 1;
    hwr_texture_2d_u8(&g_palette_texture, 256, 1, GL_RGBA8, GL_RGBA, NULL);

    g_gl.gen_vertex_arrays(1, &g_scene_vao);
    g_gl.bind_vertex_array(g_scene_vao);
    g_gl.gen_buffers(1, &g_scene_vbo);
    g_gl.bind_buffer(GL_ARRAY_BUFFER, g_scene_vbo);
    g_gl.enable_vertex_attrib_array(0);
    g_gl.vertex_attrib_i_pointer(0, 3, GL_SHORT, (GLsizei)stride,
        (const void *)offsetof(struct hwr_gl_vertex, x));
    g_gl.enable_vertex_attrib_array(1);
    g_gl.vertex_attrib_pointer(1, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride,
        (const void *)offsetof(struct hwr_gl_vertex, wall_s));
    g_gl.enable_vertex_attrib_array(2);
    g_gl.vertex_attrib_i_pointer(2, 1, GL_UNSIGNED_INT, (GLsizei)stride,
        (const void *)offsetof(struct hwr_gl_vertex, texture_offset));
    g_gl.enable_vertex_attrib_array(3);
    g_gl.vertex_attrib_i_pointer(3, 2, GL_UNSIGNED_SHORT, (GLsizei)stride,
        (const void *)offsetof(struct hwr_gl_vertex, texture_width));
    g_gl.enable_vertex_attrib_array(4);
    g_gl.vertex_attrib_i_pointer(4, 1, GL_UNSIGNED_SHORT, (GLsizei)stride,
        (const void *)offsetof(struct hwr_gl_vertex, aux));
    g_gl.enable_vertex_attrib_array(5);
    g_gl.vertex_attrib_i_pointer(5, 4, GL_UNSIGNED_BYTE, (GLsizei)stride,
        (const void *)offsetof(struct hwr_gl_vertex, pan_u));
    g_gl.enable_vertex_attrib_array(6);
    g_gl.vertex_attrib_i_pointer(6, 4, GL_UNSIGNED_BYTE, (GLsizei)stride,
        (const void *)offsetof(struct hwr_gl_vertex, light));
    /* The bottom-edge and top-edge shade words are adjacent, so one
     * two-component attribute carries both and leaves slot 15 for the surface
     * normal -- the format stays inside the sixteen guaranteed vertex
     * attributes with a normal in it, which is why the normal is free. */
    g_gl.enable_vertex_attrib_array(7);
    g_gl.vertex_attrib_i_pointer(7, 2, GL_UNSIGNED_SHORT, (GLsizei)stride,
        (const void *)offsetof(struct hwr_gl_vertex, wall_shade_endpoint));
    /* wall_tint_adjust and flat_taint are adjacent bytes, so one two-component
     * attribute carries both and leaves slot 14 for the position remainder --
     * the format stays inside the sixteen guaranteed vertex attributes. */
    g_gl.enable_vertex_attrib_array(13);
    g_gl.vertex_attrib_i_pointer(13, 2, GL_UNSIGNED_BYTE, (GLsizei)stride,
        (const void *)offsetof(struct hwr_gl_vertex, wall_tint_adjust));
    g_gl.enable_vertex_attrib_array(14);
    g_gl.vertex_attrib_pointer(14, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride,
        (const void *)offsetof(struct hwr_gl_vertex, pos_frac_x));
    /* The array carries the normal whichever program is bound: an enabled
     * attribute a program never declares is simply not read, and one array
     * object serving both programs is what keeps the two pictures the same
     * geometry. In the original picture these two bytes are the reserved pair
     * on every vertex. */
    g_gl.enable_vertex_attrib_array(15);
    g_gl.vertex_attrib_i_pointer(15, 2, GL_UNSIGNED_BYTE, (GLsizei)stride,
        (const void *)offsetof(struct hwr_gl_vertex, normal_x));
    g_gl.enable_vertex_attrib_array(8);
    g_gl.vertex_attrib_i_pointer(8, 4, GL_UNSIGNED_INT, (GLsizei)stride,
        (const void *)offsetof(struct hwr_gl_vertex, flat_guard));
    g_gl.enable_vertex_attrib_array(9);
    g_gl.vertex_attrib_i_pointer(9, 4, GL_UNSIGNED_INT, (GLsizei)stride,
        (const void *)offsetof(struct hwr_gl_vertex, wall_source_numerator));
    g_gl.enable_vertex_attrib_array(10);
    g_gl.vertex_attrib_i_pointer(10, 4, GL_UNSIGNED_INT, (GLsizei)stride,
        (const void *)offsetof(struct hwr_gl_vertex, wall_top_accum));
    g_gl.enable_vertex_attrib_array(11);
    g_gl.vertex_attrib_i_pointer(11, 4, GL_UNSIGNED_INT, (GLsizei)stride,
        (const void *)offsetof(struct hwr_gl_vertex, wall_first_x));
    g_gl.enable_vertex_attrib_array(12);
    g_gl.vertex_attrib_i_pointer(12, 4, GL_UNSIGNED_INT, (GLsizei)stride,
        (const void *)offsetof(struct hwr_gl_vertex, wall_vertical_origin));
    g_gl.gen_vertex_arrays(1, &g_resolve_vao);

    g_gl.use_program(g_geometry_program);
    hwr_set_sampler(g_geometry_program, "u_texels", 0);
    hwr_set_sampler(g_geometry_program, "u_colormap", 1);
    hwr_set_sampler(g_geometry_program, "u_tint", 2);
    hwr_set_sampler(g_geometry_program, "u_glow", 3);
    hwr_set_sampler(g_geometry_program, "u_blend", 4);
    hwr_set_sampler(g_geometry_program, "u_dst", 5);
    hwr_set_sampler(g_geometry_program, "u_stale", 6);
    g_gl.use_program(g_resolve_program);
    hwr_set_sampler(g_resolve_program, "u_index", 0);
    hwr_set_sampler(g_resolve_program, "u_palette", 1);
    g_gl.use_program(g_sky_program);
    hwr_set_sampler(g_sky_program, "u_texels", 0);
    g_gl.use_program(g_probe_program);
    hwr_set_sampler(g_probe_program, "u_texels", 0);
    hwr_set_sampler(g_probe_program, "u_colormap", 1);
    g_gl.use_program(0);

    memcpy(g_verify_colormap, colormap, sizeof g_verify_colormap);
    g_verify_store_hash = hwr_gl_store_verify_enabled()
                        ? hwr_hash_bytes(texture_bytes, texture_size) : 0;
    g_verify_store_hash_valid = 1;
    g_verify_store_size = texture_size;
    /* Probe a byte inside the run that outlives a map change, so the recurring
     * check keeps a fixed reference across tail rewrites. */
    g_verify_probe_offset = (int)((prefix_size != 0 ? prefix_size
                                                    : texture_size) / 2u);
    g_verify_probe_texel = texture_bytes[(size_t)g_verify_probe_offset];
    g_verify_first_texel = texture_bytes[0];
    g_verify_last_texel = texture_bytes[texture_size - 1u];
    g_verify_pending = 1;
    g_verify_last_status = -1;
    {
        GLint max_texels = 0;
        GLint colormap_unit = -1;
        GLint location = g_gl.get_uniform_location(g_geometry_program,
                                                   "u_colormap");
        g_gl.get_integer_v(GL_MAX_TEXTURE_BUFFER_SIZE, &max_texels);
        if (location >= 0)
            g_gl.get_uniform_iv(g_geometry_program, location, &colormap_unit);
        fprintf(stderr, "[renderer_overhaul] texture store %u bytes "
                "(driver texel limit %d, colormap sampler unit %d)\n",
                (unsigned)texture_size, (int)max_texels, (int)colormap_unit);
    }

    g_scene_uploaded = 1;
    return 1;
}

size_t hwr_gl_scene_store_reserve(void)
{
    return g_scene_uploaded && g_texture_buffer != 0 ? g_store_reserve : 0;
}

int hwr_gl_scene_update_store(size_t offset, const uint8_t *bytes, size_t size,
                              const uint8_t colormap[0x2000],
                              const uint8_t tint[0x2000],
                              const uint8_t glow[0x200],
                              const uint8_t blend[0x10000])
{
    if (!g_initialized || !g_scene_uploaded || g_texture_buffer == 0 ||
        g_texture_buffer_texture == 0 || bytes == NULL || size == 0 ||
        colormap == NULL || tint == NULL || glow == NULL || blend == NULL ||
        offset > g_store_reserve || size > g_store_reserve - offset ||
        size > (size_t)PTRDIFF_MAX || !hwr_platform_make_current()) {
        snprintf(g_error, sizeof g_error,
                 "no resident texture store can take this map's texels");
        return 0;
    }
    g_gl.bind_buffer(GL_TEXTURE_BUFFER, g_texture_buffer);
    g_gl.buffer_sub_data(GL_TEXTURE_BUFFER, (GLintptr)offset,
                         (GLsizeiptr)size, bytes);
    /* The colour tables are per map and small; they are simply replaced. */
    hwr_delete_texture(&g_colormap_texture);
    hwr_delete_texture(&g_tint_texture);
    hwr_delete_texture(&g_glow_texture);
    hwr_delete_texture(&g_blend_texture);
    g_gl.pixel_store_i(GL_UNPACK_ALIGNMENT, 1);
    hwr_texture_2d_u8(&g_colormap_texture, 256, 32, GL_R8UI,
                      GL_RED_INTEGER, colormap);
    hwr_texture_2d_u8(&g_tint_texture, 256, 32, GL_R8UI,
                      GL_RED_INTEGER, tint);
    hwr_texture_2d_u8(&g_glow_texture, 256, 2, GL_R8UI,
                      GL_RED_INTEGER, glow);
    hwr_texture_2d_u8(&g_blend_texture, 256, 256, GL_R8UI,
                      GL_RED_INTEGER, blend);
    memcpy(g_verify_colormap, colormap, sizeof g_verify_colormap);
    /* The store's used length now ends with this tail. The probe sits in the
     * untouched prefix and keeps its recorded value; the whole-store hash
     * cannot be maintained without the prefix bytes, so it is retired rather
     * than left describing a store that no longer matches it. */
    g_verify_store_size = offset + size;
    g_verify_last_texel = bytes[size - 1u];
    if ((size_t)g_verify_probe_offset >= offset &&
        (size_t)g_verify_probe_offset - offset < size)
        g_verify_probe_texel = bytes[(size_t)g_verify_probe_offset - offset];
    if (offset == 0)
        g_verify_first_texel = bytes[0];
    g_verify_store_hash = 0;
    g_verify_store_hash_valid = 0;
    g_verify_pending = 1;
    g_verify_last_status = -1;
    fprintf(stderr, "[renderer_overhaul] texture store %u bytes "
            "(%u kept, %u rewritten at %u)\n",
            (unsigned)g_verify_store_size, (unsigned)offset,
            (unsigned)size, (unsigned)offset);
    return 1;
}

int hwr_gl_scene_update_texels(uint32_t offset, const uint8_t *bytes,
                               uint32_t size)
{
    if (!g_initialized || !g_scene_uploaded || g_texture_buffer == 0 ||
        bytes == NULL || size == 0 || offset > g_verify_store_size ||
        size > (uint32_t)(g_verify_store_size - offset) ||
        !hwr_platform_make_current())
        return 0;
    /* The one-shot store self-test hashes the bytes exactly as they were
     * uploaded, so it has to read the store back before anything starts
     * rewriting parts of it. Callers retry on the next frame. */
    if (g_verify_pending)
        return 0;
    g_gl.bind_buffer(GL_TEXTURE_BUFFER, g_texture_buffer);
    g_gl.buffer_sub_data(GL_TEXTURE_BUFFER, (GLintptr)offset,
                         (GLsizeiptr)size, bytes);
    /* Keep the recurring texel probes describing what the store now holds,
     * so they still catch a store that has genuinely gone bad. */
    if (g_verify_probe_offset >= 0 &&
        (uint32_t)g_verify_probe_offset >= offset &&
        (uint32_t)g_verify_probe_offset - offset < size)
        g_verify_probe_texel = bytes[(uint32_t)g_verify_probe_offset - offset];
    if (offset == 0)
        g_verify_first_texel = bytes[0];
    if ((size_t)offset + size == g_verify_store_size)
        g_verify_last_texel = bytes[size - 1u];
    return 1;
}

static void hwr_bind_scene_textures(void)
{
    GLuint textures[7] = {
        g_texture_buffer_texture, g_colormap_texture, g_tint_texture,
        g_glow_texture, g_blend_texture, g_index_copy_texture,
        g_stale_copy_texture
    };
    GLenum targets[7] = {
        GL_TEXTURE_BUFFER, GL_TEXTURE_2D, GL_TEXTURE_2D,
        GL_TEXTURE_2D, GL_TEXTURE_2D, GL_TEXTURE_2D, GL_TEXTURE_2D
    };
    unsigned i;
    for (i = 0; i < 7; i++) {
        g_gl.active_texture(GL_TEXTURE0 + i);
        g_gl.bind_texture(targets[i], textures[i]);
    }
    /* The world pass spends units 0-7; the enhanced program resolves its own
     * emitted index through the untinted palette on unit 8, and reads the
     * previous frame's colour on unit 9. */
    if (g_truecolor_active) {
        g_gl.active_texture(GL_TEXTURE0 + 8);
        g_gl.bind_texture(GL_TEXTURE_2D, g_base_palette_texture);
        g_gl.active_texture(GL_TEXTURE0 + 9);
        g_gl.bind_texture(GL_TEXTURE_2D, g_stale_color_texture);
        g_gl.active_texture(GL_TEXTURE0 + 10);
        g_gl.bind_texture(GL_TEXTURE_2D, g_transmit_texture);
    }
    /* The final pass of the previous frame leaves the index framebuffer's
     * color attachment bound to unit 0's two-dimensional slot. Sampling
     * anything on that unit while drawing into that framebuffer is then a
     * feedback hazard, and drivers may legally return zeros for the unit's
     * fetches; clear the stale binding before the world pass. */
    g_gl.active_texture(GL_TEXTURE0);
    g_gl.bind_texture(GL_TEXTURE_2D, 0);
}

/* The world pass's draw-buffer sets. The colour attachment joins each set only
 * while the true-colour target exists; with the foundation off these are the
 * one- and two-element arrays the pass has always issued. */
static void hwr_draw_buffers_world(void)
{
    static const GLenum both_buffers[2] =
        { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    static const GLenum with_color[3] =
        { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
    if (g_truecolor_active)
        g_gl.draw_buffers(3, with_color);
    else
        g_gl.draw_buffers(2, both_buffers);
}

static void hwr_draw_buffers_index(void)
{
    static const GLenum index_only[1] = { GL_COLOR_ATTACHMENT0 };
    static const GLenum with_color[3] =
        { GL_COLOR_ATTACHMENT0, GL_NONE, GL_COLOR_ATTACHMENT2 };
    if (g_truecolor_active)
        g_gl.draw_buffers(3, with_color);
    else
        g_gl.draw_buffers(1, index_only);
}

static void hwr_uniform_1i(GLuint program, const char *name, GLint value)
{
    GLint location = g_gl.get_uniform_location(program, name);
    if (location >= 0)
        g_gl.uniform_1i(location, value);
}

static void hwr_uniform_2i(GLuint program, const char *name, GLint x, GLint y)
{
    GLint location = g_gl.get_uniform_location(program, name);
    if (location >= 0)
        g_gl.uniform_2i(location, x, y);
}

static void hwr_uniform_1f(GLuint program, const char *name, GLfloat value)
{
    GLint location = g_gl.get_uniform_location(program, name);
    if (location >= 0)
        g_gl.uniform_1f(location, value);
}

static void hwr_uniform_2f(GLuint program, const char *name, GLfloat x, GLfloat y)
{
    GLint location = g_gl.get_uniform_location(program, name);
    if (location >= 0)
        g_gl.uniform_2f(location, x, y);
}

static void hwr_uniform_3f(GLuint program, const char *name,
                           GLfloat x, GLfloat y, GLfloat z)
{
    GLint location = g_gl.get_uniform_location(program, name);
    if (location >= 0)
        g_gl.uniform_3f(location, x, y, z);
}

/* The modern law's rig, as the settings layer worked it out. Bound whatever the
 * law is: only the modern one reads any of it, and binding it unconditionally
 * keeps one road from the rows to the fragment rather than a branch here and a
 * branch there. The sun/moon slot is bound at zero -- the shape a later key
 * light lands in, costing one multiply of zero. */
static void hwr_bind_light_uniforms(GLuint program,
                                    const struct hwr_gl_light *light)
{
    hwr_uniform_2f(program, "u_room_light", light->room_gain,
                   light->room_gamma);
    hwr_uniform_1i(program, "u_room_curve", (GLint)light->room_curve);
    hwr_uniform_2f(program, "u_room_anchor", light->room_anchor_low,
                   light->room_anchor_high);
    hwr_uniform_2f(program, "u_room_level", light->room_dark, light->room_lit);
    hwr_uniform_1f(program, "u_room_shape", light->room_shape);
    /* A reach of nothing is not a darkness, it is a division: the engine law
     * divides by it. The row's own range is well clear of zero and this is what
     * holds a value that arrived some other way. */
    hwr_uniform_1f(program, "u_room_reach",
                   light->room_reach > 0.0f ? light->room_reach : 1.0f);
    hwr_uniform_1f(program, "u_room_shoulder", light->room_shoulder);
    hwr_uniform_1f(program, "u_room_unshaded", light->room_unshaded_pull);
    hwr_uniform_2f(program, "u_light_limits", light->room_floor,
                   light->world_ceiling);
    hwr_uniform_1i(program, "u_room_ceiling_authentic",
                   light->room_ceiling_authentic != 0);
    hwr_uniform_1f(program, "u_distance_shading", light->distance_shading);
    hwr_uniform_3f(program, "u_ambient_color", light->ambient_color[0],
                   light->ambient_color[1], light->ambient_color[2]);
    hwr_uniform_1f(program, "u_ambient_intensity", light->ambient_intensity);
    hwr_uniform_3f(program, "u_lamp_color", light->lamp_color[0],
                   light->lamp_color[1], light->lamp_color[2]);
    /* The strength and the reach the rows hold, times what the map's own candle
     * word and this frame's flame make of them. With neither asking both are
     * exactly one, so the two uniforms carry the rows' own bits. */
    hwr_uniform_1f(program, "u_lamp_intensity",
                   light->lamp_intensity * g_lamp_glow_intensity *
                   g_lamp_flicker);
    hwr_uniform_1f(program, "u_lamp_range",
                   light->lamp_range * g_lamp_glow_range);
    /* A share of that reach rather than a distance, so a light the map asks to
     * carry further is carried further ahead in the same proportion and the
     * pool keeps its shape. */
    hwr_uniform_1f(program, "u_lamp_forward", light->lamp_forward);
    hwr_uniform_1i(program, "u_lamp_scope_all", light->lamp_scope_all != 0);
    /* The rows of a candle-lit room's own candle the light supplies, through
     * the map's own lantern word -- a map asking for a brighter lantern gets
     * one here too. The flame is NOT in it: the original's flicker moves the
     * pool's origin and never the candle's amplitude, and amplitude flicker
     * here reads as a lamp focusing and unfocusing, not a flame. The
     * flame reaches this light as the wander of the cone's origin
     * (u_cone_wander) and as the breath of the additive lamp's own strength
     * above, which the original never had a term for. With the word not
     * asking the factor is exactly one, so at full amount the shader
     * subtracts the map's own row count to the bit and the sum is the
     * original's picture. */
    hwr_uniform_1f(program, "u_candle_amount",
                   light->candle_amount * g_lamp_glow_intensity);
    hwr_uniform_1f(program, "u_candle_pool", light->candle_pool);
    /* How softly the cone goes over from light to dark; nothing is the
     * engine's own edge and the shader never evaluates the softened arm. */
    hwr_uniform_1f(program, "u_cone_penumbra", light->cone_penumbra);
    hwr_uniform_2f(program, "u_sprite_light", light->sprite_gain,
                   light->sprite_wrap);
    hwr_uniform_3f(program, "u_key_dir", 0.0f, 0.0f, -1.0f);
    hwr_uniform_3f(program, "u_key_color", 1.0f, 1.0f, 1.0f);
    hwr_uniform_1f(program, "u_key_intensity", 0.0f);
    /* The softness of the vertical window every light that is not at the eye is
     * held inside. The window travels with each light; this is one row, so it
     * is one uniform beside the rig rather than a third component per light. */
    hwr_uniform_1f(program, "u_point_z_edge", light->point_z_edge);
}

/* The geometry stage's own world-to-view transform, in C, for a position that is
 * not a vertex.
 *
 * It is the transform at the top of the vertex stage line for line: the whole
 * units are added to the view offset and wrapped in sixteen bits -- the engine's
 * own add, which a light at the far edge of the world depends on -- the
 * sub-unit remainder is added after the wrap, the pair is turned into the view's
 * basis, a reflected view flips the across axis, and the vertical is the drop
 * from the eye. What comes out is the space the fragment tail reconstructs its
 * own position in, so a term written against the two needs no third space to
 * agree in. */
static float hwr_wrap16f(int32_t value)
{
    int32_t wrapped = value & 0xffff;

    return (float)(wrapped >= 0x8000 ? wrapped - 0x10000 : wrapped);
}

static void hwr_point_light_view(const struct hwr_gl_camera *camera,
                                 float world_x, float world_y, float world_z,
                                 float out[3])
{
    int32_t whole_x = (int32_t)floorf(world_x);
    int32_t whole_y = (int32_t)floorf(world_y);
    float across = hwr_wrap16f(whole_x + camera->view_offset_x) +
                   (world_x - (float)whole_x);
    float along = hwr_wrap16f(whole_y + camera->view_offset_y) +
                  (world_y - (float)whole_y);
    float yaw_sin = (float)camera->yaw_sin / 16384.0f;
    float yaw_cos = (float)camera->yaw_cos / 16384.0f;
    float view_x = yaw_sin * across + yaw_cos * along;

    out[0] = camera->x_flip != 0 ? -view_x : view_x;
    out[1] = yaw_sin * along - yaw_cos * across;
    out[2] = (float)camera->eye_z - world_z;
}

/* The frame's lights, in this view's own space. The vertical window stays in
 * world units: the fragment reconstructs its own world height from the position
 * above, so the window is the same window in every view. */
static void hwr_bind_point_lights(GLuint program,
                                  const struct hwr_gl_camera *camera)
{
    GLfloat position_range[HWR_GL_POINT_LIGHT_MAX * 4];
    GLfloat color_energy[HWR_GL_POINT_LIGHT_MAX * 4];
    GLfloat window[HWR_GL_POINT_LIGHT_MAX * 2];
    GLint location;
    size_t i;

    hwr_uniform_1i(program, "u_point_count", (GLint)g_point_light_count);
    if (g_point_light_count == 0)
        return;
    for (i = 0; i < g_point_light_count; i++) {
        const struct hwr_gl_point_light *light = &g_point_lights[i];
        float view[3];

        hwr_point_light_view(camera, light->world_x, light->world_y,
                             light->world_z, view);
        position_range[i * 4u + 0u] = view[0];
        position_range[i * 4u + 1u] = view[1];
        position_range[i * 4u + 2u] = view[2];
        position_range[i * 4u + 3u] = light->range;
        color_energy[i * 4u + 0u] = light->color[0];
        color_energy[i * 4u + 1u] = light->color[1];
        color_energy[i * 4u + 2u] = light->color[2];
        color_energy[i * 4u + 3u] = light->intensity;
        window[i * 2u + 0u] = light->z_lo;
        window[i * 2u + 1u] = light->z_hi;
    }
    location = g_gl.get_uniform_location(program, "u_point_a");
    if (location >= 0)
        g_gl.uniform_4fv(location, (GLsizei)g_point_light_count,
                         position_range);
    location = g_gl.get_uniform_location(program, "u_point_b");
    if (location >= 0)
        g_gl.uniform_4fv(location, (GLsizei)g_point_light_count, color_energy);
    location = g_gl.get_uniform_location(program, "u_point_z");
    if (location >= 0)
        g_gl.uniform_2fv(location, (GLsizei)g_point_light_count, window);
}

/* Verifies the uploaded lookup data three ways: the texture store bytes read
 * back from the buffer object, the colormap bytes read back from the texture,
 * and a live two-pixel draw that samples both through the world pass's texture
 * units. Runs on the first frame after every upload and re-checks quietly
 * afterwards, logging only failures and changes. Leaves no state behind that
 * the world pass does not reset itself. */
static void hwr_gl_scene_self_test(int first_run)
{
    int store_ok = 0, colormap_ok = 0, probe_ok = 0, samplers_ok = 1;
    GLint samplers[7] = { -1, -1, -1, -1, -1, -1, -1 };
    uint8_t colormap_read[0x2000];
    uint8_t probe_read[4] = { 0, 0, 0, 0 };
    uint8_t expected_colormap = g_verify_colormap[16 * 256 + 128];
    int status;
    unsigned pre_errors = hwr_gl_drain_errors();
    unsigned post_errors;

    if (!hwr_gl_store_verify_enabled()) {
        /* Whole-store hashing is opt-in; the probe/colormap/sampler checks
         * below still run every self-test. Treat the store bit as satisfied so
         * the status line does not report a spurious mismatch. */
        store_ok = 1;
    } else if (!g_verify_store_hash_valid) {
        /* The store's tail was rewritten in place, so there is no whole-store
         * hash to compare against. The probe/colormap/sampler checks below
         * still run; HWR_DAS_PERSIST=0 restores whole-store hashing. */
        store_ok = 1;
    } else if (first_run) {
        /* Reading the whole store back stalls the pipeline for a visible
         * fraction of a second, so it runs only on the frame after an
         * upload; the recurring checks rely on the probe fetches instead. */
        uint8_t *store_read = (uint8_t *)malloc(g_verify_store_size);
        if (store_read != NULL) {
            g_gl.bind_buffer(GL_TEXTURE_BUFFER, g_texture_buffer);
            g_gl.get_buffer_sub_data(GL_TEXTURE_BUFFER, 0,
                                     (GLsizeiptr)g_verify_store_size,
                                     store_read);
            store_ok = hwr_hash_bytes(store_read, g_verify_store_size) ==
                       g_verify_store_hash;
            free(store_read);
        }
    } else {
        store_ok = (g_verify_last_status & 1) != 0;
    }

    g_gl.active_texture(GL_TEXTURE1);
    g_gl.bind_texture(GL_TEXTURE_2D, g_colormap_texture);
    g_gl.pixel_store_i(GL_PACK_ALIGNMENT, 1);
    g_gl.get_tex_image(GL_TEXTURE_2D, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE,
                       colormap_read);
    colormap_ok = memcmp(colormap_read, g_verify_colormap,
                         sizeof colormap_read) == 0;

    g_gl.bind_framebuffer(GL_FRAMEBUFFER, g_index_fbo);
    g_gl.viewport(0, 0, 4, 1);
    g_gl.disable(GL_DEPTH_TEST);
    {
        /* The probe writes the palette index and nothing else, so it says so:
         * a draw-buffer set left over from the world pass would otherwise leave
         * the attachments it does not write holding undefined pixels. */
        static const GLenum index_only[1] = { GL_COLOR_ATTACHMENT0 };
        g_gl.draw_buffers(1, index_only);
    }
    g_gl.use_program(g_probe_program);
    hwr_uniform_1i(g_probe_program, "u_offset", g_verify_probe_offset);
    hwr_uniform_1i(g_probe_program, "u_last",
                   (int)(g_verify_store_size - 1u));
    g_gl.active_texture(GL_TEXTURE0);
    g_gl.bind_texture(GL_TEXTURE_BUFFER, g_texture_buffer_texture);
    g_gl.bind_texture(GL_TEXTURE_2D, 0);   /* the resolve pass left the index
                                            * attachment bound here: feedback
                                            * hazard while drawing into it */
    g_gl.active_texture(GL_TEXTURE1);
    g_gl.bind_texture(GL_TEXTURE_2D, g_colormap_texture);
    g_gl.bind_vertex_array(g_resolve_vao);
    g_gl.draw_arrays(GL_TRIANGLES, 0, 3);
    g_gl.read_pixels(0, 0, 4, 1, GL_RED_INTEGER, GL_UNSIGNED_BYTE, probe_read);
    probe_ok = probe_read[0] == g_verify_probe_texel &&
               probe_read[1] == expected_colormap &&
               probe_read[2] == g_verify_first_texel &&
               probe_read[3] == g_verify_last_texel;
    /* The probe has just written four pixels of the palette-index plane that no
     * geometry drew. Give the colour plane the colours those indices resolve
     * to, so everything that reads one plane against the other -- the held-frame
     * re-resolve above all -- keeps seeing two planes that describe the same
     * picture. */
    if (g_truecolor_active && g_color_texture != 0 && g_base_palette_valid) {
        uint8_t probe_color[4 * 4];
        int i;

        for (i = 0; i < 4; i++) {
            const uint8_t *entry = g_base_palette_bytes + probe_read[i] * 4u;

            probe_color[i * 4] = entry[0];
            probe_color[i * 4 + 1] = entry[1];
            probe_color[i * 4 + 2] = entry[2];
            probe_color[i * 4 + 3] = entry[3];
        }
        g_gl.active_texture(GL_TEXTURE0);
        g_gl.bind_texture(GL_TEXTURE_2D, g_color_texture);
        g_gl.pixel_store_i(GL_UNPACK_ALIGNMENT, 1);
        g_gl.tex_sub_image_2d(GL_TEXTURE_2D, 0, 0, 0, 4, 1, GL_RGBA,
                              GL_UNSIGNED_BYTE, probe_color);
        g_gl.bind_texture(GL_TEXTURE_2D, 0);
    }

    {
        /* The world pass depends on the geometry program's sampler uniforms
         * holding units 0..5; read the live values so a lost assignment is
         * visible in the report. */
        static const char *const names[7] = {
            "u_texels", "u_colormap", "u_tint", "u_glow", "u_blend", "u_dst",
            "u_stale"
        };
        int i;
        for (i = 0; i < 7; i++) {
            GLint location = g_gl.get_uniform_location(g_geometry_program,
                                                       names[i]);
            samplers[i] = -1;
            if (location >= 0)
                g_gl.get_uniform_iv(g_geometry_program, location, &samplers[i]);
            samplers_ok = samplers_ok && samplers[i] == i;
        }
    }

    post_errors = hwr_gl_drain_errors();
    status = (store_ok ? 1 : 0) | (colormap_ok ? 2 : 0) | (probe_ok ? 4 : 0) |
             (samplers_ok ? 8 : 0);
    if (first_run || status != g_verify_last_status || status != 15 ||
        pre_errors != 0 || post_errors != 0)
        fprintf(stderr,
                "[renderer_overhaul] sampling self-test: store=%s colormap=%s "
                "probe=%s samplers=%s [%d %d %d %d %d %d %d] (store mid %u/%u "
                "first %u/%u last %u/%u, colormap %u/%u, GL errors %u before "
                "/ %u during)\n",
                store_ok ? "ok" : "MISMATCH",
                colormap_ok ? "ok" : "MISMATCH",
                probe_ok ? "ok" : "MISMATCH",
                samplers_ok ? "ok" : "WRONG",
                samplers[0], samplers[1], samplers[2],
                samplers[3], samplers[4], samplers[5], samplers[6],
                probe_read[0], g_verify_probe_texel,
                probe_read[2], g_verify_first_texel,
                probe_read[3], g_verify_last_texel,
                probe_read[1], expected_colormap,
                pre_errors, post_errors);
    g_verify_last_status = status;
}

int hwr_gl_set_index_seed(const uint8_t *indices, uint32_t width, uint32_t height)
{
    size_t plane = (size_t)width * height;
    uint8_t *copy;

    if (indices == NULL || width == 0 || height == 0 ||
        plane / width != height || plane > (size_t)INT32_MAX)
        return 0;
    /* Re-seeding at the same size is the common case (a map change, a return to
     * gameplay, an inventory close), so the holding buffer is kept rather than
     * released and re-taken every time. */
    if (g_index_seed != NULL && g_index_seed_width == width &&
        g_index_seed_height == height) {
        copy = g_index_seed;
    } else {
        copy = (uint8_t *)malloc(plane);
        if (copy == NULL)
            return 0;
        free(g_index_seed);
    }
    memcpy(copy, indices, plane);
    g_index_seed = copy;
    g_index_seed_width = width;
    g_index_seed_height = height;
    g_index_seed_ready = 1;
    /* The target is only rewritten on a dirty frame, so ask for one. */
    g_index_target_dirty = 1;
    return 1;
}

void hwr_gl_invalidate_index_target(void)
{
    g_index_target_dirty = 1;
}

void hwr_gl_set_stale_mask(int present)
{
    g_stale_mask_present = present != 0;
}

/* Lays a pending seed of the right size into the index target and drops it, so
 * later frames persist into those pixels rather than replacing them. */
static int hwr_consume_index_seed(uint32_t width, uint32_t height)
{
    if (!g_index_seed_ready || g_index_seed == NULL || g_index_texture == 0 ||
        g_index_seed_width != width || g_index_seed_height != height)
        return 0;
    g_gl.active_texture(GL_TEXTURE0);
    g_gl.bind_texture(GL_TEXTURE_2D, g_index_texture);
    g_gl.pixel_store_i(GL_UNPACK_ALIGNMENT, 1);
    g_gl.tex_sub_image_2d(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)width, (GLsizei)height,
                          GL_RED_INTEGER, GL_UNSIGNED_BYTE, g_index_seed);
    g_index_seed_ready = 0;
    return 1;
}

/* Returns the target-space interval occupied by the original source viewport.
 * Pixels outside this interval exist only because the target is wider. */
static int hwr_source_projection_bounds(const struct hwr_gl_camera *camera,
                                        uint32_t width,
                                        int *out_left, int *out_right)
{
    double mapped_left;
    double mapped_right;
    int left;
    int right;

    if (camera == NULL || out_left == NULL || out_right == NULL ||
        camera->source_focal_x == 0 || camera->source_width == 0)
        return 0;
    mapped_left = (double)camera->center_x +
        (-(double)camera->source_center_x) *
        camera->focal_x / camera->source_focal_x;
    mapped_right = (double)camera->center_x +
        ((double)camera->source_width - camera->source_center_x) *
        camera->focal_x / camera->source_focal_x;
    if (mapped_left > mapped_right) {
        double swap = mapped_left;
        mapped_left = mapped_right;
        mapped_right = swap;
    }
    left = (int)ceil(mapped_left);
    right = (int)ceil(mapped_right);
    if (left < 0)
        left = 0;
    if ((uint32_t)left > width)
        left = (int)width;
    if (right < 0)
        right = 0;
    if ((uint32_t)right > width)
        right = (int)width;
    *out_left = left;
    *out_right = right;
    return 1;
}

/* Wiping the target-only wings every frame was an early conservative choice,
 * and it is not what the original does: the original never clears the view, so
 * a pixel no surface covers keeps the previous frame over a backdrop start, and
 * that is now applied to the whole canvas rather than only to the columns the
 * source viewport maps onto. A wing therefore holds content exactly as long as
 * the centre does, and the same repaints that reset the original's void reset
 * the whole canvas here. HWR_WING_WIPE=1 restores the per-frame wipe for
 * diagnosis; it is off by default and nothing else depends on it. */
static int hwr_wing_wipe_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *setting = getenv("HWR_WING_WIPE");
        enabled = setting != NULL && setting[0] != 0 && setting[0] != '0';
    }
    return enabled;
}

static void hwr_clear_wide_persistence_wings(
    const struct hwr_gl_scene_view *views, size_t view_count,
    uint32_t width, uint32_t height, const GLuint clear_index[4])
{
    const struct hwr_gl_scene_view *primary = NULL;
    static const GLenum index_only[1] = { GL_COLOR_ATTACHMENT0 };
    size_t i;
    int center_left;
    int center_right;

    if (!hwr_wing_wipe_enabled())
        return;
    for (i = view_count; i != 0; i--) {
        if (!views[i - 1].camera.reflected) {
            primary = &views[i - 1];
            break;
        }
    }
    if (primary == NULL ||
        !hwr_source_projection_bounds(&primary->camera, width,
                                      &center_left, &center_right) ||
        (center_left == 0 && (uint32_t)center_right == width))
        return;

    g_gl.draw_buffers(1, index_only);
    /* The wings are cleared in index space alone, so the colour plane no longer
     * describes them and has to be primed from the plane that does. */
    g_color_prime_pending = 1;
    g_gl.enable(GL_SCISSOR_TEST);
    if (center_left > 0) {
        g_gl.scissor(0, 0, center_left, (GLsizei)height);
        g_gl.clear_buffer_uiv(GL_COLOR, 0, clear_index);
    }
    if ((uint32_t)center_right < width) {
        g_gl.scissor(center_right, 0, (GLsizei)width - center_right,
                     (GLsizei)height);
        g_gl.clear_buffer_uiv(GL_COLOR, 0, clear_index);
    }
    g_gl.disable(GL_SCISSOR_TEST);
    hwr_draw_buffers_world();
}

static int hwr_gl_render_view(const struct hwr_gl_scene_view *view,
                              uint32_t width, uint32_t height)
{
    const struct hwr_gl_vertex *vertices = view->vertices;
    size_t sky_vertices = view->sky_vertices;
    size_t mirror_depth_vertices = view->mirror_depth_vertices;
    size_t opaque_vertices = view->opaque_vertices;
    size_t flat_vertices = view->flat_vertices;
    size_t translucent_vertices = view->translucent_vertices;
    const uint32_t *translucent_runs = view->translucent_runs;
    const uint8_t *translucent_run_flags = view->translucent_run_flags;
    size_t translucent_run_count = view->translucent_run_count;
    size_t opaque_mesh_vertices = view->opaque_mesh_vertices;
    size_t opaque_billboard_vertices = view->opaque_billboard_vertices;
    size_t shadow_vertices = view->shadow_vertices;
    size_t shadow_run_count = view->shadow_run_count;
    size_t translucent_exception_run_count =
        view->translucent_exception_run_count;
    const struct hwr_gl_exception_run *slab_runs = view->slab_runs;
    size_t slab_run_count = view->slab_run_count;
    int floor_exception = view->floor_exception;
    const struct hwr_gl_camera *camera = &view->camera;
    int sky_active = (camera->sky_valid &&
                      camera->sky_texture_height != 0 &&
                      camera->source_width != 0 &&
                      camera->source_height != 0 &&
                      camera->source_width <= 0x288u) ? 1 : 0;
    size_t total = sky_vertices + mirror_depth_vertices +
                   opaque_vertices + translucent_vertices;
    /* The enhanced program is the same source with the colour output added, so
     * every uniform, sampler, draw call and pass ordering below is shared. With
     * the foundation off this is the program the pass has always used. */
    GLuint program = g_truecolor_active ? g_truecolor_program
                                        : g_geometry_program;
    GLint blend_location;
    /* Whether this view has a plane saying what the light the player carries
     * got past, which is the question the lamp asks before it reads one. */
    int transmit_active = 0;

    g_gl.bind_vertex_array(g_scene_vao);
    g_gl.bind_buffer(GL_ARRAY_BUFFER, g_scene_vbo);
    g_gl.buffer_data(GL_ARRAY_BUFFER, (GLsizeiptr)(total * sizeof *vertices),
                     NULL, GL_STREAM_DRAW);
    g_gl.buffer_data(GL_ARRAY_BUFFER, (GLsizeiptr)(total * sizeof *vertices),
                     vertices, GL_STREAM_DRAW);
    g_gl.bind_framebuffer(GL_FRAMEBUFFER, g_index_fbo);
    g_gl.viewport(0, 0, (GLsizei)width, (GLsizei)height);
    if (view->clip_right > view->clip_left) {
        g_gl.enable(GL_SCISSOR_TEST);
        g_gl.scissor(view->clip_left, 0,
                     (GLsizei)(view->clip_right - view->clip_left),
                     (GLsizei)height);
    } else {
        g_gl.disable(GL_SCISSOR_TEST);
    }
    /* The world pass records both the palette index (COLOR0) and the owner-floor
     * identity (COLOR1); the exception/translucent passes switch back to COLOR0
     * only so they never disturb the recorded ids. */
    hwr_draw_buffers_world();
    /* Depth and floor ownership belong to this view alone. Colour remains so
     * later views composite over earlier ones. */
    {
        const GLuint clear_floor[4] = { 0u, 0u, 0u, 0u };
        g_gl.clear_buffer_uiv(GL_COLOR, 1, clear_floor);
    }
    g_gl.clear(GL_DEPTH_BUFFER_BIT);
    hwr_bind_scene_textures();
    if (g_debug_vis < 0) {
        const char *vis = getenv("HWR_SHADE_VIS");
        /* HWR_SHADE_VIS drives the base render ONLY as the standalone shade-map
         * feature.  Under HWR_COMPARE the base render is the final-output plane
         * (plane 1) and must stay at debug_vis 0; the compare path sets the
         * probe plane's vis explicitly per cycle (hwr_gl_set_debug_vis).  Without
         * this guard the first compare cycle -- the only one a briefly-valid
         * scene such as the main-menu backdrop ever emits -- captured the base
         * render at the env-seeded vis, so plane 1 showed the shade-row map (the
         * palette's coincidental yellow ramp) instead of the real world. */
        /* Read as a whole number rather than a single digit: the planes that
         * report a surface's normal are numbered past nine. */
        g_debug_vis = (getenv("HWR_COMPARE") == NULL &&
                       vis != NULL && vis[0] >= '0' && vis[0] <= '9')
                    ? (int)strtol(vis, NULL, 10) : 0;
    }
    if (g_sky_occlude < 0) {
        /* On by default: an EDGE_MAP face is meant to hide what stands behind
         * it.  HWR_SKY_OCCLUDE=0 restores the older non-occluding sky. */
        const char *occlude = getenv("HWR_SKY_OCCLUDE");
        g_sky_occlude = occlude != NULL && occlude[0] == '0' ? 0 : 1;
    }
    /* Sky is no longer a full-screen backdrop.  The per-wall sky caps emitted in
     * hwr_scene.c (SKY-material branch of the geometry fragment shader) are drawn
     * first as a depth-off background pass, then opaque geometry overwrites the
     * sky wherever a nearer surface projects -- reproducing the engine's rule of
     * writing sky everywhere a sky-wall projects it and letting geometry paint
     * over it. */
    g_gl.bind_vertex_array(g_scene_vao);
    g_gl.enable(GL_DEPTH_TEST);
    g_gl.depth_func(GL_LESS);
    g_gl.depth_mask(GL_TRUE);
    g_gl.disable(GL_BLEND);
    g_gl.use_program(program);
    /* Sampler assignments have been observed to come up lost on some runs
     * (every lookup then lands on unit 0 and the world renders black);
     * re-asserting them each frame costs nothing and heals that state. */
    hwr_set_sampler(program, "u_texels", 0);
    hwr_set_sampler(program, "u_colormap", 1);
    hwr_set_sampler(program, "u_tint", 2);
    hwr_set_sampler(program, "u_glow", 3);
    hwr_set_sampler(program, "u_blend", 4);
    hwr_set_sampler(program, "u_dst", 5);
    hwr_set_sampler(program, "u_stale", 6);
    hwr_set_sampler(program, "u_floor_id", 7);
    if (g_truecolor_active) {
        /* With every dependent neutral the early-out is taken and the colour
         * emitted beside each index is exactly the colour that index resolves
         * to, so the resolved canvas is the authentic canvas. A dependent that
         * has moved -- the world gain today -- clears the early-out and the
         * composition runs.
         *
         * The instrument's negative self-test detunes the gain, and has to
         * detune it far enough to survive the 8-bit resolve: a gain of 0.999
         * shifts a channel by at most 0.255 of a step and rounds back to the
         * byte it started from, so it would leave the instrument reporting a
         * clean run. This one moves every channel from 51 up. */
        GLfloat gain = g_truecolor_break ? 0.99f : g_truecolor_want.gain;
        /* The self-test detunes the gain and nothing else, so what it proves
         * stays what it proved when it was written. */
        GLfloat contrast = g_truecolor_break ? 1.0f : g_truecolor_want.contrast;
        int neutral = g_truecolor_break ? 0 : g_truecolor_want.neutral;
        if (gain <= 0.0f)
            gain = 1.0f;
        if (contrast <= 0.0f)
            contrast = 1.0f;
        hwr_set_sampler(program, "u_palette", 8);
        hwr_set_sampler(program, "u_stale_color", 9);
        hwr_set_sampler(program, "u_transmit", 10);
        /* Whether there is a previous frame in colour to read. Where there is
         * not -- the original law, a frame with no surface that reads one, the
         * first frame after the target came into existence -- the class falls
         * back to the colour its index resolves to, which is what it has always
         * shown and is the right answer wherever the two agree. */
        hwr_uniform_1i(program, "u_stale_color_valid", g_stale_color_valid);
        hwr_uniform_3f(program, "u_gain", gain, gain, gain);
        hwr_uniform_3f(program, "u_add", 0.0f, 0.0f, 0.0f);
        hwr_uniform_1f(program, "u_contrast", contrast);
        hwr_uniform_1i(program, "u_light_neutral", neutral);
        /* The self-test detunes the gain and nothing else, so the law it runs
         * against is whatever the settings layer asked for. */
        hwr_uniform_1i(program, "u_shading_mode",
                       (GLint)g_truecolor_want.shading_mode);
        hwr_uniform_1i(program, "u_shade_ramp", hwr_shade_ramp_env());
        /* The working space, which only a departing law may be composed in. */
        hwr_uniform_1i(program, "u_light_linear",
                       hwr_shade_linear_wanted(g_truecolor_want.shading_mode));
        /* The three parts of the pool, each resolved against the law in force
         * unless a handle asked for it by name. */
        hwr_uniform_1i(program, "u_shade_fixed_origin",
                       hwr_shade_part_wanted(hwr_shade_origin_fixed_env(),
                                             g_truecolor_want.shading_mode));
        hwr_uniform_1i(program, "u_shade_round",
                       hwr_shade_part_wanted(hwr_shade_radial_round_env(),
                                             g_truecolor_want.shading_mode));
        hwr_uniform_1i(program, "u_shade_per_fragment",
                       hwr_shade_part_wanted(hwr_shade_per_fragment_env(),
                                             g_truecolor_want.shading_mode));
        /* What a blended texel lets through, as the map's own table has it. */
        hwr_uniform_1f(program, "u_transmit_blend", g_transmit_blend);
        hwr_uniform_1i(program, "u_transmit_pass", 0);
        hwr_uniform_1i(program, "u_transmit_receive", 1);
        hwr_bind_light_uniforms(program, &g_truecolor_want.light);
        /* And the lights that are not at the eye, in THIS view's space: the
         * rig above is one frame's answer, this is one view's. */
        hwr_bind_point_lights(program, camera);
        /* The directional key, where one is standing. The rig above staged
         * the slot's zero rest; a frame with a sun fills it, per view: the
         * direction is turned into this view's own basis exactly as a normal
         * is, so a reflected view is lit from the reflected side by
         * construction, and the shadow lookup stays in world space, which
         * every view shares. */
        hwr_set_sampler(program, "u_sun_shadow", 11);
        hwr_set_sampler(program, "u_sun_gobo", 12);
        hwr_uniform_1i(program, "u_sun_valid", g_sun_map_valid);
        hwr_uniform_1i(program, "u_sun_gobo_valid", g_sun_gobo_valid);
        if (hwr_sun_wanted()) {
            float s = (float)camera->yaw_sin / 16384.0f;
            float c = (float)camera->yaw_cos / 16384.0f;
            float wx = -g_sun.dir[0];
            float wy = -g_sun.dir[1];
            float wz = -g_sun.dir[2];
            float rx = s * wx + c * wy;

            if (camera->x_flip != 0)
                rx = -rx;
            hwr_uniform_3f(program, "u_key_dir", rx, s * wy - c * wx, -wz);
            hwr_uniform_3f(program, "u_key_color", g_sun.color[0],
                           g_sun.color[1], g_sun.color[2]);
            hwr_uniform_1f(program, "u_key_intensity", g_sun.intensity);
        }
        if (g_sun_map_valid) {
            GLint location =
                g_gl.get_uniform_location(program, "u_sun_matrix");

            if (location >= 0)
                g_gl.uniform_matrix_4fv(location, 1, GL_FALSE, g_sun.lookup);
            hwr_uniform_1f(program, "u_sun_bias", g_sun.bias);
            hwr_uniform_1f(program, "u_sun_bias_floor", g_sun.bias_floor);
            hwr_uniform_1f(program, "u_sun_bias_slope", g_sun.bias_slope);
            hwr_uniform_1i(program, "u_sun_soft", g_sun.soft);
            hwr_uniform_1f(program, "u_sun_texel",
                           g_sun.resolution > 0
                               ? 1.0f / (float)g_sun.resolution : 0.0f);
            hwr_uniform_1i(program, "u_sun_taps", g_sun.soft_taps);
            hwr_uniform_1f(program, "u_sun_radius",
                           g_sun.resolution > 0
                               ? g_sun.soft_radius / (float)g_sun.resolution
                               : 0.0f);
            g_gl.active_texture(GL_TEXTURE0 + 11);
            g_gl.bind_texture(GL_TEXTURE_2D, g_sun_depth_texture);
            g_gl.active_texture(GL_TEXTURE0);
        }
        if (g_sun_gobo_valid) {
            g_gl.active_texture(GL_TEXTURE0 + 12);
            g_gl.bind_texture(GL_TEXTURE_2D, g_sun_gobo_texture);
            g_gl.active_texture(GL_TEXTURE0);
        }
    }
    /* The world pass never samples the floor id (it writes it); keep the gate off
     * and clear unit 7 so sampling the attachment being written is impossible. */
    hwr_uniform_1i(program, "u_floor_exception", 0);
    g_gl.active_texture(GL_TEXTURE0 + 7);
    g_gl.bind_texture(GL_TEXTURE_2D, 0);
    g_gl.active_texture(GL_TEXTURE0);
    hwr_uniform_1i(program, "u_debug_vis", g_debug_vis);
    hwr_uniform_1i(program, "u_depth_only", 0);
    hwr_uniform_2i(program, "u_view_offset",
                   camera->view_offset_x, camera->view_offset_y);
    hwr_uniform_1i(program, "u_eye_z", camera->eye_z);
    hwr_uniform_1i(program, "u_x_flip", camera->x_flip != 0);
    hwr_uniform_2f(program, "u_basis",
                   (GLfloat)camera->yaw_sin / 16384.0f,
                   (GLfloat)camera->yaw_cos / 16384.0f);
    hwr_uniform_2i(program, "u_basis_fixed",
                   camera->yaw_sin, camera->yaw_cos);
    hwr_uniform_2f(program, "u_focal",
                   (GLfloat)camera->focal_x, (GLfloat)camera->focal_y);
    hwr_uniform_2i(program, "u_focal_i",
                   camera->focal_x, camera->focal_y);
    hwr_uniform_2f(program, "u_center",
                   (GLfloat)camera->center_x, (GLfloat)camera->horizon);
    hwr_uniform_2i(program, "u_center_i",
                   camera->center_x, camera->horizon);
    hwr_uniform_2i(program, "u_shade_origin",
                   camera->shade_origin_x, camera->shade_origin_y);
    /* The flame's wander of the modern cone's origin, scaled here into the
     * origin's own screen units: the engine's amplitude is a sixteenth of the
     * per-axis focal (renderer.c:13411-13426), the wander is kept in that
     * unit, and the focal is the same one the radial divides by -- so a full
     * wander is the engine's own step whatever the resolution, and a still
     * flame is the fixed centre to the bit. */
    hwr_uniform_2i(program, "u_cone_wander",
                   (GLint)lrintf(g_lamp_wander_x *
                                 (float)camera->focal_x / 16.0f),
                   (GLint)lrintf(g_lamp_wander_y *
                                 (float)camera->focal_y / 16.0f));
    /* The fixed centre's own row: the view centre before the pitch shift,
     * which is where the engine anchors its candle origin -- the horizon
     * carries the pitch term, this anchor never does, so the pool follows
     * the gaze when the view pitches. */
    hwr_uniform_1i(program, "u_cone_center_y", camera->cone_center_y);
    hwr_uniform_1i(program, "u_flat_edge_bias", camera->flat_edge_bias);
    hwr_uniform_2f(program, "u_viewport", (GLfloat)width, (GLfloat)height);
    hwr_uniform_2i(program, "u_source_viewport",
                   camera->source_width, camera->source_height);
    hwr_uniform_2i(program, "u_source_focal",
                   camera->source_focal_x, camera->source_focal_y);
    hwr_uniform_2i(program, "u_source_center",
                   camera->source_center_x, camera->source_horizon);
    hwr_uniform_1i(program, "u_shade_level", camera->shade_level);
    hwr_uniform_1i(program, "u_tint_shade_level", camera->tint_shade_level);
    hwr_uniform_1i(program, "u_light_bias", camera->global_light_bias);
    hwr_uniform_1i(program, "u_fill_index",
                   camera->solid_fill_index);
    /* wd_project's own bias term, LIVE: the engine stages one word into
     * both of its edge projectors every frame -- the map's candle word plus
     * the player-state term, zero with the player in the candle-lit class
     * and minus thirteen outside it (renderer.c:9385-9391,
     * collision_physics.c:1774-1778). Modelling it as a constant 8 scaled by
     * the shade level erased that state: a candle-lit room read the same
     * from either side of its own doorway. */
    hwr_uniform_1i(program, "u_wd_bias", (GLint)camera->wall_edge_bias);
    hwr_uniform_1i(program, "u_candle_word", (GLint)camera->candle_word);
    /* The out-of-class step, behind the candle dial and outside the flame:
     * the flame breathes the light in the hand, not the absence of one. */
    hwr_uniform_1f(program, "u_candle_state",
                   (GLfloat)(camera->wall_edge_bias - camera->candle_word) *
                   g_truecolor_want.light.candle_amount);
    hwr_uniform_1i(program, "u_glow_phase", camera->glow_phase);
    {
        /* Feed the sky-cap fragment branch the same inputs the retired
         * full-screen prepass used, gated on the identical draw condition
         * (valid sky texture and a viewport the projection table covers) so the
         * caps sample the skybox only when the backdrop previously would have. */
        hwr_uniform_1i(program, "u_sky_valid", sky_active);
        hwr_uniform_1i(program, "u_sky_texture_offset",
                       (GLint)camera->sky_texture_offset);
        hwr_uniform_1i(program, "u_sky_source_offset",
                       camera->sky_source_offset);
        hwr_uniform_1i(program, "u_sky_projection_offset",
                       (GLint)camera->sky_projection_offset);
        hwr_uniform_1i(program, "u_sky_texture_height",
                       camera->sky_texture_height);
        hwr_uniform_1i(program, "u_sky_start_row", camera->sky_start_row);
        hwr_uniform_1i(program, "u_sky_yaw", camera->sky_yaw);
        hwr_uniform_1i(program, "u_sky_double_scanline",
                       camera->sky_double_scanline);
        hwr_uniform_1i(program, "u_sky_occlusion_pass", 0);
    }
    {
        GLint scale_location =
            g_gl.get_uniform_location(program, "u_depth_scale");
        if (scale_location >= 0)
            g_gl.uniform_1f(scale_location, camera->shade_depth_scale > 0.0f
                                          ? camera->shade_depth_scale : 1.0f);
    }
    blend_location = g_gl.get_uniform_location(program, "u_blend_pass");
    if (blend_location >= 0)
        g_gl.uniform_1i(blend_location, 0);
    /* What the light the player carries has to pass through to reach what it
     * lights.
     *
     * That light is at the eye, so it casts no shadow on anything you can see:
     * everything visible is unoccluded along the view ray, and for a light at
     * the eye the view ray IS the light ray. The one case left is a surface the
     * eye can see THROUGH -- a grate, a pane, a see-through figure -- which the
     * eye passes and the light must not pass whole. Because the two rays are
     * the same ray, a plane of what the view passed through is that light's own
     * shadow map, and it is exact rather than approximate for every fragment
     * the opaque pass drew: only surfaces NEARER than the opaque winner are in
     * it, and the opaque winner is the fragment being lit.
     *
     * The order that forces is the whole of this block. The test is against the
     * opaque winner's depth, so that depth has to stand before the pass runs;
     * and the lamp is composed where the opaque winner is DRAWN, so the pass
     * has to be finished before that. The depth is therefore laid down first
     * with nothing written but depth -- the same draws, in the same order, at
     * the same offsets, as the ones below that write it for real -- and the
     * frame's own depth starts again from nothing behind it.
     *
     * Every surface that can stop the light is in the translucent range: a
     * masked surface that OWNS depth is either the fragment being lit (its own
     * texel) or a hole the light goes straight through, so there is nothing it
     * can shadow -- the same geometry, once more, that puts no shadow on
     * anything a light at the eye can see. */
    if (hwr_transmit_wanted() && translucent_vertices != 0) {
        static const GLenum no_buffers[1] = { GL_NONE };
        static const GLenum transmit_only[4] =
            { GL_NONE, GL_NONE, GL_NONE, GL_COLOR_ATTACHMENT3 };
        static const GLfloat clear_transmit[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        size_t opaque_main = opaque_vertices >= flat_vertices
                           ? opaque_vertices - flat_vertices : opaque_vertices;
        size_t world_vertices =
            opaque_main - opaque_mesh_vertices - opaque_billboard_vertices;

        transmit_active = 1;
        /* Nothing samples the plane while it is being written. */
        g_gl.active_texture(GL_TEXTURE0 + 10);
        g_gl.bind_texture(GL_TEXTURE_2D, 0);
        g_gl.active_texture(GL_TEXTURE0);
        /* The light gets through everywhere nothing is drawn. */
        g_gl.draw_buffers(4, transmit_only);
        g_gl.clear_buffer_fv(GL_COLOR, 3, clear_transmit);
        /* The depth, with nothing written but depth. */
        g_gl.draw_buffers(1, no_buffers);
        g_gl.enable(GL_DEPTH_TEST);
        g_gl.depth_func(GL_LESS);
        g_gl.depth_mask(GL_TRUE);
        if (sky_vertices != 0 && g_sky_occlude) {
            g_gl.enable(GL_POLYGON_OFFSET_FILL);
            g_gl.polygon_offset(1.0f, 1.0f);
            hwr_uniform_1i(program, "u_sky_occlusion_pass", 1);
            g_gl.draw_arrays(GL_TRIANGLES, 0, (GLsizei)sky_vertices);
            hwr_uniform_1i(program, "u_sky_occlusion_pass", 0);
            g_gl.polygon_offset(0.0f, 0.0f);
            g_gl.disable(GL_POLYGON_OFFSET_FILL);
        }
        if (mirror_depth_vertices != 0) {
            hwr_uniform_1i(program, "u_depth_only", 1);
            g_gl.draw_arrays(GL_TRIANGLES,
                             (GLint)(sky_vertices + opaque_vertices +
                                     translucent_vertices),
                             (GLsizei)mirror_depth_vertices);
        }
        /* Two rather than one: the mirror plane above is deliberately not
         * masked, and this geometry has to be, or a grate would own the depth
         * of the wall seen through its holes. */
        hwr_uniform_1i(program, "u_depth_only", 2);
        if (world_vertices != 0)
            g_gl.draw_arrays(GL_TRIANGLES, (GLint)sky_vertices,
                             (GLsizei)world_vertices);
        if (opaque_mesh_vertices != 0) {
            g_gl.enable(GL_POLYGON_OFFSET_FILL);
            g_gl.polygon_offset(-1.0f, -1.0f);
            g_gl.draw_arrays(GL_TRIANGLES,
                             (GLint)(sky_vertices + world_vertices),
                             (GLsizei)opaque_mesh_vertices);
            g_gl.polygon_offset(0.0f, 0.0f);
            g_gl.disable(GL_POLYGON_OFFSET_FILL);
        }
        if (opaque_billboard_vertices != 0)
            g_gl.draw_arrays(GL_TRIANGLES,
                             (GLint)(sky_vertices + world_vertices +
                                     opaque_mesh_vertices),
                             (GLsizei)opaque_billboard_vertices);
        hwr_uniform_1i(program, "u_depth_only", 0);
        /* And the pass itself: what each see-through surface lets past, one
         * plane, multiplied together where two of them stand in the same pixel.
         * A product is the same product in any order, so the whole range goes
         * in one draw whatever the runs the painter order needs are. */
        g_gl.draw_buffers(4, transmit_only);
        g_gl.depth_mask(GL_FALSE);
        g_gl.enable(GL_BLEND);
        g_gl.blend_func(GL_ZERO, GL_SRC_COLOR);
        hwr_uniform_1i(program, "u_transmit_pass", 1);
        g_gl.draw_arrays(GL_TRIANGLES,
                         (GLint)(sky_vertices + opaque_vertices),
                         (GLsizei)translucent_vertices);
        hwr_uniform_1i(program, "u_transmit_pass", 0);
        g_gl.disable(GL_BLEND);
        /* The frame's own depth starts again from nothing, exactly as it stood
         * when this block was entered. */
        g_gl.depth_mask(GL_TRUE);
        g_gl.clear(GL_DEPTH_BUFFER_BIT);
        hwr_draw_buffers_world();
        g_gl.active_texture(GL_TEXTURE0 + 10);
        g_gl.bind_texture(GL_TEXTURE_2D, g_transmit_texture);
        g_gl.active_texture(GL_TEXTURE0);
    }
    hwr_uniform_1i(program, "u_transmit_valid", transmit_active);
    /* Sky background candidate: colour only, no depth interaction.  Every cap
     * contributes colour here; the occlusion pass below is the selective one. */
    g_gl.disable(GL_DEPTH_TEST);
    g_gl.depth_mask(GL_FALSE);
    if (sky_vertices != 0)
        g_gl.draw_arrays(GL_TRIANGLES, 0, (GLsizei)sky_vertices);
    /* The engine visits directed wall faces and rejects the back side before
     * EDGE_MAP sky consumes the remaining vertical column extent.  Our mesh
     * contains both sides of every portal, so making every cap depth-writing
     * lets the reverse cap on a low portal step blanket the view.  Repeat the
     * same cap range as an order-independent GPU depth pass, but have the
     * fragment shader discard the engine-rejected directed side.  The cap's x/y
     * is the wall's x/y, hence it writes exactly that wall's depth. */
    if (sky_vertices != 0 && g_sky_occlude) {
        g_gl.enable(GL_DEPTH_TEST);
        g_gl.depth_func(GL_LESS);
        g_gl.depth_mask(GL_TRUE);
        /* Push the sky occluder a hair farther than the wall it stands on.  A
         * cap shares its wall's x/y, so any of that SAME wall's geometry above
         * the cap base (a facade above an arched opening) projects at an
         * IDENTICAL depth; unbiased, GL_LESS rejects that coplanar facade and
         * the colour-pass sky bleeds through it.  With the bias the cap occludes
         * only geometry strictly BEHIND the wall, which is what an EDGE_MAP face
         * should hide. */
        g_gl.enable(GL_POLYGON_OFFSET_FILL);
        g_gl.polygon_offset(1.0f, 1.0f);
        hwr_uniform_1i(program, "u_sky_occlusion_pass", 1);
        g_gl.draw_arrays(GL_TRIANGLES, 0, (GLsizei)sky_vertices);
        hwr_uniform_1i(program, "u_sky_occlusion_pass", 0);
        g_gl.polygon_offset(0.0f, 0.0f);
        g_gl.disable(GL_POLYGON_OFFSET_FILL);
    }
    /* Reflections were composited before the primary view, whose depth buffer
     * must be cleared because it uses a different camera. Re-stamp each
     * scheduled mirror plane without touching colour so primary geometry
     * behind that plane cannot paint over the reflected image. Geometry nearer
     * than the plane still passes normally, and a translucent authored mirror
     * overlay later passes at equal depth under GL_LEQUAL. */
    if (mirror_depth_vertices != 0) {
        static const GLenum no_buffers[1] = { GL_NONE };
        g_gl.enable(GL_DEPTH_TEST);
        g_gl.depth_func(GL_LESS);
        g_gl.depth_mask(GL_TRUE);
        g_gl.draw_buffers(1, no_buffers);
        hwr_uniform_1i(program, "u_depth_only", 1);
        g_gl.draw_arrays(GL_TRIANGLES,
                         (GLint)(sky_vertices + opaque_vertices +
                                 translucent_vertices),
                         (GLsizei)mirror_depth_vertices);
        hwr_uniform_1i(program, "u_depth_only", 0);
        hwr_draw_buffers_world();
    }
    /* Opaque geometry overwrites the colour-only sky and wins against a
     * farther sky occluder wherever it projects. Stock paints sector faces
     * before 3D object meshes. Give the following mesh pass the smallest
     * toward-viewer polygon offset so independently projected coplanar
     * fragments cannot reverse that painter tie; the object reliably covers
     * its floor or ceiling while an actually nearer world face still occludes.
     *
     * Fixed-orientation (flat) billboards ride the opaque tail and draw last
     * with a small bias TOWARD the viewer, so a decal coplanar with its wall
     * wins the depth test against that wall (the engine paints it after the
     * wall) while a nearer wall still occludes it. */
    g_gl.enable(GL_DEPTH_TEST);
    g_gl.depth_func(GL_LESS);
    g_gl.depth_mask(GL_TRUE);
    {
        size_t opaque_main = opaque_vertices >= flat_vertices
                           ? opaque_vertices - flat_vertices : opaque_vertices;
        size_t world_vertices =
            opaque_main - opaque_mesh_vertices - opaque_billboard_vertices;
        if (world_vertices != 0)
            g_gl.draw_arrays(GL_TRIANGLES, (GLint)sky_vertices,
                             (GLsizei)world_vertices);
        if (opaque_mesh_vertices != 0) {
            g_gl.enable(GL_POLYGON_OFFSET_FILL);
            g_gl.polygon_offset(-1.0f, -1.0f);
            g_gl.draw_arrays(GL_TRIANGLES,
                             (GLint)(sky_vertices + world_vertices),
                             (GLsizei)opaque_mesh_vertices);
            g_gl.polygon_offset(0.0f, 0.0f);
            g_gl.disable(GL_POLYGON_OFFSET_FILL);
        }
        if (opaque_billboard_vertices != 0)
            g_gl.draw_arrays(
                GL_TRIANGLES,
                (GLint)(sky_vertices + world_vertices +
                        opaque_mesh_vertices),
                (GLsizei)opaque_billboard_vertices);
        if (flat_vertices != 0 && opaque_vertices >= flat_vertices) {
            /* Depth TEST stays (a nearer wall still occludes the decal), but
             * depth WRITES are off: the decal is a later painter overlay on its
             * coplanar wall, not a new occluder, so it must not stamp its own
             * (biased-nearer) depth and shadow a sprite that legitimately stands
             * in front of the wall. */
            g_gl.enable(GL_POLYGON_OFFSET_FILL);
            g_gl.polygon_offset(-1.0f, -1.0f);
            g_gl.depth_mask(GL_FALSE);
            g_gl.draw_arrays(GL_TRIANGLES,
                             (GLint)(sky_vertices + opaque_main),
                             (GLsizei)flat_vertices);
            g_gl.depth_mask(GL_TRUE);
            g_gl.polygon_offset(0.0f, 0.0f);
            g_gl.disable(GL_POLYGON_OFFSET_FILL);
        }
    }
    /* From here only the palette index is written; the recorded owner-surface
     * ids stay intact for the exception passes to sample. */
    hwr_draw_buffers_index();
    /* The main translucent range excludes the ground shadows appended to its
     * tail: those go through the floor-exception pass below. */
    {
        size_t trans_main_vertices = translucent_vertices >= shadow_vertices
            ? translucent_vertices - shadow_vertices : translucent_vertices;
        size_t trans_main_runs = translucent_run_count >= shadow_run_count
            ? translucent_run_count - shadow_run_count : translucent_run_count;
    /* Owner-surface depth exception (HWR_FLOOR_EXCEPTION). Re-draw object quads
     * that may overlay their own floor, plus fixed-orientation wall art authored
     * slightly through its owner-sector wall. The identity gate keeps nearer
     * objects, doors, and other sectors occluding. This also seats the ground
     * shadow BEHIND its monster for free -- the monster stamped identity 0
     * across its silhouette, so the shadow is gated out there. */
    if (floor_exception && (opaque_billboard_vertices != 0 ||
                            flat_vertices != 0 || shadow_vertices != 0 ||
                            slab_run_count != 0)) {
        size_t opaque_main = opaque_vertices >= flat_vertices
                           ? opaque_vertices - flat_vertices : opaque_vertices;
        g_gl.active_texture(GL_TEXTURE0 + 7);
        g_gl.bind_texture(GL_TEXTURE_2D, g_floor_id_texture);
        g_gl.active_texture(GL_TEXTURE0);
        hwr_uniform_1i(program, "u_floor_exception", 1);
        /* Opaque mid-platform surfaces go FIRST, while the depth test is still
         * on: they are world geometry, and the objects, shadows and wall art
         * that follow stand on top of them. GEQUAL admits exactly the fragments
         * the ordinary pass rejected -- everything at or behind the recorded
         * winner -- so a platform coplanar with its own flat also stops losing
         * the tie, and the identity gate keeps the redraw over that flat. */
        if (slab_run_count != 0) {
            size_t run;
            g_gl.depth_func(GL_GEQUAL);
            g_gl.depth_mask(GL_FALSE);
            if (blend_location >= 0)
                g_gl.uniform_1i(blend_location, 0);
            for (run = 0; run < slab_run_count; run++)
                g_gl.draw_arrays(GL_TRIANGLES, (GLint)slab_runs[run].first,
                                 (GLsizei)slab_runs[run].count);
            g_gl.depth_func(GL_LESS);
        }
        g_gl.disable(GL_DEPTH_TEST);
        g_gl.depth_mask(GL_FALSE);
        /* Painter order WITHIN this depth-off stage decides where the below-floor
         * parts overlap. The shadows go FIRST so they blend against the floor
         * already in the index buffer (not against the object below-floor parts);
         * the opaque object below-floor parts then paint OVER them, so the monster
         * wins everywhere -- matching the engine's paint order above and below the
         * floor line. */
        if (shadow_vertices != 0) {
            GLint first = (GLint)(sky_vertices + opaque_vertices +
                                  trans_main_vertices);
            if (blend_location >= 0)
                g_gl.uniform_1i(blend_location, 1);
            /* Drawn from the range the pass cast from, so it reads none of what
             * it cast. */
            hwr_uniform_1i(program, "u_transmit_receive", 0);
            g_gl.active_texture(GL_TEXTURE5);
            g_gl.bind_texture(GL_TEXTURE_2D, g_index_copy_texture);
            if (translucent_runs == NULL || shadow_run_count == 0) {
                g_gl.copy_tex_sub_image_2d(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
                                           (GLsizei)width, (GLsizei)height);
                g_gl.draw_arrays(GL_TRIANGLES, first, (GLsizei)shadow_vertices);
            } else {
                size_t run;
                for (run = trans_main_runs; run < translucent_run_count; run++) {
                    g_gl.copy_tex_sub_image_2d(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
                                               (GLsizei)width, (GLsizei)height);
                    g_gl.draw_arrays(GL_TRIANGLES, first,
                                     (GLsizei)translucent_runs[run]);
                    first += (GLint)translucent_runs[run];
                }
            }
            hwr_uniform_1i(program, "u_transmit_receive", 1);
        }
        if (opaque_billboard_vertices != 0 &&
            opaque_main >= opaque_billboard_vertices) {
            if (blend_location >= 0)
                g_gl.uniform_1i(blend_location, 0);
            g_gl.draw_arrays(GL_TRIANGLES,
                (GLint)(sky_vertices + opaque_main -
                        opaque_billboard_vertices),
                (GLsizei)opaque_billboard_vertices);
        }
        if (flat_vertices != 0 && opaque_vertices >= flat_vertices) {
            if (blend_location >= 0)
                g_gl.uniform_1i(blend_location, 0);
            g_gl.draw_arrays(GL_TRIANGLES,
                (GLint)(sky_vertices + opaque_vertices - flat_vertices),
                (GLsizei)flat_vertices);
        }
        hwr_uniform_1i(program, "u_floor_exception", 0);
        g_gl.enable(GL_DEPTH_TEST);
        g_gl.depth_mask(GL_TRUE);
    }
    if (trans_main_vertices != 0) {
        /* Translucent surfaces arrive depth-sorted back to front, and each
         * blends against what is already on screen -- INCLUDING the
         * translucent surfaces drawn before it, the way the engine's painter
         * order stacks two shimmering doorways.  Refreshing the destination
         * snapshot before every surface is what makes that stacking real; a
         * single snapshot would make every surface blend against the opaque
         * background alone. */
        size_t run;
        GLint first = (GLint)(sky_vertices + opaque_vertices);
        g_gl.depth_mask(GL_FALSE);
        /* LEQUAL (not LESS): a translucent surface coplanar with the opaque
         * winner behind it -- the ground-shadow oval sitting on its sector floor
         * -- must still draw (the engine paints it after the floor). A nearer
         * wall has a strictly smaller depth and still occludes. Writes stay off,
         * so this never lets a translucent surface shadow nearer geometry. */
        g_gl.depth_func(GL_LEQUAL);
        if (blend_location >= 0)
            g_gl.uniform_1i(blend_location, 1);
        /* Every one of these is a surface the transmittance pass cast from, so
         * none of them reads the plane it cast into: a surface stopping the
         * light does not stop it on its way to itself. */
        hwr_uniform_1i(program, "u_transmit_receive", 0);
        /* The opaque/shadow exception block above is allowed to be absent in a
         * scene containing only translucent pickups. Bind the floor identity
         * target independently for this path instead of inheriting unit 7. */
        if (floor_exception && translucent_exception_run_count != 0) {
            g_gl.active_texture(GL_TEXTURE0 + 7);
            g_gl.bind_texture(GL_TEXTURE_2D, g_floor_id_texture);
            g_gl.active_texture(GL_TEXTURE0);
        }
        g_gl.active_texture(GL_TEXTURE5);
        g_gl.bind_texture(GL_TEXTURE_2D, g_index_copy_texture);
        if (translucent_runs == NULL || trans_main_runs == 0) {
            g_gl.copy_tex_sub_image_2d(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
                                       (GLsizei)width, (GLsizei)height);
            g_gl.draw_arrays(GL_TRIANGLES, first,
                             (GLsizei)trans_main_vertices);
        } else {
            for (run = 0; run < trans_main_runs; run++) {
                g_gl.copy_tex_sub_image_2d(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
                                           (GLsizei)width, (GLsizei)height);
                g_gl.draw_arrays(GL_TRIANGLES, first,
                                 (GLsizei)translucent_runs[run]);
                /* A translucent object is one painter-order operation even
                 * when its lower pixels need the owner-floor exception. Draw
                 * both disjoint portions against the same pre-run destination
                 * snapshot, then let the next run observe their combined
                 * result. A world mid-platform takes the same route with the
                 * depth test set to GREATER rather than switched off: the two
                 * portions must stay disjoint, or a translucent platform would
                 * blend twice into every pixel the ordinary pass already
                 * covered. */
                if (floor_exception && translucent_run_flags != NULL &&
                    (translucent_run_flags[run] &
                     (uint8_t)(HWR_GL_TRANSLUCENT_RUN_FLOOR_EXCEPTION |
                               HWR_GL_TRANSLUCENT_RUN_SLAB_EXCEPTION)) != 0) {
                    int slab = (translucent_run_flags[run] &
                                HWR_GL_TRANSLUCENT_RUN_SLAB_EXCEPTION) != 0;
                    hwr_uniform_1i(program, "u_floor_exception", 1);
                    if (slab)
                        g_gl.depth_func(GL_GREATER);
                    else
                        g_gl.disable(GL_DEPTH_TEST);
                    g_gl.draw_arrays(GL_TRIANGLES, first,
                                     (GLsizei)translucent_runs[run]);
                    hwr_uniform_1i(program, "u_floor_exception", 0);
                    if (!slab)
                        g_gl.enable(GL_DEPTH_TEST);
                    g_gl.depth_func(GL_LEQUAL);
                }
                first += (GLint)translucent_runs[run];
            }
        }
        hwr_uniform_1i(program, "u_transmit_receive", 1);
        g_gl.depth_func(GL_LESS);
        g_gl.depth_mask(GL_TRUE);
    }
    }

    return 1;
}

static int hwr_gl_scene_view_valid(const struct hwr_gl_scene_view *view,
                                   uint32_t width)
{
    size_t total;
    size_t run;
    size_t run_vertices = 0;
    size_t opaque_main;
    size_t translucent_main_runs;
    size_t translucent_exception_runs = 0;

    if (view->sky_vertices > SIZE_MAX - view->mirror_depth_vertices)
        return 0;
    total = view->sky_vertices + view->mirror_depth_vertices;
    if (total > SIZE_MAX - view->opaque_vertices)
        return 0;
    total += view->opaque_vertices;
    if (total > SIZE_MAX - view->translucent_vertices)
        return 0;
    total += view->translucent_vertices;
    if ((total != 0 && view->vertices == NULL) ||
        total > (size_t)INT32_MAX ||
        total > (size_t)PTRDIFF_MAX / sizeof *view->vertices ||
        view->flat_vertices > view->opaque_vertices ||
        view->shadow_vertices > view->translucent_vertices ||
        view->shadow_run_count > view->translucent_run_count)
        return 0;
    opaque_main = view->opaque_vertices - view->flat_vertices;
    translucent_main_runs =
        view->translucent_run_count - view->shadow_run_count;
    if (view->opaque_mesh_vertices > opaque_main ||
        view->opaque_billboard_vertices >
            opaque_main - view->opaque_mesh_vertices ||
        view->translucent_exception_run_count > translucent_main_runs)
        return 0;
    /* Every mid-platform redraw range has to lie inside the opaque range this
     * view already submits: the stage redraws staged geometry, it never
     * introduces any. */
    if (view->slab_run_count != 0) {
        size_t slab;
        if (view->slab_runs == NULL ||
            view->slab_run_count > HWR_GL_SLAB_EXCEPTION_MAX)
            return 0;
        for (slab = 0; slab < view->slab_run_count; slab++) {
            size_t first = view->slab_runs[slab].first;
            size_t count = view->slab_runs[slab].count;
            if (count == 0 || first < view->sky_vertices ||
                first - view->sky_vertices > opaque_main ||
                count > opaque_main - (first - view->sky_vertices))
                return 0;
        }
    }
    if (view->translucent_run_count != 0) {
        if (view->translucent_runs == NULL ||
            view->translucent_run_flags == NULL)
            return 0;
        for (run = 0; run < view->translucent_run_count; run++) {
            size_t count = view->translucent_runs[run];
            uint8_t flags = view->translucent_run_flags[run];
            uint8_t known = (uint8_t)(
                HWR_GL_TRANSLUCENT_RUN_FLOOR_EXCEPTION |
                HWR_GL_TRANSLUCENT_RUN_SLAB_EXCEPTION);
            if ((flags & (uint8_t)~known) != 0 ||
                (run >= translucent_main_runs && flags != 0))
                return 0;
            if (run < translucent_main_runs && (flags & known) != 0)
                translucent_exception_runs++;
            if (count > (size_t)INT32_MAX ||
                run_vertices > SIZE_MAX - count)
                return 0;
            run_vertices += count;
        }
        if (run_vertices != view->translucent_vertices ||
            translucent_exception_runs !=
                view->translucent_exception_run_count)
            return 0;
    }
    if (view->clip_left != view->clip_right &&
        (view->clip_left < 0 || view->clip_right <= view->clip_left ||
         (uint32_t)view->clip_right > width))
        return 0;
    return 1;
}

static int hwr_render_view_set(const struct hwr_gl_scene_view *views,
                               size_t view_count,
                               uint32_t width, uint32_t height)
{
    size_t view_index;
    for (view_index = 0; view_index < view_count; view_index++) {
        if (!hwr_gl_render_view(&views[view_index], width, height))
            return 0;
    }
    return 1;
}

/* Primes the colour attachment from the frame's starting index plane, so void
 * persistence, the backdrop seed, the widescreen wing wipe and the previous
 * frame a stale-masked wall shows through its holes all need no rule of their
 * own: the colour plane is by construction the resolve of whatever the index
 * plane already holds. It samples the SNAPSHOT the stale pass has just taken,
 * never the attachment being written.
 *
 * It resolves through the UNTINTED palette, because the colour plane holds the
 * colours the world was composed in and the palette-level effects are applied
 * to them at the end of the frame. */
static void hwr_truecolor_prime(uint32_t width, uint32_t height)
{
    static const GLenum color_target[1] = { GL_COLOR_ATTACHMENT2 };
    g_gl.bind_framebuffer(GL_FRAMEBUFFER, g_index_fbo);
    g_gl.viewport(0, 0, (GLsizei)width, (GLsizei)height);
    g_gl.disable(GL_SCISSOR_TEST);
    g_gl.disable(GL_DEPTH_TEST);
    g_gl.depth_mask(GL_FALSE);
    g_gl.draw_buffers(1, color_target);
    g_gl.use_program(g_resolve_program);
    hwr_uniform_1i(g_resolve_program, "u_debug_vis", 0);
    g_gl.bind_vertex_array(g_resolve_vao);
    g_gl.active_texture(GL_TEXTURE0);
    g_gl.bind_texture(GL_TEXTURE_2D, g_stale_copy_texture);
    g_gl.active_texture(GL_TEXTURE1);
    g_gl.bind_texture(GL_TEXTURE_2D, g_base_palette_texture);
    g_gl.draw_arrays(GL_TRIANGLES, 0, 3);
    g_gl.depth_mask(GL_TRUE);
    g_gl.active_texture(GL_TEXTURE0);
    g_gl.bind_texture(GL_TEXTURE_2D, 0);
}

/* Priming EVERY frame is the same thing as persisting only while the colour a
 * pixel holds and the colour its index resolves to are one colour -- which is
 * the original law and nothing else. Under a law that composes, re-priming
 * would replace the picture a pixel was actually drawn with by the picture its
 * index stands for: a corridor the player has walked out of, still lit as the
 * lamp left it, would come back at full daylight.
 *
 * So the colour target persists exactly as the index target does, and it is
 * primed on the frames the plane under it restarted -- a seed, a wipe, an
 * invalidation, a target that has just come into existence -- or the frames
 * what a colour MEANS changed, which is a law moving under a standing plane. */
static void hwr_truecolor_prime_pending(uint32_t width, uint32_t height)
{
    if (!g_truecolor_active || !g_color_prime_pending)
        return;
    hwr_truecolor_prime(width, height);
    g_color_prime_pending = 0;
}

/* The colour twin of the frame's starting-plane snapshot: the same pixels of the
 * same moment, in the space the world image is composed in. Taken AFTER any
 * prime, so the two snapshots can never describe two different frames.
 *
 * The read buffer is a property of the framebuffer, and every other copy and
 * read of this one takes the palette-index plane, so it is put back before
 * anything else looks. */
static void hwr_truecolor_snapshot_color(uint32_t width, uint32_t height)
{
    g_gl.bind_framebuffer(GL_FRAMEBUFFER, g_index_fbo);
    g_gl.read_buffer(GL_COLOR_ATTACHMENT2);
    g_gl.active_texture(GL_TEXTURE0 + 9);
    g_gl.bind_texture(GL_TEXTURE_2D, g_stale_color_texture);
    g_gl.copy_tex_sub_image_2d(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
                               (GLsizei)width, (GLsizei)height);
    g_gl.read_buffer(GL_COLOR_ATTACHMENT0);
    g_gl.active_texture(GL_TEXTURE0);
    g_gl.bind_texture(GL_TEXTURE_2D, 0);
}

/* The frame's final resolve into the RGBA target the readback reads. */
static void hwr_scene_resolve_pass(uint32_t width, uint32_t height)
{
    g_gl.disable(GL_SCISSOR_TEST);
    g_gl.bind_framebuffer(GL_FRAMEBUFFER, g_fbo);
    g_gl.viewport(0, 0, (GLsizei)width, (GLsizei)height);
    g_gl.disable(GL_DEPTH_TEST);
    /* Every debug visualisation is an index plane by definition, so it keeps
     * the palette-index resolve however the foundation stands. */
    if (g_truecolor_active && g_debug_vis == 0) {
        g_gl.use_program(g_truecolor_resolve_program);
        hwr_set_sampler(g_truecolor_resolve_program, "u_source", 0);
        hwr_set_sampler(g_truecolor_resolve_program, "u_palette", 1);
        /* The palette-level effects the engine is running reach the world image
         * here, on the colour, rather than through the palette it was composed
         * in. */
        hwr_uniform_1i(g_truecolor_resolve_program, "u_damage_fade",
                       g_effect_fade);
        hwr_uniform_1i(g_truecolor_resolve_program, "u_grayscale",
                       g_effect_grayscale);
        /* And whether this frame is dithered where it is quantized. A neutral
         * picture IS the authentic picture, so it is never patterned whatever
         * the setting asks for -- the state that short-circuits the composition
         * is what decides it, so the two can never disagree. */
        hwr_uniform_1i(g_truecolor_resolve_program, "u_dither",
                       g_truecolor_want.dither && !g_truecolor_want.neutral);
        g_gl.bind_vertex_array(g_resolve_vao);
        g_gl.active_texture(GL_TEXTURE0);
        g_gl.bind_texture(GL_TEXTURE_2D, g_color_texture);
        g_gl.active_texture(GL_TEXTURE1);
        g_gl.bind_texture(GL_TEXTURE_2D, g_base_palette_texture);
        g_gl.draw_arrays(GL_TRIANGLES, 0, 3);
        return;
    }
    g_gl.use_program(g_resolve_program);
    hwr_uniform_1i(g_resolve_program, "u_debug_vis", g_debug_vis);
    g_gl.bind_vertex_array(g_resolve_vao);
    g_gl.active_texture(GL_TEXTURE0);
    g_gl.bind_texture(GL_TEXTURE_2D, g_index_texture);
    g_gl.active_texture(GL_TEXTURE1);
    g_gl.bind_texture(GL_TEXTURE_2D, g_palette_texture);
    g_gl.draw_arrays(GL_TRIANGLES, 0, 3);
}

static int hwr_truecolor_compare_enabled(void)
{
    if (g_truecolor_compare_state < 0) {
        const char *prefix = getenv("HWR_TRUECOLOR_COMPARE");
        const char *negative = getenv("HWR_TRUECOLOR_COMPARE_BREAK");
        g_truecolor_compare_state =
            prefix != NULL && prefix[0] != 0 ? 1 : 0;
        if (g_truecolor_compare_state)
            snprintf(g_truecolor_compare_path,
                     sizeof g_truecolor_compare_path, "%s", prefix);
        g_truecolor_break = g_truecolor_compare_state && negative != NULL &&
                            negative[0] != '0' && negative[0] != 0;
        if (g_truecolor_compare_state)
            fprintf(stderr, "[renderer_overhaul] true-colour compare armed"
                            " (prefix %s%s)\n", g_truecolor_compare_path,
                    g_truecolor_break ? ", NEGATIVE SELF-TEST" : "");
    }
    return g_truecolor_compare_state;
}

/* Restores the index target to the frame's starting plane, so the instrument's
 * two arms render from identical state rather than from each other's result. */
static int hwr_truecolor_restore_index(uint32_t width, uint32_t height)
{
    static const GLenum index_target[1] = { GL_COLOR_ATTACHMENT0 };
    if (g_index_restore_program == 0) {
        g_index_restore_program =
            hwr_link_program(g_resolve_vertex_shader,
                             g_index_restore_fragment_shader);
        if (g_index_restore_program == 0) {
            fprintf(stderr, "[renderer_overhaul] the index restore pass did"
                            " not link: %s\n", g_error);
            return 0;
        }
        g_gl.use_program(g_index_restore_program);
        hwr_set_sampler(g_index_restore_program, "u_source", 0);
        g_gl.use_program(0);
    }
    g_gl.bind_framebuffer(GL_FRAMEBUFFER, g_index_fbo);
    g_gl.viewport(0, 0, (GLsizei)width, (GLsizei)height);
    g_gl.disable(GL_SCISSOR_TEST);
    g_gl.disable(GL_DEPTH_TEST);
    g_gl.depth_mask(GL_FALSE);
    g_gl.draw_buffers(1, index_target);
    g_gl.use_program(g_index_restore_program);
    g_gl.bind_vertex_array(g_resolve_vao);
    g_gl.active_texture(GL_TEXTURE0);
    g_gl.bind_texture(GL_TEXTURE_2D, g_stale_copy_texture);
    g_gl.draw_arrays(GL_TRIANGLES, 0, 3);
    g_gl.depth_mask(GL_TRUE);
    g_gl.bind_texture(GL_TEXTURE_2D, 0);
    return 1;
}

static void hwr_truecolor_write_ppm(const char *suffix, const uint32_t *pixels,
                                    uint32_t width, uint32_t height,
                                    unsigned serial)
{
    char path[2176];
    FILE *file;
    uint32_t i;
    snprintf(path, sizeof path, "%s_%s.ppm", g_truecolor_compare_path, suffix);
    file = fopen(path, "wb");
    if (file == NULL)
        return;
    fprintf(file, "P6\n# frame %u\n%u %u\n255\n", serial, width, height);
    for (i = 0; i < width * height; i++) {
        uint8_t rgb[3];
        rgb[0] = (uint8_t)(pixels[i] >> 16);
        rgb[1] = (uint8_t)(pixels[i] >> 8);
        rgb[2] = (uint8_t)pixels[i];
        fwrite(rgb, 1, 3, file);
    }
    fclose(file);
}

static void hwr_truecolor_write_pgm(const char *suffix, const uint8_t *plane,
                                    uint32_t width, uint32_t height,
                                    unsigned serial)
{
    char path[2176];
    FILE *file;
    snprintf(path, sizeof path, "%s_%s.pgm", g_truecolor_compare_path, suffix);
    file = fopen(path, "wb");
    if (file == NULL)
        return;
    fprintf(file, "P5\n# frame %u\n%u %u\n255\n", serial, width, height);
    fwrite(plane, 1, (size_t)width * height, file);
    fclose(file);
}

/* On a differing frame, name the surface classes involved: render the same view
 * set once more at the material-bit visualisation and dump that plane. The
 * probe leaves the index target holding material bits, so the frame's own image
 * is rendered again afterwards and the canvas re-read. */
static void hwr_truecolor_dump_material(const struct hwr_gl_scene_view *views,
                                        size_t view_count,
                                        uint32_t width, uint32_t height,
                                        unsigned serial)
{
    uint8_t *plane = (uint8_t *)malloc((size_t)width * height);
    int saved_vis = g_debug_vis;
    if (plane != NULL) {
        g_debug_vis = 6;
        g_truecolor_active = 0;
        if (hwr_truecolor_restore_index(width, height) &&
            hwr_render_view_set(views, view_count, width, height) &&
            hwr_gl_read_scene_indices(plane, width, height))
            hwr_truecolor_write_pgm("material", plane, width, height, serial);
        g_debug_vis = saved_vis;
        g_truecolor_active = 1;
        free(plane);
    }
    if (!hwr_truecolor_restore_index(width, height))
        return;
    hwr_truecolor_prime_pending(width, height);
    if (!hwr_render_view_set(views, view_count, width, height))
        return;
    hwr_scene_resolve_pass(width, height);
    g_gl.read_pixels(0, 0, (GLsizei)width, (GLsizei)height,
                     GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, g_pixels);
}

/* The metric: differing pixels between the authentic and the true-colour arm,
 * over the whole canvas. The bar is zero, so anything above it is a defect with
 * a cause and the frame is dumped for it. */
static void hwr_truecolor_compare_report(const struct hwr_gl_scene_view *views,
                                         size_t view_count,
                                         uint32_t width, uint32_t height)
{
    size_t plane = (size_t)width * height;
    size_t differing = 0;
    unsigned max_delta = 0;
    size_t i;
    for (i = 0; i < plane; i++) {
        uint32_t a = g_truecolor_compare_pixels[i];
        uint32_t b = g_pixels[i];
        unsigned shift;
        if (a == b)
            continue;
        differing++;
        for (shift = 0; shift < 32u; shift += 8u) {
            int delta = (int)((a >> shift) & 0xffu) -
                        (int)((b >> shift) & 0xffu);
            if (delta < 0)
                delta = -delta;
            if ((unsigned)delta > max_delta)
                max_delta = (unsigned)delta;
        }
    }
    g_truecolor_compare_frames++;
    g_truecolor_compare_bad_pixels += differing;
    if (max_delta > g_truecolor_compare_max_delta)
        g_truecolor_compare_max_delta = max_delta;
    if (differing != 0) {
        g_truecolor_compare_bad_frames++;
        fprintf(stderr, "[renderer_overhaul] truecolor compare frame %u:"
                        " differing=%lu/%lu max-delta=%u\n",
                g_truecolor_compare_frames, (unsigned long)differing,
                (unsigned long)plane, max_delta);
        if (g_truecolor_compare_dumps < 4u) {
            g_truecolor_compare_dumps++;
            hwr_truecolor_write_ppm("authentic", g_truecolor_compare_pixels,
                                    width, height,
                                    g_truecolor_compare_frames);
            hwr_truecolor_write_ppm("truecolor", g_pixels, width, height,
                                    g_truecolor_compare_frames);
            hwr_truecolor_dump_material(views, view_count, width, height,
                                        g_truecolor_compare_frames);
        }
    }
    if ((g_truecolor_compare_frames % 60u) == 0u)
        fprintf(stderr, "[renderer_overhaul] truecolor compare %u frames:"
                        " differing frames=%u pixels=%llu max-delta=%u\n",
                g_truecolor_compare_frames, g_truecolor_compare_bad_frames,
                (unsigned long long)g_truecolor_compare_bad_pixels,
                g_truecolor_compare_max_delta);
}

/* The palette-level effects this frame's resolve has to reproduce, taken from
 * the state the caller published with the palette. */
static void hwr_truecolor_set_effects(const struct hwr_gl_palette *palette)
{
    int fade;

    g_effect_grayscale = palette->grayscale != 0;
    if (palette->damage_level == 0) {
        g_effect_fade = -1;
        return;
    }
    /* The engine holds the fade at zero from below, so a heavy hit saturates
     * the tint rather than coming back out of it. */
    fade = 0xff - (int)palette->damage_level;
    g_effect_fade = fade < 0 ? 0 : fade;
}

const uint32_t *hwr_gl_scene_render(const struct hwr_gl_scene_view *views,
                                    size_t view_count,
                                    const struct hwr_gl_palette *palette,
                                    uint32_t width, uint32_t height)
{
    size_t view_index;
    uint64_t perf_t0 = 0;
    uint64_t perf_setup = 0;
    uint64_t perf_prime = 0;
    uint64_t perf_views = 0;
    uint64_t perf_resolve = 0;
    /* The sun pass's own submit time, taken back out of the setup phase so
     * the reported phases still sum to the total. */
    uint64_t sun_ns = 0;
    static uint64_t perf_setup_sum;
    static uint64_t perf_sun_sum;
    static uint64_t perf_prime_sum;
    static uint64_t perf_views_sum;
    static uint64_t perf_resolve_sum;
    static uint64_t perf_readback_sum;
    static uint64_t perf_total_sum;
    static size_t perf_view_sum;
    static size_t perf_copy_sum;
    static unsigned perf_samples;
    static int perf_enabled = -1;
    int compare_active;
    size_t full_copy_count = 1u; /* previous-frame stale target */
    /* Only reached where the target has no starting art -- the frame after it
     * is created or resized, or a backdrop repaint with no usable image -- so
     * it should be unobtrusive rather than a marker colour. */
    const GLuint clear_index[4] = { 0u, 0, 0, 0 };

    if (!g_initialized || !g_scene_uploaded || views == NULL ||
        view_count == 0 || view_count > (size_t)INT32_MAX ||
        palette == NULL || palette->rgba == NULL || width == 0 || height == 0 ||
        width > (uint32_t)INT32_MAX || height > (uint32_t)INT32_MAX ||
        !hwr_platform_make_current()) {
        snprintf(g_error, sizeof g_error, "scene render inputs are invalid");
        return NULL;
    }
    for (view_index = 0; view_index < view_count; view_index++) {
        size_t main_runs =
            views[view_index].translucent_run_count >=
            views[view_index].shadow_run_count
                ? views[view_index].translucent_run_count -
                  views[view_index].shadow_run_count : 0u;
        if (!hwr_gl_scene_view_valid(&views[view_index], width)) {
            snprintf(g_error, sizeof g_error,
                     "scene view %lu is invalid", (unsigned long)view_index);
            return NULL;
        }
        if (views[view_index].shadow_vertices != 0)
            full_copy_count += views[view_index].shadow_run_count != 0
                             ? views[view_index].shadow_run_count : 1u;
        if (views[view_index].translucent_vertices >
            views[view_index].shadow_vertices)
            full_copy_count += main_runs != 0 ? main_runs : 1u;
    }
    if (perf_enabled < 0) {
        const char *setting = getenv("HWR_FRAME_PERF");
        perf_enabled =
            setting != NULL && setting[0] != '0' && setting[0] != 0 ? 1 : 0;
    }
    if (perf_enabled)
        perf_t0 = hwr_perf_now_ns();
    if ((g_fbo_width != width || g_fbo_height != height) &&
        !hwr_create_framebuffer(width, height))
        return NULL;
    /* The true-colour foundation comes up on the first frame that asks for it
     * and goes on the first frame nothing does. With nothing asking, no
     * enhanced program is compiled, no colour attachment exists, and every pass
     * below issues the calls it has always issued.
     *
     * A frame with no untinted palette to compose in cannot take the enhanced
     * path: the effects would have nowhere to be applied. */
    g_truecolor_active =
        (hwr_truecolor_env() || hwr_truecolor_compare_enabled() ||
         g_truecolor_want.required) &&
        palette->base_rgba != NULL && hwr_truecolor_ensure(width, height);
    if (!g_truecolor_active && g_color_texture != 0)
        hwr_truecolor_release();
    /* A law moving under a standing colour plane changes what the colours in it
     * mean without touching the plane they describe, so the plane is primed
     * from the index one and the frames after it persist again. */
    if (g_truecolor_want.shading_mode != g_color_prime_mode) {
        g_color_prime_mode = g_truecolor_want.shading_mode;
        g_color_prime_pending = 1;
    }
    compare_active = g_truecolor_active && hwr_truecolor_compare_enabled();
    hwr_truecolor_set_effects(palette);

    g_gl.pixel_store_i(GL_UNPACK_ALIGNMENT, 1);
    g_gl.active_texture(GL_TEXTURE1);
    g_gl.bind_texture(GL_TEXTURE_2D, g_palette_texture);
    g_gl.tex_sub_image_2d(GL_TEXTURE_2D, 0, 0, 0, 256, 1,
                          GL_RGBA, GL_UNSIGNED_BYTE, palette->rgba);
    if (g_truecolor_active) {
        g_gl.bind_texture(GL_TEXTURE_2D, g_base_palette_texture);
        g_gl.tex_sub_image_2d(GL_TEXTURE_2D, 0, 0, 0, 256, 1,
                              GL_RGBA, GL_UNSIGNED_BYTE, palette->base_rgba);
        /* What a blended texel lets through is a property of the map's blend
         * table and the colours it is authored against, so a palette that has
         * moved is a weight that has to be measured again. */
        if (!g_base_palette_valid ||
            memcmp(g_base_palette_bytes, palette->base_rgba,
                   sizeof g_base_palette_bytes) != 0)
            g_transmit_blend_pending = 1;
        memcpy(g_base_palette_bytes, palette->base_rgba,
               sizeof g_base_palette_bytes);
        g_base_palette_valid = 1;
        /* Measured only where something is going to read it: once per map
         * for a picture composing with a light at the eye, once per map for
         * a frame whose sun gobo multiplies it in, and never at all for any
         * other. */
        if (g_transmit_blend_pending &&
            (hwr_transmit_wanted() || hwr_sun_wanted()))
            hwr_transmit_measure_blend();
    }

    /* The sun's light-space maps, once per frame, before any view: every
     * view of the frame -- a mirror's included -- reads the one map, exactly
     * as the point-light array is one frame's answer. A frame that does not
     * want them lays nothing down and the lookup stands at lit. After the
     * palette block on purpose: the gobo pass multiplies the fitted blend
     * weight, which is measured there. */
    {
        uint64_t sun_t0 = perf_enabled ? hwr_perf_now_ns() : 0;

        hwr_sun_render_map();
        if (perf_enabled) {
            sun_ns = hwr_perf_now_ns() - sun_t0;
            perf_sun_sum += sun_ns;
        }
    }
    g_gl.bind_framebuffer(GL_FRAMEBUFFER, g_index_fbo);
    g_gl.viewport(0, 0, (GLsizei)width, (GLsizei)height);
    g_gl.disable(GL_SCISSOR_TEST);
    hwr_draw_buffers_world();
    /* The original never wipes the view before drawing it, so a column no
     * surface covers keeps whatever the previous frame left in that pixel.
     * This shows up where a see-through surface has nothing but void behind
     * it: the blend reads those stale pixels as its background. Wiping to a
     * constant here would lighten that result, so the target is cleared only
     * when it is first created or resized. HWR_VOID_PERSIST=0 restores the
     * per-frame wipe. */
    if (g_void_persist < 0) {
        const char *persist = getenv("HWR_VOID_PERSIST");
        g_void_persist = persist != NULL && persist[0] == '0' ? 0 : 1;
    }
    if (!g_void_persist || g_index_target_dirty) {
        /* Whenever the original repaints its screen with the backdrop image --
         * at a map load, and on every return to gameplay from the inventory or
         * a menu -- void restarts as that art rather than as a flat colour, and
         * the seed carries it. */
        if (!hwr_consume_index_seed(width, height))
            g_gl.clear_buffer_uiv(GL_COLOR, 0, clear_index);
        g_index_target_dirty = 0;
        /* The plane the colour target describes has just been replaced, so what
         * the colour target holds no longer describes anything. */
        g_color_prime_pending = 1;
    }
    hwr_clear_wide_persistence_wings(views, view_count, width, height,
                                     clear_index);
    /* Snapshot the persisted target before anything draws: this is the
     * "previous frame" that stale-masked walls show through their holes and
     * blend their high-bit texels against. */
    g_gl.active_texture(GL_TEXTURE6);
    g_gl.bind_texture(GL_TEXTURE_2D, g_stale_copy_texture);
    g_gl.copy_tex_sub_image_2d(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
                               (GLsizei)width, (GLsizei)height);
    if (perf_enabled)
        perf_setup = hwr_perf_now_ns();

    /* The acceptance instrument's authentic arm: the same view set, the same
     * camera, the same palette and the same starting index plane, rendered
     * through the program the foundation replaces. It runs first so the frame's
     * own arm is the one that stands when it is done. */
    if (compare_active) {
        size_t bytes = (size_t)width * height * sizeof *g_pixels;
        if (bytes > g_truecolor_compare_bytes) {
            uint32_t *buffer =
                (uint32_t *)realloc(g_truecolor_compare_pixels, bytes);
            if (buffer == NULL)
                compare_active = 0;
            else {
                g_truecolor_compare_pixels = buffer;
                g_truecolor_compare_bytes = bytes;
            }
        }
    }
    if (compare_active) {
        g_truecolor_active = 0;
        if (!hwr_render_view_set(views, view_count, width, height))
            return NULL;
        hwr_scene_resolve_pass(width, height);
        g_gl.read_pixels(0, 0, (GLsizei)width, (GLsizei)height, GL_BGRA,
                         GL_UNSIGNED_INT_8_8_8_8_REV,
                         g_truecolor_compare_pixels);
        g_truecolor_active = 1;
        compare_active = hwr_truecolor_restore_index(width, height);
    }

    hwr_truecolor_prime_pending(width, height);
    /* And the previous frame in colour, for the surfaces that show it. One
     * full-frame copy, and only where there is a composed picture to copy, a
     * law that would resolve it differently from its indices, and a surface in
     * the frame that reads it. */
    g_stale_color_valid = 0;
    if (g_truecolor_active && g_stale_color_texture != 0 &&
        g_truecolor_want.shading_mode != HWR_GL_SHADING_AUTHENTIC &&
        g_stale_mask_present) {
        hwr_truecolor_snapshot_color(width, height);
        g_stale_color_valid = 1;
        full_copy_count++;
    }
    if (perf_enabled)
        perf_prime = hwr_perf_now_ns();
    if (!hwr_render_view_set(views, view_count, width, height))
        return NULL;
    if (perf_enabled)
        perf_views = hwr_perf_now_ns();

    hwr_scene_resolve_pass(width, height);
    if (perf_enabled)
        perf_resolve = hwr_perf_now_ns();
    g_gl.read_pixels(0, 0, (GLsizei)width, (GLsizei)height,
                     GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, g_pixels);
    if (compare_active)
        hwr_truecolor_compare_report(views, view_count, width, height);
    if (perf_enabled) {
        uint64_t done = hwr_perf_now_ns();
        perf_setup_sum += (perf_setup - perf_t0) - sun_ns;
        perf_prime_sum += perf_prime - perf_setup;
        perf_views_sum += perf_views - perf_prime;
        perf_resolve_sum += perf_resolve - perf_views;
        perf_readback_sum += done - perf_resolve;
        perf_total_sum += done - perf_t0;
        perf_view_sum += view_count;
        perf_copy_sum += full_copy_count;
        perf_samples++;
        if (perf_samples >= 60u) {
            double divisor = (double)perf_samples * 1.0e6;
            fprintf(stderr,
                    "[renderer_overhaul] frame perf %ux%u avg/%u:"
                    " setup=%.2fms sun=%.2fms truecolor-prime=%.2fms"
                    " views-submit=%.2fms"
                    " resolve-submit=%.2fms gpu+readback=%.2fms"
                    " total=%.2fms views=%.1f full-copies=%.1f"
                    " truecolor=%s\n",
                    width, height, perf_samples,
                    perf_setup_sum / divisor,
                    perf_sun_sum / divisor,
                    perf_prime_sum / divisor,
                    perf_views_sum / divisor,
                    perf_resolve_sum / divisor,
                    perf_readback_sum / divisor,
                    perf_total_sum / divisor,
                    (double)perf_view_sum / perf_samples,
                    (double)perf_copy_sum / perf_samples,
                    g_truecolor_active
                        ? (g_truecolor_format == HWR_GL_COLOR_RGBA16F
                               ? "rgba16f" : "rgba8")
                        : "off");
            perf_setup_sum = 0;
            perf_sun_sum = 0;
            perf_prime_sum = 0;
            perf_views_sum = 0;
            perf_resolve_sum = 0;
            perf_readback_sum = 0;
            perf_total_sum = 0;
            perf_view_sum = 0;
            perf_copy_sum = 0;
            perf_samples = 0;
        }
    }
    g_render_count++;
    if (g_verify_pending || (g_render_count & 1023u) == 0u) {
        int first_run = g_verify_pending;
        g_verify_pending = 0;
        hwr_gl_scene_self_test(first_run);
    }
    return g_pixels;
}

const uint32_t *hwr_gl_scene_resolve(const struct hwr_gl_palette *palette,
                                     uint32_t width, uint32_t height)
{
    /* The true-colour twin: the colour target the last render left behind is
     * re-resolved through the new palette-level effects. Both arms are "no
     * geometry redrawn, only the transform changed", so the caller does not
     * have to know which one ran -- except that a frame whose effects cannot be
     * reproduced in colour space falls back to the palette-index arm, which
     * reproduces them exactly by construction. */
    int enhanced = g_truecolor_active && g_color_texture != 0 &&
                   g_base_palette_texture != 0 &&
                   palette != NULL && palette->base_rgba != NULL &&
                   g_debug_vis == 0;

    if (!g_initialized || !g_scene_uploaded || palette == NULL ||
        palette->rgba == NULL || width == 0 || height == 0 ||
        g_fbo_width != width ||
        g_fbo_height != height || g_pixels == NULL ||
        !hwr_platform_make_current()) {
        snprintf(g_error, sizeof g_error,
                 "no resolvable palette-index target is present");
        return NULL;
    }
    /* Nothing has drawn into the standing target since the render that produced
     * the image, so resolving it again yields exactly what that render would
     * have produced with these effects in force. */
    g_gl.pixel_store_i(GL_UNPACK_ALIGNMENT, 1);
    g_gl.active_texture(GL_TEXTURE1);
    g_gl.bind_texture(GL_TEXTURE_2D, g_palette_texture);
    g_gl.tex_sub_image_2d(GL_TEXTURE_2D, 0, 0, 0, 256, 1,
                          GL_RGBA, GL_UNSIGNED_BYTE, palette->rgba);
    if (enhanced) {
        g_gl.bind_texture(GL_TEXTURE_2D, g_base_palette_texture);
        g_gl.tex_sub_image_2d(GL_TEXTURE_2D, 0, 0, 0, 256, 1,
                              GL_RGBA, GL_UNSIGNED_BYTE, palette->base_rgba);
        memcpy(g_base_palette_bytes, palette->base_rgba,
               sizeof g_base_palette_bytes);
        g_base_palette_valid = 1;
        hwr_truecolor_set_effects(palette);
    }
    g_gl.bind_framebuffer(GL_FRAMEBUFFER, g_fbo);
    g_gl.viewport(0, 0, (GLsizei)width, (GLsizei)height);
    g_gl.disable(GL_SCISSOR_TEST);
    g_gl.disable(GL_DEPTH_TEST);
    if (enhanced) {
        g_gl.use_program(g_truecolor_resolve_program);
        hwr_set_sampler(g_truecolor_resolve_program, "u_source", 0);
        hwr_set_sampler(g_truecolor_resolve_program, "u_palette", 1);
        hwr_uniform_1i(g_truecolor_resolve_program, "u_damage_fade",
                       g_effect_fade);
        hwr_uniform_1i(g_truecolor_resolve_program, "u_grayscale",
                       g_effect_grayscale);
        hwr_uniform_1i(g_truecolor_resolve_program, "u_dither",
                       g_truecolor_want.dither && !g_truecolor_want.neutral);
        g_gl.bind_vertex_array(g_resolve_vao);
        g_gl.active_texture(GL_TEXTURE0);
        g_gl.bind_texture(GL_TEXTURE_2D, g_color_texture);
        g_gl.active_texture(GL_TEXTURE1);
        g_gl.bind_texture(GL_TEXTURE_2D, g_base_palette_texture);
    } else {
        g_gl.use_program(g_resolve_program);
        hwr_uniform_1i(g_resolve_program, "u_debug_vis", g_debug_vis);
        g_gl.bind_vertex_array(g_resolve_vao);
        g_gl.active_texture(GL_TEXTURE0);
        g_gl.bind_texture(GL_TEXTURE_2D, g_index_texture);
        g_gl.active_texture(GL_TEXTURE1);
        g_gl.bind_texture(GL_TEXTURE_2D, g_palette_texture);
    }
    g_gl.draw_arrays(GL_TRIANGLES, 0, 3);
    g_gl.read_pixels(0, 0, (GLsizei)width, (GLsizei)height,
                     GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, g_pixels);
    return g_pixels;
}

void hwr_gl_scene_clear(void)
{
    if (g_initialized && hwr_platform_make_current())
        hwr_destroy_scene_objects();
}

void hwr_gl_set_debug_vis(int mode)
{
    g_debug_vis = mode;
}

int hwr_gl_read_scene_indices(uint8_t *out, uint32_t width, uint32_t height)
{
    size_t plane = (size_t)width * height;
    size_t i;
    if (!g_initialized || g_index_fbo == 0 || out == NULL ||
        width != g_fbo_width || height != g_fbo_height ||
        !hwr_platform_make_current())
        return 0;
    g_gl.bind_framebuffer(GL_FRAMEBUFFER, g_index_fbo);
    g_gl.read_pixels(0, 0, (GLsizei)width, (GLsizei)height,
                     GL_RED_INTEGER, GL_UNSIGNED_BYTE, out);
    g_gl.bind_framebuffer(GL_FRAMEBUFFER, 0);
    if (hwr_gl_drain_errors() != 0)
        return 0;
    for (i = 0; i < plane; i++) {
        if (out[i] != 0)
            return 1;
    }
    /* Some driver paths return an empty ReadPixels result for the integer
     * attachment; fetch the texture object directly as the fallback. */
    g_gl.active_texture(GL_TEXTURE0);
    g_gl.bind_texture(GL_TEXTURE_2D, g_index_texture);
    g_gl.get_tex_image(GL_TEXTURE_2D, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, out);
    g_gl.bind_texture(GL_TEXTURE_2D, 0);
    return hwr_gl_drain_errors() == 0;
}

void hwr_gl_shutdown(void)
{
    /* An acceptance run always ends on a verdict line, however few frames it
     * sampled. */
    if (g_truecolor_compare_frames != 0)
        fprintf(stderr, "[renderer_overhaul] truecolor compare final:"
                        " %u frames, differing frames=%u pixels=%llu"
                        " max-delta=%u%s\n",
                g_truecolor_compare_frames, g_truecolor_compare_bad_frames,
                (unsigned long long)g_truecolor_compare_bad_pixels,
                g_truecolor_compare_max_delta,
                g_truecolor_break ? " (negative self-test)" : "");
    if (g_initialized && hwr_platform_make_current()) {
        hwr_destroy_scene_objects();
        hwr_sun_destroy_objects();
        hwr_destroy_programs();
        hwr_destroy_framebuffer();
    }
    free(g_sun_static_pending);
    g_sun_static_pending = NULL;
    g_sun_static_pending_count = 0;
    g_sun_static_pending_valid = 0;
    g_sun_dynamic = NULL;
    g_sun_dynamic_count = 0;
    hwr_platform_shutdown();
    free(g_pixels);
    g_pixels = NULL;
    g_pixels_bytes = 0;
    free(g_index_seed);
    g_index_seed = NULL;
    g_index_seed_width = 0;
    g_index_seed_height = 0;
    g_index_seed_ready = 0;
    free(g_truecolor_compare_pixels);
    g_truecolor_compare_pixels = NULL;
    g_truecolor_compare_bytes = 0;
    g_truecolor_active = 0;
    g_truecolor_failed = 0;
    g_truecolor_logged = 0;
    g_truecolor_format = HWR_GL_COLOR_AUTO;
    g_effect_fade = -1;
    g_effect_grayscale = 0;
    g_base_palette_valid = 0;
    g_initialized = 0;
    memset(&g_gl, 0, sizeof g_gl);
}

/* Drains and counts pending driver errors; used to verify the one-time scene
 * uploads actually took (a silent upload failure leaves lookup tables empty). */
unsigned hwr_gl_drain_errors(void)
{
    unsigned count = 0;
    if (!g_initialized || g_gl.get_error == NULL)
        return 0;
    while (g_gl.get_error() != 0 && count < 32u)
        count++;
    return count;
}

const char *hwr_gl_last_error(void)
{
    return g_error;
}
