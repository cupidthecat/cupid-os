# ADR 0266: Index x86 decode candidates

## Status

Accepted on 2026-08-12.

## Context

Strict CupidDis validation made decoder cost part of the normal build. The
production cohort contains every kernel object, the pass-one kernel, and the
final kernel. The shared x86 decoder examined all 604 catalogue forms for
every instruction, even though the first opcode byte rules out most of them.

That scan was acceptable for small interactive listings, but not for a
checked-seed production gate. A provisional Cupid-built CupidDis took about
82 seconds to validate 64 KiB of one-byte `NOP` instructions. Raw and ELF
fixtures took the same time, which isolated the cost to instruction decoding
rather than ELF parsing. A hosted build from the same source handled the
64 KiB fixture in about 0.23 seconds, but the normal build must use the
checked seed.

## Decision

Prepare an immutable, arena-owned decoder index once and reuse it across an
inspection run. The index groups catalogue form identifiers by the first
opcode byte after legacy prefixes. It retains catalogue order inside each
bucket, including the existing longest-match and canonical-over-alias rules.
Index preparation enters one-byte register-in-opcode forms in every byte
bucket selected by their mask.

Keep the exhaustive `ctool_x86_decode` operation as the compatibility and
reference path. Add a prepared decoder type and an indexed decode operation
instead of hiding mutable cache state in the shared module. CupidDis prepares
one index in its job arena and passes it through strict inspection of every
input. The kernel and other callers can continue using the existing operation
without changing their lifetime rules.

Preparation is transactional. If allocation fails, preparation rewinds the
arena to its starting position and publishes no decoder. The prepared value
contains no mutable cursor. Callers may reuse it while the preparing arena
remains alive.

## Evidence

The x86 contract compares exhaustive and indexed results for every
existing positive, unknown, invalid, truncated, and argument-error case. It
also exercises repeated use, all first-byte buckets, register-in-opcode
forms, invalid prepared-decoder arguments, and allocation failure. Status,
decoded bytes, semantic fields, consumed length, and recovery classification
remain identical.

CupidDis tests prove that one prepared decoder is reused across a
multi-input strict invocation while ordinary inspection keeps its existing
behavior. The focused x86 and CupidDis suites pass 35 tests in 5.749 seconds.
The only skip is the existing Windows `/dev/full` case.

The complete Cupid-built static fixed point passes in 789.320 seconds. Its
second and third stages reproduce every Toolchain object and all five hosted
executables byte for byte.

Three-run hosted medians for strict inspection are 5.98 ms for a 6,124-byte
object, 28.92 ms for a 379,648-byte CupidDis ELF, 254.79 ms for the
9,027,296-byte pass-one kernel, and 252.26 ms for the 9,146,080-byte final
kernel. The checked-seed 128 KiB throughput contract was still red at this
source-only decision. A new seed had to carry the indexed decoder before the
contract could become promotion evidence.

The active-source manifest was stale before this work. It now records 29
audited assembly paths, 27 instruction-bearing paths, 1,282 instruction
records, 166 normalized signatures, 189 mode-specific signatures, 42 inline
assembly files, and 230 inline occurrences. The refreshed surface adds the
two Windows startup forms already present in source and corrects the current
x87 reverse-subtract row.

## Rejected alternatives

Increasing the production timeout was rejected. The measured cost was close
to linear in decoded instruction count, so a larger limit would only hide a
large repeated scan across the complete kernel cohort.

A process-global lazy cache was rejected because it would add mutation,
initialization ordering, and data-race concerns to the freestanding shared
module.

Changing catalogue order was rejected because row order is part of stable
decode selection. The index narrows the candidate set without changing that
order.

## Consequences

The optimized path adds a small per-job index and a lifetime-bearing decoder
handle. It does not change the catalogue, accepted instructions, output
bytes, recovery policy, or ABI. The provisional checked seed built before this
decision remains diagnostic evidence only and was not promoted. ADR 0265
later carried this implementation and made strict CupidDis part of the normal
kernel publication transaction. A final poisoned-host `make -j2 all` passed
with exit 0 in 1,022.190 seconds. The exact-size prerequisite accepted all nine
artifacts before publishing the 209,715,200-byte image with SHA-256
`326844ca58c1f864a6b9a2480dfaeb5ed71ec3df22cdb46da17a6bb356e7e726`.
The final four-vCPU E1000 and RTL8139 frontiers passed from that image. Both
used the partitioned USB fixture, `--smp 4`, `--cpu max`, SMP and frontier
runtime verification, a private image, and a 300-second phase timeout. E1000
exited 0 in 725.058 seconds with 103,673 changed framebuffer pixels, 29,608,822
AC97 frames at peak 25,600, and 76,784 PC speaker frames at peak 30,710.
RTL8139 exited 0 in 725.406 seconds with 106,151 changed pixels, 29,601,879
AC97 frames at peak 25,600, and 76,719 PC speaker frames at peak 31,501. Both
used a 640 by 480 framebuffer, and the image hash remained unchanged.

No production file changes owner in this decision. No `.c` to `.cc` rename is
due. `TempleOS/` remains read-only reference material.
