## @file
#  @brief Check artifact validation, manifests, and publication ordering.
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from unittest import mock
from tests.build_graph_audit_fixtures import (
    ACTIVE_BUILD_MANIFEST,
    AUDIT_TOOL,
    LINUX_BOOTSTRAP_SEED_INPUTS,
    REPO_ROOT,
    WINDOWS_EXECUTION_SEED_INPUTS,
    WINDOWS_PRODUCTION_SEED_INPUTS,
    _load_audit_module,
    _write,
)


class BuildPublicationCases:
    def test_make_code_validation_failure_preserves_kernel(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            kernel = root / "kernel" / "kernel.bin"
            kernel.parent.mkdir(parents=True)
            kernel.write_bytes(b"last-known-good kernel")
            for path in (
                root / "kernel" / "kernel.o",
                root / "kernel" / "cpu" / "ksyms_data.o",
                root / "kernel" / "kernel.elf.pass1",
                root / "kernel" / "kernel.elf",
            ):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"fixture")
            (root / "seed.json").write_text("{}\n", encoding="utf-8")
            (root / "code-inputs.txt").write_text(
                "kernel/kernel.o\n"
                "kernel/cpu/ksyms_data.o\n"
                "kernel/kernel.elf.pass1\n"
                "kernel/kernel.elf\n",
                encoding="utf-8",
                newline="\n",
            )
            _write(
                root / "fake_cupidbuild.py",
                """
                import sys
                from pathlib import Path

                Path("calls.log").write_text("flatten-kernel\\n", encoding="utf-8")
                Path("arguments.log").write_text(
                    "\\n".join(sys.argv[1:]) + "\\n", encoding="utf-8"
                )
                sys.exit(7)
                """,
            )
            python = Path(sys.executable).resolve().as_posix()
            _write(
                root / "Makefile",
                f"""
                PYTHON := "{python}"
                CUPIDBUILD := $(PYTHON) fake_cupidbuild.py
                PRODUCTION_SEED_MANIFEST := seed.json
                KERNEL := kernel/kernel.bin
                CUPIDDIS_PRODUCTION_INPUT_MANIFEST := code-inputs.txt
                CUPIDDIS_PRODUCTION_INPUTS := kernel/kernel.o \\
                    kernel/cpu/ksyms_data.o \\
                    kernel/kernel.elf.pass1 \\
                    kernel/kernel.elf

                .PHONY: all FORCE
                .PRECIOUS: $(KERNEL)
                all: $(KERNEL)
                FORCE:

                $(KERNEL): kernel/kernel.elf $(CUPIDDIS_PRODUCTION_INPUTS) \\
                    $(CUPIDDIS_PRODUCTION_INPUT_MANIFEST) fake_cupidbuild.py \\
                    seed.json FORCE
                \t$(CUPIDBUILD) flatten-kernel \\
                \t\t--seed-manifest $(PRODUCTION_SEED_MANIFEST) --root "$(CURDIR)" \\
                \t\t--input-manifest $(CUPIDDIS_PRODUCTION_INPUT_MANIFEST) \
                \t\t--output $(KERNEL)
                """,
            )

            result = subprocess.run(
                [make, "--no-print-directory", "all"],
                cwd=root,
                text=True,
                capture_output=True,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(kernel.read_bytes(), b"last-known-good kernel")
            self.assertEqual(
                (root / "calls.log").read_text(encoding="utf-8"),
                "flatten-kernel\n",
            )
            arguments = (
                (root / "arguments.log")
                .read_text(encoding="utf-8")
                .splitlines()
            )
            self.assertEqual(
                arguments,
                [
                    "flatten-kernel",
                    "--seed-manifest",
                    "seed.json",
                    "--root",
                    root.as_posix(),
                    "--input-manifest",
                    "code-inputs.txt",
                    "--output",
                    "kernel/kernel.bin",
                ],
            )
            self.assertLess(len(" ".join(arguments)), 8191)

    def test_make_size_failure_preserves_image_and_skips_publication(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        source = (REPO_ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn("all: $(OS_IMAGE)\n", source)
        self.assertIn(
            "$(OS_IMAGE): verify-artifact-sizes $(BOOTLOADER) $(KERNEL) \\\n",
            source,
        )
        self.assertNotIn(
            "all: $(OS_IMAGE) verify-artifact-sizes",
            source,
        )

        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            image = root / "cupidos.img"
            image.write_bytes(b"last-known-good image")
            for relative in ("boot.bin", "kernel.bin", "policy.json"):
                (root / relative).write_bytes(b"fixture")
            _write(
                root / "verify.py",
                """
                import sys
                from pathlib import Path

                Path("calls.log").write_text("verify\\n", encoding="utf-8")
                sys.exit(7)
                """,
            )
            _write(
                root / "publish.py",
                """
                from pathlib import Path

                with Path("calls.log").open("a", encoding="utf-8") as log:
                    log.write("publish\\n")
                Path("cupidos.img").write_bytes(b"replaced")
                """,
            )
            python = Path(sys.executable).resolve().as_posix()
            _write(
                root / "Makefile",
                f"""
                PYTHON := "{python}"
                OS_IMAGE := cupidos.img
                ARTIFACT_SIZE_OUTPUTS := boot.bin kernel.bin

                .PHONY: all verify-artifact-sizes
                all: $(OS_IMAGE)

                verify-artifact-sizes: $(ARTIFACT_SIZE_OUTPUTS) verify.py policy.json
                \t$(PYTHON) verify.py

                $(OS_IMAGE): verify-artifact-sizes boot.bin kernel.bin publish.py
                \t$(PYTHON) publish.py
                """,
            )

            result = subprocess.run(
                [make, "--no-print-directory", "all"],
                cwd=root,
                text=True,
                capture_output=True,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(image.read_bytes(), b"last-known-good image")
            self.assertEqual(
                (root / "calls.log").read_text(encoding="utf-8"),
                "verify\n",
            )

    def test_root_build_uses_the_checked_seed_tool_trust_unit(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        module = _load_audit_module()
        commands = module._read_evaluated_make_variables(
            REPO_ROOT,
            make,
            (
                "CUPIDASM",
                "CUPIDOBJ",
                "CUPIDLD",
                "CUPIDDIS",
                "PYTHON",
                "BOOTSTRAP_SEED_MANIFEST",
                "PRODUCTION_SEED_MANIFEST",
                "KERNEL",
                "CUPIDDIS_PRODUCTION_INPUT_MANIFEST",
                "CUPIDDIS_PRODUCTION_INPUTS",
            ),
        )
        for variable, tool_name in (
            ("CUPIDASM", "cupidasm"),
            ("CUPIDDIS", "cupiddis"),
        ):
            with self.subTest(command=variable):
                command = " ".join(commands[variable].split())
                self.assertIn(
                    "tools/bootstrap_toolchain.py run",
                    command,
                )
                self.assertIn(f"--tool {tool_name} --", command)

        cupidobj_command = " ".join(commands["CUPIDOBJ"].split())
        self.assertNotIn("tools/bootstrap_toolchain.py", cupidobj_command)
        self.assertRegex(
            cupidobj_command,
            r"bootstrap/seeds/i386-(?:linux|windows)/cupidbuild\.(?:elf|exe) "
            r"run --seed-manifest bootstrap/seeds/i386-(?:linux|windows)/"
            r"manifest\.json --seed-release bootstrap/seeds/release\.json --root .+ --tool cupidobj --$",
        )
        makefile_source = (REPO_ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn("override CUPIDOBJ :=", makefile_source)
        self.assertIn("override CUPIDOBJ_INPUTS :=", makefile_source)
        self.assertNotIn("CUPIDOBJ ?=", makefile_source)

        cupidld_command = " ".join(commands["CUPIDLD"].split())
        self.assertNotIn("tools/bootstrap_toolchain.py", cupidld_command)
        self.assertRegex(
            cupidld_command,
            r"bootstrap/seeds/i386-(?:linux|windows)/cupidbuild\.(?:elf|exe) "
            r"run --seed-manifest bootstrap/seeds/i386-(?:linux|windows)/"
            r"manifest\.json --seed-release bootstrap/seeds/release\.json --root .+ --tool cupidld --$",
        )
        self.assertIn("override CUPIDLD :=", makefile_source)
        self.assertIn("override CUPIDLD_INPUTS :=", makefile_source)
        self.assertNotIn("CUPIDLD ?=", makefile_source)

        poisoned = module._read_evaluated_make_variables(
            REPO_ROOT,
            make,
            ("CUPIDOBJ", "CUPIDOBJ_INPUTS"),
            make_variables=(
                "CUPIDOBJ=python poison-cupidobj.py",
                "CUPIDOBJ_INPUTS=poison-seed.bin",
            ),
        )
        self.assertEqual(
            " ".join(poisoned["CUPIDOBJ"].split()),
            cupidobj_command,
        )
        self.assertNotIn("poison-seed.bin", poisoned["CUPIDOBJ_INPUTS"])
        self.assertIn("Makefile", poisoned["CUPIDOBJ_INPUTS"].split())
        self.assertTrue(
            set(WINDOWS_PRODUCTION_SEED_INPUTS).issubset(
                set(poisoned["CUPIDOBJ_INPUTS"].split())
            )
        )

        poisoned_linker = module._read_evaluated_make_variables(
            REPO_ROOT,
            make,
            ("CUPIDLD", "CUPIDLD_INPUTS"),
            make_variables=(
                "CUPIDLD=python poison-cupidld.py",
                "CUPIDLD_INPUTS=poison-link-seed.bin",
            ),
        )
        self.assertEqual(
            " ".join(poisoned_linker["CUPIDLD"].split()),
            cupidld_command,
        )
        self.assertNotIn(
            "poison-link-seed.bin",
            poisoned_linker["CUPIDLD_INPUTS"],
        )
        self.assertIn("Makefile", poisoned_linker["CUPIDLD_INPUTS"].split())
        self.assertTrue(
            set(WINDOWS_PRODUCTION_SEED_INPUTS).issubset(
                set(poisoned_linker["CUPIDLD_INPUTS"].split())
            )
        )

        with tempfile.TemporaryDirectory() as td:
            output = Path(td) / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(REPO_ROOT),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            audit = json.loads(output.read_text(encoding="utf-8"))

        c_ownership = audit["contracts"]["c_source_ownership"]
        self.assertEqual(c_ownership["tracked_c_sources"], 17)
        self.assertEqual(c_ownership["active_tracked_c_sources"], 0)
        self.assertEqual(c_ownership["cupidc_owned_tracked_c_sources"], 0)
        self.assertEqual(c_ownership["unreachable_tracked_c_sources"], 17)
        self.assertEqual(c_ownership["active"], [])
        self.assertEqual(
            {
                source["path"]: source["classification"]
                for source in c_ownership["unreachable"]
            },
            {
                "bin/cupidc.c": "historical_copy",
                "bin/cupidc_lex.c": "historical_copy",
                "bin/cupidc_parse.c": "historical_copy",
                "bin/fat16.c": "historical_copy",
                "bin/fat16_vfs.c": "historical_copy",
                "bin/kernel.c": "historical_copy",
                "bin/terminal_app.c": "historical_copy",
                "kernel/core/scheduler.c": "superseded",
                "kernel/gui/notepad.c": "superseded",
                "kernel/gui/terminal_ansi.c": "superseded",
                "kernel/lang/cupidc_runtime.c": "dormant",
                "tests/kernel_exec_contract.c": "host_fixture",
                "tests/kernel_process_contract.c": "host_fixture",
                "tests/usb_interrupt_ownership_contract.c": "host_fixture",
                "tests/usb_msc_lifetime_contract.c": "host_fixture",
                "tests/usb_reconciliation_runtime.c": "host_fixture",
                "toolchain/tests/elf32_oracle.c": "host_oracle",
            },
        )

        root_transforms = audit["build"]["transforms"]
        root_tools = {
            tool
            for transform in root_transforms
            for tool in transform["tools"]
        }
        self.assertNotIn("host_c_compiler", root_tools)
        for output in ("kernel/kernel.elf.pass1", "kernel/kernel.elf"):
            transform = next(
                item for item in root_transforms if item["output"] == output
            )
            self.assertEqual(
                transform["tools"],
                ["cupid_linker", "cupid_builder"],
            )
        jpeg_transform = next(
            item
            for item in root_transforms
            if item["output"] == "file_example_JPG_1MB.jpg.o"
        )
        self.assertEqual(
            jpeg_transform["tools"],
            ["cupid_builder", "cupid_object"],
        )
        self.assertEqual(
            jpeg_transform["operation"],
            "wrap_binary_as_elf32_relocatable",
        )
        self.assertIn(
            "bootstrap/seeds/i386-windows/cupidbuild.exe",
            jpeg_transform["inputs"],
        )
        self.assertFalse(
            any(
                "kernel/gui/terminal_ansi.c" in transform["inputs"]
                for transform in root_transforms
            )
        )
        terminal_ansi = {
            source["path"]: source for source in audit["unreachable_sources"]
        }["kernel/gui/terminal_ansi.c"]
        self.assertEqual(
            {
                key: terminal_ansi[key]
                for key in (
                    "path",
                    "language",
                    "classification",
                    "reason",
                    "relations",
                )
            },
            {
                "path": "kernel/gui/terminal_ansi.c",
                "language": "c",
                "classification": "superseded",
                "reason": "replaced by the recorded active implementation",
                "relations": [
                    {
                        "kind": "superseded_by",
                        "path": "kernel/gui/ansi.cc",
                        "evidence": "audited project source relationship",
                    }
                ],
            },
        )
        makefile = (REPO_ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertNotRegex(
            makefile,
            r"(?m)^kernel/gui/terminal_ansi\.o\s*:",
        )

        seed_inputs = {
            "Makefile",
            "tools/bootstrap_toolchain.py",
            *WINDOWS_PRODUCTION_SEED_INPUTS,
        }
        object_outputs = {
            transform["output"]
            for transform in audit["build"]["transforms"]
            if transform["output"].endswith(".o")
        }
        self.assertEqual(len(object_outputs), 431)
        validated_code_inputs = object_outputs | {
            "kernel/kernel.elf.pass1",
            "kernel/kernel.elf",
        }
        self.assertEqual(len(validated_code_inputs), 433)
        declared_code_inputs = commands["CUPIDDIS_PRODUCTION_INPUTS"].split()
        input_manifest = commands["CUPIDDIS_PRODUCTION_INPUT_MANIFEST"]
        manifest_code_inputs = (
            (REPO_ROOT / input_manifest)
            .read_text(encoding="utf-8")
            .splitlines()
        )
        self.assertEqual(len(declared_code_inputs), 433)
        self.assertEqual(len(set(declared_code_inputs)), 433)
        self.assertEqual(manifest_code_inputs, declared_code_inputs)
        self.assertEqual(len(manifest_code_inputs), 433)
        self.assertEqual(len(set(manifest_code_inputs)), 433)
        self.assertEqual(set(declared_code_inputs), validated_code_inputs)
        validation_command = " ".join(
            (
                "$(PRODUCTION_SEED_DIRECTORY)cupidbuild.$(PRODUCTION_SEED_SUFFIX)",
                "flatten-kernel",
                "--seed-manifest",
                commands["PRODUCTION_SEED_MANIFEST"],
                "--root",
                ".",
                "--input-manifest",
                input_manifest,
                "--output",
                commands["KERNEL"],
            )
        )
        self.assertLess(len(validation_command), 8191)
        self.assertNotIn("kernel/core/kernel.o", validation_command)
        kernel_binary_transform = next(
            transform
            for transform in audit["build"]["transforms"]
            if transform["output"] == "kernel/kernel.bin"
        )
        self.assertEqual(
            kernel_binary_transform["tools"],
            ["cupid_builder", "cupid_disassembler", "cupid_object"],
        )
        self.assertEqual(
            kernel_binary_transform["operation"], "extract_raw_binary"
        )
        self.assertEqual(
            set(kernel_binary_transform["inputs"]),
            validated_code_inputs
            | {"Makefile", *WINDOWS_PRODUCTION_SEED_INPUTS, input_manifest},
        )
        self.assertEqual(
            set(kernel_binary_transform["inputs"]) & validated_code_inputs,
            set(manifest_code_inputs),
        )
        self.assertEqual(
            kernel_binary_transform["recipe"],
            [
                "$(PRODUCTION_SEED_DIRECTORY)cupidbuild."
                "$(PRODUCTION_SEED_SUFFIX) flatten-kernel \\",
                '--seed-manifest $(PRODUCTION_SEED_MANIFEST) --root "$(CURDIR)" \\',
                '--seed-release $(PRODUCTION_SEED_RELEASE) \\',
                "--input-manifest $(CUPIDDIS_PRODUCTION_INPUT_MANIFEST) \\",
                "--output $(KERNEL)",
            ],
        )
        bootloader_transform = next(
            transform
            for transform in audit["build"]["transforms"]
            if transform["output"] == "boot/boot.bin"
        )
        self.assertEqual(
            bootloader_transform["tools"],
            ["cupid_assembler", "cupid_disassembler", "cupid_builder"],
        )
        self.assertEqual(
            bootloader_transform["operation"],
            "assemble_flat_binary",
        )
        self.assertEqual(
            set(bootloader_transform["inputs"]),
            {
                "Makefile",
                "boot/boot.asm",
                *WINDOWS_PRODUCTION_SEED_INPUTS,
            },
        )
        self.assertEqual(
            bootloader_transform["recipe"],
            [
                "$(PRODUCTION_SEED_DIRECTORY)cupidbuild."
                "$(PRODUCTION_SEED_SUFFIX) assemble-bootloader \\",
                '--seed-manifest $(PRODUCTION_SEED_MANIFEST) --root "$(CURDIR)" \\',
                '--seed-release $(PRODUCTION_SEED_RELEASE) \\',
                "--source $< --output $@",
            ],
        )
        trampoline_transform = next(
            transform
            for transform in audit["build"]["transforms"]
            if transform["output"] == "kernel/smp_trampoline.bin"
        )
        self.assertEqual(
            trampoline_transform["tools"],
            ["cupid_assembler", "cupid_disassembler", "cupid_builder"],
        )
        self.assertEqual(
            trampoline_transform["operation"],
            "assemble_flat_binary",
        )
        self.assertEqual(
            set(trampoline_transform["inputs"]),
            {
                "Makefile",
                "kernel/smp/smp_trampoline.S",
                *WINDOWS_PRODUCTION_SEED_INPUTS,
            },
        )
        self.assertEqual(
            trampoline_transform["recipe"],
            [
                "$(PRODUCTION_SEED_DIRECTORY)cupidbuild."
                "$(PRODUCTION_SEED_SUFFIX) assemble-smp-trampoline \\",
                '--seed-manifest $(PRODUCTION_SEED_MANIFEST) --root "$(CURDIR)" \\',
                '--seed-release $(PRODUCTION_SEED_RELEASE) \\',
                "--source $< --output $@",
            ],
        )
        for object_output, object_source in (
            ("kernel/cpu/isr.o", "kernel/cpu/isr.asm"),
            (
                "kernel/core/context_switch.o",
                "kernel/core/context_switch.asm",
            ),
        ):
            with self.subTest(object_output=object_output):
                object_transform = next(
                    transform
                    for transform in audit["build"]["transforms"]
                    if transform["output"] == object_output
                )
                self.assertEqual(
                    object_transform["tools"],
                    [
                        "cupid_assembler",
                        "cupid_disassembler",
                        "cupid_builder",
                    ],
                )
                self.assertEqual(
                    object_transform["operation"],
                    "assemble_elf32_relocatable",
                )
                self.assertEqual(
                    set(object_transform["inputs"]),
                    {
                        "Makefile",
                        *WINDOWS_PRODUCTION_SEED_INPUTS,
                        object_source,
                    },
                )
                self.assertEqual(
                    object_transform["recipe"],
                    [
                        "$(PRODUCTION_SEED_DIRECTORY)cupidbuild."
                        "$(PRODUCTION_SEED_SUFFIX) "
                        "assemble-cupidasm-object \\",
                        "--seed-manifest $(PRODUCTION_SEED_MANIFEST) "
                        '--root "$(CURDIR)" \\',
                        '--seed-release $(PRODUCTION_SEED_RELEASE) \\',
                        "--source $< --output $@",
                    ],
                )
        system_image_transform = next(
            transform
            for transform in audit["build"]["transforms"]
            if transform["output"] == "cupidos.img"
        )
        self.assertEqual(
            system_image_transform["tools"],
            ["cupid_object", "host_python"],
        )
        self.assertEqual(
            system_image_transform["operation"],
            "package_disk_image",
        )
        self.assertEqual(
            set(system_image_transform["inputs"]),
            {
                "Makefile",
                "boot/boot.bin",
                *WINDOWS_PRODUCTION_SEED_INPUTS,
                "kernel/kernel.bin",
                "test_iso/hello.iso",
                "tools/bootstrap_toolchain.py",
                "tools/hostbuild.py",
                "tools/hostbuild_assets.py",
                "tools/hostbuild_code_paths.py",
                "tools/hostbuild_disk.py",
                "tools/hostbuild_iso.py",
                "verify-artifact-sizes",
            },
        )
        profile_manifest_transform = next(
            transform
            for transform in audit["build"]["transforms"]
            if transform["output"] == "build/bootstrap/doom-cupidc-inputs.json"
        )
        self.assertEqual(
            profile_manifest_transform["operation"],
            "generate_profile_manifest",
        )
        self.assertEqual(
            profile_manifest_transform["tools"],
            ["cupid_builder", "cupid_object"],
        )
        expected_counts = {
            "cupid_assembler": 5,
            "cupid_builder": 444,
            "cupid_object": 194,
            "cupid_linker": 2,
            "cupid_disassembler": 7,
        }
        for tool, expected_count in expected_counts.items():
            transforms = [
                transform
                for transform in audit["build"]["transforms"]
                if tool in transform["tools"]
            ]
            with self.subTest(tool=tool):
                self.assertEqual(len(transforms), expected_count)
                for transform in transforms:
                    expected_python_count = (
                        0 if "cupid_builder" in transform["tools"] else 1
                    )
                    self.assertEqual(
                        transform["tools"].count("host_python"),
                        expected_python_count,
                        transform["output"],
                    )
                    expected_trust_inputs = (
                        {
                            "Makefile",
                            *WINDOWS_PRODUCTION_SEED_INPUTS,
                        }
                        if "cupid_builder" in transform["tools"]
                        else seed_inputs
                    )
                    self.assertTrue(
                        expected_trust_inputs.issubset(transform["inputs"]),
                        transform["output"],
                    )
        cupid_tools = {
            "cupid_c_compiler",
            "cupid_assembler",
            "cupid_builder",
            "cupid_object",
            "cupid_linker",
            "cupid_disassembler",
        }
        cupid_owned = [
            transform
            for transform in audit["build"]["transforms"]
            if cupid_tools.intersection(transform["tools"])
        ]
        python_only = sorted(
            (
                transform
                for transform in audit["build"]["transforms"]
                if not cupid_tools.intersection(transform["tools"])
            ),
            key=lambda transform: transform["output"],
        )
        self.assertEqual(
            [transform["output"] for transform in python_only],
            [],
        )
        size_contract = next(
            transform
            for transform in audit["build"]["transforms"]
            if transform["output"] == "verify-artifact-sizes"
        )
        self.assertEqual(
            size_contract["operation"],
            "verify_artifact_size_policy",
        )
        self.assertEqual(
            size_contract["tools"],
            ["cupid_builder"],
        )
        self.assertEqual(
            size_contract["recipe"],
            [
                "$(PRODUCTION_SEED_DIRECTORY)cupidbuild.$(PRODUCTION_SEED_SUFFIX) verify-artifact-sizes \\",
                "--root \"$(CURDIR)\" --policy $(ARTIFACT_SIZE_POLICY) \\",
                "--seed-manifest $(BOOTSTRAP_SEED_MANIFEST) \\",
                "--checked-manifest $(BOOTSTRAP_WINDOWS_SEED_MANIFEST) \\",
                "--execution-manifest $(PRODUCTION_SEED_MANIFEST)",
            ],
        )
        self.assertTrue(set(WINDOWS_PRODUCTION_SEED_INPUTS).issubset(size_contract["inputs"]))
        self.assertIn("Makefile", size_contract["inputs"])
        self.assertNotIn("tools/bootstrap_toolchain.py", size_contract["inputs"])
        self.assertTrue(
            {
                *LINUX_BOOTSTRAP_SEED_INPUTS,
                "bootstrap/artifact-size-policy.json",
                "kernel/kernel.bin",
                "kernel/kernel.elf",
                "kernel/kernel.elf.pass1",
                "bootstrap/seeds/release.json",
            }.issubset(size_contract["inputs"])
        )
        self.assertEqual(len(cupid_owned), 445)
        self.assertFalse(
            any(
                transform["operation"] == "recursive_make"
                for transform in audit["build"]["transforms"]
            )
        )

    def test_artifact_size_contract_input_closure_fails_closed(self):
        module = _load_audit_module()
        audit = json.loads(ACTIVE_BUILD_MANIFEST.read_text(encoding="utf-8"))
        cases = (
            ("release record removed", "bootstrap/seeds/release.json", None),
            ("policy removed", "bootstrap/artifact-size-policy.json", None),
            ("kernel removed", "kernel/kernel.bin", None),
            ("Linux payload removed", "bootstrap/seeds/i386-linux/cupidbuild.elf", None),
            ("release record duplicated", None, "bootstrap/seeds/release.json"),
            ("Python runner added", None, "tools/artifact_size_contract.py"),
            ("Linux manifest removed", "bootstrap/seeds/i386-linux/manifest.json", None),
            ("Windows manifest removed", "bootstrap/seeds/i386-windows/manifest.json", None),
            (
                "Windows payload removed",
                "bootstrap/seeds/i386-windows/cupidbuild.exe",
                None,
            ),
            (
                "hosted header added",
                None,
                "toolchain/hosted/i386-linux/include/extra.h",
            ),
        )
        for name, removed, added in cases:
            with self.subTest(name=name):
                changed = json.loads(json.dumps(audit))
                transform = next(
                    item
                    for item in changed["build"]["transforms"]
                    if item["output"] == "verify-artifact-sizes"
                )
                if removed is not None:
                    transform["inputs"].remove(removed)
                if added is not None:
                    transform["inputs"].append(added)
                with self.assertRaisesRegex(
                    module.AuditError,
                    r"artifact-size contract transform differs",
                ):
                    module._c_preprocessor_active_cases_manifest(changed)

    def test_artifact_size_contract_recipe_fails_closed(self):
        module = _load_audit_module()
        audit = json.loads(ACTIVE_BUILD_MANIFEST.read_text(encoding="utf-8"))
        original = next(item for item in audit["build"]["transforms"]
                        if item["output"] == "verify-artifact-sizes")
        command = original["recipe"]
        cases = (
            ["unexpected artifact-size recipe"],
            [line.replace("$(BOOTSTRAP_SEED_MANIFEST)",
                          "$(PRODUCTION_SEED_MANIFEST)") for line in command],
            [line.replace("$(ARTIFACT_SIZE_POLICY)", "unchecked.json")
             for line in command],
            [line.replace("$(PRODUCTION_SEED_DIRECTORY)", "unchecked/")
             for line in command],
            [*command, "$(PYTHON) tools/artifact_size_contract.py verify"],
            [line for line in command if "--checked-manifest" not in line],
            [line for line in command if "--execution-manifest" not in line],
            [line.replace("--execution-manifest $(PRODUCTION_SEED_MANIFEST)",
                          "--execution-manifest $(BOOTSTRAP_SEED_MANIFEST)")
             for line in command],
            [line.replace("--checked-manifest $(BOOTSTRAP_WINDOWS_SEED_MANIFEST)",
                          "--checked-manifest $(PRODUCTION_SEED_MANIFEST)")
             for line in command],
        )
        for recipe in cases:
            with self.subTest(recipe=recipe):
                changed = json.loads(json.dumps(audit))
                transform = next(item for item in changed["build"]["transforms"]
                                 if item["output"] == "verify-artifact-sizes")
                transform["recipe"] = recipe
                with self.assertRaisesRegex(
                    module.AuditError, r"artifact-size contract transform differs"
                ):
                    module._c_preprocessor_active_cases_manifest(changed)

    def test_artifact_size_native_tool_ownership_fails_closed(self):
        module = _load_audit_module()
        audit = json.loads(ACTIVE_BUILD_MANIFEST.read_text(encoding="utf-8"))
        for tools in ([], ["host_python"], ["cupid_builder", "host_python"],
                      ["cupid_builder", "cupid_c_compiler"],
                      ["cupid_builder", "cupid_builder"]):
            with self.subTest(tools=tools):
                changed = json.loads(json.dumps(audit))
                transform = next(item for item in changed["build"]["transforms"]
                                 if item["output"] == "verify-artifact-sizes")
                transform["tools"] = tools
                with self.assertRaisesRegex(
                    module.AuditError, r"artifact-size contract transform differs"
                ):
                    module._c_preprocessor_active_cases_manifest(changed)

    def test_artifact_size_contract_waits_for_iso_publication(self):
        audit = json.loads(ACTIVE_BUILD_MANIFEST.read_text(encoding="utf-8"))
        transform = next(
            item
            for item in audit["build"]["transforms"]
            if item["output"] == "verify-artifact-sizes"
        )
        self.assertEqual(
            transform.get("order_only_inputs"), ["test_iso/hello.iso"]
        )
        self.assertNotIn("test_iso/hello.iso", transform["inputs"])

    def test_artifact_size_contract_iso_order_fails_closed(self):
        module = _load_audit_module()
        audit = json.loads(ACTIVE_BUILD_MANIFEST.read_text(encoding="utf-8"))
        for order_only_inputs in (
            None,
            [],
            ["test_iso/fixtures/big.bin"],
            ["test_iso/hello.iso", "test_iso/hello.iso"],
        ):
            with self.subTest(order_only_inputs=order_only_inputs):
                changed = json.loads(json.dumps(audit))
                transform = next(
                    item
                    for item in changed["build"]["transforms"]
                    if item["output"] == "verify-artifact-sizes"
                )
                if order_only_inputs is None:
                    transform.pop("order_only_inputs", None)
                else:
                    transform["order_only_inputs"] = order_only_inputs
                with self.assertRaisesRegex(
                    module.AuditError,
                    r"artifact-size contract transform differs",
                ):
                    module._c_preprocessor_active_cases_manifest(changed)

    def test_toolchain_manifest_contract_input_closure_fails_closed(self):
        module = _load_audit_module()
        audit = json.loads(ACTIVE_BUILD_MANIFEST.read_text(encoding="utf-8"))
        cases = (
            (
                "publication input removed",
                "toolchain/tests/x86_catalogue_contract.inc",
                None,
            ),
            (
                "heap runtime header removed",
                "toolchain/hosted/i386-linux/include/cupid_runtime.h",
                None,
            ),
            (
                "bootstrap source added",
                None,
                "toolchain/new-bootstrap-source.cc",
            ),
            (
                "build Makefile removed",
                "toolchain/Makefile",
                None,
            ),
            (
                "candidate tool removed",
                "toolchain/build/cupidc-contracts/cupidc-cupidbuild.elf",
                None,
            ),
        )
        for name, removed, added in cases:
            with self.subTest(name=name):
                changed = json.loads(json.dumps(audit))
                build = next(
                    item
                    for item in changed["supplemental_builds"]
                    if item["directory"] == "toolchain"
                )
                transform = next(
                    item
                    for item in build["transforms"]
                    if item["output"] == "toolchain/all"
                )
                if removed is not None:
                    transform["inputs"].remove(removed)
                if added is not None:
                    transform["inputs"].append(added)
                with self.assertRaisesRegex(
                    module.AuditError,
                    r"Toolchain manifest contract transform differs",
                ):
                    module._c_preprocessor_active_cases_manifest(changed)

    def test_toolchain_manifest_closure_follows_both_host_branches(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        module = _load_audit_module()
        variables = (
            "TOOLCHAIN_MANIFEST_EXECUTION_INPUTS",
            "TOOLCHAIN_MANIFEST_PUBLICATION_SEED_INPUTS",
            "TOOLCHAIN_MANIFEST_CONTRACT_BUILD_INPUTS",
            "TOOLCHAIN_MANIFEST_PUBLICATION_INPUTS",
            "TOOLCHAIN_MANIFEST_BOOTSTRAP_INPUTS",
        )

        def root_paths(value):
            return {
                path[3:] if path.startswith("../") else f"toolchain/{path}"
                for path in value.split()
            }

        for host, expected_seed in (
            ("Windows_NT", set(WINDOWS_EXECUTION_SEED_INPUTS)),
            ("Linux", set(LINUX_BOOTSTRAP_SEED_INPUTS)),
        ):
            with self.subTest(host=host):
                with mock.patch.object(
                    module, "CANONICAL_MAKE_VARIABLES", (f"OS={host}",)
                ):
                    values = module._read_evaluated_make_variables(
                        REPO_ROOT / "toolchain", make, variables
                    )
                self.assertEqual(
                    root_paths(values["TOOLCHAIN_MANIFEST_EXECUTION_INPUTS"]),
                    expected_seed,
                )
                self.assertEqual(
                    root_paths(
                        values["TOOLCHAIN_MANIFEST_PUBLICATION_SEED_INPUTS"]
                    ),
                    set(LINUX_BOOTSTRAP_SEED_INPUTS),
                )
                self.assertEqual(
                    root_paths(
                        values["TOOLCHAIN_MANIFEST_CONTRACT_BUILD_INPUTS"]
                    ),
                    set(module.TOOLCHAIN_MANIFEST_CONTRACT_BUILD_INPUTS),
                )
                self.assertEqual(
                    root_paths(
                        values["TOOLCHAIN_MANIFEST_PUBLICATION_INPUTS"]
                    ),
                    set(module.TOOLCHAIN_MANIFEST_PUBLICATION_INPUTS),
                )
                self.assertEqual(
                    root_paths(values["TOOLCHAIN_MANIFEST_BOOTSTRAP_INPUTS"]),
                    {
                        *module.TOOLCHAIN_MANIFEST_BOOTSTRAP_INPUTS,
                        "toolchain/hosted/i386-windows/utf8_long_path_start.asm",
                    },
                )

    def test_output_source_discovery_has_locale_neutral_order(self):
        makefile = (REPO_ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertNotIn("iso_fixture_walk", makefile)
        self.assertNotIn(
            "$(file <$(ISO_FIXTURE_MANIFEST))",
            makefile,
        )
        fixture_assignment = re.search(
            r"(?ms)^ISO_FIXTURE_RELATIVE :="
            r"(?P<body>.*?)"
            r"(?=^TEST_ISO_FIXTURES :=)",
            makefile,
        )
        self.assertIsNotNone(fixture_assignment)
        declared_fixtures = (
            fixture_assignment.group("body").replace("\\", " ").split()
        )
        manifest_fixtures = (
            (REPO_ROOT / "test_iso" / "fixtures.manifest")
            .read_text(encoding="ascii")
            .splitlines()
        )
        self.assertEqual(declared_fixtures, manifest_fixtures)
        self.assertTrue(
            all(
                re.fullmatch(r"[A-Za-z0-9._/-]+", path)
                for path in declared_fixtures
            )
        )
        self.assertIn(
            "--manifest $(ISO_FIXTURE_MANIFEST)",
            makefile,
        )
        self.assertIn(
            "--seed-manifest $(PRODUCTION_SEED_MANIFEST)",
            makefile,
        )
        for variable in (
            "BIN_CC_SRCS",
            "BIN_HDR_SRCS",
            "BROWSER_SUB_SRCS",
            "DOC_CTXT_SRCS",
            "HOME_BMP_SRCS",
            "HOME_PNG_SRCS",
            "HOME_JPG_SRCS",
            "HOME_JPEG_SRCS",
            "DEMO_ASM_SRCS",
            "GOD_DD_SRCS",
            "FONT_TTF_SRCS",
            "WAD_SRCS",
            "DOOM_CUPIDC_HEADERS",
            "DOOM_SRC",
            "TEST_ISO_FIXTURES",
        ):
            self.assertRegex(
                makefile,
                rf"(?m)^{variable}\s*:=\s*\$\(sort\b",
                f"{variable} must sort discovered paths before use",
            )
