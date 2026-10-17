# ADR 0394: Compile Doom with filtered discovery

Date: 2026-09-20

## Decision

Add `cupidbuild compile-doom --seed-manifest MANIFEST --root ROOT --source
SOURCE --output OUTPUT` for the three compatibility sources and 80 Doom-tree
sources already compiled by CupidC. Membership determines the profile; the
caller cannot supply compiler flags. The output must be the selected source's
corresponding `.o` path. Both profiles keep the 180-second deadline,
`--doom-compat`, ordered include roots, and absence of `DEBUG`. The tree
profile also keeps its save-directory and port definitions and forced
`dglibc_compat.h` include.

Discovery must find exactly the approved `.cc` cohort. Extra or missing
sources, legacy `.c` files, links, and matching non-files fail. Capture retains
every discovered header and source together with the six-tool seed. The
compiler's closed bundle contains the selected source and all captured
headers, sorted by logical path. Missing bundle entries cannot fall back to
live files. Bounded allocated records replace the kernel closure's fixed-size
arrays for this larger discovered set. The existing 512-input transaction and
64 MiB bundle limits remain in force.

Kernel and Doom compilation share bundle serialization, checked execution,
ELF32 validation, output locking, candidate verification, unchanged-output
reuse, and rollback. Doom rechecks discovery before launch and at every full
publication boundary, including after installation and before accepting an
unchanged object. All captured input bytes and snapshots must still agree.

## Directory policy

Doom objects live inside the directories scanned for sources and headers.
Their publication changes directory timestamps, as do parallel kernel and
toolchain object writes. Compiler discovery therefore retains directory
handles and checks identity instead of requiring unchanged directory times.
It records each filtered query and its exact paths and file snapshots, then
repeats discovery through the retained repository hierarchy. Replaced
directories and changes to relevant membership fail. Unrelated object and
private transaction files may change.

This policy is selected before discovery and sealed after capture of the
membership tables. The profile-manifest publisher still uses strict directory
snapshots. Its output lies outside the scanned roots, and its existing race
checks remain required. The documented DrvFS recovery limit also remains:
when a replaced parent cannot safely restore the old public name, verified
recovery evidence must survive and foreign files must remain untouched.

## Evidence and ownership

Executable contracts compare all 83 native-coordinated objects with ordinary
compilation, exercise both profiles through a Cupid-built coordinator, and
check input failures, publication races, unchanged timestamps, bounded
capture, and concurrent distinct outputs. Both staged drivers include Doom
behavior checks with each compared generation's own six-tool cohort.
The bootstrap log records actual results and failed approaches.

This is a source capability. Checked-seed promotion and Make adoption require
separate paired fixed-point proofs and production build/runtime evidence.
Production ownership remains 354 CupidBuild and 98 Python participations.
All 83 sources already use `.cc`; no source rename or language workaround is
needed. Full IWAD-backed gameplay acceptance remains separate. TempleOS is
reference material only and does not enter the build or ownership counts.
