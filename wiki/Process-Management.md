# Process Management

cupid-os implements deferred preemptive multitasking with round-robin scheduling. Up to 32 kernel threads share a flat 32-bit address space, all running in ring 0.

---

## Overview

| Property | Value |
|----------|-------|
| Scheduling algorithm | Round-robin |
| Time slice | 10ms (PIT at 100Hz) |
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
         │TERMINATED│ → stack freed lazily on slot reuse
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

1. **PIT IRQ0** fires every 10ms → sets `need_reschedule = 1`
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
context_switch(uint32_t *old_esp, uint32_t new_esp, uint32_t new_eip)
```

### What It Does

1. **Save** callee-saved registers: push EFLAGS, EBX, ESI, EDI, EBP onto current stack
2. **Store** the resulting ESP into `*old_esp` (old process's PCB)
3. **Load** new ESP from `new_esp` parameter
4. **Jump** to `new_eip` (either `context_switch_resume` or process entry point)

### Resume Path

The `context_switch_resume` label pops saved registers in reverse order and does `ret`, returning normally to wherever the process left off.

### New Process Startup

New processes are set up so that:
- ESP points to a prepared stack with fake saved registers
- EIP is the process entry function
- `process_exit_trampoline` is the return address on the stack (auto-cleanup when function returns)

---

## Process Table

A static array of 32 Process Control Blocks (PCBs):

```c
typedef struct {
    uint32_t pid;           // Process ID (1-based)
    process_state_t state;  // READY, RUNNING, BLOCKED, TERMINATED
    uint32_t esp;           // Saved stack pointer
    uint32_t eip;           // Saved instruction pointer
    uint32_t stack_base;    // Bottom of allocated stack
    uint32_t stack_size;    // Stack size in bytes
    char name[32];          // Process name for display
} pcb_t;
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
- Default stack size: 4KB (configurable via `DEFAULT_STACK_SIZE`)

### Stack Canary
- A canary value (`0xDEADC0DE`) is placed at the bottom of every stack
- Checked on **every context switch**
- If corrupted → kernel panic with process identification

### Deferred Stack Freeing
- When a process is terminated, its stack is **not freed immediately** (because we may still be using it)
- Stack is freed **lazily** when the PCB slot is reused for a new process

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

Marks the process as `TERMINATED`, defers stack free, and reschedules. Also called automatically when a process's entry function returns (via the exit trampoline).

### Killing

```c
process_kill(pid);
```

Terminates any process by PID. Cannot kill PID 1 (idle process).

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
| `spawn [n]` | Create 1–16 test counting processes |
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

- [Architecture](Architecture) — How the scheduler fits into the system
- [Desktop Environment](Desktop-Environment) — Desktop as PID 2
- [Debugging](Debugging) — Stack traces and crash testing
