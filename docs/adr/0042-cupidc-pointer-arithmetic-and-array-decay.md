# Lower pointer arithmetic and array decay through CupidC

## Context

The shared CupidC frontend already types pointer addition, pointer subtraction, pointer difference, normalized subscripts, compound assignment, and pointer updates. Hosted IR lowering stopped when those nodes reached the represented object-pointer path.

Active Cupid OS source needs that path. Both ATA transfer loops advance a `uint16_t *` after moving one sector:

```c
insw(ATA_PRIMARY_DATA, buf, 256);
buf += 256;
```

The write loop has the same `buf += 256` requirement after `outsw`. Rewriting either loop as integer address arithmetic would discard pointer semantics and hide a real compiler requirement.

Pointer arithmetic also depends on target layout. An integer offset counts pointed-to objects, not bytes. Pointer difference produces an element count. The compiler must preserve that rule for one-byte objects, four-byte integers, and records whose size is not an x86 addressing scale.

## Decision

`ctool_c_lower_ir` publishes `POINTER_BINARY` for represented pointer arithmetic. Its result type remains in `type`, its left operand type remains in `input_type`, and `reference` stores the absolute graph type of the right operand. The record accepts pointer plus integer in either order, pointer minus integer, and subtraction of compatible complete-object pointers. Integer offsets must fit the represented 32-bit slice. Pointer difference uses the frontend's signed `int` target type.

The i386 emitter reads the pointed-to size from the frozen target layout. It multiplies an integer offset by that size before addition or subtraction. Pointer difference subtracts the addresses and uses signed division by the same size. A size of one needs no multiplication or division. The general multiplication sequence also handles object sizes such as twelve, which cannot use an x86 scale field directly.

The frontend already rewrites `pointer[index]` and `index[pointer]` as addition followed by dereference. The IR therefore needs no subscript-specific instruction. Both spellings use `POINTER_BINARY`, then `DEREFERENCE`, then the existing typed load.

`ARRAY_TO_POINTER` records a complete array address becoming a pointer to its first element. It emits no machine instruction because both forms already contain the same address. Validation moves array qualification to the element comparison and rejects a pointer with missing element qualification. This increment enables linked file arrays. ADR 0044 later extends the private local-layout policy to referenced, uninitialized fixed automatic arrays and records.

Pointer `+=`, `-=`, `++`, and `--` reuse `DUPLICATE_ADDRESS`. The destination is evaluated once, loaded once, updated with `POINTER_BINARY`, and stored once. Prefix forms produce the stored pointer. Postfix forms recover the earlier pointer with the inverse offset after the store, without loading the object again. A volatile pointer object follows that one-load, one-store path. Atomic pointer objects remain unsupported until the IR states an ordering contract.

Converting pointers to integers before every operation would erase referent compatibility and target stride from the public IR. A separate subscript instruction adds no information because the frontend has already reduced both C spellings to ordinary pointer operations. Restricting code generation to x86 scale factors would fail for active and vendored C that points at objects of other complete target sizes.

## Consequences and evidence

The primary IR contract publishes 113 exact instructions across sixteen functions. It covers pointer addition in both operand orders, pointer subtraction, compatible qualified pointer difference, both subscript orders, linked array decay, all four prefix and postfix updates, and both pointer compound assignments. Two fixtures reproduce the ATA read and write shapes with a two-byte `uint16_t` stride and the constant offset 256. A three-instruction qualified-array contract proves that qualification reaches the element pointer. A frozen decay with missing result qualification fails transactionally. A nine-instruction volatile-pointer contract proves one address, one load, one store, and postfix recovery.

The deterministic ELF32 object contains nineteen functions in 811 exact text bytes. Its twenty-one symbols include one sixteen-byte BSS array. Two `R_386_32` relocations target that array at text offsets 373 and 384. Shared x86 decoding finds thirteen additions, seven subtractions, seventeen multiplications, two signed divisions, and nineteen returns. Repeated emission is byte-identical and leaves the frozen unit and operation arena unchanged.

Negative contracts keep atomic pointer updates and wide pointer offsets at the unsupported-type boundary. A frozen pointer compound assignment changed to multiplication receives the invalid-unit diagnostic. Each failure preserves the input unit and publishes no partial IR.

The first positive IR run stopped on the new prefix update with the public unsupported-type diagnostic. The first object run reached the same boundary. After pointer mutation lowering was added, both runs reached their exact inventory checks. This confirmed that the frontend already carried the required semantics and that the missing work was confined to IR and emission.

This decision covers the hosted bootstrap path only. GCC or Clang still builds the shared frontend, IR, emitter, x86, ELF32, and contracts. No normal Cupid OS C object uses this path, no production artifact changes, and no boot result is claimed.
