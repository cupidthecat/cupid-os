## @file
#  @brief Check repository worktrees and canonical Make entry points.
import ast
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from tests.c_source_spelling import _c_spelling_ranges
from tools.build_graph_report import render_markdown
from tests.build_graph_audit_fixtures import (
    ACTIVE_BUILD_MANIFEST,
    AUDIT_TOOL,
    REPO_ROOT,
    _load_audit_module,
    _write,
)


class RootGraphCases:
    def test_c_spelling_fixture_preserves_tokens_and_literals(self):
        self.assertEqual(
            len(_c_spelling_ranges('int /* type */ *value = 0;', 'int * value=0;')), 1)
        self.assertEqual(
            len(_c_spelling_ranges('f(\n value, "two words");', 'f(value,"two words");')), 1)
        for source, fragment in (
            ('value++;', 'value + +;'),
            ('foobar;', 'foo;'),
            ('f("two words");', 'f("twowords");'),
            ('/* target(); */ other();', 'target();'),
            ('L"wide";', 'L "wide";'),
            ('1e+2;', '1e + 2;'),
        ):
            with self.subTest(source=source, fragment=fragment):
                self.assertEqual(_c_spelling_ranges(source, fragment), [])
        for fragment in ('', '/* incomplete', '"incomplete'):
            with self.subTest(fragment=fragment), self.assertRaises(ValueError):
                _c_spelling_ranges('int value;', fragment)


    def test_windows_worktree_inventory_translates_metadata_and_excludes_untracked_sources(self):
        module = _load_audit_module()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            link = root / ".git"
            link.write_bytes(b"gitdir: C:/repo with spaces/.git/worktrees/bootstrap\r\n")
            original_link = link.read_bytes()
            (root / "tracked.cc").write_text("int tracked;\n", encoding="utf-8")
            (root / "untracked.c").write_text("int fixture;\n", encoding="utf-8")
            translated = "/mnt/c/repo with spaces/.git/worktrees/bootstrap"
            responses = [
                subprocess.CompletedProcess([], 128, "", "invalid Windows Git link"),
                subprocess.CompletedProcess([], 0, translated + "\n", ""),
                subprocess.CompletedProcess([], 0, str(root) + "\n", ""),
                subprocess.CompletedProcess([], 0, b"tracked.cc\0", b""),
            ]
            with mock.patch.object(module, "os", SimpleNamespace(name="posix")), mock.patch.object(
                module.subprocess, "run", side_effect=responses
            ) as run:
                paths = module._source_universe(root)
            self.assertEqual(paths, ["tracked.cc"])
            self.assertEqual(link.read_bytes(), original_link)
            self.assertEqual(run.call_args_list[1].args[0], [
                "wslpath", "-u", "C:/repo with spaces/.git/worktrees/bootstrap"
            ])
            prefix = ["git", "--git-dir", translated, "--work-tree", str(root)]
            self.assertEqual(run.call_args_list[2].args[0], [
                *prefix, "rev-parse", "--show-toplevel"
            ])
            self.assertEqual(run.call_args_list[3].args[0], [*prefix, "ls-files", "-z"])

    def test_worktree_translation_rejects_invalid_links_and_unusable_paths(self):
        module = _load_audit_module()
        failed = subprocess.CompletedProcess([], 128, "", "not a repository")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            link = root / ".git"
            for text in ("gitdir: relative/path", "gitdir: /native/path", "gitdir: C:relative",
                         "gitdir: C:/valid\nextra", "not a Git link"):
                link.write_text(text, encoding="utf-8")
                with self.subTest(link=text), mock.patch.object(
                    module, "os", SimpleNamespace(name="posix")
                ), mock.patch.object(module.subprocess, "run", return_value=failed) as run:
                    self.assertIsNone(module._tracked_paths(root))
                    self.assertEqual(run.call_count, 1)
            link.write_text("gitdir: C:/repo/.git/worktrees/bootstrap", encoding="utf-8")
            for response in (
                subprocess.CompletedProcess([], 1, "", "conversion failed"),
                subprocess.CompletedProcess([], 0, "relative/path\n", ""),
                subprocess.CompletedProcess([], 0, "/absolute\nsecond/path\n", ""),
                OSError("wslpath unavailable"),
            ):
                with self.subTest(translation=repr(response)), mock.patch.object(
                    module, "os", SimpleNamespace(name="posix")
                ), mock.patch.object(module.subprocess, "run", side_effect=[failed, response]):
                    self.assertIsNone(module._tracked_paths(root))

    def test_translated_worktree_keeps_root_binding_and_listing_errors(self):
        module = _load_audit_module()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / ".git").write_text("gitdir: C:/repo/.git/worktrees/bootstrap", encoding="utf-8")
            failed = subprocess.CompletedProcess([], 128, "", "not a repository")
            translated = subprocess.CompletedProcess([], 0, "/mnt/c/repo/.git/worktrees/bootstrap\n", "")
            for probe in (
                subprocess.CompletedProcess([], 128, "", "invalid metadata"),
                subprocess.CompletedProcess([], 0, str(root.parent) + "\n", ""),
            ):
                with self.subTest(root=probe.stdout), mock.patch.object(
                    module, "os", SimpleNamespace(name="posix")
                ), mock.patch.object(module.subprocess, "run", side_effect=[failed, translated, probe]) as run:
                    self.assertIsNone(module._tracked_paths(root))
                    self.assertEqual(run.call_count, 3)
            responses = [failed, translated,
                subprocess.CompletedProcess([], 0, str(root) + "\n", ""),
                subprocess.CompletedProcess([], 128, b"", b"index unavailable")]
            with mock.patch.object(module, "os", SimpleNamespace(name="posix")), mock.patch.object(
                module.subprocess, "run", side_effect=responses
            ), self.assertRaisesRegex(module.AuditError, "git could not enumerate tracked sources: index unavailable"):
                module._tracked_paths(root)

    def test_native_git_inventory_uses_its_original_probe_and_root_guard(self):
        module = _load_audit_module()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            responses = [subprocess.CompletedProcess([], 0, str(root) + "\n", ""),
                         subprocess.CompletedProcess([], 0, b"tracked.cc\0", b"")]
            with mock.patch.object(module.subprocess, "run", side_effect=responses) as run:
                self.assertEqual(module._tracked_paths(root), ["tracked.cc"])
                self.assertEqual(run.call_count, 2)
                self.assertEqual(run.call_args_list[1].args[0], ["git", "-C", str(root), "ls-files", "-z"])
            with mock.patch.object(module.subprocess, "run", return_value=
                subprocess.CompletedProcess([], 0, str(root.parent) + "\n", "")) as run:
                self.assertIsNone(module._tracked_paths(root))
                self.assertEqual(run.call_count, 1)

    def test_order_only_edges_keep_reachability_without_becoming_content_inputs(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(root / "main.c", "int main(void) { return 0; }\n")
            _write(
                root / "Makefile",
                """
                CC = gcc
                .PHONY: all ready
                all: main.o
                main.o: main.c | main.c ready
                \t$(CC) -c $< -o $@
                ready:
                \tpython -c "pass"
                """,
            )
            output = root / "audit.json"
            result = subprocess.run(
                [sys.executable, str(AUDIT_TOOL), "--root", str(root),
                 "--output", str(output)],
                text=True, capture_output=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            audit = json.loads(output.read_text(encoding="utf-8"))
            transforms = {
                item["output"]: item for item in audit["build"]["transforms"]
            }
            self.assertEqual(transforms["main.o"]["inputs"], ["main.c"])
            self.assertEqual(transforms["main.o"]["order_only_inputs"], ["ready"])
            self.assertIn("ready", transforms)
            self.assertNotIn("order_only_inputs", transforms["ready"])

    def test_user_abi_order_only_gate_remains_reachable(self):
        module = _load_audit_module()
        rules = module._parse_make_rules(
            module._run_make_database(REPO_ROOT / "user", "make", "all")
        )
        transforms = {
            item["output"]: item
            for item in module._build_transforms(
                "user", module._reachable_rules(rules, "all"), rules
            )
        }
        self.assertIn("user/test-syscall-abi", transforms)
        for program in ("hello", "ls", "cat"):
            transform = transforms[f"user/build/{program}.o"]
            self.assertEqual(
                transform["order_only_inputs"], ["user/test-syscall-abi"]
            )
            self.assertNotIn("user/test-syscall-abi", transform["inputs"])

    def test_profile_writer_barrier_covers_every_source_root_output(self):
        module = _load_audit_module()
        rules = module._parse_make_rules(
            module._run_make_database(REPO_ROOT, "make", "all")
        )
        transforms = module._build_transforms(
            ".", module._reachable_rules(rules, "all"), rules,
            cupidobj_runner_owner="cupid_builder",
            cupidld_runner_owner="cupid_builder",
        )
        writers = [
            item for item in transforms
            if item["output"].startswith(("kernel/", "drivers/", "toolchain/"))
        ]
        self.assertEqual(len(writers), 254)
        module._validate_cupidobj_profile_manifest_delivery(REPO_ROOT, transforms)
        for writer in writers:
            changed = {
                **writer,
                "inputs": [
                    path for path in writer["inputs"]
                    if path != module._CUPIDOBJ_PROFILE_MANIFEST_OUTPUT
                ],
                "order_only_inputs": [
                    path for path in writer.get("order_only_inputs", [])
                    if path != module._CUPIDOBJ_PROFILE_MANIFEST_OUTPUT
                ],
            }
            with self.subTest(output=writer["output"]):
                with self.assertRaisesRegex(
                    module.AuditError,
                    "source-directory writer: " + re.escape(writer["output"]),
                ):
                    module._validate_cupidobj_profile_writer_dependencies(
                        [
                            changed if item is writer else item
                            for item in transforms
                        ],
                    )

    def test_tracked_audit_attributes_toolchain_startup_assembly_to_cupidasm(
        self,
    ):
        audit = json.loads(ACTIVE_BUILD_MANIFEST.read_text(encoding="utf-8"))
        sources = {source["path"]: source for source in audit["sources"]}

        for path in (
            "toolchain/hosted/i386-linux/start.asm",
            "toolchain/hosted/i386-windows/cupidbuild_start.asm",
            "toolchain/hosted/i386-windows/final_path_start.asm",
            "toolchain/hosted/i386-windows/observer_thread.asm",
            "toolchain/hosted/i386-windows/publication_start.asm",
            "toolchain/hosted/i386-windows/start.asm",
            "toolchain/hosted/i386-windows/tool_start.asm",
            "toolchain/hosted/i386-windows/utf8_cupidbuild_start.asm",
            "toolchain/hosted/i386-windows/utf8_long_path_start.asm",
            "toolchain/hosted/i386-windows/utf8_publication_start.asm",
            "toolchain/hosted/i386-windows/utf8_tool_start.asm",
        ):
            with self.subTest(path=path):
                self.assertEqual(sources[path]["runtime_owner"], "CupidASM")
                self.assertIn("cupid_assembler", sources[path]["build_owners"])
        self.assertEqual(
            audit["contracts"]["assembly_source_ownership"],
            {
                "status": "pass",
                "active_sources": 38,
                "cupidasm_owned_sources": 38,
                "other_owned_sources": 0,
                "ownerless_sources": 0,
                "explicit_classifications": [],
                "toolchain_startup_sources": 11,
            },
        )
        markdown = render_markdown(audit)
        self.assertIn(
            "38 active assembly sources; 38 CupidASM-owned; "
            "11 Toolchain startup; 0 other-owned; 0 ownerless; "
            "0 explicit host-only classifications",
            markdown,
        )

    def test_toolchain_startup_assembly_ownership_rejects_a_missing_input(
        self,
    ):
        module = _load_audit_module()
        inputs = list(module.TOOLCHAIN_CONTRACT_CUPIDASM_OWNERSHIP_INPUTS)
        inputs.remove("toolchain/hosted/i386-windows/start.asm")
        model = module.BuildModel(
            directory="toolchain",
            root_target="all",
            rules={},
            reachable=set(),
            direct_sources=set(inputs),
            generated_sources=set(),
            forced_sources=set(),
            includes_by_source={},
            include_search_paths=[],
            transforms=[
                {
                    "output": "toolchain/build/cupidc-contracts/manifest.json",
                    "operation": "generate_toolchain_manifest",
                    "tools": [
                        "cupid_assembler",
                        "cupid_c_compiler",
                        "cupid_c_contract",
                        "cupid_linker",
                        "host_python",
                    ],
                    "inputs": inputs,
                }
            ],
        )

        with self.assertRaisesRegex(
            module.AuditError,
            "Toolchain contract CupidASM startup ownership differs.*missing",
        ):
            module._toolchain_contract_cupidasm_ownership_inputs([model])

    def test_utf8_toolchain_inputs_have_complete_ownership(self):
        module = _load_audit_module()
        wide = {
            "toolchain/hosted/i386-windows/final_path_start.asm",
            "toolchain/hosted/i386-windows/observer_thread.asm",
            "toolchain/hosted/i386-windows/utf8_cupidbuild_start.asm",
            "toolchain/hosted/i386-windows/utf8_long_path_start.asm",
            "toolchain/hosted/i386-windows/utf8_publication_start.asm",
            "toolchain/hosted/i386-windows/utf8_tool_start.asm",
            "toolchain/hosted/i386-windows/windows_utf8.cc",
            "toolchain/native_utf8.h",
            "toolchain/path_encoding.cc",
            "toolchain/path_encoding.h",
        }
        self.assertTrue(wide <= set(module.TOOLCHAIN_CONTRACT_LINUX_INPUTS))
        inputs = sorted(set(module.TOOLCHAIN_CONTRACT_LINUX_INPUTS) | wide)
        transform = {
            "output": "toolchain/build/cupidc-contracts/manifest.json",
            "operation": "generate_toolchain_manifest",
            "tools": ["cupid_assembler", "cupid_c_compiler", "cupid_c_contract",
                      "cupid_linker", "host_python"],
            "inputs": inputs,
        }
        model = module.BuildModel(
            directory="toolchain", root_target="all", rules={}, reachable=set(),
            direct_sources=set(inputs), generated_sources=set(), forced_sources=set(),
            includes_by_source={}, include_search_paths=[], transforms=[transform])
        expected = {path for path in inputs if path.endswith(".asm")}
        self.assertEqual(len(expected), 11)
        self.assertEqual(module._toolchain_contract_cupidasm_ownership_inputs([model]), expected)
        self.assertTrue({path for path in wide if path.endswith(".cc")} <=
                        module._toolchain_contract_cupidc_ownership_inputs([model]))
        for path in sorted(wide):
            if not path.endswith(".asm"):
                continue
            transform["inputs"] = [item for item in inputs if item != path]
            with self.subTest(path=path), self.assertRaisesRegex(
                    module.AuditError, "startup ownership differs.*missing"):
                module._toolchain_contract_cupidasm_ownership_inputs([model])
        transform["inputs"] = inputs + ["toolchain/hosted/i386-windows/unchecked.asm"]
        with self.assertRaisesRegex(module.AuditError, "startup ownership differs.*unexpected"):
            module._toolchain_contract_cupidasm_ownership_inputs([model])

    def test_toolchain_manifest_transform_matches_current_utf8_closure(self):
        module = _load_audit_module()
        rules = module._parse_make_rules(
            module._run_make_database(REPO_ROOT / "toolchain", "make", "all"))
        transforms = module._build_transforms(
            "toolchain", module._reachable_rules(rules, "all"), rules)
        transform = next(row for row in transforms if row["output"] == "toolchain/all")
        inputs = transform["inputs"]
        self.assertEqual(len(inputs), len(set(inputs)))
        self.assertEqual(set(inputs), module.TOOLCHAIN_MANIFEST_CONTRACT_TRANSFORM_INPUTS)
        self.assertEqual(module.TOOLCHAIN_MANIFEST_PUBLICATION_INPUTS,
                         module.USER_SYSCALL_ABI_PUBLICATION_INPUTS)

    def test_utf8_preprocessing_profiles_match_checked_windows_roles(self):
        from tools import bootstrap_toolchain as bootstrap
        module = _load_audit_module()
        linux = json.loads((REPO_ROOT / "bootstrap/seeds/i386-linux/manifest.json").read_bytes())
        plan = bootstrap._windows_build_plan(linux["build_plan"], utf8=True)
        sources = {row["name"]: row for row in plan["sources"]}
        profiles = {row.name: row for row in module._C_PP_PROFILE_ROWS}
        roots, macros, _ = module._c_preprocessor_profile_configuration()
        for role, profile in (
            ("windows_utf8", "HOSTED_I386_WINDOWS_UTF8"),
            ("windows_utf8_publication", "HOSTED_I386_WINDOWS_PUBLICATION"),
            ("windows_utf8_build", "HOSTED_I386_WINDOWS_BUILD"),
            ("runtime", "HOSTED_I386_WINDOWS_UTF8_GNU"),
        ):
            with self.subTest(role=role):
                self.assertIn(profile, profiles)
                expected = [("__SIZEOF_POINTER__", "4")]
                expected.extend(tuple(value.split("=", 1)) for value in sources[role]["definitions"])
                self.assertEqual([(name, value) for owner, name, value in macros if owner == profile], expected)
                self.assertEqual(profiles[profile].gnu_extensions,
                                 "CTOOL_TRUE" if sources[role]["gnu_extensions"] else "CTOOL_FALSE")
                self.assertEqual([(path, form) for owner, path, form in roots if owner == profile], [
                    ("/toolchain", module._C_PP_INCLUDE_BOTH),
                    ("/toolchain/hosted/i386-linux/include", "CTOOL_C_PP_INCLUDE_ANGLE")])

    def test_toolchain_startup_assembly_ownership_rejects_a_mutated_input(
        self,
    ):
        module = _load_audit_module()
        inputs = list(module.TOOLCHAIN_CONTRACT_CUPIDASM_OWNERSHIP_INPUTS)
        inputs.remove("toolchain/hosted/i386-windows/start.asm")
        inputs.append("toolchain/hosted/i386-windows/unchecked_start.asm")
        model = module.BuildModel(
            directory="toolchain",
            root_target="all",
            rules={},
            reachable=set(),
            direct_sources=set(inputs),
            generated_sources=set(),
            forced_sources=set(),
            includes_by_source={},
            include_search_paths=[],
            transforms=[
                {
                    "output": "toolchain/build/cupidc-contracts/manifest.json",
                    "operation": "generate_toolchain_manifest",
                    "tools": [
                        "cupid_assembler",
                        "cupid_c_compiler",
                        "cupid_c_contract",
                        "cupid_linker",
                        "host_python",
                    ],
                    "inputs": inputs,
                }
            ],
        )

        with self.assertRaisesRegex(
            module.AuditError,
            "Toolchain contract CupidASM startup ownership differs.*"
            "missing=.*start[.]asm.*unexpected=.*unchecked_start[.]asm",
        ):
            module._toolchain_contract_cupidasm_ownership_inputs([model])

    def test_ast_shape_omits_empty_interpreter_specific_fields(self):
        module = _load_audit_module()
        function = ast.parse("def probe():\n    return 1\n").body[0]
        shape = module._stable_ast_shape(function)

        self.assertNotIn("decorator_list", shape)
        self.assertNotIn("type_params", shape)
        self.assertIn("Return(value=Constant(value=1))", shape)

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
                    "__CUPID_AUDIT_VALUE_0 := value\n__CUPID_AUDIT_ORIGIN_0 := file\n"
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

    def test_plain_windows_user_make_selects_the_supported_all_goal(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        result = subprocess.run(
            [
                make,
                "OS=Windows_NT",
                "MAKE=:",
                "--no-print-directory",
                "-prRn",
            ],
            cwd=REPO_ROOT / "user",
            text=True,
            capture_output=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        match = re.search(
            r"(?m)^\.DEFAULT_GOAL\s*:=\s*(\S+)\s*$",
            result.stdout,
        )
        self.assertIsNotNone(
            match, "GNU Make omitted its selected default goal"
        )
        self.assertEqual(match.group(1), "all")

    def test_root_make_database_omits_unused_fat_offset_variable(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        result = subprocess.run(
            [
                make,
                "MAKE=:",
                "--no-print-directory",
                "-prRn",
                "FORCE",
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        assignments = re.findall(
            r"(?m)^FAT_OFFSET_BYTES\s*[:+?]?=.*$",
            result.stdout,
        )
        self.assertEqual(assignments, [])

    def test_native_user_tools_are_an_explicit_recursive_build(self):
        module = _load_audit_module()
        transform = {
            "output": "user/native-user-tools",
            "inputs": [],
            "tools": ["make"],
            "operation": "recursive_make",
            "recipe": [
                "$(MAKE) -C ../toolchain build/cupidc.exe build/cupidld.exe"
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
