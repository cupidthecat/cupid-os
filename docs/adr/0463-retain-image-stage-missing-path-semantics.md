# ADR 0463: Retain image-stage missing-path semantics

## Status

Implemented as a held patch on 2026-10-10. All 312 new native/Cupid calls and
both complete repository replays pass. Normal source, seeds and recipe
ownership remain unchanged. Complete image discovery and publication remain
open. The later [OS manual acceptance](../bootstrap/SOURCE-STAGE-MANUAL.md)
records this held API after paired and normal-root builds and strict boots.

## Context

The optional file owner retains a first absence beneath an existing directory.
The normal image command accepts missing ancestors appearing while the final
requested leaf stays absent. Python also accepts children of broken parent
aliases on both hosts and children of regular-file parents on Windows. The
existing owner rejects those cases. The earlier diagnostics retain both
contracts; image stages need their own selection lifetime.

## Decision

Add an opaque image-stage owner. Copy the complete normalized request before
caller storage is released. Retain present regular files through the optional
file owner. Missing views expose only the original requested name, with no
observer or physical-root claim.

Add a read-only full-path absence query that follows the host's parent lookup
rules and never follows a linked leaf. Windows file/path-not-found and Linux
`ENOENT` mean missing; other errors and present leaves reject. Revalidate that
complete request before each borrowed view. A failed view clears its result
and permanently poisons the stage owner.

Require caller revalidation before consumption and after use. Keep present
observers alive through borrowing transactions. Close releases storage and
does not supply final validation. Preserve the existing required/optional
owners, captured observations and retained-stream contracts.

## Evidence and limits

The [implementation record](../bootstrap/NATIVE-SOURCE-STAGE.md) retains the
combined patch, actual Python oracles, all failed harnesses and fresh replays.
The new 312 calls include 36 complete 4,096-distinct-missing-request cases.
The current full selection preserves all 2,054 API outcomes, 340 legacy
passes with twelve skips and 48 alias methods. Whole checked objects and
programs equal their originals. All 69 strict checks pass under their
original flags. C runtime commands keep sixty seconds and Linux's
32 MiB/10,240-descriptor bounds.

This proves a stage component's missing-path semantics. Full required-input
discovery, retained physical-root interning, distinct present-stage capacity
and publication remain open. The 99 normal inputs, fifteen installed seed
files and three Python coordinators retain their existing ownership. TempleOS
stays read-only and excluded.
