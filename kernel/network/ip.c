#include "ip.h"
#include "tcp.h"
#include "net_if.h"
#include "arp.h"
#include "icmp.h"
#include "udp.h"
#include "serial.h"
#include "timer.h"

typedef struct __attribute__((packed)) {
    uint8_t  vhl;          /* 0x45 = version 4, IHL 5 */
    uint8_t  tos;
    uint16_t total_len;    /* big-endian on wire */
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  proto;
    uint16_t hdr_csum;
    uint32_t src_ip;       /* first-octet-in-low-byte (network byte order) */
    uint32_t dst_ip;
} ipv4_hdr_t;

static uint16_t ip_id_counter = 0;

uint16_t ip_checksum(const uint8_t *data, uint32_t len) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i + 1u < len; i += 2u) {
        sum += ((uint32_t)data[i] << 8) | data[i + 1u];
    }
    if (len & 1u) sum += ((uint32_t)data[len - 1u] << 8);
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)(~sum & 0xFFFFu);
}

static uint16_t be16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }

/* Reassembly */
#define IP_REASM_SLOTS      4
#define IP_REASM_TIMEOUT_MS 30000u
#define IP_REASM_MAX_BYTES  65535u
#define IP_REASM_BITMAP_SZ  ((IP_REASM_MAX_BYTES / 8u + 7u) / 8u)  /* 1 bit per 8-byte unit */

typedef struct {
    bool     in_use;
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t id;
    uint8_t  proto;
    uint32_t inserted_ms;
    uint32_t total_len;        /* set when MF=0 fragment arrives, else 0 */
    uint8_t  bitmap[IP_REASM_BITMAP_SZ];
    uint8_t  buf[IP_REASM_MAX_BYTES];
} ip_reasm_t;

static ip_reasm_t reasm[IP_REASM_SLOTS];

static void reasm_clear(ip_reasm_t *r) {
    uint32_t k;
    r->in_use      = false;
    r->total_len   = 0u;
    for (k = 0; k < IP_REASM_BITMAP_SZ; k++) r->bitmap[k] = 0u;
}

static void reasm_gc(uint32_t now) {
    int i;
    for (i = 0; i < IP_REASM_SLOTS; i++) {
        if (reasm[i].in_use && (now - reasm[i].inserted_ms) > IP_REASM_TIMEOUT_MS) {
            reasm_clear(&reasm[i]);
        }
    }
}

static ip_reasm_t *reasm_find(uint32_t src, uint32_t dst, uint16_t id, uint8_t proto) {
    int i;
    for (i = 0; i < IP_REASM_SLOTS; i++) {
        if (reasm[i].in_use && reasm[i].src_ip == src && reasm[i].dst_ip == dst
            && reasm[i].id == id && reasm[i].proto == proto)
            return &reasm[i];
    }
    return 0;
}

static ip_reasm_t *reasm_alloc(uint32_t src, uint32_t dst, uint16_t id, uint8_t proto, uint32_t now) {
    int i, oldest = 0;
    for (i = 0; i < IP_REASM_SLOTS; i++) {
        if (!reasm[i].in_use) {
            reasm_clear(&reasm[i]);
            reasm[i].in_use      = true;
            reasm[i].src_ip      = src;
            reasm[i].dst_ip      = dst;
            reasm[i].id          = id;
            reasm[i].proto       = proto;
            reasm[i].inserted_ms = now;
            return &reasm[i];
        }
        if (reasm[i].inserted_ms < reasm[oldest].inserted_ms) oldest = i;
    }
    /* All slots used: evict oldest. */
    reasm_clear(&reasm[oldest]);
    reasm[oldest].in_use      = true;
    reasm[oldest].src_ip      = src;
    reasm[oldest].dst_ip      = dst;
    reasm[oldest].id          = id;
    reasm[oldest].proto       = proto;
    reasm[oldest].inserted_ms = now;
    return &reasm[oldest];
}

/* Mark units [unit_start, unit_start+unit_count) in bitmap. Returns count
 * of units that were not previously set (so caller can detect dup). */
static uint32_t reasm_mark(ip_reasm_t *r, uint32_t unit_start, uint32_t unit_count) {
    uint32_t newly = 0u;
    uint32_t u;
    for (u = unit_start; u < unit_start + unit_count; u++) {
        uint32_t byte = u / 8u;
        uint8_t  bit  = (uint8_t)(1u << (u & 7u));
        if (byte >= IP_REASM_BITMAP_SZ) break;
        if ((r->bitmap[byte] & bit) == 0u) {
            r->bitmap[byte] |= bit;
            newly++;
        }
    }
    return newly;
}

static bool reasm_complete(const ip_reasm_t *r) {
    uint32_t total_units;
    uint32_t u;
    if (r->total_len == 0u) return false;
    total_units = (r->total_len + 7u) / 8u;
    for (u = 0; u < total_units; u++) {
        uint32_t byte = u / 8u;
        uint8_t  bit  = (uint8_t)(1u << (u & 7u));
        if ((r->bitmap[byte] & bit) == 0u) return false;
    }
    return true;
}

/* Send */
static int ipv4_send_one(uint32_t dst_ip, uint8_t proto, const uint8_t *payload,
                         uint32_t plen, uint16_t id, uint16_t frag_off_units,
                         bool more_frags) {
    net_if_t *nif = net_if_primary();
    uint8_t dst_mac[6];
    uint8_t frame[14u + 20u + NET_IF_MTU];
    ipv4_hdr_t *h;
    uint16_t cs;
    uint16_t flags_frag;
    uint32_t i;
    uint32_t next_hop;

    if (!nif || !nif->link_up) return -1;
    if (nif->ipv4_addr == 0u) return -1;
    if (plen + 20u > NET_IF_MTU) return -1;

    if ((dst_ip & nif->ipv4_mask) == (nif->ipv4_addr & nif->ipv4_mask)) {
        next_hop = dst_ip;
    } else {
        next_hop = nif->ipv4_gateway;
    }

    if (dst_ip == 0xFFFFFFFFu) {
        for (i = 0; i < 6u; i++) dst_mac[i] = 0xFFu;
    } else {
        if (arp_resolve(next_hop, dst_mac) != 0) {
            nif->tx_errors++;
            return -1;
        }
    }

    for (i = 0; i < 6u; i++) frame[i] = dst_mac[i];
    for (i = 0; i < 6u; i++) frame[6u + i] = nif->mac[i];
    frame[12] = 0x08; frame[13] = 0x00;

    h = (ipv4_hdr_t*)(frame + 14);
    h->vhl       = 0x45u;
    h->tos       = 0;
    h->total_len = be16((uint16_t)(20u + plen));
    h->id        = be16(id);
    flags_frag = (uint16_t)(frag_off_units & 0x1FFFu);
    if (more_frags) flags_frag |= 0x2000u;   /* MF */
    h->flags_frag = be16(flags_frag);
    h->ttl       = 64;
    h->proto     = proto;
    h->hdr_csum  = 0;
    h->src_ip    = nif->ipv4_addr;
    h->dst_ip    = dst_ip;

    cs = ip_checksum((uint8_t*)h, 20u);
    h->hdr_csum = be16(cs);

    for (i = 0; i < plen; i++) frame[14u + 20u + i] = payload[i];

    return nif->send(nif, frame, 14u + 20u + plen);
}

int ipv4_send(uint32_t dst_ip, uint8_t proto, const uint8_t *payload, uint32_t plen) {
    uint16_t id;
    uint32_t mtu_payload;
    uint32_t frag_size;
    uint32_t off;

    /* Single-fragment fast path: also retains DF semantics for small TCP
     * (we keep DF off; DF is only useful for PMTU-D which we don't do). */
    if (20u + plen <= NET_IF_MTU) {
        return ipv4_send_one(dst_ip, proto, payload, plen,
                             ip_id_counter++, 0u, false);
    }

    /* Fragment. Each fragment payload must be 8-byte aligned (offset is in
     * 8-byte units), except the last. */
    mtu_payload = NET_IF_MTU - 20u;
    frag_size   = mtu_payload & ~7u;
    id = ip_id_counter++;
    off = 0u;
    while (off < plen) {
        uint32_t this_len = plen - off;
        bool more;
        int r;
        if (this_len > frag_size) this_len = frag_size;
        more = (off + this_len < plen);
        r = ipv4_send_one(dst_ip, proto, payload + off, this_len, id,
                          (uint16_t)(off / 8u), more);
        if (r < 0) return r;
        off += this_len;
    }
    return (int)plen;
}

/* Receive */
void ipv4_input(const uint8_t *frame, uint32_t len) {
    const ipv4_hdr_t *h;
    uint8_t  ihl;
    uint32_t hdr_len;
    uint16_t total;
    uint8_t  tmp[60];
    uint32_t cp;
    uint16_t ff;
    uint16_t frag_off_units;
    bool     mf;
    net_if_t *nif;
    uint32_t src, dst;
    const uint8_t *payload;
    uint32_t plen;
    uint32_t now;
    uint32_t i;

    if (len < 14u + 20u) return;
    h = (const ipv4_hdr_t*)(frame + 14);
    if ((h->vhl & 0xF0u) != 0x40u) return;
    ihl = (uint8_t)(h->vhl & 0x0Fu);
    if (ihl < 5) return;
    hdr_len = (uint32_t)ihl * 4u;
    if (14u + hdr_len > len) return;

    total = be16(h->total_len);
    if ((uint32_t)14u + total > len) return;

    /* Checksum header */
    cp = hdr_len;
    if (cp > sizeof(tmp)) cp = sizeof(tmp);
    for (i = 0; i < cp; i++) tmp[i] = ((const uint8_t*)h)[i];
    if (ip_checksum(tmp, cp) != 0u) return;

    nif = net_if_primary();
    src = h->src_ip;
    dst = h->dst_ip;
    if (nif && dst != nif->ipv4_addr && dst != 0xFFFFFFFFu) return;

    payload = frame + 14u + hdr_len;
    plen    = (uint32_t)total - hdr_len;
    now     = timer_get_uptime_ms();
    reasm_gc(now);

    ff             = be16(h->flags_frag);
    frag_off_units = (uint16_t)(ff & 0x1FFFu);
    mf             = (ff & 0x2000u) != 0u;

    if (frag_off_units == 0u && !mf) {
        /* Unfragmented - fast path. */
        switch (h->proto) {
        case IP_PROTO_ICMP: icmp_input(src, payload, plen); break;
        case IP_PROTO_UDP:  udp_input (src, payload, plen); break;
        case IP_PROTO_TCP:  tcp_input (src, payload, plen); break;
        default: break;
        }
        return;
    }

    /* Fragment. Fragment payload bytes must be 8-byte aligned (last frag
     * may be shorter). */
    {
        uint32_t byte_off = (uint32_t)frag_off_units * 8u;
        uint32_t end      = byte_off + plen;
        uint32_t units;
        ip_reasm_t *r;
        uint16_t id_be = be16(h->id);
        if (end > IP_REASM_MAX_BYTES) return;
        if (mf && (plen & 7u) != 0u) return;   /* non-final frags must be 8-byte multiple */

        r = reasm_find(src, dst, id_be, h->proto);
        if (!r) r = reasm_alloc(src, dst, id_be, h->proto, now);
        if (!r) return;

        for (i = 0; i < plen; i++) r->buf[byte_off + i] = payload[i];

        units = (plen + 7u) / 8u;
        reasm_mark(r, frag_off_units, units);

        if (!mf) r->total_len = end;

        if (reasm_complete(r)) {
            /* Snapshot then clear so the recursive dispatch can't re-enter. */
            uint32_t dlen = r->total_len;
            uint8_t  proto = r->proto;
            uint32_t rsrc  = r->src_ip;
            uint8_t *dbuf  = r->buf;
            r->in_use = false;
            switch (proto) {
            case IP_PROTO_ICMP: icmp_input(rsrc, dbuf, dlen); break;
            case IP_PROTO_UDP:  udp_input (rsrc, dbuf, dlen); break;
            case IP_PROTO_TCP:  tcp_input (rsrc, dbuf, dlen); break;
            default: break;
            }
        }
    }
}

int ip_parse(const char *s, uint32_t *out) {
    uint32_t oct[4] = {0u, 0u, 0u, 0u};
    int idx = 0;
    int have_digit = 0;
    if (!s || !out) return -1;
    while (*s && idx < 4) {
        if (*s >= '0' && *s <= '9') {
            oct[idx] = oct[idx] * 10u + (uint32_t)(*s - '0');
            if (oct[idx] > 255u) return -1;
            have_digit = 1;
        } else if (*s == '.') {
            if (!have_digit) return -1;
            idx++;
            have_digit = 0;
        } else {
            return -1;
        }
        s++;
    }
    if (idx != 3 || !have_digit) return -1;
    *out = oct[0] | (oct[1] << 8) | (oct[2] << 16) | (oct[3] << 24);
    return 0;
}
