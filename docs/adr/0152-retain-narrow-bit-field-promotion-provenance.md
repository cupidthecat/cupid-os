# ADR 0152: Retain narrow bit-field promotion provenance

## Status

Accepted on 2026-07-28.

## Context

The Doom video backend packs four eight-bit color channels into one
`uint32_t` storage unit. Ordinary expressions read those channels before
shifting or masking them:

```c
blue = (c.r >> 3) << 11 | (c.g >> 2) << 5 | c.b >> 3;
```

On Cupid's i386 target, an eight-bit `unsigned int` bit field promotes to
signed `int` because every field value fits. The frontend already selected
that result. Linear IR saw only an ordinary same-rank conversion from
`unsigned int` to signed `int`, however, and correctly rejected it because a
generic integer promotion cannot change signedness at the same rank.

Weakening that generic check would admit forged conversion metadata. Changing
the Doom expressions would hide a valid C requirement instead of teaching
CupidC how to represent it.

## Decision

The frontend records the direct graph-member index on an
`INTEGER_PROMOTION` only when its operand is the lvalue-to-value conversion of
a narrow `unsigned int` bit field. "Narrow" means that the field width is
nonzero and smaller than its declared integer width. A full-width
`unsigned int` field keeps its unsigned type. An `unsigned char` bit field
uses the ordinary rank promotion and carries no special reference.

All expression paths use the same promotion helper. This includes ordinary
operators, switch controlling expressions, and the right operand of compound
assignment. The frozen frontend graph accepts the reference only on the exact
member-load-promotion chain.

Linear IR checks the reference before lowering the child expression. The
referenced graph member and layout member must exist, agree on the field
width, match the loaded type, and produce the expression's signed `int`
target under the represented bit-field promotion rule. The member expression
must be the direct child of the lvalue-to-value conversion and must name the
same member. A validated promotion becomes a `CONVERT` instruction that keeps
the member reference. All generic integer conversions still require no
reference, so the previous same-rank rejection remains in force.

## Evidence

Frontend contracts cover an eight-bit `unsigned int` field in an ordinary
operator, a switch condition, and a compound-assignment right operand. A
three-bit `unsigned char` field proves that the ordinary rank-promotion path
does not gain a member reference.

The focused Linear IR fixture has two functions and 14 instructions. The
eight-bit field produces a referenced integer-promotion `CONVERT`; the
32-bit field does not. Repeated lowering is identical. Four frozen-unit
mutations remove the reference, substitute a sibling member, forge a
full-width graph and layout pair, or make the layout width disagree. Each
reports `CTD000002`, leaves the
translation unit and output untouched, and is followed by a successful
same-job recovery.

The deterministic object fixture has three functions in 127 text bytes:

| Function | Offset | Bytes | Operation |
| --- | ---: | ---: | --- |
| `shift_red` | 0 | 37 | eight-bit extraction and signed right shift |
| `mask_green` | 37 | 47 | extraction, mask, and left shift |
| `shift_blue` | 84 | 43 | extraction and variable left shift |

The object has four symbols and no relocations. Shared decoding fixes every
instruction and byte. Eight i386 execution cases check results, unchanged
field storage, surrounding canaries, arguments, and restored cdecl stack,
frame, and callee-saved state. A 64-byte output limit fails without partial
output, and same-job recovery reproduces the first object byte for byte.

Source guards fix all four color-field declarations and all nine ordinary red,
green, and blue reads in `kernel/doom/src/i_video.c`, including both
occurrences of `c.b >> 3`.
Two exact-profile compiles of unchanged `kernel/doom/src/i_video.c` emit the
same 9,312-byte `i_video.o` with SHA-256
`8e9fcb59120cac9e8237a8243003fe1696a7841096aca7af360c89fec173336f`.
The profile now emits 74 of 80 objects.

## Rejected alternatives

Accepting every same-rank unsigned-to-signed integer promotion was rejected
because the conversion is valid here only because the source is a narrow bit
field.

Recovering the member by searching nearby expressions during IR lowering was
rejected because frozen metadata must state the semantic relationship
directly.

Tagging every bit-field promotion was rejected because full-width
`unsigned int` fields and lower-ranked integer fields already use the generic
rules without this exception.

Rewriting or casting the Doom color expressions was rejected because their C
semantics are valid and they should continue to drive the compiler.

## Consequences

Compiler-head CupidC now emits unchanged `kernel/doom/src/i_video.c`. The six
remaining Doom-tree failures concern an implicit call, positional union
initialization, and callback or pointer conversions.

The checked bootstrap seed does not contain this change. Doom remains
host-built, no production source or build owner changes, no `.c` file is
renamed, and no host dependency is retired.
