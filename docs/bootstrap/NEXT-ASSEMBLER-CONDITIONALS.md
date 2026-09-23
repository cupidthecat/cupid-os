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
