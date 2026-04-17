#ifndef TCP_H
#define TCP_H

#include "types.h"

#define TCP_MSS          1460
#define TCP_RTO_MS       500
#define TCP_TIME_WAIT_MS 60000

/* Called from ipv4_input when proto == 6. */
void tcp_input(uint32_t src_ip, const uint8_t *buf, uint32_t len);

/* Periodic tick from net_process_pending — drives retransmit + timers. */
void tcp_tick(void);

/* Socket-layer entry points (called from socket.c). */
int tcp_connect(int fd, uint32_t ip, uint16_t port);
int tcp_send   (int fd, const uint8_t *buf, uint32_t len);
int tcp_recv   (int fd, uint8_t *buf, uint32_t len);
int tcp_close  (int fd);

/* Server path — stubs here, real impl in T14. */
int tcp_listen (int fd, int backlog);
int tcp_accept (int fd, uint32_t *peer_ip, uint16_t *peer_port);

#endif
