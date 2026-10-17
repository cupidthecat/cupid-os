# ADR 0166: Represent the fabs file-scope assembly

## Status

Accepted on 2026-07-28.

## Context

After CupidC learned the `libm_exp_impl()` statement, unchanged
`kernel/cpu/libm.c` reached an aligned read-only mask block at line 242.
The block defines two local labels for the following `fabs` and `fabsf`
wrappers. Each wrapper loads its argument into XMM0, clears the sign bit with
the matching mask, and returns through the i386 floating result convention.

The existing file-scope assembly path represented twelve exact opening
wrappers. It did not yet model data definitions, local assembly labels,
alignment, absolute data relocations, or the `ANDPD` and `ANDPS` wrapper
shapes.

## Decision

Compiler-head CupidC represents the exact three-effect sequence: the aligned
mask block, `fabs`, and `fabsf`. The mask effect must precede both wrappers.
It reserves the first 32 bytes of `.rodata` at alignment 16, defines local
`STT_NOTYPE` symbols `fabs_mask_d` and `fabs_mask_s` at offsets 0 and 16,
and emits the source mask bytes exactly.

Mask placement happens before ordinary C and block-static objects. Later
read-only data starts after offset 31, so existing constants cannot move the
assembly labels. The later source-order assembly pass skips the already
placed mask effect.

The two wrappers keep their source prototypes and global function symbols.
`fabs` emits 15 text bytes and one `R_386_32` relocation to
`fabs_mask_d`. `fabsf` emits 14 text bytes and one `R_386_32` relocation to
`fabs_mask_s`. Both use Cupid's shared x86 model for their move, bitwise
operation, and return instructions.

## Evidence

The mask bytes are:

```text
FF FF FF FF FF FF FF 7F FF FF FF FF FF FF FF 7F
FF FF FF 7F FF FF FF 7F FF FF FF 7F FF FF FF 7F
```

The wrapper bytes before relocation are:

```text
fabs:  F2 0F 10 44 24 04 66 0F 54 05 00 00 00 00 C3
fabsf: F3 0F 10 44 24 04 0F 54 05 00 00 00 00 C3
```

The relocations sit at function offsets 10 and 9 with zero addends. A mixed
read-only-data fixture places another constant at offset 32 and proves that
the mask symbols stay fixed. Frontend, Linear IR, and object contracts cover
exact source order, prototypes, symbols, bytes, relocations, deterministic
output, duplicate or missing effects, label conflicts, forged metadata,
rollback, and same-job recovery.

The unchanged source now reaches the `floor` wrapper at line 281.

## Rejected alternatives

Passing these effects to GAS would retain a host assembler dependency in
CupidC object emission.

Treating the mask labels as C objects would change the active source and
would not represent file-scope assembly data.

Appending the masks after ordinary `.rodata` was rejected because earlier C
constants would change the assembly label offsets. The mixed-data regression
exposed that layout error.

## Consequences

Compiler head can now emit the exact `fabs` data and wrapper slice. The
checked seed still carries only the twelve opening wrappers, so the normal
`kernel/cpu/libm.c` transform remains host-owned and keeps its `.c` suffix.

No normal OS object, ABI, image, runtime path, or host-dependency count
changes. `TempleOS/` remains untouched reference material.
