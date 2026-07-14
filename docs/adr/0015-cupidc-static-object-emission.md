# Emit CupidC static objects through the shared ELF32 writer

## Context

ADR 0014 gives each file-scope object definition a completed i386 type, linkage, storage, source location, and one root in the immutable semantic initializer forest. That forest can describe zero initialization, target integers, copied narrow strings, string addresses, addresses of linked objects or functions, and recursive array or structure lists. It deliberately does not assign storage, serialize bytes, create symbols, or emit relocations.

ADR 0002 already owns deterministic i386 ELF32 `ET_REL` serialization. Its writer accepts semantic sections, symbols, and `R_386_32` or `R_386_PC32` relocations without exposing file offsets or generated ELF tables. CupidC needs a narrow operation between these two records before function IR and machine-code lowering are ready.

Host compiler probes confirm the required object semantics but do not provide a deterministic layout policy. Clang and GCC both place const objects in allocated read-only data, initialized writable objects in data, and tentative writable objects in BSS. Both emit `R_386_32` relocations for static pointer values. They differ in object order, trailing padding, and whether a reference to a local object uses that object symbol or a section symbol plus an addend. CupidC therefore fixes its own source-semantic policy instead of copying either host's incidental ordering.

## Decision

CupidC exposes one public `ctool_c_emit_object` operation. It borrows one successfully frozen `ctool_c_translation_unit_t`, does not mutate it, and writes one deterministic little-endian i386 ELF32 `ET_REL` object through `ctool_elf32_write`. The operation owns C storage selection, object layout, symbol construction, initializer serialization, and relocation construction. The shared ELF32 writer remains the only owner of serialized ELF layout.

The payload section order is `.rodata`, `.data`, then `.bss`. `.rodata` and `.data` are allocated `PROGBITS`; only `.data` is writable. `.bss` is allocated, writable `NOBITS`. Object definitions retain translation-unit source order within the section selected for them. Each object starts at the alignment from its completed ADR 0013 layout, each section carries the greatest alignment of its objects, padding bytes are zero, and an object's ELF symbol value is its section-relative offset.

Storage selection follows the object itself, not the type of any value it points to:

- An object with top-level `const` qualification is emitted in `.rodata`, including a const object whose bytes are all zero.
- A writable object whose complete initialized image is zero is allocated in `.bss`. This includes tentative definitions and explicit zero initialization. CupidC emits real BSS definitions rather than `COMMON` storage.
- Every other writable object is emitted in `.data`.

Defined objects use `STT_OBJECT`, their completed size, and local or global binding from C linkage. Internal-linkage objects use local binding. External-linkage definitions use global binding. Referenced external declarations that have no definition in the translation unit are emitted as global undefined symbols. Object declarations use `STT_OBJECT`; function declarations use `STT_FUNC`. Undefined symbols have value and size zero.

A copied string initializer writes its bytes directly into its destination array. A string-address initializer materializes immutable literal bytes in `.rodata` in first source encounter order and gives that storage a local object symbol. This initial operation does not merge distinct string literals.

Every represented static address reserves a four-byte little-endian field and creates one `R_386_32` relocation. The relocation names the object or function symbol directly. The initializer's signed addend is written into the reserved field, as ELF32 `REL` requires. String addresses keep a zero addend. Binding addresses may carry the checked target-byte offset produced by ADR 0014, including array-element and one-past-array addresses. CupidC does not rewrite a local-object reference to a section-symbol relocation. Direct symbol ownership keeps the relocation tied to the binding recorded by ADR 0014.

This first operation accepts static-only translation units. It rejects any translation unit containing a function definition before it emits output. Function declarations may still contribute undefined `STT_FUNC` symbols when a static initializer takes their address. Text sections, linear IR, machine code, calls, `R_386_PC32`, and function-local static objects remain outside this increment.

The operation is transactional. Invalid frozen indices, unsupported initializer forms, size or alignment overflow, output limits, and the function-definition boundary produce a structured diagnostic and leave the caller's output empty. Temporary section, symbol, byte, and relocation storage is reclaimed on both success and failure.

## Consequences and evidence

The first contract covers const bytes, writable bytes, tentative BSS, a string pointer, local and global objects, and undefined object and function references in one static-only source. It now also covers `&array[1]` and `array + 2`, whose direct-symbol `R_386_32` relocations contain addends 4 and 8 in their in-place words. Reading the emitted object back through `ctool_elf32_read` verifies section order and flags, exact bytes and padding, symbol binding, type, value, size, and every relocation offset, target, kind, and addend. A second emission of the same translation unit must be byte-identical.

Several active-source shapes set the requirements. The declaration contract parses bounded prefixes containing all seven 35-member definitions and two tentative state objects from `kernel/gui/gui_themes.c`, plus the `ramfs_ops` definition with its string and eleven function addresses from `kernel/fs/ramfs.c`. Active Doom declarations also require `&mousearray[1]`, `&joyarray[1]`, and `&finesine[FINEANGLES / 4]`. The focused frontend and object fixtures cover those array-address semantics without claiming that the full Doom translation units parse or emit. The exact Doom profile currently reaches an earlier frontend blocker at `kernel/doom/dglibc_compat.h`, where `__builtin_va_list` is not yet a recognized type. No compatibility shim is part of this decision.

Direct object-symbol relocations intentionally differ from the section-symbol optimization used by current Clang and GCC for some local references. Both encodings have the same i386 link meaning. The Cupid form is easier to trace back to the typed binding and is deterministic across bootstrap hosts.

This decision produces CupidC-owned static object bytes without claiming code generation or production build ownership. Member addresses, explicit address casts, other deferred static address forms, attributes that affect section or binding policy, function lowering, and full translation-unit emission stay explicit later work.
