# ADR 0140: Expose ordered forced includes in the CupidC driver

## Status

Accepted on 2026-07-27.

## Context

The CupidC preprocessor has accepted ordered forced inputs since ADR 0012, but
the command-line driver could not supply them. The normal Doom-tree recipe
uses `-include kernel/doom/dglibc_compat.h`, so a real driver invocation could
not reproduce the audited `DOOM_TREE_I386` request. Focused frontend tests
worked around that gap by constructing the preprocessing request in process.

Adding an `#include` to each Doom source would change vendored code to suit a
driver limitation. A hard-coded Doom profile in the driver would also hide
the underlying compiler operation and couple a general command to one source
tree.

## Decision

The native and Cupid-built `cupidc` drivers accept a repeatable option:

```text
-include FILE
```

Forced inputs run in command order after command-line macro actions and before
the primary source. This is the existing preprocessing order from ADR 0012.
The option changes no include-search rule.

In ordinary command mode, each forced input is converted from a native path to
a canonical logical path. Under `--root`, it must already be an absolute
logical path. The driver resolves every path inside the compiler job and
passes the resulting ordered array and count to `ctool_c_preprocess`.

Missing option values and empty paths are usage errors. A relative forced
input under `--root` is rejected before the job starts. A missing forced file
fails the compilation through the normal diagnostic path, so an existing
output remains untouched.

The build-graph audit pins the usage text, parser handoff, preprocessing
request fields, and `--root` diagnostic. A mutation test removes the forced
input count and requires the audit to fail.

The driver contract checks two forced headers whose definitions depend on
their order. It compares the hosted and Cupid-built ELF32 objects byte for
byte. Negative cases cover a missing value, an empty value, a relative rooted
path, and a missing file with sentinel output preservation.

The exact Doom-tree frontier test reads its roots, macros, forced input, and
80 source transforms from the checked active-build audit. Compiler head emits
71 valid ELF32 objects. The remaining nine roots fail at their first real
language boundary:

- `i_sound_cupidos.c` needs an empty volatile assembly template as a compiler
  barrier.
- `am_map.c` needs floating arithmetic in static initialization.
- `i_system.c` needs Doom-compatible implicit function declarations.
- `i_video.c` reaches an invalid IR translation-unit boundary.
- `info.c` needs positional union active-member initialization.
- `m_menu.c`, `p_ceilng.c`, `p_plats.c`, and `p_saveg.c` need the remaining
  Doom callback and pointer conversions.

Each failure is pinned by logical path, source location, diagnostic code, and
message. This keeps the next Doom work tied to unchanged active source.

## Consequences

CupidC can now express the exact forced-input part of the Doom build profile
without changing Doom source. The command remains general and can represent
other forced headers in caller order.

This change adds a driver capability at compiler head. It does not transfer a
Doom object, rename a Doom source, change the normal OS image, or retire a
host dependency. The checked seed predates this option and needs a later
five-tool promotion before a production wrapper can use it.

Issue #29 remains open for the nine compiler boundaries, full 83-file
ownership, object validation, optimization policy, and runtime proof. Issue
#32 remains open for the seed refresh and the wider host-toolchain removal
work.
