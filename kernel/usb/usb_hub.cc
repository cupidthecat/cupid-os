#include "usb.h"
#include "memory.h"
#include "serial.h"

#define HUB_CLASS 0x09
#define HUB_CONFIG_BUFFER_SIZE 255u
#define HUB_CHANGE_BUFFER_SIZE 8u

#define USB_DESC_CONFIGURATION 0x02u
#define USB_DESC_INTERFACE     0x04u
#define USB_DESC_ENDPOINT      0x05u
#define USB_ENDPOINT_IN        0x80u
#define USB_ENDPOINT_NUMBER    0x0Fu
#define USB_ENDPOINT_TYPE_MASK 0x03u
#define USB_ENDPOINT_INTERRUPT 0x03u

/* Hub class requests (wIndex = port number, 1-indexed). */
#define HUB_GET_STATUS         0x00
#define HUB_CLEAR_FEATURE      0x01
#define HUB_SET_FEATURE        0x03
#define HUB_GET_DESCRIPTOR     0x06

/* Port feature selectors. */
#define PORT_CONNECTION     0
#define PORT_ENABLE         1
#define PORT_RESET          4
#define PORT_POWER          8
#define C_PORT_CONNECTION  16
#define C_PORT_RESET       20

typedef struct {
    uint8_t  port_count;
    uint8_t  pwr_on_2_pwr_good;
    uint8_t  change_ep;
    uint8_t  change_buf[HUB_CHANGE_BUFFER_SIZE];
    usb_device_t *dev;
    usb_transfer_t interrupt_transfer;
} hub_state_t;

static usb_probe_result_t hub_find_change_endpoint(
    usb_device_t *dev,
    uint8_t bitmap_bytes,
    uint8_t *endpoint,
    uint8_t *max_packet
) {
    uint8_t cfg[HUB_CONFIG_BUFFER_SIZE] = {0};
    if (
        usb_control_retry(
            dev,
            0x80,
            HUB_GET_DESCRIPTOR,
            (uint16_t)(USB_DESC_CONFIGURATION << 8),
            0,
            cfg,
            9
        ) < 0
    ) {
        return USB_PROBE_RETRY;
    }

    uint16_t total = (uint16_t)(cfg[2] | (uint16_t)(cfg[3] << 8));
    if (total < 9u || total > (uint16_t)sizeof(cfg)) {
        return USB_PROBE_REJECTED;
    }
    if (
        usb_control_retry(
            dev,
            0x80,
            HUB_GET_DESCRIPTOR,
            (uint16_t)(USB_DESC_CONFIGURATION << 8),
            0,
            cfg,
            total
        ) < 0
    ) {
        return USB_PROBE_RETRY;
    }

    bool in_hub_interface = false;
    uint16_t offset = 0;
    while ((uint32_t)offset + 2u <= (uint32_t)total) {
        uint8_t length = cfg[offset];
        uint8_t type = cfg[(uint16_t)(offset + 1u)];
        if (
            length < 2u
            || (uint32_t)offset + (uint32_t)length > (uint32_t)total
        ) {
            return USB_PROBE_REJECTED;
        }

        if (type == USB_DESC_INTERFACE) {
            in_hub_interface =
                length >= 9u
                && cfg[(uint16_t)(offset + 5u)] == HUB_CLASS;
        } else if (
            in_hub_interface
            && type == USB_DESC_ENDPOINT
            && length >= 7u
            && (cfg[(uint16_t)(offset + 2u)] & USB_ENDPOINT_IN) != 0u
            && (
                cfg[(uint16_t)(offset + 3u)]
                & USB_ENDPOINT_TYPE_MASK
            ) == USB_ENDPOINT_INTERRUPT
        ) {
            uint8_t address = cfg[(uint16_t)(offset + 2u)];
            uint16_t packet_size = (uint16_t)(
                cfg[(uint16_t)(offset + 4u)]
                | (uint16_t)(cfg[(uint16_t)(offset + 5u)] << 8)
            );
            packet_size &= 0x07FFu;
            if (
                (address & USB_ENDPOINT_NUMBER) == 0u
                || packet_size < (uint16_t)bitmap_bytes
                || packet_size > 0xFFu
            ) {
                return USB_PROBE_REJECTED;
            }
            *endpoint = (uint8_t)(address & USB_ENDPOINT_NUMBER);
            *max_packet = (uint8_t)packet_size;
            return USB_PROBE_BOUND;
        }

        offset = (uint16_t)(offset + (uint16_t)length);
    }
    return USB_PROBE_REJECTED;
}

static void hub_handle_port_change(hub_state_t *st, int port) {
    uint8_t data[4] = {0};
    if (
        usb_control_retry(
            st->dev,
            0xA3,
            HUB_GET_STATUS,
            0,
            (uint16_t)port,
            data,
            4
        ) < 0
    ) {
        return;
    }

    uint16_t change  = (uint16_t)(data[2] | (data[3] << 8));
    if (change & (1u << PORT_CONNECTION)) {
        /*
         * The core owns the change only after the queue accepts it. It keeps
         * the work through teardown, reset, enumeration, and the final
         * C_PORT_CONNECTION acknowledgment.
         */
        if (!usb_hub_port_change(st->dev, port)) {
            KWARN("usb_hub: port %d reconciliation deferred", port);
        }
    }
}

static void hub_status_cb(int status, usb_transfer_t *t) {
    if (status < 0) return;
    hub_state_t *st = (hub_state_t*)((uint8_t*)t->buffer
                       - __builtin_offsetof(hub_state_t, change_buf));
    for (int p = 0; p < st->port_count; p++) {
        if (st->change_buf[(p+1) / 8] & (1u << ((p+1) % 8))) {
            hub_handle_port_change(st, p + 1);
        }
    }
}

static usb_probe_result_t hub_probe(usb_device_t *dev) {
    if (dev->class_code != HUB_CLASS) return USB_PROBE_NOT_SUPPORTED;

    uint8_t desc[16] = {0};
    if (
        usb_control_retry(
            dev,
            0xA0,
            HUB_GET_DESCRIPTOR,
            0x2900,
            0,
            desc,
            16
        ) < 0
    ) {
        KWARN("usb_hub: get descriptor failed");
        return USB_PROBE_RETRY;
    }
    uint8_t nports = desc[2];
    if (nports > 63) {
        KWARN("usb_hub: descriptor claims %u ports, capping to 63", nports);
        nports = 63;
    }
    uint8_t pg2pg  = desc[5];
    uint32_t bitmap_bytes_wide = ((uint32_t)nports + 8u) / 8u;
    if (
        nports == 0u
        || bitmap_bytes_wide > HUB_CHANGE_BUFFER_SIZE
    ) {
        KWARN("usb_hub: invalid status bitmap for %u ports", nports);
        return USB_PROBE_REJECTED;
    }
    uint8_t bitmap_bytes = (uint8_t)bitmap_bytes_wide;
    uint8_t change_ep = 0;
    uint8_t change_max_packet = 0;
    usb_probe_result_t endpoint_result = hub_find_change_endpoint(
        dev,
        bitmap_bytes,
        &change_ep,
        &change_max_packet
    );
    if (endpoint_result != USB_PROBE_BOUND) {
        KWARN("usb_hub: no usable interrupt IN status endpoint");
        return endpoint_result;
    }

    hub_state_t *st = (hub_state_t*)kmalloc(sizeof(hub_state_t));
    if (!st) return USB_PROBE_RETRY;
    st->port_count = nports;
    st->pwr_on_2_pwr_good = pg2pg;
    st->change_ep = change_ep;
    for (int i = 0; i < (int)HUB_CHANGE_BUFFER_SIZE; i++) {
        st->change_buf[i] = 0;
    }
    st->dev = dev;
    dev->driver_data = st;

    for (int p = 1; p <= (int)nports; p++) {
        if (
            usb_control_retry(
                dev,
                0x23,
                HUB_SET_FEATURE,
                PORT_POWER,
                (uint16_t)p,
                NULL,
                0
            ) < 0
        ) {
            KWARN("usb_hub: port power request failed");
            dev->driver_data = NULL;
            kfree(st);
            return USB_PROBE_RETRY;
        }
    }
    for (volatile uint32_t i = 0; i < (uint32_t)pg2pg * 40000u; i++) { }

    usb_transfer_t t;
    t.dir = USB_DIR_IN; t.endpoint = st->change_ep; t.device_addr = dev->address;
    t.max_packet = change_max_packet; t.speed = dev->speed; t.data_toggle = 0;
    t.buffer = st->change_buf; t.length = (uint32_t)bitmap_bytes;
    t.tt_hub_addr = dev->tt_hub_addr; t.tt_port = dev->tt_port;
    st->interrupt_transfer = t;
    if (
        dev->hc->submit_interrupt(
            dev->hc,
            &st->interrupt_transfer,
            hub_status_cb
        ) < 0
    ) {
        KWARN("usb_hub: interrupt registration failed");
        dev->driver_data = NULL;
        kfree(st);
        return USB_PROBE_RETRY;
    }

    KINFO("usb_hub: attached %u ports, addr=%u depth=%u",
          nports, dev->address, dev->hub_depth);
    return USB_PROBE_BOUND;
}

static int hub_disconnect(usb_device_t *dev) {
    for (;;) {
        usb_device_t *child = NULL;
        int count = usb_device_count();
        for (int i = 0; i < count; i++) {
            usb_device_t *d = usb_get_device(i);
            if (d && d->parent_hub == dev) {
                child = d;
                break;
            }
        }
        if (!child) break;
        if (usb_device_remove(child) < 0) {
            KERROR("usb_hub: child teardown blocked hub removal");
            return -1;
        }
    }
    hub_state_t *st = dev->driver_data;
    if (st) {
        if (
            !dev->hc->cancel_interrupt
            || dev->hc->cancel_interrupt(
                dev->hc,
                &st->interrupt_transfer
            ) < 0
        ) {
            KERROR("usb_hub: interrupt cancellation failed; state retained");
            return -1;
        }
        kfree(st);
    }
    dev->driver_data = NULL;
    KINFO("usb_hub: detached");
    return 0;
}

static usb_driver_t hub_driver = {
    .name = "usb-hub", .probe = hub_probe, .disconnect = hub_disconnect,
    .next = NULL
};

void usb_hub_init(void);
void usb_hub_init(void) {
    usb_register_driver(&hub_driver);
}
