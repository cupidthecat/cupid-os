# Retain explicit function addresses in private callbacks

## Context

Private CupidC already retained a callback signature when a plain function
designator initialized or was assigned to a signature-bearing function
pointer. The equivalent C spelling `&function` reached the ordinary object
address path instead. That path rejected functions, so adding an explicit
address operator broke otherwise valid callback declarations.

File-scope callback initialization has a separate constant-data parser. It
recognized plain and grouped function designators but did not recognize the
explicit address form. The runtime expression parser and the constant-data
parser therefore needed the same rule.

## Decision

Treat the direct address of a named function as the same four-byte callable
value as its plain designator. Preserve the function's result, parameter,
record identity, prototype, and variadic metadata. The rule applies to private
JIT and fixed-address AOT compilation.

The constant-data path accepts one explicit address operator around a defined
or later-defined function name. Parentheses may surround either the complete
expression or the function name. Defined targets are written to the data slot
immediately. Later targets use the existing absolute data patch.

The runtime expression path emits the same immediate address or forward code
patch for `function` and `&function`. Automatic initialization and checked
plain assignment then reuse the existing callback compatibility checks. The
function-address emitter is shared by both spellings so their patch behavior
cannot drift.

## Evidence

Focused tests run raw callback file objects through `(&ready_target)` and
`&(later_target)`. A raw automatic callback starts from `&ready_target`, is
assigned `&later_target`, and makes a typed indirect call. The same program
runs through JIT and fixed-address AOT and returns the expected value.

A negative test initializes an incompatible callback from `&wrong`. It checks
the result-type diagnostic, retries in the same compiler state, and executes a
valid addressed callback. This proves that the new spelling uses the existing
signature transaction instead of bypassing it.

## Rejected alternatives

Stripping `&` before compilation was rejected because it would create a second
source rewrite rule outside the parser and would not cover runtime assignment.

Treating `&function` as an untyped integer was rejected because it would erase
the signature that makes indirect argument conversion and arity checking safe.

## Consequences

Private callback objects may use a plain function designator or its explicit
address. Conditional callback initializers, callback fields and arrays,
block-static callbacks, alias chains, computed callback values, raw Cupid class
method parameters, and recursive callback signatures remain outside this
boundary.

The standalone compiler seeds do not contain this private parser, so this
change does not alter bootstrap seed identity or production tool ownership.
`TempleOS/` remains read-only reference material.
