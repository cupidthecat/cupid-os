# ADR 0176: Transfer libm to CupidC

## Status

Accepted on 2026-07-29.

## Context

ADR 0174 promoted a checked CupidC seed that emits every active statement and
file-scope assembly effect in `kernel/cpu/libm.c`. That promotion removed the
old compiler image as a trust blocker, but the normal Make recipe still sent
libm to the host C compiler.

The production transfer had to preserve the source, object, ABI, and guest
behavior. It also needed a closed compiler input set and a runtime check that
uses the linked object. Compiler-head success alone was not enough.

The source contains 43,736 bytes in 1,500 lines. Before the transfer its
SHA-256 was
`f1c13c83b758394189cc74ed6addfd9dfa99d42064c349c548476686b26cabce`.
The promoted seed emitted a 16,164-byte i386 ELF32 relocatable object with
SHA-256
`ccfb59839b058020a3cdc30c8e6db7ebac8845215a38ff974b3cbca876574eac`.

## Decision

Rename `kernel/cpu/libm.c` to `kernel/cpu/libm.cc` without changing its bytes.
Add the root to the checked production cohort and compile it through
`tools/cupidc_kernel_compile.py`.

Freeze this exact source closure for the transform:

- `kernel/cpu/libm.cc`
- `kernel/core/types.h`
- `kernel/cpu/libm.h`

The Make recipe names those inputs and the common checked-seed controls. The
wrapper copies them into a private compiler root, verifies the checked seed,
emits under the fixed `KERNEL_I386` profile, validates the result as i386
`ET_REL`, checks the live inputs for drift, and publishes the object only
after success.

Add `/bin/feature15_libm.cc` to the strong GUI terminal sequence. The gate
requires `[feature15] 22 checks total, 0 failed`, `PASS feature15_libm`, and
the CupidC JIT completion marker. It rejects `FAIL feature15_libm`.

## Evidence

The test-first ownership changes initially failed on five missing contracts:
the `.cc` source, approved wrapper closure, Make rule, guest command, and
failure marker. The focused ownership and runtime set passed all six tests
after the production changes.

The source before and after the rename is byte-identical. Its size and hash
remain 43,736 bytes and
`f1c13c83b758394189cc74ed6addfd9dfa99d42064c349c548476686b26cabce`.

A direct checked-seed compile produced the locked 16,164-byte object in 5.137
seconds. The hosted and native object contract produced the same object in
21.255 seconds.

The normal Make target passed in 2.811 seconds with `CC`, `CXX`, `CPP`,
`HOSTCC`, `HOSTCXX`, `ASM`, `AS`, `LD`, `AR`, `NM`, and `OBJCOPY` set to
commands that do not exist. Its object retained SHA-256
`ccfb59839b058020a3cdc30c8e6db7ebac8845215a38ff974b3cbca876574eac`.

The final combined kernel-wrapper and GUI terminal replay passed all 105
tests in 99.490 seconds. The two affected Linear IR cases and two affected
object-emission cases passed in 40.202 seconds. The regenerated audit drift
check passed in 63.7 seconds, and the complete build-graph audit passed all
62 tests in 633.223 seconds.

The corrected complete frontier compiled all 152 checked-in roots twice with
no boundaries in 1,322.886 seconds. Both passes produced 3,659,840 object
bytes from the same 440-file snapshot. The snapshot has SHA-256
`2143222ba61544b44655f882bc06e55ef0ff195033c907f5ae512801251e9cc1`.

Rebasing the transfer over ADR 0175 changed that snapshot because the
compiler-head sources moved. The first post-rebase frontier compiled both
complete passes, then found the old digest in the test. A bulk digest edit
updated the prose but missed the test's two adjacent string literals, so the
second full run found the same stale assertion. Updating both literals closed
the mismatch. Neither failed run found a compiler boundary or object mismatch.

The regenerated active-source graph contains 698 sources, 253 feature IDs,
504 transforms, and 42 accounted unreachable files. The active-source digest
is `83c5645147c7708c8d93b3215ea778246b2cef9fb4c261c0728340cdc0cfea61`.
The 1,527,581-byte JSON record has SHA-256
`984b3abf1d4ad636793d52df4e4425ef3cf3da572caa1ab38a904276337acb7d`.
The 15,060-byte Markdown record has SHA-256
`f1747bdfac461edef06d3eb5b78882a73b16f909b1b80d2dd0f78f0de0261bf6`.
The 39,038-byte active preprocessor cases have SHA-256
`4a3db85355dec11a8d74a191cdbc5c35a6575c2c500b3c01ec76223a9dc82744`.
Regenerating the audit after the embedded CTXT edit left all three files
byte-identical. The final drift check passed in 55.3 seconds.

The exact normal build completed both CupidLD passes, regenerated the kernel
symbol translation through CupidDis, compiled it through the checked CupidC
wrapper, flattened the kernel through CupidObj, and staged the final image in
796 seconds. A separately created clean image matched the normal image exactly.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/kernel.elf.pass1` | 8,070,372 | `e6d87110870697cb2e0342592aadde06ee97e87546506f0a60d7d4697a2a6c53` |
| `kernel/cpu/ksyms_data.cc` | 350,004 | `90b63f5cb9157dbf8b29c9b6da0e399e8beb2a12dbcc72aebdb7741c8c11ffc7` |
| `kernel/cpu/ksyms_data.o` | 105,988 | `6bc9457da85e1a806179ec6a9fb5666b9d32e26f58ff786600cdcff540006967` |
| `kernel/kernel.elf` | 8,176,868 | `7f03cbe3a9319c0524f95288df9d104d56e14382799764c9961f0585fd20f643` |
| `kernel/kernel.bin` | 7,980,796 | `63a489c9d6256ff7993923316a2cffba163cd2f5e660a754d4e020a69ecf6db8` |
| `cupidos.img` | 209,715,200 | `a68c355c5ce7ab5bc85126f45d9ca37f3ea20ed927aba8a5752f05d7912d437b` |

All 7,980,796 flat-kernel bytes match the image at LBA 5. CupidDis accepts
5,725 symbol rows from each kernel pass, including the same 4,395 text
symbols at the same addresses. The generated blob has 105,574 meaningful
bytes and two zero padding bytes.

Final isolated four-vCPU runs passed the complete frontier and SMP contracts
with e1000 and RTL8139. Both guests reported
`[feature15] 22 checks total, 0 failed`, `PASS feature15_libm`, and CupidC JIT
completion. They also passed network traffic, crypto, FPU, SMP, JPEG, glyph,
swap, input reattachment, six USB storage lifetimes, and both audio paths
without a panic. The 59,240-byte e1000 serial log has SHA-256
`6c00a3387db79509b26efd1cb7f1611807cc575b003613c61a7341a7eec38ea1`.
The 57,565-byte RTL8139 serial log has SHA-256
`3fcebd5c5903713816d3ab2f27bcfcb5ebdac808f4144f114401d990abbe11c3`.

## Rejected alternatives

Keeping the `.c` suffix was rejected because checked-in production roots move
to `.cc` when CupidC takes ownership.

Rewriting libm was rejected because the promoted compiler already represents
the active source. A rewrite would add behavior risk without closing a
toolchain gap.

Leaving the recipe on GCC or Clang after the seed promotion was rejected
because it would preserve a code-producing host dependency after the checked
compiler could own the object.

Using a broad live include tree was rejected because unrelated edits could
enter the compile. The three-file closure is the complete dependency set for
this root.

## Consequences

Checked-seed CupidC now owns 152 checked-in normal roots. The generated kernel
symbol source brings the normal total to 153 transforms. Host C owns 86 normal
root objects, and three strict checked-in roots remain.

The active graph contains 101 C translation units and 300 Cupid C files.
CupidC owns 159 transforms, host C owns 138, and host Python participates in
174 transforms.

Issue #26 remains open. Libm production ownership is complete, but the first
unsupported `xmm1` clobber in `kernel/cpu/simd.c` and the wider GNU attribute
and assembly surface still need compiler work.

`TempleOS/` remains untouched reference material.
