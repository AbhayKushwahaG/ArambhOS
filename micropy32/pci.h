#ifndef PCI_H
#define PCI_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint32_t bar[6];
    uint8_t bar_type[6]; // 0 = MMIO, 1 = IO
    uint8_t irq;
} pci_device_t;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val);
uint8_t pci_read_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

void pci_enable_bus_mastering(uint8_t bus, uint8_t slot, uint8_t func);
int pci_scan(pci_device_t *devices, int max_devices);
int pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t *out_dev);
int pci_find_network_device(pci_device_t *out_dev);
int pci_find_wifi_device(pci_device_t *out_dev);
int pci_find_display_device(pci_device_t *out_dev);

#endif // PCI_H
