# Networking Tier 4

CupidOS P6 Networking Tier 4 adds a full TCP/IP stack to the kernel. The
implementation includes two NIC drivers (RTL8139 and Intel E1000), a complete
protocol suite (ARP, IPv4, ICMP, UDP, TCP), DHCP with static fallback, a DNS
A-record resolver, and a BSD-style socket API exposed to both shell commands
and CupidC programs.

Related pages: [USB](USB), [SMP](SMP)

---

## Overview

| Property | Value |
|---|---|
| NIC drivers | RTL8139 (Realtek RTL8139), E1000 (Intel 82540EM) |
| Protocols | Ethernet, ARP, IPv4, ICMP echo reply, UDP, TCP |
| TCP model | RFC 793 subset, 10 states, fixed 500 ms RTO, MSS 1460 |
| DHCP | DISCOVER/OFFER/REQUEST/ACK + static fallback 10.0.2.15/24 |
| DNS | UDP/53 A-record resolver, 16-entry TTL cache |
| Socket API | BSD-style, 32-slot dedicated table |
| RX model | NIC IRQ top-half → 64-slot lockless ring → idle bottom-half |
| New source files | 22 (11 kernel + 2 bin + 2 doc + headers) |

New kernel files:

```
kernel/net_if.h / net_if.c     NIC vtable, RX ring, registration, net_init
kernel/arp.h    / arp.c        16-entry LRU ARP cache, blocking resolve
kernel/ip.h     / ip.c         IPv4 parse, route, send, protocol dispatch
kernel/icmp.h   / icmp.c       ICMP echo reply
kernel/udp.h    / udp.c        UDP send/recv, pseudo-header checksum
kernel/tcp.h    / tcp.c        RFC 793 subset state machine (~1200 LOC)
kernel/socket.h / socket.c     32-slot BSD socket table + API
kernel/dhcp.h   / dhcp.c       DHCP four-way handshake + static fallback
kernel/dns.h    / dns.c        UDP/53 A-record resolver + 16-entry cache
kernel/rtl8139.h / rtl8139.c   Realtek 8139 PCI NIC driver (~300 LOC)
kernel/e1000.h   / e1000.c     Intel 82540EM PCI NIC driver (~500 LOC)
bin/feature21_net.cc           TCP client smoke test (DNS + connect + HTTP GET)
bin/feature22_net_server.cc    TCP server smoke test (listen/accept/echo)
```

---

## Architecture

### Layer diagram

```
┌─────── user / kernel code ────────────────────────────────────┐
│  socket()  bind()  listen()  accept()                         │
│  connect() send()  recv()    close()                          │
│  dns_resolve(name) → ipv4                                     │
└──────────┬────────────────────────────────────────────────────┘
┌──────────▼── kernel/socket.c — 32-slot table ─────────────────┐
│  socket_t { type, state, tx_buf/rx_buf, TCP state machine }   │
└──────────┬────────────────────────────────────────────────────┘
┌──────────▼── kernel/{tcp,udp,icmp}.c ─────────────────────────┐
│  TCP state machine      UDP datagram       ICMP echo reply    │
└──────────┬────────────────────────────────────────────────────┘
┌──────────▼── kernel/ip.c — IPv4 send + dispatch ──────────────┐
│  ipv4_send(dst, proto, buf, len) → arp resolve → NIC send     │
│  ipv4_input(frame) → proto dispatch (ICMP/UDP/TCP)            │
└──────────┬────────────────────────────────────────────────────┘
┌──────────▼── kernel/arp.c — 16-entry LRU cache ───────────────┐
│  who-has / is-at    blocking resolve on cache miss (500 ms)   │
└──────────┬────────────────────────────────────────────────────┘
┌──────────▼── kernel/net_if.c — unified NIC interface ─────────┐
│  net_if_t vtable    lockless SPSC RX ring (64 slots)          │
└──────────┬────────────────────────────────────────────────────┘
           ▼
┌── kernel/rtl8139.c ──────── kernel/e1000.c ───────────────────┐
│  PCI probe + init + register      IRQ top-half (enqueue frame) │
└────────────────────────────────────────────────────────────────┘
```

### Key size constants

| Constant | Value | Purpose |
|---|---|---|
| `SOCKET_MAX` | 32 | total socket table slots |
| `NET_RX_RING_SIZE` | 64 | lockless SPSC RX ring slots |
| `NET_IF_MTU` | 1500 | max IP payload bytes |
| `SOCK_RX_BUF` | 8192 | per-socket receive ring buffer |
| `SOCK_TX_BUF` | 8192 | per-socket transmit ring buffer |
| `LQ_SIZE` | 8 | listen queue slots per LISTEN socket |
| `TCP_MSS` | 1460 | fixed maximum segment size |
| `TCP_RTO_MS` | 500 | retransmit timeout in milliseconds |
| ARP cache | 16 | LRU entries |
| DNS cache | 16 | entries, TTL-limited |

---

## Boot Flow

`net_init()` is called from `kmain` after `sti` and PCI enumeration, in the
same place USB was initialised in P4.

```c
void net_init(void) {
    rtl8139_probe();                          // try RTL8139 first
    if (!registered_nif) e1000_probe();       // fall back to E1000
    if (!registered_nif) { KWARN("net: no supported NIC"); return; }
    (void)dhcp_start(registered_nif);         // populate IP or use static fallback
    KINFO("net: if=%s ip=%u.%u.%u.%u", ...);
}
```

Startup order:

1. PCI bus scan (already done by existing `pci_init`)
2. `rtl8139_probe()` — searches for PCI vid/did 10EC:8139; if found, resets, initialises, registers NIC via `net_if_register`, installs IRQ handler
3. If no RTL8139, `e1000_probe()` — searches for 8086:100E; same sequence
4. `dhcp_start()` — DISCOVER → OFFER → REQUEST → ACK, up to ~3 seconds
5. Static fallback — if no DHCP response: `ipv4_addr = 10.0.2.15`, mask `/24`, gateway `10.0.2.2`, DNS `10.0.2.3` (QEMU user-net defaults)
6. `net_process_pending` is wired into `kernel_check_reschedule` as a bottom-half, matching the P4 USB poll pattern

---

## NIC Layer

### `net_if_t` structure (kernel/net_if.h)

```c
#define NET_IF_MTU       1500
#define NET_IF_MAC_LEN   6
#define NET_RX_RING_SIZE 64

typedef struct net_if {
    const char *name;
    uint8_t     mac[NET_IF_MAC_LEN];
    uint32_t    ipv4_addr;        // network byte order
    uint32_t    ipv4_mask;
    uint32_t    ipv4_gateway;
    uint32_t    ipv4_dns;
    bool        link_up;
    void       *driver_data;
    int       (*send)(struct net_if *, const uint8_t *frame, uint32_t len);
    // counters
    uint64_t    rx_packets, tx_packets, rx_drops, tx_errors;
} net_if_t;
```

### Registration

```c
int       net_if_register(net_if_t *nif);  // called by driver init; first NIC wins
net_if_t *net_if_primary(void);            // returns the registered NIC (or NULL)
```

Only one primary NIC is supported. A second call to `net_if_register` logs a
warning and returns -1.

### RX ring (lockless SPSC)

```c
typedef struct {
    net_if_t *nif;
    uint32_t  len;
    uint8_t   frame[NET_IF_MTU + 14];       // full Ethernet frame
} net_rx_slot_t;

static net_rx_slot_t rx_ring[NET_RX_RING_SIZE] __attribute__((aligned(64)));
static volatile uint32_t rx_head;   // producer advances (IRQ context)
static volatile uint32_t rx_tail;   // consumer advances (bottom-half)
```

Producer (`net_rx_enqueue`, called from NIC IRQ):

- Computes `next = (rx_head + 1) % NET_RX_RING_SIZE`
- If `next == rx_tail`: ring full — increment `nif->rx_drops`, return without copy
- Otherwise: copy frame bytes, advance `rx_head`

Consumer (`net_process_pending`, called from idle bottom-half):

- While `rx_tail != rx_head`: pop slot, dispatch by ethertype
- `0x0806` (ARP) → `arp_input`; `0x0800` (IPv4) → `ipv4_input`
- Advance `rx_tail`; increment `nif->rx_packets`
- Calls `tcp_tick()` at end of each drain pass (retransmit + TIME_WAIT expiry)

Single-producer single-consumer invariant holds because the NIC IRQ disables
interrupts on entry (IF=0) and `net_process_pending` runs only on the BSP in
the idle/reschedule path.

---

## RTL8139 Driver (kernel/rtl8139.c)

### PCI identification

| Field | Value |
|---|---|
| Vendor ID | 0x10EC (Realtek) |
| Device ID | 0x8139 |
| Class code | 0x020000 (Ethernet) |
| BAR | BAR0 — 32-bit I/O space |

### Key I/O registers (offset from IO base)

| Offset | Name | Purpose |
|---|---|---|
| 0x00–0x05 | IDR0–IDR5 | 6-byte MAC address |
| 0x10–0x1C | TSAD0–TSAD3 | TX buffer physical addresses (4 descriptors) |
| 0x20–0x2C | TSD0–TSD3 | TX status/control (length + OWN bit) |
| 0x30 | RBSTART | RX ring buffer physical address |
| 0x37 | CMD | bit 4 = RST, bit 3 = RE, bit 2 = TE, bit 0 = BUFE |
| 0x38 | CAPR | RX read pointer (current address of packet read) |
| 0x3C | IMR | interrupt mask register |
| 0x3E | ISR | interrupt status register (W1C) |
| 0x40 | TCR | transmit configuration |
| 0x44 | RCR | receive configuration |
| 0x50 | CONFIG1 | power management / wake |

### Initialisation sequence

1. Enable PCI bus master and I/O decoding via PCI command register
2. Read BAR0 lower bits to obtain IO base; verify not MMIO
3. `outb(CONFIG1, 0)` — power on, clear LWAKE/PMEn
4. `outb(CMD, 0x10)` — software reset; poll CMD bit 4 clear (~1 ms)
5. Allocate RX buffer: 8192 + 16 + 1500 = 9708 bytes via `kmalloc`; align to 16 bytes (raw pointer saved for `kfree`)
6. Write RBSTART = physical address of aligned buffer
7. Write RCR = 0x0000000F (AAP + APM + AM + AB: accept physical, multicast, broadcast; WRAP=0)
8. Write TCR = 0x03000700 (default IFG, MXDMA=1024, retry-max=8)
9. `outb(CMD, 0x0C)` — enable RE and TE
10. `outw(IMR, 0x0005)` — mask ROK (bit 0) + TOK (bit 2)
11. Read MAC from IDR0–IDR5 into `net_if_t.mac`
12. Install IRQ handler via `irq_install_handler(irq_line, rtl8139_irq)`
13. Call `net_if_register(&nif)`

> **Note** — The common IRQ dispatcher in CupidOS calls `lapic_eoi()` after
> every handler returns. The RTL8139 IRQ handler must NOT call it directly;
> only the NIC-side ISR (W1C) needs clearing inside the handler.

### RX drain loop

```c
static void rtl8139_rx_drain(void) {
    while ((inb(io + 0x37) & 0x01) == 0) {     // BUFE == 0: data available
        uint8_t  *p      = rx_buf + capr_offset;
        uint16_t  status = *(uint16_t *)p;
        uint16_t  len    = *(uint16_t *)(p + 2); // includes 4-byte CRC
        if ((status & 1) && len >= 18 && len <= 1518) {
            net_rx_enqueue(&nif, p + 4, len - 4);
        }
        capr_offset = (capr_offset + len + 4 + 3) & ~3u;  // dword-align
        if (capr_offset >= 8192) capr_offset -= 8192;
        outw(io + 0x38, capr_offset - 16);       // CAPR quirk: write offset minus 16
    }
}
```

The -16 write to CAPR is a hardware quirk: the RTL8139 adds 16 to the value
written before using it as the actual read pointer.

### TX path (4-descriptor round-robin)

```c
static int rtl8139_send(net_if_t *nif, const uint8_t *frame, uint32_t len) {
    static int td = 0;
    while (inl(io + 0x20 + td * 4) & (1 << 13)) __asm__("pause"); // wait OWN clear
    memcpy(tx_buf[td], frame, len);
    outl(io + 0x10 + td * 4, (uint32_t)tx_buf[td]);  // TSAD: buffer phys addr
    outl(io + 0x20 + td * 4, len & 0x1FFF);           // TSD: length, clears OWN
    td = (td + 1) & 3;
    nif->tx_packets++;
    return (int)len;
}
```

---

## E1000 Driver (kernel/e1000.c)

### PCI identification

| Field | Value |
|---|---|
| Vendor ID | 0x8086 (Intel) |
| Device ID | 0x100E (82540EM) |
| Class code | 0x020000 (Ethernet) |
| BAR | BAR0 — 32-bit MMIO, 128 KB |

> **MMIO mapping required** — BAR0 is above the kernel's identity-mapped
> region. Call `paging_map_mmio(bar0_phys, 128 * 1024)` before accessing any
> register. Without this mapping, the first register read page-faults.

### Key MMIO registers

| Offset | Name | Purpose |
|---|---|---|
| 0x00000 | CTRL | General control; bit 26 = software reset |
| 0x00008 | STATUS | bit 1 = LU (link up) |
| 0x00014 | EERD | EEPROM read (addr/data/done/start) |
| 0x000C0 | ICR | Interrupt cause (W1C) |
| 0x000D0 | IMS | Interrupt mask set |
| 0x000D8 | IMC | Interrupt mask clear |
| 0x00100 | RCTL | RX control |
| 0x02800 | RDBAL / RDBAH | RX descriptor ring base (low / high) |
| 0x02808 | RDLEN | RX ring size in bytes |
| 0x02810 | RDH | RX descriptor head |
| 0x02818 | RDT | RX descriptor tail |
| 0x00400 | TCTL | TX control |
| 0x03800 | TDBAL / TDBAH | TX descriptor ring base |
| 0x03808 | TDLEN | TX ring size in bytes |
| 0x03810 | TDH | TX descriptor head |
| 0x03818 | TDT | TX descriptor tail |
| 0x05400 | RAL0 | Receive address low (MAC bytes 0–3) |
| 0x05404 | RAH0 | Receive address high (MAC bytes 4–5, bit 31 = valid) |

### RX descriptor (16 bytes, packed)

```c
typedef struct __attribute__((packed)) {
    uint64_t addr;       // DMA buffer physical address
    uint16_t len;        // bytes written by HW
    uint16_t checksum;
    uint8_t  status;     // bit 0 = DD (descriptor done), bit 1 = EOP
    uint8_t  err;
    uint16_t special;
} e1000_rx_desc_t;
```

TX descriptor: similar, with a `cmd` byte (EOP=1, RS=1 request status report,
IFCS=1 insert FCS / CRC).

### Initialisation sequence

1. Enable PCI bus master; read BAR0 MMIO base; `paging_map_mmio(bar0, 128 * 1024)`
2. Software reset: `CTRL |= (1 << 26)`; wait ~10 ms; reset bit self-clears
3. Read MAC: RAL0/RAH0 read unconditionally (QEMU 82540EM provides the MAC via RAL0/RAH0; no EEPROM fallback implemented)
4. Allocate RX ring: 64 × 16 bytes = 1024 bytes, 4KB-aligned via `pmm_alloc_page`; each descriptor's `addr` = 2KB-aligned DMA buffer
5. Write RDBAL/RDBAH = ring physical base; RDLEN = 1024; RDH = 0; RDT = 63
6. Write RCTL = `0x0400804` (EN | BAM | BSIZE_2048)
7. Allocate TX ring: 16 × 16 bytes = 256 bytes; zero-init; write TDBAL/TDBAH; TDLEN = 256; TDH = TDT = 0
8. Write TCTL = `0x01030002` (EN | PSP | CT=16 | COLD=64)
9. Write IMS = `0x80 | 0x40` to enable RXT0 | RXDMT0
10. Install IRQ handler; call `net_if_register(&nif)`

---

## ARP (kernel/arp.c)

### Cache structure

```c
typedef struct {
    uint32_t ip;              // network byte order
    uint8_t  mac[6];
    uint32_t last_used_tick;
    bool     valid;
} arp_entry_t;

static arp_entry_t arp_cache[16];
```

16-entry LRU cache. IPv4 + Ethernet only. On cache miss, the oldest entry is
evicted.

### `arp_resolve` (blocking, 500 ms timeout)

```c
int arp_resolve(uint32_t ip, uint8_t *mac_out);
```

1. Search cache for matching IP
2. Hit — copy MAC bytes to `mac_out`, update `last_used_tick`, return 0
3. Miss — build and broadcast ARP who-has request (Ethernet broadcast, ARP opcode 1)
4. Spin: poll cache every 1 ms for up to 500 ms
5. If reply arrives (ARP opcode 2): update cache, `arp_resolve` returns 0
6. Timeout: return -1; caller drops the outbound frame

### ARP input

On receiving an ARP frame (`arp_input`):

- Opcode 2 (reply): update or insert cache entry
- Opcode 1 (request): if target IP matches our `nif->ipv4_addr`, build and unicast an ARP reply (opcode 2) back to requester

---

## IPv4 (kernel/ip.c)

### Header structure

```c
typedef struct __attribute__((packed)) {
    uint8_t  vhl;           // 0x45 (version=4, IHL=5)
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;    // 0x4000 = DF set
    uint8_t  ttl;           // 64
    uint8_t  proto;
    uint16_t hdr_csum;
    uint32_t src_ip;
    uint32_t dst_ip;
} ipv4_hdr_t;
```

Checksum is ones-complement sum over all 16-bit header words.

### Send path

```c
int ipv4_send(uint32_t dst_ip, uint8_t proto, const uint8_t *payload, uint32_t len);
```

1. Increment monotonic 16-bit ID counter
2. Routing decision: if `(dst_ip & mask) == (our_ip & mask)` — ARP for `dst_ip` directly; else ARP for `ipv4_gateway`
3. Build frame: Ethernet header (14 bytes) + IPv4 header (20 bytes) + payload
4. Call `nif->send(nif, frame, total_len)`

### Receive path

```c
void ipv4_input(const uint8_t *frame, uint32_t len);
```

- Verify `ethertype == 0x0800`
- Validate IPv4 version=4, IHL=5, checksum
- Accept only frames where `dst_ip == nif->ipv4_addr` or `dst_ip == 255.255.255.255`
- Drop fragmented packets (MF=1 or fragment offset != 0); log warning
- Dispatch by `proto`: 1=ICMP → `icmp_input`; 6=TCP → `tcp_input`; 17=UDP → `udp_input`

---

## ICMP (kernel/icmp.c)

Only echo request → echo reply is implemented.

**Input (`icmp_input`):**

1. Verify type=8 (echo request), code=0
2. Recompute checksum to verify
3. Build reply: swap src/dst IP, set type=0 (echo reply), preserve identifier + sequence + payload
4. Recompute ICMP checksum (ones-complement over entire ICMP packet)
5. Send via `ipv4_send`

No outbound ICMP (e.g., destination unreachable, time exceeded) is generated
by the kernel. The `ping` shell command uses ICMP in send mode only in the
round-trip-time display; RTT measurement is approximate.

---

## UDP (kernel/udp.c)

### Header

```c
typedef struct __attribute__((packed)) {
    uint16_t src_port, dst_port;
    uint16_t length;     // UDP header + data (8 + data bytes)
    uint16_t checksum;   // pseudo-header + UDP header + data; 0 = disabled
} udp_hdr_t;
```

### Send

```c
int udp_send_raw(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                 const uint8_t *buf, uint32_t len);
```

Builds UDP header with pseudo-header checksum (src_ip + dst_ip + 0x00 + 17 +
udp_length), then calls `ipv4_send`.

### Receive (`udp_input`)

1. Parse UDP header from IPv4 payload
2. Search `sockets[]` for entry with `type == SOCK_TYPE_UDP && local_port == udp.dst_port`
3. If found: call `socket_udp_deliver(src_ip, src_port, dst_port, data, dlen)`
4. No match: silently drop

---

## TCP (kernel/tcp.c)

### Header

```c
typedef struct __attribute__((packed)) {
    uint16_t src_port, dst_port;
    uint32_t seq, ack;
    uint8_t  data_off;    // high 4 bits = header length / 4
    uint8_t  flags;       // FIN=0x01, SYN=0x02, RST=0x04, PSH=0x08, ACK=0x10
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} tcp_hdr_t;
```

### TCP states

| State | Meaning |
|---|---|
| `TCPS_CLOSED` | Initial state; no connection |
| `TCPS_LISTEN` | Passive open; awaiting inbound SYN |
| `TCPS_SYN_SENT` | Active open; SYN sent, awaiting SYN+ACK |
| `TCPS_SYN_RCVD` | SYN received (listen queue entry), SYN+ACK sent |
| `TCPS_ESTABLISHED` | Connection open; data exchange |
| `TCPS_FIN_WAIT_1` | FIN sent; awaiting ACK |
| `TCPS_FIN_WAIT_2` | ACK of FIN received; awaiting peer FIN |
| `TCPS_TIME_WAIT` | Both FINs exchanged; 60-second wait before CLOSED |
| `TCPS_CLOSE_WAIT` | Peer sent FIN; user has not yet closed |
| `TCPS_LAST_ACK` | FIN sent in response to peer FIN; awaiting ACK |

### State machine transitions

**Active open (`connect`):**

```
CLOSED → [send SYN] → SYN_SENT
SYN_SENT + SYN+ACK received → [send ACK] → ESTABLISHED
SYN_SENT + RST received → CLOSED (ECONNREFUSED)
SYN_SENT + timeout (30 s) → CLOSED (ETIMEDOUT_SOCK)
```

**Passive open (`listen` + `accept`):**

```
CLOSED → [listen()] → LISTEN
LISTEN + inbound SYN → [send SYN+ACK; push to lq[]] → (lq entry, SYN_RCVD-like)
lq entry + ACK → lq[].completed = true
accept() → dequeue completed lq entry → new socket in ESTABLISHED
```

**Close — active:**

```
ESTABLISHED → [send FIN+ACK] → FIN_WAIT_1
FIN_WAIT_1 + ACK → FIN_WAIT_2
FIN_WAIT_2 + FIN → [send ACK] → TIME_WAIT (60 s) → CLOSED
```

**Close — passive:**

```
ESTABLISHED + FIN received → [send ACK] → CLOSE_WAIT
CLOSE_WAIT + user close() → [send FIN+ACK] → LAST_ACK
LAST_ACK + ACK → CLOSED; socket freed
```

### Implementation details

- ISS (initial send sequence): `timer_get_uptime_ms() * 250000 + fd_index`
- Fixed MSS = 1460 bytes (no MSS option negotiation)
- Fixed RTO = 500 ms; `tcp_tick` re-sends SYN if `now - last_rexmit_tick > TCP_RTO_MS`
- TIME_WAIT duration = 60 seconds (`TCP_TIME_WAIT_MS`)
- Receive window advertised = 8192 (full `SOCK_RX_BUF`)
- No delayed ACK: every segment ACKed immediately
- No Nagle: `tcp_send` flushes immediately on every call
- No fast retransmit (3 duplicate ACK rule not implemented)
- No SACK, no congestion control, no window scaling

### Listen queue (per LISTEN socket)

8 slots (`LQ_SIZE`). Each slot:

```c
struct {
    uint32_t ip;       // peer IP
    uint16_t port;     // peer port
    uint32_t iss;      // our ISS for this connection
    uint32_t rcv_nxt;  // expected next byte from peer (seq + 1 after SYN)
    uint8_t  completed;// set to 1 when ACK of our SYN+ACK arrives
} lq[LQ_SIZE];
```

`tcp_accept` uses strict-FIFO dequeue: only the `lq_tail` slot is dequeued,
and only if `completed == 1`. This prevents out-of-order completions from
creating gaps in the ring.

### `tcp_tick` (called from `net_process_pending`)

```c
void tcp_tick(void) {
    uint32_t now = timer_get_uptime_ms();
    for (int i = 0; i < SOCKET_MAX; i++) {
        socket_t *s = &sockets[i];
        if (s->tcp_state == TCPS_SYN_SENT &&
            now - s->last_rexmit_tick > TCP_RTO_MS) {
            s->snd_nxt = s->snd_iss;    // rewind for retransmit
            tcp_send_seg(s, TCP_SYN, NULL, 0);
            s->last_rexmit_tick = now;
        }
        if (s->tcp_state == TCPS_TIME_WAIT &&
            now - s->time_wait_start > TCP_TIME_WAIT_MS) {
            s->tcp_state = TCPS_CLOSED;
            s->in_use = 0;
        }
    }
}
```

---

## DHCP (kernel/dhcp.c)

### Overview

```c
bool dhcp_start(net_if_t *nif);  // blocks up to ~3 s; returns true on success
```

Classic UDP four-way handshake: DISCOVER (port 68 → broadcast:67) →
OFFER → REQUEST → ACK. All frames use broadcast Ethernet MAC
(`ff:ff:ff:ff:ff:ff`) and broadcast IP (`255.255.255.255`) because no IP
address is available before DHCP completes. This bypasses the ARP layer
entirely.

### Protocol flow

1. **DISCOVER** — op=1, XID from `timer_get_uptime_ms()`, chaddr = our MAC, options: DHCP message type = DISCOVER, parameter-request-list (subnet-mask, router, dns)
2. **OFFER** — wait for op=2 matching XID; extract yiaddr, options (1=mask, 3=gateway, 6=dns, 51=lease-time)
3. **REQUEST** — op=1, DHCP type = REQUEST, option 50 = requested-IP, option 54 = server-id
4. **ACK** — populate `nif->ipv4_addr`, `nif->ipv4_mask`, `nif->ipv4_gateway`, `nif->ipv4_dns`

### Failure handling

| Condition | Action |
|---|---|
| No OFFER in 1 s | Retry DISCOVER; up to 3 attempts |
| NAK received | Retry from DISCOVER |
| Total timeout (3 s) | Static fallback: 10.0.2.15/24, gw 10.0.2.2, dns 10.0.2.3 |

Static fallback log: `KWARN("dhcp: fallback to static 10.0.2.15/24 gw 10.0.2.2 dns 10.0.2.3")`

Lease renewal is not implemented. The hobby OS reboots before leases expire.

---

## DNS (kernel/dns.c)

### API

```c
int dns_resolve(const char *name, uint32_t *ipv4_out);
// Returns 0 on success, negative errno on failure. Timeout = 3 s.
```

### Query format

Standard DNS wire format over UDP/53 to `nif->ipv4_dns`:

- 12-byte header: ID (random from tick), flags=0x0100 (RD=1), QDCOUNT=1
- QNAME: length-prefixed labels (`"example.com"` → `\x07example\x03com\x00`)
- QTYPE=1 (A), QCLASS=1 (IN)

### Response parsing

1. Skip header (12 bytes) and question section (re-parse QNAME + 4 bytes QTYPE/QCLASS)
2. Iterate answer RRs; find first RR with TYPE=1 (A) and RDLENGTH=4
3. Read 4 RDATA bytes as IPv4 address
4. Handle DNS compression pointers (`0xC0` high two bits) in NAME fields

### Cache

16 entries keyed on lowercased hostname. TTL = min(DNS-provided TTL, 300 s).
Cache hit skips the UDP/53 query entirely.

---

## Socket API

### Error codes (kernel/socket.h)

| Constant | Value | Meaning |
|---|---|---|
| `ENETDOWN` | -1 | No NIC registered |
| `EBADF` | -2 | Invalid or closed file descriptor |
| `EINVAL_SOCK` | -3 | Invalid argument |
| `EADDRINUSE` | -4 | Port already bound |
| `ECONNREFUSED` | -5 | RST received during connect |
| `ETIMEDOUT_SOCK` | -6 | Operation timed out (30 s) |
| `ECONNRESET` | -7 | Connection reset by peer |
| `ENOBUFS_SOCK` | -8 | No free socket slots |

### Socket types

```c
#define SOCK_TYPE_UDP 1
#define SOCK_TYPE_TCP 2
```

### `socket_t` structure (kernel/socket.h, abridged)

```c
typedef struct socket_t {
    uint8_t  type;          // SOCK_TYPE_UDP or SOCK_TYPE_TCP
    uint8_t  in_use;
    uint32_t local_ip;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;
    tcp_state_t tcp_state;

    uint8_t  tx_buf[SOCK_TX_BUF];   // 8192 bytes
    uint32_t tx_head, tx_tail;
    uint8_t  rx_buf[SOCK_RX_BUF];   // 8192 bytes
    uint32_t rx_head, rx_tail;

    udp_dgram_meta_t udp_meta[UDP_MAX_QUEUED];  // 8-slot per-datagram metadata
    uint8_t  udp_meta_head, udp_meta_tail;

    // TCP sequence state
    uint32_t snd_una, snd_nxt, snd_wnd, snd_iss;
    uint32_t rcv_nxt, rcv_wnd, rcv_irs;
    uint32_t last_rexmit_tick;
    uint32_t time_wait_start;

    // Listen queue (LISTEN state only)
    struct { uint32_t ip; uint16_t port; uint32_t iss;
             uint32_t rcv_nxt; uint8_t completed; } lq[LQ_SIZE]; // LQ_SIZE=8
    int lq_head, lq_tail;
} socket_t;

#define SOCKET_MAX 32
extern socket_t sockets[SOCKET_MAX];
```

### BSD API (kernel/socket.h)

```c
int socket_create  (int type);                                       // type: 1=UDP 2=TCP
int socket_bind    (int fd, uint32_t ip, uint16_t port);
int socket_listen  (int fd, int backlog);
int socket_accept  (int fd, uint32_t *peer_ip, uint16_t *peer_port); // blocking
int socket_connect (int fd, uint32_t ip, uint16_t port);             // blocking
int socket_send    (int fd, const void *buf, uint32_t len);
int socket_recv    (int fd, void *buf, uint32_t len);                 // blocking
int socket_close   (int fd);
int socket_sendto  (int fd, const void *buf, uint32_t len, uint32_t ip, uint16_t port);
int socket_recvfrom(int fd, void *buf, uint32_t len, uint32_t *ip, uint16_t *port); // blocking

uint16_t htons(uint16_t v);
uint32_t htonl(uint32_t v);
```

### Blocking model

`socket_accept`, `socket_connect`, `socket_recv`, and `socket_recvfrom` block
by spinning: every 1000 `pause` iterations they call `schedule()` to yield,
matching the BKL-yield design from P5. Timeout = 30 seconds → `ETIMEDOUT_SOCK`.

`socket_send` and `socket_sendto` do not block — they return the bytes written
(which may be less than `len` if the TX buffer is full).

### Ephemeral port allocation

Linear scan from a rotating index in the range 49152–65535. Each candidate is
checked against the in-use port set before assignment.

### BKL protection

All `socket_t` mutations and all calls into `tcp_*` / `udp_*` are made under
the big kernel lock (`bkl_lock` / `bkl_unlock`). `net_process_pending`
(bottom-half) also runs under BKL via the `kernel_check_reschedule` path. This
serialises TCP state machine transitions with RX-driven state changes. No
fine-grained per-socket locks at Tier 4.

---

## Shell Commands

New commands added to `kernel/shell.c`:

### `ifconfig`

Prints MAC address, IP/prefix length, gateway, DNS server, link state, and
cumulative RX/TX packet counts for the primary NIC.

```
rtl8139  mac=52:54:00:12:34:56  ip=10.0.2.15/24  gw=10.0.2.2  dns=10.0.2.3
         link=up  rx=42  tx=38  drops=0
```

`ifconfig <ip>/<prefix> [gw <ip>] [dns <ip>]` overrides addressing statically.

### `ping <host-or-ip> [count]`

Resolves `host` via `dns_resolve` if not already a dotted-quad, sends ICMP
echo requests, prints RTT per reply. Default count = 4, per-reply timeout = 2 s.

### `netstat`

Lists all 32 socket slots that are in use:

```
[fd]  type  state        local               remote
  0   TCP   ESTABLISHED  10.0.2.15:49152  →  93.184.216.34:80
  1   UDP   –            0.0.0.0:5353     →  –
```

### `arp`

Dumps the 16-entry ARP cache:

```
10.0.2.2  →  52:55:0a:00:02:02  (age 123 ticks)
```

### `resolve <hostname>`

Calls `dns_resolve`, prints the resulting IPv4 address or error code.

```
$ resolve example.com
93.184.216.34
```

---

## CupidC Bindings

All networking functions are registered in `kernel/cupidc.c` so they can be
called from CupidC programs and scripts with no additional setup.

| CupidC name | C function | Arg count |
|---|---|---|
| `socket` | `socket_create` | 1 |
| `bind` | `socket_bind` | 3 |
| `listen` | `socket_listen` | 2 |
| `accept` | `socket_accept` | 3 |
| `connect` | `socket_connect` | 3 |
| `send` | `socket_send` | 3 |
| `recv` | `socket_recv` | 3 |
| `close` | `socket_close` | 1 |
| `sendto` | `socket_sendto` | 5 |
| `recvfrom` | `socket_recvfrom` | 5 |
| `dns_resolve` | `dns_resolve` | 2 |
| `htons` | `htons` | 1 |
| `htonl` | `htonl` | 1 |

### Feature test: `feature21_net` (TCP client)

```c
// bin/feature21_net.cc — DNS resolve + TCP connect + HTTP GET
U0 Main() {
    U32 ip = 0;
    if (dns_resolve("example.com", &ip) != 0) {
        serial_printf("[feature21] SKIP dns failed\n"); return;
    }
    I32 fd = socket(2);   // TCP
    if (connect(fd, ip, htons(80)) != 0) {
        serial_printf("[feature21] SKIP connect failed\n"); close(fd); return;
    }
    const char *req = "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n";
    send(fd, req, 36);
    U8 buf[2048];
    I32 n = recv(fd, buf, 2048);
    close(fd);
    if (n > 100 && buf[0] == 'H' && buf[1] == 'T' && buf[2] == 'T') {
        serial_printf("[feature21] PASS (%d bytes)\n", n);
    } else {
        serial_printf("[feature21] FAIL recv n=%d\n", n);
    }
}
Main();
```

### Feature test: `feature22_net_server` (TCP server)

```c
// bin/feature22_net_server.cc — listen on port 80, serve one request
U0 Main() {
    I32 fd = socket(2);
    bind(fd, 0, htons(80));
    listen(fd, 1);
    serial_printf("[feature22] listening on port 80\n");

    U32 peer_ip = 0;  U16 peer_port = 0;
    I32 cfd = accept(fd, &peer_ip, &peer_port);

    U8 buf[512];
    I32 n = recv(cfd, buf, 512);
    const char *resp = "HTTP/1.0 200 OK\r\nContent-Length: 13\r\n\r\nHello CupidOS";
    send(cfd, resp, 49);
    close(cfd);  close(fd);
    serial_printf("[feature22] PASS served %d bytes in\n", n);
}
Main();
```

Host-side test: `curl http://localhost:8080/` (with `-hostfwd=tcp::8080-:80`).

---

## Testing

### QEMU targets

Three `make` targets boot CupidOS under QEMU with networking enabled:

| Target | NIC | Port forward | SMP |
|---|---|---|---|
| `make run-net` | RTL8139 | host:8080 → guest:80 | 1 CPU |
| `make run-smp-net` | RTL8139 | host:8080 → guest:80 | 4 CPUs |
| `make run-net-e1000` | E1000 | none | 1 CPU |

Equivalent QEMU invocations:

```bash
# run-net
qemu-system-i386 $(QEMU_COMMON) \
    -netdev user,id=n0,hostfwd=tcp::8080-:80 \
    -device rtl8139,netdev=n0 \
    -serial stdio

# run-smp-net
qemu-system-i386 $(QEMU_COMMON) -smp cpus=4 \
    -netdev user,id=n0,hostfwd=tcp::8080-:80 \
    -device rtl8139,netdev=n0 \
    -serial stdio

# run-net-e1000
qemu-system-i386 $(QEMU_COMMON) \
    -netdev user,id=n0 \
    -device e1000,netdev=n0 \
    -serial stdio
```

### Live test procedure

1. `make run-net` — boots with RTL8139 + port forward
2. Wait for serial to show `net: if=rtl8139 ip=10.0.2.15` (DHCP or fallback)
3. In guest shell: `ifconfig` — verify IP, gateway, DNS
4. In guest shell: `feature21_net` — expected output: `[feature21] PASS (N bytes)`
5. In guest shell: `feature22_net_server` — guest prints `[feature22] listening on port 80`
6. On host: `curl http://localhost:8080/` — expected response: `Hello CupidOS`
7. Guest prints: `[feature22] PASS served N bytes in`

### E1000 path

```bash
make run-net-e1000
```

At boot, `net_init` will not find RTL8139, falls through to `e1000_probe`.
Serial output: `net: registered e1000 mac=...`. Run the same feature tests.

### Packet capture (debug)

```bash
# If using -netdev tap (not user-net):
sudo tcpdump -i tap0 -n -vv
```

---

## Known Limits

| Limitation | Notes |
|---|---|
| IPv4 only | No IPv6 |
| No IP fragmentation | DF=1 on every send; fragmented receives dropped |
| No Path MTU Discovery | MSS fixed at 1460 |
| No TCP SACK / congestion / Nagle | Plain RFC 793 subset with fixed 500 ms RTO |
| Single primary NIC | No multi-homing, no routing table |
| 32 socket slots | No dynamic expansion |
| No TLS / HTTPS | No crypto primitives in CupidOS |
| No DHCP lease renewal | Reboots before lease expires in practice |
| DNS A-record only | No AAAA, no full CNAME chasing, no PTR |
| No raw sockets / packet filter | No PROMISC, no promiscuous mode |
| Blocking via spin + yield | `accept`/`connect`/`recv` hold BKL while waiting; not efficient under contention |
| No IPv6 multicast / mDNS | Only unicast and broadcast IPv4 |

---

## Source File Index

| File | Purpose |
|---|---|
| `kernel/net_if.h` | `net_if_t` struct, RX ring constants, API declarations |
| `kernel/net_if.c` | NIC registration, RX ring, `net_init`, `net_process_pending` |
| `kernel/arp.h` | ARP cache struct, `arp_resolve` / `arp_input` declarations |
| `kernel/arp.c` | 16-entry LRU cache, ARP request/reply handler, blocking resolve |
| `kernel/ip.h` | `ipv4_hdr_t`, protocol constants, API |
| `kernel/ip.c` | IPv4 send + receive + checksum + routing |
| `kernel/icmp.h` | ICMP header struct, `icmp_input` declaration |
| `kernel/icmp.c` | Echo request → echo reply |
| `kernel/udp.h` | UDP header struct, `udp_send_raw`, `udp_input` |
| `kernel/udp.c` | UDP send + receive + pseudo-header checksum |
| `kernel/tcp.h` | `tcp_hdr_t`, flag macros, `TCP_MSS`, `TCP_RTO_MS`, API |
| `kernel/tcp.c` | RFC 793 state machine, `tcp_tick`, ~1200 LOC |
| `kernel/socket.h` | `socket_t`, error codes, `tcp_state_t`, BSD API declarations |
| `kernel/socket.c` | 32-slot table, `socket_create`/`bind`/`listen`/`accept`/… |
| `kernel/dhcp.h` | `dhcp_start` declaration |
| `kernel/dhcp.c` | DISCOVER/OFFER/REQUEST/ACK, static fallback |
| `kernel/dns.h` | `dns_resolve` declaration, cache constants |
| `kernel/dns.c` | UDP/53 query, response parse, compression pointer, 16-entry cache |
| `kernel/rtl8139.h` | RTL8139 register offsets, `rtl8139_probe` declaration |
| `kernel/rtl8139.c` | PCI probe, init, RX drain, send, IRQ handler, ~300 LOC |
| `kernel/e1000.h` | E1000 register offsets, `e1000_probe` declaration |
| `kernel/e1000.c` | PCI probe, MMIO map, RX/TX ring init, IRQ handler, ~500 LOC |
| `bin/feature21_net.cc` | TCP client smoke test: DNS + connect + HTTP GET |
| `bin/feature22_net_server.cc` | TCP server smoke test: listen + accept + echo |
