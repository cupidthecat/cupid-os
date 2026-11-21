# Source files over 2,000 lines

This inventory covers tracked C, CupidC, assembly and Python source files
and their headers as of November 21, 2026. It excludes TempleOS. The
[active-source audit](ACTIVE-SOURCE-AUDIT.md) records build ownership.

## OS and installed programs

| Area | Files |
| --- | --- |
| Browser | `bin/browser/js_interp.cc` |
| Installed programs | `bin/notepad.cc`, `bin/ssh.cc` |
| Graphics and window management | `kernel/gfx/gfx2d.cc`, `kernel/gui/desktop.cc` |
| Language and shell | `kernel/lang/as.cc`, `kernel/lang/cupidc.cc`, `kernel/lang/cupidc_parse.cc`, `kernel/lang/shell.cc` |
| Generated TLS data | `kernel/tls/tls_ca_bundle_data.cc` |

Separate parser logic from runtime bindings, and graphics primitives from
surface and window management. Update the TLS generator if the generated
certificate data moves into several files.

## Toolchain

| Area | Files |
| --- | --- |
| Compiler | `toolchain/cupidc_pp.cc`, `toolchain/cupidc_frontend.cc`, `toolchain/cupidc_ir.cc`, `toolchain/cupidc_emit.cc` |
| Assembly and inspection | `toolchain/cupidasm.cc`, `toolchain/cupiddis.cc`, `toolchain/x86.cc` |
| Linking and object production | `toolchain/cupidld.cc`, `toolchain/cupidobj.cc` |
| Build commands | `toolchain/cupidbuild.cc`, `toolchain/cupidbuild_host.cc` |

Keep command parsing, format handling and publication in focused modules.
Compare emitted objects and checked seed outputs after moving code.

## Verification

| Area | Files |
| --- | --- |
| Compiler contracts | `toolchain/tests/cupidc_frontend_contract.cc`, `toolchain/tests/cupidc_ir_contract.cc`, `toolchain/tests/cupidc_object_contract.cc`, `toolchain/tests/cupidc_pp_contract.cc`, `toolchain/tests/cupidc_type_contract.cc` |
| Assembler and format contracts | `toolchain/tests/cupidasm_contract.cc`, `toolchain/tests/cupidasm_kernel_elf_contract.cc`, `toolchain/tests/cupiddis_contract.cc`, `toolchain/tests/cupidobj_contract.cc`, `toolchain/tests/x86_contract.cc` |
| Manifest contracts | `toolchain/tests/toolchain_manifest_contract.cc` |
| Build and verification scripts | `tools/bootstrap_toolchain.py`, `tools/build_graph_audit.py`, `tools/cupidc_toolchain_contracts.py` |

Organize contract cases by behavior and keep the existing test commands.
The seed suite now keeps all 156 test IDs under the original module while
moving fixtures and cases into focused files. The Hostbuild and CupidBuild
command-line suites have smaller case files too. The Doom libc shim,
configuration code and map-object tables have been split with matching
object bytes.

## Retained sources

The audit classifies `bin/cupidc_parse.c` as historical, `bin/old_cc2_single.cc`
as excluded and `kernel/gui/notepad.c` as superseded. All active C
translation units use `.cc`. The remaining `.c` files are historical
sources or hosted test inputs. Check their references before moving them.

## Production compiler and linker tests

The 2,458-line production test module now has separate compiler and user
linker suites. Their binary fixtures live in `tests/cupidc_production_fixtures.py`.
Run both suites and the input-freeze checks together with:

```sh
python -m unittest tests.test_cupidc_production tests.test_user_link_production tests.test_cupidc_production_compile_freeze
```

The split keeps the test cases and assertions intact. All 64 tests passed
on Windows.

## Kernel compiler tests

The 2,555-line kernel compiler suite has separate files for Make recipes
and compiler operations. The test modules and the Doom suite share one
fixture file for ELF objects and expected inputs. Each test body kept
its original assertions. The three files have 739, 963 and 901 lines.
All 36 tests in the two new suites passed on Windows.

The 2,348-line kernel frontier suite now keeps its source lists and fake
compiler in a 376-line fixture module. The 1,953-line suite keeps its Make
entry point. The split drops an unused seed helper. All 39 focused tests
passed on Windows and WSL.

## Doom production tests

The 2,456-line Doom suite now separates compiler and Make coverage from
input-manifest publication. The modules have 1,109 and 1,327 lines. They
share a 49-line fixture that creates the source and seed tree. The split
preserves all 44 test cases and their assertions. The suites passed on
Windows with two platform skips and WSL with one.

## CupidASM command-line tests

The 2,409-line assembler suite now keeps raw image and range-map recovery
tests in an 831-line module. The 1,596-line command-line module keeps the
existing test names and builds CupidASM once. The 49-case suite passed on
Windows and WSL with four platform skips on each host.

## CupidDis and CupidObj command-line tests

The disassembler keeps raw decoding and range-map checks in a 655-line
module. Its 1,899-line command-line suite still runs all 37 cases under the
same names. It passed on both hosts with one Windows skip.

CupidObj keeps typed profile checks in a 769-line module. The 1,925-line
command-line suite still provides the build helper used by the ISO tests.
The two suites passed 58 cases on WSL and on Windows with three skips.

## Toolchain manifest tests

The 2,761-line manifest suite now has a 1,943-line test module and an
867-line fixture module. The runner tests use the same request fixtures.
All 102 cases passed on WSL. Windows passed with three platform skips.
The first pair shared a checkout and hit the root-change guard. Both
retries ran in separate checkouts and kept that guard enabled.

## CupidBuild command-line tests

The 6,502-line suite now keeps assembly, image publication, Doom profile
publication, checked execution and seed contracts in their own modules.
The main suite still builds the CLI once and collects the same 148 tests.
Each file stays under 1,600 lines. The suite and both fixture consumers
ran 166 cases on Windows and WSL, with 12 and eight platform skips.

## Hostbuild tests

The 5,088-line suite now keeps disk, ISO, symbol and JPEG cases in four
modules. Each stays under 1,800 lines. The original entry point still runs
all 90 tests under the same names. Windows and WSL passed with one skip each.

## Native contract runner tests

The 3,944-line suite now separates publication and user ABI cases from
contract planning. The four files each stay below 1,700 lines and share
ELF and process fixtures. All 84 cases kept their unittest names and
assertions, and passed on Windows and WSL.

## QEMU terminal smoke test

`tools/gui_terminal_smoke.py` drives QEMU and checks the resulting output.
`tools/gui_terminal_contracts.py` holds the command sequence and the expected
serial output. The runner is now 1,912 lines, and its command-line entry
point stays the same. All 160 terminal, fullscreen and user-runtime tests
passed on Windows.

The 4,486-line terminal test suite now has separate modules for terminal
input and capture, the frontier runtime, and program-specific evidence.
The modules share their serial, disk and monitor fixtures. All 160 tests
passed again across the new module paths.

## Doom libc shim

`kernel/doom/dglibc.cc` contains the runtime adapters. Its private storage,
formatting and lifecycle tests live in `kernel/doom/dglibc_selftest.inc`.
CupidC still compiles them in one translation unit so the tests can use
private helpers without exporting them. Make now tracks the included file
along with the rest of the Doom compiler profile.

The split leaves the two files at 1,281 and 909 lines. The first preview
moved an assertion's source line and changed one byte in the object. Keeping
the file header's line count restored the exact 88,972-byte object on WSL
and Windows. The two Doom suites passed 46 tests on WSL with one skip.
The four-vCPU guest passed the installed `dglibc_test`, including its
HomeFS batch check.

The include also enters the kernel frontier's 488-file input snapshot.
The fresh WSL build supplied all 156 production objects for the frontier
fixture. Its FAT16, x86 and EHCI object bytes exposed stale locks from earlier
changes. Every frontier assertion passed against the measured objects and
snapshot from this build.

The standalone replay compiled all 156 sources twice and reached the
output assertions with matching stage pairs. That run still held the old
EHCI expectation. Independent readback matched 311 saved objects against
the checked kernel build. The replay's temporary directory closed before
readback retained the second x86 object.

## Doom configuration

The 2,540-line configuration source now keeps the default and extended
variable tables in `kernel/doom/src/m_config_defaults.h`. The original file
keeps loading, saving and binding. The two files have 1,097 and 1,475 lines.
The first split used an `.inc` file, which Make tracked but the source
line count omitted. The private header keeps the tables in that count.
The profile manifest includes the new file on both hosts. The checked builds
each reproduced the preceding 42,408-byte object exactly. The 44 Doom build
and manifest cases passed on Windows and WSL with their platform skips.
The guest compiler rebuilt the split source and matched the same object.

## Doom map object tables

`kernel/doom/src/info.cc` now keeps actors, pickups and scenery in three
private headers. The table stays in enum order and uses one translation
unit. The four files each stay below 1,600 lines. The fixed production
recipe on Windows and WSL reproduced all 51,200 bytes of `info.o`.
CupidC compiled the split source inside Cupid OS and matched the same bytes.

## Bootstrap test file headers

Sixteen test files now describe their purpose in Doxygen file headers.
Their Python syntax trees match the originals. The Windows run passed all
157 cases with three platform skips.

## Checked seed and fixed-point tests

The 12,165-line seed suite moved its cases and shared fixtures into files
under 1,900 lines. The original unittest module still exposes all 156 tests.
The current source fixtures include the shared JPEG validator. Historical
stage fixtures keep their original inventories. The Doom checks pin all
parts of the split libc shim and still expect the same emitted object.
The focused WSL run covered 191 tests with 17 platform skips. The Windows
cases passed across the focused run and a separate native stage preparation.

## Kernel compiler wrapper

`tools/cupidc_kernel_compile.py` keeps the command and publication path.
`tools/cupidc_kernel_inputs.py` records each source's dependency list, and
`tools/cupidc_i386_object.py` validates the emitted ELF objects. The three
files stay under 1,900 lines. Make and the audit track both dependencies.

## CupidC object tests

The 6,059-line Python suite moved emission, assembly, hosted tool and
compiler profile cases into separate files. The original module keeps the
fixed-point test and all 118 unittest names. Every test body stayed the same.
Windows and WSL each passed 117 cases on the first run. The remaining case
passed on both after six stale object fingerprints were refreshed.

## CupidLD format tests

The linker test keeps its ELF and common fixtures in `cupidld_contract.cc`.
The fixed-layout and import tests live in `cupidld_pe32_cases.inc`. Both
files stay under 1,800 lines. On Windows and WSL all eight linker cases
passed, and checked CupidC still emitted the same 125,088-byte object.

## Build graph tests

The 13,912-line suite now separates Make rules, source inventories, compiler
profiles, and fixed-point checks. Every file stays under 2,000 lines. All 143
unittest names are unchanged. Windows and WSL passed the four reruns after
the source counts and kernel-wrapper fixtures were refreshed.

The November 21 Windows run caught three stale checks after the Browser
split and guest kernel worker addition. The refreshed cases cover 766
preprocessor source files, 2,659 include operands, and 454 transforms.
All three passed on Windows and WSL.

## Build graph source scanner

`tools/build_graph_features.py` scans C and assembly, records feature counts,
and keeps examples with their original line numbers. The audit driver records
the scanner's hash with the other build controls. The 1,227-line module
leaves the generated source and ownership inventories unchanged.

`tools/build_graph_preprocessor.py` checks include operands, line directives,
conditionals, pragmas, and `#exe` blocks. The audit uses the same include
parser for these checks and for the source closure. It records the directive
module's hash with the other build controls.

`tools/build_graph_report.py` renders the collected audit as JSON and
Markdown. Its 425 lines cover report formatting. Both formats reproduce
the prior audit byte for byte when given the same data.

`tools/build_graph_kernel_inputs.py` lists the 157 expected kernel source
closures. The audit checks the build against this list, independently of
the compiler wrapper's capture table. The inventory takes 1,790 lines.
Only provenance changed in the regenerated audit. Exact checks passed
on Windows and WSL.

## Host build commands

`hostbuild.py` keeps the command line, checked assembly and code validation.
Separate modules own disk images, ISO fixtures, retained code paths and
asset generation. All five files stay under 1,800 lines. The 181 build,
validation and artifact cases passed on Windows and WSL with platform skips.

## Browser styles

`style.cc` sets defaults and parses CSS values. `style_properties.cc`
applies declarations to the computed style. `style_cascade.cc` matches
selectors and resolves priority and inheritance. The Browser includes
all three in order, and each file has fewer than 1,300 lines.
