#ifndef S3D_MATH_H
#define S3D_MATH_H

#include "s3d_core.h"
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

// 2D Vector
typedef struct {
    float32 x, y;
} s3d_vec2;

// 3D Vector
typedef struct {
    float32 x, y, z;
} s3d_vec3;

// 4D Vector
typedef struct {
    float32 x, y, z, w;
} s3d_vec4;

// Matrix 4x4 (column-major for OpenGL)
typedef struct {
    float32 m[16];
} s3d_mat4;

// Quaternion
typedef struct {
    float32 x, y, z, w;
} s3d_quat;

// Color RGBA
typedef struct {
    float32 r, g, b, a;
} s3d_color;

// Rectangle
typedef struct {
    float32 x, y, width, height;
} s3d_rect;

// Math functions
static inline s3d_vec2 s3d_vec2_create(float32 x, float32 y) {
    s3d_vec2 v = {x, y};
    return v;
}

static inline s3d_vec3 s3d_vec3_create(float32 x, float32 y, float32 z) {
    s3d_vec3 v = {x, y, z};
    return v;
}

static inline s3d_vec4 s3d_vec4_create(float32 x, float32 y, float32 z, float32 w) {
    s3d_vec4 v = {x, y, z, w};
    return v;
}

static inline float32 s3d_vec2_length(s3d_vec2 v) {
    return sqrtf(v.x * v.x + v.y * v.y);
}

static inline float32 s3d_vec3_length(s3d_vec3 v) {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

static inline float32 s3d_vec3_length_sq(s3d_vec3 v) {
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

static inline s3d_vec3 s3d_vec3_normalize(s3d_vec3 v) {
    float32 len = s3d_vec3_length(v);
    if (len > 0.0f) {
        v.x /= len;
        v.y /= len;
        v.z /= len;
    }
    return v;
}

static inline float32 s3d_vec3_dot(s3d_vec3 a, s3d_vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline s3d_vec3 s3d_vec3_cross(s3d_vec3 a, s3d_vec3 b) {
    s3d_vec3 result;
    result.x = a.y * b.z - a.z * b.y;
    result.y = a.z * b.x - a.x * b.z;
    result.z = a.x * b.y - a.y * b.x;
    return result;
}

static inline s3d_vec3 s3d_vec3_add(s3d_vec3 a, s3d_vec3 b) {
    s3d_vec3 result = {a.x + b.x, a.y + b.y, a.z + b.z};
    return result;
}

static inline s3d_vec3 s3d_vec3_sub(s3d_vec3 a, s3d_vec3 b) {
    s3d_vec3 result = {a.x - b.x, a.y - b.y, a.z - b.z};
    return result;
}

static inline s3d_vec3 s3d_vec3_scale(s3d_vec3 v, float32 scale) {
    s3d_vec3 result = {v.x * scale, v.y * scale, v.z * scale};
    return result;
}

// Matrix operations
static inline s3d_mat4 s3d_mat4_identity(void) {
    s3d_mat4 m = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    return m;
}

static inline s3d_mat4 s3d_mat4_translate(float32 x, float32 y, float32 z) {
    s3d_mat4 m = s3d_mat4_identity();
    m.m[12] = x;
    m.m[13] = y;
    m.m[14] = z;
    return m;
}

static inline s3d_mat4 s3d_mat4_scale(float32 x, float32 y, float32 z) {
    s3d_mat4 m = {{0}};
    m.m[0] = x;
    m.m[5] = y;
    m.m[10] = z;
    m.m[15] = 1;
    return m;
}

static inline s3d_mat4 s3d_mat4_rotate_x(float32 angle) {
    s3d_mat4 m = s3d_mat4_identity();
    float32 c = cosf(angle);
    float32 s = sinf(angle);
    m.m[5] = c;
    m.m[6] = s;
    m.m[9] = -s;
    m.m[10] = c;
    return m;
}

static inline s3d_mat4 s3d_mat4_rotate_y(float32 angle) {
    s3d_mat4 m = s3d_mat4_identity();
    float32 c = cosf(angle);
    float32 s = sinf(angle);
    m.m[0] = c;
    m.m[2] = -s;
    m.m[8] = s;
    m.m[10] = c;
    return m;
}

static inline s3d_mat4 s3d_mat4_rotate_z(float32 angle) {
    s3d_mat4 m = s3d_mat4_identity();
    float32 c = cosf(angle);
    float32 s = sinf(angle);
    m.m[0] = c;
    m.m[1] = s;
    m.m[4] = -s;
    m.m[5] = c;
    return m;
}

static inline s3d_mat4 s3d_mat4_multiply(s3d_mat4 a, s3d_mat4 b) {
    s3d_mat4 result = {{0}};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result.m[i * 4 + j] = 
                a.m[i * 4 + 0] * b.m[0 * 4 + j] +
                a.m[i * 4 + 1] * b.m[1 * 4 + j] +
                a.m[i * 4 + 2] * b.m[2 * 4 + j] +
                a.m[i * 4 + 3] * b.m[3 * 4 + j];
        }
    }
    return result;
}

static inline s3d_mat4 s3d_mat4_perspective(float32 fov, float32 aspect, float32 near_plane, float32 far_plane) {
    s3d_mat4 m = {{0}};
    float32 tan_half_fov = tanf(fov / 2.0f);
    m.m[0] = 1.0f / (aspect * tan_half_fov);
    m.m[5] = 1.0f / tan_half_fov;
    m.m[10] = -(far_plane + near_plane) / (far_plane - near_plane);
    m.m[11] = -1.0f;
    m.m[14] = -(2.0f * far_plane * near_plane) / (far_plane - near_plane);
    return m;
}

static inline s3d_mat4 s3d_mat4_ortho(float32 left, float32 right, float32 bottom, float32 top, float32 near_plane, float32 far_plane) {
    s3d_mat4 m = {{0}};
    m.m[0] = 2.0f / (right - left);
    m.m[5] = 2.0f / (top - bottom);
    m.m[10] = -2.0f / (far_plane - near_plane);
    m.m[12] = -(right + left) / (right - left);
    m.m[13] = -(top + bottom) / (top - bottom);
    m.m[14] = -(far_plane + near_plane) / (far_plane - near_plane);
    m.m[15] = 1.0f;
    return m;
}

static inline s3d_mat4 s3d_mat4_look_at(s3d_vec3 eye, s3d_vec3 center, s3d_vec3 up) {
    s3d_vec3 f = s3d_vec3_normalize(s3d_vec3_sub(center, eye));
    s3d_vec3 s = s3d_vec3_normalize(s3d_vec3_cross(f, up));
    s3d_vec3 u = s3d_vec3_cross(s, f);
    
    s3d_mat4 m = {{0}};
    m.m[0] = s.x;
    m.m[4] = s.y;
    m.m[8] = s.z;
    m.m[1] = u.x;
    m.m[5] = u.y;
    m.m[9] = u.z;
    m.m[2] = -f.x;
    m.m[6] = -f.y;
    m.m[10] = -f.z;
    m.m[12] = -s3d_vec3_dot(s, eye);
    m.m[13] = -s3d_vec3_dot(u, eye);
    m.m[14] = s3d_vec3_dot(f, eye);
    m.m[15] = 1.0f;
    return m;
}

// Color helpers
static inline s3d_color s3d_color_rgba(float32 r, float32 g, float32 b, float32 a) {
    s3d_color c = {r, g, b, a};
    return c;
}

static inline s3d_color s3d_color_rgb(float32 r, float32 g, float32 b) {
    return s3d_color_rgba(r, g, b, 1.0f);
}

#ifdef __cplusplus
}
#endif

#endif // S3D_MATH_H
