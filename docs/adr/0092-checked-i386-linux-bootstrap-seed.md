# ADR 0092: Check in the static i386 Linux bootstrap seed

## Status

Accepted on 2026-07-23.

## Context

ADR 0090 established a five-tool fixed point, but its first generation still
came from a native compiler and linker. A clean checkout could prove that two
Cupid-built generations matched only after GCC or Clang had produced the
starting tools.

A useful seed needs more than five executable files. The repository must know
which source revision produced them, which tools took part in the build, which
source and link plan they implement, and which target ABI they expect. The
bootstrap must reject damaged or substituted binaries before execution. It
must also detect source edits made while a long staged build is running.

## Decision

The repository carries one generation of the five static tools under
`bootstrap/seeds/i386-linux/`:

| Tool | Bytes | SHA-256 |
| --- | ---: | --- |
| CupidASM | 433,060 | `00F684CA5CA1E2BA36763E6810C65FEA8B3786D40F6008D635751A1F2C2B6DB0` |
| CupidDis | 366,968 | `67FCDBCF8A7924E37F00EC571BB5A4DBFBF4897C9743E9F3A3BBCAF0EA20CA60` |
| CupidLD | 262,388 | `373ED96803DCFB0005B8B3B1D49CA1313396EE11E17521AAD6402F487CDD97E5` |
| CupidObj | 182,704 | `1F48C3D7B5F80D3E33EB9268C087111E8FA54EB390C24368A09F7EC2981C0030` |
| CupidC | 1,812,712 | `29CD222C6E33590932457D36F3728705134C8C6750947E7CFBC4ABA3B7C5500B` |

The manifest records the target as static little-endian i386 Linux with the
repository `int 0x80` ABI and entry point `0x08048000`. It records source
revision `d5e4ed784c54ea8dad581ac736ee8b62553627d8`, the generation-zero producer
lineage, the passing fixed-point command, all 19 C inputs, startup, include
arguments, the producer trio, bounded worker count, and all five link orders.
A digest protects the complete build plan.

Verification requires the exact schema, target, provenance, build plan, source
mapping, file set, sizes, and SHA-256 values. Unknown fields and a reordered,
duplicated, or substituted source plan are errors, even if a changed manifest
contains a matching plan digest. Duplicate JSON keys and type substitutions
between numbers and Booleans are also errors. Every file must be a regular,
listed ELF32 `ET_EXEC` image for i386. It must be static, have the declared
entry point inside file-backed executable bytes, contain no interpreter or
dynamic section, and have no writable executable load segment.

`tools/bootstrap_toolchain.py bootstrap` reads and verifies the manifest and
five binaries once, copies the binaries into a private input
snapshot, and uses only that captured data for the run. Its CupidC, CupidASM,
and CupidLD build stage two from the current 40 source inputs. The stage-two
producer trio builds stage three. The comparison covers all 19 C objects,
startup, and all five linked tools. Both stages run five help checks, ten
successful operations, and six useful failure cases.

The bootstrap captures every C source, included project header, startup file,
and `link.ld` before execution. It checks that snapshot after each stage and
after the behavior suite. The report keeps the seed's source revision and
captured manifest hash separate from the current source snapshot hash, and it
records every current input hash.

Linux runs the static seed directly from a private executable copy. Windows
stages each tool into a mode-0700 directory created by `mktemp` inside WSL.
Neither path changes the checked binaries. `make verify-bootstrap-seed`
performs validation only. `make bootstrap-from-seed` writes stage artifacts
and `bootstrap-report.json` under `build/bootstrap/checked-seed` by default.
The output directory must not contain an earlier staged result.

## Alternatives considered

Rebuilding the seed during an ordinary bootstrap would restore the dependency
on a host compiler. The checked binaries are therefore intentional source
inputs, not generated build output.

Trusting file names or Git transport alone would not bind an executable to its
ABI or build plan. The manifest and ELF checks fail before any seed process
runs.

Comparing only the five final executables would hide a compiler that changed
an intermediate object and later canceled the difference. Every object and
startup file remains part of the fixed-point comparison.

Hashing sources only once would allow a concurrent edit to produce a mixed
generation. The bootstrap repeats the source snapshot check at each boundary.

Verifying the live seed files and reopening them during the build would leave
a replacement window. The bootstrap instead keeps the decoded manifest, its
hash, and private copies of the five verified byte streams for the whole run.

Calling the seed source revision the current revision would blur two separate
facts. Reports retain both the historical producer revision and the exact live
input snapshot.

## Consequences

A clean checkout can now verify and rebuild the complete static i386 Linux
Cupid Toolchain without invoking GCC, Clang, a native linker, NASM, `nm`, or
`objcopy`. The final post-review run rebuilt all 19 C objects, startup, and
five tools across two generations in 503.357 seconds. Every compared artifact
matched byte for byte, and all 21 behavior checks passed. Its 40-input snapshot
hash is `7BEB7844F46172B7600193ABF66F5FE69A7F42FF91C023EB3EE8FE6A19BD2C11`;
the captured manifest hash is
`CA9C1CCA497717317FFDC7AF3F22EB330074B1FD5656EC4625C379363BB80412`.

The seed does not make the normal Cupid OS image build independent of the host
C toolchain. Native contract runners and hosted development commands also
remain host-built. The seed is Linux ABI software; Windows uses WSL rather than
a native Windows executable.

Future seed replacement must preserve strict provenance, update the manifest
and hashes together, pass the complete staged comparison and behavior suite,
and document the new trust transition. The existing seed remains a reviewable
recovery point rather than an opaque rolling binary.
