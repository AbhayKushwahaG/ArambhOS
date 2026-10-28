#include "pci.h"

#define PCI_CONFIG_ADDRESS 0x0CF8
#define PCI_CONFIG_DATA    0x0CFC

uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((1U << 31) | ((uint32_t)bus << 16) |
                                  ((uint32_t)slot << 11) | ((uint32_t)func << 8) |
                                  (offset & 0xFC));
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t address = (uint32_t)((1U << 31) | ((uint32_t)bus << 16) |
                                  ((uint32_t)slot << 11) | ((uint32_t)func << 8) |
                                  (offset & 0xFC));
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, val);
}

uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t val32 = pci_read_config32(bus, slot, func, offset);
    return (uint16_t)((val32 >> ((offset & 2) * 8)) & 0xFFFF);
}

void pci_write_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val) {
    uint32_t val32 = pci_read_config32(bus, slot, func, offset);
    if (offset == 0x04) {
        val32 = (uint32_t)val; // Do not touch write-1-to-clear status bits 16..31
    } else if (offset & 2) {
        val32 = (val32 & 0x0000FFFF) | ((uint32_t)val << 16);
    } else {
        val32 = (val32 & 0xFFFF0000) | (uint32_t)val;
    }
    pci_write_config32(bus, slot, func, offset, val32);
}

uint8_t pci_read_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t val32 = pci_read_config32(bus, slot, func, offset);
    return (uint8_t)((val32 >> ((offset & 3) * 8)) & 0xFF);
}

void pci_enable_bus_mastering(uint8_t bus, uint8_t slot, uint8_t func) {
    uint16_t cmd = pci_read_config16(bus, slot, func, 0x04);
    cmd |= 0x0007; // I/O Space (1), Memory Space (2), Bus Master (4)
    pci_write_config16(bus, slot, func, 0x04, cmd);
}

static void pci_read_device_bars(pci_device_t *dev) {
    for (int i = 0; i < 6; i++) {
        uint32_t bar = pci_read_config32(dev->bus, dev->slot, dev->func, 0x10 + (i * 4));
        if (bar & 1) {
            dev->bar_type[i] = 1; // I/O
            dev->bar[i] = bar & ~0x3;
        } else {
            dev->bar_type[i] = 0; // MMIO
            dev->bar[i] = bar & ~0xF;
        }
    }
}

int pci_scan(pci_device_t *devices, int max_devices) {
    int count = 0;
    for (uint16_t bus = 0; bus < 8; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint16_t vendor = pci_read_config16((uint8_t)bus, slot, 0, 0x00);
            if (vendor == 0xFFFF || vendor == 0x0000) continue;

            uint8_t header_type = pci_read_config8((uint8_t)bus, slot, 0, 0x0E);
            uint8_t max_funcs = (header_type & 0x80) ? 8 : 1;

            for (uint8_t func = 0; func < max_funcs; func++) {
                uint16_t v = pci_read_config16((uint8_t)bus, slot, func, 0x00);
                if (v == 0xFFFF || v == 0x0000) continue;

                if (count < max_devices) {
                    pci_device_t *d = &devices[count];
                    d->bus = (uint8_t)bus;
                    d->slot = slot;
                    d->func = func;
                    d->vendor_id = v;
                    d->device_id = pci_read_config16((uint8_t)bus, slot, func, 0x02);
                    uint16_t class_rev = pci_read_config16((uint8_t)bus, slot, func, 0x0A);
                    d->class_code = (uint8_t)(class_rev >> 8);
                    d->subclass = (uint8_t)(class_rev & 0xFF);
                    d->prog_if = pci_read_config8((uint8_t)bus, slot, func, 0x09);
                    d->revision = pci_read_config8((uint8_t)bus, slot, func, 0x08);
                    d->irq = pci_read_config8((uint8_t)bus, slot, func, 0x3C);
                    pci_read_device_bars(d);
                    count++;
                }
            }
        }
    }
    return count;
}

int pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t *out_dev) {
    pci_device_t devs[32];
    int n = pci_scan(devs, 32);
    for (int i = 0; i < n; i++) {
        if (devs[i].vendor_id == vendor_id && devs[i].device_id == device_id) {
            if (out_dev) *out_dev = devs[i];
            return 1;
        }
    }
    return 0;
}

int pci_find_network_device(pci_device_t *out_dev) {
    pci_device_t devs[32];
    int n = pci_scan(devs, 32);

    // Priority 1: Intel e1000 / e1000e / 82579LM
    for (int i = 0; i < n; i++) {
        if (devs[i].vendor_id == 0x8086) {
            uint16_t id = devs[i].device_id;
            if (id == 0x100E || id == 0x100F || id == 0x10D3 ||
                id == 0x1502 || id == 0x1503 || id == 0x153A ||
                id == 0x153B || id == 0x155A) {
                if (out_dev) *out_dev = devs[i];
                return 1;
            }
        }
    }

    // Priority 2: Realtek RTL8139
    for (int i = 0; i < n; i++) {
        if (devs[i].vendor_id == 0x10EC && devs[i].device_id == 0x8139) {
            if (out_dev) *out_dev = devs[i];
            return 1;
        }
    }

    // Priority 3: Any Ethernet controller (Class 0x02, Subclass 0x00)
    for (int i = 0; i < n; i++) {
        if (devs[i].class_code == 0x02 && devs[i].subclass == 0x00) {
            if (out_dev) *out_dev = devs[i];
            return 1;
        }
    }

    return 0;
}

int pci_find_wifi_device(pci_device_t *out_dev) {
    pci_device_t devs[32];
    int n = pci_scan(devs, 32);

    for (int i = 0; i < n; i++) {
        // Class 0x02, Subclass 0x80 = Network Controller (Other / Wireless)
        if (devs[i].class_code == 0x02 && devs[i].subclass == 0x80) {
            if (out_dev) *out_dev = devs[i];
            return 1;
        }
        // Known Wi-Fi chipsets: Intel Centrino (8086:0082, 8086:0085, etc.), Broadcom (14E4:xxxx)
        if (devs[i].vendor_id == 0x8086 && (devs[i].device_id == 0x0082 || devs[i].device_id == 0x0085)) {
            if (out_dev) *out_dev = devs[i];
            return 1;
        }
        if (devs[i].vendor_id == 0x14E4 && (devs[i].device_id == 0x4727 || devs[i].device_id == 0x4359)) {
            if (out_dev) *out_dev = devs[i];
            return 1;
        }
    }

    return 0;
}

int pci_find_display_device(pci_device_t *out_dev) {
    pci_device_t devs[32];
    int n = pci_scan(devs, 32);

    for (int i = 0; i < n; i++) {
        // Class 0x03 = Display controller
        if (devs[i].class_code == 0x03) {
            if (out_dev) *out_dev = devs[i];
            return 1;
        }
    }

    return 0;
}
