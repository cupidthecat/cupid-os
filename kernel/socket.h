#ifndef SOCKET_H
#define SOCKET_H

#include "types.h"

#define SOCK_TYPE_UDP 1
#define SOCK_TYPE_TCP 2

#define SOCK_RX_BUF 8192
#define SOCK_TX_BUF 8192
#define SOCKET_MAX  32
#define LQ_SIZE     8

typedef enum {
    TCPS_CLOSED = 0, TCPS_LISTEN, TCPS_SYN_SENT, TCPS_SYN_RCVD,
    TCPS_ESTABLISHED, TCPS_FIN_WAIT_1, TCPS_FIN_WAIT_2,
    TCPS_TIME_WAIT, TCPS_CLOSE_WAIT, TCPS_LAST_ACK
} tcp_state_t;

/* Error codes (negative returns) */
#define ENETDOWN        -1
#define EBADF           -2
#define EINVAL_SOCK     -3
#define EADDRINUSE      -4
#define ECONNREFUSED    -5
#define ETIMEDOUT_SOCK  -6
#define ECONNRESET      -7
#define ENOBUFS_SOCK    -8

#define UDP_MAX_QUEUED 8

typedef struct {
    uint32_t ip;
    uint16_t port;
    uint16_t len;
} udp_dgram_meta_t;

typedef struct socket_t {
    uint8_t  type;
    uint8_t  in_use;
    uint32_t local_ip;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;
    tcp_state_t tcp_state;

    uint8_t  tx_buf[SOCK_TX_BUF];
    uint32_t tx_head, tx_tail;
    uint8_t  rx_buf[SOCK_RX_BUF];
    uint32_t rx_head, rx_tail;

    udp_dgram_meta_t udp_meta[UDP_MAX_QUEUED];
    uint8_t  udp_meta_head, udp_meta_tail;

    /* TCP state (used from T13) */
    uint32_t snd_una, snd_nxt, snd_wnd, snd_iss;
    uint32_t rcv_nxt, rcv_wnd, rcv_irs;
    uint32_t last_rexmit_tick;
    uint32_t time_wait_start;

    struct {
        uint32_t ip; uint16_t port;
        uint32_t iss; uint32_t rcv_nxt;
        uint8_t completed;
        uint8_t in_use;
    } lq[LQ_SIZE];
} socket_t;

extern socket_t sockets[SOCKET_MAX];

/* BSD API */
int socket_create  (int type);
int socket_bind    (int fd, uint32_t ip, uint16_t port);
int socket_listen  (int fd, int backlog);
int socket_accept  (int fd, uint32_t *peer_ip, uint16_t *peer_port);
int socket_connect (int fd, uint32_t ip, uint16_t port);
int socket_send    (int fd, const void *buf, uint32_t len);
int socket_recv    (int fd, void *buf, uint32_t len);
int socket_close   (int fd);
int socket_sendto  (int fd, const void *buf, uint32_t len, uint32_t ip, uint16_t port);
int socket_recvfrom(int fd, void *buf, uint32_t len, uint32_t *ip, uint16_t *port);

/* UDP ingress dispatch — called from udp.c */
void socket_udp_deliver(uint32_t src_ip, uint16_t src_port,
                        uint16_t dst_port, const uint8_t *data, uint32_t dlen);

uint16_t htons(uint16_t v);
uint32_t htonl(uint32_t v);

#endif
