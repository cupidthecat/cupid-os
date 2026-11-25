# Give CupidC the complete kernel crypto cohort

- Status: Accepted
- Date: 2026-07-24

## Context

The first production CupidC cohort covered 16 of the 20 C files in
`kernel/crypto`. Four files stayed on the host compiler:

- `asn1.c`, `x509.c`, and `x509_chain.c` needed typed null conversion and
  address decay for an external array with an unspecified bound.
- `csprng.c` needed the GNU assembly forms used for RDTSC, CPUID, RDRAND, and
  SETC.

Those were compiler gaps, not reasons to change the crypto source. ADR 0094
added the pointer and array rules, ADR 0096 added the assembly subset, and ADR
0097 promoted the resulting stage-three compiler into the checked seed.

The remaining ownership gap was in the production controls. The allowlist,
frontier, and four Make recipes still described the old boundary even though
the refreshed seed could compile every source.

## Decision

Checked-seed CupidC now compiles all 20 `kernel/crypto/*.c` files in the normal
image build. `APPROVED_CRYPTO_SOURCES` names the complete sorted cohort,
`BLOCKED_CRYPTO_SOURCES` is empty, and the frontier has no expected compiler
failure. It still rejects an unclassified crypto source, watches the complete
kernel-profile input snapshot, compiles every approved file twice, validates
both i386 ELF32 objects, and publishes the result directory only after the
whole run succeeds.

The four former host recipes use the same checked wrapper as the earlier
cohort. Each rule depends on the wrapper, frontier, seed verifier, manifest,
and all five checked tool images. A compiler failure or invalid object cannot
replace an existing production object.

The boot-time TLS self-test directly exercises the new runtime surface:

- Existing positive and negative DER checks cover `asn1.c`.
- A tracked trust anchor passes through `x509_parse`; controlled SAN, wildcard,
  CN, and encoded-name cases cover the X.509 name helpers.
- Chain tests cover initialization, empty and malformed inputs, a successful
  certificate add, the missing-time error, and embedded-root lookup.
- Normal kernel startup calls `csprng_init`, including its CPU feature and
  entropy paths.

The root-lookup check matters to the compiler boundary. It executes
`TLS_CA_BUNDLE[i]` from `x509_chain.c`, so the boot gate covers the
unspecified-bound external array that originally blocked the file.

The GUI smoke runner accepts an optional QEMU CPU model. The crypto gate uses
`--cpu max` so RDRAND is present and the assembly-backed seed path must run.

## Rejected alternatives

Leaving the four files on Clang or GCC was rejected. The checked seed already
represents their unchanged source, and a stale Make boundary would keep a host
dependency for no technical reason.

Deleting the old frontier failures without adding a useful negative was
rejected. The replacement test forces an approved-source compiler failure
after a partial file appears in staging and proves that no frontier directory
is published.

Calling the new X.509 checks full trust validation was rejected. They prove
parser, name, chain-state, and root-lookup execution. The current verifier
still accepts some unauthenticated cases, including a missing root or an
unsupported signature algorithm, and discards the top-root signature result.
Those security semantics are existing limitations, not properties proven by
this ownership cutover.

Making the self-test pass with the weak empty CA bundle was rejected for the
normal tracked image. The test now requires the checked-in strong bundle,
whose static lifetime also satisfies the parser's borrowed-DER contract.

## Consequences and evidence

The real checked-seed frontier compiles all 20 sources twice with no declared
boundary. The 20 deterministic objects total 204,132 bytes. The four newly
owned objects are:

| Source | Bytes | SHA-256 |
| --- | ---: | --- |
| `asn1.c` | 9,032 | `51c5300859a80579bf27ef2f978bccfb53fdf02947c219caf01ad9bf826d872b` |
| `csprng.c` | 6,888 | `ab25ef013e9b91a9fab08c6ba610c6476897bc45296b4111f1df035c0e45e317` |
| `x509.c` | 16,072 | `82536ae0f6c1a026757f45236fe6ce2454b147764e12105e8dc0fe199bb09801` |
| `x509_chain.c` | 7,028 | `bbcad318a413be4cfce52541a0b803368ec1515e9ff4faa119552715bf0aec98` |

A forced Make run rebuilt all 20 objects with `CC` set to a nonexistent
command. The focused wrapper and frontier suites pass 35 tests, including
unapproved-source rejection, input drift, malformed objects, compiler failure,
missing output, deterministic repetition, and commit-gated publication.

The normal two-pass CupidLD build succeeds. `_kernel_end` is `0x00B19910`,
leaving 943,856 bytes below the fixed stack at `0x00C00000`. The final
6,439,120-byte kernel ELF has SHA-256
`151d79718dcbc26f7aa21beb4769c40fa3f040ff55aacd038dc75beb389ade8f`.
The 6,261,073-byte flat kernel has SHA-256
`9a9a17d7a18a8d589b1170e9b8db9a90ec1d737b80b53bac6bf4901b7f408d81`.
The 209,715,200-byte image has SHA-256
`98d3b81a14de710c3edc7af49660ec093cc1b9acae73de30b2bd0c3c422697ef`.

QEMU with the `max` CPU reports RDRAND seeding and exactly 62 successful
self-test checks. It reaches the desktop, opens the GUI terminal, and completes
the embedded CupidC run of `/bin/ls.cc`. The serial log contains no failed
self-test, panic, corruption, exception, or illegal-instruction marker. Its
SHA-256 is
`0b1ce6b9b2fa7cc59b8c4c15397bb3ad1853a405446fa7687b1b6762119ae5b9`.

The regenerated active-build graph declares `sha512.h` as a direct input to
`x509_chain.o` and records all 20 crypto transforms under CupidC plus the
Python launcher. It contains 698 active inputs, 252 feature requirements, 501
transforms, and 39 accounted unreachable files. The active-source digest is
`8550700d750fb2f2c5afb424a70e1646c1e161616f7b2c229fed1cad2fc89233`;
the audit JSON SHA-256 is
`f332b5801def9e94501ee16dead0ded3bdb4f0644b9b407a532f72ebdf5e2de4`.

No kernel crypto C object now depends on GCC or Clang for production
compilation. Python still verifies and launches the checked seed, WSL still
provides that execution bridge on Windows, the objects are not optimized like
the host `-Os` versions, and most of the remaining kernel, driver, generated,
tool, user, Doom, and vendored C graph remains host-built.
