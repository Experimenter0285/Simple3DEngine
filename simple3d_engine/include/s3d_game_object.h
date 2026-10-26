#ifndef S3D_GAME_OBJECT_H
#define S3D_GAME_OBJECT_H

#include "s3d_core.h"
#include "s3d_math.h"
#include "s3d_renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

// Component types
typedef enum {
    S3D_COMPONENT_TRANSFORM,
    S3D_COMPONENT_MESH_RENDERER,
    S3D_COMPONENT_SPRITE,
    S3D_COMPONENT_CAMERA,
    S3D_COMPONENT_LIGHT,
    S3D_COMPONENT_SCRIPT,
    S3D_COMPONENT_COLLIDER,
    S3D_COMPONENT_RIGIDBODY,
} s3d_component_type;

// Transform component
typedef struct {
    s3d_vec3 position;
    s3d_vec3 rotation;  // Euler angles in degrees
    s3d_vec3 scale;
    s3d_mat4 matrix;
    bool32 dirty;
} s3d_transform;

// Mesh renderer component
typedef struct {
    s3d_mesh* mesh;
    s3d_color color;
    s3d_texture* texture;
    bool32 cast_shadows;
    bool32 receive_shadows;
} s3d_mesh_renderer;

// Sprite component (for 2D)
typedef struct {
    s3d_texture* texture;
    s3d_rect src_rect;
    s3d_rect dst_rect;
    s3d_color tint;
    float32 rotation;
    int32 layer;
    bool32 flip_h;
    bool32 flip_v;
} s3d_sprite;

// Collider component
typedef enum {
    S3D_COLLIDER_BOX,
    S3D_COLLIDER_SPHERE,
    S3D_COLLIDER_PLANE,
} s3d_collider_type;

typedef struct {
    s3d_collider_type type;
    s3d_vec3 center;
    s3d_vec3 size;      // For box
    float32 radius;     // For sphere
    bool32 is_trigger;
} s3d_collider;

// RigidBody component
typedef struct {
    float32 mass;
    s3d_vec3 velocity;
    s3d_vec3 force;
    float32 damping;
    bool32 use_gravity;
    bool32 is_kinematic;
} s3d_rigidbody;

// Forward declarations
typedef struct s3d_light s3d_light;

// Game object
typedef struct s3d_game_object {
    char name[64];
    bool32 active;
    
    // Components
    s3d_transform transform;
    s3d_mesh_renderer* mesh_renderer;
    s3d_sprite* sprite;
    s3d_camera* camera;
    s3d_light* light;
    s3d_collider* collider;
    s3d_rigidbody* rigidbody;
    
    // Hierarchy
    struct s3d_game_object* parent;
    struct s3d_game_object* children;
    struct s3d_game_object* next_sibling;
    
    // User data for scripts
    void* script_data;
    uint32 script_id;
} s3d_game_object;

// Light structure
struct s3d_light {
    s3d_vec3 position;
    s3d_vec3 direction;
    s3d_color ambient;
    s3d_color diffuse;
    s3d_color specular;
    float32 constant_attenuation;
    float32 linear_attenuation;
    float32 quadratic_attenuation;
    bool32 enabled;
    int32 index;
};

// Scene management
void s3d_init_scene(void);
void s3d_cleanup_scene(void);

// Game object creation/destruction
s3d_game_object* s3d_create_game_object(const char* name);
void s3d_destroy_game_object(s3d_game_object* obj);
void s3d_destroy_all_game_objects(void);

// Component attachment
s3d_mesh_renderer* s3d_add_mesh_renderer(s3d_game_object* obj);
s3d_sprite* s3d_add_sprite(s3d_game_object* obj);
s3d_camera* s3d_add_camera(s3d_game_object* obj);
s3d_light* s3d_add_light(s3d_game_object* obj);
s3d_collider* s3d_add_collider(s3d_game_object* obj);
s3d_rigidbody* s3d_add_rigidbody(s3d_game_object* obj);

// Hierarchy
void s3d_set_parent(s3d_game_object* child, s3d_game_object* parent);
void s3d_remove_from_parent(s3d_game_object* obj);

// Update and render
void s3d_update_scene(float32 delta_time);
void s3d_render_scene(s3d_renderer* renderer);

// Query
s3d_game_object* s3d_find_by_name(const char* name);
s3d_game_object* s3d_get_first_active_camera(void);

// Transform operations
void s3d_set_position(s3d_game_object* obj, float32 x, float32 y, float32 z);
void s3d_set_rotation(s3d_game_object* obj, float32 x_deg, float32 y_deg, float32 z_deg);
void s3d_set_scale(s3d_game_object* obj, float32 x, float32 y, float32 z);
void s3d_translate_local(s3d_game_object* obj, float32 x, float32 y, float32 z);
void s3d_rotate_local(s3d_game_object* obj, float32 x_deg, float32 y_deg, float32 z_deg);

// Get world matrix
s3d_mat4 s3d_get_world_matrix(s3d_game_object* obj);

// Collision queries
bool32 s3d_check_collision(s3d_game_object* a, s3d_game_object* b);

// Physics tuning
void s3d_set_fixed_timestep(float32 dt);
float32 s3d_get_fixed_timestep(void);
void s3d_set_gravity(s3d_vec3 gravity);

// Collision callbacks (weak symbols: the game or the Lua layer may override
// them; default implementations are no-ops).
__attribute__((weak)) void s3d_on_collision(s3d_game_object* self, s3d_game_object* other);
__attribute__((weak)) void s3d_on_trigger_enter(s3d_game_object* self, s3d_game_object* other);

#ifdef __cplusplus
}
#endif

#endif // S3D_GAME_OBJECT_H
