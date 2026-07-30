# ADR 0188: Run the Windows user build from the checked seed

## Status

Accepted on 2026-07-30. This supersedes the default Windows path chosen in
ADR 0130. The native Windows path from that decision remains an explicit
comparison oracle.

## Context

ADR 0130 made native hosted CupidC and CupidLD the default for the three
external programs on Windows. Their i386 ELF output matched the checked Linux
seed, but a clean build first needed Clang and the Windows linker to create the
two PE drivers. That kept an ordinary host code generator in `user:all`.

The root image already runs the checked static i386 Linux seed through WSL on
Windows. Using the same seed for the user cohort removes the native driver
bootstrap from the normal user graph without adding a new host dependency.

## Decision

Automatic mode always selects the checked i386 Linux seed for production user
compilation and linking. Linux runs the seed directly. Windows runs it through
the existing WSL execution layer.

`user:all` no longer prepares native CupidC or CupidLD. The default frontier
tracks only the 23 inputs used by the checked-seed path. It compiles and links
each example twice, compares the two runs, and checks the installed artifacts.

Native Windows execution remains available only through
`test-native-windows-equivalence`. That target builds the two hosted PE
drivers, adds their 23 source inputs to the frontier closure, and compares
their six outputs with the checked seed. Callers can still request
`native-windows` explicitly from either production wrapper.

## Evidence

The checked-seed frontier covers 23 inputs with SHA-256
`b722622ded83c2b2a410099ca9d76d6cbe3788905f025b1174f1a95e2274af56`.
The optional native comparison covers 46 inputs with SHA-256
`bbd90f10f0305f12fc6eacbb71a7525f88a96db7a5922d501b86f9217cee4552`.
Both paths produce the same current artifacts:

| Program | Object bytes | Object SHA-256 | Executable bytes | Executable SHA-256 |
| --- | ---: | --- | ---: | --- |
| hello | 6,124 | `64e0a6ee0d7a45a0901d3db614e73481cdc6b30903345c5015601b2bf344be04` | 13,992 | `4c5622969f39ffe7c2427d65abae2d293dfbd76db2aa80c96f9e6cf01613600c` |
| ls | 7,120 | `e0627996a1d9cd6fd428642ffdfada7e07afa81d9267bc714360014af0dd3971` | 18,112 | `094b017eb6914bce6fbc1e99adeae845d5dc05280c1c1d897e68ab9d687c8d79` |
| cat | 6,292 | `ff002fc4710704c3941bf6320249e772a3448d15f99269987ab1b9b608b3acb4` | 13,992 | `b66cba4c98221f5006ad4aeee70349a82db20410e027aa863bc33fa5818b5f4c` |

A forced Windows `user:all` build compiles and links all three programs from
the checked seed. The same build passes with the conventional Make code
generator variables poisoned and failing `gcc.exe`, `clang.exe`, `ld.exe`,
and `cc.exe` commands placed first on `PATH`.

Focused tests first failed because automatic Windows mode called
`capture_native_tool`. After the policy change, the compiler, linker,
Makefile, closure, and poisoned-host tests pass. The optional native comparison
also passes without changing its explicit execution path.

## Rejected alternatives

Keeping the native drivers in `user:all` was rejected because their build
requires Clang and the Windows linker even though the checked seed already
produces the same target bytes.

Deleting the native path was rejected because it is a useful independent
same-host oracle and exercises the hosted Windows adapters.

Calling this a native Windows fixed point was rejected. The normal path runs a
static i386 Linux seed through WSL. A native fixed point still needs a
Cupid-built Windows runtime and PE or COFF output.

## Consequences

A clean Windows user build no longer needs Clang or the Windows linker. It
does need WSL, just like the root checked-seed build. Linux continues to run
the same seed directly.

Clang and its linker remain required by `toolchain:all` on Windows and by the
optional native equivalence target. They no longer participate in
`user:all`.

The supported build graph loses the recursive Make transform that prepared
the native user drivers. The native source closure is included only when the
comparison target requests it.
