#include "icmp.h"
#include "ip.h"
#include "net_if.h"
#include "../drivers/timer.h"

/* ICMP header: type(1) code(1) csum(2) rest-of-header(4) payload */

extern void net_process_pending(void);

/* Track reply acks across a ping run. Since ping can fire 1..N sends
 * before any reply arrives, we need a bitmap per-seq instead of a single
 * waiter. Reset whenever the calling id/dst changes. */
static uint16_t cur_id;
static uint32_t cur_src_ip;
static uint8_t  seen[256];

void icmp_input(uint32_t src_ip, const uint8_t *buf, uint32_t len) {
    if (len < 8u) return;

    if (buf[0] == 8u) {
        /* Echo request: flip type to 0, recompute checksum, send back. */
        uint8_t reply[1500];
        uint32_t i;
        uint16_t cs;
        if (len > sizeof(reply)) return;
        for (i = 0; i < len; i++) reply[i] = buf[i];
        reply[0] = 0u;
        reply[2] = 0u; reply[3] = 0u;
        cs = ip_checksum(reply, len);
        reply[2] = (uint8_t)(cs >> 8);
        reply[3] = (uint8_t)(cs & 0xFFu);
        (void)ipv4_send(src_ip, 1u, reply, len);
        return;
    }

    if (buf[0] == 0u) {
        uint16_t rid  = (uint16_t)(((uint16_t)buf[4] << 8) | buf[5]);
        uint16_t rseq = (uint16_t)(((uint16_t)buf[6] << 8) | buf[7]);
        if (rid == cur_id && src_ip == cur_src_ip) {
            seen[rseq & 0xFFu] = 1u;
        }
    }
}

int icmp_send_echo(uint32_t dst_ip, uint16_t id, uint16_t seq,
                   uint32_t payload_len) {
    uint8_t buf[8 + 1472];
    uint32_t i;
    uint32_t total;
    uint16_t cs;

    if (payload_len > 1472u) payload_len = 1472u;
    total = 8u + payload_len;

    /* New ping run: reset seen bitmap. */
    if (id != cur_id || dst_ip != cur_src_ip) {
        uint32_t j;
        cur_id = id;
        cur_src_ip = dst_ip;
        for (j = 0; j < 256u; j++) seen[j] = 0u;
    }

    buf[0] = 8u;                          /* type: echo request */
    buf[1] = 0u;
    buf[2] = 0u; buf[3] = 0u;             /* checksum placeholder */
    buf[4] = (uint8_t)(id >> 8);
    buf[5] = (uint8_t)(id & 0xFFu);
    buf[6] = (uint8_t)(seq >> 8);
    buf[7] = (uint8_t)(seq & 0xFFu);
    for (i = 0; i < payload_len; i++) buf[8u + i] = (uint8_t)(i & 0xFFu);

    cs = ip_checksum(buf, total);
    buf[2] = (uint8_t)(cs >> 8);
    buf[3] = (uint8_t)(cs & 0xFFu);

    return ipv4_send(dst_ip, 1u, buf, total);
}

int icmp_wait_reply(uint32_t expected_src_ip, uint16_t id, uint16_t seq,
                    uint32_t timeout_ms) {
    extern net_if_t *net_if_primary(void);
    net_if_t *pn = net_if_primary();
    uint32_t polls;
    (void)expected_src_ip; (void)id;
    for (polls = 0; polls < timeout_ms; polls++) {
        if (pn && pn->poll_rx) pn->poll_rx(pn);
        net_process_pending();
        if (seen[seq & 0xFFu]) {
            seen[seq & 0xFFu] = 0u;
            return (int)polls;
        }
        timer_delay_us(1000u);
    }
    return -1;
}
