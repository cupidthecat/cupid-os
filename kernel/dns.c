#include "dns.h"
#include "socket.h"
#include "net_if.h"
#include "bkl.h"
#include "../drivers/timer.h"
#include "../drivers/serial.h"

extern void net_process_pending(void);

/* Non-blocking UDP recv: returns bytes or -1 if queue empty. */
static int udp_try_recv(int fd, uint8_t *buf, uint32_t len,
                        uint32_t *ip, uint16_t *port) {
    int r = -1;
    bkl_lock();
    {
        socket_t *s = &sockets[fd];
        if (s->in_use && s->type == SOCK_TYPE_UDP &&
            s->udp_meta_head != s->udp_meta_tail) {
            udp_dgram_meta_t m = s->udp_meta[s->udp_meta_tail];
            uint32_t dlen = m.len, i;
            if (dlen > len) dlen = len;
            for (i = 0; i < dlen; i++) {
                buf[i] = s->rx_buf[(s->rx_head + i) % SOCK_RX_BUF];
            }
            s->rx_head = (s->rx_head + m.len) % SOCK_RX_BUF;
            s->udp_meta_tail = (uint8_t)((s->udp_meta_tail + 1u) % UDP_MAX_QUEUED);
            if (ip)   *ip   = m.ip;
            if (port) *port = m.port;
            r = (int)dlen;
        }
    }
    bkl_unlock();
    return r;
}

#define DNS_CACHE_SIZE 16

typedef struct {
    char     name[64];
    uint32_t ip;
    uint32_t expiry_ms;
    bool     valid;
} dns_entry_t;

static dns_entry_t cache[DNS_CACHE_SIZE];

static bool name_eq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return false;
        a++; b++;
    }
    return *a == *b;
}

static void cache_put(const char *name, uint32_t ip, uint32_t ttl_s) {
    dns_entry_t *slot = &cache[0];
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (!cache[i].valid) { slot = &cache[i]; break; }
        if (cache[i].expiry_ms < slot->expiry_ms) slot = &cache[i];
    }
    int j = 0;
    while (name[j] && j < 63) { slot->name[j] = name[j]; j++; }
    slot->name[j] = 0;
    slot->ip = ip;
    slot->expiry_ms = timer_get_uptime_ms() + (ttl_s < 300u ? ttl_s : 300u) * 1000u;
    slot->valid = true;
}

static bool cache_lookup(const char *name, uint32_t *ip_out) {
    uint32_t now = timer_get_uptime_ms();
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (!cache[i].valid) continue;
        if (cache[i].expiry_ms < now) { cache[i].valid = false; continue; }
        if (name_eq(cache[i].name, name)) { *ip_out = cache[i].ip; return true; }
    }
    return false;
}

/* Encode "foo.com" -> "\x03foo\x03com\x00" */
static int encode_name(const char *name, uint8_t *out, int max) {
    int o = 0;
    const char *p = name;
    while (*p) {
        const char *q = p;
        while (*q && *q != '.') q++;
        int label_len = (int)(q - p);
        if (label_len == 0 || label_len > 63) return -1;
        if (o + 1 + label_len >= max) return -1;
        out[o++] = (uint8_t)label_len;
        for (int i = 0; i < label_len; i++) out[o++] = (uint8_t)p[i];
        p = q;
        if (*p == '.') p++;
    }
    if (o >= max) return -1;
    out[o++] = 0;
    return o;
}

/* Skip a DNS name in a response (handles compression pointers).
 * Returns byte offset just after the name, or -1 on malformed input.
 * Compression-pointer case needs 2 bytes available (tag + low byte). */
static int skip_name(const uint8_t *buf, int pos, int total) {
    while (pos < total) {
        uint8_t b = buf[pos];
        if (b == 0) return pos + 1;
        if ((b & 0xC0u) == 0xC0u) {
            if (pos + 2 > total) return -1;
            return pos + 2;
        }
        /* Label must fit entirely in buffer. */
        if (pos + 1 + (int)b > total) return -1;
        pos += 1 + (int)b;
    }
    return -1;
}

int dns_resolve(const char *name, uint32_t *ipv4_out) {
    if (!name || !ipv4_out) return -1;

    if (cache_lookup(name, ipv4_out)) return 0;

    net_if_t *nif = net_if_primary();
    if (!nif || nif->ipv4_dns == 0u) return -1;

    int fd = socket_create(SOCK_TYPE_UDP);
    if (fd < 0) return -1;
    socket_bind(fd, 0u, 0u);   /* ephemeral */

    uint8_t query[512];
    static uint16_t id_counter = 1;
    uint16_t qid = id_counter++;

    query[0] = (uint8_t)(qid >> 8);
    query[1] = (uint8_t)(qid & 0xFFu);
    query[2] = 0x01u; query[3] = 0x00u;   /* flags: RD */
    query[4] = 0x00u; query[5] = 0x01u;   /* QDCOUNT = 1 */
    query[6] = 0x00u; query[7] = 0x00u;
    query[8] = 0x00u; query[9] = 0x00u;
    query[10] = 0x00u; query[11] = 0x00u;
    int n = encode_name(name, query + 12, 512 - 12 - 4);
    if (n < 0) { socket_close(fd); return -1; }
    int o = 12 + n;
    query[o++] = 0x00u; query[o++] = 0x01u;   /* QTYPE = A */
    query[o++] = 0x00u; query[o++] = 0x01u;   /* QCLASS = IN */

    if (socket_sendto(fd, query, (uint32_t)o, nif->ipv4_dns, 53u) < 0) {
        socket_close(fd);
        return -1;
    }

    uint8_t resp[512];
    uint32_t rip = 0; uint16_t rport = 0;
    int rn = -1;
    {
        uint32_t polls;
        net_if_t *pn = net_if_primary();
        /* Poll HW RX directly + SW ring so we work even with IRQs masked. */
        for (polls = 0; polls < 3000u; polls++) {
            if (pn && pn->poll_rx) pn->poll_rx(pn);
            net_process_pending();
            rn = udp_try_recv(fd, resp, (uint32_t)sizeof(resp), &rip, &rport);
            if (rn >= 0) break;
            timer_delay_us(1000u);
        }
    }
    socket_close(fd);
    if (rn < 12) return -1;

    uint16_t resp_id = (uint16_t)(((uint16_t)resp[0] << 8) | resp[1]);
    if (resp_id != qid) return -1;
    uint16_t ancount = (uint16_t)(((uint16_t)resp[6] << 8) | resp[7]);
    if (ancount == 0u) return -1;

    int pos = 12;
    pos = skip_name(resp, pos, rn);
    if (pos < 0 || pos + 4 > rn) return -1;
    pos += 4;

    for (int a = 0; a < ancount; a++) {
        pos = skip_name(resp, pos, rn);
        if (pos < 0 || pos + 10 > rn) return -1;
        uint16_t atype  = (uint16_t)(((uint16_t)resp[pos] << 8) | resp[pos + 1]);
        uint32_t ttl    = ((uint32_t)resp[pos + 4] << 24) | ((uint32_t)resp[pos + 5] << 16)
                        | ((uint32_t)resp[pos + 6] << 8)  | (uint32_t)resp[pos + 7];
        uint16_t rdlen  = (uint16_t)(((uint16_t)resp[pos + 8] << 8) | resp[pos + 9]);
        pos += 10;
        if (pos + rdlen > rn) return -1;
        if (atype == 1u && rdlen == 4u) {
            uint32_t ip = 0;
            for (int i = 0; i < 4; i++) ((uint8_t*)&ip)[i] = resp[pos + i];
            *ipv4_out = ip;
            cache_put(name, ip, ttl);
            return 0;
        }
        pos += rdlen;
    }
    return -1;
}
