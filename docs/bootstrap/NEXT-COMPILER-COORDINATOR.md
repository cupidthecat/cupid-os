# Compiler coordinator migration

The kernel-profile handoff is implemented: Make invokes checked CupidBuild
for all 157 roots, including generated symbols. Each rule binds the complete
source/header closure, Makefile, checked six-tool seed, and fixed source/output
pair. CupidC reads the frozen `CUPSRC1` bundle under the original logical paths.
An absent entry cannot fall back to live files. Equal validated output retains
its timestamp after the same input and publication checks.

ADRs 0390, 0391, 0392, and 0393 record the format, generation checks, full
capture table, and adoption. Both promoted cohorts come from `9d2529a7` and
one independently verified 59-input source snapshot. The staged behavior
inventories are 36/7/42 on Linux and 24/7/29 on Windows. The largest kernel
closure has 90 files; the in-kernel assembler closure has 79.

Python remains the kernel profile's optional reference coordinator. It still
owns three Doom compatibility and 80 Doom-tree compiler recipes. Generated
installation tables and user programs use their separate wrapper. The next
native compiler step is documented in [the Doom coordinator audit](NEXT-DOOM-COORDINATOR.md).
It needs a distinct discovery policy because object publication occurs inside
scanned directories; the strict profile-manifest checks must stay intact.

The investigation below is a historical design record. Its future-tense steps
describe the original proposal, not pending kernel compiler work.
The shared terms follow [CONTEXT.md](../../CONTEXT.md).

## Historical initial eleven-source scope

The first source checkpoint used the following eleven closures. ADR 0392
extends it to all 157 roots; the fixed profile and transaction requirements
below still apply.

Add a typed `cupidbuild compile-kernel` operation for all eleven existing frozen
kernel closures under the unchanged `kernel` profile:

- `kernel/audio/nuked_opl3.cc`
- `kernel/core/kernel.cc`
- `kernel/core/string.cc`
- `kernel/cpu/fpu.cc`
- `kernel/cpu/ksyms_data.cc`
- `kernel/cpu/libm.cc`
- `kernel/cpu/simd.cc`
- `kernel/gfx/glyph_raster.cc`
- `kernel/gfx/jpeg.cc`
- `kernel/smp/percpu.cc`
- `kernel/smp/smp.cc`

Use `kernel/cpu/ksyms_data.cc` as the first executable case. Its complete
closure is the generated source, `kernel/cpu/ksyms.h`, and
`kernel/core/types.h`; its output is `kernel/cpu/ksyms_data.o`. Preserve its
600-second timeout and the ordinary sources' 180-second default. Its data-only
object also exercises a validation case that an assembly-only test would miss.

Reuse CupidBuild's seed capture, candidate capture, owner lock, retained
publication parent, rollback, and cleanup. The missing host capability is a
retained private compiler root that preserves nested logical paths.
[`cupidbuild_host_freeze_input`](../../toolchain/cupidbuild_host.h#L61) currently
freezes individual files, while
[`CupidC --root`](../../toolchain/cupidc_main.cc) resolves the input, includes,
forced includes, and output beneath one native directory. Passing ordinal
frozen filenames would change include resolution and source identity. The
compiler must read the captured closure under its original logical names,
without falling back to live source files.

The existing `cupidbuild run --tool cupidc` command supplies checked execution,
not this compiler transaction. [ADR 0376](../adr/0376-admit-cupidc-to-the-checked-tool-runner.md)
leaves source closure and publication with its caller. Keep that separation.
Source capability, paired seed carriage, and normal Make adoption need their
own evidence. The first production handoff would move eleven participations;
240 is the potential total for the complete wrapper replacement.

## Historical object and test requirements

Preserve the checks in
[`validate_i386_relocatable_bytes`](../../tools/cupidc_kernel_compile.py#L688):
little-endian i386 `ET_REL`, valid section and symbol tables, section bounds and
alignment, bounded symbol values and sizes, required `.symtab`, `.strtab`, and
`.shstrtab`, and valid `REL` records. Reject `RELA`, relocations against
`NOBITS`, unsupported relocation types, and out-of-range targets or symbols.
Accept only `R_386_32` and `R_386_PC32`; absolute addends may name a subobject,
while PC-relative addends must be `-4`. Data-only objects remain valid.

[`cupidbuild_validate_relocatable`](../../toolchain/cupidbuild.cc#L1439) already
uses Cupid's ELF reader. Reuse it with executable bytes optional, then add the
compiler-specific checks that it does not currently express. Do not weaken
the wrapper's relocation policy during the transfer.

The existing executable test map is:

| Contract | Existing coverage and next check |
| --- | --- |
| Frozen input root and logical paths | [`test_source_driven_inputs_are_compiled_from_one_frozen_closure`](../../tests/test_cupidc_kernel_compile.py#L1917); compare native transaction objects with the Python wrapper for all eleven active sources. |
| Generated data-only object | [`test_generated_kernel_symbol_inputs_are_compiled_from_one_frozen_closure`](../../tests/test_cupidc_kernel_compile.py#L2048); reproduce the real generated symbol object. |
| Source and header drift | [`test_source_driven_input_drift_preserves_the_existing_object`](../../tests/test_cupidc_kernel_compile.py#L1990) and [`test_generated_kernel_symbol_drift_preserves_the_existing_object`](../../tests/test_cupidc_kernel_compile.py#L2125). |
| Tool and object failures | [Compiler, manifest, and invalid-object cases](../../tests/test_cupidc_kernel_compile.py#L2199); retain diagnostics, timeouts, and previous output bytes. |
| Relocation policy | [PC-relative rejection, absolute subobject acceptance, and data-only acceptance](../../tests/test_cupidc_kernel_compile.py#L2286). |
| Guarded publication | Reuse [`CupidBuild CLI contracts`](../../tests/test_toolchain_cupidbuild.py) for locks, aliases, candidate replacement, output drift, rollback, and cleanup; exercise them through the new command. |
| Fixed-point carriage | Add a successful compile and a failing compile with preserved sentinels to both compared generations in [`bootstrap_toolchain.py`](../../tools/bootstrap_toolchain.py). |

Also reject missing headers, unapproved sources, invalid source/output
bindings, and linked inputs. After production adoption, repeat the OS build,
exact artifact checks, and boot/runtime smoke because these objects include
kernel entry, floating-point state, and SMP initialization.

## Boundaries identified in the original investigation

CupidBuild's current ordinary frozen-file and candidate reader has a 64 MiB
limit. A compiler-root implementation must retain bounded input ownership and
safe cleanup for its nested directories. This record does not establish that
the new interface exists or that any of these tests have run against it.

Disk and ISO packaging remain separate Python-coordinated transactions.
[`hostbuild.create_or_update_image`](../../tools/hostbuild.py#L923) uses
CupidObj `disk-template`, then preserves or rebuilds FAT state, stages files,
checks an independent template, and publishes a private image. Its normal
200 MiB output needs retained streaming I/O beyond the ordinary 64 MiB reader.
[`hostbuild.build_iso`](../../tools/hostbuild.py#L3425) uses CupidObj
`iso-fixture` with exact fixture membership, independent rendering, and guarded
publication. Neither CupidObj authoring API by itself replaces those host
contracts. See [ADR 0238](../adr/0238-publish-normal-disk-images-from-cupidobj-templates.md)
and [ADR 0241](../adr/0241-publish-normal-iso-fixtures-with-cupidobj.md).
