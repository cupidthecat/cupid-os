# USB Host Controller Stack

Cupid OS supports USB 1.1 and USB 2.0.

The USB host stack supports UHCI (USB 1.1) and EHCI (USB 2.0) controllers. It
has class drivers for HID boot-protocol keyboards and mice, hubs, and mass
storage through BBB and SCSI. HID events join the PS/2 event queue, so the
shell sees a unified input stream. Mass storage registers as a block device (`usb0`, `usb1`, ...).

---

## Table of Contents

1. [Overview](#overview)
2. [Boot Order - Critical](#boot-order--critical)
3. [PCI Layer](#pci-layer)
4. [USB Core](#usb-core)
5. [UHCI Driver](#uhci-driver)
6. [EHCI Driver](#ehci-driver)
7. [HID Driver](#hid-driver)
8. [Hub Driver](#hub-driver)
9. [Mass Storage Driver](#mass-storage-driver)
10. [Shell Commands](#shell-commands)
11. [QEMU Test Invocations](#qemu-test-invocations)
12. [Known Limits](#known-limits)

---

## Overview

| Subsystem | File | Notes |
|-----------|------|-------|
| PCI enumeration | `drivers/pci.c` | Bus 0, dev 0-31, multi-function via header type bit 7 |
| USB core | `kernel/usb/usb.c` | Device model, durable reconciliation, address ownership |
| HC vtable | `kernel/usb/usb_hc.h` | `usb_hc_t` interface |
| UHCI driver | `kernel/usb/uhci.c` | I/O-port registers, USB 1.1 |
| EHCI driver | `kernel/usb/ehci.c` | MMIO, USB 2.0, companion routing |
| HID class | `kernel/usb/usb_hid.c` | Boot protocol keyboard + mouse |
| Hub class | `kernel/usb/usb_hub.c` | Hub descriptor, status pipe, downstream ports |
| Mass storage | `kernel/usb/usb_msc.c` | BBB + SCSI, block-device lifetime |

### Subsystem relationships

```
                  ┌─────────────────────────────────┐
                  │           USB Core              │
                  │  usb_device_t[], work queue,    │
                  │  enumeration FSM, usb_control() │
                  └──────────┬──────────────────────┘
                             │  usb_hc_t vtable
              ┌──────────────┼──────────────┐
              ▼              ▼              │
         ┌────────┐    ┌────────┐           │
         │  UHCI  │    │  EHCI  │  (companion routing)
         │ (1.1)  │    │ (2.0)  │
         └────────┘    └────────┘
              │
    ┌─────────┼──────────────────┐
    ▼         ▼                  ▼
┌──────┐  ┌──────┐        ┌──────────┐
│ HID  │  │ Hub  │        │   MSC    │
│(kbd/ │  │class │        │(BBB+SCSI)│
│mouse)│  │      │        │          │
└──────┘  └──────┘        └──────────┘
    │                           │
    ▼                           ▼
PS/2 event queue          block_device_t
(keyboard_inject_scancode  (usb0, usb1, ...)
 mouse_inject_event)
```

### Related documentation

- [Filesystem.md](Filesystem.md) - FAT16 block device layer that MSC devices will eventually mount into
- [Swap.md](Swap.md) - another block-device consumer; shows how Cupid OS handles disk-backed storage
- [Architecture.md](Architecture.md) - ring-0 memory layout, PCI I/O space, MMIO mapping

---

## Boot Order - Critical

```
usb_init()
  └─► ehci_init_all()     ← MUST be first
  └─► uhci_init_all()     ← second
```

EHCI must initialize first because it claims all ports on the PCI bus by writing `CONFIGFLAG = 1` to its
operational register. After claiming, it inspects each port's `PORTSC.LINE_STATUS`. Ports with
a low-speed or full-speed device (LS/FS detected by `D+`/`D−` line state) have their
`PORTSC.PORT_OWNER` bit set, releasing them to the companion UHCI controller.

If UHCI initialises first, it starts driving its ports before EHCI has a chance to claim them.
This creates a port-ownership race: a high-speed device may be reset in full-speed mode,
permanently capping it at USB 1.1 speeds for that boot session. Changing the
EHCI-to-UHCI order breaks companion handoff.

---

## PCI Layer

Source: `drivers/pci.c`, `drivers/pci.h`

### Enumeration

`pci_init()` performs a flat scan of PCI bus 0, devices 0-31. For each device:

1. Read vendor/device ID at `(0, dev, 0, 0x00)`.
2. Skip if vendor is `0xFFFF` (no device).
3. Read header type at offset `0x0E`.
4. If bit 7 of header type is set -> multi-function device; probe all 8 functions.
5. Otherwise probe function 0 only.

```c
// Header-type constants
#define PCI_HEADER_TYPE_GENERAL    0x00   // 6 BARs
#define PCI_HEADER_TYPE_BRIDGE     0x01   // 2 BARs
#define PCI_HEADER_TYPE_CARDBUS    0x02   // 0 BARs
```

### Key functions

| Function | Purpose |
|----------|---------|
| `pci_init()` | Enumerate bus 0, populate global device table |
| `pci_find_by_class(class, subclass, prog_if, start_index)` | Iterate devices by class code |
| `pci_read32(bus, dev, fn, offset)` | Config-space 32-bit read via CF8/CFC |
| `pci_write32(bus, dev, fn, offset, val)` | Config-space 32-bit write |
| `pci_enable_bus_master(dev)` | Set bit 2 of Command register |

### Bus master enable - Status bit preservation

The PCI Status register has R/WC (read/write-1-to-clear) bits. A naive read-modify-write that
ORs Status as well as Command will inadvertently clear error flags. The correct pattern:

```c
uint32_t cmd_status = pci_read32(dev->bus, dev->dev, dev->fn, 0x04);
// Preserve only Command word; zero Status word before writing
cmd_status = (cmd_status & 0x0000FFFF) | PCI_CMD_BUS_MASTER;
pci_write32(dev->bus, dev->dev, dev->fn, 0x04, cmd_status);
```

Masking with `0x0000FFFF` before OR ensures the upper 16 bits (Status) are written as zero,
which is the safe value for R/WC bits - zeros do not clear anything.

### USB PCI class codes

| Class | Subclass | Prog-IF | Controller type |
|-------|----------|---------|----------------|
| 0x0C | 0x03 | 0x00 | OHCI |
| 0x0C | 0x03 | 0x10 | UHCI |
| 0x0C | 0x03 | 0x20 | EHCI |
| 0x0C | 0x03 | 0x30 | xHCI |

---

## USB Core

Source: `kernel/usb/usb.c`, `kernel/usb/usb_hc.h`, `kernel/usb/usb.h`

### Host controller vtable (`usb_hc_t`)

```c
typedef struct usb_hc {
    const char *name;
    void *driver_data;
    uint8_t root_speed;
    int  (*submit_sync)(usb_hc_t *, usb_transfer_t *, uint32_t timeout_ms);
    int  (*submit_interrupt)(usb_hc_t *, usb_transfer_t *,
                            usb_complete_cb_t);
    int  (*cancel_interrupt)(usb_hc_t *, usb_transfer_t *);
    int  (*port_count)(usb_hc_t *);
    int  (*port_status)(usb_hc_t *, int port, uint32_t *status);
    usb_port_reset_result_t (*port_reset)(usb_hc_t *, int port,
                                          bool must_reset);
    void (*irq_handler)(usb_hc_t *);
} usb_hc_t;
```

Each HC driver fills in this vtable at init time and passes a pointer to `usb_register_hc()`.

### Device model

```c
typedef struct usb_device_t {
    uint8_t address;
    uint8_t speed;
    uint8_t max_packet_ep0;
    uint8_t hub_depth;
    uint16_t vendor_id, product_id;
    uint8_t class_code, subclass, protocol;
    usb_hc_t *hc;
    struct usb_device_t *parent_hub;
    uint8_t parent_port;
    uint8_t tt_hub_addr;
    uint8_t tt_port;
    void *driver_data;
    struct usb_driver_t *driver;
    uint32_t generation;
    bool in_use;
} usb_device_t;
```

Capacity: 32 devices global. Hub nesting: maximum depth 5.

### Serialized port reconciliation

`usb_poll()` is the public polling boundary. An atomic guard folds an
overlapping call into the poll already running. The active poll handles EHCI
ports and interrupt pipes, UHCI ports and interrupt pipes, then pending port
work. Controller slots, device slots, and reconciliation therefore have one
cooperative consumer.

```c
bool usb_port_change(usb_hc_t *hc, int port);
bool usb_hub_port_change(usb_device_t *hub_dev, int port);
```

Both functions report whether the 32-entry ring accepted the work. A root
controller leaves its hardware change pending if the ring is full. A hub
leaves `C_PORT_CONNECTION` set until the core completes the handoff.

Work stays in the ring until it completes. Each poll attempts an item at most
once, then rotates a failed item behind its peers. Retry delay begins at 10
ms, doubles after another attempted failure, and stops growing at 1 second.
A new state observation resets the delay. Hub work carries its parent's
generation, so work from a removed or reused device slot is discarded.

A reconnect removes the previous root device or hub subtree before it
enumerates the current attachment. The core owns hub teardown, reset,
enumeration, change acknowledgement, and the final state reread. If the
hardware changes during that sequence, the same item remains queued.

### Enumeration sequence

`usb_process_pending()` runs the following FSM for each pending port change:

```
port_reset(hc, port, address_quarantined)
    │
    ▼
GET_DESCRIPTOR(dev_addr=0, len=8)    ← fetch first 8 bytes only (bMaxPacketSize0)
    │
    ▼
SET_ADDRESS(new_addr)
    │
    ▼
GET_DESCRIPTOR(new_addr, len=18)     ← full device descriptor
    │
    ▼
GET_CONFIGURATION(len=9)             ← config descriptor header (get wTotalLength)
    │
    ▼
GET_CONFIGURATION(len=wTotalLength)  ← full config + interface + endpoint descriptors
    │
    ▼
SET_CONFIGURATION(bConfigurationValue)
    │
    ▼
driver probe match                   ← bind, retry, reject, or try next driver
```

Addresses 1 through 127 come from a reusable reservation map. Removal
releases the address. If enumeration passes address assignment and then gets
an ambiguous result, the work item quarantines that address until a safe
reset, disconnect, or stale-work cleanup proves it can be reused.
`address_quarantined` sets the controller's `must_reset` argument. A
successful handoff must prove that the physical reset completed before the
core releases the reservation.

Ordinary control requests make up to five attempts with a timer-backed 10 ms
delay. An ambiguous `SET_ADDRESS` probes the new address before retrying. An
ambiguous `SET_CONFIGURATION` reads the active configuration first.

Class-driver probes return one of four typed results. `NOT_SUPPORTED` tries
the next driver. `BOUND` publishes the binding. `RETRY` unwinds the current
enumeration attempt and keeps the port work queued. `REJECTED` keeps a
permanently unsupported device present but unbound. An invalid result is
handled as a retry instead of silently publishing an incomplete device.

### `usb_control()` - control transfers

```c
int usb_control(usb_device_t *dev,
                uint8_t bmRequestType, uint8_t bRequest,
                uint16_t wValue, uint16_t wIndex,
                void *data, uint16_t wLength);
```

Builds an 8-byte SETUP packet and submits a 3-phase transfer (SETUP -> optional DATA -> STATUS)
via `hc->submit_sync`. The controller receives an explicit timeout.

---

## UHCI Driver

Source: `kernel/usb/uhci.c`

### Register access

UHCI uses **I/O-port** registers located at BAR4 (IO BAR, bit 0 set). Base address obtained via
`pci_read32(..., 0x20) & ~3`.

Key registers (offset from IO base):

| Offset | Name | Purpose |
|--------|------|---------|
| `0x00` | USBCMD | Run/Stop, Host Reset, Global Suspend |
| `0x02` | USBSTS | Interrupt status (write 1 to clear) |
| `0x04` | USBINTR | Interrupt enable mask |
| `0x06` | FRNUM | Current frame number (11-bit) |
| `0x08` | FLBASEADD | Frame list base address (physical, 4KB aligned) |
| `0x0C` | SOFMOD | Start-of-frame timing |
| `0x10` | PORTSC0 | Port 0 status/control |
| `0x12` | PORTSC1 | Port 1 status/control |

### Frame list and queue heads

- 1024-entry frame list, each entry is a 32-bit physical pointer to a QH or TD.
- Single skeleton QH (`skel_qh`) inserted into every frame list slot.
- Transfers use 32-byte Transfer Descriptors (TDs) linked off the skeleton QH.

### Critical: 16-byte alignment

`kmalloc` in Cupid OS returns pointers at offset `+12` from a 16-byte-aligned block header
(the block header consumes 12 bytes). UHCI TDs and QHs require 16-byte alignment per the
UHCI spec.

The driver allocates a raw buffer (`skel_qh_raw`, `td_raw`) large enough to hold the structure
plus 15 padding bytes, then aligns the pointer manually:

```c
skel_qh_raw = kmalloc(sizeof(uhci_qh_t) + 15);
skel_qh = (uhci_qh_t*)(((uintptr_t)skel_qh_raw + 15) & ~15);
// At kfree time: kfree(skel_qh_raw)  ← NOT skel_qh
```

The `_raw` pointer is saved for `kfree()`; the aligned pointer is used for hardware access.

### Legacy SMI disable

Some BIOSes take ownership of USB via SMI (System Management Interrupt). Cupid OS forces
handoff by writing to the LEGSUP register at PCI config offset `0xC0`:

```c
pci_write16(dev->bus, dev->dev, dev->fn, 0xC0, 0x8F00);
```

Bit 13 (`0x2000`) in `0x8F00` disables the SMI; bits `0x000F` clear pending status.

### Hot-plug detection

UHCI provides no reliable hot-plug interrupt. The serialized `usb_poll()`
cycle calls `uhci_poll_ports()`, which reads `PORTSC0` and `PORTSC1` and
calls `usb_port_change()` when `CONNECT_STATUS_CHANGE` is set. UHCI clears
that bit only after the core accepts the work, so queue pressure cannot
discard the edge.

Synchronous and interrupt transfers mutate the schedule under the controller
submit lock. Before descriptor storage is released, UHCI stops the schedule
and observes the halted state. Interrupt slots carry a generation and an
in-flight claim. Cancellation waits until the matching generation has
finished DMA and returned from its callback.

---

## EHCI Driver

Source: `kernel/usb/ehci.c`

### MMIO mapping

EHCI registers live at BAR0, a **memory BAR** (bit 0 clear). On typical PC hardware BAR0 is
above `0xFEB00000` - well beyond the 128 MB identity-map that Cupid OS sets up at boot.
The driver calls:

```c
paging_map_mmio(bar0_phys, 4096);
```

to create a 4KB kernel-virtual mapping before any register access.

### Register layout

```
BAR0 + 0x00  ┌──────────────────────────────┐
             │  Capability Registers (RO)   │  length = CAPLENGTH
             │  CAPLENGTH, HCIVERSION,      │
             │  HCSPARAMS, HCCPARAMS        │
BAR0 + CAPLENGTH
             ├──────────────────────────────┤
             │  Operational Registers (R/W) │
             │  USBCMD, USBSTS, USBINTR,    │
             │  FRINDEX, CTRLDSSEGMENT,     │
             │  PERIODICLISTBASE,           │
             │  ASYNCLISTADDR, CONFIGFLAG,  │
             │  PORTSC[0..N]                │
             └──────────────────────────────┘
```

`caplength` is read from the first byte of BAR0; the operational base is `bar0 + caplength`.

### BIOS handoff (USBLEGSUP)

Before taking ownership, the driver reads the extended capability list pointer from
`HCCPARAMS[15:8]`, then walks the linked list to find a capability with `CapID = 0x01`
(USBLEGSUP). It sets `OS_OWNED = 1` and polls `BIOS_OWNED` for up to 1 second:

```c
uint32_t leg = ehci_ext_cap_read(base, usblegsup_off);
leg |= (1 << 24);   // set OS_OWNED
ehci_ext_cap_write(base, usblegsup_off, leg);
// wait up to 1 s for BIOS_OWNED to clear
for (int i = 0; i < 1000 && (ehci_ext_cap_read(...) & (1<<16)); i++)
    pit_sleep_ms(1);
// force clear if BIOS did not release
ehci_ext_cap_write(base, usblegsup_off, leg & ~(1<<16));
```

### Critical: 32-byte alignment

EHCI QHs (Queue Heads) and qTDs (queue Transfer Descriptors) require 32-byte alignment.
Same manual pattern as UHCI:

```c
qh_raw = kmalloc(sizeof(ehci_qh_t) + 31);
qh     = (ehci_qh_t*)(((uintptr_t)qh_raw + 31) & ~31);
```

### CONFIGFLAG and companion routing

```c
// 1. Set CONFIGFLAG=1 - EHCI claims all ports
op_base->CONFIGFLAG = 1;

// 2. A low-speed K-state port can move directly to UHCI when no reset is owed.
//    Every other connected port is reset first.
for (int p = 0; p < port_count; p++) {
    uint32_t ps = op_base->PORTSC[p];
    if (LINE_STATUS(ps) == K_STATE && !must_reset) {
        op_base->PORTSC[p] = ps | PORT_OWNER;
    } else {
        reset_port_and_wait_for_clear(p);
        if (!(op_base->PORTSC[p] & PORT_ENABLE))
            op_base->PORTSC[p] |= PORT_OWNER;
    }
}
```

A port with `PORT_OWNER=1` is invisible to EHCI and fully controlled by the companion UHCI.
J-state does not identify a full-speed device before reset because a
high-speed-capable device can idle in the same state. EHCI verifies reset
assertion and clearing before it hands such a port to UHCI, and it reads
`PORT_OWNER` back before reporting a completed handoff.

### Async schedule

High-speed (USB 2.0) bulk and control transfers use the **asynchronous schedule**:

- Circular doubly-linked list of QHs.
- Dummy head QH (`async_head`) with `H=1` (reclamation list head bit).
- New QHs inserted before the dummy head; removed by relinking + door-bell.

### Periodic schedule

The controller owns a 1024-entry periodic frame list, but class-driver
interrupt registrations use controller-local software slots. `usb_poll()`
claims one slot generation at a time and performs the transfer through the
synchronous controller path. Cancellation waits for that generation's DMA
and callback work before it retires the slot.

### Hot-plug and DMA ownership

The IRQ records changed root ports in an atomic pending bitmap.
`ehci_poll_ports()` hands each bit to the serialized USB core. It clears the
bit only after the work ring accepts the request.

EHCI disables the asynchronous schedule and checks its status before it
changes the list or frees synchronous-transfer QHs and qTDs. If schedule
revocation cannot be proved, the controller halts and verifies that state.
Failure takes the panic path instead of freeing memory still visible to
hardware.

Each interrupt slot carries active and cancellation state, an in-flight
claim, and a generation. Pollers copy a claimed transfer under the submit
lock, release the lock for DMA and callback delivery, then finish that same
generation. Successful cancellation is the boundary after which a class
driver may release its report buffer. A callback cannot cancel its own slot.

---

## HID Driver

Source: `kernel/usb/usb_hid.c`

### Probe and setup

The HID driver claims any device with `bInterfaceClass = 0x03` (HID) and
`bInterfaceSubClass = 0x01` (Boot Interface). On probe:

```c
// 1. Switch to boot protocol
usb_control(dev, 0x21, SET_PROTOCOL, 0, interface, 0, NULL);

// 2. Disable idle report - only send on change
usb_control(dev, 0x21, SET_IDLE, 0, interface, 0, NULL);
```

### Keyboard report

The HID boot keyboard sends an 8-byte report on every interrupt endpoint poll:

```
Byte 0: Modifier bitmap  (Ctrl/Shift/Alt/GUI L/R)
Byte 1: Reserved (0x00)
Byte 2: Keycode 0
Byte 3: Keycode 1
Byte 4: Keycode 2
Byte 5: Keycode 3
Byte 6: Keycode 4
Byte 7: Keycode 5
```

The driver diffs each new report against the previous to detect key-down and key-up events.
Each new keycode is translated through a ~100-entry HID-usage -> PS/2-scancode table and
injected via `keyboard_inject_scancode()`.

**Covered keycodes:** printable ASCII, modifier keys (Shift/Ctrl/Alt), F1-F10, cursor arrows,
Backspace, Enter, Escape, Tab, Delete, Insert, Home, End, Page Up/Down.

#### Extended-scancode handling

PgUp / PgDn / Home / End / Insert / Delete and the cursor arrows are
**extended** scancodes on PS/2 - they require a `0xE0` prefix byte before
the make/break code. The driver tracks which HID keycodes need the
prefix in `hid_is_extended[]` and injects two scancodes for them
(`0xE0` then the scancode). `keyboard_inject_scancode()` in
`drivers/keyboard.c` recognises the `0xE0` prefix and routes the next
byte through `handle_extended_key()`, the same path a real PS/2 IRQ
takes - so the kernel's keyboard buffer ends up with `scancode = 0x49`,
`character = 0` for PgUp (rather than the ASCII `9` it would otherwise
produce as the non-extended Numpad-9 mapping).

### Mouse report

The HID boot mouse sends a 4-byte report (Intellimouse-style):

```
Byte 0: Button bitmap  (bit0=left, bit1=right, bit2=middle)
Byte 1: X displacement (signed, relative)
Byte 2: Y displacement (signed, relative)
Byte 3: Wheel delta    (signed; positive = scroll DOWN per HID spec)
```

The first three bytes are injected via `mouse_inject_event(buttons, dx, dy)`;
byte 3 goes through `mouse_inject_wheel(int8_t dz)` which inverts the sign
(USB HID +Z = down; Cupid OS convention +Z = up, matching PS/2
Intellimouse) and accumulates into `mouse.scroll_z` for the desktop's
wheel router to consume.

The official HID boot-protocol report is 3 bytes, but virtually every
real USB mouse returns 4 bytes even in boot mode. Pure 3-byte devices
leave `r[3] = 0`, so `mouse_inject_wheel(0)` is a no-op - no spurious
scroll events.

---

## Hub Driver

Source: `kernel/usb/usb_hub.c`

### Probe and descriptor fetch

Claimed when `bDeviceClass = 0x09` (Hub). On probe:

1. `GET_DESCRIPTOR(HUB)` -> 7+ byte hub descriptor.
2. Read `bNbrPorts` (number of downstream ports) and `bPwrOn2PwrGood` (power-on delay in 2ms units).
3. Call `SET_FEATURE(PORT_POWER)` for each port, then wait `bPwrOn2PwrGood * 2` ms.

### Port status change polling

The driver reads the interrupt IN status endpoint from the active
configuration descriptor. Its bitmap includes bit 0 for hub status and one
bit for every downstream port. A set port bit means the hub still owns a
change that the USB core must reconcile.

For each changed port:

```
1. GET_PORT_STATUS(port)             read wPortStatus + wPortChange
2. Check C_PORT_CONNECTION           connection change bit
3. Call usb_hub_port_change(hub, port)
4. Keep C_PORT_CONNECTION set if the work ring is full
5. In the serialized USB core:
     remove the old child subtree
     reset the connected port
     enumerate the current attachment
     clear C_PORT_RESET and C_PORT_CONNECTION
     reread status and change bits
6. Retry the same durable work item if state changed or acknowledgement failed
```

The hub callback does not clear the connection change before the work ring
accepts it. The core owns teardown, reset, enumeration, acknowledgement, and
the final reread. Hub work also carries the parent device generation, so an
old callback cannot act on a hub slot that has since been reused.

### Depth cap

The maximum hub nesting depth is **5**. Before recursing into a newly found hub device, the
driver checks `parent_depth + 1 <= 5`. Devices discovered beyond depth 5 are silently ignored.
(The USB 2.0 spec allows 7; Cupid OS caps at 5 for simplicity.)

### Transaction Translator (TT) routing

When a full-speed or low-speed device is connected behind a high-speed hub, the HS hub's
built-in Transaction Translator bridges speed domains. Cupid OS propagates TT info to
child device descriptors:

```c
child->tt_hub_addr = hub_dev->addr;
child->tt_port     = port;
```

EHCI uses these fields to set the `PORTSC.SPLIT_EN` and `TT*` fields in the QH for that device.

---

## Mass Storage Driver

Source: `kernel/usb/usb_msc.c`

### Bulk-Only Transport (BBB)

Every MSC operation consists of three phases:

```
Host -> Device:  CBW  (31 bytes, OUT bulk endpoint)
Host ↔ Device:  DATA (optional, direction from CBW flags)
Device -> Host:  CSW  (13 bytes, IN bulk endpoint)
```

CBW fields used by Cupid OS:

| Field | Value |
|-------|-------|
| dCBWSignature | `0x43425355` (`"USBC"`) |
| dCBWTag | monotonically increasing tag |
| dCBWDataTransferLength | byte count for DATA phase |
| bmCBWFlags | `0x80` = device->host, `0x00` = host->device |
| bCBWLUN | 0 (single-LUN assumption) |
| bCBWCBLength | length of embedded SCSI command |
| CBWCB | SCSI Command Descriptor Block (CDB) |

CSW `bCSWStatus`: `0x00` = success, `0x01` = command failed, `0x02` = phase error.

### SCSI commands

| Command | Opcode | Direction | Use |
|---------|--------|-----------|-----|
| INQUIRY | `0x12` | D->H | Device identification |
| TEST_UNIT_READY | `0x00` | none | Check ready |
| READ_CAPACITY(10) | `0x25` | D->H | Get LBA count + block size |
| READ(10) | `0x28` | D->H | Read sectors |
| WRITE(10) | `0x2A` | H->D | Write sectors |

### Block device registration

After a successful `READ_CAPACITY`, the driver registers its embedded block
device:

```c
block_device_t bd = {
    .name = "usb0",                 // usb1, usb2, ...
    .sector_size = lba_block_size,
    .sector_count = lba_count,
    .driver_data = msc_state,
    .read = blk_read,
    .write = blk_write,
    .release = msc_block_release,
    .registry_ref_count = 0,
    .registry_registered = false,
};
blkdev_register(&bd);
```

The registry has four sparse public slots. Registration uses the first
vacancy, and removal contracts the exclusive scan limit when trailing slots
become empty. `blkdev_count()` reports live entries, while
`blkdev_index_limit()` gives the bound for an index scan. A numeric index is
valid only for one registration lifetime and may name another device after
unregistration.

`blkdev_get(index)` acquires a reference to the exact object. Its caller must
eventually call `blkdev_put(dev)`. A saturated reference count makes
`blkdev_get()` return `NULL` without wrapping. The read and write callbacks
remain fixed for the object's lifetime. Each callback enters the MSC command
lock and checks the online state and USB device pointer before starting I/O.
On disconnect, the driver marks the state offline under that lock, clears the
USB device pointer, and unregisters the public slot. The registry drops its
own reference at that point. A cached reference remains safe and its I/O
fails cleanly. If unregister fails, MSC restores the attached online state so
the core can retry removal. The final `blkdev_put()` releases the MSC
allocation through `msc_block_release()`.

### MBR parsing

Immediately after registration, the driver reads sector 0 and checks for the MBR signature
`0x55AA` at bytes 510-511. If found, the four 16-byte partition entries at offsets
`0x01BE`-`0x01FD` are parsed:

| Offset | Field | Size |
|--------|-------|------|
| +0 | Status (0x80 = active) | 1 |
| +4 | Partition type | 1 |
| +8 | LBA start | 4 |
| +12 | LBA count | 4 |

FAT16 partition types detected: `0x04`, `0x06`, `0x0E`.

### Auto-mount status

> USB mass-storage devices register as raw block devices such as `usb0`, and
> MBR partitions are detected and logged. Direct block reads and writes work,
> but FAT16 filesystem operations on USB drives do not. The FAT16 VFS
> implementation in `kernel/fs/fat16.c` is a single instance tied to the ATA
> block device. Supporting a second volume requires moving its state into a
> heap-allocated instance passed through each VFS call, then connecting
> `usb_msc_probe()` to `fat16_mount()`.

See [Filesystem.md](Filesystem.md) for the current FAT16 architecture.

---

## Shell Commands

### `usb` - list all devices

```
Cupid OS> usb
[0] addr=1  speed=HIGH  vid=8086 pid=1234  class=09 parent=root
[1] addr=2  speed=FULL  vid=045e pid=0745  class=03 parent=0
[2] addr=3  speed=FULL  vid=093a pid=2510  class=03 parent=0
[3] addr=4  speed=FULL  vid=0781 pid=5567  class=08 parent=0
```

Fields: device index, assigned USB address, speed (LOW/FULL/HIGH), vendor ID, product ID,
class code, parent device (root = root hub, number = hub device index).

### `usb hubs` - hub tree view

```
Cupid OS> usb hubs
depth=0  addr=1  ports=4  (root hub via EHCI)
  depth=1  addr=2  port=1  speed=FULL  (HID keyboard)
  depth=1  addr=3  port=2  speed=FULL  (HID mouse)
  depth=1  addr=4  port=3  speed=FULL  (mass storage)
```

### `usb hc` - host controller boot log

```
Cupid OS> usb hc
EHCI: BAR0=0xfebf0000 ports=4 BIOS_handoff=OK
UHCI: IO=0xc080 ports=2 legacy_SMI=disabled
```

Displays the host controller summary captured during `usb_init()`. Useful for verifying that
EHCI initialised before UHCI and that BIOS handoff completed.

---

## QEMU Test Invocations

### HID keyboard + mouse

```bash
qemu-system-i386 \
  -drive if=ide,format=raw,file=cupidos.img \
  -device piix3-usb-uhci \
  -device usb-ehci \
  -device usb-kbd \
  -device usb-mouse \
  -serial stdio
```

### USB mass storage

```bash
# Create a test FAT16 image
dd if=/dev/zero of=test.img bs=1M count=64
mkfs.fat -F 16 test.img

qemu-system-i386 \
  -drive if=ide,format=raw,file=cupidos.img \
  -device piix3-usb-uhci \
  -device usb-ehci \
  -blockdev driver=file,filename=test.img,node-name=ustick-file \
  -blockdev driver=raw,file=ustick-file,node-name=ustick \
  -device usb-storage,id=ustick-device,drive=ustick \
  -serial stdio
```

### Full stack (kbd + mouse + storage)

```bash
qemu-system-i386 \
  -drive if=ide,format=raw,file=cupidos.img \
  -device piix3-usb-uhci \
  -device usb-ehci \
  -device usb-kbd \
  -device usb-mouse \
  -blockdev driver=file,filename=test.img,node-name=ustick-file \
  -blockdev driver=raw,file=ustick-file,node-name=ustick \
  -device usb-storage,id=ustick-device,drive=ustick \
  -serial stdio
```

### Reusing a QEMU storage backend

The file and raw block nodes above outlive the `usb-storage` device. This
matters when a test removes and adds the front end repeatedly. A legacy
`-drive if=none` backend can disappear with the device that used it.

From the QEMU monitor:

```text
device_del ustick-device
info qtree
device_add usb-storage,id=ustick-device,drive=ustick
```

After `device_del`, poll `info qtree` until that exact ID is absent before
adding it again. Read each monitor response through the next prompt and treat
an `Error:` response as a failed hot-plug operation.

> QEMU's `-device piix3-usb-uhci` emulates a UHCI controller. OHCI
> (used on VIA/SiS real hardware) is not emulated by the above flags and is not supported
> by Cupid OS.

---

## Known Limits

| Limitation | Details |
|-----------|---------|
| No OHCI | VIA/SiS chipsets use OHCI. QEMU emulates UHCI; real OHCI hardware not supported. |
| No xHCI | USB 3.x is out of scope for the P4 milestone. |
| No isochronous transfers | Audio, webcam, video capture not supported. |
| BBB only (no UAS) | USB Attached SCSI (UAS) is a USB 3.0 feature; not implemented. |
| Flat PCI bus 0 scan | PCI bridges not traversed. Devices behind bridges are invisible. |
| No power management | No USB suspend/resume, no remote wakeup, no selective suspend. |
| Boot protocol only (HID) | No HID report descriptor parser. Devices that do not support boot protocol not usable. |
| 32-device global limit | `USB_MAX_DEVICES = 32`. Large hubs with many attached devices may hit this. |
| Hub depth 5 | USB 2.0 spec allows 7 levels; Cupid OS enforces 5. |
| Callback self-cancellation | An interrupt callback cannot cancel its own registration. Disconnect paths cancel from outside the callback and wait for the claimed generation to finish. |
| FAT16 auto-mount not wired | USB mass storage registers as raw block device only. File-level access requires the FAT16 refactor described in [Mass Storage Driver](#mass-storage-driver). |

---

## Source File Map

```
kernel/
├── pci.h          - PCI device struct, BAR helpers, class codes
├── pci.c          - pci_init, pci_find_by_class, pci_enable_bus_master
├── usb_hc.h       - usb_hc_t vtable, usb_transfer_t
├── usb.h          - usb_device_t, speed/class constants, work queue API
├── usb.c          - USB core: register_hc, enumeration FSM, usb_control
├── uhci.c         - UHCI 1.1 host controller driver
├── ehci.c         - EHCI 2.0 host controller driver
├── usb_hid.c      - HID boot protocol: keyboard + mouse class driver
├── usb_hub.c      - Hub class driver
└── usb_msc.c      - Mass storage BBB + SCSI + block_device registration
```
