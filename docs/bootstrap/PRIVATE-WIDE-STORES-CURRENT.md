# Current wide-store prototype

The rebuilt private host passes 104 native and Cupid-built case executions on
Linux and Windows. These checks cover the current v26 host and SDK bytes. The
active host adapter and fifteen installed seed files remain unchanged; the
normal image recipe still has three Python-coordinated transforms.

| Check | Case executions |
| --- | ---: |
| Real CupidObj launch, candidate capture and publication | 4 |
| Frozen executable marking and earlier drift | 16 |
| Store ranges, failure state, finish and publication | 40 |
| Precise metadata drift before read, finish or publication | 36 |
| Concurrent store calls and publication rejection after failure | 8 |

Independent rereading checks the source, SDK and program guards, subprocess
results, retained public payloads and original runtime limits. Linux keeps its
32 MiB address-space limit and 10,240-descriptor limit. Each small invocation
keeps its 60-second timeout.

## Ownership fixes

The prototype retains full-width extents and precise modification/change times.
Windows queries the retained handle through `NtQueryInformationFile`; its named
entry still supplies the identity check. Store ranges stay bounded to 64 KiB,
reads stage their result, and finish flushes and hashes the complete candidate.

Own rollback renames and links can change ctime. Their narrow refresh requires
unchanged payload, identity, extent and modification time. Foreign metadata or
replacement entries do not become a new accepted baseline. Windows also checks
the candidate before moving it back into private storage during rollback.

Making a frozen POSIX tool executable changes ctime too. The current helper
checks the complete accepted input before `fchmod`, checks it again afterward,
and refreshes only that owned change. Stable native and checked callers launch
the actual CupidObj, capture its object and publish it. Earlier modification-time
and change-time drift rejects the input. Payload writes are blocked by Linux
file seals or Windows sharing; those cases are exclusion controls, not injected
payload mutations.

## Concurrent calls and fixture corrections

Four real threads start behind a barrier. Each stable caller performs 256
disjoint writes and reads, then checks and publishes the complete 256-byte
result. A separate invalid-range case leaves the original public output intact
and prevents finish and publication.

The first native Linux fixture cannot create its fourth default 8 MiB pthread
stack under the 32 MiB process limit. A standalone probe reproduces that failure.
Its 64 KiB replacement then faults: stack-usage output identifies a 65,760-byte
digest-reader frame reached while checking the private reservation. Final Linux
fixtures use 256 KiB stacks under the same process limit. The checked caller also
checks a 4 KiB lower canary and retains its stacks through process exit. Earlier
small-stack checked results are excluded from final acceptance.

The first Linux drift observer resolves the child's `/proc/self/fd` pathname in
the parent process. Its corrected observer uses the child PID. An independent
reread also initially assumes rejected metadata drift leaves no private entries;
the corrected review records preserved entries and still checks the original
public payload. The failed fixtures and probes remain in the evidence.

## Large files and remaining work

An earlier v15 host passes all four native/checked full-file publications above
four GiB. Every byte is checked independently. Those results precede the later
rollback and executable-marking fixes. Fresh full-file replays for the current
host are running and are not accepted by this record.

The current reconstruction still needs the checked-output-to-input transfer and
unused empty-candidate transition, followed by active ABI/source-plan integration,
exact reader/import profiles and paired qualification. Native image-command
ownership and installed seed adoption remain separate gates.

The [evidence archive](evidence/private-wide-stores-current-20261109.json.gz)
contains 1,891 file records, 396 embedded payloads and 183 payload references to
the qualified producer archive. Its SHA-256 is
`331c7eae21d20111ed2ef117d427cb133ff045222ac866b979f1a4d9c1ee55a9`.
It retains the earlier rollback, large-file and metadata evidence alongside the
current source, failures, probes and accepted checks.

## Guest manual and image check

The guest manual now records these limits and the accepted source-owner cold
builds. Its guarded relink verifies every reused normal object against the
preceding accepted build. Only the raw-kernel exact-size row changes, from
9,414,748 to 9,415,688 bytes; both ELF size rows remain unchanged.

All sixteen artifact gates, seventeen policy tests and the ownership audit pass.
A strict private four-CPU max/e1000 boot passes all 62 TLS checks and ordered
JIT/terminal completion. The fifteen installed seed files remain unchanged.
This is a manual relink with existing installed tools, separate from the accepted
cold replacement-tool builds. The
[manual/image evidence](evidence/private-wide-manual-image-20261109.json.gz)
retains the complete new manual object, linked kernel products, guards and
serial log. Its SHA-256 is
`8c6d86589aa00f36c7a3335108a7a266b45f21d9e7454ec4e2cc805366682aa1`.
