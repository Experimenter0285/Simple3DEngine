// Simple3DEngine - SDL2 platform backend (Windows / Linux / FreeBSD / Android via SDL2-ndk)
// Compiled only when S3D_HAS_SDL is defined by the build system.

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

#ifdef S3D_HAS_SDL
#include <SDL2/SDL.h>

struct s3d_window {
    int32 width;
    int32 height;
    char title[256];
    bool32 should_close;
    bool32 fullscreen;
    bool32 opengl;
    uint32 renderer_hint;
    SDL_Window*   sdl_win;
    SDL_GLContext sdl_gl;
};

static bool32 g_sdl_initialized = S3D_FALSE;

static s3d_key translate_sdl_key(int sym) {
    switch (sym) {
        case SDLK_ESCAPE:    return S3D_KEY_ESCAPE;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:  return S3D_KEY_ENTER;
        case SDLK_SPACE:     return S3D_KEY_SPACE;
        case SDLK_TAB:       return S3D_KEY_TAB;
        case SDLK_BACKSPACE: return S3D_KEY_BACKSPACE;
        case SDLK_LEFT:      return S3D_KEY_LEFT;
        case SDLK_RIGHT:     return S3D_KEY_RIGHT;
        case SDLK_UP:        return S3D_KEY_UP;
        case SDLK_DOWN:      return S3D_KEY_DOWN;
        case SDLK_F1:  return S3D_KEY_F1;  case SDLK_F2:  return S3D_KEY_F2;
        case SDLK_F3:  return S3D_KEY_F3;  case SDLK_F4:  return S3D_KEY_F4;
        case SDLK_F5:  return S3D_KEY_F5;  case SDLK_F6:  return S3D_KEY_F6;
        case SDLK_F7:  return S3D_KEY_F7;  case SDLK_F8:  return S3D_KEY_F8;
        case SDLK_F9:  return S3D_KEY_F9;  case SDLK_F10: return S3D_KEY_F10;
        case SDLK_F11: return S3D_KEY_F11; case SDLK_F12: return S3D_KEY_F12;
        default: break;
    }
    if (sym >= SDLK_a && sym <= SDLK_z) return (s3d_key)(S3D_KEY_A + (sym - SDLK_a));
    if (sym >= SDLK_0 && sym <= SDLK_9) return (s3d_key)(S3D_KEY_0 + (sym - SDLK_0));
    return S3D_KEY_UNKNOWN;
}

s3d_window* s3d_create_window(const s3d_window_params* params) {
    s3d_window* window = (s3d_window*)calloc(1, sizeof(s3d_window));
    if (!window) return NULL;

    window->width = params->width;
    window->height = params->height;
    strncpy(window->title, params->title ? params->title : "Simple3DEngine", 255);
    window->fullscreen = params->fullscreen;
    window->should_close = S3D_FALSE;
    window->renderer_hint = (uint32)params->renderer_hint;

    if (!g_sdl_initialized) {
        Uint32 flags = SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER;
        if (SDL_Init(flags) != 0) {
            fprintf(stderr, "Simple3DEngine: SDL_Init failed: %s\n", SDL_GetError());
            free(window);
            return NULL;
        }
        g_sdl_initialized = S3D_TRUE;
    }

    Uint32 flags = SDL_WINDOW_SHOWN;
    bool32 want_gl = (params->renderer_hint == S3D_RENDERER_OPENGL_1_0 ||
                      params->renderer_hint == S3D_RENDERER_OPENGL_ES_1_0 ||
                      params->renderer_hint == S3D_RENDERER_AUTO);
    if (want_gl) flags |= SDL_WINDOW_OPENGL;
    if (params->fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (params->resizable)  flags |= SDL_WINDOW_RESIZABLE;

    window->sdl_win = SDL_CreateWindow(window->title,
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       window->width, window->height, flags);
    if (!window->sdl_win) {
        fprintf(stderr, "Simple3DEngine: SDL_CreateWindow failed: %s\n", SDL_GetError());
        free(window);
        return NULL;
    }

    if (want_gl) {
        // Ask for a legacy fixed-function context: works on OpenGL 1.x-era drivers
        // and on OpenGL ES 1.x devices (Android). No shaders required by the engine.
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
        window->sdl_gl = SDL_GL_CreateContext(window->sdl_win);
        if (window->sdl_gl) {
            window->opengl = S3D_TRUE;
            SDL_GL_SetSwapInterval(0); // old PCs: don't force vsync
        } else {
            fprintf(stderr, "Simple3DEngine: GL context failed (%s), using software path\n",
                    SDL_GetError());
        }
    }

    printf("Simple3DEngine: SDL2 window created (%dx%d, %s)\n",
           window->width, window->height,
           window->opengl ? "OpenGL context" : "software surface");
    return window;
}

void s3d_destroy_window(s3d_window* window) {
    if (!window) return;
    if (window->sdl_win) {
        if (window->sdl_gl) SDL_GL_DeleteContext(window->sdl_gl);
        SDL_DestroyWindow(window->sdl_win);
        if (g_sdl_initialized) { SDL_Quit(); g_sdl_initialized = S3D_FALSE; }
    }
    free(window);
}

void s3d_swap_buffers(s3d_window* window) {
    if (!window || !window->sdl_win) return;
    if (window->opengl && window->sdl_gl) SDL_GL_SwapWindow(window->sdl_win);
    else SDL_UpdateWindowSurface(window->sdl_win);
}

bool32 s3d__window_is_opengl(s3d_window* window) {
    return window ? window->opengl : S3D_FALSE;
}

void* s3d__window_get_pixels(s3d_window* window, int32* w, int32* h, int32* pitch) {
    if (!window || !window->sdl_win || window->opengl) return NULL;
    SDL_Surface* s = SDL_GetWindowSurface(window->sdl_win);
    if (!s) return NULL;
    if (w) *w = s->w;
    if (h) *h = s->h;
    if (pitch) *pitch = s->pitch;
    return s->pixels;
}

void s3d__window_update(s3d_window* window) { S3D_UNUSED(window); }

s3d_result s3d__window_glx_make_current(s3d_window* window) { S3D_UNUSED(window); return S3D_ERROR_PLATFORM; }

void s3d_make_context_current(s3d_window* window) {
    if (window && window->sdl_gl) SDL_GL_MakeCurrent(window->sdl_win, window->sdl_gl);
}

int32 s3d_poll_events(s3d_window* window, s3d_event* events, int32 max_events) {
    if (!window || !events || max_events <= 0) return 0;
    int32 count = 0;
    SDL_Event ev;
    while (count < max_events && SDL_PollEvent(&ev)) {
        s3d_event* out = &events[count];
        memset(out, 0, sizeof(*out));
        switch (ev.type) {
            case SDL_QUIT:
                out->type = S3D_EVENT_QUIT;
                window->should_close = S3D_TRUE;
                count++;
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                s3d_key k = translate_sdl_key(ev.key.keysym.sym);
                if (k == S3D_KEY_ESCAPE && ev.type == SDL_KEYDOWN)
                    window->should_close = S3D_TRUE; // convenience: Esc quits
                out->type = (ev.type == SDL_KEYDOWN) ? S3D_EVENT_KEY_DOWN : S3D_EVENT_KEY_UP;
                out->keyboard.key = k;
                count++;
                break;
            }
            case SDL_MOUSEMOTION:
                out->type = S3D_EVENT_MOUSE_MOVE;
                out->mouse_move.x = (float32)ev.motion.x;
                out->mouse_move.y = (float32)ev.motion.y;
                out->mouse_move.dx = ev.motion.xrel;
                out->mouse_move.dy = ev.motion.yrel;
                count++;
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                s3d_mouse_button b = S3D_MOUSE_LEFT;
                if (ev.button.button == SDL_BUTTON_MIDDLE) b = S3D_MOUSE_MIDDLE;
                if (ev.button.button == SDL_BUTTON_RIGHT)  b = S3D_MOUSE_RIGHT;
                out->type = (ev.type == SDL_MOUSEBUTTONDOWN) ? S3D_EVENT_MOUSE_DOWN : S3D_EVENT_MOUSE_UP;
                out->mouse_button.button = b;
                count++;
                break;
            }
            case SDL_FINGERDOWN:
            case SDL_FINGERUP:
            case SDL_FINGERMOTION: {
                out->type = (ev.type == SDL_FINGERDOWN) ? S3D_EVENT_TOUCH_BEGIN :
                            (ev.type == SDL_FINGERUP)   ? S3D_EVENT_TOUCH_END   : S3D_EVENT_TOUCH_MOVE;
                out->touch.x = ev.tfinger.x * (float32)window->width;
                out->touch.y = ev.tfinger.y * (float32)window->height;
                out->touch.id = (int32)(ev.tfinger.fingerId & 0xFFFF);
                if (ev.type == SDL_FINGERUP) out->touch.y = -1.0f; // deactivate marker
                count++;
                break;
            }
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    window->width = ev.window.data1;
                    window->height = ev.window.data2;
                    out->type = S3D_EVENT_WINDOW_RESIZE;
                    out->resize.width = window->width;
                    out->resize.height = window->height;
                    count++;
                }
                break;
            default:
                break;
        }
    }
    // Feed every event into the input system as well.
    for (int32 i = 0; i < count; i++) s3d__input_handle_event(&events[i]);
    return count;
}

float32 s3d_platform_get_time(void) {
    if (g_sdl_initialized) return (float32)SDL_GetTicks() * 0.001f;
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
    if (g_sdl_initialized) { SDL_Delay(milliseconds); return; }
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
    if (window->sdl_win) SDL_SetWindowTitle(window->sdl_win, title);
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

#endif // S3D_HAS_SDL
