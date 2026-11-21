# Source files over 2,000 lines

The November 20 inventory found 25 tracked sources over 2,000 lines in the
active build audit. It also found 26 test sources and nine other tracked
sources over the limit. The count includes blank lines and comments. It
excludes generated outputs and the TempleOS reference tree.

| Area | Active sources |
| --- | --- |
| Browser and user programs | `bin/browser/js_interp.cc`, `bin/browser/style.cc`, `bin/notepad.cc`, `bin/ssh.cc` |
| Doom | `kernel/doom/dglibc.cc`, `kernel/doom/src/info.cc`, `kernel/doom/src/m_config.cc` |
| Graphics and desktop | `kernel/gfx/gfx2d.cc`, `kernel/gui/desktop.cc` |
| Embedded languages and shell | `kernel/lang/as.cc`, `kernel/lang/cupidc.cc`, `kernel/lang/cupidc_parse.cc`, `kernel/lang/shell.cc` |
| Trust data | `kernel/tls/tls_ca_bundle_data.cc` |
| Shared compiler | `toolchain/cupidc_emit.cc`, `toolchain/cupidc_frontend.cc`, `toolchain/cupidc_ir.cc`, `toolchain/cupidc_pp.cc` |
| Shared peers and build runner | `toolchain/cupidasm.cc`, `toolchain/cupidbuild.cc`, `toolchain/cupidbuild_host.cc`, `toolchain/cupiddis.cc`, `toolchain/cupidld.cc`, `toolchain/cupidobj.cc`, `toolchain/x86.cc` |

Split code where responsibilities already meet. For example, the Doom libc
shim has a self-test suite that can move apart from its runtime adapters.
For data tables, keep their indexing and byte order when moving the data.
For toolchain code, verify the producer plans and staged fixed point with
any change to the translation units.

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
