// Simple3DEngine - Game Object and Scene Management

#include "../include/s3d_game_object.h"
#include "renderer_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Global scene state
static s3d_game_object* g_scene_root = NULL;
static uint32 g_next_script_id = 1;

// Physics settings (classic fixed timestep for deterministic old-PC behaviour)
static float32 g_physics_fixed_timestep = 1.0f / 60.0f;
static float32 g_physics_accumulator = 0.0f;
static s3d_vec3 g_gravity = {0.0f, -9.81f, 0.0f};

void s3d_set_fixed_timestep(float32 dt) {
    if (dt > 0.0f && dt < 1.0f) {
        g_physics_fixed_timestep = dt;
        g_physics_accumulator = 0.0f;
    }
}

float32 s3d_get_fixed_timestep(void) { return g_physics_fixed_timestep; }

void s3d_set_gravity(s3d_vec3 g) { g_gravity = g; }

void s3d_init_scene(void) {
    g_scene_root = NULL;
    g_next_script_id = 1;
}

void s3d_cleanup_scene(void) {
    s3d_destroy_all_game_objects();
}

s3d_game_object* s3d_create_game_object(const char* name) {
    s3d_game_object* obj = (s3d_game_object*)calloc(1, sizeof(s3d_game_object));
    if (!obj) return NULL;
    
    if (name) {
        strncpy(obj->name, name, 63);
    }
    
    obj->active = S3D_TRUE;
    obj->transform.position = s3d_vec3_create(0, 0, 0);
    obj->transform.rotation = s3d_vec3_create(0, 0, 0);
    obj->transform.scale = s3d_vec3_create(1, 1, 1);
    obj->transform.matrix = s3d_mat4_identity();
    obj->transform.dirty = S3D_TRUE;
    
    // Add to scene root
    obj->next_sibling = g_scene_root;
    g_scene_root = obj;
    
    return obj;
}

void s3d_destroy_game_object(s3d_game_object* obj) {
    if (!obj) return;
    
    // Remove from parent/scene
    if (obj->parent) {
        s3d_remove_from_parent(obj);
    } else {
        // Remove from root list
        s3d_game_object** prev = &g_scene_root;
        while (*prev) {
            if (*prev == obj) {
                *prev = obj->next_sibling;
                break;
            }
            prev = &(*prev)->next_sibling;
        }
    }
    
    // Free components
    free(obj->mesh_renderer);
    free(obj->sprite);
    free(obj->camera);
    free(obj->light);
    free(obj->collider);
    free(obj->rigidbody);
    
    free(obj);
}

void s3d_destroy_all_game_objects(void) {
    s3d_game_object* current = g_scene_root;
    while (current) {
        s3d_game_object* next = current->next_sibling;
        s3d_destroy_game_object(current);
        current = next;
    }
    g_scene_root = NULL;
}

// Component creation helpers
s3d_mesh_renderer* s3d_add_mesh_renderer(s3d_game_object* obj) {
    if (!obj) return NULL;
    obj->mesh_renderer = (s3d_mesh_renderer*)calloc(1, sizeof(s3d_mesh_renderer));
    obj->mesh_renderer->color = s3d_color_rgb(1, 1, 1);
    return obj->mesh_renderer;
}

s3d_sprite* s3d_add_sprite(s3d_game_object* obj) {
    if (!obj) return NULL;
    obj->sprite = (s3d_sprite*)calloc(1, sizeof(s3d_sprite));
    obj->sprite->tint = s3d_color_rgb(1, 1, 1);
    obj->sprite->layer = 0;
    return obj->sprite;
}

s3d_camera* s3d_add_camera(s3d_game_object* obj) {
    if (!obj) return NULL;
    obj->camera = (s3d_camera*)calloc(1, sizeof(s3d_camera));
    obj->camera->fov = 60.0f * 3.14159265f / 180.0f;
    obj->camera->near_plane = 0.1f;
    obj->camera->far_plane = 1000.0f;
    obj->camera->up = s3d_vec3_create(0, 1, 0);
    obj->camera->target = s3d_vec3_create(0, 0, -1);
    return obj->camera;
}

s3d_light* s3d_add_light(s3d_game_object* obj) {
    if (!obj) return NULL;
    obj->light = (s3d_light*)calloc(1, sizeof(s3d_light));
    obj->light->ambient = s3d_color_rgb(0.2f, 0.2f, 0.2f);
    obj->light->diffuse = s3d_color_rgb(0.8f, 0.8f, 0.8f);
    obj->light->specular = s3d_color_rgb(1.0f, 1.0f, 1.0f);
    obj->light->enabled = S3D_TRUE;
    return obj->light;
}

s3d_collider* s3d_add_collider(s3d_game_object* obj) {
    if (!obj) return NULL;
    obj->collider = (s3d_collider*)calloc(1, sizeof(s3d_collider));
    obj->collider->type = S3D_COLLIDER_BOX;
    obj->collider->size = s3d_vec3_create(1, 1, 1);
    obj->collider->radius = 0.5f;
    return obj->collider;
}

s3d_rigidbody* s3d_add_rigidbody(s3d_game_object* obj) {
    if (!obj) return NULL;
    obj->rigidbody = (s3d_rigidbody*)calloc(1, sizeof(s3d_rigidbody));
    obj->rigidbody->mass = 1.0f;
    obj->rigidbody->damping = 0.99f;
    obj->rigidbody->use_gravity = S3D_TRUE;
    return obj->rigidbody;
}

// Hierarchy
void s3d_set_parent(s3d_game_object* child, s3d_game_object* parent) {
    if (!child || !parent) return;
    
    // Remove from current parent
    s3d_remove_from_parent(child);
    
    // Set new parent
    child->parent = parent;
    child->next_sibling = parent->children;
    parent->children = child;
}

void s3d_remove_from_parent(s3d_game_object* obj) {
    if (!obj || !obj->parent) return;
    
    s3d_game_object** prev = &obj->parent->children;
    while (*prev) {
        if (*prev == obj) {
            *prev = obj->next_sibling;
            obj->parent = NULL;
            obj->next_sibling = NULL;
            break;
        }
        prev = &(*prev)->next_sibling;
    }
}

// Update transform matrices
static void update_transform_recursive(s3d_game_object* obj) {
    if (!obj) return;
    
    if (obj->transform.dirty) {
        // Build local matrix from position, rotation, scale
        s3d_mat4 trans = s3d_mat4_translate(obj->transform.position.x,
                                            obj->transform.position.y,
                                            obj->transform.position.z);
        
        float32 rx = obj->transform.rotation.x * 3.14159265f / 180.0f;
        float32 ry = obj->transform.rotation.y * 3.14159265f / 180.0f;
        float32 rz = obj->transform.rotation.z * 3.14159265f / 180.0f;
        
        s3d_mat4 rot_x = s3d_mat4_rotate_x(rx);
        s3d_mat4 rot_y = s3d_mat4_rotate_y(ry);
        s3d_mat4 rot_z = s3d_mat4_rotate_z(rz);
        
        s3d_mat4 scale_mat = s3d_mat4_scale(obj->transform.scale.x,
                                             obj->transform.scale.y,
                                             obj->transform.scale.z);
        
        obj->transform.matrix = s3d_mat4_multiply(trans, 
                                   s3d_mat4_multiply(rot_z,
                                     s3d_mat4_multiply(rot_y,
                                       s3d_mat4_multiply(rot_x, scale_mat))));
        
        obj->transform.dirty = S3D_FALSE;
    }
    
    // Update children
    s3d_game_object* child = obj->children;
    while (child) {
        update_transform_recursive(child);
        child = child->next_sibling;
    }
}

static void s3d_update_physics(float32 fixed_dt);

void s3d_update_scene(float32 delta_time) {
    // Fixed-timestep physics accumulator: run N deterministic physics steps per
    // rendered frame (classic Quake-style pattern; safe on slow old hardware).
    g_physics_accumulator += delta_time;
    int32 max_steps = 8; // spiral-of-death guard
    while (g_physics_accumulator >= g_physics_fixed_timestep && max_steps-- > 0) {
        s3d_update_physics(g_physics_fixed_timestep);
        g_physics_accumulator -= g_physics_fixed_timestep;
    }

    // Update all transforms
    s3d_game_object* obj = g_scene_root;
    while (obj) {
        if (obj->active) {
            update_transform_recursive(obj);
        }
        obj = obj->next_sibling;
    }
}

// ---------------------------------------------------------------------------
// Physics step: integrate rigidbodies, resolve collisions against static
// colliders and report hits back through the collision callbacks.
// ---------------------------------------------------------------------------
static void get_world_aabb(s3d_game_object* obj, s3d_vec3* out_min, s3d_vec3* out_max) {
    s3d_collider* c = obj->collider;

    // Apply world transform to collider center (scale affects extents too).
    s3d_mat4 w = s3d_get_world_matrix(obj);
    const float32* m = w.m;
    float32 cx = m[0]*c->center.x + m[4]*c->center.y + m[8]*c->center.z + m[12];
    float32 cy = m[1]*c->center.x + m[5]*c->center.y + m[9]*c->center.z + m[13];
    float32 cz = m[2]*c->center.x + m[6]*c->center.y + m[10]*c->center.z + m[14];

    // Approximate scale from matrix basis vectors.
    float32 sx = sqrtf(m[0]*m[0] + m[1]*m[1] + m[2]*m[2]);
    float32 sy = sqrtf(m[4]*m[4] + m[5]*m[5] + m[6]*m[6]);
    float32 sz = sqrtf(m[8]*m[8] + m[9]*m[9] + m[10]*m[10]);

    if (c->type == S3D_COLLIDER_SPHERE) {
        float32 r = c->radius * fmaxf(sx, fmaxf(sy, sz));
        *out_min = s3d_vec3_create(cx - r, cy - r, cz - r);
        *out_max = s3d_vec3_create(cx + r, cy + r, cz + r);
    } else {
        float32 hx = c->size.x * 0.5f * sx;
        float32 hy = c->size.y * 0.5f * sy;
        float32 hz = c->size.z * 0.5f * sz;
        *out_min = s3d_vec3_create(cx - hx, cy - hy, cz - hz);
        *out_max = s3d_vec3_create(cx + hx, cy + hy, cz + hz);
    }
}

bool32 s3d_check_collision(s3d_game_object* a, s3d_game_object* b) {
    if (!a || !b || !a->collider || !b->collider) return S3D_FALSE;

    // Sphere-sphere exact test when both are spheres.
    if (a->collider->type == S3D_COLLIDER_SPHERE && b->collider->type == S3D_COLLIDER_SPHERE) {
        s3d_mat4 wa = s3d_get_world_matrix(a), wb = s3d_get_world_matrix(b);
        s3d_vec3 ca = {wa.m[12], wa.m[13], wa.m[14]};
        s3d_vec3 cb = {wb.m[12], wb.m[13], wb.m[14]};
        float32 ra = a->collider->radius, rb = b->collider->radius;
        s3d_vec3 d = s3d_vec3_sub(ca, cb);
        return s3d_vec3_length_sq(d) <= (ra + rb) * (ra + rb);
    }

    // AABB overlap (box vs box, or conservative for mixed shapes).
    s3d_vec3 amin, amax, bmin, bmax;
    get_world_aabb(a, &amin, &amax);
    get_world_aabb(b, &bmin, &bmax);
    return amin.x <= bmax.x && amax.x >= bmin.x &&
           amin.y <= bmax.y && amax.y >= bmin.y &&
           amin.z <= bmax.z && amax.z >= bmin.z;
}

static void s3d_update_physics(float32 fixed_dt) {
    if (fixed_dt <= 0.0f) return;

    for (s3d_game_object* obj = g_scene_root; obj; obj = obj->next_sibling) {
        if (!obj->active || !obj->rigidbody || obj->rigidbody->is_kinematic) continue;
        s3d_rigidbody* rb = obj->rigidbody;

        // Forces -> acceleration (semi-implicit Euler: velocity first, then position)
        s3d_vec3 accel = s3d_vec3_create(0, 0, 0);
        if (rb->mass > 0.0f) accel = s3d_vec3_scale(rb->force, 1.0f / rb->mass);
        if (rb->use_gravity) accel = s3d_vec3_add(accel, g_gravity);
        rb->force = s3d_vec3_create(0, 0, 0); // forces are consumed per step

        rb->velocity = s3d_vec3_add(rb->velocity, s3d_vec3_scale(accel, fixed_dt));
        rb->velocity = s3d_vec3_scale(rb->velocity, rb->damping);

        s3d_vec3 disp = s3d_vec3_scale(rb->velocity, fixed_dt);
        s3d_translate_local(obj, disp.x, disp.y, disp.z);

        if (!obj->collider) continue; // detection needs a collider

        // Collision detection & simple response against other colliders
        for (s3d_game_object* other = g_scene_root; other; other = other->next_sibling) {
            if (other == obj || !other->active || !other->collider) continue;
            if (!s3d_check_collision(obj, other)) continue;

            if (other->collider->is_trigger || obj->collider->is_trigger) {
                s3d_on_trigger_enter(obj, other);
                continue;
            }

            // Resolve along the smallest penetration axis (positional fix, zero the
            // velocity component into the contact).
            s3d_vec3 amin, amax, bmin, bmax;
            get_world_aabb(obj, &amin, &amax);
            get_world_aabb(other, &bmin, &bmax);
            float32 ox = fminf(amax.x - bmin.x, bmax.x - amin.x);
            float32 oy = fminf(amax.y - bmin.y, bmax.y - amin.y);
            float32 oz = fminf(amax.z - bmin.z, bmax.z - amin.z);

            s3d_vec3 push = {0, 0, 0};
            if (oy <= ox && oy <= oz) {
                push.y = (obj->transform.position.y > other->transform.position.y) ? oy : -oy;
                rb->velocity.y = 0;
            } else if (ox <= oz) {
                push.x = (obj->transform.position.x > other->transform.position.x) ? ox : -ox;
                rb->velocity.x = 0;
            } else {
                push.z = (obj->transform.position.z > other->transform.position.z) ? oz : -oz;
                rb->velocity.z = 0;
            }
            s3d_translate_local(obj, push.x, push.y, push.z);
            s3d_on_collision(obj, other);
        }
    }
}

// Render the entire scene
void s3d_render_scene(s3d_renderer* renderer) {
    if (!renderer) return;
    
    s3d_game_object* obj = g_scene_root;
    while (obj) {
        if (obj->active) {
            s3d_push_matrix(renderer);
            
            // Apply world transform
            s3d_mat4 world = s3d_get_world_matrix(obj);
            s3d_load_matrix(renderer, world);
            
            // Render mesh if present
            if (obj->mesh_renderer && obj->mesh_renderer->mesh) {
                s3d_draw_mesh(renderer, obj->mesh_renderer->mesh);
            }
            
            // Render sprite if present (for 2D games)
            if (obj->sprite && obj->sprite->texture) {
                s3d_draw_textured_rect(renderer, obj->sprite->dst_rect, 
                                       obj->sprite->texture, obj->sprite->tint);
            }
            
            s3d_pop_matrix(renderer);
        }
        obj = obj->next_sibling;
    }
}

// Query functions
s3d_game_object* s3d_find_by_name(const char* name) {
    s3d_game_object* obj = g_scene_root;
    while (obj) {
        if (strcmp(obj->name, name) == 0) {
            return obj;
        }
        obj = obj->next_sibling;
    }
    return NULL;
}

s3d_game_object* s3d_get_first_active_camera(void) {
    s3d_game_object* obj = g_scene_root;
    while (obj) {
        if (obj->active && obj->camera) {
            return obj;
        }
        obj = obj->next_sibling;
    }
    return NULL;
}

// Transform operations
void s3d_set_position(s3d_game_object* obj, float32 x, float32 y, float32 z) {
    if (!obj) return;
    obj->transform.position.x = x;
    obj->transform.position.y = y;
    obj->transform.position.z = z;
    obj->transform.dirty = S3D_TRUE;
}

void s3d_set_rotation(s3d_game_object* obj, float32 x_deg, float32 y_deg, float32 z_deg) {
    if (!obj) return;
    obj->transform.rotation.x = x_deg;
    obj->transform.rotation.y = y_deg;
    obj->transform.rotation.z = z_deg;
    obj->transform.dirty = S3D_TRUE;
}

void s3d_set_scale(s3d_game_object* obj, float32 x, float32 y, float32 z) {
    if (!obj) return;
    obj->transform.scale.x = x;
    obj->transform.scale.y = y;
    obj->transform.scale.z = z;
    obj->transform.dirty = S3D_TRUE;
}

void s3d_translate_local(s3d_game_object* obj, float32 x, float32 y, float32 z) {
    if (!obj) return;
    obj->transform.position.x += x;
    obj->transform.position.y += y;
    obj->transform.position.z += z;
    obj->transform.dirty = S3D_TRUE;
}

void s3d_rotate_local(s3d_game_object* obj, float32 x_deg, float32 y_deg, float32 z_deg) {
    if (!obj) return;
    obj->transform.rotation.x += x_deg;
    obj->transform.rotation.y += y_deg;
    obj->transform.rotation.z += z_deg;
    obj->transform.dirty = S3D_TRUE;
}

s3d_mat4 s3d_get_world_matrix(s3d_game_object* obj) {
    if (!obj) return s3d_mat4_identity();
    
    s3d_mat4 world = obj->transform.matrix;
    s3d_game_object* parent = obj->parent;
    
    while (parent) {
        world = s3d_mat4_multiply(parent->transform.matrix, world);
        parent = parent->parent;
    }
    
    return world;
}

// ---------------------------------------------------------------------------
// Default collision callbacks (weak symbols). A game binary or the Lua
// scripting layer can provide strong definitions to override these.
// ---------------------------------------------------------------------------
__attribute__((weak)) void s3d_on_collision(s3d_game_object* self, s3d_game_object* other) {
    S3D_UNUSED(self); S3D_UNUSED(other);
}

__attribute__((weak)) void s3d_on_trigger_enter(s3d_game_object* self, s3d_game_object* other) {
    S3D_UNUSED(self); S3D_UNUSED(other);
}
