# Formatted producer qualification

The Linux and Windows tools rebuilt from `d13ed4aa` pass complete behavior
qualification against the reviewed current cohort. This covers the formatted
producer source. Replacement consumers and installed-seed acceptance remain
separate work.

## Rebuild and independent checks

Each host first prepares stages two through four. A separate paired readback
checks all prepared bytes and 114 source inputs against raw Git blobs before
authoring the behavior release. Qualification then rebuilds the complete
stages and requires their tools to match that reviewed release.

| Check | Native Linux | Native Windows |
| --- | ---: | ---: |
| Preparation time, seconds | 1,504.873 | 1,451.762 |
| Full qualification time, seconds | 2,165.487 | 2,388.527 |
| Objects per stage | 45 | 54 |
| Tools per stage | 6 | 6 |
| Successful behavior cases | 73 | 60 |
| Expected failure cases | 66 | 54 |
| Help cases | 7 | 7 |

Both runs retain the 7,200-second outer bound, existing tool limits and nine
forbidden conventional producer variables. Their complete source and installed
seed guards remain unchanged. Independent review rereads all 333 staged
artifacts, validates their ELF or PE structure and compares all 111 complete
stage-three/stage-four pairs. The 15 installed seed files remain unchanged.

The producer snapshot has SHA-256
`8f570ec78b0e81c2bb31b6a0774d3726b0f972d88cdb0556f83437050adbd36e`.
The separately reviewed release has SHA-256
`cbfe85822ba2780ab8898e492791ab19c814450a259571655a8884b82ec218b8`.

## Private consumer projection

Isolated Windows and native Linux checkouts receive the qualified twelve
images, two measured manifests and reviewed release. Their Python pin
assignments bind that exact pair. Twelve seed-size policy rows take the
measured image sizes; other artifact rows remain unchanged. This is a
17-file private projection per host, with no change to the 114 producer inputs.
Both private paired-seed verifiers pass.

Fresh ordinary SDK publications are running on both hosts with two workers,
the original limits and the reviewed release. Long-path publication, cold
OS/user builds, guest acceptance and the final seed transition remain open.
The normal graph still has 449 CupidBuild transforms and three Python
coordinators.

The [qualification archive](evidence/formatted-producer-qualification-20261110.json.gz)
retains 8,658 file records and 699 payloads: source blobs, preparations,
qualifications, behavior products, controllers, independent review and both
private projections. Every payload was decoded and its size and SHA-256
checked after export. The archive has 10,038,017 bytes and SHA-256
`46be8c48cf975c198ec8771ce31b06178265cb5b3b2ad5763b1c1175eb1ff0d8`.
