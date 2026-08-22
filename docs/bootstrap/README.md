# Cupid Toolchain bootstrap

This directory records Cupid OS's move from a host-produced bootstrap to the
current checked-seed build and the remaining work toward a native,
Python-free fixed point. [GitHub issue #13](https://github.com/cupidthecat/cupid-os/issues/13)
is the implementation map.

Two checked seed roles are now explicit. The static i386 Linux cohort remains
the bootstrap seed because its manifest carries the complete build plan and
fixed-point provenance. The native i386 Windows PE32 cohort is an execution
seed promoted from matching stage-four outputs. Windows selects it for
output-bearing production commands, so those CupidC, CupidASM, CupidDis,
CupidLD, and CupidObj calls no longer cross WSL. Linux fixed-point
reconstruction and the static Linux Toolchain contract paths still use the
Linux seed. Those paths include the `CUPMAN4` author. The
artifact-size gate keeps the Linux manifest as policy provenance but builds and
runs its checked contract from the host-selected execution seed. Windows runs
that contract, the user syscall ABI contract, and the Toolchain manifest
verifier as temporary native PE images. The `CUPMAN4` author is different: it
is always a static Linux ELF built and run by converged stage-four Linux tools,
so Windows reaches it through WSL. Both manifest modes bind the Linux
publication seed.
ADR 0272 records the seed roles, ADR 0295 records the native ABI gate, and ADR
0297 records the artifact-size contract. ADR 0302 records Toolchain manifest
verification. ADR 0303 records callback typedef parameters, ADR 0304 records
Toolchain manifest authoring, and ADR 0305 records local-target seed carriage
and production adoption. ADR 0306 records callback typedef storage in private
CupidC globals. ADR 0310 records automatic callback objects and Cupid class
method parameters. ADR 0313 records static callback initialization. ADR 0307
records raw stage-pair evidence. ADR 0314 records linked-image local-target
validation, ADR 0315 records raw callback declarations, and ADR 0316 records
Windows seed validation in `CUPSIZE2`. ADR 0317 records retained seed bytes as
the final report comparison authority. ADR 0318 records the current seed
promotion and linked-kernel adoption. ADR 0319 records direct explicit function
addresses in private callback values.

## 2026-08-22 source-current checkpoint

Checked CupidDis now validates unrelocated direct relative targets in static
ELF32 relocatable objects. It gives each executable `PROGBITS` section its own
instruction-start map, excludes relocated operand fields, and reports
outside-section or mid-instruction failures. Production CupidASM object
publication selects the rule after structural validation. Checked CupidDis
also scans linked `ET_EXEC` images across
nonoverlapping file-backed executable load regions and classifies targets
outside loaded memory, in loaded memory without file-backed executable code,
or inside an instruction. A `PT_DYNAMIC` or `PT_INTERP` header rejects the
image as outside the static certification domain. The normal kernel
transaction applies that rule to its frozen pass-one and final ELFs after the
broad 431-input scan and before CupidObj flattening. ADR 0309 records the
relocatable decoder boundary, ADR 0312 records its first carriage, ADR 0314
records the linked-image rule, and ADR 0318 records carriage and adoption.

The preceding fully poisoned OS build and integrated guest frontier remain
historical evidence. At source head, the artifact contract passed twice against
all fourteen exact artifacts. The current outputs are:

| Source-head artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/kernel.elf.pass1` | 9,366,752 | `106980d97475d36b7835395a5bbfb43eb1e71484cea631d80dfe47be1acc2ac3` |
| `kernel/kernel.elf` | 9,493,728 | `b8e4a34844190b22faf5840a06d32ef961b6835c3af028cc78e34352ffc6bf6d` |
| `kernel/kernel.bin` | 9,271,380 | `e1801128cceeb5a510671684cded5a0aef04220dfafe90fa686df963e7abf37f` |
| `cupidos.img` | 209,715,200 | `e1ae54dced2431bee00dbf6fdc256fc908407bba16dac3967bb54a99ca436fdd` |

Source-head bootstrap reporting now compares stage two with the verified bytes
in `SeedInputs.artifact_bytes`. It no longer reopens ephemeral
`SeedInputs.tools` paths after their capture directory is removed. Linux and
native Windows use one helper. The retained-byte regressions pass two tests in
0.067 seconds, the quick group passes seven in 1.339 seconds, and two drift
cases pass in 2.792 seconds. A later complete source-head run passed all 92
bootstrap tests in 2,820.626 seconds.
The current 50-input source-head digest is
`73b3fa6964292a7f0b753df3535058dd6399f5e6d8e277a082ac70ce65c79e43`.
Those source-head results preceded the current promotion. Both checked seeds
now bind that snapshot, and ADR 0318 records their reproof and production
adoption.

Private CupidC carries a file-scope function-pointer typedef signature on direct
free-function parameters, Cupid class method parameters,
declaration-initialized automatic objects, and file objects. Each automatic
declarator gets its own copy. A file object may start as `NULL`, a compatible
function designator, or the direct address of that function. It may receive a
compatible callback through checked plain assignment, make a typed indirect
call, and be cleared. A defined function
address is written directly into initialized data. A later definition is
resolved through an absolute data patch. JIT and AOT keep record-pointer
identity and supported scalar or SIMD results. Direct structure and array
results remain rejected. A named raw callback file object and direct
free-function parameter now retain the same parsed signature. Raw callback
fields, arrays, block-static objects, alias chains, computed expressions, and
raw Cupid class method parameters remain outside this boundary.

The private callback ABI module passes all 270 tests in 44.462 seconds. The
four-vCPU raw callback QEMU smoke also passes with
`[feature14-callback-raw] PASS initialized=1 parameter=1 cleared=1 reassigned=1 calls=3`.
The 32,981-byte
`tests/feature14-callback-raw-qemu.log` has SHA-256
`502152c8ae22fdb6b4a32159276de58c9368fa5c3a47a1803c2e0ca1da4873f7`.
The full GUI module passes all 126 tests in 2.260 seconds. The standalone
CupidC seeds do not contain this private parser. The active runtime proof
remains the in-OS JIT smoke; no normal AOT source requires the syntax yet.
ADR 0306 records global storage, ADR 0310 records automatic objects and method
parameters, ADR 0313 records initialized-data function-address patches, ADR
0315 records the raw forms, and ADR 0319 records direct explicit function
addresses.

The `CUPMAN4` Toolchain author consumes the publication facts plus raw
stage-three and stage-four bytes for 58 fixed-point pairs: 17 contract objects,
16 contract executables, 19 bootstrap C objects, one startup object, and five
tool images. It requires regular, nonempty, equal byte streams and hashes both
sides independently. The author derives the 17 schema-v3 object-comparison
records from those bytes, checks each executable pair against its artifact
digest and size, and derives the fixed-point summary from the exact pair
inventories. The protocol has no caller `all_equal` field. Schema
`cupid.toolchain-contracts.v3` remains unchanged.
Python makes the same 58 comparisons independently after the author succeeds
and retains pinned filesystem capture, drift checks, staging, rollback, and
atomic publication. The author is a static Linux ELF built and run by the
converged stage-four Linux tools. Windows uses WSL for this mode. Both checked
Python contract launchers resolve `tools` from this checkout before consulting
installed packages. The direct module passes 40 tests in 43.226 seconds, the
publisher passes 62 tests in 7.266 seconds, and the pinned verifier runner
executes 25 tests in 32.773 seconds with three POSIX-only skips on Windows. The
direct suite includes a checked stage-four build and run of the author. The
source graph retains 739
active inputs, 452 transforms,
255 feature requirements, and 25 accounted unreachable files. Participation
is CupidC 250, CupidObj 192, CupidASM 9, CupidLD 9, CupidDis 6, and four
Cupid-built contracts. Python participates in every transform, but none is
Python-only. The source-current schema v3 `CUPMAN4` `make -C toolchain all`
passed in 3,952.17 seconds. The Cupid author and Python oracle agreed on all 58 stage
pairs. Every stage-three object and executable matched its stage-four
counterpart. The hosted runtime passed, and live inputs stayed frozen. The
publisher wrote 21 artifacts and a 27,071-byte manifest with SHA-256
`ea41237781ef0662502dde675b94d06c92ffadd2154a5a9da8b987c0a01e5947`.
It records 70 inputs, 50 bootstrap files, 17 object comparisons, and Linux seed
manifest
`02ee58c6be6b6f9d2f2e4ab0a07e09fe180d39a18559e5ac3b5faf50078c9d20`.
Its final `CUPMAN2` verifier printed
`Cupid Toolchain manifest: ok (21 artifacts)`. The first corrected attempt
published a valid cohort but failed this last read-only verifier because WSL
found an unrelated installed `tools` package. ADR 0311 records the launcher pin
that closed the gap. An earlier
`make bootstrap-audit` run failed after 65.183 seconds because its
artifact-size recipe lock omitted the Windows seed verifier. The current graph
locks one `$(ARTIFACT_SIZE_CONTRACT)` command with
`--checked-manifest $(BOOTSTRAP_WINDOWS_SEED_MANIFEST)`. The final post-CTXT
`make bootstrap-audit` and `make check-bootstrap-audit` both pass. The
generated fixed-point inventory records failure, help, and success counts of
21/5/22 for Linux and 9/5/8 for Windows.

A pre-final-CTXT build at the preceding integrated checkpoint reached the
exact-size gate after 668.414 seconds. It
measured a 9,320,424-byte pass-one ELF and 9,224,756-byte raw kernel. The
9,447,400-byte final ELF remained exact, so only the pass-one and raw-kernel
policy rows moved. This is historical evidence. The final artifact group ran
45 tests in 2.582 seconds with four expected Windows skips, and its POSIX
runner passes all 15 tests in 0.146 seconds.

The preceding poisoned-host `make -j4 all` passed in 684.260 seconds with
`CC`, `CXX`, `CPP`, `HOSTCC`, `HOSTCXX`, `ASM`, `AS`, `LD`, `AR`, `RANLIB`,
`NM`, `NASM`, `OBJCOPY`, and `STRIP` set to invalid commands. It checked all
fourteen policy artifacts, preserved the existing FAT contents, and staged
`test_iso/hello.iso`.

| Preceding checkpoint output | Bytes | SHA-256 |
| --- | ---: | --- |
| `boot/boot.bin` | 2,560 | `46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3` |
| `kernel/smp_trampoline.bin` | 4,096 | `b738ebb68f28b9b07e330761f4e9a7898f0424ab0a3835cd6079ae7d4a189e90` |
| `kernel/kernel.elf.pass1` | 9,320,424 | `3f9a1c681fbcfb1aa453e42a9d77ed1069b9a487110c9ec22ac318d278bdd1e6` |
| `kernel/kernel.elf` | 9,447,400 | `92d4e2f890b657c9881eb2184c7f8f9f0e96b18b5b060dbabab17e7ea305b1ce` |
| `kernel/kernel.bin` | 9,224,756 | `4d53e0456d8e63e140f6dcab135765662d12df6e4a83b246409572501f3b4cbd` |
| `cupidos.img` | 209,715,200 | `43409d159d2da70feb20deccda0d79a695c6ab56d87a179fe21a66ab40c5eedd` |
| `bootstrap/artifact-size-policy.json` | 2,960 | `b23bdcb3757a7ddc2a49eeef51cad48cdbd6899f0080c75896b67ef0c665da6e` |

The private four-vCPU e1000 smoke for that checkpoint used CPU `max` and
passed in 64.601 seconds.
It printed these markers in order:

```text
[feature14-call] PASS float4=4 double2=2 nested=2 calls=6
[feature14-callback] PASS float4=4 double2=2 calls=2
[feature14-callback-typedef] PASS float4=4 calls=1
PASS feature14_simd
[cupidc] JIT execution complete (stack: 0 bytes used, peak: 0 bytes)
```

The 33,219-byte log has SHA-256
`e39a1905002c2baa483c65eb6e763f4f62907c22f8954873dbb20f4ba5a53e93`.
It has no rejection markers, and the source image stayed unchanged at the
`cupidos.img` identity above.

The preceding integrated checkpoint's first fully poisoned `make -j4 all` completed the
compile, assemble, link, flatten, and CupidDis work before the exact-size gate
rejected its three rebuilt kernel outputs. The pass-one ELF measured 9,345,464
bytes, the final ELF measured 9,472,440 bytes, and the raw kernel measured
9,251,100 bytes. The artifact contract group then ran 46 tests in 4.160 seconds
with four expected Windows skips.

After those three policy rows were updated, the repeated fully poisoned build
passed in 874.531 seconds. It checked all fourteen artifacts, preserved the
FAT contents, and staged `test_iso/hello.iso`.

| Historical integrated output | Bytes | SHA-256 |
| --- | ---: | --- |
| `boot/boot.bin` | 2,560 | `46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3` |
| `kernel/smp_trampoline.bin` | 4,096 | `b738ebb68f28b9b07e330761f4e9a7898f0424ab0a3835cd6079ae7d4a189e90` |
| `kernel/kernel.elf.pass1` | 9,345,464 | `5dbd2c5acb7b1604cf6daf6f311e88015d0762125c60920da3737d7e10d76f06` |
| `kernel/kernel.elf` | 9,472,440 | `5810ddcb963cfadb4fea3b1343bb38c17ce3f762a48f25615b3feb653f1638e3` |
| `kernel/kernel.bin` | 9,251,100 | `4014b1b2acf34be4dd7483fb8aa9e8a8b0e76eea771c83669571cbf7b66fe0e3` |
| `cupidos.img` | 209,715,200 | `31b25b6881419b1bb8a04b2b3765323b21c5706ac114af1a07b514dcdcd07ea3` |
| `bootstrap/artifact-size-policy.json` | 2,960 | `7b12be6d0dd33f9016ecb4287f5c9414e1da79ffc61e7957aab60cea94850474` |

The integrated strong private frontier used e1000, four `max` vCPUs, SMP, a
private image, and the USB fixture. It passed in 883.513 seconds. The 640x480
framebuffer changed 89,630 pixels. AC97 produced 36,877,878 stereo 44,100 Hz
frames with a peak of 25,600, and the PC speaker produced 76,251 stereo 44,100
Hz frames with a peak of 29,912. The direct-call, named-callback,
typedef-callback, global-callback, automatic-callback, and overall feature-14
PASS markers each appeared once and in order. The feature run then printed a
clean JIT completion. The 161,418-byte log has SHA-256
`bc30f5083b96a36362bec5975c0a88437c4f23515de329328bb03d8f6c3e9326`
and no rejection markers. The private smoke left the source image unchanged at
SHA-256
`31b25b6881419b1bb8a04b2b3765323b21c5706ac114af1a07b514dcdcd07ea3`.

The promoted Linux seed binds revision
`ad7305341003feaa7e630ab7fd45be0a214c4da7`, the 50-input snapshot
`73b3fa6964292a7f0b753df3535058dd6399f5e6d8e277a082ac70ce65c79e43`,
and build plan
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.
The Linux manifest is 5,573 bytes with SHA-256
`02ee58c6be6b6f9d2f2e4ab0a07e09fe180d39a18559e5ac3b5faf50078c9d20`.
Its candidate proof passed cleanly with all initial comparisons
true, 19 C objects, startup, five tools, and 5/21/20 behavior. The Windows manifest
is 2,118 bytes with SHA-256
`4d0f4f21ee307a5758b64a2fea163319f79f58287da68bb5bdc78b333cf0aad8`
and binds the same revision and snapshot. It names the Linux manifest as its
parent instead of repeating the build plan. Its candidate proof passed cleanly
with all comparisons true, 20 C objects, two assembly
objects, five tools, and 5/7/8 behavior. Both production raw-image transactions
select `--require-local-targets`. Production CupidASM object publication also
selects the object form after structural validation. The artifact-size policy
covers fourteen
outputs: four OS outputs, five Linux seed images, and five Windows seed images.
The cross-platform seed fixtures each prove one local target; active-source
tests prove all nine bootloader and four SMP targets. The exact tool identities
appear under the checked-seed section below.

The first completed run of the intermediate 86-test suite took 2,394.660 seconds and
reported one failure and four errors. The small source-tree fixtures omitted
the new Windows `publication_start.asm` and `publication_runtime.cc` inputs,
and the fixed-point report still expected the preceding four-byte CupidASM
output. One PE temporary-file read failed once; both focused PE validator cases
passed on immediate replay. The shared helper and manual fixture now carry both
publication inputs, and the report oracle expects the promoted six-byte output
with SHA-256
`95d76dfca4cb4f279611a6ea7a86202898305a4906c6c822c1bfce2ec9ecf06b`.
Six focused freeze and PE tests passed in 0.736 seconds. The isolated complete
fixed point passed in 1,187.863 seconds, with a 1,188.356-second wrapper. The
86-test suite then passed in 2,444.917 seconds. After the relocatable-object
cases were added, the historical ADR 0312 checked-seed module passed all 89
tests in 3,145.502 seconds. The complete source-head module later passed all 92
tests in 2,820.626 seconds. The current complete promotion carries the
standalone toolchain changes from that snapshot in both checked seeds. Private
in-kernel compiler changes remain outside the seed compiler.

Hosted CupidC converts decimal `float` and `double` tokens with the same fixed
1536-bit integer-ratio model as private CupidC. It rounds once at the written
width, including ties to even, subnormals, finite limits,
overflow to infinity, underflow, and signed zero. The 95-character accepted
boundary and the next-character failure both recover in one frontend job.
Shared frontend, Linear IR, and object fixtures pin the exact target bits and
little-endian constant bytes. The bounded decimal `long double` path and the
hexadecimal-floating rejection do not change. The converter has no host
floating dependency and does not move a production owner. ADR 0293 records the
language decision, ADR 0312 records its first carriage, and ADR 0318 records
the current seed identity.

Source head now adds a native Windows fixed-point driver without changing
those manifest roles. `bootstrap-windows` freezes the checked PE execution
seed and the checked Linux plan seed, derives the native build, and builds
through stage four. Stage two and stage three are transition generations;
stage three and stage four are the convergence pair. Source-stable Windows and
Linux runs stopped safely at `cupidobj_main` after 821.9 and 883.3 seconds
because the old stage-two versus stage-three comparison saw the new stack-probe
codegen. Neither run published. At that point the new generation had not yet
run to completion. The four-generation gate now compares and behavior-tests
stages three and four, with nested evidence labels enforced by the audit.
Uncapped preliminary runs passed the complete final-pair gates. Windows matched 20 C
objects, two assembly objects, and five tools in 20 minutes 43 seconds, then
passed 5/5/5 behavior cases. Linux matched 19 C objects, startup, and five
tools in 24 minutes 22 seconds, then passed 5/18/16 behavior cases. Both
reports bind the same 50-input source snapshot, SHA-256
`d8481a39e0d1c7f42779a8c9f5fc5de10d7e5b9bc4df63ce6afe9ddd9c9716da`.
Independent verification checked every reported inventory member, size, and
hash. These reports remain preliminary because they started from uncommitted
source.

The preceding Linux cohort passed its clean candidate proof from revision
`bf52d135348bc33ff32e66d549bbee5edc69d8ad` in 1,294.3 seconds. The
stage-four tools were promoted, and a 1,473.9-second reproof matched all five
initial seed images. Both runs matched the 19 C objects, startup, and five
tools between stages three and four. The reproof passed the stronger 5/18/17
matrix, including executable relocation ownership. The 5,573-byte manifest
has SHA-256
`d571125256d11dd707f661299738891edc5c1a8d3358554076875a3e0cac22d0`.

The preceding native Windows cohort passed its clean candidate proof from the same revision in
1,253.4 seconds. Stages three and four matched 20 C objects, two assembly
objects, and five tools, then passed the 5/5/6 behavior matrix. The previous
seed matched stage two for CupidLD and CupidObj but not for CupidASM, CupidC,
or CupidDis. After promotion, a 1,061.3-second reproof matched all five initial
seed images and repeated the complete artifact and behavior gates.

The current promoted Windows cohort is:

| Tool | Bytes | SHA-256 |
| --- | ---: | --- |
| CupidASM | 438,784 | `c54bb09f1eb317a23d1680da25c78a5a439bde44654ae8b908ddca11fd7e56d6` |
| CupidC | 2,613,760 | `c768223d4dcd36023e9793b65d86f7bcbd641e921d6a6febf0a255eb7a0e1002` |
| CupidDis | 415,744 | `23da2e2f5f99c1667d2a3eb459b8d22d8a37af021e7da18b7513d3d8632cb81c` |
| CupidLD | 296,448 | `9fe3bd4fda9b87d678aa2eb6305e65b706ecdff074b16722faab23ce05cd8e02` |
| CupidObj | 375,808 | `079bc115e74772e6224e4da164115cc5696e357cca0cb1a0583985b88381cb79` |

Its 2,118-byte manifest has SHA-256
`4d0f4f21ee307a5758b64a2fea163319f79f58287da68bb5bdc78b333cf0aad8`.
It binds revision `ad7305341003feaa7e630ab7fd45be0a214c4da7`, the
shared 50-input snapshot, and the native
stage-three producers. Its parent is Linux manifest
`02ee58c6be6b6f9d2f2e4ab0a07e09fe180d39a18559e5ac3b5faf50078c9d20`
from the same source revision.

The preliminary Linux report also rebuilds the native Windows behavior cohort.
Four of its five PE images matched the direct Windows proof. CupidDis had the same
387,584-byte size but different hashes: `ad6147cd426e204756ec8bf52ae85c64fff9ad39b0bc26e5744f3c421be1e9aa`
through Linux reconstruction and `07cff807224c425d686e32d54dc1ad541f57aaa624f7b736bba0f9ef5001ce6a`
through the native proof. The reconstructed plan had compiled
`cupiddis_main.cc` without `_WIN32=1`. The Windows profile now applies that
definition to all five tool mains. A compile-and-link parity test covers the
CupidDis path, and the build audit rejects a missing definition.

The fixed-point drivers also revalidate their live producer inputs at every
generation boundary and immediately before publication. Linux checks its
bootstrap manifest and all five seed artifacts. Native Windows checks both
roles separately: the PE execution manifest and artifacts, then the Linux plan
manifest and artifacts. Drift aborts the transaction without a public bundle.
Linux behavior reports inventory the stage-three and stage-four Windows
`cupiddis_main` objects, and the audit binds each compile to the object that
replaces the corresponding input in the reconstructed behavior link.

ADR 0278 records the two-manifest boundary, ADR 0279 records the additional
generation, ADR 0280 records the Linux promotion, and ADR 0281 records the
Windows promotion. ADR 0292 records the next promotion. ADR 0305 records raw
local-target carriage, and ADR 0318 records the current promotion.

The reproducible native operator path is:

```sh
make verify-windows-bootstrap-seed
make bootstrap-windows-from-seed
```

The first target verifies the separate PE execution seed and Linux plan
manifest. The second publishes a successful proof under
`build/bootstrap/checked-windows-seed`. Its dry run and two Make contract tests
pass. The clean proof and promoted-seed reproof use this operator path.

Hosted CupidC probes each page of a fixed frame larger than 4 KiB. The emitter
keeps its old one-step reservation for smaller frames, then uses page-sized
steps and a read-only touch for larger frames. This makes demand-grown stacks
safe without changing the active C source or the current PE reserve policy.
Five focused tests passed in 23.344 seconds. A broader self-host run reached
generation three and exceeded its 904-second limit without reporting a
failure. That timeout is not fixed-point evidence. ADR 0275 records the
prologue rule.

Kernel CupidASM AOT now stops at an ELF32 relocatable object. It publishes the
selected caller-priority entry symbol, and in-kernel CupidLD owns final
placement at the established text address. Fixed-image JIT remains unchanged.
CupidLD therefore joins the checked kernel object cohort, and strict CupidDis
validation now covers that object as part of the 431-input production list.
A private guest smoke assembled `/demos/hello.asm` to a 15,680-byte `ET_REL`
object, linked an 8,536-byte two-segment ELF at `0x01A00000`, ran it as PID 4,
and observed a normal exit in 79.661 seconds. ADR 0276 records the linker
handoff.

Raw CupidASM results now carry source-derived code16, code32, and data ranges
plus their `ORG` base. The hosted CLI can write a `cupid.raw-map.v1` file, and
CupidDis can consume it with `--raw --range-map`. One checked raw-image
transaction serves the SMP and bootloader callers. It owns locking, source and
seed freezing, drift checks, private candidates, publication-boundary checks,
and atomic replacement. Callers retain image and map policy. Both callers ask
CupidASM for source-derived maps. The SMP caller also requires the map bytes to
match its fixed AP startup layout before CupidDis runs. The central
eleven-test suite passed in 1.708 seconds, including direct mismatch and
live-output drift checks for both callers. Parent-replacement tests exposed a
POSIX candidate leak when private work lived below the output parent. Private
roots now live directly below the stable repository root. Both caller modules
pass all 10 tests on Windows and all 10 through WSL, including parent
replacement with no leaked candidate. The promoted Windows execution seed now
carries both options. The normal boot rule calls
`tools/hostbuild.py assemble-bootloader` with the production manifest and
`CHECKED_SEED_INPUTS`. Standalone CupidASM overrides therefore cannot replace
the checked closure. ADR 0277 records the schema, and ADR 0283 records the
production cutover.

Checked-seed CupidDis can require every constant relative call or jump in a
raw image to land on an instruction start in same-mode code. The explicit
`--require-local-targets` option requires `--require-known`; the production
seeds use its raw form. Raw reports distinguish outside-image, data,
wrong-mode, and mid-instruction targets while leaving far pointers and
indirect transfers outside the rule. The
active-source contracts freeze nine checked bootloader targets and four SMP
trampoline targets. They also corrupt one displacement in each image and
require the exact failure total. The promoted Linux and Windows seeds carry
the option, and both production transactions select it. A failure preserves
the prior output. [ADR 0300](../adr/0300-validate-local-relative-targets-in-raw-images.md)
records the source boundary, and [ADR 0305](../adr/0305-promote-and-adopt-local-relative-target-checks.md)
records carriage and adoption.

CupidDis also accepts the option for static i386 `ET_REL` input.
It scans each executable `PROGBITS` section twice and gives that section its
own instruction identity. Unrelocated constant direct targets must stay in
the section and land on a decoded start. A relocation at the operand field
excludes the target from this count; the existing relocation-ownership check
still validates the field. The typed report appends an outside-section count.
The hosted CLI rejects an outside-section or mid-instruction target. Both
active CupidASM objects pass source-head validation, including eleven excluded ISR call
relocations. Changing one context-switch displacement produces the expected
mid-instruction failure. Both promoted seeds carry this object form, and
production hostbuild selects it before publication. [ADR 0309](../adr/0309-validate-local-relative-targets-in-relocatable-objects.md)
records the source boundary. ADR 0312 records carriage and production adoption.

Checked CupidDis accepts the same option for linked i386 ELF32 input. It
scans each file-backed executable load region twice and allows a direct target
to cross regions when it lands on an instruction start. The linked report
separates targets outside loaded memory, inside loaded memory without
file-backed executable code, and inside an instruction. Overlapping executable
file ranges fail before a report is published. A `PT_DYNAMIC` or `PT_INTERP`
header rejects the image as outside the static certification domain. Typed and
CLI negative cases cover both forms. Far and indirect transfers stay outside
the count. The normal kernel transaction runs this rule over its frozen
pass-one and final ELFs before CupidObj flattening. ADR 0314 records the
decoder boundary, and ADR 0318 records carriage and production adoption.
The generated audit reports failure, help, and success counts of 20/5/21 for
Linux and 8/5/7 for Windows. Those counts describe the promoted seed proof.

Source-head CupidDis now has an explicit static executable code-anchor policy.
`--require-code-anchors` requires `--require-known` and counts `e_entry` plus
every defined `STT_FUNC` symbol. Each anchor must equal a decoded instruction
start in file-backed executable code. Function aliases remain separate report
entries. Undefined and absolute functions, along with non-function symbols,
stay outside the count. Failures distinguish an address outside file-backed
executable code from one in the middle of an instruction.

The typed contract and CLI cover valid aliases, both invalid classes, raw and
relocatable rejection, missing disassembly state, dynamic or interpreted
images, overlapping executable loads, bounded map exhaustion, and recovery in
the same job. The Linux and Windows fixed-point drivers each add a valid and an
invalid executable behavior case. The source-current audit therefore records
21/5/22 failure/help/success cases for Linux and 9/5/8 for Windows, and names
`cupiddis.elf32_code_anchors` as a source-head capability.

The checked seeds do not carry this option yet. Production keeps
`--require-known --require-local-targets` for the pass-one and final kernel
ELFs until a complete seed promotion proves the new behavior. Minimal DWARF
source information remains a separate missing format slice. [ADR 0320](../adr/0320-validate-static-elf32-code-anchors.md)
records the decision.

Source-head and checked-seed raw CupidASM have one origin and one section identity. An `equ`
preamble defines an absolute symbol without claiming implicit `.text`. The
first section-bound statement or explicit section directive claims the source
section. A second source `ORG` reports `CT6000010` at the directive. A source
may repeat its selected section, but selecting a different section reports
`CT6000011` before layout.
Both failures leave the result and output empty, and the hosted command
preserves an existing destination. ELF32 and fixed-image requests retain their
multi-section behavior. ADR 0285 records the source boundary, and ADR 0292
records its fixed-point carriage.

The active CupidASM demo contract lists the same 22 `.asm` files as the
`demos/` directory. It assembles every source twice in fixed-image mode with
implicit externs disabled, then compares the bytes and region metadata. The
`parity_gfx2d.asm` fixture supplies both fullscreen ownership calls exported
by the kernel adapter: `gfx2d_fullscreen_enter` and
`gfx2d_fullscreen_exit`. Its normal and error paths resolve through the same
public names used in Cupid OS.

The two production ELF32 assembly objects now use the same checked assembly
transaction. `assemble-cupidasm-object` freezes the source and five-tool seed,
asks CupidASM for a private candidate, applies the shared i386 relocatable
validator, and rejects an object with no executable bytes. CupidDis must then
decode every executable section byte before hostbuild may publish `isr.o` or
`context_switch.o`. Source, seed, candidate, output, and output-parent drift
all stop publication. The final 431-input kernel gate remains as an
independent whole-kernel check. The ISO spanning fixture also declares the
fixed checked-seed closure, so standalone assembler and disassembler
overrides cannot weaken any of the five production assembly edges. ADR 0286
records the object boundary.

Hosted CupidC now exposes the existing Cupid language profile as `--cupid`.
The option selects Cupid vocabulary in both preprocessing and parsing, while
`--gnu` remains an independent extension switch. Cupid mode cannot be combined
with Doom compatibility. C11 remains the default. ADR 0270 records the public
driver boundary.

The production SMP trampoline is assembled and inspected in one guarded
transaction. CupidASM writes a private `cupid.raw-map.v1` file from the active
source. Hostbuild requires the exact 16-bit code, data, 32-bit code, and
trailing-data policy, pins the accepted map through CupidDis, and publishes
only the 4 KiB candidate. Manual range arguments no longer cross this
production boundary. ADR 0271 records the fixed layout, and ADR 0308 records
the source-derived handoff.

CupidASM now accepts `align POWER_OF_TWO[, FILL_BYTE]` in the shared source
path. Raw output aligns `ORG + output offset`. ELF32 output aligns the current
section offset, updates `sh_addralign`, and keeps later labels and relocations
at their padded offsets. Fixed images align absolute region addresses, even
when a caller supplies an unaligned base. The optional fill defaults to zero;
NOBITS padding is logical only and rejects a nonzero fill. The FPU demo uses
this statement for its 16-byte FXSAVE area instead of relying on its position
at the front of `.data`. ADR 0197 records the boundary.

The private in-kernel CupidC compiler now gives runtime unary signs a typed
scalar path. Unary plus preserves `char`, `int`, `float`, or `double`
operands, with integer promotion where required. Unary minus emits `NEG` for
the integer path. For `float` and `double`, it spills XMM0, toggles only the
IEEE-754 sign word, reloads the original width, and restores the stack.
`feature13_double.cc` checks negative values at both widths, the exact
binary32 negative-zero payload, unary plus, rejection of a string operand,
and same-job recovery. The GUI command frontier requires that evidence before
JIT completion. Its one expected compiler diagnostic is scoped to that
completed command. An earlier copy, a second copy, or the same text outside
the feature run fails the gate. A host oracle compiles the emitter helpers
directly from `kernel/lang/cupidc_parse.cc`, checks their exact instruction
bytes, and interprets them against binary32 and binary64 payloads. ADR 0189
records this private-compiler decision.

The same private compiler now accepts all six scalar floating comparisons.
Matching widths use `UCOMISS` or `UCOMISD`; a mixed `float` and `double` pair
compares as `double`. Explicit parity checks make every unordered relation
false except `!=`. The result is a normalized `int` in EAX. A compiled host
oracle checks exact instruction bytes and interprets ordered values, signed
zero, subnormals, infinities, quiet NaNs, and signaling NaNs.
`feature13_double.cc` proves the behavior inside the complete four-vCPU
frontier. ADR 0192 records the decision.

Private CupidC now converts a scalar floating value to C truth before every
truth-consuming control path. Unary `!`, `if`, `?:`, `while`, `for`, and
`do ... while` share one lowering helper. It compares XMM0 with zero, treats
both signed zero encodings as false, and treats every nonzero value, infinity,
and NaN as true. Void expressions, structures by value, and SIMD vectors fail
with `truth test requires a scalar operand`. The exact-byte oracle covers
binary32 and binary64 zero, subnormal, finite, infinite, quiet-NaN, and
signaling-NaN payloads. The guest feature covers all six parser sites. ADR
0193 records this private-compiler boundary.

The private compiler's 557 kernel bindings publish the same result type as
their local function-pointer declarations. The table contains 208 promoted
integer, 41 unsigned-word, 25 `float`, 25 `double`, 19 character-pointer,
eight other-pointer, and 231 `void` results. The unsigned group covers every
`uint32_t`, `size_t`, and `swap_handle_t` result, while `uint8_t` and
`uint16_t` results keep their integer promotion. `BIND` is reserved for the
`void` group, while `BIND_T`
records every value result. A source-contract test parses the complete table,
checks its exact size, and rejects an untyped non-void fixture. This prevents
a returned control value from being mistaken for a `void` expression.

Private CupidC also supports prefix and postfix `++` and `--` on scalar
`float` and `double` lvalues. One typed helper serves expression updates,
statement shortcuts, and `for` increments. Direct locals, parameters, and
globals use their symbol storage. Pointer dereferences, indexed elements, and
scalar record fields keep one evaluated address until the store completes.
The helper converts integer one into XMM1 at the operand width and updates
XMM0. A postfix expression holds the exact old payload in XMM2 until the new
value is stored. Aggregate values, incomplete array rows, function pointers,
and SIMD vectors remain invalid update targets. The guest first requires
`[feature13-indirect-update] PASS score=41 once=3 zero=0x80000000` from the JIT
program. It then compiles `/bin/feature13_derived_aot.cc`, loads the external
ELF, requires `[feature13-derived-aot] PASS score=41 once=2 zero=0x80000000`,
and waits for that same PID to exit cleanly. ADR 0194 records direct variables,
and ADR 0273 records derived lvalues.

Private CupidC now uses one cdecl slot model for callers, callees, and both
method-call parser paths. Integers, pointers, function pointers, `float`, and
implicit method `self` use four bytes. A `double` uses eight bytes. Arguments
are still evaluated from left to right; a shared word permutation then places
their complete slots at increasing addresses in source order without swapping
the two words inside a `double`. Callees advance parameter offsets by the same
widths, and callers reclaim the exact total. `feature13_double.cc` replaces
its copied tolerance expressions with a real `double, double, double, int`
helper used by ten checks. ADR 0198 records this private-compiler boundary.

Direct functions and methods also retain parsed fixed parameter types. Before
the cdecl word permutation, a known fixed call converts represented integer or
`char` arguments to `float` or `double`, converts between the two floating
widths, and truncates a floating value for an `int` or `char` parameter.
Represented pointer categories and integer null forms can fill a pointer slot.
A represented object pointer can fill a fixed `int` or `unsigned int` slot as
one unchanged i386 word. Narrow and floating destinations remain rejected, so
this is an address-transport rule rather than a general numeric conversion.
The existing represented pointer-category rule is unchanged. The unchanged
`/bin/ctxt.cc` call to `ctxt_parse_action` reaches this coercion boundary.
That file is an include fragment, while `/bin/notepad.cc` includes it and
passes private AOT compilation. ADR 0230 records the rule and the corrected
census interpretation.

A parsed variadic tail widens `float` to `double` and promotes `char` to `int`.
Function-pointer calls, kernel bindings, and calls without fixed parameter
metadata keep their source-width slots. A named block-local function-pointer
declaration retains fixed parameter and result types. A file-scope callback
typedef carries the same information through direct free-function parameters,
Cupid class method parameters, declaration-initialized automatic objects, and
typedef-backed global objects initialized from null or a compatible function.
Its indirect call uses the same conversions and 4-, 8-, or 16-byte slot
layout, enforces fixed arity, applies default promotions after a variadic
prefix, and publishes floating or vector results through XMM0. Empty `()`,
`void *`, record and class fields, callback arrays, block-static objects,
callback alias chains, recursive signatures, conditional
initializers, arbitrary computed expressions, raw method parameters, and
later assignment to automatic callback objects remain outside this path. A
later global target is resolved through an absolute initialized-data patch.
Named raw callback file objects and direct free-function parameters retain
their parsed signatures. The private pool holds at most 32 raw parameter
signatures and rolls back with the program.
Character operands undergo integer
promotion in ordinary integer arithmetic and use the integer SSE conversion
path when paired with a floating operand or cast.

The private AOT census covers every runnable embedded program.
Forty-six bindings expose graphics effects, bitmap-font assets, transforms,
GUI initialization, and themes already linked into the kernel. Three
accessors return the addresses of the existing constant themes; the other 43
registrations call their existing implementations directly. All 107 runnable
top-level programs pass private AOT compilation. The fixed guest frontier
runs `gfxgui_test.cc` through AOT and private JIT, then runs a nested-owner
exit fixture and a second nested owner that a foreign helper kills. The exit
fixture leaves a generation-bound delayed request behind; after PID reuse it
must reject the replacement as stale. A final AOT graphics run reuses the same
PID. This proves exit cleanup, remote-kill cleanup, and generation-safe reuse.
Serial checkpoints
prove frame 0 and frame 240, while an unresolved native symbol or explicit
fixture failure stops the gate immediately. Theme and BMP checks plus an exact custom-font pixel, an
isolated blurred-surface pixel with unchanged screen state, and center and
off-center transformed-image pixels make the setup markers meaningful. An
off-origin point checks rotation and nonuniform scale, and popping the
transform must restore identity. The affine inverse keeps the full 32.32
determinant and inverse translation arithmetic in checked 64-bit form. It
prevents the identity matrix from becoming a zero divisor, retains representable sub-word
determinants and large scales, and rejects results that cannot fit. ADR 0233
records this boundary. GodSong publishes a local settings line, then its
popup publishes a second marker after acquiring the shared writer and raw
keyboard queue. The harness waits for the ordered pair without a timed
settle, and each confirm consumes only its first terminal key.

ADR 0261 adds one owner-token handoff around the process-wide framebuffer and
gfx2d state. Desktop composition, retained paint, the legacy frame pair, and
fullscreen programs use the same boundary. Fullscreen entry waits for an
in-flight desktop writer, and process reaping releases abandoned ownership
before PID reuse. The image, font, transform, theme, and surface registries
share that writer lease. Desktop keyboard pops and mouse-driven window state
changes also borrow it while a raw modal owns input. Raw gfx2d calls and
borrowed registry pointers need an outer render scope. Abruptly killed
processes can still orphan fully published resource handles because those
pools do not yet record an owner.

The first Browser number slice consumes those private floating capabilities
directly. Its JavaScript lexer stores decimal integer, fraction, and exponent
tokens as `double`, and numeric AST nodes keep the same lane. Interpreter comparisons
no longer scale through `int`, so close and large finite values retain their
binary64 order. JavaScript truth rejects both signed zero and NaN. Division by
zero keeps its floating result, while remainder by zero produces NaN. Decimal
exponents are bounded to 400 steps and require a digit after the optional
sign. ADR 0210 records that starting point; the expanded forms and runtime
rules are described below under ADR 0218.

The Browser's five numeric tables first exposed a private compiler defect in
typed storage and indexed access. Private CupidC now carries `float` and
`double` lvalues through one-, two-, and three-dimensional fixed arrays in
global, automatic, block-static, and persistent REPL storage. Subscripts use
the remaining row stride, while unevaluated `sizeof(array[index])` reports the
whole row. Depth-one floating pointers retain their pointee width through
declarations, address expressions, returns, function and method array
parameters, dereference, subscripting, assignment, arithmetic compound
assignment, and floating increment or decrement. Direct pointer updates
advance by four or eight bytes.

Structure and class objects, object arrays, and object pointers also retain
scalar floating fields and one-dimensional fixed floating field arrays.
Bounds must be positive, and checked count-by-stride arithmetic rejects an
overflowing allocation before storage is reserved. Fresh expression metadata
prevents an unrelated pointer result from inheriting an earlier array stride.
Derived floating updates retain that lvalue metadata through pointer, index,
and member designators, evaluate the destination once, and preserve the old
raw payload for postfix. Floating pointer depth greater than one,
pointer-to-array types, and assignment through a pointer-valued floating record
field remain unsupported. Bitwise or shift compound assignment receives a
specific diagnostic. ADR 0273 records the update boundary.

Private `float4` and `double2` values now keep their vector type through
matching `+`, `-`, `*`, and `/` expressions. Fixed arrays with one, two, or
three dimensions work in global, automatic, block-static, and persistent REPL
storage. Each symbol keeps its declared rank separately from its byte strides,
so an inner extent of one cannot turn a row into a vector leaf. Outer indexes
use checked row or middle-slice strides until the final 16-byte vector leaf,
which loads and stores through the existing unaligned-safe packed path. Plain
assignment and the four arithmetic compound assignments evaluate every
destination index once. A following lane access retains the vector type.
Unevaluated `sizeof` reports the selected row or vector size without running
its index. Bounds and allocation sizes are checked before storage is reserved.

Prefix and postfix `++` and `--` now work on modifiable whole-vector objects
and fully indexed leaves. Automatic, global, block-static, and persistent REPL
direct objects use the same 16-byte storage path as vector leaves. An indexed
update keeps its computed address until the store, so every subscript runs
once. Prefix leaves the stored vector in XMM0. Postfix restores the untouched
old 128-bit payload after writing the new vector. The exact-byte emitter
contract covers packed float and double addition and subtraction by one.
Const qualification is retained through typedef aliases. Const direct vectors
and fixed-array leaves remain readable, but an assignment, arithmetic compound
assignment, or prefix or postfix `++` or `--` is rejected before any store. The
same compiler instance can then compile a valid mutable operation.

Packed arithmetic keeps the written left operand in the machine destination,
including ADD and MUL. A byte contract fixes that instruction order. The
minimum and maximum intrinsics keep x86's second-operand result for NaN and
equal signed-zero inputs. A both-NaN ADD or MUL may carry either input payload,
depending on the processor or emulator, so the runtime contract accepts only
those two known payloads and reports which one appeared. An incomplete matrix
or cube destination is rejected instead of writing the first vector in its
row. An incomplete row expression is also rejected rather than escaping as an
untyped pointer. SIMD pointer forms, record fields, `new`, array parameters,
row values, lane updates, and computed vector updates remain unsupported.

Fixed-prototype direct functions and methods now pass `float4` and `double2`
by value. Each vector occupies one complete 16-byte cdecl stack slot, packed at
four-byte granularity with the surrounding scalar slots. Argument expressions
still run from left to right; the shared word permutation keeps all four vector
words in source order. Callees load parameters with `MOVUPS`, receive an
independent copy, and return a matching vector through XMM0. Callers reclaim
the exact 4-, 8-, and 16-byte outgoing area. The private ABI does not promise
16-byte call-site alignment.

A fixed SIMD prefix is valid before scalar variadic values. SIMD variadic-tail
arguments and unprototyped SIMD calls are rejected because they have no fixed
type metadata. A named block-local function pointer with an explicit prototype
keeps its fixed types, variadic boundary, and result, so scalar and SIMD values
use the same 4-, 8-, or 16-byte call path. A direct file-scope function-pointer
typedef declaration publishes one alias. That alias keeps the same metadata
when a free-function parameter or Cupid class method parameter uses it directly,
when it declares an automatic object with an initializer, or when a file object
uses it directly. Each automatic declarator receives its own copy. A file
object accepts null initialization, checked plain assignment, indirect calls,
and null clearing. The typedef table holds sixteen aliases, and each retained
signature holds at most 32 fixed parameters. A plain function initializer,
its direct `&function` address, or a typed global assignment must match that
signature; an explicit `void *` cast
erases the check. Empty `()`, fields, callback arrays, block-static objects,
alias chains, and `void *` pointers remain metadata-free. Direct structure and array results
remain rejected, while record-pointer identity is retained.
Named local callback copies are checked too. A later target receives an
absolute address fixup, and its real definition must match any provisional
signature inferred from the initializer. Compatible conditional selection
keeps every candidate and checks each arm. A represented integer constant
expression that evaluates to zero is a null initializer; covered forms include
unary signs, casts, arithmetic, character zero, and `sizeof(int) - 4`. A
conditional keeps the proof only when every required arm is constant. Null arms
are neutral for explicit erasure, while every non-null object-pointer arm must
be cast through `void *`. Unproved scalar, mutable enum-storage, and object
values are rejected. Failed functions and methods restore emitted state,
patches, inferred signatures, labels, and control nesting. A failed source also
restores touched prototypes, definitions, kernel bindings, and a reused
`__start`, then drops its new patches. Program and REPL rollback also restore
the typedef count and signature side tables. The implicit thunk is typed
`void(void)`.
Named `_mm_*` intrinsics retain their inline lowering. Const SIMD parameters
remain readable but cannot be modified. ADR 0216 records the first fixed-array
boundary, ADR 0257 records multidimensional row descent, ADR 0294 records
whole-vector updates, ADR 0299 records the call boundary, ADR 0301 records
named local callbacks, ADR 0303 records typedef-backed free-function
parameters, ADR 0306 records typedef-backed global storage and checked
assignment, ADR 0310 records automatic objects and Cupid class method
parameters, ADR 0313 records static callback initialization, and ADR 0319
records direct explicit function addresses. The
active guest source requires
`[feature14-update] PASS direct=6 leaves=3 once=6 payload=8` and
`[feature14-call] PASS float4=4 double2=2 nested=2 calls=6`, followed by
`[feature14-callback] PASS float4=4 double2=2 calls=2`, and
`[feature14-callback-typedef] PASS float4=4 calls=1`, then
`[feature14-callback-global] PASS float4=4 initialized=1 assigned=1`
`cleared=1 calls=2`, then
`[feature14-callback-automatic] PASS local=4 method=4 calls=2` before the
overall feature-14 result. The integrated private frontier observes every
marker once and in order before the clean JIT completion. It passes in about
889 seconds from the source-current image.

A private AOT executable with no data now reports one program header and emits
only its code load segment. Code remains at file offset `0x80`. A nonempty-data
image retains two headers. The focused executable returns 17. This corrects the
ELF header without changing the established code offset.
The promoted standalone CupidC images do not include this private kernel
parser or ELF writer. Their seed reproof is evidence for CupidDis carriage,
not for the callback or AOT change.

Private decimal floating tokens now enter a fixed 1536-bit integer converter.
It forms the exact decimal ratio and rounds once to binary32 or binary64 using
nearest-even. The `f` suffix selects binary32 before rounding, avoiding a
binary64 intermediate. Decimal subnormals, the largest finite values, overflow
to infinity, and underflow to signed zero retain their expected payloads.
Numeric tokens may contain up to 95 characters, including a suffix. A longer
token is consumed as one unit, its following delimiter remains available, and
the parser keeps the first focused lexer diagnostic across recovery.
Hexadecimal floating and `long double` literals remain unsupported. ADR 0217
records the boundary.

`browser --selftest` combines direct binary64 checks with scripts sent through
the real interpreter. Decimal, hexadecimal, binary, and octal literals retain
their binary64 lane, and valid numeric separators remain between digits. The
interpreter handles complete primitive string-to-number conversion, including
the ECMAScript whitespace set, primitive loose and strict equality, UTF-16
string relations, IEEE remainder, `%=` and string `+=`. Concatenation uses the
remaining 64 KiB string pool and fails cleanly when it cannot fit the result.
Assignment resolves its binding, member receiver, or computed key once before
the right side runs. Compound stores therefore keep the original target even
when the right side replaces its receiver or advances the key. The store copy
is consumed, so a 1,100-write loop remains stack-balanced. Every binding now
records its scope owner. A declaration searches that scope alone, while an
ordinary read still follows parent scopes; nested right-side calls can no
longer interleave their bindings into a caller-owned range. Checked value-stack
pushes abort at a fixed diagnostic and unwind expression, call, initializer,
condition, loop, and return paths to their entry depth. Deliberately exhausted
string and value pools leave targets and object-property topology unchanged,
skip a call whose arguments cannot be completed, and recover in the same run.
Lexer, `typeof`, DOM, property, and global-install paths reserve a complete
interned string before publishing it. A failed global install blocks queued
scripts. Native function IDs survive stack copies, bindings, properties, user
function arguments, and returns. Canonical array-index writes grow the
unsigned `length` lane through index 4,294,967,294. Direct length assignment
fails without changing the array, while 4,294,967,295 stays an ordinary
property.
Finite formatting no longer narrows a large integer part to signed `int`; the
self-test pins 4,294,967,295, `1e20`, and `1e-7`.
Ten malformed forms receive specific lexer diagnostics, after which a valid
script proves recovery. The PASS marker contains 26 computed fields, and its
string field covers exact 600-byte `+` and `+=` results.
ADR 0210 records the first typed-array and Browser boundary; ADR 0215 records
the broader floating lvalue model; ADR 0218 records the expanded Browser lane.

The expanded self-test also crosses the private compiler's old joined-string
buffer. Private CupidC now emits adjacent string tokens directly to the data
section in automatic expressions, file-scope initializers, and persistent REPL
declarations. Each token stays within 1,023 decoded bytes, while the joined
value can use the remaining 8 MiB data section. A longer token is consumed
before a focused error, and data exhaustion fails without publishing a
truncated string. ADR 0218 records this compiler boundary too.

The same active Browser source now uses a tagged structure typedef for its
saved assignment reference. Private CupidC parses named and anonymous
structure typedef bodies through one field-layout path and retains the
structure index through aliases, aliases of aliases, and pointer aliases. The
normal parser and persistent REPL store the same metadata. Missing alias names
and incomplete by-value fields keep focused errors. Fixed arrays require a
positive count and a checked count-by-stride product. Record padding, fields,
final allocation alignment, global or REPL data reservations, and cumulative
local frames stay within signed parser limits. Signed constant arithmetic
rejects overflow; an unsigned operand wraps in the represented `uint32_t`
lane. Integer literals reject values above `UINT32_MAX`, hexadecimal literals
require at least one digit, and `u` or `U` counts inside the 95-character token
limit. REPL rollback restores complete structure records, so rejected source
cannot fill
an older forward tag. Private member address expressions now keep the selected
storage too. `&record.field` starts with the record object;
`&pointer->field` loads the pointer before adding the field offset. A private
i386 contract writes through both forms and checks the fields on either side,
while an unknown member keeps the existing focused error. ADR 0219 records
this language and allocation boundary.

Private typedef declarations now accept comma-separated value and pointer
aliases. Each declarator keeps its own pointer depth. A one-dimensional
fixed-array alias retains its checked count and element type through automatic,
global, block-static, structure, class, and persistent REPL storage. Function
and method parameters decay to element pointers, while `sizeof` keeps the
complete array type. Array fields retain that size and a record element's
identity through `.` or `->`. Reads and assignments may continue after the
index, including from an element of a record array. Unsupported array
declarator combinations fail with specific diagnostics and leave the compiler
ready for another request. ADR 0220 records the supported shape and its limits.

Private `unsigned int` values now keep their type through aliases, parameters,
returns, arrays, fields, pointers, calls, enum symbols, unary operators, and
usual arithmetic conversion. Conditional arms choose their common integer
type without depending on source order, and `sizeof` produces unsigned
`size_t`. Relations, division, remainder, and right shift use unsigned i386
behavior; `/=`, `%=`, and `>>=` follow the same rules and evaluate the
destination once. Conversion of the complete 32-bit unsigned range to
`double` is exact; the `float` path rounds from that exact value, including
for ordinary and method returns. Values in C's defined interval convert from
`float` or `double` to an unsigned word through casts, initialization,
assignment, fixed arguments, and returns. Forty kernel results publish the unsigned lane from their local
declarations. This lets the Browser store its array length as unsigned, grow
it through the canonical ECMAScript index maximum of
4,294,967,294, and leave 4,294,967,295 as an ordinary property. ADR 0221
records the compiler and Browser boundary. ADR 0249 records floating input and
remainder assignment. The feature-13 guest checks four conversion boundaries,
signed and unsigned remainder assignment, and one evaluation of a
side-effecting destination. The boot gate requires `[feature13-unsigned] PASS
conversions=4 remainders=2 once=1` and rejects separate conversion and
remainder failures.

Hosted CupidC now carries signed and unsigned eight-byte integer values through constants, matching conditional arms, fixed direct and indirect call results, object access, declared parameters, named call arguments, ellipsis arguments, and calls through function types without prototypes. File objects, block statics, fixed automatic objects, pointer dereferences, ordinary members, and indexed elements can be initialized, loaded, assigned, mutated, chained, discarded, and returned. One Linear IR entry names an emitter-owned eight-byte frame snapshot. A declared or undeclared wide argument occupies eight cdecl stack bytes. A supported wide `va_arg` read produces an instruction-owned snapshot and advances the cursor by eight. Return restores the low word to EAX and the high word to EDX.

Wide values support addition, subtraction, multiplication, division, remainder, unary plus, unary minus, bitwise complement, shifts, AND, OR, XOR, comparisons, logical operators, conditional selection, structured scalar conditions, signed or unsigned switch dispatch, all ten compound assignments, prefix and postfix update, and conversion to or from represented integer widths. Switch lowering evaluates the condition once and duplicates its snapshot handle before each full-width case comparison. Mutation evaluates its destination once and keeps one semantic load and store. Multiplication combines one full low-word product with both cross-word products. Division and remainder run a fixed 64-step restoring loop over unsigned magnitudes, then apply the quotient or dividend sign. Each multiplication, division, remainder, or wide variadic-read result receives a fresh snapshot. The unchanged `ctool_buffer_put_le64`, `ctool_buffer_patch_le64`, `pp_if_value_truth`, `pp_if_is_negative`, `pp_if_signed_less`, `pp_if_signed_magnitude`, `cfront_constant_apply_binary`, and X25519 `fe_carry` bodies guard the broader operation set. CupidASM's unchanged number parser and unary expression branch guard the arithmetic, while X25519's unchanged `fe_mul_u32` helper guards wide-by-narrow multiplication. ADRs 0065 through 0075 record these boundaries. Runtime cases that C leaves undefined promise neither a trap nor a result.

Hosted CupidC carries `float` and `double` values through object access, automatic initialization, plain assignment, discard, fixed direct or indirect calls, parameters, call results, and returns. Explicit casts and assignment conversion work in both directions between the two widths. Every represented signed or unsigned integer through 64 bits may also convert to either floating width through a cast, initialization, plain assignment, return, or fixed argument. Unary plus and minus and binary addition, subtraction, multiplication, and division accept matching or mixed floating operands. Runtime `+`, `-`, `*`, `/`, all six comparisons, and conditional selection apply the usual arithmetic conversions when the other operand or arm is any represented value integer or compatible enum. The floating operand chooses the result width, and a conditional converts only its selected arm. Inputs through four bytes use the existing SSE conversion. An eight-byte input uses x87 `FILD`, including the unsigned 2^64 correction, before storing at binary32 or binary64 width. The four arithmetic compound operators accept a floating lvalue with an integer right operand and an integer lvalue with a floating right operand. Their usual arithmetic conversion selects `float`, `double`, or `long double`; assignment conversion restores the declared left type. The same sequence covers represented integer bit fields, evaluates the destination once, and returns the stored value. Atomic mixed compound assignment remains unsupported. Every changed x87 result is immediately stored at its C width. A `float` rounds into a fresh four-byte semantic slot, while a `double` receives a fresh private eight-byte snapshot. The unchanged `libm_tanh_impl` body pins nested arithmetic with call-produced `double` values, and the complete following `float` helper slice pins the width conversions. The path also promotes `float` to `double` at ellipsis and unprototyped call positions. Calls use four-byte or eight-byte cdecl slots, floating returns use x87 `ST0`, and `va_arg(double)` advances by eight bytes.

Checked-seed hosted CupidC also accepts prefix and postfix increment and
decrement on modifiable non-atomic `float` and `double` lvalues. Linear IR
evaluates the destination once and adds or subtracts an exact-width `1.0`.
Prefix returns the stored value. A dedicated postfix store returns the old raw
`float` or `double` snapshot, preserving negative zero and NaN payloads.
Atomic floating and `long double` updates remain unsupported. ADR 0263 records
the source boundary, and ADR 0265 records checked-seed carriage.

Decimal `float` and `double` constants carry exact IEEE bits from the frontend
into linear IR. The integer-only literal parser rounds once to nearest with
ties to even. Static-duration scalar and aggregate leaves use a separate
integer-only IEEE evaluator for unary signs, addition, subtraction,
multiplication, division, all six comparisons, casts, scalar truth,
short-circuit logic, and conditional selection. It rounds after each semantic
operation at the expression's binary32 or binary64 width, converts represented
signed and unsigned integers through 64 bits, preserves signed zero, and uses
the normal `.rodata`, `.data`, or `.bss` placement policy. SSE emission covers
runtime integer-to-floating conversions through four-byte inputs,
floating-to-signed conversions, floating-to-unsigned conversions through
represented four-byte targets, an explicit non-atomic `double` to `unsigned
long long` cast, mixed represented integer and floating arithmetic, and all
six comparisons. Eight-byte integer input uses the x87 path described below
and stores the result at the requested floating width.
Matching widths use
`UCOMISS` or `UCOMISD`; a mixed pair widens to `double`. Explicit parity
handling makes every unordered relation false except `!=`. The unsigned
four-byte input path stays exact across `0x80000000` by converting its upper
31 bits and low bit separately. Four-byte unsigned output widens binary32
exactly, then splits at 2^31 before signed truncation. The unsigned-wide output
path splits the truncated value into high and low words around exact powers of
two. ADR 0250 records the runtime unsigned-output boundary.

Non-atomic automatic `long double` objects use twelve-byte storage and x87
80-bit memory forms. Bounded finite normal decimal `L` tokens round an exact
integer ratio to a 64-bit explicit significand with ties to even. The frontend
and Linear IR keep that significand and the 16-bit x87 sign and exponent;
the emitter writes three exact words to a twelve-byte snapshot before an
80-bit load. They support conversion among the three floating widths,
unary plus and minus, and addition, subtraction, multiplication, and division.
Direct and indirect fixed, variadic, and unprototyped arguments occupy twelve
cdecl bytes. Functions return a `long double` in x87 `ST0`, and direct or
indirect callers spill the result into a twelve-byte snapshot.
`va_arg(long double)` copies twelve bytes and leaves the cursor at the
following four-byte slot. Static-duration scalars, fixed arrays, and complete
records may contain non-atomic long-double leaves. Implicit initialization
zeros the complete object. Explicit leaves accept a represented integer
constant expression or a bounded decimal `L` literal with parentheses and
unary signs. The ten value bytes are exact and the two padding bytes are zero.
All-zero payloads use `.bss`; mutable nonzero payloads use `.data`, and const
nonzero payloads use `.rodata`. Atomic leaves fail recursively without
following pointers. Static initializer conversion covers `_Bool`, plain
`char`, every signed or unsigned i386 integer width, and an enum whose
compatible integer type has the represented target layout. For a nonzero
magnitude `M` with bit width `w`, the x87 significand is `M << (64 - w)`. The
high word is
`(negative ? 0x8000 : 0) | (0x3fff + w - 1)`. Long-double input uses its
explicit significand and unbiased exponent. For integer destinations other
than `_Bool`, it discards fractional bits toward zero before the range check.
`_Bool` tests the original floating value: both signed zeros convert to false,
and every represented finite nonzero value converts to true. The fixture
converts `-0.5L` to both targets, producing true for `_Bool` and zero for an
unsigned integer. Integer-valued zero keeps a `ZERO` initializer record. All
static long-double truth, all six comparisons, short-circuit logic, and
conditional selection are folded through one target-only decoder. The decoder
accepts canonical signed zero, subnormal, normal, infinity, and NaN payloads
and rejects x87 pseudo encodings. Finite `float` and `double` values widen to
exact x87 payloads, including represented subnormal results from static
arithmetic. Infinity keeps its sign, and a source NaN becomes one canonical
quiet x87 NaN. Long-double values narrow to binary32 or binary64 with
round-to-nearest, ties-to-even packing for finite values and canonical target
encodings for infinity and NaN. The result is final static data and adds no
runtime IR. Static `long double` addition, subtraction, multiplication, and
division use a separate unsigned 128-bit packer. It rounds exact intermediate
values once to the 64-bit explicit significand, with nearest-even normal
rounding and gradual underflow. The finite path covers the spacing change
below powers of two, complete 64-by-64-bit products, and division guard and
sticky bits. Special operations produce the canonical infinity or quiet NaN.
Direct quiet NaN operands are checked on both sides of every operator. All 80
shared payload oracles become final initializer records, so this
arithmetic also adds no runtime instruction. Runtime comparisons accept matching
long-double operands or a
long-double value paired with `float` or `double`. The i386 emitter loads right
then left, compares with `FUCOMIP ST0, ST1`, and removes
the surviving x87 value. Its existing parity path makes only `!=` true for an
unordered input. Runtime `float`, `double`, and automatic `long double` values
also serve unary `!`, `&&`, `||`, the controlling operand of `?:`, the
conditions of `if`, `while`, `do`, and `for`, and conversion to `_Bool`. Both
signed zeros are false; finite nonzero values, subnormals, infinities, and NaNs
are true. Runtime casts, assignments, arguments, and returns convert between
`long double` and signed or unsigned integers at 8, 16, 32, and 64 bits.
Integer input uses
`FILD`. The unsigned 64-bit correction temporarily selects 64-bit x87
precision, keeps the caller's rounding mode, and restores its saved control
word before the final store. Floating-to-integer conversion saves the
caller's control word separately, selects truncation toward zero for `FISTP`,
and restores that copy. The unsigned 64-bit path splits at `2^63`.
Runtime `+`, `-`, `*`, `/`, all six comparisons, and conditional selection
apply the same integer-to-long-double conversion to every represented value
integer and enum. Linear IR keeps the usual-arithmetic conversion on the
integer value. Conditional lowering converts only the selected arm.
Source-head decimal `float` and `double` tokens now use a 1536-bit integer
workspace. CupidC forms their exact ratios and rounds once at binary32 or
binary64 width. The path covers halfway values, subnormals, finite limits,
overflow to infinity, underflow, signed zero, and tokens through 95
characters. The public frontend, Linear IR, and object contracts inspect the
same raw payloads. Hexadecimal floating literals, hexadecimal or subnormal
long-double literals, long-double decimals beyond the bounded ratio parser,
other floating-to-wide conversions, atomic floating compound assignment,
atomic and long-double increment or decrement, SIMD, and over-aligned floating
objects remain open. ADR 0202 records the runtime truth boundary,
ADR 0256 records canonical static x87 classes, and ADR 0260 records static x87
arithmetic. ADR 0288 records the runtime integer and long-double usual
conversions. ADR 0289 records wide integer conversion and usual arithmetic
with `float` and `double`. ADR 0293 records exact hosted decimal conversion.
ADR 0296 records mixed arithmetic compound assignment.

The static object proof covers exact `1.0L`, the next represented value above
one, the largest accepted bounded literal, positive and negative zero, and
`-1.0L`. Scalar and aggregate fixtures exercise file and block scope, const
and mutable storage, exact section placement, symbol offsets, zero padding,
and deterministic repeated emission. A shared conversion fixture adds every
represented integer kind, signed and unsigned enums, both signed 64-bit
endpoints, `ULLONG_MAX`, and both `_Bool` and unsigned-integer results for
`-0.5L`. It checks exact x87 and integer bytes without emitting runtime
conversion work. Linear IR requires an unwrapped primitive base to have a
recognized standard integer kind with Cupid's canonical target size,
signedness, and alignment. The declared wrapper must match its unwrapped base
representation. An enum's compatible integer kind must be recognized and have
its fixed target size and signedness. The enum, its unwrapped base, and its
compatible type must agree on size, signedness, integer, object, and
completeness flags, as well as alignment. A `QUALIFIED` node copies referenced
alignment unless it introduces `_Atomic`. An atomic introduction at any layer
raises alignment to at least the target atomic alignment. An
`ALIGNED` node requires an explicit, nonzero power-of-two alignment and may
lower the referenced alignment. `_Bool` has one
payload bit. Linear IR also rejects bits above the target width and stray
floating, expression, string, address, or list metadata. These checks run
during whole-unit initializer ownership and block-static declaration lowering.
The hosted i386 runtime reads the three object words for every literal payload
and checks aggregate markers.
Integer-valued zero, including `sizeof(float) - 4`, keeps the established
`ZERO` metadata.

A second shared fixture covers static long-double control, canonical x87
payload classes, and floating-width conversion. Its nine objects contain 84
initializer nodes and 75 list edges. The oracles pin signed-zero truth, all six
predicates, same-exponent x87 ordering, mixed integer and enum operands through
`ULLONG_MAX`, short-circuit nonselection, selected conditional arms, exact
binary32 and binary64 conversion, infinity and NaN widening and narrowing, and
the widened result of a binary32 subnormal expression. Linear IR retains no
expression or function for the folded forest. The object proof checks exact
bytes, padding, section and symbol order, zero relocations, deterministic
repeat emission, and same-job recovery after an output limit.

A third shared fixture covers static long-double arithmetic. Its five arrays
contain 85 initializer nodes and 80 list edges. The exact payloads cover all
four operators, nearest-even ties, cancellation, signed zero, gradual
underflow, both finite boundaries, infinity, and canonical quiet NaN on
either side of each operator. Linear IR publishes no runtime work. The
1,540-byte ELF32 object holds 768 exact read-only bytes and 192 mutable bytes,
has no `.text` section or relocation, and clears both padding bytes in every
twelve-byte element. The contracts also
check deterministic replay, a one-byte-short output, exact-fit emission, both
malformed canonical-class seams, rollback, and same-job recovery.

Decoder-driven oracles check width conversion, operand order, selected IEEE
patterns, quiet and signaling NaNs, unsigned boundary values, call alignment,
frame state, and the exact 80-bit load/store inventory. The static i386
runtime executes both call-result paths, both unprototyped argument paths, and
the variadic cursor transition. Exact section-byte contracts check static
initialization without executing target code. ADR 0076 records transport, ADR
0077 records default argument promotion, ADR 0079 records the first arithmetic
boundary, ADR 0091 records width conversion and mixed expressions, ADR 0125
records decimal scalar constants and integer conversion, ADR 0136 records
static floating constant data, ADR 0137 records comparisons, ADR 0147 records
static arithmetic, ADR 0170 records the unsigned-wide cast, and ADR 0196
records the automatic `long double` slice.
[ADR 0229](../adr/0229-emit-exact-decimal-long-double-literals.md) records
the bounded decimal literal representation and automatic object proof. ADR
0251 records static long-double data, and ADR 0253 records runtime conversions
between `long double` and integers. ADR 0254 records static initializer
conversion. ADR 0255 records static control expressions and finite
floating-width conversion. ADR 0288 records runtime integer and long-double
arithmetic, comparisons, and conditional selection. ADR 0289 records the
matching wide integer boundary for `float` and `double`. ADR 0293 records
exact hosted decimal binary32 and binary64 literals.

The self-host source frontier first closed five requirements from unchanged Toolchain code. Supported structure snapshots retain nested union bytes, and a scalar member can be loaded from a returned structure snapshot. A direct four-byte literal zero can form a represented null function pointer. An object pointer can convert to a signed or unsigned eight-byte integer with a zero high word, and conversion back keeps the low word. Compatible static character and void pointers accept an ordinary string literal through parentheses and macro expansion. At that boundary, top-level union values, aggregate members from structure rvalues, nonzero function-pointer casts, function-pointer and wide-integer conversions, and arithmetic or explicit casts on static string addresses remained open. ADR 0081 records that earlier language boundary.

The refreshed checked seed keeps represented function-pointer bits through a
cast to another function-pointer type or to and from a represented 32-bit
integer.
Object-pointer interchange and narrower or wider integer forms still fail
with a feature diagnostic. ADR 0113 records the current boundary.

The exact frontend gate checks the whole hosted source frontier at its real
target ABI. It has 44 strict C11 roots and three GNU runtime roots.
`HOSTED_I386_LINUX` owns 35 strict Linux roots, which search only the
Toolchain tree and the angle-only hosted declarations. `HOSTED_I386_WINDOWS`
owns six roots with `_WIN32=1`. `FREESTANDING_I386`
owns the headerless Windows command probe. The GNU profile owns the Linux
runtime, its behavior probe, and the Windows runtime wrapper.
`HOSTED_I386_KERNEL_BRIDGE` owns `kernel/lang/as_elf.cc` and its Toolchain
contract, the two roots that may also include `/kernel/lang`. The full set
contains the 19-source static Linux tool union, the Windows runtime wrapper
and direct contract, fifteen published Toolchain contract programs, and the
separate Toolchain manifest source used for verification and authoring. With
the runtime probe, that is seventeen Toolchain contract source roots. The retired
`HOSTED_TOOLCHAIN_64` and
`HOSTED_KERNEL_BRIDGE_64` profiles have no active roots.
Stage-three and stage-four CupidC compile every contract, CupidLD links each
static executable, and the harness rejects a byte difference in any of the
seventeen new objects or sixteen executables. The publisher validates a
dedicated `cupidc-contracts` target before work and again before promotion.
An existing destination must already verify as a complete cohort. Arbitrary
directories, source trees, files, and symbolic links are rejected without
modification. The initial contract snapshot, private copy, and newly discovered
live contract inventory must match in membership and hashes. This catches additions,
removals, and a transient edit copied before its live source is restored.
Every run derives the cohort from its requested executable, requires a named
manifest artifact, and verifies all artifact hashes, the live 70-input
contract set, the checked seed manifest, and the 50-file fixed-point source
inventory before execution. The contract set includes the user syscall ABI
contract and its six declarations, both Windows runtime paths, the CupidLD
publication runtime and bridge, the direct Windows runtime contract,
`direct.h`, `windows.h`, the Toolchain Makefile, both strict C11 contract
sources, the publisher, and the independent Python ABI oracle. The seed
manifest is read once for hashing,
decoding, schema validation, and build-plan use. A concurrent replacement
cannot mix those facts across reads.

The staged `cupidasm-kernel-elf` plan carries `as_elf`, CupidLD, CupidASM,
x86, and ELF32, which matches the native contract closure. The first supported
scheduler run reached this link after the isolated object compile and exposed
the three missing implementation objects. CupidLD rejected the unresolved
strong symbol, and transactional cleanup published nothing. A direct plan
test now locks the complete closure.

Fourteen ordinary contract programs compile in the bounded worker pool with
900-second plan budgets. The pool drains before the heavyweight
`cupidc-object` program compiles alone with a 1,800-second budget. The hosted
runtime compile and all contract links retain their 360-second limits, and the
links remain parallel. Timeout errors identify the stage, source, and applied
budget. ADR 0282 records this resource policy.
The v2 publication record requires `stage-three` and `stage-four` as the
compared convergence pair. A 4,480.3-second private rebuild completed every
compile, link, comparison, and runtime check before the stale verifier rejected
that added field. The failure published nothing. Positive and wrong-pair tests
now lock the exact record.
The final supported gate passed in 4,589.9 seconds. Stage-three and stage-four
contract objects and executables matched across seventeen object and sixteen
executable comparisons. The gate ran the stage-four hosted runtime, published
and verified 21 stage-four artifacts from 65 inputs, passed the syscall ABI,
and matched all six outputs for the three native Windows user programs. The
22,591-byte contract manifest has SHA-256
`ff193cf81293553706373f5a37d0fedf3dfae0bebcbc608d892a4f40ea3d9629`.
The same target passed again in 12.2 seconds through the current-publication
path, repeating the ABI and all six user comparisons without a rebuild.

An earlier promoted-seed user frontier passed with exit 0 in 3,291.317 seconds. It
rebuilt and transactionally published the complete 21-artifact contract
cohort, with stage two equal to stage three, before repeating the three user
objects and executables. The 23-input user closure has SHA-256
`f63919f4b4307278c825ebedf99391e3ec110646042ee397dac3a7ba330435d3`.
The checked `cupid.user-syscall-abi.v1` report confirms version 5, 103 fields,
a 412-byte table, and 101 providers. The first attempt found an older
20-artifact destination and stopped before changing it. After that directory
was preserved outside the canonical path, the supported private build
published the current cohort atomically.

A fresh build in a unique output directory passed in 10.492 seconds and
reproduced the promoted frontier's six files:

| Program | Object bytes | Object SHA-256 | Executable bytes | Executable SHA-256 |
| --- | ---: | --- | ---: | --- |
| hello | 6,124 | `64e0a6ee0d7a45a0901d3db614e73481cdc6b30903345c5015601b2bf344be04` | 13,992 | `4c5622969f39ffe7c2427d65abae2d293dfbd76db2aa80c96f9e6cf01613600c` |
| ls | 7,120 | `e0627996a1d9cd6fd428642ffdfada7e07afa81d9267bc714360014af0dd3971` | 18,112 | `094b017eb6914bce6fbc1e99adeae845d5dc05280c1c1d897e68ab9d687c8d79` |
| cat | 6,292 | `ff002fc4710704c3941bf6320249e772a3448d15f99269987ab1b9b608b3acb4` | 13,992 | `b66cba4c98221f5006ad4aeee70349a82db20410e027aa863bc33fa5818b5f4c` |

Disposable staged-copy runs returned 0 for hello in 54.546 seconds, ls in
52.637 seconds, and cat in 80.043 seconds. Cat used a 62-byte marker-shaped
fixture and passed the negative serial-event boundary. The source and evidence
images remained unchanged at SHA-256
`326844ca58c1f864a6b9a2480dfaeb5ed71ec3df22cdb46da17a6bb356e7e726`.
The five-tool fixed point still contains 19 C sources because test programs
do not contribute to a tool image. ADRs 0082 through 0186 record the earlier
compiler, fixed-point, and seed boundaries. ADR 0195 records the runtime
contract rename, and ADR 0196 records the full contract transfer.

Host Python still coordinates the fixed point. Native Windows reconstruction
runs the checked PE32 cohort directly with the verified Linux plan. Windows
still uses WSL for Linux fixed-point and Linux-contract work. Both the Linux
and Windows stage-four seeds have clean proof, promotion, and promoted-seed
reproof evidence. Python-free coordination remains open.

`make verify-artifact-sizes` runs one `ARTIFACT_SIZE_CONTRACT` wrapper with
`--checked-manifest`. Host Python pins the policy, the Linux policy manifest,
all fourteen observations, and the complete Windows manifest plus five PE
files. Checked CupidC compiles the strict C11 policy contract, CupidASM
supplies startup, and CupidLD links a static ELF on Linux or a native PE on
Windows. A `CUPSIZE2` request carries the Linux manifest digest, the raw
Windows manifest, and five regular-file size and digest observations. The C
contract validates the Windows target, provenance, Linux parent link, exact
tool inventory, and observed bytes beside the policy and Linux manifest. On
Windows, the wrapper
verifies the captured PE cohort and materializes the native execution seed
from those same bytes. Python independently checks the report, rereads every
captured Windows byte sequence, and walks the pinned repository view before
success. Membership, leaf, parent, and byte replacement all fail.

The focused semantic-contract, checked-runner, and independent-policy modules
contain 22, 16, and 13 tests, for 51 total. They pass with four existing
platform-specific skips.
The source-head artifact contract later passed twice against all fourteen exact
artifacts.

The verifier is a direct prerequisite of `cupidos.img`. A failure stops the
image publisher and preserves the existing image. An intentional output change
moves its row in `bootstrap/artifact-size-policy.json` during the same review.
Missing, unknown, duplicate, linked, nonregular, and incorrectly sized
artifacts fail with direct diagnostics. The preceding artifact group ran 46
tests in 4.160 seconds with four expected Windows skips. Its POSIX runner
passed all 15 tests in 0.146 seconds. That integrated measurement build reached the
exact-size gate with changed pass-one ELF, final ELF, and raw-kernel outputs.
After those three policy rows were updated, the repeated poisoned build passed
in 874.531 seconds. It checked all fourteen paths, preserved the FAT
contents, and staged `test_iso/hello.iso`.
The 684.260-second build remains preceding checkpoint history. ADR 0267 records
the size policy, ADR 0297 records its contract transfer, and ADR 0305
established and first carried the fourteen-path closure. ADRs 0312 and 0318
carry it on their promoted seeds, with ADR 0318 current.

The proposed 20 percent Cupid-to-oracle quality comparison remains open
because no approved same-revision oracle exists. Older Windows and Linux host
`.text` measurements differ by 22.73 percent for the same revision, so
neither is a safe default. Linker capacity checks remain independent safety
gates.

The repository now provides the next boundary as working code. CupidASM assembles i386 Linux startup and system-call wrappers. CupidC compiles a narrow runtime with a reusable heap, unbuffered files, standard streams, fixed-width integers, memory and string functions, `errno`, `getcwd`, and formatted diagnostics. Its checked surface now includes `printf`, `puts`, `snprintf`, `fputc`, `fputs`, `memmove`, and `strstr`. The formatter covers the 32-bit and 64-bit integer forms and bounded strings used by the unchanged Toolchain contracts. CupidLD joins that runtime with complete CupidC-emitted closures for CupidC, CupidASM, CupidDis, CupidLD, and CupidObj. The compiler driver handles compile-only C11 or Cupid jobs, definitions, undefinitions, forced inputs, GNU and freestanding modes, and commit-gated compiler output. C11 is the default; `--cupid` selects Cupid vocabulary for preprocessing and parsing and cannot be combined with Doom compatibility. Ordered `-I` roots accept quoted and angle includes, while `--include-angle` roots accept angle includes only. Repeatable `-include` options run before the primary source in caller order. The resulting static commands run real positive and failure fixtures on Linux or through WSL. ADR 0086 records the runtime and sibling commands. ADR 0088 records the compiler driver and first compiler generation. ADR 0140 records the forced-input command boundary, ADR 0145 records the empty memory-barrier boundary, ADR 0196 records the contract cohort, and ADR 0270 records the public Cupid profile.

The unchanged CupidLD command also exposed a valid `char **` to `char *const *` call that CupidC had rejected. The frontend now recognizes that immediate qualification addition, and Linear IR keeps it as a representation-preserving conversion. Qualifier removal and the unsafe `char **` to `const char **` conversion still fail. ADR 0087 records the type boundary.

In the older detailed sections below, open references to general floating
computation, comparison, truth testing, controlling expressions, or `_Bool`
conversion are superseded by ADRs 0079 through 0202. The limits in the current
summary remain open.

Each direct or indirect call instruction owns a packed, source-ordered slice of every actual post-conversion argument type. The shared IR validator requires those slices to form one complete partition in call-instruction order, rejects metadata on non-call instructions, and checks every packed type index. The i386 emitter uses the same validator before it reads a slice. A signed or unsigned eight-byte integer, an existing `double`, or a source `float` promoted to `double` at an ellipsis or unprototyped position occupies eight bytes in the shared outgoing area. An existing `long double` occupies twelve bytes at a fixed, ellipsis, or unprototyped position. ESP remains aligned to a sixteen-byte boundary before `CALL`. Four-byte integer and pointer transport is unchanged. Atomic and aggregate values remain outside the undeclared-parameter and variadic-read boundaries. ADRs 0075 through 0077 record the IR metadata, ABI rules, and floating promotion.

ADR 0196 extends the same checked argument metadata to represented automatic
`long double` values. A direct or indirect fixed, ellipsis, or unprototyped
argument occupies twelve bytes in the outgoing area. A function result crosses
the ABI in x87 `ST0`, then a direct or indirect caller stores it in a private
twelve-byte snapshot. A corresponding `va_arg(long double)` read copies twelve
bytes and advances the cursor to the next four-byte slot.

The verified hosted suites cover the complete frontend, Linear IR, and object surfaces, with each final count recorded in the chronological log. Focused contracts cover direct and indirect variadic and unprototyped calls, checked-seed `returns_twice` call preservation, wide and floating values, all six floating comparisons, one-active-member union initializers, canonical function code generation attributes, Doom compatibility conversions, operand-bearing and operand-free assembly, empty memory barriers, pointer output, port I/O, privileged registers, FXSAVE, LDMXCSR, MOVSS, x87 sine memory, descriptor-table and segment transitions, call-next, GNU `Nd`, machine-state memory, the self-host source frontier, deterministic output, malformed metadata, constrained storage, and same-job recovery. Decoder and execution oracles check call alignment, x87 and cdecl stack balance, word order, arithmetic, width conversion, comparisons, structure snapshots, pointer bits, register preservation, cursor movement, preserved arguments, and restored frame state. The adapter gate fixes each function count, text size, object size, and text fingerprint. The tool link gate emits every closure object twice, repeats five command links and the runtime-contract link, and checks rollback and recovery. Public execution covers compilation, assembly, disassembly, linking, object wrapping, include resolution, mixed raw decode modes, missing files, runtime success paths, and useful failures.

The i386 Linux adapter objects are `ctool_host.cc` at 11 functions, 5,522 text bytes, 6,944 object bytes, fingerprint `28739C3F`, 25 symbols, and 38 relocations; `cupidasm_main.cc` at 15 functions, 11,170 text bytes, 14,568 object bytes, fingerprint `067EF556`, 64 symbols, and 104 relocations; and `cupiddis_main.cc` at 23 functions, 29,466 text bytes, 37,380 object bytes, fingerprint `39EC6F50`, 125 symbols, and 243 relocations. Their exact undefined import counts are 10, 31, and 39. Every relocation targets `.text` and has the checked `R_386_PC32/-4` or `R_386_32/0` shape. An independent `gcc -m32 -nostdinc` syntax pass accepts all three unchanged sources against the declarations.

The `ctool_host.cc` tracer applies 45 relocations, resolves 24 symbols, and leaves no undefined symbol in its static executable. Omitting the errno provider produces the exact CupidLD undefined-symbol failure with empty output and a zero result. The same job then links the original bytes again. Linux and WSL hosts with static i386 support run the tracer with exit status zero.

The current checked artifacts are CupidASM at 458,256 bytes, CupidDis at
434,548 bytes, CupidLD at 312,792 bytes, CupidObj at 392,688 bytes, and
CupidC at 2,687,436 bytes. Verification checks every hash, size, static ELF
property, target ABI, producer lineage, source revision, and build-plan field
before execution. CupidASM has SHA-256
`1eb32e11f85bb18d39a122853dfc1ad4a446ae7516e3d810c60d5f90b43fed8e`,
CupidDis has SHA-256
`56a90efcc79aef65d9bdc684cc867a4793398282ee588630ef6f451e56ee456a`,
and CupidLD has SHA-256
`a2119556894903b662d2e131a9a2436b99a3afdd1b1600a3df4d4669569a0295`.
CupidObj has SHA-256
`99111b5db7586ac4b2ed00005f2fe2e89c66ed48f007d796206b116a088cdf7a`,
and CupidC has SHA-256
`273f2621401878f673cc3d2987e267cf188ed016ac2005dc9573b3242b225094`.
The 5,573-byte manifest has SHA-256
`02ee58c6be6b6f9d2f2e4ab0a07e09fe180d39a18559e5ac3b5faf50078c9d20`.
The 19-source plan uses `.cc` throughout and has SHA-256
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.
Native GCC and Clang recipes select C with `-x c`.

The candidate proof passed from revision
`ad7305341003feaa7e630ab7fd45be0a214c4da7`. Stages three and four match all
19 C objects, startup, and five tool images. Both stages agree on five help
paths, twenty-one successful operations, and twenty useful failures. The
50-input source snapshot has SHA-256
`73b3fa6964292a7f0b753df3535058dd6399f5e6d8e277a082ac70ce65c79e43`.
All five initial seed comparisons are true after promotion. ADRs 0265, 0280,
0292, and 0312 preserve preceding promotions; ADR 0318 records the current one.
The first production gate applied strict inspection to all 427 audited root
object outputs plus the pass-one and final kernel ELFs. A
9,028-byte graph-ordered manifest carries those 429 unique paths with SHA-256
`48bdef348f6575881b9808631173e7265abc9ea89dfb84d48de72b3d2304749e`.
That separate command passed in 185.526 seconds with exit 0 and empty output.
The current source graph records 452 transforms across the three supported
roots and 443 under root `all`. Tool participation is Python 452, CupidC 250,
CupidObj 192, CupidASM nine, CupidLD nine, and CupidDis six. The four
Cupid-built contracts cover the user ABI, artifact-size policy, Toolchain
manifest verification, and Toolchain manifest authoring. It retains the
5/21/20 Linux fixed-point matrix and assigns strict validation plus flat
extraction to `kernel.bin`, with all 431 code inputs represented. The
source-current audit generated in about 115 seconds, and deterministic check
mode passed in 122.30 seconds.

The current path performs strict inspection and flattening in one hostbuild
transaction. Hostbuild freezes the selected seed manifest and all five
artifacts, the 431-entry input manifest and cohort, and the existing
`kernel.bin` boundary. Checked CupidDis validates the private cohort, then
checked CupidObj flattens the frozen final ELF into a private candidate.
Hostbuild rechecks the live trust inputs and output before parent-relative
atomic publication. Every failure preserves the prior raw kernel. The
first reviewed transaction checkpoint passed with exit 0 in 187.054 seconds and published an
8,946,332-byte `kernel.bin` with SHA-256
`4f5f2591d01bcc4007773844e9bfb8112a16dd17fbd178014cc2056fefaab67d`.
The focused hostbuild suites each passed 31 tests on Windows and in WSL;
platform-specific cases were skipped on the opposite host.

Hostbuild keeps its secure path walker beside this transaction.
`tools/cupidc_production_compile.py` has similar no-follow path handling for
compiler inputs. Moving the private flatten extraction onto the shared
pinned-path helper remains deferred maintenance. That interface must preserve
descriptor-relative POSIX opens, parent-relative Windows handles, junction
rejection, stable identity checks, and transactional publication. Both
implementations keep focused path-race tests until that interface is ready.

The poisoned-host normal root build then passed under `make -j2` in 1,057.969
seconds. `CC`, `CXX`, `CPP`, `HOSTCC`, `HOSTCXX`, `ASM`, `AS`, `LD`, `AR`,
`NM`, and `OBJCOPY` all named invalid commands. That historical build ran the
separate production strict gate before CupidObj flattened the kernel. It
produced these outputs:

| Output | Bytes | SHA-256 |
| --- | ---: | --- |
| `boot/boot.bin` | 2,560 | `46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3` |
| `kernel/kernel.elf.pass1` | 9,039,936 | `b21fa8954499a7857ee4b12fa3950fcc08ff3c6a6234c8ae72effc38c51fdc6d` |
| `kernel/kernel.elf` | 9,162,816 | `a0b57cd886369762b65d657bb3f2915ada8f30b52102535add89466eaf4f5976` |
| `kernel/kernel.bin` | 8,946,332 | `4f5f2591d01bcc4007773844e9bfb8112a16dd17fbd178014cc2056fefaab67d` |
| `cupidos.img` | 209,715,200 | `4548005bd0aa1a3cffb74620c2309d53c6b291ea2505ed187034bf6b13f1bb37` |

On 2026-08-13, a preceding poisoned-host `make -j2 all` build completed
through the checked native Windows execution seed. The command harness stopped
the first invocation after 602.5 seconds; the resumed build finished in another
968.5 seconds, for 1,571.0 seconds of cumulative build work. These artifacts
superseded the earlier identities above when this checkpoint was recorded:

| Output | Bytes | SHA-256 |
| --- | ---: | --- |
| `boot/boot.bin` | 2,560 | `46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3` |
| `kernel/kernel.elf.pass1` | 9,056,612 | `e2f63b5cd9c4e2769b9d6bc893ab5cf778951b97aec954ece6cbac0cc429e92a` |
| `kernel/kernel.elf` | 9,179,492 | `1bc06263dbf9849e6d2c594b6fb4be2a3f3b673c91f69d23a2d2e639b1f64776` |
| `kernel/kernel.bin` | 8,962,776 | `3170aa71eafa656b1f6e23c918f1f472860f513c9c5cd0376d7d4f5f8a7d891c` |
| `cupidos.img` | 209,715,200 | `3b5dd6523a90d6ed0543a6ab2464892f3289b876654f9869f88db0901940b91e` |

The exact-size prerequisite accepted all nine artifacts before image
publication. A four-vCPU RTL8139 frontier passed from this image in 820.7
seconds. All four CPUs came online. Private CupidC emitted the broad indirect
update marker, compiled `/bin/feature13_derived_aot.cc`, loaded the resulting
ELF as PID 4, emitted
`[feature13-derived-aot] PASS score=41 once=2 zero=0x80000000`, and reported
that same PID exiting. The 640 by 480 framebuffer changed 96,101 pixels. AC97
produced 33,452,396 frames at peak 25,600, and the PC speaker produced 76,614
frames at peak 31,877. USB detach/replug and the post-replug survival window
also passed. The private run left the source image unchanged.

## Previous production checkpoint

This preceding checkpoint guarded the normal boot edge with a CupidC-built
artifact-size contract. All 443 root transforms have a Cupid
participant. The first poisoned-host build reached the new gate in 695.8
seconds and rejected the embedded-manual change that made `kernel.bin` 436
bytes larger. After that one policy row moved, a complete poisoned-host rebuild
passed in 693.5 seconds. Checked CupidC, CupidASM, and CupidLD built the private
contract, its report matched the independent Python oracle, and all nine exact
artifacts passed:

The fixed SIMD call boundary then changed the private compiler, feature-14
guest, and embedded manuals. Its first poisoned-host build reached the
exact-size gate in 659.6 seconds and measured both ELFs 8,228 bytes larger and
`kernel.bin` 8,252 bytes larger. A 600-second replay allowance expired during
strict inspection and is not counted as a result. The direct contract passed
in 12.4 seconds, and an uninterrupted poisoned-host build passed in 668.5
seconds.

The named local callback boundary then expanded the private compiler,
feature-14 guest, and embedded manuals. Review discarded an intermediate image
whose build predated the settled CTXT text. The settled poisoned-host build
reached the exact-size gate in 690.910 seconds and reported only the pass-one
ELF and raw-kernel changes. All 38 artifact-size policy and semantic-contract
tests passed in 2.650 seconds, with two Windows replacement cases skipped
because pinned handles already deny those operations. A repeated poisoned-host
build passed in 692.768 seconds and published that checkpoint image.

Toolchain manifest verification runs through a separate CupidC contract.
Python gives the verifier one pinned `CUPMAN2` request containing the 21
published artifacts, exact 70-file publication inventory, 50-file bootstrap
closure, and Linux publication seed. The publisher gives author mode an
independent `CUPMAN3` fact snapshot for the same inventories plus seventeen
object comparisons. Python checks independent oracles and repeats live
membership and drift checks. Windows builds and runs native PE contracts with
the checked execution seed. Linux builds static ELFs. ADR 0302 records
verification, and ADR 0304 records authoring.

The preceding ADR 0302 Toolchain publication passed in 3,933.424 seconds. Its
21-artifact, 22,931-byte manifest has SHA-256
`8909105d516ef53d3c5081e5752fbef8596458fdfa673ec08275e7e435cd059a`.
The first final-source poisoned-host build reached only the exact-size gate in
633.542 seconds and measured `kernel.bin` 1,188 bytes above the prior policy.
After that one row moved, all 38 policy and semantic-contract tests passed in
2.784 seconds, with two Windows replacement cases skipped. The repeated
poisoned build passed in 651.193 seconds and published that checkpoint image.

| Output | Bytes | SHA-256 |
| --- | ---: | --- |
| `boot/boot.bin` | 2,560 | `46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3` |
| `kernel/kernel.elf.pass1` | 9,299,616 | `7fe8556bae4262a1c16a206182b704310791874cb9d8ac61be6b9c5f671d2b90` |
| `kernel/kernel.elf` | 9,422,496 | `8ceee43d12586d9fff73f1752940d295a2fc20e9e6364e37d7078c6ca2418027` |
| `kernel/kernel.bin` | 9,203,248 | `403034fe4d727bba0fc4ee15545b5bc6f47840c541e95761f9cdc841ce19372f` |
| `cupidos.img` | 209,715,200 | `3a2e5acc63b50d27aca68e4e7e8872adbfcab96674040a08a22c2c6aa614bebc` |

The five-sector boot image is unchanged. A private four-vCPU e1000 boot
compiled `/bin/feature14_simd.cc` through in-OS CupidC and passed the SMP
runtime contract in 61.926 seconds. The guest printed
`[feature14-call] PASS float4=4 double2=2 nested=2 calls=6`,
`[feature14-callback] PASS float4=4 double2=2 calls=2`, overall PASS, and clean
JIT completion. Its 33,483-byte log has SHA-256
`1bfea969c354abd447aada31982011082538fe1de6a9ea1dff61927bd76c73bb`.
The private run left the source image unchanged.

The preceding dual-NIC checkpoint used image SHA-256
`326844ca58c1f864a6b9a2480dfaeb5ed71ec3df22cdb46da17a6bb356e7e726`.
Both runs used the partitioned USB fixture, `--smp 4`, `--cpu max`, SMP and
frontier runtime verification, a private image, and a 300-second phase
timeout:

| NIC | Result | Framebuffer | AC97 | PC speaker |
| --- | --- | --- | --- | --- |
| E1000 | PASS, exit 0 in 725.058 seconds | 640 by 480, 103,673 changed pixels | 29,608,822 frames, peak 25,600 | 76,784 frames, peak 30,710 |
| RTL8139 | PASS, exit 0 in 725.406 seconds | 640 by 480, 106,151 changed pixels | 29,601,879 frames, peak 25,600 | 76,719 frames, peak 31,501 |

Those private-image runs left their source image unchanged.

The definitive four-vCPU boot frontiers remain evidence for the pre-freeze
image above. They passed with `--cpu max`,
SMP and frontier runtime verification, the partitioned USB fixture, a private
image copy, and a 300-second phase timeout. E1000 exited 0 in 794.034 seconds.
Its 640 by 480 framebuffer changed 103,637 pixels. AC97 produced 32,097,292
stereo 44.1 kHz frames with peak 25,600, and the PC speaker produced 78,044
frames with peak 29,866. RTL8139 exited 0 in 758.667 seconds. Its framebuffer
changed 104,964 pixels. AC97 produced 30,838,813 frames with peak 25,600, and
the PC speaker produced 76,756 frames with peak 30,161. The source image kept
SHA-256
`4548005bd0aa1a3cffb74620c2309d53c6b291ea2505ed187034bf6b13f1bb37`.

At that preceding checkpoint, the 2,666,324-byte CupidC image carried the
complete 83-root Doom frontier,
current GNU entity metadata, x87 and SSE forms, descriptor and segment
assembly, exact naked IPI entries, all file-scope effects in `libm.cc`, the
exact dglibc jump block, pointer-preserving static address casts, the
kernel-entry BSS clear with a nonzero page-aligned stack top, packed SSE2
statements, runtime floating truth, and the returns-twice direct-call boundary.
It also carries runtime and static integer to `long double` conversion,
static long-double controls and arithmetic, canonical x87 zero, subnormal,
normal, infinity, and NaN payloads through static conversion and object
output, and ordinary non-atomic `float` and `double` updates. Its
SHA-256 is
`8b6b0f0508b1565d095297f3571ef9bb4d444d19be0700165706877b210b087c`,
and its source revision is
`bf52d135348bc33ff32e66d549bbee5edc69d8ad`. CupidASM and CupidDis carry the
604-row, 249-mnemonic shared x86 catalogue with fingerprint `55A8970F`,
including signed x87 `FILD` and `FISTP` at 16, 32, and 64 bits and canonical
`SETP` and `SETNP`. CupidLD retains deterministic PE32 imports and the Windows
loader probe. CupidDis carries typed raw code and data ranges, strict
`--require-known` inspection, and ADR 0266's decoder index. The normal kernel
path applies strict inspection and flat extraction to one frozen cohort. The
392,688-byte CupidObj image retains
the complete installation-source bounds, ordering, and linked-symbol contract.
It adds transactional kernel-symbol source generation, sequential-JPEG
validation, pristine disk-template construction, deterministic ISO fixture
authoring, and deterministic `profile-manifest` authoring. Its SHA-256 is
`99111b5db7586ac4b2ed00005f2fe2e89c66ed48f007d796206b116a088cdf7a`.
[ADR 0292](../adr/0292-promote-strict-relocation-production-seeds.md)
records that strict-relocation seed. [ADR 0305](../adr/0305-promote-and-adopt-local-relative-target-checks.md)
records its promoted successor in the source-current checkpoint above. [ADR 0280](../adr/0280-promote-the-clean-stage-four-linux-seed.md)
and [ADR 0265](../adr/0265-promote-parity-floating-and-strict-inspection-seed.md)
record the preceding seeds.

The normal ISO recipe now uses that promoted command. Hostbuild freezes the
checked manifest and fixture bytes under private ordinal filenames, runs
checked CupidObj `iso-fixture` first, and compares the complete result with an
independent Python render. It rechecks the seed and live inputs, rejects output
aliases and concurrent publishers, and replaces only a regular complete
candidate. [ADR
0241](../adr/0241-publish-normal-iso-fixtures-with-cupidobj.md) records that
production transfer. The later Doom profile handoff raises CupidObj to 189
transforms. Every one of the 438 root outputs has a Cupid owner, so no root
output is Python-only. [ADR
0244](../adr/0244-publish-the-doom-profile-manifest-with-checked-cupidobj.md)
records that byte-ownership boundary. [ADR
0245](../adr/0245-let-artifact-publishers-create-output-directories.md)
records why artifact publishers also create their output directories. The
user compiler prepares them through pinned, parent-relative, no-follow
directory identities and checks the resolved output before releasing those
pins.

Checked-seed CupidObj provides `wrap-jpeg`. It validates one sequential
SOF0 or SOF1 frame, a scan, entropy stuffing and restart markers, and a
terminal EOI before applying the byte-exact binary wrapper. Twenty-one useful
rejections match the existing Python validator. The production JPEG recipe
now runs checked `wrap-jpeg` first on a private snapshot and accepts only a
regular, non-symbolic candidate. Python then checks the same frozen bytes for
acceptance and byte parity, rechecks live inputs, and publishes atomically.
The coordinator distinguishes a validator disagreement from a failed private
oracle copy, and neither failure replaces an existing object.
ADR 0231 records the source capability, ADR 0234 records seed carriage, and
ADR 0235 records the production transfer.

Checked-seed CupidObj has a transactional `disk-template` command. It accepts
the boot image, raw kernel, complete image size, and FAT partition start, then
emits the MBR through the empty FAT16 root directory. The kernel starts at LBA
5 and the result ends before cluster 2. The active geometry produces
10,697,216 bytes, which fits the existing command limit without materializing
the complete 200 MiB disk. Exact compact and active layouts, repeating FAT-size
recovery, invalid geometry, overlap, output limits, rollback, and same-job
reuse are covered. The current fixed-point behavior matrix is 5/21/20.

The normal Make image recipe now passes the checked seed manifest to
`hostbuild.py image`. Checked CupidObj runs first against frozen bootloader and
kernel inputs and writes the pristine template. Python builds an independent
oracle from the same snapshots and requires exact byte parity. A fresh image
uses the complete checked template before Python extends it and stages guest
files. A valid existing image is copied privately, then receives only the
checked bytes before the FAT partition so its persistent FAT state survives.

Python owns FAT preservation and staging, full-disk extension, parity checks,
and the publication transaction. It verifies the manifest and all five tool
hashes, freezes present inputs, records missing optional inputs, and rechecks
the live inputs and output before publication. Symbolic links, junctions,
nonregular files, and output hard-link aliases are rejected. A cross-process
lock keyed by the resolved output path rejects overlapping hostbuild
publishers.
Malformed FAT16 geometry prevents reuse, and a corrupt file chain rejects
staging. Only a fully checked candidate reaches the atomic `os.replace`
boundary.

This transfer raises CupidObj participation from 186 to 187 transforms. The
root graph now has 436 Cupid-owned transforms, up from 435, and its Python-only
outputs fall from three to two. The remaining Python-only outputs are the ISO
image and Doom input manifest. ADR 0236 records the source capability, ADR
0237 records seed carriage, and
[ADR 0238](../adr/0238-publish-normal-disk-images-from-cupidobj-templates.md)
records the production cutover.

The guarded path built a fresh 209,715,200-byte image in 672.0 seconds. Its
SHA-256 is
`8ad90a91103bf48d1e8d1e20b1b3dee48122ed1e4059b3f94cce7d750c262f16`.
A private four-vCPU `/bin/ls.cc` JIT boot passed in 61.9 seconds. The
31,989-byte serial log has SHA-256
`005a25a49b217dc3c7cfe0a788b0dd6cdda85ad12227946a7c464c8330af0ba0`
and contains no panic, fatal, assertion, exception, or triple-fault marker.
After the CTXT manuals were updated, that handoff checkpoint rebuilt the
complete kernel and reused the image in 616.648 seconds. Hostbuild
reported that it preserved the FAT data. The final image has SHA-256
`d1bfab4aed1f2116768ceed3e301fb14ffe2a36418eb4d4ebdf1108097cb2b05`.
Its private four-vCPU JIT boot passed in 66.8 seconds; the 31,764-byte log has
SHA-256
`41ed1a20ba7dbfed4965a777e655d495fc8c9ba44d7099fd4ce73ca78838d0fb`
and no failure marker.

An earlier promotion proof matched all nineteen C objects, startup, and all
five tools between stage two and stage three. It used a 43-input source
snapshot and the 5/18/16 behavior matrix. It passed in 763.5 seconds. Its
17,035-byte report has SHA-256
`810704f6701b4b4627062981e1e969332d4aa5f409d2cdce3d4fcba150518f84`.
The Windows runtime printed its exact marker and returned 37. The independent
poisoned-host reproof passed in 766.9 seconds. It matched all five promoted
seed images to stage two, then repeated the complete fixed point and behavior
matrix. Its 17,032-byte report has SHA-256
`736872f31d853fe5b2b67c25e7ec42a1893655074a1c653112def6d66fdeac87`.
Focused carriage tests cover the three changed seed images and the two
byte-identical images.
The normal Toolchain contract cohort has its own isolated publication gate.

Checked-seed CupidLD accepts this fixed-layout command:

```text
cupidld -m i386pe --text-address 0x00401000 --entry _start -o OUTPUT OBJECT...
```

It uses the existing static i386 link operation, then serializes a PE32 console
image at image base `0x00400000`. `.text` starts at RVA `0x1000`.
Nonempty `.rodata`, `.data`, and `.bss` sections follow in that order at the
next `0x1000` boundary. Empty output categories do not get PE section headers.
File alignment is `0x200`. The image reserves and commits a one MiB stack. Its
heap reserves one MiB and commits 4 KiB. Repeatable
`--import IAT_SYMBOL=LIBRARY:PROCEDURE` options append one canonical,
writable `.idata` section. Directory 1 covers its descriptors, directory 12
covers the contiguous IAT, and the other directories remain zero. Imported
slots accept only known, zero-addend `R_386_32` references. A direct call is
rejected before publication. CupidLD uses an in-place heap to order imports
and tracks selected slots without a second quadratic scan. Name imports and
the complete fixed image must stay within the two-gibibyte PE32 name-RVA
range.

The checked-seed proof assembles the small Windows entry with both CupidASM stages,
compiles its headerless `main` with both freestanding CupidC stages, and links
both objects with both CupidLD stages. The object and image pairs match. An
independent parser checks the fixed stack and heap fields, reconstructs the
exact `.idata` cursor, and confines every descriptor, thunk, string, and
directory extent to that section. On Windows,
the validated stage-two image prints the exact marker, writes no stderr, and
returns 37 before the ten-second timeout. The report retains those observed
streams and return code, the exact imports, and hashes for both object and
image pairs.

The checked Linux seed now carries the complete 5/21/20 matrix. This first
PE32 probe was not a normal-build output and did not change the ownership
census. The later five-tool PE generation is the checked Windows execution
seed for output-bearing production work. ADR 0247 records the original format
boundary. ADR 0248 records the import and Windows loader boundary, ADR 0265
records Linux-seed carriage, ADR 0272 records native execution-seed adoption,
and ADR 0274 records the current stack commitment.

Source head now extends that loader boundary to all five hosted tools.
`toolchain/hosted/i386-windows/runtime.cc` selects the Windows side of the
shared hosted runtime. It supplies command-line parsing, a `VirtualAlloc`
heap, distinct standard streams, unbuffered file reads and writes, seeking,
the current directory, and useful `errno` mapping. The matching CupidASM
startup reaches every Windows API through a cdecl wrapper and a CupidLD IAT
slot. CupidLD adds `_fullpath`, exclusive candidate creation, durable flush,
atomic replacement, and candidate deletion through a narrow publication
extension.

At the preceding promoted-seed checkpoint, stages two and three built native
CupidASM, CupidC, CupidDis, CupidLD, and CupidObj images from their matching
objects, runtime, startup, and publication bridges. That stage pair was
byte-identical. Windows ran help plus useful
success and failure cases for all five tools. CupidDis retains exact raw-report parity with the checked
Linux tool. A direct runtime image checked non-null `realloc`, allocation
failure, named-file write and append, reads, current-directory errors, and
quote and backslash parsing. CupidLD replaces a sentinel output, skips an
occupied candidate, matched the reference PE bytes, and removed every failed
candidate after a forced replacement error. That full proof passed in
801.9 seconds. Its 50-input source snapshot has SHA-256
`5bfbca2cbe30f2fa4b638cbf462b306cc05dc50a4604fd887f89426dbe091e63`.
Stage two matched stage three for all five Linux and native Windows tools. The
public Cupid mode changes Linux CupidC, and the PE stack policy changes Linux
CupidLD, so those two images differ from the older bootstrap seed. The
38,164-byte report has SHA-256
`3c63664f08e7bcdc639a88ca6ada6cf5143100eac966d748660b65d537b01e10`.
ADR 0268 records the shared runtime boundary, ADR 0269 records the native
CupidLD publisher, and ADR 0274 records the stack policy.

The matching PE generation formed the preceding checked Windows execution seed.
CupidC is 2,594,304 bytes with SHA-256
`209b493c73ff2b30ef38f0161491dacd5564f995a019876d96e8bc805b5c83e9`.
Each image reserves and commits a one MiB stack; the heap reserves one MiB and
commits 4 KiB. The independent reader checks those fields as part of the
fixed-layout PE contract. At that checkpoint, the execution manifest recorded
paired-stage provenance and the parent Linux seed but no native build plan.
The current promoted stage-four cohort and its clean evidence appear above.
Output-bearing Windows recipes run the checked PE cohort directly. Linux
fixed-point and contract paths still run the static Linux tools through WSL,
while native Windows reconstruction runs the PE cohort directly. Host Python
still coordinates both paths. ADR 0272 records carriage and production
selection, ADR 0281 records the preceding promotion, and ADR 0292 records the
current promotion.

The checked-seed CLI uses an adjacent-candidate publisher for both ELF and PE
output. It creates the candidate with exclusive-create semantics, writes and
closes it, then reopens the file and checks its size and contents before one
replacement call. Injected write, close, verification, and replacement errors
preserve a sentinel destination. Cleanup is attempted but not guaranteed. On
POSIX, CupidLD requests mode `0777`; the process umask may remove any permission
bits. The directory must remain stable under the caller's control; the CLI does
not lock or pin the destination path.

`make verify-bootstrap-seed` checks the current inputs without running them.
`make bootstrap-from-seed` performs the complete staged build, while
`make test-toolchain-fixed-point` retains the native-generation oracle.
`make -C toolchain all` builds the checked i386 contract cohort without a
host C compiler. The ISO source-capability cohort passed in 2,764.533 seconds.
Stage-two and stage-three objects and executables match, the hosted runtime
passes, and all 20 published artifacts verify. Its 18,232-byte manifest
covers 45 inputs and has SHA-256
`8cd0ea08454d9d672e6890e040fce85ba02b2c101c21599aa3933b0d89eee202`.
It records the 5,440-byte checked-seed manifest with SHA-256
`019c77d53ddaf64a382962e1d9588a60046b75a7661f70beb0da7510945f35d0`
and the 41-file source snapshot with SHA-256
`bac03a6d2b36dff48983221aae209a6688b408232b5d5373b6c2128082228a66`.
That cohort predates the seed promotion; the post-promotion bootstrap above
proves the promoted five-tool trust unit independently.
GCC or Clang is used only by the explicit `native-oracles` and hosted
development targets.
ADR 0184 moves the 83 Doom roots out of host ownership.

The checked-seed bootstrap copies the exact bytes of its 50 source inputs into
a private compiler root before it starts any built stage. CupidC receives that
root through `--root`, and all stage directories plus the behavior workspace
stay below it. The harness rehashes the private closure and the live closure
before stage two, after each stage, and after behavior checks. It also reloads
the live seed manifest and artifacts at every generation boundary and just
before publication. The native Windows path checks its execution and plan seed
roles independently. A temporary live
edit that is restored during a compile cannot affect the captured input.
Stages two through four, behavior evidence, and the report are published as one
complete directory only after every gate succeeds. A failed run leaves an
absent or empty output unchanged, and a nonempty output is rejected without
modification. ADR 0142 records this trust boundary.

The strict non-Doom root cohort gives checked-seed CupidC ownership of 156
checked-in sources and the generated kernel symbol source. All 157 sources
use `.cc`. The 83 Doom roots bring the normal checked-in total to 239, also
with no ordinary C translation unit. The five shared Toolchain roots belong
to the 19-source i386 Linux fixed point, and native GCC or Clang rules select
C with `-x c` only for optional oracles. ADR 0124 records the first 111-root
transfer, and ADR 0126 records the complete fixed-point rename and old-seed
proof. ADR 0129 records the seed promotion and lexer transfer. ADR 0135
records the Nuked OPL3 transfer, ADR 0139 records the JPEG and glyph-raster
transfer, ADR 0167 records the FPU, per-CPU, and SMP transfer, ADR 0176
records the libm transfer, ADR 0180 records the kernel entry and SIMD
transfer, and ADR 0181 records the final strict-root transfer.

The build audit finds seventeen tracked `.c` files outside `TempleOS/` and none
in a supported transform. It records seven historical copies, three
superseded implementations, one dormant runtime draft, five native host test
fixtures, and one optional host oracle. Renaming a `bin/*.c` copy would activate
it through wildcard discovery. Renaming a fixture would silently select C++
semantics and misstate its owner. The `c_source_ownership` contract rejects an
active tracked `.c` source once the graph assigns it to CupidC. Host-owned C
inputs remain valid. The contract does not assign CupidC ownership from the
`.cc` suffix. It requires a checked compile edge, the checked Toolchain
contract, or an exact runtime-delivery policy entry backed by a CupidObj edge.
The policy fixes all seventeen residual `.c` paths, all 130 source-text
deliveries, and the three unreachable `.cc` paths. A stale path, unknown
`.cc`, or host-owned `.cc` fails before publication. Active evidence remains
mandatory when an audited tree has no policy file. A nonproduction audit
accepts policy, a recorded source relation, or an explicit Make exclusion for
an unreferenced `.cc`. The complete production graph requires exact policy
coverage, while an intentionally partial production view defers that census.
The safe suffix-only rename set is empty. ADR 0284 records the first gate, and
ADR 0291 records the independent provenance contract.

Native contract evidence keeps that distinction executable. The process
fixture now supplies the four host adapters required by the current
`process.cc` cleanup path. Its 20 focused cases pass, and the combined native C
group passes all 56 kernel, USB, and ELF32 oracle cases. Production process
code is unchanged.

The checked seed now finalizes C11 inline meaning from the complete file-scope
declaration set. The ordinary declaration in `kernel/audio/nuked_opl3.h` and
the inline definition in `kernel/audio/nuked_opl3.cc` provide one external
function. Two complete kernel-profile compiles produce the same validated
40,424-byte object with SHA-256
`a3a04ade4029d9333902bb93376fb5eef21f349ee5a1406bd0751cc4cee9f2a1`.
The object defines `OPL3_Generate4Ch` and imports only `memset`. Its production
recipe freezes the source, three headers, wrapper, and checked seed inputs.
Poisoned-host coverage rejects fallback to GCC or Clang. An earlier `static`
declaration keeps a later `extern inline` definition internal. An
external-linkage inline declaration without a definition fails during
translation-unit finalization. ADR 0131 records the language boundary, ADR
0134 records the seed promotion, and ADR 0135 records the production transfer.

The wrapper freezes and verifies the seed before each compile, passes that
capture to the shared checked runner, and publishes only a validated i386
ELF32 object. The runner verifies the live manifest and all five seed images
after CupidC returns. Drift detected by that check rejects the command before
publication. A valid data-only object no longer needs a `.text` section, but
its section and symbol ranges must still pass the shared validator. The
production frontier covers 156 approved sources, and every
Make recipe names its recursive header closure and common checked controls.
Forced rebuilds poison the host compiler. The latest complete two-pass proof
predates the 156th source. It compiles 155 roots twice against a 445-file
snapshot with SHA-256
`99d03de14f544f6a76d21ed147e62018873f1e2e8dfa2f4459830b69314432c2`.
Both object passes are byte-identical; each totals 3,749,796 bytes. The combined
frontier retries only short permission-style directory locks with five
bounded delays; persistent locks and other filesystem errors publish nothing.
Its input inventory skips hidden paths under active include roots, so a
concurrent checked compile cannot add private staging headers to the frozen
repository snapshot.
The current 156-source production build passes. The broader two-pass frontier
targets 156 sources and 312 checked compilations. Its latest rerun exceeded
2,340 seconds without a compiler diagnostic, so it is not a complete frontier
pass.
The kernel-entry recipe freezes 63 recursively included headers, and the SIMD
recipe freezes seven. Poisoning `CC` leaves both recipes on the checked
wrapper. Their validated objects are 25,920 and 8,768 bytes, respectively,
and CupidDis accepts both as i386 ELF32 relocatables.
The preceding 155-root graph passes the two-link symbol and memory checks plus clean
normal and partitioned image builds. Strong four-vCPU runtime checks pass in
235.259 seconds with e1000 and 232.832 seconds with RTL8139. Both runs cover
the promoted FPU and SMP paths, RDRAND, all 62
crypto checks, USB storage, the desktop, terminal, audio output, TrueType glyph
use, an exact baseline JPEG decode, the 29-check libm guest probe with seven
focused x87 range-reduction cases, and in-OS CupidC execution.

The pass-one kernel feeds checked CupidDis symbol text into checked CupidObj,
which writes `kernel/cpu/ksyms_data.cc`. The source stores the byte-exact
symbol blob as little-endian i386 words and records its logical length
separately. This encoding replaced an earlier 638,361-byte source. The current
380,731-byte source has SHA-256
`de0acceb9f204183b903ee2e45324f1ba9a9be513cb01820804556c2d5872e6e`.
Checked CupidDis reports the same 4,718 text-symbol address/name pairs from the
pass-one and final kernels.
Hostbuild freezes the pass-one kernel and checked seed, preserves CupidDis's
exact output for CupidObj, and independently renders the expected source. It
rejects malformed rows, missing text symbols, i386 address overflow, missing
or nonregular output, oracle mismatch, and live input drift before atomic
publication. The compiler wrapper then freezes the source and its complete
header closure, gives this generated root a separate 600-second ceiling,
validates the relocatable object, rejects input drift, and publishes
atomically.

Checked-seed CupidObj provides that transactional `ksyms-source` operation. It
consumes canonical CupidDis text, retains local, global, and weak text symbols
except private `.L` labels, orders by address and input position, keeps the
first name at a shared address, and emits the exact packed blob and C source.
Malformed rows keep their line number, and input, arena, or output failure
publishes nothing. A real CupidASM object passes through CupidDis and then
CupidObj in the hosted contract. The ADR 0223 fixed-point proof covered
fifteen successful operations, thirteen failures, five help paths, exact
Python-oracle output, line-specific rejection, and preserved destinations.
The current 5/21/20 matrix retains those cases and adds later PE, x87, and
strict-decode throughput coverage. The normal two-pass link now uses this
command while retaining
Python for orchestration and independent parity checks. ADR 0222 records the
capability boundary, ADR 0223 records seed carriage, and ADR 0224 records the
production handoff.

Three generated installation tables have also left the host C compiler. The
checked CupidObj seed generates the ramfs program table, homefs document
table, and CupidASM demo table as `.cc` sources. Checked CupidC compiles them.
A separate closed wrapper compiles `hello.cc`, `ls.cc`, and `cat.cc` under the
fixed user profile, and a CupidLD wrapper links them into the two-MiB external
arena.
Linux runs the checked bootstrap seed directly. Windows builds its ABI
contract as a temporary PE with checked native CupidC, CupidASM, and CupidLD,
then runs that image without WSL. The PE and the independent Python oracle
consume one frozen snapshot of the public header, kernel types, syscall table
and initializer, VFS declarations, and socket constants. The check pins
version 5, all 103 table fields and 101 function providers, the 412-byte table,
and the shared i386 record layouts. It freezes a 26-file native build closure
and the six-file Windows execution seed separately, validates every object and
the final PE, and rechecks both live closures. It never reads or changes the
Linux Toolchain contract publication. Linux keeps the existing published ELF
contract path.

Checked native CupidC and CupidLD perform the six output-bearing Windows user
operations. The normal user build does not prepare host-built native drivers.
Both wrappers freeze their source and control inputs and pass their existing
five-tool capture to the same checked runner used by root commands. The runner
rechecks the complete live cohort after CupidC or CupidLD returns. Each wrapper
validates its ELF output and replaces an artifact only after the full operation
succeeds. The 23-input default frontier repeats all six builds from the seed.
An explicit 46-input Windows frontier also runs private native CupidC and
CupidLD snapshots and requires every output to match. A
poisoned-path test rules out GCC, Clang, `ld`, and `cc` on the normal path.
The local `user/build/` directory is generated and ignored by Git. ADR 0127
records the ABI correction and gate. ADR 0130 records the optional native
Windows path. ADR 0188 records the checked-seed default, and ADR 0295 records
the native Windows ABI contract.

CupidObj generates those three installation-table sources through one typed
public operation and the `install-source` CLI. The bin, docs, and demos modes
validate their own path categories, reject duplicate or mixed lists, cap the
combined inventory at 512 paths with overflow-safe accumulation, preserve
caller order across mixed home-asset extensions, and rewind partial output on
failure. The checked seed, source head, and Python oracle carry the limit and
ordering corrections. The active inventories already fit the limit and retain
byte-identical order.
The checked seed, source head, and Python oracle also compare the complete
wrapped symbol domain. They reject distinct paths that normalize to one linked
symbol,
including the bin and browser prefix overlap. One exact BMP path may remain in
both the docs and home lists because both entries use the same object. Every
normal installation-table recipe now enforces this guard.
The normal Make recipes invoke the checked command and depend on
`$(CUPIDOBJ_INPUTS)`.
`tools/hostbuild.py` remains the parity oracle but is no longer a prerequisite
or recipe owner for these outputs. GNU Make's ordinal inventories reproduce
the pre-transfer files and the oracle exactly:

| Table | Bytes | SHA-256 |
| --- | ---: | --- |
| bin | 46,335 | `c79edeeaf909d6c204690acd31dd56ca91be4f65ed148fa8e5e9768ac8dc1d8f` |
| docs | 9,794 | `cff3fc8943d4b1999869653b14a882d21a463471452e429b2d742d47107b13fc` |
| demos | 12,845 | `0d1f7ee032b13abbbe1767d75fe32c6f1ffa8b7014db44ae35c9d4c47ebb8305` |

The private five-tool bootstrap reached a fixed point with the 392,688-byte
CupidObj image. ADR 0201 records the operation, ADR 0204 records production
ownership, and ADR 0206 records the linked-symbol contract. ADR 0208 records
the earlier x87 seed carriage. ADRs 0243 and 0280 record preceding seeds, and
ADR 0312 records the preceding local-target seed, and ADR 0318 records the current seed.

The active-source audit classifies all three recipes as
`generate_install_source` with `cupid_object` and `host_python`. Its exact
delivery guard checks each target, mode-specific recipe, inventory markers,
and the complete checked-seed input set. Its focused positive and negative
coverage passes in 0.222 seconds, including substituted inventories and shell
text that only resembles a command. The full CupidC production module passes
all 39 tests in 25.698 seconds, and the 195-input generated frontier passes in
33.0 seconds with digest
`3819e76ac204f4b2203abe4f34539986bc3a3f68e3a60403aa09ce29f911d799`.
The full 68-test build-graph audit passes in 613.397 seconds. A normal root
build passes in 1,452.910 seconds. It produces an 8,719,780-byte kernel ELF
with SHA-256
`5a7a491a39372697accff9b678054b4bf84e2e68ffc3e882c5ef815d570cee06`
and an 8,518,280-byte raw kernel with SHA-256
`ecde61e586fb69bf091e3586c7c0a90d65588a9d7aa22ea6cf7d2f48dc341df3`.
The resulting 209,715,200-byte image has SHA-256
`f488f54c023e6d1f7e9883be1f93f705fbdab4b1de3aab8a2b61b86f3863a085`.
A private-image `/bin/ls.cc` JIT smoke passes in 54.025 seconds. Its
27,839-byte serial log has SHA-256
`631670b29e91ffe195e343a3cb957e995776b9860efb441f51ffdee4d443d55f`
and contains no panic marker.

The external syscall table records `print`, `print_int`, and `exit` events
with the running PID before using the normal console or process path. A print
event carries only its byte count and FNV-1a fingerprint, so newline or
marker-shaped caller text cannot create a second serial event. Kernel and JIT
callers still use their existing paths. Separate hello, ls, and cat boots
use private copies of one staged image and check numeric output and a
root-directory read. The cat boot also copies a fixed FAT fixture over
`/home/readme.txt` before launch, so the program keeps its normal HomeFS path
and the selected image stays unchanged. Every boot requires PID-matched
process completion.

The larger unoptimized objects need more kernel address space. The kernel
ceiling is `0x00F00000`, and the two-MiB stack occupies
`[0x00F00000, 0x01100000)`. CupidC keeps nine MiB at
`[0x01100000, 0x01A00000)`, and CupidASM keeps two MiB at
`[0x01A00000, 0x01C00000)`. The two-MiB external ELF lease now follows at
`[0x01C00000, 0x01E00000)`. FAT16 starts at LBA 20480, so the BIOS loader
may read the kernel through LBA 20479. No active runtime arena was reduced.

The renamed graph starts every discovered CPU on the four-vCPU GUI gate,
seeds the CSPRNG through RDRAND, passes all 62 crypto, ASN.1, and X.509
checks, reaches the desktop and terminal, initializes e1000, and completes
in-OS CupidC execution at `0x01100000`. The dual-NIC, audio, input
reattachment, and six EHCI lifetime gates remain part of the contract. An
isolated image gate also
loads the same external ELF program twice at `0x01C00000` and releases the
first lease before reuse.

Python still launches the compiler and guards publication. Windows runs root,
generated-table, and external-program output-bearing commands from the checked
native execution seed. Its user ABI, artifact-size, and Toolchain manifest
checks build and run private PE contracts from that seed. Linux fixed-point
and the complete published Toolchain contract cohort retain the Linux
bootstrap seed and WSL. The artifact-size policy and Toolchain manifest
verifier still read the Linux manifest as provenance. The optional native user
drivers remain host-built comparison tools.
ADR 0110 records the earlier 40-source handoff. ADR 0111 records
the 116-source expansion, data-only object rule, and memory map.
ADR 0112 records the generated-table and external-program handoff. ADR 0113
records the compiler-head source frontier. ADR 0115 records its first
production transfer and source rename. ADR 0123 records the eight-root and
generated-symbol transfer. ADR 0124 records the 111-root naming transfer and
the five deferred shared roots. ADR 0125 records decimal floating scalars,
ADR 0126 records the complete fixed-point rename, ADR 0130 records the
optional native Windows user path, and ADR 0188 records the checked-seed
default. ADR 0133 records the live ABI input check and private guest images.
ADR 0264 moves the semantic ABI check into a staged CupidC contract while
keeping the Python implementation as an independent oracle. ADR 0295 moves
the Windows contract execution to the checked PE cohort.

The checked seed accepts GNU `used` and `__used__` on file-scope objects and
functions. Compatible redeclarations merge the flag into the canonical
entity, and the Linear IR and object boundaries validate the frozen metadata.
The current emitter already writes every represented definition, so the
attribute does not change ELF32 bytes. A hermetic object contract reproduces
the generated kernel-symbol declaration with `section(".ksyms")`, `used`, and
four-byte alignment. The normal build now compiles
`kernel/cpu/ksyms_data.cc` through the checked wrapper. The current generated
symbol blob contains 114,851 meaningful bytes followed by one zero padding
byte. Its 115,264-byte ELF32 object has SHA-256
`a5eb7e848b156754dc87203e806411ed006694167b5a67dd8233d8ef9f71a65c`.
ADR 0116 records the language boundary, ADR 0122 records the seed refresh,
and ADR 0123 records the production transfer.

The checked seed accepts the exact volatile
`call 1f\n1: popl %0` statement in the stack-trace helpers. It requires one
modifiable four-byte integer `=r` output and no inputs or clobbers. Linear IR
evaluates the destination once. The emitter uses the shared x86 model to
write `E8 00 00 00 00` followed by `POP r32`, so the captured value is the
address of the pop and ESP is restored without a relocation.

The `kernel/lang/as.cc` and `kernel/lang/cupidc.cc` roots now compile
twice under the complete kernel profile to byte-identical validated i386
ELF32 objects. Their sizes are 148,056 and 288,180 bytes. Their normal object
recipes use this checked path. ADR 0118 records the language boundary, and
ADR 0123 records the production handoff.

The checked seed accepts operand-free GNU assembly in function bodies. Basic
statements and extended statements with an empty output list own an empty
operand slice and are implicitly volatile. The i386 emitter handles exact
sequences of PAUSE, NOP, STI, HLT, CLI, CLD, SFENCE, and FNINIT without a
temporary frame slot or EBX traffic. The refreshed checked seed compiles the
unchanged `e1000.cc`, `desktop.cc`, `socket.cc`, and `tcp.cc` sources in the
normal build. ADR 0099 records the language boundary, ADR 0102 records the
seed transition, and ADR 0104 records the production hand-off. The earlier
detached hybrid build proved the same four objects through both CupidLD
passes, CupidObj, and a GUI boot before the Make graph changed owner.

The checked seed retains an exact empty volatile extended template with one
`memory` clobber and no operands. Linear IR keeps the statement as an ordering
point, while the i386 emitter writes no target bytes for it. This compiles the
unchanged Doom sound driver through its production CupidC recipe. ADR 0145
records the language boundary, and ADR 0184 records the ownership transfer.

The checked seed accepts the exact width-aware port-I/O assembly in
unchanged `kernel/core/ports.h`. The six scalar helpers use AL, AX, or EAX
with DX. The word-string helpers retain modifiable EDI or ESI pointers and an
ECX count, write both final values back, and restore the callee-saved string
register. INSW accepts the source's one `memory` clobber. The exact instruction
sequences are `EC`, `EE`, `66 ED`, `66 EF`, `ED`, `EF`, `FC F3 66 6D`, and
`FC F3 66 6F`. ADR 0105 records the represented boundary, ADR 0106 records
the checked-seed refresh, and ADR 0110 records its use by the 14-source
production handoff.

The checked seed accepts the exact GNU `Nd` constraint and byte templates in
`kernel/cpu/pic.cc`. It chooses the valid `d` alternative, loads
the 16-bit port into DX, and emits `EE` for `outb %0, %1` or `EC` for
`inb %1, %0`. The focused frontend, IR, and object contracts cover malformed
constraints, invalid types, duplicate EDX ownership, forged metadata,
partial templates, deterministic output, rollback, and recovery. The full
kernel profile produces a 2,408-byte object with SHA-256
`c1855a19e0cd285953996344493dcefe916f06d89fed706219718920b4d2ea5d`.
The normal recipe now uses this object. ADR 0120 records the language
boundary, and ADR 0123 records the production handoff.

The next compiler-head slice accepts one modifiable four-byte object or
`void` pointer as the `=r` output of the exact per-CPU template
`mov %%gs:0, %0`. The frontend and IR retain its pointer type and evaluate the
destination once. The emitter assigns EAX and produces the six-byte absolute
GS load through the shared x86 model. ADR 0100 records this boundary.

The checked seed represents the privileged-register statements in
`kernel/cpu/idt.cc`, `kernel/mm/paging.cc`, and
`kernel/smp/lapic.cc`. Independent `r` inputs accept represented four-byte
integers and data pointers. Independent `c` inputs accept represented
four-byte integers and reserve ECX. Exact CR0, CR2, CR3, and CR4 moves and
RDMSR emit without a host assembler or an EBX scratch slot. Frontend, Linear
IR, and object contracts reject unsupported widths, types, fixed-register
collisions, directions, and clobbers without publishing partial state. The
three strict roots compile twice to byte-identical validated objects, and
their normal recipes use those objects. ADR 0117 records the language and
object evidence. ADR 0123 records the production handoff.

The checked seed accepts the independent `r` pointer input used by both
FXSAVE statements in `kernel/core/process.cc`. The exact volatile
template `fxsave (%0)` requires one four-byte object or `void` pointer and one
`memory` clobber. Linear IR consumes the pointer once, and the emitter uses
the shared x86 model to produce `0F AE 00` at `[EAX]` without a temporary
frame slot. A two-function fixture produces a deterministic 396-byte ELF32
object with 40 bytes of text and no relocations. The complete process source
also compiles twice under the fixed kernel profile to matching validated
30,216-byte objects. The normal process recipe uses this path. ADR 0119
records the language and object boundary, and ADR 0123 records the handoff.

The checked seed carries the SMP integer atomics that follow that load.
`__atomic_load_n`, `__atomic_store_n`, `__atomic_exchange_n`, and
`__atomic_fetch_add` accept represented one-, two-, and four-byte integer
objects with checked constant memory orders. It also accepts
`__atomic_fetch_or` at those widths. Ordinary loads and release stores use
width-correct `MOV`; sequentially consistent stores and exchanges use memory
`XCHG`; fetch-add uses `LOCK XADD`; fetch-or uses a `LOCK CMPXCHG` retry loop
that returns the old value and preserves EBX. The six order macros are
reserved target predefines in every language mode; the expressions remain
GNU-only. A decoded i386 oracle checks results, memory updates, narrow
signedness, wraparound, cdecl state, one-time operand evaluation, and a
forced competing update during fetch-or. Runtime order arguments, pointer
atomics, HLE flags, and eight-byte atomics remain open. The checked seed
carries all five operations and compiles the active EHCI fetch-or path. ADR
0107 records the language and emitter boundary; ADR 0108 records its staged
seed promotion.

USB reconciliation keeps work durable when enumeration fails or a
callback fills the queue while the poller is handling another item. One poll
attempt visits each item at most once, rotates deferred work behind its
peers, and backs retries off from 10 milliseconds to 1 second. Device
addresses from 1 through 127 and vacant block-device slots are reusable.
Failures after address assignment quarantine that address until reset,
disconnect, or stale hub work proves it safe to release.
The core marks the next root reset as mandatory while a quarantine exists.
EHCI only bypasses reset for low-speed K-state with no quarantined address;
J-state can still identify a high-speed-capable device. The controller checks
reset assertion, reset clearing, and companion ownership before reporting a
handoff that releases a quarantine.

Hub callbacks only queue observed changes. The core owns child teardown,
reset, enumeration, change acknowledgement, and the reread that catches a
new edge arriving during acknowledgement. EHCI and UHCI interrupt
registrations use controller-local generations and in-flight state.
Cancellation waits for the exact generation's callback and DMA access to
finish, while callbacks run outside the controller lock. Compiled C fixtures
exercise reconciliation, address reuse, block-slot reuse, callback reentry,
generation cancellation, and DMA quiescence. UHCI interrupt acknowledgement
excludes HCHalted, so an IRQ cannot erase the schedule-stop proof. Block
references reject saturation without wrapping. A failed mass-storage
unregister restores the attached online state for another removal attempt.
All 45 USB tests and all 123 GUI gate unit tests pass. The e1000 and RTL8139
runtime gates both pass UHCI input
reattachment and six EHCI storage lifetimes. ADR 0109 records these lifetime
and ownership rules.

The checked-seed C11 standalone-header sweep passes 161 of 164 non-Doom
inputs. `scheduler.h`, `simd_intrin.h`, and the macro-driven exact-decimal test
fixture remain exact C11-profile failures.
The checked seed parses all 29 declarations in `simd_intrin.h` under the Cupid
profile. Checked-seed CupidC still owns unchanged `kernel/smp/acpi.cc` and
`kernel/smp/mp_tables.cc` in the normal build. A four-vCPU QEMU run discovers
and starts every CPU, initializes e1000, passes all 62 crypto, ASN.1, and
X.509 checks, reaches the desktop and terminal, and completes `/bin/ls.cc`.
The optional runtime contract checks those markers together and rejects the
known SMP, storage, crypto, exception, panic, corruption, and
illegal-instruction failures. ADR 0101 records the atomic boundary, ADR 0102
records the seed transition, and ADR 0103 records the production cutover.

The first unoptimized CupidC cohort crossed the original stack boundary and
established the fixed stack and external arenas. The 116-source expansion
moves that layout upward by one MiB as described above. ADR 0093 records the
original ownership and memory-map decision, ADR 0098 records the complete
crypto cutover, ADR 0111 records the expansion, and ADR 0115 records the
current boundary.

The wide arithmetic proof now has 26 functions and 165 exact IR instructions. Its original 83-instruction prefix keeps fingerprint `245E6D8F4F77588E`. Five multiplication slices cover signed, unsigned, mixed-sign, narrow-to-wide, and chained products, while seven later slices cover signed and unsigned quotient and remainder, mixed signedness, a widened narrow divisor, and chaining. The earlier combined operation object still contains 3,156 text bytes with fingerprint `B52392EA`, 26 symbols including the null symbol, and no relocations. A separate multiplication object contains 1,103 text bytes with fingerprint `E357BE84`, seven symbols including the null symbol, and no relocations. Its decoder finds seven one-operand `MUL`, fourteen two-operand `IMUL`, six returns, and no call or divide. The multiplication oracle covers zero, identity, low-word carry, cross-word contribution, high-bit wrap, defined signed cases, mixed signedness, narrow conversion, fresh snapshots, restored stack and frame state, preserved registers, and unchanged arguments. Malformed multiplication metadata and constrained output fail transactionally and recover in the same job.

The wide-mutation fixture publishes 15 functions and 225 exact IR instructions. Its deterministic object contains 17 functions in 4,410 text bytes, has fingerprint `4B337038`, publishes 18 symbols including the null symbol, and has no relocations. Decoder and execution checks cover all ten compound operators, signed and unsigned prefix or postfix update, mixed and narrow conversion, postfix snapshot preservation, one-time indexed evaluation, volatile access, and cdecl state. Malformed metadata, atomic wide mutation, and constrained output fail without publishing partial work, and the same job reproduces the complete object.

The division and remainder object contains eleven functions, 4,775 text bytes with fingerprint `55F1A495`, twelve symbols including the null symbol, and no relocations. Thirteen operation loops each carry a fixed branch shape through unsigned high- and low-word comparison, shared subtraction, and a repeat edge. Thirty-three defined execution checks cover all four signed sign combinations, unsigned low- and high-word operands, high-bit divisors, mixed and narrow conversions, chaining, snapshot reuse, stack restoration, preserved registers, and unchanged arguments. Invalid quotient/remainder metadata, constrained output, and same-job recovery stay transactional. Undefined runtime inputs are outside the oracle. This proof changes no production owner.

The wide switch proof covers signed and unsigned controlling values, exact full-width cases, and misses that differ in only one word. Its deterministic object contains two 252-byte functions, 504 text bytes with fingerprint `DBC82148`, three symbols including the null symbol, and no relocations. The execution oracle checks the selected return, stack and frame state, preserved registers, and unchanged two-word argument. A mismatched case type, an unpromoted narrow condition, and constrained output fail transactionally. The same job can emit the exact object afterward. This proof changes no production owner.

Hosted CupidC now lowers plain assignment, all ten compound assignments, and prefix or postfix increment and decrement for represented non-atomic bit fields in four-byte storage units. Linear IR retains the graph member and evaluates the record address once. `BIT_FIELD_STORE_VALUE` returns the stored lane for assignment, compound assignment, and prefix update. `BIT_FIELD_STORE_OLD_VALUE` carries the extracted value through a postfix store. Partial fields preserve neighboring bits; a volatile 32-bit field uses one read and one direct store. Partial volatile mutation and other storage sizes remain open. ADR 0063 records plain assignment, and ADR 0064 records mutation.

Hosted CupidC carries complete fixed-size structure values through lvalue conversion, automatic expression initialization, plain and chained assignment, conditional expressions, casts to `void`, fixed direct and indirect calls, and return. One Linear IR stack entry represents an emitter-owned snapshot of the complete target bytes. `LOAD` creates that snapshot, `STORE` copies it without a result, and `STORE_VALUE` preserves it after the copy. Ordinary locals keep their binding-ordered frame slots; structure loads and structure-result calls receive private frame slots in instruction order. Supported structures have target alignment no greater than four bytes and no volatile or atomic subobject. Their copied graph may contain a union, and a scalar member can be loaded from an owned structure-result snapshot. A union used directly as a parameter or result and an aggregate member selected from a structure rvalue remain open. ADR 0049 records the value model and i386 ABI, and ADR 0081 records the nested-union and rvalue-member boundary.

The i386 call path places each structure argument inline and rounds its stack area up to four bytes. Callers zero the outgoing area before filling scalar slots and copying structure bytes, so a three-byte structure has one deterministic padding byte. A structure result uses a hidden destination pointer at `[EBP+8]`; explicit parameters start at `[EBP+12]`. The callee copies the result, returns the hidden pointer in EAX, and removes that word with `RET 4`. Structure copies preserve ESI and EDI around `CLD` and `REP MOVSB`. The deterministic proof has 928 `.text` bytes, 13 symbols, four `R_386_PC32` relocations, and FNV fingerprint `31D58B50`. The shared x86 contract, CupidASM, and CupidDis agree that `C2 04 00` is `ret 0x4`; CupidASM rejects `ret 65536`.

Hosted i386 object emission aligns ESP to sixteen bytes immediately before every direct or indirect `CALL`. A target-private control-flow pass derives the live semantic stack depth at each reachable instruction without changing public Linear IR. The emitter combines that depth with the fixed frame and any outgoing structure or wide argument area, then reserves zero, four, eight, or twelve padding bytes. Scalar calls shift completed argument words into the padded area, while structure and wide calls copy each value into its target-sized slot. The focused proof covers all four padding amounts, nested evaluation, a conditional join, a loop back edge, direct and indirect calls, structure arguments, wide arguments, and hidden structure results. Its control-flow decoder checks every reachable call, while execution or symbolic oracles verify argument values. ADR 0050 records the alignment rule, and ADR 0067 records wide slots.

Block-static objects now reach deterministic hosted ELF32 output. The lowerer validates each constant initializer and publishes no runtime initialization instructions. The emitter gives every object a local `.LBS<absolute-block-binding-index>.<source-name>` symbol, uses the same `.rodata`, `.data`, or `.bss` rules as file objects, and emits runtime addresses through `R_386_32`. Shadowed names remain distinct, unused and unreachable objects still receive storage, and no block static consumes an EBP-relative frame slot. ADR 0051 records this boundary.

Hosted CupidC lowers automatic initializer lists for complete fixed arrays, structures, and one-active-member unions in the ADR 0044 frame boundary. `ZERO_OBJECT` semantically initializes the complete object once. Explicit represented leaves then run in source order and store through checked `MEMBER_ADDRESS` and `ELEMENT_ADDRESS` paths, including nested direct designators. A union list owns one edge: its positional clause selects the first eligible member, or a direct `.member` designator selects that member. Supported structure and eight-byte integer leaves use byte-copy `STORE`, while a narrow character-array string leaf uses `COPY_STRING` to copy the exact frontend-retained bytes after the enclosing object has been zeroed. The i386 emitter preserves EDI, issues `CLD`, and uses `REP STOSB` for zeroing and `REP MOVSB` for copies. Unchanged declarations in `toolchain/cupidc_pp.cc`, `toolchain/cupidc_frontend.cc`, `drivers/serial.cc`, and Doom's `info.cc` guard the active initializer shapes. Repeated union-member overrides, explicit bit-field initializer leaves, volatile or atomic aggregate subobjects, and floating scalar expression leaves remain deferred. ADR 0048 records the original list design, ADR 0053 records runtime narrow strings, ADR 0066 records wide leaves, and ADR 0153 records union selection.

Block-scope compound literals use the same initializer paths. The frontend gives each source site one absolute expression identity and lets that expression own its initializer root. Linear IR initializes the object at each evaluation and returns its address as an lvalue. The i386 emitter reuses one persistent frame slot for the source site. Aggregate lists use a second staging slot and commit the complete object only after every initializer read has finished. A narrow string root zeros its persistent character array and copies the retained literal bytes directly. The exact `(ctool_string_t){literal, size}` call in `toolchain/cupidc_pp.cc` and the focused `(char[]){"Cupid"}` case now parse, lower, and emit without handwritten temporaries. The audit records 40 compound-literal occurrences across four active files. Static-duration literals, variable-length literal objects, and the related named-aggregate backward-jump alias case remain deferred. ADR 0052 records object identity and lifetime, while ADR 0053 extends the runtime initializer boundary.

Ordinary narrow string expressions now cross hosted IR through `STRING_LITERAL_ADDRESS`. The instruction retains the absolute frontend expression identity. The i386 emitter gives each use a local `.LCn` object in `.rodata` and emits an `R_386_32` text relocation with addend zero. This covers array decay in pointer initializers, arguments, indexing, and returns without assigning a host address to the frozen translation unit. Literal pooling and wide strings remain outside this slice.

Hosted CupidC lowers explicit casts to `void` after evaluating the operand once. A represented integer, object pointer, function pointer, supported structure, or floating scalar produces one typed `DISCARD`. A `void` operand leaves the abstract stack unchanged, so the cast publishes no discard instruction. The complete unchanged `ctool_host_allocate` and `ctool_host_release` helpers pin the active `(void)context` and `(void)bytes` uses. Their two functions publish 18 IR instructions, including three discards and two direct calls. A focused 52-byte object has three symbols and one `R_386_PC32` relocation to `sink` at text offset 43 with addend `-4`. An eight-byte integer constant, supported call result, or lvalue can also be discarded. A transported `double` call result or lvalue follows the same rule. A wide integer or `double` lvalue is read into its private snapshot before the handle is removed. Unions, Cupid classes, atomic operands, and floating expressions that require unsupported computation still fail before discard. Represented function pointers may cast to another function-pointer type or to and from a represented 32-bit integer. Object-pointer interchange and narrower or wider integer forms remain unsupported. ADR 0047 records the scalar discard rule, ADR 0049 extends it to structures, ADRs 0065 and 0066 extend it to supported wide values and lvalues, ADR 0076 adds floating transport, and ADR 0113 records the represented function-pointer casts.

The shared frontend carries compatible structure and union values through plain assignment, return, automatic expression initialization, fixed arguments, and matching-record conditional expressions. It retains automatic, block-static, and file-scope array, structure, or one-active-member union initializer lists with direct C11 member and array designators. Selectors stay in source order, positional initialization resumes after the selected subobject, and a direct unknown-bound array uses its greatest selected index plus one. A positional union clause selects the first eligible member; a direct member designator may select another member. Brace-elided children return a following designator to the nearest explicit list. The audit finds 646 direct active-source designators across 19 files. The contract includes the sparse 134-byte ELF table from the CupidASM kernel object test, all seven 35-member definitions in unchanged `kernel/gui/gui_themes.cc`, and the active-member shape in unchanged Doom `info.cc`. Chained designators, names promoted through anonymous members, duplicate overrides, multiple union-member clauses, and Cupid class lists remain deferred.

The production in-kernel CupidC emitter keeps tagged loop and switch control frames. `break` selects the nearest frame and removes a saved switch selector before leaving that switch. `continue` scans outward to the nearest loop, removes every crossed switch selector, and then uses the loop's established target. `while` reaches its condition, `do` reaches its patched condition trampoline, and `for` reaches its iteration expression. The parser accepts 128 active control frames and 1,024 active statement calls. The next entry fails before further recursion with `control nesting too deep` or `statement nesting too deep`. REPL rollback restores both counters. The statement dispatcher has a four-byte checked CupidC frame; its token-heavy nonrecursive work no longer stays live while a nested statement is parsed. `/bin/feature25.cc` runs all three loop forms, nested switches, a nearest-inner-loop case, 600,000-iteration cleanup paths for both jump kinds, both accepted depth boundaries, useful overflow failures, and a fresh evaluation after each failure. The original marker remains `[feature25] PASS do=1 for=1 while=1 stack=1 reject=1 nearest=1`. The added marker is `[feature25-depth] PASS control=1 overflow=1 recovery=1 statement=1 statement-overflow=1 statement-recovery=1`. ADR 0078 records the control semantics, and ADR 0128 records the parser-stack hardening.

The shared frontend independently publishes typed C11 control statements. `break` and `continue` remain targetless there. Hosted IR lowering binds `break` to the nearest loop or switch and binds `continue` to the nearest loop. A `while` continuation reaches its condition, a `do` continuation reaches its post-test condition, and a `for` continuation reaches its iteration expression when present or its condition otherwise. A switch between `continue` and its loop does not become a continuation target.

Unchanged `break` and `continue` statements in `cir_validate_initializer_ownership` in `toolchain/cupidc_ir.cc` guard the active requirement. Two break functions have eight exact IR instructions, including an unconditional `do` break that skips the condition. Six continuation and nesting functions add 47 instructions and check each loop form, a `for` loop without an iteration expression, and nearest-loop binding. Private patch tags resolve deferred `do` and `for` targets during lowering and do not appear in published IR.

Block bindings, compound-literal expressions, and file object definitions own one job-owned semantic initializer forest. Automatic scalars and whole records use `EXPRESSION` roots. Automatic aggregate lists use `LIST` roots with runtime `EXPRESSION` leaves, while character arrays use `STRING`. Supported static objects use `ZERO`, target-converted `INTEGER`, `STRING`, string `ADDRESS`, an `ADDRESS` of a linked file object or function, or recursive `LIST` records. An explicit static null pointer constant uses a destination-typed `ZERO` record, including when it is a child of an array or structure list. A binding address names a linked file object or function and may carry a checked signed i386 target-byte addend. Pointer addition, subtraction, and subscripts derive that addend from the existing integer constant-expression value and the target referent layout. List edges name explicitly initialized direct array elements or structure members, nested roots are postorder, and omitted subobjects remain implicit zero. A direct unknown-bound array is completed on a private object type, leaving a shared incomplete typedef unchanged. Freeze derives storage duration from the owning definition, binding, or compound expression, accepts explicit `ZERO` children only in static forests, and rejects runtime leaves there. The forest itself does not serialize a target image. The object emitter consumes static roots owned by file definitions and block-static bindings, while automatic roots lower into runtime stores.

File-scope object definitions live in a table separate from canonical bindings. The binding keeps first-declaration facts, while the definition keeps its type, storage, source location, explicit or tentative kind, and initializer root. Repeated tentative declarations coalesce as they are parsed. Translation-unit finalization applies the merged binding type and supplies a zero root. An incomplete external array becomes a one-element array, while incomplete internal arrays and records fail precisely. The focused contract publishes 29 definitions, 39 initializer records, and nine list edges. It covers repeated and superseded tentative declarations, object addresses, array and function decay, array-element and one-past addresses, pointer subtraction, integer-first addition, `&numbers[1] + -1`, nonzero qualification and object-to-`void` conversions, both sides of the signed i386 addend boundary, unevaluated pointer arithmetic inside `sizeof`, unresolved external references, and mixed address leaves without inventing host pointers or ELF relocation records. Unchanged `kernel/fs/ramfs.cc` proves the string and all 11 function addresses in its operations table.

The hosted object path accepts represented file and block-static data plus a growing function subset in one deterministic i386 ELF32 relocatable object. Public `ctool_c_lower_ir` publishes a contiguous typed instruction slice for each supported function, with function-relative branches, absolute frontend identities, and retained source locations. The current ABI slice covers prototyped cdecl functions plus zero-parameter definitions written with an empty identifier list. Results may be `void`, represented integer or pointer scalars, same-kind `float` or `double` values, supported eight-byte integers, or supported structures. Declared parameters may use those same represented values or supported structures. Fixed calls, variadic calls, and direct or indirect calls without a prototype reach the same path. Values without declared parameter types may be represented four-byte integers or pointers, signed or unsigned eight-byte integers, or values already typed as `double`. The transport set also includes same-kind `float` and `double`; it does not yet include floating computation or value-producing conversion. Default promotions make an undeclared narrow integer a four-byte value before lowering. An eight-byte integer crosses the ADR 0065 result path, ADR 0066 object path, ADR 0067 parameter path, ADR 0068 operation path, ADR 0072 multiplication path, and ADR 0075 variadic path through one private snapshot handle. These paths include the same-rank signed-to-unsigned usual arithmetic conversion and GNU wide-enum promotion to its exact compatible type. A structure also occupies one abstract IR value while the emitter owns its target-sized snapshot and ABI area.

The path lowers parameter, automatic-local, block-static, and linked file-object loads; structure snapshots and copies; object-pointer dereference and address-of; function decay, address-of, and dereference; direct ordinary members reached through file objects or object pointers; direct reads from four-byte integer bit fields; structurally compatible pointer conversions and null-pointer conversions; represented integer promotions and conversions; explicit casts among represented one-byte, two-byte, and four-byte integers plus same-width casts between four-byte integers and object pointers; constants and enumerator identifiers; all four 32-bit integer unary operators; addition, subtraction, multiplication, signed and unsigned division and remainder, bitwise AND, OR, and XOR, and 32-bit left and right shifts; integer and pointer equality and inequality, plus all four signed or unsigned object-pointer relational comparisons; short-circuit logical AND and logical OR; scalar and matching-structure conditional expressions; statement-level `if` with optional `else`; pre-test `while`, post-test `do`, and `for` loops; 32-bit integer `switch`, `case`, and `default`; nearest-control `break`; nearest-loop `continue`; direct identifier labels and `goto`; multiple returns; represented declarations in supported compound statements and `for` initializers; direct and indirect calls; initializer stores; value-preserving plain assignment; all ten compound assignments plus prefix and postfix increment and decrement for represented non-Boolean one-byte, two-byte, four-byte, and eight-byte integers; discarded nonvoid values; explicit casts to `void` for represented scalar, structure, and `void` operands; and value or void return.

Direct `goto` uses the frontend's canonical function-scope label table. A fixed-point pass marks only labels reached from the function entry, so a dead jump after a return cannot revive its target. Forward jumps use a private patch tag that is cleared before IR is published, while backward jumps receive their target immediately. The direct contracts cover forward and backward jumps, cycles, nested compound and `if` targets, loop exit, entry before `break` and `continue` in an otherwise unreachable infinite loop, a terminal `do` body, and a declaration below a label. Eleven functions publish 73 exact instructions after entry-aware lowering removes dead structured prefixes. If a dead structured prefix still points at the end of a function that cannot fall through, lowering adds an unreachable typed return block so the target stays inside the function. The deterministic object proof contains 237 text bytes in five functions, with decoded branch targets for ordinary jumps, terminal `if` and `while` entries, and a label above a four-byte automatic local. It has no relocations. The unchanged `goto done` cleanup path in `toolchain/cupidld.cc` pins the active requirement.

Hosted switch lowering evaluates its promoted 32-bit condition once. `DUPLICATE_VALUE` preserves that value while a source-ordered equality chain selects a case target. Matching and unmatched paths discard the saved value before they jump to a case, default, or exit. Dispatch discovery follows cases inside compounds, `if` arms, loops, and identifier labels, but stops at a nested switch. Entry-aware lowering validates dead prefixes without publishing their instructions, keeps inner cases from reviving an unreachable nested switch, and still permits direct `goto` into an ordinary label inside a case body. An unused identifier label does not revive a prefix merely because a reachable case follows it in the same block. Positive fixtures place cases inside `while`, `do`, and `for` bodies. Canonical signed constants include negative cases such as `-1`. The unchanged `cfront_public_storage` function publishes 59 exact IR instructions. Its exact 272-byte local object has six comparisons, six conditional branches, seven direct jumps, six returns, two symbols including the null symbol, and no relocations. ADR 0038 records the design and limits.

Represented integer mutation evaluates its destination once. `DUPLICATE_ADDRESS` keeps the address for the final store while the loaded value passes through integer promotion, usual arithmetic conversion where required, the selected operation, and assignment conversion. This covers `*=`, `/=`, `%=`, `+=`, `-=`, `<<=`, `>>=`, `&=`, `^=`, and `|=` plus all four prefix and postfix updates for supported byte, word, doubleword, and eight-byte integer objects. Prefix forms return the stored value. Postfix forms reconstruct the prior canonical value after the store without loading the object again. Qualified volatile objects keep one semantic load and one store. Boolean, atomic, floating, and aggregate mutation remain outside this ordinary-object slice. ADR 0039 records the original four-byte contract, ADR 0046 extends it to non-Boolean byte and word objects, and ADR 0074 extends the snapshot path to wide integers.

Represented bit-field mutation keeps the complete record address because a field has no C address. A partial field is read once for the computation and again for the final read-modify-write merge. Postfix forms retain the first extracted value instead of reconstructing it after width truncation. Narrow `unsigned int` fields use their target width when deciding whether to promote to signed `int`. The 1,415-byte object proof covers 20 functions, all ten compound operators, signed and unsigned prefix or postfix wraparound, neighboring-bit preservation, and volatile 32-bit direct stores, plus one indexed postfix case that advances its side-effecting index exactly once. It has 21 symbols including the null symbol and no relocations. No unchanged active expression currently uses bit-field mutation; this issue #25 proof advances the hosted language path without moving production ownership. Character-sized, Boolean, atomic, compact packed, and partial volatile forms keep focused diagnostics. ADR 0064 records this boundary.

Ordinary narrow bit-field reads keep a different kind of information. When an `unsigned int` field narrower than 32 bits promotes to signed `int`, the frontend places its direct member index on that one conversion. Linear IR verifies the member-load chain and matching graph and layout widths before accepting the same-rank signedness change. Generic conversions still carry no member index and keep the earlier rejection. The active requirement is all nine color-channel reads in unchanged `kernel/doom/src/i_video.cc`. A focused 14-instruction IR fixture and 127-byte object cover signed right shift, masking, and variable left shift. Four forged metadata cases fail transactionally and recover in the same job. ADR 0152 records this boundary.

Represented object pointers now cross the hosted address and value boundary without losing their C meaning. `DEREFERENCE` turns one pointer value into the referenced object address, while `ADDRESS_OF` performs the inverse transition. Both are semantic IR instructions and emit no machine instruction because each represented form is one i386 word. Pointer parameters, results, locals, linked objects, loads, stores, direct arguments, plain assignment, automatic initialization, compatible pointer conversions, and null-pointer conversion now reach deterministic object emission. Value matching removes qualifiers from the pointer object but keeps referent qualifiers, including the C rule that moves array qualification to its elements. A focused initializer carries distinct compatible qualified-array referents through the emitter's load and store checks. The unchanged `obj_region_less` helper in `toolchain/cupidobj.cc` supplies the active pointer and indirect-member requirement. ADR 0040 records that address and value boundary.

Object-pointer comparisons and truth tests now use the same represented scalar path. Equality accepts the pointer types already normalized by the frontend, relational comparisons keep compatible object-pointer operands, and the i386 emitter selects unsigned predicates. Pointer values can drive `!`, `&&`, `||`, conditional selection, `if`, `while`, `do`, and `for`. Explicit same-width casts now carry all 32 bits between represented integers and object pointers, or between represented object-pointer types, without emitting an instruction. Pointer-valued conditional arms normalize to the frontend's composite result type at the join. The condition contract pins all 62 public IR records. Relational validation requires object referents while equality retains frontend-normalized `void *` pairs. A malformed frozen unit that changes `void *` equality into pointer order fails transactionally. The unchanged `ctool_job_arena` helper in `toolchain/ctool.cc` pins the typed null cast, inequality, pointer condition, pointer-valued conditional, and indirect member load in one active expression. Atomic pointer access and casts between pointers and narrow integers remain open. ADR 0041 records the comparison and condition step.

Represented pointer arithmetic uses target layout instead of byte-based integer arithmetic. `POINTER_BINARY` scales 32-bit integer offsets by the complete pointed-to object size, while compatible pointer subtraction divides the address difference by that size and returns signed `int`. Frontend-normalized `pointer[index]` and `index[pointer]` use the same addition and dereference path. `ARRAY_TO_POINTER` records linked array decay without emitting a machine instruction and carries array qualification to the element pointer. Pointer `+=`, `-=`, `++`, and `--` evaluate their destination once; volatile pointer objects receive one load and one store. The unchanged ATA read and write loops in `drivers/ata.cc` pin `buf += 256` as an active requirement. Atomic pointer mutation, wide offsets, union and Cupid class values, deferred initializer leaves, and broader production integration remain open. ADR 0042 records the design.

Represented function pointers use the same four-byte scalar storage and value paths without losing their signatures. `FUNCTION_ADDRESS` names a linked function, `FUNCTION_TO_POINTER` records decay, and `CALL_INDIRECT` retains the prototype used for ABI checks. Structural compatibility ignores top-level `const`, `volatile`, and `restrict` on parameters while retaining `_Atomic` and referent qualifiers. A checked worklist remembers compared type pairs, returns all scratch storage to the job arena, and handles repeated callback graphs without recursive path growth. The emitter evaluates the callee before its arguments, reorders completed arguments into cdecl memory order, calls through EAX, and removes the saved callee with the caller-owned argument storage. Function pointers can cross fixed parameters and results, automatic and linked storage, static and automatic initialization, assignment, direct arguments, equality, null conversion, truth tests, and conditional selection. Casts may change a represented function-pointer signature or move the same 32 bits to or from a represented 32-bit integer. The unchanged `body(&invocation, user_data)` call in `toolchain/ctool.cc` and the unchanged CupidLD section-selector call pin the active requirement. The contract publishes 86 exact IR instructions across 13 functions. A separate signed wide-parameter fixture adds a five-instruction register-indirect call. Its aligned deterministic object contains 13 functions, 513 text bytes, 17 symbols, nine text relocations, and one data relocation. Four calls are register-indirect, one is direct, and the first 234 text bytes are exact. A separate 28-byte object pins one absolute relocation to a defined static function. Indirect variadic and unprototyped calls now carry signed or unsigned eight-byte integer arguments, existing `double` values, and source `float` values promoted to `double` through the same saved-callee and aligned outgoing-area path. Fixed indirect calls carry same-kind `float` and `double` arguments and results. Aggregate ellipsis transport, object-pointer and function-pointer interchange, function-pointer casts involving narrow or wide integers, atomic callback access, floating computation, union, and Cupid class call forms remain open. ADR 0043 records function pointer values and calls, ADR 0047 records discard casts, ADR 0049 extends fixed indirect calls to structure parameters and results, ADR 0050 records call alignment, ADR 0054 adds scalar variadic calls, ADR 0055 adds scalar variadic callees, ADR 0075 adds wide integer variadic transport, ADR 0076 adds floating scalar transport, ADR 0077 adds default `float` promotion, and ADR 0113 records represented function-pointer casts.

Referenced fixed arrays and structures receive target-sized EBP-relative storage in the hosted emitter. `LOCAL_ADDRESS` keeps the absolute block-binding identity in public IR, while the emitter assigns offsets in binding order, honors target alignment up to four bytes, and rounds the final frame to four bytes. The unchanged `section_map` array in `cupidc_emit.cc` and active `&children[index]` call shape in `cupidc_ir.cc` pin the storage requirement. Five focused functions publish 47 instructions and 264 exact text bytes, including a mixed 12-byte frame with addresses at EBP minus 3 and EBP minus 12. Its three call relocations are at offsets 145, 201, and 255. ADR 0048 adds the represented initializer-list subset described above. ADR 0049 allocates instruction-owned structure snapshots after those source objects. Other initializer leaves, top-level union and Cupid class values, and alignment above four bytes remain open. Oversized frames retain a checked limit failure. ADR 0044 records the source-object storage decision.

Represented one-byte and two-byte integers cross loads, exact-width stores, promotions, explicit and implicit conversions, plain assignment, compound assignment, prefix and postfix updates, automatic and linked storage, ordinary members, indexed elements, scalar conditions, fixed and variadic direct or indirect calls, and function results. The abstract stack keeps a canonical 32-bit word, with signed values sign-extended, unsigned values zero-extended, and `_Bool` normalized to zero or one. Each promoted narrow cdecl argument slot is four bytes. Callers and callees canonicalize narrow result lanes at the ABI boundary. The unchanged `asm_lower`, `x86_class_width`, and `x86_set_memory_width` functions pin the value requirement. The complete unchanged `x86_put_u8` body pins one-byte update, and active decoder paths also require one-byte counters and prefix `|=`. Boolean mutation, narrow and atomic bit fields, pointer and eight-byte atomics, compare-exchange, integer and floating conversion, floating comparison and truth, union and Cupid class values, aggregate ellipsis transport, and aggregate variadic reads remain open. ADR 0045 records narrow values, ADR 0046 records narrow mutation, ADR 0049 records inline structure arguments, ADR 0050 records call alignment, ADR 0054 records scalar variadic calls, ADR 0055 records scalar variadic callees, ADR 0075 records wide integer variadic calls and reads, ADRs 0076, 0077, 0079, and 0091 record the hosted floating path, ADR 0101 records the first integer atomic builtins, and ADRs 0065 through 0074 record the underlying wide value path.

For a variadic call, the frontend applies lvalue conversion, array and function decay, integer promotion, and `float` to `double` promotion to each ellipsis argument as required. It applies the same default promotions to every argument at an unprototyped call site. Each public call instruction retains the actual count and indexes a packed slice of every actual post-conversion type. IR and i386 emission use both records for stack effects, argument size and order, indirect callee placement, sixteen-byte padding, and caller cleanup. Signed and unsigned eight-byte values, existing `double` values, and source `float` values promoted to `double` use full-width slots at either undeclared parameter boundary.

GNU C mode now represents `__builtin_va_list` as Cupid's target `char *` cursor. The frontend publishes explicit start, argument, copy, and end expressions. Linear IR keeps start, argument, and end operations; scalar copy uses the existing store. The i386 emitter initializes the cursor just after the full width of the final named cdecl argument and reads through the old cursor. A non-atomic pointer or four-byte signed or unsigned `int`, `long`, or enum read advances by four. A non-atomic signed or unsigned eight-byte integer, represented wide enum, or `double` advances by eight and returns an instruction-owned snapshot. ADR 0055 records the cursor and ABI decisions, ADR 0062 extends the represented read types to enums, ADR 0067 covers a final named wide parameter, ADR 0075 adds wide integer reads, and ADR 0076 adds `va_arg(double)`. A request for `float` is diagnosed as invalid C because an unnamed `float` arrives as `double`.

An empty identifier-list definition now keeps its non-prototype function type while declaring zero parameters. Calls through a function type without a prototype apply default promotions to every argument and retain their actual count and post-conversion type slice through Linear IR and i386 emission. Represented four-byte integers and pointers, signed or unsigned eight-byte integers, existing `double` values, and source `float` values promoted to `double` cross this boundary. ADR 0056 records the function form, ADR 0075 records wide argument transport, ADR 0076 records floating transport, and ADR 0077 records default argument promotion.

Block-scope `struct` and `union` tags now follow lexical C scope. The frontend supports forward declarations, same-scope completion, ordinary references, nested shadowing, and restoration after scope exit. A record tag declared in a function definition's parameter list shares the outer body scope and expires when the definition ends. Tag-only declarations may use the represented `typedef`, `extern`, `static`, `auto`, or `register` spelling, or a represented type qualifier, when they introduce a tag. They remain in the statement stream with no block bindings, and IR treats them as checked no-ops. An empty declaration with storage or type qualification that only names a visible tag is rejected. A `for` initializer may use a visible record type or an anonymous record definition for its object, but it cannot introduce a named tag or omit the object. Anonymous record definitions work when a declarator owns the type, including Doom's block-static `packs` array.

Block-scope `extern` objects keep a lexical alias to one canonical linked object. Compatible repeats share identity, incomplete arrays may be completed, visible file-scope `static` objects keep internal linkage, and a block-only name stays out of ordinary file-scope lookup. The declaration creates no automatic storage and lowers without runtime work. Block typedefs retain their lexical ordinary-name scope, stable graph type, source order, and dual location. Exact same-type repeats are accepted, nested aliases and parameter shadows restore at scope exit, and scalar, record, function, incomplete, and `void` aliases use the normal declarator path. IR consumes each alias as a validated no-op, and object emission is byte-identical to spelling the underlying type directly.

Block function declarations now separate lexical type and visibility from linked identity. Plain and `extern` declarations point to one canonical function, and a visible prior declaration contributes to the later alias's composite type. A declaration from an expired sibling scope does not change the type seen in a later block. A visible file-scope `static` function keeps internal linkage, while a function first introduced in a block stays hidden from ordinary file lookup until a later file declaration publishes it. The declaration adds no storage or runtime IR. Direct calls and function addresses use the canonical symbol with the lexical type. The exact Doom profile still parses all of `kernel/doom/src/d_main.cc`, including the `forwardmove` and `sidemove` declarations on lines 1336 and 1337. Active-source guards also pin 27 block function declarations across nine files.

Block enums use that same lexical binding stream. Each enumerator retains its folded target value and final identifier type. A declaration owns declaration-position and record-member definitions, while a function definition owns a parameter-list prefix. Definitions in block type names attach their enumerators to the expression or initializer where they become visible. Linear IR validates those events in source order before it lowers runtime control flow. This covers `sizeof`, alignment queries, casts, compound literals, `__builtin_offsetof`, case values, loop headers, variadic reads, and aggregate designators. Nested tags and constants still shadow and restore in their C scopes, including an anonymous enum in a `for` declaration. Represented uses lower to `INTEGER` with no frame slot, symbol, relocation, or declaration instruction. File, block, and function-parameter enumerators can also feed static floating arithmetic, comparisons, truth, and conditionals. The active cursor enum in `kernel/gui/desktop.cc` and REPL enum in `kernel/lang/shell.cc` remain unchanged. Block declaration attributes, nested function definitions, nonempty identifier lists, and non-scalar arguments without declared parameter types remain open. ADR 0057 records the record-tag model, ADR 0058 records linked block objects, ADR 0059 records block typedefs, ADR 0060 records linked block functions, ADR 0061 records declaration-position block enumerators, ADR 0062 records nested definitions and lexical activation, and ADR 0147 records static floating use.

The narrow mutation IR matrix covers all ten compound operators, signed and unsigned byte and word updates, one volatile byte update, and a nested byte member. It requires 19 narrow address duplications, 25 narrow loads, 26 promotions, 23 narrowing assignment conversions, 20 exact-width stores, and one volatile load. The deterministic object proof has eight functions in 878 exact text bytes, ten symbols, one byte of BSS, and one `R_386_32` relocation. Shared decoding checks fourteen byte stores, four word stores, promoted multiplication, signed and unsigned division, and shifts. A decoder-driven execution oracle runs twelve signed and unsigned prefix or postfix cases at zero and wrap boundaries. It checks EAX, the stored byte or word, and poisoned padding in the four-byte argument slot. Signed narrowing keeps the low AL or AX lane and sign-extends it, giving a deterministic two's-complement result. Boolean mutation and malformed promoted-type metadata have transactional negative coverage.

Unchanged active source drives the supported subset. `add2` in `bin/cupidc_test3.cc` pins two parameter loads, one typed `ADD`, and a scalar return. `asm_lower` in `toolchain/cupidasm.cc` pins signed-byte parameters, loads, conditions, casts, and returns. `x86_class_width` and `x86_set_memory_width` in `toolchain/x86.cc` pin signed and unsigned byte and word parameters, member storage, promotions, and results. `cemit_multiply_overflows` in `toolchain/cupidc_emit.cc` pins unsigned division in the short-circuit right operand. `cemit_power_of_two` in the same file pins inequality, bitwise AND, equality, short-circuit logical AND, and the surrounding conditional expression. `cfront_bool_valid` in `toolchain/cupidc_frontend.cc` pins short-circuit logical OR over two equality tests. `cfront_public_storage` in that file pins switch dispatch, enum constants, shared case and default targets, and enum returns. `asm_branch_fits_i8` in `toolchain/cupidasm.cc` pins unsigned less-than-or-equal inside logical OR and conditional selection. The unchanged `obj_region_less` helper in `toolchain/cupidobj.cc` pins object-pointer parameters, repeated dereference, indirect member addresses, and pointer loads. The unchanged `ctool_job_arena` helper in `toolchain/ctool.cc` pins object-pointer inequality, a typed null cast, scalar truth testing, pointer-valued conditional selection, and an indirect member load. The unchanged `rotw` helper in `kernel/crypto/aes.cc` pins left shift, unsigned right shift, and bitwise OR while retaining the independently promoted signed `int` shift counts. The unchanged CPUID-toggle return statement in `kernel/cpu/simd.cc` pins bitwise XOR inside its shift, mask, comparison, and conversion context. Its surrounding GNU inline assembly and broader statement sequence remain outside this hosted leaf slice. The unchanged `align_up` helper in `kernel/mm/memory.cc` pins bitwise complement inside its existing unsigned arithmetic and mask. The complete unchanged `dis_signed_bits` function now pins unsigned less-than-or-equal and equality conditions, two conditional branches, three returns, complement, addition, an explicit unsigned-to-signed cast, and negation. Its exact IR contains 27 instructions and reaches an abstract stack depth of two. The complete unchanged `syscall_sleep_ms` helper in `kernel/core/syscall.cc` pins condition reevaluation, the false exit, and the backward loop edge around `process_yield`. Its exact IR contains 14 instructions and reaches an abstract stack depth of two. A focused terminal-body `while` pins the five-instruction path with no backward jump. The unchanged inner tick loop in Doom's `D_Display` function pins body-first execution, condition evaluation after the body, and the backward edge to the body. Its focused IR contains 21 instructions and reaches an abstract stack depth of three. A terminal-body `do` lowers to one return while still validating its unreachable condition. The guarded `url_hash_hex` loop in `bin/browser/url_hash.cc` pins an expression initializer, signed condition, body, assignment iteration, and backward edge. Its focused IR contains 23 instructions and reaches an abstract stack depth of three. Omitted-clause fixtures cover a terminal body and an infinite loop that cannot fall through. The logical-not result in `cc_skip_brace_initializer` remains guarded separately; its broader expressions and control flow still block the complete function. The Paint coordinate functions pin subtraction, multiplication, and addition over linked objects. `vga_flip_ready` covers an automatic initializer call, a linked load, unsigned comparison, conversion to `bool`, and return. `vga_set_vsync_wait` covers a linked assignment whose result is discarded. `timer_get_frequency` keeps the `timer_state` binding and `frequency` graph member until the emitter applies byte offset 8. The Doom guard pins all four color fields in `kernel/doom/src/i_video.h` and all nine red, green, and blue ordinary-expression reads in `kernel/doom/src/i_video.cc`. Its focused IR fixtures retain both the field load and the member-specific promotion. A returned assignment chain proves that each destination is evaluated once and that the stored value survives both stores.

Supported automatic declarations name complete represented scalar objects or fixed array and structure objects with target alignment up to four bytes. Their storage spelling may be absent, `auto`, or `register`, and fixed aggregates may use the supported list initializers. A supported static declaration instead requires a complete nonzero object and a represented constant-data initializer root; it emits storage but no runtime initializer instructions. Both forms may appear in a supported compound statement or as a `for` initializer. A private source-order scan establishes the complete block-binding range for each function before lowering, including declarations below a label. The current statement set contains return, expression, empty, compound, `if` with optional `else`, pre-test `while`, post-test `do`, `for` with optional expression or declaration control clauses, `switch`, `case`, `default`, nearest-control `break`, nearest-loop `continue`, direct labels, and `goto`. A `while` evaluates its condition before each possible iteration. A `do` reaches its body first and evaluates its condition before a possible backward edge. A `for` evaluates its initializer once, then its condition, body, and iteration in C source order. A switch evaluates its promoted condition once, compares cases in source order, and jumps directly to a matching case, the default, or its exit. An omitted loop condition has no false exit, but a reachable `break` can still make the loop fall through. A `continue` reaches the condition for `while` and `do`; it reaches the iteration expression for `for` when one is present and the condition otherwise. A terminal loop body emits no unreachable work or backward edge unless reachable loop control requires the condition or iteration. Skipped conditions, iterations, declarations, labels, and jumps are still checked against the supported-language boundary. Count-only declaration validation advances the same ownership cursor without publishing initializer instructions or changing live label targets. A sequence stops publishing instructions after a terminal statement unless a later subtree contains a reachable label. Instructions serialized before that label are bypassed by the incoming jump. The fixed-point result supplies the final fallthrough decision for every function. A void path that reaches the end receives an implicit return, while a nonvoid path that can fall through remains unsupported. A rewound owner map rejects aliased roots, dangling list edges, and unowned initializer records before lowering.

All call operands are evaluated in source order. Scalar-only calls retain their four-byte slot reversal. A structure-aware call reserves one outgoing ABI block, zeroes it, and fills scalar and structure parameters in declaration order; an indirect call also keeps its callee below the evaluated arguments until the emitter loads it into EAX. Structure arguments occupy their target size rounded up to four bytes. A structure result adds a hidden first word that the callee removes with `RET 4`. Immediately before `CALL`, ESP is aligned to sixteen bytes. The emitter derives zero, four, eight, or twelve bytes of padding from the fixed frame, live semantic values, and outgoing storage. Referenced automatic scalars and fixed arrays or records receive target-sized EBP-relative slots, while structure snapshots receive private slots after the source objects. A block-static address uses its local symbol and an `R_386_32` relocation, never a frame slot. Direct calls use `.rel.text` `R_386_PC32` relocations with addend `-4`. Linked object and function addresses use `R_386_32` with addend zero. Direct jumps use no relocation. `MEMBER_ADDRESS` applies an ordinary member byte offset after relocation. `BIT_FIELD_LOAD` applies the storage-unit byte offset, bit offset, width, and signedness during target emission without changing the base symbol or relocation addend. `BIT_FIELD_STORE_VALUE` uses the same member metadata to replace one field and preserve the value represented by the stored lane. Static inline definitions and external definitions from mixed inline declaration sets are accepted. Pure external inline definitions stop at a focused lowering boundary.

Exact object contracts retain the complete 61-byte VGA load function, the 20-byte timer getter, both 60-byte Paint functions, the 28-byte unsigned multiplication fixture, and the 27-byte and 37-byte assignment fixtures. A separate 138-byte object pins signed and unsigned quotient and remainder functions, with no relocations. Signed operations use `CDQ` and `IDIV`; unsigned operations clear EDX and use `DIV`. The comparison object contains the 127-byte active CupidASM helper and three 39-byte focused functions. Its 244 text bytes cover signed less-than, signed less-than-or-equal, unsigned less-than, and unsigned less-than-or-equal with no relocations. The combined function object appends the exact 143-byte `cemit_power_of_two` function and the 127-byte `cfront_bool_valid` function, bringing its aligned text to 917 bytes. The decoder checks five branch targets in the logical AND helper and six in each logical OR helper. A separate object-pointer contract has six functions in 198 exact text bytes for inequality, equality, unsigned order, and explicit integer/object-pointer casts. The pointer-condition contract has eight functions in 372 exact text bytes for logical not, short-circuit logic, conditional selection, and every supported statement condition. Both objects have no relocations, repeat byte for byte, and decode to the expected compare, predicate, test, branch, and return instructions. The pointer-arithmetic object adds nineteen functions in 811 exact text bytes. It has twenty-one symbols, one sixteen-byte BSS array, two exact absolute relocations, complete-object strides of one, two, four, and twelve bytes, and byte-identical repeated emission.

A separate 86-byte shift object contains the exact 53-byte `rotw` helper and a 33-byte signed right-shift fixture. It has three symbols, no relocations, and decoded coverage for `SHL`, `SHR`, `SAR`, and `OR`. The CPUID-toggle expression has its own exact 69-byte local function, two symbols including the null symbol, no relocations, and decoded coverage for `XOR` with the surrounding shift, mask, and comparison. The memory-alignment contract adds one exact 73-byte local function, two symbols including the null symbol, no relocations, and decoded coverage for `ADD`, `SUB`, `NOT`, and `AND`. The integer-unary contract adds four functions totaling 86 text bytes, five symbols, no relocations, and decoded coverage for `NEG`, `TEST`, `SETE`, and `MOVZX`; unary plus needs no target instruction. The integer-cast contract adds two functions totaling 52 text bytes, with sizes of 35 and 17 bytes. It has three symbols, no relocations, and decoded coverage for `NOT`, `ADD`, and `NEG`; the same-width casts need no target instruction.

The complete signed-bit helper adds one exact 143-byte local function with 71 decoded instructions. Its two conditional branches land at byte offsets 53 and 111. The object has two symbols including the null symbol, no relocations, and repeats byte for byte. The complete sleep helper adds one exact 94-byte local function with 43 decoded instructions. Its false branch lands at byte offset 92, its backward jump lands at byte offset 20, and its three direct-call relocations are at offsets 11, 24, and 80. The focused Doom loop adds one exact 125-byte local function with 59 decoded instructions. Its false exit lands at byte offset 123, its backward jump lands at byte offset 6, and its two direct-call relocations are at offsets 14 and 78. The combined loop object contains the 107-byte browser function and eight loop-control functions totaling 319 bytes. Its 426 text bytes have ten symbols including the null symbol, no relocations, and exact branch targets for `break`, all three continuation points, and nested nearest-loop binding. A separate declaration object contains an 87-byte declaration-initialized loop, an 80-byte nested-compound function, an 11-byte function whose declaration follows an unconditional return, and a 60-byte loop-body function. Its 238 text bytes have five symbols including the null symbol, no relocations, fixed local slots, exact branch targets, and byte-identical repeat emission. The direct-jump object adds a 44-byte forward function, a 76-byte backward function, two 38-byte terminal structured functions, and a 41-byte label-entry declaration function. Its 237 text bytes have six symbols including the null symbol, no relocations, and exact decoded targets. The declaration function lands at byte offset 11 and uses one four-byte local slot. Repeat emission is byte-identical and preserves the frozen input.

The separate bit-field load object adds three functions totaling 63 text bytes. It covers an unsigned eight-bit field, a signed five-bit field at storage byte offset 4, and a full-width field at byte offset 8. Their three direct-object relocations remain at offsets 4, 25, and 49 with addend zero, and repeated emission is byte-identical. The ordinary-promotion object adds `shift_red`, `mask_green`, and `shift_blue` in 127 exact text bytes. Its four symbols have no relocations. Eight decoder-driven executions check signed right shift, masks, variable shifts, unchanged storage and arguments, canaries, and restored cdecl state. A 64-byte output limit fails transactionally, and recovery reproduces the object. The bit-field assignment contract adds four functions and 31 exact IR instructions. Its indexed Doom-shaped function places a 1,024-byte color array in `.bss` and uses one `R_386_32` relocation. A decoder-driven i386 oracle runs six pointer-based cases and checks truncation, signed extension, neighboring bits, one complete-unit store, unchanged arguments, restored stack state, and a full-width store with no old-unit read. Consecutive emissions are byte-identical. Focused negatives distinguish unsupported character-sized, Boolean, atomic, and compact packed fields. Malformed graph and layout widths fail transactionally.

The wide comparison and condition contract covers all six signed and unsigned comparisons, mixed signedness, logical not, short-circuit logical operators, conditional selection, and scalar conditions in `if`, `while`, `do`, and `for`. Its 24 functions lower to 264 exact IR instructions with fingerprint `9EE1D330DE86EDBB`. The deterministic object has 3,341 text bytes with fingerprint `16626CE1`, 25 symbols including the null symbol, and no relocations. A decoder-driven i386 oracle exercises high-word-only truth, equal-high low-word ordering, and signed overflow-aware ordering while checking the cdecl frame and callee-saved registers. Full-body guards keep `pp_if_value_truth`, `pp_if_is_negative`, and `pp_if_signed_less` tied to the active source requirement. Eight-byte shift counts remain a focused boundary. Malformed comparison metadata and output limits fail transactionally. Other unsupported bodies and call shapes, malformed pointer, structure, loop-control, direct-jump, switch, and variadic records, and pure external inline definitions still leave output empty and rewind operation storage.

Wide shifts, AND, OR, XOR, explicit represented-to-wide casts, same-rank signed-to-unsigned conversion, GNU wide-enum promotion, conversion across represented integer widths, and object-pointer conversion to and from signed or unsigned eight-byte integers retain their positive contracts. Eight-byte constants, matching conditional arms, fixed call results, discard, and returns use the ADR 0065 path. Object and function pointer values, address-of, dereference, indirect ordinary members, object-pointer arithmetic, normalized subscripts, linked array decay, pointer mutation, narrow integer mutation, structure copies including nested union storage, scalar members of structure rvalues, structure returns, fixed direct or indirect structure calls, four-byte, wide-integer, and floating variadic calls and callees, floating width conversion, mixed floating expressions, floating compound assignment, sixteen-byte call sites, and the first one-, two-, and four-byte integer atomic builtins are represented. Boolean mutation, pointer and eight-byte atomics, compare-exchange, atomic bit fields and aggregates, character-sized bit fields, non-four-byte storage units, packed storage units that cross the record boundary, partial volatile bit-field mutation, explicit bit-field initializer leaves, integer and floating conversion, floating comparison and truth, aggregate ellipsis arguments, aggregate variadic reads, top-level union and Cupid class values, aggregate members of structure rvalues, and broader production integration remain open.

ADR 0066 adds eight-byte object values to the represented path above. `FILE_ADDRESS`, `LOCAL_ADDRESS`, pointer dereference, `MEMBER_ADDRESS`, and indexed pointer arithmetic can feed a wide `LOAD`. Automatic expression initialization and aggregate leaves use `STORE`, while plain or chained assignment uses `STORE_VALUE`. The emitter copies eight bytes with `CLD` and `REP MOVSB`, preserving one snapshot handle as the assignment result. The IR proof covers eleven exact function streams and fourteen wide loads. Its deterministic object has 16 data bytes, 879 text bytes with fingerprint `2448A1CD`, fourteen symbols, and two exact `R_386_32` relocations. The execution oracle runs the relocated active `get_cpu_freq` path, the block static path, and plain and chained pointer assignments. The active source remains unchanged. Atomic wide loads and stores stay rejected.

Active Doom declarations require the same array-address form at `kernel/doom/src/g_game.cc` for `mousearray` and `joyarray`, and at `kernel/doom/src/tables.cc` for `finecosine`. The focused contract mirrors the constant-expression subscript used by `finecosine`. The forced `kernel/doom/dglibc_compat.h` header parses with its builtin cursor alias, and the empty identifier-list definition of `doomgeneric_Tick()` now passes. The pinned exact-profile parse of `d_main.cc` accepts the anonymous block-static `packs` record and both local external arrays, then completes the file. The command driver can reproduce that profile with ordered `-include` inputs. CupidC retains the sound driver's empty volatile memory barrier in Linear IR and emits no instruction bytes for it. Its static scalar evaluator also compiles the unchanged fixed-point table in `kernel/doom/src/am_map.cc`.

ADR 0149 adds a separate Doom compatibility switch for old C implicit function declarations. An undeclared direct call creates a block-scoped `int()` declaration linked to one canonical external function. Calls made before a later prototype keep default argument promotions, while later calls use the refined prototype. ADR 0151 uses that explicit profile for eleven bit-preserving conversions between unqualified function pointers and unqualified four-byte data or `void` pointers. The frontend and Linear IR check the rule independently; strict C and ordinary GNU mode still reject it. The affected pointer sites are in `m_menu.cc`, `p_saveg.cc`, `p_ceilng.cc`, and `p_plats.cc`. ADR 0153 adds one-active-member union initialization, which compiles `kernel/doom/src/info.cc`. ADR 0152 retains direct member identity when a narrow `unsigned int` bit field promotes to signed `int`; `kernel/doom/src/i_video.cc` now emits a 9,288-byte object with SHA-256 `d04e91844763391d4224d14aefce64ece02a95c9a99c604e9ef5b1392974dd20`. The checked seed owns all 80 Doom-tree sources and the three compatibility roots in the normal image.

ADR 0182 completed the separate three-root `DOOM_COMPAT_I386` frontier.
Explicit non-atomic pointer-to-pointer casts retain a static string or binding
address, while a cast through an integer remains rejected. That first frontier
carried the historical 27-byte `dg_setjmp` and the 38-byte `dg_longjmp` through
Cupid's x86 model with no relocation. The active 67,155-byte dglibc source now
uses the corrected 31-byte setjmp form. Checked-seed compiles reproduce its
93,332-byte object and the 17,084-byte libc-stub and 10,352-byte platform
objects on two runs. The dglibc object has SHA-256
`e2496b01c93a7858a0c035b53aea0ad834d95d2be3f7ae49574d1759ebec34d6`.
ADR 0183 records the first five-tool seed promotion. ADR 0184 moves all 83
normal recipes and source names to CupidC and `.cc`.

Checked-seed CupidC accepts both the compatibility form and a corrected form.
GNU `returns_twice` is canonical function metadata and must remain on a direct
call target. Supported calls use four-byte cdecl arguments and may return void
or any nonaggregate type. Each live-prefix call copies the
four-byte operands below its arguments into a region owned by that instruction,
then restores them after cdecl cleanup. A live-prefix site fails if any
returns-twice continuation can reach it again; a call with no live prefix may
repeat. Aggregate, wide-integer, and wider-than-four-byte floating arguments,
aggregate results, and marked-function pointer conversions fail explicitly.

The active `dg_setjmp` body saves `ESP + 4`, occupies 31 bytes, and requires
`returns_twice`; its matching `dg_longjmp` declaration requires `noreturn`.
A decoder-driven i386 oracle models first and second returns with transfer
values zero and seven. The asset-free guest self-test exercises direct
longjmp, two real quit cycles, and two real error cycles through the active
shell-session envelope. ADR 0212 records the compiler boundary, ADR 0213 its
checked-seed promotion, and ADR 0214 active adoption.

The Doom production wrapper has exact three-source and 80-source allowlists.
It freezes the selected source and all 291 `.h` and `.inc` inputs visible
through the profiles' 20 include roots. The 69,366-byte input manifest has
SHA-256
`47ba35158cac0a7df253a0056235223e62fee24df74701800f88763e588611c2`.
Checked-seed CupidObj accepts `profile-manifest` and produces those exact bytes
from a bounded `CUPROF1` envelope. The live snapshot holds 291 captured
headers, 665 profile memberships, and 956 encoded path records. CupidObj sorts
the logical names, computes every SHA-256 field from the captured bytes, and
matches the Python oracle. Its poisoned-host promotion proof matched all 19 C
objects, startup, and five tools between stage two and stage three. The
promoted seed now passes a 5/21/20 behavior matrix. The normal target passes
the checked seed manifest. The wrapper derives the `CUPROF1` snapshot and
independent Python oracle from one stable capture, then runs CupidObj from the
exact frozen seed.
It requires byte parity and rechecks the seed, live inputs, candidate, output
directory, and existing output under an adjacent no-follow lock. Identical
bytes retain their timestamp; changed bytes publish atomically. CupidObj
authors the production bytes, while Python retains discovery, native-path
checks, freezing, parity, drift detection, locking, and publication. ADR 0242
records the source boundary, ADR 0243 records seed carriage, and ADR 0244
records production ownership.
The wrapper recursively checks visible `.c` and `.cc` files beneath the Doom
tree before and after each compile. It rejects a legacy `.c` file, an
unlisted `.cc` file, a missing root, header membership or byte changes,
symbolic links, and NTFS junctions. An unchanged scan keeps the manifest
timestamp, so the closed input check does not force all 83 objects to rebuild.

The production object validator accepts signed `R_386_32` addends used to
select static subobjects. This keeps `&mousearray[1]` and `&joyarray[1]` in
`g_game.cc`; its 52,004-byte object has SHA-256
`51aff2138ff2ee51bae9cc18e1dcc415567c6be1699ef0ef6f1ed2b009c30df1`.
Both `.data` relocations carry addend 4. `R_386_PC32` still requires addend
-4.

The active Doom config path uses a bounded parser, restores registered
defaults between shell sessions, writes a sibling temporary file, and commits
with native VFS rename. Game saves use the same close-and-rename rule without
deleting the prior slot first. HomeFS and RamFS reject busy replacement.
HomeFS rejects corrupt containers and a second live mount, reserves its FAT
container while mounted, and batches related mutations behind one final
checked publish. FAT16 applies durable directory publication to replacement,
delete, and directory creation; reports handle exhaustion separately; and
will not replace an entry while a reader still owns it. A failed block-cache
read cannot corrupt the identity of a dirty victim because new data is staged
before publication. ADR 0211 records these storage rules.

Earlier private four-CPU e1000 and RTL8139 boots covered the complete runtime
frontier. Each no-WAD boot returned from two consecutive missing-IWAD errors
and then completed the expanded dglibc/storage diagnostic. Separate frontier boots passed
after swap holds one FAT handle open; they also record framebuffer changes and
both audio paths. The repository has no WAD, so gameplay, game input, game
audio, menu-driven save/load, and persistence across reboot remain open. FAT
publication ordering has source and guest coverage but no injected power-cut
proof.
The fixed asset-free command sequence now runs `doom`, an explicit
`doom -iwad /disk/missing.wad`, requires the shell-return marker, and then
runs a fresh CupidC-built `ls`. Its contracts prevent the earlier `ls`
completion from satisfying the recovery check. ADR 0232 records this gate.

The block-static object proof emits eleven exact local symbols, from `.LBS0.hex` through `.LBS10.unused`. Its sections contain 21 bytes of read-only data, 56 bytes of initialized writable data, and 4 bytes of zero-filled storage. Ten text, one read-only-data, and five data relocations are all direct `R_386_32` references with addend zero. The fixture covers shadowed names, unused and unreachable objects, aggregate and string initializers, linked and unresolved addresses, runtime reads and writes, and an unused eight-byte image. A referenced eight-byte block static now lowers through the wide snapshot path. Missing, out-of-range, mistyped, runtime-initialized, and constrained-output cases still fail transactionally. The unchanged `dis_hex_fixed` helper in `toolchain/cupiddis.cc` pins the active constant character array.

All twelve shared hosted Toolchain implementation files parse completely.
Each tuple reports definitions, statements, expressions, block bindings, and
initializers: `ctool.cc` 65/1,012/5,981/133/33; `cupidasm.cc`
84/3,146/20,714/346/196; `cupidc_emit.cc` 368/9,323/77,764/1,132/755;
`cupidc_frontend.cc` 461/17,619/115,690/2,629/1,584; `cupidc_ir.cc`
270/7,624/70,606/1,004/369; `cupidc_pp.cc` 143/3,932/25,287/479/286;
`cupidc_type.cc` 31/737/5,487/85/43; `cupiddis.cc`
83/1,907/12,277/206/150; `cupidld.cc` 82/2,875/18,200/369/337;
`cupidobj.cc` 140/3,451/23,768/532/452; `elf32.cc`
37/1,219/9,457/143/70; and `x86.cc` 65/1,866/12,549/200/17,124. The
generated audit records the current lexical totals and source graph. These
files belong to the i386 Linux profile and feed both the five-tool fixed point
and the Cupid-built contract cohort.

The shared frontend treats C11 `<:` and `:>` spellings as canonical brackets across array declarators, subscripts, and the explicit unsupported `__builtin_offsetof` array-designator seam while leaving the immutable preprocessing tape's original token spelling untouched. Strict-C contracts cover mixed and full digraph forms plus malformed and non-pointer subscripts. Represented `float` and `double` lvalues accept `*=`, `/=`, `+=`, `-=`, and ordinary non-atomic prefix or postfix updates. Diagnostics distinguish those forms from invalid floating remainder, shift, bitwise, aggregate, atomic, and `long double` update operands. Compatible aggregate plain assignment is represented without weakening those constraints.

ADR 0153 supersedes positional-union limits preserved in the older detailed
frontend paragraphs below. A union initializer list may now select one direct
member, positionally or by name. Repeated active-member overrides and Cupid
class initializer lists remain open.

The current normal image is compiler-owned by CupidC. A platform-neutral Cupid
Toolchain foundation, a typed transactional CupidC preprocessing tape, a
shared declaration and function-body frontend, typed linear IR, deterministic
object emission, an immutable indexed i386 type/layout operation, a shared
ELF32 module, and a shared typed 16/32-bit x86 instruction model serve the
active kernel, driver, and Doom sources. Explicit native oracle contracts and
development commands still use GCC or Clang.

The preprocessing module owns translation-phase tokenization, ordered
object, function, and variadic macros, C11 conditionals and predefined macros,
`#line` locations, direct and macro-expanded includes, forced inputs,
guarded traversal, canonical once identity, pack metadata, and typed Cupid
`#exe` markers. Checked manifests classify all 2,461 include operands as
2,204 direct quoted plus 257 direct angle forms with zero macro operands
across 704 active C-family inputs. The generated manifest drives 397 tracked
profile runs under twelve profiles plus four generated kernel roots. The
profile counts are 156 kernel, three Doom compatibility, 80 Doom tree, three
user, 108 Cupid programs, 35 strict hosted i386 Linux, four strict hosted i386
Windows tool drivers, two other strict hosted i386 Windows roots, one
freestanding i386 Windows probe, two hosted i386 kernel
bridge, and three GNU hosted i386 runtime roots. Both 64-bit hosted profiles
now have zero roots.

The `toolchain:all` target bootstraps both checked compiler stages, builds
fifteen Linux Toolchain contracts and the runtime probe as static i386 ELF
files, compares seventeen objects and sixteen executables across the two
generations, and publishes 21 artifacts with a manifest. Fourteen regular
contract compiles remain parallel; `cupidc-object` starts alone after that pool
drains because its plan carries the larger 1,800-second budget. The audit also keeps
22 browser fragments under
`bin/browser.cc` and two delivered headers without an invented standalone
context. No hosted translation unit is deferred. Checked execution-seed
carriage and production selection are complete. The native Windows driver now
derives its PE plan from the verified Linux manifest and builds through stage
four. Linux and native Windows stage-three to stage-four convergence and seed
promotion are complete. WSL-free Linux-seed contracts and a Python-free driver
remain open. The narrower Windows user syscall ABI contract already runs as a
native PE and does not use the Linux contract publication.

`toolchain:all` builds both manifest modes from their checked closures. Verify
mode consumes `CUPMAN2` and follows the host-selected execution cohort, so it
runs as a native PE on Windows. Author mode consumes independent `CUPMAN4`
facts and is always a static Linux ELF built and run by the converged
stage-four Linux tools. Windows reaches the author through WSL. Together the
modes bind all 21 artifacts, 70 publication inputs, 50 bootstrap inputs, the
Linux publication seed, and 58 raw stage pairs. The pairs cover 17 contract
objects, 16 contract executables, 19 bootstrap C objects, one startup object,
and five tool images. The author requires regular, nonempty, byte-identical
streams and hashes both sides. It derives the 17 schema-v3 object-comparison
records, checks executable pairs against their artifact facts, and derives the
fixed-point summary from the exact pair inventories. The request has no caller
`all_equal` field. Python
repeats all 58 comparisons independently after the author accepts the request.
Schema `cupid.toolchain-contracts.v3` does not change. Only
actual contract build inputs receive compiler or assembler ownership; provenance-only
observations do not. The source-current schema v3 `CUPMAN4` publication passed
in 3,952.17 seconds and wrote 21 artifacts and a 27,071-byte manifest with
SHA-256
`ea41237781ef0662502dde675b94d06c92ffadd2154a5a9da8b987c0a01e5947`.
The Cupid author and Python oracle agreed on all 58 stage pairs. Every
stage-three object and executable matched its stage-four counterpart. The
hosted runtime passed, live inputs stayed frozen, and its final verifier printed
`Cupid Toolchain manifest: ok (21 artifacts)`. Both checked Python launchers
resolve `tools` from this checkout. The direct manifest module passes 40 tests
in 43.226 seconds, the publisher passes 62 in 7.266 seconds, and the pinned
verifier runner executes 25 tests in 32.773 seconds with three
POSIX-only skips on Windows.
The publisher gives `x86_contract.cc` its sibling `/toolchain/tests`
quoted-include root so checked CupidC can read both frozen x86 `.inc`
corpora. Other contract plans keep the narrower shared include path.

The verifier keeps one pinned repository reader from initial capture through
the checked build and final recheck. POSIX rewalks wildcard directories from
the pinned root instead of trusting cached child descriptors. Windows holds
parent-relative handles that deny replacement. A hard-linked replacement
directory with one added header fails, while a transient POSIX root rename and
restore cannot redirect any contract input.

The hosted `ctool_c_layout_types` contract fixes scalar, pointer, array, enum, vector, function-marker, aligned-wrapper, qualified-wrapper, struct, union, class, bit-field, flexible-array, packed, and explicit-alignment representation to the Cupid i386 target. Enum size, alignment, and signedness copy a frontend-selected compatible integer type. The independent manual active-source layout fixtures select signed `int` and are `4/4`; the declaration frontend now selects compatible types from source, including unsigned `int` for both nonnegative enums in the FAT16 closure. Positive contracts include a direct atomic pointer at `4/4`, a synthetic signed-`long long`-compatible atomic enum at `8/8`, and an aligned incomplete array retained as `0/16` until a compatible declaration supplies its bound. `QUALIFIED` represents `const`/`volatile`/`restrict`/`_Atomic` use of an existing semantic type without cloning its representation or record slice; a pointer to qualified `T` remains distinct from a qualified pointer to `T`. Non-atomic qualification preserves layout. Aligned wrappers carry an exact effective typedef/type-attribute alignment that may lower or raise natural alignment; explicit record alignment only raises the computed record result. Atomic identity propagates through both wrappers, but alignment follows source order: introducing `_Atomic` applies the cached target minimum, a later exact alignment may lower it, and later non-atomic qualification preserves it. Atomic aggregates remain unsupported. Layout enforces a flexible array's final structure position, while the declaration frontend enforces named-member eligibility, including names promoted through anonymous records. The operation resolves immutable index graphs with an iterative strong-edge walk, caches `_Bool`, active-atomic, and atomic-minimum facts once per type, preserves presumed/physical semantic locations, reclaims traversal scratch, and returns stable job-owned layouts transactionally in `O(types + members)`. A 4,096-wrapper/4,096-bit-field regression closes the former repeated-unwrapping path. Manual typed graphs pin all 54 FAT16 member offsets plus active Doom, process, syscall-table, `e1000_rx_desc_t`, and per-CPU ABI shapes.

The hosted `ctool_c_parse` operation consumes the ADR 0012 tape directly and publishes the ADR 0013 graph and completed layouts together with canonical job-owned file bindings, tags, normalized function-parameter types, definition-local parameter object types, source-ordered block bindings, function-scoped labels, semantic object initializers, first-declaration storage/provenance, effective C linkage, names, dual locations, and immutable postorder function-definition, statement, expression, and child tables. Definition records point at the canonical entity while retaining their exact declared type, storage, `inline`, body, and label slice. Each definition parameter retains its source storage and adjusted object type, including top-level qualification, while the parallel function-type entry stays unqualified for compatibility. The body grammar owns compound, declaration, expression, return, typed `if`/`else`, counted `for`, typed `while` and `do`, typed `switch`/`case`/`default`, identifier labels, direct `goto`, `break`, and `continue` statements; null expression statements with `CTOOL_C_AST_NONE`; typed `IF` nodes with a converted scalar condition, required body, optional `else_body`, nearest-unmatched-`if` association, and postorder bodies; typed `FOR` nodes whose initializer is `CTOOL_C_AST_NONE` when omitted and otherwise names a present expression or declaration statement, plus optional converted scalar conditions, optional converted iterations, and required bodies; typed `WHILE` and `DO` nodes with converted scalar conditions and required postorder bodies; typed `SWITCH` nodes with promoted integer conditions and required postorder bodies; folded, converted `CASE` constants; per-switch default and duplicate-value tracking; targetless `break` and `continue` leaves; canonical label identities shared by `LABEL` and `GOTO` nodes; complete automatic objects with none/`auto`/`register` storage and optional scalar, whole-record, character-array, or recursive array and structure initializers; represented block-scope static objects with implicit zero, target integer or floating constants, narrow character-array strings, direct narrow-string addresses, or recursive array and structure lists; block-scope external objects whose lexical aliases name canonical linked entities; file-binding, parameter, and block-binding references; decoded owned ordinary strings; target-typed integer and ordinary narrow character constants; explicit scalar/void casts; address/dereference and direct or anonymously promoted `.`/`->` member designators; the implemented integer operator ladder; pointer addition/subtraction and normalized subscripting; right-associative simple/compound assignment; prefix/postfix updates; prototyped and unprototyped calls; empty identifier-list definitions; and scalar `return` conversion. A `DO` body and its expressions precede its condition and loop node. A label owns its statement body in postorder, while a `goto` label reference is a semantic cross-reference that may point forward or backward. Each public block binding indexes the semantic initializer forest, whose direct-subobject edges live in a parallel immutable table. Uninitialized automatic objects use `CTOOL_C_AST_NONE`; supported static objects always retain a root record, including implicit zero initialization, while omitted aggregate subobjects remain implicit zero. A provisional binding at the future stable index makes the declared name visible through its own initializer at C's point of declaration, while later comma declarators remain invisible until declared. Automatic expressions reuse the shared assignment conversion without applying assignment's modifiable-lvalue requirement. Static integer records use the target integer evaluator and conversion. Static floating records keep target-width IEEE bits after integer-only binary32 or binary64 evaluation of represented arithmetic, comparisons, casts, truth, logic, and conditional selection. String records retain effective copied bytes plus the completed destination type. Direct string-address records own their decoded bytes and a zero addend. List records own direct-subobject edge slices, with every child root preceding its parent. Recursive automatic and static arrays and structures accept explicit braces, trailing commas, and brace elision within the represented forms. Automatic leaves retain runtime expressions; static lists still require constant-data leaves. Empty and excess lists fail precisely. Direct member and array designators select one immediate subobject in source order; positional clauses resume after the selection, and brace-elided children leave a following designator for the nearest explicit list. Chained selectors, promoted anonymous members, duplicate overrides, and positional union or Cupid class lists remain explicit boundaries. Explicit nodes retain lvalue/array/function/qualification, integer-promotion, usual-arithmetic, and assignment conversions. Compound assignments and updates retain one raw designator child plus distinct stored/result and computation types, so later lowering cannot duplicate side effects or lose postfix semantics. Each member AST node refers to one direct ADR 0013 graph member; promoted anonymous-record names publish an ordered chain of direct member hops rather than a flattened pseudo-member. Record qualification and register addressability provenance follow that chain, array decay retains element qualification, and narrow unsigned-`int` bit-fields promote according to their target width. Cupid i386 ranks and widths drive the body independently of the bootstrap host, including the ILP32 `long + unsigned int -> unsigned long` rule and signed-`int` pointer difference. The integer-constant-expression path accepts ordinary narrow character constants and integer-target casts over its represented operands, which covers the active `case (ctool_u8)'x'` shape. Out-of-range signed casts use Cupid's documented two's-complement target result. Static scalar floating expressions use the typed evaluator instead of the integer-constant-expression engine. Ordinary runtime expressions are typed but not evaluated by the integer-constant-expression engine, so divide by zero, signed overflow, and overshift source remains represented for later lowering rather than receiving declaration-time folding diagnostics. Non-VLA `sizeof`, alignment queries, and `__builtin_offsetof` are the deliberate exception: typed operands/member paths are checked against the current target graph and folded to unsigned 32-bit target constants. Unevaluated expression records and decoded-literal arena scratch are rewound before publication, so assertions add no entity, member, statement, expression, or unreachable string storage. Declaration statements index ordered public block-binding slices, while block-binding expressions refer to those stable entries. Lexical lookup walks inner block bindings, definition parameters, then file bindings; static integer evaluation honors the same shadowing before accepting a file-scope enumerator. Nested blocks may shadow parameters, file objects, outer locals, and typedefs, scope exit restores the hidden declaration, and the function body's outer compound correctly shares its parameter scope. One C11 loop scope covers a declaration initializer, condition, iteration, and body before expiring, while the block-item/statement split rejects a declaration used directly as a selection or loop body. Every retained child precedes its parent for direct later lowering.

The unchanged `/kernel/fs/fat16.h` closure still reproduces every FAT layout oracle. Exact additional contracts parse unchanged `kernel.h`, `irq.h`, `cupidscript.h`, and `shell.h` and merge representative duplicate prototypes and typedefs once at the first declaration. GNU `packed`, `aligned`, and `noreturn` lists retain their semantic destinations, and compatibility keeps stronger alignment and `noreturn`. File- and record-scope `_Static_assert` use target integer evaluation, including conditional selection and fault suppression in unevaluated arms. Active-source fragments prove all 26 tracked assertions across `memory.h`, `percpu.h`, `exec.cc`, `process.cc`, and `syscall.cc`.

The [audit-derived active-source gate](./ACTIVE-SOURCE-AUDIT.md) passes 161 of
164 general non-Doom headers in the C11 profile. `scheduler.h`,
`simd_intrin.h`, and the exact-decimal contract fixture retain exact expected
failures there. The checked seed parses all
29 declarations in `simd_intrin.h` under the Cupid profile. That mode now
maps `U0`, the signed and unsigned sized integer spellings, `Bool`, `bool`,
`float4`, and `double2` directly into the shared type graph. C11 continues to
treat those spellings as ordinary identifiers. The graph contains 739 active
language inputs: 31 assembly files, 297 headers, and 411 Cupid C files. No
ordinary C translation unit remains in the supported roots. It records 255
feature IDs, 452 transforms, and 25 accounted unreachable files. The preprocessor
inventory covers 704 files and 2,461 include occurrences, split into 2,204
quoted and 257 angle forms. Its active roots contain 397 tracked and four
generated translation units.

The ADR 0302 audit remains historical. Its active-source digest is
`20eb8f85d95d7a6acb071a81e1884dd0fb8a45dd52157763324f147c54ad6f52`.
The 2,700,777-byte audit JSON has SHA-256
`924000ec9449d4874142c4240094aa4865c015f9af9dfc3f23c0b4b2677e0ae4`,
and the 12,502-byte summary has SHA-256
`56a05868915f15f3db58cd1d5d0a26cc60ebee1b3d625d1356e0dd0aa8059a41`.

The source-current generation and deterministic check mode both pass. The
generated fixed-point inventory records failure, help, and success counts of
21/5/22 for Linux and 9/5/8 for Windows.

Across the three supported roots, CupidC participates in 250 transforms,
CupidASM in nine, CupidLD in nine, and CupidObj in 192. Python participates in
all 452 as the checked-tool launcher and host-side safety, parity, and
publication layer.
No transform invokes a host C compiler, and no recursive Make transform
remains. The user and Toolchain artifact publishers create their required
output directories. The user compiler uses POSIX `dir_fd` operations or
parent-relative Windows handles and rejects links, junctions, and a changed
resolved path before releasing the directory pins. No Python-only transform
remains in the supported graph.
The user syscall ABI gate is classified separately with CupidC, CupidASM,
CupidLD, the Cupid-built contract, and Host Python. Its checked executable owns
the ABI rules. Python remains the independent oracle and snapshot coordinator.
The kernel-symbol source is classified
as `generate_ksyms_source` with CupidDis, CupidObj, and Python participants.
The Doom profile delivery is
classified as `generate_profile_manifest` with CupidObj and Python
participants. Root `all` has 443 transforms, all with a Cupid participant. The
size verifier builds and runs a private CupidC contract with CupidASM and
CupidLD; Python retains capture, launch, and oracle work. The graph runs
CupidASM, CupidObj, CupidLD, and CupidDis from the checked seed. The audit
evaluates Make conditionals with the canonical Windows branch and C locale on
every host; direct Linux builds cover the Linux execution branch. One public
runner executes root Cupid tools. Checked production CupidC and checked user
CupidLD pass it the private seed capture their wrapper already owns. The
runner rechecks the live five-tool trust unit after every command and rejects
success when that check detects cohort drift.
Output-bearing wildcard lists pass through Make's `$(sort ...)` before
generators or link order consume them. ADR 0190 records the root tool handoff,
ADR 0196 removes host C from the normal Toolchain root, and ADR 0245 records
publisher-owned output directories. ADR 0246 records the shared invocation
boundary.

The two ISO fixture transforms are now explicit. `gen-big` freezes
`test_iso/big_pattern.asm` and the checked seed, asks CupidASM to assemble a
4,096-byte candidate, and compares it with an independent Python byte oracle.
Only an exact candidate can replace `test_iso/fixtures/big.bin`. A separate
`package_iso9660_image` transform checks the exact membership in
`test_iso/fixtures.manifest`, freezes that tree under ordinal private names,
and runs checked CupidObj `iso-fixture` to write the tracked ECMA-119 image
with fixed `RRIP_1991A` metadata and a continuation placed after the directory
stream. Python renders the same frozen tree independently and requires every
byte to match.
Make declares the same seven portable paths explicitly instead of expanding
raw manifest text or recursively walking a possible link. A test locks that
prerequisite list to the manifest.
Hostbuild checks the manifest, source, seed, and destination again before
atomic publication, rejects overlapping publishers with a per-output lock,
and preserves an identical output and timestamp. The concise source
uses `times 4096 db $`, whose location counter advances for each emitted byte.
NASM freezes `$` for that TIMES statement, so this fixture is the one explicit
production-source exception to optional NASM byte parity. The normal graph no
longer probes for
`mkisofs`, `genisoimage`, or `xorrisofs`. ADR 0191 records the format and
publication contract, while ADR 0241 records the CupidObj handoff. The settled
image passes the complete four-vCPU e1000
frontier, including the exact six-name ISO directory check and the existing
read, JPEG, mount-lifetime, graphics, audio, network, SMP, and USB checks.

The production handoff build completed in 502.232 seconds. Its private
209,715,200-byte image has SHA-256
`3f8c84cea61e5e8bfc4e6a5fc09a030a4d6451d258a4ca2ea6486a923d1d08e3`.
The complete private four-vCPU e1000 frontier then passed in 496.479 seconds.
Its 111,548-byte serial log has SHA-256
`7a396b57e758044ceca8cbd7deb2fdff3f9b9786632794a243710f36e12c7c02`
and contains the exact ISO listing, `PASS feature17_iso`, and CupidC JIT
completion markers. ADR 0241 records this evidence.

An initial Windows and Linux comparison matched 426 of 430 kernel artifacts.
The only differing input object wrapped a progressive JPEG that host FFmpeg
had rewritten differently on the two systems. The repository stores the
equivalent sequential baseline bytes. Hostbuild freezes the source and runs
checked CupidObj `wrap-jpeg` first. CupidObj accepts only sequential SOF0 or
SOF1 input, copies it exactly, and rejects progressive, unsupported, or
malformed marker streams. Python then checks accepted bytes independently and
controls atomic publication. The root build
no longer calls FFmpeg, `jpegtran`, `djpeg`, or
`cjpeg`. The Linux
kernel build passed in 607.7 seconds, and the Windows root build passed in
341.6 seconds. All 430 frozen kernel artifacts match byte for byte.

The matching `kernel/kernel.bin` is 8,490,228 bytes with SHA-256
`53770a93658e757d25f5aeab9d3e434d4a3be2a1dc3fbe4b19869e5bf9820a06`.
The fresh normal image is 209,715,200 bytes with SHA-256
`e815d2ef67f114a26181f0e2cbde85f892cdadd487f8d9cbee9715e720800b3e`.
A private `/bin/ls.cc` JIT boot from it passed in 49.8 seconds. ADR 0190
records the complete artifact table, log identity, and layout headroom.

The active-source digest for that cross-host comparison was
`cfb0e1dcd276154a4db5c2747ed092581874a54cd4c9fb379f204e3c10f8253e`.

External-inline policy now follows translation-unit finalization described by
[ADR 0131](../adr/0131-finalize-c11-external-inline-definitions.md). The
frontend recognizes external definitions across compatible declaration sets,
preserves inherited internal linkage, and rejects an external-linkage inline
declaration without a definition. Iterative memoized type relations normalize C
qualifier spellings while retaining atomic parameter identity, distinguish
strict typedef identity from compatibility, apply old-style/default-promotion
rules, accept a 512-level derived pointer graph, and construct symbol-local
immutable array/function composite types without corrupting shared typedefs.
Transactional tests cover precise conflicts, lexical-scope duplicates and
expiry, automatic and static initializer forests, explicit and tentative file
definitions, binding addresses, scalar and aggregate return or assignment
legality, recursive aggregate modifiability, pointer arithmetic and comparison
constraints, conditional association and conversions, loop and switch
constraints, direct jumps and label scope, compound/update constraints,
malformed literals, unsupported local storage forms, ownership, deep syntax,
constrained output, rollback, and recovery. Runtime expression values carry
private integer-constant-expression form and value metadata. A represented zero
expression, or that expression cast to non-atomic `void *`, becomes a null
pointer constant. Comparisons, conditionals, returns, calls, assignments, and
automatic initializers publish a destination-typed
`CTOOL_C_CONVERSION_NULL_POINTER`; static explicit nulls publish `ZERO` records
and discard their temporary expression AST. Comma expressions now evaluate left
to right and retain the last operand, and known-true loops preserve
non-fallthrough reachability. GNU `weak`, `section`, and `unused` attributes
publish canonical entity metadata; exact output-only assembly can snapshot
represented i386 register and EFLAGS state. The constant and body expression
grammars remain intentionally partial, and namespace and member lookup remain
linear. Chained designated paths, promoted anonymous members, duplicate
overrides, positional union or Cupid class lists, static member-address
constants outside the block-static symbol path, integer-routed and other
unrepresented address casts, automatic bases, runtime offsets and subscripts,
block declaration attributes, nested function definitions, computed goto and
GNU label addresses, broader GNU assembly forms, hexadecimal floating
constants, hexadecimal or subnormal long-double literals, long-double decimal
ratios beyond the bounded parser,
remaining floating-to-wide conversions, nonempty identifier-list
definitions, non-scalar arguments without declared parameter
types, aggregate variadic reads, block assertions, variable-length arrays and
runtime `sizeof`, the remaining GNU attributes, complete Cupid extensions,
complete AST and IR coverage, broader function code generation, full
translation-unit emission, and production integration remain later work. The
shared hosted path owns the 156-source strict non-Doom cohort, all 83 Doom
roots, the generated kernel symbol translation, and the six checked
generated-install or user translations; the private kernel compiler remains the
embedded runtime JIT and AOT path. ADR 0196 adds block-static address
initializers, earlier static `const` integer reuse, automatic `long double`
transport, and zero-filled static long-double objects without claiming the
broader forms. ADR 0199 adds non-atomic long-double comparisons. ADR 0202 adds
floating truth, controlling operands, and conversion to `_Bool` at all three
represented widths. ADR 0250 adds runtime `float` and `double` conversion to
unsigned four-byte targets. ADR 0251 adds exact bounded decimal static
long-double data. ADR 0253 adds runtime conversion between `long double` and
every signed or unsigned i386 integer width. ADR 0254 adds static initializer
conversion between bounded finite `long double` and every represented value
integer. ADR 0255 adds static long-double control folding and finite
floating-width conversion. ADR 0256 defines the shared canonical x87 decoder
and adds static infinity, NaN, and subnormal transport. ADR 0260 adds
integer-only static long-double arithmetic with exact x87 rounding and no
runtime IR. ADR 0287 adds runtime integer conditional arms with `float` and
`double`. ADR 0288 applies runtime usual arithmetic conversions between every
represented value integer or enum and `long double` for arithmetic,
comparisons, and conditional selection.
ADR 0289 removes the remaining four-byte source-head limit on integer input to
`float` and `double` casts, assignment conversion, arithmetic, comparisons,
and conditional selection.
ADR 0293 gives source-head decimal `float` and `double` literals one exact
target-width converter through subnormal, underflow, and overflow results.

The latest local normal build completed in 1,444.7 seconds. Its
9,093,772-byte final ELF has SHA-256
`974abfa333ec21b430e5d33aecb379209e65aaee9489ea839e7984fcdbf2c2a8`.
CupidObj flattened it to an 8,885,540-byte kernel with SHA-256
`2de89e74c873969c59745df46733dfcb9a2888cd607c6a6a868accb4a08fee13`.
The 209,715,200-byte image has SHA-256
`b8bb7170975141b38e6d136b22ebf736571389f70457c65e5a1443d3d253a489`.
A focused private four-vCPU e1000 boot passed from that image in 66.429
seconds. CupidC compiled and ran `/bin/feature13_double.cc`, observed the
existing unsigned conversion and remainder markers, and completed JIT
execution. Its 38,060-byte log has SHA-256
`67650acbcfb0b110ce90c098cc0aa8d58a75c48bcf96a1bf3326d33f458ecbb8`.
This boot checks the existing ELF image path; it does not execute a PE image.
The checked image compiler predates ADR 0253, so this smoke is a regression
check rather than direct execution of the new hosted long-double conversion.

The earlier [ADR 0235](../adr/0235-transfer-jpeg-acceptance-to-cupidobj.md)
checkpoint used a 209,715,200-byte image with SHA-256
`c71fd7f5a03a4e55f4de45e6b93d4284375fb5600f4df3cda62b7f4043c33b33`.
Its complete private four-vCPU frontier passed with both supported NICs. e1000
finished in 545.151 seconds, and RTL8139 finished in 536.668 seconds. Both
historical runs captured non-silent AC97 and PC-speaker output and contained no
panic, fatal error, assertion failure, exception, or triple-fault marker.

The earlier static `const` integer rule is a narrow Cupid C extension rather
than an ISO C integer constant expression. It preserves the unchanged
`atomic_oracle_execute` address tables, which strict GCC and Clang already
fold, without making mutable or indirect object reads compile-time values.

ADR 0116 extends the entity-attribute list above with GNU `used` and
`__used__`. The metadata is canonical and validated, and the checked seed now
uses it for the production symbol-source recipe. ADR 0119 adds the exact
FXSAVE pointer input used in `process.cc`. That translation unit now passes
the complete checked profile and produces its normal object through CupidC.

ADR 0141 adds compiler-head semantics for GNU `noinline` and
`target("general-regs-only")`. Compatible declarations merge each fact into
one canonical function. `noinline` preserves the request for a future
inliner. Each IR function retains the canonical code generation mask. The
target form rejects compiler-generated floating work in Linear IR, and
object placement checks both the mask and invariant again. Explicit
assembly remains under its own contract, so an exact `FNINIT` statement is
valid. At that boundary, unchanged `kernel/cpu/fpu.c` passed its target
attribute and stopped at the independent `"m"(mxcsr)` input to `ldmxcsr` on
line 28. No checked seed or production source changed in that increment.

ADR 0146 advances that exact FPU frontier. The checked seed accepts the volatile
`ldmxcsr %0` form with one addressable, non-atomic 32-bit integer `m` input
and no outputs or clobbers. Linear IR evaluates the address once. The i386
emitter loads it into EAX and emits `0F AE 10` through the shared x86 model.
The deterministic two-function contract object is 400 bytes with 40 bytes of
text and no relocations.

Checked-seed CupidC accepts exact `fldcw %0` through the same state-memory
input boundary. It requires one addressable, non-atomic 16-bit integer `m`
input. GNU semantics make the no-output statement volatile even without that
keyword. Linear IR evaluates the address once, and the emitter produces
`D9 /5` through EAX.
Frontend, Linear IR, and object contracts retain the older LDMXCSR cases and
add wrong-width, invalid-lvalue, forged-metadata, deterministic-output, and
same-job recovery checks for FLDCW. ADR 0258 records seed carriage.

ADR 0148 carries the unchanged source through that MOVSS round trip and the
matching one-way load and store. Each exact volatile form keeps one or two
typed `float` addresses and requires the `xmm0` clobber. Linear IR evaluates
each address once in source order. The shared x86 path emits
`F3 0F 10 00` and `F3 0F 11 00` through EAX. The deterministic three-function
contract object is 464 bytes with 79 bytes of text and no relocations.
ADR 0150 carries the next exact volatile block in `stress_sin()`. Its
modifiable `double` `=m` output and addressable `double` `m` input retain
typed addresses and permit no clobbers. Linear IR evaluates the output
address before the input address, once each. The shared x86 path emits
`FLD qword [EAX]`, `FSIN`, and `FSTP qword [EAX]` with balanced x87 depth
and no frame temporary. The deterministic two-function contract object is
440 bytes with 70 bytes of text and no relocations. Two full compiler-head
builds of unchanged `kernel/cpu/fpu.c` produce the same validated 6,620-byte
object. At the ADR 0150 boundary, production ownership had not moved. The
later transfer renamed the source to `kernel/cpu/fpu.cc` and placed its
unchanged 6,620-byte object under the checked normal wrapper.
The production contract also decodes the exact `fpu_init_cpu()` symbol. It
rejects helper calls and floating work before the CR4 write, requires one
`FNINIT` followed by one 32-bit memory `LDMXCSR`, and rejects any other
floating work in the function. Its negative object replaces the CR4 write
with three NOP instructions and fails before `FNINIT`.

ADR 0160 adds the exact volatile flags-restore form used twice by
`simd_cpu_has_cpuid()`. It takes one non-atomic 32-bit integer through `r`,
has no outputs, and requires exactly one `cc` clobber. The frontend and
Linear IR keep that clobber as public metadata. The emitter consumes the
evaluated value through EAX, pushes it back, and emits POPF through Cupid's
shared x86 model. ESP remains balanced, and the path needs no temporary or
relocation.

ADR 0168 adds compatible fixed-register input sharing with a write-only
output. The unchanged CPUID statement keeps `=a` for its EAX output and `a`
for its leaf input. CupidC records output zero in the input's
`matching_output` field without
replacing the source constraint. Frontend and Linear IR require the same
fixed register, represented integer types, and equal widths. Frozen
same-width pointer, floating, and aggregate inputs fail without publishing
IR or an object. The emitter repeats those checks, loads the evaluated leaf
into EAX immediately before CPUID, then snapshots all four outputs through
the existing EBX-preserving path. Numeric ties keep their existing behavior.
The checked seed now carries unchanged `kernel/cpu/simd.cc` past that overlap
and through all six packed SSE2 statement shapes. The frontend and Linear IR
lock the exact ordered pointer and 32-bit integer inputs plus each memory and
XMM0 through XMM7 clobber set. Emission uses Cupid's shared x86 model for the
copy, broadcast, blend, and saturating-add paths. Two checked-seed builds produce
the same validated 8,768-byte object with SHA-256
`fd280c321b8eb38a90d4f0982d70b8df0364585e3da322eb2c9de722e071f8d4`.
The normal recipe freezes the source and its seven-header closure before the
checked wrapper validates and publishes the object. ADR 0178 records the
packed SSE2 boundary, ADR 0179 records seed carriage, and ADR 0180 records
production ownership.

ADR 0154 represents the complete unchanged x87 round-down statement in
`str_floor()`. It requires one modifiable `double` `=m` output, one
addressable `double` `m` input, and the exact `ax` plus `memory` clobber set.
Linear IR evaluates the output address before the input address, once each.
After loading the input, the emitter reuses its consumed address slot below
ESP for the saved and temporary control words. The pending output address at
`[ESP]` remains intact. The 44-byte direct sequence saves the caller's x87
control word, selects round toward negative infinity, executes `FRNDINT`,
restores the original word, and stores the result with balanced x87 depth.
The two 71-byte fixture functions form a 524-byte object with 142 text bytes
and no relocations.

The exact unchanged `str_floor()` definition compiles twice to the same
420-byte object with SHA-256
`448012fe57ec625c6075e97cf91163b994a0443238c5d6bdf25e4b839763f14e`.
The checked seed also accepts the later explicit non-atomic `double` to
`uint64_t` casts. It divides by 2^32 to obtain the high word, reconstructs
that multiple exactly, subtracts it, and truncates the remainder to the low
word. The shared-decoder oracle covers the active range through the largest
binary64 value below 2^64. Two complete compiles of unchanged
`kernel/core/string.cc` produce the same 14,460-byte object with SHA-256
`d48bb6ea18b7124fbefeaca0d5d5ee8a517db950f21ea88e30ededd6c5c2a577`.
The normal recipe freezes `string.cc`, `string.h`, and `types.h`, then uses
the checked wrapper to validate and publish the object. A poisoned-host
rebuild reaches the same object without GCC, Clang, or a host assembler.
ADR 0170 records the conversion boundary, and ADR 0181 records production
ownership.

ADR 0175 represents the exact operand-free volatile statement that begins
the external, prototyped `void _start(void)` body in `.text.start`. It
requires the EAX, ECX, EDI, and memory clobbers plus visible external object
declarations for `_bss_start` and `_kernel_end`. Frontend statement depth and
Linear IR body identity reject a leading statement or a reset hidden by a
label or another nested body.

The checked seed reads the stack top from the exact statement. It accepts one
through eight hexadecimal digits, rejects zero or a value that is not aligned
to 4 KiB, and emits the parsed `imm32`. The function still cannot have a
compiler-managed frame. It copies ESP to EBP, loads both linker symbols,
derives the doubleword count, clears EAX, then emits CLD and REP STOSD through
the shared x86 model. Its following `kmain()` call uses stack-base residue
zero, and a returning `kmain()` reaches an interrupt-disabled halt loop. The
active source installs `0x01100000`, the top of the fixed two-MiB stack.

The exact fixture has 42 text bytes and three relocations. Its 27-byte
assembly body has `R_386_32` relocations at offsets 11 and 16. The
`R_386_PC32` call relocation is at offset 31. Two runs of the Cupid-built
compiler emit unchanged `kernel/core/kernel.cc` as the same 25,920-byte
object with SHA-256
`ed42676ad0d7f16b1fb83442ead1b0082781324dca719104922099cee34b5ab0`.
The normal recipe freezes the source and its 63-header recursive closure.
Poisoning `CC` leaves the recipe on the checked wrapper, and CupidDis decodes
the stack reset, linked BSS clear, `kmain()` call, and halt loop. ADR 0180
records production ownership. ADR 0185 records the variable, page-aligned
stack-top boundary, ADR 0186 records its checked-seed promotion, and ADR 0187
records the coordinated memory-map move.

ADR 0157 carries the four descriptor-table and segment-register assembly
forms in unchanged `kernel/smp/percpu.c`. The LGDT forms require one
addressable, non-atomic, complete six-byte `m` input and the exact `ax` plus
`memory` clobbers. The code-segment reload keeps its `memory` clobber. The GS
form requires one represented 16-bit `r` input. Linear IR lowers the packed
GDTR as an address and the selector as a two-byte value.

The shared x86 emitter writes the 48-bit LGDT operand, the AX immediate, and
the DS, ES, SS, and GS moves. Code-segment reloads use a relative
call-and-RETF trampoline instead of an absolute compiler-local label. The
fixture object is 528 bytes with 117 text bytes, five sections, five symbols,
and no relocations. Two complete compiler-head compiles of unchanged
`kernel/smp/percpu.c` produce the same 6,760-byte object with SHA-256
`3c2c6f0e00e5edec1ca16cba91e9fc593d1c42e24f4ebd3591e5f574fb0dd772`.
ADR 0157 recorded that compiler boundary against the `.c` source. The
production source is now `kernel/smp/percpu.cc`, and its object belongs to the
checked normal wrapper.

ADR 0155 gives file-scope GNU basic assembly its own frontend and Linear IR
tables. The checked seed emits the twelve exact x87/SSE floating wrappers at
the start of the then-named `kernel/cpu/libm.c` as source-ordered,
prologue-free global functions. The shared x86 encoder produces all 248 text
bytes, and the object
has no relocations. The checked seed accepts `[identifier]` labels on statement
operands and resolves `%[identifier]` to the existing numeric index before
public metadata freezes. The same lvalue, atomic, type, and constraint checks
apply to named and numeric operands, and `%%` remains escaped text.

The checked seed represents the complete x87 statements in `libm_pow_impl()` and
`libm_powf_impl()`. The double form requires one modifiable `double` output
and four addressable `double` inputs. The mixed form requires one modifiable
`float` output, two addressable `float` inputs, and two addressable `double`
inputs. Both require one memory clobber. Linear IR evaluates each statement's
five addresses once in source order. Each focused emitter proof contains 116
exact text bytes, no relocations, the corrected `DC E9` forward-subtract
encoding, and balanced x87 depth. The checked seed also retains the legacy
`DC E1` reverse subtraction for source compatibility. Those
blocks exposed the following
`sqrtsd` statement in `libm_sqrt_impl()`.

The checked seed represents that exact volatile square-root statement. It
requires one modifiable, non-atomic `double` `=x` output, one non-atomic
`double` `x` input, and no clobbers. Linear IR evaluates the output address
before the input value. The emitter uses XMM0 internally for `MOVSD`,
`SQRTSD`, and the final `MOVSD` store. The 65-byte focused function has no
relocations.

The checked seed also represents the exact volatile x87 statement in
`libm_atan2_impl()`. It requires one modifiable, non-atomic `double` `=m`
output, two addressable, non-atomic `double` `m` inputs in `y`, `x` order,
and one `memory` clobber. Linear IR evaluates the three addresses once in
source order. The 53-byte focused function has no relocations, and its
15-byte statement sequence comes entirely from the shared x86 model. The
full source then proceeds to the x87 exponent statement in
`libm_exp_impl()`.

The checked seed also represents that exact volatile exponent statement. It
requires one modifiable, non-atomic `double` `=m` output, two addressable,
non-atomic `double` `m` inputs in `x`, `log2e` order, and one `memory`
clobber. Linear IR evaluates all three addresses once in source order. The
71-byte focused function has no relocations, reaches x87 depth three, and
returns to its incoming depth.

The checked seed represents the following aligned mask block and the exact
`fabs` and `fabsf` wrappers. The mask effect reserves the first 32 bytes of
`.rodata` at alignment 16 and defines local `STT_NOTYPE` labels at offsets 0
and 16. Later read-only C objects follow the masks. The wrappers contain 15
and 14 text bytes and carry one `R_386_32` relocation each to the matching
mask.

The checked seed also represents the next eight file-scope rounding wrappers:
`floor`, `floorf`, `ceil`, `ceilf`, `round`, `roundf`, `trunc`, and
`truncf`. Each saves the caller's x87 control word, clears its rounding
field, selects the source mode, applies `FRNDINT`, and restores the original
word. The four pairs select down, up, nearest-even, and toward-zero mode. The
nearest-even pair emits no OR instruction. The family occupies 384 exact
text bytes, uses no relocations, reaches x87 depth one, and balances ESP and
x87 depth.

The checked seed also represents the exact `fmod` and `fmodf` definitions.
Each loads `y` below `x`, repeats `FPREM` while x87 status-word C2 is set,
and uses a checked short `JNE` with displacement `-10`. After convergence it
discards ST(1), returns the remainder through XMM0 at the source width, and
restores ESP and x87 depth. Both functions contain 35 exact text bytes and
no relocation.

The checked seed represents the aligned `libm_log2e_const` and
`libm_ln2_const` block and the following `exp2`, `exp2f`, `exp`, `expf`,
`log2`, `log2f`, `log`, and `logf` wrappers. The two local constants occupy
16 `.rodata` bytes at alignment eight. The wrappers add 264 text bytes. The
four natural forms have one `R_386_32` relocation each, while the base-two
forms need none. Decoder contracts check every instruction, each operand,
x87 depth, and ESP balance. The full source then proceeds to `pow` at line
846.

The checked seed also represents `pow`, `powf`, `asin`, `asinf`, `acos`,
`acosf`, `sinh`, `sinhf`, `cosh`, `coshf`, `tanh`, `tanhf`, `cbrt`,
`cbrtf`, `hypot`, `hypotf`, `nextafter`, and `nextafterf`. Each wrapper
copies its one or two original cdecl arguments, calls the matching external
`libm_*_impl` function, reclaims the copied words, and moves the ST(0)
result into XMM0. Shared emission covers four stack shapes and adds 558 text
bytes with 18 `R_386_PC32` relocations. Each relocation has a known `-4`
addend. The decoder checks the argument copies, call, cleanup, result
bridge, return, ESP balance, and x87 balance.

Two exact kernel-profile compiles of the corrected
`kernel/cpu/libm.cc` produce the same valid 16,164-byte ELF32 relocatable
object with SHA-256
`c0911732361f2e1ea78aa778f834719ba12208cc2d9f0a312455a5e6a38a75b4`.
General file-scope GAS still fails at the CupidC boundary.

Production now uses `kernel/cpu/libm.cc`. The checked wrapper freezes its
43,736 bytes with `kernel/core/types.h` and `kernel/cpu/libm.h`. The source
has SHA-256
`baffe801c7573b8500c60251298a753f60732608d58443178be8ce9ab809ef93`.
Seven aligned GNU mnemonics now emit `DC E9` without changing the algorithm,
stack order, source size, or ABI. The runtime gate launches
`/bin/feature15_libm.cc` and requires the seven-case x87 summary, all 29
checks, and `PASS feature15_libm`. ADR 0159 records the naming boundary. ADRs 0161
through 0165 record the five statement blocks. ADR 0166 records `fabs`, ADR
0169 records rounding, ADR 0171 records remainder, ADR 0172 records
exponent/log, ADR 0173 records the final cdecl bridges, ADR 0174 records
checked-seed carriage, ADR 0176 records production ownership, and ADR 0209
records the active range-reduction correction.

ADR 0156 represents the naked interrupt entries in unchanged
`kernel/smp/smp.c`. A naked function must have type `void (void)` and contain
one complete assembly statement. The reschedule and call wrappers accept
exact `pushal`, a direct canonical C-function call, `popal`, and `iret`
sequences. The panic wrapper accepts exact `cli`, `hlt`, and a relative jump
back to the halt instruction. The i386 emitter omits every compiler-managed
frame and return instruction. Cupid's shared x86 model emits each eight-byte
call wrapper with one `R_386_PC32` relocation and the seven-byte panic loop
without a relocation. Two complete compiler-head builds produce the same
validated 8,444-byte object with SHA-256
`806509a6dd1ac7eb34b7ffcb67a1f8852950663a274145584d0260da76dcba54`.
That hash records the earlier `.c` path. The checked production source is now
`kernel/smp/smp.cc`; its 8,444-byte object has SHA-256
`bd3189b2a1a6d15728c559172f5d6acca0889103428085cec8cc1024742a22d1`.
The difference comes from the existing `__FILE__` diagnostic. The wrapper,
image, and four-vCPU dual-NIC runtime gates pass.

ADR 0121 adds the three machine-state memory outputs used by the FPU panic
path. Exact volatile `fnstsw %0` and `fnstcw %0` templates accept one
modifiable 16-bit `=m` output, while `stmxcsr %0` accepts one modifiable
32-bit output. Linear IR evaluates the lvalue once. The i386 emitter consumes
that address directly and encodes the instruction through the shared x86
model, without a register-output staging area. Other `=m` templates remain
unsupported. `kernel/core/panic.cc` uses these statements, and the checked
compiler also supports its later exact call-next template.
Two complete kernel-profile compiles produce the same validated 10,212-byte
ELF32 object with SHA-256
`84daa51a65d6970ae7a7918b05fe64b7676c39d3309264375e349cf0ae20d428`.
The checked seed carries this capability, and the normal panic recipe uses it.

The body operator ladder uses checked value/operator vectors and an iterative precedence reducer. A recursive wrapper per precedence tier was rejected after the established 256-deep nested-call contract exhausted the default Windows host stack before reaching its transactional syntax-limit diagnostic. The iterative reducer keeps binary chains bounded by job storage while calls, parentheses, unary nesting, and assignment continue to consume the explicit 256-level budget. A 4,096-operator flat addition oracle publishes 8,193 left-associated postorder expressions and 8,192 child references; a 256-byte output ceiling on the same source proves one limit diagnostic, complete arena/scratch rollback, tape and prior-result preservation, and same-job recovery. High-bit ordinary character bytes sign-extend into signed target `int`; compatible enum/integer assignment retains an explicit destination conversion; and freeze requires every assignment's `computation_type` to equal its result type. Source-head decimal `float` and `double` constants publish exact target bits across normal, subnormal, underflow, and overflow results. Bounded decimal normal `long double` constants publish their exact 64-bit explicit significand and biased x87 exponent. Hexadecimal floating constants, hexadecimal or subnormal long-double constants, and long-double decimal ratios beyond the bounded parser retain focused unsupported diagnostics.

The call subset accepts fixed prototypes, direct or indirect variadic prototypes, and function types without prototypes. It applies the shared scalar or compatible aggregate assignment conversion to named parameters. Ellipsis arguments and every argument without a declared parameter type receive lvalue conversion, array and function decay, integer promotion, and `float` to `double` promotion as required before IR accepts four-byte integers and pointers, signed or unsigned eight-byte integers, `double`, or automatic `long double`. Aggregate transport at those call boundaries remains open. Lvalue conversion removes top-level `const`, `volatile`, and `_Atomic` from the result while retaining the qualified source child, and nested calls consume the shared 256-level syntax budget instead of recursing without a host-stack bound.

Checked production kernel, generated-install, and user CupidC calls plus
checked user CupidLD links use the same seed runner as root tool commands. Each
wrapper supplies its frozen five-tool capture. The runner verifies the complete
live cohort again before returning success.
ADR 0246 records the invocation boundary for these calls.

The current production graph supersedes two details in the long capability
summary below. The SMP recipe assembles a private 4,096-byte candidate, applies
the exact mixed-mode map with CupidDis `--require-known
--require-local-targets`, and replaces the
previous trampoline only after both tools pass. Windows output-bearing tools
run from the checked native PE32 execution seed. The fixed point and complete
Toolchain contract cohort retain the Linux seed and WSL. Artifact-size policy
keeps Linux manifest provenance while its Windows contract runs natively. ADRs
0271, 0272, and 0297 record the preceding boundaries, and ADR 0305 records
local-target adoption.

[CupidDis](../adr/0008-typed-cupiddis-inspection-report.md) is fully shared between its native CLI and kernel adapters. Raw input accepts one explicit 16-bit or 32-bit mode or an ordered borrowed range map whose kinds are code16, code32, and data. The hosted CLI spells typed transitions as `--range-at OFFSET:16|32|data` and keeps `--mode-at OFFSET:16|32` for code-only maps. Code ranges use the shared decoder. Data ranges produce bounded `db` rows without entering it. A public integration test assembles the active SMP trampoline and checks code in `[0x000, 0x01f)` and `[0x210, 0x254)`, with data in `[0x01f, 0x210)` and `[0x254, 0x1000)`. ADR 0080 records the original mode map, and ADR 0200 records its typed extension. The shared x86 catalogue carries all sixteen i686 `CMOVcc` conditions in 16-bit and 32-bit widths, with same-width register or memory sources. CupidASM accepts fourteen conventional alias spellings, and CupidDis always renders the canonical condition. The catalogue also carries the complete 16-bit and 32-bit three-operand `IMUL` family. It uses `69 /r` for a full immediate and `6B /r` for a sign-extended byte, with register or memory sources in either mode. The checked seed and source head carry canonical 16-bit and 32-bit SHRD with register or memory destinations and either an immediate byte or fixed CL count. Both modes honor operand-size and address-size overrides. Active checked-CupidC objects now decode their `shrd eax, edi, cl` sites directly instead of producing fallback data rows. Ordinary compiler padding shares the same authority: plain `90`, `66 90`, and word or doubleword `0F 1F /0` register and memory forms encode and decode under the usual operand-size, address-size, and segment rules. A private 32-bit recognizer accepts only the five measured Clang padding strings with two through six `66` prefixes and the exact `2E 0F 1F 84 00 00 00 00 00` tail. The decoded form is automatic, so CupidASM and the encoder cannot request redundant prefixes. Other duplicate prefixes remain invalid. The checked seed and source head carry 604 rows, 249 canonical mnemonics, 64 registers, and fingerprint `55A8970F`. A fingerprint-bound every-form contract covers 1,202 encodable legal-mode cases with the real encoder, both real decoders, and exact-form replay. It also locks 12 aliases, four invalid rows, two illegal-mode rejections, every declared row flag, and 2,641 proper prefixes under witness digest `8C570035`. A native CupidASM-to-CupidDis selector path checks exact bytes, strict known inspection, deterministic rendering, and canonical aliases. ADR 0298 records this proof. The catalogue includes signed x87 `FILD` and `FISTP` memory operands at 16, 32, and 64 bits and canonical `SETP` and `SETNP` byte predicates. ADR 0226 records SHRD, ADR 0228 records its seed carriage, ADR 0252 records the x87 integer forms, ADR 0258 records the preceding seed, ADR 0259 records the parity predicates, and ADR 0265 records their checked-seed carriage. One freestanding CupidASM implementation produces raw, ELF32 relocatable, and fixed-image artifacts for both its hosted CLI and the in-kernel JIT/AOT commands; it owns five production assembly artifacts and supplies startup for the private artifact-size and Toolchain manifest contracts. CupidObj is a producing participant in 192 normal-build outputs: 175 canonical text wraps, eight byte-exact binary wraps, one Python-assisted JPEG wrapper, one Python-coordinated disk image, one Python-guarded ISO fixture, one Python-guarded Doom profile manifest, the flat kernel image, three installation-source generators, and the kernel-symbol source generator. ADR 0084 records the text and binary boundary, ADR 0204 records the installation-source transfer, ADR 0224 records the kernel-symbol transfer, ADR 0227 records the ISO lane fixture transfer, ADR 0238 records the disk-image transfer, ADR 0241 records ISO production ownership, and ADR 0244 records profile-manifest production ownership. CupidLD owns the two-pass kernel link and all three separate user-program links, and it links the private artifact-size and Toolchain manifest contracts. No standalone host assembler, ELF linker, `objcopy`, or symbol-reader command produces an OS or user artifact. CupidDis owns the normal two-pass kernel's symbol extraction through its deterministic `-n` view; the checked pass-one kernel produces a 114,851-byte blob. Checked CupidObj serializes the generated Cupid C source, while Python verifies the bytes independently. Root `all` runs all five production Cupid tools from a checked seed. Linux uses the static i386 Linux cohort, and Windows uses the native PE32 execution cohort for output-bearing work. The host C compiler and native linker remain confined to explicit native oracle and development commands. The audit records 249 active CupidC participations through guarded Python wrappers: 246 ordinary C-output transforms plus the native Windows ABI, artifact-size, and Toolchain manifest verifications. That count includes the three user translations. Checked CupidLD executes the three user links plus the native ABI, artifact-size, and Toolchain manifest contract links. The optional native Windows drivers remain byte-exact oracles and still depend on Clang and its Windows linker. NASM and GNU/LLVM `nm` remain optional oracle tooling only. ADR 0190 records the root handoff. Checked revision `1e079d1` independently reproduces the 447-artifact root/user/toolchain cohort on Windows Clang/LLVM and Linux GCC/binutils; it predates the hosted preprocessor and active-corpus contracts.

The ownership counts in the preceding long-form summary are superseded by
CUPMAN4. CupidC participates in 250 transforms, CupidASM and CupidLD in nine
each, CupidObj in 192, and CupidDis in six. Four semantic contracts participate,
and no transform is Python-only. Python remains present in all 452 transforms
for capture, safety, oracle comparison, and publication.

Checked-seed CupidDis also publishes a typed summary of known, unknown, invalid,
and truncated instructions across selected code regions. The hosted
`--require-known FILE [FILE...]` policy validates every input, writes no
listing, and names each failing path with all four counts. Declared raw data
and non-executable ELF regions are excluded. The normal kernel path validates
and flattens one frozen cohort. Its 9,076-byte LF-only manifest has SHA-256
`4f1936423ae06418fc2f75603c29a91997608fe82f48c323321523aed25a2ab0`
and lists 431 unique graph-ordered relative paths: all 429 audited root object
outputs plus the pass-one and final kernel ELFs. Make retains those paths as
direct prerequisites. The first separate validation command used the earlier
429-path manifest, froze and rehashed every selected input, and passed
in 185.526 seconds with exit 0 and empty streams. The first reviewed hostbuild
transaction at that boundary also froze all five selected seed artifacts and
the existing `kernel.bin` boundary, then ran checked CupidDis and checked
CupidObj against one private cohort. It rechecked live trust inputs and the
output before parent-relative atomic publication. Every failure preserved the
prior kernel. That transaction passed in 187.054 seconds with exit 0, before
strict linked-image local-target validation joined the production path. The
current transaction keeps those freeze and publication guards and runs the
broad, linked, and flat checked sequence. Its source-consistent evidence is in
the current evidence table above and the bootstrap log. Expanding the inputs
directly exceeded the Windows 8,191-character command limit and truncated
after 396 paths. The
manifest reduces the evaluated command to 163 characters. Fourteen focused
hostbuild and build-graph tests passed in 36.591 seconds. ADR 0262 records the
source boundary, and ADR 0265 records seed carriage and production adoption.

Source-head and checked-seed CupidDis add executable relocation ownership to the same typed
summary. For an ELF32 relocatable object, `R_386_PC32` must begin at a decoded
four-byte relative field and `R_386_32` must begin at a decoded four-byte
non-relative field in the relocation's target section. The report exposes the
total and unmatched executable relocation counts. `--require-known` rejects a
nonzero unmatched count and includes both values in its path-specific failure.
Data-section relocations remain outside the code policy, and ordinary
relocation rendering uses the same matching function. Both checked production
seeds now carry this policy, and the public CupidASM object transaction applies
it before publication. ADR 0290 records the source capability, and ADR 0292
records seed carriage.

The trampoline intervals above are half-open: code occupies
`[0x000, 0x01f)` and `[0x210, 0x254)`, while data occupies
`[0x01f, 0x210)` and `[0x254, 0x1000)`.

The checked seed and source head carry 604 rows, 249 canonical mnemonics, and
fingerprint `55A8970F`. The catalogue includes signed x87 `FILD` and `FISTP`
memory operands at 16, 32, and 64 bits and canonical `SETP` and `SETNP` byte
predicates. The private CupidC comparison sequences remain aligned
when CupidDis reaches their parity guards. The guest disassembles and executes
the bounded `test_fpaug.cc` parity cases before running the full feature-13
behavior. The GUI shell keeps that listing in the terminal and mirrors it to
serial after its normal sink and redirection checks. The runtime gate sees the
actual in-kernel disassembler output without changing text mode. The four SHRD
rows cover immediate
or CL counts at both operand widths. The forward x87 row is canonical
`FSUB ST(1), ST(0)`, encoded as `DC E9`, for the corrected GNU
`fsubr %st, %st(1)` exponent range subtraction. The four forms added in the
preceding seed are the
80-bit x87 `FLD` and `FSTP` memory forms, the i686 `FUCOMIP ST0, ST(i)`
register form, and operand-free `FLDZ`. Both checked stages rebuild that
catalogue before compiling the Toolchain contract cohort. ADR 0203 records
the preceding seed promotion, ADR 0207 records the forward-subtraction
boundary, ADR 0208 records its seed carriage, ADR 0226 records SHRD, and ADR
0228 records SHRD's first seed carriage. ADR 0243 records the preceding seed,
ADR 0252 records the x87 integer forms, ADR 0258 records the preceding seed,
ADR 0259 records the parity predicates, ADR 0265 records their preceding
checked-seed carriage, ADRs 0280 and 0292 record later preceding seeds, ADR
0305 records the raw local-target seed, and ADR 0312 records the preceding local-target seed, and ADR 0318 records the current seed.

ADR 0196 supersedes that paragraph's hosted-contract ownership sentence. The
normal Toolchain contracts are now built by the checked i386 CupidC and
CupidLD path. GCC or Clang and a native linker are used only when an explicit
native oracle or development command is requested.

Checked-seed CupidObj accepts `iso-fixture`. Its freestanding operation
consumes an ASCII manifest plus a typed inventory of logical directories and
loaded files. It reproduces the complete 61,440-byte tracked ECMA-119 and
`RRIP_1991A` image, including both path tables, block-contained directory
records, fixed metadata, the forward continuation, and the deliberate lack of
`ST` fields. The command rejects manifest disagreement, unsafe or colliding
logical paths, bad parent graphs, invalid source views, depth and storage
limits, and preserves prior output on failure. The checked five-tool seed now
carries the command and repeats it in the 5/21/20 fixed-point behavior matrix.
The normal publisher now runs CupidObj first against private ordinal file
snapshots and compares the result with an independent Python render. Python
retains native-path safety, drift checks, the per-output lock, and atomic
replacement. ADR 0239 records the source boundary, ADR 0240 records seed
carriage, and ADR 0241 records production ownership.

A block type name or record member may either reuse a visible enum tag or define a new one. New enumerators keep their exact lexical activation point through ADR 0062 ownership records.

## Records

- `LOG.md` is the chronological bootstrapping log. Add an entry for every completed implementation step, failed approach, user answer, important decision, and test run.
- `HOST-DEPENDENCIES.md` records every external build dependency and whether it belongs in the final normal build.
- `CAPABILITY-MATRIX.md` records implemented and missing CupidC, CupidASM, CupidDis, object, linker, and bootstrap capabilities.
- `MIGRATION-MATRIX.md` records which tool owns each source and artifact cohort today and at the self-hosting fixed point.
- `BASELINE.md` documents the reproducible oracle-build interface and evidence format.
- `c-source-suffix-ownership.json` records the reviewed residual C, runtime source-delivery, and unreachable Cupid C paths that cannot derive ownership from a direct checked compiler edge.
- `ACTIVE-SOURCE-AUDIT.md` is the generated human summary of the root OS image, separate user-program, and hosted toolchain build roots, including ownership, source features, ABI requirements, unreachable files, and source-driven priorities.
- `audits/active-build.json` is the deterministic machine-readable companion. Regenerate it with `make bootstrap-audit`; `make test` and `make check-bootstrap-audit` reject drift or a failing audit contract.
- `../adr/` records stable architectural decisions; `../../CONTEXT.md` defines project vocabulary.

## Update contract

Every toolchain implementation commit must update the affected records here and include relevant positive and negative tests. Claims in these files must distinguish source inspection from executed verification. The `TempleOS/` reference tree is excluded from all progress metrics. Generated objects, images, and logs are excluded from source-migration counts and ordinary commits unless they are intentional bootstrap inputs such as checked seeds; their hashes, ownership, layout, and runtime behavior remain required acceptance evidence.

Progress means transferring ownership without reducing Cupid OS behavior:

1. A Cupid tool gains the real feature required by an active source cohort.
2. Tests prove successful behavior and useful failures.
3. The cohort moves from the legacy host/oracle path to the Cupid path.
4. The OS build and applicable boot smoke tests remain green.
5. Host dependencies are removed from the normal build only after the replacement path is proven.

## Integrated source-head evidence

The [source-current checkpoint](#2026-08-21-source-current-checkpoint) records
the current focused results, schema v3 publication, final post-CTXT audit,
fully poisoned OS build, and strong full private guest frontier. The audit has 739
active language inputs, 452
reachable transforms, and 255 feature requirements. It records six production
CupidDis participation points and no active CupidC-owned `.c` source.

The current checked execution seed carries wide integer conversion to
`float` and `double` and executable relocation-field ownership. ADR 0292
records the fixed-point promotion that moved both capabilities into the
normal Windows and Linux publication paths.
