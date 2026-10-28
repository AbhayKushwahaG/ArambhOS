#include "net_config.h"
#include "auth.h"
#include <string.h>
#include <stdio.h>

extern const kernel_api_t *g_kapi;

int net_config_load(void) {
    if (!g_kapi || !g_kapi->fs_find_child || !g_kapi->fs_read_file) return -1;

    uint32_t etc_id = g_kapi->fs_find_child(0, "etc");
    if (!etc_id) return -1;

    uint32_t file_id = g_kapi->fs_find_child(etc_id, "network.conf");
    if (!file_id) return -1;

    char buf[512] = {0};
    uint32_t bytes_read = 0;
    if (g_kapi->fs_read_file(file_id, buf, &bytes_read) != 0 || bytes_read == 0) return -1;
    buf[bytes_read < 511 ? bytes_read : 511] = '\0';

    char *line = buf;
    while (*line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        char *eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            char *key = line;
            char *val = eq + 1;
            while (*val == ' ') val++;

            if (strcmp(key, "mode") == 0) {
                if (strncmp(val, "static", 6) == 0) g_net_config.is_dhcp = 0;
                else g_net_config.is_dhcp = 1;
            } else if (strcmp(key, "ip") == 0) {
                inet_aton(val, &g_net_config.ip);
                g_net_config.is_configured = 1;
            } else if (strcmp(key, "netmask") == 0) {
                inet_aton(val, &g_net_config.netmask);
            } else if (strcmp(key, "gateway") == 0) {
                inet_aton(val, &g_net_config.gateway);
            } else if (strcmp(key, "dns") == 0) {
                inet_aton(val, &g_net_config.dns);
            }
        }
        if (!nl) break;
        line = nl + 1;
    }

    return 0;
}

int net_config_save(void) {
    if (!g_kapi || !g_kapi->fs_find_child || !g_kapi->fs_create_entry || !g_kapi->fs_write_file) return -1;

    uint32_t etc_id = g_kapi->fs_find_child(0, "etc");
    if (!etc_id) {
        etc_id = g_kapi->fs_create_entry("etc", 2, 0);
        if (!etc_id) return -1;
    }

    uint32_t file_id = g_kapi->fs_find_child(etc_id, "network.conf");
    if (!file_id) {
        file_id = g_kapi->fs_create_entry("network.conf", 1, etc_id);
        if (!file_id) return -1;
    }

    char ip_str[16], mask_str[16], gw_str[16], dns_str[16];
    inet_ntoa(g_net_config.ip, ip_str);
    inet_ntoa(g_net_config.netmask, mask_str);
    inet_ntoa(g_net_config.gateway, gw_str);
    inet_ntoa(g_net_config.dns, dns_str);

    char buf[512];
    int len = snprintf(buf, sizeof(buf),
        "mode=%s\n"
        "ip=%s\n"
        "netmask=%s\n"
        "gateway=%s\n"
        "dns=%s\n",
        g_net_config.is_dhcp ? "dhcp" : "static",
        ip_str, mask_str, gw_str, dns_str);

    if (g_kapi->fs_write_file(file_id, buf, len) != 0) return -1;
    if (g_kapi->fs_commit) g_kapi->fs_commit();

    return 0;
}
