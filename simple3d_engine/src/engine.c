// Simple3DEngine - Main Engine Implementation

#include "../include/simple3d_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

s3d_engine* s3d_init(const s3d_window_params* params) {
    printf("========================================\n");
    printf("Simple3DEngine v%d.%d.%d\n", 
           S3D_VERSION_MAJOR, S3D_VERSION_MINOR, S3D_VERSION_PATCH);
    printf("Cross-platform 3D/2D engine for old PCs\n");
    printf("========================================\n\n");
    
    // Detect platform
    s3d_platform_type platform = s3d_get_platform();
    const char* platform_names[] = {"Windows", "Linux", "Android", "FreeBSD", "Unknown"};
    printf("Platform: %s\n", platform_names[platform]);
    
    // Create window
    s3d_window* window = s3d_create_window(params);
    if (!window) {
        fprintf(stderr, "ERROR: Failed to create window\n");
        return NULL;
    }
    
    // Determine renderer type
    s3d_renderer_type renderer_type = params->renderer_hint;
    if (renderer_type == S3D_RENDERER_AUTO) {
        // Auto-detect: Try OpenGL first, fall back to software
        printf("Auto-detecting renderer...\n");
        printf("Note: For now using software renderer\n");
        renderer_type = S3D_RENDERER_SOFTWARE;
    }
    
    // Create renderer
    s3d_renderer* renderer = s3d_create_renderer(window, renderer_type);
    if (!renderer) {
        fprintf(stderr, "ERROR: Failed to create renderer\n");
        s3d_destroy_window(window);
        return NULL;
    }
    
    // Initialize subsystems
    s3d_init_scene();
    s3d_init_input();
    
    // Allocate engine context
    s3d_engine* engine = (s3d_engine*)calloc(1, sizeof(s3d_engine));
    engine->window = window;
    engine->renderer = renderer;
    engine->running = S3D_TRUE;
    engine->delta_time = 0.0f;
    engine->time = 0.0f;
    engine->frame_count = 0;
    
    printf("Engine initialized successfully!\n\n");
    
    return engine;
}

void s3d_shutdown(s3d_engine* engine) {
    if (!engine) return;
    
    printf("Shutting down Simple3DEngine...\n");
    
    // Cleanup subsystems
    s3d_cleanup_input();
    s3d_cleanup_scene();
    
    // Destroy renderer and window
    s3d_destroy_renderer(engine->renderer);
    s3d_destroy_window(engine->window);
    
    free(engine);
    printf("Shutdown complete.\n");
}

bool32 s3d_should_quit(s3d_engine* engine) {
    if (!engine) return S3D_TRUE;
    return !engine->running || s3d_window_should_close(engine->window);
}

void s3d_process_events(s3d_engine* engine) {
    if (!engine) return;
    
    s3d_event events[64];
    int32 num_events = s3d_poll_events(engine->window, events, 64);
    
    for (int32 i = 0; i < num_events; i++) {
        switch (events[i].type) {
            case S3D_EVENT_QUIT:
                engine->running = S3D_FALSE;
                break;
                
            case S3D_EVENT_KEY_DOWN:
                // Forward to input system
                break;
                
            case S3D_EVENT_MOUSE_MOVE:
                // Update mouse position
                break;
                
            default:
                break;
        }
    }
    
    s3d_update_input(engine->window);
}

void s3d_begin_frame(s3d_engine* engine) {
    if (!engine) return;
    
    // Calculate delta time
    float32 current_time = s3d_get_time();
    engine->delta_time = current_time - engine->time;
    engine->time = current_time;
    
    // Clear screen
    s3d_clear(engine->renderer);
    s3d_begin_frame(engine->renderer);
}

void s3d_end_frame(s3d_engine* engine) {
    if (!engine) return;
    
    s3d_end_frame(engine->renderer);
    s3d_swap_buffers(engine->window);
    
    engine->frame_count++;
}

float32 s3d_get_delta_time(s3d_engine* engine) {
    return engine ? engine->delta_time : 0.0f;
}

float32 s3d_get_time(s3d_engine* engine) {
    return engine ? engine->time : 0.0f;
}

uint32 s3d_get_frame_count(s3d_engine* engine) {
    return engine ? engine->frame_count : 0;
}
