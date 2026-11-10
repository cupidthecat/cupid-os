# ADR 0448: Write candidates through owned wide stores

## Status

Implemented in a separate prototype on 2026-10-07. Native and matched Cupid-built
small contracts pass on both hosts. Both matched callers publish complete files
above four GiB; Linux also passes under a 32 MiB address-space limit. Independent
rereading compares every output byte. Source integration, qualification and
native disk ownership remain open. See
[the candidate-store evidence](../bootstrap/CANDIDATE-STORES.md).

## Context

The disk composer and FAT16 writer use sector callbacks and full-width source
views. The transaction retains inputs and candidates, but it has no native
candidate writer with explicit wide offsets or an explicit flush boundary.
Opening its exposed candidate pathname through stdio would give the recipe a
second lifetime and leave its writes outside the retained handle's checks.

## Decision

Add a fixed-extent candidate store owned by the existing transaction. Open it
once with an extent from zero through the accepted transaction capacity.
Windows retains a writable handle that excludes other data writers. POSIX
retains its exclusive-created read/write descriptor, with large-file opening
on i386. No independent file extent or identity record is introduced.

Read and write exactly zero through 65,536 bytes at an unsigned 64-bit offset.
Reject out-of-range requests before I/O. Zero-byte requests permit a null buffer
through EOF. Reads stage bytes until the retained identity, extent, precise
timestamps and named binding have passed the before/after checks. Calls are
serialized. Any failed store operation prevents subsequent publication.

Finish flushes file data, captures the complete digest and ends writable API
access. Retained reads remain available for independent validation before
publication. A failed finish clears its result and prevents capture or
publication. Existing checked-writer transactions retain their older protocol;
checked tools cannot launch after a native store opens.

Windows queries modification and change times directly through
`NtQueryInformationFile` and its forty-byte basic-information record. The
directory query binds the name and file ID. Traces show its timestamps lagging
the writable handle after a write. Microsoft documents write-time updates and
the handle query's basic-information class:
[WriteFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile),
[NtQueryInformationFile](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntqueryinformationfile),
and [FILE_BASIC_INFORMATION](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-_file_basic_information).
Both ordinary and UTF-8 i386 startup bridges carry the five-argument call.

POSIX publication creates a retained candidate alias, which changes ctime.
After that authorized change, refresh only ctime while requiring unchanged
identity, extent and precise modification time. A native fixture mutates the
modification nanosecond immediately after the real alias operation and checks
rejection and result clearing.

The flush promises file-data completion as reported by the host API. It makes
no claim of crash-durable directory publication. Existing guarded publication
and recovery checks still govern the final output. Independent disk validation,
optional stage/WAD captures, native recipe ownership and paired seed adoption
remain separate requirements.

## Extension: checked private output ownership, 2026-10-08

A checked template producer must finish before a native store opens. Transfer
its accepted private output into the same transaction's frozen-input table,
preserving the original file identity and complete snapshot without copying
the payload. Dispose only a verified unused empty checked-tool candidate so
the native store can create its writable candidate. Captured or nonempty
candidates and any started store or publication reject the transition.

Each new producer attempt invalidates earlier output eligibility before request
validation. Sealing compares against the accepted record and cannot replace its
digest with a new baseline. Later producer launches and private-output writes
fail after transfer; registered reads and owned candidate work remain available.
The existing private-output capacity stays unchanged. See
[the implementation and retained failures](../bootstrap/PRIVATE-OUTPUT-INPUTS.md).
The separate [checked template module](../bootstrap/CHECKED-DISK-TEMPLATES.md)
now supplies that producer boundary and registered-input composition. The
[combined publisher](../bootstrap/CHECKED-DISK-PUBLISH.md) passes its four-caller
small-image selection and complete current-adapter regressions for its original
body. Both matched normal-image callers retain their 600-second timeouts. Phase
measurements identify three redundant preliminary final checks; the module now
delegates them to the unchanged guarded publication owner. That owner still
checks every input, candidate and public binding. Native late-mutation controls
pass before and after the delegation. All 132 current small-image and 24
publication-boundary executions pass complete independent checking. Separate
instrumented full-image runs finish under the original bounds and measure about
11 to 12 GB of digest input per 200 MiB operation. Fresh ordinary full-image
replays also pass on both hosts under 600 seconds, with 32 MiB on Linux.
Complete independent image/FAT checks and all four strict boots pass. Optional
discovery, large-geometry template production and normal recipe ownership
remain open. The
combined publisher record retains both failed cohorts, complete source custody
and tagged diagnostics.
