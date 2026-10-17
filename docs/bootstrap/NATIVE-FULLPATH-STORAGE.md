# Windows full-path output storage

## Complete-owner capacity follow-up, 2026-10-11

The [fresh Windows replay](NATIVE-OWNER-CAPACITY-FOLLOWUP.md) passes all four
flat calls and both distinct-parent drive calls. Both distinct-parent UNC
callers still time out at sixty seconds. Native diagnosis attributes
the metadata queries. The tested replacement APIs either reject UNC queries
or take longer. Complete
input ownership and publication remain unqualified.

## Windows full-path storage manual acceptance, 2026-10-11

The [OS manual acceptance](FULLPATH-STORAGE-MANUAL.md) passes both complete
host builds and normal-root verification, including six strict four-CPU boots.
The manual has 84,889 bytes and the raw kernel 9,314,136 bytes. The
allocation component retains its accepted tests and source bytes. Its private
capacity measurements do not qualify complete native input ownership or
publication.

The checked Windows UTF-8 adapter now allocates its full-path output buffer
without clearing it first. The buffer still holds 32,768 UTF-16 units. The
adapter calls the same wide API and converts only a successful result whose
returned length fits the existing bound. Other allocations retain their rules.

`GetFullPathNameW` writes the successful result before the adapter reads it.
Failure and oversized-return paths do not consume the buffer. This removes
one 65,536-byte clear per checked full-path conversion without caching a path
or changing its authority. Root and parent bindings, leaf metadata, payload
hashing and provisional callbacks keep their contracts.

## Allocation and conversion contracts

The focused contract poisons fresh `malloc` storage with nonzero bytes before
the mocked API writes its result. It verifies the exact UTF-8 output, file-part
offset, output guard bytes and release of both allocations. Both allocation
failures reject before the API runs. API failure, oversized output, malformed
UTF-16 and a small UTF-8 buffer preserve their errors and cleanup.

Cupid-built Windows and Linux focused callers pass, with five strict
disassembly checks on each final host cohort. A Windows rerun with separate
object names matches every object and the image from its preceding run. The
existing broader adapter matrix passes seven Windows methods and five
applicable Linux methods, with two expected Windows-only skips. It covers
long paths, scalar-codec boundaries, wide startup, exact role imports,
conflicting roles and preserved compiler output on rejection.

The canonical source tree also passes both adapter matrices after application.
The Windows run takes 48.849 seconds; Linux takes 81.602 seconds while the
normal build is running. The concurrent build prevents treating those
durations as performance comparisons.

## Diagnosis and bounded replay

Earlier complete-owner replays preserve UNC timeouts. An unchanged replay
after the normal build finishes records three passes and one checked UNC
timeout. A subsequent accounting replay passes all four calls. That variation
keeps complete-owner qualification open.

The accounting replay records more checked-caller user CPU than the native
caller. WPR cannot start because the shell lacks system-profiling permission;
no recording remains active. Child-only debugger sampling uses a function
map reconstructed from the sixteen frozen link objects. Every non-relocation
code byte matches the frozen executable.

The first usable probe records 2,190 instruction-pointer samples, of which
606 fall inside mapped functions and 452 fall inside `memset`. A second probe
records 2,195 samples, with 616 mapped samples and 456 inside `memset`.
Its frame and argument capture identifies 446 samples under
`calloc -> cupid_windows_get_full_path_name`, clearing 65,536 bytes each.
These are periodic instruction-pointer samples, including waiting; they do
not give CPU percentages. Every successful suspension is resumed.

The allocation candidate rebuilds one checked adapter object and keeps the
other fifteen link inputs byte-identical. Four serialized calls use the same
4,096-stage fixture, 49,152 aggregate payload bytes and original 60-second
limit. Both views and per-file payload validation remain required.

| Checked caller | Elapsed seconds | Child user CPU seconds |
| --- | ---: | ---: |
| Preceding drive | 28.804 | 7.828 |
| Candidate drive | 7.146 | 2.531 |
| Preceding UNC | 59.829 | 7.906 |
| Candidate UNC | 56.130 | 2.781 |

All four calls pass. The lower user-CPU totals support removing the redundant
clear. One comparison per namespace and substantial UNC waiting do not
establish a speedup or complete-owner qualification. Process I/O accounting
does not provide a complete API query vector.

## Preserved harness failures

The first symbol-map guard required positive ELF function sizes. Assembly
symbols with zero declared sizes now retain that fact and use explicitly
marked extents only to the next function or section end. All non-relocation
bytes still have to match.

The first child sampler omitted the thread-context access right and captures
no usable addresses. Its single successful suspension is resumed. The
corrected sampler opens only its launched child's thread with the required
rights and captures usable samples.

The wider allocation test initially renames `malloc` in a runtime shared by
contracts that need its ordinary symbol. Their links reject. The corrected
harness confines allocator injection to the contract that defines its
wrappers and builds a separate runtime for the other contracts.

The first Linux suite completes its methods but fails to serialize skipped
test objects. The corrected fresh replay writes test names and reasons, then
passes. A source-control guard also rejects an old frozen input identity
before any normal-build command. The corrected guard first verifies each
applied file's old identity and then requires its archived new identity.
Every other input retains its original control.

## Normal build and ownership

Normal-root verification passes in 2008.825 seconds. All 429
objects, four linked products, six user products and the complete
209,715,200-byte image match the preceding acceptance. The user ABI passes
all 103 fields and 101 providers. Sixteen exact artifact gates and both
strict four-CPU boot smokes pass. The source snapshot changes only the
Windows adapter among its 99 inputs; the other 98 inputs and all twenty
projected root controls retain their accepted bytes.

The normal build retains 449 CupidBuild actions and three Python coordinators.
Installed execution seeds are unchanged. This source change does not qualify
the separate complete input owner or transfer image publication. The OS
manual will receive a separate measured acceptance step.

The API's successful output and returned-length rules are documented in
[GetFullPathNameW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfullpathnamew).

[The evidence index](evidence/windows-fullpath-storage-20261011.json) binds
the source application, both canonical host contracts, complete normal-root
product comparison and seven archives. The archives preserve the diagnostic
attempts and failed harnesses alongside the passing checks. Their scope does
not qualify complete native input ownership.
