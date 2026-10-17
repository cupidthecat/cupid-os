# ADR 0347: Certify fixed-point objects and CupidBuild startup anchors

## Status

Accepted on 2026-08-25.

## Context

The Linux and native Windows fixed points validated each CupidC object as an
i386 relocatable before linking it. Strict CupidDis certification covered the
startup object and final tool images, but not every intermediate C object. A
bad relocation could therefore survive a byte-identical fixed point and lose
its ownership information in the linked image.

The native Windows CupidBuild startup object had a related blind spot. Its
fourteen exported entry points were global `STT_NOTYPE` symbols. The code-anchor
policy checks typed function symbols, so those entry points did not participate
in the proof.

## Decision

Run the preceding generation's CupidDis on every fixed-point CupidC object
after structural ELF validation and before any link. Require known
instructions, valid local targets, and code anchors. A rejected object stops
the stage before startup assembly or linking.

Mark all fourteen CupidBuild Windows startup exports with `:function`. Keep an
ordinary untyped control symbol in the assembler contract so the test proves
that typing changes only symbol metadata, not code, relocation, placement,
binding, or value.

Keep the build audit fail-closed. It checks both host stage builders, all three
strict flags, the preceding-generation producer, and the rejection order.

## Evidence

The focused bootstrap tests cover every Linux and Windows C object and reject
an executable relocation that does not belong to a decoded field. Six tests
pass, and the build-audit mutation test passes after removing each host call or
weakening its flags.

The active-source assembler contract first found all fourteen untyped exports.
After the annotations, the focused source test, all six active-source tests,
and the three startup-candidate tests pass. The typed and untyped fixture
outputs retain identical text and relocation data.

Python compilation and `git diff --check` pass. The complete fixed points are
required before these source changes can enter a promoted cohort.

## Consequences

Fixed-point equality now includes a semantic check at the last object boundary
where relocation ownership is still available. The native CupidBuild bridge
also contributes every exported entry to code-anchor validation.

This change does not promote a six-tool seed, transfer a normal recipe, or
remove Python coordination. It changes no active source ownership, so there is
no `.c` file to rename.
