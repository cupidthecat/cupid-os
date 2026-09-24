# Add GNU atomic fetch-or to CupidC

- Status: Accepted
- Date: 2026-07-24

## Context

The EHCI port-change path keeps a pending-port bitmap. Interrupt handlers may
set that bitmap on several CPUs, and the serialized USB poller clears it when
it accepts the work. If the work queue is full, the poller must put rejected
bits back without losing bits raised by another CPU in the meantime.

The active code expresses that operation as
`__atomic_fetch_or(&pending_ports, mask, __ATOMIC_RELEASE)`. CupidC already
handled GNU atomic load, store, exchange, and fetch-add on represented integer
objects, but it treated fetch-or as an undeclared identifier.

A plain load followed by a store would lose concurrent updates. Replacing the
operation with fetch-add would also be wrong because setting an existing bit
must be idempotent.

## Decision

CupidC now accepts `__atomic_fetch_or(pointer, value, order)` in GNU mode for
complete, non-Boolean integer objects that are one, two, or four bytes wide.
The pointer and value are evaluated once. The public AST and Linear IR retain
the object type and constant memory order in a dedicated fetch-or operation.
The same memory-order checks used by fetch-add apply.

i386 has no single instruction that both computes OR and returns the old
value. The emitter therefore uses a compare-exchange loop:

```text
pop ecx
pop edx
push ebx
mov eax, [edx]
retry:
mov ebx, eax
or ebx, ecx
lock cmpxchg [edx], ebx
jne retry
pop ebx
push eax
```

The byte and word forms use the matching AL/BL/CL or AX/BX/CX lanes and
canonicalize the old value before returning it. EBX is saved because i386
cdecl makes it callee-saved. A locked compare-exchange is stronger than the
requested release order, which is valid for this target.

## Rejected alternatives

`LOCK OR` was rejected because it does not return the old value required by
the GNU builtin.

A compiler runtime helper was rejected because CupidC already owns the
required i386 instructions and ABI. Adding a helper would create a new link
dependency in every freestanding object that uses the operation.

A source-level retry loop was rejected because atomicity belongs in the
compiler contract. Kernel code should not need to reproduce target-specific
compare-exchange details to fit the current compiler.

Treating the discarded result in EHCI as permission to implement a weaker
operation was rejected. The builtin has one meaning regardless of whether a
particular caller consumes its result.

## Consequences and evidence

Frontend and IR contracts pin the dedicated operation, the 32-bit active
object type, release ordering, transactional failures, and an exact Boolean
diagnostic. Object contracts cover signed and unsigned byte and word objects
plus a 32-bit object. They check the exact OR, locked compare-exchange, and
backward-branch bytes, preservation of EBX and the other cdecl registers,
guard bytes, returned old values, updated memory, and one-time operand
evaluation.

The execution oracle injects one competing 32-bit update immediately before
the first compare-exchange. The failed attempt reloads EAX, takes the backward
branch, and succeeds without dropping the competing value. The compiler also
emits unchanged `kernel/usb/ehci.c` through the normal kernel target profile.

The self-host frontier remains valid after the change. CupidC emits
`cupidc_ir.c` as 382,389 text bytes in a 408,484-byte object,
`cupidc_emit.c` as 326,667 text bytes in a 350,092-byte object, and
`cupidc_frontend.c` as 632,095 text bytes in a 749,392-byte object. Repeated
emission is byte-identical.

ADR 0108 promotes this compiler into the checked i386 Linux seed. Pointer
atomics, eight-byte atomics, runtime memory orders, HLE flags, atomic
variadic reads, and atomic aggregates remain open.
