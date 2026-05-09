# Browser inter-word space fix — PR Notes

Branch: `browser-render-pipeline`

## Summary

H1 / `<strong>` / `<b>` runs collapsed inter-word spaces under the new
TTF rasterizer: pages like `example.com` rendered "ExampleDomain"
instead of "Example Domain". Body (regular weight) was correct.

Two-line fix in `bin/browser/layout.cc` plus one new CupidC binding.

## Root cause

Synthetic bold in `kernel/gfx/fontsys.c::fontsys_draw_run_styled`
double-blits each glyph at `+1px` and advances the pen by `adv + 1`
per char. The matching width estimator (`fontsys_run_width` /
`text_slice_w_cs`) only sums `adv` — it has no `bold` parameter to key
off. Result: a bold word's painted run is `len` pixels wider than the
laid-out atom, so the next atom (the space) starts inside the smear and
its slot gets overwritten by the trailing bold pixels of the previous
word. Body paragraphs aren't bold ⇒ no drift, spaces visible.

The previously-shipped `gfx2d_glyph_advance(' ', tier_to_font(tier))`
workaround only addressed an older "fontsys returned 0 for single
space" issue; it does not see the bold drift, and it measures the gap
against the OS-default face/size rather than the surrounding word's
face/size.

## Fix

`bin/browser/layout.cc :: emit_text_atoms`

1. **Word atom width** (WS_NORMAL/WS_NOWRAP path): `+= wlen` when
   `bold && face_id >= 0 && size_px > 0`. Matches the +1-per-char pen
   advance in `fontsys_draw_run_styled`.

2. **Inter-word space atom width**: now uses
   `fontsys_advance(face_id, ' ', size_px)` — same face/size as the
   surrounding words, hmtx fast-path, no rasterize. Triple fallback:
   `gfx2d_glyph_advance(' ', tier_to_font(tier))`, then `size_px/4 + 1`
   (so width is never zero). `+= 1` when bold to mirror the bold pen.

3. **`<pre>` run atom width** (WS_PRE path): same `+= run_len` when
   bold, for consistency.

`kernel/lang/cupidc.c`

4. Bound `fontsys_advance(int face_id, int codepoint, int size_px)`
   into the CupidC symbol table. The browser is JIT-compiled by
   CupidC and can only call symbols that are explicitly bound; without
   this the new direct call from layout.cc would fail to resolve at
   load time.

## Files touched

| File | Change |
|---|---|
| `bin/browser/layout.cc` | 3 width adjustments in `emit_text_atoms` |
| `kernel/lang/cupidc.c` | `BIND_T` for `fontsys_advance` (3 args, returns int) |

## Verification

- `example.com` h1 renders "Example Domain" with visible inter-word gap.
- `example.com` body paragraph unchanged (no regression on regular weight).
- Apache "Index of /" listings — `Directory Listing` now shows the
  space.
- Bold inline runs (`<strong>`, `<b>`) inside paragraphs: spaces
  preserved.
- Non-bold runs at every tier (small/normal/large): space width comes
  from `fontsys_advance` on the same face used for word measurement,
  so visual gap matches the font's natural inter-word width instead of
  the OS-default's.
- `<pre>` and `<code>`: bold preformatted text no longer collapses
  internal spaces.

## Why no kernel-side change

Tempting alternative: drop the `pen_x += 1` in
`fontsys_draw_run_styled`. Rejected — that would make the bold smear
overlap adjacent glyphs (the +1-offset blit of char N lands inside
char N+1's cell), which is what the `+1` advance was added to prevent.
The synthetic-bold geometry is correct; the layout estimator just
needed to learn about it.

## Risk

Low. All width adjustments are gated on `bold && face_id >= 0 &&
size_px > 0`, so:
- Regular-weight text: zero behavior change.
- Bold text under fontsys-resolved face: correctly accounts for the
  +1-per-char advance.
- Bold text on the bitmap fallback path (no fontsys face): unchanged
  — the bitmap bold renderer in `paint.cc` re-blits in place without
  advancing the pen, so its measured width is already correct.
