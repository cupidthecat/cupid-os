# Add i686 conditional moves to the shared x86 model

- Status: Accepted
- Date: 2026-07-22

## Context

The shared x86 catalogue drives CupidASM encoding and CupidDis decoding. It did not contain the i686 conditional-move family, whose opcodes occupy `0F 40` through `0F 4F`. CupidASM therefore rejected a valid instruction family, while CupidDis treated the opening opcode as unknown and could lose the intended instruction boundary in ordinary compiler output.

Conditional moves are a real x86 target capability. Removing or reshaping source to avoid them would narrow the toolchain around a missing instruction family. Keeping separate assembler and disassembler tables would also break the shared semantic seam that already owns the active instruction surface.

## Decision

The private catalogue now contains all sixteen canonical conditions:

- `cmovo`, `cmovno`, `cmovb`, and `cmovae`
- `cmove`, `cmovne`, `cmovbe`, and `cmova`
- `cmovs`, `cmovns`, `cmovp`, and `cmovnp`
- `cmovl`, `cmovge`, `cmovle`, and `cmovg`

Each mnemonic has a 16-bit destination and `r/m16` source form and a 32-bit destination and `r/m32` source form. Both forms are available in 16-bit and 32-bit modes, with the ordinary operand-size override when the requested width differs from the mode default. The catalogue marks the family as i686.

Mnemonic lookup also accepts the fourteen conventional aliases: `cmovc`, `cmovnae`, `cmovnc`, `cmovnb`, `cmovz`, `cmovnz`, `cmovna`, `cmovnbe`, `cmovpe`, `cmovpo`, `cmovnge`, `cmovnl`, `cmovng`, and `cmovnle`. Decoding and rendering always use the canonical names above.

The destination must be a general-purpose register. The source may be a same-width register or memory operand. Byte destinations, immediate sources, width mismatches, and `LOCK`, `REP`, or `REPNE` prefixes are rejected. Truncated opcodes retain the decoder's conservative truncated result, and an illegal prefix remains a separate byte so decoding can recover at the valid conditional move that follows it.

## Rejected alternatives

Adding only the conditions seen in one binary was rejected. All sixteen conditions share one encoding rule, and a partial family would make compiler output support depend on incidental optimization choices.

Adding decoder-only rows was rejected because CupidASM and CupidDis are meant to share one instruction authority.

Treating aliases as separate semantic mnemonics was rejected because it would duplicate forms and make decoded output unstable. Alias lookup maps each spelling to its canonical condition instead.

Accepting byte operands or immediates was rejected because the processor does not define those forms. Accepting repeat or lock prefixes was rejected for the same reason.

## Consequences and evidence

The shared catalogue now has 579 rows, including 577 encodable forms and two decode-only invalid recognizers. It has 242 canonical mnemonics, 64 registers, and fingerprint `063BAF16`.

The x86 contract checks every canonical condition across both modes, both widths, and register and memory sources. It also checks all aliases, exact bytes, decode semantics, requested-form replay, illegal operand and prefix failures, truncation, and recovery. CupidASM assembles every alias and rejects the unsupported neighbors. CupidDis renders all sixteen canonical conditions, includes a memory-source case, and preserves instruction boundaries around illegal and truncated input.

This change expands shared instruction coverage. It does not move a production source owner or retire a host dependency. GCC or Clang still builds the hosted and kernel consumers, and the normal OS C objects still come from the host compiler.
