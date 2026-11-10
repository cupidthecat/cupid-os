# Expand the source-driven CupidC frontier

- Status: Accepted
- Date: 2026-07-25

## Context

After the 116-source production handoff, 38 strict kernel roots remained
outside the checked cohort. Their first failures came from ordinary source
requirements rather than one subsystem: GNU attributes, register snapshots,
typed static nulls, function-pointer representation casts, comma expressions,
and reachability after constant loops. Rewriting those sources would have
hidden useful C and ABI requirements.

The compiler already had the underlying typed AST, linear IR, ELF writer, and
i386 instruction model for most of this work. The missing part was a public,
validated path that preserved the source meaning through every stage.

## Decision

CupidC adds the following bounded capabilities:

- GNU `weak` and `__weak__` mark canonical linked objects or functions.
  Definitions emit `STB_WEAK`; compatible redeclarations merge the fact.
- GNU `section("name")` and `__section__("name")` apply to file-scope objects
  and functions. The public entity owns the decoded name. The ELF writer
  creates arbitrary compatible `PROGBITS` sections, preserves alignment and
  relocations, and rejects empty, relocation-table, symbol-table, and
  string-table names. No active section name is hardcoded.
- GNU `unused` and `__unused__` record canonical entity metadata and merge
  across declarations. They do not change IR or object bytes. `used`,
  `noinline`, `target`, and `naked` remain outside this decision.
- A destination-typed static null pointer initializer uses zero storage even
  when the source expression has an explicit pointer cast.
- `while`, `do`, and `for` loops with a known nonzero integer constant
  expression are non-fallthrough unless a reachable `break` leaves the loop.
  Frontend truth metadata drives both IR lowering and label discovery. A dead
  branch cannot contribute a `break`, while a reachable label can restore
  the branch. The lowerer still validates unreachable clauses
  transactionally.
- The comma operator parses at its proper grammar level, evaluates operands
  from left to right, discards each intermediate value, and keeps the final
  operand's type and value. Assignment-expression separators in calls,
  initializers, and declarations remain separators rather than accidental
  comma operators.
- Represented function pointers may cast to another represented
  function-pointer type or to and from a represented 32-bit integer. The cast
  preserves all 32 target bits and emits no instruction. Object-pointer
  interchange, narrower integers, wider integers, and atomic values remain
  unsupported.
- Exact output-only GNU assembly snapshots may read EAX, EBX, ECX, EDX, ESI,
  EDI, EBP, ESP, the caller return slot at `4(%ebp)`, or EFLAGS through
  `pushf` and `pop`. The EFLAGS form may end in `cli`. Each statement has one
  four-byte `=r` output, evaluates its destination once, and emits a fixed
  template. Control registers, local-label capture, arbitrary trailing
  instructions, extra outputs, and matching inputs remain rejected here.

Every public record is validated again at the IR and object boundaries.
Forged enum values, internal or objectless weak entities, invalid ownership,
incompatible redeclarations, malformed truth metadata, and constrained-output
failures leave no partial result and allow a later operation in the same job
to recover.

## Consequences and evidence

The unchanged `kernel/cpu/ksyms.c` now emits its `.ksyms` data in an
allocatable four-byte-aligned section with valid symbols and relocations.
The unchanged `kernel/lang/cupidc_parse.c` clears all four `unused`
attributes. The generated `ksyms_data.c` honestly advances to its next
unsupported `used` attribute, while `kernel/core/kernel.c` advances past
`.text.start` to its later assembly-clobber requirement.

Together with the other source-driven slices in this change, 20 of the 38
strict roots now compile at compiler head. The prior 18 cover serial and timer
state reads, memory and lock snapshots, application and interrupt function
casts, filesystem, graphics, editor, language, network, SMP, and TLS code.
`ksyms.c` and `cupidc_parse.c` add the final two. Production ownership does
not move until a committed compiler revision produces a refreshed checked
seed and the complete cohort passes its frontier and runtime gates.

The full hosted Toolchain suite passes with the frontend, IR, object, static
tool, assembler, disassembler, and linker contracts on Windows Clang, Linux
GCC, and Linux Clang. Focused positive and negative selectors cover each
capability, deterministic repeat output, rollback, and same-job recovery.

The compiler-head probe compiled these 20 unchanged roots twice:
`drivers/serial.c`, `drivers/timer.c`, `kernel/core/app_launch.c`,
`kernel/cpu/irq.c`, `kernel/cpu/ksyms.c`, `kernel/fs/fat16.c`,
`kernel/fs/iso9660.c`, `kernel/fs/loopdev.c`, `kernel/gfx/deflate.c`,
`kernel/gfx/gfx2d.c`, `kernel/gfx/png.c`, `kernel/gui/ed.c`,
`kernel/lang/cupidc_parse.c`, `kernel/lang/cupidc_string.c`,
`kernel/lang/ssh_io.c`, `kernel/mm/memory.c`, `kernel/network/sshd.c`,
`kernel/network/udp.c`, `kernel/smp/bkl.c`, and
`kernel/tls/tls_ca_bundle.c`. Both runs produced the same 751,472 bytes of
validated i386 ELF32 objects. The ordered source-and-object aggregate has
SHA-256
`867c9e94b716491b4395b5440ea78b392afe108a85ea89d72ecbf79e445d83d5`.
The compiler executable used by the probe has SHA-256
`a814cc5d1107b42d4ef56135c39b12cb2eccb7eb7d0bde1f99600c8b3472e9f2`.

Review found two repeated validation rules. The frontend, IR validator, and
object emitter had separate copies of the source-selected ELF section-name
policy. One pure predicate now owns that policy, while all three boundaries
still validate their own input. The frontend also uses one helper for the
four attributes that cannot apply to a record type. Direct tests cover valid,
empty, reserved, relocation-table, null-storage, and embedded-NUL section
names. Existing positive and negative attribute cases preserve the exact
diagnostics.

The refactor changed the compiler's own source and object snapshots without
changing the 20-root output above. The refreshed source-shape locks for IR,
emission, and frontend contain 203, 204, and 322 functions. Their emitted
`.text` sizes are 392,641, 349,802, and 645,191 bytes; their complete object
sizes are 419,272, 375,736, and 765,588 bytes. The corresponding text
fingerprints are `b66f2486`, `d2d68b59`, and `675c3720`. The complete
Toolchain contract suite passes with these locks.

This decision accepts the compiler-head behavior, not a seed promotion.
Production ownership stays at the checked-seed boundary until a committed
compiler revision rebuilds and reproves all five static tool images. The
generated `kernel/cpu/ksyms_data.c` also stays on the host path because its
`used` attribute remains unsupported.

ADR 0115 records the later production transfer. Those 20 roots now use `.cc`
paths and checked-seed CupidC in the normal Make graph. The historical `.c`
paths above describe the compiler-head proof that preceded the rename.
