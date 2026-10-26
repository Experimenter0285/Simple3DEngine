#ifndef S3D_VOXEL_H
#define S3D_VOXEL_H

#include "s3d_core.h"
#include "s3d_math.h"
#include "s3d_renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

// Voxel block ids (0 = air)
typedef enum {
    S3D_BLOCK_AIR = 0,
    S3D_BLOCK_GRASS,
    S3D_BLOCK_DIRT,
    S3D_BLOCK_STONE,
    S3D_BLOCK_WOOD,
    S3D_BLOCK_LEAVES,
    S3D_BLOCK_SAND,
    S3D_BLOCK_WATER,
    S3D_BLOCK_BRICK,
    S3D_BLOCK_COUNT
} s3d_block_id;

// Per-block appearance: RGBA tints for top / side / bottom faces.
typedef struct {
    s3d_color top, side, bottom;
    bool32 transparent; // water/leaves: alpha-blended, neighbor faces still drawn
} s3d_block_style;

typedef struct s3d_voxel_world s3d_voxel_world;

// World -----------------------------------------------------------------
s3d_voxel_world* s3d_voxel_create(int32 size_x, int32 size_y, int32 size_z);
void             s3d_voxel_destroy(s3d_voxel_world* w);

uint8 s3d_voxel_get(const s3d_voxel_world* w, int32 x, int32 y, int32 z);
void  s3d_voxel_set(s3d_voxel_world* w, int32 x, int32 y, int32 z, uint8 block);

// Terrain generation (value-noise heightmap, trees, sand near water level).
void s3d_voxel_generate_terrain(s3d_voxel_world* w, uint32 seed, int32 water_level);

// Style table -----------------------------------------------------------
const s3d_block_style* s3d_voxel_block_styles(void);        // default styles
void s3d_voxel_set_block_style(s3d_block_id id, s3d_block_style style);

// Rendering -------------------------------------------------------------
// Rebuild the visible mesh by culling all faces between two solid blocks.
// Call after modifying the world. Cheap enough for small worlds.
void s3d_voxel_rebuild_mesh(s3d_voxel_world* w);
void s3d_voxel_draw(s3d_voxel_world* w, s3d_renderer* renderer);

// DDA raycast against the voxel grid (Amanatides & Woo).
// Returns S3D_TRUE on hit; fills block coords, normal and block id.
bool32 s3d_voxel_raycast(const s3d_voxel_world* w,
                         s3d_vec3 origin, s3d_vec3 direction, float32 max_dist,
                         int32* out_x, int32* out_y, int32* out_z,
                         s3d_vec3* out_normal, uint8* out_block);

// Stats (for HUD)
int32 s3d_voxel_triangle_count(const s3d_voxel_world* w);

#ifdef __cplusplus
}
#endif

#endif // S3D_VOXEL_H
