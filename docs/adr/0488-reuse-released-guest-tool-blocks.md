# Reuse released guest tool blocks

- Status: Accepted for the opt-in guest tools
- Date: 2026-11-17

CupidC's frontend reuses temporary arena and buffer sizes throughout a
single job. Each buffer used to go through the kernel heap on allocation
and release. Guest instruction samples repeatedly landed in the heap's
block scans.

Keep a bounded cache in the shared guest adapter. Each slot holds at most
one released block and requires an exact size match before reuse. The
adapter returns blocks on eviction and drains the cache before opening
the output. It also drains and retries once if a fresh allocation fails.
Both workers release cached blocks when their job ends. If a worker is
killed, its scheduler lease still owns the underlying kernel blocks.

The complete native frontend replay retained the same output bytes and
reduced underlying allocations from 612,950 to 295,646. Increasing the
arena block size alone reduced the count to 531,349. Keep the existing
arena block size with the cache.
