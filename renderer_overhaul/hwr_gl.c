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
#ifndef GL_R32UI
#define GL_R32UI 0x8236
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8cd5
#endif
#ifndef GL_MAX_TEXTURE_BUFFER_SIZE
#define GL_MAX_TEXTURE_BUFFER_SIZE 0x8c2b
#endif

typedef GLenum (APIENTRY *hwr_gl_get_error_fn)(void);
typedef const GLubyte *(APIENTRY *hwr_gl_get_string_fn)(GLenum);
typedef void (APIENTRY *hwr_gl_viewport_fn)(GLint, GLint, GLsizei, GLsizei);
typedef void (APIENTRY *hwr_gl_scissor_fn)(GLint, GLint, GLsizei, GLsizei);
typedef void (APIENTRY *hwr_gl_clear_color_fn)(GLclampf, GLclampf, GLclampf, GLclampf);
typedef void (APIENTRY *hwr_gl_clear_fn)(GLbitfield);
typedef void (APIENTRY *hwr_gl_read_pixels_fn)(GLint, GLint, GLsizei, GLsizei,
                                               GLenum, GLenum, GLvoid *);
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
typedef void (APIENTRY *hwr_gl_polygon_offset_fn)(GLfloat, GLfloat);
typedef void (APIENTRY *hwr_gl_copy_tex_sub_image_2d_fn)(GLenum, GLint, GLint,
                                                         GLint, GLint, GLint,
                                                         GLsizei, GLsizei);
typedef void (APIENTRY *hwr_gl_pixel_store_i_fn)(GLenum, GLint);
typedef void (APIENTRY *hwr_gl_get_tex_image_fn)(GLenum, GLint, GLenum, GLenum,
                                                 GLvoid *);
typedef void (APIENTRY *hwr_gl_get_integer_v_fn)(GLenum, GLint *);

struct hwr_gl_functions {
    hwr_gl_get_error_fn get_error;
    hwr_gl_get_string_fn get_string;
    hwr_gl_viewport_fn viewport;
    hwr_gl_scissor_fn scissor;
    hwr_gl_clear_color_fn clear_color;
    hwr_gl_clear_fn clear;
    hwr_gl_read_pixels_fn read_pixels;
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
    hwr_gl_polygon_offset_fn polygon_offset;
    PFNGLCLEARBUFFERUIVPROC clear_buffer_uiv;
    PFNGLDRAWBUFFERSPROC draw_buffers;
    hwr_gl_copy_tex_sub_image_2d_fn copy_tex_sub_image_2d;
    hwr_gl_pixel_store_i_fn pixel_store_i;
    hwr_gl_get_tex_image_fn get_tex_image;
    PFNGLGETBUFFERSUBDATAPROC get_buffer_sub_data;
    hwr_gl_get_integer_v_fn get_integer_v;
    PFNGLGETUNIFORMIVPROC get_uniform_iv;
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
static int g_scene_uploaded;
/* Reference data for the sampling self-test: a rare state has been observed
 * where every lookup-table fetch returns zero (the world renders black while
 * solid-fill surfaces stay visible). The self-test splits "the uploaded data
 * is gone" from "the data is intact but sampling it fails". */
static uint8_t g_verify_colormap[0x2000];
static uint32_t g_verify_store_hash;
static size_t g_verify_store_size;
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
    HWR_LOAD_GL(polygon_offset, hwr_gl_polygon_offset_fn, "glPolygonOffset");
    HWR_LOAD_GL(clear_buffer_uiv, PFNGLCLEARBUFFERUIVPROC, "glClearBufferuiv");
    HWR_LOAD_GL(draw_buffers, PFNGLDRAWBUFFERSPROC, "glDrawBuffers");
    HWR_LOAD_GL(copy_tex_sub_image_2d, hwr_gl_copy_tex_sub_image_2d_fn, "glCopyTexSubImage2D");
    HWR_LOAD_GL(pixel_store_i, hwr_gl_pixel_store_i_fn, "glPixelStorei");
    HWR_LOAD_GL(get_tex_image, hwr_gl_get_tex_image_fn, "glGetTexImage");
    HWR_LOAD_GL(get_buffer_sub_data, PFNGLGETBUFFERSUBDATAPROC, "glGetBufferSubData");
    HWR_LOAD_GL(get_integer_v, hwr_gl_get_integer_v_fn, "glGetIntegerv");
    HWR_LOAD_GL(get_uniform_iv, PFNGLGETUNIFORMIVPROC, "glGetUniformiv");
    return 1;
}

#undef HWR_LOAD_GL

static const char g_geometry_vertex_shader[] =
    "#version 330 core\n"
    "layout(location=0) in ivec3 a_pos;\n"
    "layout(location=1) in vec2 a_wall;\n"
    "layout(location=2) in uint a_offset;\n"
    "layout(location=3) in uvec2 a_dims;\n"
    "layout(location=4) in uint a_aux;\n"
    "layout(location=5) in uvec4 a_pan_scale_flags;\n"
    "layout(location=6) in uvec4 a_light_material;\n"
    "layout(location=7) in uint a_wall_shade_endpoint;\n"
    "layout(location=15) in uint a_wall_shade_top;\n"
    "layout(location=13) in uint a_wall_tint_adjust;\n"
    "layout(location=14) in uint a_flat_taint;\n"
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
    "int wrap16(int v) { int w=v & 65535; return w >= 32768 ? w-65536 : w; }\n"
    "void main() {\n"
    "  int dx=wrap16(a_pos.x+u_view_offset.x);\n"
    "  int dy=wrap16(a_pos.y+u_view_offset.y);\n"
    "  float s=u_basis.x, c=u_basis.y;\n"
    "  float vx=s*float(dx)+c*float(dy);\n"
    "  if (u_x_flip != 0) vx=-vx;\n"
    "  float depth=s*float(dy)-c*float(dx);\n"
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
    "  v_wall_shade_endpoint=float(a_wall_shade_endpoint);\n"
    "  v_wall_shade_top=float(a_wall_shade_top);\n"
    "  v_wall_tint_adjust=a_wall_tint_adjust;\n"
    "  v_flat_taint=a_flat_taint;\n"
    "}\n";

static const char g_geometry_fragment_shader[] =
    "#version 330 core\n"
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
    /* The engine evaluates the flat shade only at the projected polygon vertices
     * and interpolates it linearly in screen space: read the CPU-computed per-
     * vertex row carried by the noperspective wall endpoint varying (stored for
     * flats in the wall high-byte format) and truncate it per fragment; flats
     * clamp to 0..31.  The per-vertex word carries the engine's 8-bit sub-row
     * fraction in its low byte, so flooring the interpolated word per fragment
     * lands a band boundary at the engine's sub-pixel height instead of snapping
     * to the whole row.
     * A triangle straddling the near plane has a vertex whose flat-shade
     * projection is garbage (clamped near the shade origin -> a spurious BRIGHT
     * row that interpolates across the triangle).  The engine never samples a row
     * there (it near-clips the polygon first), so when v_flat_taint marks such a
     * triangle, fall through to the per-pixel radial below. Direct-mapped mesh
     * flats also use the radial because they do not carry the world's
     * preprojected endpoint word. */
    "      if (v_flat_taint==0u &&"
    " (v_flat_span.w & 0x80000000u)==0u)"
    " return clamp(int(floor(v_wall_shade_endpoint/256.0)),0,31);\n"
    /* The per-pixel radial for tainted (near-clipped-corner) triangles.  The edge
     * term measures from the jittered shade origin (the light position); the
     * jitter is the flicker. */
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
    "uniform int u_debug_vis;\n"
    "uniform int u_depth_only;\n"
    "void main() {\n"
    "  uint material=v_light_material.y;\n"
    /* Portal-clipped geometry retains its original world vertices and texture
     * interpolation. Exact framebuffer-X bounds travel as dedicated 16.16
     * values, so walls, flats, meshes, and billboards share the same gate. */
    "  if (v_wall_vertical.w>v_wall_vertical.z) {\n"
    "    uint fx=uint(floor(gl_FragCoord.x*65536.0));\n"
    "    if (fx<v_wall_vertical.z || fx>=v_wall_vertical.w) discard;\n"
    "  }\n"
    /* A scheduled primary mirror is colourless but still owns depth at its
     * plane. This early exit deliberately bypasses texture masks and shading;
     * glDrawBuffers(GL_NONE) discards both outputs while depth is retained. */
    "  if (u_depth_only!=0) { out_index=0u; out_floor_id=0u; return; }\n"
    /* Opaque winner identity. World flats publish FLOOR_TAG | z as before.
     * Ordinary world walls and fixed-orientation wall art carry
     * WALL_TAG | owner-sector in v_flat_span.x. Fixed wall art is marked as an
     * overlay in v_pan_scale_flags.w and, like camera-facing floor objects,
     * deliberately publishes zero. This preserves the host identity for the
     * guarded redraw and the shadow gate across ordinary object silhouettes. */
    "  out_floor_id=(material & 8u)!=0u &&"
    " (v_flat_span.w & 0x80000000u)==0u ?"
    " (0x40000000u | (uint(int(round(v_world.z))) & 0xffffu)) :"
    " ((v_flat_span.x & 0xc0000000u)==0x80000000u &&"
    " (v_pan_scale_flags.w & 0x80u)==0u ?"
    " v_flat_span.x : 0u);\n"
    /* Exception re-draw: keep only fragments over the matching opaque owner
     * surface. A nearer object, door, or wall from another sector records zero
     * or a different id and therefore remains an occluder. */
    "  if (u_floor_exception!=0) {\n"
    "    uint fid=v_flat_span.x;\n"
    "    if (fid==0u) discard;\n"
    "    if (texelFetch(u_floor_id,ivec2(gl_FragCoord.xy),0).r != fid) discard;\n"
    "  }\n"
    /* Surface-identity probes must reveal the OPAQUE winner behind a
     * translucent surface, not the translucent surface itself.  The
     * translucent pass writes colour with depth-write off, so under any probe
     * mode (2 and up) drop translucent fragments and let the recorded index
     * stay whatever opaque geometry owns the pixel. */
    "  if (u_debug_vis>=2 && (material & 16u)!=0u) discard;\n"
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
    "    out_index=u_debug_vis==1 ? 255u : stexel;\n"
    "    return;\n"
    "  }\n"
    /* An all-corners-saturated tinted wall block (flagged on the wall-wrap
     * attribute bit 3) skips texel sampling and shading entirely and outputs the
     * map fill index, exactly like the engine's counter==4 fast path.  Only in
     * normal rendering -- every debug_vis mode falls through so texel, offset,
     * material and shade-row attribution keep working on these walls. */
    "  if (u_debug_vis==0 &&"
    " (v_light_material.w & 8u)!=0u) {\n"
    "    out_index=uint(u_fill_index) & 255u;\n"
    "    return;\n"
    "  }\n"
    "  uint texel=sample_texel();\n"
    "  if ((material & 1u)!=0u && texel==0u) discard;\n"
    /* Mode 2: the raw pre-colormap texel byte, for offline texel-vs-shade
     * mismatch attribution. */
    "  if (u_debug_vis==2) { out_index=texel & 255u; return; }\n"
    /* Modes 4 and 5 report which surface won a pixel: the low and high bytes
     * of its texture's byte offset in the store, which identifies the texture
     * uniquely. Mode 6 reports the material bits, which give the surface kind.
     * Capturing 4 and 5 together names the surface behind any pixel. */
    "  if (u_debug_vis==4) { out_index=v_offset & 255u; return; }\n"
    "  if (u_debug_vis==5) { out_index=(v_offset >> 8) & 255u; return; }\n"
    "  if (u_debug_vis==6) { out_index=material & 255u; return; }\n"
    "  if (u_debug_vis==7) { out_index=v_dims.x & 255u; return; }\n"
    "  if (u_debug_vis==8) { out_index=uint(dbg_ix) & 255u; return; }\n"
    "  if (u_debug_vis==9) { out_index=uint(dbg_iy) & 255u; return; }\n"
    /* A one-sided wall with a block-transparent texture: the engine's masked
     * writer skips index-0 texels, and no traversal ever paints behind a
     * one-sided wall, so the skipped pixel keeps what the PREVIOUS frame left
     * there.  Copying that stale pixel -- rather than discarding -- both shows
     * the engine's void smear and still occludes this frame's geometry behind
     * the wall, which the engine never draws. */
    "  if ((v_light_material.w & 4u)!=0u && texel==0u) {\n"
    "    out_index=texelFetch(u_stale,ivec2(gl_FragCoord.xy),0).r;\n"
    "    return;\n"
    "  }\n"
    "  uint mapped; uint face_flags=v_pan_scale_flags.w;\n"
    "  if ((material & 4u)!=0u) mapped=texel;\n"
    "  else if ((face_flags & 64u)!=0u && (u_glow_phase & 73)!=0)"
    " mapped=texelFetch(u_glow,ivec2(int(texel),1),0).r;\n"
    "  else { bool tinted=(face_flags & 2u)!=0u;"
    " int row=shade_row(tinted ? u_tint_shade_level : u_shade_level,tinted);\n"
    "    row=dithered_row(row,tinted ? u_tint_shade_level : u_shade_level,tinted);\n"
    "    if (u_debug_vis==1) { out_index=uint(row)|(tinted ? 64u : 0u); return; }\n"
    "    mapped=tinted ?"
    " texelFetch(u_tint,ivec2(int(texel),row),0).r :"
    " texelFetch(u_colormap,ivec2(int(texel),row),0).r; }\n"
    "  if (u_debug_vis==1) { out_index=255u; return; }\n"  /* direct/glow marker */
    "  if (u_blend_pass!=0 && (material & 16u)!=0u && (texel & 128u)!=0u) {\n"
    "    uint dst=texelFetch(u_dst,ivec2(gl_FragCoord.xy),0).r;\n"
    "    mapped=texelFetch(u_blend,ivec2(int(mapped),int(dst)),0).r;\n"
    "  }\n"
    /* The stale wall's high-bit texels blend against the same previous-frame
     * pixel its holes show, per the engine's masked writer. */
    "  if ((v_light_material.w & 4u)!=0u && (texel & 128u)!=0u) {\n"
    "    uint stale=texelFetch(u_stale,ivec2(gl_FragCoord.xy),0).r;\n"
    "    mapped=texelFetch(u_blend,ivec2(int(mapped),int(stale)),0).r;\n"
    "  }\n"
    "  out_index=mapped;\n"
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

static GLuint hwr_compile_shader(GLenum type, const char *source)
{
    GLuint shader = g_gl.create_shader(type);
    GLint status = 0;
    if (shader == 0)
        return 0;
    g_gl.shader_source(shader, 1, &source, NULL);
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

static GLuint hwr_link_program(const char *vertex_source, const char *fragment_source)
{
    GLuint vertex = hwr_compile_shader(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment;
    GLuint program;
    GLint status = 0;
    if (vertex == 0)
        return 0;
    fragment = hwr_compile_shader(GL_FRAGMENT_SHADER, fragment_source);
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

static void hwr_delete_texture(GLuint *texture)
{
    if (*texture != 0) {
        g_gl.delete_textures(1, texture);
        *texture = 0;
    }
}

/* The four shader programs are built from compile-time-constant source, so they
 * are linked once and kept alive for the context's lifetime rather than relinked
 * on every scene upload (each relink is a compile + link of two shaders). They
 * are deliberately NOT torn down with the per-scene buffers below; only the GL
 * shutdown path releases them. */
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
    g_geometry_program = 0;
    g_sky_program = 0;
    g_resolve_program = 0;
    g_probe_program = 0;
}

/* Links the four programs on first use (or after a shutdown); a no-op once they
 * are live. Returns 1 when all four are ready, 0 (with the set cleared) on a
 * link failure so the caller falls back to the solid renderer. */
static int hwr_gl_ensure_programs(void)
{
    if (g_geometry_program != 0 && g_sky_program != 0 &&
        g_resolve_program != 0 && g_probe_program != 0)
        return 1;
    hwr_destroy_programs();
    g_geometry_program = hwr_link_program(g_geometry_vertex_shader,
                                          g_geometry_fragment_shader);
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
    g_scene_uploaded = 0;
}

static void hwr_destroy_framebuffer(void)
{
    hwr_delete_texture(&g_index_texture);
    hwr_delete_texture(&g_floor_id_texture);
    hwr_delete_texture(&g_index_copy_texture);
    hwr_delete_texture(&g_stale_copy_texture);
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
    fprintf(stderr, "[renderer_overhaul] GL %s / %s\n",
            (const char *)version, (const char *)renderer);
    return 1;
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
    hwr_destroy_scene_objects();
    if (!hwr_gl_ensure_programs())
        return 0;

    g_gl.gen_buffers(1, &g_texture_buffer);
    g_gl.bind_buffer(GL_TEXTURE_BUFFER, g_texture_buffer);
    g_gl.buffer_data(GL_TEXTURE_BUFFER, (GLsizeiptr)texture_size,
                     texture_bytes, GL_STATIC_DRAW);
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
    g_gl.enable_vertex_attrib_array(7);
    g_gl.vertex_attrib_i_pointer(7, 1, GL_UNSIGNED_SHORT, (GLsizei)stride,
        (const void *)offsetof(struct hwr_gl_vertex, wall_shade_endpoint));
    g_gl.enable_vertex_attrib_array(13);
    g_gl.vertex_attrib_i_pointer(13, 1, GL_UNSIGNED_BYTE, (GLsizei)stride,
        (const void *)offsetof(struct hwr_gl_vertex, wall_tint_adjust));
    g_gl.enable_vertex_attrib_array(14);
    g_gl.vertex_attrib_i_pointer(14, 1, GL_UNSIGNED_BYTE, (GLsizei)stride,
        (const void *)offsetof(struct hwr_gl_vertex, flat_taint));
    g_gl.enable_vertex_attrib_array(15);
    g_gl.vertex_attrib_i_pointer(15, 1, GL_UNSIGNED_SHORT, (GLsizei)stride,
        (const void *)offsetof(struct hwr_gl_vertex, wall_shade_endpoint_top));
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
    g_verify_store_size = texture_size;
    g_verify_probe_offset = (int)(texture_size / 2u);
    g_verify_probe_texel = texture_bytes[texture_size / 2u];
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
    /* The final pass of the previous frame leaves the index framebuffer's
     * color attachment bound to unit 0's two-dimensional slot. Sampling
     * anything on that unit while drawing into that framebuffer is then a
     * feedback hazard, and drivers may legally return zeros for the unit's
     * fetches; clear the stale binding before the world pass. */
    g_gl.active_texture(GL_TEXTURE0);
    g_gl.bind_texture(GL_TEXTURE_2D, 0);
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

static void hwr_uniform_2f(GLuint program, const char *name, GLfloat x, GLfloat y)
{
    GLint location = g_gl.get_uniform_location(program, name);
    if (location >= 0)
        g_gl.uniform_2f(location, x, y);
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
    copy = (uint8_t *)malloc(plane);
    if (copy == NULL)
        return 0;
    memcpy(copy, indices, plane);
    free(g_index_seed);
    g_index_seed = copy;
    g_index_seed_width = width;
    g_index_seed_height = height;
    /* The target is only rewritten on a dirty frame, so ask for one. */
    g_index_target_dirty = 1;
    return 1;
}

void hwr_gl_invalidate_index_target(void)
{
    g_index_target_dirty = 1;
}

/* Lays a pending seed of the right size into the index target and drops it, so
 * later frames persist into those pixels rather than replacing them. */
static int hwr_consume_index_seed(uint32_t width, uint32_t height)
{
    if (g_index_seed == NULL || g_index_texture == 0 ||
        g_index_seed_width != width || g_index_seed_height != height)
        return 0;
    g_gl.active_texture(GL_TEXTURE0);
    g_gl.bind_texture(GL_TEXTURE_2D, g_index_texture);
    g_gl.pixel_store_i(GL_UNPACK_ALIGNMENT, 1);
    g_gl.tex_sub_image_2d(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)width, (GLsizei)height,
                          GL_RED_INTEGER, GL_UNSIGNED_BYTE, g_index_seed);
    free(g_index_seed);
    g_index_seed = NULL;
    g_index_seed_width = 0;
    g_index_seed_height = 0;
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
    static const GLenum both_buffers[2] =
        { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
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
    g_gl.draw_buffers(2, both_buffers);
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
    size_t translucent_billboard_run_count =
        view->translucent_billboard_run_count;
    int floor_exception = view->floor_exception;
    const struct hwr_gl_camera *camera = &view->camera;
    int sky_active = (camera->sky_valid &&
                      camera->sky_texture_height != 0 &&
                      camera->source_width != 0 &&
                      camera->source_height != 0 &&
                      camera->source_width <= 0x288u) ? 1 : 0;
    size_t total = sky_vertices + mirror_depth_vertices +
                   opaque_vertices + translucent_vertices;
    GLint blend_location;

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
    {
        static const GLenum both_buffers[2] =
            { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        g_gl.draw_buffers(2, both_buffers);
    }
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
        g_debug_vis = (getenv("HWR_COMPARE") == NULL &&
                       vis != NULL && vis[0] >= '0' && vis[0] <= '9')
                    ? vis[0] - '0' : 0;
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
    g_gl.use_program(g_geometry_program);
    /* Sampler assignments have been observed to come up lost on some runs
     * (every lookup then lands on unit 0 and the world renders black);
     * re-asserting them each frame costs nothing and heals that state. */
    hwr_set_sampler(g_geometry_program, "u_texels", 0);
    hwr_set_sampler(g_geometry_program, "u_colormap", 1);
    hwr_set_sampler(g_geometry_program, "u_tint", 2);
    hwr_set_sampler(g_geometry_program, "u_glow", 3);
    hwr_set_sampler(g_geometry_program, "u_blend", 4);
    hwr_set_sampler(g_geometry_program, "u_dst", 5);
    hwr_set_sampler(g_geometry_program, "u_stale", 6);
    hwr_set_sampler(g_geometry_program, "u_floor_id", 7);
    /* The world pass never samples the floor id (it writes it); keep the gate off
     * and clear unit 7 so sampling the attachment being written is impossible. */
    hwr_uniform_1i(g_geometry_program, "u_floor_exception", 0);
    g_gl.active_texture(GL_TEXTURE0 + 7);
    g_gl.bind_texture(GL_TEXTURE_2D, 0);
    g_gl.active_texture(GL_TEXTURE0);
    hwr_uniform_1i(g_geometry_program, "u_debug_vis", g_debug_vis);
    hwr_uniform_1i(g_geometry_program, "u_depth_only", 0);
    hwr_uniform_2i(g_geometry_program, "u_view_offset",
                   camera->view_offset_x, camera->view_offset_y);
    hwr_uniform_1i(g_geometry_program, "u_eye_z", camera->eye_z);
    hwr_uniform_1i(g_geometry_program, "u_x_flip", camera->x_flip != 0);
    hwr_uniform_2f(g_geometry_program, "u_basis",
                   (GLfloat)camera->yaw_sin / 16384.0f,
                   (GLfloat)camera->yaw_cos / 16384.0f);
    hwr_uniform_2i(g_geometry_program, "u_basis_fixed",
                   camera->yaw_sin, camera->yaw_cos);
    hwr_uniform_2f(g_geometry_program, "u_focal",
                   (GLfloat)camera->focal_x, (GLfloat)camera->focal_y);
    hwr_uniform_2i(g_geometry_program, "u_focal_i",
                   camera->focal_x, camera->focal_y);
    hwr_uniform_2f(g_geometry_program, "u_center",
                   (GLfloat)camera->center_x, (GLfloat)camera->horizon);
    hwr_uniform_2i(g_geometry_program, "u_center_i",
                   camera->center_x, camera->horizon);
    hwr_uniform_2i(g_geometry_program, "u_shade_origin",
                   camera->shade_origin_x, camera->shade_origin_y);
    hwr_uniform_1i(g_geometry_program, "u_flat_edge_bias", camera->flat_edge_bias);
    hwr_uniform_2f(g_geometry_program, "u_viewport", (GLfloat)width, (GLfloat)height);
    hwr_uniform_2i(g_geometry_program, "u_source_viewport",
                   camera->source_width, camera->source_height);
    hwr_uniform_2i(g_geometry_program, "u_source_focal",
                   camera->source_focal_x, camera->source_focal_y);
    hwr_uniform_2i(g_geometry_program, "u_source_center",
                   camera->source_center_x, camera->source_horizon);
    hwr_uniform_1i(g_geometry_program, "u_shade_level", camera->shade_level);
    hwr_uniform_1i(g_geometry_program, "u_tint_shade_level", camera->tint_shade_level);
    hwr_uniform_1i(g_geometry_program, "u_light_bias", camera->global_light_bias);
    hwr_uniform_1i(g_geometry_program, "u_fill_index",
                   camera->solid_fill_index);
    /* wd_project's own bias term (SMC in the engine, 8 at shade level 0),
     * scaled down with the shade level the same way its shift is. */
    hwr_uniform_1i(g_geometry_program, "u_wd_bias",
                   8 >> camera->tint_shade_level);
    hwr_uniform_1i(g_geometry_program, "u_glow_phase", camera->glow_phase);
    {
        /* Feed the sky-cap fragment branch the same inputs the retired
         * full-screen prepass used, gated on the identical draw condition
         * (valid sky texture and a viewport the projection table covers) so the
         * caps sample the skybox only when the backdrop previously would have. */
        hwr_uniform_1i(g_geometry_program, "u_sky_valid", sky_active);
        hwr_uniform_1i(g_geometry_program, "u_sky_texture_offset",
                       (GLint)camera->sky_texture_offset);
        hwr_uniform_1i(g_geometry_program, "u_sky_source_offset",
                       camera->sky_source_offset);
        hwr_uniform_1i(g_geometry_program, "u_sky_projection_offset",
                       (GLint)camera->sky_projection_offset);
        hwr_uniform_1i(g_geometry_program, "u_sky_texture_height",
                       camera->sky_texture_height);
        hwr_uniform_1i(g_geometry_program, "u_sky_start_row", camera->sky_start_row);
        hwr_uniform_1i(g_geometry_program, "u_sky_yaw", camera->sky_yaw);
        hwr_uniform_1i(g_geometry_program, "u_sky_double_scanline",
                       camera->sky_double_scanline);
        hwr_uniform_1i(g_geometry_program, "u_sky_occlusion_pass", 0);
    }
    {
        GLint scale_location = g_gl.get_uniform_location(g_geometry_program, "u_depth_scale");
        if (scale_location >= 0)
            g_gl.uniform_1f(scale_location, camera->shade_depth_scale > 0.0f
                                          ? camera->shade_depth_scale : 1.0f);
    }
    blend_location = g_gl.get_uniform_location(g_geometry_program, "u_blend_pass");
    if (blend_location >= 0)
        g_gl.uniform_1i(blend_location, 0);
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
        hwr_uniform_1i(g_geometry_program, "u_sky_occlusion_pass", 1);
        g_gl.draw_arrays(GL_TRIANGLES, 0, (GLsizei)sky_vertices);
        hwr_uniform_1i(g_geometry_program, "u_sky_occlusion_pass", 0);
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
        static const GLenum both_buffers[2] =
            { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        g_gl.enable(GL_DEPTH_TEST);
        g_gl.depth_func(GL_LESS);
        g_gl.depth_mask(GL_TRUE);
        g_gl.draw_buffers(1, no_buffers);
        hwr_uniform_1i(g_geometry_program, "u_depth_only", 1);
        g_gl.draw_arrays(GL_TRIANGLES,
                         (GLint)(sky_vertices + opaque_vertices +
                                 translucent_vertices),
                         (GLsizei)mirror_depth_vertices);
        hwr_uniform_1i(g_geometry_program, "u_depth_only", 0);
        g_gl.draw_buffers(2, both_buffers);
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
    {
        static const GLenum index_only[1] = { GL_COLOR_ATTACHMENT0 };
        g_gl.draw_buffers(1, index_only);
    }
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
                            flat_vertices != 0 || shadow_vertices != 0)) {
        size_t opaque_main = opaque_vertices >= flat_vertices
                           ? opaque_vertices - flat_vertices : opaque_vertices;
        g_gl.active_texture(GL_TEXTURE0 + 7);
        g_gl.bind_texture(GL_TEXTURE_2D, g_floor_id_texture);
        g_gl.active_texture(GL_TEXTURE0);
        hwr_uniform_1i(g_geometry_program, "u_floor_exception", 1);
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
        hwr_uniform_1i(g_geometry_program, "u_floor_exception", 0);
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
        /* The opaque/shadow exception block above is allowed to be absent in a
         * scene containing only translucent pickups. Bind the floor identity
         * target independently for this path instead of inheriting unit 7. */
        if (floor_exception && translucent_billboard_run_count != 0) {
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
                 * result. */
                if (floor_exception && translucent_run_flags != NULL &&
                    (translucent_run_flags[run] &
                     HWR_GL_TRANSLUCENT_RUN_FLOOR_EXCEPTION) != 0) {
                    hwr_uniform_1i(g_geometry_program, "u_floor_exception", 1);
                    g_gl.disable(GL_DEPTH_TEST);
                    g_gl.draw_arrays(GL_TRIANGLES, first,
                                     (GLsizei)translucent_runs[run]);
                    hwr_uniform_1i(g_geometry_program, "u_floor_exception", 0);
                    g_gl.enable(GL_DEPTH_TEST);
                    g_gl.depth_func(GL_LEQUAL);
                }
                first += (GLint)translucent_runs[run];
            }
        }
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
    size_t translucent_billboard_runs = 0;

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
        view->translucent_billboard_run_count > translucent_main_runs)
        return 0;
    if (view->translucent_run_count != 0) {
        if (view->translucent_runs == NULL ||
            view->translucent_run_flags == NULL)
            return 0;
        for (run = 0; run < view->translucent_run_count; run++) {
            size_t count = view->translucent_runs[run];
            uint8_t flags = view->translucent_run_flags[run];
            if ((flags & (uint8_t)~HWR_GL_TRANSLUCENT_RUN_FLOOR_EXCEPTION) != 0 ||
                (run >= translucent_main_runs && flags != 0))
                return 0;
            if (run < translucent_main_runs &&
                (flags & HWR_GL_TRANSLUCENT_RUN_FLOOR_EXCEPTION) != 0)
                translucent_billboard_runs++;
            if (count > (size_t)INT32_MAX ||
                run_vertices > SIZE_MAX - count)
                return 0;
            run_vertices += count;
        }
        if (run_vertices != view->translucent_vertices ||
            translucent_billboard_runs !=
                view->translucent_billboard_run_count)
            return 0;
    }
    if (view->clip_left != view->clip_right &&
        (view->clip_left < 0 || view->clip_right <= view->clip_left ||
         (uint32_t)view->clip_right > width))
        return 0;
    return 1;
}

const uint32_t *hwr_gl_scene_render(const struct hwr_gl_scene_view *views,
                                    size_t view_count,
                                    const uint8_t palette_rgba[1024],
                                    uint32_t width, uint32_t height)
{
    size_t view_index;
    uint64_t perf_t0 = 0;
    uint64_t perf_setup = 0;
    uint64_t perf_views = 0;
    uint64_t perf_resolve = 0;
    static uint64_t perf_setup_sum;
    static uint64_t perf_views_sum;
    static uint64_t perf_resolve_sum;
    static uint64_t perf_readback_sum;
    static uint64_t perf_total_sum;
    static size_t perf_view_sum;
    static size_t perf_copy_sum;
    static unsigned perf_samples;
    static int perf_enabled = -1;
    size_t full_copy_count = 1u; /* previous-frame stale target */
    /* Only reached where the target has no starting art -- the frame after it
     * is created or resized, or a backdrop repaint with no usable image -- so
     * it should be unobtrusive rather than a marker colour. */
    const GLuint clear_index[4] = { 0u, 0, 0, 0 };

    if (!g_initialized || !g_scene_uploaded || views == NULL ||
        view_count == 0 || view_count > (size_t)INT32_MAX ||
        palette_rgba == NULL || width == 0 || height == 0 ||
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

    g_gl.pixel_store_i(GL_UNPACK_ALIGNMENT, 1);
    g_gl.active_texture(GL_TEXTURE1);
    g_gl.bind_texture(GL_TEXTURE_2D, g_palette_texture);
    g_gl.tex_sub_image_2d(GL_TEXTURE_2D, 0, 0, 0, 256, 1,
                          GL_RGBA, GL_UNSIGNED_BYTE, palette_rgba);
    g_gl.bind_framebuffer(GL_FRAMEBUFFER, g_index_fbo);
    g_gl.viewport(0, 0, (GLsizei)width, (GLsizei)height);
    g_gl.disable(GL_SCISSOR_TEST);
    {
        static const GLenum both_buffers[2] =
            { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        g_gl.draw_buffers(2, both_buffers);
    }
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

    for (view_index = 0; view_index < view_count; view_index++) {
        if (!hwr_gl_render_view(&views[view_index], width, height))
            return NULL;
    }
    if (perf_enabled)
        perf_views = hwr_perf_now_ns();

    g_gl.disable(GL_SCISSOR_TEST);
    g_gl.bind_framebuffer(GL_FRAMEBUFFER, g_fbo);
    g_gl.viewport(0, 0, (GLsizei)width, (GLsizei)height);
    g_gl.disable(GL_DEPTH_TEST);
    g_gl.use_program(g_resolve_program);
    hwr_uniform_1i(g_resolve_program, "u_debug_vis", g_debug_vis);
    g_gl.bind_vertex_array(g_resolve_vao);
    g_gl.active_texture(GL_TEXTURE0);
    g_gl.bind_texture(GL_TEXTURE_2D, g_index_texture);
    g_gl.active_texture(GL_TEXTURE1);
    g_gl.bind_texture(GL_TEXTURE_2D, g_palette_texture);
    g_gl.draw_arrays(GL_TRIANGLES, 0, 3);
    if (perf_enabled)
        perf_resolve = hwr_perf_now_ns();
    g_gl.read_pixels(0, 0, (GLsizei)width, (GLsizei)height,
                     GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, g_pixels);
    if (perf_enabled) {
        uint64_t done = hwr_perf_now_ns();
        perf_setup_sum += perf_setup - perf_t0;
        perf_views_sum += perf_views - perf_setup;
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
                    " setup=%.2fms views-submit=%.2fms"
                    " resolve-submit=%.2fms gpu+readback=%.2fms"
                    " total=%.2fms views=%.1f full-copies=%.1f\n",
                    width, height, perf_samples,
                    perf_setup_sum / divisor,
                    perf_views_sum / divisor,
                    perf_resolve_sum / divisor,
                    perf_readback_sum / divisor,
                    perf_total_sum / divisor,
                    (double)perf_view_sum / perf_samples,
                    (double)perf_copy_sum / perf_samples);
            perf_setup_sum = 0;
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

const uint32_t *hwr_gl_scene_resolve(const uint8_t palette_rgba[1024],
                                     uint32_t width, uint32_t height)
{
    if (!g_initialized || !g_scene_uploaded || palette_rgba == NULL ||
        width == 0 || height == 0 || g_fbo_width != width ||
        g_fbo_height != height || g_pixels == NULL ||
        !hwr_platform_make_current()) {
        snprintf(g_error, sizeof g_error,
                 "no resolvable palette-index target is present");
        return NULL;
    }
    /* Nothing has drawn into the index attachment since the render that
     * produced the standing image, so resolving it again through a different
     * palette yields exactly what that render would have produced with this
     * palette. */
    g_gl.pixel_store_i(GL_UNPACK_ALIGNMENT, 1);
    g_gl.active_texture(GL_TEXTURE1);
    g_gl.bind_texture(GL_TEXTURE_2D, g_palette_texture);
    g_gl.tex_sub_image_2d(GL_TEXTURE_2D, 0, 0, 0, 256, 1,
                          GL_RGBA, GL_UNSIGNED_BYTE, palette_rgba);
    g_gl.bind_framebuffer(GL_FRAMEBUFFER, g_fbo);
    g_gl.viewport(0, 0, (GLsizei)width, (GLsizei)height);
    g_gl.disable(GL_SCISSOR_TEST);
    g_gl.disable(GL_DEPTH_TEST);
    g_gl.use_program(g_resolve_program);
    hwr_uniform_1i(g_resolve_program, "u_debug_vis", g_debug_vis);
    g_gl.bind_vertex_array(g_resolve_vao);
    g_gl.active_texture(GL_TEXTURE0);
    g_gl.bind_texture(GL_TEXTURE_2D, g_index_texture);
    g_gl.active_texture(GL_TEXTURE1);
    g_gl.bind_texture(GL_TEXTURE_2D, g_palette_texture);
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
    if (g_initialized && hwr_platform_make_current()) {
        hwr_destroy_scene_objects();
        hwr_destroy_programs();
        hwr_destroy_framebuffer();
    }
    hwr_platform_shutdown();
    free(g_pixels);
    g_pixels = NULL;
    g_pixels_bytes = 0;
    free(g_index_seed);
    g_index_seed = NULL;
    g_index_seed_width = 0;
    g_index_seed_height = 0;
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
