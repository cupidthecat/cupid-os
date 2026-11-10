# ADR 0421: Observe file or directory kind through retained handles

Date: 2026-10-05

Status: Source capability tested; ISO capture and seed carriage pending

The ISO manifest names paths without encoding their kinds. Inferring directory
kinds from children loses empty directories. Trying the observer's typed file
operation first also fails because a wrong kind poisons that lifetime.

Add `cupidbuild_host_observer_kind` to the existing observer. It retains a
no-follow leaf handle, selects regular-file or directory kind from that handle,
and uses the existing strict stat checks to capture its metadata. Ancestors
remain typed directories. Final validation rechecks the original handle and
its name through the retained parent with the selected kind. Empty logical
paths select the already retained repository root.

The result is cleared before argument or prior-failure checks. Links, reparse
points and special files fail and poison the observer. POSIX unknown leaves use
a nonblocking open so a FIFO cannot block kind discovery. Windows already has
an unconstrained no-follow relative opener; discovery requests metadata access.
Existing file, batch-file and exact-directory APIs retain their strict kinds.

This observation reads neither payload nor directory membership. Capturing
either requires its existing explicit operation. Metadata alone retains its
same-size/restored-time limitation. Bounds, root metadata rules, close reporting
and sequential rechecks remain as specified in ADR 0401. A healthy repeated walk
may reuse a directory ancestor after comparing both its original handle and
fresh parent binding with the captured identity. Explicit leaves remain distinct,
and poisoned batches keep their existing independent-capture behavior.

All 74 observer methods pass on each host with native and checked Cupid-built
callers. Across those four selections, 280 methods execute and sixteen receive
expected platform/runtime skips. Fifteen new methods cover kind discovery,
empty entries, UTF-8 paths, rejection and poison, identity/metadata drift, and
explicit payload/membership observations after discovery, full flat/deep ISO
inventories and changed-ancestor rejection before recapture. Three current-source
callers run as native ELF32 and PE32. Independent receipts reread inputs,
commands, images, logs and unchanged installed seeds.

The first native Windows Unicode failure came from the test caller's CRT argv
encoding; ASCII hex transport matches the existing observer test convention.
The checked-build controllers first omitted a required validator argument,
then selected the wrong PE import profile. Those external failures remain
recorded, and corrected validation accepts the original linked image.

A complete seven-directory/505-file capture originally failed at file row 252
on both hosts because repeated ancestors exhausted the 4,096-handle budget.
The flat 512-file capture passed. The exact probe object and fixture trees pass
after changing only the checked host adapter object to include ancestor reuse.
The handle bound remains unchanged. Both failed callers and their actual
receipts remain retained beside the passing recovery.

The SDK and ABI controls already under qualification retain their frozen source.
The new API has no normal CLI or recipe edge yet, and installed seeds remain
unchanged. The ISO publication module will capture through this interface
before independently checking and publishing the checked author's image.
