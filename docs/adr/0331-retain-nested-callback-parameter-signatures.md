# ADR 0331: Retain nested callback-parameter signatures

## Status

Accepted on 2026-08-24.

## Context

Private CupidC retained the signature of a named function pointer, but a
callback-valued parameter inside that signature was reduced to a generic
four-byte function pointer. The compiler could lay out the outer call, yet it
could not check the inner callback's result, parameters, record identities, or
variadic boundary. Raw and typedef spellings of the same nested type could not
be compared as one type graph.

Active kernel source needs that distinction. `p_icon_set_drawer` in
`kernel/lang/cupidc.cc` points to `gfx2d_icon_set_custom_drawer`. The target's
second parameter is `void (*drawer)(int, int)`. Preserving only its address
width would accept a callback with a different prototype even though the
source declares the complete interface.

## Decision

Parse callback-valued parameters recursively and intern their signatures in
the existing callback side tables. A raw nested signature uses the bounded raw
pool, which holds 32 entries. A nested signature declared through a direct
file-scope callback typedef keeps its existing typedef handle. Together, the
sixteen typedef entries and 32 raw entries form a 48-handle domain.

For a `TYPE_FUNC_PTR` parameter, store the nested signature handle in the
existing `param_struct_indices` element. That element continues to hold record
identity for a `TYPE_STRUCT_PTR` parameter. No symbol, argument, or object grows
as a result. A callback value still occupies one four-byte i386 cdecl slot, and
the outer call keeps its existing layout and cleanup rules.

Compare retained callback graphs structurally across raw and typedef handles.
The comparison checks the result type and result record identity. At each
prototyped level, it also checks fixed parameter types, parameter record
identities, and the variadic boundary. Exact declaration equality requires the
same prototype state, while compatible uses retain the existing unprototyped
rule. A callback-valued parameter descends through its retained handle. Each
comparison memoizes handle pairs across the 48-handle domain so repeated
subgraphs do not cause repeated descent.

Limit parsing and graph validation to 16 nested callback levels. Source beyond
that bound receives `function-pointer signature nesting is too deep`. Keep the
32-entry raw-signature limit and its existing diagnostic. Program and REPL
transactions restore the raw pool after a failed declaration, comparison, or
unresolved source, so rejected nested graphs do not consume later capacity.
When a nested callback declarator is followed by its parameter list, require
exactly one `*`. A pointer-to-function-pointer `**` form receives a direct
diagnostic instead of losing one level of indirection.

This rule applies to private in-kernel CupidC. Checked-seed hosted CupidC still
compiles the production kernel sources, and the standalone checked seeds do
not contain this parser.

## Evidence

The private callback ABI tests pin the active `p_icon_set_drawer` declaration,
the public `gfx2d_icon_set_custom_drawer` declaration, and its matching
definition. JIT and fixed-address AOT cases pass a typed drawer callback through
the outer function pointer and call it with the declared two-integer signature.

Other cases compare equivalent raw and typedef-backed nested graphs. They
exercise nested record-pointer results and parameters, variadic callbacks, and
multiple callback levels. Mismatch cases reject a different nested result,
parameter list, record identity, or variadic boundary, then compile and run a
valid source in the same compiler state.

Bounded tests accept the supported nesting limit and reject a deeper graph.
Capacity tests fill the raw pool, reject the next distinct nested signature,
and recover for another compile. A persistent REPL case fails after allocating
nested signature records, restores the pool, and then uses the complete
capacity without stale metadata.

Declaration tests distinguish an unprototyped nested `()` signature from a
prototyped nested signature. A separate negative rejects `**`, then compiles
and runs a valid callback in the same compiler state.

At the accepted source head, the private callback ABI module passes all 310
tests in 75.017 seconds. The final `make -j4 all` run compiles the full kernel
and Doom cohort, links both kernel passes with CupidLD, passes the CupidDis
object and strict linked-image gates, accepts all 14 exact artifact sizes, and
publishes `cupidos.img`. A private four-vCPU frontier boot prints
`[feature14-callback-nested] PASS outer=1 inner=1 value=43`, then the overall
feature pass and clean JIT completion. Its 157,520-byte log has SHA-256
`b34a68aebdfecaeeb347c1ff4764cbe609a6ed2f154557a15133a601101585c6`.

## Rejected alternatives

Keeping only the four-byte nested slot was rejected because slot width cannot
prove that the supplied callback has the declared ABI.

A second recursive type representation was rejected. Handles in
`param_struct_indices` extend the existing bounded signature model without
changing symbol layout or call emission.

Rewriting the active binding around a cast or an untyped pointer was rejected.
The source already carries the inner callback prototype, and the compiler can
retain it directly.

Publishing nested signatures through the `BIND` table is separate work. This
decision covers the C declarator and compatibility paths without changing the
binding metadata format.

## Consequences

Private CupidC retains and checks callback-valued parameters through raw and
direct-typedef signature graphs. The active icon-drawer declaration compiles
without a source workaround, and compatible JIT and AOT calls keep the existing
i386 ABI.

Callback-valued results, pointer-to-function-pointer `**` declarators,
callback alias chains, and `BIND` metadata publication remain outside this
boundary. The 16-level nesting limit and 32-entry raw pool remain explicit
compiler limits.

This change moves no production build owner and adds no host dependency. It
does not change the object format, checked seeds, guest ABI, or source suffix
ownership. `TempleOS/` remains read-only reference material.
