#include "net_driver.h"
#include <string.h>

#define RTL_REG_IDR0    0x00
#define RTL_REG_TSD0    0x10
#define RTL_REG_TSAD0   0x20
#define RTL_REG_RBSTART 0x30
#define RTL_REG_CR      0x37
#define RTL_REG_CAPR    0x38
#define RTL_REG_CBRX    0x3A
#define RTL_REG_IMR     0x3C
#define RTL_REG_ISR     0x3E
#define RTL_REG_TCR     0x40
#define RTL_REG_RCR     0x44
#define RTL_REG_BMCR    0x62
#define RTL_REG_BMSR    0x64

#define RTL_RX_BUF_SIZE (8192 + 16 + 1536)

static uint16_t s_io_base = 0;
static uint8_t s_rx_buffer[RTL_RX_BUF_SIZE] __attribute__((aligned(16)));
static uint8_t s_tx_buffer[4][2048] __attribute__((aligned(16)));
static uint16_t s_rx_offset = 0;
static uint8_t s_cur_tx = 0;

static net_driver_t s_rtl8139_driver;

int rtl8139_init(const pci_device_t *pci) {
    if (!pci) return -1;

    // Use I/O port BAR (usually BAR0)
    s_io_base = (pci->bar_type[0] == 1) ? (uint16_t)pci->bar[0] : (uint16_t)pci->bar[1];
    if (!s_io_base) return -1;

    pci_enable_bus_mastering(pci->bus, pci->slot, pci->func);

    // Power on (CONFIG1: set to 0)
    outb(s_io_base + 0x52, 0x00);

    // Software reset (CR: bit 4)
    outb(s_io_base + RTL_REG_CR, 0x10);
    for (volatile int t = 0; t < 10000; t++) {
        if (!(inb(s_io_base + RTL_REG_CR) & 0x10)) break;
    }

    // Read MAC address from IDR0..IDR5
    for (int i = 0; i < 6; i++) {
        s_rtl8139_driver.mac[i] = inb(s_io_base + RTL_REG_IDR0 + i);
    }

    // Set receive buffer start address
    outl(s_io_base + RTL_REG_RBSTART, (uint32_t)s_rx_buffer);

    // Set CAPR and CBRX
    s_rx_offset = 0;
    outw(s_io_base + RTL_REG_CAPR, 0);

    // Disable interrupts (we poll)
    outw(s_io_base + RTL_REG_IMR, 0x0000);

    // Configure receive: AAP(1) | APM(2) | AM(4) | AB(8) | WRAP(128)
    outl(s_io_base + RTL_REG_RCR, 0x0000008F);

    // Configure transmit (default burst 2048)
    outl(s_io_base + RTL_REG_TCR, 0x03000000);

    // Enable RX and TX
    outb(s_io_base + RTL_REG_CR, 0x0C); // RE (8) | TE (4)

    s_cur_tx = 0;
    s_rtl8139_driver.name = "Realtek RTL8139 Fast Ethernet";
    s_rtl8139_driver.is_wireless = 0;
    s_rtl8139_driver.init = rtl8139_init;
    s_rtl8139_driver.send_packet = rtl8139_send_packet;
    s_rtl8139_driver.poll_packet = rtl8139_poll_packet;
    s_rtl8139_driver.get_link_status = rtl8139_get_link_status;

    g_active_net_driver = &s_rtl8139_driver;
    return 0;
}

int rtl8139_get_link_status(void) {
    if (!s_io_base) return 0;
    // MediaStatus register at offset 0x58: bit 2 (LinkB) is 0 when link is OK!
    uint8_t msr = inb(s_io_base + 0x58);
    return (msr & 0x04) ? 0 : 1;
}

int rtl8139_send_packet(const void *data, size_t len) {
    if (!s_io_base || !data || len == 0 || len > 1792) return -1;

    memcpy(s_tx_buffer[s_cur_tx], data, len);

    // Write buffer address to TSAD[s_cur_tx]
    outl(s_io_base + RTL_REG_TSAD0 + (s_cur_tx * 4), (uint32_t)s_tx_buffer[s_cur_tx]);

    // Write length to TSD[s_cur_tx] (clears bit 13, starts TX)
    outl(s_io_base + RTL_REG_TSD0 + (s_cur_tx * 4), (uint32_t)(len & 0x1FFF));

    // Wait briefly for completion (bit 15 = TOK)
    for (volatile int t = 0; t < 10000; t++) {
        uint32_t status = inl(s_io_base + RTL_REG_TSD0 + (s_cur_tx * 4));
        if (status & (1 << 15)) break; // TOK
    }

    s_cur_tx = (s_cur_tx + 1) % 4;
    return 0;
}

int rtl8139_poll_packet(void *buf, size_t max_len) {
    if (!s_io_base || !buf || max_len == 0) return 0;

    // Check CR buffer empty bit (bit 0)
    if (inb(s_io_base + RTL_REG_CR) & 0x01) {
        return 0;
    }

    uint16_t cbrx = inw(s_io_base + RTL_REG_CBRX);
    if (s_rx_offset == cbrx) {
        return 0;
    }

    uint32_t packet_header = *(uint32_t *)(s_rx_buffer + s_rx_offset);
    uint16_t status = (uint16_t)(packet_header & 0xFFFF);
    uint16_t length = (uint16_t)(packet_header >> 16);

    // Check packet OK (bit 0 of status)
    if (!(status & 1) || length < 4 || length > 2000) {
        // Reset offset on packet corruption to current hardware write pointer
        s_rx_offset = cbrx;
        uint16_t capr = (s_rx_offset >= 16) ? (s_rx_offset - 16) : (8192 + s_rx_offset - 16);
        outw(s_io_base + RTL_REG_CAPR, capr);
        return 0;
    }

    uint16_t payload_len = length - 4; // Exclude CRC
    if (payload_len > max_len) payload_len = (uint16_t)max_len;

    memcpy(buf, s_rx_buffer + s_rx_offset + 4, payload_len);

    s_rx_offset = (s_rx_offset + length + 4 + 3) & ~3;
    if (s_rx_offset >= 8192) s_rx_offset -= 8192;

    uint16_t capr = (s_rx_offset >= 16) ? (s_rx_offset - 16) : (8192 + s_rx_offset - 16);
    outw(s_io_base + RTL_REG_CAPR, capr);
    return (int)payload_len;
}
