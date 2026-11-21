# ADR 0265: Promote parity, floating, and strict inspection support

## Status

Accepted on 2026-08-12.

## Context

The checked i386 Linux seed came from revision
`9115787311bf455b6eee19e7742cc83aa252e7c8`. It predated four tested Toolchain
capabilities that now belong in the bootstrap trust unit:

- the shared `SETP` and `SETNP` x86 rows from ADR 0259
- static long-double addition, subtraction, multiplication, and division from
  ADR 0260
- typed CupidDis summaries and `--require-known FILE [FILE...]` from ADR 0262
- ordinary non-atomic `float` and `double` updates from ADR 0263

Leaving those capabilities at source head made the checked seed an incomplete
statement of the Toolchain that rebuilt it. A promotion must preserve the
fixed build plan, rebuild every tool from one captured source closure, and
show byte identity between stage two and stage three.

## Decision

Promote the stage-three i386 Linux tools built from revision
`95f5bb6cfd0468bb8852c670ada849cb5bde79a7`:

| Tool | Size | SHA-256 |
| --- | ---: | --- |
| CupidASM | 449,912 | `0d9647b61bc422e88fbc6f8d846f5041e02deca192efe4cfd62df64910340b26` |
| CupidC | 2,666,240 | `ab83e817e49f6f51a31fb41955d33ca6faa4d2073c975ba3a87999c44eeca7cb` |
| CupidDis | 396,500 | `acb136752d504445ad52abc315532a2427db844bdd5da98e2d2d78380047a73e` |
| CupidLD | 312,792 | `9561d6f7170472cd6dccd87d4988fdd2b23a138966cbe4940a9ffb062eab481d` |
| CupidObj | 392,688 | `7137ad601a7c22178112fbf08163b36ff2064807caa99962df97d7ae7ae62f2b` |

CupidASM and CupidC each grow by 4,296 bytes from the provisional candidate.
CupidDis grows by 8,460 bytes. CupidLD and CupidObj remain byte-identical. The
initial seed comparison therefore differs only for CupidASM, CupidC, and
CupidDis.

The 5,440-byte manifest has SHA-256
`5b46684d9977287f69a94473acbbf7c5302213ef98f9748482cba768ffca0be8`.
The 19-source build plan remains unchanged at SHA-256
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.

The checked seed and source head now share the same x86 catalogue: 604 rows,
249 canonical mnemonics, 64 register names, and fingerprint `55A8970F`.
CupidASM and CupidDis both carry canonical `SETP` and `SETNP` byte predicates.
CupidC carries target-only static long-double arithmetic and prefix or postfix
updates for modifiable non-atomic `float` and `double` lvalues. CupidDis carries
typed strict inspection through `--require-known` and ADR 0266's immutable
first-opcode decoder index.

Seed promotion and production adoption use separate gates. After the
promotion and reproof pass, the normal kernel path uses one hostbuild
transaction for checked CupidDis validation and checked CupidObj flat
extraction over the same frozen cohort.

## Evidence

The bootstrap captured 43 source inputs with SHA-256
`56e0943f82737a7013994f1a2b78fcbd5b5c762d0f5036aac5a48bfbb3dcbe32`.
Stage two and stage three match across all nineteen C objects, startup, and all
five linked tools. The behavior suite matches across five help cases,
eighteen successful operations, and sixteen useful failures.

The Windows runtime probe also matches between stages. The validated image
prints `Cupid-built Windows runtime: ok`, writes no stderr, and returns 37.

The 17,035-byte bootstrap report has SHA-256
`810704f6701b4b4627062981e1e969332d4aa5f409d2cdce3d4fcba150518f84`.
The complete proof passed in 763.5 seconds. Its initial-seed comparison refers
to the rejected candidate from revision
`99c5fab5539f53dfd983aa3f304209c6260a6c36`. It records changes for CupidASM,
CupidC, and CupidDis while recording CupidLD and CupidObj as matches.

The checked 128 KiB strict-inspection throughput selector passes within its
30-second limit. The focused parity and strict selector, including the matrix,
passed in 7.213 seconds. The floating carriage selector passed in 4.696
seconds.

An independent poisoned-host reproof started from the promoted manifest and
passed in 766.9 seconds. All five seed images match stage two. Stage two and
stage three match across all nineteen C objects, startup, and all five tools,
and both stages pass the 5/18/16 behavior matrix. The Windows loader proof
passes with exit 37. The report binds the 5,440-byte manifest above, the same
43-input source snapshot, and source revision
`95f5bb6cfd0468bb8852c670ada849cb5bde79a7`. Its 17,032 bytes have SHA-256
`736872f31d853fe5b2b67c25e7ec42a1893655074a1c653112def6d66fdeac87`.
An independent rehash matches every stage artifact recorded in the report.

The first production gate ran:

```text
python -B tools/hostbuild.py validate-code --seed-manifest bootstrap/seeds/i386-linux/manifest.json --root . --input-manifest bootstrap/cupiddis-production-inputs.txt
```

It passed with exit 0, empty standard output and standard error, and elapsed
time 185.526 seconds. The 9,028-byte LF-only input manifest has SHA-256
`48bdef348f6575881b9808631173e7265abc9ea89dfb84d48de72b3d2304749e`.
It lists 429 unique graph-ordered repository paths: all 427 audited root object
outputs, the pass-one kernel ELF, and the final kernel ELF. Make retains all
429 paths as direct prerequisites while passing only the manifest to
hostbuild. Hostbuild freezes and rehashes the seed manifest, input manifest,
and every selected input around the checked CupidDis invocation. Fourteen
focused hostbuild and build-graph tests passed in 36.591 seconds.

The production path now combines that validation with flat extraction.
Hostbuild freezes the selected seed manifest and all five artifacts, the
429-entry input manifest and cohort, and the existing `kernel.bin` boundary.
Checked CupidDis validates the private cohort, then checked CupidObj flattens
the frozen final ELF into a private candidate. Hostbuild rechecks the live
trust inputs and output before parent-relative atomic publication. Every
failure preserves the prior raw kernel. The complete transaction passed with
exit 0 in 187.054 seconds and published an 8,946,332-byte `kernel.bin` with
SHA-256
`4f5f2591d01bcc4007773844e9bfb8112a16dd17fbd178014cc2056fefaab67d`.
The focused hostbuild suites each passed 31 tests on Windows and in WSL;
platform-specific cases were skipped on the opposite host. Moving private
flatten extraction onto the shared pinned-path helper remains deferred
maintenance.

The final audit records 450 transforms across the three supported roots and
441 under root `all`. Its tool participation totals are Python 450, CupidC
245, CupidObj 191, CupidASM five, CupidLD five, and CupidDis two. It retains
the 5/18/16 fixed-point matrix and assigns strict validation plus flat
extraction to `kernel.bin`, with all 429 code inputs represented. `make
bootstrap-audit` passed in 64.780 seconds.

The poisoned-host normal `make -j2` then passed in 1,057.969 seconds. `CC`,
`CXX`, `CPP`, `HOSTCC`, `HOSTCXX`, `ASM`, `AS`, `LD`, `AR`, `NM`, and
`OBJCOPY` all named invalid commands. That historical build ran the separate
production strict gate before CupidObj flattened the kernel. The resulting
artifacts are:

| Output | Bytes | SHA-256 |
| --- | ---: | --- |
| `boot/boot.bin` | 2,560 | `46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3` |
| `kernel/kernel.elf.pass1` | 9,039,936 | `b21fa8954499a7857ee4b12fa3950fcc08ff3c6a6234c8ae72effc38c51fdc6d` |
| `kernel/kernel.elf` | 9,162,816 | `a0b57cd886369762b65d657bb3f2915ada8f30b52102535add89466eaf4f5976` |
| `kernel/kernel.bin` | 8,946,332 | `4f5f2591d01bcc4007773844e9bfb8112a16dd17fbd178014cc2056fefaab67d` |
| `cupidos.img` | 209,715,200 | `4548005bd0aa1a3cffb74620c2309d53c6b291ea2505ed187034bf6b13f1bb37` |

After the OS documentation was frozen, a poisoned-host normal `make -j2`
rebuild passed in 1,018.548 seconds. These current artifacts supersede the
pre-freeze identities above:

| Output | Bytes | SHA-256 |
| --- | ---: | --- |
| `boot/boot.bin` | 2,560 | `46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3` |
| `kernel/kernel.elf.pass1` | 9,044,032 | `659bd6485deb4e6a18a1efa0f575eb90f210fe5674e9e1257eeef2a4422ff21e` |
| `kernel/kernel.elf` | 9,166,912 | `7caf5ad4bc721f10418c06be7cfd8d9568efc8378e7baf2c2f7a510ec49263a3` |
| `kernel/kernel.bin` | 8,950,860 | `5f0c0becc1ba66a9d3e2eda15555fec39faedc98e2349ad3ee7b2d08775fe1a7` |
| `cupidos.img` | 209,715,200 | `326844ca58c1f864a6b9a2480dfaeb5ed71ec3df22cdb46da17a6bb356e7e726` |

A final poisoned-host `make -j2 all` passed with exit 0 in 1,022.190 seconds.
Its exact-size prerequisite accepted all nine artifacts before image
publication. The final four-vCPU E1000 and RTL8139 frontiers used the
partitioned USB fixture, `--smp 4`, `--cpu max`, `--verify-smp-runtime`,
`--verify-frontier-runtime`, `--private-image`, and `--timeout 300`.

| NIC | Result | Framebuffer | AC97 | PC speaker |
| --- | --- | --- | --- | --- |
| E1000 | PASS, exit 0 in 725.058 seconds | 640 by 480, 103,673 changed pixels | 29,608,822 frames, peak 25,600 | 76,784 frames, peak 30,710 |
| RTL8139 | PASS, exit 0 in 725.406 seconds | 640 by 480, 106,151 changed pixels | 29,601,879 frames, peak 25,600 | 76,719 frames, peak 31,501 |

Both private-image runs left the source image unchanged at SHA-256
`326844ca58c1f864a6b9a2480dfaeb5ed71ec3df22cdb46da17a6bb356e7e726`.

The definitive boot frontiers remain pre-freeze runtime evidence. They used
the image with SHA-256
`4548005bd0aa1a3cffb74620c2309d53c6b291ea2505ed187034bf6b13f1bb37`,
the partitioned USB
fixture, four virtual CPUs, `--cpu max`, SMP and frontier runtime verification,
a private image copy, and a 300-second phase timeout.

| NIC | Result | Framebuffer | AC97 | PC speaker |
| --- | --- | --- | --- | --- |
| E1000 | PASS, exit 0 in 794.034 seconds | 640 by 480, 103,637 changed pixels | stereo 44.1 kHz, 32,097,292 frames, peak 25,600 | stereo 44.1 kHz, 78,044 frames, peak 29,866 |
| RTL8139 | PASS, exit 0 in 758.667 seconds | 640 by 480, 104,964 changed pixels | stereo 44.1 kHz, 30,838,813 frames, peak 25,600 | stereo 44.1 kHz, 76,756 frames, peak 30,161 |

Both private-image runs left the source `cupidos.img` unchanged at SHA-256
`4548005bd0aa1a3cffb74620c2309d53c6b291ea2505ed187034bf6b13f1bb37`.

The promoted-seed user frontier passed with exit 0 in 3,291.317 seconds. The
publisher rebuilt stage two and stage three, required byte identity, and
transactionally published the complete 21-artifact contract cohort. The user
gate then accepted schema `cupid.user-syscall-abi.v1`, version 5, 103 fields,
a 412-byte table, and 101 providers. The ABI SHA-256 is
`3e4d31320b2f56d19d37796ef679d1abbb228de9f36c9520d2dd5ec430c3c0bc`.
The 23-input user frontier has SHA-256
`f63919f4b4307278c825ebedf99391e3ec110646042ee397dac3a7ba330435d3`.
Both frontier passes produced the same six files:

| Program | Object bytes | Object SHA-256 | Executable bytes | Executable SHA-256 |
| --- | ---: | --- | ---: | --- |
| hello | 6,124 | `64e0a6ee0d7a45a0901d3db614e73481cdc6b30903345c5015601b2bf344be04` | 13,992 | `4c5622969f39ffe7c2427d65abae2d293dfbd76db2aa80c96f9e6cf01613600c` |
| ls | 7,120 | `e0627996a1d9cd6fd428642ffdfada7e07afa81d9267bc714360014af0dd3971` | 18,112 | `094b017eb6914bce6fbc1e99adeae845d5dc05280c1c1d897e68ab9d687c8d79` |
| cat | 6,292 | `ff002fc4710704c3941bf6320249e772a3448d15f99269987ab1b9b608b3acb4` | 13,992 | `b66cba4c98221f5006ad4aeee70349a82db20410e027aa863bc33fa5818b5f4c` |

A fresh build in a unique output directory passed in 10.492 seconds and
reproduced all six identities in the table. Disposable staged-copy runs
returned 0 for hello in 54.546 seconds, ls in 52.637 seconds, and cat in 80.043
seconds. Cat used a 62-byte marker-shaped fixture and passed the negative
serial-event boundary. The source and evidence images remained unchanged at
the current image hash above.

## Rejected alternatives

Promoting only the three changed executables was rejected. The manifest binds
all five tools as one trust unit, including the two byte-identical images.

The first promotion candidate from revision
`99c5fab5539f53dfd983aa3f304209c6260a6c36` was rejected. Its checked
CupidDis took about 82 seconds to inspect 64 KiB of one-byte instructions and
timed out on the 128 KiB throughput contract. Raising the timeout would have
hidden a repeated full-catalogue scan. ADR 0266 instead indexes candidates by
their first opcode byte while preserving catalogue order and recovery.

Leaving the four capabilities at source head was rejected because the checked
bootstrap would continue to rebuild a Toolchain newer than its own recorded
capability boundary.

Combining production adoption with the seed transition was rejected. The
normal gate was added only after the promoted seed passed its independent
poisoned-host reproof.

Passing all 429 paths directly on the recipe command line was rejected. The
expanded Windows command exceeded 8,191 characters and truncated after 396
paths. The checked-in graph-ordered manifest keeps all direct Make
prerequisites while reducing the evaluated command to 163 characters.

Treating an older 20-artifact Toolchain directory as the current cohort was
rejected. The first user-frontier attempt stopped before modifying that
directory because the current manifest requires 21 artifacts. The stale
directory was preserved outside the canonical path. The supported publisher
then built a private complete cohort and moved it into place only after all
stage comparisons and manifest checks passed.

## Consequences

The checked i386 Linux bootstrap now carries the current shared x86 catalogue,
static long-double arithmetic, ordinary scalar floating updates, and strict
typed CupidDis inspection with indexed candidate selection. The fixed point
still uses CupidC, CupidASM, and CupidLD as its producer trio and still covers
the same 19-source plan.

The normal kernel path now requires checked CupidDis to accept all 427 root
object outputs plus both linked kernel ELFs before checked CupidObj flattens
the frozen final image in the same private transaction. The gate produces no
rendered listing and adds no host code generator. Python freezes the selected
five-tool seed, manifest and input cohort, and output boundary. It rechecks
the live trust inputs and output before parent-relative atomic publication.
Every failure preserves the prior raw kernel.

This promotion removes no host dependency. Windows still uses WSL for the
Linux seed, Python still orchestrates the bootstrap and normal build, and a
native Cupid-built Windows seed remains open. No `.c` to `.cc` rename is due,
and `TempleOS/` remains read-only reference material.
