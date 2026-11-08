# Worker completion and failure handling

The Windows source-owner prototype now publishes completion from its adapter
after the callback returns and after the last descriptor read. A caller can
then release the descriptor and callback context, including after a failed
wait. Ordinary successful waits and handle closes remain in place. This work
does not adopt the image command or change installed seeds.

The checked descriptor has three i386 words: callback, argument and completion
pointer. The adapter rejects missing required pointers, preserves the callback
return value and uses XCHG to publish completion. The native C adapter follows
the same ordering. The public hosted Windows header also defines `WAIT_FAILED`
with the documented DWORD value; the permanent callback fixture checks a real
invalid-handle wait against it. See Microsoft's
[wait contract](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject).

## Evidence

Windows passes all 45 permanent heap/runtime methods. Linux passes nineteen
with 26 Windows-only skips. Four ownership/input tests and the tracked graph
regeneration and consistency check pass. The [portable evidence](evidence/worker-completion-20261108.json.gz)
retains 1,083 files, including complete receipts, source controls, checked
products, prior failures and the exact one-line SDK projection.

The old adapter fails the new completion contract with exit 8 after a callback
returns without publishing its marker. The revised adapter passes 513 callback
calls and three rejected invalid descriptors. Each round also checks that
publication of callback data can precede callback completion, keeping those
two boundaries distinct. Checked objects and images pass strict disassembly
and complete PE import validation.

Twenty-four controls exercise the real entry-validation and original-parent
pools, equally split between native and Cupid-built callers. Each pool covers
normal operation, creation failure before any worker, creation failure after
three and seven workers, a failed wait and a reported close failure. Creation
failures finish untouched rows serially and retain valid views. Wait and close
failures clear the view, record an observer error and permanently poison the
owner; a repeated view stays cleared and rejected.

The wait control deliberately holds its delegate after the validation body
returns. It requires three completion polls before releasing that delegate.
Every created handle is closed, and the close seam checks completion before
context release. All 259 source files retain their bytes. These fixtures use
dummy required files and prove pool behavior rather than image authority.

The complete 4,096-parent UNC capacity case also passes with the new protocol
in 52.735 seconds under its original sixty-second deadline. The Linux host
object remains byte-identical to the previously qualified joined observer
object: 314,664 bytes, SHA-256
`970402170ce921ca17914bfdb649b99ea7cd43701e0ba8c43f6b4686c8c2ce1c`.

## Remaining work and retained failures

The first failure-shim build exposed the missing Windows SDK `WAIT_FAILED`
constant. The SDK now represents it directly. The initial native recorder
expected LF output; the CRT emitted CRLF. The corrected recorder preserves the
raw output and normalizes native line endings only for its text comparison.
Checked output keeps its exact LF comparison.
The first audit after tracking the new fixtures also exposed two missing
source-ownership entries. Both fixtures now have explicit `not_reached`
records: they run through the unit harness rather than a supported Make root.
The audit is regenerated and checked with those files tracked.

The next gate replays all existing observer, request, selection and legacy
contracts against the combined worker and heap candidate. Publisher custody,
large-image publication, producer qualification and replacement-seed adoption
remain separate gates. The normal graph still has three Python coordinators.
