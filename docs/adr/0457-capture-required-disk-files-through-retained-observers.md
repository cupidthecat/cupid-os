# ADR 0457: Capture required disk files through retained observers

## Status

Implemented and tested privately on 2026-10-09. Small-image publication and
complete legacy regressions pass independent review. Checked 200 MiB reuse,
complete command ownership, source integration and qualification remain open.

## Context

Native discovery can retain external bootloader and kernel files. The template
and publication owners previously selected those required files beneath the
transaction root. The complete image command needs to keep the selected roots,
file identities and paired seed authority through template production and
guarded publication.

## Decision

Add a publication transaction constructor with an explicit wide capacity and
no implicit source capture. It retains the existing output parent, lock,
previous-output observation and private storage. Every input is then registered
explicitly. Output names remain safe relative paths beneath the retained root;
a prepared nested parent can supply the existing retained directory chain.

Add an observed template request with separate selected observers for the
bootloader and kernel. Validate and capture the complete paired seed cohort,
bind the primary observer once and borrow each distinct external observer once.
Compare already retained required-file entries through the bound transaction.
Physical hardlinks can share one frozen capture. Own copied path strings before
input-table growth can invalidate borrowed storage.

Keep the five-sector boot view while retaining the complete boot file. Bound
the kernel by the area before the FAT partition. Run the existing checked
CupidObj template producer with its original sixty-second limit and transfer
the resulting private template into the transaction.

Let the publisher continue from that captured template without another seed
binding or producer run. Require the exact primary observer and the registered
boot, kernel and template extents. Optional external sources must already have
borrowed authority. Preserve required-file projection, sequential FAT staging,
complete verification, prior-output checks and guarded publication. The
existing template and publisher entry points keep their contracts.

Comparisons and primary requirements grant no new source authority. Invalid,
unseen, unbound, changed or late requests clear their result and prohibit further
publication. Caller-owned observers remain alive through transaction close.

## Evidence and limits

Four native/Cupid builds pass on Windows and Linux. Independent rereading checks
209 source/support controls, 109 private producer inputs, all five programs per
build, all original command bounds and 23 complete checked object pairs.
All 664 method selections close: 646 execute, eighteen retain platform skips,
and 652 actual invocations pass. Positive images are reconstructed independently;
FAT mirrors, memberships, chains and padding are checked. Negative cases retain
their prior bytes, timestamps and namespace cleanup.

The small scope includes all eighty handoff methods, sixteen existing template
methods, forty-four external publication methods, fourteen ordinary observer
methods and twenty-four Windows UNC methods. It includes caller storage release,
input-table growth, required-file aliases, prepared parents, missing optional
sources, late edits with restored timestamps and useful authority rejections.

All four native large cases pass. Both checked fresh 200 MiB cases pass, while
both checked reuse cases time out at 600 seconds. Separate phase traces also
time out during cleanup after guarded publication returns. These results do
not establish successful close, recovery cleanup or large-image acceptance.
Linux keeps its original 32 MiB address-space limit. No deadline is increased.

[ADR 0459](0459-coalesce-required-frozen-input-revalidation.md) records a
separate follow-up that removes duplicate complete frozen-set walks within one
validation call. Its clean caller and complete large-image acceptance remain
open; the original checked reuse failures remain failures.

The first fixture assumes LF-only readiness output. A separate correction
accepts the existing Windows CRLF spelling. The next checked build exposes the
missing hosted `getchar` declaration; ADR 0458 records its separate library
capability. Disk-full and WSL I/O failures remain retained. Closed generated
images are compressed without deletion, with complete hash, length and
timestamp checks, before fresh failed-case retries. Passing earlier selections
are preserved.

[The implementation record](../bootstrap/REQUIRED-DISK-HANDOFF.md) binds the
complete scope and checker repairs. The 99 normal producer inputs and fifteen
installed seed files remain unchanged. Original parent-binding lifetime,
external seed/output authority, distinct-input capacity, complete CLI behavior
and accepted large geometries still need integration. Three normal coordinators
remain Python-owned. TempleOS stays read-only and excluded.
