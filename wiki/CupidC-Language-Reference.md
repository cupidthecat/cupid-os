# CupidC Language Reference

CupidC is a C-like language for cupid-os. Its JIT compiler emits x86 machine code.

The language accepts the common C and HolyC spellings used by the shipped
programs: `U0/U8/U16/U32/I8/I16/I32`,
`U64/I64`, `float`, `double`, `float4`, `double2`, `long`, `short`,
`signed`, `unsigned`, `extern`, `inline`, `register`, `restrict`, labels,
`goto`, and skipped `__attribute__((...))` decorations.

## String Escape Sequences

CupidC supports the following escape sequences in string and character literals:

| Escape | Meaning | ASCII Value |
|--------|---------|-------------|
| `\n` | Newline (LF) | 10 |
| `\t` | Tab | 9 |
| `\r` | Carriage return (CR) | 13 |
| `\b` | Backspace | 8 |
| `\\` | Backslash | 92 |
| `\'` | Single quote | 39 |
| `\"` | Double quote | 34 |
| `\0` | Null terminator | 0 |
| `\xNN` | Hexadecimal byte (00-FF) | Variable |

### Hexadecimal Escapes

The `\xNN` escape sequence allows specifying a byte value in hexadecimal:

```c
char esc = '\x1B';          // ESC character (27)
char *red = "\x1B[31m";     // ANSI red color code
print("\x48\x69");          // Prints "Hi" (0x48='H', 0x69='i')
```

Both uppercase and lowercase hex digits are supported: `\x1B`, `\x1b`, `\xFF`, `\xff`

## Control Flow

### Break Statement

The `break` statement exits the innermost loop or switch statement:

```c
while (1) {
    if (condition1) break;  // Exit loop
    if (condition2) break;  // Multiple breaks supported
}
```

CupidC supports multiple `break` statements per loop. The compiler maintains
an array of up to 32 break patch locations per loop and patches them when the
loop ends.

### Labels and Goto

Simple local labels and `goto` are supported for C compatibility:

```c
void main() {
    int n = 3;
again:
    print_int(n);
    n = n - 1;
    if (n > 0) goto again;
}
```

## Available Functions

### Built-in Functions

#### I/O Functions
- `print(char *s)` - Print a string
- `putchar(char c)` - Print a single character
- `print_int(int n)` - Print an integer
- `print_hex(int n)` - Print integer in hexadecimal
- `getchar()` - Read a character from keyboard

#### Memory Functions
- `kmalloc(int size)` - Allocate memory
- `kfree(void *ptr)` - Free memory
- `memset(void *ptr, int val, int size)` - Set memory bytes
- `memcpy(void *dst, void *src, int size)` - Copy memory

#### String Functions
- `strlen(char *s)` - Get string length
- `strcmp(char *s1, char *s2)` - Compare strings
- `strncmp(char *s1, char *s2, int n)` - Compare first n characters

#### File System Functions
- `vfs_open(char *path, int flags)` - Open a file
- `vfs_close(int fd)` - Close a file
- `vfs_read(int fd, char *buf, int size)` - Read from file
- `vfs_write(int fd, char *buf, int size)` - Write to file
- `vfs_unlink(char *path)` - Delete a file
- `vfs_mkdir(char *path)` - Create a directory
- `resolve_path(char *rel, char *abs)` - Resolve relative path to absolute

#### Program Functions
- `get_args()` - Get command-line arguments as string
- `exit(int code)` - Exit program with code

#### Networking - NIC info
- `net_get_ip()` - Primary NIC IPv4 address (host byte order)
- `net_get_gateway()` - Default gateway IPv4
- `net_get_dns()` - DHCP-assigned DNS server IPv4
- `net_get_mask()` - Subnet mask
- `net_get_mac(uint8_t *out)` - Fills 6-byte MAC into `out`
- `net_link_up()` - 1 if link up, 0 if down
- `net_rx_packets()` / `net_tx_packets()` - Packet counters
- `net_rx_drops()` / `net_tx_errors()` - Error counters

#### Networking - Layer 2/3
- `ip_parse(char *s, uint32_t *out)` - Parse `"A.B.C.D"` into uint32_t
- `ipv4_send(uint32_t dst, uint8_t proto, uint8_t *payload, uint32_t len)` - Raw IPv4 send
- `arp_resolve(uint32_t ip, uint8_t *mac_out)` - 500ms blocking resolve, 0 ok / -1 timeout
- `arp_dump()` - Print ARP cache to serial
- `arp_get_entries(uint32_t *ips, uint8_t (*macs)[6], int max)` - Iterate cache
- `icmp_send_echo(uint32_t dst, uint16_t id, uint16_t seq, uint32_t paylen)` - Send ping
- `icmp_wait_reply(uint32_t src, uint16_t id, uint16_t seq, uint32_t timeout_ms)` - Wait for echo reply
- `udp_send_raw(uint32_t dst, uint16_t src_port, uint16_t dst_port, uint8_t *data, uint32_t len)` - Raw UDP send
- `dns_resolve(char *name, uint32_t *ip_out)` - A-record lookup
- `htons` / `htonl` / `ntohs` / `ntohl` - Byte-order helpers

#### Networking - BSD sockets
- `socket(int type)` - `SOCK_TYPE_TCP=1`, `SOCK_TYPE_UDP=2`. Returns fd.
- `bind(int fd, uint32_t ip, uint16_t port)` - Bind to address:port
- `listen(int fd, int backlog)` - TCP passive listen
- `accept(int fd, uint32_t *peer_ip, uint16_t *peer_port)` - Accept TCP
- `connect(int fd, uint32_t ip, uint16_t port)` - TCP connect / UDP set-default-peer
- `send(int fd, void *buf, uint32_t len)` - Send (routes through TLS if enabled)
- `recv(int fd, void *buf, uint32_t len)` - Receive
- `sendto(int fd, void *buf, uint32_t len, uint32_t ip, uint16_t port)` - UDP sendto
- `recvfrom(int fd, void *buf, uint32_t len, uint32_t *ip, uint16_t *port)` - UDP recvfrom
- `setsockopt(int fd, int level, int optname, void *val, uint32_t vlen)` - Use `level=SOL_TLS=1`, `optname=TLS_ENABLE=1`, `val=hostname`, `vlen=strlen(hostname)` to upgrade a connected TCP socket to TLS 1.3
- `sock_avail(int fd)` - Bytes currently buffered (0 means a `recv` would block); `EBADF` on bad fd
- `sock_state(int fd)` - Returns `tcp_state_t` enum value (`TCPS_*`); `EBADF` on bad fd
- `close(int fd)` - Close socket

```c
// HTTP-over-TLS minimal client
void main() {
    uint32_t ip;
    if (dns_resolve("example.com", &ip) != 0) { print("dns fail\n"); return; }
    int fd = socket(1);                          // SOCK_TYPE_TCP
    if (connect(fd, ip, 443) != 0) { print("connect fail\n"); return; }
    setsockopt(fd, 1, 1, "example.com", 11);     // SOL_TLS, TLS_ENABLE
    char *req = "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n";
    send(fd, req, strlen(req));
    char buf[2048];
    int n = recv(fd, buf, 2047);
    if (n > 0) { buf[n] = 0; print(buf); }
    close(fd);
}
```

#### Audio - AC97 driver
- `ac97_init()` - Probe + init AC97 card, returns 0 on success
- `ac97_start()` - Arm DMA
- `ac97_stop()` - Halt + mute
- `ac97_set_master_volume(uint8_t pct)` - 0-100
- `ac97_set_pcm_volume(uint8_t pct)` - 0-100, sets the PCM-out channel attenuation
- `ac97_get_master_volume()` - Returns last-set master percentage (0 if device absent)
- `ac97_get_pcm_volume()` - Returns last-set PCM percentage
- `ac97_tsc_sleep_ms(uint32_t ms)` - TSC busy-wait (IRQ-state independent)
- `ac97_is_present_int()` - Returns 0 or 1
- `ac97_smoke_sine()` - 440 Hz triangle for 2s
- `ac97_smoke_sweep()` - 50-8000 Hz sweep
- `ac97_smoke_pan()` - L↔R panning
- `audiotest_all()` - Sine + sweep + pan + opl in sequence

```c
void main() { ac97_init(); ac97_smoke_sine(); }
```

```c
// Set master volume to 50% (or read current with ac97_get_master_volume).
// Full source: bin/volume.cc.  Run:  volume 50
```

#### Imaging - in-memory codecs
- `png_decode_mem(uint8_t *data, uint32_t len, uint32_t **out_pixels, int *out_w, int *out_h)` - Decode PNG to a fresh XRGB buffer (caller `kfree`s `*out_pixels`); returns 0 on success, negative `PNG_E*` on failure. Non-interlaced 8-bit PNGs only.
- `jpeg_decode_mem(uint8_t *data, uint32_t len, uint32_t **out_pixels, int *out_w, int *out_h)` - Baseline JPEG (SOF0/SOF1, 8-bit, 1- or 3-channel); same buffer convention.
- `kdeflate_raw(uint8_t *src, uint32_t src_len, uint8_t *out, uint32_t out_len)` - RFC 1951 raw DEFLATE; returns produced bytes or negative on error.

```c
// Load a PNG from disk and blit it to the screen.
uint8_t *bytes; int n = vfs_read_all("/img.png", &bytes);
uint32_t *px; int w, h;
if (png_decode_mem(bytes, n, &px, &w, &h) == 0) {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) gfx2d_pixel(x, y, px[y*w + x]);
    gfx2d_flip();
    kfree(px);
}
kfree(bytes);
```

#### Audio - MIDI / OPL3 synth
- `midiopl_init(uint8_t *genmidi_lump, uint32_t lump_len)` - Parse Doom GENMIDI patches
- `midiopl_reset()` - Silence all channels, keep patches
- `midiopl_feed(uint8_t *bytes, uint32_t len)` - Stream MIDI bytes
- `midiopl_render(int16_t *out_stereo, uint32_t frames)` - Pull synth output @ 22050 Hz
- `midiopl_set_volume(uint8_t v)` - 0-127
- `opl_smoke()` - OPL3 smoke test

#### Audio - PCM mixer (16 slots, s16 stereo @ 22050 Hz)
- `mixer_init()` - One-time init
- `mixer_play(int slot, int16_t *pcm, uint32_t frames, uint8_t channels, uint8_t loop, uint8_t vol_l, uint8_t vol_r)` - Start playback
- `mixer_stop(int slot)` - Stop
- `mixer_active(int slot)` - 1 if playing
- `mixer_set_volume(int slot, uint8_t l, uint8_t r)`
- `mixer_fill(int16_t *out, uint32_t frames)` - Fill output buffer (called by AC97 IRQ)

## Limitations

- Maximum 32 `break` statements per loop
- Maximum 64 nested loops
- No `switch` statement with more than 64 cases
- No preprocessor macros
- Not full hosted GCC C; accepted wide integer spellings still target the
  32-bit flat kernel ABI unless a binding explicitly handles wider data

## Common Patterns

### Parsing Command-Line Arguments

Programs receive arguments as a single string via `get_args()`. To parse multiple space-separated arguments:

```c
// Parse a single token from a string
int parse_token(char *str, int start, char *out, int maxlen) {
    int i = start;

    // Skip leading spaces
    while (str[i] == ' ' || str[i] == '\t') {
        i = i + 1;
    }

    // Check if end of string
    if (str[i] == 0) {
        out[0] = 0;
        return 0;
    }

    // Copy token until space or end
    int j = 0;
    while (str[i] != 0 && str[i] != ' ' && str[i] != '\t' && j < maxlen - 1) {
        out[j] = str[i];
        i = i + 1;
        j = j + 1;
    }
    out[j] = 0;

    return i - start;
}

// Usage example
void main() {
    char *args = (char*)get_args();
    char token[256];
    int pos = 0;

    while (1) {
        int len = parse_token(args, pos, token, 256);
        if (len == 0) break;  // No more tokens

        print("Token: ");
        print(token);
        print("\n");

        pos = pos + len;
    }
}
```

### Error Handling with VFS

```c
int fd = vfs_open(path, 0);
if (fd < 0) {
    print("Error opening file: ");
    print(path);
    print("\n");
    return;
}

// Use file...
vfs_close(fd);
```

## Examples

### Hello World

```c
void main() {
    print("Hello, World!\n");
}
```

### Using ANSI Colors

```c
void main() {
    char *red = "\x1B[31m";
    char *reset = "\x1B[0m";

    print(red);
    print("This is red text");
    print(reset);
    print("\n");
}
```

### Loop with Multiple Breaks

```c
void main() {
    int i = 0;
    while (i < 100) {
        if (i == 10) break;      // Exit at 10
        if (i % 7 == 0) break;   // Or exit at first multiple of 7
        i = i + 1;
    }
    print_int(i);
}
```
