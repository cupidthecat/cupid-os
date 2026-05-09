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

## Not in this PR (deferred)

- **WOFF2 + Brotli.** `woff2_unwrap` is a stub that returns NULL. Full implementation needs a kernel-side Brotli decoder (~1500 LoC port from `google/brotli c/dec/`), the 122 KiB static dictionary, base-128 directory entries, and the WOFF2 §5.1 `glyf`/`loca` transform inverse (~600 LoC). Tracked for the next PR.
- **CSS custom properties + `calc()`.** Storage was prototyped (`cs_var_*[4096][8]` = 512 KiB), removed from this PR after it pushed CupidC's data section over its (then-4 MiB) cap. Re-add when the resolver lands.
- **`float: left | right` layout.** `cs_float`/`cs_clear` parse and cascade are wired in; `place_float` / `line_x_after_floats` IFC integration is the missing piece.
- **z-index stacking sort.** Out-of-flow nodes paint in document order; explicit z-index sorting is a follow-up.
