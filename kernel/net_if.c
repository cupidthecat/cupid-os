#include "net_if.h"
#include "arp.h"
#include "ip.h"
#include "tcp.h"
#include "dhcp.h"
#include "../drivers/serial.h"

static net_if_t *registered_nif = NULL;

typedef struct {
    net_if_t *nif;
    uint32_t  len;
    uint8_t   frame[NET_IF_MTU + 14];
} net_rx_slot_t;

static net_rx_slot_t rx_ring[NET_RX_RING_SIZE] __attribute__((aligned(64)));
static volatile uint32_t rx_head = 0;
static volatile uint32_t rx_tail = 0;

int net_if_register(net_if_t *nif) {
    if (registered_nif) { KWARN("net: multiple NICs — only primary used"); return -1; }
    registered_nif = nif;
    KINFO("net: registered %s mac=%x:%x:%x:%x:%x:%x",
          nif->name, nif->mac[0], nif->mac[1], nif->mac[2],
          nif->mac[3], nif->mac[4], nif->mac[5]);
    return 0;
}

net_if_t *net_if_primary(void) { return registered_nif; }

void net_rx_enqueue(net_if_t *nif, const uint8_t *frame, uint32_t len) {
    if (!nif || !frame || len == 0 || len > NET_IF_MTU + 14) return;
    uint32_t next = (rx_head + 1u) % NET_RX_RING_SIZE;
    if (next == rx_tail) {
        nif->rx_drops++;
        return;  /* ring full */
    }
    net_rx_slot_t *s = &rx_ring[rx_head];
    s->nif = nif;
    s->len = len;
    for (uint32_t i = 0; i < len; i++) s->frame[i] = frame[i];
    rx_head = next;
}

void net_process_pending(void) {
    while (rx_tail != rx_head) {
        net_rx_slot_t *s = &rx_ring[rx_tail];
        if (s->len >= 14u) {
            uint16_t ethertype = (uint16_t)(((uint16_t)s->frame[12] << 8) | s->frame[13]);
            if (ethertype == ETHERTYPE_ARP) {
                arp_input(s->nif, s->frame, s->len);
            } else if (ethertype == ETHERTYPE_IPV4) {
                ipv4_input(s->frame, s->len);
            }
            s->nif->rx_packets++;
        }
        rx_tail = (rx_tail + 1u) % NET_RX_RING_SIZE;
    }
    tcp_tick();
}

extern void rtl8139_probe(void);
extern void e1000_probe(void);

void net_init(void) {
    rtl8139_probe();
    if (!registered_nif) e1000_probe();
    if (!registered_nif) { KWARN("net: no supported NIC"); return; }
    (void)dhcp_start(registered_nif);   /* populates IP (DHCP or fallback) */
    uint8_t *ip = (uint8_t *)&registered_nif->ipv4_addr;
    KINFO("net: if=%s ip=%u.%u.%u.%u",
          registered_nif->name, ip[0], ip[1], ip[2], ip[3]);
}
