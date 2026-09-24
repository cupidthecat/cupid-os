# Lock the external program syscall ABI

- Status: Accepted
- Date: 2026-07-26

## Context

External programs receive a pointer to `cupid_syscall_table_t` when `_start`
runs. The kernel and `user/cupid.h` describe the interface separately, but
the supported user build did not compare them.

That gap hid a real layout error. The kernel allowed 128-byte VFS names and
wrote a 136-byte `vfs_dirent_t`. The user header allowed 64-byte names, so
`ls.cc` reserved only 72 bytes for the same call. A successful smoke did not
make that stack overwrite safe. The user header also omitted the table
version, and nothing checked that `syscall_init` connected each table field
to the intended provider.

## Decision

The user header now publishes syscall version 5, a 128-byte VFS name limit,
and a 512-byte path limit. Its directory entry is 136 bytes on i386, with
`name`, `size`, and `type` at offsets 0, 128, and 132. Its file status record
is 8 bytes, with `size` and `type` at offsets 0 and 4.

`tools/user_syscall_abi.py` checks the seven files that own this boundary:

- `tools/user_syscall_abi.py`
- `kernel/core/types.h`
- `kernel/core/syscall.h`
- `kernel/core/syscall.cc`
- `kernel/fs/vfs.h`
- `kernel/network/socket.h`
- `user/cupid.h`

The checker compares the ordered table fields and their signatures. It pins
version 5, 103 fields, a 412-byte i386 table, the exported scalar widths and
signedness, every shared VFS and network constant, and both shared VFS record
layouts. It also requires one initializer for every field.

The 101 function fields have an ordered field-to-provider fingerprint.
Changing a provider therefore fails even when the replacement has the same
signature. The reviewed map retains the intentional `ntohs` to `htons` and
`ntohl` to `htonl` assignments used for little-endian byte swapping.

`make -C user` runs the checker before compiling a program. Its inputs also
belong to the deterministic user frontier. The active build audit records
this step as `verify_user_syscall_abi`, performed by host Python, rather than
folding it into a generic shell command.

## Rejected alternatives

A guest smoke alone was not enough. It had already let the undersized
directory entry appear to work.

Checking only table field names was not enough either. Signature changes,
typedef drift, record layout changes, and provider swaps can all break a
field while leaving its name in place.

Including kernel headers directly from external programs would erase one
copy of the declarations, but it would also expose kernel-only definitions
and include policy to the freestanding user profile. The small public header
remains a separate interface with an exact comparison gate.

## Evidence

The focused checker suite has thirteen tests. Its negative cases cover field
order, version disagreement, matching but unreviewed version drift,
signature drift, i386 scalar width drift, VFS constants and record fields,
socket constants, a missing initializer, and a same-signature provider
change. The checked contract reports:

- syscall ABI SHA-256
  `3e4d31320b2f56d19d37796ef679d1abbb228de9f36c9520d2dd5ec430c3c0bc`
- provider SHA-256
  `0a51ba85c93b0249215b05e54867fabe0e7206d7e58a7695911a6ecb060916f4`
- 101 reviewed function providers

The deterministic user frontier tracks 22 inputs with aggregate SHA-256
`d25d079d03a0e3e2f20727d2723c7239cdbf0a4244a5b78eaa3dcef7059f309e`.
Both runs reproduced all three objects and executables. The corrected
18,112-byte `ls` executable has SHA-256
`6eb9d140dd126f74e2815a6836c8858e0d9ca8a1da837bd94784c3a1b7c5ec9d`.
The earlier `ls` hash in ADR 0112 remains evidence for the undersized header.

`make test-user-cupidc-runtime` passed from the final source and CTXT state in
1,072.9 seconds. Three independent boots loaded hello, ls, and cat as PID 4
at `0x00F00000`. Hello emitted the 27-byte greeting fingerprint, its PID,
and a nonzero uptime before exit. Ls emitted every required root-entry
fingerprint through the corrected directory record before exit. Cat emitted
the 62-byte hostile fixture fingerprint, no PID 999 exit event, and its own
exit event. The hello, ls, and cat logs have SHA-256
`e097f2eda585354b7729216d006f8bf6cf24bcf90e75ed299cdece9a5659dfa1`,
`a75e929a866caae126f0d80ac4237277e3271b1bbb52e1bca2bf5d488ba0b814`,
and
`feb93dcdecf7de2c9412d8166851924b44e3ada224ac0e11b32d4131e3795267`.

The active graph now contains 501 transforms. The new transform belongs to
host Python and produces no OS code. CupidC still owns 151 transforms, and
the host C compiler still owns 146.

## Consequences

External-program compilation and linking remain owned by checked CupidC and
CupidLD. Python now performs one additional verification transform before
that work starts.

The public and kernel headers may still be edited independently, but the
supported build stops before compilation unless their i386 ABI agrees with
the reviewed contract. Future table changes must update both declarations,
the kernel initializer, the ABI version when compatibility requires it, and
the focused tests.
