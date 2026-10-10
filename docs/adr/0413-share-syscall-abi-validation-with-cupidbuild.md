# ADR 0413: Share syscall ABI validation with CupidBuild

CupidBuild and the standalone contract use one semantic validator for the six
external-program ABI declarations. The validator borrows bounded byte spans,
returns a caller-owned report, and holds no filesystem or mutable global state.
This keeps the reviewed ABI rules identical while allowing each caller to own
its input lifetime: the standalone contract retains snapshot and reread checks;
CupidBuild retains source and ancestor observations until validation,
revalidation, and close all succeed.

The new `verify-user-abi --root ROOT` command is read-only. It creates no files,
locks or children, and emits the existing JSON report only after closing its
observations. Sequential rechecks detect drift but do not make the filesystem
atomic. Source-head candidate plans include both new modules; installed seeds
and the Python-coordinated Make gate retain their existing ownership until a
separate paired release qualifies the command.

Changed long/alias stages remain unqualified until an explicit paired release
passes behavior qualification under ADR 0411. Stage preparation checks actual
source and artifact bytes and final-stage equality; it supplies no complete
bootstrap report. The ABI extraction does not add new historical parent hashes
or relax that release boundary.
