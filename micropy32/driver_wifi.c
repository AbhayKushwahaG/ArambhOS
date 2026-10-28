#include "net_driver.h"
#include <stdio.h>
#include <string.h>

int wifi_detect(pci_device_t *out_pci, char *out_name, size_t max_len) {
    pci_device_t dev;
    if (pci_find_wifi_device(&dev)) {
        if (out_pci) *out_pci = dev;
        if (out_name && max_len > 0) {
            if (dev.vendor_id == 0x8086 && (dev.device_id == 0x0082 || dev.device_id == 0x0085)) {
                snprintf(out_name, max_len, "Intel Centrino Advanced-N 6205 (8086:%04X)", dev.device_id);
            } else if (dev.vendor_id == 0x8086) {
                snprintf(out_name, max_len, "Intel Wireless WiFi Controller (8086:%04X)", dev.device_id);
            } else if (dev.vendor_id == 0x14E4) {
                snprintf(out_name, max_len, "Broadcom 802.11 Wireless Adapter (14E4:%04X)", dev.device_id);
            } else {
                snprintf(out_name, max_len, "PCI Wireless Controller (%04X:%04X)", dev.vendor_id, dev.device_id);
            }
        }
        return 1;
    }
    return 0;
}

int wifi_scan_report(char *out_buf, size_t max_len) {
    pci_device_t dev;
    char name[64] = {0};

    if (wifi_detect(&dev, name, sizeof(name))) {
        snprintf(out_buf, max_len,
            "Wireless Adapter: %s\n"
            "PCI Location    : Bus %u, Device %u, Function %u (IRQ %u)\n"
            "Driver Status   : Firmware blob missing (requires vendor 802.11 microcode)\n"
            "Wi-Fi Scanning  : UNAVAILABLE on current architecture.\n"
            "Recommendation  : Use active Ethernet (e1000/rtl8139) for internet access.\n",
            name, dev.bus, dev.slot, dev.func, dev.irq);
        return 1;
    } else {
        snprintf(out_buf, max_len,
            "No wireless (802.11) network adapter detected on PCI bus.\n"
            "Hardware Status : Wireless device not present or not emulated in current VM.\n"
            "Recommendation  : Use Ethernet interface.\n");
        return 0;
    }
}
