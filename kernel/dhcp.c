#include "dhcp.h"
#include "net_if.h"
#include "ip.h"
#include "../drivers/serial.h"
#include "../drivers/timer.h"

/* BOOTP/DHCP frame */
typedef struct __attribute__((packed)) {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic;
    uint8_t  options[312];
} bootp_t;

/* 0x63538263 — the four magic-cookie bytes 0x63,0x82,0x53,0x63 in
 * little-endian memory order as a uint32_t. */
#define DHCP_MAGIC       0x63538263u

#define DHCP_OPT_SUBNET       1
#define DHCP_OPT_ROUTER       3
#define DHCP_OPT_DNS          6
#define DHCP_OPT_REQUESTED_IP 50
#define DHCP_OPT_LEASE        51
#define DHCP_OPT_MSG_TYPE     53
#define DHCP_OPT_SERVER_ID    54
#define DHCP_OPT_PARAM_REQ    55
#define DHCP_OPT_END          255

#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_ACK      5

static uint16_t dhcp_be16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }

/* DHCP RX intercept state — populated by dhcp_rx_intercept. */
static volatile bool     dhcp_got_reply   = false;
static bootp_t           dhcp_reply_buf;
static volatile uint32_t dhcp_reply_len   = 0;
static uint32_t          dhcp_current_xid = 0;

bool dhcp_rx_intercept(uint32_t src_ip, const uint8_t *data, uint32_t dlen) {
    (void)src_ip;
    if (dhcp_current_xid == 0u) return false;  /* not actively waiting */
    /* Minimum: fixed fields up to and including magic (236 bytes) + 4 */
    if (dlen < 240u) return false;
    const bootp_t *b = (const bootp_t *)data;
    if (b->op != 2u) return false;              /* reply only */
    if (b->magic != DHCP_MAGIC) return false;
    if (b->xid != dhcp_current_xid) return false;
    /* Copy into reply buffer */
    uint32_t cp = dlen > sizeof(dhcp_reply_buf) ? sizeof(dhcp_reply_buf) : dlen;
    uint32_t i;
    for (i = 0u; i < cp; i++) ((uint8_t *)&dhcp_reply_buf)[i] = data[i];
    dhcp_reply_len  = cp;
    dhcp_got_reply  = true;
    return true;
}

static uint16_t ip_csum_helper(const uint8_t *data, uint32_t len) {
    return ip_checksum(data, len);
}

/* Build + send a DHCP packet over raw Ethernet/IPv4/UDP.
 * We don't have an IP address yet so we can't use ipv4_send. */
static void send_dhcp_raw(net_if_t *nif, const bootp_t *b) {
    /* Ethernet(14) + IPv4(20) + UDP(8) + BOOTP */
    uint8_t pkt[14u + 20u + 8u + sizeof(bootp_t)];
    uint32_t i;

    /* --- Ethernet header --- */
    for (i = 0u; i < 6u; i++) pkt[i] = 0xFFu;           /* dst: broadcast */
    for (i = 0u; i < 6u; i++) pkt[6u + i] = nif->mac[i]; /* src: our MAC */
    pkt[12] = 0x08u; pkt[13] = 0x00u;                    /* EtherType IPv4 */

    /* --- IPv4 header --- */
    uint8_t *ip = pkt + 14u;
    ip[0]  = 0x45u; ip[1] = 0u;
    uint16_t tl = (uint16_t)(20u + 8u + sizeof(bootp_t));
    ip[2]  = (uint8_t)(tl >> 8); ip[3] = (uint8_t)(tl & 0xFFu);
    ip[4]  = 0u;    ip[5]  = 0u;    /* ID */
    ip[6]  = 0u;    ip[7]  = 0u;    /* flags/frag */
    ip[8]  = 64u;   ip[9]  = 17u;   /* TTL=64, proto=UDP */
    ip[10] = 0u;    ip[11] = 0u;    /* checksum placeholder */
    for (i = 0u; i < 4u; i++) ip[12u + i] = 0u;      /* src 0.0.0.0 */
    for (i = 0u; i < 4u; i++) ip[16u + i] = 0xFFu;   /* dst 255.255.255.255 */
    uint16_t c = ip_csum_helper(ip, 20u);
    ip[10] = (uint8_t)(c >> 8); ip[11] = (uint8_t)(c & 0xFFu);

    /* --- UDP header --- */
    uint8_t *u = pkt + 14u + 20u;
    u[0] = 0u;   u[1] = 68u;  /* src port 68 */
    u[2] = 0u;   u[3] = 67u;  /* dst port 67 */
    uint16_t ulen = (uint16_t)(8u + sizeof(bootp_t));
    u[4] = (uint8_t)(ulen >> 8); u[5] = (uint8_t)(ulen & 0xFFu);
    u[6] = 0u;   u[7] = 0u;   /* checksum 0 = omitted */

    /* --- BOOTP payload --- */
    const uint8_t *bsrc = (const uint8_t *)b;
    uint8_t *bdst = pkt + 14u + 20u + 8u;
    for (i = 0u; i < sizeof(bootp_t); i++) bdst[i] = bsrc[i];

    nif->send(nif, pkt, (uint32_t)sizeof(pkt));
}

static void build_bootp(bootp_t *b, net_if_t *nif, uint32_t xid,
                        uint8_t msg_type, uint32_t requested_ip,
                        uint32_t server_id) {
    uint32_t i;
    for (i = 0u; i < sizeof(*b); i++) ((uint8_t *)b)[i] = 0u;

    b->op    = 1u;
    b->htype = 1u;
    b->hlen  = 6u;
    b->xid   = xid;
    b->flags = dhcp_be16(0x8000u);  /* request broadcast reply */
    for (i = 0u; i < 6u; i++) b->chaddr[i] = nif->mac[i];
    b->magic = DHCP_MAGIC;

    int o = 0;
    b->options[o++] = DHCP_OPT_MSG_TYPE;
    b->options[o++] = 1u;
    b->options[o++] = msg_type;

    if (requested_ip != 0u) {
        b->options[o++] = DHCP_OPT_REQUESTED_IP;
        b->options[o++] = 4u;
        const uint8_t *rp = (const uint8_t *)&requested_ip;
        b->options[o++] = rp[0]; b->options[o++] = rp[1];
        b->options[o++] = rp[2]; b->options[o++] = rp[3];
    }
    if (server_id != 0u) {
        b->options[o++] = DHCP_OPT_SERVER_ID;
        b->options[o++] = 4u;
        const uint8_t *sp = (const uint8_t *)&server_id;
        b->options[o++] = sp[0]; b->options[o++] = sp[1];
        b->options[o++] = sp[2]; b->options[o++] = sp[3];
    }
    b->options[o++] = DHCP_OPT_PARAM_REQ;
    b->options[o++] = 3u;
    b->options[o++] = DHCP_OPT_SUBNET;
    b->options[o++] = DHCP_OPT_ROUTER;
    b->options[o++] = DHCP_OPT_DNS;
    b->options[o++] = DHCP_OPT_END;
}

static bool parse_reply(uint8_t *msg_type_out, uint32_t *server_id_out,
                        uint32_t *mask_out, uint32_t *gw_out,
                        uint32_t *dns_out) {
    if (!dhcp_got_reply) return false;
    *msg_type_out  = 0u;
    *server_id_out = 0u;
    *mask_out      = 0u;
    *gw_out        = 0u;
    *dns_out       = 0u;

    int o = 0;
    while (o < 312) {
        uint8_t op = dhcp_reply_buf.options[o++];
        if (op == 0u) continue;
        if (op == 255u) break;
        if (o >= 312) break;
        uint8_t ln = dhcp_reply_buf.options[o++];
        if (o + (int)ln > 312) break;

        if (op == DHCP_OPT_MSG_TYPE && ln >= 1u) {
            *msg_type_out = dhcp_reply_buf.options[o];
        } else if (op == DHCP_OPT_SERVER_ID && ln >= 4u) {
            uint8_t *p = (uint8_t *)server_id_out;
            p[0] = dhcp_reply_buf.options[o + 0];
            p[1] = dhcp_reply_buf.options[o + 1];
            p[2] = dhcp_reply_buf.options[o + 2];
            p[3] = dhcp_reply_buf.options[o + 3];
        } else if (op == DHCP_OPT_SUBNET && ln >= 4u) {
            uint8_t *p = (uint8_t *)mask_out;
            p[0] = dhcp_reply_buf.options[o + 0];
            p[1] = dhcp_reply_buf.options[o + 1];
            p[2] = dhcp_reply_buf.options[o + 2];
            p[3] = dhcp_reply_buf.options[o + 3];
        } else if (op == DHCP_OPT_ROUTER && ln >= 4u) {
            uint8_t *p = (uint8_t *)gw_out;
            p[0] = dhcp_reply_buf.options[o + 0];
            p[1] = dhcp_reply_buf.options[o + 1];
            p[2] = dhcp_reply_buf.options[o + 2];
            p[3] = dhcp_reply_buf.options[o + 3];
        } else if (op == DHCP_OPT_DNS && ln >= 4u) {
            uint8_t *p = (uint8_t *)dns_out;
            p[0] = dhcp_reply_buf.options[o + 0];
            p[1] = dhcp_reply_buf.options[o + 1];
            p[2] = dhcp_reply_buf.options[o + 2];
            p[3] = dhcp_reply_buf.options[o + 3];
        }
        o += (int)ln;
    }
    return true;
}

/* Forward declaration: poll RTL8139 RX before IRQs are enabled. */
extern void rtl8139_poll_rx(void);

static bool wait_reply(uint32_t timeout_ms) {
    /* timer_get_uptime_ms() returns 0 before IRQs are enabled (tick counter
     * never increments).  Use timer_delay_us (TSC-based, IRQ-free) to spin
     * in 1 ms slices and count down manually. */
    uint32_t remaining = timeout_ms;
    while (remaining > 0u) {
        rtl8139_poll_rx();          /* drain HW ring while IRQs may be off */
        net_process_pending();
        if (dhcp_got_reply) return true;
        timer_delay_us(1000u);      /* 1 ms TSC busy-wait */
        remaining--;
    }
    return false;
}

bool dhcp_start(net_if_t *nif) {
    /* XID: simple deterministic seed using uptime. */
    uint32_t xid = timer_get_uptime_ms() * 31u + 0xCABBA6Eu;
    dhcp_current_xid = xid;

    int attempt;
    for (attempt = 0; attempt < 3; attempt++) {
        /* --- DISCOVER --- */
        bootp_t disc;
        build_bootp(&disc, nif, xid, (uint8_t)DHCP_DISCOVER, 0u, 0u);
        dhcp_got_reply = false;
        send_dhcp_raw(nif, &disc);

        if (!wait_reply(1000u)) continue;

        uint8_t  msg_type  = 0u;
        uint32_t server_id = 0u;
        uint32_t mask      = 0u;
        uint32_t gw        = 0u;
        uint32_t dns       = 0u;
        if (!parse_reply(&msg_type, &server_id, &mask, &gw, &dns)) continue;
        if (msg_type != (uint8_t)DHCP_OFFER) continue;

        uint32_t offered_ip = dhcp_reply_buf.yiaddr;

        /* --- REQUEST --- */
        bootp_t req;
        build_bootp(&req, nif, xid, (uint8_t)DHCP_REQUEST, offered_ip, server_id);
        dhcp_got_reply = false;
        send_dhcp_raw(nif, &req);

        if (!wait_reply(1000u)) continue;
        if (!parse_reply(&msg_type, &server_id, &mask, &gw, &dns)) continue;
        if (msg_type != (uint8_t)DHCP_ACK) continue;

        nif->ipv4_addr    = dhcp_reply_buf.yiaddr;
        nif->ipv4_mask    = mask;
        nif->ipv4_gateway = gw;
        nif->ipv4_dns     = dns;
        dhcp_current_xid  = 0u;
        KINFO("dhcp: bound ip=%x mask=%x gw=%x dns=%x",
              nif->ipv4_addr, nif->ipv4_mask, nif->ipv4_gateway, nif->ipv4_dns);
        return true;
    }

    /* Static fallback: 10.0.2.15/24 gw 10.0.2.2 dns 10.0.2.3 (QEMU user-net). */
    KWARN("dhcp: fallback to static 10.0.2.15/24 gw 10.0.2.2 dns 10.0.2.3");
    nif->ipv4_addr    = 0x0F02000Au;
    nif->ipv4_mask    = 0x00FFFFFFu;
    nif->ipv4_gateway = 0x0202000Au;
    nif->ipv4_dns     = 0x0302000Au;
    dhcp_current_xid  = 0u;
    return false;
}
