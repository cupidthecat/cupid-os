# ADR 0335: Type assembly functions and certify relocatable code anchors

## Status

Accepted on 2026-08-24.

## Context

CupidASM wrote every declared assembly symbol as `STT_NOTYPE`. CupidDis could
therefore check function placement after linking, when CupidC symbols carried
`STT_FUNC`, but it could not identify assembly entry points in an `ET_REL`
object. Inferring functions from global binding would also misclassify exported
data and imported objects.

## Decision

CupidASM accepts the NASM-compatible `global name:function` and
`extern name:function` forms. The annotation writes `STT_FUNC`; unannotated
symbols remain `STT_NOTYPE`. Missing and unsupported type names fail with a
specific diagnostic and do not publish an object.

CupidDis applies `--require-code-anchors` to static i386 ELF32 relocatable
objects. It counts each defined `STT_FUNC`, requires its value to name a decoded
instruction start in executable `PROGBITS`, and reports functions outside that
domain separately from functions in the middle of an instruction. Undefined
functions and other symbol types remain outside the count. The policy reuses
the section instruction map used by local-target validation. Relocated operand
fields remain link-time targets and keep their existing relocation-ownership
check.

## Evidence

The public CupidASM contracts cover defined, undefined, and ordinary symbols,
both accepted declaration forms, exact diagnostics, rollback, and recovery.
The CupidDis contracts cover valid aliases, relocated instruction fields,
excluded symbols, functions in data, mid-instruction functions, the indexed
decoder, absolute functions, bounded storage, and recovery. Focused Python
suites pass 44 tests
with one platform skip. Both native CupidDis contract groups pass.

## Consequences

Assembly authors can publish function intent without changing code bytes,
relocations, binding, placement, or label values. Production sources may adopt
the annotation only after both checked seed cohorts carry this assembler and
inspector capability. CupidDis does not guess function boundaries from binding
or disassembly.

`TempleOS/` remains read-only reference material.
