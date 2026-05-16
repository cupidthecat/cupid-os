#ifndef SOCKET_H
#define SOCKET_H

#include "types.h"

#define SOCK_TYPE_UDP 1
#define SOCK_TYPE_TCP 2

/* RX buffer must hold a TLS record plus a few in-flight TCP segments.
 * 8 KB was too tight for handshakes from servers with deep RSA-4096 cert
 * chains (e.g. iana.org -> ~10 KB Certificate message); peer-side data
 * piled up faster than we drained, segments past the buffer were dropped,
 * and the server eventually closed the connection. 64 KB gives headroom
 * for a full encrypted handshake flight without dropping.*/
#define SOCK_RX_BUF 65536
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

/* setsockopt levels and options. */
#define SOL_TLS    1
#define TLS_ENABLE 1

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

    /* TCP data retransmission (stop-and-wait, one segment in flight). */
    uint8_t  rt_buf[1460];      /* TCP_MSS - last unACKed PSH segment */
    uint32_t rt_len;            /* 0 = nothing in flight */
    uint32_t rt_seq;            /* seq number of rt_buf[0] */
    uint32_t rt_send_tick;      /* timer_get_uptime_ms() at last send */
    uint8_t  rt_attempts;       /* exponential backoff counter */

    struct {
        uint32_t ip; uint16_t port;
        uint32_t iss; uint32_t rcv_nxt;
        uint32_t inserted_ms;
        uint8_t completed;
        uint8_t in_use;
    } lq[LQ_SIZE];

    /* Opaque TLS context - non-NULL means socket_send/recv route
     * through the TLS record layer instead of raw TCP. Allocated by
     * socket_setsockopt(SOL_TLS, TLS_ENABLE), freed by socket_close.*/
    void *tls_ctx;
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
int socket_setsockopt(int fd, int level, int optname,
                      const void *val, uint32_t vlen);
int socket_sendto  (int fd, const void *buf, uint32_t len, uint32_t ip, uint16_t port);
int socket_recvfrom(int fd, void *buf, uint32_t len, uint32_t *ip, uint16_t *port);

/* Non-blocking polling helpers.
 *   socket_avail: bytes pending in rx buffer (>= 0), or negative errno.
 *   socket_state: tcp_state_t for TCP sockets; 0 for UDP; negative errno on bad fd.*/
int socket_avail   (int fd);
int socket_state   (int fd);

/* UDP ingress dispatch - called from udp.c */
void socket_udp_deliver(uint32_t src_ip, uint16_t src_port,
                        uint16_t dst_port, const uint8_t *data, uint32_t dlen);

uint16_t htons(uint16_t v);
uint32_t htonl(uint32_t v);

#endif
