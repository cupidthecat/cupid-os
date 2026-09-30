# Promoted long-file seed release

The checked Linux and Windows six-tool cohorts come from source
`8403b0a82b5693409d2242fdbff32688c8f2cac5` and the same committed
77-input snapshot
`25b05a6cb0e824136db8df79b4e7f2e176aea88f0446be550f36ece04798bbf9`.
The captured parent generation is the complete paired `5ba6ea24` release.
Linux manifest SHA-256 is
`84b8bef11969bac58d69e97baacd86d8f1b4aa030ecd25359bb1dcdb8f679cbc`;
Windows manifest SHA-256 is
`5d129b2575450dac756d75a4dc859501fdcd9bacf53190ed360ec66f68e21297`.

This release carries the retained output-parent preparation and Windows wide
file operations needed by configurable user outputs. The long-file profile
adds its resolver shim and exact import selection. Default producer capture
remains 76 inputs; the installed profile captures 77. Hosted publication
captures 89 in either mode. Historical profiles keep their own exact plans,
counts, parents and imports. Long file arguments do not establish support for
long child working directories or arbitrary filesystem aliases.

## Acceptance

Independent checks bind the named commit, every producer input, captured
parents, exact plans, executable formats, stage-three/four bytes and behavior.
Linux compares 34 artifacts per stage and passes 47 failure, seven help and
55 success groups. Native Windows compares 42 artifacts and passes 35 failure,
seven help and 42 success groups. Both proposed seeds reproduce all twelve
tools under the long profile. A complete default Windows bootstrap passes
with its separate 76-input plan and stage pairs.

Both complete fresh OS builds run with host code-producing tools forbidden.
All 431 link inputs, sixteen artifacts and disk images match. The earlier settled
63,220-byte manual produces a 9,576,256-byte raw kernel. Both hosts pass the
three unchanged user programs, native artifact checks and private four-CPU
`max`/e1000 disassembly, shell completion and SMP smoke. The image remains
unchanged through each smoke; its SHA-256 is
`ce64fd631010ff723af7840d77ace5eea3788cfe66be2be99a0ab7181ea21e83`.

The corrected Linux user ABI recipe produces a fresh complete publication.
Its current control inputs and 77 producer inputs are authenticated before
copying all 23 publication files to a new Windows directory. Native Windows
then verifies all 22 artifacts against the current 89 inputs. Source inputs
and copied files are rechecked afterward. The source roots retain all 1,540
captured repository inputs throughout these acceptance runs.

The first copy harness omitted the absent `toolchain/build` parent and failed
before copying any publication file. The corrected harness prepares that
parent and preserves exclusive creation of the publication directory. An
early verifier launch also stopped before execution because the copy record
did not yet exist. Both unsuccessful harness attempts remain separate from
the successful publication and tool verification.

## Final promotion manual

The final 63,444-byte manual passes normal Make kernel-target replays on both
hosts with host code-producing tools forbidden. The raw kernel is 9,576,480
bytes, the first-pass ELF is 9,675,196 bytes and the final ELF is 9,806,268
bytes. Native size checks reject each obsolete raw/final-ELF value before those
two measured rows change. The other fourteen policy rows remain exact.

Each replay then executes the normal image recipe with its accepted defaults,
checks the embedded boot code and complete raw kernel, validates the three
unchanged user programs and passes the private four-CPU smoke. All sixteen
artifacts, 431 link inputs and images match across hosts. Only the manual object
and the two linked ELFs differ from the preceding link cohort. The images remain
unchanged through smoke; final image SHA-256 is
`af2a2c537facca3aa5ac4a38af47685f82873e6c58a960c24a979621893b3165`.
The final captures retain 1,541 source/control inputs, including this release
record. This replay does not claim another full toolchain bootstrap.

The installed regression selection passes 253 methods. Twenty-five manifest
methods also pass after historical fixtures receive explicit historical parents
and current fixtures expect the long-profile result tag. Old fixture inheritance
cannot redefine the historical profile during promotion.
The final 79-method manifest and size-policy replay also passes after the two
kernel size rows change, with all six captured inputs unchanged.

## Reproduction and evidence

Run explicit long-profile bootstrap commands from a supported short working
directory, using new output directories:

```sh
python tools/bootstrap_toolchain.py bootstrap --root . --manifest bootstrap/seeds/i386-linux/manifest.json --windows-long-paths --output build/recheck-linux-long
python tools/bootstrap_toolchain.py bootstrap-windows --root . --manifest bootstrap/seeds/i386-windows/manifest.json --plan-manifest bootstrap/seeds/i386-linux/manifest.json --windows-long-paths --output build/recheck-windows-long
```

The Windows command requires native Windows execution. Its default-profile
compatibility proof omits `--windows-long-paths` and uses a separate output.
Normal OS and user recipes select their accepted host path. Do not reuse an
earlier publication after changing captured control inputs.

Evidence is retained under
`build/bootstrap/native-profile-validation-258bb5f3/`:
`fresh-native-both-proof-verification-v1.json`,
`fresh-consumer-verification-self-*-v1.json`,
`fresh-consumer-verification-default-windows-v1.json`,
`fresh-final-os-*-acceptance-v5.json`,
`current-publication-windows-copy-v5.json`,
`current-publication-windows-verification-v5.json` and
`reviewed-long-seed-installation-v1.json`.
The final manual records are `promoted-manual-*-preparation-v6.json`,
`promoted-manual-size-calibration-v6.json`, `promoted-manual-*-acceptance-v6.json`
and `paired-promoted-manual-acceptance-v6.json`. The latter independently
rechecks the accepted Windows files and limits primary-source differences
to documentation and test fixtures.
`installed-long-final-fixtures-windows-v2.json` records the final 79-method
replay and its input rereads.
The installation changes only nineteen approved release paths and retains
their prior bytes in `seed-install-backup-v1/`.

This promotion leaves production ownership at 441 CupidBuild actions and
eleven Python actions. The three user compiler transactions, three user
links, remaining image/verification coordination and full Doom runtime
acceptance still need work. The user compiler draft is separate from this
checked release. `TempleOS/` remains read-only reference material.
