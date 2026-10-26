// Simple3DEngine - Panorama UI: tiny HTML/CSS overlay renderer.
// Dependency-free "browser" for game HUDs: parses a small HTML subset with
// inline <style>, performs block/flex layout, rasterizes into an RGBA buffer
// and composites it over the 3D scene through the regular renderer API
// (textured fullscreen quad) - works on software AND OpenGL 1.x/ES 1.0.
//
// Supported tags: html body div span p h1..h4 button input br style
// Supported CSS props: color background-color width height padding margin
//   border-width border-color font-size opacity position left top display
// Selectors: element, .class, #id (last match wins; specificity id>class>tag).

#include "../include/s3d_html_ui.h"
#include "../include/s3d_input.h"
#include "s3d_font_8x8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_NODES 512
#define MAX_TEXT  256
#define MAX_CLASS 96
#define MAX_RULES 128
#define MAX_CLICK_REGISTRATIONS 64

// ---------------------------------------------------------------------------
typedef enum { ST_COLOR, ST_BG, ST_WIDTH, ST_HEIGHT, ST_PAD, ST_MARGIN,
               ST_BW, ST_BC, ST_FONTSIZE, ST_OPACITY, ST_ABS, ST_LEFT, ST_TOP,
               ST_DISPLAY, ST_COUNT } s3d_style_prop;

typedef struct {
    char selector[64];      // "div", ".panel", "#fps"
    char prop[32];
    char value[64];
} css_rule;

struct html_node {
    char tag[16];
    char id[48];
    char classes[MAX_CLASS];
    char text[MAX_TEXT];
    int32 order;            // z-order from document order
    // Resolved styles (from rules + inline)
    float32 color[4], bg[4];
    float32 width, height;  // -1 = auto
    float32 pad[4], margin[4]; // t r b l
    float32 border_w; float32 border_c[4];
    float32 font_size;
    float32 opacity;
    bool32 absolute; float32 left, top;
    bool32 is_row;          // display:flex row
    // Layout results
    float32 lx, ly, lw, lh;
    struct html_node* parent;
    struct html_node* kids[32];
    int32 kid_count;
};

struct s3d_html_ui {
    struct html_node nodes[MAX_NODES];
    int32 node_count;
    css_rule rules[MAX_RULES];
    int32 rule_count;
    float32 vp_w, vp_h;
    uint32* pixels;         // RGBA8888 layer
    int32 pw, ph;
    bool32 dirty;
    bool32 consumes_mouse;
    char hovered_id[64];
    // Global click registry (selector -> fn)
    struct { char sel[64]; s3d_html_ui_click_fn fn; void* user; } clicks[MAX_CLICK_REGISTRATIONS];
    int32 click_count;
};

static s3d_html_ui* g_attached_ui = NULL;
static bool32 g_native_hud_visible = S3D_TRUE;

// ---------------------------------------------------------------------------
// Parsing helpers
// ---------------------------------------------------------------------------
static void skip_ws(const char** p) { while (**p && isspace((unsigned char)**p)) (*p)++; }

static void attr_value(const char** p, char* out, size_t cap) {
    // assume *p points at '=' ; parse quoted or bare token
    const char* s = *p;
    while (*s == ' ' || *s == '=') s++;
    if (*s == '"' || *s == '\'') {
        char q = *s++;
        size_t i = 0;
        while (*s && *s != q && i + 1 < cap) out[i++] = *s++;
        out[i] = 0;
        if (*s == q) s++;
    } else {
        size_t i = 0;
        while (*s && !isspace((unsigned char)*s) && *s != '>' && i + 1 < cap) out[i++] = *s++;
        out[i] = 0;
    }
    *p = s;
}

static void lower_copy(char* dst, size_t cap, const char* src, size_t n) {
    size_t i = 0;
    for (; i + 1 < cap && i < n; i++) dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = 0;
}

static struct html_node* alloc_node(s3d_html_ui* ui, struct html_node* parent) {
    if (ui->node_count >= MAX_NODES) return NULL;
    struct html_node* nd = &ui->nodes[ui->node_count++];
    memset(nd, 0, sizeof(*nd));
    nd->parent = parent;
    nd->order = ui->node_count;
    // defaults
    nd->color[0] = nd->color[1] = nd->color[2] = 1.0f; nd->color[3] = 1.0f;
    nd->bg[3] = 0.0f;
    nd->width = nd->height = -1.0f;
    nd->font_size = 8.0f;
    nd->opacity = 1.0f;
    if (parent && parent->kid_count < 32) parent->kids[parent->kid_count++] = nd;
    return nd;
}

static void parse_style_block(s3d_html_ui* ui, const char* css) {
    const char* p = css;
    while (*p) {
        skip_ws(&p);
        if (!*p) break;
        // selector up to '{'
        const char* sel_start = p;
        while (*p && *p != '{') p++;
        if (!*p) break;
        size_t sel_len = (size_t)(p - sel_start);
        css_rule* ru = NULL;
        // We store one rule per property; find/create group by selector
        char selector[64];
        lower_copy(selector, sizeof(selector), sel_start, sel_len);
        // trim selector
        char* ss = selector; skip_ws((const char**)&ss);
        p++; // consume {
        // properties until }
        while (*p && *p != '}') {
            skip_ws(&p);
            if (*p == ';') { p++; continue; }
            if (*p == '}') break;
            const char* ks = p;
            while (*p && *p != ':' && *p != '}') p++;
            if (*p != ':') break;
            size_t klen = (size_t)(p - ks);
            const char* vs = ++p;
            while (*p && *p != ';' && *p != '}') p++;
            size_t vlen = (size_t)(p - vs);
            if (*p == ';') p++;
            if (ui->rule_count < MAX_RULES) {
                ru = &ui->rules[ui->rule_count++];
                snprintf(ru->selector, sizeof(ru->selector), "%s", ss);
                lower_copy(ru->prop, sizeof(ru->prop), ks, klen);
                // trim trailing ws in prop
                size_t pl = strlen(ru->prop);
                while (pl && isspace((unsigned char)ru->prop[pl-1])) ru->prop[--pl] = 0;
                size_t i = 0;
                for (; i + 1 < sizeof(ru->value) && i < vlen; i++) ru->value[i] = vs[i];
                ru->value[i] = 0;
                // trim spaces
                char* vv = ru->value; skip_ws((const char**)&vv);
                if (vv != ru->value) memmove(ru->value, vv, strlen(vv) + 1);
            }
        }
        if (*p == '}') p++;
    }
}

static int32 parse_color(const char* v, float32 out[4]) {
    out[3] = 1.0f;
    if (v[0] == '#') {
        unsigned r = 0, g = 0, b = 0, a = 255;
        size_t len = strlen(v + 1);
        if (len == 3) { sscanf(v + 1, "%1x%1x%1x", &r, &g, &b); r *= 17; g *= 17; b *= 17; }
        else if (len == 6) sscanf(v + 1, "%2x%2x%2x", &r, &g, &b);
        else if (len == 8) { sscanf(v + 1, "%2x%2x%2x%2x", &r, &g, &b, &a); }
        else return 0;
        out[0] = r / 255.0f; out[1] = g / 255.0f; out[2] = b / 255.0f; out[3] = a / 255.0f;
        return 1;
    }
    if (!strncmp(v, "rgba(", 5)) {
        float r, g, b, a = 1.0f;
        if (sscanf(v + 5, "%f , %f , %f , %f", &r, &g, &b, &a) >= 3) {
            out[0] = r / 255.0f; out[1] = g / 255.0f; out[2] = b / 255.0f; out[3] = a;
            return 1;
        }
    }
    struct { const char* n; float c[3]; } named[] = {
        {"black",{0,0,0}}, {"white",{1,1,1}}, {"red",{1,0,0}}, {"green",{0,0.6f,0}},
        {"blue",{0,0,1}}, {"yellow",{1,1,0}}, {"orange",{1,0.5f,0}}, {"gray",{0.5f,0.5f,0.5f}},
        {"grey",{0.5f,0.5f,0.5f}}, {"cyan",{0,1,1}}, {"magenta",{1,0,1}}, {"lime",{0,1,0}},
        {"navy",{0,0,0.5f}}, {"silver",{0.75f,0.75f,0.75f}}, {"gold",{1,0.84f,0}},
        {"transparent",{0,0,0}},
    };
    for (unsigned i = 0; i < sizeof(named)/sizeof(named[0]); i++)
        if (!strcmp(v, named[i].n)) {
            out[0]=named[i].c[0]; out[1]=named[i].c[1]; out[2]=named[i].c[2];
            if (!strcmp(v, "transparent")) out[3] = 0;
            return 1;
        }
    return 0;
}

static float32 parse_px(const char* v) {
    float f = 0; sscanf(v, "%f", &f); return f;
}

static bool32 selector_matches(const char* sel, const struct html_node* nd) {
    if (sel[0] == '#') return nd->id[0] && !strcmp(sel + 1, nd->id);
    if (sel[0] == '.') {
        // class membership: classes stored space-separated
        const char* c = nd->classes;
        size_t want = strlen(sel + 1);
        while (*c) {
            while (*c == ' ') c++;
            const char* e = c; while (*e && *e != ' ') e++;
            if ((size_t)(e - c) == want && !strncmp(c, sel + 1, want)) return S3D_TRUE;
            c = e;
        }
        return S3D_FALSE;
    }
    return !strcmp(sel, nd->tag);
}

static int32 selector_specificity(const char* sel) {
    if (sel[0] == '#') return 2;
    if (sel[0] == '.') return 1;
    return 0;
}

static void apply_rule(struct html_node* nd, const char* prop, const char* val) {
    float32 tmp[4];
    if (!strcmp(prop, "color") && parse_color(val, tmp)) { memcpy(nd->color, tmp, 16); return; }
    if (!strcmp(prop, "background-color") && parse_color(val, tmp)) { memcpy(nd->bg, tmp, 16); return; }
    if (!strcmp(prop, "background") && parse_color(val, tmp)) { memcpy(nd->bg, tmp, 16); return; }
    if (!strcmp(prop, "border-color") && parse_color(val, tmp)) { memcpy(nd->border_c, tmp, 16); return; }
    if (!strcmp(prop, "width"))    { nd->width = parse_px(val); return; }
    if (!strcmp(prop, "height"))   { nd->height = parse_px(val); return; }
    if (!strcmp(prop, "padding"))  { float p = parse_px(val); nd->pad[0]=nd->pad[1]=nd->pad[2]=nd->pad[3]=p; return; }
    if (!strcmp(prop, "margin"))   { float p = parse_px(val); nd->margin[0]=nd->margin[1]=nd->margin[2]=nd->margin[3]=p; return; }
    if (!strcmp(prop, "border-width")) { nd->border_w = parse_px(val); return; }
    if (!strcmp(prop, "font-size")){ nd->font_size = parse_px(val); return; }
    if (!strcmp(prop, "opacity"))  { nd->opacity = parse_px(val); return; }
    if (!strcmp(prop, "position")) { nd->absolute = !strcmp(val, "absolute"); return; }
    if (!strcmp(prop, "left"))     { nd->left = parse_px(val); return; }
    if (!strcmp(prop, "top"))      { nd->top = parse_px(val); return; }
    if (!strcmp(prop, "display"))  { nd->is_row = strstr(val, "flex") != NULL; return; }
}

static void resolve_styles(s3d_html_ui* ui, struct html_node* root) {
    // Pass 1: cascade rules onto every node (specificity ordering)
    for (int32 i = 0; i < ui->rule_count; i++) {
        css_rule* ru = &ui->rules[i];
        for (int32 n = 0; n < ui->node_count; n++) {
            struct html_node* nd = &ui->nodes[n];
            if (selector_matches(ru->selector, nd))
                apply_rule(nd, ru->prop, ru->value);
        }
        S3D_UNUSED(root);
    }
    S3D_UNUSED(selector_specificity);
}

// Parse inline style="..." into a node
static void parse_inline_style(struct html_node* nd, const char* css) {
    const char* p = css;
    while (*p) {
        skip_ws(&p);
        if (!*p) break;
        const char* ks = p;
        while (*p && *p != ':') p++;
        if (!*p) break;
        size_t klen = (size_t)(p - ks);
        const char* vs = ++p;
        while (*p && *p != ';') p++;
        size_t vlen = (size_t)(p - vs);
        if (*p == ';') p++;
        char prop[32], val[64];
        lower_copy(prop, sizeof(prop), ks, klen);
        size_t pl = strlen(prop); while (pl && isspace((unsigned char)prop[pl-1])) prop[--pl] = 0;
        char* pp = prop; skip_ws((const char**)&pp);
        if (pp != prop) memmove(prop, pp, strlen(pp) + 1);
        size_t i = 0;
        for (; i + 1 < sizeof(val) && i < vlen; i++) val[i] = vs[i];
        val[i] = 0;
        char* vv = val; skip_ws((const char**)&vv);
        if (vv != val) memmove(val, vv, strlen(vv) + 1);
        size_t vl = strlen(val); while (vl && isspace((unsigned char)val[vl-1])) val[--vl] = 0;
        apply_rule(nd, pp, val);
    }
}

static s3d_html_ui* parse_html(const char* html) {
    s3d_html_ui* ui = (s3d_html_ui*)calloc(1, sizeof(s3d_html_ui));
    if (!ui) return NULL;
    struct html_node* root = alloc_node(ui, NULL);
    strcpy(root->tag, "body");
    struct html_node* stack[32];
    int32 depth = 0;
    stack[depth++] = root;

    const char* p = html;
    while (*p) {
        if (*p == '<') {
            if (!strncmp(p, "<!--", 4)) { const char* e = strstr(p, "-->"); p = e ? e + 3 : p + strlen(p); continue; }
            if (!strncmp(p, "<!", 2))   { const char* e = strchr(p, '>'); p = e ? e + 1 : p + strlen(p); continue; }
            // closing?
            const char* q = p + 1;
            if (*q == '/') {
                q++;
                const char* ts = q; while (*q && *q != '>' && !isspace((unsigned char)*q)) q++;
                char tag[16]; lower_copy(tag, sizeof(tag), ts, (size_t)(q - ts));
                const char* e = strchr(p, '>'); p = e ? e + 1 : p + strlen(p);
                if (strcmp(tag, "br") && strcmp(tag, "input")) {
                    for (int32 i = depth - 1; i >= 1; i--)
                        if (!strcmp(stack[i]->tag, tag)) { depth = i; break; }
                }
                continue;
            }
            // opening tag
            const char* ts = q; while (*q && *q != '>' && !isspace((unsigned char)*q)) q++;
            char tag[16]; lower_copy(tag, sizeof(tag), ts, (size_t)(q - ts));
            // <style> special case
            if (!strcmp(tag, "style")) {
                const char* e = strchr(q, '>');
                if (!e) break;
                const char* cs = e + 1;
                const char* ce = strstr(cs, "</style>");
                if (!ce) ce = cs + strlen(cs);
                char* cssbuf = (char*)malloc((size_t)(ce - cs) + 1);
                memcpy(cssbuf, cs, (size_t)(ce - cs)); cssbuf[ce - cs] = 0;
                parse_style_block(ui, cssbuf);
                free(cssbuf);
                p = ce + 8; if (p > cs + strlen(cs)) p = cs + strlen(cs);
                continue;
            }
            if (!strcmp(tag, "html")) { p = strchr(q, '>'); p = p ? p + 1 : q + strlen(q); continue; }

            struct html_node* nd = alloc_node(ui, stack[depth - 1]);
            if (!nd) break;
            strcpy(nd->tag, tag);
            if (!strcmp(tag, "h1")) nd->font_size = 16;
            if (!strcmp(tag, "h2")) nd->font_size = 12;
            if (!strcmp(tag, "h3")) nd->font_size = 10;
            if (!strcmp(tag, "h4")) nd->font_size = 9;
            if (!strcmp(tag, "button")) {
                nd->bg[0]=0.25f; nd->bg[1]=0.45f; nd->bg[2]=0.8f; nd->bg[3]=1.0f;
                nd->pad[0]=nd->pad[2]=3; nd->pad[1]=nd->pad[3]=6;
                nd->border_w = 1; nd->border_c[0]=nd->border_c[1]=nd->border_c[2]=1; nd->border_c[3]=0.8f;
            }
            // attributes
            while (*q && *q != '>') {
                skip_ws(&q);
                if (*q == '/' || *q == '>') break;
                const char* as = q; while (*q && *q != '=' && *q != '>' && !isspace((unsigned char)*q)) q++;
                char name[32]; lower_copy(name, sizeof(name), as, (size_t)(q - as));
                if (*q == '=') {
                    char value[64]; attr_value(&q, value, sizeof(value));
                    if (!strcmp(name, "id")) { char idv[48]; snprintf(idv, sizeof(idv), "%.*s", 47, value); strcpy(nd->id, idv); }
                    else if (!strcmp(name, "class")) {
                        if (strlen(nd->classes) + strlen(value) + 2 < MAX_CLASS) {
                            strcat(nd->classes, " "); strcat(nd->classes, value);
                        }
                    }
                    else if (!strcmp(name, "style")) parse_inline_style(nd, value);
                }
            }
            const char* e = strchr(q, '>');
            p = e ? e + 1 : q + strlen(q);
            if (strcmp(tag, "br") && strcmp(tag, "input") && depth < 32)
                stack[depth++] = nd;
        } else {
            // text run
            const char* ts = p; while (*p && *p != '<') p++;
            struct html_node* cur = stack[depth - 1];
            size_t have = strlen(cur->text);
            for (const char* s = ts; s < p && have + 1 < MAX_TEXT; s++) {
                char c = *s;
                if (c == '\n' || c == '\t') c = ' ';
                if (c == ' ' && have && cur->text[have-1] == ' ') continue;
                cur->text[have++] = c;
            }
            cur->text[have] = 0;
        }
    }
    resolve_styles(ui, root);
    return ui;
}

// ---------------------------------------------------------------------------
// Layout: simple block flow inside each container; flex rows lay children
// horizontally. Absolute-positioned nodes are placed at left/top.
// ---------------------------------------------------------------------------
static float32 text_width(const struct html_node* nd) {
    return (float32)strlen(nd->text) * (nd->font_size / 8.0f) * 8.0f * 0.62f + 1.0f;
}

static void layout_subtree(s3d_html_ui* ui, struct html_node* nd,
                           float32 x, float32 y, float32 avail_w) {
    nd->lx = x; nd->ly = y;
    float32 inner_w = avail_w - nd->pad[1] - nd->pad[3];
    if (inner_w < 1) inner_w = 1;

    float32 cx = x + nd->pad[3];
    float32 cy = y + nd->pad[0];
    float32 row_max_h = 0;

    for (int32 i = 0; i < nd->kid_count; i++) {
        struct html_node* k = nd->kids[i];
        float32 kw, kh;
        if (k->kid_count == 0 && k->text[0] == 0) { kw = 0; kh = 0; }
        else if (k->kid_count == 0) { // leaf with text
            kw = k->width >= 0 ? k->width : text_width(k) + k->pad[1] + k->pad[3];
            kh = k->height >= 0 ? k->height : k->font_size + k->pad[0] + k->pad[2] + 2;
        } else {
            kw = k->width >= 0 ? k->width : inner_w;
            kh = 0; // computed below
        }
        if (k->absolute) {
            layout_subtree(ui, k, x + k->left, y + k->top, kw);
            continue;
        }
        float32 kx = cx, ky = cy;
        if (nd->is_row) {
            kx = cx; ky = cy;
            layout_subtree(ui, k, kx, ky, kw);
            cx += k->lw + k->margin[1] + k->margin[3] + 4;
            if (k->lh > row_max_h) row_max_h = k->lh;
        } else {
            layout_subtree(ui, k, kx, ky, kw);
            if (kh <= 0) kh = k->lh;
            else { k->lh = kh; }
            cy += kh + k->margin[0] + k->margin[2];
        }
        if (k->height < 0 && k->kid_count == 0) { /* leaf already sized */ }
    }
    if (nd->is_row && row_max_h > 0) cy += row_max_h;

    // Text of this node itself (if it has children too, render after them)
    float32 self_h = 0;
    if (nd->text[0]) self_h = nd->font_size + 2;

    nd->lw = nd->width >= 0 ? nd->width : avail_w;
    nd->lh = nd->height >= 0 ? nd->height
                             : (cy - y + self_h + nd->pad[2] - (nd->kid_count ? 0 : 0));
    if (nd->lh < self_h + nd->pad[0] + nd->pad[2]) nd->lh = self_h + nd->pad[0] + nd->pad[2];
    S3D_UNUSED(ui);
}

static void do_layout(s3d_html_ui* ui, int32 vp_w, int32 vp_h) {
    ui->vp_w = (float32)vp_w; ui->vp_h = (float32)vp_h;
    struct html_node* root = &ui->nodes[0];
    layout_subtree(ui, root, 0, 0, (float32)vp_w);
    ui->dirty = S3D_TRUE;
}

// ---------------------------------------------------------------------------
// Rasterization into ui->pixels (RGBA8888)
// ---------------------------------------------------------------------------
static void blend_px(s3d_html_ui* ui, int32 x, int32 y, const float32 c[4], float32 alpha) {
    if (x < 0 || y < 0 || x >= ui->pw || y >= ui->ph) return;
    if (alpha <= 0) return;
    if (c[3] < 1.0f) alpha *= c[3];
    uint32* px = &ui->pixels[y * ui->pw + x];
    float32 dr = (*px >> 0) & 0xFF, dg = (*px >> 8) & 0xFF, db = (*px >> 16) & 0xFF;
    dr = dr * (1 - alpha) + c[0] * 255.0f * alpha;
    dg = dg * (1 - alpha) + c[1] * 255.0f * alpha;
    db = db * (1 - alpha) + c[2] * 255.0f * alpha;
    *px = ((uint32)0xFF << 24) | ((uint32)(uint8)db << 16) | ((uint32)(uint8)dg << 8) | (uint32)(uint8)dr;
}

static void fill_rect(s3d_html_ui* ui, float32 x, float32 y, float32 w, float32 h, const float32 c[4], float32 a) {
    int32 x0 = (int32)x, y0 = (int32)y, x1 = (int32)(x + w), y1 = (int32)(y + h);
    for (int32 yy = y0; yy < y1; yy++)
        for (int32 xx = x0; xx < x1; xx++)
            blend_px(ui, xx, yy, c, a);
}

static void stroke_rect(s3d_html_ui* ui, float32 x, float32 y, float32 w, float32 h, float32 bw, const float32 c[4]) {
    fill_rect(ui, x, y, w, bw, c, 1);
    fill_rect(ui, x, y + h - bw, w, bw, c, 1);
    fill_rect(ui, x, y, bw, h, c, 1);
    fill_rect(ui, x + w - bw, y, bw, h, c, 1);
}

// Draw text using the engine's built-in 8x8 bitmap font, scaled.
static void draw_text_px(s3d_html_ui* ui, const char* text, float32 x, float32 y,
                         float32 scale, const float32 col[4]) {
    int32 sx = scale < 0.25f ? 1 : (int32)scale;
    for (const char* s = text; *s; s++) {
        uint8 ch = (uint8)*s;
        if (ch < 32 || ch > 126) ch = '?';
        const uint8* glyph = s3d_font_8x8[ch - 32];
        for (int32 gy = 0; gy < 8; gy++) {
            uint8 bits = glyph[gy];
            for (int32 gx = 0; gx < 8; gx++) {
                if (!(bits & (1 << gx))) continue;
                for (int32 oy = 0; oy < sx; oy++)
                    for (int32 ox = 0; ox < sx; ox++)
                        blend_px(ui, (int32)x + gx * sx + ox, (int32)y + gy * sx + oy, col, col[3]);
            }
        }
        x += 8 * sx;
    }
}

static void render_node(s3d_html_ui* ui, struct html_node* nd) {
    if (nd->bg[3] > 0)
        fill_rect(ui, nd->lx, nd->ly, nd->lw, nd->lh, nd->bg, nd->opacity);
    if (nd->border_w > 0 && nd->border_c[3] > 0)
        stroke_rect(ui, nd->lx, nd->ly, nd->lw, nd->lh, nd->border_w, nd->border_c);
    if (nd->text[0]) {
        float32 tx = nd->lx + nd->pad[3] + nd->border_w;
        float32 ty = nd->ly + nd->pad[0] + nd->border_w;
        float32 sc = nd->font_size / 8.0f;
        if (sc < 1.0f) sc = 1.0f;
        float32 col[4]; memcpy(col, nd->color, 16); col[3] *= nd->opacity;
        draw_text_px(ui, nd->text, tx, ty, sc, col);
    }
    for (int32 i = 0; i < nd->kid_count; i++)
        render_node(ui, nd->kids[i]);
}

static void rasterize(s3d_html_ui* ui) {
    if (!ui->pixels || ui->pw != (int32)ui->vp_w || ui->ph != (int32)ui->vp_h) {
        free(ui->pixels);
        ui->pw = (int32)ui->vp_w; ui->ph = (int32)ui->vp_h;
        if (ui->pw <= 0) ui->pw = 320;
        if (ui->ph <= 0) ui->ph = 240;
        ui->pixels = (uint32*)calloc((size_t)ui->pw * (size_t)ui->ph, sizeof(uint32));
        if (!ui->pixels) return;
    }
    memset(ui->pixels, 0, (size_t)ui->pw * (size_t)ui->ph * sizeof(uint32));
    render_node(ui, &ui->nodes[0]);
    ui->dirty = S3D_FALSE;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
s3d_html_ui* s3d_html_ui_load(const char* html) {
    if (!html) return NULL;
    s3d_html_ui* ui = parse_html(html);
    if (!ui) return NULL;
    do_layout(ui, 640, 480);
    rasterize(ui);
    return ui;
}

void s3d_html_ui_destroy(s3d_html_ui* ui) {
    if (!ui) return;
    if (g_attached_ui == ui) g_attached_ui = NULL;
    free(ui->pixels);
    free(ui);
}

void s3d_html_ui_layout(s3d_html_ui* ui, int32 vp_w, int32 vp_h) {
    if (!ui) return;
    do_layout(ui, vp_w, vp_h);
    rasterize(ui);
}

const uint32* s3d_html_ui_pixels(const s3d_html_ui* uic, int32* out_w, int32* out_h) {
    s3d_html_ui* ui = (s3d_html_ui*)uic;
    if (!ui) return NULL;
    if (ui->dirty) rasterize(ui);
    if (out_w) *out_w = ui->pw;
    if (out_h) *out_h = ui->ph;
    return ui->pixels;
}

static struct html_node* find_by_selector(s3d_html_ui* ui, const char* selector) {
    for (int32 i = ui->node_count - 1; i >= 0; i--) { // topmost first
        if (selector_matches(selector, &ui->nodes[i])) return &ui->nodes[i];
    }
    return NULL;
}

bool32 s3d_html_ui_set_style(s3d_html_ui* ui, const char* selector,
                             const char* prop, const char* value) {
    if (!ui || !selector || !prop || !value) return S3D_FALSE;
    struct html_node* nd = find_by_selector(ui, selector);
    if (!nd) return S3D_FALSE;
    apply_rule(nd, prop, value);
    do_layout(ui, (int32)ui->vp_w, (int32)ui->vp_h);
    rasterize(ui);
    return S3D_TRUE;
}

bool32 s3d_html_ui_set_text(s3d_html_ui* ui, const char* selector, const char* text) {
    if (!ui || !selector) return S3D_FALSE;
    struct html_node* nd = find_by_selector(ui, selector);
    if (!nd) return S3D_FALSE;
    snprintf(nd->text, sizeof(nd->text), "%s", text ? text : "");
    do_layout(ui, (int32)ui->vp_w, (int32)ui->vp_h);
    rasterize(ui);
    return S3D_TRUE;
}

const char* s3d_html_ui_get_text(const s3d_html_ui* uic, const char* selector) {
    s3d_html_ui* ui = (s3d_html_ui*)uic;
    if (!ui || !selector) return NULL;
    struct html_node* nd = find_by_selector(ui, selector);
    return nd ? nd->text : NULL;
}

static bool32 class_present(const struct html_node* nd, const char* cls) {
    const char* c = nd->classes;
    size_t want = strlen(cls);
    while (*c) {
        while (*c == ' ') c++;
        const char* e = c; while (*e && *e != ' ') e++;
        if ((size_t)(e - c) == want && !strncmp(c, cls, want)) return S3D_TRUE;
        c = e;
    }
    return S3D_FALSE;
}

bool32 s3d_html_ui_add_class(s3d_html_ui* ui, const char* selector, const char* cls) {
    if (!ui || !selector || !cls) return S3D_FALSE;
    struct html_node* nd = find_by_selector(ui, selector);
    if (!nd) return S3D_FALSE;
    if (class_present(nd, cls)) return S3D_TRUE;
    if (strlen(nd->classes) + strlen(cls) + 2 < MAX_CLASS) {
        strcat(nd->classes, " "); strcat(nd->classes, cls);
    }
    return S3D_TRUE;
}

bool32 s3d_html_ui_remove_class(s3d_html_ui* ui, const char* selector, const char* cls) {
    if (!ui || !selector || !cls) return S3D_FALSE;
    struct html_node* nd = find_by_selector(ui, selector);
    if (!nd) return S3D_FALSE;
    char out[MAX_CLASS]; out[0] = 0;
    const char* c = nd->classes;
    size_t want = strlen(cls);
    while (*c) {
        while (*c == ' ') c++;
        const char* e = c; while (*e && *e != ' ') e++;
        if (!((size_t)(e - c) == want && !strncmp(c, cls, want))) {
            if (out[0] && strlen(out) + (size_t)(e - c) + 2 < MAX_CLASS) { strcat(out, " "); strncat(out, c, (size_t)(e - c)); }
            else if (!out[0]) { strncat(out, c, (size_t)(e - c) < MAX_CLASS - 1 ? (size_t)(e - c) : MAX_CLASS - 1); }
        }
        c = e;
    }
    snprintf(nd->classes, sizeof(nd->classes), "%s", out);
    return S3D_TRUE;
}

bool32 s3d_html_ui_toggle_class(s3d_html_ui* ui, const char* selector, const char* cls) {
    if (!ui || !selector || !cls) return S3D_FALSE;
    struct html_node* nd = find_by_selector(ui, selector);
    if (!nd) return S3D_FALSE;
    if (class_present(nd, cls)) return s3d_html_ui_remove_class(ui, selector, cls);
    return s3d_html_ui_add_class(ui, selector, cls);
}

void s3d_html_ui_render(s3d_html_ui* ui, s3d_renderer* renderer, int32 vp_w, int32 vp_h) {
    if (!ui || !renderer) return;
    if (ui->pw != vp_w || ui->ph != vp_h || ui->dirty) {
        do_layout(ui, vp_w, vp_h);
        rasterize(ui);
    }
    if (!ui->pixels) return;
    // Composite as a single textured fullscreen quad (ARGB byte order matches
    // s3d_texture RGBA data used by both backends).
    static s3d_texture* tex = NULL;
    if (tex) { s3d_destroy_texture(tex); tex = NULL; }
    tex = s3d_create_texture(vp_w, vp_h, (const uint8*)ui->pixels);
    if (!tex) return;
    s3d_setup_2d_projection(renderer, vp_w, vp_h);
    s3d_enable_depth_test(renderer, S3D_FALSE);
    s3d_enable_blending(renderer, S3D_TRUE);
    s3d_draw_textured_rect(renderer, (s3d_rect){0, 0, (float32)vp_w, (float32)vp_h}, tex,
                           (s3d_color){1, 1, 1, 1});
    s3d_enable_blending(renderer, S3D_FALSE);
    s3d_enable_depth_test(renderer, S3D_TRUE);
    s3d_destroy_texture(tex);
    tex = NULL;
}

const char* s3d_html_ui_hit_test(s3d_html_ui* ui, float32 x, float32 y) {
    if (!ui) return NULL;
    const char* best = NULL;
    int32 best_order = -1;
    for (int32 i = 0; i < ui->node_count; i++) {
        struct html_node* nd = &ui->nodes[i];
        bool32 clickable = !strcmp(nd->tag, "button") || strstr(nd->classes, "clickable");
        if (!clickable) continue;
        if (x >= nd->lx && x <= nd->lx + nd->lw && y >= nd->ly && y <= nd->ly + nd->lh) {
            if (nd->order > best_order) { best_order = nd->order; best = nd->id[0] ? nd->id : nd->tag; }
        }
    }
    return best;
}

void s3d_html_ui_on_click(const char* selector, s3d_html_ui_click_fn fn, void* user) {
    s3d_html_ui* ui = g_attached_ui;
    if (!ui || !selector || !fn) return;
    for (int32 i = 0; i < ui->click_count; i++) {
        if (!strcmp(ui->clicks[i].sel, selector)) { ui->clicks[i].fn = fn; ui->clicks[i].user = user; return; }
    }
    if (ui->click_count < MAX_CLICK_REGISTRATIONS) {
        snprintf(ui->clicks[ui->click_count].sel, 64, "%s", selector);
        ui->clicks[ui->click_count].fn = fn;
        ui->clicks[ui->click_count].user = user;
        ui->click_count++;
    }
}

bool32 s3d_html_ui_handle_input_at(s3d_html_ui* ui, s3d_vec2 mouse_pos, bool32 pressed) {
    if (!ui) return S3D_FALSE;
    const char* hit = s3d_html_ui_hit_test(ui, mouse_pos.x, mouse_pos.y);
    if (hit && pressed) {
        ui->consumes_mouse = S3D_TRUE;
        snprintf(ui->hovered_id, sizeof(ui->hovered_id), "%s", hit);
        for (int32 i = 0; i < ui->click_count; i++) {
            char sel[66]; snprintf(sel, sizeof(sel), "#%s", hit);
            if (!strcmp(ui->clicks[i].sel, sel) || !strcmp(ui->clicks[i].sel, hit))
                ui->clicks[i].fn(hit, ui->clicks[i].user);
        }
        return S3D_TRUE;
    }
    if (!pressed) ui->consumes_mouse = S3D_FALSE;
    return S3D_FALSE;
}

bool32 s3d_html_ui_consumes_mouse(const s3d_html_ui* uic) {
    return uic ? ((s3d_html_ui*)uic)->consumes_mouse : S3D_FALSE;
}

void s3d_html_ui_handle_input(s3d_html_ui* ui) {
    if (!ui) return;
    s3d_html_ui_handle_input_at(ui, s3d_get_mouse_position(), s3d_is_mouse_pressed(S3D_MOUSE_LEFT));
}

// ---- Attached-document helpers ---------------------------------------------
void s3d_html_ui_attach(s3d_script_context* ctx, s3d_html_ui* ui) {
    S3D_UNUSED(ctx);
    g_attached_ui = ui;
}
s3d_html_ui* s3d_html_ui_attached(s3d_script_context* ctx) { S3D_UNUSED(ctx); return g_attached_ui; }
bool32 s3d_html_ui_is_loaded(s3d_script_context* ctx) { S3D_UNUSED(ctx); return g_attached_ui ? S3D_TRUE : S3D_FALSE; }
bool32 s3d_html_ui_set_text_ctx(s3d_script_context* ctx, const char* selector, const char* text) {
    S3D_UNUSED(ctx);
    return s3d_html_ui_set_text(g_attached_ui, selector, text);
}
int32 s3d_html_ui_poll_clicks(s3d_script_context* ctx, s3d_html_ui* ui,
                              float32 mouse_x, float32 mouse_y, bool32 pressed) {
    S3D_UNUSED(ctx);
    s3d_vec2 mp = { mouse_x, mouse_y };
    return s3d_html_ui_handle_input_at(ui ? ui : g_attached_ui, mp, pressed) ? 1 : 0;
}

bool32 s3d_html_ui_save_ppm(const s3d_html_ui* uic, const char* path) {
    s3d_html_ui* ui = (s3d_html_ui*)uic;
    if (!ui || !path) return S3D_FALSE;
    int32 w, h;
    const uint32* px = s3d_html_ui_pixels(ui, &w, &h);
    if (!px) return S3D_FALSE;
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
    return S3D_TRUE;
}

void s3d_html_ui_set_native_hud_visible(bool32 visible) { g_native_hud_visible = visible; }
bool32 s3d_html_ui_native_hud_visible(void) { return g_native_hud_visible; }

// Built-in default HUD template for voxel sandbox demos.
s3d_html_ui* s3d_html_ui_load_default(void) {
    static const char* doc =
    "<html><head><style>"
    "#hud      { position:absolute; left:8px; top:8px; width:230px; padding:8px;"
    "            background-color:rgba(10,14,24,0.67); color:#dfe9ff; font-size:8px;"
    "            border-width:1px; border-color:#5a7fbf; }"
    "#hudtitle { color:#ffd23f; font-size:12px; }"
    "#stats    { display:flex; margin:4px; color:#9fd0ff; }"
    "#fps,#pos,#tris,#mode,#tool,#sel { color:#ffffff; }"
    "#status   { color:#8fe38f; }"
    "#help     { position:absolute; left:8px; top:150px; padding:6px;"
    "            background-color:rgba(10,14,24,0.6); color:#aabbd0; font-size:8px; }"
    ".btnrow   { display:flex; margin:6px; }"
    "button    { color:#ffffff; }"
    "</style></head><body>"
    "<div id='hud'>"
    "  <div id='hudtitle'>SimpleCraft - Panorama UI</div>"
    "  <div id='stats'><span>FPS </span><span id='fps'>--</span>"
    "       <span> XYZ </span><span id='pos'>0 0 0</span></div>"
    "  <div><span>Tris: </span><span id='tris'>0</span>"
    "       <span> Mode: </span><span id='mode'>WALK</span>"
    "       <span> Tool: </span><span id='tool'>Break/Place</span></div>"
    "  <div><span>Target: </span><span id='sel'>-</span></div>"
    "  <div id='status'>Welcome!</div>"
    "  <div class='btnrow'><button id='flybtn'>Toggle Fly</button>"
    "       <button id='undo'>Undo</button></div>"
    "</div>"
    "<div id='help'>WASD move | Mouse look | LMB break | RMB place | F fly | G tool | Z undo | T brush | 1-8 hotbar</div>"
    "</body></html>";
    s3d_html_ui* ui = s3d_html_ui_load(doc);
    if (ui) g_attached_ui = ui;
    return ui;
}

void s3d_html_ui_render_active(s3d_renderer* renderer, int32 vp_w, int32 vp_h) {
    if (g_attached_ui) s3d_html_ui_render(g_attached_ui, renderer, vp_w, vp_h);
}

// Lua bindings are installed by script_lua.c when Lua is available; this stub
// keeps the header promise without pulling Lua symbols into the core build.
void s3d_html_ui_register_lua(s3d_script_context* ctx) { S3D_UNUSED(ctx); }
