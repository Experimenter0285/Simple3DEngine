// Simple3DEngine - Voxel world module (Minecraft-style, chunked & infinite)
//
// The world is an unbounded XZ plane divided into 16 x H x 16 chunks.
// Terrain is generated lazily around the camera (streaming) and distant
// chunks are unloaded. Edits are stored in a per-column override map so
// they survive chunk unload/reload. A finite world (back-compat API) is
// simply an infinite world with generation bounds.
#include "../include/s3d_voxel.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_VERTS_PER_CHUNK   4096u            // hard cap; dense chunks clip extra faces
#define MAX_IDX_PER_CHUNK     (MAX_VERTS_PER_CHUNK / 2 * 3)
#define EDIT_TABLE_CAP        8192             // edited columns kept in memory
#define MESH_TABLE_CAP        121              // prime -> good open-addressing spread

typedef struct {                 // one override entry per edited (x,z) column
    int32 cx, cz;
    int32* ys;                   // local y list
    uint8* ids;                  // block id per y (0 = air carve-out)
    int32 count, cap;
    bool32 used;
} edit_col;

typedef struct {                 // mesh cache entry for a chunk
    int32 cx, cz;
    s3d_mesh* opaque;
    s3d_mesh* transp;
    uint32 tris;
    bool32 used;
} mesh_slot;

struct s3d_voxel_world {
    bool32 infinite;
    int32 height;                // Y extent (both modes)
    int32 water_level;
    uint32 seed;
    int32 fx0, fz0, fx1, fz1;    // finite-mode inclusive bounds

    edit_col edits[EDIT_TABLE_CAP];
    mesh_slot meshes[MESH_TABLE_CAP];
    int32 load_radius;           // chunks around the camera
    float gen_budget_left;       // seconds of generation budget this frame
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
        g_styles[S3D_BLOCK_GRASS].top    = rgba(96, 168, 64, 1.0f);
        g_styles[S3D_BLOCK_GRASS].side   = rgba(134, 110, 78, 1.0f);
        g_styles[S3D_BLOCK_GRASS].bottom = rgba(122, 96, 66, 1.0f);
        g_styles[S3D_BLOCK_DIRT].top = g_styles[S3D_BLOCK_DIRT].side =
        g_styles[S3D_BLOCK_DIRT].bottom = rgba(128, 100, 68, 1.0f);
        g_styles[S3D_BLOCK_STONE].top = rgba(140, 140, 144, 1.0f);
        g_styles[S3D_BLOCK_STONE].side = rgba(124, 124, 128, 1.0f);
        g_styles[S3D_BLOCK_STONE].bottom = rgba(110, 110, 114, 1.0f);
        g_styles[S3D_BLOCK_WOOD].top    = rgba(168, 132, 80, 1.0f);
        g_styles[S3D_BLOCK_WOOD].side   = rgba(106, 82, 50, 1.0f);
        g_styles[S3D_BLOCK_WOOD].bottom = rgba(168, 132, 80, 1.0f);
        g_styles[S3D_BLOCK_LEAVES].top = g_styles[S3D_BLOCK_LEAVES].side =
        g_styles[S3D_BLOCK_LEAVES].bottom = rgba(52, 126, 44, 0.92f);
        g_styles[S3D_BLOCK_LEAVES].transparent = S3D_TRUE;
        g_styles[S3D_BLOCK_SAND].top = g_styles[S3D_BLOCK_SAND].side =
        g_styles[S3D_BLOCK_SAND].bottom = rgba(216, 204, 150, 1.0f);
        g_styles[S3D_BLOCK_WATER].top = g_styles[S3D_BLOCK_WATER].side =
        g_styles[S3D_BLOCK_WATER].bottom = rgba(52, 108, 204, 0.62f);
        g_styles[S3D_BLOCK_WATER].transparent = S3D_TRUE;
        g_styles[S3D_BLOCK_BRICK].top = g_styles[S3D_BLOCK_BRICK].bottom = rgba(150, 90, 78, 1.0f);
        g_styles[S3D_BLOCK_BRICK].side = rgba(168, 84, 68, 1.0f);
        g_styles_init = S3D_TRUE;
    }
    return g_styles;
}

void s3d_voxel_set_block_style(s3d_block_id id, s3d_block_style style) {
    s3d_voxel_block_styles();
    if (id > 0 && id < S3D_BLOCK_COUNT) g_styles[id] = style;
}

// ---------------------------------------------------------------------------
// Deterministic noise (same result regardless of chunk streaming state)
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

#define TERRAIN_SCALE 0.045f

static int32 terrain_height_at(uint32 seed, int32 x, int32 z, int32 height) {
    float n = fbm2((float)x * TERRAIN_SCALE, (float)z * TERRAIN_SCALE, seed);
    int32 h = (int32)(2.0f + n * (float)(height - 6));
    if (h < 1) h = 1;
    if (h > height - 6) h = height - 6;
    return h;
}

int32 s3d_voxel_terrain_height(const s3d_voxel_world* w, int32 x, int32 z) {
    if (!w) return 0;
    if (!w->infinite && (x < w->fx0 || x > w->fx1 || z < w->fz0 || z > w->fz1)) return -1;
    return terrain_height_at(w->seed, x, z, w->height);
}

static bool32 in_bounds_gen(const s3d_voxel_world* w, int32 x, int32 z) {
    if (w->infinite) return S3D_TRUE;
    return (x >= w->fx0 && x <= w->fx1 && z >= w->fz0 && z <= w->fz1) ? S3D_TRUE : S3D_FALSE;
}

// Trees exist only at anchor columns; canopy is painted by iterating nearby
// anchors, which makes block queries order-independent and streamable.
static bool32 tree_anchor_at(uint32 seed, int32 x, int32 z) {
    return (hash2((uint32)x, (uint32)z, seed + 5u) % 71u) == 0;
}

// ---------------------------------------------------------------------------
// Edit overrides (sparse, per column)
static edit_col* edit_find_or_add(s3d_voxel_world* w, int32 cx, int32 cz, bool32 create) {
    uint32 hv = (((uint32)cx * 73856093u) ^ ((uint32)cz * 19349663u)) % EDIT_TABLE_CAP;
    for (uint32 p = 0; p < EDIT_TABLE_CAP; p++) {
        edit_col* e = &w->edits[(hv + p) % EDIT_TABLE_CAP];
        if (!e->used) {
            if (!create) return NULL;
            e->used = S3D_TRUE; e->cx = cx; e->cz = cz;
            return e;
        }
        if (e->cx == cx && e->cz == cz) return e;
    }
    return NULL; // table full: edit dropped (rare)
}

static void edit_set(s3d_voxel_world* w, int32 x, int32 y, int32 z, uint8 block) {
    edit_col* e = edit_find_or_add(w, x, z, S3D_TRUE);
    if (!e) return;
    for (int32 i = 0; i < e->count; i++)
        if (e->ys[i] == y) { e->ids[i] = block; return; }
    if (e->count >= e->cap) {
        int32 ncap = e->cap ? e->cap * 2 : 16;
        if (ncap > w->height) ncap = w->height;
        int32* ys = (int32*)realloc(e->ys, (size_t)ncap * sizeof(int32));
        if (!ys) return;
        uint8* ids = (uint8*)realloc(e->ids, (size_t)ncap);
        if (!ids) { e->ys = ys; return; }
        e->ys = ys; e->ids = ids; e->cap = ncap;
    }
    e->ys[e->count] = y; e->ids[e->count] = block; e->count++;
}

static bool32 edit_get(const s3d_voxel_world* cw, int32 x, int32 y, int32 z, uint8* out) {
    s3d_voxel_world* w = (s3d_voxel_world*)cw; // lookup never mutates
    const edit_col* e = edit_find_or_add(w, x, z, S3D_FALSE);
    if (!e) return S3D_FALSE;
    for (int32 i = 0; i < e->count; i++)
        if (e->ys[i] == y) { *out = e->ids[i]; return S3D_TRUE; }
    return S3D_FALSE;
}

// ---------------------------------------------------------------------------
// Procedural block query: pure function of (world, x, y, z) + edits.
static uint8 gen_block_at(const s3d_voxel_world* w, int32 x, int32 y, int32 z) {
    if (y < 0 || y >= w->height) return 0;
    if (!in_bounds_gen(w, x, z)) return 0;
    int32 h = terrain_height_at(w->seed, x, z, w->height);
    uint8 b = 0;
    if (y <= h) {
        if (y == h)            b = (h <= w->water_level) ? S3D_BLOCK_SAND : S3D_BLOCK_GRASS;
        else if (y >= h - 2)   b = S3D_BLOCK_DIRT;
        else                   b = S3D_BLOCK_STONE;
    } else if (y <= w->water_level) {
        b = S3D_BLOCK_WATER;
    }
    if (b == 0 || b == S3D_BLOCK_WATER) {
        for (int dx = -3; dx <= 3; dx++)
        for (int dz = -3; dz <= 3; dz++) {
            int ax = x + dx, az = z + dz;
            if (!tree_anchor_at(w->seed, ax, az)) continue;
            int ah = terrain_height_at(w->seed, ax, az, w->height);
            if (ah <= w->water_level) continue;
            int trunk = 4 + (int)(hash2((uint32)ax, (uint32)az, 4242u) % 3);
            if (dx == 0 && dz == 0 && y > ah && y <= ah + trunk) return S3D_BLOCK_WOOD;
            int dy = y - ah;
            if (dy >= trunk - 1 && dy <= trunk + 1) {
                int r = (dy >= trunk) ? 1 : 2;
                if (abs(dx) <= r && abs(dz) <= r &&
                    !((abs(dx) == r && abs(dz) == r && (hash2((uint32)x, (uint32)z, 77u) & 3) == 0)))
                    return S3D_BLOCK_LEAVES;
            }
        }
    }
    return b;
}

uint8 s3d_voxel_get(const s3d_voxel_world* w, int32 x, int32 y, int32 z) {
    if (!w) return 0;
    uint8 e;
    if (edit_get(w, x, y, z, &e)) return e;
    return gen_block_at(w, x, y, z);
}

void s3d_voxel_set(s3d_voxel_world* w, int32 x, int32 y, int32 z, uint8 block) {
    if (!w || y < 0 || y >= w->height) return;
    if (!w->infinite && (x < w->fx0 || x > w->fx1 || z < w->fz0 || z > w->fz1)) return;
    edit_set(w, x, y, z, block);
}

void s3d_voxel_for_each_in_aabb(s3d_voxel_world* w, int32 x0, int32 y0, int32 z0,
                                int32 x1, int32 y1, int32 z1,
                                s3d_voxel_edit_fn fn, void* user) {
    if (!w || !fn) return;
    for (int32 x = x0; x <= x1; x++)
    for (int32 y = y0; y <= y1; y++)
    for (int32 z = z0; z <= z1; z++)
        fn(x, y, z, user);
}

// ---------------------------------------------------------------------------
// Meshing per chunk with neighbor-aware face culling.
typedef struct {
    s3d_vertex* verts; uint32 nv;
    uint16* idx;     uint32 ni;
    s3d_color col;
} mesh_buf;

static void mb_face(mesh_buf* mb, const float quad[4][3]) {
    if (mb->nv + 4 > MAX_VERTS_PER_CHUNK || mb->ni + 6 > MAX_IDX_PER_CHUNK) return;
    uint16 base = (uint16)mb->nv;
    for (int i = 0; i < 4; i++) {
        s3d_vertex v;
        v.x = quad[i][0]; v.y = quad[i][1]; v.z = quad[i][2];
        v.r = mb->col.r; v.g = mb->col.g; v.b = mb->col.b; v.a = mb->col.a;
        v.u = (i == 1 || i == 2) ? 1.0f : 0.0f;
        v.v = (i >= 2) ? 1.0f : 0.0f;
        mb->verts[mb->nv++] = v;
    }
    mb->idx[mb->ni++] = base; mb->idx[mb->ni++] = base + 1; mb->idx[mb->ni++] = base + 2;
    mb->idx[mb->ni++] = base; mb->idx[mb->ni++] = base + 2; mb->idx[mb->ni++] = base + 3;
}

static bool32 face_visible(uint8 self, uint8 nb) {
    if (nb == 0) return S3D_TRUE;
    const s3d_block_style* st = s3d_voxel_block_styles();
    if (st[nb].transparent && nb != self) return S3D_TRUE;
    return S3D_FALSE;
}

static void mesh_chunk(s3d_voxel_world* w, int32 cx, int32 cz,
                       s3d_vertex* ov, uint32* onv, uint16* oi, uint32* oni,
                       s3d_vertex* tv, uint32* tnvv, uint16* ti, uint32* tini) {
    mesh_buf opaque = { ov, 0, oi, 0, {0,0,0,0} };
    mesh_buf transp = { tv, 0, ti, 0, {0,0,0,0} };
    const s3d_block_style* st = s3d_voxel_block_styles();

    for (int lx = 0; lx < S3D_CHUNK_SIZE; lx++)
    for (int lz = 0; lz < S3D_CHUNK_SIZE; lz++)
    for (int32 y = 0; y < w->height; y++) {
        int32 x = cx + lx, z = cz + lz;
        uint8 b = s3d_voxel_get(w, x, y, z);
        if (b == 0) continue;
        const s3d_block_style* bs = &st[b];
        mesh_buf* mb = bs->transparent ? &transp : &opaque;
        float fx = (float)x, fy = (float)y, fz = (float)z;

        #define TRYFACE(nb_, col_, q_) do {                                        \
            mb->col = col_;                                                        \
            if (face_visible(b, s3d_voxel_get(w, (nb_)[0], (nb_)[1], (nb_)[2])))   \
                mb_face(mb, q_);                                                   \
        } while (0)

        { int n[3] = {x, y+1, z};   float q[4][3] = {{fx,fy+1,fz},{fx+1,fy+1,fz},{fx+1,fy+1,fz+1},{fx,fy+1,fz+1}};         TRYFACE(n, bs->top, q); }
        { int n[3] = {x, y-1, z};   float q[4][3] = {{fx,fy,fz+1},{fx+1,fy,fz+1},{fx+1,fy,fz},{fx,fy,fz}};                 TRYFACE(n, bs->bottom, q); }
        { int n[3] = {x, y, z+1};   float q[4][3] = {{fx+1,fy,fz+1},{fx+1,fy+1,fz+1},{fx,fy+1,fz+1},{fx,fy,fz+1}};         TRYFACE(n, bs->side, q); }
        { int n[3] = {x, y, z-1};   float q[4][3] = {{fx,fy,fz},{fx,fy+1,fz},{fx+1,fy+1,fz},{fx+1,fy,fz}};                 TRYFACE(n, bs->side, q); }
        { int n[3] = {x+1, y, z};   float q[4][3] = {{fx+1,fy,fz+1},{fx+1,fy,fz},{fx+1,fy+1,fz},{fx+1,fy+1,fz+1}};         TRYFACE(n, bs->side, q); }
        { int n[3] = {x-1, y, z};   float q[4][3] = {{fx,fy,fz},{fx,fy,fz+1},{fx,fy+1,fz+1},{fx,fy+1,fz}};                 TRYFACE(n, bs->side, q); }
        #undef TRYFACE
    }
    *onv = opaque.nv; *oni = opaque.ni; *tnvv = transp.nv; *tini = transp.ni;
}

// ---------------------------------------------------------------------------
static mesh_slot* slot_find(s3d_voxel_world* w, int32 cx, int32 cz, bool32 create) {
    uint32 hv = (((uint32)cx * 73856093u) ^ ((uint32)cz * 19349663u)) % MESH_TABLE_CAP;
    for (uint32 p = 0; p < MESH_TABLE_CAP; p++) {
        mesh_slot* m = &w->meshes[(hv + p) % MESH_TABLE_CAP];
        if (!m->used) {
            if (!create) return NULL;
            m->used = S3D_TRUE; m->cx = cx; m->cz = cz;
            return m;
        }
        if (m->cx == cx && m->cz == cz) return m;
    }
    return NULL;
}

static void slot_rebuild(s3d_voxel_world* w, mesh_slot* m) {
    static s3d_vertex ov[MAX_VERTS_PER_CHUNK], tv[MAX_VERTS_PER_CHUNK];
    static uint16 oi[MAX_IDX_PER_CHUNK], ti[MAX_IDX_PER_CHUNK];
    uint32 onv, oni, tnvv, tini;
    mesh_chunk(w, m->cx, m->cz, ov, &onv, oi, &oni, tv, &tnvv, ti, &tini);

    if (m->opaque) { s3d_destroy_mesh(m->opaque); m->opaque = NULL; }
    if (m->transp) { s3d_destroy_mesh(m->transp); m->transp = NULL; }
    m->tris = 0;
    if (onv)  { m->opaque = s3d_create_mesh(ov, onv, oi, oni);   m->tris += oni / 3; }
    if (tnvv) { m->transp = s3d_create_mesh(tv, tnvv, ti, tini); m->tris += tini / 3; }
}

static void slot_free(mesh_slot* m) {
    if (m->opaque) s3d_destroy_mesh(m->opaque);
    if (m->transp) s3d_destroy_mesh(m->transp);
    memset(m, 0, sizeof(*m));
}

// ---------------------------------------------------------------------------
s3d_voxel_world* s3d_voxel_create_infinite(int32 world_height, uint32 seed, int32 water_level) {
    if (world_height <= 1) return NULL;
    s3d_voxel_world* w = (s3d_voxel_world*)calloc(1, sizeof(*w));
    if (!w) return NULL;
    w->infinite = S3D_TRUE;
    w->height = world_height;
    w->seed = seed;
    w->water_level = water_level;
    w->load_radius = 4;         // 9x9 chunks ~ 144x144 blocks around spawn
    s3d_voxel_block_styles();
    return w;
}

s3d_voxel_world* s3d_voxel_create(int32 size_x, int32 size_y, int32 size_z) {
    if (size_x <= 0 || size_y <= 0 || size_z <= 0) return NULL;
    s3d_voxel_world* w = (s3d_voxel_world*)calloc(1, sizeof(*w));
    if (!w) return NULL;
    w->infinite = S3D_FALSE;
    w->height = size_y;
    w->seed = 1337u;
    w->water_level = 6;
    w->fx0 = 0; w->fz0 = 0; w->fx1 = size_x - 1; w->fz1 = size_z - 1;
    w->load_radius = 1 + (size_x > size_z ? size_x : size_z) / S3D_CHUNK_SIZE;
    s3d_voxel_block_styles();
    return w;
}

void s3d_voxel_generate_terrain(s3d_voxel_world* w, uint32 seed, int32 water_level) {
    if (!w) return;
    w->seed = seed;
    w->water_level = water_level;
    if (!w->infinite) s3d_voxel_rebuild_mesh(w); // build all finite-world chunks now
}

void s3d_voxel_rebuild_mesh(s3d_voxel_world* w) {
    if (!w) return;
    if (w->infinite) {
        // Rebuild only loaded chunks; unseen ones generate fresh anyway.
        for (uint32 i = 0; i < MESH_TABLE_CAP; i++)
            if (w->meshes[i].used) slot_rebuild(w, &w->meshes[i]);
        return;
    }
    for (int32 cx = w->fx0; cx <= w->fx1; cx += S3D_CHUNK_SIZE)
    for (int32 cz = w->fz0; cz <= w->fz1; cz += S3D_CHUNK_SIZE) {
        mesh_slot* m = slot_find(w, cx, cz, S3D_TRUE);
        if (m) slot_rebuild(w, m);
    }
}

void s3d_voxel_update(s3d_voxel_world* w, s3d_vec3 cam, float32 dt) {
    if (!w) return;
    w->gen_budget_left += dt * 0.35f;          // <=35% of frame time on streaming
    if (w->gen_budget_left > 0.12f) w->gen_budget_left = 0.12f;

    int32 pcx = (int32)floorf(cam.x) / S3D_CHUNK_SIZE;
    int32 pcz = (int32)floorf(cam.z) / S3D_CHUNK_SIZE;
    int32 R = w->load_radius;

    for (int ring = 0; ring <= R; ring++) {
        for (int dx = -ring; dx <= ring; dx++)
        for (int dz = -ring; dz <= ring; dz++) {
            if (abs(dx) != ring && abs(dz) != ring) continue; // ring border only
            int32 cx = (pcx + dx) * S3D_CHUNK_SIZE;
            int32 cz = (pcz + dz) * S3D_CHUNK_SIZE;
            if (!w->infinite && (cx < w->fx0 || cx > w->fx1 || cz < w->fz0 || cz > w->fz1))
                continue;
            if (slot_find(w, cx, cz, S3D_FALSE)) continue;    // already resident
            mesh_slot* m = slot_find(w, cx, cz, S3D_TRUE);
            if (!m) continue;                                 // table full: retry later
            slot_rebuild(w, m);
            w->gen_budget_left -= 0.004f;
            if (w->gen_budget_left <= 0.0f) return;
        }
    }

    for (uint32 i = 0; i < MESH_TABLE_CAP; i++) {
        mesh_slot* m = &w->meshes[i];
        if (!m->used) continue;
        int32 dcx = m->cx / S3D_CHUNK_SIZE - pcx;
        int32 dcz = m->cz / S3D_CHUNK_SIZE - pcz;
        if (dcx < 0) dcx = -dcx;
        if (dcz < 0) dcz = -dcz;
        if (dcx > R + 1 || dcz > R + 1) slot_free(m);
    }
}

void s3d_voxel_destroy(s3d_voxel_world* w) {
    if (!w) return;
    for (uint32 i = 0; i < MESH_TABLE_CAP; i++)
        if (w->meshes[i].used) slot_free(&w->meshes[i]);
    for (uint32 i = 0; i < EDIT_TABLE_CAP; i++) {
        free(w->edits[i].ys);
        free(w->edits[i].ids);
    }
    free(w);
}

int32 s3d_voxel_triangle_count(const s3d_voxel_world* w) {
    if (!w) return 0;
    int32 t = 0;
    for (uint32 i = 0; i < MESH_TABLE_CAP; i++)
        if (w->meshes[i].used) t += (int32)w->meshes[i].tris;
    return t;
}

void s3d_voxel_draw(s3d_voxel_world* w, s3d_renderer* renderer) {
    if (!w || !renderer) return;
    for (uint32 i = 0; i < MESH_TABLE_CAP; i++)
        if (w->meshes[i].used && w->meshes[i].opaque)
            s3d_draw_mesh(renderer, w->meshes[i].opaque);
    s3d_enable_blending(renderer, S3D_TRUE);
    for (uint32 i = 0; i < MESH_TABLE_CAP; i++)
        if (w->meshes[i].used && w->meshes[i].transp)
            s3d_draw_mesh(renderer, w->meshes[i].transp);
    s3d_enable_blending(renderer, S3D_FALSE);
}

// ---------------------------------------------------------------------------
// DDA voxel traversal (Amanatides & Woo) — works on infinite worlds too.
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
        if (b != 0 && b != S3D_BLOCK_WATER) {
            if (out_x) *out_x = px;
            if (out_y) *out_y = py;
            if (out_z) *out_z = pz;
            if (out_normal) *out_normal = normal;
            if (out_block) *out_block = b;
            return S3D_TRUE;
        }
        if (tx < ty && tx < tz)   { travelled = tx; px += stepx; tx += invx; normal = (s3d_vec3){-stepx, 0, 0}; }
        else if (ty < tz)         { travelled = ty; py += stepy; ty += invy; normal = (s3d_vec3){0, -stepy, 0}; }
        else                      { travelled = tz; pz += stepz; tz += invz; normal = (s3d_vec3){0, 0, -stepz}; }
        if (travelled > max_dist) break;
        if (py < 0 || py >= w->height) break;
        if (!w->infinite && (px < w->fx0 || pz < w->fz0 || px > w->fx1 || pz > w->fz1)) break;
    }
    return S3D_FALSE;
}
