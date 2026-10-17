# Hosted wide file I/O notes

Research for the private disk-image handoff, 2026-10-06. These findings do not
qualify a changed runtime or transfer production ownership. The composer already
uses unsigned 64-bit lengths and offsets; its stdio caller currently stays below
two GiB. [ADR 0434](../adr/0434-compose-preserved-disk-images-through-bounded-source-views.md)
keeps retained host adapters separate from image composition.

## Windows

Reuse the existing `cupid_windows_set_file_pointer` shim with a nonnull high-word
pointer. `SetFilePointer` combines the input high and low words into one signed
64-bit displacement and replaces the high word with the resulting position's
high word. `SEEK_SET`, `SEEK_CUR` and `SEEK_END` correspond to origins 0, 1 and 2.
Negative relative displacements are valid when the resulting position is
nonnegative; a negative result fails with `ERROR_NEGATIVE_SEEK` and leaves the
position unchanged. Query the current position with zero displacement and
`SEEK_CUR`. [Microsoft API contract](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfilepointer)

A returned low word of `0xffffffff` is valid with a nonnull high pointer.
Failure requires both that low word and an immediate nonzero `GetLastError()`.
Microsoft explicitly states that the API sets `ERROR_SUCCESS` on this ambiguous
success path. The caller need not clear last error or add a `SetLastError`
import. A different low word proves success even if last error remains nonzero.
[Microsoft's explanation of the success rule](https://devblogs.microsoft.com/oldnewthing/20070711-00/?p=26063)

`SetFilePointerEx` returns an unambiguous Boolean status and takes a
`LARGE_INTEGER`, but needs a new import and shim. Its documented `FILE_BEGIN`
interpretation is unsigned, so a common signed API would have to reject negative
absolute offsets before calling it. Reusing `SetFilePointer` keeps the current
import boundary. [SetFilePointerEx contract](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfilepointerex)

## Linux i386

The syscall number is 140 and the five arguments are `fd`, `offset_high`,
`offset_low`, a writable eight-byte result pointer, and `whence`. The raw syscall
returns zero on success and writes the position through that pointer; failures
return negative errno values. Use a local result and publish an output argument
only after success. The kernel seeks before copying the result, so an `EFAULT`
from an invalid result pointer can follow a position change.
[i386 syscall table](https://github.com/torvalds/linux/blob/v6.12/arch/x86/entry/syscalls/syscall_32.tbl),
[kernel implementation](https://github.com/torvalds/linux/blob/v6.12/fs/read_write.c#L391-L419)

The current [startup assembly](../../toolchain/hosted/i386-linux/start.asm)
already exports `cupid_linux_syscall5`, loads the five arguments into
EBX/ECX/EDX/ESI/EDI and preserves the callee-saved registers. The shared
[runtime](../../toolchain/hosted/i386-linux/runtime.cc) needs its declaration.
glibc's wide seek wrapper makes one syscall without an EINTR retry loop.
Propagating any returned errno, including `EINTR`, is the narrow recommendation;
automatic retries for relative seeks would need a separate justified contract.
[glibc implementation](https://github.com/bminor/glibc/blob/glibc-2.40/sysdeps/unix/sysv/linux/lseek64.c#L23-L38)

Raw i386 opening also needs `O_LARGEFILE`: octal `0100000`, decimal 32768.
The generic regular-file opener otherwise rejects sizes above 2,147,483,647
bytes with `EOVERFLOW`. The i386 compatibility open path on a 64-bit kernel
does not add this flag automatically. Adding only `_llseek` is insufficient.
[flag definition](https://github.com/torvalds/linux/blob/v6.12/include/uapi/asm-generic/fcntl.h#L48-L50),
[open paths and admission](https://github.com/torvalds/linux/blob/v6.12/fs/open.c#L1390-L1407),
[regular-file size check](https://github.com/torvalds/linux/blob/v6.12/fs/open.c#L1516-L1527),
[size limit](https://github.com/torvalds/linux/blob/v6.12/include/linux/fs.h#L1028)

## Implemented private extension boundary

The private runtime supplies `cupid_fseek64` with a signed `long long`
displacement and `cupid_ftell64` with a signed `long long` output. Positions stay
within `0..LLONG_MAX`. An adapter with unsigned source offsets must reject values
above that range before converting them. Filesystem limits can still reject a
representable position. Both platforms use the same bit
packing, derived from the whole displacement rather than the low word's sign:

```c
unsigned long long bits = (unsigned long long)offset;
unsigned int low = (unsigned int)bits;
unsigned int high = (unsigned int)(bits >> 32);
```

| Displacement | High word | Low word |
| --- | --- | --- |
| `-1` | `0xffffffff` | `0xffffffff` |
| `+2147483648` | `0x00000000` | `0x80000000` |
| `+4294967296` | `0x00000001` | `0x00000000` |

The packing follows the verified platform contracts above and
[glibc's unsigned extraction](https://github.com/bminor/glibc/blob/glibc-2.40/sysdeps/unix/sysv/linux/lseek64.c#L30-L35).
Validate origins 0 through 2 and output pointers before the host call. Retain the
existing stream error convention and update-mode parser. Linux stdio opening
sets `O_LARGEFILE`; Windows append positioning uses the wide helper at open and
before every write. The standard `long` positioning bodies remain unchanged.

The new API reports local invalid arguments as `EINVAL`; an unsigned caller
must reject an unrepresentable source offset before this signed interface.
Linux retains its existing negative-errno conversion. The Windows wide helper maps
`ERROR_INVALID_PARAMETER` (87) and `ERROR_NEGATIVE_SEEK` (131) to `EINVAL`, while
retaining the existing invalid-handle and other I/O mappings. Existing stdio
calls keep their error mapper.
[Windows error values](https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--0-499-)
Restrict this adapter to captured regular files: Microsoft does not define
`SetFilePointer` behavior on nonseeking devices.
[device restriction](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfilepointer)

Keep standard `fseek` and `ftell` signatures and their existing in-range behavior.
`fseek` takes a `long`, so narrowing a larger displacement before calling it
cannot provide a wide API. POSIX requires `ftell` to return `-1` with
`EOVERFLOW` when the current position cannot fit in `long`; the current Linux
32-bit `lseek` also checks its result for overflow. Any repair of existing
overflow behavior should be recorded and tested explicitly alongside the new
functions. [POSIX ftell](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ftell.html),
[kernel lseek overflow check](https://github.com/torvalds/linux/blob/v6.12/fs/read_write.c#L364-L379)

All seventeen sparse-file methods pass with native and Cupid-built callers on
both hosts. They cover files above two and four GiB, negative relative seeks,
invalid origins and negative final positions, cleared tell output on failure,
and all-ones low-word success after a prior host seek error. Exact error values,
all ten update-mode methods and both existing runtime contracts pass. Runtime
qualification, retained source lifetimes, flushing and guarded publication
remain separate work. [ADR 0435](../adr/0435-add-signed-wide-seeks-to-the-hosted-runtime.md)
records the interface, evidence, failed harnesses and limits.

## Sparse large-file fixtures

Reject a negative absolute `SEEK_SET` displacement before either host call and
report `EINVAL`. This keeps the shared regular-file range explicit. Negative
`SEEK_CUR` and `SEEK_END` displacements remain valid when the result is
nonnegative. [POSIX regular-file position rules](https://pubs.opengroup.org/onlinepubs/9799919799/functions/lseek.html)

For a small fixture on local NTFS, create an empty file with read/write access
and mark its existing handle sparse with
`DeviceIoControl(handle, 0x900c4, NULL, 0, NULL, 0, &bytes, NULL)`.
Microsoft's header defines that code as filesystem device 9, function 49,
buffered method and special access 0; null input means sparse=true. The handle
needs write-data or write-attributes access.
[SDK definitions](https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/um/winioctl.h),
[null-input contract](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_set_sparse),
[access requirement](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-fsa/b5b3dd4c-b585-49d7-960e-fbfbf7a85af0)

Seek to 4,294,967,361 bytes (four GiB plus 65) and call `SetEndOfFile` directly.
Then use `FSCTL_SET_ZERO_DATA` over `[0, 4294967361)` before touching the few
test bytes. Microsoft recommends this sequence to create virtual zeros without
committing physical storage. Its control code is `0x980c8`, with two signed
64-bit fields for the start and exclusive end. Check allocated size with
`GetCompressedFileSize` separately from logical length.
[NTFS fixture sequence](https://devblogs.microsoft.com/oldnewthing/20110922-00/?p=9573),
[zero-range contract](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_set_zero_data),
[allocated-size query](https://learn.microsoft.com/en-us/windows/win32/fileio/obtaining-the-size-of-a-sparse-file)

Python's Windows `FileIO.truncate` calls `_chsize_s`, whose contract appends null
characters without promising sparse allocation. Use the direct setup above.
On Linux, Python uses `ftruncate`; extension reads as zeros and preserves the
current offset. Use a local filesystem that supports holes and check
`st_blocks * 512` against the logical length. Avoid ordinary zero-filled writes
across the gap, which allocate storage on Windows sparse files.
[CPython implementation](https://github.com/python/cpython/blob/v3.14.0/Modules/_io/fileio.c#L1042-L1049),
[CRT extension contract](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/chsize-s?view=msvc-170),
[Linux truncate](https://man7.org/linux/man-pages/man2/ftruncate.2.html),
[allocated block count](https://man7.org/linux/man-pages/man7/inode.7.html),
[sparse write behavior](https://learn.microsoft.com/en-us/windows/win32/fileio/sparse-file-operations)
