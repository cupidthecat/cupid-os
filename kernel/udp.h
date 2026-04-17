#ifndef UDP_H
#define UDP_H

#include "types.h"

/* src_ip is network byte order (uint32_t with low byte = first octet).
 * All ports are HOST byte order — conversion happens inside udp_send_raw. */
void udp_input(uint32_t src_ip, const uint8_t *buf, uint32_t len);

int udp_send_raw(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                 const uint8_t *data, uint32_t dlen);

#endif
