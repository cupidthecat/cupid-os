# ADR 0224: Transfer kernel-symbol source generation to CupidObj

## Status

Accepted on 2026-08-03.

## Context

ADR 0222 added CupidObj's transactional `ksyms-source` command, and ADR 0223
promoted a checked five-tool seed that carries it. The normal two-pass kernel
link still asked Python to turn CupidDis output into
`kernel/cpu/ksyms_data.cc`. That left a production source generator outside
Cupid tooling even though the checked command was ready.

The handoff must preserve the stronger boundary around this generated root.
The pass-one ELF and seed cannot change during either tool invocation. Bad
symbol text, a failed tool, a partial output, or disagreement with the existing
format must not replace the last good source.

## Decision

In `mksyms --seed-manifest` mode, hostbuild now performs these steps in order:

1. Freeze the pass-one kernel and the checked-seed manifest.
2. Run checked CupidDis `-n` against the private ELF.
3. Preserve its exact canonical text in a private input file.
4. Parse that text in Python and render the expected source as an independent
   parity oracle.
5. Recheck the live inputs, then run checked CupidObj `ksyms-source` against
   the private text.
6. Require a regular output that is not a symbolic link and matches the oracle
   byte for byte.
7. Recheck the live inputs once more and atomically replace the destination.

The optional `--nm` development path keeps its Python renderer. It remains an
oracle path and does not own a supported build transform.

The build graph classifies the production step as
`generate_ksyms_source` with CupidDis, CupidObj, and Python participants.
CupidDis owns inspection, CupidObj owns source generation, CupidC owns the
following compilation, and Python supplies snapshots, parity checks, and
publication. This raises CupidObj's supported-graph participation from 185 to
186 transforms without claiming a Python-free build.

## Evidence

Four focused tests first failed against the old route because CupidObj was
never invoked. They cover ordered CupidDis-to-CupidObj execution, checked-tool
failure, missing output, and Python-oracle mismatch. The final hostbuild suite
contains 46 tests; it also proves exact text handoff, drift before CupidObj,
drift after CupidObj, and preservation of an existing destination. It passed
in 2.155 seconds. The 101 GUI-smoke harness tests passed in 2.224 seconds.

A direct production probe generated the current 379,312-byte source through
the promoted seed. Its SHA-256 is
`45a112be18fc9edab1680b1c1622eeb2e2f6b3333e12652be3e0593a8f612f2a`,
and it matches both the Python oracle and the source produced before the
handoff. The packed logical blob is 114,421 bytes.
Checked CupidDis reported 4,704 text-symbol rows in both kernel passes, with
no address/name drift.

Two complete `make -j4 all` builds passed through the new route in 620.5 and
605.9 seconds. The second includes the updated in-OS manuals. Its pass-one ELF,
final ELF, raw kernel, and image are:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/kernel.elf.pass1` | 8,925,492 | `8ec64e9bb40f68832d4666287c1ae5704fa0bf5c95b4fa8e2e8bc802cb5aaf47` |
| `kernel/kernel.elf` | 9,040,180 | `3a22ec2b535b80c109dcc5597f389764cfbe2935ccab514dfaf2238199e39903` |
| `kernel/kernel.bin` | 8,832,828 | `e305a1cacfe315142f216148e16238a37c3e58ff04c5eca2adb1cf1bb270e6d9` |
| `cupidos.img` | 209,715,200 | `c7db4cd4de6f8425a51d7f536764e04a1313e67f57cd9a2703465fe14343ad52` |

A private four-vCPU e1000 boot passed the exact Browser self-test contract in
88.8 seconds. ACPI brought all four CPUs online, RDRAND seeded the generator,
e1000 obtained `10.0.2.15`, and the 26-field Browser result passed. The
38,246-byte serial log has SHA-256
`d4488a02e515d8aee0cb87e4292d18981970fc348eb5ebaa983cf74c8c5e3d42`.

The active-build audit and its independent check passed together in 214.9
seconds. They retain 718 active inputs, 449 reachable transforms, 255 feature
requirements, and 25 classified unreachable files. The active-source digest
is `48a25995a6eb517807dca2f77234ed953ca7ae967845fad446c9a011d0941f75`.
The 2,554,973-byte JSON has SHA-256
`368af0f92bdaf7b359dbd0067040bdd7eb790a8f13e3527787a90eb4f203f82d`,
and the 12,136-byte summary has SHA-256
`8c412081a7311487f1ea4185a1e84d5007ab17e8e119b54f2cac18dd3642db38`.

After the stale `sizeof` inventory expectation was corrected, the complete
68-test build-graph module passed under WSL in 1,004.134 seconds.

## Rejected alternatives

Removing the Python renderer in the same change was rejected. Keeping an
independent implementation makes format drift visible while CupidObj takes
production ownership; removing the remaining Python orchestration is a later
bootstrap boundary.

Piping CupidDis output directly into CupidObj was rejected. A private file
preserves the complete producer output, gives both implementations the same
bytes, and allows input checks between the two tool calls.

Trusting a zero CupidObj exit status was rejected. The wrapper must also see a
regular output and exact oracle parity before publication.

Combining symbol inspection and source generation into one command was
rejected. The canonical text seam is already covered by fixed-point and real
producer-to-consumer contracts, and it keeps the two tools independently
useful.

## Consequences

CupidObj now owns four production `.cc` generators: three installation tables
and the kernel symbol table. Python still participates in the transform
because it launches the checked tools, protects the input snapshot, and checks
parity, but it no longer constructs the published source bytes.

The normal build still requires host Python, and Windows still uses WSL to run
the static i386 Linux seed. Removing those orchestration bridges remains later
work.
