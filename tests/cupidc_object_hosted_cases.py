## @file
#  @brief Check the i386 ABI and behavior of Cupid-built tools.
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from tests.cupidc_object_fixtures import REPO_ROOT, TOOLCHAIN_ROOT

class HostedObjectCases:
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

    def test_static_long_double_arithmetic_emits_exact_x87_payloads(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "static-long-double-arithmetic",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout, "static-long-double-arithmetic: ok\n"
        )

    def test_floating_arithmetic_spills_each_x87_result(self):
        result = subprocess.run(
            [str(self.contract_path), "floating-arithmetic", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "floating-arithmetic: ok\n")

    def test_floating_comparisons_preserve_c11_unordered_semantics(self):
        result = subprocess.run(
            [str(self.contract_path), "floating-comparisons", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "floating-comparisons: ok\n")

    def test_floating_width_conversions_emit_deterministic_x87_objects(self):
        result = subprocess.run(
            [str(self.contract_path), "floating-conversions", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "floating-conversions: ok\n")

    def test_floating_truth_emits_deterministic_zero_comparisons(self):
        result = subprocess.run(
            [str(self.contract_path), "floating-truth", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "floating-truth: ok\n")

    def test_floating_updates_emit_exact_stores_and_postfix_results(self):
        result = subprocess.run(
            [str(self.contract_path), "floating-updates", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "floating-updates: ok\n")

    def test_static_floating_arithmetic_emits_exact_constant_data(self):
        result = subprocess.run(
            [str(self.contract_path), "floating-scalars", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "floating-scalars: ok\n")

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

    def test_doom_implicit_functions_emit_one_stable_external_identity(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "doom-implicit-functions",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "doom-implicit-functions: ok\n")

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

    def test_cupidobj_self_host_frontier_is_deterministic(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "self-host-frontier-cupidobj",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout, "self-host-frontier-cupidobj: ok\n"
        )

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
            self.cupid_cupidbuild_path,
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

    def test_cupid_built_runtime_covers_errors_and_long_double_abi(self):
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
            self.assertEqual(
                run.stdout,
                "printf-ok 7\nputs-ok\nfputs-ok\nruntime-ok\n",
            )
            self.assertEqual(run.stderr, "")
            self.assertEqual(
                output.read_text(encoding="utf-8"),
                "ok -12 0000002A\n",
            )

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
            long_double_usual = root / "long-double-usual.c"
            wide_integer_floating = root / "wide-integer-floating.c"
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
            hosted_long_double_object = root / "hosted-long-double.o"
            cupid_long_double_object = root / "cupid-long-double.o"
            hosted_wide_floating_object = root / "hosted-wide-floating.o"
            cupid_wide_floating_object = root / "cupid-wide-floating.o"
            hosted_recovery = root / "hosted-recovery.o"
            cupid_recovery = root / "cupid-recovery.o"
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
                "}\n"
                "long double compiled_long_double(void) {\n"
                "  return 1.0000000000000000001L;\n"
                "}\n"
                "long double compiled_long_double_maximum(void) {\n"
                "  return 18446744073709551615e0L;\n"
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
            long_double_usual.write_text(
                "long double add_integer(long double left, int right) {\n"
                "  return left + right;\n"
                "}\n"
                "int compare_wide(long double left, long long right) {\n"
                "  return left < right;\n"
                "}\n"
                "long double choose_unsigned_wide(\n"
                "    int condition, unsigned long long left,\n"
                "    long double right) {\n"
                "  return condition ? left : right;\n"
                "}\n",
                encoding="utf-8",
            )
            wide_integer_floating.write_text(
                "float cast_signed(long long value) {\n"
                "  return (float)value;\n"
                "}\n"
                "double add_unsigned(unsigned long long left, double right) {\n"
                "  return left + right;\n"
                "}\n"
                "int compare_signed(float left, long long right) {\n"
                "  return left >= right;\n"
                "}\n"
                "double choose_unsigned(\n"
                "    int condition, unsigned long long left, double right) {\n"
                "  return condition ? left : right;\n"
                "}\n",
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

            hosted_long_double = subprocess.run(
                [
                    str(self.hosted_cupidc_path),
                    "--root",
                    str(root),
                    "-c",
                    "/long-double-usual.c",
                    "-o",
                    "/hosted-long-double.o",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )
            cupid_long_double = self.run_cupid_linux_tool(
                self.cupid_cupidc_path,
                [
                    "--root",
                    root,
                    "-c",
                    "/long-double-usual.c",
                    "-o",
                    "/cupid-long-double.o",
                ],
                timeout=60,
            )
            self.assertEqual(
                hosted_long_double.returncode, 0, hosted_long_double.stderr
            )
            self.assertEqual(
                cupid_long_double.returncode,
                hosted_long_double.returncode,
                cupid_long_double.stderr,
            )
            self.assertEqual(
                cupid_long_double.stdout, hosted_long_double.stdout
            )
            self.assertEqual(
                cupid_long_double.stderr, hosted_long_double.stderr
            )
            self.assertEqual(
                cupid_long_double_object.read_bytes(),
                hosted_long_double_object.read_bytes(),
            )

            hosted_wide_floating = subprocess.run(
                [
                    str(self.hosted_cupidc_path),
                    "--root",
                    str(root),
                    "-c",
                    "/wide-integer-floating.c",
                    "-o",
                    "/hosted-wide-floating.o",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )
            cupid_wide_floating = self.run_cupid_linux_tool(
                self.cupid_cupidc_path,
                [
                    "--root",
                    root,
                    "-c",
                    "/wide-integer-floating.c",
                    "-o",
                    "/cupid-wide-floating.o",
                ],
                timeout=60,
            )
            self.assertEqual(
                hosted_wide_floating.returncode,
                0,
                hosted_wide_floating.stderr,
            )
            self.assertEqual(
                cupid_wide_floating.returncode,
                hosted_wide_floating.returncode,
                cupid_wide_floating.stderr,
            )
            self.assertEqual(
                cupid_wide_floating.stdout, hosted_wide_floating.stdout
            )
            self.assertEqual(
                cupid_wide_floating.stderr, hosted_wide_floating.stderr
            )
            self.assertEqual(
                cupid_wide_floating_object.read_bytes(),
                hosted_wide_floating_object.read_bytes(),
            )

            hosted_recovered = subprocess.run(
                [
                    str(self.hosted_cupidc_path),
                    "--root",
                    str(root),
                    "-c",
                    "/source.c",
                    "-I",
                    "/include",
                    "-DCOMMAND_VALUE=7",
                    "-o",
                    "/hosted-recovery.o",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )
            cupid_recovered = self.run_cupid_linux_tool(
                self.cupid_cupidc_path,
                [
                    "--root",
                    root,
                    "-c",
                    "/source.c",
                    "-I",
                    "/include",
                    "-DCOMMAND_VALUE=7",
                    "-o",
                    "/cupid-recovery.o",
                ],
                timeout=60,
            )
            self.assertEqual(
                hosted_recovered.returncode, 0, hosted_recovered.stderr
            )
            self.assertEqual(
                cupid_recovered.returncode,
                hosted_recovered.returncode,
                cupid_recovered.stderr,
            )
            self.assertEqual(cupid_recovered.stdout, hosted_recovered.stdout)
            self.assertEqual(cupid_recovered.stderr, hosted_recovered.stderr)
            self.assertEqual(
                cupid_recovery.read_bytes(), hosted_recovery.read_bytes()
            )

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
