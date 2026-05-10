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

## Not in this PR (still deferred)

- **WOFF2 + Brotli.** `woff2_unwrap` is a stub that returns NULL. Full implementation needs a kernel-side Brotli decoder (~1500 LoC port from `google/brotli c/dec/`), the 122 KiB static dictionary, base-128 directory entries, and the WOFF2 §5.1 `glyf`/`loca` transform inverse (~600 LoC). Tracked for the next PR.
- **`float: left | right` layout.** `cs_float`/`cs_clear` parse and cascade are wired in; `place_float` / `line_x_after_floats` IFC integration is the missing piece.
- **z-index stacking sort.** Out-of-flow nodes paint in document order; explicit z-index sorting is a follow-up.
- **Wikipedia thumbnail URL with `.svg.png` extension.** Returns 404 from Wikimedia even in real browsers — not a decoder issue. Tests use stable `Wiki.png` + `PNG_transparency_demonstration_1.png` instead.
