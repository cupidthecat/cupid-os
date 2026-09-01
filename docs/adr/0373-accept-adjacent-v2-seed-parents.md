# ADR 0373: Accept adjacent v2 seed parent pairs

- Status: Accepted
- Date: 2026-08-30

## Context

The kernel-flattening seed candidates passed their stage-three/stage-four
comparisons while they were still driven by the committed parent seeds. The
first Windows self-consumption run then rejected the copied candidate manifest
with `fixed-point provenance differs`. Its source-built CupidBuild recognized
only the parent pair named by the committed v2 seeds, because the candidate
run could not exercise the manifest that would exist after promotion.

The Linux reproof was stopped after the shared cause was confirmed. No failed
candidate was committed or published as a checked seed.

## Decision

CupidBuild's v2 manifest validator accepts two exact, adjacent parent
generations during a seed refresh: the pair named by the active checked seeds
and the pair named by their replacement. A manifest digest is always coupled
to its matching source revision. Mixing the digest from one generation with
the revision from the other remains invalid.

The v1 contract is unchanged. The v2 source-input transition continues to
accept only 58 or 59 inputs, and the rest of the manifest remains exact. The
replacement candidates are built from an archived copy of the committed
seeds after independently checking the Linux and Windows manifest hashes.

## Evidence

Focused tests accept the replacement pair and reject a mixed parent
generation before any output changes. The complete CupidBuild module passes
96 tests with three expected platform skips. The same suite also exercises
the still-active predecessor manifests, which keeps the current checked seeds
usable throughout the transition.

## Consequences

A source-built CupidBuild can validate both sides of one controlled v2 seed
promotion. This is a narrow transition window, not a general relaxation of
provenance. A later promotion can retire the older pair when it no longer
needs to bootstrap its successor.

This decision changes no production Make ownership. Seed carriage and the
kernel-flatten handoff remain separate green steps, and issue #34 stays open.
