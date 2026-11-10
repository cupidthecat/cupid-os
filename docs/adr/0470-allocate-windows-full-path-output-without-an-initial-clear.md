# ADR 0470: Allocate Windows full-path output without an initial clear

Date: 2026-10-11

Status: Implemented; checked host contracts and normal-root verification pass

The checked Windows full-path adapter allocated 32,768 UTF-16 units with
`calloc`. A debugger probe of the complete private input caller found most
mapped samples inside the runtime's scalar `memset`. A second probe captured
the callers and arguments: 446 samples showed `calloc` clearing 65,536 bytes
under `cupid_windows_get_full_path_name`.

Use `malloc` for that output buffer. Keep its size, allocation order, API call,
length checks and conversion rules. `GetFullPathNameW` writes the successful
result, and the adapter consumes only the returned unit count after checking
that it is nonzero and smaller than the buffer. Failure and oversized-return
paths do not consume the buffer. Other allocations retain their current rules.

The checked contract poisons fresh storage before the mocked API writes its
result. It checks output bytes, a file-part offset, guard bytes, both allocation
failures, API errors, oversized results, malformed UTF-16 and small output.
The existing wider adapter matrix also checks long-path behavior, role imports,
wide startup and conflicting roles.

Four serialized capacity calls retain 4,096 stages, 49,152 payload bytes and the
60-second bound. Both preceding and candidate checked callers pass drive and
UNC paths. The UNC child user-CPU total falls from 7.906 to 2.781 seconds,
while candidate elapsed time remains 56.130 seconds. The lower user-CPU total
supports removing that clear. Complete-owner and speed qualification remain open.
The preceding unchanged owner still has preserved UNC timeouts.

The change introduces no path cache or relaxed source checks. Root and parent
binding, full leaf metadata, payload custody and provisional callbacks keep
their existing contracts. The complete private owner stays outside normal
image discovery and publication. Installed execution seeds are unchanged.

The measured cost belongs to output storage whose successful contents come
from the API. The change therefore stays in the Windows path adapter.

Windows documents the API's successful output and returned-length contract in
[GetFullPathNameW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfullpathnamew).

[The acceptance record](../bootstrap/NATIVE-FULLPATH-STORAGE.md) contains
the complete measurements, limits and preserved failed attempts.

The separate [OS manual acceptance](../bootstrap/FULLPATH-STORAGE-MANUAL.md)
passes complete builds on both hosts and normal-root verification. Its measured
kernel policy and all six strict four-CPU boots pass. The 84,889-byte manual
records this allocation's scope; complete native input ownership and publication
remain open.
