// Simple3DEngine - Software Renderer Implementation
// Pure software rendering for old PCs without OpenGL support

#include "../include/s3d_renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Framebuffer pixel format: RGBA8888
typedef struct {
    uint32* pixels;  // Actually storing as uint32 (packed RGBA)
    int32 width;
    int32 height;
    float32* depth_buffer;
} s3d_framebuffer;

// Internal renderer structure
struct s3d_renderer {
    s3d_window* window;
    s3d_renderer_type type;
    
    // Software rendering specific
    s3d_framebuffer framebuffer;
    
    // Current state
    s3d_mat4 current_matrix;
    s3d_mat4 matrix_stack[32];
    int32 matrix_stack_top;
    
    s3d_color clear_color;
    bool32 depth_test_enabled;
    bool32 blending_enabled;
    bool32 lighting_enabled;
    
    // Stats
    s3d_render_stats stats;
    float32 last_frame_time;
};

// Helper: Pack RGBA into uint32
static inline uint32 pack_rgba(float32 r, float32 g, float32 b, float32 a) {
    uint8 ri = (uint8)(r * 255.0f);
    uint8 gi = (uint8)(g * 255.0f);
    uint8 bi = (uint8)(b * 255.0f);
    uint8 ai = (uint8)(a * 255.0f);
    return (ai << 24) | (bi << 16) | (gi << 8) | ri;
}

// Create renderer
s3d_renderer* s3d_create_renderer(s3d_window* window, s3d_renderer_type type) {
    s3d_renderer* renderer = (s3d_renderer*)calloc(1, sizeof(s3d_renderer));
    if (!renderer) return NULL;
    
    renderer->window = window;
    renderer->type = type;
    renderer->matrix_stack_top = 0;
    renderer->current_matrix = s3d_mat4_identity();
    renderer->clear_color = s3d_color_rgb(0.1f, 0.1f, 0.15f);
    renderer->depth_test_enabled = S3D_TRUE;
    renderer->blending_enabled = S3D_FALSE;
    renderer->lighting_enabled = S3D_FALSE;
    
    // Initialize framebuffer
    int32 width, height;
    s3d_get_window_size(window, &width, &height);
    renderer->framebuffer.width = width;
    renderer->framebuffer.height = height;
    renderer->framebuffer.pixels = (uint32*)calloc(width * height, sizeof(uint32));
    renderer->framebuffer.depth_buffer = (float32*)malloc(width * height * sizeof(float32));
    
    printf("Simple3DEngine: Software renderer initialized (%dx%d)\n", width, height);
    
    return renderer;
}

void s3d_destroy_renderer(s3d_renderer* renderer) {
    if (renderer) {
        free(renderer->framebuffer.pixels);
        free(renderer->framebuffer.depth_buffer);
        free(renderer);
    }
}

s3d_renderer_type s3d_get_active_renderer_type(s3d_renderer* renderer) {
    return renderer ? renderer->type : S3D_RENDERER_SOFTWARE;
}

// State management
void s3d_set_clear_color(s3d_renderer* renderer, s3d_color color) {
    if (renderer) renderer->clear_color = color;
}

void s3d_clear(s3d_renderer* renderer) {
    if (!renderer) return;
    
    uint32 clear_pixel = pack_rgba(renderer->clear_color.r, 
                                    renderer->clear_color.g,
                                    renderer->clear_color.b,
                                    renderer->clear_color.a);
    
    int32 total_pixels = renderer->framebuffer.width * renderer->framebuffer.height;
    for (int i = 0; i < total_pixels; i++) {
        renderer->framebuffer.pixels[i] = clear_pixel;
        renderer->framebuffer.depth_buffer[i] = 1.0f;
    }
}

void s3d_begin_frame(s3d_renderer* renderer) {
    if (!renderer) return;
    renderer->stats.draw_calls = 0;
    renderer->stats.triangles_rendered = 0;
    renderer->stats.vertices_processed = 0;
}

void s3d_end_frame(s3d_renderer* renderer) {
    S3D_UNUSED(renderer);
    // TODO: Blit framebuffer to window surface
}

// Matrix operations
void s3d_push_matrix(s3d_renderer* renderer) {
    if (!renderer || renderer->matrix_stack_top >= 31) return;
    renderer->matrix_stack[renderer->matrix_stack_top++] = renderer->current_matrix;
}

void s3d_pop_matrix(s3d_renderer* renderer) {
    if (!renderer || renderer->matrix_stack_top <= 0) return;
    renderer->current_matrix = renderer->matrix_stack[--renderer->matrix_stack_top];
}

void s3d_load_matrix(s3d_renderer* renderer, s3d_mat4 matrix) {
    if (renderer) renderer->current_matrix = matrix;
}

void s3d_mult_matrix(s3d_renderer* renderer, s3d_mat4 matrix) {
    if (renderer) {
        renderer->current_matrix = s3d_mat4_multiply(renderer->current_matrix, matrix);
    }
}

void s3d_translate(s3d_renderer* renderer, float32 x, float32 y, float32 z) {
    s3d_mat4 trans = s3d_mat4_translate(x, y, z);
    s3d_mult_matrix(renderer, trans);
}

void s3d_rotate(s3d_renderer* renderer, float32 angle_degrees, float32 x, float32 y, float32 z) {
    float32 angle_rad = angle_degrees * 3.14159265f / 180.0f;
    // Simplified: only Y-axis rotation for now
    S3D_UNUSED(x); S3D_UNUSED(z);
    s3d_mat4 rot = s3d_mat4_rotate_y(angle_rad);
    if (fabsf(y) > 0.5f) {
        s3d_mult_matrix(renderer, rot);
    }
}

void s3d_scale(s3d_renderer* renderer, float32 x, float32 y, float32 z) {
    s3d_mat4 scale_mat = s3d_mat4_scale(x, y, z);
    s3d_mult_matrix(renderer, scale_mat);
}

// Camera setup
void s3d_setup_3d_projection(s3d_renderer* renderer, s3d_camera* camera, float32 aspect_ratio) {
    if (!renderer || !camera) return;
    
    camera->proj_matrix = s3d_mat4_perspective(camera->fov, aspect_ratio, 
                                                camera->near_plane, camera->far_plane);
    camera->view_matrix = s3d_mat4_look_at(camera->position, camera->target, camera->up);
}

void s3d_setup_2d_projection(s3d_renderer* renderer, int32 width, int32 height) {
    if (!renderer) return;
    
    s3d_mat4 ortho = s3d_mat4_ortho(0, (float32)width, (float32)height, 0, -1, 1);
    s3d_load_matrix(renderer, ortho);
}

// Drawing primitives - Software rasterization
static void draw_pixel(s3d_renderer* renderer, int32 x, int32 y, float32 depth, uint32 color) {
    if (!renderer) return;
    if (x < 0 || x >= renderer->framebuffer.width) return;
    if (y < 0 || y >= renderer->framebuffer.height) return;
    
    int32 idx = y * renderer->framebuffer.width + x;
    
    // Depth test
    if (renderer->depth_test_enabled && depth >= renderer->framebuffer.depth_buffer[idx]) {
        return;
    }
    
    renderer->framebuffer.pixels[idx] = color;
    renderer->framebuffer.depth_buffer[idx] = depth;
}

static void transform_vertex(s3d_renderer* renderer, s3d_vec3 in_pos, float32* out_x, float32* out_y, float32* out_depth) {
    if (!renderer) return;
    
    s3d_mat4 m = renderer->current_matrix;
    float32 w = m.m[3] * in_pos.x + m.m[7] * in_pos.y + m.m[11] * in_pos.z + m.m[15];
    if (w == 0) w = 1;
    
    float32 clip_x = m.m[0] * in_pos.x + m.m[4] * in_pos.y + m.m[8] * in_pos.z + m.m[12];
    float32 clip_y = m.m[1] * in_pos.x + m.m[5] * in_pos.y + m.m[9] * in_pos.z + m.m[13];
    float32 clip_z = m.m[2] * in_pos.x + m.m[6] * in_pos.y + m.m[10] * in_pos.z + m.m[14];
    
    *out_x = clip_x / w;
    *out_y = clip_y / w;
    *out_depth = clip_z / w;
}

void s3d_draw_point(s3d_renderer* renderer, s3d_vec3 pos, s3d_color color, float32 size) {
    if (!renderer) return;
    
    float32 x, y, depth;
    transform_vertex(renderer, pos, &x, &y, &depth);
    
    // Convert from [-1,1] to screen coords
    int32 sx = (int32)((x + 1.0f) * 0.5f * renderer->framebuffer.width);
    int32 sy = (int32)((1.0f - y) * 0.5f * renderer->framebuffer.height);
    
    uint32 packed = pack_rgba(color.r, color.g, color.b, color.a);
    
    // Draw simple point
    draw_pixel(renderer, sx, sy, depth, packed);
    
    renderer->stats.draw_calls++;
    renderer->stats.vertices_processed++;
}

void s3d_draw_line(s3d_renderer* renderer, s3d_vec3 start, s3d_vec3 end, s3d_color color, float32 width) {
    if (!renderer) return;
    
    float32 x1, y1, d1, x2, y2, d2;
    transform_vertex(renderer, start, &x1, &y1, &d1);
    transform_vertex(renderer, end, &x2, &y2, &d2);
    
    int32 sx1 = (int32)((x1 + 1.0f) * 0.5f * renderer->framebuffer.width);
    int32 sy1 = (int32)((1.0f - y1) * 0.5f * renderer->framebuffer.height);
    int32 sx2 = (int32)((x2 + 1.0f) * 0.5f * renderer->framebuffer.width);
    int32 sy2 = (int32)((1.0f - y2) * 0.5f * renderer->framebuffer.height);
    
    uint32 packed = pack_rgba(color.r, color.g, color.b, color.a);
    
    // Bresenham's line algorithm
    int32 dx = abs(sx2 - sx1);
    int32 dy = abs(sy2 - sy1);
    int32 stepx = (sx1 < sx2) ? 1 : -1;
    int32 stepy = (sy1 < sy2) ? 1 : -1;
    int32 err = dx - dy;
    
    while (1) {
        draw_pixel(renderer, sx1, sy1, d1, packed);
        if (sx1 == sx2 && sy1 == sy2) break;
        int32 e2 = 2 * err;
        if (e2 > -dy) { err -= dy; sx1 += stepx; }
        if (e2 < dx) { err += dx; sy1 += stepy; }
    }
    
    S3D_UNUSED(width);
    renderer->stats.draw_calls++;
}

void s3d_draw_triangle(s3d_renderer* renderer, s3d_triangle tri, bool32 filled) {
    if (!renderer) return;
    
    float32 x1, y1, d1, x2, y2, d2, x3, y3, d3;
    transform_vertex(renderer, s3d_vec3_create(tri.v1.x, tri.v1.y, tri.v1.z), &x1, &y1, &d1);
    transform_vertex(renderer, s3d_vec3_create(tri.v2.x, tri.v2.y, tri.v2.z), &x2, &y2, &d2);
    transform_vertex(renderer, s3d_vec3_create(tri.v3.x, tri.v3.y, tri.v3.z), &x3, &y3, &d3);
    
    // Convert to screen coordinates
    int32 sx1 = (int32)((x1 + 1.0f) * 0.5f * renderer->framebuffer.width);
    int32 sy1 = (int32)((1.0f - y1) * 0.5f * renderer->framebuffer.height);
    int32 sx2 = (int32)((x2 + 1.0f) * 0.5f * renderer->framebuffer.width);
    int32 sy2 = (int32)((1.0f - y2) * 0.5f * renderer->framebuffer.height);
    int32 sx3 = (int32)((x3 + 1.0f) * 0.5f * renderer->framebuffer.width);
    int32 sy3 = (int32)((1.0f - y3) * 0.5f * renderer->framebuffer.height);
    
    if (filled) {
        // Barycentric coordinate fill
        int32 min_x = fminf(fminf(sx1, sx2), sx3);
        int32 max_x = fmaxf(fmaxf(sx1, sx2), sx3);
        int32 min_y = fminf(fminf(sy1, sy2), sy3);
        int32 max_y = fmaxf(fmaxf(sy1, sy2), sy3);
        
        for (int32 py = min_y; py <= max_y; py++) {
            for (int32 px = min_x; px <= max_x; px++) {
                // Calculate barycentric coordinates
                float32 denom = ((sy2 - sy3) * (sx1 - sx3) + (sx3 - sx2) * (sy1 - sy3));
                if (fabsf(denom) < 0.0001f) continue;
                
                float32 u = ((sy2 - sy3) * (px - sx3) + (sx3 - sx2) * (py - sy3)) / denom;
                float32 v = ((sy3 - sy1) * (px - sx3) + (sx1 - sx3) * (py - sy3)) / denom;
                float32 w = 1.0f - u - v;
                
                if (u >= 0 && u <= 1 && v >= 0 && v <= 1 && w >= 0 && w <= 1) {
                    float32 depth = u * d1 + v * d2 + w * d3;
                    
                    // Interpolate color
                    float32 r = u * tri.v1.r + v * tri.v2.r + w * tri.v3.r;
                    float32 g = u * tri.v1.g + v * tri.v2.g + w * tri.v3.g;
                    float32 b = u * tri.v1.b + v * tri.v2.b + w * tri.v3.b;
                    float32 a = u * tri.v1.a + v * tri.v2.a + w * tri.v3.a;
                    
                    uint32 packed = pack_rgba(r, g, b, a);
                    draw_pixel(renderer, px, py, depth, packed);
                }
            }
        }
    } else {
        // Just draw edges
        s3d_vec3 p1 = {tri.v1.x, tri.v1.y, tri.v1.z};
        s3d_vec3 p2 = {tri.v2.x, tri.v2.y, tri.v2.z};
        s3d_vec3 p3 = {tri.v3.x, tri.v3.y, tri.v3.z};
        s3d_color c = {tri.v1.r, tri.v1.g, tri.v1.b, tri.v1.a};
        
        s3d_draw_line(renderer, p1, p2, c, 1.0f);
        s3d_draw_line(renderer, p2, p3, c, 1.0f);
        s3d_draw_line(renderer, p3, p1, c, 1.0f);
    }
    
    renderer->stats.draw_calls++;
    renderer->stats.triangles_rendered++;
}

void s3d_draw_rect(s3d_renderer* renderer, s3d_rect rect, s3d_color color, bool32 filled) {
    if (!renderer) return;
    
    if (filled) {
        uint32 packed = pack_rgba(color.r, color.g, color.b, color.a);
        int32 x0 = (int32)rect.x;
        int32 y0 = (int32)rect.y;
        int32 x1 = (int32)(rect.x + rect.width);
        int32 y1 = (int32)(rect.y + rect.height);
        
        for (int32 y = y0; y < y1; y++) {
            for (int32 x = x0; x < x1; x++) {
                draw_pixel(renderer, x, y, 0.5f, packed);
            }
        }
    } else {
        // Draw outline using lines
        s3d_vec3 tl = {rect.x, rect.y, 0};
        s3d_vec3 tr = {rect.x + rect.width, rect.y, 0};
        s3d_vec3 br = {rect.x + rect.width, rect.y + rect.height, 0};
        s3d_vec3 bl = {rect.x, rect.y + rect.height, 0};
        
        s3d_draw_line(renderer, tl, tr, color, 1.0f);
        s3d_draw_line(renderer, tr, br, color, 1.0f);
        s3d_draw_line(renderer, br, bl, color, 1.0f);
        s3d_draw_line(renderer, bl, tl, color, 1.0f);
    }
    
    renderer->stats.draw_calls++;
}

void s3d_draw_circle(s3d_renderer* renderer, s3d_vec2 center, float32 radius, s3d_color color, int32 segments) {
    if (!renderer || segments <= 0) return;
    
    // Draw as polygon
    float32 angle_step = 2.0f * 3.14159265f / segments;
    
    for (int32 i = 0; i < segments; i++) {
        float32 angle1 = i * angle_step;
        float32 angle2 = (i + 1) * angle_step;
        
        s3d_vec3 p1 = {center.x + cosf(angle1) * radius, center.y + sinf(angle1) * radius, 0};
        s3d_vec3 p2 = {center.x + cosf(angle2) * radius, center.y + sinf(angle2) * radius, 0};
        
        s3d_draw_line(renderer, p1, p2, color, 1.0f);
    }
    
    renderer->stats.draw_calls++;
}

// Mesh operations
s3d_mesh* s3d_create_mesh(const s3d_vertex* vertices, uint32 vertex_count, const uint16* indices, uint32 index_count) {
    s3d_mesh* mesh = (s3d_mesh*)calloc(1, sizeof(s3d_mesh));
    if (!mesh) return NULL;
    
    mesh->vertex_count = vertex_count;
    mesh->vertices = (s3d_vertex*)malloc(vertex_count * sizeof(s3d_vertex));
    memcpy(mesh->vertices, vertices, vertex_count * sizeof(s3d_vertex));
    
    if (indices && index_count > 0) {
        mesh->index_count = index_count;
        mesh->indices = (uint16*)malloc(index_count * sizeof(uint16));
        memcpy(mesh->indices, indices, index_count * sizeof(uint16));
    }
    
    return mesh;
}

void s3d_destroy_mesh(s3d_mesh* mesh) {
    if (mesh) {
        free(mesh->vertices);
        free(mesh->indices);
        free(mesh);
    }
}

void s3d_draw_mesh(s3d_renderer* renderer, s3d_mesh* mesh) {
    if (!renderer || !mesh) return;
    
    // Draw triangles from indexed mesh
    for (uint32 i = 0; i < mesh->index_count; i += 3) {
        s3d_triangle tri;
        tri.v1 = mesh->vertices[mesh->indices[i]];
        tri.v2 = mesh->vertices[mesh->indices[i + 1]];
        tri.v3 = mesh->vertices[mesh->indices[i + 2]];
        
        s3d_draw_triangle(renderer, tri, S3D_TRUE);
    }
    
    renderer->stats.draw_calls++;
}

// Primitive generators
s3d_mesh* s3d_create_cube(float32 size) {
    float32 h = size / 2.0f;
    
    s3d_vertex vertices[] = {
        // Front face
        {-h, -h,  h, 1, 1, 1, 1, 0, 0},
        { h, -h,  h, 1, 1, 1, 1, 1, 0},
        { h,  h,  h, 1, 1, 1, 1, 1, 1},
        {-h,  h,  h, 1, 1, 1, 1, 0, 1},
        // Back face
        {-h, -h, -h, 1, 1, 1, 1, 0, 0},
        { h, -h, -h, 1, 1, 1, 1, 1, 0},
        { h,  h, -h, 1, 1, 1, 1, 1, 1},
        {-h,  h, -h, 1, 1, 1, 1, 0, 1},
    };
    
    uint16 indices[] = {
        0, 1, 2,  0, 2, 3,  // Front
        4, 5, 6,  4, 6, 7,  // Back
        // ... more faces would go here
    };
    
    return s3d_create_mesh(vertices, 8, indices, 12);
}

s3d_mesh* s3d_create_sphere(float32 radius, int32 slices, int32 stacks) {
    // Simplified stub - full implementation would generate UV sphere
    S3D_UNUSED(radius); S3D_UNUSED(slices); S3D_UNUSED(stacks);
    return NULL;
}

s3d_mesh* s3d_create_quad(float32 width, float32 height) {
    float32 hw = width / 2.0f;
    float32 hh = height / 2.0f;
    
    s3d_vertex vertices[] = {
        {-hw, -hh, 0, 1, 1, 1, 1, 0, 0},
        { hw, -hh, 0, 1, 1, 1, 1, 1, 0},
        { hw,  hh, 0, 1, 1, 1, 1, 1, 1},
        {-hw,  hh, 0, 1, 1, 1, 1, 0, 1},
    };
    
    uint16 indices[] = {0, 1, 2, 0, 2, 3};
    
    return s3d_create_mesh(vertices, 4, indices, 6);
}

// Texture operations (stub for software renderer)
s3d_texture* s3d_create_texture(int32 width, int32 height, const uint8* rgba_data) {
    s3d_texture* tex = (s3d_texture*)calloc(1, sizeof(s3d_texture));
    if (!tex) return NULL;
    
    tex->width = width;
    tex->height = height;
    tex->data = (uint8*)malloc(width * height * 4);
    memcpy(tex->data, rgba_data, width * height * 4);
    tex->loaded = S3D_TRUE;
    
    return tex;
}

void s3d_destroy_texture(s3d_texture* texture) {
    if (texture) {
        free(texture->data);
        free(texture);
    }
}

void s3d_bind_texture(s3d_renderer* renderer, s3d_texture* texture) {
    S3D_UNUSED(renderer); S3D_UNUSED(texture);
    // Software renderer doesn't use textures yet
}

void s3d_draw_textured_rect(s3d_renderer* renderer, s3d_rect rect, s3d_texture* texture, s3d_color tint) {
    S3D_UNUSED(texture);
    // Fallback to solid color
    s3d_draw_rect(renderer, rect, tint, S3D_TRUE);
}

// Lighting (not supported in basic software renderer)
void s3d_enable_lighting(s3d_renderer* renderer, bool32 enable) {
    if (renderer) renderer->lighting_enabled = enable;
}

void s3d_set_light_position(s3d_renderer* renderer, int32 light_index, float32 x, float32 y, float32 z, float32 w) {
    S3D_UNUSED(renderer); S3D_UNUSED(light_index); S3D_UNUSED(x); S3D_UNUSED(y); S3D_UNUSED(z); S3D_UNUSED(w);
}

void s3d_set_light_color(s3d_renderer* renderer, int32 light_index, s3d_color ambient, s3d_color diffuse, s3d_color specular) {
    S3D_UNUSED(renderer); S3D_UNUSED(light_index); S3D_UNUSED(ambient); S3D_UNUSED(diffuse); S3D_UNUSED(specular);
}

void s3d_set_material(s3d_renderer* renderer, s3d_color ambient, s3d_color diffuse, s3d_color specular, float32 shininess) {
    S3D_UNUSED(renderer); S3D_UNUSED(ambient); S3D_UNUSED(diffuse); S3D_UNUSED(specular); S3D_UNUSED(shininess);
}

// Depth and blending
void s3d_enable_depth_test(s3d_renderer* renderer, bool32 enable) {
    if (renderer) renderer->depth_test_enabled = enable;
}

void s3d_clear_depth(s3d_renderer* renderer) {
    if (!renderer) return;
    int32 total = renderer->framebuffer.width * renderer->framebuffer.height;
    for (int i = 0; i < total; i++) {
        renderer->framebuffer.depth_buffer[i] = 1.0f;
    }
}

void s3d_enable_blending(s3d_renderer* renderer, bool32 enable) {
    if (renderer) renderer->blending_enabled = enable;
}

void s3d_get_stats(s3d_renderer* renderer, s3d_render_stats* stats) {
    if (renderer && stats) {
        *stats = renderer->stats;
    }
}
