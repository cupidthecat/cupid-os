# ADR 0292: Promote strict relocation production seeds

## Status

Accepted on 2026-08-14.

## Context

ADR 0286 made CupidDis the publication guard for the two normal CupidASM
objects. ADR 0290 then taught source-head CupidDis to check that every
relocation in executable code belongs to a compatible decoded instruction
field. The checked Linux and Windows seeds predated that work, so the public
transaction could still publish an object whose bytes decoded completely but
whose relocation pointed at an opcode or the wrong operand kind.

Windows runs the checked PE execution seed for output-bearing production
commands. Linux runs the checked static ELF seed, and the Windows fixed-point
driver also takes its build plan from that Linux manifest. Moving only one
CupidDis image would split the five-tool trust unit and would leave one normal
host on the old rule.

The accepted source closure also contains wide integer conversion to `float`
and `double` and the corrected CupidASM treatment of absolute `EQU` before a
raw section claim. A promotion from an earlier revision would knowingly omit
that assembler correction.

## Decision

Promote the five stage-four i386 Linux tools built from clean revision
`bf52d135348bc33ff32e66d549bbee5edc69d8ad`:

| Tool | Bytes | SHA-256 |
| --- | ---: | --- |
| CupidASM | 458,256 | `1eb32e11f85bb18d39a122853dfc1ad4a446ae7516e3d810c60d5f90b43fed8e` |
| CupidC | 2,666,324 | `8b6b0f0508b1565d095297f3571ef9bb4d444d19be0700165706877b210b087c` |
| CupidDis | 413,204 | `ff2e345c1000c7e4843b91e5d17d9a171e76b0d6fbae2871ce879b338691555a` |
| CupidLD | 312,792 | `a2119556894903b662d2e131a9a2436b99a3afdd1b1600a3df4d4669569a0295` |
| CupidObj | 392,688 | `99111b5db7586ac4b2ed00005f2fe2e89c66ed48f007d796206b116a088cdf7a` |

The 5,573-byte Linux manifest has SHA-256
`d571125256d11dd707f661299738891edc5c1a8d3358554076875a3e0cac22d0`.
It binds generation four, the clean revision, the stage-three producers, and
the 50-input snapshot with SHA-256
`e76d36ed4edc7679e91ac237135fe476dff6e69946bbffca56077afbf19a47f9`.

Promote the five native stage-four Windows tools reconstructed from the same
source closure:

| Tool | Bytes | SHA-256 |
| --- | ---: | --- |
| CupidASM | 438,784 | `c54bb09f1eb317a23d1680da25c78a5a439bde44654ae8b908ddca11fd7e56d6` |
| CupidC | 2,592,768 | `765fa14724c1615088fb9280a16f3457a4c4f14fa2d1915d3c56ff73b2b797cd` |
| CupidDis | 391,680 | `f6d38d66f002c4440aacea08ca32b848d470665679afc13dca5f5ae8ce6b913b` |
| CupidLD | 296,448 | `9fe3bd4fda9b87d678aa2eb6305e65b706ecdff074b16722faab23ce05cd8e02` |
| CupidObj | 375,808 | `079bc115e74772e6224e4da164115cc5696e357cca0cb1a0583985b88381cb79` |

The 2,118-byte Windows manifest has SHA-256
`ae1d3dfb10604bba419c5936884668d10595f6c671915a4ae5f16706204bb41e`.
It binds the same clean revision and source snapshot, native stage-three
producers, and Linux parent manifest
`d571125256d11dd707f661299738891edc5c1a8d3358554076875a3e0cac22d0`
from revision `bf52d135348bc33ff32e66d549bbee5edc69d8ad`.

Keep executable relocation ownership in both fixed-point behavior matrices.
Their ELF fixture contains `mov eax, imm32; ret` and one `R_386_32`
relocation at section offset zero. The instruction field starts at offset one,
so CupidDis must report one unmatched executable relocation. This raises the
Linux behavior matrix to 5 help cases, 18 successes, and 17 failures. The
native Windows matrix becomes 5 help cases, 5 successes, and 6 failures.

## Evidence

An initial 1,240.8-second Linux run from revision `4e351609` reached a clean
stage-three to stage-four fixed point. It is preliminary evidence only because
the accepted raw `EQU` correction landed afterward.

The replacement clean Linux proof from the exact `bf52d135` source closure
passed in 1,294.3 seconds. It matched all 19 C objects, the startup object, and
five tools between stages three and four. Its 42,418-byte report has SHA-256
`935cdb9c22a9d71d7db6b9de62d4bed3131a6051f96678fc8515f0682ca0cbc8`.
After promotion, the 1,473.9-second reproof matched all five initial seed
images and passed the strengthened 5/18/17 behavior matrix. Its 42,415-byte
report has SHA-256
`51a922a92e581c5613027a0bf2994b4079ee4d42a39407960060e86cecfa005e`.

The clean native Windows proof passed in 1,253.4 seconds. It matched 20 C
objects, two assembly objects, and five tools between stages three and four,
then passed the 5/5/6 behavior matrix. The old seed matched stage two for
CupidLD and CupidObj but not for CupidASM, CupidC, or CupidDis. Every native
stage-four tool also matched the independent Windows reconstruction in the
Linux report. The 35,282-byte direct report has SHA-256
`63ff9ca99cce14cdbc213900d7f2cfb01d3370fac607a5c1d68c864ce0b64970`.

The promoted Windows reproof passed in 1,061.3 seconds. All
five initial execution-seed comparisons are true, and the 20/2/5 artifact set
again passes the 5/5/6 behavior matrix. Its 35,279-byte report has SHA-256
`69f25c54092a4a705aed83610ce0910c260f52ad59a5075530865cfab2ec3278`.

The public publication test first failed because the preceding production
seed returned success for the unowned relocation. It passes with the promoted
Windows seed and confirms that the earlier object remains unchanged. Focused
manifest tests also verify that both seed snapshots match the named source
revision.

The first attempts to run the stronger matrices exposed a fixture bug. One
`struct.pack_into` call omitted its destination buffer. Both fixed-point
drivers stopped before publishing a report bundle. A runtime structural test
now validates the synthetic i386 object before the behavior-matrix shape test,
and both corrected proofs start from fresh private roots.

## Rejected alternatives

Promoting only CupidDis was rejected because each checked seed is one
five-tool trust unit. The producer trio and checked outputs must keep one
source and producer lineage.

Keeping the older Windows seed was rejected because normal Windows publication
would remain decode-only even after Linux became strict.

Weakening the publication guard or accepting a decode-only fallback was
rejected because it would discard the ownership guarantee from ADR 0290.

Building a replacement tool with GCC, Clang, or another host code generator
was rejected. Both cohorts come from the existing checked-seed fixed points.

## Consequences

Normal guarded CupidASM object publication now checks decode completeness and
executable relocation ownership on Windows and Linux. The promoted cohorts
also carry wide integer conversion and the corrected raw `EQU` rule from the
same source closure.

This promotion changes no transform owner and adds no host dependency. Python
still coordinates the fixed point and publication transactions. No active
source needs a `.c` to `.cc` rename, and the `TempleOS/` reference tree remains
unchanged and outside the progress counts.
