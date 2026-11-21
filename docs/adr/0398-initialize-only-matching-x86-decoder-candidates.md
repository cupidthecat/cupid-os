# ADR 0398: Initialize matching x86 decoder candidates

## Status

Source implementation validated in isolated copies on 2026-09-21. Integration
and checked-seed promotion remain separate steps; no diagnostic executable has
replaced a checked seed.

## Evidence and decision

The first Windows Doom-adoption build exceeded the existing 300-second deadline
for its 431-input `CupidDis --require-known` pass. A retained private copy of the
same batch passed in 293.462 seconds. No decoding error was reported, but the
remaining margin was small.

The indexed decoder already narrows the catalogue by first opcode byte. Within
each bucket, it clears a complete 284-byte candidate before testing the remaining
opcode bytes and prefixes. Most escaped-opcode rows fail those tests and never
read or publish the cleared storage. Host instrumentation of the exact batch
records 5,704,653 instructions and 89,698,818 candidate attempts. Only 7,273,632
attempts reach the first candidate write. The original calls request
27,094,585,764 zeroed bytes; 23,408,752,824 of those bytes belong to early rejected
rows. These are source-operation counts, not measured memory-bus traffic.

Move candidate initialization into `x86_decode_row`, after its existing opcode,
prefix, and early invalid-encoding checks and before its first candidate write.
Keep initialization of the public result at the start of `x86_decode_impl`.
The caller reads a candidate only after `X86_PARSE_OK`, and every successful
path crosses the moved initialization. Partial late failures remain discarded.

The catalogue, bucket order, prefix rules, longest-match choice, alias preference,
and truncated-before-invalid fallback stay the same. No instruction, operand,
diagnostic, or publication check is removed. The helper definition moves earlier
in the file so the call has a declaration in ordinary C.

## Validation

The existing sixteen host contract modes pass for both baseline and candidate
on Windows and Linux, with identical output. They include the complete catalogue,
indexed/exhaustive comparisons, active instruction surface, aliases, addressing,
relocations, invalid inputs, truncation, and public-result initialization.

A new sequence in `decoder-index` reuses one prepared decoder across fifteen
ordered cases in each mode. It alternates field-bearing and field-free successes,
early opcode/prefix failures, late truncations, and subsequent successes. Every
call starts with dirty public output and checks fixed classifications, mnemonics,
lengths, bytes, and encoding-field counts. A mutant that omits initialization of
matching rows fails the intended assertion. Pattern-initialized Clang builds make
that failure independent of accidental stack contents.

The complete modified contract also passes all sixteen modes for both baseline
and candidate on checked Cupid-built PE32 and ELF executables: 64 successful
mode executions. Status, stdout, and stderr match across variants and hosts.
Both variants produce identical contract and decoder object bytes across hosts.
These runs use hash-verified source closures and stage-four objects from the
retained `83d00ce7` proof. Their exact commands and hashes are recorded in
`build/bootstrap/x86-decode-initialization-draft/regression/native-summary.json`.

Checked CupidC compiles the changed object on both platforms. Each diagnostic
relinks the remaining hash-verified objects from the committed `83d00ce7` proof.
The baseline relink reproduces the checked CupidDis image exactly. The candidate
passes object, executable-format, and import checks. Both hosts produce the same
changed object, SHA-256
`f912265d1ed23b00a3c6877ab4bdd129d43d234ff8171e31d1a2c4ccfa1f34eb`.
The initial private source used CRLF; compiling its canonical LF form reproduces
that object exactly. The retained initial source remains available for audit.

All 256 paired rendered/strict cases match on each Cupid-built executable,
including 86 expected strict rejections. The 2,432-byte fixture contains 768
instructions from the escaped-opcode bucket. Three-run medians change from
0.552 to 0.071 seconds on Windows and 0.539 to 0.076 seconds on Linux. Its requested
zeroing falls from 46,021,632 to 436,224 bytes. The candidate Windows executable
passes the exact full 431-input batch in 71.535 seconds, with empty stdout and
stderr and the unchanged 300-second limit. These are diagnostic measurements
under the current host load, not a universal throughput guarantee.

## Integration boundary

The source and tests passed the required isolated OS validation. Builds still
run the existing checked decoder. Clean paired bootstrap proofs and seed
promotion must carry this implementation into that path; same-generation relink
experiments are not fixed-point or promotion evidence.

The change adds no source path or public API. It changes a shared decoder used by
the toolchain and OS, so integration must check the active source audit, normal
kernel build, exact artifact policy, and private boot/runtime smoke. Preserve the
generated-compilation work and its adjacent-parent reader window when rebasing.
`TempleOS/` remains read-only reference material outside the build and metrics.

## OS preflight

The decoder copies preserve the completed generated-compilation builds. Each
runs `make -j1 -o FORCE all`, ignoring only the empty FORCE trigger and retaining
all real dependencies. The changed decoder and manual rebuild through the
existing checked tools; inspection deadlines remain unchanged. Both first runs
stop only at the expected exact-size check. Matching measurements change the
raw-kernel policy from 9,554,244 to 9,554,884 bytes; both ELF size rows stay fixed.
The manual is 51,945 bytes with SHA-256
`beb667fbcb56d7709cf13eae6ee571d58c54dafb3337e9894935b6c8f4c35a3f`.

All three kernels match across hosts. Both measured-policy finishes and all
sixteen standalone artifact checks pass. The private four-CPU `max`/`e1000`
smoke verifies SMP, runs `dis /bin/ls.cc`, requires a raw listing containing a
return instruction, and then requires normal `ls` completion. Serial logs
contain generated instructions rather than only a heading, with no compiler,
disassembler, or panic failure. Both source images remain unchanged. These are
source OS preflights, not fixed-point, promotion, or Doom gameplay evidence.
Commands, hashes, initial failures, and serial output are retained under
`build/bootstrap/20260921-decoder-source-preflight/`.
