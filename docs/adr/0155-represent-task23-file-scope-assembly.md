# ADR 0155: Represent the Task 23 file-scope assembly wrappers

## Status

Accepted on 2026-07-28.

## Context

Unchanged `kernel/cpu/libm.c` begins with twelve public wrappers written as
GNU file-scope basic assembly. They provide `sqrt`, `sqrtf`, `sin`, `sinf`,
`cos`, `cosf`, `tan`, `tanf`, `atan`, `atanf`, `atan2`, and `atan2f`.
Compiler-head CupidC previously stopped at the first `__asm__` on line 25
because declaration parsing expected another C type.

Treating the blocks as statement assembly would lose their translation-unit
placement and symbol definitions. Sending the text to GAS would add a hidden
host-tool dependency. Rewriting the wrappers as C would also discard their
exact ABI and x87/SSE instruction sequences instead of extending CupidC for
the active source.

## Decision

GNU mode accepts file-scope `asm`, `__asm`, and `__asm__` in the basic
string form. Adjacent ordinary narrow strings are joined. The frontend owns
the decoded templates in a translation-unit table separate from function
statement assembly. A record has basic, implicitly volatile semantics and no
operands. The `volatile`, `goto`, and `inline` modifiers, extended operands,
empty templates, embedded nulls, and GNU-disabled use fail during parsing.

Linear IR publishes a source-ordered table of references to those frontend
records. It validates the table shape, flags, template storage, and empty
operand slices before lowering any function. File-scope assembly is a unit
effect, not an `ASSEMBLY` instruction inside a function.

The i386 emitter recognizes only the twelve exact wrappers used at the start
of `libm.c`. Each template must match one visible external declaration with
the expected one- or two-argument floating prototype. The emitter writes a
prologue-free global `STT_FUNC` symbol in source order and obtains every
instruction from the shared x86 encoder. It does not invoke a host assembler
or parse general GAS syntax. Any other file-scope template remains a precise
unsupported-emission error.

## Evidence

Frontend contracts cover all three accepted spellings, adjacent strings,
ordering beside ordinary declarations, table separation, and same-job
recovery. Negative cases cover disabled GNU mode, the `volatile`, `goto`, and
`inline` modifiers, extended operands, missing or blank strings, embedded
nulls, and malformed syntax.

The Linear IR fixture retains two ordered references and stays byte-identical
across repeated lowering. Frozen-unit mutations cover a missing table,
a nonzero operand-table reference, invalid flags, a nonempty operand slice,
absent or blank templates, and recovery.

The object fixture copies the twelve active templates and matching
prototypes. It emits exactly 248 text bytes with these symbol spans:

| Symbol | Offset | Bytes |
| --- | ---: | ---: |
| `sqrt` | 0 | 11 |
| `sqrtf` | 11 | 11 |
| `sin` | 22 | 21 |
| `sinf` | 43 | 21 |
| `cos` | 64 | 21 |
| `cosf` | 85 | 21 |
| `tan` | 106 | 23 |
| `tanf` | 129 | 23 |
| `atan` | 152 | 23 |
| `atanf` | 175 | 23 |
| `atan2` | 198 | 25 |
| `atan2f` | 223 | 25 |

All twelve symbols are global functions, and the object has no relocations.
The expected bytes were measured from the unchanged source with the native
oracle, then fixed in the Cupid object contract. Repeated emission is
identical. An unsupported later template fails without publishing an object,
and the same job then reproduces the valid object.

The exact `KERNEL_I386` source check fixes unchanged `libm.c` at 43,736 bytes,
1,500 lines, and SHA-256
`f1c13c83b758394189cc74ed6addfd9dfa99d42064c349c548476686b26cabce`.
Two compiler-head runs now pass all file-scope blocks and stop at line 782,
where a function-body GNU assembly statement uses named operands. Both runs
produce the same diagnostic and no partial object.

## Rejected alternatives

Passing opaque templates to GAS was rejected because self-hosting requires
Cupid tooling to own target bytes and diagnostics.

Turning every file-scope template into an artificial statement or C function
was rejected because either representation would lie about source placement
and ABI ownership.

Adding a general GAS parser in this increment was rejected because the
active requirement is twelve small, exact definitions. The public frontend
and IR representation leaves room for later file-scope forms without
claiming that arbitrary assembly is understood.

## Consequences

Compiler-head CupidC now represents file-scope assembly as a distinct
translation-unit effect and can emit the first twelve `libm.c` wrappers
without GCC, GAS, or NASM. The next exact source blocker is named
function-body assembly operands at line 782.

The checked bootstrap seed predates this support. `libm.c` remains
host-owned and keeps its `.c` name. No normal build recipe, production object
owner, ABI, runtime path, or host dependency changes.
