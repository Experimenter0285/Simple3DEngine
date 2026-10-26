// Simple3DEngine - Input System Implementation
// State is fed by the platform layer through s3d__input_handle_event().

#include "../include/s3d_input.h"
#include "s3d_internal.h"
#include <string.h>

typedef struct {
    bool32 current[128];
    bool32 previous[128];
} s3d_key_state;

static s3d_key_state g_keys;
static s3d_vec2 g_mouse_pos = {0, 0};
static s3d_vec2 g_mouse_delta = {0, 0};
static bool32 g_mouse_buttons_current[3] = {S3D_FALSE, S3D_FALSE, S3D_FALSE};
static bool32 g_mouse_buttons_previous[3] = {S3D_FALSE, S3D_FALSE, S3D_FALSE};
static float32 g_mouse_wheel = 0.0f;

#define MAX_TOUCHES 10
static struct {
    s3d_vec2 position;
    bool32 active;
    bool32 just_began;
    bool32 just_ended;
} g_touches[MAX_TOUCHES];

void s3d_init_input(void) {
    memset(&g_keys, 0, sizeof(g_keys));
    memset(g_mouse_buttons_current, 0, sizeof(g_mouse_buttons_current));
    memset(g_mouse_buttons_previous, 0, sizeof(g_mouse_buttons_previous));
    memset(g_touches, 0, sizeof(g_touches));
    g_mouse_pos.x = g_mouse_pos.y = 0;
    g_mouse_delta.x = g_mouse_delta.y = 0;
    g_mouse_wheel = 0.0f;
}

void s3d_cleanup_input(void) {
    s3d_init_input();
}

// Called by the platform backend for every event it polls.
void s3d__input_handle_event(const s3d_event* e) {
    if (!e) return;
    switch (e->type) {
        case S3D_EVENT_KEY_DOWN:
            if (e->keyboard.key >= 0 && e->keyboard.key < 128)
                g_keys.current[e->keyboard.key] = S3D_TRUE;
            break;
        case S3D_EVENT_KEY_UP:
            if (e->keyboard.key >= 0 && e->keyboard.key < 128)
                g_keys.current[e->keyboard.key] = S3D_FALSE;
            break;
        case S3D_EVENT_MOUSE_MOVE:
            g_mouse_pos.x = e->mouse_move.x;
            g_mouse_pos.y = e->mouse_move.y;
            g_mouse_delta.x += (float32)e->mouse_move.dx;
            g_mouse_delta.y += (float32)e->mouse_move.dy;
            break;
        case S3D_EVENT_MOUSE_DOWN:
            if (e->mouse_button.button >= 0 && e->mouse_button.button <= 2)
                g_mouse_buttons_current[e->mouse_button.button] = S3D_TRUE;
            break;
        case S3D_EVENT_MOUSE_UP:
            if (e->mouse_button.button >= 0 && e->mouse_button.button <= 2)
                g_mouse_buttons_current[e->mouse_button.button] = S3D_FALSE;
            break;
        case S3D_EVENT_MOUSE_WHEEL:
            g_mouse_wheel += e->wheel.offset;
            break;
        case S3D_EVENT_TOUCH_BEGIN:
            if (e->touch.id >= 0 && e->touch.id < MAX_TOUCHES) {
                g_touches[e->touch.id].position.x = e->touch.x;
                g_touches[e->touch.id].position.y = e->touch.y;
                g_touches[e->touch.id].active = S3D_TRUE;
                g_touches[e->touch.id].just_began = S3D_TRUE;
            }
            break;
        case S3D_EVENT_TOUCH_MOVE:
            if (e->touch.id >= 0 && e->touch.id < MAX_TOUCHES) {
                g_touches[e->touch.id].position.x = e->touch.x;
                g_touches[e->touch.id].position.y = e->touch.y;
                g_touches[e->touch.id].active = S3D_TRUE;
            }
            break;
        case S3D_EVENT_TOUCH_END:
            if (e->touch.id >= 0 && e->touch.id < MAX_TOUCHES) {
                g_touches[e->touch.id].active = S3D_FALSE;
                g_touches[e->touch.id].just_ended = S3D_TRUE;
            }
            break;
        default:
            break;
    }
}

void s3d_update_input(s3d_window* window) {
    S3D_UNUSED(window);

    // Copy current state to previous at frame boundary.
    memcpy(g_keys.previous, g_keys.current, sizeof(g_keys.current));
    memcpy(g_mouse_buttons_previous, g_mouse_buttons_current, sizeof(g_mouse_buttons_current));

    // Per-frame accumulators reset.
    g_mouse_delta.x = 0;
    g_mouse_delta.y = 0;
    g_mouse_wheel = 0.0f;
    for (int i = 0; i < MAX_TOUCHES; i++) {
        g_touches[i].just_began = S3D_FALSE;
        g_touches[i].just_ended = S3D_FALSE;
    }
}

bool32 s3d_is_key_down(s3d_key key) {
    if (key >= 0 && key < 128) return g_keys.current[key];
    return S3D_FALSE;
}

bool32 s3d_is_key_pressed(s3d_key key) {
    if (key >= 0 && key < 128) return g_keys.current[key] && !g_keys.previous[key];
    return S3D_FALSE;
}

bool32 s3d_is_key_released(s3d_key key) {
    if (key >= 0 && key < 128) return !g_keys.current[key] && g_keys.previous[key];
    return S3D_FALSE;
}

s3d_vec2 s3d_get_mouse_position(void) { return g_mouse_pos; }
s3d_vec2 s3d_get_mouse_delta(void)   { return g_mouse_delta; }

bool32 s3d_is_mouse_down(s3d_mouse_button button) {
    if (button >= 0 && button <= 2) return g_mouse_buttons_current[button];
    return S3D_FALSE;
}

bool32 s3d_is_mouse_pressed(s3d_mouse_button button) {
    if (button >= 0 && button <= 2)
        return g_mouse_buttons_current[button] && !g_mouse_buttons_previous[button];
    return S3D_FALSE;
}

bool32 s3d_is_mouse_released(s3d_mouse_button button) {
    if (button >= 0 && button <= 2)
        return !g_mouse_buttons_current[button] && g_mouse_buttons_previous[button];
    return S3D_FALSE;
}

float32 s3d_get_mouse_wheel(void) { return g_mouse_wheel; }

int32 s3d_get_touch_count(void) {
    int32 count = 0;
    for (int i = 0; i < MAX_TOUCHES; i++)
        if (g_touches[i].active) count++;
    return count;
}

s3d_vec2 s3d_get_touch_position(int32 touch_id) {
    if (touch_id >= 0 && touch_id < MAX_TOUCHES) return g_touches[touch_id].position;
    s3d_vec2 zero = {0, 0};
    return zero;
}

bool32 s3d_is_touch_active(int32 touch_id) {
    if (touch_id >= 0 && touch_id < MAX_TOUCHES) return g_touches[touch_id].active;
    return S3D_FALSE;
}

float32 s3d_get_axis_horizontal(void) {
    float32 axis = 0.0f;
    if (s3d_is_key_down(S3D_KEY_A) || s3d_is_key_down(S3D_KEY_LEFT))  axis -= 1.0f;
    if (s3d_is_key_down(S3D_KEY_D) || s3d_is_key_down(S3D_KEY_RIGHT)) axis += 1.0f;
    return axis;
}

float32 s3d_get_axis_vertical(void) {
    float32 axis = 0.0f;
    if (s3d_is_key_down(S3D_KEY_W) || s3d_is_key_down(S3D_KEY_UP))    axis += 1.0f;
    if (s3d_is_key_down(S3D_KEY_S) || s3d_is_key_down(S3D_KEY_DOWN))  axis -= 1.0f;
    return axis;
}

void s3d_reset_input(void) {
    s3d_init_input();
}
