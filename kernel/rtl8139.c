#include "net_if.h"
#include "pci.h"
#include "memory.h"
#include "ports.h"
#include "irq.h"
#include "isr.h"
#include "../drivers/serial.h"

#define RTL_IDR0    0x00
#define RTL_RBSTART 0x30
#define RTL_CMD     0x37
#define RTL_CAPR    0x38
#define RTL_IMR     0x3C
#define RTL_ISR     0x3E
#define RTL_TCR     0x40
#define RTL_RCR     0x44
#define RTL_CONFIG1 0x50
#define RTL_TSD(n)  (0x10 + (n)*4)
#define RTL_TSAD(n) (0x20 + (n)*4)

#define RTL_CMD_RESET 0x10
#define RTL_CMD_RE    0x08
#define RTL_CMD_TE    0x04
#define RTL_CMD_BUFE  0x01

#define RTL_ISR_ROK 0x01
#define RTL_ISR_TOK 0x04

#define RTL_RX_BUF_SIZE (8192 + 16 + 1500)
#define RTL_TX_BUF_SIZE 2048

typedef struct {
    uint16_t io_base;
    uint8_t  irq_line;
    uint8_t *rx_buf;
    void    *rx_buf_raw;
    uint32_t rx_offset;
    uint8_t *tx_buf[4];
    void    *tx_buf_raw[4];
    int      tx_next;
    net_if_t nif;
} rtl_ctrl_t;

static rtl_ctrl_t rtl_ctrl;
static bool rtl_present = false;

bool rtl8139_init(pci_device_t *d);
void rtl8139_probe(void);
void rtl8139_poll_rx(void);
static void rtl_irq(struct registers *r);
static int  rtl_send(net_if_t *nif, const uint8_t *frame, uint32_t len);
static void rtl_poll_rx_nif(net_if_t *nif);

static void rtl_poll_rx_nif(net_if_t *nif) { (void)nif; rtl8139_poll_rx(); }

static void *align16(void *p) {
    uint32_t v = (uint32_t)p;
    return (void*)((v + 15u) & ~15u);
}

bool rtl8139_init(pci_device_t *d) {
    rtl_ctrl_t *c = &rtl_ctrl;

    uint32_t bar0 = d->bars[0];
    if (d->bar_is_mmio[0] || bar0 == 0) {
        KERROR("rtl8139: BAR0 not IO port");
        return false;
    }
    c->io_base = (uint16_t)(bar0 & 0xFFFCu);
    c->irq_line = d->irq_line;

    pci_enable_bus_master(d);

    /* Power on */
    outb((uint16_t)(c->io_base + RTL_CONFIG1), 0);

    /* Software reset */
    outb((uint16_t)(c->io_base + RTL_CMD), RTL_CMD_RESET);
    for (int i = 0; i < 100; i++) {
        if ((inb((uint16_t)(c->io_base + RTL_CMD)) & RTL_CMD_RESET) == 0) break;
        for (volatile int k = 0; k < 100000; k++) { }
    }

    /* Allocate + align RX buffer */
    c->rx_buf_raw = kmalloc(RTL_RX_BUF_SIZE + 16);
    if (!c->rx_buf_raw) { KERROR("rtl8139: rx alloc failed"); return false; }
    c->rx_buf = (uint8_t*)align16(c->rx_buf_raw);
    for (uint32_t i = 0; i < RTL_RX_BUF_SIZE; i++) c->rx_buf[i] = 0;
    c->rx_offset = 0;

    /* Allocate 4 TX buffers, 16-byte aligned */
    for (int i = 0; i < 4; i++) {
        c->tx_buf_raw[i] = kmalloc(RTL_TX_BUF_SIZE + 16);
        if (!c->tx_buf_raw[i]) { KERROR("rtl8139: tx alloc failed"); return false; }
        c->tx_buf[i] = (uint8_t*)align16(c->tx_buf_raw[i]);
    }
    c->tx_next = 0;

    /* Program RX buffer base */
    outl((uint16_t)(c->io_base + RTL_RBSTART), (uint32_t)c->rx_buf);

    /* RCR: AB | AM | APM | AAP | WRAP=0. 0x0F sets low 4 flags. */
    outl((uint16_t)(c->io_base + RTL_RCR), 0x0000000Fu);

    /* TCR: default IFG + MXDMA */
    outl((uint16_t)(c->io_base + RTL_TCR), 0x03000700u);

    /* Enable RX + TX */
    outb((uint16_t)(c->io_base + RTL_CMD), (uint8_t)(RTL_CMD_RE | RTL_CMD_TE));

    /* Interrupt mask: ROK + TOK */
    outw((uint16_t)(c->io_base + RTL_IMR), (uint16_t)(RTL_ISR_ROK | RTL_ISR_TOK));

    /* Read MAC */
    for (int i = 0; i < 6; i++) {
        c->nif.mac[i] = inb((uint16_t)(c->io_base + RTL_IDR0 + i));
    }

    /* Populate net_if */
    c->nif.name = "rtl8139";
    c->nif.driver_data = c;
    c->nif.link_up = true;
    c->nif.send = rtl_send;
    c->nif.poll_rx = rtl_poll_rx_nif;
    c->nif.rx_packets = 0;
    c->nif.tx_packets = 0;
    c->nif.rx_drops = 0;
    c->nif.tx_errors = 0;
    c->nif.ipv4_addr = 0;
    c->nif.ipv4_mask = 0;
    c->nif.ipv4_gateway = 0;
    c->nif.ipv4_dns = 0;

    irq_install_handler(c->irq_line, rtl_irq);
    net_if_register(&c->nif);

    KINFO("rtl8139: init OK io=%x irq=%u", c->io_base, c->irq_line);
    rtl_present = true;
    return true;
}

static void rtl_rx_drain(rtl_ctrl_t *c) {
    while ((inb((uint16_t)(c->io_base + RTL_CMD)) & RTL_CMD_BUFE) == 0) {
        uint8_t *p = c->rx_buf + c->rx_offset;
        uint16_t status = (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
        uint16_t pkt_len = (uint16_t)((uint16_t)p[2] | ((uint16_t)p[3] << 8));

        if ((status & 0x0001u) && pkt_len >= 14u + 4u && pkt_len <= 1514u + 4u) {
            /* Payload begins at p+4 (skip 4-byte HW header); strip trailing
             * 4-byte FCS from length. */
            net_rx_enqueue(&c->nif, p + 4, (uint32_t)pkt_len - 4u);
        } else {
            c->nif.rx_drops++;
        }

        /* Advance rx_offset by 4-byte header + packet length, round up to 4 */
        c->rx_offset = (c->rx_offset + (uint32_t)pkt_len + 4u + 3u) & ~3u;
        if (c->rx_offset >= 8192u) c->rx_offset -= 8192u;

        /* CAPR HW quirk: written value = rx_offset - 16 */
        outw((uint16_t)(c->io_base + RTL_CAPR),
             (uint16_t)((c->rx_offset - 16u) & 0xFFFFu));
    }
}

static void rtl_irq(struct registers *r) {
    (void)r;
    rtl_ctrl_t *c = &rtl_ctrl;
    uint16_t isr = inw((uint16_t)(c->io_base + RTL_ISR));
    outw((uint16_t)(c->io_base + RTL_ISR), isr);   /* W1C */
    if (isr & RTL_ISR_ROK) rtl_rx_drain(c);
    /* TX OK (RTL_ISR_TOK): nothing to do — send() polls OWN bit. */
}

static int rtl_send(net_if_t *nif, const uint8_t *frame, uint32_t len) {
    rtl_ctrl_t *c = nif->driver_data;
    if (len > RTL_TX_BUF_SIZE - 4) {
        nif->tx_errors++;
        return -1;
    }

    int td = c->tx_next;
    /* Wait for this descriptor's prior TX to finish: OWN bit (13) is set
     * by HW on completion. Initial state after reset has bits 13 and 15
     * (OWN|TOK) set, so first use passes immediately. */
    {
        int spin;
        uint32_t tsd = 0;
        for (spin = 0; spin < 100000; spin++) {
            tsd = inl((uint16_t)(c->io_base + RTL_TSD(td)));
            if (tsd & (1u << 13)) break;
            __asm__ volatile("pause");
        }
        if ((tsd & (1u << 13)) == 0u) {
            nif->tx_errors++;
            return -1;
        }
    }

    /* Copy frame into TX buffer */
    for (uint32_t i = 0; i < len; i++) c->tx_buf[td][i] = frame[i];
    /* Pad to minimum 60-byte Ethernet frame (before FCS is auto-added) */
    uint32_t pad_len = len;
    while (pad_len < 60) { c->tx_buf[td][pad_len++] = 0; }

    /* Kick HW: write phys addr, then TSD with length (OWN auto-clears to 0
     * on the TSD write, HW transmits, HW sets OWN=1 when done). */
    outl((uint16_t)(c->io_base + RTL_TSAD(td)), (uint32_t)c->tx_buf[td]);
    outl((uint16_t)(c->io_base + RTL_TSD(td)), pad_len & 0x1FFFu);

    c->tx_next = (td + 1) & 3;
    nif->tx_packets++;
    return (int)len;
}

void rtl8139_probe(void) {
    pci_device_t *d = pci_find_by_vendor_device(0x10EC, 0x8139);
    if (d) (void)rtl8139_init(d);
}

/* Poll RX ring manually — used before STI (e.g. during DHCP init). */
void rtl8139_poll_rx(void);
void rtl8139_poll_rx(void) {
    if (!rtl_present) return;
    rtl_rx_drain(&rtl_ctrl);
}
