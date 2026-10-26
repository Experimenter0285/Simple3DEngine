// Simple3DEngine - Voxel game module (Minecraft-like: "SimpleCraft")
// First-person block breaking/placing with AABB collision, gravity and jump.
// Works on every backend (software / OpenGL 1.x / ES 1.0) because it only
// uses the public engine API.
#include "../include/s3d_voxel_game.h"
#include "../include/simple3d_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define PLAYER_RADIUS 0.3f
#define PLAYER_HEIGHT 1.7f
#define EYE_HEIGHT    1.6f
#define REACH         6.0f
#define WALK_SPEED    4.5f
#define SPRINT_SPEED  7.0f
#define JUMP_SPEED    6.2f
#define GRAVITY       19.6f

struct s3d_voxel_game {
    s3d_engine* engine;
    s3d_voxel_world* world;

    // Player state (position = feet center)
    float px, py, pz;
    float vx, vy, vz;
    float yaw, pitch;      // radians
    bool32 on_ground;

    // Selection & inventory
    int32 sel_x, sel_y, sel_z;
    s3d_vec3 sel_normal;
    bool32 has_target;
    uint8 hotbar[5];
    int32 hotbar_index;

    // Timing for FPS display
    float fps_timer;
    int32 fps_frames;
    float fps_value;

    // Auto-pilot for headless screenshots
    bool32 autopilot;
    float auto_t;

    char status_line[128];
};

// ---------------------------------------------------------------------------
static void place_structure(s3d_voxel_world* w) {
    // A small brick house with a wooden roof to show off editing.
    int cx = 34, cz = 34;
    int gy = 0;
    while (gy < 32 && s3d_voxel_get(w, cx, gy, cz) == 0) gy++;
    if (gy == 0) gy = 6;
    for (int x = cx - 4; x <= cx + 4; x++)
    for (int z = cz - 4; z <= cz + 4; z++) {
        bool32 edge = (x == cx - 4 || x == cx + 4 || z == cz - 4 || z == cz + 4);
        int y0 = s3d_voxel_get(w, x, gy, z) ? gy : gy;
        if (edge) {
            for (int y = 1; y <= 4; y++) {
                // doorway
                if (z == cz - 4 && y <= 3 && x >= cx - 1 && x <= cx + 1) continue;
                s3d_voxel_set(w, x, y0 + y, z, S3D_BLOCK_BRICK);
            }
        }
        if (y0 + 5 > 0) { /* roof below */ }
    }
    for (int x = cx - 4; x <= cx + 4; x++)
    for (int z = cz - 4; z <= cz + 4; z++)
        s3d_voxel_set(w, x, gy + 5, z, S3D_BLOCK_WOOD);
}

s3d_voxel_game* s3d_voxel_game_create(s3d_engine* engine, uint32 seed, bool32 autopilot) {
    s3d_voxel_game* g = (s3d_voxel_game*)calloc(1, sizeof(*g));
    if (!g) return NULL;
    g->engine = engine;
    g->autopilot = autopilot;

    g->world = s3d_voxel_create(64, 32, 64);
    if (!g->world) { free(g); return NULL; }
    s3d_voxel_generate_terrain(g->world, seed, 6);
    place_structure(g->world);
    s3d_voxel_rebuild_mesh(g->world);
    printf("SimpleCraft: world built, %d triangles visible\n", s3d_voxel_triangle_count(g->world));

    // Spawn above terrain at map center
    int sx = 32, sz = 32, sy = 31;
    while (sy > 0 && s3d_voxel_get(g->world, sx, sy, sz) == 0) sy--;
    g->px = (float)sx + 0.5f;
    g->py = (float)(sy + 1) + 0.05f;
    g->pz = (float)sz + 0.5f;
    g->yaw = (float)(M_PI * 0.25);
    g->pitch = -0.15f;

    g->hotbar[0] = S3D_BLOCK_GRASS;
    g->hotbar[1] = S3D_BLOCK_DIRT;
    g->hotbar[2] = S3D_BLOCK_STONE;
    g->hotbar[3] = S3D_BLOCK_BRICK;
    g->hotbar[4] = S3D_BLOCK_WOOD;
    snprintf(g->status_line, sizeof(g->status_line), "SimpleCraft - WASD move, mouse look, LMB break, RMB place");
    return g;
}

void s3d_voxel_game_destroy(s3d_voxel_game* g) {
    if (!g) return;
    s3d_voxel_destroy(g->world);
    free(g);
}

// ---------------------------------------------------------------------------
static bool32 solid_at(const s3d_voxel_game* g, int32 x, int32 y, int32 z) {
    uint8 b = s3d_voxel_get(g->world, x, y, z);
    return (b != 0 && b != S3D_BLOCK_WATER) ? S3D_TRUE : S3D_FALSE;
}

// AABB of player vs voxel grid; resolve per-axis so we slide along walls.
static void collide_and_move(s3d_voxel_game* g, float dx, float dy, float dz) {
    const float r = PLAYER_RADIUS, h = PLAYER_HEIGHT;
    // Y first (ground detection), then X, then Z.
    struct { float* v; int axis; } steps[3] = { {&dy, 1}, {&dx, 0}, {&dz, 2} };
    for (int s = 0; s < 3; s++) {
        float d = *steps[s].v;
        if (d == 0.0f) continue;
        if (steps[s].axis == 0) g->px += d;
        if (steps[s].axis == 1) g->py += d;
        if (steps[s].axis == 2) g->pz += d;

        int x0 = (int)floorf(g->px - r), x1 = (int)floorf(g->px + r);
        int y0 = (int)floorf(g->py),     y1 = (int)floorf(g->py + h);
        int z0 = (int)floorf(g->pz - r), z1 = (int)floorf(g->pz + r);
        for (int x = x0; x <= x1; x++)
        for (int y = y0; y <= y1; y++)
        for (int z = z0; z <= z1; z++) {
            if (!solid_at(g, x, y, z)) continue;
            // Overlap: push out along this axis only.
            if (steps[s].axis == 0) {
                if (d > 0) g->px = (float)x - r - 0.001f;
                else       g->px = (float)(x + 1) + r + 0.001f;
                g->vx = 0;
            } else if (steps[s].axis == 1) {
                if (d > 0) { g->py = (float)y - h - 0.001f; g->vy = 0; }
                else       { g->py = (float)(y + 1) + 0.001f; g->vy = 0; g->on_ground = S3D_TRUE; }
            } else {
                if (d > 0) g->pz = (float)z - r - 0.001f;
                else       g->pz = (float)(z + 1) + r + 0.001f;
                g->vz = 0;
            }
            goto resolved; // one correction per axis step is enough here
            resolved:;
        }
    }
}

static void update_player(s3d_voxel_game* g, float dt) {
    // Input -> wish direction (WASD relative to yaw)
    float fwd = 0.0f, side = 0.0f;
    if (s3d_is_key_down(S3D_KEY_W) || s3d_is_key_down(S3D_KEY_UP))    fwd += 1.0f;
    if (s3d_is_key_down(S3D_KEY_S) || s3d_is_key_down(S3D_KEY_DOWN))  fwd -= 1.0f;
    if (s3d_is_key_down(S3D_KEY_A) || s3d_is_key_down(S3D_KEY_LEFT))  side -= 1.0f;
    if (s3d_is_key_down(S3D_KEY_D) || s3d_is_key_down(S3D_KEY_RIGHT)) side += 1.0f;

    if (g->autopilot) {
        // Gentle orbit + bob for headless demo frames
        g->auto_t += dt;
        g->yaw += dt * 0.15f;
        fwd = 0.6f;
    }

    float speed = (s3d_is_key_down(S3D_KEY_LSHIFT) || s3d_is_key_down(S3D_KEY_RSHIFT)) ? SPRINT_SPEED : WALK_SPEED;
    float sy = sinf(g->yaw), cy = cosf(g->yaw);
    // forward = (-sin(yaw), 0, -cos(yaw)) like a -Z-facing camera
    float wx = (-sy * fwd + cy * side);
    float wz = (-cy * fwd - sy * side);
    float wl = sqrtf(wx * wx + wz * wz);
    if (wl > 0.001f) { wx = wx / wl * speed; wz = wz / wl * speed; }

    g->vx = wx; g->vz = wz;
    g->vy -= GRAVITY * dt;

    // Jump
    if ((s3d_is_key_pressed(S3D_KEY_SPACE)) && g->on_ground) {
        g->vy = JUMP_SPEED;
        g->on_ground = S3D_FALSE;
    }

    bool32 was_ground = g->on_ground;
    g->on_ground = S3D_FALSE;
    collide_and_move(g, g->vx * dt, g->vy * dt, g->vz * dt);
    if (!was_ground && g->on_ground && g->vy < -8.0f)
        snprintf(g->status_line, sizeof(g->status_line), "Ouch! Landed hard (%.1f m/s)", -g->vy);

    // Keep inside the map
    if (g->py < -10.0f) { g->px = 32.5f; g->py = 30.0f; g->pz = 32.5f; g->vy = 0; }
}

static void update_selection(s3d_voxel_game* g, s3d_renderer* renderer) {
    s3d_camera_state cs; s3d_get_camera_state(renderer, &cs);
    S3D_UNUSED(cs);
    // Ray from eye along view direction derived from yaw/pitch
    float cp = cosf(g->pitch);
    s3d_vec3 dir = { -sinf(g->yaw) * cp, sinf(g->pitch), -cosf(g->yaw) * cp };
    s3d_vec3 eye = { g->px, g->py + EYE_HEIGHT, g->pz };
    g->has_target = s3d_voxel_raycast(g->world, eye, dir, REACH,
                                      &g->sel_x, &g->sel_y, &g->sel_z,
                                      &g->sel_normal, NULL);
}

static void handle_mouse_actions(s3d_voxel_game* g) {
    if (!g->has_target) return;
    if (s3d_is_mouse_pressed(S3D_MOUSE_LEFT)) {          // break block
        s3d_voxel_set(g->world, g->sel_x, g->sel_y, g->sel_z, 0);
        s3d_voxel_rebuild_mesh(g->world);
        snprintf(g->status_line, sizeof(g->status_line), "Broke block at (%d,%d,%d)", g->sel_x, g->sel_y, g->sel_z);
    }
    if (s3d_is_mouse_pressed(S3D_MOUSE_RIGHT)) {         // place block
        int nx = g->sel_x + (int)g->sel_normal.x;
        int ny = g->sel_y + (int)g->sel_normal.y;
        int nz = g->sel_z + (int)g->sel_normal.z;
        // don't place inside the player
        float r = PLAYER_RADIUS + 0.05f;
        if (!(nx + 1 > g->px - r && nx < g->px + r &&
              ny + 1 > g->py && ny < g->py + PLAYER_HEIGHT &&
              nz + 1 > g->pz - r && nz < g->pz + r)) {
            s3d_voxel_set(g->world, nx, ny, nz, g->hotbar[g->hotbar_index]);
            s3d_voxel_rebuild_mesh(g->world);
            snprintf(g->status_line, sizeof(g->status_line), "Placed block #%d at (%d,%d,%d)",
                     g->hotbar[g->hotbar_index], nx, ny, nz);
        }
    }
}

static void handle_hotbar_keys(s3d_voxel_game* g) {
    static const s3d_key digits[5] = { S3D_KEY_1, S3D_KEY_2, S3D_KEY_3, S3D_KEY_4, S3D_KEY_5 };
    for (int i = 0; i < 5; i++)
        if (s3d_is_key_pressed(digits[i])) g->hotbar_index = i;
    float wheel = s3d_get_mouse_wheel();
    if (wheel > 0) g->hotbar_index = (g->hotbar_index + 4) % 5;
    if (wheel < 0) g->hotbar_index = (g->hotbar_index + 1) % 5;
}

// ---------------------------------------------------------------------------
static const char* block_name(uint8 b) {
    switch (b) {
        case S3D_BLOCK_GRASS: return "Grass";
        case S3D_BLOCK_DIRT:  return "Dirt";
        case S3D_BLOCK_STONE: return "Stone";
        case S3D_BLOCK_WOOD:  return "Wood";
        case S3D_BLOCK_LEAVES: return "Leaves";
        case S3D_BLOCK_SAND:  return "Sand";
        case S3D_BLOCK_WATER: return "Water";
        case S3D_BLOCK_BRICK: return "Brick";
        default: return "Air";
    }
}

static void draw_wire_cube(s3d_renderer* r, int32 x, int32 y, int32 z, s3d_color col) {
    float a = (float)x, b = (float)y, c = (float)z, e = a + 1.0f, f = b + 1.0f, k = c + 1.0f;
    s3d_vec3 p[8] = {
        {a,b,c},{e,b,c},{e,b,k},{a,b,k},{a,f,c},{e,f,c},{e,f,k},{a,f,k}
    };
    static const int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}
    };
    for (int i = 0; i < 12; i++)
        s3d_draw_line(r, p[edges[i][0]], p[edges[i][1]], col, 1.0f);
}

static void draw_hud(s3d_voxel_game* g, s3d_renderer* renderer, int32 w, int32 h) {
    s3d_setup_2d_projection(renderer, w, h);
    s3d_enable_depth_test(renderer, S3D_FALSE);

    // Crosshair
    s3d_color white = {1, 1, 1, 0.9f};
    s3d_draw_line(renderer, (s3d_vec3){w/2 - 6, h/2, 0}, (s3d_vec3){w/2 + 6, h/2, 0}, white, 1.0f);
    s3d_draw_line(renderer, (s3d_vec3){w/2, h/2 - 6, 0}, (s3d_vec3){w/2, h/2 + 6, 0}, white, 1.0f);

    // Info panel
    char buf[160];
    s3d_color dim = {0.85f, 0.9f, 1.0f, 1.0f};
    snprintf(buf, sizeof(buf), "FPS %.0f | XYZ %.1f %.1f %.1f | tris %d",
             g->fps_value, g->px, g->py, g->pz, s3d_voxel_triangle_count(g->world));
    s3d_draw_text(renderer, buf, 6, 6, 1.0f, dim);
    snprintf(buf, sizeof(buf), "Yaw %.0f deg | Pitch %.0f deg | target: %s",
             g->yaw * 180.0f / (float)M_PI, g->pitch * 180.0f / (float)M_PI,
             g->has_target ? block_name(s3d_voxel_get(g->world, g->sel_x, g->sel_y, g->sel_z)) : "none");
    s3d_draw_text(renderer, buf, 6, 20, 1.0f, dim);
    s3d_draw_text(renderer, g->status_line, 6, 34, 1.0f, dim);

    // Hotbar
    int slot = 24;
    int bx = w / 2 - (5 * slot) / 2;
    for (int i = 0; i < 5; i++) {
        s3d_rect rc = { (float)bx + i * slot + 2, (float)(h - slot - 6), (float)slot - 4, (float)slot - 4 };
        const s3d_block_style* st = s3d_voxel_block_styles();
        s3d_color c = st[g->hotbar[i]].top;
        s3d_draw_rect(renderer, rc, c, S3D_TRUE);
        s3d_rect frame = { rc.x - 1, rc.y - 1, rc.width + 2, rc.height + 2 };
        s3d_draw_rect(renderer, frame, i == g->hotbar_index ? white : (s3d_color){0.3f,0.3f,0.3f,1}, S3D_FALSE);
    }
    snprintf(buf, sizeof(buf), "[%d] %s", g->hotbar[g->hotbar_index], block_name(g->hotbar[g->hotbar_index]));
    s3d_draw_text(renderer, buf, (float)bx, (float)(h - slot - 18), 1.0f, white);

    s3d_enable_depth_test(renderer, S3D_TRUE);
}

void s3d_voxel_game_frame(s3d_voxel_game* g, float dt) {
    if (!g || !g->engine) return;
    s3d_renderer* renderer = g->engine->renderer;
    int32 w, h; s3d_get_window_size(g->engine->window, &w, &h);
    if (w <= 0) w = 320;
    if (h <= 0) h = 240;

    // FPS counter
    g->fps_timer += dt; g->fps_frames++;
    if (g->fps_timer >= 0.5f) { g->fps_value = g->fps_frames / g->fps_timer; g->fps_timer = 0; g->fps_frames = 0; }

    // Mouse look (delta from input system)
    s3d_vec2 md = s3d_get_mouse_delta();
    if (md.x != 0.0f || md.y != 0.0f) {
        g->yaw   -= md.x * 0.0035f;
        g->pitch -= md.y * 0.0035f;
        if (g->pitch > 1.5f) g->pitch = 1.5f;
        if (g->pitch < -1.5f) g->pitch = -1.5f;
    }

    handle_hotbar_keys(g);
    update_player(g, dt);
    handle_mouse_actions(g);

    // ---- Render ----
    s3d_begin_frame(g->engine); // clears framebuffer

    s3d_camera cam;
    memset(&cam, 0, sizeof(cam));
    float cp = cosf(g->pitch);
    cam.position = (s3d_vec3){ g->px, g->py + EYE_HEIGHT, g->pz };
    cam.target   = (s3d_vec3){ cam.position.x - sinf(g->yaw) * cp,
                               cam.position.y + sinf(g->pitch),
                               cam.position.z - cosf(g->yaw) * cp };
    cam.up = (s3d_vec3){0, 1, 0};
    cam.fov = 70.0f * (float32)M_PI / 180.0f;
    cam.near_plane = 0.1f;
    cam.far_plane = 200.0f;
    s3d_setup_3d_projection(renderer, &cam, (float32)w / (float32)h);

    // Sky-colored clear + fog-ish tint via clear color
    s3d_set_clear_color(renderer, (s3d_color){0.45f, 0.65f, 0.95f, 1.0f});
    s3d_clear(renderer);

    s3d_voxel_draw(g->world, renderer);

    // Selection highlight (projected wireframe cube)
    update_selection(g, renderer);
    if (g->has_target) {
        s3d_color hl = {1.0f, 1.0f, 0.2f, 1.0f};
        draw_wire_cube(renderer, g->sel_x, g->sel_y, g->sel_z, hl);
    }

    draw_hud(g, renderer, w, h);

    s3d_end_frame(g->engine);
}

const char* s3d_voxel_game_status(const s3d_voxel_game* g) {
    return g ? g->status_line : "";
}
