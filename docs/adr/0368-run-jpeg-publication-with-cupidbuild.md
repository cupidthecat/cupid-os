# ADR 0368: Run JPEG publication with CupidBuild

## Status

Accepted on 2026-08-29.

## Context

ADR 0364 added a typed `cupidbuild embed-jpeg` transaction, and ADR 0367
promoted Linux and Windows seeds that carry it. The normal `%.jpg.o` and
`%.jpeg.o` rules still called Hostbuild, even though the promoted tool could
already freeze the asset and seed, run CupidObj under the original logical
identity, validate the result independently, and publish it safely.

Keeping that recipe on Python left one production transform behind its
available CupidBuild boundary. It also meant that fixed-point carriage proved
more than the normal build used.

## Decision

Make invokes the promoted platform `cupidbuild embed-jpeg` command directly
for both JPEG suffixes. Each rule depends on Makefile and the complete
production seed trust unit, then passes the production manifest, repository
root, source, and output explicitly.

The recipe does not consult `PYTHON`, `CUPIDOBJ`, or the older checked-seed
wrapper variables. CupidBuild owns the transaction: it freezes the source,
manifest, and all six tool images; runs frozen CupidObj `wrap-jpeg` under the
original source identity; checks the exact data payload and start, end, and
size symbols; applies an independent native sequential-JPEG parser; rechecks
the lock and every live publication boundary; and replaces the destination
atomically. Failure preserves the previous object.

The build audit attributes the active JPEG object to CupidBuild and CupidObj,
with no Python owner. Across the three supported graphs, CupidBuild
participation rises from 192 to 193 and Python participation falls from 260 to
259. The transform count remains 452, including 443 under root `all`.
CupidObj remains at 192. Five composite CupidObj paths still retain Python:
kernel flattening, kernel-symbol generation, disk-image publication, ISO
publication, and Doom profile-manifest publication.

## Evidence

The recipe contract first failed against the two Hostbuild rules, then passed
after both rules moved to CupidBuild. A separate dry-run contract instantiates
both suffixes with poisoned `PYTHON` and `CUPIDOBJ` overrides and requires
the promoted typed command.

A forced normal rebuild with `PYTHON=python-that-must-not-run` produced the
800,860-byte `file_example_JPG_1MB.jpg.o` through CupidBuild. Its SHA-256 is
`74ab86d88302c90385bb0b858632b0d6c4ac983d6be28c976dd1a3a348204b3e`,
which matches the established production object exactly.

The complete CupidBuild CLI module passed 79 tests in 79.180 seconds with
three expected platform skips. Audit regeneration and independent check mode
passed with the new 193/259 ownership split. The audit records the active JPEG
transform as `wrap_binary_as_elf32_relocatable`, lists the asset, Makefile,
manifest, and all six seed images, and assigns exactly CupidBuild and CupidObj.

The first CTXT-bearing replay reached the exact-size gate and rejected a
9,509,800-byte flat kernel against the older 9,509,748-byte policy row. Only
that measured row changed. A fresh `make -j4 all` then rebuilt the full kernel
and all 83 Doom roots, passed both CupidLD links and both CupidDis inspections,
accepted all 16 exact artifacts, and published the disk image.

The final private-image smoke brought all four vCPUs online, passed all five
in-kernel toolchain self-tests, opened the GUI terminal, and ran `/bin/ls.cc`
to normal JIT completion without a panic or exception marker. The bootstrap
log records the complete artifact, audit, test, and runtime identities.

## Alternatives considered

Leaving the recipe on Hostbuild was rejected because the promoted seed already
owns the complete transaction. Calling generic `cupidbuild run --tool
cupidobj` was rejected because that runner does not provide the JPEG-specific
parser, identity checks, lock, rollback, or atomic publication contract.
Removing the independent parser was rejected because CupidObj must not be the
only semantic authority inside the transaction.

## Consequences

Normal JPEG embedding no longer requires Python. Both `.jpg` and `.jpeg`
sources use the same checked production boundary, including suffixes that are
not present in the current asset set. Python remains required elsewhere in the
build and test system, so this is one ownership transfer rather than a
Python-free bootstrap claim.

No active C source changed, so no `.c` rename is due. `TempleOS/` remains
untouched reference material.
