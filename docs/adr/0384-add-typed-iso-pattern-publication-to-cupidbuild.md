# ADR 0384: Add typed ISO pattern publication to CupidBuild

Source-head CupidBuild provides `assemble-iso-pattern` for
`test_iso/big_pattern.asm`. Feature 17 uses its 4,096-byte output to check reads
across ISO sectors. The command keeps that exact artifact contract inside the
existing guarded assembly transaction, alongside the bootloader and SMP
trampoline contracts.

CupidBuild freezes the source and complete six-tool seed, asks checked
CupidASM for a private binary and raw-map v2 file, and verifies every output
byte against sixteen repetitions of `00` through `ff`. The complete map must
declare base zero, no control edges, and one data range at offset zero. Checked
CupidDis validates the pinned image and map before publication. The common
transaction rechecks live inputs, candidate, map, output, parent, and owner
lock. A failed check preserves the previous output. Equal bytes keep its
timestamp through the existing `publish_if_changed` path.

The source stays `times 4096 db $`. CupidASM already supports its required
directives and reevaluates `$` for each emission. ADR 0227 records why this
source has an explicit exception to the optional NASM byte oracle. Replacing
the source with a literal byte table would obscure that maintained contract.

Both fixed-point matrices include a deterministic ISO pattern fixture and a
wrong-pattern rollback case. Their definitions become 32 failure, seven help,
and 38 success groups on Linux, and 20 failure, seven help, and 25 success
groups on Windows. These definitions require a later clean paired replay;
they do not establish seed carriage.

Six focused CLI cases pass on Linux and native Windows with retained
source-current seed candidates. They cover the active source, both other raw
artifacts, rollback, map restrictions, locking, and aliases. Checked CupidC compiles the
changed sources, CupidLD links both hosted targets with verified support
objects, and strict CupidDis inspection accepts both images. The same six
cases pass through both Cupid-built images; the Windows image also passes the
raw option contract. The complete audit mutation
test also passes, including removal of the ISO fixture, byte check, and
specific failure diagnostic. The bootstrap log records the exact evidence.

The CLI tests accept `CUPIDBUILD_TEST_SEED_MANIFEST` to select a complete,
host-matching candidate cohort. If it is unset, they use the normal checked
manifest. Only the two new ISO publication and size/bytes/layout cases skip
when that selected manifest names source revision
`0232cb57aad5d6bdfd7bd77499762514b2f0ebfd`, whose assembler rejects
`--caller-owned-output`. The skip names the required candidate selection.
Lock, alias, help, and option checks remain active. A newer selected revision
runs both publication tests without a skip; the transition can be removed
after the repaired seed promotion.

From the repository root, the retained candidate cohort can be tested on
Windows with:

```powershell
$env:CUPIDBUILD_TEST_SEED_MANIFEST = (Resolve-Path 'build/bootstrap/iso-pattern-validation/seeds/i386-windows/manifest.json').Path
python -m unittest discover -s tests -p test_toolchain_cupidbuild.py -k iso_pattern -v
```

On Linux:

```sh
CUPIDBUILD_TEST_SEED_MANIFEST="$PWD/build/bootstrap/iso-pattern-validation/seeds/i386-linux/manifest.json" python3 -m unittest discover -s tests -p test_toolchain_cupidbuild.py -k iso_pattern -v
```

Use the corresponding manifest path when testing another complete candidate
cohort. Selection changes only the test input; CupidBuild still validates the
manifest, image membership, hashes, and execution profiles.
The selected candidate command passes all three ISO cases on each host.
With the original `0232cb57` manifest and the variable unset, the default
checks retain exactly the two documented skips and still run the lock, alias,
help, and option cases.

This change adds source capability. The checked seeds do not yet contain this
command, and the normal `big.bin` recipe remains on Hostbuild. Seed promotion
and the production handoff remain separate steps. No C translation unit changes
owner, so no `.c` rename is due. `TempleOS/` remains reference material.
