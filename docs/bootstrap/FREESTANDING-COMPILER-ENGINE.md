# Freestanding compiler engine experiment

A loadable freestanding CupidC engine rebuilt all nine of its own objects,
then relinked to the same complete image. This ran in a native Linux harness
at the existing guest compiler arena address. Execution inside Cupid OS and
normal-build integration remain unfinished.

## What ran

Checked CupidC compiled the unchanged core, ELF, x86, preprocessor, type,
frontend, IR and emitter sources with `--gnu --freestanding`. A small ninth
translation unit exposes the preprocess/parse/emit pipeline through an ordinary
toolchain job. CupidLD linked those objects at `0x01100000`, with no hosted
runtime or unresolved imports in the module.

The caller supplies the existing allocator, file and diagnostic callbacks.
The experimental version-one request occupies 156 bytes on i386. Invalid ABI,
request size or mismatched parser/preprocessor options leave the result
untouched. Accepted requests use the existing invocation transaction.

The Cupid-built Linux harness validates the ELF load ranges, maps the reserved
address range and calls the loaded engine. It compiles every engine source,
including the complete frontend, IR and emitter. Each resulting object matches
the checked compiler's object byte-for-byte. Relinking all nine reproduces the
whole first module.

| Check | Result |
| --- | --- |
| Frozen producer inputs | 114; snapshot unchanged |
| Complete object pairs | Nine identical |
| Complete module | 2,486,352 bytes; identical |
| Load span | 2,418,055 bytes within the nine-MiB compiler arena |
| Rejected requests | 54, followed by successful compilation |
| Tracked allocations remaining after each call | Zero |
| Largest tracked live allocation total | 95,309,779 bytes, compiling the frontend |
| Resumed harness/build time | 571.301 seconds |

The negative cases cover a wrong ABI, short request, language-mode mismatch,
GNU-mode mismatch, missing input and an insufficient source-byte limit.

The first harness build failed because the narrow hosted declarations do not
provide `exit`. The Linux-only harness now exits through the existing syscall
adapter. Compiler and production runtime sources did not change. The failed
command and original harness remain in the evidence.

## Guest integration still needed

The Linux harness does not establish the kernel's memory, stack or process
lifetime behavior. The guest loader must validate the module before copying,
honor the shell's compiler-region ownership, provide a sufficient worker stack
and reclaim job storage when the process exits or is killed. Sources and module
bytes must also reach the guest through a checked build and filesystem path.

The current kernel has about 980 KB of raw-image headroom and 493 KB before
its fixed stack. Embedding this engine would exceed those limits. Keeping the
payload on disk and loading it into the existing compiler arena is the measured
integration direction; a real guest self-recompile is still required.

## Retained proof

The module SHA-256 is
`732dabe3f2086109c85661b9aa4c648188bee70dcc746bcb4e80bd62b658b9bd`.
Independent readback rechecks all 114 inputs, both sets of objects, both complete
modules, ELF segment bounds, executable entry and the absence of unresolved
symbols. It also checks every execution result and exported payload.

The [archive](evidence/freestanding-engine-proof-20261110.json.gz) contains
153 file records and 143 payloads, including the experimental API, harness,
controllers, failures, sources and built products. It has 2,470,024 bytes and
SHA-256 `64f59ff10000ea54c4202af9384795597a6fbb816fc74d81df275ae04f9ab34b`.
