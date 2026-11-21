# ADR 0375: Transfer normal kernel flattening to CupidBuild

Date: 2026-08-30

Status: Accepted

## Context

Both promoted six-tool seeds carry `cupidbuild flatten-kernel`. The command
freezes the graph-ordered production manifest and its 431 inputs, checks the
whole cohort with CupidDis, applies linked local-target and code-anchor checks
to the pass-one and final kernel ELFs, asks CupidObj for a private flat image,
and requires an independent native renderer to produce the same bytes before
publication.

The normal `kernel/kernel.bin` rule still entered that behavior through
`tools/hostbuild.py validate-code`. Python therefore remained the production
owner even though the promoted CupidBuild images already implemented and
proved the complete guarded transaction.

## Decision

The normal Make rule invokes the host-selected promoted CupidBuild image and
its typed `flatten-kernel` command directly. The prerequisite closure is the
checked input manifest, every path named by `CUPIDDIS_PRODUCTION_INPUTS`, the
root Makefile, and all six production seed images plus their manifest.

The graph-owned manifest and input-list variables are `override` assignments.
Standalone CupidDis, CupidObj, checked-runner, and Python variables cannot
replace or enlarge this edge. `PRODUCTION_SEED_MANIFEST` remains selectable so
an explicitly chosen, complete seed cohort can move as one trust unit.

The build-graph audit recognizes the typed command as one CupidBuild,
CupidDis, and CupidObj transform. A production validator requires exactly one
`kernel/kernel.bin` delivery, the exact operation and recipe, a distinct Make
input cohort equal to the checked manifest, the pass-one and final ELFs as the
last two manifest identities, the complete manifest-derived seed closure, and
no extra host inputs.

Hostbuild's equivalent operation remains available as an oracle and for
fault-injection coverage. It is no longer a prerequisite or command on the
normal raw-kernel edge.

## Alternatives considered

Keeping the Python wrapper after seed promotion would preserve duplicate
production ownership and leave the completed CupidBuild capability idle.
Calling standalone CupidDis and CupidObj from Make would split one guarded
transaction across several mutable commands and weaken rollback and drift
checks. Removing the broad validation or shortening its 431-input manifest was
rejected because the active source graph, not the current command-line limit,
defines the required coverage.

## Consequences

The supported three-root graph keeps 748 active inputs, 452 transforms, 255
feature requirements, and 28 accounted unreachable inputs. CupidBuild
participation rises from 194 to 195, while host Python participation falls
from 258 to 257. CupidDis remains at nine and CupidObj at 192 because the same
semantic work moved under the typed coordinator rather than disappearing.

Kernel flattening now has direct promoted-tool ownership on both Windows and
Linux. Python still coordinates disk-image publication, the repository ISO,
the Doom profile manifest, artifact and manifest parity, and both fixed-point
reconstructions. Those boundaries remain separate migration work.
