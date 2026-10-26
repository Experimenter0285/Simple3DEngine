// Simple3DEngine - OpenGL 1.x / OpenGL ES 1.x fixed-function backend.
// Targets legacy hardware: only GL 1.0/1.1 + ES 1.0 API is used (immediate mode,
// matrix stacks, glLight*, glEnable(GL_TEXTURE_2D) or ES client-side textures).
// Compiled only when S3D_HAS_GL is defined by the build system; otherwise this
// file compiles to an inert stub so the engine links everywhere.

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

#ifdef S3D_HAS_GL

#if defined(S3D_USE_GLES1)
    #include <GLES/gl.h>
    #include <GLES/glext.h>
    // OpenGL ES 1.0 has no GL_TEXTURE_2D; textures are client-side and enabled
    // through the GL_OES_draw_texture / glDrawTextureNV paths or by binding a
    // pointer via glEnableClientState(GL_TEXTURE_COORD_ARRAY). We emulate the
    // bind by remembering the texture in bound_texture and drawing with manual
    // sampling fallback (see s3d_bind_texture below).
    #define S3D_TEX_TARGET 0
#else
    #define GL_GLEXT_LEGACY 1
    #include <GL/gl.h>
    #define S3D_TEX_TARGET GL_TEXTURE_2D
#endif

#define S3D_MAX_LIGHTS 4

typedef struct {
    s3d_renderer base;      // MUST be first
    GLuint tex_name;        // font atlas texture (desktop GL only)
    bool32 font_ready;
    float32 last_time;
} gl_renderer;

static inline gl_renderer* GLR(s3d_renderer* r) { return (gl_renderer*)r; }

// ---------------------------------------------------------------------------
// Font atlas: upload the built-in 8x8 font once as a white-on-transparent
// texture and draw text with textured quads (works on GL 1.0 & ES 1.0).
// ---------------------------------------------------------------------------
#define FONT_ATLAS_COLS 15               // 95 glyphs -> 15 x 7 grid
#define FONT_ATLAS_ROWS 7
#define FONT_ATLAS_W    (FONT_ATLAS_COLS * S3D_FONT_GLYPH_WIDTH)   // 120
#define FONT_ATLAS_H    (FONT_ATLAS_ROWS * S3D_FONT_GLYPH_HEIGHT)  // 56

static uint8_t* build_font_atlas(void) {
    // Pad to power of two for strict GL 1.0 drivers: 128x64.
    int aw = 128, ah = 64;
    uint8_t* atlas = (uint8_t*)calloc((size_t)aw * ah, 4);
    if (!atlas) return NULL;
    for (int i = 0; i < S3D_FONT_NUM_GLYPHS; i++) {
        int gx = (i % FONT_ATLAS_COLS) * S3D_FONT_GLYPH_WIDTH;
        int gy = (i / FONT_ATLAS_COLS) * S3D_FONT_GLYPH_HEIGHT;
        const uint8_t* glyph = s3d_font_8x8[i];
        for (int row = 0; row < S3D_FONT_GLYPH_HEIGHT; row++) {
            for (int col = 0; col < S3D_FONT_GLYPH_WIDTH; col++) {
                if (glyph[row] & (0x80 >> col)) {
                    size_t o = ((size_t)(gy + row) * aw + (gx + col)) * 4;
                    atlas[o+0] = 255; atlas[o+1] = 255; atlas[o+2] = 255; atlas[o+3] = 255;
                }
            }
        }
    }
    return atlas;
}

static void ensure_font(gl_renderer* g) {
    if (g->font_ready) return;
    uint8_t* atlas = build_font_atlas();
    if (!atlas) return;
    glGenTextures(1, &g->tex_name);
#if defined(S3D_USE_GLES1)
    glBindTexture(S3D_TEX_TARGET, g->tex_name);
    glColor4f(1,1,1,1);
    glTexImage2D(S3D_TEX_TARGET, 0, GL_RGBA, 128, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas);
#else
    glBindTexture(GL_TEXTURE_2D, g->tex_name);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas);
#endif
    free(atlas);
    g->font_ready = S3D_TRUE;
}

// ---------------------------------------------------------------------------
// Creation / destruction
// ---------------------------------------------------------------------------
s3d_renderer* s3d__create_renderer_gl(s3d_window* window, s3d_renderer_type gl_type) {
    gl_renderer* g = (gl_renderer*)calloc(1, sizeof(gl_renderer));
    if (!g) return NULL;

    g->base.type = gl_type;
    g->base.window = window;
    g->base.current_matrix = s3d_mat4_identity();
    g->base.clear_color = s3d_color_rgba(0.1f, 0.1f, 0.15f, 1.0f);
    g->base.depth_test_enabled = S3D_TRUE;

    s3d_make_context_current(window);

    const char* version = (const char*)glGetString(GL_VERSION);
    printf("Simple3DEngine: %s context active (GL version: %s)\n",
#if defined(S3D_USE_GLES1)
           "OpenGL ES 1.x",
#else
           "OpenGL 1.x",
#endif
           version ? version : "unknown");

    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);
    glEnable(GL_NORMALIZE); // legacy safety net for non-uniform scales
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    ensure_font(g);
    return &g->base;
}

void s3d_destroy_renderer(s3d_renderer* renderer) {
    if (!renderer) return;
    gl_renderer* g = GLR(renderer);
    if (g->font_ready) glDeleteTextures(1, &g->tex_name);
    free(g);
}

s3d_renderer_type s3d_get_active_renderer_type(s3d_renderer* renderer) {
    return renderer ? renderer->type : S3D_RENDERER_SOFTWARE;
}

// ---------------------------------------------------------------------------
// State / frame
// ---------------------------------------------------------------------------
void s3d_set_clear_color(s3d_renderer* renderer, s3d_color color) {
    if (!renderer) return;
    renderer->clear_color = color;
    glClearColor(color.r, color.g, color.b, color.a);
}

void s3d_clear(s3d_renderer* renderer) {
    if (!renderer) return;
    glClearColor(renderer->clear_color.r, renderer->clear_color.g,
                 renderer->clear_color.b, renderer->clear_color.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void s3d_renderer_begin_frame(s3d_renderer* renderer) {
    if (!renderer) return;
    renderer->stats.draw_calls = 0;
    renderer->stats.triangles_rendered = 0;
    renderer->stats.vertices_processed = 0;
    renderer->current_matrix = s3d_mat4_identity();
    renderer->matrix_stack_top = 0;
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void s3d_renderer_end_frame(s3d_renderer* renderer) {
    if (!renderer) return;
    // Presentation happens in s3d_swap_buffers() at the platform layer.
    glFlush();
}

// ---------------------------------------------------------------------------
// Matrix stack: mirror our software state into the real GL stacks
// ---------------------------------------------------------------------------
void s3d_push_matrix(s3d_renderer* renderer) {
    if (!renderer || renderer->matrix_stack_top >= S3D_MATRIX_STACK_SIZE) return;
    renderer->matrix_stack[renderer->matrix_stack_top++] = renderer->current_matrix;
    glPushMatrix();
}

void s3d_pop_matrix(s3d_renderer* renderer) {
    if (!renderer || renderer->matrix_stack_top <= 0) return;
    renderer->current_matrix = renderer->matrix_stack[--renderer->matrix_stack_top];
    glPopMatrix();
}

void s3d_load_matrix(s3d_renderer* renderer, s3d_mat4 matrix) {
    if (!renderer) return;
    renderer->current_matrix = matrix;
    glLoadMatrixf(matrix.m);
}

void s3d_mult_matrix(s3d_renderer* renderer, s3d_mat4 matrix) {
    if (!renderer) return;
    renderer->current_matrix = s3d_mat4_multiply(renderer->current_matrix, matrix);
    glMultMatrixf(matrix.m);
}

// The engine keeps a shadow copy of the modelview matrix (current_matrix) so the
// same scene code works on both backends; every call updates both copies.
void s3d_translate(s3d_renderer* renderer, float32 x, float32 y, float32 z) {
    if (!renderer) return;
    glTranslatef(x, y, z);
    renderer->current_matrix = s3d_mat4_multiply(renderer->current_matrix, s3d_mat4_translate(x, y, z));
}

void s3d_rotate(s3d_renderer* renderer, float32 angle_degrees, float32 x, float32 y, float32 z) {
    if (!renderer) return;
    glRotatef(angle_degrees, x, y, z);

    float32 a = angle_degrees * (float32)M_PI / 180.0f;
    float32 len = sqrtf(x*x + y*y + z*z);
    if (len < 1e-8f) return;
    x /= len; y /= len; z /= len;
    float32 c = cosf(a), s = sinf(a), t = 1.0f - c;
    s3d_mat4 m = s3d_mat4_identity();
    m.m[0]=t*x*x+c;   m.m[4]=t*x*y-s*z;  m.m[8]=t*x*z+s*y;
    m.m[1]=t*x*y+s*z; m.m[5]=t*y*y+c;    m.m[9]=t*y*z-s*x;
    m.m[2]=t*x*z-s*y; m.m[6]=t*y*z+s*x;  m.m[10]=t*z*z+c;
    renderer->current_matrix = s3d_mat4_multiply(renderer->current_matrix, m);
}

void s3d_scale(s3d_renderer* renderer, float32 x, float32 y, float32 z) {
    if (!renderer) return;
    glScalef(x, y, z);
    renderer->current_matrix = s3d_mat4_multiply(renderer->current_matrix, s3d_mat4_scale(x, y, z));
}

// ---------------------------------------------------------------------------
// Projection
// ---------------------------------------------------------------------------
void s3d_setup_3d_projection(s3d_renderer* renderer, s3d_camera* camera, float32 aspect_ratio) {
    if (!renderer || !camera) return;
    camera->proj_matrix = s3d_mat4_perspective(camera->fov, aspect_ratio,
                                               camera->near_plane, camera->far_plane);
    camera->view_matrix = s3d_mat4_look_at(camera->position, camera->target, camera->up);

    int32 w, h; s3d_get_window_size(renderer->window, &w, &h);
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glLoadMatrixf(camera->proj_matrix.m);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glLoadMatrixf(camera->view_matrix.m);
    renderer->current_matrix = s3d_mat4_identity();
    renderer->matrix_stack_top = 0;
}

void s3d_setup_2d_projection(s3d_renderer* renderer, int32 width, int32 height) {
    if (!renderer) return;
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    renderer->current_matrix = s3d_mat4_identity();
    renderer->matrix_stack_top = 0;
}

// ---------------------------------------------------------------------------
// Primitives (immediate mode)
// ---------------------------------------------------------------------------
void s3d_draw_point(s3d_renderer* renderer, s3d_vec3 pos, s3d_color color, float32 size) {
    if (!renderer) return;
    glPointSize(size > 0 ? size : 1.0f);
    glColor4f(color.r, color.g, color.b, color.a);
    glBegin(GL_POINTS);
    glVertex3f(pos.x, pos.y, pos.z);
    glEnd();
    renderer->stats.draw_calls++;
    renderer->stats.vertices_processed++;
}

void s3d_draw_line(s3d_renderer* renderer, s3d_vec3 start, s3d_vec3 end, s3d_color color, float32 width) {
    if (!renderer) return;
    glLineWidth(width > 0 ? width : 1.0f);
    glColor4f(color.r, color.g, color.b, color.a);
    glBegin(GL_LINES);
    glVertex3f(start.x, start.y, start.z);
    glVertex3f(end.x, end.y, end.z);
    glEnd();
    renderer->stats.draw_calls++;
    renderer->stats.vertices_processed += 2;
}

void s3d_draw_triangle(s3d_renderer* renderer, s3d_triangle tri, bool32 filled) {
    if (!renderer) return;
    glBegin(filled ? GL_TRIANGLES : GL_LINE_LOOP);
    glColor4f(tri.v1.r, tri.v1.g, tri.v1.b, tri.v1.a); glTexCoord2f(tri.v1.u, tri.v1.v); glVertex3f(tri.v1.x, tri.v1.y, tri.v1.z);
    glColor4f(tri.v2.r, tri.v2.g, tri.v2.b, tri.v2.a); glTexCoord2f(tri.v2.u, tri.v2.v); glVertex3f(tri.v2.x, tri.v2.y, tri.v2.z);
    glColor4f(tri.v3.r, tri.v3.g, tri.v3.b, tri.v3.a); glTexCoord2f(tri.v3.u, tri.v3.v); glVertex3f(tri.v3.x, tri.v3.y, tri.v3.z);
    glEnd();
    renderer->stats.draw_calls++;
    renderer->stats.vertices_processed += 3;
    if (filled) renderer->stats.triangles_rendered++;
}

void s3d_draw_rect(s3d_renderer* renderer, s3d_rect rect, s3d_color color, bool32 filled) {
    if (!renderer) return;
    glColor4f(color.r, color.g, color.b, color.a);
    glBegin(filled ? GL_QUADS : GL_LINE_STRIP);
    glVertex2f(rect.x, rect.y);
    glVertex2f(rect.x + rect.width, rect.y);
    glVertex2f(rect.x + rect.width, rect.y + rect.height);
    glVertex2f(rect.x, rect.y + rect.height);
    if (!filled) glVertex2f(rect.x, rect.y);
    glEnd();
    renderer->stats.draw_calls++;
    renderer->stats.vertices_processed += 4;
}

void s3d_draw_circle(s3d_renderer* renderer, s3d_vec2 center, float32 radius, s3d_color color, int32 segments) {
    if (!renderer || segments <= 2) return;
    glColor4f(color.r, color.g, color.b, color.a);
    glBegin(GL_LINE_LOOP);
    for (int32 i = 0; i < segments; i++) {
        float32 a = 2.0f * (float32)M_PI * i / segments;
        glVertex2f(center.x + cosf(a) * radius, center.y + sinf(a) * radius);
    }
    glEnd();
    renderer->stats.draw_calls++;
    renderer->stats.vertices_processed += segments;
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
    if (mesh) { free(mesh->vertices); free(mesh->indices); free(mesh); }
}

void s3d_draw_mesh(s3d_renderer* renderer, s3d_mesh* mesh) {
    if (!renderer || !mesh) return;
    // Immediate mode keeps us compatible with any GL 1.0 / ES 1.0 driver,
    // including software rasterizers like Mesa's llvmpipe/softpipe or VideoCore.
    glBegin(GL_TRIANGLES);
    if (mesh->indices && mesh->index_count >= 3) {
        for (uint32 i = 0; i + 2 < mesh->index_count; i += 3) {
            const s3d_vertex* vs[3] = {&mesh->vertices[mesh->indices[i]],
                                       &mesh->vertices[mesh->indices[i+1]],
                                       &mesh->vertices[mesh->indices[i+2]]};
            for (int k = 0; k < 3; k++) {
                glColor4f(vs[k]->r, vs[k]->g, vs[k]->b, vs[k]->a);
                glTexCoord2f(vs[k]->u, vs[k]->v);
                glVertex3f(vs[k]->x, vs[k]->y, vs[k]->z);
            }
        }
    } else {
        for (uint32 i = 0; i + 2 < mesh->vertex_count; i += 3) {
            for (int k = 0; k < 3; k++) {
                const s3d_vertex* v = &mesh->vertices[i + k];
                glColor4f(v->r, v->g, v->b, v->a);
                glTexCoord2f(v->u, v->v);
                glVertex3f(v->x, v->y, v->z);
            }
        }
    }
    glEnd();
    renderer->stats.draw_calls++;
    renderer->stats.triangles_rendered += (mesh->index_count ? mesh->index_count : mesh->vertex_count) / 3;
    renderer->stats.vertices_processed += (mesh->index_count ? mesh->index_count : mesh->vertex_count);
}

s3d_mesh* s3d_create_cube(float32 size) {
    float32 h = size * 0.5f;
    s3d_vertex v[] = {
        {-h,-h, h, 1,1,1,1, 0,1}, { h,-h, h, 1,1,1,1, 1,1}, { h, h, h, 1,1,1,1, 1,0}, {-h, h, h, 1,1,1,1, 0,0},
        { h,-h,-h, .7f,.7f,.7f,1, 0,1}, {-h,-h,-h, .7f,.7f,.7f,1, 1,1}, {-h, h,-h, .7f,.7f,.7f,1, 1,0}, { h, h,-h, .7f,.7f,.7f,1, 0,0},
        { h,-h, h, .9f,.9f,.9f,1, 0,1}, { h,-h,-h, .9f,.9f,.9f,1, 1,1}, { h, h,-h, .9f,.9f,.9f,1, 1,0}, { h, h, h, .9f,.9f,.9f,1, 0,0},
        {-h,-h,-h, .6f,.6f,.6f,1, 0,1}, {-h,-h, h, .6f,.6f,.6f,1, 1,1}, {-h, h, h, .6f,.6f,.6f,1, 1,0}, {-h, h,-h, .6f,.6f,.6f,1, 0,0},
        {-h, h, h, 1,1,.8f,1, 0,0}, { h, h, h, 1,1,.8f,1, 1,0}, { h, h,-h, 1,1,.8f,1, 1,1}, {-h, h,-h, 1,1,.8f,1, 0,1},
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
        float32 phi = (float32)st / stacks * (float32)M_PI;
        for (int32 sl = 0; sl <= slices; sl++) {
            float32 th = (float32)sl / slices * 2.0f * (float32)M_PI;
            float32 x = sinf(phi) * cosf(th), y = cosf(phi), z = sinf(phi) * sinf(th);
            float32 shade = 0.45f + 0.55f * fabsf(y * 0.5f + x * 0.5f);
            s3d_vertex vv;
            vv.x = radius*x; vv.y = radius*y; vv.z = radius*z;
            vv.r = shade; vv.g = shade; vv.b = shade; vv.a = 1.0f;
            vv.u = (float32)sl / slices; vv.v = (float32)st / stacks;
            verts[vi++] = vv;
        }
    }
    uint32 ii = 0;
    int32 stride = slices + 1;
    for (int32 st = 0; st < stacks; st++)
        for (int32 sl = 0; sl < slices; sl++) {
            uint16 a = (uint16)(st*stride+sl), b = (uint16)(a+1), c = (uint16)(a+stride), d = (uint16)(c+1);
            inds[ii++]=a; inds[ii++]=c; inds[ii++]=b;
            inds[ii++]=b; inds[ii++]=c; inds[ii++]=d;
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
    if (texture) { free(texture->data); free(texture); }
}

void s3d_bind_texture(s3d_renderer* renderer, s3d_texture* texture) {
    if (!renderer) return;
    renderer->bound_texture = texture;
#if !defined(S3D_USE_GLES1)
    if (texture) {
        glBindTexture(GL_TEXTURE_2D, texture->id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture->width, texture->height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, texture->data);
        glEnable(GL_TEXTURE_2D);
    } else {
        glDisable(GL_TEXTURE_2D);
    }
#else
    // ES 1.0: textures are referenced by name through the fixed pipeline as well
    // (glBindTexture exists in ES 1.0 with GL_TEXTURE_2D == 0x0DE1 from glext).
    if (texture) {
        glBindTexture(GL_TEXTURE_2D_OES, texture->id);
        glTexParameteri(GL_TEXTURE_2D_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_OES, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D_OES, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glTexImage2D(GL_TEXTURE_2D_OES, 0, GL_RGBA, texture->width, texture->height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, texture->data);
        glEnable(GL_TEXTURE_2D_OES);
    } else {
        glDisable(GL_TEXTURE_2D_OES);
    }
#endif
}

void s3d_draw_textured_rect(s3d_renderer* renderer, s3d_rect rect, s3d_texture* texture, s3d_color tint) {
    if (!renderer) return;
    if (!texture || !texture->loaded) { s3d_draw_rect(renderer, rect, tint, S3D_TRUE); return; }
    s3d_bind_texture(renderer, texture);
    glColor4f(tint.r, tint.g, tint.b, tint.a);
    glBegin(GL_QUADS);
    glTexCoord2f(0,0); glVertex2f(rect.x, rect.y);
    glTexCoord2f(1,0); glVertex2f(rect.x + rect.width, rect.y);
    glTexCoord2f(1,1); glVertex2f(rect.x + rect.width, rect.y + rect.height);
    glTexCoord2f(0,1); glVertex2f(rect.x, rect.y + rect.height);
    glEnd();
    renderer->stats.draw_calls++;
    renderer->stats.vertices_processed += 4;
}

// ---------------------------------------------------------------------------
// Fixed-function lighting (the whole point of targeting GL 1.0 / ES 1.0)
// ---------------------------------------------------------------------------
void s3d_enable_lighting(s3d_renderer* renderer, bool32 enable) {
    if (!renderer) return;
    renderer->lighting_enabled = enable;
    if (enable) glEnable(GL_LIGHTING); else glDisable(GL_LIGHTING);
}

void s3d_set_light_position(s3d_renderer* renderer, int32 light_index, float32 x, float32 y, float32 z, float32 w) {
    if (!renderer || light_index < 0 || light_index >= S3D_MAX_LIGHTS) return;
    GLfloat pos[4] = {x, y, z, w};
    glLightfv((GLenum)(GL_LIGHT0 + light_index), GL_POSITION, pos);
}

void s3d_set_light_color(s3d_renderer* renderer, int32 light_index, s3d_color ambient, s3d_color diffuse, s3d_color specular) {
    if (!renderer || light_index < 0 || light_index >= S3D_MAX_LIGHTS) return;
    GLenum light = (GLenum)(GL_LIGHT0 + light_index);
    GLfloat a[4] = {ambient.r, ambient.g, ambient.b, ambient.a};
    GLfloat d[4] = {diffuse.r, diffuse.g, diffuse.b, diffuse.a};
    GLfloat s[4] = {specular.r, specular.g, specular.b, specular.a};
    glLightfv(light, GL_AMBIENT, a);
    glLightfv(light, GL_DIFFUSE, d);
    glLightfv(light, GL_SPECULAR, s);
    glEnable(light);
}

void s3d_set_material(s3d_renderer* renderer, s3d_color ambient, s3d_color diffuse, s3d_color specular, float32 shininess) {
    if (!renderer) return;
    GLfloat a[4] = {ambient.r, ambient.g, ambient.b, ambient.a};
    GLfloat d[4] = {diffuse.r, diffuse.g, diffuse.b, diffuse.a};
    GLfloat s[4] = {specular.r, specular.g, specular.b, specular.a};
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, a);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, d);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, s);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, shininess);
}

// ---------------------------------------------------------------------------
// Depth / blending / stats
// ---------------------------------------------------------------------------
void s3d_enable_depth_test(s3d_renderer* renderer, bool32 enable) {
    if (!renderer) return;
    renderer->depth_test_enabled = enable;
    if (enable) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
}

void s3d_clear_depth(s3d_renderer* renderer) {
    if (!renderer) return;
    glClear(GL_DEPTH_BUFFER_BIT);
}

void s3d_enable_blending(s3d_renderer* renderer, bool32 enable) {
    if (!renderer) return;
    renderer->blending_enabled = enable;
    if (enable) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
}

void s3d_get_stats(s3d_renderer* renderer, s3d_render_stats* stats) {
    if (renderer && stats) *stats = renderer->stats;
}

// ---------------------------------------------------------------------------
// Text via the font-atlas quad path
// ---------------------------------------------------------------------------
void s3d_draw_text(s3d_renderer* renderer, const char* text, float32 x, float32 y,
                   float32 scale, s3d_color color) {
    if (!renderer || !text) return;
    if (scale < 1.0f) scale = 1.0f;

    gl_renderer* g = GLR(renderer);
    ensure_font(g);

    glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT);
    glEnable(S3D_TEX_TARGET);
    glBindTexture(S3D_TEX_TARGET, g->tex_name);
    glTexParameteri(S3D_TEX_TARGET, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(S3D_TEX_TARGET, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(color.r, color.g, color.b, color.a);

    const float32 u_step = (float32)S3D_FONT_GLYPH_WIDTH / 128.0f;
    const float32 v_step = (float32)S3D_FONT_GLYPH_HEIGHT / 64.0f;
    const float32 gw = S3D_FONT_GLYPH_WIDTH * scale;
    const float32 gh = S3D_FONT_GLYPH_HEIGHT * scale;

    float32 cursor_x = x;
    glBegin(GL_QUADS);
    for (const char* p = text; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch == '\n') { cursor_x = x; y += gh; continue; }
        if (ch < S3D_FONT_FIRST_CHAR || ch > S3D_FONT_LAST_CHAR) { cursor_x += gw; continue; }
        int gi = ch - S3D_FONT_FIRST_CHAR;
        float32 u0 = (gi % FONT_ATLAS_COLS) * u_step;
        float32 v0 = (gi / FONT_ATLAS_COLS) * v_step;
        glTexCoord2f(u0, v0);          glVertex2f(cursor_x, y);
        glTexCoord2f(u0 + u_step, v0); glVertex2f(cursor_x + gw, y);
        glTexCoord2f(u0 + u_step, v0 + v_step); glVertex2f(cursor_x + gw, y + gh);
        glTexCoord2f(u0, v0 + v_step); glVertex2f(cursor_x, y + gh);
        cursor_x += gw;
    }
    glEnd();
    glPopAttrib();
    renderer->stats.draw_calls++;
}

#else // !S3D_HAS_GL -- inert stub so the file always compiles

s3d_renderer* s3d__create_renderer_gl(s3d_window* window, s3d_renderer_type gl_type) {
    S3D_UNUSED(window); S3D_UNUSED(gl_type);
    fprintf(stderr, "Simple3DEngine: built without OpenGL support\n");
    return NULL;
}

#endif // S3D_HAS_GL
