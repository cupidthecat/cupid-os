# browser: universal webfonts + closer-to-Blink rendering, with cupidc compiler upgrades

## Why

Two top-level goals drove this branch:

1. **Make webfonts work on real sites.** The previous browser only loaded TTF/OTF, capped at 4 slots per page, walked the `src:` list as "first-match-and-stop", and had no `unicode-range` filtering — so Google-Fonts-style stylesheets that declare 12+ subsetted faces per family with WOFF2-first src lists came out as the OS fallback face instead of the served font.
2. **Close the visible gaps versus Blink.** Layout still used 5-tier font-size buckets even though the kernel had a TTF rasterizer that could deliver real metrics; `<img>` was a placeholder rectangle; `position: relative | absolute | fixed` was unsupported; line-height + baseline math drifted between mixed-size runs.

Hitting both required CupidC compiler work along the way (browser globals overflowed the data section; 3D arrays were rejected; multi-declarator and error reporting were thin), so the compiler picked up several upgrades too.

## What ships

### Browser: rendering engine

**Real font metrics in the layout/paint pipeline.** `effective_line_h` (`bin/browser/layout.cc`) now consults `fontsys_line_height(face, size_px)` for the unset-line-height fallback and uses `fontsys_ascent` for baseline placement. Mixed-size inline runs no longer drift because the line box pulls its height from the actual face metrics rather than a 5-bucket tier table.

**`<img>` actually fetches and decodes.** New `bin/browser/image.cc` mirrors the `font_face.cc` pump pattern: after first paint we walk the DOM for `<img src=...>`, queue up to 32 entries, fetch one per render tick via `fetch_url`, and hand the bytes to `gfx2d_image_load_mem` (PNG and JPEG; the kernel decoders sniff magic). Decoded handles store on `n_img_handle[4096]` per DOM node; `paint_rt_replaced` calls `gfx2d_image_draw_scaled` when a handle is present, falling back to the `[img]` placeholder otherwise. `nav.cc` calls `image_evict_all` on navigate so the previous page's decoded buffers are freed before parsing the new document.

**`position: relative | absolute | fixed` (+ `z-index`).** New `cs_position`, `cs_top/right/bottom/left`, `cs_z_index` arrays in `main.cc`; CSS parse + cascade in `css.cc` + `style.cc`. `render_tree.cc:rt_collect_oof` collects out-of-flow nodes during build into `rt_oof_list`. After the in-flow `layout_block` pass, `layout_oof` resolves each absolute/fixed node against its containing block (nearest positioned ancestor, or viewport for fixed). `rt_is_oof[]`/`rt_is_fixed[]` flags steer `rt_screen_x/y` so absolute boxes paint at their absolute document-space position and fixed bars ignore `scroll_y`. `relative` adds `cs_top`/`cs_left` as a paint-time offset (no reflow), matching CSS 2.1 §9.4.3.

**Per-codepoint face resolution at width-measure and paint time.** `text_slice_w_cs` (`layout.cc`) now walks UTF-8 codepoint by codepoint, calls a new `cs_face_id_for_cp(cs, cp)` per codepoint, and sums `fontsys_advance` per cp — Cyrillic/Greek/etc. route through the right `unicode-range` subset face instead of being forced through the Latin face. `paint_rt_line_box` does the same walk and groups consecutive same-face codepoints into single `fontsys_draw_run_styled` calls. The atom carries the originating cs index in a new `la_cs[8192]` array so paint can re-resolve without splitting atoms in the IFC.

**CSS parser additions.** `position`, `top`, `right`, `bottom`, `left`, `z-index`, `float`, `clear` parse and cascade. `unicode-range:` descriptor on `@font-face` parses `U+XXXX`, `U+XXXX-YYYY`, and wildcard `U+XX??` tokens into `(lo, hi)` pairs. `local()` tokens in the `src:` list are silently skipped.

**Margin collapsing.** `layout_block` already had adjacent-sibling collapse, parent-first-child collapse, parent-last-child collapse, and self-collapsing-block handling against CSS 2.1 §8.3.1. This branch verifies the existing path and removes a stale "deferred" comment.

### Browser: universal webfont support

**Slot capacity bumped 4 → 128.** `bin/browser/font_face.cc` parallel arrays moved from a fixed `[4]` to `[128]`. Static cost: ~410 KiB (URL strings dominate), comfortably inside the now-8 MiB browser data section.

**Three-deep `src:` fallback chain.** `css_at_font_face` walks the entire `src` list, scoring each entry by format (`truetype/otf/ttf/opentype = 4`, `woff2 = 3`, `woff = 2`, bare `url() = 1`, anything else skipped), then takes the top three by score with source-order tiebreaking. `font_face_add_rule_n` stores up to three URLs per slot (`ff_url[128][3][1024]`); `ff_advance_one_pending` tries `ff_url_tried[s]` first and on failure advances to the next URL before declaring the slot dead.

**WOFF1 decode (real, end to end).** New `bin/browser/woff.cc:woff1_unwrap` reads the 44-byte WOFF1 header, walks the per-table directory, and rebuilds a plain sfnt with the standard 12-byte offset subtable + 16-byte directory entries. Each table either copies through (when `compLength == origLength`) or is zlib-stripped and inflated via the existing kernel `kdeflate_raw` primitive, mirroring how `kernel/gfx/png.c` already feeds IDAT chunks. WOFF2 is a stub that returns NULL so the `src:` walker falls through to a WOFF/TTF entry — the Brotli decoder + glyf/loca transform inverse is intentionally deferred (see "Not in this PR").

**`unicode-range` matching.** `font_face_match_cp(family, weight, italic, codepoint)` filters webfont slots by a per-slot range list (`ff_range_lo/hi[128][8]`) and prefers range-having slots over range-less ones (range-having gets a -50 score bias). The previous range-blind `font_face_match` is preserved as a thin wrapper that passes `cp = -1`, so callers without a codepoint keep working.

**Per-page font eviction.** New `kernel/gfx/fontsys.c:fontsys_unregister(face_id)` clears the registry slot, `kfree`'s the blob if we owned it, and walks the glyph cache freeing every entry that referenced the face. `font_face_init` (called from `nav.cc` on navigate) walks the previous page's slots, calls `fontsys_unregister`, `kfree`'s the blob, then resets the slot tables. Heap usage now plateaus across navigations instead of climbing.

**Diagnostics.** `font_face_pump` logs an end-of-pass summary on the serial console: `[browser] @font-face: N loaded / M failed / K skipped`. Per-fetch lines show `face_id` and byte count on success; format detect (`'wOFF'` vs `'wOF2'` magic) and unwrap failures emit named warnings.

### Kernel APIs added or changed

- `fontsys_unregister(int face_id)` (kernel/gfx/fontsys.h, fontsys.c).
- `FONTSYS_MAX_FACES` 128 → 160 (covers 128 webfont slots plus the bundled Liberation faces).
- `kdeflate_raw` and `fontsys_unregister` exposed to CupidC programs via the BIND table (`kernel/lang/cupidc.c`).

### CupidC compiler

The browser source pushed past several latent CupidC limits. Each of the following is implemented in the compiler rather than worked around in the consuming code.

**Static data section: 4 MiB → 8 MiB.** `CC_MAX_DATA` doubled in `kernel/lang/cupidc.h`. The PMM region for the JIT image (`0x01000000-0x01900000`) already had 9 MiB reserved, so 1 MiB code + 8 MiB data fits with no paging changes.

**3D arrays.** `T name[A][B][C]` now parses for global declarations; the symbol tracks both `array_elem_size` (outer stride = `B*C*base`) and a new `array_dim2` (middle stride = `C*base`). The assignment-side subscript path (`cc_parse_subscript_assignment`) was extended so `arr[i][j][k] = val` scales each subscript by the right stride. The read-side subscript already supported chaining via `cc_last_expr_elem_size`; a new `cc_last_expr_dim2` carries the middle stride through the first subscript so `arr[i][j][k]` reads compile correctly.

**Multi-declarator local declarations.** `int a = 0, b = 0, c;` for `int`/`char`/pointer types is now accepted in `cc_parse_declaration` — the function loops on `,` and parses each declarator with the same base type. `float`/`double`/`struct` multi-decl falls back to the existing per-statement form.

**Better `unexpected token` errors.** `cc_expect` no longer prints a bare `unexpected token`; it now embeds the offending token's text and a numeric type tag (`unexpected token: '[' (type=82)`), making it possible to find the exact source location and the specific lexer category that failed without a debugger.

**Multi-error reporting.** `cc_error` now mirrors every error to the serial console regardless of whether it's the first call (the parser still bails on the first per `cc->error`, but cascade diagnostics from later sanity checks are no longer muted). `cc_parse_program`'s top-level loop adds error-recovery: on `cc->error`, the parser skips ahead to the next semicolon or matching `}` boundary, clears `cc->error`, and resumes — up to 16 cascading errors per file before bailing.

### JavaScript engine (already on the branch)

Pre-existing improvements on this branch that this PR carries forward:

- `innerHTML` setter parses small HTML fragments into DOM children with a tag-stack walker (handles `b/i/em/strong/span/code/font/a/br`, void tags, attribute skip, mismatched-close tolerance) (`bin/browser/js_dom.cc:jsd_parse_inner_html`).
- `textContent` and inline tag append helpers (`jsd_make_text_child`, `jsd_append_text`).
- Interpreter cleanups in `js_interp.cc`.

These predate this session but are part of the branch's overall scope.

## Files changed in this branch

**Browser (modify):** `bin/browser.cc`, `bin/browser/css.cc`, `bin/browser/font_face.cc`, `bin/browser/layout.cc`, `bin/browser/main.cc`, `bin/browser/nav.cc`, `bin/browser/paint.cc`, `bin/browser/render_tree.cc`, `bin/browser/style.cc`, `bin/browser/js_dom.cc`, `bin/browser/js_interp.cc`.

**Browser (new):** `bin/browser/woff.cc`, `bin/browser/woff2.cc`, `bin/browser/image.cc`.

**Kernel:** `kernel/gfx/fontsys.c`, `kernel/gfx/fontsys.h`, `kernel/lang/cupidc.c`, `kernel/lang/cupidc.h`, `kernel/lang/cupidc_parse.c`.

## Test plan

Run via `make run-net` (or `make run-net-e1000`). Watch the serial output for `[browser] @font-face: ...` and `[browser] <img> load: ...` lines.

- **`http://example.com/`** — sans-serif body text resolves to LiberationSans (no @font-face on this site); margin collapse leaves a single gap between the heading and paragraph.
- **`http://lite.cnn.com/`** — thumbnails actually decode and render at intrinsic dimensions; gaps tighten.
- **`http://en.m.wikipedia.org/wiki/HTTP`** — older mobile CSS lists WOFF after WOFF2 in the `src:` chain, so the WOFF1 fallback path lands the served face; infobox renders close to its real position.
- **`https://fonts.googleapis.com/css2?family=Roboto:wght@400;700&display=swap`** consumed by a small static page — exercises the 128-slot capacity and unicode-range route.
- **Synthetic page** with `<div style="position:absolute;top:80;left:200">overlay</div>` and `<div style="position:fixed;top:0;right:0">sticky</div>` — overlay paints relative to its containing block; sticky bar stays visible while scrolling.
- **Memory regression** — navigate between three font-heavy pages while watching `serial.log`; heap usage plateaus, kernel `face_count` stays under 160.

---

## Phase 2 — CSS variables, calc, real-site polish, JPEG correctness

A second pass against real sites and the e9 / f1 / f3 test pages closed
the remaining visible gaps. Each item here came from a side-by-side
debug session against Chrome.

### CSS custom properties + `calc()`

**Custom-property cascade (`--name`).** `bin/browser/css.cc` declaration
parser now recognises any `--prefix` property, interns its name + raw
value into the rule pool, and tags the rule with a new `CP_CUSTOM_VAR`
prop_id. Storage `cs_var_name_off/len/val_off/val_len[4096][8]` +
`cs_var_count[4096]` (re-added now that the kernel data section is
8 MiB). Cascade in `style.cc:style_resolve_all` runs a second pass
specifically for `CP_CUSTOM_VAR` rules: dedupes by name, applies the
highest-scoring rule per name. `apply_inline_style` recognises
`style="--foo: bar"` too. Reference: Blink later versions store custom
properties on `RenderStyle` as a hash map; the parallel-array layout
here gives the same lookup semantics within cupidc's literal-size
constraints.

**`var()` resolver with ancestor-walk inheritance.**
`style.cc:cs_var_lookup(cs, name, len, ...)` walks the DOM ancestor
chain (cs index == DOM node index), capped at 32 hops, and returns the
narrowest defined value. `cs_var_substitute(cs, val_off, val_len, buf,
cap)` scans value bytes for `var(--name [, fallback])`, expands
recursively, and writes the substituted text into a scratch buffer.
`cs_apply_property` runs this pre-resolution before dispatch when the
raw value contains a `var(` substring, so `color: var(--brand)` and
`width: calc(var(--gutter) - 4px)` both work without per-property
plumbing. Reference:
`blink/Source/core/css/CSSVariableResolver.cpp::resolveVariableReference`.

**`calc()` recursive-descent evaluator.**
`style.cc:eval_calc(text, len, base_px)` parses + evaluates the
expression at `cs_apply_property` time. Tracks a `{px, pct}`
accumulator (Blink `CSSCalcExpressionNode` style) so
`calc(100% - 16px)` resolves to `containing_block * 100/100 - 16`
without losing precision. Grammar: `expr = mul (('+'|'-') mul)*`,
`mul = term (('*'|'/') term)*`, `term = '(' expr ')' | '-' term |
number unit?`. Units: `px`, `pt`, `em` (treated as px in v1), `%`.
Plugged into `css_value_len` so any length-typed property (width,
margin, top/left, etc.) automatically picks up calc support.

**Cupidc workaround:** the original `eval_calc` was a struct +
recursive function with `struct ec_state *s` parameters. Cupidc's
codegen for nested `s->t[s->p]` patterns and multi-init declarators
inside hot loops misbehaved at runtime; rewrote to module-level
globals (`ec_t`, `ec_n`, `ec_p`, `ec_err`) which keeps the same
semantics with no struct-pointer codegen surface. The cupidc-side
fix landed too — see "Cupidc upgrades" below.

### CSS named-color table

`bin/browser/dom.cc:parse_color_named` now covers the full CSS Color
Module Level 4 keyword set (~140 colours) — previously only 16. The
direct trigger was `--brand: tomato` in the e4 test, which fell back
to the default colour because `tomato` wasn't in the table; the
substituted value was correct but `parse_color` returned 0. Adds
`transparent` and `currentcolor` (mapped to 0 for now). Reference:
`blink/Source/core/css/CSSColorParser` keyword table.

### Position layout — shrink-to-fit + double-shift fixes

**`position: absolute` shrink-to-fit (Blink `RenderBox::shrinkToFitWidth`).**
`layout.cc:layout_oof_one` now pre-measures intrinsic content width
via `oof_intrinsic_text_w` (recursive walk over text descendants
summing `text_slice_w_cs`), then lays out the subtree ONCE at the
shrunk width. Without this, `right: 8px` with `width: auto` used the
full containing block, stretching the element across the parent. The
naive "lay out at cb_w then re-layout" approach caused duplicate
line-box children appended to `rt_first_child[oof]` and double-painted
text — fixed by measuring before layout instead of relayout.

**Out-of-flow children skipped by in-flow pass.** `layout_block`'s
child loop (`bin/browser/layout.cc`) now skips children whose
`cs_position` is `ABSOLUTE` or `FIXED`. Reference:
`blink/Source/core/rendering/RenderBlockFlow::layoutBlockChildren`
checks `isOutOfFlowPositioned()` before placing in flow. Without this
a `position: fixed` nav bar still consumed its in-flow vertical space,
pushing the next block down by the bar height.

**Relative paint double-shift.** `rt_screen_x/y` walked rt parent chain
adding `cs_top`/`cs_left` for every node whose `cs_position ==
POS_RELATIVE`. Anonymous wrappers and line boxes share the relative
element's cs (rt_alloc passes parent's style), so the offset compounded
once per RT level. Gated by `rt_dom[cur] >= 0` so anonymous nodes
don't re-apply the shift.

**Atom-position offset double-counting.**
`flush_inline` stored `la_x[k] = x` where `x` started at `cx` (the
parent block's `padding_l + border_l`), AND set `rt_x[line_box] = cx`.
Paint then computed `sx = rt_screen_x(line_box) + la_x[k]` —
`cx` was added twice, so text inside any block with padding was
shifted right by an extra `padding_l`. Fixed by storing
`la_x[k] = x - cx` (atom offset relative to its line box). Visible on
e9 corner labels with `padding: 4` where text appeared at the right
edge instead of centred.

### Glyph-level fallback chain

**`fontsys_face_has_cp` + `fontsys_find_face_with_cp`.** Two new
kernel helpers (`kernel/gfx/fontsys.c`) exposed via cupidc binds.
`cs_face_id_for_cp` (browser side) calls `fontsys_face_has_cp` after
`fontsys_match` returns the generic-family face; if the face's cmap
doesn't carry the codepoint, walks every registered face for one
that does. Reference:
`blink/Source/platform/fonts/FontFallbackList::fontDataForCharacter`
last-resort fallback chain. Same pattern.

**Bundled symbol font: NotoSansSymbols.** ~227 KiB, registered next
to the Liberation faces in `fontsys_init`. Covers the Misc Symbols
block (U+2600 incl. snowman U+2603), arrows, geometric shapes,
dingbats. The kernel area was bumped from 4 MiB to 8 MiB to fit it
— `boot/boot.asm` (FAT16 partition LBA 8192 → 16384, sectors_left
8187 → 16379), `link.ld` (`_loaded_end` cap 0x4FF600 → 0x8FF600),
`Makefile` (`FAT_START_LBA` 8192 → 16384). Image generation re-creates
the FAT partition at the new offset on next build.

**Synthesized snowman fallback.** `paint_rt_line_box` (`bin/browser/paint.cc`)
detects U+2603 BEFORE the per-face draw, and if no registered face
covers it, draws three filled circles (head/body/base) via
`gfx2d_circle_fill`. Defensive: handles the case where
NotoSansSymbols ever fails to register. JPEGs/PNGs of the snowman
itself are unaffected.

### Generic family detection from comma list

`cs_apply_property` for `CP_FONT_FAMILY` now walks the full
comma-separated family list looking for the FIRST generic keyword
(`serif`, `sans-serif`, `monospace`, `cursive`, `fantasy`,
`system-ui`). Previous code used `css_value_keyword` which only
matched the WHOLE value, so `font-family: 'X', sans-serif` left
`cs_font_generic = DEFAULT` (which mapped to LiberationSerif). On
f1_unicode_range.html every paragraph rendered in serif because the
generic at the END of the list went unrecognised.

### Per-codepoint face cache in paint

`paint_rt_line_box`'s sub-run grouping calls `cs_face_id_for_cp` once
per codepoint plus once per neighbour. The function does a non-trivial
walk (font_face_match_cp + fontsys_match), so an ASCII-only run was
roughly quadratic. Added a `last_cp_face_cp` / `last_cp_face` cache
keyed on the codepoint — collapses to O(1) per cp for runs of the
same character class. Brought paint fps back up after the per-cp
fallback work.

### Net stack: HTTP/1.1 + chunked + sub-resource isolation

**HTTP/1.1 + `Accept: */*`.** `build_request` (`bin/browser/net.cc`)
now sends HTTP/1.1, `Accept: */*`, `Accept-Encoding: identity`, and
`Connection: close`. The old HTTP/1.0 + `Accept: text/html,*/*` was
rejected by Wikimedia / Cloudflare CDNs for image paths.

**Chunked Transfer-Encoding decoder (RFC 7230 §4.1).** HTTP/1.1
servers default to chunked when `Content-Length` isn't computed up
front. State machine (size hex line → CRLF → bytes → CRLF, repeat,
terminator `0\r\n\r\n`) drains every available byte each `recv` so
chunks split across `recv` boundaries decode correctly. Without it
body bytes were the literal hex sizes interleaved with image data,
making `image_decode_blob` reject everything as malformed.

**Sub-resource fetches preserve `status_msg`.** `fetch_url` writes
`HTTP error: 404` to the page-level status bar on any non-2xx. Sub-
resource pumps (`font_face.cc:ff_try_one_url`,
`image.cc:image_advance_one_pending`) now save+restore `status_msg`
around the inner `fetch_url`, so a 404 image or a fake @font-face URL
doesn't replace the user-visible status with a sub-resource error.

### JPEG decoder correctness

**Hardcoded IDCT cosine basis.** `kernel/gfx/jpeg.c:jp_cos_tbl[8][8]`
is now a `const float[64]` initialiser instead of a runtime
`cosf()` table built lazily on first decode. The old code's `cosf`
calls went through the kernel's x87 `fcos`-based libm, which can
produce imprecise results from a cold-boot context where SSE/FPU
state isn't fully stabilised. Eliminating the runtime call removes
the dependency entirely. Values match Python `math.cos` to single
precision and Blink's libjpeg-turbo equivalent constants.

**JPEG output now emits `alpha=255`.** `jp_yuv_xrgb` and the
grayscale path OR in `0xFF000000` so JPEG pixels are fully opaque.
Previously alpha was 0; combined with the new alpha-aware blit (next
item), every JPEG would have rendered transparent.

**Alpha-aware `gfx2d_image_draw_scaled`.** Switched from `gfx2d_pixel`
(raw write) to `gfx2d_pixel_alpha` (blends per source alpha:
`a==0` skip, `a>=255` opaque write, else blend). Reference:
`blink/Source/platform/graphics/GraphicsContext::drawImage` honouring
source alpha when compositing. The visible bug was the bottom of the
Wikipedia logo — transparent regions of the source PNG had `RGB=0,
A=0` and were being blitted as solid black. Now they composite
correctly over the page background.

### Tests

11 new files under `tests/browser/` exercising each new feature:

- `e4_custom_props.html` — `:root { --brand: tomato }` + `var(--brand)`
- `e5_calc.html` — `calc(100% - 32px)`, `calc(2 * 100px)`, `calc(400px / 2)`
- `e6_var_in_calc.html` — `calc(var(--gutter) * 2)`
- `e7_var_inheritance.html` — DOM ancestor walk for `--c` overrides
- `e8_var_fallback.html` — `var(--undef, fallback)` + nested fallbacks
- `e9_position.html` — fixed bar, relative shift, 4 absolute corners in a frame
- `f1_unicode_range.html` — unicode-range parse forms incl. wildcard + snowman
- `f2_woff1.html` — 3-deep src fallback chain with intentional 404s
- `f3_img_decode.html` — Wikipedia logo + PNG transparency demo + 2 cat JPEGs
- `f4_img_intrinsic.html` — width/height attr resolution + aspect ratio
- `f5_fontsys_metrics.html` — mixed-size lines from fontsys_line_height
- `f6_per_cp_face.html` — Latin + Cyrillic + catch-all routing
- `f7_inline_var.html` — `style="--c: blue"` inline custom properties

### Cupidc upgrades (round 2)

- **Anonymous-struct typedef.** `typedef struct { ... } Name;` now
  parses. `cc_parse_type`'s STRUCT branch generates a synthetic
  `__anon_struct_N` tag when the next token is `{` and parses the
  body inline; emit forward decls for `cc_align_up` /
  `cc_type_size` / `cc_type_align` so the inline body parser can
  call them.

- **Forward function declarations.** `T name(params);` is now
  accepted as a forward decl: `cc_parse_function` peeks for `;`
  after the closing `)` and registers the symbol with
  `is_defined = 0`. Use sites compile through the existing patch
  table. Required for mutually-recursive functions like
  `ec_eval_expr` calling itself indirectly via `ec_eval_term`.

- **Bound kernel symbols:** `fontsys_face_has_cp`,
  `fontsys_find_face_with_cp`, `fontsys_unregister`, `kdeflate_raw`
  in `kernel/lang/cupidc.c`'s BIND table.

- **`Defined struct` printf newline.** Cosmetic — the format string
  had `\\n` instead of `\n`, so the message printed literal `\n`
  text in the serial log.

### Kernel area expanded 4 → 8 MiB

`boot/boot.asm`: partition entry LBA 8192 → 16384, sectors_left 8187
→ 16379. `link.ld`: ASSERT cap 0x4FF600 → 0x8FF600. `Makefile`:
FAT_START_LBA 8192 → 16384. The image creation rule detects the LBA
mismatch on first run and recreates `cupidos.img` automatically.

## Files changed in phase 2

**Browser:** `bin/browser/css.cc`, `bin/browser/dom.cc`,
`bin/browser/font_face.cc`, `bin/browser/image.cc`,
`bin/browser/layout.cc`, `bin/browser/main.cc`,
`bin/browser/net.cc`, `bin/browser/paint.cc`, `bin/browser/style.cc`.

**Kernel:** `kernel/gfx/fontsys.c`, `kernel/gfx/fontsys.h`,
`kernel/gfx/gfx2d_assets.c`, `kernel/gfx/jpeg.c`,
`kernel/lang/cupidc.c`, `kernel/lang/cupidc_parse.c`.

**Layout / build:** `boot/boot.asm`, `link.ld`, `Makefile`,
`system/fonts/NotoSansSymbols-Regular.ttf` (new asset).

**Tests:** 13 new HTML files under `tests/browser/`.

### Cascade order fix: inline `--vars` resolved before regular rules

`<p style="--c: blue">` set inline ended up resolving `color: var(--c, red)` to red because the cascade ordered `apply_inline_style` (step 3) AFTER the regular author-rule pass (step 2). When `cs_apply_property` ran for the regular `color: var(--c, red)` rule, the inline `--c` hadn't been recorded yet, so `cs_var_substitute` fell through to the fallback.

Restructured `style_resolve_all` cascade to match Blink's two-phase resolution:

- **2a.** Author-rule `CP_CUSTOM_VAR` cascade into `cs_var_*[]` (was previously after the regular pass).
- **2b. NEW.** `apply_inline_vars` extracts ONLY `--name:` declarations from `style="..."` into `cs_var_*[]`. Sits before the regular property pass so inline-declared vars are visible during `var()` substitution. Appended order means inline beats matching author-rule customs (cs_var_lookup returns first match).
- **2c.** Regular author-rule cascade — `cs_apply_property` calls `cs_var_substitute` against fully-populated `cs_var_*[]`.
- **3.** Regular `apply_inline_style` for non-custom inline declarations (still wins over author rules without `!important`).
- **4.** `!important` author rules.

Reference: `blink/Source/core/css/resolver/StyleCascade.cpp::resolve` does the equivalent — custom properties have their own cascade phase that completes before any regular property is resolved with `var()`.

`tests/browser/f7_inline_var.html` is the regression case: `<p style="--c: blue">` now renders blue.

### Test page asset cleanup

`tests/browser/f4_img_intrinsic.html` swapped its four `/local/test.png` references for the stable `https://upload.wikimedia.org/wikipedia/en/b/bc/Wiki.png` URL so the test exercises actual decoded intrinsic dimensions (135×155) against width/height attribute combinations instead of producing four 404 placeholders.

## Phase 3: float / clear layout

CSS 2.1 §9.5 (floats) + §9.5.2 (clear) + §9.4.1 (BFC scoping). The
storage and CSS parse landed in phase 1 with the rest of the
positioning work; phase 3 wires up the IFC + BFC integration so
float boxes actually exclude line content and `clear` advances past
the relevant float bottoms.

**Float placement (`bin/browser/layout.cc`).** Inside `layout_block`,
when a child has `cs_float == LEFT` or `cs_float == RIGHT`:

1. Flush any pending inline atoms before placing the float so
   subsequent lines see the new exclusion.
2. Resolve the float's content width: `cs_width` if set; otherwise
   shrink-to-fit via `oof_intrinsic_text_w` clamped to the
   container's content width minus padding/border.
3. Save and reset `float_visible_first` / `float_count`. This
   establishes a new BFC for the float subtree (CSS 2.1 §9.4.1):
   inner floats are invisible to outer line-box exclusion, and
   outer floats stay invisible to the float's own descendants. Pre-
   set `rt_x[c]` / `rt_y[c]` so descendants' `rt_doc_x_of` /
   `rt_doc_y_of` walks return correct ancestry during the float's
   layout pass.
4. Recurse `layout_block(c, f_w)`. Restore the saved
   `float_visible_first` / `float_count` afterwards.
5. Compute the final document-space `(x, y)` against existing
   floats: `line_left_edge_at` / `line_right_edge_at` walk the
   visible floats and clip the available band, the float anchors at
   the resulting edge minus its own margin.
6. Store the float's MARGIN box (not border box) in
   `floats[float_count]`: `float_y = parent_doc_y`,
   `float_h = margin_t + rt_h + margin_b`, `float_x` and
   `float_w` similarly include left/right margins. CSS 2.1 line-box
   exclusion is against the outer margin edge. Reference:
   `blink/Source/core/rendering/FloatingObject::frameRect` (border
   box) plus `marginAfter`/`marginBefore` applied at line-exclusion
   time in `RenderBlockFlow::logicalRightOffsetForFloat`.

**Line-box exclusion (`flush_inline`).** Each line queries
`line_left_edge_at(line_doc_y, line_h, parent_doc_x)` and
`line_right_edge_at(...)` against the visible-float window
(`float_visible_first .. float_count`). The line's `cx` and
`max_w` shrink to the post-exclusion band, so lines that overlap a
float's vertical extent wrap inside the remaining horizontal slot.
Reference: `blink/Source/core/rendering/RenderBlockFlow.cpp`
`logicalLeftOffsetForLine` / `logicalRightOffsetForLine`.

**Clear (`cs_clear`).** Before placing a non-float in-flow child,
if `cs_clear != NONE`, compute `floats_lowest_bottom(mask)` over the
visible-float window (left-only, right-only, or both per the
`clear` value), and advance `cy` past the lowest matching margin-box
bottom. CSS 2.1 §9.5.2 says the cleared box's top border edge sits
at-or-below the relevant float's bottom margin edge — including the
float's own `margin-bottom` because `float_h` already accumulates
it. Reference: `RenderBlockFlow::clearFloats`.

**Critical fix: pre-set `rt_y[c]` for in-flow blocks.** Before
recursing `layout_block(c, ...)` for a non-float in-flow block
child, `rt_y[c]` is now set to its final `cy` BEFORE the recursion
(after margin collapse resolution). Otherwise descendants' calls to
`rt_doc_y_of(ancestor)` see a stale `rt_y` and float exclusion math
in document-space sees ancestors at the wrong y, which made lines
run through floats. Same fix for `rt_x[c]` with the static
margin-left.

**Critical fix: skip out-of-flow children in the in-flow advance.**
The block-child loop now early-continues when
`cs_position[c] == ABSOLUTE || FIXED`, so absolute and fixed boxes
no longer push subsequent siblings down. They're laid out by the
post-pass `layout_oof`. Reference:
`RenderBlockFlow::layoutBlockChildren` skips
`isOutOfFlowPositioned()` children.

**Two-pass paint for stacking order (`paint_rt_node`).** CSS 2.1
§E.2: in-flow non-positioned block backgrounds paint at level 3,
floats at level 4. Without paint-time ordering a wide in-flow block
that comes after a float in document order (e.g. `<div class="cl">`
after `<div class="r">`) covers the float's painted box. The block
paint loop now does two passes over block-level children: pass 1
paints in-flow non-floats, pass 2 paints floats on top. Float
deferral is gated on `rt_dom[c] >= 0` so anonymous `RT_LINE_BOX`
children of a float parent (which inherit the parent's style index,
including `cs_float`) still paint in pass 1 alongside the parent's
own content. Reference: `blink/Source/core/paint/PaintLayerPainter`
ordering of `paintBackgroundForFragments` / `paintFloats` /
`paintForeground`.

**Float branch flushes pending margin.** A floated child placed
right after an in-flow block child (e.g. `.cr` followed by
`<div class="l">L2</div>`) must apply the previous block's pending
bottom margin before computing its static `parent_doc_y`. CSS 2.1
§8.3.1: float margins never collapse with adjacent boxes, so the
preceding block's `margin-bottom` flushes in full and the float
sits below it instead of touching its bottom edge. The flush now
happens in the float branch itself (mirroring the `pile_count > 0`
flush already present for inline atoms).

**Block-level replaced elements.** `.thumb img { display: block }`
on a real `<img>` produced wrong placement before this PR: the
render-tree forces `kind = RT_REPLACED` for replaced elements
regardless of display, and the in-flow block child loop only
matched `rt_kind_is_block_level()` (which excludes RT_REPLACED), so
the image fell through to `collect_inline_atoms` and got absorbed
into a line box even when the author asked for block layout. The
loop now also matches `RT_REPLACED && cs_display == DISP_BLOCK` and
sizes the box from explicit `width`/`height` CSS or
`rt_intrinsic_w/h` (HTML attrs captured by `render_tree.cc`)
without recursing. Paint side: the inline-atom skip in
`paint_rt_node`'s child loop now lets a block-level RT_REPLACED
through so it paints via the normal block path. Reference:
`blink/Source/core/rendering/RenderImage.cpp` distinguishing
`isBlockLevelReplaced()` from inline replaced layout. Visible bug
fix: `tests/browser/g5_float_image.html` thumbnail now puts the
Wikipedia logo at the top of the box with the caption below,
matching Chrome.

**Tests.** Five new `tests/browser/g{1..5}_*.html` pages exercise
basic floats, infobox-style figures, two-column layouts, clear
semantics, and a floated thumbnail with caption.

### CupidC compiler: adjacent string literal concatenation

ISO C §6.4.5p5 requires adjacent string literal tokens to splice into
a single literal: `"foo" "bar"` is one literal `"foobar"`. CupidC
treated each literal as a separate token, so multi-line `printf`
format strings written in the standard wrapped style failed at
parse. `cc_parse_primary`'s `CC_TOK_STRING` case now loops
`cc_peek` while the next token is also `CC_TOK_STRING`,
concatenates into a single buffer (capped at `CC_MAX_STRING - 1`),
and writes one record into the data section. Same byte-for-byte
output as the single-literal path; only the parse step is new.

## Phase 4: modern CSS visual polish

A cluster of features that show up on every modern page. References
stay in `blink/Source/core/`.

**`box-sizing: border-box` (CSS box-sizing module).** New
`cs_box_sizing[4096]` storage and `BOX_SIZING_BORDER` enum. Width
and height resolution in `layout_block` now subtract padding +
border from `cs_width`/`cs_height` when border-box is set, so the
declared value describes the painted box edge (matching every
modern reset CSS). Same fix in the abspos height path. Reference:
`blink/Source/core/rendering/RenderBox.cpp`
`computeLogicalWidthInRegion` + `boxSizing()`.

**`linear-gradient` backgrounds.** `css_value_linear_gradient`
parses `linear-gradient([dir,] c1, c2)` with cardinal directions
(`to top` / `to right` / `to bottom` / `to left`) and angle keywords
(`0deg` / `90deg` / `180deg` / `270deg`). Stops past two are dropped;
non-cardinal angles snap to the nearest cardinal. Storage:
`cs_bg_grad / cs_bg_grad_c1 / cs_bg_grad_c2`. Paint replaces the
solid `cs_bg` with `gfx2d_gradient_h` / `gfx2d_gradient_v` for sharp
corners and the new `gfx2d_gradient_h_round` / `gfx2d_gradient_v_round`
when `border-radius > 0`. The rounded variants reuse
`gfx2d_rect_round_fill`'s per-row `g2d_isqrt(r*r - dy*dy)` corner
mask and stride the gradient color along x or y. Bound to CupidC
via the BIND table. Reference:
`blink/Source/core/css/CSSGradientValue.cpp`
(`CSSGradientValue::parseLengthsAndPercents`) +
`BoxPainterBase::paintFillLayer` rounded clip.

**Z-index stacking sort.** `rt_collect_oof` (in `render_tree.cc`)
now appends every stacking-context participant to `rt_oof_list`:
absolute / fixed positioned boxes AND `position: relative` boxes
with an explicit `z-index` (CSS 2.1 §9.9.1). New `rt_is_stack[6144]`
flag marks all collected nodes; the in-flow paint walk skips them so
they only paint in `render()`'s post-walk z-sorted pass. Insertion
sort runs in-place on `rt_oof_list` (rebuilt every render-tree pass,
so the mutation never leaks across frames). `layout_oof_one` early-
returns for non-abs/fixed entries so relative-with-z stays in flow.
Reference: `blink/Source/core/paint/PaintLayerStackingNode.cpp`
`PaintLayerStackingNode::dirtyZOrderLists`.

**Negative absolute offsets (`top: -8px`).** The previous sentinel
`-1` for "auto" collided with literal negative offsets. Storage now
splits presence into `cs_pos_set[4096]` bits 0..3 (top / right /
bottom / left) and bit 16 (z-index explicit). Layout's
`layout_oof_one` and the relative-position walks in `rt_screen_x` /
`rt_screen_y` check the bit, not `>= 0`. CSS author's
`top: -8 right: -8` on a `.badge` now hangs 8 px past the
positioned ancestor's padding edge. Reference:
`blink/Source/core/rendering/RenderBox.cpp::computePositionedLogicalWidth`
allows any sign on `left`/`right`.

**Containing block = padding edge of positioned ancestor.**
`layout_oof_one` previously computed cb against the content box
(subtracting both padding AND border). CSS 2.1 §10.1 says the
containing block for absolutely positioned descendants is the
padding edge of the nearest positioned ancestor — between its
borders, INCLUDING its padding. Now `cb_x = doc_x + border_l`,
`cb_w = rt_w - border_l - border_r`. Reference:
`blink/Source/core/rendering/RenderBox.cpp::containingBlock`.

**`text-align: center | right`.** Parsed and cascaded already, but
never consumed. `paint_rt_line_box` now computes the rightmost
atom's edge in line-box-local coords, derives slack against
`rt_w[line_box]`, and shifts every atom (replaced refs included)
by `slack/2` for center or full `slack` for right. Reference:
`blink/Source/core/rendering/RenderBlockLineLayout.cpp`
`computeInlineDirectionPositionsForLine` +
`setInlineBoxesAlignment`.

**Per-side border width painted, not 1-px clamped.** Layout always
read the declared px width via `rt_border_l/r/t/b`, but
`paint_rt_box_decoration`'s side strips called `gfx2d_rect_fill(..., 1, ...)`.
Now each side strokes its own `cs_border[cs][i]` height/width,
so `border: 4px solid` paints the full 4px ring. Same fix on the
dashed/dotted path.

### Kernel additions

- `gfx2d_gradient_h_round(x, y, w, h, r, c1, c2)` and
  `gfx2d_gradient_v_round` in `kernel/gfx/gfx2d.c`. Per-row gradient
  color combined with `gfx2d_rect_round_fill`'s `g2d_isqrt` corner
  mask. Bound to CupidC.

### CupidC

`CC_PP_MAX_OUTPUT` 512 KiB → 1 MiB. The browser tree concatenated
with `#include` reach ~525 KiB after this PR, so the preprocessor's
output buffer was overflowing on JIT compile. Symptom was the boot
loop `[cupidc] preprocess failed` immediately after launching the
browser.

### Tests (h-series)

- `tests/browser/h1_box_sizing.html` — content-box vs border-box
  side by side; declared width 200 + padding 12 + border 4 should
  paint 232 px outer in content-box and 200 px outer in border-box.
- `tests/browser/h2_z_index.html` — four overlapping abspos boxes
  with z-indices 0..3 in a different order from doc order.
- `tests/browser/h3_linear_gradient.html` — eight tiles covering
  every supported direction (default / `to <side>` / `<n>deg`) with
  `border-radius: 6` to exercise the rounded-gradient path.
- `tests/browser/h4_card_combo.html` — gradient + border-radius +
  box-sizing + box-shadow + abspos children with negative offsets +
  `text-align: center` inside a `.badge` + `position: relative;
  z-index` mid-stack to validate the full stacking pipeline.

## Phase 5: display: flex (single-line)

CSS Flexible Box Layout Module Level 1, single-line subset (no
`flex-wrap`, no `order`, no `align-self`). Modern web layouts depend
on flex; without it Bootstrap / Tailwind / utility-CSS pages render
as a single column. Reference:
`blink/Source/core/layout/LayoutFlexibleBox.cpp`
(computeNextFlexLine + resolveFlexibleLengths +
layoutAndPlaceChildren).

**Storage (`bin/browser/main.cc`).**

```c
int cs_flex_dir   [4096];   /* FLEX_DIR_ROW (default) | COLUMN */
int cs_justify    [4096];   /* JUSTIFY_FLEX_START | END | CENTER | SPACE_BETWEEN | SPACE_AROUND | SPACE_EVENLY */
int cs_align_items[4096];   /* ALIGN_STRETCH (default) | FLEX_START | END | CENTER | BASELINE */
int cs_flex_grow  [4096];   /* hundredths so 1.5 = 150 (avoids float in cupidc) */
int cs_flex_shrink[4096];   /* hundredths; default 100 */
int cs_flex_basis [4096];   /* px; -1 = auto */
int cs_gap        [4096];
```

UA defaults: row, flex-start, stretch, grow=0, shrink=1, basis=auto,
gap=0.

**CSS parse (`bin/browser/css.cc` + `bin/browser/style.cc`).**

- `display: flex | inline-flex` adds two new `DISP_*` enum values.
- `flex-direction`, `justify-content`, `align-items` parse keyword
  values with the obvious defaults on unknown.
- `flex-grow`, `flex-shrink`, `flex-basis` parse individually.
- `flex` shorthand handles the canonical forms:
  `auto | none | <number> | <a> <b> | <a> <length> | <a> <b> <length>`.
  Numbers go to grow then shrink; the first token with a unit becomes
  basis. Keywords map per spec (`auto` -> 1 1 auto, `none` -> 0 0 auto).
- `gap` parses a single length (row + column gap shorthand, no
  per-axis split yet).
- `rt_kind_for_display` maps `DISP_FLEX` to `RT_BLOCK` and
  `DISP_INLINE_FLEX` to `RT_INLINE_BLOCK` so the parent's child walk
  treats a flex container as a block-level child.

**Layout (`bin/browser/layout.cc:layout_flex`).**

`layout_block` dispatches to `layout_flex(n, content_w, cx, cy)` when
the container's `cs_display` is `DISP_FLEX` or `DISP_INLINE_FLEX`.
The algorithm:

1. **Walk + base sizes.** Skip out-of-flow children (POS_ABSOLUTE /
   POS_FIXED) and anonymous nodes. For each flex item compute the
   FLEX BASE SIZE in the main axis from `flex-basis` first, else
   `cs_width` (row) or `cs_height` (column), else an intrinsic-text
   estimate. Box-sizing: border-box uses the declared value as-is;
   content-box adds padding+border on the main axis. Floor at the
   item's padding+border so a `flex: 1 0 0` item never shrinks below
   its own chrome.
2. **Free-space distribution.**
   `free_space = main_size - sum(base) - gap_total`.
   - `> 0`: distribute proportionally to `cs_flex_grow`. Rounding
     remainder lands on the last grower so totals match exactly.
   - `< 0`: distribute proportionally to
     `cs_flex_shrink * base / 100`, clamped to each item's
     padding+border floor.
   - `== 0`: items keep their base sizes.
3. **Indefinite main size.** A column flex container with no
   declared `height` sizes its main axis to `sum(base) + gaps`; if
   we left `main_size = 0`, free_space went deeply negative and the
   shrink pass crushed every item to padding+border only. CSS
   Flexbox §9.7.3 "if the available main size is infinite, this is
   the flex line's main size." (Reproduced by
   `tests/browser/i1_flex_basic.html` `.col` block before this fix.)
4. **Per-item layout.** Recurse `layout_block(item, item_w_or_h)` to
   compute the cross size from content. For column direction we then
   override `rt_h[item]` to the flex-resolved main size so a fixed
   `height: 60` survives the cross-axis re-layout.
5. **Container cross size.** Use `cs_height`/`cs_width` (per
   direction) if set, else the tallest item's cross size.
6. **align-items.** `flex-start` / `flex-end` / `center` shift the
   cross axis by `cross - item_cross` (or its half). `stretch`
   (default) extends the item's cross size to the container's when
   the item has no explicit cross size.
7. **justify-content packing.** `flex-start` is the no-shift default;
   `flex-end` shifts by full slack; `center` by half; `space-between`
   spreads slack into `n-1` gaps; `space-around` gives each item a
   half-margin on both sides; `space-evenly` distributes slack
   equally including before/after the run. `gap` is added between
   items as a base separator before extra `space-*` distribution.

**Tests.**

- `tests/browser/i1_flex_basic.html` - row + column with `gap`,
  exercises the indefinite-main-size fix and content-box height +
  padding.
- `tests/browser/i2_flex_justify.html` - all six justify-content
  values on the same `.row`.
- `tests/browser/i3_flex_align.html` - flex-start / flex-end / center
  / stretch with three items of different declared heights.
- `tests/browser/i4_flex_grow.html` - `flex: 1` even split, `flex: 1
  / 2 / 3` proportional split, and a nav-bar pattern with a
  `flex: 1` spacer between fixed-size buttons.

## Phase 6: CSS background images

`background-image: url()` + `background-size` + `background-position`
+ `background-repeat`. Used on every modern landing page (hero
sections, decorative panels, repeating patterns). References:
`blink/Source/core/css/parser/CSSParserBackground.cpp` +
`blink/Source/core/paint/BackgroundImageGeometry.cpp`.

**Storage (`bin/browser/main.cc`).**

```c
int cs_bg_img_off    [4096];   /* URL index into css_value_pool, -1 = none */
int cs_bg_img_len    [4096];
int cs_bg_handle     [4096];   /* gfx2d image handle, -1 until loaded */
int cs_bg_intrinsic_w[4096];
int cs_bg_intrinsic_h[4096];
int cs_bg_size_w     [4096];   /* px or BG_SIZE_AUTO/COVER/CONTAIN */
int cs_bg_size_h     [4096];
int cs_bg_pos_x      [4096];   /* px, or sentinel: -10000=center,
                                  -20000=right/bottom */
int cs_bg_pos_y      [4096];
int cs_bg_repeat     [4096];   /* BG_REPEAT_BOTH | NONE | X | Y */
```

UA defaults: url=-1, handle=-1, size=auto/auto, position=0/0,
repeat=both.

**CSS parse (`bin/browser/css.cc` + `bin/browser/style.cc`).**

- New property names: `background-image`, `background-size`,
  `background-position`, `background-repeat`.
- `background-image: url(<href>)` strips `url(` ... `)` and any
  surrounding single/double quotes; the inner string remains in
  `css_value_pool` and is referenced by `cs_bg_img_off/_len`.
  `none` clears.
- `background-size` accepts `cover` / `contain` / `<length> <length>`
  / `<length>` (single value, missing axis is `auto`) / `auto`. The
  COVER and CONTAIN sentinels survive into paint where the actual
  scaling math runs.
- `background-position` accepts pairwise keywords (`left top`,
  `center bottom`, etc.) and lengths. Keywords encode at apply time
  as sentinel positions resolved at paint: `-10000` = center,
  `-20000` = right / bottom.
- `background-repeat` covers all four canonical values.
- The `background:` shorthand still falls through to the existing
  color path (gradient or solid); composing it with `image / size /
  position / repeat` in one declaration is a follow-up.

**Image fetch (`bin/browser/image.cc`).**

A parallel 16-slot queue keyed by style index (cs) instead of DOM
node, alongside the existing 32-slot `<img>` queue:

- `bg_image_queue_collect` walks `cs_count` slots, queues any
  unique URL whose handle isn't already loaded, and re-links
  `cs_bg_handle` from the URL cache when a previously fetched
  image's handle would otherwise have been wiped.
- `bg_image_advance_one_pending` drives one slot per render tick
  through `fetch_url` + `image_decode_blob`, with the same
  save / restore around `cur_host` / `cur_path` / `status_msg`
  the `<img>` pump uses.
- `image_evict_all` was extended: bg-image handles live on the
  cache entry (`bg_url_handle[16]`) so per-page eviction is one
  free per unique URL, not one per cs slot.

**Critical fix during testing: the URL cache owns the handle.**
Initial implementation stashed the handle on `cs_bg_handle` only;
every reflow's `init_style_for_cs` reset that to -1, so the next
tick re-queued the URL and re-issued the HTTPS GET. Symptom was a
boot-loop of `[browser] bg-image load: <url> -> handle 5/6/7/8/...`
once per second. The cache entry now owns `bg_url_handle[slot]` /
`bg_url_w[slot]` / `bg_url_h[slot]`, and `bg_image_advance_one_pending`
no longer sets `img_state_dirty` (layout doesn't depend on bg-image
dimensions, so the next render() picks up the new handle without a
reflow). Reflow + re-link path is now: reflow wipes
`cs_bg_handle`, the next `bg_image_queue_collect` finds the URL in
cache and re-links from the cache entry — no fetch.

**Paint (`bin/browser/paint.cc`).**

`paint_rt_box_decoration` paints the bg image AFTER the solid /
gradient bg fill:

1. **Tile size.** `cover` picks `scale = max(box_w/img_w,
   box_h/img_h)` so both axes >= box. `contain` picks `scale = min`
   so both axes <= box. Explicit lengths use the declared values;
   when one axis is `auto` and the other is fixed, the missing axis
   is derived from the intrinsic aspect ratio. Min tile size 1×1.
2. **Tile origin.** Position keywords resolve to `(box-tile)/2` for
   center, `box-tile` for right/bottom, `0` for left/top. Numeric
   lengths pass through. Final origin in element-local coords is
   `start_x = origin_x` (then stepped back to the first tile that
   intersects the box for repeating axes).
3. **Tile loop.** Repeat axes step `start_*` back to the first tile
   that intersects the box, then walk `tile_*_count = (box - start +
   tile - 1) / tile` tiles across. Capped at 64×64 tiles per box.
4. **Background-clip.** The whole tile-draw loop is wrapped in
   `paint_clip_push(sx, sy, w, h)` / `paint_clip_pop` so a `cover`
   that intentionally overflows on the shorter axis doesn't bleed
   into adjacent elements (CSS Backgrounds & Borders §3.10
   `background-clip: border-box` default).

**Tests.**

- `tests/browser/j1_bg_image_basic.html` - default size + position,
  explicit `Npx Mpx`, explicit `Npx auto` over a solid color.
- `tests/browser/j2_bg_size.html` - cover, contain, explicit, auto-
  aspect; this is the test that surfaced the missing background
  clip.
- `tests/browser/j3_bg_repeat.html` - all four repeat modes at a
  small explicit tile size.
- `tests/browser/j4_bg_position.html` - the 9-keyword grid (corners
  / edges / center) + explicit `24px 40px`.

## Phase 7: scripting bindings parity + AC97 volume tool

The browser pulled most of its weight onto the kernel-binding registry,
but the registry was lopsided: CupidC had 403 bindings, CupidASM 337,
and the AC97 driver only exposed master volume with no read-back. This
phase closes the parity gap and adds a small user-facing tool.

### AC97 driver — PCM channel + getters

`kernel/audio/ac97.{c,h}` gained `ac97_set_pcm_volume(pct)`,
`ac97_get_master_volume()`, and `ac97_get_pcm_volume()`. The setter
mirrors the existing master path (pct → 6-bit attenuation → write to
`NAM_PCM_OUT_VOL`); the getters return cached percentages from a new
`master_pct` / `pcm_pct` pair on `s_ac97`. `ac97_init` now routes its
unmute writes through the setters so the cache stays in sync with the
codec from boot.

### CupidC bindings (kernel/lang/cupidc.c)

- `sock_avail(fd)` / `sock_state(fd)` — non-blocking polling primitives
  already in `kernel/network/socket.c` v5; bound as `BIND_T(..., 1, TYPE_INT)`.
- `ac97_set_pcm_volume`, `ac97_get_master_volume`, `ac97_get_pcm_volume`.
- `png_decode_mem`, `jpeg_decode_mem` — the in-memory PNG/JPEG decoders
  that already power `gfx2d_image_load_mem`. Caller passes
  `(data, len, &out_pixels, &out_w, &out_h)` and `kfree`s the buffer.

### CupidASM full parity (kernel/lang/as.c)

Closed the 84-symbol gap between cupidc.c and as.c, plus added the 8
new bindings above. New `as_*` wrappers were written for the cases
where the cupidc side uses static helpers (`gui_win_*` 16, shell buffer
inspection 7, clipboard 3, notepad path, ANSI palette, PCI bus-master,
keyboard ctrl, network drop / error stat). Direct binds for the rest:
all `gfx2d_image_*`, thick-stroke shapes, rounded gradients, glyph
helpers, swap, SMP, USB, fontsys, REPL, image codecs, `kdeflate_raw`,
`dglibc_test_main`, and the full libm surface (54 functions —
sin/cos/tan, exp/log/pow, trig inverses, hyperbolics, rounding —
documented with the float-ABI caveat that callers `fstp` the FPU
result).

After this phase a parity diff (`comm -23` of the two binding name
sets) leaves only `IP_PROTO_ICMP` / `_TCP` / `_UDP` in the cupidc-only
set; those are int constants registered via `as_bind_equ` on the asm
side, not function bindings.

### CupidC compiler — stdint type aliases

The lexer (`kernel/lang/cupidc_lex.c::cc_check_keyword`) now accepts
`uint8_t`/`uint16_t`/`uint32_t`/`int8_t`/`int16_t`/`int32_t` as
synonyms for `U8`/`U16`/`U32`/`I8`/`I16`/`I32`. Programs ported from
kernel C no longer fail with `undefined variable` cascades on
`(uint8_t)n` casts. The parser is token-keyed, so this is a
lexer-only change — no parser table touched.

### CupidC compiler — auto-main no longer fires twice

Files with top-level statements that *also* call `main();` explicitly
(the canonical pattern in `bin/audiotest.cc`, `bin/volume.cc`, etc.)
were running `main` twice: once from the user's top-level call, once
from the post-parse "if main() exists, call it for compatibility"
emission. Two flags were added to `cc_state_t`: `in_top_level` (on
during the top-level statement loop) and `main_called_top_level` (set
when a call expression resolves to a `SYM_FUNC` named `main` while
`in_top_level` is true). The post-parse auto-call now skips when the
user has already invoked main themselves. Legacy programs that define
`main()` without calling it still get the implicit invocation.

### `bin/volume.cc`

Small CupidC tool exercising the new AC97 surface:

```
volume          # prints current master volume
volume 60       # sets master volume to 60% and confirms
volume 150      # rejects out-of-range with an error message
```

Uses only existing bindings (`get_args`, `ac97_is_present_int`,
`ac97_set_master_volume`, `print_int`, `println`) plus the new
`ac97_get_master_volume` getter. Pattern follows
`bin/audiotest.cc` — `void main()` + a trailing `main();`, which the
post-parse fix makes safe again.

### Documentation

- `CUPIDOS.txt` — appended `sock_avail` / `sock_state` to the BSD
  socket cheat-sheet, AC97 PCM + getters to the audio block, a new
  IMAGING section for the codec bindings, a new 2D GFX parity section
  listing the asm-newly-bound names, a new GUI WINDOW API section,
  and a `volume 50` quick-start line.
- `wiki/CupidC-Language-Reference.md` — same additions in the binding
  catalogue, plus a PNG-decode-and-blit example.
- `wiki/CupidASM-Assembler.md` — appended TLS/poll bindings to BSD
  sockets, `net_rx_drops` / `net_tx_errors`, AC97 PCM + getters, a new
  Imaging block, a 2D Graphics parity table, a GUI window table, and
  a libm section with the float-ABI note.
- `wiki/Networking.md` — new subsections on non-blocking polling
  (`sock_avail` + `sock_state`) and TLS upgrade via `setsockopt`.
- `wiki/CupidC-2D-Graphics-Library.md` — appended PNG / JPEG codec
  block and a CupidASM parity note.

### Files changed in phase 7

- `kernel/audio/ac97.h`, `kernel/audio/ac97.c`
- `kernel/lang/cupidc.h` — new `in_top_level` / `main_called_top_level` fields
- `kernel/lang/cupidc.c` — 9 new bindings, png/jpeg.h includes
- `kernel/lang/cupidc_lex.c` — stdint keyword aliases
- `kernel/lang/cupidc_parse.c` — auto-main suppression
- `kernel/lang/as.c` — ~92 new bindings + ~36 wrapper helpers + new
  includes (gui, swap, smp, percpu, usb, clipboard, fontsys, ansi,
  png, jpeg, fat16, deflate, dglibc, graphics, gfx2d_assets,
  gfx2d_transform, libm, cupidc)
- `bin/volume.cc` — new
- `CUPIDOS.txt` + four `wiki/*.md` pages

## Polish + correctness fixes (post-phase-6)

A small but high-leverage cluster of fixes surfaced while building a
real demo page (`tests/browser/demo_showcase.html`).

**`<style>` content > 4 KiB was being truncated.** The HTML tokenizer
emits `<style>` content as RAWTEXT (no entity decoding per spec),
but the post-tokenize DATA path still ran the bytes through
`decode_entities` into a 4 KiB `ctype_buf`, capping any stylesheet
above that size. Symptom: rules near the bottom of the stylesheet
silently dropped, classes never matched, those elements rendered as
default block-flow with no styling. Fix: pre-intern RAWTEXT bytes
in the tokenizer and tag with the 0x40000000 sentinel like RCDATA
does, so the consumer reads from `attr_pool` directly at full
length. CSS stylesheets up to `ATTR_POOL_SIZE` (128 KiB) now survive
intact.

**Flex item blockification (CSS Flexbox §4).** Inline-level children
of a flex container must be treated as block-level flex items. The
paint walk's "skip inline atoms" check was dropping `RT_INLINE_BLOCK`
and `RT_INLINE` children of flex containers because they were never
absorbed into a `LINE_BOX` (the flex container doesn't produce
line boxes). `build_rt_children` now detects a flex parent and routes
non-text children through the block path with their `rt_kind` forced
to `RT_BLOCK` (preserving `RT_REPLACED` so `<img>` keeps its replaced
paint). Fixes the missing `.btn` buttons inside `.actions` and the
inline-span stats columns.

**Column flex with auto basis.** `flex-direction: column` items with
no explicit `height` or `flex-basis` collapsed to padding+border
only. There's no cheap intrinsic-height estimator (unlike text
width), so the base size came back 0; free-space distribution then
treated the deficit as a shrink target and crushed every item to
its minimum. Fix: lay out each auto-basis column item in pass 1 to
learn its natural height, use that as the base. Plus the matching
"indefinite main size" rule (CSS Flexbox §9.7.3) — a column
container with no `height` sizes its main axis to
`sum(base) + gaps` so `free_space = 0` and items keep their
declared dimensions.

**Flex pass-2 duplicate layout.** Pass 1's new layout_block call
(above) combined with pass 2's unconditional re-layout produced two
sets of `RT_LINE_BOX` children per item. Paint walked both and
rendered the text twice at slightly different offsets — "525"
showed up as "52525". Fix: pass 2 only re-runs `layout_block` for
column items whose flex-resolved main size differs from their
pass-1 base (i.e. grow/shrink actually moved the size).

**BFC root containment (`overflow: hidden`).** Per CSS 2.1 §9.4.1 +
§10.6.7, a block with non-visible overflow establishes a new BFC
that both hides outer floats from descendants AND grows its own
`rt_h` to cover any internal float that overhangs the in-flow
content. `layout_block` now saves `float_visible_first` /
`float_count` on entry when `cs_overflow == HIDDEN`, scopes
descendants to internal floats only, and on exit walks the float
range `[saved_count .. float_count)` for the lowest margin-box
bottom, growing `rt_h` to cover. Internal floats are then dropped
from the global list since they can't escape. Reference:
`blink/Source/core/rendering/RenderBlockFlow::addOverflowFromFloats`.

**Negative margin-bottom on auto-margin items.** No code change for
this; verified the existing margin path already handles it.

**Demo showcase page.** `tests/browser/demo_showcase.html` exercises
the full visual surface in one page: gradient nav, hero with
positioned badge, five-tile font showcase (sans / serif / monospace
/ weight+style / symbols), three-card flex row with gradient
icons, floated thumbnail article, stats strip with
`justify-content: space-between` over a gradient bar, dark footer.

## Not in this PR (still deferred)

- **`background:` shorthand composition.** Composing
  `image / size / position / repeat / color` in one declaration
  isn't parsed yet; only the longhand properties + the existing
  color/gradient path on `background:` work.
- **`background-clip` / `background-origin` / `background-attachment`**
  - clip is hard-coded to border-box, origin to padding-box,
  attachment to scroll. These pieces are unused on the test pages
  we hit.
- **`flex-wrap`, `order`, `align-self`, `align-content`** - the
  multi-line / per-item override pieces of the spec. Not in real-
  world use yet on the test pages we run.
- **WOFF2 + Brotli.** `woff2_unwrap` is a stub that returns NULL. Full implementation needs a kernel-side Brotli decoder (~1500 LoC port from `google/brotli c/dec/`), the 122 KiB static dictionary, base-128 directory entries, and the WOFF2 §5.1 `glyf`/`loca` transform inverse (~600 LoC). Tracked for the next PR.
- **Wikipedia thumbnail URL with `.svg.png` extension.** Returns 404 from Wikimedia even in real browsers — not a decoder issue. Tests use stable `Wiki.png` + `PNG_transparency_demonstration_1.png` instead.
