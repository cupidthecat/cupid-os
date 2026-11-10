# ADR 0303: Retain typedef callback signatures in private CupidC

## Status

Accepted on 2026-08-20.

## Context

ADR 0301 retained a complete signature for a named block-local function
pointer. Active callback declarations in the ISO9660, FAT, Doom, and Toolchain
sources usually spell that type through a file-scope typedef and use it
directly on a free-function parameter. Private CupidC kept only the four-byte
pointer
representation at that boundary. An indirect call could therefore lose fixed
argument conversions, arity, record identity, variadic state, and its result
channel.

The private compiler also writes fixed-address ELF32 executables. Its data-free
case advertised two program headers while emitting only the code header, and
moving code forward to make room for an absent header would have changed the
established `0x80` file offset.

## Decision

Keep callback signature metadata beside the existing file-scope typedef
table. The table has sixteen entries. Each entry can record a result type and
record index, at most 32 fixed parameter types and record indexes, a fixed
count, prototype state, and variadic state. A direct function-pointer typedef
declaration publishes one alias and consumes one entry. For a free-function
parameter declared directly with that alias, the parameter record-index slot
carries the referenced typedef entry. This is a bounded side table, not a
recursive type graph.

When a free-function parameter is declared directly with one of those
typedefs, copy the signature to the parameter symbol. JIT and AOT indirect
calls then use the same fixed cdecl validation and conversion path as direct
calls. They check fixed arity, scalar and SIMD slot widths, record-pointer
identity, the variadic boundary, and the result channel. Direct structure and
array results
remain rejected. A represented integer constant zero remains a null pointer.
An explicit cast through `void *` remains the intentional signature-erasure
path.

Retain source and REPL transactions around the typedef metadata. A rejected
translation restores the prior typedef count and every touched symbol,
prototype, definition, kernel binding, patch, label, control frame, statement
depth, and emitted code or data boundary. A failed declaration cannot leave a
signature visible to the next compile.

For a data-free AOT executable, emit one program header and set `e_phnum` to
one. Keep code at file offset `0x80`. Emit the second header only when a data
segment exists.

## Evidence

`python -B -m unittest -q tests.test_private_cupidc_call_abi` passes all 231
tests in 37.585 seconds. Positive JIT and AOT cases cover direct typedef
parameters on free functions, the ISO `uint8_t` to `uint32_t` callback
conversion, fixed SIMD transport,
variadic fixed prefixes and default promotions, REPL reuse, and later target
definitions. Negative and same-state cases cover wrong arity, parameter type,
record identity, result channel, variadic boundary, null provenance, the
32-parameter capacity and 33-parameter rejection, and leaked metadata after
failed program and REPL transactions.

The AOT layout cases also pin one program header with code at file offset
`0x80` when no data is emitted, and two program headers when initialized data
is present.

The AOT regression runs a code-only executable that returns 17 and checks one
program header with code still at offset `0x80`. The existing nonempty-data
case keeps two headers.

`python -B -m unittest -q tests.test_gui_terminal_smoke` passes all 125 tests
in 1.000 second. It requires the new guest marker
`[feature14-callback-typedef] PASS float4=4 calls=1` and rejects a stale or
missing marker. At the preceding integrated checkpoint, the private four-vCPU
e1000 smoke with CPU `max` passed in 64.601 seconds. It printed
`[feature14-call] PASS float4=4 double2=2 nested=2 calls=6`,
`[feature14-callback] PASS float4=4 double2=2 calls=2`, the typedef-callback
marker above, `PASS feature14_simd`, and the JIT completion marker in order.
Its 33,219-byte log has SHA-256
`e39a1905002c2baa483c65eb6e763f4f62907c22f8954873dbb20f4ba5a53e93`.
The log contains no rejection markers, and the source image stayed unchanged.

The final checked self-host command,
`make kernel/lang/cupidc_parse.o kernel/lang/cupidc_elf.o`, passed in 70.9
seconds with the promoted Windows checked seed. `cupidc_parse.o` is 459,544
bytes with SHA-256
`266eeb3e531d26770501e514fd51a64bd98022b738c2165aa3bb8b2fed38ac62`.
`cupidc_elf.o` is 3,604 bytes with SHA-256
`c2ad171aacd493a33a477e7a3196a5d28b04b0f74521cd8cbaec2598f391880c`.
This supersedes the earlier 81.9-second pre-final object build. The final
post-CTXT audit generated in 71.299 seconds, and deterministic check mode
passed in 72.051 seconds. Its active-source digest is
`6ebbbbf7e10e349ba703fc335e87ba5ba40f241d477155f879f2b86b879efd22`.
The preceding poisoned-host OS build passed in 684.260 seconds. That build and
the 64.601-second smoke above remain historical checkpoint evidence.

The first post-documentation fully poisoned build reached only the expected
size mismatches after 680.281 seconds. The 9,324,520-byte pass-one ELF stayed
within policy, while the final ELF measured 9,451,496 bytes and the raw kernel
measured 9,228,296 bytes. Only the policy rows for those two outputs changed.
The artifact group then ran 45 tests in 2.582 seconds with four expected
Windows skips. The definitive fully poisoned build passed in 708.912 seconds.
It checked all fourteen artifacts, preserved the FAT contents, and staged
`test_iso/hello.iso`. The final ELF, raw kernel, and 209,715,200-byte image have
SHA-256 values
`718470e9e08ee8eb07aeae7512c6c74c9bcb4b102290fdcf237d956cc9afc616`,
`8e5d7c172814dd5db51a16acd41bf0436cb613a7da5f67511622c4b6517e0dbb`,
and `8a7a67e3da4dd8e256bbe1f69d511b59dc9f669cb6026acbeca055c998889195`.

The strong full private frontier used e1000, four `max` vCPUs, SMP, a private
image, and the USB fixture. It passed in 801.490 seconds. The 640x480
framebuffer changed 96,925 pixels. AC97 produced 32,722,102 stereo 44,100 Hz
frames with a peak of 25,600, and the PC speaker produced 73,533 stereo 44,100
Hz frames with a peak of 8,415. The direct-call,
named-callback, typedef-callback, overall feature-14, and JIT markers each
appeared once and in order. The 150,376-byte log has SHA-256
`73f77abc06357bf5d7185b40825d9d197e9954014ccf09362e9a1d219cc30f02`
and no rejection markers. The source image stayed unchanged at SHA-256
`8a7a67e3da4dd8e256bbe1f69d511b59dc9f669cb6026acbeca055c998889195`.
ADR 0305 records the artifact-size policy expansion.
The promoted standalone CupidC seeds do not contain this private in-kernel
parser or ELF writer. Their fixed-point reproof is not callback or AOT carriage
evidence.

## Rejected alternatives

Do not infer a callback signature from argument expressions at each call.
That cannot recover the declared fixed types or result channel.

Do not widen the global typedef table or build a general recursive type graph
in this increment. The active free-function parameter cases fit the existing
sixteen-entry lifetime and need one retained signature reference.

Do not add a dummy data segment or move code to another file offset. A
code-only executable needs one truthful load segment.

Do not special-case the ISO, FAT, Doom, or SIMD callback names. The typedef
declaration is the ABI authority.

## Consequences

File-scope function-pointer typedefs can cross direct free-function parameters
without losing their fixed signature. Active scalar and SIMD callbacks use the
same validation and slot rules in JIT and AOT output.

The metadata remains limited to sixteen file-scope typedef entries and 32
fixed parameters per retained signature, with one alias per function-pointer
typedef declaration. Typedef-typed local objects and method parameters do not
carry the typedef index. Global function-pointer
objects, record or class fields, later pointer assignments, recursive nested
signatures, direct structure or array results, and arbitrary computed callback
expressions remain open. `TempleOS/` remains read-only reference material.
