// Simple3DEngine - Shared internal definitions for all renderer backends.
// The public struct s3d_renderer is defined here ONCE; every backend
// (software / OpenGL 1.x / OpenGL ES 1.x) embeds it as its first member,
// so a single factory can dispatch on the type field.

#ifndef S3D_RENDERER_COMMON_H
#define S3D_RENDERER_COMMON_H

#include "../include/s3d_renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define S3D_MATRIX_STACK_SIZE 64

struct s3d_renderer {
    s3d_window* window;
    s3d_renderer_type type;

    // Fixed-function style state shared by all backends
    s3d_mat4 current_matrix;
    s3d_mat4 matrix_stack[S3D_MATRIX_STACK_SIZE];
    int32 matrix_stack_top;

    s3d_color clear_color;
    bool32 depth_test_enabled;
    bool32 blending_enabled;
    bool32 lighting_enabled;

    s3d_texture* bound_texture;   // currently bound texture (software: sampled directly)

    s3d_render_stats stats;
};

// Backend factories (called by s3d_create_renderer in engine.c)
s3d_renderer* s3d__create_renderer_software(s3d_window* window);
s3d_renderer* s3d__create_renderer_gl(s3d_window* window, s3d_renderer_type gl_type);

// Implemented by software backend, reused by the text helper below.
void s3d__draw_text_software(s3d_renderer* r, const char* text, float32 x, float32 y,
                             float32 scale, s3d_color color);

#ifdef __cplusplus
}
#endif

#endif // S3D_RENDERER_COMMON_H
