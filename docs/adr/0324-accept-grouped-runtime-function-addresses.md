# ADR 0324: Accept grouped runtime function addresses

## Status

Accepted on 2026-08-22.

## Context

Private CupidC already accepted `&function`, `(&function)`, and grouped
function names in static callback initializers. The runtime expression parser
still required an identifier immediately after `&`. A valid automatic
initializer such as `int (*callback)(int) = &(target);` therefore failed with
`expected variable after &`. The mismatch also affected later checked
assignment and both private JIT and fixed-address AOT compilation.

## Decision

After unary `&`, accept one or more opening parentheses around a direct
identifier and require the same number of closing parentheses. A function or
kernel function then enters the existing typed address path, so its result,
parameters, record identities, prototype state, and variadic boundary remain
available to callback initialization and assignment.

The same parsing rule accepts a grouped direct object name. Member and indexed
designators still use their existing ungrouped address paths; this change does
not attempt to parse an arbitrary expression after `&`.

## Evidence

The new JIT and AOT contract initializes a callback with `&(ready_target)`,
reassigns it with `&((later_target))`, calls both targets, and checks the result.
A mismatched grouped target still reports the function-pointer signature error.
The compiler then accepts `&(right)` and runs it in the same restored state.

Before the parser change, these cases failed with `expected variable after &`.
After the change, the complete private callback ABI module passes all 273 tests
in 48.557 seconds.

## Consequences

Private CupidC now treats grouped runtime function addresses like plain
function designators and ungrouped function addresses. Static initialization,
automatic initialization, checked assignment, forward patches, JIT, and AOT
keep one compatibility rule.

Computed address operands, grouped member or indexed designators, raw callback
fields, callback arrays, block-static callbacks, and direct postfix field calls
remain separate work. The standalone compiler seeds do not contain this private
parser. No build owner or host dependency changes. `TempleOS/` remains
read-only reference material.
