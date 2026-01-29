#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <strings.h>
#include <math.h>
#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

#include "stb_image.h"
#include "stb_truetype.h"
#include "stb_image_write.h"

// Try to include embedded font if available (optional)
#ifdef EMBED_FONT
#include "font_embedded.h"
#endif
extern unsigned char *stbi_write_png_to_mem(const unsigned char *pixels, int stride_bytes, int x, int y, int n, int *out_len);

#define ARRAY_COUNT(x) (sizeof(x)/sizeof((x)[0]))
#define MAX_ASSETS 256
#define MAX_TOKENS 256
#define MAX_DRAWINGS 256
#define SAVE_MAGIC 0x56545402  // Version 2 with embedded assets

typedef enum { TOOL_SELECT, TOOL_FOG, TOOL_SQUAD, TOOL_DRAW } Tool;
typedef enum { SHAPE_RECT, SHAPE_CIRCLE } Shape;
typedef enum {
    COND_BLEED, COND_DAZED, COND_FRIGHT, COND_GRABBED,
    COND_RESTRAINED, COND_SLOWED, COND_TAUNTED, COND_WEAK, COND_COUNT
} Condition;

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_WindowID id;
    int w, h;
} Window;

typedef struct {
    char path[256];
    SDL_Texture *tex[2];
    int w, h;
    bool loaded;
} Asset;

typedef struct {
    int grid_x, grid_y, size, image_idx, damage, squad;
    uint8_t opacity;
    bool hidden, selected, cond[COND_COUNT];
    SDL_Texture *damage_tex[2];
    int cached_dmg[2];
} Token;

typedef struct {
    Shape type;
    int x1, y1, x2, y2;
    int color;
} Drawing;

typedef struct {
    float x, y, target_x, target_y, zoom, target_zoom;
} Camera;

typedef struct {
    SDL_Texture *tex;
    char text[64];
    int w, h;
} CachedText;

static struct {
    Window dm, player;
    Asset map_assets[MAX_ASSETS];
    int map_count, map_current;
    Asset token_lib[MAX_ASSETS];
    int token_lib_count;
    
    Token tokens[MAX_TOKENS];
    int token_count;
    Drawing drawings[MAX_DRAWINGS];
    int drawing_count;
    
    bool *fog;
    int fog_w, fog_h, grid_size, grid_off_x, grid_off_y;
    int map_w, map_h;
    
    Camera cam[2];
    bool sync_views;
    bool show_grid;
    Tool tool;
    int current_squad, current_shape;
    
    bool drag_token, paint_fog, fog_mode, draw_shape, shift, ctrl;
    int drag_idx, paint_start_x, paint_start_y;
    float last_mx, last_my;
    
    bool cal_active, cal_drag;
    int cal_x1, cal_y1, cal_x2, cal_y2, cal_cells_w, cal_cells_h;
    
    bool cond_wheel;
    int cond_token_idx;
    bool dmg_input;
    char dmg_buf[16];
    int dmg_len;
    
    bool measure_active;
    int measure_start_gx, measure_start_gy;
    
    stbtt_fontinfo font;
    unsigned char *font_data;
    SDL_Texture *cond_tex[2][COND_COUNT];  // 2-letter abbreviations for tokens [view][cond]
    SDL_Texture *cond_wheel_tex[COND_COUNT];  // Full names for wheel
    int cond_w, cond_h;
    
    CachedText ui_tool, ui_squad, ui_dmg, ui_help;
    Tool cached_tool;
} g;

static bool is_image(const char *f) {
    const char *e = strrchr(f, '.');
    if (!e) return false;
    return !strcasecmp(e, ".png") || !strcasecmp(e, ".jpg") || 
           !strcasecmp(e, ".jpeg") || !strcasecmp(e, ".bmp");
}

static void scan_assets(const char *dir, Asset *arr, int *count) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && *count < MAX_ASSETS) {
        if (!is_image(e->d_name)) continue;
        int len = snprintf(arr[*count].path, 256, "%s/%s", dir, e->d_name);
        if (len >= 256) continue;
        (*count)++;
    }
    closedir(d);
    for (int i = 0; i < *count; i++)
        for (int j = i+1; j < *count; j++)
            if (strcmp(arr[i].path, arr[j].path) > 0) {
                Asset t = arr[i]; arr[i] = arr[j]; arr[j] = t;
            }
}

static SDL_Texture* bake_text_once(SDL_Renderer *r, const char *s, int *out_w, int *out_h, SDL_Color col, float font_size) {
    if (!g.font_data || !s[0]) return NULL;
    float scale = stbtt_ScaleForPixelHeight(&g.font, font_size);
    int ascent; stbtt_GetFontVMetrics(&g.font, &ascent, NULL, NULL);
    float x = 0;
    for (int i = 0; s[i]; i++) {
        int adv, lsb; stbtt_GetCodepointHMetrics(&g.font, s[i], &adv, &lsb);
        x += adv * scale + (s[i+1] ? stbtt_GetCodepointKernAdvance(&g.font, s[i], s[i+1]) * scale : 0);
    }
    int w = (int)x + 8, h = (int)font_size + 8;
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (!t) return NULL;
    SDL_SetRenderTarget(r, t);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
    x = 4;
    float y = 4 + ascent * scale;
    for (int i = 0; s[i]; i++) {
        int adv, lsb; stbtt_GetCodepointHMetrics(&g.font, s[i], &adv, &lsb);
        int gw, gh, xoff, yoff;
        unsigned char *bmp = stbtt_GetCodepointBitmap(&g.font, 0, scale, s[i], &gw, &gh, &xoff, &yoff);
        if (bmp) {
            for (int py = 0; py < gh; py++) for (int px = 0; px < gw; px++) {
                if ((int)x+px+xoff >= 0 && (int)x+px+xoff < w && (int)y+py+yoff >= 0 && (int)y+py+yoff < h && bmp[py*gw+px]) {
                    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, bmp[py*gw+px]);
                    SDL_RenderPoint(r, x+px+xoff, y+py+yoff);
                }
            }
            stbtt_FreeBitmap(bmp, NULL);
        }
        x += adv * scale;
    }
    SDL_SetRenderTarget(r, NULL);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    *out_w = w; *out_h = h;
    return t;
}

static void update_cached_text(CachedText *c, SDL_Renderer *r, const char *s, SDL_Color col) {
    if (c->tex && !strcmp(c->text, s)) return;
    if (c->tex) SDL_DestroyTexture(c->tex);
    strncpy(c->text, s, 63);
    c->tex = bake_text_once(r, s, &c->w, &c->h, col, 20.0f);
}

static int load_asset_to_both(const char *path, Asset *slot) {
    int w, h;
    unsigned char *data = stbi_load(path, &w, &h, NULL, 4);
    if (!data) return -1;
    strncpy(slot->path, path, 255);
    slot->path[255] = '\0';
    slot->w = w; slot->h = h;
    SDL_Surface *s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, data, w*4);
    if (s) {
        slot->tex[0] = SDL_CreateTextureFromSurface(g.dm.ren, s);
        slot->tex[1] = SDL_CreateTextureFromSurface(g.player.ren, s);
        SDL_DestroySurface(s);
    }
    stbi_image_free(data);
    slot->loaded = (slot->tex[0] && slot->tex[1]);
    return slot->loaded ? 0 : -1;
}

static int load_asset_from_memory(unsigned char *data, int data_len, Asset *slot, const char *name) {
    int w, h;
    unsigned char *pixels = stbi_load_from_memory(data, data_len, &w, &h, NULL, 4);
    if (!pixels) return -1;
    strncpy(slot->path, name, 255);
    slot->path[255] = '\0';
    slot->w = w; slot->h = h;
    SDL_Surface *s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, pixels, w*4);
    if (s) {
        slot->tex[0] = SDL_CreateTextureFromSurface(g.dm.ren, s);
        slot->tex[1] = SDL_CreateTextureFromSurface(g.player.ren, s);
        SDL_DestroySurface(s);
    }
    stbi_image_free(pixels);
    slot->loaded = (slot->tex[0] && slot->tex[1]);
    return slot->loaded ? 0 : -1;
}

static void ensure_asset_loaded(Asset *slot) {
    if (!slot->loaded && slot->path[0]) {
        load_asset_to_both(slot->path, slot);
    }
}

static int find_or_load_token_image(const char *path) {
    for (int i = 0; i < g.token_lib_count; i++)
        if (!strcmp(g.token_lib[i].path, path)) return i;
    if (g.token_lib_count >= MAX_ASSETS) return -1;
    if (load_asset_to_both(path, &g.token_lib[g.token_lib_count]) == 0)
        return g.token_lib_count++;
    return -1;
}

static void fog_init(int w, int h) {
    g.fog = realloc(g.fog, w * h);
    if (g.fog) {
        memset(g.fog, 1, w * h);
        g.fog_w = w; g.fog_h = h;
    }
}

static inline bool fog_get(int x, int y) {
    return (x >= 0 && x < g.fog_w && y >= 0 && y < g.fog_h) ? g.fog[y*g.fog_w+x] : false;
}

static inline void fog_set(int x, int y, bool v) {
    if (x >= 0 && x < g.fog_w && y >= 0 && y < g.fog_h) g.fog[y*g.fog_w+x] = v;
}

static void cam_update(Camera *c) {
    c->x += (c->target_x - c->x) * 0.15f;
    c->y += (c->target_y - c->y) * 0.15f;
    c->zoom += (c->target_zoom - c->zoom) * 0.15f;
}

static void cam_zoom(Camera *c, float mx, float my, float f) {
    float nz = c->target_zoom * f;
    nz = fmaxf(0.25f, fminf(4.0f, nz));
    float wx = mx / c->target_zoom + c->target_x;
    float wy = my / c->target_zoom + c->target_y;
    c->target_zoom = nz;
    c->target_x = wx - mx / nz;
    c->target_y = wy - my / nz;
}

static void screen_to_grid(float sx, float sy, const Camera *c, int *gx, int *gy) {
    int wx = (int)(sx / c->zoom + c->x);
    int wy = (int)(sy / c->zoom + c->y);
    *gx = (wx - g.grid_off_x) / g.grid_size;
    *gy = (wy - g.grid_off_y) / g.grid_size;
}

static void render_circle(SDL_Renderer *r, float cx, float cy, float rad, bool fill, SDL_Color col) {
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    if (fill) {
        int ir = (int)rad;
        for (int y = -ir; y <= ir; y++) {
            int hw = (int)sqrtf(rad*rad - y*y);
            if (hw > 0) SDL_RenderFillRect(r, &(SDL_FRect){cx-hw, cy+y, hw*2, 1});
        }
    } else {
        int x = 0, y = (int)rad, d = 3 - 2*(int)rad;
        while (y >= x) {
            SDL_RenderPoint(r, cx+x, cy+y); SDL_RenderPoint(r, cx-x, cy+y);
            SDL_RenderPoint(r, cx+x, cy-y); SDL_RenderPoint(r, cx-x, cy-y);
            SDL_RenderPoint(r, cx+y, cy+x); SDL_RenderPoint(r, cx-y, cy+x);
            SDL_RenderPoint(r, cx+y, cy-x); SDL_RenderPoint(r, cx-y, cy-x);
            x++; d += (d > 0) ? 4*(x-y)+10 : 4*x+6;
            if (d > 0) y--;
        }
    }
}

static void render_token(SDL_Renderer *r, Token *t, const Camera *c, int view) {
    if (t->hidden && view == 1) return;
    int gx = t->grid_x * g.grid_size + g.grid_off_x;
    int gy = t->grid_y * g.grid_size + g.grid_off_y;
    Asset *img = &g.token_lib[t->image_idx];
    ensure_asset_loaded(img);
    if (!img->tex[view]) return;
    float scale = (g.grid_size * t->size) / (float)img->w * c->zoom;
    float sw = img->w * scale, sh = img->h * scale;
    float sx = (gx - c->x) * c->zoom;
    float sy = (gy - c->y) * c->zoom - (sh - g.grid_size * c->zoom);
    
    if (t->squad >= 0) {
        static const SDL_Color squad_cols[8] = {
            {255,50,50,255},{50,150,255,255},{50,255,50,255},{255,255,50,255},
            {255,150,50,255},{200,50,255,255},{50,255,255,255},{255,255,255,255}
        };
        SDL_Color col = squad_cols[t->squad % 8];
        float thick = 3 * c->zoom;
        SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
        SDL_FRect rects[4] = {
            {sx-thick, sy-thick, sw+2*thick, thick},
            {sx-thick, sy+sh, sw+2*thick, thick},
            {sx-thick, sy, thick, sh},
            {sx+sw, sy, thick, sh}
        };
        SDL_RenderFillRects(r, rects, 4);
    }
    
    SDL_SetTextureAlphaMod(img->tex[view], t->hidden ? 128 : t->opacity);
    SDL_RenderTexture(r, img->tex[view], NULL, &(SDL_FRect){sx, sy, sw, sh});
    SDL_SetTextureAlphaMod(img->tex[view], 255);
    
    if (t->selected && view == 0) {
        SDL_SetRenderDrawColor(r, 255, 255, 0, 255);
        SDL_RenderRect(r, &(SDL_FRect){sx, sy, sw, sh});
    }
}

static void render_token_markers(SDL_Renderer *r, Token *t, const Camera *c, int view) {
    if (t->hidden && view == 1) return;
    int gx = t->grid_x * g.grid_size + g.grid_off_x;
    int gy = t->grid_y * g.grid_size + g.grid_off_y;
    Asset *img = &g.token_lib[t->image_idx];
    if (!img->loaded) return;
    float scale = (g.grid_size * t->size) / (float)img->w * c->zoom;
    float sw = img->w * scale, sh = img->h * scale;
    float sx = (gx - c->x) * c->zoom;
    float sy = (gy - c->y) * c->zoom - (sh - g.grid_size * c->zoom);
    
    // Damage number at top center
    if (t->damage > 0) {
        if (!t->damage_tex[view] || t->cached_dmg[view] != t->damage) {
            if (t->damage_tex[view]) SDL_DestroyTexture(t->damage_tex[view]);
            char buf[16]; snprintf(buf, 16, "%d", t->damage);
            SDL_Color white = {255, 255, 255, 255};
            int w, h;
            t->damage_tex[view] = bake_text_once(r, buf, &w, &h, white, 20.0f);
            t->cached_dmg[view] = t->damage;
        }
        if (t->damage_tex[view]) {
            float w, h; SDL_GetTextureSize(t->damage_tex[view], &w, &h);
            SDL_SetRenderDrawColor(r, 200, 0, 0, 230);
            SDL_RenderFillRect(r, &(SDL_FRect){sx + sw/2 - w/2 - 2, sy - h - 4, w + 4, h + 4});
            SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
            SDL_RenderRect(r, &(SDL_FRect){sx + sw/2 - w/2 - 2, sy - h - 4, w + 4, h + 4});
            SDL_RenderTexture(r, t->damage_tex[view], NULL, &(SDL_FRect){sx + sw/2 - w/2, sy - h - 2, w, h});
        }
    }
    
    // Condition tags - bigger, inside token bounds, growing upward from bottom
    float token_scale = t->size;
    
    static const SDL_Color cond_colors[COND_COUNT] = {
        {220,20,20,255}, {255,215,0,255}, {147,51,234,255}, {255,140,0,255},
        {139,69,19,255}, {30,144,255,255}, {255,20,147,255}, {50,205,50,255}
    };
    
    // Make tags bigger - CHANGE base_font_size TO ADJUST BOX SIZE (text scales independently below)
    float padding = 3.0f * c->zoom;
    float base_font_size = 32.0f;  // <-- CHANGE THIS VALUE to resize condition tag BOXES
    float tag_width = base_font_size * 2.5f * c->zoom;
    float tag_height = base_font_size * 1.4f * c->zoom;
    
    // Start from bottom of token, inside bounds, grow upward
    float tag_x = sx + padding;
    float tag_y = sy + sh - tag_height - padding * 2;
    
    for (int i = 0; i < COND_COUNT; i++) {
        if (!t->cond[i]) continue;
        
        SDL_Color col = cond_colors[i];
        
        // Ensure tag stays inside token bounds
        if (tag_y < sy + padding) break; // Stop if we run out of space
        
        // Background with condition color
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_FRect tag_bg = {tag_x, tag_y, tag_width + padding*2, tag_height + padding*2};
        SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 230);
        SDL_RenderFillRect(r, &tag_bg);
        
        // Border
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderRect(r, &tag_bg);
        
        // Text - larger than box (text is baked at 16.0f)
        if (g.cond_tex[view][i]) {
            // CHANGE text_size_multiplier TO ADJUST TEXT SIZE (independent of box size)
            float text_size_multiplier = 2.0f;  // <-- CHANGE THIS to make text bigger/smaller
            float text_w = g.cond_w * c->zoom * text_size_multiplier;
            float text_h = g.cond_h * c->zoom * text_size_multiplier;
            SDL_FRect text_dst = {tag_x + padding + (tag_width - text_w) / 2, 
                                  tag_y + padding + (tag_height - text_h) / 2, 
                                  text_w, text_h};
            SDL_RenderTexture(r, g.cond_tex[view][i], NULL, &text_dst);
        }
        
        // Move up for next tag
        tag_y -= tag_height + padding * 3;
    }
}

static void render_view(int view) {
    Window *win = view == 0 ? &g.dm : &g.player;
    SDL_Renderer *r = win->ren;
    Camera *c = &g.cam[view];
    SDL_GetWindowSize(win->win, &win->w, &win->h);
    
    SDL_SetRenderDrawColor(r, 20, 20, 20, 255);
    SDL_RenderClear(r);
    
    if (g.map_current < g.map_count) {
        Asset *m = &g.map_assets[g.map_current];
        ensure_asset_loaded(m);
        if (m->tex[view]) {
            SDL_RenderTexture(r, m->tex[view], NULL, &(SDL_FRect){-c->x*c->zoom, -c->y*c->zoom, m->w*c->zoom, m->h*c->zoom});
        }
    }
    
    // Z-Layer: Grid (optional overlay)
    if (view == 0 && g.show_grid) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 100, 100, 100, 100);
        int sc = (c->x - g.grid_off_x) / g.grid_size;
        int ec = ((c->x + win->w/c->zoom) - g.grid_off_x) / g.grid_size + 1;
        int sr = (c->y - g.grid_off_y) / g.grid_size;
        int er = ((c->y + win->h/c->zoom) - g.grid_off_y) / g.grid_size + 1;
        for (int x = sc; x <= ec; x++) {
            float px = (x * g.grid_size + g.grid_off_x - c->x) * c->zoom;
            SDL_RenderLine(r, px, 0, px, win->h);
        }
        for (int y = sr; y <= er; y++) {
            float py = (y * g.grid_size + g.grid_off_y - c->y) * c->zoom;
            SDL_RenderLine(r, 0, py, win->w, py);
        }
    }
    
    // Z-Layer: Drawings (below tokens)
    static const SDL_Color cols[8] = {
        {255,50,50,128},{50,150,255,128},{50,255,50,128},{255,255,50,128},
        {255,150,50,128},{200,50,255,128},{50,255,255,128},{255,255,255,128}
    };
    for (int i = 0; i < g.drawing_count; i++) {
        Drawing *d = &g.drawings[i];
        SDL_Color col = cols[d->color % 8];
        float x1 = (d->x1 - c->x) * c->zoom, y1 = (d->y1 - c->y) * c->zoom;
        float x2 = (d->x2 - c->x) * c->zoom, y2 = (d->y2 - c->y) * c->zoom;
        if (d->type == SHAPE_RECT) {
            SDL_FRect rect = {x1, y1, x2-x1, y2-y1};
            if (rect.w < 0) { rect.x += rect.w; rect.w = -rect.w; }
            if (rect.h < 0) { rect.y += rect.h; rect.h = -rect.h; }
            SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
            SDL_RenderFillRect(r, &rect);
            SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
            SDL_RenderRect(r, &rect);
        } else {
            float rad = sqrtf((x2-x1)*(x2-x1) + (y2-y1)*(y2-y1))/2;
            render_circle(r, (x1+x2)/2, (y1+y2)/2, rad, true, col);
            SDL_Color b = {col.r, col.g, col.b, 255};
            render_circle(r, (x1+x2)/2, (y1+y2)/2, rad, false, b);
        }
    }
    
    // Z-Layer: Tokens (without damage/conditions)
    for (int i = 0; i < g.token_count; i++) {
        if (view == 1 && !fog_get(g.tokens[i].grid_x, g.tokens[i].grid_y)) continue;
        render_token(r, &g.tokens[i], c, view);
    }
    
    if (view == 0 && g.draw_shape) {
        float mx, my;
        SDL_GetMouseState(&mx, &my);
        int wx = (int)(mx/c->zoom + c->x);
        int wy = (int)(my/c->zoom + c->y);
        SDL_Color col = cols[g.current_squad % 8];
        col.a = 100;
        float x1 = (g.paint_start_x - c->x) * c->zoom, y1 = (g.paint_start_y - c->y) * c->zoom;
        float x2 = (wx - c->x) * c->zoom, y2 = (wy - c->y) * c->zoom;
        if (g.current_shape == SHAPE_RECT) {
            SDL_FRect rect = {x1, y1, x2-x1, y2-y1};
            if (rect.w < 0) { rect.x += rect.w; rect.w = -rect.w; }
            if (rect.h < 0) { rect.y += rect.h; rect.h = -rect.h; }
            SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
            SDL_RenderFillRect(r, &rect);
            SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
            SDL_RenderRect(r, &rect);
        } else {
            float rad = sqrtf((x2-x1)*(x2-x1) + (y2-y1)*(y2-y1))/2;
            render_circle(r, (x1+x2)/2, (y1+y2)/2, rad, true, col);
            SDL_Color b = {col.r, col.g, col.b, 255};
            render_circle(r, (x1+x2)/2, (y1+y2)/2, rad, false, b);
        }
    }
    
    // Z-Layer: Fog of War
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, view == 0 ? 180 : 255);
    int sc = (c->x - g.grid_off_x) / g.grid_size;
    int ec = ((c->x + win->w/c->zoom) - g.grid_off_x) / g.grid_size + 1;
    int sr = (c->y - g.grid_off_y) / g.grid_size;
    int er = ((c->y + win->h/c->zoom) - g.grid_off_y) / g.grid_size + 1;
    if (sc < 0) sc = 0;
    if (sr < 0) sr = 0;
    if (ec > g.fog_w) ec = g.fog_w;
    if (er > g.fog_h) er = g.fog_h;
    for (int y = sr; y < er; y++) {
        for (int x = sc; x < ec; x++) {
            if (!g.fog[y*g.fog_w+x]) {
                SDL_FRect cell = {
                    (x*g.grid_size + g.grid_off_x - c->x)*c->zoom,
                    (y*g.grid_size + g.grid_off_y - c->y)*c->zoom,
                    g.grid_size*c->zoom, g.grid_size*c->zoom
                };
                SDL_RenderFillRect(r, &cell);
            }
        }
    }
    
    // Z-Layer: Damage and Condition Markers (topmost layer for tokens)
    for (int i = 0; i < g.token_count; i++) {
        if (view == 1 && !fog_get(g.tokens[i].grid_x, g.tokens[i].grid_y)) continue;
        render_token_markers(r, &g.tokens[i], c, view);
    }
    
    if (view == 0 && g.cal_active && g.cal_drag) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 100, 255, 80);
        float x = (fmin(g.cal_x1, g.cal_x2) - c->x)*c->zoom;
        float y = (fmin(g.cal_y1, g.cal_y2) - c->y)*c->zoom;
        float w = abs(g.cal_x2 - g.cal_x1)*c->zoom;
        float h = abs(g.cal_y2 - g.cal_y1)*c->zoom;
        SDL_RenderFillRect(r, &(SDL_FRect){x, y, w, h});
        SDL_SetRenderDrawColor(r, 0, 150, 255, 180);
        float cw = w/g.cal_cells_w, ch = h/g.cal_cells_h;
        for (int i = 1; i < g.cal_cells_w; i++) SDL_RenderLine(r, x+i*cw, y, x+i*cw, y+h);
        for (int i = 1; i < g.cal_cells_h; i++) SDL_RenderLine(r, x, y+i*ch, x+w, y+i*ch);
        SDL_RenderRect(r, &(SDL_FRect){x, y, w, h});
    }
    
    // Measurement tool (DM view only)
    if (view == 0 && g.measure_active) {
        float mx, my;
        SDL_GetMouseState(&mx, &my);
        int end_gx, end_gy;
        screen_to_grid(mx, my, c, &end_gx, &end_gy);
        
        // Calculate center points of grid cells
        float start_wx = g.measure_start_gx * g.grid_size + g.grid_off_x + g.grid_size / 2.0f;
        float start_wy = g.measure_start_gy * g.grid_size + g.grid_off_y + g.grid_size / 2.0f;
        float end_wx = end_gx * g.grid_size + g.grid_off_x + g.grid_size / 2.0f;
        float end_wy = end_gy * g.grid_size + g.grid_off_y + g.grid_size / 2.0f;
        
        // Convert to screen space
        float start_sx = (start_wx - c->x) * c->zoom;
        float start_sy = (start_wy - c->y) * c->zoom;
        float end_sx = (end_wx - c->x) * c->zoom;
        float end_sy = (end_wy - c->y) * c->zoom;
        
        // Draw line
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 255, 255, 0, 255);
        SDL_RenderLine(r, start_sx, start_sy, end_sx, end_sy);
        
        // Draw endpoints
        render_circle(r, start_sx, start_sy, 5, true, (SDL_Color){255, 255, 0, 200});
        render_circle(r, end_sx, end_sy, 5, true, (SDL_Color){255, 255, 0, 200});
        
        // Calculate distance in grid cells (Chebyshev distance - diagonal = 1 cell)
        int dx = abs(end_gx - g.measure_start_gx);
        int dy = abs(end_gy - g.measure_start_gy);
        int distance = (dx > dy) ? dx : dy;  // max(dx, dy)
        
        // Display distance text
        if (g.font_data) {
            char dist_buf[64];
            snprintf(dist_buf, 64, "%d cells", distance);
            
            SDL_Color yellow = {255, 255, 0, 255};
            int text_w, text_h;
            SDL_Texture *dist_tex = bake_text_once(r, dist_buf, &text_w, &text_h, yellow, 20.0f);
            
            if (dist_tex) {
                // Position text at midpoint of line, slightly above
                float mid_sx = (start_sx + end_sx) / 2.0f;
                float mid_sy = (start_sy + end_sy) / 2.0f - text_h - 10;
                
                // Background
                SDL_SetRenderDrawColor(r, 0, 0, 0, 180);
                SDL_RenderFillRect(r, &(SDL_FRect){mid_sx - text_w/2 - 5, mid_sy - 5, text_w + 10, text_h + 10});
                
                // Border
                SDL_SetRenderDrawColor(r, 255, 255, 0, 255);
                SDL_RenderRect(r, &(SDL_FRect){mid_sx - text_w/2 - 5, mid_sy - 5, text_w + 10, text_h + 10});
                
                // Text
                SDL_RenderTexture(r, dist_tex, NULL, &(SDL_FRect){mid_sx - text_w/2, mid_sy, text_w, text_h});
                SDL_DestroyTexture(dist_tex);
            }
        }
    }
    
    if (view == 0 && g.font_data) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        
        const char *tool_names[] = {"SELECT TOOL", "FOG OF WAR", "SQUAD ASSIGN", "DRAWING"};
        if (g.cached_tool != g.tool) {
            g.cached_tool = g.tool;
            update_cached_text(&g.ui_tool, r, tool_names[g.tool], (SDL_Color){255,255,255,255});
        }
        if (g.ui_tool.tex) {
            SDL_SetRenderDrawColor(r, 40, 40, 60, 240);
            SDL_RenderFillRect(r, &(SDL_FRect){10, 10, g.ui_tool.w + 40, g.ui_tool.h + 20});
            SDL_SetRenderDrawColor(r, 100, 100, 150, 255);
            SDL_RenderRect(r, &(SDL_FRect){10, 10, g.ui_tool.w + 40, g.ui_tool.h + 20});
            SDL_RenderTexture(r, g.ui_tool.tex, NULL, &(SDL_FRect){20, 20, g.ui_tool.w, g.ui_tool.h});
        }
        
        if (g.tool == TOOL_SQUAD || g.tool == TOOL_DRAW) {
            char buf[64];
            const char *type = g.tool == TOOL_SQUAD ? "SQUAD" : "DRAW";
            snprintf(buf, 64, "%s: Color %d", type, g.current_squad);
            update_cached_text(&g.ui_squad, r, buf, (SDL_Color){255,255,255,255});
            if (g.ui_squad.tex) {
                int y = 50;
                SDL_SetRenderDrawColor(r, 40, 40, 60, 240);
                SDL_RenderFillRect(r, &(SDL_FRect){10, y, g.ui_squad.w + 60, g.ui_squad.h + 20});
                static const SDL_Color squad_cols[8] = {
                    {255,50,50,255},{50,150,255,255},{50,255,50,255},{255,255,50,255},
                    {255,150,50,255},{200,50,255,255},{50,255,255,255},{255,255,255,255}
                };
                SDL_Color col = squad_cols[g.current_squad % 8];
                SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
                SDL_RenderFillRect(r, &(SDL_FRect){20, y+10, 20, 20});
                SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
                SDL_RenderRect(r, &(SDL_FRect){20, y+10, 20, 20});
                SDL_RenderTexture(r, g.ui_squad.tex, NULL, &(SDL_FRect){50, y+10, g.ui_squad.w, g.ui_squad.h});
            }
        }
        
        if (g.dmg_input) {
            char buf[32]; snprintf(buf, 32, "%s: %s_", g.shift ? "HEAL" : "DAMAGE", g.dmg_buf);
            update_cached_text(&g.ui_dmg, r, buf, g.shift ? (SDL_Color){100,255,100,255} : (SDL_Color){255,100,100,255});
            if (g.ui_dmg.tex) {
                int x = win->w/2 - (g.ui_dmg.w + 40)/2;
                SDL_SetRenderDrawColor(r, 40, 40, 60, 240);
                SDL_RenderFillRect(r, &(SDL_FRect){x, 20, g.ui_dmg.w + 40, g.ui_dmg.h + 20});
                SDL_SetRenderDrawColor(r, g.shift ? 100 : 200, g.shift ? 200 : 100, 100, 255);
                SDL_RenderRect(r, &(SDL_FRect){x, 20, g.ui_dmg.w + 40, g.ui_dmg.h + 20});
                SDL_RenderTexture(r, g.ui_dmg.tex, NULL, &(SDL_FRect){x+20, 30, g.ui_dmg.w, g.ui_dmg.h});
            }
        }
        
        if (g.cond_wheel && g.cond_token_idx >= 0) {
            Token *t = &g.tokens[g.cond_token_idx];
            float cx = win->w/2.0f, cy = win->h/2.0f;
            float radius = 220.0f;
            float inner_radius = 70.0f;
            
            float mx, my; SDL_GetMouseState(&mx, &my);
            float dx = mx - cx, dy = my - cy;
            float dist = sqrtf(dx*dx + dy*dy);
            float angle = atan2f(dy, dx);
            if (angle < 0) angle += 6.28318f;
            
            int hovered_index = -1;
            if (dist >= inner_radius && dist <= radius) {
                float segment_angle = 6.28318f / COND_COUNT;
                hovered_index = (int)(angle / segment_angle);
                if (hovered_index >= COND_COUNT) hovered_index = COND_COUNT - 1;
            }
            
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            
            static const float cond_cols[COND_COUNT][3] = {
                {220,20,20}, {255,215,0}, {147,51,234}, {255,140,0},
                {139,69,19}, {30,144,255}, {255,20,147}, {50,205,50}
            };
            
            // Draw wheel segments
            for (int i = 0; i < COND_COUNT; i++) {
                bool is_active = t->cond[i];
                float start_angle = (6.28318f * i) / COND_COUNT;
                float end_angle = (6.28318f * (i + 1)) / COND_COUNT;
                
                float rf, gf, bf, alpha;
                if (is_active) {
                    float gray = (cond_cols[i][0] * 0.3f + cond_cols[i][1] * 0.59f + cond_cols[i][2] * 0.11f) / 255.0f;
                    rf = gf = bf = gray * 0.5f;
                    alpha = (i == hovered_index) ? 0.9f : 0.7f;
                } else {
                    rf = cond_cols[i][0] / 255.0f;
                    gf = cond_cols[i][1] / 255.0f;
                    bf = cond_cols[i][2] / 255.0f;
                    alpha = (i == hovered_index) ? 1.0f : 0.85f;
                }
                
                int steps = 30;
                for (int step = 0; step < steps; step++) {
                    float a1 = start_angle + (end_angle - start_angle) * step / steps;
                    float a2 = start_angle + (end_angle - start_angle) * (step + 1) / steps;
                    
                    SDL_Vertex vertices[4] = {
                        {{cx + inner_radius * cosf(a1), cy + inner_radius * sinf(a1)}, {rf, gf, bf, alpha}, {0, 0}},
                        {{cx + radius * cosf(a1), cy + radius * sinf(a1)}, {rf, gf, bf, alpha}, {0, 0}},
                        {{cx + radius * cosf(a2), cy + radius * sinf(a2)}, {rf, gf, bf, alpha}, {0, 0}},
                        {{cx + inner_radius * cosf(a2), cy + inner_radius * sinf(a2)}, {rf, gf, bf, alpha}, {0, 0}}
                    };
                    
                    int indices[6] = {0, 1, 2, 0, 2, 3};
                    SDL_RenderGeometry(r, NULL, vertices, 4, indices, 6);
                }
                
                // Border lines
                SDL_SetRenderDrawColor(r, 255, 255, 255, i == hovered_index ? 255 : 180);
                SDL_RenderLine(r, 
                    cx + inner_radius * cosf(start_angle), cy + inner_radius * sinf(start_angle),
                    cx + radius * cosf(start_angle), cy + radius * sinf(start_angle));
            }
            
            // Render condition names in each segment
            for (int i = 0; i < COND_COUNT; i++) {
                float mid_angle = (6.28318f * i + 6.28318f * (i + 1)) / (2.0f * COND_COUNT);
                float mid_radius = (inner_radius + radius) / 2.0f;
                float text_x = cx + cosf(mid_angle) * mid_radius;
                float text_y = cy + sinf(mid_angle) * mid_radius;
                
                // Render text from pre-baked textures
                if (g.cond_wheel_tex[i]) {
                    float tw, th;
                    SDL_GetTextureSize(g.cond_wheel_tex[i], &tw, &th);
                    SDL_RenderTexture(r, g.cond_wheel_tex[i], NULL, &(SDL_FRect){text_x - tw/2.0f, text_y - th/2.0f, tw, th});
                }
            }
            
            // Center circle
            SDL_SetRenderDrawColor(r, 40, 40, 60, 240);
            int ir = (int)inner_radius;
            for (int y = -ir; y <= ir; y++) {
                int half_width = (int)sqrtf(inner_radius * inner_radius - y * y);
                if (half_width > 0) {
                    SDL_FRect line = {cx - half_width, cy + y, half_width * 2, 1};
                    SDL_RenderFillRect(r, &line);
                }
            }
            
            // Outer circle border
            SDL_SetRenderDrawColor(r, 255, 255, 255, 200);
            for (int i = 0; i < 360; i += 2) {
                float a = i * 0.0174533f;
                SDL_RenderPoint(r, cx + radius * cosf(a), cy + radius * sinf(a));
            }
        }
    }
    
    SDL_RenderPresent(r);
}

// Helper for saving embedded PNG data
// Helper struct for PNG writing
typedef struct {
    unsigned char *data;
    int size;
    int capacity;
} PNGBuffer;

static void png_write_callback(void *context, void *data, int size) {
    PNGBuffer *buf = (PNGBuffer*)context;
    if (buf->size + size > buf->capacity) {
        buf->capacity = (buf->size + size) * 2;
        buf->data = realloc(buf->data, buf->capacity);
    }
    memcpy(buf->data + buf->size, data, size);
    buf->size += size;
}

static void write_embedded_asset(FILE *f, Asset *asset) {
    ensure_asset_loaded(asset);
    
    // Write path
    int path_len = strlen(asset->path);
    fwrite(&path_len, 4, 1, f);
    fwrite(asset->path, 1, path_len, f);
    
    // Read image from file
    int w, h;
    unsigned char *data = stbi_load(asset->path, &w, &h, NULL, 4);
    if (data) {
        // Write PNG to memory buffer using callback
        PNGBuffer buf = {0};
        buf.capacity = w * h * 4;
        buf.data = malloc(buf.capacity);
        
        if (stbi_write_png_to_func(png_write_callback, &buf, w, h, 4, data, w * 4)) {
            fwrite(&buf.size, 4, 1, f);
            fwrite(buf.data, 1, buf.size, f);
        } else {
            int zero = 0;
            fwrite(&zero, 4, 1, f);
        }
        
        free(buf.data);
        stbi_image_free(data);
    } else {
        int zero = 0;
        fwrite(&zero, 4, 1, f);
    }
}

// Helper for loading embedded PNG data
static int read_embedded_asset(FILE *f, int *out_idx, Asset *lib, int *lib_count, int max_count) {
    // Read path
    int path_len;
    if (fread(&path_len, 4, 1, f) != 1) return -1;
    char path[256] = {0};
    if (path_len > 0 && path_len < 256) {
        fread(path, 1, path_len, f);
    }
    
    // Read PNG data
    int png_len;
    if (fread(&png_len, 4, 1, f) != 1) return -1;
    if (png_len <= 0 || png_len > 50*1024*1024) return -1; // Sanity check: max 50MB
    
    unsigned char *png_data = malloc(png_len);
    if (!png_data) return -1;
    if (fread(png_data, 1, png_len, f) != (size_t)png_len) {
        free(png_data);
        return -1;
    }
    
    // Check if asset already exists in library
    for (int i = 0; i < *lib_count; i++) {
        if (strcmp(lib[i].path, path) == 0) {
            free(png_data);
            *out_idx = i;
            return 0;
        }
    }
    
    // Add new asset to library
    if (*lib_count >= max_count) {
        free(png_data);
        return -1;
    }
    
    int idx = *lib_count;
    if (load_asset_from_memory(png_data, png_len, &lib[idx], path) == 0) {
        (*lib_count)++;
        *out_idx = idx;
        free(png_data);
        return 0;
    }
    
    free(png_data);
    return -1;
}

static void handle_input() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) exit(0);
        
        if (e.type == SDL_EVENT_KEY_DOWN) {
            g.shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
            g.ctrl = (e.key.mod & SDL_KMOD_CTRL) != 0;
            SDL_Keycode k = e.key.key;
            
            if (!g.dmg_input && !g.cond_wheel) {
                // Only allow tool switching if no token is selected
                bool any_selected = false;
                for (int i = 0; i < g.token_count; i++) {
                    if (g.tokens[i].selected) { any_selected = true; break; }
                }
                
                if (!any_selected) {
                    if (k == SDLK_1) g.tool = TOOL_SELECT;
                    if (k == SDLK_2) g.tool = TOOL_FOG;
                    if (k == SDLK_3) g.tool = TOOL_SQUAD;
                    if (k == SDLK_4) g.tool = TOOL_DRAW;
                }
            }
            
            if ((g.tool == TOOL_SQUAD || g.tool == TOOL_DRAW) && (k == SDLK_Q || k == SDLK_E)) {
                g.current_squad = (g.current_squad + (k == SDLK_E ? 1 : -1) + 8) % 8;
            }
            
            if (g.current_shape == SHAPE_RECT && g.tool == TOOL_DRAW && k == SDLK_W) {
                g.current_shape = SHAPE_CIRCLE;
            } else if (g.current_shape == SHAPE_CIRCLE && g.tool == TOOL_DRAW && k == SDLK_W) {
                g.current_shape = SHAPE_RECT;
            }
            
            if (k == SDLK_C && !g.cal_active) {
                g.cal_active = true; g.cal_drag = false;
                g.cal_cells_w = g.cal_cells_h = 2;
            }
            
            if (g.cal_active) {
                if (k == SDLK_RETURN) {
                    int w = abs(g.cal_x2 - g.cal_x1), h = abs(g.cal_y2 - g.cal_y1);
                    if (w > 10 && h > 10) {
                        g.grid_size = (w/g.cal_cells_w + h/g.cal_cells_h)/2;
                        g.grid_off_x = (g.cal_x1 < g.cal_x2 ? g.cal_x1 : g.cal_x2) % g.grid_size;
                        g.grid_off_y = (g.cal_y1 < g.cal_y2 ? g.cal_y1 : g.cal_y2) % g.grid_size;
                        fog_init((g.map_w + g.grid_size)/g.grid_size, (g.map_h + g.grid_size)/g.grid_size);
                    }
                    g.cal_active = false;
                }
                if (k == SDLK_ESCAPE) g.cal_active = false;
                if (g.cal_drag) {
                    if (k == SDLK_UP) g.cal_cells_h++;
                    if (k == SDLK_DOWN && g.cal_cells_h > 1) g.cal_cells_h--;
                    if (k == SDLK_RIGHT) g.cal_cells_w++;
                    if (k == SDLK_LEFT && g.cal_cells_w > 1) g.cal_cells_w--;
                }
                continue;
            }
            
            if (k == SDLK_DELETE || k == SDLK_BACKSPACE) {
                for (int i = 0; i < g.token_count; i++) {
                    if (g.tokens[i].selected) {
                        if (g.tokens[i].damage_tex[0]) SDL_DestroyTexture(g.tokens[i].damage_tex[0]);
                        if (g.tokens[i].damage_tex[1]) SDL_DestroyTexture(g.tokens[i].damage_tex[1]);
                        memmove(&g.tokens[i], &g.tokens[i+1], (g.token_count-i-1)*sizeof(Token));
                        g.token_count--;
                        break;
                    }
                }
            }
            
            if (k == SDLK_H) {
                for (int i = 0; i < g.token_count; i++) 
                    if (g.tokens[i].selected) g.tokens[i].hidden = !g.tokens[i].hidden;
            }
            
            if (k == SDLK_D) {
                if (g.shift) {
                    for (int i = 0; i < g.token_count; i++) g.tokens[i].opacity = 255;
                } else {
                    for (int i = 0; i < g.token_count; i++) 
                        if (g.tokens[i].selected) g.tokens[i].opacity = (g.tokens[i].opacity == 255) ? 128 : 255;
                }
            }
            
            if (k == SDLK_RETURN && !g.dmg_input) {
                for (int i = 0; i < g.token_count; i++) {
                    if (g.tokens[i].selected) { 
                        g.dmg_input = true; 
                        g.dmg_buf[0] = 0; 
                        g.dmg_len = 0; 
                        break; 
                    }
                }
            } else if (g.dmg_input) {
                if (k == SDLK_RETURN) {
                    int val = atoi(g.dmg_buf);
                    if (g.shift) val = -val;
                    for (int i = 0; i < g.token_count; i++) {
                        if (g.tokens[i].selected) {
                            g.tokens[i].damage += val;
                            if (g.tokens[i].damage < 0) g.tokens[i].damage = 0;
                        }
                    }
                    g.dmg_input = false;
                } else if (k == SDLK_ESCAPE) {
                    g.dmg_input = false;
                } else if (k == SDLK_BACKSPACE && g.dmg_len > 0) {
                    g.dmg_buf[--g.dmg_len] = 0;
                } else if (k >= SDLK_0 && k <= SDLK_9 && g.dmg_len < 15) {
                    g.dmg_buf[g.dmg_len++] = '0' + (k - SDLK_0);
                    g.dmg_buf[g.dmg_len] = 0;
                } else if (k >= SDLK_KP_0 && k <= SDLK_KP_9 && g.dmg_len < 15) {
                    g.dmg_buf[g.dmg_len++] = '0' + (k - SDLK_KP_0);
                    g.dmg_buf[g.dmg_len] = 0;
                }
                continue;
            }
            
            if (k == SDLK_A && !g.cond_wheel) {
                for (int i = 0; i < g.token_count; i++) {
                    if (g.tokens[i].selected) { g.cond_wheel = true; g.cond_token_idx = i; break; }
                }
            } else if (g.cond_wheel && k == SDLK_ESCAPE) {
                g.cond_wheel = false;
            }
            
            if (k == SDLK_ESCAPE && g.measure_active) {
                g.measure_active = false;
            }
            
            if (k >= SDLK_F1 && k <= SDLK_F12) {
                int slot = k - SDLK_F1;
                char path[64]; snprintf(path, 64, "saves/slot_%d.vtt", slot);
                uint32_t magic = SAVE_MAGIC;
                if (g.shift) {
                    // SAVE
                    FILE *f = fopen(path, "wb");
                    if (f) {
                        // Write header
                        fwrite(&magic, 4, 1, f);
                        fwrite(&g.fog_w, 4, 1, f);
                        fwrite(&g.fog_h, 4, 1, f);
                        fwrite(&g.grid_size, 4, 1, f);
                        fwrite(&g.grid_off_x, 4, 1, f);
                        fwrite(&g.grid_off_y, 4, 1, f);
                        fwrite(&g.cam[0].target_x, 4, 1, f);
                        fwrite(&g.cam[0].target_y, 4, 1, f);
                        fwrite(&g.cam[0].target_zoom, 4, 1, f);
                        
                        // Write embedded map asset
                        if (g.map_current < g.map_count) {
                            write_embedded_asset(f, &g.map_assets[g.map_current]);
                        } else {
                            int zero = 0;
                            fwrite(&zero, 4, 1, f);
                            fwrite(&zero, 4, 1, f);
                        }
                        
                        // Write token count and tokens with embedded assets
                        fwrite(&g.token_count, 4, 1, f);
                        for (int i = 0; i < g.token_count; i++) {
                            Token *t = &g.tokens[i];
                            fwrite(&t->grid_x, 4, 1, f);
                            fwrite(&t->grid_y, 4, 1, f);
                            fwrite(&t->size, 4, 1, f);
                            fwrite(&t->damage, 4, 1, f);
                            fwrite(&t->squad, 4, 1, f);
                            fwrite(&t->opacity, 1, 1, f);
                            fwrite(&t->hidden, 1, 1, f);
                            fwrite(t->cond, 1, COND_COUNT, f);
                            
                            // Write embedded token image
                            write_embedded_asset(f, &g.token_lib[t->image_idx]);
                        }
                        
                        // Write fog data
                        fwrite(g.fog, 1, g.fog_w*g.fog_h, f);
                        fclose(f);
                        printf("Saved to slot %d\n", slot + 1);
                    }
                } else {
                    // LOAD
                    FILE *f = fopen(path, "rb");
                    if (f) {
                        uint32_t rmagic;
                        fread(&rmagic, 4, 1, f);
                        if (rmagic == SAVE_MAGIC) {
                            // Read header
                            int fw, fh;
                            fread(&fw, 4, 1, f);
                            fread(&fh, 4, 1, f);
                            if (fw != g.fog_w || fh != g.fog_h) fog_init(fw, fh);
                            fread(&g.grid_size, 4, 1, f);
                            fread(&g.grid_off_x, 4, 1, f);
                            fread(&g.grid_off_y, 4, 1, f);
                            fread(&g.cam[0].target_x, 4, 1, f);
                            fread(&g.cam[0].target_y, 4, 1, f);
                            fread(&g.cam[0].target_zoom, 4, 1, f);
                            g.cam[0].x = g.cam[0].target_x;
                            g.cam[0].y = g.cam[0].target_y;
                            g.cam[0].zoom = g.cam[0].target_zoom;
                            
                            // Read embedded map asset
                            int map_idx;
                            if (read_embedded_asset(f, &map_idx, g.map_assets, &g.map_count, MAX_ASSETS) == 0) {
                                g.map_current = map_idx;
                                g.map_w = g.map_assets[map_idx].w;
                                g.map_h = g.map_assets[map_idx].h;
                            }
                            
                            // Read tokens with embedded assets
                            fread(&g.token_count, 4, 1, f);
                            for (int i = 0; i < g.token_count && i < MAX_TOKENS; i++) {
                                Token *t = &g.tokens[i];
                                fread(&t->grid_x, 4, 1, f);
                                fread(&t->grid_y, 4, 1, f);
                                fread(&t->size, 4, 1, f);
                                fread(&t->damage, 4, 1, f);
                                fread(&t->squad, 4, 1, f);
                                fread(&t->opacity, 1, 1, f);
                                fread(&t->hidden, 1, 1, f);
                                fread(t->cond, 1, COND_COUNT, f);
                                t->selected = false;
                                t->damage_tex[0] = t->damage_tex[1] = NULL;
                                t->cached_dmg[0] = t->cached_dmg[1] = -1;
                                
                                // Read embedded token image
                                int tok_idx;
                                if (read_embedded_asset(f, &tok_idx, g.token_lib, &g.token_lib_count, MAX_ASSETS) == 0) {
                                    t->image_idx = tok_idx;
                                } else {
                                    t->image_idx = 0;
                                }
                            }
                            
                            // Read fog data
                            if (g.fog) fread(g.fog, 1, g.fog_w*g.fog_h, f);
                            printf("Loaded from slot %d\n", slot + 1);
                        }
                        fclose(f);
                    }
                }
            }
            
            if (k == SDLK_M) {
                if (g.shift) g.map_current = (g.map_current - 1 + g.map_count) % g.map_count;
                else g.map_current = (g.map_current + 1) % g.map_count;
                if (g.map_current < g.map_count) {
                    ensure_asset_loaded(&g.map_assets[g.map_current]);
                    g.map_w = g.map_assets[g.map_current].w;
                    g.map_h = g.map_assets[g.map_current].h;
                }
            }
            
            if (k == SDLK_P) g.sync_views = !g.sync_views;
            if (k == SDLK_G) g.show_grid = !g.show_grid;
            
            if (k == SDLK_X && g.tool == TOOL_DRAW) {
                g.drawing_count = 0;
            }
            
            if ((k == SDLK_EQUALS || k == SDLK_KP_PLUS)) {
                for (int i = 0; i < g.token_count; i++) 
                    if (g.tokens[i].selected && g.tokens[i].size < 4) g.tokens[i].size++;
            }
            if ((k == SDLK_MINUS || k == SDLK_KP_MINUS)) {
                for (int i = 0; i < g.token_count; i++) 
                    if (g.tokens[i].selected && g.tokens[i].size > 1) g.tokens[i].size--;
            }
            
            if (!g.dmg_input && k >= SDLK_1 && k <= SDLK_9) {
                int dmg = (k - SDLK_0);
                if (g.shift) dmg = -dmg;
                for (int i = 0; i < g.token_count; i++) {
                    if (g.tokens[i].selected) {
                        g.tokens[i].damage += dmg;
                        if (g.tokens[i].damage < 0) g.tokens[i].damage = 0;
                    }
                }
            }
            if (!g.dmg_input && k == SDLK_0) {
                int dmg = 10;
                if (g.shift) dmg = -10;
                for (int i = 0; i < g.token_count; i++) {
                    if (g.tokens[i].selected) {
                        g.tokens[i].damage += dmg;
                        if (g.tokens[i].damage < 0) g.tokens[i].damage = 0;
                    }
                }
            }
            
            if (k == SDLK_ESCAPE && !g.cond_wheel && !g.dmg_input) {
                for (int i = 0; i < g.token_count; i++) g.tokens[i].selected = false;
            }
        }
        
        if (e.type == SDL_EVENT_KEY_UP) {
            g.shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
            g.ctrl = (e.key.mod & SDL_KMOD_CTRL) != 0;
        }
        
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            float mx = e.button.x, my = e.button.y;
            int gx, gy; 
            screen_to_grid(mx, my, &g.cam[0], &gx, &gy);
            
            // Alt+Click to start measurement, Left Click to end measurement
            SDL_Keymod mods = SDL_GetModState();
            if (e.button.button == 1 && (mods & SDL_KMOD_ALT)) {
                if (!g.measure_active) {
                    // Start measurement
                    g.measure_active = true;
                    g.measure_start_gx = gx;
                    g.measure_start_gy = gy;
                } else {
                    // End measurement (Alt+Click again)
                    g.measure_active = false;
                }
            } else if (e.button.button == 1 && g.measure_active) {
                // Left click (without Alt) ends measurement
                g.measure_active = false;
            } else if (g.cal_active && e.button.button == 1) {
                g.cal_x1 = g.cal_x2 = (int)(mx/g.cam[0].zoom + g.cam[0].x);
                g.cal_y1 = g.cal_y2 = (int)(my/g.cam[0].zoom + g.cam[0].y);
                g.cal_drag = true;
            } else if (e.button.button == 1) {
                if (g.cond_wheel) {
                    float cx = g.dm.w/2.0f, cy = g.dm.h/2.0f;
                    float radius = 220.0f;
                    float inner_radius = 70.0f;
                    
                    float dx = mx - cx, dy = my - cy;
                    float dist = sqrtf(dx*dx + dy*dy);
                    
                    if (dist >= inner_radius && dist <= radius && g.cond_token_idx >= 0) {
                        float angle = atan2f(dy, dx);
                        if (angle < 0) angle += 6.28318f;
                        float segment_angle = 6.28318f / COND_COUNT;
                        int clicked_index = (int)(angle / segment_angle);
                        if (clicked_index >= 0 && clicked_index < COND_COUNT) {
                            g.tokens[g.cond_token_idx].cond[clicked_index] = !g.tokens[g.cond_token_idx].cond[clicked_index];
                        }
                    }
                } else if (g.tool == TOOL_SELECT) {
                    Token *hit = NULL;
                    int hit_idx = -1;
                    for (int i = g.token_count-1; i >= 0; i--) {
                        if (g.tokens[i].grid_x == gx && g.tokens[i].grid_y == gy) {
                            hit = &g.tokens[i];
                            hit_idx = i;
                            break;
                        }
                    }
                    
                    if (hit && (g.shift || g.ctrl) && g.token_count < MAX_TOKENS) {
                        for (int j = 0; j < g.token_count; j++) g.tokens[j].selected = false;
                        g.tokens[g.token_count] = *hit;
                        g.tokens[g.token_count].selected = true;
                        g.tokens[g.token_count].damage_tex[0] = g.tokens[g.token_count].damage_tex[1] = NULL;
                        g.tokens[g.token_count].cached_dmg[0] = g.tokens[g.token_count].cached_dmg[1] = -1;
                        g.tokens[g.token_count].grid_x = gx;
                        g.tokens[g.token_count].grid_y = gy;
                        g.drag_token = true;
                        g.drag_idx = g.token_count++;
                    } else if (hit) {
                        if (!g.shift && !g.ctrl) 
                            for (int j = 0; j < g.token_count; j++) g.tokens[j].selected = false;
                        g.tokens[hit_idx].selected = true;
                        g.drag_token = true;
                        g.drag_idx = hit_idx;
                    } else {
                        for (int j = 0; j < g.token_count; j++) g.tokens[j].selected = false;
                    }
                } else if (g.tool == TOOL_FOG) {
                    g.paint_fog = true;
                    g.fog_mode = fog_get(gx, gy);
                    fog_set(gx, gy, !g.fog_mode);
                } else if (g.tool == TOOL_SQUAD) {
                    for (int i = 0; i < g.token_count; i++) {
                        if (g.tokens[i].grid_x == gx && g.tokens[i].grid_y == gy) {
                            g.tokens[i].squad = (g.tokens[i].squad == g.current_squad) ? -1 : g.current_squad;
                        }
                    }
                } else if (g.tool == TOOL_DRAW) {
                    g.draw_shape = true;
                    g.paint_start_x = (int)(mx/g.cam[0].zoom + g.cam[0].x);
                    g.paint_start_y = (int)(my/g.cam[0].zoom + g.cam[0].y);
                }
            } else if (e.button.button == 3) {
                g.last_mx = mx; g.last_my = my;
            } else if (e.button.button == 2 && g.tool == TOOL_DRAW) {
                int wx = (int)(mx/g.cam[0].zoom + g.cam[0].x);
                int wy = (int)(my/g.cam[0].zoom + g.cam[0].y);
                for (int i = g.drawing_count-1; i >= 0; i--) {
                    Drawing *d = &g.drawings[i];
                    if (d->type == SHAPE_RECT) {
                        if (wx >= fmin(d->x1,d->x2) && wx <= fmax(d->x1,d->x2) &&
                            wy >= fmin(d->y1,d->y2) && wy <= fmax(d->y1,d->y2)) {
                            memmove(&g.drawings[i], &g.drawings[i+1], (g.drawing_count-i-1)*sizeof(Drawing));
                            g.drawing_count--;
                            break;
                        }
                    } else {
                        int cx = (d->x1+d->x2)/2, cy = (d->y1+d->y2)/2;
                        int r2 = ((d->x2-d->x1)*(d->x2-d->x1) + (d->y2-d->y1)*(d->y2-d->y1))/4;
                        if ((wx-cx)*(wx-cx) + (wy-cy)*(wy-cy) <= r2) {
                            memmove(&g.drawings[i], &g.drawings[i+1], (g.drawing_count-i-1)*sizeof(Drawing));
                            g.drawing_count--;
                            break;
                        }
                    }
                }
            }
        }
        
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            if (g.cal_active && e.button.button == 1) {
                g.cal_drag = false;
            } else if (e.button.button == 1) {
                g.drag_token = false;
                if (g.draw_shape && g.drawing_count < MAX_DRAWINGS) {
                    float mx = e.button.x, my = e.button.y;
                    int ex = (int)(mx/g.cam[0].zoom + g.cam[0].x);
                    int ey = (int)(my/g.cam[0].zoom + g.cam[0].y);
                    if (abs(ex - g.paint_start_x) > 5 || abs(ey - g.paint_start_y) > 5) {
                        Drawing *d = &g.drawings[g.drawing_count++];
                        d->type = g.current_shape;
                        d->x1 = g.paint_start_x; d->y1 = g.paint_start_y;
                        d->x2 = ex; d->y2 = ey;
                        d->color = g.current_squad;
                    }
                }
                g.draw_shape = false;
                g.paint_fog = false;
            }
        }
        
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            float mx = e.motion.x, my = e.motion.y;
            if (g.cal_drag) {
                g.cal_x2 = (int)(mx/g.cam[0].zoom + g.cam[0].x);
                g.cal_y2 = (int)(my/g.cam[0].zoom + g.cam[0].y);
            } else if (g.drag_token && g.drag_idx >= 0) {
                int gx, gy; 
                screen_to_grid(mx, my, &g.cam[0], &gx, &gy);
                g.tokens[g.drag_idx].grid_x = gx;
                g.tokens[g.drag_idx].grid_y = gy;
            } else if (g.paint_fog) {
                int gx, gy; 
                screen_to_grid(mx, my, &g.cam[0], &gx, &gy);
                fog_set(gx, gy, !g.fog_mode);
            } else if (e.motion.state & SDL_BUTTON_MASK(3)) {
                g.cam[0].target_x -= (mx - g.last_mx)/g.cam[0].zoom;
                g.cam[0].target_y -= (my - g.last_my)/g.cam[0].zoom;
                g.last_mx = mx; g.last_my = my;
            }
        }
        
        if (e.type == SDL_EVENT_MOUSE_WHEEL) {
            float mx, my;
            SDL_GetMouseState(&mx, &my);
            cam_zoom(&g.cam[0], mx, my, e.wheel.y > 0 ? 1.1f : 0.9f);
        }
        
        if (e.type == SDL_EVENT_DROP_FILE) {
            if (is_image(e.drop.data) && g.token_count < MAX_TOKENS) {
                float mx, my;
                SDL_GetMouseState(&mx, &my);
                int gx, gy; 
                screen_to_grid(mx, my, &g.cam[0], &gx, &gy);
                int idx = find_or_load_token_image(e.drop.data);
                if (idx >= 0) {
                    Token *t = &g.tokens[g.token_count++];
                    memset(t, 0, sizeof(Token));
                    t->grid_x = gx; t->grid_y = gy; t->size = 1;
                    t->image_idx = idx; t->opacity = 255;
                    t->squad = -1;
                }
            }
        }
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    
    SDL_Init(SDL_INIT_VIDEO);
    
    g.dm.win = SDL_CreateWindow("DM View", 1280, 720, SDL_WINDOW_RESIZABLE);
    g.player.win = SDL_CreateWindow("Player View", 1280, 720, SDL_WINDOW_RESIZABLE);
    g.dm.ren = SDL_CreateRenderer(g.dm.win, NULL);
    g.player.ren = SDL_CreateRenderer(g.player.win, NULL);
    g.dm.id = SDL_GetWindowID(g.dm.win);
    g.player.id = SDL_GetWindowID(g.player.win);
    
    // Load font: try embedded first, then file, then system fonts
    bool font_loaded = false;
    
    #ifdef EMBED_FONT
    // Use embedded font data (compiled into executable)
    g.font_data = malloc(embedded_font_size);
    if (g.font_data) {
        memcpy(g.font_data, embedded_font_data, embedded_font_size);
        if (stbtt_InitFont(&g.font, g.font_data, stbtt_GetFontOffsetForIndex(g.font_data, 0))) {
            font_loaded = true;
            printf("Using embedded font\n");
        } else {
            free(g.font_data);
            g.font_data = NULL;
        }
    }
    #endif
    
    // Fallback to font files if no embedded font
    if (!font_loaded) {
        const char *font_paths[] = {
            "font.ttf",  // Local font
            #ifdef _WIN32
            "C:/Windows/Fonts/arial.ttf",
            "C:/Windows/Fonts/calibri.ttf",
            "C:/Windows/Fonts/segoeui.ttf",
            #else
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/System/Library/Fonts/Helvetica.ttc",  // macOS
            #endif
        };
        
        FILE *f = NULL;
        const char *loaded_path = NULL;
        for (int i = 0; i < (int)ARRAY_COUNT(font_paths) && !f; i++) {
            f = fopen(font_paths[i], "rb");
            if (f) loaded_path = font_paths[i];
        }
        
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            g.font_data = malloc(sz);
            fseek(f, 0, SEEK_SET);
            fread(g.font_data, 1, sz, f);
            fclose(f);
            if (stbtt_InitFont(&g.font, g.font_data, stbtt_GetFontOffsetForIndex(g.font_data, 0))) {
                font_loaded = true;
                printf("Using font: %s\n", loaded_path);
            }
        }
    }
    
    if (!font_loaded) {
        printf("Warning: No font found. Text rendering will be disabled.\n");
        printf("To embed a font: python embed_font.py font.ttf > font_embedded.h\n");
        printf("Then compile with: -DEMBED_FONT\n");
    }
    
    SDL_Color white = {255, 255, 255, 255};
    const char *cond_abbrev[] = {"BL","DA","FR","GR","RE","SL","TA","WE"};
    const char *cond_names[] = {"Bleeding","Dazed","Frightened","Grabbed","Restrained","Slowed","Taunted","Weakened"};
    for (int i = 0; i < COND_COUNT; i++) {
        g.cond_tex[0][i] = bake_text_once(g.dm.ren, cond_abbrev[i], &g.cond_w, &g.cond_h, white, 16.0f);
        g.cond_tex[1][i] = bake_text_once(g.player.ren, cond_abbrev[i], &g.cond_w, &g.cond_h, white, 16.0f);
        int dummy_w, dummy_h;
        g.cond_wheel_tex[i] = bake_text_once(g.dm.ren, cond_names[i], &dummy_w, &dummy_h, white, 16.0f);
    }
    
    scan_assets("assets/maps", g.map_assets, &g.map_count);
    scan_assets("assets/tokens", g.token_lib, &g.token_lib_count);
    
    if (g.map_count > 0) {
        // Only load the first map on startup for faster loading
        g.map_current = 0;
        load_asset_to_both(g.map_assets[0].path, &g.map_assets[0]);
        g.map_w = g.map_assets[0].w;
        g.map_h = g.map_assets[0].h;
        g.grid_size = 64;
        g.grid_off_x = g.grid_off_y = 0;
        fog_init((g.map_w+64)/64, (g.map_h+64)/64);
    }
    
    g.cam[0].target_zoom = g.cam[1].target_zoom = 1.0f;
    g.current_squad = 0;
    g.current_shape = SHAPE_RECT;
    g.tool = TOOL_SELECT;
    g.show_grid = true;
    g.sync_views = true;
    g.cached_tool = TOOL_FOG;
    
    printf("VTT started. Controls:\n");
    printf("  1 - Select tool, 2 - Fog tool, 3 - Squad assignment tool, 4 - Draw tool\n");
    printf("  Left click - Select/move tokens, toggle fog, assign squad, or draw shapes\n");
    printf("  Right click - Pan camera (drag) / Delete drawing (middle-click in draw mode)\n");
    printf("  Mouse Wheel - Zoom in/out at cursor\n");
    printf("  ALT+Click - Start/end measurement tool (shows distance in grid cells)\n");
    printf("  W - Cycle shape (in draw mode)\n");
    printf("  Q/E - Cycle colors (in squad/draw mode)\n");
    printf("  A - Open condition wheel for selected token\n");
    printf("  D - Toggle token opacity (50%% downed / 100%% normal)\n");
    printf("  SHIFT+D - Reset all token opacities to 100%%\n");
    printf("  X - Clear all drawings (in draw mode)\n");
    printf("  P - Toggle player view sync to DM view\n");
    printf("  G - Toggle grid overlay\n");
    printf("  M - Cycle to next map, SHIFT+M - Previous map\n");
    printf("  C - Enter grid calibration mode\n");
    printf("  H - Toggle selected token hidden/visible\n");
    printf("  +/- - Resize selected token\n");
    printf("  1-9 - Add damage to selected token\n");
    printf("  SHIFT+1-9 - Heal (subtract damage) from selected token\n");
    printf("  0 - Add 10 damage to selected token\n");
    printf("  SHIFT+0 - Heal 10 damage from selected token\n");
    printf("  ENTER - Type multi-digit damage (Hold SHIFT to heal)\n");
    printf("  DELETE/BACKSPACE - Remove selected token\n");
    printf("  Drag & Drop - Drop image files onto DM window to add tokens\n");
    printf("  SHIFT+F1-F12 - Save to slot\n");
    printf("  F1-F12 - Load from slot\n");
    printf("  ESC - Deselect all / Cancel damage input / Close condition wheel\n");
    
    while (1) {
        handle_input();
        
        cam_update(&g.cam[0]);
        if (g.sync_views) {
            g.cam[1].target_x = g.cam[0].target_x;
            g.cam[1].target_y = g.cam[0].target_y;
            g.cam[1].target_zoom = g.cam[0].target_zoom;
        }
        cam_update(&g.cam[1]);
        
        render_view(0);
        render_view(1);
        
        SDL_Delay(16);
    }
    
    return 0;
}