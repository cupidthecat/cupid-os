# ADR 0274: Commit the native Windows tool stack

## Status

Accepted.

## Context

The first poisoned-host build after selecting the checked Windows execution
seed stopped while native CupidC compiled `drivers/keyboard.cc`. The process
returned the Windows access-violation status `0xC0000005`; the same command
failed three times, while the checked Linux compiler and hosted Windows
compiler both produced a valid 11,740-byte object.

Phase markers narrowed the fault to the first `switch` lowered inside
`handle_extended_key`. A debugger then showed `cir_lower_switch` reserving a
`0x4984`-byte frame with one `sub esp` instruction. The PE header reserved one
MiB of stack but initially committed only one 4 KiB page. The first push below
the new stack pointer skipped past Windows' guard page and faulted before the
operating system could grow the stack.

Changing only `SizeOfStackCommit` on a private copy of the checked CupidC image
from 4 KiB to one MiB made both a 48-byte empty-switch reproducer and the full
keyboard source compile. The emitted keyboard object retained its expected
11,740-byte size. Increasing `SizeOfStackReserve` alone did not help because it
did not commit the skipped pages.

## Decision

CupidLD now gives its fixed-layout PE32 console images a one MiB stack reserve
and commits that reserve in full. The heap keeps its one MiB reserve and 4 KiB
initial commit. The independent PE reader requires the new stack fields, and
the checked native seed must compile the unchanged keyboard source through the
full kernel profile before it can be accepted.

This is a Windows tool ABI policy, not a source workaround. CupidC still emits
the same C and IR for the keyboard driver, and the driver remains unchanged.
The policy applies to every CupidLD-produced native tool, including CupidC,
CupidASM, CupidDis, CupidLD, and CupidObj.

## Alternatives considered

Moving the validation snapshot in `cir_lower_switch` to the arena would remove
this particular large frame, but it would leave every other large generated
frame exposed to the same Windows guard-page rule. Raising only the reserve was
tested and did not fix the fault. Adding page probes to every large CupidC
frame is the more general compiler feature, but changing the emitter also
changes the compiler that builds itself and needs an additional bootstrap
generation. Full commit is deterministic, fits the bounded hosted-tool stack,
and removes the immediate ABI fault without weakening active source.

## Consequences

Native tools commit one MiB of virtual stack when Windows creates a process.
They still fail normally if they exceed that bounded stack. CupidLD's PE output
bytes change, so the five native images and their manifest must be rebuilt and
verified together. A future page-probing implementation can return the initial
commit to one page after a staged compiler fixed point proves the new prologue
on Windows.

## Evidence

The original checked native compiler failed the empty-switch and keyboard
commands with `0xC0000005`. The private header-only experiment passed both
commands after changing the stack commit field and produced the 11,740-byte
keyboard object. The fixed-layout PE contract and the continued Make-rule
contract pass with the implementation. The bootstrap log records the debugger
evidence and focused tests here. A later promotion entry records the fixed
point, checked seed, poisoned build, and guest run from this committed
revision.
