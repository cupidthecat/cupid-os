# Frontend inventory and hosted-header checks

The frontend's five stale inventory assertions now match the source audit.
A per-file scan of all 418 changed audited sources accounts for every delta:

| Keyword | Previous count | Current count |
| --- | ---: | ---: |
| `return` | 28,579 | 28,598 |
| `for` | 5,094 | 5,097 |
| `while` | 3,153 | 3,163 |
| `if` | 46,062 | 46,085 |
| `else` | 5,794 | 5,800 |

Only four files add these occurrences: the help parser repair, the IR and
object contracts, and the shared C spelling fixture. The fixture is also the
only added header in this sweep. Its presence is now asserted explicitly.

## Keep the ABI profiles distinct

The kernel sweep still visits all 193 headers. It retains 188 successes and
the four preceding expected rejections. The added hosted fixture also rejects
that mixed profile: hosted libc headers and kernel `string.h` supply different
`NULL` definitions. The exact diagnostic remains checked. Preprocessing is
unchanged.

A new positive compiles the same header twice through each checked host
compiler with the hosted include profile. All four validated ELF32 objects
match: 9,100 bytes, SHA-256
`013e5ad5d3b95e235b07244cdec25ef407087e62a40f9447494609dd19c3bd7b`.
The header and compiler source are unchanged.

The complete frontend module passes all 99 tests on Windows and native Linux.
Each host also passes the six focused methods and rejects nine mutations:
six inventory changes, a reserved pointer-width override, missing hosted
includes and a corrupted ELF class. The full `make check-bootstrap-audit`
entry point passes on both hosts. AST review leaves 96 existing methods
unchanged, changes five inventory methods, adds the hosted positive and removes
none.

The [profile evidence](evidence/frontend-inventory-profiles-20261110.json.gz)
retains 48 file records and 45 payloads, including the original failures,
source deltas, ABI diagnostic, actual checked objects and full passing logs.
Every exported payload passes size and SHA-256 readback. The archive has
2,145,119 bytes and SHA-256
`c367b2246dbbb8409b973dbcfab0d1d30360896b51cea385deac31729903798c`.
Seeds, production sources and build ownership remain unchanged.
