# CupidASM Assembler

CupidASM is an x86-32 assembler built directly into the cupid-os kernel. It assembles Intel-syntax `.asm` source files to native machine code and can either execute them immediately (JIT) or produce ELF32 binaries on disk (AOT). Like CupidC, JIT programs run in ring 0 with full kernel access - no restrictions.

---

## Overview

| Feature | Details |
|---------|---------|
| Syntax | Intel (NASM-style) |
| Target | x86 32-bit flat model |
| Assembler type | Single-pass with forward-reference patch table |
| Calling convention | cdecl |
| Execution modes | JIT (in-memory) and AOT (ELF32 binary) |
| Privilege level | Ring 0 - full system access |
| Source extension | `.asm` |
| Code size limit | 128 KB |
| Data size limit | 32 KB |
| Max source file | 256 KB |
| Max labels | 512 |
| Max forward refs | 512 |
| Include depth | 4 levels |
| Instructions | 62 mnemonics |
| Registers | 24 (8/16/32-bit) |

---

## Getting Started

### JIT Mode - Assemble and Run Instantly

```
> as demos/hello.asm
```

Assembles the source into memory at `0x500000` and executes immediately. No binary is saved.

You can also run `.asm` files directly with `./`:

```
> ./demos/hello.asm
```

### AOT Mode - Assemble to ELF Binary

Using the `as` command with `-o`:

```
> as -o hello demos/hello.asm
> exec hello
```

Or using the dedicated `cupidasm` command:

```
> cupidasm demos/hello.asm -o hello
> exec hello
```

If `-o` is omitted with `cupidasm`, the output name is derived from the source file (e.g., `hello.asm` -> `hello`).

---

## Program Structure

CupidASM programs use NASM-style section directives and require a `main:` (or `_start:`) label as the entry point.

```asm
section .data
    msg db "Hello from CupidASM!", 10, 0

section .text

main:
    push msg
    call print
    add  esp, 4
    ret
```

### Sections

| Section | Purpose |
|---------|---------|
| `section .text` | Code (instructions). Default section. |
| `section .data` | Initialized data (`db`, `dw`, `dd` directives). |
| `section .bss` | Uninitialized data (`resb`, `resw`, `resd`). Treated as data section. |

### Labels

Labels are defined with a trailing colon:

```asm
my_function:
    ret
```

**Local labels** start with a `.` and are scoped to the nearest non-local label:

```asm
main:
    jmp .done
.done:
    ret
```

### Constants

Use `equ` to define numeric constants:

```asm
BUFFER_SIZE equ 1024
MAX_ITEMS   equ 64
```

---

## Calling Convention

CupidASM uses the **cdecl** calling convention:

- Arguments pushed right-to-left onto the stack
- Caller cleans up the stack after the call
- Return value in `eax`
- `eax`, `ecx`, `edx` are caller-saved (may be clobbered)
- `ebx`, `esi`, `edi`, `ebp` are callee-saved

### Function Example

```asm
; add_numbers(a, b) - returns a + b in eax
add_numbers:
    push ebp
    mov  ebp, esp
    mov  eax, [ebp+8]     ; first argument
    add  eax, [ebp+12]    ; second argument
    pop  ebp
    ret

main:
    push 27               ; second arg
    push 15               ; first arg
    call add_numbers
    add  esp, 8           ; clean up 2 args
    ; eax = 42
    ret
```

### Stack Frame Layout

```
         ┌─────────────┐  (high addresses)
         │  arg 2      │  [ebp+12]
         │  arg 1      │  [ebp+8]
         │  return addr│  [ebp+4]
         │  saved ebp  │  [ebp]     ← ebp points here
         │  locals...  │  [ebp-4], [ebp-8], ...
         └─────────────┘  (low addresses, esp grows down)
```

---

## Data Directives

| Directive | Size | Example |
|-----------|------|---------|
| `db` | 1 byte | `msg db "Hello", 10, 0` |
| `dw` | 2 bytes | `port dw 0x3F8` |
| `dd` | 4 bytes | `count dd 42` |
| `resb` | Reserve N bytes | `buffer resb 256` |
| `resw` | Reserve N words | `table resw 16` |
| `resd` | Reserve N dwords | `array resd 8` |
| `rb` | Alias of `resb` | `buffer rb 256` |
| `rw` | Alias of `resw` | `table rw 16` |
| `rd` | Alias of `resd` | `array rd 8` |
| `reserve` | Alias of `resb` | `scratch reserve 64` |
| `times` | Repeat | `times 10 db 0` |
| `equ` | Constant | `SIZE equ 1024` |

You can declare reserve/data directives in either style:

```asm
buffer resb 256
buffer: resb 256
array:  rd 8
scratch reserve 64
```

### String Data

Strings are `db` directives with quoted text. Newline is `10`, null terminator is `0`:

```asm
section .data
    hello  db "Hello, World!", 10, 0
    prompt db "> ", 0
    digits db "0123456789", 0
```

### Arrays

```asm
section .data
    numbers dd 5, 3, 8, 1, 9, 2, 7, 4
    count   dd 8
```

---

## Instruction Reference

### Data Movement

| Instruction | Description | Example |
|-------------|-------------|---------|
| `mov` | Move data | `mov eax, 42` / `mov eax, [ebp+8]` |
| `push` | Push to stack | `push eax` / `push 42` / `push msg` |
| `pop` | Pop from stack | `pop eax` |
| `lea` | Load effective address | `lea eax, [ebx+ecx*4]` |
| `xchg` | Exchange values | `xchg eax, ebx` |
| `movzx` | Move with zero-extend | `movzx eax, al` |
| `movsx` | Move with sign-extend | `movsx eax, al` |

### Arithmetic

| Instruction | Description | Example |
|-------------|-------------|---------|
| `add` | Add | `add eax, ebx` / `add eax, 10` |
| `sub` | Subtract | `sub eax, 1` |
| `mul` | Unsigned multiply (EDX:EAX) | `mul ebx` |
| `imul` | Signed multiply | `imul ebx` |
| `div` | Unsigned divide (EAX/reg) | `div ecx` |
| `idiv` | Signed divide | `idiv ecx` |
| `inc` | Increment by 1 | `inc eax` |
| `dec` | Decrement by 1 | `dec ecx` |
| `neg` | Two's complement negate | `neg eax` |

### Bitwise & Logic

| Instruction | Description | Example |
|-------------|-------------|---------|
| `and` | Bitwise AND | `and eax, 0xFF` |
| `or` | Bitwise OR | `or eax, 1` |
| `xor` | Bitwise XOR | `xor eax, eax` |
| `not` | Bitwise NOT | `not eax` |
| `shl` | Shift left | `shl eax, 2` |
| `shr` | Shift right (logical) | `shr eax, 1` |
| `sar` | Shift right (arithmetic) | `sar eax, 1` |
| `rol` | Rotate left | `rol eax, 4` |
| `ror` | Rotate right | `ror eax, 4` |

### Comparison & Test

| Instruction | Description | Example |
|-------------|-------------|---------|
| `cmp` | Compare (sets flags) | `cmp eax, 0` |
| `test` | Bitwise AND test (sets flags) | `test eax, eax` |

### Control Flow

| Instruction | Description | Condition |
|-------------|-------------|-----------|
| `jmp` | Unconditional jump | - |
| `call` | Call function | - |
| `ret` | Return from function | - |
| `je` / `jz` | Jump if equal / zero | ZF=1 |
| `jne` / `jnz` | Jump if not equal / not zero | ZF=0 |
| `jl` | Jump if less (signed) | SF≠OF |
| `jg` | Jump if greater (signed) | ZF=0 and SF=OF |
| `jle` | Jump if less or equal | ZF=1 or SF≠OF |
| `jge` | Jump if greater or equal | SF=OF |
| `jb` | Jump if below (unsigned) | CF=1 |
| `ja` | Jump if above (unsigned) | CF=0 and ZF=0 |
| `jbe` | Jump if below or equal | CF=1 or ZF=1 |
| `jae` | Jump if above or equal | CF=0 |
| `js` | Jump if sign (negative) | SF=1 |
| `jns` | Jump if not sign | SF=0 |
| `jo` | Jump if overflow | OF=1 |
| `jno` | Jump if not overflow | OF=0 |

### System & Misc

| Instruction | Description |
|-------------|-------------|
| `nop` | No operation |
| `hlt` | Halt CPU |
| `cli` | Clear interrupt flag |
| `sti` | Set interrupt flag |
| `int` | Software interrupt (`int 0x80`) |
| `iret` | Return from interrupt |
| `in` | Read from I/O port |
| `out` | Write to I/O port |
| `leave` | Destroy stack frame (`mov esp, ebp; pop ebp`) |
| `cdq` | Sign-extend EAX into EDX:EAX |
| `cbw` | Sign-extend AL into AX |
| `cwde` | Sign-extend AX into EAX |
| `pushad` | Push all 32-bit general registers |
| `popad` | Pop all 32-bit general registers |
| `pushfd` | Push EFLAGS |
| `popfd` | Pop EFLAGS |

### String Operations

| Instruction | Description |
|-------------|-------------|
| `rep` | Repeat prefix for string ops |
| `movsb` | Move byte (ESI -> EDI) |
| `movsd` | Move dword (ESI -> EDI) |
| `stosb` | Store AL at EDI |
| `stosd` | Store EAX at EDI |

---

## Registers

### 32-bit General Purpose

| Register | Index | Typical Use |
|----------|-------|-------------|
| `eax` | 0 | Accumulator, return value |
| `ecx` | 1 | Counter (loops, shifts) |
| `edx` | 2 | Data, I/O port, mul/div high bits |
| `ebx` | 3 | Base pointer (callee-saved) |
| `esp` | 4 | Stack pointer |
| `ebp` | 5 | Base/frame pointer (callee-saved) |
| `esi` | 6 | Source index (callee-saved) |
| `edi` | 7 | Destination index (callee-saved) |

### 16-bit and 8-bit

The assembler also supports 16-bit (`ax`, `cx`, `dx`, `bx`, `sp`, `bp`, `si`, `di`) and 8-bit (`al`, `cl`, `dl`, `bl`, `ah`, `ch`, `dh`, `bh`) register names.

---

## Kernel Bindings (JIT Mode)

In JIT mode, the assembler pre-registers kernel functions as labels. Programs can `call` them directly using cdecl convention (push args right-to-left, caller cleans stack).

### Console Output

| Function | Signature | Description |
|----------|-----------|-------------|
| `print` | `print(const char *str)` | Print a null-terminated string |
| `putchar` | `putchar(char c)` | Print a single character |
| `print_int` | `print_int(int n)` | Print a decimal integer |
| `print_hex` | `print_hex(uint32_t n)` | Print a hex value (0xNNNNNNNN) |
| `clear_screen` | `clear_screen()` | Clear the terminal |

### Memory

| Function | Signature | Description |
|----------|-----------|-------------|
| `kmalloc` | `void *kmalloc(size_t size)` | Allocate heap memory |
| `kfree` | `kfree(void *ptr)` | Free heap memory |

### Strings

| Function | Signature | Description |
|----------|-----------|-------------|
| `strlen` | `int strlen(const char *s)` | String length |
| `strcmp` | `int strcmp(const char *a, const char *b)` | Compare strings |
| `memset` | `memset(void *dst, int val, size_t n)` | Fill memory |
| `memcpy` | `memcpy(void *dst, const void *src, size_t n)` | Copy memory |

### File System (VFS)

| Function | Signature | Description |
|----------|-----------|-------------|
| `vfs_open` | `int vfs_open(const char *path, int flags)` | Open a file |
| `vfs_close` | `vfs_close(int fd)` | Close a file descriptor |
| `vfs_read` | `int vfs_read(int fd, void *buf, size_t n)` | Read from file |
| `vfs_write` | `int vfs_write(int fd, const void *buf, size_t n)` | Write to file |
| `vfs_seek` | `int vfs_seek(int fd, int off, int whence)` | Seek file position |
| `vfs_stat` | `int vfs_stat(const char *path, stat_t *st)` | Stat file |
| `vfs_readdir` | `int vfs_readdir(int fd, dirent_t *ent)` | Read directory entry |
| `vfs_mkdir` | `int vfs_mkdir(const char *path)` | Create directory |
| `vfs_unlink` | `int vfs_unlink(const char *path)` | Delete file |

### Process Control

| Function | Signature | Description |
|----------|-----------|-------------|
| `exit` | `exit()` | Exit JIT program (returns to shell) |
| `yield` | `yield()` | Yield CPU to scheduler |
| `getpid` | `getpid()` | Current process PID |
| `kill` | `kill(pid)` | Kill process by PID |
| `sleep_ms` | `sleep_ms(uint32_t ms)` | Sleep for N milliseconds |

### Shell / Program

| Function | Signature | Description |
|----------|-----------|-------------|
| `shell_execute` | `shell_execute(const char *line)` | Execute shell command line |
| `shell_get_cwd` | `const char *shell_get_cwd()` | Get shell CWD string |
| `exec` | `int exec(const char *path, const char *name)` | Launch executable |

### System

| Function | Signature | Description |
|----------|-----------|-------------|
| `uptime_ms` | `uint32_t uptime_ms()` | Get system uptime in ms |
| `memstats` | `memstats()` | Print memory statistics |

### Networking - BSD sockets

Ports passed to / returned from these calls are network byte order
(`htons(80)` for HTTP). See [Networking](Networking) for protocol
details.

| Function | Signature |
|---|---|
| `socket` | `int socket(int type)` - `2`=TCP, `1`=UDP |
| `bind` | `int bind(int fd, U32 ip, U16 port)` |
| `listen` | `int listen(int fd, int backlog)` |
| `accept` | `int accept(int fd, U32 *peer_ip, U16 *peer_port)` |
| `connect` | `int connect(int fd, U32 ip, U16 port)` |
| `send` / `recv` | stream I/O on TCP socket |
| `sendto` / `recvfrom` | UDP datagram I/O |
| `close` | `int close(int fd)` |
| `dns_resolve` | `int dns_resolve(char *name, U32 *out)` |
| `htons` / `ntohs` / `htonl` / `ntohl` | byte-swap helpers |

Equ constants registered alongside: `IP_PROTO_ICMP`, `IP_PROTO_UDP`,
`IP_PROTO_TCP`, `SOCK_TYPE_UDP`, `SOCK_TYPE_TCP`.

### Networking - interface info & raw protocol

| Function | Description |
|---|---|
| `net_get_ip` / `_gateway` / `_dns` / `_mask` | Primary NIC info, returns `U32` |
| `net_get_mac(out)` | Fills 6-byte MAC into `out` |
| `net_link_up` | 1 if link up |
| `net_rx_packets` / `net_tx_packets` | Counters |
| `ip_parse(s, out)` | `"a.b.c.d"` -> uint32 |
| `ipv4_send(dst, proto, payload, plen)` | Raw IPv4 (auto-fragments > MTU) |
| `arp_resolve(ip, mac_out)` | Blocking 500 ms ARP |
| `arp_dump`, `arp_get_entries` | Cache inspection |
| `icmp_send_echo(dst, id, seq, paylen)` | Ping request |
| `icmp_wait_reply(src, id, seq, timeout_ms)` | Block for matching reply |
| `udp_send_raw(dst, sport, dport, data, len)` | One-shot UDP |

### Block devices

| Function | Description |
|---|---|
| `blkdev_count` | Number of block devices |
| `blkdev_read(idx, lba, count, buf)` | Read N sectors from blkdev[idx] |
| `blkdev_write(idx, lba, count, buf)` | Write N sectors |
| `ata_read_sectors(drive, lba, count, buf)` | Direct ATA read |
| `ata_write_sectors(drive, lba, count, buf)` | Direct ATA write |

### Keyboard, serial, speaker, PIT - direct driver access

| Function | Description |
|---|---|
| `keyboard_read_event(out)` | Pop one event |
| `keyboard_inject_scancode(sc)` | Synthesize scancode |
| `keyboard_get_shift` / `_ctrl` / `_alt` / `_caps_lock` | Modifier state |
| `serial_read_char` / `serial_write_char` / `serial_write_string` / `serial_has_rx` | Direct COM1 |
| `pc_speaker_on(freq)` / `pc_speaker_off()` | PC speaker square wave |
| `pit_set_frequency(channel, hz)` | Reprogram PIT |
| `timer_delay_us(us)` | TSC busy delay |
| `outb` / `inb` | Raw 8-bit port I/O for new drivers |

### PCI introspection (by index)

| Function | Description |
|---|---|
| `pci_device_count` | Number of PCI devices found at boot |
| `pci_get_vendor(idx)` | 16-bit vendor ID |
| `pci_get_device_id(idx)` | 16-bit device ID |
| `pci_get_class(idx)` | Packed `class<<16 | sub<<8 | prog_if` |
| `pci_get_irq(idx)` | IRQ line |
| `pci_get_bar(idx, bar)` | BAR value (0..5) |

### SMP / LAPIC / paging / PMM

> ⚠ Wrap shared-state work in `bkl_lock`/`bkl_unlock`.

| Function | Description |
|---|---|
| `lapic_get_id` | This CPU's local APIC ID |
| `lapic_eoi` | End-of-interrupt (only from a real ISR) |
| `bkl_lock` / `bkl_unlock` | Big kernel lock - recursive ticket spinlock |
| `paging_map_mmio(phys, size)` | Identity-map an MMIO region |
| `pmm_alloc_page` / `pmm_free_page(page)` | 4 KB physical page allocator |

### Audio - AC97 driver

| Function | Description |
|---|---|
| `ac97_init` | Probe + init AC97. Returns 0 on success in eax |
| `ac97_start` | Arm DMA |
| `ac97_stop` | Halt + mute |
| `ac97_set_master_volume(pct)` | 0-100 master volume |
| `ac97_tsc_sleep_ms(ms)` | TSC busy-wait |
| `ac97_is_present_int` | 0 / 1 |
| `ac97_smoke_sine` | 440 Hz triangle 2s |
| `ac97_smoke_sweep` | 50→8000 Hz sweep |
| `ac97_smoke_pan` | 1 kHz with L↔R pan |
| `audiotest_all` | sine + sweep + pan + opl |

### Audio - MIDI / OPL3 synth

| Function | Description |
|---|---|
| `midiopl_init(genmidi_lump, lump_len)` | Parse Doom GENMIDI patches |
| `midiopl_reset` | Silence channels, keep patches |
| `midiopl_feed(bytes, len)` | Stream MIDI bytes into synth |
| `midiopl_render(out_stereo, frames)` | Pull s16-stereo @ 22050 Hz |
| `midiopl_set_volume(0..127)` | Master synth volume |
| `opl_smoke` | OPL3 smoke test |

### Audio - PCM mixer

s16 stereo @ 22050 Hz, 16 slots.

| Function | Description |
|---|---|
| `mixer_init` | One-time init |
| `mixer_play(slot, pcm, frames, ch, loop, vol_l, vol_r)` | Start playback (returns 0 in eax on success) |
| `mixer_stop(slot)` | Stop slot |
| `mixer_active(slot)` | 1 if playing |
| `mixer_set_volume(slot, vol_l, vol_r)` | Per-slot volume |
| `mixer_fill(out, frames)` | Mix all active slots into `out` |

### Example: Audio smoke test

```asm
main:
    call ac97_init       ; init AC97 codec
    test eax, eax
    jnz  .done
    call ac97_smoke_sine ; 2s 440 Hz triangle
.done:
    ret
```

### Example: Using Kernel Bindings

```asm
section .data
    greeting db "Hello, ", 0
    name     db "CupidOS", 0
    newline  db 10, 0

section .text

main:
    push greeting
    call print
    add  esp, 4

    push name
    call strlen       ; returns length in eax
    add  esp, 4

    push eax
    call print_int    ; print the length
    add  esp, 4

    push newline
    call print
    add  esp, 4
    ret
```

---

## AOT Syscall Table (ELF Programs)

> Syscall table version: **3** (since Phase 5 of Networking). The layout is
> append-only - programs built against v2 still work and observe the new
> larger `SYS_TABLE_SIZE`. `kernel/core/syscall.c` has `_Static_assert` guards
> on the offsets below so a future field reorder fails to compile.

AOT-compiled programs receive a pointer to the syscall table at `[esp+4]` when executed. Use `SYS_*` constants (pre-defined as `equ` values) to call kernel functions indirectly:

```asm
section .text

main:
    mov  ebx, [esp+4]      ; syscall table pointer

    push msg
    call [ebx + SYS_PRINT]
    add  esp, 4

    ret

section .data
    msg db "Hello from AOT!", 10, 0
```

### SYS_* Constants

| Constant | Offset | Function |
|----------|--------|----------|
| `SYS_VERSION` | 0 | Version field |
| `SYS_TABLE_SIZE` | 4 | Table size |
| `SYS_SIZE` | 4 | Alias of `SYS_TABLE_SIZE` |
| `SYS_PRINT` | 8 | print() |
| `SYS_PUTCHAR` | 12 | putchar() |
| `SYS_PRINT_INT` | 16 | print_int() |
| `SYS_PRINT_HEX` | 20 | print_hex() |
| `SYS_CLEAR_SCREEN` | 24 | clear_screen() |
| `SYS_MALLOC` | 28 | kmalloc() |
| `SYS_FREE` | 32 | kfree() |
| `SYS_STRLEN` | 36 | strlen() |
| `SYS_STRCMP` | 40 | strcmp() |
| `SYS_STRNCMP` | 44 | strncmp() |
| `SYS_MEMSET` | 48 | memset() |
| `SYS_MEMCPY` | 52 | memcpy() |
| `SYS_VFS_OPEN` | 56 | vfs_open() |
| `SYS_VFS_CLOSE` | 60 | vfs_close() |
| `SYS_VFS_READ` | 64 | vfs_read() |
| `SYS_VFS_WRITE` | 68 | vfs_write() |
| `SYS_VFS_SEEK` | 72 | vfs_seek() |
| `SYS_VFS_STAT` | 76 | vfs_stat() |
| `SYS_VFS_READDIR` | 80 | vfs_readdir() |
| `SYS_VFS_MKDIR` | 84 | vfs_mkdir() |
| `SYS_VFS_UNLINK` | 88 | vfs_unlink() |
| `SYS_EXIT` | 92 | exit() |
| `SYS_YIELD` | 96 | yield() |
| `SYS_GETPID` | 100 | getpid() |
| `SYS_KILL` | 104 | kill() |
| `SYS_SLEEP_MS` | 108 | sleep_ms() |
| `SYS_SHELL_EXEC` | 112 | shell_exec() |
| `SYS_SHELL_EXEC_LINE` | 112 | Alias of `SYS_SHELL_EXEC` |
| `SYS_SHELL_CWD` | 116 | shell_cwd() |
| `SYS_UPTIME_MS` | 120 | uptime_ms() |
| `SYS_EXEC` | 124 | exec() |
| `SYS_VFS_RENAME` | 128 | vfs_rename() |
| `SYS_VFS_COPY_FILE` | 132 | vfs_copy_file() |
| `SYS_VFS_COPY` | 132 | Alias of `SYS_VFS_COPY_FILE` |
| `SYS_VFS_READ_ALL` | 136 | vfs_read_all() |
| `SYS_VFS_WRITE_ALL` | 140 | vfs_write_all() |
| `SYS_VFS_READ_TEXT` | 144 | vfs_read_text() |
| `SYS_VFS_WRITE_TEXT` | 148 | vfs_write_text() |
| `SYS_MEMSTATS` | 152 | memstats() |

### Phase 4/5 syscall table extensions (v3)

| Constant | Offset | Function |
|----------|--------|----------|
| `SYS_NET_GET_IP` | 156 | net_get_ip() |
| `SYS_NET_GET_GATEWAY` | 160 | net_get_gateway() |
| `SYS_NET_GET_DNS` | 164 | net_get_dns() |
| `SYS_NET_GET_MASK` | 168 | net_get_mask() |
| `SYS_NET_GET_MAC` | 172 | net_get_mac(out) |
| `SYS_NET_LINK_UP` | 176 | net_link_up() |
| `SYS_NET_RX_PACKETS` | 180 | net_rx_packets() |
| `SYS_NET_TX_PACKETS` | 184 | net_tx_packets() |
| `SYS_NET_RX_DROPS` | 188 | net_rx_drops() |
| `SYS_NET_TX_ERRORS` | 192 | net_tx_errors() |
| `SYS_IP_PARSE` | 196 | ip_parse(s, out) |
| `SYS_IPV4_SEND` | 200 | ipv4_send(dst, proto, payload, plen) |
| `SYS_ARP_RESOLVE` | 204 | arp_resolve(ip, mac_out) |
| `SYS_ARP_DUMP` | 208 | arp_dump() |
| `SYS_ARP_GET_ENTRIES` | 212 | arp_get_entries(ips, macs, max) |
| `SYS_ICMP_SEND_ECHO` | 216 | icmp_send_echo(dst, id, seq, paylen) |
| `SYS_ICMP_WAIT_REPLY` | 220 | icmp_wait_reply(src, id, seq, timeout_ms) |
| `SYS_UDP_SEND_RAW` | 224 | udp_send_raw(dst, sport, dport, data, len) |
| `SYS_DNS_RESOLVE` | 228 | dns_resolve(name, ip_out) |
| `SYS_HTONS` | 232 | htons(v) |
| `SYS_HTONL` | 236 | htonl(v) |
| `SYS_NTOHS` | 240 | ntohs(v) |
| `SYS_NTOHL` | 244 | ntohl(v) |
| `SYS_SOCKET` | 248 | socket(type) |
| `SYS_BIND` | 252 | bind(fd, ip, port) |
| `SYS_LISTEN` | 256 | listen(fd, backlog) |
| `SYS_ACCEPT` | 260 | accept(fd, peer_ip, peer_port) |
| `SYS_CONNECT` | 264 | connect(fd, ip, port) |
| `SYS_SEND` | 268 | send(fd, buf, len) |
| `SYS_RECV` | 272 | recv(fd, buf, len) |
| `SYS_SENDTO` | 276 | sendto(fd, buf, len, ip, port) |
| `SYS_RECVFROM` | 280 | recvfrom(fd, buf, len, ip, port) |
| `SYS_CLOSE` | 284 | close(fd) |
| `SYS_BLKDEV_COUNT` | 288 | blkdev_count() |
| `SYS_BLKDEV_READ` | 292 | blkdev_read(idx, lba, count, buf) |
| `SYS_BLKDEV_WRITE` | 296 | blkdev_write(idx, lba, count, buf) |
| `SYS_ATA_READ_SECTORS` | 300 | ata_read_sectors(drive, lba, count, buf) |
| `SYS_ATA_WRITE_SECTORS` | 304 | ata_write_sectors(drive, lba, count, buf) |
| `SYS_SERIAL_READ_CHAR` | 308 | serial_read_char() |
| `SYS_SERIAL_WRITE_CHAR` | 312 | serial_write_char(c) |
| `SYS_SERIAL_WRITE_STRING` | 316 | serial_write_string(s) |
| `SYS_SERIAL_HAS_RX` | 320 | serial_has_rx() |
| `SYS_PC_SPEAKER_ON` | 324 | pc_speaker_on(freq) |
| `SYS_PC_SPEAKER_OFF` | 328 | pc_speaker_off() |
| `SYS_PIT_SET_FREQUENCY` | 332 | pit_set_frequency(channel, hz) |
| `SYS_TIMER_DELAY_US` | 336 | timer_delay_us(us) |
| `SYS_PCI_DEVICE_COUNT` | 340 | pci_device_count() |
| `SYS_PCI_GET_VENDOR` | 344 | pci_get_vendor(idx) |
| `SYS_PCI_GET_DEVICE_ID` | 348 | pci_get_device_id(idx) |
| `SYS_PCI_GET_CLASS` | 352 | pci_get_class(idx) |
| `SYS_PCI_GET_IRQ` | 356 | pci_get_irq(idx) |
| `SYS_PCI_GET_BAR` | 360 | pci_get_bar(idx, bar) |
| `SYS_LAPIC_GET_ID` | 364 | lapic_get_id() |
| `SYS_LAPIC_EOI` | 368 | lapic_eoi() |
| `SYS_BKL_LOCK` | 372 | bkl_lock() |
| `SYS_BKL_UNLOCK` | 376 | bkl_unlock() |
| `SYS_PAGING_MAP_MMIO` | 380 | paging_map_mmio(phys, size) |
| `SYS_PMM_ALLOC_PAGE` | 384 | pmm_alloc_page() |
| `SYS_PMM_FREE_PAGE` | 388 | pmm_free_page(page) |
| `SYS_OUTB` | 392 | outb(port, val) |
| `SYS_INB` | 396 | inb(port) |

Equ constants registered alongside (compile-time literals, no syscall):
`IP_PROTO_ICMP`, `IP_PROTO_UDP`, `IP_PROTO_TCP`, `SOCK_TYPE_UDP`,
`SOCK_TYPE_TCP`.

Example - outbound TCP from AOT asm:

```asm
section .text

main:
    mov  ebx, [esp+4]                  ; syscall table

    push 2                             ; SOCK_TYPE_TCP
    call [ebx + SYS_SOCKET]
    add  esp, 4
    mov  edi, eax                      ; fd

    push 80
    call [ebx + SYS_HTONS]             ; htons(80)
    add  esp, 4

    push eax                           ; port (network order)
    push 0x08080808                    ; 8.8.8.8 - replace w/ real IP
    push edi
    call [ebx + SYS_CONNECT]
    add  esp, 12
    ret
```

---

## Memory Addressing

CupidASM supports Intel-style memory operands with base, index, scale, and displacement:

```asm
mov eax, [ebx]           ; base only
mov eax, [ebp+8]         ; base + displacement
mov eax, [ebx+ecx*4]     ; base + index*scale
mov eax, [ebx+ecx*4+16]  ; base + index*scale + displacement
mov eax, [label]         ; absolute address (label)
```

**Supported scales**: 1, 2, 4, 8

---

## Memory Layout

| Region | Address | Size |
|--------|---------|------|
| JIT Code | `0x500000` | 128 KB |
| JIT Data | `0x520000` | 32 KB |

JIT code and data are separate from CupidC's JIT region (`0x400000`). Both can coexist.

---

## Demo Programs

Demo programs are included in the `demos/` folder:

| File | Description | Key Concepts |
|------|-------------|-------------|
| `hello.asm` | Hello World | `db`, `print`, sections, entry point |
| `math.asm` | Arithmetic operations | `add`, `sub`, `shl`, `div`, `and`, `or` |
| `loop.asm` | Sum 1..100 = 5050 | `cmp`, `jl`, `inc`, local labels |
| `stack.asm` | Function calls | Stack frames, `push`/`pop`, `call`/`ret` |
| `fibonacci.asm` | Fibonacci sequence | Register saves, loops, `print_int` |
| `factorial.asm` | Recursive factorial | Recursion, `mul`, stack frames |
| `data.asm` | Data directives | `db`, `dd`, arrays, `strlen` |
| `bubblesort.asm` | Bubble sort | Memory indexing, `shl`, in-place swap |
| `fs_syscalls.asm` | VFS/syscall usage | `vfs_open/read/write/close`, `getpid`, constants |
| `reserve_directives.asm` | Reserve directives | `resw` and `resd` layout/size checks |
| `asm_compat_reserve.asm` | NASM-compat reserve syntax | `rb/rw/rd`, `reserve`, `label: resb ...` |
| `syscall_table_demo.asm` | Syscall table calls | `syscall_get_table`, `SYS_*` offsets |
| `parity_core.asm` | Core parity smoke test | shell/process/time/rtc/string parity bindings |
| `parity_gfx2d.asm` | gfx2d parity smoke test | drawing/text/fullscreen-safe gfx2d calls |
| `parity_diag.asm` | Diagnostics parity smoke test | variadic print, logs, heap/stack/register diagnostics |
| `syscall_vfs_extended_demo.asm` | Extended syscall table VFS | `SYS_VFS_*` copy/read/write helper calls |

### Running Demos

```
> as demos/hello.asm
Hello from CupidASM!

> as demos/loop.asm
Sum of 1..100 = 5050

> as demos/factorial.asm
Factorials:
1! = 1
2! = 2
3! = 6
4! = 24
5! = 120
6! = 720
7! = 5040
8! = 40320
9! = 362880
10! = 3628800

> as demos/stack.asm
add_numbers(15, 27) = 42
multiply(6, 7) = 42
```

---

## Complete Example: Bubble Sort

```asm
; bubblesort.asm - Bubble sort on an integer array
section .data
    arr     dd 5, 3, 8, 1, 9, 2, 7, 4
    count   dd 8
    msg_before db "Before: ", 0
    msg_after  db "After:  ", 0
    space      db " ", 0
    newline    db 10, 0

section .text

; print_array() - prints all elements
print_array:
    push ebp
    mov  ebp, esp
    push esi
    push ecx

    mov  esi, arr
    mov  ecx, [count]
.print_loop:
    cmp  ecx, 0
    je   .print_done
    push ecx
    push dword [esi]
    call print_int
    add  esp, 4
    push space
    call print
    add  esp, 4
    pop  ecx
    add  esi, 4
    dec  ecx
    jmp  .print_loop
.print_done:
    push newline
    call print
    add  esp, 4
    pop  ecx
    pop  esi
    pop  ebp
    ret

; bubble_sort() - sorts arr[] in place
bubble_sort:
    push ebp
    mov  ebp, esp
    push esi
    push edi
    push ebx

    mov  ecx, [count]
    dec  ecx              ; outer loop: n-1 passes
.outer:
    cmp  ecx, 0
    jle  .sort_done
    xor  edi, edi         ; inner index
    mov  edx, ecx         ; inner limit
.inner:
    cmp  edi, edx
    jge  .next_pass

    ; Compare arr[edi] and arr[edi+1]
    mov  eax, edi
    shl  eax, 2           ; eax = edi * 4
    add  eax, arr         ; eax = &arr[edi]
    mov  ebx, [eax]       ; ebx = arr[edi]
    mov  esi, [eax+4]     ; esi = arr[edi+1]
    cmp  ebx, esi
    jle  .no_swap

    ; Swap
    mov  [eax], esi
    mov  [eax+4], ebx

.no_swap:
    inc  edi
    jmp  .inner

.next_pass:
    dec  ecx
    jmp  .outer

.sort_done:
    pop  ebx
    pop  edi
    pop  esi
    pop  ebp
    ret

main:
    push msg_before
    call print
    add  esp, 4
    call print_array

    call bubble_sort

    push msg_after
    call print
    add  esp, 4
    call print_array

    ret
```

---

## Differences from NASM

| Feature | NASM | CupidASM |
|---------|------|----------|
| Output formats | ELF, bin, COFF, etc. | JIT (execute) or ELF32 |
| Macros | Full macro system | Not supported |
| Preprocessor | `%define`, `%macro`, `%if` | `%include` only |
| Segments | Full segment support | `.text` and `.data` only |
| Linker | Separate step | Built-in (single file) |
| External symbols | Via linker | Kernel bindings (JIT) |
| Expressions | Full constant expressions | Numeric literals only |
| Local labels | `@@`, `.label` | `.label` (dot prefix) |
| 16/64-bit modes | `[bits 16]`, `[bits 64]` | 32-bit only |

---

## Error Messages

| Error | Cause |
|-------|-------|
| `cannot open <file>` | File not found or not readable |
| `file too large or empty` | Source exceeds 256 KB or is 0 bytes |
| `no main: or _start: label found` | Missing entry point label |
| `undefined label '<name>'` | Forward reference to a label that was never defined |
| `duplicate label` | Same label name defined twice |
| `too many labels` | More than 512 labels |
| `too many forward references` | More than 512 unresolved references |
| `code buffer overflow` | Code section exceeds 128 KB |
| `data buffer overflow` | Data section exceeds 32 KB |
| `expected end of line` | Extra tokens after an instruction |
| `short jump out of range` | Branch target too far for rel8 |

---

## Tips

- **Entry point**: Every program needs a `main:` or `_start:` label.
- **Return from main**: Use `ret` to return control to the shell. Don't `hlt` - that stops the whole OS.
- **Preserve registers**: If you call kernel functions, save `ecx` and `edx` first - they may be clobbered.
- **Null-terminate strings**: All strings passed to `print` must end with `0`.
- **Clean the stack**: After `call`, always `add esp, N` where N = number of bytes pushed as arguments.
- **Forward references**: Labels can be referenced before they're defined - the assembler patches them automatically.
- **Case insensitive**: Mnemonics and register names are case-insensitive (`MOV EAX, 1` works).
