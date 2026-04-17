#include "tcp.h"
#include "ip.h"
#include "net_if.h"
#include "socket.h"
#include "bkl.h"
#include "process.h"
#include "cpu.h"
#include "../drivers/timer.h"

/* Pseudo-random 32-bit ISS from TSC. Off-path cannot observe → not spoofable.
 * Mixes low TSC bits, a shifted copy, and a per-slot golden-ratio scramble. */
static uint32_t tcp_gen_iss(uint32_t salt) {
    uint64_t t1 = rdtsc();
    uint64_t t2 = rdtsc();
    uint32_t lo = (uint32_t)t1;
    uint32_t hi = (uint32_t)t2;
    return lo ^ (hi << 13) ^ (salt * 2654435761u);
}

typedef struct __attribute__((packed)) {
    uint16_t src_port, dst_port;
    uint32_t seq, ack;
    uint8_t  data_off;
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} tcp_hdr_t;

#define TCP_FIN 0x01u
#define TCP_SYN 0x02u
#define TCP_RST 0x04u
#define TCP_PSH 0x08u
#define TCP_ACK 0x10u

static uint16_t be16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
static uint32_t be32(uint32_t v) {
    return ((v >> 24) & 0xFFu) | ((v >> 8) & 0xFF00u)
         | ((v << 8)  & 0xFF0000u) | ((v << 24) & 0xFF000000u);
}

/* TCP checksum: pseudo-header (src_ip 4, dst_ip 4, zero 1, proto 1, tcp_len 2)
 * + TCP header + data, ones-complement. */
static uint16_t tcp_csum(uint32_t src_ip, uint32_t dst_ip,
                         const uint8_t *tcp_pkt, uint32_t len) {
    uint32_t sum = 0;
    uint32_t i;
    const uint8_t *s = (const uint8_t*)&src_ip;
    const uint8_t *d = (const uint8_t*)&dst_ip;
    sum += ((uint32_t)s[0] << 8) | s[1];
    sum += ((uint32_t)s[2] << 8) | s[3];
    sum += ((uint32_t)d[0] << 8) | d[1];
    sum += ((uint32_t)d[2] << 8) | d[3];
    sum += 6u;
    sum += len;
    for (i = 0; i + 1u < len; i += 2u)
        sum += ((uint32_t)tcp_pkt[i] << 8) | tcp_pkt[i + 1u];
    if (len & 1u) sum += ((uint32_t)tcp_pkt[len - 1u] << 8);
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)(~sum & 0xFFFFu);
}

/* Build + send a TCP segment. Caller holds BKL or is in net_process_pending. */
static int tcp_send_seg(socket_t *s, uint8_t flags, const uint8_t *data, uint32_t dlen) {
    net_if_t *nif = net_if_primary();
    uint8_t pkt[20u + TCP_MSS];
    tcp_hdr_t *h;
    uint16_t cs;
    uint32_t i;
    if (!nif) return -1;
    if (dlen > TCP_MSS) dlen = TCP_MSS;
    h = (tcp_hdr_t*)pkt;
    h->src_port  = be16(s->local_port);
    h->dst_port  = be16(s->remote_port);
    h->seq       = be32(s->snd_nxt);
    h->ack       = be32(s->rcv_nxt);
    h->data_off  = (uint8_t)(5u << 4);
    h->flags     = flags;
    h->window    = be16(8192u);
    h->checksum  = 0;
    h->urgent    = 0;
    for (i = 0; i < dlen; i++) pkt[20u + i] = data[i];

    cs = tcp_csum(s->local_ip ? s->local_ip : nif->ipv4_addr,
                  s->remote_ip, pkt, 20u + dlen);
    h->checksum = be16(cs);

    if (flags & TCP_SYN) s->snd_nxt++;
    if (flags & TCP_FIN) s->snd_nxt++;
    s->snd_nxt += dlen;

    return ipv4_send(s->remote_ip, IP_PROTO_TCP, pkt, 20u + dlen);
}

int tcp_connect(int fd, uint32_t ip, uint16_t port) {
    uint32_t start;
    bkl_lock();
    {
        socket_t *s = &sockets[fd];
        if (!s->in_use || s->type != SOCK_TYPE_TCP) { bkl_unlock(); return EBADF; }
        if (s->tcp_state != TCPS_CLOSED) { bkl_unlock(); return EINVAL_SOCK; }
        if (s->local_port == 0u) {
            s->local_port = (uint16_t)(49152u + ((uint32_t)fd * 37u) % 16383u);
        }
        s->remote_ip   = ip;
        s->remote_port = port;
        s->snd_iss     = tcp_gen_iss((uint32_t)fd);
        s->snd_una     = s->snd_iss;
        s->snd_nxt     = s->snd_iss;
        s->rcv_nxt     = 0;
        tcp_send_seg(s, TCP_SYN, NULL, 0);
        s->tcp_state        = TCPS_SYN_SENT;
        s->last_rexmit_tick = timer_get_uptime_ms();
    }
    bkl_unlock();

    /* Block until ESTABLISHED / refused / timeout. */
    start = timer_get_uptime_ms();
    while (timer_get_uptime_ms() - start < 30000u) {
        tcp_state_t st;
        uint32_t pi;
        bkl_lock();
        st = sockets[fd].tcp_state;
        bkl_unlock();
        if (st == TCPS_ESTABLISHED) return 0;
        if (st == TCPS_CLOSED)      return ECONNREFUSED;
        for (pi = 0; pi < 1000u; pi++) __asm__ volatile("pause");
        schedule();
    }
    return ETIMEDOUT_SOCK;
}

int tcp_send(int fd, const uint8_t *buf, uint32_t len) {
    uint32_t sent;
    if (fd < 0 || fd >= SOCKET_MAX) return EBADF;
    bkl_lock();
    {
        socket_t *s = &sockets[fd];
        if (!s->in_use || s->type != SOCK_TYPE_TCP || s->tcp_state != TCPS_ESTABLISHED) {
            bkl_unlock(); return EBADF;
        }
        sent = 0;
        while (sent < len) {
            uint32_t chunk = len - sent;
            if (chunk > TCP_MSS) chunk = TCP_MSS;
            tcp_send_seg(s, (uint8_t)(TCP_ACK | TCP_PSH), buf + sent, chunk);
            sent += chunk;
        }
    }
    bkl_unlock();
    return (int)sent;
}

int tcp_recv(int fd, uint8_t *buf, uint32_t len) {
    uint32_t start;
    if (fd < 0 || fd >= SOCKET_MAX) return EBADF;
    start = timer_get_uptime_ms();
    for (;;) {
        uint32_t used;
        uint32_t pi;
        bkl_lock();
        {
            socket_t *s = &sockets[fd];
            if (!s->in_use || s->type != SOCK_TYPE_TCP) { bkl_unlock(); return EBADF; }
            if (s->rx_tail >= s->rx_head) used = s->rx_tail - s->rx_head;
            else used = SOCK_RX_BUF - s->rx_head + s->rx_tail;
            if (used > 0u) {
                uint32_t n = (used < len) ? used : len;
                uint32_t i;
                for (i = 0; i < n; i++) {
                    buf[i] = s->rx_buf[s->rx_head];
                    s->rx_head = (s->rx_head + 1u) % SOCK_RX_BUF;
                }
                bkl_unlock();
                return (int)n;
            }
            if (s->tcp_state == TCPS_CLOSE_WAIT || s->tcp_state == TCPS_CLOSED) {
                bkl_unlock(); return 0;
            }
        }
        bkl_unlock();
        if (timer_get_uptime_ms() - start > 30000u) return ETIMEDOUT_SOCK;
        for (pi = 0; pi < 1000u; pi++) __asm__ volatile("pause");
        schedule();
    }
}

int tcp_close(int fd) {
    if (fd < 0 || fd >= SOCKET_MAX) return EBADF;
    bkl_lock();
    {
        socket_t *s = &sockets[fd];
        if (s->in_use && s->type == SOCK_TYPE_TCP) {
            if (s->tcp_state == TCPS_ESTABLISHED) {
                tcp_send_seg(s, (uint8_t)(TCP_FIN | TCP_ACK), NULL, 0);
                s->tcp_state = TCPS_FIN_WAIT_1;
            } else if (s->tcp_state == TCPS_CLOSE_WAIT) {
                tcp_send_seg(s, (uint8_t)(TCP_FIN | TCP_ACK), NULL, 0);
                s->tcp_state = TCPS_LAST_ACK;
            } else {
                s->in_use = 0;
            }
        }
    }
    bkl_unlock();
    return 0;
}

int tcp_listen(int fd, int backlog) {
    int r;
    (void)backlog;
    bkl_lock();
    {
        socket_t *s = &sockets[fd];
        if (!s->in_use || s->type != SOCK_TYPE_TCP) r = EBADF;
        else if (s->local_port == 0) r = EINVAL_SOCK;
        else {
            int i;
            s->tcp_state = TCPS_LISTEN;
            for (i = 0; i < LQ_SIZE; i++) {
                s->lq[i].in_use    = 0;
                s->lq[i].completed = 0;
            }
            r = 0;
        }
    }
    bkl_unlock();
    return r;
}

int tcp_accept(int fd, uint32_t *peer_ip, uint16_t *peer_port) {
    uint32_t start;
    start = timer_get_uptime_ms();
    for (;;) {
        int found;
        int j;
        bkl_lock();
        {
            socket_t *l = &sockets[fd];
            if (!l->in_use || l->type != SOCK_TYPE_TCP || l->tcp_state != TCPS_LISTEN) {
                bkl_unlock(); return EBADF;
            }
            /* Any-slot dequeue: take the first completed half-open entry.
             * Out-of-order 3rd-ACKs no longer block earlier incomplete ones. */
            found = -1;
            for (j = 0; j < LQ_SIZE; j++) {
                if (l->lq[j].in_use && l->lq[j].completed) { found = j; break; }
            }
            if (found >= 0) {
                int newfd;
                int ii;
                newfd = -1;
                for (ii = 0; ii < SOCKET_MAX; ii++) {
                    if (!sockets[ii].in_use) {
                        socket_t *ns = &sockets[ii];
                        uint32_t k;
                        for (k = 0; k < (uint32_t)sizeof(*ns); k++) ((uint8_t*)ns)[k] = 0;
                        ns->in_use      = 1;
                        ns->type        = SOCK_TYPE_TCP;
                        ns->local_ip    = l->local_ip;
                        ns->local_port  = l->local_port;
                        ns->remote_ip   = l->lq[found].ip;
                        ns->remote_port = l->lq[found].port;
                        ns->snd_iss     = l->lq[found].iss;
                        ns->snd_una     = l->lq[found].iss + 1u;
                        ns->snd_nxt     = l->lq[found].iss + 1u;
                        ns->rcv_irs     = l->lq[found].rcv_nxt - 1u;
                        ns->rcv_nxt     = l->lq[found].rcv_nxt;
                        ns->tcp_state   = TCPS_ESTABLISHED;
                        newfd = ii;
                        break;
                    }
                }
                if (peer_ip)   *peer_ip   = l->lq[found].ip;
                if (peer_port) *peer_port = l->lq[found].port;
                l->lq[found].completed = 0;
                l->lq[found].in_use    = 0;
                bkl_unlock();
                if (newfd < 0) return ENOBUFS_SOCK;
                return newfd;
            }
        }
        bkl_unlock();
        if (timer_get_uptime_ms() - start > 30000u) return ETIMEDOUT_SOCK;
        {
            int pi;
            for (pi = 0; pi < 1000; pi++) __asm__ volatile("pause");
        }
        schedule();
    }
}

void tcp_input(uint32_t src_ip, const uint8_t *buf, uint32_t len) {
    uint16_t src_port, dst_port;
    uint32_t seq, ack;
    uint8_t  flags;
    uint8_t  doff;
    uint32_t hlen;
    int      i;
    socket_t *s;
    const tcp_hdr_t *h;

    if (len < 20u) return;
    h        = (const tcp_hdr_t*)buf;
    src_port = be16(h->src_port);
    dst_port = be16(h->dst_port);
    seq      = be32(h->seq);
    ack      = be32(h->ack);
    flags    = h->flags;
    doff     = (uint8_t)(h->data_off >> 4);
    if (doff < 5u) return;
    hlen = (uint32_t)doff * 4u;
    if (hlen > len) return;

    /* Find socket by (local_port, remote_ip, remote_port). Exclude LISTEN
     * (handled in T14). */
    s = NULL;
    for (i = 0; i < SOCKET_MAX; i++) {
        socket_t *c = &sockets[i];
        if (!c->in_use || c->type != SOCK_TYPE_TCP) continue;
        if (c->tcp_state == TCPS_LISTEN || c->tcp_state == TCPS_CLOSED) continue;
        if (c->local_port != dst_port) continue;
        if (c->remote_ip == src_ip && c->remote_port == src_port) { s = c; break; }
    }
    /* If no established-match socket found, check for a LISTEN on dst_port. */
    if (!s && (flags & TCP_SYN)) {
        int li;
        for (li = 0; li < SOCKET_MAX; li++) {
            socket_t *l = &sockets[li];
            int slot;
            int si;
            if (!l->in_use || l->type != SOCK_TYPE_TCP) continue;
            if (l->tcp_state != TCPS_LISTEN) continue;
            if (l->local_port != dst_port) continue;
            /* Dup SYN from same peer: ignore, don't consume another slot. */
            for (si = 0; si < LQ_SIZE; si++) {
                if (l->lq[si].in_use && l->lq[si].ip == src_ip && l->lq[si].port == src_port)
                    return;
            }
            /* Find free slot; drop SYN if listen queue full. */
            slot = -1;
            for (si = 0; si < LQ_SIZE; si++) {
                if (!l->lq[si].in_use) { slot = si; break; }
            }
            if (slot < 0) return;
            l->lq[slot].ip        = src_ip;
            l->lq[slot].port      = src_port;
            l->lq[slot].iss       = tcp_gen_iss((uint32_t)slot);
            l->lq[slot].rcv_nxt   = seq + 1u;
            l->lq[slot].completed = 0;
            l->lq[slot].in_use    = 1;
            {
                socket_t tmp;
                uint32_t k;
                for (k = 0; k < (uint32_t)sizeof(tmp); k++) ((uint8_t*)&tmp)[k] = 0;
                tmp.local_ip    = l->local_ip ? l->local_ip : net_if_primary()->ipv4_addr;
                tmp.local_port  = l->local_port;
                tmp.remote_ip   = src_ip;
                tmp.remote_port = src_port;
                tmp.snd_nxt     = l->lq[slot].iss;
                tmp.rcv_nxt     = seq + 1u;
                tcp_send_seg(&tmp, (uint8_t)(TCP_SYN | TCP_ACK), NULL, 0);
            }
            return;
        }
    }

    /* If ACK on LISTEN-queue half-open, promote to "completed". */
    if (!s && (flags & TCP_ACK) && !(flags & TCP_SYN)) {
        int li;
        for (li = 0; li < SOCKET_MAX; li++) {
            socket_t *l = &sockets[li];
            int j;
            if (!l->in_use || l->type != SOCK_TYPE_TCP) continue;
            if (l->tcp_state != TCPS_LISTEN) continue;
            if (l->local_port != dst_port) continue;
            for (j = 0; j < LQ_SIZE; j++) {
                if (!l->lq[j].in_use || l->lq[j].completed) continue;
                if (l->lq[j].ip == src_ip && l->lq[j].port == src_port
                    && ack == l->lq[j].iss + 1u) {
                    l->lq[j].completed = 1;
                    return;
                }
            }
        }
    }

    if (!s) return;

    if (s->tcp_state == TCPS_SYN_SENT) {
        if ((flags & TCP_SYN) && (flags & TCP_ACK) && ack == s->snd_nxt) {
            s->rcv_irs = seq;
            s->rcv_nxt = seq + 1u;
            s->snd_una = ack;
            s->tcp_state = TCPS_ESTABLISHED;
            tcp_send_seg(s, TCP_ACK, NULL, 0);
            return;
        } else if (flags & TCP_RST) {
            s->tcp_state = TCPS_CLOSED;
            return;
        }
    }

    if (s->tcp_state == TCPS_ESTABLISHED) {
        uint32_t dlen = len - hlen;
        if (seq == s->rcv_nxt && dlen > 0u) {
            uint32_t used, free_bytes;
            /* Capacity check: reserve 1 byte so head==tail always means empty.
             * If no room, don't advance rcv_nxt and don't consume — ACK
             * carrying unchanged rcv_nxt triggers peer retransmit. */
            if (s->rx_tail >= s->rx_head) used = s->rx_tail - s->rx_head;
            else used = SOCK_RX_BUF - s->rx_head + s->rx_tail;
            free_bytes = (used < SOCK_RX_BUF - 1u) ? (SOCK_RX_BUF - 1u - used) : 0u;
            if (dlen <= free_bytes) {
                uint32_t j;
                for (j = 0; j < dlen; j++) {
                    s->rx_buf[s->rx_tail] = buf[hlen + j];
                    s->rx_tail = (s->rx_tail + 1u) % SOCK_RX_BUF;
                }
                s->rcv_nxt += dlen;
            }
            tcp_send_seg(s, TCP_ACK, NULL, 0);
        }
        if (flags & TCP_ACK) s->snd_una = ack;
        if (flags & TCP_FIN) {
            s->rcv_nxt++;
            s->tcp_state = TCPS_CLOSE_WAIT;
            tcp_send_seg(s, TCP_ACK, NULL, 0);
        }
        return;
    }

    if (s->tcp_state == TCPS_FIN_WAIT_1 && (flags & TCP_ACK) && ack == s->snd_nxt) {
        s->tcp_state = TCPS_FIN_WAIT_2;
        /* fall through to check for FIN in same segment */
    }
    if (s->tcp_state == TCPS_FIN_WAIT_2 && (flags & TCP_FIN)) {
        s->rcv_nxt++;
        tcp_send_seg(s, TCP_ACK, NULL, 0);
        s->time_wait_start = timer_get_uptime_ms();
        s->tcp_state = TCPS_TIME_WAIT;
        return;
    }
    if (s->tcp_state == TCPS_LAST_ACK && (flags & TCP_ACK) && ack == s->snd_nxt) {
        s->tcp_state = TCPS_CLOSED;
        s->in_use = 0;
        return;
    }
}

void tcp_tick(void) {
    uint32_t now = timer_get_uptime_ms();
    int i;
    for (i = 0; i < SOCKET_MAX; i++) {
        socket_t *s = &sockets[i];
        if (!s->in_use || s->type != SOCK_TYPE_TCP) continue;
        if (s->tcp_state == TCPS_SYN_SENT &&
            now - s->last_rexmit_tick > TCP_RTO_MS) {
            /* Rewind snd_nxt to snd_iss (undo SYN increment from prior send). */
            s->snd_nxt = s->snd_iss;
            tcp_send_seg(s, TCP_SYN, NULL, 0);
            s->last_rexmit_tick = now;
        }
        if (s->tcp_state == TCPS_TIME_WAIT &&
            now - s->time_wait_start > TCP_TIME_WAIT_MS) {
            s->tcp_state = TCPS_CLOSED;
            s->in_use = 0;
        }
    }
}
