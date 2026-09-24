# Windows UTF-8 integration

The first source integration passes paired staged, publication and OS/runtime
acceptance. Canonical LF replay also passes. No seed has been
promoted and no production ownership has transferred.

## Canonical LF replay

Both staged proofs and their independent paired verification pass on the
canonical source bytes: 39 Windows artifacts and 32 Linux artifacts match
between stages three and four, with the same 73 producer inputs. The converged
Windows tools pass all 28 Unicode-path commands. Regression suites pass on
both hosts. Paired kernel verification also passes for all 16 artifacts and
431 link inputs, with 1,524 captured sources checked on each host. Measured
kernel sizes match the existing policy.

Full publication and its checked manifest verifier pass: 22 artifacts, 87
publication inputs, 73 bootstrap inputs and agreement on all 65 stage pairs.
Paired image/user/runtime acceptance passes independent verification, including
the three user binaries and private four-CPU disassembly/shell smokes that
preserved their images. Both hosts produce identical images and user programs.
The completed first-run evidence below remains separate from this replay.

## Implemented

The shared scalar codec rejects malformed UTF-8 and UTF-16. Explicit checked
Windows startups select wide command-line, file, publication and process APIs.
Each tool links one adapter role and its exact import profile. Source capture
keeps the selected mode through freezing and live/private revalidation.

Native Windows Make targets select separate UTF-8 host and entry objects for
all six tools. Linux retains its existing objects. Shared Windows host paths
use the codec for enumeration, retained component opens, queries and renames.

The C image reader distinguishes legacy, current ANSI and UTF-8 profiles. The
shared manifest reader binds the UTF-8 profile to 73 source inputs, the existing
Linux plan and Windows plan
`a31575236059b77a47bb58c79072754258c4762d30105319c451e407b7353f99`.
Historical manifests retain their separate counts and plan checks.

## Evidence

Native and Cupid-built adapter/observer tests pass on both hosts. One complete
Windows candidate generation builds with checked Cupid tools. The native CLI
suite uses it for guarded publication under ASCII, accented, Japanese and
supplementary-character roots. Output bytes, rejected-publication preservation,
directory membership and unchanged seed files pass. The bootstrap log records
commands' test modules, timings, skipped cases and failed attempts. The converged
Windows stage-four tools also pass the fixture directly: 28 commands across all
four path variants, with all six roles exercised.

## Integration acceptance

The candidate bootstrap paths now capture 73 UTF-8 source inputs; Windows
selects the wide plan. Publication capture, Make prerequisites and the exact C
manifest inventories now agree on 87 publication inputs. Artifact policy admits
the exact 73-input plan pair and rejects mixed generations. The build-graph
audit passes all contracts.

Paired staged proofs now pass independent verification: 39 Windows and 32
Linux artifacts match, with the same unchanged 73-file producer input set.
Paired kernel builds also pass: sixteen artifacts and 431 link inputs match
across hosts, and all 1,524 captured sources remain unchanged. The size policy
now records the measured raw kernel and both kernel ELF sizes.
Full Linux publication passes with 22 artifacts and 65 matching stage pairs.
The Cupid-built manifest reader and independent artifact/input verification
also pass, binding the 87 publication inputs and 73 bootstrap inputs.

Both image builds, all three user programs and private four-CPU E1000
disassembly/shell smokes pass. Independent verification rehashes all 1,524
inputs, sixteen artifacts and 431 link inputs, confirms identical images and
user binaries across hosts, and checks that each smoke preserved its source
image. The preceding accepted images also remain unchanged.

All 141 bootstrap regressions pass on both hosts alongside the separate
production fixed-point proofs. Publication suites pass 153 methods per host;
the manifest runner passes 25 per host. The bootstrap log retains platform
skips, native/checked command and transaction evidence, and failed attempts.

## Remaining work

Audit the final tree and committed bytes against canonical LF acceptance before
pushing. The first acceptance runs passed, but Git's text normalization would
have changed tested bytes at commit time. The canonical replay now passes; the
original runs remain separate evidence. Seed promotion remains a separate gate.

The Linux native GCC attempt also reported a maybe-uninitialized warning in
unchanged floating-update code. The new native CLI suite uses Clang; the GCC
warning remains recorded for diagnosis.
