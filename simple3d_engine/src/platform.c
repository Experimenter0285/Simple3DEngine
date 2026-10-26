// Simple3DEngine - Platform layer: detection, headless fallback, and X11/GLX backend.
// The SDL2 backend lives in platform_sdl.c (compiled when SDL2 is available) and takes
// precedence over everything in this file at link time via the S3D_HAS_SDL guard below.

#include "../include/s3d_platform.h"
#include "../include/simple3d_engine.h" // S3D_UNUSED
#include "s3d_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32) || defined(_WIN64)
    #define S3D_WINDOWS 1
#elif defined(__ANDROID__)
    #define S3D_ANDROID 1
#elif defined(__linux__)
    #define S3D_LINUX 1
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
    #define S3D_FREEBSD 1
#endif

s3d_platform_type s3d_get_platform(void) {
#if defined(S3D_WINDOWS)
    return S3D_PLATFORM_WINDOWS;
#elif defined(S3D_ANDROID)
    return S3D_PLATFORM_ANDROID;
#elif defined(S3D_LINUX)
    return S3D_PLATFORM_LINUX;
#elif defined(S3D_FREEBSD)
    return S3D_PLATFORM_FREEBSD;
#else
    return S3D_PLATFORM_UNKNOWN;
#endif
}

#ifndef S3D_HAS_SDL // ---- when SDL2 backend is compiled in, it provides all of the below ----

// ---------------------------------------------------------------------------
// Window structure shared by all non-SDL backends in this file
// ---------------------------------------------------------------------------
struct s3d_window {
    int32 width;
    int32 height;
    char title[256];
    bool32 should_close;
    bool32 fullscreen;
    bool32 opengl;
    uint32 renderer_hint;
    bool32 headless;      // no display available: run without a visible window
    void* pixels;         // software framebuffer owned by the backend (or NULL -> offscreen fb)
    int32 pitch;
#ifdef S3D_HAS_X11
    void* x_disp;         // Display*
    unsigned long x_win;  // Window
    void* x_gc;           // GC
    void* x_image;        // XImage*
    void* glx_ctx;        // GLXContext
#endif
};

#ifdef S3D_HAS_X11
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <GL/glx.h>

static s3d_key translate_x11_key(KeySym ks) {
    switch (ks) {
        case XK_Escape:    return S3D_KEY_ESCAPE;
        case XK_Return:    return S3D_KEY_ENTER;
        case XK_space:     return S3D_KEY_SPACE;
        case XK_Tab:       return S3D_KEY_TAB;
        case XK_BackSpace: return S3D_KEY_BACKSPACE;
        case XK_Left:      return S3D_KEY_LEFT;
        case XK_Right:     return S3D_KEY_RIGHT;
        case XK_Up:        return S3D_KEY_UP;
        case XK_Down:      return S3D_KEY_DOWN;
        case XK_F1:  return S3D_KEY_F1;  case XK_F2:  return S3D_KEY_F2;
        case XK_F3:  return S3D_KEY_F3;  case XK_F4:  return S3D_KEY_F4;
        case XK_F5:  return S3D_KEY_F5;  case XK_F6:  return S3D_KEY_F6;
        case XK_F7:  return S3D_KEY_F7;  case XK_F8:  return S3D_KEY_F8;
        case XK_F9:  return S3D_KEY_F9;  case XK_F10: return S3D_KEY_F10;
        case XK_F11: return S3D_KEY_F11; case XK_F12: return S3D_KEY_F12;
        default: break;
    }
    if (ks >= XK_a && ks <= XK_z) return (s3d_key)(S3D_KEY_A + (ks - XK_a));
    if (ks >= XK_0 && ks <= XK_9) return (s3d_key)(S3D_KEY_0 + (ks - XK_0));
    return S3D_KEY_UNKNOWN;
}

static bool32 try_create_x11(s3d_window* w, const s3d_window_params* params) {
    Display* dpy = XOpenDisplay(NULL);
    if (!dpy) return S3D_FALSE;

    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);
    Window win = XCreateSimpleWindow(dpy, root, 100, 100,
                                     (unsigned)w->width, (unsigned)w->height, 1,
                                     BlackPixel(dpy, screen), WhitePixel(dpy, screen));
    XStoreName(dpy, win, w->title);
    Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete, 1);
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | KeyReleaseMask |
                           ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                           StructureNotifyMask);
    XMapWindow(dpy, win);

    w->x_disp = dpy;
    w->x_win = win;
    w->x_gc = XCreateGC(dpy, win, 0, NULL);

    // Try to get an actual GLX context (legacy fixed-function drivers qualify here too).
    if (params->renderer_hint == S3D_RENDERER_OPENGL_1_0 ||
        params->renderer_hint == S3D_RENDERER_AUTO) {
        static int glx_attribs[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_DEPTH_SIZE, 16, None };
        XVisualInfo* vi = glXChooseVisual(dpy, screen, glx_attribs);
        if (vi) {
            GLXContext ctx = glXCreateContext(dpy, vi, NULL, True);
            if (ctx) {
                w->glx_ctx = ctx;
                w->opengl = S3D_TRUE;
            }
            XFree(vi);
        }
    }

    if (!w->opengl) {
        // Software path backing store presented with XPutImage.
        uint32* backing = (uint32*)calloc((size_t)w->width * w->height, sizeof(uint32));
        XImage* img = XCreateImage(dpy, DefaultVisual(dpy, screen),
                                   (unsigned)DefaultDepth(dpy, screen), ZPixmap, 0,
                                   (char*)backing, (unsigned)w->width, (unsigned)w->height,
                                   32, 0);
        w->x_image = img;
        w->pixels = backing;
        w->pitch = w->width * 4;
    }
    printf("Simple3DEngine: X11 window created (%dx%d, %s)\n",
           w->width, w->height, w->opengl ? "GLX context" : "software surface");
    return S3D_TRUE;
}
#endif // S3D_HAS_X11

s3d_window* s3d_create_window(const s3d_window_params* params) {
    s3d_window* w = (s3d_window*)calloc(1, sizeof(s3d_window));
    if (!w) return NULL;
    w->width = params->width;
    w->height = params->height;
    strncpy(w->title, params->title ? params->title : "Simple3DEngine", 255);
    w->fullscreen = params->fullscreen;
    w->renderer_hint = (uint32)params->renderer_hint;

#ifdef S3D_HAS_X11
    if (try_create_x11(w, params)) return w;
#endif

    // Headless fallback: lets games/tests run on machines without any display server
    // (also used for automated CI runs with S3D_HEADLESS=1).
    w->headless = S3D_TRUE;
    fprintf(stderr, "Simple3DEngine: no display found, running headless (%dx%d)\n",
            w->width, w->height);
    return w;
}

void s3d_destroy_window(s3d_window* window) {
    if (!window) return;
#ifdef S3D_HAS_X11
    if (window->x_disp) {
        Display* dpy = (Display*)window->x_disp;
        if (window->glx_ctx) glXDestroyContext(dpy, (GLXContext)window->glx_ctx);
        if (window->x_image) { XDestroyImage((XImage*)window->x_image); }
        else free(window->pixels);
        if (window->x_gc) XFreeGC(dpy, (GC)window->x_gc);
        if (window->x_win) XDestroyWindow(dpy, (Window)window->x_win);
        XCloseDisplay(dpy);
    }
#else
    free(window->pixels);
#endif
    free(window);
}

void s3d_swap_buffers(s3d_window* window) {
    if (!window) return;
#ifdef S3D_HAS_X11
    if (window->x_disp) {
        Display* dpy = (Display*)window->x_disp;
        if (window->opengl && window->glx_ctx) {
            glXSwapBuffers(dpy, (Window)window->x_win);
        } else if (window->x_image) {
            XPutImage(dpy, (Window)window->x_win, (GC)window->x_gc,
                      (XImage*)window->x_image, 0, 0, 0, 0,
                      (unsigned)window->width, (unsigned)window->height);
            XFlush(dpy);
        }
        return;
    }
#endif
    S3D_UNUSED(window); // headless: nothing to present
}

bool32 s3d__window_is_opengl(s3d_window* window) {
    return window ? window->opengl : S3D_FALSE;
}

void* s3d__window_get_pixels(s3d_window* window, int32* w, int32* h, int32* pitch) {
    if (!window || window->opengl) return NULL;
#ifdef S3D_HAS_X11
    if (window->pixels) {
        if (w) *w = window->width;
        if (h) *h = window->height;
        if (pitch) *pitch = window->pitch;
        return window->pixels;
    }
#else
    S3D_UNUSED(w); S3D_UNUSED(h); S3D_UNUSED(pitch);
#endif
    return NULL; // headless -> renderer keeps its own offscreen framebuffer
}

void s3d__window_update(s3d_window* window) { S3D_UNUSED(window); }

s3d_result s3d__window_glx_make_current(s3d_window* window) {
#ifdef S3D_HAS_X11
    if (window && window->x_disp && window->glx_ctx) {
        glXMakeCurrent((Display*)window->x_disp, (Window)window->x_win, (GLXContext)window->glx_ctx);
        return S3D_OK;
    }
#else
    S3D_UNUSED(window);
#endif
    return S3D_ERROR_PLATFORM;
}

void s3d_make_context_current(s3d_window* window) {
    s3d__window_glx_make_current(window);
}

int32 s3d_poll_events(s3d_window* window, s3d_event* events, int32 max_events) {
    if (!window || !events || max_events <= 0) return 0;
    S3D_UNUSED(window);
    int32 count = 0;

#ifdef S3D_HAS_X11
    if (window->x_disp) {
        Display* dpy = (Display*)window->x_disp;
        while (count < max_events && XPending(dpy)) {
            XEvent xe;
            XNextEvent(dpy, &xe);
            s3d_event* out = &events[count];
            memset(out, 0, sizeof(*out));
            switch (xe.type) {
                case ClientMessage:
                    out->type = S3D_EVENT_QUIT;
                    window->should_close = S3D_TRUE;
                    count++;
                    break;
                case KeyPress:
                case KeyRelease: {
                    s3d_key k = translate_x11_key(XLookupKeysym(&xe.xkey, 0));
                    if (k == S3D_KEY_ESCAPE && xe.type == KeyPress) window->should_close = S3D_TRUE;
                    out->type = (xe.type == KeyPress) ? S3D_EVENT_KEY_DOWN : S3D_EVENT_KEY_UP;
                    out->keyboard.key = k;
                    count++;
                    break;
                }
                case MotionNotify:
                    out->type = S3D_EVENT_MOUSE_MOVE;
                    out->mouse_move.x = (float32)xe.xmotion.x;
                    out->mouse_move.y = (float32)xe.xmotion.y;
                    out->mouse_move.dx = xe.xmotion.x - (int)xe.xmotion.lastx;
                    out->mouse_move.dy = xe.xmotion.y - (int)xe.xmotion.lasty;
                    count++;
                    break;
                case ButtonPress:
                case ButtonRelease:
                    out->type = (xe.type == ButtonPress) ? S3D_EVENT_MOUSE_DOWN : S3D_EVENT_MOUSE_UP;
                    out->mouse_button.button = (xe.xbutton.button == ButtonRight)  ? S3D_MOUSE_RIGHT :
                                               (xe.xbutton.button == ButtonMiddle) ? S3D_MOUSE_MIDDLE :
                                                                                      S3D_MOUSE_LEFT;
                    count++;
                    break;
                case ConfigureNotify:
                    if (xe.xconfigure.width != window->width || xe.xconfigure.height != window->height) {
                        window->width = xe.xconfigure.width;
                        window->height = xe.xconfigure.height;
                        out->type = S3D_EVENT_WINDOW_RESIZE;
                        out->resize.width = window->width;
                        out->resize.height = window->height;
                        count++;
                    }
                    break;
                default: break;
            }
        }
        for (int32 i = 0; i < count; i++) s3d__input_handle_event(&events[i]);
        return count;
    }
#endif

#ifndef S3D_HAS_X11
    S3D_UNUSED(count);
#endif

    // Headless fallback: no event source, nothing to report.
    return 0;
}

float32 s3d_platform_get_time(void) {
#if defined(S3D_WINDOWS)
    {
        static LARGE_INTEGER freq; static bool32 inited = S3D_FALSE;
        LARGE_INTEGER ctr;
        if (!inited) { QueryPerformanceFrequency(&freq); inited = S3D_TRUE; }
        QueryPerformanceCounter(&ctr);
        return (float32)((double)ctr.QuadPart / (double)freq.QuadPart);
    }
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (float32)ts.tv_sec + (float32)ts.tv_nsec * 1e-9f;
#endif
}

void s3d_sleep(uint32 milliseconds) {
#if defined(S3D_WINDOWS)
    Sleep(milliseconds);
#else
    struct timespec req;
    req.tv_sec = milliseconds / 1000;
    req.tv_nsec = (milliseconds % 1000) * 1000000L;
    nanosleep(&req, NULL);
#endif
}

bool32 s3d_window_should_close(s3d_window* window) {
    return window ? window->should_close : S3D_TRUE;
}

void s3d_set_title(s3d_window* window, const char* title) {
    if (!window || !title) return;
    strncpy(window->title, title, 255);
#ifdef S3D_HAS_X11
    if (window->x_disp) {
        XStoreName((Display*)window->x_disp, (Window)window->x_win, title);
        XFlush((Display*)window->x_disp);
    }
#endif
}

void s3d_get_window_size(s3d_window* window, int32* width, int32* height) {
    if (window) {
        if (width) *width = window->width;
        if (height) *height = window->height;
    }
}

bool32 s3d_has_touch_support(void) {
#ifdef S3D_ANDROID
    return S3D_TRUE;
#else
    return S3D_FALSE;
#endif
}

#endif // !S3D_HAS_SDL
