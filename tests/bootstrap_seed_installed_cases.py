## @file
#  @brief Verify the installed seed pair and native Windows execution.
import ast
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tests.bootstrap_seed_fixture_profiles import observer_workers_for_seed
from tools.bootstrap_toolchain import (
    BootstrapError,
    CANDIDATE_TOOL_NAMES,
    PROMOTED_LINUX_MANIFEST_SHA256,
    PROMOTED_WINDOWS_MANIFEST_SHA256,
    Stage,
    TOOL_NAMES,
    ToolRunner,
    WINDOWS_SEED_SCHEMA,
    WINDOWS_SEED_SOURCE_SNAPSHOT_SHA256,
    WINDOWS_LINKER_SEED_IMPORTS,
    WINDOWS_TOOL_SEED_IMPORTS,
    _build_plan_sha256,
    _materialize_behavior_seed,
    _retarget_native_windows_behavior_seed,
    _windows_build_plan,
    _validate_static_i386_pe32,
    bootstrap_from_seed,
    capture_source_snapshot,
    freeze_seed_inputs,
    run_seed_tool,
    verify_seed_inputs,
)
from tools.cupidc_kernel_compile import KERNEL_I386_ARGUMENTS, validate_i386_relocatable_bytes

from tests.bootstrap_seed_suite_fixtures import (
    CURRENT_SOURCE_HEAD_INPUT_COUNT,
    BOOTSTRAP_TOOL,
    REPO_ROOT,
    SEED_MANIFEST,
    WINDOWS_SEED_MANIFEST,
    _promoted_windows_profile_flags,
)


class BootstrapSeedInstalledCases:
    def test_checked_i386_linux_seed_verifies(self):
        self.assertEqual(
            hashlib.sha256(SEED_MANIFEST.read_bytes()).hexdigest(),
            PROMOTED_LINUX_MANIFEST_SHA256,
        )
        result = subprocess.run(
            [
                sys.executable,
                str(BOOTSTRAP_TOOL),
                "verify",
                "--manifest",
                str(SEED_MANIFEST),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "checked i386 Linux seed: ok (6 tools)\n",
        )
        self.assertEqual(result.stderr, "")

    def test_checked_i386_linux_seed_carries_local_relative_target_policy(
        self,
    ):
        self._assert_checked_seed_local_relative_target_policy(SEED_MANIFEST)

    def test_checked_i386_linux_seed_carries_relocatable_local_target_policy(
        self,
    ):
        self._assert_checked_seed_relocatable_local_target_policy(
            SEED_MANIFEST
        )

    def test_checked_i386_linux_seed_carries_linked_local_target_policy(self):
        self._assert_checked_seed_linked_local_target_policy(SEED_MANIFEST)

    def test_checked_i386_linux_seed_carries_code_anchor_policy(self):
        self._assert_checked_seed_code_anchor_policy(SEED_MANIFEST)

    def test_checked_i386_linux_seed_snapshot_matches_its_named_commit(self):
        manifest = json.loads(SEED_MANIFEST.read_text(encoding="utf-8"))
        provenance = manifest["provenance"]
        revision = provenance["source_revision"]
        plan = manifest["build_plan"]
        windows_provenance = json.loads(
            WINDOWS_SEED_MANIFEST.read_text(encoding="utf-8")
        )["provenance"]
        flags = _promoted_windows_profile_flags(
            windows_provenance["native_build_plan_sha256"], provenance["source_input_count"]
        )
        live_inventory = capture_source_snapshot(
            REPO_ROOT,
            plan,
            windows_utf8=True,
            windows_long_paths=flags["long_paths"],
            windows_user_link_aliases=flags["user_link_aliases"],
            windows_observer_workers=observer_workers_for_seed(
                windows_provenance["native_build_plan_sha256"], provenance["source_input_count"]),
        )
        committed_inventory = self._committed_source_inventory(
            revision, live_inventory
        )

        encoded = json.dumps(
            committed_inventory,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=True,
        ).encode("ascii")
        self.assertEqual(
            len(committed_inventory), provenance["source_input_count"]
        )
        self.assertEqual(
            hashlib.sha256(encoded).hexdigest(),
            provenance["source_snapshot_sha256"],
        )

    def test_checked_i386_windows_seed_verifies(self):
        self.assertEqual(
            hashlib.sha256(WINDOWS_SEED_MANIFEST.read_bytes()).hexdigest(),
            PROMOTED_WINDOWS_MANIFEST_SHA256,
        )
        result = subprocess.run(
            [
                sys.executable,
                str(BOOTSTRAP_TOOL),
                "verify",
                "--manifest",
                str(WINDOWS_SEED_MANIFEST),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "checked i386 Windows seed: ok (6 tools)\n",
        )
        self.assertEqual(result.stderr, "")

    @unittest.skipUnless(os.name == "nt", "native Windows seed")
    def test_checked_i386_windows_seed_runs_without_wsl(self):
        with mock.patch(
            "tools.bootstrap_toolchain.shutil.which",
            side_effect=AssertionError("native seed must not probe WSL"),
        ):
            result = run_seed_tool(
                WINDOWS_SEED_MANIFEST,
                REPO_ROOT,
                "cupiddis",
                ("--help",),
                timeout=60,
            )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("usage:", result.stdout.casefold())
        self.assertIn("cupiddis", result.stdout.casefold())
        self.assertEqual(result.stderr, "")

    @unittest.skipUnless(os.name == "nt", "native Windows seed")
    def test_checked_i386_windows_seed_carries_local_relative_target_policy(
        self,
    ):
        self._assert_checked_seed_local_relative_target_policy(
            WINDOWS_SEED_MANIFEST,
            native_windows=True,
        )

    @unittest.skipUnless(os.name == "nt", "native Windows seed")
    def test_checked_i386_windows_seed_carries_relocatable_local_target_policy(
        self,
    ):
        self._assert_checked_seed_relocatable_local_target_policy(
            WINDOWS_SEED_MANIFEST,
            native_windows=True,
        )

    @unittest.skipUnless(os.name == "nt", "native Windows seed")
    def test_checked_i386_windows_seed_carries_linked_local_target_policy(self):
        self._assert_checked_seed_linked_local_target_policy(
            WINDOWS_SEED_MANIFEST,
            native_windows=True,
        )

    @unittest.skipUnless(os.name == "nt", "native Windows seed")
    def test_checked_i386_windows_seed_carries_code_anchor_policy(self):
        self._assert_checked_seed_code_anchor_policy(
            WINDOWS_SEED_MANIFEST,
            native_windows=True,
        )

    @unittest.skipUnless(os.name == "nt", "native Windows seed")
    def test_checked_i386_windows_seed_runs_all_tool_boundaries(self):
        with mock.patch(
            "tools.bootstrap_toolchain.shutil.which",
            side_effect=AssertionError("native seed must not probe WSL"),
        ):
            for tool in (
                "cupidasm",
                "cupiddis",
                "cupidld",
                "cupidobj",
                "cupidc",
            ):
                with self.subTest(tool=tool, case="help"):
                    result = run_seed_tool(
                        WINDOWS_SEED_MANIFEST,
                        REPO_ROOT,
                        tool,
                        ("--help",),
                        timeout=60,
                    )
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertIn("usage:", result.stdout.casefold())
                    self.assertEqual(result.stderr, "")
                with self.subTest(tool=tool, case="failure"):
                    result = run_seed_tool(
                        WINDOWS_SEED_MANIFEST,
                        REPO_ROOT,
                        tool,
                        ("--definitely-invalid-option",),
                        timeout=60,
                    )
                    self.assertEqual(result.returncode, 2)
                    self.assertIn("usage:", result.stderr.casefold())

    @unittest.skipUnless(os.name == "nt", "native Windows seed")
    def test_checked_i386_windows_cupidc_compiles_large_frame_source(self):
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-windows-large-frame-", dir=REPO_ROOT
        ) as temporary:
            outputs = {
                "windows": Path(temporary) / "keyboard-windows.o",
                "linux": Path(temporary) / "keyboard-linux.o",
            }
            manifests = {
                "windows": WINDOWS_SEED_MANIFEST,
                "linux": SEED_MANIFEST,
            }
            for platform, manifest in manifests.items():
                with self.subTest(platform=platform):
                    output = outputs[platform]
                    logical_output = (
                        "/" + output.relative_to(REPO_ROOT).as_posix()
                    )
                    output.write_bytes(b"previous object")
                    result = run_seed_tool(
                        manifest,
                        REPO_ROOT,
                        "cupidc",
                        (
                            "-c",
                            "/drivers/keyboard.cc",
                            "-o",
                            logical_output,
                            *KERNEL_I386_ARGUMENTS,
                            "--root",
                            REPO_ROOT,
                        ),
                        timeout=120,
                    )

                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout, "")
                    self.assertEqual(result.stderr, "")
                    self.assertNotEqual(
                        output.read_bytes(), b"previous object"
                    )
                    validate_i386_relocatable_bytes(output.read_bytes())

            self.assertEqual(
                outputs["windows"].read_bytes(),
                outputs["linux"].read_bytes(),
            )

    def test_checked_i386_windows_seed_rejects_unlisted_executable(self):
        for extra_name in ("unlisted.exe", "unlisted.EXE"):
            with self.subTest(
                extra_name=extra_name
            ), tempfile.TemporaryDirectory(
                prefix="cupid-bootstrap-windows-seed-"
            ) as temporary:
                copied_seed = Path(temporary) / "i386-windows"
                shutil.copytree(WINDOWS_SEED_MANIFEST.parent, copied_seed)
                (copied_seed / extra_name).write_bytes(b"MZ")

                with self.assertRaisesRegex(
                    BootstrapError,
                    "seed directory contains an unlisted PE32 file",
                ):
                    verify_seed_inputs(copied_seed / "manifest.json")

    def test_checked_i386_windows_seed_binds_execution_provenance(self):
        original = json.loads(
            WINDOWS_SEED_MANIFEST.read_text(encoding="utf-8")
        )
        cases = (
            (
                "unknown manifest field",
                lambda manifest: manifest.update({"unrecorded": True}),
                "manifest keys differ",
            ),
            (
                "wrong artifact generation",
                lambda manifest: manifest["provenance"].update(
                    {"artifact_generation": "stage-three"}
                ),
                "Windows seed generation differs",
            ),
            (
                "wrong fixed-point command",
                lambda manifest: manifest["provenance"].update(
                    {"fixed_point_command": "make bootstrap-from-seed"}
                ),
                "fixed-point command differs",
            ),
            (
                "failed fixed-point result",
                lambda manifest: manifest["provenance"].update(
                    {"fixed_point_result": "failed"}
                ),
                "seed lacks passing fixed-point provenance",
            ),
            (
                "wrong source snapshot",
                lambda manifest: manifest["provenance"].update(
                    {"source_snapshot_sha256": "0" * 64}
                ),
                "source snapshot differs",
            ),
            (
                "previous native build plan",
                lambda manifest: manifest["provenance"].update(
                    {
                        "native_build_plan_sha256": (
                            "f9dce66230a693de9d9d0e60127a4a6c44ea465989f381c995086bfe723cff14"
                        )
                    }
                ),
                "native build plan differs",
            ),
            (
                "unknown native build plan",
                lambda manifest: manifest["provenance"].update(
                    {"native_build_plan_sha256": "0" * 64}
                ),
                "native build plan differs",
            ),
            (
                "wrong source revision",
                lambda manifest: manifest["provenance"].update(
                    {"source_revision": "0" * 40}
                ),
                "source revision differs",
            ),
            (
                "wrong source count",
                lambda manifest: manifest["provenance"].update(
                    {"source_input_count": 49}
                ),
                "source input count differs",
            ),
            (
                "floating source count",
                lambda manifest: manifest["provenance"].update(
                    {"source_input_count": 50.0}
                ),
                "source input count differs",
            ),
            (
                "wrong parent execution seed",
                lambda manifest: manifest["provenance"].update(
                    {"parent_execution_seed_manifest_sha256": "0" * 64}
                ),
                "parent execution seed manifest differs",
            ),
            (
                "wrong parent execution source revision",
                lambda manifest: manifest["provenance"].update(
                    {"parent_execution_seed_source_revision": "0" * 40}
                ),
                "parent execution seed revision differs",
            ),
            (
                "wrong producer lineage",
                lambda manifest: manifest["provenance"][
                    "producer_lineage"
                ].update({"c": "unreviewed compiler"}),
                "producer lineage differs",
            ),
            (
                "wrong target type",
                lambda manifest: manifest["target"].update(
                    {"pe_class": 32.0}
                ),
                "manifest target field type differs: pe_class",
            ),
            (
                "wrong producer role",
                lambda manifest: manifest["artifacts"][0].update(
                    {"producer": False}
                ),
                "artifact producer role differs: cupidasm",
            ),
        )
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-windows-provenance-"
        ) as temporary:
            copied_seed = Path(temporary) / "seed"
            shutil.copytree(WINDOWS_SEED_MANIFEST.parent, copied_seed)
            manifest_path = copied_seed / "manifest.json"
            for label, mutate, expected in cases:
                with self.subTest(label=label):
                    manifest = json.loads(json.dumps(original))
                    mutate(manifest)
                    manifest_path.write_text(
                        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8",
                        newline="\n",
                    )
                    with self.assertRaisesRegex(
                        BootstrapError, f"^{expected}$"
                    ):
                        verify_seed_inputs(manifest_path)

    def test_legacy_windows_seed_accepts_native_producer_provenance(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-windows-stage-four-"
        ) as temporary:
            copied_seed = Path(temporary) / "seed"
            shutil.copytree(WINDOWS_SEED_MANIFEST.parent, copied_seed)
            manifest_path = copied_seed / "manifest.json"
            manifest = json.loads(
                manifest_path.read_text(encoding="utf-8")
            )
            manifest["schema"] = WINDOWS_SEED_SCHEMA
            manifest["artifacts"] = [
                artifact
                for artifact in manifest["artifacts"]
                if artifact["name"] != "cupidbuild"
            ]
            (copied_seed / "cupidbuild.exe").unlink()
            for artifact in manifest["artifacts"]:
                profile = (
                    WINDOWS_LINKER_SEED_IMPORTS
                    if artifact["name"] == "cupidld"
                    else WINDOWS_TOOL_SEED_IMPORTS
                )
                payload = self._minimal_cupidbuild_profile_pe32(profile)
                (copied_seed / artifact["file"]).write_bytes(payload)
                artifact["size"] = len(payload)
                artifact["sha256"] = hashlib.sha256(payload).hexdigest()
            manifest["provenance"] = {
                "artifact_generation": (
                    "paired-stage-four-native-windows"
                ),
                "fixed_point_command": (
                    "make bootstrap-windows-from-seed"
                ),
                "fixed_point_result": "pass",
                "parent_seed_manifest_sha256": (
                    "b6e34a2e18dd18aba91c6358116eafde"
                    "39953566efeadb224575ac8c13ab2c1b"
                ),
                "parent_seed_source_revision": (
                    "a17c9465911da41d59b7ada71733d36c39faa5ea"
                ),
                "producer_lineage": {
                    "assembly": (
                        "native stage-three CupidASM from the checked "
                        "i386 Windows bootstrap"
                    ),
                    "c": (
                        "native stage-three CupidC from the checked "
                        "i386 Windows bootstrap"
                    ),
                    "link": (
                        "native stage-three CupidLD from the checked "
                        "i386 Windows bootstrap"
                    ),
                },
                "source_input_count": 50,
                "source_revision": (
                    "a17c9465911da41d59b7ada71733d36c39faa5ea"
                ),
                "source_snapshot_sha256": (
                    WINDOWS_SEED_SOURCE_SNAPSHOT_SHA256
                ),
            }
            manifest_path.write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
                newline="\n",
            )

            verified = verify_seed_inputs(manifest_path)

            self.assertEqual(set(verified.tools), set(TOOL_NAMES))

    def test_checked_i386_windows_seed_snapshot_matches_its_named_commit(self):
        manifest = json.loads(
            WINDOWS_SEED_MANIFEST.read_text(encoding="utf-8")
        )
        provenance = manifest["provenance"]
        revision = provenance["source_revision"]
        plan = json.loads(SEED_MANIFEST.read_text(encoding="utf-8"))[
            "build_plan"
        ]
        windows_provenance = json.loads(
            WINDOWS_SEED_MANIFEST.read_text(encoding="utf-8")
        )["provenance"]
        flags = _promoted_windows_profile_flags(
            windows_provenance["native_build_plan_sha256"], provenance["source_input_count"]
        )
        live_inventory = capture_source_snapshot(
            REPO_ROOT,
            plan,
            windows_utf8=True,
            windows_long_paths=flags["long_paths"],
            windows_user_link_aliases=flags["user_link_aliases"],
            windows_observer_workers=observer_workers_for_seed(
                windows_provenance["native_build_plan_sha256"], provenance["source_input_count"]),
        )
        committed_inventory = self._committed_source_inventory(
            revision, live_inventory
        )

        encoded = json.dumps(
            committed_inventory,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=True,
        ).encode("ascii")
        self.assertEqual(
            len(committed_inventory), provenance["source_input_count"]
        )
        self.assertEqual(
            hashlib.sha256(encoded).hexdigest(),
            provenance["source_snapshot_sha256"],
        )

    def test_windows_execution_seed_cannot_drive_the_fixed_point(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-windows-role-"
        ) as temporary:
            output = Path(temporary) / "published"
            with self.assertRaisesRegex(
                BootstrapError,
                "^the native Windows execution seed cannot drive the "
                "Linux fixed-point bootstrap$",
            ):
                bootstrap_from_seed(
                    WINDOWS_SEED_MANIFEST, REPO_ROOT, output
                )
            self.assertFalse(output.exists())

    def test_native_windows_plan_is_derived_from_the_linux_plan(self):
        linux_plan = json.loads(
            SEED_MANIFEST.read_text(encoding="utf-8")
        )["build_plan"]
        linux_plan["include_arguments"] = [
            *linux_plan["include_arguments"],
            "-I",
            "/toolchain/plan-owned-include",
        ]
        linux_plan["sources"].append(
            {
                "gnu_extensions": False,
                "name": "plan_owned_source",
                "path": "/toolchain/plan_owned_source.cc",
            }
        )
        linux_plan["links"]["cupiddis"].insert(
            -1, "plan_owned_source"
        )

        native_plan = _windows_build_plan(linux_plan)

        self.assertEqual(
            native_plan["include_arguments"],
            linux_plan["include_arguments"],
        )
        self.assertEqual(
            [source["name"] for source in native_plan["sources"]],
            [source["name"] for source in linux_plan["sources"]]
            + ["publication_runtime"],
        )
        native_sources = {
            source["name"]: source
            for source in native_plan["sources"]
        }
        for linux_source in linux_plan["sources"]:
            expected_path = linux_source["path"]
            expected_gnu = linux_source["gnu_extensions"]
            if linux_source["name"] == "runtime":
                expected_path = (
                    "/toolchain/hosted/i386-windows/runtime.cc"
                )
                expected_gnu = True
            self.assertEqual(
                native_sources[linux_source["name"]]["path"],
                expected_path,
            )
            self.assertEqual(
                native_sources[linux_source["name"]][
                    "gnu_extensions"
                ],
                expected_gnu,
            )
        for tool_name in TOOL_NAMES:
            if tool_name in ("cupidasm", "cupidld"):
                continue
            self.assertEqual(
                native_plan["links"][tool_name],
                linux_plan["links"][tool_name],
            )
        self.assertEqual(
            native_plan["links"]["cupidasm"],
            [
                "start",
                "publication_start",
                *linux_plan["links"]["cupidasm"][1:-1],
                "publication_runtime",
                "runtime",
            ],
        )
        self.assertEqual(
            native_plan["links"]["cupidld"],
            [
                "start",
                "publication_start",
                *linux_plan["links"]["cupidld"][1:-1],
                "publication_runtime",
                "runtime",
            ],
        )
        cupidasm_imports = {
            imported["procedure"]
            for imported in native_plan["imports"]["cupidasm"]
        }
        self.assertTrue(
            {
                "DeleteFileA",
                "FlushFileBuffers",
                "GetFullPathNameA",
                "MoveFileExA",
            }.issubset(cupidasm_imports)
        )
        cupiddis_main = next(
            source
            for source in native_plan["sources"]
            if source["name"] == "cupiddis_main"
        )
        self.assertEqual(cupiddis_main["definitions"], ["_WIN32=1"])

    def test_native_windows_relink_uses_the_planned_cupidasm_imports(self):
        tree = ast.parse(BOOTSTRAP_TOOL.read_text(encoding="utf-8"))
        behavior = next(
            node
            for node in tree.body
            if isinstance(node, ast.FunctionDef)
            and node.name == "_run_native_windows_behavior_checks"
        )
        validators = [
            node
            for node in ast.walk(behavior)
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Name)
            and node.func.id == "_validate_static_i386_pe32"
        ]
        self.assertEqual(len(validators), 1)
        expected_imports = validators[0].args[2]
        self.assertIsInstance(expected_imports, ast.IfExp)
        self.assertEqual(expected_imports.test.id, "behavior_utf8")
        for selected, function in (
            (expected_imports.body, "_windows_utf8_imports"),
            (expected_imports.orelse, "_windows_imports"),
        ):
            self.assertIsInstance(selected, ast.Call)
            self.assertEqual(selected.func.id, function)
            self.assertEqual(
                [argument.value for argument in selected.args], ["cupidasm"]
            )
        links = [
            node for node in ast.walk(behavior)
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Name)
            and node.func.id == "_windows_link_arguments"
        ]
        self.assertEqual(len(links), 2)
        for link in links:
            self.assertEqual(
                [(item.arg, item.value.id) for item in link.keywords],
                [("utf8", "behavior_utf8"),
                 ("long_paths", "behavior_long_paths"),
                 ("user_link_aliases", "behavior_user_link_aliases")],
            )

    def test_linux_behavior_windows_cupidasm_uses_publication_closure(self):
        tree = ast.parse(BOOTSTRAP_TOOL.read_text(encoding="utf-8"))
        behavior = next(
            node
            for node in tree.body
            if isinstance(node, ast.FunctionDef)
            and node.name == "_run_behavior_checks"
        )

        def assigned_dict(name):
            assignment = next(
                node
                for node in behavior.body
                if isinstance(node, ast.Assign)
                and any(
                    isinstance(target, ast.Name) and target.id == name
                    for target in node.targets
                )
            )
            return {
                key.value: value
                for key, value in zip(
                    assignment.value.keys, assignment.value.values
                )
            }

        plans = assigned_dict("windows_native_tool_plans")
        self.assertEqual(
            [item.value for item in plans["cupidasm"].elts],
            [
                "publication_start",
                "cupidasm_main",
                "cupidasm",
                "ctool_host",
                "ctool",
                "elf32",
                "x86",
                "publication_runtime",
            ],
        )
        imports = assigned_dict("windows_native_tool_imports")
        self.assertIsInstance(imports["cupidasm"], ast.Name)
        self.assertEqual(
            imports["cupidasm"].id, "windows_cupidld_imports"
        )
        for extras_name in (
            "windows_native_stage_two_extras",
            "windows_native_stage_three_extras",
        ):
            extras = assigned_dict(extras_name)
            self.assertIn("cupidasm", extras)
            self.assertEqual(
                [key.value for key in extras["cupidasm"].keys],
                ["publication_runtime", "publication_start"],
            )

    def test_native_windows_plan_rejects_an_unplanned_link_object(self):
        linux_plan = json.loads(
            SEED_MANIFEST.read_text(encoding="utf-8")
        )["build_plan"]
        linux_plan["links"]["cupiddis"].insert(-1, "unplanned_object")

        with self.assertRaisesRegex(
            BootstrapError,
            "^Linux build plan links an unknown object: "
            "cupiddis: unplanned_object$",
        ):
            _windows_build_plan(linux_plan)

    @unittest.skipUnless(os.name == "nt", "native Windows bootstrap")
    def test_checked_windows_seed_prepares_the_long_alias_source_head(self):
        with tempfile.TemporaryDirectory(
            prefix=".checked-windows-bootstrap-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            output = root / "prepared"
            environment = dict(os.environ)
            environment["PATH"] = str(root / "no-host-tools")
            for name in ("CC", "CXX", "CPP", "HOSTCC", "HOSTCXX", "ASM",
                         "AS", "LD", "AR", "NM", "OBJCOPY"):
                environment[name] = f"__cupid_host_{name}_must_not_run__"
            result = subprocess.run(
                [sys.executable, "-m", "tools.bootstrap_stage_release",
                 "prepare-windows", "--windows-long-paths",
                 "--linux-manifest", str(SEED_MANIFEST),
                 "--windows-manifest", str(WINDOWS_SEED_MANIFEST),
                 "--root", str(REPO_ROOT), "--output", str(output)],
                cwd=REPO_ROOT, env=environment, text=True,
                capture_output=True, timeout=3000,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout,
                             "stage preparation: written (behavior pending)\n")
            self.assertEqual(result.stderr, "")
            self.assertEqual(sorted(path.name for path in output.iterdir()),
                             ["source", "stage-four", "stage-preparation.json",
                              "stage-three", "stage-two"])
            record = json.loads((output / "stage-preparation.json").read_text())
            self.assertEqual(record["schema"], "cupid.bootstrap-stage-preparation.v1")
            self.assertEqual((record["status"], record["format"]),
                             ("unqualified", "pe32"))
            self.assertEqual(len(record["source_inputs"]), CURRENT_SOURCE_HEAD_INPUT_COUNT)
            self.assertEqual(_build_plan_sha256(record["linux_plan"]),
                             "e58a047f92d21a9ffbae6cc19726477f95ca3a872f1238d9cbab58dae950faaa")
            self.assertEqual(_build_plan_sha256(record["windows_plan"]),
                             "edf91edb181c104e9762ac7f68c4636b5da153ac43e24f1d4dc941424f96e63c")
            snapshot = capture_source_snapshot(REPO_ROOT, record["linux_plan"],
                windows_utf8=True, windows_long_paths=True, windows_user_link_aliases=True,
                windows_observer_workers=True)
            self.assertEqual(record["source_inputs"], snapshot)
            for name, identity in snapshot.items():
                self._assert_file_artifact_identity(identity, output / "source" / name)
            for stage_name in ("stage-two", "stage-three", "stage-four"):
                stage = record["stages"][stage_name]
                self.assertEqual(len(stage["objects"]), 54)
                self.assertEqual(set(stage["tools"]), set(CANDIDATE_TOOL_NAMES))
                for kind in ("objects", "tools"):
                    for role, identity in stage[kind].items():
                        self._assert_file_artifact_identity(
                            {key: identity[key] for key in ("sha256", "size")},
                            output / stage_name / identity["file"])
            for kind in ("objects", "tools"):
                self.assertEqual(record["stages"]["stage-three"][kind],
                                 record["stages"]["stage-four"][kind])
            # Changed long/alias bytes need an explicit paired release. Keep
            # the original native entry point's parent rejection (ADR 0411).
            frozen = freeze_seed_inputs(WINDOWS_SEED_MANIFEST, root / "windows-seed")
            linux = freeze_seed_inputs(SEED_MANIFEST, root / "linux-seed")
            retargeted = _retarget_native_windows_behavior_seed(
                frozen, _build_plan_sha256(record["windows_plan"]),
                record["linux_plan"], snapshot, utf8=True, long_paths=True,
                user_link_aliases=True, observer_workers=True, parent_plan_seed=linux)
            tools = {name: output / "stage-four" / row["file"]
                     for name, row in record["stages"]["stage-four"]["tools"].items()}
            manifest = _materialize_behavior_seed(retargeted, root,
                "unreleased-behavior", Stage(objects={}, tools=tools))
            rejected = subprocess.run(
                [str(tools["cupidbuild"]), "run", "--seed-manifest", str(manifest),
                 "--root", str(REPO_ROOT), "--tool", "cupidobj", "--", "--help"],
                cwd=REPO_ROOT, env=environment, text=True, capture_output=True, timeout=60)
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn("fixed-point provenance differs", rejected.stderr)
            self.assertFalse((output / "bootstrap-report.json").exists())

    def test_checked_seed_builds_and_runs_imported_pe32(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-imported-pe32-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            startup_source = (
                REPO_ROOT / "toolchain/hosted/i386-windows/start.asm"
            )
            contract_source = (
                REPO_ROOT
                / "toolchain/tests/hosted_i386_windows_contract.cc"
            )
            startup_object = root / "start.o"
            contract_object = root / "contract.o"
            image = root / "runtime.exe"
            invalid_source = root / "invalid-import.asm"
            invalid_object = root / "invalid-import.o"
            invalid_image = root / "invalid-import.exe"
            invalid_source.write_text(
                "bits 32\n"
                "extern __imp_ExitProcess\n"
                "global _start\n"
                "section .text\n"
                "_start:\n"
                "    call __imp_ExitProcess\n"
                "    ret\n",
                encoding="utf-8",
                newline="\n",
            )
            invalid_image.write_bytes(b"sentinel")
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)

            assembled = runner.run(
                frozen.tools["cupidasm"],
                ["-f", "elf32", startup_source, "-o", startup_object],
                120,
            )
            self.assertEqual(assembled.returncode, 0, assembled.stderr)
            self.assertEqual(assembled.stdout, "")
            self.assertEqual(assembled.stderr, "")
            compiled = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "--freestanding",
                    "-c",
                    "/" + contract_source.relative_to(REPO_ROOT).as_posix(),
                    "-o",
                    "/" + contract_object.relative_to(REPO_ROOT).as_posix(),
                ],
                180,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            self.assertEqual(compiled.stdout, "")
            self.assertEqual(compiled.stderr, "")
            imports = (
                "__imp_WriteFile=KERNEL32.dll:WriteFile",
                "__imp_ExitProcess=KERNEL32.dll:ExitProcess",
                "__imp_GetStdHandle=KERNEL32.dll:GetStdHandle",
            )
            linked = runner.run(
                frozen.tools["cupidld"],
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
                    image,
                    startup_object,
                    contract_object,
                ],
                180,
            )
            self.assertEqual(linked.returncode, 0, linked.stderr)
            self.assertEqual(linked.stdout, "")
            self.assertEqual(linked.stderr, "")
            _validate_static_i386_pe32(
                image,
                0x00401000,
                (
                    (
                        "KERNEL32.dll",
                        ("ExitProcess", "GetStdHandle", "WriteFile"),
                    ),
                ),
            )
            if os.name == "nt":
                executed = subprocess.run(
                    [str(image)],
                    cwd=REPO_ROOT,
                    capture_output=True,
                    timeout=10,
                )
                self.assertEqual(executed.returncode, 37)
                self.assertEqual(
                    executed.stdout, b"Cupid-built Windows runtime: ok\n"
                )
                self.assertEqual(executed.stderr, b"")

            assembled_invalid = runner.run(
                frozen.tools["cupidasm"],
                ["-f", "elf32", invalid_source, "-o", invalid_object],
                120,
            )
            self.assertEqual(
                assembled_invalid.returncode,
                0,
                assembled_invalid.stderr,
            )
            self.assertEqual(assembled_invalid.stdout, "")
            self.assertEqual(assembled_invalid.stderr, "")
            rejected = runner.run(
                frozen.tools["cupidld"],
                [
                    "-m",
                    "i386pe",
                    "--text-address",
                    "0x00401000",
                    "--entry",
                    "_start",
                    "--import",
                    imports[1],
                    "-o",
                    invalid_image,
                    invalid_object,
                ],
                180,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn(
                "IAT symbols require an absolute zero-addend relocation",
                rejected.stderr,
            )
            self.assertEqual(invalid_image.read_bytes(), b"sentinel")

    def test_checked_seed_builds_and_runs_native_windows_tool_boundary(self):
        if os.name != "nt":
            self.skipTest("native PE32 execution requires Windows")
        if shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-windows-cupiddis-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)
            sources = (
                "ctool",
                "ctool_host",
                "elf32",
                "x86",
                "cupiddis",
                "cupiddis_main",
                "cupidld",
            )
            objects: dict[str, Path] = {}
            for name in sources:
                source = REPO_ROOT / "toolchain" / f"{name}.cc"
                output = root / f"{name}.o"
                arguments = ["--root", REPO_ROOT]
                if name in ("ctool_host", "cupiddis_main"):
                    arguments.extend(("-D", "_WIN32=1"))
                arguments.extend(
                    (
                        "-c",
                        "/" + source.relative_to(REPO_ROOT).as_posix(),
                        "-I",
                        "/toolchain",
                        "--include-angle",
                        "/toolchain/hosted/i386-linux/include",
                        "-o",
                        "/" + output.relative_to(REPO_ROOT).as_posix(),
                    )
                )
                result = runner.run(
                    frozen.tools["cupidc"], arguments, 360
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertEqual(result.stderr, "")
                objects[name] = output

            runtime_source = (
                REPO_ROOT / "toolchain/hosted/i386-windows/runtime.cc"
            )
            runtime_object = root / "runtime.o"
            compiled_runtime = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "--gnu",
                    "-c",
                    "/" + runtime_source.relative_to(REPO_ROOT).as_posix(),
                    "--include-angle",
                    "/toolchain/hosted/i386-linux/include",
                    "-o",
                    "/" + runtime_object.relative_to(REPO_ROOT).as_posix(),
                ],
                360,
            )
            self.assertEqual(
                compiled_runtime.returncode, 0, compiled_runtime.stderr
            )
            self.assertEqual(compiled_runtime.stdout, "")
            self.assertEqual(compiled_runtime.stderr, "")

            runtime_contract_source = (
                REPO_ROOT
                / "toolchain/tests/hosted_i386_windows_runtime_contract.cc"
            )
            runtime_contract_object = root / "runtime-contract.o"
            compiled_contract = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "-c",
                    "/"
                    + runtime_contract_source.relative_to(
                        REPO_ROOT
                    ).as_posix(),
                    "--include-angle",
                    "/toolchain/hosted/i386-linux/include",
                    "-o",
                    "/"
                    + runtime_contract_object.relative_to(
                        REPO_ROOT
                    ).as_posix(),
                ],
                360,
            )
            self.assertEqual(
                compiled_contract.returncode, 0, compiled_contract.stderr
            )
            self.assertEqual(compiled_contract.stdout, "")
            self.assertEqual(compiled_contract.stderr, "")

            startup_source = (
                REPO_ROOT / "toolchain/hosted/i386-windows/tool_start.asm"
            )
            startup_lines = [
                line.strip()
                for line in startup_source.read_text(
                    encoding="utf-8"
                ).splitlines()
            ]
            entry_start = startup_lines.index("_start:") + 1
            entry_end = startup_lines.index(
                "cupid_windows_close_handle:"
            )
            self.assertEqual(
                startup_lines[entry_start:entry_end],
                [
                    "cld",
                    "and esp, 0xfffffff0",
                    "call dword [__imp_GetCommandLineA]",
                    "sub esp, 12",
                    "push eax",
                    "call cupid_windows_runtime_start",
                    "add esp, 16",
                    "sub esp, 12",
                    "push eax",
                    "call dword [__imp_ExitProcess]",
                    "hlt",
                    "",
                ],
            )
            startup_object = root / "start.o"
            assembled = runner.run(
                frozen.tools["cupidasm"],
                ["-f", "elf32", startup_source, "-o", startup_object],
                120,
            )
            self.assertEqual(assembled.returncode, 0, assembled.stderr)
            self.assertEqual(assembled.stdout, "")
            self.assertEqual(assembled.stderr, "")

            imports = (
                "__imp_CloseHandle=KERNEL32.dll:CloseHandle",
                "__imp_CreateFileA=KERNEL32.dll:CreateFileA",
                "__imp_ExitProcess=KERNEL32.dll:ExitProcess",
                "__imp_GetCommandLineA=KERNEL32.dll:GetCommandLineA",
                "__imp_GetCurrentDirectoryA=KERNEL32.dll:GetCurrentDirectoryA",
                "__imp_GetFileInformationByHandle=KERNEL32.dll:GetFileInformationByHandle",
                "__imp_GetLastError=KERNEL32.dll:GetLastError",
                "__imp_GetStdHandle=KERNEL32.dll:GetStdHandle",
                "__imp_ReadFile=KERNEL32.dll:ReadFile",
                "__imp_SetFilePointer=KERNEL32.dll:SetFilePointer",
                "__imp_VirtualAlloc=KERNEL32.dll:VirtualAlloc",
                "__imp_VirtualFree=KERNEL32.dll:VirtualFree",
                "__imp_WriteFile=KERNEL32.dll:WriteFile",
            )
            image = root / "cupiddis.exe"
            link_arguments: list[str | Path] = [
                "-m",
                "i386pe",
                "--text-address",
                "0x00401000",
                "--entry",
                "_start",
            ]
            for imported in reversed(imports):
                link_arguments.extend(("--import", imported))
            link_arguments.extend(
                (
                    "-o",
                    image,
                    startup_object,
                    objects["cupiddis_main"],
                    objects["cupiddis"],
                    objects["ctool_host"],
                    objects["ctool"],
                    objects["elf32"],
                    objects["x86"],
                    runtime_object,
                )
            )
            linked = runner.run(
                frozen.tools["cupidld"], link_arguments, 180
            )
            self.assertEqual(linked.returncode, 0, linked.stderr)
            self.assertEqual(linked.stdout, "")
            self.assertEqual(linked.stderr, "")
            _validate_static_i386_pe32(
                image,
                0x00401000,
                (
                    (
                        "KERNEL32.dll",
                        tuple(
                            item.rsplit(":", 1)[1] for item in imports
                        ),
                    ),
                ),
            )

            help_result = subprocess.run(
                [str(image), "--help"],
                cwd=root,
                capture_output=True,
                timeout=20,
            )
            self.assertEqual(help_result.returncode, 0)
            self.assertIn(b"usage: cupiddis", help_result.stdout)
            self.assertEqual(help_result.stderr, b"")

            raw_input = root / "quoted input.bin"
            raw_input.write_bytes(b"\x90\xc3")
            arguments = [
                "--raw",
                "--mode",
                "32",
                "--base",
                "0",
                raw_input,
            ]
            oracle = runner.run(
                frozen.tools["cupiddis"], arguments, 60
            )
            self.assertEqual(oracle.returncode, 0, oracle.stderr)
            native = subprocess.run(
                [str(image), *[str(argument) for argument in arguments]],
                cwd=root,
                capture_output=True,
                timeout=20,
            )
            self.assertEqual(native.returncode, oracle.returncode)
            self.assertEqual(native.stdout, oracle.stdout.encode("utf-8"))
            self.assertEqual(native.stderr, oracle.stderr.encode("utf-8"))

            missing = root / "missing input.bin"
            failed = subprocess.run(
                [
                    str(image),
                    "--raw",
                    "--mode",
                    "32",
                    "--base",
                    "0",
                    str(missing),
                ],
                cwd=root,
                capture_output=True,
                timeout=20,
            )
            self.assertEqual(failed.returncode, 1)
            self.assertEqual(failed.stdout, b"")
            self.assertIn(b"cannot load", failed.stderr)
            self.assertIn(b"not_found", failed.stderr)

            runtime_contract_image = root / "runtime-contract.exe"
            contract_link_arguments: list[str | Path] = [
                "-m",
                "i386pe",
                "--text-address",
                "0x00401000",
                "--entry",
                "_start",
            ]
            for imported in imports:
                contract_link_arguments.extend(("--import", imported))
            contract_link_arguments.extend(
                (
                    "-o",
                    runtime_contract_image,
                    startup_object,
                    runtime_contract_object,
                    runtime_object,
                )
            )
            contract_linked = runner.run(
                frozen.tools["cupidld"], contract_link_arguments, 180
            )
            self.assertEqual(
                contract_linked.returncode, 0, contract_linked.stderr
            )
            self.assertEqual(contract_linked.stdout, "")
            self.assertEqual(contract_linked.stderr, "")
            _validate_static_i386_pe32(
                runtime_contract_image,
                0x00401000,
                (
                    (
                        "KERNEL32.dll",
                        tuple(
                            item.rsplit(":", 1)[1] for item in imports
                        ),
                    ),
                ),
            )

            contract_output = root / "runtime output.bin"
            contract_missing = root / "runtime missing.bin"
            runtime_result = subprocess.run(
                [
                    str(runtime_contract_image),
                    "plain",
                    "space arg",
                    'quote"arg',
                    "trailing\\",
                    str(contract_output),
                    str(contract_missing),
                ],
                cwd=root,
                capture_output=True,
                timeout=20,
            )
            self.assertEqual(
                runtime_result.returncode, 0, runtime_result.stderr
            )
            self.assertEqual(
                runtime_result.stdout,
                b"Cupid-built Windows tool runtime: ok\n",
            )
            self.assertEqual(runtime_result.stderr, b"")
            self.assertEqual(contract_output.read_bytes(), b"headtail")

            runtime_failure = subprocess.run(
                [str(runtime_contract_image), "wrong"],
                cwd=root,
                capture_output=True,
                timeout=20,
            )
            self.assertEqual(runtime_failure.returncode, 41)
            self.assertEqual(runtime_failure.stdout, b"")
            self.assertIn(
                b"windows runtime arguments: bad", runtime_failure.stderr
            )

            cupidld_main_source = REPO_ROOT / "toolchain/cupidld_main.cc"
            cupidld_main_object = root / "cupidld-main-windows.o"
            compiled_cupidld_main = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "-D",
                    "_WIN32=1",
                    "-c",
                    "/"
                    + cupidld_main_source.relative_to(REPO_ROOT).as_posix(),
                    "-I",
                    "/toolchain",
                    "--include-angle",
                    "/toolchain/hosted/i386-linux/include",
                    "-o",
                    "/"
                    + cupidld_main_object.relative_to(REPO_ROOT).as_posix(),
                ],
                360,
            )
            self.assertEqual(
                compiled_cupidld_main.returncode,
                0,
                compiled_cupidld_main.stderr,
            )
            self.assertEqual(compiled_cupidld_main.stdout, "")
            self.assertEqual(compiled_cupidld_main.stderr, "")

            publication_runtime_source = (
                REPO_ROOT
                / "toolchain/hosted/i386-windows/publication_runtime.cc"
            )
            publication_runtime_object = root / "publication-runtime.o"
            compiled_publication_runtime = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "-D",
                    "_WIN32=1",
                    "-c",
                    "/"
                    + publication_runtime_source.relative_to(
                        REPO_ROOT
                    ).as_posix(),
                    "--include-angle",
                    "/toolchain/hosted/i386-linux/include",
                    "-o",
                    "/"
                    + publication_runtime_object.relative_to(
                        REPO_ROOT
                    ).as_posix(),
                ],
                360,
            )
            self.assertEqual(
                compiled_publication_runtime.returncode,
                0,
                compiled_publication_runtime.stderr,
            )
            self.assertEqual(compiled_publication_runtime.stdout, "")
            self.assertEqual(compiled_publication_runtime.stderr, "")

            publication_start_source = (
                REPO_ROOT
                / "toolchain/hosted/i386-windows/publication_start.asm"
            )
            publication_start_object = root / "publication-start.o"
            assembled_publication_start = runner.run(
                frozen.tools["cupidasm"],
                [
                    "-f",
                    "elf32",
                    publication_start_source,
                    "-o",
                    publication_start_object,
                ],
                120,
            )
            self.assertEqual(
                assembled_publication_start.returncode,
                0,
                assembled_publication_start.stderr,
            )
            self.assertEqual(assembled_publication_start.stdout, "")
            self.assertEqual(assembled_publication_start.stderr, "")

            publication_imports = (
                "__imp_DeleteFileA=KERNEL32.dll:DeleteFileA",
                "__imp_FlushFileBuffers=KERNEL32.dll:FlushFileBuffers",
                "__imp_GetFullPathNameA=KERNEL32.dll:GetFullPathNameA",
                "__imp_MoveFileExA=KERNEL32.dll:MoveFileExA",
            )
            cupidld_image = root / "cupidld.exe"
            cupidld_link_arguments: list[str | Path] = [
                "-m",
                "i386pe",
                "--text-address",
                "0x00401000",
                "--entry",
                "_start",
            ]
            for imported in (*imports, *publication_imports):
                cupidld_link_arguments.extend(("--import", imported))
            cupidld_link_arguments.extend(
                (
                    "-o",
                    cupidld_image,
                    startup_object,
                    publication_start_object,
                    cupidld_main_object,
                    objects["cupidld"],
                    objects["ctool_host"],
                    objects["ctool"],
                    objects["elf32"],
                    runtime_object,
                    publication_runtime_object,
                )
            )
            linked_cupidld = runner.run(
                frozen.tools["cupidld"], cupidld_link_arguments, 180
            )
            self.assertEqual(
                linked_cupidld.returncode, 0, linked_cupidld.stderr
            )
            self.assertEqual(linked_cupidld.stdout, "")
            self.assertEqual(linked_cupidld.stderr, "")
            _validate_static_i386_pe32(
                cupidld_image,
                0x00401000,
                (
                    (
                        "KERNEL32.dll",
                        tuple(
                            sorted(
                                item.rsplit(":", 1)[1]
                                for item in (*imports, *publication_imports)
                            )
                        ),
                    ),
                ),
            )

            cupidld_help = subprocess.run(
                [str(cupidld_image), "--help"],
                cwd=root,
                capture_output=True,
                timeout=20,
            )
            cupidld_help_oracle = runner.run(
                frozen.tools["cupidld"], ["--help"], 60
            )
            self.assertEqual(cupidld_help.returncode, 0)
            self.assertEqual(cupidld_help_oracle.returncode, 0)
            self.assertEqual(
                cupidld_help.stdout,
                cupidld_help_oracle.stdout.replace(
                    "usage: cupidld -m", "usage: cupidld [--caller-owned-output] -m"
                ).replace(
                    "       cupidld -m", "       cupidld [--caller-owned-output] -m"
                ).encode("utf-8"),
            )
            self.assertEqual(cupidld_help.stderr, b"")

            native_link_output = root / "native linked output.exe"
            native_link_output.write_bytes(b"sentinel")
            occupied_candidate = root / (
                "native linked output.exe.cupid-tmp-00000000"
            )
            occupied_candidate.write_bytes(b"occupied")
            native_link_arguments = [
                "-m",
                "i386pe",
                "--text-address",
                "0x00401000",
                "--entry",
                "_start",
            ]
            for imported in imports:
                native_link_arguments.extend(("--import", imported))
            native_link_arguments.extend(
                (
                    "-o",
                    native_link_output.name,
                    startup_object.name,
                    runtime_contract_object.name,
                    runtime_object.name,
                )
            )
            native_link = subprocess.run(
                [str(cupidld_image), *native_link_arguments],
                cwd=root,
                capture_output=True,
                timeout=60,
            )
            self.assertEqual(native_link.returncode, 0, native_link.stderr)
            self.assertEqual(native_link.stdout, b"")
            self.assertEqual(native_link.stderr, b"")
            self.assertEqual(
                native_link_output.read_bytes(),
                runtime_contract_image.read_bytes(),
            )
            self.assertEqual(occupied_candidate.read_bytes(), b"occupied")
            self.assertEqual(
                sorted(root.glob("native linked output.exe.cupid-tmp-*")),
                [occupied_candidate],
            )

            blocked_output = root / "blocked-output.exe"
            blocked_output.mkdir()
            blocked_arguments = list(native_link_arguments)
            blocked_arguments[
                blocked_arguments.index(native_link_output.name)
            ] = blocked_output.name
            blocked_link = subprocess.run(
                [str(cupidld_image), *blocked_arguments],
                cwd=root,
                capture_output=True,
                timeout=60,
            )
            self.assertEqual(blocked_link.returncode, 1)
            self.assertEqual(blocked_link.stdout, b"")
            self.assertIn(b"cupidld: link failed (io)", blocked_link.stderr)
            self.assertTrue(blocked_output.is_dir())
            self.assertEqual(
                sorted(root.glob("blocked-output.exe.cupid-tmp-*")), []
            )
