# ADR 0334: Expose typed raw CupidDis kernel requests

## Status

Accepted on 2026-08-24.

## Context

The shared CupidDis core already accepts fixed 16-bit or 32-bit raw input and
ordered maps whose ranges are code16, code32, or data. It also reports known,
unknown, invalid, and truncated instruction counts. The hosted driver exposes
these choices, but the public kernel adapter always selected permissive
32-bit decoding. An in-OS caller could not describe a boot-style mixed image
or reject code that fell back to `db` rows.

Adding a second map parser to the kernel adapter would split validation from
the shared contract. Making the old entry point strict would also change the
current CupidC JIT listing, which deliberately renders fallback bytes.

## Decision

Add `dis_raw_request_t` and `dis_disassemble_raw` to the public kernel
adapter. A request supplies the base address, a fixed x86 mode or
`CTOOL_DIS_RAW_RANGE_MAP`, an optional borrowed array of shared
`ctool_dis_raw_range_t` records, and a `require_known` flag.

The adapter passes the mode and range storage directly to
`ctool_dis_inspect`. CupidDis remains responsible for range kinds, ordering,
bounds, decoding, label placement, and data rendering. When `require_known`
is set, the adapter reads the report's typed decode summary and rejects any
unknown, invalid, or truncated code before rendering a listing. It returns a
VFS-style status and writes a short diagnostic through the caller's existing
output callback.

Keep `dis_disassemble` as the compatibility entry point. It constructs one
permissive fixed-32 request and ignores the returned status, preserving the
existing JIT and DEBUG callers.

## Evidence

The native public-adapter contract enters only through `dis.h`. It checks the
legacy 32-bit listing, strict fixed-16 decoding, one code16/data/code32 map
with a label inside the data interval, strict rejection of unknown, invalid,
and truncated instructions, permissive fallback through the legacy call, a
nonzero first map offset, and a valid request after that failure.

The DEBUG kernel self-test keeps its original fixed-32 and VFS ELF checks. It
also sends the mixed map through `dis_disassemble_raw` and confirms that the
data byte is rendered as `db`, not `nop`. A second call requires known code
and confirms that a truncated opcode returns `VFS_EINVAL` without a listing.

Checked CupidC compiles `kernel/lang/dis.cc`,
`kernel/lang/ctool_kernel.cc`, and the existing `kernel/lang/cupidc.cc`
caller with the public request in their recursive header closures. Exact
commands are recorded in the bootstrap log. The full source build linked both
kernel ELFs before its checked CupidDis process reached the existing
300-second budget. The branch therefore claims no guest runtime proof; that
check remains with the consolidated integration build.

## Rejected alternatives

Parsing range-map text in the kernel adapter was rejected. In-process callers
already have typed ranges, and the shared inspector owns their validation.

Inferring code, data, or x86 mode from the bytes was rejected. A flat image
does not carry enough information to make those choices reliably.

Making `dis_disassemble` strict was rejected because existing callers depend
on deterministic fallback rows for incomplete instruction coverage.

## Consequences

In-OS code can use the same fixed-mode, typed-map, and strict-known boundary as
the hosted driver without copying or splitting its input. The range array and
label storage remain borrowed for the duration of the synchronous call.

This step does not add shell syntax for raw files or expose local-target and
ELF code-anchor policies through the raw adapter. The `dis` and `exec -d` ELF
commands are unchanged. It transfers no build owner, removes no host
dependency, changes no object format, and qualifies no source for a suffix
rename. `TempleOS/` remains read-only reference material.
