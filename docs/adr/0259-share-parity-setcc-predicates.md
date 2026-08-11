# ADR 0259: Share parity SETcc predicates

## Status

Accepted on 2026-08-10.

## Context

Private CupidC uses the x86 parity flag when it turns an unordered floating
comparison into a C Boolean. Equality and ordered relations merge `SETNP`
with the primary comparison result. Inequality and floating truth use `SETP`
to keep NaN true where required.

Those two byte predicates were absent from the shared x86 catalogue. CupidDis
therefore rendered the leading `0F` as data, then misread `9B C2` as `FWAIT`
and a large immediate `RET`. The listing drifted across the following Boolean
merge. CupidASM could not express the same instructions either.

## Decision

Add canonical `SETP r/m8` and `SETNP r/m8` rows to the shared x86 model. They
use the ordinary SETcc recipe in both execution modes:

| Form | Encoding | Condition |
| --- | --- | --- |
| `SETP r/m8` | `0F 9A /r` | PF is set |
| `SETNP r/m8` | `0F 9B /r` | PF is clear |

The existing register and memory machinery supplies byte operands, 16-bit and
32-bit addressing, and address-size overrides. CupidASM and CupidDis need no
adapter-specific parser or decoder case.

Keep `SETP` and `SETNP` as the only accepted spellings in this slice. Active
CupidC emits those names, and the shared model does not yet claim the complete
SETcc alias family. `SETPE` and `SETPO` therefore remain unknown mnemonics.

Source head has 604 forms, 249 canonical mnemonics, 64 register names, and
fingerprint `55A8970F`. The promoted seed has 602 forms, 247 canonical
mnemonics, and fingerprint `64429699`. These two source rows remain outside
that seed trust unit. ADR 0258 records the promoted baseline.

## Evidence

The first public test run failed at all three consumer boundaries. The shared
lookup returned `not_found` for `setp`. CupidASM reported an unknown
instruction, and CupidDis split each private parity opcode into fallback data
and unrelated instructions. The three focused tests failed in 6.746 seconds.

After adding the two catalogue rows, the same three tests passed in 7.576
seconds. Locking the inventory first exposed the measured 604/249/64 and
`55A8970F` boundary against the old 602/247/64 lock. The focused model,
active-surface, CupidASM, and CupidDis group then passed all eight tests in
8.004 seconds.

The shared-model contract covers both mnemonics in both modes, a byte
register, native and cross-size memory addresses, exact encoding, decode
semantics, requested-form replay, every-byte truncation, invalid operands,
illegal prefixes, rollback, and same-job recovery. It also pins rejection of
`setpe` and `setpo`. Public CupidASM tests check exact raw bytes and useful
diagnostics. Public CupidDis tests decode complete emitter-shaped equality and
inequality sequences without a `db 0x0F` fallback.

The GUI terminal contract compiles and disassembles `test_fpaug.cc` before
running it and the full feature-13 program. The bounded source emits the same
ordered, unordered, and truth sequences as feature 13. Its listing must
contain `setnp dl`, `and al, dl`, `setp dl`, `or al, dl`, and the following
`movzx eax, al` in order. The runtime check then requires equality, unordered
inequality, and NaN truth to return one. All 119 smoke-contract tests pass.
The broader x86, CupidASM, and CupidDis modules pass 42 tests with one existing
platform skip.

The first rebuilt-image proof used feature 13 for both inspection and runtime.
A contended 600-second run did not reach the desktop. A clean 600-second run
and a later 900-second run both booted normally but never exposed a listing on
serial. Moving the expressions into the small floating regression did not fix
that: the bounded source compiled, yet its listing was still absent. That run
falsified source size as the cause.

The shell passed the ordinary `shell_print` callback to all three CupidC and
both ELF disassembly paths. In GUI mode, that callback writes only to the
terminal, while the smoke reads serial. A focused source contract failed in
one millisecond on the missing callback. The shell shares its existing
routing logic with a disassembly callback that mirrors only the GUI listing to
serial. Process sinks and redirection still return before either destination,
and text mode is unchanged. The focused contract and all 119 GUI contracts
pass, and checked-seed CupidC compiles the changed shell object.

The complete root build passed in 503.412 seconds. Its two new local helpers
raise the pass-one inventory to 4,718 text symbols and the logical symbol blob
to 114,851 bytes. The packed object is 115,264 bytes with SHA-256
`a5eb7e848b156754dc87203e806411ed006694167b5a67dd8233d8ef9f71a65c`.
The final 8,900,124-byte kernel has SHA-256
`ed9acc572058ef0dbd330a0384135a941b1babcb2a08ce2b1e96dc93551a3e33`,
and the 209,715,200-byte image has SHA-256
`f254205f377d2fd8b2e1c253007211dd1d116f2e026963f3606125cc1ac06487`.

Four-vCPU e1000 and RTL8139 frontiers then passed in 517.701 and 519.233
seconds. Both serial logs contain the canonical parity rows, the compact
runtime PASS, the full feature-13 PASS, and the rest of the fixed GUI, USB,
filesystem, Doom, browser, network, and audio sequence. Their SHA-256 values
are `731c1bc170cba7d5c2d218af911285d9526b085c70fba780bbab3f3ec8a6a559`
and `3c5b0be52bed3afb7cd83061d51f91ab72a51f80a0f712b90ba278ca39b91f97`.

The first RTL8139 attempt was excluded after concurrent serial output damaged
one dglibc marker, leaving `S] dglibc FAT read boundary`. The gate correctly
refused the incomplete line. A fresh run produced the intact marker and passed
without changing the matcher or product code.

The stacked tree regenerated and checked the active-source audit. Its snapshot
digest is `5b2b2e935b3f90f9f147ad26b1604d80e6349cc6c797edaeafd86f72290dce68`.
Generation passed in 68.121 seconds; its 3,886-byte log has SHA-256
`bbd224afe525838d673bdeec0169f389e48f58065b6c762fb9d1ed472916ca92`.
The final serial check passed in 67.952 seconds; its 3,894-byte log has
SHA-256
`6a98699a202874d933c39a3a997749149146a42b68ddd0ebb78d51dad23e58ad`.
An earlier check overlapped the Toolchain rebuild and exhausted host memory
before it could compare the audit. The serial retry removed that contention.
All 75 build-graph tests then passed in 776.306 seconds. That 13,617-byte log
has SHA-256
`f2ce834dba35bd6da982d4b8101644b3fd9a06873e2eb1e169f0e3ad48888b79`.
The combined x86, CupidASM, CupidDis, and GUI group ran 161 tests in 14.343
seconds: 160 passed and one platform test was skipped. Its 56,878-byte log has
SHA-256
`af0e548e757004c655daa9783062bc0bc0037f5445a1ebe9f2fc9b44dd620002`.

`make -C toolchain all` rebuilt the 604-row source catalogue through both
checked stages. The stage-two and stage-three objects and executables matched,
and all 20 artifacts were published and verified in 3,397.047 seconds. The
12,010-byte log has SHA-256
`b9f6611f6fdda1014b80d6045e6577834c079cffa20cab1b7685a822ecd8fac6`.

The first canonical Toolchain replay stopped at the old structural lock for
`x86.cc`. It measured 60 functions, 1,766 statements, 11,903 expressions, 180
block bindings, 17,112 initializers, three object definitions, and no labels.
The corrected focused selector passed in 10.662 seconds.

The next replay cleared 220 selectors and the IR self-host frontier before the
object frontier found three stale source-head locks. Adding `SETP` and `SETNP`
shifts the later mnemonic enum values embedded by `cupidc_emit.cc` and
`cupidasm.cc`; the two catalogue rows also grow the compiled `x86.cc` object.
Two isolated replays produced byte-identical 1,570-byte logs and the same
measurements. Their SHA-256 is
`effc6498954c93ff10c181af919a0c3bdf4125f3d422c5067d9419ea93dbf4cd`.
The old contract checked those locks before comparing its repeated output and
then described either failure as nondeterminism. The corrected contract checks
the repeat first, reports inventory drift separately, and locks only the four
measured changes.

The corrected public object selector reached the repeated-buffer comparison
and passed in 25.939 seconds. Its 313-byte log has SHA-256
`9c049aad5af62ff4abb7341c3d014b107ee58a193337ef31b084d59ede7c8a03`.
One later canonical attempt was interrupted during checked contract
compilation; the atomic builder discarded its partial cohort. The final
`make -C toolchain test` run rebuilt both stages, matched every staged object
and executable, passed the hosted runtime, published and verified 20
artifacts, passed both self-host frontiers, and completed the remaining x86,
CupidDis, CupidASM, demo, kernel-ELF, CupidObj, and CupidLD contracts. It took
6,218.713 seconds. The 107,596-byte log has SHA-256
`f9ee398ae3d5054c2578f60c5d3c72cd17953ce5d6efe11a65d32536e7ab2456`.

## Rejected alternatives

A private CupidDis exception was rejected because the opcodes are ordinary
x86 instructions that CupidASM and future CupidC emitters should share.

Adding the complete SETcc family was not selected. The active source gap is
the parity pair, while a larger family would add unmeasured catalogue surface.

Adding `setpe` and `setpo` aliases was deferred for the same reason. Their
absence is tested, so a later source requirement must make that expansion
explicit.

## Consequences

CupidASM can assemble the parity predicates that private CupidC already emits,
and CupidDis can inspect those floating comparison and truth sequences without
losing instruction alignment. The change adds no host dependency and changes
no production owner. GUI disassembly remains visible in the terminal and is
also serial-observable, which lets the runtime gate inspect production output
without adding an oracle.

The checked five-tool seed still predates the two rows. Issue #13 remains open
for the broader self-hosting and seed work. Issue #31 remains open for the rest
of the private CupidC runtime. No `.c` to `.cc` rename is due, and
`TempleOS/` remains untouched reference material.
