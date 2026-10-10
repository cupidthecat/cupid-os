# ADR 0344: Validate the complete checked seed in CupidBuild

## Status

Accepted on 2026-08-25.

## Context

CupidBuild already froze the source, manifest, CupidASM, and CupidDis for its
first guarded object transaction. It parsed the exact current seed contract and
checked the frozen tool sizes and hashes. The Python publisher still enforced
two parts of the trust boundary that CupidBuild did not: the seed directory
could not contain an unlisted executable-shaped peer, and every checked image
had to match the selected host execution profile.

A manifest digest alone is not enough for either check. An extra `.elf` or
`.exe` can sit beside the listed tools without changing the manifest. A listed
file can also have the expected digest while the manifest is deliberately
rewritten around an executable with the wrong format, entry point, segment
permissions, or Windows imports.

## Decision

Make the five listed executables one complete trust unit inside CupidBuild.
Before freezing a tool, enumerate the manifest directory and reject any
case-insensitive `.elf` or `.exe` peer that is not one of the five exact
manifest file names. Unrelated files remain legal. Repeat the membership check
after every attempted CupidASM and CupidDis launch, including a failed command
or timeout, so a tool cannot add a new executable peer during the transaction.

Freeze all five images even though this command runs only CupidASM and
CupidDis. Check every frozen image against the host profile before either tool
runs. Linux accepts a static i386 `ET_EXEC` with entry `0x08048000`, at least
one load segment, no interpreter or dynamic segment, no writable executable
load segment, and an entry inside file-backed executable bytes. Windows accepts
the strict CupidLD PE32 layout with entry `0x00401000`, one `KERNEL32.dll`
import library, the shared twelve imports for ordinary tools, and the existing
sixteen-import publication profile for CupidLD.

Use the hosted platform layer for directory enumeration. Native POSIX builds
use `readdir`, hosted i386 Linux uses `getdents64`, and Windows uses the three
`FindFirstFileA` family calls. Add those Windows calls to CupidBuild's checked
startup bridge. Keep the typed ELF32 and PE32 readers as the format authority.

## Evidence

The public CupidBuild suite passes 40 tests on native Windows and 40 through
Linux, with two Windows skips and one Linux skip. It covers a valid five-tool
directory, an unrelated text file, uppercase unlisted executable suffixes, an
executable-shaped directory, pre-execution profile drift, and membership drift
while a checked tool runs. A separate case makes CupidASM fail after the peer
appears and proves that the post-run check still takes place. Every rejected
case preserves the previous object.

The checked CupidC host-adapter, static-link, and wrong-ABI selectors pass for
the expanded hosted source. The existing Linux and Windows manifests and tool
images are unchanged.

## Consequences

CupidBuild now decides the complete seed membership and execution-profile
rules needed by its first transaction. Python no longer owns a semantic check
inside that source-head operation.

The production build still enters Python for the ISR and context-switch object
recipes. Both promoted seeds still contain five tools, so CupidBuild cannot be
selected from either checked cohort. A non-self-referential six-tool manifest,
fixed-point evidence on both hosts, and a deliberate Make recipe transfer are
still required.

No active source rename is due. Every source changed here already uses `.cc`
or `.asm`, and `TempleOS/` remains untouched reference material.
