#ifndef S3D_PLATFORM_H
#define S3D_PLATFORM_H

#include "s3d_core.h"
#include "s3d_math.h"

#ifdef __cplusplus
extern "C" {
#endif

// Platform types
typedef enum {
    S3D_PLATFORM_WINDOWS,
    S3D_PLATFORM_LINUX,
    S3D_PLATFORM_ANDROID,
    S3D_PLATFORM_FREEBSD,
    S3D_PLATFORM_UNKNOWN,
} s3d_platform_type;

// Renderer types
typedef enum {
    S3D_RENDERER_OPENGL_1_0,      // Legacy OpenGL 1.0
    S3D_RENDERER_OPENGL_ES_1_0,   // OpenGL ES 1.0 (fixed pipeline)
    S3D_RENDERER_SOFTWARE,        // Pure software rendering
    S3D_RENDERER_AUTO,            // Auto-detect best available
} s3d_renderer_type;

// Window creation parameters
typedef struct {
    const char* title;
    int32 width;
    int32 height;
    bool32 fullscreen;
    bool32 resizable;
    s3d_renderer_type renderer_hint;
} s3d_window_params;

// Input events
typedef enum {
    S3D_EVENT_NONE = 0,
    S3D_EVENT_QUIT,
    S3D_EVENT_KEY_DOWN,
    S3D_EVENT_KEY_UP,
    S3D_EVENT_MOUSE_MOVE,
    S3D_EVENT_MOUSE_DOWN,
    S3D_EVENT_MOUSE_UP,
    S3D_EVENT_WINDOW_RESIZE,
    S3D_EVENT_TOUCH_BEGIN,
    S3D_EVENT_TOUCH_MOVE,
    S3D_EVENT_TOUCH_END,
} s3d_event_type;

// Keyboard keys
typedef enum {
    S3D_KEY_UNKNOWN = 0,
    S3D_KEY_ESCAPE,
    S3D_KEY_SPACE,
    S3D_KEY_ENTER,
    S3D_KEY_TAB,
    S3D_KEY_BACKSPACE,
    S3D_KEY_LEFT,
    S3D_KEY_RIGHT,
    S3D_KEY_UP,
    S3D_KEY_DOWN,
    S3D_KEY_A, S3D_KEY_B, S3D_KEY_C, S3D_KEY_D, S3D_KEY_E, S3D_KEY_F,
    S3D_KEY_G, S3D_KEY_H, S3D_KEY_I, S3D_KEY_J, S3D_KEY_K, S3D_KEY_L,
    S3D_KEY_M, S3D_KEY_N, S3D_KEY_O, S3D_KEY_P, S3D_KEY_Q, S3D_KEY_R,
    S3D_KEY_S, S3D_KEY_T, S3D_KEY_U, S3D_KEY_V, S3D_KEY_W, S3D_KEY_X,
    S3D_KEY_Y, S3D_KEY_Z,
    S3D_KEY_0, S3D_KEY_1, S3D_KEY_2, S3D_KEY_3, S3D_KEY_4,
    S3D_KEY_5, S3D_KEY_6, S3D_KEY_7, S3D_KEY_8, S3D_KEY_9,
    S3D_KEY_F1, S3D_KEY_F2, S3D_KEY_F3, S3D_KEY_F4, S3D_KEY_F5,
    S3D_KEY_F6, S3D_KEY_F7, S3D_KEY_F8, S3D_KEY_F9, S3D_KEY_F10,
    S3D_KEY_F11, S3D_KEY_F12,
} s3d_key;

// Mouse buttons
typedef enum {
    S3D_MOUSE_LEFT = 0,
    S3D_MOUSE_RIGHT,
    S3D_MOUSE_MIDDLE,
} s3d_mouse_button;

// Event structure
typedef struct {
    s3d_event_type type;
    union {
        struct { s3d_key key; } keyboard;
        struct { float32 x, y; int32 dx, dy; } mouse_move;
        struct { s3d_mouse_button button; } mouse_button;
        struct { int32 width, height; } resize;
        struct { float32 x, y; int32 id; } touch;
    };
} s3d_event;

// Opaque window handle
typedef struct s3d_window s3d_window;

// Platform API functions
s3d_platform_type s3d_get_platform(void);
s3d_window* s3d_create_window(const s3d_window_params* params);
void s3d_destroy_window(s3d_window* window);
void s3d_swap_buffers(s3d_window* window);
int32 s3d_poll_events(s3d_window* window, s3d_event* events, int32 max_events);
// Raw platform clock (seconds since engine-ish epoch). Named distinctly from
// the engine-level s3d_get_time(s3d_engine*) in simple3d_engine.h.
float32 s3d_platform_get_time(void);
void s3d_sleep(uint32 milliseconds);
bool32 s3d_window_should_close(s3d_window* window);
void s3d_set_title(s3d_window* window, const char* title);
void s3d_get_window_size(s3d_window* window, int32* width, int32* height);
void s3d_make_context_current(s3d_window* window);

// Touch support (for Android)
bool32 s3d_has_touch_support(void);

#ifdef __cplusplus
}
#endif

#endif // S3D_PLATFORM_H
