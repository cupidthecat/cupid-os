# ADR 0371: Transfer kernel-symbol publication to CupidBuild

## Status

Accepted on 2026-08-30.

## Context

ADR 0369 added the typed `cupidbuild generate-ksyms` transaction, and ADR 0370
promoted it in both active seed cohorts. The normal two-pass kernel build still
called `tools/hostbuild.py mksyms`, even though the promoted command already
owned the same guarded work without shell redirection.

Keeping that wrapper in the normal recipe left Python responsible for one
production transform after CupidBuild could already freeze the pass-one ELF
and six-tool seed, capture CupidDis output privately, run CupidObj against
those exact rows, compare the result with its independent native renderer, and
publish atomically.

## Decision

Make invokes the platform production CupidBuild image directly for
`kernel/cpu/ksyms_data.cc`:

```text
cupidbuild generate-ksyms
  --seed-manifest <platform manifest>
  --root <repository>
  --source kernel/kernel.elf.pass1
  --output kernel/cpu/ksyms_data.cc
```

The rule depends on Makefile, the selected manifest, and all six images in the
production seed directory. The platform suffix, manifest-derived directory,
and `PRODUCTION_SEED_INPUTS` are `override` assignments, so command-line
variables cannot separate the invoked CupidBuild image from that trust-unit
closure.
The recipe no longer depends on Python, Hostbuild, the generic checked runner,
or redirectable CupidDis and CupidObj variables.

The audit attributes the transform to CupidBuild, CupidDis, and CupidObj. It
recognizes both typed `generate-ksyms` and legacy `mksyms` as kernel-symbol
source operations so Hostbuild fixtures remain useful. Hostbuild itself stays
available as an optional independent oracle; it is no longer the production
coordinator for this edge.

## Evidence

The Make recipe contract failed first against the Hostbuild command, then
passed after the direct handoff. A second contract poisons Python, CupidDis,
CupidObj, the generic checked-seed inputs, the production directory and
suffix, and the production seed inputs on both Linux and Windows Make graphs.
Both graphs retain the exact platform CupidBuild command and canonical
six-image closure.

On the real pass-one kernel, promoted Windows CupidBuild and Hostbuild produced
the same 432,591-byte source with SHA-256
`0f4ef6cd6c1c1cb14fd82efbe3905d33628d7627b00ab1bd7e1b1fed72764e6a`.
The existing typed-operation tests cover successful generation, malformed
input, forced renderer disagreement, a live publication lock, drift, rollback,
and same-job recovery.

The regenerated audit keeps 452 transforms, 443 under root `all`, 192 CupidObj
participations, and nine CupidDis participations. CupidBuild rises from 193 to
194, while Python falls from 259 to 258. Four composite CupidObj paths remain
Python-coordinated: kernel flattening, disk-image publication, ISO publication,
and the Doom profile manifest.

## Consequences

Kernel-symbol source generation is the sixth typed guarded publication owned
directly by CupidBuild, alongside two relocatable objects, two raw images, and
the JPEG object. Python still coordinates fixed points, compiler wrappers,
independent contracts, and the four remaining composite CupidObj paths, so
issues tracking a Python-free bootstrap remain open.

No C source changes suffix. Every active Cupid-owned translation unit already
uses `.cc`, and `TempleOS/` remains read-only reference material.
