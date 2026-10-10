# ADR 0139: Transfer JPEG and glyph rasterization to CupidC

## Status

Accepted on 2026-07-27.

## Context

ADRs 0136 and 0137 added the floating-point features required by the JPEG
decoder and TrueType glyph rasterizer. ADR 0138 then promoted a checked seed
that carries both features. The two production sources still used `.c` names
and host-compiler recipes, so the normal image did not exercise those
capabilities through checked CupidC.

The transfer must preserve the source and ABI. It must also close each
compiler input set, reject host-compiler fallback, and prove both code paths
inside the operating system.

## Decision

Rename `kernel/gfx/jpeg.c` to `kernel/gfx/jpeg.cc` and
`kernel/gfx/glyph_raster.c` to `kernel/gfx/glyph_raster.cc`. Keep their object
names and final-link positions unchanged.

Build both sources through `tools/cupidc_kernel_compile.py` and the promoted
five-tool seed. The wrapper freezes these exact recursive input sets:

| Source | Frozen headers |
| --- | --- |
| `kernel/gfx/jpeg.cc` | `kernel/core/types.h`, `kernel/cpu/libm.h`, `kernel/gfx/jpeg.h`, `kernel/mm/memory.h` |
| `kernel/gfx/glyph_raster.cc` | `kernel/core/string.h`, `kernel/core/types.h`, `kernel/gfx/glyph_raster.h`, `kernel/mm/memory.h` |

The former glyph recipe listed `kernel/cpu/libm.h`, although the source did
not include it. Remove that stale edge and add the transitive
`kernel/core/types.h` edge.

Add a byte-fixed 8-by-8 baseline JPEG to the ISO test fixtures. The existing
`feature17_iso.cc` runtime command reads it from the mounted ISO, calls
`jpeg_decode_mem`, checks all 64 decoded pixels and both dimensions, frees the
allocation, and prints a dedicated success marker. The same command matches
Liberation Mono and asks for an otherwise unused size-37 `Q` twice. The first
positive width proves the outline reached `glyph_rasterize`; the equal second
width proves the new cache entry is reusable. The strong frontier smoke must
see both markers before the command-level ISO success marker.

The normal image now declares the generated ISO as an input, so changing a
fixture cannot leave a stale staged image. The build audit classifies that
operation as disk-image packaging. It classifies non-C `gen-*` outputs such
as the large ISO fixture as binary generation instead of generated C.

## Evidence

The promoted seed compiles each renamed source twice to the same validated
i386 `ET_REL` object:

| Source | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/gfx/jpeg.cc` | 21,120 | `ccabae9e3b979031079f1ed72189c990f3aee4aa773c6ec742b5ccc263570851` |
| `kernel/gfx/glyph_raster.cc` | 11,744 | `83d2f4cac28abbc5bb8a92020ab7fb57251b1b927b4fdbc40981f29556aa1e80` |

CupidDis reports `jpeg_decode_mem` as the only global definition in the JPEG
object; its only imports are `kmalloc_debug` and `kfree`. The raster object
defines `glyph_rasterize` and imports only `kmalloc_debug`, `kfree`, and
`memset`.

A forced dry run and a real two-object build set `CC`, `CXX`, `CPP`,
`HOSTCC`, `HOSTCXX`, `ASM`, `AS`, `LD`, `AR`, `NM`, and `OBJCOPY` to commands
that do not exist. Both recipes still complete through the checked CupidC
wrapper.

The strict frontier compiles all 148 checked-in roots twice against a
436-file snapshot with SHA-256
`5e0a69e1ac12e6acec0edf9c21fe09ce1b0e3ca399a545614f58dfa9e0b3fec7`.
Both object sets are byte-identical, have no boundary files, and total
3,619,012 bytes per pass.

The 331-byte JPEG fixture has SHA-256
`76aac1d6ee61f230d47cd6fef3ba1ea50fe55f1a32634c109489cb3b8d931957`.
It uses an 8-bit, one-component SOF0 frame and contains no progressive SOF2
marker. Host contracts pin those bytes, require the ordered guest marker,
and reject JPEG or glyph failure markers. The live glyph proof reports
`width=22 cache=22` through both supported NICs.

The complete `make test` target passes 775 tests in 3,466.546 seconds, with
one expected skip. After the runtime-reachability and audit-classification
review, 233 focused wrapper, frontend, audit, and smoke tests pass in 546.226
seconds. A separate checked audit replay also passes.

The clean final `make -j2 all WAD_SRCS=` build completes in 447.0 seconds.
The resulting artifacts are:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/kernel.elf.pass1` | 8,024,896 | `5e4d10f9ad7fcd3ce96288bc65089263e62ab31f253a312675e36d95d6e49c04` |
| `kernel/cpu/ksyms_data.o` | 105,656 | `af3ef76b05fb0eacea8925177671e8fe06e1424887fced598d2876d187cd8ed2` |
| `kernel/kernel.elf` | 8,131,392 | `0affba20843a1fa01d3bd7a1a648e377e3ab28bb777df5711dc3740b663ac01a` |
| `kernel/kernel.bin` | 7,934,664 | `3ba60890a2d0b14709c3a4570b60ab7ae8456caa6e7ea90b5a791febb80daa80` |
| `cupidos.img` | 209,715,200 | `543c8744c306268207414ede73bd6d8589bd734f1ebab4c106c5b2e8504fba73` |
| `test_usb_partitioned.img` | 33,554,432 | `057e0c86874090c99095f0558e9fa604bd7f1929f4da357da2c1baca949bb2bb` |
| `test_iso/hello.iso` | 389,120 | `f3878646cc77e075dccef3b4e19be843f37d9550c35f289976976e749a87f4e0` |

CupidDis reads the same 4,384 accepted text-symbol rows from each kernel
pass, with no row or address drift. Python packs them into a 105,242-byte
backtrace blob with two zero pad bytes. `_loaded_end` is `0x008912C8`,
`_bss_start` is `0x00892000`, and `_kernel_end` is `0x00CB6A70`. The final
link leaves 451,384 bytes in the bootloader's reserved disk area and 300,432
bytes below the fixed stack base at `0x00D00000`.

The strong four-vCPU runtime passes with `cpu=max` through both supported
NICs. E1000 completes in 191.7 seconds with 126,232 changed framebuffer
pixels. RTL8139 completes in 189.4 seconds with 94,438 changed pixels. Both
runs pass the ordered JPEG and glyph checks, all 62 crypto checks, SMP
startup, DHCP, graphics, AC'97 and PC-speaker audio, UHCI input reattachment,
and six EHCI storage lifetimes. The e1000 log is 71,568 bytes with SHA-256
`f2a8c00a9275fec6b363bf53493e406ebeac2803120c3daa2e9b2cae790a16d2`.
The RTL8139 log is 68,135 bytes with SHA-256
`4deefc0d86f33ef8f43c51f244e37dd6604249059da1062bafb8f7d680536bac`.

## Rejected alternatives

Keeping the `.c` names after production ownership changed was rejected. The
repository uses `.cc` to mark sources owned by CupidC.

Calling the host compiler from the two recipes was rejected because it would
hide a checked-seed regression and leave the ownership audit false.

Dropping floating work from either source, replacing the JPEG decoder with a
smaller implementation, or rewriting the rasterizer around the compiler was
rejected. The compiler was extended for the active sources; the operating
system was not reduced to fit an older compiler.

Keeping the JPEG fixture outside the image dependency graph was rejected
because an updated test file could be omitted from an otherwise current
`cupidos.img`.

Running both four-vCPU gates together was also rejected. Host contention made
the e1000 harness miss the unrelated `godsong` deadline even though the
RTL8139 guest reached both new markers. Uncontended reruns completed through
both NICs under the normal per-command timeout.

Shortening a stale glyph-raster comment was also rejected after the first
full frontier run. The allocator macro records `__LINE__`, so moving the
allocation sites changed the otherwise equivalent object. The comment now
describes the checked CupidC path while preserving the original line depth.

## Consequences

The normal cohort now contains 148 checked-in roots plus the generated kernel
symbol source. All 149 normal CupidC translations use `.cc`. Across the
supported roots, CupidC owns 155 transforms and the host C compiler owns 142.
Making the ISO build explicit adds two host-Python transforms, bringing its
total to 170.

The host compiler still produces 90 normal root objects. Seven strict
checked-in roots remain: `kernel/core/kernel.c`, `kernel/core/string.c`,
`kernel/cpu/fpu.c`, `kernel/cpu/libm.c`, `kernel/cpu/simd.c`,
`kernel/smp/percpu.c`, and `kernel/smp/smp.c`.

This transfer proves production use of static floating data in JPEG and
floating comparisons in glyph rasterization. It does not claim complete
floating-point language support or remove the remaining host compiler,
Python, WSL, Doom, or vendored-code dependencies.
