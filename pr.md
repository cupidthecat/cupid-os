# Browser stack + networking robustness

Branch: `browser-render-pipeline` → `main` (110 commits ahead).

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

## Out-of-scope on this branch

The branch also carries the DOOM port, the AC97 / Nuked-OPL3 audio
mixer, the FAT16/homefs build reshuffle, and a handful of kernel
hardening fixes. Those are independent and should be reviewed under
their own PRs — not summarised here.

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
