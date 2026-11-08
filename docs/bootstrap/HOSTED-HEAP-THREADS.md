# Hosted heap serialization

Source head adds `cupid_runtime_heap_enable_threads()` in `<cupid_runtime.h>`.
Call it on the original thread before creating concurrent heap users. Once
enabled, the four allocation functions serialize access to their shared
metadata for the rest of the process. Ordinary hosted commands keep the
default single-threaded mode. Installed bootstrap seeds remain unchanged.

The wrappers call the existing allocator cores under one mutex. Internal
calloc and realloc operations use unlocked helpers, preserving allocation,
alignment, bin search, splitting, coalescing and OS-failure behavior without
recursive locking. Streams, process state and errno do not become thread-safe.
This API does not create or join threads.

## Verification

Windows passes all 45 selected methods. Linux passes nineteen, with 26
declared Windows-only skips. The [portable evidence](evidence/hosted-heap-threads-20261108.json.gz)
retains 454 files: complete receipts, source controls, generated test entry
points, strict checked products, repeated runtime objects and earlier failures.
The [audit receipt](evidence/hosted-heap-threads-audit-20261108.json.gz) retains
the regenerated graph, consistency check, three closure tests and repeated
capacity cases. Independent source comparison preserves all four original
legacy fixtures, the allocator-core projection and all fifteen installed seed
files.

Run `python -m unittest discover -s tests -p test_hosted_heap.py -v` on each
host. The suite exercises the existing density, live-neighbor, mixed-realloc,
overflow, zero-size and OS-failure cases with heap serialization both disabled
and enabled. Linux retains its 32 MiB runtime bound. Windows also exercises
both ordinary and UTF-8 startup, including Unicode paths beyond 260 characters.

The Windows concurrency contracts use eight checked i386 callbacks and 32,000
allocation iterations. They check sixteen live slots per worker, alignment,
calloc clearing, successful realloc data, failed realloc preservation and
neighboring payloads. A test-only hook detects overlapping allocator entry
before metadata mutation. The disabled control reports overlap; the enabled
caller passes both that exclusion check and the uninstrumented stress case.

The test callback adapter separately checks 513 calls, return values, stack
cleanup, data publication before join and two rejected invalid descriptors.
All objects and images pass strict disassembly. Thread fixtures add exactly
CreateThread, GetExitCodeThread and WaitForSingleObject to their existing
Windows import profiles and validate the complete PE import set. Conventional
host producers remain forbidden in checked invocations.

## Why this work was needed

The complete source-owner prototype rejects a valid 4,096-parent UNC request
by timing out at its original sixty-second bound. Reusing one payload metadata
query saves only about four percent of the diagnostic metadata calls and does
not resolve capacity. Parallel metadata-entry checks alone still time out
after `ready`; the original-parent checks remain serial.

The private group candidate preserves both original-parent resolutions and
both leaf metadata checks. It runs independent parent groups concurrently
only when the caller supplies no callback. Native Windows completes the full
UNC request in 56.174 seconds. With the synchronized hosted heap, the Cupid-built
caller passes in 48.986 seconds; Linux passes in 2.797 seconds under 32 MiB.
The repeated Windows UNC parent case passes in 53.038 seconds. Flat inputs
also pass on drive, UNC and POSIX paths; the Windows parent drive case passes.
Those fixtures use dummy required files and prove capacity, not seed authority
or image publication. Worker failure controls and complete regression replay
are still required before integrating that source-owner candidate.

## Retained failures and remaining work

An early private runtime snapshot lacked the relative path-encoding header
needed by UTF-8 startup; its corrected snapshot preserves the include spelling.
The first overlap probe left its hooks enabled during runtime cleanup and
timed out. The permanent negative control exits directly after detecting
overlap, keeping deliberately suspended workers out of allocator cleanup.
One integration run caught a harness edit during guarded execution;
another rejected a log placed in a directory required to start empty. Fresh
complete runs use frozen inputs and separate log and product directories.
The graph audit also rejected edits to its pinned legacy fixtures. The final
tests retain those original files and enable threading through generated test
entry points that include their unchanged bodies.
The exact build-graph input lists also gain the new runtime header; the
producer snapshot now contains 100 source inputs. No prior input is removed.

This source change does not promote a producer cohort or retire a Python
coordinator. Full producer qualification and replacement-seed adoption remain
separate gates. The normal graph still has 449 CupidBuild transforms and three
Python coordinators. TempleOS remains read-only reference material.
