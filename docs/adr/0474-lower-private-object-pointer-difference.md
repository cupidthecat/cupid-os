# ADR 0474: Lower private object-pointer difference

Status: Implemented; native source checks and Windows OS/JIT/AOT guest qualification pass.

Private CupidC previously sent pointer subtraction through ordinary integer
subtraction. Halfword and larger element types therefore returned byte counts,
and incompatible referents could pass without a diagnostic.

Use the expression parser's retained type, record identity, stride and row-shape
metadata to validate complete compatible referents. Evaluate each operand once,
subtract addresses and divide by the signed element size. Unit strides require
no division. The result is a signed four-byte integer and clears array metadata.
Keep erased void pointers outside this operation because they have no complete
pointed-to object type. Retain the existing pointer-plus-integer behavior.

This uses the same signed target arithmetic principle as hosted pointer
lowering in ADR 0042. It does not introduce a new IR or change the hosted
compiler. General pointer-to-array declarators and runtime provenance checks
are outside this implementation. The caller must obey C's same-array and
representable-distance requirements.

[The bootstrap record](../bootstrap/PRIVATE-POINTER-DIFFERENCE.md) records
positive execution, rejection and recovery evidence and remaining qualification.
