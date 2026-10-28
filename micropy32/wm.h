#ifndef WM_H
#define WM_H

#include <stdint.h>
#include <stddef.h>
#include "mouse.h"

#define MAX_WINDOWS 16

#define WIN_FLAG_VISIBLE    (1 << 0)
#define WIN_FLAG_MINIMIZED  (1 << 1)
#define WIN_FLAG_MAXIMIZED  (1 << 2)
#define WIN_FLAG_RESIZABLE  (1 << 3)
#define WIN_FLAG_MODAL      (1 << 4)
#define WIN_FLAG_NO_BORDER  (1 << 5)

struct window_t;

typedef struct window_t {
    int id;
    char title[64];
    int x;
    int y;
    int w;
    int h;
    uint32_t flags;

    // Saved bounds for restore after maximize
    int saved_x;
    int saved_y;
    int saved_w;
    int saved_h;

    // Computed content area coordinates
    int content_x;
    int content_y;
    int content_w;
    int content_h;

    // Event Callbacks
    void (*on_paint)(struct window_t *win);
    void (*on_click)(struct window_t *win, int cx, int cy, int btn);
    void (*on_mouse_move)(struct window_t *win, int cx, int cy, int btn);
    void (*on_key)(struct window_t *win, int key);
    void (*on_close)(struct window_t *win);

    void *user_data;
} window_t;

void wm_init(void);
window_t *wm_create_window(const char *title, int x, int y, int w, int h, uint32_t flags);
void wm_destroy_window(window_t *win);
void wm_bring_to_front(window_t *win);
window_t *wm_get_active_window(void);
int wm_get_window_count(void);
window_t *wm_get_window_by_index(int idx);

void wm_minimize_window(window_t *win);
void wm_maximize_window(window_t *win);
void wm_restore_window(window_t *win);
void wm_close_window(window_t *win);

// Event Handlers & Painting
int  wm_handle_mouse(const mouse_state_t *ms);
void wm_handle_key(int key);
void wm_paint_all(void);

#endif // WM_H
