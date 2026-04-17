#include "usb.h"
#include "memory.h"
#include "irq.h"
#include "isr.h"
#include "../drivers/serial.h"

static usb_hc_t     *hcs[USB_MAX_HCS];
static int           hc_count = 0;

static usb_driver_t *driver_list = NULL;

static usb_device_t  devices[USB_MAX_DEVICES];

typedef struct {
    usb_hc_t     *hc;          /* non-null for root-port change */
    usb_device_t *hub;         /* non-null for hub-port change  */
    int           port;
} usb_work_t;

static usb_work_t workq[USB_WORKQ_SIZE];
static volatile uint32_t workq_head = 0;
static volatile uint32_t workq_tail = 0;

void usb_init(void) {
    for (int i = 0; i < USB_MAX_DEVICES; i++) devices[i].in_use = false;
    hc_count = 0;
    driver_list = NULL;
    workq_head = 0;
    workq_tail = 0;
    KINFO("usb: core initialized");
}

int usb_register_hc(usb_hc_t *hc) {
    if (hc_count >= USB_MAX_HCS) return -1;
    hcs[hc_count++] = hc;
    KINFO("usb: registered HC '%s'", hc->name);
    return 0;
}

int usb_register_driver(usb_driver_t *d) {
    d->next = driver_list;
    driver_list = d;
    KINFO("usb: registered driver '%s'", d->name);
    return 0;
}

static void workq_push(usb_work_t w) {
    uint32_t next = (workq_head + 1u) % USB_WORKQ_SIZE;
    if (next == workq_tail) {
        KWARN("usb: work queue full, dropping event");
        return;
    }
    workq[workq_head] = w;
    workq_head = next;
}

void usb_port_change(usb_hc_t *hc, int port) {
    usb_work_t w = { hc, NULL, port };
    workq_push(w);
}

void usb_hub_port_change(usb_device_t *hub, int port) {
    usb_work_t w = { NULL, hub, port };
    workq_push(w);
}

int usb_device_count(void) {
    int n = 0;
    for (int i = 0; i < USB_MAX_DEVICES; i++) if (devices[i].in_use) n++;
    return n;
}

usb_device_t *usb_get_device(int index) {
    int n = 0;
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (!devices[i].in_use) continue;
        if (n == index) return &devices[i];
        n++;
    }
    return NULL;
}

typedef struct __attribute__((packed)) {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_setup_t;

uint8_t usb_device_class(int index) {
    usb_device_t *d = usb_get_device(index);
    return d ? d->class_code : 0u;
}

int usb_control(usb_device_t *dev, uint8_t bmRequestType, uint8_t bRequest,
                uint16_t wValue, uint16_t wIndex, void *data, uint16_t wLength) {
    usb_setup_t setup = { bmRequestType, bRequest, wValue, wIndex, wLength };
    usb_transfer_t t;

    /* SETUP stage */
    t.dir = USB_DIR_SETUP; t.endpoint = 0; t.device_addr = dev->address;
    t.max_packet = dev->max_packet_ep0; t.speed = dev->speed;
    t.data_toggle = 0; t.buffer = (uint8_t*)&setup; t.length = sizeof(setup);
    t.tt_hub_addr = dev->tt_hub_addr; t.tt_port = dev->tt_port;
    int r = dev->hc->submit_sync(dev->hc, &t, 500);
    if (r < 0) return r;

    /* DATA stage (optional) */
    if (wLength > 0 && data) {
        t.dir = (uint8_t)((bmRequestType & 0x80u) ? USB_DIR_IN : USB_DIR_OUT);
        t.endpoint = 0;
        t.data_toggle = 1;
        t.buffer = (uint8_t*)data;
        t.length = wLength;
        r = dev->hc->submit_sync(dev->hc, &t, 500);
        if (r < 0) return r;
    }

    /* STATUS stage (opposite direction, zero length) */
    t.dir = (uint8_t)((bmRequestType & 0x80u) ? USB_DIR_OUT : USB_DIR_IN);
    t.endpoint = 0; t.data_toggle = 1;
    t.buffer = NULL; t.length = 0;
    return dev->hc->submit_sync(dev->hc, &t, 500);
}

static uint8_t next_usb_addr = 1;

static uint8_t alloc_address(void) {
    if (next_usb_addr == 0 || next_usb_addr > 127) return 0;
    return next_usb_addr++;
}

static usb_device_t *alloc_device_slot(void) {
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (!devices[i].in_use) { devices[i].in_use = true; return &devices[i]; }
    }
    return NULL;
}

static void enumerate_port(usb_work_t w) {
    usb_hc_t     *hc    = w.hc    ? w.hc    : w.hub->hc;
    usb_device_t *pHub  = w.hub;
    int           port  = w.port;
    uint8_t       depth = pHub ? (uint8_t)(pHub->hub_depth + 1) : 0u;

    if (depth > USB_MAX_HUB_DEPTH) {
        KWARN("usb: hub depth %u exceeded, skipping port", depth);
        return;
    }

    uint32_t status = 0;
    if (!pHub) {
        if (hc->port_reset(hc, port) < 0) { KWARN("usb: root port reset failed"); return; }
        hc->port_status(hc, port, &status);
        if ((status & 0x1u) == 0u) return;
    }

    usb_device_t *dev = alloc_device_slot();
    if (!dev) { KWARN("usb: no free device slot"); return; }
    dev->hc = hc;
    dev->parent_hub = pHub;
    dev->parent_port = (uint8_t)port;
    dev->hub_depth = depth;
    dev->address = 0;
    dev->speed = (pHub && pHub->class_code == 0x09)
                 ? USB_SPEED_FULL
                 : ((status & (1u << 8)) ? USB_SPEED_LOW : USB_SPEED_FULL);
    if (!pHub) dev->speed = hc->root_speed;

    dev->max_packet_ep0 = 8;

    if (pHub && pHub->speed == USB_SPEED_HIGH && dev->speed != USB_SPEED_HIGH) {
        dev->tt_hub_addr = pHub->address;
        dev->tt_port = (uint8_t)port;
    } else if (pHub) {
        dev->tt_hub_addr = pHub->tt_hub_addr;
        dev->tt_port = pHub->tt_port;
    } else {
        dev->tt_hub_addr = 0;
        dev->tt_port = 0;
    }

    uint8_t desc[18] = {0};
    if (usb_control(dev, 0x80, 0x06, (uint16_t)(0x01 << 8), 0, desc, 8) < 0) {
        KWARN("usb: first GET_DESC failed"); dev->in_use = false; return;
    }
    dev->max_packet_ep0 = desc[7];
    if (dev->max_packet_ep0 == 0) dev->max_packet_ep0 = 8;

    uint8_t addr = alloc_address();
    if (addr == 0) { KERROR("usb: address space exhausted"); dev->in_use = false; return; }
    if (usb_control(dev, 0x00, 0x05, addr, 0, NULL, 0) < 0) {
        KWARN("usb: SET_ADDRESS failed"); dev->in_use = false; return;
    }
    dev->address = addr;

    if (usb_control(dev, 0x80, 0x06, (uint16_t)(0x01 << 8), 0, desc, 18) < 0) {
        KWARN("usb: full GET_DESC failed"); dev->in_use = false; return;
    }
    dev->class_code = desc[4];
    dev->subclass   = desc[5];
    dev->protocol   = desc[6];
    dev->vendor_id  = (uint16_t)(desc[8]  | (desc[9]  << 8));
    dev->product_id = (uint16_t)(desc[10] | (desc[11] << 8));

    uint8_t cfg[255] = {0};
    if (usb_control(dev, 0x80, 0x06, (uint16_t)(0x02 << 8), 0, cfg, 9) < 0) {
        KWARN("usb: GET_CONFIG(short) failed"); dev->in_use = false; return;
    }
    uint16_t total = (uint16_t)(cfg[2] | (cfg[3] << 8));
    if (total > sizeof(cfg)) total = (uint16_t)sizeof(cfg);
    if (usb_control(dev, 0x80, 0x06, (uint16_t)(0x02 << 8), 0, cfg, total) < 0) {
        KWARN("usb: GET_CONFIG(full) failed"); dev->in_use = false; return;
    }
    if (total >= 9 + 9 && dev->class_code == 0) {
        uint8_t *iface = &cfg[9];
        dev->class_code = iface[5];
        dev->subclass   = iface[6];
        dev->protocol   = iface[7];
    }

    (void)usb_control(dev, 0x00, 0x09, cfg[5], 0, NULL, 0);

    KINFO("usb: dev addr=%u speed=%u vid=%x pid=%x class=%x",
          dev->address, dev->speed, dev->vendor_id, dev->product_id, dev->class_code);

    for (usb_driver_t *d = driver_list; d; d = d->next) {
        if (d->probe(dev) == 0) { dev->driver = d; return; }
    }
    KINFO("usb: no driver for dev addr=%u class=%x", dev->address, dev->class_code);
}

void usb_process_pending(void) {
    while (workq_tail != workq_head) {
        usb_work_t w = workq[workq_tail];
        workq_tail = (workq_tail + 1u) % USB_WORKQ_SIZE;
        enumerate_port(w);
    }
}

void usb_device_remove(usb_device_t *dev) {
    if (!dev || !dev->in_use) return;
    if (dev->driver && dev->driver->disconnect) dev->driver->disconnect(dev);
    dev->in_use = false;
    KINFO("usb: removed device addr=%u", dev->address);
}

/* Shared USB IRQ dispatcher: calls irq_handler on every registered HC.
 * Installed once per IRQ line via usb_register_irq(). Supports up to 16
 * IRQ lines tracked via bitmask. */
static uint16_t usb_irq_mask = 0u; /* bitmask of installed IRQ lines */

static void usb_irq_dispatch(struct registers *r) {
    (void)r;
    for (int i = 0; i < hc_count; i++) {
        if (hcs[i]->irq_handler) hcs[i]->irq_handler(hcs[i]);
    }
}

void usb_register_irq(uint8_t irq) {
    if (irq >= 16u) return;
    if (usb_irq_mask & (uint16_t)(1u << irq)) return; /* already installed */
    usb_irq_mask = (uint16_t)(usb_irq_mask | (uint16_t)(1u << irq));
    irq_install_handler((int)irq, usb_irq_dispatch);
    KINFO("usb: shared IRQ %u dispatcher installed", (uint32_t)irq);
}
