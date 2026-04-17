# SMP Tier 2

CupidOS P5 Tier 2 SMP adds symmetric multiprocessing support for up to 32
logical CPUs. The design is deliberately conservative: a single shared
runqueue under a **big kernel lock (BKL)**, per-CPU LAPIC timers, and all
external IRQs routed through the BSP. This gives correct multiprocessor
boot and scheduling without the complexity of lock-free per-CPU runqueues or
full IRQ migration.

Related pages: [USB](USB.md), [Swap](Swap.md)

---

## Overview

| Property | Value |
|---|---|
| Max CPUs | 32 |
| Scheduler | shared runqueue, round-robin, BKL-protected |
| Timer source | per-CPU LAPIC periodic timer, vector 0x20 |
| External IRQs | all on BSP (keyboard, mouse, disk, …) |
| IPI vectors | 0xF0 reschedule, 0xF1 cross-CPU call, 0xFE panic |
| New source files | 13 |

New files:

```
kernel/percpu.h / percpu.c       per-CPU data, GS-base init
kernel/lapic.h  / lapic.c        Local APIC driver
kernel/ioapic.h / ioapic.c       IOAPIC driver
kernel/smp.h    / smp.c          AP trampoline, bringup, IPI wrappers
kernel/bkl.h    / bkl.c          big kernel lock (ticket spinlock)
kernel/mp.h     / mp.c           MP table + ACPI MADT discovery
bin/smp.cc                       `smp` shell command
```

---

## Boot Order (critical)

The LAPIC and IOAPIC must be live before any AP is released. The 8259 PIC
must be fully masked so that its IRQ lines do not collide with LAPIC vectors.

```
percpu_init_bsp()       // allocate BSP per_cpu_t, load GS, set cpu_id=0
lapic_init_bsp()        // map LAPIC MMIO, software-enable, calibrate PIT-ch2
ioapic_init_all()       // discover IOAPICs via MADT, build GSI table
pic_mask_all()          // OCW1=0xFF on both 8259s — all legacy IRQ lines masked
bkl_init()              // initialise ticket spinlock, per-CPU recursion counters
smp_init()              // parse MP/MADT, populate cpu_table[], write trampoline
smp_init_ipi()          // wire IPI vectors 0xF0/0xF1/0xFE in IDT
                        // --- uniprocessor regression gate ---
                        // if cpu_count == 1 → skip AP bringup entirely
                        // --- AP bringup ---
for each AP:
    lapic_send_init_ipi(apic_id)
    lapic_send_sipi(apic_id, 0x08)   // trampoline at 0x8000
    wait up to 10 ms for AP to set ap_ready flag
```

> **Warning** — reversing `lapic_init_bsp` and `ioapic_init_all` causes
> spurious LAPIC vectors during the IOAPIC redirection table write and
> locks the BSP before APs are released. Do not reorder.

---

## Per-CPU Data

### `per_cpu_t` struct (kernel/percpu.h)

```c
typedef struct {
    uint32_t  self;          // GS:0  — pointer to this struct (cheap self-ref)
    uint32_t  cpu_id;        // GS:4  — logical CPU index 0..31
    uint32_t  apic_id;       // GS:8  — APIC hardware ID
    uint32_t  current_pid;   // GS:12 — PID running on this CPU
    uint32_t  bkl_depth;     // GS:16 — BKL recursion depth
    uint32_t  bkl_irqflags;  // GS:20 — EFLAGS saved on outermost lock
    uint8_t   pad[104];      // pad to 128 bytes
} per_cpu_t;

_Static_assert(sizeof(per_cpu_t) == 128, "per_cpu_t size mismatch");
```

`this_cpu()` compiles to a single `mov eax, gs:[0]` — the struct starts
with a self-pointer so callers never need to know the linear address.

### GDT layout

| Slot | Selector | Content |
|---|---|---|
| 0 | 0x00 | null descriptor |
| 1 | 0x08 | flat code segment (ring 0) |
| 2 | 0x10 | flat data segment (ring 0) |
| 3 | 0x18 | TSS |
| 4 | 0x20 | (reserved) |
| 5–36 | 0x28–0x128 | 32 per-CPU data descriptors (one per logical CPU) |

BSP uses slot 5 (selector 0x28). AP n uses slot 5+n. The descriptor base
points to the `per_cpu_t` allocated for that CPU; limit = 127; DPL = 0;
type = data, writable.

---

## CPU Discovery

Discovery tries MP tables first and falls back to ACPI MADT.

### MP Tables

Scan in order:

1. EBDA: `[EBDA_BASE, EBDA_BASE + 1KB)`
2. Top of base memory: `0x9FC00..0x9FFFF`
3. BIOS ROM: `0xF0000..0xFFFFF`

Look for the 4-byte signature `_MP_` followed by a valid 1-byte checksum.
The floating pointer structure gives the address of the MP configuration
table. Parse entry type 0 (processor) records to build `cpu_table[]`.

If the MP table reports only the BSP (common in QEMU `-smp 1`) the code
falls through to ACPI rather than assuming the system is uniprocessor.

### ACPI MADT

```
RSDP scan (0xE0000..0xFFFFF, then EBDA)
  → RSDT (32-bit physical pointers) or XSDT (64-bit)
    → find table signature "APIC" (MADT)
      → walk variable-length entries:
          type 0  Processor Local APIC     → add to cpu_table[]
          type 1  I/O APIC                 → add to ioapic_table[]
          type 2  Interrupt Source Override → build isa_to_gsi[] remap
```

The MADT `flags` field on type-0 entries: bit 0 (Enabled) must be set, or
the CPU is skipped. Bit 1 (Online Capable) is noted but not required.

---

## Local APIC

### MMIO mapping

The LAPIC register window is 4KB at the physical address stored in
`IA32_APIC_BASE` MSR bits 31:12. `lapic_init_bsp` calls
`paging_map_mmio(phys, 4096)` to get a virtual address; all
subsequent `lapic_read` / `lapic_write` calls use that VA.

### Software enable

```c
uint32_t svr = lapic_read(0xF0);
svr |= 0x100;          // bit 8: APIC Software Enable
svr |= 0xFF;           // spurious vector = 0xFF
lapic_write(0xF0, svr);
```

### Timer calibration (PIT channel 2)

1. Program PIT channel 2 for a 10 ms one-shot count using the 1.193182 MHz
   clock (count = 11932).
2. Read the LAPIC timer current count register (0x390) before and after.
3. Difference ÷ 10 = ticks per millisecond. Store as `lapic_ticks_per_ms`.
4. Set timer divide register (0x3E0) = 0x3 (divide by 16).
5. Set initial count (0x380) = `lapic_ticks_per_ms * 10`.
6. Set LVT Timer (0x320) = `0x20020` (periodic, vector 0x20).

### EOI

```c
lapic_write(0xB0, 0);   // write any value to EOI register
```

Call this at the end of every LAPIC-delivered interrupt handler, including
the timer ISR and all IPI handlers.

### IPI send

```c
// 1. Write destination APIC ID to ICR high (offset 0x310)
lapic_write(0x310, apic_id << 24);
// 2. Write vector + delivery mode to ICR low (offset 0x300) — this fires
lapic_write(0x300, vector | (delivery_mode << 8));
// 3. Spin until delivery status bit (bit 12) clears
while (lapic_read(0x300) & (1 << 12)) {}
```

---

## IOAPIC

Each IOAPIC has an index/data register pair. Reads and writes go through:

```c
void ioapic_write(uint32_t base_va, uint8_t reg, uint32_t val) {
    *(volatile uint32_t *)(base_va + 0x00) = reg;   // IOREGSEL
    *(volatile uint32_t *)(base_va + 0x10) = val;   // IOWIN
}
```

### Redirection table

Each GSI has a 64-bit redirection entry split across two 32-bit registers
`0x10 + 2*gsi` (low) and `0x11 + 2*gsi` (high).

```
high[31:24]  destination APIC ID (BSP = 0)
low[16]      mask bit (1 = masked)
low[15]      trigger mode (0 = edge, 1 = level)
low[13]      polarity (0 = high, 1 = low)
low[10:8]    delivery mode (000 = fixed)
low[7:0]     vector
```

At init, all entries are masked. `ioapic_route(gsi, vector)` unmasks the
entry, sets delivery mode fixed, destination = BSP APIC ID.

### ISA → GSI remap

ACPI MADT type-2 (Interrupt Source Override) records map legacy ISA IRQ
numbers to GSIs. For example, ISA IRQ 0 (PIT) typically remaps to GSI 2.
The kernel maintains an `isa_to_gsi[16]` table initialised to identity
(gsi = isa) and then patched by MADT overrides. `ioapic_route_isa(irq,
vector)` looks up `isa_to_gsi[irq]` before routing.

### 8259 masking

```c
void pic_mask_all(void) {
    outb(0x21, 0xFF);   // master OCW1 — mask all 8 lines
    outb(0xA1, 0xFF);   // slave  OCW1 — mask all 8 lines
}
```

Called once after `ioapic_init_all`. The 8259 is never unmasked again;
all IRQ routing goes through the IOAPIC→LAPIC path. EOI no longer goes to
the 8259.

---

## AP Trampoline

The trampoline is a 4KB raw binary placed at physical address 0x8000. The
BSP writes it there before sending SIPI. It runs in 16-bit real mode then
transitions each AP to 32-bit protected mode.

### Trampoline stages

```nasm
; Stage 1 – real mode entry at 0x8000
    cli
    xor  ax, ax
    mov  ds, ax
    lgdt [gdtr_flat]           ; load temporary flat GDT
    mov  eax, cr0
    or   al, 1
    mov  cr0, eax
    jmp  0x08:tramp32          ; far jump → protected mode

; Stage 2 – 32-bit flat mode
tramp32:
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  ss, ax

    ; Read own APIC ID from LAPIC MMIO (BSP mapped it before SIPI)
    mov  eax, [LAPIC_VA + 0x20]
    shr  eax, 24               ; APIC ID in bits 31:24

    ; Look up logical cpu_id in BSP-populated apic_to_cpu[] table
    mov  ecx, [apic_to_cpu + eax*4]

    ; Load idle stack pointer
    mov  esp, [idle_stacks + ecx*4]

    ; Set GS = 0x10 (flat data) temporarily, ap_main_c fixes it
    mov  ax, 0x10
    mov  gs, ax

    call ap_main_c             ; → loads real kernel GDT + per-CPU GS
```

### `ap_main_c` (C side)

```c
void ap_main_c(uint32_t cpu_id) {
    percpu_init_ap(cpu_id);    // allocate per_cpu_t, set GS selector
    lapic_init_ap();           // enable LAPIC, start periodic timer
    bkl_acquire();
    scheduler_ap_idle();       // enter idle loop, release BKL each round
}
```

---

## Big Kernel Lock

The BKL is a **ticket spinlock** with per-CPU recursion tracking and IRQ
save on the outermost acquire.

### Data structure

```c
typedef struct {
    volatile uint32_t next_ticket;
    volatile uint32_t now_serving;
} ticket_lock_t;
```

### Acquire / release

```c
void bkl_acquire(void) {
    uint32_t cpu = this_cpu()->cpu_id;
    if (this_cpu()->bkl_depth > 0) {   // recursive — just increment depth
        this_cpu()->bkl_depth++;
        return;
    }
    // save EFLAGS and disable interrupts (outermost only)
    this_cpu()->bkl_irqflags = read_eflags();
    cli();
    uint32_t my_ticket = atomic_fetch_add(&bkl.next_ticket, 1);
    while (bkl.now_serving != my_ticket) { __asm__ volatile("pause"); }
    this_cpu()->bkl_depth = 1;
}

void bkl_release(void) {
    this_cpu()->bkl_depth--;
    if (this_cpu()->bkl_depth > 0) return;
    bkl.now_serving++;
    write_eflags(this_cpu()->bkl_irqflags);   // restore interrupts
}
```

### What is wrapped

| Subsystem | Functions |
|---|---|
| Scheduler | `schedule`, `scheduler_tick` |
| Process | `process_create`, `process_exit`, `process_block`, `process_unblock` |
| Memory | `kmalloc`, `kfree`, `pmm_alloc`, `pmm_free` |
| I/O | `serial_printf`, `klog` |

---

## Scheduler Integration

`process_t` gains two new fields:

```c
uint8_t  on_cpu;     // logical cpu_id currently executing this process
uint8_t  last_cpu;   // logical cpu_id last time it ran (for NUMA hints)
```

`current_pid` moves from a global variable into `per_cpu_t.current_pid`.
Each CPU reads `this_cpu()->current_pid` to identify its own running
process; the global accessor `get_current_pid()` compiles to
`mov eax, gs:[12]`.

APs run `schedule()` inside their idle loop:

```c
void scheduler_ap_idle(void) {
    while (1) {
        bkl_acquire();
        schedule();
        bkl_release();
        __asm__ volatile("hlt");   // wait for next LAPIC timer tick
    }
}
```

---

## IPIs

### Vectors

| Vector | Purpose |
|---|---|
| 0xF0 | Reschedule — target CPU calls `schedule()` on next tick |
| 0xF1 | Cross-CPU function call — target runs `fn(arg)` atomically |
| 0xFE | Panic — target halts and prints its CPU id |

### Handler skeleton

```nasm
ipi_reschedule:
    pushal
    call   ipi_reschedule_c   ; sets per-CPU reschedule_pending flag
    popal
    mov    dword [LAPIC_VA + 0xB0], 0   ; EOI
    iret
```

### Cross-CPU call

```c
void smp_call_on_cpu(uint32_t cpu, void (*fn)(void *), void *arg) {
    // populate shared call_req[cpu] struct
    call_req[cpu].fn   = fn;
    call_req[cpu].arg  = arg;
    call_req[cpu].done = 0;
    lapic_send_ipi(cpu_table[cpu].apic_id, 0xF1);
    while (!call_req[cpu].done) { __asm__ volatile("pause"); }
}
```

---

## Shell Commands

The `smp` command (bin/smp.cc) provides two views:

```
smp              List all CPUs with status
smp info         Show BKL ticket counts and current CPU for the shell process
```

Example output:

```
CPU  APIC  STATUS
  0     0  online (BSP)
  1     1  online
  2     2  online
  3     3  online

BKL: now_serving=1402 next_ticket=1402 (unlocked)
shell running on cpu 0
```

---

## Testing

### `make run-smp`

Boots with 4 CPUs in QEMU:

```bash
make run-smp
```

Equivalent to:

```bash
qemu-system-i386 <common-flags> -smp cpus=4 -serial stdio
```

All 4 APs should appear in `smp` output within a few hundred milliseconds
of boot.

### `feature20_smp`

An atomic increment stress test:

```c
// 4 threads each increment a shared counter 10000 times
// under the BKL. Expected final value: 40000.
feature20_smp
```

Run it and verify the final counter value matches 40000.

### X_VERIFY cross-CPU call probe

```
X_VERIFY
```

Sends a cross-CPU call from CPU 0 to CPU 1 and waits for the done flag.
Pass = "cross-CPU call OK" printed to serial.

---

## Known Limits

| Limitation | Notes |
|---|---|
| No CPU hotplug | cpu_table[] is frozen at boot |
| No NUMA awareness | all memory allocated from a single pool |
| No TLB shootdown | paging changes not propagated to other CPUs |
| No per-CPU runqueues | single shared runqueue under BKL caps parallelism |
| All IRQs on BSP | IRQ migration not implemented |
| No MWAIT idle | APs use HLT in idle loop |
| BKL serialisation | only one CPU in kernel at a time |

For workloads that are mostly user-space compute with occasional kernel calls
the BKL overhead is acceptable. Workloads with heavy concurrent kernel entry
(e.g., many processes doing simultaneous disk I/O) will serialize at the BKL.
Replacing the BKL with fine-grained locking is the natural next step for
Tier 3.

---

## Source File Index

| File | Purpose |
|---|---|
| `kernel/percpu.h` | `per_cpu_t` struct definition, `this_cpu()` macro |
| `kernel/percpu.c` | BSP + AP per-CPU init, GDT slot allocation |
| `kernel/lapic.h` | LAPIC register offsets, API declarations |
| `kernel/lapic.c` | MMIO map, software enable, PIT calibration, IPI send |
| `kernel/ioapic.h` | IOAPIC register layout, API |
| `kernel/ioapic.c` | Redirection table init, GSI routing, ISA remap |
| `kernel/smp.h` | `cpu_table_t`, AP trampoline API |
| `kernel/smp.c` | Trampoline placement, INIT/SIPI sequence, idle loop |
| `kernel/bkl.h` | `bkl_acquire` / `bkl_release` declarations |
| `kernel/bkl.c` | Ticket spinlock implementation |
| `kernel/mp.h` | MP table + ACPI MADT parser API |
| `kernel/mp.c` | `_MP_` scan, MADT walk, cpu/ioapic/gsi table build |
| `bin/smp.cc` | `smp` and `smp info` shell commands |
