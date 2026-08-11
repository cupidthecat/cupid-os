# USB reconciliation and lifetime ownership

- Status: Accepted
- Date: 2026-07-25

## Context

USB port changes can arrive while the USB work ring is full, while a previous reconciliation is retrying, or after a hub slot has been removed and reused. Treating an edge as a one-shot notification lost changes under those conditions. Repeating enumeration without first removing the old subtree also left old class-driver resources attached to a reconnecting port.

Address assignment has a separate ambiguity. A device can accept `SET_ADDRESS` or `SET_CONFIGURATION` even when the controller loses the status-stage completion. Releasing that address immediately after a reported failure could assign it to another device while the first device still owns it.

Interrupt pipes have two ownership boundaries. A controller may use the transfer buffer while a poller is about to call a callback, and a slot can be reused after cancellation. Freeing state after an unqualified cancellation or allowing an old cancellation to retire a new registration risks a use after free. EHCI and UHCI also need a proved hardware quiescence point before they free synchronous-transfer DMA descriptors.

USB mass storage exposes the same problem through the block registry. A numeric registry index can be reused, while filesystem code can retain a `block_device_t` pointer after a drive disconnects.

## Decision

`usb_poll()` serializes controller polling and pending-work reconciliation. Port work is durable until it completes. Root-controller status is only acknowledged after the core has accepted the work. Hub callbacks queue work without acknowledging `C_PORT_CONNECTION`; the core owns teardown, reset, enumeration, acknowledgement, and the final port-state reread.

Each pending item is a reconciliation request for current hardware state. A failed item remains in the ring, rotates behind its peers, and receives an exponential retry delay from 10 ms to 1 s. A state observation resets that delay. Hub work carries the source hub's generation, so work from a removed or reused hub slot is discarded. Reconnection removes the old root device or hub subtree before it enumerates the current attachment.

Ordinary control transfers retry at most five times with timer delays. Address assignment probes the new address after an ambiguous completion. Configuration selection reads back the active configuration before it repeats an ambiguous request. The allocator reserves addresses 1 through 127 and reuses released addresses. Work that has passed the address boundary but has an ambiguous later result quarantines its reservation until a reset, disconnect, or stale-work cleanup makes it safe to release. The core passes `must_reset` to the root controller while such a reservation exists. A successful handoff then proves that the physical reset completed before the address becomes reusable.

Driver probes return a typed result. `NOT_SUPPORTED` lets the next driver try, `BOUND` publishes the binding, `RETRY` unwinds enumeration and keeps the port work durable, and `REJECTED` leaves a permanently unsupported device unbound. An invalid result is treated as retryable failure. HID setup and hub descriptor, endpoint, and power requests use the bounded control helper. Mass storage also retries its optional maximum-LUN request, while retaining LUN zero when a valid Bulk-Only device stalls that request.

EHCI and UHCI own fixed controller-local interrupt slots. A slot carries its active state, cancellation request, in-flight claim, and generation. The submit lock covers publication, claims, and cancellation requests. Pollers copy a claimed transfer and call callbacks outside the lock. Cancellation waits for the same generation's DMA and callback to finish, then retires that generation. A callback may not cancel its own registration.

Both controllers prove that hardware no longer owns synchronous-transfer DMA before they free it. UHCI stops the schedule and observes halt. EHCI quiesces the asynchronous schedule or halts the controller and verifies the relevant state. A failed revocation leaves submission failed and takes the panic path rather than releasing memory still visible to hardware. EHCI only bypasses reset for a low-speed K-state attachment with no quarantined address. J-state proceeds through reset because it can also describe a high-speed-capable device. The controller verifies reset assertion and clearing, then checks that companion ownership latched before it reports handoff.

The UHCI interrupt handler acknowledges only the five write-clear interrupt
status bits. It never writes the read-only HCHalted bit back to USBSTS. This
preserves the schedule-stop proof when an interrupt races transfer teardown
on another CPU.

The block registry reuses its first vacant slot and shortens the scan bound when trailing entries become vacant. `blkdev_count()` reports live registrations, while `blkdev_index_limit()` reports the exclusive sparse scan bound. A numeric index is a registration-scoped lease.

`blkdev_get()` acquires a reference and every successful call requires one `blkdev_put()`. It returns `NULL` instead of wrapping a saturated reference count. The registry owns one reference while an entry is public. USB mass storage keeps its callbacks immutable. Each callback enters the command lock and checks the online state and USB device pointer before starting I/O. Disconnect marks the state offline under that lock, clears the USB pointer, and unregisters the public entry. Cached references then fail I/O safely. If unregister fails, the driver restores its online state and USB pointer so removal can retry. The final returned reference invokes the driver's release callback and frees the mass-storage state.

## Invariants

- One `usb_poll()` call owns the cooperative USB poll cycle at a time.
- A rejected port handoff leaves the controller or hub change state pending.
- Failed reconciliation work remains represented until completion or stale hub work disposes of it.
- A reconnect removes the prior subtree before a replacement is enumerated.
- A quarantined address is unavailable until the associated work reaches a safe release point.
- A handoff with a quarantined address proves reset before releasing that address.
- A successful interrupt cancellation means the caller may release its transfer buffer.
- An interrupt callback runs without the controller submit lock.
- A controller does not free transfer DMA until it has proved quiescence.
- UHCI interrupt acknowledgement cannot clear the observed halt state.
- A disconnected mass-storage pointer remains safe to call and reports I/O failure.
- A failed mass-storage unregister restores the attached online state.
- A block-device index can name a different device after unregistration.
- A block-device reference count never wraps.
- Every successful `blkdev_get()` has one matching `blkdev_put()`.

## Rejected alternatives

Clearing a hardware change before the work ring accepts it was rejected. That turns queue pressure into a lost connect or disconnect event.

Dropping failed work was rejected. Enumeration and hub control requests can fail transiently, and one failing port must not prevent later ports from receiving a turn.

Releasing an address after every reported `SET_ADDRESS` failure was rejected. The device may have accepted the request before the controller lost its completion.

Treating pre-reset J-state as proof of a full-speed device was rejected. High-speed-capable devices also idle in J-state before reset. Releasing a quarantined address during an unproved companion handoff was rejected for the same reason: the device may still answer at that address.

Holding the controller submit lock while a callback runs was rejected. The callback may queue more work and should not make the transfer path depend on callback execution time.

Returning from cancellation while DMA or a callback remains in flight was rejected. The caller uses successful cancellation as the boundary for freeing its buffer and related driver state.

Increasing the UHCI halt timeout was rejected after the four-CPU RTL8139
frontier exposed a panic. The controller had already stopped. Its IRQ handler
could acknowledge the whole USBSTS word and erase HCHalted from the emulator's
status register. Waiting longer could not restore that lost proof.

Freeing mass-storage state at disconnect was rejected. Existing block-device pointers can outlive the public registration and need a defined offline result. Rewriting callback pointers during disconnect was also rejected because a reader could race the rewrite. Immutable callbacks and the command lock provide one ownership boundary.

## Evidence

`tests/usb_reconciliation_runtime.c` executes the core's durable queue, reentrant producer, retry, quarantine, address reuse, removal, hub acknowledgement, probe retry, fallback, rejection, and companion-handoff paths. Its quarantined-handoff case requires the controller's reset proof. `tests/test_usb_reconciliation_runtime.py` checks that fixture and source-level contracts. `tests/usb_interrupt_ownership_contract.c` and `tests/test_usb_interrupt_ownership.py` cover generation claims, cancellation, callback ordering, and slot reuse for both controllers. `tests/test_usb_interrupt_cancel.py`, `tests/test_usb_poll_serialization.py`, and `tests/test_usb_port_reset.py` keep the cancellation, single-poller, and verified-reset contracts explicit. `tests/test_blockdev_slot_reuse.py` exercises repeated registration, removal, sparse counts, saturated references, and concurrent unregister during logging through the real block-device code. `tests/usb_msc_lifetime_contract.c` holds a reference across disconnect, checks offline I/O, proves rollback after a failed unregister, and proves release on the final put.

All 45 USB tests and all 123 GUI gate tests pass. Four-vCPU QEMU gates with e1000 and RTL8139 each detach and reattach the UHCI keyboard and mouse, require fresh input after reattachment, and cycle the EHCI disk through six storage lifetimes. The gates also reject controller errors, failed device additions, panic markers, and stale I/O after removal.

The implementation lives in `kernel/usb/usb.c`, `kernel/usb/usb_hub.c`, `kernel/usb/usb_hc.h`, `kernel/usb/ehci.c`, `kernel/usb/uhci.c`, `kernel/usb/usb_msc.c`, `kernel/fs/blockdev.c`, and `kernel/fs/blockdev.h`.

## Limits

The stack still has no OHCI or xHCI controller, isochronous scheduler, USB Attached SCSI support, power-management path, general HID report parser, or USB FAT16 automatic mount path. The device table remains limited to 32 live slots and hub depth remains limited to five. The block registry has four public slots. This decision does not add a general reader-lifetime protocol for callers of `usb_get_device()`.
