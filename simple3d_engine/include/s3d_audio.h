#ifndef S3D_AUDIO_H
#define S3D_AUDIO_H

#include "s3d_core.h"
#include "s3d_math.h"

#ifdef __cplusplus
extern "C" {
#endif

// Audio source types
typedef enum {
    S3D_AUDIO_2D,
    S3D_AUDIO_3D,
} s3d_audio_type;

// Audio clip (opaque)
typedef struct s3d_audio_clip s3d_audio_clip;

// Audio source (opaque)
typedef struct s3d_audio_source s3d_audio_source;

// Initialize audio system
s3d_result s3d_init_audio(void);
void s3d_cleanup_audio(void);

// Load audio files (WAV format for simplicity)
s3d_audio_clip* s3d_load_wav(const char* filepath);
void s3d_destroy_audio_clip(s3d_audio_clip* clip);

// Create audio sources
s3d_audio_source* s3d_create_audio_source(s3d_audio_type type);
void s3d_destroy_audio_source(s3d_audio_source* source);

// Playback control
void s3d_play(s3d_audio_source* source, s3d_audio_clip* clip, bool32 loop);
void s3d_stop(s3d_audio_source* source);
void s3d_pause(s3d_audio_source* source);
void s3d_resume(s3d_audio_source* source);
bool32 s3d_is_playing(s3d_audio_source* source);

// Volume and pitch
void s3d_set_volume(s3d_audio_source* source, float32 volume);
float32 s3d_get_volume(s3d_audio_source* source);
void s3d_set_pitch(s3d_audio_source* source, float32 pitch);

// 3D audio positioning
void s3d_set_position_3d(s3d_audio_source* source, float32 x, float32 y, float32 z);
void s3d_set_velocity_3d(s3d_audio_source* source, float32 vx, float32 vy, float32 vz);
void s3d_set_min_distance(s3d_audio_source* source, float32 distance);
void s3d_set_max_distance(s3d_audio_source* source, float32 distance);

// Listener (camera) position for 3D audio
void s3d_set_listener_position(float32 x, float32 y, float32 z);
void s3d_set_listener_orientation(float32 forward_x, float32 forward_y, float32 forward_z,
                                   float32 up_x, float32 up_y, float32 up_z);

// Master volume
void s3d_set_master_volume(float32 volume);
float32 s3d_get_master_volume(void);

#ifdef __cplusplus
}
#endif

#endif // S3D_AUDIO_H
