// Simple3DEngine - Voxel world module (Minecraft-style chunk meshing + raycast)
#include "../include/s3d_voxel.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define S3D_MAX_VERTS_PER_MESH 60000u

struct s3d_voxel_world {
    int32 sx, sy, sz;
    uint8* blocks;              // sx*sy*sz, id per cell
    s3d_mesh* opaque_mesh;      // pre-built visible-face mesh
    s3d_mesh* water_mesh;       // transparent layer drawn second
    uint32 opaque_tris, water_tris;
};

// ---------------------------------------------------------------------------
// Block styles (RGB in [0..1], Minecraft-ish palette)
static s3d_block_style g_styles[S3D_BLOCK_COUNT];
static bool32 g_styles_init = S3D_FALSE;

static s3d_color rgba(int r, int g, int b, float a) {
    s3d_color c; c.r = r / 255.0f; c.g = g / 255.0f; c.b = b / 255.0f; c.a = a; return c;
}

const s3d_block_style* s3d_voxel_block_styles(void) {
    if (!g_styles_init) {
        memset(g_styles, 0, sizeof(g_styles));
        // grass: green top, dirt+green side, dirt bottom
        g_styles[S3D_BLOCK_GRASS].top    = rgba(96, 168, 64, 1.0f);
        g_styles[S3D_BLOCK_GRASS].side   = rgba(134, 110, 78, 1.0f);
        g_styles[S3D_BLOCK_GRASS].bottom = rgba(122, 96, 66, 1.0f);
        // dirt
        g_styles[S3D_BLOCK_DIRT].top = g_styles[S3D_BLOCK_DIRT].side =
        g_styles[S3D_BLOCK_DIRT].bottom = rgba(128, 100, 68, 1.0f);
        // stone
        g_styles[S3D_BLOCK_STONE].top = rgba(140, 140, 144, 1.0f);
        g_styles[S3D_BLOCK_STONE].side = rgba(124, 124, 128, 1.0f);
        g_styles[S3D_BLOCK_STONE].bottom = rgba(110, 110, 114, 1.0f);
        // wood
        g_styles[S3D_BLOCK_WOOD].top    = rgba(168, 132, 80, 1.0f);
        g_styles[S3D_BLOCK_WOOD].side   = rgba(106, 82, 50, 1.0f);
        g_styles[S3D_BLOCK_WOOD].bottom = rgba(168, 132, 80, 1.0f);
        // leaves (semi-transparent)
        g_styles[S3D_BLOCK_LEAVES].top = g_styles[S3D_BLOCK_LEAVES].side =
        g_styles[S3D_BLOCK_LEAVES].bottom = rgba(52, 126, 44, 0.92f);
        g_styles[S3D_BLOCK_LEAVES].transparent = S3D_TRUE;
        // sand
        g_styles[S3D_BLOCK_SAND].top = g_styles[S3D_BLOCK_SAND].side =
        g_styles[S3D_BLOCK_SAND].bottom = rgba(216, 204, 150, 1.0f);
        // water (transparent)
        g_styles[S3D_BLOCK_WATER].top = g_styles[S3D_BLOCK_WATER].side =
        g_styles[S3D_BLOCK_WATER].bottom = rgba(52, 108, 204, 0.62f);
        g_styles[S3D_BLOCK_WATER].transparent = S3D_TRUE;
        // brick
        g_styles[S3D_BLOCK_BRICK].top = g_styles[S3D_BLOCK_BRICK].bottom = rgba(150, 90, 78, 1.0f);
        g_styles[S3D_BLOCK_BRICK].side = rgba(168, 84, 68, 1.0f);
        g_styles_init = S3D_TRUE;
    }
    return g_styles;
}

void s3d_voxel_set_block_style(s3d_block_id id, s3d_block_style style) {
    s3d_voxel_block_styles(); // ensure defaults exist
    if (id > 0 && id < S3D_BLOCK_COUNT) g_styles[id] = style;
}

// ---------------------------------------------------------------------------
s3d_voxel_world* s3d_voxel_create(int32 size_x, int32 size_y, int32 size_z) {
    if (size_x <= 0 || size_y <= 0 || size_z <= 0) return NULL;
    s3d_voxel_world* w = (s3d_voxel_world*)calloc(1, sizeof(*w));
    if (!w) return NULL;
    w->sx = size_x; w->sy = size_y; w->sz = size_z;
    w->blocks = (uint8*)calloc((size_t)size_x * (size_t)size_y * (size_t)size_z, 1);
    if (!w->blocks) { free(w); return NULL; }
    s3d_voxel_block_styles();
    return w;
}

void s3d_voxel_destroy(s3d_voxel_world* w) {
    if (!w) return;
    if (w->opaque_mesh) s3d_destroy_mesh(w->opaque_mesh);
    if (w->water_mesh)  s3d_destroy_mesh(w->water_mesh);
    free(w->blocks);
    free(w);
}

static inline int32 idx(const s3d_voxel_world* w, int32 x, int32 y, int32 z) {
    return (x * w->sy + y) * w->sz + z;
}

uint8 s3d_voxel_get(const s3d_voxel_world* w, int32 x, int32 y, int32 z) {
    if (!w || x < 0 || y < 0 || z < 0 || x >= w->sx || y >= w->sy || z >= w->sz) return 0;
    return w->blocks[idx(w, x, y, z)];
}

void s3d_voxel_set(s3d_voxel_world* w, int32 x, int32 y, int32 z, uint8 block) {
    if (!w || x < 0 || y < 0 || z < 0 || x >= w->sx || y >= w->sy || z >= w->sz) return;
    w->blocks[idx(w, x, y, z)] = block;
}

int32 s3d_voxel_triangle_count(const s3d_voxel_world* w) {
    if (!w) return 0;
    return (int32)(w->opaque_tris + w->water_tris);
}

// ---------------------------------------------------------------------------
// Terrain generation: layered value noise heightmap + water + trees.
static uint32 hash2(uint32 x, uint32 y, uint32 seed) {
    uint32 h = x * 374761393u + y * 668265263u + seed * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static float noise2(float x, float y, uint32 seed) {
    int32 x0 = (int32)floorf(x), y0 = (int32)floorf(y);
    float fx = x - (float)x0, fy = y - (float)y0;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    float a = (float)(hash2((uint32)x0,     (uint32)y0,     seed) & 1023) / 1023.0f;
    float b = (float)(hash2((uint32)x0 + 1, (uint32)y0,     seed) & 1023) / 1023.0f;
    float c = (float)(hash2((uint32)x0,     (uint32)y0 + 1, seed) & 1023) / 1023.0f;
    float d = (float)(hash2((uint32)x0 + 1, (uint32)y0 + 1, seed) & 1023) / 1023.0f;
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

static float fbm2(float x, float y, uint32 seed) {
    float v = 0.0f, amp = 0.5f, freq = 1.0f;
    for (int i = 0; i < 4; i++) {
        v += amp * noise2(x * freq, y * freq, seed + (uint32)i * 7919u);
        amp *= 0.5f; freq *= 2.0f;
    }
    return v;
}

static void plant_tree(s3d_voxel_world* w, int32 x, int32 base_y, int32 z) {
    int32 trunk = 4 + (int32)(hash2((uint32)x, (uint32)z, 4242u) % 3);
    for (int32 i = 1; i <= trunk; i++)
        s3d_voxel_set(w, x, base_y + i, z, S3D_BLOCK_WOOD);
    for (int32 dy = trunk - 2; dy <= trunk + 1; dy++) {
        int32 r = (dy >= trunk) ? 1 : 2;
        for (int32 dx = -r; dx <= r; dx++)
            for (int32 dz = -r; dz <= r; dz++) {
                if (dx == 0 && dz == 0 && dy <= trunk) continue;
                if (abs(dx) == r && abs(dz) == r && (hash2((uint32)(x+dx), (uint32)(z+dz), 77u) & 3) == 0)
                    continue; // ragged canopy corners
                if (s3d_voxel_get(w, x + dx, base_y + dy, z + dz) == S3D_BLOCK_AIR)
                    s3d_voxel_set(w, x + dx, base_y + dy, z + dz, S3D_BLOCK_LEAVES);
            }
    }
}

void s3d_voxel_generate_terrain(s3d_voxel_world* w, uint32 seed, int32 water_level) {
    if (!w) return;
    const float scale = 0.06f;
    for (int32 x = 0; x < w->sx; x++) {
        for (int32 z = 0; z < w->sz; z++) {
            float n = fbm2((float)x * scale, (float)z * scale, seed);
            int32 h = (int32)(2.0f + n * (float)(w->sy - 4));
            if (h < 1) h = 1;
            if (h > w->sy - 6) h = w->sy - 6;
            for (int32 y = 0; y <= h; y++) {
                uint8 b;
                if (y == 0)                       b = S3D_BLOCK_STONE;
                else if (y < h - 2)               b = S3D_BLOCK_STONE;
                else if (y < h)                   b = S3D_BLOCK_DIRT;
                else /* y == h */                 b = (h <= water_level) ? S3D_BLOCK_SAND : S3D_BLOCK_GRASS;
                s3d_voxel_set(w, x, y, z, b);
            }
            for (int32 y = h + 1; y <= water_level; y++)
                s3d_voxel_set(w, x, y, z, S3D_BLOCK_WATER);
            // trees on grass, away from edges
            if (h > water_level && x > 2 && z > 2 && x < w->sx - 3 && z < w->sz - 3) {
                if ((hash2((uint32)x, (uint32)z, seed + 5u) % 53u) == 0)
                    plant_tree(w, x, h, z);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Meshing: emit two triangles per exposed face into one of two buffers.
typedef struct {
    s3d_vertex verts[S3D_MAX_VERTS_PER_MESH];
    uint16 indices[S3D_MAX_VERTS_PER_MESH / 2 * 3];
    uint32 nv, ni;
} mesh_buf;

static void mb_face(mesh_buf* mb, const s3d_vertex quad[4], const s3d_color col) {
    if (mb->nv + 4 > S3D_MAX_VERTS_PER_MESH || mb->ni + 6 > sizeof(mb->indices) / sizeof(uint16))
        return; // mesh full; remaining faces skipped this rebuild
    uint16 base = (uint16)mb->nv;
    for (int i = 0; i < 4; i++) {
        s3d_vertex v = quad[i];
        v.r = col.r; v.g = col.g; v.b = col.b; v.a = col.a;
        v.u = (i == 1 || i == 2) ? 1.0f : 0.0f;
        v.v = (i >= 2) ? 1.0f : 0.0f;
        mb->verts[mb->nv++] = v;
    }
    mb->indices[mb->ni++] = base; mb->indices[mb->ni++] = base + 1; mb->indices[mb->ni++] = base + 2;
    mb->indices[mb->ni++] = base; mb->indices[mb->ni++] = base + 2; mb->indices[mb->ni++] = base + 3;
}

static bool32 face_visible(uint8 self, uint8 nb) {
    if (nb == 0) return S3D_TRUE;                          // neighbor is air
    const s3d_block_style* st = s3d_voxel_block_styles();
    if (st[nb].transparent && nb != self) return S3D_TRUE; // see through water/leaves
    return S3D_FALSE;
}

void s3d_voxel_rebuild_mesh(s3d_voxel_world* w) {
    if (!w) return;
    static mesh_buf opaque, transp; // ~4 MB but avoids heap churn per rebuild
    memset(&opaque, 0, sizeof(opaque));
    memset(&transp, 0, sizeof(transp));
    const s3d_block_style* st = s3d_voxel_block_styles();

    for (int32 x = 0; x < w->sx; x++)
    for (int32 y = 0; y < w->sy; y++)
    for (int32 z = 0; z < w->sz; z++) {
        uint8 b = w->blocks[idx(w, x, y, z)];
        if (b == 0) continue;
        const s3d_block_style* bs = &st[b];
        mesh_buf* mb = bs->transparent ? &transp : &opaque;
        float fx = (float)x, fy = (float)y, fz = (float)z;

        // Each face: 4 corner vertices, CCW seen from outside (+Y up).
        #define FACE(nb_x, nb_y, nb_z, q0, q1, q2, q3, col)                  \
            do {                                                              \
                if (face_visible(b, s3d_voxel_get(w, nb_x, nb_y, nb_z))) {    \
                    s3d_vertex q[4];                                          \
                    q[0].x=q0.x;q[0].y=q0.y;q[0].z=q0.z;                      \
                    q[1].x=q1.x;q[1].y=q1.y;q[1].z=q1.z;                      \
                    q[2].x=q2.x;q[2].y=q2.y;q[2].z=q2.z;                      \
                    q[3].x=q3.x;q[3].y=q3.y;q[3].z=q3.z;                      \
                    for (int i_ = 0; i_ < 4; i_++) { q[i_].r=q[i_].g=q[i_].b=q[i_].a=0; q[i_].u=q[i_].v=0; } \
                    mb_face(mb, q, col);                                      \
                }                                                             \
            } while (0)

        #define P(ax,ay,az) ((s3d_vec3){(ax),(ay),(az)})
        // +Y top
        FACE(x, y+1, z, P(fx,fy+1,fz), P(fx+1,fy+1,fz), P(fx+1,fy+1,fz+1), P(fx,fy+1,fz+1), bs->top);
        // -Y bottom
        FACE(x, y-1, z, P(fx,fy,fz+1), P(fx+1,fy,fz+1), P(fx+1,fy,fz), P(fx,fy,fz), bs->bottom);
        // +Z front
        FACE(x, y, z+1, P(fx+1,fy,fz+1), P(fx+1,fy+1,fz+1), P(fx,fy+1,fz+1), P(fx,fy,fz+1), bs->side);
        // -Z back
        FACE(x, y, z-1, P(fx,fy,fz), P(fx,fy+1,fz), P(fx+1,fy+1,fz), P(fx+1,fy,fz), bs->side);
        // +X right
        FACE(x+1, y, z, P(fx+1,fy,fz+1), P(fx+1,fy,fz), P(fx+1,fy+1,fz), P(fx+1,fy+1,fz+1), bs->side);
        // -X left
        FACE(x-1, y, z, P(fx,fy,fz), P(fx,fy,fz+1), P(fx,fy+1,fz+1), P(fx,fy+1,fz), bs->side);
        #undef P
        #undef FACE
    }

    if (w->opaque_mesh) { s3d_destroy_mesh(w->opaque_mesh); w->opaque_mesh = NULL; }
    if (w->water_mesh)  { s3d_destroy_mesh(w->water_mesh);  w->water_mesh  = NULL; }
    if (opaque.nv) {
        w->opaque_mesh = s3d_create_mesh(opaque.verts, opaque.nv, opaque.indices, opaque.ni);
        w->opaque_tris = opaque.ni / 3;
    } else w->opaque_tris = 0;
    if (transp.nv) {
        w->water_mesh = s3d_create_mesh(transp.verts, transp.nv, transp.indices, transp.ni);
        w->water_tris = transp.ni / 3;
    } else w->water_tris = 0;
}

void s3d_voxel_draw(s3d_voxel_world* w, s3d_renderer* renderer) {
    if (!w || !renderer) return;
    if (w->opaque_mesh) s3d_draw_mesh(renderer, w->opaque_mesh);
    if (w->water_mesh) {
        s3d_enable_blending(renderer, S3D_TRUE);
        s3d_draw_mesh(renderer, w->water_mesh);
        s3d_enable_blending(renderer, S3D_FALSE);
    }
}

// ---------------------------------------------------------------------------
// DDA voxel traversal (Amanatides & Woo, "Fast Voxel Traversal Algorithm")
bool32 s3d_voxel_raycast(const s3d_voxel_world* w,
                         s3d_vec3 origin, s3d_vec3 direction, float32 max_dist,
                         int32* out_x, int32* out_y, int32* out_z,
                         s3d_vec3* out_normal, uint8* out_block) {
    if (!w) return S3D_FALSE;
    float len = sqrtf(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (len < 1e-6f) return S3D_FALSE;
    direction.x /= len; direction.y /= len; direction.z /= len;

    int32 px = (int32)floorf(origin.x), py = (int32)floorf(origin.y), pz = (int32)floorf(origin.z);
    int32 stepx = direction.x > 0 ? 1 : (direction.x < 0 ? -1 : 0);
    int32 stepy = direction.y > 0 ? 1 : (direction.y < 0 ? -1 : 0);
    int32 stepz = direction.z > 0 ? 1 : (direction.z < 0 ? -1 : 0);

    float invx = direction.x != 0 ? fabsf(1.0f / direction.x) : 1e30f;
    float invy = direction.y != 0 ? fabsf(1.0f / direction.y) : 1e30f;
    float invz = direction.z != 0 ? fabsf(1.0f / direction.z) : 1e30f;

    float tx = (stepx > 0 ? (px + 1 - origin.x) : (origin.x - px)) * invx;
    float ty = (stepy > 0 ? (py + 1 - origin.y) : (origin.y - py)) * invy;
    float tz = (stepz > 0 ? (pz + 1 - origin.z) : (origin.z - pz)) * invz;

    s3d_vec3 normal = {0, 0, 0};
    float travelled = 0.0f;

    for (int guard = 0; guard < 4096; guard++) {
        uint8 b = s3d_voxel_get(w, px, py, pz);
        if (b != 0 && b != S3D_BLOCK_WATER) { // don't target water by default? keep: hit solid only
            if (out_x) *out_x = px;
            if (out_y) *out_y = py;
            if (out_z) *out_z = pz;
            if (out_normal) *out_normal = normal;
            if (out_block) *out_block = b;
            return S3D_TRUE;
        }
        if (tx < ty && tx < tz)      { travelled = tx; px += stepx; tx += invx; normal = (s3d_vec3){-stepx, 0, 0}; }
        else if (ty < tz)            { travelled = ty; py += stepy; ty += invy; normal = (s3d_vec3){0, -stepy, 0}; }
        else                         { travelled = tz; pz += stepz; tz += invz; normal = (s3d_vec3){0, 0, -stepz}; }
        if (travelled > max_dist) break;
        if (px < 0 || py < 0 || pz < 0 || px >= w->sx || py >= w->sy || pz >= w->sz) break;
    }
    return S3D_FALSE;
}
