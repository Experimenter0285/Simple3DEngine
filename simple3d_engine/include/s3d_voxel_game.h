#ifndef S3D_VOXEL_GAME_H
#define S3D_VOXEL_GAME_H

#include "s3d_core.h"
#include "s3d_voxel.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct s3d_engine s3d_engine;
typedef struct s3d_voxel_game s3d_voxel_game;
typedef struct s3d_html_ui s3d_html_ui;

// Create the demo voxel game ("SimpleCraft") bound to an initialized engine.
// autopilot = S3D_TRUE drives the player automatically (headless demos).
s3d_voxel_game* s3d_voxel_game_create(s3d_engine* engine, uint32 seed, bool32 autopilot);
void            s3d_voxel_game_destroy(s3d_voxel_game* g);

// One frame: input -> physics -> block edit -> render + HUD. Call from the
// main loop instead of drawing manually.
void s3d_voxel_game_frame(s3d_voxel_game* g, float dt);

// Human-readable last action (for tests / title bar).
const char* s3d_voxel_game_status(const s3d_voxel_game* g);

// Panorama UI: attach an HTML/CSS overlay document (NULL detaches), or load
// the engine's built-in HUD template. When attached, live stats (fps, pos,
// tris, mode, tool, selection, status) are pushed into elements with ids
// #fps #pos #tris #mode #tool #sel #status every frame.
void   s3d_voxel_game_set_html_ui(s3d_voxel_game* g, s3d_html_ui* ui);
bool32 s3d_voxel_game_enable_default_ui(s3d_voxel_game* g);

#ifdef __cplusplus
}
#endif

#endif // S3D_VOXEL_GAME_H
