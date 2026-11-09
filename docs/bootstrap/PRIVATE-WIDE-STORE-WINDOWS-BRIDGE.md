# Private Windows bindings for the wide store

The private wide store now passes a normal Windows build with only two added
imports: `SetEndOfFile` and `NtQueryInformationFile`. The native build omits the
publication-race test macro. Its SDK declarations, assembly and checked PE
imports omit the fixture-only `GetFileTime` and `SetFileTime` support.

This is a tested prerequisite for active integration. Installed seeds, the 114
producer inputs, OS artifacts and 449 CupidBuild / three Python ownership counts
remain unchanged. The separate complete-image staging matrix is still running.

## Runtime and assembly checks

Ten native and Cupid-built cases pass. Four publish either the exact 8,193-byte
payload or an empty file. Six reject an out-of-range read, truncation behind the
store, or a same-size write behind it. Rejected reads leave caller storage
untouched; finish clears its result; publication preserves the previous bytes,
file identity and modification time. All temporary publication entries close.

Each case also makes 256 invalid end-of-file calls, 256 invalid information
queries and 256 successful basic-information queries. The pair makes 7,680
direct bridge calls before payload I/O. Caller bounds are 120
seconds, and nine conventional producer commands are forbidden.

The bridge preserves all 451 original code bytes, nineteen function offsets and
original relocations. It adds two functions, 44 code bytes and two import
relocations. Native NASM and CupidASM agree on complete text, function offsets
and resolved relocation tuples. Their complete ELF object files retain their
different metadata. Strict CupidDis checks pass for the caller, bridge and PE.

The exact two-addition import profile accepts the checked program. The old
profile, either missing addition, and the fixture-time profile all reject.
The private host adapter, header and three includes remain identical to the
[accepted retained publisher](PRIVATE-RETAINED-CHECKED-PUBLISHER.md).

## Remaining integration and evidence

Active header/runtime/source-plan changes, a new qualified producer cohort,
external input capture and the normal image-command/Make handoff remain open.
This result does not establish those boundaries or complete repository
acceptance for the pending source-owner seed adoption.

The [evidence archive](evidence/private-wide-store-production-windows-bridge-20261109.json.gz)
retains 369 file records, 34 local payloads and 172 references to the qualified
producer archive. It includes every runtime output and the independent assembly
and import checks. Its SHA-256 is
`c9811f95dd61c333a91b461ece12f7f81e3f1f3af35262a83409c87d740cf3e5`.
