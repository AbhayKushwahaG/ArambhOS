#include "net_driver.h"

net_driver_t *g_active_net_driver = NULL;

net_driver_t *net_driver_probe(void) {
    if (g_active_net_driver != NULL) {
        return g_active_net_driver;
    }

    pci_device_t dev;

    // Check Intel e1000 family
    if (pci_find_network_device(&dev)) {
        if (dev.vendor_id == 0x8086) {
            if (e1000_init(&dev) == 0) {
                return g_active_net_driver;
            }
        } else if (dev.vendor_id == 0x10EC) {
            if (rtl8139_init(&dev) == 0) {
                return g_active_net_driver;
            }
        }
    }

    return NULL;
}
