## @file
#  @brief Check fixed-point coverage of published tool capabilities.
import ast
import struct

from tools.bootstrap_toolchain import (
    _code_anchor_executable_payload,
    _local_target_executable_payload,
    _unowned_relocation_object_payload,
)
from tools.cupidc_kernel_compile import validate_i386_relocatable_bytes

from tests.bootstrap_seed_suite_fixtures import (
    BOOTSTRAP_TOOL,
)


class BootstrapSeedMatrixCases:
    def test_fixed_point_matrix_keeps_strict_cupiddis_checks(self):
        tree = ast.parse(BOOTSTRAP_TOOL.read_text(encoding="utf-8"))
        behavior_functions = [
            node
            for node in tree.body
            if isinstance(node, ast.FunctionDef)
            and node.name == "_run_behavior_checks"
        ]
        self.assertEqual(len(behavior_functions), 1)
        behavior = behavior_functions[0]

        assigned_calls = {}
        for statement in behavior.body:
            if (
                isinstance(statement, ast.Assign)
                and len(statement.targets) == 1
                and isinstance(statement.targets[0], ast.Name)
                and isinstance(statement.value, ast.Call)
            ):
                assigned_calls[statement.targets[0].id] = statement.value

        def stage_pair(name):
            call = assigned_calls[name]
            self.assertIsInstance(call.func, ast.Name)
            self.assertEqual(call.func.id, "_run_stage_pair")
            self.assertEqual(ast.literal_eval(call.args[3]), "cupiddis")
            return call

        def matrix_arguments(argument):
            self.assertIsInstance(argument, ast.List)
            rendered = []
            for item in argument.elts:
                if isinstance(item, ast.Constant):
                    rendered.append(item.value)
                elif isinstance(item, ast.Name):
                    rendered.append(f"<{item.id}>")
                else:
                    self.fail(
                        "strict CupidDis arguments contain an unexpected "
                        f"expression: {ast.dump(item)}"
                    )
            return rendered

        clean = stage_pair("strict_disassembly_result")
        self.assertEqual(
            matrix_arguments(clean.args[4]),
            ["--require-known", "<stage_two_valid>"],
        )
        self.assertEqual(
            matrix_arguments(clean.args[5]),
            ["--require-known", "<stage_three_valid>"],
        )

        truncated = stage_pair("strict_failure_result")
        self.assertEqual(len(truncated.args), 5)
        self.assertEqual(
            matrix_arguments(truncated.args[4]),
            [
                "--require-known",
                "--raw",
                "--mode=32",
                "--base=0",
                "<truncated_code>",
            ],
        )

        local_target = stage_pair("local_target_result")
        self.assertIn(
            "local_target_arguments", ast.unparse(local_target)
        )
        self.assertIn("'--require-local-targets'", ast.unparse(behavior))
        self.assertIn("stage_two_binary", ast.unparse(local_target.args[4]))
        self.assertIn("stage_three_binary", ast.unparse(local_target.args[5]))

        invalid_target = stage_pair("invalid_target_result")
        self.assertEqual(len(invalid_target.args), 5)
        self.assertIn(
            "local_target_arguments", ast.unparse(invalid_target.args[4])
        )
        self.assertIn(
            "invalid_target_binary", ast.unparse(invalid_target.args[4])
        )

        statuses = {}
        for node in ast.walk(behavior):
            if (
                isinstance(node, ast.Call)
                and isinstance(node.func, ast.Name)
                and node.func.id == "_expect_status"
                and len(node.args) >= 2
                and isinstance(node.args[0], ast.Name)
                and isinstance(node.args[1], ast.Constant)
            ):
                statuses.setdefault(node.args[0].id, []).append(
                    node.args[1].value
                )
        self.assertEqual(statuses["strict_disassembly_result"], [0])
        self.assertEqual(statuses["strict_failure_result"], [1])
        self.assertEqual(statuses["local_target_result"], [0])
        self.assertEqual(statuses["invalid_target_result"], [1])

        expected_fixture = ast.dump(
            ast.parse("bytes([0x0F])", mode="eval").body,
            include_attributes=False,
        )
        fixture_payloads = [
            ast.dump(node.args[0], include_attributes=False)
            for node in ast.walk(behavior)
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Attribute)
            and node.func.attr == "write_bytes"
            and isinstance(node.func.value, ast.Name)
            and node.func.value.id == "truncated_code"
            and len(node.args) == 1
        ]
        self.assertEqual(fixture_payloads, [expected_fixture])

        messages = {
            node.value
            for node in ast.walk(behavior)
            if isinstance(node, ast.Constant)
            and isinstance(node.value, str)
        }
        self.assertIn(
            "truncated-code.bin: code check failed: "
            "0 known, 0 unknown, 0 invalid, 1 truncated",
            messages,
        )

        returns = [
            node
            for node in behavior.body
            if isinstance(node, ast.Return)
        ]
        self.assertEqual(len(returns), 1)
        return_value = returns[0].value
        self.assertIsInstance(return_value, ast.Dict)
        returned = {
            key.value: value
            for key, value in zip(return_value.keys, return_value.values)
        }
        self.assertEqual(
            returned["failure_cases"].value,
            66,
        )
        self.assertEqual(returned["success_cases"].value, 73)
        self.assertIsInstance(returned["help_cases"], ast.BinOp)
        self.assertIsInstance(returned["help_cases"].op, ast.Add)
        self.assertEqual(returned["help_cases"].right.value, 1)
        self.assertEqual(returned["help_cases"].left.func.id, "len")
        self.assertEqual(
            returned["help_cases"].left.args[0].id,
            "tool_names",
        )

        self.assertIn(
            "1 of 1 direct relative targets invalid",
            messages,
        )
        self.assertIn("1 outside image", messages)

    def test_fixed_point_cupidbuild_checks_cupidc_runner_behavior(self):
        tree = ast.parse(BOOTSTRAP_TOOL.read_text(encoding="utf-8"))

        def function(name):
            return next(
                node
                for node in tree.body
                if isinstance(node, ast.FunctionDef) and node.name == name
            )

        behavior = function("_check_cupidbuild_cupidc_runner_behavior")
        rendered = ast.unparse(behavior)
        for expected in (
            "'--tool', 'cupidc', '--'",
            "'--help'",
            "'runner-valid.cc'",
            "'/runner-valid.cc'",
            "'/runner-invalid.cc:1:'",
            "b'preserved CupidBuild checked CupidC output\\n'",
            "stage_two_output.read_bytes() != "
            "stage_three_output.read_bytes()",
            "stage_two_failure.read_bytes() != sentinel",
            "stage_three_failure.read_bytes() != sentinel",
            "_validate_i386_relocatable(stage_two_output)",
        ):
            self.assertIn(expected, rendered)

        stage_pair_calls = [
            node
            for node in ast.walk(behavior)
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Name)
            and node.func.id == "_run_stage_pair"
        ]
        self.assertEqual(len(stage_pair_calls), 3)
        statuses = {
            node.args[0].id: node.args[1].value
            for node in ast.walk(behavior)
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Name)
            and node.func.id == "_expect_status"
        }
        self.assertEqual(
            statuses,
            {"help_result": 0, "success_result": 0, "failure_result": 1},
        )

        for matrix_name in (
            "_run_behavior_checks",
            "_run_native_windows_behavior_checks",
        ):
            matrix = function(matrix_name)
            calls = [
                node
                for node in ast.walk(matrix)
                if isinstance(node, ast.Call)
                and isinstance(node.func, ast.Name)
                and node.func.id
                == "_check_cupidbuild_cupidc_runner_behavior"
            ]
            self.assertEqual(len(calls), 1)

    def test_fixed_point_matrix_checks_executable_relocation_ownership(self):
        tree = ast.parse(BOOTSTRAP_TOOL.read_text(encoding="utf-8"))

        def behavior_function(name):
            return next(
                node
                for node in tree.body
                if isinstance(node, ast.FunctionDef) and node.name == name
            )

        def assignment(function, name):
            return next(
                statement.value
                for statement in function.body
                if isinstance(statement, ast.Assign)
                and len(statement.targets) == 1
                and isinstance(statement.targets[0], ast.Name)
                and statement.targets[0].id == name
            )

        def assert_relocation_check(function):
            call = assignment(function, "relocation_ownership_result")
            self.assertIsInstance(call, ast.Call)
            self.assertIsInstance(call.func, ast.Name)
            self.assertEqual(call.func.id, "_run_stage_pair")
            self.assertEqual(ast.literal_eval(call.args[3]), "cupiddis")
            for arguments in call.args[4:]:
                self.assertEqual(
                    [
                        item.value
                        if isinstance(item, ast.Constant)
                        else f"<{item.id}>"
                        for item in arguments.elts
                    ],
                    ["--require-known", "<unowned_relocation>"],
                )

            statuses = [
                node.args[1].value
                for node in ast.walk(function)
                if isinstance(node, ast.Call)
                and isinstance(node.func, ast.Name)
                and node.func.id == "_expect_status"
                and isinstance(node.args[0], ast.Name)
                and node.args[0].id == "relocation_ownership_result"
            ]
            self.assertEqual(statuses, [1])
            self.assertIn(
                "1 of 1 executable relocations unmatched",
                {
                    node.value
                    for node in ast.walk(function)
                    if isinstance(node, ast.Constant)
                    and isinstance(node.value, str)
                },
            )

        assert_relocation_check(behavior_function("_run_behavior_checks"))
        assert_relocation_check(
            behavior_function("_run_native_windows_behavior_checks")
        )

    def test_fixed_point_cupidbuild_checks_every_guarded_assembly_operation(
        self,
    ):
        tree = ast.parse(BOOTSTRAP_TOOL.read_text(encoding="utf-8"))
        behavior = next(
            node
            for node in tree.body
            if isinstance(node, ast.FunctionDef)
            and node.name == "_check_cupidbuild_guarded_object_behavior"
        )
        rendered = ast.unparse(behavior)

        for operation in (
            "assemble-cupidasm-object",
            "assemble-bootloader",
            "assemble-smp-trampoline",
            "assemble-iso-pattern",
        ):
            self.assertIn(repr(operation), rendered)
        self.assertIn("('guarded-bootloader.asm', 2560)", rendered)
        self.assertIn("('guarded-smp-trampoline.S', 4096)", rendered)
        self.assertIn("('guarded-iso-pattern.asm', 4096)", rendered)
        self.assertIn("malformed-bootloader.asm", rendered)
        self.assertIn("malformed-iso-pattern.asm", rendered)
        self.assertIn("bytes(range(256)) * 16", rendered)
        self.assertIn("ISO pattern differs", rendered)
        self.assertIn("preserved CupidBuild raw output", rendered)

    def test_fixed_point_cupidbuild_checks_typed_jpeg_transaction(self):
        tree = ast.parse(BOOTSTRAP_TOOL.read_text(encoding="utf-8"))

        def function(name):
            return next(
                node
                for node in tree.body
                if isinstance(node, ast.FunctionDef) and node.name == name
            )

        behavior = function("_check_cupidbuild_embed_jpeg_behavior")
        rendered = ast.unparse(behavior)
        for expected in (
            "'embed-jpeg'",
            "'asset.jpg'",
            "'progressive.jpg'",
            "'checked CupidObj failed'",
            "b'preserved CupidBuild JPEG output\\n'",
            "stage_two_output.read_bytes() != "
            "stage_three_output.read_bytes()",
            "stage_two_failure.read_bytes() != sentinel",
            "stage_three_failure.read_bytes() != sentinel",
        ):
            self.assertIn(expected, rendered)

        for matrix_name in (
            "_run_behavior_checks",
            "_run_native_windows_behavior_checks",
        ):
            matrix = function(matrix_name)
            calls = [
                node
                for node in ast.walk(matrix)
                if isinstance(node, ast.Call)
                and isinstance(node.func, ast.Name)
                and node.func.id
                == "_check_cupidbuild_embed_jpeg_behavior"
            ]
            self.assertEqual(len(calls), 1)

    def test_fixed_point_cupidbuild_checks_typed_kernel_symbol_transaction(
        self,
    ):
        tree = ast.parse(BOOTSTRAP_TOOL.read_text(encoding="utf-8"))

        def function(name):
            return next(
                node
                for node in tree.body
                if isinstance(node, ast.FunctionDef) and node.name == name
            )

        behavior = function("_check_cupidbuild_generate_ksyms_behavior")
        rendered = ast.unparse(behavior)
        for expected in (
            "'generate-ksyms'",
            "'kernel.elf.pass1'",
            "'malformed.elf'",
            "'checked CupidDis failed'",
            "b'preserved CupidBuild kernel symbol source\\n'",
            "stage_two_output.read_bytes() != "
            "stage_three_output.read_bytes()",
            "stage_two_failure.read_bytes() != sentinel",
            "stage_three_failure.read_bytes() != sentinel",
        ):
            self.assertIn(expected, rendered)

        for matrix_name in (
            "_run_behavior_checks",
            "_run_native_windows_behavior_checks",
        ):
            matrix = function(matrix_name)
            calls = [
                node
                for node in ast.walk(matrix)
                if isinstance(node, ast.Call)
                and isinstance(node.func, ast.Name)
                and node.func.id
                == "_check_cupidbuild_generate_ksyms_behavior"
            ]
            self.assertEqual(len(calls), 1)

    def test_fixed_point_cupidbuild_checks_typed_kernel_flatten_transaction(
        self,
    ):
        tree = ast.parse(BOOTSTRAP_TOOL.read_text(encoding="utf-8"))

        def function(name):
            return next(
                node
                for node in tree.body
                if isinstance(node, ast.FunctionDef) and node.name == name
            )

        behavior = function("_check_cupidbuild_flatten_kernel_behavior")
        rendered = ast.unparse(behavior)
        for expected in (
            "flatten_root.mkdir(parents=True)",
            "kernel_root = source_root / 'kernel'",
            "'flatten-kernel'",
            "'kernel.elf.pass1'",
            "'kernel.elf'",
            "'code-inputs.txt'",
            "range(498)",
            "code_members[-1].write_bytes(b'invalid final ELF input\\n')",
            "'could not be started' in final_input_result.stderr",
            "stage_two_output.stat().st_mtime_ns",
            "stage_three_output.stat().st_mtime_ns",
            "'malformed-code-inputs.txt'",
            "'must end with a newline'",
            "b'preserved CupidBuild flat kernel\\n'",
            "stage_two_output.read_bytes() != "
            "stage_three_output.read_bytes()",
            "stage_two_failure.read_bytes() != sentinel",
            "stage_three_failure.read_bytes() != sentinel",
        ):
            self.assertIn(expected, rendered)

        for matrix_name in (
            "_run_behavior_checks",
            "_run_native_windows_behavior_checks",
        ):
            matrix = function(matrix_name)
            calls = [
                node
                for node in ast.walk(matrix)
                if isinstance(node, ast.Call)
                and isinstance(node.func, ast.Name)
                and node.func.id
                == "_check_cupidbuild_flatten_kernel_behavior"
            ]
            self.assertEqual(len(calls), 1)

    def test_fixed_point_cupidbuild_checks_typed_profile_transaction(self):
        tree = ast.parse(BOOTSTRAP_TOOL.read_text(encoding="utf-8"))

        def function(name):
            return next(
                node
                for node in tree.body
                if isinstance(node, ast.FunctionDef) and node.name == name
            )

        behavior = function("_check_cupidbuild_generate_profile_behavior")
        rendered = ast.unparse(behavior)
        for expected in (
            "'generate-profile-manifest'",
            "Path('build/bootstrap/doom-cupidc-inputs.json')",
            "'cupidbuild-profile-stage-three-success'",
            "'cupidbuild-profile-stage-four-success'",
            "'cupidbuild-profile-stage-three-failure'",
            "'cupidbuild-profile-stage-four-failure'",
            "'cupidbuild-profile-stage-three-missing-parent'",
            "'cupidbuild-profile-stage-four-missing-parent'",
            "'cupid.doom-profile-inputs.v1'",
            "'unlisted.cc'",
            "'approved source cohort'",
            "'profile parent build component must already exist on POSIX'",
            "stage_two_output.read_bytes() != stage_three_output.read_bytes()",
            "stage_two_failure.exists()",
            "stage_three_failure.exists()",
            "(stage_two_failure_root / 'build').exists()",
            "(stage_three_failure_root / 'build').exists()",
        ):
            self.assertIn(expected, rendered)

        for matrix_name, clean_parent_creation in (
            ("_run_behavior_checks", False),
            ("_run_native_windows_behavior_checks", True),
        ):
            matrix = function(matrix_name)
            calls = [
                node
                for node in ast.walk(matrix)
                if isinstance(node, ast.Call)
                and isinstance(node.func, ast.Name)
                and node.func.id == "_check_cupidbuild_generate_profile_behavior"
            ]
            self.assertEqual(len(calls), 1)
            self.assertIsInstance(calls[0].args[1], ast.Name)
            self.assertEqual(calls[0].args[1].id, "profile_source_root")
            self.assertIsInstance(calls[0].args[-1], ast.Constant)
            self.assertEqual(
                calls[0].args[-1].value, clean_parent_creation
            )

        driver = function("_bootstrap_from_frozen_seed")
        driver_calls = [
            node
            for node in ast.walk(driver)
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Name)
            and node.func.id == "_run_behavior_checks"
        ]
        self.assertEqual(len(driver_calls), 1)
        self.assertIsInstance(driver_calls[0].args[-1], ast.Name)
        self.assertEqual(driver_calls[0].args[-1].id, "source_root")

    def test_fixed_point_relocation_fixture_is_a_valid_i386_object(self):
        payload = _unowned_relocation_object_payload()
        validate_i386_relocatable_bytes(payload)
        self.assertEqual(payload[64:70], bytes.fromhex("b8 00 00 00 00 c3"))

    def test_fixed_point_linked_local_target_fixture_is_a_static_i386_exec(self):
        valid = _local_target_executable_payload(5)
        invalid = _local_target_executable_payload(1)

        self.assertEqual(valid[:7], b"\x7fELF\x01\x01\x01")
        self.assertEqual(struct.unpack_from("<H", valid, 16)[0], 2)
        self.assertEqual(struct.unpack_from("<H", valid, 18)[0], 3)
        self.assertEqual(struct.unpack_from("<I", valid, 24)[0], 0x00400000)
        self.assertEqual(struct.unpack_from("<I", valid, 52)[0], 1)
        self.assertEqual(struct.unpack_from("<I", valid, 76)[0], 5)
        self.assertEqual(valid[84:], bytes.fromhex("eb 05 b8 00 00 00 00 c3"))
        self.assertEqual(invalid[84:], bytes.fromhex("eb 01 b8 00 00 00 00 c3"))

    def test_fixed_point_code_anchor_fixture_is_a_sectioned_i386_exec(self):
        valid = _code_anchor_executable_payload()
        invalid = _code_anchor_executable_payload(entry=0x00400001)

        self.assertEqual(valid[:7], b"\x7fELF\x01\x01\x01")
        self.assertEqual(struct.unpack_from("<H", valid, 16)[0], 2)
        self.assertEqual(struct.unpack_from("<H", valid, 18)[0], 3)
        self.assertEqual(struct.unpack_from("<I", valid, 24)[0], 0x00400000)
        self.assertEqual(struct.unpack_from("<I", invalid, 24)[0], 0x00400001)
        self.assertEqual(struct.unpack_from("<I", valid, 32)[0], 212)
        self.assertEqual(struct.unpack_from("<H", valid, 48)[0], 5)
        self.assertEqual(valid[84:90], bytes.fromhex("b8 78 56 34 12 c3"))
        self.assertEqual(struct.unpack_from("<I", valid, 132)[0], 0x00400000)
        self.assertEqual(struct.unpack_from("<I", valid, 148)[0], 0x00400000)
        self.assertEqual(valid[140], 0x12)
        self.assertEqual(valid[156], 0x12)

    def test_fixed_point_matrices_call_code_anchor_behavior_once(self):
        tree = ast.parse(BOOTSTRAP_TOOL.read_text(encoding="utf-8"))
        helpers = [
            node
            for node in tree.body
            if isinstance(node, ast.FunctionDef)
            and node.name == "_check_executable_code_anchor_behavior"
        ]
        self.assertEqual(len(helpers), 1)
        rendered_helper = ast.unparse(helpers[0])
        self.assertIn("'--require-code-anchors'", rendered_helper)
        self.assertIn("expected_invalid_stderr", rendered_helper)
        self.assertIn(
            "1 of 3 code anchors invalid (0 outside file-backed executable "
            "code, 1 mid-instruction)",
            rendered_helper,
        )

        for function_name in (
            "_run_behavior_checks",
            "_run_native_windows_behavior_checks",
        ):
            function = next(
                node
                for node in tree.body
                if isinstance(node, ast.FunctionDef)
                and node.name == function_name
            )
            calls = [
                node
                for node in ast.walk(function)
                if isinstance(node, ast.Call)
                and isinstance(node.func, ast.Name)
                and node.func.id == "_check_executable_code_anchor_behavior"
            ]
            self.assertEqual(len(calls), 1, function_name)

    def test_linux_fixed_point_rebuilds_windows_cupiddis_main(self):
        tree = ast.parse(BOOTSTRAP_TOOL.read_text(encoding="utf-8"))
        behavior = next(
            node
            for node in tree.body
            if isinstance(node, ast.FunctionDef)
            and node.name == "_run_behavior_checks"
        )
        assignments = {
            node.targets[0].id: node.value
            for node in behavior.body
            if isinstance(node, ast.Assign)
            and len(node.targets) == 1
            and isinstance(node.targets[0], ast.Name)
        }
        compile_call = assignments["windows_cupiddis_main_compile_result"]
        self.assertIsInstance(compile_call, ast.Call)
        self.assertIsInstance(compile_call.func, ast.Name)
        self.assertEqual(compile_call.func.id, "_run_stage_pair")
        self.assertEqual(ast.literal_eval(compile_call.args[3]), "cupidc")
        for command in compile_call.args[4:6]:
            rendered = ast.unparse(command)
            self.assertIn("'_WIN32=1'", rendered)
            self.assertIn("'/toolchain/cupiddis_main.cc'", rendered)

        rendered = ast.unparse(behavior)
        for object_name in (
            "stage_two_windows_cupiddis_main",
            "stage_three_windows_cupiddis_main",
        ):
            self.assertIn(object_name, rendered)
        self.assertNotIn("objects['cupiddis_main']", rendered)
