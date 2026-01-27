// ============================================================================
// INCLUDES AND CONSTANTS
// ============================================================================

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

// stb_image.h - single-header image loader supporting:
// JPEG, PNG, BMP, TGA, PSD, GIF, HDR, PIC, PNM
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// stb_truetype.h - TrueType font rendering
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define DEFAULT_GRID_SIZE 64
#define MAX_TOKENS 100
#define MAX_SAVE_SLOTS 12
#define WINDOW_DM_WIDTH 1280
#define WINDOW_DM_HEIGHT 720
#define WINDOW_PLAYER_WIDTH 1280
#define WINDOW_PLAYER_HEIGHT 720

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// CAMERA & ZOOM CONSTANTS
// ============================================================================

#define ZOOM_MIN 0.25f
#define ZOOM_MAX 4.0f
#define ZOOM_SPEED 0.1f
#define CAMERA_SPEED 10
#define CAMERA_SMOOTHING 0.15f

// ============================================================================
// DATA STRUCTURES
// ============================================================================

typedef enum {
    CONDITION_BLEEDING = 0,
    CONDITION_DAZED,
    CONDITION_FRIGHTENED,
    CONDITION_GRABBED,
    CONDITION_RESTRAINED,
    CONDITION_SLOWED,
    CONDITION_TAUNTED,
    CONDITION_WEAKEND,
    CONDITION_COUNT
} ConditionType;

typedef struct {
    const char *name;
    uint8_t r, g, b;  // Color for display
} ConditionInfo;

static const ConditionInfo CONDITION_INFO[CONDITION_COUNT] = {
    {"Bleeding", 220, 20, 20},      // Bright Red
    {"Dazed", 255, 215, 0},         // Gold/Yellow
    {"Frightened", 147, 51, 234},   // Purple
    {"Grabbed", 255, 140, 0},       // Dark Orange
    {"Restrained", 139, 69, 19},    // Saddle Brown
    {"Slowed", 30, 144, 255},       // Dodger Blue
    {"Taunted", 255, 20, 147},      // Deep Pink
    {"Weakend", 50, 205, 50}        // Lime Green
};

typedef enum {
    WINDOW_TYPE_DM,
    WINDOW_TYPE_PLAYER
} WindowType;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_WindowID id;
    WindowType type;
    bool is_open;
} WindowState;

typedef struct {
    WindowState dm_window;
    WindowState player_window;
} WindowManager;

typedef struct {
    SDL_Texture *dm_texture;
    SDL_Texture *player_texture;
    int width;
    int height;
    int grid_cols;
    int grid_rows;
    int grid_size;           // Variable grid size (calibrated or default)
    int grid_offset_x;       // Grid offset for alignment (calibrated)
    int grid_offset_y;       // Grid offset for alignment (calibrated)
    char filepath[256];
    char name[64];           // Display name extracted from filename
} Map;

typedef struct {
    SDL_Texture *dm_texture;
    SDL_Texture *player_texture;
    int grid_x;
    int grid_y;
    int width;
    int height;
    int grid_size;  // Number of grid cells wide (1 for 1x1, 2 for 2x2, etc.)
    bool selected;
    bool hidden;     // If true, token is hidden from player view
    int damage;      // Amount of damage taken
    int squad;       // Squad/group index (-1 = no squad, 0-7 = squad color)
    uint8_t opacity; // Opacity (255 = fully opaque, 128 = semi-transparent for "dead" tokens)
    bool conditions[CONDITION_COUNT];  // Active conditions
    char name[64];
    char filepath[256];
    
    // Cached damage number rendering (performance optimization)
    SDL_Texture *damage_texture_dm;
    SDL_Texture *damage_texture_player;
    int cached_damage_dm;      // Track cached damage for DM view
    int cached_damage_player;  // Track cached damage for player view
    float damage_tex_width;
    float damage_tex_height;
} Token;

typedef struct {
    Token tokens[MAX_TOKENS];
    int count;
} TokenManager;

typedef struct {
    bool **cells;
    int cols;
    int rows;
} FogOfWar;

typedef enum {
    TOOL_SELECT,
    TOOL_FOG,
    TOOL_SQUAD,
    TOOL_DRAW
} EditorTool;



#define SQUAD_COLOR_COUNT 8
typedef struct {
    uint8_t r, g, b;
    const char *name;
} SquadColor;

static const SquadColor SQUAD_COLORS[SQUAD_COLOR_COUNT] = {
    {255, 50, 50, "Red"},      // 0
    {50, 150, 255, "Blue"},    // 1
    {50, 255, 50, "Green"},    // 2
    {255, 255, 50, "Yellow"},  // 3
    {255, 150, 50, "Orange"},  // 4
    {200, 50, 255, "Purple"},  // 5
    {50, 255, 255, "Cyan"},    // 6
    {255, 255, 255, "White"}   // 7
};

// Drawing shapes
#define MAX_DRAWINGS 100
typedef enum {
    SHAPE_RECTANGLE,
    SHAPE_CIRCLE
} ShapeType;

typedef struct {
    ShapeType type;
    int x1, y1;  // Start position (world coordinates)
    int x2, y2;  // End position (world coordinates)
    int color_index;
    bool filled;
    SDL_Texture *cached_texture_dm;      // Cached rendering (DM view)
    SDL_Texture *cached_texture_player;  // Cached rendering (player view)
    int cached_width, cached_height;
} Drawing;

typedef struct {
    Drawing drawings[MAX_DRAWINGS];
    int count;
} DrawingManager;

typedef struct {
    EditorTool current_tool;
    int mouse_x;
    int mouse_y;
    bool mouse_left_down;
    bool mouse_right_down;
    int last_mouse_x;
    int last_mouse_y;
    bool dragging_token;
    int dragged_token_index;
    bool painting_fog;       // Currently painting fog of war
    bool fog_paint_mode;     // True = add fog, False = remove fog
    bool shift_pressed;
    bool ctrl_pressed;
    bool show_grid;          // Grid visibility toggle
    bool damage_input_mode;  // Currently entering damage value
    char damage_input_buffer[16];  // Buffer for damage input
    int damage_input_len;    // Length of current input
    int current_squad;       // Current squad color index (0-7)
    bool drawing_shape;      // Currently drawing a shape
    ShapeType current_shape; // Current shape type (rectangle or circle)
    int draw_start_x, draw_start_y;  // Drawing start position (world coords)
    bool condition_wheel_open;  // Condition selection wheel is open
    int condition_wheel_token_index;  // Which token the wheel is for
} InputState;

typedef struct {
    uint32_t magic;
    char map_path[256];
    uint32_t token_count;
    uint32_t fog_cols;
    uint32_t fog_rows;
    uint32_t grid_size;      // Save calibrated grid size
    uint32_t grid_offset_x;  // Save calibrated grid offset
    uint32_t grid_offset_y;  // Save calibrated grid offset
    uint8_t show_grid;       // Save grid visibility state
    float camera_x;          // Save DM camera position
    float camera_y;
    float camera_zoom;
} SaveFileHeader;

// Asset library for maps
typedef struct {
    char filepaths[100][512];
    int count;
    int current_index;
} MapLibrary;

// Cached map data for fast loading
typedef struct {
    char filepath[256];
    unsigned char *pixel_data;  // Raw RGBA pixel data
    int width;
    int height;
    bool loaded;
} CachedMap;

typedef struct {
    CachedMap maps[100];
    int count;
    bool initialized;
} MapCache;

// Asset library for tokens
typedef struct {
    char filepaths[100][512];
    int count;
    int current_index;
} TokenLibrary;

// Grid calibration state
typedef struct {
    bool active;
    bool dragging;
    int start_x, start_y;
    int end_x, end_y;
    int cells_wide;          // Number of cells in rectangle (for multi-cell calibration)
    int cells_tall;          // Number of cells in rectangle (for multi-cell calibration)
    int calibrated_size;     // 0 = not calibrated, use default
    int calibrated_offset_x; // Grid offset from calibration
    int calibrated_offset_y; // Grid offset from calibration
} GridCalibration;

// ============================================================================
// CAMERA SYSTEM
// ============================================================================

typedef struct {
    float x, y;           // Current smoothed position
    float target_x, target_y;  // Target position (for smoothing)
    float zoom;
    float target_zoom;
    int view_width, view_height;
} Camera;

// ============================================================================
// GLOBAL STATE
// ============================================================================
WindowManager g_wm;
Map g_map;
TokenManager g_tokens;
FogOfWar g_fog;
InputState g_input;
MapLibrary g_map_library;
TokenLibrary g_token_library;
GridCalibration g_grid_calibration;
MapCache g_map_cache;
Camera g_dm_camera;
Camera g_player_camera;
bool g_sync_views = true;
DrawingManager g_drawings;

// Font rendering globals
stbtt_fontinfo g_font;
unsigned char *g_font_buffer = NULL;
bool g_font_loaded = false;

// UI texture cache (performance optimization)
typedef struct {
    SDL_Texture *texture;
    char cached_text[256];
    int cached_value;  // For tracking state changes
    float width;
    float height;
} UITextureCache;

UITextureCache g_damage_input_cache = {NULL, "", -1, 0, 0};
UITextureCache g_squad_mode_cache = {NULL, "", -1, 0, 0};

// Condition wheel text cache (one per condition, shared since wheel only in DM view)
UITextureCache g_condition_wheel_text_cache[CONDITION_COUNT];
bool g_condition_wheel_cache_initialized = false;

// ============================================================================
// FILE UTILITIES
// ============================================================================

bool is_image_file(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return false;
    return (strcasecmp(ext, ".png") == 0 ||
            strcasecmp(ext, ".jpg") == 0 ||
            strcasecmp(ext, ".jpeg") == 0 ||
            strcasecmp(ext, ".bmp") == 0 ||
            strcasecmp(ext, ".tga") == 0);
}

void extract_filename(const char *filepath, char *name, size_t name_size) {
    const char *basename = strrchr(filepath, '/');
    if (!basename) basename = strrchr(filepath, '\\');
    if (!basename) basename = filepath;
    else basename++;
    
    strncpy(name, basename, name_size - 1);
    name[name_size - 1] = '\0';
    char *ext = strrchr(name, '.');
    if (ext) *ext = '\0';
}

void normalize_path_slashes(char *path) {
    for (int i = 0; path[i] != '\0'; i++) {
        if (path[i] == '\\') {
            path[i] = '/';
        }
    }
}

void make_path_relative(char *path, size_t path_size) {
    (void)path_size;
    
    // Get current working directory
    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) {
        return;
    }
    
    // Normalize slashes in cwd
    for (int i = 0; cwd[i] != '\0'; i++) {
        if (cwd[i] == '\\') cwd[i] = '/';
    }
    
    // Normalize slashes in path
    for (int i = 0; path[i] != '\0'; i++) {
        if (path[i] == '\\') path[i] = '/';
    }
    
    size_t cwd_len = strlen(cwd);
    
    // Check if path starts with cwd
    if (strncasecmp(path, cwd, cwd_len) == 0) {
        // Skip the cwd and any following slash
        size_t offset = cwd_len;
        if (path[offset] == '/' || path[offset] == '\\') {
            offset++;
        }
        memmove(path, path + offset, strlen(path) - offset + 1);
    }
}

int compare_strings(const void *a, const void *b) {
    return strcmp((const char*)a, (const char*)b);
}

void scan_directory(const char *dir_path, char filepaths[][512], int *count, int max_count) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
    //         SDL_Log("Failed to open directory: %s", dir_path);
        *count = 0;
        return;
    }
    
    struct dirent *entry;
    *count = 0;
    
    while ((entry = readdir(dir)) != NULL && *count < max_count) {
        if (is_image_file(entry->d_name)) {
            snprintf(filepaths[*count], 512, "%s/%s", dir_path, entry->d_name);
            (*count)++;
        }
    }
    closedir(dir);
    
    // Sort alphabetically
    if (*count > 0) {
        qsort(filepaths, *count, 512, compare_strings);
    }
    
    //     SDL_Log("Found %d images in %s", *count, dir_path);
}

// ============================================================================
// TEXT RENDERING (Using TrueType font with caching)
// ============================================================================

bool font_load(const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (!f) {
    //         SDL_Log("Failed to open font file: %s", filepath);
        return false;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    g_font_buffer = (unsigned char*)malloc(size);
    if (!g_font_buffer) {
        fclose(f);
    //         SDL_Log("Failed to allocate font buffer");
        return false;
    }
    
    fread(g_font_buffer, 1, size, f);
    fclose(f);
    
    if (!stbtt_InitFont(&g_font, g_font_buffer, stbtt_GetFontOffsetForIndex(g_font_buffer, 0))) {
    //         SDL_Log("Failed to initialize font");
        free(g_font_buffer);
        g_font_buffer = NULL;
        return false;
    }
    
    g_font_loaded = true;
    //     SDL_Log("Font loaded successfully");
    return true;
}

void font_cleanup() {
    if (g_font_buffer) {
        free(g_font_buffer);
        g_font_buffer = NULL;
    }
    
    // Clean up UI texture caches
    if (g_damage_input_cache.texture) {
        SDL_DestroyTexture(g_damage_input_cache.texture);
        g_damage_input_cache.texture = NULL;
    }
    if (g_squad_mode_cache.texture) {
        SDL_DestroyTexture(g_squad_mode_cache.texture);
        g_squad_mode_cache.texture = NULL;
    }
    
    // Clean up condition wheel text cache
    for (int i = 0; i < CONDITION_COUNT; i++) {
        if (g_condition_wheel_text_cache[i].texture) {
            SDL_DestroyTexture(g_condition_wheel_text_cache[i].texture);
            g_condition_wheel_text_cache[i].texture = NULL;
        }
    }
    g_condition_wheel_cache_initialized = false;
    
    g_font_loaded = false;
}

// Helper: Create a damage number texture (cached for performance)
SDL_Texture* create_damage_texture(SDL_Renderer *renderer, int damage, float *out_width, float *out_height) {
    if (damage <= 0 || !g_font_loaded) return NULL;
    
    char text[16];
    snprintf(text, sizeof(text), "%d", damage);
    
    // Fixed font size for damage numbers (scale applied at render time)
    float font_size = 20.0f;
    float stb_scale = stbtt_ScaleForPixelHeight(&g_font, font_size);
    
    // Calculate text dimensions
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&g_font, &ascent, &descent, &line_gap);
    
    float text_width = 0;
    for (int i = 0; text[i]; i++) {
        int advance, left_bearing;
        stbtt_GetCodepointHMetrics(&g_font, text[i], &advance, &left_bearing);
        text_width += advance * stb_scale;
        if (text[i+1]) {
            text_width += stbtt_GetCodepointKernAdvance(&g_font, text[i], text[i+1]) * stb_scale;
        }
    }
    
    float text_height = (ascent - descent) * stb_scale;
    float padding = 4.0f;
    int texture_width = (int)(text_width + padding * 2);
    int texture_height = (int)(text_height + padding * 2);
    
    // Create a render target texture
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_TARGET,
                                             texture_width, texture_height);
    if (!texture) return NULL;
    
    // Render to texture
    SDL_SetRenderTarget(renderer, texture);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    
    // Clear with transparent
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    
    // Draw background
    SDL_FRect bg_rect = {0, 0, (float)texture_width, (float)texture_height};
    SDL_SetRenderDrawColor(renderer, 200, 0, 0, 230);
    SDL_RenderFillRect(renderer, &bg_rect);
    
    // Draw border
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderRect(renderer, &bg_rect);
    
    // Render text
    float cursor_x = padding;
    float baseline_y = padding + ascent * stb_scale;
    
    for (int i = 0; text[i]; i++) {
        int advance, left_bearing;
        stbtt_GetCodepointHMetrics(&g_font, text[i], &advance, &left_bearing);
        
        int glyph_width, glyph_height, xoff, yoff;
        unsigned char *bitmap = stbtt_GetCodepointBitmap(&g_font, 0, stb_scale,
                                                         text[i], &glyph_width, &glyph_height,
                                                         &xoff, &yoff);
        
        if (bitmap) {
            SDL_Surface *surface = SDL_CreateSurface(glyph_width, glyph_height, SDL_PIXELFORMAT_RGBA32);
            if (surface) {
                uint32_t *pixels = (uint32_t*)surface->pixels;
                for (int py = 0; py < glyph_height; py++) {
                    for (int px = 0; px < glyph_width; px++) {
                        unsigned char alpha = bitmap[py * glyph_width + px];
                        pixels[py * glyph_width + px] = (alpha << 24) | 0x00FFFFFF;
                    }
                }
                
                SDL_Texture *glyph_tex = SDL_CreateTextureFromSurface(renderer, surface);
                if (glyph_tex) {
                    SDL_SetTextureBlendMode(glyph_tex, SDL_BLENDMODE_BLEND);
                    SDL_FRect dst = {
                        cursor_x + left_bearing * stb_scale,
                        baseline_y + yoff,
                        (float)glyph_width,
                        (float)glyph_height
                    };
                    SDL_RenderTexture(renderer, glyph_tex, NULL, &dst);
                    SDL_DestroyTexture(glyph_tex);
                }
                SDL_DestroySurface(surface);
            }
            stbtt_FreeBitmap(bitmap, NULL);
        }
        
        cursor_x += advance * stb_scale;
        if (text[i+1]) {
            cursor_x += stbtt_GetCodepointKernAdvance(&g_font, text[i], text[i+1]) * stb_scale;
        }
    }
    
    // Reset render target
    SDL_SetRenderTarget(renderer, NULL);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    
    *out_width = texture_width;
    *out_height = texture_height;
    
    return texture;
}

// Render damage number above token (using cached texture)
void render_damage_number_cached(SDL_Renderer *renderer, Token *token, float x, float y, float scale, bool is_dm_view) {
    if (token->damage <= 0) return;
    
    // Get the appropriate texture pointer and cached damage value
    SDL_Texture **texture_ptr;
    int *cached_damage_ptr;
    
    if (is_dm_view) {
        texture_ptr = &token->damage_texture_dm;
        cached_damage_ptr = &token->cached_damage_dm;
    } else {
        texture_ptr = &token->damage_texture_player;
        cached_damage_ptr = &token->cached_damage_player;
    }
    
    // Regenerate texture if damage changed or texture doesn't exist
    if (*texture_ptr == NULL || *cached_damage_ptr != token->damage) {
        // Destroy old texture if it exists
        if (*texture_ptr) {
            SDL_DestroyTexture(*texture_ptr);
            *texture_ptr = NULL;
        }
        
        // Create new texture for this specific renderer
        *texture_ptr = create_damage_texture(renderer, token->damage, 
                                             &token->damage_tex_width, 
                                             &token->damage_tex_height);
        *cached_damage_ptr = token->damage;
    }
    
    // Render the cached texture
    if (*texture_ptr) {
        SDL_FRect dst = {
            x - (token->damage_tex_width * scale) / 2,
            y - (token->damage_tex_height * scale) - 4 * scale,
            token->damage_tex_width * scale,
            token->damage_tex_height * scale
        };
        SDL_RenderTexture(renderer, *texture_ptr, NULL, &dst);
    }
}

// ============================================================================
// ASSET LIBRARY
// ============================================================================

void map_library_init(MapLibrary *lib) {
    scan_directory("assets/maps", lib->filepaths, &lib->count, 100);
    lib->current_index = (lib->count > 0) ? 0 : -1;
}

void token_library_init(TokenLibrary *lib) {
    scan_directory("assets/tokens", lib->filepaths, &lib->count, 100);
    lib->current_index = (lib->count > 0) ? 0 : -1;
}

const char* map_library_get_current(MapLibrary *lib) {
    if (lib->current_index >= 0 && lib->current_index < lib->count) {
        return lib->filepaths[lib->current_index];
    }
    return NULL;
}

void map_library_next(MapLibrary *lib) {
    if (lib->count == 0) return;
    lib->current_index = (lib->current_index + 1) % lib->count;
}

void map_library_prev(MapLibrary *lib) {
    if (lib->count == 0) return;
    lib->current_index = (lib->current_index - 1 + lib->count) % lib->count;
}

// ============================================================================
// MAP CACHE
// ============================================================================

void map_cache_init(MapCache *cache, MapLibrary *lib) {
    cache->count = 0;
    cache->initialized = false;
    
    for (int i = 0; i < lib->count && i < 100; i++) {
        strncpy(cache->maps[i].filepath, lib->filepaths[i], sizeof(cache->maps[i].filepath) - 1);
        cache->maps[i].pixel_data = NULL;
        cache->maps[i].loaded = false;
        cache->count++;
    }
    
    //     SDL_Log("Map cache initialized with %d maps", cache->count);
}

void map_cache_preload(MapCache *cache, MapLibrary *lib) {
    if (cache->initialized) return;
    
    for (int i = 0; i < lib->count && i < 100; i++) {
        const char *filepath = lib->filepaths[i];
        
        int width, height, channels;
        unsigned char *data = stbi_load(filepath, &width, &height, &channels, 4);
        
        if (data) {
            cache->maps[i].pixel_data = data;
            cache->maps[i].width = width;
            cache->maps[i].height = height;
            cache->maps[i].loaded = true;
    //             SDL_Log("Preloaded map: %s (%dx%d)", filepath, width, height);
        } else {
    //             SDL_Log("Failed to preload map: %s", filepath);
        }
    }
    
    cache->initialized = true;
}

unsigned char* map_cache_get_data(MapCache *cache, const char *filepath, int *width, int *height) {
    for (int i = 0; i < cache->count; i++) {
        if (strcmp(cache->maps[i].filepath, filepath) == 0 && cache->maps[i].loaded) {
            *width = cache->maps[i].width;
            *height = cache->maps[i].height;
            return cache->maps[i].pixel_data;
        }
    }
    return NULL;
}

void map_cache_destroy(MapCache *cache) {
    for (int i = 0; i < cache->count; i++) {
        if (cache->maps[i].pixel_data) {
            stbi_image_free(cache->maps[i].pixel_data);
            cache->maps[i].pixel_data = NULL;
        }
        cache->maps[i].loaded = false;
    }
    cache->count = 0;
    cache->initialized = false;
}

// Forward declarations for fog functions (used in grid calibration)
bool fog_init(FogOfWar *fog, int cols, int rows);
void fog_destroy(FogOfWar *fog);

// ============================================================================
// GRID CALIBRATION
// ============================================================================

void grid_calibration_init(GridCalibration *cal) {
    cal->active = false;
    cal->dragging = false;
    cal->calibrated_size = 0;
    cal->calibrated_offset_x = 0;
    cal->calibrated_offset_y = 0;
    cal->start_x = 0;
    cal->start_y = 0;
    cal->end_x = 0;
    cal->end_y = 0;
    cal->cells_wide = 2;
    cal->cells_tall = 2;
}

void grid_calibration_start(GridCalibration *cal) {
    cal->active = true;
    cal->dragging = false;
    cal->cells_wide = 2;
    cal->cells_tall = 2;
    //     SDL_Log("Grid calibration mode: Draw rectangle around map grid cells (default 2x2)");
    //     SDL_Log("Use arrow keys to adjust cell count while dragging, Enter to apply, Escape to cancel");
}

void grid_calibration_apply(GridCalibration *cal, Map *map, FogOfWar *fog) {
    int width = abs(cal->end_x - cal->start_x);
    int height = abs(cal->end_y - cal->start_y);
    
    if (width < 10 || height < 10) {
    //         SDL_Log("Rectangle too small, calibration cancelled");
        cal->active = false;
        return;
    }
    
    // Calculate cell size by dividing rectangle by cell count
    int grid_size_x = width / cal->cells_wide;
    int grid_size_y = height / cal->cells_tall;
    cal->calibrated_size = (grid_size_x + grid_size_y) / 2;
    
    // Get the top-left corner of the calibration rectangle
    int rect_left = (cal->start_x < cal->end_x) ? cal->start_x : cal->end_x;
    int rect_top = (cal->start_y < cal->end_y) ? cal->start_y : cal->end_y;
    
    // Calculate where the grid origin would be by extending the pattern backward
    // This allows the grid to extend infinitely across the whole map
    cal->calibrated_offset_x = rect_left % cal->calibrated_size;
    cal->calibrated_offset_y = rect_top % cal->calibrated_size;
    
    //     SDL_Log("Grid calibrated: size=%d pixels, offset=(%d, %d), cells=%dx%d",
    //         cal->calibrated_size, cal->calibrated_offset_x, cal->calibrated_offset_y,
    //         cal->cells_wide, cal->cells_tall);
    
    // Update map grid dimensions and offset
    map->grid_size = cal->calibrated_size;
    map->grid_offset_x = cal->calibrated_offset_x;
    map->grid_offset_y = cal->calibrated_offset_y;
    // Calculate grid dimensions to cover the entire map (round up)
    map->grid_cols = (map->width + map->grid_size - 1) / map->grid_size;
    map->grid_rows = (map->height + map->grid_size - 1) / map->grid_size;
    
    // Reinitialize fog for new grid size
    fog_destroy(fog);
    fog_init(fog, map->grid_cols, map->grid_rows);
    
    cal->active = false;
}

void grid_calibration_render(SDL_Renderer *renderer, GridCalibration *cal,
                             float camera_x, float camera_y, float zoom) {
    if (!cal->active || !cal->dragging) return;
    
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    
    // Draw current rectangle with zoom transformation
    SDL_FRect rect;
    rect.x = (fmin(cal->start_x, cal->end_x) - camera_x) * zoom;
    rect.y = (fmin(cal->start_y, cal->end_y) - camera_y) * zoom;
    rect.w = abs(cal->end_x - cal->start_x) * zoom;
    rect.h = abs(cal->end_y - cal->start_y) * zoom;
    
    // Blue semi-transparent fill
    SDL_SetRenderDrawColor(renderer, 0, 100, 255, 80);
    SDL_RenderFillRect(renderer, &rect);
    
    // Draw grid lines within rectangle to show cell divisions
    float cell_width = rect.w / cal->cells_wide;
    float cell_height = rect.h / cal->cells_tall;
    
    SDL_SetRenderDrawColor(renderer, 0, 150, 255, 255);
    
    // Vertical lines
    for (int i = 1; i < cal->cells_wide; i++) {
        float x = rect.x + i * cell_width;
        SDL_RenderLine(renderer, x, rect.y, x, rect.y + rect.h);
    }
    
    // Horizontal lines
    for (int i = 1; i < cal->cells_tall; i++) {
        float y = rect.y + i * cell_height;
        SDL_RenderLine(renderer, rect.x, y, rect.x + rect.w, y);
    }
    
    // Draw border (thicker for visibility)
    SDL_RenderRect(renderer, &rect);
}

// ============================================================================
// CAMERA SYSTEM
// ============================================================================

void camera_init(Camera *cam, int map_width, int map_height, int win_width, int win_height) {
    (void)map_width;
    (void)map_height;
    cam->x = cam->target_x = 0;
    cam->y = cam->target_y = 0;
    cam->zoom = cam->target_zoom = 1.0f;
    cam->view_width = win_width;
    cam->view_height = win_height;
}

void camera_clamp(Camera *cam, int map_width, int map_height) {
    (void)cam;
    (void)map_width;
    (void)map_height;
    // No clamping - dark edges shown when zoomed out
}

void camera_update(Camera *cam) {
    cam->x += (cam->target_x - cam->x) * CAMERA_SMOOTHING;
    cam->y += (cam->target_y - cam->y) * CAMERA_SMOOTHING;
    cam->zoom += (cam->target_zoom - cam->zoom) * CAMERA_SMOOTHING;
}

void camera_zoom_toward(Camera *cam, int mouse_x, int mouse_y, float factor, int map_width, int map_height) {
    float new_zoom = cam->target_zoom * factor;
    new_zoom = fminf(fmaxf(new_zoom, ZOOM_MIN), ZOOM_MAX);
    
    // Calculate world position of mouse using target values
    float world_mouse_x = (mouse_x / cam->target_zoom) + cam->target_x;
    float world_mouse_y = (mouse_y / cam->target_zoom) + cam->target_y;
    
    cam->target_zoom = new_zoom;
    
    // Set new camera position so mouse stays at same world position
    cam->target_x = world_mouse_x - (mouse_x / new_zoom);
    cam->target_y = world_mouse_y - (mouse_y / new_zoom);
    
    // No clamping - dark edges shown when zoomed out
    (void)map_width;
    (void)map_height;
}

void camera_pan(Camera *cam, int delta_x, int delta_y) {
    cam->target_x -= delta_x / cam->zoom;
    cam->target_y -= delta_y / cam->zoom;
}

void camera_set_position(Camera *cam, float x, float y, int map_width, int map_height) {
    (void)map_width;
    (void)map_height;
    cam->target_x = x;
    cam->target_y = y;
}

void camera_sync(Camera *dst, Camera *src, int map_width, int map_height) {
    (void)map_width;
    (void)map_height;
    dst->target_x = src->target_x;
    dst->target_y = src->target_y;
    dst->target_zoom = src->target_zoom;
    dst->x = src->x;
    dst->y = src->y;
    dst->zoom = src->zoom;
}

// ============================================================================
// WINDOW MANAGER
// ============================================================================

bool window_manager_init(WindowManager *wm) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
    //         SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    
    int display_count = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&display_count);
    
    // Create DM window
    SDL_PropertiesID dm_props = SDL_CreateProperties();
    SDL_SetStringProperty(dm_props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "VTT - DM View");
    SDL_SetNumberProperty(dm_props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, WINDOW_DM_WIDTH);
    SDL_SetNumberProperty(dm_props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, WINDOW_DM_HEIGHT);
    SDL_SetNumberProperty(dm_props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED);
    SDL_SetNumberProperty(dm_props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED);
    SDL_SetNumberProperty(dm_props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, SDL_WINDOW_RESIZABLE);
    
    wm->dm_window.window = SDL_CreateWindowWithProperties(dm_props);
    SDL_DestroyProperties(dm_props);
    
    if (!wm->dm_window.window) {
    //         SDL_Log("Failed to create DM window: %s", SDL_GetError());
        return false;
    }
    
    wm->dm_window.renderer = SDL_CreateRenderer(wm->dm_window.window, NULL);
    wm->dm_window.id = SDL_GetWindowID(wm->dm_window.window);
    wm->dm_window.type = WINDOW_TYPE_DM;
    wm->dm_window.is_open = true;
    
    // Note: Drag-and-drop events are handled by SDL3 by default via SDL_EVENT_DROP_FILE
    
    // Create Player window (centered on second monitor)
    SDL_PropertiesID player_props = SDL_CreateProperties();
    SDL_SetStringProperty(player_props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "VTT - Player View");
    SDL_SetNumberProperty(player_props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, WINDOW_PLAYER_WIDTH);
    SDL_SetNumberProperty(player_props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, WINDOW_PLAYER_HEIGHT);
    SDL_SetNumberProperty(player_props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, SDL_WINDOW_RESIZABLE);
    
    if (display_count > 1) {
        SDL_Rect bounds;
        SDL_GetDisplayBounds(displays[1], &bounds);
        int x = bounds.x + (bounds.w - WINDOW_PLAYER_WIDTH) / 2;
        int y = bounds.y + (bounds.h - WINDOW_PLAYER_HEIGHT) / 2;
        SDL_SetNumberProperty(player_props, SDL_PROP_WINDOW_CREATE_X_NUMBER, x);
        SDL_SetNumberProperty(player_props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, y);
    } else {
        SDL_SetNumberProperty(player_props, SDL_PROP_WINDOW_CREATE_X_NUMBER, 100);
        SDL_SetNumberProperty(player_props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, 100);
    }
    
    wm->player_window.window = SDL_CreateWindowWithProperties(player_props);
    SDL_DestroyProperties(player_props);
    
    if (!wm->player_window.window) {
    //         SDL_Log("Failed to create Player window: %s", SDL_GetError());
        return false;
    }
    
    wm->player_window.renderer = SDL_CreateRenderer(wm->player_window.window, NULL);
    wm->player_window.id = SDL_GetWindowID(wm->player_window.window);
    wm->player_window.type = WINDOW_TYPE_PLAYER;
    wm->player_window.is_open = true;
    
    SDL_free(displays);
    return true;
}

void window_manager_destroy(WindowManager *wm) {
    if (wm->dm_window.renderer) SDL_DestroyRenderer(wm->dm_window.renderer);
    if (wm->dm_window.window) SDL_DestroyWindow(wm->dm_window.window);
    if (wm->player_window.renderer) SDL_DestroyRenderer(wm->player_window.renderer);
    if (wm->player_window.window) SDL_DestroyWindow(wm->player_window.window);
    SDL_Quit();
}

WindowState* window_manager_get_window_by_id(WindowManager *wm, SDL_WindowID id) {
    if (wm->dm_window.id == id) return &wm->dm_window;
    if (wm->player_window.id == id) return &wm->player_window;
    return NULL;
}

// ============================================================================
// MAP SYSTEM
// ============================================================================

void map_init(Map *map) {
    map->dm_texture = NULL;
    map->player_texture = NULL;
    map->width = 0;
    map->height = 0;
    map->grid_cols = 0;
    map->grid_rows = 0;
    map->grid_size = DEFAULT_GRID_SIZE;
    map->filepath[0] = '\0';
    map->name[0] = '\0';
}

bool map_load(Map *map, const char *filepath, SDL_Renderer *dm_renderer, SDL_Renderer *player_renderer) {
    int width, height;
    unsigned char *data = map_cache_get_data(&g_map_cache, filepath, &width, &height);
    bool data_from_cache = (data != NULL);
    
    if (!data) {
        data = stbi_load(filepath, &width, &height, NULL, 4);
    }
    
    if (!data) {
    //         SDL_Log("Failed to load map: %s", stbi_failure_reason());
        return false;
    }
    
    SDL_Surface *surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32,
                                                 data, width * 4);
    if (!surface) {
    //         SDL_Log("Failed to create surface: %s", SDL_GetError());
        if (!data_from_cache) stbi_image_free(data);
        return false;
    }
    
    map->width = width;
    map->height = height;
    
    map->grid_size = (g_grid_calibration.calibrated_size > 0)
        ? g_grid_calibration.calibrated_size
        : DEFAULT_GRID_SIZE;
    
    map->grid_offset_x = (g_grid_calibration.calibrated_size > 0)
        ? g_grid_calibration.calibrated_offset_x
        : 0;
    map->grid_offset_y = (g_grid_calibration.calibrated_size > 0)
        ? g_grid_calibration.calibrated_offset_y
        : 0;
    
    map->grid_cols = (map->width + map->grid_size - 1) / map->grid_size;
    map->grid_rows = (map->height + map->grid_size - 1) / map->grid_size;
    
    strncpy(map->filepath, filepath, sizeof(map->filepath) - 1);
    extract_filename(filepath, map->name, sizeof(map->name));
    
    map->dm_texture = SDL_CreateTextureFromSurface(dm_renderer, surface);
    map->player_texture = SDL_CreateTextureFromSurface(player_renderer, surface);
    
    SDL_DestroySurface(surface);
    
    if (!data_from_cache) {
        stbi_image_free(data);
    }
    
    if (!map->dm_texture || !map->player_texture) {
    //         SDL_Log("Failed to create textures: %s", SDL_GetError());
        return false;
    }
    
    //     SDL_Log("Map loaded: %s (%dx%d, grid %dx%d)%s", filepath, map->width, map->height,
    //         map->grid_cols, map->grid_rows, data_from_cache ? " (from cache)" : "");
    
    return true;
}

void map_destroy(Map *map) {
    if (map->dm_texture) SDL_DestroyTexture(map->dm_texture);
    if (map->player_texture) SDL_DestroyTexture(map->player_texture);
    map->dm_texture = NULL;
    map->player_texture = NULL;
}

// ============================================================================
// GRID SYSTEM
// ============================================================================

void grid_world_to_grid(int world_x, int world_y,
                        int grid_size, int grid_offset_x, int grid_offset_y,
                        int *grid_x, int *grid_y) {
    *grid_x = (world_x - grid_offset_x) / grid_size;
    *grid_y = (world_y - grid_offset_y) / grid_size;
}

void grid_grid_to_world(int grid_x, int grid_y,
                        int grid_size, int grid_offset_x, int grid_offset_y,
                        int *world_x, int *world_y) {
    *world_x = grid_x * grid_size + grid_offset_x;
    *world_y = grid_y * grid_size + grid_offset_y;
}

SDL_FRect grid_get_cell_world_rect(int grid_x, int grid_y, int camera_x, int camera_y,
                                   int grid_size, int grid_offset_x, int grid_offset_y) {
    SDL_FRect rect;
    rect.x = grid_x * grid_size + grid_offset_x - camera_x;
    rect.y = grid_y * grid_size + grid_offset_y - camera_y;
    rect.w = grid_size;
    rect.h = grid_size;
    return rect;
}

void grid_screen_to_grid_direct(float screen_x, float screen_y,
                                float camera_x, float camera_y, float zoom,
                                int grid_size, int grid_offset_x, int grid_offset_y,
                                int *grid_x, int *grid_y) {
    // Screen → World → Grid in one call
    int world_x = (int)(screen_x / zoom + camera_x);
    int world_y = (int)(screen_y / zoom + camera_y);
    *grid_x = (world_x - grid_offset_x) / grid_size;
    *grid_y = (world_y - grid_offset_y) / grid_size;
}

void grid_render(SDL_Renderer *renderer, const Map *map, int camera_x, int camera_y,
                 int window_width, int window_height, float zoom) {
    if (!map->dm_texture) return;
    
    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 128);
    
    int grid_size = map->grid_size;
    int grid_offset_x = map->grid_offset_x;
    int grid_offset_y = map->grid_offset_y;
    
    // Calculate visible grid range accounting for zoom
    int start_col = (camera_x - grid_offset_x) / grid_size;
    int end_col = ((camera_x + window_width / zoom) - grid_offset_x) / grid_size + 1;
    int start_row = (camera_y - grid_offset_y) / grid_size;
    int end_row = ((camera_y + window_height / zoom) - grid_offset_y) / grid_size + 1;
    
    if (start_col < 0) start_col = 0;
    if (start_row < 0) start_row = 0;
    if (end_col > map->grid_cols) end_col = map->grid_cols;
    if (end_row > map->grid_rows) end_row = map->grid_rows;
    
    // Vertical lines (apply zoom transformation to stay with map)
    for (int x = start_col; x <= end_col; x++) {
        float screen_x = (x * grid_size + grid_offset_x - camera_x) * zoom;
        SDL_RenderLine(renderer, screen_x, 0, screen_x, window_height);
    }
    
    // Horizontal lines (apply zoom transformation to stay with map)
    for (int y = start_row; y <= end_row; y++) {
        float screen_y = (y * grid_size + grid_offset_y - camera_y) * zoom;
        SDL_RenderLine(renderer, 0, screen_y, window_width, screen_y);
    }
}

// ============================================================================
// FOG OF WAR
// ============================================================================

bool fog_init(FogOfWar *fog, int cols, int rows) {
    fog->cols = cols;
    fog->rows = rows;
    fog->cells = malloc(rows * sizeof(bool*));
    if (!fog->cells) return false;
    
    for (int i = 0; i < rows; i++) {
        fog->cells[i] = malloc(cols * sizeof(bool));
        if (!fog->cells[i]) {
            for (int j = 0; j < i; j++) {
                free(fog->cells[j]);
            }
            free(fog->cells);
            return false;
        }
        // Initialize all cells to visible (true) by default
        for (int j = 0; j < cols; j++) {
            fog->cells[i][j] = true;
        }
    }
    
    return true;
}

void fog_destroy(FogOfWar *fog) {
    if (fog->cells) {
        for (int i = 0; i < fog->rows; i++) {
            free(fog->cells[i]);
        }
        free(fog->cells);
        fog->cells = NULL;
    }
}

bool fog_is_visible(const FogOfWar *fog, int grid_x, int grid_y) {
    if (grid_x < 0 || grid_x >= fog->cols || grid_y < 0 || grid_y >= fog->rows) {
        return false;
    }
    return fog->cells[grid_y][grid_x];
}

void fog_toggle_cell(FogOfWar *fog, int grid_x, int grid_y) {
    if (grid_x >= 0 && grid_x < fog->cols && grid_y >= 0 && grid_y < fog->rows) {
        fog->cells[grid_y][grid_x] = !fog->cells[grid_y][grid_x];
    }
}

void fog_set_cell(FogOfWar *fog, int grid_x, int grid_y, bool visible) {
    if (grid_x >= 0 && grid_x < fog->cols && grid_y >= 0 && grid_y < fog->rows) {
        fog->cells[grid_y][grid_x] = visible;
    }
}

void fog_reveal_all(FogOfWar *fog) {
    for (int y = 0; y < fog->rows; y++) {
        for (int x = 0; x < fog->cols; x++) {
            fog->cells[y][x] = true;
        }
    }
}

void fog_hide_all(FogOfWar *fog) {
    for (int y = 0; y < fog->rows; y++) {
        for (int x = 0; x < fog->cols; x++) {
            fog->cells[y][x] = false;
        }
    }
}

void fog_render(SDL_Renderer *renderer, const FogOfWar *fog, float camera_x, float camera_y,
                int window_width, int window_height, int grid_size,
                int grid_offset_x, int grid_offset_y,
                float zoom, bool is_dm_view) {
    if (!fog->cells) return;
    
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, is_dm_view ? 180 : 255);
    
    // Calculate visible grid range accounting for zoom and grid offset
    int start_col = ((int)camera_x - grid_offset_x) / grid_size;
    int end_col = ((int)(camera_x + window_width / zoom) - grid_offset_x) / grid_size + 1;
    int start_row = ((int)camera_y - grid_offset_y) / grid_size;
    int end_row = ((int)(camera_y + window_height / zoom) - grid_offset_y) / grid_size + 1;
    
    if (end_col > fog->cols) end_col = fog->cols;
    if (end_row > fog->rows) end_row = fog->rows;
    if (start_col < 0) start_col = 0;
    if (start_row < 0) start_row = 0;
    
    for (int y = start_row; y < end_row; y++) {
        for (int x = start_col; x < end_col; x++) {
            if (!fog->cells[y][x]) {
                SDL_FRect rect = grid_get_cell_world_rect(x, y, (int)camera_x, (int)camera_y, grid_size, grid_offset_x, grid_offset_y);
                rect.x *= zoom;
                rect.y *= zoom;
                rect.w *= zoom;
                rect.h *= zoom;
                SDL_RenderFillRect(renderer, &rect);
            }
        }
    }
}

// ============================================================================
// DRAWING SYSTEM
// ============================================================================

void drawing_manager_init(DrawingManager *mgr) {
    mgr->count = 0;
}

void drawing_add(DrawingManager *mgr, ShapeType type, int x1, int y1, int x2, int y2, int color_index) {
    if (mgr->count >= MAX_DRAWINGS) {
    //         SDL_Log("Max drawings reached");
        return;
    }
    
    Drawing *d = &mgr->drawings[mgr->count];
    d->type = type;
    d->x1 = x1;
    d->y1 = y1;
    d->x2 = x2;
    d->y2 = y2;
    d->color_index = color_index;
    d->filled = true;
    d->cached_texture_dm = NULL;
    d->cached_texture_player = NULL;
    d->cached_width = 0;
    d->cached_height = 0;
    
    mgr->count++;
    //     SDL_Log("Drawing added: type=%d, color=%s", type, SQUAD_COLORS[color_index].name);
}

// Check if point is inside a drawing (for right-click deletion)
bool drawing_contains_point(const Drawing *d, int world_x, int world_y) {
    if (d->type == SHAPE_RECTANGLE) {
        int min_x = (d->x1 < d->x2) ? d->x1 : d->x2;
        int max_x = (d->x1 > d->x2) ? d->x1 : d->x2;
        int min_y = (d->y1 < d->y2) ? d->y1 : d->y2;
        int max_y = (d->y1 > d->y2) ? d->y1 : d->y2;
        
        return world_x >= min_x && world_x <= max_x && world_y >= min_y && world_y <= max_y;
    } else if (d->type == SHAPE_CIRCLE) {
        int cx = (d->x1 + d->x2) / 2;
        int cy = (d->y1 + d->y2) / 2;
        int dx = d->x2 - d->x1;
        int dy = d->y2 - d->y1;
        int radius_sq = (dx * dx + dy * dy) / 4;
        
        int dist_x = world_x - cx;
        int dist_y = world_y - cy;
        int dist_sq = dist_x * dist_x + dist_y * dist_y;
        
        return dist_sq <= radius_sq;
    }
    return false;
}

void drawing_remove(DrawingManager *mgr, int index) {
    if (index < 0 || index >= mgr->count) return;
    
    // Free cached textures
    if (mgr->drawings[index].cached_texture_dm) {
        SDL_DestroyTexture(mgr->drawings[index].cached_texture_dm);
    }
    if (mgr->drawings[index].cached_texture_player) {
        SDL_DestroyTexture(mgr->drawings[index].cached_texture_player);
    }
    
    // Shift remaining drawings
    for (int i = index; i < mgr->count - 1; i++) {
        mgr->drawings[i] = mgr->drawings[i + 1];
    }
    
    mgr->count--;
    //     SDL_Log("Drawing removed");
}

// Optimized circle rendering using scanlines
void render_filled_circle(SDL_Renderer *renderer, float cx, float cy, float radius) {
    if (radius < 1) return;
    
    // Use scanline algorithm - much faster than pixel-by-pixel
    int r = (int)radius;
    for (int y = -r; y <= r; y++) {
        int half_width = (int)sqrtf(radius * radius - y * y);
        if (half_width > 0) {
            SDL_FRect line = {
                cx - half_width,
                cy + y,
                half_width * 2,
                1
            };
            SDL_RenderFillRect(renderer, &line);
        }
    }
}

void drawing_render_all(SDL_Renderer *renderer, const DrawingManager *mgr,
                        int camera_x, int camera_y, float zoom) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    
    for (int i = 0; i < mgr->count; i++) {
        const Drawing *d = &mgr->drawings[i];
        const SquadColor *color = &SQUAD_COLORS[d->color_index];
        
        // Semi-transparent (alpha = 128)
        SDL_SetRenderDrawColor(renderer, color->r, color->g, color->b, 128);
        
        if (d->type == SHAPE_RECTANGLE) {
            SDL_FRect rect = {
                (d->x1 - camera_x) * zoom,
                (d->y1 - camera_y) * zoom,
                (d->x2 - d->x1) * zoom,
                (d->y2 - d->y1) * zoom
            };
            
            // Normalize rect (in case dragged backwards)
            if (rect.w < 0) {
                rect.x += rect.w;
                rect.w = -rect.w;
            }
            if (rect.h < 0) {
                rect.y += rect.h;
                rect.h = -rect.h;
            }
            
            SDL_RenderFillRect(renderer, &rect);
            
            // Draw border
            SDL_SetRenderDrawColor(renderer, color->r, color->g, color->b, 255);
            SDL_RenderRect(renderer, &rect);
            
        } else if (d->type == SHAPE_CIRCLE) {
            // Draw circle with optimized scanline algorithm
            float cx = ((d->x1 + d->x2) / 2.0f - camera_x) * zoom;
            float cy = ((d->y1 + d->y2) / 2.0f - camera_y) * zoom;
            float radius = sqrtf(powf((d->x2 - d->x1) * zoom, 2) + powf((d->y2 - d->y1) * zoom, 2)) / 2.0f;
            
            render_filled_circle(renderer, cx, cy, radius);
            
            // Draw border using Bresenham's circle algorithm (outline only)
            SDL_SetRenderDrawColor(renderer, color->r, color->g, color->b, 255);
            int r = (int)radius;
            int x = 0;
            int y = r;
            int d = 3 - 2 * r;
            
            while (y >= x) {
                // Draw 8 octants
                SDL_RenderPoint(renderer, cx + x, cy + y);
                SDL_RenderPoint(renderer, cx - x, cy + y);
                SDL_RenderPoint(renderer, cx + x, cy - y);
                SDL_RenderPoint(renderer, cx - x, cy - y);
                SDL_RenderPoint(renderer, cx + y, cy + x);
                SDL_RenderPoint(renderer, cx - y, cy + x);
                SDL_RenderPoint(renderer, cx + y, cy - x);
                SDL_RenderPoint(renderer, cx - y, cy - x);
                
                x++;
                if (d > 0) {
                    y--;
                    d = d + 4 * (x - y) + 10;
                } else {
                    d = d + 4 * x + 6;
                }
            }
        }
    }
}

void drawing_clear_all(DrawingManager *mgr) {
    // Free all cached textures
    for (int i = 0; i < mgr->count; i++) {
        if (mgr->drawings[i].cached_texture_dm) {
            SDL_DestroyTexture(mgr->drawings[i].cached_texture_dm);
        }
        if (mgr->drawings[i].cached_texture_player) {
            SDL_DestroyTexture(mgr->drawings[i].cached_texture_player);
        }
    }
    mgr->count = 0;
    //     SDL_Log("All drawings cleared");
}

// ============================================================================
// TOKEN SYSTEM
// ============================================================================

// Forward declarations
void render_token_conditions(SDL_Renderer *renderer, const Token *token, float x, float y, float scale);

void token_manager_init(TokenManager *mgr) {
    mgr->count = 0;
}

void token_manager_destroy(TokenManager *mgr) {
    for (int i = 0; i < mgr->count; i++) {
        if (mgr->tokens[i].dm_texture) SDL_DestroyTexture(mgr->tokens[i].dm_texture);
        if (mgr->tokens[i].player_texture) SDL_DestroyTexture(mgr->tokens[i].player_texture);
        if (mgr->tokens[i].damage_texture_dm) SDL_DestroyTexture(mgr->tokens[i].damage_texture_dm);
        if (mgr->tokens[i].damage_texture_player) SDL_DestroyTexture(mgr->tokens[i].damage_texture_player);
    }
    mgr->count = 0;
}

int token_add(TokenManager *mgr, const char *filepath, int grid_x, int grid_y,
              SDL_Renderer *dm_renderer, SDL_Renderer *player_renderer, const char *name) {
    if (mgr->count >= MAX_TOKENS) return -1;
    
    int width, height, channels;
    unsigned char *data = stbi_load(filepath, &width, &height, &channels, 4); // Force RGBA
    
    if (!data) {
    //         SDL_Log("Failed to load token: %s", stbi_failure_reason());
        return -1;
    }
    
    SDL_Surface *surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32,
                                                 data, width * 4);
    if (!surface) {
    //         SDL_Log("Failed to create surface: %s", SDL_GetError());
        stbi_image_free(data);
        return -1;
    }
    
    Token *token = &mgr->tokens[mgr->count];
    token->dm_texture = SDL_CreateTextureFromSurface(dm_renderer, surface);
    token->player_texture = SDL_CreateTextureFromSurface(player_renderer, surface);
    token->width = surface->w;
    token->height = surface->h;
    token->grid_x = grid_x;
    token->grid_y = grid_y;
    token->grid_size = 1;  // Default to 1x1 grid cell
    token->selected = false;
    token->hidden = false;
    token->damage = 0;  // Initialize damage to 0
    token->squad = -1;  // Initialize to no squad
    token->opacity = 255;  // Fully opaque by default
    for (int i = 0; i < CONDITION_COUNT; i++) {
        token->conditions[i] = false;
    }
    token->damage_texture_dm = NULL;
    token->damage_texture_player = NULL;
    token->cached_damage_dm = -1;
    token->cached_damage_player = -1;
    token->damage_tex_width = 0;
    token->damage_tex_height = 0;
    strncpy(token->name, name ? name : "token", sizeof(token->name) - 1);
    strncpy(token->filepath, filepath, sizeof(token->filepath) - 1);
    
    SDL_DestroySurface(surface);
    stbi_image_free(data);
    
    if (!token->dm_texture || !token->player_texture) {
    //         SDL_Log("Failed to create token textures: %s", SDL_GetError());
        return -1;
    }
    
    mgr->count++;
    //     SDL_Log("Token added: %s at (%d, %d)", filepath, grid_x, grid_y);
    return mgr->count - 1;
}

Token* token_get_at_grid(TokenManager *mgr, int grid_x, int grid_y) {
    for (int i = mgr->count - 1; i >= 0; i--) {
        if (mgr->tokens[i].grid_x == grid_x && mgr->tokens[i].grid_y == grid_y) {
            return &mgr->tokens[i];
        }
    }
    return NULL;
}

void token_deselect_all(TokenManager *mgr) {
    for (int i = 0; i < mgr->count; i++) {
        mgr->tokens[i].selected = false;
    }
}

void token_remove(TokenManager *mgr, int index) {
    if (index < 0 || index >= mgr->count) return;
    
    // Destroy textures for the token being removed
    if (mgr->tokens[index].dm_texture) {
        SDL_DestroyTexture(mgr->tokens[index].dm_texture);
    }
    if (mgr->tokens[index].player_texture) {
        SDL_DestroyTexture(mgr->tokens[index].player_texture);
    }
    if (mgr->tokens[index].damage_texture_dm) {
        SDL_DestroyTexture(mgr->tokens[index].damage_texture_dm);
    }
    if (mgr->tokens[index].damage_texture_player) {
        SDL_DestroyTexture(mgr->tokens[index].damage_texture_player);
    }
    
    // Shift remaining tokens down
    for (int i = index; i < mgr->count - 1; i++) {
        mgr->tokens[i] = mgr->tokens[i + 1];
    }
    
    mgr->count--;
    //     SDL_Log("Token removed at index %d", index);
}

void token_resize(TokenManager *mgr, int index, int delta) {
    if (index < 0 || index >= mgr->count) return;
    Token *t = &mgr->tokens[index];
    int new_size = t->grid_size + delta;
    if (new_size >= 1 && new_size <= 4) {
        t->grid_size = new_size;
    //         SDL_Log("Token resized to %dx%d", new_size, new_size);
    }
}

int token_copy(TokenManager *mgr, int index, int new_grid_x, int new_grid_y,
               SDL_Renderer *dm_renderer, SDL_Renderer *player_renderer) {
    if (index < 0 || index >= mgr->count) return -1;
    if (mgr->count >= MAX_TOKENS) return -1;
    
    Token *src = &mgr->tokens[index];
    
    int width, height, channels;
    unsigned char *data = stbi_load(src->filepath, &width, &height, &channels, 4);
    if (!data) {
    //         SDL_Log("Failed to reload token for copy: %s", stbi_failure_reason());
        return -1;
    }
    
    SDL_Surface *surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32,
                                                 data, width * 4);
    if (!surface) {
    //         SDL_Log("Failed to create surface for token copy: %s", SDL_GetError());
        stbi_image_free(data);
        return -1;
    }
    
    Token *dst = &mgr->tokens[mgr->count];
    dst->dm_texture = SDL_CreateTextureFromSurface(dm_renderer, surface);
    dst->player_texture = SDL_CreateTextureFromSurface(player_renderer, surface);
    dst->width = surface->w;
    dst->height = surface->h;
    dst->grid_x = new_grid_x;
    dst->grid_y = new_grid_y;
    dst->grid_size = src->grid_size;
    dst->selected = false;
    dst->hidden = src->hidden;
    dst->damage = src->damage;
    dst->squad = src->squad;
    dst->opacity = src->opacity;
    for (int i = 0; i < CONDITION_COUNT; i++) {
        dst->conditions[i] = src->conditions[i];
    }
    dst->damage_texture_dm = NULL;
    dst->damage_texture_player = NULL;
    dst->cached_damage_dm = -1;
    dst->cached_damage_player = -1;
    dst->damage_tex_width = 0;
    dst->damage_tex_height = 0;
    strncpy(dst->name, src->name, sizeof(dst->name) - 1);
    strncpy(dst->filepath, src->filepath, sizeof(dst->filepath) - 1);
    
    SDL_DestroySurface(surface);
    stbi_image_free(data);
    
    if (!dst->dm_texture || !dst->player_texture) {
    //         SDL_Log("Failed to create textures for token copy: %s", SDL_GetError());
        return -1;
    }
    
    mgr->count++;
    //     SDL_Log("Token copied: %s to (%d, %d)", src->name, new_grid_x, new_grid_y);
    return mgr->count - 1;
}

void token_toggle_hidden(TokenManager *mgr, int index) {
    if (index < 0 || index >= mgr->count) return;
    mgr->tokens[index].hidden = !mgr->tokens[index].hidden;
    //     SDL_Log("Token %s: %s", mgr->tokens[index].name,
    //         mgr->tokens[index].hidden ? "hidden" : "visible");
}

void token_render(SDL_Renderer *renderer, const Token *token, int camera_x, int camera_y,
                  int grid_size, int grid_offset_x, int grid_offset_y,
                  float zoom, bool is_dm_view, const FogOfWar *fog) {
    if (!is_dm_view && !fog_is_visible(fog, token->grid_x, token->grid_y)) {
        return;
    }
    
    if (!is_dm_view && token->hidden) {
        return;
    }
    
    SDL_Texture *texture = is_dm_view ? token->dm_texture : token->player_texture;
    if (!texture) return;
    
    // Set opacity: hidden tokens in DM view = 128, otherwise use token's opacity setting
    uint8_t alpha = token->opacity;
    if (is_dm_view && token->hidden) {
        alpha = 128;
    }
    SDL_SetTextureAlphaMod(texture, alpha);
    
    int token_pixel_width = grid_size * token->grid_size;
    float scale = (float)token_pixel_width / token->width;
    float scaled_w = token->width * scale * zoom;
    float scaled_h = token->height * scale * zoom;
    
    SDL_FRect dst;
    dst.x = (token->grid_x * grid_size + grid_offset_x - camera_x) * zoom;
    dst.y = (token->grid_y * grid_size + grid_offset_y - camera_y) * zoom - (scaled_h - grid_size * zoom);
    dst.w = scaled_w;
    dst.h = scaled_h;
    
    // Draw squad ring if token is in a squad (optimized with filled rectangles)
    if (token->squad >= 0 && token->squad < SQUAD_COLOR_COUNT) {
        const SquadColor *color = &SQUAD_COLORS[token->squad];
        SDL_SetRenderDrawColor(renderer, color->r, color->g, color->b, 255);
        
        float ring_thickness = 3.0f * zoom;
        float expand = ring_thickness;
        
        // Draw as 4 filled rectangles (top, bottom, left, right)
        SDL_FRect rects[4] = {
            {dst.x - expand, dst.y - expand, dst.w + 2*expand, ring_thickness},  // Top
            {dst.x - expand, dst.y + dst.h + expand - ring_thickness, dst.w + 2*expand, ring_thickness},  // Bottom
            {dst.x - expand, dst.y, ring_thickness, dst.h},  // Left
            {dst.x + dst.w + expand - ring_thickness, dst.y, ring_thickness, dst.h}  // Right
        };
        SDL_RenderFillRects(renderer, rects, 4);
    }
    
    SDL_RenderTexture(renderer, texture, NULL, &dst);
    
    if (token->selected && is_dm_view) {
        SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
        SDL_RenderRect(renderer, &dst);
    }
    
    // Render damage number above token (using cached texture)
    if (token->damage > 0) {
        float damage_x = dst.x + dst.w / 2;
        float damage_y = dst.y;
        // Pass token pointer so we can access/update cached texture
        render_damage_number_cached(renderer, (Token*)token, damage_x, damage_y, zoom, is_dm_view);
    }
    
    // Render condition indicators below token
    float condition_x = dst.x + dst.w / 2;
    float condition_y = dst.y + dst.h;
    render_token_conditions(renderer, token, condition_x, condition_y, zoom);
}

void token_manager_render_all(SDL_Renderer *renderer, const TokenManager *mgr,
                              int camera_x, int camera_y, int grid_size,
                              int grid_offset_x, int grid_offset_y,
                              float zoom, bool is_dm_view, const FogOfWar *fog) {
    for (int i = 0; i < mgr->count; i++) {
        token_render(renderer, &mgr->tokens[i], camera_x, camera_y, grid_size,
                     grid_offset_x, grid_offset_y, zoom, is_dm_view, fog);
    }
}

// ============================================================================
// SAVE/LOAD SYSTEM
// ============================================================================

bool save_to_slot(int slot, const Map *map, const TokenManager *tokens, const FogOfWar *fog, const InputState *input, const Camera *camera) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "saves/slot_%d.vtt", slot);
    
    FILE *f = fopen(filepath, "wb");
    if (!f) {
    //         SDL_Log("Failed to open save file: %s", filepath);
        return false;
    }
    
    SaveFileHeader header = {0};
    header.magic = 0x56545401;
    strncpy(header.map_path, map->filepath, sizeof(header.map_path) - 1);
    make_path_relative(header.map_path, sizeof(header.map_path));
    normalize_path_slashes(header.map_path);
    header.token_count = tokens->count;
    header.fog_cols = fog->cols;
    header.fog_rows = fog->rows;
    header.grid_size = map->grid_size;
    header.grid_offset_x = map->grid_offset_x;
    header.grid_offset_y = map->grid_offset_y;
    header.show_grid = input->show_grid ? 1 : 0;
    header.camera_x = camera->target_x;
    header.camera_y = camera->target_y;
    header.camera_zoom = camera->target_zoom;
    
    fwrite(&header, sizeof(header), 1, f);
    
    for (int i = 0; i < tokens->count; i++) {
        char normalized_path[256];
        strncpy(normalized_path, tokens->tokens[i].filepath, sizeof(normalized_path) - 1);
        make_path_relative(normalized_path, sizeof(normalized_path));
        normalize_path_slashes(normalized_path);
        fwrite(normalized_path, 256, 1, f);
        fwrite(&tokens->tokens[i].grid_x, sizeof(int), 1, f);
        fwrite(&tokens->tokens[i].grid_y, sizeof(int), 1, f);
        fwrite(&tokens->tokens[i].grid_size, sizeof(int), 1, f);
        fwrite(&tokens->tokens[i].name, 64, 1, f);
        fwrite(&tokens->tokens[i].hidden, sizeof(bool), 1, f);
        fwrite(&tokens->tokens[i].damage, sizeof(int), 1, f);
        fwrite(&tokens->tokens[i].squad, sizeof(int), 1, f);
        fwrite(&tokens->tokens[i].opacity, sizeof(uint8_t), 1, f);
        fwrite(tokens->tokens[i].conditions, sizeof(bool), CONDITION_COUNT, f);
    }
    
    for (int y = 0; y < fog->rows; y++) {
        fwrite(fog->cells[y], sizeof(bool), fog->cols, f);
    }
    
    fclose(f);
    //     SDL_Log("Saved to slot %d", slot);
    return true;
}

bool load_from_slot(int slot, Map *map, TokenManager *tokens, FogOfWar *fog, InputState *input,
                    SDL_Renderer *dm_renderer, SDL_Renderer *player_renderer, Camera *camera) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "saves/slot_%d.vtt", slot);
    
    FILE *f = fopen(filepath, "rb");
    if (!f) {
    //         SDL_Log("Failed to open save file: %s", filepath);
        return false;
    }
    
    // Get file size to determine format
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (file_size < (long)sizeof(SaveFileHeader)) {
    //         SDL_Log("Save file too small: %s", filepath);
        fclose(f);
        return false;
    }
    
    SaveFileHeader header;
    if (fread(&header, sizeof(header), 1, f) != 1) {
    //         SDL_Log("Failed to read save file header");
        fclose(f);
        return false;
    }
    
    if (header.magic != 0x56545401) {
    //         SDL_Log("Invalid save file magic: 0x%08X (expected 0x56545401)", header.magic);
        fclose(f);
        return false;
    }
    
    // Calculate expected file sizes for different versions
    // V1: filepath(256) + grid_x,y,size(12) + name(64) + hidden(1) = 333 bytes per token
    // V2: V1 + damage(4) = 337 bytes per token
    // V3: V2 + squad(4) = 341 bytes per token
    // V4: V3 + opacity(1) = 342 bytes per token
    // V5: V4 + conditions(CONDITION_COUNT bools) = 342 + CONDITION_COUNT bytes per token
    size_t token_data_size_v1 = header.token_count * (256 + sizeof(int)*3 + 64 + sizeof(bool));
    size_t token_data_size_v2 = header.token_count * (256 + sizeof(int)*4 + 64 + sizeof(bool));
    size_t token_data_size_v3 = header.token_count * (256 + sizeof(int)*5 + 64 + sizeof(bool));
    size_t token_data_size_v4 = header.token_count * (256 + sizeof(int)*5 + 64 + sizeof(bool) + sizeof(uint8_t));
    size_t token_data_size_v5 = header.token_count * (256 + sizeof(int)*5 + 64 + sizeof(bool) + sizeof(uint8_t) + sizeof(bool)*CONDITION_COUNT);
    size_t fog_data_size = header.fog_rows * header.fog_cols * sizeof(bool);
    
    size_t expected_size_v1 = sizeof(SaveFileHeader) + token_data_size_v1 + fog_data_size;
    size_t expected_size_v2 = sizeof(SaveFileHeader) + token_data_size_v2 + fog_data_size;
    size_t expected_size_v3 = sizeof(SaveFileHeader) + token_data_size_v3 + fog_data_size;
    size_t expected_size_v4 = sizeof(SaveFileHeader) + token_data_size_v4 + fog_data_size;
    size_t expected_size_v5 = sizeof(SaveFileHeader) + token_data_size_v5 + fog_data_size;
    
    // Determine format version (with some tolerance for small discrepancies)
    int save_version = 1;
    if (file_size >= (long)expected_size_v5 - 100) {
        save_version = 5;  // Has conditions
    } else if (file_size >= (long)expected_size_v4 - 100) {
        save_version = 4;  // Has damage, squad, and opacity
    } else if (file_size >= (long)expected_size_v3 - 100) {
        save_version = 3;  // Has damage and squad
    } else if (file_size >= (long)expected_size_v2 - 100) {
        save_version = 2;  // Has damage but no squad
    }
    
    // const char *version_names[] = {"Unknown", "V1", "V2", "V3", "V4", "V5 (conditions)"};
    //     SDL_Log("Loading save from slot %d... (format: %s)", slot, version_names[save_version]);
    //     SDL_Log("  File size: %ld bytes, V1: %lu, V2: %lu, V3: %lu, V4: %lu, V5: %lu", file_size, 
    //         (unsigned long)expected_size_v1, (unsigned long)expected_size_v2, (unsigned long)expected_size_v3, 
    //         (unsigned long)expected_size_v4, (unsigned long)expected_size_v5);
    //     SDL_Log("  Map: %s", header.map_path);
    //     SDL_Log("  Tokens: %u", header.token_count);
    //     SDL_Log("  Grid: %ux%u (size=%u, offset=%u,%u)", 
    //         header.fog_cols, header.fog_rows, header.grid_size, 
    //         header.grid_offset_x, header.grid_offset_y);
    
    // Restore grid calibration from save
    g_grid_calibration.calibrated_size = header.grid_size;
    g_grid_calibration.calibrated_offset_x = header.grid_offset_x;
    g_grid_calibration.calibrated_offset_y = header.grid_offset_y;
    
    // Clean up existing data
    map_destroy(map);
    token_manager_destroy(tokens);
    fog_destroy(fog);
    
    // Load map
    if (!map_load(map, header.map_path, dm_renderer, player_renderer)) {
    //         SDL_Log("Failed to load map from save: %s", header.map_path);
        fclose(f);
        // Re-initialize empty state
        map_init(map);
        token_manager_init(tokens);
        fog_init(fog, 10, 10);
        return false;
    }
    
    // Apply saved grid configuration to the loaded map
    map->grid_size = header.grid_size;
    map->grid_offset_x = header.grid_offset_x;
    map->grid_offset_y = header.grid_offset_y;
    map->grid_cols = (map->width + map->grid_size - 1) / map->grid_size;
    map->grid_rows = (map->height + map->grid_size - 1) / map->grid_size;
    
    // Initialize fog with correct dimensions
    if (!fog_init(fog, map->grid_cols, map->grid_rows)) {
    //         SDL_Log("Failed to initialize fog of war");
        fclose(f);
        return false;
    }
    
    // Initialize token manager
    token_manager_init(tokens);
    
    // Load tokens based on detected format version
    for (uint32_t i = 0; i < header.token_count; i++) {
        char token_filepath[256];
        int grid_x, grid_y, grid_size;
        char name[64];
        bool hidden;
        int damage = 0;
        int squad = -1;
        uint8_t opacity = 255;
        bool conditions[CONDITION_COUNT];
        for (int c = 0; c < CONDITION_COUNT; c++) conditions[c] = false;
        
        if (fread(token_filepath, 256, 1, f) != 1) {
    //             SDL_Log("Failed to read token %u filepath", i);
            break;
        }
        if (fread(&grid_x, sizeof(int), 1, f) != 1) {
    //             SDL_Log("Failed to read token %u grid_x", i);
            break;
        }
        if (fread(&grid_y, sizeof(int), 1, f) != 1) {
    //             SDL_Log("Failed to read token %u grid_y", i);
            break;
        }
        if (fread(&grid_size, sizeof(int), 1, f) != 1) {
    //             SDL_Log("Failed to read token %u grid_size", i);
            break;
        }
        if (fread(name, 64, 1, f) != 1) {
    //             SDL_Log("Failed to read token %u name", i);
            break;
        }
        if (fread(&hidden, sizeof(bool), 1, f) != 1) {
    //             SDL_Log("Failed to read token %u hidden flag", i);
            break;
        }
        
        // Read damage if V2 or higher
        if (save_version >= 2) {
            if (fread(&damage, sizeof(int), 1, f) != 1) {
    //                 SDL_Log("Failed to read token %u damage", i);
                damage = 0;
            }
        }
        
        // Read squad if V3 or higher
        if (save_version >= 3) {
            if (fread(&squad, sizeof(int), 1, f) != 1) {
    //                 SDL_Log("Failed to read token %u squad", i);
                squad = -1;
            }
        }
        
        // Read opacity if V4 or higher
        if (save_version >= 4) {
            if (fread(&opacity, sizeof(uint8_t), 1, f) != 1) {
    //                 SDL_Log("Failed to read token %u opacity", i);
                opacity = 255;
            }
        }
        
        // Read conditions if V5
        if (save_version >= 5) {
            if (fread(conditions, sizeof(bool), CONDITION_COUNT, f) != CONDITION_COUNT) {
    //                 SDL_Log("Failed to read token %u conditions", i);
                for (int c = 0; c < CONDITION_COUNT; c++) conditions[c] = false;
            }
        }
        
    //         SDL_Log("Token %u: %s at (%d,%d) size=%d hidden=%d damage=%d squad=%d opacity=%u", 
    //             i, name, grid_x, grid_y, grid_size, hidden, damage, squad, opacity);
        
        int token_idx = token_add(tokens, token_filepath, grid_x, grid_y, dm_renderer, player_renderer, name);
        if (token_idx >= 0) {
            tokens->tokens[token_idx].grid_size = grid_size;
            tokens->tokens[token_idx].hidden = hidden;
            tokens->tokens[token_idx].damage = damage;
            tokens->tokens[token_idx].squad = squad;
            tokens->tokens[token_idx].opacity = opacity;
            for (int c = 0; c < CONDITION_COUNT; c++) {
                tokens->tokens[token_idx].conditions[c] = conditions[c];
            }
        } else {
    //             SDL_Log("Warning: Failed to add token: %s", token_filepath);
        }
    }
    
    // Load fog of war data
    //     SDL_Log("Loading fog of war (%dx%d)...", fog->cols, fog->rows);
    int fog_read_cols = (fog->cols < (int)header.fog_cols) ? fog->cols : (int)header.fog_cols;
    int fog_read_rows = (fog->rows < (int)header.fog_rows) ? fog->rows : (int)header.fog_rows;
    
    for (int y = 0; y < fog_read_rows; y++) {
        if (fread(fog->cells[y], sizeof(bool), fog_read_cols, f) != (size_t)fog_read_cols) {
    //             SDL_Log("Warning: Failed to read fog row %d", y);
        }
        // Skip remaining columns in file if map grid is smaller
        if ((int)header.fog_cols > fog->cols) {
            fseek(f, (header.fog_cols - fog->cols) * sizeof(bool), SEEK_CUR);
        }
    }
    
    // Skip remaining rows if map grid is smaller
    if ((int)header.fog_rows > fog->rows) {
        fseek(f, (header.fog_rows - fog->rows) * header.fog_cols * sizeof(bool), SEEK_CUR);
    }
    
    // Restore grid visibility state
    input->show_grid = (header.show_grid != 0);
    
    // Restore camera state
    camera_set_position(camera, header.camera_x, header.camera_y, map->width, map->height);
    camera->target_zoom = header.camera_zoom;
    camera->zoom = header.camera_zoom;  // Set immediate zoom too
    camera_sync(&g_player_camera, camera, map->width, map->height);
    g_player_camera.zoom = header.camera_zoom;  // Set immediate zoom for player camera
    
    fclose(f);
    //     SDL_Log("Successfully loaded from slot %d", slot);
    return true;
}

// ============================================================================
// UI TEXT RENDERING HELPERS (cached for performance)
// ============================================================================

// Helper: Create a text texture from string
SDL_Texture* create_text_texture(SDL_Renderer *renderer, const char *text, float font_size, 
                                 float *out_width, float *out_height, uint8_t r, uint8_t g, uint8_t b) {
    if (!g_font_loaded || !text || !text[0]) return NULL;
    
    float stb_scale = stbtt_ScaleForPixelHeight(&g_font, font_size);
    
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&g_font, &ascent, &descent, &line_gap);
    
    // Calculate dimensions
    float text_width = 0;
    for (int i = 0; text[i]; i++) {
        int advance, left_bearing;
        stbtt_GetCodepointHMetrics(&g_font, text[i], &advance, &left_bearing);
        text_width += advance * stb_scale;
        if (text[i+1]) {
            text_width += stbtt_GetCodepointKernAdvance(&g_font, text[i], text[i+1]) * stb_scale;
        }
    }
    
    float text_height = (ascent - descent) * stb_scale;
    int texture_width = (int)(text_width + 2);
    int texture_height = (int)(text_height + 2);
    
    // Create render target
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_TARGET,
                                             texture_width, texture_height);
    if (!texture) return NULL;
    
    SDL_SetRenderTarget(renderer, texture);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    
    // Render text
    float cursor_x = 0;
    float baseline_y = ascent * stb_scale;
    
    for (int i = 0; text[i]; i++) {
        int advance, left_bearing;
        stbtt_GetCodepointHMetrics(&g_font, text[i], &advance, &left_bearing);
        
        int glyph_width, glyph_height, xoff, yoff;
        unsigned char *bitmap = stbtt_GetCodepointBitmap(&g_font, 0, stb_scale,
                                                         text[i], &glyph_width, &glyph_height,
                                                         &xoff, &yoff);
        
        if (bitmap) {
            SDL_Surface *surface = SDL_CreateSurface(glyph_width, glyph_height, SDL_PIXELFORMAT_RGBA32);
            if (surface) {
                uint32_t *pixels = (uint32_t*)surface->pixels;
                for (int py = 0; py < glyph_height; py++) {
                    for (int px = 0; px < glyph_width; px++) {
                        unsigned char alpha = bitmap[py * glyph_width + px];
                        pixels[py * glyph_width + px] = (alpha << 24) | (b << 16) | (g << 8) | r;
                    }
                }
                
                SDL_Texture *glyph_tex = SDL_CreateTextureFromSurface(renderer, surface);
                if (glyph_tex) {
                    SDL_SetTextureBlendMode(glyph_tex, SDL_BLENDMODE_BLEND);
                    SDL_FRect dst = {
                        cursor_x + left_bearing * stb_scale,
                        baseline_y + yoff,
                        (float)glyph_width,
                        (float)glyph_height
                    };
                    SDL_RenderTexture(renderer, glyph_tex, NULL, &dst);
                    SDL_DestroyTexture(glyph_tex);
                }
                SDL_DestroySurface(surface);
            }
            stbtt_FreeBitmap(bitmap, NULL);
        }
        
        cursor_x += advance * stb_scale;
        if (text[i+1]) {
            cursor_x += stbtt_GetCodepointKernAdvance(&g_font, text[i], text[i+1]) * stb_scale;
        }
    }
    
    SDL_SetRenderTarget(renderer, NULL);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    
    *out_width = text_width;
    *out_height = text_height;
    
    return texture;
}

// Helper function to get 2-letter abbreviation for each condition
const char* get_condition_abbrev(int condition_index) {
    switch (condition_index) {
        case CONDITION_BLEEDING: return "BL";
        case CONDITION_DAZED: return "DA";
        case CONDITION_FRIGHTENED: return "FR";
        case CONDITION_GRABBED: return "GR";
        case CONDITION_RESTRAINED: return "RE";
        case CONDITION_SLOWED: return "SL";
        case CONDITION_TAUNTED: return "TA";
        case CONDITION_WEAKEND: return "WE";
        default: return "??";
    }
}

// ============================================================================
// CONDITION WHEEL UI
// ============================================================================

// Wheel size scale factor (adjust this to make wheel bigger/smaller)
#define CONDITION_WHEEL_SCALE 1.6f

void render_condition_wheel(SDL_Renderer *renderer, const InputState *input, const TokenManager *tokens,
                            int window_width, int window_height) {
    if (!input->condition_wheel_open) return;
    if (input->condition_wheel_token_index < 0 || input->condition_wheel_token_index >= tokens->count) return;
    
    const Token *token = &tokens->tokens[input->condition_wheel_token_index];
    
    float center_x = window_width / 2.0f;
    float center_y = window_height / 2.0f;
    float radius = 150.0f * CONDITION_WHEEL_SCALE;
    float inner_radius = 50.0f * CONDITION_WHEEL_SCALE;
    
    // Get mouse position
    float mouse_x, mouse_y;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    
    // Calculate which segment is hovered
    float dx = mouse_x - center_x;
    float dy = mouse_y - center_y;
    float dist = sqrtf(dx * dx + dy * dy);
    float angle = atan2f(dy, dx);
    if (angle < 0) angle += 2.0f * M_PI;
    
    int hovered_index = -1;
    if (dist >= inner_radius && dist <= radius) {
        float segment_angle = (2.0f * M_PI) / CONDITION_COUNT;
        hovered_index = (int)(angle / segment_angle);
        if (hovered_index >= CONDITION_COUNT) hovered_index = CONDITION_COUNT - 1;
    }
    
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    
    // Draw wheel segments for ALL conditions (active ones grayed out)
    for (int i = 0; i < CONDITION_COUNT; i++) {
        const ConditionInfo *info = &CONDITION_INFO[i];
        bool is_active = token->conditions[i];
        
        float start_angle = (2.0f * M_PI * i) / CONDITION_COUNT;
        float end_angle = (2.0f * M_PI * (i + 1)) / CONDITION_COUNT;
        
        // Convert colors from 0-255 to 0.0-1.0 for SDL3
        float r, g, b, alpha;
        
        if (is_active) {
            // Gray out active conditions (desaturate to gray)
            float gray = (info->r * 0.3f + info->g * 0.59f + info->b * 0.11f) / 255.0f;
            r = g = b = gray * 0.5f;  // Darker gray
            alpha = (i == hovered_index) ? 0.9f : 0.7f;
        } else {
            // Full color for inactive conditions
            r = info->r / 255.0f;
            g = info->g / 255.0f;
            b = info->b / 255.0f;
            alpha = (i == hovered_index) ? 1.0f : 0.85f;
        }
        
        int steps = 30;
        for (int step = 0; step < steps; step++) {
            float a1 = start_angle + (end_angle - start_angle) * step / steps;
            float a2 = start_angle + (end_angle - start_angle) * (step + 1) / steps;
            
            float x1_inner = center_x + inner_radius * cosf(a1);
            float y1_inner = center_y + inner_radius * sinf(a1);
            float x1_outer = center_x + radius * cosf(a1);
            float y1_outer = center_y + radius * sinf(a1);
            
            float x2_inner = center_x + inner_radius * cosf(a2);
            float y2_inner = center_y + inner_radius * sinf(a2);
            float x2_outer = center_x + radius * cosf(a2);
            float y2_outer = center_y + radius * sinf(a2);
            
            // Use SDL_Vertex with properly normalized colors (0.0-1.0)
            SDL_Vertex vertices[4] = {
                {{x1_inner, y1_inner}, {r, g, b, alpha}, {0, 0}},
                {{x1_outer, y1_outer}, {r, g, b, alpha}, {0, 0}},
                {{x2_outer, y2_outer}, {r, g, b, alpha}, {0, 0}},
                {{x2_inner, y2_inner}, {r, g, b, alpha}, {0, 0}}
            };
            
            int indices[6] = {0, 1, 2, 0, 2, 3};
            SDL_RenderGeometry(renderer, NULL, vertices, 4, indices, 6);
        }
        
        // Draw border lines with more contrast when hovered
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, i == hovered_index ? 255 : 180);
        float line_angle_start = start_angle;
        SDL_RenderLine(renderer, 
                       center_x + inner_radius * cosf(line_angle_start),
                       center_y + inner_radius * sinf(line_angle_start),
                       center_x + radius * cosf(line_angle_start),
                       center_y + radius * sinf(line_angle_start));
    }
    
    // Draw center circle using scanline algorithm
    SDL_SetRenderDrawColor(renderer, 40, 40, 60, 240);
    int r = (int)inner_radius;
    for (int y = -r; y <= r; y++) {
        int half_width = (int)sqrtf(inner_radius * inner_radius - y * y);
        if (half_width > 0) {
            SDL_FRect line = {center_x - half_width, center_y + y, half_width * 2, 1};
            SDL_RenderFillRect(renderer, &line);
        }
    }
    
    // Initialize condition text cache if needed
    if (g_font_loaded && !g_condition_wheel_cache_initialized) {
        float font_size = 14.0f * CONDITION_WHEEL_SCALE;
        for (int i = 0; i < CONDITION_COUNT; i++) {
            const char *condition_name = CONDITION_INFO[i].name;
            g_condition_wheel_text_cache[i].texture = create_text_texture(
                renderer, condition_name, font_size, 
                &g_condition_wheel_text_cache[i].width,
                &g_condition_wheel_text_cache[i].height,
                255, 255, 255
            );
            strncpy(g_condition_wheel_text_cache[i].cached_text, condition_name, 
                    sizeof(g_condition_wheel_text_cache[i].cached_text) - 1);
        }
        g_condition_wheel_cache_initialized = true;
    }
    
    // Draw condition names (using cached textures) - ALL conditions
    if (g_font_loaded && g_condition_wheel_cache_initialized) {
        for (int i = 0; i < CONDITION_COUNT; i++) {
            bool is_active = token->conditions[i];
            
            float mid_angle = (2.0f * M_PI * (i + 0.5f)) / CONDITION_COUNT;
            float text_radius = (radius + inner_radius) / 2.0f;
            float text_x = center_x + text_radius * cosf(mid_angle);
            float text_y = center_y + text_radius * sinf(mid_angle);
            
            const ConditionInfo *info = &CONDITION_INFO[i];
            SDL_Texture *text_tex = g_condition_wheel_text_cache[i].texture;
            float text_w = g_condition_wheel_text_cache[i].width;
            float text_h = g_condition_wheel_text_cache[i].height;
            
            if (text_tex) {
                // Draw colored background for the text
                float padding = 4.0f * CONDITION_WHEEL_SCALE;
                SDL_FRect text_bg = {text_x - text_w/2 - padding, text_y - text_h/2 - padding, text_w + padding*2, text_h + padding*2};
                
                if (is_active) {
                    // Gray background for active conditions
                    uint8_t gray = (uint8_t)(info->r * 0.3f + info->g * 0.59f + info->b * 0.11f) / 2;
                    SDL_SetRenderDrawColor(renderer, gray, gray, gray, 200);
                } else {
                    // Full color background for inactive conditions
                    SDL_SetRenderDrawColor(renderer, info->r, info->g, info->b, 240);
                }
                SDL_RenderFillRect(renderer, &text_bg);
                
                // Draw border - thicker if hovered
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                SDL_RenderRect(renderer, &text_bg);
                if (i == hovered_index) {
                    SDL_FRect text_bg_outer = {text_bg.x - 1, text_bg.y - 1, text_bg.w + 2, text_bg.h + 2};
                    SDL_RenderRect(renderer, &text_bg_outer);
                    SDL_FRect text_bg_outer2 = {text_bg.x - 2, text_bg.y - 2, text_bg.w + 4, text_bg.h + 4};
                    SDL_RenderRect(renderer, &text_bg_outer2);
                }
                
                // Render the cached text (dimmer if active)
                SDL_SetTextureAlphaMod(text_tex, is_active ? 150 : 255);
                SDL_FRect text_dst = {text_x - text_w/2, text_y - text_h/2, text_w, text_h};
                SDL_RenderTexture(renderer, text_tex, NULL, &text_dst);
                SDL_SetTextureAlphaMod(text_tex, 255);  // Reset alpha
            }
        }
    }
    
    // Draw instructions
    if (g_font_loaded) {
        static SDL_Texture *help_tex = NULL;
        static float help_w = 0, help_h = 0;
        const char *help = "Click to toggle condition (Gray = Active) | ESC to close";
        
        if (!help_tex) {
            help_tex = create_text_texture(renderer, help, 16.0f * CONDITION_WHEEL_SCALE, &help_w, &help_h, 255, 255, 255);
        }
        
        if (help_tex) {
            SDL_FRect help_dst = {center_x - help_w/2, center_y + radius + 20 * CONDITION_WHEEL_SCALE, help_w, help_h};
            SDL_RenderTexture(renderer, help_tex, NULL, &help_dst);
        }
    }
}

void render_token_conditions(SDL_Renderer *renderer, const Token *token, float x, float y, float scale) {
    // Count active conditions
    int active_count = 0;
    for (int i = 0; i < CONDITION_COUNT; i++) {
        if (token->conditions[i]) active_count++;
    }
    
    if (active_count == 0) return;
    if (!g_font_loaded) return;

    // Render 2-letter abbreviations stacked vertically at the bottom left of the token, growing up
    float font_size = 16.0f * scale;
    float padding = 4 * scale;
    float tag_width = 24.0f * scale;
    float tag_height = 18.0f * scale;
    float tag_x = x - (token->width * scale) / 2 + (tag_width * 0.6f);
    float tag_y = y - tag_height - padding;
    
    int tag_index = 0;
    for (int i = 0; i < CONDITION_COUNT; i++) {
        if (token->conditions[i]) {
            const ConditionInfo *info = &CONDITION_INFO[i];
            const char *abbrev = get_condition_abbrev(i);

            // Render the abbreviation with a colored background
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_FRect tag_bg = {tag_x - padding, tag_y - padding, tag_width + padding*2, tag_height + padding*2};
            SDL_SetRenderDrawColor(renderer, info->r, info->g, info->b, 220);
            SDL_RenderFillRect(renderer, &tag_bg);

            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderRect(renderer, &tag_bg);

            // Render the abbreviation text
            float text_w, text_h;
            SDL_Texture *text_tex = create_text_texture(renderer, abbrev, font_size, &text_w, &text_h, 255, 255, 255);
            if (text_tex) {
                SDL_FRect text_dst = {tag_x + (tag_width - text_w) / 2, tag_y + (tag_height - text_h) / 2, text_w, text_h};
                SDL_RenderTexture(renderer, text_tex, NULL, &text_dst);
                SDL_DestroyTexture(text_tex);
            }

            tag_index++;
            
            // Move to next position AFTER drawing (so next tag stacks upward)
            tag_y -= (tag_height + padding * 2);
            
            // If we have too many conditions, stop
            if (tag_index >= 6) break;
        }
    }
}

// ============================================================================
// SQUAD MODE UI (with texture caching)
// ============================================================================

void render_squad_mode_ui(SDL_Renderer *renderer, const InputState *input, int window_width, int window_height) {
    (void)window_height;  // Unused parameter
    if (input->current_tool != TOOL_SQUAD || !g_font_loaded) return;
    
    const SquadColor *current_color = &SQUAD_COLORS[input->current_squad];
    
    // Create display text
    char display_text[128];
    snprintf(display_text, sizeof(display_text), "Squad Mode: %s", current_color->name);
    
    // Check if we need to regenerate the cached texture
    if (g_squad_mode_cache.texture == NULL || 
        strcmp(g_squad_mode_cache.cached_text, display_text) != 0 ||
        g_squad_mode_cache.cached_value != input->current_squad) {
        
        // Destroy old texture
        if (g_squad_mode_cache.texture) {
            SDL_DestroyTexture(g_squad_mode_cache.texture);
        }
        
        // Create new texture
        g_squad_mode_cache.texture = create_text_texture(renderer, display_text, 24.0f,
                                                         &g_squad_mode_cache.width,
                                                         &g_squad_mode_cache.height,
                                                         255, 255, 255);
        strncpy(g_squad_mode_cache.cached_text, display_text, sizeof(g_squad_mode_cache.cached_text) - 1);
        g_squad_mode_cache.cached_value = input->current_squad;
    }
    
    if (!g_squad_mode_cache.texture) return;
    
    // Calculate layout
    float padding = 12.0f;
    float color_box_size = g_squad_mode_cache.height;
    float spacing = 10.0f;
    float bg_width = g_squad_mode_cache.width + color_box_size + spacing + padding * 3;
    float bg_height = g_squad_mode_cache.height + padding * 2;
    
    SDL_FRect bg_rect = {
        (window_width - bg_width) / 2,
        20,
        bg_width,
        bg_height
    };
    
    // Draw background
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 40, 40, 60, 240);
    SDL_RenderFillRect(renderer, &bg_rect);
    
    // Draw border
    SDL_SetRenderDrawColor(renderer, current_color->r, current_color->g, current_color->b, 255);
    SDL_FRect borders[3] = {
        {bg_rect.x, bg_rect.y, bg_rect.w, bg_rect.h},
        {bg_rect.x - 1, bg_rect.y - 1, bg_rect.w + 2, bg_rect.h + 2},
        {bg_rect.x - 2, bg_rect.y - 2, bg_rect.w + 4, bg_rect.h + 4}
    };
    SDL_RenderRects(renderer, borders, 3);
    
    // Draw color indicator box
    SDL_FRect color_box = {bg_rect.x + padding, bg_rect.y + padding, color_box_size, color_box_size};
    SDL_SetRenderDrawColor(renderer, current_color->r, current_color->g, current_color->b, 255);
    SDL_RenderFillRect(renderer, &color_box);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderRect(renderer, &color_box);
    
    // Render cached text texture
    SDL_FRect text_dst = {
        bg_rect.x + padding + color_box_size + spacing,
        bg_rect.y + padding,
        g_squad_mode_cache.width,
        g_squad_mode_cache.height
    };
    SDL_RenderTexture(renderer, g_squad_mode_cache.texture, NULL, &text_dst);
    
    // Help text (simplified - no per-frame regeneration)
    const char *help = "Q/E=Cycle | Click=Assign | Click Again=Remove";
    static SDL_Texture *help_tex = NULL;
    static float help_w = 0, help_h = 0;
    
    if (!help_tex) {
        help_tex = create_text_texture(renderer, help, 14.0f, &help_w, &help_h, 238, 238, 238);
    }
    
    if (help_tex) {
        SDL_FRect help_dst = {bg_rect.x, bg_rect.y + bg_rect.h + 8, help_w, help_h};
        SDL_RenderTexture(renderer, help_tex, NULL, &help_dst);
    }
}

// ============================================================================
// DRAW MODE UI (with texture caching)
// ============================================================================

void render_draw_mode_ui(SDL_Renderer *renderer, const InputState *input, int window_width, int window_height) {
    if (input->current_tool != TOOL_DRAW || !g_font_loaded) return;
    (void)window_height;  // Unused parameter
    
    const SquadColor *current_color = &SQUAD_COLORS[input->current_squad];
    const char *shape_name = (input->current_shape == SHAPE_RECTANGLE) ? "Rectangle" : "Circle";
    
    char display_text[128];
    snprintf(display_text, sizeof(display_text), "Draw Mode: %s - %s", current_color->name, shape_name);
    
    // Check if we need to regenerate cached texture
    static SDL_Texture *draw_ui_tex = NULL;
    static char cached_text[128] = "";
    static int cached_color = -1;
    static float cached_w = 0, cached_h = 0;
    
    if (!draw_ui_tex || strcmp(cached_text, display_text) != 0 || cached_color != input->current_squad) {
        if (draw_ui_tex) SDL_DestroyTexture(draw_ui_tex);
        draw_ui_tex = create_text_texture(renderer, display_text, 24.0f, &cached_w, &cached_h, 255, 255, 255);
        strncpy(cached_text, display_text, sizeof(cached_text) - 1);
        cached_color = input->current_squad;
    }
    
    if (!draw_ui_tex) return;
    
    float padding = 12.0f;
    float color_box_size = cached_h;
    float spacing = 10.0f;
    float bg_width = cached_w + color_box_size + spacing + padding * 3;
    float bg_height = cached_h + padding * 2;
    
    SDL_FRect bg_rect = {
        (window_width - bg_width) / 2,
        20,
        bg_width,
        bg_height
    };
    
    // Draw background
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 40, 40, 60, 240);
    SDL_RenderFillRect(renderer, &bg_rect);
    
    // Draw border with current color
    SDL_SetRenderDrawColor(renderer, current_color->r, current_color->g, current_color->b, 255);
    SDL_FRect borders[3] = {
        {bg_rect.x, bg_rect.y, bg_rect.w, bg_rect.h},
        {bg_rect.x - 1, bg_rect.y - 1, bg_rect.w + 2, bg_rect.h + 2},
        {bg_rect.x - 2, bg_rect.y - 2, bg_rect.w + 4, bg_rect.h + 4}
    };
    SDL_RenderRects(renderer, borders, 3);
    
    // Draw color indicator box
    SDL_FRect color_box = {bg_rect.x + padding, bg_rect.y + padding, color_box_size, color_box_size};
    SDL_SetRenderDrawColor(renderer, current_color->r, current_color->g, current_color->b, 255);
    SDL_RenderFillRect(renderer, &color_box);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderRect(renderer, &color_box);
    
    // Render text
    SDL_FRect text_dst = {
        bg_rect.x + padding + color_box_size + spacing,
        bg_rect.y + padding,
        cached_w,
        cached_h
    };
    SDL_RenderTexture(renderer, draw_ui_tex, NULL, &text_dst);
    
    // Help text
    static SDL_Texture *help_tex = NULL;
    static float help_w = 0, help_h = 0;
    const char *help = "Q/E=Color | W=Cycle Shape | Right Click=Delete | X=Clear All";
    
    if (!help_tex) {
        help_tex = create_text_texture(renderer, help, 14.0f, &help_w, &help_h, 238, 238, 238);
    }
    
    if (help_tex) {
        SDL_FRect help_dst = {bg_rect.x, bg_rect.y + bg_rect.h + 8, help_w, help_h};
        SDL_RenderTexture(renderer, help_tex, NULL, &help_dst);
    }
}

// Render current drawing preview
void render_drawing_preview(SDL_Renderer *renderer, const InputState *input,
                            float camera_x, float camera_y, float zoom) {
    if (!input->drawing_shape) return;
    
    const SquadColor *color = &SQUAD_COLORS[input->current_squad];
    
    float mouse_x, mouse_y;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    
    int world_x = (int)(mouse_x / zoom + camera_x);
    int world_y = (int)(mouse_y / zoom + camera_y);
    
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color->r, color->g, color->b, 100);
    
    if (input->current_shape == SHAPE_RECTANGLE) {
        SDL_FRect rect = {
            (input->draw_start_x - camera_x) * zoom,
            (input->draw_start_y - camera_y) * zoom,
            (world_x - input->draw_start_x) * zoom,
            (world_y - input->draw_start_y) * zoom
        };
        
        // Normalize
        if (rect.w < 0) {
            rect.x += rect.w;
            rect.w = -rect.w;
        }
        if (rect.h < 0) {
            rect.y += rect.h;
            rect.h = -rect.h;
        }
        
        SDL_RenderFillRect(renderer, &rect);
        SDL_SetRenderDrawColor(renderer, color->r, color->g, color->b, 255);
        SDL_RenderRect(renderer, &rect);
    } else {
        // Circle preview with optimized rendering
        float cx = ((input->draw_start_x + world_x) / 2.0f - camera_x) * zoom;
        float cy = ((input->draw_start_y + world_y) / 2.0f - camera_y) * zoom;
        float radius = sqrtf(powf((world_x - input->draw_start_x) * zoom, 2) + 
                             powf((world_y - input->draw_start_y) * zoom, 2)) / 2.0f;
        
        render_filled_circle(renderer, cx, cy, radius);
        
        // Draw border
        SDL_SetRenderDrawColor(renderer, color->r, color->g, color->b, 255);
        int r = (int)radius;
        int x = 0;
        int y = r;
        int d = 3 - 2 * r;
        
        while (y >= x) {
            SDL_RenderPoint(renderer, cx + x, cy + y);
            SDL_RenderPoint(renderer, cx - x, cy + y);
            SDL_RenderPoint(renderer, cx + x, cy - y);
            SDL_RenderPoint(renderer, cx - x, cy - y);
            SDL_RenderPoint(renderer, cx + y, cy + x);
            SDL_RenderPoint(renderer, cx - y, cy + x);
            SDL_RenderPoint(renderer, cx + y, cy - x);
            SDL_RenderPoint(renderer, cx - y, cy - x);
            
            x++;
            if (d > 0) {
                y--;
                d = d + 4 * (x - y) + 10;
            } else {
                d = d + 4 * x + 6;
            }
        }
    }
}

// ============================================================================
// DAMAGE INPUT UI (with texture caching)
// ============================================================================

void render_damage_input_ui(SDL_Renderer *renderer, const InputState *input, int window_width, int window_height) {
    (void)window_height;  // Unused parameter
    if (!input->damage_input_mode || !g_font_loaded) return;
    
    const char *prefix = input->shift_pressed ? "Heal: " : "Damage: ";
    char display_text[64];
    if (input->damage_input_len > 0) {
        snprintf(display_text, sizeof(display_text), "%s%s_", prefix, input->damage_input_buffer);
    } else {
        snprintf(display_text, sizeof(display_text), "%s_", prefix);
    }
    
    // Check if we need to regenerate cached texture
    int cache_key = input->shift_pressed ? 1 : 0;
    if (g_damage_input_cache.texture == NULL ||
        strcmp(g_damage_input_cache.cached_text, display_text) != 0 ||
        g_damage_input_cache.cached_value != cache_key) {
        
        // Destroy old texture
        if (g_damage_input_cache.texture) {
            SDL_DestroyTexture(g_damage_input_cache.texture);
        }
        
        // Create new texture
        g_damage_input_cache.texture = create_text_texture(renderer, display_text, 24.0f,
                                                           &g_damage_input_cache.width,
                                                           &g_damage_input_cache.height,
                                                           255, 255, 255);
        strncpy(g_damage_input_cache.cached_text, display_text, sizeof(g_damage_input_cache.cached_text) - 1);
        g_damage_input_cache.cached_value = cache_key;
    }
    
    if (!g_damage_input_cache.texture) return;
    
    // Calculate layout
    float padding = 12.0f;
    float bg_width = g_damage_input_cache.width + padding * 2;
    float bg_height = g_damage_input_cache.height + padding * 2;
    
    SDL_FRect bg_rect = {
        (window_width - bg_width) / 2,
        20,
        bg_width,
        bg_height
    };
    
    // Draw background
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 40, 40, 60, 240);
    SDL_RenderFillRect(renderer, &bg_rect);
    
    // Draw border
    SDL_SetRenderDrawColor(renderer, 
                           input->shift_pressed ? 100 : 200, 
                           input->shift_pressed ? 200 : 100, 
                           100, 255);
    SDL_FRect borders[2] = {
        {bg_rect.x, bg_rect.y, bg_rect.w, bg_rect.h},
        {bg_rect.x - 1, bg_rect.y - 1, bg_rect.w + 2, bg_rect.h + 2}
    };
    SDL_RenderRects(renderer, borders, 2);
    
    // Render cached text texture
    SDL_FRect text_dst = {
        bg_rect.x + padding,
        bg_rect.y + padding,
        g_damage_input_cache.width,
        g_damage_input_cache.height
    };
    SDL_RenderTexture(renderer, g_damage_input_cache.texture, NULL, &text_dst);
    
    // Help text (static, cached once)
    static SDL_Texture *help_tex = NULL;
    static float help_w = 0, help_h = 0;
    const char *help = "Enter=Confirm | Esc=Cancel | Shift=Toggle";
    
    if (!help_tex) {
        help_tex = create_text_texture(renderer, help, 14.0f, &help_w, &help_h, 238, 238, 238);
    }
    
    if (help_tex) {
        SDL_FRect help_dst = {bg_rect.x, bg_rect.y + bg_rect.h + 8, help_w, help_h};
        SDL_RenderTexture(renderer, help_tex, NULL, &help_dst);
    }
}

// ============================================================================
// RENDERER
// ============================================================================

void renderer_render_dm(WindowState *window, const Map *map, const TokenManager *tokens,
                        const FogOfWar *fog, const InputState *input, Camera *camera) {
    SDL_Renderer *renderer = window->renderer;
    float zoom = camera->zoom;
    
    // Get actual window dimensions
    int win_width, win_height;
    SDL_GetWindowSize(window->window, &win_width, &win_height);
    
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    
    if (map->dm_texture) {
        SDL_FRect dst = {-camera->x * zoom, -camera->y * zoom, map->width * zoom, map->height * zoom};
        SDL_RenderTexture(renderer, map->dm_texture, NULL, &dst);
    }
    
    // Render grid only if visible
    if (input->show_grid) {
        grid_render(renderer, map, (int)camera->x, (int)camera->y,
                    win_width, win_height, zoom);
    }
    
    fog_render(renderer, fog, camera->x, camera->y,
               win_width, win_height, map->grid_size,
               map->grid_offset_x, map->grid_offset_y, zoom, true);
    
    // Render drawings
    drawing_render_all(renderer, &g_drawings, (int)camera->x, (int)camera->y, zoom);
    
    token_manager_render_all(renderer, tokens, (int)camera->x, (int)camera->y,
                             map->grid_size, map->grid_offset_x, map->grid_offset_y,
                             zoom, true, fog);
    
    // Render calibration overlay
    grid_calibration_render(renderer, &g_grid_calibration, camera->x, camera->y, zoom);
    
    // Render drawing preview
    render_drawing_preview(renderer, input, camera->x, camera->y, zoom);
    
    // Render UI overlays
    render_draw_mode_ui(renderer, input, win_width, win_height);
    render_squad_mode_ui(renderer, input, win_width, win_height);
    render_damage_input_ui(renderer, input, win_width, win_height);
    render_condition_wheel(renderer, input, tokens, win_width, win_height);
    
    SDL_RenderPresent(renderer);
}

void renderer_render_player(WindowState *window, const Map *map, const TokenManager *tokens,
                            const FogOfWar *fog, Camera *camera) {
    SDL_Renderer *renderer = window->renderer;
    float zoom = camera->zoom;
    
    // Get actual window dimensions
    int win_width, win_height;
    SDL_GetWindowSize(window->window, &win_width, &win_height);
    
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    
    if (map->player_texture) {
        SDL_FRect dst = {-camera->x * zoom, -camera->y * zoom, map->width * zoom, map->height * zoom};
        SDL_RenderTexture(renderer, map->player_texture, NULL, &dst);
    }
    
    // Render drawings (visible on player view)
    drawing_render_all(renderer, &g_drawings, (int)camera->x, (int)camera->y, zoom);
    
    token_manager_render_all(renderer, tokens, (int)camera->x, (int)camera->y,
                             map->grid_size, map->grid_offset_x, map->grid_offset_y,
                             zoom, false, fog);
    
    fog_render(renderer, fog, camera->x, camera->y,
               win_width, win_height, map->grid_size,
               map->grid_offset_x, map->grid_offset_y, zoom, false);
    
    SDL_RenderPresent(renderer);
}

// ============================================================================
// INPUT HANDLING
// ============================================================================

void input_init(InputState *input) {
    input->current_tool = TOOL_SELECT;
    input->mouse_x = 0;
    input->mouse_y = 0;
    input->mouse_left_down = false;
    input->mouse_right_down = false;
    input->last_mouse_x = 0;
    input->last_mouse_y = 0;
    input->dragging_token = false;
    input->dragged_token_index = -1;
    input->painting_fog = false;
    input->fog_paint_mode = false;
    input->shift_pressed = false;
    input->ctrl_pressed = false;
    input->show_grid = true;  // Grid visible by default
    input->damage_input_mode = false;
    input->damage_input_buffer[0] = '\0';
    input->damage_input_len = 0;
    input->current_squad = 0;  // Default to first squad color (Red)
    input->drawing_shape = false;
    input->current_shape = SHAPE_RECTANGLE;
    input->draw_start_x = 0;
    input->draw_start_y = 0;
    input->condition_wheel_open = false;
    input->condition_wheel_token_index = -1;
}

void input_handle_dm_mouse(Camera *camera, InputState *input, TokenManager *tokens,
                           FogOfWar *fog, Map *map, SDL_Event *event) {
    float zoom = camera->zoom;
    float cam_x = camera->x;
    float cam_y = camera->y;
    
    // Handle calibration mode separately
    if (g_grid_calibration.active) {
        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT) {
            g_grid_calibration.dragging = true;
            g_grid_calibration.start_x = (int)(event->button.x / zoom + cam_x);
            g_grid_calibration.start_y = (int)(event->button.y / zoom + cam_y);
        }
        
        if (event->type == SDL_EVENT_MOUSE_MOTION && g_grid_calibration.dragging) {
            g_grid_calibration.end_x = (int)(event->motion.x / zoom + cam_x);
            g_grid_calibration.end_y = (int)(event->motion.y / zoom + cam_y);
        }
        
        if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT) {
            g_grid_calibration.dragging = false;
        }
        return;  // Don't process other mouse events in calibration mode
    }
    
    // Handle condition wheel clicks
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT && input->condition_wheel_open) {
        int win_width, win_height;
        SDL_GetWindowSize(g_wm.dm_window.window, &win_width, &win_height);
        
        float center_x = win_width / 2.0f;
        float center_y = win_height / 2.0f;
        float radius = 150.0f * CONDITION_WHEEL_SCALE;
        float inner_radius = 50.0f * CONDITION_WHEEL_SCALE;
        
        float dx = event->button.x - center_x;
        float dy = event->button.y - center_y;
        float dist = sqrtf(dx * dx + dy * dy);
        
        if (dist >= inner_radius && dist <= radius) {
            if (input->condition_wheel_token_index >= 0 && input->condition_wheel_token_index < tokens->count) {
                Token *token = &tokens->tokens[input->condition_wheel_token_index];
                
                float angle = atan2f(dy, dx);
                if (angle < 0) angle += 2.0f * M_PI;
                
                float segment_angle = (2.0f * M_PI) / CONDITION_COUNT;
                int clicked_condition = (int)(angle / segment_angle);
                if (clicked_condition >= CONDITION_COUNT) clicked_condition = CONDITION_COUNT - 1;
                
                // Toggle condition on/off
                token->conditions[clicked_condition] = !token->conditions[clicked_condition];
    //                 SDL_Log("Token %s: %s condition %s", token->name,
    //                     token->conditions[clicked_condition] ? "Added" : "Removed",
    //                     CONDITION_INFO[clicked_condition].name);
            }
        }
        return;  // Don't process other mouse actions while wheel is open
    }
    
    // Normal mouse handling
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT) {
        int grid_x, grid_y;
        grid_screen_to_grid_direct(event->button.x, event->button.y,
                                   cam_x, cam_y, zoom,
                                   map->grid_size, map->grid_offset_x, map->grid_offset_y,
                                   &grid_x, &grid_y);
        
        if (input->current_tool == TOOL_SELECT) {
            Token *token = token_get_at_grid(tokens, grid_x, grid_y);
            if (token) {
                int token_idx = -1;
                for (int i = 0; i < tokens->count; i++) {
                    if (&tokens->tokens[i] == token) {
                        token_idx = i;
                        break;
                    }
                }
                
                if (input->shift_pressed || input->ctrl_pressed) {
                    int new_idx = token_copy(tokens, token_idx, grid_x, grid_y,
                                             g_wm.dm_window.renderer, g_wm.player_window.renderer);
                    if (new_idx >= 0) {
                        token_deselect_all(tokens);
                        tokens->tokens[new_idx].selected = true;
                        input->dragging_token = true;
                        input->dragged_token_index = new_idx;
                    }
                } else {
                    token_deselect_all(tokens);
                    token->selected = true;
                    input->dragging_token = true;
                    input->dragged_token_index = token_idx;
                }
            } else {
                token_deselect_all(tokens);
            }
        } else if (input->current_tool == TOOL_FOG) {
            // Start fog painting - determine mode based on initial cell state
            input->painting_fog = true;
            bool cell_visible = fog_is_visible(fog, grid_x, grid_y);
            // If cell is visible, we'll paint fog (hide it). If fogged, we'll reveal it.
            input->fog_paint_mode = cell_visible;
            fog_set_cell(fog, grid_x, grid_y, !cell_visible);
        } else if (input->current_tool == TOOL_SQUAD) {
            // Squad mode - assign/remove squad from token
            Token *token = token_get_at_grid(tokens, grid_x, grid_y);
            if (token) {
                // If token already has this squad, remove it; otherwise assign it
                if (token->squad == input->current_squad) {
                    token->squad = -1;
    //                     SDL_Log("Removed %s from squad", token->name);
                } else {
                    token->squad = input->current_squad;
    //                     SDL_Log("Assigned %s to squad %s", token->name, SQUAD_COLORS[input->current_squad].name);
                }
            }
        } else if (input->current_tool == TOOL_DRAW) {
            // Draw mode - start drawing shape
            input->drawing_shape = true;
            // Convert screen to world coordinates
            input->draw_start_x = (int)(event->button.x / zoom + cam_x);
            input->draw_start_y = (int)(event->button.y / zoom + cam_y);
        }
    }
    
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT) {
        input->dragging_token = false;
        input->painting_fog = false;
        
        // Complete drawing shape
        if (input->drawing_shape) {
            int end_x = (int)(event->button.x / zoom + cam_x);
            int end_y = (int)(event->button.y / zoom + cam_y);
            
            // Only add if shape has some size
            if (abs(end_x - input->draw_start_x) > 5 || abs(end_y - input->draw_start_y) > 5) {
                drawing_add(&g_drawings, input->current_shape,
                            input->draw_start_x, input->draw_start_y,
                            end_x, end_y,
                            input->current_squad);
            }
            
            input->drawing_shape = false;
        }
    }
    
    if (event->type == SDL_EVENT_MOUSE_MOTION && input->dragging_token) {
        int grid_x, grid_y;
        grid_screen_to_grid_direct(event->motion.x, event->motion.y,
                                   cam_x, cam_y, zoom,
                                   map->grid_size, map->grid_offset_x, map->grid_offset_y,
                                   &grid_x, &grid_y);
        if (input->dragged_token_index >= 0 && input->dragged_token_index < tokens->count) {
            tokens->tokens[input->dragged_token_index].grid_x = grid_x;
            tokens->tokens[input->dragged_token_index].grid_y = grid_y;
        }
    }
    
    if (event->type == SDL_EVENT_MOUSE_MOTION && input->painting_fog) {
        int grid_x, grid_y;
        grid_screen_to_grid_direct(event->motion.x, event->motion.y,
                                   cam_x, cam_y, zoom,
                                   map->grid_size, map->grid_offset_x, map->grid_offset_y,
                                   &grid_x, &grid_y);
        // Apply fog based on paint mode (add fog or remove fog)
        fog_set_cell(fog, grid_x, grid_y, !input->fog_paint_mode);
    }
    
    // Right-click in draw mode to delete shapes
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_RIGHT) {
        if (input->current_tool == TOOL_DRAW) {
            int world_x = (int)(event->button.x / zoom + cam_x);
            int world_y = (int)(event->button.y / zoom + cam_y);
            
            // Check drawings in reverse order (top to bottom)
            for (int i = g_drawings.count - 1; i >= 0; i--) {
                if (drawing_contains_point(&g_drawings.drawings[i], world_x, world_y)) {
                    drawing_remove(&g_drawings, i);
                    break;  // Only delete one at a time
                }
            }
        }
    }
}

void input_handle_camera(Camera *camera, InputState *input, SDL_Event *event,
                         int map_width, int map_height, SDL_Window *window) {
    int win_width, win_height;
    SDL_GetWindowSize(window, &win_width, &win_height);
    camera->view_width = win_width;
    camera->view_height = win_height;
    
    switch (event->type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event->button.button == SDL_BUTTON_RIGHT) {
            input->mouse_right_down = true;
            input->last_mouse_x = event->button.x;
            input->last_mouse_y = event->button.y;
        }
        break;
        
        case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event->button.button == SDL_BUTTON_RIGHT) {
            input->mouse_right_down = false;
        }
        break;
        
        case SDL_EVENT_MOUSE_MOTION:
        if (input->mouse_right_down) {
            int delta_x = event->motion.x - input->last_mouse_x;
            int delta_y = event->motion.y - input->last_mouse_y;
            camera_pan(camera, delta_x, delta_y);
            input->last_mouse_x = event->motion.x;
            input->last_mouse_y = event->motion.y;
        }
        break;
        
        case SDL_EVENT_MOUSE_WHEEL: {
            float factor = (event->wheel.y > 0) ? (1.0f + ZOOM_SPEED) : (1.0f / (1.0f + ZOOM_SPEED));
            camera_zoom_toward(camera, event->wheel.x, event->wheel.y, factor, map_width, map_height);
            break;
        }
    }
}

void input_handle_keyboard(Camera *camera, InputState *input, Map *map, TokenManager *tokens, FogOfWar *fog,
                           WindowManager *wm, SDL_Event *event) {
    if (event->type == SDL_EVENT_KEY_DOWN) {
        input->shift_pressed = (event->key.mod & SDL_KMOD_SHIFT) != 0;
        input->ctrl_pressed = (event->key.mod & SDL_KMOD_CTRL) != 0;
        
        // Handle damage input mode
        if (input->damage_input_mode) {
            if (event->key.key == SDLK_RETURN) {
                // Confirm and apply damage
                if (input->damage_input_len > 0) {
                    int damage_change = atoi(input->damage_input_buffer);
                    if (input->shift_pressed) {
                        damage_change = -damage_change;  // Heal
                    }
                    
                    for (int i = 0; i < tokens->count; i++) {
                        if (tokens->tokens[i].selected) {
                            tokens->tokens[i].damage += damage_change;
                            if (tokens->tokens[i].damage < 0) {
                                tokens->tokens[i].damage = 0;
                            }
    //                             SDL_Log("Token %s damage: %d (%s%d)", 
    //                                 tokens->tokens[i].name, 
    //                                 tokens->tokens[i].damage,
    //                                 input->shift_pressed ? "-" : "+",
    //                                 abs(damage_change));
                            break;
                        }
                    }
                }
                
                // Exit damage input mode
                input->damage_input_mode = false;
                input->damage_input_buffer[0] = '\0';
                input->damage_input_len = 0;
                return;
            }
            else if (event->key.key == SDLK_ESCAPE) {
                // Cancel damage input
                input->damage_input_mode = false;
                input->damage_input_buffer[0] = '\0';
                input->damage_input_len = 0;
    //                 SDL_Log("Damage input cancelled");
                return;
            }
            else if (event->key.key == SDLK_BACKSPACE && input->damage_input_len > 0) {
                // Remove last character
                input->damage_input_len--;
                input->damage_input_buffer[input->damage_input_len] = '\0';
                return;
            }
            else if (event->key.key >= SDLK_0 && event->key.key <= SDLK_9) {
                // Add digit to buffer
                if (input->damage_input_len < 15) {
                    input->damage_input_buffer[input->damage_input_len] = '0' + (event->key.key - SDLK_0);
                    input->damage_input_len++;
                    input->damage_input_buffer[input->damage_input_len] = '\0';
                }
                return;
            }
            else if (event->key.key >= SDLK_KP_0 && event->key.key <= SDLK_KP_9) {
                // Add numpad digit to buffer
                if (input->damage_input_len < 15) {
                    input->damage_input_buffer[input->damage_input_len] = '0' + (event->key.key - SDLK_KP_0);
                    input->damage_input_len++;
                    input->damage_input_buffer[input->damage_input_len] = '\0';
                }
                return;
            }
            // Ignore other keys in damage input mode (except shift for toggling)
            return;
        }
        
        // Enter key - start damage input mode if token is selected
        if (event->key.key == SDLK_RETURN && !input->damage_input_mode) {
            // Check if any token is selected
            bool has_selection = false;
            for (int i = 0; i < tokens->count; i++) {
                if (tokens->tokens[i].selected) {
                    has_selection = true;
                    break;
                }
            }
            
            if (has_selection) {
                input->damage_input_mode = true;
                input->damage_input_buffer[0] = '\0';
                input->damage_input_len = 0;
    //                 SDL_Log("Damage input mode started (Shift = heal)");
                return;
            }
        }
        
        // Opacity control with D key
        if (event->key.key == SDLK_D) {
            if (input->shift_pressed) {
                // Shift+D: Reset all token opacities
                for (int i = 0; i < tokens->count; i++) {
                    tokens->tokens[i].opacity = 255;
                }
    //                 SDL_Log("All token opacities reset to fully opaque");
            } else {
                // D: Toggle selected token opacity (downed/normal)
                for (int i = 0; i < tokens->count; i++) {
                    if (tokens->tokens[i].selected) {
                        if (tokens->tokens[i].opacity == 255) {
                            tokens->tokens[i].opacity = 128;
    //                             SDL_Log("Token %s marked as downed (50%% opacity)", tokens->tokens[i].name);
                        } else {
                            tokens->tokens[i].opacity = 255;
    //                             SDL_Log("Token %s restored to normal (100%% opacity)", tokens->tokens[i].name);
                        }
                        break;
                    }
                }
            }
        }
        
        // Condition wheel with A key
        if (event->key.key == SDLK_A && !input->condition_wheel_open) {
            // Find selected token
            for (int i = 0; i < tokens->count; i++) {
                if (tokens->tokens[i].selected) {
                    input->condition_wheel_open = true;
                    input->condition_wheel_token_index = i;
    //                     SDL_Log("Condition wheel opened for token: %s", tokens->tokens[i].name);
                    break;
                }
            }
        }
        
        // Calibration mode handling
        if (g_grid_calibration.active) {
            if (event->key.key == SDLK_RETURN) {
                grid_calibration_apply(&g_grid_calibration, map, fog);
            } else if (event->key.key == SDLK_ESCAPE) {
                g_grid_calibration.active = false;
    //                 SDL_Log("Grid calibration cancelled");
            }
            // Arrow keys adjust cell count while dragging
            else if (g_grid_calibration.dragging) {
                if (event->key.key == SDLK_UP && g_grid_calibration.cells_tall < 10) {
                    g_grid_calibration.cells_tall++;
    //                     SDL_Log("Cell count: %dx%d", g_grid_calibration.cells_wide, g_grid_calibration.cells_tall);
                } else if (event->key.key == SDLK_DOWN && g_grid_calibration.cells_tall > 1) {
                    g_grid_calibration.cells_tall--;
    //                     SDL_Log("Cell count: %dx%d", g_grid_calibration.cells_wide, g_grid_calibration.cells_tall);
                } else if (event->key.key == SDLK_RIGHT && g_grid_calibration.cells_wide < 10) {
                    g_grid_calibration.cells_wide++;
    //                     SDL_Log("Cell count: %dx%d", g_grid_calibration.cells_wide, g_grid_calibration.cells_tall);
                } else if (event->key.key == SDLK_LEFT && g_grid_calibration.cells_wide > 1) {
                    g_grid_calibration.cells_wide--;
    //                     SDL_Log("Cell count: %dx%d", g_grid_calibration.cells_wide, g_grid_calibration.cells_tall);
                }
            }
            return;  // Don't process other keys in calibration mode
        }
        
        // Tool selection (NOT allowed when token is selected - prevents accidental damage when switching modes)
        bool has_selected_token = false;
        for (int i = 0; i < tokens->count; i++) {
            if (tokens->tokens[i].selected) {
                has_selected_token = true;
                break;
            }
        }
        
        // Allow tool switching only if not in damage input mode AND no token is selected
        bool allow_tool_switch = !input->damage_input_mode && !has_selected_token;
        
        if (allow_tool_switch) {
            if (event->key.key == SDLK_1) input->current_tool = TOOL_SELECT;
            if (event->key.key == SDLK_2) input->current_tool = TOOL_FOG;
            if (event->key.key == SDLK_3) input->current_tool = TOOL_SQUAD;
            if (event->key.key == SDLK_4) input->current_tool = TOOL_DRAW;
        }
        
        // Color cycling for squad/draw modes (Q and E keys)
        if (input->current_tool == TOOL_SQUAD || input->current_tool == TOOL_DRAW) {
            if (event->key.key == SDLK_Q) {
                input->current_squad = (input->current_squad - 1 + SQUAD_COLOR_COUNT) % SQUAD_COLOR_COUNT;
    //                 SDL_Log("Color: %s", SQUAD_COLORS[input->current_squad].name);
            }
            if (event->key.key == SDLK_E) {
                input->current_squad = (input->current_squad + 1) % SQUAD_COLOR_COUNT;
    //                 SDL_Log("Color: %s", SQUAD_COLORS[input->current_squad].name);
            }
        }
        
        // Shape cycling in draw mode (W key to cycle shapes)
        if (input->current_tool == TOOL_DRAW) {
            if (event->key.key == SDLK_W) {
                input->current_shape = (input->current_shape == SHAPE_RECTANGLE) ? SHAPE_CIRCLE : SHAPE_RECTANGLE;
    //             const char *shape_name = (input->current_shape == SHAPE_RECTANGLE) ? "Rectangle" : "Circle";
    //             SDL_Log("Shape: %s", shape_name);
            }
            if (event->key.key == SDLK_X) {
                drawing_clear_all(&g_drawings);
            }
        }
        
        // Delete selected token
        if (event->key.key == SDLK_DELETE || event->key.key == SDLK_BACKSPACE) {
            for (int i = 0; i < tokens->count; i++) {
                if (tokens->tokens[i].selected) {
                    token_remove(tokens, i);
                    break;  // Only delete one token at a time
                }
            }
        }
        
        // Toggle hidden flag on selected token
        if (event->key.key == SDLK_H) {
            for (int i = 0; i < tokens->count; i++) {
                if (tokens->tokens[i].selected) {
                    token_toggle_hidden(tokens, i);
                    break;
                }
            }
        }
        
        // Map cycling
        if (event->key.key == SDLK_M) {
            if (input->shift_pressed) {
                map_library_prev(&g_map_library);
            } else {
                map_library_next(&g_map_library);
            }
            
            const char *map_path = map_library_get_current(&g_map_library);
            if (map_path) {
                map_destroy(map);
                if (map_load(map, map_path, wm->dm_window.renderer, wm->player_window.renderer)) {
                    // Reinitialize fog for new map
                    fog_destroy(fog);
                    fog_init(fog, map->grid_cols, map->grid_rows);
    //                     SDL_Log("Loaded map: %s", map->name);
                }
            }
        }
        
        // Grid calibration mode
        if (event->key.key == SDLK_C && !g_grid_calibration.active) {
            grid_calibration_start(&g_grid_calibration);
        }
        
        // Grid toggle
        if (event->key.key == SDLK_G) {
            input->show_grid = !input->show_grid;
    //             SDL_Log("Grid overlay: %s", input->show_grid ? "ON" : "OFF");
        }
        
        // View sync toggle
        if (event->key.key == SDLK_P) {
            g_sync_views = !g_sync_views;
    //             SDL_Log("View sync: %s", g_sync_views ? "ON" : "OFF");
        }
        
        // Token resizing with +/-
        if (event->key.key == SDLK_EQUALS || event->key.key == SDLK_KP_PLUS) {
            for (int i = 0; i < tokens->count; i++) {
                if (tokens->tokens[i].selected) {
                    token_resize(tokens, i, +1);
                    break;
                }
            }
        }
        if (event->key.key == SDLK_MINUS || event->key.key == SDLK_KP_MINUS) {
            for (int i = 0; i < tokens->count; i++) {
                if (tokens->tokens[i].selected) {
                    token_resize(tokens, i, -1);
                    break;
                }
            }
        }
        
        // Damage tracking with number keys 1-9 and 0
        // 1-9: Add damage, Shift+1-9: Heal (subtract damage)
        // 0: Add 10 damage, Shift+0: Heal 10
        if (event->key.key >= SDLK_1 && event->key.key <= SDLK_9) {
            int damage_change = (event->key.key - SDLK_0);
            if (input->shift_pressed) {
                damage_change = -damage_change;  // Heal
            }
            
            for (int i = 0; i < tokens->count; i++) {
                if (tokens->tokens[i].selected) {
                    tokens->tokens[i].damage += damage_change;
                    if (tokens->tokens[i].damage < 0) {
                        tokens->tokens[i].damage = 0;  // Can't heal below 0
                    }
    //                     SDL_Log("Token %s damage: %d", tokens->tokens[i].name, tokens->tokens[i].damage);
                    break;
                }
            }
        }
        
        if (event->key.key == SDLK_0) {
            int damage_change = 10;
            if (input->shift_pressed) {
                damage_change = -10;  // Heal
            }
            
            for (int i = 0; i < tokens->count; i++) {
                if (tokens->tokens[i].selected) {
                    tokens->tokens[i].damage += damage_change;
                    if (tokens->tokens[i].damage < 0) {
                        tokens->tokens[i].damage = 0;  // Can't heal below 0
                    }
    //                     SDL_Log("Token %s damage: %d", tokens->tokens[i].name, tokens->tokens[i].damage);
                    break;
                }
            }
        }
        
        // Close condition wheel with ESC
        if (event->key.key == SDLK_ESCAPE && input->condition_wheel_open) {
            input->condition_wheel_open = false;
            input->condition_wheel_token_index = -1;
    //             SDL_Log("Condition wheel closed");
            return;  // Don't process other ESC actions
        }
        
        // Deselect all (only if not in damage input mode - that's handled above)
        if (event->key.key == SDLK_ESCAPE && !input->damage_input_mode) {
            token_deselect_all(tokens);
        }
        
        // F1-F12 save/load
        if (event->key.key >= SDLK_F1 && event->key.key <= SDLK_F12) {
            int slot = (event->key.key - SDLK_F1) + 1;
            if (input->shift_pressed) {
                save_to_slot(slot, map, tokens, fog, input, camera);
            } else {
                load_from_slot(slot, map, tokens, fog, input,
                               wm->dm_window.renderer, wm->player_window.renderer, camera);
            }
        }
    }
    
    if (event->type == SDL_EVENT_KEY_UP) {
        input->shift_pressed = (event->key.mod & SDL_KMOD_SHIFT) != 0;
        input->ctrl_pressed = (event->key.mod & SDL_KMOD_CTRL) != 0;
    }
}

bool input_handle_events(WindowManager *wm, InputState *input, TokenManager *tokens,
                         FogOfWar *fog, Map *map, Camera *camera) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
            return false;
            
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
                WindowState *win = window_manager_get_window_by_id(wm, event.window.windowID);
                if (win) {
                    win->is_open = false;
                    if (!wm->dm_window.is_open && !wm->player_window.is_open) {
                        return false;
                    }
                }
                break;
            }
            
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            case SDL_EVENT_MOUSE_MOTION: {
                WindowState *win = window_manager_get_window_by_id(wm, event.button.windowID);
                if (win && win->type == WINDOW_TYPE_DM) {
                    input_handle_camera(camera, input, &event, map->width, map->height, win->window);
                    input_handle_dm_mouse(camera, input, tokens, fog, map, &event);
                }
                break;
            }
            
            case SDL_EVENT_MOUSE_WHEEL: {
                WindowState *win = window_manager_get_window_by_id(wm, event.wheel.windowID);
                if (win && win->type == WINDOW_TYPE_DM) {
                    input_handle_camera(camera, input, &event, map->width, map->height, win->window);
                }
                break;
            }
            
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
            input_handle_keyboard(camera, input, map, tokens, fog, wm, &event);
            break;
            
            case SDL_EVENT_DROP_FILE: {
                WindowState *win = window_manager_get_window_by_id(wm, event.drop.windowID);
                if (win && win->type == WINDOW_TYPE_DM) {
                    const char *dropped_file = event.drop.data;
                    if (is_image_file(dropped_file)) {
                        float mouse_x, mouse_y;
                        SDL_GetMouseState(&mouse_x, &mouse_y);
                        int grid_x, grid_y;
                        float cam_x = camera->x;
                        float cam_y = camera->y;
                        float zoom = camera->zoom;
                        grid_screen_to_grid_direct(mouse_x, mouse_y,
                                                   cam_x, cam_y, zoom,
                                                   map->grid_size,
                                                   map->grid_offset_x, map->grid_offset_y,
                                                   &grid_x, &grid_y);
                        char token_name[64];
                        char rel_path[256];
                        strncpy(rel_path, dropped_file, sizeof(rel_path) - 1);
                        make_path_relative(rel_path, sizeof(rel_path));
                        normalize_path_slashes(rel_path);
                        extract_filename(rel_path, token_name, sizeof(token_name));
                        token_add(tokens, rel_path, grid_x, grid_y,
                                  wm->dm_window.renderer, wm->player_window.renderer,
                                  token_name);
    //                         SDL_Log("Token added: %s at grid (%d, %d)", token_name, grid_x, grid_y);
                    }
                }
                break;
            }
        }
    }
    return true;
}

// ============================================================================
// MAIN LOOP
// ============================================================================

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    if (!window_manager_init(&g_wm)) {
        return 1;
    }
    
    // Load font for damage numbers
    if (!font_load("font.ttf")) {
    //         SDL_Log("Warning: Could not load font.ttf - damage numbers will not be displayed");
    }
    
    // Initialize asset libraries
    map_library_init(&g_map_library);
    token_library_init(&g_token_library);
    grid_calibration_init(&g_grid_calibration);
    
    // Initialize map cache and preload all maps
    map_cache_init(&g_map_cache, &g_map_library);
    map_cache_preload(&g_map_cache, &g_map_library);
    
    map_init(&g_map);
    token_manager_init(&g_tokens);
    drawing_manager_init(&g_drawings);
    input_init(&g_input);
    
    // Load first map from library if available, otherwise try default
    const char *first_map = map_library_get_current(&g_map_library);
    if (first_map) {
        if (!map_load(&g_map, first_map, g_wm.dm_window.renderer, g_wm.player_window.renderer)) {
    //             SDL_Log("Failed to load map from library: %s", first_map);
        }
    } else {
        // Try to load a default map if it exists (supports PNG, JPEG, BMP, etc.)
        if (!map_load(&g_map, "assets/maps/test_map.png", g_wm.dm_window.renderer, g_wm.player_window.renderer)) {
            // Try JPEG if PNG doesn't exist
            if (!map_load(&g_map, "assets/maps/test_map.jpg", g_wm.dm_window.renderer, g_wm.player_window.renderer)) {
                map_load(&g_map, "assets/maps/test_map.jpeg", g_wm.dm_window.renderer, g_wm.player_window.renderer);
            }
        }
    }
    
    // Initialize fog of war
    if (g_map.grid_cols > 0 && g_map.grid_rows > 0) {
        fog_init(&g_fog, g_map.grid_cols, g_map.grid_rows);
    }
    
    // Initialize cameras after map is loaded
    camera_init(&g_dm_camera, g_map.width, g_map.height, WINDOW_DM_WIDTH, WINDOW_DM_HEIGHT);
    
    int player_win_width, player_win_height;
    SDL_GetWindowSize(g_wm.player_window.window, &player_win_width, &player_win_height);
    camera_init(&g_player_camera, g_map.width, g_map.height, player_win_width, player_win_height);
    
    // Try to load a default token if it exists (supports PNG, JPEG, BMP, etc.)
    if (!token_add(&g_tokens, "assets/tokens/warrior.png", 5, 5,
                   g_wm.dm_window.renderer, g_wm.player_window.renderer, "Warrior")) {
        // Try JPEG if PNG doesn't exist
        if (!token_add(&g_tokens, "assets/tokens/warrior.jpg", 5, 5,
                       g_wm.dm_window.renderer, g_wm.player_window.renderer, "Warrior")) {
            token_add(&g_tokens, "assets/tokens/warrior.jpeg", 5, 5,
                      g_wm.dm_window.renderer, g_wm.player_window.renderer, "Warrior");
        }
    }
    
    SDL_Log("VTT started. Controls:");
    SDL_Log("  1 - Select tool, 2 - Fog tool, 3 - Squad assignment tool, 4 - Draw tool");
    SDL_Log("      (Note: Cannot switch tools when a token is selected - deselect first)");
    SDL_Log("  Left click - Select/move tokens, toggle fog, assign squad, or draw shapes");
    SDL_Log("  Right click - Pan camera (drag) / Delete drawing (in draw mode)");
    SDL_Log("  Mouse Wheel - Zoom in/out at cursor");
    SDL_Log("  W - Cycle shape (in draw mode)");
    SDL_Log("  Q/E - Cycle colors (in squad/draw mode)");
    SDL_Log("  A - Open condition wheel for selected token");
    SDL_Log("  D - Toggle token opacity (50%% downed / 100%% normal)");
    SDL_Log("  SHIFT+D - Reset all token opacities to 100%%");
    SDL_Log("  X - Clear all drawings (in draw mode)");
    SDL_Log("  P - Toggle player view sync to DM view");
    SDL_Log("  M - Cycle to next map, SHIFT+M - Previous map");
    SDL_Log("  CTRL+C - Enter grid calibration mode");
    SDL_Log("  G - Toggle grid overlay");
    SDL_Log("  H - Toggle selected token hidden/visible");
    SDL_Log("  +/- - Resize selected token");
    SDL_Log("  1-9 - Add damage to selected token");
    SDL_Log("  SHIFT+1-9 - Heal (subtract damage) from selected token");
    SDL_Log("  0 - Add 10 damage to selected token");
    SDL_Log("  SHIFT+0 - Heal 10 damage from selected token");
    SDL_Log("  ENTER - Type multi-digit damage (e.g. ENTER > 15 > ENTER adds 15 damage)");
    SDL_Log("          Hold SHIFT while typing to heal instead");
    SDL_Log("  DELETE/BACKSPACE - Remove selected token");
    SDL_Log("  Drag & Drop - Drop image files onto DM window to add tokens");
    SDL_Log("  SHIFT+F1-F12 - Save to slot");
    SDL_Log("  F1-F12 - Load from slot");
    SDL_Log("  ESC - Deselect all / Cancel damage input / Close condition wheel");
    
    bool running = true;
    while (running) {
        running = input_handle_events(&g_wm, &g_input, &g_tokens, &g_fog, &g_map, &g_dm_camera);
        
        // Update camera view sizes
        int dm_width, dm_height;
        SDL_GetWindowSize(g_wm.dm_window.window, &dm_width, &dm_height);
        g_dm_camera.view_width = dm_width;
        g_dm_camera.view_height = dm_height;
        
        int player_width, player_height;
        SDL_GetWindowSize(g_wm.player_window.window, &player_width, &player_height);
        g_player_camera.view_width = player_width;
        g_player_camera.view_height = player_height;
        
        // Update cameras
        camera_update(&g_dm_camera);
        
        // Sync views if enabled
        if (g_sync_views) {
            camera_sync(&g_player_camera, &g_dm_camera, g_map.width, g_map.height);
        }
        camera_update(&g_player_camera);
        
        if (g_wm.dm_window.is_open) {
            renderer_render_dm(&g_wm.dm_window, &g_map, &g_tokens, &g_fog, &g_input, &g_dm_camera);
        }
        
        if (g_wm.player_window.is_open) {
            renderer_render_player(&g_wm.player_window, &g_map, &g_tokens, &g_fog, &g_player_camera);
        }
        
        SDL_Delay(16);
    }
    
    fog_destroy(&g_fog);
    token_manager_destroy(&g_tokens);
    map_destroy(&g_map);
    map_cache_destroy(&g_map_cache);
    font_cleanup();
    window_manager_destroy(&g_wm);
    
    return 0;
}
