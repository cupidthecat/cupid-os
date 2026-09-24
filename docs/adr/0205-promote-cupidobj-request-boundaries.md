# ADR 0205: Promote CupidObj request boundaries

## Status

Accepted on 2026-08-01.

## Context

Production review found two gaps in the checked CupidObj `install-source`
contract. The 512-path guard covered only demos, and the docs emitter grouped
mixed home assets by extension instead of keeping caller order. Revision
`a32d1cc0f655cd0e161fc5bac8ead54f4586423e` fixes both paths and updates the
Python oracle. That revision was committed and pushed before the candidate
seed was built.

The 19-source build plan remains unchanged at SHA-256
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.

## Decision

Promote the five stage-three tools as one checked cohort. Four images remain
byte-identical to the preceding seed. CupidObj changes as follows:

| Tool | Bytes | SHA-256 | Producer |
| --- | ---: | --- | --- |
| CupidObj | 245,220 | `e9631e8b9377a17497bc87418c56282d97f91b8d1cd43e4670130e5e54334747` | no |

The manifest keeps the other four image records unchanged and binds the
complete cohort to the pushed revision above. It is 5,440 bytes with SHA-256
`906abf16651775fe4ab52c13652a19b2e36d816ad506c08f05fbcc3264c5576b`.
CupidASM, CupidC, and CupidLD remain the producer tools. CupidDis and CupidObj
remain checked outputs.

## Evidence

The transition bootstrap completed in 646.6 seconds. It froze 41 source
inputs with SHA-256
`eefdb24a987176ebb79a9407f45dcb3d02b803364a1450048678bb3aafa126cd`.
All 19 C objects, the startup object, and all five tool images matched between
stage two and stage three. Both stages passed five help cases, ten successful
operations, and six useful failures. Only the preceding CupidObj image
differed from stage two. The 15,054-byte transition report has SHA-256
`db2c62ea8de385847ee14ad57f006fac781168d99b6db6288526077638c17470`.

After promotion, `make verify-bootstrap-seed` passed. A checked-seed
regression preserved PNG, JPEG, BMP, JPG, and BMP caller order, rejected 513
bin paths, and left the earlier output intact. It passed in 0.768 seconds.
The complete checked-seed module passed all 37 tests in 712.199 seconds.
All eleven hosted CupidObj tests passed in 2.977 seconds, and all 39
production tests passed in 27.309 seconds. The 195-input generated frontier
passed in 25.4 seconds with digest
`fb526be4b4388ecd62ed54b8321b043ef483fd3907c998dc7e062ab6ffef39ea`.
Its three generated source and object pairs remain byte-identical to the
pre-promotion outputs.
The full 68-test build-graph audit passed in 589.740 seconds.
The complete Toolchain target passed in 2,755.8 seconds. Both checked stages
compiled and linked the 20-artifact contract cohort byte-identically, and the
hosted runtime contract passed.
A normal root build passed in 1,428.5 seconds. The resulting 8,719,780-byte
kernel ELF has SHA-256
`b3964b134e777ca73bcd5c87e504efe5ae01cf9d31a5ad7d2d476c28cdd941cf`,
and its 8,517,944-byte flat image has SHA-256
`0fd1d09d451a13f14e6c396e6dd32b28b376c0a68fe2826768ac2727b307bae2`.
A private-image boot reached the desktop and terminal, then ran `ls` through
the in-OS JIT without a panic in 47.5 seconds.

The post-promotion bootstrap completed in 650.5 seconds. Every checked image
matched stage two, then all 19 C objects, startup, and five images matched
between stage two and stage three. The complete behavior matrix passed again.
The 15,053-byte report has SHA-256
`9c1fa329855aa1a3a4e68e5b17dc7fac95b07905c1817fce80ad58f25847d92a`.

## Rejected alternatives

Keeping the preceding seed was rejected because the production command would
retain the two reviewed contract gaps.

Replacing CupidObj without rebuilding the complete cohort was rejected. The
manifest is one trust unit, and a stage-three fixed point is required even
when only one image changes.

Recording an unpushed source state was rejected. The manifest names the exact
pushed revision that produced the promoted image.

## Consequences

Normal installation-table generation now runs a checked CupidObj that applies
one overflow-safe 512-path total before mode dispatch and preserves caller
order for mixed home assets. The active table bytes do not change because the
current inventories already satisfy both rules.

Python still coordinates the staged bootstrap, and Windows still runs the
static i386 tools through WSL. Those host dependencies remain open.
