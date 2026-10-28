#include "wm.h"
#include "graphics.h"
#include <string.h>

static window_t s_windows[MAX_WINDOWS];
static int s_z_order[MAX_WINDOWS];
static int s_num_windows = 0;
static int s_next_id = 1;

static window_t *s_dragging_win = NULL;
static int s_drag_off_x = 0;
static int s_drag_off_y = 0;

void wm_init(void) {
    s_num_windows = 0;
    s_next_id = 1;
    s_dragging_win = NULL;
    memset(s_windows, 0, sizeof(s_windows));
}

static void update_content_area(window_t *win) {
    if (win->flags & WIN_FLAG_NO_BORDER) {
        win->content_x = win->x;
        win->content_y = win->y;
        win->content_w = win->w;
        win->content_h = win->h;
    } else {
        win->content_x = win->x + 3;
        win->content_y = win->y + 24;
        win->content_w = win->w - 6;
        win->content_h = win->h - 27;
        if (win->content_w < 0) win->content_w = 0;
        if (win->content_h < 0) win->content_h = 0;
    }
}

window_t *wm_create_window(const char *title, int x, int y, int w, int h, uint32_t flags) {
    if (s_num_windows >= MAX_WINDOWS) return NULL;

    int slot = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (s_windows[i].id == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return NULL;

    window_t *win = &s_windows[slot];
    memset(win, 0, sizeof(window_t));

    win->id = s_next_id++;
    strncpy(win->title, title ? title : "Window", sizeof(win->title) - 1);
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->flags = flags | WIN_FLAG_VISIBLE;

    win->saved_x = x;
    win->saved_y = y;
    win->saved_w = w;
    win->saved_h = h;

    update_content_area(win);

    s_z_order[s_num_windows] = slot;
    s_num_windows++;

    return win;
}

void wm_destroy_window(window_t *win) {
    if (!win || win->id == 0) return;

    if (win->on_close) {
        win->on_close(win);
    }

    int slot = (int)(win - s_windows);
    int z_idx = -1;
    for (int i = 0; i < s_num_windows; i++) {
        if (s_z_order[i] == slot) {
            z_idx = i;
            break;
        }
    }

    if (z_idx >= 0) {
        for (int i = z_idx; i < s_num_windows - 1; i++) {
            s_z_order[i] = s_z_order[i + 1];
        }
        s_num_windows--;
    }

    if (s_dragging_win == win) s_dragging_win = NULL;
    win->id = 0;
}

void wm_bring_to_front(window_t *win) {
    if (!win || win->id == 0 || s_num_windows <= 1) return;
    int slot = (int)(win - s_windows);

    int cur_pos = -1;
    for (int i = 0; i < s_num_windows; i++) {
        if (s_z_order[i] == slot) {
            cur_pos = i;
            break;
        }
    }
    if (cur_pos < 0 || cur_pos == s_num_windows - 1) return;

    for (int i = cur_pos; i < s_num_windows - 1; i++) {
        s_z_order[i] = s_z_order[i + 1];
    }
    s_z_order[s_num_windows - 1] = slot;
}

window_t *wm_get_active_window(void) {
    if (s_num_windows <= 0) return NULL;
    int top_slot = s_z_order[s_num_windows - 1];
    window_t *top = &s_windows[top_slot];
    if (top->flags & WIN_FLAG_VISIBLE && !(top->flags & WIN_FLAG_MINIMIZED)) {
        return top;
    }
    return NULL;
}

int wm_get_window_count(void) {
    return s_num_windows;
}

window_t *wm_get_window_by_index(int idx) {
    if (idx < 0 || idx >= s_num_windows) return NULL;
    return &s_windows[s_z_order[idx]];
}

void wm_minimize_window(window_t *win) {
    if (!win) return;
    win->flags |= WIN_FLAG_MINIMIZED;
    if (s_dragging_win == win) s_dragging_win = NULL;
}

void wm_maximize_window(window_t *win) {
    if (!win) return;
    if (win->flags & WIN_FLAG_MAXIMIZED) {
        wm_restore_window(win);
        return;
    }
    win->saved_x = win->x;
    win->saved_y = win->y;
    win->saved_w = win->w;
    win->saved_h = win->h;

    // Expand between Fedora GNOME Top Panel (y=28) and Taskbar (bottom 36px)
    win->x = 0;
    win->y = 28;
    win->w = g_width;
    win->h = g_height - 28 - 36;
    win->flags |= WIN_FLAG_MAXIMIZED;
    win->flags &= ~WIN_FLAG_MINIMIZED;
    update_content_area(win);
    wm_bring_to_front(win);
}

void wm_restore_window(window_t *win) {
    if (!win) return;
    if (win->flags & WIN_FLAG_MAXIMIZED) {
        win->x = win->saved_x;
        win->y = win->saved_y;
        win->w = win->saved_w;
        win->h = win->saved_h;
        win->flags &= ~WIN_FLAG_MAXIMIZED;
    }
    win->flags &= ~WIN_FLAG_MINIMIZED;
    win->flags |= WIN_FLAG_VISIBLE;
    update_content_area(win);
    wm_bring_to_front(win);
}

void wm_close_window(window_t *win) {
    wm_destroy_window(win);
}

static int is_inside(int px, int py, int x, int y, int w, int h) {
    return (px >= x && px < x + w && py >= y && py < y + h);
}

int wm_handle_mouse(const mouse_state_t *ms) {
    if (!ms) return 0;

    // Handle Active Dragging
    if (s_dragging_win) {
        if (!ms->left) {
            s_dragging_win = NULL; // Released mouse button
        } else {
            s_dragging_win->x = ms->x - s_drag_off_x;
            s_dragging_win->y = ms->y - s_drag_off_y;

            // Clamp so title bar cannot be completely dragged off-screen
            if (s_dragging_win->y < 28) s_dragging_win->y = 28; // Below top panel
            if (s_dragging_win->y > g_height - 64) s_dragging_win->y = g_height - 64;
            if (s_dragging_win->x < -(s_dragging_win->w - 60)) s_dragging_win->x = -(s_dragging_win->w - 60);
            if (s_dragging_win->x > g_width - 60) s_dragging_win->x = g_width - 60;

            update_content_area(s_dragging_win);
            return 1;
        }
    }

    // Traverse windows top-to-bottom for hit testing
    for (int i = s_num_windows - 1; i >= 0; i--) {
        int slot = s_z_order[i];
        window_t *win = &s_windows[slot];

        if (!(win->flags & WIN_FLAG_VISIBLE) || (win->flags & WIN_FLAG_MINIMIZED)) {
            continue;
        }

        if (is_inside(ms->x, ms->y, win->x, win->y, win->w, win->h)) {
            // Hit this window!
            if (ms->left_clicked) {
                wm_bring_to_front(win);
            }

            if (!(win->flags & WIN_FLAG_NO_BORDER)) {
                // Title bar buttons
                int btn_y = win->y + 4;
                int close_x = win->x + win->w - 22;
                int max_x = win->x + win->w - 42;
                int min_x = win->x + win->w - 62;

                if (ms->left_clicked) {
                    // Close Button [X]
                    if (is_inside(ms->x, ms->y, close_x, btn_y, 18, 16)) {
                        wm_close_window(win);
                        return 1;
                    }
                    // Maximize Button [+]
                    if (is_inside(ms->x, ms->y, max_x, btn_y, 18, 16)) {
                        wm_maximize_window(win);
                        return 1;
                    }
                    // Minimize Button [-]
                    if (is_inside(ms->x, ms->y, min_x, btn_y, 18, 16)) {
                        wm_minimize_window(win);
                        return 1;
                    }

                    // Title bar drag start
                    if (is_inside(ms->x, ms->y, win->x, win->y, win->w - 66, 24)) {
                        if (!(win->flags & WIN_FLAG_MAXIMIZED)) {
                            s_dragging_win = win;
                            s_drag_off_x = ms->x - win->x;
                            s_drag_off_y = ms->y - win->y;
                            return 1;
                        }
                    }
                }
            }

            // Click inside Content Area
            if (is_inside(ms->x, ms->y, win->content_x, win->content_y, win->content_w, win->content_h)) {
                int cx = ms->x - win->content_x;
                int cy = ms->y - win->content_y;

                if (ms->left_clicked && win->on_click) {
                    win->on_click(win, cx, cy, 1);
                } else if (ms->right_clicked && win->on_click) {
                    win->on_click(win, cx, cy, 2);
                }

                if (win->on_mouse_move) {
                    win->on_mouse_move(win, cx, cy, ms->left ? 1 : 0);
                }
            }

            return 1; // Event consumed by this window
        }
    }

    return 0; // Clicked on desktop background
}

void wm_handle_key(int key) {
    window_t *active = wm_get_active_window();
    if (active && active->on_key) {
        active->on_key(active, key);
    }
}

void wm_paint_all(void) {
    window_t *active = wm_get_active_window();

    for (int i = 0; i < s_num_windows; i++) {
        int slot = s_z_order[i];
        window_t *win = &s_windows[slot];

        if (!(win->flags & WIN_FLAG_VISIBLE) || (win->flags & WIN_FLAG_MINIMIZED)) {
            continue;
        }

        int is_act = (win == active);

        // 1. Subtle Window Drop Shadow (Right and Bottom)
        gfx_fill_rect(win->x + 4, win->y + win->h, win->w, 4, COLOR_RGB(20, 24, 30));
        gfx_fill_rect(win->x + win->w, win->y + 4, 4, win->h, COLOR_RGB(20, 24, 30));

        if (!(win->flags & WIN_FLAG_NO_BORDER)) {
            // 2. Window 3D Frame & Title Bar
            uint32_t border_col = is_act ? COLOR_XP_LUNA_BORDER : COLOR_MID_GRAY;
            gfx_draw_rect(win->x, win->y, win->w, win->h, border_col);
            gfx_draw_rect(win->x + 1, win->y + 1, win->w - 2, win->h - 2, COLOR_WHITE);

            // Title Bar Gradient (22 pixels high)
            uint32_t t_top = is_act ? COLOR_XP_LUNA_BLUE_TOP : COLOR_XP_LUNA_INACT_L;
            uint32_t t_bot = is_act ? COLOR_XP_LUNA_BLUE_BOT : COLOR_XP_LUNA_TITLE_INACT;
            gfx_gradient_v(win->x + 2, win->y + 2, win->w - 4, 22, t_top, t_bot);

            // Title Text with Drop Shadow
            gfx_draw_text_transparent(win->x + 11, win->y + 8, win->title, COLOR_RGB(20, 40, 80));
            gfx_draw_text_transparent(win->x + 10, win->y + 7, win->title, COLOR_WHITE);

            // Title Bar Buttons:
            int btn_y = win->y + 4;
            int close_x = win->x + win->w - 22;
            int max_x = win->x + win->w - 42;
            int min_x = win->x + win->w - 62;

            // [X] Close Button (Luna Red)
            gfx_gradient_v(close_x, btn_y, 18, 16, COLOR_XP_CLOSE_RED_TOP, COLOR_XP_CLOSE_RED_BOT);
            gfx_draw_rect_3d(close_x, btn_y, 18, 16, COLOR_WHITE, COLOR_RGB(80, 10, 10), 0);
            gfx_draw_icon(close_x + 3, btn_y + 3, ICON_CLOSE, 1);

            // [+] Maximize Button
            gfx_fill_rect(max_x, btn_y, 18, 16, COLOR_3D_FACE);
            gfx_draw_rect_3d(max_x, btn_y, 18, 16, COLOR_3D_LIGHT, COLOR_3D_SHADOW, 0);
            gfx_draw_icon(max_x + 3, btn_y + 3, ICON_MAXIMIZE, 1);

            // [-] Minimize Button
            gfx_fill_rect(min_x, btn_y, 18, 16, COLOR_3D_FACE);
            gfx_draw_rect_3d(min_x, btn_y, 18, 16, COLOR_3D_LIGHT, COLOR_3D_SHADOW, 0);
            gfx_draw_icon(min_x + 3, btn_y + 3, ICON_MINIMIZE, 1);
        }

        // 3. Render Window Content with Strict Clipping
        gfx_set_clip(win->content_x, win->content_y, win->content_x + win->content_w, win->content_y + win->content_h);

        // Fill background
        gfx_fill_rect(win->content_x, win->content_y, win->content_w, win->content_h, COLOR_XP_LUNA_BG);

        // Call Application Paint Callback
        if (win->on_paint) {
            win->on_paint(win);
        }

        // Reset Screen Clip
        gfx_reset_clip();
    }
}
