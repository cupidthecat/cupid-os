import importlib.util
import json
import re
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path
from unittest import mock


REPO_ROOT = Path(__file__).resolve().parents[1]
AUDIT_TOOL = REPO_ROOT / "tools" / "build_graph_audit.py"
ACTIVE_BUILD_MANIFEST = (
    REPO_ROOT / "docs" / "bootstrap" / "audits" / "active-build.json"
)
CONDITIONAL_MANIFEST = (
    REPO_ROOT / "toolchain" / "tests" / "cupidc_pp_conditional_cases.inc"
)
ACTIVE_CASE_MANIFEST = (
    REPO_ROOT / "toolchain" / "tests" / "cupidc_pp_active_cases.inc"
)
CUPIDC_PP_CONTRACT = REPO_ROOT / "toolchain" / "tests" / "cupidc_pp_contract.cc"
TOOLCHAIN_MAKEFILE = REPO_ROOT / "toolchain" / "Makefile"


def _write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(textwrap.dedent(content).lstrip(), encoding="utf-8")


def _json_list_recipe(values):
    python = Path(sys.executable).resolve().as_posix()
    return (
        f'\t@"{python}" -c "import json; '
        f"print(json.dumps({list(values)!r}))\""
    )


def _conditional_manifest_records():
    records = {}
    pattern = re.compile(
        r'^CUPIDC_PP_CONDITIONAL_CASE\("([^"]+)", '
        r"([0-9]+)u, ([0-9]+)u, ([01])\)$"
    )
    for line in CONDITIONAL_MANIFEST.read_text(encoding="utf-8").splitlines():
        match = pattern.fullmatch(line)
        if match is None:
            continue
        expression, if_count, elif_count, expected = match.groups()
        if expression in records:
            raise AssertionError(f"duplicate conditional manifest: {expression}")
        records[expression] = (
            int(if_count),
            int(elif_count),
            int(expected),
        )
    return records


def _load_audit_module():
    spec = importlib.util.spec_from_file_location(
        "_cupid_build_graph_audit_manifest_test", AUDIT_TOOL
    )
    if spec is None or spec.loader is None:
        raise AssertionError("could not load build graph audit module")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    try:
        spec.loader.exec_module(module)
    finally:
        del sys.modules[spec.name]
    return module


class BuildGraphAuditCliTests(unittest.TestCase):
    def test_make_readers_force_the_canonical_windows_graph(self):
        module = _load_audit_module()
        self.assertEqual(
            module.CANONICAL_MAKE_VARIABLES,
            ("OS=Windows_NT",),
        )
        completed = [
            subprocess.CompletedProcess([], 0, stdout="database", stderr=""),
            subprocess.CompletedProcess([], 0, stdout='["item"]\n', stderr=""),
            subprocess.CompletedProcess(
                [],
                0,
                stdout=(
                    "__CUPID_AUDIT_VALUE_0 := value\n"
                    "__CUPID_AUDIT_ORIGIN_0 := file\n"
                ),
                stderr="",
            ),
        ]
        with mock.patch.object(
            module.subprocess, "run", side_effect=completed
        ) as run:
            module._run_make_database(REPO_ROOT, "make", "all")
            module._read_make_json_list(REPO_ROOT, "make", "list")
            module._read_evaluated_make_variables(
                REPO_ROOT, "make", ("PROFILE",)
            )

        self.assertEqual(run.call_count, 3)
        for call in run.call_args_list:
            self.assertEqual(
                call.args[0][1 : 1 + len(module.CANONICAL_MAKE_VARIABLES)],
                list(module.CANONICAL_MAKE_VARIABLES),
            )
            self.assertEqual(call.kwargs["env"]["LC_ALL"], "C")
        self.assertIn(
            module._audit_python_make_variable(),
            run.call_args_list[1].args[0],
        )

    def test_make_database_uses_the_canonical_windows_graph(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        module = _load_audit_module()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "Makefile").write_text(
                "ifeq ($(OS),Windows_NT)\n"
                "all: windows\n"
                "else\n"
                "all: linux\n"
                "endif\n"
                "windows:\n"
                "linux:\n",
                encoding="utf-8",
            )
            rules = module._parse_make_rules(
                module._run_make_database(root, make, "all")
            )
            reachable = module._reachable_rules(rules, "all")

        self.assertIn("windows", reachable)
        self.assertNotIn("linux", reachable)

    def test_native_user_tools_are_an_explicit_recursive_build(self):
        module = _load_audit_module()
        transform = {
            "output": "user/native-user-tools",
            "inputs": [],
            "tools": ["make"],
            "operation": "recursive_make",
            "recipe": [
                "$(MAKE) -C ../toolchain "
                "build/cupidc.exe build/cupidld.exe"
            ],
        }
        module._validate_native_user_tools_transform("user", transform)

        changed = {
            **transform,
            "recipe": ["$(MAKE) -C ../toolchain build/cupidc.exe"],
        }
        with self.assertRaisesRegex(
            module.AuditError,
            "native Windows user-tool prerequisite",
        ):
            module._validate_native_user_tools_transform("user", changed)

    def test_user_syscall_abi_verifier_is_a_first_class_transform(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        module = _load_audit_module()
        rules = module._parse_make_rules(
            module._run_make_database(
                REPO_ROOT / "user", make, "test-syscall-abi"
            )
        )
        transforms = module._build_transforms(
            "user",
            module._reachable_rules(rules, "test-syscall-abi"),
            rules,
        )
        transform = next(
            item
            for item in transforms
            if item["output"] == "user/test-syscall-abi"
        )
        toolchain_rules = module._parse_make_rules(
            module._run_make_database(
                REPO_ROOT / "toolchain",
                make,
                "build/cupidc-contracts/manifest.json",
            )
        )
        publication_transform = module._build_transforms(
            "toolchain",
            {"build/cupidc-contracts/manifest.json"},
            toolchain_rules,
        )[0]
        input_variables = module._read_evaluated_make_variables(
            REPO_ROOT / "user",
            make,
            (
                "USER_SYSCALL_ABI_PUBLICATION_INPUTS",
                "USER_SYSCALL_ABI_BOOTSTRAP_SOURCE_INPUTS",
                "CHECKED_SEED_INPUTS",
            ),
        )

        def repo_inputs(variable):
            return tuple(
                sorted(
                    module._prefix_repo_path("user", path)
                    for path in input_variables[variable].split()
                )
            )

        self.assertEqual(transform["output"], "user/test-syscall-abi")
        self.assertEqual(
            transform["tools"], ["cupid_c_contract", "host_python"]
        )
        self.assertEqual(
            transform["operation"], "verify_user_syscall_abi"
        )
        self.assertEqual(
            len(module.USER_SYSCALL_ABI_PUBLICATION_INPUTS), 65
        )
        self.assertEqual(
            len(module.USER_SYSCALL_ABI_BOOTSTRAP_SOURCE_INPUTS), 50
        )
        self.assertEqual(
            len(module.USER_SYSCALL_ABI_CHECKED_SEED_INPUTS), 6
        )
        self.assertEqual(
            repo_inputs("USER_SYSCALL_ABI_PUBLICATION_INPUTS"),
            module.USER_SYSCALL_ABI_PUBLICATION_INPUTS,
        )
        self.assertEqual(
            repo_inputs("USER_SYSCALL_ABI_BOOTSTRAP_SOURCE_INPUTS"),
            module.USER_SYSCALL_ABI_BOOTSTRAP_SOURCE_INPUTS,
        )
        self.assertEqual(
            repo_inputs("CHECKED_SEED_INPUTS"),
            tuple(sorted(module.USER_SYSCALL_ABI_CHECKED_SEED_INPUTS)),
        )
        self.assertEqual(len(module.USER_SYSCALL_ABI_AUDIT_INPUTS), 92)
        self.assertEqual(
            module.USER_SYSCALL_ABI_AUDIT_INPUTS,
            tuple(
                sorted(
                    set(module.USER_SYSCALL_ABI_PUBLICATION_INPUTS)
                    | set(module.USER_SYSCALL_ABI_BOOTSTRAP_SOURCE_INPUTS)
                    | set(module.USER_SYSCALL_ABI_CHECKED_SEED_INPUTS)
                )
            ),
        )
        self.assertEqual(
            transform["inputs"],
            [*module.USER_SYSCALL_ABI_AUDIT_INPUTS, "user/Makefile"],
        )
        self.assertEqual(len(publication_transform["inputs"]), 92)
        self.assertEqual(
            set(transform["inputs"][:-1]),
            set(publication_transform["inputs"]),
        )
        module._validate_user_syscall_abi_transform("user", transform)

    def test_syscall_abi_oracle_input_does_not_relabel_contract_publication(self):
        module = _load_audit_module()
        transform = module._build_transforms(
            "toolchain",
            {"build/cupidc-contracts/manifest.json"},
            {
                "build/cupidc-contracts/manifest.json": module.MakeRule(
                    prerequisites=[
                        "../tools/cupidc_toolchain_contracts.py",
                        "../tools/user_syscall_abi.py",
                        "tests/user_syscall_abi_contract.cc",
                    ],
                    recipe=[
                        "$(PYTHON) ../tools/cupidc_toolchain_contracts.py "
                        "build --root .. --manifest seed.json "
                        "--output $(CONTRACT_DIR)"
                    ],
                )
            },
        )[0]

        self.assertEqual(transform["tools"], ["host_python"])
        self.assertEqual(transform["operation"], "host_orchestration")

    def test_user_syscall_abi_verifier_rejects_missing_live_input(self):
        module = _load_audit_module()
        transform = {
            "output": "user/test-syscall-abi",
            "inputs": [
                *module.USER_SYSCALL_ABI_AUDIT_INPUTS,
                "user/Makefile",
            ],
            "tools": ["cupid_c_contract", "host_python"],
            "operation": "verify_user_syscall_abi",
            "recipe": ["$(USER_SYSCALL_ABI)"],
        }
        missing_path = "toolchain/cupidc_emit.cc"
        changed = {
            **transform,
            "inputs": [
                path for path in transform["inputs"] if path != missing_path
            ],
        }
        with self.assertRaisesRegex(
            module.AuditError,
            r"user syscall ABI verifier inputs changed; "
            r"missing=\['toolchain/cupidc_emit\.cc'\], unexpected=\[\]",
        ):
            module._validate_user_syscall_abi_transform("user", changed)

    def test_user_syscall_abi_verifier_rejects_unexpected_live_input(self):
        module = _load_audit_module()
        transform = {
            "output": "user/test-syscall-abi",
            "inputs": [
                *module.USER_SYSCALL_ABI_AUDIT_INPUTS,
                "user/Makefile",
            ],
            "tools": ["cupid_c_contract", "host_python"],
            "operation": "verify_user_syscall_abi",
            "recipe": ["$(USER_SYSCALL_ABI)"],
        }
        unexpected_path = "user/unchecked-abi-input.h"
        changed = {
            **transform,
            "inputs": [
                *module.USER_SYSCALL_ABI_AUDIT_INPUTS,
                unexpected_path,
                "user/Makefile",
            ],
        }
        with self.assertRaisesRegex(
            module.AuditError,
            r"user syscall ABI verifier inputs changed; missing=\[\], "
            r"unexpected=\['user/unchecked-abi-input\.h'\]",
        ):
            module._validate_user_syscall_abi_transform("user", changed)

    def test_user_syscall_abi_verifier_rejects_contract_drift(self):
        module = _load_audit_module()
        transform = {
            "output": "user/test-syscall-abi",
            "inputs": [
                *module.USER_SYSCALL_ABI_AUDIT_INPUTS,
                "user/Makefile",
            ],
            "tools": ["cupid_c_contract", "host_python"],
            "operation": "verify_user_syscall_abi",
            "recipe": ["$(USER_SYSCALL_ABI)"],
        }
        changes = {
            "wrong target": {
                **transform,
                "output": "user/check-abi",
            },
            "wrong tool": {
                **transform,
                "tools": ["host_python"],
            },
            "wrong recipe": {
                **transform,
                "recipe": ["$(PYTHON) unchecked.py"],
            },
        }
        for name, changed in changes.items():
            with self.subTest(name=name), self.assertRaisesRegex(
                module.AuditError,
                "user syscall ABI verifier",
            ):
                module._validate_user_syscall_abi_transform("user", changed)

    def test_inventory_attributes_checked_seed_assembly_once(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                PYTHON = python
                CUPIDASM = $(PYTHON) tools/bootstrap_toolchain.py run \
                    --manifest seed/manifest.json --root . \
                    --tool cupidasm --

                .PHONY: all
                all: boot.bin entry.o

                boot.bin: boot.asm
                \t$(CUPIDASM) -f bin $< -o $@

                entry.o: entry.asm
                \t$(CUPIDASM) -f elf32 $< -o $@
                """,
            )
            _write(root / "boot.asm", "bits 16\norg 0x7c00\nhlt\n")
            _write(root / "entry.asm", "bits 32\nret\n")

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            audit = json.loads(output.read_text(encoding="utf-8"))
            transforms = {
                entry["output"]: entry for entry in audit["build"]["transforms"]
            }
            self.assertEqual(
                transforms["boot.bin"]["tools"],
                ["cupid_assembler", "host_python"],
            )
            self.assertEqual(
                transforms["boot.bin"]["operation"], "assemble_flat_binary"
            )
            self.assertEqual(
                transforms["entry.o"]["tools"],
                ["cupid_assembler", "host_python"],
            )
            self.assertEqual(
                transforms["entry.o"]["operation"], "assemble_elf32_relocatable"
            )
            sources = {entry["path"]: entry for entry in audit["sources"]}
            self.assertEqual(sources["boot.asm"]["runtime_owner"], "CupidASM")
            self.assertEqual(sources["entry.asm"]["runtime_owner"], "CupidASM")

    def test_inventory_maps_reachable_language_inputs_to_tool_owned_outputs(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                ASM = nasm
                LD = host-ld
                OBJCOPY = host-objcopy

                .PHONY: all
                all: kernel.elf app.o demo.o

                kernel.elf: main.o boot.bin
                \t$(LD) -o $@ $^

                main.o: main.c api.h
                \t$(CC) -c $< -o $@

                boot.bin: boot.asm
                \t$(ASM) -f bin $< -o $@

                app.o: app.cc
                \t$(OBJCOPY) -I binary -O elf32-i386 $< $@

                demo.o: demo.asm
                \t$(OBJCOPY) -I binary -O elf32-i386 $< $@
                """,
            )
            _write(
                root / "main.c",
                """
                #include "api.h"
                int main(void) { return answer(); }
                """,
            )
            _write(
                root / "api.h",
                """
                #include "types.h"
                static inline word answer(void) { return 42; }
                """,
            )
            _write(root / "types.h", "typedef int word;\n")
            _write(root / "app.cc", "U0 Main() {}\n")
            _write(root / "boot.asm", "bits 16\norg 0x7c00\nhlt\n")
            _write(root / "demo.asm", "bits 32\nmov eax, 1\nret\n")

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            audit = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(audit["schema"], "cupid.build-graph-audit.v1")
            self.assertEqual(audit["build"]["root_target"], "all")
            self.assertEqual(
                audit["provenance"]["control_files"][0]["path"], "Makefile"
            )

            sources = {entry["path"]: entry for entry in audit["sources"]}
            self.assertEqual(
                set(sources),
                {"api.h", "app.cc", "boot.asm", "demo.asm", "main.c", "types.h"},
            )
            self.assertEqual(sources["main.c"]["language"], "c")
            self.assertEqual(sources["app.cc"]["language"], "cupid_c")
            self.assertEqual(sources["boot.asm"]["language"], "assembly")
            self.assertEqual(sources["types.h"]["reachability"], "transitive_include")
            self.assertEqual(sources["app.cc"]["runtime_owner"], "CupidC")
            self.assertEqual(sources["demo.asm"]["runtime_owner"], "CupidASM")
            self.assertIsNone(sources["boot.asm"]["runtime_owner"])

            transforms = {
                entry["output"]: entry for entry in audit["build"]["transforms"]
            }
            self.assertEqual(transforms["main.o"]["tools"], ["host_c_compiler"])
            self.assertEqual(transforms["boot.bin"]["tools"], ["nasm"])
            self.assertEqual(
                transforms["boot.bin"]["operation"], "assemble_flat_binary"
            )
            self.assertEqual(
                transforms["kernel.elf"]["operation"], "link_elf32_executable"
            )
            self.assertEqual(transforms["app.o"]["tools"], ["host_object_copy"])
            self.assertEqual(transforms["demo.o"]["tools"], ["host_object_copy"])
            self.assertEqual(
                transforms["kernel.elf"]["inputs"], ["main.o", "boot.bin"]
            )
            self.assertEqual(
                audit["roadmap"]["capability_priorities"][0]["id"],
                "elf32_relocatable_interchange",
            )

    def test_inventory_attributes_transforms_to_cupid_linker_and_object(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            artifact_recipe = _json_list_recipe(["main.o"])
            _write(
                root / "Makefile",
                f"""
                .SUFFIXES:
                CC = host-cc
                PYTHON = python
                CUPIDLD = $(PYTHON) tools/bootstrap_toolchain.py run \
                    --manifest seed/manifest.json --root . \
                    --tool cupidld --
                CUPIDOBJ = $(PYTHON) tools/bootstrap_toolchain.py run \
                    --manifest seed/manifest.json --root . \
                    --tool cupidobj --

                .PHONY: all print-bootstrap-artifacts
                all: kernel.elf kernel.bin app.o

                kernel.elf: main.o link.ld
                \t$(CUPIDLD) -m elf_i386 -T link.ld -o $@ main.o

                kernel.bin: kernel.elf
                \t$(CUPIDOBJ) flat $< -o $@

                app.o: app.cc
                \t$(CUPIDOBJ) wrap-text $< -o $@

                main.o: main.c
                \t$(CC) -c $< -o $@

                print-bootstrap-artifacts:
                {artifact_recipe}
                """,
            )
            _write(root / "main.c", "int main(void) { return 0; }\n")
            _write(root / "app.cc", "U0 Main() {}\n")
            _write(
                root / "link.ld",
                "ENTRY(main)\nSECTIONS { . = 0x100000; .text : { *(.text) } }\n",
            )

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            audit = json.loads(output.read_text(encoding="utf-8"))
            transforms = {
                entry["output"]: entry for entry in audit["build"]["transforms"]
            }
            self.assertEqual(
                transforms["kernel.elf"]["tools"],
                ["cupid_linker", "host_python"],
            )
            self.assertEqual(
                transforms["kernel.elf"]["operation"], "link_elf32_executable"
            )
            self.assertEqual(
                transforms["kernel.bin"]["tools"],
                ["cupid_object", "host_python"],
            )
            self.assertEqual(
                transforms["kernel.bin"]["operation"], "extract_raw_binary"
            )
            self.assertEqual(
                transforms["app.o"]["tools"],
                ["cupid_object", "host_python"],
            )
            self.assertEqual(
                transforms["app.o"]["operation"],
                "wrap_text_as_elf32_relocatable",
            )
            self.assertEqual(
                audit["contracts"]["bootstrap_artifact_coverage"]["linked_objects"],
                1,
            )

    def test_inventory_attributes_nested_checked_seed_tools_once(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                PYTHON = python
                CUPIDDIS = $(PYTHON) tools/bootstrap_toolchain.py run \
                    --manifest seed/manifest.json --root . \
                    --tool cupiddis --

                .PHONY: all
                all: symbols.cc photo.o reader.txt big.bin cupidos.img

                symbols.cc: kernel.elf
                \t$(PYTHON) tools/hostbuild.py mksyms \
                    --seed-manifest seed/manifest.json $< $@

                photo.o: photo.jpg
                \t$(PYTHON) tools/hostbuild.py embed-jpeg \
                    --seed-manifest seed/manifest.json $< $@

                reader.txt: kernel.elf
                \t$(PYTHON) helper.py --reader $(CUPIDDIS) $< $@

                big.bin: big_pattern.asm
                \t$(PYTHON) tools/hostbuild.py gen-big \
                    --seed-manifest seed/manifest.json \
                    --source $< $@

                hello.iso: fixtures fixtures.manifest
                \t$(PYTHON) tools/hostbuild.py build-iso \
                    --seed-manifest seed/manifest.json \
                    --fixtures fixtures --manifest fixtures.manifest \
                    --out $@

                cupidos.img: boot.bin kernel.bin hello.iso
                \t$(PYTHON) tools/hostbuild.py image \
                    --seed-manifest seed/manifest.json \
                    --image $@ --bootloader boot.bin \
                    --kernel kernel.bin --hdd-mb 200 \
                    --fat-start-lba 20480 \
                    --stage hello.iso:/hello.iso
                """,
            )
            _write(root / "kernel.elf", "fixture\n")
            _write(root / "photo.jpg", "fixture\n")
            _write(root / "big_pattern.asm", "times 4096 db $\n")
            _write(root / "boot.bin", "fixture\n")
            _write(root / "kernel.bin", "fixture\n")
            _write(root / "fixtures" / "readme.txt", "fixture\n")
            _write(root / "fixtures.manifest", "readme.txt\n")

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            audit = json.loads(output.read_text(encoding="utf-8"))
            transforms = {
                entry["output"]: entry
                for entry in audit["build"]["transforms"]
            }
            self.assertEqual(
                transforms["symbols.cc"]["tools"],
                ["cupid_disassembler", "cupid_object", "host_python"],
            )
            self.assertEqual(
                transforms["symbols.cc"]["operation"],
                "generate_ksyms_source",
            )
            self.assertEqual(
                transforms["photo.o"]["tools"],
                ["cupid_object", "host_python"],
            )
            self.assertEqual(
                transforms["photo.o"]["operation"],
                "wrap_binary_as_elf32_relocatable",
            )
            self.assertEqual(
                transforms["reader.txt"]["tools"],
                ["cupid_disassembler", "host_python"],
            )
            self.assertEqual(
                transforms["big.bin"]["tools"],
                ["cupid_assembler", "host_python"],
            )
            self.assertEqual(
                transforms["big.bin"]["operation"],
                "assemble_flat_binary",
            )
            self.assertEqual(
                transforms["hello.iso"]["tools"],
                ["cupid_object", "host_python"],
            )
            self.assertEqual(
                transforms["hello.iso"]["operation"],
                "package_iso9660_image",
            )
            self.assertEqual(
                transforms["cupidos.img"]["tools"],
                ["cupid_object", "host_python"],
            )
            self.assertEqual(
                transforms["cupidos.img"]["operation"],
                "package_disk_image",
            )

    def test_make_database_does_not_execute_recursive_recipes(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                .PHONY: all
                all: main.o hosted-tool
                main.o: main.c
                \t$(CC) -c $< -o $@
                hosted-tool:
                \t$(MAKE) -C child all
                """,
            )
            _write(root / "main.c", "int main(void) { return 0; }\n")
            _write(
                root / "child" / "Makefile",
                """
                .SUFFIXES:
                CC = child-cc
                all: child.o
                child.o: child.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(root / "child" / "child.c", "int child(void) { return 1; }\n")

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            audit = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(audit["build"]["root_target"], "all")
            self.assertIn(
                "main.o",
                {entry["output"] for entry in audit["build"]["transforms"]},
            )
            self.assertEqual(
                [entry["path"] for entry in audit["sources"]], ["main.c"]
            )

    def test_inventory_reports_source_features_with_stable_evidence(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                ASM = nasm
                OBJCOPY = host-objcopy

                .PHONY: all
                all: feature.o app.o entry.o

                feature.o: feature.c
                \t$(CC) -c $< -o $@

                app.o: app.cc
                \t$(OBJCOPY) -I binary -O elf32-i386 $< $@

                entry.o: entry.asm
                \t$(ASM) -f elf32 $< -o $@
                """,
            )
            _write(
                root / "feature.c",
                """
                #define SPANNED( \\
                    left, \\
                    right) left ## \\
                    right
                #define TRACE(fmt, ...) log(fmt, __VA_ARGS__)
                #define JOIN(a, b) a ## b
                #define NAME(value) #value
                #define DEBUG(fmt, ...) log(fmt, ##__VA_ARGS__)
                #define STRINGIFIED(value) \\
                    #value
                #define GNU_MORE(fmt, ...) log(fmt, \\
                    ## __VA_ARGS__)
                %:define DIGRAPH_JOIN(a, b) a %:%: b
                %:define DIGRAPH_NAME(value) %: value
                %:define DIGRAPH_GNU(fmt, ...) log(fmt, %:%: __VA_ARGS__)
                #define ADJACENT_HASH(value) left ### value
                %:define ADJACENT_DIGRAPH(value) left %:%:%: value
                // phase-two splice keeps the next line in this comment \\
                #define NOT_REAL(value) value ## value
                %:pragma pack(push, 1)
                struct __attribute__((packed)) packet {
                    unsigned kind : 3;
                    int values[];
                };
                static const char blob[]
                    __attribute__((weak, section(".meta"),
                                   aligned(16)));
                static long long wide_value;
                static void (*handler)(int);
                static void noop(void) {}
                static void (*factory(void))(void) { return noop; }
                static int choose(int cond, int yes, int no) {
                    return cond ? yes : no;
                }
                int inspect(void) {
                    struct packet value = (struct packet){ .kind = 1 };
                    if (handler) { value.values[0] = 2; }
                    __asm__ volatile ("nop" ::: "memory");
                    return value.kind;
                }
                """,
            )
            _write(
                root / "app.cc",
                """
                #exe {
                class Widget {};
                U0 Main() {
                    I64 wide = 1;
                    reg U32 value = 1;
                    noreg U32 other = 2;
                    Widget *widget = new Widget;
                    del widget;
                    float4 lanes;
                    asm { nop }
                }
                """,
            )
            _write(
                root / "entry.asm",
                """
                [bits 16]
                BITS 32
                [org 0x7c00]
                ORG 0x8000
                section .text
                global start
                extern target
                %define COUNT 2
                start:
                    mov eax, [ebx + ecx*4 + 8]
                    rep movsd
                    call target
                table dd 1
                message db "[brackets in data are not an address]"
                    times COUNT db 0
                """,
            )

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            audit = json.loads(output.read_text(encoding="utf-8"))
            features = {entry["id"]: entry for entry in audit["features"]}
            expected = {
                "asm.addressing.base_index_scale",
                "asm.addressing.memory",
                "asm.directive.bits",
                "asm.directive.extern",
                "asm.directive.global",
                "asm.directive.org",
                "asm.directive.section",
                "asm.directive.times",
                "asm.instruction.call",
                "asm.instruction.mov",
                "asm.preprocessor.define",
                "asm.prefix.rep",
                "asm.relocation.pc_relative_external",
                "asm.output.elf32_relocatable",
                "asm.register.eax",
                "asm.register.ebx",
                "asm.register.ecx",
                "c.declarator.function_pointer",
                "c.declarator.unsized_array",
                "c.declarator.variadic",
                "c.extension.attribute.packed",
                "c.extension.attribute.aligned",
                "c.extension.attribute.section",
                "c.extension.attribute.weak",
                "c.extension.inline_assembly",
                "c.initializer.designated",
                "c.preprocessor.function_macro",
                "c.preprocessor.gnu_variadic_comma_elision",
                "c.preprocessor.pragma.pack",
                "c.preprocessor.stringify",
                "c.preprocessor.token_paste",
                "c.preprocessor.variadic_macro",
                "c.type.long_long",
                "c.type.struct",
                "c.output.elf32_relocatable",
                "cupid_c.declaration.class",
                "cupid_c.delivery.embedded_source",
                "cupid_c.expression.del",
                "cupid_c.expression.new",
                "cupid_c.extension.asm_block",
                "cupid_c.storage.noreg",
                "cupid_c.storage.reg",
                "cupid_c.type.float4",
                "cupid_c.type.i64",
                "cupid_c.type.u0",
            }
            self.assertTrue(expected.issubset(features), expected - set(features))
            self.assertEqual(features["asm.addressing.memory"]["occurrences"], 1)
            self.assertEqual(features["asm.directive.bits"]["occurrences"], 2)
            self.assertEqual(features["asm.directive.org"]["occurrences"], 2)
            self.assertEqual(features["asm.instruction.call"]["occurrences"], 1)
            self.assertEqual(
                features["c.preprocessor.function_macro"]["occurrences"], 12
            )
            self.assertEqual(
                features["c.preprocessor.function_macro"]["examples"][0][
                    "line"
                ],
                1,
            )
            self.assertTrue(
                features["c.preprocessor.function_macro"]["examples"][0][
                    "text"
                ].startswith("#define SPANNED(")
            )
            self.assertEqual(
                features["c.preprocessor.variadic_macro"]["occurrences"], 4
            )
            self.assertEqual(
                features["c.preprocessor.token_paste"]["occurrences"], 8
            )
            self.assertEqual(
                features["c.preprocessor.stringify"]["occurrences"], 5
            )
            self.assertEqual(
                features["c.preprocessor.gnu_variadic_comma_elision"][
                    "occurrences"
                ],
                3,
            )
            self.assertEqual(
                features["c.preprocessor.define"]["occurrences"], 12
            )
            self.assertNotIn("c.preprocessor.value", features)
            self.assertNotIn("asm.instruction.bits", features)
            self.assertNotIn("asm.instruction.org", features)
            self.assertNotIn("asm.instruction.table", features)
            self.assertEqual(features["c.expression.compound_literal"]["occurrences"], 1)
            self.assertEqual(features["c.declarator.bit_field"]["occurrences"], 1)
            self.assertEqual(features["c.initializer.designated"]["occurrences"], 1)
            self.assertIn("cupid_c.directive.exe", features)
            self.assertNotIn("c.preprocessor.exe", features)
            self.assertEqual(
                features["c.extension.attribute.packed"]["files"],
                ["feature.c"],
            )
            self.assertEqual(
                features["c.extension.attribute.packed"]["examples"][0]["line"],
                21,
            )
            for attribute_name in ("aligned", "section", "weak"):
                feature = features[f"c.extension.attribute.{attribute_name}"]
                self.assertEqual(feature["occurrences"], 1)
                self.assertEqual(feature["files"], ["feature.c"])
                self.assertEqual(feature["examples"][0]["line"], 26)
                self.assertTrue(
                    feature["examples"][0]["text"].startswith("__attribute__")
                )

            sources = {entry["path"]: entry for entry in audit["sources"]}
            self.assertIn(
                "cupid_c.type.u0",
                sources["app.cc"]["features"],
            )

    def test_inventory_distinguishes_gnu_and_c11_alignof_operators(self):
        module = _load_audit_module()
        collector = module.FeatureCollector()
        source = textwrap.dedent(
            """
            int gnu_plain = __alignof(int);
            int gnu_wrapped = __alignof__(int);
            int c11 = _Alignof(int);
            int __alignof_suffix = 0;
            const char *documentation = "__alignof __alignof__";
            /* __alignof(int) and __alignof__(int) are masked documentation. */
            """
        ).lstrip()

        module._scan_c_features("alignof.c", source, "c", collector)
        features = {entry["id"]: entry for entry in collector.inventory()}

        self.assertEqual(features["c.extension.gnu_alignof"]["occurrences"], 2)
        self.assertEqual(
            [
                example["line"]
                for example in features["c.extension.gnu_alignof"]["examples"]
            ],
            [1, 2],
        )
        self.assertEqual(features["c.expression.alignof"]["occurrences"], 1)

    def test_checked_attribute_inventory_matches_active_sources(self):
        audit = json.loads(ACTIVE_BUILD_MANIFEST.read_text(encoding="utf-8"))
        features = {entry["id"]: entry for entry in audit["features"]}
        expected = {
            "packed": 30,
            "aligned": 12,
            "noreturn": 11,
            "returns_twice": 1,
            "section": 2,
            "weak": 5,
            "used": 18,
            "noinline": 18,
            "unused": 5,
            "naked": 3,
            "target": 1,
        }
        attribute_files = set()
        for name, occurrences in expected.items():
            feature = features[f"c.extension.attribute.{name}"]
            self.assertEqual(feature["occurrences"], occurrences)
            attribute_files.update(feature["files"])
        self.assertEqual(sum(expected.values()), 106)
        self.assertEqual(len(attribute_files), 34)
        for name in ("aligned", "section", "weak"):
            self.assertIn(
                "kernel/cpu/ksyms.cc",
                features[f"c.extension.attribute.{name}"]["files"],
            )

    def test_c_logical_lines_use_only_real_newlines_and_preserve_evidence(self):
        spec = importlib.util.spec_from_file_location(
            "_cupid_build_graph_audit_test", AUDIT_TOOL
        )
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        try:
            spec.loader.exec_module(module)
        finally:
            del sys.modules[spec.name]

        lf = "#define JOIN( \\\n left) left\n"
        crlf = lf.replace("\n", "\r\n")
        self.assertEqual(
            module._c_logical_lines(lf), module._c_logical_lines(crlf)
        )
        self.assertEqual(
            module._c_logical_lines("#define TRAILING value\\"),
            [(1, "#define TRAILING value\\", "#define TRAILING value\\")],
        )
        controls = "#define ONLY 1\v#define NOT_SECOND 2\fstill_same"
        self.assertEqual(
            module._c_logical_lines(controls),
            [(1, controls, controls)],
        )

    def test_inventory_contracts_direct_c_line_directives(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc

                .PHONY: all
                all: main.o

                main.o: main.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(root / "main.c", '#line 40 "virtual.c"\nint value;\n')

            output = root / "audit.json"
            summary = root / "AUDIT.md"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                    "--summary",
                    str(summary),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            contract = json.loads(output.read_text(encoding="utf-8"))[
                "contracts"
            ]["c_preprocessor_line_directives"]
            self.assertEqual(contract["status"], "pass")
            self.assertEqual(contract["source_files"], 1)
            self.assertEqual(contract["named_line_occurrences"], 1)
            self.assertEqual(contract["direct_line_occurrences"], 1)
            self.assertEqual(contract["pp_token_line_occurrences"], 0)
            self.assertEqual(contract["filename_occurrences"], 1)
            self.assertEqual(contract["ordinary_marker_occurrences"], 1)
            self.assertEqual(contract["digraph_marker_occurrences"], 0)
            self.assertEqual(contract["numeric_marker_occurrences"], 0)
            self.assertEqual(contract["max_conditional_depth"], 0)
            self.assertEqual(
                contract["forms"],
                [
                    {
                        "conditional_depth": 0,
                        "evidence": [
                            {
                                "line": 1,
                                "operand": '40 "virtual.c"',
                                "path": "main.c",
                                "text": '#line 40 "virtual.c"',
                            }
                        ],
                        "files": ["main.c"],
                        "form": "direct_decimal_filename",
                        "has_filename": True,
                        "marker": "#",
                        "occurrences": 1,
                    }
                ],
            )
            self.assertIn(
                "1 named #line directive (1 direct, 0 pp-token; 1 filename); "
                "0 numeric markers; 1 source file; max conditional depth 0",
                summary.read_text(encoding="utf-8"),
            )

    def test_inventory_classifies_all_c_line_forms_after_phase_two(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc

                .PHONY: all
                all: main.o

                main.o: main.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(
                root / "main.c",
                r'''
                /* #line 1 "comment.c" */
                static const char ignored[] = "#line 2 \"string.c\"";
                #define LINE_NUMBER 70
                #if FEATURE
                #li\
                ne 20
                %:line LINE_NUMBER FILE_NAME
                # 88 "generated.c" 1 3
                #endif
                #line 90 /* separator */ "direct.c"
                int value;
                ''',
            )

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            contract = json.loads(output.read_text(encoding="utf-8"))[
                "contracts"
            ]["c_preprocessor_line_directives"]
            self.assertEqual(contract["named_line_occurrences"], 3)
            self.assertEqual(contract["direct_line_occurrences"], 2)
            self.assertEqual(contract["pp_token_line_occurrences"], 1)
            self.assertEqual(contract["filename_occurrences"], 1)
            self.assertEqual(contract["ordinary_marker_occurrences"], 2)
            self.assertEqual(contract["digraph_marker_occurrences"], 1)
            self.assertEqual(contract["numeric_marker_occurrences"], 1)
            self.assertEqual(contract["max_conditional_depth"], 1)
            features = {
                entry["id"]: entry
                for entry in json.loads(output.read_text(encoding="utf-8"))[
                    "features"
                ]
            }
            self.assertEqual(
                features["c.preprocessor.line"]["occurrences"], 3
            )

            forms = {
                (
                    entry["form"],
                    entry["marker"],
                    entry["conditional_depth"],
                ): entry
                for entry in contract["forms"]
            }
            self.assertEqual(
                set(forms),
                {
                    ("direct_decimal", "#", 1),
                    ("direct_decimal_filename", "#", 0),
                    ("numeric_marker", "#", 1),
                    ("pp_tokens", "%:", 1),
                },
            )
            self.assertEqual(
                forms[("direct_decimal", "#", 1)]["evidence"],
                [
                    {
                        "line": 5,
                        "operand": "20",
                        "path": "main.c",
                        "text": "#line 20",
                    }
                ],
            )
            self.assertEqual(
                forms[("pp_tokens", "%:", 1)]["evidence"][0]["operand"],
                "LINE_NUMBER FILE_NAME",
            )
            self.assertIsNone(
                forms[("pp_tokens", "%:", 1)]["has_filename"]
            )
            self.assertTrue(
                forms[("numeric_marker", "#", 1)]["has_filename"]
            )
            self.assertEqual(
                forms[("numeric_marker", "#", 1)]["evidence"][0][
                    "operand"
                ],
                '88 "generated.c" 1 3',
            )

    def test_line_directive_contract_rejects_unclassifiable_operands(self):
        cases = {
            "empty": ("#line\n", "unclassified active #line form"),
            "comment only": (
                "#line /* no operand */\n",
                "unclassified active #line form",
            ),
            "invalid token": (
                "%:line @\n",
                "unrecognized preprocessing token",
            ),
        }
        for name, (source, message) in cases.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                _write(
                    root / "Makefile",
                    """
                    .SUFFIXES:
                    CC = host-cc

                    .PHONY: all
                    all: main.o

                    main.o: main.c
                    \t$(CC) -c $< -o $@
                    """,
                )
                _write(root / "main.c", source)

                output = root / "audit.json"
                result = subprocess.run(
                    [
                        sys.executable,
                        str(AUDIT_TOOL),
                        "--root",
                        str(root),
                        "--output",
                        str(output),
                    ],
                    text=True,
                    capture_output=True,
                )

                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(output.exists())
                self.assertIn("main.c:1", result.stderr)
                self.assertIn(message, result.stderr)

    def test_line_directive_contract_rejects_active_templeos_edges(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc

                .PHONY: all
                all: TempleOS/reference.o

                TempleOS/reference.o: TempleOS/reference.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(
                root / "TempleOS" / "reference.c",
                '#line 900 "temple-reference.c"\nint reference;\n',
            )

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(output.exists())
            self.assertIn("TempleOS/reference.c", result.stderr)
            self.assertIn(
                "TempleOS reference tree cannot be an active C preprocessing input",
                result.stderr,
            )

    def test_inventory_contracts_active_conditional_expression_tokens(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc

                .PHONY: all
                all: main.o

                main.o: main.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(
                root / "main.c",
                r"""
                /* #if COMMENTED_OUT */
                static const char ignored[] = "#elif STRING_LITERAL";
                #if FLAG && \
                    defined(OTHER)
                #elif (VALUE + 1) == 2
                #endif
                %:if defined /* separator */ THIRD || '\x41' == 'A'
                %:elif FLAG && defined(OTHER)
                %:endif
                """,
            )

            output = root / "audit.json"
            summary = root / "AUDIT.md"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                    "--summary",
                    str(summary),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            contract = json.loads(output.read_text(encoding="utf-8"))[
                "contracts"
            ]["c_preprocessor_conditionals"]
            self.assertEqual(contract["status"], "pass")
            self.assertEqual(contract["if_occurrences"], 2)
            self.assertEqual(contract["elif_occurrences"], 2)
            self.assertEqual(contract["expression_occurrences"], 4)
            self.assertEqual(contract["unique_expressions"], 3)
            self.assertEqual(contract["directive_expression_pairs"], 4)
            expressions = {
                entry["expression"]: entry for entry in contract["expressions"]
            }
            self.assertEqual(
                set(expressions),
                {
                    "FLAG && defined ( OTHER )",
                    "( VALUE + 1 ) == 2",
                    "defined THIRD || '\\x41' == 'A'",
                },
            )
            shared = expressions["FLAG && defined ( OTHER )"]
            self.assertEqual(shared["if_occurrences"], 1)
            self.assertEqual(shared["elif_occurrences"], 1)
            self.assertEqual(shared["occurrences"], 2)
            self.assertEqual(shared["files"], ["main.c"])
            self.assertEqual(
                [(item["directive"], item["line"]) for item in shared["evidence"]],
                [("if", 3), ("elif", 8)],
            )
            self.assertIn(
                "4 conditional expressions (2 #if, 2 #elif); "
                "3 normalized expressions; 4 directive/expression pairs",
                summary.read_text(encoding="utf-8"),
            )

    def test_inventory_contracts_active_pragma_once_form(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc

                .PHONY: all
                all: main.o

                main.o: main.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(root / "main.c", "#pragma once\nint main_value;\n")

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            audit = json.loads(output.read_text(encoding="utf-8"))
            contract = audit["contracts"]["c_preprocessor_pragmas"]
            self.assertEqual(contract["status"], "pass")
            self.assertEqual(contract["pragma_occurrences"], 1)
            self.assertEqual(contract["once_occurrences"], 1)
            self.assertEqual(contract["pack_occurrences"], 0)
            self.assertEqual(
                contract["forms"],
                [
                    {
                        "action": "once",
                        "alignment": None,
                        "evidence": [
                            {
                                "line": 1,
                                "path": "main.c",
                                "text": "#pragma once",
                            }
                        ],
                        "files": ["main.c"],
                        "form": "once",
                        "occurrences": 1,
                    }
                ],
            )
            sources = {entry["path"]: entry for entry in audit["sources"]}
            self.assertIn(
                "c.preprocessor.pragma.once", sources["main.c"]["features"]
            )

    def test_inventory_contracts_normalized_pack_actions_and_depth(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc

                .PHONY: all
                all: main.o

                main.o: main.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(
                root / "main.c",
                """
                #pragma pack(push, 1)
                struct outer { char value; };
                %:pragma pack(push, 2)
                struct inner { char value; };
                #pragma pack(pop)
                #pragma pack(\\
                pop)
                struct natural { char value; };
                """,
            )

            output = root / "audit.json"
            summary = root / "AUDIT.md"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                    "--summary",
                    str(summary),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            contract = json.loads(output.read_text(encoding="utf-8"))[
                "contracts"
            ]["c_preprocessor_pragmas"]
            self.assertEqual(contract["pragma_occurrences"], 4)
            self.assertEqual(contract["once_occurrences"], 0)
            self.assertEqual(contract["pack_occurrences"], 4)
            self.assertEqual(contract["pack_push_occurrences"], 2)
            self.assertEqual(contract["pack_pop_occurrences"], 2)
            self.assertTrue(contract["pack_balanced"])
            self.assertEqual(contract["max_pack_depth"], 2)
            self.assertEqual(contract["pack_underflow_occurrences"], 0)
            self.assertEqual(contract["unmatched_pack_pushes"], 0)
            forms = {entry["form"]: entry for entry in contract["forms"]}
            self.assertEqual(
                set(forms),
                {
                    "pack(pop)",
                    "pack(push, 1)",
                    "pack(push, 2)",
                },
            )
            self.assertEqual(
                (
                    forms["pack(push, 1)"]["action"],
                    forms["pack(push, 1)"]["alignment"],
                ),
                ("pack_push", 1),
            )
            self.assertEqual(
                (
                    forms["pack(push, 2)"]["action"],
                    forms["pack(push, 2)"]["alignment"],
                ),
                ("pack_push", 2),
            )
            self.assertEqual(
                (
                    forms["pack(pop)"]["action"],
                    forms["pack(pop)"]["alignment"],
                    forms["pack(pop)"]["occurrences"],
                ),
                ("pack_pop", None, 2),
            )
            self.assertIn(
                "4 pragmas (0 once, 2 pack pushes, 2 pack pops); "
                "pack balanced: yes; max pack depth 2",
                summary.read_text(encoding="utf-8"),
            )

    def test_pragma_inventory_fails_closed_on_unclassified_active_forms(self):
        cases = {
            "vendor": "#pragma cupid_vendor frobnicate\n",
            "malformed-pack": "#pragma pack(push, 3)\n",
            "pragma-operator": '#define APPLY _Pragma("once")\n',
        }
        for name, source in cases.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                _write(
                    root / "Makefile",
                    """
                    .SUFFIXES:
                    CC = host-cc
                    .PHONY: all
                    all: main.o
                    main.o: main.c
                    \t$(CC) -c $< -o $@
                    """,
                )
                _write(root / "main.c", source)

                result = subprocess.run(
                    [
                        sys.executable,
                        str(AUDIT_TOOL),
                        "--root",
                        str(root),
                        "--output",
                        str(root / "audit.json"),
                    ],
                    text=True,
                    capture_output=True,
                )

                self.assertNotEqual(result.returncode, 0)
                self.assertIn("main.c:1", result.stderr)
                self.assertIn(
                    "unclassified active #pragma form", result.stderr
                )

    def test_checked_pragma_manifest_matches_active_source_contract(self):
        with tempfile.TemporaryDirectory() as td:
            output = Path(td) / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(REPO_ROOT),
                    "--supplemental-build",
                    "user:all",
                    "--supplemental-build",
                    "toolchain:all",
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            generated = json.loads(output.read_text(encoding="utf-8"))
            checked = json.loads(
                ACTIVE_BUILD_MANIFEST.read_text(encoding="utf-8")
            )
            contract = generated["contracts"]["c_preprocessor_pragmas"]
            self.assertEqual(
                checked["contracts"]["c_preprocessor_pragmas"], contract
            )
            self.assertEqual(contract["pragma_occurrences"], 5)
            self.assertEqual(contract["once_occurrences"], 1)
            self.assertEqual(contract["pack_occurrences"], 4)
            self.assertEqual(contract["pack_push_occurrences"], 2)
            self.assertEqual(contract["pack_pop_occurrences"], 2)
            self.assertTrue(contract["pack_balanced"])
            self.assertEqual(contract["max_pack_depth"], 1)
            self.assertEqual(contract["pack_underflow_occurrences"], 0)
            self.assertEqual(contract["unmatched_pack_pushes"], 0)

            forms = {entry["form"]: entry for entry in contract["forms"]}
            self.assertEqual(
                set(forms),
                {
                    "once",
                    "pack(pop)",
                    "pack(push, 1)",
                },
            )
            self.assertEqual(
                [(item["path"], item["line"]) for item in forms["once"]["evidence"]],
                [("bin/ctxt.cc", 1)],
            )
            self.assertEqual(
                [
                    (item["path"], item["line"])
                    for item in forms["pack(push, 1)"]["evidence"]
                ],
                [("bin/fat16.h", 26), ("kernel/fs/fat16.h", 26)],
            )
            self.assertEqual(
                [
                    (item["path"], item["line"])
                    for item in forms["pack(pop)"]["evidence"]
                ],
                [("bin/fat16.h", 76), ("kernel/fs/fat16.h", 76)],
            )
            self.assertTrue(
                all(
                    not item["path"].casefold().startswith("templeos/")
                    for form in contract["forms"]
                    for item in form["evidence"]
                )
            )
            features = {entry["id"]: entry for entry in generated["features"]}
            self.assertEqual(
                features["c.preprocessor.pragma.once"]["occurrences"], 1
            )

    def test_checked_line_directive_contract_matches_active_sources(self):
        with tempfile.TemporaryDirectory() as td:
            output = Path(td) / "audit.json"
            summary = Path(td) / "audit.md"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(REPO_ROOT),
                    "--supplemental-build",
                    "user:all",
                    "--supplemental-build",
                    "toolchain:all",
                    "--output",
                    str(output),
                    "--summary",
                    str(summary),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            generated = json.loads(output.read_text(encoding="utf-8"))
            checked = json.loads(
                ACTIVE_BUILD_MANIFEST.read_text(encoding="utf-8")
            )
            contract = generated["contracts"][
                "c_preprocessor_line_directives"
            ]
            self.assertEqual(contract["source_files"], 700)
            self.assertEqual(contract["named_line_occurrences"], 0)
            self.assertEqual(contract["direct_line_occurrences"], 0)
            self.assertEqual(contract["pp_token_line_occurrences"], 0)
            self.assertEqual(contract["filename_occurrences"], 0)
            self.assertEqual(contract["ordinary_marker_occurrences"], 0)
            self.assertEqual(contract["digraph_marker_occurrences"], 0)
            self.assertEqual(contract["numeric_marker_occurrences"], 0)
            self.assertEqual(contract["max_conditional_depth"], 0)
            self.assertEqual(contract["forms"], [])
            self.assertEqual(
                checked["contracts"]["c_preprocessor_line_directives"],
                contract,
            )
            self.assertNotIn(
                "c.preprocessor.line",
                {entry["id"] for entry in generated["features"]},
            )
            self.assertIn(
                "`c_preprocessor_line_directives` | `pass` | "
                "0 named #line directives (0 direct, 0 pp-token; 0 filename); "
                "0 numeric markers; 700 source files; max conditional depth 0",
                summary.read_text(encoding="utf-8"),
            )

    def test_inventory_contracts_unconditional_cupid_exe_block_forms(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                OBJCOPY = host-objcopy

                .PHONY: all
                all: ordinary.o digraph.o header_user.o

                ordinary.o: ordinary.cc
                \t$(OBJCOPY) -I binary -O elf32-i386 $< $@

                digraph.o: digraph.cc
                \t$(OBJCOPY) -I binary -O elf32-i386 $< $@

                header_user.o: header_user.cc exe_header.h
                \t$(OBJCOPY) -I binary -O elf32-i386 $< $@
                """,
            )
            _write(
                root / "ordinary.cc",
                """
                I32 ordinary_value;
                #exe {
                    ordinary_value = 1;
                }
                """,
            )
            _write(
                root / "digraph.cc",
                """
                I32 digraph_value;
                %:exe { digraph_value = 2; }
                """,
            )
            _write(
                root / "header_user.cc",
                '#include "exe_header.h"\nI32 header_value;\n',
            )
            _write(
                root / "exe_header.h",
                "#exe { header_value = 3; }\n",
            )

            output = root / "audit.json"
            summary = root / "AUDIT.md"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                    "--summary",
                    str(summary),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            contract = json.loads(output.read_text(encoding="utf-8"))["contracts"][
                "c_preprocessor_cupid_exe"
            ]
            self.assertEqual(
                contract,
                {
                    "status": "pass",
                    "exe_occurrences": 3,
                    "block_occurrences": 3,
                    "ordinary_marker_occurrences": 2,
                    "digraph_marker_occurrences": 1,
                    "max_conditional_depth": 0,
                    "forms": [
                        {
                            "form": "block",
                            "marker": "#",
                            "conditional_depth": 0,
                            "occurrences": 2,
                            "files": ["exe_header.h", "ordinary.cc"],
                            "evidence": [
                                {
                                    "path": "exe_header.h",
                                    "line": 1,
                                    "text": "#exe { header_value = 3; }",
                                },
                                {
                                    "path": "ordinary.cc",
                                    "line": 2,
                                    "text": "#exe {",
                                }
                            ],
                        },
                        {
                            "form": "block",
                            "marker": "%:",
                            "conditional_depth": 0,
                            "occurrences": 1,
                            "files": ["digraph.cc"],
                            "evidence": [
                                {
                                    "path": "digraph.cc",
                                    "line": 2,
                                    "text": "%:exe { digraph_value = 2; }",
                                }
                            ],
                        },
                    ],
                },
            )
            self.assertIn(
                "3 Cupid #exe blocks (2 #, 1 %:); max conditional depth 0",
                summary.read_text(encoding="utf-8"),
            )

    def test_cupid_exe_inventory_fails_closed_on_conditional_form(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                OBJCOPY = host-objcopy
                .PHONY: all
                all: app.o
                app.o: app.cc
                \t$(OBJCOPY) -I binary -O elf32-i386 $< $@
                """,
            )
            _write(
                root / "app.cc",
                """
                #if ENABLED
                #exe {
                }
                #endif
                """,
            )

            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(root / "audit.json"),
                ],
                text=True,
                capture_output=True,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("app.cc:2", result.stderr)
            self.assertIn(
                "unclassified active Cupid #exe form: conditional depth 1",
                result.stderr,
            )

    def test_cupid_exe_inventory_fails_closed_on_non_block_forms(self):
        cases = {
            "empty": "#exe\n",
            "string": '#exe "script.cc"\n',
            "angle-file": "#exe <script.cc>\n",
            "identifier": "#exe body\n",
            "parenthesized": "#exe()\n",
            "case-variant": "#EXE {\n}\n",
            "brace-digraph": "#exe <%\n%>\n",
            "invalid-token": "#exe @\n",
        }
        for name, source in cases.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                _write(
                    root / "Makefile",
                    """
                    .SUFFIXES:
                    OBJCOPY = host-objcopy
                    .PHONY: all
                    all: app.o
                    app.o: app.cc
                    \t$(OBJCOPY) -I binary -O elf32-i386 $< $@
                    """,
                )
                _write(root / "app.cc", source)

                result = subprocess.run(
                    [
                        sys.executable,
                        str(AUDIT_TOOL),
                        "--root",
                        str(root),
                        "--output",
                        str(root / "audit.json"),
                    ],
                    text=True,
                    capture_output=True,
                )

                self.assertNotEqual(result.returncode, 0)
                self.assertIn("app.cc:1", result.stderr)
                self.assertIn(
                    "unclassified active Cupid #exe form", result.stderr
                )

    def test_checked_cupid_exe_manifest_matches_active_source_contract(self):
        with tempfile.TemporaryDirectory() as td:
            output = Path(td) / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(REPO_ROOT),
                    "--supplemental-build",
                    "user:all",
                    "--supplemental-build",
                    "toolchain:all",
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            generated = json.loads(output.read_text(encoding="utf-8"))
            checked = json.loads(
                ACTIVE_BUILD_MANIFEST.read_text(encoding="utf-8")
            )
            contract = generated["contracts"]["c_preprocessor_cupid_exe"]
            self.assertEqual(
                checked["contracts"]["c_preprocessor_cupid_exe"], contract
            )
            self.assertEqual(
                contract,
                {
                    "status": "pass",
                    "exe_occurrences": 1,
                    "block_occurrences": 1,
                    "ordinary_marker_occurrences": 1,
                    "digraph_marker_occurrences": 0,
                    "max_conditional_depth": 0,
                    "forms": [
                        {
                            "form": "block",
                            "marker": "#",
                            "conditional_depth": 0,
                            "occurrences": 1,
                            "files": ["bin/feature6_exe.cc"],
                            "evidence": [
                                {
                                    "path": "bin/feature6_exe.cc",
                                    "line": 7,
                                    "text": "#exe {",
                                }
                            ],
                        }
                    ],
                },
            )
            self.assertTrue(
                all(
                    not item["path"].casefold().startswith("templeos/")
                    for form in contract["forms"]
                    for item in form["evidence"]
                )
            )

    def test_conditional_inventory_fails_closed_on_unknown_tokens(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                .PHONY: all
                all: main.o
                main.o: main.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(root / "main.c", "#if VALUE @ 1\n#endif\n")

            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(root / "audit.json"),
                ],
                text=True,
                capture_output=True,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("main.c:1", result.stderr)
            self.assertIn("unrecognized preprocessing token", result.stderr)

    def test_checked_conditional_manifest_matches_active_source_contract(self):
        with tempfile.TemporaryDirectory() as td:
            output = Path(td) / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(REPO_ROOT),
                    "--supplemental-build",
                    "user:all",
                    "--supplemental-build",
                    "toolchain:all",
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            contract = json.loads(output.read_text(encoding="utf-8"))[
                "contracts"
            ]["c_preprocessor_conditionals"]
            self.assertEqual(contract["if_occurrences"], 134)
            self.assertEqual(contract["elif_occurrences"], 9)
            self.assertEqual(contract["expression_occurrences"], 143)
            self.assertEqual(contract["unique_expressions"], 29)
            self.assertEqual(contract["directive_expression_pairs"], 31)
            self.assertTrue(
                all(
                    not item["path"].casefold().startswith("templeos/")
                    for expression in contract["expressions"]
                    for item in expression["evidence"]
                )
            )

            manifest = _conditional_manifest_records()
            inventory = {
                entry["expression"]: entry for entry in contract["expressions"]
            }
            self.assertEqual(set(manifest), set(inventory))
            for expression, entry in inventory.items():
                self.assertEqual(len(entry["evidence"]), entry["occurrences"])
                self.assertEqual(
                    entry["files"],
                    sorted({item["path"] for item in entry["evidence"]}),
                )
                self.assertEqual(
                    manifest[expression][:2],
                    (entry["if_occurrences"], entry["elif_occurrences"]),
                )
            self.assertEqual(
                {expression: values[2] for expression, values in manifest.items()},
                {
                    "! defined ( CUPID_HOSTED_I386_LINUX_ABI_H )": 1,
                    "! defined ( CUPID_RUNTIME_WINDOWS )": 1,
                    "! defined ( _WIN32 ) && ! defined ( __MACOSX__ ) && "
                    "! defined ( __DJGPP__ )": 1,
                    "! defined ( __SIZEOF_POINTER__ ) || "
                    "__SIZEOF_POINTER__ != 4": 0,
                    "! defined ( __STDC_VERSION__ ) || "
                    "( __STDC_VERSION__ < 202311L )": 1,
                    "! defined ( __cplusplus )": 1,
                    "( __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ )": 0,
                    "( __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ )": 1,
                    "0": 0,
                    "1": 1,
                    "OPL_ENABLE_STEREOEXT": 0,
                    "OPL_ENABLE_STEREOEXT && ! defined OPL_SIN": 0,
                    "OPL_QUIRK_CHANNELSAMPLEDELAY": 1,
                    "ORIGCODE": 0,
                    "defined ( DOOM_PORT_CUPIDOS )": 0,
                    "defined ( ORIGCODE ) || "
                    "defined ( DOOM_PORT_CUPIDOS )": 0,
                    "_MSC_VER < 1400": 1,
                    "_WIN64": 0,
                    "defined ( _MSC_VER ) && ! defined ( __cplusplus )": 0,
                    "defined ( _WIN32 )": 0,
                    "defined ( _WIN32 ) && ! defined ( _WIN32_WCE )": 0,
                    "defined ( _WIN32 ) || defined ( __DJGPP__ )": 0,
                    "defined ( CUPID_HOSTED_I386_LINUX_ABI_H )": 0,
                    "defined ( CUPID_RUNTIME_WINDOWS )": 0,
                    "defined ( CUPID_TOOLCHAIN_CUPIDC_STATIC_LONG_DOUBLE_INTERNAL )": 0,
                    "defined ( __DJGPP__ )": 0,
                    "defined ( __MACOSX__ )": 0,
                    "defined ( __SIZEOF_POINTER__ ) && "
                    "( __SIZEOF_POINTER__ == 8 )": 0,
                    "defined ( __cplusplus ) || "
                    "defined ( __bool_true_false_are_defined )": 1,
                },
            )

    def test_checked_conditional_manifest_is_a_c_contract_prerequisite(self):
        source = CUPIDC_PP_CONTRACT.read_text(encoding="utf-8")
        define = source.index(
            "#define CUPIDC_PP_CONDITIONAL_CASE(expression, if_count, "
            "elif_count, expected)"
        )
        include = source.index(
            '#include "cupidc_pp_conditional_cases.inc"', define
        )
        undefine = source.index(
            "#undef CUPIDC_PP_CONDITIONAL_CASE", include
        )
        self.assertLess(define, include)
        self.assertLess(include, undefine)

        makefile = TOOLCHAIN_MAKEFILE.read_text(encoding="utf-8")
        rule = re.search(
            r"\$\(BUILD_DIR\)/cupidc_pp_contract\.o:(.*?)\n\t",
            makefile,
            flags=re.DOTALL,
        )
        self.assertIsNotNone(rule)
        self.assertIn("tests/cupidc_pp_conditional_cases.inc", rule.group(1))

    def test_active_assembly_controls_are_not_memory_operands(self):
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
            features = {entry["id"]: entry for entry in audit["features"]}
            self.assertEqual(features["asm.addressing.memory"]["occurrences"], 101)
            self.assertEqual(features["asm.directive.bits"]["occurrences"], 8)
            self.assertEqual(features["asm.directive.org"]["occurrences"], 3)
            transforms = {
                entry["output"]: entry for entry in audit["build"]["transforms"]
            }
            expected_assembly = {
                "boot/boot.bin": "assemble_flat_binary",
                "kernel/core/context_switch.o": "assemble_elf32_relocatable",
                "kernel/cpu/isr.o": "assemble_elf32_relocatable",
                "kernel/smp_trampoline.bin": "assemble_flat_binary",
            }
            for output_path, operation in expected_assembly.items():
                self.assertEqual(
                    transforms[output_path]["tools"],
                    ["cupid_assembler", "host_python"],
                )
                self.assertEqual(transforms[output_path]["operation"], operation)
            self.assertFalse(
                [
                    entry["output"]
                    for entry in audit["build"]["transforms"]
                    if "nasm" in entry["tools"]
                ]
            )

    def test_inventory_accounts_for_unreachable_and_duplicate_sources(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                ALL_CC := $(filter-out filtered.cc, $(wildcard *.cc))

                .PHONY: all
                all: active.o

                active.o: active.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(root / "active.c", "int answer(void) { return 42; }\n")
            _write(root / "copy.c", "int answer(void) { return 42; }\n")
            _write(root / "unused.c", "int unused(void) { return 0; }\n")
            _write(root / "filtered.cc", "U0 Legacy() {}\n")
            _write(root / "bin" / "build.cup", "echo bootstrap\n")
            _write(
                root / "TempleOS" / "reference.c",
                '#line 900 "temple-reference.c"\nint reference;\n',
            )

            first = root / "first.json"
            second = root / "second.json"
            for output in (first, second):
                result = subprocess.run(
                    [
                        sys.executable,
                        str(AUDIT_TOOL),
                        "--root",
                        str(root),
                        "--output",
                        str(output),
                    ],
                    text=True,
                    capture_output=True,
                )
                self.assertEqual(result.returncode, 0, result.stderr)

            self.assertEqual(first.read_bytes(), second.read_bytes())
            audit = json.loads(first.read_text(encoding="utf-8"))
            unreachable = {
                entry["path"]: entry for entry in audit["unreachable_sources"]
            }
            self.assertEqual(
                set(unreachable),
                {"bin/build.cup", "copy.c", "filtered.cc", "unused.c"},
            )
            self.assertEqual(
                audit["contracts"]["c_preprocessor_line_directives"],
                {
                    "status": "pass",
                    "source_files": 1,
                    "named_line_occurrences": 0,
                    "direct_line_occurrences": 0,
                    "pp_token_line_occurrences": 0,
                    "filename_occurrences": 0,
                    "ordinary_marker_occurrences": 0,
                    "digraph_marker_occurrences": 0,
                    "numeric_marker_occurrences": 0,
                    "max_conditional_depth": 0,
                    "forms": [],
                },
            )
            self.assertEqual(
                unreachable["bin/build.cup"]["language"], "cupid_script"
            )
            self.assertEqual(
                unreachable["copy.c"]["classification"], "exact_duplicate"
            )
            self.assertEqual(unreachable["copy.c"]["duplicate_of"], ["active.c"])
            self.assertEqual(
                unreachable["filtered.cc"]["classification"],
                "explicitly_excluded",
            )
            self.assertEqual(
                unreachable["unused.c"]["classification"], "not_reached"
            )
            self.assertEqual(audit["summary"]["active_sources"], 1)
            self.assertEqual(audit["summary"]["unreachable_sources"], 4)

    def test_inventory_records_known_historical_source_relationships(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc

                .PHONY: all
                all: kernel/lang/cupidc.o

                kernel/lang/cupidc.o: kernel/lang/cupidc.cc
                \t$(CC) -c $< -o $@
                """,
            )
            _write(
                root / "kernel" / "lang" / "cupidc.cc",
                "int current_compiler(void) { return 2; }\n",
            )
            _write(
                root / "bin" / "cupidc.c",
                "int historical_compiler(void) { return 1; }\n",
            )

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            audit = json.loads(output.read_text(encoding="utf-8"))
            historical = {
                entry["path"]: entry for entry in audit["unreachable_sources"]
            }["bin/cupidc.c"]
            self.assertEqual(historical["classification"], "historical_copy")
            self.assertEqual(
                historical["relations"],
                [
                    {
                        "kind": "historical_copy_of",
                        "path": "kernel/lang/cupidc.cc",
                        "evidence": "audited project source relationship",
                    }
                ],
            )

    def test_inventory_resolves_declared_and_assembly_include_edges(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                ASM = nasm
                CFLAGS = -Iinclude -include forced.h

                .PHONY: all
                all: main.o entry.o

                main.o: main.c
                \t$(CC) $(CFLAGS) -c $< -o $@

                entry.o: entry.asm
                \t$(ASM) -f elf32 $< -o $@
                """,
            )
            _write(
                root / "main.c",
                """
                /*
                #include "ignored.h"
                */
                #include <api.h>
                int value;
                """,
            )
            _write(root / "include" / "api.h", "%:include \"types.h\"\n")
            _write(root / "include" / "types.h", "typedef int word;\n")
            _write(root / "ignored.h", "#define IGNORED 1\n")
            _write(root / "forced.h", "#define FORCED 1\n")
            _write(root / "entry.asm", "%include \"helper.asm\"\nret\n")
            _write(root / "helper.asm", "%define VALUE 1\n")

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            audit = json.loads(output.read_text(encoding="utf-8"))
            sources = {entry["path"]: entry for entry in audit["sources"]}
            self.assertEqual(
                set(sources),
                {
                    "entry.asm",
                    "forced.h",
                    "helper.asm",
                    "include/api.h",
                    "include/types.h",
                    "main.c",
                },
            )
            self.assertEqual(sources["main.c"]["includes"], ["include/api.h"])
            self.assertEqual(
                sources["include/api.h"]["includes"], ["include/types.h"]
            )
            self.assertEqual(sources["entry.asm"]["includes"], ["helper.asm"])
            self.assertEqual(sources["forced.h"]["reachability"], "forced_include")
            self.assertEqual(
                audit["build"]["include_search_paths"], ["include"]
            )

    def test_inventory_contracts_direct_c_include_operand_forms(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                CFLAGS = -Iinclude

                .PHONY: all
                all: main.o

                main.o: main.c
                \t$(CC) $(CFLAGS) -c $< -o $@
                """,
            )
            _write(
                root / "main.c",
                """
                # include "local.h" // trailing comment
                %:include <angle.h>
                #inc\\
                lude "spliced.h"
                #include /* operand comment */ "commented.h"
                int value;
                """,
            )
            _write(root / "local.h", "int local_value;\n")
            _write(root / "spliced.h", "int spliced_value;\n")
            _write(root / "commented.h", "int commented_value;\n")
            _write(root / "include" / "angle.h", "int angle_value;\n")

            output = root / "audit.json"
            summary = root / "audit.md"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                    "--summary",
                    str(summary),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            audit = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(
                audit["contracts"]["c_preprocessor_include_operands"],
                {
                    "status": "pass",
                    "source_files": 5,
                    "include_occurrences": 4,
                    "direct_quoted_occurrences": 3,
                    "direct_angle_occurrences": 1,
                    "pp_token_operand_occurrences": 0,
                    "ordinary_marker_occurrences": 3,
                    "digraph_marker_occurrences": 1,
                    "max_conditional_depth": 0,
                },
            )
            sources = {entry["path"]: entry for entry in audit["sources"]}
            self.assertEqual(
                sources["main.c"]["includes"],
                ["commented.h", "include/angle.h", "local.h", "spliced.h"],
            )
            self.assertIn(
                "4 C include operands (3 quoted, 1 angle, 0 pp-token); "
                "5 source files; max conditional depth 0",
                summary.read_text(encoding="utf-8"),
            )

    def test_include_closure_fails_closed_on_pp_token_operands(self):
        cases = {
            "object": {
                "flags": "",
                "source": """
                    #define HEADER "x.h"
                    #if FEATURE
                    #include /* bridge */ HEADER
                    #endif
                """,
                "line": 3,
                "marker": "#",
                "raw": "#include /* bridge */ HEADER",
                "normalized": "HEADER",
                "conditional": "#if FEATURE at line 2",
            },
            "function": {
                "flags": "",
                "source": """
                    #define PICK(value) value
                    %:include PICK("x.h")
                """,
                "line": 2,
                "marker": "%:",
                "raw": '%:include PICK("x.h")',
                "normalized": 'PICK ( "x.h" )',
                "conditional": "<unconditional>",
            },
            "configured": {
                "flags": '-DCONFIG_HEADER=\\"x.h\\"',
                "source": "#include CONFIG_HEADER\n",
                "line": 1,
                "marker": "#",
                "raw": "#include CONFIG_HEADER",
                "normalized": "CONFIG_HEADER",
                "conditional": "<unconditional>",
            },
            "forced": {
                "flags": "-include forced.h",
                "source": "#include FORCED_HEADER\n",
                "line": 1,
                "marker": "#",
                "raw": "#include FORCED_HEADER",
                "normalized": "FORCED_HEADER",
                "conditional": "<unconditional>",
            },
        }
        for name, case in cases.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                _write(
                    root / "Makefile",
                    f"""
                    .SUFFIXES:
                    CC = host-cc
                    CFLAGS = {case['flags']}

                    .PHONY: all
                    all: main.o

                    main.o: main.c
                    \t$(CC) $(CFLAGS) -c $< -o $@
                    """,
                )
                _write(root / "main.c", case["source"])
                if name == "forced":
                    _write(
                        root / "forced.h",
                        '#define FORCED_HEADER "x.h"\n',
                    )

                output = root / "audit.json"
                result = subprocess.run(
                    [
                        sys.executable,
                        str(AUDIT_TOOL),
                        "--root",
                        str(root),
                        "--output",
                        str(output),
                    ],
                    text=True,
                    capture_output=True,
                )

                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(output.exists())
                self.assertIn(f"main.c:{case['line']}", result.stderr)
                self.assertIn(
                    "macro-expanded #include operand", result.stderr
                )
                self.assertIn(f"marker={case['marker']!r}", result.stderr)
                self.assertIn(f"raw={case['raw']!r}", result.stderr)
                self.assertIn(
                    f"normalized={case['normalized']!r}", result.stderr
                )
                self.assertIn(
                    f"conditional={case['conditional']!r}", result.stderr
                )

    def test_checked_include_operand_contract_matches_active_sources(self):
        with tempfile.TemporaryDirectory() as td:
            output = Path(td) / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(REPO_ROOT),
                    "--supplemental-build",
                    "user:all",
                    "--supplemental-build",
                    "toolchain:all",
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            generated = json.loads(output.read_text(encoding="utf-8"))
            checked = json.loads(
                ACTIVE_BUILD_MANIFEST.read_text(encoding="utf-8")
            )
            contract = generated["contracts"][
                "c_preprocessor_include_operands"
            ]
            self.assertEqual(
                checked["contracts"]["c_preprocessor_include_operands"],
                contract,
            )
            self.assertEqual(contract["source_files"], 700)
            self.assertEqual(contract["include_occurrences"], 2450)
            self.assertEqual(contract["direct_quoted_occurrences"], 2197)
            self.assertEqual(contract["direct_angle_occurrences"], 253)
            self.assertEqual(contract["pp_token_operand_occurrences"], 0)

    def test_inventory_detects_link_inputs_missing_from_artifact_manifest(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            artifact_recipe = _json_list_recipe(["main.o"])
            _write(
                root / "Makefile",
                f"""
                .SUFFIXES:
                CC = host-cc
                LD = host-ld
                OBJECTS = main.o
                ARTIFACTS := $(OBJECTS)
                OBJECTS += late.o

                .PHONY: all print-bootstrap-artifacts
                all: kernel.elf

                kernel.elf: $(OBJECTS)
                \t$(LD) -o $@ $^

                main.o: main.c
                \t$(CC) -c $< -o $@

                late.o: late.c
                \t$(CC) -c $< -o $@

                print-bootstrap-artifacts:
                {artifact_recipe}
                """,
            )
            _write(root / "main.c", "int main(void) { return 0; }\n")
            _write(root / "late.c", "int late(void) { return 1; }\n")

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            audit = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(
                audit["contracts"]["bootstrap_artifact_coverage"],
                {
                    "status": "fail",
                    "declared_artifacts": 1,
                    "linked_objects": 2,
                    "missing_link_inputs": ["late.o"],
                },
            )

    def test_checked_json_and_markdown_fail_when_sources_drift(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc

                .PHONY: all
                all: main.o

                main.o: main.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(root / "main.c", "int main(void) { return 0; }\n")
            output = root / "audit.json"
            summary = root / "AUDIT.md"
            command = [
                sys.executable,
                str(AUDIT_TOOL),
                "--root",
                str(root),
                "--output",
                str(output),
                "--summary",
                str(summary),
            ]

            generated = subprocess.run(command, text=True, capture_output=True)
            self.assertEqual(generated.returncode, 0, generated.stderr)
            self.assertIn("# Active build and source audit", summary.read_text())

            checked = subprocess.run(
                [*command, "--check"], text=True, capture_output=True
            )
            self.assertEqual(checked.returncode, 0, checked.stderr)

            _write(root / "main.c", "int main(void) { return 1; }\n")
            stale = subprocess.run(
                [*command, "--check"], text=True, capture_output=True
            )
            self.assertEqual(stale.returncode, 1)
            self.assertIn("out of date", stale.stderr)
            self.assertIn("audit.json", stale.stderr)

    def test_inventory_is_stable_when_generated_c_has_not_been_materialized(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                PYTHON = python

                .PHONY: all
                all: generated.o

                generated.c: input.txt
                \t$(PYTHON) generator.py $< $@

                generated.o: generated.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(root / "input.txt", "payload\n")
            _write(root / "generator.py", "# fixture generator\n")
            _write(root / "ignored.h", "#define DIRTY_GENERATOR_EDGE 1\n")

            absent_output = root / "absent.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(absent_output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            _write(
                root / "generated.c",
                '#line 77 "generated-template.c"\n'
                '#include "ignored.h"\nint generated;\n',
            )
            materialized_output = root / "materialized.json"
            materialized = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(materialized_output),
                ],
                text=True,
                capture_output=True,
            )
            self.assertEqual(materialized.returncode, 0, materialized.stderr)
            self.assertEqual(absent_output.read_bytes(), materialized_output.read_bytes())

            audit = json.loads(absent_output.read_text(encoding="utf-8"))
            sources = {entry["path"]: entry for entry in audit["sources"]}
            self.assertEqual(sources["generated.c"]["origin"], "generated")
            self.assertIsNone(sources["generated.c"]["sha256"])
            self.assertEqual(sources["generated.c"]["includes"], [])
            self.assertEqual(
                sources["generated.c"]["features"],
                ["c.output.elf32_relocatable"],
            )
            self.assertEqual(
                audit["contracts"]["c_preprocessor_line_directives"],
                {
                    "status": "pass",
                    "source_files": 0,
                    "named_line_occurrences": 0,
                    "direct_line_occurrences": 0,
                    "pp_token_line_occurrences": 0,
                    "filename_occurrences": 0,
                    "ordinary_marker_occurrences": 0,
                    "digraph_marker_occurrences": 0,
                    "numeric_marker_occurrences": 0,
                    "max_conditional_depth": 0,
                    "forms": [],
                },
            )

    def test_inventory_includes_an_explicit_supplemental_build(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                .PHONY: all
                all: kernel.o
                kernel.o: kernel.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(root / "kernel.c", "int kernel(void) { return 0; }\n")
            _write(
                root / "user" / "Makefile",
                """
                .SUFFIXES:
                CC = gcc
                LD = ld
                .PHONY: all
                all: build build/tool
                build:
                \tmkdir -p build
                build/tool: build/tool.o build/shared.o
                \t$(LD) -o $@ $^
                build/tool.o: examples/tool.c cupid.h
                \t$(CC) -I. -c $< -o $@
                build/shared.o: ../shared.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(
                root / "user" / "examples" / "tool.c",
                "#include \"cupid.h\"\nint tool(void) { return CUPID; }\n",
            )
            _write(root / "user" / "cupid.h", "#define CUPID 1\n")
            _write(root / "shared.c", "int shared(void) { return 1; }\n")

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--supplemental-build",
                    "user:all",
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            audit = json.loads(output.read_text(encoding="utf-8"))
            sources = {entry["path"]: entry for entry in audit["sources"]}
            self.assertIn("user/examples/tool.c", sources)
            self.assertIn("user/cupid.h", sources)
            self.assertIn("shared.c", sources)
            self.assertNotIn("user/../shared.c", sources)
            self.assertEqual(
                sources["user/examples/tool.c"]["cohort"], "user_program"
            )
            self.assertEqual(
                audit["supplemental_builds"][0]["directory"], "user"
            )
            transforms = {
                entry["output"]: entry
                for entry in audit["supplemental_builds"][0]["transforms"]
            }
            self.assertEqual(
                transforms["user/build/tool.o"]["tools"], ["host_c_compiler"]
            )
            self.assertEqual(
                transforms["user/build/shared.o"]["inputs"], ["shared.c"]
            )
            self.assertEqual(
                transforms["user/build/tool"]["tools"], ["host_linker"]
            )
            self.assertEqual(transforms["user/build"]["tools"], ["host_shell"])

    def test_inventory_distinguishes_c_objects_from_host_executables(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                CFLAGS = -c
                .PHONY: all
                all: module.o
                module.o: module.c
                \t$(CC) $(CFLAGS) $< -o $@
                """,
            )
            _write(root / "module.c", "int module(void) { return 0; }\n")
            _write(
                root / "toolchain" / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                .PHONY: all
                all: build/contract
                build/contract: build/contract.o
                \t$(CC) $< -o $@
                build/contract.o: contract.c
                \t$(CC) -c $< -o $@
                """,
            )
            _write(
                root / "toolchain" / "contract.c",
                "int main(void) { return 0; }\n",
            )

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--supplemental-build",
                    "toolchain:all",
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            audit = json.loads(output.read_text(encoding="utf-8"))
            root_transforms = {
                entry["output"]: entry for entry in audit["build"]["transforms"]
            }
            host_transforms = {
                entry["output"]: entry
                for entry in audit["supplemental_builds"][0]["transforms"]
            }
            self.assertEqual(
                root_transforms["module.o"]["operation"],
                "compile_c_to_elf32_object",
            )
            self.assertEqual(
                host_transforms["toolchain/build/contract"]["operation"],
                "compile_and_link_host_executable",
            )
            self.assertEqual(
                host_transforms["toolchain/build/contract.o"]["operation"],
                "compile_c_to_host_object",
            )
            sources = {entry["path"]: entry for entry in audit["sources"]}
            self.assertIn(
                "c.output.elf32_relocatable", sources["module.c"]["features"]
            )
            self.assertNotIn(
                "c.output.elf32_relocatable",
                sources["toolchain/contract.c"]["features"],
            )

    def test_inventory_records_the_i386_abi_and_linker_script_subset(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                LD = host-ld
                CFLAGS = -m32 -ffreestanding
                LDFLAGS = -m elf_i386 -T link.ld
                .PHONY: all
                all: kernel.elf
                kernel.o: kernel.c
                \t$(CC) $(CFLAGS) -c $< -o $@
                kernel.elf: kernel.o
                \t$(LD) $(LDFLAGS) -o $@ $^
                """,
            )
            _write(root / "kernel.c", "int main(void) { return 0; }\n")
            _write(
                root / "link.ld",
                """
                ENTRY(main)
                SECTIONS {
                    . = 1M;
                    .text ALIGN(16) : { *(.text*) }
                    .bss : { *(COMMON) }
                    end = .;
                    ASSERT(end < 2M, "too large")
                }
                """,
            )

            output = root / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            abi = json.loads(output.read_text(encoding="utf-8"))["abi"]
            self.assertEqual(abi["architecture"], "i386")
            self.assertEqual(abi["data_model"], "ILP32")
            self.assertEqual(
                abi["required_relocations"], ["R_386_32", "R_386_PC32"]
            )
            self.assertEqual(
                abi["linker_script"]["features"],
                [
                    "ALIGN",
                    "ASSERT",
                    "COMMON",
                    "ENTRY",
                    "SECTIONS",
                    "input_section_wildcards",
                    "location_counter",
                    "symbol_definitions",
                ],
            )

    def test_inventory_hashes_use_the_canonical_lf_checkout_policy(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                .PHONY: all
                all: main.o
                main.o: main.c
                \t$(CC) -c $< -o $@
                """,
            )
            source = root / "main.c"
            source.write_bytes(b"int main(void) {\r\n    return 0;\r\n}\r\n")
            first = root / "first.json"
            second = root / "second.json"

            first_result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(first),
                ],
                text=True,
                capture_output=True,
            )
            self.assertEqual(first_result.returncode, 0, first_result.stderr)

            source.write_bytes(b"int main(void) {\n    return 0;\n}\n")
            second_result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(second),
                ],
                text=True,
                capture_output=True,
            )
            self.assertEqual(second_result.returncode, 0, second_result.stderr)
            self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_checked_cupidc_active_manifest_has_exact_source_cohorts(self):
        lines = ACTIVE_CASE_MANIFEST.read_text(encoding="utf-8").splitlines()
        profile_pattern = re.compile(
            r"^CUPIDC_PP_PROFILE\(([A-Z0-9_]+), ([A-Z0-9_]+), "
            r"(CTOOL_(?:TRUE|FALSE)), (CTOOL_(?:TRUE|FALSE)), "
            r"(CTOOL_(?:TRUE|FALSE)), (CTOOL_(?:TRUE|FALSE))\)$"
        )
        active_pattern = re.compile(
            r'^CUPIDC_PP_ACTIVE_CASE\(([A-Z0-9_]+), "([^"]+)"\)$'
        )
        generated_pattern = re.compile(
            r'^CUPIDC_PP_GENERATED_CASE\(([A-Z0-9_]+), "([^"]+)"\)$'
        )
        profiles = [
            match.groups()
            for line in lines
            if (match := profile_pattern.fullmatch(line)) is not None
        ]
        active = [
            match.groups()
            for line in lines
            if (match := active_pattern.fullmatch(line)) is not None
        ]
        generated = [
            match.groups()
            for line in lines
            if (match := generated_pattern.fullmatch(line)) is not None
        ]

        self.assertEqual(
            profiles,
            [
                (
                    "KERNEL_I386",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
                (
                    "DOOM_COMPAT_I386",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                    "CTOOL_TRUE",
                    "CTOOL_TRUE",
                ),
                (
                    "DOOM_TREE_I386",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                    "CTOOL_TRUE",
                    "CTOOL_TRUE",
                ),
                (
                    "USER_I386",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
                (
                    "FREESTANDING_I386",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
                (
                    "CUPID_RUNTIME",
                    "CTOOL_C_PP_MODE_CUPID",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
                (
                    "HOSTED_TOOLCHAIN_64",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_FALSE",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
                (
                    "HOSTED_KERNEL_BRIDGE_64",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_FALSE",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
                (
                    "HOSTED_I386_LINUX",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_FALSE",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
                (
                    "HOSTED_I386_WINDOWS",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_FALSE",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
                (
                    "HOSTED_I386_KERNEL_BRIDGE",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_FALSE",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
                (
                    "HOSTED_I386_LINUX_GNU",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_TRUE",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
            ],
        )
        self.assertEqual(
            {name: sum(case_name == name for case_name, _ in active)
            for name, _, _, _, _, _ in profiles},
            {
                "KERNEL_I386": 155,
                "DOOM_COMPAT_I386": 3,
                "DOOM_TREE_I386": 80,
                "USER_I386": 3,
                "FREESTANDING_I386": 1,
                "CUPID_RUNTIME": 107,
                "HOSTED_TOOLCHAIN_64": 0,
                "HOSTED_KERNEL_BRIDGE_64": 0,
                "HOSTED_I386_LINUX": 33,
                "HOSTED_I386_WINDOWS": 6,
                "HOSTED_I386_KERNEL_BRIDGE": 2,
                "HOSTED_I386_LINUX_GNU": 3,
            },
        )
        self.assertEqual(len(active), 393)
        for expected in (
            ("KERNEL_I386", "/kernel/core/kernel.cc"),
            ("KERNEL_I386", "/kernel/audio/memio.cc"),
            ("KERNEL_I386", "/kernel/audio/mus2midi.cc"),
            ("KERNEL_I386", "/kernel/audio/nuked_opl3.cc"),
            ("DOOM_TREE_I386", "/kernel/doom/i_sound_cupidos.cc"),
            ("DOOM_TREE_I386", "/kernel/doom/src/d_main.cc"),
            ("USER_I386", "/user/examples/hello.cc"),
            (
                "FREESTANDING_I386",
                "/toolchain/tests/hosted_i386_windows_contract.cc",
            ),
            ("CUPID_RUNTIME", "/bin/browser.cc"),
            ("HOSTED_I386_LINUX", "/toolchain/ctool.cc"),
            ("HOSTED_I386_LINUX", "/toolchain/cupidc_emit.cc"),
            ("HOSTED_I386_LINUX", "/toolchain/cupidc_ir.cc"),
            ("HOSTED_I386_LINUX", "/toolchain/x86.cc"),
            ("HOSTED_I386_KERNEL_BRIDGE", "/kernel/lang/as_elf.cc"),
            ("HOSTED_I386_LINUX", "/toolchain/ctool_host.cc"),
            ("HOSTED_I386_LINUX", "/toolchain/cupidc_main.cc"),
            ("HOSTED_I386_WINDOWS", "/toolchain/ctool_host.cc"),
            ("HOSTED_I386_WINDOWS", "/toolchain/cupidasm_main.cc"),
            ("HOSTED_I386_WINDOWS", "/toolchain/cupidc_main.cc"),
            ("HOSTED_I386_WINDOWS", "/toolchain/cupidld_main.cc"),
            ("HOSTED_I386_WINDOWS", "/toolchain/cupidobj_main.cc"),
            (
                "HOSTED_I386_WINDOWS",
                "/toolchain/hosted/i386-windows/publication_runtime.cc",
            ),
            (
                "HOSTED_I386_LINUX",
                "/toolchain/tests/cupidc_object_contract.cc",
            ),
            (
                "HOSTED_I386_LINUX",
                "/toolchain/tests/user_syscall_abi_contract.cc",
            ),
            (
                "HOSTED_I386_LINUX_GNU",
                "/toolchain/tests/hosted_i386_runtime_contract.cc",
            ),
            (
                "HOSTED_I386_KERNEL_BRIDGE",
                "/toolchain/tests/cupidasm_kernel_elf_contract.cc",
            ),
            (
                "HOSTED_I386_LINUX_GNU",
                "/toolchain/hosted/i386-linux/runtime.cc",
            ),
            (
                "HOSTED_I386_LINUX_GNU",
                "/toolchain/hosted/i386-windows/runtime.cc",
            ),
            (
                "HOSTED_I386_LINUX",
                "/toolchain/tests/hosted_i386_windows_runtime_contract.cc",
            ),
        ):
            self.assertIn(expected, active)
        self.assertNotIn(
            (
                "HOSTED_I386_LINUX",
                "/toolchain/tests/hosted_i386_runtime_contract.c",
            ),
            active,
        )
        self.assertNotIn(
            ("DOOM_COMPAT_I386", "/kernel/audio/memio.cc"),
            active,
        )
        self.assertNotIn(
            ("DOOM_COMPAT_I386", "/kernel/audio/mus2midi.cc"),
            active,
        )
        self.assertEqual(
            generated,
            [
                ("KERNEL_I386", "/kernel/cpu/ksyms_data.cc"),
                ("KERNEL_I386", "/kernel/util/bin_programs_gen.cc"),
                ("KERNEL_I386", "/kernel/util/demos_programs_gen.cc"),
                ("KERNEL_I386", "/kernel/util/docs_programs_gen.cc"),
            ],
        )

    def test_cupidobj_profile_manifest_is_a_checked_generator(self):
        module = _load_audit_module()
        target = module._CUPIDOBJ_PROFILE_MANIFEST_OUTPUT
        expected_inputs = module._cupidobj_profile_manifest_expected_inputs(
            REPO_ROOT
        )
        delivery = module._build_transforms(
            ".",
            {target},
            {
                target: module.MakeRule(
                    prerequisites=expected_inputs,
                    recipe=list(module._CUPIDOBJ_PROFILE_MANIFEST_RECIPE),
                )
            },
        )[0]

        self.assertEqual(delivery["operation"], "generate_profile_manifest")
        self.assertEqual(delivery["tools"], ["cupid_object", "host_python"])
        self.assertEqual(delivery["inputs"], expected_inputs)
        module._validate_cupidobj_profile_manifest_delivery(
            REPO_ROOT,
            [delivery],
        )

        seed_inputs = {
            "Makefile",
            "tools/bootstrap_toolchain.py",
            "bootstrap/seeds/i386-linux/manifest.json",
            "bootstrap/seeds/i386-linux/cupidasm.elf",
            "bootstrap/seeds/i386-linux/cupidc.elf",
            "bootstrap/seeds/i386-linux/cupiddis.elf",
            "bootstrap/seeds/i386-linux/cupidld.elf",
            "bootstrap/seeds/i386-linux/cupidobj.elf",
        }
        self.assertTrue(seed_inputs.issubset(delivery["inputs"]))

        changes = {
            "missing delivery": [],
            "wrong output": [{**delivery, "output": "build/profile.json"}],
            "wrong operation": [
                {**delivery, "operation": "host_orchestration"}
            ],
            "wrong tools": [{**delivery, "tools": ["host_python"]}],
            "changed recipe": [
                {
                    **delivery,
                    "recipe": [
                        "$(PYTHON) tools/cupidc_kernel_compile.py --root . \\",
                        "--write-profile-input-manifest $@",
                    ],
                }
            ],
            "missing seed": [
                {
                    **delivery,
                    "inputs": [
                        path
                        for path in delivery["inputs"]
                        if path
                        != "bootstrap/seeds/i386-linux/cupidobj.elf"
                    ],
                }
            ],
            "missing profile input": [
                {
                    **delivery,
                    "inputs": [
                        path
                        for path in delivery["inputs"]
                        if path != "kernel/doom/src/doom.h"
                    ],
                }
            ],
            "unexpected input": [
                {
                    **delivery,
                    "inputs": [*delivery["inputs"], "build/untracked.input"],
                }
            ],
        }
        for name, changed in changes.items():
            with self.subTest(name=name), self.assertRaisesRegex(
                module.AuditError,
                r"CupidObj profile manifest",
            ):
                module._validate_cupidobj_profile_manifest_delivery(
                    REPO_ROOT,
                    changed,
                )

        near_match = module._build_transforms(
            ".",
            {target},
            {
                target: module.MakeRule(
                    prerequisites=expected_inputs,
                    recipe=[
                        "$(PYTHON) tools/cupidc_kernel_compile.py --root . "
                        "--write-profile-input-manifests $@"
                    ],
                )
            },
        )[0]
        self.assertNotEqual(
            near_match["operation"],
            "generate_profile_manifest",
        )
        self.assertEqual(near_match["operation"], "host_orchestration")
        self.assertEqual(near_match["tools"], ["host_python"])

    def test_cupidobj_profile_manifest_tracks_nested_profile_headers(self):
        module = _load_audit_module()
        wrapper_source = (
            REPO_ROOT / "tools" / "cupidc_kernel_compile.py"
        ).read_text(encoding="utf-8")
        current_inputs = module._cupidobj_profile_manifest_expected_inputs(
            REPO_ROOT
        )

        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for relative in current_inputs:
                if Path(relative).suffix in {".h", ".inc"}:
                    (root / relative).parent.mkdir(parents=True, exist_ok=True)
            _write(
                root / "tools" / "cupidc_kernel_compile.py",
                wrapper_source,
            )
            _write(root / "toolchain" / "cupidobj.cc", "// fixture\n")
            nested_header = (
                "kernel/doom/src/include_stubs/sys/nested/profile.h"
            )
            _write(root / nested_header, "#define PROFILE_NESTED 1\n")
            _write(
                root / "Makefile",
                """
                CUPIDC_KERNEL_COMPILE := $(PYTHON) tools/cupidc_kernel_compile.py --root .
                KERNEL=kernel/kernel.bin
                OS_IMAGE=cupidos.img
                DOOM_CUPIDC_INPUT_MANIFEST := build/bootstrap/doom-cupidc-inputs.json
                $(DOOM_CUPIDC_INPUT_MANIFEST): FORCE $(DOOM_CUPIDC_HEADERS) \
                    $(CHECKED_SEED_INPUTS) tools/cupidc_kernel_compile.py
                \t$(PYTHON) tools/cupidc_kernel_compile.py --root . \
                \t\t--manifest $(BOOTSTRAP_SEED_MANIFEST) \
                \t\t--write-profile-input-manifest $@
                """,
            )

            expected_inputs = (
                module._cupidobj_profile_manifest_expected_inputs(root)
            )
            self.assertIn(nested_header, expected_inputs)
            delivery = {
                "output": module._CUPIDOBJ_PROFILE_MANIFEST_OUTPUT,
                "operation": "generate_profile_manifest",
                "tools": ["cupid_object", "host_python"],
                "recipe": list(module._CUPIDOBJ_PROFILE_MANIFEST_RECIPE),
                "inputs": [
                    path for path in expected_inputs if path != nested_header
                ],
            }
            with self.assertRaisesRegex(
                module.AuditError,
                rf"CupidObj profile manifest inputs changed.*{re.escape(nested_header)}",
            ):
                module._validate_cupidobj_profile_manifest_delivery(
                    root,
                    [delivery],
                )

    def test_cupidobj_profile_manifest_cannot_disappear_from_production_root(
        self,
    ):
        module = _load_audit_module()
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(root / "Makefile", ".PHONY: all\nall:\n")
            module._validate_cupidobj_profile_manifest_delivery(root, [])

            _write(
                root / "Makefile",
                """
                CUPIDC_KERNEL_COMPILE := $(PYTHON) tools/cupidc_kernel_compile.py --root .
                KERNEL=kernel/renamed.bin
                OS_IMAGE=cupidos.img
                .PHONY: all
                all:
                """,
            )
            _write(
                root / "tools" / "cupidc_kernel_compile.py",
                "# production wrapper fixture\n",
            )
            _write(root / "toolchain" / "cupidobj.cc", "// fixture\n")
            _write(root / "CONTEXT.md", "# Cupid OS\n")
            _write(
                root / "docs" / "bootstrap" / "README.md",
                "# Bootstrap\n",
            )
            (root / "kernel" / "doom" / "src").mkdir(
                parents=True,
                exist_ok=True,
            )

            with self.assertRaisesRegex(
                module.AuditError,
                r"CupidObj profile manifest Make contract changed",
            ):
                module._validate_cupidobj_profile_manifest_delivery(root, [])

    def test_cupidobj_profile_manifest_make_contract_rejects_drift(self):
        module = _load_audit_module()
        source = (REPO_ROOT / "Makefile").read_text(encoding="utf-8")
        mutations = {
            "renamed output": (
                "DOOM_CUPIDC_INPUT_MANIFEST := "
                "build/bootstrap/doom-cupidc-inputs.json",
                "DOOM_CUPIDC_INPUT_MANIFEST := build/bootstrap/doom-inputs.json",
            ),
            "unchecked prerequisites": (
                "$(CHECKED_SEED_INPUTS) tools/cupidc_kernel_compile.py",
                "tools/cupidc_kernel_compile.py",
            ),
            "unpinned seed manifest": (
                "$(PYTHON) tools/cupidc_kernel_compile.py --root . \\\n"
                "\t\t--manifest $(BOOTSTRAP_SEED_MANIFEST) \\",
                "$(PYTHON) tools/cupidc_kernel_compile.py --root . \\\n"
                "\t\t--manifest bootstrap/unchecked.json \\",
            ),
            "different publisher mode": (
                "--write-profile-input-manifest $@",
                "--write-input-manifest $@",
            ),
        }
        module._validate_cupidobj_profile_manifest_make_source(source)
        for name, (old, new) in mutations.items():
            with self.subTest(name=name):
                self.assertEqual(source.count(old), 1)
                changed = source.replace(old, new, 1)
                with self.assertRaisesRegex(
                    module.AuditError,
                    r"CupidObj profile manifest Make contract changed",
                ):
                    module._validate_cupidobj_profile_manifest_make_source(
                        changed
                    )

    def test_checked_seed_runner_contract_rejects_drift(self):
        module = _load_audit_module()
        module._validate_checked_seed_runner_contract(REPO_ROOT)
        sources = {
            relative: (REPO_ROOT / relative).read_text(encoding="utf-8")
            for relative in module._CHECKED_SEED_RUNNER_FILES
        }
        mutations = (
            (
                "optional runner removed",
                "tools/bootstrap_toolchain.py",
                "runner: ToolRunner | None = None,",
                "runner: ToolRunner | None = ToolRunner,",
            ),
            (
                "live cohort not reloaded",
                "tools/bootstrap_toolchain.py",
                "live_seed = _load_seed_inputs(manifest_path, None)",
                "live_seed = _load_seed_inputs(manifest_path, seed_inputs)",
            ),
            (
                "kernel runner not forwarded",
                "tools/cupidc_kernel_compile.py",
                "                        runner=executor,",
                "                        runner=None,",
            ),
            (
                "production capture not forwarded",
                "tools/cupidc_production_compile.py",
                "                        frozen_seed=seed_inputs,",
                "                        frozen_seed=None,",
            ),
            (
                "linker selection changed",
                "tools/cupidld_user_link.py",
                '                        "cupidld",',
                '                        "cupidc",',
            ),
            (
                "injected runner left as a dead marker",
                "tools/bootstrap_toolchain.py",
                "            result = active_runner.run("
                "executable, arguments, timeout)",
                "            if False:\n"
                "                active_runner.run("
                "executable, arguments, timeout)\n"
                "            result = ToolRunner(root).run("
                "executable, arguments, timeout)",
            ),
            (
                "live seed reload left as a dead marker",
                "tools/bootstrap_toolchain.py",
                "            live_seed = _load_seed_inputs("
                "manifest_path, None)",
                "            if False:\n"
                "                _load_seed_inputs(manifest_path, None)\n"
                "            live_seed = seed_inputs",
            ),
            (
                "manifest mismatch does not raise",
                "tools/bootstrap_toolchain.py",
                "            raise BootstrapError(\n"
                "                f\"checked seed inputs changed while "
                "{display_name} ran: \"\n"
                "                \"manifest content differs\"\n"
                "            )",
                "            pass",
            ),
            (
                "frozen seed rebound from live inputs",
                "tools/cupidc_kernel_compile.py",
                "                try:\n"
                "                    result = run_seed_tool(\n"
                "                        manifest_path,\n"
                "                        root,\n"
                "                        \"cupidc\",",
                "                seed_inputs = verify_seed_inputs("
                "manifest_path)\n"
                "                try:\n"
                "                    result = run_seed_tool(\n"
                "                        manifest_path,\n"
                "                        root,\n"
                "                        \"cupidc\",",
            ),
            (
                "kernel delegation left in dead code",
                "tools/cupidc_kernel_compile.py",
                "                    result = run_seed_tool(\n"
                "                        manifest_path,\n"
                "                        root,\n"
                "                        \"cupidc\",\n"
                "                        arguments,\n"
                "                        timeout=timeout,\n"
                "                        frozen_seed=seed_inputs,\n"
                "                        runner=executor,\n"
                "                    )",
                "                    if False:\n"
                "                        result = run_seed_tool(\n"
                "                            manifest_path,\n"
                "                            root,\n"
                "                            \"cupidc\",\n"
                "                            arguments,\n"
                "                            timeout=timeout,\n"
                "                            frozen_seed=seed_inputs,\n"
                "                            runner=executor,\n"
                "                        )\n"
                "                    result = (\n"
                "                        executor\n"
                "                        if executor is not None\n"
                "                        else ToolRunner(root)\n"
                "                    ).run(\n"
                "                        seed_inputs.tools[\"cupidc\"],\n"
                "                        arguments,\n"
                "                        timeout,\n"
                "                    )",
            ),
            (
                "outer frozen dispatch bypasses checked helper",
                "tools/bootstrap_toolchain.py",
                "    if frozen_seed is not None:\n"
                "        return run_frozen(frozen_seed)",
                "    if frozen_seed is not None:\n"
                "        return (\n"
                "            runner\n"
                "            if runner is not None\n"
                "            else ToolRunner(root)\n"
                "        ).run(\n"
                "            frozen_seed.tools[tool_name],\n"
                "            arguments,\n"
                "            timeout,\n"
                "        )",
            ),
            (
                "frozen seed tool map mutated in place",
                "tools/cupidc_kernel_compile.py",
                "                try:\n"
                "                    result = run_seed_tool(\n"
                "                        manifest_path,\n"
                "                        root,\n"
                "                        \"cupidc\",",
                "                seed_inputs.tools[\"cupidc\"] = (\n"
                "                    verify_seed_inputs("
                "manifest_path).tools[\"cupidc\"]\n"
                "                )\n"
                "                try:\n"
                "                    result = run_seed_tool(\n"
                "                        manifest_path,\n"
                "                        root,\n"
                "                        \"cupidc\",",
            ),
            (
                "publication moved into an exception handler",
                "tools/cupidc_kernel_compile.py",
                "                _replace_with_retry("
                "temporary_output, output)",
                "                try:\n"
                "                    pass\n"
                "                except Exception:\n"
                "                    _replace_with_retry("
                "temporary_output, output)",
            ),
            (
                "wrapper returns before checked execution",
                "tools/cupidc_kernel_compile.py",
                '    """Compile one approved source and atomically publish '
                'a checked object."""',
                '    """Compile one approved source and atomically publish '
                'a checked object."""\n'
                "    return",
            ),
            (
                "shared runner shadowed by a local unchecked adapter",
                "tools/cupidc_kernel_compile.py",
                "                try:\n"
                "                    result = run_seed_tool(\n"
                "                        manifest_path,\n"
                "                        root,\n"
                "                        \"cupidc\",",
                "                run_seed_tool = lambda manifest_path, "
                "root, tool, arguments, **kwargs: (\n"
                "                    executor\n"
                "                    if executor is not None\n"
                "                    else ToolRunner(root)\n"
                "                ).run(\n"
                "                    kwargs[\"frozen_seed\"].tools[tool],\n"
                "                    arguments,\n"
                "                    kwargs[\"timeout\"],\n"
                "                )\n"
                "                try:\n"
                "                    result = run_seed_tool(\n"
                "                        manifest_path,\n"
                "                        root,\n"
                "                        \"cupidc\",",
            ),
            (
                "shared runner replaced through the module namespace",
                "tools/cupidc_kernel_compile.py",
                "                try:\n"
                "                    result = run_seed_tool(\n"
                "                        manifest_path,\n"
                "                        root,\n"
                "                        \"cupidc\",",
                "                globals().__setitem__(\n"
                "                    \"run_seed_tool\",\n"
                "                    lambda manifest_path, root, tool, "
                "arguments, **kwargs: (\n"
                "                        executor\n"
                "                        if executor is not None\n"
                "                        else ToolRunner(root)\n"
                "                    ).run(\n"
                "                        kwargs[\"frozen_seed\"].tools[tool],\n"
                "                        arguments,\n"
                "                        kwargs[\"timeout\"],\n"
                "                    ),\n"
                "                )\n"
                "                try:\n"
                "                    result = run_seed_tool(\n"
                "                        manifest_path,\n"
                "                        root,\n"
                "                        \"cupidc\",",
            ),
            (
                "frozen tool map replaced from the live seed",
                "tools/bootstrap_toolchain.py",
                "    def run_frozen(seed_inputs: SeedInputs) -> "
                "subprocess.CompletedProcess[str]:\n"
                "        try:\n"
                "            executable = seed_inputs.tools[tool_name]",
                "    def run_frozen(seed_inputs: SeedInputs) -> "
                "subprocess.CompletedProcess[str]:\n"
                "        seed_inputs.tools[tool_name] = "
                "_load_seed_inputs(\n"
                "            manifest_path, None\n"
                "        ).tools[tool_name]\n"
                "        try:\n"
                "            executable = seed_inputs.tools[tool_name]",
            ),
            (
                "wrapper converted into an inert generator",
                "tools/cupidc_kernel_compile.py",
                '    """Compile one approved source and atomically publish '
                'a checked object."""',
                '    """Compile one approved source and atomically publish '
                'a checked object."""\n'
                "    if False:\n"
                "        yield None",
            ),
            (
                "shared runner replaced by a decorator",
                "tools/bootstrap_toolchain.py",
                "def run_seed_tool(\n",
                "@unchecked_run_seed_tool\n"
                "def run_seed_tool(\n",
            ),
            (
                "candidate published through an early alias",
                "tools/cupidc_kernel_compile.py",
                "                    raise KernelCompileError(str(error)) "
                "from error\n"
                "                if result.returncode != 0:",
                "                    raise KernelCompileError(str(error)) "
                "from error\n"
                "                publish_unchecked = _replace_with_retry\n"
                "                publish_unchecked(temporary_output, output)\n"
                "                if result.returncode != 0:",
            ),
            (
                "live seed reload failure returns tool success",
                "tools/bootstrap_toolchain.py",
                "        except BootstrapError as error:\n"
                "            raise BootstrapError(\n"
                "                f\"checked seed inputs changed while \"\n"
                "                f\"{display_name} ran: {error}\"\n"
                "            ) from error",
                "        except BootstrapError:\n"
                "            return result",
            ),
            (
                "seed freezer shadowed by a live verifier",
                "tools/cupidc_kernel_compile.py",
                "            try:\n"
                "                seed_inputs = freeze_seed_inputs(\n"
                "                    manifest_path, Path(seed_temporary)\n"
                "                )",
                "            freeze_seed_inputs = lambda manifest_path, "
                "directory: verify_seed_inputs(manifest_path)\n"
                "            try:\n"
                "                seed_inputs = freeze_seed_inputs(\n"
                "                    manifest_path, Path(seed_temporary)\n"
                "                )",
            ),
            (
                "exported runner rebound after its checked definition",
                "tools/bootstrap_toolchain.py",
                "\n\ndef _build_stage(\n",
                "\n\nrun_seed_tool = unchecked_seed_tool\n\n\n"
                "def _build_stage(\n",
            ),
            (
                "candidate published through Path.replace before validation",
                "tools/cupidc_kernel_compile.py",
                "                    raise KernelCompileError(str(error)) "
                "from error\n"
                "                if result.returncode != 0:",
                "                    raise KernelCompileError(str(error)) "
                "from error\n"
                "                temporary_output.replace(output)\n"
                "                if result.returncode != 0:",
            ),
            (
                "outer transaction handler suppresses failure",
                "tools/cupidc_kernel_compile.py",
                "    except OSError as error:\n"
                "        raise KernelCompileError(\n"
                "            f\"could not publish kernel object {output}: "
                "{error}\"\n"
                "        ) from error",
                "    except BaseException:\n"
                "        pass",
            ),
        )
        for name, relative, old, new in mutations:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                for source_relative, source in sources.items():
                    changed = source
                    if source_relative == relative:
                        self.assertEqual(source.count(old), 1)
                        changed = source.replace(old, new, 1)
                    _write(root / source_relative, changed)
                with self.assertRaisesRegex(
                    module.AuditError,
                    r"checked-seed runner contract changed",
                ):
                    module._validate_checked_seed_runner_contract(root)

    def test_checked_seed_runner_production_identity_uses_build_inputs(self):
        module = _load_audit_module()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for relative in (
                "Makefile",
                "toolchain/cupidobj.cc",
                "bootstrap/seeds/i386-linux/manifest.json",
            ):
                _write(root / relative, "fixture\n")
            for relative in ("kernel/doom/src", "user/examples"):
                (root / relative).mkdir(parents=True)
            _write(root / "CONTEXT.md", "# mutable documentation\n")
            self.assertTrue(
                module._is_checked_seed_runner_production_root(root)
            )
            (root / "CONTEXT.md").unlink()
            self.assertTrue(
                module._is_checked_seed_runner_production_root(root)
            )
            (root / "Makefile").unlink()
            self.assertFalse(
                module._is_checked_seed_runner_production_root(root)
            )

    def test_cupidobj_profile_manifest_wrapper_rejects_drift(self):
        module = _load_audit_module()
        source = (
            REPO_ROOT / "tools" / "cupidc_kernel_compile.py"
        ).read_text(encoding="utf-8")
        start = source.index("def write_profile_input_manifest(")
        end = source.index("\ndef ", start + 4)
        publisher = source[start:end]
        mutations = {
            "no publication lock": (
                "publication_lock = _acquire_profile_manifest_lock(resolved)",
                "publication_lock = None",
            ),
            "unchecked seed": (
                "checked_seed = verify_seed_inputs(manifest_path)",
                "checked_seed = load_seed_inputs(manifest_path)",
            ),
            "live seed not rechecked": (
                "live_seed = verify_seed_inputs(manifest_path)",
                "live_seed = checked_seed",
            ),
            "wrong checked tool": ('"cupidobj",', '"cupidasm",'),
            "no parity gate": (
                "candidate_capture.payload != oracle_payload",
                "candidate_capture.payload == oracle_payload",
            ),
            "live profile not rechecked": (
                "_require_profile_inputs_unchanged(root, capture)",
                "pass  # profile inputs were not rechecked",
            ),
            "output not rechecked": (
                "_require_profile_output_unchanged(resolved, initial_output)",
                "pass  # output was not rechecked",
            ),
            "non-atomic publication": (
                "_replace_profile_candidate_with_retry(",
                "_replace_profile_candidate_without_checks(",
            ),
            "reordered live checks": (
                "            _require_profile_inputs_unchanged(root, capture)\n"
                "            _require_profile_directory_unchanged(output_directory)\n"
                "            _require_profile_output_unchanged(resolved, initial_output)",
                "            _require_profile_output_unchanged(resolved, initial_output)\n"
                "            _require_profile_directory_unchanged(output_directory)\n"
                "            _require_profile_inputs_unchanged(root, capture)",
            ),
            "late post-tool directory recheck": (
                "                _require_profile_directory_unchanged(output_directory)\n"
                "                _require_profile_file_unchanged(\n"
                "                    snapshot_capture,\n"
                '                    "CupidObj profile snapshot",\n'
                "                )",
                "                _require_profile_file_unchanged(\n"
                "                    snapshot_capture,\n"
                '                    "CupidObj profile snapshot",\n'
                "                )\n"
                "                _require_profile_directory_unchanged(output_directory)",
            ),
            "dead checked-tool marker": (
                "                    result = run_seed_tool(",
                '                    "result = run_seed_tool("\n'
                "                    result = run_unchecked_tool(",
            ),
        }
        module._validate_cupidobj_profile_manifest_wrapper(REPO_ROOT)
        for name, (old, new) in mutations.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                self.assertEqual(publisher.count(old), 1)
                changed_publisher = publisher.replace(old, new, 1)
                root = Path(td)
                _write(
                    root / "tools" / "cupidc_kernel_compile.py",
                    source[:start] + changed_publisher + source[end:],
                )
                with self.assertRaisesRegex(
                    module.AuditError,
                    r"CupidObj profile manifest wrapper .* changed",
                ):
                    module._validate_cupidobj_profile_manifest_wrapper(root)

    def test_cupidobj_install_source_is_a_checked_delivery_generator(self):
        module = _load_audit_module()
        audit = json.loads(ACTIVE_BUILD_MANIFEST.read_text(encoding="utf-8"))
        expected_content = module._cupidobj_install_source_expected_content(
            audit["build"]["transforms"]
        )
        checked_inputs = [
            "Makefile",
            "tools/bootstrap_toolchain.py",
            "bootstrap/seeds/i386-linux/manifest.json",
            "bootstrap/seeds/i386-linux/cupidasm.elf",
            "bootstrap/seeds/i386-linux/cupidc.elf",
            "bootstrap/seeds/i386-linux/cupiddis.elf",
            "bootstrap/seeds/i386-linux/cupidld.elf",
            "bootstrap/seeds/i386-linux/cupidobj.elf",
        ]
        ignored_inputs = {*checked_inputs, "tools/hostbuild.py"}
        cases = (
            (
                "kernel/util/bin_programs_gen.cc",
                "$(CUPIDOBJ) install-source bin "
                "--bin $(BIN_CC_SRCS) --headers $(BIN_HDR_SRCS) "
                "--browser $(BROWSER_SUB_SRCS) -o $@",
            ),
            (
                "kernel/util/docs_programs_gen.cc",
                "$(CUPIDOBJ) install-source docs "
                "--ctxt $(DOC_CTXT_SRCS) --doc-assets $(DOC_ASSET_SRCS) "
                "--home-assets $(HOME_ASSET_SRCS) -o $@",
            ),
            (
                "kernel/util/demos_programs_gen.cc",
                "$(CUPIDOBJ) install-source demos "
                "--demos $(DEMO_ASM_SRCS) -o $@",
            ),
        )
        deliveries = {}
        for delivery_target, recipe in cases:
            with self.subTest(delivery_target=delivery_target):
                checked = next(
                    item
                    for item in audit["build"]["transforms"]
                    if item["output"] == delivery_target
                )
                content_inputs = [
                    path
                    for path in checked["inputs"]
                    if path not in ignored_inputs
                ]
                delivery = module._build_transforms(
                    ".",
                    {delivery_target},
                    {
                        delivery_target: module.MakeRule(
                            prerequisites=[*content_inputs, *checked_inputs],
                            recipe=[recipe],
                        )
                    },
                )[0]
                self.assertEqual(
                    delivery["operation"], "generate_install_source"
                )
                module._validate_cupidobj_install_source_delivery(
                    ".", delivery, expected_content[delivery_target]
                )
                checked.update(delivery)
                deliveries[delivery_target] = delivery

        manifest = module._c_preprocessor_active_cases_manifest(audit)
        for delivery_target, _recipe in cases:
            self.assertIn(
                ("KERNEL_I386", f"/{delivery_target}"),
                manifest.generated_cases,
            )

        target = "kernel/util/bin_programs_gen.cc"
        transform = deliveries[target]
        changes = {
            "wrong output": {**transform, "output": "bin/install.cc"},
            "wrong operation": {
                **transform,
                "operation": "transform_object",
            },
            "wrong tools": {**transform, "tools": ["cupid_object"]},
            "missing checked input": {
                **transform,
                "inputs": [
                    path
                    for path in transform["inputs"]
                    if path != "bootstrap/seeds/i386-linux/cupidobj.elf"
                ],
            },
            "missing content input": {
                **transform,
                "inputs": [
                    path
                    for path in transform["inputs"]
                    if path != "bin/fat16.h"
                ],
            },
            "duplicate content input": {
                **transform,
                "inputs": [*transform["inputs"], "bin/fat16.h"],
            },
            "changed recipe": {
                **transform,
                "recipe": [
                    "$(CUPIDOBJ) install-source bin "
                    "--bin $(BIN_CC_SRCS) -o $@"
                ],
            },
        }
        for name, changed in changes.items():
            with self.subTest(name=name), self.assertRaisesRegex(
                module.AuditError,
                r"CupidObj install-source delivery",
            ):
                module._validate_cupidobj_install_source_delivery(
                    ".", changed, expected_content[target]
                )

        substitutions = (
            (
                "kernel/util/bin_programs_gen.cc",
                "bin/cat.cc",
                "bin/not_a_real_program.cc",
            ),
            (
                "kernel/util/docs_programs_gen.cc",
                "cupidos-txt/00INDEX.CTXT",
                "cupidos-txt/99NOTREAL.CTXT",
            ),
            (
                "kernel/util/demos_programs_gen.cc",
                "demos/hello.asm",
                "demos/not_a_real_demo.asm",
            ),
        )
        for delivery_target, existing, replacement in substitutions:
            changed = deliveries[delivery_target]
            changed = {
                **changed,
                "inputs": [
                    replacement if path == existing else path
                    for path in changed["inputs"]
                ],
            }
            with self.subTest(
                delivery_target=delivery_target
            ), self.assertRaisesRegex(
                module.AuditError,
                r"CupidObj install-source delivery content inputs changed",
            ):
                module._validate_cupidobj_install_source_delivery(
                    ".", changed, expected_content[delivery_target]
                )

        near_match = module._build_transforms(
            ".",
            {target},
            {
                target: module.MakeRule(
                    prerequisites=transform["inputs"],
                    recipe=[
                        "$(CUPIDOBJ) install-sources bin "
                        "--bin $(BIN_CC_SRCS) -o $@"
                    ],
                )
            },
        )[0]
        self.assertEqual(near_match["operation"], "transform_object")

        misplaced_tokens = {
            "$(CUPIDOBJ) wrap-text bin/hello.cc -o install-source": (
                "wrap_text_as_elf32_relocatable"
            ),
            "echo install-source && "
            "$(CUPIDOBJ) wrap-text bin/hello.cc -o hello.o": (
                "wrap_text_as_elf32_relocatable"
            ),
            "echo $(CUPIDOBJ) install-source bin": "transform_object",
            "echo '; $(CUPIDOBJ) install-source bin'": "transform_object",
            'printf "x && $(CUPIDOBJ) install-source bin"': (
                "transform_object"
            ),
        }
        for recipe, expected_operation in misplaced_tokens.items():
            with self.subTest(recipe=recipe):
                misplaced = module._build_transforms(
                    ".",
                    {target},
                    {
                        target: module.MakeRule(
                            prerequisites=transform["inputs"],
                            recipe=[recipe],
                        )
                    },
                )[0]
                self.assertEqual(misplaced["operation"], expected_operation)

    def test_hosted_tool_builds_remain_optional(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        module = _load_audit_module()
        rules = module._parse_make_rules(
            module._run_make_database(REPO_ROOT, make, "all")
        )
        hosted_tools = [
            f"toolchain/build/{tool}.exe"
            for tool in ("cupidasm", "cupiddis", "cupidld", "cupidobj")
        ]
        self.assertTrue(
            set(hosted_tools).issubset(rules),
        )
        self.assertTrue(
            set(hosted_tools).isdisjoint(
                module._reachable_rules(rules, "all")
            )
        )

    def test_checked_cupidc_active_manifest_keeps_profiles_isolated(self):
        lines = ACTIVE_CASE_MANIFEST.read_text(encoding="utf-8").splitlines()
        roots = [line for line in lines if line.startswith("CUPIDC_PP_INCLUDE_ROOT(")]
        macros = [line for line in lines if line.startswith("CUPIDC_PP_MACRO(")]
        forced = [
            line for line in lines if line.startswith("CUPIDC_PP_FORCED_INCLUDE(")
        ]
        both_forms = (
            "(CTOOL_C_PP_INCLUDE_QUOTED | CTOOL_C_PP_INCLUDE_ANGLE)"
        )
        angle_forms = "CTOOL_C_PP_INCLUDE_ANGLE"
        kernel_roots = [
            "/kernel",
            "/kernel/audio",
            "/kernel/core",
            "/kernel/cpu",
            "/kernel/crypto",
            "/kernel/doom",
            "/kernel/fs",
            "/kernel/gfx",
            "/kernel/gui",
            "/kernel/lang",
            "/kernel/mm",
            "/kernel/network",
            "/kernel/smp",
            "/kernel/tls",
            "/kernel/usb",
            "/kernel/util",
            "/drivers",
            "/toolchain",
        ]
        expected_root_paths = {
            "KERNEL_I386": kernel_roots,
            "DOOM_COMPAT_I386": [
                *kernel_roots,
                "/kernel/doom/src",
                "/kernel/doom/src/include_stubs",
            ],
            "DOOM_TREE_I386": [
                *kernel_roots,
                "/kernel/doom/src",
                "/kernel/doom/src/include_stubs",
            ],
            "USER_I386": ["/user"],
            "FREESTANDING_I386": [],
            "CUPID_RUNTIME": [],
            "HOSTED_TOOLCHAIN_64": ["/toolchain"],
            "HOSTED_KERNEL_BRIDGE_64": ["/toolchain", "/kernel/lang"],
            "HOSTED_I386_LINUX": [
                "/toolchain",
                "/toolchain/hosted/i386-linux/include",
            ],
            "HOSTED_I386_WINDOWS": [
                "/toolchain",
                "/toolchain/hosted/i386-linux/include",
            ],
            "HOSTED_I386_KERNEL_BRIDGE": [
                "/toolchain",
                "/kernel/lang",
                "/toolchain/hosted/i386-linux/include",
            ],
            "HOSTED_I386_LINUX_GNU": [
                "/toolchain",
                "/toolchain/hosted/i386-linux/include",
            ],
        }
        expected_roots = {
            name: [
                (
                    path,
                    angle_forms
                    if name
                    in {
                        "HOSTED_I386_LINUX",
                        "HOSTED_I386_WINDOWS",
                        "HOSTED_I386_KERNEL_BRIDGE",
                        "HOSTED_I386_LINUX_GNU",
                    }
                    and path.endswith("/hosted/i386-linux/include")
                    else both_forms,
                )
                for path in paths
            ]
            for name, paths in expected_root_paths.items()
        }
        root_pattern = re.compile(
            r'^CUPIDC_PP_INCLUDE_ROOT\(([A-Z0-9_]+), "([^"]+)", '
            r"(.+)\)$"
        )
        actual_roots = {name: [] for name in expected_roots}
        for line in roots:
            match = root_pattern.fullmatch(line)
            self.assertIsNotNone(match, line)
            name, path, actual_forms = match.groups()
            actual_roots[name].append((path, actual_forms))
        self.assertEqual(actual_roots, expected_roots)

        self.assertEqual(
            roots[0],
            f'CUPIDC_PP_INCLUDE_ROOT(KERNEL_I386, "/kernel", {both_forms})',
        )
        common_macros = [
            ("__GNUC__", "1"),
            ("__SIZEOF_POINTER__", "4"),
            ("__ORDER_LITTLE_ENDIAN__", "1234"),
            ("__ORDER_BIG_ENDIAN__", "4321"),
            ("__ORDER_PDP_ENDIAN__", "3412"),
            ("__BYTE_ORDER__", "__ORDER_LITTLE_ENDIAN__"),
        ]
        expected_macros = {
            "KERNEL_I386": [
                *common_macros,
                ("__SSE2__", "1"),
                ("DEBUG", "1"),
            ],
            "DOOM_COMPAT_I386": [*common_macros, ("__SSE2__", "1")],
            "DOOM_TREE_I386": [
                *common_macros,
                ("__SSE2__", "1"),
                ("DEFAULT_SAVEGAMEDIR", '\"/home/doom/\"'),
                ("DOOM_PORT_CUPIDOS", "1"),
            ],
            "USER_I386": common_macros,
            "FREESTANDING_I386": [("__SIZEOF_POINTER__", "4")],
            "CUPID_RUNTIME": [],
            "HOSTED_TOOLCHAIN_64": [("__SIZEOF_POINTER__", "8")],
            "HOSTED_KERNEL_BRIDGE_64": [("__SIZEOF_POINTER__", "8")],
            "HOSTED_I386_LINUX": [("__SIZEOF_POINTER__", "4")],
            "HOSTED_I386_WINDOWS": [
                ("__SIZEOF_POINTER__", "4"),
                ("_WIN32", "1"),
            ],
            "HOSTED_I386_KERNEL_BRIDGE": [("__SIZEOF_POINTER__", "4")],
            "HOSTED_I386_LINUX_GNU": [("__SIZEOF_POINTER__", "4")],
        }

        def macro_line(profile, name, replacement):
            escaped_name = name.replace("\\", "\\\\").replace('"', '\\"')
            escaped_replacement = replacement.replace("\\", "\\\\").replace(
                '"', '\\"'
            )
            return (
                f'CUPIDC_PP_MACRO({profile}, "{escaped_name}", '
                f'"{escaped_replacement}")'
            )

        self.assertEqual(
            macros,
            [
                macro_line(profile, name, replacement)
                for profile in expected_macros
                for name, replacement in expected_macros[profile]
            ],
        )
        self.assertEqual(
            forced,
            [
                'CUPIDC_PP_FORCED_INCLUDE(DOOM_TREE_I386, '
                '"/kernel/doom/dglibc_compat.h")'
            ],
        )

    def test_hosted_i386_contract_profiles_fail_closed_at_the_c_seam(self):
        module = _load_audit_module()
        module._validate_hosted_i386_contract_profiles(REPO_ROOT)
        contract = (
            REPO_ROOT
            / "toolchain"
            / "tests"
            / "cupidc_object_contract.cc"
        ).read_text(encoding="utf-8")
        mutations = {
            "runtime loses GNU mode": (
                '"/toolchain/hosted/i386-linux/runtime.o", '
                "HOST_TOOL_SOURCE_C,\n       CTOOL_TRUE}",
                '"/toolchain/hosted/i386-linux/runtime.o", '
                "HOST_TOOL_SOURCE_C,\n       CTOOL_FALSE}",
                r"source-profile rows differ.*runtime\.c",
            ),
            "preprocessor ignores the selected mode": (
                "pp_request = profile->request;\n"
                "  pp_request.gnu_extensions = gnu_extensions;",
                "pp_request = profile->request;\n"
                "  pp_request.gnu_extensions = CTOOL_FALSE;",
                r"profile emitter does not forward the checked GNU mode",
            ),
            "compile loop ignores the selected mode": (
                "job, &source, &profile, "
                "source_cases[index].gnu_extensions,\n"
                "          compiled_objects[index]",
                "job, &source, &profile, CTOOL_FALSE,\n"
                "          compiled_objects[index]",
                r"compile loop does not consume each checked source profile",
            ),
        }
        for name, (old, new, message) in mutations.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                target = (
                    root
                    / "toolchain"
                    / "tests"
                    / "cupidc_object_contract.cc"
                )
                target.parent.mkdir(parents=True)
                mutated = contract.replace(old, new, 1)
                self.assertNotEqual(mutated, contract)
                target.write_text(mutated, encoding="utf-8")
                with self.assertRaisesRegex(module.AuditError, message):
                    module._validate_hosted_i386_contract_profiles(root)

    def test_cupid_toolchain_fixed_point_contract_fails_closed(self):
        module = _load_audit_module()
        contract = module._cupid_toolchain_fixed_point_contract(REPO_ROOT)
        self.assertEqual(contract["help_cases"], 5)
        self.assertEqual(contract["success_behavior_cases"], 18)
        self.assertEqual(contract["failure_behavior_cases"], 16)
        self.assertEqual(contract["contract_manifest_inputs"], 65)
        self.assertEqual(
            contract["source_head_capabilities"],
            [
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
        bootstrap = (
            REPO_ROOT / "tools" / "bootstrap_toolchain.py"
        ).read_text(encoding="utf-8")
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
        linker_cli = (
            REPO_ROOT / "toolchain" / "cupidld_main.cc"
        ).read_text(encoding="utf-8")
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
            '  if (status == CTOOL_OK) {\n'
            '    phase = "CupidLD executable serialization failed";\n'
            "    if (request->image_kind == CTOOL_LD_IMAGE_PE32_FIXED) {\n"
        )
        core_dispatch_end = linker_core.index(
            "\n  if (status != CTOOL_OK &&", core_dispatch_start
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
            "    for tool_name, link_objects in "
            "windows_native_tool_plans.items():\n"
        )
        native_build_loop_end = bootstrap.index(
            "\n\n    windows_native_tool_loaders:",
            native_build_loop_start,
        )
        native_build_loop_feature = bootstrap[
            native_build_loop_start:native_build_loop_end
        ]
        mutations = {
            "angle root widened": (
                "driver",
                "cli->include_forms[cli->include_count] = "
                "CTOOL_C_PP_INCLUDE_ANGLE;",
                "cli->include_forms[cli->include_count] = "
                "CTOOL_C_PP_INCLUDE_QUOTED | CTOOL_C_PP_INCLUDE_ANGLE;",
                r"does not retain its exact include contract",
            ),
            "forced include count disappears": (
                "driver",
                "pp_request.forced_include_count = "
                "context->forced_include_count;",
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
                '"--include-angle",\n'
                '    "/toolchain/hosted/i386-linux/include",',
                '"-I",\n'
                '    "/toolchain/hosted/i386-linux/include",',
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
                '            "x86",\n'
                "        ),\n"
                "    ),\n"
                "    (\n"
                '        "cupiddis",',
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
            "profile positive stops comparing stages": (
                "bootstrap",
                "    profile_result = _run_stage_pair(\n",
                "    profile_result = _run_one_stage(\n",
                r"fixed-point profile behavior differs",
            ),
            "truncated profile failure disappears": (
                "bootstrap",
                '        ("truncated", profile_payload[:-1], '
                '"snapshot is truncated"),\n',
                '        ("short-input", profile_payload[:-1], '
                '"snapshot is truncated"),\n',
                r"fixed-point profile behavior differs",
            ),
            "unsafe profile failure disappears": (
                "bootstrap",
                '            "unsafe-path",\n',
                '            "invalid-path",\n',
                r"fixed-point profile behavior differs",
            ),
            "profile case collision failure disappears": (
                "bootstrap",
                '            "case-collision",\n',
                '            "case-mismatch",\n',
                r"fixed-point profile behavior differs",
            ),
            "profile failures stop comparing stages": (
                "bootstrap",
                "        failure_result = _run_stage_pair(\n",
                "        failure_result = _run_one_stage(\n",
                r"fixed-point profile behavior differs",
            ),
            "profile failures stop checking diagnostics": (
                "bootstrap",
                "            or failure_message not in failure_result.stderr\n",
                "            or failure_result.stderr == \"\"\n",
                r"fixed-point profile behavior differs",
            ),
            "profile failures stop preserving the second output": (
                "bootstrap",
                "            or stage_three_profile_failure.read_bytes() "
                "!= sentinel\n",
                "            or stage_two_profile_failure.read_bytes() "
                "!= sentinel\n",
                r"fixed-point profile behavior differs",
            ),
            "PE32 positive stops comparing stages": (
                "bootstrap",
                "    pe32_result = _run_stage_pair(\n",
                "    pe32_result = _run_one_stage(\n",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 positive loses its output mode": (
                "bootstrap",
                '            "i386pe",\n'
                '            "--text-address",\n'
                '            "0x00401000",\n',
                '            "elf_i386",\n'
                '            "--text-address",\n'
                '            "0x00401000",\n',
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 positive compares the first output twice": (
                "bootstrap",
                "        or stage_two_pe32.read_bytes()\n"
                "        != stage_three_pe32.read_bytes()\n",
                "        or stage_two_pe32.read_bytes()\n"
                "        != stage_two_pe32.read_bytes()\n",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 staged parser disappears": (
                "bootstrap",
                "    _validate_static_i386_pe32(\n",
                "    _skip_static_i386_pe32_validation(\n",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 staged parser stops reading the image": (
                "bootstrap",
                "    data = path.read_bytes()\n"
                "    has_imports = bool(expected_imports)\n",
                '    data = b""\n'
                "    has_imports = bool(expected_imports)\n",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 staged parser accepts a changed DOS stub": (
                "bootstrap",
                '    "4d5a90000300000004000000ffff0000"\n',
                '    "4d5a91000300000004000000ffff0000"\n',
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 staged parser reads the checksum from the wrong field": (
                "bootstrap",
                "    checksum = read_u32("
                "optional_offset + 64, \"PE32 checksum\")\n",
                "    checksum = read_u32("
                "optional_offset + 60, \"PE32 checksum\")\n",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 staged parser allows writable read-only data": (
                "bootstrap",
                '        ".rodata": (1, 0x40000040),\n',
                '        ".rodata": (1, 0xC0000040),\n',
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 staged parser drops the empty-section guard": (
                "bootstrap",
                "        if virtual_size == 0:\n"
                "            raise BootstrapError("
                'f"{path.name} has an empty PE32 section")\n',
                "",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 staged parser guards only empty BSS": (
                "bootstrap",
                "        if virtual_size == 0:\n"
                "            raise BootstrapError("
                'f"{path.name} has an empty PE32 section")\n',
                '        if virtual_size == 0 and name == ".bss":\n'
                "            raise BootstrapError("
                'f"{path.name} has an empty PE32 section")\n',
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 staged parser skips the first header padding byte": (
                "bootstrap",
                "        data[section_table_end:headers_size]\n",
                "        data[section_table_end + 1:headers_size]\n",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 staged parser skips the first section padding byte": (
                "bootstrap",
                "            if any(data[raw_offset + virtual_size "
                ": raw_offset + raw_size]):\n",
                "            if any(data[raw_offset + virtual_size + 1 "
                ": raw_offset + raw_size]):\n",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 staged parser weakens the final file extent": (
                "bootstrap",
                "        or expected_raw_offset != len(data)\n",
                "        or expected_raw_offset > len(data)\n",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 staged parser expects the wrong entry": (
                "bootstrap",
                "    _validate_static_i386_pe32(\n"
                "        stage_two_pe32,\n"
                "        0x00401000,\n"
                "    )\n",
                "    _validate_static_i386_pe32(\n"
                "        stage_two_pe32,\n"
                "        0x00402000,\n"
                "    )\n",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 import parser resolves outside idata": (
                "bootstrap",
                "        ) = sections[\".idata\"]\n",
                "        ) = sections[\".text\"]\n",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 import parser accepts a displaced lookup table": (
                "bootstrap",
                "            if lookup_rva != idata_virtual_address + cursor:\n",
                "            if False:\n",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 import parser accepts trailing payload": (
                "bootstrap",
                "        if cursor != idata_virtual_size:\n",
                "        if cursor > idata_virtual_size:\n",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 failure stops comparing stages": (
                "bootstrap",
                "    invalid_pe32_result = _run_stage_pair(\n",
                "    invalid_pe32_result = _run_one_stage(\n",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 failure skips the second sentinel setup": (
                "bootstrap",
                "    stage_three_pe32_failure.write_bytes(sentinel)\n",
                "    stage_two_pe32_failure.write_bytes(sentinel)\n",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 failure uses the accepted text address": (
                "bootstrap",
                '            "i386pe",\n'
                '            "--text-address",\n'
                '            "0x00402000",\n',
                '            "i386pe",\n'
                '            "--text-address",\n'
                '            "0x00401000",\n',
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 failure loses its diagnostic": (
                "bootstrap",
                '        or "CupidLD PE32 requires text address 0x00401000"\n',
                '        or "CupidLD PE32 text address differs"\n',
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 failure stops preserving the second output": (
                "bootstrap",
                "        or stage_three_pe32_failure.read_bytes() "
                "!= sentinel\n",
                "        or stage_two_pe32_failure.read_bytes() "
                "!= sentinel\n",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 success count becomes stale": (
                "bootstrap",
                '        "success_cases": 18,\n',
                '        "success_cases": 17,\n',
                r"fixed-point behavior matrix differs",
            ),
            "PE32 failure count becomes stale": (
                "bootstrap",
                '        "failure_cases": 16,\n',
                '        "failure_cases": 15,\n',
                r"fixed-point behavior matrix differs",
            ),
            "PE32 import source leaves the frozen closure": (
                "bootstrap",
                'source_root / "toolchain/hosted/i386-windows/start.asm"',
                'source_root / "toolchain/hosted/i386-linux/start.asm"',
                r"fixed-point source freeze differs",
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
            "PE32 Windows assembly stops comparing stages": (
                "bootstrap",
                "    windows_assembly_result = _run_stage_pair(\n",
                "    windows_assembly_result = _run_one_stage(\n",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 Windows compile loses freestanding mode": (
                "bootstrap",
                '        "--freestanding",\n',
                '        "--gnu",\n',
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 Windows link stops comparing stages": (
                "bootstrap",
                "    windows_link_result = _run_stage_pair(\n",
                "    windows_link_result = _run_one_stage(\n",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 imported image skips validation": (
                "bootstrap",
                "    _validate_static_i386_pe32(\n"
                "        stage_two_windows_image,\n",
                "    _skip_static_i386_pe32_validation(\n"
                "        stage_two_windows_image,\n",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 native probe loses its timeout": (
                "bootstrap",
                "                timeout=10,\n",
                "                timeout=None,\n",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 native probe accepts stderr": (
                "bootstrap",
                "            or native_result.stderr\n",
                "            or False\n",
                r"fixed-point PE32 behavior differs",
            ),
            "Windows CupidDis stops comparing native output": (
                "bootstrap",
                "            or native_disassembly.stdout "
                "!= reference_disassembly.stdout\n",
                "            or False\n",
                r"fixed-point PE32 behavior differs",
            ),
            "Windows CupidDis accepts the wrong missing-input exit": (
                "bootstrap",
                "            or native_missing.returncode != 1\n",
                "            or False\n",
                r"fixed-point PE32 behavior differs",
            ),
            "Windows runtime contract stops checking output bytes": (
                "bootstrap",
                "            or contract_output.read_bytes() "
                "!= b\"headtail\"\n",
                "            or False\n",
                r"fixed-point PE32 behavior differs",
            ),
            "Windows runtime contract accepts the wrong negative exit": (
                "bootstrap",
                "            or native_contract_failure.returncode != 41\n",
                "            or False\n",
                r"fixed-point PE32 behavior differs",
            ),
            "Windows native tools stop comparing successful output": (
                "bootstrap",
                "                or native_output.read_bytes() "
                "!= reference_output.read_bytes()\n",
                "                or False\n",
                r"fixed-point PE32 behavior differs",
            ),
            "Windows native tools stop preserving failure output": (
                "bootstrap",
                "                or failure_output.read_bytes() != sentinel\n",
                "                or False\n",
                r"fixed-point PE32 behavior differs",
            ),
            "Windows CupidLD stops comparing published output": (
                "bootstrap",
                "            or native_cupidld_output.read_bytes()\n"
                "            != stage_two_windows_runtime_contract_image.read_bytes()\n",
                "            or False\n",
                r"fixed-point PE32 behavior differs",
            ),
            "Windows CupidLD stops checking failed-publication cleanup": (
                "bootstrap",
                "            or remaining_blocked_candidates\n",
                "            or False\n",
                r"fixed-point PE32 behavior differs",
            ),
            "Windows publication runtime compiles the wrong source": (
                "bootstrap",
                '            "/toolchain/hosted/i386-windows/'
                'publication_runtime.cc",\n',
                '            "/toolchain/hosted/i386-windows/runtime.cc",\n',
                r"fixed-point PE32 behavior differs",
            ),
            "Windows publication header redirects atomic replacement": (
                "windows_publication_header",
                "#define MoveFileExA cupid_windows_move_file_ex\n",
                "#define MoveFileExA cupid_windows_delete_file\n",
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
            "Windows native tool behavior moves under a dead block": (
                "bootstrap",
                '    if os.name == "nt":\n'
                "        windows_invalid_assembly = behavior_root / ",
                '    if os.name == "nt":\n'
                "        pass\n"
                "    if False:\n"
                "        windows_invalid_assembly = behavior_root / ",
                r"fixed-point PE32 behavior differs",
            ),
            "Windows helper link moves under a dead block": (
                "bootstrap",
                "    link_result = _run_stage_pair(\n",
                "    if False:\n"
                "        link_result = _run_stage_pair(\n",
                r"fixed-point PE32 behavior differs",
            ),
            "Windows helper uses the wrong platform define": (
                "bootstrap",
                '                "_WIN32=1",\n',
                '                "_WIN64=1",\n',
                r"fixed-point PE32 behavior differs",
            ),
            "Windows helper links the wrong image format": (
                "bootstrap",
                '            "i386pe",\n',
                '            "elf_i386",  # "i386pe",\n',
                r"fixed-point PE32 behavior differs",
            ),
            "Windows helper stops comparing compiled mains": (
                "bootstrap",
                "            or stage_two_object.read_bytes() "
                "!= stage_three_object.read_bytes()\n",
                "            or False  # stage_two_object.read_bytes() "
                "!= stage_three_object.read_bytes()\n",
                r"fixed-point PE32 behavior differs",
            ),
            "Windows helper stops comparing linked images": (
                "bootstrap",
                "        or stage_two_image.read_bytes() "
                "!= stage_three_image.read_bytes()\n",
                "        or False  # stage_two_image.read_bytes() "
                "!= stage_three_image.read_bytes()\n",
                r"fixed-point PE32 behavior differs",
            ),
            "Windows helper skips platform-sensitive mains": (
                "bootstrap",
                "    replacement_names = windows_sources.get(tool_name, ())\n",
                "    replacement_names = ()\n",
                r"fixed-point PE32 behavior differs",
            ),
            "Windows native image loop moves under a dead block": (
                "bootstrap",
                native_build_loop_feature,
                "    if False:\n"
                + textwrap.indent(native_build_loop_feature, "    "),
                r"fixed-point PE32 behavior differs",
            ),
            "Windows native image loop calls a dead helper": (
                "bootstrap",
                "            _build_windows_tool_image(\n",
                "            _skip_build_windows_tool_image(\n",
                r"fixed-point PE32 behavior differs",
            ),
            "Windows native execution loop becomes empty": (
                "bootstrap",
                "        ) in native_checks.items():\n",
                "        ) in ():\n",
                r"fixed-point PE32 behavior differs",
            ),
            "Windows CupidASM compares its reference with itself": (
                "bootstrap",
                "                behavior_root / \"native-cupidasm.bin\",\n"
                "                behavior_root / "
                "\"native-cupidasm-failure.bin\",\n"
                "                stage_two_binary,\n",
                "                stage_two_binary,\n"
                "                behavior_root / "
                "\"native-cupidasm-failure.bin\",\n"
                "                stage_two_binary,\n",
                r"fixed-point PE32 behavior differs",
            ),
            "Windows CupidDis substitutes help for raw disassembly": (
                "bootstrap",
                "        raw_arguments: list[str | Path] = [\n"
                '            "--raw",\n'
                '            "--mode",\n'
                '            "32",\n'
                '            "--base",\n'
                '            "0",\n'
                "            windows_cupiddis_input,\n"
                "        ]\n",
                '        raw_arguments: list[str | Path] = ["--help"]\n',
                r"fixed-point PE32 behavior differs",
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
                "static int allocator_contract(void) {\n"
                "  unsigned char *allocation;\n",
                "static int allocator_contract(void) {\n"
                "  return 0;\n"
                "  unsigned char *allocation;\n",
                r"Windows runtime contract differs",
            ),
            "Windows runtime main returns before its contract": (
                "windows_runtime_contract",
                "  int index;\n"
                "  if (argc != 7",
                "  int index;\n"
                "  return 0;\n"
                "  if (argc != 7",
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
            "Windows native execution loop skips every tool": (
                "bootstrap",
                "        ) in native_checks.items():\n"
                "            failure_output.write_bytes(sentinel)\n",
                "        ) in native_checks.items():\n"
                "            continue\n"
                "            failure_output.write_bytes(sentinel)\n",
                r"fixed-point PE32 behavior differs",
            ),
            "Windows CupidDis overwrites native evidence": (
                "bootstrap",
                "        if (\n"
                "            reference_disassembly.returncode != 0\n",
                "        native_disassembly = reference_disassembly\n"
                "        if (\n"
                "            reference_disassembly.returncode != 0\n",
                r"fixed-point PE32 behavior differs",
            ),
            "Windows native tools overwrite output evidence": (
                "bootstrap",
                "            if (\n"
                "                reference_help.returncode != 0\n",
                "            native_output.write_bytes(\n"
                "                reference_output.read_bytes()\n"
                "            )\n"
                "            if (\n"
                "                reference_help.returncode != 0\n",
                r"fixed-point PE32 behavior differs",
            ),
            "Windows runtime contract manufactures output evidence": (
                "bootstrap",
                "        if (\n"
                "            native_contract.returncode != 0\n",
                "        contract_output.write_bytes(b\"headtail\")\n"
                "        if (\n"
                "            native_contract.returncode != 0\n",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 direct IAT failure stops comparing stages": (
                "bootstrap",
                "    invalid_import_result = _run_stage_pair(\n",
                "    invalid_import_result = _run_one_stage(\n",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 direct IAT failure loses its diagnostic": (
                "bootstrap",
                '        or "IAT symbols require an absolute '
                'zero-addend relocation"\n',
                '        or "IAT relocation differs"\n',
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 direct IAT failure loses the second sentinel": (
                "bootstrap",
                "        or stage_three_invalid_import_image.read_bytes() "
                "!= sentinel\n",
                "        or stage_two_invalid_import_image.read_bytes() "
                "!= sentinel\n",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 direct IAT fixture collapses assembler outputs": (
                "bootstrap",
                "            stage_three_invalid_import_object,\n"
                "        ],\n"
                "    )\n"
                "    _expect_status(\n"
                "        invalid_import_assembly_result, 0,",
                "            stage_two_invalid_import_object,\n"
                "        ],\n"
                "    )\n"
                "    _expect_status(\n"
                "        invalid_import_assembly_result, 0,",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 direct IAT failure uses a stage-specific input": (
                "bootstrap",
                "            invalid_import_object,\n"
                "        ],\n"
                "    )\n"
                "    _expect_status(\n"
                "        invalid_import_result, 1,",
                "            stage_three_invalid_import_object,\n"
                "        ],\n"
                "    )\n"
                "    _expect_status(\n"
                "        invalid_import_result, 1,",
                r"fixed-point PE32 behavior differs",
            ),
            "PE32 public enum is only a near match": (
                "linker_header",
                "  CTOOL_LD_IMAGE_PE32_FIXED\n"
                "} ctool_ld_image_kind_t;\n",
                "  CTOOL_LD_IMAGE_PE32_FIXEDS\n"
                "} ctool_ld_image_kind_t;\n",
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
                '    taken = cupidld_take_value(argc, argv, &index, '
                'argument, "--import",\n',
                '    taken = cupidld_take_value(argc, argv, &index, '
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
                "  request.image_kind = strcmp(cli.machine, \"i386pe\") == 0\n"
                "                           ? CTOOL_LD_IMAGE_PE32_FIXED\n"
                "                           : CTOOL_LD_IMAGE_ELF32;\n",
                "  /* request.image_kind = "
                "strcmp(cli.machine, \"i386pe\") == 0\n"
                "                           ? CTOOL_LD_IMAGE_PE32_FIXED\n"
                "                           : CTOOL_LD_IMAGE_ELF32; */\n"
                "  request.image_kind = CTOOL_LD_IMAGE_ELF32;\n",
                r"fixed-point PE32 source contract differs",
            ),
            "PE32 CLI bypasses atomic publication": (
                "linker_cli",
                "    status = cupidld_publish_output("
                "native_paths[output_native_index],\n"
                "                                    ctool_buffer_view(output));\n",
                "    status = ctool_job_write(job, &output_path,\n"
                "                             ctool_buffer_view(output));\n",
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
                "candidate, destination);\n"
                + publication_verify_guard,
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
                "  return cupidld_publication_replace("
                "destination, destination);\n",
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
                "    result_out->output_section_count = "
                "emitted_section_count;\n",
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
                "  section->flags = CTOOL_ELF32_SHF_ALLOC | "
                "CTOOL_ELF32_SHF_WRITE;\n",
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
            "checked source closure is not frozen": (
                "bootstrap",
                "        source_inputs = freeze_source_inputs(\n",
                "        source_inputs = capture_source_inputs(\n",
                r"fixed-point source freeze differs",
            ),
            "checked stage runs from the live root": (
                "bootstrap",
                "        runner = ToolRunner(private_source_root)\n",
                "        runner = ToolRunner(source_root)\n",
                r"fixed-point source freeze differs",
            ),
            "checked private root aliases the live root": (
                "bootstrap",
                "        private_source_root = source_inputs.root\n",
                "        private_source_root = source_root\n",
                r"fixed-point source freeze differs",
            ),
            "checked stage output leaves the private root": (
                "bootstrap",
                "            private_source_root / \"stage-two\",\n",
                "            output_root / \"stage-two\",\n",
                r"fixed-point source freeze differs",
            ),
            "checked closure boundary rehash disappears": (
                "bootstrap",
                "        require_source_closures("
                "source_inputs, source_root, plan)\n",
                "        require_source_snapshot(\n"
                "            source_root, plan, source_inputs.inventory\n"
                "        )\n",
                r"fixed-point source freeze differs",
            ),
            "checked closure stops rehashing the private root": (
                "bootstrap",
                "    require_frozen_source_snapshot(source_inputs, plan)\n",
                "    require_source_snapshot(\n"
                "        live_source_root, plan, source_inputs.inventory\n"
                "    )\n",
                r"fixed-point source freeze differs",
            ),
            "checked behavior returns to the live root": (
                "bootstrap",
                "        behavior = _run_behavior_checks(\n"
                "            runner,\n"
                "            private_source_root,\n"
                "            private_source_root,",
                "        behavior = _run_behavior_checks(\n"
                "            runner,\n"
                "            source_root,\n"
                "            private_source_root,",
                r"fixed-point source freeze differs",
            ),
            "checked report leaves the private root": (
                "bootstrap",
                "        report_path = "
                "private_source_root / \"bootstrap-report.json\"\n",
                "        report_path = "
                "output_root / \"bootstrap-report.json\"\n",
                r"fixed-point source freeze differs",
            ),
            "checked bundle leaves the private workspace": (
                "bootstrap",
                "        publication_root = "
                "private_workspace / \"publication\"\n",
                "        publication_root = "
                "output_root / \"publication\"\n",
                r"fixed-point source freeze differs",
            ),
            "checked bundle moves public stage paths": (
                "bootstrap",
                "            (private_source_root / name).replace(\n",
                "            (output_root / name).replace(\n",
                r"fixed-point source freeze differs",
            ),
            "checked output bypasses gated publication": (
                "bootstrap",
                "        publish_bootstrap_outputs("
                "publication_root, output_root)\n",
                "        publication_root.replace(output_root)\n",
                r"fixed-point source freeze differs",
            ),
        }
        for name, (target_name, old, new, message) in mutations.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                driver_target = root / "toolchain" / "cupidc_main.cc"
                test_target = (
                    root / "tests" / "test_toolchain_cupidc_object.py"
                )
                bootstrap_target = (
                    root / "tools" / "bootstrap_toolchain.py"
                )
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
                driver_payload = driver
                test_payload = test
                bootstrap_payload = bootstrap
                contract_publisher_payload = contract_publisher
                windows_runtime_contract_payload = windows_runtime_contract
                windows_publication_header_payload = windows_publication_header
                windows_publication_runtime_payload = windows_publication_runtime
                windows_publication_start_payload = windows_publication_start
                linker_header_payload = linker_header
                linker_cli_payload = linker_cli
                linker_core_payload = linker_core
                if target_name == "driver":
                    driver_payload = driver_payload.replace(old, new, 1)
                    self.assertNotEqual(driver_payload, driver)
                elif target_name == "test":
                    test_payload = test_payload.replace(old, new, 1)
                    self.assertNotEqual(test_payload, test)
                elif target_name == "bootstrap":
                    bootstrap_payload = bootstrap_payload.replace(
                        old, new, 1
                    )
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
                        windows_runtime_contract_payload.replace(old, new, 1)
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
                        windows_publication_runtime_payload.replace(old, new, 1)
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
                    linker_cli_payload = linker_cli_payload.replace(old, new, 1)
                    self.assertNotEqual(linker_cli_payload, linker_cli)
                else:
                    self.assertEqual(target_name, "linker_core")
                    linker_core_payload = linker_core_payload.replace(old, new, 1)
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

    def test_cupidc_active_manifest_fails_closed_on_compile_recipe_shape(self):
        module = _load_audit_module()

        def audit(inputs, recipe, sources=None):
            return {
                "build": {
                    "directory": ".",
                    "transforms": [
                        {
                            "output": "unit.o",
                            "inputs": inputs,
                            "tools": ["host_c_compiler"],
                            "operation": "compile_c_to_elf32_object",
                            "recipe": [recipe],
                        }
                    ],
                },
                "supplemental_builds": [],
                "sources": [] if sources is None else sources,
            }

        def hosted_audit(
            recipe,
            source="toolchain/unit.c",
            *,
            origin=None,
        ):
            return {
                "build": {"directory": ".", "transforms": []},
                "supplemental_builds": [
                    {
                        "directory": "toolchain",
                        "transforms": [
                            {
                                "output": "build/unit.o",
                                "inputs": [source],
                                "tools": ["host_c_compiler"],
                                "operation": "compile_c_to_host_object",
                                "recipe": [recipe],
                            }
                        ],
                    }
                ],
                "sources": (
                    []
                    if origin is None
                    else [{"path": source, "origin": origin}]
                ),
            }

        def contract_cohort_audit(
            *,
            include_seed=True,
            operation="host_orchestration",
            tool="host_python",
            recipe=None,
        ):
            inputs = [
                path[1:]
                for path in (
                    module._C_PP_HOSTED_I386_STRICT_CASES
                    + module._C_PP_HOSTED_I386_GNU_CASES
                    + module._C_PP_TOOLCHAIN_CONTRACT_CASES
                )
            ]
            inputs.extend(
                (
                    "bootstrap/seeds/i386-linux/manifest.json",
                    "bootstrap/seeds/i386-linux/cupidasm.elf",
                    "bootstrap/seeds/i386-linux/cupiddis.elf",
                    "bootstrap/seeds/i386-linux/cupidld.elf",
                    "bootstrap/seeds/i386-linux/cupidobj.elf",
                    "link.ld",
                    "toolchain/hosted/i386-linux/start.asm",
                    "toolchain/hosted/i386-windows/start.asm",
                    "toolchain/hosted/i386-windows/tool_start.asm",
                    "toolchain/Makefile",
                    "tools/bootstrap_toolchain.py",
                    "tools/cupidc_toolchain_contracts.py",
                )
            )
            inputs.extend(
                path
                for path in module.USER_SYSCALL_ABI_AUDIT_INPUTS
                if path not in inputs
                and (
                    include_seed
                    or path != "bootstrap/seeds/i386-linux/cupidc.elf"
                )
            )
            if include_seed and (
                "bootstrap/seeds/i386-linux/cupidc.elf" not in inputs
            ):
                inputs.append("bootstrap/seeds/i386-linux/cupidc.elf")
            return {
                "build": {"directory": ".", "transforms": []},
                "supplemental_builds": [
                    {
                        "directory": "toolchain",
                        "transforms": [
                            {
                                "output": (
                                    "toolchain/build/"
                                    "cupidc-contracts/manifest.json"
                                ),
                                "inputs": inputs,
                                "tools": [tool],
                                "operation": operation,
                                "recipe": (
                                    [
                                        "$(PYTHON) "
                                        "../tools/"
                                        "cupidc_toolchain_contracts.py "
                                        "build \\",
                                        "--root .. --manifest "
                                        "../bootstrap/seeds/i386-linux/"
                                        "manifest.json \\",
                                        "--output $(CONTRACT_DIR)",
                                    ]
                                    if recipe is None
                                    else recipe
                                ),
                            }
                        ],
                    }
                ],
                "sources": [],
            }

        cases = {
            "unknown marker": (
                audit(["unit.c"], "$(CC) $(NEW_CFLAGS) -c $< -o $@"),
                r"unknown recipe marker.*NEW_CFLAGS",
            ),
            "brace-form unknown marker": (
                audit(["unit.c"], "${CC} ${NEW_CFLAGS} -c $< -o $@"),
                r"unknown recipe marker.*NEW_CFLAGS",
            ),
            "escaped shell marker": (
                audit(["unit.c"], "$(CC) $(CFLAGS) $${NEW_CFLAGS} -c $< -o $@"),
                r"unmodeled recipe dollar reference",
            ),
            "computed marker": (
                audit(["unit.c"], "$(CC) $(CFLAGS) $(value NEW_CFLAGS) -c $< -o $@"),
                r"unmodeled recipe Make reference/function",
            ),
            "zero roots": (
                audit(["unit.h"], "$(CC) $(CFLAGS) -c $< -o $@"),
                r"exactly one C translation-unit root.*found 0",
            ),
            "multiple roots": (
                audit(
                    ["first.c", "second.c"],
                    "$(CC) $(CFLAGS) -c $< -o $@",
                ),
                r"exactly one C translation-unit root.*found 2",
            ),
            "literal macro": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) -DLOCAL=1 -c $< -o $@",
                ),
                r"literal preprocessor flag.*-DLOCAL=1",
            ),
            "literal contradictory target": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) -m64 -c $< -o $@",
                ),
                r"literal preprocessor flag.*-m64",
            ),
            "literal driver pass-through": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) -Wp,-DLOCAL=1 -c $< -o $@",
                ),
                r"literal preprocessor flag.*-Wp,-DLOCAL=1",
            ),
            "literal language mode": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) -xc++ -c $< -o $@",
                ),
                r"literal preprocessor flag.*-xc\+\+",
            ),
            "literal character mode": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) -funsigned-char -c $< -o $@",
                ),
                r"literal preprocessor flag.*-funsigned-char",
            ),
            "literal predefined accelerator macro": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) -fopenacc -c $< -o $@",
                ),
                r"literal preprocessor flag.*-fopenacc",
            ),
            "compiler response file": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) @extra.rsp -c $< -o $@",
                ),
                r"literal preprocessor flag.*@extra.rsp",
            ),
            "recipe environment include path": (
                audit(
                    ["unit.c"],
                    "CPATH=private $(CC) $(CFLAGS) -c $< -o $@",
                ),
                r"literal preprocessor flag.*CPATH=private",
            ),
            "hosted recipe omits include profile": (
                hosted_audit("$(CC) $(CFLAGS) -c $< -o $@"),
                r"hosted recipe markers differ.*CPPFLAGS",
            ),
            "hosted recipe duplicates include profile": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) $(CPPFLAGS) $(CFLAGS) "
                    "-c $< -o $@"
                ),
                r"hosted recipe markers differ.*CPPFLAGS",
            ),
            "root recipe duplicates compiler marker": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CC) $(CFLAGS) -c $< -o $@",
                ),
                r"recipe markers differ.*CC",
            ),
            "root recipe duplicates profile marker": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) $(CFLAGS) -c $< -o $@",
                ),
                r"recipe markers differ.*CFLAGS",
            ),
            "hosted bridge flag moves to ordinary source": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) -I../kernel/lang $(CFLAGS) "
                    "-c $< -o $@"
                ),
                r"hosted bridge recipe differs.*toolchain/unit\.c",
            ),
            "hosted bridge source loses its include flag": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@",
                    "kernel/lang/as_elf.cc",
                ),
                r"hosted bridge recipe differs.*kernel/lang/as_elf\.cc",
            ),
            "renamed hosted bridge loses its C language mode": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) -I../kernel/lang $(CFLAGS) "
                    "-c $< -o $@",
                    "kernel/lang/as_elf.cc",
                ),
                r"hosted bridge recipe differs.*kernel/lang/as_elf\.cc",
            ),
            "renamed hosted bridge selects C++ mode": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) -I../kernel/lang $(CFLAGS) "
                    "-x c++ -c $< -o $@",
                    "kernel/lang/as_elf.cc",
                ),
                r"compiler argument profile differs"
                r".*kernel/lang/as_elf\.cc",
            ),
            "renamed hosted source loses its C language mode": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@",
                    "toolchain/ctool.cc",
                ),
                r"hosted bridge recipe differs.*toolchain/ctool\.cc",
            ),
            "renamed hosted source selects C++ mode": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) $(CFLAGS) -x c++ -c $< -o $@",
                    "toolchain/ctool.cc",
                ),
                r"compiler argument profile differs.*toolchain/ctool\.cc",
            ),
            "hosted bridge include precedes common include roots": (
                hosted_audit(
                    "$(CC) -I../kernel/lang $(CPPFLAGS) $(CFLAGS) -x c "
                    "-c $< -o $@",
                    "kernel/lang/as_elf.cc",
                ),
                r"compiler argument profile differs"
                r".*kernel/lang/as_elf\.cc",
            ),
            "hosted include marker is shell quoted": (
                hosted_audit(
                    '$(CC) "$(CPPFLAGS)" $(CFLAGS) -c $< -o $@'
                ),
                r"compiler argument profile differs.*toolchain/unit\.c",
            ),
            "hosted command substitution injects flags": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) $(CFLAGS) `cat extra.flags` "
                    "-c $< -o $@"
                ),
                r"compile recipe has unmodeled shell substitution",
            ),
            "hosted markers hidden in shell comment": (
                hosted_audit(
                    "$(CC) $(CFLAGS) -c $< -o $@ # $(CPPFLAGS)"
                ),
                r"compile recipe contains a shell comment",
            ),
            "hosted markers belong to a different command": (
                hosted_audit(
                    "echo $(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@"
                ),
                r"compile recipe does not invoke.*CC",
            ),
            "optional hosted root is absent from source inventory": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@",
                    "toolchain/ctool_host.cc",
                ),
                r"root is absent from source inventory.*toolchain/ctool_host\.cc",
            ),
            "optional hosted root has generated origin": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@",
                    "toolchain/ctool_host.cc",
                    origin="generated",
                ),
                r"generated root has non-kernel profile"
                r".*toolchain/ctool_host\.cc",
            ),
            "contract cohort omits source closure": (
                {
                    "build": {"directory": ".", "transforms": []},
                    "supplemental_builds": [
                        {
                            "directory": "toolchain",
                            "transforms": [
                                {
                                    "output": (
                                        "toolchain/build/"
                                        "cupidc-contracts/manifest.json"
                                    ),
                                    "inputs": ["toolchain/ctool.cc"],
                                    "tools": ["host_python"],
                                    "operation": "host_orchestration",
                                    "recipe": ["checked orchestration"],
                                }
                            ],
                        }
                    ],
                    "sources": [],
                },
                r"toolchain contract closure changed; missing=",
            ),
            "contract cohort loses compiler seed": (
                contract_cohort_audit(include_seed=False),
                r"contract cohort lost checked inputs.*cupidc\.elf",
            ),
            "contract cohort changes subcommand": (
                contract_cohort_audit(
                    recipe=[
                        "$(PYTHON) "
                        "../tools/cupidc_toolchain_contracts.py other "
                    ]
                ),
                r"recipe no longer invokes the checked fixed-point builder",
            ),
            "contract cohort changes orchestrator": (
                contract_cohort_audit(tool="host_shell"),
                r"cohort transform differs from the checked orchestration",
            ),
        }
        for name, (synthetic, message) in cases.items():
            with self.subTest(name=name), self.assertRaisesRegex(
                module.AuditError, message
            ):
                module._c_preprocessor_active_cases_manifest(synthetic)

        malformed_wrap = {
            "build": {
                "directory": ".",
                "transforms": [
                    {
                        "output": "bin/malformed.o",
                        "inputs": None,
                        "tools": ["cupid_object"],
                        "operation": "wrap_text_as_elf32_relocatable",
                        "recipe": ["$(CUPIDOBJ) wrap-text $< -o $@"],
                    }
                ],
            },
            "supplemental_builds": [],
            "sources": [],
        }
        with self.assertRaisesRegex(
            module.AuditError, r"delivery transform inputs are absent"
        ):
            module._c_preprocessor_active_cases_manifest(malformed_wrap)

        for delivered_path in ("bin/unit.cc", "bin/unit.h"):
            binary_text_delivery = {
                "build": {
                    "directory": ".",
                    "transforms": [
                        {
                            "output": f"{delivered_path}.o",
                            "inputs": [delivered_path],
                            "tools": ["cupid_object"],
                            "operation": "wrap_binary_as_elf32_relocatable",
                            "recipe": ["$(CUPIDOBJ) wrap $< -o $@"],
                        }
                    ],
                },
                "supplemental_builds": [],
                "sources": [],
            }
            with self.subTest(delivered_path=delivered_path), \
                    self.assertRaisesRegex(
                        module.AuditError,
                        r"unclassified Cupid delivery transform: bin/unit\.(?:cc|h)\.o "
                        r"\(wrap_binary_as_elf32_relocatable\)",
                    ):
                module._c_preprocessor_active_cases_manifest(
                    binary_text_delivery
                )

    def test_cupidc_kernel_wrapper_has_a_closed_compile_recipe(self):
        module = _load_audit_module()
        transform = {
            "output": "kernel/crypto/aes.o",
            "inputs": [
                "kernel/crypto/aes.cc",
                "kernel/crypto/aes.h",
                "kernel/core/types.h",
            ],
            "tools": ["cupid_c_compiler", "host_python"],
            "operation": "compile_c_to_elf32_object",
            "recipe": [
                "$(CUPIDC_KERNEL_COMPILE) "
                "--source kernel/crypto/aes.cc "
                "--output kernel/crypto/aes.o"
            ],
        }
        self.assertEqual(
            module._c_preprocessor_profile_for_c_transform(".", transform),
            "KERNEL_I386",
        )

        changed = dict(transform)
        changed["recipe"] = [
            "$(CUPIDC_KERNEL_COMPILE) "
            "--source kernel/crypto/aes.cc "
            "--output build/aes.o"
        ]
        with self.assertRaisesRegex(
            module.AuditError, r"wrapper arguments differ"
        ):
            module._c_preprocessor_profile_for_c_transform(".", changed)

        doom_compat = {
            "output": "kernel/doom/dglibc.o",
            "inputs": [
                "kernel/doom/dglibc.cc",
                "kernel/doom/dglibc.h",
            ],
            "tools": ["cupid_c_compiler", "host_python"],
            "operation": "compile_c_to_elf32_object",
            "recipe": [
                "$(CUPIDC_KERNEL_COMPILE) --profile doom-compat "
                "--source kernel/doom/dglibc.cc "
                "--output kernel/doom/dglibc.o"
            ],
        }
        self.assertEqual(
            module._c_preprocessor_profile_for_c_transform(
                ".", doom_compat
            ),
            "DOOM_COMPAT_I386",
        )

        doom_tree = {
            "output": "kernel/doom/src/am_map.o",
            "inputs": [
                "kernel/doom/src/am_map.cc",
                "kernel/doom/src/am_map.h",
            ],
            "tools": ["cupid_c_compiler", "host_python"],
            "operation": "compile_c_to_elf32_object",
            "recipe": [
                "$(CUPIDC_KERNEL_COMPILE) --profile doom-tree "
                "--source $< --output $@"
            ],
        }
        self.assertEqual(
            module._c_preprocessor_profile_for_c_transform(".", doom_tree),
            "DOOM_TREE_I386",
        )

        wrong_doom_profile = dict(doom_compat)
        wrong_doom_profile["recipe"] = [
            "$(CUPIDC_KERNEL_COMPILE) --profile doom-tree "
            "--source kernel/doom/dglibc.cc "
            "--output kernel/doom/dglibc.o"
        ]
        with self.assertRaisesRegex(
            module.AuditError, r"wrapper arguments differ"
        ):
            module._c_preprocessor_profile_for_c_transform(
                ".", wrong_doom_profile
            )

    def test_checked_cupidc_active_manifest_classifies_non_roots_and_hosted(self):
        lines = ACTIVE_CASE_MANIFEST.read_text(encoding="utf-8").splitlines()
        include_only_pattern = re.compile(
            r'^CUPIDC_PP_INCLUDE_ONLY\("([^"]+)", "([^"]+)"\)$'
        )
        non_root_pattern = re.compile(
            r'^CUPIDC_PP_NON_ROOT\("([^"]+)", "([^"]+)"\)$'
        )
        deferred_pattern = re.compile(
            r'^CUPIDC_PP_DEFERRED_HOSTED\("([^"]+)", "([^"]+)"\)$'
        )
        include_only = [
            match.groups()
            for line in lines
            if (match := include_only_pattern.fullmatch(line)) is not None
        ]
        non_roots = [
            match.groups()
            for line in lines
            if (match := non_root_pattern.fullmatch(line)) is not None
        ]
        deferred = [
            match.groups()
            for line in lines
            if (match := deferred_pattern.fullmatch(line)) is not None
        ]

        self.assertEqual(len(include_only), 22)
        self.assertTrue(
            all(owner == "/bin/browser.cc" for _, owner in include_only)
        )
        self.assertEqual(
            [path for path, _ in non_roots],
            ["/bin/fat16.h", "/bin/shell.h"],
        )
        self.assertEqual(deferred, [])
        self.assertTrue(all(reason for _, reason in [*non_roots, *deferred]))

    def test_cupidc_active_manifest_renderer_is_grouped_and_c_escaped(self):
        module = _load_audit_module()
        manifest = module.CPreprocessorActiveCasesManifest(
            profiles=(
                module.CPreprocessorProfile(
                    name="SYNTH",
                    mode="CTOOL_C_PP_MODE_C11",
                    gnu_extensions="CTOOL_TRUE",
                    hosted_environment="CTOOL_FALSE",
                    implicit_function_declarations="CTOOL_FALSE",
                    compatibility_pointer_conversions="CTOOL_FALSE",
                ),
            ),
            include_roots=(
                (
                    "SYNTH",
                    '/root/??/"quoted"\\tab\n\N{GREEK CAPITAL LETTER OMEGA}',
                    "CTOOL_C_PP_INCLUDE_QUOTED",
                ),
            ),
            macros=(("SYNTH", 'A"B', "line\n\\end"),),
            forced_includes=(("SYNTH", "/forced.h"),),
            active_cases=(("SYNTH", "/active.c"),),
            generated_cases=(("SYNTH", "/generated.c"),),
            include_only=(("/fragment.cc", "/owner.cc"),),
            non_roots=(("/header.h", "delivered header"),),
            deferred_hosted=(("/host.c", "host-only contract"),),
        )

        first = module._render_c_preprocessor_active_cases(manifest)
        second = module._render_c_preprocessor_active_cases(manifest)
        self.assertEqual(first, second)
        self.assertIn(
            "CUPIDC_PP_PROFILE(SYNTH, CTOOL_C_PP_MODE_C11, CTOOL_TRUE, "
            "CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE)",
            first,
        )
        self.assertIn(
            'CUPIDC_PP_INCLUDE_ROOT(SYNTH, '
            '"/root/\\?\\?/\\\"quoted\\\"\\\\tab\\012\\316\\251", '
            "CTOOL_C_PP_INCLUDE_QUOTED)",
            first,
        )
        self.assertIn(
            'CUPIDC_PP_MACRO(SYNTH, "A\\\"B", '
            '"line\\012\\\\end")',
            first,
        )
        row_prefixes = [
            "CUPIDC_PP_PROFILE(",
            "CUPIDC_PP_INCLUDE_ROOT(",
            "CUPIDC_PP_MACRO(",
            "CUPIDC_PP_FORCED_INCLUDE(",
            "CUPIDC_PP_ACTIVE_CASE(",
            "CUPIDC_PP_GENERATED_CASE(",
            "CUPIDC_PP_INCLUDE_ONLY(",
            "CUPIDC_PP_NON_ROOT(",
            "CUPIDC_PP_DEFERRED_HOSTED(",
        ]
        self.assertEqual(
            [
                next(index for index, line in enumerate(first.splitlines())
                     if line.startswith(prefix))
                for prefix in row_prefixes
            ],
            sorted(
                next(index for index, line in enumerate(first.splitlines())
                     if line.startswith(prefix))
                for prefix in row_prefixes
            ),
        )

    def test_cupidc_active_manifest_check_rejects_drift(self):
        with tempfile.TemporaryDirectory() as td:
            output = Path(td) / "audit.json"
            summary = Path(td) / "audit.md"
            manifest = Path(td) / "cupidc_pp_active_cases.inc"
            command = [
                sys.executable,
                str(AUDIT_TOOL),
                "--root",
                str(REPO_ROOT),
                "--supplemental-build",
                "user:all",
                "--supplemental-build",
                "toolchain:all",
                "--output",
                str(output),
                "--summary",
                str(summary),
                "--c-preprocessor-active-cases",
                str(manifest),
            ]
            generated = subprocess.run(command, text=True, capture_output=True)
            self.assertEqual(generated.returncode, 0, generated.stderr)
            self.assertTrue(summary.read_text(encoding="utf-8").endswith("\n\n"))
            audit_payload = json.loads(output.read_text(encoding="utf-8"))
            control_paths = {
                entry["path"]
                for entry in audit_payload["provenance"]["control_files"]
            }
            self.assertTrue(
                {
                    "tools/cupidc_kernel_compile.py",
                    "tools/kernel_cupidc_frontier.py",
                    "tools/bootstrap_toolchain.py",
                    "bootstrap/seeds/i386-linux/manifest.json",
                }.issubset(control_paths)
            )
            contract = audit_payload["contracts"][
                "c_preprocessor_translation_units"
            ]
            self.assertEqual(
                {
                    key: contract[key]
                    for key in (
                        "status",
                        "tracked_translation_units",
                        "generated_translation_units",
                        "total_translation_units",
                        "include_only_fragments",
                        "delivered_non_root_headers",
                        "deferred_hosted_translation_units",
                        "deferred_external_header_units",
                        "deferred_hermetic_units",
                    )
                },
                {
                    "status": "pass",
                    "tracked_translation_units": 393,
                    "generated_translation_units": 4,
                    "total_translation_units": 397,
                    "include_only_fragments": 22,
                    "delivered_non_root_headers": 2,
                    "deferred_hosted_translation_units": 0,
                    "deferred_external_header_units": 0,
                    "deferred_hermetic_units": 0,
                },
            )
            self.assertEqual(
                [
                    (
                        profile["name"],
                        profile["tracked_translation_units"],
                        profile["generated_translation_units"],
                    )
                    for profile in contract["profiles"]
                ],
                [
                    ("KERNEL_I386", 155, 4),
                    ("DOOM_COMPAT_I386", 3, 0),
                    ("DOOM_TREE_I386", 80, 0),
                    ("USER_I386", 3, 0),
                    ("FREESTANDING_I386", 1, 0),
                    ("CUPID_RUNTIME", 107, 0),
                    ("HOSTED_TOOLCHAIN_64", 0, 0),
                    ("HOSTED_KERNEL_BRIDGE_64", 0, 0),
                    ("HOSTED_I386_LINUX", 33, 0),
                    ("HOSTED_I386_WINDOWS", 6, 0),
                    ("HOSTED_I386_KERNEL_BRIDGE", 2, 0),
                    ("HOSTED_I386_LINUX_GNU", 3, 0),
                ],
            )
            self.assertEqual(
                {
                    profile["name"]:
                        profile["implicit_function_declarations"]
                    for profile in contract["profiles"]
                },
                {
                    "KERNEL_I386": False,
                    "DOOM_COMPAT_I386": True,
                    "DOOM_TREE_I386": True,
                    "USER_I386": False,
                    "FREESTANDING_I386": False,
                    "CUPID_RUNTIME": False,
                    "HOSTED_TOOLCHAIN_64": False,
                    "HOSTED_KERNEL_BRIDGE_64": False,
                    "HOSTED_I386_LINUX": False,
                    "HOSTED_I386_WINDOWS": False,
                    "HOSTED_I386_KERNEL_BRIDGE": False,
                    "HOSTED_I386_LINUX_GNU": False,
                },
            )
            self.assertEqual(
                {
                    profile["name"]:
                        profile["compatibility_pointer_conversions"]
                    for profile in contract["profiles"]
                },
                {
                    "KERNEL_I386": False,
                    "DOOM_COMPAT_I386": True,
                    "DOOM_TREE_I386": True,
                    "USER_I386": False,
                    "FREESTANDING_I386": False,
                    "CUPID_RUNTIME": False,
                    "HOSTED_TOOLCHAIN_64": False,
                    "HOSTED_KERNEL_BRIDGE_64": False,
                    "HOSTED_I386_LINUX": False,
                    "HOSTED_I386_WINDOWS": False,
                    "HOSTED_I386_KERNEL_BRIDGE": False,
                    "HOSTED_I386_LINUX_GNU": False,
                },
            )
            self.assertEqual(
                audit_payload["summary"],
                {
                    "active_sources": 735,
                    "features": 255,
                    "transforms": 450,
                    "unreachable_sources": 25,
                },
            )
            features = {
                entry["id"]: entry for entry in audit_payload["features"]
            }
            expected_c_expression_inventory = {
                "c.declaration.static_assert": (28, 5),
                "c.expression.sizeof": (6016, 172),
                "c.extension.builtin.offsetof": (12, 6),
                "c.extension.gnu_alignof": (1, 1),
            }
            for feature_id, expected_counts in (
                expected_c_expression_inventory.items()
            ):
                feature = features[feature_id]
                self.assertEqual(
                    (feature["occurrences"], len(feature["files"])),
                    expected_counts,
                )
            self.assertEqual(
                features["c.extension.gnu_alignof"]["files"],
                ["kernel/core/process.cc"],
            )
            self.assertEqual(
                features["c.extension.gnu_alignof"]["examples"][0]["line"],
                42,
            )
            root_transform_by_output = {
                transform["output"]: transform
                for transform in audit_payload["build"]["transforms"]
            }
            iso_transform = root_transform_by_output["test_iso/hello.iso"]
            self.assertEqual(
                iso_transform["tools"],
                ["cupid_object", "host_python"],
            )
            self.assertEqual(
                iso_transform["operation"],
                "package_iso9660_image",
            )
            self.assertEqual(
                set(iso_transform["inputs"]),
                {
                    "Makefile",
                    "bootstrap/seeds/i386-linux/cupidasm.elf",
                    "bootstrap/seeds/i386-linux/cupidc.elf",
                    "bootstrap/seeds/i386-linux/cupiddis.elf",
                    "bootstrap/seeds/i386-linux/cupidld.elf",
                    "bootstrap/seeds/i386-linux/cupidobj.elf",
                    "bootstrap/seeds/i386-linux/manifest.json",
                    "tools/bootstrap_toolchain.py",
                    "tools/hostbuild.py",
                    "test_iso/fixtures",
                    "test_iso/fixtures.manifest",
                    "test_iso/fixtures/big.bin",
                    "test_iso/fixtures/gen_big.sh",
                    "test_iso/fixtures/jpeg_baseline_8x8.jpg",
                    "test_iso/fixtures/long_named_file.txt",
                    "test_iso/fixtures/readme.txt",
                    "test_iso/fixtures/sub",
                    "test_iso/fixtures/sub/nested.txt",
                },
            )
            big_fixture_transform = root_transform_by_output[
                "test_iso/fixtures/big.bin"
            ]
            self.assertEqual(
                big_fixture_transform["tools"],
                ["cupid_assembler", "host_python"],
            )
            self.assertEqual(
                big_fixture_transform["operation"],
                "assemble_flat_binary",
            )
            self.assertEqual(
                set(big_fixture_transform["inputs"]),
                {
                    "Makefile",
                    "bootstrap/seeds/i386-linux/cupidasm.elf",
                    "bootstrap/seeds/i386-linux/cupidc.elf",
                    "bootstrap/seeds/i386-linux/cupiddis.elf",
                    "bootstrap/seeds/i386-linux/cupidld.elf",
                    "bootstrap/seeds/i386-linux/cupidobj.elf",
                    "bootstrap/seeds/i386-linux/manifest.json",
                    "test_iso/big_pattern.asm",
                    "tools/bootstrap_toolchain.py",
                    "tools/hostbuild.py",
                },
            )
            system_image_transform = root_transform_by_output[
                "cupidos.img"
            ]
            self.assertEqual(
                system_image_transform["operation"],
                "package_disk_image",
            )
            self.assertIn(
                "test_iso/hello.iso",
                system_image_transform["inputs"],
            )
            checked_cupidc_roots = []
            for transform in root_transform_by_output.values():
                if (
                    transform["tools"]
                    != ["cupid_c_compiler", "host_python"]
                    or transform["operation"]
                    != "compile_c_to_elf32_object"
                ):
                    continue
                roots = [
                    path
                    for path in transform["inputs"]
                    if Path(path).suffix in {".c", ".cc"}
                ]
                self.assertEqual(len(roots), 1, transform["output"])
                checked_cupidc_roots.extend(roots)
            seed_bound_roots = {
                "toolchain/ctool.cc",
                "toolchain/cupidasm.cc",
                "toolchain/cupiddis.cc",
                "toolchain/elf32.cc",
                "toolchain/x86.cc",
            }
            self.assertEqual(len(checked_cupidc_roots), 242)
            self.assertEqual(
                {
                    path
                    for path in checked_cupidc_roots
                    if Path(path).suffix == ".c"
                },
                set(),
            )
            self.assertTrue(
                seed_bound_roots.issubset(set(checked_cupidc_roots))
            )
            self.assertEqual(
                sum(
                    Path(path).suffix == ".cc"
                    for path in checked_cupidc_roots
                ),
                242,
            )
            symbol_transform = root_transform_by_output[
                "kernel/cpu/ksyms_data.cc"
            ]
            self.assertEqual(
                symbol_transform["tools"],
                ["cupid_disassembler", "cupid_object", "host_python"],
            )
            self.assertEqual(
                symbol_transform["operation"], "generate_ksyms_source"
            )
            for checked_seed_input in (
                "tools/bootstrap_toolchain.py",
                "bootstrap/seeds/i386-linux/manifest.json",
                "bootstrap/seeds/i386-linux/cupidasm.elf",
                "bootstrap/seeds/i386-linux/cupidc.elf",
                "bootstrap/seeds/i386-linux/cupiddis.elf",
                "bootstrap/seeds/i386-linux/cupidld.elf",
                "bootstrap/seeds/i386-linux/cupidobj.elf",
            ):
                self.assertIn(
                    checked_seed_input,
                    symbol_transform["inputs"],
                )
            self.assertFalse(
                any(
                    "host_symbol_reader" in transform["tools"]
                    for transform in audit_payload["build"]["transforms"]
                )
            )
            established_cupidc_kernel_sources = (
                "kernel/crypto/aes.cc",
                "kernel/crypto/aes_gcm.cc",
                "kernel/crypto/asn1.cc",
                "kernel/crypto/bigint.cc",
                "kernel/crypto/chacha20.cc",
                "kernel/crypto/chacha20poly1305.cc",
                "kernel/crypto/csprng.cc",
                "kernel/crypto/ct.cc",
                "kernel/crypto/ecdsa.cc",
                "kernel/crypto/ed25519.cc",
                "kernel/crypto/hkdf.cc",
                "kernel/crypto/hmac.cc",
                "kernel/crypto/p256.cc",
                "kernel/crypto/poly1305.cc",
                "kernel/crypto/rsa.cc",
                "kernel/crypto/sha256.cc",
                "kernel/crypto/sha512.cc",
                "kernel/crypto/x25519.cc",
                "kernel/crypto/x509.cc",
                "kernel/crypto/x509_chain.cc",
                "drivers/e1000.cc",
                "kernel/gui/desktop.cc",
                "kernel/network/socket.cc",
                "kernel/network/tcp.cc",
                "kernel/smp/acpi.cc",
                "kernel/smp/mp_tables.cc",
            )
            port_io_header_closures = {
                "drivers/ata.cc": (
                    "drivers/ata.h",
                    "kernel/core/debug.h",
                    "kernel/core/kernel.h",
                    "kernel/core/ports.h",
                    "kernel/core/types.h",
                    "kernel/cpu/isr.h",
                    "kernel/fs/blockdev.h",
                ),
                "drivers/keyboard.cc": (
                    "drivers/keyboard.h",
                    "drivers/rtc.h",
                    "drivers/serial.h",
                    "drivers/vga.h",
                    "kernel/core/kernel.h",
                    "kernel/core/ports.h",
                    "kernel/core/process.h",
                    "kernel/core/types.h",
                    "kernel/cpu/irq.h",
                    "kernel/cpu/isr.h",
                    "kernel/gui/desktop.h",
                    "kernel/gui/gui.h",
                    "kernel/lang/shell.h",
                    "kernel/util/calendar.h",
                ),
                "drivers/mouse.cc": (
                    "drivers/mouse.h",
                    "drivers/serial.h",
                    "drivers/vga.h",
                    "kernel/core/ports.h",
                    "kernel/core/string.h",
                    "kernel/core/types.h",
                    "kernel/cpu/isr.h",
                    "kernel/cpu/pic.h",
                    "kernel/gfx/graphics.h",
                ),
                "drivers/pci.cc": (
                    "drivers/pci.h",
                    "drivers/serial.h",
                    "kernel/core/ports.h",
                    "kernel/core/types.h",
                ),
                "drivers/pit.cc": (
                    "drivers/pit.h",
                    "kernel/core/ports.h",
                    "kernel/core/types.h",
                ),
                "drivers/rtc.cc": (
                    "drivers/rtc.h",
                    "drivers/serial.h",
                    "kernel/core/kernel.h",
                    "kernel/core/ports.h",
                    "kernel/core/types.h",
                    "kernel/cpu/isr.h",
                ),
                "drivers/rtl8139.cc": (
                    "drivers/pci.h",
                    "drivers/serial.h",
                    "kernel/core/ports.h",
                    "kernel/core/types.h",
                    "kernel/cpu/irq.h",
                    "kernel/cpu/isr.h",
                    "kernel/mm/memory.h",
                    "kernel/network/net_if.h",
                ),
                "drivers/speaker.cc": (
                    "drivers/pit.h",
                    "drivers/speaker.h",
                    "drivers/timer.h",
                    "kernel/core/kernel.h",
                    "kernel/core/ports.h",
                    "kernel/core/types.h",
                    "kernel/cpu/isr.h",
                ),
                "drivers/vga.cc": (
                    "drivers/timer.h",
                    "drivers/vga.h",
                    "kernel/core/kernel.h",
                    "kernel/core/ports.h",
                    "kernel/core/string.h",
                    "kernel/core/types.h",
                    "kernel/cpu/isr.h",
                    "kernel/cpu/simd.h",
                    "kernel/mm/memory.h",
                ),
                "kernel/audio/ac97.cc": (
                    "drivers/pci.h",
                    "drivers/serial.h",
                    "kernel/audio/ac97.h",
                    "kernel/core/kernel.h",
                    "kernel/core/ports.h",
                    "kernel/core/types.h",
                    "kernel/cpu/irq.h",
                    "kernel/cpu/isr.h",
                    "kernel/mm/memory.h",
                ),
                "kernel/core/syscall.cc": (
                    "drivers/ata.h",
                    "drivers/pci.h",
                    "drivers/pit.h",
                    "drivers/serial.h",
                    "drivers/speaker.h",
                    "drivers/timer.h",
                    "kernel/core/kernel.h",
                    "kernel/core/ports.h",
                    "kernel/core/process.h",
                    "kernel/core/string.h",
                    "kernel/core/syscall.h",
                    "kernel/core/types.h",
                    "kernel/cpu/isr.h",
                    "kernel/fs/blockdev.h",
                    "kernel/fs/vfs.h",
                    "kernel/fs/vfs_helpers.h",
                    "kernel/lang/exec.h",
                    "kernel/lang/shell.h",
                    "kernel/mm/memory.h",
                    "kernel/network/arp.h",
                    "kernel/network/dns.h",
                    "kernel/network/icmp.h",
                    "kernel/network/ip.h",
                    "kernel/network/net_if.h",
                    "kernel/network/socket.h",
                    "kernel/network/udp.h",
                    "kernel/smp/bkl.h",
                    "kernel/smp/lapic.h",
                ),
                "kernel/lang/shell.cc": (
                    "drivers/keyboard.h",
                    "drivers/pci.h",
                    "drivers/rtc.h",
                    "drivers/serial.h",
                    "drivers/timer.h",
                    "drivers/vga.h",
                    "kernel/core/app_launch.h",
                    "kernel/core/assert.h",
                    "kernel/core/kernel.h",
                    "kernel/core/panic.h",
                    "kernel/core/ports.h",
                    "kernel/core/process.h",
                    "kernel/core/string.h",
                    "kernel/core/types.h",
                    "kernel/cpu/irq.h",
                    "kernel/cpu/isr.h",
                    "kernel/cpu/math.h",
                    "kernel/fs/blockcache.h",
                    "kernel/fs/blockdev.h",
                    "kernel/fs/fat16.h",
                    "kernel/fs/fs.h",
                    "kernel/fs/vfs.h",
                    "kernel/gfx/gfx2d.h",
                    "kernel/gui/ansi.h",
                    "kernel/gui/desktop.h",
                    "kernel/gui/gui.h",
                    "kernel/gui/gui_themes.h",
                    "kernel/gui/terminal_app.h",
                    "kernel/lang/as.h",
                    "kernel/lang/cupidc.h",
                    "kernel/lang/cupidscript.h",
                    "kernel/lang/cupidscript_arrays.h",
                    "kernel/lang/cupidscript_jobs.h",
                    "kernel/lang/cupidscript_streams.h",
                    "kernel/lang/dis.h",
                    "kernel/lang/exec.h",
                    "kernel/lang/shell.h",
                    "kernel/mm/memory.h",
                    "kernel/mm/swap.h",
                    "kernel/network/arp.h",
                    "kernel/network/dns.h",
                    "kernel/network/icmp.h",
                    "kernel/network/ip.h",
                    "kernel/network/net_if.h",
                    "kernel/network/socket.h",
                    "kernel/network/sshd.h",
                    "kernel/smp/bkl.h",
                    "kernel/smp/percpu.h",
                    "kernel/smp/smp.h",
                    "kernel/usb/usb.h",
                    "kernel/usb/usb_hc.h",
                    "kernel/util/calendar.h",
                ),
                "kernel/usb/ehci.cc": (
                    "drivers/pci.h",
                    "drivers/serial.h",
                    "drivers/timer.h",
                    "kernel/core/kernel.h",
                    "kernel/core/panic.h",
                    "kernel/core/ports.h",
                    "kernel/core/types.h",
                    "kernel/cpu/irq.h",
                    "kernel/cpu/isr.h",
                    "kernel/mm/memory.h",
                    "kernel/usb/usb.h",
                    "kernel/usb/usb_hc.h",
                ),
                "kernel/usb/uhci.cc": (
                    "drivers/pci.h",
                    "drivers/serial.h",
                    "drivers/timer.h",
                    "kernel/core/kernel.h",
                    "kernel/core/panic.h",
                    "kernel/core/ports.h",
                    "kernel/core/types.h",
                    "kernel/cpu/irq.h",
                    "kernel/cpu/isr.h",
                    "kernel/mm/memory.h",
                    "kernel/usb/usb.h",
                    "kernel/usb/usb_hc.h",
                ),
            }
            self.assertEqual(len(port_io_header_closures), 14)
            floating_gfx_header_closures = {
                "kernel/gfx/glyph_raster.cc": (
                    "kernel/core/string.h",
                    "kernel/core/types.h",
                    "kernel/gfx/glyph_raster.h",
                    "kernel/mm/memory.h",
                ),
                "kernel/gfx/jpeg.cc": (
                    "kernel/core/types.h",
                    "kernel/cpu/libm.h",
                    "kernel/gfx/jpeg.h",
                    "kernel/mm/memory.h",
                ),
            }
            libm_header_closures = {
                "kernel/cpu/libm.cc": (
                    "kernel/core/types.h",
                    "kernel/cpu/libm.h",
                ),
            }
            fpu_smp_header_closures = {
                "kernel/cpu/fpu.cc": (
                    "drivers/serial.h",
                    "kernel/core/panic.h",
                    "kernel/core/process.h",
                    "kernel/core/types.h",
                    "kernel/cpu/fpu.h",
                    "kernel/cpu/isr.h",
                    "kernel/cpu/libm.h",
                ),
                "kernel/smp/percpu.cc": (
                    "drivers/serial.h",
                    "kernel/core/process.h",
                    "kernel/core/types.h",
                    "kernel/smp/percpu.h",
                ),
                "kernel/smp/smp.cc": (
                    "drivers/serial.h",
                    "kernel/core/process.h",
                    "kernel/core/types.h",
                    "kernel/cpu/fpu.h",
                    "kernel/cpu/idt.h",
                    "kernel/cpu/isr.h",
                    "kernel/mm/memory.h",
                    "kernel/smp/acpi.h",
                    "kernel/smp/bkl.h",
                    "kernel/smp/ioapic.h",
                    "kernel/smp/lapic.h",
                    "kernel/smp/mp_tables.h",
                    "kernel/smp/percpu.h",
                    "kernel/smp/smp.h",
                ),
            }
            cupidc_control_inputs = (
                "Makefile",
                "tools/cupidc_kernel_compile.py",
                "tools/kernel_cupidc_frontier.py",
                "tools/bootstrap_toolchain.py",
                "bootstrap/seeds/i386-linux/manifest.json",
                "bootstrap/seeds/i386-linux/cupidasm.elf",
                "bootstrap/seeds/i386-linux/cupidc.elf",
                "bootstrap/seeds/i386-linux/cupiddis.elf",
                "bootstrap/seeds/i386-linux/cupidld.elf",
                "bootstrap/seeds/i386-linux/cupidobj.elf",
            )
            closed_header_closures = {
                **port_io_header_closures,
                **floating_gfx_header_closures,
                **libm_header_closures,
                **fpu_smp_header_closures,
            }
            for source_path, headers in closed_header_closures.items():
                output_path = Path(source_path).with_suffix(".o").as_posix()
                with self.subTest(port_io_closure=source_path):
                    self.assertEqual(
                        root_transform_by_output[output_path]["inputs"],
                        [source_path, *headers, *cupidc_control_inputs],
                    )

            cupidc_kernel_sources = (
                *established_cupidc_kernel_sources,
                *port_io_header_closures,
                *floating_gfx_header_closures,
                *libm_header_closures,
                *fpu_smp_header_closures,
            )
            for source_path in cupidc_kernel_sources:
                output_path = Path(source_path).with_suffix(".o").as_posix()
                with self.subTest(cupidc_kernel_source=source_path):
                    transform = root_transform_by_output[output_path]
                    self.assertEqual(
                        transform["tools"],
                        ["cupid_c_compiler", "host_python"],
                    )
                    self.assertEqual(
                        transform["operation"],
                        "compile_c_to_elf32_object",
                    )
                    self.assertIn(source_path, transform["inputs"])

            all_transforms = [
                *audit_payload["build"]["transforms"],
                *[
                    transform
                    for build in audit_payload["supplemental_builds"]
                    for transform in build["transforms"]
                ],
            ]
            self.assertEqual(
                {
                    tool: sum(
                        tool in transform["tools"]
                        for transform in all_transforms
                    )
                    for tool in (
                        "cupid_c_compiler",
                        "host_c_compiler",
                        "host_python",
                    )
                },
                {
                    "cupid_c_compiler": 245,
                    "host_c_compiler": 0,
                    "host_python": 450,
                },
            )

            toolchain_cohort = next(
                cohort
                for cohort in audit_payload["roadmap"]["source_cohort_order"]
                if cohort["id"] == "toolchain_sources"
            )
            self.assertEqual(toolchain_cohort["source_count"], 85)
            user_program_cohort = next(
                cohort
                for cohort in audit_payload["roadmap"]["source_cohort_order"]
                if cohort["id"] == "user_programs"
            )
            self.assertEqual(user_program_cohort["source_count"], 4)
            self.assertEqual(
                user_program_cohort["rationale"],
                "Keep the checked-seed CupidC and CupidLD user build "
                "reproducible on Linux and Windows, keep the native Windows "
                "oracle explicit, then stage its validated executables "
                "deliberately.",
            )

            source_by_path = {
                source["path"]: source for source in audit_payload["sources"]
            }
            self.assertEqual(
                source_by_path["kernel/lang/as.cc"]["cohort"],
                "cupidasm",
            )
            for source_path in cupidc_kernel_sources:
                with self.subTest(cupidc_owned_source=source_path):
                    self.assertEqual(
                        source_by_path[source_path]["runtime_owner"],
                        "CupidC",
                    )
                    self.assertIn(
                        "cupid_c_compiler",
                        source_by_path[source_path]["build_owners"],
                    )
                    self.assertNotIn(
                        "host_c_compiler",
                        source_by_path[source_path]["build_owners"],
                    )
            self.assertEqual(
                source_by_path["kernel/crypto/aes.h"]["runtime_owner"],
                "CupidC",
            )
            frontend_sources = {
                "toolchain/cupidc_emit.cc":
                    ("toolchain_core", "CupidC"),
                "toolchain/cupidc_emit.h":
                    ("toolchain_core", "CupidC"),
                "toolchain/cupidc_frontend.cc":
                    ("toolchain_core", "CupidC"),
                "toolchain/cupidc_frontend.h":
                    ("toolchain_core", "CupidC"),
                "toolchain/cupidc_ir.cc":
                    ("toolchain_core", "CupidC"),
                "toolchain/cupidc_ir.h":
                    ("toolchain_core", "CupidC"),
                "toolchain/cupidc_main.cc":
                    ("toolchain_core", "CupidC"),
                "toolchain/hosted/i386-linux/runtime.cc":
                    ("toolchain_core", "CupidC"),
                "toolchain/hosted/i386-linux/start.asm":
                    ("toolchain_core", None),
                "toolchain/hosted/i386-windows/runtime.cc":
                    ("toolchain_core", "CupidC"),
                "toolchain/hosted/i386-windows/start.asm":
                    ("toolchain_core", None),
                "toolchain/hosted/i386-windows/tool_start.asm":
                    ("toolchain_core", None),
                "toolchain/tests/core_contract.cc":
                    ("toolchain_contract", "CupidC"),
                "toolchain/tests/cupidasm_contract.cc":
                    ("toolchain_contract", "CupidC"),
                "toolchain/tests/cupidasm_demos_contract.cc":
                    ("toolchain_contract", "CupidC"),
                "toolchain/tests/cupidasm_kernel_elf_contract.cc":
                    ("toolchain_contract", "CupidC"),
                "toolchain/tests/cupidc_frontend_contract.cc":
                    ("toolchain_contract", "CupidC"),
                "toolchain/tests/cupidc_ir_contract.cc":
                    ("toolchain_contract", "CupidC"),
                "toolchain/tests/cupidc_object_contract.cc":
                    ("toolchain_contract", "CupidC"),
                "toolchain/tests/cupidc_pp_contract.cc":
                    ("toolchain_contract", "CupidC"),
                "toolchain/tests/cupidc_type_contract.cc":
                    ("toolchain_contract", "CupidC"),
                "toolchain/tests/cupiddis_contract.cc":
                    ("toolchain_contract", "CupidC"),
                "toolchain/tests/cupidld_contract.cc":
                    ("toolchain_contract", "CupidC"),
                "toolchain/tests/cupidobj_contract.cc":
                    ("toolchain_contract", "CupidC"),
                "toolchain/tests/elf32_contract.cc":
                    ("toolchain_contract", "CupidC"),
                "toolchain/tests/x86_contract.cc":
                    ("toolchain_contract", "CupidC"),
                "toolchain/tests/hosted_i386_runtime_contract.cc":
                    ("toolchain_contract", "CupidC"),
                "toolchain/tests/hosted_i386_windows_contract.cc":
                    ("toolchain_contract", "CupidC"),
                "toolchain/tests/hosted_i386_windows_runtime_contract.cc":
                    ("toolchain_contract", "CupidC"),
            }
            for path, (cohort, runtime_owner) in frontend_sources.items():
                with self.subTest(path=path):
                    self.assertEqual(source_by_path[path]["cohort"], cohort)
                    self.assertEqual(
                        source_by_path[path]["reachability"],
                        "direct_build_input",
                    )
                    self.assertEqual(
                        source_by_path[path]["runtime_owner"],
                        runtime_owner,
                    )
            self.assertEqual(
                source_by_path[
                    "toolchain/tests/hosted_i386_runtime_contract.cc"
                ]["language"],
                "cupid_c",
            )
            self.assertNotIn(
                "toolchain/tests/hosted_i386_runtime_contract.c",
                source_by_path,
            )

            toolchain_build = next(
                build
                for build in audit_payload["supplemental_builds"]
                if build["directory"] == "toolchain"
            )
            self.assertEqual(len(toolchain_build["transforms"]), 2)
            toolchain_transform_by_output = {
                transform["output"]: transform
                for transform in toolchain_build["transforms"]
            }
            self.assertNotIn("toolchain/build", toolchain_transform_by_output)
            contract_verifier = toolchain_transform_by_output[
                "toolchain/all"
            ]
            self.assertEqual(
                contract_verifier["operation"], "host_orchestration"
            )
            self.assertEqual(contract_verifier["tools"], ["host_python"])
            self.assertEqual(
                contract_verifier["inputs"],
                ["toolchain/build/cupidc-contracts/manifest.json"],
            )
            self.assertIn(
                "cupidc_toolchain_contracts.py verify",
                " ".join(contract_verifier["recipe"]),
            )
            contract_manifest = toolchain_transform_by_output[
                "toolchain/build/cupidc-contracts/manifest.json"
            ]
            self.assertEqual(
                contract_manifest["operation"], "host_orchestration"
            )
            self.assertEqual(contract_manifest["tools"], ["host_python"])
            for input_path in (
                "toolchain/hosted/i386-linux/runtime.cc",
                "toolchain/hosted/i386-linux/start.asm",
                "toolchain/hosted/i386-windows/runtime.cc",
                "toolchain/hosted/i386-windows/start.asm",
                "toolchain/hosted/i386-windows/tool_start.asm",
                "link.ld",
                "toolchain/tests/hosted_i386_runtime_contract.cc",
                "toolchain/tests/hosted_i386_windows_contract.cc",
                "toolchain/tests/hosted_i386_windows_runtime_contract.cc",
                "toolchain/cupidc_main.cc",
                "toolchain/tests/cupidc_object_contract.cc",
                "toolchain/tests/user_syscall_abi_contract.cc",
                "tools/cupidc_toolchain_contracts.py",
                "tools/user_syscall_abi.py",
                "user/cupid.h",
                "bootstrap/seeds/i386-linux/cupidc.elf",
            ):
                with self.subTest(contract_input=input_path):
                    self.assertIn(input_path, contract_manifest["inputs"])
            self.assertNotIn(
                "host_c_compiler",
                {
                    tool
                    for transform in toolchain_build["transforms"]
                    for tool in transform["tools"]
                },
            )
            self.assertIn(
                "`c_preprocessor_translation_units` | `pass` | "
                "393 tracked + 4 generated",
                summary.read_text(encoding="utf-8"),
            )
            audit_payload["build"]["transforms"].append(
                {
                    "output": "bin/new.h.o",
                    "inputs": ["bin/new.h"],
                    "tools": ["cupid_object", "host_python"],
                    "operation": "wrap_text_as_elf32_relocatable",
                    "recipe": ["$(CUPIDOBJ) wrap-text $< -o $@"],
                }
            )
            module = _load_audit_module()
            with self.assertRaisesRegex(
                module.AuditError,
                r"CupidObj install-source delivery content inputs changed",
            ):
                module._c_preprocessor_active_cases_manifest(audit_payload)
            checked = subprocess.run(
                [*command, "--check"], text=True, capture_output=True
            )
            self.assertEqual(checked.returncode, 0, checked.stderr)

            manifest.write_text(
                manifest.read_text(encoding="utf-8") + "/* stale */\n",
                encoding="utf-8",
            )
            stale = subprocess.run(
                [*command, "--check"], text=True, capture_output=True
            )
            self.assertEqual(stale.returncode, 1)
            self.assertIn(manifest.name, stale.stderr)

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
                root / "fake_hostbuild.py",
                """
                import sys
                from pathlib import Path

                Path("calls.log").write_text("validate-code\\n", encoding="utf-8")
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
                BOOTSTRAP_SEED_MANIFEST := seed.json
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
                    $(CUPIDDIS_PRODUCTION_INPUT_MANIFEST) fake_hostbuild.py \\
                    seed.json FORCE
                \t$(PYTHON) fake_hostbuild.py validate-code \\
                \t\t--seed-manifest $(BOOTSTRAP_SEED_MANIFEST) --root . \\
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
                "validate-code\n",
            )
            arguments = (root / "arguments.log").read_text(
                encoding="utf-8"
            ).splitlines()
            self.assertEqual(
                arguments,
                [
                    "validate-code",
                    "--seed-manifest",
                    "seed.json",
                    "--root",
                    ".",
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
                "KERNEL",
                "CUPIDDIS_PRODUCTION_INPUT_MANIFEST",
                "CUPIDDIS_PRODUCTION_INPUTS",
            ),
        )
        for variable, tool_name in (
            ("CUPIDASM", "cupidasm"),
            ("CUPIDOBJ", "cupidobj"),
            ("CUPIDLD", "cupidld"),
            ("CUPIDDIS", "cupiddis"),
        ):
            with self.subTest(command=variable):
                command = " ".join(commands[variable].split())
                self.assertIn(
                    "tools/bootstrap_toolchain.py run",
                    command,
                )
                self.assertIn(f"--tool {tool_name} --", command)

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

        root_transforms = audit["build"]["transforms"]
        root_tools = {
            tool
            for transform in root_transforms
            for tool in transform["tools"]
        }
        self.assertNotIn("host_c_compiler", root_tools)
        self.assertFalse(
            any(
                "kernel/gui/terminal_ansi.c" in transform["inputs"]
                for transform in root_transforms
            )
        )
        terminal_ansi = {
            source["path"]: source
            for source in audit["unreachable_sources"]
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
            "bootstrap/seeds/i386-linux/manifest.json",
            "bootstrap/seeds/i386-linux/cupidasm.elf",
            "bootstrap/seeds/i386-linux/cupidc.elf",
            "bootstrap/seeds/i386-linux/cupiddis.elf",
            "bootstrap/seeds/i386-linux/cupidld.elf",
            "bootstrap/seeds/i386-linux/cupidobj.elf",
        }
        object_outputs = {
            transform["output"]
            for transform in audit["build"]["transforms"]
            if transform["output"].endswith(".o")
        }
        self.assertEqual(len(object_outputs), 427)
        validated_code_inputs = object_outputs | {
            "kernel/kernel.elf.pass1",
            "kernel/kernel.elf",
        }
        self.assertEqual(len(validated_code_inputs), 429)
        declared_code_inputs = commands[
            "CUPIDDIS_PRODUCTION_INPUTS"
        ].split()
        input_manifest = commands["CUPIDDIS_PRODUCTION_INPUT_MANIFEST"]
        manifest_code_inputs = (
            REPO_ROOT / input_manifest
        ).read_text(encoding="utf-8").splitlines()
        self.assertEqual(len(declared_code_inputs), 429)
        self.assertEqual(len(set(declared_code_inputs)), 429)
        self.assertEqual(manifest_code_inputs, declared_code_inputs)
        self.assertEqual(len(manifest_code_inputs), 429)
        self.assertEqual(len(set(manifest_code_inputs)), 429)
        self.assertEqual(set(declared_code_inputs), validated_code_inputs)
        validation_command = " ".join(
            (
                commands["PYTHON"],
                "tools/hostbuild.py",
                "validate-code",
                "--seed-manifest",
                commands["BOOTSTRAP_SEED_MANIFEST"],
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
            ["cupid_disassembler", "cupid_object", "host_python"],
        )
        self.assertEqual(
            kernel_binary_transform["operation"], "extract_raw_binary"
        )
        self.assertEqual(
            set(kernel_binary_transform["inputs"]),
            validated_code_inputs
            | seed_inputs
            | {input_manifest, "tools/hostbuild.py"},
        )
        self.assertEqual(
            set(kernel_binary_transform["inputs"])
            & validated_code_inputs,
            set(manifest_code_inputs),
        )
        self.assertEqual(
            kernel_binary_transform["recipe"],
            [
                "$(PYTHON) tools/hostbuild.py validate-code \\",
                "--seed-manifest $(BOOTSTRAP_SEED_MANIFEST) --root . \\",
                "--input-manifest $(CUPIDDIS_PRODUCTION_INPUT_MANIFEST) \\",
                "--output $(KERNEL)",
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
                "bootstrap/seeds/i386-linux/cupidasm.elf",
                "bootstrap/seeds/i386-linux/cupidc.elf",
                "bootstrap/seeds/i386-linux/cupiddis.elf",
                "bootstrap/seeds/i386-linux/cupidld.elf",
                "bootstrap/seeds/i386-linux/cupidobj.elf",
                "bootstrap/seeds/i386-linux/manifest.json",
                "kernel/kernel.bin",
                "test_iso/hello.iso",
                "tools/bootstrap_toolchain.py",
                "tools/hostbuild.py",
                "verify-artifact-sizes",
            },
        )
        profile_manifest_transform = next(
            transform
            for transform in audit["build"]["transforms"]
            if transform["output"]
            == "build/bootstrap/doom-cupidc-inputs.json"
        )
        self.assertEqual(
            profile_manifest_transform["operation"],
            "generate_profile_manifest",
        )
        self.assertEqual(
            profile_manifest_transform["tools"],
            ["cupid_object", "host_python"],
        )
        expected_counts = {
            "cupid_assembler": 5,
            "cupid_object": 191,
            "cupid_linker": 2,
            "cupid_disassembler": 2,
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
                    self.assertEqual(
                        transform["tools"].count("host_python"),
                        1,
                        transform["output"],
                    )
                    self.assertTrue(
                        seed_inputs.issubset(transform["inputs"]),
                        transform["output"],
                    )
        cupid_tools = {
            "cupid_c_compiler",
            "cupid_assembler",
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
            ["verify-artifact-sizes"],
        )
        self.assertEqual(
            python_only[0],
            {
                "inputs": [
                    "boot/boot.bin",
                    "bootstrap/seeds/i386-linux/cupidasm.elf",
                    "bootstrap/seeds/i386-linux/cupidc.elf",
                    "bootstrap/seeds/i386-linux/cupiddis.elf",
                    "bootstrap/seeds/i386-linux/cupidld.elf",
                    "bootstrap/seeds/i386-linux/cupidobj.elf",
                    "kernel/kernel.bin",
                    "kernel/kernel.elf",
                    "kernel/kernel.elf.pass1",
                    "tools/artifact_size_policy.py",
                    "bootstrap/artifact-size-policy.json",
                    "bootstrap/seeds/i386-linux/manifest.json",
                ],
                "operation": "host_orchestration",
                "output": "verify-artifact-sizes",
                "recipe": [
                    "$(PYTHON) tools/artifact_size_policy.py verify "
                    "--root . \\",
                    "--policy $(ARTIFACT_SIZE_POLICY) \\",
                    "--seed-manifest $(BOOTSTRAP_SEED_MANIFEST)",
                ],
                "tools": ["host_python"],
            },
        )
        self.assertEqual(len(cupid_owned), 440)
        self.assertFalse(
            any(
                transform["operation"] == "recursive_make"
                for transform in audit["build"]["transforms"]
            )
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
            fixture_assignment.group("body")
            .replace("\\", " ")
            .split()
        )
        manifest_fixtures = (
            REPO_ROOT / "test_iso" / "fixtures.manifest"
        ).read_text(encoding="ascii").splitlines()
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
            "--seed-manifest $(BOOTSTRAP_SEED_MANIFEST)",
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

    def test_root_seed_manifest_override_moves_the_trust_unit(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        module = _load_audit_module()
        with tempfile.TemporaryDirectory(
            prefix=".audit-seed-",
            dir=REPO_ROOT,
        ) as td:
            seed_directory = Path(td)
            manifest = seed_directory / "manifest.json"
            manifest.write_text("{}\n", encoding="utf-8")
            for tool in (
                "cupidasm",
                "cupidc",
                "cupiddis",
                "cupidld",
                "cupidobj",
            ):
                (seed_directory / f"{tool}.elf").write_bytes(b"seed\n")
            relative_manifest = manifest.relative_to(REPO_ROOT).as_posix()
            variables = (
                *module.CANONICAL_MAKE_VARIABLES,
                f"BOOTSTRAP_SEED_MANIFEST={relative_manifest}",
            )
            with mock.patch.object(
                module,
                "CANONICAL_MAKE_VARIABLES",
                variables,
            ):
                rules = module._parse_make_rules(
                    module._run_make_database(
                        REPO_ROOT,
                        make,
                        "boot/boot.bin",
                    )
                )

            inputs = set(rules["boot/boot.bin"].prerequisites)
            expected = {
                relative_manifest,
                *{
                    (seed_directory / f"{tool}.elf")
                    .relative_to(REPO_ROOT)
                    .as_posix()
                    for tool in (
                        "cupidasm",
                        "cupidc",
                        "cupiddis",
                        "cupidld",
                        "cupidobj",
                    )
                },
            }
            self.assertTrue(expected.issubset(inputs))
            self.assertNotIn(
                "bootstrap/seeds/i386-linux/manifest.json",
                inputs,
            )

    def test_root_tool_override_can_replace_its_dependency_closure(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        module = _load_audit_module()
        with tempfile.TemporaryDirectory(
            prefix=".audit-tool-",
            dir=REPO_ROOT,
        ) as td:
            driver = Path(td) / "cupidasm-driver"
            driver.write_bytes(b"driver\n")
            relative_driver = driver.relative_to(REPO_ROOT).as_posix()
            variables = (
                *module.CANONICAL_MAKE_VARIABLES,
                "CUPIDASM=custom-cupidasm",
                f"CUPIDASM_INPUTS={relative_driver}",
            )
            with mock.patch.object(
                module,
                "CANONICAL_MAKE_VARIABLES",
                variables,
            ):
                rules = module._parse_make_rules(
                    module._run_make_database(
                        REPO_ROOT,
                        make,
                        "boot/boot.bin",
                    )
                )

            inputs = set(rules["boot/boot.bin"].prerequisites)
            self.assertIn(relative_driver, inputs)
            self.assertNotIn(
                "bootstrap/seeds/i386-linux/manifest.json",
                inputs,
            )

    def test_generated_kernel_symbols_use_the_checked_cupidc_graph(self):
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
            transforms = {
                entry["output"]: entry
                for entry in audit["build"]["transforms"]
            }
            generated = transforms["kernel/cpu/ksyms_data.cc"]
            self.assertEqual(
                generated["tools"],
                ["cupid_disassembler", "cupid_object", "host_python"],
            )
            self.assertEqual(
                generated["operation"], "generate_ksyms_source"
            )
            self.assertEqual(
                set(generated["inputs"]),
                {
                    "kernel/kernel.elf.pass1",
                    "tools/hostbuild.py",
                    "Makefile",
                    "tools/bootstrap_toolchain.py",
                    "bootstrap/seeds/i386-linux/manifest.json",
                    "bootstrap/seeds/i386-linux/cupidasm.elf",
                    "bootstrap/seeds/i386-linux/cupidc.elf",
                    "bootstrap/seeds/i386-linux/cupiddis.elf",
                    "bootstrap/seeds/i386-linux/cupidld.elf",
                    "bootstrap/seeds/i386-linux/cupidobj.elf",
                },
            )

            compiled = transforms["kernel/cpu/ksyms_data.o"]
            self.assertEqual(
                compiled["tools"],
                ["cupid_c_compiler", "host_python"],
            )
            self.assertEqual(
                compiled["operation"],
                "compile_c_to_elf32_object",
            )
            self.assertEqual(
                set(compiled["inputs"]),
                {
                    "kernel/cpu/ksyms_data.cc",
                    "kernel/cpu/ksyms.h",
                    "kernel/core/types.h",
                    "Makefile",
                    "tools/cupidc_kernel_compile.py",
                    "tools/kernel_cupidc_frontier.py",
                    "tools/bootstrap_toolchain.py",
                    "bootstrap/seeds/i386-linux/manifest.json",
                    "bootstrap/seeds/i386-linux/cupidasm.elf",
                    "bootstrap/seeds/i386-linux/cupidc.elf",
                    "bootstrap/seeds/i386-linux/cupiddis.elf",
                    "bootstrap/seeds/i386-linux/cupidld.elf",
                    "bootstrap/seeds/i386-linux/cupidobj.elf",
                },
            )
            sources = {
                entry["path"]: entry for entry in audit["sources"]
            }
            self.assertEqual(
                sources["kernel/cpu/ksyms_data.cc"]["language"],
                "cupid_c",
            )
            self.assertEqual(
                sources["kernel/cpu/ksyms_data.cc"]["origin"],
                "generated",
            )
            self.assertNotIn("kernel/cpu/ksyms_data.c", transforms)

    def test_make_include_extraction_keeps_assignment_adjacent_first_root(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                KERNEL_INCLUDES=-I./kernel -I./kernel/core
                CFLAGS = $(KERNEL_INCLUDES)
                .PHONY: all
                all: main.o
                main.o: main.c
                \t$(CC) $(CFLAGS) -c $< -o $@
                """,
            )
            _write(root / "main.c", "int main(void) { return 0; }\n")
            (root / "kernel" / "core").mkdir(parents=True)
            output = root / "audit.json"

            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                json.loads(output.read_text(encoding="utf-8"))["build"][
                    "include_search_paths"
                ],
                ["kernel", "kernel/core"],
            )

    def test_cupidc_make_profile_validation_rejects_preprocessor_drift(self):
        module = _load_audit_module()
        kernel_roots = " ".join(
            f"-I.{path}"
            for path in (
                "/kernel",
                "/kernel/audio",
                "/kernel/core",
                "/kernel/cpu",
                "/kernel/crypto",
                "/kernel/doom",
                "/kernel/fs",
                "/kernel/gfx",
                "/kernel/gui",
                "/kernel/lang",
                "/kernel/mm",
                "/kernel/network",
                "/kernel/smp",
                "/kernel/tls",
                "/kernel/usb",
                "/kernel/util",
                "/drivers",
                "/toolchain",
            )
        )

        def fixture(
            root,
            *,
            cflags_extra="",
            opt_extra="",
            roots_extra="",
            doom_implicit_flag="-Wno-implicit-function-declaration",
            hosted_cppflags_extra="",
            hosted_cflags_extra="",
        ):
            _write(
                root / "Makefile",
                f"""
                KERNEL_INCLUDES={kernel_roots} {roots_extra}
                CFLAGS=-m32 -ffreestanding -nostdinc -msse2 -pedantic \\
                       $(KERNEL_INCLUDES) -DDEBUG {cflags_extra}
                CFLAGS_DOOM=-m32 -ffreestanding -nostdinc -msse2 \\
                            $(KERNEL_INCLUDES) -I./kernel/doom/src \\
                            -I./kernel/doom/src/include_stubs \\
                            {doom_implicit_flag}
                CFLAGS_DOOM_TREE=$(CFLAGS_DOOM) \\
                    -include kernel/doom/dglibc_compat.h \\
                    -DDEFAULT_SAVEGAMEDIR=\\\"/home/doom/\\\" \\
                    -DDOOM_PORT_CUPIDOS=1
                OPT=-O2 {opt_extra}
                """,
            )
            _write(
                root / "user" / "Makefile",
                "CFLAGS=-m32 -ffreestanding -I.\n",
            )
            _write(
                root / "toolchain" / "Makefile",
                f"CPPFLAGS=-I. {hosted_cppflags_extra}\n"
                f"CFLAGS=-std=c11 {hosted_cflags_extra}\n",
            )

        cases = {
            "strict standard": (
                {"cflags_extra": "-std=c11"},
                r"unmodeled preprocessor flag.*-std=c11",
            ),
            "undefinition": (
                {"cflags_extra": "-UDEBUG"},
                r"unmodeled preprocessor flag.*-UDEBUG",
            ),
            "alternate include": (
                {"cflags_extra": "-iquote./private"},
                r"unmodeled preprocessor flag.*-iquote",
            ),
            "contradictory word size": (
                {"cflags_extra": "-m64"},
                r"unmodeled preprocessor flag.*-m64",
            ),
            "disabled SIMD target": (
                {"cflags_extra": "-mno-sse2"},
                r"unmodeled preprocessor flag.*-mno-sse2",
            ),
            "driver preprocessor pass-through": (
                {"cflags_extra": "-Wp,-DLOCAL=1"},
                r"unmodeled preprocessor flag.*-Wp,-DLOCAL=1",
            ),
            "alternate language mode": (
                {"cflags_extra": "-xc++"},
                r"unmodeled preprocessor flag.*-xc\+\+",
            ),
            "missing Doom implicit-call policy": (
                {"doom_implicit_flag": ""},
                r"DOOM_COMPAT_I386 implicit-function policy differs",
            ),
            "unsigned character mode": (
                {"cflags_extra": "-funsigned-char"},
                r"unmodeled preprocessor flag.*-funsigned-char",
            ),
            "negated signed character mode": (
                {"cflags_extra": "-fno-signed-char"},
                r"unmodeled preprocessor flag.*-fno-signed-char",
            ),
            "short wchar mode": (
                {"cflags_extra": "-fshort-wchar"},
                r"unmodeled preprocessor flag.*-fshort-wchar",
            ),
            "accelerator predefined macro": (
                {"cflags_extra": "-fopenacc"},
                r"unmodeled preprocessor flag.*-fopenacc",
            ),
            "extra include root": (
                {"roots_extra": "-I./private"},
                r"include-root order differs",
            ),
            "OPT-only macro": (
                {"opt_extra": "-DLOCAL=1"},
                r"OPT has preprocessor effects",
            ),
            "hosted alternate language mode": (
                {"hosted_cflags_extra": "-std=gnu11"},
                r"unmodeled preprocessor flag.*-std=gnu11",
            ),
            "hosted configured macro": (
                {"hosted_cflags_extra": "-DLOCAL=1"},
                r"configured macros differ.*CPPFLAGS\+CFLAGS",
            ),
            "hosted extra include root": (
                {"hosted_cppflags_extra": "-Iprivate"},
                r"HOSTED_TOOLCHAIN_64 include-root order differs",
            ),
            "hosted contradictory word size": (
                {"hosted_cflags_extra": "-m32"},
                r"HOSTED_TOOLCHAIN_64 has unmodeled preprocessor flag.*-m32",
            ),
            "hosted freestanding mode": (
                {"hosted_cflags_extra": "-ffreestanding"},
                r"HOSTED_TOOLCHAIN_64 has unmodeled preprocessor flag"
                r".*-ffreestanding",
            ),
        }
        for name, (changes, message) in cases.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                fixture(root, **changes)
                with self.assertRaisesRegex(module.AuditError, message):
                    module._validate_c_preprocessor_make_profiles(root, "make")

    def test_make_profile_values_use_gnu_make_assignment_timing(self):
        module = _load_audit_module()
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                ROOT=-I/a
                CFLAGS := $(ROOT)
                ROOT=-I/b
                .PHONY: all
                all:
                """,
            )

            values = module._read_evaluated_make_variables(
                root, "make", ("CFLAGS", "ROOT")
            )

            self.assertEqual(values, {"CFLAGS": "-I/a", "ROOT": "-I/b"})

    def test_cupidc_kernel_wrapper_make_binding_rejects_drift(self):
        module = _load_audit_module()
        transforms = [
            {
                "tools": ["cupid_c_compiler", "host_python"],
            }
        ]
        cases = {
            "non-Python launcher": (
                "PYTHON := clang\n"
                "CUPIDC_KERNEL_COMPILE := $(PYTHON) "
                "tools/cupidc_kernel_compile.py --root .\n",
                "is not a Python launcher",
            ),
            "Python command string": (
                "PYTHON := python3 -c pass\n"
                "CUPIDC_KERNEL_COMPILE := $(PYTHON) "
                "tools/cupidc_kernel_compile.py --root .\n",
                "must contain only the Python launcher",
            ),
            "Python module": (
                "PYTHON := python3 -m unchecked\n"
                "CUPIDC_KERNEL_COMPILE := $(PYTHON) "
                "tools/cupidc_kernel_compile.py --root .\n",
                "must contain only the Python launcher",
            ),
            "different wrapper": (
                "PYTHON := python3\n"
                "CUPIDC_KERNEL_COMPILE := $(PYTHON) "
                "tools/other_wrapper.py --root .\n",
                "differs from the checked command",
            ),
            "extra option": (
                "PYTHON := python3\n"
                "CUPIDC_KERNEL_COMPILE := $(PYTHON) "
                "tools/cupidc_kernel_compile.py --root . --unchecked\n",
                "differs from the checked command",
            ),
        }
        for name, (binding, message) in cases.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                _write(
                    root / "Makefile",
                    binding + ".PHONY: all\nall:\n",
                )
                with self.assertRaisesRegex(module.AuditError, message):
                    module._validate_cupidc_kernel_compile_make_binding(
                        root,
                        "make",
                        transforms,
                    )

    def test_cupidc_kernel_wrapper_make_binding_accepts_checked_command(self):
        module = _load_audit_module()
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                PYTHON := python3
                CUPIDC_KERNEL_COMPILE := $(PYTHON) \
                    tools/cupidc_kernel_compile.py --root .
                .PHONY: all
                all:
                """,
            )

            module._validate_cupidc_kernel_compile_make_binding(
                root,
                "make",
                [{"tools": ["cupid_c_compiler", "host_python"]}],
            )

    def test_cupidc_ownership_provenance_tracks_wrapper_drift(self):
        module = _load_audit_module()
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for relative in (
                "Makefile",
                *module.CUPIDC_KERNEL_CONTROL_FILES,
            ):
                _write(root / relative, f"{relative}\n")
            model = module.BuildModel(
                directory=".",
                root_target="all",
                rules={},
                reachable=set(),
                direct_sources=set(),
                generated_sources=set(),
                forced_sources=set(),
                includes_by_source={},
                include_search_paths=[],
                transforms=[
                    {
                        "tools": ["cupid_c_compiler", "host_python"],
                    }
                ],
            )

            first = module._provenance(root, [model], [])
            wrapper = root / "tools" / "cupidc_kernel_compile.py"
            _write(wrapper, "changed wrapper\n")
            second = module._provenance(root, [model], [])
            first_controls = {
                entry["path"]: entry["sha256"]
                for entry in first["control_files"]
            }
            second_controls = {
                entry["path"]: entry["sha256"]
                for entry in second["control_files"]
            }

            self.assertNotEqual(
                first_controls["tools/cupidc_kernel_compile.py"],
                second_controls["tools/cupidc_kernel_compile.py"],
            )
            self.assertEqual(
                first_controls["bootstrap/seeds/i386-linux/manifest.json"],
                second_controls[
                    "bootstrap/seeds/i386-linux/manifest.json"
                ],
            )

    def test_supported_audit_targets_check_and_consume_active_manifest(self):
        root_makefile = (REPO_ROOT / "Makefile").read_text(encoding="utf-8")
        toolchain_makefile = TOOLCHAIN_MAKEFILE.read_text(encoding="utf-8")

        self.assertEqual(
            root_makefile.count("--c-preprocessor-active-cases "), 3
        )
        self.assertIn(
            "BOOTSTRAP_CUPIDC_ACTIVE_CASES := "
            "toolchain/tests/cupidc_pp_active_cases.inc",
            root_makefile,
        )
        self.assertIn(
            "$(CUPIDC_PP_ACTIVE_CASES) \\", toolchain_makefile
        )
        self.assertIn(
            '#include "cupidc_pp_active_cases.inc"',
            CUPIDC_PP_CONTRACT.read_text(encoding="utf-8"),
        )


if __name__ == "__main__":
    unittest.main()
