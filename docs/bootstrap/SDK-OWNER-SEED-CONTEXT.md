# SDK policy for captured source-owner seeds

The native SDK manifest policy now accepts the exact 44-source owner plan in
captured incoming-seed contexts. Its candidate-publication rules already
supported that plan. The missing incoming-plan entry stopped both full ordinary
SDK builds after all stage pairs and runtime checks had passed.

The shared seed reader still validates the selected cohort before capture. The
C policy checks the bounded context, raw manifest digest and six ordered tool
identities. It then checks the publication inventory and every stage pair.
Historical plans, classic request formats and their pins remain unchanged.
No completed manifest or new parent hash is embedded in the policy.

## Failure and correction

Both original full builds finish all 84 final-stage pairs and the hosted runtime
contract, then reject the author request with `captured seed context differs`.
The retained stage products remain converged; neither run publishes an SDK.

A focused native fixture reproduces the same error in about two seconds. Its
eight author/check profiles reject the owner context on each host. Changing only
the context plan to an older allowed value makes those diagnostic requests pass.
That deliberately altered context is not a valid shared-reader capture and is
never used as a production workaround.

The fix adds the exact owner digest to the incoming-context allowlist and shares
its constant with the existing candidate-plan selection. Source counts, tool
membership, ordering, identity checks and request bounds keep their contracts.

## Checks and scope

Both 186-method native-policy/Python selections pass. Actual stage-four Cupid
tools also build the native-format author and pass all 73 historical and new
policy methods on each host. The total is 518 selected method executions, with
three declared Windows skips. Positive cases cover every publication profile;
negative cases retain unknown-plan, changed manifest/tool facts, membership,
ordering, truncation and raw-byte checks.

The source audit is regenerated for the changed contract input and passes its
consistency check. Transform ownership remains 449 of 452 in the normal graph.

The first broad host selections run concurrently against the same protected
repository root. Their temporary publications trigger the root-change guard.
Sequential replays pass with the same guards and timeouts. The failed selections
remain recorded.

This contract source is outside the 114 qualified producer inputs. Independent
rereading confirms those inputs are unchanged, so the six-tool qualification
remains separate from SDK policy acceptance. All fifteen installed seed files
remain unchanged. Four fresh ordinary/long SDK builds pass in isolated checkouts
with the corrected policy and original timeouts. Their
[complete publication acceptance](SOURCE-OWNER-SDK-PUBLICATIONS.md) includes all
336 final-stage pairs and four actual author replays. Seed adoption remains open.

The [policy evidence](evidence/sdk-owner-context-policy-20261109.json.gz)
retains the original full failures, 168 converged pairs, focused requests,
diagnostics, fixed sources and accepted native/Cupid-built selections. It has
478 file records, 105 embedded payloads and 54 references to the qualified
producer archive. Its SHA-256 is
`13641b65b423fc282748274d39bfa86cc7e2bcc47616b0123b5bd401d3708102`.
