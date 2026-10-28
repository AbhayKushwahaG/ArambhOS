#include "login.h"
#include "graphics.h"
#include "mouse.h"
#include "theme.h"
#include "auth.h"
#include <string.h>
#include <stdio.h>

extern const kernel_api_t *g_kapi;

// Functions exposed from auth.c
int auth_gui_verify(const char *user, const char *pass);
int auth_gui_change_password(const char *user, const char *new_pass);
int auth_gui_ensure_gentix(void);

static char s_selected_user[32] = "Gentix";
static char s_password[64] = "";
static char s_error_msg[64] = "";
static int  s_need_pwd_change = 0;
static char s_new_pwd[64] = "";
static char s_confirm_pwd[64] = "";
static int  s_pwd_change_field = 0; // 0 = new, 1 = confirm

static int is_inside(int px, int py, int x, int y, int w, int h) {
    return (px >= x && px < x + w && py >= y && py < y + h);
}

int login_run_screen(void) {
    auth_gui_ensure_gentix();

    s_password[0] = '\0';
    s_error_msg[0] = '\0';
    s_need_pwd_change = 0;
    s_new_pwd[0] = '\0';
    s_confirm_pwd[0] = '\0';
    s_pwd_change_field = 0;

    int running = 1;

    while (running) {
        // Poll Input
        mouse_poll();
        const mouse_state_t *ms = mouse_get_state();

        // 1. Draw Background (Windows XP Bliss / Fedora GNOME gradient)
        theme_render_wallpaper(g_config.wallpaper_idx, 0, 0, g_width, g_height);

        // Center Login Dialog Box (w=420, h=280)
        int box_w = 420;
        int box_h = s_need_pwd_change ? 300 : 260;
        int box_x = (g_width - box_w) / 2;
        int box_y = (g_height - box_h) / 2;

        // Drop shadow
        gfx_fill_rect(box_x + 6, box_y + 6, box_w, box_h, COLOR_RGB(20, 24, 30));

        // Dialog Window Background
        gfx_fill_rect(box_x, box_y, box_w, box_h, COLOR_XP_LUNA_BG);
        gfx_draw_rect(box_x, box_y, box_w, box_h, COLOR_XP_LUNA_BORDER);
        gfx_draw_rect(box_x + 1, box_y + 1, box_w - 2, box_h - 2, COLOR_WHITE);

        // Title Header Gradient
        gfx_gradient_v(box_x + 2, box_y + 2, box_w - 4, 32, COLOR_XP_LUNA_BLUE_TOP, COLOR_XP_LUNA_BLUE_BOT);
        gfx_draw_icon(box_x + 8, box_y + 8, ICON_COMPUTER, 1);
        gfx_draw_text_transparent(box_x + 30, box_y + 11, "Aarambh OS - Welcome", COLOR_WHITE);

        if (!s_need_pwd_change) {
            // User Avatar Banner
            int av_x = box_x + 30;
            int av_y = box_y + 55;
            gfx_fill_rect(av_x, av_y, 48, 48, COLOR_RGB(53, 132, 228));
            gfx_draw_rect(av_x, av_y, 48, 48, COLOR_NAVY);
            gfx_draw_icon(av_x + 8, av_y + 8, ICON_USER, 2);

            // User Selection Buttons:
            int u1_x = box_x + 95;
            int u1_y = box_y + 55;
            int u2_x = box_x + 210;
            int u2_y = box_y + 55;

            int is_gentix = (strcmp(s_selected_user, "Gentix") == 0);

            // [ Gentix ] Button
            gfx_fill_rect(u1_x, u1_y, 100, 26, is_gentix ? COLOR_WHITE : COLOR_3D_FACE);
            gfx_draw_rect_3d(u1_x, u1_y, 100, 26, COLOR_3D_LIGHT, COLOR_3D_SHADOW, is_gentix);
            gfx_draw_text_transparent(u1_x + 24, u1_y + 8, "Gentix", is_gentix ? COLOR_BLUE : COLOR_BLACK);

            // [ root ] Button
            gfx_fill_rect(u2_x, u2_y, 100, 26, !is_gentix ? COLOR_WHITE : COLOR_3D_FACE);
            gfx_draw_rect_3d(u2_x, u2_y, 100, 26, COLOR_3D_LIGHT, COLOR_3D_SHADOW, !is_gentix);
            gfx_draw_text_transparent(u2_x + 32, u2_y + 8, "root", !is_gentix ? COLOR_BLUE : COLOR_BLACK);

            // Password Label & Input Field
            gfx_draw_text_transparent(box_x + 30, box_y + 115, "Password:", COLOR_BLACK);
            int input_x = box_x + 110;
            int input_y = box_y + 110;
            int input_w = 270;
            int input_h = 24;

            gfx_fill_rect(input_x, input_y, input_w, input_h, COLOR_WHITE);
            gfx_draw_rect_3d(input_x, input_y, input_w, input_h, COLOR_3D_DARK, COLOR_3D_LIGHT, 1);

            // Draw masked password bullets (•)
            size_t plen = strlen(s_password);
            char bullets[64];
            size_t bcount = (plen < sizeof(bullets) - 1) ? plen : (sizeof(bullets) - 1);
            for (size_t i = 0; i < bcount; i++) bullets[i] = '*';
            bullets[bcount] = '\0';
            gfx_draw_text_transparent(input_x + 8, input_y + 8, bullets, COLOR_BLACK);

            // Blinking cursor
            uint32_t ticks = (g_kapi && g_kapi->timer_ticks) ? *g_kapi->timer_ticks : 0;
            if ((ticks / 30) % 2 == 0) {
                gfx_fill_rect(input_x + 8 + bcount * 8, input_y + 5, 2, 14, COLOR_BLACK);
            }

            // Error Message (Red)
            if (s_error_msg[0]) {
                gfx_draw_text_transparent(box_x + 30, box_y + 150, s_error_msg, COLOR_RED);
            } else {
                gfx_draw_text_transparent(box_x + 30, box_y + 150, "Default password for Gentix is: password", COLOR_DARK_GRAY);
            }

            // Action Buttons:
            // [ Log In ] Button
            int btn_login_x = box_x + 160;
            int btn_login_y = box_y + 185;
            gfx_gradient_v(btn_login_x, btn_login_y, 110, 30, COLOR_XP_LUNA_BLUE_TOP, COLOR_XP_LUNA_BLUE_BOT);
            gfx_draw_rect_3d(btn_login_x, btn_login_y, 110, 30, COLOR_WHITE, COLOR_NAVY, 0);
            gfx_draw_text_transparent(btn_login_x + 32, btn_login_y + 10, "Log In", COLOR_WHITE);

            // [ Terminal (Esc) ] Button
            int btn_term_x = box_x + 285;
            int btn_term_y = box_y + 185;
            gfx_fill_rect(btn_term_x, btn_term_y, 100, 30, COLOR_3D_FACE);
            gfx_draw_rect_3d(btn_term_x, btn_term_y, 100, 30, COLOR_3D_LIGHT, COLOR_3D_SHADOW, 0);
            gfx_draw_text_transparent(btn_term_x + 16, btn_term_y + 10, "Terminal", COLOR_BLACK);

            // Handle Mouse Clicks
            if (ms->left_clicked) {
                if (is_inside(ms->x, ms->y, u1_x, u1_y, 100, 26)) {
                    strncpy(s_selected_user, "Gentix", sizeof(s_selected_user) - 1);
                    s_error_msg[0] = '\0';
                } else if (is_inside(ms->x, ms->y, u2_x, u2_y, 100, 26)) {
                    strncpy(s_selected_user, "root", sizeof(s_selected_user) - 1);
                    s_error_msg[0] = '\0';
                } else if (is_inside(ms->x, ms->y, btn_login_x, btn_login_y, 110, 30)) {
                    goto do_login;
                } else if (is_inside(ms->x, ms->y, btn_term_x, btn_term_y, 100, 30)) {
                    return -1; // Exit back to terminal shell
                }
            }
        } else {
            // First-Time Login: Forced Password Change Dialog
            gfx_draw_text_transparent(box_x + 30, box_y + 45, "First Login: Please set a new password.", COLOR_RED);

            // New Password field
            gfx_draw_text_transparent(box_x + 30, box_y + 80, "New Password:", COLOR_BLACK);
            int in1_x = box_x + 140;
            int in1_y = box_y + 75;
            gfx_fill_rect(in1_x, in1_y, 240, 24, COLOR_WHITE);
            gfx_draw_rect_3d(in1_x, in1_y, 240, 24, COLOR_3D_DARK, COLOR_3D_LIGHT, 1);

            char b1[64];
            size_t l1 = strlen(s_new_pwd);
            for (size_t i = 0; i < l1 && i < 63; i++) b1[i] = '*';
            b1[l1 < 63 ? l1 : 63] = '\0';
            gfx_draw_text_transparent(in1_x + 8, in1_y + 8, b1, COLOR_BLACK);

            // Confirm Password field
            gfx_draw_text_transparent(box_x + 30, box_y + 125, "Confirm:", COLOR_BLACK);
            int in2_x = box_x + 140;
            int in2_y = box_y + 120;
            gfx_fill_rect(in2_x, in2_y, 240, 24, COLOR_WHITE);
            gfx_draw_rect_3d(in2_x, in2_y, 240, 24, COLOR_3D_DARK, COLOR_3D_LIGHT, 1);

            char b2[64];
            size_t l2 = strlen(s_confirm_pwd);
            for (size_t i = 0; i < l2 && i < 63; i++) b2[i] = '*';
            b2[l2 < 63 ? l2 : 63] = '\0';
            gfx_draw_text_transparent(in2_x + 8, in2_y + 8, b2, COLOR_BLACK);

            // Error text
            if (s_error_msg[0]) {
                gfx_draw_text_transparent(box_x + 30, box_y + 165, s_error_msg, COLOR_RED);
            }

            // [ Save & Enter Desktop ] Button
            int btn_save_x = box_x + 110;
            int btn_save_y = box_y + 210;
            gfx_gradient_v(btn_save_x, btn_save_y, 200, 32, COLOR_XP_START_TOP, COLOR_XP_START_BOT);
            gfx_draw_rect_3d(btn_save_x, btn_save_y, 200, 32, COLOR_WHITE, COLOR_NAVY, 0);
            gfx_draw_text_transparent(btn_save_x + 22, btn_save_y + 10, "Save & Enter Desktop", COLOR_WHITE);

            if (ms->left_clicked) {
                if (is_inside(ms->x, ms->y, in1_x, in1_y, 240, 24)) s_pwd_change_field = 0;
                else if (is_inside(ms->x, ms->y, in2_x, in2_y, 240, 24)) s_pwd_change_field = 1;
                else if (is_inside(ms->x, ms->y, btn_save_x, btn_save_y, 200, 32)) {
                    goto do_pwd_change;
                }
            }
        }

        // Check Keyboard Input
        if (g_kapi && g_kapi->kbd_check && g_kapi->kbd_check()) {
            char key = g_kapi->getc();
            if (key == 27) { // Esc = exit to shell
                return -1;
            } else if (key == 13) { // Enter = Login / Submit
                if (!s_need_pwd_change) {
                    goto do_login;
                } else {
                    goto do_pwd_change;
                }
            } else if (key == 9) { // Tab = switch fields
                if (s_need_pwd_change) {
                    s_pwd_change_field = !s_pwd_change_field;
                }
            } else if (key == 8) { // Backspace
                if (!s_need_pwd_change) {
                    size_t len = strlen(s_password);
                    if (len > 0) s_password[len - 1] = '\0';
                } else {
                    char *tgt = (s_pwd_change_field == 0) ? s_new_pwd : s_confirm_pwd;
                    size_t len = strlen(tgt);
                    if (len > 0) tgt[len - 1] = '\0';
                }
            } else if (key >= 32 && key <= 126) {
                if (!s_need_pwd_change) {
                    size_t len = strlen(s_password);
                    if (len < sizeof(s_password) - 1) {
                        s_password[len] = key;
                        s_password[len + 1] = '\0';
                    }
                } else {
                    char *tgt = (s_pwd_change_field == 0) ? s_new_pwd : s_confirm_pwd;
                    size_t len = strlen(tgt);
                    if (len < 63) {
                        tgt[len] = key;
                        tgt[len + 1] = '\0';
                    }
                }
            }
        }

        // Render Mouse and Flip
        mouse_draw();
        gfx_flip();

        // Small delay
        for (volatile int d = 0; d < 8000; d++);
        continue;

    do_login:
        if (auth_gui_verify(s_selected_user, s_password)) {
            // Check if user is Gentix and still has bootstrap password 'password'
            if (strcmp(s_selected_user, "Gentix") == 0 && strcmp(s_password, "password") == 0) {
                s_need_pwd_change = 1;
                s_error_msg[0] = '\0';
                s_new_pwd[0] = '\0';
                s_confirm_pwd[0] = '\0';
            } else {
                return 0; // Success! Proceed to Desktop
            }
        } else {
            snprintf(s_error_msg, sizeof(s_error_msg), "Invalid password for user '%s'.", s_selected_user);
            s_password[0] = '\0';
        }
        continue;

    do_pwd_change:
        if (strlen(s_new_pwd) < 4) {
            snprintf(s_error_msg, sizeof(s_error_msg), "Password must be at least 4 characters.");
        } else if (strcmp(s_new_pwd, s_confirm_pwd) != 0) {
            snprintf(s_error_msg, sizeof(s_error_msg), "Passwords do not match.");
        } else if (strcmp(s_new_pwd, "password") == 0) {
            snprintf(s_error_msg, sizeof(s_error_msg), "New password cannot be default 'password'.");
        } else {
            auth_gui_change_password(s_selected_user, s_new_pwd);
            return 0; // Password successfully updated and verified!
        }
    }

    return 0;
}
