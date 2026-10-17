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

/* Capability registers (RO, at BAR0). */
#define EHCI_CAP_CAPLENGTH  0x00
#define EHCI_CAP_HCIVERSION 0x02
#define EHCI_CAP_HCSPARAMS  0x04
#define EHCI_CAP_HCCPARAMS  0x08

/* Operational registers (offset = caplength). */
#define EHCI_OP_USBCMD    0x00
#define EHCI_OP_USBSTS    0x04
#define EHCI_OP_USBINTR   0x08
#define EHCI_OP_FRINDEX   0x0C
#define EHCI_OP_PERIODICLISTBASE 0x14
#define EHCI_OP_ASYNCLISTADDR    0x18
#define EHCI_OP_CONFIGFLAG       0x40
#define EHCI_OP_PORTSC(n) (0x44u + 4u*(uint32_t)(n))

#define EHCI_CMD_RUN         (1u << 0)
#define EHCI_CMD_HCRESET     (1u << 1)
#define EHCI_CMD_PERIOD_EN   (1u << 4)
#define EHCI_CMD_ASYNC_EN    (1u << 5)
#define EHCI_CMD_INT_ASYNC_ADV (1u << 6)
#define EHCI_CMD_FLS_1024    (0u << 2)

#define EHCI_STS_INT       (1u << 0)
#define EHCI_STS_ERR       (1u << 1)
#define EHCI_STS_PORT_CH   (1u << 2)
#define EHCI_STS_HOST_ERR  (1u << 4)
#define EHCI_STS_ASYNC_ADV (1u << 5)
#define EHCI_STS_HALTED    (1u << 12)
#define EHCI_STS_ASYNC     (1u << 15)

#define EHCI_PORTSC_CONNECT     (1u << 0)
#define EHCI_PORTSC_CONNECT_CH  (1u << 1)
#define EHCI_PORTSC_ENABLE      (1u << 2)
#define EHCI_PORTSC_ENABLE_CH   (1u << 3)
#define EHCI_PORTSC_RESET       (1u << 8)
#define EHCI_PORTSC_LINESTATUS  (3u << 10)
#define EHCI_PORTSC_OWNER       (1u << 13)

#define EHCI_QTD_STATUS_ACTIVE (1u << 7)
#define EHCI_QTD_STATUS_HALTED (1u << 6)
#define EHCI_QTD_PID_OUT   (0u << 8)
#define EHCI_QTD_PID_IN    (1u << 8)
#define EHCI_QTD_PID_SETUP (2u << 8)
#define EHCI_QTD_CERR_3    (3u << 10)
#define EHCI_QTD_TOGGLE    (1u << 31)
#define EHCI_QTD_IOC       (1u << 15)
#define EHCI_POLL_STEP_US   50u
#define EHCI_QUIESCE_TIMEOUT_US 10000u

typedef struct __attribute__((packed, aligned(32))) {
    uint32_t next;
    uint32_t alt_next;
    uint32_t token;
    uint32_t buf_page[5];
} ehci_qtd_t;

typedef struct __attribute__((packed, aligned(32))) {
    uint32_t horiz_link;
    uint32_t ep_chars;
    uint32_t ep_caps;
    uint32_t current_qtd;
    uint32_t overlay_next;
    uint32_t overlay_alt;
    uint32_t overlay_token;
    uint32_t overlay_buf[5];
} ehci_qh_t;

#define EHCI_INT_SLOTS 16
typedef struct {
    usb_interrupt_ownership_t owner;
    usb_transfer_t            t;
    usb_complete_cb_t         cb;
} ehci_int_slot_t;

typedef struct {
    void    *mmio_base;
    void    *op_base;
    uint8_t  port_count;
    uint8_t  irq_line;
    uint32_t *periodic_list;
    ehci_qh_t *async_head;
    void      *async_head_raw; /* original kmalloc ptr for kfree */
    volatile uint32_t submit_lock;
    volatile uint32_t pending_ports;
    bool submit_failed;
    ehci_int_slot_t interrupt_slots[EHCI_INT_SLOTS];
    usb_hc_t  hc;
} ehci_ctrl_t;

#define EHCI_MAX_CTRLS 4
static ehci_ctrl_t ehci_ctrls[EHCI_MAX_CTRLS];
static int ehci_count = 0;

static inline uint32_t ehci_op_read(ehci_ctrl_t *c, uint32_t off) {
    return *(volatile uint32_t*)((uint8_t*)c->op_base + off);
}
static inline void ehci_op_write(ehci_ctrl_t *c, uint32_t off, uint32_t v) {
    *(volatile uint32_t*)((uint8_t*)c->op_base + off) = v;
}

static bool ehci_wait_status(
    ehci_ctrl_t *c,
    uint32_t mask,
    bool want_set,
    uint32_t timeout_us
) {
    uint32_t waited_us = 0;
    for (;;) {
        bool is_set = (ehci_op_read(c, EHCI_OP_USBSTS) & mask) != 0;
        if (is_set == want_set) return true;
        if (waited_us >= timeout_us) return false;
        uint32_t delay_us = timeout_us - waited_us;
        if (delay_us > EHCI_POLL_STEP_US) delay_us = EHCI_POLL_STEP_US;
        timer_delay_us(delay_us);
        waited_us += delay_us;
    }
}

/*
 * ASYNCLISTADDR is only safe to replace after the controller acknowledges
 * that the asynchronous schedule stopped. If that handshake stalls, halt the
 * controller and prove HCHalted before allowing a caller to switch or release
 * DMA memory.
 */
static bool ehci_halt_controller(ehci_ctrl_t *c) {
    uint32_t command = ehci_op_read(c, EHCI_OP_USBCMD);
    ehci_op_write(c, EHCI_OP_USBCMD, command & ~EHCI_CMD_RUN);
    if (!ehci_wait_status(
            c,
            EHCI_STS_HALTED,
            true,
            EHCI_QUIESCE_TIMEOUT_US
        )) {
        return false;
    }

    /*
     * Once halted, clear a pending Async Enable request as a separate write.
     * This leaves both the command bit and ASS at zero before ASYNCLISTADDR is
     * touched.
     */
    command = ehci_op_read(c, EHCI_OP_USBCMD);
    ehci_op_write(c, EHCI_OP_USBCMD, command & ~EHCI_CMD_ASYNC_EN);
    return (
        (ehci_op_read(c, EHCI_OP_USBCMD) & EHCI_CMD_ASYNC_EN) == 0
        && (ehci_op_read(c, EHCI_OP_USBSTS) & EHCI_STS_ASYNC) == 0
    );
}

static bool ehci_quiesce_async(ehci_ctrl_t *c) {
    uint32_t command = ehci_op_read(c, EHCI_OP_USBCMD);
    bool requested = (command & EHCI_CMD_ASYNC_EN) != 0;
    bool active =
        (ehci_op_read(c, EHCI_OP_USBSTS) & EHCI_STS_ASYNC) != 0;

    if (!requested && !active) return true;

    /*
     * EHCI only permits changing Async Enable when its requested and actual
     * states agree. A mismatched state is handled by stopping the controller
     * without changing Async Enable, then clearing the request after halt.
     */
    if (requested && active) {
        ehci_op_write(c, EHCI_OP_USBCMD, command & ~EHCI_CMD_ASYNC_EN);
        if (ehci_wait_status(
                c,
                EHCI_STS_ASYNC,
                false,
                EHCI_QUIESCE_TIMEOUT_US
            )) {
            return true;
        }
    }

    return ehci_halt_controller(c);
}

static int ehci_submit_sync(usb_hc_t *, usb_transfer_t *, uint32_t);
static int ehci_submit_interrupt(usb_hc_t *, usb_transfer_t *, usb_complete_cb_t);
static int ehci_cancel_interrupt(usb_hc_t *, usb_transfer_t *);
static int ehci_port_count_fn(usb_hc_t *hc);
static int ehci_port_status(usb_hc_t *, int, uint32_t *);
static usb_port_reset_result_t ehci_port_reset(usb_hc_t *, int, bool);
static void ehci_irq_handler_fn(usb_hc_t *);

static void ehci_submit_lock(ehci_ctrl_t *c) {
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

static void ehci_submit_unlock(ehci_ctrl_t *c) {
    __atomic_store_n(&c->submit_lock, 0u, __ATOMIC_RELEASE);
}

static void ehci_bios_handoff(pci_device_t *d, ehci_ctrl_t *c) {
    uint32_t hccparams = *(volatile uint32_t*)((uint8_t*)c->mmio_base + EHCI_CAP_HCCPARAMS);
    uint8_t eecp = (uint8_t)((hccparams >> 8) & 0xFFu);
    if (eecp < 0x40) return;
    uint32_t legsup = pci_config_read_dword(d->bus, d->device, d->function, eecp);
    if ((legsup & 0xFFu) != 0x01u) return;
    pci_config_write_dword(d->bus, d->device, d->function, eecp, legsup | (1u << 24));
    for (int i = 0; i < 1000; i++) {
        uint32_t v = pci_config_read_dword(d->bus, d->device, d->function, eecp);
        if ((v & (1u << 16)) == 0) return;
        for (volatile int k = 0; k < 100000; k++) { }
    }
    pci_config_write_dword(d->bus, d->device, d->function, eecp,
        pci_config_read_dword(d->bus, d->device, d->function, eecp) & ~(1u << 16));
    KWARN("ehci: BIOS handoff timeout, forced");
}

static int ehci_probe_pci(pci_device_t *d) {
    ehci_ctrl_t *c = &ehci_ctrls[ehci_count];

    if (!d->bar_is_mmio[0] || d->bars[0] == 0) { KERROR("ehci: BAR0 not MMIO"); return -1; }
    c->mmio_base = (void*)d->bars[0];
    c->irq_line = d->irq_line;
    c->submit_lock = 0;
    c->pending_ports = 0;
    c->submit_failed = false;
    for (int i = 0; i < EHCI_INT_SLOTS; i++) {
        usb_interrupt_ownership_init(&c->interrupt_slots[i].owner);
        c->interrupt_slots[i].t.buffer = NULL;
        c->interrupt_slots[i].cb = NULL;
    }

    /* Map EHCI MMIO region into the page tables before any register access.
     * The BAR is typically above IDENTITY_MAP_SIZE (e.g. 0xfeb...) so the
     * region is not covered by paging_init()'s identity map.*/
    paging_map_mmio(d->bars[0], 4096);

    uint8_t caplen = *(volatile uint8_t*)((uint8_t*)c->mmio_base + EHCI_CAP_CAPLENGTH);
    c->op_base = (uint8_t*)c->mmio_base + caplen;
    uint32_t hcs = *(volatile uint32_t*)((uint8_t*)c->mmio_base + EHCI_CAP_HCSPARAMS);
    c->port_count = (uint8_t)(hcs & 0xFu);

    pci_enable_bus_master(d);
    ehci_bios_handoff(d, c);

    ehci_op_write(c, EHCI_OP_USBCMD, EHCI_CMD_HCRESET);
    for (int i = 0; i < 1000; i++) {
        if ((ehci_op_read(c, EHCI_OP_USBCMD) & EHCI_CMD_HCRESET) == 0) break;
        for (volatile int k = 0; k < 100000; k++) { }
    }

    c->periodic_list  = (uint32_t*)pmm_alloc_contiguous(1);
    /* QH must be 32-byte aligned per EHCI spec. kmalloc may not guarantee
     * this, so allocate extra room and align manually.*/
    c->async_head_raw = kmalloc(sizeof(ehci_qh_t) + 32u);
    if (!c->periodic_list || !c->async_head_raw) { KERROR("ehci: alloc failed"); return -1; }
    {
        uint32_t a = ((uint32_t)c->async_head_raw + 31u) & ~31u;
        c->async_head = (ehci_qh_t *)((char *)c->async_head_raw + (a - (uint32_t)c->async_head_raw));
    }
    for (int i = 0; i < 1024; i++) c->periodic_list[i] = 1u;

    c->async_head->horiz_link = (uint32_t)c->async_head | 0x2u;
    c->async_head->ep_chars = (1u << 15);
    c->async_head->ep_caps = 0;
    c->async_head->current_qtd = 0;
    c->async_head->overlay_next = 1u;
    c->async_head->overlay_alt  = 1u;
    c->async_head->overlay_token = (1u << 6);
    for (int i = 0; i < 5; i++) c->async_head->overlay_buf[i] = 0;

    ehci_op_write(c, EHCI_OP_PERIODICLISTBASE, (uint32_t)c->periodic_list);
    ehci_op_write(c, EHCI_OP_ASYNCLISTADDR,    (uint32_t)c->async_head);
    ehci_op_write(c, EHCI_OP_USBINTR, EHCI_STS_INT | EHCI_STS_ERR
                                      | EHCI_STS_PORT_CH | EHCI_STS_HOST_ERR);
    ehci_op_write(c, EHCI_OP_USBCMD, EHCI_CMD_RUN | EHCI_CMD_PERIOD_EN
                                     | EHCI_CMD_ASYNC_EN | EHCI_CMD_FLS_1024);
    ehci_op_write(c, EHCI_OP_CONFIGFLAG, 1);

    c->hc.name = "ehci";
    c->hc.driver_data = c;
    c->hc.root_speed = USB_SPEED_HIGH;
    c->hc.submit_sync      = ehci_submit_sync;
    c->hc.submit_interrupt = ehci_submit_interrupt;
    c->hc.cancel_interrupt = ehci_cancel_interrupt;
    c->hc.port_count       = ehci_port_count_fn;
    c->hc.port_status      = ehci_port_status;
    c->hc.port_reset       = ehci_port_reset;
    c->hc.irq_handler      = ehci_irq_handler_fn;

    usb_register_hc(&c->hc);
    usb_register_irq(d->irq_line);

    KINFO("ehci: init OK mmio=%p ports=%u irq=%u",
          c->mmio_base, c->port_count, d->irq_line);
    ehci_count++;
    return 0;
}

void ehci_init_all(void);
void ehci_init_all(void) {
    int start = 0;
    pci_device_t *d;
    while ((d = pci_find_by_class(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_USB,
                                   PCI_PROGIF_EHCI, start)) != NULL) {
        if (ehci_count >= EHCI_MAX_CTRLS) break;
        (void)ehci_probe_pci(d);
        start = 0;
        for (int i = 0; i < pci_device_count(); i++) {
            if (pci_get_device(i) == d) { start = i + 1; break; }
        }
    }
}

static uint32_t ehci_make_token(uint8_t pid, uint16_t bytes, uint8_t toggle) {
    uint32_t t = EHCI_QTD_STATUS_ACTIVE | EHCI_QTD_CERR_3
               | ((uint32_t)bytes << 16);
    if (pid == USB_DIR_SETUP)    t |= EHCI_QTD_PID_SETUP;
    else if (pid == USB_DIR_IN)  t |= EHCI_QTD_PID_IN;
    else                         t |= EHCI_QTD_PID_OUT;
    if (toggle) t |= EHCI_QTD_TOGGLE;
    return t;
}

static void ehci_qtd_init(ehci_qtd_t *q, uint32_t token, void *buf, uint32_t len) {
    q->next = 1u; q->alt_next = 1u; q->token = token;
    uint32_t addr = (uint32_t)buf;
    for (int i = 0; i < 5; i++) q->buf_page[i] = 0;
    q->buf_page[0] = addr;
    (void)len;
    uint32_t base = addr & ~0xFFFu;
    for (int i = 1; i < 5; i++) q->buf_page[i] = base + (uint32_t)(4096u * (uint32_t)i);
}

/* Align a pointer up to 32-byte boundary. */
static inline ehci_qtd_t *qtd_align32(void *raw) {
    uint32_t a = ((uint32_t)raw + 31u) & ~31u;
    return (ehci_qtd_t *)((char *)raw + (a - (uint32_t)raw));
}
static inline ehci_qh_t *qh_align32(void *raw) {
    uint32_t a = ((uint32_t)raw + 31u) & ~31u;
    return (ehci_qh_t *)((char *)raw + (a - (uint32_t)raw));
}

static int ehci_submit_sync(usb_hc_t *hc, usb_transfer_t *t, uint32_t timeout_ms) {
    ehci_ctrl_t *c = hc->driver_data;
    /* Allocate sizeof+32 so we can align to 32 bytes per EHCI spec. */
    void *q_raw  = kmalloc(sizeof(ehci_qtd_t) + 32u);
    void *qh_raw = kmalloc(sizeof(ehci_qh_t)  + 32u);
    if (!q_raw || !qh_raw) { if (q_raw) kfree(q_raw); if (qh_raw) kfree(qh_raw); return -1; }
    ehci_qtd_t *q  = qtd_align32(q_raw);
    ehci_qh_t  *qh = qh_align32(qh_raw);

    ehci_qtd_init(q, ehci_make_token(t->dir, (uint16_t)t->length, t->data_toggle),
                  t->buffer, t->length);
    q->token |= EHCI_QTD_IOC;

    uint32_t chars = (uint32_t)t->device_addr
                   | ((uint32_t)t->endpoint << 8)
                   | ((uint32_t)((t->speed == USB_SPEED_HIGH) ? 2u
                              : (t->speed == USB_SPEED_FULL) ? 0u : 1u) << 12)
                   | (1u << 14)
                   | ((uint32_t)t->max_packet << 16);
    uint32_t caps  = ((uint32_t)t->tt_hub_addr << 16)
                   | ((uint32_t)t->tt_port << 23)
                   | (1u << 30);
    /* Build our QH as a self-contained one-element async ring with H=1.
     * This way we set ASYNCLISTADDR to our QH and the HC processes it
     * without needing the existing async_head to be in the ring.*/
    qh->horiz_link  = (uint32_t)qh | 0x2u;          /* circular: points to itself */
    qh->ep_chars    = chars | (1u << 15);            /* H-bit: head of reclamation list */
    qh->ep_caps     = caps;
    qh->current_qtd = 0;
    /*
     * Leave the overlay inactive and point Next qTD at the transfer. EHCI
     * advances the queue by loading q into the overlay; seeding an active
     * overlay while Current qTD is zero can make retirement target address 0.
     */
    qh->overlay_next  = (uint32_t)q;
    qh->overlay_alt   = 1u;
    qh->overlay_token = 0;
    for (int i = 0; i < 5; i++) qh->overlay_buf[i] = 0;

    ehci_submit_lock(c);
    if (c->submit_failed) {
        ehci_submit_unlock(c);
        kfree(qh_raw);
        kfree(q_raw);
        return -3;
    }

    /* Stop the async schedule, set ASYNCLISTADDR to our standalone QH ring,
     * then restart. Restoring ASYNCLISTADDR to async_head is done after.*/
    uint32_t usbcmd_save = ehci_op_read(c, EHCI_OP_USBCMD);
    if (!ehci_quiesce_async(c)) {
        KERROR("ehci: could not quiesce async schedule before submit");
        c->submit_failed = true;
        ehci_submit_unlock(c);
        kfree(qh_raw);
        kfree(q_raw);
        return -3;
    }
    ehci_op_write(c, EHCI_OP_ASYNCLISTADDR, (uint32_t)qh);
    ehci_op_write(
        c,
        EHCI_OP_USBCMD,
        usbcmd_save | EHCI_CMD_RUN | EHCI_CMD_ASYNC_EN
    );

    uint32_t timeout_us = timeout_ms > 4294967u
        ? 0xFFFFFFFFu : timeout_ms * 1000u;
    uint32_t waited_us = 0;
    int status = -1;
    for (;;) {
        /*
         * The inactive overlay is not completion evidence before hardware
         * loads q. qTD status write-back is the reliable retirement signal.
         */
        uint32_t tok = *(volatile uint32_t *)&q->token;
        if (!(tok & EHCI_QTD_STATUS_ACTIVE)) {
            status = (tok & EHCI_QTD_STATUS_HALTED) ? -2 : 0;
            break;
        }
        if (waited_us >= timeout_us) break;
        uint32_t delay_us = timeout_us - waited_us;
        if (delay_us > EHCI_POLL_STEP_US) delay_us = EHCI_POLL_STEP_US;
        timer_delay_us(delay_us);
        waited_us += delay_us;
    }

    if (!ehci_quiesce_async(c)) {
        /*
         * Returning would let the caller release a stack or heap buffer that
         * the controller may still own. Leave the descriptors and their
         * caller storage live by stopping here.
         */
        c->submit_failed = true;
        ehci_submit_unlock(c);
        kernel_panic(
            "ehci: DMA ownership could not be revoked after transfer"
        );
    }

    uint32_t completion_token =
        *(volatile uint32_t *)&qh->overlay_token;
    if (completion_token & EHCI_QTD_STATUS_HALTED) status = -2;

    ehci_op_write(c, EHCI_OP_ASYNCLISTADDR, (uint32_t)c->async_head);
    ehci_op_write(c, EHCI_OP_USBCMD, usbcmd_save);
    kfree(qh_raw); kfree(q_raw);
    if (status == 0) {
        /*
         * With QH data-toggle control enabled, hardware leaves the next packet
         * PID in the retired QH overlay. qTD write-back does not promise an
         * updated toggle bit.
         */
        t->data_toggle = (uint8_t)(
            (completion_token & EHCI_QTD_TOGGLE) ? 1u : 0u
        );
    }
    ehci_submit_unlock(c);
    return status;
}

static int ehci_submit_interrupt(usb_hc_t *hc, usb_transfer_t *t, usb_complete_cb_t cb) {
    ehci_ctrl_t *c = (ehci_ctrl_t*)hc->driver_data;
    ehci_submit_lock(c);
    if (c->submit_failed) {
        ehci_submit_unlock(c);
        return -1;
    }
    for (int i = 0; i < EHCI_INT_SLOTS; i++) {
        ehci_int_slot_t *slot = &c->interrupt_slots[i];
        if (usb_interrupt_ownership_publish(&slot->owner) != 0u) {
            slot->t = *t;
            slot->cb = cb;
            ehci_submit_unlock(c);
            return 0;
        }
    }
    ehci_submit_unlock(c);
    return -1;
}
static int ehci_cancel_interrupt(usb_hc_t *hc, usb_transfer_t *t) {
    ehci_ctrl_t *c = (ehci_ctrl_t*)hc->driver_data;
    ehci_int_slot_t *slot = NULL;
    uint32_t generation = 0u;
    ehci_submit_lock(c);
    for (int i = 0; i < EHCI_INT_SLOTS; i++) {
        ehci_int_slot_t *candidate = &c->interrupt_slots[i];
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
    ehci_submit_unlock(c);
    if (!slot || generation == 0u) return -1;

    /*
     * The poller owns the caller's buffer until both DMA and its callback
     * finish. Waiting here makes a successful return a release guarantee.
     */
    while (usb_interrupt_ownership_is_in_flight(&slot->owner)) {
        __asm__ volatile("pause");
    }

    ehci_submit_lock(c);
    if (slot->owner.generation != generation) {
        ehci_submit_unlock(c);
        return 0;
    }
    if (!usb_interrupt_ownership_retire(&slot->owner, generation)) {
        ehci_submit_unlock(c);
        return -1;
    }
    slot->cb = NULL;
    slot->t.buffer = NULL;
    ehci_submit_unlock(c);
    return 0;
}
static int ehci_port_count_fn(usb_hc_t *hc) {
    return ((ehci_ctrl_t*)hc->driver_data)->port_count;
}
static int ehci_port_status(usb_hc_t *hc, int port, uint32_t *status) {
    ehci_ctrl_t *c = (ehci_ctrl_t*)hc->driver_data;
    if (port < 0 || port >= c->port_count) return -1;
    *status = ehci_op_read(c, EHCI_OP_PORTSC(port));
    return 0;
}

static usb_port_reset_result_t ehci_handoff_port(
    ehci_ctrl_t *c,
    int port,
    uint32_t status
) {
    ehci_op_write(c, EHCI_OP_PORTSC(port), status | EHCI_PORTSC_OWNER);
    if (
        (
            ehci_op_read(c, EHCI_OP_PORTSC(port))
            & EHCI_PORTSC_OWNER
        ) == 0u
    ) {
        KWARN("ehci: port %u companion handoff did not latch",
              (uint32_t)port);
        return USB_PORT_RESET_FAILED;
    }
    return USB_PORT_RESET_HANDOFF;
}

static usb_port_reset_result_t ehci_port_reset(
    usb_hc_t *hc,
    int port,
    bool must_reset
) {
    ehci_ctrl_t *c = (ehci_ctrl_t*)hc->driver_data;
    if (port < 0 || port >= c->port_count) {
        return USB_PORT_RESET_FAILED;
    }
    uint32_t v = ehci_op_read(c, EHCI_OP_PORTSC(port));
    if ((v & EHCI_PORTSC_CONNECT) == 0) {
        return USB_PORT_RESET_FAILED;
    }
    /*
     * PORTSC[11:10] reports K-state for a low-speed attachment. J-state is
     * also the idle state for a high-speed-capable attachment, so only
     * K-state may bypass reset. A quarantined address requires a physical
     * reset before handoff.
     */
    uint32_t ls = (v >> 10) & 0x3u;
    if (ls == 0x1u && !must_reset) {
        KINFO("ehci: port %u low-speed device, handing off to companion",
              (uint32_t)port);
        return ehci_handoff_port(c, port, v);
    }
    v &= ~EHCI_PORTSC_ENABLE;
    ehci_op_write(c, EHCI_OP_PORTSC(port), v | EHCI_PORTSC_RESET);
    uint32_t asserted = ehci_op_read(c, EHCI_OP_PORTSC(port));
    if (
        (asserted & EHCI_PORTSC_CONNECT) == 0u
        || (asserted & EHCI_PORTSC_RESET) == 0u
    ) {
        KWARN("ehci: port %u reset did not latch",
              (uint32_t)port);
        return USB_PORT_RESET_FAILED;
    }
    timer_delay_us(50000u);
    ehci_op_write(c, EHCI_OP_PORTSC(port), v);
    bool reset_cleared = false;
    for (int i = 0; i < 100; i++) {
        uint32_t s = ehci_op_read(c, EHCI_OP_PORTSC(port));
        if ((s & EHCI_PORTSC_CONNECT) == 0) {
            return USB_PORT_RESET_FAILED;
        }
        if ((s & EHCI_PORTSC_RESET) == 0u) {
            reset_cleared = true;
            if (s & EHCI_PORTSC_ENABLE) return USB_PORT_RESET_OK;
        }
        timer_delay_us(1000u);
    }
    uint32_t final = ehci_op_read(c, EHCI_OP_PORTSC(port));
    if (
        reset_cleared
        && (final & EHCI_PORTSC_CONNECT) != 0u
        && (final & EHCI_PORTSC_RESET) == 0u
    ) {
        return ehci_handoff_port(c, port, final);
    }
    return USB_PORT_RESET_FAILED;
}
static void ehci_irq_handler_fn(usb_hc_t *hc) {
    ehci_ctrl_t *c = (ehci_ctrl_t*)hc->driver_data;
    uint32_t sts = ehci_op_read(c, EHCI_OP_USBSTS);
    ehci_op_write(c, EHCI_OP_USBSTS, sts);
    if (sts & EHCI_STS_PORT_CH) {
        uint32_t pending = 0;
        for (int p = 0; p < c->port_count; p++) {
            uint32_t s = ehci_op_read(c, EHCI_OP_PORTSC((uint32_t)p));
            if (s & EHCI_PORTSC_CONNECT_CH) {
                ehci_op_write(c, EHCI_OP_PORTSC((uint32_t)p), s | EHCI_PORTSC_CONNECT_CH);
                pending |= 1u << (uint32_t)p;
            }
        }
        if (pending != 0u) {
            __atomic_fetch_or(
                &c->pending_ports,
                pending,
                __ATOMIC_RELEASE
            );
        }
    }
}
void ehci_poll_ports(void);
void ehci_poll_ports(void) {
    for (int i = 0; i < ehci_count; i++) {
        ehci_ctrl_t *c = &ehci_ctrls[i];
        uint32_t pending = __atomic_exchange_n(
            &c->pending_ports,
            0u,
            __ATOMIC_ACQ_REL
        );
        uint32_t retry = 0u;
        for (int port = 0; port < c->port_count; port++) {
            if (pending & (1u << (uint32_t)port)) {
                if (!usb_port_change(&c->hc, port)) {
                    retry |= 1u << (uint32_t)port;
                }
            }
        }
        if (retry != 0u) {
            /*
             * The IRQ already acknowledged the controller's change bit.
             * Keep rejected work in the software bitmap until the queue has
             * room, otherwise this edge would disappear.
             */
            __atomic_fetch_or(
                &c->pending_ports,
                retry,
                __ATOMIC_RELEASE
            );
        }
    }
}
void ehci_poll_interrupts(void);
void ehci_poll_interrupts(void) {
    for (int controller = 0; controller < ehci_count; controller++) {
        ehci_ctrl_t *c = &ehci_ctrls[controller];
        for (int i = 0; i < EHCI_INT_SLOTS; i++) {
            ehci_int_slot_t *slot = &c->interrupt_slots[i];
            usb_transfer_t local;
            usb_complete_cb_t cb = NULL;
            uint32_t generation = 0u;

            ehci_submit_lock(c);
            if (!c->submit_failed) {
                generation = usb_interrupt_ownership_claim(&slot->owner);
                if (generation != 0u) {
                    local = slot->t;
                    cb = slot->cb;
                }
            }
            ehci_submit_unlock(c);
            if (generation == 0u) continue;

            int r = ehci_submit_sync(&c->hc, &local, 5);
            bool deliver = false;
            ehci_submit_lock(c);
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
            ehci_submit_unlock(c);

            if (deliver) cb(0, &local);

            ehci_submit_lock(c);
            usb_interrupt_ownership_finish(&slot->owner, generation);
            ehci_submit_unlock(c);
        }
    }
}
