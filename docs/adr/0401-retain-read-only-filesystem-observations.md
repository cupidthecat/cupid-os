# ADR 0401: Retain read-only filesystem observations

Date: 2026-09-23

Status: Accepted

## Context

Native artifact verification needs to keep its original inputs open through
validation. The execution runner retains frozen copies and creates private
state. Reusing that transaction would change the verifier's read-only behavior.
Its Windows relative opens also share deletion and accept only ASCII names.

## Decision

Add an opaque observer lifetime to `cupidbuild_host.h`, implemented beside the
existing platform adapters. It opens an absolute repository root, observes
regular files or exact directory membership, requires unchanged observations,
reports a diagnostic, and closes. It creates no files or locks. Every ancestor
and original file handle remains open until close. A failed operation poisons
the lifetime, and result outputs are cleared on failure.
Opening also captures the repository root's link count, size and modification
time. Final validation compares those fields as well as its identity, preserving
the production wrapper's root-drift check. Other retained directories require
unchanged identity; unrelated changes within them are permitted unless exact
membership was observed.

File observations distinguish metadata from bounded payload reads. Metadata
keeps 64-bit file sizes and compares identity, link count, size and modification
time. Payload observations also retain a SHA-256 and reread the original handle
during final validation. Metadata alone cannot detect same-size edits with
restored modification times. Directory membership is an unordered exact set;
it does not establish the kind or contents of each member.

Windows opens share reading and writing, but not deletion. Relative NT opens
decode strict UTF-8 and directory enumeration decodes UTF-16, including surrogate
pairs. Reparse points are rejected. POSIX opens use no-follow and nonblocking
flags before file-kind checks. The raw i386 Linux adapter also requests large-file
access and normalizes syscall failures before retaining handles. Revalidation
checks original handles and reopens each component through its retained parent.
These are sequential drift checks, not an atomic snapshot.

Bound each lifetime to 4,096 handles and 4,096 expected names. Paths allow 8,191
bytes and individual UTF-8 components allow 1,023 bytes. Payloads are limited to
64 MiB; metadata-only observations are not limited to 32-bit file sizes.

The hosted runtime supplies standard input and `strcpy`, required by the new
checked caller and implementation. Windows pipe EOF is not a stream error.
Existing installed seeds remain pinned; the observer is not yet used by the
production artifact-verification command.

## Evidence and remaining work

The 36-method filesystem/runtime suite passes with both native and checked
callers on both hosts. It covers replacement denial on Windows, replacement
detection on Linux, payload drift, Unicode names, links, junctions, FIFOs,
large files, membership errors, output clearing, poisoned lifetimes and repeated
cleanup. The existing fourteen-method host-runner suite also passes on both
hosts. `docs/bootstrap/NEXT-NATIVE-OBSERVER.md` records skips, failed approaches,
limits and reproduction commands.

Both candidate staged proofs pass and have been independently rehashed against
the same 66 source inputs. Stage-three/four output matches for all 35 Windows
and 32 Linux objects and tool images. Linux contract publication passes all 65
stage pairs and verifies its 80 inputs and 22 published artifacts. Both hosts
pass final image, user-program, ABI and four-CPU runtime checks. Independent
paired rehashing checks 1,495 source inputs, sixteen artifacts and 431 link
inputs; the final images match byte for byte and remain unchanged through the
private smokes. Installed seeds are unchanged. Release-file authority and the
final native verification command remain separate work.
