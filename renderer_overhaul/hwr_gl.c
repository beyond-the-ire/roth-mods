#include "hwr_gl.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#else
#include <EGL/egl.h>
#include <GL/gl.h>
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

struct hwr_gl_functions {
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
};

static struct hwr_gl_functions g_gl;
static GLuint g_fbo;
static GLuint g_color_buffer;
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
    return 1;
}

#undef HWR_LOAD_GL

static void hwr_destroy_framebuffer(void)
{
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
    if (g_initialized && hwr_platform_make_current())
        hwr_destroy_framebuffer();
    hwr_platform_shutdown();
    free(g_pixels);
    g_pixels = NULL;
    g_pixels_bytes = 0;
    g_initialized = 0;
    memset(&g_gl, 0, sizeof g_gl);
}

const char *hwr_gl_last_error(void)
{
    return g_error;
}
