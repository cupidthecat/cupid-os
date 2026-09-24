# Transfer the GNU assembly frontier to CupidC

- Status: Accepted
- Date: 2026-07-26

## Context

ADRs 0116 through 0121 added the language and object support needed by eight
strict production roots. ADR 0122 promoted that compiler into the checked
five-tool seed. The roots still used `.c` names and host-compiler recipes, so
the capability proof had not changed normal-build ownership.

The generated kernel symbol translation was also host-owned. Its declaration
was already valid CupidC, but the byte-per-initializer source took too long to
compile through the checked seed. A production transfer needed a smaller
source representation without changing the linked symbol bytes.

## Decision

Rename these roots to `.cc` and compile their normal objects through the
checked kernel wrapper:

- `kernel/core/panic.cc` and `kernel/core/process.cc`
- `kernel/cpu/idt.cc` and `kernel/cpu/pic.cc`
- `kernel/lang/as.cc` and `kernel/lang/cupidc.cc`
- `kernel/mm/paging.cc`
- `kernel/smp/lapic.cc`

Each Make recipe declares the root's exact recursive header closure. The
wrapper accepts each root through an explicit source-and-header allowlist,
freezes and verifies the checked seed, validates the resulting i386 `ET_REL`
object, and replaces the target only after success. The Make prerequisites
and frontier snapshot guard the checked-in source closures.

Generate `kernel/cpu/ksyms_data.cc` as a packed array of little-endian
`unsigned int` words. Keep the exact logical byte count in
`ksym_blob_size`; add at most three zero bytes only to complete the final
source word. The generated source has its own checked closure and drift
contract. Normal checked-in roots retain a 180-second compile limit. The
generated symbol root receives 600 seconds so slower checked-seed hosts fail
with a useful bound rather than an unbounded process.

Keep generated source and object publication transactional. The wrapper
captures the generated source and headers in a private compiler root, rejects
drift in the live copies, and never replaces the previous object after a
failed seed, compile, validation, or drift check.

## Evidence

The checked-in frontier now contains 144 roots with no compiler boundary. It
freezes 432 inputs with SHA-256
`7670679039ca8f2b9b7816a68cb9b391d8a2e65f6b03a7a043d35005b75283bf`
and emits 3,514,456 byte-identical object bytes over two passes. The final
30-check proof completed in 1,356.040 seconds.

The transferred objects are pinned individually:

| Source | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/core/panic.cc` | 10,212 | `84daa51a65d6970ae7a7918b05fe64b7676c39d3309264375e349cf0ae20d428` |
| `kernel/core/process.cc` | 30,216 | `819e6e712cdb08d3b1b112fcc42122a1aa5802b19c0cac8c1a3edbc0bca620d4` |
| `kernel/cpu/idt.cc` | 8,756 | `0ad16fd3250bc09ced7c928cb287123db245980de73c15f0249db71a2f2f6ea3` |
| `kernel/cpu/pic.cc` | 2,408 | `c1855a19e0cd285953996344493dcefe916f06d89fed706219718920b4d2ea5d` |
| `kernel/lang/as.cc` | 148,056 | `f05ffb741a81403f3bfb86358b3f96011b2ddef65c87e291f582c1d77b0cedfd` |
| `kernel/lang/cupidc.cc` | 288,180 | `4e8501e628a770b346bbe16e23d9549c4320f1f01f0ddcb9309b907a8c898046` |
| `kernel/mm/paging.cc` | 2,336 | `fc9b757a35cf474f90436333ba732be252253feeea531cad851215e17f793e2d` |
| `kernel/smp/lapic.cc` | 4,184 | `6ce344d265ad3fb6b221a9159d860954c5f5512a7eac526838e69bc181a4c045` |

The packed symbol generator emits 345,405 source bytes with SHA-256
`7d3c81da65335df7214bd8be0629194938cb3bcf87c16b54950d49b726132efd`.
Two checked compiles produce the same 104,600-byte object with SHA-256
`475335be28078c794f423bc4d0bb00cf0474289f23bacbc1f7314d29e5b4abd5`.
Its `.ksyms` section contains the exact 104,185-byte logical blob followed by
three zero padding bytes.

Both CupidLD passes and the CupidObj raw conversion complete in the normal
image build. The artifacts are:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/kernel.elf.pass1` | 7,949,776 | `e2438e7a2c7988d9537a4a4e16a05db15865e2f42aed583b4da462398062788b` |
| `kernel/kernel.elf` | 8,056,272 | `5b13f5e02bd6cc394b575783ebaa432a291472cac1acd212c4632c6dfa3c1722` |
| `kernel/kernel.bin` | 7,861,556 | `6c4085b62ca8e28099b1621d5f0ed75fc4301461cef3996e591a103229514f6a` |
| `cupidos.img` | 209,715,200 | `fffb2e8615ea20a8948a9dd7e1cd1d6fd6ccb88931d9a6b02d244223e6649ba6` |

CupidDis reads 4,069 text symbols from both kernel passes. Every shared
symbol keeps the same address. A four-vCPU QEMU run starts all processors,
passes the runtime SMP checks, reaches the desktop and terminal, and runs
`/bin/ls.cc` through CupidC. The 58,789-byte serial log has SHA-256
`637002626058492b8de1d2bfb34c154f32f2d4d687211b36bafebd2be8cfc1ce`.

The regenerated graph contains 698 active sources, 253 feature IDs, 500
transforms, and 42 accounted unreachable files. Across the root and
supplemental builds, CupidC owns 151 transforms, the host C compiler owns
146, and Python owns 163. The host compiler produces 94 normal root objects.
The active-source digest is
`81aad9a2de145b0400cd77277db0500f9ba2b6deaba6450820a2cad3aad0418e`.

## Rejected alternatives

Keeping the eight files named `.c` was rejected because the extension states
which compiler owns a production source.

Leaving the generated symbol object on the host compiler was rejected after
the checked seed proved the exact declaration and object path.

Keeping the byte-per-initializer source was rejected. A measured checked
compile took 1,041.3 seconds and exceeded the normal wrapper's 180-second
limit. Packing the same bytes into target-endian words reduced the measured
compile to 65.6 seconds without changing the logical blob or final symbol
addresses.

Raising every kernel compile timeout was rejected. The extra allowance belongs
only to the large generated translation.

Removing symbols or shortening the kernel symbol payload was rejected because
that would reduce operating-system behavior to fit the current compiler.

## Consequences

The normal build now has 144 checked-in CupidC objects plus the generated
kernel symbol object. Nine production transformations leave the host compiler
without changing source behavior or the linked symbol map.

Ten strict checked-in roots, Doom, vendored C, native hosted contracts and
commands, Python orchestration, and WSL execution on Windows remain
host-dependent. The normal build is not yet fully self-hosted.
