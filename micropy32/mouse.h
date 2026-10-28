#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

typedef struct {
    int x;
    int y;
    int left;           // 1 if button is held down
    int right;
    int middle;
    int left_clicked;   // 1 on the single frame button was pressed
    int right_clicked;
    int double_clicked; // 1 if double clicked
} mouse_state_t;

void mouse_init(int max_w, int max_h);
void mouse_set_bounds(int max_w, int max_h);
void mouse_set_pos(int x, int y);
void mouse_poll(void);
const mouse_state_t *mouse_get_state(void);
void mouse_draw(void);

#endif // MOUSE_H
