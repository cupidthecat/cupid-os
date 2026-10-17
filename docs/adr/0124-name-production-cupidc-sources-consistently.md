# Name production CupidC sources consistently

- Status: Accepted
- Date: 2026-07-26

## Context

Checked-seed CupidC owns 144 checked-in translations in the normal build.
Before this change, 28 of those roots used `.cc`, while 111 roots owned only
by production CupidC still used `.c`.

Five Toolchain roots have a second role. The normal OS build compiles them
with CupidC, but the checked-seed fixed point and native hosted contracts
still consume their `.c` paths. Renaming those files changes trusted seed
inputs and native language-mode selection, so they need a separate seed
refresh and proof.

## Decision

Rename the 111 roots used exclusively by production CupidC from `.c` to
`.cc`. Update their active build, test, and documentation references without
changing source behavior, object ownership, or output contracts.

The checked-in cohort now contains 139 `.cc` roots and these five seed-bound
`.c` roots:

- `toolchain/ctool.c`
- `toolchain/cupidasm.c`
- `toolchain/cupiddis.c`
- `toolchain/elf32.c`
- `toolchain/x86.c`

The generated `kernel/cpu/ksyms_data.cc` translation brings the normal build
to 140 `.cc` translations within its existing 145-transform CupidC cohort.
The broader ownership counts remain 151 CupidC transforms, 146 host C
transforms, and 163 Python transforms. The host compiler still produces 94
normal root objects.

Rename the five shared Toolchain roots only after a separate seed refresh
reproduces the five-tool fixed point with the new paths and updates the native
contracts to select the intended C language mode.

Run the closed-source frontier, repeat-emission, poisoned-host, normal-image,
link, and runtime checks again after the path transfer. Record hashes,
digests, byte counts, and timing only from that completed proof. Existing
values in earlier ADRs remain historical evidence for the graphs they
measured.

## Rejected alternatives

Leaving the 111 files named `.c` was rejected because their production owner
is already CupidC. The old names made the extension unreliable as an
ownership marker.

Renaming the five shared Toolchain roots in this change was rejected because
it would alter checked-seed inputs before the fixed point and native-host
contracts had been refreshed.

Changing source behavior to make the rename easier was rejected. This is a
path and ownership-label change, not a language simplification.

## Evidence

The isolated frontier compiled all 144 checked-in roots twice from 432 frozen
inputs. Its input snapshot has SHA-256
`938cee8dfd75ca09c1b16da6e107b811d4edbe8482f63a52e04e61a85c7b647f`.
Both passes produced the same 3,514,568 object bytes. The final test completed
in 1,317.122 seconds.

Snapshot hashes use canonical LF text bytes, matching `.gitattributes` and
the active-source audit. CRLF and LF checkouts therefore name the same source
content. Lone carriage returns are normalized to LF as well.

The total is 112 bytes larger than the graph recorded by ADR 0123. Twenty-eight
renamed objects crossed a four-byte file-size boundary because their source
path in object metadata gained one character. Their normalized source code is
unchanged, and every object remains deterministic.

The regenerated active-source audit reports 698 active inputs, 253 feature
IDs, 500 transforms, and 42 accounted unreachable files. The language split is
27 assembly files, 128 C files, 270 C headers, and 273 Cupid C files. Its
active-source digest is
`0a51a83a53ece2df74529c0166d26f9e4a38a2f7c9f1762969d69fdf80313a3d`.
The 151 CupidC, 146 host C, 163 Python, and 94 host root-object counts do not
change.

The clean normal image built in 292.125 seconds. It completed both CupidLD
passes, generated the symbol translation through CupidDis and Python, compiled
that translation through CupidC, and flattened the final kernel through
CupidObj.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/cpu/ksyms_data.cc` | 345,405 | `7d3c81da65335df7214bd8be0629194938cb3bcf87c16b54950d49b726132efd` |
| `kernel/cpu/ksyms_data.o` | 104,600 | `475335be28078c794f423bc4d0bb00cf0474289f23bacbc1f7314d29e5b4abd5` |
| `kernel/kernel.elf.pass1` | 7,953,872 | `ded23a6f0f30fab668d179d50b7510e7acf2cbfe879b45f944eef04026af4ec6` |
| `kernel/kernel.elf` | 8,056,272 | `9d8c9c1af22a7a3c6cbb629a92c82aa63729bca9c379597feb32a3b5dcefd6f4` |
| `kernel/kernel.bin` | 7,862,352 | `1ffb43764bc224dc48a523e47c5e2425d14fa2e965a1801b419578c651fb27a6` |
| `cupidos.img` | 209,715,200 | `b3d903f79f39c44a4c4d5a64533e08214cd9a0c0ffde1038a717293688c8a7df` |

The `.ksyms` section is 104,188 bytes. It carries 104,185 logical bytes and
three zero pad bytes. Its `0x000010f6` count word and current CupidDis output
both report 4,342 global text symbols. CupidDis emits the same 4,342 lines
from both kernel passes. This corrects the stale 4,069 count in earlier prose;
the recorded blob size and hashes were already for the 4,342-symbol data.

`_loaded_end` is `0x0087F850`, leaving 523,696 bytes below
`0x008FF600`. `_kernel_end` is `0x00CA4A70`, leaving 374,160 bytes below
the `0x00D00000` stack boundary. The 7,862,352-byte image payload at LBA 5
matches `kernel/kernel.bin` exactly.

The four-vCPU QEMU gate seeds the CSPRNG through RDRAND, passes all 62 crypto,
ASN.1, and X.509 checks, observes the one-CPU MP result and four-CPU ACPI
result, starts CPUs 1 through 3, and reports four of four CPUs online. It also
passes every in-kernel Cupid toolchain self-test, initializes e1000, reaches
the desktop and terminal, and completes `/bin/ls.cc` JIT execution. The
59,644-byte serial log spans 64.515 seconds and has SHA-256
`7d426813b433c322c25df2bf8df550e990bb76d80c05f9f09a9729b243b21478`.

## Consequences

The `.cc` extension now identifies every checked-in root used only by
production CupidC. The five `.c` exceptions identify the remaining seed and
native-host boundary instead of an untracked naming inconsistency.

Ownership and output counts do not change. The renamed graph passes its
path-sensitive frontier, clean build, symbol, memory, image, and runtime
checks. Historical ADRs and earlier bootstrap log entries keep their original
paths and results unless a later entry explicitly corrects a factual count.
