# User compilation staged proof and OS acceptance

The fresh native Linux and Windows proofs pass from committed producer
`78e71bd6137042720c378d2c596aa40b153dad11`. Both bind the same 77-input snapshot,
`ae31a4da21f099ab2c9d449b3ed1825947a9bedcb9ff756b5125953076ec7f30`.
Independent verification rereads all 1,544 frozen root inputs, checks the
producer inputs against Git, validates both retained parent seeds, and checks
every stage and comparison. Linux matches 27 C objects, one startup object
and six tool images. Windows matches 32 C objects, four assembly objects and
six tool images. Each stage-three/four comparison is byte-identical.

Linux passes 55 failure, seven help and 62 success groups. Windows passes
43 failure, seven help and 49 success groups. These include the new closed
user compiler behavior. The installed preceding pair keeps its own earlier
counts; no historical proof is relabeled.

The nineteen-path release preview validates both proposed manifests and the
independently pinned release author. Proposed Linux manifest SHA-256 is
`b6f247af2034d7432333eed74230452fede2198ba744c30a5c410ce19c4b79b4`;
Windows is `1d40ec6e03bdd736e5993f8a204588f0e376541f4019b83bd00650469b9531bd`.
The pair is not installed. Separate fresh consumer roots contain all 1,544
captured inputs, with only the nineteen reviewed preview paths substituted.
Both long-profile self-consumption runs and the complete default-profile
Windows bootstrap are running. Seed installation and the normal/custom user
Make handoff remain separate acceptance steps.

## Committed source OS replay

Both hosts pass the normal kernel build with host code-producing tools
forbidden, all sixteen artifact checks, image publication, the three user
programs and private four-CPU max/e1000 disassembly/shell/SMP smoke. Linux's
current-source contract publication and user ABI gate complete before its
runtime replay. Independent verification rereads 1,544 source/control inputs,
all 431 link inputs and both disk images.

The embedded manual is 64,715 bytes. Both raw kernels are 9,577,752 bytes,
first-pass ELFs are 9,675,196 bytes and final ELFs are 9,806,268 bytes. Only
the measured raw-kernel size row changes; its preceding value rejects the new
kernel before calibration. The other fifteen policy rows remain exact.
Both 200 MiB images have SHA-256
`8631d37aa4cdb4aa417f9cd0b1a65c9e7924446b1ab87464d379b39ba6ddbd42`
and remain unchanged through runtime checks. The three user executables match
the preceding acceptance: `cat` and `hello` are 13,992 bytes, `ls` is 18,112.
The changed link inputs are the manual object and the two kernel ELFs.

## Retained evidence and failed setup

Evidence is under `build/bootstrap/native-profile-validation-258bb5f3/`:
`user-native-both-proof-verification-v1.json`, `user-preview-v1.json`,
`user-preview-v1.log`, `user-promoted-source-v1.json`, the three fresh consumer
capture records, both `user-source-manual-*-acceptance-v7.json` records and
`paired-user-source-manual-acceptance-v7.json`.

A premature publication capture rejects an incomplete private bootstrap report
before copying it. A separate first Windows proof launch precedes completion
of its source preparation and fails on the missing capture file before tool
execution. The completed preparation supplies a new, separate passing run.
Neither failed setup contributes fixed-point or runtime evidence.

Production ownership remains 441 CupidBuild actions and eleven Python actions.
The three user links and full Doom gameplay, audio, save/load, reboot and
performance acceptance remain open. `TempleOS/` stays outside these builds and
counts.
