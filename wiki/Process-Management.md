# Process Management

cupid-os implements deferred preemptive multitasking with round-robin scheduling. Up to 32 kernel threads share a flat 32-bit address space, all running in ring 0.

---

## Overview

| Property | Value |
|----------|-------|
| Scheduling algorithm | Round-robin |
| Timer tick | 5ms (PIT channel 0 at 200Hz) |
| Max processes | 32 |
| Address space | Flat, shared (ring 0) |
| Context switch | Pure assembly (`context_switch.asm`) |
| Preemption model | Deferred (flag-based) |

---

## Process States

```
         ┌──────────┐
         │  READY   │◄────────────────────┐
         └────┬─────┘                     │
              │ schedule()                │ yield() / timer
              ▼                           │
         ┌──────────┐              ┌──────┴─────┐
         │ RUNNING  │──────────────│   READY    │
         └────┬─────┘              └────────────┘
              │ exit() / kill()
              ▼
         ┌──────────┐
         │TERMINATED│ -> detach from CPU -> quiescent reaper cleanup
         └──────────┘
```

| State | Description |
|-------|-------------|
| `READY` | Waiting to be scheduled |
| `RUNNING` | Currently executing on CPU |
| `BLOCKED` | Waiting for an event (future use) |
| `TERMINATED` | Finished, stack pending cleanup |

---

## Deferred Preemptive Scheduling

cupid-os does **not** context switch inside interrupt handlers. Instead:

1. **PIT IRQ0** fires every 5ms -> sets `need_reschedule = 1`
2. The flag is checked only at **safe voluntary points**:
   - Desktop main loop (before `HLT` instruction)
   - `process_yield()` calls
   - Idle process loop
3. If the flag is set, `schedule()` is called to switch to the next ready process

This approach avoids the complexity and stack corruption risks of switching inside ISRs.

---

## Context Switching

The context switch is implemented in pure x86 assembly (`context_switch.asm`):

```
context_switch(process_t *old_proc, process_t *new_proc,
               uint32_t resume_eflags)
```

### What It Does

1. **Save** the scheduler caller's pre-BKL EFLAGS and callee-saved registers on the current stack
2. **FXSAVE** the current x87/SSE state into `old_proc->fp_state`
3. **Store** ESP in `old_proc->context.esp`
4. **Load** `new_proc->context.esp` and **FXRSTOR** its x87/SSE state
5. Release the scheduler's sole BKL acquisition through `bkl_context_switch_release()` on the new stack
6. Restore the target's interrupt policy only after the new stack and FPU state are active
7. **Jump** to `new_proc->context.eip` (either `context_switch_resume` or a new entry point)

The handoff is deliberately split across C and assembly. `schedule()` may switch only when its BKL depth is exactly one; a nested request sets the CPU-local pending bit and is retried by the outer `bkl_unlock()`. This prevents a suspended caller's critical section from becoming owned by the unrelated process selected next. A no-switch scheduler call uses the ordinary unlock path, while a real switch releases exactly once on the target stack.

### Resume Path

The `context_switch_resume` label pops saved registers in reverse order and does `ret`, returning normally to wherever the process left off.

### New Process Startup

New processes are set up so that:
- ESP points to a prepared stack containing the optional entry argument and
  the trampoline return address; saved registers are created only after a
  running process is switched out
- EIP is the process entry function
- `process_exit_trampoline` is the return address on the stack (auto-cleanup when function returns)

---

## Process Table

A static array of 32 Process Control Blocks (PCBs):

```c
typedef struct {
    uint32_t pid;                       // Process ID (1-based)
    process_state_t state;              // READY/RUNNING/BLOCKED/TERMINATED
    cpu_context_t context;              // Saved registers, ESP, and EIP
    uint8_t fp_state[512];               // 16-byte-aligned FXSAVE image
    void *stack_base;
    uint32_t stack_size;
    uint32_t image_base, image_size;
    process_image_ownership_t image_ownership;
    uint32_t image_lease_generation;
    process_domain_t domain;
    char name[32];
    uint8_t on_cpu;                     // 0xFF only after scheduler detach
    uint8_t last_cpu;
} process_t;
```

Processes are indexed by `PID - 1`.

---

## Key Processes

| PID | Name | Description |
|-----|------|-------------|
| 1 | idle | Always present, never exits. Runs `STI; HLT` in a loop. |
| 2 | desktop | Main kernel thread, registered via `process_register_current()` |
| 3 | terminal | GUI terminal, spawned via `process_create()` |
| 4+ | user | Test processes, spawned via `spawn` command |

---

## Stack Management

### Stack Allocation
- Each process gets its own stack via `kmalloc(stack_size)`
- Default stack size: 32 KiB (configurable via `DEFAULT_STACK_SIZE`)

### Stack Canary
- A canary value (`0xDEADC0DE`) is placed at the bottom of every stack
- Checked on **every context switch**
- If corrupted, the process is marked terminated; its current CPU switches
  away before the reaper may release the stack or executable-image lease

### Deferred Stack Freeing
- A terminated process keeps its stack and image while `on_cpu` identifies an
  executing CPU
- The outgoing CPU publishes `on_cpu = 0xFF` while holding the BKL as part of
  the stack switch; the BKL is released only after target ESP/FXRSTOR, and only
  then may a reaper free the old stack, release an external image lease, and
  clear the PCB

### Remote Reschedule

`process_kill()` marks a process running on another CPU terminated, publishes
that CPU's `reschedule_pending` flag, and then raises IPI `0xF0`. The IPI
handler acknowledges the LAPIC and consumes the already-published request
without re-arming it. If the target CPU owns an outer BKL critical section,
the request remains pending until that outer unlock; otherwise it switches at
the IPI suspension point and later returns through the original `IRET` frame.

---

## Process API

### Creating Processes

```c
uint32_t pid = process_create(entry_function, "name", stack_size);
```

- Allocates a stack, sets up initial context
- Process starts in `READY` state
- Returns PID (0 on failure)

### Registering Current Thread

```c
process_register_current("desktop");
```

Used to register an already-running thread (like the main kernel thread) in the process table.

### Yielding

```c
process_yield();
```

Voluntarily gives up the CPU. Clears the reschedule flag and calls `schedule()`.

### Exiting

```c
process_exit();
```

Marks the process as `TERMINATED` and reschedules without clearing its current
CPU ownership. The scheduler detaches it during the actual context switch;
the quiescent reaper later frees its stack and releases any external image
lease. Also called automatically when a process entry function returns.

### Killing

```c
process_kill(pid);
```

Marks any process terminated by PID. A target already detached from every CPU
is reclaimed immediately; an executing local or remote target retains its
stack and image until its owning CPU switches away. PID 1 cannot be killed.

### Listing

```c
process_list();
```

Prints all processes with PID, state, and name. Used by the `ps` shell command.

---

## Shell Commands

| Command | Description |
|---------|-------------|
| `ps` | List all processes with PID, state, and name |
| `kill <pid>` | Terminate a process (cannot kill PID 1) |
| `spawn [n]` | Create 1-16 test counting processes |
| `yield` | Voluntarily yield CPU to next process |

### Example

```
> ps
  PID  STATE     NAME
    1  READY     idle
    2  RUNNING   desktop
    3  READY     terminal

> spawn 2
Spawned PID 4
Spawned PID 5

> ps
  PID  STATE     NAME
    1  READY     idle
    2  RUNNING   desktop
    3  READY     terminal
    4  READY     test
    5  READY     test

> kill 4
Killing PID 4...
```

---

## See Also

- [Architecture](Architecture) - How the scheduler fits into the system
- [Desktop Environment](Desktop-Environment) - Desktop as PID 2
- [Debugging](Debugging) - Stack traces and crash testing
