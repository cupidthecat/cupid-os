# ADR 0298: Prove every shared x86 catalogue form

## Status

Accepted on 2026-08-15.

## Context

CupidASM, CupidDis, and hosted CupidC already share one private x86 form
catalogue. Active-source manifests and focused instruction tests exercised a
large part of it, but they did not prove that every row could be reached in
each legal mode. A row could become unencodable, select the wrong decode form,
or mishandle truncated input without changing an active source fixture.

The catalogue is private implementation data. Publishing its rows as a new
production API would expose ordering that is only meaningful under one model
fingerprint. A generated golden file would also need an external maintenance
step and could drift away from the implementation it was meant to check.

## Decision

Add a test-only catalogue contract to the existing x86 contract program. The
fixture includes a renamed copy of `x86.cc` only to inspect private row
metadata. Encoding and both exhaustive and indexed decoding still call the
separately linked production implementation.

The contract is bound to 604 forms and fingerprint `55A8970F`. It constructs
one deterministic, valid instruction for every encodable row in every legal
mode, requests that exact form from the real encoder, decodes the bytes through
both real decoder paths, and re-encodes the decoded form byte for byte. It also
checks every proper byte prefix, every decode-alias row, all invalid rows, and
each form that excludes one of the two supported modes. A fixed witness digest
detects changes to selected forms or bytes even when the row count stays the
same.

The proof covers 1,202 encodable legal-mode cases, 12 decode-alias cases, four
invalid cases, two illegal-mode rejections, and 2,641 proper prefixes. Four
proper prefixes are complete shorter instructions and must make progress; the
rest must report retained truncated input. Every declared row flag is present
in the locked coverage, and the witness digest is `8C570035`.

Keep a separate public path from source text to bytes and back. CupidASM
assembles representative selectors, including SHRD, a memory NOP, condition
aliases, IRET, and push/pop aliases. CupidDis must accept the exact bytes under
`--require-known`, render canonical aliases, and produce the same listing on a
repeat run.

The test fixture belongs to every build closure that compiles the x86 contract.
The build audit rejects a Make edge or publication-input list that omits it.
It also requires the normal Toolchain recipe to run every public x86 contract
selector.

Compare decoded records by their active typed fields. Inactive union storage,
unused array entries, and structure padding are not part of the public decoder
result. A focused case gives two equal records different background bytes and
requires them to compare equal.

## Evidence

The public Python test failed first because `x86-contract catalogue` did not
exist. The finished contract passes all 604 rows without skipping an operand
class. The 1,202 legal-mode witnesses and all 2,641 proper prefixes agree under
the exhaustive and indexed production decoders. Exact-form replay preserves
every full witness.

The focused x86 suite passes 16 tests. The CupidASM suite passes 16 tests. The
CupidDis suite passes 22 tests with one platform skip. The new native source
round trip emits
`0F AD F8 66 0F 1F 00 0F 92 C2 CF 60 61 9C 9D`, passes strict inspection,
and renders stable canonical names. Build-graph dependency and frozen-input
checks pass.

The first checked-seed execution exposed a false decoder mismatch at form 156.
Both paths selected the same two-byte form and published the same active
fields; byte offset 29 differed because it is padding after a register index.
Replacing whole-record `memcmp` with the typed comparison made both
`decoder-index` and `catalogue` pass. The audit then found that
`decoder-index` and `double-shift` were public selectors missing from the
normal Toolchain recipe. An exact selector-set check failed first, and the
recipe now runs both.

Checked-seed CupidC compiles the final source-current x86 contract into a
439,768-byte ELF32 object with SHA-256
`03ea401f82a514e53ace55d101105ccc4fbde78113cadd4607e187bacd723f82`.
Checked CupidASM supplies startup and checked CupidLD links a 580,632-byte
static i386 executable with SHA-256
`d1ac70060697ebd7058dc1a90eaf0f4a94af9a51a73a61375e18aa8fd3459841`.
That executable passes `catalogue`, `decoder-index`, and `double-shift` under
WSL. An earlier compile reached only an I/O failure because its requested
output directory did not exist; it produced no parser or code-generation
diagnostic.

The final build-graph audit passes all 100 tests, including exact selector and
contract-input closure checks. Audit regeneration and deterministic replay
also pass. A poisoned-host normal build passed all nine artifact-size checks
in 718.1 seconds after the expected measurement rejection moved the one changed
`kernel.bin` row. A private four-vCPU e1000 boot then ran `/bin/ls.cc` through
in-OS CupidC and passed the strong SMP runtime contract.

## Rejected alternatives

Publishing the private catalogue was rejected because form identifiers are
valid only with their fingerprint and are not part of the toolchain API.

Checking only active source was rejected because it leaves dormant catalogue
rows unproved. Active source still provides the feature roadmap and keeps its
own end-to-end parity checks.

Committing bytes produced by NASM, LLVM, or another external generator was
rejected for the normal test. Those tools remain useful optional oracles, but
the required proof must run with Cupid's checked implementation alone.

Comparing the complete object representation of decoded structures was
rejected after the checked build exposed different inactive padding. Equality
now follows the public typed result instead of compiler-specific padding.

## Consequences

Every current shared x86 catalogue form now has a deterministic witness in
every legal mode, with exact encoder and decoder agreement. A catalogue change
must update its fingerprint, coverage totals, and witness digest in the same
reviewed change.

This is exhaustive by form and legal mode, not by every possible operand,
addressing, prefix, or immediate value. Focused and active-source contracts
continue to own those semantic combinations.

No production instruction row, checked seed, build owner, or required host
dependency changes. The source is already named `.cc`; no C-source rename is
due. `TempleOS/` remains untouched reference material.
