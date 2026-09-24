# Retained observations for native artifact verification

This work follows accepted ADR 0400. The observer implementation passes native
and checked-toolchain filesystem tests, both staged proofs, contract publication
and paired final OS/runtime acceptance. The production verifier still uses Python.

The observer interface lives in `cupidbuild_host.h`. It owns one read-only
repository lifetime and retains live file and directory handles until close.
Opening it must not create a private directory, lock, or output. File calls
either observe metadata without reading content or return a caller-owned,
bounded payload. Sizes use `uint64_t` so metadata observation does not truncate
large files on the hosted i386 runtime. Directory calls require an exact set of
names. Final validation checks retained identities, fresh repository-relative
walks, captured payloads, and observed directory membership. The repository root
also retains link count, size and modification time, preserving the wrapper's
separate root-metadata guard.

The interface has six operations: open, file, directory, require unchanged,
error, and close. Failures clear result outputs and poison the observation
lifetime. Close accepts a null observer. Logical paths are UTF-8; empty, dot,
parent, absolute, and link components are rejected, with the empty path reserved
for a directory observation of the repository root.

The existing execution runner cannot provide this lifetime unchanged: it keeps
frozen copies, and its Windows relative opens share deletion. The observer needs
retained live handles. Windows opens preserve Python's read/write sharing
without delete sharing, including retained ancestors. The existing ASCII-only
relative-name conversion cannot handle a Unicode selected-manifest parent.
POSIX leaf opens must remain nonblocking before type checks so FIFOs cannot
hang verification. Both native host builds and checked i386 builds need coverage.

`tests/test_cupidbuild_observer.py` defines the first real-filesystem contract.
Its caller pauses between observation and validation so tests can mutate files
without sleeps. Positive cases cover metadata-only capture, bounded payloads,
empty regular files, Unicode parents, and exact directory membership. Negative
cases cover oversized payloads, missing files, directories used as files,
unsafe paths, size changes, content edits with restored mtime, extra directory
members, later membership changes, symbolic links, and FIFO rejection. The
initial Windows run fails to link all six unimplemented observer functions.
No observer behavior is claimed from that red run.

The expanded suite has 36 methods. Both native builds pass, with four Windows
skips and two Linux skips. Both checked i386 callers pass, with three Windows
skips and one Linux skip. Windows rejects leaf, parent and repository replacement
while handles remain open; Linux permits replacement and the observer rejects
the changed identity. Coverage includes Unicode repository roots and membership,
malformed UTF-8, NTFS junctions, POSIX links and FIFOs, exact root membership,
duplicate and unsafe membership requests, a sparse 4 GiB + 37 byte file, and
1,200 repeated failed-observation cleanup cycles. The original host-runner suite
also passes on both hosts: fourteen methods, with nine Windows and one Linux skip.

Checked compilation first exposed missing hosted `stdin` and `strcpy` support.
The shared runtime now exposes descriptor zero on Linux and the inherited input
handle on Windows, treats Windows broken-pipe reads as EOF, and implements
`strcpy`. Tests cover partial pipe reads followed by EOF, invalid read destinations,
and string-copy return values, terminators and bytes beyond the copied string.
The first Linux checked run then found two observer bugs: raw syscall errors
were mistaken for retained descriptors, and large-file opens lacked
`O_LARGEFILE`. The corrected checked runs pass. Retained v1 through v4 evidence
lives under `build/bootstrap/native-release-6f2fe0e9/`; earlier failures remain.
V5 adds the root-metadata guard and passes on both checked hosts. A comparison
with Python reproduced the missing check on both hosts before the repair.
The first staged proof attempts were explicitly retired because they used the
earlier observer source. Their partial outputs remain as evidence.

Limits are 4,096 retained handles, 4,096 expected names in total, 8,191 path bytes,
1,023 UTF-8 bytes per component, and 64 MiB per captured payload. Metadata-only
observations retain the full 64-bit size. Absolute repository roots are required;
Windows uses drive-rooted paths. Logical names reject slash-separated empty,
dot and parent components, backslashes, colons, malformed UTF-8 and links.
Final checks are sequential drift checks, not an atomic filesystem snapshot.

Reproduce the native suite with `python -m unittest tests.test_cupidbuild_observer`.
Set `CUPIDBUILD_OBSERVER_CHECKED=1` for the same suite built and linked entirely
with the checked host seed. `CUPIDBUILD_OBSERVER_PROGRAM` can select a retained
caller. Native invalid-destination testing is skipped because that test targets
the Cupid runtime's defined error handling. Other skips are platform-specific.

Both candidate staged proofs pass against the same 66 source inputs, with
snapshot `b3f191540e183604e2cd5a939924f4408881ea39090452fbe5d19b2fd6e3e6c6`.
Independent native and paired rehashing confirms 35 matching Windows
stage-three/four outputs and 32 matching Linux outputs, including all six tool
images per host. Windows behavior checks cover 41 success, 34 failure and seven
help cases; Linux covers 54 success, 46 failure and seven help cases.
The common runtime change changes all six initial tool images from the installed
seeds. No installed seed has been promoted and none of the twelve
Python-coordinated operations has moved.

The first Windows attempt with these inputs failed under the longer evidence
path. A separate installed-seed probe reproduced the failure: a short checkout
passed while a deeper checkout failed before launching the frozen compiler.
Private instrumentation located `GetFileAttributesA` error 3 in the execution
runner's link check. The accepted retry uses a shorter checkout; it does not fix
this existing launcher limitation or establish long-path support for that runner.

The first final Linux kernel rebuild overlapped contract publication in the same
checkout. Creation of `toolchain/build` changed the compiler's recursively
observed directory closure, and two Doom compilations rejected the transaction.
The retry uses a separate checkout with the same frozen source and stable
objects. The first contract-publication attempt also exceeded the existing
360-second frontend deadline. Its retry keeps the same inputs and deadline;
the retried stage-two frontend object matches the accepted staged proof.
Windows final OS/runtime acceptance passes, including all sixteen artifacts,
the unchanged ABI, three unchanged user executables and a private four-CPU
SMP/disassembly/shell smoke. Independent rehashing confirms the final source and
link-input inventories and unchanged source image through the smoke. Linux
contract publication now passes: Cupid's author and the Python oracle agree on
all 65 stage pairs. Its 80 inputs and 22 published artifacts verify against both
the publication checkout and the separate final OS checkout. The publication
manifest SHA-256 is
`c56d2c2169f248584a074cf757b9bcd21b707d5bae1644aca968c2c4db4c040f`.
Linux image/user/runtime acceptance and the independent paired final comparison
pass. Both hosts reproduce all sixteen artifacts, 431 link inputs and the same
200 MiB image, SHA-256
`d5eabad433bf496ea65b310a71d885cd7fc7fd5e56ca3b52c5f122b49e49511a`.
The images are unchanged through their private four-CPU smokes. All 1,495 final
source inputs rehash correctly; the three user executables and prior accepted
images remain unchanged. Failed runs and their logs remain under
`build/bootstrap/native-observer-9814c273/`.

The earlier metadata-only Python probe accepted same-size edits with restored
mtime on both hosts. The production wrapper separately rereads seed and build
payloads, so that probe does not prove a failure of its payload comparisons.
The native observer must recheck captured payloads. Release-file authority and
the final artifact-verification command remain separate work; successful
observation or manifest parsing does not establish release trust.
