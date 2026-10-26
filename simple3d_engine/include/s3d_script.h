#ifndef S3D_SCRIPT_H
#define S3D_SCRIPT_H

#include "s3d_core.h"
#include "s3d_game_object.h"

#ifdef __cplusplus
extern "C" {
#endif

// Script context (opaque)
typedef struct s3d_script_context s3d_script_context;

// Initialize Lua scripting system
s3d_result s3d_init_scripting(void);
void s3d_cleanup_scripting(void);

// Load and execute a Lua script file
s3d_result s3d_load_script_file(s3d_script_context* ctx, const char* filepath);

// Execute a Lua code string
s3d_result s3d_execute_script_string(s3d_script_context* ctx, const char* code);

// Create script context for a game object
s3d_script_context* s3d_create_script_context(s3d_game_object* obj);
void s3d_destroy_script_context(s3d_script_context* ctx);

// Call Lua functions from C++
void s3d_call_lua_update(s3d_script_context* ctx, float32 delta_time);
void s3d_call_lua_start(s3d_script_context* ctx);
void s3d_call_lua_destroy(s3d_script_context* ctx);
void s3d_call_lua_on_collision(s3d_script_context* ctx, s3d_game_object* other, const char* collision_type);

// Register C++ functions to be callable from Lua
typedef int (*s3d_lua_function)(void* lua_state);
void s3d_register_lua_function(const char* name, s3d_lua_function func);

// Get the main script context (for global scripts)
s3d_script_context* s3d_get_main_script_context(void);

// Error handling
const char* s3d_get_last_script_error(void);
void s3d_clear_script_error(void);

#ifdef __cplusplus
}
#endif

#endif // S3D_SCRIPT_H
