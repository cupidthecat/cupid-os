# ADR 0290: Validate executable relocation ownership

## Status

Accepted on 2026-08-14.

## Context

ADR 0286 placed strict CupidDis inspection in front of both production
CupidASM object publications. That policy checked every executable byte, but
it did not check whether a relocation belonged to a decoded operand. An ELF32
object could move a valid `R_386_PC32` relocation from a call displacement to
the call opcode and still pass `--require-known` because the instruction bytes
continued to decode.

Ordinary CupidDis rendering already overlays a relocation only when its site,
width, and kind match a decoded field. The strict summary ignored relocations
that the renderer could not claim. This left the object gate with two separate
ideas of valid executable code.

## Decision

The typed decode summary now counts relocations that target executable
`PROGBITS` sections in an ELF32 relocatable object. It also reports how many
of those relocations have no compatible decoded field.

CupidDis recognizes the two relocation shapes used by the active i386 object
path:

- `R_386_PC32` must start at a four-byte relative field.
- `R_386_32` must start at a four-byte non-relative field.

The field must begin at the relocation offset in the same target section.
One decoded field claims at most one relocation. Relocations in data and
other non-executable sections remain outside this policy.

The ELF32 reader remains the first boundary for duplicate sites. It combines
relocation entries by target section and rejects overlapping four-byte fields,
even when the entries came from different relocation sections. CupidDis only
assigns ownership after that structural check succeeds.

The inspector and renderer share the same field-matching function. This keeps
strict validation and symbolic operand rendering on one rule. Hosted
`cupiddis --require-known` now fails when the unmatched count is nonzero and
prints both the unmatched and executable relocation totals. Raw input and
static executables retain the earlier instruction-only summary.

## Evidence

The first CLI test changed the call relocation in the existing writer-model
object from operand offset 1 to opcode offset 0. The unmodified command
returned success. It now reports one unmatched executable relocation and
returns failure without writing a listing.

A second CLI fixture clones a compatible code relocation into another
relocation section and points both entries at one decoded field. The ELF32
reader rejects the object with its existing `relocation fields overlap`
diagnostic. This pins the structural half of the one-field, one-relocation
rule at the strict command boundary.

The public object contract checks three executable relocations and one data
relocation. All three code relocations match, while the data relocation is
excluded. A second case changes an absolute memory relocation to
`R_386_PC32`; inspection remains available for ordinary rendering, but the
typed report records one unmatched executable relocation.

The complete CupidDis module passes 22 tests in 5.026 seconds with one
platform-specific skip.
The four active CupidASM source tests also pass. A fresh checked-seed build of
`toolchain/cupiddis.cc` produces a 99,752-byte relocatable object with SHA-256
`1dadf420e0629bb6082c41df2099910fe076e7559857f459f3117322f4c0b848`.
The new inspector accepts it. Fresh checked-seed production outputs for the
ISR, context-switch, `cupidld`, and three generated program objects also pass.
The complete 19-source static Toolchain fixed-point test passes in 817.172
seconds, including matching consecutive Cupid-built CupidDis images.

An audit of the 427 production-manifest paths available in the existing build
tree found 384 objects that pass the earlier decode rule. Every one also passes
relocation ownership. The other 43 artifacts already fail on unknown or
invalid instructions; none fails only on relocation ownership. Four absent
paths were rebuilt with the checked seed and pass. The existing tree is not a
fresh 431-input production build, so this result is compatibility evidence,
not a replacement for the production transaction.

## Rejected alternatives

Parsing rendered disassembly text was rejected. The typed decoder already
publishes field offsets, widths, and kinds, so text parsing would duplicate a
less reliable view of the same data.

Checking only that a relocation offset falls inside an instruction was
rejected. A relocation on an opcode or ModR/M byte can sit inside a valid
instruction without naming an operand field.

Counting every relocation in the object was rejected. Data-section pointers
are valid object metadata and do not belong to an instruction.

Leaving this to the structural ELF validator was rejected. That validator can
check section and symbol bounds, but it does not decode instructions or know
their operand fields.

## Consequences

Source-head CupidDis closes the semantic gap identified by ADR 0286 without
changing an object, ABI, link order, or OS source. The checked production seed
predates this capability. A later seed promotion must carry it before the
normal publication transactions enforce relocation ownership.

The report grows by two 64-bit counts. The summary pass uses temporary arena
storage proportional to the object's relocation count and rewinds it before
returning. Dynamic relocation domains, non-i386 relocation kinds, and data
embedded in executable sections still need an explicit typed model before
they can enter this policy.

The shared source rebuilds the in-kernel inspector as well as the hosted
command, and the CTXT update changes the embedded manual payload. The combined
integration branch must therefore run the root build, artifact-size policy,
and boot smoke after this slice and its companion compiler work are both
present. Those results are deliberately not claimed here. The change does not
alter the guest calling convention, linker policy, or instruction encoder. It
adds no host dependency and does not qualify any `.c` source for a `.cc`
rename.
