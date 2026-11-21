# ADR 0472: Use declared halfword storage in private CupidC

Date: 2026-10-12

Status: Applied to the engineering working tree; bounded final engineering acceptance passes

Private CupidC needs real two-byte `short`, `unsigned short`, `I16` and `U16`
objects for active network ports and array elements. Use distinct signed and
unsigned halfword types for storage, loads, casts, assignments and results.
Keep four-byte pointers and cdecl argument slots, default promotion to `int`
and the existing unsigned-word widening behavior.

Append the new type values to preserve earlier enum identities. Halfword
objects have two-byte size and alignment. Pointer arithmetic follows the
declared element width, and array descent retains the remaining row shape
for `sizeof`. Named native calls, record callbacks and class methods normalize
the low sixteen result bits by signedness, including when the callee leaves
unrelated bits in upper EAX. Fixed signatures cover `htons`, `ntohs`, `bind`,
`connect`, `accept`, `sendto`, `recvfrom` and `udp_send_raw`; peer-port outputs
write two bytes through `U16 *`.

Use one typed constant-list helper for global, block-static and persistent REPL
integer arrays. It reserves zeroed storage and writes the declared one-, two-
or four-byte little-endian elements. Partial lists retain omitted zeros. The
existing constant grammar accepts character constants, escapes, arithmetic
constants and trailing commas. Array typedefs follow the same path. Block-static
array data initializes once. Excess elements, nonconstant calls and division
by zero publish no declaration. Persistent REPL failures restore earlier data
and names and permit reuse of the rejected name.

Four-byte word aliases have the wrong storage and stride for these declarations.
Skipping initializer braces discards declared data. A separate list parser
for each lifetime would duplicate width, bound and constant-expression rules.

Flat constant integer lists are the qualified scope. Nested lists, inferred
bounds, pointer difference and indirect integer updates remain separate work.
This array work does not qualify general scalar or aggregate initializer
semantics or change the recorded scalar-string and block-static scalar limits.
It preserves the hosted static-conversion rules in
[ADR 0254](0254-convert-static-integers-and-long-double.md) and the private
floating-update rules in [ADR 0273](0273-update-private-derived-floating-lvalues.md).

[The qualification record](../bootstrap/PRIVATE-INTEGER-ARRAY-QUALIFICATION.md)
retains positive and rejection tests, native ABI oracles, complete outputs,
actual runtime results and both failed ordinary Linux discovery attempts. The
Linux runtime retains its diagnostic-image provenance; ordinary R3 separately
qualifies identical image bytes. Connected peer and successful packet-transfer
coverage remain separate from the checked guest rejection paths.

The complete original forty-method frontier modules pass on both hosts. Their
retained archive and exact reviewed source maps authorize the completed private
compiler-source and measured-lock working-tree adoption. The protected
baseline remains failed. The separately reviewed reversible seed transition
is applied; it preserves the production release comparison and all original
test assertions. Final engineering acceptance passes in the distinct host scopes below;
source and documentation closure still precedes a coherent commit. Earlier failures, source cohorts
and original deadlines remain recorded.

Windows acceptance combines eight retained build/guest command results with
one fresh original 3,526-method suite, including its original 72 skips.
Linux acceptance covers a fresh forced-native normal rebuild and guest
checks; no complete Linux suite is claimed. See the [final five-test proof](../bootstrap/evidence/verification-final-root-combined-five-test-acceptance-20261013.json.gz).
