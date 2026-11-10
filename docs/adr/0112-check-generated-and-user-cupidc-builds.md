# Build generated tables and example programs with checked Cupid tools

- Status: Accepted
- Date: 2026-07-25

## Context

The checked CupidC seed owned 116 normal-build objects, but three generated
installation tables still went through the host C compiler. The separate
`user/` build also compiled `hello`, `ls`, and `cat` with GCC or Clang before
linking them with CupidLD. All six sources fit the represented CupidC
language. Leaving their recipes on the host path understated the compiler's
working production surface.

These builds need more than a command substitution. A production handoff must
bind an exact source cohort, freeze the seed and source inputs, reject malformed
objects or executables, and preserve an existing artifact when any check
fails. The example executables also need a guest test that proves the checked
binary reached its entry point and used the syscall table.

## Decision

The ramfs program table, homefs document table, and CupidASM demo table are
generated as `.cc` translation units. A closed production wrapper compiles
only those three files with the checked CupidC seed and the fixed kernel
profile. The wrapper also owns a separate user cohort containing
`hello.cc`, `ls.cc`, and `cat.cc`. That cohort uses the freestanding user
profile and the repository `user/cupid.h` closure.

Both cohorts capture their source and header bytes once, write that closure
into a private tree, and compile only from that tree. They also freeze the
seed manifest and images before invoking CupidC. The wrapper validates the
temporary i386 ELF32 relocatable, checks that the live inputs have not
changed, and publishes with an atomic replacement. A transient live edit that
restores the original bytes cannot alter what the compiler read.

A second closed wrapper links only the three approved user object names with
the checked CupidLD seed. It copies the object once, validates and links that
copy, and rejects publication if the live object changes. It fixes `_start`
as the entry and `0x00F00000` as the external arena base. Its executable
validator follows the kernel loader's program-header rules: at most 16
headers; a program-table offset representable by the loader's signed seek;
only `PT_NULL`, `PT_LOAD`, and an empty `PT_GNU_STACK`; known permission
flags; power-of-two and congruent load alignment; bounded, nonoverlapping
load ranges; and an entry point inside executable file-backed bytes. Each
nonempty load must stay inside `[0x00F00000, 0x01100000)`.

The user build keeps configurable output directories for isolated frontier
and poison runs. It still requires an approved program name and an object and
executable in the same directory below `user/`. An attempted restriction to
the literal `user/build/` directory was rejected because it broke the
intentional `BUILD` override and the two temporary frontier runs without
adding source authority.

The default `user/build/` directory is generated and ignored by Git. The
frontier rebuilds these files before comparison, and image staging consumes
the local results. Ordinary commits carry the sources, wrappers, tests, and
evidence, not the executables.

The external syscall table records `print`, `print_int`, and `exit` events
with the current PID. A print event carries its byte count and FNV-1a
fingerprint instead of caller text, then calls the existing console function.
Newlines and marker-shaped file contents therefore stay inside one serial
event. Kernel and JIT callers continue to use their existing paths.

## Consequences and evidence

The generated-table frontier snapshots the full generator closure, reruns
each generator twice, compares the installed source, compiles each table
twice, and requires both replayed objects to match the installed object. The
user frontier compiles and links all three programs twice, validates every
artifact, and compares both objects and executables with the installed build.
The normal `make test` target runs both frontiers before the Python suite.
Poisoned-host tests cover the root and user Make recipes, so neither build can
silently return to GCC or Clang.

The focused production suite covers allowlists, exact source-to-output
pairing, fixed profiles, symlink and path rejection, immutable compiler and
linker inputs, failed-tool preservation, malformed ELF objects and
executables, loader-rule parity, installed-artifact comparison, checked Make
bindings, and PID-bound runtime-log requirements.

The generated-table frontier tracks 194 inputs with aggregate SHA-256
`378bb5745606cccb43e9ed6eaddc8cd72569b04e9f3c81124434a4f8f1642d8a`.
It reproduced all three sources and objects byte for byte. The user frontier
tracks 16 inputs with aggregate SHA-256
`86bd95ece01110db55366424b4b2907418ba8de1c39d90582a819c9def37f0cc`.
It reproduced all three objects and executables byte for byte and matched
every current local artifact. The checked executables have SHA-256
`dbef548d246e12a0933b95ec8349a97f542bd8cbecc253efc514b1483fcc9e0f`
for hello, `0e9da33927f611442feeff8abd7147829ef9f49e3abec0aeddb59fb8b496c635`
for ls, and
`ffa5957fb58f0de81e564b3fbadadf60b7b8bc2beb0c50984cd1d4e9481f9367`
for cat.

The normal image build completed with `_loaded_end = 0x007FB2E3` and
`_kernel_end = 0x00C20A70`, leaving 914,832 bytes below the kernel stack. The
final `kernel.elf` has SHA-256
`886304f79ba86d66ceb38f00ba8fac0c1a5d5e22c49883662dfe04f7ccb796dd`;
`kernel.bin` has SHA-256
`e37f46fa8b43b9afa6326f2e9e60b3bd759690400f3e43caefb2672b5ec8452b`.

Three independent guest boots loaded hello, ls, and cat at `0x00F00000` as
PID 4. Hello emitted the exact greeting fingerprint plus PID and uptime
integers. Ls emitted fingerprints for `bin`, `home`, `disk`, `docs`, and
`demos`. Cat emitted one 62-byte print with FNV-1a `c12ed628` for the hostile
fixture, produced no PID 999 event, and exited as PID 4. In a private image
copy, the cat session first compiled and ran
`cp /disk/catfix.txt /home/readme.txt`. This kept the program's normal HomeFS
path intact. The selected image kept SHA-256
`7fd1f53d3fdc8ac866d802ed5a56ce62c863103e11d72b1a9054acd551235467`
before and after the session. Their serial logs have SHA-256
`7ab03783cb64d5c4ff5d84bc4fe65cb661140bd0d5f45e1e41b6a7dabe500b3d`,
`0463372fa96ecc1101a49b5af55d457eb1390b5cec18ce3c6c14980b401476b1`, and
`fa25eeb4099ca71fc060efe87d4f57308bf8dc88c587385e5591d85a22846388`.

The regenerated active graph contains 698 sources, 253 feature IDs, and 500
transforms. CupidC owns 122 transforms, the host C compiler owns 175, and host
Python owns 134. Its active-source digest is
`630319a4b5dde5a519c0a0e4da7ee89ff489b2b7aa6224ddad64fa958588de23`;
the JSON has SHA-256
`91bc6ddff7805c08a293edc0d2d5b9d2fe46b89e5636aea1e3cba327387aa68d`.

Python remains the generator and checked-seed launcher. Windows still uses
WSL to run the static i386 seeds. The six ownership transfers remove host C
compilation from these paths, but they do not remove Python, WSL, or the host
compiler from the remaining build.
