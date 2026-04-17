#ifndef NET_IF_H
#define NET_IF_H

#include "types.h"

#define NET_IF_MTU       1500
#define NET_IF_MAC_LEN   6
#define NET_RX_RING_SIZE 64

typedef struct net_if {
    const char *name;
    uint8_t     mac[NET_IF_MAC_LEN];
    uint32_t    ipv4_addr;        /* network byte order */
    uint32_t    ipv4_mask;
    uint32_t    ipv4_gateway;
    uint32_t    ipv4_dns;
    bool        link_up;
    void       *driver_data;
    int       (*send)(struct net_if *, const uint8_t *frame, uint32_t len);
    /* Optional: direct HW ring drain. Callable with IRQs disabled (BKL path).
     * NULL means driver does not support polled RX. */
    void      (*poll_rx)(struct net_if *);
    uint64_t    rx_packets, tx_packets, rx_drops, tx_errors;
} net_if_t;

int  net_if_register(net_if_t *nif);
net_if_t *net_if_primary(void);

/* IRQ-safe lockless push. Callable from top-half. */
void net_rx_enqueue(net_if_t *nif, const uint8_t *frame, uint32_t len);

/* Drained from idle loop. Parses Ethernet + dispatches to ARP / IPv4. */
void net_process_pending(void);

/* Boot entry: scan PCI, register driver, run DHCP. Called from kernel_main. */
void net_init(void);

#endif
