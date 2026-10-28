#include "app_snake.h"
#include "graphics.h"
#include "auth.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern const kernel_api_t *g_kapi;

#define GRID_W 20
#define GRID_H 20
#define CELL_SZ 14

enum {
    SNAKE_READY = 0,
    SNAKE_RUNNING,
    SNAKE_PAUSED,
    SNAKE_GAMEOVER
};

typedef struct {
    int state;
    int body_x[GRID_W * GRID_H];
    int body_y[GRID_W * GRID_H];
    int len;
    int dir; // 0=Up, 1=Right, 2=Down, 3=Left
    int next_dir;
    int food_x;
    int food_y;
    int score;
    int high_score;
    uint32_t last_tick;
} snake_game_t;

static snake_game_t s_snake;

static void snake_spawn_food(void) {
    uint32_t ticks = (g_kapi && g_kapi->timer_ticks) ? *g_kapi->timer_ticks : 12345;
    s_snake.food_x = (ticks ^ (ticks >> 4)) % GRID_W;
    s_snake.food_y = (ticks * 7) % GRID_H;

    // Ensure food doesn't spawn on snake body
    for (int i = 0; i < s_snake.len; i++) {
        if (s_snake.body_x[i] == s_snake.food_x && s_snake.body_y[i] == s_snake.food_y) {
            s_snake.food_x = (s_snake.food_x + 1) % GRID_W;
            s_snake.food_y = (s_snake.food_y + 1) % GRID_H;
        }
    }
}

static void snake_reset(void) {
    s_snake.len = 4;
    s_snake.dir = 1; // Right
    s_snake.next_dir = 1;
    s_snake.score = 0;
    s_snake.state = SNAKE_READY;

    int start_x = GRID_W / 4;
    int start_y = GRID_H / 2;
    for (int i = 0; i < s_snake.len; i++) {
        s_snake.body_x[i] = start_x - i;
        s_snake.body_y[i] = start_y;
    }

    snake_spawn_food();
}

static void snake_step(void) {
    if (s_snake.state != SNAKE_RUNNING) return;

    s_snake.dir = s_snake.next_dir;

    int new_head_x = s_snake.body_x[0];
    int new_head_y = s_snake.body_y[0];

    if (s_snake.dir == 0) new_head_y--;      // Up
    else if (s_snake.dir == 1) new_head_x++; // Right
    else if (s_snake.dir == 2) new_head_y++; // Down
    else if (s_snake.dir == 3) new_head_x--; // Left

    // Check Wall Collision
    if (new_head_x < 0 || new_head_x >= GRID_W || new_head_y < 0 || new_head_y >= GRID_H) {
        s_snake.state = SNAKE_GAMEOVER;
        return;
    }

    // Check Self Collision
    for (int i = 0; i < s_snake.len; i++) {
        if (s_snake.body_x[i] == new_head_x && s_snake.body_y[i] == new_head_y) {
            s_snake.state = SNAKE_GAMEOVER;
            return;
        }
    }

    // Check Food Collision
    int ate = (new_head_x == s_snake.food_x && new_head_y == s_snake.food_y);

    if (ate) {
        s_snake.score += 10;
        if (s_snake.score > s_snake.high_score) {
            s_snake.high_score = s_snake.score;
        }
        if (s_snake.len < GRID_W * GRID_H - 1) {
            s_snake.len++;
        }
        snake_spawn_food();
    }

    // Shift body segments
    for (int i = s_snake.len - 1; i > 0; i--) {
        s_snake.body_x[i] = s_snake.body_x[i - 1];
        s_snake.body_y[i] = s_snake.body_y[i - 1];
    }
    s_snake.body_x[0] = new_head_x;
    s_snake.body_y[0] = new_head_y;
}

static void snake_on_paint(window_t *win) {
    int cx = win->content_x;
    int cy = win->content_y;
    int cw = win->content_w;
    int ch = win->content_h;

    // Advance game tick
    uint32_t now = (g_kapi && g_kapi->timer_ticks) ? *g_kapi->timer_ticks : 0;
    if (s_snake.state == SNAKE_RUNNING && (now - s_snake.last_tick) >= 12) { // ~120ms
        s_snake.last_tick = now;
        snake_step();
    }

    // 1. Grid Canvas Area (280 x 280)
    int grid_x = cx + 12;
    int grid_y = cy + 12;
    gfx_fill_rect(grid_x, grid_y, GRID_W * CELL_SZ, GRID_H * CELL_SZ, COLOR_RGB(18, 22, 28));
    gfx_draw_rect_3d(grid_x - 1, grid_y - 1, GRID_W * CELL_SZ + 2, GRID_H * CELL_SZ + 2, COLOR_3D_DARK, COLOR_3D_LIGHT, 1);

    // Subtle Grid Lines
    for (int r = 0; r <= GRID_H; r++) {
        gfx_draw_line(grid_x, grid_y + r * CELL_SZ, grid_x + GRID_W * CELL_SZ, grid_y + r * CELL_SZ, COLOR_RGB(25, 32, 40));
    }
    for (int c = 0; c <= GRID_W; c++) {
        gfx_draw_line(grid_x + c * CELL_SZ, grid_y, grid_x + c * CELL_SZ, grid_y + GRID_H * CELL_SZ, COLOR_RGB(25, 32, 40));
    }

    // Draw Food (Ruby Apple)
    int fx = grid_x + s_snake.food_x * CELL_SZ;
    int fy = grid_y + s_snake.food_y * CELL_SZ;
    gfx_fill_rect(fx + 2, fy + 2, CELL_SZ - 4, CELL_SZ - 4, COLOR_RED);
    gfx_fill_rect(fx + 4, fy + 4, 3, 3, COLOR_RGB(255, 180, 180)); // shine

    // Draw Snake Body
    for (int i = 0; i < s_snake.len; i++) {
        int bx = grid_x + s_snake.body_x[i] * CELL_SZ;
        int by = grid_y + s_snake.body_y[i] * CELL_SZ;
        if (i == 0) { // Head (Bright Green)
            gfx_fill_rect(bx + 1, by + 1, CELL_SZ - 2, CELL_SZ - 2, COLOR_RGB(80, 220, 80));
            gfx_draw_rect(bx + 1, by + 1, CELL_SZ - 2, CELL_SZ - 2, COLOR_RGB(40, 140, 40));
            // Eyes
            gfx_fill_rect(bx + 3, by + 3, 2, 2, COLOR_BLACK);
            gfx_fill_rect(bx + 9, by + 3, 2, 2, COLOR_BLACK);
        } else { // Body (Emerald Green)
            gfx_fill_rect(bx + 2, by + 2, CELL_SZ - 4, CELL_SZ - 4, COLOR_RGB(40, 160, 40));
            gfx_draw_rect(bx + 1, by + 1, CELL_SZ - 2, CELL_SZ - 2, COLOR_RGB(20, 100, 20));
        }
    }

    // Overlay Game Messages
    if (s_snake.state == SNAKE_READY) {
        gfx_fill_rect(grid_x + 50, grid_y + 110, 180, 50, COLOR_RGB(30, 36, 45));
        gfx_draw_rect(grid_x + 50, grid_y + 110, 180, 50, COLOR_WHITE);
        gfx_draw_text_transparent(grid_x + 65, grid_y + 120, "Aarambh Snake", COLOR_GREEN);
        gfx_draw_text_transparent(grid_x + 60, grid_y + 138, "Click Start to Play", COLOR_WHITE);
    } else if (s_snake.state == SNAKE_PAUSED) {
        gfx_fill_rect(grid_x + 70, grid_y + 110, 140, 40, COLOR_RGB(30, 36, 45));
        gfx_draw_rect(grid_x + 70, grid_y + 110, 140, 40, COLOR_YELLOW);
        gfx_draw_text_transparent(grid_x + 110, grid_y + 125, "PAUSED", COLOR_YELLOW);
    } else if (s_snake.state == SNAKE_GAMEOVER) {
        gfx_fill_rect(grid_x + 60, grid_y + 100, 160, 60, COLOR_RGB(30, 36, 45));
        gfx_draw_rect(grid_x + 60, grid_y + 100, 160, 60, COLOR_RED);
        gfx_draw_text_transparent(grid_x + 95, grid_y + 112, "GAME OVER", COLOR_RED);
        char fin_score[32];
        snprintf(fin_score, sizeof(fin_score), "Final Score: %d", s_snake.score);
        gfx_draw_text_transparent(grid_x + 75, grid_y + 132, fin_score, COLOR_WHITE);
    }

    // 2. Right Control Panel (cx+305..cx+cw)
    int px = cx + 305;
    int py = cy + 12;

    // Score Board
    gfx_fill_rect(px, py, 145, 60, COLOR_3D_FACE);
    gfx_draw_rect_3d(px, py, 145, 60, COLOR_3D_SHADOW, COLOR_3D_LIGHT, 1);
    char sc_str[32];
    snprintf(sc_str, sizeof(sc_str), "Score: %d", s_snake.score);
    gfx_draw_text_transparent(px + 12, py + 12, sc_str, COLOR_BLUE);

    char hs_str[32];
    snprintf(hs_str, sizeof(hs_str), "High:  %d", s_snake.high_score);
    gfx_draw_text_transparent(px + 12, py + 34, hs_str, COLOR_DARK_GRAY);

    // [ Start / Restart ] Button
    int btn_start_y = py + 72;
    gfx_gradient_v(px, btn_start_y, 145, 28, COLOR_XP_START_TOP, COLOR_XP_START_BOT);
    gfx_draw_rect_3d(px, btn_start_y, 145, 28, COLOR_WHITE, COLOR_NAVY, 0);
    const char *start_label = (s_snake.state == SNAKE_GAMEOVER) ? "Restart" : ((s_snake.state == SNAKE_RUNNING) ? "Pause" : "Start");
    gfx_draw_text_transparent(px + 48, btn_start_y + 8, start_label, COLOR_WHITE);

    // On-screen D-Pad Directional Controls for mouse
    int dpad_y = btn_start_y + 40;
    gfx_draw_text_transparent(px + 36, dpad_y, "D-Pad Controls", COLOR_DARK_GRAY);

    // Up [^]
    int up_x = px + 56, up_y = dpad_y + 16;
    gfx_fill_rect(up_x, up_y, 32, 24, COLOR_3D_FACE);
    gfx_draw_rect_3d(up_x, up_y, 32, 24, COLOR_3D_LIGHT, COLOR_3D_SHADOW, 0);
    gfx_draw_text_transparent(up_x + 12, up_y + 7, "^", COLOR_BLACK);

    // Left [<]
    int l_x = px + 20, l_y = dpad_y + 44;
    gfx_fill_rect(l_x, l_y, 32, 24, COLOR_3D_FACE);
    gfx_draw_rect_3d(l_x, l_y, 32, 24, COLOR_3D_LIGHT, COLOR_3D_SHADOW, 0);
    gfx_draw_text_transparent(l_x + 10, l_y + 7, "<", COLOR_BLACK);

    // Down [v]
    int dn_x = px + 56, dn_y = dpad_y + 44;
    gfx_fill_rect(dn_x, dn_y, 32, 24, COLOR_3D_FACE);
    gfx_draw_rect_3d(dn_x, dn_y, 32, 24, COLOR_3D_LIGHT, COLOR_3D_SHADOW, 0);
    gfx_draw_text_transparent(dn_x + 12, dn_y + 7, "v", COLOR_BLACK);

    // Right [>]
    int r_x = px + 92, r_y = dpad_y + 44;
    gfx_fill_rect(r_x, r_y, 32, 24, COLOR_3D_FACE);
    gfx_draw_rect_3d(r_x, r_y, 32, 24, COLOR_3D_LIGHT, COLOR_3D_SHADOW, 0);
    gfx_draw_text_transparent(r_x + 12, r_y + 7, ">", COLOR_BLACK);

    // Help Text
    gfx_draw_text_transparent(px + 4, dpad_y + 80, "Use Arrows or W/A/S/D", COLOR_MID_GRAY);
}

static void snake_on_click(window_t *win, int cx, int cy, int btn) {
    (void)win; (void)btn;
    int px = 305;
    int py = 12;

    // [ Start / Pause / Restart ] Button
    int btn_start_y = py + 72;
    if (cx >= px && cx < px + 145 && cy >= btn_start_y && cy < btn_start_y + 28) {
        if (s_snake.state == SNAKE_READY || s_snake.state == SNAKE_GAMEOVER) {
            snake_reset();
            s_snake.state = SNAKE_RUNNING;
            s_snake.last_tick = (g_kapi && g_kapi->timer_ticks) ? *g_kapi->timer_ticks : 0;
        } else if (s_snake.state == SNAKE_RUNNING) {
            s_snake.state = SNAKE_PAUSED;
        } else if (s_snake.state == SNAKE_PAUSED) {
            s_snake.state = SNAKE_RUNNING;
        }
        return;
    }

    // D-Pad Buttons
    int dpad_y = btn_start_y + 40;
    // Up
    if (cx >= px + 56 && cx < px + 88 && cy >= dpad_y + 16 && cy < dpad_y + 40) {
        if (s_snake.dir != 2) s_snake.next_dir = 0;
    }
    // Left
    else if (cx >= px + 20 && cx < px + 52 && cy >= dpad_y + 44 && cy < dpad_y + 68) {
        if (s_snake.dir != 1) s_snake.next_dir = 3;
    }
    // Down
    else if (cx >= px + 56 && cx < px + 88 && cy >= dpad_y + 44 && cy < dpad_y + 68) {
        if (s_snake.dir != 0) s_snake.next_dir = 2;
    }
    // Right
    else if (cx >= px + 92 && cx < px + 124 && cy >= dpad_y + 44 && cy < dpad_y + 68) {
        if (s_snake.dir != 3) s_snake.next_dir = 1;
    }
}

static void snake_on_key(window_t *win, int key) {
    (void)win;
    if (key == ' ' || key == 13) { // Space or Enter = Pause / Resume
        if (s_snake.state == SNAKE_RUNNING) s_snake.state = SNAKE_PAUSED;
        else if (s_snake.state == SNAKE_PAUSED) s_snake.state = SNAKE_RUNNING;
        else if (s_snake.state == SNAKE_READY || s_snake.state == SNAKE_GAMEOVER) {
            snake_reset();
            s_snake.state = SNAKE_RUNNING;
        }
    } else if (key == 'w' || key == 'W' || (uint8_t)key == 0x90) { // Up
        if (s_snake.dir != 2) s_snake.next_dir = 0;
    } else if (key == 'd' || key == 'D' || (uint8_t)key == 0x93) { // Right
        if (s_snake.dir != 3) s_snake.next_dir = 1;
    } else if (key == 's' || key == 'S' || (uint8_t)key == 0x91) { // Down
        if (s_snake.dir != 0) s_snake.next_dir = 2;
    } else if (key == 'a' || key == 'A' || (uint8_t)key == 0x92) { // Left
        if (s_snake.dir != 1) s_snake.next_dir = 3;
    }
}

window_t *app_snake_open(void) {
    snake_reset();

    window_t *win = wm_create_window("Aarambh Snake", 220, 100, 480, 340, 0);
    if (!win) return NULL;

    win->on_paint = snake_on_paint;
    win->on_click = snake_on_click;
    win->on_key = snake_on_key;

    return win;
}
