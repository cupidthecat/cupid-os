# ADR 0247: Serialize fixed-layout PE32 images with CupidLD

## Status

Accepted on 2026-08-08.

## Context

The checked Toolchain seed contains static i386 Linux executables. Linux runs
them directly, while Windows uses WSL. The optional native Windows CupidC and
CupidLD commands are still built by Clang and its linker, so they cannot start
a Cupid-owned Windows bootstrap.

CupidLD already owns symbol resolution, i386 relocation application, and final
layout for ordered static ELF32 objects. Adding a second linker for Windows
would split that authority. The next useful boundary is narrower: keep the
existing link operation and give it a deterministic PE32 serializer for one
known layout.

## Decision

Source-head CupidLD accepts this fixed-image command:

```text
cupidld -m i386pe --text-address 0x00401000 --entry _start -o OUTPUT OBJECT...
```

The command consumes ordered static i386 `ET_REL` inputs through the existing
link engine. It resolves the requested entry and ordinary CupidLD relocations
before serializing the final image. Failure leaves an existing output
unchanged.

The source-head CLI publishes both ELF and PE images through an adjacent
candidate created with exclusive-create semantics. It writes and closes the
candidate file, reopens it, checks its size and contents against the in-memory
linker result, closes the verification read, and then makes one filesystem
replacement call. A partial-write error, close error, verification mismatch,
or replacement error leaves the old destination untouched. CupidLD then
attempts to remove the candidate; cleanup failure is not reported.
Candidate-name search stops after 4,096 exclusive-create attempts and fails
before writing when none succeeds.

On POSIX, CupidLD requests mode `0777`; the process umask may remove any
permission bits. The published candidate's mode replaces any prior destination
mode. The output directory must remain
under the caller's control and keep stable names for the duration of one
command. The publisher does not lock or pin the destination path, defend
against a same-user directory writer after verification, or promise crash
durability. The boundary covers one command's replacement step. Guarded
multi-process publication remains outside it.

Publication remains a private CLI adapter. `ctool_ld_link` still returns a
transactional memory buffer and has no native-path or filesystem policy.

The PE image has an image base of `0x00400000`, with `.text` at RVA `0x1000`.
Nonempty `.rodata`, `.data`, and `.bss` sections follow in that order at the
next `0x1000` boundary. Empty output categories are omitted from the PE section
table. File alignment is `0x200`. The entry point must be in
file-backed executable bytes. The current format is an i386 console image with
PE32 magic `0x10b`, console subsystem 3, operating-system and subsystem
versions 6.0, and DLL characteristics `0x0100` for NX compatibility.
Timestamps, the COFF symbol fields, and the checksum are zero so repeated links
produce the same bytes. A canonical DOS stub points to the PE header at file
offset `0x80`. Stack and heap reservations are each `0x00100000`, with
`0x1000` committed, and all header and section padding is zero.

The serializer writes no import table and no base-relocation table. All sixteen
data-directory entries are zero. It does not accept COFF objects, choose a
layout, import a host runtime, or promise that Windows can execute a generated
tool. It also rejects writable executable input instead of emitting an RWX
section. Those other capabilities belong to later bootstrap work.

The layout and header fields follow
[Microsoft's PE format description](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format).
The bootstrap harness parses the result independently. It checks the DOS and
PE signatures, i386 machine type, PE32 optional header, image and entry
addresses, alignments, section bounds and permissions, zero data directories,
and that the entry lies in an executable file-backed section. This validator
does not run the image.

The source-head fixed-point behavior matrix contains one successful PE32 link
and one rejected layout. It therefore has five help cases, sixteen successful
operations, and fourteen useful failures. Both stages produce the same PE
bytes and the same failure diagnostic while preserving both sentinel outputs.

## Evidence

The 19-source build plan remains unchanged at SHA-256
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`,
and the frozen source closure still contains 41 inputs. All 19 C object pairs,
startup, and five tool images match between stage two and stage three. Of the
five comparisons with the promoted seed, only CupidLD differs. The other four
images remain byte-identical.

The promoted seed itself is unchanged. Its post-promotion proof remains the
5/15/13 matrix recorded by ADR 0243. The PE32 command exists in source head but
not in that checked seed. The fixed-point proof rebuilds source-head CupidLD
from the seed and uses the 5/16/14 matrix.

`make test-toolchain-fixed-point` passed in 757.141 seconds. A retained
checked-seed bootstrap passed in 838.053 seconds and published a 15,060-byte
report. Its 287,804-byte source-head CupidLD images match, as do the two
1,024-byte PE32 images. LLVM `llvm-readobj` independently identifies the
published image as i386 PE32 with one executable, read-only `.text` section,
entry RVA `0x1000`, image base `0x00400000`, `0x1000` section alignment, and
`0x0200` file alignment.

The focused four-section fixture is `0x0a00` bytes. Its `0x0400`-byte header is
followed by three `0x0200`-byte file-backed sections; `.bss` occupies memory
only. Absolute relocations in `.text` resolve to the fixed read-only, writable,
and BSS addresses. Repeating the command produces identical bytes. Invalid PE
selectors, a text address other than `0x00401000`, an entry outside file-backed
executable code, writable executable input, and malformed ELF input leave an
existing destination untouched. The injected publication harness substitutes
the candidate between close and verification and injects write, close, and
replacement failures. Each failure preserves a sentinel destination and
invokes cleanup once. It also covers two occupied candidate names, all 4,096
names occupied, and a later successful call in the same process. The
build-graph audit requires the real
verifier to check size and contents, propagate its close status, and run before
replacement. Mutations that bypass or weaken any of those steps fail. It also
rejects a mutation that routes a successful link through `ctool_job_write`
instead of this publisher.

The regenerated audit retains 719 active inputs, 447 transforms, 255 feature
records, and 25 accounted unreachable files. It records
`cupidld.pe32_fixed_image` as a source-head capability, not as a production
owner or checked-seed feature. The hosted CupidLD module passed all ten tests
on Windows and under WSL. The complete bootstrap module passed 50 tests in
901.358 seconds, and the build-graph audit passed 75 tests in 808.833 seconds.
Audit generation completed in 68.392 seconds; the final independent freshness
check completed in 92.535 seconds.

The complete Toolchain build published all 20 checked contract artifacts in
2,836.013 seconds. A fresh private user build compiled and linked `hello`, `ls`,
and `cat` in 5.615 seconds. The normal root build passed in 1,738.517 seconds,
and a private QEMU smoke ran `/bin/ls.cc` successfully in 49.997 seconds. The
smoke validates the existing ELF path; no PE image was executed.

Recorded proof values:

- Source snapshot SHA-256: `7b6b40b666acc599f758065e2be4fc7824823618d0ffd46350450699eb980dcb`
- Active-source digest SHA-256: `3ccfc3161018c2569873255aca8b86a581a5cd36ec6c016669ae95935727ac47`
- Source-head CupidLD SHA-256: `f2a126d57072e268b13cd0ab36f7b1067e586d85cae987afcfd148a961410b87`
- Published stage-two fixed-point PE32 SHA-256: `656526add2a4703dbb9bdce21fe00e93ff2ac7b2d4a87d6514c87b5bc17d6fb5`
- Fixed-point report SHA-256: `c7ac36eeedce0aa4c2db75bfbd1a29eefd6ad064aa0ece779b7489b98c2fb3ab`
- Active-build audit JSON SHA-256: `d4861b90f1403e65531ba0c0bb1d25d2041f9d5e4905da2df9c32dadec0c4b15`
- Active-build audit summary SHA-256: `00fa7c7d15ac3274eda9076df24ac477aa7a019c62ed13d88e126fa0207f470f`

## Rejected alternatives

Using a host linker to produce PE32 would test the format but leave Windows
image authorship outside CupidLD.

Checking the existing host-built Windows commands into the seed would confuse
an oracle with a self-hosted artifact. They still depend on Clang and its
Windows linker.

Adding imports, runtime relocation, or a Windows runtime in the same change
would combine independent boundaries. The fixed layout first proves that
CupidLD owns PE32 headers, sections, addresses, and deterministic bytes. Later
work can extend the same operation when the runtime and native seed need those
features.

## Consequences

Source-head CupidLD can author one deterministic fixed-layout i386 PE32 image
without a host linker. The ordinary ELF serializer remains the only production
path and still builds the Linux seed, kernel, Toolchain contracts, and user
programs.

No PE image is executed, promoted into the checked seed, or used by a normal
build. Imports, runtime relocation, a Cupid-built Windows runtime, native seed
carriage, and a native Windows fixed point remain open.

The publication path covers source-head ELF output as well as PE output. The
promoted seed does not carry it.

This change transfers no artifact ownership. The graph stays at 447
transforms, with CupidC at 245, CupidASM at five, CupidObj at 189, CupidLD at
five, CupidDis at one, and Python at 447. All 438 root outputs retain a Cupid
owner, and the three Python-only supplemental outputs remain. This work changes
no ordinary C or assembly owner, so it requires no `.c`-to-`.cc` rename.
