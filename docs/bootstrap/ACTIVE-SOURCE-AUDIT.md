# Active build and source audit

This file is generated deterministically by `tools/build_graph_audit.py` from the supported Make graph and source tree.

## Scope

- Root Make target: `all`
- Supplemental builds: `user:all`, `toolchain:all`
- Active source inputs: 688
- Unreachable source-like files: 39
- Reachable output transforms: 498
- Distinct feature requirements: 251
- The `TempleOS/` reference tree is excluded.
- Source and control-file SHA-256 values use canonical LF text bytes.

Generated C translation units are recorded as reachable build inputs but have no source hash or lexical features; their content is owned by the recorded generator transform.

## Active language inputs

| Language | Files |
| --- | ---: |
| `assembly` | 26 |
| `c` | 271 |
| `c_header` | 264 |
| `cupid_c` | 127 |

## Source cohorts

| Cohort | Files | Checked-source lines |
| --- | ---: | ---: |
| `boot_assembly` | 1 | 302 |
| `cupid_asm_demo` | 22 | 1463 |
| `cupid_c_browser_fragment` | 22 | 14185 |
| `cupid_c_program` | 105 | 18480 |
| `cupid_c_runtime_header` | 2 | 286 |
| `cupidasm` | 7 | 6678 |
| `cupidc` | 7 | 13081 |
| `cupiddis` | 5 | 2670 |
| `doom_port` | 7 | 2638 |
| `driver` | 22 | 3774 |
| `generated_install_table` | 3 | 0 |
| `generated_symbol_table` | 1 | 0 |
| `kernel_assembly` | 3 | 536 |
| `kernel_audio` | 14 | 4532 |
| `kernel_core` | 16 | 3985 |
| `kernel_cpu` | 18 | 3274 |
| `kernel_crypto` | 40 | 5144 |
| `kernel_fs` | 26 | 6114 |
| `kernel_gfx` | 28 | 12182 |
| `kernel_gui` | 28 | 11971 |
| `kernel_lang` | 20 | 10542 |
| `kernel_mm` | 7 | 1291 |
| `kernel_network` | 20 | 3629 |
| `kernel_smp` | 14 | 1161 |
| `kernel_tls` | 13 | 6409 |
| `kernel_usb` | 8 | 1701 |
| `kernel_util` | 2 | 660 |
| `toolchain_contract` | 14 | 88011 |
| `toolchain_core` | 22 | 54718 |
| `toolchain_host_adapter` | 2 | 266 |
| `toolchain_kernel_adapter` | 2 | 530 |
| `user_program` | 3 | 154 |
| `user_runtime_interface` | 1 | 357 |
| `vendored_doom` | 183 | 65784 |

## Supported build roots

| Directory | Root target | Transforms | Include paths |
| --- | --- | ---: | ---: |
| `.` | `all` | 439 | 20 |
| `user` | `all` | 8 | 1 |
| `toolchain` | `all` | 51 | 2 |

## Current output ownership

| Tool interface | Reachable transforms |
| --- | ---: |
| `cupid_assembler` | 4 |
| `cupid_disassembler` | 1 |
| `cupid_linker` | 5 |
| `cupid_object` | 182 |
| `host_c_compiler` | 295 |
| `host_python` | 8 |
| `make` | 5 |

## Feature inventory

| Feature family | Distinct requirements | Lexical/build occurrences |
| --- | ---: | ---: |
| `asm.addressing` | 6 | 122 |
| `asm.delivery` | 1 | 22 |
| `asm.directive` | 18 | 301 |
| `asm.expression` | 2 | 12 |
| `asm.instruction` | 90 | 1212 |
| `asm.label` | 2 | 156 |
| `asm.output` | 2 | 4 |
| `asm.prefix` | 2 | 6 |
| `asm.preprocessor` | 2 | 5 |
| `asm.register` | 27 | 738 |
| `asm.relocation` | 1 | 12 |
| `c.control` | 12 | 61817 |
| `c.declaration` | 1 | 22 |
| `c.declarator` | 4 | 2729 |
| `c.expression` | 2 | 3799 |
| `c.extension` | 18 | 373 |
| `c.initializer` | 1 | 637 |
| `c.output` | 1 | 245 |
| `c.preprocessor` | 18 | 6484 |
| `c.qualifier` | 2 | 10905 |
| `c.storage` | 4 | 7361 |
| `c.type` | 14 | 42346 |
| `cupid_c.declaration` | 1 | 2 |
| `cupid_c.delivery` | 2 | 129 |
| `cupid_c.directive` | 1 | 1 |
| `cupid_c.expression` | 2 | 4 |
| `cupid_c.extension` | 1 | 9 |
| `cupid_c.storage` | 2 | 2 |
| `cupid_c.type` | 12 | 151 |

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

`link.ld` has SHA-256 `29fe0e78e8e231752d5fb08f8e974095c1590546ccf370ddc07f7633ffbf80b5` and uses `ALIGN`, `ASSERT`, `COMMON`, `ENTRY`, `SECTIONS`, `input_section_wildcards`, `location_counter`, `symbol_definitions`.
It is also a declared Make prerequisite.

## Source-driven capability priority

| Rank | Capability | Source evidence |
| ---: | --- | ---: |
| 1 | `host_runnable_toolchain_core` - Establish a host-runnable shared Cupid Toolchain core | 59 |
| 2 | `elf32_relocatable_interchange` - Emit and consume deterministic ELF32 relocatable objects | 247 |
| 3 | `shared_i386_abi_and_instruction_model` - Share one i386 ABI and instruction model | 61 |
| 4 | `cupiddis_object_inspection` - Make CupidDis inspect raw and ELF32 relocatable output | 9 |
| 5 | `cupidasm_source_controls_and_expressions` - Implement the active Cupid ASM directives and expression language | 26 |
| 6 | `cupidasm_encoding_and_raw_parity` - Reach byte parity for boot and trampoline binaries | 14 |
| 7 | `cupidasm_symbols_and_relocations` - Emit ELF32 sections, symbols, and i386 relocations | 3 |
| 8 | `cupidc_preprocessor` - Implement the active C and Cupid C preprocessing contract | 528 |
| 9 | `cupidc_c11_types_initializers_and_abi` - Implement freestanding C11 type, initializer, and cdecl semantics | 631 |
| 10 | `cupidc_platform_extensions` - Implement required GNU attributes and extended inline assembly | 55 |
| 11 | `cupidc_doom_compatibility` - Compile the complete Doom and compatibility cohort | 190 |
| 12 | `cupid_mode_production_and_extensions` - Scale Cupid mode across embedded programs and browser fragments | 129 |

## Source-cohort migration order

| Rank | Cohort step | Files | Rationale |
| ---: | --- | ---: | --- |
| 1 | `toolchain_sources` | 59 | Bootstrap the tools that transfer ownership to every later cohort. |
| 2 | `boot_and_kernel_assembly` | 4 | Keep the four production transforms CupidASM-owned while retaining NASM only as an optional parity oracle. |
| 3 | `kernel_and_drivers` | 276 | Move foundational strict C before vendored compatibility cohorts. |
| 4 | `doom_and_vendored_c` | 190 | Preserve upstream behavior under a deliberate compatibility mode. |
| 5 | `user_programs` | 4 | Migrate the remaining separate host-C compilation path to CupidC and stage its CupidLD outputs deliberately. |
| 6 | `embedded_cupid_sources` | 151 | Keep runtime CupidC/CupidASM regression corpora active through the host migration. |

## Unreachable source classification

| Classification | Files |
| --- | ---: |
| `exact_duplicate` | 7 |
| `explicitly_excluded` | 2 |
| `historical_copy` | 7 |
| `not_reached` | 18 |
| `superseded` | 5 |

An exact content match does not by itself prove semantic duplication; path-sensitive compatibility headers remain removal-blocked.

| Path | Language | Classification | Lines | Evidence |
| --- | --- | --- | ---: | --- |
| `bin/browser/gen_css_keywords.h` | `c_header` | `not_reached` | 1326 | not reachable from the supported Make target or include closure |
| `bin/browser/gen_css_properties.h` | `c_header` | `not_reached` | 1302 | not reachable from the supported Make target or include closure |
| `bin/browser/gen_media_features.h` | `c_header` | `not_reached` | 50 | not reachable from the supported Make target or include closure |
| `bin/build.cup` | `cupid_script` | `not_reached` | 46 | not reachable from the supported Make target or include closure |
| `bin/cupidc.c` | `c` | `historical_copy` | 1955 | historical_copy_of: `kernel/lang/cupidc.c` |
| `bin/cupidc_lex.c` | `c` | `historical_copy` | 647 | historical_copy_of: `kernel/lang/cupidc_lex.c` |
| `bin/cupidc_parse.c` | `c` | `historical_copy` | 4111 | historical_copy_of: `kernel/lang/cupidc_parse.c` |
| `bin/fat16.c` | `c` | `historical_copy` | 1468 | historical_copy_of: `kernel/fs/fat16.c` |
| `bin/fat16_vfs.c` | `c` | `historical_copy` | 423 | historical_copy_of: `kernel/fs/fat16_vfs.c` |
| `bin/kernel.c` | `c` | `historical_copy` | 719 | historical_copy_of: `kernel/core/kernel.c` |
| `bin/old_cc2.cc` | `cupid_c` | `explicitly_excluded` | 2 | listed in a Make filter-out expression |
| `bin/old_cc2_single.cc` | `cupid_c` | `explicitly_excluded` | 6744 | listed in a Make filter-out expression |
| `bin/terminal_app.c` | `c` | `historical_copy` | 318 | historical_copy_of: `kernel/gui/terminal_app.c` |
| `demos/paint.cc` | `cupid_c` | `superseded` | 622 | superseded_by: `bin/paint.cc` |
| `kernel/core/scheduler.c` | `c` | `superseded` | 154 | superseded_by: `kernel/core/process.c` |
| `kernel/core/scheduler.h` | `c_header` | `superseded` | 42 | superseded_by: `kernel/core/process.h` |
| `kernel/cpu/simd_intrin.h` | `c_header` | `not_reached` | 71 | not reachable from the supported Make target or include closure |
| `kernel/doom/src/d_textur.h` | `c_header` | `not_reached` | 43 | not reachable from the supported Make target or include closure |
| `kernel/doom/src/doom.h` | `c_header` | `not_reached` | 42 | not reachable from the supported Make target or include closure |
| `kernel/doom/src/gusconf.h` | `c_header` | `not_reached` | 29 | not reachable from the supported Make target or include closure |
| `kernel/doom/src/i_cdmus.h` | `c_header` | `not_reached` | 41 | not reachable from the supported Make target or include closure |
| `kernel/doom/src/include_stubs/assert.h` | `c_header` | `exact_duplicate` | 1 | content match: `kernel/doom/src/include_stubs/ctype.h`, `kernel/doom/src/include_stubs/errno.h`, `kernel/doom/src/include_stubs/fcntl.h` (+13 more) |
| `kernel/doom/src/include_stubs/machine/endian.h` | `c_header` | `exact_duplicate` | 1 | content match: `kernel/doom/src/include_stubs/sys/stat.h`, `kernel/doom/src/include_stubs/sys/types.h` |
| `kernel/doom/src/include_stubs/math.h` | `c_header` | `exact_duplicate` | 1 | content match: `kernel/doom/src/include_stubs/assert.h`, `kernel/doom/src/include_stubs/ctype.h`, `kernel/doom/src/include_stubs/errno.h` (+13 more) |
| `kernel/doom/src/include_stubs/setjmp.h` | `c_header` | `exact_duplicate` | 1 | content match: `kernel/doom/src/include_stubs/assert.h`, `kernel/doom/src/include_stubs/ctype.h`, `kernel/doom/src/include_stubs/errno.h` (+13 more) |
| `kernel/doom/src/include_stubs/stddef.h` | `c_header` | `exact_duplicate` | 1 | content match: `kernel/doom/src/include_stubs/assert.h`, `kernel/doom/src/include_stubs/ctype.h`, `kernel/doom/src/include_stubs/errno.h` (+13 more) |
| `kernel/doom/src/include_stubs/string.h` | `c_header` | `exact_duplicate` | 1 | content match: `kernel/doom/src/include_stubs/assert.h`, `kernel/doom/src/include_stubs/ctype.h`, `kernel/doom/src/include_stubs/errno.h` (+13 more) |
| `kernel/doom/src/include_stubs/time.h` | `c_header` | `exact_duplicate` | 1 | content match: `kernel/doom/src/include_stubs/assert.h`, `kernel/doom/src/include_stubs/ctype.h`, `kernel/doom/src/include_stubs/errno.h` (+13 more) |
| `kernel/doom/src/memio.h` | `c_header` | `not_reached` | 38 | not reachable from the supported Make target or include closure |
| `kernel/doom/src/mus2mid.h` | `c_header` | `not_reached` | 9 | not reachable from the supported Make target or include closure |
| `kernel/doom/src/net_packet.h` | `c_header` | `not_reached` | 44 | not reachable from the supported Make target or include closure |
| `kernel/gui/notepad.c` | `c` | `superseded` | 5683 | superseded_by: `bin/notepad.cc` |
| `kernel/gui/terminal_ansi.c` | `c` | `superseded` | 285 | superseded_by: `kernel/gui/ansi.c` |
| `kernel/lang/cupidc_runtime.c` | `c` | `not_reached` | 284 | not reachable from the supported Make target or include closure |
| `kernel/lang/cupidc_runtime.h` | `c_header` | `not_reached` | 66 | not reachable from the supported Make target or include closure |
| `tests/kernel_contract_support/percpu.h` | `c_header` | `not_reached` | 43 | not reachable from the supported Make target or include closure |
| `tests/kernel_exec_contract.c` | `c` | `not_reached` | 579 | not reachable from the supported Make target or include closure |
| `tests/kernel_process_contract.c` | `c` | `not_reached` | 929 | not reachable from the supported Make target or include closure |
| `toolchain/tests/elf32_oracle.c` | `c` | `not_reached` | 8 | not reachable from the supported Make target or include closure |

## Audit contracts

| Contract | Status | Detail |
| --- | --- | --- |
| `bootstrap_artifact_coverage` | `pass` | 425 linked objects; 432 declared artifacts; 0 missing |
| `c_preprocessor_conditionals` | `pass` | 101 conditional expressions (97 #if, 4 #elif); 21 normalized expressions; 22 directive/expression pairs |
| `c_preprocessor_cupid_exe` | `pass` | 1 Cupid #exe blocks (1 #, 0 %:); max conditional depth 0 |
| `c_preprocessor_include_operands` | `pass` | 2343 C include operands (2132 quoted, 211 angle, 0 pp-token); 658 source files; max conditional depth 2 |
| `c_preprocessor_line_directives` | `pass` | 0 named #line directives (0 direct, 0 pp-token; 0 filename); 0 numeric markers; 658 source files; max conditional depth 0 |
| `c_preprocessor_pragmas` | `pass` | 5 pragmas (1 once, 2 pack pushes, 2 pack pops); pack balanced: yes; max pack depth 1 |
| `c_preprocessor_translation_units` | `pass` | 359 tracked + 4 generated translation units (KERNEL_I386=152, DOOM_COMPAT_I386=6, DOOM_TREE_I386=80, USER_I386=3, CUPID_RUNTIME=105, HOSTED_TOOLCHAIN_64=12, HOSTED_KERNEL_BRIDGE_64=1); 22 include-only, 2 non-root headers; 19 hosted deferred (19 external, 0 hermetic) |

## Interpretation limits

- Feature occurrences are comment/string-masked lexical evidence, not a substitute for a compiler AST or executed semantic tests.
- Include reachability follows checked Make include paths, forced includes, quoted/angle C includes, and `%include`; the conditional contract records normalized source expressions while evaluation remains a compiler-contract responsibility.
- Named `#line` pp-token operands are classified before macro expansion; the CupidC corpus harness owns expansion and semantic validation.
- Relocation kinds and ABI values are required interchange contracts; per-object relocation counts are recorded in the chronological bootstrap log.
- `not_reached` means absent from the supported roots recorded above, not automatically safe to delete.

