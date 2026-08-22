# ADR 0322: Run the Toolchain manifest author natively on Windows

## Status

Accepted on 2026-08-22.

## Context

The Toolchain publisher asks one Cupid-built program to decide all 58
stage-three to stage-four equality pairs in a `CUPMAN4` request. The converged
stage-four Linux CupidC, CupidASM, and CupidLD tools produce that author. Linux
runs the resulting static ELF directly. Windows previously ran the same ELF
through WSL even though the repository already has a checked Windows startup,
runtime, PE linker path, import contract, and native execution boundary.

This WSL hop was not part of the author's semantics. The request schema, raw
pair evidence, Linux publication seed, and producer lineage were already
independent of the executable container used to run the author.

## Decision

Keep the converged stage-four Linux tools as the only producers of the
Toolchain manifest author. Select the final executable format by host. Linux
continues to build a static i386 ELF with the Linux startup and runtime
objects. Windows uses the same stage-four CupidC to compile the unchanged
author source and the checked Windows runtime, the same stage-four CupidASM to
assemble `tool_start.asm`, and the same stage-four CupidLD to link an i386 PE32
image.

The Windows link declares the exact `KERNEL32.dll` import inventory already
used by checked Cupid tools. The publisher validates the PE class, machine,
entry point, image layout, relocation state, and exact imports before the
author can be returned for execution. A malformed PE fails the private build
and cannot reach the process launcher.

Do not change the `CUPMAN4` bytes, schema, 58-pair inventory, or author logic.
The native PE receives the same frozen request and must emit the same canonical
manifest bytes as the independent Python oracle. Python keeps pinned
filesystem capture, source and seed drift checks, process launch, private
staging, rollback, and atomic directory publication. A failed build, author,
or oracle comparison preserves the previous cohort and leaves no transaction
workspace behind.

## Evidence

The focused Windows build test first failed because the author build compiled
only the contract object and selected the Linux startup. It passes after the
host-selected PE build was added. The focused group covers the Linux build,
the exact Windows compile and link plan, exact imports, PE validation before
execution, failed-publication cleanup, and recovery.

On Windows, the checked-stage-four integration test freezes the promoted Linux
seed, uses its CupidC, CupidASM, and CupidLD images as the producers, builds a
native PE author, blocks WSL path translation during author execution, and
still receives the exact Python-oracle manifest bytes. That test passes in
52.208 seconds.

## Rejected alternatives

Using the promoted Windows execution seed to build the author was rejected.
That would change producer provenance and would make the author depend on a
separate Windows reconstruction plan. The Linux stage-four producer lineage
is already the publication authority.

Keeping both an ELF author and a PE author on Windows was rejected. Running
both would duplicate execution without moving any semantic boundary and would
add another result that the transaction must reconcile.

Moving request construction, the independent comparison, or publication into
the PE was rejected. Those operations are separate host transaction duties and
need their own ownership transfer, safety design, and tests.

## Limitations

Windows still needs WSL to execute the Linux seed during fixed-point
reconstruction and the Linux contract cohort. This decision removes WSL only
from the final `CUPMAN4` author execution. Host Python still builds the request,
launches the checked author, verifies its result independently, and publishes
the cohort.

The focused integration test proves the native execution seam and exact output
against representative `CUPMAN4` data. A complete `make -C toolchain all` run
remains the publication-wide evidence gate.

## Ownership impact

The Cupid-built author retains semantic ownership of all 58 stage-pair
decisions. Python retains transaction and independent-oracle ownership. The PE
is a host-native execution form of the same Cupid source, not a second author
and not a transfer of semantic ownership to Windows-specific code.

No seed image, manifest, schema, source lock, or operating-system artifact is
promoted by this change. No `.c` file qualifies for a rename. `TempleOS/`
remains untouched reference material.
