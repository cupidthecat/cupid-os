# ADR 0141: Retain CupidC function code generation attributes

## Status

Accepted on 2026-07-27.

## Context

The unchanged `kernel/cpu/fpu.c` source uses
`target("general-regs-only")` on `fpu_init_cpu()`. That function must set
CR0, CR4, the x87 state, and MXCSR before ordinary compiler-generated
floating code can run on a logical CPU. The active tree also has 18
`noinline` uses. Compiler head previously rejected both attributes before it
could inspect the function body.

Ignoring these attributes would make the parser appear compatible while
dropping code generation requirements. The shared frontend, Linear IR, and
ELF32 emitter need one canonical fact that survives compatible
redeclarations and fails closed if a later phase receives forged metadata.

## Decision

Accept GNU `noinline`, `__noinline__`, `target`, and `__target__` on
file-scope function declarations in GNU mode. `noinline` takes no argument.
The first target option is deliberately narrow:
`target("general-regs-only")`. Unknown options and invalid placements remain
unsupported.

Merge both facts into the canonical function binding across compatible
redeclarations. `noinline` preserves the source request for a future inliner.
It does not change current object bytes because CupidC does not inline
functions. Each published IR function copies the canonical code generation
mask so a later optimizer does not need to reconstruct declaration policy.

For `general-regs-only`, inspect the completed function's Linear IR before
publication. Reject compiler-generated floating instructions, floating
values, and floating call arguments. Repeat the check at the emitter
boundary and require the IR mask to match its canonical binding, so frozen or
forged input cannot bypass the rule. Explicit source assembly remains
governed by its own typed contracts. This permits the
required `FNINIT` instruction without treating arbitrary assembly as
compiler-generated floating code.

## Evidence

Dedicated frontend, IR, and object selectors cover both GNU spellings,
redeclaration order, canonical metadata, exact target parsing, invalid
arguments and placements, GNU-disabled use, forged metadata, rollback, and
same-job recovery. The IR selector accepts an integer-only target function
with explicit `FNINIT` and rejects a target function that returns `float`.
The object selector emits the attributed and plain fixtures twice, requires
byte-identical objects, and finds exactly one `DB E3` instruction inside the
target function.

The complete frontend suite passes 72 tests. The complete IR suite passes 60
tests. A final partition covers all 72 object tests: 70 cases pass together,
the enlarged compiler implementation reproduction passes with its two
Cupid-built compiles allowed 180 seconds each, and the complete static
stage-two to stage-three fixed point passes in 709.072 seconds. The focused
object selector and active self-host object frontier also pass.
The current Toolchain source gates report:

| Source | Definitions | Statements | Expressions | Block bindings | Initializers |
| --- | ---: | ---: | ---: | ---: | ---: |
| `toolchain/cupidc_frontend.cc` | 338 | 13,895 | 91,398 | 2,054 | 1,347 |
| `toolchain/cupidc_ir.cc` | 214 | 6,456 | 58,656 | 827 | 302 |
| `toolchain/cupidc_emit.cc` | 227 | 6,039 | 52,094 | 739 | 370 |

An exact kernel-profile compile of unchanged `kernel/cpu/fpu.c` now passes
the target attribute. It stops at line 28 on the independent `"m"(mxcsr)`
input to `ldmxcsr`, with the diagnostic `GNU inline assembly input
constraint is outside this slice`.

## Rejected alternatives

Skipping the attributes was rejected because it would discard a machine
state safety requirement.

Treating `target` as an opaque string was rejected because unsupported
options would silently claim semantics the compiler does not enforce.

Rejecting explicit `FNINIT` inside a general-register-only function was
rejected because the source instruction is intentional and already passes a
separate assembly contract.

Rewriting `fpu_init_cpu()` or removing its setup work was rejected. The
compiler must grow to represent the operating system source.

## Consequences

Compiler head now owns the semantic declaration and code generation boundary
for `noinline` and `target("general-regs-only")`. The checked seed does not
yet contain this capability, so no production source changes owner and no
`.c` file is renamed in this decision.

`kernel/cpu/fpu.c` remains host-built until CupidC represents the independent
memory input used by `ldmxcsr` and the rest of the root compiles through the
checked path. No kernel object, image, ABI, or runtime behavior changes here.
