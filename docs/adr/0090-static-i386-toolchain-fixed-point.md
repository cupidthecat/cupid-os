# ADR 0090: Establish a static i386 Cupid Toolchain fixed point

## Status

Accepted on 2026-07-23.

## Context

ADR 0089 proved a complete CupidC generation. Generation-one CupidC compiled
all eleven objects in its own closure, CupidASM assembled startup, and CupidLD
linked stage two. Stage two repeated that compiler build for stage three.

That proof did not rebuild CupidASM, CupidDis, CupidLD, or CupidObj with the
stage-two compiler. The four commands already existed as complete static i386
Linux images, but repeating an earlier host-assisted link did not establish a
Toolchain generation boundary.

The five commands share most of their source. Compiling each closure separately
would repeat work and make it easier for the source lists to drift. The
fixed-point gate needs one literal union of the active sources, exact per-tool
link orders, and an explicit record of which stage tools produce the next
stage.

## Decision

The static i386 Toolchain fixed point uses these 19 C sources:

```text
hosted/i386-linux/runtime.c
ctool.c
ctool_host.c
elf32.c
x86.c
cupidasm.c
cupidasm_main.c
cupiddis.c
cupiddis_main.c
cupidld.c
cupidld_main.c
cupidobj.c
cupidobj_main.c
cupidc_pp.c
cupidc_type.c
cupidc_frontend.c
cupidc_ir.c
cupidc_emit.c
cupidc_main.c
```

The runtime alone enables GNU mode for its represented variadic built-ins. The
other 18 sources use strict C11. Every compile uses `/toolchain` through `-I`,
the checked i386 Linux declarations through `--include-angle`, and
`__SIZEOF_POINTER__=4`. Two bounded workers compile independent objects once
per stage.

Each stage assembles a fresh `hosted/i386-linux/start.asm` object. The five
exact link orders are:

| Tool | Objects after startup |
| --- | --- |
| CupidASM | driver, assembler, host adapter, core, ELF32, x86, runtime |
| CupidDis | driver, disassembler, host adapter, core, ELF32, x86, runtime |
| CupidLD | driver, linker, host adapter, core, ELF32, runtime |
| CupidObj | driver, object tool, host adapter, core, ELF32, runtime |
| CupidC | driver, emitter, IR, frontend, types, preprocessor, host adapter, core, ELF32, x86, runtime |

Generation-one CupidC, CupidASM, and CupidLD produce the complete stage-two
object and image set. Stage-two CupidC, CupidASM, and CupidLD produce the
complete stage-three set. CupidDis and CupidObj are rebuilt, compared, and
executed at both stages, but they are not build producers.

The contract requires all 19 stage-two and stage-three C object pairs to match.
The two startup objects must match, and each stage-two image must match its
generation-one image. Each stage-three image must match its stage-two image.
Every image must be a static i386 `ET_EXEC` with entry point `0x08048000`.

Both compared stages run all five help paths and ten successful operations:
C compilation, raw and ELF32 assembly, raw disassembly, symbol listing,
fixed-address and production-script links, binary wrapping, canonical text
wrapping, and executable flattening. Six failure cases cover malformed C,
invalid assembly, missing disassembler input, malformed ELF inspection,
malformed linker input, and missing wrapper input. Output-producing failures
must preserve an existing sentinel file.

`make test-toolchain-fixed-point` runs the focused contract.
`make test-cupidc-fixed-point` remains an alias for compatibility. The regular
repository test discovery also runs the contract.

The active-source audit parses the literal source and link manifests. It pins
the include forms, bounded worker count, producer call sites, generation
lineage, object and image comparisons, and named behavior calls. Audit mutation
tests remove or redirect each important edge and require the contract to fail
closed.

## Alternatives considered

Reusing generation-one CupidASM or CupidLD for stage three would prove only
that stage-two CupidC can compile the source union. Stage three therefore takes
all three producers from stage two.

Treating CupidDis or CupidObj as build producers would invent dependencies that
the build does not have. They remain required outputs with independent behavior
checks.

Building every tool closure separately would compile shared sources five times
per stage. A single union keeps one object identity for each source and gives
the comparison one complete manifest.

Comparing binaries without behavior checks would miss matching tools that fail
in the same way. The contract runs successful and failing work through every
command at both stages.

Calling this a checked-seed bootstrap would hide the native generation that
starts the test. Generation zero and the native contracts still come from GCC
or Clang.

## Consequences

All five static i386 Linux tools reach the same stage-two to stage-three fixed
point under the exercised Windows-through-WSL path. Their sizes and SHA-256
values are:

| Tool | Bytes | SHA-256 |
| --- | ---: | --- |
| CupidASM | 433,060 | `00F684CA5CA1E2BA36763E6810C65FEA8B3786D40F6008D635751A1F2C2B6DB0` |
| CupidDis | 366,968 | `67FCDBCF8A7924E37F00EC571BB5A4DBFBF4897C9743E9F3A3BBCAF0EA20CA60` |
| CupidLD | 262,388 | `373ED96803DCFB0005B8B3B1D49CA1313396EE11E17521AAD6402F487CDD97E5` |
| CupidObj | 182,704 | `1F48C3D7B5F80D3E33EB9268C087111E8FA54EB390C24368A09F7EC2981C0030` |
| CupidC | 1,812,712 | `29CD222C6E33590932457D36F3728705134C8C6750947E7CFBC4ABA3B7C5500B` |

The fixed point adds no checked artifacts to the repository. Both stage
directories are temporary and contain reproductions of the declared
generation-one images.

This decision advances Toolchain self-hosting without moving a production
build owner. A host C compiler and native linker still bootstrap generation
zero, the native contracts, and the hosted production commands. They also
compile normal Cupid OS C objects. Direct native Linux evidence, a native
Windows fixed point, checked seeds, fresh-checkout bootstrap independence,
normal-build handoff, and production C migration remain open.
