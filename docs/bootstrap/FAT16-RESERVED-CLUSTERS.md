# FAT16 reserved cluster allocation

The allocator's exclusive scan limit is the smaller of the represented data
extent and `FAT16_RESERVED_MIN` (`0xfff0`). It can allocate cluster `0xffef`;
reserved cluster numbers cannot satisfy an exhausted volume. Lowest-free
selection, one read per scanned FAT sector and marking every FAT copy remain
the existing allocation contract.

The regression uses a valid maximum FAT16 data extent with 65,524 represented
clusters, one sector per cluster, two 256-sector FAT copies and a 512-entry root.
Every usable entry is occupied while reserved entries remain free. The exact
four active allocator/accessor bodies run with real kernel headers and controlled
sector callbacks. The fixture maps each FAT copy separately and hashes its
active sectors, preserving the original smaller fixture's identity.

Before the fix, both native host runs fail exactly the new exhaustion case.
They make 258 reads and two writes, allocating reserved cluster `0xfff0`.
The positive `0xffef` case and all thirteen preceding methods pass. Capping the
scan limit is the sole allocator change; a named constant records the boundary
in the FAT16 header.

All fifteen methods now pass through native and Cupid-built callers on Windows
and Linux, for sixty caller/method combinations. Independent rereading compares
all fifteen raw mode results across the four retained programs. Exhaustion makes
256 reads and no writes. Both FAT copies retain their complete hash,
`3546532869`; allocating the final usable cluster makes 258 reads and two writes.
The original controlled HomeFS replacement still makes 9,708 reads and 634
writes, with table hash `3810730861`.

The qualified CupidC commands produce the same 15,652-byte contract object,
SHA-256 `abeb8fff6a02d6b909600af8cb21bdc97261c633ad8f1e3641dcfbb463f06882`.
CupidASM and CupidLD build the callers with the unchanged original hosted
runtimes. CupidDis certifies known instructions, local targets and code anchors
in all three objects on each host.

The first Windows collector compares native CRLF output directly with Cupid's
LF output and stops after the passing full suite and first raw mode. A separate
collector retains those original passing compilation, certification and suite
receipts, then compares every raw mode after removing only the native carriage
return. The original failure remains retained. No production command changes.

The full frontier command compiles all 156 sources twice in 2,236.819 seconds,
within the original 2,340-second deadline. Independent rereading verifies all
312 ELF32 objects, 474 captured source controls, compiler authority and manifest
provenance. Only the FAT allocator object changes: it grows by 48 bytes to
65,720 bytes, SHA-256
`77f2f571d26b4750911c6c14610e35885bec4f1972ec3869ba10dc3226d1c4ef`.
The complete object total is 4,421,620 bytes. The input snapshot changes only
the two FAT16 source files and has SHA-256
`c1b09132179591081e77d5b0e7b3d9696e43ae9ae22ec32c19c9568ea5477c41`.

All nineteen original post-build assertions pass against the retained full
frontier after those three measured locks are refreshed. This assertion replay
does not count as another compiler invocation. The first external controller
uses the preceding 469-input count and stops before compilation; its corrected
controller retains all 474 inputs. The first independent checker compares a
list with the driver's tuple; correcting that representation comparison permits
the complete object checks. These failures remain retained.

All 35 existing CLI, capture, failure and publication methods pass on each host,
for seventy executions without skips. This selection excludes the separately
accepted full compilation. Its first controller expects the earlier receipt's
top-level input field and stops before running tests. The corrected controller
reads the retained frontier's `input_snapshot.files`; the test selection,
captured source closure and compiler deadlines remain unchanged.

Evidence is `kernel-reserved-contracts-paired-independent.json`,
`kernel-reserved-frontier-independent.json` and
`kernel-reserved-frontier-post-build-assertions.json` under
`cupid-native-iso-proof-20261005`, with the original red captures and complete
green captures.

Independent paired production checking now verifies all 429 objects, sixteen
artifacts, six unchanged user products, the complete ABI and full 200 MiB image.
All 156 frontier objects match both production cohorts. The only changed
objects are FAT, the manual wrapper and generated kernel symbols. Both hosts
produce the same 9,599,128-byte raw kernel and embed the 164,948-byte manual
exactly once in each of the three kernel outputs. The raw size grows by 616
bytes; final and pass-one ELF sizes remain 9,826,748 and 9,695,676 bytes. The old
policy fails its raw-size row before that sole measured row is updated. The
complete preserved image has SHA-256
`cf5f4ded28e6c14fa3a6e896ff96c39eed00f7ad96ba48c13232fb3938248e3f`.
The entire FAT suffix remains byte-identical to the accepted ISO handoff image.
All four strict private four-CPU max/e1000 sessions pass completed ls/SMP and
feature 17 ISO checks under the original 150-second limits.

Canonical audit generation and reproducibility checking pass. They retain 780
active inputs, 255 features and 452 transforms, with 449 CupidBuild actions and
three Python actions. Generated preprocessing cases stay byte-identical, so
the accepted 474-input full frontier remains current. The closed private
frontier products are moved outside the OS root only after complete identity
checking; their 312 objects cannot pollute the production object's inventory.
The user target verifies the native ABI and retains six previously accepted
user products; it does not repeat their compiler invocations.

Production evidence is `kernel-reserved-os-paired-independent.json`, its two
host receipts, the original command closures and strict serial logs. The normal
tool ownership count and qualified fifteen-file seed cohort do not change.
`TempleOS/` remains read-only reference material and is excluded.
