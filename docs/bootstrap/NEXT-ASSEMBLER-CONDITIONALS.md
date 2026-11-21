# Definition conditionals

The shared assembler implements the semantics in
[ADR 0404](../adr/0404-assemble-definition-conditionals.md). This is the prerequisite
for a selective Windows UTF-8 startup entry. The native verifier integration is
preserved separately, including its observer batching and diagnostic changes.

Capability acceptance is complete: canonical staged proofs, Linux publication
and paired image/user/runtime checks pass. Installed seeds are unchanged; actual
commit-byte verification and seed promotion remain separate. The records below
retain the intermediate failures and corrections.

Retained evidence is under `build/bootstrap/asm-defined-conditionals-v4-bd13198b/`.
The earlier uninitialized-state and local-name failures remain in the v1/v2
directories. Both checked builds and their 23-case CLI suites pass. Each checked
C contract passes thirteen modes and four include cases. `paired-checked.json`
rehashes the inputs, objects and programs and records byte-identical startup
selector outputs. Only host-independent objects are compared across hosts;
the CLI main object has host-specific publication code.

Run `python -m unittest tests.test_toolchain_cupidasm` for the active CLI suite.
The `conditionals` mode of `cupidasm-contract` checks the C API. Make's contract
target includes this mode. Both staged bootstrap paths also exercise successful
conditional selection and failure with previous-output preservation.

Both hosts pass the 49-method CLI suite, with four platform skips. The v4 staged
proofs pass independent verification: 35 Windows and 32 Linux final artifact
pairs match. Both kernel builds and the Linux image build pass. Publication, paired OS and
runtime acceptance, and seed promotion were still pending at that point. The twelve Python-owned
build operations have not moved.

Before parking the verifier draft, its full CLI suite ran 148 tests per host.
Concurrent runs shared temporary-root observations and reported five Windows and
one Linux reservation-directory failures. All affected tests pass sequentially.
The Windows replay also caught a signedness warning after introducing the typed
diagnostic enum; its explicit unsigned serialization is saved with the draft.
Original logs and both draft snapshots remain under
`build/bootstrap/native-artifact-integration-bd13198b/`.

The v3 checked lexer probes reproduced carriage-return whitespace and directive
prefix bugs. V4 uses the normal lexer's identifier boundaries and whitespace.
Its regression cases cover both. The v3 staged and kernel processes were stopped
for this source correction, with all partial outputs retained; they are not
acceptance evidence.

The v4 compiler snapshot contained CRLF bytes in two inputs that Git stores as
LF. Those working-copy mismatches are corrected, and v5 repeats the staged
proofs against the exact source bytes intended for the commit. Its source
snapshot is `7a1a58ce65141b59ec779f0ef6ccb5b7e97e8a54df4732cc27bf7a5bee28e6ed`.
Publication inputs are checked separately. Files whose committed content already
contains CRLF retain those bytes; a clean-filter result alone does not describe
the stored blob. The retained reports distinguish v4 acceptance, v5 source
proofs, and the final manual rebuild still required.

Both v5 staged proofs now pass. Independent verification rehashes all 66 inputs
and the retained artifacts on both hosts. All three generations match v4 byte
for byte: 35 artifacts per Windows generation and 32 per Linux generation.
Only the two recorded CRLF-to-LF source corrections distinguish their input
inventories. The paired report is `paired-staged-verification.json` under
`build/bootstrap/asm-defined-conditionals-v5-bd13198b/`; the two
`*-canonical-proof-comparison.json` reports bind the cross-run comparisons.
Publication and final OS/runtime acceptance were still required at that point.

V5 publication now passes all thirteen assembler modes. Independent verification
rehashes its 80 inputs and 22 artifacts and compares the artifacts and 33 contract
records with v4. All match. The publisher also verifies the full set of 65 stage
pairs: 33 contract and 32 tool-bootstrap pairs. The final publication manifest
is `32720a78e7352a6680f2309169861206b615e147bd447f913e52bb418fd0f32f`.

V4 Linux publication passes its thirteen assembler modes. The Linux runtime
smoke then exposed an early setup-success match: `ls` arrived while disassembly
was still running. The captured screen confirms the terminal rejected it as
busy. Original failure reports and successful and failed diagnostic replays are
preserved. The shell now supplies a serial command-completion marker, and the
smoke harness can require it before advancing. Its regression suite passes;
fresh paired OS acceptance must verify this synchronization change together
with the canonical sources and embedded manuals. The failed prior run is not
relabelled as accepted.

The fresh completion-marker kernel builds pass on both hosts. Independent
verification covers 1,501 source inputs, sixteen artifacts and 431 link inputs
per host; all paired artifacts and link inputs match. Five link inputs changed
from v4: the shell, assembler manual, generated symbols and two kernel ELFs.
The HolyC manual object stays identical because CupidObj normalizes its changed
line endings. An initial verifier expected that object to change; its failed
report is retained alongside the normalization proof and corrected verifier.

The flat kernel measures 9,566,560 bytes. Its exact-size policy now records that
measurement. Paired image and user-program builds and required-completion-marker
runtime checks now pass. Independent verification rehashes the frozen files,
artifacts, link inputs, transferred publication and user outputs. Both
209,715,200-byte images have SHA-256
`e5f4ddc67697067acf1d209ccc51d484f7868957d6d125fe4ba01b091906ee82` and remain
unchanged through the four-CPU smokes. `paired-acceptance.json` under
`build/bootstrap/asm-defined-conditionals-final-bd13198b/` records the result.
Final documentation is audited separately from the frozen build inputs;
actual commit-byte verification is still required before seed promotion.

## Committed capability and seed installation

Capability commit `e4f2ed652e756b1abb375ec061923f5259799e01` is pushed.
`actual-commit-inputs.json` checks all 1,501 committed files, the two LFS payloads
and exact 105-input tool closure against the accepted OS/runtime evidence.
The four final documentation updates are explicitly reconciled with the frozen
build snapshot. The preceding pending-commit notes record the earlier state.

The strict promotion preview and installation pass against that commit and the
canonical v5 proof directories. Both six-tool manifests now name its 66-input
snapshot. The installed Linux manifest is
`da26556401dd20d039ed1175f3bf857c4c8bebd50fb52b3bd75a06b95fdf41ed`; Windows is
`c715ce354c28b97c6b9c4e5702c98d368d07dff9e52bc2f0deb71ab3194d2395`.
The twelve payloads, parent lineage, plans, release record, retained pins,
manifest-contract digests and size policy were updated together. Both installed
manifest verifiers pass. Full promotion regressions and fresh OS acceptance
for the updated embedded seed reference remain in progress; this installation
is not yet a committed release.

The first full Linux promotion suite found two stale assumptions: the publication
validator still pins the preceding seed build plan, and the legacy test fixture
removes three promoted modules rather than six. Isolated corrections restore
the hosted validator and exact negative-test diagnostics. Corrected snapshots
are now under test on both hosts. The Linux fixed-point timeout is also being
rerun from its native filesystem. None of these pending checks count as release
acceptance; the original failures remain in the evidence directory.


The first promoted-seed kernels now pass on both hosts. Their sixteen artifacts
and 431 link inputs match. Three link inputs differ from the capability build:
the embedded CupidC manual object and the two kernel ELFs. The symbol object
stays byte-identical. The first verifier wrongly required it to change; the
corrected verifier passes, and the original failure remains in the evidence.

The corrected Linux suite completed 210 tests with one failure and seventeen
skips. The remaining assertion pinned the preceding Windows disassembler hash.
Its observed 517,120-byte output matches both the installed seed and the retained
paired stage-four proof. The assertion now records that independently verified
hash. The original Windows suite finished with both full fixed-point tests
exceeding their 3,000-second limits; neither is counted as accepted. New full
suites run on both hosts with every assertion and the original time limits.

The active corrections include the publication seed-plan pin, legacy source
selection, historical and promoted import-profile fixtures, and the disassembler
image identity. Fresh OS snapshots contain those exact corrections. Their
kernel checks require every artifact and link input to remain identical to the
first promoted-seed build before image/runtime acceptance. Linux acceptance
requires the corrected publication. Evidence is under
build/bootstrap/seed-promotion-final-e4f2ed65/; regression replays remain under
build/bootstrap/seed-promotion-e4f2ed65/. Seed promotion is still uncommitted.

## Corrected full regression coverage

The latest Linux run passed 210 tests with seventeen platform skips. The
corresponding Windows run completed with two stale fixture assertions: the
native runtime-contract executable hash and a stage count that omitted three
assembly objects. Independent retained-output checks reproduced both failures
and established the corrected values. Both complete Windows fixed-point
methods then passed together in 5005.732 seconds with the original time limits.
The original failed run remains part of the evidence.

`combined-recovery-v6-regressions.json` accounts for all original methods and
both corrected full replays, with exact source comparison for unaffected tests.
The fixture corrections and input preflight pass. Linux publication also passed:
its report records 80 inputs, 22 artifacts and all thirteen published assembler
contract modes. The fresh Windows kernel build passed, with its source inventory
rechecked and disk images unchanged. The Linux kernel build is running. Paired
kernel comparison, OS/runtime acceptance and committed-byte verification remain
pending.
Current acceptance reports are under
`build/bootstrap/seed-promotion-v6-e4f2ed65/`. This promotion is not yet committed.

## Artifact-reader correction, 2026-09-24

The paired kernel comparison and measured policy passed. Windows image
acceptance then exposed an older C artifact-reader limit: it rejected the
promoted 66-input manifests. The fix admits that count with its exact
Linux/Windows plan pair; mixed generations and unrelated counts are rejected.
All 65 hosted policy/API tests and 23 runner tests pass (four runner skips).
Cupid-built contracts pass all sixteen artifacts on both hosts. Windows and
Linux image and user builds and four-CPU disassembly/shell smokes pass. The
independent paired audit rehashes all 1,501 source inputs, sixteen artifacts,
431 link inputs, three user programs and each image. Both hosts produce
identical images and user programs. Each private smoke preserves its source
image and completes disassembly and ls without panic or corruption markers.
Current reports, including paired-acceptance.json, are under
`build/bootstrap/seed-promotion-reader-fix-e4f2ed65/`; the earlier failed reports
remain intact. Commit `88bb2d26` is pushed; its audit verifies all 1,501 committed inputs
and both Freedoom LFS payloads.
