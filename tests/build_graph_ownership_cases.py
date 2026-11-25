## @file
#  @brief Check tool ownership, user ABI, and build graph reachability.
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from unittest import mock
from tests.build_graph_audit_fixtures import (
    AUDIT_TOOL,
    REPO_ROOT,
    _json_list_recipe,
    _load_audit_module,
    _write,
    _write_source_suffix_policy,
)


class BuildOwnershipCases:
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
        input_variables = module._read_evaluated_make_variables(
            REPO_ROOT / "user",
            make,
            (
                "NATIVE_WINDOWS_USER_SYSCALL_ABI_INPUTS",
                "CHECKED_SEED_INPUTS",
                "USER_SYSCALL_ABI_PLATFORM_ARGUMENTS",
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
        self.assertEqual(transform["tools"], ["cupid_builder"])
        self.assertEqual(transform["operation"], "verify_user_syscall_abi")
        self.assertEqual(len(module.USER_SYSCALL_ABI_NATIVE_BUILD_INPUTS), 30)
        self.assertEqual(len(module.USER_SYSCALL_ABI_CHECKED_SEED_INPUTS), 8)
        self.assertEqual(
            repo_inputs("NATIVE_WINDOWS_USER_SYSCALL_ABI_INPUTS"),
            module.USER_SYSCALL_ABI_NATIVE_BUILD_INPUTS,
        )
        self.assertEqual(
            repo_inputs("CHECKED_SEED_INPUTS"),
            tuple(sorted(module.USER_SYSCALL_ABI_CHECKED_SEED_INPUTS)),
        )
        self.assertEqual(
            input_variables["USER_SYSCALL_ABI_PLATFORM_ARGUMENTS"],
            "--windows-manifest ../bootstrap/seeds/i386-windows/manifest.json",
        )
        self.assertEqual(len(module.USER_SYSCALL_ABI_AUDIT_INPUTS), 38)
        self.assertEqual(
            module.USER_SYSCALL_ABI_AUDIT_INPUTS,
            tuple(
                sorted(
                    set(module.USER_SYSCALL_ABI_NATIVE_BUILD_INPUTS)
                    | set(module.USER_SYSCALL_ABI_CHECKED_SEED_INPUTS)
                )
            ),
        )
        self.assertEqual(
            transform["inputs"],
            [*sorted({*module.USER_SYSCALL_ABI_SOURCE_INPUTS,
                      *module.USER_SYSCALL_ABI_CHECKED_SEED_INPUTS}), "user/Makefile"],
        )
        module._validate_user_syscall_abi_transform("user", transform)

    def test_native_user_abi_delivery_rejects_closure_and_recipe_drift(self):
        module = _load_audit_module()
        for suffix in ("elf", "exe"):
            directory = "bootstrap/seeds/i386-" + ("linux" if suffix == "elf" else "windows")
            seeds = [directory + "/manifest.json", "bootstrap/seeds/release.json", *(
                directory + "/" + role + "." + suffix
                for role in ("cupidasm", "cupidc", "cupiddis", "cupidld", "cupidobj", "cupidbuild")
            )]
            inputs = sorted({*seeds, *module.USER_SYSCALL_ABI_SOURCE_INPUTS, "user/Makefile"})
            transform = {
                "output": "user/test-syscall-abi", "operation": "verify_user_syscall_abi",
                "inputs": inputs, "tools": ["cupid_builder"],
                "recipe": ["$(PRODUCTION_SEED_DIRECTORY)cupidbuild.$(PRODUCTION_SEED_SUFFIX) "
                           'verify-user-abi --root "$(abspath ..)"'],
            }
            module._validate_cupidbuild_user_abi_delivery([transform], seed_inputs=seeds)
            module._validate_user_syscall_abi_transform("user", transform)
            alternate_release = "reviewed/cohort.release"
            alternate_seeds = [alternate_release if path == "bootstrap/seeds/release.json" else path
                               for path in seeds]
            alternate = {**transform, "inputs": sorted({*alternate_seeds,
                         *module.USER_SYSCALL_ABI_SOURCE_INPUTS, "user/Makefile"})}
            module._validate_cupidbuild_user_abi_delivery([alternate], seed_inputs=alternate_seeds)
            with self.assertRaises(module.AuditError):
                module._validate_cupidbuild_user_abi_delivery([alternate], seed_inputs=seeds)
            changes = [
                ("missing " + path, {**transform, "inputs": [row for row in inputs if row != path]})
                for path in inputs
            ] + [
                ("duplicate input", {**transform, "inputs": inputs + [inputs[0]]}),
                ("unexpected input", {**transform, "inputs": inputs + ["user/unchecked.h"]}),
                ("Python coordinator", {**transform, "tools": ["host_python"]}),
                ("wrong operation", {**transform, "operation": "generate_toolchain_manifest"}),
                ("wrong target", {**transform, "output": "user/unchecked-abi"}),
                ("unchecked recipe", {**transform, "recipe": ["$(USER_SYSCALL_ABI)"]}),
                ("wrong source root", {**transform, "recipe": [transform["recipe"][0].replace('"$(abspath ..)"', "..")] }),
                ("order-only input", {**transform, "order_only_inputs": [inputs[0]]}),
            ]
            for name, changed in changes:
                with self.subTest(suffix=suffix, name=name), self.assertRaises(module.AuditError):
                    module._validate_cupidbuild_user_abi_delivery([changed], seed_inputs=seeds)
            for rejected in ([transform, transform], []):
                with self.assertRaises(module.AuditError):
                    module._validate_cupidbuild_user_abi_delivery(rejected, seed_inputs=seeds)
            for path in seeds:
                with self.subTest(suffix=suffix, omitted_seed=path), self.assertRaises(module.AuditError):
                    module._validate_cupidbuild_user_abi_delivery(
                        [transform], seed_inputs=[row for row in seeds if row != path])

    def test_native_user_abi_make_binding_requires_the_six_declarations_and_seed(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        module = _load_audit_module()
        source = (REPO_ROOT / "user/Makefile").read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory(prefix="cupid-native-abi-binding-") as temporary:
            root = Path(temporary)
            target = root / "user/Makefile"
            target.parent.mkdir()
            target.write_text(source, encoding="utf-8")
            self.assertEqual(
                module._validate_cupidbuild_user_make_binding(root, make),
                list(module.USER_SYSCALL_ABI_CHECKED_SEED_INPUTS),
            )
            for path in ("../kernel/core/types.h", "../kernel/core/syscall.h", "../kernel/core/syscall.cc",
                         "../kernel/fs/vfs.h", "../kernel/network/socket.h", "cupid.h", "$(CHECKED_SEED_INPUTS)"):
                start = source.index("override USER_SYSCALL_ABI_INPUTS :=")
                end = source.index("\nUSER_FRONTIER_INPUTS", start)
                declaration = source[start:end]
                self.assertEqual(declaration.count(path), 1)
                changed = source[:start] + declaration.replace(path, "", 1) + source[end:]
                target.write_text(changed, encoding="utf-8")
                with self.subTest(omitted=path), self.assertRaisesRegex(module.AuditError, "user Make binding differs"):
                    module._validate_cupidbuild_user_make_binding(root, make)

    def test_syscall_abi_oracle_input_does_not_relabel_contract_publication(
        self,
    ):
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

        self.assertEqual(
            transform["tools"],
            [
                "cupid_assembler",
                "cupid_c_compiler",
                "cupid_c_contract",
                "cupid_linker",
                "host_python",
            ],
        )
        self.assertEqual(transform["operation"], "generate_toolchain_manifest")
        model = mock.Mock(directory="toolchain", transforms=[transform])
        self.assertEqual(
            module._toolchain_contract_cupidc_ownership_inputs([model]),
            {"toolchain/tests/user_syscall_abi_contract.cc"},
        )

    def test_user_syscall_abi_verifier_rejects_missing_live_input(self):
        module = _load_audit_module()
        transform = {
            "output": "user/test-syscall-abi",
            "inputs": [
                *module.USER_SYSCALL_ABI_AUDIT_INPUTS,
                "user/Makefile",
            ],
            "tools": [
                "cupid_assembler",
                "cupid_c_compiler",
                "cupid_c_contract",
                "cupid_linker",
                "host_python",
            ],
            "operation": "verify_user_syscall_abi",
            "recipe": ["$(USER_SYSCALL_ABI)"],
        }
        missing_path = "toolchain/ctool.cc"
        changed = {
            **transform,
            "inputs": [
                path for path in transform["inputs"] if path != missing_path
            ],
        }
        with self.assertRaisesRegex(
            module.AuditError,
            r"user syscall ABI verifier inputs changed; "
            r"missing=\['toolchain/ctool\.cc'\], unexpected=\[\]",
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
            "tools": [
                "cupid_assembler",
                "cupid_c_compiler",
                "cupid_c_contract",
                "cupid_linker",
                "host_python",
            ],
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
            "tools": [
                "cupid_assembler",
                "cupid_c_compiler",
                "cupid_c_contract",
                "cupid_linker",
                "host_python",
            ],
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
            with (
                self.subTest(name=name),
                self.assertRaisesRegex(
                    module.AuditError,
                    "user syscall ABI verifier",
                ),
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
                entry["output"]: entry
                for entry in audit["build"]["transforms"]
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
                transforms["entry.o"]["operation"],
                "assemble_elf32_relocatable",
            )
            sources = {entry["path"]: entry for entry in audit["sources"]}
            self.assertEqual(sources["boot.asm"]["runtime_owner"], "CupidASM")
            self.assertEqual(sources["entry.asm"]["runtime_owner"], "CupidASM")

    def test_inventory_rejects_ownerless_active_assembly(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:

                .PHONY: all
                all: orphan.asm
                """,
            )
            _write(root / "orphan.asm", "bits 32\nret\n")

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

            self.assertEqual(result.returncode, 2)
            self.assertIn(
                "active assembly source has no build owner or explicit "
                "classification: orphan.asm",
                result.stderr,
            )

    def test_assembly_ownership_accepts_an_explicit_host_fixture(self):
        module = _load_audit_module()
        source = {
            "path": "tests/host_start.asm",
            "language": "assembly",
            "build_owners": [],
            "runtime_owner": None,
        }
        policy = {
            "tests/host_start.asm": (
                "host_fixture",
                "native startup fixture outside Cupid OS publication",
            )
        }

        with mock.patch.object(
            module,
            "KNOWN_ACTIVE_ASSEMBLY_POLICIES",
            policy,
            create=True,
        ):
            contract = module._active_assembly_ownership_contract([source])

        self.assertEqual(contract["active_sources"], 1)
        self.assertEqual(contract["ownerless_sources"], 1)
        self.assertEqual(
            contract["explicit_classifications"],
            [
                {
                    "path": "tests/host_start.asm",
                    "classification": "host_fixture",
                    "reason": "native startup fixture outside Cupid OS publication",
                }
            ],
        )

    def test_inventory_maps_reachable_language_inputs_to_tool_owned_outputs(
        self,
    ):
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
                CUPIDOBJ = seed/cupidbuild.exe run \
                    --seed-manifest seed/manifest.json --root . \
                    --tool cupidobj --

                .PHONY: all
                all: kernel.elf app.o demo.o

                kernel.elf: main.o boot.bin
                \t$(LD) -o $@ $^

                main.o: main.c api.h
                \t$(CC) -c $< -o $@

                boot.bin: boot.asm
                \t$(ASM) -f bin $< -o $@

                app.o: app.cc
                \t$(CUPIDOBJ) wrap-text $< -o $@

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
            _write_source_suffix_policy(
                root,
                runtime_delivery_sources=["app.cc"],
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
            self.assertEqual(audit["schema"], "cupid.build-graph-audit.v1")
            self.assertEqual(audit["build"]["root_target"], "all")
            self.assertEqual(
                audit["provenance"]["control_files"][0]["path"], "Makefile"
            )

            sources = {entry["path"]: entry for entry in audit["sources"]}
            self.assertEqual(
                set(sources),
                {
                    "api.h",
                    "app.cc",
                    "boot.asm",
                    "demo.asm",
                    "main.c",
                    "types.h",
                },
            )
            self.assertEqual(sources["main.c"]["language"], "c")
            self.assertEqual(sources["app.cc"]["language"], "cupid_c")
            self.assertEqual(sources["boot.asm"]["language"], "assembly")
            self.assertEqual(
                sources["types.h"]["reachability"], "transitive_include"
            )
            self.assertEqual(sources["app.cc"]["runtime_owner"], "CupidC")
            self.assertEqual(sources["demo.asm"]["runtime_owner"], "CupidASM")
            self.assertIsNone(sources["boot.asm"]["runtime_owner"])

            transforms = {
                entry["output"]: entry
                for entry in audit["build"]["transforms"]
            }
            self.assertEqual(
                transforms["main.o"]["tools"], ["host_c_compiler"]
            )
            self.assertEqual(transforms["boot.bin"]["tools"], ["nasm"])
            self.assertEqual(
                transforms["boot.bin"]["operation"], "assemble_flat_binary"
            )
            self.assertEqual(
                transforms["kernel.elf"]["operation"], "link_elf32_executable"
            )
            self.assertEqual(
                transforms["app.o"]["tools"],
                ["cupid_object", "cupid_builder"],
            )
            self.assertEqual(
                transforms["demo.o"]["tools"], ["host_object_copy"]
            )
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

                app.o: app.txt
                \t$(CUPIDOBJ) wrap-text $< -o $@

                main.o: main.c
                \t$(CC) -c $< -o $@

                print-bootstrap-artifacts:
                {artifact_recipe}
                """,
            )
            _write(root / "main.c", "int main(void) { return 0; }\n")
            _write(root / "app.txt", "plain delivery payload\n")
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
                entry["output"]: entry
                for entry in audit["build"]["transforms"]
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
                audit["contracts"]["bootstrap_artifact_coverage"][
                    "linked_objects"
                ],
                1,
            )

    def test_checked_tool_runner_owner_requires_the_matching_command(self):
        module = _load_audit_module()
        cases = (
            (
                "native linker",
                "seed/cupidbuild.exe run --seed-manifest seed/manifest.json "
                "--root . --tool cupidld --",
                "cupidld",
                "cupid_builder",
            ),
            (
                "Python linker",
                "python tools/bootstrap_toolchain.py run "
                "--seed-manifest seed/manifest.json --root . "
                "--tool cupidld --",
                "cupidld",
                "host_python",
            ),
            (
                "direct linker",
                "seed/cupidld.exe -m elf_i386",
                "cupidld",
                None,
            ),
            (
                "wrong tool",
                "seed/cupidbuild.exe run --seed-manifest seed/manifest.json "
                "--root . --tool cupidobj --",
                "cupidld",
                None,
            ),
            (
                "missing argument boundary",
                "seed/cupidbuild.exe run --seed-manifest seed/manifest.json "
                "--root . --tool cupidld",
                "cupidld",
                None,
            ),
        )
        for name, command, tool, expected in cases:
            with self.subTest(name=name):
                self.assertEqual(
                    module._checked_tool_runner_owner(command, tool),
                    expected,
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
                all: symbols.cc photo.o reader.txt big.bin native-big.bin cupidos.img

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

                native-big.bin: big_pattern.asm
                \t$(PRODUCTION_SEED_DIRECTORY)cupidbuild.$(PRODUCTION_SEED_SUFFIX) assemble-iso-pattern \
                    --seed-manifest seed/manifest.json --root "$(CURDIR)" \
                    --source $< --output $@

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
                transforms["native-big.bin"]["tools"],
                ["cupid_assembler", "cupid_disassembler", "cupid_builder"],
            )
            self.assertEqual(
                transforms["native-big.bin"]["operation"],
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
            _write(
                root / "child" / "child.c", "int child(void) { return 1; }\n"
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
            self.assertEqual(audit["build"]["root_target"], "all")
            self.assertIn(
                "main.o",
                {entry["output"] for entry in audit["build"]["transforms"]},
            )
            self.assertEqual(
                [entry["path"] for entry in audit["sources"]], ["main.c"]
            )
