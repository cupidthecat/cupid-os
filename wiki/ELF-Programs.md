# ELF Programs

Cupid OS loads and runs static **ELF32 i386** executables. CupidC compiles the
three repository examples, and CupidLD links them. Linux runs the checked i386
Linux seed directly. On Windows, checked native CupidC, CupidASM, and CupidLD
build and run the user ABI contract as a private PE. Checked native CupidC and
CupidLD then perform the output-bearing compile and link. An optional native
Windows comparison requires host-built CupidC and
CupidLD output to match the checked seed. Programs run as ring-0 kernel threads
and receive a **syscall table**, a struct of function pointers passed to
`_start()`.

---

## Quick Start

### 1. Write a Program

Create a `.cc` file that includes `cupid.h` and implements `_start()`:

```c
/* user/examples/hello.cc */
#include "../cupid.h"

void _start(cupid_syscall_table_t *sys) {
    cupid_init(sys);

    print("Hello from an ELF program!\n");
    print("  PID: ");
    print_int(getpid());
    print("\n");

    exit();
}
```

### 2. Compile

```bash
# From the cupid-os root directory:
make -C user

# Verify the deterministic object and executable frontier:
make test-user-cupidc-frontier

# On Windows, compare every native result with the checked seed:
make test-user-native-windows-equivalence
```

The normal Windows build runs checked native CupidC and CupidLD directly and
does not prepare host-built native drivers. Its user ABI contract also runs
directly as a checked PE. The separate comparison command builds private
drivers with Clang and its native linker. The user Makefile declares `all` as
its default goal, so the plain command above selects the same supported target
on Windows and Linux. Native drivers are built only when requested.
Checked-seed CupidLD accepts
`-m i386pe` for ordered static i386 ELF32 objects. It serializes one deterministic,
fixed-layout PE32 console image at image base `0x00400000`, with `.text` at RVA
`0x1000`, each nonempty later section category at the next `0x1000` boundary,
and file alignment `0x200`. Empty output categories do not get PE section
headers. The image reserves and commits a one MiB stack. Its heap reserves one
MiB and commits 4 KiB. Repeatable import options add canonical `.idata` descriptors, lookup
tables, IAT cells, and names. Imported slots require zero-addend absolute
relocations, and the image has no base relocations. Writable executable input
is rejected. CupidLD orders imports with an in-place heap, rejects a repeated
slot without rescanning prior records, and keeps name imports below the PE32
high-bit boundary. The independent validator checks the fixed headers, stack
and heap fields, and exact `.idata` cursor instead of accepting an equivalent
but noncanonical layout. ADR 0274 records the stack policy.

Checked-seed CupidASM, freestanding CupidC, and CupidLD build a small command
that imports `GetStdHandle`, `WriteFile`, and `ExitProcess`. Windows runs the
validated image, checks its exact stdout marker and empty stderr, and requires
exit 37. The bootstrap report retains the observed result and both stages'
object and image hashes.

Each native bootstrap generation builds a shared hosted runtime and startup for
CupidASM, CupidC, CupidDis, CupidLD, and CupidObj. The runtime provides
arguments, a heap, separate standard streams, named-file reads and writes,
append behavior, seeking, the current directory, and useful error mapping. A
dedicated contract checks allocation, file modes, negative paths, and Windows
quote and backslash rules. Each tool runs help plus a useful success and
failure path. CupidDis also checks exact disassembly parity. CupidLD adds
`_fullpath` and four publication imports, then proves exact output, candidate
collision, replacement failure, and candidate cleanup.

The matching PE32 images form the checked Windows execution seed used by the
normal user build and other output-bearing recipes. The Linux seed still runs
through WSL for the complete Toolchain contract cohort. Artifact-size policy
keeps the Linux manifest as provenance, but its checker runs as a temporary PE
from the Windows seed. The user ABI gate builds and runs another temporary PE.
The native fixed-point command freezes the PE execution seed and a
separate verified Linux plan manifest. The seed builds stage two, stage two
builds stage three, and stage three builds stage four. Stages two and three are
transition generations. The convergence check compares stages three and four.
The older stage-two to stage-three comparison stopped safely at
`cupidobj_main`: after 821.9 seconds on Windows and after 883.3 seconds on
Linux. New stack-probe code generation changed compiler-produced objects, so
that comparison measured a transition instead of convergence. Later uncapped
proofs passed: Windows matched 20 C objects, two assembly objects, and five
tools in 20 minutes 43 seconds with 5/5/5 behavior cases; Linux matched 19 C
objects, startup, and five tools in 24 minutes 22 seconds with 5/18/16 behavior
cases. Both reports bind the same 50-input snapshot, SHA-256
`d8481a39e0d1c7f42779a8c9f5fc5de10d7e5b9bc4df63ce6afe9ddd9c9716da`.
Those reports remain preliminary. Linux later passed a clean 1,294.3-second
proof, promoted the stage-four seed, and passed a 1,473.9-second reproof from
that seed. Native Windows then passed a clean 1,253.4-second proof and a
1,061.3-second promoted-seed reproof. Both of those Windows runs matched 20 C
objects, two assembly objects, and five PE32 tools and passed the 5/5/6
behavior matrix. The
old seed comparison was false for CupidASM, CupidC, and CupidDis and true for
CupidLD and CupidObj. That promoted 2,118-byte manifest has SHA-256
`ae1d3dfb10604bba419c5936884668d10595f6c671915a4ae5f16706204bb41e`.
The current 2,118-byte Windows manifest has SHA-256
`751e1d7787a4be08e4e86814bbb7473979fe2eb8a3292baed0241967f772eaef`.
It binds revision `a17c9465911da41d59b7ada71733d36c39faa5ea`, exact 50-input
snapshot
`46c5335c80d822dd5085ee22077486ea647e5396482d42454847c87e4222aa67`,
and Linux parent manifest
`b6e34a2e18dd18aba91c6358116eafde39953566efeadb224575ac8c13ab2c1b`.
ADR 0336 records the current pair.
See [ADR
0247](../docs/adr/0247-serialize-fixed-layout-pe32-images-with-cupidld.md) and
[ADR
0248](../docs/adr/0248-link-deterministic-pe32-imports-and-run-a-cupid-built-windows-command.md).
ADR 0258 records checked-seed carriage. The preliminary Linux behavior
reconstruction also found that `cupiddis_main.cc` lacked `_WIN32=1`; the corrected Windows
profile, parity test, and audit guard now cover all five tool mains. ADR 0268 records the shared runtime,
ADR 0269 records CupidLD publication, ADR 0272 records Windows execution seed
carriage and production selection, ADR 0278 records the native driver, and
[ADR 0279](../docs/adr/0279-prove-post-change-fixed-points-through-convergence.md)
records the convergence rule. [ADR 0280](../docs/adr/0280-promote-the-clean-stage-four-linux-seed.md)
records the Linux promotion. [ADR 0281](../docs/adr/0281-promote-the-clean-stage-four-windows-seed.md)
records the preceding Windows promotion. [ADR 0292](../docs/adr/0292-promote-strict-relocation-production-seeds.md)
records the preceding strict-relocation promotion. [ADR
0323](../docs/adr/0323-promote-and-adopt-static-elf-code-anchor-checks.md)
records the current promotion and production adoption.

Source-head hosted CupidDis can inspect the same deterministic static i386
PE32 profile that CupidLD emits. `--headers`, `--sections`, and `--imports`
report the accepted PE fields, canonical section layout, and named imports.
Strict known-instruction, local-target, and code-anchor checks decode each
executable section through the shared x86 model and require the entry point to
start an instruction. The five checked Windows tool images and an import-free
CupidLD fixture pass alongside the independent Python PE validator. This
reader deliberately rejects dynamic PE, base relocations, ordinal imports, PE
symbols, loaded spans above CupidLD's 2 GiB RVA limit, and layouts outside its
current static profile. The complete seed reports match an independent Python
reconstruction of their sections and import records. The promoted CupidDis
seeds predate this input mode; a later fixed-point proof and seed promotion
must establish carriage. ADR 0338 records the boundary.

The checked-seed CLI uses an adjacent-candidate publisher for ELF and PE images.
It creates the candidate with exclusive-create semantics, writes and closes it,
then reopens the file and checks its size and contents against the linker
buffer. A failed write, close, verification, or replacement preserves an
existing destination. Cleanup is attempted but not guaranteed. On POSIX,
CupidLD requests mode `0777`; the process umask may remove any permission bits.
The directory must remain stable under the caller's control; the CLI does not
lock or pin the path.

### 3. Deploy to Disk

Build the image and stage the validated executables at the FAT16 root:

```bash
# Build a fresh, never-booted image
make clean-image
make sync-user
```

After an image has booted and created `HOMEFS.SYS`, newly staged FAT-root files
appear under `/disk` and are not automatically re-imported. Run them there or
copy them into `/home` from inside Cupid OS.

The checked build is deliberately closed over `hello.cc`, `ls.cc`, and
`cat.cc`. Adding another build-time ELF program means adding it to the
`user/Makefile` program list and the production allowlist, then extending the
frontier tests. This keeps a changed source or tool from bypassing the checked
tool snapshot and ELF validators.

Each checked link stays private until CupidDis accepts it with
`--require-known`, `--require-local-targets`, and
`--require-code-anchors`. CupidLD and CupidDis share one frozen seed capture.
A failed inspection or unexpected output preserves the earlier executable.
[ADR 0326](../docs/adr/0326-inspect-user-elfs-before-publication.md) records the
publication gate.

### 4. Run in Cupid OS

```
/home> exec /home/hello
Hello from an ELF program!
  PID: 4
```

---

## Architecture

### How ELF Programs Run

```
┌────────────────────┐
│   ELF Binary       │  (in homefs at /home/hello)
│   .text @ 0xF00000 │
│   .data / .bss     │
└────────┬───────────┘
         │  exec("/home/hello", "hello")
         ▼
┌────────────────────────────────────────────────┐
│  1. Format Detection                           │
│     Read first 4 bytes -> 0x7F 'E' 'L' 'F'     │
├────────────────────────────────────────────────┤
│  2. Header Validation                          │
│     ELF32, little-endian, i386, ET_EXEC        │
├────────────────────────────────────────────────┤
│  3. Scan PT_LOAD Segments                      │
│     Calculate vaddr range (min -> max)         │
├────────────────────────────────────────────────┤
│  4. Classify Fixed Executable Arena            │
│     claim the external arena's exclusive lease │
├────────────────────────────────────────────────┤
│  5. Load Segments at Virtual Addresses         │
│     memset(0) entire region, then read each    │
│     segment directly to its p_vaddr            │
├────────────────────────────────────────────────┤
│  6. Create Process                             │
│     atomically transfer image/lease ownership  │
│     before publishing the READY process        │
├────────────────────────────────────────────────┤
│  7. Schedule                                   │
│     process_yield() -> new process runs        │
└────────────────────────────────────────────────┘
```

### Memory Model

Cupid OS uses a **flat 512 MB identity-mapped** address space. The loader places ELF segments at the virtual addresses in their program headers without address translation.

```
Physical / Virtual Memory (512 MB identity-mapped):
0x00100000 ... kernel image ... below 0x00F00000
0x00F00000 - 0x01100000  fixed kernel stack
0x01100000 - 0x01A00000  CupidC JIT/AOT region
0x01A00000 - 0x01C00000  CupidASM JIT/AOT region
0x01C00000 - 0x01E00000  exclusive external-ELF arena
0x20000000                end of identity map
```

Ordinary external programs are linked at `0x01C00000`. The whole two-MiB
arena is permanently reserved from ordinary PMM allocation and leased to one
fixed-address external process at a time. Process cleanup releases the lease,
not the permanent pages. Programs linked at an earlier fixed base must be
rebuilt.

A runtime smoke runs the same external program twice at `0x01C00000`. The
first process exits and releases its lease before the second process claims the
arena and loads at the same address.

### Syscall Table

Since Cupid OS runs everything in ring 0 (TempleOS-style), there is no privilege boundary. Instead of traditional `int 0x80` syscalls, the kernel passes a **function pointer table** directly to each ELF program. The program calls kernel functions through this table.

```c
void _start(cupid_syscall_table_t *sys) {
    // sys->print("Hello!\n");     ← direct function call
    // sys->vfs_open("/home/f", 0) ← direct VFS access
    // sys->exit();                ← clean process exit
}
```

Calls through the table do not require a privilege-mode switch, and the table exposes kernel services directly.

The current table ABI is version 5. It has 103 four-byte fields and occupies
412 bytes on i386. The first two fields carry the version and table size; the
remaining 101 fields are kernel function pointers.

Before compiling a tracked example, the build runs the Cupid-built ABI
contract. Linux verifies or rebuilds the published static i386 cohort and runs
its ELF contract. Windows freezes a separate 26-file closure, builds a private
PE with checked CupidC, CupidASM, and CupidLD, validates it, and runs it
directly. Either contract captures and rereads the same six kernel and public
declarations and checks the reviewed table, scalar, constant, record, and
provider rules. Python compares that report with an independent oracle.
The Windows path rechecks source and seed drift and leaves the Linux publication
untouched. ADR 0264 records the semantic transfer, and ADR 0295 records the
native Windows path.

The public scalar types follow the i386 data model: `uint8_t` is one byte,
`uint16_t` is two bytes, and `uint32_t`, `int32_t`, and `size_t` are four
bytes. `size_t` is unsigned and `int32_t` is signed.

VFS names may use 128 bytes and paths may use 512 bytes. A
`cupid_dirent_t` occupies 136 bytes: `name` starts at byte 0, `size` at byte
128, and `type` at byte 132. A `cupid_stat_t` occupies 8 bytes, with `size`
at byte 0 and `type` at byte 4.

---

## Compiling Programs

### Compiler Flags

| Flag | Purpose |
|------|---------|
| `-m32` | Generate 32-bit x86 code |
| `-fno-pie` | Disable position-independent executable |
| `-nostdlib` | Don't link the standard C library |
| `-static` | Static linking only (no shared libraries) |
| `-ffreestanding` | Freestanding environment, no hosted features |
| `-O2` | Optimization level 2 |
| `-Wall -Wextra` | Enable warnings |

### Linker Flags

| Flag | Purpose |
|------|---------|
| `-m elf_i386` | Target i386 ELF format |
| `--text-address 0x01C00000` | Set the external-ELF arena base |
| `--entry _start` | Select the program entry symbol |

### User Makefile

The provided `user/Makefile` builds all example programs:

```bash
make -C user          # Build all programs
make -C user clean    # Clean build artifacts
```

The first command runs the checked Linux seed directly on Linux. On Windows,
checked native CupidC, CupidASM, and CupidLD build and run the ABI contract,
then checked native CupidC and CupidLD build the six program artifacts. The
command does not build the optional host-built native drivers. `all` is an
explicit default goal rather than a side effect of target order.

To add a new program:

1. Create `user/examples/yourprog.cc`
2. Add `yourprog` to the `PROGRAMS` list in `user/Makefile`
3. Add its path to the production source allowlist and frontier contract
4. Run `make test-user-cupidc-frontier`

### Program Structure

Every ELF program must:

1. **Include `cupid.h`** - provides types, constants, and wrapper functions
2. **Implement `_start(cupid_syscall_table_t *sys)`** - the entry point
3. **Call `cupid_init(sys)`** - stores the syscall table pointer globally
4. **Terminate cleanly** - either call `exit()` explicitly or return from `_start()`

```c
#include "../cupid.h"

void _start(cupid_syscall_table_t *sys) {
    cupid_init(sys);       // Required: save syscall table

    // ... your code here ...

    exit();                // Optional: returning also exits cleanly
}
```

The initial process stack supplies `process_exit_trampoline` as `_start()`'s
return address, so falling off the end marks the process terminated just like
an explicit `exit()` call. Explicit `exit()` remains useful for early exits.

---

## Syscall Table API Reference

After calling `cupid_init(sys)`, you can use these wrapper functions directly (no `sys->` prefix needed):

### Console Output

| Function | Signature | Description |
|----------|-----------|-------------|
| `print` | `void print(const char *s)` | Print a string to the terminal |
| `putchar` | `void putchar(char c)` | Print a single character |
| `print_int` | `void print_int(uint32_t n)` | Print an unsigned integer |
| `print_hex` | `void print_hex(uint32_t n)` | Print a hex number (0x...) |
| `clear_screen` | `void clear_screen(void)` | Clear the terminal screen |

### Memory Management

| Function | Signature | Description |
|----------|-----------|-------------|
| `malloc` | `void *malloc(size_t size)` | Allocate heap memory |
| `free` | `void free(void *ptr)` | Free allocated memory |

### String Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `strlen` | `size_t strlen(const char *s)` | String length |
| `strcmp` | `int strcmp(const char *a, const char *b)` | Compare strings |
| `strncmp` | `int strncmp(const char *a, const char *b, size_t n)` | Compare N bytes |
| `memset` | `void *memset(void *p, int v, size_t n)` | Fill memory |
| `memcpy` | `void *memcpy(void *d, const void *s, size_t n)` | Copy memory |

### VFS File Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `open` | `int open(const char *path, uint32_t flags)` | Open a file (returns fd) |
| `close` | `int close(int fd)` | Close a file descriptor |
| `read` | `int read(int fd, void *buf, uint32_t count)` | Read bytes |
| `write` | `int write(int fd, const void *buf, uint32_t count)` | Write bytes |
| `seek` | `int seek(int fd, int32_t off, int whence)` | Seek in file |
| `stat` | `int stat(const char *path, cupid_stat_t *st)` | Get file info |
| `readdir` | `int readdir(int fd, cupid_dirent_t *ent)` | Read directory entry |
| `mkdir` | `int mkdir(const char *path)` | Create a directory |
| `unlink` | `int unlink(const char *path)` | Delete a file |

**Open flags:** `O_RDONLY` (0), `O_WRONLY` (1), `O_RDWR` (2), `O_CREAT` (0x100), `O_TRUNC` (0x200), `O_APPEND` (0x400)

**Seek modes:** `SEEK_SET` (0), `SEEK_CUR` (1), `SEEK_END` (2)

### Process Management

| Function | Signature | Description |
|----------|-----------|-------------|
| `exit` | `void exit(void)` | Terminate current process |
| `yield` | `void yield(void)` | Yield CPU to other processes |
| `getpid` | `uint32_t getpid(void)` | Get current process ID |
| `kill` | `void kill(uint32_t pid)` | Kill a process by PID |
| `sleep_ms` | `void sleep_ms(uint32_t ms)` | Sleep for N milliseconds |

### Shell Integration

| Function | Signature | Description |
|----------|-----------|-------------|
| `shell_execute` | `void shell_execute(const char *line)` | Execute a shell command |
| `shell_get_cwd` | `const char *shell_get_cwd(void)` | Get current working directory |

### Time

| Function | Signature | Description |
|----------|-----------|-------------|
| `uptime_ms` | `uint32_t uptime_ms(void)` | System uptime in milliseconds |

### Program Execution

| Function | Signature | Description |
|----------|-----------|-------------|
| `exec_program` | `int exec_program(const char *path, const char *name)` | Load and run another program |

### Phase 4 / 5 - Networking + drivers (syscall table v3)

`kernel/core/syscall.h` defines **`CUPID_SYSCALL_VERSION = 3`**. The layout is
append-only, so programs built against version 2 remain compatible. A program
that uses version 3 fields should check `sys->version >= 3` and
`sys->table_size >= sizeof(<largest field it touches>)` before calling them.

The kernel ships `_Static_assert` checks on the offsets of key fields
(`memstats`, `net_get_ip`, `ipv4_send`, `sock_socket`, `blkdev_count`,
`pci_device_count`, `inb_io`) so a future field reorder fails to
compile rather than silently shipping a layout mismatch with the AOT
`SYS_*` constants in CupidASM.

#### Network interface info (primary NIC)

| Field | Signature |
|---|---|
| `net_get_ip` | `uint32_t (*net_get_ip)(void)` |
| `net_get_gateway` | `uint32_t (*net_get_gateway)(void)` |
| `net_get_dns` | `uint32_t (*net_get_dns)(void)` |
| `net_get_mask` | `uint32_t (*net_get_mask)(void)` |
| `net_get_mac` | `void (*net_get_mac)(uint8_t *out)` - fills 6 bytes |
| `net_link_up` | `uint32_t (*net_link_up)(void)` - 1=up, 0=down |
| `net_rx_packets` / `net_tx_packets` | counters |
| `net_rx_drops` / `net_tx_errors` | error counters |

#### IP / ARP / ICMP / UDP raw

| Field | Signature |
|---|---|
| `ip_parse` | `int (*ip_parse)(const char *s, uint32_t *out)` |
| `ipv4_send` | `int (*ipv4_send)(uint32_t dst, uint8_t proto, const uint8_t *payload, uint32_t plen)` |
| `arp_resolve` | `int (*arp_resolve)(uint32_t ip, uint8_t mac_out[6])` |
| `arp_dump` / `arp_get_entries` | cache inspection |
| `icmp_send_echo` | `int (*icmp_send_echo)(uint32_t dst, uint16_t id, uint16_t seq, uint32_t paylen)` |
| `icmp_wait_reply` | `int (*icmp_wait_reply)(uint32_t src, uint16_t id, uint16_t seq, uint32_t timeout_ms)` - returns RTT ms |
| `udp_send_raw` | `int (*udp_send_raw)(uint32_t dst, uint16_t sport, uint16_t dport, const uint8_t *data, uint32_t len)` |

#### DNS + byte-order

| Field | Signature |
|---|---|
| `dns_resolve` | `int (*dns_resolve)(const char *name, uint32_t *ip_out)` |
| `htons` / `ntohs` | 16-bit byte-swap |
| `htonl` / `ntohl` | 32-bit byte-swap |

#### BSD sockets

Ports are network byte order - wrap literals in `htons()`.

| Field | Signature |
|---|---|
| `sock_socket` | `int (*sock_socket)(int type)` - `2`=TCP, `1`=UDP |
| `sock_bind` | `int (*sock_bind)(int fd, uint32_t ip, uint16_t port)` |
| `sock_listen` | `int (*sock_listen)(int fd, int backlog)` |
| `sock_accept` | `int (*sock_accept)(int fd, uint32_t *peer_ip, uint16_t *peer_port)` |
| `sock_connect` | `int (*sock_connect)(int fd, uint32_t ip, uint16_t port)` |
| `sock_send` / `sock_recv` | TCP stream I/O |
| `sock_sendto` / `sock_recvfrom` | UDP datagram I/O |
| `sock_close` | tear down |

#### Block devices

| Field | Signature |
|---|---|
| `blkdev_count` | `int (*blkdev_count)(void)` |
| `blkdev_read` | `int (*blkdev_read)(int idx, uint32_t lba, uint32_t count, void *buf)` |
| `blkdev_write` | `int (*blkdev_write)(int idx, uint32_t lba, uint32_t count, const void *buf)` |
| `ata_read_sectors` / `ata_write_sectors` | direct ATA I/O |

#### Drivers - serial, speaker, PIT

| Field | Signature |
|---|---|
| `serial_read_char` | `int (*)(void)` - non-blocking, -1 if empty |
| `serial_write_char` | `void (*)(char)` |
| `serial_write_string` | `void (*)(const char *)` |
| `serial_has_rx` | `int (*)(void)` |
| `pc_speaker_on` / `pc_speaker_off` | square wave on PC speaker |
| `pit_set_frequency` | `void (*)(uint32_t channel, uint32_t hz)` |
| `timer_delay_us` | `void (*)(uint32_t us)` - TSC busy delay |

#### PCI introspection (by index)

| Field | Returns |
|---|---|
| `pci_device_count()` | Number of devices |
| `pci_get_vendor(idx)` | 16-bit vendor ID |
| `pci_get_device_id(idx)` | 16-bit device ID |
| `pci_get_class(idx)` | Packed `class<<16 | sub<<8 | prog_if` |
| `pci_get_irq(idx)` | IRQ line |
| `pci_get_bar(idx, bar)` | BAR value, `bar` = 0..5 |

#### SMP / paging / PMM / port I/O

> Misusing these functions can deadlock or corrupt the kernel.

| Field | Signature |
|---|---|
| `lapic_get_id` | `uint8_t (*)(void)` |
| `lapic_eoi` | `void (*)(void)` |
| `bkl_lock` / `bkl_unlock` | recursive ticket spinlock |
| `paging_map_mmio` | `void (*)(uint32_t phys, uint32_t size)` |
| `pmm_alloc_page` | `void *(*)(void)` - one 4 KB page |
| `pmm_free_page` | `void (*)(void *page)` |
| `outb_io` / `inb_io` | raw 8-bit port I/O |

Example: query the network interface and ping the gateway from an
ELF program:

```c
void _start(cupid_syscall_table_t *sys) {
    uint8_t mac[6];
    sys->net_get_mac(mac);
    sys->print("My MAC: ");
    sys->print_hex(mac[0]); sys->putchar(':');
    sys->print_hex(mac[5]); sys->putchar('\n');

    uint32_t gw = sys->net_get_gateway();
    sys->icmp_send_echo(gw, 0xCAFE, 1, 32);
    int rtt = sys->icmp_wait_reply(gw, 0xCAFE, 1, 3000);
    if (rtt >= 0) {
        sys->print("Gateway reply: ");
        sys->print_int((uint32_t)rtt);
        sys->print(" ms\n");
    }
    sys->exit();
}
```

---

## VFS Structures

These structures are defined in `cupid.h` and match the kernel's VFS types:

```c
/* Directory entry (from readdir) */
typedef struct {
    char     name[64];   /* File/directory name */
    uint32_t size;       /* File size in bytes */
    uint8_t  type;       /* VFS_TYPE_FILE, VFS_TYPE_DIR, or VFS_TYPE_DEV */
} cupid_dirent_t;

/* File status (from stat) */
typedef struct {
    uint32_t size;       /* File size in bytes */
    uint8_t  type;       /* VFS_TYPE_FILE, VFS_TYPE_DIR, or VFS_TYPE_DEV */
} cupid_stat_t;
```

---

## Example Programs

### hello.cc: Hello world

```c
#include "../cupid.h"

void _start(cupid_syscall_table_t *sys) {
    cupid_init(sys);

    print("Hello from an ELF program!\n");
    print("  PID: ");
    print_int(getpid());
    print("\n");
    print("  Uptime: ");
    print_int(uptime_ms());
    print(" ms\n");
    print("  CWD: ");
    print(shell_get_cwd());
    print("\n");

    exit();
}
```

### ls.cc: Directory listing

```c
#include "../cupid.h"

void _start(cupid_syscall_table_t *sys) {
    cupid_init(sys);

    const char *path = shell_get_cwd();
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        print("ls: cannot open ");
        print(path);
        print("\n");
        exit();
    }

    cupid_dirent_t ent;
    while (readdir(fd, &ent) > 0) {
        if (ent.type == VFS_TYPE_DIR)
            print("[DIR]  ");
        else if (ent.type == VFS_TYPE_DEV)
            print("[DEV]  ");
        else
            print("       ");

        print(ent.name);

        if (ent.type == VFS_TYPE_FILE) {
            print("  (");
            print_int(ent.size);
            print(" B)");
        }
        print("\n");
    }

    close(fd);
    exit();
}
```

### cat.cc: Display file contents

```c
#include "../cupid.h"

void _start(cupid_syscall_table_t *sys) {
    cupid_init(sys);

    const char *path = "/home/readme.txt";

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        print("cat: cannot open ");
        print(path);
        print("\n");
        exit();
    }

    char buf[512];
    int n;
    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        print(buf);
    }

    close(fd);
    exit();
}
```

---

## Deploying Programs to Disk

ELF binaries go on the FAT16 partition inside `cupidos.img`:

```bash
# Build the programs
make -C user

# For /home import, stage into a fresh image before its first boot
make clean-image
make
python tools/hostbuild.py stage --image cupidos.img --fat-start-lba 20480 \
    user/build/hello:/hello user/build/ls:/ls user/build/cat:/cat
```

`user/build/` contains generated files and is ignored by Git. Rebuild the
programs before staging them instead of committing local executables.

On an already-booted image these staged files are visible under `/disk`; copy
them into `/home` in the guest if persistent homefs placement is required.

Then in Cupid OS:

```
/home> exec /home/hello
/home> exec /home/ls
/home> exec /home/cat
```

---

## Technical Details

### Private CupidC AOT Images

Private CupidC AOT output is separate from the CupidLD-linked external program
path described elsewhere on this page. It places code at virtual address
`0x01100000` and file offset `0x80`. A program with no data has one executable
`PT_LOAD`; a program with data adds a writable segment at virtual address
`0x01200000`. The code offset remains `0x80` in either form.

This AOT path accepts a direct file-scope function-pointer typedef when a free
function parameter names it. The parameter retains the callback result, fixed
and variadic arguments, record identities, and prototype state, so indirect
calls use the direct cdecl conversion and 4-, 8-, and 16-byte layout. SIMD
results return through XMM0. A file object declared directly with that typedef
retains the same signature. It may start as null or a compatible defined or
later-defined function, receive a checked plain assignment, make a typed
indirect call, and be cleared. Private JIT and fixed-address AOT write or patch
the function address in initialized data before execution. A Cupid class
method parameter declared directly with a file-scope callback typedef keeps
the signature as well. An automatic object
declared directly with that typedef keeps it when initialized in its
declaration. A structure or class field declared directly with the typedef
keeps the signature for checked stores, named copies, null checks, and clearing.
Direct members, nested records, and indexed record arrays share that path. Raw
callback fields retain the same metadata. Direct postfix calls through either
field form use typed cdecl conversion without evaluating the designator twice.
Typedef-backed fixed callback arrays in records retain the signature through
indexing. Callback alias chains, aggregate results, and arbitrary computed
callback expressions remain signature-erased
or unsupported. ADR 0325 records the completed field boundary, and ADR 0328
records typedef-backed callback field arrays. AOT still compiles one translation
unit into a fixed-address executable and does not emit a relocatable object for
a later link.

A named raw callback file object and direct free-function parameter retain the
same parsed signature without a typedef. The file object uses the existing
initialized-data write or patch. The parameter uses the existing cdecl slot
and arity checks. The private pool accepts 32 distinct raw parameter
signatures, rejects the next one, and restores the pool before a valid retry.
When one callback takes another callback as a fixed parameter, the outer
signature stores a child handle. Raw nested declarators use raw handles;
callback typedef parameters use typedef handles. The AOT type checker descends
through both forms and compares nested results, parameters, record-pointer
identities, and variadic boundaries. Initializers, assignments, higher-order
arguments, indirect calls, and conditionals keep the existing compatibility
rule for unprototyped callbacks. Matching declarations and definitions also
require the same prototype state.

Nested signature metadata does not change the executable ABI. Each callback
argument remains one four-byte i386 cdecl slot. A signature deeper than 16
levels or a 33rd distinct source signature rejects the source and restores the
signature pool and surrounding AOT transaction. A 33-record backing pool keeps
that source budget after the active kernel descriptor occupies one record. The
active `void (*p_icon_set_drawer)(int, void (*)(int, int))` binding in
`kernel/lang/cupidc.cc` retains `void(int, int)` and publishes that handle for
`set_icon_drawer`. The production
compiler source remains built by checked-seed hosted CupidC; this private AOT
rule does not transfer its ownership to in-OS CupidC.

Fixed-address AOT calls to reviewed native bindings use their retained
`cc_function_pointer_signature_t`. Integer-to-float, integer-to-double,
float-to-double, mixed-width slots, arity checks, variadic promotions, cleanup,
and result transport share the ordinary typed-call path. The same tests run in
JIT mode. Unreviewed bindings stay on the named legacy result-only path, and
this private metadata change does not alter the external ELF loader ABI. ADR
0332 records fixed signatures, and ADR 0333 records the nested binding.

Raw callback scalars with static storage use the same data write and patch
path at file scope, block-static scope, and persistent REPL scope. A
one-dimensional raw callback array is also available in those contexts. A
fixed-size array may omit its initializer or provide a shorter braced list;
both cases leave the remaining entries at zero. An unsized array requires a
nonempty braced initializer, which determines its count. Compatible defined
functions are written into the writable data segment, later functions receive
absolute data patches, and explicit null entries remain zero. Indexed stores
and calls retain the signature, apply the existing compatibility and cdecl
rules, and evaluate the index once.

This private AOT rule represents the active six-entry `wipes` table in
`kernel/doom/src/f_wipe.cc`, including its `wipeno*3`, `wipeno*3+1`, and
`wipeno*3+2` calls. Fixed-size automatic raw callback arrays use cleared local
frame storage and retain their signature through brace initialization, indexed
stores, copies, and calls. Unsized automatic arrays, raw callback array
parameters, raw callback arrays in records or classes, multidimensional raw
callback arrays, raw method parameters, and aggregate callback contexts remain
unsupported. ADR 0315 records the raw scalar and parameter source rule.
[ADR 0330](../docs/adr/0330-support-data-backed-raw-callback-arrays.md)
records data-backed raw callback arrays and block-static scalar callbacks.

The source-current private callback ABI module passes all 310 tests in 75.017
seconds, and the full GUI module passes all 128 tests in 0.955 seconds. A
private four-vCPU frontier boot records all four CPUs online,
`[feature14-callback-raw-array] PASS modes=2 phases=3 calls=12 stored=1 persistent=1`,
`[feature14-callback-nested] PASS outer=1 inner=1 value=43`,
`PASS feature14_simd`, and clean in-OS CupidC JIT completion. Its 157,520-byte
log has SHA-256
`b34a68aebdfecaeeb347c1ff4764cbe609a6ed2f154557a15133a601101585c6`.
The broader frontier changes 109,518 framebuffer pixels and captures
32,701,862 AC97 frames and 76,710 PC-speaker frames.
The standalone CupidC seeds do not contain this private parser. The production
Doom cohort, including `f_wipe.cc`, remains built by checked-seed hosted
CupidC. This private AOT capability does not transfer that cohort to in-OS
CupidC, promote a seed, or remove a remaining bootstrap dependency.
[ADR 0303](../docs/adr/0303-retain-typedef-callback-signatures-in-private-cupidc.md)
records the callback and one-header AOT boundaries.
[ADR 0306](../docs/adr/0306-retain-global-typedef-callback-signatures-in-private-cupidc.md)
records the global storage and assignment boundary.
[ADR 0310](../docs/adr/0310-retain-automatic-callback-typedef-signatures-in-private-cupidc.md)
records automatic objects and Cupid class method parameters.
[ADR 0313](../docs/adr/0313-initialize-private-cupidc-global-callbacks-from-functions.md)
records static callback initialization for private JIT and fixed-address AOT
data.
[ADR 0319](../docs/adr/0319-retain-explicit-function-addresses-in-private-callbacks.md)
records direct explicit function addresses. Runtime initialization and
assignment accept `&(function)` and nested grouping; ADR 0324 records that
runtime boundary.
[ADR 0321](../docs/adr/0321-retain-typedef-callback-signatures-on-private-record-fields.md)
records typedef-backed record and class fields.

The preceding poisoned-host build checkpoint passed in 684.260 seconds with
all fourteen exact policy artifacts accepted. It produced a 9,320,424-byte
`kernel/kernel.elf.pass1`, SHA-256
`3f9a1c681fbcfb1aa453e42a9d77ed1069b9a487110c9ec22ac318d278bdd1e6`,
and a 9,447,400-byte `kernel/kernel.elf`, SHA-256
`92d4e2f890b657c9881eb2184c7f8f9f0e96b18b5b060dbabab17e7ea305b1ce`.
The 9,224,756-byte raw kernel has SHA-256
`4d53e0456d8e63e140f6dcab135765662d12df6e4a83b246409572501f3b4cbd`.
A private four-vCPU `max`/e1000 smoke of the resulting image passed in 64.601
seconds and left that image unchanged. Its typedef callback marker belongs to
the JIT path. The zero-data AOT layout and typed AOT callback path remain
covered by the focused private compiler contracts rather than this guest run.

The pre-documentation artifact gate later passed in 651.3 seconds, accepted all
fourteen exact paths, and measured `kernel/kernel.bin` at 9,225,092 bytes.

The integrated fully poisoned build first reached the exact-size gate with
three rebuilt kernel outputs. The artifact group passed all 46 tests in 4.160
seconds, with four expected Windows skips. After those three policy rows were
updated, the repeated build passed in 874.531 seconds with all fourteen
artifacts accepted, existing FAT contents preserved, and `hello.iso` staged.
At that historical checkpoint, the pass-one ELF was 9,345,464 bytes with SHA-256
`5dbd2c5acb7b1604cf6daf6f311e88015d0762125c60920da3737d7e10d76f06`;
the final ELF is 9,472,440 bytes with SHA-256
`5810ddcb963cfadb4fea3b1343bb38c17ce3f762a48f25615b3feb653f1638e3`;
the raw kernel is 9,251,100 bytes with SHA-256
`4014b1b2acf34be4dd7483fb8aa9e8a8b0e76eea771c83669571cbf7b66fe0e3`.

The source-head artifact contract passes against all fourteen exact artifacts.

| Source-head artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/kernel.elf.pass1` | 9,596,956 | `fbf1f1feb45d9c1edd094a1daa57602bfa8d8185ac3d9e83771e57b1ebe854f1` |
| `kernel/kernel.elf` | 9,728,028 | `18918ed937654801f89e8a5a23487af31f0445aa9c5c46f0a7e3ec89c007fb2e` |
| `kernel/kernel.bin` | 9,499,524 | `be34d514278e28a91e36709a8a2c4e6876f1689d77322e6a53353252e3415949` |
| `cupidos.img` | 209,715,200 | `fbcf52218dfc630b80373253e00d7f5a53895494ad615683f40b88ead1a8d602` |

Those output identities come from the completed normal build. Both checked
seeds carry the same source snapshot. The kernel transaction passed linked
local-target and static code-anchor validation before flattening, and the image
passed a private four-vCPU E1000 frontier smoke. ADR 0318 records
the preceding linked-image promotion, ADR 0323 records the preceding
code-anchor promotion, and ADR 0336 records the current promotion and adoption.

The integrated strong full private frontier smoke passed in 883.513 seconds
with e1000, four `max` vCPUs, SMP and frontier checks, and the private USB
fixture. The expected direct-call, named-callback, typedef-callback,
global-callback, automatic-callback, and overall feature14 PASS markers each
appeared once and in order. The feature run then printed a clean JIT completion.
The 161,418-byte log has SHA-256
`bc30f5083b96a36362bec5975c0a88437c4f23515de329328bb03d8f6c3e9326`.
The source image was unchanged at SHA-256
`31b25b6881419b1bb8a04b2b3765323b21c5706ac114af1a07b514dcdcd07ea3`.

### ELF Header Validation

The loader checks all of the following before loading:

| Field | Required Value | Meaning |
|-------|----------------|---------|
| `e_ident[0..3]` | `0x7F 'E' 'L' 'F'` | ELF magic number |
| `e_ident[4]` | `1` (ELF_CLASS_32) | 32-bit ELF |
| `e_ident[5]` | `1` (ELF_DATA_LSB) | Little-endian |
| `e_type` | `2` (ET_EXEC) | Executable file |
| `e_machine` | `3` (EM_386) | Intel i386 |
| `e_phnum` | `> 0`, `≤ 16` | Has program headers |

### Address Constraints

| Constraint | Value | Reason |
|------------|-------|--------|
| External arena | `0x01C00000..0x01E00000` | Avoid kernel, stack, and Cupid JIT/AOT regions |
| Max external image span | 2 MiB | The complete image must fit the external arena |
| Entry/load range | Loads wholly inside one arena; entry in file-backed `PF_X` bytes | Prevent cross-region overwrite and non-code entry |
| Link address | `0x01C00000` | Fixed base used by `user/Makefile` |

The loader also preserves CupidC's `0x01100000..0x01A00000` and CupidASM's
`0x01A00000..0x01C00000` fixed AOT ranges. An image must fit wholly inside
exactly one of these three arenas, and its entry must be inside the
file-backed (`p_filesz`) bytes of a `PF_X` `PT_LOAD`. The legacy Cupid ranges are permanent shared runtime regions; the
exclusive lease applies only to ordinary external images.

### Memory Lifecycle

```
exec("/home/hello")
  │
  ├─ validate metadata and read a zero-filled staging image
  ├─ close the source after all validation and reads complete
  ├─ claim external-ELF arena lease
  ├─ commit staged segments to fixed vaddrs
  ├─ create process with image/lease metadata atomically
  │
  │  ... program runs ...
  │
  └─ exit()  -> mark TERMINATED -> scheduler detaches owning CPU
                                      │
                                      └─ quiescent reaper releases lease
```

Validation, load/read, allocation, and close failures happen before a lease is
claimed. If process creation fails after the claim, the loader discards that
still-unconsumed generation. Exit, kill, and stack-canary termination release
a consumed lease only after the process is no longer executing on any CPU. The
underlying arena pages remain permanently reserved in every case.

### BSS Handling

The BSS section (uninitialized global data) is handled implicitly: the loader `memset(0)`s the entire page-aligned region before loading file data, so any gap between `p_filesz` and `p_memsz` in a segment is already zeroed.

---

## Limitations

### Supported

- ELF32 i386 static executables
- Multiple `PT_LOAD` segments (`.text`, `.data`, `.rodata`, `.bss`)
- BSS zero-initialization
- External executables up to the two-MiB arena boundary
- Kernel access for console, VFS, memory, process, and shell services
- Quiescent external-arena lease cleanup after exit, kill, or stack failure

### Not Supported

- Dynamic linking and shared libraries
- Position-independent executables (PIE)
- ELF relocations
- Thread-local storage (TLS)
- ELF64 executables
- Architectures other than i386
- Command-line arguments (`argc` and `argv`)
- The standard C library; programs use syscall-table wrappers instead
- Multiple ordinary external ELF programs at the same time; the fixed arena has one exclusive lease

### Constraints

| Constraint | Value |
|------------|-------|
| Max external executable span | 2 MiB |
| Max program headers | 16 |
| Max concurrent external ELF images | 1 |
| Max concurrent processes | 32 |
| Stack per process | 32 KB (default) |
| Total managed memory | 512 MB |
| Disk filename format | VFS paths, with FAT16 constraints visible under `/disk` |

---

## See Also

- [Filesystem](Filesystem) - VFS, mount points, FAT16 disk I/O
- [Process Management](Process-Management) - Scheduler, context switching
- [Shell Commands](Shell-Commands) - `exec` command reference
- [Architecture](Architecture) - System memory layout
