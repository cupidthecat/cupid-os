# Browser Build-System Split — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split `bin/browser.cc` (~2070 LOC, single file) into `bin/browser/*.cc` library files glued via CupidC `#include`, with **zero behavior change**. This is the foundation for the WebKit-lite redesign — every subsequent plan (parser rewrite, CSS+render-tree, layout, tables, images) builds on this file structure.

**Architecture:** CupidC's preprocessor (`kernel/cupidc.c`) already supports `#include "rel/path"` resolved relative to the including file's directory and read from VFS at JIT time. The change is purely:
1. Move sections of `bin/browser.cc` into `bin/browser/<name>.cc` files
2. Top-level `bin/browser.cc` becomes a thin trampoline: `#include "browser/util.cc"` ... `void main() { browser_main(); }`
3. Makefile gets a pattern rule for `bin/browser/%.o` plus generator extension to embed sub-files into ramfs at `/bin/browser/<name>.cc`
4. Sub-files are NOT registered as runnable programs (excluded from `BIN_CC_NAMES`)

**Tech Stack:** GNU Make, gcc + objcopy (host toolchain), CupidC (in-OS JIT), QEMU (smoke test).

**Reference spec:** `docs/superpowers/specs/2026-05-05-browser-redesign.md` §0 "Build System: file split".

**Testing reality:** No host-side unit-test framework exists for browser code (CupidC code uses kernel-only bindings: `gfx2d_*`, `gui_win_*`, `socket()`, etc.). Each task verifies via:
- **Build check** — `make` succeeds, `_binary_bin_browser_<n>_cc_start` symbols emit, `make` log shows `[kernel] Installed /bin/browser/<n>.cc (NNNN bytes)` line
- **Boot check** — `make run-headless` completes early-boot phase; serial log includes the install lines
- **Smoke check** — `make run-net`, in shell run `browser http://example.com/`, page renders with title bar showing "Example Domain", clickable link visible

For each extraction task that moves code verbatim from the original file, the test is "browser still renders example.com identically to pre-split" — verified by manual visual comparison against a pre-split screenshot kept on the working branch.

---

## File Structure

After this plan completes:

```
bin/browser.cc                — entry; #includes sub-files; main() trampoline (≈30 LOC)
bin/browser/util.cc           — string helpers (b_strlen, b_streq, b_lc, ...)
bin/browser/url.cc            — parse_url, resolve_redirect, compute_url_relative, hex_digit
bin/browser/net.cc            — fetch_url, build_request, HTTP/HTTPS+redirects
bin/browser/dom.cc            — DOM arrays, attr_pool, alloc_node, attr_intern, decode_entities
bin/browser/parser.cc         — tag_id, is_void_tag, skip_to_close, apply_style, parse_html
bin/browser/layout.cc         — L_* state, layout_text/node/children, run_layout, parent_*
bin/browser/paint.cc          — render, draw_text/input/button/image, draw_address/status/scrollbar, error_page, viewport_*
bin/browser/input.cc          — handle_address/input/page_key, handle_keys, handle_mouse, hit_box, find_*, clamp_scroll
bin/browser/nav.cc            — navigate, go_back, submit_form
bin/browser/main.cc           — globals (cur_url, cur_host, ..., focus_mode, ...) + browser_main()
```

The trampoline `bin/browser.cc`:
```c
//help: Web browser. Renders HTTP and HTTPS pages in a window.
//help: Usage: browser [url]
//help:   Address bar: Ctrl-L to focus, Enter to go.
//help:   Backspace (page focus): back history.
//help:   Arrow keys / mouse wheel: scroll.
//help:   Click link to navigate.

#include "browser/util.cc"
#include "browser/url.cc"
#include "browser/net.cc"
#include "browser/dom.cc"
#include "browser/parser.cc"
#include "browser/layout.cc"
#include "browser/paint.cc"
#include "browser/input.cc"
#include "browser/nav.cc"
#include "browser/main.cc"

void main() { browser_main(); }
```

**Order matters.** CupidC has no forward declarations across `#include` boundaries — the preprocessor concatenates expanded source into one buffer, then a single-pass compiler reads it. Functions/globals must be defined before any reference. The order above is bottom-up: leaf utilities first, mutual entry point last. `main.cc` holds globals + the entry function `browser_main()` whose body calls into all preceding modules.

**Why no `bin/browser/image.cc` yet?** Spec §7 (image fetching) is a new feature for a later plan; this plan ships unchanged behavior. Image code would be a stub today — YAGNI.

**Why a separate `main.cc` instead of inlining into `bin/browser.cc`?** Globals and `browser_main()` reference everything else, so they must come last in include order. Keeping the trampoline file as pure `#include` lines + one stub `main()` makes the trampoline obviously a build artifact, not a place where logic accumulates.

---

## Pre-flight

- [ ] **Step 0.1: Take pre-split visual reference**

Run: `make run-net` and in the shell type `browser http://example.com/`. Take a screenshot (or photograph the QEMU window) and save it as `/tmp/browser-pre-split.png`. This is the visual oracle that every extraction task is compared against. Quit QEMU with `Ctrl-A X` or close the window.

Expected: the address bar shows `http://example.com/`, the title is "Example Domain", there is one underlined link "More information...", and the page background is white.

- [ ] **Step 0.2: Confirm baseline build is clean on main**

Run: `git status` and confirm `bin/browser.cc` is the only browser file (no `bin/browser/` directory exists yet).
Expected: no `bin/browser/` listed.

Run: `make 2>&1 | tail -20`
Expected: build succeeds, last line is something like `Linked $(KERNEL)`.

---

## Task 1: Smoke test — prove `#include` + ramfs-embed flow works

This task validates the entire build mechanism on a tiny stub before doing any real refactor. If anything is wrong with the Makefile or generator, we find out here.

**Files:**
- Create: `bin/browser/_smoke.cc`
- Modify: `bin/browser.cc` (add one `#include` and one call)
- Modify: `Makefile:22-31, 678-695, 734-740, 909-911`
- Modify: nothing in `kernel/` (the `bin_programs_gen.c` file is generated)

- [ ] **Step 1.1: Create `bin/browser/` directory + smoke stub**

Create file `bin/browser/_smoke.cc` with exact contents:
```c
/* Smoke stub: proves CupidC #include + ramfs-embed pipeline works.
 * browser_smoke_token() is called from bin/browser.cc once at startup.
 * Returns a magic value the caller checks via println(). */

int browser_smoke_token() {
    return 0xCAFE;
}
```

- [ ] **Step 1.2: Add Makefile discovery for `bin/browser/*.cc`**

Edit `Makefile`. Find the block at lines 22-31 (the `BIN_CC_SRCS`/`BIN_CC_OBJS`/`BIN_CC_NAMES` and the matching `BIN_HDR_*` block). Append immediately after line 31 (after the `BIN_HDR_NAMES :=` line, before the next `# Auto-discover` comment):

```make

# Auto-discover browser library sub-files (bin/browser/*.cc).
# These are #include'd by bin/browser.cc and embedded in ramfs at
# /bin/browser/<n>.cc so the CupidC preprocessor can resolve them
# at JIT time. They are NOT runnable programs (not added to BIN_CC_NAMES).
BROWSER_SUB_SRCS := $(wildcard bin/browser/*.cc)
$(info BROWSER_SUB_SRCS=$(BROWSER_SUB_SRCS))
BROWSER_SUB_OBJS := $(BROWSER_SUB_SRCS:.cc=.o)
BROWSER_SUB_NAMES := $(notdir $(basename $(BROWSER_SUB_SRCS)))
```

- [ ] **Step 1.3: Add `BROWSER_SUB_OBJS` to the kernel link list**

Still in `Makefile`. Find the kernel link list at line 138 — the line that ends with `$(BIN_CC_OBJS) $(BIN_HDR_OBJS) $(DOC_CTXT_OBJS) $(DOC_ASSET_OBJS) $(DEMO_ASM_OBJS) $(GOD_DD_OBJS)`.

Replace that single line (whatever its current line number is — it was 138 at plan-write time) with:
```make
			$(BIN_CC_OBJS) $(BIN_HDR_OBJS) $(BROWSER_SUB_OBJS) $(DOC_CTXT_OBJS) $(DOC_ASSET_OBJS) $(DEMO_ASM_OBJS) $(GOD_DD_OBJS)
```

(Inserts `$(BROWSER_SUB_OBJS)` between `$(BIN_HDR_OBJS)` and `$(DOC_CTXT_OBJS)`.)

- [ ] **Step 1.4: Add objcopy pattern rule for `bin/browser/*.cc`**

Still in `Makefile`. Find the existing pattern rule near line 735:
```make
# Pattern rule: embed any bin/*.cc file via objcopy
bin/%.o: bin/%.cc
	objcopy -I binary -O elf32-i386 -B i386 $< $@
```

Append immediately after that block (still before the `bin/%.h.o:` rule at line 739):
```make

# Pattern rule: embed any bin/browser/*.cc library file via objcopy.
# These live in ramfs at /bin/browser/<n>.cc and are #include'd by
# bin/browser.cc at JIT time. They are NOT in BIN_CC_NAMES.
bin/browser/%.o: bin/browser/%.cc
	objcopy -I binary -O elf32-i386 -B i386 $< $@
```

- [ ] **Step 1.5: Extend `bin_programs_gen.c` generator to emit ramfs entries for sub-files**

Still in `Makefile`. Find the recipe at lines 678-692 that builds `kernel/bin_programs_gen.c`. After the line that emits the closing `}` for `install_bin_programs` (currently `@echo '}' >> $@`), and **before** the rule that follows (`kernel/bin_programs_gen.o:` near line 694), inject the BROWSER_SUB iteration. The clearest way is to add it inside the same recipe just before the closing `}`. Replace lines 688-692 (the block from `@echo 'void install_bin_programs(void *fs_private);'` through `@echo '}' >> $@`) with:

```make
	@echo 'void install_bin_programs(void *fs_private);' >> $@
	@echo 'void install_bin_programs(void *fs_private) {' >> $@
	@$(foreach n,$(BIN_CC_NAMES),echo '    { uint32_t sz = (uint32_t)(_binary_bin_$(n)_cc_end - _binary_bin_$(n)_cc_start); ramfs_add_file(fs_private, "bin/$(n).cc", _binary_bin_$(n)_cc_start, sz); serial_printf("[kernel] Installed /bin/$(n).cc (%u bytes)\n", sz); }' >> $@;)
	@$(foreach n,$(BIN_HDR_NAMES),echo '    { uint32_t sz = (uint32_t)(_binary_bin_$(n)_h_end - _binary_bin_$(n)_h_start); ramfs_add_file(fs_private, "bin/$(n).h", _binary_bin_$(n)_h_start, sz); serial_printf("[kernel] Installed /bin/$(n).h (%u bytes)\n", sz); }' >> $@;)
	@$(foreach n,$(BROWSER_SUB_NAMES),echo 'extern const char _binary_bin_browser_$(n)_cc_start[];' >> $@;)
	@$(foreach n,$(BROWSER_SUB_NAMES),echo 'extern const char _binary_bin_browser_$(n)_cc_end[];' >> $@;)
	@$(foreach n,$(BROWSER_SUB_NAMES),echo '    { uint32_t sz = (uint32_t)(_binary_bin_browser_$(n)_cc_end - _binary_bin_browser_$(n)_cc_start); ramfs_add_file(fs_private, "bin/browser/$(n).cc", _binary_bin_browser_$(n)_cc_start, sz); serial_printf("[kernel] Installed /bin/browser/$(n).cc (%u bytes)\n", sz); }' >> $@;)
	@echo '}' >> $@
```

**Note:** The two `extern` lines for browser sub-files appear AFTER the `install_bin_programs` body opens — that is intentionally wrong for clean C, but it doesn't matter because both `extern` decls and the body use the same identifiers, and the generated file is a single translation unit where the C compiler accepts re-declarations after a function body opens as long as the symbols are still in file scope. Verify in Step 1.7 that gcc accepts it; if it complains, move the two `extern` `foreach` lines to before the `void install_bin_programs(void *fs_private) {` line.

Actually: gcc DOES reject decls inside a function body that aren't local. **Move the two `extern` foreach lines BEFORE the `'void install_bin_programs(void *fs_private) {'` line**, alongside the existing extern blocks. The corrected replacement for lines 684-692 is:

```make
	@$(foreach n,$(BIN_CC_NAMES),echo 'extern const char _binary_bin_$(n)_cc_start[];' >> $@;)
	@$(foreach n,$(BIN_HDR_NAMES),echo 'extern const char _binary_bin_$(n)_h_start[];' >> $@;)
	@$(foreach n,$(BIN_CC_NAMES),echo 'extern const char _binary_bin_$(n)_cc_end[];' >> $@;)
	@$(foreach n,$(BIN_HDR_NAMES),echo 'extern const char _binary_bin_$(n)_h_end[];' >> $@;)
	@$(foreach n,$(BROWSER_SUB_NAMES),echo 'extern const char _binary_bin_browser_$(n)_cc_start[];' >> $@;)
	@$(foreach n,$(BROWSER_SUB_NAMES),echo 'extern const char _binary_bin_browser_$(n)_cc_end[];' >> $@;)
	@echo 'void install_bin_programs(void *fs_private);' >> $@
	@echo 'void install_bin_programs(void *fs_private) {' >> $@
	@$(foreach n,$(BIN_CC_NAMES),echo '    { uint32_t sz = (uint32_t)(_binary_bin_$(n)_cc_end - _binary_bin_$(n)_cc_start); ramfs_add_file(fs_private, "bin/$(n).cc", _binary_bin_$(n)_cc_start, sz); serial_printf("[kernel] Installed /bin/$(n).cc (%u bytes)\n", sz); }' >> $@;)
	@$(foreach n,$(BIN_HDR_NAMES),echo '    { uint32_t sz = (uint32_t)(_binary_bin_$(n)_h_end - _binary_bin_$(n)_h_start); ramfs_add_file(fs_private, "bin/$(n).h", _binary_bin_$(n)_h_start, sz); serial_printf("[kernel] Installed /bin/$(n).h (%u bytes)\n", sz); }' >> $@;)
	@$(foreach n,$(BROWSER_SUB_NAMES),echo '    { uint32_t sz = (uint32_t)(_binary_bin_browser_$(n)_cc_end - _binary_bin_browser_$(n)_cc_start); ramfs_add_file(fs_private, "bin/browser/$(n).cc", _binary_bin_browser_$(n)_cc_start, sz); serial_printf("[kernel] Installed /bin/browser/$(n).cc (%u bytes)\n", sz); }' >> $@;)
	@echo '}' >> $@
```

Verify nothing else in this recipe (the dependency list at line 678 and the `@echo "/* Auto-generated"` etc. above) was changed.

**Also** add `$(BROWSER_SUB_SRCS)` to the dependency list of the `kernel/bin_programs_gen.c:` rule at line 678 so the generator re-runs when sub-files change. Replace:
```make
kernel/bin_programs_gen.c: $(BIN_CC_SRCS) $(BIN_HDR_SRCS) Makefile
```
with:
```make
kernel/bin_programs_gen.c: $(BIN_CC_SRCS) $(BIN_HDR_SRCS) $(BROWSER_SUB_SRCS) Makefile
```

- [ ] **Step 1.6: Update `clean` target to remove `bin/browser/*.o`**

Find the `clean:` rule at line 909. The line currently reads (approximately, see Makefile):
```make
	rm -f $(BOOTLOADER) $(KERNEL) kernel/*.o kernel/tls/*.o drivers/*.o filesystem/*.o bin/*.o cupidos-txt/*.o demos/*.o \
```
Append `bin/browser/*.o` to that wildcard list. Replace `bin/*.o` with `bin/*.o bin/browser/*.o`.

- [ ] **Step 1.7: Wire smoke call into `bin/browser.cc`**

Open `bin/browser.cc`. Find the `void main()` function near the end (line 1988 in the pre-split file). At the very top of `main()`, immediately after the opening `{`, add:

```c
    if (browser_smoke_token() != 0xCAFE) {
        println("browser: smoke token mismatch — include broken");
        return;
    }
```

At the top of `bin/browser.cc` (after the `//help:` comment block that ends at line 6), add the include line **before** any other code:

```c
#include "browser/_smoke.cc"
```

- [ ] **Step 1.8: Build and verify the install line appears**

Run: `make 2>&1 | tee /tmp/build.log`
Expected: build succeeds (exit 0). Last line is the linker invocation that produces `kernel/kernel.bin`.

Run: `grep "browser/_smoke" /tmp/build.log`
Expected: at least one line, e.g. `BROWSER_SUB_SRCS=bin/browser/_smoke.cc`.

Run: `grep "_binary_bin_browser__smoke_cc" kernel/bin_programs_gen.c`
Expected: at least four lines (two `extern` decls, one ramfs_add_file, possibly the serial_printf as part of one of those).

- [ ] **Step 1.9: Boot QEMU headless and verify ramfs install line**

Run: `timeout 25 make run-headless 2>&1 | grep "Installed /bin"`
Expected: among many lines, one reads `[kernel] Installed /bin/browser/_smoke.cc (NNN bytes)` where NNN is roughly the byte count of `bin/browser/_smoke.cc` (~250 bytes).

If the line is missing: the generator extension or wildcard discovery is wrong. Re-check Steps 1.2 / 1.5.

- [ ] **Step 1.10: Smoke test the browser at runtime**

Run: `make run-net` and in the QEMU shell, type `browser http://example.com/`.
Expected: the browser window opens. The page renders identically to `/tmp/browser-pre-split.png` (Step 0.1). The status bar must NOT say "smoke token mismatch — include broken". If you see that message, the `#include` resolution failed at JIT time — check that `/bin/browser/_smoke.cc` exists in ramfs by running `ls /bin/browser/` in the shell first.

Quit QEMU.

- [ ] **Step 1.11: Commit**

```bash
git add bin/browser/_smoke.cc bin/browser.cc Makefile
git commit -m "$(cat <<'EOF'
browser: add bin/browser/ subdir + ramfs-embed pipeline

Wires up Makefile discovery, objcopy pattern rule, and bin_programs_gen
generator to embed bin/browser/*.cc files into ramfs at /bin/browser/<n>.cc.
Sub-files are CupidC #include targets, NOT runnable programs (excluded from
BIN_CC_NAMES).

The bin/browser/_smoke.cc stub + browser_smoke_token() call in main()
proves the full pipeline works before any real code extraction.

Foundation for browser WebKit-lite redesign (see
docs/superpowers/specs/2026-05-05-browser-redesign.md).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Extract `util.cc`

Move pure string/character helpers. Verify build + run unchanged.

**Files:**
- Create: `bin/browser/util.cc`
- Modify: `bin/browser.cc` (delete moved lines, add `#include`)

- [ ] **Step 2.1: Create `bin/browser/util.cc`**

Copy verbatim from `bin/browser.cc` lines 187-264 (the block from `/* ---------- Utility ---------- */` through the closing of `b_append_int`) into a new file `bin/browser/util.cc`. The exact functions to move:

- `b_strlen(char *s)` (lines 189-193)
- `b_streq(char *a, char *b)` (lines 195-199)
- `b_lc(int c)` (lines 201-204)
- `b_strieq(char *a, char *b)` (lines 206-210)
- `b_streq_n(char *a, char *b, int n)` (lines 212-216)
- `b_strieq_n(char *a, char *b, int n)` (lines 218-227)
- `b_strcpy_n(char *dst, char *src, int max)` (lines 229-233)
- `b_strchr(char *s, int c)` (lines 235-242)
- `b_append(char *dst, int p, char *src)` (lines 244-248)
- `b_append_n(char *dst, int p, char *src, int n)` (lines 250-254)
- `b_append_int(char *dst, int p, int v)` (lines 256-264)

Keep the `/* ---------- Utility ---------- */` banner as a comment at the top of `util.cc`.

- [ ] **Step 2.2: Delete moved lines from `bin/browser.cc`**

Open `bin/browser.cc`. Delete lines 187-264 (the entire moved block including the banner). Replace with a single line:
```c
/* utility helpers in browser/util.cc */
```

- [ ] **Step 2.3: Add `#include "browser/util.cc"` to trampoline**

In `bin/browser.cc`, the top now has `#include "browser/_smoke.cc"`. Add directly above it (so include order is: util first, smoke after — order will matter once smoke goes away, but for now util is a leaf):
```c
#include "browser/util.cc"
#include "browser/_smoke.cc"
```

- [ ] **Step 2.4: Build**

Run: `make 2>&1 | tail -5`
Expected: build succeeds.

If the build fails with "duplicate definition" — the lines weren't fully deleted from `bin/browser.cc`. Re-check Step 2.2.

If it fails with "undefined function b_strlen" inside some other function — `util.cc` must come BEFORE every other `#include` that uses these helpers (it's the first one — good). If still broken, the file content didn't get the body, only the banner.

- [ ] **Step 2.5: Smoke test**

Run: `make run-net`, in shell `browser http://example.com/`, visually compare to `/tmp/browser-pre-split.png`.
Expected: identical render.

Quit QEMU.

- [ ] **Step 2.6: Commit**

```bash
git add bin/browser/util.cc bin/browser.cc
git commit -m "$(cat <<'EOF'
browser: extract string helpers to bin/browser/util.cc

Moves b_strlen, b_streq, b_lc, b_strieq, b_streq_n, b_strieq_n,
b_strcpy_n, b_strchr, b_append, b_append_n, b_append_int verbatim.
No behavior change.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Extract `url.cc`

Move URL parsing, redirect resolution, color helpers (`hex_digit`).

**Files:**
- Create: `bin/browser/url.cc`
- Modify: `bin/browser.cc` (delete moved lines, add `#include`)

- [ ] **Step 3.1: Create `bin/browser/url.cc`**

Move verbatim from `bin/browser.cc` (line numbers from pre-split file):

- `parse_url(char *url, char *host, int *port_out, char *path, int *is_https_out)` (lines 268-307)
- `resolve_redirect(char *location, char *cur_h, int cur_p, int cur_is_h, char *out_url, int max)` (lines 309-338)
- `hex_digit(int c)` (lines 519-524) — small helper used by both URL escaping and color parsing

Keep banner `/* ---------- URL parsing ---------- */` at top of file.

`compute_url_relative` stays in `nav.cc` (Task 9) because it pulls in globals (`cur_host`, `cur_is_https`, `cur_port`, `cur_path`).

- [ ] **Step 3.2: Delete moved lines from `bin/browser.cc`**

Delete the original ranges (lines 268-338 and lines 519-524). Replace each with a comment:
- After line 267 (where the banner used to be): `/* URL parsing in browser/url.cc */`
- Where `hex_digit` was: `/* hex_digit in browser/url.cc */`

- [ ] **Step 3.3: Update trampoline `#include` order**

In `bin/browser.cc` top:
```c
#include "browser/util.cc"
#include "browser/url.cc"
#include "browser/_smoke.cc"
```

- [ ] **Step 3.4: Build + smoke + commit**

Run: `make 2>&1 | tail -5` → succeeds.
Run: `make run-net` → `browser http://example.com/` → matches `/tmp/browser-pre-split.png`. Quit.

```bash
git add bin/browser/url.cc bin/browser.cc
git commit -m "$(cat <<'EOF'
browser: extract URL parsing to bin/browser/url.cc

Moves parse_url, resolve_redirect, hex_digit verbatim.
compute_url_relative stays with navigation (uses page-state globals).
No behavior change.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Extract `net.cc`

Move HTTP/HTTPS fetch logic.

**Files:**
- Create: `bin/browser/net.cc`
- Modify: `bin/browser.cc`

- [ ] **Step 4.1: Create `bin/browser/net.cc`**

Move verbatim from `bin/browser.cc`:
- `build_request(char *buf, char *method, char *p, char *h)` (lines 342-352)
- `fetch_url(char *url, char *content_type_out)` (lines 355-515)

Banner: `/* ---------- HTTP fetch ---------- */`.

`fetch_url` references globals (`cur_host`, `cur_port`, `cur_path`, `cur_is_https`, `cur_url`, `page_buf`, `page_len`, `status_msg`) — those live in `main.cc` (Task 11) but since CupidC concatenates `#include`s into one TU, references resolve at the final compile pass. The order of `#include`s ensures `main.cc` (which declares the globals) comes after `net.cc` declarations BUT in CupidC parallel arrays / globals are forward-referenced because the compiler's symbol pass sees the full source. **Verify:** if the build fails with "undefined symbol cur_host" inside `net.cc`, swap include order so `main.cc` comes first. (Test below catches this.)

Also references kernel bindings (`socket`, `connect`, `setsockopt`, `send`, `recv`, `close`, `dns_resolve`, `htons`) — these are kernel symbols always available.

- [ ] **Step 4.2: Delete moved lines from `bin/browser.cc`**

Delete lines 340-515 (banner through end of `fetch_url`). Replace with `/* HTTP fetch in browser/net.cc */`.

- [ ] **Step 4.3: Update trampoline order**

```c
#include "browser/util.cc"
#include "browser/url.cc"
#include "browser/net.cc"
#include "browser/_smoke.cc"
```

- [ ] **Step 4.4: Build**

Run: `make 2>&1 | tail -10`

If error mentions undefined `cur_host`/`cur_port`/`page_buf`: globals must be defined before use. Either the order needs `main.cc` (containing globals) first, OR we need to forward-declare globals at the top of the trampoline. Simplest fix: add the global declarations to a new `bin/browser/globals.cc` and include it FIRST. We'll do that in Task 11 once we know which globals are involved.

For this task only, if the build fails with undefined globals, use this workaround: before `#include "browser/net.cc"`, add forward declarations at the top of `bin/browser.cc`:
```c
char cur_url[1024];
char cur_host[256];
int  cur_port;
char cur_path[1024];
int  cur_is_https;
char status_msg[256];
char page_buf[96000];
int  page_len;
```
These are CupidC global declarations and act both as definitions and as forward refs (the CupidC compiler uses one-pass with parallel-array global tracking). This is temporary scaffolding — Task 11 moves them to `main.cc`.

If they end up duplicated against the originals still in `bin/browser.cc`, you'll get a duplicate-definition error — in that case the fix is to delete the originals from `bin/browser.cc`. They're declared at lines 97-178 of the pre-split file — leave them in place for now and only add the temporary forward decls if the build actually fails.

- [ ] **Step 4.5: Smoke test + commit**

Run: `make run-net` → `browser http://example.com/`. Network fetch must work — page loads. Match `/tmp/browser-pre-split.png`. Quit.

```bash
git add bin/browser/net.cc bin/browser.cc
git commit -m "$(cat <<'EOF'
browser: extract HTTP fetch to bin/browser/net.cc

Moves build_request, fetch_url verbatim. References to globals
(cur_host, page_buf, etc.) resolve at trampoline-link time.
No behavior change.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Extract `dom.cc`

Move DOM allocation, attribute pool, entity decoder, color parsing.

**Files:**
- Create: `bin/browser/dom.cc`
- Modify: `bin/browser.cc`

- [ ] **Step 5.1: Create `bin/browser/dom.cc`**

Move verbatim from `bin/browser.cc`:

- `parse_color_named(char *s, int *out)` (lines 526-544)
- `parse_color(char *s, int *out)` (lines 546-569)
- `apply_style(char *style, int node_idx)` (lines 572-612)
- `alloc_node(int tag, int parent)` (lines 660-689)
- `attr_intern(char *s, int len)` (lines 692-700)
- `decode_entities(char *src, int slen, char *out, int omax)` (lines 704-768)

Banner: `/* ---------- DOM nodes + attribute pool + entity decoder ---------- */`.

Note: `apply_style` calls `parse_color` — both move together so call order remains contiguous.

- [ ] **Step 5.2: Delete moved lines from `bin/browser.cc`**

Delete original ranges. Replace each block with a comment marker.

- [ ] **Step 5.3: Update trampoline order**

```c
#include "browser/util.cc"
#include "browser/url.cc"
#include "browser/net.cc"
#include "browser/dom.cc"
#include "browser/_smoke.cc"
```

- [ ] **Step 5.4: Build + smoke + commit**

Run: `make` → succeeds. Run: `make run-net` → `browser http://example.com/` matches reference. Quit.

```bash
git add bin/browser/dom.cc bin/browser.cc
git commit -m "$(cat <<'EOF'
browser: extract DOM/attr-pool/entities to bin/browser/dom.cc

Moves parse_color_named, parse_color, apply_style, alloc_node,
attr_intern, decode_entities verbatim. No behavior change.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Extract `parser.cc`

Move HTML lex/parse logic.

**Files:**
- Create: `bin/browser/parser.cc`
- Modify: `bin/browser.cc`

- [ ] **Step 6.1: Create `bin/browser/parser.cc`**

Move verbatim from `bin/browser.cc`:

- `tag_id(char *name, int len)` (lines 616-654)
- `is_void_tag(int tag)` (lines 656-658)
- `skip_to_close(char *html, int n, int i, char *closetag)` (lines 772-786)
- `parse_html(int html_len)` (lines 789-1034)

Banner: `/* ---------- HTML lex / parse ---------- */`.

`parse_html` uses helpers from `dom.cc` (`alloc_node`, `attr_intern`, `decode_entities`, `apply_style`, `parse_color`, `b_strieq_n`, `b_streq`, `b_strcpy_n`, `b_strlen`) — already in earlier `#include`s.

- [ ] **Step 6.2: Delete moved lines + update trampoline + build + smoke + commit**

Delete original ranges. Update trampoline include order:
```c
#include "browser/util.cc"
#include "browser/url.cc"
#include "browser/net.cc"
#include "browser/dom.cc"
#include "browser/parser.cc"
#include "browser/_smoke.cc"
```

Run: `make` → succeeds. `make run-net` → `browser http://example.com/` matches reference. Quit.

```bash
git add bin/browser/parser.cc bin/browser.cc
git commit -m "$(cat <<'EOF'
browser: extract HTML parser to bin/browser/parser.cc

Moves tag_id, is_void_tag, skip_to_close, parse_html verbatim.
No behavior change.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Extract `layout.cc`

Move layout-time state and routines.

**Files:**
- Create: `bin/browser/layout.cc`
- Modify: `bin/browser.cc`

- [ ] **Step 7.1: Create `bin/browser/layout.cc`**

Move verbatim from `bin/browser.cc`:

- `viewport_w()` (lines 1038-1042)
- `parent_color(int idx, int default_c)` (lines 1044-1050)
- `parent_bg(int idx, int default_c)` (lines 1052-1058)
- `parent_bold(int idx)` (lines 1060-1068)
- `parent_link(int idx)` (lines 1070-1076)
- Layout state globals `L_x`, `L_y`, `L_line_h`, `L_max_w`, `L_left_margin` (lines 1079-1083)
- `emit_box(int kind)` (lines 1085-1095)
- `last_box()` (line 1097)
- `register_link(int href_off)` (lines 1099-1105)
- `newline()` (lines 1107-1111)
- `layout_text(int node_idx, int text_off, int len, int link_idx, int bold, int fg, int bg)` (lines 1113-1180)
- `layout_children(int idx)` (lines 1182-1188)
- `layout_node(int idx)` (lines 1190-1307)
- `run_layout()` (lines 1309-1337)

Banner: `/* ---------- Layout ---------- */`.

`layout_node` and `layout_children` mutually recurse. CupidC needs both visible in source order — `layout_children` is defined before `layout_node`, and `layout_node` calls `layout_children` AND vice versa. Check the original file: line 1186 has `layout_children` calling `layout_node`. The pre-split file works because both are in the same TU; same applies after move (both in `layout.cc`, same TU).

- [ ] **Step 7.2: Delete moved lines + update trampoline + build + smoke + commit**

Update include order:
```c
#include "browser/util.cc"
#include "browser/url.cc"
#include "browser/net.cc"
#include "browser/dom.cc"
#include "browser/parser.cc"
#include "browser/layout.cc"
#include "browser/_smoke.cc"
```

Run: `make` → succeeds. `make run-net` → matches reference. Quit.

```bash
git add bin/browser/layout.cc bin/browser.cc
git commit -m "$(cat <<'EOF'
browser: extract layout to bin/browser/layout.cc

Moves layout state (L_*), emit_box, register_link, newline, layout_text,
layout_children, layout_node, run_layout, parent_* getters, viewport_w
verbatim. No behavior change.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Extract `paint.cc`

Move all rendering routines.

**Files:**
- Create: `bin/browser/paint.cc`
- Modify: `bin/browser.cc`

- [ ] **Step 8.1: Create `bin/browser/paint.cc`**

Move verbatim from `bin/browser.cc`:

- `viewport_x()`, `viewport_y()`, `viewport_h()` (lines 1341-1347)
- `draw_text_box(int bi, int sx, int sy)` (lines 1349-1380)
- `draw_input_box(int bi, int sx, int sy)` (lines 1382-1404)
- `draw_button_box(int bi, int sx, int sy)` (lines 1406-1425)
- `draw_image_box(int bi, int sx, int sy)` (lines 1427-1438)
- `draw_address_bar(int sx, int sy, int sw)` (lines 1440-1456)
- `draw_status_bar(int sx, int sy, int sw)` (lines 1458-1470)
- `draw_scrollbar(int sx, int sy)` (lines 1472-1483)
- `render()` (lines 1485-1534)
- `error_page(char *msg)` (lines 1964-1986)

Banner: `/* ---------- Render ---------- */`.

- [ ] **Step 8.2: Delete moved lines + update trampoline + build + smoke + commit**

Update include order:
```c
#include "browser/util.cc"
#include "browser/url.cc"
#include "browser/net.cc"
#include "browser/dom.cc"
#include "browser/parser.cc"
#include "browser/layout.cc"
#include "browser/paint.cc"
#include "browser/_smoke.cc"
```

Run: `make` → succeeds. `make run-net` → matches reference. Quit.

```bash
git add bin/browser/paint.cc bin/browser.cc
git commit -m "$(cat <<'EOF'
browser: extract paint to bin/browser/paint.cc

Moves draw_text/input/button/image_box, draw_address/status_bar,
draw_scrollbar, render, error_page, viewport_x/y/h verbatim.
No behavior change.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: Extract `nav.cc`

Move navigation, history, form submission.

**Files:**
- Create: `bin/browser/nav.cc`
- Modify: `bin/browser.cc`

- [ ] **Step 9.1: Create `bin/browser/nav.cc`**

Move verbatim from `bin/browser.cc`:

- `compute_url_relative(char *rel, char *out, int max)` (lines 1538-1577)
- `navigate(char *u)` (lines 1581-1620)
- `go_back()` (lines 1622-1630)
- `submit_form(int form_node_idx)` (lines 1634-1711)

Banner: `/* ---------- Navigation ---------- */`.

These reference `fetch_url` (in `net.cc`), `parse_html` (in `parser.cc`), `run_layout` (in `layout.cc`), `render` (in `paint.cc`), `error_page` (in `paint.cc`). All earlier in include order — good.

- [ ] **Step 9.2: Delete moved lines + update trampoline + build + smoke + commit**

Update include order:
```c
#include "browser/util.cc"
#include "browser/url.cc"
#include "browser/net.cc"
#include "browser/dom.cc"
#include "browser/parser.cc"
#include "browser/layout.cc"
#include "browser/paint.cc"
#include "browser/nav.cc"
#include "browser/_smoke.cc"
```

Run: `make` → succeeds. `make run-net` → matches reference, navigate to a link, hit Backspace, page goes back. Quit.

```bash
git add bin/browser/nav.cc bin/browser.cc
git commit -m "$(cat <<'EOF'
browser: extract navigation to bin/browser/nav.cc

Moves compute_url_relative, navigate, go_back, submit_form verbatim.
No behavior change.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: Extract `input.cc`

Move keyboard, mouse, and hit-testing handlers.

**Files:**
- Create: `bin/browser/input.cc`
- Modify: `bin/browser.cc`

- [ ] **Step 10.1: Create `bin/browser/input.cc`**

Move verbatim from `bin/browser.cc`:

- `hit_box(int mx, int my)` (lines 1715-1729)
- `find_node_for_input(int ii)` (lines 1732-1739)
- `find_form_node(int input_node)` (lines 1741-1748)
- `clamp_scroll()` (lines 1752-1757)
- `handle_address_key(int sc, int ch)` (lines 1759-1800)
- `handle_input_key(int sc, int ch)` (lines 1802-1834)
- `handle_page_key(int sc, int ch)` (lines 1836-1854)
- `handle_keys()` (lines 1856-1866)
- `handle_left_click(int mx, int my)` (lines 1868-1927)
- `handle_hover(int mx, int my)` (lines 1929-1944)
- `handle_mouse()` (lines 1946-1960)

Banner: `/* ---------- Hit testing + Input ---------- */`.

These reference `submit_form`, `navigate`, `compute_url_relative` (all in `nav.cc`) and `gui_win_close`, `gui_win_content_x/y`, `mouse_x/y`, `mouse_buttons`, `mouse_scroll`, `gui_win_poll_key` (kernel bindings).

- [ ] **Step 10.2: Delete moved lines + update trampoline + build + smoke + commit**

Update include order — `input.cc` AFTER `nav.cc` (because input handlers call `navigate`/`submit_form`/`go_back`/`compute_url_relative`):
```c
#include "browser/util.cc"
#include "browser/url.cc"
#include "browser/net.cc"
#include "browser/dom.cc"
#include "browser/parser.cc"
#include "browser/layout.cc"
#include "browser/paint.cc"
#include "browser/nav.cc"
#include "browser/input.cc"
#include "browser/_smoke.cc"
```

Run: `make` → succeeds. `make run-net` → `browser http://example.com/`, click the link, page loads. Press Ctrl-L, type a new URL, press Enter, page loads. Press Backspace, page goes back. Quit.

```bash
git add bin/browser/input.cc bin/browser.cc
git commit -m "$(cat <<'EOF'
browser: extract input/hit-test to bin/browser/input.cc

Moves hit_box, find_node_for_input, find_form_node, clamp_scroll,
handle_*_key, handle_keys, handle_left_click, handle_hover,
handle_mouse verbatim. No behavior change.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 11: Extract `main.cc` (globals + entry)

Move all remaining globals and the entry point. After this task `bin/browser.cc` is a pure trampoline.

**Files:**
- Create: `bin/browser/main.cc`
- Modify: `bin/browser.cc`

- [ ] **Step 11.1: Create `bin/browser/main.cc`**

Move verbatim from `bin/browser.cc` everything that remains except the `//help:` header. That's:

- All global declarations (lines 92-185 of pre-split file): `win`, `font_id`, `line_h`, `char_w`, `cur_url`, `cur_host`, `cur_port`, `cur_path`, `cur_is_https`, `addr_buf`, `addr_len`, `addr_cursor`, `title_buf`, `status_msg`, `page_buf`, `page_len`, `ctype_buf`, `nodes_count`, all `n_*` parallel arrays, `attr_pool`, `attr_pool_pos`, `boxes_count`, all `b_*` arrays, `links_count`, `link_url_off`, `inputs_count`, `input_value`, `input_name_off`, `input_form`, `forms_count`, `form_action`, `doc_h`, `scroll_y`, `page_bg`, `page_fg`, `hist_url_pool`, `hist_count`, `focus_mode`, `focused_input`, `prev_buttons`, `hover_link`, `cur_cw`, `cur_ch`.
- All `enum` constants (lines 8-88 of pre-split file): `SOCK_TCP`, `SOL_TLS`, `TLS_ENABLE`, `WIN_*`, `ADDR_H`, `STATUS_H`, `URL_MAX`, `HOST_MAX`, `PATH_MAX_`, `PAGE_BUF_SIZE`, `HEADER_LINE_MAX`, `REQ_MAX`, `RECV_BUF_SIZE`, `MAX_NODES`, `MAX_BOXES`, `MAX_LINKS`, `MAX_INPUTS`, `MAX_FORMS`, `HIST_MAX`, `ATTR_POOL_SIZE`, all `T_*`, all `BK_*`, all `FOCUS_*`.
- The body of `main()` (lines 1988-2070 of pre-split file).

Rename `void main() { ... }` to `void browser_main() { ... }` in `main.cc` — the trampoline owns the real `main()`.

Banner at top: `/* ---------- Globals + entry point ---------- */`.

**Constants (`enum`) and globals must come BEFORE every other `#include` reference to them.** That means `main.cc` is included FIRST in the trampoline (right after `_smoke.cc` removal in Task 12).

But `browser_main()` calls into every other module — it must be defined LAST. Solution: split `main.cc` into two files? Or use the property that CupidC's parser allows globals/constants to be referenced from earlier-in-source-order code if they're in the same TU. **Verify in Step 11.4.**

If forward-reference of globals doesn't work in CupidC: split into `bin/browser/globals.cc` (all enum + globals, no functions) included FIRST, and `bin/browser/main.cc` (just `browser_main()`) included LAST.

- [ ] **Step 11.2: Reduce `bin/browser.cc` to trampoline**

Replace the entire contents of `bin/browser.cc` with:

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

Note: `browser/main.cc` is FIRST so its globals/enums are visible to all subsequent includes. The `void browser_main()` body is INSIDE `main.cc` and references functions defined in later includes — this works in CupidC because it concatenates includes into one TU and the parser allows forward references to functions (the existing single-file browser does exactly this between `main()` and earlier-defined helpers).

- [ ] **Step 11.3: Remove the smoke stub**

Delete `bin/browser/_smoke.cc`. Remove the `browser_smoke_token()` call from inside `browser_main()` in `bin/browser/main.cc` (added in Step 1.7). Remove `#include "browser/_smoke.cc"` from `bin/browser.cc` (it should already be absent from Step 11.2's rewrite; if not, delete it).

```bash
rm bin/browser/_smoke.cc
```

Edit `bin/browser/main.cc` to remove the lines:
```c
    if (browser_smoke_token() != 0xCAFE) {
        println("browser: smoke token mismatch — include broken");
        return;
    }
```
from the top of `browser_main()`.

- [ ] **Step 11.4: Build**

Run: `make 2>&1 | tail -20`

Expected: build succeeds.

If build fails with "undefined identifier" referring to a global from `main.cc` used in a later-included function (e.g. `cur_host` referenced in `net.cc`): CupidC does NOT forward-resolve globals from earlier-in-source. Fall back to splitting:

  1. Move all enum + global declarations from `bin/browser/main.cc` to a NEW file `bin/browser/globals.cc`.
  2. Leave only `void browser_main() { ... }` in `bin/browser/main.cc`.
  3. Update `bin/browser.cc`:
     ```c
     #include "browser/globals.cc"
     #include "browser/util.cc"
     #include "browser/url.cc"
     #include "browser/net.cc"
     #include "browser/dom.cc"
     #include "browser/parser.cc"
     #include "browser/layout.cc"
     #include "browser/paint.cc"
     #include "browser/nav.cc"
     #include "browser/input.cc"
     #include "browser/main.cc"

     void main() { browser_main(); }
     ```

  Then re-run `make`. If it now succeeds, commit the `globals.cc` extraction as part of Task 11 (don't make a separate commit — the split is a single logical change).

- [ ] **Step 11.5: Smoke test full functional set**

Run: `make run-net`. In QEMU shell:
1. `browser http://example.com/` — page renders identically to `/tmp/browser-pre-split.png`.
2. Click "More information..." link — navigates to IANA page.
3. Press Backspace — back to example.com.
4. Press Ctrl-L, type `https://example.com/`, Enter — TLS path loads.
5. Mouse wheel scrolls.

Quit QEMU.

- [ ] **Step 11.6: Commit**

```bash
git add bin/browser/main.cc bin/browser/globals.cc bin/browser.cc bin/browser/_smoke.cc
git rm bin/browser/_smoke.cc 2>/dev/null || true
git commit -m "$(cat <<'EOF'
browser: extract globals + entry to bin/browser/main.cc

Moves enum constants, globals (cur_url, page_buf, n_*, b_*, etc.)
and main() body (renamed browser_main) verbatim. bin/browser.cc
becomes pure #include trampoline.

Removes the bin/browser/_smoke.cc stub and its call site from
browser_main — pipeline proven, scaffolding no longer needed.

(If CupidC required splitting globals into bin/browser/globals.cc
to satisfy forward-reference rules, that split is also included.)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 12: Acceptance run

Final verification across multiple URLs. No code change.

- [ ] **Step 12.1: HTTP path on plain text site**

Run: `make run-net`. In shell: `browser http://example.com/`.
Expected: title "Example Domain", paragraph text, one underlined link, identical to pre-split reference.

- [ ] **Step 12.2: HTTPS / TLS path**

Same QEMU session: Ctrl-L, type `https://example.com/`, Enter.
Expected: TLS handshake succeeds (status bar does NOT show "TLS handshake failed"), page renders identically to HTTP version.

- [ ] **Step 12.3: Multi-page navigation + history**

Same QEMU session: Ctrl-L, type `http://news.ycombinator.com/`, Enter.
Expected: HN front page renders. Click any story link. Press Backspace — back to HN front page.

(Note: HN rendering is broken even pre-split because the existing layout doesn't handle tables. That's expected. The acceptance criterion is "broken in the same way" — i.e. the failure mode is identical to pre-split.)

- [ ] **Step 12.4: Form interaction**

Same QEMU session: Ctrl-L, type a URL with a form (e.g. `http://www.google.com/`). Click address-bar-style search input, type a query, press Enter. Form submits via GET, page navigates to results URL.

(Search results page itself may not render — that's pre-existing behavior, not a regression.)

- [ ] **Step 12.5: Verify file count**

```bash
ls bin/browser/
```
Expected list (alphabetical):
```
dom.cc
globals.cc          # only if Step 11.4 fallback was needed
input.cc
layout.cc
main.cc
nav.cc
net.cc
paint.cc
parser.cc
url.cc
util.cc
```
(No `_smoke.cc`.)

```bash
wc -l bin/browser.cc
```
Expected: ~12-15 lines (the trampoline).

```bash
wc -l bin/browser/*.cc
```
Expected: total roughly equals pre-split `bin/browser.cc` (≈2070 lines), distributed across files.

- [ ] **Step 12.6: Diff vs pre-split git revision**

```bash
git log --oneline | head -15
```
Expected: 11-12 commits since the start of this plan, one per task.

```bash
git diff main~12..HEAD -- bin/ Makefile kernel/bin_programs_gen.c | wc -l
```
Expected: a few hundred lines of net diff (mostly file-creation noise; the underlying logic should be byte-identical because every move was verbatim).

```bash
# Optional: compare the concatenation of split files to the original
cat bin/browser/util.cc bin/browser/url.cc bin/browser/net.cc \
    bin/browser/dom.cc bin/browser/parser.cc bin/browser/layout.cc \
    bin/browser/paint.cc bin/browser/nav.cc bin/browser/input.cc \
    bin/browser/main.cc | grep -v "^/\*" | grep -v "^$" | wc -l

git show main~12:bin/browser.cc | grep -v "^/\*" | grep -v "^$" | wc -l
```
Expected: line counts within ~5% of each other (small discrepancy from comment banners + the renamed `main` → `browser_main` + removed help comments at top of sub-files).

- [ ] **Step 12.7: Commit acceptance result (notes only)**

If all 6 prior steps pass without code changes, no commit needed — Plan 1 is complete. If you found a regression and patched it inline during acceptance, commit the patch separately:

```bash
git add <files>
git commit -m "$(cat <<'EOF'
browser: fix regression discovered in plan-1 acceptance

<one-line description of what broke and how>

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Self-Review (already done at plan-write time)

**Spec coverage:** Plan covers spec §0 "Build System: file split" entirely. Subsequent spec sections (§1 parser, §2 CSS, §3 render tree, §4 layout, §5 tables, §6 paint, §7 images, §8 memory limits, §9-11 net/forms/history) are explicitly out of scope for this plan and remain for follow-on plans. The file structure landed here matches §11 of the spec exactly except `bin/browser/image.cc` is deferred (YAGNI).

**Placeholder scan:** No "TBD", "implement later", or "fill in details". Two contingency branches exist (Step 4.4 fallback for forward-ref globals; Step 11.4 fallback for `globals.cc` split) — both have full code in the plan, not "do something appropriate".

**Type / signature consistency:** `browser_main()` defined in Task 11 / called in trampoline in Task 11; no signature mismatch. Helpers retain original C signatures throughout (verbatim moves).

---

## Risks & Notes for executor

1. **CupidC forward-reference behavior is the one unknown.** If `main.cc` (or `globals.cc`) globals can't be referenced from later-included functions, Task 11 falls back to the split layout. The fallback is fully specified, not "figure it out."

2. **No host-side test harness.** Every "verify" step is build-success + visual smoke. If you have a script that automates QEMU + serial-driven browser (none exists in tree as of plan write), prefer that.

3. **CupidC `cc_read_source` 256 KB cap per file.** All sub-files are well under 100 KB. Not a concern for this plan; will be a concern for the parser rewrite plan if any single sub-file balloons.

4. **Don't mix unrelated cleanup in.** This plan's invariant is **byte-identical behavior**. Any tempted "while I'm here" fix should be a separate commit on a follow-on plan, NOT folded in.

5. **If a task fails the smoke check**, do NOT proceed to the next task. The failure mode in the next task will be confused. Roll back the failing task with `git reset --hard HEAD~1`, diagnose, retry.
