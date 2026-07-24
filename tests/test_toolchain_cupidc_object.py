import os
import shutil
import subprocess
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"
CUPIDC_FIXED_POINT_SOURCES = (
    ("ctool", "/toolchain/ctool.c", False),
    ("ctool_host", "/toolchain/ctool_host.c", False),
    ("cupidc_pp", "/toolchain/cupidc_pp.c", False),
    ("cupidc_type", "/toolchain/cupidc_type.c", False),
    ("cupidc_frontend", "/toolchain/cupidc_frontend.c", False),
    ("cupidc_ir", "/toolchain/cupidc_ir.c", False),
    ("cupidc_emit", "/toolchain/cupidc_emit.c", False),
    ("elf32", "/toolchain/elf32.c", False),
    ("x86", "/toolchain/x86.c", False),
    ("cupidc_main", "/toolchain/cupidc_main.c", False),
    ("runtime", "/toolchain/hosted/i386-linux/runtime.c", True),
)
CUPIDC_FIXED_POINT_INCLUDE_ARGUMENTS = (
    "-I",
    "/toolchain",
    "--include-angle",
    "/toolchain/hosted/i386-linux/include",
)
CUPIDC_FIXED_POINT_LINK_ORDER = (
    "start",
    "cupidc_main",
    "cupidc_emit",
    "cupidc_ir",
    "cupidc_frontend",
    "cupidc_type",
    "cupidc_pp",
    "ctool_host",
    "ctool",
    "elf32",
    "x86",
    "runtime",
)
CUPID_TOOLCHAIN_FIXED_POINT_SOURCES = (
    ("runtime", "/toolchain/hosted/i386-linux/runtime.c", True),
    ("ctool", "/toolchain/ctool.c", False),
    ("ctool_host", "/toolchain/ctool_host.c", False),
    ("elf32", "/toolchain/elf32.c", False),
    ("x86", "/toolchain/x86.c", False),
    ("cupidasm", "/toolchain/cupidasm.c", False),
    ("cupidasm_main", "/toolchain/cupidasm_main.c", False),
    ("cupiddis", "/toolchain/cupiddis.c", False),
    ("cupiddis_main", "/toolchain/cupiddis_main.c", False),
    ("cupidobj", "/toolchain/cupidobj.c", False),
    ("cupidobj_main", "/toolchain/cupidobj_main.c", False),
    ("cupidld", "/toolchain/cupidld.c", False),
    ("cupidld_main", "/toolchain/cupidld_main.c", False),
    ("cupidc_pp", "/toolchain/cupidc_pp.c", False),
    ("cupidc_type", "/toolchain/cupidc_type.c", False),
    ("cupidc_frontend", "/toolchain/cupidc_frontend.c", False),
    ("cupidc_ir", "/toolchain/cupidc_ir.c", False),
    ("cupidc_emit", "/toolchain/cupidc_emit.c", False),
    ("cupidc_main", "/toolchain/cupidc_main.c", False),
)
CUPID_TOOLCHAIN_FIXED_POINT_LINKS = (
    (
        "cupidasm",
        (
            "start",
            "cupidasm_main",
            "cupidasm",
            "ctool_host",
            "ctool",
            "elf32",
            "x86",
            "runtime",
        ),
    ),
    (
        "cupiddis",
        (
            "start",
            "cupiddis_main",
            "cupiddis",
            "ctool_host",
            "ctool",
            "elf32",
            "x86",
            "runtime",
        ),
    ),
    (
        "cupidld",
        (
            "start",
            "cupidld_main",
            "cupidld",
            "ctool_host",
            "ctool",
            "elf32",
            "runtime",
        ),
    ),
    (
        "cupidobj",
        (
            "start",
            "cupidobj_main",
            "cupidobj",
            "ctool_host",
            "ctool",
            "elf32",
            "runtime",
        ),
    ),
    (
        "cupidc",
        (
            "start",
            "cupidc_main",
            "cupidc_emit",
            "cupidc_ir",
            "cupidc_frontend",
            "cupidc_type",
            "cupidc_pp",
            "ctool_host",
            "ctool",
            "elf32",
            "x86",
            "runtime",
        ),
    ),
)


class ToolchainCupidCObjectContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._build_directory = tempfile.TemporaryDirectory(
            prefix=".cupidc-object-build-", dir=TOOLCHAIN_ROOT
        )
        build_path = Path(cls._build_directory.name)
        relative_build = build_path.relative_to(TOOLCHAIN_ROOT).as_posix()
        suffix = ".exe" if os.name == "nt" else ""
        cls.contract_path = build_path / ("cupidc-object-contract" + suffix)
        cls.hosted_cupidasm_path = build_path / ("cupidasm" + suffix)
        cls.hosted_cupiddis_path = build_path / ("cupiddis" + suffix)
        cls.hosted_cupidld_path = build_path / ("cupidld" + suffix)
        cls.hosted_cupidobj_path = build_path / ("cupidobj" + suffix)
        cls.hosted_cupidc_path = build_path / ("cupidc" + suffix)
        cls.cupid_cupidasm_path = build_path / "cupid-built-cupidasm.elf"
        cls.cupid_cupiddis_path = build_path / "cupid-built-cupiddis.elf"
        cls.cupid_cupidld_path = build_path / "cupid-built-cupidld.elf"
        cls.cupid_cupidobj_path = build_path / "cupid-built-cupidobj.elf"
        cls.cupid_cupidc_path = build_path / "cupid-built-cupidc.elf"
        cls.cupid_runtime_path = build_path / "cupid-built-runtime.elf"
        cls._cupid_tool_link = None
        target = f"{relative_build}/cupidc-object-contract{suffix}"
        cupidasm_target = f"{relative_build}/cupidasm{suffix}"
        cupiddis_target = f"{relative_build}/cupiddis{suffix}"
        cupidld_target = f"{relative_build}/cupidld{suffix}"
        cupidobj_target = f"{relative_build}/cupidobj{suffix}"
        cupidc_target = f"{relative_build}/cupidc{suffix}"
        result = subprocess.run(
            [
                "make",
                "-C",
                str(TOOLCHAIN_ROOT),
                f"BUILD_DIR={relative_build}",
                target,
                cupidasm_target,
                cupiddis_target,
                cupidld_target,
                cupidobj_target,
                cupidc_target,
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        if (
            result.returncode != 0
            or not cls.contract_path.exists()
            or not cls.hosted_cupidasm_path.exists()
            or not cls.hosted_cupiddis_path.exists()
            or not cls.hosted_cupidld_path.exists()
            or not cls.hosted_cupidobj_path.exists()
            or not cls.hosted_cupidc_path.exists()
        ):
            cls._build_directory.cleanup()
            raise AssertionError(
                "CupidC object contract build failed\n"
                + result.stdout
                + result.stderr
            )

    @classmethod
    def tearDownClass(cls):
        cls._build_directory.cleanup()

    @classmethod
    def build_cupid_tools(cls):
        if cls._cupid_tool_link is None:
            cls._cupid_tool_link = subprocess.run(
                [
                    str(cls.contract_path),
                    "self-host-link-tools",
                    str(REPO_ROOT),
                    str(cls.cupid_cupidasm_path),
                    str(cls.cupid_cupiddis_path),
                    str(cls.cupid_cupidld_path),
                    str(cls.cupid_cupidobj_path),
                    str(cls.cupid_cupidc_path),
                    str(cls.cupid_runtime_path),
                ],
                cwd=TOOLCHAIN_ROOT,
                text=True,
                capture_output=True,
                timeout=180,
            )
        return cls._cupid_tool_link

    def wsl_path(self, path):
        converted = subprocess.run(
            ["wsl", "-e", "wslpath", "-a", str(path)],
            text=True,
            capture_output=True,
        )
        self.assertEqual(
            converted.returncode,
            0,
            "WSL could not translate " + str(path) + "\n" + converted.stderr,
        )
        return converted.stdout.strip()

    def run_cupid_linux_tool(self, executable, arguments, timeout=20):
        if os.name != "nt":
            executable.chmod(0o755)
            try:
                return subprocess.run(
                    [
                        str(executable),
                        *[str(argument) for argument in arguments],
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=timeout,
                )
            except OSError as error:
                if error.errno == 8:
                    self.skipTest(
                        "this kernel cannot execute static i386 ELF files"
                    )
                raise
        if shutil.which("wsl") is None:
            self.skipTest("WSL is unavailable for the i386 Linux tool check")
        linux_executable = self.wsl_path(executable)
        linux_arguments = [
            self.wsl_path(argument)
            if isinstance(argument, Path)
            else str(argument)
            for argument in arguments
        ]
        script = (
            'probe="/tmp/cupid-built-tool-$$"; '
            "trap 'rm -f \"$probe\"' EXIT HUP INT TERM; "
            'cp "$1" "$probe" || exit 125; '
            'chmod +x "$probe" || exit 125; '
            'shift; "$probe" "$@"'
        )
        result = subprocess.run(
            [
                "wsl",
                "-e",
                "sh",
                "-c",
                script,
                "sh",
                linux_executable,
                *linux_arguments,
            ],
            text=True,
            capture_output=True,
            timeout=timeout,
        )
        if result.returncode in (126, 127):
            self.skipTest("WSL cannot execute static i386 ELF files")
        return result

    def test_static_definitions_emit_deterministic_elf32_objects(self):
        result = subprocess.run(
            [str(self.contract_path), "static-data", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "static-data: ok\n")

    def test_direct_goto_emits_resolved_intra_function_branches(self):
        result = subprocess.run(
            [str(self.contract_path), "direct-goto", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "direct-goto: ok\n")

    def test_switch_emits_resolved_dispatch_branches(self):
        result = subprocess.run(
            [str(self.contract_path), "switch-object", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "switch-object: ok\n")

    def test_integer_mutation_emits_deterministic_i386_objects(self):
        result = subprocess.run(
            [str(self.contract_path), "integer-mutation", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "integer-mutation: ok\n")

    def test_object_pointer_values_emit_deterministic_i386_objects(self):
        result = subprocess.run(
            [str(self.contract_path), "pointer-values", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "pointer-values: ok\n")

    def test_object_pointer_comparisons_emit_unsigned_i386_predicates(self):
        result = subprocess.run(
            [str(self.contract_path), "pointer-comparisons", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "pointer-comparisons: ok\n")

    def test_object_pointer_conditions_emit_zero_tests_and_branches(self):
        result = subprocess.run(
            [str(self.contract_path), "pointer-conditions", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "pointer-conditions: ok\n")

    def test_object_pointer_arithmetic_emits_scaled_i386_operations(self):
        result = subprocess.run(
            [str(self.contract_path), "pointer-arithmetic", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "pointer-arithmetic: ok\n")

    def test_function_pointer_calls_emit_deterministic_i386_objects(self):
        result = subprocess.run(
            [str(self.contract_path), "function-pointers", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "function-pointers: ok\n")

    def test_fixed_automatic_objects_use_target_sized_i386_frames(self):
        result = subprocess.run(
            [str(self.contract_path), "automatic-objects", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "automatic-objects: ok\n")

    def test_block_extern_objects_emit_one_undefined_linked_symbol(self):
        result = subprocess.run(
            [str(self.contract_path), "block-externs", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "block-externs: ok\n")

    def test_block_function_declarations_match_file_scope_prototypes(self):
        result = subprocess.run(
            [str(self.contract_path), "block-functions", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "block-functions: ok\n")

    def test_block_enumerators_emit_like_direct_integer_constants(self):
        result = subprocess.run(
            [str(self.contract_path), "block-enums", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "block-enums: ok\n")

    def test_bit_field_assignments_emit_value_preserving_stores(self):
        result = subprocess.run(
            [str(self.contract_path), "bit-field-stores", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "bit-field-stores: ok\n")

    def test_bit_field_mutations_execute_with_single_designator_evaluation(self):
        result = subprocess.run(
            [str(self.contract_path), "bit-field-mutations", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "bit-field-mutations: ok\n")

    def test_block_typedefs_do_not_change_emitted_objects(self):
        result = subprocess.run(
            [str(self.contract_path), "block-typedefs", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "block-typedefs: ok\n")

    def test_automatic_aggregate_initializers_zero_then_store_subobjects(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "aggregate-initializers",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "aggregate-initializers: ok\n")

    def test_narrow_integer_mutation_emits_exact_width_i386_stores(self):
        result = subprocess.run(
            [str(self.contract_path), "narrow-mutations", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "narrow-mutations: ok\n")

    def test_narrow_integer_values_use_width_correct_i386_operations(self):
        result = subprocess.run(
            [str(self.contract_path), "narrow-values", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "narrow-values: ok\n")

    def test_explicit_void_casts_emit_exact_discard_code(self):
        result = subprocess.run(
            [str(self.contract_path), "void-casts", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "void-casts: ok\n")

    def test_pointer_output_assembly_emits_segmented_i386_load(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "pointer-output-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "pointer-output-assembly: ok\n")

    def test_operand_free_assembly_emits_exact_i386_instructions(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "operand-free-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "operand-free-assembly: ok\n")

    def test_structure_values_follow_the_i386_cdecl_memory_abi(self):
        result = subprocess.run(
            [str(self.contract_path), "structure-values", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "structure-values: ok\n")

    def test_calls_align_the_i386_stack_to_sixteen_bytes(self):
        result = subprocess.run(
            [str(self.contract_path), "call-alignment", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "call-alignment: ok\n")

    def test_block_static_objects_use_static_elf_storage(self):
        result = subprocess.run(
            [str(self.contract_path), "block-statics", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "block-statics: ok\n")

    def test_compound_literals_initialize_storage_before_structure_calls(self):
        result = subprocess.run(
            [str(self.contract_path), "compound-literals", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "compound-literals: ok\n")

    def test_variadic_callees_follow_the_i386_cursor_abi(self):
        result = subprocess.run(
            [str(self.contract_path), "variadic-callees", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "variadic-callees: ok\n")

    def test_wide_variadics_follow_the_i386_cursor_abi(self):
        result = subprocess.run(
            [str(self.contract_path), "wide-variadics", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "wide-variadics: ok\n")

    def test_floating_transport_models_selected_ieee_patterns(self):
        result = subprocess.run(
            [str(self.contract_path), "floating-transport", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "floating-transport: ok\n")

    def test_floating_arithmetic_spills_each_x87_result(self):
        result = subprocess.run(
            [str(self.contract_path), "floating-arithmetic", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "floating-arithmetic: ok\n")

    def test_floating_width_conversions_emit_deterministic_x87_objects(self):
        result = subprocess.run(
            [str(self.contract_path), "floating-conversions", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "floating-conversions: ok\n")

    def test_old_style_empty_functions_emit_deterministic_i386_objects(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "old-style-empty-functions",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "old-style-empty-functions: ok\n")

    def test_block_record_tags_emit_deterministic_i386_objects(self):
        result = subprocess.run(
            [str(self.contract_path), "block-records", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "block-records: ok\n")

    def test_wide_shifts_bitwise_operations_and_conversions_execute(self):
        result = subprocess.run(
            [str(self.contract_path), "wide-returns", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "wide-returns: ok\n")

    def test_wide_comparisons_and_conditions_execute(self):
        result = subprocess.run(
            [str(self.contract_path), "wide-conditions", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "wide-conditions: ok\n")

    def test_wide_integer_mutations_preserve_snapshots_and_cdecl_state(self):
        result = subprocess.run(
            [str(self.contract_path), "wide-mutations", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "wide-mutations: ok\n")

    def test_self_host_frontier_values_emit_execute_and_recover_together(self):
        result = subprocess.run(
            [str(self.contract_path), "self-host-frontier", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "self-host-frontier: ok\n")

    def test_i386_linux_host_adapters_emit_deterministic_objects(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "self-host-hosted-adapters",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "self-host-hosted-adapters: ok\n")

    def test_cupid_built_host_adapter_links_deterministically(self):
        with tempfile.TemporaryDirectory(prefix="cupid-host-link-") as temp:
            first = Path(temp) / "ctool-host-first.elf"
            second = Path(temp) / "ctool-host-second.elf"
            for output in (first, second):
                result = subprocess.run(
                    [
                        str(self.contract_path),
                        "self-host-link-ctool-host",
                        str(REPO_ROOT),
                        str(output),
                    ],
                    cwd=TOOLCHAIN_ROOT,
                    text=True,
                    capture_output=True,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(
                    result.stdout, "self-host-link-ctool-host: ok\n"
                )
            first_bytes = first.read_bytes()
            self.assertEqual(first_bytes, second.read_bytes())
            self.assertEqual(first_bytes[:4], b"\x7fELF")
            self.assertEqual(first_bytes[4:7], b"\x01\x01\x01")

    def test_cupid_built_host_adapter_runs_on_i386_linux(self):
        with tempfile.TemporaryDirectory(prefix="cupid-host-run-") as temp:
            output = Path(temp) / "ctool-host-smoke.elf"
            link = subprocess.run(
                [
                    str(self.contract_path),
                    "self-host-link-ctool-host",
                    str(REPO_ROOT),
                    str(output),
                ],
                cwd=TOOLCHAIN_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(link.returncode, 0, link.stderr)

            if os.name == "nt":
                if shutil.which("wsl") is None:
                    self.skipTest("WSL is unavailable for the i386 Linux smoke")
                converted = subprocess.run(
                    ["wsl", "-e", "wslpath", "-a", str(output)],
                    text=True,
                    capture_output=True,
                )
                if converted.returncode != 0:
                    self.skipTest("WSL could not translate the smoke path")
                linux_path = converted.stdout.strip()
                script = (
                    'probe="/tmp/cupid-ctool-host-$$"; '
                    "trap 'rm -f \"$probe\"' EXIT HUP INT TERM; "
                    'cp "$1" "$probe" || exit 125; '
                    'chmod +x "$probe" || exit 125; "$probe"'
                )
                run = subprocess.run(
                    ["wsl", "-e", "sh", "-c", script, "sh", linux_path],
                    text=True,
                    capture_output=True,
                    timeout=10,
                )
                if run.returncode in (126, 127):
                    self.skipTest("WSL cannot execute static i386 ELF files")
            else:
                output.chmod(0o755)
                try:
                    run = subprocess.run(
                        [str(output)],
                        text=True,
                        capture_output=True,
                        timeout=10,
                    )
                except OSError as error:
                    if error.errno == 8:
                        self.skipTest(
                            "this kernel cannot execute static i386 ELF files"
                        )
                    raise
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_cupid_built_tools_are_static_i386_executables(self):
        result = self.build_cupid_tools()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "self-host-link-tools: ok\n")
        for executable in (
            self.cupid_cupidasm_path,
            self.cupid_cupiddis_path,
            self.cupid_cupidld_path,
            self.cupid_cupidobj_path,
            self.cupid_cupidc_path,
            self.cupid_runtime_path,
        ):
            image = executable.read_bytes()
            self.assertEqual(image[:7], b"\x7fELF\x01\x01\x01")
            self.assertEqual(
                int.from_bytes(image[16:18], "little"), 2
            )
            self.assertEqual(
                int.from_bytes(image[18:20], "little"), 3
            )

    def test_cupid_built_runtime_contract_covers_success_and_failure_paths(self):
        linked = self.build_cupid_tools()
        self.assertEqual(linked.returncode, 0, linked.stderr)
        with tempfile.TemporaryDirectory(
            prefix=".cupid-built-runtime-contract-", dir=REPO_ROOT
        ) as temp:
            root = Path(temp)
            output = root / "runtime-output.txt"
            missing = root / "missing-input.txt"
            self.assertFalse(missing.exists())
            run = self.run_cupid_linux_tool(
                self.cupid_runtime_path,
                [output, missing],
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout, "runtime-ok\n")
            self.assertEqual(run.stderr, "")
            self.assertEqual(
                output.read_text(encoding="utf-8"),
                "ok -12 0000002A\n",
            )

    def test_toolchain_all_rebuilds_a_missing_cupid_artifact(self):
        build_path = Path(self._build_directory.name)
        relative_build = build_path.relative_to(TOOLCHAIN_ROOT).as_posix()
        command = [
            "make",
            "-C",
            str(TOOLCHAIN_ROOT),
            f"BUILD_DIR={relative_build}",
            "all",
        ]
        initial = subprocess.run(
            command,
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=180,
        )
        self.assertEqual(initial.returncode, 0, initial.stdout + initial.stderr)

        manifest = build_path / "cupidc-hosted-i386-tools.json"
        artifact = build_path / "cupidc-cupidc.elf"
        self.assertTrue(manifest.exists())
        self.assertTrue(artifact.exists())
        manifest_bytes = manifest.read_bytes()
        artifact_bytes = artifact.read_bytes()

        artifact.unlink()
        self.assertTrue(manifest.exists())
        rebuilt = subprocess.run(
            command,
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=180,
        )
        self.assertEqual(rebuilt.returncode, 0, rebuilt.stdout + rebuilt.stderr)
        self.assertIn("self-host-link-tools: ok", rebuilt.stdout)
        self.assertEqual(artifact.read_bytes(), artifact_bytes)
        self.assertEqual(manifest.read_bytes(), manifest_bytes)

        artifact.unlink()
        direct = subprocess.run(
            [
                "make",
                "-C",
                str(TOOLCHAIN_ROOT),
                f"BUILD_DIR={relative_build}",
                f"{relative_build}/cupidc-cupidc.elf",
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=180,
        )
        self.assertEqual(direct.returncode, 0, direct.stdout + direct.stderr)
        self.assertIn("self-host-link-tools: ok", direct.stdout)
        self.assertEqual(artifact.read_bytes(), artifact_bytes)
        self.assertEqual(manifest.read_bytes(), manifest_bytes)

    def test_cupid_built_tools_match_hosted_runtime_behavior(self):
        linked = self.build_cupid_tools()
        self.assertEqual(linked.returncode, 0, linked.stderr)
        with tempfile.TemporaryDirectory(
            prefix=".cupid-built-tools-", dir=REPO_ROOT
        ) as temp:
            root = Path(temp)
            source = root / "simple.asm"
            hosted_binary = root / "hosted.bin"
            cupid_binary = root / "cupid.bin"
            source.write_text(
                "BITS 16\n"
                "ORG 0x7c00\n"
                "start:\n"
                "    mov ax, 0x1234\n"
                "    ret\n",
                encoding="utf-8",
            )
            hosted_assembly = subprocess.run(
                [
                    str(self.hosted_cupidasm_path),
                    "-f",
                    "bin",
                    str(source),
                    "-o",
                    str(hosted_binary),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            cupid_assembly = self.run_cupid_linux_tool(
                self.cupid_cupidasm_path,
                ["-f", "bin", source, "-o", cupid_binary],
            )
            self.assertEqual(
                cupid_assembly.returncode,
                hosted_assembly.returncode,
                cupid_assembly.stderr,
            )
            self.assertEqual(cupid_assembly.stdout, hosted_assembly.stdout)
            self.assertEqual(cupid_assembly.stderr, hosted_assembly.stderr)
            self.assertEqual(cupid_binary.read_bytes(), hosted_binary.read_bytes())
            self.assertEqual(cupid_binary.read_bytes(), b"\xb8\x34\x12\xc3")

            hosted_report = subprocess.run(
                [
                    str(self.hosted_cupiddis_path),
                    "--raw",
                    "--mode",
                    "16",
                    "--base",
                    "0x7c00",
                    str(hosted_binary),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            cupid_report = self.run_cupid_linux_tool(
                self.cupid_cupiddis_path,
                [
                    "--raw",
                    "--mode",
                    "16",
                    "--base",
                    "0x7c00",
                    cupid_binary,
                ],
            )
            self.assertEqual(
                cupid_report.returncode,
                hosted_report.returncode,
                cupid_report.stderr,
            )
            self.assertEqual(cupid_report.stdout, hosted_report.stdout)
            self.assertEqual(cupid_report.stderr, hosted_report.stderr)
            self.assertIn("mov ax, 0x1234", cupid_report.stdout)

            link_source = root / "start.asm"
            hosted_object = root / "hosted-start.o"
            cupid_object = root / "cupid-start.o"
            hosted_executable = root / "hosted-linked.elf"
            cupid_executable = root / "cupid-linked.elf"
            link_source.write_text(
                "BITS 32\n"
                "global _start\n"
                "section .text\n"
                "_start:\n"
                "    mov eax, 1\n"
                "    xor ebx, ebx\n"
                "    int 0x80\n",
                encoding="utf-8",
            )
            hosted_object_build = subprocess.run(
                [
                    str(self.hosted_cupidasm_path),
                    "-f",
                    "elf32",
                    str(link_source),
                    "-o",
                    str(hosted_object),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            cupid_object_build = self.run_cupid_linux_tool(
                self.cupid_cupidasm_path,
                ["-f", "elf32", link_source, "-o", cupid_object],
            )
            self.assertEqual(
                cupid_object_build.returncode,
                hosted_object_build.returncode,
                cupid_object_build.stderr,
            )
            self.assertEqual(cupid_object.read_bytes(), hosted_object.read_bytes())
            hosted_link = subprocess.run(
                [
                    str(self.hosted_cupidld_path),
                    "-m",
                    "elf_i386",
                    "--text-address",
                    "0x00600000",
                    "--entry",
                    "_start",
                    "-o",
                    str(hosted_executable),
                    str(cupid_object),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            cupid_link = self.run_cupid_linux_tool(
                self.cupid_cupidld_path,
                [
                    "-m",
                    "elf_i386",
                    "--text-address",
                    "0x00600000",
                    "--entry",
                    "_start",
                    "-o",
                    cupid_executable,
                    cupid_object,
                ],
            )
            self.assertEqual(
                cupid_link.returncode,
                hosted_link.returncode,
                cupid_link.stderr,
            )
            self.assertEqual(cupid_link.stdout, hosted_link.stdout)
            self.assertEqual(cupid_link.stderr, hosted_link.stderr)
            self.assertEqual(
                cupid_executable.read_bytes(),
                hosted_executable.read_bytes(),
            )
            linked_image = cupid_executable.read_bytes()
            self.assertEqual(linked_image[:7], b"\x7fELF\x01\x01\x01")
            self.assertEqual(
                int.from_bytes(linked_image[24:28], "little"),
                0x00600000,
            )

            asset = root / "asset.bin"
            hosted_wrapped = root / "hosted-wrapped.o"
            cupid_wrapped = root / "cupid-wrapped.o"
            asset.write_bytes(b"Cupid\x00bytes")
            hosted_wrap = subprocess.run(
                [
                    str(self.hosted_cupidobj_path),
                    "wrap",
                    str(asset),
                    "--stem",
                    "payload",
                    "--section",
                    ".rodata",
                    "--readonly",
                    "-o",
                    str(hosted_wrapped),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            cupid_wrap = self.run_cupid_linux_tool(
                self.cupid_cupidobj_path,
                [
                    "wrap",
                    asset,
                    "--stem",
                    "payload",
                    "--section",
                    ".rodata",
                    "--readonly",
                    "-o",
                    cupid_wrapped,
                ],
            )
            self.assertEqual(
                cupid_wrap.returncode,
                hosted_wrap.returncode,
                cupid_wrap.stderr,
            )
            self.assertEqual(cupid_wrap.stdout, hosted_wrap.stdout)
            self.assertEqual(cupid_wrap.stderr, hosted_wrap.stderr)
            self.assertEqual(cupid_wrapped.read_bytes(), hosted_wrapped.read_bytes())
            self.assertEqual(
                cupid_wrapped.read_bytes()[:7],
                b"\x7fELF\x01\x01\x01",
            )

    def test_cupid_built_cupidc_matches_hosted_object_emission_and_failures(self):
        linked = self.build_cupid_tools()
        self.assertEqual(linked.returncode, 0, linked.stderr)
        with tempfile.TemporaryDirectory(
            prefix=".cupid-built-cupidc-", dir=REPO_ROOT
        ) as temp:
            root = Path(temp)
            include_root = root / "include"
            include_root.mkdir()
            header = include_root / "value.h"
            source = root / "source.c"
            gnu = root / "gnu.c"
            freestanding = root / "freestanding.c"
            invalid = root / "invalid.c"
            hosted_object = root / "hosted.o"
            cupid_object = root / "cupid.o"
            cupid_native_path_object = root / "cupid-native-path.o"
            hosted_gnu_object = root / "hosted-gnu.o"
            cupid_gnu_object = root / "cupid-gnu.o"
            hosted_gnu_strict_output = root / "hosted-gnu-strict.o"
            cupid_gnu_strict_output = root / "cupid-gnu-strict.o"
            hosted_freestanding = root / "hosted-freestanding.o"
            cupid_freestanding = root / "cupid-freestanding.o"
            hosted_failure = root / "hosted-failure.o"
            cupid_failure = root / "cupid-failure.o"
            hosted_missing_output = root / "hosted-missing.o"
            cupid_missing_output = root / "cupid-missing.o"
            reserved_output = root / "reserved.o"
            reserved_undef_output = root / "reserved-undef.o"
            header.write_text("#define HEADER_VALUE 11\n", encoding="utf-8")
            source.write_text(
                "#include <value.h>\n"
                "#if __STDC_HOSTED__ != 1\n"
                "#error hosted profile missing\n"
                "#endif\n"
                "#ifdef REMOVED_VALUE\n"
                "#error command-line undef failed\n"
                "#endif\n"
                "int compiled_value(int argument) {\n"
                "  return argument + HEADER_VALUE + COMMAND_VALUE +\n"
                "         __SIZEOF_POINTER__;\n"
                "}\n",
                encoding="utf-8",
            )
            gnu.write_text(
                "#if 0b10 != 2\n"
                "#error GNU binary constant was misread\n"
                "#endif\n"
                "int gnu_value(void) { return 0b10; }\n",
                encoding="utf-8",
            )
            freestanding.write_text(
                "#if __STDC_HOSTED__ != 0\n"
                "#error freestanding profile missing\n"
                "#endif\n"
                "int freestanding_value(void) { return 4; }\n",
                encoding="utf-8",
            )
            invalid.write_text(
                "int broken_source( {\n",
                encoding="utf-8",
            )
            arguments = [
                "--root",
                root,
                "-c",
                "/source.c",
                "-I",
                "/include",
                "-D",
                "COMMAND_VALUE=7",
                "-D",
                "REMOVED_VALUE=1",
                "-U",
                "REMOVED_VALUE",
                "--gnu",
            ]
            hosted = subprocess.run(
                [
                    str(self.hosted_cupidc_path),
                    *[str(argument) for argument in arguments],
                    "-o",
                    "/hosted.o",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )
            cupid = self.run_cupid_linux_tool(
                self.cupid_cupidc_path,
                [*arguments, "-o", "/cupid.o"],
                timeout=60,
            )
            self.assertEqual(hosted.returncode, 0, hosted.stderr)
            self.assertEqual(cupid.returncode, hosted.returncode, cupid.stderr)
            self.assertEqual(cupid.stdout, hosted.stdout)
            self.assertEqual(cupid.stderr, hosted.stderr)
            self.assertEqual(cupid_object.read_bytes(), hosted_object.read_bytes())
            self.assertEqual(cupid_object.read_bytes()[:7], b"\x7fELF\x01\x01\x01")
            cupid_native_path = self.run_cupid_linux_tool(
                self.cupid_cupidc_path,
                [
                    "-c",
                    source,
                    "-I",
                    include_root,
                    "-D",
                    "COMMAND_VALUE=7",
                    "-D",
                    "REMOVED_VALUE=1",
                    "-U",
                    "REMOVED_VALUE",
                    "--gnu",
                    "-o",
                    cupid_native_path_object,
                ],
                timeout=60,
            )
            self.assertEqual(
                cupid_native_path.returncode, 0, cupid_native_path.stderr
            )
            self.assertEqual(cupid_native_path.stdout, hosted.stdout)
            self.assertEqual(cupid_native_path.stderr, hosted.stderr)
            self.assertEqual(
                cupid_native_path_object.read_bytes(),
                hosted_object.read_bytes(),
            )

            hosted_gnu = subprocess.run(
                [
                    str(self.hosted_cupidc_path),
                    "--root",
                    str(root),
                    "--gnu",
                    "-c",
                    "/gnu.c",
                    "-o",
                    "/hosted-gnu.o",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )
            cupid_gnu = self.run_cupid_linux_tool(
                self.cupid_cupidc_path,
                [
                    "--root",
                    root,
                    "--gnu",
                    "-c",
                    "/gnu.c",
                    "-o",
                    "/cupid-gnu.o",
                ],
                timeout=60,
            )
            self.assertEqual(hosted_gnu.returncode, 0, hosted_gnu.stderr)
            self.assertEqual(cupid_gnu.returncode, hosted_gnu.returncode)
            self.assertEqual(cupid_gnu.stdout, hosted_gnu.stdout)
            self.assertEqual(cupid_gnu.stderr, hosted_gnu.stderr)
            self.assertEqual(
                cupid_gnu_object.read_bytes(),
                hosted_gnu_object.read_bytes(),
            )

            hosted_gnu_strict_output.write_bytes(b"hosted-gnu-sentinel")
            cupid_gnu_strict_output.write_bytes(b"cupid-gnu-sentinel")
            hosted_gnu_strict = subprocess.run(
                [
                    str(self.hosted_cupidc_path),
                    "--root",
                    str(root),
                    "-c",
                    "/gnu.c",
                    "-o",
                    "/hosted-gnu-strict.o",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )
            cupid_gnu_strict = self.run_cupid_linux_tool(
                self.cupid_cupidc_path,
                [
                    "--root",
                    root,
                    "-c",
                    "/gnu.c",
                    "-o",
                    "/cupid-gnu-strict.o",
                ],
                timeout=60,
            )
            self.assertEqual(
                hosted_gnu_strict.returncode, 1, hosted_gnu_strict.stderr
            )
            self.assertEqual(
                cupid_gnu_strict.returncode,
                hosted_gnu_strict.returncode,
                cupid_gnu_strict.stderr,
            )
            self.assertEqual(cupid_gnu_strict.stdout, hosted_gnu_strict.stdout)
            self.assertEqual(cupid_gnu_strict.stderr, hosted_gnu_strict.stderr)
            self.assertIn(
                "CupidC binary conditional constants require GNU mode",
                hosted_gnu_strict.stderr,
            )
            self.assertEqual(
                hosted_gnu_strict_output.read_bytes(),
                b"hosted-gnu-sentinel",
            )
            self.assertEqual(
                cupid_gnu_strict_output.read_bytes(),
                b"cupid-gnu-sentinel",
            )

            hosted_free = subprocess.run(
                [
                    str(self.hosted_cupidc_path),
                    "--root",
                    str(root),
                    "--freestanding",
                    "-c",
                    "/freestanding.c",
                    "-o",
                    "/hosted-freestanding.o",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )
            cupid_free = self.run_cupid_linux_tool(
                self.cupid_cupidc_path,
                [
                    "--root",
                    root,
                    "--freestanding",
                    "-c",
                    "/freestanding.c",
                    "-o",
                    "/cupid-freestanding.o",
                ],
                timeout=60,
            )
            self.assertEqual(hosted_free.returncode, 0, hosted_free.stderr)
            self.assertEqual(
                cupid_free.returncode, hosted_free.returncode, cupid_free.stderr
            )
            self.assertEqual(cupid_free.stdout, hosted_free.stdout)
            self.assertEqual(cupid_free.stderr, hosted_free.stderr)
            self.assertEqual(
                cupid_freestanding.read_bytes(),
                hosted_freestanding.read_bytes(),
            )

            hosted_failure.write_bytes(b"hosted-sentinel")
            cupid_failure.write_bytes(b"cupid-sentinel")
            hosted_bad = subprocess.run(
                [
                    str(self.hosted_cupidc_path),
                    "--root",
                    str(root),
                    "-c",
                    "/invalid.c",
                    "-o",
                    "/hosted-failure.o",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )
            cupid_bad = self.run_cupid_linux_tool(
                self.cupid_cupidc_path,
                [
                    "--root",
                    root,
                    "-c",
                    "/invalid.c",
                    "-o",
                    "/cupid-failure.o",
                ],
                timeout=60,
            )
            self.assertEqual(hosted_bad.returncode, 1, hosted_bad.stderr)
            self.assertIn("/invalid.c:1:", hosted_bad.stderr)
            self.assertIn(
                "declaration specifiers do not name a type",
                hosted_bad.stderr,
            )
            self.assertEqual(
                cupid_bad.returncode, hosted_bad.returncode, cupid_bad.stderr
            )
            self.assertEqual(cupid_bad.stdout, hosted_bad.stdout)
            self.assertEqual(cupid_bad.stderr, hosted_bad.stderr)
            self.assertEqual(hosted_failure.read_bytes(), b"hosted-sentinel")
            self.assertEqual(cupid_failure.read_bytes(), b"cupid-sentinel")

            hosted_missing_output.write_bytes(b"hosted-missing-sentinel")
            cupid_missing_output.write_bytes(b"cupid-missing-sentinel")
            hosted_missing = subprocess.run(
                [
                    str(self.hosted_cupidc_path),
                    "--root",
                    str(root),
                    "-c",
                    "/missing.c",
                    "-o",
                    "/hosted-missing.o",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )
            cupid_missing = self.run_cupid_linux_tool(
                self.cupid_cupidc_path,
                [
                    "--root",
                    root,
                    "-c",
                    "/missing.c",
                    "-o",
                    "/cupid-missing.o",
                ],
                timeout=60,
            )
            self.assertEqual(hosted_missing.returncode, 1)
            self.assertEqual(
                cupid_missing.returncode,
                hosted_missing.returncode,
                cupid_missing.stderr,
            )
            self.assertIn(
                "cupidc: cannot load /missing.c (not_found)",
                hosted_missing.stderr,
            )
            self.assertEqual(cupid_missing.stdout, hosted_missing.stdout)
            self.assertEqual(cupid_missing.stderr, hosted_missing.stderr)
            self.assertEqual(
                hosted_missing_output.read_bytes(),
                b"hosted-missing-sentinel",
            )
            self.assertEqual(
                cupid_missing_output.read_bytes(),
                b"cupid-missing-sentinel",
            )

            hosted_reserved = subprocess.run(
                [
                    str(self.hosted_cupidc_path),
                    "--root",
                    str(root),
                    "-c",
                    "/source.c",
                    "-D__SIZEOF_POINTER__=8",
                    "-o",
                    "/reserved.o",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )
            cupid_reserved = self.run_cupid_linux_tool(
                self.cupid_cupidc_path,
                [
                    "--root",
                    root,
                    "-c",
                    "/source.c",
                    "-D__SIZEOF_POINTER__=8",
                    "-o",
                    "/reserved.o",
                ],
                timeout=60,
            )
            self.assertEqual(hosted_reserved.returncode, 2, hosted_reserved.stderr)
            self.assertEqual(
                cupid_reserved.returncode,
                hosted_reserved.returncode,
                cupid_reserved.stderr,
            )
            self.assertEqual(cupid_reserved.stdout, hosted_reserved.stdout)
            self.assertEqual(cupid_reserved.stderr, hosted_reserved.stderr)
            self.assertFalse(reserved_output.exists())

            hosted_reserved_undef = subprocess.run(
                [
                    str(self.hosted_cupidc_path),
                    "--root",
                    str(root),
                    "-c",
                    "/source.c",
                    "-U__SIZEOF_POINTER__",
                    "-o",
                    "/reserved-undef.o",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )
            cupid_reserved_undef = self.run_cupid_linux_tool(
                self.cupid_cupidc_path,
                [
                    "--root",
                    root,
                    "-c",
                    "/source.c",
                    "-U__SIZEOF_POINTER__",
                    "-o",
                    "/reserved-undef.o",
                ],
                timeout=60,
            )
            self.assertEqual(
                hosted_reserved_undef.returncode,
                2,
                hosted_reserved_undef.stderr,
            )
            self.assertEqual(
                cupid_reserved_undef.returncode,
                hosted_reserved_undef.returncode,
                cupid_reserved_undef.stderr,
            )
            self.assertEqual(
                cupid_reserved_undef.stdout,
                hosted_reserved_undef.stdout,
            )
            self.assertEqual(
                cupid_reserved_undef.stderr,
                hosted_reserved_undef.stderr,
            )
            self.assertFalse(reserved_undef_output.exists())

    def test_cupidc_angle_only_roots_do_not_change_quoted_lookup(self):
        linked = self.build_cupid_tools()
        self.assertEqual(linked.returncode, 0, linked.stderr)
        with tempfile.TemporaryDirectory(
            prefix=".cupidc-include-forms-", dir=REPO_ROOT
        ) as temp:
            root = Path(temp)
            quoted_root = root / "quoted"
            angle_root = root / "angle"
            quoted_root.mkdir()
            angle_root.mkdir()
            (quoted_root / "select.h").write_text(
                "#define SELECTED_VALUE 11\n", encoding="utf-8"
            )
            (angle_root / "select.h").write_text(
                "#define SELECTED_VALUE 22\n", encoding="utf-8"
            )
            (root / "quoted.c").write_text(
                '#include "select.h"\n'
                "int quote_selection(void) { return SELECTED_VALUE; }\n",
                encoding="utf-8",
            )
            (root / "angle.c").write_text(
                "#include <select.h>\n"
                "int angle_selection(void) { return SELECTED_VALUE; }\n",
                encoding="utf-8",
            )

            compile_cases = (
                (
                    "/quoted.c",
                    "/quoted",
                    "/expected-quoted.o",
                    "/hosted-quoted.o",
                    "/cupid-quoted.o",
                ),
                (
                    "/angle.c",
                    "/angle",
                    "/expected-angle.o",
                    "/hosted-angle.o",
                    "/cupid-angle.o",
                ),
            )
            for (
                source,
                expected_include,
                expected_output,
                hosted_output,
                cupid_output,
            ) in compile_cases:
                expected = subprocess.run(
                    [
                        str(self.hosted_cupidc_path),
                        "--root",
                        str(root),
                        "-c",
                        source,
                        "-I",
                        expected_include,
                        "-o",
                        expected_output,
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=60,
                )
                hosted = subprocess.run(
                    [
                        str(self.hosted_cupidc_path),
                        "--root",
                        str(root),
                        "-c",
                        source,
                        "--include-angle",
                        "/angle",
                        "-I",
                        "/quoted",
                        "-o",
                        hosted_output,
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=60,
                )
                cupid = self.run_cupid_linux_tool(
                    self.cupid_cupidc_path,
                    [
                        "--root",
                        root,
                        "-c",
                        source,
                        "--include-angle",
                        "/angle",
                        "-I",
                        "/quoted",
                        "-o",
                        cupid_output,
                    ],
                    timeout=60,
                )
                self.assertEqual(expected.returncode, 0, expected.stderr)
                self.assertEqual(hosted.returncode, 0, hosted.stderr)
                self.assertEqual(cupid.returncode, 0, cupid.stderr)
                self.assertEqual(expected.stdout, "")
                self.assertEqual(hosted.stdout, "")
                self.assertEqual(cupid.stdout, "")
                self.assertEqual(expected.stderr, "")
                self.assertEqual(hosted.stderr, "")
                self.assertEqual(cupid.stderr, "")
                expected_object = root / expected_output[1:]
                hosted_object = root / hosted_output[1:]
                cupid_object = root / cupid_output[1:]
                self.assertEqual(
                    hosted_object.read_bytes(), expected_object.read_bytes()
                )
                self.assertEqual(
                    cupid_object.read_bytes(), expected_object.read_bytes()
                )
                self.assertEqual(
                    expected_object.read_bytes()[:7],
                    b"\x7fELF\x01\x01\x01",
                )

    def test_cupidc_angle_only_root_rejects_bad_values_and_root_paths(self):
        linked = self.build_cupid_tools()
        self.assertEqual(linked.returncode, 0, linked.stderr)
        usage = (
            "usage: cupidc -c INPUT -o OUTPUT [-I PATH] "
            "[--include-angle PATH] [-D NAME[=VALUE]] [-U NAME] [--gnu] "
            "[--freestanding] [--root NATIVE_ROOT]\n"
        )
        with tempfile.TemporaryDirectory(
            prefix=".cupidc-include-errors-", dir=REPO_ROOT
        ) as temp:
            root = Path(temp)
            (root / "source.c").write_text(
                "int include_error_source(void) { return 0; }\n",
                encoding="utf-8",
            )
            cases = (
                (
                    "missing",
                    [
                        "--root",
                        root,
                        "-c",
                        "/source.c",
                        "-o",
                        "/hosted-missing.o",
                        "--include-angle",
                    ],
                    [
                        "--root",
                        root,
                        "-c",
                        "/source.c",
                        "-o",
                        "/cupid-missing.o",
                        "--include-angle",
                    ],
                    2,
                    usage,
                    root / "hosted-missing.o",
                    root / "cupid-missing.o",
                ),
                (
                    "empty",
                    [
                        "--root",
                        root,
                        "-c",
                        "/source.c",
                        "--include-angle",
                        "",
                        "-o",
                        "/hosted-empty.o",
                    ],
                    [
                        "--root",
                        root,
                        "-c",
                        "/source.c",
                        "--include-angle",
                        "",
                        "-o",
                        "/cupid-empty.o",
                    ],
                    2,
                    usage,
                    root / "hosted-empty.o",
                    root / "cupid-empty.o",
                ),
                (
                    "relative",
                    [
                        "--root",
                        root,
                        "-c",
                        "/source.c",
                        "--include-angle",
                        "angle",
                        "-o",
                        "/hosted-relative.o",
                    ],
                    [
                        "--root",
                        root,
                        "-c",
                        "/source.c",
                        "--include-angle",
                        "angle",
                        "-o",
                        "/cupid-relative.o",
                    ],
                    1,
                    "cupidc: --root requires logical include paths\n",
                    root / "hosted-relative.o",
                    root / "cupid-relative.o",
                ),
            )
            for (
                name,
                hosted_arguments,
                cupid_arguments,
                returncode,
                stderr,
                hosted_output,
                cupid_output,
            ) in cases:
                with self.subTest(name=name):
                    hosted = subprocess.run(
                        [
                            str(self.hosted_cupidc_path),
                            *[str(argument) for argument in hosted_arguments],
                        ],
                        cwd=REPO_ROOT,
                        text=True,
                        capture_output=True,
                        timeout=60,
                    )
                    cupid = self.run_cupid_linux_tool(
                        self.cupid_cupidc_path,
                        cupid_arguments,
                        timeout=60,
                    )
                    self.assertEqual(
                        hosted.returncode, returncode, hosted.stderr
                    )
                    self.assertEqual(
                        cupid.returncode, returncode, cupid.stderr
                    )
                    self.assertEqual(hosted.stdout, "")
                    self.assertEqual(cupid.stdout, "")
                    self.assertEqual(hosted.stderr, stderr)
                    self.assertEqual(cupid.stderr, stderr)
                    self.assertFalse(hosted_output.exists())
                    self.assertFalse(cupid_output.exists())

    def test_cupid_built_cupidc_matches_help_and_usage_failures(self):
        linked = self.build_cupid_tools()
        self.assertEqual(linked.returncode, 0, linked.stderr)
        usage = (
            "usage: cupidc -c INPUT -o OUTPUT [-I PATH] "
            "[--include-angle PATH] [-D NAME[=VALUE]] [-U NAME] [--gnu] "
            "[--freestanding] [--root NATIVE_ROOT]\n"
        )
        hosted_help = subprocess.run(
            [str(self.hosted_cupidc_path), "--help"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=20,
        )
        cupid_help = self.run_cupid_linux_tool(
            self.cupid_cupidc_path, ["--help"], timeout=20
        )
        self.assertEqual(hosted_help.returncode, 0, hosted_help.stderr)
        self.assertEqual(cupid_help.returncode, hosted_help.returncode)
        self.assertEqual(hosted_help.stdout, usage)
        self.assertEqual(cupid_help.stdout, usage)
        self.assertEqual(hosted_help.stderr, "")
        self.assertEqual(cupid_help.stderr, "")

        hosted_invalid = subprocess.run(
            [str(self.hosted_cupidc_path), "-c", "missing.c"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=20,
        )
        cupid_invalid = self.run_cupid_linux_tool(
            self.cupid_cupidc_path,
            ["-c", "missing.c"],
            timeout=20,
        )
        self.assertEqual(hosted_invalid.returncode, 2)
        self.assertEqual(cupid_invalid.returncode, hosted_invalid.returncode)
        self.assertEqual(hosted_invalid.stdout, "")
        self.assertEqual(cupid_invalid.stdout, "")
        self.assertEqual(hosted_invalid.stderr, usage)
        self.assertEqual(cupid_invalid.stderr, usage)

    def test_hosted_cupidc_resolves_native_absolute_and_relative_paths(self):
        with tempfile.TemporaryDirectory(
            prefix=".hosted-cupidc-paths-", dir=REPO_ROOT
        ) as temp:
            root = Path(temp)
            source = root / "source.c"
            absolute_object = root / "absolute.o"
            relative_object = root / "relative.o"
            source.write_text(
                "int native_path_value(void) { return 32; }\n",
                encoding="utf-8",
            )
            absolute = subprocess.run(
                [
                    str(self.hosted_cupidc_path),
                    "-c",
                    str(source.resolve()),
                    "-o",
                    str(absolute_object.resolve()),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )
            relative = subprocess.run(
                [
                    str(self.hosted_cupidc_path),
                    "-c",
                    source.name,
                    "-o",
                    relative_object.name,
                ],
                cwd=root,
                text=True,
                capture_output=True,
                timeout=60,
            )
            self.assertEqual(absolute.returncode, 0, absolute.stderr)
            self.assertEqual(relative.returncode, 0, relative.stderr)
            self.assertEqual(absolute.stdout, "")
            self.assertEqual(relative.stdout, "")
            self.assertEqual(absolute.stderr, "")
            self.assertEqual(relative.stderr, "")
            self.assertEqual(
                relative_object.read_bytes(), absolute_object.read_bytes()
            )
            self.assertEqual(
                absolute_object.read_bytes()[:7], b"\x7fELF\x01\x01\x01"
            )
            if os.name == "nt":
                root_relative_object = root / "root-relative.o"
                source_absolute = source.resolve()
                object_absolute = root_relative_object.resolve()
                root_relative_source = (
                    "\\"
                    + str(source_absolute.relative_to(source_absolute.anchor))
                )
                root_relative_output = (
                    "\\"
                    + str(object_absolute.relative_to(object_absolute.anchor))
                )
                root_relative = subprocess.run(
                    [
                        str(self.hosted_cupidc_path),
                        "-c",
                        root_relative_source,
                        "-o",
                        root_relative_output,
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=60,
                )
                self.assertEqual(
                    root_relative.returncode, 0, root_relative.stderr
                )
                self.assertEqual(root_relative.stdout, "")
                self.assertEqual(root_relative.stderr, "")
                self.assertEqual(
                    root_relative_object.read_bytes(),
                    absolute_object.read_bytes(),
                )

    def test_cupid_built_cupidc_reproduces_a_compiler_implementation_object(self):
        linked = self.build_cupid_tools()
        self.assertEqual(linked.returncode, 0, linked.stderr)
        with tempfile.TemporaryDirectory(
            prefix=".cupid-built-cupidc-generation-", dir=REPO_ROOT
        ) as temp:
            root = Path(temp)
            relative = root.relative_to(REPO_ROOT).as_posix()
            hosted_object = root / "generation-zero.o"
            first_object = root / "generation-one-first.o"
            second_object = root / "generation-one-second.o"
            common = [
                "--root",
                REPO_ROOT,
                "-c",
                "/toolchain/cupidc_ir.c",
                "-I",
                "/toolchain",
            ]
            hosted = subprocess.run(
                [
                    str(self.hosted_cupidc_path),
                    *[str(argument) for argument in common],
                    "-o",
                    f"/{relative}/generation-zero.o",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=90,
            )
            first = self.run_cupid_linux_tool(
                self.cupid_cupidc_path,
                [
                    *common,
                    "-o",
                    f"/{relative}/generation-one-first.o",
                ],
                timeout=90,
            )
            second = self.run_cupid_linux_tool(
                self.cupid_cupidc_path,
                [
                    *common,
                    "-o",
                    f"/{relative}/generation-one-second.o",
                ],
                timeout=90,
            )
            self.assertEqual(hosted.returncode, 0, hosted.stderr)
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertEqual(first.stdout, hosted.stdout)
            self.assertEqual(first.stderr, hosted.stderr)
            self.assertEqual(second.stdout, hosted.stdout)
            self.assertEqual(second.stderr, hosted.stderr)
            self.assertEqual(first_object.read_bytes(), hosted_object.read_bytes())
            self.assertEqual(second_object.read_bytes(), hosted_object.read_bytes())
            self.assertGreater(len(hosted_object.read_bytes()), 100000)
            self.assertEqual(hosted_object.read_bytes()[:7], b"\x7fELF\x01\x01\x01")

    def test_cupid_built_toolchain_reaches_a_full_static_fixed_point(self):
        linked = self.build_cupid_tools()
        self.assertEqual(linked.returncode, 0, linked.stderr)
        self.assertEqual(len(CUPIDC_FIXED_POINT_SOURCES), 11)
        self.assertEqual(len(CUPID_TOOLCHAIN_FIXED_POINT_SOURCES), 19)
        self.assertEqual(
            [
                name
                for name, _source, gnu_extensions
                in CUPID_TOOLCHAIN_FIXED_POINT_SOURCES
                if gnu_extensions
            ],
            ["runtime"],
        )
        self.assertEqual(len(CUPIDC_FIXED_POINT_LINK_ORDER), 12)
        self.assertEqual(
            [name for name, _objects in CUPID_TOOLCHAIN_FIXED_POINT_LINKS],
            ["cupidasm", "cupiddis", "cupidld", "cupidobj", "cupidc"],
        )
        self.assertEqual(
            dict(CUPID_TOOLCHAIN_FIXED_POINT_LINKS)["cupidc"],
            CUPIDC_FIXED_POINT_LINK_ORDER,
        )
        usage = (
            "usage: cupidc -c INPUT -o OUTPUT [-I PATH] "
            "[--include-angle PATH] [-D NAME[=VALUE]] [-U NAME] [--gnu] "
            "[--freestanding] [--root NATIVE_ROOT]\n"
        )
        generation_one_tools = {
            "cupidasm": self.cupid_cupidasm_path,
            "cupiddis": self.cupid_cupiddis_path,
            "cupidld": self.cupid_cupidld_path,
            "cupidobj": self.cupid_cupidobj_path,
            "cupidc": self.cupid_cupidc_path,
        }
        generation_one_producers = {
            name: generation_one_tools[name]
            for name in ("cupidc", "cupidasm", "cupidld")
        }
        with tempfile.TemporaryDirectory(
            prefix=".cupid-toolchain-fixed-point-", dir=REPO_ROOT
        ) as temp:
            root = Path(temp)
            stage_producers = {}

            def build_stage(producers, stage_name):
                stage_producers[stage_name] = dict(producers)
                stage = root / stage_name
                stage.mkdir()
                logical_stage = "/" + stage.relative_to(REPO_ROOT).as_posix()

                def compile_source(case):
                    name, source, gnu_extensions = case
                    arguments = [
                        "--root",
                        REPO_ROOT,
                        "-c",
                        source,
                        *CUPIDC_FIXED_POINT_INCLUDE_ARGUMENTS,
                    ]
                    if gnu_extensions:
                        arguments.append("--gnu")
                    arguments.extend(
                        ["-o", f"{logical_stage}/{name}.o"]
                    )
                    return (
                        case,
                        self.run_cupid_linux_tool(
                            producers["cupidc"], arguments, timeout=300
                        ),
                    )

                with ThreadPoolExecutor(max_workers=2) as executor:
                    compiled = list(
                        executor.map(
                            compile_source,
                            CUPID_TOOLCHAIN_FIXED_POINT_SOURCES,
                        )
                    )
                objects = {}
                for (name, source, _gnu_extensions), result in compiled:
                    self.assertEqual(
                        result.returncode,
                        0,
                        source + "\n" + result.stderr,
                    )
                    self.assertEqual(result.stdout, "", source)
                    self.assertEqual(result.stderr, "", source)
                    object_path = stage / f"{name}.o"
                    object_bytes = object_path.read_bytes()
                    self.assertEqual(
                        object_bytes[:7],
                        b"\x7fELF\x01\x01\x01",
                        source,
                    )
                    self.assertEqual(
                        int.from_bytes(object_bytes[16:18], "little"),
                        1,
                        source,
                    )
                    self.assertEqual(
                        int.from_bytes(object_bytes[18:20], "little"),
                        3,
                        source,
                    )
                    objects[name] = object_path

                start_object = stage / "start.o"
                assembled = self.run_cupid_linux_tool(
                    producers["cupidasm"],
                    [
                        "-f",
                        "elf32",
                        TOOLCHAIN_ROOT / "hosted" / "i386-linux" / "start.asm",
                        "-o",
                        start_object,
                    ],
                    timeout=120,
                )
                self.assertEqual(
                    assembled.returncode, 0, assembled.stderr
                )
                self.assertEqual(assembled.stdout, "")
                self.assertEqual(assembled.stderr, "")
                start_bytes = start_object.read_bytes()
                self.assertEqual(
                    start_bytes[:7], b"\x7fELF\x01\x01\x01"
                )
                self.assertEqual(
                    int.from_bytes(start_bytes[16:18], "little"), 1
                )
                self.assertEqual(
                    int.from_bytes(start_bytes[18:20], "little"), 3
                )
                objects["start"] = start_object

                executables = {}
                for tool_name, link_order in (
                    CUPID_TOOLCHAIN_FIXED_POINT_LINKS
                ):
                    executable = stage / f"{tool_name}.elf"
                    linked_stage = self.run_cupid_linux_tool(
                        producers["cupidld"],
                        [
                            "-m",
                            "elf_i386",
                            "--text-address",
                            "0x08048000",
                            "--entry",
                            "_start",
                            "-o",
                            executable,
                            *[objects[name] for name in link_order],
                        ],
                        timeout=120,
                    )
                    self.assertEqual(
                        linked_stage.returncode,
                        0,
                        tool_name + "\n" + linked_stage.stderr,
                    )
                    self.assertEqual(linked_stage.stdout, "", tool_name)
                    self.assertEqual(linked_stage.stderr, "", tool_name)
                    image = executable.read_bytes()
                    self.assertEqual(
                        image[:7], b"\x7fELF\x01\x01\x01", tool_name
                    )
                    self.assertEqual(
                        int.from_bytes(image[16:18], "little"), 2, tool_name
                    )
                    self.assertEqual(
                        int.from_bytes(image[18:20], "little"), 3, tool_name
                    )
                    self.assertEqual(
                        int.from_bytes(image[24:28], "little"),
                        0x08048000,
                        tool_name,
                    )
                    executables[tool_name] = executable
                return objects, executables

            stage_two_objects, stage_two_tools = build_stage(
                generation_one_producers, "stage-two"
            )
            stage_two_producers = {
                name: stage_two_tools[name]
                for name in ("cupidc", "cupidasm", "cupidld")
            }
            stage_three_objects, stage_three_tools = build_stage(
                stage_two_producers, "stage-three"
            )
            self.assertEqual(
                stage_producers["stage-two"], generation_one_producers
            )
            self.assertEqual(
                stage_producers["stage-three"], stage_two_producers
            )
            for producer_name in ("cupidc", "cupidasm", "cupidld"):
                self.assertNotEqual(
                    stage_producers["stage-three"][producer_name],
                    generation_one_producers[producer_name],
                )
            for name, _source, _gnu_extensions in (
                CUPID_TOOLCHAIN_FIXED_POINT_SOURCES
            ):
                self.assertEqual(
                    stage_three_objects[name].read_bytes(),
                    stage_two_objects[name].read_bytes(),
                    name,
                )
            self.assertEqual(
                stage_three_objects["start"].read_bytes(),
                stage_two_objects["start"].read_bytes(),
            )
            for tool_name, generation_one_tool in (
                generation_one_tools.items()
            ):
                self.assertEqual(
                    stage_two_tools[tool_name].read_bytes(),
                    generation_one_tool.read_bytes(),
                    tool_name,
                )
                self.assertEqual(
                    stage_three_tools[tool_name].read_bytes(),
                    stage_two_tools[tool_name].read_bytes(),
                    tool_name,
                )

            def run_stage_pair(
                tool_name,
                stage_two_arguments,
                stage_three_arguments=None,
                timeout=60,
            ):
                if stage_three_arguments is None:
                    stage_three_arguments = stage_two_arguments
                stage_two_run = self.run_cupid_linux_tool(
                    stage_two_tools[tool_name],
                    stage_two_arguments,
                    timeout=timeout,
                )
                stage_three_run = self.run_cupid_linux_tool(
                    stage_three_tools[tool_name],
                    stage_three_arguments,
                    timeout=timeout,
                )
                self.assertEqual(
                    stage_three_run.returncode,
                    stage_two_run.returncode,
                    tool_name + "\n" + stage_three_run.stderr,
                )
                self.assertEqual(
                    stage_three_run.stdout,
                    stage_two_run.stdout,
                    tool_name,
                )
                self.assertEqual(
                    stage_three_run.stderr,
                    stage_two_run.stderr,
                    tool_name,
                )
                return stage_two_run, stage_three_run

            for tool_name in generation_one_tools:
                stage_two_help, _stage_three_help = run_stage_pair(
                    tool_name, ["--help"]
                )
                self.assertEqual(
                    stage_two_help.returncode,
                    0,
                    tool_name + "\n" + stage_two_help.stderr,
                )
                self.assertNotEqual(stage_two_help.stdout, "", tool_name)
                self.assertEqual(stage_two_help.stderr, "", tool_name)
            self.assertEqual(
                run_stage_pair("cupidc", ["--help"])[0].stdout,
                usage,
            )

            valid_source = root / "fixed-point-valid.c"
            invalid_source = root / "fixed-point-invalid.c"
            valid_source.write_text(
                "int fixed_point_value(int value) { return value + 17; }\n",
                encoding="utf-8",
            )
            invalid_source.write_text(
                "int fixed_point_broken( {\n",
                encoding="utf-8",
            )
            stage_two_valid = root / "stage-two-valid.o"
            stage_three_valid = root / "stage-three-valid.o"
            valid_arguments = [
                "--root",
                root,
                "-c",
                "/fixed-point-valid.c",
            ]
            stage_two_valid_run = self.run_cupid_linux_tool(
                stage_two_tools["cupidc"],
                [*valid_arguments, "-o", "/stage-two-valid.o"],
                timeout=60,
            )
            stage_three_valid_run = self.run_cupid_linux_tool(
                stage_three_tools["cupidc"],
                [*valid_arguments, "-o", "/stage-three-valid.o"],
                timeout=60,
            )
            self.assertEqual(stage_two_valid_run.returncode, 0)
            self.assertEqual(stage_two_valid_run.stdout, "")
            self.assertEqual(stage_two_valid_run.stderr, "")
            self.assertEqual(
                stage_three_valid_run.returncode,
                stage_two_valid_run.returncode,
                stage_three_valid_run.stderr,
            )
            self.assertEqual(
                stage_three_valid_run.stdout, stage_two_valid_run.stdout
            )
            self.assertEqual(
                stage_three_valid_run.stderr, stage_two_valid_run.stderr
            )
            self.assertEqual(
                stage_three_valid.read_bytes(),
                stage_two_valid.read_bytes(),
            )
            self.assertEqual(
                stage_two_valid.read_bytes()[:7],
                b"\x7fELF\x01\x01\x01",
            )

            failure_sentinel = b"fixed-point-failure-sentinel"
            stage_two_failure = root / "stage-two-failure.o"
            stage_three_failure = root / "stage-three-failure.o"
            stage_two_failure.write_bytes(failure_sentinel)
            stage_three_failure.write_bytes(failure_sentinel)
            invalid_arguments = [
                "--root",
                root,
                "-c",
                "/fixed-point-invalid.c",
            ]
            stage_two_invalid_run = self.run_cupid_linux_tool(
                stage_two_tools["cupidc"],
                [*invalid_arguments, "-o", "/stage-two-failure.o"],
                timeout=60,
            )
            stage_three_invalid_run = self.run_cupid_linux_tool(
                stage_three_tools["cupidc"],
                [*invalid_arguments, "-o", "/stage-three-failure.o"],
                timeout=60,
            )
            self.assertEqual(stage_two_invalid_run.returncode, 1)
            self.assertEqual(stage_two_invalid_run.stdout, "")
            self.assertEqual(
                stage_three_invalid_run.returncode,
                stage_two_invalid_run.returncode,
                stage_three_invalid_run.stderr,
            )
            self.assertEqual(
                stage_three_invalid_run.stdout,
                stage_two_invalid_run.stdout,
            )
            self.assertEqual(
                stage_three_invalid_run.stderr,
                stage_two_invalid_run.stderr,
            )
            self.assertIn(
                "/fixed-point-invalid.c:1:",
                stage_two_invalid_run.stderr,
            )
            self.assertEqual(stage_two_failure.read_bytes(), failure_sentinel)
            self.assertEqual(
                stage_three_failure.read_bytes(), failure_sentinel
            )

            assembly_source = root / "fixed-point.asm"
            stage_two_binary = root / "stage-two.bin"
            stage_three_binary = root / "stage-three.bin"
            assembly_source.write_text(
                "BITS 16\n"
                "ORG 0x7c00\n"
                "start:\n"
                "    mov ax, 0x1234\n"
                "    ret\n",
                encoding="utf-8",
            )
            stage_two_assembly, _stage_three_assembly = run_stage_pair(
                "cupidasm",
                [
                    "-f",
                    "bin",
                    assembly_source,
                    "-o",
                    stage_two_binary,
                ],
                [
                    "-f",
                    "bin",
                    assembly_source,
                    "-o",
                    stage_three_binary,
                ],
            )
            self.assertEqual(
                stage_two_assembly.returncode,
                0,
                stage_two_assembly.stderr,
            )
            self.assertEqual(
                stage_three_binary.read_bytes(),
                stage_two_binary.read_bytes(),
            )
            self.assertEqual(stage_two_binary.read_bytes(), b"\xb8\x34\x12\xc3")

            stage_two_report, _stage_three_report = run_stage_pair(
                "cupiddis",
                [
                    "--raw",
                    "--mode",
                    "16",
                    "--base",
                    "0x7c00",
                    stage_two_binary,
                ],
                [
                    "--raw",
                    "--mode",
                    "16",
                    "--base",
                    "0x7c00",
                    stage_three_binary,
                ],
            )
            self.assertEqual(
                stage_two_report.returncode,
                0,
                stage_two_report.stderr,
            )
            self.assertIn("mov ax, 0x1234", stage_two_report.stdout)

            asset = root / "fixed-point-asset.bin"
            stage_two_wrapped = root / "stage-two-wrapped.o"
            stage_three_wrapped = root / "stage-three-wrapped.o"
            asset.write_bytes(b"Cupid fixed point\x00")
            stage_two_wrap, _stage_three_wrap = run_stage_pair(
                "cupidobj",
                [
                    "wrap",
                    asset,
                    "--stem",
                    "fixed_point_asset",
                    "--section",
                    ".rodata",
                    "--readonly",
                    "-o",
                    stage_two_wrapped,
                ],
                [
                    "wrap",
                    asset,
                    "--stem",
                    "fixed_point_asset",
                    "--section",
                    ".rodata",
                    "--readonly",
                    "-o",
                    stage_three_wrapped,
                ],
            )
            self.assertEqual(
                stage_two_wrap.returncode, 0, stage_two_wrap.stderr
            )
            self.assertEqual(
                stage_three_wrapped.read_bytes(),
                stage_two_wrapped.read_bytes(),
            )
            self.assertEqual(
                stage_two_wrapped.read_bytes()[:7],
                b"\x7fELF\x01\x01\x01",
            )

            link_source = root / "fixed-point-start.asm"
            stage_two_link_object = root / "stage-two-start.o"
            stage_three_link_object = root / "stage-three-start.o"
            stage_two_linked = root / "stage-two-linked.elf"
            stage_three_linked = root / "stage-three-linked.elf"
            link_source.write_text(
                "BITS 32\n"
                "global _start\n"
                "section .text\n"
                "_start:\n"
                "    mov eax, 1\n"
                "    xor ebx, ebx\n"
                "    int 0x80\n",
                encoding="utf-8",
            )
            stage_two_link_assembly, _stage_three_link_assembly = (
                run_stage_pair(
                    "cupidasm",
                    [
                        "-f",
                        "elf32",
                        link_source,
                        "-o",
                        stage_two_link_object,
                    ],
                    [
                        "-f",
                        "elf32",
                        link_source,
                        "-o",
                        stage_three_link_object,
                    ],
                )
            )
            self.assertEqual(
                stage_two_link_assembly.returncode,
                0,
                stage_two_link_assembly.stderr,
            )
            self.assertEqual(
                stage_three_link_object.read_bytes(),
                stage_two_link_object.read_bytes(),
            )
            stage_two_link, _stage_three_link = run_stage_pair(
                "cupidld",
                [
                    "-m",
                    "elf_i386",
                    "--text-address",
                    "0x00600000",
                    "--entry",
                    "_start",
                    "-o",
                    stage_two_linked,
                    stage_two_link_object,
                ],
                [
                    "-m",
                    "elf_i386",
                    "--text-address",
                    "0x00600000",
                    "--entry",
                    "_start",
                    "-o",
                    stage_three_linked,
                    stage_three_link_object,
                ],
            )
            self.assertEqual(
                stage_two_link.returncode, 0, stage_two_link.stderr
            )
            self.assertEqual(
                stage_three_linked.read_bytes(),
                stage_two_linked.read_bytes(),
            )
            self.assertEqual(
                int.from_bytes(
                    stage_two_linked.read_bytes()[24:28], "little"
                ),
                0x00600000,
            )

            stage_two_nm, _stage_three_nm = run_stage_pair(
                "cupiddis",
                ["--nm", stage_two_linked],
                ["--nm", stage_three_linked],
            )
            self.assertEqual(
                stage_two_nm.returncode, 0, stage_two_nm.stderr
            )
            self.assertIn(" T _start\n", stage_two_nm.stdout)

            stage_two_script_linked = root / "stage-two-script.elf"
            stage_three_script_linked = root / "stage-three-script.elf"
            stage_two_script_link, _stage_three_script_link = run_stage_pair(
                "cupidld",
                [
                    "-m",
                    "elf_i386",
                    "-T",
                    REPO_ROOT / "link.ld",
                    "-o",
                    stage_two_script_linked,
                    stage_two_link_object,
                ],
                [
                    "-m",
                    "elf_i386",
                    "-T",
                    REPO_ROOT / "link.ld",
                    "-o",
                    stage_three_script_linked,
                    stage_three_link_object,
                ],
            )
            self.assertEqual(
                stage_two_script_link.returncode,
                0,
                stage_two_script_link.stderr,
            )
            self.assertEqual(
                stage_three_script_linked.read_bytes(),
                stage_two_script_linked.read_bytes(),
            )
            self.assertEqual(
                int.from_bytes(
                    stage_two_script_linked.read_bytes()[24:28],
                    "little",
                ),
                0x00100000,
            )

            text_asset = root / "fixed-point-text.txt"
            stage_two_text = root / "stage-two-text.o"
            stage_three_text = root / "stage-three-text.o"
            text_asset.write_bytes(b"first\r\nsecond\r\n")
            stage_two_text_wrap, _stage_three_text_wrap = run_stage_pair(
                "cupidobj",
                [
                    "wrap-text",
                    text_asset,
                    "--identity",
                    "fixed-point.txt",
                    "-o",
                    stage_two_text,
                ],
                [
                    "wrap-text",
                    text_asset,
                    "--identity",
                    "fixed-point.txt",
                    "-o",
                    stage_three_text,
                ],
            )
            self.assertEqual(
                stage_two_text_wrap.returncode,
                0,
                stage_two_text_wrap.stderr,
            )
            self.assertEqual(
                stage_three_text.read_bytes(),
                stage_two_text.read_bytes(),
            )

            stage_two_flat = root / "stage-two-flat.bin"
            stage_three_flat = root / "stage-three-flat.bin"
            stage_two_flat_run, _stage_three_flat_run = run_stage_pair(
                "cupidobj",
                [
                    "flat",
                    stage_two_linked,
                    "-o",
                    stage_two_flat,
                ],
                [
                    "flat",
                    stage_three_linked,
                    "-o",
                    stage_three_flat,
                ],
            )
            self.assertEqual(
                stage_two_flat_run.returncode,
                0,
                stage_two_flat_run.stderr,
            )
            self.assertEqual(
                stage_three_flat.read_bytes(),
                stage_two_flat.read_bytes(),
            )
            self.assertNotEqual(stage_two_flat.read_bytes(), b"")

            invalid_assembly = root / "fixed-point-invalid.asm"
            stage_two_invalid_assembly = root / "stage-two-invalid.bin"
            stage_three_invalid_assembly = root / "stage-three-invalid.bin"
            invalid_assembly.write_text(
                "BITS 16\nthis_is_not_an_instruction ax\n",
                encoding="utf-8",
            )
            stage_two_invalid_assembly.write_bytes(failure_sentinel)
            stage_three_invalid_assembly.write_bytes(failure_sentinel)
            invalid_asm_run, _invalid_asm_stage_three = run_stage_pair(
                "cupidasm",
                [
                    "-f",
                    "bin",
                    invalid_assembly,
                    "-o",
                    stage_two_invalid_assembly,
                ],
                [
                    "-f",
                    "bin",
                    invalid_assembly,
                    "-o",
                    stage_three_invalid_assembly,
                ],
            )
            self.assertEqual(
                invalid_asm_run.returncode, 1, invalid_asm_run.stderr
            )
            self.assertEqual(invalid_asm_run.stdout, "")
            self.assertIn(
                "unknown Cupid ASM instruction mnemonic",
                invalid_asm_run.stderr,
            )
            self.assertEqual(
                stage_two_invalid_assembly.read_bytes(), failure_sentinel
            )
            self.assertEqual(
                stage_three_invalid_assembly.read_bytes(), failure_sentinel
            )

            missing_input = root / "fixed-point-missing.bin"
            missing_dis_run, _missing_dis_stage_three = run_stage_pair(
                "cupiddis",
                [
                    "--raw",
                    "--mode",
                    "16",
                    "--base",
                    "0",
                    missing_input,
                ],
            )
            self.assertEqual(
                missing_dis_run.returncode, 1, missing_dis_run.stderr
            )
            self.assertEqual(missing_dis_run.stdout, "")
            self.assertIn("cupiddis: cannot load ", missing_dis_run.stderr)
            self.assertIn("(not_found)", missing_dis_run.stderr)

            malformed_object = root / "fixed-point-malformed.o"
            stage_two_link_failure = root / "stage-two-link-failure.elf"
            stage_three_link_failure = root / "stage-three-link-failure.elf"
            malformed_object.write_bytes(b"\x7fELF")
            malformed_dis_run, _malformed_dis_stage_three = run_stage_pair(
                "cupiddis",
                ["--all", malformed_object],
            )
            self.assertEqual(
                malformed_dis_run.returncode,
                1,
                malformed_dis_run.stderr,
            )
            self.assertIn(
                "ELF32 header is truncated", malformed_dis_run.stderr
            )
            stage_two_link_failure.write_bytes(failure_sentinel)
            stage_three_link_failure.write_bytes(failure_sentinel)
            malformed_link_run, _malformed_link_stage_three = run_stage_pair(
                "cupidld",
                [
                    "-m",
                    "elf_i386",
                    "--text-address",
                    "0x00600000",
                    "--entry",
                    "_start",
                    "-o",
                    stage_two_link_failure,
                    malformed_object,
                ],
                [
                    "-m",
                    "elf_i386",
                    "--text-address",
                    "0x00600000",
                    "--entry",
                    "_start",
                    "-o",
                    stage_three_link_failure,
                    malformed_object,
                ],
            )
            self.assertEqual(
                malformed_link_run.returncode,
                1,
                malformed_link_run.stderr,
            )
            self.assertIn(
                "ELF32 header is truncated", malformed_link_run.stderr
            )
            self.assertEqual(
                stage_two_link_failure.read_bytes(), failure_sentinel
            )
            self.assertEqual(
                stage_three_link_failure.read_bytes(), failure_sentinel
            )

            stage_two_obj_failure = root / "stage-two-obj-failure.o"
            stage_three_obj_failure = root / "stage-three-obj-failure.o"
            stage_two_obj_failure.write_bytes(failure_sentinel)
            stage_three_obj_failure.write_bytes(failure_sentinel)
            missing_obj_run, _missing_obj_stage_three = run_stage_pair(
                "cupidobj",
                [
                    "wrap",
                    missing_input,
                    "--stem",
                    "missing",
                    "-o",
                    stage_two_obj_failure,
                ],
                [
                    "wrap",
                    missing_input,
                    "--stem",
                    "missing",
                    "-o",
                    stage_three_obj_failure,
                ],
            )
            self.assertEqual(
                missing_obj_run.returncode, 1, missing_obj_run.stderr
            )
            self.assertEqual(missing_obj_run.stdout, "")
            self.assertIn("cupidobj: cannot load ", missing_obj_run.stderr)
            self.assertIn("(not_found)", missing_obj_run.stderr)
            self.assertEqual(
                stage_two_obj_failure.read_bytes(), failure_sentinel
            )
            self.assertEqual(
                stage_three_obj_failure.read_bytes(), failure_sentinel
            )

    def test_cupid_built_runtime_handles_includes_mode_maps_and_missing_files(
        self,
    ):
        linked = self.build_cupid_tools()
        self.assertEqual(linked.returncode, 0, linked.stderr)
        with tempfile.TemporaryDirectory(
            prefix=".cupid-built-runtime-", dir=REPO_ROOT
        ) as temp:
            root = Path(temp)
            include_source = root / "include-main.asm"
            include_part = root / "include-part.asm"
            hosted_include = root / "hosted-include.o"
            cupid_include = root / "cupid-include.o"
            include_source.write_text(
                "BITS 32\n"
                '%include "include-part.asm"\n'
                "global included_value\n"
                "section .text\n"
                "included_value:\n"
                "    mov eax, INCLUDED_VALUE\n"
                "    ret\n",
                encoding="utf-8",
            )
            include_part.write_text(
                "%define INCLUDED_VALUE 0x12345678\n",
                encoding="utf-8",
            )
            hosted_assembly = subprocess.run(
                [
                    str(self.hosted_cupidasm_path),
                    "-f",
                    "elf32",
                    str(include_source),
                    "-o",
                    str(hosted_include),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            cupid_assembly = self.run_cupid_linux_tool(
                self.cupid_cupidasm_path,
                ["-f", "elf32", include_source, "-o", cupid_include],
            )
            self.assertEqual(hosted_assembly.returncode, 0, hosted_assembly.stderr)
            self.assertEqual(
                cupid_assembly.returncode,
                hosted_assembly.returncode,
                cupid_assembly.stderr,
            )
            self.assertEqual(cupid_assembly.stdout, hosted_assembly.stdout)
            self.assertEqual(cupid_assembly.stderr, hosted_assembly.stderr)
            self.assertEqual(
                cupid_include.read_bytes(),
                hosted_include.read_bytes(),
            )

            mixed = root / "mixed-mode.bin"
            mixed.write_bytes(
                bytes(
                    [
                        0xB8,
                        0x34,
                        0x12,
                        0xB8,
                        0x78,
                        0x56,
                        0x34,
                        0x12,
                        0xB8,
                        0xCD,
                        0xAB,
                        0xC3,
                    ]
                )
            )
            arguments = [
                "--raw",
                "--mode=16",
                "--mode-at=3:32",
                "--mode-at=8:16",
                "--base=0x7c00",
                mixed,
            ]
            hosted_report = subprocess.run(
                [str(self.hosted_cupiddis_path), *[str(arg) for arg in arguments]],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            cupid_report = self.run_cupid_linux_tool(
                self.cupid_cupiddis_path,
                arguments,
            )
            self.assertEqual(hosted_report.returncode, 0, hosted_report.stderr)
            self.assertEqual(
                cupid_report.returncode,
                hosted_report.returncode,
                cupid_report.stderr,
            )
            self.assertEqual(cupid_report.stdout, hosted_report.stdout)
            self.assertEqual(cupid_report.stderr, hosted_report.stderr)
            self.assertIn("mov ax, 0x1234", cupid_report.stdout)
            self.assertIn("mov eax, 0x12345678", cupid_report.stdout)
            self.assertIn("mov ax, 0xABCD", cupid_report.stdout)

        with tempfile.TemporaryDirectory(
            prefix=".cupid-built-missing-", dir=REPO_ROOT
        ) as temp:
            relative_root = Path(temp).relative_to(REPO_ROOT)
            missing = (
                relative_root / "source-that-does-not-exist.asm"
            ).as_posix()
            hosted_output = (relative_root / "hosted.bin").as_posix()
            cupid_output = (relative_root / "cupid.bin").as_posix()
            self.assertFalse((REPO_ROOT / missing).exists())
            hosted_missing = subprocess.run(
                [
                    str(self.hosted_cupidasm_path),
                    "-f",
                    "bin",
                    missing,
                    "-o",
                    hosted_output,
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            cupid_missing = self.run_cupid_linux_tool(
                self.cupid_cupidasm_path,
                [
                    "-f",
                    "bin",
                    missing,
                    "-o",
                    cupid_output,
                ],
            )
            self.assertEqual(hosted_missing.returncode, 1, hosted_missing.stderr)
            self.assertEqual(
                cupid_missing.returncode,
                hosted_missing.returncode,
                cupid_missing.stderr,
            )
            self.assertEqual(cupid_missing.stdout, hosted_missing.stdout)
            self.assertEqual(cupid_missing.stderr, hosted_missing.stderr)
            self.assertFalse((REPO_ROOT / hosted_output).exists())
            self.assertFalse((REPO_ROOT / cupid_output).exists())

    def test_cupid_built_cupidasm_matches_hosted_bad_source_diagnostic(self):
        linked = self.build_cupid_tools()
        self.assertEqual(linked.returncode, 0, linked.stderr)
        with tempfile.TemporaryDirectory(
            prefix=".cupid-built-bad-source-", dir=REPO_ROOT
        ) as temp:
            root = Path(temp)
            source = root / "invalid.asm"
            hosted_output = root / "hosted.bin"
            cupid_output = root / "cupid.bin"
            source.write_text(
                "BITS 16\nthis_is_not_an_instruction ax\n",
                encoding="utf-8",
            )
            hosted = subprocess.run(
                [
                    str(self.hosted_cupidasm_path),
                    "-f",
                    "bin",
                    str(source),
                    "-o",
                    str(hosted_output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            cupid = self.run_cupid_linux_tool(
                self.cupid_cupidasm_path,
                ["-f", "bin", source, "-o", cupid_output],
            )
            self.assertEqual(hosted.returncode, 1, hosted.stderr)
            self.assertEqual(cupid.returncode, hosted.returncode, cupid.stderr)
            self.assertEqual(cupid.stdout, hosted.stdout)
            self.assertEqual(cupid.stderr, hosted.stderr)
            self.assertFalse(cupid_output.exists())

    def test_cupid_built_cupidld_matches_hosted_malformed_object_failure(self):
        linked = self.build_cupid_tools()
        self.assertEqual(linked.returncode, 0, linked.stderr)
        with tempfile.TemporaryDirectory(
            prefix=".cupid-built-bad-object-", dir=REPO_ROOT
        ) as temp:
            root = Path(temp)
            malformed = root / "malformed.o"
            hosted_output = root / "hosted.elf"
            cupid_output = root / "cupid.elf"
            malformed.write_bytes(b"\x7fELF")
            hosted_output.write_bytes(b"sentinel")
            cupid_output.write_bytes(b"sentinel")
            hosted = subprocess.run(
                [
                    str(self.hosted_cupidld_path),
                    "-m",
                    "elf_i386",
                    "--text-address",
                    "0x00600000",
                    "--entry",
                    "_start",
                    "-o",
                    str(hosted_output),
                    str(malformed),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            cupid = self.run_cupid_linux_tool(
                self.cupid_cupidld_path,
                [
                    "-m",
                    "elf_i386",
                    "--text-address",
                    "0x00600000",
                    "--entry",
                    "_start",
                    "-o",
                    cupid_output,
                    malformed,
                ],
            )
            self.assertEqual(hosted.returncode, 1, hosted.stderr)
            self.assertEqual(cupid.returncode, hosted.returncode, cupid.stderr)
            self.assertEqual(cupid.stdout, hosted.stdout)
            self.assertIn("ELF32 header is truncated", hosted.stderr)
            self.assertIn("ELF32 header is truncated", cupid.stderr)
            self.assertEqual(
                [
                    line.split(": error ", 1)[1]
                    for line in cupid.stderr.splitlines()
                    if ": error " in line
                ],
                [
                    line.split(": error ", 1)[1]
                    for line in hosted.stderr.splitlines()
                    if ": error " in line
                ],
            )
            self.assertEqual(cupid_output.read_bytes(), b"sentinel")

    def test_i386_linux_host_profile_rejects_missing_or_wrong_abi(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "self-host-hosted-profile-errors",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout, "self-host-hosted-profile-errors: ok\n"
        )


if __name__ == "__main__":
    unittest.main()
