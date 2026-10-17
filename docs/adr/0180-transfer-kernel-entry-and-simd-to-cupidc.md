# ADR 0180: Transfer kernel entry and SIMD to CupidC

## Status

Accepted on 2026-07-29.

## Context

ADR 0179 promoted a checked CupidC seed that emits the complete kernel entry
and SIMD sources. The normal Make recipes still sent both files to the host C
compiler, so that promotion had not removed either production dependency.

The transfer covers two different compiler boundaries. The kernel entry owns
the fixed stack setup, linked BSS clear, call to `kmain()`, and halt loop. The
SIMD source owns CPUID state handling and six packed SSE2 statement shapes.
Both paths affect boot or runtime behavior, so compiler-head success by itself
was not enough.

Before the transfer, `kernel/core/kernel.c` contained 31,172 bytes with
SHA-256
`fcc92bb561ed107ec6b328f5e9502f1040a2fedd9cf573f6876e5b93556945c3`.
`kernel/cpu/simd.c` contained 13,971 bytes with SHA-256
`5b4c892322d41e901cdeda34817f79a6547139a2ed703fb6a90eb4b06d34692d`.

## Decision

Rename the files to `kernel/core/kernel.cc` and `kernel/cpu/simd.cc` without
changing their bytes. Add both roots to the checked production cohort and
compile them through `tools/cupidc_kernel_compile.py`.

The SIMD file contains reviewed mixed line endings. Give its new path a
specific `-text` Git attribute so the rename stores and checks out the exact
source blob instead of silently normalizing it while staging.

Freeze the complete recursive input set for each transform. The kernel entry
has a 63-header closure. SIMD has a seven-header closure. The Make recipes
name those inputs and the shared seed controls, while the wrapper copies the
approved closure into a private compiler root.

The wrapper verifies the checked seed, emits under the fixed `KERNEL_I386`
profile, validates the result as i386 `ET_REL`, checks every live input for
drift, and publishes the object only after success. The old `.c` paths are
explicitly rejected.

Keep the source behavior and ABI unchanged. CupidC now represents the active
requirements; the source does not need a reduced kernel entry or a
host-specific SIMD substitute.

## Evidence

The byte-preserving renames retained both source sizes and hashes. Two
checked-seed builds of each source produced byte-identical objects:

| Source | Object bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/core/kernel.cc` | 25,920 | `d44d06949d48ead865d0d8c1bdd3b76a67b429e0b7a369318ec4fbe8d9f44ed7` |
| `kernel/cpu/simd.cc` | 8,768 | `fd280c321b8eb38a90d4f0982d70b8df0364585e3da322eb2c9de722e071f8d4` |

The kernel-wrapper module passed all 29 tests. Its positive cases cover both
new closures and exact objects. Negative cases reject the retired paths,
unapproved inputs, closure drift, malformed output, and failed publication.
A poisoned-host Make expansion emits only the checked wrapper commands for
both targets. The final combined wrapper, freestanding-codegen, and
memory-layout replay passed all 36 tests in 112.171 seconds.

The complete hosted compiler set passed 282 tests: 93 frontend cases, 82
Linear IR cases, and 105 object cases. The two direct checked-seed source
proofs also passed. The complete native Toolchain contract target passed,
including the compiler, assembler, disassembler, object, and linker cases.
The complete checked-seed module passed all 25 tests, including another
five-tool fixed point and both transferred source proofs, in 813.110 seconds.
The corrected full frontier passed in 1,626.509 seconds. It compiled all 154
checked-in roots twice with zero boundaries. The two 154-object sets match
byte for byte, and each set totals 3,694,528 bytes from the reviewed 443-file
snapshot with SHA-256
`c94e8f69bfb3de5792ad81ec0334b4ef88be56d6437926f32146630c26f0b50d`.
CupidDis accepts both production objects. It decodes the kernel entry's stack
setup, BSS clear, `kmain()` call, and halt path. In the SIMD object it decodes
`movdqu`, `movntdq`, `sfence`, `pshufd`, `punpcklwd`, `pmullw`, `paddw`,
`packuswb`, and `paddusb`.

The normal build completed both CupidLD passes, generated symbols through
CupidDis, compiled the generated translation through checked-seed CupidC,
flattened the final ELF through CupidObj, and staged the disk image.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/kernel.elf.pass1` | 8,083,520 | `e79a858431b96f6152441ef62ef75ec2fc6cc86e8020a18a7e46b0dbf6d8515a` |
| `kernel/cpu/ksyms_data.cc` | 352,217 | `4d18784c13ff6cb45a7b0025030c68c9e7a9f1d94e40381f426344d190900a3b` |
| `kernel/cpu/ksyms_data.o` | 106,656 | `b884e111c9141a299fdf2082224f5da56205a877366f51f5cf9820a0d11558ab` |
| `kernel/kernel.elf` | 8,190,016 | `25b7b66b4a7997fe9d0b4a8bfb163a016b18c8a66dc415889355bc7d1bfee372` |
| `kernel/kernel.bin` | 7,994,604 | `a930d09bfc2e1cc6db8652412bfec9b4bd798fcc5082d0717913d9804d89dbf8` |
| `cupidos.img` | 209,715,200 | `2b144f8c005756f5f4f254b1132f8d0de42f25c8bbced8673f6ec64af574d0ee` |

An independently created clean image matched `cupidos.img` byte for byte.
All 7,994,604 flat-kernel bytes match the image at LBA 5. CupidDis reads the
same 4,419 text-symbol rows at the same addresses from both kernel passes.
The generated blob contains 106,241 meaningful bytes and three padding bytes.

The regenerated build graph contains 699 active sources, 253 feature IDs, 504
transforms, and 42 accounted unreachable files. Its active-source digest is
`ef26b7bd09cdf4fcb3eec19b5a5599714a5a52e9faf061310159cbe50b5edd3c`.
The 1,531,825-byte JSON record has SHA-256
`1c451c56984f182f2e13ee61cae52056a2afb877da39530db7a5e1ad7a0fc442`.
The 15,060-byte Markdown summary has SHA-256
`50f911374bd36c5dd15a957408a7f329f90d6043a12353d3a111cc429082d13b`.
The 39,040-byte preprocessor case file has SHA-256
`9ec21e0316b8f5b16c962283736c024b63aa50e82838d4c6c680a4676331788e`.
The deterministic audit check accepts all three records, and the complete
build-graph module passed all 62 tests in 605.004 seconds.

The first runtime attempt started both four-vCPU guests together under the
default 45-second deadline. Both stopped during first-boot HomeFS work without
a panic or rejected self-test marker. An uncontended e1000 retry still reached
the same deadline while creating `HOMEFS.SYS`. This was a test-control
failure, not accepted runtime evidence.

The isolated e1000 proof used a 180-second allowance and completed in 75.291
seconds. It brought all four CPUs online, enabled SSE2, passed all 62 TLS
checks, entered the desktop, launched the terminal, and compiled and ran
`/bin/ls.cc` inside Cupid OS. Its 64,192-byte serial log has SHA-256
`68a870df5431d350d91802925db6e6a5221e770600ef5849b6a7a217cf51a2b0`.
The isolated RTL8139 proof passed the same contract in 70.426 seconds. Its
62,351-byte log has SHA-256
`09e75044765e4c0d003dbf51c86f1f8a78032a45a6d3798f37a6db269a8ea25a`.
Neither final log contains a rejected panic, exception, storage, SMP, TLS, or
network marker.

## Rejected alternatives

Keeping the `.c` suffix was rejected because checked-in production roots move
to `.cc` when CupidC owns their normal objects.

Rewriting either source was rejected because the checked seed already
represents the active code. A rewrite would add behavior risk without closing
another toolchain gap.

Keeping the normal recipes on GCC or Clang was rejected because it would
preserve two code-producing host dependencies after the checked compiler
could own both objects.

Using the live include tree without a frozen closure was rejected because an
unrelated header edit could enter a compile without being part of its
declared transform.

## Consequences

Checked-seed CupidC now owns 154 checked-in normal roots. The generated symbol
translation brings the normal total to 155. Host C owns 84 normal root
objects. Across the active graph, CupidC owns 161 transforms, host C owns 136,
and Python participates in 176.

`kernel/core/string.c` is the only strict checked-in root outside production
CupidC ownership. Issues #26 and #28 remain open because broader GNU assembly,
native execution, Doom, vendored code, and the string production transfer
still need work.

`TempleOS/` remains untouched reference material.
