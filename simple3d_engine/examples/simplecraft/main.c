// Simple3DEngine - "SimpleCraft" voxel sandbox demo.
// Infinite streaming world + Panorama UI (HTML/CSS HUD) + first-person
// block editing. Runs on software rendering, OpenGL 1.x and GL ES 1.0.
//
// Usage:
//   simplecraft [--headless N] [--width W] [--height H] [--seed S] [--no-ui]
// In headless mode it renders N frames with an autopilot camera and saves
// build/frame.ppm + build/ui.ppm screenshots, then exits (great for CI).

#include "../../include/simple3d_engine.h"
#include "../../include/s3d_voxel_game.h"
#include "../../include/s3d_html_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/s3d_internal.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static bool32 save_ppm(const char* path, const uint32* px, int32 w, int32 h) {
    if (!px || w <= 0 || h <= 0) return S3D_FALSE;
    FILE* f = fopen(path, "wb");
    if (!f) return S3D_FALSE;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int32 y = 0; y < h; y++)
        for (int32 x = 0; x < w; x++) {
            uint32 p = px[y * w + x];
            uint8 rgb[3] = { (uint8)(p & 0xFF), (uint8)((p >> 8) & 0xFF), (uint8)((p >> 16) & 0xFF) };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
    printf("Saved screenshot: %s (%dx%d)\n", path, w, h);
    return S3D_TRUE;
}

int main(int argc, char** argv) {
    int width = 640, height = 480;
    int headless_frames = 0;
    uint32 seed = 20261026u;
    bool32 use_ui = S3D_TRUE;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless") && i + 1 < argc) headless_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--width") && i + 1 < argc) width = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--height") && i + 1 < argc) height = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = (uint32)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--no-ui")) use_ui = S3D_FALSE;
        else if (!strcmp(argv[i], "--help")) {
            printf("Usage: %s [--headless N] [--width W] [--height H] [--seed S] [--no-ui]\n", argv[0]);
            return 0;
        }
    }

    s3d_window_params params;
    memset(&params, 0, sizeof(params));
    params.title = "SimpleCraft - Simple3DEngine voxel sandbox";
    params.width = width;
    params.height = height;
    params.fullscreen = S3D_FALSE;
    params.resizable = S3D_TRUE;
    params.renderer_hint = headless_frames > 0 ? S3D_RENDERER_SOFTWARE : S3D_RENDERER_AUTO;

    s3d_engine* engine = s3d_init(&params);
    if (!engine) { fprintf(stderr, "Failed to init engine\n"); return 1; }

    s3d_voxel_game* game = s3d_voxel_game_create(engine, seed,
                                                 headless_frames > 0 ? S3D_TRUE : S3D_FALSE);
    if (!game) { fprintf(stderr, "Failed to create voxel game\n"); s3d_shutdown(engine); return 1; }

    // Panorama UI: HTML/CSS overlay rendered by the engine itself.
    s3d_html_ui* ui = NULL;
    if (use_ui) {
        ui = s3d_html_ui_load_default();
        s3d_voxel_game_set_html_ui(game, ui);
        if (ui) printf("Panorama UI loaded (HTML/CSS HUD active)\n");
    }

    if (headless_frames > 0) {
        float dt = 1.0f / 60.0f;
        for (int i = 0; i < headless_frames && !s3d_should_quit(engine); i++) {
            s3d_process_events(engine);
            s3d_voxel_game_frame(game, dt);
        }
        printf("Headless run done: %d frames. Status: %s\n",
               headless_frames, s3d_voxel_game_status(game));
        /* build dir created by Makefile */
        int32 fw = 0, fh = 0;
        void* surf = s3d__window_get_pixels(engine->window, &fw, &fh, NULL);
        bool32 shot = S3D_FALSE;
        if (surf) shot = save_ppm("build/frame.ppm", (const uint32*)surf, fw, fh);
        if (!shot) fprintf(stderr, "Could not grab framebuffer pixels\n");
        if (ui) s3d_html_ui_save_ppm(ui, "build/ui.ppm");
    } else {
        while (s3d_should_quit(engine) == S3D_FALSE) {
            s3d_process_events(engine);
            s3d_voxel_game_frame(game, engine->delta_time > 0 ? engine->delta_time : 0.016f);
        }
    }

    s3d_voxel_game_destroy(game);
    if (ui) s3d_html_ui_destroy(ui);
    s3d_shutdown(engine);
    return 0;
}
