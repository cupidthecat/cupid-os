# ADR 0269: Run Cupid-built CupidLD on Windows

## Status

Accepted on 2026-08-13.

## Context

ADR 0268 gave native Windows CupidASM, CupidC, CupidDis, and CupidObj a shared
startup and hosted runtime. Those tools use the same heap, stream, file,
directory, and diagnostic surface. CupidLD could already be linked as a PE and
print its help text, but its normal output path needed four more Windows
operations:

- create an adjacent candidate exclusively;
- flush the candidate before closing it;
- replace the destination atomically;
- delete a failed candidate.

The `_WIN32` driver also uses `_fullpath` before it creates a candidate. The
checked hosted declarations did not provide `_fullpath`, a minimal `windows.h`,
or cdecl bridges for the four publication calls. Rewriting the driver around
plain `fopen` would have removed its collision handling, verification, and
replacement guarantees.

## Decision

Keep the shared twelve-import runtime unchanged for the first four native
tools. Give CupidLD two small, link-local publication objects and four extra
`KERNEL32.dll` imports:

- `DeleteFileA`;
- `FlushFileBuffers`;
- `GetFullPathNameA`;
- `MoveFileExA`.

`publication_start.asm` exposes cdecl entry points and performs the stdcall
calls through CupidLD-authored IAT cells. `publication_runtime.cc` implements
the `_fullpath` path used by CupidLD with `GetFullPathNameA`: a null output
pointer requests one allocation of the required size. The narrow adapter also
rejects invalid inputs and contains caller-buffer and Windows-error handling,
but those branches are not part of this native behavior proof.

The checked `windows.h` contains only the types, constants, declarations, and
name mappings used by the unchanged CupidLD driver. Host compiler builds still
use the system header. The CupidC `HOSTED_I386_WINDOWS` profile now covers
`cupidld_main.cc` and the publication runtime in addition to its four existing
roots. The Linux profile and every Linux fixed-point object remain unchanged.

Both compiler stages build the Windows CupidLD main, publication runtime, and
publication bridge, then link and compare the complete PE. The image has the
shared twelve imports plus the four publication imports. The native check:

- compares `--help` with the checked Linux tool;
- links a PE from relative input and output paths;
- starts with an existing destination and an occupied candidate suffix;
- requires exact byte parity with the stage-two reference PE;
- confirms that the occupied candidate was not touched;
- forces replacement to fail by using a directory as the destination;
- requires exit 1, the `cupidld: link failed (io)` diagnostic, and no leftover
  candidate.

The fixed-point source closure now contains 50 files. The Toolchain contract
publisher owns 65 inputs. The build-graph audit binds the publication source
tokens, assembly bridge, paired compile and assembly commands, import and
object maps, native workloads, reference/output data flow, exit codes, and
cleanup result. The report contract pins the native loader record and exact
artifact set independently.

## Evidence

The poisoned-host checked-seed bootstrap completed in 902.792 seconds.
All 19 Linux C objects, Linux startup, and five Linux tool images match between
stage two and stage three. All five promoted Linux seeds still match stage two,
and the existing 5/18/16 behavior matrix passes.

The 50-input source snapshot has SHA-256
`76bb7c1cc63c44d29d0f062af0a714e1855632da7db13ff8652f6a897a2931a4`.
The 38,162-byte report has SHA-256
`d90cf63e19ed1b4af560e4c15660d0583a1591bccdaa75157432204a82079efd`.

The native CupidLD image is 296,448 bytes with SHA-256
`7799324d179cf0d5862d4bdfa9df865cac35fac0f8c2ec565ae9c060812db03a`.
Its Windows help, successful link, and failed publication return 0, 0, and 1.
The successful output is the 32,256-byte runtime-contract PE with SHA-256
`df61f3a830d26fe47761cd1d927ca7f77b80a8788bf33e308a7d7f997a11eeec`.
The occupied candidate remains eight bytes with SHA-256
`20323a24be105b1b519962994b8e4e6a7f8e3cd0d005b8ee10c9aeb66da5d40a`,
and the failed path leaves zero candidates.

Both stages reproduce these supporting objects:

| Object | Bytes | SHA-256 |
| --- | ---: | --- |
| Windows CupidLD main | 29,752 | `a5a645141c05d61ca755927aced70922089c1404fd5bd977e524cb7c7eed4cb6` |
| Publication runtime | 2,152 | `d91f2f8f6faf1afcf4a0205f69cfab33584863697a3f9b003779a12d1466d040` |
| Publication bridge | 808 | `b2a60c95ca0a1942a2bb2efce051e3f7fec2bf8b5ae8e5a940b5786cf41a6dd5` |

The focused native boundary test passed in 127.863 seconds. The complete
behavior matrix passed against the preceding byte-identical stage pair in
125.0 seconds. The fail-closed fixed-point audit and all 36 Toolchain contract
plan tests passed together in 105.763 seconds.

### Failed paths

The first focused build stopped at `#include <windows.h>` because the checked
hosted declarations deliberately had no Windows header. Adding a narrow owned
header exposed the existing driver without importing a host SDK.

The first generated-audit run rejected the publication runtime as an unexpected
Toolchain closure root. The closure calculation covered Linux, GNU, and
contract roots but not a source unique to the Windows profile. It now adds only
Windows roots that are not already represented by the Linux closure.

The first source-freeze check treated `windows.h` like an explicitly listed
Windows source. Headers enter the frozen closure through the checked header
glob, while the contract publisher names them directly. The audit now checks
those two inventories separately instead of inventing a second explicit header
path.

## Rejected alternatives

Adding the four publication calls to every Windows tool would enlarge the
other four PEs even though they do not use them. The extra imports and objects
stay local to CupidLD.

Using a host import library or C runtime would make the result depend on a host
compiler or linker. Every object and IAT entry remains Cupid-produced.

Publishing directly to the destination would make a failed link destructive.
CupidLD keeps its existing exclusive candidate, verification, replacement, and
cleanup sequence.

Promoting a native Windows seed would claim a stronger boundary than this
change proves. Seed carriage, WSL removal, and normal-build adoption remain
separate work.

## Consequences

Cupid tooling now builds and runs all five hosted commands natively on Windows:
CupidASM, CupidC, CupidDis, CupidLD, and CupidObj. Each image is reproduced by
both fixed-point stages and has useful positive and negative native behavior
evidence.

The checked seed is still the static i386 Linux five-tool cohort. Windows still
uses WSL to execute that seed, and Host Python still coordinates, validates,
and publishes the bootstrap. The native PEs are source-head evidence rather
than normal-build inputs.

No OS or tool behavior was removed. The added C-family sources use `.cc`, so no
`.c` rename is due. `TempleOS/` remains read-only reference material.
