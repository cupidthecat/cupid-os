# Retained seed capture for complete image publishers

The complete disk publisher still needs authority over its checked template
producer. Shared byte validators already check a reviewed release, the manifest
pair and every tool's execution profile. The ISO operation supplies the file
capture and freezing around those validators. Keep those requirements together
in `cupidbuild_seed_capture` for further complete image operations.

The caller opens a transaction and observer over the same retained root, then
selects its release, both manifests and one host tool role. Capture binds the
observer once before reading inputs. It observes and freezes all fifteen files,
requires the two captures to agree, checks the complete paired release and both
six-tool directories, and validates every image's size, digest and execution
profile. Only the selected host tool becomes executable. The result owns its
frozen pathname so later input-table growth cannot invalidate it.

The caller keeps the observer alive through transaction close. Subsequent input
checks revalidate the captured files. Publication boundaries also recheck the
observer's file and directory observations. The operation executes no tool,
opens no candidate and grants no authority over a release. A failed capture clears its result and
requires discarding the whole caller operation. Pure manifest validation errors
do not imply poisoning the host transaction.

All four native and Cupid-built callers pass the private API's fixtures. They cover all
six selected host roles, input-table growth, UTF-8 roots, bounded diagnostics,
invalid arguments and document paths, poisoned/repeated/wrong-root observers,
opposite-cohort missing or changed images, exact directory membership, paired
binding, release claims and matching digests around an invalid execution image.
Paused controls change release or tool bytes with restored timestamps, or add a
cohort member, before the caller attempts publication. The prior output must
survive and all transaction names must be cleaned. No installed seed, producer
plan, normal recipe or OS source changes with this prototype.

Both original native runs remain recorded. Linux rejects a potentially truncated
growth-fixture path. Windows reaches capture but its Python role-name variable
overwrites the saved namespace; its native caller also needs the existing UTF-8
adapter. Correct the fixture length check, variable name and native build profile.
The corrected native twelve-method selections pass in 24.880 seconds on Windows
and 13.586 on Linux. A subsequent fixture revision validates its one-digit role
and uses the shared stream reader for the exact resume byte. Native replays pass
in 29.276 and 15.930 seconds, with original source copies retained.

The first checked fixture build rejects undeclared test helpers. The next build
reaches an existing compile caller and rejects its full-width snapshot pointer
where the bounded payload reader requires `size_t *`. Read the payload length
into a local memory-size count, then assign that bounded count to the snapshot's
64-bit extent. This preserves both interfaces without casting their pointers.
The following build then reaches the unchanged capacity expression
`total += overhead + inputs[index].snapshot.size` and rejects its wide
computation with a narrow destination. [The compiler extension](MIXED-WIDE-INTEGER-MUTATION.md)
accepts the existing arithmetic and assignment conversion instead of adding a
cast or rewriting that source. Complete Cupid-built caller builds pass in
106.400 seconds on Windows and 105.102 on Linux, with the original two workers
and 360/120/180-second compiler, assembler and linker bounds.

Each final caller passes twelve methods and 35 cases. The four callers cover
140 cases, including all six selected roles and twelve paused input changes.
Both Linux programs keep the 32 MiB address-space limit and all runtime cases
keep their original 180-second limit.

| Caller | Complete selection seconds |
| --- | ---: |
| Native Windows | 38.181 |
| Cupid-built Windows | 119.297 |
| Native Linux | 25.590 |
| Cupid-built Linux | 80.386 |

Independent rereading passes in 20.788 seconds. It checks all 140 cases,
complete programs and producer artifacts, original bounds, source controls,
selected tool images, restored timestamps, namespace cleanup and publication
results. The first independent control rejects missing copied source files.
The final source copies are retained after the callers close and must agree
with their existing before/after facts. Separate custody records identify that
timing; they do not claim capture of those copies during execution. The original
failed control remains. The accepted checker is
`seed-capture-four-producer-independent2`; its detailed product file is
`seed-capture-four-producer-independent1-products.json` under
`C:/Users/admin/cp7/`. Earlier complete native source copies remain under
`seed-capture-native-source4-windows` and `seed-capture-native-source4-linux`.

## Complete public compile regressions

The bounded payload length repair also passes through the existing public
`cupidbuild_compile_kernel_with_release` operation. Four methods run through
native and Cupid-built callers on both hosts. They compile the complete tracked
4,474-byte `kernel/cpu/ksyms.cc` and compare its whole object with the ordinary
selected seed compiler. Equal output preserves its timestamp. Invalid source
and a missing final header preserve the prior bytes and timestamp. The missing
header case captures the 79-input closure of `kernel/lang/as.cc` before rejecting
the absent header. Every case keeps all paired seed inputs and transaction
namespace checks.

| Caller | Complete selection seconds |
| --- | ---: |
| Native Windows | 5.492 |
| Cupid-built Windows | 18.573 |
| Native Linux | 3.087 |
| Cupid-built Linux | 10.521 |

All sixteen methods pass with the original ordinary compiler bound of 180
seconds and the compile API's 610-second outer bound. These public API tests
do not establish a 32 MiB memory bound. Independent rereading passes in 3.236
seconds and checks complete source/header copies, every object, retained caller
build artifacts, exact diagnostics, bounds, timestamps and namespace cleanup.
All four complete source objects have SHA-256
`67a34475226fc81680583d7e359c6e7e27893977b784c133a808e7db374de583`.
Evidence is `wide-compile-four-producer-independent1-products.json` under the
same proof directory, with final selection labels ending in `windows3` and
`linux3`.

The first selection requests a generated symbol source that is absent from the
checkout and receives the test helper's 175-byte fallback. Its passing result
does not establish complete active-source compilation. The final fixture reads
the tracked source directly; the earlier selection remains separate. The next
selection reaches the correct source but fails its generic diagnostic-word
assertion. The final check requires the actual `CTB000003` and missing-closure
diagnostics. These repairs change test inputs and assertions, not production
behavior, and both earlier selections remain recorded.

The prototype observes 103 producer inputs; the installed qualified cohort
still has its unchanged 99 inputs. Complete seed qualification, publisher
integration and normal disk recipe ownership remain open.

The accepted source and complete compiler/caller artifacts were archived before
the next host-adapter change at
`C:/Users/admin/cp7/checked-template-parent-accepted1-windows` and
`/var/tmp/checked-template-parent-accepted1-linux`. The current private adapter
has subsequent [private-output transfer work](PRIVATE-OUTPUT-INPUTS.md); its
results require their own matched builds and regression evidence.

The newer [checked disk-template module](CHECKED-DISK-TEMPLATES.md) now captures
this paired cohort before executing CupidObj and transferring its output into
retained image composition. Its sixteen methods pass through all four callers,
with independent complete source, artifact and image checks. The subsequent
[combined publisher](CHECKED-DISK-PUBLISH.md) also passes all 132 executions
and independently reconstructed images. All 140 seed-capture cases pass again
through the current native and matched adapters. Full normal-image acceptance,
optional discovery and normal recipe ownership remain open.
