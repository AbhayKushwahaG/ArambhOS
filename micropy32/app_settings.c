#include "app_settings.h"
#include "graphics.h"
#include "theme.h"
#include "auth.h"
#include <string.h>
#include <stdio.h>

extern const kernel_api_t *g_kapi;
int auth_gui_change_password(const char *user, const char *new_pass);

enum {
    TAB_APPEARANCE = 0,
    TAB_DISPLAY,
    TAB_USERS,
    TAB_SYSTEM,
    TAB_COUNT
};

typedef struct {
    int current_tab;
    int preview_wp;
    int preview_theme;
    char pwd_new[32];
    char pwd_confirm[32];
    int pwd_field;
    char status_msg[64];
} settings_state_t;

static settings_state_t s_set;

static void settings_on_paint(window_t *win) {
    int cx = win->content_x;
    int cy = win->content_y;
    int cw = win->content_w;
    int ch = win->content_h;

    // 1. Left Category Sidebar (Width: 130px)
    int sb_w = 130;
    gfx_fill_rect(cx, cy, sb_w, ch, COLOR_VERY_LIGHT_GRAY);
    gfx_draw_line(cx + sb_w - 1, cy, cx + sb_w - 1, cy + ch, COLOR_3D_DARK);

    const char *tabs[TAB_COUNT] = {"Appearance", "Display", "User Accounts", "System & Input"};
    for (int t = 0; t < TAB_COUNT; t++) {
        int ty = cy + 12 + t * 36;
        int is_sel = (s_set.current_tab == t);

        if (is_sel) {
            gfx_fill_rect(cx + 6, ty, sb_w - 12, 28, COLOR_RGB(53, 132, 228));
            gfx_draw_text_transparent(cx + 14, ty + 9, tabs[t], COLOR_WHITE);
        } else {
            gfx_fill_rect(cx + 6, ty, sb_w - 12, 28, COLOR_VERY_LIGHT_GRAY);
            gfx_draw_text_transparent(cx + 14, ty + 9, tabs[t], COLOR_BLACK);
        }
    }

    // 2. Main Content Area (cx + sb_w .. cx + cw)
    int mx = cx + sb_w + 16;
    int my = cy + 12;
    int mw = cw - sb_w - 32;

    if (s_set.current_tab == TAB_APPEARANCE) {
        gfx_draw_text_transparent(mx, my, "Select Desktop Wallpaper:", COLOR_BLACK);

        // Wallpaper list buttons (4 options)
        for (int i = 0; i < THEME_WALLPAPER_COUNT; i++) {
            int wy = my + 24 + i * 26;
            int is_sel = (s_set.preview_wp == i);
            int is_act = (g_config.wallpaper_idx == i);

            gfx_fill_rect(mx, wy, 160, 22, is_sel ? COLOR_RGB(53, 132, 228) : COLOR_WHITE);
            gfx_draw_rect_3d(mx, wy, 160, 22, COLOR_3D_LIGHT, COLOR_3D_SHADOW, is_sel);

            char wp_text[32];
            snprintf(wp_text, sizeof(wp_text), "%d. %s%s", i + 1, theme_get_wallpaper_name(i), is_act ? " (Active)" : "");
            gfx_draw_text_transparent(mx + 8, wy + 6, wp_text, is_sel ? COLOR_WHITE : COLOR_BLACK);
        }

        // Live Thumbnail Preview Box (160 x 100)
        int prev_x = mx + 180;
        int prev_y = my + 24;
        gfx_draw_text_transparent(prev_x, my, "Live Preview:", COLOR_BLACK);
        gfx_draw_rect_3d(prev_x - 1, prev_y - 1, 162, 102, COLOR_3D_SHADOW, COLOR_3D_LIGHT, 1);
        theme_render_wallpaper(s_set.preview_wp, prev_x, prev_y, 160, 100);

        // Theme Style Selector
        int th_y = my + 145;
        gfx_draw_text_transparent(mx, th_y, "Select Window & Taskbar Theme:", COLOR_BLACK);
        for (int i = 0; i < THEME_COUNT; i++) {
            int bx = mx + i * 115;
            int by = th_y + 20;
            int is_sel = (s_set.preview_theme == i);
            gfx_fill_rect(bx, by, 110, 24, is_sel ? COLOR_WHITE : COLOR_3D_FACE);
            gfx_draw_rect_3d(bx, by, 110, 24, COLOR_3D_LIGHT, COLOR_3D_SHADOW, is_sel);
            gfx_draw_text_transparent(bx + 6, by + 7, theme_get_theme_name(i), is_sel ? COLOR_BLUE : COLOR_BLACK);
        }

        // [ Apply & Save to /etc/desktop.conf ] Button
        int btn_apply_x = mx + 80;
        int btn_apply_y = my + 215;
        gfx_gradient_v(btn_apply_x, btn_apply_y, 220, 32, COLOR_XP_START_TOP, COLOR_XP_START_BOT);
        gfx_draw_rect_3d(btn_apply_x, btn_apply_y, 220, 32, COLOR_WHITE, COLOR_NAVY, 0);
        gfx_draw_text_transparent(btn_apply_x + 28, btn_apply_y + 10, "Apply & Save Settings", COLOR_WHITE);

    } else if (s_set.current_tab == TAB_DISPLAY) {
        gfx_draw_text_transparent(mx, my, "Display & Framebuffer Information", COLOR_BLUE);

        char res_str[64];
        snprintf(res_str, sizeof(res_str), "Current Resolution: %dx%d (%d-bit TrueColor)", g_width, g_height, g_bpp);
        gfx_draw_text_transparent(mx, my + 30, res_str, COLOR_BLACK);

        char fb_str[64];
        snprintf(fb_str, sizeof(fb_str), "Linear Framebuffer Base: 0x%08X", (unsigned int)(uintptr_t)g_fb);
        gfx_draw_text_transparent(mx, my + 50, fb_str, COLOR_BLACK);

        gfx_draw_text_transparent(mx, my + 70, "Graphics Engine: Bochs BGA / VESA BIOS Extensions", COLOR_BLACK);
        gfx_draw_text_transparent(mx, my + 90, "Double Buffer: Dedicated RAM Arena at 0x00800000", COLOR_BLACK);
        gfx_draw_text_transparent(mx, my + 110, "Refresh: Direct 32-bit dword blitting (rep movsl)", COLOR_BLACK);

    } else if (s_set.current_tab == TAB_USERS) {
        gfx_draw_text_transparent(mx, my, "User Account Management", COLOR_BLUE);

        const char *user = (g_kapi && g_kapi->session_user) ? g_kapi->session_user : "Gentix";
        uint16_t uid = (g_kapi && g_kapi->session_uid) ? *g_kapi->session_uid : 1000;
        uint16_t gid = (g_kapi && g_kapi->session_gid) ? *g_kapi->session_gid : 1000;
        const char *home = (g_kapi && g_kapi->session_home) ? g_kapi->session_home : "/home/gentix";

        char uinfo[128];
        snprintf(uinfo, sizeof(uinfo), "Active Session: %s (UID %u, GID %u)", user, uid, gid);
        gfx_draw_text_transparent(mx, my + 30, uinfo, COLOR_BLACK);

        char hinfo[128];
        snprintf(hinfo, sizeof(hinfo), "Home Directory: %s", home);
        gfx_draw_text_transparent(mx, my + 50, hinfo, COLOR_BLACK);

        gfx_draw_text_transparent(mx, my + 80, "Change Password:", COLOR_BLACK);

        // New Password Input
        gfx_draw_text_transparent(mx, my + 105, "New Password:", COLOR_DARK_GRAY);
        int p1_x = mx + 110, p1_y = my + 100;
        gfx_fill_rect(p1_x, p1_y, 180, 22, COLOR_WHITE);
        gfx_draw_rect_3d(p1_x, p1_y, 180, 22, COLOR_3D_DARK, COLOR_3D_LIGHT, 1);
        char b1[32];
        size_t l1 = strlen(s_set.pwd_new);
        for (size_t i = 0; i < l1 && i < 31; i++) b1[i] = '*';
        b1[l1 < 31 ? l1 : 31] = '\0';
        gfx_draw_text_transparent(p1_x + 6, p1_y + 6, b1, COLOR_BLACK);

        // Confirm Password Input
        gfx_draw_text_transparent(mx, my + 135, "Confirm:", COLOR_DARK_GRAY);
        int p2_x = mx + 110, p2_y = my + 130;
        gfx_fill_rect(p2_x, p2_y, 180, 22, COLOR_WHITE);
        gfx_draw_rect_3d(p2_x, p2_y, 180, 22, COLOR_3D_DARK, COLOR_3D_LIGHT, 1);
        char b2[32];
        size_t l2 = strlen(s_set.pwd_confirm);
        for (size_t i = 0; i < l2 && i < 31; i++) b2[i] = '*';
        b2[l2 < 31 ? l2 : 31] = '\0';
        gfx_draw_text_transparent(p2_x + 6, p2_y + 6, b2, COLOR_BLACK);

        // [ Update Password ] Button
        int btn_pwd_y = my + 165;
        gfx_fill_rect(mx + 60, btn_pwd_y, 160, 26, COLOR_3D_FACE);
        gfx_draw_rect_3d(mx + 60, btn_pwd_y, 160, 26, COLOR_3D_LIGHT, COLOR_3D_SHADOW, 0);
        gfx_draw_text_transparent(mx + 86, btn_pwd_y + 8, "Update Password", COLOR_BLACK);

    } else if (s_set.current_tab == TAB_SYSTEM) {
        gfx_draw_text_transparent(mx, my, "System & Hardware Diagnostics", COLOR_BLUE);

        gfx_draw_text_transparent(mx, my + 30, "Operating System: Aarambh OS (Version 0.4.1)", COLOR_BLACK);
        gfx_draw_text_transparent(mx, my + 50, "CPU Architecture: x86 32-bit Protected Mode (i686)", COLOR_BLACK);
        gfx_draw_text_transparent(mx, my + 70, "Protected Mode Kernel: 31,729 bytes (Limit: 32,256 bytes)", COLOR_BLACK);
        gfx_draw_text_transparent(mx, my + 90, "Kernel Headroom: 527 bytes remaining", COLOR_BLACK);
        gfx_draw_text_transparent(mx, my + 110, "Filesystem: AarFS (2048 entries, 128 MiB virtual disk)", COLOR_BLACK);
        gfx_draw_text_transparent(mx, my + 130, "PS/2 Mouse Subsystem: IRQ12 interrupt-driven 8042 streaming", COLOR_BLACK);
        gfx_draw_text_transparent(mx, my + 150, "Network Stack: RTL8139 / Intel e1000 Gigabit Driver", COLOR_BLACK);
    }

    // Status Message at bottom
    gfx_draw_line(cx + sb_w, cy + ch - 24, cx + cw, cy + ch - 24, COLOR_3D_DARK);
    gfx_draw_text_transparent(mx, cy + ch - 16, s_set.status_msg, COLOR_DARK_GRAY);
}

static void settings_on_click(window_t *win, int cx, int cy, int btn) {
    (void)win; (void)btn;
    // Category Sidebar Clicks (cx < 130)
    if (cx < 130) {
        for (int t = 0; t < TAB_COUNT; t++) {
            int ty = 12 + t * 36;
            if (cy >= ty && cy < ty + 28) {
                s_set.current_tab = t;
                snprintf(s_set.status_msg, sizeof(s_set.status_msg), "Category: %d", t);
                return;
            }
        }
        return;
    }

    int mx = 130 + 16;
    int my = 12;

    if (s_set.current_tab == TAB_APPEARANCE) {
        // Wallpaper Selection
        for (int i = 0; i < THEME_WALLPAPER_COUNT; i++) {
            int wy = my + 24 + i * 26;
            if (cx >= mx && cx < mx + 160 && cy >= wy && cy < wy + 22) {
                s_set.preview_wp = i;
                snprintf(s_set.status_msg, sizeof(s_set.status_msg), "Selected: %s", theme_get_wallpaper_name(i));
                return;
            }
        }
        // Theme Selection
        int th_y = my + 145;
        for (int i = 0; i < THEME_COUNT; i++) {
            int bx = mx + i * 115;
            int by = th_y + 20;
            if (cx >= bx && cx < bx + 110 && cy >= by && cy < by + 24) {
                s_set.preview_theme = i;
                snprintf(s_set.status_msg, sizeof(s_set.status_msg), "Selected Theme: %s", theme_get_theme_name(i));
                return;
            }
        }
        // [ Apply & Save Settings ] Button
        int btn_apply_x = mx + 80;
        int btn_apply_y = my + 215;
        if (cx >= btn_apply_x && cx < btn_apply_x + 220 && cy >= btn_apply_y && cy < btn_apply_y + 32) {
            g_config.wallpaper_idx = s_set.preview_wp;
            g_config.theme_idx = s_set.preview_theme;
            if (theme_save_config() == 0) {
                snprintf(s_set.status_msg, sizeof(s_set.status_msg), "Saved to /etc/desktop.conf on AarFS!");
            } else {
                snprintf(s_set.status_msg, sizeof(s_set.status_msg), "Failed saving to AarFS.");
            }
        }
    } else if (s_set.current_tab == TAB_USERS) {
        int p1_x = mx + 110, p1_y = my + 100;
        int p2_x = mx + 110, p2_y = my + 130;
        if (cx >= p1_x && cx < p1_x + 180 && cy >= p1_y && cy < p1_y + 22) {
            s_set.pwd_field = 0;
        } else if (cx >= p2_x && cx < p2_x + 180 && cy >= p2_y && cy < p2_y + 22) {
            s_set.pwd_field = 1;
        } else if (cx >= mx + 60 && cx < mx + 220 && cy >= my + 165 && cy < my + 191) {
            // Update Password
            if (strlen(s_set.pwd_new) < 4) {
                snprintf(s_set.status_msg, sizeof(s_set.status_msg), "Password too short (< 4 chars).");
            } else if (strcmp(s_set.pwd_new, s_set.pwd_confirm) != 0) {
                snprintf(s_set.status_msg, sizeof(s_set.status_msg), "Passwords do not match.");
            } else {
                const char *user = (g_kapi && g_kapi->session_user) ? g_kapi->session_user : "Gentix";
                if (auth_gui_change_password(user, s_set.pwd_new)) {
                    snprintf(s_set.status_msg, sizeof(s_set.status_msg), "Password updated successfully in /etc/passwd!");
                    s_set.pwd_new[0] = '\0';
                    s_set.pwd_confirm[0] = '\0';
                }
            }
        }
    }
}

static void settings_on_key(window_t *win, int key) {
    (void)win;
    if (s_set.current_tab == TAB_USERS) {
        char *tgt = (s_set.pwd_field == 0) ? s_set.pwd_new : s_set.pwd_confirm;
        size_t len = strlen(tgt);
        if (key == 8) { // Backspace
            if (len > 0) tgt[len - 1] = '\0';
        } else if (key == 9) { // Tab
            s_set.pwd_field = !s_set.pwd_field;
        } else if (key >= 32 && key <= 126 && len < 30) {
            tgt[len] = (char)key;
            tgt[len + 1] = '\0';
        }
    }
}

window_t *app_settings_open(void) {
    s_set.current_tab = TAB_APPEARANCE;
    s_set.preview_wp = g_config.wallpaper_idx;
    s_set.preview_theme = g_config.theme_idx;
    s_set.pwd_new[0] = '\0';
    s_set.pwd_confirm[0] = '\0';
    s_set.pwd_field = 0;
    snprintf(s_set.status_msg, sizeof(s_set.status_msg), "Ready");

    window_t *win = wm_create_window("Aarambh Control Center", 200, 110, 620, 380, WIN_FLAG_RESIZABLE);
    if (!win) return NULL;

    win->on_paint = settings_on_paint;
    win->on_click = settings_on_click;
    win->on_key = settings_on_key;

    return win;
}
