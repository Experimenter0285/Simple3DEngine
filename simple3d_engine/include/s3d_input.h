#ifndef S3D_INPUT_H
#define S3D_INPUT_H

#include "s3d_core.h"
#include "s3d_math.h"
#include "s3d_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize input system
void s3d_init_input(void);
void s3d_cleanup_input(void);

// Update input state (call once per frame)
void s3d_update_input(s3d_window* window);

// Keyboard functions
bool32 s3d_is_key_down(s3d_key key);
bool32 s3d_is_key_pressed(s3d_key key);  // Just pressed this frame
bool32 s3d_is_key_released(s3d_key key); // Just released this frame

// Mouse functions
s3d_vec2 s3d_get_mouse_position(void);
s3d_vec2 s3d_get_mouse_delta(void);
bool32 s3d_is_mouse_down(s3d_mouse_button button);
bool32 s3d_is_mouse_pressed(s3d_mouse_button button);
bool32 s3d_is_mouse_released(s3d_mouse_button button);
float32 s3d_get_mouse_wheel(void);

// Touch functions (for Android/mobile)
int32 s3d_get_touch_count(void);
s3d_vec2 s3d_get_touch_position(int32 touch_id);
bool32 s3d_is_touch_active(int32 touch_id);

// Axis helpers (for common game controls)
float32 s3d_get_axis_horizontal(void);  // A/D or Left/Right
float32 s3d_get_axis_vertical(void);    // W/S or Up/Down

// Reset all input states
void s3d_reset_input(void);

#ifdef __cplusplus
}
#endif

#endif // S3D_INPUT_H
