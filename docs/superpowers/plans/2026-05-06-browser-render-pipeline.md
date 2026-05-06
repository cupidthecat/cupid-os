# Browser Render Pipeline — Implementation Plan (Plan 2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current single-pass DOM-into-flat-boxes browser rendering with the WebKit-lite pipeline from spec §1+§2+§3+§4: HTML5-lite tokenizer + tree builder, CSS lexer + UA stylesheet + author cascade, render tree with anonymous wrappers and list-item markers, layout via BFC + IFC + line boxes, render-tree-driven paint and hit testing.

**Architecture:** Three trees with single responsibilities. DOM is structure (output of parser). Render tree is renderable structure × computed style (output of style resolver, with anonymous wrappers + markers). Layout consumes the render tree to fill geometry fields. Paint reads geometry from the render tree, never from a separate flat-box list. The current `boxes[]`/`L_*`/`render()` machinery is deleted at cutover.

**Tech Stack:** CupidC (in-OS JIT), kernel `gfx2d_*` / `gui_win_*` / socket bindings (unchanged), QEMU smoke testing against live HTTP/HTTPS endpoints.

**Reference spec:** `docs/superpowers/specs/2026-05-05-browser-redesign.md` §1, §2, §3, §4, §6, §8 (full coverage); §10, §11 (rebind only). §5 (tables) and §7 (images) are deferred to Plan 3 — this plan ships with table elements rendered as block placeholders and `<img>` painted as 80×60 placeholder rectangles.

**Branch:** `browser-render-pipeline`, off `browser-build-split`.

**Testing reality:** No host-side unit-test framework for browser code. Each task verifies via:
- **Build check** — `make 2>&1 | tail -5` succeeds; install lines for any new sub-files appear in serial log.
- **Boot check** — `timeout 25 make run-headless 2>&1 | grep "Installed /bin/browser"` lists every sub-file.
- **Smoke check** — `make run-net`, in shell `browser http://example.com/`, then progressively richer URLs as the pipeline grows.

For each task that changes parser, DOM, render tree, layout, or paint, the test is a side-by-side visual comparison against the pre-Plan-2 reference screenshots captured in Step 0.1.

---

## File Structure

After this plan completes:

```
bin/browser.cc                — entry; #includes sub-files; main() trampoline (≈20 LOC)
bin/browser/util.cc           — string helpers, ASCII/hex (UNCHANGED from Plan 1)
bin/browser/url.cc            — parse_url, resolve_redirect (UNCHANGED)
bin/browser/net.cc            — fetch_url (UNCHANGED)
bin/browser/dom.cc            — DOM arrays, attr-pair pool, alloc_node, attr_intern,
                                attr_get_by_name, decode_entities (REWRITTEN: removes per-attr
                                fields, replaces with name-keyed pair pool)
bin/browser/parser.cc         — HTML5-lite tokenizer + tree builder (REWRITTEN)
bin/browser/css.cc            — CSS lexer, selector parser, rule table (NEW)
bin/browser/style.cc          — UA stylesheet, ComputedStyle pool, style resolver (NEW)
bin/browser/render_tree.cc    — RT build, anonymous wrappers, list-item markers (NEW)
bin/browser/layout.cc         — layout_block, flush_inline, white-space (REWRITTEN: BFC+IFC)
bin/browser/paint.cc          — render-tree paint, hit_box, address/status/scrollbar
                                (REWRITTEN to read render tree)
bin/browser/input.cc          — keyboard + mouse, rebound to render-tree hit testing
                                (lightly modified)
bin/browser/nav.cc            — navigate, go_back, submit_form, rebound to new DOM
                                (lightly modified)
bin/browser/main.cc           — globals (sized per §8), browser_main(); adds new pools
                                (modified: pool size bumps, new arrays)
```

No new sub-files for tables or images — table elements layout as anonymous blocks; `<img>` paints as a placeholder. Plan 3 introduces `bin/browser/table.cc` and `bin/browser/image.cc`.

**Trampoline include order** (after Plan 2 completes):
```c
//help: ...

#include "browser/main.cc"        // globals + enums first
#include "browser/util.cc"
#include "browser/url.cc"
#include "browser/net.cc"
#include "browser/dom.cc"
#include "browser/parser.cc"
#include "browser/css.cc"
#include "browser/style.cc"
#include "browser/render_tree.cc"
#include "browser/layout.cc"
#include "browser/paint.cc"
#include "browser/nav.cc"
#include "browser/input.cc"

void main() { browser_main(); }
```

The order is bottom-up: each module's references only point earlier (or to globals in `main.cc`). `paint.cc` reads style from `style.cc`, structure from `render_tree.cc`, geometry filled by `layout.cc`. `nav.cc` and `input.cc` come last because they call into every other module.

---

## Pre-flight

- [ ] **Step 0.1: Capture pre-Plan-2 visual references**

Run: `make run-net`. In the QEMU shell, navigate to each URL below and screenshot the rendered page. Save under `/tmp/browser-pre-p2/<n>.png`:

| File | URL | What it captures |
|---|---|---|
| `01-example.png` | `http://example.com/` | Sanity baseline (heading, paragraph, link) |
| `02-iana.png` | `https://www.iana.org/help/example-domains` | TLS path, larger body, list |
| `03-info-cern.png` | `http://info.cern.ch/hypertext/WWW/TheProject.html` | Historic, dense link list |
| `04-cnn-lite.png` | `http://lite.cnn.com/` | Text-heavy paragraphs |
| `05-hn.png` | `http://news.ycombinator.com/` | Tables (will look broken in Plan 2 — placeholder fallback expected; capture for visual diff in Plan 3) |

These are oracles for "no regression on the parts the new pipeline already handles." Plan 2 should match or improve every reference except `05-hn.png` (which the new pipeline reuses as a "broken in the same way" baseline).

Quit QEMU.

- [ ] **Step 0.2: Confirm clean build on `browser-render-pipeline`**

Run: `git status` and confirm clean working tree, branch is `browser-render-pipeline`.
Run: `git log --oneline browser-build-split..HEAD` — expect no commits yet (this branch's first commit lands in Task 1).
Run: `make 2>&1 | tail -5` — build succeeds.
Run: `timeout 25 make run-headless 2>&1 | tail -20` — boot completes through "Installed /bin/browser/*.cc" lines.

If any check fails, fix before proceeding. Plan 2 commits land on top of this baseline.

---

## Task 1: §8 — Memory pool bumps + new constants

Bump pool capacities to spec §8 sizes and introduce constants the parser/style/layout pipeline will need. This task only edits constants and array bounds; no logic changes. Build and smoke verify before proceeding.

**Files:**
- Modify: `bin/browser/main.cc` (constants block at top of file; array declarations)

- [ ] **Step 1.1: Bump existing pool sizes**

Edit the enum at the top of `bin/browser/main.cc` (the block that declares `URL_MAX`, `PAGE_BUF_SIZE`, `MAX_NODES`, etc.). Set:

| Constant | Old | New |
|---|---|---|
| `PAGE_BUF_SIZE` | 96000 | 524288 (512 KB) |
| `ATTR_POOL_SIZE` | 16384 | 131072 (128 KB) |
| `MAX_NODES` | 1024 | 4096 |
| `MAX_LINKS` | 256 | 1024 |
| `MAX_INPUTS` | 16 | 64 |
| `MAX_FORMS` | 8 | 32 |
| `HIST_MAX` | 8 | 16 |

Leave `URL_MAX`, `HOST_MAX`, `PATH_MAX_`, `HEADER_LINE_MAX`, `REQ_MAX`, `RECV_BUF_SIZE`, `MAX_BOXES` unchanged for now — `MAX_BOXES` is removed entirely in Task 11 along with the `boxes[]` array.

- [ ] **Step 1.2: Add new constants for tokenizer, render tree, style resolver**

Append to the same enum block:

```c
    /* §1 tokenizer scratch */
    MAX_TOKENS = 16384,
    MAX_ATTR_PAIRS = 8192,

    /* §3 render tree pool */
    MAX_RT_NODES = 6144,

    /* §2 style/CSS */
    MAX_CSS_RULES = 256,
    MAX_COMPUTED_STYLES = 4096,
    CSS_VALUE_POOL_SIZE = 32768,

    /* §1 entity table */
    MAX_ENTITY_NAMES = 64,

    /* §4 layout scratch — pending inline atoms before line-box flush */
    MAX_PENDING_INLINE = 1024,
```

`MAX_TOKENS`/`MAX_ATTR_PAIRS` are sized for the largest test pages (Wikipedia HTTP article ≈12 K tokens, ≈6 K attr pairs measured offline). `MAX_RT_NODES = 6144` matches spec §3 directly. `MAX_COMPUTED_STYLES = 4096` matches spec §2. `MAX_CSS_RULES = 256` matches spec §2 cap.

- [ ] **Step 1.3: Build + smoke + commit**

Run: `make 2>&1 | tail -5`
Expected: build succeeds. `bin/browser/main.cc` recompiles; total kernel BSS grows by ~470 KB (mostly `page_buf`). Confirm via `objdump -h kernel/kernel.elf | grep -A1 ".bss"` if curious; not required for pass.

Run: `make run-net`, browse `http://example.com/`, match `/tmp/browser-pre-p2/01-example.png`. Quit.

```bash
git add bin/browser/main.cc
git commit -m "$(cat <<'EOF'
browser: bump pool sizes per spec §8 and add Plan-2 capacity constants

page_buf 96K → 512K, attr_pool 16K → 128K, MAX_NODES 1024 → 4096,
MAX_LINKS 256 → 1024, MAX_INPUTS 16 → 64, MAX_FORMS 8 → 32, HIST_MAX
8 → 16. Adds capacity constants the upcoming parser/style/RT pipeline
will allocate against (MAX_TOKENS, MAX_ATTR_PAIRS, MAX_RT_NODES,
MAX_CSS_RULES, MAX_COMPUTED_STYLES, CSS_VALUE_POOL_SIZE).

No logic change; Plan 2 prep.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: §1 Tokenizer — replace `parse_html`'s lex with state-machine tokenizer

Replace the ad-hoc lexer at the head of `parse_html` with a proper HTML5-lite tokenizer that produces a typed token stream. This task only adds the tokenizer; the existing tree-builder logic in `parse_html` keeps reading from `page_buf` directly until Task 3 swaps it to consume tokens. The tokenizer runs as a new function `tokenize(html_len)` and writes to scratch arrays; it is called but its output is discarded for one task to keep the diff isolated.

**Files:**
- Modify: `bin/browser/main.cc` (add token + attr-pair scratch arrays)
- Modify: `bin/browser/parser.cc` (add `tokenize` and helpers; do not yet rewire `parse_html`)

- [ ] **Step 2.1: Add token + attr-pair scratch arrays to `main.cc`**

In the globals section of `bin/browser/main.cc`, immediately after the existing `attr_pool[]`/`attr_pool_pos` declarations, add:

```c
/* §1 tokenizer scratch — populated by tokenize(), consumed by tree builder in Task 3 */
int  tok_count;
int  tok_kind     [MAX_TOKENS];   /* TK_START, TK_END, TK_TEXT, TK_EOF */
int  tok_tag      [MAX_TOKENS];   /* T_* enum, only meaningful for START/END */
int  tok_attr_first[MAX_TOKENS];  /* index into ap_* arrays */
int  tok_attr_count[MAX_TOKENS];
int  tok_text_off [MAX_TOKENS];   /* offset into page_buf or ctype_buf */
int  tok_text_len [MAX_TOKENS];
int  tok_self_close[MAX_TOKENS];

/* attr-pair pool: name and value byte-offsets into attr_pool */
int  ap_count;
int  ap_name_off  [MAX_ATTR_PAIRS];
int  ap_value_off [MAX_ATTR_PAIRS];
```

Add token-kind constants to the enum near the top:

```c
    TK_START = 1,
    TK_END   = 2,
    TK_TEXT  = 3,
    TK_EOF   = 4,
```

- [ ] **Step 2.2: Add tokenizer state constants to `parser.cc`**

At the very top of `bin/browser/parser.cc` (after the banner comment), add a private state enum. CupidC scopes enum constants to the TU, so this does not collide with anything else:

```c
enum {
    ST_DATA = 0,
    ST_TAG_OPEN,
    ST_END_TAG_OPEN,
    ST_TAG_NAME,
    ST_BEFORE_ATTR,
    ST_ATTR_NAME,
    ST_AFTER_ATTR_NAME,
    ST_BEFORE_VALUE,
    ST_VALUE_DQ,
    ST_VALUE_SQ,
    ST_VALUE_UQ,
    ST_SELF_CLOSE,
    ST_MARKUP_DECL,    /* sees "<!" -- decide between Comment and Doctype */
    ST_COMMENT,        /* skip to "-->" */
    ST_DOCTYPE,        /* skip to ">" */
    ST_RAWTEXT,        /* used inside <script>/<style> */
    ST_RCDATA          /* used inside <title>/<textarea> */
};
```

- [ ] **Step 2.3: Implement core tokenizer skeleton (Data, TagOpen, TagName)**

Define `tokenize(int html_len)` in `parser.cc`. This first cut handles only the four most common transitions: text runs, start-tag open, end-tag open, tag-name accumulation. Attributes, entities, RAWTEXT/RCDATA, comments, doctypes are stubs that just skip ahead. Each later step in this task fills one of those in.

```c
int emit_token(int kind, int tag, int text_off, int text_len,
               int attr_first, int attr_count, int self_close) {
    if (tok_count >= MAX_TOKENS) return -1;
    tok_kind[tok_count]      = kind;
    tok_tag[tok_count]       = tag;
    tok_text_off[tok_count]  = text_off;
    tok_text_len[tok_count]  = text_len;
    tok_attr_first[tok_count]= attr_first;
    tok_attr_count[tok_count]= attr_count;
    tok_self_close[tok_count]= self_close;
    return tok_count++;
}

void tokenize(int html_len) {
    tok_count = 0;
    ap_count  = 0;
    int state = ST_DATA;
    int i = 0;
    int text_start = 0;
    int tag_start = 0;
    int tag_is_end = 0;
    int cur_tag = 0;            /* T_* during ST_TAG_NAME */
    int cur_attr_first = 0;
    int cur_attr_count = 0;
    int cur_self_close = 0;

    while (i <= html_len) {
        int c = (i < html_len) ? (page_buf[i] & 0xFF) : -1;

        if (state == ST_DATA) {
            if (c == '<') {
                if (i > text_start) {
                    emit_token(TK_TEXT, 0, text_start, i - text_start, 0, 0, 0);
                }
                state = ST_TAG_OPEN;
                tag_is_end = 0;
                cur_tag = 0;
                cur_attr_first = ap_count;
                cur_attr_count = 0;
                cur_self_close = 0;
                i++;
                continue;
            }
            if (c < 0) {
                if (i > text_start) {
                    emit_token(TK_TEXT, 0, text_start, i - text_start, 0, 0, 0);
                }
                emit_token(TK_EOF, 0, 0, 0, 0, 0, 0);
                return;
            }
            i++;
            continue;
        }

        if (state == ST_TAG_OPEN) {
            if (c == '/') { state = ST_END_TAG_OPEN; i++; continue; }
            if (c == '!') { state = ST_MARKUP_DECL; i++; continue; }
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                state = ST_TAG_NAME;
                tag_start = i;
                continue;            /* re-process this byte in TAG_NAME */
            }
            /* anything else: treat '<' as text, fall back */
            text_start = i - 1;
            state = ST_DATA;
            continue;
        }

        if (state == ST_END_TAG_OPEN) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                state = ST_TAG_NAME;
                tag_is_end = 1;
                tag_start = i;
                continue;
            }
            /* malformed; skip to '>' */
            while (i < html_len && page_buf[i] != '>') i++;
            if (i < html_len) i++;
            state = ST_DATA;
            text_start = i;
            continue;
        }

        if (state == ST_TAG_NAME) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '/' || c == '>') {
                cur_tag = tag_id(page_buf + tag_start, i - tag_start);
                state = ST_BEFORE_ATTR;
                continue;          /* let BEFORE_ATTR re-handle byte */
            }
            i++;
            continue;
        }

        if (state == ST_BEFORE_ATTR) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { i++; continue; }
            if (c == '/') { cur_self_close = 1; i++; continue; }
            if (c == '>') {
                emit_token(tag_is_end ? TK_END : TK_START, cur_tag,
                           0, 0, cur_attr_first, cur_attr_count, cur_self_close);
                /* RAWTEXT/RCDATA entry handled in 2.6 */
                if (!tag_is_end) {
                    if (cur_tag == T_SCRIPT || cur_tag == T_STYLE) {
                        state = ST_RAWTEXT;
                    } else if (cur_tag == T_TITLE || cur_tag == T_TEXTAREA) {
                        state = ST_RCDATA;
                    } else {
                        state = ST_DATA;
                    }
                } else {
                    state = ST_DATA;
                }
                i++;
                text_start = i;
                continue;
            }
            /* attribute name starts here — handled in 2.4 */
            state = ST_ATTR_NAME;
            tag_start = i;
            continue;
        }

        /* §2.4 fills ST_ATTR_NAME, ST_AFTER_ATTR_NAME, ST_BEFORE_VALUE,
           ST_VALUE_DQ/SQ/UQ; for now, drop the attribute and skip to '>' */
        while (i < html_len && page_buf[i] != '>') i++;
        if (i < html_len) i++;
        state = ST_DATA;
        text_start = i;
    }
}
```

The function references `T_SCRIPT`, `T_STYLE`, `T_TITLE`, `T_TEXTAREA` from the existing `T_*` enum in `main.cc`. Confirm those constants exist there before building (they do today: `tag_id` returns them).

- [ ] **Step 2.4: Implement attribute states (ATTR_NAME → BEFORE_VALUE → VALUE_*)**

Replace the placeholder block at the end of Step 2.3's `tokenize` (the `/* §2.4 fills ... */` comment and the trailing skip-to-`>` loop) with full attribute handling:

```c
        if (state == ST_ATTR_NAME) {
            if (c == '=') {
                int name_off = attr_intern(page_buf + tag_start, i - tag_start);
                if (ap_count < MAX_ATTR_PAIRS) {
                    ap_name_off[ap_count] = name_off;
                    ap_value_off[ap_count] = -1;       /* filled at value end */
                    ap_count++;
                    cur_attr_count++;
                }
                state = ST_BEFORE_VALUE;
                i++; continue;
            }
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
                c == '/' || c == '>') {
                /* boolean attribute (no '=') */
                int name_off = attr_intern(page_buf + tag_start, i - tag_start);
                if (ap_count < MAX_ATTR_PAIRS) {
                    ap_name_off[ap_count]  = name_off;
                    ap_value_off[ap_count] = attr_intern("", 0);
                    ap_count++;
                    cur_attr_count++;
                }
                state = ST_BEFORE_ATTR;
                continue;
            }
            i++;
            continue;
        }

        if (state == ST_BEFORE_VALUE) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { i++; continue; }
            if (c == '"')  { state = ST_VALUE_DQ; i++; tag_start = i; continue; }
            if (c == '\'') { state = ST_VALUE_SQ; i++; tag_start = i; continue; }
            state = ST_VALUE_UQ; tag_start = i; continue;
        }

        if (state == ST_VALUE_DQ || state == ST_VALUE_SQ) {
            int term = (state == ST_VALUE_DQ) ? '"' : '\'';
            if (c == term) {
                /* decode entities into ctype_buf, intern that */
                int dec_max = (i - tag_start) + 1;
                /* fast path: no '&' → intern directly */
                int has_amp = 0;
                for (int k = tag_start; k < i; k++) {
                    if (page_buf[k] == '&') { has_amp = 1; break; }
                }
                int voff;
                if (!has_amp) {
                    voff = attr_intern(page_buf + tag_start, i - tag_start);
                } else {
                    /* Reuse the existing decode_entities; ctype_buf is the scratch. */
                    int dec_len = decode_entities(page_buf + tag_start, i - tag_start,
                                                  ctype_buf, dec_max < 4096 ? dec_max : 4096);
                    voff = attr_intern(ctype_buf, dec_len);
                }
                ap_value_off[ap_count - 1] = voff;
                state = ST_BEFORE_ATTR;
                i++; continue;
            }
            i++; continue;
        }

        if (state == ST_VALUE_UQ) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
                c == '>' || c < 0) {
                int has_amp = 0;
                for (int k = tag_start; k < i; k++) {
                    if (page_buf[k] == '&') { has_amp = 1; break; }
                }
                int voff;
                if (!has_amp) {
                    voff = attr_intern(page_buf + tag_start, i - tag_start);
                } else {
                    int dec_max = (i - tag_start) + 1;
                    int dec_len = decode_entities(page_buf + tag_start, i - tag_start,
                                                  ctype_buf, dec_max < 4096 ? dec_max : 4096);
                    voff = attr_intern(ctype_buf, dec_len);
                }
                ap_value_off[ap_count - 1] = voff;
                state = ST_BEFORE_ATTR;
                continue;
            }
            i++; continue;
        }
```

`attr_intern` is the existing helper from Plan 1's `dom.cc`; it stores into `attr_pool[]` and returns the byte-offset.

- [ ] **Step 2.5: Implement markup-declaration / comment / doctype states**

Insert before the catch-all skip-to-`>` block:

```c
        if (state == ST_MARKUP_DECL) {
            /* If next two bytes are "--", it's a comment; otherwise treat as DOCTYPE-like */
            if (i + 1 < html_len && page_buf[i] == '-' && page_buf[i+1] == '-') {
                state = ST_COMMENT;
                i += 2;
                continue;
            }
            state = ST_DOCTYPE;
            continue;
        }

        if (state == ST_COMMENT) {
            /* skip until '-->' */
            while (i + 2 < html_len &&
                   !(page_buf[i] == '-' && page_buf[i+1] == '-' && page_buf[i+2] == '>')) {
                i++;
            }
            if (i + 2 < html_len) i += 3; else i = html_len;
            state = ST_DATA;
            text_start = i;
            continue;
        }

        if (state == ST_DOCTYPE) {
            while (i < html_len && page_buf[i] != '>') i++;
            if (i < html_len) i++;
            state = ST_DATA;
            text_start = i;
            continue;
        }
```

- [ ] **Step 2.6: Implement RAWTEXT and RCDATA states**

`<script>` / `<style>` content is RAWTEXT — no entity decoding, only `</script>` or `</style>` exits. `<title>` / `<textarea>` content is RCDATA — entities decoded, same exit rule. The existing `skip_to_close` from Plan 1's parser handled this for the old parser; replicate the logic but emit a single TK_TEXT token then a TK_END for the close tag.

Insert before the catch-all skip-to-`>` block, after the markup-decl handling:

```c
        if (state == ST_RAWTEXT || state == ST_RCDATA) {
            /* find matching </tag> for the most recently emitted START. */
            int last_start = tok_count - 1;
            while (last_start >= 0 && tok_kind[last_start] != TK_START) last_start--;
            int close_tag = (last_start >= 0) ? tok_tag[last_start] : 0;
            char *close_name = 0;
            int close_name_len = 0;
            if (close_tag == T_SCRIPT)   { close_name = "script";   close_name_len = 6; }
            else if (close_tag == T_STYLE)    { close_name = "style";    close_name_len = 5; }
            else if (close_tag == T_TITLE)    { close_name = "title";    close_name_len = 5; }
            else if (close_tag == T_TEXTAREA) { close_name = "textarea"; close_name_len = 8; }
            else { state = ST_DATA; continue; }

            int t_start = i;
            while (i < html_len) {
                if (page_buf[i] == '<' && i + 1 + close_name_len < html_len &&
                    page_buf[i+1] == '/' &&
                    b_strieq_n(page_buf + i + 2, close_name, close_name_len) &&
                    (page_buf[i + 2 + close_name_len] == '>' ||
                     page_buf[i + 2 + close_name_len] == ' ' ||
                     page_buf[i + 2 + close_name_len] == '\t' ||
                     page_buf[i + 2 + close_name_len] == '\n')) break;
                i++;
            }
            int t_end = i;

            if (t_end > t_start) {
                if (state == ST_RCDATA) {
                    int dec_len = decode_entities(page_buf + t_start, t_end - t_start,
                                                  ctype_buf, 4096);
                    /* For simplicity, store decoded bytes into a fresh attr_pool slice and
                       reference it via tok_text_off as a negative index. Easier: emit text
                       referencing page_buf for RAWTEXT, or for RCDATA store decoded bytes
                       into attr_pool and use that offset (mark with a flag). */
                    int off = attr_intern(ctype_buf, dec_len);
                    /* tok_text_off currently means page_buf offset. Use a sentinel: if
                       tok_text_len & 0x40000000 is set, the offset is into attr_pool. */
                    emit_token(TK_TEXT, 0, off,
                               dec_len | 0x40000000, 0, 0, 0);
                } else {
                    emit_token(TK_TEXT, 0, t_start, t_end - t_start, 0, 0, 0);
                }
            }

            /* skip past "</closename" and any whitespace/'>' */
            if (i < html_len) {
                emit_token(TK_END, close_tag, 0, 0, 0, 0, 0);
                i += 2 + close_name_len;
                while (i < html_len && page_buf[i] != '>') i++;
                if (i < html_len) i++;
            }
            state = ST_DATA;
            text_start = i;
            continue;
        }
```

The `0x40000000` sentinel bit on `tok_text_len` distinguishes attr-pool-backed text from page-buf-backed text. The tree builder in Task 3 reads it as `int real_len = tok_text_len[ti] & 0x3FFFFFFF; int from_attr = (tok_text_len[ti] >> 30) & 1;`. Document this above the `tok_text_len` declaration in `main.cc`.

- [ ] **Step 2.7: Implement entity decoding in TEXT runs (carry-over from §1)**

Currently the tokenizer emits `TK_TEXT` referencing raw `page_buf` bytes. Entity decoding for normal text happens at consumer time (the existing `parse_html` already calls `decode_entities`). Task 3's tree builder will call `decode_entities` when it converts a TK_TEXT to a DOM text node. No change needed in the tokenizer for this step beyond what 2.6 already added for RCDATA.

Skip — placeholder in the task list to make scope explicit.

- [ ] **Step 2.8: Wire `tokenize` call into `parse_html` (no consumption yet)**

At the very top of the existing `parse_html` function in `parser.cc`, add a call to `tokenize` so the new tokenizer runs on every page. Its output is unused for now — Task 3 wires the tree builder to consume it. This step proves `tokenize` doesn't crash on real input.

Insert at line 1 of `parse_html`:

```c
    tokenize(html_len);
    serial_printf("[browser] tokenize: %d tokens, %d attr-pairs\n", tok_count, ap_count);
```

`serial_printf` is the existing kernel binding; output appears in QEMU serial log.

- [ ] **Step 2.9: Build + smoke + commit**

Run: `make 2>&1 | tail -5` — expect success.
Run: `make run-net`, `browser http://example.com/`, page renders identically to `/tmp/browser-pre-p2/01-example.png` (old `parse_html` is still in charge).
Run: `make run-net 2>&1 | grep "tokenize:"` — expect a line like `[browser] tokenize: 47 tokens, 9 attr-pairs` for example.com.
Run: `browser http://info.cern.ch/hypertext/WWW/TheProject.html` — render matches reference; serial log shows tokenize line with much higher counts (~400 tokens, ~40 attr-pairs).

If `tok_count` or `ap_count` ever shows `MAX_TOKENS` or `MAX_ATTR_PAIRS` exactly, the cap is tight on a real page — bump it now. Otherwise commit.

```bash
git add bin/browser/main.cc bin/browser/parser.cc
git commit -m "$(cat <<'EOF'
browser: add §1 tokenizer (state machine, full attribute handling, RAWTEXT/RCDATA)

Adds tokenize(html_len) producing a typed token stream
(TK_START/END/TEXT/EOF) with attribute-pair pool. States: Data,
TagOpen, EndTagOpen, TagName, BeforeAttr, AttrName, BeforeValue,
ValueDQ/SQ/UQ, MarkupDecl, Comment, Doctype, RAWTEXT, RCDATA.

Entities decoded inline for attribute values (DQ/SQ/UQ) and for
RCDATA text. Plain TEXT runs reference page_buf directly; the tree
builder will decode at consumption time.

The tokenizer's output is wired up at the top of parse_html for now
but ignored — the existing parser is still in charge. Task 3 swaps
parse_html to consume the token stream.

No behavior change.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: §1 Tree builder + new DOM (attr-pair pool replaces per-attr fields)

Rewrite `parse_html` to consume the token stream produced by Task 2 and emit DOM nodes via a new attr-pair pool. Per-attribute fields (`n_href`, `n_src`, `n_color`, `n_bg`, `n_action`, `n_name`, `n_value`, `n_type`) are removed; consumers (layout, paint, nav, input, form submit) switch to a generic `dom_attr_get(node, "name")`. Open-elements stack with simplified insertion modes drives implicit-close rules.

This is the biggest task in Plan 2 because it spans parser, DOM, and every downstream consumer. Split into focused steps.

**Files:**
- Modify: `bin/browser/main.cc` (DOM array shape; remove old per-attr arrays)
- Modify: `bin/browser/dom.cc` (add `dom_attr_get`; rewrite `alloc_node` to take attrs)
- Modify: `bin/browser/parser.cc` (replace `parse_html` body with tree builder over token stream)
- Modify: `bin/browser/layout.cc` (read attrs via `dom_attr_get`)
- Modify: `bin/browser/paint.cc` (read attrs via `dom_attr_get`)
- Modify: `bin/browser/nav.cc` (read attrs via `dom_attr_get`)
- Modify: `bin/browser/input.cc` (read attrs via `dom_attr_get`)

- [ ] **Step 3.1: Update DOM globals in `main.cc`**

In the globals block of `main.cc`, find the existing `n_*` declarations. Apply these changes:

**Remove:**
```c
int n_href     [MAX_NODES];
int n_src      [MAX_NODES];
int n_color    [MAX_NODES];
int n_bg       [MAX_NODES];
int n_bold     [MAX_NODES];
int n_link     [MAX_NODES];
int n_input    [MAX_NODES];
int n_form     [MAX_NODES];
int n_action   [MAX_NODES];
int n_name     [MAX_NODES];
int n_value    [MAX_NODES];
int n_type     [MAX_NODES];
```

(Adjust to whatever the exact list is in `main.cc` after Plan 1; the principle is "every per-attribute parallel array goes away.") Keep `n_tag`, `n_parent`, `n_first_child`, `n_next`, `n_text_off`, `n_text_len` — these encode tree structure, not attributes.

**Add:**
```c
/* §1 DOM attribute pool — each node has a contiguous slice of (name_off, value_off)
 * pairs in ap_*[]. Slice begins at dom_attrs_first[i] and runs dom_attrs_count[i]
 * pairs. ap_*[] is the same pool the tokenizer wrote into; slices are copied (not
 * referenced) into a separate dom_ap_* pool because the tokenizer's pool is
 * scratch — see step 3.4. */
int dom_attrs_first[MAX_NODES];
int dom_attrs_count[MAX_NODES];

/* Permanent DOM attr-pair pool. The tokenizer's ap_*[] is scratch and reused
 * across pages; dom_ap_*[] persists for the lifetime of the page so layout/paint
 * can re-query during repaint. */
int dom_ap_count;
int dom_ap_name_off [MAX_ATTR_PAIRS];
int dom_ap_value_off[MAX_ATTR_PAIRS];

/* §1 selector cache: id and class strings extracted at DOM-build time.
 * Used by §2 selector matching. -1 if absent. */
int dom_class_off[MAX_NODES];
int dom_id_off   [MAX_NODES];
```

`dom_ap_*` gets `MAX_ATTR_PAIRS = 8192` from Task 1 (ample for the largest test pages).

- [ ] **Step 3.2: Add `dom_attr_get` helper to `dom.cc`**

In `bin/browser/dom.cc`, append (above `decode_entities` so all later tasks can use it):

```c
/* Returns offset into attr_pool (where strings live) or -1 if attribute absent.
 * `name` is a NUL-terminated literal like "href"; comparison case-insensitive. */
int dom_attr_get(int node, char *name) {
    if (node < 0 || node >= nodes_count) return -1;
    int first = dom_attrs_first[node];
    int count = dom_attrs_count[node];
    int name_len = b_strlen(name);
    for (int k = 0; k < count; k++) {
        char *aname = attr_pool + dom_ap_name_off[first + k];
        if (b_strieq_n(aname, name, name_len) && aname[name_len] == 0) {
            return dom_ap_value_off[first + k];
        }
    }
    return -1;
}

/* Like dom_attr_get but returns the string pointer, or NULL if absent. */
char *dom_attr_str(int node, char *name) {
    int off = dom_attr_get(node, name);
    return (off < 0) ? 0 : attr_pool + off;
}
```

- [ ] **Step 3.3: Rewrite `alloc_node` to accept token-stream attrs**

In `dom.cc`, replace the existing `alloc_node(int tag, int parent)` with:

```c
int alloc_node(int tag, int parent, int tok_idx) {
    if (nodes_count >= MAX_NODES) return -1;
    int n = nodes_count++;
    n_tag[n] = tag;
    n_parent[n] = parent;
    n_first_child[n] = -1;
    n_next[n] = -1;
    n_text_off[n] = 0;
    n_text_len[n] = 0;
    dom_class_off[n] = -1;
    dom_id_off[n] = -1;

    /* Copy attrs from tokenizer scratch into permanent dom_ap_* pool */
    int t_first = (tok_idx >= 0) ? tok_attr_first[tok_idx] : 0;
    int t_count = (tok_idx >= 0) ? tok_attr_count[tok_idx] : 0;
    if (t_count > 0 && dom_ap_count + t_count <= MAX_ATTR_PAIRS) {
        dom_attrs_first[n] = dom_ap_count;
        dom_attrs_count[n] = t_count;
        for (int k = 0; k < t_count; k++) {
            int n_off = ap_name_off[t_first + k];
            int v_off = ap_value_off[t_first + k];
            dom_ap_name_off[dom_ap_count]  = n_off;
            dom_ap_value_off[dom_ap_count] = v_off;
            dom_ap_count++;
            /* cache id / class hits */
            char *aname = attr_pool + n_off;
            if (b_strieq(aname, "class")) dom_class_off[n] = v_off;
            else if (b_strieq(aname, "id")) dom_id_off[n]  = v_off;
        }
    } else {
        dom_attrs_first[n] = 0;
        dom_attrs_count[n] = 0;
    }

    /* parent-child wiring (same as old alloc_node) */
    if (parent >= 0) {
        if (n_first_child[parent] < 0) {
            n_first_child[parent] = n;
        } else {
            int sib = n_first_child[parent];
            while (n_next[sib] >= 0) sib = n_next[sib];
            n_next[sib] = n;
        }
    }
    return n;
}
```

The old `alloc_node` had two args (`tag`, `parent`). All call sites in the old `parse_html` pass `-1` for `tok_idx` because the rewrite replaces all those call sites. Once Step 3.4 lands, no caller passes `-1` for a normal element node. The `-1` path is kept for compatibility with one site: anonymous root creation.

- [ ] **Step 3.4: Replace `parse_html` body with tree builder over token stream**

This is the surgical replacement. Open `bin/browser/parser.cc`. The current `parse_html(int html_len)` body is the old single-pass parser (~250 lines). Delete that body — keep the function signature — and replace with:

```c
void parse_html(int html_len) {
    tokenize(html_len);
    serial_printf("[browser] tokenize: %d tokens, %d attr-pairs\n", tok_count, ap_count);

    /* reset DOM */
    nodes_count = 0;
    dom_ap_count = 0;
    attr_pool_pos = 0;        /* tokenizer interned into attr_pool already; we keep
                                 those interned strings — attr_pool_pos was advanced
                                 by attr_intern(); leave it. */
    /* Actually: attr_pool was extended by tokenize(); preserve attr_pool_pos. */

    /* Synthetic root */
    int root = alloc_node(T_DOC_ROOT, -1, -1);

    /* Open-elements stack */
    int stack[64];
    int sp = 0;
    stack[sp++] = root;

    enum { IM_INITIAL, IM_IN_HEAD, IM_IN_BODY, IM_IN_TABLE, IM_IN_ROW, IM_IN_CELL, IM_IN_CAPTION };
    int mode = IM_INITIAL;

    for (int ti = 0; ti < tok_count; ti++) {
        int kind = tok_kind[ti];
        int tag  = tok_tag[ti];

        if (kind == TK_EOF) break;

        if (kind == TK_TEXT) {
            int parent = stack[sp - 1];
            int len_field = tok_text_len[ti];
            int from_attr = (len_field >> 30) & 1;
            int real_len  = len_field & 0x3FFFFFFF;
            int text_off, text_len;
            if (from_attr) {
                /* RCDATA: bytes are already in attr_pool, no decode needed */
                text_off = tok_text_off[ti];
                text_len = real_len;
            } else {
                /* Decode entities into ctype_buf, then intern */
                int dec_max = real_len + 1;
                if (dec_max > 4096) dec_max = 4096;
                int dec_len = decode_entities(page_buf + tok_text_off[ti], real_len,
                                              ctype_buf, dec_max);
                text_off = attr_intern(ctype_buf, dec_len);
                text_len = dec_len;
            }
            /* Skip whitespace-only text outside <table> in IN_TABLE mode (foster parenting simplified) */
            if (mode == IM_IN_TABLE) {
                int ws = 1;
                for (int k = 0; k < text_len; k++) {
                    char c = attr_pool[text_off + k];
                    if (c != ' ' && c != '\t' && c != '\n' && c != '\r') { ws = 0; break; }
                }
                if (!ws) {
                    /* drop non-whitespace text inside <table> outside cells */
                    continue;
                }
            }
            int n = alloc_node(T_TEXT, parent, -1);
            if (n < 0) continue;
            n_text_off[n] = text_off;
            n_text_len[n] = text_len;
            continue;
        }

        if (kind == TK_START) {
            /* implicit-close rules */
            if (tag == T_P) {
                /* close any currently-open <p> */
                while (sp > 1 && n_tag[stack[sp - 1]] != T_P) {
                    /* pop until we find a <p>, or hit a block boundary */
                    if (is_block_tag(n_tag[stack[sp - 1]])) break;
                    sp--;
                }
                if (sp > 1 && n_tag[stack[sp - 1]] == T_P) sp--;
            }
            else if (is_block_tag(tag)) {
                /* close <p> if open as direct ancestor */
                if (sp > 1 && n_tag[stack[sp - 1]] == T_P) sp--;
            }
            else if (tag == T_LI) {
                while (sp > 1 && n_tag[stack[sp - 1]] != T_LI &&
                       !is_list_container(n_tag[stack[sp - 1]])) sp--;
                if (sp > 1 && n_tag[stack[sp - 1]] == T_LI) sp--;
            }
            else if (tag == T_DT || tag == T_DD) {
                while (sp > 1 && n_tag[stack[sp - 1]] != T_DT && n_tag[stack[sp - 1]] != T_DD &&
                       n_tag[stack[sp - 1]] != T_DL) sp--;
                if (sp > 1 && (n_tag[stack[sp - 1]] == T_DT || n_tag[stack[sp - 1]] == T_DD)) sp--;
            }
            else if (tag == T_TR) {
                while (sp > 1 && n_tag[stack[sp - 1]] != T_TR &&
                       n_tag[stack[sp - 1]] != T_TABLE && n_tag[stack[sp - 1]] != T_THEAD &&
                       n_tag[stack[sp - 1]] != T_TBODY && n_tag[stack[sp - 1]] != T_TFOOT) sp--;
                if (sp > 1 && n_tag[stack[sp - 1]] == T_TR) sp--;
            }
            else if (tag == T_TD || tag == T_TH) {
                while (sp > 1 && n_tag[stack[sp - 1]] != T_TD && n_tag[stack[sp - 1]] != T_TH &&
                       n_tag[stack[sp - 1]] != T_TR) sp--;
                if (sp > 1 && (n_tag[stack[sp - 1]] == T_TD || n_tag[stack[sp - 1]] == T_TH)) sp--;
            }
            else if (tag == T_OPTION) {
                if (sp > 1 && n_tag[stack[sp - 1]] == T_OPTION) sp--;
            }
            else if (tag == T_A) {
                /* close any open <a> (HN nests) */
                if (sp > 1 && n_tag[stack[sp - 1]] == T_A) sp--;
            }

            /* allocate node attached to current top-of-stack */
            int parent = stack[sp - 1];
            int n = alloc_node(tag, parent, ti);
            if (n < 0) continue;

            /* mode transitions */
            if (tag == T_TABLE) mode = IM_IN_TABLE;
            else if (tag == T_TR) mode = IM_IN_ROW;
            else if (tag == T_TD || tag == T_TH) mode = IM_IN_CELL;
            else if (tag == T_CAPTION) mode = IM_IN_CAPTION;
            else if (tag == T_BODY) mode = IM_IN_BODY;
            else if (tag == T_HEAD) mode = IM_IN_HEAD;

            /* push unless void */
            if (!is_void_tag(tag) && !tok_self_close[ti]) {
                if (sp < 64) stack[sp++] = n;
            }

            /* register links inline (mirror old behavior) */
            if (tag == T_A) {
                int href_off = dom_attr_get(n, "href");
                if (href_off >= 0 && links_count < MAX_LINKS) {
                    link_url_off[links_count] = href_off;
                    links_count++;
                }
            }
            /* register inputs */
            if (tag == T_INPUT && inputs_count < MAX_INPUTS) {
                int v_off = dom_attr_get(n, "value");
                int n_off = dom_attr_get(n, "name");
                input_value[inputs_count][0] = 0;
                if (v_off >= 0) {
                    char *v = attr_pool + v_off;
                    int len = b_strlen(v);
                    if (len >= 64) len = 63;
                    for (int k = 0; k < len; k++) input_value[inputs_count][k] = v[k];
                    input_value[inputs_count][len] = 0;
                }
                input_name_off[inputs_count] = n_off;
                input_form[inputs_count] = (forms_count > 0) ? forms_count - 1 : -1;
                inputs_count++;
            }
            /* register forms */
            if (tag == T_FORM && forms_count < MAX_FORMS) {
                int a_off = dom_attr_get(n, "action");
                form_action[forms_count] = a_off;
                forms_count++;
            }

            continue;
        }

        if (kind == TK_END) {
            /* close until matching open tag */
            int found = -1;
            for (int k = sp - 1; k >= 1; k--) {
                if (n_tag[stack[k]] == tag) { found = k; break; }
            }
            if (found >= 0) sp = found;

            /* mode transitions on close */
            if (tag == T_TABLE) mode = IM_IN_BODY;
            else if (tag == T_TR) mode = IM_IN_TABLE;
            else if (tag == T_TD || tag == T_TH) mode = IM_IN_ROW;
            else if (tag == T_CAPTION) mode = IM_IN_TABLE;
            continue;
        }
    }
}
```

`is_block_tag(int tag)` and `is_list_container(int tag)` are new helpers — add them in `parser.cc` above `parse_html`:

```c
int is_block_tag(int tag) {
    return tag == T_P || tag == T_DIV || tag == T_H1 || tag == T_H2 ||
           tag == T_H3 || tag == T_H4 || tag == T_H5 || tag == T_H6 ||
           tag == T_UL || tag == T_OL || tag == T_LI || tag == T_PRE ||
           tag == T_BLOCKQUOTE || tag == T_HR || tag == T_TABLE ||
           tag == T_FORM || tag == T_HEADER || tag == T_FOOTER ||
           tag == T_NAV || tag == T_SECTION || tag == T_ARTICLE ||
           tag == T_ASIDE || tag == T_MAIN;
}

int is_list_container(int tag) {
    return tag == T_UL || tag == T_OL || tag == T_DL;
}
```

Add `T_DOC_ROOT`, `T_DT`, `T_DD`, `T_DL`, `T_THEAD`, `T_TBODY`, `T_TFOOT`, `T_CAPTION`, `T_OPTION`, `T_HEADER`, `T_FOOTER`, `T_NAV`, `T_SECTION`, `T_ARTICLE`, `T_ASIDE`, `T_MAIN`, `T_TEXTAREA` to the `T_*` enum in `main.cc` if not already present. Update `tag_id` in `parser.cc` to map their names. (Plan 1 inherited the old tag list; verify each is present and add missing ones.)

- [ ] **Step 3.5: Update `decode_entities` table to spec §1 named entities**

Open `bin/browser/dom.cc`. Find the named-entity table inside `decode_entities` (Plan 1 has `amp`, `lt`, `gt`, `quot`, `nbsp`, `apos`). Extend to the spec §1 list:

```c
static char *ent_names[] = {
    "amp",   "lt",    "gt",    "quot",  "apos",  "nbsp",
    "mdash", "ndash", "hellip","lsquo", "rsquo", "ldquo", "rdquo",
    "middot","bull",  "copy",  "reg",   "trade", "sect",  "para",
    "deg",   "times", "divide","plusmn","frac12","frac14","frac34",
    "larr",  "rarr",  "uarr",  "darr",  "laquo", "raquo",
    "iexcl", "iquest","cent",  "pound", "yen",   "euro",
    0
};
static int ent_chars[] = {
    '&',     '<',     '>',     '"',     '\'',    ' ',           /* nbsp → space */
    '-',     '-',     '.',     '\'',    '\'',    '"',     '"',  /* dashes / quotes ASCII */
    '*',     '*',     'C',     'R',     'T',     'S',     'P',  /* misc → ASCII fallback */
    'd',     'x',     '/',     '+',     'h',     'q',     'Q',  /* fractions → letter approx */
    '<',     '>',     '^',     'v',     '<',     '>',
    '!',     '?',     'c',     'L',     'Y',     'E'
};
```

The mapping is lossy (no Unicode in the 8x8 ASCII font) but predictable. Numeric `&#N;` and `&#xH;` decode to literal char if ASCII; NBSP (160) → space; soft hyphen (173) → drop; everything else → '?'. This is already the existing behavior; preserve it.

Verify with: `browser https://en.wikipedia.org/wiki/HTTP` (will TLS-load now thanks to Phase 8 fixes) — `&copy;` and `&mdash;` no longer print as raw text.

- [ ] **Step 3.6: Migrate layout.cc readers to `dom_attr_get`**

Open `bin/browser/layout.cc`. Search for every reference to the removed `n_*` fields. Replace inline. Examples:

```c
/* old */
int color = n_color[idx];
/* new */
int color_off = dom_attr_get(idx, "color");
int color = -1;
if (color_off >= 0) parse_color(attr_pool + color_off, &color);
```

```c
/* old */
int href = n_href[idx];
/* new */
int href = dom_attr_get(idx, "href");      /* still an attr_pool offset */
```

For `n_bold` / `n_link` (which aren't really attribute lookups but cached "is this node bold?"), keep them — but recompute by walking ancestors looking at `tag` (the existing `parent_bold`/`parent_link` already do this; remove the cache). Delete the lines that wrote `n_bold[n] = ...` and `n_link[n] = ...` from `parse_html` (they're gone in step 3.4 already).

Replace all per-attr field reads in `layout.cc`. The full list to handle: `n_href`, `n_src`, `n_color`, `n_bg`, `n_action`, `n_name`, `n_value`, `n_type`, `n_input`, `n_form`. Map `n_input[idx]` and `n_form[idx]` to "look up the input/form index by walking the inputs[] / forms[] arrays for one whose backing node matches `idx`" — or, simpler, swap the directionality: keep an `input_node[i]` parallel to `input_value[i]` so input-index → node is a direct lookup, and node → input-index is a linear walk (rare).

Add to `main.cc`:
```c
int input_node[MAX_INPUTS];     /* DOM node index that this input came from */
int form_node [MAX_FORMS];      /* DOM node index that this form came from */
```
Set in step 3.4's TK_START handler for T_INPUT and T_FORM (`input_node[inputs_count] = n;`, `form_node[forms_count] = n;`).

Then in layout.cc, where the old code did `int v_idx = n_input[idx];`, replace with:
```c
int v_idx = -1;
for (int k = 0; k < inputs_count; k++) {
    if (input_node[k] == idx) { v_idx = k; break; }
}
```

Keep this linear scan localized to one helper, e.g. `int find_input_for_node(int idx)`, called wherever needed.

- [ ] **Step 3.7: Migrate paint.cc, nav.cc, input.cc the same way**

Apply the same pattern to:
- `bin/browser/paint.cc` — uses `n_color`, `n_bg`, `n_href` for link styling and `n_value` for input rendering. Same swap as 3.6.
- `bin/browser/nav.cc` — `submit_form` reads `n_action` and walks form children for `<input>` `n_value`/`n_name`. Replace with `dom_attr_str(form_node[fi], "action")` and walk DOM children of `form_node[fi]`, calling `dom_attr_str(child, "name")` etc.
- `bin/browser/input.cc` — reads `n_href` to follow links on click, `n_input` to figure out which input a click hit. Replace with `dom_attr_get(node, "href")` and the `find_input_for_node` helper from 3.6.

This is mechanical but tedious. For each file: `grep -nE 'n_(href|src|color|bg|bold|link|input|form|action|name|value|type)\[' bin/browser/<file>.cc` enumerates every occurrence; each line is a one-by-one swap.

- [ ] **Step 3.8: Build — expect failures, fix iteratively**

Run: `make 2>&1 | tail -30`. Expect compile errors of the form `undefined symbol n_href`, etc. Each error names a file + line. Visit each, swap to `dom_attr_*`. Repeat until build succeeds.

If any reference is in a comment marker or conditionally-compiled block, just delete the dead reference.

- [ ] **Step 3.9: Smoke — example.com**

Run: `make run-net`, `browser http://example.com/`. Expected: page renders identically to `/tmp/browser-pre-p2/01-example.png`. Title bar, paragraph, link.

If the page is blank but no panic: the tree builder produced no nodes. Check serial log for the `[browser] tokenize:` line — if `tok_count` is 0, the tokenizer is broken; if `nodes_count` is 0 in a `serial_printf("[browser] dom: %d nodes\n", nodes_count)` you should add temporarily after `parse_html`, the tree builder skipped everything.

If the page is garbled but not blank: tag mapping in `tag_id` is incomplete, or one of the `is_*_tag` helpers is missing a case — fix and retry.

- [ ] **Step 3.10: Smoke — multi-site regression**

Run, in same QEMU session:
1. `browser http://info.cern.ch/hypertext/WWW/TheProject.html` — match `03-info-cern.png`.
2. `browser http://lite.cnn.com/` — match `04-cnn-lite.png`.
3. Ctrl-L, `https://www.iana.org/help/example-domains`, Enter — match `02-iana.png` (TLS path).
4. Click a link → page navigates.
5. Backspace → history pop.
6. Ctrl-L typing → address-bar editing.

All should render at parity with pre-Plan-2 references. If any one regresses noticeably, the tree-builder / attr-migration is incomplete for that site's pattern; diagnose by adding `serial_printf` traces in `parse_html` for that page's tokens.

- [ ] **Step 3.11: Verify no leftover per-attr references**

Run: `grep -rE 'n_(href|src|color|bg|action|name|value|type|input|form|bold|link)\[' bin/browser/`
Expected: empty output (or only comment/marker lines).

If anything remains, swap or delete.

- [ ] **Step 3.12: Commit**

```bash
git add bin/browser/main.cc bin/browser/dom.cc bin/browser/parser.cc \
        bin/browser/layout.cc bin/browser/paint.cc bin/browser/nav.cc \
        bin/browser/input.cc
git commit -m "$(cat <<'EOF'
browser: swap parser to token-stream tree builder; replace per-attr DOM fields with attr-pair pool

Replaces parse_html's ad-hoc lex+tree pass with tokenize() output
consumption: TK_START pushes onto an open-elements stack with
implicit-close rules per spec §1 (p, li, dt/dd, tr, td/th, option,
nested a, body-after-head). TK_END pops to matching open tag.
TK_TEXT decodes entities and inserts as text node, dropping
non-whitespace stray text inside <table> outside cells.

DOM per-attribute fields (n_href, n_src, n_color, n_bg, n_action,
n_name, n_value, n_type, n_input, n_form, n_bold, n_link) removed.
Replaced with a (name_off, value_off) attr-pair pool scoped to the
DOM (dom_ap_*[]). dom_attr_get(node, "name") / dom_attr_str(node,
"name") look up by name.

Bold / link / inheritable styling are no longer cached on the DOM —
the existing parent_bold / parent_link helpers walk ancestors as
needed; that path was always live, the cache was stale-prone and is
removed. Layout, paint, nav, input, form-submit migrated to
dom_attr_*. New input_node[]/form_node[] lookup keeps node↔input
mapping cheap.

decode_entities table extended to spec §1 set: mdash, ndash,
hellip, lsquo/rsquo/ldquo/rdquo, middot, bull, copy, reg, trade,
sect, para, deg, times, divide, plusmn, frac12/14/34,
larr/rarr/uarr/darr, laquo/raquo, iexcl, iquest, cent, pound, yen,
euro. ASCII-only font means the mapping is lossy by design.

Verified: example.com, info.cern.ch, lite.cnn.com, iana.org over
TLS render at parity with pre-plan-2 references.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: §2 CSS lexer + author rule table + UA stylesheet

Add `bin/browser/css.cc` with CSS tokenization, selector parsing, property/value parsing, and `<style>` rule extraction. Add `bin/browser/style.cc` (skeleton — populated in Task 5) holding the UA stylesheet hard-coded as a switch over tag, plus the rule storage shared with `css.cc`.

This task introduces the new files but does not yet apply styles to the DOM. End state: every `<style>` block on the page has its rules parsed and added to `rule[]`; the UA-stylesheet helper returns reasonable defaults per tag.

**Files:**
- Create: `bin/browser/css.cc`
- Create: `bin/browser/style.cc`
- Modify: `bin/browser/main.cc` (rule pool globals; ComputedStyle pool reservation)
- Modify: `bin/browser.cc` (trampoline include order)

- [ ] **Step 4.1: Add rule pool globals + ComputedStyle pool reservation to `main.cc`**

Append to the globals block:

```c
/* §2 CSS rule table — populated by css.cc, consumed by style.cc resolver.
 * One rule = one selector chain + one declaration block. Multiple
 * declarations in a single CSS rule produce multiple entries here
 * (one per property), so MAX_CSS_RULES caps property-count, not rule-count. */
int css_rule_count;
int css_rule_sel_first[MAX_CSS_RULES];   /* index into css_sel_* arrays */
int css_rule_sel_count[MAX_CSS_RULES];
int css_rule_prop_id  [MAX_CSS_RULES];   /* CP_* property enum */
int css_rule_value_off[MAX_CSS_RULES];   /* offset into css_value_pool */
int css_rule_value_len[MAX_CSS_RULES];
int css_rule_specificity[MAX_CSS_RULES]; /* (id_count<<16) | (class_count<<8) | tag_count */
int css_rule_doc_order [MAX_CSS_RULES];  /* tiebreaker */

/* Each selector is a chain of "compound selectors". A compound is a
 * (tag, class_off, id_off) triple. The rule's selector array stores the
 * chain in order; descendant combinators are implicit (any depth). */
enum { MAX_CSS_SELECTORS = 1024 };
int css_sel_count;
int css_sel_tag      [MAX_CSS_SELECTORS];   /* T_* or 0 for "any" */
int css_sel_class_off[MAX_CSS_SELECTORS];   /* offset into attr_pool, -1 if none */
int css_sel_id_off   [MAX_CSS_SELECTORS];   /* offset into attr_pool, -1 if none */

/* CSS value pool — separate from attr_pool because values are CSS-shaped, not
 * HTML-attribute-shaped, and we want to free per-page without invalidating
 * attribute strings. */
char css_value_pool[CSS_VALUE_POOL_SIZE];
int  css_value_pool_pos;

/* §2 ComputedStyle pool — sized per spec §2 (4096 entries).
 * Each render node will reference one of these via rt_style[i].
 * Plan-2 fields cover what spec §2 lists as supported. */
int cs_count;
int cs_color    [MAX_COMPUTED_STYLES];   /* RGB packed 0xRRGGBB or -1 (inherit) */
int cs_bg       [MAX_COMPUTED_STYLES];   /* RGB or -1 (transparent) */
int cs_font_w   [MAX_COMPUTED_STYLES];   /* 400 normal, 700 bold */
int cs_font_i   [MAX_COMPUTED_STYLES];   /* 0 normal, 1 italic */
int cs_font_size_tier[MAX_COMPUTED_STYLES]; /* 0..4, see spec §6 font sizing */
int cs_text_align[MAX_COMPUTED_STYLES];  /* TA_LEFT/CENTER/RIGHT */
int cs_text_dec [MAX_COMPUTED_STYLES];   /* bitmask: TD_UNDERLINE | TD_LINE_THROUGH */
int cs_display  [MAX_COMPUTED_STYLES];   /* DISP_BLOCK/INLINE/INLINE_BLOCK/LIST_ITEM/TABLE_*/NONE */
int cs_margin   [MAX_COMPUTED_STYLES][4]; /* TRBL */
int cs_padding  [MAX_COMPUTED_STYLES][4];
int cs_border   [MAX_COMPUTED_STYLES][4]; /* width per side; 0 or 1 only in v1 */
int cs_border_color[MAX_COMPUTED_STYLES];
int cs_width    [MAX_COMPUTED_STYLES];   /* px, -1 = auto */
int cs_height   [MAX_COMPUTED_STYLES];
int cs_white_space[MAX_COMPUTED_STYLES]; /* WS_NORMAL/PRE/NOWRAP */
int cs_list_style[MAX_COMPUTED_STYLES];  /* LS_DISC/CIRCLE/SQUARE/DECIMAL/NONE */
int cs_vertical_align[MAX_COMPUTED_STYLES]; /* VA_BASELINE/TOP/MIDDLE/BOTTOM */
```

Add the supporting enums (in the same enum block):

```c
    /* CSS property IDs — internal */
    CP_COLOR = 1, CP_BG_COLOR, CP_BG, CP_FONT_WEIGHT, CP_FONT_STYLE,
    CP_FONT_SIZE, CP_TEXT_ALIGN, CP_TEXT_DEC, CP_DISPLAY,
    CP_MARGIN, CP_MARGIN_T, CP_MARGIN_R, CP_MARGIN_B, CP_MARGIN_L,
    CP_PADDING, CP_PADDING_T, CP_PADDING_R, CP_PADDING_B, CP_PADDING_L,
    CP_BORDER, CP_BORDER_COLOR, CP_BORDER_T, CP_BORDER_R, CP_BORDER_B, CP_BORDER_L,
    CP_WIDTH, CP_HEIGHT, CP_WHITE_SPACE, CP_LIST_STYLE_TYPE, CP_VERTICAL_ALIGN,

    TA_LEFT = 0, TA_CENTER, TA_RIGHT,
    TD_UNDERLINE = 1, TD_LINE_THROUGH = 2,
    DISP_BLOCK = 1, DISP_INLINE, DISP_INLINE_BLOCK, DISP_LIST_ITEM,
    DISP_TABLE, DISP_TABLE_ROW_GROUP, DISP_TABLE_ROW, DISP_TABLE_CELL,
    DISP_TABLE_CAPTION, DISP_NONE,
    WS_NORMAL = 0, WS_PRE, WS_NOWRAP,
    LS_DISC = 0, LS_CIRCLE, LS_SQUARE, LS_DECIMAL, LS_NONE,
    VA_BASELINE = 0, VA_TOP, VA_MIDDLE, VA_BOTTOM,
```

- [ ] **Step 4.2: Create `bin/browser/css.cc` with lexer + selector parser**

Create `bin/browser/css.cc`:

```c
/* ---------- §2 CSS lexer + selector parser + rule extractor ----------
 * css_parse_block(text, len) appends to css_rule_*[] every rule it sees.
 * Selectors supported: tag, .class, #id, comma list, descendant (whitespace).
 * Unsupported (>, +, ~, [attr=...], :pseudo, *) parse-skip silently. */

int css_intern_value(char *src, int len) {
    if (css_value_pool_pos + len + 1 >= CSS_VALUE_POOL_SIZE) return -1;
    int off = css_value_pool_pos;
    for (int k = 0; k < len; k++) css_value_pool[off + k] = src[k];
    css_value_pool[off + len] = 0;
    css_value_pool_pos += len + 1;
    return off;
}

int css_skip_ws(char *s, int n, int i) {
    while (i < n) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { i++; continue; }
        if (c == '/' && i + 1 < n && s[i+1] == '*') {
            i += 2;
            while (i + 1 < n && !(s[i] == '*' && s[i+1] == '/')) i++;
            if (i + 1 < n) i += 2;
            continue;
        }
        break;
    }
    return i;
}

/* parse one compound selector; returns end index. Fills out_tag, out_class_off,
 * out_id_off. If the compound is unsupported, sets *unsupp = 1. */
int css_parse_compound(char *s, int n, int i,
                       int *out_tag, int *out_class_off, int *out_id_off,
                       int *unsupp) {
    *out_tag = 0;
    *out_class_off = -1;
    *out_id_off = -1;
    *unsupp = 0;
    int started = 0;
    while (i < n) {
        char c = s[i];
        if (c == '*') {
            if (!started) { *unsupp = 1; }   /* "*" alone — supported by treating as any */
            i++; started = 1; continue;
        }
        if (c == '#') {
            i++;
            int s_start = i;
            while (i < n && (((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
                              (s[i] >= '0' && s[i] <= '9') || s[i] == '_' || s[i] == '-'))) i++;
            *out_id_off = attr_intern(s + s_start, i - s_start);
            started = 1; continue;
        }
        if (c == '.') {
            i++;
            int s_start = i;
            while (i < n && (((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
                              (s[i] >= '0' && s[i] <= '9') || s[i] == '_' || s[i] == '-'))) i++;
            *out_class_off = attr_intern(s + s_start, i - s_start);
            started = 1; continue;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            int s_start = i;
            while (i < n && (((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
                              (s[i] >= '0' && s[i] <= '9')))) i++;
            *out_tag = tag_id(s + s_start, i - s_start);
            started = 1; continue;
        }
        if (c == '[' || c == ':' || c == '>' || c == '+' || c == '~') {
            *unsupp = 1;
            /* skip to next whitespace, comma, or { */
            while (i < n && s[i] != ' ' && s[i] != '\t' && s[i] != ',' && s[i] != '{') i++;
            return i;
        }
        break;
    }
    if (!started) *unsupp = 1;
    return i;
}

/* Parse one selector-chain, append compounds to css_sel_*. Returns chain start
 * (and writes count). Sets *unsupported=1 if any compound unsupported. */
int css_parse_selector_chain(char *s, int n, int i, int *chain_count, int *unsupported) {
    int first = css_sel_count;
    int count = 0;
    *unsupported = 0;
    while (i < n) {
        i = css_skip_ws(s, n, i);
        if (i >= n) break;
        char c = s[i];
        if (c == ',' || c == '{') break;
        int t, c_off, id_off, unsupp;
        int j = css_parse_compound(s, n, i, &t, &c_off, &id_off, &unsupp);
        if (unsupp) *unsupported = 1;
        if (j == i) break;            /* no progress */
        if (css_sel_count < MAX_CSS_SELECTORS) {
            css_sel_tag[css_sel_count] = t;
            css_sel_class_off[css_sel_count] = c_off;
            css_sel_id_off[css_sel_count] = id_off;
            css_sel_count++;
            count++;
        }
        i = j;
    }
    *chain_count = count;
    if (count == 0) {
        css_sel_count = first;       /* roll back */
        *unsupported = 1;
    }
    return i;
}

int css_compute_specificity(int sel_first, int sel_count) {
    int id_c = 0, cls_c = 0, tag_c = 0;
    for (int k = 0; k < sel_count; k++) {
        if (css_sel_id_off   [sel_first + k] >= 0) id_c++;
        if (css_sel_class_off[sel_first + k] >= 0) cls_c++;
        if (css_sel_tag      [sel_first + k] != 0) tag_c++;
    }
    int s = (id_c << 16) | (cls_c << 8) | tag_c;
    if (s > 0xFFFFFF) s = 0xFFFFFF;
    return s;
}
```

The lexer is intentionally minimal — handles whitespace, comments, identifiers, `#`/`.`/tag selectors, the bracket/pseudo unsupported skip. No tokenizer-as-state-machine; it's a recursive-descent parser over the CSS text buffer.

- [ ] **Step 4.3: Add property/value parser**

Append to `css.cc`:

```c
int css_match_property(char *s, int len) {
    if (len == 5  && b_strieq_n(s, "color", 5))                   return CP_COLOR;
    if (len == 16 && b_strieq_n(s, "background-color", 16))       return CP_BG_COLOR;
    if (len == 10 && b_strieq_n(s, "background", 10))             return CP_BG;
    if (len == 11 && b_strieq_n(s, "font-weight", 11))            return CP_FONT_WEIGHT;
    if (len == 10 && b_strieq_n(s, "font-style", 10))             return CP_FONT_STYLE;
    if (len == 9  && b_strieq_n(s, "font-size", 9))               return CP_FONT_SIZE;
    if (len == 10 && b_strieq_n(s, "text-align", 10))             return CP_TEXT_ALIGN;
    if (len == 15 && b_strieq_n(s, "text-decoration", 15))        return CP_TEXT_DEC;
    if (len == 7  && b_strieq_n(s, "display", 7))                 return CP_DISPLAY;
    if (len == 6  && b_strieq_n(s, "margin", 6))                  return CP_MARGIN;
    if (len == 10 && b_strieq_n(s, "margin-top", 10))             return CP_MARGIN_T;
    if (len == 12 && b_strieq_n(s, "margin-right", 12))           return CP_MARGIN_R;
    if (len == 13 && b_strieq_n(s, "margin-bottom", 13))          return CP_MARGIN_B;
    if (len == 11 && b_strieq_n(s, "margin-left", 11))            return CP_MARGIN_L;
    if (len == 7  && b_strieq_n(s, "padding", 7))                 return CP_PADDING;
    if (len == 11 && b_strieq_n(s, "padding-top", 11))            return CP_PADDING_T;
    if (len == 13 && b_strieq_n(s, "padding-right", 13))          return CP_PADDING_R;
    if (len == 14 && b_strieq_n(s, "padding-bottom", 14))         return CP_PADDING_B;
    if (len == 12 && b_strieq_n(s, "padding-left", 12))           return CP_PADDING_L;
    if (len == 6  && b_strieq_n(s, "border", 6))                  return CP_BORDER;
    if (len == 12 && b_strieq_n(s, "border-color", 12))           return CP_BORDER_COLOR;
    if (len == 5  && b_strieq_n(s, "width", 5))                   return CP_WIDTH;
    if (len == 6  && b_strieq_n(s, "height", 6))                  return CP_HEIGHT;
    if (len == 11 && b_strieq_n(s, "white-space", 11))            return CP_WHITE_SPACE;
    if (len == 16 && b_strieq_n(s, "list-style-type", 16))        return CP_LIST_STYLE_TYPE;
    if (len == 14 && b_strieq_n(s, "vertical-align", 14))         return CP_VERTICAL_ALIGN;
    return 0;
}

void css_emit_rule(int sel_first, int sel_count, int prop_id, int val_off, int val_len) {
    if (css_rule_count >= MAX_CSS_RULES) return;
    int r = css_rule_count++;
    css_rule_sel_first[r] = sel_first;
    css_rule_sel_count[r] = sel_count;
    css_rule_prop_id[r] = prop_id;
    css_rule_value_off[r] = val_off;
    css_rule_value_len[r] = val_len;
    css_rule_specificity[r] = css_compute_specificity(sel_first, sel_count);
    css_rule_doc_order[r] = r;
}

/* Walk a declaration block { prop: value; prop: value; ... }. Caller has positioned
 * at '{'. Returns position after '}'. */
int css_parse_decls(char *s, int n, int i,
                    int sel_first, int sel_count) {
    if (i >= n || s[i] != '{') return i;
    i++;
    while (i < n) {
        i = css_skip_ws(s, n, i);
        if (i < n && s[i] == '}') { i++; break; }
        int p_start = i;
        while (i < n && s[i] != ':' && s[i] != ';' && s[i] != '}') i++;
        int p_end = i;
        if (i >= n || s[i] != ':') {
            /* malformed — skip to ';' or '}' */
            while (i < n && s[i] != ';' && s[i] != '}') i++;
            if (i < n && s[i] == ';') i++;
            continue;
        }
        i++;
        i = css_skip_ws(s, n, i);
        int v_start = i;
        while (i < n && s[i] != ';' && s[i] != '}') i++;
        int v_end = i;
        /* trim trailing whitespace */
        while (v_end > v_start && (s[v_end-1] == ' ' || s[v_end-1] == '\t')) v_end--;
        if (i < n && s[i] == ';') i++;

        /* trim trailing whitespace from prop name */
        while (p_end > p_start && (s[p_end-1] == ' ' || s[p_end-1] == '\t')) p_end--;
        int prop_id = css_match_property(s + p_start, p_end - p_start);
        if (prop_id == 0) continue;        /* unknown property — drop */
        int v_off = css_intern_value(s + v_start, v_end - v_start);
        if (v_off < 0) continue;
        css_emit_rule(sel_first, sel_count, prop_id, v_off, v_end - v_start);
    }
    return i;
}

void css_parse_block(char *text, int len) {
    int i = 0;
    while (i < len) {
        i = css_skip_ws(text, len, i);
        if (i >= len) break;
        /* Possibly multiple selectors separated by ',' */
        int chain_starts[16];
        int chain_counts[16];
        int chain_n = 0;
        int unsup = 0;
        while (i < len && chain_n < 16) {
            int sf = css_sel_count;
            int cc;
            int u;
            i = css_parse_selector_chain(text, len, i, &cc, &u);
            chain_starts[chain_n] = sf;
            chain_counts[chain_n] = cc;
            if (u || cc == 0) unsup = 1;
            chain_n++;
            i = css_skip_ws(text, len, i);
            if (i < len && text[i] == ',') { i++; continue; }
            break;
        }
        i = css_skip_ws(text, len, i);
        if (i >= len) break;
        if (text[i] != '{') {
            /* malformed @-rule or unsupported — skip to next ';' or '}' */
            while (i < len && text[i] != '{' && text[i] != '}') i++;
            if (i < len && text[i] == '{') {
                /* skip block */
                int depth = 1;
                i++;
                while (i < len && depth > 0) {
                    if (text[i] == '{') depth++;
                    else if (text[i] == '}') depth--;
                    i++;
                }
            } else if (i < len) i++;
            continue;
        }
        if (unsup) {
            /* skip block */
            int depth = 1;
            i++;
            while (i < len && depth > 0) {
                if (text[i] == '{') depth++;
                else if (text[i] == '}') depth--;
                i++;
            }
            continue;
        }
        /* Apply decls to each selector chain */
        int decl_start = i;
        for (int k = 0; k < chain_n; k++) {
            i = css_parse_decls(text, len, decl_start, chain_starts[k], chain_counts[k]);
        }
    }
}
```

The selector cap is `MAX_CSS_SELECTORS = 1024` from Task 1; the rule cap is `MAX_CSS_RULES = 256` from Task 1. Real-world CSS overflows these (Wikipedia ships hundreds of KB of CSS). Spec §2 says "drop overflow rules; UA defaults still apply" — which is what `css_emit_rule`'s early return achieves naturally.

- [ ] **Step 4.4: Hook `<style>` extraction into the tree builder**

In `parser.cc`'s tree builder loop (Step 3.4), after emitting a TK_TEXT token, check if the parent's tag is `T_STYLE` and call `css_parse_block` on the text. Add inside the `if (kind == TK_TEXT)` branch, before `alloc_node`:

```c
            if (n_tag[parent] == T_STYLE) {
                /* Author CSS block */
                if (from_attr) {
                    css_parse_block(attr_pool + text_off, text_len);
                } else {
                    css_parse_block(page_buf + tok_text_off[ti], real_len);
                }
                /* Drop the text node — author CSS doesn't render */
                continue;
            }
```

Reset CSS state at the top of `parse_html`:

```c
    css_rule_count = 0;
    css_sel_count = 0;
    css_value_pool_pos = 0;
```

- [ ] **Step 4.5: Create skeleton `bin/browser/style.cc`**

Create `bin/browser/style.cc` with the UA-stylesheet helper plus a stub `style_resolve_all()` that Task 5 fills in:

```c
/* ---------- §2 Style resolver + UA stylesheet ----------
 * Plan-2 split:
 *   - This file (Plan-2 skeleton) declares the UA stylesheet helper and a
 *     stub style_resolve_all().
 *   - Task 5 fills style_resolve_all to produce a ComputedStyle per DOM node,
 *     applying UA + author cascade. */

void ua_default_style(int tag, int cs) {
    /* Fill cs_*[cs] with sensible UA defaults for `tag`. Inherit-friendly defaults
     * (color, font_*, white_space, text_align, list_style) start at -1 / 0 so the
     * resolver can treat them as "unset, inherit from parent". */
    cs_color[cs] = -1;
    cs_bg[cs] = -1;
    cs_font_w[cs] = 400;
    cs_font_i[cs] = 0;
    cs_font_size_tier[cs] = 1;       /* default 8x8 */
    cs_text_align[cs] = TA_LEFT;
    cs_text_dec[cs] = 0;
    cs_display[cs] = DISP_INLINE;
    cs_margin[cs][0] = cs_margin[cs][1] = cs_margin[cs][2] = cs_margin[cs][3] = 0;
    cs_padding[cs][0] = cs_padding[cs][1] = cs_padding[cs][2] = cs_padding[cs][3] = 0;
    cs_border[cs][0] = cs_border[cs][1] = cs_border[cs][2] = cs_border[cs][3] = 0;
    cs_border_color[cs] = 0x000000;
    cs_width[cs] = -1;
    cs_height[cs] = -1;
    cs_white_space[cs] = WS_NORMAL;
    cs_list_style[cs] = LS_DISC;
    cs_vertical_align[cs] = VA_BASELINE;

    /* Per-tag overrides matching spec §2 UA stylesheet */
    if (tag == T_H1) { cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700;
                       cs_font_size_tier[cs] = 4;
                       cs_margin[cs][0] = 16; cs_margin[cs][2] = 16; }
    else if (tag == T_H2) { cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700;
                       cs_font_size_tier[cs] = 3;
                       cs_margin[cs][0] = 12; cs_margin[cs][2] = 12; }
    else if (tag == T_H3) { cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700;
                       cs_font_size_tier[cs] = 3;
                       cs_margin[cs][0] = 10; cs_margin[cs][2] = 10; }
    else if (tag == T_H4) { cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700;
                       cs_font_size_tier[cs] = 2;
                       cs_margin[cs][0] = 8; cs_margin[cs][2] = 8; }
    else if (tag == T_H5 || tag == T_H6) {
                       cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700;
                       cs_font_size_tier[cs] = 1;
                       cs_margin[cs][0] = 8; cs_margin[cs][2] = 8; }
    else if (tag == T_P) { cs_display[cs] = DISP_BLOCK;
                       cs_margin[cs][0] = 8; cs_margin[cs][2] = 8; }
    else if (tag == T_A) { cs_color[cs] = 0x0000EE; cs_text_dec[cs] = TD_UNDERLINE; }
    else if (tag == T_PRE) { cs_display[cs] = DISP_BLOCK;
                       cs_white_space[cs] = WS_PRE; }
    else if (tag == T_CODE) { /* inline; monospace approximated by leaving 8x8 */ }
    else if (tag == T_B || tag == T_STRONG) { cs_font_w[cs] = 700; }
    else if (tag == T_I || tag == T_EM) { cs_font_i[cs] = 1; cs_text_dec[cs] = TD_UNDERLINE; }
    else if (tag == T_UL || tag == T_OL || tag == T_DL) {
                       cs_display[cs] = DISP_BLOCK; cs_padding[cs][3] = 24;
                       cs_margin[cs][0] = 8; cs_margin[cs][2] = 8;
                       if (tag == T_OL) cs_list_style[cs] = LS_DECIMAL; }
    else if (tag == T_LI) { cs_display[cs] = DISP_LIST_ITEM; }
    else if (tag == T_TABLE) { cs_display[cs] = DISP_TABLE; }
    else if (tag == T_THEAD || tag == T_TBODY || tag == T_TFOOT) {
                       cs_display[cs] = DISP_TABLE_ROW_GROUP; }
    else if (tag == T_TR) { cs_display[cs] = DISP_TABLE_ROW; }
    else if (tag == T_TD) { cs_display[cs] = DISP_TABLE_CELL; cs_padding[cs][0] = 2;
                       cs_padding[cs][1] = 2; cs_padding[cs][2] = 2; cs_padding[cs][3] = 2; }
    else if (tag == T_TH) { cs_display[cs] = DISP_TABLE_CELL; cs_font_w[cs] = 700;
                       cs_text_align[cs] = TA_CENTER;
                       cs_padding[cs][0] = 2; cs_padding[cs][1] = 2;
                       cs_padding[cs][2] = 2; cs_padding[cs][3] = 2; }
    else if (tag == T_HR) { cs_display[cs] = DISP_BLOCK; cs_height[cs] = 1;
                       cs_bg[cs] = 0x808080;
                       cs_margin[cs][0] = 8; cs_margin[cs][2] = 8; }
    else if (tag == T_BLOCKQUOTE) { cs_display[cs] = DISP_BLOCK; cs_padding[cs][3] = 16;
                       cs_margin[cs][0] = 8; cs_margin[cs][2] = 8; }
    else if (tag == T_DIV) { cs_display[cs] = DISP_BLOCK; }
    else if (tag == T_HEADER || tag == T_FOOTER || tag == T_NAV ||
             tag == T_SECTION || tag == T_ARTICLE || tag == T_ASIDE ||
             tag == T_MAIN) { cs_display[cs] = DISP_BLOCK; }
    else if (tag == T_FORM) { cs_display[cs] = DISP_BLOCK; }
    else if (tag == T_INPUT || tag == T_BUTTON) { cs_display[cs] = DISP_INLINE_BLOCK; }
    else if (tag == T_IMG) { cs_display[cs] = DISP_INLINE_BLOCK; }
    else if (tag == T_BODY) { cs_display[cs] = DISP_BLOCK; }
    else if (tag == T_HTML) { cs_display[cs] = DISP_BLOCK; }
    else if (tag == T_DOC_ROOT) { cs_display[cs] = DISP_BLOCK; }
    else if (tag == T_HEAD || tag == T_SCRIPT || tag == T_STYLE ||
             tag == T_NOSCRIPT) { cs_display[cs] = DISP_NONE; }
    else if (tag == T_DT) { cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700; }
    else if (tag == T_DD) { cs_display[cs] = DISP_BLOCK; cs_padding[cs][3] = 24; }
    else if (tag == T_TEXTAREA) { cs_display[cs] = DISP_INLINE_BLOCK; }
    else if (tag == T_OPTION) { cs_display[cs] = DISP_BLOCK; }
    else if (tag == T_CAPTION) { cs_display[cs] = DISP_TABLE_CAPTION; }
}

void style_resolve_all() {
    /* Stub. Task 5 fills this. */
    cs_count = 0;
}
```

- [ ] **Step 4.6: Update trampoline include order**

Edit `bin/browser.cc`. Insert `css.cc` and `style.cc` after `dom.cc` and before `parser.cc`'s consumer code is loaded. Actually selector parsing in `css.cc` calls `tag_id` (in `parser.cc`) and `attr_intern` (in `dom.cc`). Order needs:

```c
#include "browser/main.cc"
#include "browser/util.cc"
#include "browser/url.cc"
#include "browser/net.cc"
#include "browser/dom.cc"
#include "browser/parser.cc"        /* defines tag_id used by css.cc */
#include "browser/css.cc"
#include "browser/style.cc"
#include "browser/layout.cc"
#include "browser/paint.cc"
#include "browser/nav.cc"
#include "browser/input.cc"
```

But `parser.cc::parse_html` calls `css_parse_block` (added in Step 4.4). That's a forward reference within the same TU. CupidC accepts this. If the build fails on this, fall back to splitting the `<style>` text-handler into a function `dispatch_style_text` defined in `parser.cc` whose body just calls `css_parse_block` — and forward-declare `dispatch_style_text` if needed. (CupidC doesn't have C-style forward decls, but a function-pointer global initialised in `browser_main` is the classic workaround. Document this fallback in the commit if it triggers.)

- [ ] **Step 4.7: Build + smoke**

Run: `make 2>&1 | tail -10`. Expect success. New install lines for `/bin/browser/css.cc` and `/bin/browser/style.cc` appear at boot.

Run: `make run-net`, `browser http://example.com/`. Expected: rendering identical to Plan-2-pre reference (style not yet applied; UA stylesheet is computed but unused). Serial log shows tokenize line.

Add temporary instrumentation to `parse_html` after the loop:
```c
serial_printf("[browser] css: %d rules, %d sels, %d val-bytes\n",
              css_rule_count, css_sel_count, css_value_pool_pos);
```

For Wikipedia-on-HTTP (or any page with `<style>`): expect non-zero counts. Confirm that hits `MAX_CSS_RULES` (256) on Wikipedia pages, and that the cap doesn't crash — selectors past the cap silently drop.

- [ ] **Step 4.8: Commit**

```bash
git add bin/browser/main.cc bin/browser/css.cc bin/browser/style.cc bin/browser.cc
git commit -m "$(cat <<'EOF'
browser: add §2 CSS lexer, rule table, UA stylesheet skeleton

bin/browser/css.cc parses author <style> blocks: selector chains
(tag, .class, #id, comma list, descendant), declaration blocks,
property/value extraction. Supports color, background[-color],
font-{weight,style,size}, text-{align,decoration}, display,
margin/-tlrb, padding/-tlrb, border, border-color, width, height,
white-space, list-style-type, vertical-align. Unsupported selectors
(>, +, ~, [attr=...], :pseudo) and properties parse-skip silently.
Caps: MAX_CSS_RULES=256, MAX_CSS_SELECTORS=1024,
CSS_VALUE_POOL_SIZE=32K. Overflow drops further rules/selectors per
spec §2.

bin/browser/style.cc declares the UA stylesheet via a tag-switch
filling a ComputedStyle entry with sensible defaults: h1..h6 sized
4..1, p/div/section block, a blue underlined, ul/ol/dl with
padding-left, hr a 1px gray bar, head/script/style display:none.
style_resolve_all() is a stub — Task 5 fills it.

The tree builder calls css_parse_block on text inside <style>; the
text node itself is dropped (CSS doesn't render). Rule/selector/value
counts logged via serial_printf for visibility.

UA stylesheet not yet applied to the DOM — Plan-2 lays the groundwork
in this task and resolves+applies in Task 5.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: §2 Style resolver — apply UA + author cascade to every DOM node

Fill in `style_resolve_all` so every DOM node gets a `ComputedStyle` entry. Implements: UA defaults, author rule matching with descendant-combinator support, `style=` inline overrides, specificity tiebreak by document order, inheritance for `color`/`font-*`/`text-align`/`white-space`/`list-style`.

This task does not yet visibly change rendering — old layout reads from per-node fields, and the resolver writes to `cs_*[]` which nothing reads yet. Task 6 builds the render tree on top of this. Verification is via serial log: dump a few computed styles and confirm they make sense.

**Files:**
- Modify: `bin/browser/style.cc` (replace stub `style_resolve_all`)
- Modify: `bin/browser/parser.cc` (call `style_resolve_all()` at end of `parse_html`)

- [ ] **Step 5.1: Implement value parsers**

In `bin/browser/style.cc`, add helpers that turn a `(value_off, value_len)` pair into a typed value:

```c
int css_value_int(int off, int len) {
    /* Parses an integer, optional 'px' / 'pt' / 'em' suffix. Returns px (rounded). */
    int sign = 1;
    int i = off;
    int end = off + len;
    while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t')) i++;
    if (i < end && css_value_pool[i] == '-') { sign = -1; i++; }
    int v = 0;
    while (i < end && css_value_pool[i] >= '0' && css_value_pool[i] <= '9') {
        v = v * 10 + (css_value_pool[i] - '0');
        i++;
    }
    /* Suffix */
    if (i + 1 < end) {
        char a = css_value_pool[i], b = css_value_pool[i+1];
        if (a == 'p' && b == 't') { v = (v * 4) / 3; }
        else if (a == 'e' && b == 'm') { v = v * 8; }   /* 1em = 8px (base font) */
        /* px or unknown: treat as px */
    }
    return v * sign;
}

int css_value_color(int off, int len, int *out) {
    /* Reuse existing parse_color (in dom.cc). It accepts #RGB/#RRGGBB/named. */
    char tmp[64];
    if (len >= 64) len = 63;
    for (int k = 0; k < len; k++) tmp[k] = css_value_pool[off + k];
    tmp[len] = 0;
    return parse_color(tmp, out);
}

int css_value_keyword(int off, int len, char *kw) {
    int kl = b_strlen(kw);
    if (len < kl) return 0;
    return b_strieq_n(css_value_pool + off, kw, kl);
}
```

- [ ] **Step 5.2: Implement single-property apply**

```c
/* Apply a parsed property/value to ComputedStyle entry cs. Handles the shorthand
 * margin/padding 1-4 value forms. */
void cs_apply_property(int cs, int prop, int val_off, int val_len) {
    if (prop == CP_COLOR) {
        int c; if (css_value_color(val_off, val_len, &c)) cs_color[cs] = c;
    }
    else if (prop == CP_BG_COLOR || prop == CP_BG) {
        int c; if (css_value_color(val_off, val_len, &c)) cs_bg[cs] = c;
    }
    else if (prop == CP_FONT_WEIGHT) {
        if (css_value_keyword(val_off, val_len, "bold")) cs_font_w[cs] = 700;
        else if (css_value_keyword(val_off, val_len, "normal")) cs_font_w[cs] = 400;
        else cs_font_w[cs] = css_value_int(val_off, val_len);
    }
    else if (prop == CP_FONT_STYLE) {
        cs_font_i[cs] = css_value_keyword(val_off, val_len, "italic") ? 1 : 0;
    }
    else if (prop == CP_FONT_SIZE) {
        int px = css_value_int(val_off, val_len);
        /* Map to tier per spec §6 */
        if (px <= 9)        cs_font_size_tier[cs] = 0;
        else if (px <= 11)  cs_font_size_tier[cs] = 1;
        else if (px <= 14)  cs_font_size_tier[cs] = 2;
        else if (px <= 20)  cs_font_size_tier[cs] = 3;
        else                cs_font_size_tier[cs] = 4;
    }
    else if (prop == CP_TEXT_ALIGN) {
        if (css_value_keyword(val_off, val_len, "center"))      cs_text_align[cs] = TA_CENTER;
        else if (css_value_keyword(val_off, val_len, "right"))  cs_text_align[cs] = TA_RIGHT;
        else                                                     cs_text_align[cs] = TA_LEFT;
    }
    else if (prop == CP_TEXT_DEC) {
        cs_text_dec[cs] = 0;
        if (css_value_keyword(val_off, val_len, "underline"))     cs_text_dec[cs] |= TD_UNDERLINE;
        if (css_value_keyword(val_off, val_len, "line-through"))  cs_text_dec[cs] |= TD_LINE_THROUGH;
    }
    else if (prop == CP_DISPLAY) {
        if      (css_value_keyword(val_off, val_len, "none"))         cs_display[cs] = DISP_NONE;
        else if (css_value_keyword(val_off, val_len, "block"))        cs_display[cs] = DISP_BLOCK;
        else if (css_value_keyword(val_off, val_len, "inline-block")) cs_display[cs] = DISP_INLINE_BLOCK;
        else if (css_value_keyword(val_off, val_len, "inline"))       cs_display[cs] = DISP_INLINE;
        else if (css_value_keyword(val_off, val_len, "list-item"))    cs_display[cs] = DISP_LIST_ITEM;
        else if (css_value_keyword(val_off, val_len, "table"))        cs_display[cs] = DISP_TABLE;
        else if (css_value_keyword(val_off, val_len, "table-row"))    cs_display[cs] = DISP_TABLE_ROW;
        else if (css_value_keyword(val_off, val_len, "table-cell"))   cs_display[cs] = DISP_TABLE_CELL;
        /* unsupported display values fall through, keeping existing default */
    }
    else if (prop == CP_MARGIN || prop == CP_PADDING) {
        /* Parse up to 4 ints */
        int vals[4] = {0,0,0,0};
        int nvals = 0;
        int i = val_off;
        int end = val_off + val_len;
        while (i < end && nvals < 4) {
            while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t')) i++;
            if (i >= end) break;
            int v_start = i;
            while (i < end && css_value_pool[i] != ' ' && css_value_pool[i] != '\t') i++;
            vals[nvals++] = css_value_int(v_start, i - v_start);
        }
        int t,r,b,l;
        if (nvals == 1)      { t=r=b=l=vals[0]; }
        else if (nvals == 2) { t=b=vals[0]; r=l=vals[1]; }
        else if (nvals == 3) { t=vals[0]; r=l=vals[1]; b=vals[2]; }
        else                 { t=vals[0]; r=vals[1]; b=vals[2]; l=vals[3]; }
        if (prop == CP_MARGIN) {
            cs_margin[cs][0]=t; cs_margin[cs][1]=r; cs_margin[cs][2]=b; cs_margin[cs][3]=l;
        } else {
            cs_padding[cs][0]=t; cs_padding[cs][1]=r; cs_padding[cs][2]=b; cs_padding[cs][3]=l;
        }
    }
    else if (prop == CP_MARGIN_T) cs_margin[cs][0] = css_value_int(val_off, val_len);
    else if (prop == CP_MARGIN_R) cs_margin[cs][1] = css_value_int(val_off, val_len);
    else if (prop == CP_MARGIN_B) cs_margin[cs][2] = css_value_int(val_off, val_len);
    else if (prop == CP_MARGIN_L) cs_margin[cs][3] = css_value_int(val_off, val_len);
    else if (prop == CP_PADDING_T) cs_padding[cs][0] = css_value_int(val_off, val_len);
    else if (prop == CP_PADDING_R) cs_padding[cs][1] = css_value_int(val_off, val_len);
    else if (prop == CP_PADDING_B) cs_padding[cs][2] = css_value_int(val_off, val_len);
    else if (prop == CP_PADDING_L) cs_padding[cs][3] = css_value_int(val_off, val_len);
    else if (prop == CP_BORDER) {
        /* "1px solid #ccc" — width keyword color */
        cs_border[cs][0] = cs_border[cs][1] = cs_border[cs][2] = cs_border[cs][3] = 1;
        int c;
        if (css_value_color(val_off, val_len, &c)) cs_border_color[cs] = c;
    }
    else if (prop == CP_BORDER_COLOR) {
        int c; if (css_value_color(val_off, val_len, &c)) cs_border_color[cs] = c;
    }
    else if (prop == CP_WIDTH)  cs_width[cs]  = css_value_int(val_off, val_len);
    else if (prop == CP_HEIGHT) cs_height[cs] = css_value_int(val_off, val_len);
    else if (prop == CP_WHITE_SPACE) {
        if (css_value_keyword(val_off, val_len, "pre"))         cs_white_space[cs] = WS_PRE;
        else if (css_value_keyword(val_off, val_len, "nowrap")) cs_white_space[cs] = WS_NOWRAP;
        else                                                     cs_white_space[cs] = WS_NORMAL;
    }
    else if (prop == CP_LIST_STYLE_TYPE) {
        if (css_value_keyword(val_off, val_len, "decimal"))     cs_list_style[cs] = LS_DECIMAL;
        else if (css_value_keyword(val_off, val_len, "circle")) cs_list_style[cs] = LS_CIRCLE;
        else if (css_value_keyword(val_off, val_len, "square")) cs_list_style[cs] = LS_SQUARE;
        else if (css_value_keyword(val_off, val_len, "none"))   cs_list_style[cs] = LS_NONE;
        else                                                     cs_list_style[cs] = LS_DISC;
    }
    else if (prop == CP_VERTICAL_ALIGN) {
        if (css_value_keyword(val_off, val_len, "top"))         cs_vertical_align[cs] = VA_TOP;
        else if (css_value_keyword(val_off, val_len, "middle")) cs_vertical_align[cs] = VA_MIDDLE;
        else if (css_value_keyword(val_off, val_len, "bottom")) cs_vertical_align[cs] = VA_BOTTOM;
        else                                                     cs_vertical_align[cs] = VA_BASELINE;
    }
}
```

- [ ] **Step 5.3: Selector matching against a DOM node + ancestor chain**

Append to `style.cc`:

```c
int sel_compound_matches(int sel_idx, int node) {
    int t = css_sel_tag[sel_idx];
    if (t != 0 && n_tag[node] != t) return 0;
    int c_off = css_sel_class_off[sel_idx];
    if (c_off >= 0) {
        int node_class = dom_class_off[node];
        if (node_class < 0) return 0;
        /* class= can have multiple space-separated values; check each */
        char *tgt = attr_pool + c_off;
        int tgt_len = b_strlen(tgt);
        char *cls = attr_pool + node_class;
        int cls_len = b_strlen(cls);
        int i = 0;
        while (i < cls_len) {
            while (i < cls_len && (cls[i] == ' ' || cls[i] == '\t')) i++;
            int s = i;
            while (i < cls_len && cls[i] != ' ' && cls[i] != '\t') i++;
            if (i - s == tgt_len && b_strieq_n(cls + s, tgt, tgt_len)) goto class_ok;
        }
        return 0;
class_ok:;
    }
    int id_off = css_sel_id_off[sel_idx];
    if (id_off >= 0) {
        int node_id = dom_id_off[node];
        if (node_id < 0) return 0;
        if (!b_strieq(attr_pool + node_id, attr_pool + id_off)) return 0;
    }
    return 1;
}

/* Match a selector chain against a node by walking up parents, last selector matches
 * `node`, earlier selectors must match an ancestor (any depth). */
int sel_chain_matches(int sel_first, int sel_count, int node) {
    if (sel_count == 0) return 0;
    int last = sel_first + sel_count - 1;
    if (!sel_compound_matches(last, node)) return 0;
    int cur = n_parent[node];
    int s = last - 1;
    while (s >= sel_first) {
        if (cur < 0) return 0;
        if (sel_compound_matches(s, cur)) {
            s--;
            cur = n_parent[cur];
        } else {
            cur = n_parent[cur];     /* descendant: skip non-matching ancestors */
        }
    }
    return 1;
}
```

- [ ] **Step 5.4: Implement `style_resolve_all`**

Replace the stub from Step 4.5:

```c
void style_resolve_all() {
    cs_count = 0;

    /* Allocate one ComputedStyle per DOM node, in DOM order so parent < child */
    for (int n = 0; n < nodes_count; n++) {
        int cs = cs_count++;
        if (cs >= MAX_COMPUTED_STYLES) { cs_count = MAX_COMPUTED_STYLES; break; }

        /* 1. UA defaults */
        ua_default_style(n_tag[n], cs);

        /* 2. Author rules in (specificity, doc-order) order. We sort indirectly by
         *    iterating rules and tracking the highest (spec, order) winner per
         *    property. Simpler: iterate twice — first collect winners, second apply.
         *    Since MAX_CSS_RULES=256 and properties ~30, worst case ~7680 ops/node;
         *    acceptable. */
        int winner_rule[32];          /* per-property */
        int winner_score[32];         /* (spec << 16) | order, with 0xFFFF order */
        for (int p = 0; p < 32; p++) { winner_rule[p] = -1; winner_score[p] = -1; }
        for (int r = 0; r < css_rule_count; r++) {
            int p = css_rule_prop_id[r];
            if (p < 1 || p >= 32) continue;
            int sf = css_rule_sel_first[r];
            int sc = css_rule_sel_count[r];
            if (!sel_chain_matches(sf, sc, n)) continue;
            int score = (css_rule_specificity[r] << 12) | (css_rule_doc_order[r] & 0xFFF);
            if (score > winner_score[p]) {
                winner_score[p] = score;
                winner_rule[p] = r;
            }
        }
        for (int p = 1; p < 32; p++) {
            int r = winner_rule[p];
            if (r >= 0) {
                cs_apply_property(cs, p,
                                  css_rule_value_off[r], css_rule_value_len[r]);
            }
        }

        /* 3. Inline style="..." attribute (always wins over author rules) */
        int sty_off = dom_attr_get(n, "style");
        if (sty_off >= 0) {
            apply_inline_style(cs, attr_pool + sty_off);
        }

        /* 4. Inheritance: for unset inheritable properties, copy from parent's cs */
        int parent = n_parent[n];
        if (parent >= 0 && parent < cs_count) {
            int pcs = parent;
            if (cs_color[cs] < 0)         cs_color[cs] = cs_color[pcs];
            if (cs_text_align[cs] == TA_LEFT && cs_text_align[pcs] != TA_LEFT)
                                          cs_text_align[cs] = cs_text_align[pcs];
            /* font-* inherit if not explicitly set. Tracked by leaving cs_font_w == 0
             * meaning "unset"; UA defaults set 400 which counts as set. To preserve
             * inheritance, change UA default to 0 for non-bold tags, then default-fill
             * here. For Plan-2 simplicity: always inherit weight if parent's > 400 and
             * this node didn't get an explicit author override.
             *
             * Simpler: copy parent unconditionally for white-space / list-style / font_w
             * if the child's UA default matches the global UA default (i.e., this tag
             * didn't have a tag-specific override). Track this via a separate
             * "was-explicitly-set" bitmask if needed. Plan-2 ships without
             * bitmask — accept that h1 inside <em> won't inherit italic, etc. */
            if (cs_white_space[cs] == WS_NORMAL) cs_white_space[cs] = cs_white_space[pcs];
            if (cs_list_style[cs] == LS_DISC && cs_list_style[pcs] != LS_DISC)
                                          cs_list_style[cs] = cs_list_style[pcs];
        } else {
            /* root: ensure color is concrete */
            if (cs_color[cs] < 0) cs_color[cs] = 0x000000;
        }
    }
}
```

The "0x12 in score, 12 doc-order bits" packing assumes specificity ≤ 0xFFFFF and doc-order ≤ 4095, which holds within `MAX_CSS_RULES=256`.

- [ ] **Step 5.5: Inline style parser**

Append:

```c
void apply_inline_style(int cs, char *style) {
    /* Treat "color: red; padding: 4px" same as the body of a <style> rule.
     * Reuse css_parse_decls by writing the string into a scratch buffer wrapped
     * in "{...}" and calling css_parse_decls — but that emits new rules. Cleaner:
     * inline a minimal version here. */
    int n = b_strlen(style);
    int i = 0;
    while (i < n) {
        while (i < n && (style[i] == ' ' || style[i] == '\t')) i++;
        if (i >= n) break;
        int p_start = i;
        while (i < n && style[i] != ':' && style[i] != ';') i++;
        int p_end = i;
        while (p_end > p_start && (style[p_end-1] == ' ' || style[p_end-1] == '\t')) p_end--;
        if (i >= n || style[i] != ':') break;
        i++;
        while (i < n && (style[i] == ' ' || style[i] == '\t')) i++;
        int v_start = i;
        while (i < n && style[i] != ';') i++;
        int v_end = i;
        while (v_end > v_start && (style[v_end-1] == ' ' || style[v_end-1] == '\t')) v_end--;
        if (i < n && style[i] == ';') i++;
        int prop = css_match_property(style + p_start, p_end - p_start);
        if (prop) {
            /* Stash value in css_value_pool so cs_apply_property can read it */
            int voff = css_intern_value(style + v_start, v_end - v_start);
            if (voff >= 0) cs_apply_property(cs, prop, voff, v_end - v_start);
        }
    }
}
```

- [ ] **Step 5.6: Call `style_resolve_all` at end of `parse_html`**

In `bin/browser/parser.cc`, append at the end of `parse_html` (after the main token loop closes):

```c
    style_resolve_all();
    serial_printf("[browser] style: %d computed entries\n", cs_count);
```

- [ ] **Step 5.7: Build + verify via serial dump**

Add temporary debug helper to `style.cc`:

```c
void dump_style(int n) {
    serial_printf("[browser] node %d (tag %d): disp=%d color=0x%x bg=0x%x weight=%d size=%d\n",
                  n, n_tag[n], cs_display[n], cs_color[n], cs_bg[n],
                  cs_font_w[n], cs_font_size_tier[n]);
}
```

Call `dump_style(0); dump_style(1); dump_style(2);` after `style_resolve_all`. Run `make run-net`, `browser http://example.com/`, check serial log:
- Node 0 (DOC_ROOT): display=block, color=0x000000.
- An `<h1>` node should have weight=700, font-tier=4.
- An `<a>` node should have color=0x0000EE.

If a node's color is `-1` after resolution, inheritance fallback didn't fire — fix the loop.

Remove the `dump_style` calls before commit (or guard with a `#if 0`). Keep `dump_style` itself for future debugging.

- [ ] **Step 5.8: Commit**

```bash
git add bin/browser/style.cc bin/browser/parser.cc
git commit -m "$(cat <<'EOF'
browser: §2 style resolver — UA + author cascade with inheritance

style_resolve_all walks every DOM node, fills a ComputedStyle entry
with: (1) UA defaults via ua_default_style switch, (2) author rules
matched against the node and its ancestor chain in (specificity,
document-order) winning order, (3) inline style="..." overrides
parsed in apply_inline_style, (4) inheritance for color, white-space,
list-style, text-align from parent ComputedStyle.

Selector matching supports: tag, .class (handles space-separated
class= values), #id, comma-list (handled at parse), descendant
(any-depth ancestor walk).

Property handlers cover the spec §2 set: color, background[-color],
font-{weight,style,size}, text-{align,decoration}, display,
margin/padding/border with 1-4 value shorthand, width, height,
white-space, list-style-type, vertical-align. Font-size px values
map to the spec §6 tier table (tier 0..4).

Output unused yet — Task 6 builds the render tree consuming it.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: §3 Render tree — anonymous wrappers, list markers, table fallback

Add `bin/browser/render_tree.cc`. Walks the DOM, emits one render node per renderable DOM node (skips `display:none`), inserts anonymous block wrappers around inline-run-among-blocks, anonymous `<tr>`/`<table>` ancestors for stray cells/rows, and a `LIST_MARKER` node before each `<li>`'s content. For Plan-2 scope, table-display nodes are kept in the tree but laid out as anonymous blocks (Plan 3 introduces real table layout).

The output of this task is unused at runtime — it builds the tree but old layout/paint still drive rendering. Verification is via serial-log structure dump.

**Files:**
- Create: `bin/browser/render_tree.cc`
- Modify: `bin/browser/main.cc` (render-tree pool globals)
- Modify: `bin/browser/parser.cc` (call `build_render_tree()` at end of `parse_html`)
- Modify: `bin/browser.cc` (trampoline include order)

- [ ] **Step 6.1: Add render-tree pool globals to `main.cc`**

```c
/* §3 Render tree pool — sized at MAX_RT_NODES (6144 per spec) */
int rt_count;
int rt_dom         [MAX_RT_NODES];   /* back-pointer to DOM node, -1 for anonymous */
int rt_parent      [MAX_RT_NODES];
int rt_first_child [MAX_RT_NODES];
int rt_next        [MAX_RT_NODES];
int rt_kind        [MAX_RT_NODES];   /* RT_* */
int rt_style       [MAX_RT_NODES];   /* index into cs_*[] */
int rt_text_off    [MAX_RT_NODES];
int rt_text_len    [MAX_RT_NODES];
int rt_intrinsic_w [MAX_RT_NODES];
int rt_intrinsic_h [MAX_RT_NODES];
int rt_link_idx    [MAX_RT_NODES];   /* link[] index for clickable, -1 otherwise */
int rt_input_idx   [MAX_RT_NODES];   /* input[] index, -1 otherwise */

/* Geometry filled by §4 layout */
int rt_x[MAX_RT_NODES], rt_y[MAX_RT_NODES];
int rt_w[MAX_RT_NODES], rt_h[MAX_RT_NODES];
int rt_content_x[MAX_RT_NODES], rt_content_y[MAX_RT_NODES];
int rt_baseline[MAX_RT_NODES];

/* Line-box atom storage — one entry per word/glyph in an inline run.
 * line_box render nodes are LINE_BOX kind with rt_first_child indexing into
 * the atom pool via a separate atom_first/count pair. */
enum { MAX_LINE_ATOMS = 8192 };
int la_count;
int la_x        [MAX_LINE_ATOMS];   /* x within line box (cumulative) */
int la_w        [MAX_LINE_ATOMS];
int la_text_off [MAX_LINE_ATOMS];   /* into attr_pool */
int la_text_len [MAX_LINE_ATOMS];
int la_font_tier[MAX_LINE_ATOMS];
int la_fg       [MAX_LINE_ATOMS];
int la_bg       [MAX_LINE_ATOMS];
int la_bold     [MAX_LINE_ATOMS];
int la_underline[MAX_LINE_ATOMS];
int la_link_idx [MAX_LINE_ATOMS];

/* Each LINE_BOX render node references a contiguous atom slice */
int rt_line_atom_first[MAX_RT_NODES];
int rt_line_atom_count[MAX_RT_NODES];
```

Add render-kind enum:

```c
    RT_BLOCK = 1,
    RT_INLINE,
    RT_INLINE_BLOCK,
    RT_LIST_ITEM,
    RT_LIST_MARKER,
    RT_TABLE,
    RT_TABLE_ROW_GROUP,
    RT_TABLE_ROW,
    RT_TABLE_CELL,
    RT_TABLE_CAPTION,
    RT_TEXT,
    RT_REPLACED,        /* <img>, <input>, <button> */
    RT_LINE_BOX,
```

- [ ] **Step 6.2: Create `bin/browser/render_tree.cc`**

```c
/* ---------- §3 Render tree builder ---------- */

int rt_alloc(int kind, int dom, int parent, int style_cs) {
    if (rt_count >= MAX_RT_NODES) return -1;
    int n = rt_count++;
    rt_dom[n] = dom;
    rt_parent[n] = parent;
    rt_first_child[n] = -1;
    rt_next[n] = -1;
    rt_kind[n] = kind;
    rt_style[n] = style_cs;
    rt_text_off[n] = 0;
    rt_text_len[n] = 0;
    rt_intrinsic_w[n] = 0;
    rt_intrinsic_h[n] = 0;
    rt_link_idx[n] = -1;
    rt_input_idx[n] = -1;
    rt_line_atom_first[n] = 0;
    rt_line_atom_count[n] = 0;
    if (parent >= 0) {
        if (rt_first_child[parent] < 0) {
            rt_first_child[parent] = n;
        } else {
            int sib = rt_first_child[parent];
            while (rt_next[sib] >= 0) sib = rt_next[sib];
            rt_next[sib] = n;
        }
    }
    return n;
}

int rt_kind_for_display(int disp) {
    if (disp == DISP_BLOCK)              return RT_BLOCK;
    if (disp == DISP_INLINE)             return RT_INLINE;
    if (disp == DISP_INLINE_BLOCK)       return RT_INLINE_BLOCK;
    if (disp == DISP_LIST_ITEM)          return RT_LIST_ITEM;
    if (disp == DISP_TABLE)              return RT_TABLE;
    if (disp == DISP_TABLE_ROW_GROUP)    return RT_TABLE_ROW_GROUP;
    if (disp == DISP_TABLE_ROW)          return RT_TABLE_ROW;
    if (disp == DISP_TABLE_CELL)         return RT_TABLE_CELL;
    if (disp == DISP_TABLE_CAPTION)      return RT_TABLE_CAPTION;
    return RT_INLINE;
}

int rt_kind_is_inline(int kind) {
    return kind == RT_INLINE || kind == RT_INLINE_BLOCK || kind == RT_TEXT ||
           kind == RT_REPLACED;
}

int rt_kind_is_block_level(int kind) {
    return kind == RT_BLOCK || kind == RT_LIST_ITEM ||
           kind == RT_TABLE || kind == RT_TABLE_ROW_GROUP ||
           kind == RT_TABLE_ROW || kind == RT_TABLE_CELL ||
           kind == RT_TABLE_CAPTION;
}

/* Forward */
int build_rt_subtree(int dom, int rt_parent_n);

/* Helper: build children of `dom` under render parent `rt_parent_n`, inserting
 * anonymous block wrappers around contiguous inline runs whenever this parent
 * has at least one block-level child. */
void build_rt_children(int dom, int rt_parent_n) {
    /* Pre-scan: do we have a mix? */
    int has_block = 0;
    int c = n_first_child[dom];
    while (c >= 0) {
        if (n_tag[c] != T_TEXT) {
            int cs_c = c;     /* style index = DOM index */
            int disp = cs_display[cs_c];
            if (disp == DISP_NONE) { c = n_next[c]; continue; }
            int rk = rt_kind_for_display(disp);
            if (rt_kind_is_block_level(rk)) { has_block = 1; break; }
        }
        c = n_next[c];
    }

    int anon_block = -1;          /* current anon block wrapper, -1 if none open */
    c = n_first_child[dom];
    while (c >= 0) {
        if (n_tag[c] == T_TEXT) {
            /* Whitespace-only text between blocks is dropped per spec. */
            int ws = 1;
            for (int k = 0; k < n_text_len[c]; k++) {
                char ch = attr_pool[n_text_off[c] + k];
                if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') { ws = 0; break; }
            }
            if (ws && has_block) { c = n_next[c]; continue; }
            int target = rt_parent_n;
            if (has_block) {
                if (anon_block < 0) {
                    /* Anonymous block uses parent's style for inheritance */
                    anon_block = rt_alloc(RT_BLOCK, -1, rt_parent_n, rt_style[rt_parent_n]);
                }
                target = anon_block;
            }
            int rt_t = rt_alloc(RT_TEXT, c, target, c);
            if (rt_t < 0) return;
            rt_text_off[rt_t] = n_text_off[c];
            rt_text_len[rt_t] = n_text_len[c];
            c = n_next[c];
            continue;
        }
        int cs_c = c;
        int disp = cs_display[cs_c];
        if (disp == DISP_NONE) { c = n_next[c]; continue; }
        int rk = rt_kind_for_display(disp);

        if (rt_kind_is_block_level(rk)) {
            anon_block = -1;          /* close any open anon */
            int n = build_rt_subtree(c, rt_parent_n);
            (void)n;
        } else {
            int target = rt_parent_n;
            if (has_block) {
                if (anon_block < 0) {
                    anon_block = rt_alloc(RT_BLOCK, -1, rt_parent_n, rt_style[rt_parent_n]);
                }
                target = anon_block;
            }
            int n = build_rt_subtree(c, target);
            (void)n;
        }
        c = n_next[c];
    }
}

int build_rt_subtree(int dom, int rt_parent_n) {
    int cs_d = dom;       /* style index = DOM index */
    int disp = cs_display[cs_d];
    if (disp == DISP_NONE) return -1;

    /* REPLACED elements: <img>, <input>, <button>, <textarea> */
    int tag = n_tag[dom];
    int is_replaced = (tag == T_IMG || tag == T_INPUT || tag == T_BUTTON ||
                       tag == T_TEXTAREA);

    int kind = rt_kind_for_display(disp);
    if (is_replaced) kind = RT_REPLACED;

    int n = rt_alloc(kind, dom, rt_parent_n, cs_d);
    if (n < 0) return -1;

    /* For <a>: bind the link index */
    if (tag == T_A) {
        int href_off = dom_attr_get(dom, "href");
        for (int k = 0; k < links_count; k++) {
            if (link_url_off[k] == href_off) { rt_link_idx[n] = k; break; }
        }
    }

    /* For <img>: intrinsic dims from width/height attrs (placeholder default 80x60) */
    if (tag == T_IMG) {
        int w_off = dom_attr_get(dom, "width");
        int h_off = dom_attr_get(dom, "height");
        rt_intrinsic_w[n] = (w_off >= 0) ? css_value_int(0, 0) : 80;
        rt_intrinsic_h[n] = (h_off >= 0) ? css_value_int(0, 0) : 60;
        /* css_value_int reads from css_value_pool — wrong source. Use a local int parser. */
        if (w_off >= 0) {
            int v = 0;
            char *s = attr_pool + w_off;
            for (int k = 0; s[k] >= '0' && s[k] <= '9'; k++) v = v*10 + (s[k]-'0');
            if (v > 0) rt_intrinsic_w[n] = v;
        }
        if (h_off >= 0) {
            int v = 0;
            char *s = attr_pool + h_off;
            for (int k = 0; s[k] >= '0' && s[k] <= '9'; k++) v = v*10 + (s[k]-'0');
            if (v > 0) rt_intrinsic_h[n] = v;
        }
    }

    /* For <input>: bind input index, set intrinsic size */
    if (tag == T_INPUT) {
        for (int k = 0; k < inputs_count; k++) {
            if (input_node[k] == dom) { rt_input_idx[n] = k; break; }
        }
        rt_intrinsic_w[n] = 120;
        rt_intrinsic_h[n] = 16;
    }
    if (tag == T_BUTTON) {
        rt_intrinsic_w[n] = 64;
        rt_intrinsic_h[n] = 18;
    }

    /* For <li>: insert a LIST_MARKER child first */
    if (kind == RT_LIST_ITEM) {
        int marker = rt_alloc(RT_LIST_MARKER, -1, n, cs_d);
        (void)marker;
    }

    /* Replaced elements have no children rendered */
    if (!is_replaced) {
        build_rt_children(dom, n);
    }
    return n;
}

/* Anonymous-table-ancestor wrapping. Walk the freshly-built tree and look for
 * RT_TABLE_ROW that has a non-RT_TABLE/non-RT_TABLE_ROW_GROUP parent → wrap in
 * anonymous RT_TABLE_ROW_GROUP + RT_TABLE; RT_TABLE_CELL with non-row parent →
 * wrap in anonymous RT_TABLE_ROW + parents. v1 simplification: only handle the
 * common HN/Wikipedia patterns where authors write <table><tr><td>...</td></tr>;
 * stray cells without ancestors are wrapped here for safety. */
void rt_anon_table_fixup() {
    /* For Plan 2, table layout is the same as block fallback (no real grid),
     * so anon-table wrappers cosmetically don't matter — they're just
     * additional block boxes. Skip the wrap for Plan 2; Plan 3 implements
     * proper anon-table-ancestor logic alongside table layout. */
    (void)0;
}

void build_render_tree() {
    rt_count = 0;
    la_count = 0;
    /* Synthetic RT root mirrors DOM root */
    int root = rt_alloc(RT_BLOCK, 0, -1, 0);
    if (root < 0) return;
    build_rt_children(0, root);
    rt_anon_table_fixup();
}
```

- [ ] **Step 6.3: Wire `build_render_tree` into `parse_html`**

In `parser.cc`'s `parse_html`, after `style_resolve_all()`:

```c
    build_render_tree();
    serial_printf("[browser] rt: %d nodes\n", rt_count);
```

- [ ] **Step 6.4: Update trampoline include order**

```c
#include "browser/main.cc"
#include "browser/util.cc"
#include "browser/url.cc"
#include "browser/net.cc"
#include "browser/dom.cc"
#include "browser/parser.cc"
#include "browser/css.cc"
#include "browser/style.cc"
#include "browser/render_tree.cc"
#include "browser/layout.cc"
#include "browser/paint.cc"
#include "browser/nav.cc"
#include "browser/input.cc"
```

- [ ] **Step 6.5: Verify via serial dump**

Add temporary debug helper to `render_tree.cc`:

```c
void dump_rt(int n, int depth) {
    char *kn = "?";
    if (rt_kind[n] == RT_BLOCK)        kn = "BLOCK";
    else if (rt_kind[n] == RT_INLINE)  kn = "INLINE";
    else if (rt_kind[n] == RT_TEXT)    kn = "TEXT";
    else if (rt_kind[n] == RT_LIST_ITEM)   kn = "LI";
    else if (rt_kind[n] == RT_LIST_MARKER) kn = "MARKER";
    else if (rt_kind[n] == RT_REPLACED)kn = "REPLACED";
    else if (rt_kind[n] == RT_TABLE)   kn = "TABLE";
    else if (rt_kind[n] == RT_TABLE_ROW)  kn = "TR";
    else if (rt_kind[n] == RT_TABLE_CELL) kn = "TD";
    serial_printf("[rt] %d%*s %s dom=%d tag=%d txtlen=%d\n",
                  depth, depth*2, "", kn, rt_dom[n],
                  rt_dom[n] >= 0 ? n_tag[rt_dom[n]] : 0,
                  rt_text_len[n]);
    int c = rt_first_child[n];
    while (c >= 0) { dump_rt(c, depth + 1); c = rt_next[c]; }
}
```

Call `if (rt_count > 0) dump_rt(0, 0);` in `parse_html`. Run `make run-net`, `browser http://example.com/`, examine serial. Expected layout for example.com:

```
[rt] 0 BLOCK dom=0 (DOC_ROOT)
[rt] 1   BLOCK dom=N (HTML)
[rt] 2     BLOCK dom=N (BODY)
[rt] 3       BLOCK dom=N (DIV or H1)
[rt] 4         INLINE dom=N (H1)
[rt] 5           TEXT dom=N (Example Domain)
... etc
```

Anon-block wrapper test: a page with `<body>hello <p>world</p></body>` should produce an anonymous BLOCK wrapping the "hello " text node, sibling-to the `<p>` BLOCK.

Remove debug call before commit. Keep `dump_rt` for future use.

- [ ] **Step 6.6: Commit**

```bash
git add bin/browser/main.cc bin/browser/render_tree.cc bin/browser/parser.cc bin/browser.cc
git commit -m "$(cat <<'EOF'
browser: §3 render tree builder

build_render_tree() walks the DOM and emits a parallel render tree:
one render node per DOM node whose computed display != none, plus
anonymous BLOCK wrappers around contiguous inline runs whose siblings
include any block-level child (CSS 2.1 §9.2.1.1). LIST_MARKER nodes
inserted before each <li>'s content. <img>/<input>/<button>/<textarea>
flagged RT_REPLACED with intrinsic dimensions resolved from width/
height attrs (img) or sensible defaults.

Anonymous table-ancestor wrapping is a no-op for Plan 2 — table-display
nodes are kept in the tree (RT_TABLE / RT_TABLE_ROW / RT_TABLE_CELL)
and Plan 2's layout treats them as blocks. Plan 3 introduces the
proper grid + anon-ancestor logic.

Output dumped via serial for verification; not yet consumed by
layout/paint.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: §4 Layout — Block Formatting Context

Rewrite `bin/browser/layout.cc` to walk the render tree and fill `rt_x[]/rt_y[]/rt_w[]/rt_h[]/rt_content_x[]/rt_content_y[]`. This task implements only the BFC half: blocks stack vertically; widths/margins/padding/borders resolved against parent content width. The IFC (inline runs → line boxes) is Task 8.

The new layout runs alongside the old `run_layout()`/`boxes[]` pipeline; old paint still drives rendering. Cutover to render-tree-based paint is Task 9.

**Files:**
- Modify: `bin/browser/layout.cc` (rewrite — keep old `run_layout` as `legacy_run_layout` until Task 11 cleanup)

- [ ] **Step 7.1: Stash legacy layout under a renamed entry point**

In `bin/browser/layout.cc`, rename the existing `run_layout` and its helpers (`layout_node`, `layout_children`, `layout_text`, `emit_box`, etc.) by prefixing with `legacy_`. The current call site in `nav.cc::navigate` calls `run_layout()`; change that one call site to `legacy_run_layout()`. The new `run_layout()` we add in this task lives alongside; nav.cc temporarily calls both.

- [ ] **Step 7.2: Add new `run_layout` over the render tree**

Append to `layout.cc`:

```c
/* §4 BFC + IFC layout — fills rt_x/y/w/h/content_x/y for every render node. */

int rt_padding_l(int n) { return cs_padding[rt_style[n]][3]; }
int rt_padding_r(int n) { return cs_padding[rt_style[n]][1]; }
int rt_padding_t(int n) { return cs_padding[rt_style[n]][0]; }
int rt_padding_b(int n) { return cs_padding[rt_style[n]][2]; }
int rt_border_l (int n) { return cs_border [rt_style[n]][3]; }
int rt_border_r (int n) { return cs_border [rt_style[n]][1]; }
int rt_border_t (int n) { return cs_border [rt_style[n]][0]; }
int rt_border_b (int n) { return cs_border [rt_style[n]][2]; }
int rt_margin_l (int n) { return cs_margin [rt_style[n]][3]; }
int rt_margin_r (int n) { return cs_margin [rt_style[n]][1]; }
int rt_margin_t (int n) { return cs_margin [rt_style[n]][0]; }
int rt_margin_b (int n) { return cs_margin [rt_style[n]][2]; }

int viewport_content_w() {
    /* Window inner width minus 12px scrollbar */
    return cur_cw - 12;
}

/* Forward */
void layout_block(int n, int avail_w);
void flush_inline(int parent, int *atom_pile_first, int *atom_pile_count,
                  int cx, int *cy, int max_w);

void layout_block(int n, int avail_w) {
    /* Resolve width */
    int style_w = cs_width[rt_style[n]];
    int w;
    if (style_w >= 0) w = style_w;
    else w = avail_w - rt_margin_l(n) - rt_margin_r(n);
    if (w < 0) w = 0;
    rt_w[n] = w;

    int content_w = w - rt_padding_l(n) - rt_padding_r(n)
                    - rt_border_l(n) - rt_border_r(n);
    if (content_w < 0) content_w = 0;

    int cx = rt_padding_l(n) + rt_border_l(n);
    int cy = rt_padding_t(n) + rt_border_t(n);
    rt_content_x[n] = cx;
    rt_content_y[n] = cy;

    /* Walk children. Inline runs get accumulated and flushed when a block
     * sibling appears or end of children. */
    int pile_first = la_count;
    int pile_count = 0;

    int c = rt_first_child[n];
    while (c >= 0) {
        int kind = rt_kind[c];
        if (kind == RT_LIST_MARKER) {
            /* Markers don't affect block layout flow; placed in padding-left
             * reservation at paint time. */
            c = rt_next[c]; continue;
        }
        if (rt_kind_is_block_level(kind) ||
            (kind == RT_BLOCK)) {
            /* Flush pending inline run */
            if (pile_count > 0) {
                flush_inline(n, &pile_first, &pile_count, cx, &cy, content_w);
            }
            /* Lay out block child */
            int child_avail = content_w;
            layout_block(c, child_avail);
            int child_x = cx + rt_margin_l(c);
            int child_y = cy + rt_margin_t(c);
            rt_x[c] = child_x;
            rt_y[c] = child_y;
            cy = child_y + rt_h[c] + rt_margin_b(c);
        } else {
            /* Inline / text / inline-block / replaced: accumulate as atoms */
            collect_inline_atoms(c);
            pile_count = la_count - pile_first;
        }
        c = rt_next[c];
    }
    if (pile_count > 0) {
        flush_inline(n, &pile_first, &pile_count, cx, &cy, content_w);
    }

    /* Resolve own height */
    int style_h = cs_height[rt_style[n]];
    if (style_h >= 0) {
        rt_h[n] = style_h;
    } else {
        rt_h[n] = cy + rt_padding_b(n) + rt_border_b(n);
    }
}

void run_layout() {
    if (rt_count == 0) return;
    int root = 0;
    /* Root: width = viewport content width minus chrome (address bar, status, scrollbar) */
    rt_x[root] = viewport_x();   /* viewport_x in paint.cc; for now fixed */
    rt_y[root] = viewport_y();
    int avail = viewport_content_w();
    layout_block(root, avail);
    doc_h = rt_y[root] + rt_h[root];
}
```

`collect_inline_atoms` and `flush_inline` are defined in Task 8. Stub them now to keep the build green:

```c
void collect_inline_atoms(int n) { (void)n; }
void flush_inline(int parent, int *atom_pile_first, int *atom_pile_count,
                  int cx, int *cy, int max_w) {
    (void)parent; (void)atom_pile_first; (void)atom_pile_count;
    (void)cx; (void)cy; (void)max_w;
}
```

- [ ] **Step 7.3: Build + verify side-by-side**

Run: `make 2>&1 | tail -10`. Expect success.

Run: `make run-net`, `browser http://example.com/`. Expected: page renders identically to reference (the new `run_layout` runs alongside the legacy one — `nav.cc::navigate` calls both — but old `paint()` reads from `boxes[]` so old layout drives rendering). Serial log shows `[browser] rt: NN nodes` from Task 6 and could optionally show `[browser] layout-new: doc_h=NN` if you add a print after `run_layout()`.

If new `run_layout` crashes or asserts: comment out the `nav.cc` call to it temporarily, debug via standalone QEMU run with `dump_rt` to confirm tree shape, fix, retry.

- [ ] **Step 7.4: Commit**

```bash
git add bin/browser/layout.cc bin/browser/nav.cc
git commit -m "$(cat <<'EOF'
browser: §4 BFC layout over render tree (Plan-2 step 1 of 2)

New run_layout() walks the render tree filling rt_x/y/w/h/content_x/y
for every node. layout_block resolves width via style.width or
avail-w-minus-margins, derives content_w from padding+border insets,
walks children: block-level siblings stack vertically with margins
summed (no margin-collapse in v1 per spec), inline siblings accumulate
into a pending pile that flushes to line boxes at the next block
boundary or end-of-children.

Inline-formatting-context flush_inline + collect_inline_atoms are
stubbed; Task 8 fills them in. Without them, inline content is
silently dropped — but the legacy run_layout / boxes[] / paint
pipeline still drives rendering, so visible output is unchanged.

Legacy entry points renamed to legacy_*. nav.cc calls both old and
new during this transition; old still drives paint.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: §4 Layout — Inline Formatting Context (line boxes, white-space)

Fill in `flush_inline` and `collect_inline_atoms`. Walks an inline subtree depth-first, splits text into atoms by whitespace (or by `\n`/per-char per `white-space`), tracks `(x, line_top, line_h)`, emits `LINE_BOX` render nodes whose `rt_line_atom_first/count` reference the atom slice.

After this task, the new layout produces complete geometry for every render node. Cutover to render-tree-driven paint happens in Task 9.

**Files:**
- Modify: `bin/browser/layout.cc` (replace stubs)

- [ ] **Step 8.1: Implement `text_atom_w` (per-tier glyph width)**

```c
int tier_char_w(int tier) {
    if (tier == 0 || tier == 1) return 8;
    if (tier == 2) return 12;
    if (tier == 3) return 16;
    return 24;     /* tier 4 */
}
int tier_line_h(int tier) {
    if (tier == 0 || tier == 1) return 12;
    if (tier == 2) return 16;
    if (tier == 3) return 20;
    return 28;
}

int text_atom_w(int len, int tier) { return len * tier_char_w(tier); }
```

- [ ] **Step 8.2: Implement `collect_inline_atoms`**

```c
/* Walks the inline subtree rooted at `n` (which is RT_INLINE / RT_TEXT /
 * RT_INLINE_BLOCK / RT_REPLACED). Appends one or more entries to la_*[].
 * White-space behaviour from cs_white_space[rt_style[n]]: NORMAL collapses
 * runs of \s to single space, breaks anywhere; PRE preserves all whitespace,
 * splits on \n; NOWRAP collapses but never breaks.
 */
void emit_text_atoms(int rt_text_n, int parent_rt) {
    int cs = rt_style[rt_text_n];
    int ws = cs_white_space[cs];
    int tier = cs_font_size_tier[cs];
    int fg = cs_color[cs];
    int bg = cs_bg[cs];
    int bold = cs_font_w[cs] >= 700;
    int underline = (cs_text_dec[cs] & TD_UNDERLINE) ? 1 : 0;
    int link_idx = -1;
    /* Walk up looking for a parent with rt_link_idx >= 0 */
    int p = rt_text_n;
    while (p >= 0) {
        if (rt_link_idx[p] >= 0) { link_idx = rt_link_idx[p]; break; }
        p = rt_parent[p];
    }

    int off = rt_text_off[rt_text_n];
    int len = rt_text_len[rt_text_n];
    int i = 0;

    if (ws == WS_PRE) {
        /* Split on \n; each line is one atom. Tabs → 8 spaces. */
        int s = 0;
        while (s < len) {
            int e = s;
            while (e < len && attr_pool[off + e] != '\n') e++;
            int run_len = e - s;
            if (la_count < MAX_LINE_ATOMS) {
                la_text_off[la_count] = off + s;
                la_text_len[la_count] = run_len;
                la_w[la_count] = text_atom_w(run_len, tier);
                la_font_tier[la_count] = tier;
                la_fg[la_count] = fg; la_bg[la_count] = bg;
                la_bold[la_count] = bold; la_underline[la_count] = underline;
                la_link_idx[la_count] = link_idx;
                la_x[la_count] = -1;     /* filled at flush time */
                la_count++;
            }
            if (e < len) {
                /* Sentinel \n atom: text_len = 0 means hard break */
                if (la_count < MAX_LINE_ATOMS) {
                    la_text_off[la_count] = off + e;
                    la_text_len[la_count] = 0;
                    la_w[la_count] = 0;
                    la_font_tier[la_count] = tier;
                    la_fg[la_count] = fg; la_bg[la_count] = bg;
                    la_bold[la_count] = bold; la_underline[la_count] = underline;
                    la_link_idx[la_count] = link_idx;
                    la_x[la_count] = -2;    /* hard break sentinel */
                    la_count++;
                }
                s = e + 1;
            } else {
                s = e;
            }
        }
        return;
    }

    /* WS_NORMAL or WS_NOWRAP: collapse whitespace to single spaces, split into
     * word atoms. */
    while (i < len) {
        /* skip leading whitespace, emit a single space atom if any (only between
         * word atoms — leading at start of run handled by line layout). */
        int saw_ws = 0;
        while (i < len) {
            char c = attr_pool[off + i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { i++; saw_ws = 1; }
            else break;
        }
        if (i >= len) break;
        if (saw_ws && la_count > 0 && la_count < MAX_LINE_ATOMS) {
            /* Inter-word space atom: text_len=1, text_off pointing at a literal " ".
             * Cheap: stash a single-byte " " in attr_pool ahead of time and reuse. */
            la_text_off[la_count] = attr_intern(" ", 1);   /* idempotent if pooled */
            la_text_len[la_count] = 1;
            la_w[la_count] = tier_char_w(tier);
            la_font_tier[la_count] = tier;
            la_fg[la_count] = fg; la_bg[la_count] = bg;
            la_bold[la_count] = bold; la_underline[la_count] = underline;
            la_link_idx[la_count] = link_idx;
            la_x[la_count] = (ws == WS_NOWRAP) ? -1 : -3;  /* -3 = soft break point */
            la_count++;
        }
        /* word atom */
        int s = i;
        while (i < len) {
            char c = attr_pool[off + i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') break;
            i++;
        }
        int wlen = i - s;
        if (wlen > 0 && la_count < MAX_LINE_ATOMS) {
            la_text_off[la_count] = off + s;
            la_text_len[la_count] = wlen;
            la_w[la_count] = text_atom_w(wlen, tier);
            la_font_tier[la_count] = tier;
            la_fg[la_count] = fg; la_bg[la_count] = bg;
            la_bold[la_count] = bold; la_underline[la_count] = underline;
            la_link_idx[la_count] = link_idx;
            la_x[la_count] = -1;
            la_count++;
        }
    }
}

void collect_inline_atoms(int n) {
    int kind = rt_kind[n];
    if (kind == RT_TEXT) {
        emit_text_atoms(n, rt_parent[n]);
        return;
    }
    if (kind == RT_INLINE) {
        int c = rt_first_child[n];
        while (c >= 0) {
            collect_inline_atoms(c);
            c = rt_next[c];
        }
        return;
    }
    if (kind == RT_INLINE_BLOCK || kind == RT_REPLACED) {
        /* For replaced/inline-block: lay out as a mini block, then atom carries
         * the resulting w/h. */
        int avail = viewport_content_w();
        if (kind == RT_INLINE_BLOCK) layout_block(n, avail);
        int w = (rt_intrinsic_w[n] > 0) ? rt_intrinsic_w[n] : rt_w[n];
        int h = (rt_intrinsic_h[n] > 0) ? rt_intrinsic_h[n] : rt_h[n];
        if (la_count < MAX_LINE_ATOMS) {
            la_text_off[la_count] = -n - 1;     /* negative encodes "RT node ref"; -n-1 reverses to n */
            la_text_len[la_count] = 0;
            la_w[la_count] = w;
            la_font_tier[la_count] = 0;
            la_fg[la_count] = 0; la_bg[la_count] = 0;
            la_bold[la_count] = 0; la_underline[la_count] = 0;
            la_link_idx[la_count] = rt_link_idx[n];
            la_x[la_count] = -1;
            la_count++;
        }
        return;
    }
}
```

The encoding `la_text_off = -n - 1` for replaced/inline-block atoms means "this atom is a render node reference, not text"; paint code reads `if (la_text_off[k] < 0) { int rt_n = -la_text_off[k] - 1; paint_replaced(rt_n, ...); }`.

The `la_x[]` sentinels: `-1` = pending word, `-2` = hard newline (PRE), `-3` = soft break point (NORMAL). The flush loop interprets them.

- [ ] **Step 8.3: Implement `flush_inline`**

```c
void flush_inline(int parent, int *atom_pile_first, int *atom_pile_count,
                  int cx, int *cy, int max_w) {
    int first = *atom_pile_first;
    int total = *atom_pile_count;
    if (total == 0) return;

    int line_start_atom = first;
    int x = cx;
    int line_h = 0;
    int line_top = *cy;

    for (int k = first; k < first + total; k++) {
        int aw = la_w[k];
        int tier = la_font_tier[k];
        int sentinel = la_x[k];

        if (sentinel == -2) {
            /* Hard break (PRE) */
            la_x[k] = x;
            int lb = rt_alloc(RT_LINE_BOX, -1, parent, rt_style[parent]);
            if (lb >= 0) {
                rt_line_atom_first[lb] = line_start_atom;
                rt_line_atom_count[lb] = (k + 1) - line_start_atom;
                rt_x[lb] = cx;
                rt_y[lb] = line_top;
                rt_w[lb] = max_w;
                int lh = line_h ? line_h : tier_line_h(tier);
                rt_h[lb] = lh;
            }
            line_top += line_h ? line_h : tier_line_h(tier);
            x = cx;
            line_h = 0;
            line_start_atom = k + 1;
            continue;
        }

        /* Wrap if this atom doesn't fit and we have something on the line */
        if (sentinel == -3 && x + aw > cx + max_w && x > cx) {
            int lb = rt_alloc(RT_LINE_BOX, -1, parent, rt_style[parent]);
            if (lb >= 0) {
                rt_line_atom_first[lb] = line_start_atom;
                rt_line_atom_count[lb] = k - line_start_atom;
                rt_x[lb] = cx;
                rt_y[lb] = line_top;
                rt_w[lb] = max_w;
                rt_h[lb] = line_h;
            }
            line_top += line_h;
            x = cx;
            line_h = 0;
            line_start_atom = k + 1;        /* drop the trailing space atom */
            continue;
        }
        if (x + aw > cx + max_w && x > cx && sentinel != -3) {
            /* Mid-word wrap not allowed in v1 — but we accept overflow rather
             * than truncate */
        }
        la_x[k] = x;
        x += aw;
        int lh = tier_line_h(tier);
        if (lh > line_h) line_h = lh;
    }

    /* Flush remaining */
    if (line_start_atom < first + total) {
        int lb = rt_alloc(RT_LINE_BOX, -1, parent, rt_style[parent]);
        if (lb >= 0) {
            rt_line_atom_first[lb] = line_start_atom;
            rt_line_atom_count[lb] = (first + total) - line_start_atom;
            rt_x[lb] = cx;
            rt_y[lb] = line_top;
            rt_w[lb] = max_w;
            rt_h[lb] = line_h ? line_h : 12;
        }
        line_top += line_h ? line_h : 12;
    }

    *cy = line_top;
    *atom_pile_first = la_count;
    *atom_pile_count = 0;
}
```

- [ ] **Step 8.4: Build + smoke**

Run: `make 2>&1 | tail -5`. Expect success.

Run: `make run-net`, `browser http://example.com/`. Expected: page still renders identically (old paint pipeline still in charge — Task 9 cuts over). New layout populated rt_x/y/w/h fields for every node. Add temporary print after `run_layout`:

```c
serial_printf("[browser] rt[0] doc dims w=%d h=%d  rt[3] x=%d y=%d w=%d h=%d\n",
              rt_w[0], rt_h[0], rt_x[3], rt_y[3], rt_w[3], rt_h[3]);
```

Numbers should match approximately what the legacy layout produces. If `rt_h[0]` is 0 or absurdly large, BFC has a bug.

- [ ] **Step 8.5: Commit**

```bash
git add bin/browser/layout.cc
git commit -m "$(cat <<'EOF'
browser: §4 IFC line-box layout (Plan-2 step 2 of 2)

Fills in collect_inline_atoms / flush_inline so the new run_layout
produces complete geometry. Each inline run becomes a sequence of
LINE_BOX render nodes whose rt_line_atom_first/count points into a
shared atom pool (la_*[]).

White-space modes:
  - WS_NORMAL: collapse whitespace runs to single space; soft break
    points (-3 sentinel) at every space; lines wrap when next atom
    overflows.
  - WS_PRE: preserve whitespace; \n forces hard break (-2 sentinel).
  - WS_NOWRAP: collapse but never break.

Atoms encode font tier, fg/bg color, bold, underline, link index. An
atom with text_off < 0 is a REPLACED/INLINE_BLOCK node reference
(decoded as -off-1) — paint follows the pointer.

run_layout now populates rt_x/y/w/h for the entire render tree.
Legacy paint still drives rendering — Task 9 swaps to render-tree
paint.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: §6 Paint over render tree — cutover

Replace `paint.cc::render` with a render-tree walker. Old `boxes[]`-driven paint becomes dead code (deleted in Task 11). After this commit, the visible pipeline is end-to-end new: tokenizer → tree builder → style resolver → render tree → BFC/IFC layout → render-tree paint.

**Files:**
- Modify: `bin/browser/paint.cc` (rewrite `render` body and the `draw_*` helpers)

- [ ] **Step 9.1: Add `paint_rt_node` walker**

In `bin/browser/paint.cc`, add at the top (above the existing `render` function which we will replace):

```c
/* §6 Render-tree paint — single traversal, bg → border → content order */

void paint_rt_text(int n, int sx, int sy);
void paint_rt_replaced(int n, int sx, int sy);
void paint_rt_marker(int n, int sx, int sy);
void paint_rt_line_box(int n, int sx, int sy);

int rt_screen_x(int n) {
    /* Walk parent chain summing x offsets; viewport_x() is the page origin */
    int x = 0;
    int cur = n;
    while (cur >= 0) {
        x += rt_x[cur];
        cur = rt_parent[cur];
    }
    return x + viewport_x();
}
int rt_screen_y(int n) {
    int y = 0;
    int cur = n;
    while (cur >= 0) {
        y += rt_y[cur];
        cur = rt_parent[cur];
    }
    return y + viewport_y() - scroll_y;
}

void paint_rt_node(int n) {
    int sx = rt_screen_x(n);
    int sy = rt_screen_y(n);
    int w  = rt_w[n];
    int h  = rt_h[n];

    /* Off-screen cull */
    if (sy + h < viewport_y()) return;
    if (sy > viewport_y() + viewport_h()) return;

    int kind = rt_kind[n];
    int cs = rt_style[n];

    /* 1. Background */
    int bg = cs_bg[cs];
    if (bg >= 0 && (kind == RT_BLOCK || kind == RT_INLINE_BLOCK ||
                    kind == RT_LIST_ITEM || kind == RT_TABLE_CELL)) {
        gfx2d_fill_rect(sx, sy, w, h, bg);
    }

    /* 2. Border (1px) */
    if (cs_border[cs][0] || cs_border[cs][1] || cs_border[cs][2] || cs_border[cs][3]) {
        int bc = cs_border_color[cs];
        if (cs_border[cs][0]) gfx2d_fill_rect(sx, sy, w, 1, bc);
        if (cs_border[cs][2]) gfx2d_fill_rect(sx, sy + h - 1, w, 1, bc);
        if (cs_border[cs][3]) gfx2d_fill_rect(sx, sy, 1, h, bc);
        if (cs_border[cs][1]) gfx2d_fill_rect(sx + w - 1, sy, 1, h, bc);
    }

    /* 3. Content */
    if (kind == RT_TEXT) {
        paint_rt_text(n, sx, sy);
    } else if (kind == RT_REPLACED) {
        paint_rt_replaced(n, sx, sy);
    } else if (kind == RT_LIST_MARKER) {
        paint_rt_marker(n, sx, sy);
    } else if (kind == RT_LINE_BOX) {
        paint_rt_line_box(n, sx, sy);
    }

    /* 4. Children */
    int c = rt_first_child[n];
    while (c >= 0) {
        paint_rt_node(c);
        c = rt_next[c];
    }
}
```

- [ ] **Step 9.2: Implement leaf-paint helpers**

```c
void paint_rt_text(int n, int sx, int sy) {
    /* RT_TEXT outside a LINE_BOX (rare — orphan text node not flushed): just draw */
    int cs = rt_style[n];
    int tier = cs_font_size_tier[cs];
    int fg = cs_color[cs];
    if (fg < 0) fg = 0x000000;
    char buf[256];
    int len = rt_text_len[n];
    if (len > 255) len = 255;
    for (int k = 0; k < len; k++) buf[k] = attr_pool[rt_text_off[n] + k];
    buf[len] = 0;
    gfx2d_text(sx, sy, buf, fg, tier);
    if (cs_text_dec[cs] & TD_UNDERLINE) {
        gfx2d_fill_rect(sx, sy + tier_line_h(tier) - 2, len * tier_char_w(tier), 1, fg);
    }
}

void paint_rt_line_box(int n, int sx, int sy) {
    int first = rt_line_atom_first[n];
    int count = rt_line_atom_count[n];
    for (int k = first; k < first + count; k++) {
        if (la_x[k] < 0) continue;       /* sentinel atom — break point not painted */
        int ax = sx + la_x[k];
        int tier = la_font_tier[k];
        int ay = sy + (rt_h[n] - tier_line_h(tier));    /* baseline-ish */
        if (la_text_off[k] < 0) {
            /* Replaced/inline-block reference */
            int rt_n = -la_text_off[k] - 1;
            rt_x[rt_n] = la_x[k];
            rt_y[rt_n] = sy - rt_screen_y(rt_parent[rt_n]) + viewport_y() - scroll_y;
            paint_rt_node(rt_n);
            continue;
        }
        int fg = la_fg[k];
        if (fg < 0) fg = 0x000000;
        int bg = la_bg[k];
        if (bg >= 0) gfx2d_fill_rect(ax, ay, la_w[k], tier_line_h(tier), bg);
        char buf[256];
        int len = la_text_len[k];
        if (len > 255) len = 255;
        for (int kk = 0; kk < len; kk++) buf[kk] = attr_pool[la_text_off[k] + kk];
        buf[len] = 0;
        gfx2d_text(ax, ay, buf, fg, tier);
        if (la_bold[k]) gfx2d_text(ax + 1, ay, buf, fg, tier);
        if (la_underline[k]) {
            gfx2d_fill_rect(ax, ay + tier_line_h(tier) - 2, la_w[k], 1, fg);
        }
    }
}

void paint_rt_replaced(int n, int sx, int sy) {
    int tag = (rt_dom[n] >= 0) ? n_tag[rt_dom[n]] : 0;
    if (tag == T_IMG) {
        /* Plan-2: placeholder. Plan 3 fetches and decodes. */
        gfx2d_fill_rect(sx, sy, rt_w[n], rt_h[n], 0xE0E0E0);
        gfx2d_fill_rect(sx, sy, rt_w[n], 1, 0x808080);
        gfx2d_fill_rect(sx, sy + rt_h[n] - 1, rt_w[n], 1, 0x808080);
        gfx2d_fill_rect(sx, sy, 1, rt_h[n], 0x808080);
        gfx2d_fill_rect(sx + rt_w[n] - 1, sy, 1, rt_h[n], 0x808080);
        gfx2d_text(sx + 4, sy + 4, "[img]", 0x404040, 1);
        return;
    }
    if (tag == T_INPUT) {
        /* Reuse legacy draw_input_box logic, but keyed off rt_input_idx */
        gfx2d_fill_rect(sx, sy, rt_w[n], rt_h[n], 0xFFFFFF);
        gfx2d_fill_rect(sx, sy, rt_w[n], 1, 0x808080);
        gfx2d_fill_rect(sx, sy + rt_h[n] - 1, rt_w[n], 1, 0x808080);
        gfx2d_fill_rect(sx, sy, 1, rt_h[n], 0x808080);
        gfx2d_fill_rect(sx + rt_w[n] - 1, sy, 1, rt_h[n], 0x808080);
        int ii = rt_input_idx[n];
        if (ii >= 0) gfx2d_text(sx + 2, sy + 2, input_value[ii], 0x000000, 1);
        return;
    }
    if (tag == T_BUTTON) {
        gfx2d_fill_rect(sx, sy, rt_w[n], rt_h[n], 0xC0C0C0);
        /* button label: walk to first text child */
        int c = rt_first_child[n];
        while (c >= 0 && rt_kind[c] != RT_TEXT) c = rt_first_child[c] >= 0 ? rt_first_child[c] : rt_next[c];
        if (c >= 0) {
            char buf[64];
            int len = rt_text_len[c];
            if (len > 63) len = 63;
            for (int k = 0; k < len; k++) buf[k] = attr_pool[rt_text_off[c] + k];
            buf[len] = 0;
            gfx2d_text(sx + 4, sy + 2, buf, 0x000000, 1);
        }
        return;
    }
}

void paint_rt_marker(int n, int sx, int sy) {
    int parent = rt_parent[n];
    int cs = rt_style[parent];
    int ls = cs_list_style[cs];
    char *glyph = "*";
    if (ls == LS_DISC)        glyph = "*";
    else if (ls == LS_CIRCLE) glyph = "o";
    else if (ls == LS_SQUARE) glyph = "#";
    else if (ls == LS_NONE)   return;
    else if (ls == LS_DECIMAL) {
        /* Compute index among LIST_ITEM siblings */
        int idx = 1;
        int sib = rt_first_child[rt_parent[parent]];
        while (sib >= 0 && sib != parent) {
            if (rt_kind[sib] == RT_LIST_ITEM) idx++;
            sib = rt_next[sib];
        }
        char buf[16];
        int p = b_append_int(buf, 0, idx);
        buf[p] = '.'; buf[p+1] = 0;
        gfx2d_text(sx - 16, sy + 2, buf, cs_color[cs] >= 0 ? cs_color[cs] : 0x000000, 1);
        return;
    }
    gfx2d_text(sx - 12, sy + 2, glyph, cs_color[cs] >= 0 ? cs_color[cs] : 0x000000, 1);
}
```

- [ ] **Step 9.3: Replace `render()` body**

Find existing `render()` in `paint.cc`. Replace its body:

```c
void render() {
    /* Clear viewport */
    int bg = (cs_count > 0 && cs_bg[0] >= 0) ? cs_bg[0] : 0xFFFFFF;
    gfx2d_fill_rect(viewport_x(), viewport_y(), cur_cw - 12, viewport_h(), bg);

    /* Walk render tree from root */
    if (rt_count > 0) paint_rt_node(0);

    /* Chrome */
    draw_address_bar(0, 0, cur_cw);
    draw_status_bar(0, cur_ch - STATUS_H, cur_cw);
    draw_scrollbar(cur_cw - 12, ADDR_H);
}
```

- [ ] **Step 9.4: Update `nav.cc` to drop the legacy `run_layout` call**

Find the `legacy_run_layout()` call inserted in Task 7. Delete it (and the legacy `run_layout` invocation). Only call the new `run_layout()`.

- [ ] **Step 9.5: Build + smoke — full pipeline**

Run: `make 2>&1 | tail -10`. Expect success.

Run: `make run-net`, navigate to each reference URL, compare to `/tmp/browser-pre-p2/*.png`. Now the new pipeline drives rendering — visible deltas are EXPECTED on:

- Pages with `<style>` author CSS: layout/colors should improve (or at least change).
- Pages with nested inline runs across block boundaries: layout should be cleaner (anon-block wrappers fix the bug).
- Tables: degraded — laid out as blocks. `05-hn.png` will look very different (cells stack vertically). Document expected divergence.
- Lists: should now show real bullet/numeric markers.
- Wikipedia (HTML over HTTP first to avoid TLS variability): should show heading hierarchy, paragraph spacing.

If anything completely blanks out: render-tree walk has a bug in `rt_screen_x`/`y` (likely sign error or unintended scroll subtraction). Add a `serial_printf("paint_rt %d at %d,%d kind=%d\n", n, sx, sy, kind);` at top of `paint_rt_node` and verify coordinates.

- [ ] **Step 9.6: Commit**

```bash
git add bin/browser/paint.cc bin/browser/nav.cc
git commit -m "$(cat <<'EOF'
browser: §6 cutover — paint walks the render tree

render() now walks rt_*[]: for each node, paint background, border,
content (text / replaced / marker / line-box atoms), then children.
Off-screen cull on (sy + h < viewport) || (sy > viewport_bottom).

Legacy box-list paint (draw_text/input/button/image_box, the boxes[]
walker) is no longer driven from render(); it's dead code, removed in
Task 11.

End state of Plan 2: full new pipeline drives visible output
(tokenizer → tree builder → style resolver → render tree → BFC/IFC
layout → render-tree paint). Tables degrade to block layout
(Plan 3 fixes); images render as 80×60 placeholder rects (Plan 3
adds fetch+decode).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: §6 Hit testing + §10 form rebind + §11 history rebind

Update `input.cc::hit_box` and surrounding helpers to query the render tree instead of the legacy `boxes[]` array. Update `nav.cc::submit_form` to walk DOM children of the form's node via `dom_attr_get`. History (`HIST_MAX = 16` per §11) is already correct from Task 1's pool bump; verify Backspace flow still works.

**Files:**
- Modify: `bin/browser/input.cc`
- Modify: `bin/browser/nav.cc`

- [ ] **Step 10.1: Replace `hit_box` with render-tree hit test**

In `bin/browser/input.cc`:

```c
/* Reverse depth walk: return the deepest render node containing (mx, my+scroll_y). */
int rt_hit(int n, int mx, int my) {
    int sx = rt_screen_x(n);
    int sy = rt_screen_y(n);
    if (mx < sx || mx >= sx + rt_w[n] || my < sy || my >= sy + rt_h[n]) return -1;
    /* Try children deepest-first (last-painted-on-top) */
    int c = rt_first_child[n];
    int last = -1;
    while (c >= 0) { last = c; c = rt_next[c]; }
    /* Walk siblings in reverse */
    int sibs[256];
    int nsib = 0;
    c = rt_first_child[n];
    while (c >= 0 && nsib < 256) { sibs[nsib++] = c; c = rt_next[c]; }
    for (int k = nsib - 1; k >= 0; k--) {
        int h = rt_hit(sibs[k], mx, my);
        if (h >= 0) return h;
    }
    return n;
}

int hit_box(int mx, int my) {
    if (rt_count == 0) return -1;
    return rt_hit(0, mx, my);
}
```

The old `hit_box` returned a `boxes[]` index; consumers used `b_link_idx[hit]` etc. Now it returns a render-node index; consumers use `rt_link_idx[hit]`, `rt_input_idx[hit]`. Update all callers in `input.cc`:

```c
/* old */
int hit = hit_box(mx, my);
if (hit >= 0 && b_link_idx[hit] >= 0) navigate(...);
/* new */
int hit = hit_box(mx, my);
if (hit < 0) return;
/* Walk up looking for the nearest link-bearing ancestor */
int link = -1;
int input = -1;
int cur = hit;
while (cur >= 0) {
    if (rt_link_idx[cur] >= 0) { link = rt_link_idx[cur]; break; }
    if (rt_input_idx[cur] >= 0) { input = rt_input_idx[cur]; break; }
    cur = rt_parent[cur];
}
if (link >= 0) {
    char *u = attr_pool + link_url_off[link];
    char absu[URL_MAX];
    compute_url_relative(u, absu, URL_MAX);
    navigate(absu);
} else if (input >= 0) {
    focus_mode = FOCUS_INPUT;
    focused_input = input;
    render();
}
```

Apply the same swap in `handle_hover` (cursor change on link).

- [ ] **Step 10.2: Rebind `submit_form` over new DOM**

In `bin/browser/nav.cc`:

```c
void submit_form(int form_idx) {
    if (form_idx < 0 || form_idx >= forms_count) return;
    int form_n = form_node[form_idx];
    char *action = dom_attr_str(form_n, "action");
    if (action == 0) action = cur_path;

    /* Build query string: walk DOM descendants for <input> nodes belonging to this form */
    char query[1024];
    int qlen = 0;
    int first_pair = 1;
    int c = n_first_child[form_n];
    int stack[64];
    int sp = 0;
    if (c >= 0) stack[sp++] = c;
    while (sp > 0) {
        int node = stack[--sp];
        if (n_tag[node] == T_INPUT) {
            char *name = dom_attr_str(node, "name");
            int ii = -1;
            for (int k = 0; k < inputs_count; k++) {
                if (input_node[k] == node) { ii = k; break; }
            }
            char *value = (ii >= 0) ? input_value[ii] :
                          ((dom_attr_get(node, "value") >= 0) ? attr_pool + dom_attr_get(node, "value") : "");
            if (name && qlen + 200 < 1024) {
                if (!first_pair) query[qlen++] = '&';
                first_pair = 0;
                int nl = b_strlen(name);
                for (int k = 0; k < nl && qlen < 1023; k++) query[qlen++] = name[k];
                query[qlen++] = '=';
                int vl = b_strlen(value);
                for (int k = 0; k < vl && qlen < 1023; k++) {
                    char ch = value[k];
                    if (ch == ' ') query[qlen++] = '+';
                    else if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                             (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.') {
                        query[qlen++] = ch;
                    } else {
                        if (qlen + 3 < 1024) {
                            char hex[] = "0123456789ABCDEF";
                            query[qlen++] = '%';
                            query[qlen++] = hex[(ch >> 4) & 0xF];
                            query[qlen++] = hex[ch & 0xF];
                        }
                    }
                }
            }
        }
        /* push children */
        int cc = n_first_child[node];
        while (cc >= 0 && sp < 64) { stack[sp++] = cc; cc = n_next[cc]; }
    }
    query[qlen] = 0;

    /* Build target URL: action + '?' + query */
    char target[URL_MAX];
    int tp = b_append(target, 0, action);
    if (qlen > 0) {
        target[tp++] = '?';
        for (int k = 0; k < qlen; k++) target[tp++] = query[k];
    }
    target[tp] = 0;

    char absu[URL_MAX];
    compute_url_relative(target, absu, URL_MAX);
    navigate(absu);
}
```

- [ ] **Step 10.3: Build + smoke — interactive flows**

Run: `make 2>&1 | tail -5`. Expect success.

Run: `make run-net`. Test:
1. `browser http://example.com/` — click "More information..." link → navigates to IANA page.
2. Backspace → back to example.com.
3. Ctrl-L, type `https://www.google.com/`, Enter — page renders (search box visible as input box).
4. Click search box, type `cupidos`, press Enter → form submits as GET, navigates to results URL (results page may not render correctly; that's pre-existing).
5. Mouse wheel scrolls.

- [ ] **Step 10.4: Commit**

```bash
git add bin/browser/input.cc bin/browser/nav.cc
git commit -m "$(cat <<'EOF'
browser: §6 hit_box + §10 form submit rebind to render tree / new DOM

hit_box walks rt_*[] reverse-depth, returns deepest render node
containing the mouse point. Click handler walks parent chain looking
for nearest rt_link_idx (follow link) or rt_input_idx (focus input).
Hover does the same for cursor styling.

submit_form pulls action via dom_attr_str(form_node, "action"),
collects all <input> descendants of the form's DOM node by walking
n_first_child/n_next, URL-encodes name=value pairs, navigates to
action?query (GET only per spec §10).

History (HIST_MAX=16) and Backspace path use existing logic; pool
size already bumped in Task 1.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 11: Cleanup — delete dead legacy layout/paint code

After Task 9's cutover, the legacy `boxes[]` pipeline and all `b_*` parallel arrays, `legacy_run_layout`, `legacy_layout_node`, `legacy_emit_box`, `draw_text_box`, `draw_input_box`, `draw_button_box`, `draw_image_box` are unreachable. Remove them. This is a delete-only change.

**Files:**
- Modify: `bin/browser/layout.cc` (remove legacy_*)
- Modify: `bin/browser/paint.cc` (remove draw_*_box helpers)
- Modify: `bin/browser/main.cc` (remove `boxes[]` and `b_*` parallel arrays, `MAX_BOXES` enum entry)

- [ ] **Step 11.1: Delete `boxes[]` arrays from `main.cc`**

Find and delete:

```c
int boxes_count;
int b_kind[MAX_BOXES];
int b_x[MAX_BOXES];
int b_y[MAX_BOXES];
int b_w[MAX_BOXES];
int b_h[MAX_BOXES];
int b_text_off[MAX_BOXES];
int b_text_len[MAX_BOXES];
int b_link_idx[MAX_BOXES];
int b_input_idx[MAX_BOXES];
int b_color[MAX_BOXES];
int b_bg[MAX_BOXES];
int b_bold[MAX_BOXES];
int b_underline[MAX_BOXES];
int b_font_tier[MAX_BOXES];
/* ...whatever else b_* exists in current main.cc */
```

(Adjust to the exact set after Plan 1.) Also remove `MAX_BOXES` from the enum.

- [ ] **Step 11.2: Delete legacy layout from `layout.cc`**

Remove every function with `legacy_` prefix and any helper they referenced (`L_*` state globals, `emit_box`, `register_link`, `newline`, `last_box`, `parent_color/bg/bold/link`, `viewport_w` which is now `viewport_content_w`). The new `run_layout` and its inline helpers are kept.

- [ ] **Step 11.3: Delete `draw_*_box` helpers from `paint.cc`**

Remove `draw_text_box`, `draw_input_box`, `draw_button_box`, `draw_image_box`. Keep `draw_address_bar`, `draw_status_bar`, `draw_scrollbar` — those still drive chrome.

- [ ] **Step 11.4: Build + verify**

Run: `make 2>&1 | tail -10`. If anything fails to compile, the deletion was incomplete or something still references a `b_*` field — search and remove.

Run: `make run-net`, browse all reference URLs, verify rendering matches Task 9's results.

- [ ] **Step 11.5: Commit**

```bash
git add bin/browser/main.cc bin/browser/layout.cc bin/browser/paint.cc
git commit -m "$(cat <<'EOF'
browser: drop legacy box-list layout + paint (dead since Task 9 cutover)

Removes boxes[] / b_*[] parallel arrays, MAX_BOXES enum, legacy_run_layout
+ legacy_layout_* helpers, parent_color/bg/bold/link, draw_*_box paint
helpers. All replaced by the render-tree pipeline introduced in Plan 2.
Browser is functionally unchanged; just less code.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 12: Acceptance run

Final verification. No code change. If a regression is found, patch it inline as a separate commit.

- [ ] **Step 12.1: Visual diff against pre-Plan-2 references**

Run: `make run-net`. For each URL:

| URL | Reference | Expected outcome |
|---|---|---|
| `http://example.com/` | `01-example.png` | Match or improvement (heading sized, link blue underlined, paragraph spaced). |
| `https://www.iana.org/help/example-domains` | `02-iana.png` | Match or improvement; TLS path. |
| `http://info.cern.ch/.../TheProject.html` | `03-info-cern.png` | Match or improvement; nested inline runs cleaner thanks to anon-block wrappers. |
| `http://lite.cnn.com/` | `04-cnn-lite.png` | Match. Paragraph spacing should look better. |
| `http://news.ycombinator.com/` | `05-hn.png` | DEGRADED — cells stack vertically (table fallback). Document this as expected; Plan 3 fixes. |

Take new screenshots, save to `/tmp/browser-post-p2/`. If `05-hn.png` is the only regression, plan succeeds.

- [ ] **Step 12.2: Wikipedia article (HTTPS, large body, real CSS, lists, headings)**

Ctrl-L, type `https://en.wikipedia.org/wiki/HTTP`, Enter.

Expected:
- Title bar shows "HTTP - Wikipedia".
- Heading hierarchy visible (h1, h2 etc.).
- Paragraphs spaced; no run-together text.
- Links blue + underlined.
- Lists indented with markers.
- Infobox table is degraded (stacks vertically) — expected.
- Scrolls smoothly via wheel + scrollbar drag.

If the page truncates or fails: revisit page_buf size, attr_pool size, MAX_NODES, MAX_TOKENS — log boot serial during this run, look for `truncated` status messages.

- [ ] **Step 12.3: Form interaction**

Ctrl-L, `http://www.google.com/`, Enter. Type `cupidos` in the search box (should be focused on click). Enter. Form submits via GET; address bar shows `?q=cupidos&...`. Search results page may not render correctly — that's pre-existing and out of scope.

- [ ] **Step 12.4: Multi-page navigation + history**

From example.com: click link → IANA. Backspace → example.com. Ctrl-L → IANA again. Backspace × 2 → first-loaded page. History ring should show 16 entries before wrap (verify by walking 16+ pages; oldest evicted).

- [ ] **Step 12.5: Keyboard scrolling**

PgDn / PgUp / Home / End / arrow keys move scroll position smoothly. Verify on Wikipedia article (long enough to need scrolling).

- [ ] **Step 12.6: File count + line counts**

```bash
ls bin/browser/
```
Expected:
```
css.cc
dom.cc
input.cc
layout.cc
main.cc
nav.cc
net.cc
paint.cc
parser.cc
render_tree.cc
style.cc
url.cc
util.cc
```

```bash
wc -l bin/browser/*.cc bin/browser.cc
```
Expected total: ~3500-4500 lines (was ~2050 after Plan 1; new files css.cc/style.cc/render_tree.cc add ~1500-2500 LOC; main.cc grows by ~200 LOC for new pools; layout.cc roughly even (legacy out, new in); paint.cc roughly even).

- [ ] **Step 12.7: Memory + performance sanity**

In QEMU shell (after browsing several pages), check:
- `free` (or whatever the shell exposes) — kernel data still has headroom.
- Repeated navigation doesn't slow noticeably (no heap leaks; pools reset per page).

If a leak is suspected, audit `attr_pool_pos`, `css_value_pool_pos`, `dom_ap_count`, `rt_count`, `cs_count`, `la_count` reset paths — all should be zeroed at the top of `parse_html`. The `attr_pool` is the one shared pool that doesn't reset (stores both DOM strings and CSS scratch); accept the leak for now and deal with it in Plan 3 cleanup.

- [ ] **Step 12.8: Commit acceptance result (only if regression patched)**

If 12.1 through 12.7 pass without code changes: no commit, plan complete.

If a regression was patched inline:

```bash
git add <files>
git commit -m "$(cat <<'EOF'
browser: fix regression in plan-2 acceptance — <one-line>

<paragraph: what broke and how>

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Self-Review

**Spec coverage:**
- §1 HTML5-lite tokenizer + tree builder + entities + DOM: covered by Tasks 2, 3.
- §2 CSS lexer/parser + UA stylesheet + cascade: covered by Tasks 4, 5.
- §3 Render tree with anon-block wrappers + list markers: covered by Task 6. **Anonymous table-ancestor wrapping deferred to Plan 3** (see `rt_anon_table_fixup` no-op note in 6.2). Documented gap.
- §4 BFC + IFC + line boxes + white-space + list markers: covered by Tasks 7, 8 (and marker paint in 9.2).
- §5 Tables: out of scope; degrade to block layout. Plan 3.
- §6 Paint, hit test, font tiers: covered by Tasks 9, 10.
- §7 Images: out of scope; placeholder paint only. Plan 3.
- §8 Memory limits: covered by Task 1.
- §9 Network/TLS/DNS: unchanged (Phase 8 fixes already landed on `browser-build-split`).
- §10 Forms: rebind covered by Task 10. Same GET-only logic as today.
- §11 History: pool size bumped in Task 1; flow unchanged.

**Placeholder scan:** No "TBD", "implement later", or "fill in details". Two contingency notes:
- Task 4.6 fallback for forward-reference of `css_parse_block` from `parser.cc` — full alternative documented (function-pointer global initialised in `browser_main`).
- Task 5.4 inheritance limitation: italic on `<em>` doesn't propagate to nested `<h1>`. Documented as accepted Plan-2 limitation; spec §2 doesn't promise full inheritance for non-listed properties.

**Type / signature consistency:**
- `dom_attr_get(node, name)` returns `int` (offset or `-1`); `dom_attr_str(node, name)` returns `char *` (or `NULL`). Used consistently across Tasks 3, 5, 6, 9, 10.
- `alloc_node(tag, parent, tok_idx)` — three args throughout Task 3.
- `rt_alloc(kind, dom, parent, style_cs)` — four args throughout Tasks 6, 8.
- `cs_apply_property(cs, prop, val_off, val_len)` — four args throughout Tasks 4, 5.
- `flush_inline(parent, *atom_pile_first, *atom_pile_count, cx, *cy, max_w)` — six args throughout Tasks 7, 8.
- `paint_rt_node(n)` / `rt_screen_x(n)` / `rt_screen_y(n)` — single-arg throughout Tasks 9, 10.
- Atom sentinels `la_x = -1 / -2 / -3` consistent in Tasks 8 (write) and 9.2 (read).

---

## Risks & Notes for executor

1. **CupidC forward-reference behavior is Plan-2's biggest unknown.** `parse_html` (in `parser.cc`) calls `css_parse_block` (in `css.cc`), `style_resolve_all` (in `style.cc`), `build_render_tree` (in `render_tree.cc`). All are defined in includes that come AFTER `parser.cc` in the trampoline. If the JIT compiles them in concatenated order and accepts forward refs to functions in the same TU (which Plan 1 demonstrated for globals/main), this works. If a future CupidC tightening breaks this, fall back to: define `void browser_dispatch_pipeline_stage(int stage)` in `main.cc` (which sees everything because it's first), and have `parse_html` call it. Function-pointer globals are another option.

2. **`MAX_TOKENS = 16384` and `MAX_ATTR_PAIRS = 8192` may be tight on Wikipedia.** Measured offline at ~12 K / 6 K for `/wiki/HTTP`; longer articles can push higher. Bump if `tok_count` ever equals `MAX_TOKENS` exactly or `nodes_count` truncates noticeably. The `truncated` status message hooks should remain user-visible.

3. **`attr_pool` is shared between DOM strings, tokenizer attr values, and decoded entity buffers.** It does NOT reset per-page in this plan (it stores style strings, attribute values, etc.). Accept the leak for Plan 2; Plan 3 introduces a per-page arena reset.

4. **`MAX_CSS_RULES = 256` is far too low for real Wikipedia CSS** (which ships hundreds of KB). Spec §2 explicitly accepts that overflow rules drop and UA defaults still apply; rendering will be visibly different from a real browser but not broken. If a test page becomes unreadable due to dropped rules, the right move is bumping the cap, not changing the design.

5. **Tables degrade to block fallback in Plan 2.** HN, Wikipedia infobox, and any data table will look wrong (cells stack vertically). This is the explicit cost of choosing scope D without §5 (tables). Document in commit messages and the post-Plan-2 reference screenshots.

6. **No host-side test harness.** Every "verify" step is manual. Consider writing a `tools/browser_smoke.py` that drives QEMU + the shell over serial (similar to `tools/net_test.py` from Phase 4) — but that's a separate plan.

7. **Don't mix unrelated cleanup.** This plan's invariant is "render-pipeline rewrite, no other code touched." Any tempted "while I'm here" fix in `kernel/`, `bin/curl.cc`, `bin/notepad.cc`, etc. should be a separate commit on a different branch.

8. **If a task fails the smoke check**, do NOT proceed to the next task. The failure mode in the next task will be confused. Roll back with `git reset --hard HEAD~1`, diagnose, retry. Tasks 3, 8, 9 are the highest-risk; Tasks 1, 2, 4, 5, 6, 7, 10, 11 are lower-risk and can usually be re-attempted incrementally.

