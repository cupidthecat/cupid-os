# Hardening & Polish — PR Notes

Reference doc for opening a PR. Three independent phases, each
self-contained and shippable on its own. Pick splits as you like — the
phases share no dependencies beyond touching adjacent files.

Plan file (source-of-truth): `/home/frank/.claude/plans/what-should-be-aded-wild-boole.md`

---

## Suggested PR splits

| Option | Scope | Files touched |
|---|---|---|
| **One big PR** | All three phases together | ~15 files |
| **Three PRs** | Phase 1 / Phase 2 / Phase 3 | 5 / 6 / 6 files |
| **Four PRs** | P1 bugs, P2A compiler, P2B utils, P3 symbols | 5 / 1 / 5 / 6 files |

Recommend the four-PR split — each is reviewable in one sitting and
each has its own verification story.

---

## Phase 1 — Critical bug fixes

Five small surgical patches. All low-risk, all independently verifiable.

### 1.1 ELF integer overflow in `exec.c`

**File:** `kernel/exec.c`

**Problem:** A crafted ELF with `p_vaddr = 0x80000000` and
`p_memsz = 0x80000001` makes `seg_end = p_vaddr + p_memsz` wrap to `1`,
which then bypasses the `max_vaddr > IDENTITY_MAP_SIZE` check at line
176 and lets the loader write into kernel space.

**Fix:** Reject any segment where `p_memsz > IDENTITY_MAP_SIZE` or
`p_vaddr > IDENTITY_MAP_SIZE - p_memsz` *before* the addition. Same
class of bug in the CUPD flat-binary loader (`code_size + data_size +
bss_size`); bound each section individually first so the sum can't
wrap.

**Affected ranges:** `kernel/exec.c` ~lines 132–151 (ELF path) and
~lines 295–315 (CUPD path).

### 1.2 Stack overrun in icon scanner

**File:** `kernel/gfx2d_icons.c:702-704`

**Problem:** `strcpy(path, "/bin/"); strcat(path, ent.name);` writes
into a `char path[GFX2D_ICON_PATH_MAX]` (128 bytes) with no bound on
`ent.name`. Today `vfs_dirent.name` is structurally bounded to
`VFS_MAX_NAME-1 = 63` chars by every existing readdir backend, so this
isn't currently exploitable — but a future backend could write more,
and the pattern is brittle.

**Fix:** Inline bounded copy that respects `GFX2D_ICON_PATH_MAX-1`. No
new helper needed; matches the style at `gfx2d_icons.c:847`.

### 1.3 IRQ handler chain race

**File:** `kernel/irq.c:1-79`

**Problem:** `irq_install_handler()` writes `irq_handlers[][]` while
`irq_handler()` reads from IRQ context. On SMP one CPU can see a
half-built slot; on UP an interrupt firing during install can read
mid-update.

**Fix:**
- `irq_handlers[][]` marked `volatile` so the dispatcher always
  re-reads each slot.
- New `irq_lock()` / `irq_unlock()` helpers take the BKL when
  available (covers SMP; BKL also disables IRQs locally). Falls back
  to no-op during very-early boot before `bkl_init()` — at that point
  only the BSP is running and the IOAPIC has not yet been unmasked, so
  no race is possible.
- Added `#include "bkl.h"`.

### 1.4 `process_set_image` race

**File:** `kernel/process.c:867-874`

**Problem:** `image_base` and `image_size` were assigned without a
lock; the scheduler can read a torn pair if `exec()` is mid-write.

**Fix:** `bkl_lock()`/`bkl_unlock()` around the assignment. Publishes
`image_size = 0` first so any concurrent reader that sees the new
`image_base` before the new `image_size` observes an empty region
rather than a stale combination.

### 1.5 UDP receive buffer wraparound — closed as not-a-bug

**File:** `kernel/socket.c:170-180`

The audit flagged this. Re-read confirmed the existing check
`used + dlen >= SOCK_RX_BUF` (note the `>=`, intentionally
one-byte conservative) correctly enforces the bound before any write.
No code change.

### Phase 1 verification

```
make clean && make           # builds with no new warnings
timeout 8 qemu-system-i386 -m 128M -boot c \
  -drive file=cupidos.img,format=raw,if=ide,index=0,media=disk \
  -display none -serial file:/tmp/smoke.log -no-reboot
grep -E "panic|FAULT" /tmp/smoke.log   # no hits
tail /tmp/smoke.log                    # ends with "Entering desktop environment"
```

End-to-end: 88 (now 93 after Phase 2B) `.cc` programs install, icons
register through the new bounded path, IRQ subsystem comes up, no
panics for the 8s smoke window.

---

## Phase 2A — CupidC compiler completions

**File:** `kernel/cupidc_parse.c`

### FP compound assignment (`+= -= *= /=` on float / double)

Was `cc_error("FP compound assignment not yet supported (Task 18)")`
at line 3116. Now generates code:

```
spill RHS (XMM0) to esp        ; sub esp, 8 ; movss/sd [esp], xmm0
load LHS into XMM0             ; movss/sd xmm0, [ebp+off] | [disp32]
reload spilled RHS into XMM1   ; movss/sd xmm1, [esp]
discard spill                  ; add esp, 8
ADDSS/SUBSS/MULSS/DIVSS xmm0,xmm1 (or PD variants for double)
store XMM0 back to LHS
```

Works for `SYM_LOCAL`, `SYM_PARAM`, and `SYM_GLOBAL` destinations. Uses
the existing `emit_sse_scalar_op()` helper (the same instruction-emit
the binary FP path uses for `a + b`), so the encoding is identical to
already-proven code paths. Bitwise/shift compound ops on FP types are
explicitly rejected with a clearer error.

### SIMD compound assignment + non-local SIMD `=`

Two improvements at the float4/double2 assignment path
(`cupidc_parse.c:3086+`):

1. **Compound** (`+= -= *= /=`) emits packed-vector ops:
   `ADDPS/SUBPS/MULPS/DIVPS` for float4, `66`-prefixed `ADDPD/...` for
   double2. Same spill / load / reload / op / store pattern as FP
   scalars but via `MOVUPS xmm, [esp]` on a 16-byte slot.

2. **Plain `=` on a global** (`SYM_GLOBAL`) now emits
   `MOVUPS [disp32], xmm0`. Previously errored with
   `"SIMD assignment to non-local not supported"`.

### Regression test

`bin/test_fpaug.cc` — exercises `+=`, `-=`, `*=`, `/=` on float and
double locals plus int → float coercion. Tests via integer scaling and
casts (no `print_float` binding exists, matching the convention in
`bin/feature12_float.cc`). Reports PASS/FAIL on serial and console.

### Deferred

- **Mixed int/FP function args** (`cupidc_parse.c:1800`) — the
  argument-slot reordering pass only handles same-size pairs; the
  variable-width block reverse needed for mixed `int + double` args is
  a substantial parser change with no existing use case in tree.
  Workaround: separate calls or all-int / all-FP arg lists. Issue
  documented in the existing comment.

- **Plain-int brace initializers** (`int x[] = {1, 2, 3}`) — the
  audit-flagged "non-scalar initializer" message at lines 4291/4303
  actually refers to FP scalars receiving non-scalar RHS. SIMD literal
  inits (`float4 v = {1, 2, 3, 4}`) and FP scalar inits already work
  (`cupidc_parse.c:4213-4256`). Plain int-array brace init would need
  its own parser+codegen path; element-by-element assignment is the
  current workaround.

---

## Phase 2B — Missing shell utilities

Four new `.cc` programs in `bin/`. The Makefile auto-discovers anything
matching `bin/*.cc`, embeds it via `objcopy`, and exposes it under
`/bin/` in the RamFS — no Makefile change needed.

### `bin/wc.cc` (~100 lines)

`wc [-l|-w|-c] <file>` — counts lines, words, bytes. Default prints
all three plus filename. Same arg-parsing pattern as the existing
utilities.

### `bin/head.cc` (~95 lines)

`head [-n N] <file>` — first N lines (default 10). Streams from
`vfs_read` 256 bytes at a time; truncates output at 64KB as a safety
cap (matches the `bin/cat.cc` pattern).

### `bin/tail.cc` (~115 lines)

`tail [-n N] <file>` — last N lines (default 10). Slurps up to 64KB
with `kmalloc`, counts newlines, walks back to the start of line N,
prints to EOF. `kfree`s the buffer on every exit path.

### `bin/sort.cc` (~135 lines)

`sort [-r] <file>` — lex-sort lines, `-r` reverses. Slurps up to 64KB,
in-place replaces `\n` with `\0`, builds an offsets array (cap 4096
lines), insertion-sorts. Trades sort speed for code simplicity (no
recursion, no quicksort stack depth concerns).

### Already present (audit found them after I marked tasks)

`bin/grep.cc` — recursive `grep <pattern> <path>` with line:n:content
output.

`bin/find.cc` — recursive `find [path] [name]`.

### Phase 2B verification

```
make                          # new .cc files appear in build line
grep "Installed /bin/wc.cc"   /tmp/smoke.log   # in boot log
# Manual: from QEMU shell, run:
#   ls /bin | grep .cc | wc -l
#   head -n 5 /docs/00INDEX
#   sort /tmp/foo
```

---

## Phase 3 — Kernel symbols + panic backtrace

Single biggest debugging upgrade. Before: panics print raw addresses
that you cross-reference against `nm` output by hand. After: panics
print `addr  function_name+offset` per frame.

### New / changed files

| File | Purpose |
|---|---|
| `tools/mksyms.sh` (new) | Host script: reads kernel.elf via `nm -n`, packs into binary blob, emits as a C const array |
| `kernel/ksyms.h` (new) | API: `ksym_lookup`, `ksym_backtrace` |
| `kernel/ksyms.c` (new) | Runtime: weak fallback blob + binary-search lookup + frame-pointer walker |
| `kernel/ksyms_data.c` (generated) | Strong blob, overrides the weak one. Generated by `tools/mksyms.sh` from the pass-1 ELF |
| `link.ld` | Removed `OUTPUT_FORMAT("binary")` (now selected on cmdline); added `.ksyms` section between `.data` and `.bss` alignment, with `__ksyms_start` / `__ksyms_end` linker symbols |
| `Makefile` | Added `-fno-omit-frame-pointer` to CFLAGS; added `LDFLAGS_ELF`; replaced single-pass kernel link with a two-pass build (pass 1 → mksyms → pass 2 → objcopy → bin) |
| `kernel/panic.c` | `print_stack_trace()` now calls `ksym_backtrace()`; new `panic_print_frame()` callback prints `addr  name+offset` to both VGA and serial |

### Blob format (documented in `tools/mksyms.sh`)

```
Header (16 bytes, little-endian):
  uint32_t magic        ('KSYM' = 0x4D59534B)
  uint32_t count
  uint32_t string_off   (offset of string table from blob start)
  uint32_t total_size

Entries (count * 8):
  uint32_t addr
  uint32_t name_off

String table: NUL-terminated function names, packed.
```

### Why two passes?

Symbol addresses are determined at link time, but the symbol blob
itself contributes to the binary. Two-pass solves the chicken-and-egg:

1. **Pass 1** links with `kernel/ksyms.o`'s 16-byte weak placeholder
   blob → `kernel/kernel.elf.pass1`.
2. **mksyms** reads pass-1 ELF → emits `kernel/ksyms_data.c` with the
   real blob (~48KB for ~2000 symbols).
3. **Pass 2** links again with `kernel/ksyms_data.o` added — its
   strong `ksym_blob` symbol overrides the weak one.

Code addresses don't shift between passes because `.ksyms` is placed
*after* `.text` / `.rodata` / `.data` in `link.ld`. Only the `.bss`
start moves.

### Lookup correctness — return-address adjustment

A return address points to the byte *after* the CALL instruction. If
the caller's CALL is the last thing in its function body, the literal
return address can land at the start of the *next* function's symbol.
Standard fix in unwinders: look up `(ret_addr - 1)` for non-zero
frames. `ksym_backtrace` does this; without it, a panic from `kmain`
got mis-attributed to `idt_set_gate+0x0` because `idt_set_gate`
happens to start exactly where `kmain` ends.

### Frame pointers

GCC default at `-O2` is `-fomit-frame-pointer`, which would break the
`%ebp`-chain walk. Added `-fno-omit-frame-pointer` to global CFLAGS.
Cost: ~1 register per frame, small instruction-count overhead. Worth
it for a hobby OS where panic debuggability matters more than the last
1% of perf.

### Graceful fallback

If `ksyms_data.o` is absent or the blob is corrupt, `ksym_lookup`
returns `NULL` and `panic_print_frame` falls back to printing raw
addresses. Build still succeeds; panics still work; only the symbol
decoding silently degrades. The weak fallback in `kernel/ksyms.c`
guarantees the link doesn't fail even if mksyms is removed.

### Verification

Temporary `-DKSYM_BACKTRACE_TEST` gate in `kernel/kernel.c` (added,
verified, then reverted) calls `kernel_panic("KSYM_BACKTRACE_TEST")`
right before desktop launch. Output:

```
STACK TRACE:
  #0: 0x0011852a  kernel_panic+0x00000084
  #1: 0x00100f3b  kmain+0x00000815
  #2: 0x00100027  _start+0x00000026
  #3: 0x00008304                              ← bootloader, no symbol (expected)
```

The chain matches the actual call path: bootloader → `_start` →
`kmain` → `kernel_panic`. mksyms reported "1998 symbols, 47779 bytes".

---

## Phase 4 — Networking stack completion + integration tests

The TCP/IP stack was ~95% scaffolded (drivers, ARP, IP, ICMP, UDP, TCP,
sockets, DHCP, DNS, shell `ifconfig`/`ping`/`netstat`/`arp`, CupidC
bindings, `feature21_net.cc` / `feature22_net_server.cc`) but four real
gaps + two latent bugs prevented end-to-end use. This phase fills them
and adds an integration suite that verifies the stack on **both** RTL8139
and Intel E1000 against QEMU SLIRP and real hosts on the public Internet.

### 4.1 TCP data retransmission

**Files:** `kernel/tcp.c`, `kernel/socket.h`

**Problem:** Only `SYN` was retried. Lost `PSH+ACK` data segments caused
the receiver to silently time out — fatal under any packet loss.

**Fix:** Per-socket retransmit slot (`rt_buf[TCP_MSS]`, `rt_seq`,
`rt_send_tick`, `rt_attempts`) added to `socket_t`. Refactor
`tcp_send_seg` into `tcp_emit` (low-level: build + send at explicit seq,
no bookkeeping) plus a wrapper that advances `snd_nxt` and buffers any
`PSH`+data segment. `tcp_tick` walks established sockets each
`net_process_pending`, resends after `TCP_RTO_MS << rt_attempts`,
exponential backoff, gives up after 5 attempts (state → `CLOSED`).
`tcp_send` is now stop-and-wait: only one in-flight `PSH` segment
permitted, blocks (with `schedule()` + 30s deadline) until the previous
one is ACKed. `tcp_input` clears `rt_len` when the cumulative `ack`
covers `rt_seq + rt_len`.

### 4.2 ARP cache TTL eviction

**Files:** `kernel/arp.c`, `kernel/arp.h`, `kernel/net_if.c`

**Problem:** Entries lived until LRU eviction. A peer that changed MAC
would silently misroute until cache pressure forced reuse.

**Fix:** Per-entry `inserted_ms`, new `arp_tick()` exported via header,
called from `net_process_pending` right after `tcp_tick()`. Entries
older than `ARP_TTL_MS = 300_000` (5 min, RFC 1122 minimum) flip to
`valid = false` and get re-resolved on next use.

### 4.3 IP fragmentation + reassembly

**File:** `kernel/ip.c`

**Problem:** `ipv4_input` early-returned with
`KWARN("ip: dropping fragmented packet")` for any frame with
`MF` or non-zero offset. `ipv4_send` couldn't emit > 1500-byte payloads
at all. Broke any DNS response with `>1480` bytes of records and any
TCP burst the gateway fragmented.

**Fix:**
- **Send side**: refactored into `ipv4_send_one()` (single-fragment
  emit) + `ipv4_send()` driver. If `20 + plen > MTU`, splits payload on
  8-byte boundaries (offset is in 8-byte units), reuses the same IP id
  across fragments, sets `MF=1` on all but the last.
- **Receive side**: 4-slot reassembly table keyed by
  `(src_ip, dst_ip, id, proto)`. Each slot has a 65 535-byte buffer
  and an 8 192-bit completion bitmap (1 bit per 8-byte unit).
  `total_len` set when the `MF=0` fragment arrives.
  `reasm_complete()` checks every required bit. On full reassembly,
  the slot snapshots its data, frees, then dispatches to ICMP/UDP/TCP.
- **GC**: `reasm_gc()` runs at the top of every `ipv4_input` call,
  evicts slots older than `IP_REASM_TIMEOUT_MS = 30_000`.

### 4.4 Listen-queue half-open garbage collection

**Files:** `kernel/socket.h`, `kernel/tcp.c`

**Problem:** `lq[]` slots set on incoming `SYN` were never freed if the
3rd ACK never arrived. Eight stuck SYN-RCVD entries permanently DoS'd
the listener.

**Fix:** Added `lq_inserted_ms` to the per-slot anonymous struct in
`socket_t`. Set when `SYN` allocates the slot. `tcp_tick` walks
`LISTEN` sockets and clears any slot with
`in_use && !completed && now - lq_inserted_ms > 30_000`.

### 4.5 Latent bug: double-htons in socket layer

**Files:** `kernel/socket.c`, `kernel/dns.c`

**Problem:** Internal storage convention was host byte order
(`tcp_send_seg` does `be16(s->remote_port)` to write wire bytes), but
user callers — following BSD convention — passed `htons(port)`. Result:
the wrapper byte-swapped a value that was already byte-swapped, sending
port `0x5000` (20480) on the wire instead of `80`. `feature21_net.cc`
SYNs went to dead ports and connect timed out. Caught by inspecting the
filter-dump pcap: scapy showed `dport=20480` for a connection to
`example.com:80`.

**Fix:** Socket-layer entry points now `ntohs()` ports on the way in
(`socket_bind`, `socket_connect`, `socket_sendto`) and `htons()` on the
way out (`socket_accept`, `socket_recvfrom`), matching standard BSD
semantics. `dns.c` updated to wrap its literal `53u` in `htons()` at
the only in-tree caller of `socket_sendto` that wasn't going through
the user API.

### 4.6 Latent bug: E1000 driver silently broken

**File:** `kernel/e1000.c`

**Problem:** Plain `make run-net-e1000` looked like it booted but DHCP
fell back to the static IP. Two MAC-config bits were wrong:

1. `RCTL = 0x0400804u` — comment claimed `EN | BAM | BSIZE_2048`, but
   the literal **didn't include the EN (bit 1) flag**. Receiver was
   never enabled; nothing got delivered to the IRQ handler.
2. `TCTL = 0x01030002u` — missing PSP (bit 3, "pad short packets"). Any
   42-byte ARP frame got silently dropped by the MAC because it was
   below the 60-byte Ethernet minimum.

**Fix:** `RCTL = 0x04008002u` (EN | BAM | SECRC, default BSIZE_2048).
`TCTL = 0x0004010Au` (EN | PSP | reasonable CT/COLD).

### 4.7 Test harness

**Files (new):** `tools/net_test.py`, `tools/net_pcap.py`,
plus `headless-net-image`, `test-net-quick`, `test-net` Makefile
targets.

`tools/net_test.py` (~250 LoC, pexpect):
- spawns headless QEMU with `-display none -serial stdio
  -netdev user,id=n0,hostfwd=tcp::18080-:80 -device <nic>,netdev=n0
  -object filter-dump,id=f0,netdev=n0,file=tests/<nic>.pcap`
- waits for the `/> ` shell prompt over COM1 (the existing
  `keyboard.c:479` serial-RX path makes this work without any guest
  changes)
- runs `ifconfig`, `ping 10.0.2.2 2`, `arp` and parses output (with a
  `_scrub()` helper that strips the kernel's `[print_int]` debug lines
  that interleave with shell output)
- runs `/bin/feature21_net.cc` and looks for `[feature21] PASS`
- runs `/bin/feature22_net_server.cc`, opens a host TCP socket to
  `127.0.0.1:18080`, sends `GET /`, verifies the body is
  `Hello CupidOS`, then waits for `[feature22] PASS` from the guest

`tools/net_pcap.py` (~140 LoC, scapy) re-validates the captured frames
at the wire level: ARP req/reply pairing, full 4-message DHCP exchange,
ICMP echo-request → echo-reply, TCP `SYN` / `SYN-ACK` / `FIN`, at least
one SYN to a public (non-RFC1918) destination, and recomputes every IP
header checksum to confirm zero corrupt headers.

### Phase 4 verification

```
make test-net      # rtl8139 + e1000, ~30s total
```

Output (each line is one assertion):

```
=== rtl8139 results (pcap=tests/rtl8139.pcap) ===
  [PASS] dhcp        (10.0.2.15)
  [PASS] ping_gw     (recv=2)
  [PASS] arp         (entry present)
  [PASS] tcp_client  (feature21 PASS)
  [PASS] tcp_server  (host got Hello CupidOS)
=== rtl8139: OK ===

=== e1000 results (pcap=tests/e1000.pcap) ===
  [PASS] dhcp        (10.0.2.15)
  [PASS] ping_gw     (recv=2)
  [PASS] arp         (entry present)
  [PASS] tcp_client  (feature21 PASS)
  [PASS] tcp_server  (host got Hello CupidOS)
=== e1000: OK ===

=== tests/rtl8139.pcap ===  (and same for e1000.pcap)
  OK   ARP requests: 2
  OK   ARP replies:  2
  OK   DHCP DISCOVER: 1
  OK   DHCP OFFER:    1
  OK   DHCP REQUEST:  1
  OK   DHCP ACK:      1
  OK   ICMP echo-request: 2
  OK   ICMP echo-reply:   2
  OK   TCP SYN sent:     2
  OK   TCP SYN-ACK rcvd: 2
  OK   TCP FIN seen:     4
  OK   TCP SYN to public IP: 1
  OK   Bad IP checksums: 0
```

End-to-end this exercises: NIC IRQ → ARP cache → DHCP autoconfig → DNS
A-record query (UDP/53 to 10.0.2.3) → TCP 3-way handshake → HTTP GET to
`example.com:80` → server-side bind/listen/accept/send/close from a
host-initiated connection through QEMU's hostfwd.

### Deferred (out of scope for this PR)

- TCP Nagle, delayed-ACK, window scaling, congestion control. The
  stack is stop-and-wait at MSS granularity; throughput is poor but
  correctness is solid.
- IPv6, multicast, raw sockets.
- Multi-NIC routing (only the primary NIC is consulted).

---

## Phase 5 — CupidC / CupidASM / ELF binding expansion

The CupidC compiler exposed kernel APIs via a `BIND()` table in
`cupidc.c`; CupidASM had its own `AS_BIND()` table in `as.c`; ELF
programs received a `cupid_syscall_table_t` struct from `syscall.h`.
The three surfaces had drifted apart, and none exposed networking,
block devices, or low-level driver APIs. This phase brings full parity
across the three runtimes.

### 5.1 CupidC bindings (`kernel/cupidc.c`)

Added 53 new `BIND()` entries plus the matching wrappers near the top
of `cc_register_kernel_bindings`. Categories:

| Category | New bindings |
|---|---|
| Network info | `net_get_ip`, `net_get_gateway`, `net_get_dns`, `net_get_mask`, `net_get_mac`, `net_link_up`, `net_rx_packets`, `net_tx_packets`, `net_rx_drops`, `net_tx_errors` |
| IP / ARP / ICMP / UDP raw | `ip_parse`, `ipv4_send`, `arp_resolve`, `arp_dump`, `arp_get_entries`, `icmp_send_echo`, `icmp_wait_reply`, `udp_send_raw` |
| Byte-order + protocol consts | `ntohs`, `ntohl`, `IP_PROTO_ICMP`, `IP_PROTO_UDP`, `IP_PROTO_TCP` |
| Block devices | `blkdev_count`, `blkdev_read`, `blkdev_write`, `ata_read_sectors`, `ata_write_sectors` |
| Keyboard direct | `keyboard_read_event`, `keyboard_inject_scancode`, `keyboard_get_shift`, `keyboard_get_ctrl`, `keyboard_get_alt`, `keyboard_get_caps_lock` |
| Serial direct | `serial_read_char`, `serial_write_char`, `serial_write_string`, `serial_has_rx` |
| PIT / timer | `pit_set_frequency`, `timer_delay_us` |
| PCI introspection (by index) | `pci_device_count`, `pci_get_vendor`, `pci_get_device_id`, `pci_get_class`, `pci_get_irq`, `pci_get_bar`, `pci_bar_is_mmio`, `pci_enable_bus_master` |
| SMP / LAPIC | `lapic_get_id`, `lapic_eoi` |
| Concurrency / paging | `bkl_lock`, `bkl_unlock`, `paging_map_mmio`, `pmm_alloc_page`, `pmm_free_page` |

Wrappers like `cc_pci_vendor(int idx)` adapt the kernel's
opaque-pointer APIs (`pci_device_t *pci_get_device(int)`) into
index-based getters that the CupidC ABI can call without exposing the
internal struct.

### 5.2 CupidASM bindings (`kernel/as.c`)

Same 50+ APIs registered via `AS_BIND()` in
`as_register_kernel_bindings`, plus the **full BSD socket family**
that CupidC already had (`socket`/`bind`/`listen`/`accept`/`connect`/
`send`/`recv`/`sendto`/`recvfrom`/`close`) and `outb`/`inb` for driver
authors. Added `IP_PROTO_*` and `SOCK_TYPE_*` as `as_bind_equ`
constants so AOT asm programs can use them as compile-time literals.

### 5.3 ELF syscall table (`kernel/syscall.h`, `kernel/syscall.c`)

Bumped `CUPID_SYSCALL_VERSION` 2 → 3. Appended 60 new function-pointer
fields to `cupid_syscall_table_t`, populated them in `syscall_init()`.
The append-only layout means programs built against v2 still work —
they observe a larger `table_size` but only access the prefix they
know.

Added `_Static_assert` checks on the offsets of key fields
(`memstats`, `net_get_ip`, `ipv4_send`, `sock_socket`, `blkdev_count`,
`pci_device_count`, `inb_io`) so future field reorders fail at compile
time instead of silently shipping a layout mismatch with the AOT
`SYS_*` equ constants.

### 5.4 AOT syscall offset constants (`kernel/as.c`)

Added 60 new `SYS_*` `as_bind_equ` constants (offsets 156–396) so AOT
`.asm` programs can do `call [ebx + SYS_NET_GET_IP]` against the
syscall-table pointer they receive in `ebx`. The static_asserts in
syscall.c keep these honest.

### 5.5 `curl` and `wget` — proof the surface is usable

`bin/curl.cc` (~280 lines) and `bin/wget.cc` (~250 lines) are real
HTTP/1.0 clients written in CupidC against the new bindings. No new
kernel features were needed — the Phase 4 socket layer plus the
existing VFS bindings (`vfs_open`/`vfs_write`/`vfs_close` with
`O_WRONLY|O_CREAT|O_TRUNC`) cover everything.

`curl`:
- `curl <url>` — body to stdout
- `-o file` — body to file
- `-i` — include response headers
- `-s` — silent (suppress error messages)
- `-X METHOD` / `-d data` (auto-sets POST) / `-H "Header: val"`
- URL parser handles `http://host[:port][/path]`, rejects `https://`
  with a clear "no TLS in CupidOS" message

`wget`:
- `wget <url>` — auto-derives output filename from URL path
- `-O file` / `-q` (quiet)
- Reports parsed status code and bytes saved

Quoted-string args (`-H "Cookie: foo=bar"`, `-d "k1=v1&k2=v2"`)
parsed by a custom tokeniser since the shell passes args as a raw
string. Both programs share a state machine that streams the response
into stdout/file while consuming the headers byte-by-byte (4-state
`\r\n\r\n` matcher) so we never need to buffer the full body.

Live runs against the public Internet (via QEMU SLIRP):

```
/> /bin/curl.cc http://example.com/
<!doctype html><html lang="en"><head><title>Example Domain ...

/> /bin/curl.cc -d test=42 -s -X POST http://httpbin.org/post
{
  "form": { "test": "42" },
  "headers": { "Content-Type": "application/x-www-form-urlencoded",
               "User-Agent": "cupidos-curl/1.0", ... },
  "url": "http://httpbin.org/post"
}

/> /bin/wget.cc -O /out.html http://example.com/
wget: example.com -> /out.html
wget: HTTP 200, 528 bytes saved to /out.html
```

### 5.6 Verification

`bin/feature23_full_access.cc` — CupidC program that exercises the
new APIs end-to-end (network info, ARP/ICMP/UDP/PCI/blkdev). Runs
under live QEMU:

```
[feature23] ip=0x0f02000a gw=0x0202000a dns=0x0302000a mask=0x00ffffff link=1
[feature23] mac=52:54:00:12:34:56 rxp=2 txp=2
[feature23] ip_parse rc=0 val=0x08080808
[feature23] pci_device_count=6
[feature23]  pci[0..5] enumerated with vendor/device/class/irq
[feature23] blkdev_count=1
[feature23] PASS
```

`make test-net` still passes 5/5 + 13/13 wire-format on both
`rtl8139` and `e1000` after the binding expansion. Zero regressions.

---

## Phase 6 — Desktop / UX bug fixes

A pile of cross-cutting bugs in the desktop, terminal, paint, notepad,
and curl that surfaced during interactive testing. Each was a discrete
problem with a one- or two-file fix, but together they made the
desktop feel unusable. Listed roughly in order of severity.

### 6.1 CupidC JIT data region overlapped kernel BSS

**Files:** `kernel/cupidc.h`, `kernel/memory.c`, `kernel/exec.c`

**Symptom:** opening Notepad blanked the window content, deleted desktop
icons, and corrupted GUI state. Anywhere from random visual glitches
to a fully unrecoverable desktop.

**Root cause:** `CC_JIT_DATA_BASE = 0x00420000`, but kernel BSS now
extends to ~`0x00474000` (embedded ramfs assets, ksym blob, gfx2d
state). Notepad's data section (~434 KB) ran from `0x420000` past
`0x46CE40` — the address of `g2d_surf_data[]`, `windows[]`, theme
state, etc. The `memcpy(CC_JIT_DATA_BASE, cc->data, cc->data_pos)`
in `cupidc_jit_status()` zeroed out kernel state mid-paint.

**Fix:** moved `CC_JIT_CODE_BASE` 0x400000 → 0x600000,
`CC_JIT_DATA_BASE` 0x420000 → 0x620000 (past kernel `_end` and the
ASM JIT region at 0x500000-0x528000). `pmm_mark_region` updated to
match. `exec.c` raised the ELF min load address from `0x400000` →
`0x500000` so future external programs can't fall back into the BSS
hole.

### 6.2 Paint cursor frozen / fullscreen apps starved

**File:** `kernel/desktop.c`, `kernel/gfx2d.c`

**Symptom:** opening Paint locked the screen on the first painted
frame; the mouse cursor never moved.

**Root cause:** when a fullscreen app was active, both the desktop
loop and `gfx2d_flip()`'s 16 ms frame limiter waited via
`__asm__ volatile("hlt")`. `hlt` sleeps until *any* IRQ, then the
calling instruction resumes — no context switch. So:
- Desktop spun on `hlt` → IRQ wake → "fullscreen still active" check
  → `hlt` again. Forever.
- Paint hit `gfx2d_flip()` → `hlt`-wait → woke → still < 16 ms
  remaining → `hlt` again. Eventually exited and flipped frame 0.
- Frame 1: same wait, but desktop had been monopolising the
  hlt-IRQ-wake cycle, so paint never got scheduled back in.

**Fix:** replaced `hlt` with `process_yield()` in both call sites
(desktop's two fullscreen branches in `desktop_run`/`desktop_redraw_cycle`,
and `gfx2d_flip`'s wait loop). The scheduler now alternates between the
fullscreen app and the desktop's idle yield, so the app's render loop
runs at a steady ~30 fps. Verified: paint reaches frame 480+ in 15 s
with the cursor tracking the mouse.

### 6.3 USB HID keyboard PgUp / PgDn / Home / End / Insert / Delete dropped

**Files:** `kernel/usb_hid.c`, `drivers/keyboard.c`

**Symptom:** in Terminal, scrolling back through `curl` output via
PgUp / PgDn / Home / End did nothing. Same keys worked nowhere else
in the system.

**Root cause:** the `hid_to_ps2[]` translation table had entries only
for the four arrow keys (HID 0x4F-0x52). HID 0x49 (Insert), 0x4A
(Home), 0x4B (PgUp), 0x4C (Delete), 0x4D (End), 0x4E (PgDn) were all
zero → the USB HID callback's `if (hid_to_ps2[k])` short-circuit
silently dropped them.

**Fix:**
- `usb_hid.c` — added the six missing entries to `hid_to_ps2[]` plus
  a parallel `hid_is_extended[]` flag table. Press/release loops now
  inject a `0xE0` prefix before the make/break code for any extended
  key.
- `keyboard.c` — `keyboard_inject_scancode()` previously called
  `process_keypress()` directly, bypassing the extended-scancode path
  entirely. Now recognises `0xE0` and routes the next byte through
  `handle_extended_key()` (matching what a real PS/2 IRQ does).

### 6.4 USB HID mouse wheel dropped

**Files:** `kernel/usb_hid.c`, `drivers/mouse.c`, `drivers/mouse.h`

**Symptom:** wheel scrolling did nothing in Terminal (or anywhere).

**Root cause:** the USB HID mouse driver used the official 3-byte
boot protocol report `[buttons, dx, dy]`. No wheel byte. PS/2
Intellimouse detection code in `mouse.c` was wheel-aware, but USB
mice never produced a wheel value to feed it.

**Fix:**
- Bumped HID mouse report buffer from 3 → 4 bytes (Intellimouse-style:
  `[buttons, dx, dy, wheel]`). Real USB mice return 4 bytes even in
  boot protocol; pure 3-byte devices leave `r[3] = 0` (no spurious
  scroll).
- Added `mouse_inject_wheel(int8_t dz)` to `drivers/mouse.c` —
  inverts sign (HID +Z = scroll down, our convention +Z = up) and
  accumulates into `mouse.scroll_z`.

### 6.5 Notepad couldn't open big files (Psalms.DD)

**File:** `bin/notepad.cc`

**Symptom:** opening `/god/Psalms.DD` (322 KB) showed an empty editor
even though the file size was reported.

**Root cause:** `file_buf` was a 64 KB static array. `vfs_read_text`
truncated. Bumping the static array further would push notepad's
data section over `CC_MAX_DATA = 512 KB`.

**Fix:** switched to **heap allocation** — `char *file_buf = 0;`
global, `kmalloc(FILE_BUF_SIZE)` (512 KB) on first launch in `main()`,
`kfree` at exit. `load_file` now reads up to `FILE_BUF_SIZE - 1` bytes.
Verified: Psalms.DD displays from 1:1 through 5:3.

### 6.6 Notepad scrollbar / wheel snapped back to cursor

**File:** `bin/notepad.cc`

**Symptom:** clicking the scrollbar or rolling the wheel briefly
moved the view, then immediately snapped back to wherever the cursor
was.

**Root cause:** `ensure_cursor_visible()` ran in the paint block
*every frame*, not just when the user moved the cursor. So any
scroll input that didn't include a cursor move was undone before the
next frame.

**Fix:** moved `ensure_cursor_visible()` into the keyboard-handling
block, gated on `key_handled = 1`. Mouse-wheel / scrollbar drag
updates `scroll_y` and the view stays put.

### 6.7 Curl couldn't follow http→http redirects

**File:** `bin/curl.cc`

**Symptom:** `curl frankhagan.online` printed the bare 301 HTML page
instead of following the redirect or printing a useful message.

**Root cause:** curl ignored the `Location:` header.

**Fix:** added a state machine that captures the `Location:` header
during header parsing. After the response, if status is 3xx and
Location is `http://`, reconnect and reissue (cap 5 redirects, force
GET). If Location is `https://` we print a clear `curl: redirect to
https:// not supported` and exit — much better than dumping 200 bytes
of redirect-page HTML at the user.

### 6.8 Terminal: scroll wheel never reached `terminal_handle_scroll`

**File:** `kernel/desktop.c`

**Symptom:** wheel scroll worked nowhere.

**Root cause:** desktop's wheel handler `if (mouse.scroll_z != 0)`
reset `scroll_z` to 0 unconditionally when no shell-JIT program was
running. Terminal isn't a JIT program, so wheel events were
discarded before any window's app got to read them.

**Fix:** if the focused window's title is `"Terminal"`, route the
delta to `terminal_handle_scroll()` (which exists but had no
callers). For any other focused window, leave `scroll_z` intact so
the window's owning process (notepad, paint, fm, …) can consume it
through its own `mouse_scroll()` binding. The scroll_z auto-reset
only fires when no window is focused at all.

Same change applied to both desktop entry points
(`desktop_redraw_cycle` and `desktop_run`).

### 6.9 Terminal: typing reset scroll position to bottom

**File:** `kernel/terminal_app.c`

**Symptom:** after scrolling back through curl output with PgUp,
typing any character (including modifier-only events with
`character == 0`) snapped the view back to the bottom.

**Root cause:** the original "any other key resets scroll to bottom"
guard fired on every keystroke, not just on Enter.

**Fix:** only `\n` / `\r` resets the scroll. Plus removed the
redundant `WINDOW_FLAG_FOCUSED` recheck inside `terminal_handle_key`
and `terminal_handle_scroll` — the desktop dispatch loop already
verified focus, and the in-handler recheck silently swallowed events
when the FOCUSED bit was transiently cleared between dispatch and
handler.

### 6.10 Notepad opens .DD / .MD / .ASM files via fm

**File:** `bin/fm.cc`, `bin/notepad.cc`

**Symptom:** double-clicking a `.DD` (TempleOS-style data file) in
the file manager tried to `exec` it as an ELF and failed.

**Fix:** added `fm_ends_with_dd` / `_md` / `_asm` extension helpers
in `fm.cc`; routed those (plus `.txt`/`.ctxt`/`.cc` already there)
to `notepad_open_file`. Notepad's load path is unchanged — files
just appear as text.

### 6.11 Shell GUI putchar dropped backspace; `\r` rendered as ♪

**File:** `kernel/shell.c` (`shell_gui_putchar`)

**Symptom:** typing in Terminal — backspace appeared to *advance*
the cursor instead of erasing. HTTP responses streamed by curl had
a CP437 musical-note glyph (♪, byte 0x0D) at the end of every
header line.

**Root cause:** my earlier fix added `if (c < 32 && c != '\t') return;`
*before* the `\b` handler, so `\b = 0x08 < 32` was dropped before
it could move the cursor back. The shell's line editor still
re-rendered the suffix + space, which looked like the cursor walked
forward.

**Fix:** reorder so `\b`, `\n`, `\r`, `\t` are all handled
explicitly before the catch-all drop for `c < 32`. Cast to
`unsigned char` for safety.

### 6.12 e1000 driver was silently broken (RCTL/TCTL bits)

(Already covered in Phase 4.6 — listed here only because it surfaced
during the same testing pass.)

---

## Build / boot impact

| Metric | Before | After | Notes |
|---|---|---|---|
| `kernel.bin` size | ~2.0 MB | ~2.06 MB (+~30KB) | Phase 4 IP reasm BSS is 256KB (not in `.bin`); Phase 5 binding expansion ~10KB code + table |
| Symbol coverage | 0 | 2007 functions | All `t/T/w/W` symbols from final ELF |
| Boot time | ~3.0s | ~3.0s | No measurable change |
| New files | 0 | 12 (4 utils, 3 phase-3, 1 test, 3 net, 1 phase-5 sanity) | Plus 1 generated |
| Networking | builds, untested | DHCP + ARP + ICMP + UDP + TCP client + TCP server, tested on 2 NICs | `make test-net` ~30s |
| Userland API surface | ~298 CupidC bindings, 249 CupidASM, 30-fn syscall table | +53 CupidC, +50 CupidASM, +60 syscall fields (v3) | Networking + drivers + PCI + SMP + paging |
| Phase 6 fixes | n/a | 12 cross-cutting bug fixes | JIT-region collision with kernel BSS, fullscreen scheduler deadlock, USB HID PgUp/PgDn/wheel, notepad heap, terminal scrollback, curl redirects, ... |

`link.ld` `ASSERT` cap is `0x2FF600` (~3MB); we're well under.

---

## Files changed (final summary)

**Phase 1**
- `kernel/exec.c` — overflow guards (ELF + CUPD paths)
- `kernel/gfx2d_icons.c` — bounded path build
- `kernel/irq.c` — volatile + BKL wrap on install/uninstall
- `kernel/process.c` — BKL on `process_set_image`

**Phase 2A**
- `kernel/cupidc_parse.c` — FP compound + SIMD compound + non-local SIMD store

**Phase 2B**
- `bin/wc.cc`, `bin/head.cc`, `bin/tail.cc`, `bin/sort.cc` (new)
- `bin/test_fpaug.cc` (new — Phase 2A regression test)

**Phase 3**
- `tools/mksyms.sh` (new)
- `kernel/ksyms.h`, `kernel/ksyms.c` (new)
- `kernel/ksyms_data.c` (generated by mksyms; suggest `.gitignore`)
- `link.ld` — `.ksyms` section + dropped `OUTPUT_FORMAT`
- `Makefile` — two-pass link + `-fno-omit-frame-pointer` + `LDFLAGS_ELF`
- `kernel/panic.c` — `print_stack_trace` uses `ksym_backtrace`

**Phase 4**
- `kernel/tcp.c` — `tcp_emit` split, data retransmit, stop-and-wait `tcp_send`, LQ GC
- `kernel/socket.h` — `rt_buf`/`rt_seq`/`rt_send_tick`/`rt_attempts`/`lq_inserted_ms`
- `kernel/arp.c`, `kernel/arp.h` — per-entry TTL + `arp_tick()`
- `kernel/ip.c` — full rewrite for fragmentation/reassembly
- `kernel/net_if.c` — call `arp_tick()` from `net_process_pending`
- `kernel/socket.c` — `ntohs`/`htons` at API boundaries (BSD convention)
- `kernel/dns.c` — `htons(53)` at the one socket-API caller
- `kernel/e1000.c` — fixed RCTL (missing EN bit) + TCTL (missing PSP bit)
- `tools/net_test.py` (new) — pexpect harness, headless QEMU, host curl
- `tools/net_pcap.py` (new) — scapy wire-format verifier
- `Makefile` — `test-net`, `test-net-quick`, `headless-net-image` targets

**Phase 5**
- `kernel/cupidc.c` — 53 new BIND entries + index-based wrappers (net info, ARP/ICMP/UDP, blkdev, PCI, LAPIC, BKL, paging, PMM, kbd, serial, PIT) + new headers
- `kernel/as.c` — 50+ AS_BIND entries + 60 new SYS_* equ constants for AOT + IP_PROTO_* / SOCK_TYPE_* equs + new headers + index-based wrappers
- `kernel/syscall.h` — version 2→3, append-only struct grew from 30 to 90 fields
- `kernel/syscall.c` — populate new fields, _Static_assert key offsets, new headers
- `bin/feature23_full_access.cc` (new) — sanity test exercising new bindings
- `bin/curl.cc` (new) — HTTP/1.0 client (GET/POST, -o, -i, -s, -X, -d, -H)
- `bin/wget.cc` (new) — HTTP/1.0 downloader (auto-named or -O, -q, status report)

**Phase 6**
- `kernel/cupidc.h` — `CC_JIT_*_BASE` 0x400000/0x420000 → 0x600000/0x620000
- `kernel/memory.c` — `pmm_mark_region` for new CupidC JIT range
- `kernel/exec.c` — raised ELF min load 0x400000 → 0x500000
- `kernel/desktop.c` — fullscreen `hlt` → `process_yield`; wheel routing to `terminal_handle_scroll`
- `kernel/gfx2d.c` — `gfx2d_flip` 16 ms wait `hlt` → `process_yield`
- `kernel/usb_hid.c` — added Insert/Delete/Home/End/PgUp/PgDn HID→PS/2 entries + `hid_is_extended[]` 0xE0 prefix; bumped HID mouse report to 4 bytes (Intellimouse) and feeds wheel via `mouse_inject_wheel`
- `drivers/keyboard.c` — `keyboard_inject_scancode` recognises `0xE0` prefix
- `drivers/mouse.c` / `mouse.h` — new `mouse_inject_wheel` API
- `kernel/terminal_app.c` — wheel routing exposed; `Home`/`End` scancodes; scroll-cap from `shell_get_cursor_y` to `SHELL_ROWS`; only `\n`/`\r` resets scroll; removed redundant FOCUSED recheck
- `kernel/shell.c` — reordered `shell_gui_putchar` so `\b`/`\n`/`\r`/`\t` are handled before the catch-all `c < 32` drop
- `bin/notepad.cc` — `file_buf` static array → `kmalloc(512KB)`; `ensure_cursor_visible` only after key handling
- `bin/fm.cc` — added `.dd` / `.md` / `.asm` extensions to notepad routing

**Total:** 36 modified, 13 created, 1 generated.

---

## Suggested commit messages (Conventional Commits style)

```
fix(exec): guard ELF/CUPD loaders against integer overflow

p_vaddr + p_memsz could wrap u32 and bypass the IDENTITY_MAP_SIZE
range check, letting a crafted ELF write into kernel space. Bound
each field individually before the addition. Same class of bug fixed
in the CUPD flat-binary loader.
```

```
fix(gfx2d_icons): bound path build in /bin scanner

Replace strcpy+strcat with an inline bounded copy. Not exploitable
today (vfs_readdir backends all enforce VFS_MAX_NAME), but the pattern
is brittle if future backends differ.
```

```
fix(irq): protect handler chain from race during install

Wrap irq_install_handler / irq_uninstall_handler in BKL when
initialized (no-op fallback during very-early boot, before BKL init
and before IOAPIC unmask). Mark irq_handlers[][] volatile so the
dispatcher always re-reads each slot.
```

```
fix(process): lock process_set_image to avoid torn image_base/size

Take BKL around the pair assignment; publish image_size = 0 first so
any reader that sees the new base before the new size observes an
empty region instead of a stale pair.
```

```
feat(cupidc): support FP and SIMD compound assignment

Add codegen for +=, -=, *=, /= on float / double scalars and on
float4 / double2 vectors. Also support plain '=' on global SIMD
destinations (was previously local-only). Adds bin/test_fpaug.cc as
regression coverage.
```

```
feat(bin): add wc, head, tail, sort utilities

Standard Unix tools, written in CupidC. Auto-embedded via the
existing bin/*.cc Makefile pipeline.
```

```
feat(panic): decode kernel backtrace addresses to function names

Two-pass kernel link: pass 1 produces an ELF that tools/mksyms.sh
parses with nm, then pass 2 links the populated symbol blob into a
.ksyms section between .data and .bss (so code addresses don't shift
between passes). Adds ksym_lookup / ksym_backtrace; rewires
panic.c print_stack_trace to use them. Adds -fno-omit-frame-pointer
to CFLAGS so the EBP chain is walkable. Graceful fallback to raw
addresses if the blob is missing or corrupt.
```

```
fix(net): correct htons double-swap, e1000 RCTL/TCTL, and listen-queue leak

Five issues that together prevented end-to-end networking:
- socket layer was double-swapping ports (user passed htons(80), wrapper
  byte-swapped again → port 0x5000 on wire). Apply ntohs at API entry
  and htons at API exit, matching BSD semantics. dns.c updated.
- e1000 RCTL literal omitted the EN bit (RX never enabled, DHCP fell
  back to static). TCTL omitted PSP (short ARP frames silently dropped).
  Set RCTL=0x04008002, TCTL=0x0004010A.
- TCP only retransmitted SYN; lost data segments timed out the receiver.
  Added stop-and-wait per-socket retransmit slot with exponential
  backoff (tcp_emit/tcp_send_seg split, rt_buf in socket_t).
- ARP cache had no TTL; entries lived until LRU pressure. Added 5-min
  per-entry TTL via arp_tick() called from net_process_pending.
- Listen-queue half-open entries were never freed; 8 stuck SYN-RCVD
  permanently DoS'd the listener. Added lq_inserted_ms + 30s GC in
  tcp_tick.
```

```
feat(net): IP fragmentation and reassembly

Send side: ipv4_send now splits payloads >MTU into 8-byte-aligned
fragments sharing one IP id, MF=1 on all but last. Receive side: 4-slot
reassembly table keyed by (src,dst,id,proto) with a 64KB buffer and
8192-bit completion bitmap each, 30s expiry. Drops the previous
KWARN("dropping fragmented packet") early-return.
```

```
test(net): headless QEMU integration suite for rtl8139 + e1000

tools/net_test.py spawns a -display none -serial stdio QEMU with a
filter-dump pcap, drives the shell over COM1 (existing keyboard.c
serial-RX path), runs ifconfig/ping/arp/feature21/feature22, and
host-curls the forwarded port to verify the in-guest TCP server.
tools/net_pcap.py uses scapy to re-validate the captured frames at
wire level (ARP pairing, full DHCP exchange, ICMP echo, TCP handshake,
IP checksums). New Makefile targets: test-net, test-net-quick.
```

```
feat(api): expose full networking + driver surface to CupidC, CupidASM,
           and ELF programs

Three previously-divergent runtime surfaces now reach feature parity
covering ~110 new entry points:

  - cupidc.c: +53 BIND entries (net info, ARP/ICMP/UDP raw, blkdev,
    PCI, LAPIC, BKL, paging, PMM, kbd direct, serial, PIT) with
    index-based wrappers for opaque kernel pointers (pci_device_t etc).

  - as.c: +50 AS_BIND entries plus the full BSD socket family,
    outb/inb for driver authors, and IP_PROTO_*/SOCK_TYPE_* as
    compile-time equ constants for AOT.

  - syscall.h: CUPID_SYSCALL_VERSION 2 → 3. cupid_syscall_table_t
    grows append-only from 30 to 90 fields, with _Static_assert
    guards on key offsets so a future field reorder fails to compile
    instead of silently shipping a layout mismatch.

  - as.c: +60 SYS_* equ constants matching the new struct offsets so
    AOT .asm programs can call kernel APIs via [ebx + SYS_NET_GET_IP]
    style indirection.

bin/feature23_full_access.cc exercises the new APIs end-to-end and
prints PASS on live QEMU. make test-net regressions remain 5/5.
```

```
fix(memory): move CupidC JIT region above kernel BSS

CC_JIT_DATA_BASE was 0x420000; kernel BSS now extends to ~0x474000
(embedded ramfs, ksym blob, gfx2d state).  Any CupidC program with
> ~310 KB data (notepad ≈ 434 KB) silently corrupted kernel state on
its memcpy-to-CC_JIT_DATA_BASE — Notepad blanked windows and wiped
desktop icons.  Move CC_JIT_*_BASE 0x400000/0x420000 →
0x600000/0x620000 (past _kernel_end and the ASM JIT region).  Update
pmm_mark_region; raise exec.c ELF min load to 0x500000 to keep
external programs out of the BSS hole.
```

```
fix(scheduler): yield (not hlt) when fullscreen app owns the screen

Both desktop_run's fullscreen branch and gfx2d_flip's 16ms frame
limiter waited via __asm__ volatile("hlt").  hlt sleeps until any
IRQ then resumes the same instruction — no context switch.  Result:
desktop spun on hlt-IRQ-wake forever; the fullscreen app
(paint/etc.) only got CPU until it hit its first flip wait, then
starved.  Replace both hlt sites with process_yield() so the
scheduler alternates the desktop and the app at PIT tick rate.
Verified: paint reaches frame 480+ in 15 s with smooth cursor
tracking.
```

```
fix(input): wire USB HID PgUp/PgDn/Home/End/Insert/Delete and wheel

hid_to_ps2[] only had entries for the four arrow keys; everything
else dropped before reaching the PS/2-style keyboard buffer.  Added
HID 0x49–0x4E translations plus a hid_is_extended[] flag table that
prepends 0xE0 on press AND release.  keyboard_inject_scancode() now
recognises 0xE0 and routes the next byte through handle_extended_key
(matching the real PS/2 IRQ path).

Same code path for wheel: the HID mouse driver was using the
official 3-byte boot report so wheel always read as 0.  Bumped to
4-byte (Intellimouse-style) reports and added mouse_inject_wheel()
that accumulates dz into mouse.scroll_z.
```

```
fix(terminal): scrollback finally works (PgUp/PgDn/Home/End/wheel)

- desktop wheel handler routes to terminal_handle_scroll() when
  Terminal is focused (was unconditionally resetting scroll_z to 0
  unless a JIT program was running, which terminal isn't).
- terminal_handle_key + terminal_handle_scroll dropped the
  redundant WINDOW_FLAG_FOCUSED recheck — caller already verified
  focus, and the inner check silently swallowed events when the
  flag was transiently cleared between dispatch and handler.
- Scroll cap raised from shell_get_cursor_y() (often 0) to
  SHELL_ROWS so PgUp scrolls back through the full 500-row buffer.
- Only Enter/Return resets the scroll back to bottom; arbitrary
  typing keeps the user's scrolled-back view.
- Added Home (top) / End (bottom) bindings.
```

```
fix(notepad): heap-alloc file_buf so Psalms.DD opens

file_buf was a 64 KB static array.  Bumping past 64 KB pushed
notepad's data section over CC_MAX_DATA = 512 KB.  Switch to
kmalloc(512 KB) on first launch in main(); kfree at exit.  load_file
reads up to FILE_BUF_SIZE - 1 bytes.  Verified: /god/Psalms.DD
(322 KB) opens and displays from 1:1 through 5:3.

Also: ensure_cursor_visible() was running every paint frame, snapping
scroll_y back to the cursor after every wheel/scrollbar input — moved
inside the keyboard-handling block so it only fires after a key press.
```

```
fix(curl): follow http→http redirects, exit cleanly on http→https

curl printed the bare 301 HTML when the server redirected.  Added a
state machine that captures the Location: header during header
parsing.  3xx with http:// Location → reconnect, force GET, retry
(cap 5 hops).  https:// Location → print "curl: redirect to
https:// not supported (URL)" and exit, instead of dumping 200
bytes of redirect-page HTML.
```

```
fix(misc): backspace in Terminal, music-note glyph in curl output

shell_gui_putchar's c < 32 catch-all was placed BEFORE the \b
handler, so backspace (0x08 < 32) was dropped before it could move
the screen cursor back; the line editor still rendered the
remaining suffix + space, which made the cursor look like it was
walking forward.  Reorder so \b/\n/\r/\t are handled explicitly
before the drop, fixing both backspace and \r-as-♪ (CP437 byte
0x0D) in HTTP responses.

Plus fm.cc routes .DD/.MD/.ASM extensions to notepad — TempleOS
data files now open by double-click in My Computer.
```

---

## Things to call out for reviewers

1. **`kernel/ksyms_data.c` is generated** — should probably be in
   `.gitignore`. Or check in for reproducible builds; both are
   defensible. Currently not in `.gitignore`.

2. **Two-pass link doubles the kernel link time.** On this codebase
   that's still under a second. If it ever becomes a concern, the
   pass-1 link could skip RamFS object embedding (~80 small `.o`
   files) since their addresses don't appear in symbols. Not urgent.

3. **`OUTPUT_FORMAT("binary")` removed from `link.ld`.** Build now
   relies on `--oformat binary` being on the `LDFLAGS` command line
   for the final `kernel.bin` step. If you grep for `link.ld` users
   outside of the Makefile, double-check none rely on the old
   default.

4. **Bootloader load size unchanged** — still 4091 sectors / ~2MB. The
   new kernel binary fits comfortably (~2.03 MB → 2.04 MB). The
   `link.ld` `ASSERT(_loaded_end <= 0x2FF600)` will catch overflow if
   future symbol growth pushes us over.

5. **Frame pointers** add tiny overhead. If you ever want them off
   per-file for hot loops, drop `-fomit-frame-pointer` selectively
   into the rule for that file. For now, global on.

6. **Deferred Phase 2A items** (mixed int/FP args, plain int-array
   brace init) are documented in the plan and in the existing comments
   at `cupidc_parse.c:1799` and `:4291`. Both have working workarounds.

7. **Phase 4 IP reassembly table is 256 KB BSS** (4 slots × 65 535-byte
   buffer, plus bitmaps). Kernel heap is 32 MB so this is comfortable,
   but it does grow `.bss`. If memory pressure ever matters, drop slot
   count to 2 or buffer to 16 KB; the reassembly path is the only piece
   that keeps the full 64 KB worst-case datagram in memory.

8. **`tools/net_test.py` requires `python3-pexpect` and the `scapy`
   pcap reader** (used by `tools/net_pcap.py`). Install with
   `pip install --user pexpect scapy`. Both are pure-Python and don't
   need root. Without scapy the test harness still runs; only the
   wire-level re-validation step is skipped.

9. **`make test-net` invokes `headless-image`**, which calls
   `make clean` to switch to a `-DHEADLESS` build. After running tests,
   re-run plain `make` to get a desktop kernel back.

10. **QEMU SLIRP ICMP** depends on the host's
    `net.ipv4.ping_group_range` permitting unprivileged pings. If
    `ping 8.8.8.8` from inside CupidOS reports zero replies but TCP
    works, that's the cause — `sysctl -w
    net.ipv4.ping_group_range="0 2147483647"` on the host fixes it.

---

## Phase 7 — Browser source split (Plan 1 of WebKit-lite redesign)

Foundation for the browser redesign spec
(`docs/superpowers/specs/2026-05-05-browser-redesign.md`). The current
`bin/browser.cc` was a single ~2070-line file that the redesign needs
to split before touching pipeline structure. CupidC's preprocessor
already supports `#include "rel/path"` resolved relative to the
including file (in `kernel/cupidc.c`); only the build system needed
extension. Plan source-of-truth:
`docs/superpowers/plans/2026-05-05-browser-build-split.md`.

**Invariant:** zero behavior change. Every commit verified against a
pre-split visual reference of `http://example.com/` rendered in QEMU.

### 7.1 Build system extension

**Files:** `Makefile` (~24 lines added)

- New wildcard `BROWSER_SUB_SRCS := $(wildcard bin/browser/*.cc)` plus
  derived `BROWSER_SUB_OBJS` / `BROWSER_SUB_NAMES`.
- Pattern rule `bin/browser/%.o: bin/browser/%.cc` — same `objcopy
  -I binary -O elf32-i386 -B i386` recipe as the existing `bin/%.o`
  rule, embedding each sub-file as `_binary_bin_browser_<n>_cc_*`
  symbols.
- `kernel/bin_programs_gen.c` generator extended to iterate
  `BROWSER_SUB_NAMES`: emits the `extern` decls before
  `install_bin_programs`, then `ramfs_add_file(fs_private,
  "bin/browser/<n>.cc", _binary_bin_browser_<n>_cc_start, sz)` calls
  inside the body. Output line per file:
  `[kernel] Installed /bin/browser/<n>.cc (NNNN bytes)`.
- Sub-files NOT added to `BIN_CC_NAMES` — they are CupidC `#include`
  targets, not runnable programs. Convention reserved: `bin/browser/`
  is the browser library namespace.
- Generator dependency line picks up `BROWSER_SUB_SRCS` so changes to
  any sub-file re-run the embed step.
- `clean` rule appended `bin/browser/*.o` to its rm wildcard.

At boot, ramfs has `/bin/browser.cc` (runnable, 19 lines — pure
trampoline) plus `/bin/browser/*.cc` (library). When the user runs
`browser`, JIT preprocesses the trampoline, follows each `#include
"browser/*.cc"` via the existing path resolver (relative to `/bin/`),
and compiles the concatenated TU.

### 7.2 File extraction (10 commits, verbatim moves)

Each commit moves one cohesive section out of `bin/browser.cc` into
`bin/browser/<name>.cc`, preserving call order in include directives.
CupidC has no forward declarations across `#include` boundaries; the
preprocessor concatenates expanded source into one buffer for a
single-pass compiler, so order matters bottom-up.

| Commit | New file | LOC | Contents |
|---|---|---|---|
| `fc7e5b5` | `bin/browser/_smoke.cc` | ~5 | `browser_smoke_token()` stub proving `#include` + ramfs-embed pipeline (deleted at end of plan in `8a1afdc`) |
| `53864fc` | `bin/browser/util.cc` | 85 | `b_strlen`, `b_streq`, `b_lc`, `b_strieq`, `b_streq_n`, `b_strieq_n`, `b_strcpy_n`, `b_strchr`, `b_append`, `b_append_n`, `b_append_int` |
| `c2ee640` | `bin/browser/url.cc` | 73 | `parse_url`, `resolve_redirect` |
| `69701fe` | (relocate) | — | `hex_digit` moved url.cc → util.cc (review-driven; consumers all live in dom.cc) |
| `b2a155c` | `bin/browser/net.cc` | 176 | `build_request`, `fetch_url` (HTTP/HTTPS + redirects). References to globals (`cur_host`, `page_buf`, …) resolve at trampoline-link time |
| `bb9c3ea` | `bin/browser/dom.cc` | 199 | `parse_color_named`, `parse_color`, `apply_style`, `alloc_node`, `attr_intern`, `decode_entities` |
| `23651e2` | `bin/browser/parser.cc` | 311 | `tag_id`, `is_void_tag`, `skip_to_close`, `parse_html` |
| `7480ea9` | (cleanup) | — | drop stranded `/* DOM helpers in dom.cc */` marker from parser.cc |
| `983dcc1` | `bin/browser/layout.cc` | 302 | `viewport_w`, `parent_color/bg/bold/link`, `L_*` state, `emit_box`, `last_box`, `register_link`, `newline`, `layout_text`, `layout_children`, `layout_node`, `run_layout` |
| `75d48dc` | `bin/browser/paint.cc` | 220 | `viewport_x/y/h`, `draw_text/input/button/image_box`, `draw_address_bar`, `draw_status_bar`, `draw_scrollbar`, `render`, `error_page` |
| `8820f2d` | `bin/browser/nav.cc` | 172 | `compute_url_relative`, `navigate`, `go_back`, `submit_form` |
| `83e2598` | `bin/browser/input.cc` | 246 | `hit_box`, `find_node_for_input`, `find_form_node`, `clamp_scroll`, `handle_address_key`, `handle_input_key`, `handle_page_key`, `handle_keys`, `handle_left_click`, `handle_hover`, `handle_mouse` |
| `8a1afdc` | `bin/browser/main.cc` | 266 | All globals (`cur_url`, `page_buf`, `n_*`/`b_*` arrays, focus state, history ring), all `enum` constants (`T_*`, `BK_*`, `FOCUS_*`, sizing caps), `browser_main()` (renamed from `main()`); deletes `_smoke.cc` |
| `5a79371` | (cleanup) | — | drop stale `/* moved to X */` markers left behind in main.cc |

The trampoline `bin/browser.cc` ends at 19 lines:

```c
//help: Web browser. Renders HTTP and HTTPS pages in a window.
//help: Usage: browser [url]
//help:   Address bar: Ctrl-L to focus, Enter to go.
//help:   Backspace (page focus): back history.
//help:   Arrow keys / mouse wheel: scroll.
//help:   Click link to navigate.

#include "browser/main.cc"
#include "browser/util.cc"
#include "browser/url.cc"
#include "browser/net.cc"
#include "browser/dom.cc"
#include "browser/parser.cc"
#include "browser/layout.cc"
#include "browser/paint.cc"
#include "browser/nav.cc"
#include "browser/input.cc"

void main() { browser_main(); }
```

`main.cc` is included FIRST so its globals/enums are visible to all
subsequent includes. CupidC accepts forward references to functions
within the concatenated TU (the existing single-file browser already
relied on this between `main()` and earlier-defined helpers), so
`browser_main()`'s body — which calls into every later include — works.

### Phase 7 verification

- Build log per commit shows `[kernel] Installed /bin/browser/<n>.cc
  (NNNN bytes)` lines for every sub-file.
- Manual visual smoke after every commit: `make run-net`, in shell
  `browser http://example.com/`, compare against pre-split reference.
  Expected: title "Example Domain", one underlined link, identical
  layout.
- Final acceptance covers HTTP, HTTPS, multi-page nav (Backspace
  history), and form submit (GET only). No regressions vs pre-split.

---

## Phase 8 — Networking, TLS, and filesystem hardening

Discovered while exercising the freshly-split browser against real
sites. Each fix has a specific failure-mode reproduction; none are
speculative cleanup. Listed in order of root-cause depth.

### 8.1 RTL8139 receiver: WRAP=1 in RCR

**File:** `kernel/rtl8139.c`

**Problem:** the driver's `rtl_rx_drain` reads each received packet
linearly from `p+4` for `pkt_len` bytes, relying on the chip's
1500-byte buffer extension after offset 8192 (`RTL_RX_BUF_SIZE = 8192
+ 16 + 1500`) to make boundary-straddling packets contiguous in
memory. That extension is only used when **WRAP=1** in RCR.

RCR was being programmed as `0x0F` (`AB | AM | APM | AAP`, WRAP=0).
With WRAP=0 the chip wraps any packet that doesn't fit before offset
8192 back to offset 0 in the visible 8192-byte ring; only the first
part of such a packet appears at `p+4`. The driver's linear read then
walks past offset 8192 into the zero-initialised extension region,
silently producing frames whose tails are zero-padded.

Invisible for short connections that never wrapped and for short
packets that fit before the boundary. Symptom: TCP streams larger
than the 8K chip ring consistently delivered some zero-padded
segments. AEAD-protected payloads (TLS records) decrypt-failed on the
silently-corrupted bytes; plain HTTP browsers got garbage in long
responses.

Concrete impact: TLS handshakes against servers with deep RSA-4096
cert chains (e.g. `iana.org`, ~10 KB Certificate handshake message)
AEAD-failed on the Certificate record because the source ciphertext
bytes for one segment were partially zeros. Decoding the all-zero
Poly1305 tag was the giveaway in the trace.

**Fix:** RCR `0x0F` → `0x8F` (WRAP | AB | AM | APM | AAP).

### 8.2 TCP receive buffer + dynamic advertised window

**Files:** `kernel/socket.h`, `kernel/tcp.c`

**Problem:** two issues with the same root cause — peer-side data
piling up faster than we drained, segments dropped, peer eventually
closing the connection mid-handshake (surfaced as
`TLS_ERR_TRANSPORT = -1`):

1. `SOCK_RX_BUF` was 8 KB. Servers with deep RSA-4096 cert chains
   send a Certificate handshake message ~10 KB; after
   ChaCha20-Poly1305 framing it lands as one or two TLS records and
   easily exceeds 8 KB of TCP-buffered data. The buffer couldn't
   hold a single record's TCP segments long enough for the TLS layer
   to drain.
2. Advertised window was hard-coded `8192`. With a fixed window the
   peer always thought it had 8 KB of room even when our buffer was
   full — leading to drops + retransmit storms.

**Fix:**
- Bump `SOCK_RX_BUF` 8 KB → 64 KB. Cost: 32 sockets × 56 KB extra =
  1.75 MB. Plenty of room in our 16 MB CupidC data base.
- Compute actual free rx-buffer space at every send and put that in
  the TCP header, capped at `0xFFFF` (no window scaling).

### 8.3 TLS handshake reader carry buffer

**File:** `kernel/tls/tls_handshake.c`

**Problem:** the handshake-message reassembler held only 4 KB of
in-flight bytes between TLS-record arrivals. A single TLS record's
plaintext can be up to 16 KB (RFC 8446 §5.1), and large handshake
messages — notably `Certificate` when the server's chain contains
4096-bit RSA certs — land in records of 6-7 KB. `hs_reader_fill`
rejected those with `TLS_ERR_PROTOCOL`.

**Fix:** size the carry buffer to `TLS_REC_MAX_PLAINTEXT` (16 KB).
`hs_reader` is a stack local in `tls_handshake_client`; +12 KB on a
1 MB user stack is fine.

### 8.4 TLS app-data plaintext spillover buffer

**Files:** `kernel/tls/tls_ctx.h`, `kernel/tls/tls_handshake.c`

**Problem:** a single TLS 1.3 record's plaintext can be up to 16 KB
but callers typically pass a small recv buffer (curl/browser use
4 KB). The old `tls_app_recv` handed the caller's buffer straight
to `tls_record_recv`, which rejected oversized records with
`TLS_ERR_PARSE` (returned to caller as `-2`) — losing the response
body for any single-record HTTP response > ~4 KB.

**Fix:** add `app_buf[TLS_REC_MAX_PLAINTEXT]` + `app_off` / `app_len`
to `tls_ctx_t`. `tls_app_recv` decrypts a whole record into
`app_buf`, returns up to `buf_max` bytes to the caller, and drains
leftovers across subsequent recv calls.

Verified: `www.iana.org`'s 6142-byte HTML body now flows through curl
and browser without truncation.

### 8.5 RSA-PSS verifier: drop strict DB top-bit check

**File:** `kernel/tls/rsa.c`

**Problem:** RFC 3447 §9.1.2 step 6 says "Set the leftmost
(8·emLen − emBits) bits of the leftmost octet in DB to zero." That's
a directive to **mask** those bits, not check them.

The signer (per §9.1.1 step 11) only zeroes the top bits of
*maskedDB*, not DB. After the verifier MGF1-unmasks (`db = maskedDB ^
mask`), DB's top bits equal `mask`'s top bits, which are essentially
uniform-random SHA-256 output. The verifier is supposed to mask them
off and proceed.

The previous code added a strict pre-mask check:
```c
if ((db[0] & ~topmask) != 0u) return 0;
```
For RSA-4096 (`modbits=4096, emBits=4095, top_zero_bits=1`), this
rejected any signature where MGF1's high bit happened to be 1 —
i.e., ~50% of all valid signatures. For RSA-3072 (`top_zero_bits=0`)
the bit-clearing branch was skipped entirely, so the check never
fired. That's why example.com (ECDSA) and most Cloudflare-fronted
sites worked, but iana.org's RSA-4096 leaf cert produced a flaky
~60% verify success rate.

Symptom: TLS handshake with iana.org fails with
`TLS_ERR_BAD_SIGNATURE = -8` on roughly half of all attempts.
Reproduced offline by computing `sig^e mod n` with our exact byte
inputs — Python's PSS verify accepts both successful and "failing"
cases identically; only our extra check diverged.

**Fix:** drop the check, just mask the bits per RFC.

### 8.6 HomeFS flush re-entrance — leaked clusters → stack overflow

**File:** `kernel/homefs.c`

**Problem:** `fat16_write_file` calls `blockcache_sync()` internally,
which calls `homefs_sync()`, which calls back into `homefs_flush()`.
Because `dirty` was cleared **after** `fat16_write_file` returned, the
inner call saw `dirty=true` and re-entered the flush, allocating a
fresh FAT cluster each time. Each iteration leaked one cluster; the
unbounded recursion eventually overflowed the kernel stack and
rebooted the machine.

Symptom (e.g. `curl -o /home/X.html https://...`): the boot log shows
many `[fat16_write_file] HOMEFS.SYS` allocations of fresh sequential
clusters (3927, 3928, 3929, …) followed by an immediate kernel reboot.

**Fix:** clear `fs->dirty` up front. If the write fails, restore it so
data isn't silently lost. Inner re-entrant calls now see `dirty=false`
and bail with `VFS_OK` without recursing.

### Phase 8 verification

- HTTP path: `browser http://example.com/` renders unchanged.
- HTTPS path on small response: `browser https://example.com/`
  completes (was already passing).
- HTTPS path on RSA-4096 chain + large body:
  `browser https://www.iana.org/` now completes the handshake
  reliably (was ~60%) and renders the 6142-byte body fully (was
  truncated at ~4 KB).
- Disk write path: `curl -o /home/test.html http://example.com/` no
  longer crashes the kernel.

### Phase 7 + 8 file summary

**Browser split (Plan 1):**
- `bin/browser.cc` — reduced from ~2075 lines to 19 (pure trampoline)
- `bin/browser/util.cc`, `url.cc`, `net.cc`, `dom.cc`, `parser.cc`,
  `layout.cc`, `paint.cc`, `nav.cc`, `input.cc`, `main.cc` — new,
  total ~2050 lines (verbatim moves + banner comments)
- `Makefile` — wildcard discovery, pattern rule, generator extension,
  clean rule (~24 lines)
- `docs/superpowers/plans/2026-05-05-browser-build-split.md` — minor
  edits during execution

**Networking / TLS / filesystem (Phase 8):**
- `kernel/rtl8139.c` — RCR WRAP bit
- `kernel/tcp.c`, `kernel/socket.h` — rx buffer + dynamic window
- `kernel/tls/tls_handshake.c` — hs_reader carry → 16 KB
- `kernel/tls/tls_ctx.h`, `kernel/tls/tls_handshake.c` — app-data
  spillover buffer
- `kernel/tls/rsa.c` — drop non-RFC PSS strict check
- `kernel/homefs.c` — clear dirty before flush

**Total commits on branch:** 22 (Plan 1: 13 incl. cleanups; Phase 8: 6
fixes + 1 reverted debug commit + 1 revert + 1 placeholder).

### Suggested commit-message style for downstream PRs

The existing commits already follow this — listed here for reference
when squashing or backporting:

```
browser: extract <area> to bin/browser/<file>.cc

Moves <listed functions> verbatim. No behavior change.
```

```
<subsystem>: <one-line root cause and fix>

<why-it-mattered paragraph: failure mode, reproduction site>

<fix paragraph: what changed, why this is the right scope>
```

### Things to call out for reviewers (Phases 7-8)

1. **CupidC forward-reference behaviour** is the load-bearing
   assumption of the trampoline ordering. `main.cc` (containing
   globals) is included first; later includes reference those globals.
   This works because the JIT compiles one concatenated TU. If a
   future CupidC change tightens forward-reference rules, the fallback
   already documented in the plan (split `globals.cc` first, `main.cc`
   last) applies.

2. **`bin/browser/` is now a reserved path** in the ramfs convention.
   Future runnable programs go in `bin/<name>.cc` (flat); browser
   library files go in `bin/browser/<name>.cc`. Documented in spec
   §12.

3. **TLS reliability on large chains**: the 8.1+8.2+8.3+8.4+8.5 stack
   moves us from "TLS works on Cloudflare, flaky on RSA-4096
   originals" to "TLS works reliably on the public Internet sites we
   test against." It does not yet implement window scaling, congestion
   control, or session resumption — out of scope for this PR.

4. **`SOCK_RX_BUF` 8 KB → 64 KB** grows static BSS by 1.75 MB across
   32 socket slots. Kernel data base is 16 MB; comfortable. If memory
   pressure ever shows up, the right move is dynamic per-socket
   buffers (each scaled to negotiated window), not reverting this
   bump.

5. **HomeFS re-entrance fix** is conservative — restores the dirty
   flag on write failure so data isn't silently lost. The deeper
   architectural fix (decouple `blockcache_sync` from VFS callbacks)
   is out of scope; flag-based gate is sufficient for current
   call-sites.
