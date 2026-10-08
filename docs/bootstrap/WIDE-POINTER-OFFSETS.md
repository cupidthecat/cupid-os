# Wide integer pointer offsets

Shared hosted CupidC accepts signed and unsigned eight-byte integer offsets in
ordinary pointer addition and subtraction, either subscript spelling, and
pointer compound assignment. The operand keeps its type and single evaluation
through Linear IR. The i386 emitter reads the low word from its private snapshot,
then applies the pointed-to object's target size. Signed negative offsets work
within the same object. Arithmetic outside C's defined object bounds has no
promised result.

This extends the word-only boundary in ADR 0042. It adds no public IR kind and
leaves the existing word-offset sequence intact. Pointer difference retains its
target result type. Floating offsets, function-pointer arithmetic, incomplete
referents and atomic accesses retain their rejection paths.

The requirement came from preserving full file extents in CupidBuild's guarded
snapshot. Its owner-record reader uses `bytes[snapshot.size - 1u]` after checking
the extent. Widening that size exposed the compiler restriction. Keeping the
ordinary expression avoids source casts added solely to evade the compiler.
The full-width transaction prototype remains separate from this compiler step.

The hosted Windows header also supplies `LONG` and uses `LONG *` for the seek
high word. The matching runtime declaration and local use `long`. These are
four-byte i386 values; the assembly bridge and calling convention are unchanged.
The former `int *` declaration rejected an ordinary `LONG *` caller even though
both storage widths were four bytes.
The graph audit binds the corrected header's exact token fingerprint. Its
mutation contract rejects a mismatched seek high-word type.

## Evidence

The three-line `bytes[index]` reproduction failed twice with `CTD000003`, in
about 30 milliseconds. Signed offsets and explicit pointer addition failed too;
the explicit word conversion passed. The prepared `3072bc4f` compiler retained
the same restriction. The cause was the IR's word-only offset check and the
emitter's matching validation.

The regression exercises signed and unsigned subscripts, negative offsets,
reversed operands, twelve-byte record stride, array stride, volatile index
loads, postfix index updates, and single evaluation of a compound assignment's
destination and offset. Useful failures preserve a preexisting object file.
Native compiler copies pass all three methods on Windows and Linux. The existing
pointer IR, pointer object and wide-mutation contracts also pass on both hosts
with their exact checks.

Cupid-built compiler copies were produced on both hosts from the installed
`2d04ff25` producers with conventional producer commands disabled. Both new
compilers pass all three methods, including a linked i386 runtime. These copies
are development evidence; they have not been adopted as installed seeds.
An additional header method accepts `LONG *` and rejects `int *` while preserving
the existing object. Both Cupid-built compilers pass the final four-method module.
Both matched hosted runtime copies also pass all seventeen sparse-file methods:
reads, writes and append above four GiB, signed seeks, the all-ones low word,
and useful failures. Their existing thirty-second execution bounds remain.

Receipts are under `C:/Users/admin/cp7/`: `wide-index-red1.json`,
`wide-index-red2.json`, `wide-pointer-regression-red.json`,
`wide-pointer-native-windows2.json`, `wide-pointer-native-linux2.json`,
`wide-pointer-neighbor-contracts-windows.json`, `wide-compiler-checked-windows.json`,
`wide-compiler-checked-linux.json`, `wide-pointer-checked-windows.json`, and
`wide-pointer-checked-linux.json`, `wide-pointer-neighbor-contracts-linux.json`,
`wide-file-runtime-windows.json`, and `wide-file-runtime-linux.json`.
The final module receipts are `wide-seek-type-checked-windows2.json` and
`wide-seek-type-checked-linux2.json`. Its initial diagnostic wording assertion
is retained as a failure; the corrected check requires the real `CTB000010`
parameter-conversion diagnostic.
The final static width assertion passes in `wide-seek-type-checked-windows3.json`
and `wide-seek-type-checked-linux3.json`.
The initial byte assertion assumed the short
MOV encoding; it was corrected to the emitter's existing displacement form.
Initial diagnostic assertions were corrected to the actual pointer diagnostics.

The Linux development harness initially reused the copied compiler's output
filename when linking the new compiler. All compilation finished before that
replacement. Its post-link compiler hash therefore identifies the new program,
not the original execution seed. The installed seed was checked before copying;
subsequent harness runs keep execution seeds in a separate directory. This
receipt is not a seed-promotion identity.

The source/manual consumer gate uses the installed `2d04ff25` seeds. Linux's
cold kernel build takes 1,146.547 seconds; its complete normal image build takes
2,470.606 seconds. The full Windows kernel rebuild takes 3,561.948 seconds with
two workers. Windows then runs the unchanged exact-size verifier and Make image
recipe directly, taking 3.476 and 3.087 seconds. This avoids another invocation
of Make's deliberate forced kernel rebuild; no Make rule or compiler deadline
changes. The first Windows kernel attempt fails at the unchanged in-kernel
compiler source with a generic checked-compiler diagnostic. Its isolated retry
passes within the original 180-second child limit and preserves the baseline
object. Load is an inference because the failed wrapper does not report a cause.
An early collector and policy run precede final flatten publication and reject
its locked or older output. The completed producer passes unchanged checks.

All 429 corresponding objects agree between hosts. Only the installed manual's
wrapper differs from the baseline; the other 428 objects and generated symbols
remain exact. The complete 176,375-byte manual occurs once in all three kernel
outputs. Raw size is 9,294,504 bytes, an increase of 596; final and pass-one ELF
sizes remain 9,523,644 and 9,392,572 bytes. Only the raw policy row changes.
Both 200 MiB images have SHA-256
`a29691b4471c3c3a3abbc58229b4d82453380f091927401321d1b4c8db714cf1`.
Every byte in the 199,229,440-byte FAT suffix matches the archived baseline.
All sixteen artifacts meet their exact policy, and all fifteen installed seed
files remain unchanged. These checks do not claim a fresh user-product build.

Evidence is `wide-source-kernel-linux`, `wide-source-image-linux`,
`wide-source-kernel-windows2`, `wide-source-policy-windows2`,
`wide-source-image-windows`, `wide-kernel-measured-linux-image`,
`wide-kernel-measured-windows`, and `wide-image-measured-{linux,windows}`.
The final graph regeneration and check pass all ten contracts in
`wide-source-audit-final` and `wide-source-audit-check-final`. The graph retains
452 transforms, with 449 CupidBuild operations and three Python coordinators.
All four strict private four-CPU max/e1000 smokes pass within the original
150-second limits. Windows ls/SMP and feature 17 take 54.046 and 61.723 seconds;
Linux takes 73.420 and 81.191 seconds. The unchanged completion, success and SMP
predicates remain required. Their records use `wide-boot-{ls,iso}-{linux,windows}`.
`wide-source-os-independent.json` rereads the paired measurements, complete
images and all four original smoke records and serial logs.

Both complete preparations match the 99-input producer snapshot
`0980af62c697272c5107ee52dbc0e8feae5774e98e9af562a5d57ee8cced2753`.
Linux takes 1,957.055 seconds and Windows 1,665.945 seconds. Independent rereading
checks every source and staged artifact, all fifteen parents, both exact plans
and all 97 equal stage-three/stage-four pairs in
`wide-paired-preparations-unqualified.json`. Both actual stage-four tool sets
pass all four current pointer/header methods in `wide-prepared-pointer-linux`
and `wide-prepared-pointer-windows`. Their exact copied tool identities and
control hashes are retained in each mirror's `prepared-pointer-controls/controls.json`.
These preparations and focused behavior checks still require committed-source
binding and complete paired qualification before adoption.

## Ownership

This is a hosted compiler source capability. Installed seeds, ordinary
transaction limits, the normal build graph and Python coordinator count retain
their current ownership. Paired producer qualification and adoption remain
required before the normal build can use the new compiler capability.
