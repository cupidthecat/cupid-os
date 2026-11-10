# ADR 0336: Promote and adopt assembly function anchors

## Status

Accepted on 2026-08-24.

## Context

ADR 0335 gave CupidASM an explicit function-symbol spelling and let CupidDis
check those symbols in relocatable objects. ADR 0340 added source-resolved
control edges to raw maps. The checked Linux and Windows seeds predated both
features. Active assembly could not publish function types until production
used a seed that understood them, and promoting raw-map v2 without updating
the guarded publishers would reject the new map shape.

## Decision

Build one Linux and Windows stage-four cohort from combined source revision
`a17c9465911da41d59b7ada71733d36c39faa5ea`. Promote both cohorts only after
their stage-three and stage-four objects and tool images match and their public
behavior checks pass. This single promotion carries relocatable function
anchors and raw source-edge metadata.

Keep that exact source revision reachable at the archival remote branch
`bootstrap/seed-provenance-a17c946`. The manifests continue to name the commit
itself; the branch prevents the proof object from disappearing when the
temporary construction branch is removed.

Mark the exported ISR, context-switch, Linux startup, and Windows startup
labels as `STT_FUNC`. Guarded ISR and context-switch publication runs strict
CupidDis local-target and code-anchor checks before replacing an object. Every
fixed-point startup object receives the same checks before CupidLD runs.

The guarded bootloader and SMP publishers accept the v2 map shape and require
source-edge validation. The SMP publisher keeps its exact range and edge list.
A failed assembler or inspector run preserves the published binary or object.

## Evidence

The clean Linux candidate bound 50 inputs with SHA-256
`46c5335c80d822dd5085ee22077486ea647e5396482d42454847c87e4222aa67`.
It matched 19 C objects, one startup object, and five tools across stages three
and four, then passed 5 help, 22 success, and 21 failure cases. Its report has
SHA-256 `e798158ff5f796d7c477eae4ad5e5fab8474143640fe2e9aa56c10b9b1541485`.

The native Windows candidate used the same source closure. It matched 20 C
objects, two assembly objects, and five tools across stages three and four,
then passed 5 help, 8 success, and 9 failure cases. Its report has SHA-256
`c8ae74245097f5414c5e75c00df355c1e35344fc79ced26986f3e3e6aa96adba`.
In both reports, only CupidASM and CupidDis differed from the preceding seed;
CupidC, CupidLD, and CupidObj remained byte-identical.

The promoted Linux manifest has SHA-256
`b6e34a2e18dd18aba91c6358116eafde39953566efeadb224575ac8c13ab2c1b`.
The paired Windows manifest has SHA-256
`751e1d7787a4be08e4e86814bbb7473979fe2eb8a3292baed0241967f772eaef`.
Both verify all five promoted artifacts.

The active adoption closure has 50 inputs with SHA-256
`f7ac3fbf682bc7cc2a70f2aec8a3d5157cd79378937f9116bb0cf612ab5d1fc5`.
Its Linux promoted-seed reproof matched the final generations and repeated the
5/22/21 behavior matrix. The report has SHA-256
`58cb7459b6f7c4918e24cbe717eb19803916696ef25e3ec7375207f6bbb4d5ed`.
The native Windows reproof used the same source snapshot, matched 20 C
objects, two assembly objects, and five tools, then repeated the 5/8/9 matrix.
Its report has SHA-256
`0812756b2a589693a4bb019eb82ef68e8cd2e149316e0a8916e857f3c35cf1dc`.

Active-source contracts prove that all function annotations preserve section
bytes, relocations, bindings, placements, and symbol values. Hostbuild and
bootstrap-stage contracts cover successful certification, rejected anchors,
rejected source edges, rollback, and the rule that CupidLD never runs after a
startup object fails inspection.

## Consequences

Assembly entry points now participate in the same explicit instruction-start
policy as CupidC functions. Raw publishers bind represented control transfers
to source-resolved targets instead of relying only on decoded local branches.
Inference from global binding remains deliberately unsupported.

`TempleOS/` remains read-only reference material.
