#include "app_paint.h"
#include "graphics.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define CANVAS_W 480
#define CANVAS_H 320

enum {
    TOOL_PENCIL = 0,
    TOOL_BRUSH,
    TOOL_ERASER,
    TOOL_LINE,
    TOOL_RECT,
    TOOL_COUNT
};

static const uint32_t s_palette[16] = {
    COLOR_BLACK,
    COLOR_DARK_GRAY,
    COLOR_MID_GRAY,
    COLOR_LIGHT_GRAY,
    COLOR_WHITE,
    COLOR_RGB(160, 40, 40),   // Dark Red
    COLOR_RED,
    COLOR_ORANGE,
    COLOR_YELLOW,
    COLOR_RGB(40, 160, 40),   // Green
    COLOR_RGB(80, 220, 80),   // Lime
    COLOR_CYAN,
    COLOR_RGB(40, 100, 220),  // Light Blue
    COLOR_BLUE,
    COLOR_NAVY,
    COLOR_MAGENTA
};

typedef struct {
    char filename[64];
    uint32_t canvas[CANVAS_W * CANVAS_H];
    int current_tool;
    int current_color_idx;
    int brush_size;
    int last_draw_x;
    int last_draw_y;
    int start_drag_x;
    int start_drag_y;
    int is_drawing;
    char status_msg[64];
} paint_state_t;

static paint_state_t s_paint;

static void canvas_set_pixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < CANVAS_W && y >= 0 && y < CANVAS_H) {
        s_paint.canvas[y * CANVAS_W + x] = color;
    }
}

static void canvas_draw_brush(int x, int y, int size, uint32_t color) {
    int rad = size / 2;
    for (int dy = -rad; dy <= rad; dy++) {
        for (int dx = -rad; dx <= rad; dx++) {
            canvas_set_pixel(x + dx, y + dy, color);
        }
    }
}

static void canvas_draw_line(int x0, int y0, int x1, int y1, int size, uint32_t color) {
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        if (size <= 1) {
            canvas_set_pixel(x0, y0, color);
        } else {
            canvas_draw_brush(x0, y0, size, color);
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void paint_clear(void) {
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) {
        s_paint.canvas[i] = COLOR_WHITE;
    }
    snprintf(s_paint.status_msg, sizeof(s_paint.status_msg), "Canvas Cleared");
}

static void paint_on_paint(window_t *win) {
    int cx = win->content_x;
    int cy = win->content_y;
    int cw = win->content_w;
    int ch = win->content_h;

    // 1. Top Toolbar (Height: 34px)
    gfx_fill_rect(cx, cy, cw, 34, COLOR_3D_FACE);
    gfx_draw_line(cx, cy + 33, cx + cw, cy + 33, COLOR_3D_DARK);

    // Tools Buttons: [Pencil] [Brush] [Eraser] [Line] [Rect]
    const char *tool_labels[TOOL_COUNT] = {"Pen", "Brush", "Eraser", "Line", "Rect"};
    for (int t = 0; t < TOOL_COUNT; t++) {
        int tx = cx + 6 + t * 48;
        int is_sel = (s_paint.current_tool == t);
        gfx_fill_rect(tx, cy + 4, 44, 24, is_sel ? COLOR_WHITE : COLOR_3D_FACE);
        gfx_draw_rect_3d(tx, cy + 4, 44, 24, COLOR_3D_LIGHT, COLOR_3D_SHADOW, is_sel);
        gfx_draw_text_transparent(tx + 6, cy + 10, tool_labels[t], is_sel ? COLOR_BLUE : COLOR_BLACK);
    }

    // Brush Size Selector: [1px] [2px] [4px] [8px]
    int sx = cx + 256;
    gfx_draw_text_transparent(sx, cy + 11, "Size:", COLOR_BLACK);
    int sizes[4] = {1, 2, 4, 8};
    for (int s = 0; s < 4; s++) {
        int bx = sx + 38 + s * 24;
        int is_sel = (s_paint.brush_size == sizes[s]);
        gfx_fill_rect(bx, cy + 6, 20, 20, is_sel ? COLOR_WHITE : COLOR_3D_FACE);
        gfx_draw_rect_3d(bx, cy + 6, 20, 20, COLOR_3D_LIGHT, COLOR_3D_SHADOW, is_sel);
        // Draw dot representation of size
        int rad = (sizes[s] > 4) ? 4 : (sizes[s] / 2 + 1);
        gfx_fill_rect(bx + 10 - rad, cy + 16 - rad, rad * 2, rad * 2, COLOR_BLACK);
    }

    // Action Buttons: [Clear] [Save] [Load]
    int ax = cx + 368;
    // Clear
    gfx_fill_rect(ax, cy + 4, 48, 24, COLOR_3D_FACE);
    gfx_draw_rect_3d(ax, cy + 4, 48, 24, COLOR_3D_LIGHT, COLOR_3D_SHADOW, 0);
    gfx_draw_text_transparent(ax + 8, cy + 10, "Clear", COLOR_BLACK);

    // Save BMP
    gfx_fill_rect(ax + 52, cy + 4, 48, 24, COLOR_3D_FACE);
    gfx_draw_rect_3d(ax + 52, cy + 4, 48, 24, COLOR_3D_LIGHT, COLOR_3D_SHADOW, 0);
    gfx_draw_text_transparent(ax + 58, cy + 10, "Save", COLOR_BLACK);

    // Load BMP
    gfx_fill_rect(ax + 104, cy + 4, 48, 24, COLOR_3D_FACE);
    gfx_draw_rect_3d(ax + 104, cy + 4, 48, 24, COLOR_3D_LIGHT, COLOR_3D_SHADOW, 0);
    gfx_draw_text_transparent(ax + 110, cy + 10, "Load", COLOR_BLACK);

    // 2. Palette Bar (Left column: width 48px, y=cy+34..cy+ch-22)
    int pal_y = cy + 34;
    int pal_w = 48;
    gfx_fill_rect(cx, pal_y, pal_w, ch - 56, COLOR_3D_FACE);
    gfx_draw_line(cx + pal_w - 1, pal_y, cx + pal_w - 1, cy + ch - 22, COLOR_3D_DARK);

    // Active Color Indicator Box
    gfx_fill_rect(cx + 8, pal_y + 8, 32, 24, s_palette[s_paint.current_color_idx]);
    gfx_draw_rect_3d(cx + 8, pal_y + 8, 32, 24, COLOR_3D_SHADOW, COLOR_3D_LIGHT, 1);

    // Color Swatches (2 columns x 8 rows)
    for (int i = 0; i < 16; i++) {
        int col = i % 2;
        int row = i / 2;
        int px = cx + 8 + col * 16;
        int py = pal_y + 40 + row * 16;
        gfx_fill_rect(px, py, 14, 14, s_palette[i]);
        gfx_draw_rect(px, py, 14, 14, (i == s_paint.current_color_idx) ? COLOR_BLACK : COLOR_MID_GRAY);
    }

    // 3. Status Bar at bottom (Height: 22px)
    int stat_y = cy + ch - 22;
    gfx_fill_rect(cx, stat_y, cw, 22, COLOR_3D_FACE);
    gfx_draw_line(cx, stat_y, cx + cw, stat_y, COLOR_3D_SHADOW);
    gfx_draw_text_transparent(cx + 8, stat_y + 6, s_paint.status_msg, COLOR_BLACK);

    // 4. Canvas Drawing Area
    int cv_x = cx + pal_w + 4;
    int cv_y = pal_y + 4;
    int max_disp_w = (cw - pal_w - 8 < CANVAS_W) ? (cw - pal_w - 8) : CANVAS_W;
    int max_disp_h = (ch - 56 - 8 < CANVAS_H) ? (ch - 56 - 8) : CANVAS_H;

    // Canvas Border
    gfx_draw_rect_3d(cv_x - 1, cv_y - 1, max_disp_w + 2, max_disp_h + 2, COLOR_3D_SHADOW, COLOR_3D_LIGHT, 1);

    // Blit Canvas Pixels to Backbuffer
    for (int r = 0; r < max_disp_h; r++) {
        uint32_t *dst = &g_backbuffer[(cv_y + r) * g_width + cv_x];
        const uint32_t *src = &s_paint.canvas[r * CANVAS_W];
        for (int c = 0; c < max_disp_w; c++) {
            dst[c] = src[c];
        }
    }
}

static void paint_on_click(window_t *win, int cx, int cy, int btn) {
    (void)btn;
    // Top Toolbar Clicks (cy < 34)
    if (cy < 34) {
        // Tool buttons
        for (int t = 0; t < TOOL_COUNT; t++) {
            int tx = 6 + t * 48;
            if (cx >= tx && cx < tx + 44) {
                s_paint.current_tool = t;
                snprintf(s_paint.status_msg, sizeof(s_paint.status_msg), "Tool Selected: %d", t);
                return;
            }
        }
        // Size buttons
        int sx = 256;
        int sizes[4] = {1, 2, 4, 8};
        for (int s = 0; s < 4; s++) {
            int bx = sx + 38 + s * 24;
            if (cx >= bx && cx < bx + 20) {
                s_paint.brush_size = sizes[s];
                snprintf(s_paint.status_msg, sizeof(s_paint.status_msg), "Brush Size: %dpx", s_paint.brush_size);
                return;
            }
        }
        // Action buttons: Clear, Save, Load
        int ax = 368;
        if (cx >= ax && cx < ax + 48) { // Clear
            paint_clear();
        } else if (cx >= ax + 52 && cx < ax + 100) { // Save BMP
            if (gfx_save_bmp("paint.bmp", CANVAS_W, CANVAS_H, s_paint.canvas) == 0) {
                snprintf(s_paint.status_msg, sizeof(s_paint.status_msg), "Saved to paint.bmp in AarFS!");
            } else {
                snprintf(s_paint.status_msg, sizeof(s_paint.status_msg), "Save to paint.bmp failed.");
            }
        } else if (cx >= ax + 104 && cx < ax + 152) { // Load BMP
            int lw = 0, lh = 0;
            uint32_t *lpix = NULL;
            if (gfx_load_bmp("paint.bmp", &lw, &lh, &lpix) == 0 && lpix) {
                int copy_w = (lw < CANVAS_W) ? lw : CANVAS_W;
                int copy_h = (lh < CANVAS_H) ? lh : CANVAS_H;
                for (int r = 0; r < copy_h; r++) {
                    memcpy(&s_paint.canvas[r * CANVAS_W], &lpix[r * lw], copy_w * sizeof(uint32_t));
                }
                snprintf(s_paint.status_msg, sizeof(s_paint.status_msg), "Loaded paint.bmp (%dx%d)", lw, lh);
            } else {
                snprintf(s_paint.status_msg, sizeof(s_paint.status_msg), "paint.bmp not found in AarFS");
            }
        }
        return;
    }

    // Palette Bar Clicks (cx < 48)
    if (cx < 48) {
        int pal_y = 34;
        for (int i = 0; i < 16; i++) {
            int col = i % 2;
            int row = i / 2;
            int px = 8 + col * 16;
            int py = pal_y + 40 + row * 16;
            if (cx >= px && cx < px + 16 && cy >= py && cy < py + 16) {
                s_paint.current_color_idx = i;
                snprintf(s_paint.status_msg, sizeof(s_paint.status_msg), "Color %d selected", i);
                return;
            }
        }
        return;
    }

    // Canvas click
    int cv_x = 48 + 4;
    int cv_y = 34 + 4;
    int can_x = cx - cv_x;
    int can_y = cy - cv_y;

    if (can_x >= 0 && can_x < CANVAS_W && can_y >= 0 && can_y < CANVAS_H) {
        uint32_t col = (s_paint.current_tool == TOOL_ERASER) ? COLOR_WHITE : s_palette[s_paint.current_color_idx];

        if (s_paint.current_tool == TOOL_PENCIL || s_paint.current_tool == TOOL_BRUSH || s_paint.current_tool == TOOL_ERASER) {
            int sz = (s_paint.current_tool == TOOL_PENCIL) ? 1 : s_paint.brush_size;
            canvas_draw_brush(can_x, can_y, sz, col);
            s_paint.last_draw_x = can_x;
            s_paint.last_draw_y = can_y;
            s_paint.is_drawing = 1;
        } else if (s_paint.current_tool == TOOL_LINE || s_paint.current_tool == TOOL_RECT) {
            s_paint.start_drag_x = can_x;
            s_paint.start_drag_y = can_y;
            s_paint.is_drawing = 1;
        }
    }
}

static void paint_on_mouse_move(window_t *win, int cx, int cy, int is_down) {
    (void)win;
    if (!is_down) {
        s_paint.is_drawing = 0;
        return;
    }

    int cv_x = 48 + 4;
    int cv_y = 34 + 4;
    int can_x = cx - cv_x;
    int can_y = cy - cv_y;

    if (can_x < 0 || can_x >= CANVAS_W || can_y < 0 || can_y >= CANVAS_H) {
        return;
    }

    uint32_t col = (s_paint.current_tool == TOOL_ERASER) ? COLOR_WHITE : s_palette[s_paint.current_color_idx];

    if (s_paint.current_tool == TOOL_PENCIL || s_paint.current_tool == TOOL_BRUSH || s_paint.current_tool == TOOL_ERASER) {
        int sz = (s_paint.current_tool == TOOL_PENCIL) ? 1 : s_paint.brush_size;
        if (s_paint.is_drawing) {
            canvas_draw_line(s_paint.last_draw_x, s_paint.last_draw_y, can_x, can_y, sz, col);
        } else {
            canvas_draw_brush(can_x, can_y, sz, col);
            s_paint.is_drawing = 1;
        }
        s_paint.last_draw_x = can_x;
        s_paint.last_draw_y = can_y;
    }
}

window_t *app_paint_open(const char *filename) {
    paint_clear();
    s_paint.current_tool = TOOL_PENCIL;
    s_paint.current_color_idx = 0; // Black
    s_paint.brush_size = 2;
    s_paint.is_drawing = 0;

    if (filename && *filename) {
        strncpy(s_paint.filename, filename, sizeof(s_paint.filename) - 1);
        int lw = 0, lh = 0;
        uint32_t *lpix = NULL;
        if (gfx_load_bmp(filename, &lw, &lh, &lpix) == 0 && lpix) {
            int copy_w = (lw < CANVAS_W) ? lw : CANVAS_W;
            int copy_h = (lh < CANVAS_H) ? lh : CANVAS_H;
            for (int r = 0; r < copy_h; r++) {
                memcpy(&s_paint.canvas[r * CANVAS_W], &lpix[r * lw], copy_w * sizeof(uint32_t));
            }
            snprintf(s_paint.status_msg, sizeof(s_paint.status_msg), "Opened %s", filename);
        }
    } else {
        strncpy(s_paint.filename, "untitled.bmp", sizeof(s_paint.filename) - 1);
        snprintf(s_paint.status_msg, sizeof(s_paint.status_msg), "Ready");
    }

    window_t *win = wm_create_window("Aarambh Paint", 180, 90, 560, 410, WIN_FLAG_RESIZABLE);
    if (!win) return NULL;

    win->on_paint = paint_on_paint;
    win->on_click = paint_on_click;
    win->on_mouse_move = paint_on_mouse_move;

    return win;
}
