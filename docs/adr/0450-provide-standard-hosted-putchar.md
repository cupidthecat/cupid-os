# ADR 0450: Provide standard hosted putchar

## Decision

Declare `int putchar(int)` in hosted `stdio.h` and implement it by calling
`fputc(character, stdout)` in the shared runtime. Both hosted targets use that
runtime. This preserves the existing unsigned-byte conversion, byte return,
`EOF`, stream error and errno behavior. It also observes the current `stdout`
pointer rather than retaining a separate stream.

The optional-stage contract uses this standard interface. Its first Cupid
compile exposed the missing declaration. Adding the runtime interface keeps
the caller's ordinary C source intact.

## Evidence

Native and Cupid-built callers pass both methods on Windows and Linux. Each
caller writes all 768 arguments from -256 through 511 and checks every return
value. Complete output is three repetitions of bytes 0 through 255. Redirecting
`stdout` to a read-only stream requires `EOF`, a sticky stream error and nonzero
errno, preserves every file byte and its timestamp, then restores stdout and
writes another byte successfully.

The native oracle extracts the actual function body and combines it with the
same caller. Windows explicitly selects binary CRT output so CRLF translation
cannot change the oracle bytes. The Cupid callers compile the actual integration
runtime with the qualified `a1cc8f3a` tools and retain 360/120/180-second producer
bounds and 20-second case bounds. The first two Windows build recipes selected
an assembly bridge incompatible with their import profile. The corrected recipe
uses the ordinary tool and long-path starts; the failed commands remain retained.

This changes two producer source paths. Installed seeds remain pinned to their
earlier qualified source. Complete qualification and carriage of the new runtime
are separate work. [The runtime record](../bootstrap/HOSTED-PUTCHAR.md) gives
the retained commands and remaining acceptance.

## Consumer compatibility

The updated manual and runtime source pass paired normal kernel/image builds,
fresh user builds and all four strict private boots through the installed seeds.
Independent comparison checks every byte of the 429 objects, sixteen artifacts,
six user products and complete images, with baseline FAT data preserved. Only
the manual wrapper changes. The runtime record gives measured sizes and original
command bounds. All installed seed bytes remain pinned; new runtime carriage
still requires complete producer qualification.
