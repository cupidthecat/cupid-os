# ADR 0399: Extract reusable artifact and seed validation

Date: 2026-09-22

Status: Accepted

## Context

Native artifact verification needs the existing CUPSIZE2 policy decision and
both ELF32 and PE32 seed-image checks. The policy implementation lived in a
test executable. The Toolchain manifest verifier included that executable's
source to reuse its parsing helpers, types, and global error state. Replacing
only the policy executable would break the manifest verifier and author.

The checked execution path selects its own host format. Artifact verification
must also inspect the opposite host's images without making them executable.

## Decision

Extract `artifact_size_policy.cc` behind an immutable-byte API with an explicit
result and bounded diagnostic buffer. Failure clears the result. Keep request
file I/O and canonical report output in the existing executable adapter.

Move shared parsing helpers into `contract_parse_internal.cc`. Every caller
supplies an error context; failure state remains available even when the
diagnostic buffer has zero capacity. The manifest verifier and author use this
module directly and no longer include the artifact-policy test executable.
Neither core retains input pointers or mutable global parsing state.

Expose CupidBuild's existing executable readers through an explicit format and
tool-role byte API. Both formats are available on each host. The execution
transaction continues to select its own host format. Image structure and role
compatibility do not prove manifest authenticity, release identity, or file
lifetime.

Extend every affected build closure, staged contract runner, and graph audit
to include the extracted modules. The source inventories become 61 bootstrap
inputs and 78 Toolchain publication inputs. Release pins continue to describe
the installed 59-input generation. Readers admit the existing 59 and new 61
counts, rejecting intervening or unrelated counts. Following ADR 0380, the
v2 parent window advances to the preceding 83d00ce7 and active 142a9737 pairs;
the historical v1 rule remains unchanged.

## Validation and limits

The isolated draft passed policy, manifest, runner, publication, graph, and
CupidBuild regression suites on both hosts. Checked Cupid-built policy and
manifest executables passed their semantic suites. Both host-built and
Cupid-built image callers passed all twelve profile tests. Removing the
ELF Windows-plan flag guard made the dedicated negative case fail on each
host. The bootstrap log records active-worktree validation separately.

The integrated source passes both staged proofs: 29 Linux and 32 Windows
artifact pairs match between stages three and four. Both hosts also pass
normal OS builds, all sixteen size checks, user ABI validation, user builds,
and private four-CPU disassembly and shell smokes. Independent post-run checks
confirm matching artifacts, generated objects, user executables, ABI reports,
and images. The updated embedded manual accounts for the only changed kernel
input object and the two reconciled kernel-size rows. The bootstrap log records
the failed size gates and completed acceptance separately from seed promotion.

This step does not adopt a native artifact-verification command or change the
twelve remaining Python-coordinated operations. Retained filesystem observation,
release identities, paired manifest validation, and final input rechecks remain
required before that command can replace its Python coordinator. New checked
seed publication requires fresh paired staged proofs and production acceptance.
TempleOS remains read-only reference material outside these inventories.
