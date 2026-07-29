# ADR 0181: Transfer the core string root to CupidC

## Status

Accepted on 2026-07-29.

## Context

ADRs 0154 and 0170 added the two compiler capabilities required by
`kernel/core/string.c`. The checked seed can emit the x87 round-down block in
`str_floor()` and the explicit `double` to `uint64_t` conversions in
`str_from_double()`. Two complete compiler proofs already produced the same
validated object, but the normal Make recipe still used the host C compiler.

This file was the final strict checked-in kernel or driver root outside the
production CupidC cohort. Its transfer had to retain the existing source,
calling convention, floating-point behavior, and object validation policy.

Before the transfer, the source contained 8,751 bytes with SHA-256
`d376b489757cc7835b1e249310dd3c9c26bc920b4799d61ae619613e0765d17f`.

## Decision

Rename the source to `kernel/core/string.cc` without changing its bytes.
Compile it through `tools/cupidc_kernel_compile.py` under the fixed
`KERNEL_I386` profile.

Freeze the exact transform inputs:

- `kernel/core/string.cc`
- `kernel/core/string.h`
- `kernel/core/types.h`

The Make recipe names this closure and the common checked-seed controls. The
wrapper copies the approved closure into a private compiler root, verifies
the seed, validates the emitted i386 `ET_REL` object, checks the live inputs
for drift, and publishes only after the complete operation succeeds.

Keep the old `.c` path outside the approved source set. Tests continue to
require a useful rejection before the compiler executor can run.

## Evidence

The rename retained the 8,751-byte source and its SHA-256. Direct wrapper
compilation and a forced Make rebuild with `CC`, `CXX`, `CPP`, `HOSTCC`,
`HOSTCXX`, `ASM`, `AS`, `LD`, `AR`, `NM`, and `OBJCOPY` poisoned produced
the same object:

| Source | Object bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/core/string.cc` | 14,460 | `d48bb6ea18b7124fbefeaca0d5d5ee8a517db950f21ea88e30ededd6c5c2a577` |

The test-first change initially failed four ownership and recipe assertions.
The completed contracts require the renamed source, the exact two-header
closure, the checked wrapper command, and the expanded approved cohort. The
retired `.c` path remains in the negative source set.

The complete frontier contains 155 checked-in roots and no compiler
boundary. It compiles each root twice against a 444-file snapshot with
SHA-256
`bfa1e7210193b95df3c357a6c893078c86a74afa33e1cb2baa1cafc0173efab6`.
Both 155-object sets are byte-identical, and each totals 3,708,988 bytes.

The regenerated active graph contains 699 sources, 253 feature IDs, 504
transforms, and 42 accounted unreachable files. Its active-source digest is
`996ce721955e6be27cad7b166eab6bef9028614b356a0f9e10d9e13c0cabcd93`.
The generated records are:

| Record | Bytes | SHA-256 |
| --- | ---: | --- |
| `docs/bootstrap/ACTIVE-SOURCE-AUDIT.md` | 15,060 | `8c73b022efa4b2a5af809fad78d68a4978d07d32a535530172916c23fab7bb72` |
| `docs/bootstrap/audits/active-build.json` | 1,532,457 | `8ea61f8db0e40735e86184a457775f0667f4195f61347ba70d77f5b062ec2b65` |
| `toolchain/tests/cupidc_pp_active_cases.inc` | 39,041 | `73658dee198a5b7f4a437c27fa9ef53a16c3e50ebc1292af4b95ccae9265b5ad` |

The normal build completed both CupidLD passes, generated the symbol source
through CupidDis, compiled that source through checked-seed CupidC, flattened
the final kernel through CupidObj, and staged the image:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/kernel.elf.pass1` | 8,095,832 | `5b46f8a0ab033f789fd3ad4fb82811ef3539b91c81484158388523610caaaebe` |
| `kernel/cpu/ksyms_data.cc` | 352,269 | `3e0c8730304b4db6e5636ee6358a6e3bf2673434044604f3340224e608c81f86` |
| `kernel/cpu/ksyms_data.o` | 106,672 | `6e6ccb31aca44246d14372a865da10648222565ebc47f9a6b28aa67dd3f3909d` |
| `kernel/kernel.elf` | 8,202,328 | `9f9bcd4e1e4646ccbe326982feb93502f86d5e86786949aeb1cbdf2dd897998f` |
| `kernel/kernel.bin` | 8,004,284 | `ddd4a197cd1fd3797875786ea6569735f5434103712472bec7f1855f5a66cb42` |
| `cupidos.img` | 209,715,200 | `5769f200192d3a253925d04d58310fa62fa355541032e7943924f8c06b2ee5b9` |

An independently created clean image matched `cupidos.img` byte for byte.
The flat kernel matches the image bytes at LBA 5. CupidDis reads the same
4,420 text-symbol rows at the same addresses from both kernel passes. The
generated symbol blob contains 106,259 meaningful bytes and one padding
byte.

The runtime harness needed one reliability correction before the final
dual-NIC gate. The `godsong` interaction had waited for CupidC's generic
execution marker, which appears before the graphics program is ready for
keyboard input. It now waits for the program's second framebuffer flip and
allows that command two more seconds to settle. The keyboard-substitution
probe keeps its existing timing. All 76 harness contracts pass.
The final combined GUI-harness and kernel-wrapper run passes all 105 tests in
113.583 seconds. The deterministic audit replay passes in 74.2 seconds.

An independent standards review found that the first fixture wrote the frame
marker and completed command output together. The revised fixture withholds
the marker, proves that no follow-up key is sent while the harness waits, and
publishes the command tail only after input begins. The issue-spec review
found no remaining defect. The review also corrected two stale `string.c`
names in the root README.

The final isolated RTL8139 run passed in 240.482 seconds. Its 72,978-byte
serial log has SHA-256
`dcc65e4666ef0b0f1d1df2829d2d7b6b7f63148c2488b4556b2ef44322cf006b`.
It reached the 640x480 desktop, changed 91,302 pixels, checked AC97 and PC
speaker output, completed the USB replug sequence, and passed the complete
frontier runtime contract without stderr.

The matching e1000 run passed in 251.169 seconds. Its 76,483-byte serial log
has SHA-256
`2101c1062c704e972afd4a077703ad6dd4c50171675ea138efed56e9d8d115bb`.
It changed 65,436 pixels and passed the same graphics, audio, storage, and
in-OS compiler checks without stderr.

## Rejected alternatives

Keeping the `.c` suffix was rejected because checked-in production roots use
`.cc` after CupidC owns their normal objects.

Rewriting the string routines was rejected because the checked compiler
already represents the active source. A rewrite would change a mature kernel
interface without removing another dependency.

Keeping the Make recipe on GCC or Clang was rejected because it would retain
the last strict host root after the checked seed could produce its validated
object.

Using the live include tree without a frozen closure was rejected because a
concurrent header change could enter the object without belonging to the
declared transform.

Keeping the generic CupidC execution marker as the `godsong` input boundary
was rejected after two RTL8139 runs reached that marker but sent the
follow-up keys before graphics input was ready. Waiting for the second frame
made the readiness condition observable.

Adding the same two-second delay to every interactive runtime command was
also rejected. It changed the timing of the keyboard-substitution probe and
lost a Shift make or break event. The final delay belongs only to the
graphics command that needs it.

## Consequences

Checked-seed CupidC owns 155 checked-in normal roots. The generated symbol
translation brings the normal total to 156. The strict checked-in kernel and
driver cohort has no host-compiled root.

Across the active graph, CupidC owns 162 transforms, host C owns 135, and
Python participates in 177. The host compiler still produces 83 normal root
objects in other cohorts.

This closes the strict foundational ownership scope in issue #28. Doom,
vendored code, native hosted tools, broader GNU assembly, and the remaining
normal-build roots continue under their own bootstrap issues.

`TempleOS/` remains untouched reference material.
