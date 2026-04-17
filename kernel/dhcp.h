#ifndef DHCP_H
#define DHCP_H

#include "types.h"
#include "net_if.h"

/* Blocks up to ~3s. Returns true on success (net_if populated),
 * false on timeout — caller has already had static fallback applied. */
bool dhcp_start(net_if_t *nif);

/* Called from udp_input when dst_port == 68. Returns true if consumed. */
bool dhcp_rx_intercept(uint32_t src_ip, const uint8_t *data, uint32_t dlen);

#endif
