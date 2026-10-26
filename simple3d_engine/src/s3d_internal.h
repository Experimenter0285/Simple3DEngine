// Simple3DEngine - Internal shared declarations (not part of public API)
#ifndef S3D_INTERNAL_H
#define S3D_INTERNAL_H

#include "../include/s3d_platform.h"
#include "../include/s3d_renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

// Implemented by the input system; called by platform backends for every event.
void s3d__input_handle_event(const s3d_event* e);

// Platform layer accessors used by renderers to present their output.
void*      s3d__window_get_pixels(s3d_window* window, int32* w, int32* h, int32* pitch); // surface pixels (RGBA8888)
void       s3d__window_update(s3d_window* window);                                        // unlock + present
bool32     s3d__window_is_opengl(s3d_window* window);                                     // GL context active?
s3d_result s3d__window_glx_make_current(s3d_window* window);                              // X11/GLX fallback path

#ifdef __cplusplus
}
#endif

#endif // S3D_INTERNAL_H
