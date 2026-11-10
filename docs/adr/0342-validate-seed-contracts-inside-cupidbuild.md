# ADR 0342: Validate seed contracts inside CupidBuild

## Status

Accepted on 2026-08-25.

## Context

CupidBuild's first guarded object command froze all five seed tools and checked
their sizes and hashes, but it found artifact records with text searches. A
manifest with the wrong schema, target, provenance, producer roles, or build
plan could still reach a checked tool. Compact JSON also failed because the
reader depended on one whitespace layout.

The Python publisher already requires code anchors on the ISR and
context-switch objects. CupidBuild checked instruction coverage and local
targets, but it did not select the same anchor rule. That left its public
command weaker than the production transaction it is intended to replace.

## Decision

CupidBuild now parses the frozen manifest as JSON before it freezes or runs a
listed tool. The parser accepts insignificant whitespace and field order, but
rejects malformed input, trailing data, duplicate fields, unknown fields, and
missing fields. It requires the exact five-tool schema for the current host,
including target values, fixed-point provenance, artifact names and file
names, producer roles, positive sizes, and lowercase SHA-256 values. On Linux
it also checks the complete source and link order in the bootstrap build plan
and its recorded digest.

The guarded object command asks CupidDis for known instruction coverage,
local-target validation, and code-anchor validation. A defined function symbol
inside an instruction now blocks publication and leaves the previous object
unchanged.

The checked manifests still contain five tools. This change does not move a
Make recipe or claim a six-tool fixed point.

## Evidence

The public CupidBuild suite passes on native Windows and Linux. It covers
compact reordered JSON, schema, target, provenance, producer-role, duplicate,
extra-field, and incomplete-inventory failures. The Linux run also covers
build-plan drift. A real CupidASM object with a function anchor in the middle
of a `mov` instruction is rejected by the checked CupidDis image.

The existing transaction cases continue to cover tool and source drift,
links and aliases, lock ownership, cleanup, rollback, and publication-boundary
changes.

## Consequences

Later CupidBuild commands can reuse a parser that understands the current seed
contract instead of relying on manifest formatting. The parser deliberately
pins the current source revision and build plan, just as the Python verifier
does, so a seed rotation must update both trust implementations.

Seed-directory membership and independent ELF32 or PE32 execution-profile
checks have not moved into CupidBuild yet. Python remains the production seed
verifier and publisher until those checks, a six-tool promotion, and a normal
recipe transfer are complete.

ADR 0344 supersedes the first sentence above. CupidBuild now checks complete
seed-directory membership and every frozen tool's execution profile. Six-tool
promotion and a normal recipe transfer remain open.

ADR 0339's earlier statement that relocatable objects do not select code
anchors is superseded for the active object lanes. ADRs 0335 and 0336 define
and adopt that `ET_REL` rule.

`TempleOS/` remains untouched reference material.
