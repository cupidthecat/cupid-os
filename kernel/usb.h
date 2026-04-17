#ifndef USB_H
#define USB_H

#include "types.h"
#include "usb_hc.h"

#define USB_MAX_DEVICES    32
#define USB_MAX_HUB_DEPTH   5
#define USB_MAX_HCS         4
#define USB_MAX_DRIVERS    16
#define USB_WORKQ_SIZE     32

typedef struct usb_device_t {
    uint8_t   address;
    uint8_t   speed;
    uint8_t   max_packet_ep0;
    uint8_t   hub_depth;
    uint16_t  vendor_id, product_id;
    uint8_t   class_code, subclass, protocol;
    usb_hc_t *hc;
    struct usb_device_t *parent_hub;
    uint8_t   parent_port;
    uint8_t   tt_hub_addr;
    uint8_t   tt_port;
    void     *driver_data;
    struct usb_driver_t *driver;
    bool      in_use;
} usb_device_t;

typedef struct usb_driver_t {
    const char *name;
    int  (*probe)     (usb_device_t *dev);
    void (*disconnect)(usb_device_t *dev);
    struct usb_driver_t *next;
} usb_driver_t;

void usb_init(void);
int  usb_register_hc(usb_hc_t *hc);
int  usb_register_driver(usb_driver_t *d);

/* Lock-free pushes. Safe from IRQ / HC callback context. */
void usb_port_change    (usb_hc_t *hc, int port);
void usb_hub_port_change(usb_device_t *hub, int port);

/* Cooperatively scheduled; call from idle loop. */
void usb_process_pending(void);

int  usb_device_count(void);
usb_device_t *usb_get_device(int index);

/* Standard control transfer helper. Returns 0 on success, negative on error. */
int usb_control(usb_device_t *dev, uint8_t bmRequestType, uint8_t bRequest,
                uint16_t wValue, uint16_t wIndex, void *data, uint16_t wLength);

/* Called by hub driver on device remove. */
void usb_device_remove(usb_device_t *dev);

/* Register a shared USB IRQ dispatcher for the given IRQ line.
 * Safe to call multiple times with the same irq — installs only once.
 * HCs sharing an IRQ line must use this instead of irq_install_handler
 * directly so all HC irq_handler callbacks are invoked on each firing. */
void usb_register_irq(uint8_t irq);

/* Exported for CupidC feature test. Returns class_code of device at
 * slot `index`, or 0 if slot is empty / out of range. */
uint8_t usb_device_class(int index);

#endif
