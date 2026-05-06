#include "socket.h"
#include "tcp.h"
#include "udp.h"
#include "bkl.h"
#include "process.h"
#include "memory.h"
#include "tls/tls_ctx.h"
#include "../drivers/timer.h"
#include "../drivers/rtc.h"
#include "../drivers/serial.h"

socket_t sockets[SOCKET_MAX];
static uint16_t ephemeral_next = 49152u;

uint16_t htons(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
/* ntohs is the same byte-swap as htons on this little-endian target. */
static uint16_t ntohs(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }

uint32_t htonl(uint32_t v) {
    return ((v >> 24) & 0xFFu) | ((v >> 8) & 0xFF00u)
         | ((v << 8)  & 0xFF0000u) | ((v << 24) & 0xFF000000u);
}

static void zero_socket(socket_t *s) {
    uint32_t k;
    for (k = 0; k < (uint32_t)sizeof(*s); k++) ((uint8_t*)s)[k] = 0u;
}

static int alloc_socket(void) {
    int i;
    for (i = 0; i < SOCKET_MAX; i++) {
        if (!sockets[i].in_use) {
            zero_socket(&sockets[i]);
            sockets[i].in_use = 1;
            return i;
        }
    }
    return ENOBUFS_SOCK;
}

static bool port_in_use(uint16_t port) {
    int i;
    for (i = 0; i < SOCKET_MAX; i++)
        if (sockets[i].in_use && sockets[i].local_port == port) return true;
    return false;
}

static uint16_t alloc_ephemeral(void) {
    int tries;
    for (tries = 0; tries < 16384; tries++) {
        uint16_t p = ephemeral_next++;
        if (ephemeral_next < 49152u || ephemeral_next == 0u) ephemeral_next = 49152u;
        if (!port_in_use(p)) return p;
    }
    return 0u;
}

int socket_create(int type) {
    int fd;
    if (type != SOCK_TYPE_UDP && type != SOCK_TYPE_TCP) return EINVAL_SOCK;
    bkl_lock();
    fd = alloc_socket();
    if (fd >= 0) {
        sockets[fd].type = (uint8_t)type;
        sockets[fd].tcp_state = TCPS_CLOSED;
    }
    bkl_unlock();
    return fd;
}

int socket_bind(int fd, uint32_t ip, uint16_t port) {
    int r = 0;
    socket_t *s;
    /* BSD convention: callers pass ports in network byte order (htons'd).
     * Internally we store host order so it can be compared against the
     * host-order dst_port produced by tcp/udp input parsers. */
    uint16_t host_port = (port != 0u) ? ntohs(port) : 0u;
    if (fd < 0 || fd >= SOCKET_MAX) return EBADF;
    bkl_lock();
    s = &sockets[fd];
    if (!s->in_use) r = EBADF;
    else if (host_port != 0u && port_in_use(host_port)) r = EADDRINUSE;
    else {
        s->local_ip = ip;
        s->local_port = (host_port != 0u) ? host_port : alloc_ephemeral();
    }
    bkl_unlock();
    return r;
}

int socket_sendto(int fd, const void *buf, uint32_t len, uint32_t ip, uint16_t port) {
    socket_t *s;
    uint16_t local_port;
    if (fd < 0 || fd >= SOCKET_MAX) return EBADF;
    bkl_lock();
    s = &sockets[fd];
    if (!s->in_use || s->type != SOCK_TYPE_UDP) {
        bkl_unlock();
        return EBADF;
    }
    if (s->local_port == 0u) s->local_port = alloc_ephemeral();
    local_port = s->local_port;
    bkl_unlock();
    /* udp_send_raw calls ipv4_send→arp_resolve which busy-waits; must NOT
     * run under BKL (BKL disables IRQs → timer freeze + no NIC RX). */
    return udp_send_raw(ip, local_port, ntohs(port), (const uint8_t*)buf, len);
}

int socket_recvfrom(int fd, void *buf, uint32_t len, uint32_t *ip, uint16_t *port) {
    uint32_t start;
    if (fd < 0 || fd >= SOCKET_MAX) return EBADF;
    start = timer_get_uptime_ms();
    for (;;) {
        socket_t *s;
        bkl_lock();
        s = &sockets[fd];
        if (!s->in_use || s->type != SOCK_TYPE_UDP) { bkl_unlock(); return EBADF; }
        if (s->udp_meta_head != s->udp_meta_tail) {
            udp_dgram_meta_t m = s->udp_meta[s->udp_meta_tail];
            uint32_t dlen = m.len;
            uint32_t i;
            if (dlen > len) dlen = len;
            for (i = 0; i < dlen; i++) {
                ((uint8_t*)buf)[i] = s->rx_buf[(s->rx_head + i) % SOCK_RX_BUF];
            }
            s->rx_head = (s->rx_head + m.len) % SOCK_RX_BUF;
            s->udp_meta_tail = (uint8_t)((s->udp_meta_tail + 1u) % UDP_MAX_QUEUED);
            if (ip)   *ip   = m.ip;
            /* Caller will ntohs() — return network byte order. */
            if (port) *port = htons(m.port);
            bkl_unlock();
            return (int)dlen;
        }
        bkl_unlock();
        if (timer_get_uptime_ms() - start > 30000u) return ETIMEDOUT_SOCK;
        {
            int pause_i;
            for (pause_i = 0; pause_i < 1000; pause_i++) __asm__ volatile("pause");
        }
        schedule();
    }
}

int socket_close(int fd) {
    socket_t *s;
    int r = 0;
    if (fd < 0 || fd >= SOCKET_MAX) return EBADF;
    bkl_lock();
    s = &sockets[fd];
    if (!s->in_use) {
        r = EBADF;
        bkl_unlock();
        return r;
    }
    if (s->tls_ctx != NULL) {
        tls_ctx_t *t = (tls_ctx_t *)s->tls_ctx;
        s->tls_ctx = NULL;
        bkl_unlock();
        tls_close_notify(t);
        tls_ctx_destroy(t);
        kfree(t);
        bkl_lock();
        s = &sockets[fd];
    }
    if (s->type == SOCK_TYPE_TCP) {
        bkl_unlock();
        tcp_close(fd);
        return 0;
    }
    s->in_use = 0;
    s->type   = 0;
    bkl_unlock();
    return r;
}

/* Called from udp.c — delivers an incoming UDP datagram to the matching socket. */
void socket_udp_deliver(uint32_t src_ip, uint16_t src_port,
                        uint16_t dst_port, const uint8_t *data, uint32_t dlen) {
    int i;
    for (i = 0; i < SOCKET_MAX; i++) {
        socket_t *s = &sockets[i];
        uint8_t next_meta;
        uint32_t used;
        uint32_t k;
        udp_dgram_meta_t *m;

        if (!s->in_use || s->type != SOCK_TYPE_UDP) continue;
        if (s->local_port != dst_port) continue;

        next_meta = (uint8_t)((s->udp_meta_head + 1u) % UDP_MAX_QUEUED);
        if (next_meta == s->udp_meta_tail) return;   /* meta queue full */

        if (s->rx_tail >= s->rx_head) used = s->rx_tail - s->rx_head;
        else used = SOCK_RX_BUF - s->rx_head + s->rx_tail;
        if (used + dlen >= SOCK_RX_BUF) return;      /* no room */

        for (k = 0; k < dlen; k++) {
            s->rx_buf[s->rx_tail] = data[k];
            s->rx_tail = (s->rx_tail + 1u) % SOCK_RX_BUF;
        }
        m = &s->udp_meta[s->udp_meta_head];
        m->ip   = src_ip;
        m->port = src_port;
        m->len  = (uint16_t)dlen;
        s->udp_meta_head = next_meta;
        return;
    }
}

/* TCP wrappers — wired in T13/T14. Caller passes ports in network byte
 * order; tcp_* functions store in host order so we translate at the
 * boundary (BSD convention). */
int socket_listen (int fd, int backlog)                            { return tcp_listen(fd, backlog); }
int socket_accept (int fd, uint32_t *peer_ip, uint16_t *peer_port) {
    int r = tcp_accept(fd, peer_ip, peer_port);
    if (r >= 0 && peer_port) *peer_port = htons(*peer_port);
    return r;
}
int socket_connect(int fd, uint32_t ip, uint16_t port)             { return tcp_connect(fd, ip, ntohs(port)); }

/* TLS-aware send/recv: when the socket has a tls_ctx attached, route
 * application bytes through the TLS record layer; otherwise plain TCP. */
int socket_send(int fd, const void *buf, uint32_t len) {
    socket_t *s;
    if (fd < 0 || fd >= SOCKET_MAX) return EBADF;
    s = &sockets[fd];
    if (!s->in_use) return EBADF;
    if (s->tls_ctx != NULL) {
        int rc = tls_app_send((tls_ctx_t *)s->tls_ctx,
                              (const uint8_t *)buf, len);
        if (rc != TLS_ERR_OK) return ECONNRESET;
        return (int)len;
    }
    return tcp_send(fd, (const uint8_t *)buf, len);
}

int socket_recv(int fd, void *buf, uint32_t len) {
    socket_t *s;
    if (fd < 0 || fd >= SOCKET_MAX) return EBADF;
    s = &sockets[fd];
    if (!s->in_use) return EBADF;
    if (s->tls_ctx != NULL) {
        return tls_app_recv((tls_ctx_t *)s->tls_ctx,
                            (uint8_t *)buf, len);
    }
    return tcp_recv(fd, (uint8_t *)buf, len);
}

/* --- TLS setsockopt ----------------------------------------------- */

/* Transport callbacks for the TLS record layer. user is a socket_t*; we
 * derive the fd from its index in `sockets`. */
static int sock_tls_xp_send(void *user, const uint8_t *buf, uint32_t len) {
    socket_t *s = (socket_t *)user;
    int fd = (int)(s - sockets);
    return tcp_send(fd, buf, len);
}
static int sock_tls_xp_recv(void *user, uint8_t *buf, uint32_t len) {
    socket_t *s = (socket_t *)user;
    int fd = (int)(s - sockets);
    return tcp_recv(fd, buf, len);
}

int socket_setsockopt(int fd, int level, int optname,
                      const void *val, uint32_t vlen) {
    socket_t *s;
    char hostname[256];
    tls_ctx_t *tls;
    int rc;
    uint32_t i;
    uint64_t now;

    if (fd < 0 || fd >= SOCKET_MAX) return EBADF;
    s = &sockets[fd];
    if (!s->in_use || s->type != SOCK_TYPE_TCP) return EBADF;

    if (level != SOL_TLS || optname != TLS_ENABLE) return EINVAL_SOCK;
    if (s->tls_ctx != NULL) return EINVAL_SOCK;
    if (val == NULL || vlen == 0u || vlen >= sizeof(hostname))
        return EINVAL_SOCK;

    /* Copy hostname; strip a trailing NUL if the caller included one. */
    for (i = 0; i < vlen; i++) hostname[i] = ((const char *)val)[i];
    if (hostname[i - 1u] == '\0') i--;
    hostname[i] = '\0';
    if (i == 0u) return EINVAL_SOCK;

    tls = (tls_ctx_t *)kmalloc(sizeof(tls_ctx_t));
    if (tls == NULL) return ENOBUFS_SOCK;
    now = (uint64_t)rtc_get_epoch_seconds();
    rc = tls_ctx_init(tls, s, sock_tls_xp_send, sock_tls_xp_recv,
                      hostname, now);
    if (rc != TLS_ERR_OK) {
        tls_ctx_destroy(tls);
        kfree(tls);
        return EINVAL_SOCK;
    }
    serial_printf("[tls] handshake start: host=%s\n", hostname);
    rc = tls_handshake_client(tls);
    if (rc != TLS_ERR_OK) {
        serial_printf("[tls] handshake failed: %d\n", rc);
        tls_ctx_destroy(tls);
        kfree(tls);
        return rc;
    }
    serial_printf("[tls] handshake ok\n");
    s->tls_ctx = tls;
    return 0;
}
