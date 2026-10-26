#ifndef SIMPLE3D_ENGINE_H
#define SIMPLE3D_ENGINE_H

// Simple3DEngine - Cross-platform 3D/2D game engine for old PCs
// Supports: Windows, Linux, Android, FreeBSD
// Renderers: OpenGL 1.0, OpenGL ES 1.0, Software Rendering

#include "s3d_core.h"
#include "s3d_math.h"
#include "s3d_platform.h"
#include "s3d_renderer.h"
#include "s3d_game_object.h"
#include "s3d_input.h"
#include "s3d_script.h"
#include "s3d_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

// Engine context (main handle)
typedef struct s3d_engine {
    s3d_window* window;
    s3d_renderer* renderer;
    bool32 running;
    float32 delta_time;
    float32 time;
    uint32 frame_count;
} s3d_engine;

// Initialize the engine
s3d_engine* s3d_init(const s3d_window_params* params);

// Cleanup engine
void s3d_shutdown(s3d_engine* engine);

// Main loop control
bool32 s3d_should_quit(s3d_engine* engine);
void s3d_process_events(s3d_engine* engine);
void s3d_begin_frame(s3d_engine* engine);
void s3d_end_frame(s3d_engine* engine);

// Get global state
float32 s3d_get_delta_time(s3d_engine* engine);
float32 s3d_get_time(s3d_engine* engine);
uint32 s3d_get_frame_count(s3d_engine* engine);

// Convenience macros
#define S3D_UNUSED(x) ((void)(x))

#ifdef __cplusplus
}
#endif

#endif // SIMPLE3D_ENGINE_H
