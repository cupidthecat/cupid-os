# Retain GNU used entities in CupidC

- Status: Accepted
- Date: 2026-07-26

## Context

The normal two-pass kernel link generates `kernel/cpu/ksyms_data.c`. Its
symbol blob uses the same declaration shape on every build:

```c
const unsigned char
__attribute__((section(".ksyms"), used, aligned(4)))
ksym_blob[] = {
    /* generated symbol bytes */
};
```

Compiler head already represented the named section and alignment, but it
stopped at `used`. The checked seed therefore could not compile this
generated root. Removing the attribute or teaching the generator a
CupidC-only spelling would have hidden a source requirement instead of
implementing it.

GNU `used` says that a definition must remain in the object even when the
translation unit has no ordinary reference to it. CupidC currently emits
every represented file-scope definition. The missing work was still
semantic: the frontend had to retain the declaration fact, and later
boundaries had to reject forged or misplaced metadata.

## Decision

In GNU mode, CupidC accepts `used` and `__used__` on file-scope object and
function declarations. Compatible redeclarations merge the flag into the
canonical binding, regardless of which declaration carries it. Repeated
spellings are harmless, and `used` may appear with `unused`.

The attribute takes no arguments. Record types, record members, typedefs,
parameters, block objects, and block function declarations remain invalid
placements. GNU-disabled source receives the usual attribute diagnostic.

Linear IR validates that a frozen `used` binding has a valid type, names an
object or function, and is visible at file scope. Object emission repeats the
same validation before reading the binding. Invalid frozen metadata leaves
the input unchanged, publishes no partial output, and permits a later
operation in the same job.

The flag does not add an IR instruction or alter current ELF32 bytes. CupidC
already emits every represented definition, including unreferenced local
objects and functions. Keeping the fact in canonical metadata gives a later
dead-code or section-elimination pass enough information to preserve the
source contract.

## Rejected alternatives

Skipping `used` after parsing was rejected. That would accept the spelling
while discarding its only meaning.

Removing the attribute from the symbol generator was rejected. The generated
source expresses why `.ksyms` must survive, and other active source uses the
same GNU contract.

Special-casing `ksym_blob` in the object writer was rejected. Entity metadata
is reusable and keeps the object path independent of one generated symbol
name.

Making the permanent contract depend on an ignored generated file was
rejected. Clean checkouts do not contain that build artifact. The hermetic
fixture keeps the exact declaration shape, while a separate compiler-head
probe covers the current generated source.

## Consequences and evidence

Strict Clang builds of the frontend, Linear IR, and object contract binaries
pass. Their `used-attributes` selectors pass independently. The frontend
contract covers both spellings, canonical redeclaration merging, repeated
attributes, combination with `unused`, GNU-disabled input, invalid arguments,
invalid placements, and recovery.

The Linear IR contract accepts valid public metadata without changing
function IR. It rejects `used` on a typedef, an invalid entity type, and an
entity hidden from file scope, then lowers the original unit again.

The object contract emits attributed and unattributed fixtures to identical
ELF32 bytes. It confirms that the local object and function remain defined,
checks the same forged metadata failures, and reproduces the original object
after each failure. Its hermetic generated-source fixture emits a
four-byte-aligned allocated `.ksyms` section, a matching `ksym_blob_size`
object, no relocation, and no required `.text` section. Repeated emission is
byte-identical.

A compiler-head probe also compiles the current 621,273-byte generated
`kernel/cpu/ksyms_data.c` source twice under the complete `KERNEL_I386`
profile. Both 101,808-byte ELF32 objects have SHA-256
`802b604aa24261b48251a537c011e7d81839fab67fbe3c7491e7991ad4797ae3`.
The complete toolchain contract target passes from the exact staged source
tree. That run includes the frontend, Linear IR, object, self-host, active
source, and static hosted-tool checks.

ADR 0122 moves `used` into the checked seed. The normal Make graph still
compiles the generated root with GCC or Clang. Production-wrapper ownership,
the full image build, and runtime proof remain separate work.
