# ADR 0179: Promote the BSS and SIMD capable Toolchain seed

## Status

Accepted on 2026-07-29.

## Context

The checked i386 Linux CupidC seed could compile the complete libm source, but
it predated two later compiler capabilities needed by the remaining strict
kernel roots. It could not emit the operand-free stack and BSS reset at the
start of `kernel/core/kernel.c`, and it did not carry the six packed SSE2
statement shapes in `kernel/cpu/simd.c`.

Compiler head emitted both unchanged sources deterministically. Normal build
ownership could not move until those capabilities crossed the checked seed
boundary and survived a poisoned-host fixed-point rebuild.

## Decision

Promote the complete stage-three Toolchain set built from pushed revision
`8d5ef4564f753d528630c0f0a78db0f535d56b60`.

The five manifest-bound files remain one seed unit. CupidASM, CupidDis,
CupidLD, and CupidObj are byte-identical to the preceding seed. CupidC changes
to a 2,511,176-byte static i386 Linux image with SHA-256
`4b24bf45726e4ab43fe7830f992120f11de34236daef9ef8753303ab4513934c`.
The manifest binds that image, the four unchanged tools, the source revision,
and the unchanged 19-source plan.

The checked-seed test gate now compiles three unchanged production sources
twice under the exact `KERNEL_I386` profile:

- `kernel/cpu/libm.cc` produces a 16,164-byte object with SHA-256
  `ccfb59839b058020a3cdc30c8e6db7ebac8845215a38ff974b3cbca876574eac`.
- `kernel/core/kernel.c` produces a 25,920-byte object with SHA-256
  `d44d06949d48ead865d0d8c1bdd3b76a67b429e0b7a369318ec4fbe8d9f44ed7`.
- `kernel/cpu/simd.c` produces an 8,768-byte object with SHA-256
  `fd280c321b8eb38a90d4f0982d70b8df0364585e3da322eb2c9de722e071f8d4`.

This decision moves compiler capability across the seed boundary. It does not
move either remaining production recipe or rename either source.

## Evidence

The transition bootstrap started from the preceding checked seed with all
normal host code-generator commands poisoned. It captured 40 inputs with
snapshot SHA-256
`c5807ad5189552501ed25d2a2a2e37dff94867ab65efaca2fb3cf2db54960c6a`.

All 19 C objects, the startup object, and all five linked images matched
between stage two and stage three. Both stages passed five help cases, ten
successful operations, and six useful failures. The four unchanged seed tools
matched stage two. The preceding CupidC image did not match, which was the
expected transition to the new compiler. The run completed in 729.685 seconds.
Its 14,880-byte report has SHA-256
`57864d38fa69f66715173ff3ff9b9f25c4149bd55d5c855fc779549e0315dfdf`.

The promoted manifest is 5,440 bytes with SHA-256
`34ce355ea8939c5f61a2998c2b084ef860e1f13430940abbfb85ddbe8b46790b`.
`make verify-bootstrap-seed` accepts all five files. The manifest check and
three production-source compile proofs pass as four tests in 22.980 seconds.

A second poisoned-host bootstrap started from the promoted seed. Every
checked seed image matched its stage-two replacement. Stage two and stage
three again matched all 19 C objects, startup, and five linked tools and
passed all 21 behavior cases. The run completed in 716.195 seconds. Its
14,879-byte report has SHA-256
`7f7c41bfdddb6bc1d50fa4e225f02db0b80e49f6f61b2790c1ef57391f6a76f7`.
The complete bootstrap-seed module then passed all 25 tests, including another
fixed-point rebuild and the three production-source proofs, in 772.465
seconds.

## Rejected alternatives

Promoting only the changed CupidC file without rebuilding and comparing the
complete five-tool set was rejected because the manifest treats those tools
as one trust root.

Moving the normal kernel and SIMD recipes before seed promotion was rejected
because that would make the production build depend on an unpromoted compiler
head.

Changing either active source to fit the preceding seed was rejected because
both sources already express valid kernel behavior that CupidC now represents
directly.

## Consequences

The checked seed now carries the kernel-entry BSS clear and every packed SSE2
statement in the active SIMD source. The normal build can transfer those roots
through the checked wrapper after their exact recursive closures, build graph,
frontier records, image, and runtime behavior pass.

The normal host-dependency count does not change in this commit. General GNU
assembly, general XMM allocation, other packed templates, and the wider strict
root frontier remain open. `TempleOS/` remains untouched reference material.
