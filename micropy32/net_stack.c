#include "net_stack.h"
#include "auth.h"
#include <string.h>
#include <stdio.h>

extern const kernel_api_t *g_kapi;
extern void auth_cprint(const char *s);
extern void auth_cprintln(const char *s);

static inline uint32_t get_ticks(void) {
    if (g_kapi && g_kapi->timer_ticks) {
        return *g_kapi->timer_ticks;
    }
    return 0;
}

net_config_t g_net_config = {0};
socket_t g_sockets[MAX_SOCKETS] = {0};

// Static ARP cache
#define ARP_CACHE_SIZE 16
typedef struct {
    uint32_t ip;
    uint8_t mac[6];
    uint32_t timestamp;
} arp_entry_t;
static arp_entry_t s_arp_cache[ARP_CACHE_SIZE] = {0};

static uint16_t s_next_local_port = 49152;
static uint32_t s_dhcp_xid = 0x39141026;
static int s_ping_received = 0;
static uint16_t s_ping_seq = 0;
static uint32_t s_dns_resolved_ip = 0;
static uint16_t s_dns_txid = 0;

// Header structures
struct eth_header {
    uint8_t dst_mac[6];
    uint8_t src_mac[6];
    uint16_t ethertype;
} __attribute__((packed));

struct arp_packet {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t  hw_size;
    uint8_t  proto_size;
    uint16_t opcode;
    uint8_t  sender_mac[6];
    uint32_t sender_ip;
    uint8_t  target_mac[6];
    uint32_t target_ip;
} __attribute__((packed));

struct ip_header {
    uint8_t  ihl_version;
    uint8_t  tos;
    uint16_t total_length;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} __attribute__((packed));

struct icmp_header {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed));

struct udp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed));

struct tcp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_offset; // (offset << 4)
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed));

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

static uint16_t net_checksum(const void *data, size_t len) {
    const uint16_t *p = (const uint16_t *)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += *p++;
        len -= 2;
    }
    if (len == 1) sum += *(const uint8_t *)p;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip, const void *tcp_seg, size_t tcp_len) {
    uint32_t sum = 0;

    // Pseudo-header words (summed in little-endian matching network byte order)
    sum += (src_ip & 0xFFFF) + (src_ip >> 16);
    sum += (dst_ip & 0xFFFF) + (dst_ip >> 16);
    sum += htons(6); // Protocol TCP
    sum += htons((uint16_t)tcp_len);

    // TCP segment words
    const uint16_t *p = (const uint16_t *)tcp_seg;
    size_t len = tcp_len;
    while (len > 1) {
        sum += *p++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(const uint8_t *)p;
    }

    // Fold 32-bit sum to 16 bits
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}

char *inet_ntoa(uint32_t ip, char *buf) {
    uint8_t *b = (uint8_t *)&ip;
    snprintf(buf, 16, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return buf;
}

int inet_aton(const char *str, uint32_t *out_ip) {
    if (!str || !out_ip) return 0;
    unsigned int a, b, c, d;
    if (sscanf(str, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return 0;
    if (a > 255 || b > 255 || c > 255 || d > 255) return 0;
    uint8_t bytes[4] = { (uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d };
    memcpy(out_ip, bytes, 4);
    return 1;
}

static int send_ethernet_raw(const uint8_t *dst_mac, uint16_t ethertype, const void *payload, size_t len) {
    if (!g_active_net_driver || !g_active_net_driver->send_packet) return -1;

    uint8_t frame[1536];
    struct eth_header *eth = (struct eth_header *)frame;
    memcpy(eth->dst_mac, dst_mac, 6);
    memcpy(eth->src_mac, g_active_net_driver->mac, 6);
    eth->ethertype = htons(ethertype);

    if (len > sizeof(frame) - sizeof(struct eth_header)) return -1;
    memcpy(frame + sizeof(struct eth_header), payload, len);

    size_t total = sizeof(struct eth_header) + len;
    if (total < 60) {
        memset(frame + total, 0, 60 - total);
        total = 60;
    }

    return g_active_net_driver->send_packet(frame, total);
}

// ---------------- ARP PROTOCOL ----------------

static void arp_cache_update(uint32_t ip, const uint8_t *mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (s_arp_cache[i].ip == ip) {
            memcpy(s_arp_cache[i].mac, mac, 6);
            return;
        }
    }
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (s_arp_cache[i].ip == 0) {
            s_arp_cache[i].ip = ip;
            memcpy(s_arp_cache[i].mac, mac, 6);
            return;
        }
    }
    // Replace first entry if full
    s_arp_cache[0].ip = ip;
    memcpy(s_arp_cache[0].mac, mac, 6);
}

int arp_resolve(uint32_t target_ip, uint8_t *out_mac, uint32_t timeout_ms) {
    // Check broadcast
    if (target_ip == 0xFFFFFFFF) {
        memset(out_mac, 0xFF, 6);
        return 0;
    }

    // Check subnet routing: if target outside subnet, route via gateway
    if (g_net_config.is_configured && g_net_config.gateway != 0) {
        if ((target_ip & g_net_config.netmask) != (g_net_config.ip & g_net_config.netmask)) {
            target_ip = g_net_config.gateway;
        }
    }

    // Check ARP cache
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (s_arp_cache[i].ip == target_ip) {
            memcpy(out_mac, s_arp_cache[i].mac, 6);
            return 0;
        }
    }

    // Send ARP request
    struct arp_packet arp;
    arp.hw_type = htons(1);
    arp.proto_type = htons(0x0800);
    arp.hw_size = 6;
    arp.proto_size = 4;
    arp.opcode = htons(1); // Request
    static const uint8_t s_zero_mac[6] = {0};
    memcpy(arp.sender_mac, g_active_net_driver ? g_active_net_driver->mac : s_zero_mac, 6);
    arp.sender_ip = g_net_config.ip;
    memset(arp.target_mac, 0, 6);
    arp.target_ip = target_ip;

    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    send_ethernet_raw(bcast, 0x0806, &arp, sizeof(arp));

    // Poll for reply
    uint32_t start_ticks = get_ticks();
    uint32_t loop_count = 0;
    uint32_t max_loops = timeout_ms * 1000;

    while (1) {
        net_poll();
        for (int i = 0; i < ARP_CACHE_SIZE; i++) {
            if (s_arp_cache[i].ip == target_ip) {
                memcpy(out_mac, s_arp_cache[i].mac, 6);
                return 0;
            }
        }
        if (g_kapi && g_kapi->timer_ticks) {
            uint32_t now = *g_kapi->timer_ticks;
            if (now >= start_ticks && (now - start_ticks) >= timeout_ms) return -1;
        }
        if (++loop_count >= max_loops) return -1;
        for (volatile int d = 0; d < 2000; d++) io_wait();
    }

    return -1; // Timed out
}

static void handle_arp(const uint8_t *payload, size_t len) {
    if (len < sizeof(struct arp_packet)) return;
    const struct arp_packet *arp = (const struct arp_packet *)payload;

    if (ntohs(arp->hw_type) != 1 || ntohs(arp->proto_type) != 0x0800) return;

    arp_cache_update(arp->sender_ip, arp->sender_mac);

    // If request for our IP, reply
    if (ntohs(arp->opcode) == 1 && g_net_config.is_configured && arp->target_ip == g_net_config.ip) {
        struct arp_packet reply;
        reply.hw_type = htons(1);
        reply.proto_type = htons(0x0800);
        reply.hw_size = 6;
        reply.proto_size = 4;
        reply.opcode = htons(2); // Reply
        memcpy(reply.sender_mac, g_active_net_driver->mac, 6);
        reply.sender_ip = g_net_config.ip;
        memcpy(reply.target_mac, arp->sender_mac, 6);
        reply.target_ip = arp->sender_ip;

        send_ethernet_raw(arp->sender_mac, 0x0806, &reply, sizeof(reply));
    }
}

// ---------------- IP & ICMP ----------------

static int send_ipv4_packet(uint32_t dst_ip, uint8_t proto, const void *payload, size_t len) {
    uint8_t dst_mac[6];
    if (arp_resolve(dst_ip, dst_mac, 1000) != 0) {
        return -1;
    }

    uint8_t packet[1536];
    struct ip_header *ip = (struct ip_header *)packet;
    ip->ihl_version = (4 << 4) | 5;
    ip->tos = 0;
    ip->total_length = htons((uint16_t)(sizeof(struct ip_header) + len));
    ip->id = htons(0x4200);
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->protocol = proto;
    ip->checksum = 0;
    ip->src_ip = g_net_config.ip;
    ip->dst_ip = dst_ip;
    ip->checksum = net_checksum(ip, sizeof(struct ip_header));

    if (len > sizeof(packet) - sizeof(struct ip_header)) return -1;
    memcpy(packet + sizeof(struct ip_header), payload, len);

    return send_ethernet_raw(dst_mac, 0x0800, packet, sizeof(struct ip_header) + len);
}

static void handle_icmp(uint32_t src_ip, const uint8_t *payload, size_t len) {
    if (len < sizeof(struct icmp_header)) return;
    const struct icmp_header *icmp = (const struct icmp_header *)payload;

    if (icmp->type == 8) { // Echo Request
        uint8_t reply_buf[512];
        if (len > sizeof(reply_buf)) len = sizeof(reply_buf);
        memcpy(reply_buf, payload, len);

        struct icmp_header *reply = (struct icmp_header *)reply_buf;
        reply->type = 0; // Echo Reply
        reply->code = 0;
        reply->checksum = 0;
        reply->checksum = net_checksum(reply_buf, len);

        send_ipv4_packet(src_ip, 1, reply_buf, len);
    } else if (icmp->type == 0) { // Echo Reply
        if (ntohs(icmp->seq) == s_ping_seq) {
            s_ping_received = 1;
        }
    }
}

int icmp_ping(uint32_t target_ip, uint32_t timeout_ms) {
    if (!g_net_config.is_configured) return -1;

    uint8_t ping_buf[64];
    memset(ping_buf, 0xAA, sizeof(ping_buf));
    struct icmp_header *icmp = (struct icmp_header *)ping_buf;
    icmp->type = 8;
    icmp->code = 0;
    icmp->id = htons(0x1337);
    s_ping_seq++;
    icmp->seq = htons(s_ping_seq);
    icmp->checksum = 0;
    icmp->checksum = net_checksum(ping_buf, sizeof(ping_buf));

    s_ping_received = 0;
    if (send_ipv4_packet(target_ip, 1, ping_buf, sizeof(ping_buf)) != 0) {
        return -1;
    }

    for (uint32_t t = 0; t < timeout_ms; t += 10) {
        net_poll();
        if (s_ping_received) {
            return (int)t; // RTT ms
        }
        for (volatile int d = 0; d < 5000; d++) io_wait();
    }

    return -1;
}

// ---------------- UDP & DHCP & DNS ----------------

static int send_udp_packet(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port, const void *payload, size_t len) {
    uint8_t packet[1536];
    struct udp_header *udp = (struct udp_header *)packet;
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length = htons((uint16_t)(sizeof(struct udp_header) + len));
    udp->checksum = 0; // Optional in IPv4

    if (len > sizeof(packet) - sizeof(struct udp_header)) return -1;
    memcpy(packet + sizeof(struct udp_header), payload, len);

    // If destination is broadcast 255.255.255.255 (e.g. DHCP)
    if (dst_ip == 0xFFFFFFFF) {
        uint8_t bcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        uint8_t ip_pkt[1536];
        struct ip_header *ip = (struct ip_header *)ip_pkt;
        ip->ihl_version = (4 << 4) | 5;
        ip->tos = 0;
        ip->total_length = htons((uint16_t)(sizeof(struct ip_header) + sizeof(struct udp_header) + len));
        ip->id = htons(0x1111);
        ip->flags_frag = 0;
        ip->ttl = 64;
        ip->protocol = 17; // UDP
        ip->checksum = 0;
        ip->src_ip = 0; // 0.0.0.0
        ip->dst_ip = 0xFFFFFFFF;
        ip->checksum = net_checksum(ip, sizeof(struct ip_header));
        memcpy(ip_pkt + sizeof(struct ip_header), packet, sizeof(struct udp_header) + len);

        return send_ethernet_raw(bcast_mac, 0x0800, ip_pkt, sizeof(struct ip_header) + sizeof(struct udp_header) + len);
    }

    return send_ipv4_packet(dst_ip, 17, packet, sizeof(struct udp_header) + len);
}

// DHCP Packet layout
struct dhcp_packet {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic_cookie; // 0x63825363
    uint8_t  options[312];
} __attribute__((packed));

static uint32_t s_dhcp_offered_ip = 0;
static uint32_t s_dhcp_server_ip = 0;
static int s_dhcp_state = 0; // 0=none, 1=offer received, 2=ack received

static void handle_dhcp(const uint8_t *payload, size_t len) {
    if (len < sizeof(struct dhcp_packet) - 312) return;
    const struct dhcp_packet *dhcp = (const struct dhcp_packet *)payload;

    if (dhcp->op != 2 || dhcp->xid != s_dhcp_xid) return; // Must be reply to our XID

    uint8_t msg_type = 0;
    const uint8_t *opt = dhcp->options;
    const uint8_t *end = payload + len;

    while (opt < end && *opt != 0xFF) {
        uint8_t code = *opt++;
        if (code == 0) continue; // Pad
        if (opt >= end) break;
        uint8_t opt_len = *opt++;
        if (opt + opt_len > end) break;

        if (code == 53 && opt_len >= 1) { // DHCP Message Type
            msg_type = *opt;
        } else if (code == 1 && opt_len == 4) { // Subnet Mask
            memcpy(&g_net_config.netmask, opt, 4);
        } else if (code == 3 && opt_len >= 4) { // Router (Gateway)
            memcpy(&g_net_config.gateway, opt, 4);
        } else if (code == 6 && opt_len >= 4) { // DNS Server
            memcpy(&g_net_config.dns, opt, 4);
        } else if (code == 54 && opt_len == 4) { // Server Identifier
            memcpy(&s_dhcp_server_ip, opt, 4);
        }
        opt += opt_len;
    }

    if (msg_type == 2) { // DHCPOFFER
        s_dhcp_offered_ip = dhcp->yiaddr;
        s_dhcp_state = 1;
    } else if (msg_type == 5) { // DHCPACK
        g_net_config.ip = dhcp->yiaddr;
        g_net_config.is_dhcp = 1;
        g_net_config.is_configured = 1;
        s_dhcp_state = 2;
    }
}

int dhcp_acquire(uint32_t timeout_ms) {
    if (!g_active_net_driver) return -1;

    s_dhcp_xid++;
    s_dhcp_state = 0;
    s_dhcp_offered_ip = 0;
    s_dhcp_server_ip = 0;

    // Build DHCPDISCOVER
    struct dhcp_packet pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.op = 1; // BootRequest
    pkt.htype = 1; // Ethernet
    pkt.hlen = 6;
    pkt.xid = s_dhcp_xid;
    pkt.flags = htons(0x8000); // Broadcast flag
    memcpy(pkt.chaddr, g_active_net_driver->mac, 6);
    pkt.magic_cookie = htonl(0x63825363);

    uint8_t *opt = pkt.options;
    // Option 53: Discover
    *opt++ = 53; *opt++ = 1; *opt++ = 1;
    // Option 55: Parameter Request List (1=mask, 3=router, 6=dns)
    *opt++ = 55; *opt++ = 3; *opt++ = 1; *opt++ = 3; *opt++ = 6;
    // Option 255: End
    *opt++ = 0xFF;

    size_t dhcp_len = (size_t)(opt - (uint8_t *)&pkt);
    if (dhcp_len < 300) dhcp_len = 300;
    send_udp_packet(0xFFFFFFFF, 68, 67, &pkt, dhcp_len);

    // Wait for DHCPOFFER
    for (uint32_t t = 0; t < timeout_ms / 2; t += 10) {
        net_poll();
        if (s_dhcp_state == 1) break;
        for (volatile int d = 0; d < 5000; d++) io_wait();
    }
    if (s_dhcp_state != 1) return -1; // No offer

    // Build DHCPREQUEST
    memset(&pkt, 0, sizeof(pkt));
    pkt.op = 1;
    pkt.htype = 1;
    pkt.hlen = 6;
    pkt.xid = s_dhcp_xid;
    pkt.flags = htons(0x8000);
    memcpy(pkt.chaddr, g_active_net_driver->mac, 6);
    pkt.magic_cookie = htonl(0x63825363);

    opt = pkt.options;
    // Option 53: Request
    *opt++ = 53; *opt++ = 1; *opt++ = 3;
    // Option 50: Requested IP
    *opt++ = 50; *opt++ = 4; memcpy(opt, &s_dhcp_offered_ip, 4); opt += 4;
    // Option 54: Server ID
    *opt++ = 54; *opt++ = 4; memcpy(opt, &s_dhcp_server_ip, 4); opt += 4;
    // Option 55: Parameter Request List
    *opt++ = 55; *opt++ = 3; *opt++ = 1; *opt++ = 3; *opt++ = 6;
    // Option 255: End
    *opt++ = 0xFF;

    dhcp_len = (size_t)(opt - (uint8_t *)&pkt);
    if (dhcp_len < 300) dhcp_len = 300;
    send_udp_packet(0xFFFFFFFF, 68, 67, &pkt, dhcp_len);

    // Wait for DHCPACK
    for (uint32_t t = 0; t < timeout_ms / 2; t += 10) {
        net_poll();
        if (s_dhcp_state == 2) {
            return 0; // Configured!
        }
        for (volatile int d = 0; d < 5000; d++) io_wait();
    }

    return -1;
}

// ---------------- DNS CLIENT ----------------

struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed));

static void handle_dns(const uint8_t *payload, size_t len) {
    if (len < sizeof(struct dns_header)) return;
    const struct dns_header *dns = (const struct dns_header *)payload;

    if (ntohs(dns->id) != s_dns_txid) return;
    if (ntohs(dns->ancount) == 0) return;

    // Skip question section
    const uint8_t *p = payload + sizeof(struct dns_header);
    const uint8_t *end = payload + len;

    while (p < end && *p != 0) p += (*p + 1);
    p += 5; // Skip zero byte + QTYPE(2) + QCLASS(2)

    // Parse answers
    while (p < end) {
        if ((*p & 0xC0) == 0xC0) p += 2; // Pointer
        else {
            while (p < end && *p != 0) p += (*p + 1);
            p++;
        }
        if (p + 10 > end) break;
        uint16_t type = ntohs(*(uint16_t *)p); p += 2;
        /* uint16_t cls = ntohs(*(uint16_t *)p); */ p += 2;
        p += 4; // TTL
        uint16_t rdlen = ntohs(*(uint16_t *)p); p += 2;

        if (type == 1 && rdlen == 4 && p + 4 <= end) { // Type A (IPv4)
            memcpy(&s_dns_resolved_ip, p, 4);
            return;
        }
        p += rdlen;
    }
}

int dns_resolve(const char *hostname, uint32_t *out_ip, uint32_t timeout_ms) {
    if (!hostname || !out_ip) return -1;

    // Direct dotted decimal?
    if (inet_aton(hostname, out_ip)) {
        return 0;
    }

    if (!g_net_config.is_configured || g_net_config.dns == 0) {
        return -1;
    }

    s_dns_txid++;
    s_dns_resolved_ip = 0;

    uint8_t packet[512];
    struct dns_header *dns = (struct dns_header *)packet;
    dns->id = htons(s_dns_txid);
    dns->flags = htons(0x0100); // Standard query, recursion desired
    dns->qdcount = htons(1);
    dns->ancount = 0;
    dns->nscount = 0;
    dns->arcount = 0;

    uint8_t *q = packet + sizeof(struct dns_header);
    const char *src = hostname;
    while (*src) {
        const char *dot = strchr(src, '.');
        size_t label_len = dot ? (size_t)(dot - src) : strlen(src);
        if (label_len > 63) label_len = 63;
        *q++ = (uint8_t)label_len;
        memcpy(q, src, label_len);
        q += label_len;
        if (!dot) break;
        src = dot + 1;
    }
    *q++ = 0; // Terminating null label
    *(uint16_t *)q = htons(1); q += 2; // Type A
    *(uint16_t *)q = htons(1); q += 2; // Class IN

    send_udp_packet(g_net_config.dns, 53535, 53, packet, (size_t)(q - packet));

    uint32_t start_ticks = get_ticks();
    uint32_t loop_count = 0;
    uint32_t max_loops = timeout_ms * 1000;

    while (s_dns_resolved_ip == 0) {
        net_poll();
        if (s_dns_resolved_ip != 0) {
            *out_ip = s_dns_resolved_ip;
            return 0;
        }
        if (g_kapi && g_kapi->timer_ticks) {
            uint32_t now = *g_kapi->timer_ticks;
            if (now >= start_ticks && (now - start_ticks) >= timeout_ms) return -1;
        }
        if (++loop_count >= max_loops) return -1;
        for (volatile int d = 0; d < 2000; d++) io_wait();
    }

    return -1;
}

// ---------------- TCP PROTOCOL & SOCKETS ----------------

static void handle_tcp(uint32_t src_ip, const uint8_t *payload, size_t len) {
    if (len < sizeof(struct tcp_header)) return;
    const struct tcp_header *tcp = (const struct tcp_header *)payload;

    uint16_t src_port = ntohs(tcp->src_port);
    uint16_t dst_port = ntohs(tcp->dst_port);
    uint8_t offset = (tcp->data_offset >> 4) * 4;
    if (offset < sizeof(struct tcp_header) || offset > len) return;

    const uint8_t *data = payload + offset;
    size_t data_len = len - offset;

    // Find matching socket
    socket_t *s = NULL;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (g_sockets[i].in_use && g_sockets[i].type == SOCK_TCP &&
            g_sockets[i].local_port == dst_port && g_sockets[i].remote_port == src_port &&
            g_sockets[i].remote_ip == src_ip) {
            s = &g_sockets[i];
            break;
        }
    }
    if (!s) return;

    uint32_t incoming_seq = ntohl(tcp->seq);
    /* uint32_t incoming_ack = ntohl(tcp->ack); */

    if (s->state == TCP_STATE_SYN_SENT) {
        if ((tcp->flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
            s->remote_seq = incoming_seq + 1;
            s->ack_num = incoming_seq + 1;
            s->seq_num++;

            // Send ACK
            struct tcp_header ack_hdr;
            memset(&ack_hdr, 0, sizeof(ack_hdr));
            ack_hdr.src_port = htons(s->local_port);
            ack_hdr.dst_port = htons(s->remote_port);
            ack_hdr.seq = htonl(s->seq_num);
            ack_hdr.ack = htonl(s->ack_num);
            ack_hdr.data_offset = (sizeof(struct tcp_header) / 4) << 4;
            ack_hdr.flags = TCP_FLAG_ACK;
            ack_hdr.window = htons(4096);
            ack_hdr.checksum = tcp_checksum(g_net_config.ip, s->remote_ip, &ack_hdr, sizeof(ack_hdr));

            send_ipv4_packet(s->remote_ip, 6, &ack_hdr, sizeof(ack_hdr));
            s->state = TCP_STATE_ESTABLISHED;
        }
        return;
    }

    if (s->state == TCP_STATE_ESTABLISHED || s->state == TCP_STATE_FIN_WAIT) {
        if (data_len > 0) {
            // Buffer data into socket rx_buf
            for (size_t i = 0; i < data_len; i++) {
                uint16_t next_head = (s->rx_head + 1) % TCP_RX_BUF_SIZE;
                if (next_head != s->rx_tail) {
                    s->rx_buf[s->rx_head] = data[i];
                    s->rx_head = next_head;
                }
            }
            s->ack_num = incoming_seq + data_len;

            // Send ACK
            struct tcp_header ack_hdr;
            memset(&ack_hdr, 0, sizeof(ack_hdr));
            ack_hdr.src_port = htons(s->local_port);
            ack_hdr.dst_port = htons(s->remote_port);
            ack_hdr.seq = htonl(s->seq_num);
            ack_hdr.ack = htonl(s->ack_num);
            ack_hdr.data_offset = (sizeof(struct tcp_header) / 4) << 4;
            ack_hdr.flags = TCP_FLAG_ACK;
            ack_hdr.window = htons(4096);
            ack_hdr.checksum = tcp_checksum(g_net_config.ip, s->remote_ip, &ack_hdr, sizeof(ack_hdr));

            send_ipv4_packet(s->remote_ip, 6, &ack_hdr, sizeof(ack_hdr));
        }

        if (tcp->flags & TCP_FLAG_FIN) {
            s->ack_num = incoming_seq + data_len + 1;
            // Send ACK for FIN
            struct tcp_header ack_hdr;
            memset(&ack_hdr, 0, sizeof(ack_hdr));
            ack_hdr.src_port = htons(s->local_port);
            ack_hdr.dst_port = htons(s->remote_port);
            ack_hdr.seq = htonl(s->seq_num);
            ack_hdr.ack = htonl(s->ack_num);
            ack_hdr.data_offset = (sizeof(struct tcp_header) / 4) << 4;
            ack_hdr.flags = TCP_FLAG_ACK;
            ack_hdr.window = htons(4096);
            ack_hdr.checksum = tcp_checksum(g_net_config.ip, s->remote_ip, &ack_hdr, sizeof(ack_hdr));

            send_ipv4_packet(s->remote_ip, 6, &ack_hdr, sizeof(ack_hdr));
            s->state = TCP_STATE_CLOSED_WAIT;
        }
    }
}

int net_socket_open(int type) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!g_sockets[i].in_use) {
            memset(&g_sockets[i], 0, sizeof(socket_t));
            g_sockets[i].in_use = 1;
            g_sockets[i].type = type;
            g_sockets[i].state = TCP_STATE_CLOSED;
            return i;
        }
    }
    return -1;
}

int net_connect(int sock, uint32_t ip, uint16_t port, uint32_t timeout_ms) {
    if (sock < 0 || sock >= MAX_SOCKETS || !g_sockets[sock].in_use) return -1;
    socket_t *s = &g_sockets[sock];

    s->remote_ip = ip;
    s->remote_port = port;
    s->local_port = s_next_local_port++;
    if (s_next_local_port > 65000) s_next_local_port = 49152;
    s->seq_num = 0x10000000 + (uint32_t)s->local_port;
    s->ack_num = 0;
    s->rx_head = 0;
    s->rx_tail = 0;
    s->state = TCP_STATE_SYN_SENT;

    // Send SYN
    struct tcp_header syn;
    memset(&syn, 0, sizeof(syn));
    syn.src_port = htons(s->local_port);
    syn.dst_port = htons(s->remote_port);
    syn.seq = htonl(s->seq_num);
    syn.ack = 0;
    syn.data_offset = (sizeof(struct tcp_header) / 4) << 4;
    syn.flags = TCP_FLAG_SYN;
    syn.window = htons(4096);
    syn.checksum = tcp_checksum(g_net_config.ip, s->remote_ip, &syn, sizeof(syn));

    if (send_ipv4_packet(s->remote_ip, 6, &syn, sizeof(syn)) != 0) {
        s->state = TCP_STATE_CLOSED;
        return -1;
    }

    // Wait for connection established
    uint32_t start_ticks = get_ticks();
    uint32_t loop_count = 0;
    uint32_t max_loops = timeout_ms * 1000;

    while (s->state != TCP_STATE_ESTABLISHED) {
        net_poll();
        if (s->state == TCP_STATE_ESTABLISHED) {
            return 0;
        }
        if (s->state == TCP_STATE_CLOSED) {
            return -1;
        }
        if (g_kapi && g_kapi->timer_ticks) {
            uint32_t now = *g_kapi->timer_ticks;
            if (now >= start_ticks && (now - start_ticks) >= timeout_ms) {
                s->state = TCP_STATE_CLOSED;
                return -1;
            }
        }
        if (++loop_count >= max_loops) {
            s->state = TCP_STATE_CLOSED;
            return -1;
        }
        for (volatile int d = 0; d < 2000; d++) io_wait();
    }

    s->state = TCP_STATE_CLOSED;
    return -1; // Timed out
}

int net_send(int sock, const void *buf, size_t len) {
    if (sock < 0 || sock >= MAX_SOCKETS || !g_sockets[sock].in_use) return -1;
    socket_t *s = &g_sockets[sock];
    if (s->state != TCP_STATE_ESTABLISHED) return -1;

    const uint8_t *p = (const uint8_t *)buf;
    size_t rem = len;

    while (rem > 0) {
        size_t chunk = rem > 1400 ? 1400 : rem;

        uint8_t pkt[1536];
        struct tcp_header *tcp = (struct tcp_header *)pkt;
        memset(tcp, 0, sizeof(struct tcp_header));
        tcp->src_port = htons(s->local_port);
        tcp->dst_port = htons(s->remote_port);
        tcp->seq = htonl(s->seq_num);
        tcp->ack = htonl(s->ack_num);
        tcp->data_offset = (sizeof(struct tcp_header) / 4) << 4;
        tcp->flags = TCP_FLAG_ACK | TCP_FLAG_PSH;
        tcp->window = htons(4096);

        memcpy(pkt + sizeof(struct tcp_header), p, chunk);
        tcp->checksum = tcp_checksum(g_net_config.ip, s->remote_ip, pkt, sizeof(struct tcp_header) + chunk);

        send_ipv4_packet(s->remote_ip, 6, pkt, sizeof(struct tcp_header) + chunk);
        s->seq_num += chunk;

        p += chunk;
        rem -= chunk;

        // Brief poll for ACKs
        for (int t = 0; t < 5; t++) {
            net_poll();
            for (volatile int d = 0; d < 1000; d++) io_wait();
        }
    }

    return (int)len;
}

int net_recv(int sock, void *buf, size_t max_len, uint32_t timeout_ms) {
    if (sock < 0 || sock >= MAX_SOCKETS || !g_sockets[sock].in_use) return -1;
    socket_t *s = &g_sockets[sock];

    uint32_t start_ticks = get_ticks();
    uint32_t loop_count = 0;
    uint32_t max_loops = timeout_ms * 1000;

    while (s->rx_head == s->rx_tail) {
        if (s->state == TCP_STATE_CLOSED_WAIT || s->state == TCP_STATE_CLOSED) {
            return 0; // EOF (Remote closed connection)
        }
        net_poll();

        if (g_kapi && g_kapi->timer_ticks) {
            uint32_t now = *g_kapi->timer_ticks;
            if (now >= start_ticks && (now - start_ticks) >= timeout_ms) {
                return -1; // Timed out
            }
        }
        if (++loop_count >= max_loops) {
            return -1; // Timed out
        }
        for (volatile int d = 0; d < 2000; d++) io_wait();
    }

    size_t count = 0;
    uint8_t *dst = (uint8_t *)buf;
    while (s->rx_head != s->rx_tail && count < max_len) {
        dst[count++] = s->rx_buf[s->rx_tail];
        s->rx_tail = (s->rx_tail + 1) % TCP_RX_BUF_SIZE;
    }

    return (int)count;
}

void net_close(int sock) {
    if (sock < 0 || sock >= MAX_SOCKETS || !g_sockets[sock].in_use) return;
    socket_t *s = &g_sockets[sock];

    if (s->state == TCP_STATE_ESTABLISHED) {
        struct tcp_header fin;
        memset(&fin, 0, sizeof(fin));
        fin.src_port = htons(s->local_port);
        fin.dst_port = htons(s->remote_port);
        fin.seq = htonl(s->seq_num);
        fin.ack = htonl(s->ack_num);
        fin.data_offset = (sizeof(struct tcp_header) / 4) << 4;
        fin.flags = TCP_FLAG_FIN | TCP_FLAG_ACK;
        fin.window = htons(4096);
        fin.checksum = tcp_checksum(g_net_config.ip, s->remote_ip, &fin, sizeof(fin));

        send_ipv4_packet(s->remote_ip, 6, &fin, sizeof(fin));
        s->state = TCP_STATE_CLOSED;
    }

    s->in_use = 0;
}

// ---------------- DISPATCHER & PACKET POLL ----------------

void net_poll(void) {
    if (!g_active_net_driver || !g_active_net_driver->poll_packet) return;

    uint8_t frame[1536];
    int len = g_active_net_driver->poll_packet(frame, sizeof(frame));
    if (len <= 0) return;

    if (len < (int)sizeof(struct eth_header)) return;
    const struct eth_header *eth = (const struct eth_header *)frame;
    uint16_t ethertype = ntohs(eth->ethertype);

    const uint8_t *payload = frame + sizeof(struct eth_header);
    size_t payload_len = (size_t)(len - sizeof(struct eth_header));

    if (ethertype == 0x0806) { // ARP
        handle_arp(payload, payload_len);
    } else if (ethertype == 0x0800) { // IPv4
        if (payload_len < sizeof(struct ip_header)) return;
        const struct ip_header *ip = (const struct ip_header *)payload;
        uint8_t ihl = (ip->ihl_version & 0x0F) * 4;
        if (ihl < sizeof(struct ip_header) || ihl > payload_len) return;

        uint16_t ip_total_len = ntohs(ip->total_length);
        if (ip_total_len < ihl || ip_total_len > payload_len) return;

        const uint8_t *ip_payload = payload + ihl;
        size_t ip_payload_len = (size_t)(ip_total_len - ihl);

        if (ip->protocol == 1) { // ICMP
            handle_icmp(ip->src_ip, ip_payload, ip_payload_len);
        } else if (ip->protocol == 17) { // UDP
            if (ip_payload_len < sizeof(struct udp_header)) return;
            const struct udp_header *udp = (const struct udp_header *)ip_payload;
            uint16_t dst_port = ntohs(udp->dst_port);
            const uint8_t *udp_data = ip_payload + sizeof(struct udp_header);
            size_t udp_data_len = ip_payload_len - sizeof(struct udp_header);

            if (dst_port == 68) { // DHCP
                handle_dhcp(udp_data, udp_data_len);
            } else {
                handle_dns(udp_data, udp_data_len);
            }
        } else if (ip->protocol == 6) { // TCP
            handle_tcp(ip->src_ip, ip_payload, ip_payload_len);
        }
    }
}

void net_init(void) {
    net_driver_probe();
}

// ---------------- BOOT & COMMANDS ----------------

extern void auth_cprint(const char *s);
extern void auth_cprintln(const char *s);

int net_boot_init(void) {
    net_driver_probe();
    if (!g_active_net_driver) {
        auth_cprintln("[net] No Ethernet network adapter detected on PCI bus.");
        return 0;
    }

    int link = g_active_net_driver->get_link_status ? g_active_net_driver->get_link_status() : 1;
    if (!link) {
        auth_cprint("[net] ");
        auth_cprint(g_active_net_driver->name);
        auth_cprintln(": Cable disconnected / Link down.");
        return 0;
    }

    auth_cprint("[net] ");
    auth_cprint(g_active_net_driver->name);
    auth_cprint(" (MAC ");
    char mac_str[24];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             g_active_net_driver->mac[0], g_active_net_driver->mac[1], g_active_net_driver->mac[2],
             g_active_net_driver->mac[3], g_active_net_driver->mac[4], g_active_net_driver->mac[5]);
    auth_cprint(mac_str);
    auth_cprintln("): Link up, requesting DHCP...");

    // Acquire DHCP with 2.5s bounded timeout
    if (dhcp_acquire(2500) == 0) {
        char ip_str[16], gw_str[16], dns_str[16];
        inet_ntoa(g_net_config.ip, ip_str);
        inet_ntoa(g_net_config.gateway, gw_str);
        inet_ntoa(g_net_config.dns, dns_str);

        auth_cprint("[net] Configured via DHCP: IP ");
        auth_cprint(ip_str);
        auth_cprint(", Gateway ");
        auth_cprint(gw_str);
        auth_cprint(", DNS ");
        auth_cprintln(dns_str);
        return 1;
    } else {
        auth_cprintln("[net] DHCP request timed out; use 'network' to configure static IP.");
        return 0;
    }
}

int cmd_ifconfig(const char *args) {
    (void)args;
    net_driver_probe();
    if (!g_active_net_driver) {
        auth_cprintln("ifconfig: No network device detected.");
        return 1;
    }

    char mac_str[24], ip_str[16], mask_str[16], gw_str[16], dns_str[16];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             g_active_net_driver->mac[0], g_active_net_driver->mac[1], g_active_net_driver->mac[2],
             g_active_net_driver->mac[3], g_active_net_driver->mac[4], g_active_net_driver->mac[5]);

    int link = g_active_net_driver->get_link_status ? g_active_net_driver->get_link_status() : 1;

    auth_cprint("eth0: flags=UP,");
    auth_cprint(link ? "RUNNING" : "NO-CARRIER");
    auth_cprint("  hw_addr "); auth_cprintln(mac_str);
    auth_cprint("      device: "); auth_cprintln(g_active_net_driver->name);

    if (g_net_config.is_configured) {
        inet_ntoa(g_net_config.ip, ip_str);
        inet_ntoa(g_net_config.netmask, mask_str);
        inet_ntoa(g_net_config.gateway, gw_str);
        inet_ntoa(g_net_config.dns, dns_str);

        auth_cprint("      inet "); auth_cprint(ip_str);
        auth_cprint("  netmask "); auth_cprintln(mask_str);
        auth_cprint("      gateway "); auth_cprint(gw_str);
        auth_cprint("  dns "); auth_cprintln(dns_str);
        auth_cprint("      config: "); auth_cprintln(g_net_config.is_dhcp ? "DHCP" : "static");
    } else {
        auth_cprintln("      inet: not configured (run 'network' or wait for DHCP)");
    }
    return 0;
}

int cmd_ping(const char *args) {
    if (!args || !*args) {
        auth_cprintln("Usage: ping <hostname_or_ip>");
        return 1;
    }

    net_driver_probe();
    if (!g_active_net_driver || !g_net_config.is_configured) {
        auth_cprintln("ping: network unconfigured; run 'network' first");
        return 1;
    }

    uint32_t target_ip = 0;
    if (dns_resolve(args, &target_ip, 2000) != 0) {
        auth_cprint("ping: cannot resolve host '"); auth_cprint(args); auth_cprintln("'");
        return 1;
    }

    char ip_str[16];
    inet_ntoa(target_ip, ip_str);
    auth_cprint("PING "); auth_cprint(args); auth_cprint(" ("); auth_cprint(ip_str); auth_cprintln("): 56 data bytes");

    int success = 0;
    for (int i = 1; i <= 3; i++) {
        int rtt = icmp_ping(target_ip, 1500);
        if (rtt >= 0) {
            char line[80];
            snprintf(line, sizeof(line), "64 bytes from %s: icmp_seq=%d time=%d ms\n", ip_str, i, rtt);
            auth_cprint(line);
            success++;
        } else {
            char line[80];
            snprintf(line, sizeof(line), "Request timeout for icmp_seq=%d\n", i);
            auth_cprint(line);
        }
    }

    if (success == 0) return 1;
    return 0;
}
