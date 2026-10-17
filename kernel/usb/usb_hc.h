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

/*
 * A root-port reset can complete locally or transfer the port to a companion
 * controller. A negative result is retryable failure. When must_reset is true,
 * a successful result proves the physical port reset before this call returns.
 * Handoff is complete for the current controller; the companion observes and
 * enumerates the device.
 */
typedef enum {
    USB_PORT_RESET_FAILED = -1,
    USB_PORT_RESET_OK = 0,
    USB_PORT_RESET_HANDOFF = 1
} usb_port_reset_result_t;

/*
 * Controller interrupt slots keep this state under their submit lock.
 * in_flight stays set from the poller's snapshot through callback return, so
 * successful cancellation also proves that the caller may release its buffer.
 * A callback must not cancel its own registration.
 */
typedef struct {
    bool              active;
    bool              cancel_requested;
    volatile uint32_t in_flight;
    uint32_t          generation;
} usb_interrupt_ownership_t;

static inline void usb_interrupt_ownership_init(
    usb_interrupt_ownership_t *owner
) {
    owner->active = false;
    owner->cancel_requested = false;
    __atomic_store_n(&owner->in_flight, 0u, __ATOMIC_RELAXED);
    owner->generation = 0u;
}

/* The controller submit lock must cover publish, claim, and cancellation. */
static inline uint32_t usb_interrupt_ownership_publish(
    usb_interrupt_ownership_t *owner
) {
    if (
        owner->active
        || owner->cancel_requested
        || __atomic_load_n(&owner->in_flight, __ATOMIC_RELAXED) != 0u
    ) {
        return 0u;
    }
    owner->generation++;
    if (owner->generation == 0u) owner->generation = 1u;
    owner->active = true;
    return owner->generation;
}

static inline uint32_t usb_interrupt_ownership_claim(
    usb_interrupt_ownership_t *owner
) {
    if (
        !owner->active
        || owner->cancel_requested
        || __atomic_load_n(&owner->in_flight, __ATOMIC_RELAXED) != 0u
    ) {
        return 0u;
    }
    __atomic_store_n(&owner->in_flight, 1u, __ATOMIC_RELEASE);
    return owner->generation;
}

static inline uint32_t usb_interrupt_ownership_request_cancel(
    usb_interrupt_ownership_t *owner
) {
    if (!owner->active) return 0u;
    owner->cancel_requested = true;
    return owner->generation;
}

static inline bool usb_interrupt_ownership_may_deliver(
    const usb_interrupt_ownership_t *owner,
    uint32_t generation
) {
    return (
        owner->active
        && owner->generation == generation
        && !owner->cancel_requested
    );
}

static inline bool usb_interrupt_ownership_is_in_flight(
    const usb_interrupt_ownership_t *owner
) {
    return (
        __atomic_load_n(&owner->in_flight, __ATOMIC_ACQUIRE) != 0u
    );
}

static inline void usb_interrupt_ownership_finish(
    usb_interrupt_ownership_t *owner,
    uint32_t generation
) {
    if (owner->generation == generation) {
        __atomic_store_n(&owner->in_flight, 0u, __ATOMIC_RELEASE);
    }
}

static inline bool usb_interrupt_ownership_retire(
    usb_interrupt_ownership_t *owner,
    uint32_t generation
) {
    if (owner->generation != generation) return true;
    if (usb_interrupt_ownership_is_in_flight(owner)) return false;
    owner->active = false;
    owner->cancel_requested = false;
    return true;
}

struct usb_hc {
    const char *name;
    void       *driver_data;
    uint8_t     root_speed;   /* USB_SPEED_LOW/FULL/HIGH - speed of this HC's root ports */
    int  (*submit_sync)     (usb_hc_t *, usb_transfer_t *, uint32_t timeout_ms);
    int  (*submit_interrupt)(usb_hc_t *, usb_transfer_t *, usb_complete_cb_t cb);
    int  (*cancel_interrupt)(usb_hc_t *, usb_transfer_t *);
    int  (*port_count)      (usb_hc_t *);
    int  (*port_status)     (usb_hc_t *, int port, uint32_t *status);
    usb_port_reset_result_t (*port_reset)(
        usb_hc_t *,
        int port,
        bool must_reset
    );
    void (*irq_handler)     (usb_hc_t *);
};

#endif
