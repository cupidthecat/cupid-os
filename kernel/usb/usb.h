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
    uint32_t  generation;
    bool      in_use;
} usb_device_t;

/*
 * NOT_SUPPORTED lets the core try another driver. RETRY means the driver
 * matched but could not finish for a temporary reason. REJECTED means the
 * driver matched and found a permanent reason not to bind.
 */
typedef enum {
    USB_PROBE_NOT_SUPPORTED = -1,
    USB_PROBE_BOUND = 0,
    USB_PROBE_RETRY = 1,
    USB_PROBE_REJECTED = 2
} usb_probe_result_t;

typedef struct usb_driver_t {
    const char *name;
    usb_probe_result_t (*probe)(usb_device_t *dev);
    int  (*disconnect)(usb_device_t *dev);
    struct usb_driver_t *next;
} usb_driver_t;

void usb_init(void);
int  usb_register_hc(usb_hc_t *hc);
int  usb_register_driver(usb_driver_t *d);

/*
 * Queue port work from the serialized USB polling cycle. A false result means
 * the caller must leave the hardware change pending and try again later.
 */
bool usb_port_change    (usb_hc_t *hc, int port);
bool usb_hub_port_change(usb_device_t *hub, int port);

/*
 * Run one controller and enumeration poll. Concurrent callers are folded
 * into the poll already in progress.
 */
void usb_poll(void);

/* Internal pending-work drain. Use usb_poll() outside the USB core. */
void usb_process_pending(void);

int  usb_device_count(void);
usb_device_t *usb_get_device(int index);

/* Standard control transfer helper. Returns 0 on success, negative on error. */
int usb_control(usb_device_t *dev, uint8_t bmRequestType, uint8_t bRequest,
                uint16_t wValue, uint16_t wIndex, void *data, uint16_t wLength);
int usb_control_retry(usb_device_t *dev, uint8_t bmRequestType,
                      uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
                      void *data, uint16_t wLength);

/* Retire a device only after its driver has released controller ownership. */
int usb_device_remove(usb_device_t *dev);

/* Register a shared USB IRQ dispatcher for the given IRQ line.
 * Safe to call multiple times with the same irq - installs only once.
 * HCs sharing an IRQ line must use this instead of irq_install_handler
 * directly so all HC irq_handler callbacks are invoked on each firing.*/
void usb_register_irq(uint8_t irq);

/* Exported for CupidC feature test. Returns class_code of device at
 * slot `index`, or 0 if slot is empty / out of range.*/
uint8_t usb_device_class(int index);

#endif
