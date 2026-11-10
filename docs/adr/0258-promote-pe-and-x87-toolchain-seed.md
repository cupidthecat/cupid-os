# ADR 0258: Promote PE and x87 capabilities into the checked Toolchain seed

## Status

Accepted on 2026-08-10.

## Context

Revision `9115787311bf455b6eee19e7742cc83aa252e7c8` contains four hosted
Toolchain advances that the preceding checked seed did not carry. CupidASM
and CupidDis share six signed x87 integer load and store rows. CupidLD writes
deterministic PE32 images with canonical imports. CupidC converts between
integers and `long double` at runtime and in static initializers, folds static
long-double controls, and accepts canonical x87 zero, subnormal, normal,
infinity, and NaN payloads. CupidObj source and output are unchanged.

The 19-source build plan remains at SHA-256
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.
The fixed source closure contains 43 files, including the Windows startup and
runtime probe.

## Decision

Promote all five stage-three images from one fixed-point generation:

| Tool | Bytes | SHA-256 | Producer |
| --- | ---: | --- | --- |
| CupidASM | 445,616 | `fc4e3824d01364debbdfcb6a726e9594a6c68d7c1ed013ffbb2d7f78f05644f9` | yes |
| CupidC | 2,632,760 | `bfe4b9581302439ae35dac340c3f3e38812a2ce7b0ce54a8af1e04731cd077c1` | yes |
| CupidDis | 379,648 | `6f3425e7c1fb1e1274c945fdeb891347f1ef681c8b852aec783f4ecd1fa8acfe` | no |
| CupidLD | 312,792 | `9561d6f7170472cd6dccd87d4988fdd2b23a138966cbe4940a9ffb062eab481d` | yes |
| CupidObj | 392,688 | `7137ad601a7c22178112fbf08163b36ff2064807caa99962df97d7ae7ae62f2b` | no |

CupidObj is byte-identical to the preceding cohort. The other four images
change. The manifest still binds all five because the checked seed is one
trust unit. The 5,440-byte manifest has SHA-256
`8fd462648360d3c705e203fc771299007d590d1665a6c253781a4ee83c811c33`.
It names the pushed capability revision and retains the static i386 Linux
target, producer lineage, link orders, and build plan.

The behavior gate is five help cases, seventeen successful operations, and
fifteen useful failures. It includes the imported Windows command. Both
rebuilt stages must assemble its startup, compile its freestanding C source,
link the same PE32 image, and pass the independent format validator. On
Windows, the loader must print `Cupid-built Windows runtime: ok`, write no
stderr, and return 37.

Two direct checked-seed tests provide separate carriage boundaries. One
assembles and disassembles signed `FILD`, `FISTP`, and `FLDCW`, checks static
x87 special payloads and integer conversions, runs the result, and preserves
outputs on rejected forms. The other builds the imported PE32 command and
checks that a direct call to an IAT symbol fails without replacing a sentinel.

## Evidence

Before promotion, the x87 carriage test stopped at the old CupidASM parser
with an unknown `fild` mnemonic. The PE test reached the old CupidLD option
parser, which accepted only ELF output. Both failures left their existing
outputs intact.

The promotion candidate rebuilt from the preceding checked seed with `CC`,
`CXX`, `CPP`, `HOSTCC`, `HOSTCXX`, `ASM`, `AS`, `LD`, `AR`, `NM`, and
`OBJCOPY` set to commands that could not run. The same values were passed as
Make command-line variables. It ran in a detached worktree at capability
revision `91157873` with:

```text
make bootstrap-from-seed BOOTSTRAP_SEED_OUTPUT=.codex-seed-promotion-91157873
```

The run completed in 777.591 seconds. It froze
43 source files with SHA-256
`3619e7d508f55f5e91bf3fa79071fd2dcd818ec8e0281f03a1d9d48f0a7a3547`.

All nineteen C object pairs, startup, and five tool-image pairs match between
stage two and stage three. The preceding seed matches stage two only for
CupidObj, which is the expected transition. The candidate report is 17,036
bytes with SHA-256
`2a4686775111ac2d14dfad8c12c189cb139a61589b120f133d0d8a5530a2403e`.

The stage-two and stage-three Windows objects and images match. The imported
image is 2,048 bytes with SHA-256
`c83ac4a301d82b26527ccd87ec8c020e44c72f7c09a0b228a83e743846a4ca1c`.
It imports `ExitProcess`, `GetStdHandle`, and `WriteFile` from `KERNEL32.dll`
through the expected IAT slots. The Windows loader produced the required
stdout, empty stderr, and exit status 37.

After the five images and manifest were installed, `make
verify-bootstrap-seed` passed. Both direct carriage tests then passed in
7.508 seconds.

The complete checked-seed module passed all 53 tests in 1,064.113 seconds.
Its 20,802-byte log has SHA-256
`b01554062241d69c0a286661fc9e3a85970f12c4789bbe23c487a24c50f82e1a`.

Regenerating the active-source audit exposed two new Toolchain headers and one
new internal preprocessor guard from the promoted capability. The checked
locks now cover 723 active sources, 76 Toolchain sources, 5,895 `sizeof`
expressions, 690 preprocessor source files, and 27 conditional expressions.
The first audit test run overlapped those lock edits and retained four stale
expectations. A frozen rerun found two deeper Python count locks, which were
corrected without changing the inventory. The first full Toolchain replay then
found the same old conditional total in the native preprocessor contract. The
focused 39-test preprocessor module passed after that last lock was corrected.
The final build-graph module passed all 75 tests in 815.127 seconds. Its
27,932-byte log has SHA-256
`a85e9f58d22b2631708e408a62f085d1dd75c35659dbce4bc461b161a332d953`.

The first captured Toolchain build ended before it produced a result. A second
build completed before the native conditional lock was found, so it was not
used as final evidence. With the tree frozen after that correction, `make -C
toolchain all` rebuilt and verified all 20 artifacts in 3,397.047 seconds. Its
12,010-byte log has SHA-256
`b9f6611f6fdda1014b80d6045e6577834c079cffa20cab1b7685a822ecd8fac6`.
The complete `make -C toolchain test` replay passed in 3,289.464 seconds,
including both self-host selectors and all 22 assembly demos. Its 95,992-byte
log has SHA-256
`ccdc557c43e48ca3b4f1d397eab8a97341ae1e6907378da7d945b2faa6556db3`.

An independent post-promotion rebuild completed in 794.659 seconds with the
same eleven variables poisoned in the environment and on the Make command
line. It used:

```text
make bootstrap-from-seed BOOTSTRAP_SEED_OUTPUT=.codex-seed-reproof-91157873
```

All five seed images match stage two, and stage two matches stage three
across the complete 19/1/5 artifact set and 5/17/15 behavior matrix. The
17,032-byte report has SHA-256
`a518747e2e4701f8f75fae083bf38d1d1aa86207c13c34fb78fca3800de21fd6`.
It retains the same source digest, PE bytes, imports, and Windows result as the
promotion candidate.

The post-promotion `make -j4 all test_usb_partitioned.img` gate passed in
622.683 seconds. It rebuilt the normal kernel and disk image and confirmed
that the partitioned USB test image was current. The 199,026-byte build log
has SHA-256
`cb329c31081a6429f9c2d6c383861909eb79f67b629e1d5d322436399ae8c98a`.
The final kernel ELF is 9,106,192 bytes with SHA-256
`51954fb43c093e1fd190867b024e6507fad38d1ced927cbd3112500a80c7ad92`;
the flat kernel is 8,897,492 bytes with SHA-256
`3e76b91bf8394cd80435196c61c475d8909c475beb8ab6df80611de915b77981`.
The 209,715,200-byte disk image has SHA-256
`a6d48c60cb9f3b0c7da2c9fc05ba29a2b194058dcdee8b1b38f015c0f490e102`.
A final `make verify-bootstrap-seed` passed in 0.289 seconds.

The rebuilt image passed the private four-vCPU SMP and frontier-runtime gate
with both supported emulated NICs. The e1000 run completed in 540.671 seconds;
its 126,112-byte serial log has SHA-256
`7d409e0155b66803ee9cbd56249a3a274f08186386bc098ad111ed2949ee7faa`.
The RTL8139 run completed in 521.028 seconds; its 123,032-byte serial log has
SHA-256
`ac77359d66e0e549804382ca4286ed8611c6779d24a628cc3aa4878f6f0dd5cb`.
Both runs passed the compiler frontier, GUI and browser paths, framebuffer,
AC97 and PC-speaker audio, SMP runtime, and JIT completion checks without a
panic or fatal marker.

## Rejected alternatives

Replacing only the four changed images was rejected. CupidObj was rebuilt and
compared in the same generation, and the manifest describes a five-tool
cohort rather than independent executable caches.

Keeping PE imports or x87 payloads as source-only capabilities was rejected.
The normal Toolchain and OS paths execute the checked seed, so source support
alone does not move the runnable trust root.

Using a host compiler, assembler, linker, import library, or PE writer was
rejected. The candidate and its behavior suite ran with every conventional
code-generator variable poisoned.

## Consequences

The checked static i386 Linux seed now carries the 602-row, 247-mnemonic x86
catalogue, the hosted x87 conversion and static payload frontier, and
deterministic imported PE32 output. Windows still executes this Linux seed
through WSL. Python still coordinates the fixed point and normal artifact
publication, and a native Windows five-tool seed remains open.

No normal-build source changes owner in this promotion, so no `.c` to `.cc`
rename is due. The private multidimensional SIMD work is not part of the
hosted seed closure. `TempleOS/` remains read-only reference material.
