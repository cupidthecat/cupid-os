# Explicit release context for seed consumption

Installed Linux and Windows seeds remain the `78e71bd6` pair. Normal build
ownership remains 444 CupidBuild actions and eight Python actions, including
three user links. TempleOS remains reference material.

## Why the separate context exists

The proposed alias seed pair failed the original Windows default behavior
consumer with `fixed-point provenance differs`. That reader recognized only
historical parent tuples embedded in its source. Embedding the completed new
manifest digests would change the source that produces those manifests.
ADR 0411 uses the existing external release record to break that cycle.

`cupid_seed_manifest_validate_release` accepts reviewed release bytes and
checks every applicable manifest claim before structural, target and plan
validation. The original reader retains historical rules. The pair API still
hashes the actual Linux manifest bytes to verify the Windows plan reference.

Every typed seeded operation has a separate release-aware C entry point. The
original functions pass a null release and keep their request layouts. Every
seeded CLI accepts `--seed-release RELEASE`; missing, empty and repeated values
are usage errors. The transaction captures the release with its source and
tools, enforces a 65,536-byte limit and rechecks observations after execution.

The release does not authenticate itself. The caller must review its source,
actual parent manifests, complete plans and twelve paired tool identities.
Release parsing and file lifetime are separate checks.

## Reproduce preparation and qualification

Use `python -m tools.bootstrap_stage_release` from the reviewed source root.
Every command requires `--root`, `--linux-manifest`, `--windows-manifest` and
`--output`. Select the same real parent seed pair for both hosts.

1. Run `prepare-linux` on Linux and `prepare-windows` on Windows, with separate
   new output directories. Add `--windows-long-paths` to both for the long-file
   profile. Preparation retains source, objects and six tools through stages
   two, three and four. It compares the final stages and writes an explicitly
   `unqualified` receipt. It supplies no complete bootstrap report or behavior
   verdict. The complete publication checker rejects this bundle.
2. Independently check the source closure, actual parents, full plan digests,
   every retained object and tool, and equality of stages three and four. A
   preparation receipt alone does not establish that review.
3. Run `author` with both `--linux-preparation` and `--windows-preparation`,
   `--source-revision` and `--source-snapshot-sha256`. Select those two source
   identities from the reviewed capture. Use the same profile flags. Authoring
   checks source, plans, parents and all prepared bytes, then exclusively
   creates a twelve-artifact release candidate. Existing records are preserved.
4. Review the candidate independently. Run `qualify-linux` and
   `qualify-windows` on their actual hosts with the same paired inputs and
   `--release`. Each uses a new output directory. It rebuilds every stage,
   compares all final-stage objects and tools with the preparations, then runs
   complete behavior with the selected release. The runner inserts the release
   before the child argument separator and preserves child arguments.
5. Verify the closed child result, report, source copies, plans, parents, all
   artifact bytes and behavior files again. Only completed full behavior writes
   the normal bootstrap report. Committed-source proof, seed installation and
   recipe ownership require their own later acceptance.

Directory aliases are enabled by default. `--no-windows-user-link-aliases`
selects another closure and plan and must be used consistently throughout a
separate preparation, authoring and qualification sequence. Never relabel an
existing receipt or change parent or source fields to make it match.

## Current evidence

The private source capture contains 1,564 files. Default producer inputs number
77; adding long-file support gives 78. Both source review axes pass. Native
coordinator and staged-publication tests execute 77 methods across both hosts,
with one Windows POSIX FIFO skip. Generated fixture bytes in those unit tests
are metadata fixtures, not staged tool acceptance.

Actual default preparations retain 228 artifacts across three stages per host,
and all 76 final-stage pairs match. A separate author creates the paired release
from those actual tools and selected `78e71bd6` parents. Independent authoring
verification checks its 180-file Linux preparation mirror and original bytes.

Fresh complete default qualification passes on both hosts with host C/ASM tools
forbidden. Linux reports 62 success, 55 failure and seven help cases; Windows
reports 49 success, 43 failure and seven help cases. Independent verification
rereads every stage against preparation, all 1,564 source files and copies,
closed logs, plans, parents and 7,300 behavior files.

Long preparations retain 231 artifacts and match all 77 final-stage pairs.
The separate release author and its 181-file Linux mirror pass independent
verification. Fresh complete long qualification passes on both actual hosts.
It retains 7,300 behavior files and the same exact per-host behavior counts as
default. Independent verification checks all prepared and rebuilt bytes, closed
commands and logs, source copies, parents, plans and release claims.

Windows has 32 C objects, four assembly objects and six tools per default stage.
The long stage adds `utf8_long_path_start.asm`, giving five assembly objects and
43 total artifacts. Linux has 27 C objects, one startup object and six tools
for either profile. The verifier derives those counts from the selected plans;
its useful negative checks reject the earlier incorrect 33-C/four-assembly
split, floating-point counts and a false comparison result.

The broader native selection runs 580 methods per host: 566 execute on Windows
and 563 on Linux, with fourteen and seventeen exact platform skips. Independent
verification accepts all 1,129 executions. The original collectors failed
because a helper wrote one auxiliary JSON receipt after the unittest summary.
Those failures remain unchanged. The separate verifier accepts only the exact
known trailer after a complete successful method log; a failed suite followed
by a receipt remains a failure.

Cupid-built normal dispatchers and typed-operation callers also retain the
earlier verified 114 executed results and eight platform skips. The byte API
corpus retains 3,770 actual checked results. The source C bytes in those captures
are unchanged. These narrower checks remain separate from the new full staged
qualification.

## Corrections and remaining work

The first profile and Doom probes crashed when release capture grew the input
table and invalidated retained tool-path pointers. Reserving the complete
closure plus the release before retaining paths fixes that failure. Linux
regular-file candidates use nonblocking opens so FIFO rejection cannot wait
for a writer. Bounded capture now opens one handle, checks kind and identity,
reads at most the limit plus one byte, and rechecks handle and pathname state.

Review also found missing rebuilt-object comparisons, incomplete behavior
summary acceptance, overwritten first observations and an unbound audit Git
inventory. Each was corrected before the corresponding driver ran. The audit
driver now checks the owned detached checkout, base revision, all 1,564 tracked
paths and index bytes before and after commands.

Both initial canonical audits reject stale `active-build.json` and
`ACTIVE-SOURCE-AUDIT.md`. A separate integration checkpoint regenerates them.
Both actual canonical audits and all four conditional contracts per host pass;
independent verification binds their source copies, owned Git inventory, index,
closed commands and complete method logs. The earlier failed records remain
unchanged.

Both fresh normal `make -j4 all` builds pass with host C/ASM tools forbidden.
Independent verification checks all 1,564 source files and copies per host,
429 i386 relocatable objects, two linked kernel executables and 16 artifacts.
The images match byte for byte at 200 MiB and contain the original captured
manual. The first OS verifier incorrectly classified the linked first-pass
kernel as relocatable; the second used Linux separators for a Windows terminal
line. Those failed receipts remain preserved. The corrected verifier checks
both ELF kinds and each host's exact terminal rendering.

A fresh normal Windows user build passes its closed native ABI contract. The
entire canonical ABI report matches the accepted baseline, including types and
offsets, 103 fields, 101 providers and the 412-byte table. Actual CupidBuild
compiles three i386 objects and the normal CupidLD route links `cat`, `hello`
and `ls`. All three executables match the accepted bytes. Independent proof
rereads the source, support, six retained outputs, closed Make command and log,
full ABI report and unchanged OS image. This does not establish guest execution.

The first complete ordinary Linux default and long publication attempts fail
with `[Errno 5] Input/output error: 'cupidbuild.elf'`. The long collector's final
save also fails, leaving its raw state marked running after the actual process
closed with failure. A separate terminal observation records that distinction.
Four exact-tool copy probes later pass at the failed fixture and evidence
boundaries. The cause remains unknown. Fresh default and long retries pass
in new roots with the unchanged ordinary CLI and installed parents. Independent
verification rereads all 90 controls, 23 retained files and 22 static images per
profile; all six published tools match the corresponding qualified stage-four
tools. The closed producer logs establish the declared 67 author/oracle pairs.
Temporary discarded objects are outside the independent retained-file check.

Both final-manual normal kernel preparations pass. Independent verification
rereads all 1,564 source inputs and 431 production inputs per host, matches the
three kernels and finds the same 68,681-byte manual in both raw kernels. Raw,
pass-one and final sizes are 9,581,716, 9,679,292 and 9,810,364 bytes. Each host
rejects all three isolated old-size policies and accepts the full 16-artifact
candidate before applying only those three measured rows. The applied policy
also passes. The first Windows calibration harness stopped before any command
because the independent record used a WSL path. The corrected native path
conversion passes with separate evidence; the failed receipt remains intact.
Both final normal Make builds finish successfully. Their original collectors
fail on kernel-file metadata retained before legitimate regeneration; those
failed receipts remain unchanged. A separate verifier requires that exact
failure and a closed successful Make command, then checks all 1,564 source
inputs per host, 431 production inputs, 16 artifacts, the selected manual and
complete disk layouts. Both 200 MiB images have SHA-256
`f7fbd52ee59cb1b92914a55e745d22baface5dc723ab28d5974edffff88f8b91`.
Both canonical audits and all four conditional checks per host pass again.

Fresh normal users pass on both hosts. Independent verification checks the
complete canonical ABI, three objects and three executables per host. Objects
match across hosts; `cat`, `hello` and `ls` retain accepted executable bytes.
The ABI still has 103 fields, 101 providers and a 412-byte table. Linux consumes
all 23 files of the verified long publication without rebuilding it, with all
90 controls matching current source. Native Windows also accepts both ordinary
publications through the unchanged supported manifest-contract CLI.

All eight serial private-image four-CPU max/e1000 boots pass: kernel disassembly
and shell commands, then separate hello, ls and cat runs on each host.
Independent verification applies the complete SMP/crypto/network contract and
Makefile dynamic PID and payload hashes to retained serial logs. It checks exact
staged FAT16 users and the 62-byte fixture plus boot/kernel placement. Source,
users and base or staged images stay unchanged. The first Windows setup runs
out of space before staging or boot; its failure remains recorded. A fresh run
checks available space and forces completed-image compression before boot-time
observations, with image bytes unchanged.

The completed verified Windows base image is force-compressed with unchanged
bytes. Only the incomplete owned copy from the failed run is removed after recording
its hash and checking the exact base-image prefix. The failed receipt, driver
and log remain preserved.

Native Windows file observations can report different creation times through
pathname and descriptor APIs. The final user driver compares creation time
within each family and shares only device, inode, size and modification time
across families. Its actual reader checks include identical-byte replacement,
changed content with restored modification time, malformed JSON and wrong hashes.

Checked deterministic release drift remains pending. The Windows test-only
race build needs a hosted replacement for `getenv`; the raw Linux adapter has
no equivalent launch hook. Native drift checks do not replace those tests.

The private release revision is the c05 working-source base. It does not name
a commit containing the new implementation. Preserve failed runs and frozen
captures. Complete the coherent source/tests/docs commit and fresh committed
producer proofs before installing seeds or changing production ownership.
