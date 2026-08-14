# ADR 0291: Require independent CupidC source suffix provenance

## Status

Accepted on 2026-08-14.

## Context

Cupid OS uses `.cc` for source owned by CupidC. ADR 0284 made one direction
of that rule executable: an active tracked `.c` file assigned to CupidC failed
the build audit. The reverse direction still depended on the suffix itself.
The audit classified every `.cc` input as Cupid C, assigned CupidC as its
runtime owner, and then checked only active `.c` files.

That made two unproved changes look valid. Renaming an active host C input to
`.cc` changed the reported owner even when its recipe still called the host
compiler. Renaming an unreachable `.c` file removed it from the residual C
census and created an unreachable `.cc` file that the ownership contract did
not inspect. In both cases, the spelling supplied the proof that the spelling
was supposed to report.

The active graph also has a legitimate exception to direct compilation. One
hundred thirty sources under `bin/` are installed as source text by CupidObj
and compiled inside Cupid OS. Their host build edge delivers the source rather
than compiling it, so the normal `cupid_c_compiler` marker cannot establish
their runtime owner on its own.

## Decision

The audit assigns CupidC ownership to a tracked `.cc` source only when it has
one of these independent records:

- an active checked CupidC compile edge;
- the checked CupidC Toolchain contract edge;
- an exact entry in `docs/bootstrap/c-source-suffix-ownership.json` together
  with a runtime delivery edge owned only by CupidObj and its host Python
  wrapper.

The suffix still selects the Cupid C language inventory. It no longer assigns
the runtime owner. This rule applies to every active `.cc` source in every
audit, including a repository without a source-suffix policy.

The source suffix policy records only the places where the graph cannot carry
the complete proof. It lists the 130 runtime-delivered sources, the 17
residual `.c` files and their audited roles, and the three unreachable `.cc`
files and their classifications. Paths are relative, normalized, unique, and
sorted. The parser rejects duplicate keys, unknown fields, a wrong schema,
bad suffixes, traversal and drive-qualified paths, stale paths,
classification drift, and entries that claim both active and unreachable
roles.

A production audit requires exact residual coverage. A newly tracked `.c`
file, an unapproved unreachable `.cc` file, or a policy entry whose source has
disappeared fails before report publication. A runtime delivery entry also
fails unless the evaluated graph gives that source exactly the CupidObj and
host Python owner pair. A compiler, contract, object-copy, or any other extra
owner rejects the entry. This keeps the exception list narrow.

In a nonproduction audit, an unreachable `.cc` source needs a policy entry, a
recorded historical relation, or an explicit Make exclusion. A partial
production-root audit does not apply this check to sources that belong to
omitted user or Toolchain graphs. The canonical three-root audit has the
complete view and requires exact policy coverage.

Generated `.cc` sources remain outside rename provenance. Their generators
own their content, and their checked CupidC compile edges establish the owner
of the generated translation.

## Evidence

The first public CLI run reproduced all three missing controls. A host-built
`main.cc`, an unreferenced `orphan.cc`, and a policy that still named
`retired.c` after a suffix-only rename all passed under the previous rule. The
three tests failed for that reason in 0.854 seconds.

After implementation, nine focused ownership tests passed in 3.171 seconds.
They cover ordinary host C, checked CupidC, approved CupidObj runtime delivery,
the two suffix bypasses, a stale residual policy, a lying delivery policy,
historical source relationships, and explicitly excluded `.cc` input.

A final review found that the delivery predicate rejected compiler owners but
still admitted an unrelated extra owner. A mixed CupidObj and host object-copy
fixture reproduced the problem in 0.398 seconds. The contract now compares
the complete owner set. All ten focused ownership tests passed in 4.558
seconds after that correction.

The first real three-root audit passed in 74.2 seconds. It found 408 tracked
`.cc` files: 405 active sources with independent ownership evidence and three
unreachable sources with explicit policy. The active evidence divides into
242 checked compile edges, 33 checked Toolchain contract edges, and 130
reviewed runtime delivery entries. The same policy locks all 17 residual `.c`
paths. No source path changed.

The first complete build-graph run exposed an overbroad enforcement scope. It
ran 93 tests in 643.225 seconds and failed 17. Sixteen fixtures deliberately
use small, policy-free repositories to test unrelated scanners, and a root-only
production audit deliberately omits the user and Toolchain graphs. Strict
active `.cc` ownership initially applied only when the production repository
or an explicit policy was present. Exact unreachable coverage applied to the
complete root, user, and Toolchain graph. The bypass tests carried an explicit
policy, so they still exercised the fail-closed path.

The seventeenth failure found a stale inventory lock from the preceding wide
integer conversion work. The committed audit already counted 6,088 `sizeof`
tokens, but the test still expected 6,043. Correcting the lock made its focused
generation and drift check pass in 216.585 seconds without changing the active
source census.

The first implementation's complete module passed all 94 tests in 980.970
seconds. Its `make bootstrap-audit` passed in 91.1 seconds, and
`make check-bootstrap-audit` passed in 87.0 seconds. The generated JSON is
2,677,678 bytes with SHA-256
`0433c9313a7fa8a4b2753000060d7447438c1ca94fa266928792db003de6bf81`.
The 12,502-byte Markdown summary has SHA-256
`349b56aab626fa3cb9c9ef07d1fc7530854f6b668ae3ee859e03d8513da8f142`.
The 4,297-byte policy has SHA-256
`139876a26fef87b4e769dd397642817a89f6565564e402c46572950645fa7e82`.

Spec review then found that a policy-free fixture still received
`unscoped_fixture_suffix` ownership. A host-built `main.cc` and an unreferenced
`orphan.cc` both returned success without a policy. Their direct CLI tests
failed in 0.306 and 0.314 seconds. Removing the fallback made active ownership
strict in every audit. Nonproduction repositories also check unreachable
`.cc` files, while partial production views retain their deliberate omission
boundary.

The same review added direct parser coverage. Seven document-shape cases cover
duplicate JSON keys, unknown fields, the schema, the root type, and all three
inventory types. Eleven path and inventory cases cover wrong suffixes,
traversal, drive-qualified paths, ordering, duplicate delivery entries, and
classification values. Two more cases exercise classification drift, and one
rejects an active and unreachable overlap. The drive-qualified case exposed a
real gap: `C:/escape.c` reached the stale-path check instead of the path
validator. Rejecting drive prefixes fixed it.

Four positive scanner fixtures and the two Cupid `#exe` diagnostic fixtures
had relied on suffix ownership. They now use exact CupidObj delivery edges and
explicit fixture policy. The first 98-test module found those assumptions and
the incomplete production-root boundary, failing 12 assertions in 743.935
seconds. The affected six methods passed in 92.382 seconds after correction.

The final focused cohort passed 16 methods in 10.869 seconds. The complete
module passed all 98 tests in 841.743 seconds. A final
`make bootstrap-audit` passed in 68.8 seconds, and the checked replay passed in
87.7 seconds. The generated JSON remains 2,677,678 bytes and has SHA-256
`3038b348a83ea614c5a8d61ff8e73bd7e1a01496fdece5f5ef10583a5a86affe`.
The Markdown summary and policy bytes did not change.

## Rejected alternatives

Keeping suffix-derived runtime ownership was rejected because it lets a path
change create its own proof.

Treating every active `.cc` file as CupidC-owned when any build edge reaches it
was rejected because a host C recipe is still a host C recipe, even when the
file has a `.cc` suffix.

Approving all `bin/**/*.cc` paths as a prefix was rejected because wildcard
discovery is part of the bypass. A newly renamed file beneath that prefix
would authorize itself again.

Listing every compiled `.cc` file in policy was rejected because the checked
compiler and contract edges already provide stronger, current evidence. The
policy covers only runtime delivery and unreachable or residual exceptions.

Reading rename history from the local Git checkout was rejected because a
shallow checkout, exported source tree, or later commit may not retain the
same history. The checked policy travels with the source and participates in
the audit provenance hash.

## Consequences

The audit can no longer transfer source ownership by changing a suffix alone.
Reviewers can see every exception in one small policy, while the generated
audit reports evidence counts and keeps the existing per-source build owners.

Changing the policy does not prove compiler behavior. A real rename still
requires the checked build, object validation, relevant behavior test, and
normal build or boot evidence that justify the ownership transfer. This
decision changes no source suffix, compiler output, ABI, build owner, object,
or OS runtime path.
