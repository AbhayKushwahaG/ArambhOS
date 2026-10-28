#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>
#include <stddef.h>

// Screen Dimensions (Dynamic: default 1024x768, fallback 800x600 or 320x200)
extern int g_width;
extern int g_height;
extern int g_bpp;
extern uint32_t *g_fb;
extern uint32_t *g_backbuffer;

// Color Macros (32-bit ARGB TrueColor: 0xAARRGGBB)
#define COLOR_RGB(r, g, b)       (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define COLOR_ARGB(a, r, g, b)   (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define COLOR_GET_R(c)           (((c) >> 16) & 0xFF)
#define COLOR_GET_G(c)           (((c) >> 8) & 0xFF)
#define COLOR_GET_B(c)           ((c) & 0xFF)

// Standard Palette Constants
#define COLOR_BLACK              COLOR_RGB(0, 0, 0)
#define COLOR_WHITE              COLOR_RGB(255, 255, 255)
#define COLOR_DARK_GRAY          COLOR_RGB(64, 64, 64)
#define COLOR_MID_GRAY           COLOR_RGB(160, 160, 160)
#define COLOR_LIGHT_GRAY         COLOR_RGB(212, 208, 200)
#define COLOR_VERY_LIGHT_GRAY    COLOR_RGB(240, 240, 240)
#define COLOR_BLUE               COLOR_RGB(0, 85, 234)
#define COLOR_NAVY               COLOR_RGB(10, 36, 106)
#define COLOR_RED                COLOR_RGB(220, 30, 30)
#define COLOR_GREEN              COLOR_RGB(34, 160, 40)
#define COLOR_LIGHT_GREEN        COLOR_RGB(80, 220, 80)
#define COLOR_YELLOW             COLOR_RGB(255, 215, 0)
#define COLOR_CYAN               COLOR_RGB(0, 200, 220)
#define COLOR_MAGENTA            COLOR_RGB(200, 40, 200)
#define COLOR_ORANGE             COLOR_RGB(255, 140, 0)

// Windows XP Luna Blue & UI Elements
#define COLOR_XP_LUNA_BLUE_TOP   COLOR_RGB(0, 88, 238)
#define COLOR_XP_LUNA_BLUE_BOT   COLOR_RGB(53, 147, 255)
#define COLOR_XP_LUNA_TITLE_ACT  COLOR_RGB(0, 85, 234)
#define COLOR_XP_LUNA_TITLE_INACT COLOR_RGB(120, 135, 155)
#define COLOR_XP_LUNA_INACT_L    COLOR_RGB(160, 175, 195)
#define COLOR_XP_LUNA_BORDER     COLOR_RGB(0, 85, 234)
#define COLOR_XP_LUNA_BG         COLOR_RGB(236, 233, 216)
#define COLOR_XP_CLOSE_RED_TOP   COLOR_RGB(230, 70, 45)
#define COLOR_XP_CLOSE_RED_BOT   COLOR_RGB(180, 25, 15)
#define COLOR_XP_TASKBAR_TOP     COLOR_RGB(36, 96, 216)
#define COLOR_XP_TASKBAR_BOT     COLOR_RGB(16, 52, 146)
#define COLOR_XP_START_TOP       COLOR_RGB(60, 180, 70)
#define COLOR_XP_START_BOT       COLOR_RGB(28, 125, 42)

// Fedora GNOME Slate Colors
#define COLOR_GNOME_PANEL        COLOR_RGB(38, 43, 49)
#define COLOR_GNOME_DARK         COLOR_RGB(28, 32, 37)
#define COLOR_GNOME_BORDER       COLOR_RGB(55, 61, 69)
#define COLOR_GNOME_PILL         COLOR_RGB(50, 56, 64)
#define COLOR_GNOME_PILL_HOVER   COLOR_RGB(68, 76, 86)
#define COLOR_GNOME_BLUE         COLOR_RGB(53, 132, 228)

// 3D Bevel UI Colors
#define COLOR_3D_FACE            COLOR_RGB(212, 208, 200)
#define COLOR_3D_LIGHT           COLOR_RGB(255, 255, 255)
#define COLOR_3D_DARK            COLOR_RGB(128, 128, 128)
#define COLOR_3D_SHADOW          COLOR_RGB(64, 64, 64)

// Built-in Icon IDs
enum {
    ICON_COMPUTER = 0,
    ICON_EXPLORER,
    ICON_NOTEPAD,
    ICON_PAINT,
    ICON_SNAKE,
    ICON_SETTINGS,
    ICON_TERMINAL,
    ICON_SCRIPT,
    ICON_PACKAGE,
    ICON_USER,
    ICON_POWER,
    ICON_CLOSE,
    ICON_MINIMIZE,
    ICON_MAXIMIZE,
    ICON_COUNT
};

// Graphics Subsystem API
int  gfx_init(int preferred_w, int preferred_h);
void gfx_exit(void);
void gfx_flip(void);

// Clipping Management
void gfx_set_clip(int x0, int y0, int x1, int y1);
void gfx_reset_clip(void);
void gfx_get_clip(int *x0, int *y0, int *x1, int *y1);

// Drawing Primitives
void gfx_put_pixel(int x, int y, uint32_t color);
uint32_t gfx_get_pixel(int x, int y);
void gfx_fill_rect(int x, int y, int w, int h, uint32_t color);
void gfx_draw_rect(int x, int y, int w, int h, uint32_t color);
void gfx_draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void gfx_gradient_v(int x, int y, int w, int h, uint32_t col_top, uint32_t col_bot);
void gfx_gradient_h(int x, int y, int w, int h, uint32_t col_left, uint32_t col_right);
void gfx_draw_rect_3d(int x, int y, int w, int h, uint32_t col_light, uint32_t col_dark, int sunken);
void gfx_draw_rounded_rect(int x, int y, int w, int h, int r, uint32_t color, int fill);

// Typography & Text Rendering
void gfx_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg, int transparent);
void gfx_draw_text(int x, int y, const char *s, uint32_t fg, uint32_t bg);
void gfx_draw_text_transparent(int x, int y, const char *s, uint32_t fg);
void gfx_draw_text_scaled(int x, int y, const char *s, uint32_t fg, int scale);

// Icon & Image Rendering
void gfx_draw_icon(int x, int y, int icon_id, int scale);
int  gfx_load_bmp(const char *filename, int *out_w, int *out_h, uint32_t **out_pixels);
int  gfx_save_bmp(const char *filename, int w, int h, const uint32_t *pixels);

#endif // GRAPHICS_H
