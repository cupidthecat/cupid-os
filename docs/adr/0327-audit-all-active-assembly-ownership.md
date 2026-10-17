# ADR 0327: Audit all active assembly ownership

## Status

Accepted on 2026-08-22.

## Context

The generated build audit attributed normal image assembly to CupidASM, but
the fixed-point startup sources entered through the Toolchain contract
manifest and did not all receive an explicit owner. The omission made the
reported assembly frontier narrower than the supported build graph.

## Decision

Derive the fixed-point startup assembly cohort from the Toolchain contract
manifest transform and assign those sources to CupidASM. Fail if that cohort
loses or gains a startup input without an audit update. Also fail when any
active assembly source has no build owner unless it has an explicit
`host_fixture` or `host_oracle` classification with a reason.

Publish an `assembly_source_ownership` contract in the generated audit with
active, CupidASM-owned, other-owned, ownerless, explicitly classified, and
Toolchain startup counts.

## Evidence

Seven focused tests cover the four startup inputs, mutated manifest inputs,
ownerless source rejection, and allowed host-only classifications. A full
audit generation followed by an immediate `--check` passes. The current graph
contains 31 active assembly sources. All 31 are CupidASM-owned, four are
Toolchain startup sources, and none are ownerless.

## Consequences

The audit now covers the complete active assembly inventory rather than only
the normal image recipes. This is an ownership proof, not a new assembler
encoding or build transform. The supported graph still has 452 transforms and
no NASM-owned transform. No source rename or seed promotion is involved.

`TempleOS/` remains read-only reference material.
