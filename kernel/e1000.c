#include "net_if.h"
#include "pci.h"
#include "memory.h"
#include "irq.h"
#include "isr.h"
#include "../drivers/serial.h"

#define E1000_CTRL    0x00000u
#define E1000_STATUS  0x00008u
#define E1000_EECD    0x00010u
#define E1000_EERD    0x00014u
#define E1000_ICR     0x000C0u
#define E1000_IMS     0x000D0u
#define E1000_RCTL    0x00100u
#define E1000_RDBAL   0x02800u
#define E1000_RDBAH   0x02804u
#define E1000_RDLEN   0x02808u
#define E1000_RDH     0x02810u
#define E1000_RDT     0x02818u
#define E1000_TCTL    0x00400u
#define E1000_TDBAL   0x03800u
#define E1000_TDBAH   0x03804u
#define E1000_TDLEN   0x03808u
#define E1000_TDH     0x03810u
#define E1000_TDT     0x03818u
#define E1000_RAL0    0x05400u
#define E1000_RAH0    0x05404u

#define E1000_RX_RING_LEN 64
#define E1000_TX_RING_LEN 16
#define E1000_BUF_SIZE   2048

typedef struct __attribute__((packed, aligned(16))) {
    uint64_t addr;
    uint16_t len;
    uint16_t csum;
    uint8_t  status;
    uint8_t  err;
    uint16_t special;
} e1000_rx_desc_t;

typedef struct __attribute__((packed, aligned(16))) {
    uint64_t addr;
    uint16_t len;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  sta;
    uint8_t  css;
    uint16_t special;
} e1000_tx_desc_t;

typedef struct {
    volatile uint8_t *mmio;
    uint8_t  irq;
    e1000_rx_desc_t *rx_ring;
    e1000_tx_desc_t *tx_ring;
    uint8_t  *rx_bufs[E1000_RX_RING_LEN];
    uint8_t  *tx_bufs[E1000_TX_RING_LEN];
    int      tx_next;
    net_if_t nif;
} e1000_ctrl_t;

/* Forward declarations */
bool e1000_init(pci_device_t *d);
void e1000_probe(void);
static void e1000_irq(struct registers *r);
static int  e1000_send(net_if_t *nif, const uint8_t *frame, uint32_t len);
static void e1000_rx_drain(e1000_ctrl_t *c);
static void e1000_poll_rx_nif(net_if_t *nif);
static uint32_t reg_read(e1000_ctrl_t *c, uint32_t off);
static void reg_write(e1000_ctrl_t *c, uint32_t off, uint32_t val);
static void read_mac(e1000_ctrl_t *c, uint8_t *mac);

static e1000_ctrl_t e1000_ctrl;
static bool e1000_present = false;

static void e1000_poll_rx_nif(net_if_t *nif) {
    (void)nif;
    e1000_rx_drain(&e1000_ctrl);
}

static uint32_t reg_read(e1000_ctrl_t *c, uint32_t off) {
    return *(volatile uint32_t*)(c->mmio + off);
}

static void reg_write(e1000_ctrl_t *c, uint32_t off, uint32_t val) {
    *(volatile uint32_t*)(c->mmio + off) = val;
}

static void read_mac(e1000_ctrl_t *c, uint8_t *mac) {
    uint32_t low  = reg_read(c, E1000_RAL0);
    uint32_t high = reg_read(c, E1000_RAH0);
    mac[0] = (uint8_t)(low);
    mac[1] = (uint8_t)(low  >> 8);
    mac[2] = (uint8_t)(low  >> 16);
    mac[3] = (uint8_t)(low  >> 24);
    mac[4] = (uint8_t)(high);
    mac[5] = (uint8_t)(high >> 8);
}

static int e1000_send(net_if_t *nif, const uint8_t *frame, uint32_t len) {
    e1000_ctrl_t *c = (e1000_ctrl_t *)nif->driver_data;
    int td;
    uint32_t i;
    int spin;
    if (len == 0u || len > (uint32_t)E1000_BUF_SIZE) {
        nif->tx_errors++;
        return -1;
    }
    td = c->tx_next;
    for (i = 0; i < len; i++) c->tx_bufs[td][i] = frame[i];
    c->tx_ring[td].addr = (uint64_t)(uint32_t)c->tx_bufs[td];
    c->tx_ring[td].len  = (uint16_t)len;
    c->tx_ring[td].cmd  = 0x0Bu;   /* RS | IFCS | EOP */
    c->tx_ring[td].sta  = 0;
    c->tx_next = (int)((uint32_t)(td + 1) % (uint32_t)E1000_TX_RING_LEN);
    reg_write(c, E1000_TDT, (uint32_t)c->tx_next);
    for (spin = 0; spin < 100000; spin++) {
        if (c->tx_ring[td].sta & 0x01u) break;
        __asm__ volatile("pause");
    }
    if (!(c->tx_ring[td].sta & 0x01u)) {
        nif->tx_errors++;
        return -1;
    }
    nif->tx_packets++;
    return (int)len;
}

static void e1000_rx_drain(e1000_ctrl_t *c) {
    uint32_t head = reg_read(c, E1000_RDH);
    uint32_t tail = reg_read(c, E1000_RDT);
    while (tail != head) {
        uint32_t idx = (tail + 1u) % (uint32_t)E1000_RX_RING_LEN;
        if (!(c->rx_ring[idx].status & 0x01u)) break;
        net_rx_enqueue(&c->nif, c->rx_bufs[idx], c->rx_ring[idx].len);
        c->rx_ring[idx].status = 0;
        tail = idx;
    }
    reg_write(c, E1000_RDT, tail);
}

static void e1000_irq(struct registers *r) {
    e1000_ctrl_t *c = &e1000_ctrl;
    uint32_t icr;
    (void)r;
    icr = reg_read(c, E1000_ICR);
    reg_write(c, E1000_ICR, icr);
    if (icr & (0x80u | 0x40u)) e1000_rx_drain(c);
}

bool e1000_init(pci_device_t *d) {
    e1000_ctrl_t *c = &e1000_ctrl;
    uint32_t bar0 = d->bars[0];
    void *rx_page;
    void *tx_page;
    int i;

    if (!d->bar_is_mmio[0] || bar0 == 0) {
        KERROR("e1000: BAR0 not MMIO");
        return false;
    }
    paging_map_mmio(bar0, 128u * 1024u);
    c->mmio = (volatile uint8_t*)bar0;
    c->irq  = d->irq_line;

    pci_enable_bus_master(d);

    /* Software reset */
    reg_write(c, E1000_CTRL, reg_read(c, E1000_CTRL) | (1u << 26));
    for (i = 0; i < 1000; i++) {
        if ((reg_read(c, E1000_CTRL) & (1u << 26)) == 0u) break;
        { volatile int k; for (k = 0; k < 100000; k++) { } }
    }

    read_mac(c, c->nif.mac);

    /* RX ring (page-aligned) */
    rx_page = pmm_alloc_page();
    if (!rx_page) { KERROR("e1000: rx ring alloc failed"); return false; }
    c->rx_ring = (e1000_rx_desc_t*)rx_page;
    for (i = 0; i < E1000_RX_RING_LEN; i++) {
        c->rx_bufs[i] = (uint8_t*)kmalloc(E1000_BUF_SIZE);
        if (!c->rx_bufs[i]) { KERROR("e1000: rx buf alloc failed"); return false; }
        c->rx_ring[i].addr   = (uint64_t)(uint32_t)c->rx_bufs[i];
        c->rx_ring[i].status = 0;
    }
    reg_write(c, E1000_RDBAL, (uint32_t)c->rx_ring);
    reg_write(c, E1000_RDBAH, 0u);
    reg_write(c, E1000_RDLEN, (uint32_t)E1000_RX_RING_LEN * 16u);
    reg_write(c, E1000_RDH,   0u);
    reg_write(c, E1000_RDT,   (uint32_t)(E1000_RX_RING_LEN - 1));
    reg_write(c, E1000_RCTL,  0x0400804u);  /* EN | BAM | BSIZE_2048 */

    /* TX ring */
    tx_page = pmm_alloc_page();
    if (!tx_page) { KERROR("e1000: tx ring alloc failed"); return false; }
    c->tx_ring = (e1000_tx_desc_t*)tx_page;
    for (i = 0; i < E1000_TX_RING_LEN; i++) {
        c->tx_bufs[i] = (uint8_t*)kmalloc(E1000_BUF_SIZE);
        if (!c->tx_bufs[i]) { KERROR("e1000: tx buf alloc failed"); return false; }
        c->tx_ring[i].addr  = 0;
        c->tx_ring[i].sta   = 1;
    }
    reg_write(c, E1000_TDBAL, (uint32_t)c->tx_ring);
    reg_write(c, E1000_TDBAH, 0u);
    reg_write(c, E1000_TDLEN, (uint32_t)E1000_TX_RING_LEN * 16u);
    reg_write(c, E1000_TDH,   0u);
    reg_write(c, E1000_TDT,   0u);
    c->tx_next = 0;
    reg_write(c, E1000_TCTL,  0x01030002u);

    reg_write(c, E1000_IMS, 0x80u | 0x40u);

    c->nif.name        = "e1000";
    c->nif.driver_data = c;
    c->nif.link_up     = true;
    c->nif.send        = e1000_send;
    c->nif.poll_rx     = e1000_poll_rx_nif;
    c->nif.rx_packets  = 0;
    c->nif.tx_packets  = 0;
    c->nif.rx_drops    = 0;
    c->nif.tx_errors   = 0;
    c->nif.ipv4_addr   = 0;
    c->nif.ipv4_mask   = 0;
    c->nif.ipv4_gateway = 0;
    c->nif.ipv4_dns    = 0;

    irq_install_handler(c->irq, e1000_irq);
    net_if_register(&c->nif);
    KINFO("e1000: init OK mmio=%x irq=%u", bar0, (uint32_t)c->irq);
    e1000_present = true;
    return true;
}

void e1000_probe(void) {
    pci_device_t *d = pci_find_by_vendor_device(0x8086u, 0x100Eu);
    if (d) (void)e1000_init(d);
}
