# ADR 0144: Recognize exact Clang repeated-prefix padding

- Status: Accepted
- Date: 2026-07-27

## Context

Clang emits a second family of long alignment NOPs in current host-built
kernel and Doom objects. Each instruction starts with two through six
operand-size prefixes and ends with the same nine-byte sequence:

```text
2E 0F 1F 84 00 00 00 00 00
```

The shared decoder deliberately classifies repeated prefixes from one legacy
prefix group as invalid. That rule prevents a malformed prefix run from
silently changing instruction boundaries. Relaxing it for every instruction
would make the decoder less conservative and would let CupidASM request
redundant output if the forms entered the public catalogue.

An independent instruction-boundary census found 568 exact instances in 67
host-built objects. They occupy 7,380 bytes:

| Leading `66` bytes | Instruction bytes | Sites |
| ---: | ---: | ---: |
| 2 | 11 | 109 |
| 3 | 12 | 130 |
| 4 | 13 | 92 |
| 5 | 14 | 130 |
| 6 | 15 | 107 |

The longest form reaches the architectural 15-byte instruction limit. No
other repeated-prefix spelling was part of the measured compiler output.

## Decision

Add one private, decode-only recognizer before general legacy-prefix parsing.
It is active only in 32-bit mode and accepts only the five measured byte
strings. The prefix count must be two through six, the segment byte must be
`2E`, and the opcode, ModRM, SIB, and zero displacement must match exactly.

The result is a known word NOP with the semantic memory operand
`cs:[eax+eax+0x0]`. The result preserves all input bytes and the displacement
field, but reports `CTOOL_X86_FORM_AUTO` instead of a catalogue form. Asking
the encoder to encode those semantics produces the ordinary canonical
ten-byte NOP. It cannot reproduce the redundant prefixes.

All other duplicate-prefix input keeps the existing invalid classification
and consumes one byte. A one-byte `66` prefix remains truncated. Once a second
`66` is present, every partial exact form is invalid rather than truncated.
That rule makes recovery independent of how much of a malformed prefix run is
currently available.

## Rejected alternatives

Allowing repeated operand-size prefixes in the general prefix parser was
rejected. It would change every represented instruction and weaken an
established malformed-input boundary for five compiler-specific strings.

Adding five catalogue rows was rejected because catalogue forms are public
encoding choices shared by CupidASM and CupidDis. Redundant prefix runs are
accepted here only to inspect existing host output.

Recognizing a common prefix and then decoding any following NOP was rejected.
The compiler evidence supports exact complete strings, not a new prefix
grammar.

Changing Clang alignment flags or active source was rejected. CupidDis must
inspect the compiler output that the repository already has.

## Consequences and evidence

The public catalogue remains at 587 rows, 242 canonical mnemonics, 64
registers, and fingerprint `68E281CB`. The private recognizer does not create
an encodable form.

The x86 contract checks all five exact lengths, exact bytes and semantic
fields, the automatic form identity, canonical re-encoding, a following
instruction boundary, 16-bit mode rejection, every cut of every form, and
eight near misses. The near misses change the segment, opcode, ModRM, SIB, or
displacement, omit the segment, use seven prefixes, or spell `66 66 90`.
CupidDis renders five adjacent forms as five NOP rows, reaches the following
return at the exact offset, and recovers conservatively from the short near
miss.

Across the 228 available i386 kernel objects, this exception reduces fallback
rows from 3,597 in 68 objects to 1,901 in 36 objects. From the baseline before
ordinary and repeated-prefix NOP work, fallback rows fall from 6,952 in 77
objects to 1,901 in 36 objects.

This change moves no source owner and retires no host dependency. CupidASM
still cannot request redundant prefixes. The checked bootstrap seed still
contains the earlier 583-row decoder and needs an integration refresh before
fixed-point or production use can claim either padding slice.
