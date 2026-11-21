## @file
#  @brief Check fixed-point inputs and reject altered toolchain evidence.
import shutil
import tempfile
import textwrap
from pathlib import Path
from tests.c_source_spelling import _c_spelling_ranges
from tests.build_graph_bootstrap_mutations import bootstrap_mutations
from tests.build_graph_audit_fixtures import (
    REPO_ROOT,
    _load_audit_module,
)


class FixedPointGraphCases:
    def test_cupid_toolchain_fixed_point_contract_fails_closed(self):
        module = _load_audit_module()
        contract = module._cupid_toolchain_fixed_point_contract(REPO_ROOT)
        self.assertEqual(contract["help_cases"], 7)
        self.assertEqual(contract["success_behavior_cases"], 73)
        self.assertEqual(contract["failure_behavior_cases"], 66)
        self.assertEqual(contract["tool_c_sources"], 44)
        self.assertEqual(contract["tool_images"], 6)
        self.assertEqual(contract["compared_c_objects"], 44)
        self.assertEqual(contract["compared_tool_images"], 6)
        self.assertEqual(contract["windows_help_cases"], 7)
        self.assertEqual(contract["windows_success_behavior_cases"], 60)
        self.assertEqual(contract["windows_failure_behavior_cases"], 54)
        self.assertEqual(contract["contract_manifest_inputs"], 125)
        self.assertEqual(len(module.USER_SYSCALL_ABI_PUBLICATION_INPUTS), 125)
        self.assertIn(
            "toolchain/x86.cc",
            module.USER_SYSCALL_ABI_PUBLICATION_INPUTS,
        )
        self.assertEqual(len(module.TOOLCHAIN_CONTRACT_LINUX_INPUTS), 177)
        self.assertIn("toolchain/tests/cupidld_pe32_cases.inc", module.TOOLCHAIN_CONTRACT_LINUX_INPUTS)
        self.assertTrue(
            set(module.USER_SYSCALL_ABI_PUBLICATION_INPUTS).issubset(
                module.TOOLCHAIN_CONTRACT_LINUX_INPUTS
            )
        )
        self.assertNotEqual(
            contract["contract_manifest_inputs"],
            len(module.TOOLCHAIN_CONTRACT_LINUX_INPUTS),
        )
        self.assertEqual(
            contract["source_head_capabilities"],
            [
                "cupid.cupidbuild_native_user_abi",
                "cupid.cupidbuild_checked_cupidc_runner",
                "cupid.cupidbuild_checked_cupidobj_runner",
                "cupid.cupidbuild_guarded_object_transaction",
                "cupid.cupidbuild_guarded_raw_transaction",
                "cupid.cupidbuild_typed_jpeg_transaction",
                "cupid.cupidbuild_typed_ksyms_transaction",
                "cupid.cupidbuild_typed_profile_transaction",
                "cupiddis.candidate_image_certification",
                "cupiddis.elf32_code_anchors",
                "cupidld.pe32_fixed_image",
                "cupidld.pe32_imports",
                "cupid.windows_cupidasm",
                "cupid.windows_cupidc",
                "cupid.windows_cupiddis",
                "cupid.windows_cupidld",
                "cupid.windows_cupidobj",
                "cupid.windows_runtime_contract",
                "cupid.windows_runtime_probe",
            ],
        )
        driver = (REPO_ROOT / "toolchain" / "cupidc_main.cc").read_text(
            encoding="utf-8"
        )
        test = (
            REPO_ROOT / "tests" / "test_toolchain_cupidc_object.py"
        ).read_text(encoding="utf-8")
        bootstrap = (REPO_ROOT / "tools" / "bootstrap_toolchain.py").read_text(
            encoding="utf-8"
        )
        contract_publisher = (
            REPO_ROOT / "tools" / "cupidc_toolchain_contracts.py"
        ).read_text(encoding="utf-8")
        windows_runtime_contract = (
            REPO_ROOT
            / "toolchain"
            / "tests"
            / "hosted_i386_windows_runtime_contract.cc"
        ).read_text(encoding="utf-8")
        windows_publication_header = (
            REPO_ROOT
            / "toolchain"
            / "hosted"
            / "i386-linux"
            / "include"
            / "windows.h"
        ).read_text(encoding="utf-8")
        windows_publication_runtime = (
            REPO_ROOT
            / "toolchain"
            / "hosted"
            / "i386-windows"
            / "publication_runtime.cc"
        ).read_text(encoding="utf-8")
        windows_publication_start = (
            REPO_ROOT
            / "toolchain"
            / "hosted"
            / "i386-windows"
            / "publication_start.asm"
        ).read_text(encoding="utf-8")
        linker_header = (REPO_ROOT / "toolchain" / "cupidld.h").read_text(
            encoding="utf-8"
        )
        linker_cli = (REPO_ROOT / "toolchain" / "cupidld_main.cc").read_text(
            encoding="utf-8"
        )
        linker_core = (REPO_ROOT / "toolchain" / "cupidld.cc").read_text(
            encoding="utf-8"
        )
        verifier_start = linker_cli.index(
            "static ctool_status_t cupidld_publication_verify("
        )
        verifier_end = linker_cli.index(
            "\nstatic ctool_status_t cupidld_publication_write_all(",
            verifier_start,
        )
        verifier_feature = linker_cli[verifier_start:verifier_end]
        publication_verify_guard = (
            "  if (status == CTOOL_OK) {\n"
            "    status = ops->verify(candidate, contents);\n"
            "  }\n"
        )
        publication_replace_guard = (
            "  if (status == CTOOL_OK) {\n"
            "    status = ops->replace(candidate, destination);\n"
            "  }\n"
        )
        verifier_close_tail = (
            "  if (fclose(file) != 0) {\n"
            "    status = CTOOL_ERR_IO;\n"
            "  }\n"
            "  return status;\n"
        )
        serializer_start = linker_core.index(
            "static ctool_status_t ld_serialize_pe32_fixed("
        )
        serializer_end = linker_core.index(
            "\nstatic ctool_bool ld_name_start(", serializer_start
        )
        serializer_feature = linker_core[serializer_start:serializer_end]
        core_dispatch_start = linker_core.index(
            "    if (status == CTOOL_OK) {\n"
            '        phase = "CupidLD executable serialization failed";\n'
            "        if (request->image_kind == CTOOL_LD_IMAGE_PE32_FIXED) {\n"
        )
        core_dispatch_end = linker_core.index(
            "\n    if (status != CTOOL_OK &&", core_dispatch_start
        )
        core_dispatch_feature = linker_core[
            core_dispatch_start:core_dispatch_end
        ]
        allocator_contract_start = windows_runtime_contract.index(
            "static int allocator_contract(void) {"
        )
        allocator_contract_end = windows_runtime_contract.index(
            "\nstatic int file_contract(", allocator_contract_start
        )
        file_contract_start = allocator_contract_end + 1
        file_contract_end = windows_runtime_contract.index(
            "\nstatic int directory_contract(void) {", file_contract_start
        )
        directory_contract_start = file_contract_end + 1
        directory_contract_end = windows_runtime_contract.index(
            "\nint main(", directory_contract_start
        )

        def hide_contract_body(feature: str) -> str:
            opening = feature.index("{") + 1
            final_return = feature.rindex("  return 0;\n")
            body = feature[opening + 1 : final_return]
            return (
                feature[:opening]
                + "\n  if (0) {\n"
                + textwrap.indent(body, "  ")
                + "  }\n"
                + feature[final_return:]
            )

        allocator_contract_feature = windows_runtime_contract[
            allocator_contract_start:allocator_contract_end
        ]
        file_contract_feature = windows_runtime_contract[
            file_contract_start:file_contract_end
        ]
        directory_contract_feature = windows_runtime_contract[
            directory_contract_start:directory_contract_end
        ]
        native_build_loop_start = bootstrap.index(
            "    for tool_name, link_objects in windows_native_tool_plans.items():\n"
        )
        native_build_loop_end = bootstrap.index(
            "\n\n    windows_native_tool_loaders:",
            native_build_loop_start,
        )
        native_build_loop_feature = bootstrap[
            native_build_loop_start:native_build_loop_end
        ]
        def replace_c_fragment(source, old, new):
            matches = _c_spelling_ranges(source, old)
            self.assertEqual(len(matches), 1, 'ambiguous or absent C mutation: ' + old)
            start, end = matches[0]
            return source[:start] + new + source[end:]

        mutations = {
            "Windows seek header loses its LONG pointer type": (
                "windows_publication_header",
                "unsigned int handle, LONG distance, LONG *distance_high,",
                "unsigned int handle, LONG distance, int *distance_high,",
                r"fixed-point Windows publication contract differs",
            ),
            "Windows final-path declaration changes calling arguments": (
                "windows_publication_header",
                "DWORD cupid_windows_get_final_path_name_by_handle_wide(\n    HANDLE handle, unsigned short *destination, DWORD capacity, DWORD flags);",
                "DWORD cupid_windows_get_final_path_name_by_handle_wide(\n    HANDLE handle, unsigned short *destination, DWORD capacity);",
                r"fixed-point Windows publication contract differs",
            ),
            "angle root widened": (
                "driver",
                "cli->include_forms[cli->include_count] = CTOOL_C_PP_INCLUDE_ANGLE;",
                "cli->include_forms[cli->include_count] = "
                "CTOOL_C_PP_INCLUDE_QUOTED | CTOOL_C_PP_INCLUDE_ANGLE;",
                r"does not retain its exact include contract",
            ),
            "forced include count disappears": (
                "driver",
                "pp_request.forced_include_count = context->forced_include_count;",
                "pp_request.forced_include_count = 0u;",
                r"does not retain its exact include contract",
            ),
            "runtime loses GNU mode": (
                "test",
                "CUPID_TOOLCHAIN_FIXED_POINT_SOURCES = (\n"
                '    ("runtime", '
                '"/toolchain/hosted/i386-linux/runtime.cc", True),',
                "CUPID_TOOLCHAIN_FIXED_POINT_SOURCES = (\n"
                '    ("runtime", '
                '"/toolchain/hosted/i386-linux/runtime.cc", False),',
                r"fixed-point manifest differs: "
                r"CUPID_TOOLCHAIN_FIXED_POINT_SOURCES",
            ),
            "ABI root loses angle-only option": (
                "test",
                '"--include-angle",\n    "/toolchain/hosted/i386-linux/include",',
                '"-I",\n    "/toolchain/hosted/i386-linux/include",',
                r"fixed-point manifest differs: "
                r"CUPIDC_FIXED_POINT_INCLUDE_ARGUMENTS",
            ),
            "runtime drops from link order": (
                "test",
                '            "x86",\n'
                '            "runtime",\n'
                "        ),\n"
                "    ),\n"
                "    (\n"
                '        "cupiddis",',
                '            "x86",\n        ),\n    ),\n    (\n        "cupiddis",',
                r"fixed-point manifest differs: "
                r"CUPID_TOOLCHAIN_FIXED_POINT_LINKS",
            ),
            "stage one builds stage three": (
                "test",
                "stage_three_objects, stage_three_tools = build_stage(\n"
                '                stage_two_producers, "stage-three"\n'
                "            )",
                "stage_three_objects, stage_three_tools = build_stage(\n"
                '                generation_one_producers, "stage-three"\n'
                "            )",
                r"fixed-point staged comparison differs",
            ),
            "stage assembler reuses generation one": (
                "test",
                '                    producers["cupidasm"],',
                '                    generation_one_producers["cupidasm"],',
                r"fixed-point staged comparison differs",
            ),
            "stage linker reuses generation one": (
                "test",
                '                        producers["cupidld"],',
                '                        generation_one_producers["cupidld"],',
                r"fixed-point staged comparison differs",
            ),
            "tool image comparison disappears": (
                "test",
                "stage_three_tools[tool_name].read_bytes(),\n"
                "                    stage_two_tools[tool_name].read_bytes(),",
                "stage_three_tools[tool_name].read_bytes(),\n"
                "                    generation_one_tool.read_bytes(),",
                r"fixed-point staged comparison differs",
            ),
            "source object comparison disappears": (
                "test",
                "stage_three_objects[name].read_bytes(),\n"
                "                    stage_two_objects[name].read_bytes(),",
                "stage_two_objects[name].read_bytes(),\n"
                "                    stage_two_objects[name].read_bytes(),",
                r"fixed-point staged comparison differs",
            ),
            "symbol behavior check disappears": (
                "test",
                "stage_two_nm, _stage_three_nm = run_stage_pair(",
                "stage_two_nm, _stage_three_nm = missing_stage_pair(",
                r"fixed-point staged comparison differs",
            ),
            "stage pair runs stage two twice": (
                "test",
                "stage_three_run = self.run_cupid_linux_tool(\n"
                "                    stage_three_tools[tool_name],",
                "stage_three_run = self.run_cupid_linux_tool(\n"
                "                    stage_two_tools[tool_name],",
                r"fixed-point staged comparison differs",
            ),
            "help behavior check disappears": (
                "test",
                "for tool_name in generation_one_tools:\n"
                "                stage_two_help, _stage_three_help = "
                "run_stage_pair(",
                "for tool_name in ():\n"
                "                stage_two_help, _stage_three_help = "
                "run_stage_pair(",
                r"fixed-point staged comparison differs",
            ),
            **bootstrap_mutations(native_build_loop_feature),
            "publication recaptures the checked plan": (
                "contract_publisher",
                "            root, _candidate_build_plan(build_plan), windows_utf8=True,\n",
                "            root, build_plan, windows_utf8=True,\n",
                r"manifest author decision order differs",
            ),
            "publication candidate call is hidden in a false conditional": (
                "contract_publisher",
                "            root, _candidate_build_plan(build_plan), windows_utf8=True,\n",
                "            root, (\n"
                "                _candidate_build_plan(build_plan)\n"
                "                if False\n"
                "                else build_plan\n"
                "            ), windows_utf8=True,\n",
                r"manifest author decision order differs",
            ),
            "PE32 Windows startup leaves the contract manifest": (
                "contract_publisher",
                '    "toolchain/hosted/i386-windows/start.asm",\n',
                '    "toolchain/hosted/i386-linux/start.asm",\n',
                r"fixed-point source freeze differs",
            ),
            "user ABI source leaves the contract manifest": (
                "contract_publisher",
                '    "user/cupid.h",\n',
                '    "user/cupid-x.h",\n',
                r"fixed-point source freeze differs",
            ),
            "Make prerequisite enters the publication inventory": (
                "contract_publisher",
                '    paths.add(root / "kernel/lang/as_elf.h")\n',
                '    paths.add(root / "kernel/lang/as_elf.h")\n'
                '    paths.add(root / "link.ld")\n',
                r"fixed-point publication input closure differs",
            ),
            "SDK release capture leaves the publication inventory": (
                "contract_publisher",
                '    "tools/bootstrap_stage_release.py",\n',
                "",
                r"fixed-point source freeze differs",
            ),
            "SDK release pins leave the publication inventory": (
                "contract_publisher",
                '    "tools/seed_release_identity.py",\n',
                "",
                r"fixed-point source freeze differs",
            ),
            "SDK release ABI capture leaves the publication inventory": (
                "contract_publisher",
                '    "tools/bootstrap_user_abi.py",\n',
                "",
                r"fixed-point source freeze differs",
            ),
            "SDK authority package leaves the publication inventory": (
                "contract_publisher",
                '    "tools/__init__.py",\n',
                "",
                r"fixed-point source freeze differs",
            ),
            "SDK skips release capture before bootstrap": (
                "contract_publisher",
                "    behavior_request = _capture_behavior_request(root, manifest, behavior_release)\n",
                "    behavior_request = None\n",
                r"manifest author decision order differs",
            ),
            "SDK bootstrap loses its captured release": (
                "contract_publisher",
                '**({"release_request": behavior_request} if behavior_request is not None else {}),',
                '**({"release_request": None} if behavior_request is not None else {}),',
                r"manifest author decision order differs",
            ),
            "SDK loses the live check after bootstrap": (
                "contract_publisher",
                '        _announce("checked-seed bootstrap completed")\n'
                "        _require_behavior_request_live(behavior_request)\n",
                '        _announce("checked-seed bootstrap completed")\n',
                r"manifest author decision order differs",
            ),
            "SDK publication loses its captured release": (
                "contract_publisher",
                '**({"behavior_request": behavior_request} if behavior_request is not None else {}),',
                '**({"behavior_request": None} if behavior_request is not None else {}),',
                r"manifest author decision order differs",
            ),
            "SDK replacement skips the final live release check": (
                "contract_publisher",
                "    _require_behavior_request_live(behavior_request)\n"
                "    moved_old = False\n",
                "    moved_old = False\n",
                r"manifest author decision order differs",
            ),
            "SDK cache skips the live release check": (
                "contract_publisher",
                '            _require_behavior_request_live(behavior_request)\n'
                '            _announce("the published cohort is current")\n',
                '            _announce("the published cohort is current")\n',
                r"manifest author decision order differs",
            ),
            "SDK authority capture accepts an unchecked seed": (
                "contract_publisher",
                "return capture_seed_behavior_release(path, verify_seed_inputs(manifest))",
                "return capture_seed_behavior_release(path, manifest)",
                r"manifest author decision order differs",
            ),
            "SDK lifetime helper hides its check in dead code": (
                "contract_publisher",
                "            request.require_live()\n",
                "            if False:\n                request.require_live()\n",
                r"manifest author decision order differs",
            ),
            "contract publisher restores the bootstrap precomparison": (
                "contract_publisher",
                "            bootstrap_report = _bootstrap_for_manifest_author(\n",
                "            bootstrap_report = bootstrap_from_seed(\n",
                r"manifest author decision order differs",
            ),
            "publisher accepts a final bootstrap report": (
                "contract_publisher",
                '            != "pending-fixed-point-author"\n',
                '            != "pass"\n',
                r"manifest author decision order differs",
            ),
            "fixed-point summary precedes the Cupid author": (
                "contract_publisher",
                "        authored_report = _checked_manifest_author_bytes(\n",
                '        report["tool_fixed_point"] = '
                "_tool_fixed_point_record()\n"
                "        authored_report = "
                "_checked_manifest_author_bytes(\n",
                r"manifest author decision order differs",
            ),
            "stage evidence follows a path before opening it": (
                "contract_publisher",
                "        path_status = path.lstat()\n",
                "        path_status = path.stat()\n",
                r"manifest author decision order differs",
            ),
            "stage evidence loses no-follow open policy": (
                "contract_publisher",
                '    flags |= getattr(os, "O_NOFOLLOW", 0)\n',
                '    flags |= getattr(os, "O_CLOEXEC", 0)\n',
                r"manifest author decision order differs",
            ),
            "Windows publication header redirects atomic replacement": (
                "windows_publication_header",
                "#define MoveFileExA cupid_windows_move_file_ex\n",
                "#define MoveFileExA cupid_windows_delete_file\n",
                r"Windows publication contract differs",
            ),
            "Windows publication header changes direct-output creation": (
                "windows_publication_header",
                "#define CREATE_ALWAYS 2u\n",
                "#define CREATE_ALWAYS 3u\n",
                r"Windows publication contract differs",
            ),
            "Windows publication runtime skips the size query": (
                "windows_publication_runtime",
                "GetFullPathNameA(path, 0u, (char *)0, (char **)0);",
                "GetFullPathNameA(path, 1u, (char *)0, (char **)0);",
                r"Windows publication contract differs",
            ),
            "Windows publication bridge calls the wrong API": (
                "windows_publication_start",
                "call dword [__imp_FlushFileBuffers]",
                "call dword [__imp_DeleteFileA]",
                r"Windows publication contract differs",
            ),
            "Windows runtime contract skips allocator coverage": (
                "windows_runtime_contract",
                "  result = allocator_contract();\n",
                "  result = 0;\n",
                r"Windows runtime contract differs",
            ),
            "Windows runtime contract skips directory coverage": (
                "windows_runtime_contract",
                "    result = directory_contract();\n",
                "    result = 0;\n",
                r"Windows runtime contract differs",
            ),
            "Windows runtime contract hides coverage under if zero": (
                "windows_runtime_contract",
                "  result = allocator_contract();\n"
                "  if (result == 0) {\n"
                "    result = file_contract(argv[5], argv[6]);\n"
                "  }\n"
                "  if (result == 0) {\n"
                "    result = directory_contract();\n"
                "  }\n",
                "  if (0) {\n"
                "    result = allocator_contract();\n"
                "    if (result == 0) {\n"
                "      result = file_contract(argv[5], argv[6]);\n"
                "    }\n"
                "    if (result == 0) {\n"
                "      result = directory_contract();\n"
                "    }\n"
                "  }\n"
                "  result = file_contract(argv[5], argv[6]);\n",
                r"Windows runtime contract differs",
            ),
            "Windows allocator returns before its assertions": (
                "windows_runtime_contract",
                "static int allocator_contract(void) {\n  unsigned char *allocation;\n",
                "static int allocator_contract(void) {\n"
                "  return 0;\n"
                "  unsigned char *allocation;\n",
                r"Windows runtime contract differs",
            ),
            "Windows runtime main returns before its contract": (
                "windows_runtime_contract",
                "  int index;\n  if (argc != 7",
                "  int index;\n  return 0;\n  if (argc != 7",
                r"Windows runtime contract differs",
            ),
            "Windows allocator assertions move under if zero": (
                "windows_runtime_contract",
                allocator_contract_feature,
                hide_contract_body(allocator_contract_feature),
                r"Windows runtime contract differs",
            ),
            "Windows file assertions move under if zero": (
                "windows_runtime_contract",
                file_contract_feature,
                hide_contract_body(file_contract_feature),
                r"Windows runtime contract differs",
            ),
            "Windows directory assertions move under if zero": (
                "windows_runtime_contract",
                directory_contract_feature,
                hide_contract_body(directory_contract_feature),
                r"Windows runtime contract differs",
            ),
            "PE32 public enum is only a near match": (
                "linker_header",
                "  CTOOL_LD_IMAGE_PE32_FIXED\n} ctool_ld_image_kind_t;\n",
                "  CTOOL_LD_IMAGE_PE32_FIXEDS\n} ctool_ld_image_kind_t;\n",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 request member is only a near match": (
                "linker_header",
                "  ctool_ld_image_kind_t image_kind;\n",
                "  ctool_ld_image_kind_t image_kinds;\n",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 import request member is only a near match": (
                "linker_header",
                "  const ctool_ld_pe32_import_t *pe32_imports;\n",
                "  const ctool_ld_pe32_import_t *pe32_import_records;\n",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 CLI import selector is only a near match": (
                "linker_cli",
                "    taken = cupidld_take_value(argc, argv, &index, "
                'argument, "--import",\n',
                "    taken = cupidld_take_value(argc, argv, &index, "
                'argument, "--imports",\n',
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 CLI drops import request threading": (
                "linker_cli",
                "  request.pe32_import_count = cli.import_count;\n",
                "  request.pe32_import_count = 0u;\n",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 CLI selector is only a near match": (
                "linker_cli",
                '      (strcmp(cli->machine, "elf_i386") != 0 &&\n'
                '       strcmp(cli->machine, "i386pe") != 0) ||\n',
                '      (strcmp(cli->machine, "elf_i386") != 0 &&\n'
                '       strcmp(cli->machine, "i386pex") != 0) ||\n',
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 CLI dispatch survives only as a comment": (
                "linker_cli",
                '  request.image_kind = strcmp(cli.machine, "i386pe") == 0\n'
                "                           ? CTOOL_LD_IMAGE_PE32_FIXED\n"
                "                           : CTOOL_LD_IMAGE_ELF32;\n",
                "  /* request.image_kind = "
                'strcmp(cli.machine, "i386pe") == 0\n'
                "                           ? CTOOL_LD_IMAGE_PE32_FIXED\n"
                "                           : CTOOL_LD_IMAGE_ELF32; */\n"
                "  request.image_kind = CTOOL_LD_IMAGE_ELF32;\n",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 CLI bypasses atomic publication": (
                "linker_cli",
                "        : cupidld_publish_output("
                "native_paths[output_native_index],\n"
                "                                   ctool_buffer_view(output));\n",
                "        : ctool_job_write(job, &output_path,\n"
                "                          ctool_buffer_view(output));\n",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 CLI defaults to caller-owned output": (
                "linker_cli",
                "status = cli.caller_owned_output == CTOOL_TRUE\n",
                "status = cli.caller_owned_output == CTOOL_FALSE\n",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 CLI accepts duplicate caller-owned selectors": (
                "linker_cli",
                "      if (cli->caller_owned_output == CTOOL_TRUE) {\n",
                "      if (0) {\n",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 caller-owned output loses candidate sharing": (
                "linker_cli",
                "      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,\n",
                "      FILE_SHARE_READ,\n",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 publication replaces without verification": (
                "linker_cli",
                publication_verify_guard,
                "",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 publication verifies after replacement": (
                "linker_cli",
                publication_verify_guard + publication_replace_guard,
                publication_replace_guard + publication_verify_guard,
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 publication clears verification failure": (
                "linker_cli",
                publication_verify_guard + publication_replace_guard,
                publication_verify_guard
                + "  status = CTOOL_OK;\n"
                + publication_replace_guard,
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 publisher returns before verification": (
                "linker_cli",
                publication_verify_guard,
                "  return status;\n" + publication_verify_guard,
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 publisher directly replaces before verification": (
                "linker_cli",
                publication_verify_guard,
                "  return cupidld_publication_replace("
                "candidate, destination);\n" + publication_verify_guard,
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 publication verifier accepts every candidate": (
                "linker_cli",
                verifier_feature,
                "static ctool_status_t cupidld_publication_verify(\n"
                "    const char *candidate, ctool_bytes_t contents) {\n"
                "  (void)candidate;\n"
                "  (void)contents;\n"
                "  return CTOOL_OK;\n"
                "}\n",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 publication verifier clears every failure": (
                "linker_cli",
                verifier_close_tail,
                verifier_close_tail.replace(
                    "  return status;\n",
                    "  status = CTOOL_OK;\n  return status;\n",
                ),
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 publication wrapper calls replace directly": (
                "linker_cli",
                "  return cupidld_publish_output_with_ops("
                "destination, contents, &ops);\n",
                "  return cupidld_publication_replace(destination, destination);\n",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 pre-count includes empty sections": (
                "linker_core",
                "    if (link->outputs[index].size != 0u) {\n"
                "      emitted_section_count++;\n"
                "    }\n",
                "    if (link->outputs[index].size == 0u) {\n"
                "      emitted_section_count++;\n"
                "    }\n",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 pre-count is overwritten before use": (
                "linker_core",
                "    }\n"
                "  }\n"
                "  if (emitted_section_count == 0u || "
                "emitted_section_count > 5u) {\n",
                "    }\n"
                "  }\n"
                "  emitted_section_count = link->output_count;\n"
                "  if (emitted_section_count == 0u || "
                "emitted_section_count > 5u) {\n",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 header extent includes empty sections": (
                "linker_core",
                "  headers_end = LD_PE_DOS_HEADER_SIZE + LD_PE_SIGNATURE_SIZE +\n"
                "                LD_PE_COFF_HEADER_SIZE + "
                "LD_PE_OPTIONAL_HEADER_SIZE +\n"
                "                emitted_section_count * "
                "LD_PE_SECTION_HEADER_SIZE;\n",
                "  headers_end = LD_PE_DOS_HEADER_SIZE + LD_PE_SIGNATURE_SIZE +\n"
                "                LD_PE_COFF_HEADER_SIZE + "
                "LD_PE_OPTIONAL_HEADER_SIZE +\n"
                "                link->output_count * "
                "LD_PE_SECTION_HEADER_SIZE;\n",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 empty section accepts file bytes": (
                "linker_core",
                "    if (section->size == 0u) {\n"
                "      if (section->file_size != 0u) {\n",
                "    if (section->size == 0u) {\n"
                "      if (section->file_size == 0u) {\n",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 empty layout section is not skipped": (
                "linker_core",
                "      section->file_offset = 0u;\n"
                "      continue;\n"
                "    }\n"
                "    if ((section->flags & CTOOL_ELF32_SHF_EXECINSTR) != 0u",
                "      section->file_offset = 0u;\n"
                "    }\n"
                "    if ((section->flags & CTOOL_ELF32_SHF_EXECINSTR) != 0u",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 emitted overlap check is disabled": (
                "linker_core",
                "    if (have_previous_section == CTOOL_TRUE &&\n"
                "        section->address < previous_section_end) {\n",
                "    if (have_previous_section == CTOOL_FALSE &&\n"
                "        section->address < previous_section_end) {\n",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 COFF count includes empty sections": (
                "linker_core",
                "    status = ctool_buffer_put_le16(output,\n"
                "                                   (ctool_u16)"
                "emitted_section_count);\n",
                "    status = ctool_buffer_put_le16(output,\n"
                "                                   (ctool_u16)"
                "link->output_count);\n",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 empty section reaches header emission": (
                "linker_core",
                "    if (section->size == 0u) {\n"
                "      continue;\n"
                "    }\n"
                "    if (section->type == "
                "(ctool_u32)CTOOL_ELF32_SHT_PROGBITS) {\n",
                "    if (section->size == 0u) {\n"
                "      section->file_offset = 0u;\n"
                "    }\n"
                "    if (section->type == "
                "(ctool_u32)CTOOL_ELF32_SHT_PROGBITS) {\n",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 result reports all output sections": (
                "linker_core",
                "    result_out->output_section_count = emitted_section_count;\n",
                "    result_out->output_section_count = link->output_count;\n",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 core serializer is removed wholesale": (
                "linker_core",
                serializer_feature,
                "",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 core serializer survives only under if zero": (
                "linker_core",
                serializer_feature,
                "#if 0\n" + serializer_feature + "\n#endif",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 core dispatch is unreachable": (
                "linker_core",
                "    if (request->image_kind == CTOOL_LD_IMAGE_PE32_FIXED) {\n",
                "    if (CTOOL_FALSE &&\n"
                "        request->image_kind == CTOOL_LD_IMAGE_PE32_FIXED) {\n",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 image loses its two-GiB RVA guard": (
                "linker_core",
                "  if (image_size > LD_PE_NAME_RVA_LIMIT) {\n",
                "  if (image_size == 0u) {\n",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 import construction is skipped": (
                "linker_core",
                "    status = ld_prepare_pe32_imports(&link);\n",
                "    status = CTOOL_OK;\n",
                r"fixed-point PE32 import contract differs",
            ),
            "PE32 name-RVA ceiling is widened": (
                "linker_core",
                "#define LD_PE_NAME_RVA_LIMIT 0x80000000u\n",
                "#define LD_PE_NAME_RVA_LIMIT 0xffffffffu\n",
                r"fixed-point PE32 import contract differs",
            ),
            "PE32 import construction skips canonical sorting": (
                "linker_core",
                "  ld_pe32_import_sort(link->pe32_imports, import_count);\n",
                "  (void)import_count;\n",
                r"fixed-point PE32 import contract differs",
            ),
            "PE32 import construction disables duplicate selection guard": (
                "linker_core",
                "    if (global->import_selected == CTOOL_TRUE) {\n",
                "    if (CTOOL_FALSE) {\n",
                r"fixed-point PE32 import contract differs",
            ),
            "PE32 import construction stops recording selected symbols": (
                "linker_core",
                "    global->import_selected = CTOOL_TRUE;\n",
                "    global->import_selected = CTOOL_FALSE;\n",
                r"fixed-point PE32 import contract differs",
            ),
            "PE32 import table may start at the name-RVA ceiling": (
                "linker_core",
                "      address - LD_PE_IMAGE_BASE >= LD_PE_NAME_RVA_LIMIT ||\n",
                "      address - LD_PE_IMAGE_BASE > LD_PE_NAME_RVA_LIMIT ||\n",
                r"fixed-point PE32 import contract differs",
            ),
            "PE32 import payload may cross the name-RVA ceiling": (
                "linker_core",
                "      import_payload_size >\n"
                "          LD_PE_NAME_RVA_LIMIT - "
                "(address - LD_PE_IMAGE_BASE)) {\n",
                "      import_payload_size ==\n"
                "          LD_PE_NAME_RVA_LIMIT - "
                "(address - LD_PE_IMAGE_BASE)) {\n",
                r"fixed-point PE32 import contract differs",
            ),
            "PE32 name thunk may set the ordinal flag": (
                "linker_core",
                "      if (hint_rva >= LD_PE_NAME_RVA_LIMIT) {\n",
                "      if (hint_rva > LD_PE_NAME_RVA_LIMIT) {\n",
                r"fixed-point PE32 import contract differs",
            ),
            "PE32 import section loses write permission": (
                "linker_core",
                "  section->flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE;\n",
                "  section->flags = CTOOL_ELF32_SHF_ALLOC;\n",
                r"fixed-point PE32 import contract differs",
            ),
            "PE32 IAT permits a nonzero addend": (
                "linker_core",
                "               relocation->addend != 0)) {\n",
                "               relocation->addend == 0)) {\n",
                r"fixed-point PE32 import contract differs",
            ),
            "PE32 IAT directory aliases the import directory": (
                "linker_core",
                "    } else if (directory == LD_PE_IAT_DIRECTORY) {\n",
                "    } else if (directory == LD_PE_IMPORT_DIRECTORY) {\n",
                r"fixed-point PE32 import contract differs",
            ),
            "PE32 core dispatch survives only in a dead block": (
                "linker_core",
                core_dispatch_feature,
                "  if (CTOOL_FALSE) {\n"
                + textwrap.indent(core_dispatch_feature, "  ")
                + "\n  }",
                r"fixed-point PE32 source contract differs",
            ),
        }
        for name, (target_name, old, new, message) in mutations.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                driver_target = root / "toolchain" / "cupidc_main.cc"
                test_target = (
                    root / "tests" / "test_toolchain_cupidc_object.py"
                )
                bootstrap_target = root / "tools" / "bootstrap_toolchain.py"
                contract_publisher_target = (
                    root / "tools" / "cupidc_toolchain_contracts.py"
                )
                windows_runtime_contract_target = (
                    root
                    / "toolchain"
                    / "tests"
                    / "hosted_i386_windows_runtime_contract.cc"
                )
                windows_publication_header_target = (
                    root
                    / "toolchain"
                    / "hosted"
                    / "i386-linux"
                    / "include"
                    / "windows.h"
                )
                windows_publication_runtime_target = (
                    root
                    / "toolchain"
                    / "hosted"
                    / "i386-windows"
                    / "publication_runtime.cc"
                )
                windows_publication_start_target = (
                    root
                    / "toolchain"
                    / "hosted"
                    / "i386-windows"
                    / "publication_start.asm"
                )
                linker_header_target = root / "toolchain" / "cupidld.h"
                linker_cli_target = root / "toolchain" / "cupidld_main.cc"
                linker_core_target = root / "toolchain" / "cupidld.cc"
                driver_target.parent.mkdir(parents=True)
                test_target.parent.mkdir(parents=True)
                bootstrap_target.parent.mkdir(parents=True)
                shutil.copyfile(REPO_ROOT / "tools/bootstrap_user_abi.py",
                                root / "tools/bootstrap_user_abi.py")
                driver_payload = driver
                test_payload = test
                bootstrap_payload = bootstrap
                contract_publisher_payload = contract_publisher
                windows_runtime_contract_payload = windows_runtime_contract
                windows_publication_header_payload = windows_publication_header
                windows_publication_runtime_payload = (
                    windows_publication_runtime
                )
                windows_publication_start_payload = windows_publication_start
                linker_header_payload = linker_header
                linker_cli_payload = linker_cli
                linker_core_payload = linker_core
                if target_name == "driver":
                    driver_payload = replace_c_fragment(driver_payload, old, new)
                    self.assertNotEqual(driver_payload, driver)
                elif target_name == "test":
                    test_payload = test_payload.replace(old, new, 1)
                    self.assertNotEqual(test_payload, test)
                elif target_name == "bootstrap":
                    bootstrap_payload = bootstrap_payload.replace(old, new, 1)
                    self.assertNotEqual(bootstrap_payload, bootstrap)
                elif target_name == "contract_publisher":
                    contract_publisher_payload = (
                        contract_publisher_payload.replace(old, new, 1)
                    )
                    self.assertNotEqual(
                        contract_publisher_payload, contract_publisher
                    )
                elif target_name == "windows_runtime_contract":
                    windows_runtime_contract_payload = (
                        replace_c_fragment(windows_runtime_contract_payload, old, new)
                    )
                    self.assertNotEqual(
                        windows_runtime_contract_payload,
                        windows_runtime_contract,
                    )
                elif target_name == "windows_publication_header":
                    windows_publication_header_payload = (
                        windows_publication_header_payload.replace(old, new, 1)
                    )
                    self.assertNotEqual(
                        windows_publication_header_payload,
                        windows_publication_header,
                    )
                elif target_name == "windows_publication_runtime":
                    windows_publication_runtime_payload = (
                        replace_c_fragment(windows_publication_runtime_payload, old, new)
                    )
                    self.assertNotEqual(
                        windows_publication_runtime_payload,
                        windows_publication_runtime,
                    )
                elif target_name == "windows_publication_start":
                    windows_publication_start_payload = (
                        windows_publication_start_payload.replace(old, new, 1)
                    )
                    self.assertNotEqual(
                        windows_publication_start_payload,
                        windows_publication_start,
                    )
                elif target_name == "linker_header":
                    linker_header_payload = linker_header_payload.replace(
                        old, new, 1
                    )
                    self.assertNotEqual(linker_header_payload, linker_header)
                elif target_name == "linker_cli":
                    linker_cli_payload = replace_c_fragment(linker_cli_payload, old, new)
                    self.assertNotEqual(linker_cli_payload, linker_cli)
                else:
                    self.assertEqual(target_name, "linker_core")
                    linker_core_payload = replace_c_fragment(linker_core_payload, old, new)
                    self.assertNotEqual(linker_core_payload, linker_core)
                driver_target.write_text(driver_payload, encoding="utf-8")
                test_target.write_text(test_payload, encoding="utf-8")
                bootstrap_target.write_text(
                    bootstrap_payload, encoding="utf-8"
                )
                contract_publisher_target.write_text(
                    contract_publisher_payload, encoding="utf-8"
                )
                windows_runtime_contract_target.parent.mkdir(
                    parents=True, exist_ok=True
                )
                windows_runtime_contract_target.write_text(
                    windows_runtime_contract_payload, encoding="utf-8"
                )
                windows_publication_header_target.parent.mkdir(
                    parents=True, exist_ok=True
                )
                windows_publication_header_target.write_text(
                    windows_publication_header_payload, encoding="utf-8"
                )
                windows_publication_runtime_target.parent.mkdir(
                    parents=True, exist_ok=True
                )
                windows_publication_runtime_target.write_text(
                    windows_publication_runtime_payload, encoding="utf-8"
                )
                windows_publication_start_target.write_text(
                    windows_publication_start_payload, encoding="utf-8"
                )
                linker_header_target.write_text(
                    linker_header_payload, encoding="utf-8"
                )
                linker_cli_target.write_text(
                    linker_cli_payload, encoding="utf-8"
                )
                linker_core_target.write_text(
                    linker_core_payload, encoding="utf-8"
                )
                with self.assertRaisesRegex(module.AuditError, message):
                    module._cupid_toolchain_fixed_point_contract(root)
