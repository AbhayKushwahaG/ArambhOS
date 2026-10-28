#include "desktop.h"
#include "graphics.h"
#include "mouse.h"
#include "wm.h"
#include "theme.h"
#include "login.h"
#include "app_notepad.h"
#include "app_paint.h"
#include "app_filemgr.h"
#include "app_snake.h"
#include "app_settings.h"
#include "auth.h"
#include <string.h>
#include <stdio.h>

extern const kernel_api_t *g_kapi;
int cmd_run(const char *args);
int cmd_dpkg(const char *args);

static int s_start_menu_open = 0;
static int s_selected_desktop_icon = -1;

typedef struct {
    const char *name;
    int icon_id;
    int x;
    int y;
    int action_id;
} desktop_icon_t;

enum {
    ACT_COMPUTER = 0,
    ACT_EXPLORER,
    ACT_NOTEPAD,
    ACT_PAINT,
    ACT_SNAKE,
    ACT_SETTINGS,
    ACT_TERMINAL
};

#define DESKTOP_ICON_COUNT 7
static desktop_icon_t s_desktop_icons[DESKTOP_ICON_COUNT] = {
    {"My Computer",   ICON_COMPUTER, 24,  44, ACT_COMPUTER},
    {"File Explorer", ICON_EXPLORER, 24, 116, ACT_EXPLORER},
    {"Notepad",       ICON_NOTEPAD,  24, 188, ACT_NOTEPAD},
    {"Paint",         ICON_PAINT,     24, 260, ACT_PAINT},
    {"Snake Game",    ICON_SNAKE,     24, 332, ACT_SNAKE},
    {"Settings",      ICON_SETTINGS,  24, 404, ACT_SETTINGS},
    {"Terminal",      ICON_TERMINAL,  24, 476, ACT_TERMINAL}
};

static void launch_action(int action_id) {
    s_start_menu_open = 0;
    switch (action_id) {
        case ACT_COMPUTER:
            app_filemgr_open("/");
            break;
        case ACT_EXPLORER:
            app_filemgr_open("/home/gentix");
            break;
        case ACT_NOTEPAD:
            app_notepad_open("");
            break;
        case ACT_PAINT:
            app_paint_open("");
            break;
        case ACT_SNAKE:
            app_snake_open();
            break;
        case ACT_SETTINGS:
            app_settings_open();
            break;
        case ACT_TERMINAL:
            // Handled as exit or terminal window
            break;
        default:
            break;
    }
}

static void print_line(const char *s) {
    if (g_kapi && g_kapi->putc && s) {
        while (*s) g_kapi->putc(*s++);
        g_kapi->putc('\r');
        g_kapi->putc('\n');
    }
}

static void render_gnome_top_panel(void) {
    // Fedora GNOME Top Panel (Height: 28px, y = 0..27)
    gfx_fill_rect(0, 0, g_width, 28, COLOR_GNOME_PANEL);
    gfx_draw_line(0, 27, g_width, 27, COLOR_GNOME_BORDER);

    // Left: "Activities" Pill Button
    gfx_fill_rect(8, 3, 90, 22, COLOR_GNOME_PILL);
    gfx_draw_rect(8, 3, 90, 22, COLOR_GNOME_BORDER);
    gfx_draw_text_transparent(22, 9, "Activities", COLOR_WHITE);

    // Active Window Title in Panel
    window_t *act = wm_get_active_window();
    if (act) {
        char win_tag[64];
        snprintf(win_tag, sizeof(win_tag), "|  %s", act->title);
        gfx_draw_text_transparent(112, 9, win_tag, COLOR_WHITE);
    }

    // Center: Live Digital Clock & Date
    uint32_t ticks = (g_kapi && g_kapi->timer_ticks) ? *g_kapi->timer_ticks : 0;
    uint32_t total_sec = ticks / 100;
    uint32_t sec = total_sec % 60;
    uint32_t min = (total_sec / 60) % 60;
    uint32_t hr  = (total_sec / 3600 + 12) % 24; // Simulated 24-hr clock

    char clock_str[64];
    snprintf(clock_str, sizeof(clock_str), "Aarambh OS  %02u:%02u:%02u", (unsigned)hr, (unsigned)min, (unsigned)sec);
    int cw_x = (g_width - (int)strlen(clock_str) * 8) / 2;
    gfx_draw_text_transparent(cw_x, 9, clock_str, COLOR_WHITE);

    // Right: Status Pills
    const char *usr = (g_kapi && g_kapi->session_user) ? g_kapi->session_user : "Gentix";
    int rx = g_width - 190;

    // User Pill
    gfx_fill_rect(rx, 3, 76, 22, COLOR_GNOME_PILL);
    gfx_draw_rect(rx, 3, 76, 22, COLOR_GNOME_BORDER);
    gfx_draw_icon(rx + 6, 6, ICON_USER, 1);
    gfx_draw_text_transparent(rx + 26, 9, usr, COLOR_WHITE);

    // Network Status Pill
    int nx = g_width - 105;
    gfx_fill_rect(nx, 3, 95, 22, COLOR_GNOME_PILL);
    gfx_draw_rect(nx, 3, 95, 22, COLOR_GNOME_BORDER);
    gfx_fill_rect(nx + 8, 9, 8, 8, COLOR_GREEN); // Online indicator
    gfx_draw_text_transparent(nx + 22, 9, "LAN [UP]", COLOR_WHITE);
}

static void render_xp_taskbar(void) {
    // Windows XP Luna Taskbar (Height: 36px at bottom of screen)
    int tb_y = g_height - 36;
    gfx_gradient_v(0, tb_y, g_width, 36, COLOR_XP_TASKBAR_TOP, COLOR_XP_TASKBAR_BOT);
    gfx_draw_line(0, tb_y, g_width, tb_y, COLOR_RGB(53, 147, 255)); // Highlight border

    // Windows XP Start Button
    int start_w = 110;
    gfx_gradient_v(0, tb_y + 1, start_w, 34, COLOR_XP_START_TOP, COLOR_XP_START_BOT);
    gfx_draw_rect_3d(0, tb_y + 1, start_w, 34, COLOR_WHITE, COLOR_RGB(10, 60, 20), s_start_menu_open);

    // Start Button Text & Icon
    gfx_draw_text_transparent(36, tb_y + 12, "start", COLOR_WHITE);
    gfx_fill_rect(12, tb_y + 10, 16, 16, COLOR_WHITE);
    gfx_fill_rect(14, tb_y + 12, 5, 5, COLOR_RED);
    gfx_fill_rect(21, tb_y + 12, 5, 5, COLOR_GREEN);
    gfx_fill_rect(14, tb_y + 19, 5, 5, COLOR_BLUE);
    gfx_fill_rect(21, tb_y + 19, 5, 5, COLOR_YELLOW);

    // Quick Launch Icons
    int qx = start_w + 10;
    gfx_draw_icon(qx, tb_y + 10, ICON_EXPLORER, 1);
    gfx_draw_icon(qx + 22, tb_y + 10, ICON_NOTEPAD, 1);
    gfx_draw_icon(qx + 44, tb_y + 10, ICON_PAINT, 1);
    gfx_draw_icon(qx + 66, tb_y + 10, ICON_SETTINGS, 1);
    gfx_draw_line(qx + 90, tb_y + 6, qx + 90, tb_y + 30, COLOR_RGB(30, 80, 180));

    // Open Windows Tabs
    int tab_x = qx + 100;
    int tab_w = 150;
    int win_count = wm_get_window_count();
    window_t *active_win = wm_get_active_window();

    for (int i = 0; i < win_count; i++) {
        window_t *w = wm_get_window_by_index(i);
        if (!w || !(w->flags & WIN_FLAG_VISIBLE)) continue;
        if (tab_x + tab_w > g_width - 120) break;

        int is_act = (w == active_win);
        if (is_act) {
            gfx_gradient_v(tab_x, tb_y + 4, tab_w, 28, COLOR_RGB(30, 70, 170), COLOR_RGB(15, 45, 120));
            gfx_draw_rect_3d(tab_x, tb_y + 4, tab_w, 28, COLOR_RGB(10, 30, 80), COLOR_RGB(80, 140, 240), 1);
        } else {
            gfx_gradient_v(tab_x, tb_y + 4, tab_w, 28, COLOR_XP_TASKBAR_TOP, COLOR_XP_TASKBAR_BOT);
            gfx_draw_rect_3d(tab_x, tb_y + 4, tab_w, 28, COLOR_RGB(80, 140, 240), COLOR_RGB(10, 30, 80), 0);
        }

        char tab_title[20];
        strncpy(tab_title, w->title, sizeof(tab_title) - 1);
        tab_title[sizeof(tab_title) - 1] = '\0';
        gfx_draw_text_transparent(tab_x + 10, tb_y + 12, tab_title, is_act ? COLOR_WHITE : COLOR_LIGHT_GRAY);

        tab_x += tab_w + 4;
    }

    // System Tray Area on Right
    int tray_w = 110;
    int tray_x = g_width - tray_w;
    gfx_fill_rect(tray_x, tb_y + 2, tray_w, 32, COLOR_RGB(15, 45, 120));
    gfx_draw_rect_3d(tray_x, tb_y + 2, tray_w, 32, COLOR_RGB(10, 30, 80), COLOR_RGB(80, 140, 240), 1);

    uint32_t ticks = (g_kapi && g_kapi->timer_ticks) ? *g_kapi->timer_ticks : 0;
    uint32_t total_sec = ticks / 100;
    uint32_t sec = total_sec % 60;
    uint32_t min = (total_sec / 60) % 60;
    char tray_time[32];
    snprintf(tray_time, sizeof(tray_time), "%02u:%02u:%02u", (unsigned)((total_sec / 3600 + 12) % 24), (unsigned)min, (unsigned)sec);
    gfx_draw_text_transparent(tray_x + 22, tb_y + 13, tray_time, COLOR_WHITE);
}

static void render_start_menu(void) {
    if (!s_start_menu_open) return;

    int sm_w = 340;
    int sm_h = 420;
    int sm_x = 0;
    int sm_y = g_height - 36 - sm_h;

    // Outer Drop Shadow
    gfx_fill_rect(sm_x + 6, sm_y + 6, sm_w, sm_h, COLOR_RGB(20, 24, 30));

    // Dialog Frame
    gfx_fill_rect(sm_x, sm_y, sm_w, sm_h, COLOR_WHITE);
    gfx_draw_rect(sm_x, sm_y, sm_w, sm_h, COLOR_XP_LUNA_BORDER);

    // Top User Profile Header (Height: 56px)
    gfx_gradient_v(sm_x + 1, sm_y + 1, sm_w - 2, 56, COLOR_XP_LUNA_BLUE_TOP, COLOR_XP_LUNA_BLUE_BOT);
    // User Avatar
    gfx_fill_rect(sm_x + 12, sm_y + 8, 40, 40, COLOR_RGB(53, 132, 228));
    gfx_draw_rect(sm_x + 12, sm_y + 8, 40, 40, COLOR_WHITE);
    gfx_draw_icon(sm_x + 16, sm_y + 12, ICON_USER, 2);

    const char *usr = (g_kapi && g_kapi->session_user) ? g_kapi->session_user : "Gentix";
    gfx_draw_text_scaled(sm_x + 62, sm_y + 16, usr, COLOR_WHITE, 2);
    gfx_draw_text_transparent(sm_x + 64, sm_y + 36, "Aarambh OS Workstation", COLOR_RGB(200, 225, 255));

    // Left Column: Applications (Width: 190px)
    int left_w = 190;
    gfx_fill_rect(sm_x + 1, sm_y + 57, left_w, sm_h - 97, COLOR_WHITE);

    const char *app_names[8] = {
        "Notepad", "Paint", "File Explorer", "Snake Game",
        "Control Center", "Terminal", "Run Script", "Package Tool"
    };
    int app_icons[8] = {
        ICON_NOTEPAD, ICON_PAINT, ICON_EXPLORER, ICON_SNAKE,
        ICON_SETTINGS, ICON_TERMINAL, ICON_SCRIPT, ICON_PACKAGE
    };

    for (int i = 0; i < 8; i++) {
        int iy = sm_y + 62 + i * 36;
        gfx_draw_icon(sm_x + 10, iy + 6, app_icons[i], 1);
        gfx_draw_text_transparent(sm_x + 36, iy + 10, app_names[i], COLOR_BLACK);
        gfx_draw_line(sm_x + 6, iy + 34, sm_x + left_w - 6, iy + 34, COLOR_VERY_LIGHT_GRAY);
    }

    // Right Column: Places & Settings (Width: sm_w - left_w - 2)
    int right_x = sm_x + left_w + 1;
    int right_w = sm_w - left_w - 2;
    gfx_fill_rect(right_x, sm_y + 57, right_w, sm_h - 97, COLOR_RGB(212, 228, 248));
    gfx_draw_line(right_x, sm_y + 57, right_x, sm_y + sm_h - 40, COLOR_RGB(165, 195, 230));

    const char *places[4] = {"My Documents", "Root Storage", "Settings", "Network"};
    int p_icons[4] = {ICON_EXPLORER, ICON_COMPUTER, ICON_SETTINGS, ICON_TERMINAL};
    for (int i = 0; i < 4; i++) {
        int py = sm_y + 66 + i * 38;
        gfx_draw_icon(right_x + 8, py + 6, p_icons[i], 1);
        gfx_draw_text_transparent(right_x + 30, py + 10, places[i], COLOR_NAVY);
    }

    // Bottom Action Bar (Height: 40px)
    int bot_y = sm_y + sm_h - 40;
    gfx_gradient_v(sm_x + 1, bot_y, sm_w - 2, 39, COLOR_XP_TASKBAR_TOP, COLOR_XP_TASKBAR_BOT);

    // [ Log Off ] Button
    int lo_x = sm_x + 120;
    gfx_draw_icon(lo_x, bot_y + 11, ICON_USER, 1);
    gfx_draw_text_transparent(lo_x + 22, bot_y + 15, "Log Off", COLOR_WHITE);

    // [ Exit to Shell ] Button
    int sh_x = sm_x + 215;
    gfx_draw_icon(sh_x, bot_y + 11, ICON_POWER, 1);
    gfx_draw_text_transparent(sh_x + 22, bot_y + 15, "Exit Shell", COLOR_WHITE);
}

static void render_desktop_icons(void) {
    for (int i = 0; i < DESKTOP_ICON_COUNT; i++) {
        desktop_icon_t *it = &s_desktop_icons[i];
        int is_sel = (s_selected_desktop_icon == i);

        // Icon Box
        if (is_sel) {
            gfx_fill_rect(it->x - 4, it->y - 4, 76, 56, COLOR_RGB(53, 132, 228));
            gfx_draw_rect(it->x - 4, it->y - 4, 76, 56, COLOR_WHITE);
        }

        // Draw Vector Icon (scale 2 = 32x32 pixels)
        gfx_draw_icon(it->x + 16, it->y, it->icon_id, 2);

        // Icon Label with Drop Shadow
        gfx_draw_text_transparent(it->x - 2, it->y + 37, it->name, COLOR_BLACK);
        gfx_draw_text_transparent(it->x - 3, it->y + 36, it->name, is_sel ? COLOR_WHITE : COLOR_WHITE);
    }
}

int cmd_desktop(const char *args) {
    (void)args;

    // 1. Initialize High-Resolution 1024x768 32-bit TrueColor Graphics
    if (!gfx_init(1024, 768)) {
        print_line("desktop: graphics initialization failed.");
        return 1;
    }

    // 2. Initialize Theme & Configuration Engine
    theme_init();

    // 3. Initialize PS/2 Interrupt-Driven Mouse
    mouse_init(g_width, g_height);

    // 4. Initialize Window Manager
    wm_init();

    // 5. Run Graphical Login Screen First
    int login_res = login_run_screen();
    if (login_res < 0) {
        // User pressed Esc or clicked Exit on login screen
        gfx_exit();
        print_line("\n[Aarambh OS] Returned from Desktop to Text Shell.");
        return 0;
    }

    // 6. Launch Initial Window (Aarambh Control Center)
    app_settings_open();

    int running = 1;

    while (running) {
        // --- Input Processing ---
        mouse_poll();
        const mouse_state_t *ms = mouse_get_state();

        // 1. Handle Start Menu Clicks
        if (s_start_menu_open) {
            int sm_w = 340;
            int sm_h = 420;
            int sm_x = 0;
            int sm_y = g_height - 36 - sm_h;

            if (ms->left_clicked) {
                // Check Start Menu items
                if (ms->x >= sm_x && ms->x < sm_x + sm_w && ms->y >= sm_y && ms->y < sm_y + sm_h) {
                    // Check Left Column apps
                    if (ms->x < sm_x + 190 && ms->y >= sm_y + 60 && ms->y < sm_y + sm_h - 40) {
                        int idx = (ms->y - (sm_y + 62)) / 36;
                        if (idx == 0) launch_action(ACT_NOTEPAD);
                        else if (idx == 1) launch_action(ACT_PAINT);
                        else if (idx == 2) launch_action(ACT_EXPLORER);
                        else if (idx == 3) launch_action(ACT_SNAKE);
                        else if (idx == 4) launch_action(ACT_SETTINGS);
                        else if (idx == 5) { // Terminal
                            running = 0;
                            break;
                        } else if (idx == 6) { // Run Script
                            app_filemgr_open("/");
                            s_start_menu_open = 0;
                        } else if (idx == 7) { // Package Manager
                            cmd_dpkg("");
                            s_start_menu_open = 0;
                        }
                    }
                    // Check Bottom Bar
                    else if (ms->y >= sm_y + sm_h - 40) {
                        if (ms->x >= sm_x + 120 && ms->x < sm_x + 200) { // Log Off
                            s_start_menu_open = 0;
                            if (login_run_screen() < 0) {
                                running = 0;
                                break;
                            }
                        } else if (ms->x >= sm_x + 215) { // Exit Shell
                            running = 0;
                            break;
                        }
                    }
                } else {
                    s_start_menu_open = 0; // Clicked outside Start Menu
                }
            }
        }

        // 2. Handle Taskbar Clicks
        int tb_y = g_height - 36;
        if (ms->left_clicked && ms->y >= tb_y) {
            // Start Button Click
            if (ms->x < 110) {
                s_start_menu_open = !s_start_menu_open;
            }
            // Quick Launch
            else if (ms->x >= 120 && ms->x < 140) launch_action(ACT_EXPLORER);
            else if (ms->x >= 142 && ms->x < 162) launch_action(ACT_NOTEPAD);
            else if (ms->x >= 164 && ms->x < 184) launch_action(ACT_PAINT);
            else if (ms->x >= 186 && ms->x < 206) launch_action(ACT_SETTINGS);
            // Window Tabs
            else if (ms->x >= 220 && ms->x < g_width - 120) {
                int tab_idx = (ms->x - 220) / 154;
                if (tab_idx >= 0 && tab_idx < wm_get_window_count()) {
                    window_t *w = wm_get_window_by_index(tab_idx);
                    if (w) {
                        if (w == wm_get_active_window()) {
                            wm_minimize_window(w);
                        } else {
                            wm_restore_window(w);
                        }
                    }
                }
            }
        }

        // 3. Handle Window Manager Mouse Events
        int wm_consumed = wm_handle_mouse(ms);

        // 4. Handle Desktop Icon Clicks (if not consumed by WM, Taskbar, or Start Menu)
        if (!wm_consumed && !s_start_menu_open && ms->y < tb_y && ms->y >= 28) {
            if (ms->left_clicked) {
                int hit = -1;
                for (int i = 0; i < DESKTOP_ICON_COUNT; i++) {
                    desktop_icon_t *it = &s_desktop_icons[i];
                    if (ms->x >= it->x - 4 && ms->x < it->x + 72 && ms->y >= it->y - 4 && ms->y < it->y + 52) {
                        hit = i;
                        break;
                    }
                }
                if (hit >= 0) {
                    if (ms->double_clicked || (s_selected_desktop_icon == hit)) {
                        launch_action(s_desktop_icons[hit].action_id);
                        if (s_desktop_icons[hit].action_id == ACT_TERMINAL) {
                            running = 0;
                            break;
                        }
                    } else {
                        s_selected_desktop_icon = hit;
                    }
                } else {
                    s_selected_desktop_icon = -1;
                }
            }
        }

        // 5. Handle Keyboard Events
        if (g_kapi && g_kapi->kbd_check && g_kapi->kbd_check()) {
            char key = g_kapi->getc();
            if (key == 27 || key == 'q' || key == 'Q') { // Esc or Q = Exit to Shell
                running = 0;
                break;
            } else {
                wm_handle_key(key);
            }
        }

        // --- Rendering Phase ---

        // 1. Draw Active Wallpaper
        theme_render_wallpaper(g_config.wallpaper_idx, 0, 28, g_width, g_height - 64);

        // 2. Draw Desktop Icons
        render_desktop_icons();

        // 3. Render Windows via Window Manager
        wm_paint_all();

        // 4. Render Fedora GNOME Top Panel (renders over desktop and windows)
        render_gnome_top_panel();

        // 5. Render Windows XP Taskbar
        render_xp_taskbar();

        // 6. Render Start Menu Popup (if open)
        render_start_menu();

        // 7. Render Mouse Cursor
        mouse_draw();

        // 8. Flip backbuffer to Linear Framebuffer
        gfx_flip();

        // Delay to prevent CPU spinning
        for (volatile int d = 0; d < 8000; d++);
    }

    // --- Clean Exit Transition ---
    gfx_exit();
    print_line("\n[Aarambh OS] Returned from Desktop to Text Shell.");

    return 0;
}
