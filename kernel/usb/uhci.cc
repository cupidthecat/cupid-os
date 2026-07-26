#include "usb.h"
#include "usb_hc.h"
#include "pci.h"
#include "memory.h"
#include "ports.h"
#include "irq.h"
#include "isr.h"
#include "serial.h"
#include "timer.h"
#include "panic.h"

/* UHCI register offsets (IO-port). */
#define UHCI_USBCMD    0x00
#define UHCI_USBSTS    0x02
#define UHCI_USBINTR   0x04
#define UHCI_FRNUM     0x06
#define UHCI_FRBASEADD 0x08
#define UHCI_SOFMOD    0x0C
#define UHCI_PORTSC(n) (0x10 + 2*(n))

#define UHCI_CMD_RS       (1u << 0)
#define UHCI_CMD_HCRESET  (1u << 1)
#define UHCI_CMD_GRESET   (1u << 2)
#define UHCI_CMD_MAXP64   (1u << 7)

#define UHCI_STS_INT      (1u << 0)
#define UHCI_STS_ERR      (1u << 1)
#define UHCI_STS_HALTED   (1u << 5)

#define UHCI_PORT_CONNECT     (1u << 0)
#define UHCI_PORT_CONNECT_CH  (1u << 1)
#define UHCI_PORT_ENABLE      (1u << 2)
#define UHCI_PORT_LOWSPEED    (1u << 8)
#define UHCI_PORT_RESET       (1u << 9)

#define UHCI_TD_TERMINATE 1u
#define UHCI_TD_QH        (1u << 1)
#define UHCI_TD_DEPTH     (1u << 2)

#define UHCI_TD_ACTIVE    (1u << 23)
#define UHCI_TD_IOC       (1u << 24)
#define UHCI_TD_LS        (1u << 26)
#define UHCI_TD_SPD       (1u << 29)
#define UHCI_TD_CERR_3    (3u << 27)

#define UHCI_PID_SETUP 0x2D
#define UHCI_PID_IN    0x69
#define UHCI_PID_OUT   0xE1
#define UHCI_POLL_STEP_US 50u
#define UHCI_HALT_TIMEOUT_US 10000u

typedef struct __attribute__((packed, aligned(16))) {
    uint32_t link;
    uint32_t ctrl_sts;
    uint32_t token;
    uint32_t buffer;
    uint32_t sw[4];
} uhci_td_t;

typedef struct __attribute__((packed, aligned(16))) {
    uint32_t head_link;
    uint32_t elem_link;
} uhci_qh_t;

#define UHCI_INT_SLOTS 16
typedef struct {
    usb_interrupt_ownership_t owner;
    usb_transfer_t            t;
    usb_complete_cb_t         cb;
} uhci_int_slot_t;

#define UHCI_FRAME_COUNT 1024u

typedef struct {
    uint16_t io_base;
    uint8_t  port_count;
    uint8_t  irq_line;
    uint32_t *frame_list;
    uhci_qh_t *skel_qh;
    void      *skel_qh_raw; /* original kmalloc ptr for kfree */
    volatile uint32_t submit_lock;
    bool submit_failed;
    uhci_int_slot_t interrupt_slots[UHCI_INT_SLOTS];
    usb_hc_t hc;
    uint32_t last_port_status[8];
} uhci_ctrl_t;

#define UHCI_MAX_CTRLS 4
static uhci_ctrl_t uhci_ctrls[UHCI_MAX_CTRLS];
static int uhci_count = 0;

static int uhci_submit_sync(usb_hc_t *, usb_transfer_t *, uint32_t);
static int uhci_submit_interrupt(usb_hc_t *, usb_transfer_t *, usb_complete_cb_t);
static int uhci_cancel_interrupt(usb_hc_t *, usb_transfer_t *);
static int uhci_port_count_fn(usb_hc_t *);
static int uhci_port_status(usb_hc_t *, int port, uint32_t *status);
static usb_port_reset_result_t uhci_port_reset(usb_hc_t *, int port, bool);
static void uhci_irq_handler_fn(usb_hc_t *);

static void uhci_submit_lock(uhci_ctrl_t *c) {
    while (
        __atomic_exchange_n(
            &c->submit_lock,
            1u,
            __ATOMIC_ACQUIRE
        ) != 0u
    ) {
        __asm__ volatile("pause");
    }
}

static void uhci_submit_unlock(uhci_ctrl_t *c) {
    __atomic_store_n(&c->submit_lock, 0u, __ATOMIC_RELEASE);
}

static bool uhci_stop_schedule(
    uhci_ctrl_t *c,
    uint16_t *saved_command
) {
    *saved_command = inw(c->io_base + UHCI_USBCMD);
    outw(
        c->io_base + UHCI_USBCMD,
        (uint16_t)(*saved_command & ~UHCI_CMD_RS)
    );
    for (
        uint32_t waited_us = 0;
        waited_us <= UHCI_HALT_TIMEOUT_US;
        waited_us += UHCI_POLL_STEP_US
    ) {
        if (inw(c->io_base + UHCI_USBSTS) & UHCI_STS_HALTED) {
            return true;
        }
        if (waited_us < UHCI_HALT_TIMEOUT_US) {
            timer_delay_us(UHCI_POLL_STEP_US);
        }
    }
    return false;
}

static void uhci_restore_schedule(
    uhci_ctrl_t *c,
    uint16_t saved_command
) {
    if (saved_command & UHCI_CMD_RS) {
        outw(c->io_base + UHCI_USBCMD, saved_command);
    }
}

static void uhci_global_reset(uint16_t io) {
    outw(io + UHCI_USBCMD, UHCI_CMD_GRESET);
    for (volatile int i = 0; i < 1000000; i++) { }
    outw(io + UHCI_USBCMD, 0);
}

static int uhci_probe_pci(pci_device_t *d) {
    uhci_ctrl_t *c = &uhci_ctrls[uhci_count];

    uint32_t bar4 = d->bars[4];
    if (d->bar_is_mmio[4] || bar4 == 0) {
        KERROR("uhci: BAR4 not IO port");
        return -1;
    }
    c->io_base = (uint16_t)(bar4 & 0xFFFCu);
    c->irq_line = d->irq_line;
    c->port_count = 2;
    c->submit_lock = 0;
    c->submit_failed = false;
    for (int i = 0; i < UHCI_INT_SLOTS; i++) {
        usb_interrupt_ownership_init(&c->interrupt_slots[i].owner);
        c->interrupt_slots[i].t.buffer = NULL;
        c->interrupt_slots[i].cb = NULL;
    }

    pci_config_write_dword(d->bus, d->device, d->function, 0xC0, 0x8F00u);

    pci_enable_bus_master(d);
    uhci_global_reset(c->io_base);

    outw(c->io_base + UHCI_USBCMD, UHCI_CMD_HCRESET);
    for (volatile int i = 0; i < 1000000; i++) { }
    if ((inw(c->io_base + UHCI_USBCMD) & UHCI_CMD_HCRESET) != 0) {
        KERROR("uhci: HC reset stuck");
        return -1;
    }

    c->frame_list = (uint32_t*)pmm_alloc_contiguous(1);
    /* QH must be 16-byte aligned per UHCI spec. kmalloc may not guarantee
     * this, so allocate extra room and align manually.*/
    c->skel_qh_raw = kmalloc(sizeof(uhci_qh_t) + 16u);
    if (!c->frame_list || !c->skel_qh_raw) { KERROR("uhci: alloc failed"); return -1; }
    {
        uint32_t a = ((uint32_t)c->skel_qh_raw + 15u) & ~15u;
        c->skel_qh = (uhci_qh_t *)((char *)c->skel_qh_raw + (a - (uint32_t)c->skel_qh_raw));
    }
    KDEBUG("uhci: skel_qh=%x align=%u", (uint32_t)c->skel_qh, (uint32_t)((uint32_t)c->skel_qh & 15u));

    c->skel_qh->head_link = UHCI_TD_TERMINATE;
    c->skel_qh->elem_link = UHCI_TD_TERMINATE;

    uint32_t skel_phys = (uint32_t)c->skel_qh | UHCI_TD_QH;
    for (uint32_t i = 0; i < UHCI_FRAME_COUNT; i++) c->frame_list[i] = skel_phys;

    outl(c->io_base + UHCI_FRBASEADD, (uint32_t)c->frame_list);
    outw(c->io_base + UHCI_FRNUM, 0);
    outb(c->io_base + UHCI_SOFMOD, 0x40);
    outw(c->io_base + UHCI_USBINTR, 0x000F);
    outw(c->io_base + UHCI_USBCMD, UHCI_CMD_RS | UHCI_CMD_MAXP64);

    c->hc.name = "uhci";
    c->hc.driver_data = c;
    c->hc.root_speed = USB_SPEED_FULL;
    c->hc.submit_sync      = uhci_submit_sync;
    c->hc.submit_interrupt = uhci_submit_interrupt;
    c->hc.cancel_interrupt = uhci_cancel_interrupt;
    c->hc.port_count       = uhci_port_count_fn;
    c->hc.port_status      = uhci_port_status;
    c->hc.port_reset       = uhci_port_reset;
    c->hc.irq_handler      = uhci_irq_handler_fn;
    for (int i = 0; i < 8; i++) c->last_port_status[i] = 0;

    usb_register_hc(&c->hc);
    usb_register_irq(d->irq_line);

    KINFO("uhci: init OK io=%x irq=%u", c->io_base, d->irq_line);
    uhci_count++;
    return 0;
}

void uhci_init_all(void);
void uhci_init_all(void) {
    int start = 0;
    pci_device_t *d;
    while ((d = pci_find_by_class(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_USB,
                                   PCI_PROGIF_UHCI, start)) != NULL) {
        if (uhci_count >= UHCI_MAX_CTRLS) break;
        (void)uhci_probe_pci(d);
        start = 0;
        for (int i = 0; i < pci_device_count(); i++) {
            if (pci_get_device(i) == d) { start = i + 1; break; }
        }
    }
}

static uint32_t uhci_make_token(uint8_t pid, uint8_t addr, uint8_t ep,
                                 uint8_t toggle, uint16_t maxlen) {
    uint16_t mlen = (uint16_t)((maxlen == 0) ? 0x7FFu : (maxlen - 1u));
    return (uint32_t)pid
         | ((uint32_t)(addr & 0x7Fu) << 8)
         | ((uint32_t)(ep   & 0x0Fu) << 15)
         | ((uint32_t)(toggle & 1u)  << 19)
         | ((uint32_t)(mlen  & 0x7FFu) << 21);
}

/* Align a pointer up to 16-byte boundary. */
static inline uhci_td_t *td_align16(void *raw) {
    uint32_t a = ((uint32_t)raw + 15u) & ~15u;
    /* On 32-bit x86, uint32_t and pointer are same size.
     * Cast via char* to satisfy strict-aliasing/pointer-arith warnings.*/
    return (uhci_td_t *)((char *)raw + (a - (uint32_t)raw));
}

static int uhci_submit_sync(usb_hc_t *hc, usb_transfer_t *t, uint32_t timeout_ms) {
    uhci_ctrl_t *c = hc->driver_data;

    uint8_t pid = (t->dir == USB_DIR_SETUP) ? UHCI_PID_SETUP
              : (t->dir == USB_DIR_IN)      ? UHCI_PID_IN : UHCI_PID_OUT;

    #define UHCI_MAX_TDS 64
    uhci_td_t *tds[UHCI_MAX_TDS];
    void      *td_raw[UHCI_MAX_TDS]; /* original kmalloc pointers for kfree */
    int td_count = 0;
    uint32_t remaining = t->length;
    uint8_t toggle = t->data_toggle;
    uint8_t *bufp = t->buffer;
    do {
        if (td_count >= UHCI_MAX_TDS) {
            KERROR("uhci: too many TDs");
            for (int _j = 0; _j < td_count; _j++) kfree(td_raw[_j]);
            return -1;
        }
        /* Allocate sizeof(uhci_td_t)+16 so we can align to 16 bytes. */
        void *raw = kmalloc(sizeof(uhci_td_t) + 16u);
        if (!raw) {
            for (int _j = 0; _j < td_count; _j++) kfree(td_raw[_j]);
            return -1;
        }
        uhci_td_t *td = td_align16(raw);
        td_raw[td_count] = raw;
        uint16_t chunk = (uint16_t)((remaining > t->max_packet) ? t->max_packet : remaining);
        td->ctrl_sts = UHCI_TD_ACTIVE | UHCI_TD_CERR_3
                     | (t->speed == USB_SPEED_LOW ? UHCI_TD_LS : 0u);
        td->token = uhci_make_token(pid, t->device_addr, t->endpoint, toggle, chunk);
        td->buffer = (uint32_t)bufp;
        td->link = UHCI_TD_TERMINATE;
        td->sw[0] = 0;
        tds[td_count++] = td;
        bufp += chunk;
        remaining -= chunk;
        toggle = (uint8_t)(toggle ^ 1u);
        if (t->length == 0) break;
    } while (remaining > 0);

    for (int i = 0; i + 1 < td_count; i++) {
        tds[i]->link = (uint32_t)tds[i+1] | UHCI_TD_DEPTH;
    }
    tds[td_count-1]->ctrl_sts |= UHCI_TD_IOC;

    uhci_submit_lock(c);
    if (c->submit_failed) {
        uhci_submit_unlock(c);
        for (int i = 0; i < td_count; i++) kfree(td_raw[i]);
        return -3;
    }

    c->skel_qh->elem_link = (uint32_t)tds[0];

    uint32_t timeout_us = timeout_ms > 4294967u
        ? 0xFFFFFFFFu : timeout_ms * 1000u;
    uint32_t waited_us = 0;
    int status = -1;
    for (;;) {
        bool done = true;
        for (int i = 0; i < td_count; i++) {
            if (tds[i]->ctrl_sts & UHCI_TD_ACTIVE) { done = false; break; }
        }
        if (done) { status = 0; break; }
        if (waited_us >= timeout_us) break;
        uint32_t delay_us = timeout_us - waited_us;
        if (delay_us > UHCI_POLL_STEP_US) delay_us = UHCI_POLL_STEP_US;
        timer_delay_us(delay_us);
        waited_us += delay_us;
    }
    uint32_t s = tds[td_count-1]->ctrl_sts;
    if (s & (0x7Eu << 16)) status = -2;

    c->skel_qh->elem_link = UHCI_TD_TERMINATE;
    /*
     * UHCI may prefetch a queue element before software unlinks it. Prove the
     * schedule is halted before releasing TD storage on success, error, or
     * timeout.
     */
    uint16_t saved_command = 0;
    if (!uhci_stop_schedule(c, &saved_command)) {
        c->submit_failed = true;
        uhci_submit_unlock(c);
        kernel_panic(
            "uhci: DMA ownership could not be revoked after transfer"
        );
    }
    for (int i = 0; i < td_count; i++) kfree(td_raw[i]);
    uhci_restore_schedule(c, saved_command);

    if (status == 0) t->data_toggle = toggle;
    uhci_submit_unlock(c);
    return status;
}

static int uhci_submit_interrupt(usb_hc_t *hc, usb_transfer_t *t, usb_complete_cb_t cb) {
    uhci_ctrl_t *c = (uhci_ctrl_t*)hc->driver_data;
    uhci_submit_lock(c);
    if (c->submit_failed) {
        uhci_submit_unlock(c);
        return -1;
    }
    for (int i = 0; i < UHCI_INT_SLOTS; i++) {
        uhci_int_slot_t *slot = &c->interrupt_slots[i];
        if (usb_interrupt_ownership_publish(&slot->owner) != 0u) {
            slot->t = *t;
            slot->cb = cb;
            uhci_submit_unlock(c);
            return 0;
        }
    }
    uhci_submit_unlock(c);
    return -1;
}
static int uhci_cancel_interrupt(usb_hc_t *hc, usb_transfer_t *t) {
    uhci_ctrl_t *c = (uhci_ctrl_t*)hc->driver_data;
    uhci_int_slot_t *slot = NULL;
    uint32_t generation = 0u;
    uhci_submit_lock(c);
    for (int i = 0; i < UHCI_INT_SLOTS; i++) {
        uhci_int_slot_t *candidate = &c->interrupt_slots[i];
        if (
            candidate->owner.active
            && candidate->t.device_addr == t->device_addr
            && candidate->t.endpoint == t->endpoint
            && candidate->t.buffer == t->buffer
        ) {
            slot = candidate;
            generation = usb_interrupt_ownership_request_cancel(
                &slot->owner
            );
            break;
        }
    }
    uhci_submit_unlock(c);
    if (!slot || generation == 0u) return -1;

    /*
     * The poller owns the caller's buffer until both DMA and its callback
     * finish. Waiting here makes a successful return a release guarantee.
     */
    while (usb_interrupt_ownership_is_in_flight(&slot->owner)) {
        __asm__ volatile("pause");
    }

    uhci_submit_lock(c);
    if (slot->owner.generation != generation) {
        uhci_submit_unlock(c);
        return 0;
    }
    if (!usb_interrupt_ownership_retire(&slot->owner, generation)) {
        uhci_submit_unlock(c);
        return -1;
    }
    slot->cb = NULL;
    slot->t.buffer = NULL;
    uhci_submit_unlock(c);
    return 0;
}
static int uhci_port_count_fn(usb_hc_t *hc) {
    return ((uhci_ctrl_t*)hc->driver_data)->port_count;
}
static int uhci_port_status(usb_hc_t *hc, int port, uint32_t *status) {
    uhci_ctrl_t *c = hc->driver_data;
    if (port < 0 || port >= c->port_count) return -1;
    *status = inw((uint16_t)(c->io_base + UHCI_PORTSC(port)));
    return 0;
}
static usb_port_reset_result_t uhci_port_reset(
    usb_hc_t *hc,
    int port,
    bool must_reset
) {
    (void)must_reset;
    uhci_ctrl_t *c = hc->driver_data;
    if (port < 0 || port >= c->port_count) {
        return USB_PORT_RESET_FAILED;
    }
    uint16_t val = inw((uint16_t)(c->io_base + UHCI_PORTSC(port)));
    KDEBUG("uhci: port %u reset start portsc=%x", (uint32_t)port, (uint32_t)val);
    outw((uint16_t)(c->io_base + UHCI_PORTSC(port)), (uint16_t)(val | UHCI_PORT_RESET));
    for (volatile int i = 0; i < 1000000; i++) { }
    outw((uint16_t)(c->io_base + UHCI_PORTSC(port)), (uint16_t)(val & ~UHCI_PORT_RESET));
    for (volatile int i = 0; i < 200000; i++) { }
    outw((uint16_t)(c->io_base + UHCI_PORTSC(port)),
         (uint16_t)(inw((uint16_t)(c->io_base + UHCI_PORTSC(port))) | UHCI_PORT_ENABLE));
    uint16_t final = inw((uint16_t)(c->io_base + UHCI_PORTSC(port)));
    KDEBUG("uhci: port %u reset done portsc=%x", (uint32_t)port, (uint32_t)final);
    if (
        (final & UHCI_PORT_CONNECT) == 0u
        || (final & UHCI_PORT_RESET) != 0u
        || (final & UHCI_PORT_ENABLE) == 0u
    ) {
        KWARN("uhci: port %u reset did not reach enabled state", (uint32_t)port);
        return USB_PORT_RESET_FAILED;
    }
    return USB_PORT_RESET_OK;
}
static void uhci_irq_handler_fn(usb_hc_t *hc) {
    uhci_ctrl_t *c = hc->driver_data;
    uint16_t sts = inw(c->io_base + UHCI_USBSTS);
    outw(c->io_base + UHCI_USBSTS, sts);
    (void)sts;
}
void uhci_poll_interrupts(void);
void uhci_poll_interrupts(void) {
    for (int controller = 0; controller < uhci_count; controller++) {
        uhci_ctrl_t *c = &uhci_ctrls[controller];
        for (int i = 0; i < UHCI_INT_SLOTS; i++) {
            uhci_int_slot_t *slot = &c->interrupt_slots[i];
            usb_transfer_t local;
            usb_complete_cb_t cb = NULL;
            uint32_t generation = 0u;

            uhci_submit_lock(c);
            if (!c->submit_failed) {
                generation = usb_interrupt_ownership_claim(&slot->owner);
                if (generation != 0u) {
                    local = slot->t;
                    cb = slot->cb;
                }
            }
            uhci_submit_unlock(c);
            if (generation == 0u) continue;

            int r = uhci_submit_sync(&c->hc, &local, 5);
            bool deliver = false;
            uhci_submit_lock(c);
            if (
                r == 0
                && usb_interrupt_ownership_may_deliver(
                    &slot->owner,
                    generation
                )
            ) {
                slot->t.data_toggle = local.data_toggle;
                deliver = cb != NULL;
            }
            uhci_submit_unlock(c);

            if (deliver) cb(0, &local);

            uhci_submit_lock(c);
            usb_interrupt_ownership_finish(&slot->owner, generation);
            uhci_submit_unlock(c);
        }
    }
}

void uhci_poll_ports(void);
void uhci_poll_ports(void) {
    for (int i = 0; i < uhci_count; i++) {
        uhci_ctrl_t *c = &uhci_ctrls[i];
        for (int p = 0; p < c->port_count; p++) {
            uint16_t val = inw((uint16_t)(c->io_base + UHCI_PORTSC(p)));
            if (val & UHCI_PORT_CONNECT_CH) {
                /*
                 * PORTSC keeps the change bit asserted until software clears
                 * it. A full software queue therefore leaves the bit alone so
                 * the next poll can retry the same port.
                 */
                if (usb_port_change(&c->hc, p)) {
                    outw((uint16_t)(c->io_base + UHCI_PORTSC(p)),
                         (uint16_t)(val | UHCI_PORT_CONNECT_CH));
                }
            }
        }
    }
}
