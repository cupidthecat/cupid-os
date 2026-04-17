#include "ip.h"
#include "tcp.h"
#include "net_if.h"
#include "arp.h"
#include "icmp.h"
#include "udp.h"
#include "../drivers/serial.h"

typedef struct __attribute__((packed)) {
    uint8_t  vhl;          /* 0x45 = version 4, IHL 5 */
    uint8_t  tos;
    uint16_t total_len;    /* big-endian on wire */
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  proto;
    uint16_t hdr_csum;
    uint32_t src_ip;       /* first-octet-in-low-byte (network byte order) */
    uint32_t dst_ip;
} ipv4_hdr_t;

static uint16_t ip_id_counter = 0;

uint16_t ip_checksum(const uint8_t *data, uint32_t len) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i + 1u < len; i += 2u) {
        sum += ((uint32_t)data[i] << 8) | data[i + 1u];
    }
    if (len & 1u) sum += ((uint32_t)data[len - 1u] << 8);
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)(~sum & 0xFFFFu);
}

static uint16_t be16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }

int ipv4_send(uint32_t dst_ip, uint8_t proto, const uint8_t *payload, uint32_t plen) {
    net_if_t *nif = net_if_primary();
    if (!nif || !nif->link_up) return -1;
    if (nif->ipv4_addr == 0u) return -1;
    if (plen + 20u > NET_IF_MTU) return -1;

    /* Pick next-hop IP */
    uint32_t next_hop;
    if ((dst_ip & nif->ipv4_mask) == (nif->ipv4_addr & nif->ipv4_mask)) {
        next_hop = dst_ip;
    } else {
        next_hop = nif->ipv4_gateway;
    }

    /* Resolve MAC */
    uint8_t dst_mac[6];
    if (dst_ip == 0xFFFFFFFFu) {
        for (int i = 0; i < 6; i++) dst_mac[i] = 0xFFu;
    } else {
        if (arp_resolve(next_hop, dst_mac) != 0) {
            nif->tx_errors++;
            return -1;
        }
    }

    /* Build frame: eth(14) + ipv4(20) + payload */
    uint8_t frame[14u + 20u + NET_IF_MTU];
    for (int i = 0; i < 6; i++) frame[i] = dst_mac[i];
    for (int i = 0; i < 6; i++) frame[6 + i] = nif->mac[i];
    frame[12] = 0x08; frame[13] = 0x00;

    ipv4_hdr_t *h = (ipv4_hdr_t*)(frame + 14);
    h->vhl = 0x45u;
    h->tos = 0;
    h->total_len = be16((uint16_t)(20u + plen));
    h->id = be16(ip_id_counter);
    ip_id_counter++;
    h->flags_frag = be16(0x4000u);   /* DF */
    h->ttl = 64;
    h->proto = proto;
    h->hdr_csum = 0;
    h->src_ip = nif->ipv4_addr;
    h->dst_ip = dst_ip;

    uint16_t cs = ip_checksum((uint8_t*)h, 20);
    h->hdr_csum = be16(cs);

    for (uint32_t i = 0; i < plen; i++) frame[14u + 20u + i] = payload[i];

    return nif->send(nif, frame, 14u + 20u + plen);
}

void ipv4_input(const uint8_t *frame, uint32_t len) {
    if (len < 14u + 20u) return;
    const ipv4_hdr_t *h = (const ipv4_hdr_t*)(frame + 14);
    if ((h->vhl & 0xF0u) != 0x40u) return;
    uint8_t ihl = (uint8_t)(h->vhl & 0x0Fu);
    if (ihl < 5) return;
    uint32_t hdr_len = (uint32_t)ihl * 4u;
    if (14u + hdr_len > len) return;

    uint16_t total = be16(h->total_len);
    if ((uint32_t)14u + total > len) return;

    /* Checksum header */
    uint8_t tmp[60];
    uint32_t cp = hdr_len;
    if (cp > sizeof(tmp)) cp = sizeof(tmp);
    for (uint32_t i = 0; i < cp; i++) tmp[i] = ((const uint8_t*)h)[i];
    if (ip_checksum(tmp, cp) != 0u) return;

    /* Drop fragmented */
    uint16_t ff = be16(h->flags_frag);
    if ((ff & 0x1FFFu) != 0u || (ff & 0x2000u) != 0u) {
        KWARN("ip: dropping fragmented packet");
        return;
    }

    net_if_t *nif = net_if_primary();
    uint32_t src = h->src_ip;
    uint32_t dst = h->dst_ip;
    if (nif && dst != nif->ipv4_addr && dst != 0xFFFFFFFFu) return;

    const uint8_t *payload = frame + 14u + hdr_len;
    uint32_t plen = (uint32_t)total - hdr_len;

    switch (h->proto) {
    case IP_PROTO_ICMP: icmp_input(src, payload, plen); break;
    case IP_PROTO_UDP:  udp_input (src, payload, plen); break;
    case IP_PROTO_TCP:  tcp_input (src, payload, plen); break;
    default: break;
    }
}

int ip_parse(const char *s, uint32_t *out) {
    uint32_t oct[4] = {0u, 0u, 0u, 0u};
    int idx = 0;
    int have_digit = 0;
    if (!s || !out) return -1;
    while (*s && idx < 4) {
        if (*s >= '0' && *s <= '9') {
            oct[idx] = oct[idx] * 10u + (uint32_t)(*s - '0');
            if (oct[idx] > 255u) return -1;
            have_digit = 1;
        } else if (*s == '.') {
            if (!have_digit) return -1;
            idx++;
            have_digit = 0;
        } else {
            return -1;
        }
        s++;
    }
    if (idx != 3 || !have_digit) return -1;
    *out = oct[0] | (oct[1] << 8) | (oct[2] << 16) | (oct[3] << 24);
    return 0;
}
