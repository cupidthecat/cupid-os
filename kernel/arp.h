#ifndef ARP_H
#define ARP_H

#include "types.h"
#include "net_if.h"

#define ETHERTYPE_IPV4 0x0800u
#define ETHERTYPE_ARP  0x0806u

/* Blocking resolve. 500ms timeout. Returns 0 on success; -1 on timeout. */
int arp_resolve(uint32_t ipv4_target, uint8_t mac_out[6]);

/* Called by net_process_pending when ethertype == ARP. */
void arp_input(net_if_t *nif, const uint8_t *frame, uint32_t len);

/* Debug dump */
void arp_dump(void);

/* Iterate valid entries. Returns the number of valid entries, and fills
 * ips[], macs[] for up to max entries. macs[i][0..5] = 6-byte MAC. */
int arp_get_entries(uint32_t *ips, uint8_t macs[][6], int max);

#endif
