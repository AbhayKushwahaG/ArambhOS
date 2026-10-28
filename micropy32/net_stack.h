#ifndef NET_STACK_H
#define NET_STACK_H

#include <stdint.h>
#include <stddef.h>
#include "net_driver.h"

// Byte order helpers
static inline uint16_t htons(uint16_t v) {
    return (uint16_t)((v << 8) | (v >> 8));
}
#define ntohs htons

static inline uint32_t htonl(uint32_t v) {
    return ((v & 0x000000FF) << 24) |
           ((v & 0x0000FF00) << 8)  |
           ((v & 0x00FF0000) >> 8)  |
           ((v & 0xFF000000) >> 24);
}
#define ntohl htonl

// IP string conversions
char *inet_ntoa(uint32_t ip, char *buf);
int inet_aton(const char *str, uint32_t *out_ip);

typedef struct {
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
    uint8_t  is_dhcp;
    uint8_t  is_configured;
} net_config_t;

extern net_config_t g_net_config;

// Socket definition
#define SOCK_TCP 1
#define SOCK_UDP 2

#define TCP_STATE_CLOSED      0
#define TCP_STATE_SYN_SENT    1
#define TCP_STATE_ESTABLISHED 2
#define TCP_STATE_FIN_WAIT    3
#define TCP_STATE_CLOSED_WAIT 4

#define TCP_RX_BUF_SIZE 16384

typedef struct {
    int in_use;
    int type;
    int state;
    uint32_t remote_ip;
    uint16_t remote_port;
    uint16_t local_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint32_t remote_seq;
    uint8_t rx_buf[TCP_RX_BUF_SIZE];
    uint16_t rx_head;
    uint16_t rx_tail;
} socket_t;

#define MAX_SOCKETS 8
extern socket_t g_sockets[MAX_SOCKETS];

// Stack API
void net_init(void);
void net_poll(void);
int net_boot_init(void);

int arp_resolve(uint32_t target_ip, uint8_t *out_mac, uint32_t timeout_ms);
int icmp_ping(uint32_t target_ip, uint32_t timeout_ms);
int dhcp_acquire(uint32_t timeout_ms);
int dns_resolve(const char *hostname, uint32_t *out_ip, uint32_t timeout_ms);

// Socket API
int net_socket_open(int type);
int net_connect(int sock, uint32_t ip, uint16_t port, uint32_t timeout_ms);
int net_send(int sock, const void *buf, size_t len);
int net_recv(int sock, void *buf, size_t max_len, uint32_t timeout_ms);
void net_close(int sock);

// Commands
int cmd_ifconfig(const char *args);
int cmd_ping(const char *args);

#endif // NET_STACK_H
