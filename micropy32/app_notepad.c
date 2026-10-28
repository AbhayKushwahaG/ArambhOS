#include "app_notepad.h"
#include "graphics.h"
#include "auth.h"
#include <string.h>
#include <stdio.h>

extern const kernel_api_t *g_kapi;

#define NOTEPAD_MAX_TEXT 16384

typedef struct {
    char filename[64];
    char text[NOTEPAD_MAX_TEXT];
    size_t text_len;
    size_t cursor_pos;
    int scroll_line;
    int is_dirty;
    char status_msg[64];
} notepad_state_t;

static notepad_state_t s_notepad;

static void notepad_load_file(const char *fn) {
    s_notepad.text[0] = '\0';
    s_notepad.text_len = 0;
    s_notepad.cursor_pos = 0;
    s_notepad.scroll_line = 0;
    s_notepad.is_dirty = 0;

    if (!fn || !*fn) {
        strncpy(s_notepad.filename, "untitled.txt", sizeof(s_notepad.filename) - 1);
        snprintf(s_notepad.status_msg, sizeof(s_notepad.status_msg), "New Document");
        return;
    }

    strncpy(s_notepad.filename, fn, sizeof(s_notepad.filename) - 1);

    if (!g_kapi || !g_kapi->fs_find_child || !g_kapi->fs_read_file) {
        snprintf(s_notepad.status_msg, sizeof(s_notepad.status_msg), "Filesystem API unavailable");
        return;
    }

    uint32_t file_id = g_kapi->fs_find_child(0, fn);
    if (!file_id) {
        snprintf(s_notepad.status_msg, sizeof(s_notepad.status_msg), "New File: %s", fn);
        return;
    }

    uint32_t bytes_read = 0;
    if (g_kapi->fs_read_file(file_id, s_notepad.text, &bytes_read) == 0 && bytes_read > 0) {
        if (bytes_read >= NOTEPAD_MAX_TEXT) bytes_read = NOTEPAD_MAX_TEXT - 1;
        s_notepad.text[bytes_read] = '\0';
        s_notepad.text_len = bytes_read;
        snprintf(s_notepad.status_msg, sizeof(s_notepad.status_msg), "Loaded %u bytes", (unsigned)bytes_read);
    } else {
        snprintf(s_notepad.status_msg, sizeof(s_notepad.status_msg), "Error reading %s", fn);
    }
}

static void notepad_save_file(void) {
    if (!g_kapi || !g_kapi->fs_find_child || !g_kapi->fs_create_entry || !g_kapi->fs_write_file) {
        snprintf(s_notepad.status_msg, sizeof(s_notepad.status_msg), "Save failed: No FS API");
        return;
    }

    if (!s_notepad.filename[0]) {
        strncpy(s_notepad.filename, "untitled.txt", sizeof(s_notepad.filename) - 1);
    }

    uint32_t file_id = g_kapi->fs_find_child(0, s_notepad.filename);
    if (!file_id) {
        file_id = g_kapi->fs_create_entry(s_notepad.filename, 1, 0); // 1 = regular file
    }

    if (!file_id) {
        snprintf(s_notepad.status_msg, sizeof(s_notepad.status_msg), "Save failed: create entry error");
        return;
    }

    if (g_kapi->fs_write_file(file_id, s_notepad.text, s_notepad.text_len) == 0) {
        if (g_kapi->fs_commit) g_kapi->fs_commit();
        s_notepad.is_dirty = 0;
        snprintf(s_notepad.status_msg, sizeof(s_notepad.status_msg), "Saved to %s (%u bytes)", s_notepad.filename, (unsigned)s_notepad.text_len);
    } else {
        snprintf(s_notepad.status_msg, sizeof(s_notepad.status_msg), "Write error to %s", s_notepad.filename);
    }
}

static void notepad_on_paint(window_t *win) {
    int cx = win->content_x;
    int cy = win->content_y;
    int cw = win->content_w;
    int ch = win->content_h;

    // 1. Menu Bar (Height: 24px)
    gfx_fill_rect(cx, cy, cw, 24, COLOR_3D_FACE);
    gfx_draw_line(cx, cy + 23, cx + cw, cy + 23, COLOR_3D_DARK);

    // Menu Buttons: [ New ] [ Save ] [ Clear ]
    int b_new_x = cx + 6;
    gfx_fill_rect(b_new_x, cy + 3, 50, 18, COLOR_3D_FACE);
    gfx_draw_rect_3d(b_new_x, cy + 3, 50, 18, COLOR_3D_LIGHT, COLOR_3D_SHADOW, 0);
    gfx_draw_text_transparent(b_new_x + 12, cy + 7, "New", COLOR_BLACK);

    int b_save_x = cx + 62;
    gfx_fill_rect(b_save_x, cy + 3, 50, 18, COLOR_3D_FACE);
    gfx_draw_rect_3d(b_save_x, cy + 3, 50, 18, COLOR_3D_LIGHT, COLOR_3D_SHADOW, 0);
    gfx_draw_text_transparent(b_save_x + 10, cy + 7, "Save", COLOR_BLACK);

    int b_clr_x = cx + 118;
    gfx_fill_rect(b_clr_x, cy + 3, 50, 18, COLOR_3D_FACE);
    gfx_draw_rect_3d(b_clr_x, cy + 3, 50, 18, COLOR_3D_LIGHT, COLOR_3D_SHADOW, 0);
    gfx_draw_text_transparent(b_clr_x + 8, cy + 7, "Clear", COLOR_BLACK);

    // Current File Info
    char file_hdr[128];
    snprintf(file_hdr, sizeof(file_hdr), "File: %s%s", s_notepad.filename, s_notepad.is_dirty ? " *" : "");
    gfx_draw_text_transparent(cx + 180, cy + 7, file_hdr, COLOR_DARK_GRAY);

    // 2. Status Bar (Height: 20px at bottom)
    int status_y = cy + ch - 20;
    gfx_fill_rect(cx, status_y, cw, 20, COLOR_3D_FACE);
    gfx_draw_line(cx, status_y, cx + cw, status_y, COLOR_3D_SHADOW);

    // Compute cursor line and column
    int cur_line = 1;
    int cur_col = 1;
    for (size_t i = 0; i < s_notepad.cursor_pos && i < s_notepad.text_len; i++) {
        if (s_notepad.text[i] == '\n') {
            cur_line++;
            cur_col = 1;
        } else {
            cur_col++;
        }
    }

    char stat_text[128];
    snprintf(stat_text, sizeof(stat_text), "%s | Ln %d, Col %d | %u chars", s_notepad.status_msg, cur_line, cur_col, (unsigned)s_notepad.text_len);
    gfx_draw_text_transparent(cx + 8, status_y + 5, stat_text, COLOR_BLACK);

    // 3. Text Editing Area
    int edit_y = cy + 24;
    int edit_h = ch - 44;
    gfx_fill_rect(cx, edit_y, cw, edit_h, COLOR_WHITE);

    // Render Text Lines
    int visible_lines = edit_h / 14;
    int line_num = 0;
    int line_start = 0;

    int cur_draw_x = cx + 8;
    int cur_draw_y = edit_y + 4;
    int found_cursor = 0;
    int cursor_screen_x = cx + 8;
    int cursor_screen_y = edit_y + 4;

    size_t pos = 0;
    while (pos <= s_notepad.text_len) {
        if (pos == s_notepad.cursor_pos) {
            cursor_screen_x = cur_draw_x;
            cursor_screen_y = cur_draw_y;
            found_cursor = 1;
        }

        if (pos == s_notepad.text_len) break;

        char c = s_notepad.text[pos++];
        if (c == '\n') {
            line_num++;
            cur_draw_x = cx + 8;
            cur_draw_y += 14;
        } else if (c == '\t') {
            cur_draw_x += 32; // 4 spaces
        } else if (c >= 32 && c <= 126) {
            if (cur_draw_y >= edit_y && cur_draw_y < edit_y + edit_h - 12) {
                if (cur_draw_x < cx + cw - 16) {
                    gfx_draw_char(cur_draw_x, cur_draw_y, c, COLOR_BLACK, COLOR_WHITE, 1);
                }
            }
            cur_draw_x += 8;
        }
    }

    // Blinking Text Cursor
    uint32_t ticks = (g_kapi && g_kapi->timer_ticks) ? *g_kapi->timer_ticks : 0;
    if (found_cursor && (ticks / 30) % 2 == 0) {
        if (cursor_screen_y >= edit_y && cursor_screen_y < edit_y + edit_h - 12) {
            gfx_fill_rect(cursor_screen_x, cursor_screen_y, 2, 12, COLOR_BLACK);
        }
    }
}

static void notepad_on_click(window_t *win, int cx, int cy, int btn) {
    (void)btn;
    // Menu Bar clicks (cy < 24)
    if (cy < 24) {
        if (cx >= 6 && cx < 56) { // New
            notepad_load_file("");
        } else if (cx >= 62 && cx < 112) { // Save
            notepad_save_file();
        } else if (cx >= 118 && cx < 168) { // Clear
            s_notepad.text[0] = '\0';
            s_notepad.text_len = 0;
            s_notepad.cursor_pos = 0;
            s_notepad.is_dirty = 1;
            snprintf(s_notepad.status_msg, sizeof(s_notepad.status_msg), "Cleared");
        }
        return;
    }

    // Click in text editing area
    if (cy >= 24 && cy < win->content_h - 20) {
        int clicked_line = (cy - 28) / 14;
        int clicked_col = (cx - 8) / 8;
        if (clicked_line < 0) clicked_line = 0;
        if (clicked_col < 0) clicked_col = 0;

        // Find nearest text position
        int cur_l = 0;
        size_t line_start_pos = 0;
        for (size_t i = 0; i < s_notepad.text_len; i++) {
            if (cur_l == clicked_line) {
                size_t target = line_start_pos + clicked_col;
                if (target > s_notepad.text_len) target = s_notepad.text_len;
                s_notepad.cursor_pos = target;
                return;
            }
            if (s_notepad.text[i] == '\n') {
                cur_l++;
                line_start_pos = i + 1;
            }
        }
        s_notepad.cursor_pos = s_notepad.text_len;
    }
}

static void notepad_on_key(window_t *win, int key) {
    (void)win;
    if (key == 8) { // Backspace
        if (s_notepad.cursor_pos > 0) {
            memmove(&s_notepad.text[s_notepad.cursor_pos - 1],
                    &s_notepad.text[s_notepad.cursor_pos],
                    s_notepad.text_len - s_notepad.cursor_pos + 1);
            s_notepad.cursor_pos--;
            s_notepad.text_len--;
            s_notepad.is_dirty = 1;
        }
    } else if (key == 127) { // Delete
        if (s_notepad.cursor_pos < s_notepad.text_len) {
            memmove(&s_notepad.text[s_notepad.cursor_pos],
                    &s_notepad.text[s_notepad.cursor_pos + 1],
                    s_notepad.text_len - s_notepad.cursor_pos);
            s_notepad.text_len--;
            s_notepad.is_dirty = 1;
        }
    } else if (key == 13) { // Enter
        if (s_notepad.text_len < NOTEPAD_MAX_TEXT - 2) {
            memmove(&s_notepad.text[s_notepad.cursor_pos + 1],
                    &s_notepad.text[s_notepad.cursor_pos],
                    s_notepad.text_len - s_notepad.cursor_pos + 1);
            s_notepad.text[s_notepad.cursor_pos++] = '\n';
            s_notepad.text_len++;
            s_notepad.is_dirty = 1;
        }
    } else if (key == 9) { // Tab
        if (s_notepad.text_len < NOTEPAD_MAX_TEXT - 5) {
            for (int i = 0; i < 4; i++) {
                memmove(&s_notepad.text[s_notepad.cursor_pos + 1],
                        &s_notepad.text[s_notepad.cursor_pos],
                        s_notepad.text_len - s_notepad.cursor_pos + 1);
                s_notepad.text[s_notepad.cursor_pos++] = ' ';
                s_notepad.text_len++;
            }
            s_notepad.is_dirty = 1;
        }
    } else if ((uint8_t)key == 0x92) { // Left arrow
        if (s_notepad.cursor_pos > 0) s_notepad.cursor_pos--;
    } else if ((uint8_t)key == 0x93) { // Right arrow
        if (s_notepad.cursor_pos < s_notepad.text_len) s_notepad.cursor_pos++;
    } else if (key >= 32 && key <= 126) {
        if (s_notepad.text_len < NOTEPAD_MAX_TEXT - 2) {
            memmove(&s_notepad.text[s_notepad.cursor_pos + 1],
                    &s_notepad.text[s_notepad.cursor_pos],
                    s_notepad.text_len - s_notepad.cursor_pos + 1);
            s_notepad.text[s_notepad.cursor_pos++] = (char)key;
            s_notepad.text_len++;
            s_notepad.is_dirty = 1;
        }
    }
}

window_t *app_notepad_open(const char *filename) {
    notepad_load_file(filename);

    char title[128];
    snprintf(title, sizeof(title), "Notepad - %s", s_notepad.filename);

    window_t *win = wm_create_window(title, 140, 70, 560, 420, WIN_FLAG_RESIZABLE);
    if (!win) return NULL;

    win->on_paint = notepad_on_paint;
    win->on_click = notepad_on_click;
    win->on_key = notepad_on_key;

    return win;
}
