// Simple3DEngine - Input System Implementation

#include "../include/s3d_input.h"
#include <string.h>

// Key state tracking
typedef struct {
    bool32 current[128];  // Current frame state
    bool32 previous[128]; // Previous frame state
} s3d_key_state;

static s3d_key_state g_keys;
static s3d_vec2 g_mouse_pos = {0, 0};
static s3d_vec2 g_mouse_delta = {0, 0};
static bool32 g_mouse_buttons[3] = {S3D_FALSE, S3D_FALSE, S3D_FALSE};
static float32 g_mouse_wheel = 0.0f;

// Touch state (for Android)
#define MAX_TOUCHES 10
static struct {
    s3d_vec2 position;
    bool32 active;
} g_touches[MAX_TOUCHES];

void s3d_init_input(void) {
    memset(&g_keys, 0, sizeof(g_keys));
    memset(g_mouse_buttons, 0, sizeof(g_mouse_buttons));
    memset(g_touches, 0, sizeof(g_touches));
    g_mouse_pos.x = g_mouse_pos.y = 0;
    g_mouse_delta.x = g_mouse_delta.y = 0;
    g_mouse_wheel = 0.0f;
}

void s3d_cleanup_input(void) {
    // Nothing to clean up for now
}

void s3d_update_input(s3d_window* window) {
    S3D_UNUSED(window);
    
    // Copy current state to previous
    memcpy(g_keys.previous, g_keys.current, sizeof(g_keys.current));
    
    // Reset delta and wheel each frame
    g_mouse_delta.x = 0;
    g_mouse_delta.y = 0;
    g_mouse_wheel = 0.0f;
    
    // TODO: Poll events from platform layer and update states
    // This would be called by the main loop after event processing
}

bool32 s3d_is_key_down(s3d_key key) {
    if (key >= 0 && key < 128) {
        return g_keys.current[key];
    }
    return S3D_FALSE;
}

bool32 s3d_is_key_pressed(s3d_key key) {
    if (key >= 0 && key < 128) {
        return g_keys.current[key] && !g_keys.previous[key];
    }
    return S3D_FALSE;
}

bool32 s3d_is_key_released(s3d_key key) {
    if (key >= 0 && key < 128) {
        return !g_keys.current[key] && g_keys.previous[key];
    }
    return S3D_FALSE;
}

s3d_vec2 s3d_get_mouse_position(void) {
    return g_mouse_pos;
}

s3d_vec2 s3d_get_mouse_delta(void) {
    return g_mouse_delta;
}

bool32 s3d_is_mouse_down(s3d_mouse_button button) {
    if (button >= 0 && button <= 2) {
        return g_mouse_buttons[button];
    }
    return S3D_FALSE;
}

bool32 s3d_is_mouse_pressed(s3d_mouse_button button) {
    // Simplified - would need separate tracking for press detection
    return s3d_is_mouse_down(button);
}

bool32 s3d_is_mouse_released(s3d_mouse_button button) {
    S3D_UNUSED(button);
    return S3D_FALSE;
}

float32 s3d_get_mouse_wheel(void) {
    return g_mouse_wheel;
}

int32 s3d_get_touch_count(void) {
    int32 count = 0;
    for (int i = 0; i < MAX_TOUCHES; i++) {
        if (g_touches[i].active) count++;
    }
    return count;
}

s3d_vec2 s3d_get_touch_position(int32 touch_id) {
    if (touch_id >= 0 && touch_id < MAX_TOUCHES) {
        return g_touches[touch_id].position;
    }
    s3d_vec2 zero = {0, 0};
    return zero;
}

bool32 s3d_is_touch_active(int32 touch_id) {
    if (touch_id >= 0 && touch_id < MAX_TOUCHES) {
        return g_touches[touch_id].active;
    }
    return S3D_FALSE;
}

float32 s3d_get_axis_horizontal(void) {
    float32 axis = 0.0f;
    if (s3d_is_key_down(S3D_KEY_A) || s3d_is_key_down(S3D_KEY_LEFT)) axis -= 1.0f;
    if (s3d_is_key_down(S3D_KEY_D) || s3d_is_key_down(S3D_KEY_RIGHT)) axis += 1.0f;
    return axis;
}

float32 s3d_get_axis_vertical(void) {
    float32 axis = 0.0f;
    if (s3d_is_key_down(S3D_KEY_W) || s3d_is_key_down(S3D_KEY_UP)) axis += 1.0f;
    if (s3d_is_key_down(S3D_KEY_S) || s3d_is_key_down(S3D_KEY_DOWN)) axis -= 1.0f;
    return axis;
}

void s3d_reset_input(void) {
    memset(&g_keys, 0, sizeof(g_keys));
    memset(g_mouse_buttons, 0, sizeof(g_mouse_buttons));
    memset(g_touches, 0, sizeof(g_touches));
}
