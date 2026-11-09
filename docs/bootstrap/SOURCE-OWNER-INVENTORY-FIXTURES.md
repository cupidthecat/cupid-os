# Current source-owner test inventories

The prospective seed checks exposed historical fixture expectations. The fixes
retain exact inventories, generated/checked equality and negative-path checks.
They change no producer implementation or installed seed file.

The Windows behavior fixture now selects observer workers from the exact seed
provenance through the existing profile helper. Its negative long-path case
keeps that worker selection so it reaches the intended plan mismatch. The
original fixture omitted the worker flag and failed before checking tool bytes.

Thirteen added source files explain the include and line-source differences;
six owner modules explain the hosted corpus increase. The conditional fixture
also needs two real expressions and twelve additional `_WIN32` occurrences.
Both new expressions evaluate false in its freestanding i386 profile.

| Inventory | Previous fixture | Current source |
| --- | ---: | ---: |
| Include/line source files | 745 | 758 |
| Direct includes | 2,608 | 2,640 |
| Hosted Linux translation units | 53 | 59 |
| Complete tracked translation units | 422 | 428 |
| Conditional expressions / directive pairs | 61 / 64 | 63 / 66 |
| `#if` / `#elif` occurrences | 452 / 24 | 472 / 24 |

Both complete 65-test selections pass against the prospective cohort; Linux
retains three platform skips. Fresh CupidC/CupidLD builds pass all four selected
conditional success, error and scale modes on each host. Their object and
executable bytes match. Existing-cohort Windows checks also pass after correcting
the remaining historical count in the summary-string assertion. Initial failures,
the missed second assertion and corrected replays remain recorded.

The [evidence archive](evidence/source-owner-current-inventory-fixtures-20261109.json.gz)
retains 257 file records, 129 local payloads and 123 qualified-archive references.
Its SHA-256 is
`dee2625a691b8fff49609d139dc2eafb4f621bfd8b8e213698c33989e3c1bbd2`.
Complete repository replays and their other failures remain open. These focused
results do not authorize installed seed adoption.
