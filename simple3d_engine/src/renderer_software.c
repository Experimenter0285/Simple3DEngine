// Simple3DEngine - Software Renderer Implementation
// Pure CPU rasterizer for old PCs without any OpenGL support.
// Pipeline: model -> view/proj matrix stack -> clip space -> homogeneous divide
//           -> viewport -> half-space fill with barycentric interpolation
//           (color + depth + UV, perspective-correct) -> nearest texture sampling
//           -> alpha blending -> framebuffer (+ z-buffer).

#include "renderer_common.h"
#include "s3d_internal.h"
#include "s3d_font_8x8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    int32 width;
    int32 height;
    uint32* pixels;      // RGBA8888 owned here or borrowed from the platform surface
    uint32* own_pixels;  // non-NULL if we allocated it ourselves (headless fallback)
    float32* depth_buffer;
    bool32 present_to_window; // TRUE when pixels belong to the window surface
} s3d_framebuffer;

typedef struct {
    s3d_renderer base;          // MUST be first member (shared layout)
    s3d_framebuffer framebuffer;
} sw_renderer;

// Cast helper: struct s3d_renderer is the first member of sw_renderer.
static inline sw_renderer* SW(s3d_renderer* r) { return (sw_renderer*)r; }

// ---------------------------------------------------------------------------
// Pixel packing / blending
// ---------------------------------------------------------------------------
static inline uint32 pack_rgba(float32 r, float32 g, float32 b, float32 a) {
    if (r < 0) r = 0; if (r > 1) r = 1;
    if (g < 0) g = 0; if (g > 1) g = 1;
    if (b < 0) b = 0; if (b > 1) b = 1;
    if (a < 0) a = 0; if (a > 1) a = 1;
    uint32 ri = (uint32)(r * 255.0f);
    uint32 gi = (uint32)(g * 255.0f);
    uint32 bi = (uint32)(b * 255.0f);
    uint32 ai = (uint32)(a * 255.0f);
    return (ai << 24) | (bi << 16) | (gi << 8) | ri;
}

static inline void unpack_rgba(uint32 p, float32* r, float32* g, float32* b, float32* a) {
    *r = ((p      ) & 0xFFu) / 255.0f;
    *g = ((p >>  8) & 0xFFu) / 255.0f;
    *b = ((p >> 16) & 0xFFu) / 255.0f;
    *a = ((p >> 24) & 0xFFu) / 255.0f;
}

// Write one pixel with depth test and source-over alpha blending.
// Depth in [0,1], smaller = nearer.
static inline void fb_put(s3d_framebuffer* fb, int32 x, int32 y, float32 depth, uint32 color) {
    if (x < 0 || x >= fb->width || y < 0 || y >= fb->height) return;
    size_t idx = (size_t)y * (size_t)fb->width + (size_t)x;

    if (depth < fb->depth_buffer[idx]) {
        uint8 sa = (uint8)(color >> 24);
        if (sa == 255) {
            fb->pixels[idx] = color;
            fb->depth_buffer[idx] = depth;
        } else if (sa > 0) {
            float32 dr, dg, db, da;
            unpack_rgba(fb->pixels[idx], &dr, &dg, &db, &da);
            float32 sr = ((color      ) & 0xFFu) / 255.0f;
            float32 sg = ((color >>  8) & 0xFFu) / 255.0f;
            float32 sb = ((color >> 16) & 0xFFu) / 255.0f;
            float32 af = sa / 255.0f;
            float32 rr = sr * af + dr * (1.0f - af);
            float32 rg = sg * af + dg * (1.0f - af);
            float32 rb = sb * af + db * (1.0f - af);
            float32 ra = af + da * (1.0f - af);
            fb->pixels[idx] = pack_rgba(rr, rg, rb, ra);
            fb->depth_buffer[idx] = depth;
        }
    }
}

// Solid pixel write ignoring depth (used by the bitmap-font blitter).
static inline void fb_put_nodepth(s3d_framebuffer* fb, int32 x, int32 y, uint32 color) {
    if (x < 0 || x >= fb->width || y < 0 || y >= fb->height) return;
    fb->pixels[(size_t)y * (size_t)fb->width + (size_t)x] = color;
}

// ---------------------------------------------------------------------------
// Creation / destruction
// ---------------------------------------------------------------------------
s3d_renderer* s3d__create_renderer_software(s3d_window* window) {
    sw_renderer* r = (sw_renderer*)calloc(1, sizeof(sw_renderer));
    if (!r) return NULL;

    r->base.type = S3D_RENDERER_SOFTWARE;
    r->base.window = window;
    r->base.current_matrix = s3d_mat4_identity();
    r->base.clear_color = s3d_color_rgba(0.1f, 0.1f, 0.15f, 1.0f);
    r->base.depth_test_enabled = S3D_TRUE;

    int32 w = 0, h = 0, pitch = 0;
    void* surf = window ? s3d__window_get_pixels(window, &w, &h, &pitch) : NULL;

    s3d_framebuffer* fb = &r->framebuffer;
    if (surf && pitch == w * 4) {
        fb->pixels = (uint32*)surf;         // draw straight into the window surface
        fb->present_to_window = S3D_TRUE;
    } else {
        if (!surf) { s3d_get_window_size(window, &w, &h); }
        if (w <= 0) w = 320;
        if (h <= 0) h = 240;
        fb->own_pixels = (uint32*)calloc((size_t)w * (size_t)h, sizeof(uint32));
        fb->pixels = fb->own_pixels;
        fb->present_to_window = S3D_FALSE;
    }
    fb->width = w;
    fb->height = h;
    fb->depth_buffer = (float32*)malloc((size_t)w * (size_t)h * sizeof(float32));
    if (!fb->pixels || !fb->depth_buffer) {
        free(fb->own_pixels); free(fb->depth_buffer); free(r); return NULL;
    }

    printf("Simple3DEngine: software renderer ready (%dx%d, %s)\n",
           w, h, fb->present_to_window ? "direct-to-surface" : "offscreen");
    return &r->base;
}

void s3d_destroy_renderer(s3d_renderer* renderer) {
    if (!renderer) return;
    sw_renderer* r = SW(renderer);
    free(r->framebuffer.own_pixels);
    free(r->framebuffer.depth_buffer);
    free(r);
}

s3d_renderer_type s3d_get_active_renderer_type(s3d_renderer* renderer) {
    return renderer ? renderer->type : S3D_RENDERER_SOFTWARE;
}

// ---------------------------------------------------------------------------
// State management / frame lifecycle
// ---------------------------------------------------------------------------
void s3d_set_clear_color(s3d_renderer* renderer, s3d_color color) {
    if (renderer) renderer->clear_color = color;
}

void s3d_clear(s3d_renderer* renderer) {
    if (!renderer) return;
    s3d_framebuffer* fb = &SW(renderer)->framebuffer;
    uint32 c = pack_rgba(renderer->clear_color.r, renderer->clear_color.g,
                         renderer->clear_color.b, renderer->clear_color.a);
    size_t total = (size_t)fb->width * (size_t)fb->height;
    for (size_t i = 0; i < total; i++) {
        fb->pixels[i] = c;
        fb->depth_buffer[i] = 1.0f;
    }
}

void s3d_begin_frame(s3d_renderer* renderer) {
    if (!renderer) return;
    renderer->stats.draw_calls = 0;
    renderer->stats.triangles_rendered = 0;
    renderer->stats.vertices_processed = 0;
    // Reset to identity so games can build their own MVP each frame.
    renderer->current_matrix = s3d_mat4_identity();
    renderer->matrix_stack_top = 0;
}

void s3d_end_frame(s3d_renderer* renderer) {
    if (!renderer) return;
    if (SW(renderer)->framebuffer.present_to_window && renderer->window) {
        s3d__window_update(renderer->window); // push surface to screen (SDL/X11)
    }
    // Headless/offscreen: nothing to present; swap_buffers is a no-op there.
}

// ---------------------------------------------------------------------------
// Matrix stack (OpenGL 1.x style fixed pipeline)
// ---------------------------------------------------------------------------
void s3d_push_matrix(s3d_renderer* renderer) {
    if (!renderer || renderer->matrix_stack_top >= S3D_MATRIX_STACK_SIZE) return;
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
    if (renderer) renderer->current_matrix = s3d_mat4_multiply(renderer->current_matrix, matrix);
}

void s3d_translate(s3d_renderer* renderer, float32 x, float32 y, float32 z) {
    s3d_mult_matrix(renderer, s3d_mat4_translate(x, y, z));
}

// Arbitrary-axis rotation (Rodrigues), matching glRotatef semantics.
void s3d_rotate(s3d_renderer* renderer, float32 angle_degrees, float32 x, float32 y, float32 z) {
    if (!renderer) return;
    float32 len = sqrtf(x * x + y * y + z * z);
    if (len < 1e-8f) return;
    x /= len; y /= len; z /= len;

    float32 a = angle_degrees * (float32)M_PI / 180.0f;
    float32 c = cosf(a), s = sinf(a), t = 1.0f - c;

    s3d_mat4 m = s3d_mat4_identity();
    // Column-major layout consistent with s3d_math.h primitives.
    m.m[0]  = t*x*x + c;     m.m[4]  = t*x*y - s*z;   m.m[8]  = t*x*z + s*y;
    m.m[1]  = t*x*y + s*z;   m.m[5]  = t*y*y + c;     m.m[9]  = t*y*z - s*x;
    m.m[2]  = t*x*z - s*y;   m.m[6]  = t*y*z + s*x;   m.m[10] = t*z*z + c;
    s3d_mult_matrix(renderer, m);
}

void s3d_scale(s3d_renderer* renderer, float32 x, float32 y, float32 z) {
    s3d_mult_matrix(renderer, s3d_mat4_scale(x, y, z));
}

// ---------------------------------------------------------------------------
// Projection setup
// ---------------------------------------------------------------------------
void s3d_setup_3d_projection(s3d_renderer* renderer, s3d_camera* camera, float32 aspect_ratio) {
    if (!renderer || !camera) return;
    camera->proj_matrix = s3d_mat4_perspective(camera->fov, aspect_ratio,
                                               camera->near_plane, camera->far_plane);
    camera->view_matrix = s3d_mat4_look_at(camera->position, camera->target, camera->up);
    s3d_load_matrix(renderer, s3d_mat4_multiply(camera->proj_matrix, camera->view_matrix));
}

void s3d_setup_2d_projection(s3d_renderer* renderer, int32 width, int32 height) {
    if (!renderer) return;
    // Screen-pixel coordinates: (0,0) top-left, y grows downward, z in [-1,1].
    s3d_mat4 ortho = s3d_mat4_ortho(0.0f, (float32)width, (float32)height, 0.0f, -1.0f, 1.0f);
    s3d_load_matrix(renderer, ortho);
}

// ---------------------------------------------------------------------------
// Vertex transformation
// ---------------------------------------------------------------------------
typedef struct {
    float32 sx, sy;   // screen pixel coords
    float32 z;        // NDC depth mapped to [0,1]
    float32 inv_w;    // 1/w for perspective-correct interpolation
    float32 r, g, b, a;
    float32 u, v;
} screen_vertex;

static bool32 transform_vertex(s3d_renderer* renderer, const s3d_vertex* vin, screen_vertex* out) {
    const float32* m = renderer->current_matrix.m;
    float32 x = vin->x, y = vin->y, z = vin->z;

    float32 cw = m[3]*x + m[7]*y + m[11]*z + m[15];
    if (cw > -1e-6f && cw < 1e-6f) cw = (cw < 0) ? -1e-6f : 1e-6f; // avoid div-by-zero
    float32 inv_w = 1.0f / cw;

    float32 cx = m[0]*x + m[4]*y + m[8]*z + m[12];
    float32 cy = m[1]*x + m[5]*y + m[9]*z + m[13];
    float32 cz = m[2]*x + m[6]*y + m[10]*z + m[14];

    float32 ndc_x = cx * inv_w;
    float32 ndc_y = cy * inv_w;
    float32 ndc_z = cz * inv_w;

    s3d_framebuffer* fb = &SW(renderer)->framebuffer;
    out->sx = (ndc_x * 0.5f + 0.5f) * (float32)fb->width;
    out->sy = (0.5f - ndc_y * 0.5f) * (float32)fb->height; // flip Y for raster space
    out->z  = ndc_z * 0.5f + 0.5f;
    out->inv_w = inv_w;
    out->r = vin->r; out->g = vin->g; out->b = vin->b; out->a = vin->a;
    out->u = vin->u; out->v = vin->v;

    return cw > 0.0f; // FALSE -> vertex is behind the near plane
}

// ---------------------------------------------------------------------------
// Triangle rasterization with perspective-correct interpolation
// ---------------------------------------------------------------------------
static void rasterize_triangle(s3d_renderer* renderer, const screen_vertex* v0,
                               const screen_vertex* v1, const screen_vertex* v2,
                               const s3d_texture* tex) {
    s3d_framebuffer* fb = &SW(renderer)->framebuffer;

    float32 minx = fminf(v0->sx, fminf(v1->sx, v2->sx));
    float32 maxx = fmaxf(v0->sx, fmaxf(v1->sx, v2->sx));
    float32 miny = fminf(v0->sy, fminf(v1->sy, v2->sy));
    float32 maxy = fmaxf(v0->sy, fmaxf(v1->sy, v2->sy));

    int32 ix0 = (int32)fmaxf(floorf(minx), 0.0f);
    int32 iy0 = (int32)fmaxf(floorf(miny), 0.0f);
    int32 ix1 = (int32)fminf(ceilf(maxx), (float32)(fb->width  - 1));
    int32 iy1 = (int32)fminf(ceilf(maxy), (float32)(fb->height - 1));
    if (ix0 > ix1 || iy0 > iy1) return;

    float32 area = (v1->sx - v0->sx) * (v2->sy - v0->sy) -
                   (v2->sx - v0->sx) * (v1->sy - v0->sy);
    if (fabsf(area) < 1e-8f) return; // degenerate
    float32 inv_area = 1.0f / area;

    for (int32 py = iy0; py <= iy1; py++) {
        float32 pcy = (float32)py + 0.5f;
        for (int32 px = ix0; px <= ix1; px++) {
            float32 pcx = (float32)px + 0.5f;

            float32 w0 = ((v1->sx - pcx) * (v2->sy - pcy) - (v2->sx - pcx) * (v1->sy - pcy)) * inv_area;
            float32 w1 = ((v2->sx - pcx) * (v0->sy - pcy) - (v0->sx - pcx) * (v2->sy - pcy)) * inv_area;
            float32 w2 = 1.0f - w0 - w1;

            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue; // two-sided rendering

            // Perspective-correct: interpolate attr/w, then multiply by real 1/w.
            float32 iw = w0 * v0->inv_w + w1 * v1->inv_w + w2 * v2->inv_w;
            if (iw <= 0.0f) continue;
            float32 corr = 1.0f / iw;

            float32 depth = (w0 * v0->z + w1 * v1->z + w2 * v2->z) * corr;

            float32 r = (w0*v0->r*v0->inv_w + w1*v1->r*v1->inv_w + w2*v2->r*v2->inv_w) * corr;
            float32 g = (w0*v0->g*v0->inv_w + w1*v1->g*v1->inv_w + w2*v2->g*v2->inv_w) * corr;
            float32 b = (w0*v0->b*v0->inv_w + w1*v1->b*v1->inv_w + w2*v2->b*v2->inv_w) * corr;
            float32 a = (w0*v0->a*v0->inv_w + w1*v1->a*v1->inv_w + w2*v2->a*v2->inv_w) * corr;

            uint32 color;
            if (tex && tex->loaded && tex->data) {
                float32 tu = (w0*v0->u*v0->inv_w + w1*v1->u*v1->inv_w + w2*v2->u*v2->inv_w) * corr;
                float32 tv = (w0*v0->v*v0->inv_w + w1*v1->v*v1->inv_w + w2*v2->v*v2->inv_w) * corr;
                tu -= floorf(tu); tv -= floorf(tv); // wrap mode
                int32 tx = (int32)(tu * tex->width);
                int32 ty = (int32)(tv * tex->height);
                if (tx >= tex->width)  tx = tex->width - 1;
                if (ty >= tex->height) ty = tex->height - 1;
                uint32 tc = ((const uint32*)tex->data)[(size_t)ty * tex->width + tx];
                float32 tr, tg, tb, ta;
                unpack_rgba(tc, &tr, &tg, &tb, &ta);
                color = pack_rgba(r * tr, g * tg, b * tb, a * ta);
            } else {
                color = pack_rgba(r, g, b, a);
            }

            fb_put(fb, px, py, depth, color);
        }
    }
}

// ---------------------------------------------------------------------------
// Primitive drawing
// ---------------------------------------------------------------------------
void s3d_draw_point(s3d_renderer* renderer, s3d_vec3 pos, s3d_color color, float32 size) {
    if (!renderer) return;
    s3d_vertex vin = {pos.x, pos.y, pos.z, color.r, color.g, color.b, color.a, 0, 0};
    screen_vertex sv;
    if (!transform_vertex(renderer, &vin, &sv)) return;

    int32 rad = (size > 1.0f) ? (int32)(size * 0.5f) : 0;
    uint32 packed = pack_rgba(color.r, color.g, color.b, color.a);
    for (int32 dy = -rad; dy <= rad; dy++)
        for (int32 dx = -rad; dx <= rad; dx++)
            fb_put(&SW(renderer)->framebuffer, (int32)sv.sx + dx, (int32)sv.sy + dy, sv.z, packed);

    renderer->stats.draw_calls++;
    renderer->stats.vertices_processed++;
}

void s3d_draw_line(s3d_renderer* renderer, s3d_vec3 start, s3d_vec3 end, s3d_color color, float32 width) {
    if (!renderer) return;
    s3d_vertex va = {start.x, start.y, start.z, color.r, color.g, color.b, color.a, 0, 0};
    s3d_vertex vb = {end.x, end.y, end.z, color.r, color.g, color.b, color.a, 0, 0};
    screen_vertex s0, s1;
    bool32 ok0 = transform_vertex(renderer, &va, &s0);
    bool32 ok1 = transform_vertex(renderer, &vb, &s1);
    if (!ok0 || !ok1) return;

    if (width < 1.0f) width = 1.0f;
    int32 thick = (int32)width;

    // Bresenham with thickness and linear depth lerp
    int32 x0 = (int32)s0.sx, y0 = (int32)s0.sy, x1 = (int32)s1.sx, y1 = (int32)s1.sy;
    int32 adx = abs(x1 - x0), ady = abs(y1 - y0);
    int32 sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int32 dx = adx, dy = -ady;
    int32 err = dx + dy, dist = 0, total = (adx > ady) ? adx : ady;
    if (total == 0) total = 1;

    uint32 packed = pack_rgba(color.r, color.g, color.b, color.a);
    while (1) {
        float32 t = dist / (float32)total;
        float32 depth = s0.z + (s1.z - s0.z) * t;
        for (int32 oy = 0; oy < thick; oy++)
            for (int32 ox = 0; ox < thick; ox++)
                fb_put(&SW(renderer)->framebuffer, x0 + ox, y0 + oy, depth, packed);
        if (x0 == x1 && y0 == y1) break;
        int32 e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; dist++; }
        if (e2 <= dx) { err += dx; y0 += sy; dist++; }
    }
    renderer->stats.draw_calls++;
}

void s3d_draw_triangle(s3d_renderer* renderer, s3d_triangle tri, bool32 filled) {
    if (!renderer) return;
    screen_vertex s0, s1, s2;
    bool32 ok = transform_vertex(renderer, &tri.v1, &s0);
    ok &= transform_vertex(renderer, &tri.v2, &s1);
    ok &= transform_vertex(renderer, &tri.v3, &s2);
    renderer->stats.vertices_processed += 3;
    if (!ok) return;

    if (filled) {
        rasterize_triangle(renderer, &s0, &s1, &s2, renderer->bound_texture);
        renderer->stats.triangles_rendered++;
    } else {
        s3d_color c = {tri.v1.r, tri.v1.g, tri.v1.b, tri.v1.a};
        s3d_draw_line(renderer, s3d_vec3_create(tri.v1.x, tri.v1.y, tri.v1.z),
                      s3d_vec3_create(tri.v2.x, tri.v2.y, tri.v2.z), c, 1.0f);
        s3d_draw_line(renderer, s3d_vec3_create(tri.v2.x, tri.v2.y, tri.v2.z),
                      s3d_vec3_create(tri.v3.x, tri.v3.y, tri.v3.z), c, 1.0f);
        s3d_draw_line(renderer, s3d_vec3_create(tri.v3.x, tri.v3.y, tri.v3.z),
                      s3d_vec3_create(tri.v1.x, tri.v1.y, tri.v1.z), c, 1.0f);
    }
    renderer->stats.draw_calls++;
}

// Axis-aligned quad directly in framebuffer space (fast path for UI/2D sprites).
static void fill_rect_fb(s3d_renderer* renderer, float32 x, float32 y, float32 w, float32 h,
                         float32 depth, s3d_color color, const s3d_texture* tex,
                         float32 u0, float32 v0, float32 u1, float32 v1) {
    s3d_framebuffer* fb = &SW(renderer)->framebuffer;
    int32 ix0 = (int32)x, iy0 = (int32)y;
    int32 ix1 = (int32)(x + w), iy1 = (int32)(y + h);
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > fb->width)  ix1 = fb->width;
    if (iy1 > fb->height) iy1 = fb->height;

    uint32 solid = pack_rgba(color.r, color.g, color.b, color.a);
    for (int32 py = iy0; py < iy1; py++) {
        for (int32 px = ix0; px < ix1; px++) {
            if (tex && tex->loaded && tex->data) {
                float32 fu = (w > 0) ? (px - x) / w : 0;
                float32 fv = (h > 0) ? (py - y) / h : 0;
                float32 tu = u0 + (u1 - u0) * fu;
                float32 tv = v0 + (v1 - v0) * fv;
                int32 tx = (int32)(tu * tex->width);
                int32 ty = (int32)(tv * tex->height);
                if (tx < 0) tx = 0; if (tx >= tex->width)  tx = tex->width - 1;
                if (ty < 0) ty = 0; if (ty >= tex->height) ty = tex->height - 1;
                uint32 tc = ((const uint32*)tex->data)[(size_t)ty * tex->width + tx];
                float32 tr, tg, tb, ta;
                unpack_rgba(tc, &tr, &tg, &tb, &ta);
                fb_put(fb, px, py, depth, pack_rgba(color.r*tr, color.g*tg, color.b*tb, color.a*ta));
            } else {
                fb_put(fb, px, py, depth, solid);
            }
        }
    }
}

void s3d_draw_rect(s3d_renderer* renderer, s3d_rect rect, s3d_color color, bool32 filled) {
    if (!renderer) return;
    if (filled) {
        fill_rect_fb(renderer, rect.x, rect.y, rect.width, rect.height, 0.5f, color, NULL, 0,0,0,0);
    } else {
        s3d_vec3 tl = {rect.x, rect.y, 0}, tr = {rect.x + rect.width, rect.y, 0};
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
    if (!renderer || segments <= 2) return;
    float32 step = 2.0f * (float32)M_PI / (float32)segments;
    for (int32 i = 0; i < segments; i++) {
        float32 a1 = i * step, a2 = (i + 1) * step;
        s3d_vec3 p1 = {center.x + cosf(a1) * radius, center.y + sinf(a1) * radius, 0};
        s3d_vec3 p2 = {center.x + cosf(a2) * radius, center.y + sinf(a2) * radius, 0};
        s3d_draw_line(renderer, p1, p2, color, 1.0f);
    }
    renderer->stats.draw_calls++;
}

// ---------------------------------------------------------------------------
// Meshes
// ---------------------------------------------------------------------------
s3d_mesh* s3d_create_mesh(const s3d_vertex* vertices, uint32 vertex_count,
                          const uint16* indices, uint32 index_count) {
    if (!vertices || vertex_count == 0) return NULL;
    s3d_mesh* mesh = (s3d_mesh*)calloc(1, sizeof(s3d_mesh));
    if (!mesh) return NULL;

    mesh->vertex_count = vertex_count;
    mesh->vertices = (s3d_vertex*)malloc(vertex_count * sizeof(s3d_vertex));
    memcpy(mesh->vertices, vertices, vertex_count * sizeof(s3d_vertex));

    if (indices && index_count >= 3) {
        mesh->index_count = index_count - (index_count % 3);
        mesh->indices = (uint16*)malloc(mesh->index_count * sizeof(uint16));
        memcpy(mesh->indices, indices, mesh->index_count * sizeof(uint16));
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
    if (mesh->indices && mesh->index_count >= 3) {
        for (uint32 i = 0; i + 2 < mesh->index_count; i += 3) {
            s3d_triangle tri;
            tri.v1 = mesh->vertices[mesh->indices[i]];
            tri.v2 = mesh->vertices[mesh->indices[i + 1]];
            tri.v3 = mesh->vertices[mesh->indices[i + 2]];
            s3d_draw_triangle(renderer, tri, S3D_TRUE);
        }
    } else {
        for (uint32 i = 0; i + 2 < mesh->vertex_count; i += 3) {
            s3d_triangle tri;
            tri.v1 = mesh->vertices[i];
            tri.v2 = mesh->vertices[i + 1];
            tri.v3 = mesh->vertices[i + 2];
            s3d_draw_triangle(renderer, tri, S3D_TRUE);
        }
    }
}

s3d_mesh* s3d_create_cube(float32 size) {
    float32 h = size * 0.5f;
    // Six faces; per-face baked shade keeps silhouettes readable without lighting.
    s3d_vertex v[] = {
        // Front (+Z)
        {-h,-h, h, 1,1,1,1, 0,1}, { h,-h, h, 1,1,1,1, 1,1}, { h, h, h, 1,1,1,1, 1,0}, {-h, h, h, 1,1,1,1, 0,0},
        // Back (-Z)
        { h,-h,-h, .7f,.7f,.7f,1, 0,1}, {-h,-h,-h, .7f,.7f,.7f,1, 1,1}, {-h, h,-h, .7f,.7f,.7f,1, 1,0}, { h, h,-h, .7f,.7f,.7f,1, 0,0},
        // Right (+X)
        { h,-h, h, .9f,.9f,.9f,1, 0,1}, { h,-h,-h, .9f,.9f,.9f,1, 1,1}, { h, h,-h, .9f,.9f,.9f,1, 1,0}, { h, h, h, .9f,.9f,.9f,1, 0,0},
        // Left (-X)
        {-h,-h,-h, .6f,.6f,.6f,1, 0,1}, {-h,-h, h, .6f,.6f,.6f,1, 1,1}, {-h, h, h, .6f,.6f,.6f,1, 1,0}, {-h, h,-h, .6f,.6f,.6f,1, 0,0},
        // Top (+Y)
        {-h, h, h, 1,1,.8f,1, 0,0}, { h, h, h, 1,1,.8f,1, 1,0}, { h, h,-h, 1,1,.8f,1, 1,1}, {-h, h,-h, 1,1,.8f,1, 0,1},
        // Bottom (-Y)
        {-h,-h,-h, .4f,.4f,.4f,1, 0,0}, { h,-h,-h, .4f,.4f,.4f,1, 1,0}, { h,-h, h, .4f,.4f,.4f,1, 1,1}, {-h,-h, h, .4f,.4f,.4f,1, 0,1},
    };
    uint16 idx[] = {
         0, 1, 2,  0, 2, 3,   4, 5, 6,  4, 6, 7,   8, 9,10,  8,10,11,
        12,13,14, 12,14,15,  16,17,18, 16,18,19,  20,21,22, 20,22,23,
    };
    return s3d_create_mesh(v, 24, idx, 36);
}

s3d_mesh* s3d_create_sphere(float32 radius, int32 slices, int32 stacks) {
    if (slices < 3) slices = 8;
    if (stacks < 2) stacks = 6;

    uint32 vcount = (uint32)(slices + 1) * (uint32)(stacks + 1);
    s3d_vertex* verts = (s3d_vertex*)malloc(vcount * sizeof(s3d_vertex));
    if (!verts) return NULL;

    uint32 icount = (uint32)slices * (uint32)stacks * 6;
    uint16* inds = (uint16*)malloc(icount * sizeof(uint16));
    if (!inds) { free(verts); return NULL; }

    uint32 vi = 0;
    for (int32 st = 0; st <= stacks; st++) {
        float32 phi = (float32)st / stacks * (float32)M_PI;             // 0..PI
        for (int32 sl = 0; sl <= slices; sl++) {
            float32 th = (float32)sl / slices * 2.0f * (float32)M_PI;   // 0..2PI
            float32 x = sinf(phi) * cosf(th);
            float32 y = cosf(phi);
            float32 z = sinf(phi) * sinf(th);
            float32 shade = 0.45f + 0.55f * fabsf(y * 0.5f + x * 0.5f); // cheap baked light
            s3d_vertex vv;
            vv.x = radius*x; vv.y = radius*y; vv.z = radius*z;
            vv.r = shade; vv.g = shade; vv.b = shade; vv.a = 1.0f;
            vv.u = (float32)sl / slices; vv.v = (float32)st / stacks;
            verts[vi++] = vv;
        }
    }
    uint32 ii = 0;
    int32 stride = slices + 1;
    for (int32 st = 0; st < stacks; st++) {
        for (int32 sl = 0; sl < slices; sl++) {
            uint16 a = (uint16)(st * stride + sl);
            uint16 b = (uint16)(a + 1);
            uint16 c = (uint16)(a + stride);
            uint16 d = (uint16)(c + 1);
            inds[ii++] = a; inds[ii++] = c; inds[ii++] = b;
            inds[ii++] = b; inds[ii++] = c; inds[ii++] = d;
        }
    }
    s3d_mesh* m = s3d_create_mesh(verts, vcount, inds, icount);
    free(verts); free(inds);
    return m;
}

s3d_mesh* s3d_create_quad(float32 width, float32 height) {
    float32 hw = width * 0.5f, hh = height * 0.5f;
    s3d_vertex v[] = {
        {-hw,-hh,0, 1,1,1,1, 0,1}, { hw,-hh,0, 1,1,1,1, 1,1},
        { hw, hh,0, 1,1,1,1, 1,0}, {-hw, hh,0, 1,1,1,1, 0,0},
    };
    uint16 idx[] = {0,1,2, 0,2,3};
    return s3d_create_mesh(v, 4, idx, 6);
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------
static uint32 g_next_texture_id = 1;

s3d_texture* s3d_create_texture(int32 width, int32 height, const uint8* rgba_data) {
    if (width <= 0 || height <= 0) return NULL;
    s3d_texture* tex = (s3d_texture*)calloc(1, sizeof(s3d_texture));
    if (!tex) return NULL;
    tex->id = g_next_texture_id++;
    tex->width = width;
    tex->height = height;
    tex->data = (uint8*)malloc((size_t)width * (size_t)height * 4);
    if (!tex->data) { free(tex); return NULL; }
    if (rgba_data) memcpy(tex->data, rgba_data, (size_t)width * (size_t)height * 4);
    else           memset(tex->data, 0xFF, (size_t)width * (size_t)height * 4);
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
    if (renderer) renderer->bound_texture = texture;
}

void s3d_draw_textured_rect(s3d_renderer* renderer, s3d_rect rect, s3d_texture* texture, s3d_color tint) {
    if (!renderer) return;
    if (!texture || !texture->loaded) {
        s3d_draw_rect(renderer, rect, tint, S3D_TRUE);
        return;
    }
    // Fast path: direct framebuffer-space textured blit (2D sprite mode).
    fill_rect_fb(renderer, rect.x, rect.y, rect.width, rect.height, 0.5f, tint,
                 texture, 0.0f, 0.0f, 1.0f, 1.0f);
    renderer->stats.draw_calls++;
}

// ---------------------------------------------------------------------------
// Lighting stubs (software path uses vertex colors / baked shades)
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Depth / blending / stats
// ---------------------------------------------------------------------------
void s3d_enable_depth_test(s3d_renderer* renderer, bool32 enable) {
    if (renderer) renderer->depth_test_enabled = enable;
}

void s3d_clear_depth(s3d_renderer* renderer) {
    if (!renderer) return;
    s3d_framebuffer* fb = &SW(renderer)->framebuffer;
    size_t total = (size_t)fb->width * (size_t)fb->height;
    for (size_t i = 0; i < total; i++) fb->depth_buffer[i] = 1.0f;
}

void s3d_enable_blending(s3d_renderer* renderer, bool32 enable) {
    if (renderer) renderer->blending_enabled = enable;
}

void s3d_get_stats(s3d_renderer* renderer, s3d_render_stats* stats) {
    if (renderer && stats) *stats = renderer->stats;
}

// ---------------------------------------------------------------------------
// Built-in bitmap font text (screen space, ignores current matrix)
// ---------------------------------------------------------------------------
void s3d__draw_text_software(s3d_renderer* renderer, const char* text, float32 x, float32 y,
                             float32 scale, s3d_color color) {
    if (!renderer || !text) return;
    if (scale < 1.0f) scale = 1.0f;
    s3d_framebuffer* fb = &SW(renderer)->framebuffer;
    uint32 fg = pack_rgba(color.r, color.g, color.b, color.a);

    float32 cursor_x = x;
    for (const char* p = text; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch == '\n') { cursor_x = x; y += scale * S3D_FONT_GLYPH_HEIGHT; continue; }
        if (ch < S3D_FONT_FIRST_CHAR || ch > S3D_FONT_LAST_CHAR) {
            cursor_x += scale * S3D_FONT_GLYPH_WIDTH;
            continue;
        }
        const uint8_t* glyph = s3d_font_8x8[ch - S3D_FONT_FIRST_CHAR];
        for (int row = 0; row < S3D_FONT_GLYPH_HEIGHT; row++) {
            uint8_t bits = glyph[row];
            if (!bits) continue;
            for (int col = 0; col < S3D_FONT_GLYPH_WIDTH; col++) {
                if (bits & (0x80 >> col)) {
                    int32 px0 = (int32)(cursor_x + col * scale);
                    int32 py0 = (int32)(y + row * scale);
                    for (int32 sy = 0; sy < (int32)scale; sy++)
                        for (int32 sx = 0; sx < (int32)scale; sx++)
                            fb_put_nodepth(fb, px0 + sx, py0 + sy, fg);
                }
            }
        }
        cursor_x += scale * S3D_FONT_GLYPH_WIDTH;
    }
    renderer->stats.draw_calls++;
}

// Public entry point for the software backend: blit through the shared
// rasterizer. The GL backend defines its own s3d_draw_text in renderer_gl.c.
void s3d_draw_text(s3d_renderer* renderer, const char* text, float32 x, float32 y,
                   float32 scale, s3d_color color) {
    if (!renderer || !text) return;
    if (renderer->type == S3D_RENDERER_SOFTWARE) {
        s3d__draw_text_software(renderer, text, x, y, scale, color);
    }
}
