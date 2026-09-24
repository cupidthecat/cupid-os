# ADR 0231: Validate sequential JPEG input with CupidObj

## Status

Accepted on 2026-08-04.

## Context

The normal image embeds one repository JPEG as exact bytes in an ELF32 object.
Python currently validates the marker stream, copies a private snapshot, and
asks checked CupidObj to apply the ordinary binary wrapper. CupidObj therefore
owns the object format but not the JPEG acceptance decision.

That split keeps host image converters out of the build, but it leaves a
semantic transformation in orchestration code. CupidObj already has the
transactional job, diagnostic, buffer, and deterministic wrapper seams needed
to own the validation directly.

## Decision

Add a `wrap-jpeg` operation to the CupidObj library and command. It accepts one
baseline SOF0 or extended sequential SOF1 frame, requires a nonzero sample
precision, dimensions, and component table, requires a scan after the frame,
checks scan component-table length, accepts byte stuffing and restart markers
inside entropy data, and requires a terminal EOI marker with no trailing
bytes.

Progressive and other unsupported frame types receive a distinct unsupported
diagnostic. Malformed marker streams, lengths, frame or scan tables, missing
structure, partial entropy markers, and unexpected standalone markers receive
input diagnostics.

After validation, CupidObj passes the exact source bytes through its existing
binary wrapper. Section flags, alignment, identity-derived symbols,
determinism, output rollback, arena rollback, and same-job recovery therefore
retain the established object contract.

## Evidence

Library and command contracts cover valid SOF0, SOF1, and entropy data with
stuffing and a restart marker. Each result matches an ordinary binary wrapper
byte for byte. The active 800,393-byte repository JPEG also produces the same
object through both commands.

Twenty-one malformed or unsupported inputs match the existing Python
validator's exact messages. Every failure preserves an existing command
output. The library contract also checks a constrained-output failure, arena
rewind, and successful reuse of the same job after every rejection.

The fixed-point behavior harness requires both newly built CupidObj stages to
list `wrap-jpeg`, produce the ordinary wrapper's exact object for a valid
sequential fixture, reject its progressive variant with the expected
diagnostic, and preserve both pre-existing outputs. This raises the next
transition's behavior matrix from five help, eleven success, and seven failure
cases to five, twelve, and eight. The preceding checked-seed report retains
its historical five, eleven, and seven counts until promotion.

## Rejected alternatives

Calling a host JPEG converter was rejected because it reintroduces
platform-dependent bytes and an external build dependency. Decoding and
re-encoding the image in CupidObj was rejected because the build needs to
validate and preserve the checked-in asset, not normalize it.

Replacing the production recipe in the same source commit was rejected. The
checked CupidObj seed does not yet recognize `wrap-jpeg`, and the current
Python publisher also freezes inputs, detects live drift and path hazards,
uses a sibling temporary file, and replaces the destination atomically.

## Consequences

Source-head CupidObj now owns the JPEG validation semantics and deterministic
object bytes. The normal build still uses the preceding checked seed and the
Python validation path until a full five-tool seed promotion completes. A
later ownership transfer can call checked `wrap-jpeg` while retaining the
existing snapshot, drift, path, and atomic-publication safeguards.
