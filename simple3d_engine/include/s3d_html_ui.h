#ifndef S3D_HTML_UI_H
#define S3D_HTML_UI_H

// ============================================================================
// Simple3DEngine - Panorama UI: HTML/CSS interfaces rendered by the engine.
//
// A tiny, dependency-free "browser" for game overlays: you write an HTML
// document with inline <style> (CSS) or a separate stylesheet, and the module
// lays it out (block flow + flex rows/columns) and rasterizes it into a 32-bit
// RGBA texture that can be composited over the 3D scene ("panorama UI").
//
// Supported CSS: color, background-color, width, height, padding, margin,
// border-width/border-color, font-size, opacity, position:absolute, left/top.
// Supported elements: div, span, p, h1..h4, button, input, br; id/class
// selectors (#id, .class, element).
//
// Lua bindings are registered when the engine is built with scripting:
//   ui = htmlui.new([[ <html>...<style>...</style>... </html> ]])
//   ui:set_text("#fps", "60")     -- find by #id / .class / tag name
//   ui:set_style("#fps", "color", "red")
//   ui:add_class("#panel", "active") / ui:remove_class(...)
//   ui:on_click("#button", function() ... end)   -- via s3d_html_ui_poll_clicks
//   ui:render(w, h)               -- returns packed rgba32 pointer as number
//   ui:is_loaded(), ui:layout(w,h), ui:text("#id")
#include "s3d_core.h"
#include "s3d_renderer.h"
#include "s3d_script.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct s3d_html_ui s3d_html_ui;

// Parse & lay out a document. Returns NULL on parse failure.
s3d_html_ui* s3d_html_ui_load(const char* html);
void         s3d_html_ui_destroy(s3d_html_ui* ui);

// Re-run layout for a given viewport size (CSS px). Call on window resize.
void s3d_html_ui_layout(s3d_html_ui* ui, int32 vp_w, int32 vp_h);

// Rasterize into an internal ARGB8888 buffer (call after load/layout).
// Returns the pixel buffer (engine-owned, do not free) and its size.
const uint32* s3d_html_ui_pixels(const s3d_html_ui* ui, int32* out_w, int32* out_h);

// Convenience: update one style property of the first node matching a
// selector ("#id", ".class", "tag"), then re-layout.
bool32 s3d_html_ui_set_style(s3d_html_ui* ui, const char* selector,
                             const char* prop, const char* value);
// Set text content of the first node matching the selector.
bool32 s3d_html_ui_set_text(s3d_html_ui* ui, const char* selector, const char* text);
// Read text content of the first matching node (NULL if none).
const char* s3d_html_ui_get_text(const s3d_html_ui* ui, const char* selector);
// Class manipulation
bool32 s3d_html_ui_add_class(s3d_html_ui* ui, const char* selector, const char* cls);
bool32 s3d_html_ui_remove_class(s3d_html_ui* ui, const char* selector, const char* cls);
bool32 s3d_html_ui_toggle_class(s3d_html_ui* ui, const char* selector, const char* cls);

// Upload the rasterized layer and composite it fullscreen over the current
// frame (no-op if nothing was ever laid out). Safe on every backend.
void s3d_html_ui_render(s3d_html_ui* ui, s3d_renderer* renderer, int32 vp_w, int32 vp_h);

// Hit-test a point against the last layout; returns the id of the topmost
// clickable node ("button" or class="clickable"), or NULL.
const char* s3d_html_ui_hit_test(s3d_html_ui* ui, float32 x, float32 y);

// ---- Attached to the script system (Lua) -----------------------------------
// Register the global `htmlui` table in a script context (called by the
// engine automatically when scripts initialize).
void s3d_html_ui_register_lua(s3d_script_context* ctx);
// Attach/detach the active UI document used by helper APIs below.
void s3d_html_ui_attach(s3d_script_context* ctx, s3d_html_ui* ui);
s3d_html_ui* s3d_html_ui_attached(s3d_script_context* ctx);
bool32 s3d_html_ui_is_loaded(s3d_script_context* ctx);
// Game-side convenience: set text on the attached document.
bool32 s3d_html_ui_set_text_ctx(s3d_script_context* ctx, const char* selector, const char* text);
// Fire queued click events (mouse press inside a clickable node). Returns
// number of events dispatched.
int32 s3d_html_ui_poll_clicks(s3d_script_context* ctx, s3d_html_ui* ui,
                              float32 mouse_x, float32 mouse_y, bool32 pressed);

// ---- Built-in default document & C-side callbacks --------------------------
// Load the engine's built-in HTML/CSS HUD template (also usable from Lua via
// htmlui.load_default()).
s3d_html_ui* s3d_html_ui_load_default(void);
// Composite the attached/loaded UI over the frame; no-op when nothing loaded.
void s3d_html_ui_render_active(s3d_renderer* renderer, int32 vp_w, int32 vp_h);
// Hide/show the native fallback HUD while an HTML UI is visible.
void         s3d_html_ui_set_native_hud_visible(bool32 visible);
bool32       s3d_html_ui_native_hud_visible(void);
// Save the rasterized UI layer as a binary PPM file (debug/tests).
bool32       s3d_html_ui_save_ppm(const s3d_html_ui* ui, const char* path);
// Register a C callback for clicks on a clickable element ("#id" or ".class").
typedef void (*s3d_html_ui_click_fn)(const char* id, void* user);
void s3d_html_ui_on_click(const char* selector, s3d_html_ui_click_fn fn, void* user);
// Route engine mouse input to the UI (call once per frame before rendering).
void s3d_html_ui_handle_input(s3d_html_ui* ui);

// Explicit hit-test + click dispatch with current mouse state. If the point
// lands on a clickable element, the click is consumed (returns S3D_TRUE) so
// the game can skip its own mouse handling this frame. Fires callbacks
// registered via s3d_html_ui_on_click and Lua "onclick" handlers.
bool32 s3d_html_ui_handle_input_at(s3d_html_ui* ui, s3d_vec2 mouse_pos, bool32 pressed);

// S3D_TRUE while the last handle_input_at() press was inside a clickable
// element (mouse is "over the UI").
bool32 s3d_html_ui_consumes_mouse(const s3d_html_ui* ui);

#ifdef __cplusplus
}
#endif
#endif // S3D_HTML_UI_H
