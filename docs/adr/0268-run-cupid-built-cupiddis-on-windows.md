# ADR 0268: Run four Cupid-built tools on Windows

## Status

Accepted on 2026-08-13.

## Context

ADR 0248 proved that CupidC, CupidASM, and CupidLD could produce a small PE32
program that Windows could load. The program printed one fixed line. It did
not have arguments, a heap, named files, or useful error reporting. The
checked producers still ran as static i386 Linux programs through WSL.

A useful hosted tool needs the normal driver surface: Windows argument rules,
allocation, named files, seeking, separate standard streams, current-directory
lookup, and public exit codes. CupidDis offered a small first closure and a
direct parity oracle. Once that closure worked, CupidASM, CupidC, and CupidObj
needed no additional operating-system calls. Their platform-sensitive host
adapters and driver mains did need to be compiled under a real `_WIN32`
profile.

CupidLD has a different boundary. Its publisher also needs exclusive file
creation, a durable flush, atomic replacement, and deletion. Those calls are
outside the shared runtime added here.

## Decision

Keep one hosted runtime implementation and select its operating-system edge at
compile time. `toolchain/hosted/i386-windows/runtime.cc` defines
`CUPID_RUNTIME_WINDOWS` and includes the shared implementation from the i386
Linux runtime. The Linux path keeps its existing system calls, layout, and
allocator. The Windows path uses repository-owned cdecl bridges for these
`KERNEL32.dll` procedures:

- `CloseHandle`
- `CreateFileA`
- `ExitProcess`
- `GetCommandLineA`
- `GetCurrentDirectoryA`
- `GetLastError`
- `GetStdHandle`
- `ReadFile`
- `SetFilePointer`
- `VirtualAlloc`
- `VirtualFree`
- `WriteFile`

`tool_start.asm` obtains the command line and calls the shared runtime entry.
It reserves twelve bytes before each one-argument cdecl call. ESP is therefore
16-byte aligned at every call site, and a CupidC callee enters with the ABI
residue assumed by the emitter. The bridges call imported stdcall procedures
through CupidLD-authored IAT cells and remove the API arguments before
returning to CupidC code.

The runtime parses the Windows command line into one allocation that holds the
pointer table and writable argument text. It implements the quote and
backslash rules needed for spaces, embedded quotation marks, and trailing
backslashes. It initializes stdout and stderr from distinct handles before
calling `main`.

Each heap request uses `VirtualAlloc` and is released with `VirtualFree`.
`realloc` copies the old payload into a new region. The file layer maps the
represented `fopen` modes onto `CreateFileA`, uses `ReadFile` and `WriteFile`
for unbuffered streams, and uses `SetFilePointer` for `fseek` and `ftell`. An
append stream seeks to end before every write, so a caller cannot defeat
append semantics by seeking first. `GetCurrentDirectoryA` supplies `getcwd`.
Common Windows file, path, handle, and memory errors map to the runtime's
existing `errno` values.

Add `HOSTED_I386_WINDOWS` as a strict i386 profile. It defines `_WIN32=1`,
uses the checked hosted declarations, and compiles `ctool_host.cc` plus the
CupidASM, CupidC, and CupidObj driver mains. CupidDis has no conditional main,
but it uses the separately compiled Windows host adapter. Core objects remain
shared with the Linux fixed point.

Both compiler stages build and compare native CupidASM, CupidC, CupidDis, and
CupidObj images. Windows runs help, a useful successful operation, and a
useful failure for each tool. CupidDis also disassembles a quoted two-byte
input and must match the checked Linux tool byte for byte. A separate native
runtime contract covers `calloc`, non-null `realloc`, allocation overflow,
named-file write and append after seek, reads, `fopen_s` errors, `getcwd`
errors, embedded quotes, and a trailing backslash. Bad argument parsing must
return 41. The older exit-37 loader probe remains independent.

The fixed-point harness freezes the runtime, startup, `direct.h`, and direct
runtime contract with the older inputs. The source closure now has 47 files,
and the Toolchain contract publisher has 62 inputs. The build-graph audit owns
the four-tool Windows profile and exact native workloads.

## Evidence

The complete checked-seed bootstrap passed in 871.1 seconds. All 19 Linux C
objects, Linux startup, and five Linux tool images match between stage two and
stage three. The existing 5/18/16 behavior matrix also passes. All five
promoted Linux seed images match stage two.

The 47-input source snapshot has SHA-256
`976fca9ccef9a759151ea4cf544f17f3c303ef60fc3ad2207eda18857261d9c4`.
The 32,681-byte report has SHA-256
`d3608ab66f6781780ba3fe68eb3c5814248d1903d65f50651c8950ca46dda1e4`.

| Native image | Bytes | SHA-256 | Windows exits |
| --- | ---: | --- | --- |
| CupidASM | 433,664 | `c93a296e04a7a5bb9706ec7d360040a2cdc288941340e76941d9629049c8ce3a` | help 0, success 0, failure 1 |
| CupidC | 2,593,792 | `ed6e667bd96f839c3bc9f55eb62e60bab8462f8c1c53d2ae36458134acc37def` | help 0, success 0, failure 1 |
| CupidDis | 378,368 | `e4f20cd46344a4a68914389187d2cc9fbf3653e9ca8d0e56119d40bab17eed49` | help 0, raw 0, missing 1 |
| CupidObj | 375,808 | `46ab2b19fc99bf7ee4856ae6f71a397668fb33bbd0da38535728d00a57a57924` | help 0, success 0, failure 1 |
| Runtime contract | 32,256 | `df61f3a830d26fe47761cd1d927ca7f77b80a8788bf33e308a7d7f997a11eeec` | success 0, bad arguments 41 |

The successful native outputs are also pinned. CupidASM writes 4 bytes with
SHA-256
`e26807846248e3d1ea2d9dc0980c4329e7b4638db148879849c725e57de64559`;
CupidC writes 364 bytes with SHA-256
`8ba6e2f7ca3af67775dfdd350767e737fcf66dd9a1d8fececbdce756df7ced37`;
and CupidObj writes 452 bytes with SHA-256
`a4950b4f13759a63540da33f08b584e804b6fb4f98afaa97a82e3d0a9191c35a`.
CupidDis produces a 56-byte report with SHA-256
`7730fe73e97c921fae17e167e6960bb0189fee47de4fddc943117520ad82e6ac`.
The runtime contract leaves `headtail`, eight bytes with SHA-256
`87c2aebe999878ed1c244b6a85d1a2ad0b5c6f0916afed00797c1bc7d6097961`.

The shared Windows host adapter is 7,084 bytes with SHA-256
`42af2613b436e9783e6b27d0c11a4152923297f548a803e30affdd84f57f3c0b`.
The runtime object is 28,132 bytes with SHA-256
`04c0b5cb979d54803787baaf689ba139b5ab26238851f43959e0328af7de6d2e`.
The startup object is 1,656 bytes with SHA-256
`f4c73984c249a6e10945417b7fb2a1777b2cbb5ebabaf644e678f78fdc4207e6`.
Both stages reproduce all three objects.

The Linux runtime and platform-sensitive Linux driver objects remain
byte-identical to the preceding fixed point. The Linux runtime is 26,404 bytes
with SHA-256
`e1e9258006a871b2bff2707580b9a110a1e673959b817bc29dfe3d2927bbe1e2`.
The unchanged Linux object sizes are 6,944 bytes for `ctool_host`, 12,384 for
the CupidASM main, 22,748 for the CupidC main, and 38,120 for the CupidObj
main.

### Failed paths found during review

The first focused run failed because the Windows runtime source did not yet
exist. A later native runtime-contract run failed after 107.781 seconds. Its
unquoted trailing backslash consumed the following separator and merged two
arguments. Rechecking whitespace after each backslash run fixed the parser;
the corrected focused run passed in 89.669 seconds.

The initial startup aligned ESP, called zero-argument `GetCommandLineA`, then
pushed the runtime argument without caller padding. That shifted every
compiler-aligned call made by the runtime by four bytes. Adding twelve bytes
of caller padding restored the documented cdecl alignment.

The first audit used raw source fragments and unrestricted AST walks. A dead
block, comment, empty iterator, early return, or self-referential comparison
could preserve the expected text while removing the proof. Report tests also
read some expected loader values from the report being tested. Review
mutations demonstrated each false pass. The audit now binds normalized token
digests for every direct runtime-contract function, exact AST fingerprints for
the three native execution blocks and image helper, live loop ownership,
fixed workloads, distinct reference and output paths, exit codes,
diagnostics, and sentinel preservation. Loader tests pin expected values and
artifact key sets independently. Fourteen control-flow, workload, and
manufactured-evidence mutations now fail closed.

## Rejected alternatives

Linking a host C runtime would provide arguments and file I/O, but it would
leave every native tool dependent on a host compiler, import library, and
linker. The new executables contain only Cupid-produced objects and imports
selected by CupidLD.

Shrinking CupidDis into another marker probe would avoid the real driver,
arena, decoder, path handling, and diagnostics. The unchanged tool closure is
built and exercised instead.

Copying the Linux runtime into a separate Windows implementation would leave
two large copies to maintain. The small Windows source selects one shared
implementation, while the operating-system calls stay behind a visible
compile-time boundary.

Promoting a Windows seed in this change would overstate the result. CupidLD
still lacks its publication calls, and none of the four PE images is carried
by the checked seed.

## Consequences

Cupid tooling can now produce and run native Windows CupidASM, CupidC,
CupidDis, and CupidObj without GCC, Clang, NASM, a host linker, or a host C
runtime. The shared runtime supplies the driver surface used by those four
tools.

The normal build still uses the checked static i386 Linux seed. Windows still
uses WSL to run those producers, and Host Python still coordinates the fixed
point. CupidLD needs native exclusive creation, durable flush, atomic
replacement, and deletion. Checked Windows seed carriage, a native five-tool
fixed point, WSL removal, and production adoption remain open.

No active source was reduced to fit the compiler. The new C-family sources use
`.cc`, so no `.c` rename is due. `TempleOS/` remains read-only reference
material.
