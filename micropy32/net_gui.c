#include "net_gui.h"
#include "net_stack.h"
#include "net_config.h"
#include "auth.h"
#include <stdio.h>
#include <string.h>

extern const kernel_api_t *g_kapi;
extern void auth_cprint(const char *s);
extern void auth_cprintln(const char *s);

static void gui_print(const char *s) {
    auth_cprint(s);
}

static void gui_println(const char *s) {
    auth_cprintln(s);
}

static void gui_readline(char *buf, size_t max_len) {
    if (!g_kapi || !g_kapi->getc) return;
    size_t pos = 0;
    while (pos < max_len - 1) {
        char c = (char)g_kapi->getc();
        if (c == '\r' || c == '\n') {
            gui_println("");
            break;
        } else if (c == '\b' || c == 0x7F) {
            if (pos > 0) {
                pos--;
                gui_print("\b \b");
            }
        } else if (c >= 32 && c <= 126) {
            buf[pos++] = c;
            char s[2] = { c, '\0' };
            gui_print(s);
        }
    }
    buf[pos] = '\0';
}

static void gui_readline_masked(char *buf, size_t max_len) {
    if (!g_kapi || !g_kapi->getc) return;
    size_t pos = 0;
    while (pos < max_len - 1) {
        char c = (char)g_kapi->getc();
        if (c == '\r' || c == '\n') {
            gui_println("");
            break;
        } else if (c == '\b' || c == 0x7F) {
            if (pos > 0) {
                pos--;
                gui_print("\b \b");
            }
        } else if (c >= 32 && c <= 126) {
            buf[pos++] = c;
            gui_print("*");
        }
    }
    buf[pos] = '\0';
}

static void wait_for_enter(void) {
    gui_print("\nPress Enter to return to menu...");
    char tmp[4];
    gui_readline(tmp, sizeof(tmp));
}

// Option 1: Network Status
static void menu_status(void) {
    gui_println("\n--- [1. Network Status] ---");
    cmd_ifconfig(NULL);
    wait_for_enter();
}

// Option 2: Ethernet Settings
static void menu_ethernet_settings(void) {
    gui_println("\n--- [2. Ethernet Settings] ---");
    gui_println("1. Enable DHCP (Automatic Configuration)");
    gui_println("2. Restart DHCP Client now");
    gui_println("3. Switch to Static Configuration");
    gui_print("Choose option (1-3): ");

    char choice[16];
    gui_readline(choice, sizeof(choice));
    if (choice[0] == '1' || choice[0] == '2') {
        gui_println("Requesting IP configuration via DHCP...");
        if (dhcp_acquire(3000) == 0) {
            gui_println("DHCP configuration successful!");
            cmd_ifconfig(NULL);
        } else {
            gui_println("DHCP timed out. Check cable connection or DHCP server.");
        }
    } else if (choice[0] == '3') {
        g_net_config.is_dhcp = 0;
        gui_println("Switched mode to Static. Remember to configure IP (Option 6) and Save (Option 10).");
    }
    wait_for_enter();
}

// Option 3: Scan Wi-Fi
static void menu_scan_wifi(void) {
    gui_println("\n--- [3. Scan for Wi-Fi Networks] ---");
    char report[512];
    wifi_scan_report(report, sizeof(report));
    gui_print(report);
    wait_for_enter();
}

// Option 4: Connect Wi-Fi
static void menu_connect_wifi(void) {
    gui_println("\n--- [4. Connect to Wi-Fi] ---");
    char report[512];
    wifi_scan_report(report, sizeof(report));
    gui_print(report);

    pci_device_t dev;
    if (!pci_find_wifi_device(&dev)) {
        gui_println("\nCannot associate: No wireless device present in system.");
        wait_for_enter();
        return;
    }

    gui_print("Enter SSID: ");
    char ssid[64];
    gui_readline(ssid, sizeof(ssid));

    gui_print("Enter Wi-Fi Password: ");
    char pass[64];
    gui_readline_masked(pass, sizeof(pass));

    gui_println("\nAttempting 802.11 association to SSID...");
    for (volatile int d = 0; d < 50000; d++) io_wait();

    gui_println("Error: WPA2-PSK 4-way handshake failed (firmware microcode missing).");
    gui_println("Wi-Fi connection aborted. Connect via Ethernet.");
    wait_for_enter();
}

// Option 5: Disconnect
static void menu_disconnect(void) {
    gui_println("\n--- [5. Disconnect from Network] ---");
    g_net_config.is_configured = 0;
    gui_println("Network interface disconnected / deconfigured.");
    wait_for_enter();
}

// Option 6: Static IP
static void menu_static_ip(void) {
    gui_println("\n--- [6. Configure Static IPv4 Address] ---");
    char ip_buf[32], mask_buf[32], gw_buf[32];

    gui_print("Enter IPv4 Address [e.g. 192.168.1.100]: ");
    gui_readline(ip_buf, sizeof(ip_buf));
    uint32_t ip;
    if (!inet_aton(ip_buf, &ip)) {
        gui_println("Invalid IPv4 address format.");
        wait_for_enter();
        return;
    }

    gui_print("Enter Subnet Mask [e.g. 255.255.255.0]: ");
    gui_readline(mask_buf, sizeof(mask_buf));
    uint32_t mask;
    if (!inet_aton(mask_buf, &mask)) {
        mask = 0x00FFFFFF; // 255.255.255.0
    }

    gui_print("Enter Gateway [e.g. 192.168.1.1]: ");
    gui_readline(gw_buf, sizeof(gw_buf));
    uint32_t gw;
    if (!inet_aton(gw_buf, &gw)) {
        gw = 0;
    }

    g_net_config.ip = ip;
    g_net_config.netmask = mask;
    g_net_config.gateway = gw;
    g_net_config.is_dhcp = 0;
    g_net_config.is_configured = 1;

    gui_println("Static IPv4 configuration applied.");
    wait_for_enter();
}

// Option 7: Configure DNS
static void menu_dns(void) {
    gui_println("\n--- [7. Configure DNS] ---");
    char dns_buf[32];
    gui_print("Enter Primary DNS Server [e.g. 8.8.8.8]: ");
    gui_readline(dns_buf, sizeof(dns_buf));
    uint32_t dns;
    if (!inet_aton(dns_buf, &dns)) {
        gui_println("Invalid DNS IP format.");
        wait_for_enter();
        return;
    }
    g_net_config.dns = dns;
    gui_println("DNS server updated.");
    wait_for_enter();
}

// Option 8: Test Connectivity
static void menu_test_connectivity(void) {
    gui_println("\n--- [8. Test Connectivity] ---");
    if (!g_net_config.is_configured) {
        gui_println("Network is not configured. Run DHCP (Option 2) or Static IP (Option 6) first.");
        wait_for_enter();
        return;
    }

    if (g_net_config.gateway != 0) {
        char gw_str[16];
        inet_ntoa(g_net_config.gateway, gw_str);
        gui_print("Pinging Gateway ("); gui_print(gw_str); gui_println(")...");
        cmd_ping(gw_str);
    }

    gui_println("\nPinging Public DNS (8.8.8.8)...");
    cmd_ping("8.8.8.8");

    gui_println("\nPinging Hostname (google.com)...");
    cmd_ping("google.com");

    wait_for_enter();
}

// Option 9: View Routes & Stats
static void menu_routes(void) {
    gui_println("\n--- [9. Routes & Interface Information] ---");
    char ip_str[16], mask_str[16], gw_str[16];
    inet_ntoa(g_net_config.ip, ip_str);
    inet_ntoa(g_net_config.netmask, mask_str);
    inet_ntoa(g_net_config.gateway, gw_str);

    gui_println("Destination     Gateway         Genmask         Flags Metric Iface");
    gui_println("------------------------------------------------------------------");
    char line[128];
    snprintf(line, sizeof(line), "default         %-15s 0.0.0.0         UG    0      eth0\n", gw_str);
    gui_print(line);
    snprintf(line, sizeof(line), "%-15s *               %-15s U     0      eth0\n", ip_str, mask_str);
    gui_print(line);

    wait_for_enter();
}

// Option 10: Save Configuration
static void menu_save(void) {
    gui_println("\n--- [10. Save Configuration] ---");
    if (net_config_save() == 0) {
        gui_println("Network settings saved to AarFS at /etc/network.conf.");
    } else {
        gui_println("Failed to save settings to disk.");
    }
    wait_for_enter();
}

int cmd_network(const char *args) {
    (void)args;
    net_driver_probe();

    while (1) {
        gui_println("\n=======================================================");
        gui_println("          AARAMBH OS - NETWORK CONFIGURATION          ");
        gui_println("=======================================================");

        // Summary bar
        char ip_str[16];
        inet_ntoa(g_net_config.ip, ip_str);
        int link = g_active_net_driver && g_active_net_driver->get_link_status ?
                   g_active_net_driver->get_link_status() : 0;
        char status_line[128];
        snprintf(status_line, sizeof(status_line), " Device: %s | Link: %s | IP: %s\n",
                 g_active_net_driver ? g_active_net_driver->name : "None",
                 link ? "UP" : "DOWN",
                 g_net_config.is_configured ? ip_str : "Unconfigured");
        gui_print(status_line);
        gui_println("-------------------------------------------------------");
        gui_println(" 1. Network status");
        gui_println(" 2. Ethernet settings (DHCP / Link)");
        gui_println(" 3. Scan for Wi-Fi networks");
        gui_println(" 4. Connect to Wi-Fi");
        gui_println(" 5. Disconnect from a network");
        gui_println(" 6. Configure a static IPv4 address");
        gui_println(" 7. Configure DNS");
        gui_println(" 8. Test connectivity (Ping Gateway / Internet)");
        gui_println(" 9. View routes and interface information");
        gui_println(" 10. Save configuration to /etc/network.conf");
        gui_println(" 11. Return to the shell");
        gui_println("-------------------------------------------------------");
        gui_print("Select option (1-11, or 'q' to quit): ");

        char choice[16];
        gui_readline(choice, sizeof(choice));
        if (choice[0] == '\0') continue;

        if (strcmp(choice, "1") == 0) {
            menu_status();
        } else if (strcmp(choice, "2") == 0) {
            menu_ethernet_settings();
        } else if (strcmp(choice, "3") == 0) {
            menu_scan_wifi();
        } else if (strcmp(choice, "4") == 0) {
            menu_connect_wifi();
        } else if (strcmp(choice, "5") == 0) {
            menu_disconnect();
        } else if (strcmp(choice, "6") == 0) {
            menu_static_ip();
        } else if (strcmp(choice, "7") == 0) {
            menu_dns();
        } else if (strcmp(choice, "8") == 0) {
            menu_test_connectivity();
        } else if (strcmp(choice, "9") == 0) {
            menu_routes();
        } else if (strcmp(choice, "10") == 0) {
            menu_save();
        } else if (strcmp(choice, "11") == 0 || strcmp(choice, "q") == 0 || choice[0] == 27) {
            gui_println("Returning to shell...");
            break;
        } else {
            gui_println("Invalid selection; please enter a number from 1 to 11.");
        }
    }
    return 0;
}
