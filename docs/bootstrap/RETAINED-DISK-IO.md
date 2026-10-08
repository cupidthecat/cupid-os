# Retained disk-image I/O prototype

The separate prototype connects the native disk composer to an existing wide
publication transaction. `cupidbuild_disk_io` owns its callback frame and
borrows the host transaction until adapter close. It uses the constructor's
registered frozen boot input, captures the kernel and checked template under
explicit geometry bounds, captures the complete previous output, and opens a
fixed-extent candidate store before returning any views. Transfers use retained
handles and complete 64-bit byte offsets. The adapter allocates its fixed frame
and small transfer buffers rather than a complete image.

The boot view exposes exactly its first five sectors. Its original capture
still retains the complete ordinary input and digest. Every source callback
checks the advertised extent before retained I/O. Present sources accept
zero-count reads through EOF, including a null destination, and still validate
retained metadata. An absent previous output has no available source view.
A callback failure latches the adapter and rejects further callbacks and finish;
the host store continues to own its open/finished phase authority. The caller
must discard the complete borrowed transaction after any failed composition or
staging.

Each captured pathname is copied immediately into the owned callback frame.
The host input table can move during capture or later growth, so retaining its
returned pointer would leave a dangling view. A 528-slot growth control keeps
every exposed view valid. Candidate callbacks widen the sector before
multiplying by 512. The sparse high-sector control writes distinct markers to
sector 1 and sector 8,388,609, reads both, and verifies that the high marker at
byte offset 4,294,967,808 does not alias the low sector. It closes the unfinished
store without composing or hashing a complete 4 GiB image.

## Evidence and repairs

The first native Windows selections fail because the kernel/template paths
were relative and the boot input was captured twice. The host correctly rejects
the duplicate file identity. The corrected request accepts the already
registered frozen boot input and absolute kernel/template spellings.
Receipts `disk-io-native-windows1`, `windows2` and the focused `windows3` retain
those failures; the repaired thirteen-method selections pass on both hosts.
The same thirteen methods pass through matched Cupid-built callers in
`disk-io-checked-small-windows1` and `disk-io-checked-small-linux1`.

Review finds a boot callback that initially checked only the complete captured
extent, so a longer boot input could be read beyond its five-sector view.
The repair checks the view first and preserves the caller's buffer on rejection.
The new controls use a 3,072-byte capture and cover exact prefix bytes, valid
zero-count reads, crossing-prefix reads, nonempty reads at EOF and zero-count
reads beyond EOF. A second review finding adds the positive sparse high-sector
control. All eighteen methods pass through native callers on both hosts and
through matched Cupid-built callers in `disk-io-checked-small-windows2` and
`disk-io-checked-small-linux2`. Build bounds remain 360/120/180 seconds with two
workers; every runtime call retains its 180-second bound.

Four deliberately defective native bridges also fail the intended controls:
32-bit arithmetic in the candidate reader, writer, both callbacks, and a source
reader without the advertised extent check. The last variant fails all three
prefix rejection methods. Evidence is `disk-io-regression-controls-linux1`
and `/var/tmp/cw8/disk-io-regression-controls1/closed.json`.

The final header scopes zero-count reads to present source views. A nineteenth
method verifies that reading an absent previous view fails, leaves the public
output absent, and rejects adapter finish. All nineteen methods pass through
native Windows and Linux callers in 86.416 and 29.359 seconds, respectively.
Matched Cupid callers pass in 175.143 and 174.114 seconds. Receipts are
`disk-io-native-windows7`, `disk-io-native-linux7`,
`disk-io-checked-small-windows3` and `disk-io-checked-small-linux3`.
The matched builds retain the qualified compiler, actual compiled inputs and
original process bounds. Both reviews close without further findings.
A mistaken PowerShell launch
of a Python helper ran no tests; its failure is retained separately. The correct
Python launchers run the complete selections.

## Ownership remaining

The fixture compares complete small images against the independent Python
oracle, preserves FAT payloads and tail data, checks force-format and equal-byte
reuse, and exercises geometry, range and finish rejection. Its guarded
publication proves bridge byte parity. The separate
[required-file publication caller](RETAINED-DISK-PUBLISH.md) connects this bridge
to the read-only disk and FAT16 validators, then the guarded publisher. Its 24
methods pass through all four native and matched callers; independent rereading
checks every positive image byte and all corresponding outputs.

Paired seed and template-producer trust, optional discovery and the normal CLI
remain separate. The normal image recipe remains Python-owned. The prototype
changes none of the installed 99 producer inputs or seeds. Its new headers must
be included in an expanded source snapshot before qualification. Source
integration, exact new Windows import approval and complete producer
qualification remain separate work.
