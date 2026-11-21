## @file
#  @brief Check the hosted Cupid toolchain fixed point and collect object tests.
import os
import subprocess
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from tests.cupidc_object_assembly_cases import ObjectAssemblyCases
from tests.cupidc_object_emission_cases import ObjectEmissionCases
from tests.cupidc_object_fixtures import ObjectToolSetup, REPO_ROOT, TOOLCHAIN_ROOT
from tests.cupidc_object_hosted_cases import HostedObjectCases
from tests.cupidc_object_profile_cases import ObjectProfileCases
from tools.bootstrap_toolchain import _validate_static_i386_pe32


CUPIDC_FIXED_POINT_SOURCES = (
    ("ctool", "/toolchain/ctool.cc", False),
    ("ctool_host", "/toolchain/ctool_host.cc", False),
    ("cupidc_pp", "/toolchain/cupidc_pp.cc", False),
    ("cupidc_type", "/toolchain/cupidc_type.cc", False),
    ("cupidc_frontend", "/toolchain/cupidc_frontend.cc", False),
    ("cupidc_ir", "/toolchain/cupidc_ir.cc", False),
    ("cupidc_emit", "/toolchain/cupidc_emit.cc", False),
    ("elf32", "/toolchain/elf32.cc", False),
    ("x86", "/toolchain/x86.cc", False),
    ("cupidc_main", "/toolchain/cupidc_main.cc", False),
    ("runtime", "/toolchain/hosted/i386-linux/runtime.cc", True),
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
    ("runtime", "/toolchain/hosted/i386-linux/runtime.cc", True),
    ("ctool", "/toolchain/ctool.cc", False),
    ("ctool_host", "/toolchain/ctool_host.cc", False),
    ("elf32", "/toolchain/elf32.cc", False),
    ("x86", "/toolchain/x86.cc", False),
    ("cupidasm", "/toolchain/cupidasm.cc", False),
    ("cupidasm_main", "/toolchain/cupidasm_main.cc", False),
    ("cupiddis", "/toolchain/cupiddis.cc", False),
    ("cupiddis_main", "/toolchain/cupiddis_main.cc", False),
    ("cupidobj", "/toolchain/cupidobj.cc", False),
    ("cupidobj_main", "/toolchain/cupidobj_main.cc", False),
    ("iso_fixture_bundle", "/toolchain/iso_fixture_bundle.cc", False),
    ("cupidld", "/toolchain/cupidld.cc", False),
    ("cupidld_main", "/toolchain/cupidld_main.cc", False),
    ("cupidc_pp", "/toolchain/cupidc_pp.cc", False),
    ("cupidc_type", "/toolchain/cupidc_type.cc", False),
    ("cupidc_frontend", "/toolchain/cupidc_frontend.cc", False),
    ("cupidc_ir", "/toolchain/cupidc_ir.cc", False),
    ("cupidc_emit", "/toolchain/cupidc_emit.cc", False),
    ("cupidc_main", "/toolchain/cupidc_main.cc", False),
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
            "iso_fixture_bundle",
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

class ToolchainCupidCObjectContractTests(
    ObjectEmissionCases,
    ObjectAssemblyCases,
    HostedObjectCases,
    ObjectProfileCases,
    ObjectToolSetup,
    unittest.TestCase,
):
    def test_cupid_built_toolchain_reaches_a_full_static_fixed_point(self):
        linked = self.build_cupid_tools()
        self.assertEqual(linked.returncode, 0, linked.stderr)
        self.assertEqual(len(CUPIDC_FIXED_POINT_SOURCES), 11)
        self.assertEqual(len(CUPID_TOOLCHAIN_FIXED_POINT_SOURCES), 20)
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
            "[--include-angle PATH] [-include FILE] [-D NAME[=VALUE]] "
            "[-U NAME] [--cupid] [--gnu] [--doom-compat] [--freestanding] "
            "[--root NATIVE_ROOT] [--source-bundle CUPSRC1_FILE]\n"
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
                            producers["cupidc"], arguments, timeout=900
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

            stage_two_pe32 = root / "stage-two-fixed.exe"
            stage_three_pe32 = root / "stage-three-fixed.exe"
            stage_two_pe32_run, _stage_three_pe32_run = run_stage_pair(
                "cupidld",
                [
                    "-m",
                    "i386pe",
                    "--text-address",
                    "0x00401000",
                    "--entry",
                    "_start",
                    "-o",
                    stage_two_pe32,
                    stage_two_link_object,
                ],
                [
                    "-m",
                    "i386pe",
                    "--text-address",
                    "0x00401000",
                    "--entry",
                    "_start",
                    "-o",
                    stage_three_pe32,
                    stage_three_link_object,
                ],
            )
            self.assertEqual(
                stage_two_pe32_run.returncode,
                0,
                stage_two_pe32_run.stderr,
            )
            self.assertEqual(stage_two_pe32_run.stdout, "")
            self.assertEqual(stage_two_pe32_run.stderr, "")
            self.assertEqual(
                stage_three_pe32.read_bytes(),
                stage_two_pe32.read_bytes(),
            )
            _validate_static_i386_pe32(stage_two_pe32, 0x00401000)

            windows_start_source = (
                REPO_ROOT / "toolchain/hosted/i386-windows/start.asm"
            )
            windows_contract_source = (
                REPO_ROOT
                / "toolchain/tests/hosted_i386_windows_contract.cc"
            )
            local_windows_contract = root / "windows-contract.cc"
            local_windows_contract.write_bytes(
                windows_contract_source.read_bytes()
            )
            stage_two_windows_start = root / "stage-two-windows-start.o"
            stage_three_windows_start = root / "stage-three-windows-start.o"
            windows_assembly_run, _stage_three_windows_assembly = (
                run_stage_pair(
                    "cupidasm",
                    [
                        "-f",
                        "elf32",
                        windows_start_source,
                        "-o",
                        stage_two_windows_start,
                    ],
                    [
                        "-f",
                        "elf32",
                        windows_start_source,
                        "-o",
                        stage_three_windows_start,
                    ],
                )
            )
            self.assertEqual(
                windows_assembly_run.returncode,
                0,
                windows_assembly_run.stderr,
            )
            self.assertEqual(windows_assembly_run.stdout, "")
            self.assertEqual(windows_assembly_run.stderr, "")
            self.assertEqual(
                stage_three_windows_start.read_bytes(),
                stage_two_windows_start.read_bytes(),
            )

            stage_two_windows_contract = (
                root / "stage-two-windows-contract.o"
            )
            stage_three_windows_contract = (
                root / "stage-three-windows-contract.o"
            )
            windows_compile_arguments = [
                "--root",
                root,
                "--freestanding",
                "-c",
                "/windows-contract.cc",
            ]
            stage_two_windows_compile = self.run_cupid_linux_tool(
                stage_two_tools["cupidc"],
                [
                    *windows_compile_arguments,
                    "-o",
                    "/stage-two-windows-contract.o",
                ],
                timeout=60,
            )
            stage_three_windows_compile = self.run_cupid_linux_tool(
                stage_three_tools["cupidc"],
                [
                    *windows_compile_arguments,
                    "-o",
                    "/stage-three-windows-contract.o",
                ],
                timeout=60,
            )
            self.assertEqual(
                stage_two_windows_compile.returncode,
                0,
                stage_two_windows_compile.stderr,
            )
            self.assertEqual(stage_two_windows_compile.stdout, "")
            self.assertEqual(stage_two_windows_compile.stderr, "")
            self.assertEqual(
                stage_three_windows_compile.returncode,
                stage_two_windows_compile.returncode,
                stage_three_windows_compile.stderr,
            )
            self.assertEqual(stage_three_windows_compile.stdout, "")
            self.assertEqual(stage_three_windows_compile.stderr, "")
            self.assertEqual(
                stage_three_windows_contract.read_bytes(),
                stage_two_windows_contract.read_bytes(),
            )

            imports = (
                "__imp_WriteFile=KERNEL32.dll:WriteFile",
                "__imp_ExitProcess=KERNEL32.dll:ExitProcess",
                "__imp_GetStdHandle=KERNEL32.dll:GetStdHandle",
            )
            stage_two_windows_image = root / "stage-two-windows.exe"
            stage_three_windows_image = root / "stage-three-windows.exe"
            windows_link_run, _stage_three_windows_link = run_stage_pair(
                "cupidld",
                [
                    "-m",
                    "i386pe",
                    "--text-address",
                    "0x00401000",
                    "--entry",
                    "_start",
                    "--import",
                    imports[0],
                    "--import",
                    imports[1],
                    "--import",
                    imports[2],
                    "-o",
                    stage_two_windows_image,
                    stage_two_windows_start,
                    stage_two_windows_contract,
                ],
                [
                    "-m",
                    "i386pe",
                    "--text-address",
                    "0x00401000",
                    "--entry",
                    "_start",
                    "--import",
                    imports[2],
                    "--import",
                    imports[0],
                    "--import",
                    imports[1],
                    "-o",
                    stage_three_windows_image,
                    stage_three_windows_start,
                    stage_three_windows_contract,
                ],
            )
            self.assertEqual(
                windows_link_run.returncode,
                0,
                windows_link_run.stderr,
            )
            self.assertEqual(windows_link_run.stdout, "")
            self.assertEqual(windows_link_run.stderr, "")
            self.assertEqual(
                stage_three_windows_image.read_bytes(),
                stage_two_windows_image.read_bytes(),
            )
            _validate_static_i386_pe32(
                stage_two_windows_image,
                0x00401000,
                (
                    (
                        "KERNEL32.dll",
                        ("ExitProcess", "GetStdHandle", "WriteFile"),
                    ),
                ),
            )
            if os.name == "nt":
                native_windows_run = subprocess.run(
                    [str(stage_two_windows_image)],
                    cwd=REPO_ROOT,
                    capture_output=True,
                    timeout=10,
                )
                self.assertEqual(native_windows_run.returncode, 37)
                self.assertEqual(
                    native_windows_run.stdout,
                    b"Cupid-built Windows runtime: ok\n",
                )
                self.assertEqual(native_windows_run.stderr, b"")

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

            symbol_text = root / "fixed-point-kernel.symbols"
            stage_two_ksyms = root / "stage-two-ksyms.cc"
            stage_three_ksyms = root / "stage-three-ksyms.cc"
            symbol_text.write_text(
                "00002000 T second\n"
                "00001000 T first\n"
                "00001000 T duplicate\n"
                "         U unresolved\n"
                "00003000 D data_only\n",
                encoding="ascii",
            )
            expected_ksyms = (
                "/* Auto-generated by tools/hostbuild.py -- do not edit. */\n"
                '#include "ksyms.h"\n\n'
                "/* i386 words preserve the blob bytes with fewer initializers. */\n"
                "const unsigned int\n"
                '__attribute__((section(".ksyms"), used, aligned(4)))\n'
                "ksym_blob[] = {\n"
                "  0x4d59534bu, 0x00000002u, 0x00000020u, 0x0000002du, "
                "0x00001000u, 0x00000000u, 0x00002000u, 0x00000006u,\n"
                "  0x73726966u, 0x65730074u, 0x646e6f63u, 0x00000000u,\n"
                "};\n\n"
                "const unsigned int ksym_blob_size = 45u;\n"
            ).encode("ascii")
            stage_two_ksyms_run, _stage_three_ksyms_run = run_stage_pair(
                "cupidobj",
                ["ksyms-source", symbol_text, "-o", stage_two_ksyms],
                ["ksyms-source", symbol_text, "-o", stage_three_ksyms],
            )
            self.assertEqual(
                stage_two_ksyms_run.returncode,
                0,
                stage_two_ksyms_run.stderr,
            )
            self.assertEqual(stage_two_ksyms_run.stdout, "")
            self.assertEqual(stage_two_ksyms_run.stderr, "")
            self.assertEqual(stage_two_ksyms.read_bytes(), expected_ksyms)
            self.assertEqual(stage_three_ksyms.read_bytes(), expected_ksyms)

            invalid_symbols = root / "fixed-point-invalid.symbols"
            stage_two_invalid_ksyms = root / "stage-two-invalid-ksyms.cc"
            stage_three_invalid_ksyms = root / "stage-three-invalid-ksyms.cc"
            invalid_symbols.write_text(
                "100000000 T too_wide\n", encoding="ascii"
            )
            stage_two_invalid_ksyms.write_bytes(failure_sentinel)
            stage_three_invalid_ksyms.write_bytes(failure_sentinel)
            invalid_ksyms_run, _invalid_ksyms_stage_three = run_stage_pair(
                "cupidobj",
                [
                    "ksyms-source",
                    invalid_symbols,
                    "-o",
                    stage_two_invalid_ksyms,
                ],
                [
                    "ksyms-source",
                    invalid_symbols,
                    "-o",
                    stage_three_invalid_ksyms,
                ],
            )
            self.assertEqual(
                invalid_ksyms_run.returncode,
                1,
                invalid_ksyms_run.stderr,
            )
            self.assertEqual(invalid_ksyms_run.stdout, "")
            self.assertIn(
                "address is outside i386", invalid_ksyms_run.stderr
            )
            self.assertEqual(
                stage_two_invalid_ksyms.read_bytes(), failure_sentinel
            )
            self.assertEqual(
                stage_three_invalid_ksyms.read_bytes(), failure_sentinel
            )

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

            stage_two_pe32_failure = root / "stage-two-invalid-pe32.exe"
            stage_three_pe32_failure = root / "stage-three-invalid-pe32.exe"
            stage_two_pe32_failure.write_bytes(failure_sentinel)
            stage_three_pe32_failure.write_bytes(failure_sentinel)
            invalid_pe32_run, _invalid_pe32_stage_three = run_stage_pair(
                "cupidld",
                [
                    "-m",
                    "i386pe",
                    "--text-address",
                    "0x00402000",
                    "--entry",
                    "_start",
                    "-o",
                    stage_two_pe32_failure,
                    stage_two_link_object,
                ],
                [
                    "-m",
                    "i386pe",
                    "--text-address",
                    "0x00402000",
                    "--entry",
                    "_start",
                    "-o",
                    stage_three_pe32_failure,
                    stage_three_link_object,
                ],
            )
            self.assertEqual(invalid_pe32_run.returncode, 1)
            self.assertEqual(invalid_pe32_run.stdout, "")
            self.assertIn(
                "CupidLD PE32 requires text address 0x00401000",
                invalid_pe32_run.stderr,
            )
            self.assertEqual(
                stage_two_pe32_failure.read_bytes(), failure_sentinel
            )
            self.assertEqual(
                stage_three_pe32_failure.read_bytes(), failure_sentinel
            )

            invalid_import_source = root / "fixed-point-invalid-import.asm"
            invalid_import_source.write_text(
                "BITS 32\n"
                "extern __imp_ExitProcess\n"
                "global _start\n"
                "section .text\n"
                "_start:\n"
                "    call __imp_ExitProcess\n"
                "    ret\n",
                encoding="utf-8",
                newline="\n",
            )
            stage_two_invalid_import_object = (
                root / "stage-two-invalid-import.o"
            )
            stage_three_invalid_import_object = (
                root / "stage-three-invalid-import.o"
            )
            invalid_import_assembly_run, _stage_three_invalid_assembly = (
                run_stage_pair(
                    "cupidasm",
                    [
                        "-f",
                        "elf32",
                        invalid_import_source,
                        "-o",
                        stage_two_invalid_import_object,
                    ],
                    [
                        "-f",
                        "elf32",
                        invalid_import_source,
                        "-o",
                        stage_three_invalid_import_object,
                    ],
                )
            )
            self.assertEqual(
                invalid_import_assembly_run.returncode,
                0,
                invalid_import_assembly_run.stderr,
            )
            self.assertEqual(
                stage_three_invalid_import_object.read_bytes(),
                stage_two_invalid_import_object.read_bytes(),
            )
            invalid_import_object = root / "invalid-import.o"
            invalid_import_object.write_bytes(
                stage_two_invalid_import_object.read_bytes()
            )
            stage_two_invalid_import_image = (
                root / "stage-two-invalid-import.exe"
            )
            stage_three_invalid_import_image = (
                root / "stage-three-invalid-import.exe"
            )
            stage_two_invalid_import_image.write_bytes(failure_sentinel)
            stage_three_invalid_import_image.write_bytes(failure_sentinel)
            invalid_import_run, _stage_three_invalid_import = run_stage_pair(
                "cupidld",
                [
                    "-m",
                    "i386pe",
                    "--text-address",
                    "0x00401000",
                    "--entry",
                    "_start",
                    "--import",
                    "__imp_ExitProcess=KERNEL32.dll:ExitProcess",
                    "-o",
                    stage_two_invalid_import_image,
                    invalid_import_object,
                ],
                [
                    "-m",
                    "i386pe",
                    "--text-address",
                    "0x00401000",
                    "--entry",
                    "_start",
                    "--import",
                    "__imp_ExitProcess=KERNEL32.dll:ExitProcess",
                    "-o",
                    stage_three_invalid_import_image,
                    invalid_import_object,
                ],
            )
            self.assertEqual(invalid_import_run.returncode, 1)
            self.assertEqual(invalid_import_run.stdout, "")
            self.assertIn(
                "IAT symbols require an absolute zero-addend relocation",
                invalid_import_run.stderr,
            )
            self.assertEqual(
                stage_two_invalid_import_image.read_bytes(),
                failure_sentinel,
            )
            self.assertEqual(
                stage_three_invalid_import_image.read_bytes(),
                failure_sentinel,
            )

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



if __name__ == "__main__":
    unittest.main()
