#ifndef THEME_H
#define THEME_H

#include <stdint.h>

#define THEME_WALLPAPER_COUNT 4
#define THEME_COUNT 4

typedef struct {
    int wallpaper_idx;
    int theme_idx;
    uint32_t accent_color;
} desktop_config_t;

extern desktop_config_t g_config;

void theme_init(void);
void theme_load_config(void);
int  theme_save_config(void);

const char *theme_get_wallpaper_name(int idx);
const char *theme_get_theme_name(int idx);

void theme_render_wallpaper(int wp_idx, int x, int y, int w, int h);

#endif // THEME_H
