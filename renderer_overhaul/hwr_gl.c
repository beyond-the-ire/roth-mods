#include "hwr_gl.h"

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
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8cd5
#endif
#ifndef GL_MAX_TEXTURE_BUFFER_SIZE
#define GL_MAX_TEXTURE_BUFFER_SIZE 0x8c2b
#endif

typedef GLenum (APIENTRY *hwr_gl_get_error_fn)(void);
typedef const GLubyte *(APIENTRY *hwr_gl_get_string_fn)(GLenum);
typedef void (APIENTRY *hwr_gl_viewport_fn)(GLint, GLint, GLsizei, GLsizei);
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
    EGLDeviceEXT devices[16];
    EGLint count = 0;
    EGLint i;

    query_devices = (hwr_egl_query_devices_fn)eglGetProcAddress("eglQueryDevicesEXT");
    get_platform_display = (hwr_egl_get_platform_display_fn)
        eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (query_devices == NULL || get_platform_display == NULL ||
        !query_devices((EGLint)(sizeof devices / sizeof devices[0]), devices, &count))
        return 0;
    for (i = 0; i < count; i++) {
        EGLDisplay display = get_platform_display(EGL_PLATFORM_DEVICE_EXT, devices[i], NULL);
        if (hwr_initialize_display(display))
            return 1;
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

    if (!hwr_initialize_display(eglGetDisplay(EGL_DEFAULT_DISPLAY)) &&
        !hwr_try_device_display()) {
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
    "layout(location=8) in uvec4 a_flat_span;\n"
    "layout(location=9) in uvec4 a_wall_source;\n"
    "layout(location=10) in uvec4 a_wall_rows;\n"
    "layout(location=11) in uvec4 a_wall_span;\n"
    "layout(location=12) in uvec2 a_wall_vertical;\n"
    "uniform ivec2 u_view_offset;\n"
    "uniform int u_eye_z;\n"
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
    "flat out uvec2 v_wall_vertical;\n"
    "noperspective out float v_wall_shade_endpoint;\n"
    "int wrap16(int v) { int w=v & 65535; return w >= 32768 ? w-65536 : w; }\n"
    "void main() {\n"
    "  int dx=wrap16(a_pos.x+u_view_offset.x);\n"
    "  int dy=wrap16(a_pos.y+u_view_offset.y);\n"
    "  float s=u_basis.x, c=u_basis.y;\n"
    "  float vx=s*float(dx)+c*float(dy);\n"
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
    "}\n";

static const char g_geometry_fragment_shader[] =
    "#version 330 core\n"
    "layout(location=0) out uint out_index;\n"
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
    "uniform ivec2 u_basis_fixed;\n"
    "uniform ivec2 u_focal_i;\n"
    "uniform ivec2 u_center_i;\n"
    "uniform vec2 u_focal;\n"
    "uniform vec2 u_center;\n"
    "uniform ivec2 u_shade_origin;\n"
    "uniform int u_flat_edge_bias;\n"
    "uniform float u_depth_scale;\n"
    "uniform int u_blend_pass;\n"
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
    "flat in uvec2 v_wall_vertical;\n"
    "noperspective in float v_wall_shade_endpoint;\n"
    "int imul_shift2(int a,int b) { return int(uint(a)*uint(b)) >> 2; }\n"
    "int signed16(uint v) { int n=int(v & 65535u); return n>=32768 ? n-65536 : n; }\n"
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
    "    int row=int(gl_FragCoord.y), column=int(gl_FragCoord.x);\n"
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
    "      if (seed>0) vertical_seed=uint(seed);\n"
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
    "int shade_row(int selected_level, bool tinted) {\n"
    "  int level=selected_level; int shr=5+level;\n"
    "  int authored=int(v_light_material.x); int light=authored;\n"
    "  if (authored!=0) light=(light+u_light_bias) & 255;\n"
    "  int axv=light==0 ? 0 : light-128;\n"
    "  int bias=8+axv;\n"
    "  int scale=light==0 ? 32 : 32-(max(axv+4,0) >> 2);\n"
    "  int transformed=int(floor(max(v_depth_fixed*u_depth_scale,0.0)*256.0));\n"
    "  int depth=(transformed >> 8) & 65535;\n"  /* the span vertex depth word: world units */
    "  bool flat_surface=(v_light_material.y & 8u)!=0u; int r;\n"
    "  if (flat_surface) {\n"
    "    if (tinted) {\n"
    /* The edge term measures from the jittered shade origin (the light
     * position); the jitter is the flicker. */
    "      int sx=int(262144.0/u_focal.x), sy=int(262144.0/u_focal.y);\n"
    "      int px=(abs(wrap16(int(gl_FragCoord.x)-u_shade_origin.x)) * sx) >> 8;\n"
    "      int py=(abs(wrap16(int(gl_FragCoord.y)-u_shade_origin.y)) * sy) >> 8;\n"
    "      int edge_depth=(depth+max(px,py)) & 65535;\n"
    "      r=(edge_depth >> shr)-bias-u_flat_edge_bias;\n"
    "      r=((r+32768) & 65535)-32768;\n"
    "      return r<=0 ? 1 : min(r,31);\n"
    "    }\n"
    "    r=(depth >> shr)-bias;\n"
    "    return clamp(((r+32768) & 65535)-32768,0,31);\n"
    "  }\n"
    /* Tinted walls carry per-corner shade words interpolated across columns,
     * matching the column rasterizer's endpoint stepping. */
    "  if (tinted) return clamp(int(v_wall_shade_endpoint) >> 8,0,31);\n"
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
    "  if (tinted || (v_light_material.y & 8u)!=0u) return row;\n"
    "  int transformed=int(floor(max(v_depth_fixed*u_depth_scale,0.0)*256.0));\n"
    "  int depth=(transformed >> 8) & 65535;\n"
    "  int q=depth/(8*(2 << selected_level));\n"
    "  int checker=(int(gl_FragCoord.x)+int(gl_FragCoord.y)) & 1;\n"
    "  return ((q & 1)==0 && checker!=0) ? max(row-1,0) : row;\n"
    "}\n"
    "uniform int u_debug_vis;\n"
    "void main() {\n"
    "  uint material=v_light_material.y;\n"
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
    "    int sx=int(gl_FragCoord.x), sy=int(gl_FragCoord.y);\n"
    "    int scolumn=int(texelFetch(u_texels,u_sky_projection_offset+sx).r);\n"
    "    scolumn=(scolumn-(u_sky_yaw << 1)) & 255;\n"
    "    int srow=u_sky_start_row+(u_sky_double_scanline!=0 ? (sy >> 1) : sy);\n"
    "    srow=clamp(srow,0,u_sky_texture_height-1);\n"
    "    uint stexel=texelFetch(u_texels,\n"
    "        u_sky_texture_offset+u_sky_source_offset+srow*256+scolumn).r;\n"
    "    out_index=u_debug_vis==1 ? 255u : stexel;\n"
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
    "uniform int u_texture_offset;\n"
    "uniform int u_source_offset;\n"
    "uniform int u_projection_offset;\n"
    "uniform int u_texture_height;\n"
    "uniform int u_start_row;\n"
    "uniform int u_yaw;\n"
    "uniform int u_double_scanline;\n"
    "uniform int u_debug_vis;\n"
    "void main() {\n"
    "  int x=int(gl_FragCoord.x), y=int(gl_FragCoord.y);\n"
    "  int column=int(texelFetch(u_texels,u_projection_offset+x).r);\n"
    "  column=(column-(u_yaw << 1)) & 255;\n"
    "  int row=u_start_row+(u_double_scanline!=0 ? (y >> 1) : y);\n"
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

static void hwr_destroy_scene_objects(void)
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

const uint32_t *hwr_gl_render_frame(uint32_t width, uint32_t height,
                                    uint32_t color)
{
    GLfloat red;
    GLfloat green;
    GLfloat blue;

    if (!g_initialized || !hwr_platform_make_current()) {
        snprintf(g_error, sizeof g_error, "the OpenGL context could not be made current");
        return NULL;
    }
    if ((g_fbo_width != width || g_fbo_height != height) &&
        !hwr_create_framebuffer(width, height))
        return NULL;

    red = (GLfloat)((color >> 16) & 0xffu) / 255.0f;
    green = (GLfloat)((color >> 8) & 0xffu) / 255.0f;
    blue = (GLfloat)(color & 0xffu) / 255.0f;
    g_gl.bind_framebuffer(GL_FRAMEBUFFER, g_fbo);
    g_gl.viewport(0, 0, (GLsizei)width, (GLsizei)height);
    g_gl.clear_color(red, green, blue, 1.0f);
    g_gl.clear(GL_COLOR_BUFFER_BIT);

    /* BGRA components with the reversed packed type put B in the low byte,
     * producing numeric 0xAARRGGBB words on a little-endian host. Rows read back
     * bottom-up (OpenGL origin); invisible for a uniform fill, but real scene
     * rendering must flip the projection or the readback. */
    g_gl.read_pixels(0, 0, (GLsizei)width, (GLsizei)height,
                     GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, g_pixels);
    return g_pixels;
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
    g_geometry_program = hwr_link_program(g_geometry_vertex_shader,
                                          g_geometry_fragment_shader);
    g_sky_program = hwr_link_program(g_resolve_vertex_shader,
                                     g_sky_fragment_shader);
    g_resolve_program = hwr_link_program(g_resolve_vertex_shader,
                                         g_resolve_fragment_shader);
    g_probe_program = hwr_link_program(g_resolve_vertex_shader,
                                       g_probe_fragment_shader);
    if (g_geometry_program == 0 || g_sky_program == 0 || g_resolve_program == 0 ||
        g_probe_program == 0) {
        hwr_destroy_scene_objects();
        return 0;
    }

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
    g_gl.vertex_attrib_i_pointer(12, 2, GL_UNSIGNED_INT, (GLsizei)stride,
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
    g_verify_store_hash = hwr_hash_bytes(texture_bytes, texture_size);
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

    if (first_run) {
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

const uint32_t *hwr_gl_scene_render(const struct hwr_gl_vertex *vertices,
                                    size_t sky_vertices,
                                    size_t opaque_vertices,
                                    size_t translucent_vertices,
                                    const uint32_t *translucent_runs,
                                    size_t translucent_run_count,
                                    const struct hwr_gl_camera *camera,
                                    const uint8_t palette_rgba[1024],
                                    uint32_t width, uint32_t height)
{
    size_t total = sky_vertices + opaque_vertices + translucent_vertices;
    /* Only ever seen on the first frame after the target is created or
     * resized, so it should be unobtrusive rather than a marker colour. */
    const GLuint clear_index[4] = { 0u, 0, 0, 0 };
    GLint blend_location;
    if (!g_initialized || !g_scene_uploaded || vertices == NULL || camera == NULL ||
        palette_rgba == NULL || total > (size_t)INT32_MAX ||
        total > (size_t)PTRDIFF_MAX / sizeof *vertices ||
        !hwr_platform_make_current()) {
        snprintf(g_error, sizeof g_error, "scene render inputs are invalid");
        return NULL;
    }
    if ((g_fbo_width != width || g_fbo_height != height) &&
        !hwr_create_framebuffer(width, height))
        return NULL;
    g_gl.pixel_store_i(GL_UNPACK_ALIGNMENT, 1);
    g_gl.active_texture(GL_TEXTURE1);
    g_gl.bind_texture(GL_TEXTURE_2D, g_palette_texture);
    g_gl.tex_sub_image_2d(GL_TEXTURE_2D, 0, 0, 0, 256, 1,
                          GL_RGBA, GL_UNSIGNED_BYTE, palette_rgba);

    g_gl.bind_vertex_array(g_scene_vao);
    g_gl.bind_buffer(GL_ARRAY_BUFFER, g_scene_vbo);
    g_gl.buffer_data(GL_ARRAY_BUFFER, (GLsizeiptr)(total * sizeof *vertices),
                     NULL, GL_STREAM_DRAW);
    g_gl.buffer_data(GL_ARRAY_BUFFER, (GLsizeiptr)(total * sizeof *vertices),
                     vertices, GL_STREAM_DRAW);
    g_gl.bind_framebuffer(GL_FRAMEBUFFER, g_index_fbo);
    g_gl.viewport(0, 0, (GLsizei)width, (GLsizei)height);
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
        /* At a map load the engine's framebuffer holds the backdrop image, so
         * void starts as that art rather than as a flat colour; the seed carries
         * the part of it the viewport covers. */
        if (!hwr_consume_index_seed(width, height))
            g_gl.clear_buffer_uiv(GL_COLOR, 0, clear_index);
        g_index_target_dirty = 0;
    }
    /* Snapshot the persisted target before anything draws: this is the
     * "previous frame" that stale-masked walls show through their holes and
     * blend their high-bit texels against. */
    g_gl.active_texture(GL_TEXTURE6);
    g_gl.bind_texture(GL_TEXTURE_2D, g_stale_copy_texture);
    g_gl.copy_tex_sub_image_2d(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
                               (GLsizei)width, (GLsizei)height);
    g_gl.clear(GL_DEPTH_BUFFER_BIT);
    hwr_bind_scene_textures();
    if (g_debug_vis < 0) {
        const char *vis = getenv("HWR_SHADE_VIS");
        g_debug_vis = vis != NULL && vis[0] >= '0' && vis[0] <= '9'
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
    hwr_uniform_1i(g_geometry_program, "u_debug_vis", g_debug_vis);
    hwr_uniform_2i(g_geometry_program, "u_view_offset",
                   camera->view_offset_x, camera->view_offset_y);
    hwr_uniform_1i(g_geometry_program, "u_eye_z", camera->eye_z);
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
    hwr_uniform_1i(g_geometry_program, "u_shade_level", camera->shade_level);
    hwr_uniform_1i(g_geometry_program, "u_tint_shade_level", camera->tint_shade_level);
    hwr_uniform_1i(g_geometry_program, "u_light_bias", camera->global_light_bias);
    hwr_uniform_1i(g_geometry_program, "u_glow_phase", camera->glow_phase);
    {
        /* Feed the sky-cap fragment branch the same inputs the retired
         * full-screen prepass used, gated on the identical draw condition
         * (valid sky texture and a viewport the projection table covers) so the
         * caps sample the skybox only when the backdrop previously would have. */
        int sky_active = (camera->sky_valid && camera->sky_texture_height != 0 &&
                          width <= 0x288u) ? 1 : 0;
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
    /* Opaque geometry overwrites the colour-only sky and wins against a
     * farther sky occluder wherever it projects. */
    g_gl.enable(GL_DEPTH_TEST);
    g_gl.depth_func(GL_LESS);
    g_gl.depth_mask(GL_TRUE);
    if (opaque_vertices != 0)
        g_gl.draw_arrays(GL_TRIANGLES, (GLint)sky_vertices,
                         (GLsizei)opaque_vertices);
    if (translucent_vertices != 0) {
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
        if (blend_location >= 0)
            g_gl.uniform_1i(blend_location, 1);
        g_gl.active_texture(GL_TEXTURE5);
        g_gl.bind_texture(GL_TEXTURE_2D, g_index_copy_texture);
        if (translucent_runs == NULL || translucent_run_count == 0) {
            g_gl.copy_tex_sub_image_2d(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
                                       (GLsizei)width, (GLsizei)height);
            g_gl.draw_arrays(GL_TRIANGLES, first,
                             (GLsizei)translucent_vertices);
        } else {
            for (run = 0; run < translucent_run_count; run++) {
                g_gl.copy_tex_sub_image_2d(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
                                           (GLsizei)width, (GLsizei)height);
                g_gl.draw_arrays(GL_TRIANGLES, first,
                                 (GLsizei)translucent_runs[run]);
                first += (GLint)translucent_runs[run];
            }
        }
        g_gl.depth_mask(GL_TRUE);
    }

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
    g_gl.read_pixels(0, 0, (GLsizei)width, (GLsizei)height,
                     GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, g_pixels);
    g_render_count++;
    if (g_verify_pending || (g_render_count & 1023u) == 0u) {
        int first_run = g_verify_pending;
        g_verify_pending = 0;
        hwr_gl_scene_self_test(first_run);
    }
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

void hwr_gl_resize(uint32_t width, uint32_t height)
{
    if (!g_initialized)
        return;
    if (width == 0 || height == 0 || width != g_fbo_width || height != g_fbo_height) {
        if (hwr_platform_make_current())
            hwr_destroy_framebuffer();
    }
}

void hwr_gl_shutdown(void)
{
    if (g_initialized && hwr_platform_make_current()) {
        hwr_destroy_scene_objects();
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
