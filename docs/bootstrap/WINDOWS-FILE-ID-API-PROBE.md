# Windows file-ID platform experiment

The native i386 Windows SDK contract passed on 2026-10-16. The shared
contract then failed under checked CupidC with `CTD000003` during IR lowering.
The proposed hosted API declarations, CupidASM bridges and contract runner
remain private and unadopted. Cupid-built API qualification is incomplete.
Normal ownership stays at 449 CupidBuild actions and three Python coordinators.

This experiment follows a directory-cohort rename failure: an ordinary
named member handle prevents the measured parent move on local NTFS. Opening
the same retained file by ID is a candidate platform requirement. The experiment
checks the actual Windows APIs before any cohort or SDK-publisher integration.

## Native SDK result

Clang targets `i686-pc-windows-msvc` with genuine Windows SDK headers and x86
CRT/import libraries. A shared typed cdecl adapter calls `OpenFileById` and
`GetVolumeInformationByHandleW`; a tiny COFF stack oracle keeps SafeSEH enabled.
The native executable returns zero and emits all 18 required events.

- The real descriptor is 24 bytes, aligned to eight, with size/type/union
  offsets 0/4/8. Tag, typedef and pointer-alias assertions pass. Ordinary
  automatic descriptor, array and nested-member addresses satisfy the checks.
- Each of three lanes writes and reads 131,073 patterned bytes, checks FNV-1a
  `f5185866`, seeks and requires EOF. Identity comparisons retain volume serial
  and both actual file-index halves, along with the asserted file metadata.
- The named attribute-handle lane requires the observed move denial. Joined
  by-ID attribute and payload handles remain held through successful directory
  move and move-back. Transient named readers close before the move.
- Each lane repeats both APIs 512 times without changing the observed stack
  position. Invalid descriptor size/type and invalid hint calls yield errors
  87/87/6, captured before diagnostics or cleanup, followed by a valid retry.
- Volume-query outputs and UTF-16 buffer bounds retain canaries. Optional-null
  outputs, a short buffer and an invalid handle exercise the failure paths.

All owned native fixture files and directories are removed. This result covers
these observed local NTFS assertions; it does not qualify UNC, ReFS, a complete
directory cohort or SDK publication. Microsoft's [OpenFileById documentation](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-openfilebyid)
also records filesystem and remote-technology limits.

## Cupid result and remaining gap

Checked CupidC stops while compiling the unchanged shared assertion contract:

```text
/toolchain/tests/windows_file_id_contract.cc:108:22: error CTD000003: CupidC IR lowering does not yet support this value type
```

This record preserves the reported failure without attributing it to a more
specific construct. No compiler follow-up, contract rewrite or retry was
performed. There is no Cupid executable or overall platform report.

Three profile unit tests pass: the dedicated import inventory, explicit source
closure and rejection-case inventory. The seven checks against an actual
Cupid-generated PE were not reached. The two runtime-dependent test methods
were not completed because `setUpClass` failed. The complete invocation exits 1,
with a null controller error and zero active processes after cleanup.

The API slice needs a separately qualified compiler capability before its
original contract can pass through CupidC, CupidASM and CupidLD. Cohort handoff,
its original zero/nonempty lifecycle cases, capability-query negatives and
publisher integration remain pending. No installed seed, historical import
profile, normal build edge or ownership count changes.

## Retained evidence

The private worktree is `C:\Users\admin\cupid-windows-file-id-api-20261016`,
based on `98a356805ce765e35e66dd3e2d703d4fc40a6a53`. The frozen assertion
contract has 19,388 bytes, SHA-256
`e1df25c0eec6697d77fccd488a565ae8b827d50a85ba6a5839b43374a5b6410a`.
The runner has 18,416 bytes, SHA-256
`bd1e775b986f5422578490d104ccb859d71a77156eba085c701fd383ba2409ef`.
These sources are retained for review and are not part of this documentation
commit.

The validation directory is
`C:\Users\admin\cupid-verification-repair-20261011\windows-file-id-api-validation-r1-20261016`.
Its 4,114-input plan freezes the source, checked Windows seed, Python, LLVM and
selected genuine SDK/CRT/resource directories. The outside-source private
product tree retains 25 source copies, six seed copies, three native objects,
one native PE32 and five exclusive native command receipts. Receipt 5 contains
the SDK assertion output. The failed invocation and all products are preserved;
no command was replayed.

| Record | Bytes | SHA-256 |
| --- | ---: | --- |
| Frozen recipe | 9,713 | `ef7749bfd5263a3a56bb698fd897cad6ce836878b952e7abd32f1595276d31df` |
| Input plan | 7,663,869 | `228a10950cfb310e0be48133795212c3264ec9b9dfd3a98c359c6b644bd58606` |
| Closed result | 3,169 | `b83b5f752e37d1265961c2e5f6f12bdcc544aefa2f8e9aabff9ee48d9dea8958` |
| Invocation log | 2,480 | `4ac65b01b93163a80ce845b835eda57574f877c9cb8367f96aad79ded3b5897f` |
| Root readback | 129,680 | `18a5de50b9fedcef396eafa2a30472bd5947f0f3bba5cca86de4e4fc238e16a3` |
| Current native PE32 | 133,120 | `719afd4a3810a73de968f7a585d554fd0e529226b5df66ed1d8c51708cc2b556` |

Independent Spec and Standards DATA reviews verify all 4,114 frozen inputs and
4,157 current models, copied-input/seed joins, the five receipt creator joins,
all SDK events and cleanup. Full guards pass again after stdout. The original
native executable writer identity and an unpersisted pre-execution status model
remain unknown; current captures do not reconstruct either. Checked-in source,
new source copies, native products and exclusive receipt writers retain
separate provenance. Documentation updates after validation are intentional
transitions; the frozen source/code evidence remains historical.
