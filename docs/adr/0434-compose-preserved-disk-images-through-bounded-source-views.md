# ADR 0434: Compose preserved disk images through bounded source views

The normal disk publisher preserves a 200 MiB image while replacing its boot
and kernel prefix. Represent captured inputs as bounded readers with 64-bit
lengths and offsets, and the disposable candidate as a sector writer. Fixed
sector buffers cover composition without retaining the image in memory.

Validate the complete checked CupidObj template against an independent native
renderer before the first candidate write. The renderer checks boot and kernel
bytes, MBR and BPB fields, initial FAT copies, empty root and every padding byte.
A valid previous image supplies the FAT suffix; a missing, invalid or
force-formatted image receives the full template and a zero data area. Preserve
the hosted publisher's existing geometry admission rules.

The result distinguishes a ready private candidate from a failed operation and
records attempted writes. A callback can fail after changing bytes, so any
failure after an attempted write requires discarding that candidate. Per-call
rollback would add a second disk transaction inside the existing publication
transaction and require another complete set of retained observations.

The caller still owns source capture, immutable views, staging, flushing, final
validation and guarded publication. The stdio test adapter covers the active
image below two GiB; full-range retained host adapters remain separate work.
The interface does not truncate 64-bit kernel sizes during overlap checks.

Both native and Cupid-built callers pass sixteen methods per host. Independent
rereading checks 22 complete image scenarios, identical composer objects and
actual checked CupidObj generation for the 200 MiB fixture. Producer plans,
paired tool qualification, installed seeds and normal recipe ownership remain
open. No production disk-image recipe changes in this private implementation.

The separate real-OS comparison uses the accepted paired bootloader, kernel and
preceding persistent image. All four native/Cupid callers produce the complete
209,715,200-byte runtime-tested image, SHA-256
`eb9c8531022cd5fdb3dd5450aabd7c5bb86968205f05ecca53049339133ccde9`.
Independent evidence rereads both host command logs, source captures, checked
templates and complete images. This carries the earlier image's exact identity;
it does not count as another boot or installed publisher acceptance.

The composed caller also exercises the composer, Unicode projector and FAT16
writer in one process. Every destination and payload is checked before the
first composition write. Ten methods pass through native and Cupid-built
callers on both hosts; six complete image scenarios match the Python oracle.
The separate real-OS comparison stages all three accepted user executables and
an 8 MiB synthetic WAD-shaped payload. All six complete images match at
209,715,200 bytes, SHA-256
`d04e63967a0dc1574c35e4cb04de1b6cc3f4051bdaa6eed725d6f8501d498daf`.
These are private caller and byte-parity checks. Retained capture, flushing,
independent final validation, publication and changed-image runtime acceptance
remain separate requirements.
