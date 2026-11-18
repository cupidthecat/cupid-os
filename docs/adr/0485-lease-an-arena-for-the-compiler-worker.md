# Lease an arena for the compiler worker

- Status: Accepted
- Date: 2026-11-17

Reserve 30–34 MiB for the standalone compiler ELF. Give it a distinct
lease so an ordinary external program can use its own arena at the same
time. The loader applies the same segment and entry checks to both arenas.
It stages the segment data before claiming the lease.

The worker needs a two-MiB scheduler stack. Its syscall table preserves
the existing layout and gives the worker allocation and free callbacks
that track live blocks. The callbacks require the running lease owner.
Normal frees return blocks immediately. The reaper frees outstanding blocks
after CPU detachment, then releases the compiler lease.

A compile publishes through a same-filesystem rename. Killing a worker
during publication can leave its pending file. Inspect and remove that file
before retrying if it remains. The compiler publishes one object per
invocation.
