# Represent operand-free GNU assembly statements in CupidC

- Status: Accepted
- Date: 2026-07-24

## Context

CupidC first learned the GNU assembly forms used by `kernel/crypto/csprng.c`.
That work covered register outputs, single-digit matching inputs, and five CPU
instructions. It did not cover a basic statement such as:

```c
__asm__("pause");
```

The active C graph contains 94 basic GNU assembly blocks. Forty-one are inside
functions and 53 define functions or data at file scope. Of the function-local
statements, 38 use only `pause`, `sti`, `hlt`, `sti; hlt`, `cli`, or `fninit`.
The remaining three contain labels and control transfer.

This was a compiler limitation, not a reason to rewrite the kernel. The
checked seed stopped at basic assembly in `drivers/e1000.c`,
`kernel/gui/desktop.c`, `kernel/network/socket.c`, and
`kernel/network/tcp.c`.

## Decision

In GNU mode, CupidC represents nonblank function-body basic assembly and the
operand-free extended form with an empty output list. A basic record carries
`CTOOL_C_ASSEMBLY_BASIC` and is implicitly volatile. An extended record with
no outputs is also implicitly volatile. Both forms own an empty operand slice.

Linear IR validates that a basic record is volatile and has no operands. It
also rejects an input-only record until CupidC has a real input constraint and
register plan for that form.

The i386 emitter accepts exact instruction sequences made from `pause`, `nop`,
`sti`, `hlt`, `cli`, `cld`, `sfence`, and `fninit`. It emits each instruction
through the shared x86 model. An operand-free statement allocates no temporary
frame slot and does not save or restore EBX. The existing operand-bearing NOP
contract remains valid.

Clobbers, blank compiler barriers, file-scope assembly, labels, branches,
calls, and general template parsing remain outside this decision.

## Rejected alternatives

Replacing active assembly with helper calls was rejected. The instructions
express processor state and timing directly, and a call would change both
behavior and ABI details.

Treating basic assembly as extended assembly with an invented output was
rejected. It would create a false data dependency and unnecessary register
traffic.

Accepting blank templates without clobber semantics was rejected. The active
Doom sound barrier uses a blank template with a `memory` clobber. Emitting no
bytes is correct only after CupidC represents the compiler barrier.

Extending this statement parser to the 53 file-scope blocks was rejected for
this increment. Those blocks also own sections, symbols, relocations, data,
and complete function bodies.

## Consequences and evidence

Frontend, IR, and object contracts cover basic syntax, the empty extended
form, implicit volatility, empty operand ownership, deterministic emission,
exact decoded instructions, exact 10-byte and 12-byte function bodies with no
frame adjustment, and same-job recovery. Useful failures cover blank
templates, input-only forms, clobbers, `asm goto`, malformed record flags,
forged operand slices, and an instruction that needs outputs.

All 54 CupidC object tests pass in 806.507 seconds. That run includes the
five-tool static fixed-point proof, so CupidC, CupidASM, CupidDis, CupidLD,
and CupidObj still match between stages two and three after this compiler
change.

The native compiler at this revision emits production-valid i386 ELF32
relocatable objects for the four sources that previously stopped at basic
assembly:

| Source | Object bytes |
| --- | ---: |
| `drivers/e1000.c` | 8,780 |
| `kernel/gui/desktop.c` | 111,196 |
| `kernel/network/socket.c` | 12,416 |
| `kernel/network/tcp.c` | 20,204 |

A detached hybrid build replaced exactly those four host-built objects with
the objects above. Both CupidLD passes and CupidObj accepted the mixed link.
The final `kernel.elf` was 6,525,952 bytes with SHA-256
`14ff0583a2a76ab95d17c8a4e57969c6604301baf8207eaa9f41c487b0fccd67`.
`_kernel_end` was `0x00B2E910`, leaving 857,840 bytes below the fixed stack.
The 6,347,254-byte `kernel.bin` has SHA-256
`667dc8431169b9561331efff07fca7b553353d04229ac5fb0c6346f2f8b16db2`;
the 209,715,200-byte image has SHA-256
`ea33e324b3a8ea057d401cc6aa5d76a20fa4aa03d2a312a6d8a387fd5d54ad1a`.

QEMU used its `max` CPU and e1000 device. The image seeded through RDRAND,
passed all 62 crypto, ASN.1, and X.509 checks, initialized e1000, reached the
desktop and terminal, and completed `/bin/ls.cc`. The 34,958-byte serial log
has SHA-256
`534890a308a934eb47c5ebcbe19f51253d4bebf0897de34fb1765fbaed74cbe0`
and contains no panic, corruption, exception, illegal-instruction, or failed
self-test marker.

This changes compiler capability at source head. The checked seed still
contains the earlier compiler, so normal build ownership does not move in
this decision.
