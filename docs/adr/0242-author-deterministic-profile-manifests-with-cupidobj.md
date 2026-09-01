# ADR 0242: Author deterministic profile manifests with CupidObj

## Status

Accepted on 2026-08-08.

## Context

The normal Doom build records the exact header and source membership of its
`doom-compat` and `doom-tree` profiles. Python still authors that JSON file,
which makes it the last root output without a Cupid tool author.

The active inventory does not fit comfortably on a portable Windows command
line. It contains 665 profile membership references and 291 captured header
inputs, for 956 encoded path records. CupidObj also needs the captured bytes,
not hashes supplied by Python, if it is to own the manifest's SHA-256 fields.

This step adds the deterministic format operation without changing the normal
publisher. The checked seed predates the command, so seed carriage and the
guarded production handoff remain separate work.

## Decision

CupidObj now accepts `profile-manifest SNAPSHOT -o OUTPUT`. The input is one
bounded binary envelope with the eight-byte magic `CUPROF1\0`. Every count and
length is an unsigned little-endian 32-bit value. A byte string is encoded as
its length followed by its exact bytes. The fields appear in this order:

1. schema byte string and profile count;
2. for each profile, its name, header count and paths, then source count and
   paths;
3. captured-input count; and
4. for each captured input, its path and raw bytes.

The public contract allows at most 127 schema bytes, 63 profile-name bytes,
1,024 path bytes, 16 profiles, 512 entries in any one membership list, 512
captured inputs, and 2,048 combined header and source references. These limits
are checked before allocation or list traversal. Folded and exact-order heap
sorting plus binary lookup keep validation bounded without a quadratic worst
case at the public maximum.

Names and paths use portable ASCII spelling. Paths must be nonempty and
repository-relative, with forward slashes and no empty, `.`, or `..`
components. Duplicate names, duplicate rows, and case-only aliases fail with
specific diagnostics. Each header path must have one exact captured input,
and every captured input must belong to at least one profile. Source paths are
membership records; their bytes are not captured.

CupidObj sorts profile names and paths by unsigned ASCII order, computes
SHA-256 over each captured input, and emits the same indented, key-sorted JSON
as the existing Python oracle. Equal typed snapshots therefore produce equal
bytes regardless of input order. Failure clears the result, rewinds temporary
arena storage, and preserves the caller's output.

The operation stays format-generic. Python still owns active Doom profile
discovery, suffix policy, link and junction rejection, file freezing, live
membership checks, parity, locking, and atomic publication. Those host rules
do not belong in a freestanding object transform.

## Evidence

The live profiles contain these rows:

| Profile | Headers | Sources |
| --- | ---: | ---: |
| `doom-compat` | 291 | 3 |
| `doom-tree` | 291 | 80 |

The 291 distinct headers contain 767,913 bytes. Their canonical 796,337-byte
snapshot has SHA-256
`2c22f2dd26a9fdcc41d5972b91c863d103c564c04f74860a0fc500d1fe684941`.
CupidObj's 69,366-byte JSON matches the Python oracle exactly and has SHA-256
`47ba35158cac0a7df253a0056235223e62fee24df74701800f88763e588611c2`.

The hosted CupidObj suite passes all 36 tests. It covers exact output,
input-order independence, the live Doom inventory, SHA-256 lengths 0, 3, 55,
56, 63, 64, 65, and 129, useful malformed-input diagnostics, output
preservation, and the complete 2,048-reference, 512-input, 1,024-byte-path
boundary. The strict native contract passes `profile-manifest`, including
invalid source views, output and arena exhaustion, rollback, and same-job
recovery. All 95 hosted CupidC frontend tests pass.

Checked-seed CupidC compiles the three changed C roots:

| Object | Bytes | SHA-256 |
| --- | ---: | --- |
| `cupidobj.o` | 220,508 | `b878a621e47ccd3da2656432569cbe16e9b0dac2cbeb0359d02aebb7e6603062` |
| `cupidobj-main.o` | 38,120 | `4e26e024be9aafe92763714502cf8aaf5beac21404550e7cc8c31e3dd7ed9133` |
| `cupidobj-contract.o` | 145,256 | `b29a64873e44b719435a7e5946f0d7bb0d3291a642b6e86e1fba821424e80ec4` |

CupidC's exact source frontier records 140 function definitions, 3,451
statements, 23,768 expressions, 532 block bindings, and 452 initializers for
`cupidobj.cc`. Its exact object frontier is 220,508 bytes with 183,181 text
bytes and fingerprint `90f1448f`.

The poisoned-host bootstrap completed in 904.2 seconds. All 19 C object pairs,
startup, and five tool images match between stage two and stage three. The
source snapshot has SHA-256
`bbbeb2b9f1532c9e7574ec47bb05c428f308fa430cf5fafe33b6222488b1ea33`.
The 15,058-byte report has SHA-256
`6ef11227e3976131a45c270742559a05221d3e8627cd927a0201cbb9b844dc7d`
and records five help cases, fifteen successful operations, and thirteen
failures. The staged behavior covers every SHA padding boundary above,
repeated full-block hashing, truncation, unsafe paths, case collisions, and
sentinel preservation in both stages. A direct stage-three run reproduced the
1,809-byte behavior manifest with SHA-256
`fb8fd21ec547a9649514af57d7660f5ecfd30ffad650e8c6ee4ed9535effedfc`.
The stage-three CupidObj image is 392,688 bytes with SHA-256
`7137ad601a7c22178112fbf08163b36ff2064807caa99962df97d7ae7ae62f2b`.

The normal Toolchain contract cohort then completed in 3,109.2 seconds. Its
two checked stages matched 16 objects and 15 linked executables, the hosted
runtime passed, and all 20 published artifacts verified. The 18,232-byte
manifest covers 45 inputs and has SHA-256
`00cc1b7332203e8fd780a9c5ffa592bd05e41fc5d48a8ca3cba0b22e1662c3ba`.
It records the source snapshot above and the pre-promotion 5,440-byte seed
manifest at SHA-256
`5a27d7a4a65637da413756a6c154bf44ac0879c7d941881fbd3b995733a805a8`.
Running the published CupidObj contract's `profile-manifest` selector also
passes.

The graph audit now fails closed if the positive stage pair, any of the three
profile failures, either diagnostic check, either sentinel check, or the
5/15/13 count changes. All 68 graph-audit tests pass in 782.460 seconds.

## Rejected alternatives

Passing 291 precomputed hashes was rejected because Python would still author
the manifest's content. CupidObj must hash the frozen bytes itself.

Passing every membership row on the command line was rejected because the
active inventory exceeds a dependable Windows argument budget. One bounded
snapshot also gives the transform a single deterministic input.

Walking the host filesystem inside CupidObj was rejected. Native path safety,
freezing, drift detection, and publication are host responsibilities; adding
them would blur the freestanding format boundary.

Changing the normal Make target in this step was rejected because the current
checked CupidObj image does not yet expose `profile-manifest`. Production
ownership moves only after a five-tool seed promotion and a guarded handoff.

## Consequences

Source-head CupidObj can author the active profile format, and the complete
five-tool fixed point carries the implementation. The promoted seed and normal
build are unchanged for now. CupidObj still participates in 188 transforms,
Cupid tools own 437 of 438 root outputs, and the Doom profile manifest remains
the only Python-only root output.

No C or assembly source changes ownership, so no `.c` to `.cc` rename is due.
`TempleOS/` remains untouched reference material.
