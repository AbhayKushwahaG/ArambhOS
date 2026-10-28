#include "mouse.h"
#include "graphics.h"
#include "pci.h"
#include "auth.h"

extern const kernel_api_t *g_kapi;

static mouse_state_t s_mouse = {512, 384, 0, 0, 0, 0, 0, 0};
static int s_max_w = 1024;
static int s_max_h = 768;

static uint8_t s_packet[3];
static int s_packet_idx = 0;
static uint32_t s_last_click_tick = 0;

static void mouse_wait_write(void) {
    int timeout = 100000;
    while ((inb(0x64) & 0x02) && --timeout) io_wait();
}

static void mouse_wait_read(void) {
    int timeout = 100000;
    while (!(inb(0x64) & 0x01) && --timeout) io_wait();
}

static void mouse_write_cmd(uint8_t byte) {
    mouse_wait_write();
    outb(0x64, 0xD4); // Tell 8042: route next data byte to mouse
    mouse_wait_write();
    outb(0x60, byte);
}

static uint8_t mouse_read_data(void) {
    mouse_wait_read();
    return inb(0x60);
}

void mouse_init(int max_w, int max_h) {
    mouse_set_bounds(max_w, max_h);
    s_mouse.x = max_w / 2;
    s_mouse.y = max_h / 2;
    s_mouse.left = 0;
    s_mouse.right = 0;
    s_mouse.middle = 0;
    s_mouse.left_clicked = 0;
    s_mouse.right_clicked = 0;
    s_mouse.double_clicked = 0;
    s_packet_idx = 0;

    // 1. Enable auxiliary (mouse) device in 8042 controller
    mouse_wait_write();
    outb(0x64, 0xA8);

    // 2. Read controller command byte
    mouse_wait_write();
    outb(0x64, 0x20);
    uint8_t status = mouse_read_data();

    // Enable IRQ12 interrupt (bit 1) and enable mouse clock (clear bit 5)
    status |= 0x02;
    status &= ~0x20;

    // 3. Write back controller command byte
    mouse_wait_write();
    outb(0x64, 0x60);
    mouse_wait_write();
    outb(0x60, status);

    // 4. Set mouse to default sampling / streaming
    mouse_write_cmd(0xF6);
    mouse_read_data(); // ACK (0xFA)

    // 5. Enable data streaming
    mouse_write_cmd(0xF4);
    mouse_read_data(); // ACK (0xFA)
}

void mouse_set_bounds(int max_w, int max_h) {
    s_max_w = (max_w > 0) ? max_w : 1024;
    s_max_h = (max_h > 0) ? max_h : 768;
    if (s_mouse.x >= s_max_w) s_mouse.x = s_max_w - 1;
    if (s_mouse.y >= s_max_h) s_mouse.y = s_max_h - 1;
}

void mouse_set_pos(int x, int y) {
    if (x < 0) x = 0;
    if (x >= s_max_w) x = s_max_w - 1;
    if (y < 0) y = 0;
    if (y >= s_max_h) y = s_max_h - 1;
    s_mouse.x = x;
    s_mouse.y = y;
}

const mouse_state_t *mouse_get_state(void) {
    return &s_mouse;
}

void mouse_poll(void) {
    s_mouse.left_clicked = 0;
    s_mouse.right_clicked = 0;
    s_mouse.double_clicked = 0;

    if (!g_kapi || !g_kapi->mouse_read) return;

    // Process all bytes currently in the kernel ring buffer
    while (1) {
        int b = g_kapi->mouse_read();
        if (b < 0) break; // Ring buffer empty

        uint8_t byte = (uint8_t)b;

        // Packet Synchronization: byte 0 must have bit 3 set to 1
        if (s_packet_idx == 0) {
            if (!(byte & 0x08)) {
                // Out of sync byte, discard
                continue;
            }
        }

        s_packet[s_packet_idx++] = byte;

        if (s_packet_idx == 3) {
            // Complete 3-byte PS/2 packet received!
            s_packet_idx = 0;

            uint8_t flags = s_packet[0];
            int rel_x = (int8_t)s_packet[1];
            int rel_y = (int8_t)s_packet[2];

            // Ignore packet if overflow occurred
            if (!(flags & 0xC0)) {
                // In PS/2, positive rel_y means mouse moved UP (decreasing Y on screen)
                s_mouse.x += rel_x;
                s_mouse.y -= rel_y;

                if (s_mouse.x < 0) s_mouse.x = 0;
                if (s_mouse.x >= s_max_w) s_mouse.x = s_max_w - 1;
                if (s_mouse.y < 0) s_mouse.y = 0;
                if (s_mouse.y >= s_max_h) s_mouse.y = s_max_h - 1;
            }

            int new_left = (flags & 0x01) ? 1 : 0;
            int new_right = (flags & 0x02) ? 1 : 0;
            int new_mid = (flags & 0x04) ? 1 : 0;

            // Single and Double Click Detection
            if (new_left && !s_mouse.left) {
                s_mouse.left_clicked = 1;
                uint32_t now = (g_kapi && g_kapi->timer_ticks) ? *g_kapi->timer_ticks : 0;
                if (now > s_last_click_tick && (now - s_last_click_tick) <= 35) { // 350 ms
                    s_mouse.double_clicked = 1;
                }
                s_last_click_tick = now;
            }
            if (new_right && !s_mouse.right) {
                s_mouse.right_clicked = 1;
            }

            s_mouse.left = new_left;
            s_mouse.right = new_right;
            s_mouse.middle = new_mid;
        }
    }
}

// Crisp High-Resolution 12x18 Arrow Cursor
// 'X' = Black Border, '.' = Pure White Fill, 'S' = Shadow, ' ' = Transparent
static const char *s_cursor_map[18] = {
    "X           ",
    "XX          ",
    "X.X         ",
    "X..X        ",
    "X...X       ",
    "X....X      ",
    "X.....X     ",
    "X......X    ",
    "X.......X   ",
    "X........X  ",
    "X.....XXXXX ",
    "X..X..X     ",
    "X.X X..X    ",
    "XX   X..X   ",
    "X    X..X   ",
    "      X..X  ",
    "      X..X  ",
    "       XX   "
};

void mouse_draw(void) {
    int mx = s_mouse.x;
    int my = s_mouse.y;

    // Draw subtle drop shadow first (offset +1, +1)
    for (int j = 0; j < 18; j++) {
        const char *row = s_cursor_map[j];
        for (int i = 0; i < 12; i++) {
            if (row[i] == 'X' || row[i] == '.') {
                gfx_put_pixel(mx + i + 1, my + j + 1, COLOR_RGB(30, 30, 30));
            }
        }
    }

    // Draw main cursor (White interior with crisp Black outline)
    for (int j = 0; j < 18; j++) {
        const char *row = s_cursor_map[j];
        for (int i = 0; i < 12; i++) {
            char p = row[i];
            if (p == 'X') {
                gfx_put_pixel(mx + i, my + j, COLOR_BLACK);
            } else if (p == '.') {
                gfx_put_pixel(mx + i, my + j, COLOR_WHITE);
            }
        }
    }
}
