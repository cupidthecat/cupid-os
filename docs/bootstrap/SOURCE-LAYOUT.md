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

## QEMU terminal smoke test

`tools/gui_terminal_smoke.py` drives QEMU and checks the resulting output.
`tools/gui_terminal_contracts.py` holds the command sequence and the expected
serial output. The runner is now 1,912 lines, and its command-line entry
point stays the same. All 160 terminal, fullscreen and user-runtime tests
passed on Windows.
