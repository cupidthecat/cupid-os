# Borrow external observers through publication

The current private wide adapter can retain observer authority for input roots
outside its repository. A transaction first binds its primary observer, then
borrows each distinct external observer once. The caller keeps every observer
alive through transaction close.

The new include holds the external authority checks. The transaction accepts up
to 528 distinct observer pointers and physical roots. An explicit requirement
checks the exact primary and an already borrowed selected observer; it creates
no new observation or input authority. Failed, unbound, duplicate, temporary
selection, active-capture and late requests permanently prevent publication.

Every publication boundary checks external root metadata, captured payloads,
first absences, memberships and original source-root requests. The primary
observer retains its existing root-identity rule for owned namespace writes.
External payload freezing and complete image-command integration remain open.

## Acceptance

| Selection | Native/Cupid-built executions across both hosts |
| --- | ---: |
| Clean authority, exact selection and drift checks | 88 |
| Stable publication and external edits after installation | 16 |
| Actual capacity and original requested-root rebinding | 20 |

All 124 executions pass independent review. Linux retains 32 MiB and 10,240
descriptors; each case retains its 120-second limit. The four capacity cases
actually borrow and validate 528 roots, then reject the 529th and prohibit
publication. Normal Windows builds retain only the two wide-store additions.

The diagnostic publisher rereads all 8,193 newly installed bytes before an
external edit or parent-controlled alias change. The final guard rejects the
change, and the actual publisher restores the previous bytes, identity and
modification time or restores an absent output. Stable controls publish through
each route. Rebinding changes only the original alias; both physical source
directories and their payloads stay unchanged. The tests require the exact
original-root binding diagnostic.

Removing the three diagnostic hook insertions reconstructs the complete clean
host. Existing store modules, the open module, SDK and Windows bridge are
unchanged. Independent review rereads retained payloads and namespaces and
checks the guarded runners' identity/time assertions; copied Linux fixtures do
not preserve their original inode numbers.

## Failed fixtures and remaining work

The first Windows writer cannot edit the retained file through secure CRT
sharing. A standalone probe confirms that conflict; explicit compatible Win32
sharing lets the corrected fixture perform the edit. A later checked fixture
calls `getchar`, which remains absent from the pinned SDK. Its acknowledgment
protocol now reads a complete newline through `fgets`; this does not adopt the
separate byte-input library capability.

The first independent checker also tried to resolve WSL symlinks through
Windows file APIs. The corrected check reads those same links on Linux while
retaining native Windows junction checks. Failed fixtures, compile outputs and
the failed review remain in the archive.

All 114 producer inputs, installed seeds, current OS bytes and normal ownership
counts remain unchanged. This accepts observer authority for the current private
wide adapter. It does not capture external payloads into frozen storage, finish
native image discovery, adopt a new producer cohort or replace the three Python
coordinators.

[Evidence archive](evidence/private-wide-external-observer-authority-20261109.json.gz) SHA-256:
`0b8458ee0505ef7cb78a5aea9439a3dec77c0071dc21f917aa6ff6b76317f53b`.
