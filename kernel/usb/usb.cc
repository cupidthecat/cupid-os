#include "usb.h"
#include "memory.h"
#include "irq.h"
#include "isr.h"
#include "serial.h"
#include "timer.h"

#define USB_CONTROL_ATTEMPTS 5u
#define USB_CONTROL_RETRY_US 10000u
#define USB_HUB_RESET_SETTLE_US 50000u
#define USB_WORK_RETRY_MIN_MS 10u
#define USB_WORK_RETRY_MAX_MS 1000u

#define USB_HUB_GET_STATUS        0x00u
#define USB_HUB_CLEAR_FEATURE     0x01u
#define USB_HUB_SET_FEATURE       0x03u
#define USB_HUB_PORT_CONNECTION   0u
#define USB_HUB_PORT_ENABLE       1u
#define USB_HUB_PORT_RESET        4u
#define USB_HUB_C_PORT_CONNECTION 16u
#define USB_HUB_C_PORT_RESET      20u

static usb_hc_t     *hcs[USB_MAX_HCS];
static int           hc_count = 0;

static usb_driver_t *driver_list = NULL;

static usb_device_t  devices[USB_MAX_DEVICES];
static bool          usb_address_reserved[128];
static uint8_t       usb_address_hint = 1u;

typedef struct {
    usb_hc_t     *hc;          /* non-null for root-port change */
    usb_device_t *hub;         /* non-null for hub-port change */
    uint32_t      hub_generation;
    int           port;
    bool          reconciled;
    bool          reconciled_connected;
    bool          state_observed;
    bool          last_connected;
    uint8_t       quarantined_address;
    uint32_t      retry_delay_ms;
    uint32_t      retry_after_ms;
} usb_work_t;

typedef enum {
    USB_WORK_COMPLETE = 0,
    USB_WORK_RETRY = 1
} usb_work_result_t;

static usb_work_t workq[USB_WORKQ_SIZE];
static volatile uint32_t workq_head = 0;
static volatile uint32_t workq_tail = 0;
static volatile uint32_t usb_poll_active = 0;

void usb_init(void) {
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        devices[i].generation = 0;
        devices[i].in_use = false;
    }
    for (int i = 0; i < 128; i++) usb_address_reserved[i] = false;
    usb_address_hint = 1u;
    hc_count = 0;
    driver_list = NULL;
    workq_head = 0;
    workq_tail = 0;
    usb_poll_active = 0;
    KINFO("usb: core initialized");
}

void usb_poll(void) {
    /*
     * kernel_check_reschedule() can run on more than one CPU. Folding
     * overlapping calls into one poll protects the interrupt slots, device
     * table, and work queue from two cooperative consumers.
     */
    if (
        __atomic_exchange_n(
            &usb_poll_active,
            1u,
            __ATOMIC_ACQUIRE
        ) != 0u
    ) {
        return;
    }

    extern void ehci_poll_interrupts(void);
    extern void ehci_poll_ports(void);
    extern void uhci_poll_ports(void);
    extern void uhci_poll_interrupts(void);
    ehci_poll_ports();
    ehci_poll_interrupts();
    uhci_poll_ports();
    uhci_poll_interrupts();
    usb_process_pending();

    __atomic_store_n(&usb_poll_active, 0u, __ATOMIC_RELEASE);
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

static bool workq_push(usb_work_t w) {
    /*
     * A port event describes work to reconcile against current hardware
     * state, so a queued event for the same port already covers another edge.
     */
    for (
        uint32_t cursor = workq_tail;
        cursor != workq_head;
        cursor = (cursor + 1u) % USB_WORKQ_SIZE
    ) {
        usb_work_t *queued = &workq[cursor];
        if (
            queued->hc == w.hc
            && queued->hub == w.hub
            && queued->hub_generation == w.hub_generation
            && queued->port == w.port
        ) {
            /*
             * Root controller change bits are cleared only after this handoff,
             * so another root handoff is a new edge. Wake a backed-off
             * reconciliation to observe it. Hub interrupt bitmaps repeat until
             * C_PORT_CONNECTION is cleared and must not defeat backoff.
             */
            if (w.hc) {
                queued->retry_delay_ms = 0u;
                queued->retry_after_ms = 0u;
                queued->state_observed = false;
            }
            return true;
        }
    }

    uint32_t next = (workq_head + 1u) % USB_WORKQ_SIZE;
    if (next == workq_tail) {
        KWARN("usb: work queue full, deferring port event");
        return false;
    }
    workq[workq_head] = w;
    workq_head = next;
    return true;
}

bool usb_port_change(usb_hc_t *hc, int port) {
    usb_work_t w = {
        hc, NULL, 0u, port, false, false, false, false, 0u, 0u, 0u
    };
    return workq_push(w);
}

bool usb_hub_port_change(usb_device_t *hub, int port) {
    usb_work_t w = {
        NULL,
        hub,
        hub ? hub->generation : 0u,
        port,
        false,
        false,
        false,
        false,
        0u,
        0u,
        0u
    };
    return workq_push(w);
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

int usb_control_retry(
    usb_device_t *dev,
    uint8_t bmRequestType,
    uint8_t bRequest,
    uint16_t wValue,
    uint16_t wIndex,
    void *data,
    uint16_t wLength
) {
    int result = -1;
    for (uint32_t attempt = 0; attempt < USB_CONTROL_ATTEMPTS; attempt++) {
        result = usb_control(
            dev,
            bmRequestType,
            bRequest,
            wValue,
            wIndex,
            data,
            wLength
        );
        if (result >= 0) return result;
        if (attempt + 1u < USB_CONTROL_ATTEMPTS) {
            timer_delay_us(USB_CONTROL_RETRY_US);
        }
    }
    return result;
}

static int usb_assign_address(
    usb_device_t *dev,
    uint8_t address,
    uint8_t descriptor[18]
) {
    for (uint32_t attempt = 0; attempt < USB_CONTROL_ATTEMPTS; attempt++) {
        dev->address = 0;
        int result = usb_control(
            dev,
            0x00,
            0x05,
            address,
            0,
            NULL,
            0
        );
        dev->address = address;
        timer_delay_us(USB_CONTROL_RETRY_US);
        if (result >= 0) return 0;

        /*
         * A device adopts its new address after the status stage. If the
         * controller lost that completion, probing the new address tells us
         * whether the request actually succeeded.
         */
        if (
            usb_control(
                dev,
                0x80,
                0x06,
                (uint16_t)(0x01 << 8),
                0,
                descriptor,
                8
            ) >= 0
        ) {
            return 0;
        }
        timer_delay_us(USB_CONTROL_RETRY_US);
    }
    /*
     * Keep the reserved address attached to the device slot until the caller
     * unwinds it. The next reconciliation resets the physical port before
     * reusing the slot.
     */
    dev->address = address;
    return -1;
}

static int usb_set_configuration(
    usb_device_t *dev,
    uint8_t configuration
) {
    for (uint32_t attempt = 0; attempt < USB_CONTROL_ATTEMPTS; attempt++) {
        int result = usb_control(
            dev,
            0x00,
            0x09,
            configuration,
            0,
            NULL,
            0
        );
        if (result >= 0) return 0;

        /*
         * SET_CONFIGURATION takes effect before its status stage completes.
         * If that completion was lost, query the active configuration before
         * repeating a request the device has already applied.
         */
        uint8_t active = 0;
        if (
            usb_control(
                dev,
                0x80,
                0x08,
                0,
                0,
                &active,
                1
            ) >= 0
            && active == configuration
        ) {
            return 0;
        }
        if (attempt + 1u < USB_CONTROL_ATTEMPTS) {
            timer_delay_us(USB_CONTROL_RETRY_US);
        }
    }
    return -1;
}

static uint8_t alloc_address(void) {
    for (uint32_t checked = 0; checked < 127u; checked++) {
        uint8_t address = usb_address_hint;
        usb_address_hint++;
        if (usb_address_hint == 0u || usb_address_hint > 127u) {
            usb_address_hint = 1u;
        }
        if (!usb_address_reserved[address]) {
            usb_address_reserved[address] = true;
            return address;
        }
    }
    return 0;
}

static void release_address(uint8_t address) {
    if (address > 0u && address <= 127u) {
        usb_address_reserved[address] = false;
    }
}

static void clear_device_slot(usb_device_t *dev) {
    if (!dev) return;
    dev->address = 0;
    dev->driver_data = NULL;
    dev->driver = NULL;
    dev->parent_hub = NULL;
    dev->in_use = false;
}

static void release_device_slot(usb_device_t *dev) {
    if (!dev) return;
    release_address(dev->address);
    clear_device_slot(dev);
}

static void unwind_enumeration(
    usb_work_t *w,
    usb_device_t *dev
) {
    if (dev->address != 0u) {
        w->quarantined_address = dev->address;
        clear_device_slot(dev);
    } else {
        release_device_slot(dev);
    }
}

static void release_work_quarantine(usb_work_t *w) {
    release_address(w->quarantined_address);
    w->quarantined_address = 0u;
}

static void reset_work_backoff(usb_work_t *w) {
    w->retry_delay_ms = 0u;
    w->retry_after_ms = 0u;
}

static void observe_port_state(usb_work_t *w, bool connected) {
    if (!w->state_observed || w->last_connected != connected) {
        reset_work_backoff(w);
    }
    w->state_observed = true;
    w->last_connected = connected;
}

static void defer_work(usb_work_t *w) {
    uint32_t delay = w->retry_delay_ms;
    if (delay == 0u) {
        delay = USB_WORK_RETRY_MIN_MS;
    } else if (delay < USB_WORK_RETRY_MAX_MS) {
        delay *= 2u;
        if (delay > USB_WORK_RETRY_MAX_MS) {
            delay = USB_WORK_RETRY_MAX_MS;
        }
    }
    w->retry_delay_ms = delay;
    w->retry_after_ms = timer_get_uptime_ms() + delay;
}

static bool work_retry_ready(const usb_work_t *w, uint32_t now) {
    return w->retry_after_ms == 0u
        || (int32_t)(now - w->retry_after_ms) >= 0;
}

static usb_device_t *alloc_device_slot(void) {
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (!devices[i].in_use) {
            devices[i].generation++;
            if (devices[i].generation == 0u) devices[i].generation = 1u;
            devices[i].driver_data = NULL;
            devices[i].driver = NULL;
            devices[i].address = 0;
            devices[i].parent_hub = NULL;
            devices[i].in_use = true;
            return &devices[i];
        }
    }
    return NULL;
}

static usb_device_t *find_root_device(usb_hc_t *hc, int port) {
    int count = usb_device_count();
    for (int i = 0; i < count; i++) {
        usb_device_t *dev = usb_get_device(i);
        if (
            dev
            && dev->hc == hc
            && dev->parent_hub == NULL
            && dev->parent_port == (uint8_t)port
        ) {
            return dev;
        }
    }
    return NULL;
}

static int remove_root_device(usb_hc_t *hc, int port) {
    usb_device_t *dev = find_root_device(hc, port);
    return dev ? usb_device_remove(dev) : 0;
}

static int remove_hub_port_devices(usb_device_t *hub, int port) {
    for (;;) {
        usb_device_t *child = NULL;
        int count = usb_device_count();
        for (int i = 0; i < count; i++) {
            usb_device_t *dev = usb_get_device(i);
            if (
                dev
                && dev->parent_hub == hub
                && dev->parent_port == (uint8_t)port
            ) {
                child = dev;
                break;
            }
        }
        if (!child) return 0;
        if (usb_device_remove(child) < 0) return -1;
    }
}

static int hub_port_status(
    usb_device_t *hub,
    int port,
    uint16_t *status,
    uint16_t *change
) {
    uint8_t data[4] = {0};
    if (
        usb_control_retry(
            hub,
            0xA3,
            USB_HUB_GET_STATUS,
            0,
            (uint16_t)port,
            data,
            4
        ) < 0
    ) {
        return -1;
    }
    *status = (uint16_t)(data[0] | (uint16_t)(data[1] << 8));
    *change = (uint16_t)(data[2] | (uint16_t)(data[3] << 8));
    return 0;
}

static int hub_clear_port_feature(
    usb_device_t *hub,
    int port,
    uint16_t feature
) {
    return usb_control_retry(
        hub,
        0x23,
        USB_HUB_CLEAR_FEATURE,
        feature,
        (uint16_t)port,
        NULL,
        0
    );
}

static int hub_reset_port(
    usb_device_t *hub,
    int port,
    uint16_t *status
) {
    if (
        usb_control_retry(
            hub,
            0x23,
            USB_HUB_SET_FEATURE,
            USB_HUB_PORT_RESET,
            (uint16_t)port,
            NULL,
            0
        ) < 0
    ) {
        return -1;
    }

    timer_delay_us(USB_HUB_RESET_SETTLE_US);

    uint16_t change = 0;
    if (
        hub_port_status(hub, port, status, &change) < 0
        || (*status & (1u << USB_HUB_PORT_CONNECTION)) == 0u
        || (change & (1u << (USB_HUB_C_PORT_RESET - 16u))) == 0u
        || hub_clear_port_feature(
            hub,
            port,
            USB_HUB_C_PORT_RESET
        ) < 0
    ) {
        return -1;
    }
    return 0;
}

static usb_work_result_t enumerate_device(
    usb_work_t *w,
    uint32_t status
) {
    usb_device_t *pHub = w->hub;
    usb_hc_t *hc = w->hc ? w->hc : pHub->hc;
    int port = w->port;
    uint8_t depth = pHub ? (uint8_t)(pHub->hub_depth + 1u) : 0u;

    usb_device_t *dev = alloc_device_slot();
    if (!dev) {
        KWARN("usb: no free device slot");
        return USB_WORK_RETRY;
    }
    dev->hc = hc;
    dev->parent_hub = pHub;
    dev->parent_port = (uint8_t)port;
    dev->hub_depth = depth;
    dev->speed = USB_SPEED_FULL;
    if (pHub) {
        if (status & (1u << 10)) {
            dev->speed = USB_SPEED_HIGH;
        } else if (status & (1u << 9)) {
            dev->speed = USB_SPEED_LOW;
        }
    } else {
        dev->speed = hc->root_speed;
    }

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
    if (
        usb_control_retry(
            dev,
            0x80,
            0x06,
            (uint16_t)(0x01 << 8),
            0,
            desc,
            8
        ) < 0
    ) {
        KWARN("usb: first GET_DESC failed");
        unwind_enumeration(w, dev);
        return USB_WORK_RETRY;
    }
    dev->max_packet_ep0 = desc[7];
    if (dev->max_packet_ep0 == 0) dev->max_packet_ep0 = 8;

    uint8_t addr = alloc_address();
    if (addr == 0) {
        KERROR("usb: address space exhausted");
        unwind_enumeration(w, dev);
        return USB_WORK_RETRY;
    }
    dev->address = addr;
    if (usb_assign_address(dev, addr, desc) < 0) {
        KWARN("usb: SET_ADDRESS failed");
        unwind_enumeration(w, dev);
        return USB_WORK_RETRY;
    }

    if (
        usb_control_retry(
            dev,
            0x80,
            0x06,
            (uint16_t)(0x01 << 8),
            0,
            desc,
            18
        ) < 0
    ) {
        KWARN("usb: full GET_DESC failed");
        unwind_enumeration(w, dev);
        return USB_WORK_RETRY;
    }
    dev->class_code = desc[4];
    dev->subclass   = desc[5];
    dev->protocol   = desc[6];
    dev->vendor_id  = (uint16_t)(desc[8]  | (desc[9]  << 8));
    dev->product_id = (uint16_t)(desc[10] | (desc[11] << 8));

    uint8_t cfg[255] = {0};
    if (
        usb_control_retry(
            dev,
            0x80,
            0x06,
            (uint16_t)(0x02 << 8),
            0,
            cfg,
            9
        ) < 0
    ) {
        KWARN("usb: GET_CONFIG(short) failed");
        unwind_enumeration(w, dev);
        return USB_WORK_RETRY;
    }
    uint16_t total = (uint16_t)(cfg[2] | (cfg[3] << 8));
    if (total < 9u) {
        KWARN("usb: malformed configuration descriptor");
        unwind_enumeration(w, dev);
        return USB_WORK_RETRY;
    }
    if (total > sizeof(cfg)) total = (uint16_t)sizeof(cfg);
    if (
        usb_control_retry(
            dev,
            0x80,
            0x06,
            (uint16_t)(0x02 << 8),
            0,
            cfg,
            total
        ) < 0
    ) {
        KWARN("usb: GET_CONFIG(full) failed");
        unwind_enumeration(w, dev);
        return USB_WORK_RETRY;
    }
    if (total >= 9 + 9 && dev->class_code == 0) {
        uint8_t *iface = &cfg[9];
        dev->class_code = iface[5];
        dev->subclass   = iface[6];
        dev->protocol   = iface[7];
    }

    if (usb_set_configuration(dev, cfg[5]) < 0) {
        KWARN("usb: SET_CONFIGURATION failed");
        unwind_enumeration(w, dev);
        return USB_WORK_RETRY;
    }

    KINFO("usb: dev addr=%u speed=%u vid=%x pid=%x class=%x",
          dev->address, dev->speed, dev->vendor_id, dev->product_id, dev->class_code);

    for (usb_driver_t *d = driver_list; d; d = d->next) {
        usb_probe_result_t probe_result = d->probe(dev);
        if (probe_result == USB_PROBE_BOUND) {
            dev->driver = d;
            return USB_WORK_COMPLETE;
        }
        if (probe_result == USB_PROBE_RETRY) {
            KWARN("usb: driver '%s' requested a probe retry", d->name);
            unwind_enumeration(w, dev);
            return USB_WORK_RETRY;
        }
        if (probe_result == USB_PROBE_REJECTED) {
            KWARN("usb: driver '%s' rejected dev addr=%u",
                  d->name, dev->address);
            return USB_WORK_COMPLETE;
        }
        if (probe_result != USB_PROBE_NOT_SUPPORTED) {
            KWARN("usb: driver '%s' returned invalid probe result %d",
                  d->name, (int)probe_result);
            unwind_enumeration(w, dev);
            return USB_WORK_RETRY;
        }
    }
    KINFO("usb: no driver for dev addr=%u class=%x", dev->address, dev->class_code);
    return USB_WORK_COMPLETE;
}

static usb_work_result_t reconcile_root_port(usb_work_t *w) {
    usb_hc_t *hc = w->hc;
    int port = w->port;
    uint32_t status = 0;
    if (hc->port_status(hc, port, &status) < 0) {
        return USB_WORK_RETRY;
    }
    bool connected = (status & 0x1u) != 0u;
    observe_port_state(w, connected);
    if (!connected) {
        if (remove_root_device(hc, port) < 0) {
            return USB_WORK_RETRY;
        }
        release_work_quarantine(w);
        return USB_WORK_COMPLETE;
    }

    if (remove_root_device(hc, port) < 0) {
        KERROR("usb: connected root port still owns its old device");
        return USB_WORK_RETRY;
    }
    if (
        hc->port_status(hc, port, &status) < 0
    ) {
        return USB_WORK_RETRY;
    }
    connected = (status & 0x1u) != 0u;
    observe_port_state(w, connected);
    if (!connected) {
        release_work_quarantine(w);
        return USB_WORK_COMPLETE;
    }
    bool must_reset = w->quarantined_address != 0u;
    usb_port_reset_result_t reset_result = hc->port_reset(
        hc,
        port,
        must_reset
    );
    if (reset_result == USB_PORT_RESET_HANDOFF) {
        release_work_quarantine(w);
        return USB_WORK_COMPLETE;
    }
    if (reset_result != USB_PORT_RESET_OK) {
        KWARN("usb: root port reset failed");
        return USB_WORK_RETRY;
    }
    release_work_quarantine(w);
    if (hc->port_status(hc, port, &status) < 0) {
        return USB_WORK_RETRY;
    }
    connected = (status & 0x1u) != 0u;
    observe_port_state(w, connected);
    if (!connected) return USB_WORK_COMPLETE;
    return enumerate_device(w, status);
}

static usb_work_result_t reconcile_hub_port(usb_work_t *w) {
    usb_device_t *hub = w->hub;
    int port = w->port;
    uint16_t status = 0;
    uint16_t change = 0;
    if (hub_port_status(hub, port, &status, &change) < 0) {
        return USB_WORK_RETRY;
    }

    bool connected =
        (status & (1u << USB_HUB_PORT_CONNECTION)) != 0u;
    observe_port_state(w, connected);
    if (!w->reconciled || w->reconciled_connected != connected) {
        if (remove_hub_port_devices(hub, port) < 0) {
            KERROR("usb: hub child teardown failed on port %d", port);
            return USB_WORK_RETRY;
        }

        if (connected) {
            if (hub->hub_depth + 1u > USB_MAX_HUB_DEPTH) {
                KWARN("usb: hub depth %u exceeded, disabling port",
                      (uint32_t)(hub->hub_depth + 1u));
                if (
                    hub_clear_port_feature(
                        hub,
                        port,
                        USB_HUB_PORT_ENABLE
                    ) < 0
                ) {
                    return USB_WORK_RETRY;
                }
                release_work_quarantine(w);
            } else {
                if (hub_reset_port(hub, port, &status) < 0) {
                    KWARN("usb: hub port reset failed");
                    return USB_WORK_RETRY;
                }
                release_work_quarantine(w);
                usb_work_result_t result = enumerate_device(w, status);
                if (result != USB_WORK_COMPLETE) return result;
            }
        } else {
            release_work_quarantine(w);
        }
        w->reconciled = true;
        w->reconciled_connected = connected;
    }

    /*
     * Re-read before acknowledging. If the physical state changed while
     * enumeration ran, keep the same durable work item and reconcile the new
     * state on the next poll.
     */
    uint16_t current_status = 0;
    uint16_t current_change = 0;
    if (
        hub_port_status(
            hub,
            port,
            &current_status,
            &current_change
        ) < 0
    ) {
        return USB_WORK_RETRY;
    }
    bool current_connected =
        (current_status & (1u << USB_HUB_PORT_CONNECTION)) != 0u;
    observe_port_state(w, current_connected);
    if (current_connected != w->reconciled_connected) {
        w->reconciled = false;
        return USB_WORK_RETRY;
    }
    if ((current_change & (1u << USB_HUB_PORT_CONNECTION)) != 0u) {
        if (
            hub_clear_port_feature(
                hub,
                port,
                USB_HUB_C_PORT_CONNECTION
            ) < 0
        ) {
            return USB_WORK_RETRY;
        }

        uint16_t verified_status = 0;
        uint16_t verified_change = 0;
        if (
            hub_port_status(
                hub,
                port,
                &verified_status,
                &verified_change
            ) < 0
        ) {
            return USB_WORK_RETRY;
        }
        bool verified_connected =
            (verified_status & (1u << USB_HUB_PORT_CONNECTION)) != 0u;
        observe_port_state(w, verified_connected);
        if (
            verified_connected != w->reconciled_connected
            || (
                verified_change
                & (1u << USB_HUB_PORT_CONNECTION)
            ) != 0u
        ) {
            w->reconciled = false;
            return USB_WORK_RETRY;
        }
    }
    return USB_WORK_COMPLETE;
}

static usb_work_result_t enumerate_port(usb_work_t *w) {
    usb_device_t *pHub = w->hub;
    if (
        pHub
        && (
            !pHub->in_use
            || pHub->generation != w->hub_generation
        )
    ) {
        KDEBUG("usb: dropped stale hub work");
        release_work_quarantine(w);
        return USB_WORK_COMPLETE;
    }
    return pHub ? reconcile_hub_port(w) : reconcile_root_port(w);
}

void usb_process_pending(void) {
    uint32_t pending = (
        workq_head + USB_WORKQ_SIZE - workq_tail
    ) % USB_WORKQ_SIZE;
    while (pending > 0u) {
        usb_work_t w = workq[workq_tail];
        pending--;
        bool attempted = work_retry_ready(
            &w,
            timer_get_uptime_ms()
        );
        usb_work_result_t result = attempted
            ? enumerate_port(&w)
            : USB_WORK_RETRY;
        if (result == USB_WORK_COMPLETE) {
            workq_tail = (workq_tail + 1u) % USB_WORKQ_SIZE;
        } else {
            if (attempted) defer_work(&w);
            /*
             * Keep the in-flight entry live until its replacement is stored.
             * Reentrant producers therefore see it during reconciliation and
             * cannot consume the ring's sentinel slot. Moving it behind the
             * other work gives every port one attempt per poll.
             */
            workq[workq_head] = w;
            workq_head = (workq_head + 1u) % USB_WORKQ_SIZE;
            workq_tail = (workq_tail + 1u) % USB_WORKQ_SIZE;
        }
    }
}

int usb_device_remove(usb_device_t *dev) {
    if (!dev || !dev->in_use) return -1;
    uint8_t address = dev->address;
    if (
        dev->driver
        && dev->driver->disconnect
        && dev->driver->disconnect(dev) < 0
    ) {
        KERROR("usb: driver refused removal for addr=%u", address);
        return -1;
    }
    release_device_slot(dev);
    KINFO("usb: removed device addr=%u", address);
    return 0;
}

/* Shared USB IRQ dispatcher: calls irq_handler on every registered HC.
 * Installed once per IRQ line via usb_register_irq(). Supports up to 16
 * IRQ lines tracked via bitmask.*/
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
