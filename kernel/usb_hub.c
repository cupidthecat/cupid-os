#include "usb.h"
#include "memory.h"
#include "../drivers/serial.h"

#define HUB_CLASS 0x09

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
    uint8_t  change_buf[8];
    usb_device_t *dev;
} hub_state_t;

static void hub_handle_port_change(hub_state_t *st, int port) {
    uint8_t data[4] = {0};
    if (usb_control(st->dev, 0xA3, HUB_GET_STATUS, 0, (uint16_t)port, data, 4) < 0) return;

    uint16_t status  = (uint16_t)(data[0] | (data[1] << 8));
    uint16_t change  = (uint16_t)(data[2] | (data[3] << 8));

    if (change & (1u << PORT_CONNECTION)) {
        usb_control(st->dev, 0x23, HUB_CLEAR_FEATURE, C_PORT_CONNECTION,
                    (uint16_t)port, NULL, 0);

        if (status & (1u << PORT_CONNECTION)) {
            for (volatile int i = 0; i < 2000000; i++) { }

            usb_control(st->dev, 0x23, HUB_SET_FEATURE, PORT_RESET,
                        (uint16_t)port, NULL, 0);

            for (int i = 0; i < 100; i++) {
                usb_control(st->dev, 0xA3, HUB_GET_STATUS, 0, (uint16_t)port, data, 4);
                uint16_t ch = (uint16_t)(data[2] | (data[3] << 8));
                if (ch & (1u << (C_PORT_RESET - 16))) break;
                for (volatile int k = 0; k < 100000; k++) { }
            }
            usb_control(st->dev, 0x23, HUB_CLEAR_FEATURE,
                        C_PORT_RESET, (uint16_t)port, NULL, 0);

            if (st->dev->hub_depth + 1 > USB_MAX_HUB_DEPTH) {
                KWARN("usb_hub: depth cap %u exceeded, skipping port %d",
                       USB_MAX_HUB_DEPTH, port);
                usb_control(st->dev, 0x23, HUB_CLEAR_FEATURE, PORT_ENABLE,
                            (uint16_t)port, NULL, 0);
                return;
            }
            usb_hub_port_change(st->dev, port);
        } else {
            for (int i = 0; i < USB_MAX_DEVICES; i++) {
                usb_device_t *d = usb_get_device(i);
                if (!d) continue;
                if (d->parent_hub == st->dev && d->parent_port == port) {
                    usb_device_remove(d);
                }
            }
        }
    }
    (void)status;
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

static int hub_probe(usb_device_t *dev) {
    if (dev->class_code != HUB_CLASS) return -1;

    uint8_t desc[16] = {0};
    if (usb_control(dev, 0xA0, HUB_GET_DESCRIPTOR, 0x2900, 0, desc, 16) < 0) {
        KWARN("usb_hub: get descriptor failed");
        return -1;
    }
    uint8_t nports = desc[2];
    if (nports > 63) {
        KWARN("usb_hub: descriptor claims %u ports, capping to 63", nports);
        nports = 63;
    }
    uint8_t pg2pg  = desc[5];

    hub_state_t *st = (hub_state_t*)kmalloc(sizeof(hub_state_t));
    if (!st) return -1;
    st->port_count = nports;
    st->pwr_on_2_pwr_good = pg2pg;
    st->change_ep = 1;
    for (int i = 0; i < 8; i++) st->change_buf[i] = 0;
    st->dev = dev;
    dev->driver_data = st;

    for (int p = 1; p <= (int)nports; p++) {
        usb_control(dev, 0x23, HUB_SET_FEATURE, PORT_POWER, (uint16_t)p, NULL, 0);
    }
    for (volatile uint32_t i = 0; i < (uint32_t)pg2pg * 40000u; i++) { }

    usb_transfer_t t;
    t.dir = USB_DIR_IN; t.endpoint = st->change_ep; t.device_addr = dev->address;
    t.max_packet = 1; t.speed = dev->speed; t.data_toggle = 0;
    t.buffer = st->change_buf; t.length = (uint32_t)((nports + 7) / 8);
    t.tt_hub_addr = dev->tt_hub_addr; t.tt_port = dev->tt_port;
    dev->hc->submit_interrupt(dev->hc, &t, hub_status_cb);

    KINFO("usb_hub: attached %u ports, addr=%u depth=%u",
          nports, dev->address, dev->hub_depth);
    return 0;
}

static void hub_disconnect(usb_device_t *dev) {
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        usb_device_t *d = usb_get_device(i);
        if (d && d->parent_hub == dev) usb_device_remove(d);
    }
    if (dev->driver_data) kfree(dev->driver_data);
    dev->driver_data = NULL;
    KINFO("usb_hub: detached");
}

static usb_driver_t hub_driver = {
    .name = "usb-hub", .probe = hub_probe, .disconnect = hub_disconnect,
    .next = NULL
};

void usb_hub_init(void);
void usb_hub_init(void) {
    usb_register_driver(&hub_driver);
}
