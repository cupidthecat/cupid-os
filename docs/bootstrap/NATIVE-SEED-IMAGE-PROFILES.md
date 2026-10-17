# Seed executable byte profiles

`cupidbuild_validate_seed_image_bytes` checks a captured executable against an
explicit ELF32 or PE32 seed profile. Both readers are available on each host.
The existing execution transaction still selects its own host format before
calling this function. Accepting an opposite-host image here does not make it
executable through `cupidbuild run`.

The caller supplies immutable bytes, their length, a format, an artifact role,
and the two existing generation flags. Role order is CupidASM, CupidC, CupidDis,
CupidLD, CupidObj, and CupidBuild. The legacy cohort has the first five roles.
Flags must be zero or one. The current Windows plan requires promoted PE32;
ELF callers use zero for that flag. Invalid arguments and payloads return zero.
The function retains no input pointer and does not modify the bytes.

The shared readers preserve the existing executable checks. Linux requires an
i386 static executable with entry `0x08048000`, at least one load segment, and
an entry in executable file bytes. Dynamic/interpreter segments and writable
executable load segments fail. Windows requires the supported PE32 layout,
entry `0x00401000`, and the exact ordered libraries and procedures for the
selected tool role and plan. The format parsers retain their existing bounds
and structure checks. The 64 MiB seed-image bound applies before parsing.

This function validates executable structure and role compatibility. The
caller must separately capture and recheck the file, parse its manifest, verify
its digest and release identity, and establish the relationship between the
Linux and Windows seeds. A self-consistent changed manifest is not release
proof. The future native artifact verifier still needs those boundaries; this
API does not replace Python's promoted-release checks.

The regression module tests all twelve current seed images on each host, wrong
format, role and import profiles, malformed and truncated input, entry and
machine changes, dynamic ELF segments, writable code, missing loads, and
invalid API arguments. Its caller compares the input bytes after validation.
An explicit `CUPID_SEED_IMAGE_PROGRAM` selects a prebuilt caller for Cupid-built
tests; an empty or invalid value fails without falling back to a host compiler.

Argument-rejection cases use a valid image for the selected format. In particular,
the ELF case rejects the Windows-plan flag without relying on a PE/ELF mismatch.
A private mutation that removes that flag restriction makes the test fail on
both hosts. All twelve tests pass with host-built and checked-Cupid-built callers
on both hosts. These results cover the byte API, not the unfinished filesystem
transaction or a staged bootstrap proof.

The implementation uses the existing CupidBuild source and header. Its build
rule already names both PE headers, and the six-tool snapshot already captures
them. No new production source path, host import, command, Make ownership, or
seed promotion is part of this extraction.
