# ADR 0270: Expose Cupid language mode in the hosted driver

## Status

Accepted on 2026-08-13.

## Context

The shared preprocessor and frontend already implement
`CTOOL_C_PP_MODE_CUPID`. Contract tests use that mode for Cupid types such as
`U32`, `float4`, and `double2`. The public CupidC command always requested C11,
so callers could not select the implemented language from the command line.
That left active Cupid declarations behind a private test interface.

GNU extensions are independent of the language vocabulary. Doom compatibility
changes C parsing rules and header assumptions, so combining it with Cupid mode
would create an unclear profile.

## Decision

Add `--cupid` to the public CupidC driver. The option selects Cupid mode for
both preprocessing and parsing. `--gnu` can appear before or after it and still
controls only GNU extensions. The driver rejects `--cupid` with
`--doom-compat`, regardless of order, before opening the destination.

Keep C11 as the default. Existing kernel, Doom, hosted-tool, and external-user
recipes therefore retain their current language profile.

## Evidence

The hosted and Cupid-built drivers compile the same `U32` function to identical
i386 ELF32 objects. They also compile the unchanged
`kernel/cpu/simd_intrin.h` declaration surface in Cupid mode. Both orders of
`--cupid` and `--gnu` produce identical output. Both conflicting option orders
return status 2, print the same diagnostic and usage text, and preserve an
existing destination.

The checked Linux seed compiled the changed `cupidc_main.cc` twice to the same
23,392-byte relocatable object. Its SHA-256 is
`a71ec0ec1d19f852a6c6216399068ea10f53a29d48ca415091fa946c832e6312`.
The complete checked-seed bootstrap passed in 1,077.5 seconds. Stage two and
stage three match across all five Linux tools, every native Windows tool, and
the 5/18/16 behavior matrix. The new Linux CupidC image is 2,666,240 bytes
with SHA-256
`d2364255805ed1809d4a67d2770ff30015e29ca17dc95ecdf5604db24a0f0474`.
The native Windows image is 2,594,304 bytes with SHA-256
`4d8aec51cb0776d3583b2bb88ad140f48465b1912b9bdb402768a64e9ced18eb`.
The 50-input source snapshot has SHA-256
`8ec98b0a5e8ce4ac99014269330bb5f23e6c43a91cbd28a6da748aff2db8ec8a`.
The 38,163-byte report has SHA-256
`f36772444a7852a1c047ae45f3233967a00b29b117340583155614e497e31bc2`.

## Consequences

Hosted CupidC now has a public route for the language already used by the
in-kernel compiler and active SIMD declarations. Later production migrations
can select that vocabulary directly instead of adding a private driver.

The option does not claim that every private CupidC feature is available in the
hosted compiler. C11 remains the default, and Doom compatibility remains a
separate profile.

No `.c` source entered the active build, so no source rename was due.
