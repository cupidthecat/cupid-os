# CupidOS Browser Redesign — WebKit-lite Render Tree

**Status:** design  •  **Date:** 2026-05-05  •  **Target:** v1 viewable HN + Wikipedia

## Goal

Replace the existing `bin/browser.cc` (single-pass DOM-into-flat-boxes, ~2070 lines) with a WebKit2-shaped pipeline: **DOM → Style → Render Tree → Layout → Paint**. Three trees instead of one, fixing root cause of every current rendering bug (inline runs across block boundaries, broken `<table>` layout, missing whitespace normalization, no real CSS).

Network stack, DNS, TLS, sockets, GUI, image decoders are already in place — none need changes. Browser is pure rewrite of the rendering pipeline plus a build-system change to split source across files.

## Non-Goals (v1)

- JavaScript / DOM events
- Canvas, video, audio, SVG
- WebSocket, XHR, fetch, cookies, localStorage
- Float / flex / grid layouts
- Border-collapse on tables; `colgroup`
- Pseudo-classes (`:hover`/`:active`), `[attr=v]`, `>`/`+`/`~` selectors
- POST forms (GET only in v1)
- Forward history; only Back

## Architecture

```
URL ─► Loader (HTTP/HTTPS via existing socket+TLS+DNS)
         │
         ▼
   page_buf (raw bytes)
         │
         ▼
   HTML5-lite Tokenizer → Tree Builder → DOM
         │                                │
                                          ▼
                              UA + Author Stylesheets
                                          │
                              Style Resolver (cascade,
                              specificity tag<.class<#id<inline)
                                          │
                                          ▼
                       Render Tree (skips display:none, attaches
                       ComputedStyle, classifies block / inline /
                       list-item / table-* ; injects anonymous
                       wrappers per CSS 2.1)
                                          │
                                          ▼
                       Layout: BFC + IFC + line boxes;
                       table 3-pass auto layout
                                          │
                                          ▼
                       Paint (bg → border → content)
                                          │
                                          ▼
                       GUI window + hit testing + scroll
                       Background image loader (same-host, cap 8)
```

Each tree has one job. DOM is structure; render tree is renderable structure × computed style; layout produces geometry; paint reads geometry.

## Build System: file split

CupidC already supports `#include` (resolved relative to including file's dir, with cycle guard, in `kernel/cupidc.c`). No compiler change needed.

### Source layout

```
bin/browser.cc                — entry; #includes sub-files; main()
bin/browser/util.cc           — string helpers
bin/browser/url.cc            — parse_url, resolve_redirect, compute_url_relative
bin/browser/net.cc            — fetch_url (HTTP/HTTPS, redirects)
bin/browser/dom.cc            — DOM arrays, attr pool, allocators
bin/browser/parser.cc         — HTML5-lite tokenizer + tree builder
bin/browser/css.cc            — CSS lexer + selector + rule table
bin/browser/style.cc          — UA stylesheet, resolver, ComputedStyle
bin/browser/render_tree.cc    — RT build w/ anonymous wrappers
bin/browser/layout.cc         — BFC + IFC + line boxes
bin/browser/table.cc          — table grid + 3-pass layout
bin/browser/paint.cc          — paint, hit-test, chrome
bin/browser/image.cc          — same-host queue, fetch, decode
bin/browser/input.cc          — keyboard + mouse + focus
bin/browser/nav.cc            — navigate, history, form submit
```

`bin/browser.cc`:
```c
//help: Web browser. Renders HTTP and HTTPS pages in a window.
//help: Usage: browser [url]
#include "browser/util.cc"
#include "browser/url.cc"
#include "browser/net.cc"
#include "browser/dom.cc"
#include "browser/parser.cc"
#include "browser/css.cc"
#include "browser/style.cc"
#include "browser/render_tree.cc"
#include "browser/layout.cc"
#include "browser/table.cc"
#include "browser/paint.cc"
#include "browser/image.cc"
#include "browser/input.cc"
#include "browser/nav.cc"
void main() { browser_main(); }
```

### Makefile changes

1. Pattern rule for sub-files (objcopy embed):
   ```make
   bin/browser/%.o: bin/browser/%.cc
       objcopy -I binary -O elf32-i386 -B i386 $< $@
   ```
2. Discover sub-files: `BROWSER_SUB_SRCS := $(wildcard bin/browser/*.cc)`, `BROWSER_SUB_OBJS := $(BROWSER_SUB_SRCS:.cc=.o)`, `BROWSER_SUB_NAMES := $(notdir $(BROWSER_SUB_SRCS:.cc=))`. Add `$(BROWSER_SUB_OBJS)` to the kernel link list (same place as `$(BIN_CC_OBJS)`).
3. Extend `kernel/bin_programs_gen.c` generator:
   - Iterate `BROWSER_SUB_NAMES` (basename of each sub-file)
   - Emit `extern` decls and `ramfs_add_file(fs_private, "bin/browser/<n>.cc", ...)` calls
   - **Do not** add to `BIN_CC_NAMES` (sub-files are library, not runnable)
4. Update `clean` rule to remove `bin/browser/*.o`.

Result: at boot, ramfs has `/bin/browser.cc` (runnable) plus `/bin/browser/*.cc` (library files). When user runs `browser`, JIT preprocesses `/bin/browser.cc`, follows each `#include "browser/*.cc"` via the existing path resolver (relative to `/bin/`), and compiles the concatenated output.

## §1. HTML5-lite Tokenizer + Tree Builder

### Tokenizer

State machine over `page_buf`. States:
- `Data`, `TagOpen`, `EndTagOpen`, `TagName`
- `BeforeAttrName`, `AttrName`, `AfterAttrName`, `BeforeAttrValue`
- `AttrValueDoubleQuoted`, `AttrValueSingleQuoted`, `AttrValueUnquoted`
- `SelfClosingStartTag`
- `MarkupDeclarationOpen` → `Comment` (drop) / `Doctype` (drop)
- `RAWTEXT` for `<script>`/`<style>` — only `</script>` / `</style>` exits; entities not decoded
- `RCDATA` for `<title>`/`<textarea>` — same exits, entities decoded

Tokens emitted via parallel arrays:
```
tok_kind[]    // START, END, TEXT, EOF
tok_tag[]     // T_* enum
tok_attr_first[], tok_attr_count[]   // index into (name_off, value_off) pool
tok_text_off[], tok_text_len[]        // into page_buf
tok_self_close[]
```

### Entities

Extend `decode_entities` to ~50 named entries used by Wikipedia: `amp`, `lt`, `gt`, `quot`, `apos`, `nbsp`, `mdash`, `ndash`, `hellip`, `lsquo`/`rsquo`/`ldquo`/`rdquo`, `middot`, `bull`, `copy`, `reg`, `trade`, `sect`, `para`, `deg`, `times`, `divide`, `plusmn`, `frac12`/`frac14`/`frac34`, `larr`/`rarr`/`uarr`/`darr`, `laquo`/`raquo`, `iexcl`, `iquest`, `cent`, `pound`, `yen`, `euro`, `nbsp`. Numeric `&#N;` and `&#xH;`: ASCII range → literal char; NBSP (160) → space; soft hyphen (173) → drop; other non-ASCII → `?`.

### Tree Builder

Open-elements stack + simplified insertion modes: `InHead`, `InBody`, `InTable`, `InRow`, `InCell`, `InCaption`. Implicit-close rules:

| Trigger | Closes |
|---|---|
| `<p>` while `<p>` open | prior `<p>` |
| any block start while `<p>` open | prior `<p>` |
| `<li>` while `<li>` open | prior `<li>` |
| `<dt>`/`<dd>` while either open | prior `<dt>`/`<dd>` |
| `<tr>` while `<tr>` open | prior `<tr>` |
| `<td>`/`<th>` while `<td>`/`<th>` open | prior cell |
| `</table>` | any open `<tr>`/`<td>`/`<th>` |
| `<option>` while `<option>` open | prior `<option>` |
| `<body>` or any body-level start while in `<head>` | `<head>` |
| `<a>` while `<a>` open | prior `<a>` (HN sometimes nests) |

Stray text inside `<table>` outside cells: drop (foster parenting simplified). EOF closes all open elements.

### DOM

Parallel arrays (existing convention; CupidC has structs but parallel arrays match codebase):
```
dom_tag[], dom_parent[], dom_first_child[], dom_next[]
dom_text_off[], dom_text_len[]                    // T_TEXT only
dom_attrs_first[], dom_attrs_count[]              // (name_off, value_off) pairs
dom_class_off[], dom_id_off[]                     // cached for selector matching
```

Attribute storage replaces current per-attr fields (`n_href`, `n_src`, `n_color`, ...) with a generic `(name_off, value_off)` pair pool. Style/render code fetches by name.

## §2. CSS

### Selectors supported

- Tag: `p`, `h1`
- Class: `.foo`
- Id: `#bar`
- Comma list: `a, b`
- Descendant: `A B` (any depth)

Not supported: `>`, `+`, `~`, `[attr]`, `:pseudo`, universal `*`. Unsupported selectors parse-skip silently.

### Properties supported

`color`, `background-color`, `background` (color only), `font-weight`, `font-style`, `font-size`, `text-align`, `text-decoration`, `display`, `margin`/`-top`/`-right`/`-bottom`/`-left`, `padding`/`-...`, `border` (1px solid color only), `border-color`, `width`, `height`, `white-space`, `list-style-type`, `vertical-align`. Anything else: ignored silently.

### Units

`px`, `pt` (×4/3), `em` (×base font), `%` (only for table widths). Other units: ignored.

### Cascade

Order: UA stylesheet < `<style>` rules < `style=` attribute. Within author rules: specificity `(id_count, class_count, tag_count)`; ties broken by document order. Inheritable properties (`color`, `font-*`, `text-align`, `white-space`, `list-style`) inherit from parent when unset.

### UA stylesheet

Hardcoded as a switch over `dom_tag` setting defaults: `h1..h6` (block, bold, sized 4..1, margins), `p` (block, margin 8 0 8 0), `a` (inline, color blue, underline), `pre` (block, white-space pre, monospace), `code` (inline, monospace), `b`/`strong` (inline, bold), `i`/`em` (inline, italic), `ul`/`ol` (block, padding-left 24), `li` (list-item, marker), `table` (display table), `tr` (table-row), `td`/`th` (table-cell, padding 2), `th` (bold, text-align center), `hr` (block, height 1, bg gray, margin 8 0), `blockquote` (block, padding-left 16), `script`/`style`/`noscript` (display:none), `head` (display:none).

### `<style>` parser

Single pass over each `<style>` block. Output: flat `rule[]` array of `(selector_chain, property_idx[], value_off[])`. Cap **256 rules** total per page; further rules dropped.

ComputedStyle pool: 4096 entries, allocated at resolver pass. Each render node references one.

## §3. Render Tree

Built once per page after style resolution. Walks DOM, emits render nodes:

- Skip `display: none` subtrees
- Skip `<head>`, `<script>`, `<style>` content always
- **Anonymous block wrappers**: when a block parent has mixed inline+block children, contiguous inline runs are wrapped in a synthetic anonymous block render node. Fixes "inline runs across block boundaries" bugs.
- **Anonymous table ancestors** (CSS 2.1 §17.2.1): bare `<td>` outside `<tr>` synthesizes anon `<tr>`; bare `<tr>` outside `<table>` synthesizes anon `<table>`.
- **List-item marker** node (`LIST_MARKER`) injected before content of each `<li>`. Content is bullet glyph or computed numeric string.

Render node fields (parallel arrays):
```
rt_dom[]         // back-pointer; -1 for anonymous
rt_parent[], rt_first_child[], rt_next[]
rt_kind[]        // BLOCK, INLINE, INLINE_BLOCK, LIST_ITEM, LIST_MARKER,
                 // TABLE, TABLE_ROW_GROUP, TABLE_ROW, TABLE_CELL,
                 // TABLE_CAPTION, TEXT, REPLACED, LINE_BOX
rt_style[]       // ComputedStyle pool index
rt_text_off[], rt_text_len[]      // TEXT only
rt_intrinsic_w[], rt_intrinsic_h[]  // REPLACED only

// Filled by layout:
rt_x[], rt_y[], rt_w[], rt_h[]
rt_content_x[], rt_content_y[]
rt_baseline[]
```

Pool: 6144 render nodes.

## §4. Layout — BFC + IFC

### Block Formatting Context

```
layout_block(node, available_w):
    // resolve own width
    if style.width != AUTO: node.w = style.width
    else: node.w = available_w
    cx = padding_l + border_l
    cy = padding_t + border_t
    pending_inline = []
    for child in node.children:
        if child.kind in {BLOCK, LIST_ITEM, TABLE, anon-block}:
            flush_inline(pending_inline, cx, &cy, content_w)
            child.w = resolve_child_width(child, content_w)
            layout_block(child, child.w)
            child.x = cx + child.style.margin_l
            child.y = cy + child.style.margin_t
            cy += child.style.margin_t + child.h + child.style.margin_b
        else if child.kind == TABLE_*:
            flush_inline(...)
            layout_table(child, content_w)
            place as block
        else:
            pending_inline.append(child)
    flush_inline(pending_inline, cx, &cy, content_w)
    node.h = cy + padding_b + border_b
```

Margin-collapse is **off** in v1 (margins simply sum). Adds vertical spacing close enough to expectations for HN/Wikipedia at the cost of slight over-spacing between consecutive blocks. Can enable later.

### Inline Formatting Context

`flush_inline(atoms, cx, &cy, max_w)` walks the inline subtree depth-first, splits text into atoms by whitespace (or per-`\n`/per-char in `pre`/`nowrap`), tracks `(x, line_top, line_h)`:

```
for atom in walk_inline(subtree):
    aw = atom.text_w_for(font, atom.text)
    if x + aw > max_w and x > cx:
        emit_line_box(line_atoms, line_top, line_h)
        x = cx; line_top += line_h; line_h = atom.font.line_h
    place atom at (x, line_top); x += aw
    line_h = max(line_h, atom.font.line_h)
emit_line_box(remaining, line_top, line_h)
*cy = line_top + line_h
```

LineBox = synthetic `LINE_BOX` render node child, holding per-atom `(x_offset, w, font_tier, fg, bg, link_idx, text_off, text_len, bold, underline)`. `line_h` derives from the atom's `style.font_size` tier (8x8 → 12, 12x12 → 16, 16x16 → 20, 24x24 → 28). Atoms inherit nested-inline styles (e.g. `<a><b>foo</b></a>` produces an atom with `bold + link`).

### List markers

`LIST_MARKER` painted in the `padding_l` reservation at fixed offset (-16 px from content_x). Content: `•` for `disc`, `o` for `circle`, `■` for `square`, `1.` `2.` ... computed from sibling-index for `decimal` (auto-counts non-marker siblings).

### White-space

Per `style.white_space`:
- `normal`: collapse runs of `\s` to single space; `\n` is a space
- `pre`: keep all whitespace; `\n` forces line break; tabs → 8 spaces
- `nowrap`: collapse like normal but never break

## §5. Tables — 3-pass auto layout

### Pass 0 — Grid materialization

Walk `<table>` subtree, produce grid of cells resolving `colspan`/`rowspan`:
- Place each `<td>`/`<th>` in next free `(row, col)` slot scanning L→R
- Mark spanned slots occupied
- `<thead>`/`<tbody>`/`<tfoot>` flatten in document order
- `<caption>` extracted; laid out as block above table

Caps: `MAX_TABLE_ROWS = 256`, `MAX_TABLE_COLS = 32`. Cells beyond drop.

### Pass 1 — Column widths

For each cell compute `min_content_w` (longest unbreakable atom) and `max_content_w` (everything one-line). Spanning cells distribute proportionally across spanned columns (CSS 2.1 §17.5.2).

Per column `c`:
- `col_min[c]` = max over non-spanning cells; spanning cells contribute `(cell_min - sum_other_cols) / span_count` to each spanned column
- `col_max[c]` similar

Resolve against `available_w`:
- If `Σcol_max ≤ available_w`: use `col_max`
- Else if `Σcol_min ≤ available_w`: distribute slack proportional to `(col_max - col_min)`
- Else: use `col_min`, accept overflow

### Pass 2 — Cell content layout

Each cell at `(r, c)` span `(rs, cs)`: width = `Σ col_w[c..c+cs]`. Run `layout_block(cell, width)`. Record `cell_h`.

### Pass 3 — Row positioning

Stack rows top-to-bottom. Row `r` height = `max(cell_h)` over cells *ending* at row `r` (i.e. `row_start + row_span - 1 == r`). Spanning cell final h = `Σ row_h[r..r+rs]`. `vertical-align: top|middle|bottom` shifts content within cell box.

Border-collapse: not implemented; cells render with their own borders.

## §6. Paint, Hit Test, Input

### Paint

```
paint(node, scroll_y):
    if node.y + node.h - scroll_y < 0 or node.y - scroll_y > viewport_h:
        return  // off-screen, skip
    fill bg over (node.x, node.y, node.w, node.h)
    draw 1px borders flagged
    if node.kind == TEXT:
        gfx2d_text(node.x, node.y, ...)
    else if node.kind == REPLACED:
        if image handle valid: gfx2d_image_draw_scaled(...)
        else: draw placeholder rect
    else if node.kind == LIST_MARKER:
        draw bullet/number glyph
    else if node.kind == LINE_BOX:
        for atom in atoms: draw atom text/underline
    for child in node.children:
        paint(child, scroll_y)
```

Single source of geometry truth = render tree (not a flat box list).

### Hit testing

Reverse depth walk; deepest node containing `(mx, my + scroll_y)` wins. Returns the link node (`<a>`), input, or button hit.

### Font sizing

One 8x8 ASCII bitmap font today. Tier mapping via `gfx2d` integer scale:
- tier 0..1: 8x8 normal
- tier 2: 12x12 (1.5×, used for `h4`)
- tier 3: 16x16 (2×, used for `h2`/`h3`)
- tier 4: 24x24 (3×, used for `h1`)

Bold = double-render with 1px x-offset. Italic = optional skew via per-row pixel shift; fallback to underline.

### Address bar, status bar, scrollbar

Same as today: top 24px address, bottom 18px status, right 12px scrollbar. Ported to render-tree-backed layout but visually unchanged.

## §7. Images

After initial layout + first paint:
1. Walk render tree collecting `REPLACED` nodes whose URL host matches current page host (case-insensitive). Drop cross-host.
2. Cap queue at 8, document-order priority.
3. Sequential fetch loop:
   ```
   for url in queue:
       if !fetch_url(url, &ct, &buf, &len): continue
       if ct contains "png": handle = png_decode(...)
       else if "jpeg" or "jpg": handle = jpeg_decode(...)
       else if "bmp": handle = bmp_decode(...)
       else: continue
       store handle on render node
       repaint
   ```

Box dimensions stay fixed at intrinsic-from-attrs (`width`/`height`) or 80×60 placeholder. No relayout on decode (deferred to v1.5). User can keep scrolling/clicking; UI freezes per fetch (single-thread). Press `Esc` aborts page (existing behavior).

## §8. Memory & Limits

| Pool | Current | v1 |
|---|---|---|
| `page_buf` | 96 KB | 512 KB |
| `attr_pool` | 16 KB | 128 KB |
| `MAX_NODES` (DOM) | 1024 | 4096 |
| `MAX_RT_NODES` | — | 6144 |
| ComputedStyle pool | — | 4096 |
| `MAX_LINKS` | 256 | 1024 |
| `MAX_INPUTS` | 16 | 64 |
| `MAX_FORMS` | 8 | 32 |
| Author CSS rules | — | 256 |
| Table grid | — | 256 × 32 |
| Image cache slots | — | 8 |
| `HIST_MAX` | 8 | 16 |

Pool exhaustion: drop further entries silently; set `status_msg` to `truncated` once. Net static footprint ≈ +1.5 MB, well within CupidC data base (16 MB).

## §9. Network, TLS, DNS, Sockets

**No change.** `fetch_url` ported as-is from current code into `bin/browser/net.cc`; uses existing `socket()`, `connect()`, `setsockopt(SOL_TLS,...)`, `recv()`, `send()`, `dns_resolve()` syscalls. Redirect handling (5 max) unchanged.

## §10. Forms

GET only in v1. URL-encode each input value, build `?k1=v1&k2=v2`, navigate. Same logic as today, rebound to render-tree input nodes.

Submit triggers: Enter in focused input; click on submit button (`<input type=submit>` or `<button>`).

## §11. History

Ring buffer of 16 URLs. Backspace in page focus → pop, navigate. Forward not implemented in v1.

## §12. Open Risks

| Risk | Mitigation |
|---|---|
| CupidC JIT can't compile concatenated source over its source-size cap | Spec assumes `cc_preprocess_source` already handles this; verify with one large `#include` chain before splitting; fallback = collapse two adjacent files into one |
| Wikipedia pages exceed 512 KB after redirect | Truncate gracefully; show partial page with status `truncated` |
| Real CSS from Wikipedia explodes 256-rule cap | Drop overflow rules; UA defaults still apply |
| Table layout O(R·C·cells) on big tables | Caps at 256×32 already; benchmark on Wikipedia infobox |
| Images hang the UI thread on slow connection | v1 accepts this; v1.5 adds `recv()` poll w/ Esc check |
| `bin/browser/*.cc` ramfs paths collide with future runnable programs | Convention: `bin/browser/` reserved for browser library; runnable programs stay flat in `bin/` |

## §13. Test Plan

No in-OS test harness exists. Acceptance via QEMU run against real sites:

| # | URL | What it exercises |
|---|---|---|
| 1 | `http://example.com/` | Sanity: heading, paragraph, link |
| 2 | `http://info.cern.ch/hypertext/WWW/TheProject.html` | Historic; many links, lists |
| 3 | `http://news.ycombinator.com/` | Tables for layout, dense links |
| 4 | `http://lite.cnn.com/` | Text-heavy, paragraph spacing |
| 5 | `https://en.wikipedia.org/wiki/HTTP` | TLS + infobox tables + lists + headings + footnotes (boss fight) |
| 6 | `https://news.ycombinator.com/` | TLS path |
| 7 | `bin/browser_test.html` (synthetic) | Nested inline, `<pre>`, lists, table colspan/rowspan, form, img 404, CSS selectors |

Manual visual checklist per page:
- Title in window bar
- Headings sized + spaced
- Paragraphs spaced; no run-together text
- Links blue + underlined
- Lists indented with markers
- Tables aligned, no cell overlap, colspan/rowspan honored
- Scroll smooth via wheel + scrollbar drag + arrows + PgUp/Dn
- Click navigates; Backspace goes back
- Address bar editable on Ctrl-L
- TLS pages load (status doesn't say `TLS handshake failed`)

Regression: `curl <url>` (existing program, same `bin/curl.cc`) must still work — only browser layer changes.

## §14. Out of Scope for v1, candidates for v1.5+

- Margin-collapsing
- Float (`float: left|right`)
- Border-collapse on tables
- POST forms, `<select>`, `<textarea>` content
- Forward history
- Image relayout on intrinsic-size discovery
- Concurrent image fetch (requires non-blocking sockets)
- Real Unicode font (current 8x8 ASCII only)
- Cookies, localStorage, cache
- `<details>`/`<summary>`, `<dialog>`
