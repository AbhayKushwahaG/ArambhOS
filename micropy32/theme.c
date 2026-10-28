#include "theme.h"
#include "graphics.h"
#include "auth.h"
#include <string.h>
#include <stdio.h>

extern const kernel_api_t *g_kapi;

desktop_config_t g_config = {
    .wallpaper_idx = 0,
    .theme_idx = 0,
    .accent_color = COLOR_RGB(0, 85, 234)
};

static const char *s_wallpaper_names[THEME_WALLPAPER_COUNT] = {
    "XP Bliss",
    "GNOME Adwaita",
    "Classic Teal",
    "Charcoal Dark"
};

static const char *s_theme_names[THEME_COUNT] = {
    "Windows XP Luna",
    "Fedora GNOME Slate",
    "Modern Dark",
    "Classic 9x"
};

const char *theme_get_wallpaper_name(int idx) {
    if (idx < 0 || idx >= THEME_WALLPAPER_COUNT) return "Unknown";
    return s_wallpaper_names[idx];
}

const char *theme_get_theme_name(int idx) {
    if (idx < 0 || idx >= THEME_COUNT) return "Unknown";
    return s_theme_names[idx];
}

void theme_init(void) {
    theme_load_config();
}

void theme_load_config(void) {
    if (!g_kapi || !g_kapi->fs_find_child || !g_kapi->fs_read_file) return;

    uint32_t etc_id = g_kapi->fs_find_child(0, "etc");
    if (!etc_id) return;

    uint32_t conf_id = g_kapi->fs_find_child(etc_id, "desktop.conf");
    if (!conf_id) return;

    static char buf[512];
    uint32_t bytes_read = 0;
    if (g_kapi->fs_read_file(conf_id, buf, &bytes_read) != 0 || bytes_read == 0) return;
    buf[bytes_read < 511 ? bytes_read : 511] = '\0';

    char *p = strstr(buf, "wallpaper=");
    if (p) {
        int w = p[10] - '0';
        if (w >= 0 && w < THEME_WALLPAPER_COUNT) g_config.wallpaper_idx = w;
    }

    p = strstr(buf, "theme=");
    if (p) {
        if (strstr(p, "theme=xp_luna") || strstr(p, "theme=0")) g_config.theme_idx = 0;
        else if (strstr(p, "theme=gnome") || strstr(p, "theme=1")) g_config.theme_idx = 1;
        else if (strstr(p, "theme=dark") || strstr(p, "theme=2")) g_config.theme_idx = 2;
        else if (strstr(p, "theme=classic") || strstr(p, "theme=3")) g_config.theme_idx = 3;
    }
}

int theme_save_config(void) {
    if (!g_kapi || !g_kapi->fs_find_child || !g_kapi->fs_create_entry || !g_kapi->fs_write_file) {
        return -1;
    }

    uint32_t etc_id = g_kapi->fs_find_child(0, "etc");
    if (!etc_id) {
        etc_id = g_kapi->fs_create_entry("etc", 2, 0); // type 2 = dir
    }
    if (!etc_id) return -1;

    uint32_t conf_id = g_kapi->fs_find_child(etc_id, "desktop.conf");
    if (!conf_id) {
        conf_id = g_kapi->fs_create_entry("desktop.conf", 1, etc_id); // type 1 = file
    }
    if (!conf_id) return -1;

    char buf[256];
    snprintf(buf, sizeof(buf),
             "wallpaper=%d\ntheme=%d\naccent=%06X\n",
             g_config.wallpaper_idx, g_config.theme_idx, (unsigned int)(g_config.accent_color & 0xFFFFFF));

    size_t len = strlen(buf);
    if (g_kapi->fs_write_file(conf_id, buf, len) != 0) return -1;
    if (g_kapi->fs_commit) g_kapi->fs_commit();

    return 0;
}

void theme_render_wallpaper(int wp_idx, int x, int y, int w, int h) {
    switch (wp_idx) {
        case 0: { // XP Bliss: Azure Sky + Sun + Rolling Emerald Hills
            int sky_h = (h * 58) / 100;
            // Sky Gradient
            gfx_gradient_v(x, y, w, sky_h, COLOR_RGB(0, 85, 215), COLOR_RGB(110, 185, 255));

            // Sun Radiance
            int sun_x = x + (w * 3) / 4;
            int sun_y = y + sky_h / 3;
            gfx_fill_rect(sun_x - 12, sun_y - 12, 24, 24, COLOR_WHITE);
            gfx_draw_rect(sun_x - 16, sun_y - 16, 32, 32, COLOR_RGB(255, 250, 200));

            // Lush Rolling Hills (Polynomial wave function across columns)
            int hill_y = y + sky_h;
            for (int col = 0; col < w; col++) {
                int px = x + col;
                // Double smooth hill curve
                int c1 = ((col * 16) / w);
                int curve1 = (c1 > 8) ? (16 - c1) : c1;
                int curve2 = ((col * 24) % 31) > 15 ? 2 : 0;
                int start_y = hill_y - (curve1 * 3) + curve2;

                if (start_y < y) start_y = y;
                int hill_depth = (y + h) - start_y;
                if (hill_depth > 0) {
                    gfx_gradient_v(px, start_y, 1, hill_depth, COLOR_RGB(78, 175, 42), COLOR_RGB(25, 95, 18));
                }
            }
            break;
        }

        case 1: { // GNOME Adwaita: Deep Indigo & Slate Gradient
            gfx_gradient_v(x, y, w, h, COLOR_RGB(28, 34, 46), COLOR_RGB(45, 58, 82));
            // Subtle geometric accent lines
            for (int i = 0; i < 4; i++) {
                int ly = y + (h * (i + 1)) / 5;
                gfx_draw_line(x, ly, x + w, ly - 20, COLOR_RGB(40, 50, 68));
            }
            break;
        }

        case 2: { // Classic Teal: Windows 95/XP Classic Teal
            gfx_fill_rect(x, y, w, h, COLOR_RGB(0, 128, 128));
            // Fine coordinate grid dots
            for (int r = y + 16; r < y + h; r += 24) {
                for (int c = x + 16; c < x + w; c += 24) {
                    gfx_put_pixel(c, r, COLOR_RGB(0, 140, 140));
                }
            }
            break;
        }

        case 3: // Charcoal Dark: Modern Minimalist Workstation
        default: {
            gfx_gradient_v(x, y, w, h, COLOR_RGB(24, 26, 30), COLOR_RGB(36, 40, 48));
            for (int r = y + 20; r < y + h; r += 32) {
                for (int c = x + 20; c < x + w; c += 32) {
                    gfx_put_pixel(c, r, COLOR_RGB(50, 56, 68));
                }
            }
            break;
        }
    }
}
