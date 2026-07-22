# Carry eight-byte integer object values through CupidC

## Context

ADR 0065 gave hosted CupidC one logical eight-byte integer value for constants, fixed call results, discard, and return. Object access still stopped at lvalue conversion. The unchanged `get_cpu_freq` function in `kernel/core/kernel.c` exposes that next boundary:

```c
uint64_t get_cpu_freq(void) {
    return tsc_freq;
}
```

`tsc_freq` is a file-scope eight-byte object. Changing its type, splitting the read into two source-level words, or replacing the object with a helper written for the current compiler would hide an ordinary active-source requirement.

The one-handle snapshot model from ADR 0065 already gives a wide value stable identity. Structure values use the same idea for lvalue snapshots and bytewise stores. Extending that model keeps object semantics out of the public IR and avoids a separate register-pair value system.

## Decision

Hosted CupidC accepts signed and unsigned eight-byte integer objects at the supported address and value seams. File objects, block statics, fixed automatic objects, pointer dereferences, ordinary members, and indexed elements can produce an address. Lvalue conversion copies eight target bytes into an instruction-owned frame snapshot and publishes one logical value handle.

Plain assignment uses the existing `STORE_VALUE` contract. It copies the source snapshot to the destination and preserves the same snapshot handle as the assignment result. `STORE` performs the same copy without a result for declaration initialization and aggregate initializer leaves. This covers direct and chained assignment without borrowing either destination address as the resulting C value.

The emitter allocates a private slot for each wide `LOAD`, after ordinary automatic storage and in deterministic instruction order. It establishes forward string direction with `CLD`, preserves ESI and EDI, and copies eight bytes with `REP MOVSB`. Wide stores use the same byte-copy helper. A later return reads the snapshot through ADR 0065 and restores EDX:EAX.

Volatile wide lvalues remain evaluated at their source expression and use the same bytewise transfer. This is not an atomic access and does not promise a single machine instruction. `_Atomic` eight-byte loads and stores remain rejected because the current copy sequence cannot provide their ordering or indivisibility contract.

This step did not add eight-byte parameters or call arguments, conditions, arithmetic, compound assignment, increment or decrement, variadic transport, or mixed-width conversion. Those paths kept their focused diagnostics and transactional rollback at this decision boundary.

Publishing two public stack lanes was rejected for the reasons recorded in ADR 0065. Keeping a wide lvalue in EDX:EAX was also rejected because nested evaluation, calls, and chained assignment would overwrite the value before its C lifetime ends. Treating the source address as the value was rejected because a later store could change an earlier load. The private snapshot makes the copy point explicit without exposing frame offsets or target registers in Linear IR.

## Consequences and evidence

The IR contract uses eleven functions, including the complete unchanged `get_cpu_freq` function. It covers a file object, an automatic local, a block static, pointer dereference, an ordinary member, indexed access, a wide aggregate initializer leaf, volatile access, plain and chained assignment, discard, and return. Each function has a fixed instruction count, stack depth, and stream fingerprint. The combined stream contains fourteen wide `LOAD` instructions, two `STORE` instructions, four `STORE_VALUE` instructions, one file address, three local addresses, three member addresses, one indexed pointer operation, ten wide returns, and two discards. The volatile return is also checked for a volatile-qualified load source. Repeated lowering is deterministic and leaves the frozen translation unit unchanged.

The deterministic ELF32 object contains 16 data bytes and 879 text bytes with fingerprint `2448A1CD`. It has fourteen symbols and two `R_386_32` text relocations. One relocation lies inside `get_cpu_freq` and names `tsc_freq`; the other lies inside `block_static` and names its exact eight-byte local object. Decoder checks cover all eleven functions, twenty byte copies, and the aggregate initializer's complete-object zeroing.

A decoder-driven i386 oracle relocates and executes `get_cpu_freq` and `block_static`, then checks both halves of EDX:EAX against their data objects. It also executes plain and chained pointer assignment and checks both destinations, the unchanged source, adjacent guard words, ESP, and EBP. Separate negative contracts reject atomic wide loads and stores without publishing partial IR or object output.

The first IR and object runs stopped at the former unsupported file-object load. Several older negative fixtures became valid once object access was present, including a referenced block static, a fixed automatic local, declaration initialization, a wide aggregate leaf, assignment, and discard of a wide lvalue. They now have positive contracts instead of stale rejection claims.

This remains hosted bootstrap evidence. GCC or Clang still builds the shared compiler, its contracts, and every normal C object. The private in-kernel CupidC compiler remains the runtime JIT and AOT path. No production object, build transform, boot path, host dependency, or ownership count changes.

Issue #25 remains open. Eight-byte values without declared parameter types, conditions, arithmetic, mutation, mixed-width conversion, production integration, staged self-hosting, and the fixed-point bootstrap remain unfinished. ADR 0067 adds declared wide parameters and named call arguments without changing the object-copy model.

## Extension: operation result snapshots

ADR 0068 allocates the same private object-shaped storage for every wide shift, bitwise result, and represented-to-wide conversion. Wide-to-represented conversion reads the snapshot without borrowing its address as a C value. The object load and store rules from this decision do not change.
