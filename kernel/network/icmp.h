#ifndef ICMP_H
#define ICMP_H

#include "types.h"

void icmp_input(uint32_t src_ip, const uint8_t *buf, uint32_t len);

/* Send an ICMP echo request with given id/seq. payload_len is extra data
 * beyond the 8-byte ICMP header (0..1472 bytes). Returns 0 on success. */
int icmp_send_echo(uint32_t dst_ip, uint16_t id, uint16_t seq,
                   uint32_t payload_len);

/* Block until a matching echo reply arrives or timeout_ms elapses.
 * Returns RTT in ms on success, -1 on timeout. Only one wait at a time. */
int icmp_wait_reply(uint32_t expected_src_ip, uint16_t id, uint16_t seq,
                    uint32_t timeout_ms);

#endif
