# ADR 0207: Represent forward x87 stack subtraction

## Status

Accepted on 2026-08-01.

## Context

The exponent range-reduction programs in `kernel/cpu/libm.cc` intend to keep
`x - round(x)` below the rounded value. Their GNU assembly spells the
subtraction as `fsub %st, %st(1)`. GNU `as` encodes that spelling as `DC E1`,
whose Intel-ordered meaning is `FSUBR ST(1), ST(0)`. The result is therefore
`round(x) - x`. This explains the recorded `exp(1)` result near 1.47.

The first diagnosis blamed CupidC's mapping of the GNU spelling to `FSUBR`.
An independent GNU assembly probe produced the same `DC E1` bytes. A native
x87 probe then returned `-0.44269504088896339` for the fractional part of
`log2(e)`. Those checks showed that CupidC preserved the source exactly and
that the source needed the opposite GNU mnemonic.

The corrected spelling, `fsubr %st, %st(1)`, encodes as `DC E9` and has the
required Intel-ordered meaning `FSUB ST(1), ST(0)`. Cupid's shared x86 model
did not contain that two-register form, so CupidC could not represent the
correction without extending the model.

## Decision

Add the two-register `FSUB ST(i), ST(0)` form to the shared x86 catalogue.
The form is available in 16-bit and 32-bit modes, requires `ST(0)` as its
second operand, and encodes `ST(1), ST(0)` as `DC E9`.

CupidC's exact exponent and power assembly paths accept both aligned source
spellings during the bootstrap transition. The existing `fsub` spelling
continues to emit canonical `FSUBR` and `DC E1`. The corrected `fsubr`
spelling emits canonical `FSUB` and `DC E9`. Frontend, Linear IR, and object
emission retain the distinction instead of silently changing legacy source
semantics.

The corrected spelling is now the positive contract for the double and
float power statements, the exponent statement, and the four file-scope
exponent wrappers. The current checked source remains unchanged until a seed
that carries this capability is promoted.

## Evidence

The shared x86 contract encodes and decodes `FSUB ST(1), ST(0)` as `DC E9`
in both execution modes. It rejects the reversed operand order
transactionally and checks that the failed encoding is zeroed. The
active-surface replay covers both `DC E9` and the existing `DC E1`
reverse-subtract form.

Corrected frontend, Linear IR, and object contracts pass for exponent,
double power, float power, and file-scope exponent assembly. Decoder checks
require canonical `FSUB`, operands `ST(1), ST(0)`, and the exact `DC E9`
bytes. Existing altered-template, forged-metadata, output-limit, rollback,
and recovery cases remain in force.

Compiler head still compiles the unchanged 43,736-byte `libm.cc` twice to
the locked 16,164-byte object with SHA-256
`ccfb59839b058020a3cdc30c8e6db7ebac8845215a38ff974b3cbca876574eac`.
This proves that the compatibility path does not alter the current production
object before the source and seed move together.

Source head now has 592 shared x86 forms, 244 canonical mnemonics, 64
register names, and fingerprint `F4420CB4`. The checked seed still carries
the 591-form `DBE77533` model in this increment.

## Rejected alternatives

Changing CupidC's meaning for the existing `fsub` spelling was rejected.
GNU `as` and CupidC already agree on its bytes, and silently reversing it
would make Cupid assembly effects differ from their source.

Changing the range-reduction stack order was rejected after the native probe
showed that the subtraction alone has the wrong sign. The `log2(e)` constant,
`FRNDINT` result, and return bridge were not needed to explain that isolated
failure.

Rewriting the exponent functions in ordinary C was rejected because the
active x87 program and its ABI bridge are established source behavior.

## Consequences

Compiler head can represent the source correction without a host assembler
or an opcode escape. A checked seed promotion must happen before changing
`kernel/cpu/libm.cc`, because the current seed recognizes only the legacy
templates.

No production source, object, image, runtime result, transform owner, or host
dependency changes in this increment. The next increment promotes the
capability, then the active source and guest checks can move to the corrected
range reduction.

`TempleOS/` remains untouched reference material.
