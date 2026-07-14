# SMP Tier 2

CupidOS supports symmetric multiprocessing on up to 32 logical CPUs. It uses a
single shared runqueue under a **big kernel lock (BKL)**, per-CPU LAPIC timers,
and routes external IRQs through the BSP. The implementation does not use
per-CPU runqueues or IRQ migration.

Related pages: [USB](USB.md), [Swap](Swap.md)

---

## Overview

| Property | Value |
|---|---|
| Max CPUs | 32 |
| Scheduler | shared runqueue, round-robin, BKL-protected |
| Timer source | per-CPU LAPIC periodic timer, vector 0x20 |
| External IRQs | all on BSP (keyboard, mouse, disk, ...) |
| IPI vectors | 0xF0 reschedule, 0xF1 cross-CPU call, 0xFE panic |
| Per-CPU selector | `%gs`, one 128-byte `per_cpu_t` per logical CPU |

Core files:

```
kernel/smp/percpu.h / percpu.c       per-CPU data, GS-base init
kernel/smp/lapic.h  / lapic.c        Local APIC driver
kernel/smp/ioapic.h / ioapic.c       IOAPIC driver
kernel/smp/smp.h    / smp.c          AP trampoline, bringup, IPI wrappers
kernel/smp/bkl.h    / bkl.c          big kernel lock (ticket spinlock)
kernel/smp/mp_tables.h / .c      MP table discovery
kernel/smp/acpi.h / acpi.c       ACPI MADT fallback
kernel/lang/shell.c              `smp` shell command
```

---

## Boot Order (critical)

The LAPIC and IOAPIC must be live before any AP is released. The 8259 PIC
must be fully masked so that its IRQ lines do not collide with LAPIC vectors.

```
percpu_init_bsp()       // initialise static per_cpu_t array/GDT, load BSP GS
lapic_init_bsp()        // map LAPIC MMIO, software-enable, calibrate PIT-ch2
pic_mask_all()          // OCW1=0xFF on both 8259s - all legacy IRQ lines masked
ioapic_init_all(bsp_id) // discover IOAPICs via MADT, build GSI table
keyboard_init()         // install handler only after IOAPIC setup
bkl_init()              // initialise ticket spinlock, per-CPU recursion counters
smp_init()              // parse MP/MADT, populate cpu_table[], write trampoline
smp_init_ipi()          // wire IPI vectors 0xF0/0xF1/0xFE in IDT
                        // --- AP bringup ---
for each AP:
    send INIT, wait 10 ms
    send SIPI vector 0x08 twice      // trampoline at 0x8000
    wait up to 100 ms for cpus[i].online
```

> Reversing `lapic_init_bsp` and `ioapic_init_all` causes
> spurious LAPIC vectors during the IOAPIC redirection table write and
> locks the BSP before APs are released. Do not reorder.

---

## Per-CPU Data

### `per_cpu_t` struct (kernel/smp/percpu.h)

```c
typedef struct per_cpu_t {
    struct per_cpu_t *self_ptr; // GS:0x00
    uint8_t cpu_id;              // GS:0x04, logical CPU index
    uint8_t apic_id;             // GS:0x05, hardware APIC ID
    uint8_t bootstrap;           // GS:0x06, BSP marker
    uint8_t online;              // GS:0x07
    process_t *current;          // GS:0x08, reserved task pointer
    process_t *idle;             // GS:0x0C, reserved idle pointer
    uint32_t bkl_depth;          // GS:0x10, mirrored global BKL depth
    uint64_t preempt_count;      // GS:0x14
    uint8_t *idle_stack_top;     // GS:0x1C, AP bootstrap stack
    uint32_t bkl_eflags_saved;   // GS:0x20, outer caller EFLAGS
    uint32_t current_pid;        // GS:0x24, 0 means CPU-local idle loop
    void (*call_fn)(void *);     // GS:0x28, cross-CPU call
    void *call_arg;              // GS:0x2C
    uint8_t call_pending;        // GS:0x30
    uint8_t call_done;           // GS:0x31
    uint8_t reschedule_pending;  // GS:0x32
    uint8_t interrupt_depth;     // GS:0x33
    uint8_t _pad[74];
} per_cpu_t;

_Static_assert(sizeof(per_cpu_t) == 128, "per_cpu_t size mismatch");
```

`this_cpu()` compiles to a single `mov eax, gs:[0]`. The struct starts with a
self-pointer, so callers do not need its linear address.

### GDT layout

| Slot | Selector | Content |
|---|---|---|
| 0 | 0x00 | null descriptor |
| 1 | 0x08 | flat code segment (ring 0) |
| 2 | 0x10 | flat data segment (ring 0) |
| 3 | 0x18 | reserved |
| 4 | 0x20 | (reserved) |
| 5-36 | 0x28-0x128 | 32 per-CPU data descriptors (one per logical CPU) |

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
  -> RSDT (32-bit physical pointers) or XSDT (64-bit)
    -> find table signature "APIC" (MADT)
      -> walk variable-length entries:
          type 0  Processor Local APIC     -> add to cpu_table[]
          type 1  I/O APIC                 -> add to ioapic_table[]
          type 2  Interrupt Source Override -> build isa_to_gsi[] remap
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
// 2. Write vector + delivery mode to ICR low (offset 0x300) - this fires
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

### ISA -> GSI remap

ACPI MADT type-2 (Interrupt Source Override) records map legacy ISA IRQ
numbers to GSIs. For example, ISA IRQ 0 (PIT) typically remaps to GSI 2.
The kernel maintains an `isa_to_gsi[16]` table initialised to identity
(gsi = isa) and then patched by MADT overrides. `ioapic_route_isa(irq,
vector)` looks up `isa_to_gsi[irq]` before routing.

### 8259 masking

```c
void pic_mask_all(void) {
    outb(0x21, 0xFF);   // master OCW1 - mask all 8 lines
    outb(0xA1, 0xFF);   // slave  OCW1 - mask all 8 lines
}
```

Called once after `ioapic_init_all`. The 8259 is never unmasked again;
all IRQ routing goes through the IOAPIC->LAPIC path. EOI no longer goes to
the 8259.

---

## AP Trampoline

The trampoline is a 4KB raw binary placed at physical address 0x8000. The
BSP writes it there before sending SIPI. It runs in 16-bit real mode then
transitions each AP to 32-bit protected mode.

### Trampoline stages

```nasm
; Stage 1 - real mode entry at 0x8000
    cli
    xor  ax, ax
    mov  ds, ax
    lgdt [gdtr_flat]           ; load temporary flat GDT
    mov  eax, cr0
    or   al, 1
    mov  cr0, eax
    jmp  0x08:tramp32          ; far jump -> protected mode

; Stage 2 - 32-bit flat mode
tramp32:
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  ss, ax

    ; Read own APIC ID from LAPIC MMIO (BSP mapped it before SIPI)
    mov  eax, [LAPIC_VA + 0x20]
    shr  eax, 24               ; APIC ID in bits 31:24

    ; Scan apic_to_cpu_id[32] for the logical CPU slot
    ; Load temporary GS=0x10 and idle_stack_tops[cpu_id]

    call ap_main_c             ; -> loads real kernel GDT + per-CPU GS
```

### `ap_main_c` (C side)

```c
void ap_main_c(void) {
    fpu_init_cpu();            // first operation: no logging or XMM use
    uint8_t my_apic = lapic_get_id();
    int cpu_id = 0;
    for (int j = 1; j < smp_cpu_count_var; j++) {
        if (cpus[j].apic_id == my_apic) { cpu_id = j; break; }
    }
    percpu_load_kernel_gdt(cpu_id);
    this_cpu()->online = 1;
    idt_load_ap();
    lapic_init_ap();
    __asm__ volatile("sti");
    for (;;) {
        schedule();
        __asm__ volatile("sti; hlt");
    }
}
```

CR0 and CR4 are per logical CPU, so the BSP cannot enable SSE for an AP.
`fpu_init_cpu()` is compiled with `target("no-sse,no-sse2")`, performs no
logging, and establishes CR0/CR4, `FNINIT`, and MXCSR before ordinary AP C
code can execute compiler-generated SSE.

---

## Big Kernel Lock

The BKL is a **ticket spinlock** with per-CPU recursion tracking and IRQ
save on the outermost acquire.

### Data structure

```c
typedef struct {
    volatile uint32_t ticket_head;
    volatile uint32_t ticket_tail;
    int32_t owner_cpu;
    uint32_t depth;
} bkl_t;
```

### Acquire / release

```c
void bkl_lock(void) {
    uint32_t eflags = save_and_cli();
    per_cpu_t *cpu = this_cpu();
    if (klock.owner_cpu == cpu->cpu_id) {
        klock.depth++;
        cpu->bkl_depth = klock.depth;
        return;
    }
    uint32_t ticket = atomic_fetch_add(&klock.ticket_tail, 1);
    while (klock.ticket_head != ticket) pause();
    klock.owner_cpu = cpu->cpu_id;
    klock.depth = 1;
    cpu->bkl_depth = 1;
    cpu->bkl_eflags_saved = eflags;
}

void bkl_unlock(void) {
    per_cpu_t *cpu = this_cpu();
    if (klock.depth == 0 || klock.owner_cpu != cpu->cpu_id) return;
    uint32_t eflags = cpu->bkl_eflags_saved;
    klock.depth--;
    cpu->bkl_depth = klock.depth;
    if (klock.depth != 0) return;
    klock.owner_cpu = -1;
    cpu->bkl_eflags_saved = 0;
    atomic_fetch_add(&klock.ticket_head, 1);
    process_reschedule_if_pending();
    restore_if(eflags);
}
```

A real switch uses a different release path. `schedule()` may transfer only
its sole BKL acquisition. After it publishes the outgoing PCB detached
(`on_cpu = 0xFF`), `context_switch.asm` loads the target ESP and FPU state,
then calls `bkl_context_switch_release()` on the target stack with interrupts
still disabled. The target's saved interrupt policy is restored only after
that handoff. Nested scheduling requests remain CPU-local and are consumed by
the outer `bkl_unlock()`.

### What is wrapped

| Subsystem | Functions |
|---|---|
| Scheduler | `schedule`, `scheduler_tick` |
| Process | `process_create`, `process_exit`, `process_block`, `process_unblock` |
| Memory | `kmalloc`, `kfree`, `pmm_alloc`, `pmm_free` |
| I/O | `serial_printf`, `klog` |

---

## Scheduler Integration

`process_t` contains two SMP ownership fields:

```c
uint8_t  on_cpu;     // logical cpu_id currently executing this process
uint8_t  last_cpu;   // logical cpu_id last time it ran (for NUMA hints)
```

`per_cpu_t.current_pid` stores the running process for each CPU. Each CPU reads
`this_cpu()->current_pid` to identify its own running
process. The field is at offset `0x24`; `process_get_current_pid()` loads it
through the per-CPU pointer selected by `%gs`.

Only a detached `READY` PCB (`on_cpu == 0xFF`) may migrate. PID 1's single
stack is a BSP-only fallback. Each AP enters its inline schedule / `STI; HLT`
loop with `current_pid == 0`; its first dispatch saves that PID-less bootstrap
stack and FPU state in `cpu_idle_contexts[cpu_id]`. A blocked or terminated AP
task can switch back to its own CPU-local context and detach safely for the
quiescent reaper without borrowing PID 1 or another CPU's stack.

---

## Interrupt Entry and Per-CPU GS

Common exception and IRQ entry keeps the live per-CPU `%gs` selector while C
runs; only `%ds`, `%es`, and `%fs` are replaced with the flat data selector.
Entry increments `interrupt_depth`, so BKL unlock and direct `schedule()` calls
leave a request pending until the handler completes and, for IRQs,
`irq_handler()` has sent the LAPIC EOI. Common exit then decrements the depth
and calls `process_reschedule_if_pending()` at an explicit suspension point.

The saved `%gs` frame slot is discarded rather than reloaded. A suspended
frame may resume on a different CPU and must retain that destination CPU's
live selector; restoring the source selector would make `this_cpu()` address
the wrong `per_cpu_t`.

---

## IPIs

### Vectors

| Vector | Purpose |
|---|---|
| 0xF0 | Reschedule - target CPU calls `schedule()` on next tick |
| 0xF1 | Cross-CPU function call - target runs `fn(arg)` atomically |
| 0xFE | Panic - target halts and prints its CPU id |

### Handler skeleton

```nasm
ipi_reschedule_stub:
    pushal
    call   ipi_reschedule_c   ; EOI, then consume the published request
    popal
    iret
```

The sender stores `target.reschedule_pending` before raising the IPI. The
handler does not re-arm the request: it sends EOI and calls the process safe
point, which either switches from the IPI frame or leaves the request for the
outer BKL unlock.

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

The `smp` command is implemented by `shell_smp_cmd` in
`kernel/lang/shell.c` and provides two views:

```
smp              List per-CPU APIC, online, preemption, and PID state
smp info         Show whether this CPU holds the BKL, CPU count, and CPU id
```

Example output:

```
[0] apic=0 online=1 preempts=0 current_pid=2
[1] apic=1 online=1 preempts=0 current_pid=0
[2] apic=2 online=1 preempts=0 current_pid=0
[3] apic=3 online=1 preempts=0 current_pid=0

bkl: free
cpus: 4
me: 0
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

## Known Limits

| Limitation | Notes |
|---|---|
| No CPU hotplug | cpu_table[] is frozen at boot |
| No NUMA awareness | all memory allocated from a single pool |
| No TLB shootdown | paging changes not propagated to other CPUs |
| No per-CPU runqueues | single shared runqueue under BKL caps parallelism |
| External IOAPIC IRQs target the BSP | IRQ affinity/migration is not implemented; every CPU still receives its LAPIC timer |
| No MWAIT idle | APs use HLT in idle loop |
| BKL serialisation | only one CPU in kernel at a time |
| Timer callback cadence | Every active logical callback currently runs on each LAPIC timer tick in hard-IRQ context; frequency-aware deferred work remains |

For workloads that are mostly user-space compute with occasional kernel calls
the BKL overhead is acceptable. Workloads with heavy concurrent kernel entry
(e.g., many processes doing simultaneous disk I/O) will serialize at the BKL.
Fine-grained locking would be required to remove this serialization.

---

## Source File Index

| File | Purpose |
|---|---|
| `kernel/smp/percpu.h` | `per_cpu_t` struct definition, `this_cpu()` macro |
| `kernel/smp/percpu.c` | BSP + AP per-CPU init, GDT slot allocation |
| `kernel/smp/lapic.h` | LAPIC register offsets, API declarations |
| `kernel/smp/lapic.c` | MMIO map, software enable, PIT calibration, IPI send |
| `kernel/smp/ioapic.h` | IOAPIC register layout, API |
| `kernel/smp/ioapic.c` | Redirection table init, GSI routing, ISA remap |
| `kernel/smp/smp.h` | `cpu_table_t`, AP trampoline API |
| `kernel/smp/smp.c` | Trampoline placement, INIT/SIPI sequence, idle loop |
| `kernel/smp/bkl.h` | `bkl_lock` / `bkl_unlock` and target-stack handoff declarations |
| `kernel/smp/bkl.c` | Ticket spinlock implementation |
| `kernel/smp/mp_tables.h` | MP table parser API |
| `kernel/smp/mp_tables.c` | `_MP_` scan and CPU/IOAPIC discovery |
| `kernel/smp/acpi.h` / `kernel/smp/acpi.c` | ACPI RSDP/RSDT/XSDT/MADT fallback |
| `kernel/lang/shell.c` | `smp` and `smp info` shell commands |
