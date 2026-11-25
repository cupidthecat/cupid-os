# ADR 0390: Compile closed kernel inputs with CupidBuild

Accepted on 2026-09-20 as a source capability. The checked seeds and normal
Make recipes retain their existing ownership.

The next compiler coordinator needs to preserve logical include paths while
compiling captured bytes. CupidC now accepts a closed source bundle through
`--source-bundle FILE`, together with `--root ROOT`. The bundle replaces all
source reads for that invocation. `--root` still selects the output location.
Nested includes, ordered include roots, forced includes, and `__FILE__` retain
their logical identities. A missing bundle entry never reads the live root.

The earlier investigation proposed a retained private directory tree. A closed
file store supplies the same compiler input semantics without adding nested
directory creation, identity tracking, and deletion to the publication adapter.
CupidBuild already freezes ordinary files and provides a retained private byte
stream. It now serializes those captures into that stream and gives CupidC the
bundle path. The compiler reads the complete bundle once before preprocessing.
The coordinator remains responsible for file identity, capture, and live drift.

## Bundle format

`CUPSRC1` consists of the eight bytes `CUPSRC1\n`, a little-endian uint32 file
count, and consecutive file records. Each record contains a little-endian
uint32 path length, a little-endian uint32 content length, the path bytes, and
the content bytes. There is no padding or terminator. Paths are canonical
absolute ASCII logical paths in strictly increasing byte order. Empty
components, `.` and `..`, controls, backslashes, colons, duplicates, and trailing
data are invalid. A path may contain at most 4,095 bytes. The complete bundle
may contain at most 512 files and 64 MiB, including its framing. Empty files
are valid. Source bytes are otherwise opaque to the bundle parser.

The parser validates every record before compilation. Its file-store callbacks
read retained memory and delegate only output writes to the native adapter.
An output path that names a bundled input fails. Failed parsing or compilation
leaves the previous output intact. The bundle is a transport format, not a
signed manifest or an independent provenance proof.

## Compiler transaction

`cupidbuild compile-kernel --seed-manifest MANIFEST --root ROOT --source SOURCE
--output OUTPUT` admits the eleven existing frozen kernel closures recorded in
`tools/cupidc_kernel_compile.py`. Its native table retains every source and
header, and an executable contract compares the complete table and fixed
compiler argument vector with the existing wrapper. The output must be the
source's corresponding `.o` path. Other sources, Doom profiles, and caller
compiler flags are outside this command.

The transaction freezes the complete source closure and six-tool seed,
constructs the source bundle, and runs checked CupidC under the unchanged
kernel profile. Generated `ksyms_data.cc` retains its 600-second tool deadline;
the other ten sources retain 180 seconds. The candidate must pass Cupid's ELF
reader and the compiler's relocation policy before publication. Data-only
objects are valid. Required metadata tables, ASCII section names, payload
alignment, symbol bounds, and REL targets remain checked. RELA, unsupported
relocations, relocations against NOBITS, and PC-relative addends other than
`-4` fail. Absolute relocations may retain subobject addends.

The existing owner lock, pinned output parent, candidate identity, seed
membership, live-input checks, and rollback apply. A new post-install header
mutation test exposed a gap in the shared publication boundary: it rechecked
discovered directories but not captured file contents after installation.
Every full publication-boundary check now rechecks both. Rollback's separate
namespace-only check still avoids treating a recoverable source change as
namespace interference.

## Migration boundary

The promoted compiler does not understand `--source-bundle`. Running this
transaction with that seed therefore fails and preserves the previous object.
The executable contracts build a current CupidC target image and substitute
it only in private test cohorts. They also link a current CupidBuild target
image with the checked tools and run successful and failed compile transactions
through it on Linux and Windows. No production seed is replaced.

Paired fixed-point behavior coverage, clean seed promotion, and normal Make
adoption remain necessary before these eleven recipes can stop using Python.
Production counts remain 197 CupidBuild and 255 Python participations, including
240 Python-coordinated kernel and Doom compilations. All eleven admitted sources
already use `.cc`; this change requires no suffix migration. TempleOS remains
reference material only. Test outcomes and failed probes belong in the
bootstrap log.
