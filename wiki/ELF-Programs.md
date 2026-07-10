# ELF Programs

CupidOS supports loading and running static **ELF32 i386** executables. The current hosted examples are compiled to relocatable objects by GCC or Clang and linked by CupidLD. Programs run as kernel threads in ring 0 and access kernel services through a **syscall table** - a struct of function pointers passed to the program's `_start()` entry point.

---

## Quick Start

### 1. Write a Program

Create a `.c` file that includes `cupid.h` and implements `_start()`:

```c
/* user/examples/hello.c */
#include "cupid.h"

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

# Or compile manually:
gcc -m32 -fno-pie -nostdlib -static -ffreestanding -O2 \
    -Iuser -c user/examples/hello.c -o hello.o
cupidld -m elf_i386 --text-address 0x00D00000 --entry _start \
    -o hello hello.o
```

### 3. Deploy to Disk

Copy the binary onto the FAT16 disk image:

```bash
# Build a fresh, never-booted image
make clean-image
make

# Stage programs at FAT root before first boot; homefs imports them into /home
python tools/hostbuild.py stage --image cupidos.img --fat-start-lba 16384 \
    user/build/hello:/hello user/build/ls:/ls user/build/cat:/cat
```

After an image has booted and created `HOMEFS.SYS`, newly staged FAT-root files
appear under `/disk` and are not automatically re-imported. Run them there or
copy them into `/home` from inside Cupid OS.

### 4. Run in CupidOS

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
│   .text @ 0xD00000 │
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

CupidOS uses a **flat 512 MB identity-mapped** address space. ELF programs are loaded directly at the virtual addresses specified in their program headers - no address translation needed.

```
Physical / Virtual Memory (512 MB identity-mapped):
0x00100000 ... kernel image ... below 0x00B00000
0x00B00000 - 0x00D00000  fixed kernel stack
0x00D00000 - 0x00F00000  exclusive external-ELF arena
0x01000000 - 0x01900000  CupidC JIT/AOT region
0x01A00000 - 0x01C00000  CupidASM JIT/AOT region
0x20000000                end of identity map
```

Ordinary external programs are linked at `0x00D00000`. The whole two-MiB arena is permanently reserved from ordinary PMM allocation and leased to one fixed-address external process at a time; process cleanup releases the lease, not the permanent pages.

### Syscall Table

Since CupidOS runs everything in ring 0 (TempleOS-style), there is no privilege boundary. Instead of traditional `int 0x80` syscalls, the kernel passes a **function pointer table** directly to each ELF program. The program calls kernel functions through this table.

```c
void _start(cupid_syscall_table_t *sys) {
    // sys->print("Hello!\n");     ← direct function call
    // sys->vfs_open("/home/f", 0) ← direct VFS access
    // sys->exit();                ← clean process exit
}
```

This design is simple, fast (no mode switches), and gives programs full kernel access.

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
| `--text-address 0x00D00000` | Set the external-ELF arena base |
| `--entry _start` | Select the program entry symbol |

### User Makefile

The provided `user/Makefile` builds all example programs:

```bash
make -C user          # Build all programs
make -C user clean    # Clean build artifacts
```

To add a new program:
1. Create `user/examples/yourprog.c`
2. Add `yourprog` to the `PROGRAMS` list in `user/Makefile`
3. Run `make -C user`

### Program Structure

Every ELF program must:

1. **Include `cupid.h`** - provides types, constants, and wrapper functions
2. **Implement `_start(cupid_syscall_table_t *sys)`** - the entry point
3. **Call `cupid_init(sys)`** - stores the syscall table pointer globally
4. **Terminate cleanly** - either call `exit()` explicitly or return from `_start()`

```c
#include "cupid.h"

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

Bumped to **`CUPID_SYSCALL_VERSION = 3`** in
`kernel/core/syscall.h`. Layout is append-only - programs built against v2
still work; new programs should check `sys->version >= 3` and
`sys->table_size >= sizeof(<largest field they touch>)` before calling
the new fields.

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

> ⚠ These can deadlock or corrupt the kernel if misused.

| Field | Signature |
|---|---|
| `lapic_get_id` | `uint8_t (*)(void)` |
| `lapic_eoi` | `void (*)(void)` |
| `bkl_lock` / `bkl_unlock` | recursive ticket spinlock |
| `paging_map_mmio` | `void (*)(uint32_t phys, uint32_t size)` |
| `pmm_alloc_page` | `void *(*)(void)` - one 4 KB page |
| `pmm_free_page` | `void (*)(void *page)` |
| `outb_io` / `inb_io` | raw 8-bit port I/O |

Example - query the network interface and ping the gateway from an
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

### hello.c - Hello World

```c
#include "cupid.h"

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

### ls.c - Directory Listing

```c
#include "cupid.h"

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

### cat.c - Display File Contents

```c
#include "cupid.h"

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
python tools/hostbuild.py stage --image cupidos.img --fat-start-lba 16384 \
    user/build/hello:/hello user/build/ls:/ls user/build/cat:/cat
```

On an already-booted image these staged files are visible under `/disk`; copy
them into `/home` in the guest if persistent homefs placement is required.

Then in CupidOS:

```
/home> exec /home/hello
/home> exec /home/ls
/home> exec /home/cat
```

---

## Technical Details

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
| External arena | `0x00D00000..0x00F00000` | Avoid kernel, stack, and Cupid JIT/AOT regions |
| Max external image span | 2 MiB | The complete image must fit the external arena |
| Entry/load range | Loads wholly inside one arena; entry in file-backed `PF_X` bytes | Prevent cross-region overwrite and non-code entry |
| Link address | `0x00D00000` | Fixed base used by `user/Makefile` |

The loader also preserves CupidC's `0x01000000..0x01900000` and CupidASM's
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

- ✅ ELF32 i386 static executables
- ✅ Multiple PT_LOAD segments (.text, .data, .rodata, .bss)
- ✅ BSS zero-initialization
- ✅ External executables up to the two-MiB arena boundary
- ✅ Full kernel API access (console, VFS, memory, process, shell)
- ✅ Quiescent external-arena lease cleanup after exit, kill, or stack failure

### Not Supported

- ❌ Dynamic linking / shared libraries
- ❌ Position-independent executables (PIE)
- ❌ ELF relocations
- ❌ Thread-local storage (TLS)
- ❌ ELF64 (64-bit)
- ❌ Non-i386 architectures
- ❌ Command-line arguments (programs can't receive argc/argv)
- ❌ Standard C library (no libc - use syscall table wrappers)
- ❌ Multiple ordinary external ELF programs simultaneously (the complete fixed arena has one exclusive lease)

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
