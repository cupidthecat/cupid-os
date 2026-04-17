#include "udp.h"
#include "ip.h"
#include "net_if.h"
#include "dhcp.h"
#include "../drivers/serial.h"

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} udp_hdr_t;

/* Socket dispatch hook — filled in T10 by socket.c. */
void socket_udp_deliver(uint32_t src_ip, uint16_t src_port,
                        uint16_t dst_port, const uint8_t *data, uint32_t dlen);
__attribute__((weak)) void socket_udp_deliver(uint32_t src_ip, uint16_t src_port,
                                              uint16_t dst_port, const uint8_t *data,
                                              uint32_t dlen) {
    (void)src_ip; (void)src_port; (void)dst_port; (void)data; (void)dlen;
}

static uint16_t be16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }

/* UDP checksum: pseudo-header (src_ip 4, dst_ip 4, zero 1, proto 1, udp_len 2)
 * + UDP header + data, ones-complement. */
static uint16_t udp_csum(uint32_t src_ip, uint32_t dst_ip,
                         const uint8_t *udp_pkt, uint32_t len) {
    uint32_t sum = 0;
    const uint8_t *sp = (const uint8_t*)&src_ip;
    const uint8_t *dp = (const uint8_t*)&dst_ip;
    sum += ((uint32_t)sp[0] << 8) | sp[1];
    sum += ((uint32_t)sp[2] << 8) | sp[3];
    sum += ((uint32_t)dp[0] << 8) | dp[1];
    sum += ((uint32_t)dp[2] << 8) | dp[3];
    sum += 17u;          /* proto UDP */
    sum += len;          /* udp_len */
    for (uint32_t i = 0; i + 1u < len; i += 2u) {
        sum += ((uint32_t)udp_pkt[i] << 8) | udp_pkt[i + 1u];
    }
    if (len & 1u) sum += ((uint32_t)udp_pkt[len - 1u] << 8);
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    uint16_t c = (uint16_t)(~sum & 0xFFFFu);
    return c == 0u ? 0xFFFFu : c;
}

int udp_send_raw(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                 const uint8_t *data, uint32_t dlen) {
    if (dlen > 1472u) return -1;
    uint8_t pkt[8u + 1472u];
    udp_hdr_t *h = (udp_hdr_t*)pkt;
    h->src_port = be16(src_port);
    h->dst_port = be16(dst_port);
    h->length   = be16((uint16_t)(8u + dlen));
    h->checksum = 0;
    for (uint32_t i = 0; i < dlen; i++) pkt[8u + i] = data[i];

    net_if_t *nif = net_if_primary();
    uint32_t src_ip = nif ? nif->ipv4_addr : 0u;
    uint16_t cs = udp_csum(src_ip, dst_ip, pkt, 8u + dlen);
    h->checksum = be16(cs);

    return ipv4_send(dst_ip, 17u /* UDP */, pkt, 8u + dlen);
}

void udp_input(uint32_t src_ip, const uint8_t *buf, uint32_t len) {
    if (len < 8u) return;
    const udp_hdr_t *h = (const udp_hdr_t*)buf;
    uint16_t src_port = be16(h->src_port);
    uint16_t dst_port = be16(h->dst_port);
    uint16_t ulen     = be16(h->length);
    if (ulen < 8u || (uint32_t)ulen > len) return;

    if (dst_port == 68u) {
        if (dhcp_rx_intercept(src_ip, buf + 8u, (uint32_t)ulen - 8u)) return;
    }

    socket_udp_deliver(src_ip, src_port, dst_port, buf + 8u, (uint32_t)ulen - 8u);
}
