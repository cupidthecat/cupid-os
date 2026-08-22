# ADR 0320: Validate static ELF32 code anchors

## Status

Accepted on 2026-08-22.

## Context

CupidDis already proves that every file-backed executable byte decodes and
that every constant direct branch or call lands on an instruction start. A
static executable can still advertise a broken entry address or a defined
function symbol that points outside executable code or into the middle of an
instruction. Both cases can survive byte-level decoding and local-target
checks.

The linker supplies the final addresses needed to validate these anchors.
CupidDis already builds an instruction-start map for static `ET_EXEC` local
targets, so code-anchor validation can share that model without adding a new
symbol or decoder authority.

## Decision

Add the explicit `CTOOL_DIS_POLICY_CODE_ANCHORS` policy and the hosted
`--require-code-anchors` option. The CLI requires `--require-known`. The typed
request requires the disassembly view.

Apply the policy only to static i386 ELF32 `ET_EXEC` input with nonoverlapping
file-backed executable load regions. Reject `PT_DYNAMIC` and `PT_INTERP`
images. Raw input and relocatable objects remain outside this policy.

Count the ELF entry address and every defined `STT_FUNC` symbol as separate
anchors. Function aliases therefore remain visible in the report. Undefined
and absolute functions do not describe executable locations and are excluded,
as are symbols of other types.

Require each counted anchor to equal a decoded instruction start in
file-backed executable code. Report anchors outside that code separately from
anchors in the middle of an instruction. When local-target and code-anchor
policies are both selected, build and populate one instruction-start map for
both checks.

Keep this as a source-head capability until both checked seeds are promoted.
The normal kernel publisher continues to use `--require-known` and
`--require-local-targets`. A later promotion can add
`--require-code-anchors` to the pass-one and final ELF validation call.

## Evidence

The typed public contract covers a valid entry and two function aliases,
indexed and unindexed parity, combined local-target and anchor policies,
undefined and absolute functions, a mid-instruction entry, a mid-instruction
function, an entry outside loaded code, and an entry in an executable
memory-only tail. It also covers missing disassembly state, raw and `ET_REL`
rejection, dynamic and interpreter images, constrained instruction-map
storage, a zeroed report on failure, and same-job recovery.

The hosted CLI accepts the valid fixture without output. It rejects each
invalid anchor class with exact counts, rejects incomplete option
combinations as usage errors, and rejects relocatable, overlapping, dynamic,
and interpreter inputs without publishing a strict success.

The fixed-point drivers add one valid and one invalid sectioned executable
fixture on Linux and Windows. The generated audit records failure, help, and
success counts of 21/5/22 for Linux and 9/5/8 for Windows. It also records
`cupiddis.elf32_code_anchors` as a source-head capability. These counts are
source evidence only. The promoted seed matrices remain 20/5/21 and 8/5/7.

## Rejected alternatives

Silently extending `--require-known` was rejected. Artifact owners should
select symbol and entry validation explicitly.

Treating every defined symbol as a code anchor was rejected. Objects, section
markers, and linker boundaries do not promise an instruction address.

Using symbol values to invent instruction starts was rejected. Symbols are
claims to validate against decoded bytes, not another decoder authority.

Enabling the option in production before seed promotion was rejected. The
checked Linux and Windows CupidDis images do not recognize the new option yet.

Adding partial DWARF support in this slice was rejected. Line information is
useful for presentation and diagnostics, but it does not strengthen entry or
function placement and should arrive with its own format contract.

## Consequences

Source-head CupidDis can now certify that a static executable's entry and
defined function symbols name real instruction starts. The check composes with
local-target validation and reuses its compact map.

No production owner, transform, checked seed, or host dependency changes in
this step. Minimal DWARF source information remains missing. `TempleOS/`
remains read-only reference material.
