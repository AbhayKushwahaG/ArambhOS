#include "net_driver.h"
#include <string.h>

#define REG_CTRL     0x0000
#define REG_STATUS   0x0008
#define REG_EECD     0x0010
#define REG_EERD     0x0014
#define REG_ICR      0x00C0
#define REG_IMS      0x00D0
#define REG_RCTL     0x0100
#define REG_TCTL     0x0400
#define REG_TIPG     0x0410
#define REG_RDBAL    0x2800
#define REG_RDBAH    0x2804
#define REG_RDLEN    0x2808
#define REG_RDH      0x2810
#define REG_RDT      0x2818
#define REG_TDBAL    0x3800
#define REG_TDBAH    0x3804
#define REG_TDLEN    0x3808
#define REG_TDH      0x3810
#define REG_TDT      0x3818
#define REG_MTA      0x5200
#define REG_RAL      0x5400
#define REG_RAH      0x5404

#define NUM_RX_DESC 32
#define NUM_TX_DESC 16

struct e1000_rx_desc {
    volatile uint64_t buffer_addr;
    volatile uint16_t length;
    volatile uint16_t checksum;
    volatile uint8_t  status;
    volatile uint8_t  errors;
    volatile uint16_t special;
} __attribute__((packed));

struct e1000_tx_desc {
    volatile uint64_t buffer_addr;
    volatile uint16_t length;
    volatile uint8_t  cso;
    volatile uint8_t  cmd;
    volatile uint8_t  status;
    volatile uint8_t  css;
    volatile uint16_t special;
} __attribute__((packed));

static uint32_t s_mmio_base = 0;
static uint16_t s_cur_rx = 0;
static uint16_t s_cur_tx = 0;

static struct e1000_rx_desc s_rx_desc[NUM_RX_DESC] __attribute__((aligned(16)));
static uint8_t s_rx_buf[NUM_RX_DESC][2048] __attribute__((aligned(16)));

static struct e1000_tx_desc s_tx_desc[NUM_TX_DESC] __attribute__((aligned(16)));
static uint8_t s_tx_buf[NUM_TX_DESC][2048] __attribute__((aligned(16)));

static net_driver_t s_e1000_driver;

static inline uint32_t e1000_read(uint32_t reg) {
    return *(volatile uint32_t *)(s_mmio_base + reg);
}

static inline void e1000_write(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(s_mmio_base + reg) = val;
}

static void e1000_read_mac(uint8_t *mac) {
    uint32_t ral = e1000_read(REG_RAL);
    uint32_t rah = e1000_read(REG_RAH);

    mac[0] = (uint8_t)(ral & 0xFF);
    mac[1] = (uint8_t)((ral >> 8) & 0xFF);
    mac[2] = (uint8_t)((ral >> 16) & 0xFF);
    mac[3] = (uint8_t)((ral >> 24) & 0xFF);
    mac[4] = (uint8_t)(rah & 0xFF);
    mac[5] = (uint8_t)((rah >> 8) & 0xFF);

    // Fallback if RAL is zero: read EEPROM via EERD
    if (mac[0] == 0 && mac[1] == 0 && mac[2] == 0 && mac[3] == 0) {
        for (int i = 0; i < 3; i++) {
            e1000_write(REG_EERD, (i << 8) | 1); // Start read
            for (int t = 0; t < 10000; t++) {
                uint32_t val = e1000_read(REG_EERD);
                if (val & (1 << 4)) { // Done bit
                    uint16_t word = (uint16_t)((val >> 16) & 0xFFFF);
                    mac[i * 2] = (uint8_t)(word & 0xFF);
                    mac[i * 2 + 1] = (uint8_t)((word >> 8) & 0xFF);
                    break;
                }
            }
        }
    }
}

int e1000_init(const pci_device_t *pci) {
    if (!pci || pci->bar_type[0] != 0 || pci->bar[0] == 0) return -1;

    s_mmio_base = pci->bar[0];
    pci_enable_bus_mastering(pci->bus, pci->slot, pci->func);

    // Read MAC address before reset
    e1000_read_mac(s_e1000_driver.mac);

    // Reset controller
    e1000_write(REG_CTRL, e1000_read(REG_CTRL) | (1 << 26)); // Device Reset
    for (volatile int i = 0; i < 50000; i++) io_wait();

    // Auto-Speed Detect Enable, Set Link Up, and Full Duplex
    uint32_t ctrl = e1000_read(REG_CTRL);
    ctrl &= ~(1 << 26); // Clear reset
    ctrl |= (1 << 0) | (1 << 5) | (1 << 6); // FD | ASDE | SLU
    e1000_write(REG_CTRL, ctrl);

    // If MAC was not read, try again
    if (s_e1000_driver.mac[0] == 0 && s_e1000_driver.mac[1] == 0 && s_e1000_driver.mac[2] == 0) {
        e1000_read_mac(s_e1000_driver.mac);
    }

    // Program Receive Address filter with Station MAC and set AV (bit 31)
    uint32_t ral = (uint32_t)s_e1000_driver.mac[0] |
                   ((uint32_t)s_e1000_driver.mac[1] << 8) |
                   ((uint32_t)s_e1000_driver.mac[2] << 16) |
                   ((uint32_t)s_e1000_driver.mac[3] << 24);
    uint32_t rah = (uint32_t)s_e1000_driver.mac[4] |
                   ((uint32_t)s_e1000_driver.mac[5] << 8) |
                   (1U << 31); // Address Valid (AV)
    e1000_write(REG_RAL, ral);
    e1000_write(REG_RAH, rah);

    // Clear Multicast Table Array
    for (int i = 0; i < 128; i++) {
        e1000_write(REG_MTA + (i * 4), 0);
    }

    // Enable receive interrupts in IMS (RXT0)
    e1000_write(REG_IMS, (1 << 7));
    e1000_read(REG_ICR); // Clear pending

    // Initialize RX ring
    memset(s_rx_desc, 0, sizeof(s_rx_desc));
    s_cur_rx = 0;
    for (int i = 0; i < NUM_RX_DESC; i++) {
        s_rx_desc[i].buffer_addr = (uint32_t)&s_rx_buf[i][0];
        s_rx_desc[i].status = 0;
    }
    e1000_write(REG_RDBAL, (uint32_t)s_rx_desc);
    e1000_write(REG_RDBAH, 0);
    e1000_write(REG_RDLEN, sizeof(s_rx_desc));
    e1000_write(REG_RDH, 0);
    e1000_write(REG_RDT, NUM_RX_DESC - 1);
    // RCTL: EN (1<<1) | UPE (1<<3) | MPE (1<<4) | BAM (1<<15) | SECRC (1<<26)
    e1000_write(REG_RCTL, (1 << 1) | (1 << 3) | (1 << 4) | (1 << 15) | (1 << 26));

    // Initialize TX ring
    memset(s_tx_desc, 0, sizeof(s_tx_desc));
    s_cur_tx = 0;
    for (int i = 0; i < NUM_TX_DESC; i++) {
        s_tx_desc[i].buffer_addr = (uint32_t)&s_tx_buf[i][0];
        s_tx_desc[i].status = 1; // DD = 1 (free)
        s_tx_desc[i].cmd = 0;
    }
    e1000_write(REG_TDBAL, (uint32_t)s_tx_desc);
    e1000_write(REG_TDBAH, 0);
    e1000_write(REG_TDLEN, sizeof(s_tx_desc));
    e1000_write(REG_TDH, 0);
    e1000_write(REG_TDT, 0);
    // TIPG: 10 + (10 << 10) + (10 << 20)
    e1000_write(REG_TIPG, 0x0060200A);
    // TCTL: EN (1<<1) | PSP (1<<3) | CT (15<<4) | COLD (64<<12)
    e1000_write(REG_TCTL, (1 << 1) | (1 << 3) | (0x0F << 4) | (0x40 << 12));

    s_e1000_driver.name = "Intel Gigabit Ethernet (e1000)";
    s_e1000_driver.is_wireless = 0;
    s_e1000_driver.init = e1000_init;
    s_e1000_driver.send_packet = e1000_send_packet;
    s_e1000_driver.poll_packet = e1000_poll_packet;
    s_e1000_driver.get_link_status = e1000_get_link_status;

    g_active_net_driver = &s_e1000_driver;
    return 0;
}

int e1000_get_link_status(void) {
    if (!s_mmio_base) return 0;
    uint32_t status = e1000_read(REG_STATUS);
    return (status & (1 << 1)) ? 1 : 0; // Bit 1 = Link Up
}

extern void auth_cprintln(const char *s);
extern int snprintf(char *buf, size_t size, const char *fmt, ...);

void e1000_debug_dump(void) {
    if (!s_mmio_base) return;
    char buf[160];
    uint32_t ctrl = e1000_read(REG_CTRL);
    uint32_t status = e1000_read(REG_STATUS);
    uint32_t rctl = e1000_read(REG_RCTL);
    uint32_t tctl = e1000_read(REG_TCTL);
    uint32_t rdh = e1000_read(REG_RDH);
    uint32_t rdt = e1000_read(REG_RDT);
    uint32_t tdh = e1000_read(REG_TDH);
    uint32_t tdt = e1000_read(REG_TDT);
    uint32_t icr = e1000_read(REG_ICR);
    snprintf(buf, sizeof(buf), "[e1000 dbg] CTRL=0x%08X STAT=0x%08X ICR=0x%08X RCTL=0x%08X TCTL=0x%08X RDH=%u RDT=%u TDH=%u TDT=%u RX0_st=0x%02X TX0_st=0x%02X",
             ctrl, status, icr, rctl, tctl, rdh, rdt, tdh, tdt, s_rx_desc[0].status, s_tx_desc[0].status);
    auth_cprintln(buf);
    snprintf(buf, sizeof(buf), "[e1000 buf] TX_buf=0x%08X RX_buf=0x%08X TX_desc=0x%08X RX_desc=0x%08X",
             (uint32_t)&s_tx_buf[0][0], (uint32_t)&s_rx_buf[0][0], (uint32_t)s_tx_desc, (uint32_t)s_rx_desc);
    auth_cprintln(buf);
}

int e1000_send_packet(const void *data, size_t len) {
    if (!s_mmio_base || !data || len == 0 || len > 2048) return -1;

    // Check if current descriptor is free (DD set)
    if (!(s_tx_desc[s_cur_tx].status & 1)) {
        // Wait briefly for previous packet
        for (volatile int t = 0; t < 10000; t++) {
            if (s_tx_desc[s_cur_tx].status & 1) break;
        }
    }

    memcpy((void *)(uint32_t)s_tx_desc[s_cur_tx].buffer_addr, data, len);
    s_tx_desc[s_cur_tx].length = (uint16_t)len;
    // CMD: EOP (1<<0) | RS (1<<3)
    s_tx_desc[s_cur_tx].cmd = (1 << 0) | (1 << 3);
    s_tx_desc[s_cur_tx].status = 0;

    uint16_t next_tx = (s_cur_tx + 1) % NUM_TX_DESC;
    s_cur_tx = next_tx;
    e1000_write(REG_TDT, s_cur_tx);

    return 0;
}

int e1000_poll_packet(void *buf, size_t max_len) {
    if (!s_mmio_base || !buf || max_len == 0) return 0;

    if (!(s_rx_desc[s_cur_rx].status & 1)) {
        return 0; // No packet ready
    }

    uint16_t len = s_rx_desc[s_cur_rx].length;
    if (len > max_len) len = (uint16_t)max_len;

    memcpy(buf, (const void *)(uint32_t)s_rx_desc[s_cur_rx].buffer_addr, len);

    // Reset status and hand back to NIC
    s_rx_desc[s_cur_rx].status = 0;
    uint16_t old_rx = s_cur_rx;
    s_cur_rx = (s_cur_rx + 1) % NUM_RX_DESC;
    e1000_write(REG_RDT, old_rx);

    return (int)len;
}
