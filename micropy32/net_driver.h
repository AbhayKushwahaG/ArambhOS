#ifndef NET_DRIVER_H
#define NET_DRIVER_H

#include <stdint.h>
#include <stddef.h>
#include "pci.h"

typedef struct net_driver {
    const char *name;
    uint8_t mac[6];
    uint8_t is_wireless;
    int (*init)(const pci_device_t *pci);
    int (*send_packet)(const void *data, size_t len);
    int (*poll_packet)(void *buf, size_t max_len);
    int (*get_link_status)(void);
} net_driver_t;

extern net_driver_t *g_active_net_driver;

// Driver registries
int e1000_init(const pci_device_t *pci);
int e1000_send_packet(const void *data, size_t len);
int e1000_poll_packet(void *buf, size_t max_len);
int e1000_get_link_status(void);

int rtl8139_init(const pci_device_t *pci);
int rtl8139_send_packet(const void *data, size_t len);
int rtl8139_poll_packet(void *buf, size_t max_len);
int rtl8139_get_link_status(void);

// Wi-Fi diagnostics & status
int wifi_detect(pci_device_t *out_pci, char *out_name, size_t max_len);
int wifi_scan_report(char *out_buf, size_t max_len);

// Global driver probe
net_driver_t *net_driver_probe(void);

#endif // NET_DRIVER_H
