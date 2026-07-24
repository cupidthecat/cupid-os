# Add the SMP integer atomic builtins to CupidC

- Status: Accepted
- Date: 2026-07-24

## Context

The per-CPU helpers and SMP startup code use four GNU atomic builtins:
`__atomic_load_n`, `__atomic_store_n`, `__atomic_exchange_n`, and
`__atomic_fetch_add`. Eight calls operate on byte flags and five operate on
32-bit counters. Their memory orders are acquire, release, acquire-release,
or sequentially consistent.

The language and order rules follow GCC's
[atomic builtin contract](https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html)
for the represented operations.

Compiler head already handled the surrounding C and the per-CPU GS load. The
atomic calls were the next shared blocker in `percpu.h`, `acpi.c`,
`mp_tables.c`, `percpu.c`, `bkl.c`, `smp.c`, and `process.c`. Replacing them
with volatile reads, ad hoc assembly, or weaker source would have changed the
SMP contract to fit the compiler.

## Decision

The six `__ATOMIC_*` order macros are target predefines with their GCC values
from zero through five. They remain available in strict C11 mode, just as
GCC's target macros do, and source cannot redefine or undefine them. The four
atomic expressions themselves remain a GNU-mode language feature.

CupidC accepts the four builtins for complete one-, two-, and four-byte
integer objects. Loads may read a const object. Store, exchange, and fetch-add
require a non-const object, and fetch-add rejects `_Bool`. The pointer is
evaluated first and once. Operations with a value apply the normal assignment
conversion and evaluate that value once.

The memory order must be an integer constant expression. Loads accept relaxed,
consume, acquire, and sequentially consistent order. Stores accept relaxed,
release, and sequentially consistent order. Exchange and fetch-add accept all
six standard order values. Runtime order arguments, HLE flag bits, pointer
atomics, floating and aggregate objects, and eight-byte atomics remain outside
this slice.

The public AST retains a dedicated expression kind, its ordered child slice,
the unqualified result type, and the validated order. Store expressions have
type `void`. Linear IR uses four dedicated instructions. Each keeps the
integer object type for width, the evaluated pointer type, and the order.
Whole-unit validation checks every atomic expression, including unreachable
ones, before lowering publishes output.

The i386 emitter follows the processor memory model:

- Relaxed, consume, acquire, and sequentially consistent loads use one
  width-correct `MOV`, with sign or zero extension for narrow results.
- Relaxed and release stores use a width-correct `MOV`.
- Sequentially consistent stores and every exchange use memory `XCHG`, whose
  memory form is implicitly locked.
- Every fetch-add uses `LOCK XADD`.

The compiler emits instructions in semantic order and has no optimizer that
can move memory operations around them. Stronger ordering on exchange and
fetch-add is intentional. EAX and ECX carry the temporary address and value;
the path does not borrow EBX, ESI, or EDI.

## Rejected alternatives

Rewriting the SMP helpers around plain volatile access was rejected. Volatile
does not provide the inter-processor ordering or read-modify-write semantics
the source requests.

Open-coding each call as GNU assembly was rejected. That would duplicate
instruction selection in active kernel headers and make the source harder to
type-check.

Lowering every order to a locked instruction was rejected for loads and
release stores. Ordinary naturally aligned i386 loads and stores already give
the required x86 ordering, while an unnecessary lock would make hot per-CPU
paths more expensive.

Silently accepting arbitrary runtime order values was rejected. GCC may map
them conservatively, but CupidC does not yet retain a runtime order operand.
The current diagnostic is preferable to pretending the argument was honored.

## Consequences and evidence

Positive contracts cover all four operations, every accepted order class
including consume, and one-, two-, and four-byte encoding. The object proof
decodes and pins ordinary loads and stores, implicit-lock `XCHG`, and
`LOCK XADD`, including their exact widths and bytes. Its decoded i386 model
also checks returned old values, memory updates, signed and unsigned narrow
results, wraparound, guard bytes, stack and callee-saved state, and
pointer-before-value single evaluation. It rejects calls, stray lock prefixes,
malformed frozen metadata, and partial output. Repeated emission is
byte-identical, and the same job recovers after a rejected unit.

Negative frontend cases cover strict mode, bad arity, non-pointer operands,
records, floating and eight-byte objects, const writes, Boolean fetch-add,
invalid load and store orders, runtime orders, and incompatible values.
Preprocessor tests also reject attempts to redefine or undefine the order
macros.

The active non-Doom header gate advances from 150 to 153 of 154. The three
roots that include `percpu.h` now parse completely; `ports.h` remains at its
width-aware I/O assembly.

Under the complete `KERNEL_I386` profile, compiler head emits deterministic,
validated i386 ELF32 objects for unchanged `kernel/smp/acpi.c` and
`kernel/smp/mp_tables.c`. The other four audited roots now reach their next
real boundary: input-only GNU assembly in `percpu.c` and `process.c`, the
flags template in `bkl.c`, and the `naked` attribute in `smp.c`.

A detached hybrid build replaced only those two SMP objects after the normal
object graph was complete. Both CupidLD passes, CupidDis symbol extraction,
CupidObj flattening, and disk-image construction accepted the result. QEMU
with four CPUs, the `max` CPU model, and e1000 reported RDRAND seeding, 62
successful crypto, ASN.1, and X.509 checks, four discovered and online CPUs,
network initialization, desktop entry, terminal launch, and CupidC JIT
completion. The serial log contains none of the accepted panic, corruption,
self-test failure, or illegal-instruction markers.

This decision changes compiler-head capability, not the normal build. The
checked seed predates the atomic implementation, so the two new objects remain
host-owned until a staged seed refresh and a boot-tested Make cutover carry
the same behavior into production.
