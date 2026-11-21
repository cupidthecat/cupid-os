# ADR 0417: Pass captured seed facts to Toolchain publication policy

Date: 2026-10-04

Status: Source capability; release installation pending

The Toolchain manifest author and verifier contain a second Linux seed parser.
Its classic requests require one historical manifest digest and a 27-source
plan. The qualified ABI candidate has a 29-source plan and new tool identities.
Changing only the historical digest still fails; changing both pins in a
disposable diagnosis copy succeeds. Adding each new release to this parser
would duplicate the shared reader's provenance and image rules.

Add CUPMAN5 author requests and CUPMAN6 verification requests. They carry an
immutable seed context before the existing request body: the captured manifest
digest, a supported plan digest and six ordered filename, size and digest rows.
The policy validates the bounded envelope, hashes the raw manifest bytes and
matches every seed observation against the captured identities. It retains
the existing publication schema, source inventories, artifact rules and all
sixty-nine raw stage-pair comparisons. The supported plans remain the classic
27-source plan and the active 29-source candidate plan.

The Python adapters construct this context only after the shared seed reader
has checked the selected Linux cohort. Verification materializes its already
captured manifest and six images in a private directory for the same reader.
The adapters keep original observations through final drift and membership
checks. Captured facts do not establish release authority or file lifetime.
Windows execution seeds cannot supply the Linux publication-policy context.

Keep CUPMAN4 author and CUPMAN2 verification layouts and historical checks.
Their tests use a frozen historical manifest fixture so later seed installation
does not silently redefine those interfaces. Production adapters use the new
formats. Harmless manifest formatting follows the shared reader's rules; the
new context still binds the exact bytes selected for that request.

Tests cover candidate facts, wrong manifest and image identities, unsupported
plans, tool membership and ordering, zero sizes, truncation, trailing bytes,
live drift and recovery. Checked-seed integration builds and runs the native
policy with CupidC, CupidASM and CupidLD on each host. The bootstrap log records
the diagnosis failures and acceptance evidence separately from release proofs.

This contract source is outside the 81-/82-input compiler producer snapshots.
It therefore leaves the qualified compiler and tool images unchanged. It does
change hosted publication policy and its captured source inputs. Installed
seeds, production ownership and the native ABI Make handoff remain separate
acceptance steps.
