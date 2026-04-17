#ifndef USB_HC_H
#define USB_HC_H

#include "types.h"

#define USB_SPEED_LOW  1u
#define USB_SPEED_FULL 2u
#define USB_SPEED_HIGH 3u

#define USB_DIR_OUT   0u
#define USB_DIR_IN    1u
#define USB_DIR_SETUP 2u

typedef struct usb_hc usb_hc_t;

typedef struct {
    uint8_t   dir;
    uint8_t   endpoint;
    uint8_t   device_addr;
    uint8_t   max_packet;
    uint8_t   speed;
    uint8_t   data_toggle;
    uint8_t  *buffer;
    uint32_t  length;
    /* split-TT fields (EHCI transporting LS/FS device behind 2.0 hub) */
    uint8_t   tt_hub_addr;
    uint8_t   tt_port;
} usb_transfer_t;

typedef void (*usb_complete_cb_t)(int status, usb_transfer_t *);

struct usb_hc {
    const char *name;
    void       *driver_data;
    uint8_t     root_speed;   /* USB_SPEED_LOW/FULL/HIGH — speed of this HC's root ports */
    int  (*submit_sync)     (usb_hc_t *, usb_transfer_t *, uint32_t timeout_ms);
    int  (*submit_interrupt)(usb_hc_t *, usb_transfer_t *, usb_complete_cb_t cb);
    int  (*port_count)      (usb_hc_t *);
    int  (*port_status)     (usb_hc_t *, int port, uint32_t *status);
    int  (*port_reset)      (usb_hc_t *, int port);
    void (*irq_handler)     (usb_hc_t *);
};

#endif
