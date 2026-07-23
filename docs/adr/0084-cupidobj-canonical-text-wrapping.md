# Canonicalize embedded text in CupidObj

- Status: Accepted
- Date: 2026-07-22

## Context

Cupid OS embeds source files, manuals, and other text in the kernel image. The
Makefile previously sent those files through CupidObj's byte-preserving `wrap`
operation, so their object payloads depended on the checkout's line endings.
The repository declares LF in `.gitattributes`, but a long-lived Windows
checkout still held CRLF copies of 172 embedded text files.

Those extra carriage returns added 49,907 bytes to the linked image. In that
checkout, the projected `_kernel_end` reached `0x00B00910`, which crossed the
fixed stack boundary at `0x00B00000` by 2,320 bytes. The same source set with LF
payloads ended at `0x00AF3910`, leaving 50,928 bytes below the stack. Deleting
manuals or moving an ABI boundary would hide the artifact drift without fixing
it.

## Decision

CupidObj now has separate text and binary wrapping operations. `wrap-text`
converts each CRLF pair to LF before it emits an ELF32 relocatable object. A
lone CR remains unchanged. The existing `wrap` operation preserves every input
byte and remains the required path for images, fonts, and other binary assets.

Text normalization uses the job's scratch arena. The operation marks and
rewinds that arena on success or failure, and object publication remains
transactional. An allocation failure cannot replace an existing output. A
corrected request can run in the same job afterward.

The root Makefile selects `wrap-text` for the enumerated source, header, manual,
demo assembly, and God-mode text cohorts. Those pattern rules also depend on
the Makefile, so a wrapping-policy change rebuilds objects that already exist.
The graph audit records text and binary wrapping as distinct operations and
rejects an active Cupid C source or header delivered through the binary path.

## Rejected alternatives

Deleting or shortening embedded text was rejected because documentation is an
OS feature, not disposable link padding.

Moving the fixed stack or external program arena was rejected. It would change
the established runtime address contract, risk old linked programs, and leave
checkout-dependent object bytes in place.

Normalizing only the current worktree was rejected because another stale or
misconfigured checkout could reproduce the failure. The build must define the
bytes it emits.

Changing the existing binary wrapper was rejected because line-ending
conversion can corrupt arbitrary asset data. The operation name must make the
policy explicit.

## Consequences and evidence

LF and CRLF versions of the same text, given the same explicit identity, now
produce byte-identical ELF32 objects. Contracts also prove the lone-CR rule,
the exact end and size symbols, repeat determinism, scratch rewind, constrained
output failure, sentinel preservation, and same-job recovery. A neighboring
binary-wrap case proves that CRLF bytes remain untouched.

The normal graph contains 172 canonical text wraps and eight byte-exact binary
wraps. CupidObj also owns the Python-assisted JPEG transform and the flat kernel
image transform, for 182 normal-build outputs in total.

The production link now succeeds without moving any address boundary.
`_loaded_end` is `0x006D2D8F`, `_bss_start` is `0x006D3000`, and `_kernel_end`
is `0x00AF3910`. The kernel keeps 50,928 bytes below the fixed stack. CupidObj
itself still passes the hosted self-build gate as a deterministic i386 ELF32
object with 14 functions, 16,872 text bytes, 20,180 object bytes, and fingerprint
`7238E153`.
