# ADR 0332: Publish fixed signatures for private kernel bindings

## Status

Accepted on 2026-08-24.

## Context

Private CupidC recorded each native kernel binding's result type and an
advisory parameter count. Calls through `SYM_KERNEL` still used the source
expression width because the binding did not retain fixed parameter types,
record identities, prototype state, or the variadic boundary. This disagreed
with the typed call path already used by direct functions and retained
function pointers. A call such as `sqrt(9)` therefore placed one integer word
where `double sqrt(double)` expects eight bytes.

## Decision

Publish reviewed native binding ABIs with the existing
`cc_function_pointer_signature_t` representation. The public registration
function validates the result, fixed parameters, record identities, callback
handles, prototype state, variadic state, and bounded parameter count before
it adds a symbol. A failed registration leaves the symbol table unchanged.
Unreviewed entries use the explicitly named legacy result-only function.

A typed `SYM_KERNEL` call now uses the same fixed argument conversion, cdecl
slot layout, exact cleanup, arity diagnostics, variadic default promotions,
and result channel as the other typed call paths. The first reviewed cohort
contains console, string, and port helpers plus all 50 `libm` bindings. This
includes `double sqrt(double)` and the matching `float` function.

## Evidence

The red public test driver could not link because the registration function
did not exist. After the implementation, JIT and fixed-address AOT cases pass
integer values to `double` and `float`, widen `float` to `double`, lay out an
`int, double, float, int` call, and promote a variadic `char` and `float`.
Negative cases reject too few or too many arguments and an incompatible
argument type. Descriptor tests reject an invalid type and 33 fixed
parameters, preserve the symbol count, and register a valid binding in the
same state.

The private call ABI, binding table, and GUI modules pass 450 tests in 110.266
seconds while the audit checker runs in parallel. The unchanged feature-13
source compiles through the same test driver, whose `sqrt` binding now carries
`double(double)`. Checked-seed CupidC
builds `kernel/lang/cupidc_parse.o` and `kernel/lang/cupidc.o`. Their sizes are
504,980 and 277,760 bytes, with SHA-256 values
`be724c126e0aa70f5bc618396a6444ccbee0691132d62a4cfa8335bbd14bbf7f`
and
`d07d4f8bae7db939032a34e8b35cc64522975ef9c09db67828c2f006e59899bc`.

The regenerated active-source audit reproduces exactly through
`make check-bootstrap-audit`. Two normal builds compile the complete kernel and
Doom cohorts and finish both CupidLD passes. The checked CupidDis production
scan then reaches its fixed 300-second process timeout in both runs without a
code diagnostic. The policy remains unchanged. No raw kernel is published, so
this boundary records no new artifact size or four-vCPU image result.

## Consequences

Reviewed native bindings no longer depend on a source expression already
having the ABI width expected by the kernel function. The remaining entries
keep their previous result handling and source-width arguments through the
legacy path until each declaration is reviewed.

Nested callback handles can pass through the public descriptor, but no active
kernel binding publishes one at this boundary. Aggregate results and
unreviewed binding parameters remain outside the typed cohort. This private
compiler change does not alter the standalone checked seeds or transfer a
production source owner. `TempleOS/` remains read-only reference material.
