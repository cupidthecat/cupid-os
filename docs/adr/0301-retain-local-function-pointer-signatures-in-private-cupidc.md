# ADR 0301: Retain local function-pointer signatures in private CupidC

## Status

Accepted on 2026-08-20.

## Context

Private CupidC can transport fixed scalar and SIMD arguments through direct
calls, but its block-local function-pointer declarator used to skip the entire
parameter list. An indirect call therefore used each expression's source width
instead of the declared parameter type and assumed an integer result. It could
not apply fixed conversions, enforce arity, or carry a typed SIMD value through
an ordinary callback.

Active Cupid OS code shows why the signature matters. The ISO9660 SUSP walker
passes an eight-bit entry length to a callback declared with a 32-bit length,
and the kernel, Doom, and Toolchain sources use fixed callback interfaces. Most
of those active declarations currently reach the private parser through
typedefs or parameters, which still erase their signatures. A useful first
step is to retain the signature where the private parser already recognizes a
named local `T (*name)(...)` declaration.

## Decision

Parse and retain the declared result type, fixed parameter types, prototype
state, and variadic state on a named block-local function-pointer symbol.
Treat `()` as the existing unprototyped form and `(void)` as a complete
zero-parameter prototype.

Represent each outer parameter by the ABI type needed for its slot. Pointer
parameters remain one word. A nested callback parameter becomes
`TYPE_FUNC_PTR`, and a pointer-to-array parameter becomes an ordinary pointer.
The nested signature and array extent do not affect the outer call layout, so
they are consumed without pretending that the private type system retains
them.

Route a signature-bearing indirect call through the same fixed cdecl coercion
and slot layout as a direct call. Four-byte scalar and pointer values,
eight-byte `double`, and 16-byte `float4` or `double2` values keep their
existing representations. A fixed argument is converted to its declared type
before the word permutation. A variadic tail still applies the default
`float` and `char` promotions. The call must satisfy its fixed arity.

Publish the declared result after the indirect call. Integer and pointer
results stay in EAX. `float`, `double`, `float4`, and `double2` results use
XMM0 through the existing result path. A typed SIMD parameter therefore uses
one complete 16-byte slot rather than the old metadata-free rejection.

Keep erased calls unchanged. A `void *` callee, an empty `()` declaration,
and any pointer whose signature did not reach its symbol continue to use
source-width arguments and the established SIMD diagnostics.

Before storing a plain function designator in a typed local pointer, compare
the declared result, record-pointer identity, fixed parameter list, and
variadic boundary with the target's retained signature. Reject a mismatch
before the declaration can make an indirect call use the wrong stack or
result channel. Record-pointer parameters retain their record identity as well
as their four-byte slot type. A second named local callback is checked through
the same metadata. Grouping parentheses do not hide a designator. A
conditional expression over compatible named callbacks retains every arm.
CupidC validates the complete candidate set before it infers or refines any
provisional signature, so one bad arm cannot leave partial metadata behind.

If the target has only a prescan entry, use the local declaration as a
provisional signature and check it when the real declaration or definition is
parsed. Patch an undefined function's absolute address alongside the existing
relative-call fixups. This makes a prototype-first or prescan-only later
definition usable in JIT and AOT output without storing zero.

Accept a represented integer constant expression that evaluates to zero as a
null pointer constant. The represented forms include unary signs, integer
casts, arithmetic, character zero, and `sizeof(int) - 4`. A conditional keeps
that proof only when every required arm is an integer constant expression.
Reject floating values, nonzero integers, mutable enum storage, object
pointers, and other initializer expressions whose callable type cannot be
proved. An explicit cast through `void *` remains the deliberate escape. In a
conditional, every non-null object-pointer arm must pass through that cast.
Casts used only in a condition, subscript, `sizeof`, or another child
expression do not erase the selected value's type.

Treat signature parsing and forward fixups as transactions. A failed function
or method restores emitted code and data, patch tables, inferred signatures,
entry state, labels, control nesting, and statement nesting. A failed complete
source also restores every pre-existing symbol it touched, including prior
prototypes, definitions, kernel bindings, and a reused implicit `__start`.
Only patches added by that source are resolved or discarded. New symbols and
all other emitted state are rewound to the source checkpoint. The implicit
`__start` symbol has a complete `void(void)` signature, so it cannot be mistaken
for a prescan placeholder or retagged by an incompatible callback.

## Evidence

The private call-ABI module passes all 217 tests in 40.806 seconds. JIT and
AOT cases cover integer-to-`double`, `float`-to-`int`, a `double` result,
exact 12-byte cleanup, a variadic fixed prefix and promoted tail, a nested
callback parameter, a pointer-to-array parameter, and the active ISO9660
callback widths. Typed `float4` arguments and results cross the indirect call
through the existing 16-byte slot and XMM0 channel.

Negative cases reject mismatched fixed types, too few arguments, and too many
arguments, then compile and run a valid program in the same compiler state.
Initializer contracts also reject a different parameter type, result channel,
record-pointer identity, or variadic boundary. A separate runtime case proves
that an explicit `void *` cast still opts into signature erasure. Later
function addresses, provisional definitions, callback copies, compatible
conditional selection, represented integer constant zero, and explicit
erasure have positive coverage. Every callback arm is constrained, including
targets known only to the prescan. Null arms are neutral. Erasure survives a
conditional only when every possible non-null object pointer was explicitly
cast. Malformed later declarations and failed functions, methods, or complete
sources recover without leaking symbols, control state, or forward patches.
The tests cover prior prototypes, prior definitions, kernel bindings, and the
implicit `__start` thunk across same-state retries. Non-callable scalar,
enum-storage, and object values are rejected before a stale register can be
stored. The existing erased-pointer and unprototyped SIMD cases remain focused
rejections.

The checked kernel compile suite passes all 35 tests in 97.926 seconds. The
checked Linux CupidC seed compiles the final parser in 74.531 seconds to a
450,176-byte ELF32 object with SHA-256
`93aed3434532b1c2db297165cbcc8a8e7d70e253341eb91d6be6d3534737d260`.
The broader two-pass frontier targets 156 approved sources and 312 checked
compilations. Its rerun exceeded the 2,340-second command limit without a
compiler diagnostic, so this is not a completed deterministic-frontier proof.

Review found that the first callback artifacts and guest log predated the
settled CTXT text, so they were discarded. A pre-publication rebuild was also
stopped when a second embedded manual still carried self-referential artifact
values. Neither intermediate checkpoint is acceptance evidence.

The settled poisoned-host `make -j4 all` reached the exact artifact-size gate
in 690.910 seconds. It reported only the expected pass-one ELF and raw-kernel
changes; the final ELF size remained exact. All 38 artifact-size policy and
semantic-contract tests passed in 2.650 seconds, with two Windows replacement
cases skipped because pinned handles already deny the operation. The repeated
poisoned build passed in 692.768 seconds and accepted all nine artifacts. The
final pass-one ELF, ELF, and raw kernel are 9,299,616, 9,422,496, and 9,202,060
bytes.

The four-vCPU e1000 guest smoke passed in 66.095 seconds. The in-OS compiler ran
`/bin/feature14_simd.cc` and printed
`[feature14-callback] PASS float4=4 double2=2 calls=2` before the overall
feature result and JIT completion marker. The 31,408-byte serial log has
SHA-256
`27bb7ea972ef0ca034f09c47a91d9566cc571a5f1d9d113ff639c742f07454fd`.
The private-image run left the original 200 MiB image unchanged at SHA-256
`fb79586e6bc9aaa998ef248265d5bc3eaf43524ffed8b1c96e71affb96d0460a`.

## Rejected alternatives

Do not keep lowering every indirect call from expression source widths. That
disagrees with its prototype and can leave the caller and callee using
different stack layouts.

Do not special-case SIMD callback names. The declared signature should drive
the same general ABI path used by every fixed parameter.

Do not treat `()` as `(void)`. The first is the private compiler's existing
unprototyped form; changing it would silently impose an arity rule on old
source.

Do not claim that nested callback signatures are fully represented. This
increment needs their four-byte outer slot, not a recursive private type graph.

Do not trust the pointer declaration while ignoring a plain target's type.
Once that declaration controls conversion and result handling, a mismatched
initializer would turn useful metadata into an ABI bug.

## Consequences

Private JIT and AOT programs can compose ordinary named local callbacks with
fixed scalar, floating, pointer, and SIMD parameters and results. Their calls
reuse one cdecl implementation instead of maintaining an indirect width rule.

Typedef signatures, global function-pointer declarations, function-pointer
parameters, record fields, later pointer assignments, copied signature
metadata beyond named local initialization, recursive nested-signature
checking, and aggregate results remain open. Compatible conditional selection
is represented, but arbitrary computed callback expressions still need a
richer expression type. Those gaps block the active ISO9660, Doom, and
Toolchain callback shapes from serving as end-to-end private-compiler evidence.

This changes no normal-build owner, host dependency, or checked seed. The
larger parser changes the three deterministic kernel artifact sizes recorded
above. The production parser source already uses `.cc`, so no suffix rename is
due. `TempleOS/` remains untouched reference material.
