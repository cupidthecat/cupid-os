# Lower linked file-object loads through CupidC IR

## Context

ADR 0018 reached the automatic declaration in `drivers/vga.c`, but the hosted path still stopped when the return expression read `last_flip_ms`. The unchanged function needs three capabilities together: a linked file-object address, a four-byte load through that address, and unsigned greater-than-or-equal.

```c
bool vga_flip_ready(void) {
  uint32_t now = timer_get_uptime_ms();
  return (now - last_flip_ms) >= 16u;
}
```

The frontend already represented this expression with the correct object binding, lvalue conversion, arithmetic conversions, and final conversion to `bool`. The missing work belonged in linear IR and object emission.

## Decision

Linear IR adds `FILE_ADDRESS`. Its `reference` is the absolute file-binding index from the frozen frontend unit, and its `type` is the linked object's semantic type. The instruction pushes an object address. The existing `LOAD` instruction then consumes that address and publishes the value. `LOCAL_ADDRESS` remains the separate identity for automatic objects, and machine addresses remain private to target emission.

This slice accepts internal-linkage and external-linkage file objects whose represented value is a four-byte integer. A function or enumerator identifier remains an unsupported expression at this seam. A mismatched, out-of-range, or otherwise malformed object reference is an invalid frozen unit. Narrow, wide, pointer, floating, and aggregate file-object values remain explicit type boundaries.

The existing `BINARY` instruction now accepts greater-than-or-equal. Its input type determines the predicate. Signed inputs use `SETGE`, while unsigned inputs use `SETAE`. The comparison still produces the frontend's target signed `int` type, and an existing conversion instruction handles the final assignment conversion to `bool`.

The emitter discovers each `FILE_ADDRESS` reference before it creates symbols. A defined internal object therefore keeps its local `STT_OBJECT` symbol, and an unresolved external object receives a global undefined `STT_OBJECT` symbol. The address is encoded as `PUSH imm32` through the shared x86 encoder. The encoder's absolute four-byte field becomes a `.rel.text` `R_386_32` relocation against the direct object symbol with an in-place addend of zero. Direct calls continue to use `R_386_PC32` with addend `-4`.

Relocation capacity counts both direct calls and file-object addresses before emission. Symbol discovery and capacity counting share one instruction classifier so a later relocatable IR kind cannot be added to one pass and missed by the other. The count is exact for the represented IR and uses the same checked overflow and transactional output rules as the existing object path. A failure still leaves the caller's output empty, rewinds temporary job storage, and retains the structured diagnostic.

Hard-coding a section address was rejected because a relocatable object does not yet know its link address. Rewriting an internal object reference to a section symbol was rejected because the canonical frontend binding already provides a stable direct symbol identity. Adding an x86 address or relocation field to public IR was rejected because those details belong to the target emitter. Rewriting `vga_flip_ready` to avoid the file object or comparison was not an acceptable bootstrap shortcut.

## Consequences and evidence

The source-driven IR contract now lowers the complete unchanged `vga_flip_ready` body. Its exact 12-instruction stream has a maximum abstract-stack depth of two. It includes the automatic initializer call and store, a local load, `FILE_ADDRESS` plus `LOAD` for `last_flip_ms`, subtraction, the unsigned constant 16, greater-than-or-equal, conversion to `bool`, and return.

The mixed object contract pins the complete 61-byte function and decodes `SETAE`. The function begins at `.text` offset 460. Its call field has an `R_386_PC32` relocation at offset 471 with addend `-4`, and its `last_flip_ms` address has an `R_386_32` relocation at offset 489 with addend zero. The object remains in `.bss` as a four-byte local symbol.

A separate unresolved `external_clock` fixture emits a 15-byte load function and one global undefined object symbol. Its `R_386_32` relocation proves that symbol discovery and relocation capacity do not depend on an in-unit definition. A 39-byte signed fixture pins `SETGE`, while the active VGA fixture pins unsigned `SETAE`. Negative contracts cover out-of-range and mismatched binding references, malformed identifier payloads, an unresolved internal object, a narrow object type, an enumerator identifier, and a wide greater-than-or-equal expression. Repeat object emission remains byte-identical.

This is hosted bootstrap evidence. GCC or Clang still builds the shared frontend, IR, emitter, x86 and ELF32 modules, and their contracts. The private in-kernel CupidC path still produces every normal OS C object. No production artifact, ABI owner, build transform, host dependency, boot path, or runtime behavior changes here.

ADR 0020 closes the plain four-byte integer file-object assignment frontier named above. ADR 0021 extends `FILE_ADDRESS` to complete record address roots and lowers direct ordinary members through `MEMBER_ADDRESS`. Issue #25 remains open. Bit-field extraction, subscript and pointer-based addresses, atomic and compound assignment, other value widths and aggregates, nested blocks and general statements, indirect and variadic calls, call-site alignment, production integration, and staged self-hosting still remain.
