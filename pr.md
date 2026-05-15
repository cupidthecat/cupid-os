# Browser stack + networking robustness

Branch: `browser-render-pipeline` → `main` (144 commits ahead).

## Summary

This branch rebuilds the in-tree web browser around a real HTML5-style
tokenizer, a CSS engine with cascade and specificity, BFC + IFC layout,
a paint pipeline that walks a render tree, and a small JavaScript engine
with DOM bindings — and lands the networking and TLS fixes that let the
browser actually fetch HTTPS pages without dropping bytes. Two
unrelated tracks live in the same branch (DOOM port, audio/OPL3,
build/homefs reorg) — those are out of scope for this PR description
and are documented separately.

The browser binary is JIT-compiled by CupidC at runtime, so a long tail
of dialect workarounds were needed once the implementation started
exercising parts of the parser nobody had used before. Those are
captured below for future contributors.

## SSH client + GUI terminal updates (May 15, 2026)

The branch now includes a CupidC SSH client and the kernel/terminal
plumbing needed to use it interactively from the GUI Terminal.

- Added `/bin/ssh.cc`, a CupidC SSH-2 client that negotiates
  `curve25519-sha256` with `chacha20-poly1305@openssh.com`, verifies
  `ssh-ed25519`, `rsa-sha2-512`, and `ecdsa-sha2-nistp256` host keys,
  performs password / keyboard-interactive auth, opens a session channel,
  requests a PTY, and runs an interactive remote shell.
- Added `kernel/lang/ssh_io.c` / `.h` bridge helpers for CupidC programs:
  hidden password input, GUI-safe byte output, VT/xterm key translation,
  GUI terminal geometry, and P-256 host-key signature verification.
- Moved GUI Terminal command execution out of the desktop key handler and
  into the terminal process. This keeps the desktop repaint/input loop
  alive while SSH blocks on password input or remote I/O.
- Increased the terminal process stack to 512 KiB so CupidC JIT compile
  of larger programs like `/bin/ssh.cc` does not smash heap metadata.
- Expanded GUI ANSI/terminal handling for real Linux PTY output:
  cursor movement, cursor positioning, erase-line/display, save/restore
  cursor, OSC title sequences, 256-color SGR fallbacks, bracketed-paste
  private-mode sequences, CRLF-safe line advancement, and remote special
  keys such as arrows/Home/End/PageUp/PageDown/Delete.
- Added enough xterm/VT behavior for full-screen SSH programs such as
  `top` and `btop`: alternate screen save/restore, cursor visibility,
  application cursor-key mode, origin mode, scroll regions, insert/delete
  lines, insert/delete/erase chars, repeat-character CSI, ACS line-drawing
  fallback, UTF-8 cell fallback, reverse-video SGR, and truecolor-to-VGA
  color fallback.
- GUI Terminal now tracks visible rows as well as columns, resets scrollback
  while a full-screen alternate-screen program is active, and SSH sends
  `window-change` channel requests when the terminal geometry changes.
- Added targeted serial diagnostics for SSH terminal debugging:
  `[ssh-debug]` channel/window/render byte dumps, `[ssh-io]` VT key
  translation, `[ssh-input]` GUI key routing into the JIT input queue, and
  `[ssh-render]` ANSI parser/cursor actions.
- Fixed a GUI input path where cursor navigation could reach SSH as raw
  control bytes (`Ctrl-P/N/F/B`) while `top` had application-cursor mode
  enabled. SSH now converts those fallback bytes to the expected application
  cursor sequences (`ESC OA/OB/OC/OD`) so selection movement updates in
  remote full-screen programs.
- SSH PTY setup now reports the actual GUI terminal columns/rows and sends
  cooked terminal modes for echo, canonical input, CR/LF behavior,
  Ctrl-C, Backspace, and terminal speeds.

## Telnet client + CupidC network bindings (May 15, 2026)

The branch now also includes an interactive CupidC Telnet client for
plain TCP Telnet services such as Telehack.

- Added `/bin/telnet.cc`, with host/port parsing, DNS/IP resolution,
  TCP connect, Telnet option negotiation, subnegotiation handling, and
  interactive keyboard/display bridging through the GUI terminal path.
- Implemented Telnet `IAC`, `WILL/WONT/DO/DONT`, `SB/SE`, terminal-type
  (`TTYPE`) and window-size (`NAWS`) support. The client reports
  `CUPIDOS` as the terminal type and updates NAWS when the terminal
  size changes.
- Added a local Telnet escape prompt on `Ctrl+]` with `status`, `quit`,
  and `send ayt/brk/ip/ao/nop` commands.
- Fixed GUI Enter handling for Telnet: the keyboard bridge delivers
  Enter as LF, and the Telnet client now always transmits Telnet
  newline as `CR LF`, even if the peer negotiated transmit-binary mode.
  This fixes Telehack commands such as `help`, `rand`, `clear`, and
  `login` not submitting after typing.
- Added the missing kernel syscall-table entries and user C wrappers for
  `sock_avail` and `sock_state`, plus expanded `user/cupid.h` network
  constants and socket/DNS helpers so user programs can monitor TCP
  state without reaching into kernel internals.
- Updated the CupidC compiler so array bounds at file scope and inside
  struct/class fields accept constant integer expressions, including
  enum constants. Enum explicit values now accept constant expressions
  too. This lets code like `char sb_buf[SB_CAP];` compile without
  rewriting named constants into literals.
- Added regression coverage in `/bin/cupidc_test5.cc` for enum-sized
  global arrays, 2D arrays, and struct field arrays.
- `user/Makefile` now disables the host stack protector for freestanding
  user examples, avoiding unresolved `__stack_chk_fail` during local
  user builds.

## Recent commits (May 9-11, 2026)

The last nine commits move the branch from "browser stack works" to a
more complete modern-page renderer, close a binding parity gap in the
script front-ends, and sweep up three unrelated regressions in
userspace tools, the file-dialog, and the DNS resolver.

| Commit | Area | Change |
|---|---|---|
| `93022dd8` | Browser + kernel | CSS custom properties, `var()`, `calc()`, HTTP/1.1 chunked decoding, PNG alpha compositing, opaque JPEG output, per-codepoint font fallback, NotoSansSymbols, larger kernel image area, and CupidC parser/binding upgrades. |
| `1098c887` | Cascade | Inline `--vars` now resolve before regular author rules, so `style="--c: blue"` is visible to `color: var(--c)`. `f4_img_intrinsic.html` now uses a stable Wikimedia asset. |
| `ca3f932f` | Layout | CSS floats, line-box exclusion, `clear`, BFC scoping, float paint ordering, block-level replaced elements, and adjacent string literal concatenation in CupidC. |
| `b2b891b6` | Visual CSS | `box-sizing: border-box`, `linear-gradient()` backgrounds, z-index stacking sort, negative absolute offsets, positioned containing-block fixes, text alignment, per-side border paint, rounded gradient primitives, and larger CupidC preprocessor output. |
| `82b626c0` | Flexbox | Single-line `display: flex` / `inline-flex`, `flex-direction`, `justify-content`, `align-items`, `flex` shorthand, `gap`, grow/shrink distribution, and i-series flex tests. |
| `2b917ee0` | Backgrounds | `background-image`, `background-size`, `background-position`, `background-repeat`, async background image fetch/dedup, clip-protected cover/contain paint, and j-series tests. |
| `89ea6b6a` | Bindings + audio | CupidASM is brought to CupidC binding parity, AC97 gains PCM volume plus master/PCM getters, `bin/volume.cc` lands, `uint*_t` / `int*_t` aliases parse in CupidC, auto-main avoids double invocation, and docs are refreshed. |
| `64fdafa2` | Correctness + demo | `<style>` RAWTEXT beyond 4 KiB survives, flex items are blockified, column flex auto-basis uses natural item height, duplicate column-flex layout is avoided, `overflow:hidden` establishes BFC containment, and `tests/browser/demo_showcase.html` exercises the full visual stack. |
| `c5dafc60` | Userspace + kernel | Userspace dirent buffers (`help`, `ls`, `fm`, `grep`, `find`) widened to the real 136-byte `vfs_dirent_t` layout, `/home/bin` auto-created at boot, file-dialog modal key-input drains the host window's per-window queue instead of racing the desktop dispatcher, and DNS failure paths now serial-dump the wire bytes for diagnosis. |

Current browser test surface added by these commits:

- `tests/browser/e4..e9` and `f1..f7`: custom properties, `calc`,
  Unicode ranges, WOFF1 fallback, decoded images, intrinsic image
  sizing, font metrics, per-codepoint face routing, and inline vars.
- `tests/browser/g1..g5`: floats, clear, infobox-style figures, two
  columns, and floated images.
- `tests/browser/h1..h4`: box sizing, z-index, gradients, rounded
  corners, negative offsets, and card-style combinations.
- `tests/browser/i1..i4`: row/column flex, justify packing, cross-axis
  alignment, and flex growth.
- `tests/browser/j1..j4`: background image sizing, repeat, position,
  cover, contain, and clipping.
- `tests/browser/demo_showcase.html`: integrated nav, hero, font cards,
  flex cards, floated article, stats strip, and footer.

---

## Browser stack

### Modularisation

`bin/browser.cc` was a single monolithic file. It is now a thin master
that `#include`s a directory of focused modules:

```
bin/browser/main.cc          globals + entry point + event loop
bin/browser/util.cc          string helpers (b_strieq_n, b_strlen, ...)
bin/browser/url.cc           URL parsing
bin/browser/net.cc           HTTP/HTTPS fetch (TLS via setsockopt)
bin/browser/dom.cc           node alloc, attribute pool, color, entities
bin/browser/js_lex.cc        JS tokenizer
bin/browser/js_dom.cc        JS DOM bindings
bin/browser/js_interp.cc     JS interpreter (values, scopes, eval)
bin/browser/js_parse.cc      JS recursive-descent parser
bin/browser/parser.cc        HTML tokenizer + tree builder
bin/browser/css.cc           CSS lexer + selector/rule parser
bin/browser/style.cc         UA stylesheet, cascade, inheritance
bin/browser/render_tree.cc   render-tree builder
bin/browser/layout.cc        BFC + IFC layout
bin/browser/paint.cc         paint, hit-test
bin/browser/nav.cc           navigation, form submit, about:dump
bin/browser/input.cc         keyboard, mouse, hover, click
```

Commits: `fc7e5b5` `53864fc` `c2ee640` `69701fe` `b2a155c` `bb9c3ea`
`23651e2` `7480ea9` `75d48dc` `983dcc1` `8820f2d` `83e2598` `8a1afdc`
`5a79371`.

### Render pipeline rewrite

The page-load pipeline is now `tokenize → tree-build → CSS parse →
style resolve → render-tree build → layout → paint`. Each stage owns
its own state and never reaches into the next stage's pools.

| Commit | What |
|---|---|
| `aa28259` | pool sizes + capacity constants |
| `fdcdfda` | §1 13-state HTML5-like tokenizer w/ RAWTEXT/RCDATA |
| `7af881a` | tree builder over token stream + attr-pair pool |
| `16b45f0` | §2 CSS lexer + UA stylesheet skeleton |
| `585fd5d` | §2 style resolver — UA + author cascade + inheritance |
| `3cfeab9` | §3 render-tree builder |
| `955c895` | §4 BFC layout over render tree |
| `853cc39` | §4 IFC line-box layout |
| `df94dcf` | §6 paint walks the render tree |
| `16b77b6` | §6 hit-test + form submit rebound to RT/new DOM |
| `a45520d` | drop legacy box-list layout/paint |
| `d001328` | final pass — `example.com` matches Chrome |

### Polish (this PR's headline work)

| Phase | Commit | Change |
|---|---|---|
| A | `1fd5c45` | UA stylesheet — drop italic-underline bug, blockquote left bar, table cell borders, body-bg double-paint fix |
| B1 | `0373eba` | CSS child combinator `>` and verify universal `*` |
| B2 | `a1b64d1` | CSS attribute selectors `[attr]`, `[attr=v]`, `[attr~=w]` |
| B3 | `a0cbb0c` | CSS `:hover`/`:focus`/`:link` with hover-driven restyle |
| B4 | `b163181` | CSS `border`/`font` shorthands, `rgb()`/`rgba()`, `rem` |
| C1 | `a2f5208` | kernel `gfx2d_glyph_advance` / `gfx2d_text_width_n` / `gfx2d_text_n` — proportional advances |
| C2 | `40fa21a` | browser layout & paint use proportional widths |
| D1 | `7e4361a` | vertical margin collapsing between block siblings |
| D2 | `2948341` | clamp auto-margin overflow + child_x clamp |
| E1 | `7036f52` | extend HTML entity table (named + numeric high codepoints) |
| E3 | `7045a5c` | `about:dump` URL + Ctrl-D shortcut for serial render-tree dump |

### JavaScript engine

A small tree-walking interpreter that lives entirely in fixed pools
reset per page-load (mirrors the existing `attr_pool` discipline). Style
follows `kernel/cupidscript_*.c`: recursive-descent, parallel-array
AST, no `kmalloc` mid-script.

**Phase 1 — interpreter**

| Commit | Change |
|---|---|
| `9596e62` | lexer + parser + AST dump (no eval) |
| `ae7c48a` | primitives, arithmetic, comparison, control flow, `console.log` |
| `1211429` | functions, return, lexical scope, closures |
| `6cca428` | object & array literals with property access |

**Phase 2 — DOM bindings**

| Commit | Change |
|---|---|
| `a32f37a` | `window`, `document`, `getElementById`, read `textContent`/`innerText` |
| `e687f0f` | set `textContent` (innerHTML downscoped to text) |
| `8ae707d` | `createElement`, `appendChild`, `remove` |
| `2afed43` | `getAttribute`, `setAttribute`, `element.style` proxy |
| `1ca6105` | `querySelector` (#id, tag) and `location.href` |

`<script>` text is queued by `parser.cc` during tree build and run after
`build_render_tree()` (DOMContentLoaded-equivalent). After the queue
drains, if any DOM mutation fired `dom_dirty`, the browser re-runs
`style_resolve_all()` + `build_render_tree()` + `run_layout()` once.

---

## Networking & TLS fixes

Independent track on the same branch. Uncovered while making the
browser actually load real-world HTTPS pages.

### `46938b6` — TCP recv overflow on big TLS handshakes

`kernel/socket.h`, `kernel/tcp.c`. The recv backlog could not absorb a
multi-segment TLS server-hello (RSA-4096 cert chains push past the
old buffer). Fix bumps the per-socket RX cap and lifts the early
drop in the TCP segment handler. Pages like `iana.org` no longer
truncate.

### `d49d842` — rtl8139 WRAP mode in RCR

`kernel/rtl8139.c`. Without WRAP the chip silently corrupts incoming
TCP payloads when an Ethernet frame straddles the ring's tail; the
all-zero Poly1305 tag in the trace was the smoking gun. RCR is now
programmed as `0x8F` (`WRAP | AB | AM | APM | AAP`).

### `c601178` — TLS handshake reader carry buffer

`kernel/tls/tls_handshake.c`. The carry between successive
`tls_read_record` calls used a 4 KB scratch; bumped to
`TLS_REC_MAX_PLAINTEXT` (16 KB) so a single TLS record never
overflows the carry. Combined with the rtl8139 fix, RSA-4096
handshakes against `iana.org` complete cleanly.

### `ccfdc39` — TLS app-data plaintext spillover

`kernel/tls/tls_ctx.h`, `kernel/tls/tls_handshake.c`. When a TLS record
yields more plaintext than the caller's buffer, the remainder is now
parked in `tls_ctx` and replayed on the next `recv` instead of being
dropped. `www.iana.org`'s 6142-byte HTML body flows through `curl`
and the browser without truncation.

### `f312d57` — RSA PSS verify

`kernel/tls/rsa.c`. A non-RFC strict check on the top bits of the DB
masked our verify path against well-formed signatures from real
servers; dropped the check and just mask per RFC.

### `a7f71ef` — revert TEMP debug instrumentation

Backs out the noisy `b49dfd6` debug prints once the underlying issues
were fixed.

### `039248c` — DNS retry + longer timeout for cold lookups

`kernel/dns.c`. Single 3-second poll was too short for cold recursive
resolution (root → TLD → auth) via the SLIRP/upstream forwarder, so
non-cached hostnames like `frankhagan.online` failed even though the
resolver itself worked. Now: up to 3 attempts (5 s + 5 s + 8 s) with
a fresh ephemeral socket each, qid-filtered receive that ignores stale
replies, 1500-byte response buffer (was 512), TC-bit check, and
`serial_printf` at every failure path naming the cause (no DNS
configured, RCODE, zero answers, encode failure, no A record).

### `fb87e8e` — broader `signature_algorithms_cert` for ECDSA chains

`kernel/tls/tls_handshake.c`. Modern Let's Encrypt issues ECDSA leaf
certs signed by a P-384 intermediate using `ecdsa_secp384r1_sha384`.
Old `signature_algorithms_cert` listed only `ecdsa_secp256r1_sha256`
and `rsa_pkcs1_*`, so servers serving such chains saw "client can't
verify any chain I have" and returned `handshake_failure` (alert 40)
before sending `ServerHello`. Expanded to 10 schemes covering
RSA-PSS-{256,384,512}, ECDSA-{P256,P384,P521}, RSA-PKCS1-{256,384,512},
and ed25519. The existing lenient mode in `x509_chain.c:48-54`
accepts unknown sig algs as if verified, so chains we can't natively
check still complete the handshake (same trade-off the code already
takes for casual browsing). Also tagged every `TLS_ERR_PROTOCOL` site
in `parse_server_hello` and the `ServerHello` reader with
`serial_printf` so future `handshake_failure` events report the exact
reason instead of a bare `-5`.

---

## Userspace + file-dialog + DNS sweep (`c5dafc60`)

Three unrelated regressions reported together. Grouped into one commit
because each touch is small.

### `vfs_dirent_t` layout mismatch in userspace

`VFS_MAX_NAME` was bumped from 64 to 128 at some point, making the real
`vfs_dirent_t` 136 bytes (`name@0`, `size@128`, `type@132`). Five
userspace tools still declared `char ent[72]` and read `ent[68]` as the
type byte:

- `bin/help.cc:121,124` — the user-reported symptom: `help` did not list
  programs in `/home/bin`.
- `bin/ls.cc:21,24,46` — also reads size from the wrong offset.
- `bin/fm.cc:249,260,307,319,325` — type *and* size at the wrong
  offsets, in both the refresh loop and the recursive-delete path.
- `bin/grep.cc:102,114,116` — type at wrong offset, name capped at 63.
- `bin/find.cc:43,55` — same shape.

`vfs_readdir` writes 136 bytes into a 72-byte stack buffer → ~60 bytes
of stack overflow per entry. Short names appeared to "work" only
because `ent[68]` happens to land in the zero-padded portion of the
name field. Buffers widened to 136, type read from `ent[132]`, size
read from `ent[128..131]`. Literal offsets (no `sizeof`) per the
CupidC literal-array constraint.

### `/home/bin` auto-created at boot

`kernel/core/kernel.c` mkdir'd `/home` but not `/home/bin`, so on a
fresh disk the user-programs directory simply did not exist and
`help.cc`'s `vfs_open("/home/bin")` silently failed. Added a single
unconditional `vfs_mkdir("/home/bin")` after the disk-mount block —
runs whether `/home` resolved to homefs, the FAT16 fallback, or the
ramfs no-disk fallback.

### File-dialog modal key-input race

`file_dialog_open` (notepad's `File → Open` / `Save`) drained the global
keyboard ring directly via `keyboard_read_event`. The desktop
dispatcher (`kernel/gui/desktop.c`) drains the same ring and routes
events into the focused window's `key_queue` via `gui_handle_key`. Both
are concurrent consumers of the same global queue, so any character
the desktop won first ended up sitting in notepad's `key_queue` —
where it stayed until the dialog returned and notepad's main loop
drained it into the editor. User-visible symptom: typing `ls.cc` in
the Open dialog showed `ls` (or nothing) in the dialog input, and the
remaining characters appeared in the editor after the dialog closed.

`fdlg_run_window` now reads from `win->key_queue` directly using the
same packed `(scancode << 8) | char` format the producer writes, and
flushes the queue on exit so chars typed during the dialog cannot
replay into the host app. `fdlg_run_screen` (fullscreen, no host
window) is unchanged.

### DNS resolver — diagnostic logging on failure

`vaultwarden.omegadc.synology.com -> 0 answers` was reported. Inspection
of `kernel/network/dns.c` shows `encode_name`, `skip_name`, and the
answer-section loop all handle arbitrary label counts correctly under
manual trace, but the failure path has no visibility into the actual
server response. Plausible real causes (NODATA, CNAME-only answer,
SOA-only response, parser corner case) are indistinguishable without
the bytes on the wire.

Added a `dns_dump_bytes` helper and wired it into:

- The outgoing query, after `encode_name`, so QNAME encoding for the
  failing name is always visible.
- The `rcode != 0` branch — logs `rcode` plus `qdcount/ancount/nscount/arcount`
  and the first 96 response bytes.
- The `ancount == 0` branch — same shape.
- The "no A record" branch — first 96 bytes, plus each answer's
  `atype/rdlen` printed inside the loop so CNAME-only responses are
  obvious.

Next failure on this name will produce actionable bytes on the serial
console; the targeted fix (improved UX message, CNAME chasing, or
parser correction) follows from what those bytes show.

---

## Build / runtime glue

### `cc2ab21` — bind new gfx2d APIs in CupidC

`kernel/cupidc.c`. The browser is JIT-compiled at runtime; new kernel
symbols only become callable after an explicit `BIND` entry. Added:

- `gfx2d_glyph_advance(char, int)`
- `gfx2d_text_width_n(const char *, int, int)`
- `gfx2d_text_n(int, int, const char *, int, uint32_t, int)`

### CupidC dialect notes

The browser exposed CupidC parser limits not reachable by previous
programs. Future contributors editing `bin/browser/*.cc` should know:

| Limitation | Workaround |
|---|---|
| Array sizes must be integer literals — enums rejected | Use `1024` not `MAX_FOO` (still OK in loop bounds) |
| One variable per declaration — no `int a, b;` | Split into separate stmts |
| Long `else if` chains overflow the parser's recursion | Use flat `if (...) { return; }` blocks |
| Locals share a flat table — no shadowing | Hoist re-used names like `int c` |
| No forward function declarations | Drop them; rely on deferred cross-resolve |
| No comparison operators on doubles (`<`/`>`/`==`) | Cast to scaled int (`js_dcmp` / `js_dnz`) |
| No unary minus on doubles | `0.0 - x` |
| No mixed int+double args in a single call | Split into two functions |
| Backticks in `/* */` comments lex weird | Drop them; plain prose only |

Captured in commits `f7d1594` `c0d124b` `e9ee421` `f852e65` `02a1808`
`253b075` `af9ac94` `a02f398` `5fd7133` (Plan-2 era) and `a211888`
`8ab9c81` `34f4365` `0374735` `db1bba7` `9fb7c09` `d8777fa` `63ea8fe`
(this PR's JS engine work).

---

## Verification

1. **Build:** `make` from repo root completes with no errors. The
   browser, DOOM and audio binaries all rebuild.
2. **Boot:** `make run` boots into the desktop; FPS counter steady;
   no kernel panic.
3. **Browser open:** click the browser icon. Window opens at 600×420;
   address bar focuses on `Ctrl-L`.
4. **example.com:** loads as default URL. Renders with proper
   centred body, heading hierarchy, blue underlined link, and
   readable text wrapping at the proportional widths from C1/C2.
5. **JS smoke:** any page containing
   ```html
   <script>console.log("hello", 1 + 2);</script>
   ```
   produces `[js] hello 3` on the serial log.
6. **JS DOM smoke:**
   ```html
   <p id="t">old</p>
   <script>document.getElementById("t").textContent = "new";</script>
   ```
   should render "new" after the post-script reflow.
7. **TLS smoke:** `https://iana.org/` and `https://www.iana.org/`
   load and render without truncation. With either networking fix
   reverted these regress to "TLS handshake failed" or zero-length
   body (validates `46938b6` / `d49d842` / `c601178` / `ccfdc39`).
8. **Debug dump:** typing `about:dump` in the address bar (or
   pressing Ctrl-D on a loaded page) emits the render tree and
   per-node computed style on the host serial console.

---

## Adjacent branch content

The branch also carries the DOOM port, the Nuked-OPL3 audio mixer,
the FAT16/homefs build reshuffle, and a handful of kernel hardening
fixes. Those are independent and should be reviewed under their own
PRs, so this file keeps them to a mention only.

The AC97 scripting-surface work is no longer just adjacent content:
`89ea6b6a` adds PCM volume, getters, CupidC/CupidASM bindings, and
the `volume` utility, so it is summarized in the recent-commits
section and in the bindings section below.

---

## Cupid C / Cupid ASM bindings + quick-start guide

In addition to the browser work above, this branch lands a parity
pass on the script-language bindings and ships a top-level
quick-start guide.

### What's bound

**Cupid C** (`kernel/cupidc.c`, `cc_register_kernel_bindings`):

- *Already had* the full networking surface (NIC info, ARP/ICMP/UDP
  raw, DNS, byte-order, all 11 BSD socket calls including
  `setsockopt` for TLS upgrade). Verified during this pass — no
  additions needed.
- *Newly added*: AC97 driver control (`ac97_init`, `ac97_start`,
  `ac97_stop`, `ac97_set_master_volume`, `ac97_tsc_sleep_ms`),
  the MIDI/OPL3 synth surface (`midiopl_init`, `_reset`, `_feed`,
  `_render`, `_set_volume`), and the PCM mixer surface
  (`mixer_init`, `_play`, `_stop`, `_active`, `_set_volume`,
  `_fill`). The smoke-test entries (`ac97_smoke_sine` / `_sweep` /
  `_pan`, `opl_smoke`, `audiotest_all`) were already bound and are
  unchanged.

**Cupid ASM** (`kernel/as.c`, `as_register_kernel_bindings`):

- *Already had* full networking parity with Cupid C.
- *Newly added*: same 22 audio bindings as Cupid C, so a `.s`
  program can `call ac97_init` / `call midiopl_feed` / `call
  mixer_play` directly. Audio includes (`audio/ac97.h`,
  `audio/midiopl.h`, `audio/mixer.h`, `audio/opl_smoke.h`) and
  `notepad.h` were added to the include list.

### Quick-start guide

`/CUPIDOS.txt` (new) is a flat-text catalogue covering:

1. Two-line "what is CupidOS / what are the languages".
2. Hello-world in both languages.
3. Cheat-sheet of every binding added or affirmed in this pass —
   networking, audio, and a placeholder for the planned web-engine
   surface.
4. Recipes: DNS lookup + ping, HTTPS GET via `setsockopt(SOL_TLS)`,
   AC97 sine in both languages.
5. Pointers into `wiki/`.

### Wiki updates

- `wiki/CupidC-Language-Reference.md` — appended Networking (NIC,
  layer 2/3, sockets), Audio (AC97, MIDI/OPL3, mixer) sections to
  *Built-in Functions*, with an HTTPS GET example.
- `wiki/CupidASM-Assembler.md` — appended Audio binding tables and
  an `ac97_init`/`ac97_smoke_sine` example to *Kernel Bindings
  (JIT Mode)*.
- `wiki/Networking.md` — header now cross-links to the CupidC and
  CupidASM binding sections plus `CUPIDOS.txt`.

### Web engine — deferred to its own branch

The full `bin/browser/*.cc` → `kernel/browser/*.c` port (~7,800
LOC across 15 files, plus a `bweb_ctx` wrapper for ~70 globals,
plus 75 `bweb_*` entry points bound to both languages) is too
large to share a branch with the rendering work above. It will
land on its own branch with a 5-step rollout: URL+util+ctx →
HTTP → DOM+parser → CSS/style/layout/RT/paint → JS. `CUPIDOS.txt`
notes this explicitly so users aren't surprised.

### Stale-file note

`bin/cupidc.c` (1,913 lines) is an older partial copy of the
real binding registry in `kernel/cupidc.c` (3,778 lines) and is
not built by the Makefile (`grep -n cupidc.c Makefile` only finds
`kernel/cupidc.c`). It carries some pre-existing in-tree
modifications and a few additions made during this pass before
the duplication was caught — none of which affect the build.
Cleaning up the duplicate is out of scope for this PR.

---

## OS-wide TTF font system + `fontswitch` picker

Adds a TrueType rasterizer wired into every text-drawing primitive in
the kernel, plus a CupidC GUI program (`fontswitch`) that picks the
active face/size and persists the choice to `/etc/font.conf`.

### Layout

```
kernel/gfx/
├── ttf.{c,h}            TrueType parser (head, hhea, maxp, OS/2,
│                        name, cmap fmt 4 + 12, loca, glyf, hmtx;
│                        composite glyph recursion w/ depth cap 4)
├── glyph_raster.{c,h}   Outline → 8-bit alpha bitmap (4x4
│                        supersampled, non-zero winding fill,
│                        quadratic Bézier midpoint subdivision)
└── fontsys.{c,h}        Face registry + LRU glyph cache (4 MB) +
                         CSS-shaped match (family list, weight,
                         italic, generics) + alpha-blend draw with
                         synthetic bold/italic
system/fonts/
├── LiberationSans-Regular.ttf   (bundled)
├── LiberationSerif-Regular.ttf  (bundled)
├── LiberationMono-Regular.ttf   (bundled)
└── LICENSE.liberation
bin/fontswitch.cc        GUI picker (list + preview + size ± + Apply)
```

Bundled TTFs are embedded into the kernel ELF via
`objcopy -I binary` (Makefile pattern at `system/fonts/%.ttf.o`),
exposing `_binary_system_fonts_<face>_ttf_{start,end}` symbols that
`fontsys_init()` registers at boot. No filesystem dependency for the
default fonts.

### Engine, modeled on Lexbor + stb_truetype

`fontsys`'s public surface mirrors the Lexbor CSS property layer
(`LXB_CSS_FONT_FAMILY_*` enum ordering, generic family fallback
table, comma-separated family lists, alias resolution for
Arial / Times / Courier → metric-compatible Liberation faces). The
parser/rasterizer follow stb_truetype's structure: integer outline
points + per-contour endpoints, recursive flatten on quadratic
Béziers, scanline non-zero-winding sweep with subpixel coverage. No
hinting interpreter, no kerning, no GPOS — out of scope for v1.

### OS-wide gating in gfx2d / graphics

`fontsys_set_os_default(face_id, size_px)` flips an OS-wide pair.
`face_id == -1` keeps the existing 8x8 bitmap path. The five primitive
seams that 339 callsites flow through were patched to consult the
default before rendering:

- `kernel/gfx/gfx2d.c::g2d_draw_char` — TTF glyph + alpha blit when
  active, else the original 8x8 path.
- `kernel/gfx/gfx2d.c::gfx2d_text` — proportional pen advance when
  TTF active (fixed-grid stepping was wrong for proportional faces).
- `kernel/gfx/gfx2d.c::gfx2d_text_width` — `fontsys_run_width`.
- `kernel/gfx/gfx2d.c::gfx2d_glyph_advance` — `fontsys_advance`
  (fast hmtx-only path; no rasterization).
- `kernel/gfx/gfx2d.c::gfx2d_text_height` — `fontsys_line_height`.
- `kernel/gfx/graphics.c::gfx_draw_char` / `gfx_draw_text` /
  `gfx_text_width` — forward to the gfx2d-side gates.

Bitmap fallback is preserved verbatim for `face_id == -1` and for
glyphs the rasterizer can't produce (kept as a safety net under
memory pressure).

### Persistence

`/etc/font.conf` (two lines: `family=<name|__bitmap__>` +
`size=<px>`) is read once at boot from `kernel/core/kernel.c`
immediately after `fontsys_init()`. First boot has no conf, so the
default stays bitmap (opt-in policy).

`fontsys_register_file()` (previously a stub) now loads a TTF blob
from VFS via `vfs_stat` + `vfs_read_all`, takes ownership, and
registers it — opens the door to user-supplied fonts.

### CupidC bindings

13 new BIND() rows in both `kernel/lang/cupidc.c` (JIT path used by
`fontswitch`) and `bin/cupidc.c` (ELF path):

```
fontsys_match              fontsys_face_count
fontsys_set_os_default     fontsys_face_family
fontsys_get_os_default_face fontsys_face_weight
fontsys_get_os_default_size fontsys_face_italic
fontsys_register_file      fontsys_run_width
fontsys_draw_run_styled    fontsys_ascent
fontsys_line_height
```

### `fontswitch` GUI

Modeled on `bin/paint.cc` (mouse-poll loop, `gfx2d_app_toolbar` close
button). Lists the registered TTF faces plus an "8x8 bitmap (revert)"
sentinel row, draws a live preview at the chosen size by temp-flipping
the OS default through `gfx2d_text`, and on Apply commits both
`fontsys_set_os_default` (live flip) and `vfs_write_text("/etc/font.conf",
…)` (next-boot persistence). Clicking the bitmap row + Apply reverts
the system to the 8x8 path.

### libm-ABI bug fix

`kernel/cpu/libm.h` documents that `floor` / `ceil` (and the rest)
use a CupidC-internal calling convention — return value in `xmm0`,
not `ST(0)` as the i386 SysV ABI dictates. Plain kernel C callers
get garbage. `glyph_rasterize` was a plain-C caller, so its bbox
math (`int ix0 = (int)floor(pxmin); …`) produced w=0/h=0 and every
TTF glyph came back as an empty bitmap.

Replaced with two local helpers in `kernel/gfx/glyph_raster.c`:

```c
static inline int float_floor_int(float v) {
    int i = (int)v;
    if (v < 0.0f && (float)i != v) i--;
    return i;
}
static inline int float_ceil_int(float v) {
    int i = (int)v;
    if (v > 0.0f && (float)i != v) i++;
    return i;
}
```

Used at all four call sites in the rasterizer (bbox + per-row
crossing-edge clamps). No libm dependency anymore.

### Verification

- `make clean && make` builds clean (only pre-existing linker
  warnings about RWX permissions).
- All new symbols present in `kernel.elf`: `fontsys_match`,
  `fontsys_set_os_default`, `fontsys_advance`,
  `fontsys_load_os_default_from_conf`, `fontsys_register_file`.
- Boot path: `fontsys_init()` registers the three bundled
  Liberation TTFs and logs `[fontsys] init done, 3 faces`. With no
  `/etc/font.conf` the kernel logs `fontsys: no /etc/font.conf,
  bitmap default`.
- `fontswitch`: clicking Liberation Sans + size 14 + Apply renders
  the preview line in anti-aliased TTF and writes
  `family=Liberation Sans\nsize=14\n` to `/etc/font.conf`. Clicking
  the bitmap sentinel + Apply restores the 8x8 path.
- Browser (`bin/browser/paint.cc`) was already a `fontsys` consumer
  before this PR; its rendering path is unchanged.

---

## Source-tree refactor: kernel/ split into per-subsystem subdirs

After the bindings work above, the formerly-flat `kernel/` (~200
files) was reorganised by responsibility. The build is verified
clean (`make clean && make` produces the kernel ELF and
`cupidos.img`).

### New layout

```
kernel/
├── audio/      (existing)
├── core/       kmain, panic, process, scheduler, syscall,
│               app_launch, types, debug, ports, string,
│               context_switch.asm
├── cpu/        IDT, IRQ, PIC, FPU, libm, math, simd, ksyms,
│               isr.asm
├── crypto/     AES, ChaCha20, SHA, HMAC, HKDF, RSA, x25519,
│               P-256, ECDSA, ASN.1, X.509, csprng, ct, bigint
├── doom/       (existing)
├── fs/         VFS, FAT16, ISO9660, ramfs, devfs, homefs,
│               loopdev, blockcache, blockdev, fs.c
├── gfx/        gfx2d, BMP/PNG/JPEG, font_8x8, graphics
├── gui/        gui_*, desktop, ui, clipboard, notepad, ed,
│               terminal_app, ansi, terminal_ansi
├── lang/       cupidc*, as*, cupidscript*, dis, shell, exec,
│               godspeak
├── mm/         memory, paging, swap, swap_disk
├── network/    arp, dhcp, dns, icmp, ip, net_if, socket, tcp,
│               udp
├── smp/        smp, mp_tables, lapic, ioapic, percpu, bkl,
│               acpi, smp_trampoline.S
├── tls/        tls_record, tls_kdf, tls_ctx, tls_handshake,
│               tls12_handshake, tls_selftest, tls_ca_bundle
├── usb/        usb, uhci, ehci, usb_hid, usb_hub, usb_msc
└── util/       calendar, generated *_programs_gen.c

drivers/        ata, keyboard, mouse, pit, rtc, serial, speaker,
                timer, vga, pci (moved from kernel/),
                rtl8139 (moved), e1000 (moved)
```

The crypto primitives (AES, ChaCha20, SHA, HMAC, HKDF, RSA,
x25519, P-256, ECDSA, ASN.1, X.509, etc.) split out of
`kernel/tls/` into the new `kernel/crypto/`. The TLS protocol
files (record, KDF, ctx, handshake, 1.2 handshake, selftest, CA
bundle) stay in `kernel/tls/`. The PCI enumerator and the two
NIC chipset drivers (`rtl8139`, `e1000`) move from `kernel/` to
`drivers/` next to ATA / keyboard / mouse / serial / VGA, since
they are hardware drivers rather than protocol code.

### Mechanics

- Files moved with `git mv` so blame and history follow the new
  paths.
- All `#include "../X"` and `#include "../../X"` forms in
  `kernel/` and `drivers/` rewritten to bare `#include "X"`. A
  new `$(KERNEL_INCLUDES)` Makefile variable adds `-I` for every
  kernel subdir plus `drivers/`, so bare includes resolve from
  any source-file location and cross-subsystem header references
  don't need a path prefix.
- ~340 path references in `Makefile` rewritten via a generated
  sed mapping (each stem → its new subdir): `KERNEL_OBJS`, every
  per-file build rule, the `mksyms` invocation, the linker
  command line, and the `CFLAGS_DOOM` include list.
- `clean` target updated to remove `.o` files from every kernel
  subdir, plus the new generated-source paths
  (`kernel/util/*_programs_gen.c`, `kernel/cpu/ksyms_data.c`,
  `kernel/smp/smp_trampoline.bin`).
- `.gitignore` repointed for the same generated paths.

### Documentation

- `README.md` — "Project layout" section now shows the subdir
  tree with one-line responsibilities per subdir.
- `wiki/Architecture.md` — new "Source-tree layout" section with
  the same tree plus a module dependency diagram (top depends on
  bottom, no cycles): `gui → gfx, lang, fs, mm, core` /
  `lang → fs, mm, core, cpu` / `network → core, drivers` /
  `tls → network, crypto, core` / `core → (nothing)`.
- All other wiki pages had their `kernel/foo.c` references
  bulk-rewritten to `kernel/<subdir>/foo.c` so navigation still
  works after the move.

---

## Render pipeline upgrades + `@font-face` web fonts

Builds on the Browser stack above. Adds the missing pieces that turn
"renders most pages OK" into "renders pages with the right
typography": webfonts downloaded over HTTPS at runtime, external
stylesheets, rounded corners / drop shadows / overflow clipping, and
Blink-style line-break punctuation rules.

References the three vendored upstream trees as design sources:
`lexbor/`, `blink/Source/`, `mozjs-45.0.2/`.

### `bin/browser/font_face.cc` — webfont registry

New 310-LOC module. Public API:

```c
void font_face_init(void);
int  font_face_add_rule(const char *family, int family_len,
                        const char *src_url, int src_url_len,
                        int weight, int italic);
int  font_face_match(const char *family, int family_len,
                     int weight, int italic);
void font_face_pump(void);
int  font_face_any_state_changed(void);
void font_face_state_clear(void);
int  font_face_count(void);
```

Modeled on `blink/Source/core/css/RemoteFontFaceSource.cpp`,
`CSSFontFace.cpp`, `CSSFontSelector.cpp`. State machine per slot:
`PENDING → LOADED | FAILED | SKIPPED`. 4-slot registry, URL-dedup
cache. Webfont blobs are `kmalloc`-allocated when the font arrives
(NOT static — CupidC's data section is capped at 4 MiB and the
existing browser globals already use most of it). 1 MiB hard cap per
font. fontsys keeps the pointer (`take_ownership=0`); blobs leak
across page navigations by design (`fontsys_unregister` doesn't
exist yet).

### Async fetch + FOUT reflow

Per user choice between sync (page hangs while font downloads) and
async (FOUT). Implementation:

- `font_face_add_rule` from CSS parser just records the slot with
  state `PENDING`. No fetch.
- `font_face_pump()` is called once per main-loop iteration (after
  `render()` in `browser_main`'s loop). It advances at most one
  PENDING slot per tick: saves cur_host/cur_port/cur_path/page_buf
  globals, calls `fetch_url`, copies bytes into a fresh kmalloc
  buffer, calls `fontsys_register_blob`, restores globals. State
  transitions to LOADED, sets `ff_state_dirty=1`.
- Main loop checks `font_face_any_state_changed()`. On dirty:
  re-runs `style_resolve_all → build_render_tree → run_layout`, then
  clears the flag. Next paint picks up the new face. Reference:
  `blink/Source/core/css/CSSFontSelector.cpp::fontFaceInvalidated`.

### `@font-face` CSS parser

`bin/browser/css.cc` gets:

1. **At-rule dispatcher**: top-level `@`-token branch in
   `css_parse_block` (modeled on
   `blink/Source/core/css/parser/CSSAtRuleID.cpp`). Unknown at-rules
   skip to the matching `}` so following selectors keep parsing.
   `@font-face` routes to a real handler; `@media` / `@import` /
   `@keyframes` skip silently.
2. **`css_at_font_face` handler**: parses `font-family`, `src`,
   `font-weight`, `font-style` descriptors. The `src` list is walked
   left-to-right; the first `format("truetype")` /
   `format("opentype")` / bare `url()` entry wins. WOFF/WOFF2 are
   skipped with a serial warning (no Brotli/zlib decompressor in the
   browser yet). On success, `font_face_add_rule` records a PENDING
   slot.
3. **Resolution hook**: `bin/browser/layout.cc::cs_face_id_for`
   consults `font_face_match` BEFORE falling back to the kernel's
   `fontsys_match`. `font_face_match` walks comma-separated family
   lists, applies a simplified CSS weight-matching algorithm
   (exact → 400 below 500, 700 above; italic exact preferred).

### External stylesheets — `<link rel="stylesheet">`

Most real sites declare `@font-face` in external CSS. Without
`<link>` support the entire pipeline is dead.

- `parser.cc`: `T_LINK` added to the tag enum and `is_void_tag` list.
- `parser.cc::parse_html`: just before `populate_sibling_caches()` /
  `style_resolve_all()`, calls `fetch_external_stylesheets()`. The
  injection point matters — DOM is fully built and all text is
  interned in `attr_pool` (text tokens no longer reference
  `page_buf`), so reusing `page_buf` for stylesheet fetches is safe.
- `nav.cc::fetch_external_stylesheets`: walks the DOM, finds every
  `<link rel="stylesheet" href="…">`, resolves relative URLs via
  the existing `compute_url_relative`, fetches, feeds the body
  straight into `css_parse_block`. Cap of 16 sheets per page.
  Cur-state globals are saved/restored around each fetch.

Cascade caveat (documented inline): external rules are appended
AFTER any inline `<style>` rules emitted during tree-build, so a
`<style>` preceding a `<link>` in the source out-ranks it on
doc-order ties. Real-world pages put `<link>` first; acceptable v1
limitation.

### Visual-quality CSS additions

| Property | Status |
|---|---|
| `border-radius: <px>` | uniform corners; clamps to `min(w,h)/2`; uses `gfx2d_rect_round_fill` for bg, `gfx2d_rect_round` for border outline |
| `box-shadow: <dx> <dy> [<color>]` | offset rect blits, no blur, no spread (Blink supports both via `CSSShadowValue` — out of scope here, no Gaussian blur path in `gfx2d`) |
| `overflow: hidden` | userland clip stack in `paint.cc` (`paint_clip_push` / `paint_clip_pop`); intersects with parent clip and pushes to `gfx2d_clip_set` |

`paint_rt_node` was split into `paint_rt_box_decoration` (shadow →
background → border, in Blink BoxPainter order) and
`paint_rt_content` (text / replaced / marker / line-box). When a
`border-radius` is set, the rect-fill paths transparently switch to
the rounded variants.

### Blink line-break punctuation rules

Adopted from `blink/Source/core/rendering/break_lines.cpp`. Two
helpers in `layout.cc`:

```c
int can_break_before(char c) {  /* no break before . , ; : ! ? ) ] } ' " */
int can_break_after (char c) {  /* no break after  ( [ { $ # @ */
```

Inside `emit_text_atoms`, the inter-word soft-break sentinel
(`la_x = -3`) flips to non-break (`la_x = -1`) when surrounding
characters forbid wrapping. `prev_last_char` tracks the last char of
the previous word for the after-check; the next char is read from
`attr_pool` for the before-check. Pre/nowrap semantics unchanged.

### Kernel-side: CupidC binding for `fontsys_register_blob`

`kernel/lang/cupidc.c`. The browser is JIT-compiled at runtime;
new kernel symbols only become callable after an explicit `BIND`.
`fontsys_register_blob` was already declared in `fontsys.h` and
implemented in `fontsys.c` (used by the kernel internally to register
the bundled Liberation TTFs at boot) but had no userland binding.

```c
int (*p_fontsys_register_blob)(const char *, int, int) = fontsys_register_blob;
BIND_T("fontsys_register_blob", p_fontsys_register_blob, 3, TYPE_INT);
```

### CupidC dialect notes (new ones found)

| Limitation | Workaround |
|---|---|
| C-comment lexer does NOT recognise nested `*/` inside strings of asterisks/slashes — `/* … cs_*/rt_* */` ends the comment after `cs_*` | Avoid `*/` patterns inside comment prose; rewrite `cs_*/rt_*/la_*` as `cs/rt/la` |
| Static array data counts against `CC_MAX_DATA = 4 MiB`; uninitialized globals included | Use `(char*)kmalloc(N)` for big buffers; CupidC supports pointer indexing on `char *` heap allocations |
| Forward references to functions defined later in the unity-include order produce `Unresolved symbol` warnings during parse but resolve at JIT-link time — only `undefined variable` (variable, not function) is fatal | Ignore the warnings unless they fire on a name you actually misspelt |

### Files

| File | Δ | Purpose |
|---|---|---|
| `bin/browser/font_face.cc` | new (310) | webfont registry + async pump |
| `bin/browser/css.cc` | +205 | at-rule dispatcher, `@font-face` parser, three new property recognizers |
| `bin/browser/style.cc` | +109 | appliers + UA defaults for new properties |
| `bin/browser/paint.cc` | +145 net | clip stack, decoration/content split, rounded rect, shadow |
| `bin/browser/layout.cc` | +115 | `font_face_match` hook, break-rule helpers |
| `bin/browser/main.cc` | +52 | enum tags, SoA arrays, async pump in main loop |
| `bin/browser/nav.cc` | +77 | external-link fetcher, per-page `font_face_init` |
| `bin/browser/parser.cc` | +9 | `T_LINK`, void-tag list, fetch trigger |
| `bin/browser.cc` | +1 | include `font_face.cc` |
| `kernel/lang/cupidc.c` | +3 | bind `fontsys_register_blob` |
| `tests/browser/d1..d8` | 9 new pages | test fixtures (basic webfont, weights, woff2 skip, external link, border-radius, box-shadow, overflow:hidden, line-break punctuation) |

### Deferred (require deeper layout refactor)

- Inline-block shrink-to-fit (CSS 2.1 §10.3.9) — needs a
  min-content / max-content two-pass measurement.
- Grandparent margin propagation — needs Blink's `MarginInfo`
  bubble-up out-param threaded through `layout_block`.
- WOFF / WOFF2 — needs Brotli + zlib decompressors.
- `font-display: swap | fallback | optional` — always FOUT for now.
- Persistent `/tmp/fontcache/<sha1>.ttf` cache.

### Verification

1. `make` builds clean.
2. `make run` boots; click Browser. With a host HTTP server
   (`python3 -m http.server 80` from `tests/browser/`), navigate
   to e.g. `http://10.0.2.2/d1_font_face_basic.html`.
3. `d1`: paragraph rendered in Liberation Sans served as a
   "remote" webfont; fallback paragraph uses the OS default.
4. `d2`: 400 vs 700 weight match against two `@font-face` rules.
5. `d3`: WOFF2 entry skipped with serial warning, TTF entry used.
6. `d4`: external `<link>` rules apply (red bold text).
7. `d5`–`d7`: border-radius / box-shadow / overflow:hidden render.
8. `d8`: long paragraph with parentheses and commas — no
   wrap-before-`)` / wrap-before-`,` events.
9. `about:dump` on any loaded page emits the render tree with the
   new `cs_*` properties (border-radius, overflow, shadow) on the
   serial console.

---

## Render correctness pass — d1_selectors_v2 polish

A debug-driven cleanup pass against `tests/browser/d1_selectors_v2.html`
side-by-side with Chrome. Every fix is anchored in a misbehaviour the
test page exposed; the page now renders close to Chrome on selectors,
combinators, list markers, generated content, form controls, and inline
spacing.

### Parser — implicit-close walks no longer pop `<body>`

`bin/browser/parser.cc::is_block_tag`. The walks for `<p>`, `<li>`,
`<dt>` / `<dd>`, `<tr>`, `<td>` / `<th>` pop ancestors until they hit
their own tag or `is_block_tag`. `T_BODY` / `T_HTML` / `T_ROOT` were
not classified as block, so opening a `<p>` with body on the stack
walked PAST body, popped it, and reparented every later element under
root. `prev_sibling_elt` then placed each `<p>` as a sibling of body
instead of a child, and `h2 + p` / `h2 ~ p.note` silently failed to
match. Adding the three scope-anchor tags to `is_block_tag` makes them
hard stops for every implicit-close walk. Single highest-impact fix in
the pass.

### Layout / paint — viewport y double-count

`bin/browser/layout.cc::run_layout`. Root was seeded with
`rt_y[root] = viewport_y()`, but `rt_screen_y` already adds
`viewport_y()` at the end of its parent-chain sum, so the document
shifted ~25 px below where Chrome paints it (the "huge gap before the
first h2"). Root now sits at document origin `(0, 0)`; chrome offset
is added once at screen-conversion time.

### Paint — replaced atom y/w/h plumbing

`bin/browser/paint.cc::paint_rt_line_box` and
`bin/browser/layout.cc::collect_inline_atoms`. Two bugs that both
made `<input>` controls invisible/clickless:

1. `rt_y[child] = sy - rt_screen_y(parent) + viewport_y - scroll_y`
   double-counted the chrome offset for replaced-element y, pushing
   the `<input>` ~25 px below its line. Replaced atom y is now
   `sy - rt_screen_y(parent)` exactly (chrome added by
   `rt_screen_y` of the child later).
2. `rt_w[n]` / `rt_h[n]` for replaced atoms were never written by
   layout — only by paint, after `paint_rt_box_decoration` had
   already returned with `w = h = 0`. `collect_inline_atoms` now
   stamps `rt_w/rt_h` from `rt_intrinsic_w/h`; `paint_rt_line_box`
   keeps the same write so a click between paints sees a real hit
   box.

### Paint — `RT_REPLACED` in box-decoration

`bin/browser/paint.cc::paint_rt_box_decoration`. The function returned
early for any kind not in `{BLOCK, INLINE_BLOCK, LIST_ITEM,
TABLE_CELL}` — so author CSS borders / backgrounds on `<input>`,
`<img>`, `<button>`, `<textarea>` never painted. With
`input[type="checkbox"] { border: 2px solid #c00 }` from the test, the
red stroke was missing and `paint_rt_replaced` then filled white over
the whole box, producing an invisible white-on-white control. Added
`RT_REPLACED` to the `paints_box` set; the white interior fill in
`paint_rt_replaced` insets by 1 px when an author border is present so
the stroke survives.

### Paint — list markers via fontsys

`bin/browser/paint.cc::paint_rt_marker`. `gfx2d_text` is the bitmap
path and treats each byte as a glyph index; passing the multi-byte
UTF-8 bullet "\xE2\x80\xA2" produced three garbage cells (often
invisible). Marker rendering routes through
`fontsys_draw_run_styled` with the parent `<li>`'s
`cs_face_id_for(cs)` and `cs_font_size_px`, so the TTF cmap looks up
the real `•` / `◦` / `■`. Baseline aligned to first-line baseline via
`fontsys_ascent` (matches the formula in `paint_rt_line_box`).

### Render-tree — initialise geometry on alloc

`bin/browser/render_tree.cc::rt_alloc`. `rt_x`, `rt_y`, `rt_w`, `rt_h`,
`rt_intrinsic_w`, `rt_intrinsic_h` were left uninitialised; layout
writes them for blocks but skips RT_LIST_MARKER (`layout_block:567-569`),
so the first marker on a fresh page reused stale memory from the
previous render. Symptom on the directory-listing screenshot: the
first list bullet jumped right of the text while the rest looked
correct. `rt_alloc` now zeroes all six geometry fields; intrinsic dims
are set immediately afterward by callers that need them.

### Inline backgrounds inherited through `<a>`

`bin/browser/layout.cc::emit_text_atoms`. CSS `background-color` is
non-inherited, so `a[href*="example"] { background:#eef }` set
`cs_bg` on the `<a>` but the text-node child kept `cs_bg = -1`. Atom
emission now walks RT_INLINE ancestors (stopping at the first
non-inline kind) and adopts the closest inline ancestor's `cs_bg`,
matching Blink's per-line-fragment background paint. Block ancestors
keep their own `paint_rt_box_decoration` path (no double-paint).

### Default line-height proportional to px

`bin/browser/layout.cc::effective_line_h`. The bitmap-tier fallback
returned 12 px for tier 0 (`px ≤ 14`), so 14 px TTF text in
consecutive `<li>` items overlapped. When CSS doesn't set
`line-height` but the cs has a px-resolved font size, we now use
`cs_font_size_px * 1.2` (clamped to at least the tier value).

### text-decoration line-through

`bin/browser/layout.cc::emit_text_atoms` and
`bin/browser/paint.cc::paint_rt_line_box`. `la_underline` was a bool;
expanded to carry the full `cs_text_dec` bitmask. Paint draws underline
at `baseline + 2` and line-through at `baseline - asc/2` (or the
bitmap-path equivalents). `.menu li.disabled { text-decoration:
line-through }` now strikes through correctly.

### UTF-8 end-to-end

`bin/browser/dom.cc`, `bin/browser/style.cc`,
`kernel/gfx/fontsys.c`. Three places previously folded high
codepoints to ASCII placeholders, defeating the TTF cmap:

- `decode_entities` (HTML body text) emitted ASCII via
  `map_high_codepoint`; now emits raw UTF-8 via the new
  `emit_utf8_codepoint` helper. `·`, `—`, `…` etc. flow through to
  `attr_pool` as bytes.
- `css_value_string` (CSS string-value escapes) decoded
  `\HHHHHH` to one ASCII char; now emits UTF-8. `q::before {
  content: "\201C" }` produces a real left double-quotation mark.
- `kernel/gfx/fontsys.c::fontsys_run_width` and
  `fontsys_draw_run_styled` decoded one byte per iteration. Now
  call a `fontsys_utf8_decode` helper (1–4 byte lead-byte classes,
  malformed → Latin-1 fallback) so multi-byte input maps to the
  right cmap glyph.

Side effect: list bullet, curly quotes, en/em dashes, ellipsis all
render via TTF instead of an ASCII approximation.

### Status-string null termination

`bin/browser/net.cc`. `b_append_int(status_msg, sl, status)` returned
the new length, but the caller hardcoded `status_msg[sl + 4] = 0;` —
correct for "404" only when `sl + 3` was the digit count, leaving a
leftover `c` after `404` (the visible "HTTP error: 404c" in the bottom
bar). Now uses the helper's return value for the null position.

### Italic — synthesised obliques

`kernel/gfx/fontsys.c::fontsys_draw_run_styled` and `blit_glyph`. No
`LiberationSans-Italic.ttf` is bundled, so fontsys synthesises italic
via row-shear. Two bugs:

1. The 12 % slope was visually flat at body sizes — a 14 px glyph
   leaned barely 1 px and read as upright. Bumped to 17 %.
2. The shear shifted every row right of the glyph axis, drifting the
   whole glyph right and clipping into the next cell while ALSO
   reading as letter-spaced. Glyph is now pre-shifted left by half
   the max shear so the slant straddles the axis. Per-glyph advance
   stays the regular hmtx value (matches Chrome's synthesis behaviour
   for fonts without an Italic face). To replace with a real italic
   face, drop a `LiberationSans-Italic.ttf` into `system/fonts/`;
   `fontsys_match` will pick it via the existing weight/italic table.

`fontsys_italic_extra(size_px)` is exported (with a CupidC binding) for
callers that need the shear width for ink-bounds clipping; layout
itself doesn't use it.

### Form controls — checkbox + text-input interactivity

`bin/browser/main.cc`, `bin/browser/parser.cc`,
`bin/browser/paint.cc`, `bin/browser/input.cc`,
`bin/browser/render_tree.cc`. The DOM has no attr-set helper, so
toggle state lives in a parallel `n_checkbox_state[4096]` array seeded
from the initial `checked` HTML attribute and mutated on click.
`paint_rt_replaced` reads it instead of re-querying `dom_attr_str`.
Click routing:

- `hit_box` returns the `RT_LINE_BOX` covering the row, not the
  replaced atom underneath. New `line_box_replaced_at(line_box,
  rel_ax)` walks the line's atoms; if the click column falls on an
  atom whose `la_text_off < 0` (encoded RT-node ref), the click
  redirects to that node before the parent walk. Without this, every
  input/checkbox click stopped at the line_box and walked straight
  up through the block parent.
- The parent walk now recognises `<input type="checkbox|radio">`
  and toggles `n_checkbox_state[dom]`. Radio clears every other
  radio with the same `name` attribute. Text inputs still hit the
  existing `rt_input_idx` / `FOCUS_INPUT` path, with a 1 px blue
  inner ring + a vertical caret rendered at the end of the value
  when focused.

Intrinsic dimensions tightened so controls read at body text height:
checkbox 14×14 → 16×16; text input 120×16 → 140×18.

### Diagnostic tooling

Two temporary `serial_printf` traces were added to localise the
selector/sibling bug without a debugger and removed once the parser
fix landed. Documented here so the same pattern can be reused next
time:

- `css_emit_rule` logged any rule whose tail compound was
  `COMB_ADJACENT` or `COMB_GEN_SIBLING`, dumping
  `(rule#, prop, sel range, tail tag/class, comb, head tag)`.
- `sel_chain_matches` logged every match attempt against `<p>` for
  sibling rules, dumping `(node, tag, class_off, sel range, tail
  tag/cls/comb, tail_ok, prev sibling, walk result)`. Read out as
  `prev=7(tag=4)` — i.e. the previous element sibling of a `<p>`
  was body itself — pinpointed the parser bug to within five
  minutes.

### Files

| File | Purpose |
|---|---|
| `bin/browser/parser.cc` | scope anchors in `is_block_tag`; `n_checkbox_state` reset + seed |
| `bin/browser/layout.cc` | root y=0; replaced atom rt_w/h; inline-bg walk; px-based default line-height; line-through bitmask |
| `bin/browser/paint.cc` | `RT_REPLACED` in box decoration; replaced atom y/w/h plumbing; bullet via fontsys; inset white fill; focus ring + caret; line-through |
| `bin/browser/render_tree.cc` | zero geometry in `rt_alloc`; checkbox 16×16, text input 140×18 |
| `bin/browser/dom.cc` | `emit_utf8_codepoint`; UTF-8 emit in `decode_entities` |
| `bin/browser/style.cc` | UTF-8 emit for `\HHHHHH` escapes in `css_value_string` |
| `bin/browser/main.cc` | `n_checkbox_state[4096]` |
| `bin/browser/input.cc` | `line_box_replaced_at`; checkbox/radio toggle on click |
| `bin/browser/net.cc` | `status_msg` null-term off-by-one |
| `kernel/gfx/fontsys.c` | UTF-8 decode in run_width / draw_run_styled; italic shear half-axis pre-shift; `fontsys_italic_extra` |
| `kernel/gfx/fontsys.h` | export `fontsys_italic_extra` |
| `kernel/lang/cupidc.c` | bind `fontsys_italic_extra` |

### Verification

1. `make` — clean build.
2. `make run` — boot, click Browser, navigate
   `http://10.0.2.2:<port>/d1_selectors_v2.html` (HTTP server cwd in
   the repo root, so `/system/fonts/...` resolves).
3. `h2 + p` paragraph after "adjacent and sibling" renders gray and
   visibly italic; `h2 ~ p.note` paints the pale-yellow background +
   4 px padding.
4. `nav > a` is blue/bold; `nav span > a` is orange — child vs
   descendant combinator.
5. Attribute selectors apply: `https example` green, `PDF doc` bold,
   `https example` has the light-blue `#eef` inline background under
   the link text.
6. `<input type="text">` and `<input type="checkbox">` render on the
   same line with a `·` separator. Clicking the checkbox toggles the
   `x`. Clicking the text input draws a blue inner ring + caret;
   typing appends.
7. List bullets (`•`) render in TTF, baseline-aligned to first line.
8. `:not()` menu — disabled items gray with line-through.
9. `<q>hello world</q>` renders as `"hello world"` with real curly
   quotes (U+201C / U+201D) from Liberation Sans.

---

## CSS box model — content-box width/height resolution

`bin/browser/layout.cc:layout_block`. `cs_width` / `cs_height` were
treated as the BORDER box size: `.card { width:240px; padding:16px;
border:1px }` painted at 240 px outer and squeezed content to 206 px,
instead of Chrome's 274 px outer with the requested 240 px content.
The CSS 2.1 default is `box-sizing: content-box`, so `width` describes
the CONTENT box and the painted border box adds left+right (or
top+bottom) padding+border.

`layout_block` now resolves both axes that way: when `style_w >= 0` the
outer width is `style_w + padding-l + padding-r + border-l + border-r`,
and similarly for `style_h`. d5_border_radius's blue card now paints
at the expected 274×114 with rounded corners.

`box-sizing: border-box` is not honoured yet — pages that rely on it
will need to subtract padding/border from their declared `width` until
a `cs_box_sizing` field exists.

### Inline-block shrink-to-fit — deferred (CupidC JIT bug)

Pills declared `display: inline-block; width: auto` paint as
full-width bars stacked vertically because `layout_block` stretches
auto-width boxes to `avail`. The intended fix is shrink-to-fit:
pre-set `cs_width` to the measured max-content of the inline-block's
children before calling `layout_block`. Implementation attempts so far
all stack-overflow at JIT execute time:

- A separate `shrink_to_fit_inner` recursive function: blew the stack
  on first call.
- Folding the recursion into a single self-recursive function: same.
- Inlining the measurement loop into `collect_inline_atoms`'s
  inline-block branch: same.
- Reducing the loop to a single-text-child special case calling
  `text_slice_w_cs`: same.
- Replacing the call with an arithmetic estimate (`len * (size/2+1)`):
  same.
- A constant `cs_width = avail/4` (fresh local + divide): same.
- A literal `cs_width = 96` (no new computation): boots on
  example.com (no inline-block hit) but crashes on d5 (inline-block
  hit, identical 308721-byte JIT output).

The CupidC JIT appears to corrupt ESP when the inline-block branch of
`collect_inline_atoms` does anything beyond the original
`if (kind == RT_INLINE_BLOCK) layout_block(n, avail);` call. Same
source compiled to identical byte counts on consecutive runs produces
divergent crashes. Likely a JIT register-allocation or frame-setup
quirk specific to that branch's code shape — needs investigation in
`kernel/lang/cupidc.c` rather than another patch attempt here.

Workarounds available for authors hitting this limitation:
- Set `width` on the inline-block explicitly (avoids the auto-width
  branch entirely).
- Use `display: inline` with padding for chip-like UI (loses
  block-level box decoration).
