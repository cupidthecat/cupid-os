# ADR 0132: Share immediate three-operand multiply encodings

- Status: Accepted
- Date: 2026-07-26

## Context

The shared x86 catalogue drives CupidASM encoding and CupidDis decoding. It
already represented the one-operand and two-operand forms of `IMUL`, but it
did not represent the three-operand forms encoded by `69 /r` and `6B /r`.
CupidDis therefore fell back to data rows at real compiler-generated
instructions, and CupidASM could not express the same operation.

The older complete gap census found 391 immediate three-operand multiply
instructions among host-built kernel and Doom objects. A current focused LLVM
census found seven in `kernel/gfx/jpeg.o`, eighteen in
`kernel/audio/nuked_opl3.o`, two in `kernel/doom/src/r_main.o`, and none in
`kernel/doom/src/m_fixed.o`. The checked Cupid-built seed images already
decode without fallback, so this is an active host-output frontier rather than
a checked-seed regression.

## Decision

Add the complete 16-bit and 32-bit immediate multiply family to the shared
catalogue. Both widths are available in 16-bit and 32-bit modes. The
destination is a general-purpose register, the second operand is a same-width
register or memory value, and the third operand is an immediate.

`69 /r` carries a full 16-bit or 32-bit immediate. `6B /r` carries an
eight-bit immediate that the processor sign-extends to the operand width. The
automatic encoder chooses `6B /r` only when the semantic value fits a signed
byte; it otherwise uses `69 /r`. A caller that supplies a decoded form can
replay that exact encoding.

The ordinary operand-size override selects a non-default width. The usual
ModRM, SIB, displacement, and 16-bit addressing rules apply to the source.
`LOCK`, `REP`, and `REPNE` are rejected, as are a memory destination, byte
operands, width mismatches, and an incompatible serialized immediate width.
Truncated input retains every available byte with zero bytes consumed. An
illegal prefix remains separate so decoding can recover at the valid
instruction that follows it.

CupidDis renders the decoded three-operand instruction through its existing
canonical Intel-style formatter. A sign-extended byte is rendered as the
decoded semantic value, which CupidASM can assemble back to the same short
form.

## Rejected alternatives

Decoder-only rows were rejected because CupidASM and CupidDis share one
instruction authority.

Adding only `6B /r`, or only the width found in one object, was rejected. The
four encodings are one small instruction family, and partial support would
make coverage depend on host optimization choices.

Changing active C source to avoid the instruction was rejected. Immediate
multiply is a normal i386 compiler output, so the toolchain must represent it.

Apparent matches caused by decode drift inside unsupported packed-SSE
instructions were not counted as real multiply instructions. The independent
LLVM instruction boundaries provide the focused source-grounded census.

## Consequences and evidence

The catalogue now has 583 rows, including 581 encodable forms and two
decode-only invalid recognizers. It has 242 canonical mnemonics, 64 registers,
and fingerprint `EE543CA5`.

The shared x86 contract checks exact `69` and `6B` bytes in both modes and
both widths, register and memory sources, operand-size overrides, signed-byte
selection, full-immediate selection, decoded fields, exact-form replay,
every-byte truncation, transactional failures, illegal prefixes, and
same-buffer recovery. CupidASM produces the same bytes from source and rejects
invalid neighbors. CupidDis renders the family deterministically and recovers
around an illegal prefix and a truncated tail.

Across the four focused host-built objects, fallback rows fall from 540 to
495. The 27 independently identified instructions render as canonical
three-operand `IMUL`. The reduction is larger than the instruction count
because recognizing a full instruction also repairs downstream byte
alignment.

This change expands the shared instruction model. It moves no production
source owner and retires no host dependency. The sampled kernel, Doom, and
vendored objects remain host-built. Padding NOPs, packed-integer SSE2,
repeated-prefix padding, code/data range typing, dynamic ELF, DWARF, and a
generated every-form corpus remain open.
