# Hosted heap storage and search

The [allocator implementation](../adr/0461-manage-hosted-heap-regions-and-free-blocks.md)
now lives in the shared hosted runtime source. It shares Windows memory regions
and searches free blocks on both hosts while preserving malloc, calloc, realloc
and free behavior. The [original prototype patch](prototypes/hosted-heap.patch)
remains a historical reference. It must not be applied over the integrated source.

## Source integration, 2026-10-10

The runtime uses one shared implementation of size checking and free-bin
maintenance. Compile-time layout checks pin the 32-byte block header and the
Windows 16-byte region header. The platform backends retain their respective
VirtualAlloc/VirtualFree and brk ownership and release rules.

Run `python -m unittest -v tests.test_hosted_heap` from a clean checkout on
Windows or Linux. The test builds the actual runtime twice with the verified
platform seed and requires identical complete object bytes. CupidASM supplies
startup, CupidLD links each caller, and CupidDis checks every object and program
with all three strict code checks. No conventional compiler builds the fixture.
An optional `CUPID_HOSTED_HEAP_PRODUCTS` directory inside the checkout retains
inputs, commands, complete outputs and executable identities.

The density fixture covers 65,536 one-byte and cache-line allocations, page
allocations, alternating frees, zeroed reuse and intact live neighbors. A
deterministic 20,000-operation mixed-size replay verifies all live payloads
during shrink, growth, moves and release. Separate fault callers redirect only
the existing OS allocation/release boundary. They force allocation failure,
confirm failed realloc leaves data intact, retry successfully, and force a
release failure whose storage is then reused without another OS allocation.
Overflow must fail before requesting OS storage. Both complete pre-existing
platform runtime contracts link against the same ordinary runtime object.
Windows runs the contracts through both its original narrow startup and the
normal UTF-8/long-path profile. A separate complete runtime call writes and reads
a Unicode filename beyond 260 characters. Retained product directories must be
empty; a negative harness check preserves an existing input receipt.
Linux allocator calls retain a sixty-second deadline and a 32 MiB address-space
limit. Windows uses the same deadline.

The initial unchanged Windows runtime fails both 65,536 controls at 32,547 live
requests with ENOMEM. Its page-sized and existing boundary controls pass.
The first Linux harness run omitted the legacy fixture's GNU mode. A later
run correctly executed that fixture but compared only its final stdout line.
Both harness errors remain recorded; the settled test checks its complete
four-line output. A later rerun selected an occupied product directory and
replaced its old input list before seed capture rejected the run. Those older
input receipts are no longer usable. The early empty-directory check now rejects
that case before any receipt write.

The settled suite passes twenty tests on Windows in 70.978 seconds. Linux passes
ten tests in 42.755 seconds and declares ten Windows-profile skips. Each build
and runtime invocation rechecks the captured source and seed bytes. Retained
current receipts are under `build/heap-acceptance/windows-matrix-accepted` and
`build/heap-acceptance/linux-matrix-accepted`.
These prove the integrated source through the installed compiler, rather than
qualification of a new six-tool producer cohort.

The [committed source evidence](evidence/hosted-heap-20261010.json) records all
twenty-eight allocator/runtime outcomes across three profiles, source/header
inventories, program and seed identities, both original density failures and
36 strict certifications. Both retained-receipt guards pass.
Independent rereading verifies every recorded current source, runtime duplicate,
command result and executable identity before writing that summary. The Windows
37,512-byte runtime object has SHA-256
`ff9ababfbe09e87462f2740085db46a1a2f4ad7a7d6b3f7eeda087a21ec0e2b1`.
The Linux 32,664-byte object has SHA-256
`51d3ff76543209dc1101686995f2155e73d5b9a3de69e75e242ccc127abe616d`.
The normal Windows UTF-8 runtime is 38,496 bytes with SHA-256
`88e549ecf681cd117708e7e61eb36df584c2bb4d734c33c6822fd225e2579f37`.

Both hosts finish source preparations with the existing parent pair and all nine
conventional producer variables forbidden. Independent rereading checks the same
99-input snapshot, `4ae98403b0896b24e50b81b11b213b8117207421b9152e95d0500e00e612d07e`,
and all 97 complete stage-three/stage-four object and tool pairs. Linux startup
and runtime and all five selected Windows UTF-8 support objects match the tested
profile bytes. An initial comparison incorrectly expected the narrow Windows
runtime to match the UTF-8 object; the corrected check selects the actual profile.
Preparation receipts retain their unqualified status. Separate complete
[named-commit qualifications](QUALIFIED-HOSTED-HEAP-SEEDS.md) now pass for
`1d852023` on both hosts. Independent rereading checks 291 staged files, 97
complete fixed-point pairs, 7,761 published files and all 99 committed inputs.
The complete pair verifies in both private consumers, which start from zero
production objects. Replacement-tool consumer acceptance remains open.

The normal kernel/boot and user builds pass with conventional producers
forbidden. Only the manual wrapper differs among 429 production object
identities. Its 71,593-byte source occurs once in each kernel output. The raw
kernel grows to 9,300,840 bytes; both ELF sizes stay unchanged. The private normal
image publication preserves every FAT suffix byte and passes strict four-CPU
ls/SMP and ISO boots under the existing 150-second limits. All sixteen measured
artifact gates pass on frozen complete products and the normal root. The full
forced normal all rerun passes; its published image matches every byte of the
checked private image. [OS source evidence](evidence/hosted-heap-os-20261010.json)
records this boundary separately from producer qualification.

The qualified replacement tools now also pass complete cold normal image
builds on both hosts, fresh user builds, ABI checks, sixteen artifact
gates per host and all four strict boots. All 429 complete object pairs, six
user pairs and the complete 200 MiB image pair match the source-accepted
products. Both full compatibility replays pass all 367 selections per host, with
705 passes and 29 declared platform skips in total. Independent review rechecks
every result, all 99 raw committed inputs and the unchanged installed seeds.
The four SDK profiles, original public methods and both Make bootstraps remain
in progress. The [qualification record](QUALIFIED-HOSTED-HEAP-SEEDS.md)
also records the historical fixture correction and its thirty-two passing
original release-operation selections.

## Failure and diagnosis

The original Windows runtime uses one VirtualAlloc call per allocation. A
minimal caller linked with its unchanged runtime object exhausts address space
near 32,500 live requests. A second caller pauses for an external VirtualQueryEx
map, then checks data and frees its allocations. One-byte and 8 KiB requests
both stop at 32,548. Their committed storage differs by about threefold.
Small requests occupy 4 KiB regions whose bases are 64 KiB aligned. The first
map check's assumption of 64 KiB reservations is incorrect and remains preserved.
The [VirtualAlloc documentation](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc)
describes the allocation-granularity and page-size boundaries.

Linux's original allocator searches its full physical block list. An actual
Cupid-built work counter records these live-allocation scans:

| Live requests | Original block inspections | Free-bin inspections |
| --- | ---: | ---: |
| 128 | 8,128 | 0 |
| 256 | 32,640 | 0 |
| 512 | 130,816 | 0 |
| 1,024 | 523,776 | 0 |

The new counts describe uninterrupted allocations with no free candidates.
They do not claim that allocation performs no work or that every fragmented
free-list search is constant time. Both probes keep sixty seconds and 32 MiB.

## Implementation

Windows regions contain a sixteen-byte arena header and 32-byte block headers.
Small requests share at least 64 KiB. Larger requests obtain sufficient storage
with checked size arithmetic. Neighbor links describe only one region, so
coalescing cannot cross a VirtualAlloc boundary. A wholly free region is released;
an unsuccessful release leaves it reusable.

Both backends maintain size bins and separate free-list links. Allocation
searches available blocks, splits a useful remainder and updates neighbor links.
Freeing joins available neighbors. Linux still grows and trims brk storage,
including recovery when trimming fails. All returned payloads retain sixteen-byte
alignment. The allocator remains the existing single-threaded hosted service.

## Tests and retained controls

The same Windows fixture rejects 65,536 one-byte requests with the original
runtime and accepts them with the new one. Both hosts accept 65,536 one-byte
and 64-byte allocations, verify data/alignment, free alternating blocks, reuse
them through calloc and release all remaining allocations. Additional cases
exercise split/shrink/growth boundaries, zero sizes, null frees, arithmetic
overflow, failed huge requests and preserved data after failed realloc.

Both complete original runtime contracts pass. They retain file, argument,
directory, memory/string, formatting and represented numeric behavior. The
new runtime objects also pass strict CupidDis checks. The Windows original
contract uses exactly the same runtime object as the density fixture and full
discovery builder. Linux's focused and full-builder objects also match exactly.

Private source nine changes only the shared runtime relative to source seven.
Its 206 source/support controls and 109 private producer inputs remain held
separately. Source-nine selections pass all 64 methods on both hosts. An initial
Windows nested timeout, a passing isolated replay and complete serial retry
remain retained. None establishes a stable timing comparison.

The first lightweight recorder does not enforce host-producer sentinels.
Guarded cohort ten repeats every builder and complete runtime selection with
the checked sentinels enforced. Every legacy ordinary/UNC observer selection
also passes. Paired independent review accepts 256 selected methods, 242
executions, fourteen declared platform skips and 410 actual discovery calls.
It rereads 206 controls, 109 private producer inputs, five complete object pairs
and 128 direct identity comparisons. It also accepts all 100 current legacy
observer executions and 106 calls. Complete native and checked results agree
within each host. Every checked object except the runtime retains its
source-seven bytes, and the normal source and installed seed boundaries stay
unchanged. This accepts the private cohort; named-commit qualification and
normal integration remain required.

Earlier records locate their evidence under `C:/Users/admin/cp7` and paired
Linux trees. Those private products are absent from this linked worktree and
do not establish the current integration result:

- `runtime-allocator-debug10`, the original minimal allocation failure.
- `runtime-allocator-map-debug14`, actual region/commit maps and repeated limits.
- `runtime-allocator-work-debug16` and `runtime-allocator-work-debug18`, original
  and new Linux search counts.
- `runtime-arena-contracts-windows9-independent-products` and
  `runtime-free-bin-contracts-linux12-independent-products`, density and useful
  failure checks plus corrected strict/runtime verification.
- `runtime-original-contract-windows13-products`, the complete Windows contract.
- `input-discovery-source9`, its paired Linux copy and guarded cohort-ten outputs.
- `input-discovery-independent10/closed.json`, complete paired cohort acceptance.
- `hosted-heap-prototype19.json`, the patch and source/fixture fingerprints.
- `hosted-heap-patch-context21.json`, removal of blank context-line spaces from
  the stored patch. Every added/removed source line is unchanged, Git accepts
  the patch, and the original patch bytes remain retained.

Only debug copies contain counters or phase markers. Failed compiler probes,
incorrect checker assumptions, unsupported disassembler flags, the wrong Linux
stdout expectation and the first Windows nested timeout remain preserved.

## Remaining work

Both named-commit producer qualifications, paired cold OS builds and boots,
and complete compatibility selections pass. All four original SDK profiles
also pass complete publication and independent 77-pair audits. A separate
comparison rereads all 616 stage files and 88 published programs; every complete
stream agrees across both hosts and profiles. [The seed
record](QUALIFIED-HOSTED-HEAP-SEEDS.md) retains those consumer results. Original
public methods and both actual Make bootstraps remain required before
replacement-seed promotion. This integration changes
one of the 99 normal producer inputs. The fifteen installed seed files retain
their preceding identities until promotion. No coordinator or normal recipe
ownership changes.
The separate 200 MiB required-file reuse failure and its complete large-image
and boot checks remain open. TempleOS is excluded.
