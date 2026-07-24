# Emit the per-CPU pointer-output GNU assembly load

- Status: Accepted
- Date: 2026-07-24

## Context

The per-CPU accessor in `kernel/smp/percpu.h` reads the current processor
record with:

```c
__asm__ volatile("mov %%gs:0, %0" : "=r"(p));
```

CupidC already represented integer register outputs and operand-free assembly.
It still rejected `p` because the output has a pointer type. Three non-Doom
header roots and every C source that includes this accessor stopped at that
statement.

The source expresses an i386 ABI fact. Replacing it with an integer temporary
or a helper call would hide the pointer type or change the generated calling
sequence.

## Decision

In GNU mode, CupidC accepts `=r` for one modifiable four-byte object or `void`
pointer output. Qualification on the pointed-to type is preserved. A
top-level `const` or `_Atomic` pointer object is not a valid output, and
function pointers remain outside this slice.

The frontend keeps the pointer type on the assembly operand. Linear IR
evaluates the output address once and then publishes the existing `ASSEMBLY`
instruction. Pointer matching inputs remain unsupported.

The i386 emitter recognizes the exact active template
`mov %%gs:0, %0`. It assigns the output to EAX and asks the shared x86 model
to encode an absolute GS load. The resulting instruction is
`65 A1 00 00 00 00`. The existing output path saves EBX, snapshots the
result, restores EBX, and stores the snapshot through the previously
evaluated destination address.

Other pointer constraints, named operands, clobbers, matching pointer inputs,
different segments or offsets, and general GNU template parsing remain open.
The emitter rejects a pointer output in every other represented template,
including RDRAND and an otherwise valid instruction before or after the GS
load.

## Rejected alternatives

Casting the pointer output to an integer was rejected. It would weaken the
semantic graph and make later pointer checks depend on source rewriting.

Replacing the assembly with a C helper was rejected. A call would change the
ABI sequence and would still need a compiler-supported way to read GS.

Accepting every pointer template once `=r` parsed was rejected. Register
allocation alone does not define the instruction, segment, memory address, or
side effects.

## Consequences and evidence

Frontend contracts cover direct and indirect pointer lvalues, a pointer to a
qualified object, exact `=r` handling, and useful failures for immutable,
atomic, function, named, clobbered, mismatched, and tied forms. The IR
contract proves that an indirect destination call runs once, retains the
pointer type, and leaves unreachable assembly unpublished. The object
contract pins two complete 54-byte functions, all 20 decoded instructions in
each function, the six-byte GS load, deterministic repetition, output
rollback, and same-job recovery. The second function exercises the `void *`
path.

The complete native Toolchain contract suite passes. Its self-source gates
also compile the changed frontend, IR, and emitter twice and pin their new
function counts, text sizes, object sizes, and fingerprints.

The non-Doom header result stays at 150 of 154. `ports.h` still stops at its
width-aware port assembly. The three roots that include `percpu.h` now pass
the pointer output and stop at the undeclared `__atomic_store_n` call on line
49.

The complete `KERNEL_I386` profile gives the same next failure for
`process.c`, `acpi.c`, `bkl.c`, `mp_tables.c`, `percpu.c`, and `smp.c`.
None produces an object, so normal-build ownership and host dependency counts
do not change. The checked seed also predates this compiler addition.

The active build audit still contains 698 sources, 252 feature requirements,
501 transforms, and 39 accounted unreachable files. Its active-source digest
is `f4b70c68052c91844b25e33444fe526a210884e860feb2ed763b2cd5ce10e599`.

No OS build or boot result is claimed for this increment because it changes
no normal-build object or runtime path. Atomic load, store, exchange, and
fetch-add builtins are the next shared SMP boundary.
