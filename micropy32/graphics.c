#include "graphics.h"
#include "font8x8.h"
#include "pci.h"
#include "auth.h"
#include <string.h>
#include <stdlib.h>

extern const kernel_api_t *g_kapi;

// BGA I/O Ports
#define BGA_INDEX_PORT 0x01CE
#define BGA_DATA_PORT  0x01CF

#define BGA_REG_ID          0
#define BGA_REG_XRES        1
#define BGA_REG_YRES        2
#define BGA_REG_BPP         3
#define BGA_REG_ENABLE      4
#define BGA_REG_BANK        5
#define BGA_REG_VIRT_WIDTH  6
#define BGA_REG_VIRT_HEIGHT 7
#define BGA_REG_X_OFFSET    8
#define BGA_REG_Y_OFFSET    9

#define BGA_DISABLED        0x00
#define BGA_ENABLED         0x01
#define BGA_LFB_ENABLED     0x40

int g_width = 1024;
int g_height = 768;
int g_bpp = 32;
uint32_t *g_fb = NULL;
uint32_t *g_backbuffer = (uint32_t *)0x00800000; // Dedicated 3-4 MiB RAM area (8 MiB address)

static int s_clip_x0 = 0;
static int s_clip_y0 = 0;
static int s_clip_x1 = 1024;
static int s_clip_y1 = 768;

static int s_is_bga = 0;

int gfx_init(int preferred_w, int preferred_h) {
    if (preferred_w <= 0) preferred_w = 1024;
    if (preferred_h <= 0) preferred_h = 768;

    // 1. Probe for Bochs / QEMU BGA
    outw(BGA_INDEX_PORT, BGA_REG_ID);
    uint16_t bga_id = inw(BGA_DATA_PORT);

    if (bga_id >= 0xB0C0 && bga_id <= 0xB0C6) {
        // Detected Bochs / QEMU BGA device!
        pci_device_t disp;
        uint32_t lfb_addr = 0xFD000000; // Standard QEMU default
        if (pci_find_display_device(&disp) && disp.bar[0]) {
            lfb_addr = disp.bar[0] & 0xFFFFFFF0;
        }

        // Program BGA registers
        outw(BGA_INDEX_PORT, BGA_REG_ENABLE);
        outw(BGA_DATA_PORT, BGA_DISABLED);

        outw(BGA_INDEX_PORT, BGA_REG_XRES);
        outw(BGA_DATA_PORT, preferred_w);

        outw(BGA_INDEX_PORT, BGA_REG_YRES);
        outw(BGA_DATA_PORT, preferred_h);

        outw(BGA_INDEX_PORT, BGA_REG_BPP);
        outw(BGA_DATA_PORT, 32);

        outw(BGA_INDEX_PORT, BGA_REG_ENABLE);
        outw(BGA_DATA_PORT, BGA_ENABLED | BGA_LFB_ENABLED);

        g_width = preferred_w;
        g_height = preferred_h;
        g_bpp = 32;
        g_fb = (uint32_t *)lfb_addr;
        g_backbuffer = (uint32_t *)0x00800000;
        s_is_bga = 1;

        gfx_reset_clip();
        gfx_fill_rect(0, 0, g_width, g_height, COLOR_BLACK);
        gfx_flip();
        return 1;
    }

    // 2. Hardware VBE via BIOS INT 10h (for real PCs like Dell Latitude)
    if (g_kapi && g_kapi->set_video_mode) {
        // Attempt VBE 1024x768x32bpp (Mode 0x4118) or 800x600x32bpp (0x4115)
        g_kapi->set_video_mode(0x4F02, 0x4118);
        g_width = 1024;
        g_height = 768;
        g_bpp = 32;
        pci_device_t disp;
        uint32_t lfb_addr = 0xE0000000;
        if (pci_find_display_device(&disp) && disp.bar[0]) {
            lfb_addr = disp.bar[0] & 0xFFFFFFF0;
        }
        g_fb = (uint32_t *)lfb_addr;
        g_backbuffer = (uint32_t *)0x00800000;
        s_is_bga = 0;
        gfx_reset_clip();
        return 1;
    }

    return 0;
}

void gfx_exit(void) {
    if (s_is_bga) {
        outw(BGA_INDEX_PORT, BGA_REG_ENABLE);
        outw(BGA_DATA_PORT, BGA_DISABLED);
    }
    if (g_kapi && g_kapi->set_video_mode) {
        g_kapi->set_video_mode(0x03, 0); // Restore 80x25 standard VGA text mode
    }
}

void gfx_flip(void) {
    if (!g_fb || !g_backbuffer) return;
    size_t dwords = (size_t)g_width * g_height;
    const uint32_t *src = g_backbuffer;
    uint32_t *dst = g_fb;

    __asm__ volatile (
        "cld\n\t"
        "rep movsl"
        : "+S"(src), "+D"(dst), "+c"(dwords)
        :
        : "memory"
    );
}

void gfx_set_clip(int x0, int y0, int x1, int y1) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > g_width) x1 = g_width;
    if (y1 > g_height) y1 = g_height;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    s_clip_x0 = x0;
    s_clip_y0 = y0;
    s_clip_x1 = x1;
    s_clip_y1 = y1;
}

void gfx_reset_clip(void) {
    s_clip_x0 = 0;
    s_clip_y0 = 0;
    s_clip_x1 = g_width;
    s_clip_y1 = g_height;
}

void gfx_get_clip(int *x0, int *y0, int *x1, int *y1) {
    if (x0) *x0 = s_clip_x0;
    if (y0) *y0 = s_clip_y0;
    if (x1) *x1 = s_clip_x1;
    if (y1) *y1 = s_clip_y1;
}

void gfx_put_pixel(int x, int y, uint32_t color) {
    if (x >= s_clip_x0 && x < s_clip_x1 && y >= s_clip_y0 && y < s_clip_y1) {
        g_backbuffer[y * g_width + x] = color;
    }
}

uint32_t gfx_get_pixel(int x, int y) {
    if (x >= 0 && x < g_width && y >= 0 && y < g_height) {
        return g_backbuffer[y * g_width + x];
    }
    return 0;
}

void gfx_fill_rect(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    int x2 = x + w;
    int y2 = y + h;

    if (x < s_clip_x0) x = s_clip_x0;
    if (y < s_clip_y0) y = s_clip_y0;
    if (x2 > s_clip_x1) x2 = s_clip_x1;
    if (y2 > s_clip_y1) y2 = s_clip_y1;

    if (x >= x2 || y >= y2) return;
    int rw = x2 - x;

    for (int r = y; r < y2; r++) {
        uint32_t *p = &g_backbuffer[r * g_width + x];
        for (int c = 0; c < rw; c++) {
            p[c] = color;
        }
    }
}

void gfx_draw_rect(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    gfx_fill_rect(x, y, w, 1, color);
    gfx_fill_rect(x, y + h - 1, w, 1, color);
    gfx_fill_rect(x, y, 1, h, color);
    gfx_fill_rect(x + w - 1, y, 1, h, color);
}

void gfx_draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        gfx_put_pixel(x0, y0, color);
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

void gfx_gradient_v(int x, int y, int w, int h, uint32_t col_top, uint32_t col_bot) {
    if (w <= 0 || h <= 0) return;
    int r1 = COLOR_GET_R(col_top), g1 = COLOR_GET_G(col_top), b1 = COLOR_GET_B(col_top);
    int r2 = COLOR_GET_R(col_bot), g2 = COLOR_GET_G(col_bot), b2 = COLOR_GET_B(col_bot);

    for (int row = 0; row < h; row++) {
        int py = y + row;
        if (py < s_clip_y0 || py >= s_clip_y1) continue;

        int r = r1 + ((r2 - r1) * row) / (h > 1 ? (h - 1) : 1);
        int g = g1 + ((g2 - g1) * row) / (h > 1 ? (h - 1) : 1);
        int b = b1 + ((b2 - b1) * row) / (h > 1 ? (h - 1) : 1);
        uint32_t col = COLOR_RGB(r, g, b);

        int cx0 = (x < s_clip_x0) ? s_clip_x0 : x;
        int cx1 = (x + w > s_clip_x1) ? s_clip_x1 : (x + w);
        if (cx1 > cx0) {
            uint32_t *p = &g_backbuffer[py * g_width + cx0];
            int count = cx1 - cx0;
            for (int i = 0; i < count; i++) p[i] = col;
        }
    }
}

void gfx_gradient_h(int x, int y, int w, int h, uint32_t col_left, uint32_t col_right) {
    if (w <= 0 || h <= 0) return;
    int r1 = COLOR_GET_R(col_left), g1 = COLOR_GET_G(col_left), b1 = COLOR_GET_B(col_left);
    int r2 = COLOR_GET_R(col_right), g2 = COLOR_GET_G(col_right), b2 = COLOR_GET_B(col_right);

    for (int col = 0; col < w; col++) {
        int px = x + col;
        if (px < s_clip_x0 || px >= s_clip_x1) continue;

        int r = r1 + ((r2 - r1) * col) / (w > 1 ? (w - 1) : 1);
        int g = g1 + ((g2 - g1) * col) / (w > 1 ? (w - 1) : 1);
        int b = b1 + ((b2 - b1) * col) / (w > 1 ? (w - 1) : 1);
        uint32_t c = COLOR_RGB(r, g, b);

        int cy0 = (y < s_clip_y0) ? s_clip_y0 : y;
        int cy1 = (y + h > s_clip_y1) ? s_clip_y1 : (y + h);
        for (int row = cy0; row < cy1; row++) {
            g_backbuffer[row * g_width + px] = c;
        }
    }
}

void gfx_draw_rect_3d(int x, int y, int w, int h, uint32_t col_light, uint32_t col_dark, int sunken) {
    uint32_t tl = sunken ? col_dark : col_light;
    uint32_t br = sunken ? col_light : col_dark;

    gfx_fill_rect(x, y, w, 1, tl);
    gfx_fill_rect(x, y, 1, h, tl);
    gfx_fill_rect(x, y + h - 1, w, 1, br);
    gfx_fill_rect(x + w - 1, y, 1, h, br);
}

void gfx_draw_rounded_rect(int x, int y, int w, int h, int r, uint32_t color, int fill) {
    if (fill) {
        gfx_fill_rect(x + r, y, w - 2 * r, h, color);
        gfx_fill_rect(x, y + r, r, h - 2 * r, color);
        gfx_fill_rect(x + w - r, y + r, r, h - 2 * r, color);
    } else {
        gfx_draw_rect(x, y, w, h, color);
    }
}

void gfx_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg, int transparent) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = font8x8[(int)c - 32];

    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (1 << col)) {
                gfx_put_pixel(x + col, y + row, fg);
            } else if (!transparent) {
                gfx_put_pixel(x + col, y + row, bg);
            }
        }
    }
}

void gfx_draw_text(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    if (!s) return;
    int cur_x = x;
    while (*s) {
        gfx_draw_char(cur_x, y, *s++, fg, bg, 0);
        cur_x += 8;
    }
}

void gfx_draw_text_transparent(int x, int y, const char *s, uint32_t fg) {
    if (!s) return;
    int cur_x = x;
    while (*s) {
        gfx_draw_char(cur_x, y, *s++, fg, 0, 1);
        cur_x += 8;
    }
}

void gfx_draw_text_scaled(int x, int y, const char *s, uint32_t fg, int scale) {
    if (!s) return;
    if (scale <= 1) {
        gfx_draw_text_transparent(x, y, s, fg);
        return;
    }

    int cur_x = x;
    while (*s) {
        char c = *s++;
        if (c < 32 || c > 126) c = '?';
        const uint8_t *glyph = font8x8[(int)c - 32];

        for (int row = 0; row < 8; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 8; col++) {
                if (bits & (1 << col)) {
                    gfx_fill_rect(cur_x + col * scale, y + row * scale, scale, scale, fg);
                }
            }
        }
        cur_x += 8 * scale;
    }
}

// Procedural Vector Icon Renderer
void gfx_draw_icon(int x, int y, int icon_id, int scale) {
    int s = (scale > 0) ? scale : 1;
    switch (icon_id) {
        case ICON_COMPUTER: // My Computer / System
            gfx_fill_rect(x + 2*s, y + 2*s, 12*s, 9*s, COLOR_LIGHT_GRAY);
            gfx_draw_rect(x + 2*s, y + 2*s, 12*s, 9*s, COLOR_DARK_GRAY);
            gfx_fill_rect(x + 4*s, y + 4*s, 8*s, 5*s, COLOR_BLUE); // screen
            gfx_fill_rect(x + 6*s, y + 11*s, 4*s, 2*s, COLOR_DARK_GRAY); // stand
            gfx_fill_rect(x + 3*s, y + 13*s, 10*s, 2*s, COLOR_LIGHT_GRAY); // base
            break;

        case ICON_EXPLORER: // File Explorer / Folder
            gfx_fill_rect(x + 1*s, y + 3*s, 6*s, 3*s, COLOR_ORANGE);
            gfx_fill_rect(x + 1*s, y + 5*s, 14*s, 9*s, COLOR_YELLOW);
            gfx_draw_rect(x + 1*s, y + 5*s, 14*s, 9*s, COLOR_ORANGE);
            gfx_fill_rect(x + 3*s, y + 7*s, 10*s, 5*s, COLOR_WHITE); // inner sheet
            break;

        case ICON_NOTEPAD: // Notepad document
            gfx_fill_rect(x + 2*s, y + 1*s, 11*s, 14*s, COLOR_WHITE);
            gfx_draw_rect(x + 2*s, y + 1*s, 11*s, 14*s, COLOR_DARK_GRAY);
            gfx_fill_rect(x + 4*s, y + 4*s, 7*s, 1*s, COLOR_BLUE);
            gfx_fill_rect(x + 4*s, y + 7*s, 7*s, 1*s, COLOR_MID_GRAY);
            gfx_fill_rect(x + 4*s, y + 10*s, 5*s, 1*s, COLOR_MID_GRAY);
            // Pencil across document
            gfx_draw_line(x + 11*s, y + 3*s, x + 7*s, y + 13*s, COLOR_YELLOW);
            break;

        case ICON_PAINT: // Paint Palette
            gfx_fill_rect(x + 2*s, y + 3*s, 12*s, 10*s, COLOR_LIGHT_GRAY);
            gfx_draw_rect(x + 2*s, y + 3*s, 12*s, 10*s, COLOR_DARK_GRAY);
            gfx_fill_rect(x + 4*s, y + 5*s, 2*s, 2*s, COLOR_RED);
            gfx_fill_rect(x + 7*s, y + 5*s, 2*s, 2*s, COLOR_BLUE);
            gfx_fill_rect(x + 10*s, y + 5*s, 2*s, 2*s, COLOR_GREEN);
            gfx_fill_rect(x + 5*s, y + 9*s, 2*s, 2*s, COLOR_YELLOW);
            break;

        case ICON_SNAKE: // Snake Game
            gfx_fill_rect(x + 1*s, y + 1*s, 14*s, 14*s, COLOR_BLACK);
            gfx_draw_rect(x + 1*s, y + 1*s, 14*s, 14*s, COLOR_GREEN);
            // Sinuous snake body
            gfx_fill_rect(x + 3*s, y + 4*s, 3*s, 3*s, COLOR_GREEN);
            gfx_fill_rect(x + 6*s, y + 4*s, 3*s, 3*s, COLOR_GREEN);
            gfx_fill_rect(x + 6*s, y + 7*s, 3*s, 3*s, COLOR_GREEN);
            gfx_fill_rect(x + 9*s, y + 7*s, 3*s, 3*s, COLOR_LIGHT_GREEN); // head
            gfx_fill_rect(x + 11*s, y + 11*s, 2*s, 2*s, COLOR_RED); // food
            break;

        case ICON_SETTINGS: // Gear / Control Panel
            gfx_fill_rect(x + 4*s, y + 4*s, 8*s, 8*s, COLOR_MID_GRAY);
            gfx_draw_rect(x + 4*s, y + 4*s, 8*s, 8*s, COLOR_DARK_GRAY);
            gfx_fill_rect(x + 6*s, y + 2*s, 4*s, 12*s, COLOR_MID_GRAY);
            gfx_fill_rect(x + 2*s, y + 6*s, 12*s, 4*s, COLOR_MID_GRAY);
            gfx_fill_rect(x + 6*s, y + 6*s, 4*s, 4*s, COLOR_WHITE); // inner hub
            break;

        case ICON_TERMINAL: // Command Prompt
            gfx_fill_rect(x + 1*s, y + 2*s, 14*s, 12*s, COLOR_BLACK);
            gfx_draw_rect(x + 1*s, y + 2*s, 14*s, 12*s, COLOR_MID_GRAY);
            // Prompt cursor > _
            gfx_draw_char(x + 3*s, y + 4*s, '>', COLOR_WHITE, 0, 1);
            gfx_fill_rect(x + 10*s, y + 9*s, 3*s, 1*s, COLOR_WHITE);
            break;

        case ICON_SCRIPT: // Shell Script (.sh)
            gfx_fill_rect(x + 2*s, y + 1*s, 11*s, 14*s, COLOR_LIGHT_GRAY);
            gfx_draw_rect(x + 2*s, y + 1*s, 11*s, 14*s, COLOR_DARK_GRAY);
            gfx_draw_char(x + 3*s, y + 3*s, '#', COLOR_GREEN, 0, 1);
            gfx_draw_char(x + 7*s, y + 3*s, '!', COLOR_GREEN, 0, 1);
            gfx_fill_rect(x + 4*s, y + 10*s, 7*s, 1*s, COLOR_BLUE);
            break;

        case ICON_PACKAGE: // Debian Package (.deb)
            gfx_fill_rect(x + 2*s, y + 3*s, 12*s, 10*s, COLOR_ORANGE);
            gfx_draw_rect(x + 2*s, y + 3*s, 12*s, 10*s, COLOR_RED);
            gfx_draw_line(x + 2*s, y + 3*s, x + 8*s, y + 8*s, COLOR_RED);
            gfx_draw_line(x + 14*s, y + 3*s, x + 8*s, y + 8*s, COLOR_RED);
            gfx_fill_rect(x + 7*s, y + 8*s, 2*s, 5*s, COLOR_RED);
            break;

        case ICON_USER: // User Avatar
            gfx_fill_rect(x + 5*s, y + 2*s, 6*s, 6*s, COLOR_BLUE); // head
            gfx_fill_rect(x + 2*s, y + 9*s, 12*s, 6*s, COLOR_NAVY); // shoulders
            break;

        case ICON_POWER: // Turn Off / Reboot
            gfx_draw_rect(x + 3*s, y + 3*s, 10*s, 10*s, COLOR_RED);
            gfx_fill_rect(x + 7*s, y + 1*s, 2*s, 6*s, COLOR_RED);
            break;

        case ICON_CLOSE: // Window [X]
            gfx_draw_line(x + 2*s, y + 2*s, x + 7*s, y + 7*s, COLOR_WHITE);
            gfx_draw_line(x + 3*s, y + 2*s, x + 8*s, y + 7*s, COLOR_WHITE);
            gfx_draw_line(x + 7*s, y + 2*s, x + 2*s, y + 7*s, COLOR_WHITE);
            gfx_draw_line(x + 8*s, y + 2*s, x + 3*s, y + 7*s, COLOR_WHITE);
            break;

        case ICON_MINIMIZE: // Window [-]
            gfx_fill_rect(x + 2*s, y + 6*s, 6*s, 2*s, COLOR_WHITE);
            break;

        case ICON_MAXIMIZE: // Window [+]
            gfx_draw_rect(x + 2*s, y + 2*s, 6*s, 6*s, COLOR_WHITE);
            gfx_fill_rect(x + 2*s, y + 2*s, 6*s, 2*s, COLOR_WHITE);
            break;

        default:
            gfx_fill_rect(x, y, 16*s, 16*s, COLOR_BLUE);
            break;
    }
}

// BMP Image Structures (Packed)
typedef struct __attribute__((packed)) {
    uint16_t type;             // 'BM' = 0x4D42
    uint32_t file_size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset_data;
} bmp_header_t;

typedef struct __attribute__((packed)) {
    uint32_t size;              // 40
    int32_t  width;
    int32_t  height;
    uint16_t planes;            // 1
    uint16_t bpp;               // 24 or 32
    uint32_t compression;       // 0
    uint32_t image_size;
    int32_t  x_pixels_per_m;
    int32_t  y_pixels_per_m;
    uint32_t colors_used;
    uint32_t colors_important;
} bmp_info_header_t;

int gfx_save_bmp(const char *filename, int w, int h, const uint32_t *pixels) {
    if (!filename || w <= 0 || h <= 0 || !pixels) return -1;
    if (!g_kapi || !g_kapi->fs_write_file) return -1;

    size_t row_stride = (w * 3 + 3) & ~3; // 4-byte aligned
    size_t data_size = row_stride * h;
    size_t total_size = sizeof(bmp_header_t) + sizeof(bmp_info_header_t) + data_size;

    static uint8_t s_bmp_buf[131072]; // up to 128 KiB
    if (total_size > sizeof(s_bmp_buf)) return -2; // Exceeds internal buffer

    bmp_header_t *hdr = (bmp_header_t *)s_bmp_buf;
    bmp_info_header_t *info = (bmp_info_header_t *)(s_bmp_buf + sizeof(bmp_header_t));

    hdr->type = 0x4D42; // "BM"
    hdr->file_size = (uint32_t)total_size;
    hdr->reserved1 = 0;
    hdr->reserved2 = 0;
    hdr->offset_data = sizeof(bmp_header_t) + sizeof(bmp_info_header_t);

    info->size = sizeof(bmp_info_header_t);
    info->width = w;
    info->height = h; // Bottom-up
    info->planes = 1;
    info->bpp = 24; // 24-bit RGB
    info->compression = 0;
    info->image_size = (uint32_t)data_size;
    info->x_pixels_per_m = 2835;
    info->y_pixels_per_m = 2835;
    info->colors_used = 0;
    info->colors_important = 0;

    uint8_t *dst = s_bmp_buf + hdr->offset_data;
    for (int r = h - 1; r >= 0; r--) {
        uint8_t *row_dst = dst + (h - 1 - r) * row_stride;
        for (int c = 0; c < w; c++) {
            uint32_t p = pixels[r * w + c];
            row_dst[c * 3 + 0] = (uint8_t)(p & 0xFF);         // B
            row_dst[c * 3 + 1] = (uint8_t)((p >> 8) & 0xFF);  // G
            row_dst[c * 3 + 2] = (uint8_t)((p >> 16) & 0xFF); // R
        }
    }

    // Save to AarFS
    uint32_t file_id = g_kapi->fs_find_child(0, filename);
    if (!file_id) {
        file_id = g_kapi->fs_create_entry(filename, 1, 0); // 1 = file
    }
    if (!file_id) return -3;

    if (g_kapi->fs_write_file(file_id, s_bmp_buf, total_size) != 0) return -4;
    if (g_kapi->fs_commit) g_kapi->fs_commit();

    return 0;
}

int gfx_load_bmp(const char *filename, int *out_w, int *out_h, uint32_t **out_pixels) {
    if (!filename || !out_w || !out_h || !out_pixels) return -1;
    if (!g_kapi || !g_kapi->fs_find_child || !g_kapi->fs_read_file) return -1;

    uint32_t file_id = g_kapi->fs_find_child(0, filename);
    if (!file_id) return -2;

    static uint8_t s_load_buf[131072];
    uint32_t bytes_read = 0;
    if (g_kapi->fs_read_file(file_id, s_load_buf, &bytes_read) != 0 || bytes_read < 54) {
        return -3;
    }

    bmp_header_t *hdr = (bmp_header_t *)s_load_buf;
    if (hdr->type != 0x4D42) return -4;

    bmp_info_header_t *info = (bmp_info_header_t *)(s_load_buf + sizeof(bmp_header_t));
    int w = info->width;
    int h = info->height;
    if (w <= 0 || h <= 0 || w > 512 || h > 512) return -5;

    static uint32_t s_pixel_buf[512 * 512];
    size_t row_stride = (w * (info->bpp / 8) + 3) & ~3;
    const uint8_t *src = s_load_buf + hdr->offset_data;

    for (int r = 0; r < h; r++) {
        int src_row = (h - 1 - r); // BMP is bottom-up
        const uint8_t *row_src = src + src_row * row_stride;
        for (int c = 0; c < w; c++) {
            if (info->bpp == 24) {
                uint8_t b = row_src[c * 3 + 0];
                uint8_t g = row_src[c * 3 + 1];
                uint8_t r_val = row_src[c * 3 + 2];
                s_pixel_buf[r * w + c] = COLOR_RGB(r_val, g, b);
            } else if (info->bpp == 32) {
                uint8_t b = row_src[c * 4 + 0];
                uint8_t g = row_src[c * 4 + 1];
                uint8_t r_val = row_src[c * 4 + 2];
                s_pixel_buf[r * w + c] = COLOR_RGB(r_val, g, b);
            }
        }
    }

    *out_w = w;
    *out_h = h;
    *out_pixels = s_pixel_buf;
    return 0;
}
