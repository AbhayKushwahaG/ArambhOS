#include "app_filemgr.h"
#include "app_notepad.h"
#include "app_paint.h"
#include "graphics.h"
#include "auth.h"
#include <string.h>
#include <stdio.h>

extern const kernel_api_t *g_kapi;
extern int cmd_run(const char *args);

typedef struct {
    uint32_t id;
    char name[44];
    uint8_t type;
    uint32_t size;
    uint16_t mode;
} filemgr_item_t;

typedef struct {
    uint32_t current_dir_id;
    char current_path[128];
    filemgr_item_t items[128];
    int item_count;
    int selected_idx;
    char status_msg[64];
} filemgr_state_t;

static filemgr_state_t s_fm;

static void filemgr_scan(void) {
    s_fm.item_count = 0;
    s_fm.selected_idx = -1;

    if (!g_kapi || !g_kapi->fs_entry_ptr) {
        snprintf(s_fm.status_msg, sizeof(s_fm.status_msg), "AarFS Unavailable");
        return;
    }

    // Add [..] entry if not at root
    if (s_fm.current_dir_id != 0) {
        aarfs_entry_t *cur = (aarfs_entry_t *)g_kapi->fs_entry_ptr(s_fm.current_dir_id);
        uint32_t parent = cur ? cur->parent_id : 0;
        filemgr_item_t *it = &s_fm.items[s_fm.item_count++];
        it->id = parent;
        strncpy(it->name, "..", sizeof(it->name));
        it->type = 2; // Directory
        it->size = 0;
        it->mode = 0755;
    }

    // Scan all 2048 AarFS entries
    for (uint32_t i = 1; i <= 2048 && s_fm.item_count < 128; i++) {
        aarfs_entry_t *e = (aarfs_entry_t *)g_kapi->fs_entry_ptr(i);
        if (e && e->status == 1 && e->parent_id == s_fm.current_dir_id) {
            filemgr_item_t *it = &s_fm.items[s_fm.item_count++];
            it->id = i;
            strncpy(it->name, e->name, sizeof(it->name) - 1);
            it->name[sizeof(it->name) - 1] = '\0';
            it->type = e->type;
            it->size = e->size;
            it->mode = e->mode;
        }
    }

    snprintf(s_fm.status_msg, sizeof(s_fm.status_msg), "%d items in %s", s_fm.item_count, s_fm.current_path);
}

static void filemgr_open_item(int idx) {
    if (idx < 0 || idx >= s_fm.item_count) return;
    filemgr_item_t *it = &s_fm.items[idx];

    if (it->type == 2) { // Directory
        if (strcmp(it->name, "..") == 0) {
            s_fm.current_dir_id = it->id;
            // Trim last path component
            char *last_slash = strrchr(s_fm.current_path, '/');
            if (last_slash && last_slash != s_fm.current_path) {
                *last_slash = '\0';
            } else {
                strcpy(s_fm.current_path, "/");
            }
        } else {
            s_fm.current_dir_id = it->id;
            if (strcmp(s_fm.current_path, "/") != 0) {
                strncat(s_fm.current_path, "/", sizeof(s_fm.current_path) - 1);
            }
            strncat(s_fm.current_path, it->name, sizeof(s_fm.current_path) - 1);
        }
        filemgr_scan();
    } else { // File
        // File associations based on extension
        const char *dot = strrchr(it->name, '.');
        if (dot && strcmp(dot, ".bmp") == 0) {
            app_paint_open(it->name);
        } else if (dot && strcmp(dot, ".sh") == 0) {
            cmd_run(it->name);
            snprintf(s_fm.status_msg, sizeof(s_fm.status_msg), "Executed %s", it->name);
        } else {
            app_notepad_open(it->name);
        }
    }
}

static void filemgr_on_paint(window_t *win) {
    int cx = win->content_x;
    int cy = win->content_y;
    int cw = win->content_w;
    int ch = win->content_h;

    // 1. Path & Navigation Bar (Height: 28px)
    gfx_fill_rect(cx, cy, cw, 28, COLOR_3D_FACE);
    gfx_draw_line(cx, cy + 27, cx + cw, cy + 27, COLOR_3D_DARK);

    // [ Up ] Button
    gfx_fill_rect(cx + 6, cy + 4, 36, 20, COLOR_3D_FACE);
    gfx_draw_rect_3d(cx + 6, cy + 4, 36, 20, COLOR_3D_LIGHT, COLOR_3D_SHADOW, 0);
    gfx_draw_text_transparent(cx + 12, cy + 8, "Up", COLOR_BLACK);

    // [ Refresh ] Button
    gfx_fill_rect(cx + 46, cy + 4, 60, 20, COLOR_3D_FACE);
    gfx_draw_rect_3d(cx + 46, cy + 4, 60, 20, COLOR_3D_LIGHT, COLOR_3D_SHADOW, 0);
    gfx_draw_text_transparent(cx + 52, cy + 8, "Refresh", COLOR_BLACK);

    // [ New ] Button
    gfx_fill_rect(cx + 110, cy + 4, 50, 20, COLOR_3D_FACE);
    gfx_draw_rect_3d(cx + 110, cy + 4, 50, 20, COLOR_3D_LIGHT, COLOR_3D_SHADOW, 0);
    gfx_draw_text_transparent(cx + 118, cy + 8, "New", COLOR_BLACK);

    // [ Delete ] Button
    gfx_fill_rect(cx + 164, cy + 4, 56, 20, COLOR_3D_FACE);
    gfx_draw_rect_3d(cx + 164, cy + 4, 56, 20, COLOR_3D_LIGHT, COLOR_3D_SHADOW, 0);
    gfx_draw_text_transparent(cx + 170, cy + 8, "Delete", COLOR_RED);

    // Current Path Box
    int path_x = cx + 228;
    int path_w = cw - 234;
    gfx_fill_rect(path_x, cy + 4, path_w, 20, COLOR_WHITE);
    gfx_draw_rect_3d(path_x, cy + 4, path_w, 20, COLOR_3D_DARK, COLOR_3D_LIGHT, 1);
    gfx_draw_text_transparent(path_x + 6, cy + 8, s_fm.current_path, COLOR_BLACK);

    // 2. Status Bar at bottom (Height: 20px)
    int stat_y = cy + ch - 20;
    gfx_fill_rect(cx, stat_y, cw, 20, COLOR_3D_FACE);
    gfx_draw_line(cx, stat_y, cx + cw, stat_y, COLOR_3D_SHADOW);
    gfx_draw_text_transparent(cx + 8, stat_y + 5, s_fm.status_msg, COLOR_BLACK);

    // 3. File Listing Area (List View with Icons)
    int list_y = cy + 28;
    int list_h = ch - 48;
    gfx_fill_rect(cx, list_y, cw, list_h, COLOR_WHITE);

    // Columns Header
    gfx_fill_rect(cx, list_y, cw, 18, COLOR_VERY_LIGHT_GRAY);
    gfx_draw_line(cx, list_y + 17, cx + cw, list_y + 17, COLOR_MID_GRAY);
    gfx_draw_text_transparent(cx + 36, list_y + 4, "Name", COLOR_DARK_GRAY);
    gfx_draw_text_transparent(cx + 260, list_y + 4, "Type", COLOR_DARK_GRAY);
    gfx_draw_text_transparent(cx + 350, list_y + 4, "Size", COLOR_DARK_GRAY);
    gfx_draw_text_transparent(cx + 430, list_y + 4, "Permissions", COLOR_DARK_GRAY);

    // Render Items
    int item_start_y = list_y + 20;
    for (int i = 0; i < s_fm.item_count; i++) {
        int iy = item_start_y + i * 22;
        if (iy + 20 > list_y + list_h) break;

        filemgr_item_t *it = &s_fm.items[i];
        int is_sel = (s_fm.selected_idx == i);

        if (is_sel) {
            gfx_fill_rect(cx + 2, iy, cw - 4, 20, COLOR_RGB(53, 132, 228));
        }

        uint32_t text_col = is_sel ? COLOR_WHITE : COLOR_BLACK;

        // Draw Icon
        if (it->type == 2) {
            gfx_draw_icon(cx + 8, iy + 2, ICON_EXPLORER, 1);
        } else {
            const char *dot = strrchr(it->name, '.');
            if (dot && strcmp(dot, ".bmp") == 0) {
                gfx_draw_icon(cx + 8, iy + 2, ICON_PAINT, 1);
            } else if (dot && strcmp(dot, ".sh") == 0) {
                gfx_draw_icon(cx + 8, iy + 2, ICON_SCRIPT, 1);
            } else if (dot && strcmp(dot, ".deb") == 0) {
                gfx_draw_icon(cx + 8, iy + 2, ICON_PACKAGE, 1);
            } else {
                gfx_draw_icon(cx + 8, iy + 2, ICON_NOTEPAD, 1);
            }
        }

        // Name
        gfx_draw_text_transparent(cx + 36, iy + 5, it->name, text_col);

        // Type
        const char *t_str = (it->type == 2) ? "Directory" : "File";
        gfx_draw_text_transparent(cx + 260, iy + 5, t_str, text_col);

        // Size
        char sz_str[32];
        if (it->type == 2) {
            sz_str[0] = '\0';
        } else if (it->size < 1024) {
            snprintf(sz_str, sizeof(sz_str), "%u B", (unsigned)it->size);
        } else {
            snprintf(sz_str, sizeof(sz_str), "%u KiB", (unsigned)(it->size / 1024));
        }
        gfx_draw_text_transparent(cx + 350, iy + 5, sz_str, text_col);

        // Permissions Mode
        char mode_str[16];
        snprintf(mode_str, sizeof(mode_str), "0%03o", it->mode ? it->mode : (it->type == 2 ? 0755 : 0644));
        gfx_draw_text_transparent(cx + 430, iy + 5, mode_str, text_col);
    }
}

static void filemgr_on_click(window_t *win, int cx, int cy, int btn) {
    (void)btn;
    // Top Bar Clicks (cy < 28)
    if (cy < 28) {
        if (cx >= 6 && cx < 42) { // Up
            if (s_fm.current_dir_id != 0) {
                aarfs_entry_t *cur = (aarfs_entry_t *)g_kapi->fs_entry_ptr(s_fm.current_dir_id);
                s_fm.current_dir_id = cur ? cur->parent_id : 0;
                char *last_slash = strrchr(s_fm.current_path, '/');
                if (last_slash && last_slash != s_fm.current_path) {
                    *last_slash = '\0';
                } else {
                    strcpy(s_fm.current_path, "/");
                }
                filemgr_scan();
            }
        } else if (cx >= 46 && cx < 106) { // Refresh
            filemgr_scan();
        } else if (cx >= 110 && cx < 160) { // New File
            app_notepad_open("");
        } else if (cx >= 164 && cx < 220) { // Delete
            if (s_fm.selected_idx >= 0 && s_fm.selected_idx < s_fm.item_count) {
                filemgr_item_t *it = &s_fm.items[s_fm.selected_idx];
                aarfs_entry_t *e = (aarfs_entry_t *)g_kapi->fs_entry_ptr(it->id);
                if (e) {
                    e->status = 0; // free entry
                    if (g_kapi->fs_commit) g_kapi->fs_commit();
                    snprintf(s_fm.status_msg, sizeof(s_fm.status_msg), "Deleted %s", it->name);
                    filemgr_scan();
                }
            }
        }
        return;
    }

    // Click in Item List
    int list_y = 28 + 20;
    if (cy >= list_y && cy < win->content_h - 20) {
        int idx = (cy - list_y) / 22;
        if (idx >= 0 && idx < s_fm.item_count) {
            const mouse_state_t *ms = mouse_get_state();
            if (ms->double_clicked || (s_fm.selected_idx == idx)) {
                // Open item!
                filemgr_open_item(idx);
            } else {
                s_fm.selected_idx = idx;
                filemgr_item_t *it = &s_fm.items[idx];
                snprintf(s_fm.status_msg, sizeof(s_fm.status_msg), "%s (%u bytes)", it->name, (unsigned)it->size);
            }
        }
    }
}

window_t *app_filemgr_open(const char *initial_path) {
    s_fm.current_dir_id = 0; // root
    strncpy(s_fm.current_path, (initial_path && *initial_path) ? initial_path : "/", sizeof(s_fm.current_path) - 1);
    filemgr_scan();

    window_t *win = wm_create_window("File Explorer", 120, 60, 560, 420, WIN_FLAG_RESIZABLE);
    if (!win) return NULL;

    win->on_paint = filemgr_on_paint;
    win->on_click = filemgr_on_click;

    return win;
}
