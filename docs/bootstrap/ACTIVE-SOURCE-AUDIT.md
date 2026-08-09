# Active build and source audit

This file is generated deterministically by `tools/build_graph_audit.py` from the supported Make graph and source tree.

## Scope

- Root Make target: `all`
- Supplemental builds: `user:all`, `toolchain:all`
- Active source inputs: 721
- Unreachable source-like files: 25
- Reachable output transforms: 447
- Distinct feature requirements: 255
- Make conditionals use the canonical `OS=Windows_NT` graph and the C locale fixes wildcard order on every host. Direct Linux build tests cover the Linux execution branch.
- The `TempleOS/` reference tree is excluded.
- Source and control-file SHA-256 values use canonical LF text bytes.

Generated C translation units are recorded as reachable build inputs but have no source hash or lexical features; their content is owned by the recorded generator transform.

## Active language inputs

| Language | Files |
| --- | ---: |
| `assembly` | 29 |
| `c_header` | 290 |
| `cupid_c` | 402 |

## Source cohorts

| Cohort | Files | Checked-source lines |
| --- | ---: | ---: |
| `boot_assembly` | 1 | 298 |
| `cupid_asm_demo` | 22 | 1466 |
| `cupid_c_browser_fragment` | 22 | 15958 |
| `cupid_c_program` | 105 | 19652 |
| `cupid_c_runtime_header` | 2 | 286 |
| `cupidasm` | 7 | 6851 |
| `cupidc` | 8 | 16258 |
| `cupiddis` | 5 | 3010 |
| `doom_port` | 7 | 3981 |
| `driver` | 22 | 3861 |
| `generated_install_table` | 3 | 0 |
| `generated_symbol_table` | 1 | 0 |
| `kernel_assembly` | 3 | 536 |
| `kernel_audio` | 14 | 4541 |
| `kernel_core` | 17 | 4067 |
| `kernel_cpu` | 19 | 3347 |
| `kernel_crypto` | 40 | 5149 |
| `kernel_fs` | 27 | 7339 |
| `kernel_gfx` | 28 | 12231 |
| `kernel_gui` | 28 | 11971 |
| `kernel_lang` | 20 | 10538 |
| `kernel_mm` | 7 | 1302 |
| `kernel_network` | 20 | 3629 |
| `kernel_smp` | 14 | 1162 |
| `kernel_tls` | 13 | 6661 |
| `kernel_usb` | 8 | 3518 |
| `kernel_util` | 2 | 660 |
| `project_source` | 1 | 5 |
| `toolchain_contract` | 17 | 148878 |
| `toolchain_core` | 33 | 86097 |
| `toolchain_host_adapter` | 2 | 266 |
| `toolchain_kernel_adapter` | 2 | 530 |
| `user_program` | 3 | 139 |
| `user_runtime_interface` | 1 | 360 |
| `vendored_doom` | 197 | 66533 |

## Supported build roots

| Directory | Root target | Transforms | Include paths |
| --- | --- | ---: | ---: |
| `.` | `all` | 438 | 20 |
| `user` | `all` | 7 | 0 |
| `toolchain` | `all` | 2 | 2 |

## Current output ownership

| Tool interface | Reachable transforms |
| --- | ---: |
| `cupid_assembler` | 5 |
| `cupid_c_compiler` | 245 |
| `cupid_disassembler` | 1 |
| `cupid_linker` | 5 |
| `cupid_object` | 189 |
| `host_python` | 447 |

## Feature inventory

| Feature family | Distinct requirements | Lexical/build occurrences |
| --- | ---: | ---: |
| `asm.addressing` | 6 | 145 |
| `asm.delivery` | 1 | 22 |
| `asm.directive` | 19 | 321 |
| `asm.expression` | 2 | 13 |
| `asm.instruction` | 91 | 1278 |
| `asm.label` | 2 | 163 |
| `asm.output` | 2 | 5 |
| `asm.prefix` | 2 | 6 |
| `asm.preprocessor` | 2 | 5 |
| `asm.register` | 27 | 801 |
| `asm.relocation` | 1 | 17 |
| `c.control` | 12 | 80861 |
| `c.declaration` | 1 | 28 |
| `c.declarator` | 4 | 3770 |
| `c.expression` | 2 | 5760 |
| `c.extension` | 19 | 416 |
| `c.initializer` | 1 | 654 |
| `c.preprocessor` | 18 | 6895 |
| `c.qualifier` | 2 | 15365 |
| `c.storage` | 4 | 9791 |
| `c.type` | 15 | 50411 |
| `cupid_c.declaration` | 1 | 2 |
| `cupid_c.delivery` | 2 | 129 |
| `cupid_c.directive` | 1 | 1 |
| `cupid_c.expression` | 2 | 4 |
| `cupid_c.extension` | 1 | 9 |
| `cupid_c.output` | 1 | 245 |
| `cupid_c.storage` | 2 | 474 |
| `cupid_c.type` | 12 | 180 |

The JSON companion records stable feature IDs, occurrence counts, files, and representative source locations.

## ABI and object contract

| Property | Required value |
| --- | --- |
| Architecture | `i386` |
| Data model | `ILP32` |
| Endianness | `little` |
| Calling convention | `cdecl` |
| Object interchange | `ELF32 ET_REL` |
| Required relocations | `R_386_32, R_386_PC32` |
| Stack alignment | 16 bytes |

`link.ld` has SHA-256 `69da6839c814f7d5b3d166c531184ebb7c35757f5523d4b4d2db37d9123678fe` and uses `ALIGN`, `ASSERT`, `COMMON`, `ENTRY`, `SECTIONS`, `input_section_wildcards`, `location_counter`, `symbol_definitions`.
It is also a declared Make prerequisite.

## Source-driven capability priority

| Rank | Capability | Source evidence |
| ---: | --- | ---: |
| 1 | `host_runnable_toolchain_core` - Establish a host-runnable shared Cupid Toolchain core | 74 |
| 2 | `elf32_relocatable_interchange` - Emit and consume deterministic ELF32 relocatable objects | 247 |
| 3 | `shared_i386_abi_and_instruction_model` - Share one i386 ABI and instruction model | 67 |
| 4 | `cupiddis_object_inspection` - Make CupidDis inspect raw and ELF32 relocatable output | 12 |
| 5 | `cupidasm_source_controls_and_expressions` - Implement the active Cupid ASM directives and expression language | 29 |
| 6 | `cupidasm_encoding_and_raw_parity` - Reach byte parity for boot and trampoline binaries | 17 |
| 7 | `cupidasm_symbols_and_relocations` - Emit ELF32 sections, symbols, and i386 relocations | 5 |
| 8 | `cupidc_preprocessor` - Implement the active C and Cupid C preprocessing contract | 549 |
| 9 | `cupidc_c11_types_initializers_and_abi` - Implement freestanding C11 type, initializer, and cdecl semantics | 653 |
| 10 | `cupidc_platform_extensions` - Implement required GNU attributes and extended inline assembly | 58 |
| 11 | `cupidc_doom_compatibility` - Compile the complete Doom and compatibility cohort | 204 |
| 12 | `cupid_mode_production_and_extensions` - Scale Cupid mode across embedded programs and browser fragments | 378 |

## Source-cohort migration order

| Rank | Cohort step | Files | Rationale |
| ---: | --- | ---: | --- |
| 1 | `toolchain_sources` | 74 | Bootstrap the tools that transfer ownership to every later cohort. |
| 2 | `boot_and_kernel_assembly` | 4 | Keep the four boot and kernel transforms plus the ISO lane fixture CupidASM-owned while retaining NASM only as an optional parity oracle. |
| 3 | `kernel_and_drivers` | 279 | Move foundational strict C before vendored compatibility cohorts. |
| 4 | `doom_and_vendored_c` | 204 | Preserve upstream behavior under a deliberate compatibility mode. |
| 5 | `user_programs` | 4 | Keep the checked-seed CupidC and CupidLD user build reproducible on Linux and Windows, keep the native Windows oracle explicit, then stage its validated executables deliberately. |
| 6 | `embedded_cupid_sources` | 151 | Keep runtime CupidC/CupidASM regression corpora active through the host migration. |

## Unreachable source classification

| Classification | Files |
| --- | ---: |
| `explicitly_excluded` | 2 |
| `historical_copy` | 7 |
| `not_reached` | 12 |
| `superseded` | 4 |

An exact content match does not by itself prove semantic duplication; path-sensitive compatibility headers remain removal-blocked.

| Path | Language | Classification | Lines | Evidence |
| --- | --- | --- | ---: | --- |
| `bin/browser/gen_css_keywords.h` | `c_header` | `not_reached` | 1326 | not reachable from the supported Make target or include closure |
| `bin/browser/gen_css_properties.h` | `c_header` | `not_reached` | 1302 | not reachable from the supported Make target or include closure |
| `bin/browser/gen_media_features.h` | `c_header` | `not_reached` | 50 | not reachable from the supported Make target or include closure |
| `bin/build.cup` | `cupid_script` | `not_reached` | 46 | not reachable from the supported Make target or include closure |
| `bin/cupidc.c` | `c` | `historical_copy` | 1955 | historical_copy_of: `kernel/lang/cupidc.cc` |
| `bin/cupidc_lex.c` | `c` | `historical_copy` | 647 | historical_copy_of: `kernel/lang/cupidc_lex.cc` |
| `bin/cupidc_parse.c` | `c` | `historical_copy` | 4111 | historical_copy_of: `kernel/lang/cupidc_parse.cc` |
| `bin/fat16.c` | `c` | `historical_copy` | 1468 | historical_copy_of: `kernel/fs/fat16.cc` |
| `bin/fat16_vfs.c` | `c` | `historical_copy` | 423 | historical_copy_of: `kernel/fs/fat16_vfs.cc` |
| `bin/kernel.c` | `c` | `historical_copy` | 719 | historical_copy_of: `kernel/core/kernel.cc` |
| `bin/old_cc2.cc` | `cupid_c` | `explicitly_excluded` | 2 | listed in a Make filter-out expression |
| `bin/old_cc2_single.cc` | `cupid_c` | `explicitly_excluded` | 6744 | listed in a Make filter-out expression |
| `bin/terminal_app.c` | `c` | `historical_copy` | 318 | historical_copy_of: `kernel/gui/terminal_app.cc` |
| `demos/paint.cc` | `cupid_c` | `superseded` | 622 | superseded_by: `bin/paint.cc` |
| `kernel/core/scheduler.c` | `c` | `superseded` | 154 | superseded_by: `kernel/core/process.cc` |
| `kernel/gui/notepad.c` | `c` | `superseded` | 5683 | superseded_by: `bin/notepad.cc` |
| `kernel/gui/terminal_ansi.c` | `c` | `superseded` | 285 | superseded_by: `kernel/gui/ansi.cc` |
| `kernel/lang/cupidc_runtime.c` | `c` | `not_reached` | 284 | not reachable from the supported Make target or include closure |
| `tests/kernel_contract_support/percpu.h` | `c_header` | `not_reached` | 43 | not reachable from the supported Make target or include closure |
| `tests/kernel_exec_contract.c` | `c` | `not_reached` | 601 | not reachable from the supported Make target or include closure |
| `tests/kernel_process_contract.c` | `c` | `not_reached` | 929 | not reachable from the supported Make target or include closure |
| `tests/usb_interrupt_ownership_contract.c` | `c` | `not_reached` | 50 | not reachable from the supported Make target or include closure |
| `tests/usb_msc_lifetime_contract.c` | `c` | `not_reached` | 150 | not reachable from the supported Make target or include closure |
| `tests/usb_reconciliation_runtime.c` | `c` | `not_reached` | 728 | not reachable from the supported Make target or include closure |
| `toolchain/tests/elf32_oracle.c` | `c` | `not_reached` | 8 | not reachable from the supported Make target or include closure |

## Audit contracts

| Contract | Status | Detail |
| --- | --- | --- |
| `bootstrap_artifact_coverage` | `pass` | 425 linked objects; 432 declared artifacts; 0 missing |
| `c_preprocessor_conditionals` | `pass` | 124 conditional expressions (115 #if, 9 #elif); 26 normalized expressions; 28 directive/expression pairs |
| `c_preprocessor_cupid_exe` | `pass` | 1 Cupid #exe blocks (1 #, 0 %:); max conditional depth 0 |
| `c_preprocessor_include_operands` | `pass` | 2410 C include operands (2173 quoted, 237 angle, 0 pp-token); 688 source files; max conditional depth 2 |
| `c_preprocessor_line_directives` | `pass` | 0 named #line directives (0 direct, 0 pp-token; 0 filename); 0 numeric markers; 688 source files; max conditional depth 0 |
| `c_preprocessor_pragmas` | `pass` | 5 pragmas (1 once, 2 pack pushes, 2 pack pops); pack balanced: yes; max pack depth 1 |
| `c_preprocessor_translation_units` | `pass` | 382 tracked + 4 generated translation units (KERNEL_I386=155, DOOM_COMPAT_I386=3, DOOM_TREE_I386=80, USER_I386=3, FREESTANDING_I386=1, CUPID_RUNTIME=105, HOSTED_TOOLCHAIN_64=0, HOSTED_KERNEL_BRIDGE_64=0, HOSTED_I386_LINUX=31, HOSTED_I386_KERNEL_BRIDGE=2, HOSTED_I386_LINUX_GNU=2); 22 include-only, 2 non-root headers; 0 hosted deferred (0 external, 0 hermetic) |
| `cupid_toolchain_fixed_point` | `pass` | 19 tool C sources (18 strict, 1 GNU); 5 tools (cupidasm=8, cupiddis=8, cupidld=7, cupidobj=7, cupidc=12); 19 C objects and 1 startup object compared across stages; 5 tool images; 17 success and 15 failure cases; i386-linux |

## Interpretation limits

- Feature occurrences are comment/string-masked lexical evidence, not a substitute for a compiler AST or executed semantic tests.
- Include reachability follows checked Make include paths, forced includes, quoted/angle C includes, and `%include`; the conditional contract records normalized source expressions while evaluation remains a compiler-contract responsibility.
- Named `#line` pp-token operands are classified before macro expansion; the CupidC corpus harness owns expansion and semantic validation.
- Relocation kinds and ABI values are required interchange contracts; per-object relocation counts are recorded in the chronological bootstrap log.
- `not_reached` means absent from the supported roots recorded above, not automatically safe to delete.

