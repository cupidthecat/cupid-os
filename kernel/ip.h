#ifndef IP_H
#define IP_H

#include "types.h"

#define IP_PROTO_ICMP  1
#define IP_PROTO_TCP   6
#define IP_PROTO_UDP   17

/* All IPs and ports in these APIs are network byte order
 * (uint32_t where byte 0 = first octet). */

/* Build IP header, ARP-resolve next hop, prepend Ethernet header, send. */
int ipv4_send(uint32_t dst_ip, uint8_t proto, const uint8_t *payload, uint32_t len);

/* Called from ethernet dispatch in net_process_pending. */
void ipv4_input(const uint8_t *frame, uint32_t len);

uint16_t ip_checksum(const uint8_t *data, uint32_t len);

/* Parse dotted-quad "A.B.C.D" into a uint32_t (byte 0 = first octet).
 * Returns 0 on success, -1 on parse failure. */
int ip_parse(const char *s, uint32_t *out);

#endif
