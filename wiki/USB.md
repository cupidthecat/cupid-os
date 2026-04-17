# USB Host Controller Stack

**CupidOS P4 — USB 1.1 + 2.0**

CupidOS implements a full USB host controller stack supporting UHCI (USB 1.1) and EHCI (USB 2.0)
host controllers. Three class drivers are provided: HID boot protocol (keyboard + mouse), Hub,
and Mass Storage (BBB + SCSI). HID events are merged into the existing PS/2 event queue so the
shell sees a unified input stream. Mass storage registers as a block device (`usb0`, `usb1`, …).

---

## Table of Contents

1. [Overview](#overview)
2. [Boot Order — Critical](#boot-order--critical)
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
| PCI enumeration | `kernel/pci.c` | Bus 0, dev 0-31, multi-function via header type bit 7 |
| USB core | `kernel/usb.c` | Device model, enumeration FSM, work queue |
| HC vtable | `kernel/usb_hc.h` | `usb_hc_t` interface |
| UHCI driver | `kernel/uhci.c` | IO-port MMIO, USB 1.1 |
| EHCI driver | `kernel/ehci.c` | MMIO, USB 2.0, companion routing |
| HID class | `kernel/usb_hid.c` | Boot protocol keyboard + mouse |
| Hub class | `kernel/usb_hub.c` | Hub descriptor, per-port power + reset |
| Mass storage | `kernel/usb_msc.c` | BBB + SCSI, block device registration |

### Subsystem relationships

```
                  ┌─────────────────────────────────┐
                  │           USB Core               │
                  │  usb_device_t[], work queue,     │
                  │  enumeration FSM, usb_control()  │
                  └──────────┬──────────────────────┘
                             │  usb_hc_t vtable
              ┌──────────────┼──────────────┐
              ▼              ▼              │
         ┌────────┐    ┌────────┐          │
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

- [Filesystem.md](Filesystem.md) — FAT16 block device layer that MSC devices will eventually mount into
- [Swap.md](Swap.md) — another block-device consumer; shows how CupidOS handles disk-backed storage
- [Architecture.md](Architecture.md) — ring-0 memory layout, PCI I/O space, MMIO mapping

---

## Boot Order — Critical

```
usb_init()
  └─► ehci_init_all()     ← MUST be first
  └─► uhci_init_all()     ← second
```

**Why order matters:** EHCI claims all ports on the PCI bus by writing `CONFIGFLAG = 1` to its
operational register. After claiming, it inspects each port's `PORTSC.LINE_STATUS`. Ports with
a low-speed or full-speed device (LS/FS detected by `D+`/`D−` line state) have their
`PORTSC.PORT_OWNER` bit set, releasing them to the companion UHCI controller.

If UHCI initialises first, it starts driving its ports before EHCI has a chance to claim them.
This creates a port-ownership race: a high-speed device may be reset in full-speed mode,
permanently capping it at USB 1.1 speeds for that boot session. The reversed init order
(EHCI → UHCI) is **not optional** — changing it breaks companion handoff.

---

## PCI Layer

Source: `kernel/pci.c`, `kernel/pci.h`

### Enumeration

`pci_init()` performs a flat scan of PCI bus 0, devices 0–31. For each device:

1. Read vendor/device ID at `(0, dev, 0, 0x00)`.
2. Skip if vendor is `0xFFFF` (no device).
3. Read header type at offset `0x0E`.
4. If bit 7 of header type is set → multi-function device; probe all 8 functions.
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

### Bus master enable — Status bit preservation

The PCI Status register has R/WC (read/write-1-to-clear) bits. A naive read-modify-write that
ORs Status as well as Command will inadvertently clear error flags. The correct pattern:

```c
uint32_t cmd_status = pci_read32(dev->bus, dev->dev, dev->fn, 0x04);
// Preserve only Command word; zero Status word before writing
cmd_status = (cmd_status & 0x0000FFFF) | PCI_CMD_BUS_MASTER;
pci_write32(dev->bus, dev->dev, dev->fn, 0x04, cmd_status);
```

Masking with `0x0000FFFF` before OR ensures the upper 16 bits (Status) are written as zero,
which is the safe value for R/WC bits — zeros do not clear anything.

### USB PCI class codes

| Class | Subclass | Prog-IF | Controller type |
|-------|----------|---------|----------------|
| 0x0C | 0x03 | 0x00 | OHCI |
| 0x0C | 0x03 | 0x10 | UHCI |
| 0x0C | 0x03 | 0x20 | EHCI |
| 0x0C | 0x03 | 0x30 | xHCI |

---

## USB Core

Source: `kernel/usb.c`, `kernel/usb_hc.h`, `kernel/usb.h`

### Host controller vtable (`usb_hc_t`)

```c
typedef struct usb_hc {
    int   (*submit_sync)      (struct usb_hc*, usb_transfer_t*);
    int   (*submit_interrupt) (struct usb_hc*, usb_transfer_t*, usb_callback_t, void*);
    int   (*port_count)       (struct usb_hc*);
    int   (*port_status)      (struct usb_hc*, int port);
    int   (*port_reset)       (struct usb_hc*, int port);
    void  (*irq_handler)      (struct usb_hc*);
    void  *priv;              // driver-private data (uhci_t* / ehci_t*)
} usb_hc_t;
```

Each HC driver fills in this vtable at init time and passes a pointer to `usb_register_hc()`.

### Device model

```c
typedef struct usb_device {
    uint8_t  addr;            // assigned USB address (1-127)
    uint8_t  speed;           // USB_SPEED_LOW / FULL / HIGH
    uint8_t  class;           // bDeviceClass from descriptor
    uint16_t vid, pid;        // vendor / product IDs
    uint8_t  depth;           // hub depth (0 = root)
    uint8_t  tt_hub_addr;     // transaction translator hub addr (HS hubs)
    uint8_t  tt_port;         // TT port on that hub
    usb_hc_t *hc;             // owning HC
    void     *class_priv;     // class driver private state
} usb_device_t;
```

Capacity: 32 devices global. Hub nesting: maximum depth 5.

### Lock-free work queue

USB hot-plug events arrive in IRQ context (EHCI port-change interrupt) or from polled callbacks
(UHCI 256 ms idle poll). Both paths call into:

```c
void usb_port_change(usb_hc_t *hc, int port);
void usb_hub_port_change(usb_device_t *hub_dev, int port);
```

These functions push a `{hc, port}` tuple onto a ring buffer without taking a lock. The main
idle loop drains the queue by calling `usb_process_pending()`.

### Enumeration sequence

`usb_process_pending()` runs the following FSM for each pending port change:

```
port_reset(hc, port)
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
driver probe match                   ← iterate class drivers, first match wins
```

### `usb_control()` — control transfers

```c
int usb_control(usb_device_t *dev,
                uint8_t bmRequestType, uint8_t bRequest,
                uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                void *data);
```

Builds an 8-byte SETUP packet and submits a 3-phase transfer (SETUP → optional DATA → STATUS)
via `hc->submit_sync`.

---

## UHCI Driver

Source: `kernel/uhci.c`

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

`kmalloc` in CupidOS returns pointers at offset `+12` from a 16-byte-aligned block header
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

Some BIOSes take ownership of USB via SMI (System Management Interrupt). CupidOS forces
handoff by writing to the LEGSUP register at PCI config offset `0xC0`:

```c
pci_write16(dev->bus, dev->dev, dev->fn, 0xC0, 0x8F00);
```

Bit 13 (`0x2000`) in `0x8F00` disables the SMI; bits `0x000F` clear pending status.

### Hot-plug detection

UHCI provides no reliable hot-plug interrupt. The driver relies on the 256 ms idle-loop tick
to call `uhci_poll_ports()`, which reads `PORTSC0`/`PORTSC1` and calls `usb_port_change()` on
any port whose `CONNECT_STATUS_CHANGE` bit is set.

---

## EHCI Driver

Source: `kernel/ehci.c`

### MMIO mapping

EHCI registers live at BAR0, a **memory BAR** (bit 0 clear). On typical PC hardware BAR0 is
above `0xFEB00000` — well beyond the 128 MB identity-map that CupidOS sets up at boot.
The driver calls:

```c
paging_map_mmio(bar0_phys, 4096);
```

to create a 4KB kernel-virtual mapping before any register access.

### Register layout

```
BAR0 + 0x00  ┌──────────────────────────────┐
             │  Capability Registers (RO)    │  length = CAPLENGTH
             │  CAPLENGTH, HCIVERSION,       │
             │  HCSPARAMS, HCCPARAMS         │
BAR0 + CAPLENGTH
             ├──────────────────────────────┤
             │  Operational Registers (R/W) │
             │  USBCMD, USBSTS, USBINTR,    │
             │  FRINDEX, CTRLDSSEGMENT,     │
             │  PERIODICLISTBASE,            │
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
// 1. Set CONFIGFLAG=1 — EHCI claims all ports
op_base->CONFIGFLAG = 1;

// 2. For each port: if LINE_STATUS indicates LS or FS device → release to UHCI
for (int p = 0; p < port_count; p++) {
    uint32_t ps = op_base->PORTSC[p];
    if (LINE_STATUS(ps) != K_HISPEED) {
        op_base->PORTSC[p] = ps | PORT_OWNER;  // hand off to companion
    }
}
```

A port with `PORT_OWNER=1` is invisible to EHCI and fully controlled by the companion UHCI.

### Async schedule

High-speed (USB 2.0) bulk and control transfers use the **asynchronous schedule**:

- Circular doubly-linked list of QHs.
- Dummy head QH (`async_head`) with `H=1` (reclamation list head bit).
- New QHs inserted before the dummy head; removed by relinking + door-bell.

### Periodic schedule

Interrupt transfers use the **periodic schedule**:

- 1024-entry frame list; each slot points to a chain of periodic QHs or iTDs.
- CupidOS uses a fixed 256 ms poll interval for HID and Hub interrupt pipes
  (slot index = `frame & 0x3FF`, inserting the same QH in every 256th slot).

---

## HID Driver

Source: `kernel/usb_hid.c`

### Probe and setup

The HID driver claims any device with `bInterfaceClass = 0x03` (HID) and
`bInterfaceSubClass = 0x01` (Boot Interface). On probe:

```c
// 1. Switch to boot protocol
usb_control(dev, 0x21, SET_PROTOCOL, 0, interface, 0, NULL);

// 2. Disable idle report — only send on change
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
Each new keycode is translated through a ~100-entry HID-usage → PS/2-scancode table and
injected via `keyboard_inject_scancode()`.

**Covered keycodes:** printable ASCII, modifier keys (Shift/Ctrl/Alt), F1–F10, cursor arrows,
Backspace, Enter, Escape, Tab, Delete, Insert, Home, End, Page Up/Down.

### Mouse report

The HID boot mouse sends a 3-byte report:

```
Byte 0: Button bitmap  (bit0=left, bit1=right, bit2=middle)
Byte 1: X displacement (signed, relative)
Byte 2: Y displacement (signed, relative)
```

Injected via `mouse_inject_event(dx, dy, buttons)`, which feeds the existing PS/2 mouse handler.
The shell and GUI window manager receive USB mouse events identically to PS/2 mouse events.

---

## Hub Driver

Source: `kernel/usb_hub.c`

### Probe and descriptor fetch

Claimed when `bDeviceClass = 0x09` (Hub). On probe:

1. `GET_DESCRIPTOR(HUB)` → 7+ byte hub descriptor.
2. Read `bNbrPorts` (number of downstream ports) and `bPwrOn2PwrGood` (power-on delay in 2ms units).
3. Call `SET_FEATURE(PORT_POWER)` for each port, then wait `bPwrOn2PwrGood * 2` ms.

### Port status change polling

The hub's status-change endpoint (interrupt IN, typically endpoint 1) is polled at **256 ms**
intervals. A set bit N in the returned bitmap means port N has a status change pending.

For each changed port:

```
1. GET_PORT_STATUS(port)         ← read wPortStatus + wPortChange
2. Check C_PORT_CONNECTION       ← connection change bit
3. CLEAR_FEATURE(C_PORT_CONNECTION)
4. Debounce 100 ms
5. If device present:
     SET_FEATURE(PORT_RESET)
     Poll for PORT_RESET to clear
     CLEAR_FEATURE(C_PORT_RESET)
     Call usb_hub_port_change(this_hub_dev, port)   ← triggers enumeration
```

### Depth cap

The maximum hub nesting depth is **5**. Before recursing into a newly found hub device, the
driver checks `parent_depth + 1 <= 5`. Devices discovered beyond depth 5 are silently ignored.
(The USB 2.0 spec allows 7; CupidOS caps at 5 for simplicity.)

### Transaction Translator (TT) routing

When a full-speed or low-speed device is connected behind a high-speed hub, the HS hub's
built-in Transaction Translator bridges speed domains. CupidOS propagates TT info to
child device descriptors:

```c
child->tt_hub_addr = hub_dev->addr;
child->tt_port     = port;
```

EHCI uses these fields to set the `PORTSC.SPLIT_EN` and `TT*` fields in the QH for that device.

---

## Mass Storage Driver

Source: `kernel/usb_msc.c`

### Bulk-Only Transport (BBB)

Every MSC operation consists of three phases:

```
Host → Device:  CBW  (31 bytes, OUT bulk endpoint)
Host ↔ Device:  DATA (optional, direction from CBW flags)
Device → Host:  CSW  (13 bytes, IN bulk endpoint)
```

CBW fields used by CupidOS:

| Field | Value |
|-------|-------|
| dCBWSignature | `0x43425355` (`"USBC"`) |
| dCBWTag | monotonically increasing tag |
| dCBWDataTransferLength | byte count for DATA phase |
| bmCBWFlags | `0x80` = device→host, `0x00` = host→device |
| bCBWLUN | 0 (single-LUN assumption) |
| bCBWCBLength | length of embedded SCSI command |
| CBWCB | SCSI Command Descriptor Block (CDB) |

CSW `bCSWStatus`: `0x00` = success, `0x01` = command failed, `0x02` = phase error.

### SCSI commands

| Command | Opcode | Direction | Use |
|---------|--------|-----------|-----|
| INQUIRY | `0x12` | D→H | Device identification |
| TEST_UNIT_READY | `0x00` | none | Check ready |
| READ_CAPACITY(10) | `0x25` | D→H | Get LBA count + block size |
| READ(10) | `0x28` | D→H | Read sectors |
| WRITE(10) | `0x2A` | H→D | Write sectors |

### Block device registration

After a successful `READ_CAPACITY`, the driver calls:

```c
block_device_t bd = {
    .name       = "usb0",          // usb1, usb2, ...
    .read_block = usb_msc_read,
    .write_block= usb_msc_write,
    .block_size = lba_block_size,
    .block_count= lba_count,
    .priv       = msc_state,
};
block_register(&bd);
```

### MBR parsing

Immediately after registration, the driver reads sector 0 and checks for the MBR signature
`0x55AA` at bytes 510–511. If found, the four 16-byte partition entries at offsets
`0x01BE`–`0x01FD` are parsed:

| Offset | Field | Size |
|--------|-------|------|
| +0 | Status (0x80 = active) | 1 |
| +4 | Partition type | 1 |
| +8 | LBA start | 4 |
| +12 | LBA count | 4 |

FAT16 partition types detected: `0x04`, `0x06`, `0x0E`.

### Auto-mount status

> **Not yet wired.** The FAT16 VFS implementation (`kernel/fat16.c`) is currently a
> single-instance driver hardcoded to the ATA block device. Mounting a second FAT16 volume
> would require per-instance state throughout the driver.
>
> **Current behaviour:** USB mass storage devices register as raw block devices (`usb0` etc.)
> and MBR partitions are detected and logged. Direct block reads/writes work. FAT16
> filesystem operations on USB drives are not available yet.
>
> **Planned fix:** Refactor `fat16.c` to carry all state in a heap-allocated instance struct
> passed through every VFS call, then wire `usb_msc_probe()` into `fat16_mount()`.

See [Filesystem.md](Filesystem.md) for the current FAT16 architecture.

---

## Shell Commands

### `usb` — list all devices

```
CupidOS> usb
[0] addr=1  speed=HIGH  vid=8086 pid=1234  class=09 parent=root
[1] addr=2  speed=FULL  vid=045e pid=0745  class=03 parent=0
[2] addr=3  speed=FULL  vid=093a pid=2510  class=03 parent=0
[3] addr=4  speed=FULL  vid=0781 pid=5567  class=08 parent=0
```

Fields: device index, assigned USB address, speed (LOW/FULL/HIGH), vendor ID, product ID,
class code, parent device (root = root hub, number = hub device index).

### `usb hubs` — hub tree view

```
CupidOS> usb hubs
depth=0  addr=1  ports=4  (root hub via EHCI)
  depth=1  addr=2  port=1  speed=FULL  (HID keyboard)
  depth=1  addr=3  port=2  speed=FULL  (HID mouse)
  depth=1  addr=4  port=3  speed=FULL  (mass storage)
```

### `usb hc` — host controller boot log

```
CupidOS> usb hc
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
  -drive if=none,id=ustick,file=test.img,format=raw \
  -device usb-storage,drive=ustick \
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
  -drive if=none,id=ustick,file=test.img,format=raw \
  -device usb-storage,drive=ustick \
  -serial stdio
```

> **Note on OHCI:** QEMU's `-device piix3-usb-uhci` emulates a UHCI controller. OHCI
> (used on VIA/SiS real hardware) is not emulated by the above flags and is not supported
> by CupidOS.

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
| Hub depth 5 | USB 2.0 spec allows 7 levels; CupidOS enforces 5. |
| EHCI+UHCI companion timing | Full-speed devices on a system with both EHCI and UHCI may occasionally fail enumeration due to companion handoff timing. UHCI-only configurations are reliable. |
| FAT16 auto-mount not wired | USB mass storage registers as raw block device only. File-level access requires the FAT16 refactor described in [Mass Storage Driver](#mass-storage-driver). |

---

## Source File Map

```
kernel/
├── pci.h          — PCI device struct, BAR helpers, class codes
├── pci.c          — pci_init, pci_find_by_class, pci_enable_bus_master
├── usb_hc.h       — usb_hc_t vtable, usb_transfer_t
├── usb.h          — usb_device_t, speed/class constants, work queue API
├── usb.c          — USB core: register_hc, enumeration FSM, usb_control
├── uhci.c         — UHCI 1.1 host controller driver
├── ehci.c         — EHCI 2.0 host controller driver
├── usb_hid.c      — HID boot protocol: keyboard + mouse class driver
├── usb_hub.c      — Hub class driver
└── usb_msc.c      — Mass storage BBB + SCSI + block_device registration
```
