# ADR 0323: Promote and adopt static ELF code-anchor checks

## Status

Accepted on 2026-08-22.

## Context

ADR 0320 added source-built CupidDis checks for executable entry points and
defined `STT_FUNC` symbols. The promoted Linux and Windows seeds still carried
the preceding CupidDis image, so the normal kernel transaction could not select
the new option safely.

The kernel publisher already freezes the checked five-tool cohort and both
linked ELFs. It runs broad decoding, linked local-target validation, and
CupidObj flattening inside one private transaction. Code-anchor validation
belongs in that same linked-image pass because it uses the same instruction
boundaries and must block flattening on failure.

## Decision

Promote the stage-four CupidDis images from matching Linux and native Windows
fixed points. Both manifests bind source revision
`b3f0910f84ba182d0882fc67b5983b49e9627482`, the exact 50-input snapshot
`4cc8183e1def88b33cec4b8b5f9111badb22999f27b9a48f54b991aad65e2c19`, and
the existing build plan. The Windows manifest names the promoted Linux
manifest as its parent.

The normal linked-kernel pass now invokes CupidDis with:

```text
--require-known
--require-local-targets
--require-code-anchors
kernel/kernel.elf.pass1
kernel/kernel.elf
```

The two policies share one decoded instruction-start map. Input drift, seed
drift, diagnostics on standard output, a nonzero tool status, or a runner
failure all stop the transaction before CupidObj runs. The previous public
kernel remains in place on failure.

## Evidence

The Linux candidate matches 19 C objects, one startup object, and all five tool
images between stages three and four. Its behavior matrix passes five help
cases, 22 successful operations, and 21 useful failures. The native Windows
candidate matches 20 C objects, two assembly objects, and all five tool images.
Its matrix passes five help cases, eight successful operations, and nine useful
failures.

Only CupidDis changes in either checked cohort. The promoted Linux image is
442,780 bytes with SHA-256
`1f4fade7dad85077b320d8ef51347eaaf2bef6510659e08be0980cabe5368569`.
The promoted Windows image is 420,352 bytes with SHA-256
`18167d5ae7b86ad0edd332da2eaef292c718572c5c8eba5847e57f142cdd8e45`.
Direct Linux and native Windows carriage tests accept a valid static image and
reject an entry point in the middle of an instruction.

The fixed-point behavior gate compares the complete stable failure diagnostic.
Its expected input path follows the actual tool image: an ELF CupidDis on
Windows receives a WSL path, while a PE CupidDis receives the native Windows
path. A focused native-PE helper contract keeps that boundary distinct.

## Consequences

The normal kernel publication path now rejects linked images whose entry point
or defined function symbols do not start decoded instructions in file-backed
executable code. This adds no host compiler, assembler, linker, or WSL step to
the normal build. Python still coordinates the checked transaction and both
fixed-point drivers. Minimal source-level debug information and Python-free
coordination remain separate bootstrap work.

No active source suffix changes in this promotion. `TempleOS/` remains
read-only reference material.
