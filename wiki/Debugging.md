# Debugging & Memory Safety

cupid-os provides extensive debugging facilities through serial logging, memory safety checks, and introspection commands.

---

## Serial Console

All kernel debug output is sent to **COM1** at **115200 baud**, making it visible in QEMU's terminal or any serial capture tool.

### Configuration

| Parameter | Value |
|-----------|-------|
| Port | COM1 (0x3F8) |
| Baud rate | 115200 |
| Data bits | 8 |
| Stop bits | 1 |
| Parity | None |

### QEMU Setup

```bash
qemu-system-i386 -serial stdio ...
```

This prints all kernel debug output to your host terminal.

---

## Debug Macros

Four severity levels are available, defined in `kernel/debug.h`:

| Macro | Level | Prefix | Usage |
|-------|-------|--------|-------|
| `KDEBUG(fmt, ...)` | Verbose | `[DEBUG]` | Detailed tracing |
| `KINFO(fmt, ...)` | Info | `[INFO]` | Status messages |
| `KWARN(fmt, ...)` | Warning | `[WARN]` | Non-critical issues |
| `KERROR(fmt, ...)` | Error | `[ERROR]` | Serious problems |

### Usage

```c
KDEBUG("Process %u switching to %u", old_pid, new_pid);
KINFO("FAT16 filesystem mounted, %u clusters", total_clusters);
KWARN("Block cache full, evicting LRU entry");
KERROR("Double free detected at %p", ptr);
```

### Log Level Control

The runtime log level can be changed with the `loglevel` shell command:

```
> loglevel 0    # Show all messages (DEBUG and above)
> loglevel 2    # Show WARN and ERROR only
> loglevel 3    # Show ERROR only
```

### Log Buffer

The kernel maintains a circular log buffer in memory. Use `logdump` to view recent log messages:

```
> logdump
[INFO] VGA initialized: 320x200x256
[INFO] PS/2 mouse initialized
[DEBUG] FAT16: Reading cluster 42
[WARN] Block cache eviction triggered
```

---

## Memory Safety

cupid-os implements multiple layers of memory protection to catch bugs early.

### Heap Canaries

Every allocation from `kmalloc()` is wrapped with canary values:

```
┌─────────────┬──────────────────┬─────────────┐
│ HEAD CANARY │   User Data      │ TAIL CANARY │
│ 0xDEADBEEF  │  (requested sz)  │ 0xBEEFDEAD │
└─────────────┴──────────────────┴─────────────┘
```

- **Head canary**: `0xDEADBEEF` — placed before user data
- **Tail canary**: `0xBEEFDEAD` — placed after user data
- Checked on every `kfree()` call
- Buffer overflows and underflows corrupt canaries → detected and reported

### Free-Memory Poisoning

When memory is freed via `kfree()`:
- The entire freed block is filled with `0xFEFEFEFE`
- Any use-after-free that reads this memory gets a recognizable poison pattern
- Helps identify stale pointer bugs

### Allocation Tracking

The allocator tracks:
- Total allocations made
- Total frees performed
- Currently active allocations
- Peak memory usage

---

## Memory Inspection Commands

| Command | Description |
|---------|-------------|
| `memdump <addr> [len]` | Hex dump of memory at address |
| `memstats` | Show allocation statistics |
| `memleak` | Report suspected memory leaks |
| `memcheck` | Validate all active heap allocations |

### memdump

```
> memdump 0x100000 64
00100000: 48 65 6C 6C 6F 20 57 6F  72 6C 64 00 00 00 00 00  Hello World.....
00100010: FE FE FE FE FE FE FE FE  FE FE FE FE FE FE FE FE  ................
```

### memstats

```
> memstats
Heap Statistics:
  Total allocations: 342
  Total frees:       318
  Active allocations: 24
  Heap start:  0x00200000
  Heap end:    0x00800000
```

### memleak

Lists all active allocations that haven't been freed, helping identify leaks:

```
> memleak
Active allocations:
  0x00201000  size=4096  (stack)
  0x00202000  size=1024  (buffer)
  ...
```

### memcheck

Walks every active allocation and verifies canary integrity:

```
> memcheck
Checking all active allocations...
All 24 allocations OK
```

Or if corruption is detected:

```
> memcheck
CORRUPTION: allocation at 0x00205000 - tail canary damaged!
```

---

## Stack Protection

### Stack Canary

Every process stack has a canary at the bottom:

| Value | Location | Purpose |
|-------|----------|---------|
| `0xDEADC0DE` | Bottom of stack | Detect stack overflow |

Checked on every context switch. If the canary is corrupted → **kernel panic** identifying the process.

---

## Crash Testing

The `crashtest` command deliberately triggers various failure modes for testing:

| Subcommand | What It Does |
|------------|-------------|
| `crashtest divzero` | Division by zero (INT 0) |
| `crashtest nullptr` | Null pointer dereference |
| `crashtest overflow` | Stack overflow via infinite recursion |
| `crashtest gpf` | General protection fault |
| `crashtest assert` | Failed assertion |
| `crashtest panic` | Manual kernel panic |

### Example

```
> crashtest nullptr
*** KERNEL PANIC ***
Page Fault at address 0x00000000
EIP: 0x00102ABC
Process: desktop (PID 2)
System halted.
```

---

## Introspection Commands

| Command | Description |
|---------|-------------|
| `stacktrace` | Show current call stack |
| `registers` | Dump CPU register state |
| `sysinfo` | Show OS version, uptime, memory |
| `logdump` | Print log buffer contents |
| `loglevel <n>` | Set log verbosity (0–3) |

### registers

```
> registers
EAX=00000001 EBX=00103000 ECX=00000010 EDX=000003F8
ESI=00000000 EDI=00200000 EBP=001FFFF0 ESP=001FFFE0
EIP=00102345 EFLAGS=00000202
```

### sysinfo

```
> sysinfo
cupid-os v0.1.0
Uptime: 42 seconds
Processes: 3/32
Memory: 24 active allocations
PIT frequency: 100 Hz
```

---

## Panic Handler

When an unrecoverable error occurs, the kernel panic handler:

1. Disables interrupts (`CLI`)
2. Prints panic message with location info
3. Dumps register state
4. Dumps stack trace (if available)
5. Halts the CPU (`HLT` in infinite loop)

Panics can be triggered by:
- Unhandled exceptions (page fault, GPF, divide-by-zero)
- Failed assertions (`KASSERT()` macro)
- Detected corruption (canary violation, stack overflow)
- Manual trigger (`kernel_panic()`)

### KASSERT Macro

```c
KASSERT(condition, "message");

// Example:
KASSERT(ptr != NULL, "Null pointer in process_create");
```

If the condition is false → kernel panic with file, line, and message.

---

## See Also

- [Architecture](Architecture) — System memory layout
- [Process Management](Process-Management) — Stack canaries and process table
- [Shell Commands](Shell-Commands) — Full command reference
