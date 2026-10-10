# ADR 0233: Complete the private gfxgui binding frontier

## Status

Accepted on 2026-08-04.

## Context

A fresh private-CupidC census compiled 103 of the 104 runnable top-level
programs. The remaining source, `bin/gfxgui_test.cc`, reported 49 unresolved
references across 46 distinct native names. The kernel already linked
implementations for 43 of those names. The other three names represented
constant built-in themes that did not yet have callable accessors.

Removing calls from the program would have hidden a real embedded-toolchain
gap. The active source defines the native surface that the private compiler
needs to expose.

The first full runtime attempt uncovered a separate defect. Frame zero
stopped inside transformed image drawing. `mat_invert()` multiplied two 16.16
matrix values into a 32.32 determinant, cast that 64-bit product directly to
`int`, and reduced the identity determinant to zero. Its unsigned division
then received a zero divisor. The test also passed a raw integer as the
denominator of a fixed-point ratio, which did not express the intended 1.5
scale.

The first full fixed-frontier attempt exposed a harness dependency after the
graphics workload itself passed. GodSong waited for the global
`[gfx2d] flip frame=2` diagnostic before accepting its follow-up keys.
`gfxgui_test.cc` had already consumed the three startup-only flip diagnostics,
so GodSong could never produce another copy.

## Decision

Register the complete 46-name cohort in the private CupidC kernel table.
Forty-three entries bind the existing effects, bitmap-font, transform, and GUI
implementations directly. Three small accessors return the addresses of
`UI_THEME_WINDOWS95`, `UI_THEME_DARK_MODE`, and
`UI_THEME_PASTEL_DREAM`. They do not copy or modify those constants.

Publish the declared result type for every new entry. The complete table has
556 registrations: 325 value results and 231 `void` results. The value group
contains 208 promoted integers, 40 unsigned words, 25 `float` values, 25
`double` values, 19 character pointers, and eight other pointers.

Make the fixed runtime frontier compile `gfxgui_test.cc` to `/gfxgui_test`
before it runs the same source through private JIT. The gate requires a
nonempty AOT image, the serial initialization and frame-progress markers,
frame 240, program cleanup, and CupidC's JIT completion marker. An unresolved
native symbol fails the command immediately. The AOT command has a 180-second
budget, and the measured JIT workload has a 300-second budget.

Have GodSong publish `[godsong] settings ready` immediately before its first
popup. The frontier waits for that program-local line, keeps the existing
two-second settling interval, and then sends the eight Escape keys used to
close the settings flow. Output from an earlier graphics command cannot
satisfy this interaction boundary.

Keep the test's generated theme, BMP, and one-glyph font files in root RamFS.
The workload requires theme save and load, BMP encode and load, font creation,
load, default selection, exact glyph rendering, and release. It seeds an
offscreen surface with one isolated white pixel and requires the exact result
after the box filter while a distinct screen sentinel remains unchanged. A
`[gfxgui_test] FAIL` line stops the gate immediately,
including when the program starts outside GUI mode. Serial success markers
cover assets, fullscreen entry, the font and surface pixels, center and
off-center transformed-image pixels, every 24th frame, and cleanup. An
off-origin point under a 90-degree rotation and nonuniform scale checks the
linear matrix, and popping the transform must restore identity.

Retain the affine determinant at its full signed 64-bit 32.32 precision.
Derive each inverse coefficient from an unsigned coefficient magnitude scaled
by `2^32`, the unsigned determinant magnitude, the existing unsigned 64-bit
division helper, and explicit sign handling. This accepts positive and
negative sub-word determinants and large determinants when their inverse
coefficients fit, including a uniform 256x scale. Keep both inverse
translation terms and their sum in signed 64-bit storage until the result is
range-checked. Reject zero determinants, coefficients, and translations that
cannot fit a signed matrix word. Express the demo's 3/2 scale and direct
matrix translation with fixed-point operands.

## Evidence

The binding contract parses all 556 declarations and registrations. It pins
the 46 new names, parameter counts, and result types, and rejects a theme
accessor published as an integer. The binding module passes all six tests.
The focused GUI and transform run passes 121 tests in 0.978 seconds. Its
negative cases cover missing runtime markers, unresolved symbols, an explicit
graphics-test failure, stale AOT output, invalid fixed-point operands, and
skipped pixel or matrix probes. They also prove that the old graphics marker
cannot release GodSong and that a missing GodSong readiness line times out. A
hosted executable includes the production transform source and calls its
actual inverse routine. It covers identity, a uniform 256x scale, reflection,
positive and negative sub-word determinants, the minimum
signed coefficient and translation, a translation with both matrix products,
a singular matrix, coefficient overflow, and translation overflow in both
directions. The complete private-CupidC discovery passes all 143 tests in
22.438 seconds. Ruff accepts every changed Python file.

Checked-seed CupidC emits a 309,384-byte `kernel/lang/cupidc.o` with SHA-256
`5acdf8d318130295b19393afc0b58d4ea2bdb2f33fd535502fa757e89d648ced`.
It emits a 22,668-byte `kernel/gfx/gfx2d_transform.o` with SHA-256
`3ff2634eddd45d584d693f0b2cc8b48af424c7de3ce7df1598f7d9ff57d7f493`.
The four-job OS build passes in 649.851 seconds and produces an 8,861,888-byte
flat kernel with SHA-256
`576e7535a18b241d7b776286cf6e15cdae063597feea00b47eae602d74280f43`.
The 209,715,200-byte image has SHA-256
`6c13b2df62c8ce073b72989b799e7bd5a332c89f2ba02ad49166bd1300d246ef`.

The private-image QEMU gate passes in 524.809 seconds. AOT reports 9,829 bytes
of code, 1,124 bytes of data, and an 11,084-byte ELF. JIT loads the generated
8x8 font and checks the exact custom-font pixel at `(16,16) = 0xFFFFFF`, the
blurred surface pixel at `(4,4) = 0x1C1C1C`, the unchanged screen sentinel at
`(4,4) = 0x123456`, the transformed center at `(460,150) = 0x808080`, and the
off-center sample at `(484,150) = 0xBC809E`. The matrix checks map `(2,3)` to
`(91,104)`, map `(0,0)` to `(468,154)` after direct translation, and recover
the exact identity after the pop. The program passes every 24-frame
checkpoint through frame 240, emits `done`, and returns cleanly from JIT.
GodSong then emits its settings-readiness line, receives seed `1` and quarter
value `200`, and returns cleanly from JIT. The harness reports 107,829 changed
framebuffer pixels, 20,462,835 captured AC97 frames, and 73,303 captured PC
speaker frames. The 149,061-byte serial log has SHA-256
`05f53cfda52e965fe4d61b6d3e0ba7e206cca29088649381458da01f45f7d84b`.
Together with the preceding census, this completes private AOT compilation
for all 104 runnable top-level programs. `bin/ctxt.cc` remains an include
fragment and is not counted as runnable.

The canonical build audit regenerates and passes its independent check. It
records 719 active inputs, 449 transforms, 255 feature requirements, and 25
unreachable source-like files. Its active source digest is
`fc21e0e56dbdf6df21ac0a00fc8582c4d9b7d1caf035c5d00b6362d8d43b5420`.
Regeneration takes 70.434 seconds, and the independent replay takes 72.713
seconds.

The broader discovery run also exposed five stale hosted test fixtures. Four
copied a type enum that predated `TYPE_UINT` and `TYPE_UINT_PTR`. One expected
the existing unsigned `is_gui_mode` result to use signed metadata. Updating
those fixtures to mirror the live enum and declaration changes no compiler
behavior.

## Rejected approaches

The first AOT matcher accepted only one `0x` prefix, while the guest's legacy
diagnostic printed two. The matcher now accepts either form and still requires
nonzero code and total sizes.

Writing the AOT image and scratch assets under `/home` caused the private test
image to publish a large seeded HomeFS. A temporary batch of HomeFS calls
reduced the number of publications but did not remove the expensive
serialization. Root RamFS is the correct scope for disposable binding-smoke
artifacts, so the experimental bindings were removed.

Longer timeouts did not repair the frame-zero stall. Progress markers narrowed
the failure to transformed drawing, where source inspection found the
determinant truncation. Fixing the arithmetic restored progress without
removing any graphics work.

The first settled full QEMU run completed the final graphics workload, then
timed out in GodSong. Its 148,270-byte log has SHA-256
`2114938c3ba8b676826b39907e06b46a820b8a828fa9f864141056059f356bf3`.
The global graphics debug counter prints only frames 0, 1, and 2, all of which
the earlier workload had consumed. Moving GodSong earlier or raising its
timeout would have hidden that ordering dependency. The program-local settings
marker replaces it without weakening either workload.

## Consequences

Every runnable top-level Cupid program passes the private AOT frontier, and
the broadest graphics binding program has a fixed AOT and JIT boot gate. The
affine inverse handles the identity, signed sub-word determinants, large
determinants, and ordinary representable matrices without a zero-divisor hang,
precision loss, or unchecked translation wrap.
Transformed text still moves only its origin; image and sprite drawing use the
inverse sampler.

The change adds no host dependency and moves no normal build owner. It does
not claim full behavioral coverage for every embedded program or close the
broader Cupid-mode hardening issue.
