# Boot-exercise the complete CupidC crypto cohort

- Status: Accepted
- Date: 2026-07-23

## Context

Checked-seed CupidC owns sixteen `kernel/crypto` objects in the normal image
build. The existing TLS self-test executed production code from twelve of
them. `bigint.c`, `rsa.c`, `sha512.c`, and `ed25519.c` compiled, linked, and
passed deterministic object checks, but the boot gate did not call them.

Object evidence catches compiler and linker failures. It cannot show that the
linked code computes the right result in Cupid OS. Leaving four objects
unexecuted also made the cohort's runtime claim weaker than its ownership
claim.

## Decision

The existing boot-time TLS self-test now calls all sixteen migrated crypto
sources. It adds eight checks:

- SHA-512 and SHA-384 for an empty input.
- Big-integer big-endian conversion and `4^13 mod 497 = 445`.
- RSA PKCS#1 v1.5 SHA-256 verification for one valid signature and a
  copy with one bit flipped.
- Ed25519 verification for RFC 8032's empty-message vector and a corrupted
  signature.

The RSA and Ed25519 pairs cover acceptance and rejection. A positive-only
signature check could miss a verifier that accepts every input.
The checks run against the objects already linked into the kernel; no duplicate
test implementation is substituted.

The TLS self-test target declares every newly included crypto header as a Make
prerequisite. A header change therefore rebuilds the caller that consumes its
interface.

## Rejected alternatives

Keeping compile and link evidence alone was rejected because it does not test
the emitted code in its production runtime.

A host-only unit runner was rejected as the ownership gate. Host tests are
useful, but they do not exercise the CupidC-produced i386 objects inside the
kernel.

Checking only valid signatures was rejected because a verifier must also
refuse altered input.

## Consequences and evidence

The boot self-test grows from forty to forty-eight checks. A bounded QEMU run
reports every check, prints
`[tls-selftest] all primitives + ASN.1 vectors passed`, reaches the desktop,
opens the GUI terminal, and completes its command. The serial log contains no
failed self-test, panic, or exception.

An independent host oracle raises the RSA signature to the public exponent and
recovers the exact PKCS#1 v1.5 SHA-256 encoding. Python's Ed25519 verifier
accepts the fixed empty-message signature and rejects the same one-bit change
used by the boot test. These are supporting checks, not build dependencies.

The current kernel ELF is 6,414,372 bytes with SHA-256
`2bb6adcfa61fc2ec166d2dd380b80e9d4e7ff9cbaf8e542684438e4d45f602da`.
The flattened kernel is 6,235,333 bytes with SHA-256
`1f7c42a264a5be699981b42a09469c54c05e1c90847b80162908aa4d4b8f3f0c`.
The 209,715,200-byte image has SHA-256
`ff0fa1356c4f27a5438d1629cf501a525c2832954aef60933c777a854367a0ea`.
The serial log has SHA-256
`6a32754caa6039da7fb079302c55bf4fa114e3306f65525e935552af597ce08c`.

This decision closes the runtime-vector gap for the current sixteen-source
cohort. It does not add a production source, refresh the checked seed, remove
Python or WSL orchestration, or transfer any of the four neighboring crypto
objects.
