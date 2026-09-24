# Emit block-static objects in hosted CupidC

## Context

The shared frontend already gives each block-static object a completed target type, an absolute block-binding index, and a root in the semantic initializer forest. The hosted IR and object paths stopped at that declaration even though file-scope objects with the same initializer forms already reached deterministic ELF32 output.

The unchanged `dis_hex_fixed` helper in `toolchain/cupiddis.c` contains the active requirement:

```c
static const char hex[] = "0123456789ABCDEF";
```

Moving this array to file scope or rebuilding it on every call would change good source to fit an incomplete compiler. The existing binding identity and initializer forest carry enough information to implement the storage duration directly.

## Decision

A block-static declaration now validates its complete, nonzero target layout and constant initializer root during IR lowering. It advances the same block-binding cursor and visibility boundary as an automatic declaration, but emits no runtime initializer instructions. A later runtime reference still publishes `LOCAL_ADDRESS` with the absolute block-binding index.

The i386 emitter interprets `LOCAL_ADDRESS` according to the binding's storage duration. An automatic object maps to its private EBP-relative slot. A block static emits `PUSH imm32` with an `R_386_32` relocation to a local `STT_OBJECT` symbol. Block statics never participate in frame allocation.

Each symbol is named `.LBS<absolute-block-binding-index>.<source-name>`. The absolute index keeps shadowed names distinct without exposing target symbol policy in Linear IR. Symbols use local binding and default visibility.

Object placement is deterministic. File-scope definitions are placed first, followed by every block static in absolute block-binding order, then functions. Unused block statics and declarations after an unconditional return still receive storage because reachability does not change static storage duration.

Block statics reuse ADR 0015's storage and initializer rules. A top-level `const` object goes to `.rodata`, writable all-zero storage goes to `.bss`, and other writable storage goes to `.data`. The common encoder handles zero, integer, copied string, string address, linked file-object or function address, and recursive array or structure list roots. Represented addresses use direct-symbol `R_386_32` relocations with their checked in-place addends.

Capacity calculations include all block-static symbols and every static `LOCAL_ADDRESS` text relocation before allocation. Invalid roots, mismatched types, unsupported runtime value loads, arithmetic overflow, and output exhaustion fail transactionally. A failed operation leaves the output empty, preserves the frozen translation unit, rewinds job storage, and permits a same-job retry.

## Consequences and evidence

The focused IR proof lowers one static integer to exactly `LOCAL_ADDRESS`, `LOAD`, and `RETURN_VALUE`, with a maximum abstract-stack depth of one. The declaration contributes no initializer store.

The object proof emits eleven block statics. Its sections contain 21 bytes of `.rodata`, 56 bytes of `.data`, and 4 bytes of `.bss`. The exact local symbols run from `.LBS0.hex` through `.LBS10.unused`, including separate `.LBS8.same` and `.LBS9.same` symbols for shadowed declarations. The object has ten text, one read-only-data, and five data relocations. All sixteen are `R_386_32` with addend zero. Functions that only use block statics reserve no frame storage for them.

The fixture covers constant character arrays, initialized and zero integers, arrays, structures, string-backed pointers, linked file-object and function addresses, an unresolved external object, runtime reads and writes, shadowed names, an unused object, and an unreachable object. It also serializes an unused eight-byte integer image. ADR 0066 later adds runtime access to that wide object class.

Negative contracts cover missing and out-of-range roots, an initializer type mismatch, a runtime expression root in static storage, constrained output, rollback, deterministic repetition, and same-job recovery. A referenced eight-byte scalar is now a positive object contract. The active-source guard pins `dis_hex_fixed` without modifying it.

Static initializer addresses based on another block static remain outside the frontend's represented address forms. Wide arithmetic and mutation, production integration, staged self-hosting, and the rest of issue #25 also remain open.

GCC or Clang still builds this hosted proof and the normal OS C objects. No production owner, host dependency, disk image behavior, or runtime path changes in this decision. `TempleOS/` remains reference-only and untouched.
