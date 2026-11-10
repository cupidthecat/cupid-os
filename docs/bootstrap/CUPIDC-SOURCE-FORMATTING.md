# Cupid C source formatting

All 492 tracked `.cc` files use the repository's `.clang-format`: LLVM style,
four spaces, attached braces, a 100-column limit and pointers aligned with
their declarators. TempleOS is excluded.

The browser's implementation includes retain their dependency order. A guarded
FPU macro-instantiation block avoids repeated Clang reflow. Both regions have
formatting comments explaining the exception.

## Repairs needed by formatting

- `help` accepts `//help:` and `// help:` metadata, including horizontal space
  after `//`. Marker recognition stays within the bytes read. The shell manual
  documents summaries and consecutive help lines.
- Doom's `i_scale.h` includes `i_video.h` for `screen_mode_t`; `w_main.h` includes
  `doomtype.h` for `boolean`. Both headers have standalone include tests.
- Source-driven IR, object and Python audit checks accept layout changes while
  retaining C token boundaries, literal contents and effective mutations.
  Mutation locations must be unique. Whole-function byte fingerprints and
  frontend source locations remain exact, remeasured checks.
- The shared C spelling fixture joins the frozen publication closure: 113 SDK
  publication inputs and 165 Linux contract inputs. The C manifest author and
  verifier accept that exact release profile and preserve historical profiles.
  Negative cases distinguish missing observations, unknown members and count drift.
- `make -j2 test` finishes generated publications before the read-only user ABI
  observation. A real-Make regression also requires parallel work within each
  frontier, preserving the nested jobs.

The active audit now counts 801 inputs and 257 feature requirements. Build
ownership remains 449 CupidBuild transforms and three Python coordinators,
with GNU Make scheduling the 452-transform graph.

## Verification

| Check | Result |
| --- | --- |
| Final formatter readback | All 492 files passed |
| Strict native toolchain build | Passed |
| Frontend / IR / focused object methods | 98 / 86 / 87 passed |
| Focused preprocessing and type methods | 45 passed |
| Help, header and Make regressions | Seven passed on Windows |
| Browser checks | 20 passed on each host |
| Private call/ABI and repaired source checks | 420 passed on each host |
| Native manifest policy | All 73 methods passed, including historical profiles |
| Artifact policy, API, contracts and runner | 112 passed on Linux; Windows passed with four platform skips |
| Checked-seed IR and object compilation | Both passed, with strict object inspection |
| Audit inventories and negative mutations | Six targeted methods passed on each host, including a focused Linux inventory rerun |
| Deterministic active audit | Regeneration and readback passed on Linux; Windows readback passed |
| Final artifact gate | All 16 exact byte locks passed |
| Private four-CPU boot | `ls`, `help help`, 62 TLS checks and ordered terminal completion passed |

The checked compilation keeps the original 900-second IR and 1,800-second
object bounds and runs the object compile exclusively. It establishes target
compilation and inspection; complete SDK publication remains a separate gate.

A lexical review found unchanged include membership across all 492 files.
After accounting for layout, comments and adjacent literal splitting, its
program-token differences were the help repair and parentheses around an
intentional test-string concatenation. Later changes add the test spelling
fixture, release-profile support and the explicit header dependencies above.

An optional native syntax oracle checked 239 kernel and Doom sources. Its
three warning failures also reproduce on the original `mus2midi`, Nuked OPL3
and `homefs` sources. This oracle emits no production objects.

## Image and byte locks

The normal build completed compilation, both links and flattening, then
rejected the three old kernel sizes. The 130 embedded program/library sources
add 84,305 bytes; alignment accounts for the initial 84,308-byte raw increase.
The subsequent shell-manual update adds 440 raw-kernel bytes.

A guarded manual relink retained source and object hashes and checked that
the raw kernel contains the updated manual. Final publication retained the
429 object hashes and all three kernel identities, ran the exact artifact gate,
and produced the 200 MiB image. The private boot left that image unchanged.
This final publication uses verified existing products incrementally.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Pass-one ELF | 9,598,676 | `1cecf38a10884182bda2d3bd82c040245b5ef1709b43d683284915b98a1221af` |
| Final ELF | 9,729,748 | `7e276a6378bb2e570b452602a2e231b5efa2719dbed3e8b739369ee3371db7ee` |
| Raw kernel | 9,502,616 | `8b00eced7fdc6f456dcff4976fb06e343674f1f8d1b6508921a650d3383fd764` |
| Disk image | 209,715,200 | `c4129c8777b8056d4b30a4e4ce5567112d5c7c1bc4b6a1efac29265fb5d00bbf` |

Only the three measured kernel-size rows change. The seed files remain unchanged.

## Failed attempts and remaining acceptance

Builds that overlapped source edits rejected the changed observations. An
incremental Make replay repeated unchanged Doom compilation because equal-byte
publication preserves timestamps older than the regenerated profile. A first
reuse command also marked the changed manual object old and left the preceding
kernel in place. The corrected command excludes that object from reuse and
requires the complete new manual to appear in the final kernel.

Earlier source fixtures assumed old indentation, declaration spacing and
physical line numbers. Their failed transcripts remain retained. The first
release-profile negative also confused an author's observed input count with a
verifier's declared manifest count; corrected controls preserve valid historical
inventories.

Both default checked SDK builds reach release authorization and reject
`behavior seed plan differs from reviewed cohort`. The installed release still
binds the earlier plan. A partial unformatted Windows replay also reports three
failures and three setup errors at the owner's flexible-array access. A direct
probe reproduces rejection of both original and formatted source with the
installed compiler; the current compiler emits identical objects for both.
These failures remain open for installed-cohort acceptance.

The complete current repository replays, installed seed adoption and in-guest
self-hosting remain unfinished. The [evidence archive](evidence/cupidc-source-formatting-20261109.json.gz)
retains source and product identities, failed attempts and the passing scoped
checks above.

Fresh [formatted-producer qualifications](FORMATTED-PRODUCER-QUALIFICATION.md)
now pass on Windows and Linux against the separately reviewed current cohort.
Independent checks cover all 333 staged artifacts, 111 complete fixed-point
pairs and 114 Git source inputs. Private replacement seeds verify on each
host; their SDK and normal-build acceptance still precede installation.

## Compile-policy table readbacks

The continuing replay found two more source readers that expected initializer
fields and closing `};` tokens on particular lines. They now allow whitespace
between fields and stop at the declaration terminator. Exact kernel closures,
profile arguments and generated-source closures remain checked; duplicate
production rows also fail.

Both source-only readbacks pass on Windows and Linux. Six controls on each host
reject changed headers, wrong counts, missing rows and duplicate rows. These
checks read the actual C tables; they do not execute the native coordinator.
The [readback evidence](evidence/compile-policy-layout-20261109.json.gz) retains
the failures, corrected sources and controls. Installed-seed acceptance remains
open.

## Runtime-source readbacks

The shared Python C-token fixture also supports graphics handoff, shell
routing, indirect floating updates, callback checks and Doom exit checks.
Block extraction follows balanced braces outside comments and literals.
SIMD markers may span adjacent string literals; their content stays exact.
The SDK inventory check requires the shared C spelling header explicitly.

All 109 focused methods pass on Windows and Linux, including the checked-seed
`g_game.cc` object fingerprint. Seven altered-source controls on each host
reject changes to the guarded operations. The
[runtime fixture evidence](evidence/formatted-runtime-fixtures-20261109.json.gz)
retains sources, controls and failed and passing logs. These checks do not
replace the complete repository replay or installed-cohort acceptance.
