#ifndef S3D_RENDERER_H
#define S3D_RENDERER_H

#include "s3d_core.h"
#include "s3d_math.h"
#include "s3d_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

// Vertex structure for software rendering
typedef struct {
    float32 x, y, z;      // Position
    float32 r, g, b, a;   // Color
    float32 u, v;          // Texture coordinates
} s3d_vertex;

// Triangle primitive
typedef struct {
    s3d_vertex v1, v2, v3;
} s3d_triangle;

// Mesh structure
typedef struct {
    s3d_vertex* vertices;
    uint32 vertex_count;
    uint16* indices;
    uint32 index_count;
} s3d_mesh;

// Texture structure
typedef struct {
    uint32 id;
    int32 width;
    int32 height;
    uint8* data;  // RGBA pixel data
    bool32 loaded;
} s3d_texture;

// Camera
typedef struct {
    s3d_vec3 position;
    s3d_vec3 target;
    s3d_vec3 up;
    float32 fov;
    float32 near_plane;
    float32 far_plane;
    s3d_mat4 view_matrix;
    s3d_mat4 proj_matrix;
} s3d_camera;

// Renderer context (opaque)
typedef struct s3d_renderer s3d_renderer;

// Initialization and cleanup
s3d_renderer* s3d_create_renderer(s3d_window* window, s3d_renderer_type type);
void s3d_destroy_renderer(s3d_renderer* renderer);
s3d_renderer_type s3d_get_active_renderer_type(s3d_renderer* renderer);

// State management
void s3d_set_clear_color(s3d_renderer* renderer, s3d_color color);
void s3d_clear(s3d_renderer* renderer);
void s3d_begin_frame(s3d_renderer* renderer);
void s3d_end_frame(s3d_renderer* renderer);

// Matrix operations
void s3d_push_matrix(s3d_renderer* renderer);
void s3d_pop_matrix(s3d_renderer* renderer);
void s3d_load_matrix(s3d_renderer* renderer, s3d_mat4 matrix);
void s3d_mult_matrix(s3d_renderer* renderer, s3d_mat4 matrix);
void s3d_translate(s3d_renderer* renderer, float32 x, float32 y, float32 z);
void s3d_rotate(s3d_renderer* renderer, float32 angle_degrees, float32 x, float32 y, float32 z);
void s3d_scale(s3d_renderer* renderer, float32 x, float32 y, float32 z);

// Camera setup
void s3d_setup_3d_projection(s3d_renderer* renderer, s3d_camera* camera, float32 aspect_ratio);
void s3d_setup_2d_projection(s3d_renderer* renderer, int32 width, int32 height);

// Drawing primitives
void s3d_draw_point(s3d_renderer* renderer, s3d_vec3 pos, s3d_color color, float32 size);
void s3d_draw_line(s3d_renderer* renderer, s3d_vec3 start, s3d_vec3 end, s3d_color color, float32 width);
void s3d_draw_triangle(s3d_renderer* renderer, s3d_triangle tri, bool32 filled);
void s3d_draw_rect(s3d_renderer* renderer, s3d_rect rect, s3d_color color, bool32 filled);
void s3d_draw_circle(s3d_renderer* renderer, s3d_vec2 center, float32 radius, s3d_color color, int32 segments);

// Mesh drawing
s3d_mesh* s3d_create_mesh(const s3d_vertex* vertices, uint32 vertex_count, const uint16* indices, uint32 index_count);
void s3d_destroy_mesh(s3d_mesh* mesh);
void s3d_draw_mesh(s3d_renderer* renderer, s3d_mesh* mesh);

// Primitive mesh generators
s3d_mesh* s3d_create_cube(float32 size);
s3d_mesh* s3d_create_sphere(float32 radius, int32 slices, int32 stacks);
s3d_mesh* s3d_create_quad(float32 width, float32 height);

// Texture operations
s3d_texture* s3d_create_texture(int32 width, int32 height, const uint8* rgba_data);
void s3d_destroy_texture(s3d_texture* texture);
void s3d_bind_texture(s3d_renderer* renderer, s3d_texture* texture);
void s3d_draw_textured_rect(s3d_renderer* renderer, s3d_rect rect, s3d_texture* texture, s3d_color tint);

// Lighting (for OpenGL 1.0 fixed pipeline)
void s3d_enable_lighting(s3d_renderer* renderer, bool32 enable);
void s3d_set_light_position(s3d_renderer* renderer, int32 light_index, float32 x, float32 y, float32 z, float32 w);
void s3d_set_light_color(s3d_renderer* renderer, int32 light_index, s3d_color ambient, s3d_color diffuse, s3d_color specular);
void s3d_set_material(s3d_renderer* renderer, s3d_color ambient, s3d_color diffuse, s3d_color specular, float32 shininess);

// Depth buffer
void s3d_enable_depth_test(s3d_renderer* renderer, bool32 enable);
void s3d_clear_depth(s3d_renderer* renderer);

// Blending
void s3d_enable_blending(s3d_renderer* renderer, bool32 enable);

// Stats
typedef struct {
    uint32 draw_calls;
    uint32 triangles_rendered;
    uint32 vertices_processed;
    float32 fps;
    float32 frame_time_ms;
} s3d_render_stats;

void s3d_get_stats(s3d_renderer* renderer, s3d_render_stats* stats);

#ifdef __cplusplus
}
#endif

#endif // S3D_RENDERER_H
