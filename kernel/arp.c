#include "arp.h"
#include "net_if.h"
#include "../drivers/serial.h"
#include "../drivers/timer.h"

#define ARP_CACHE_SIZE 16

typedef struct {
    uint32_t ip;           /* network byte order */
    uint8_t  mac[6];
    uint32_t last_used_ms;
    bool     valid;
} arp_entry_t;

static arp_entry_t cache[ARP_CACHE_SIZE];

static arp_entry_t *cache_find(uint32_t ip) {
    int i;
    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].ip == ip) return &cache[i];
    }
    return 0;
}

static arp_entry_t *cache_slot_for(uint32_t ip) {
    arp_entry_t *free_slot = 0;
    arp_entry_t *lru = &cache[0];
    int i;
    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].ip == ip) return &cache[i];
        if (!cache[i].valid && !free_slot) free_slot = &cache[i];
        if (cache[i].valid && cache[i].last_used_ms < lru->last_used_ms) lru = &cache[i];
    }
    return free_slot ? free_slot : lru;
}

static void cache_put(uint32_t ip, const uint8_t mac[6]) {
    arp_entry_t *e = cache_slot_for(ip);
    int i;
    e->ip = ip;
    for (i = 0; i < 6; i++) e->mac[i] = mac[i];
    e->last_used_ms = timer_get_uptime_ms();
    e->valid = true;
}

static void build_frame(uint8_t *out, const uint8_t *dst_mac, const uint8_t *src_mac,
                        uint16_t ethertype, const uint8_t *payload, uint32_t plen) {
    uint32_t i;
    for (i = 0; i < 6u; i++) out[i] = dst_mac[i];
    for (i = 0; i < 6u; i++) out[6u + i] = src_mac[i];
    out[12] = (uint8_t)(ethertype >> 8);
    out[13] = (uint8_t)(ethertype & 0xFFu);
    for (i = 0; i < plen; i++) out[14u + i] = payload[i];
}

static void send_arp_request(net_if_t *nif, uint32_t target_ip) {
    uint8_t arp[28];
    uint8_t frame[60];
    uint8_t bcast[6] = { 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu };
    int i;

    arp[0] = 0x00; arp[1] = 0x01;   /* HTYPE = Ethernet */
    arp[2] = 0x08; arp[3] = 0x00;   /* PTYPE = IPv4 */
    arp[4] = 6;    arp[5] = 4;
    arp[6] = 0x00; arp[7] = 0x01;   /* OPER = request */
    for (i = 0; i < 6; i++) arp[8 + i] = nif->mac[i];  /* SHA */
    arp[14] = (uint8_t)(nif->ipv4_addr);
    arp[15] = (uint8_t)(nif->ipv4_addr >> 8);
    arp[16] = (uint8_t)(nif->ipv4_addr >> 16);
    arp[17] = (uint8_t)(nif->ipv4_addr >> 24);
    for (i = 0; i < 6; i++) arp[18 + i] = 0;           /* THA zero */
    arp[24] = (uint8_t)(target_ip);
    arp[25] = (uint8_t)(target_ip >> 8);
    arp[26] = (uint8_t)(target_ip >> 16);
    arp[27] = (uint8_t)(target_ip >> 24);

    for (i = 0; i < 60; i++) frame[i] = 0;
    build_frame(frame, bcast, nif->mac, ETHERTYPE_ARP, arp, 28u);
    nif->send(nif, frame, 14u + 28u);
}

static void send_arp_reply(net_if_t *nif, const uint8_t *req_sha, uint32_t req_spa) {
    uint8_t arp[28];
    uint8_t frame[60];
    int i;

    arp[0] = 0x00; arp[1] = 0x01;
    arp[2] = 0x08; arp[3] = 0x00;
    arp[4] = 6;    arp[5] = 4;
    arp[6] = 0x00; arp[7] = 0x02;
    for (i = 0; i < 6; i++) arp[8 + i] = nif->mac[i];
    arp[14] = (uint8_t)(nif->ipv4_addr);
    arp[15] = (uint8_t)(nif->ipv4_addr >> 8);
    arp[16] = (uint8_t)(nif->ipv4_addr >> 16);
    arp[17] = (uint8_t)(nif->ipv4_addr >> 24);
    for (i = 0; i < 6; i++) arp[18 + i] = req_sha[i];
    arp[24] = (uint8_t)(req_spa);
    arp[25] = (uint8_t)(req_spa >> 8);
    arp[26] = (uint8_t)(req_spa >> 16);
    arp[27] = (uint8_t)(req_spa >> 24);

    for (i = 0; i < 60; i++) frame[i] = 0;
    build_frame(frame, req_sha, nif->mac, ETHERTYPE_ARP, arp, 28u);
    nif->send(nif, frame, 14u + 28u);
}

void arp_input(net_if_t *nif, const uint8_t *frame, uint32_t len) {
    const uint8_t *arp;
    uint16_t htype, ptype, oper;
    uint32_t spa, tpa;

    if (len < 14u + 28u) return;
    arp = frame + 14;
    htype = (uint16_t)(((uint16_t)arp[0] << 8) | arp[1]);
    ptype = (uint16_t)(((uint16_t)arp[2] << 8) | arp[3]);
    if (htype != 1u || ptype != 0x0800u) return;
    if (arp[4] != 6u || arp[5] != 4u) return;
    oper = (uint16_t)(((uint16_t)arp[6] << 8) | arp[7]);

    /* IP addresses stored as uint32_t where byte 0 (low byte) = first octet.
     * Wire is big-endian (first octet first), so first wire byte -> low byte. */
    spa = (uint32_t)arp[14]
        | ((uint32_t)arp[15] << 8)
        | ((uint32_t)arp[16] << 16)
        | ((uint32_t)arp[17] << 24);
    tpa = (uint32_t)arp[24]
        | ((uint32_t)arp[25] << 8)
        | ((uint32_t)arp[26] << 16)
        | ((uint32_t)arp[27] << 24);

    if (oper == 1u) {
        if (tpa == nif->ipv4_addr) {
            send_arp_reply(nif, arp + 8, spa);
            cache_put(spa, arp + 8);
        }
    } else if (oper == 2u) {
        cache_put(spa, arp + 8);
    }
}

int arp_resolve(uint32_t ip, uint8_t mac_out[6]) {
    arp_entry_t *e;
    net_if_t *nif;
    uint32_t spins;
    int i;

    e = cache_find(ip);
    if (e) {
        e->last_used_ms = timer_get_uptime_ms();
        for (i = 0; i < 6; i++) mac_out[i] = e->mac[i];
        return 0;
    }
    nif = net_if_primary();
    if (!nif) return -1;
    send_arp_request(nif, ip);

    extern void net_process_pending(void);
    /* Wait up to ~500ms using TSC-based delay so we work correctly when
     * BKL is held (IF=0 → timer IRQ frozen). Poll HW RX directly for the
     * same reason — the NIC IRQ won't fire with interrupts disabled. */
    for (spins = 0; spins < 500u; spins++) {
        if (nif->poll_rx) nif->poll_rx(nif);
        net_process_pending();
        e = cache_find(ip);
        if (e) {
            for (i = 0; i < 6; i++) mac_out[i] = e->mac[i];
            return 0;
        }
        timer_delay_us(1000u);
    }
    return -1;
}

void arp_dump(void) {
    int i;
    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!cache[i].valid) continue;
        KINFO("arp[%d] ip=%x mac=%x:%x:%x:%x:%x:%x",
              i, cache[i].ip,
              cache[i].mac[0], cache[i].mac[1], cache[i].mac[2],
              cache[i].mac[3], cache[i].mac[4], cache[i].mac[5]);
    }
}

int arp_get_entries(uint32_t *ips, uint8_t macs[][6], int max) {
    int i, n = 0, j;
    for (i = 0; i < ARP_CACHE_SIZE && n < max; i++) {
        if (!cache[i].valid) continue;
        ips[n] = cache[i].ip;
        for (j = 0; j < 6; j++) macs[n][j] = cache[i].mac[j];
        n++;
    }
    return n;
}
