# ADR 0343: Close the source-head CupidBuild link

## Status

Accepted on 2026-08-25.

## Context

The hosted self-link contract had grown a CupidBuild output, but the Toolchain
Make recipe still passed the older output list. Once that edge was supplied,
CupidC reached `cupidbuild.cc` and reported that standard C `memchr` was not
declared by the static i386 hosted runtime.

Two exact driver locks also predated the expanded CupidASM artifact command and
CupidDis PE32 inspection command. The checked object frontier had already
proved the underlying output deterministic, so these were test-inventory gaps
rather than compiler instability.

## Decision

The Toolchain test passes a dedicated `cupidc-cupidbuild.elf` output to the
self-link contract. That contract compiles and links CupidBuild beside CupidC,
CupidASM, CupidDis, CupidLD, CupidObj, and the runtime probe.

The hosted i386 `string.h` and runtime provide `memchr` with the standard
byte-search interface. The runtime probe covers its first match, unsigned-byte
conversion, embedded zero, absent byte, and zero-length behavior. CupidBuild
keeps the standard call instead of carrying a private substitute.

The complete function, text, object, fingerprint, symbol, and relocation
inventories for the expanded CupidASM and CupidDis drivers are refreshed from
two identical emissions. The earlier source-frontier locks for CupidDis,
CupidASM, and the in-OS ELF assembler bridge are refreshed by the same rule.

## Evidence

Native CupidC compiles the updated hosted runtime. The native source-head
self-link produces all seven outputs, and the linked runtime probe prints
`runtime-ok`. The native hosted-adapter and profile-error selectors pass. The
remaining native ELF32, x86, CupidDis, CupidASM, demo, kernel-ELF, CupidObj,
and CupidLD selectors also pass.

The checked `make -C toolchain test` target passed in 7,586.378 seconds. It
rebuilt and verified 21 artifacts, matched all 58 author and oracle stage
pairs, passed the hosted runtime, and ran the complete Toolchain selector set.
The source-head link produced all seven requested outputs.

The final Linux fixed-point unittest passed in 1,241.572 seconds. Its frozen
55-source closure produced matching stages three and four across 19 C objects,
one startup object, and all five tool images. The current source snapshot is
`fca7f65463e26d48159e8e71be68c8b35aa56a2215ec8b572116f773c21a694c`.
The bootstrap log records the stale source-head evidence found and refreshed
before that final pass.

## Consequences

CupidBuild has a complete source-head compile and link closure without a host
libc dependency. The additional runtime function changes source-head static
tool images, so fixed-point evidence must be refreshed before promotion.

This decision does not add CupidBuild to either checked seed, transfer a normal
Make recipe, or remove Python from production coordination. Seed-directory
membership and independent execution-profile checks remain open.

`TempleOS/` remains untouched reference material.
