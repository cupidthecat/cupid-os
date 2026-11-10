#include "usb_hc.h"

int main(void) {
    usb_interrupt_ownership_t owner;
    usb_interrupt_ownership_init(&owner);
    if (owner.active || usb_interrupt_ownership_is_in_flight(&owner)) {
        return 1;
    }

    uint32_t first = usb_interrupt_ownership_publish(&owner);
    if (first == 0u || usb_interrupt_ownership_publish(&owner) != 0u) {
        return 2;
    }
    if (usb_interrupt_ownership_claim(&owner) != first) return 3;
    if (usb_interrupt_ownership_claim(&owner) != 0u) return 4;
    if (!usb_interrupt_ownership_is_in_flight(&owner)) return 5;

    if (usb_interrupt_ownership_request_cancel(&owner) != first) return 6;
    if (usb_interrupt_ownership_may_deliver(&owner, first)) return 7;
    if (usb_interrupt_ownership_retire(&owner, first)) return 8;

    usb_interrupt_ownership_finish(&owner, first);
    if (usb_interrupt_ownership_is_in_flight(&owner)) return 9;
    if (!usb_interrupt_ownership_retire(&owner, first)) return 10;
    if (owner.active || owner.cancel_requested) return 11;

    uint32_t second = usb_interrupt_ownership_publish(&owner);
    if (second == 0u || second == first) return 12;
    if (usb_interrupt_ownership_claim(&owner) != second) return 13;

    /*
     * A waiter for the first generation must not retire a slot that has since
     * been published for a different transfer.
     */
    if (!usb_interrupt_ownership_retire(&owner, first)) return 14;
    if (
        !owner.active
        || owner.generation != second
        || !usb_interrupt_ownership_is_in_flight(&owner)
    ) {
        return 15;
    }
    if (!usb_interrupt_ownership_may_deliver(&owner, second)) return 16;

    if (usb_interrupt_ownership_request_cancel(&owner) != second) return 17;
    if (usb_interrupt_ownership_may_deliver(&owner, second)) return 18;
    usb_interrupt_ownership_finish(&owner, second);
    if (!usb_interrupt_ownership_retire(&owner, second)) return 19;
    return 0;
}
