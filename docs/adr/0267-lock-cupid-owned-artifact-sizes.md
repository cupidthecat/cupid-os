# ADR 0267: Lock Cupid-owned artifact sizes

## Status

Accepted on 2026-08-12.

## Context

The normal build produces a small set of top-level files whose size matters.
The boot image must fit the five sectors copied by the disk builder. The two
kernel links and the raw kernel are deterministic Cupid outputs. The five
checked Toolchain seeds are immutable between reviewed promotions.

The baseline records these sizes, but it does not enforce them. Its older
Windows Clang and Linux GCC `.text` samples differ by 22.73 percent for the
same revision. Neither host compiler is a sound oracle for a normal build
that is now owned by Cupid tools. A blanket 20 percent allowance would also
permit more than 1.8 MiB of unexplained kernel growth before it failed.

## Decision

Keep an exact size policy in `bootstrap/artifact-size-policy.json`. It covers
`boot.bin`, both kernel ELFs, `kernel.bin`, and the five images named by the
selected `BOOTSTRAP_SEED_MANIFEST`. Each row names the repository-relative
path, Cupid producer, exact byte count, and reason for the lock. Rows use
canonical sorted order.

Run the policy through `make verify-artifact-sizes`. The target receives
`$(BOOTSTRAP_SEED_MANIFEST)`, derives the five seed paths and declared sizes
from that selected manifest, and requires the policy to agree. The verifier is
a direct prerequisite of `cupidos.img`. A failure prevents the image recipe
from running and preserves the existing image. An intentional output change
updates the implementation and its policy row in the same review. This makes
growth visible without setting an artificial limit on approved work.

The verifier accepts only the selected nine-artifact cohort. It rejects missing,
duplicate, unknown, or out-of-order rows, unknown fields, duplicate JSON keys,
wrong producer names, policy and manifest size disagreements, invalid sizes,
and unsafe paths. The selected manifest and its five images may reside in an
alternate checked-seed directory inside the repository. The policy, manifest,
and outputs
must be regular files inside the repository. Links and Windows reparse points
fail closed. Artifact failures are collected so one run reports every missing,
nonregular, or incorrectly sized output.

## Evidence

The initial seven focused CLI tests covered a complete valid cohort,
checked-policy and seed manifest agreement, missing, duplicate, and unknown
policy rows, multiple size
mismatches, missing and nonregular outputs, linked files, unsafe paths, an
outside policy, and a linked policy. They pass in 1.251 seconds.

The final 12-test suite also covers a relocated selected seed, a policy that
names an unselected seed, pinned-path reads, and the Windows parent-relative
handle walk. It first passed in 1.603 seconds. After the Linux seed promotion,
all 12 tests passed again in 2.130 seconds.

The live policy lists these production files:

| Artifact | Producer | Exact bytes |
| --- | --- | ---: |
| `boot/boot.bin` | CupidASM | 2,560 |
| `bootstrap/seeds/i386-linux/cupidasm.elf` | CupidASM | 458,256 |
| `bootstrap/seeds/i386-linux/cupidc.elf` | CupidC | 2,666,324 |
| `bootstrap/seeds/i386-linux/cupiddis.elf` | CupidDis | 413,204 |
| `bootstrap/seeds/i386-linux/cupidld.elf` | CupidLD | 312,792 |
| `bootstrap/seeds/i386-linux/cupidobj.elf` | CupidObj | 392,688 |
| `kernel/kernel.bin` | CupidObj | 9,323,140 |
| `kernel/kernel.elf` | CupidLD | 9,548,120 |
| `kernel/kernel.elf.pass1` | CupidLD | 9,421,144 |

These are the live policy values after the typed in-kernel CupidDis adapter.
The new public request path, DEBUG coverage, and embedded manual account for
the reviewed kernel change.
The hashes and sizes below belong to the dated adoption proof and are retained
as historical evidence.

An earlier `make verify-artifact-sizes` attempt timed out after 604 seconds
during a seed-triggered kernel compile and was stopped cleanly. That attempt is
retained as a failed approach rather than counted as a pass.

The frozen-document poisoned-host rebuild passed in 1,018.548 seconds. The
2,560-byte boot image has SHA-256
`46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3`.
The 9,044,032-byte pass-one ELF has SHA-256
`659bd6485deb4e6a18a1efa0f575eb90f210fe5674e9e1257eeef2a4422ff21e`,
the 9,166,912-byte final ELF has SHA-256
`7caf5ad4bc721f10418c06be7cfd8d9568efc8378e7baf2c2f7a510ec49263a3`,
and the 8,950,860-byte raw kernel has SHA-256
`5f0c0becc1ba66a9d3e2eda15555fec39faedc98e2349ad3ee7b2d08775fe1a7`.
These identities supersede the pre-freeze policy evidence.

A final poisoned-host `make -j2 all` passed with exit 0 in 1,022.190 seconds.
The size prerequisite accepted the complete nine-artifact cohort before the
image publisher ran. The resulting 209,715,200-byte `cupidos.img` has SHA-256
`326844ca58c1f864a6b9a2480dfaeb5ed71ec3df22cdb46da17a6bb356e7e726`.
The final four-vCPU E1000 and RTL8139 frontiers passed from this image. Both
used the partitioned USB fixture, `--smp 4`, `--cpu max`, SMP and frontier
runtime verification, a private image, and a 300-second phase timeout. E1000
exited 0 in 725.058 seconds with 103,673 changed framebuffer pixels, 29,608,822
AC97 frames at peak 25,600, and 76,784 PC speaker frames at peak 30,710.
RTL8139 exited 0 in 725.406 seconds with 106,151 changed pixels, 29,601,879
AC97 frames at peak 25,600, and 76,719 PC speaker frames at peak 31,501. Both
used a 640 by 480 framebuffer, and the image hash remained unchanged.

The current combined checkpoint first reached the size gate in 624.6 seconds.
Every build and inspection step passed, and the gate rejected only a measured
680-byte increase in `kernel.bin`. After the live policy adopted the value in
the table above, all twelve policy tests passed in 1.536 seconds. A complete
poisoned-host rebuild then passed in 625.8 seconds and published a
209,715,200-byte image with SHA-256
`69ead54daa9f20eed8e5b4cb3aaac71947f64cce02f08dd8eceb1ab00dc18ddd`.
Two private four-vCPU boots ran `/bin/ls.cc` through CupidC and
`as /demos/hello.asm` through CupidASM. Both reached JIT completion without an
accepted panic or fault marker.

The live verification returns success with empty standard error. Python bytecode
compilation, Ruff, and the scoped diff check also pass.

## Rejected alternatives

A same-revision host comparison remains useful future quality work, but the
current host measurements cannot select a trusted oracle. This decision does
not treat either host compiler as one.

A 20 percent allowance on every current file was rejected. It is too loose for
the five-sector boot contract and checked seeds, and it would hide meaningful
kernel growth. The exact policy can still move when a review explains the
change.

Hashing every kernel output in this policy was rejected. Seed hashes already
belong to the selected seed manifest. The new gate concerns output size and permits
same-size implementation changes without a second kernel provenance system.

The 200 MiB disk image was not added. Its fixed container size does not measure
the amount of generated code, and the image builder already enforces its
layout and capacity rules.

## Consequences

An unexplained change in any locked size stops the normal build with the
observed and expected byte counts before `cupidos.img` can be published. The
existing image remains in place. Developers making an intentional change must
inspect the artifact and update the policy in the same review.

Host Python performs this read-only verification. It does not create or alter
an OS artifact, so production code ownership does not move. The future trusted
Cupid-to-oracle quality comparison and the agreed 20 percent ceiling remain
separate work.

No source was reduced or renamed. `TempleOS/` remains read-only reference
material.
