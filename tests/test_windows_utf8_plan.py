import copy
import json
import tempfile
from pathlib import Path
import unittest
from unittest import mock

from tools import bootstrap_toolchain as bootstrap


ROOT = Path(__file__).resolve().parents[1]


class WindowsUtf8PlanTests(unittest.TestCase):
    def setUp(self):
        self.linux = json.loads((ROOT / "bootstrap/seeds/i386-linux/manifest.json").read_bytes())["build_plan"]

    def test_default_retains_installed_plan_identity(self):
        manifest = json.loads((ROOT / "bootstrap/seeds/i386-windows/manifest.json").read_bytes())
        plan = bootstrap._windows_build_plan(self.linux)
        self.assertEqual(bootstrap._build_plan_sha256(plan),
                         manifest["provenance"]["native_build_plan_sha256"])
        self.assertEqual(bootstrap._build_plan_sha256(plan), bootstrap.PROMOTED_WINDOWS_PLAN_SHA256)
        self.assertEqual(plan, bootstrap._windows_build_plan(self.linux, utf8=False))

    def test_candidate_binds_codec_runtime_and_each_adapter_role(self):
        before = copy.deepcopy(self.linux)
        plan = bootstrap._windows_build_plan(self.linux, utf8=True)
        self.assertEqual(before, self.linux)
        sources = {row["name"]: row for row in plan["sources"]}
        self.assertEqual(len(sources), len(plan["sources"]))
        self.assertEqual(sources["path_encoding"]["path"], "/toolchain/path_encoding.cc")
        self.assertEqual(sources["runtime"]["definitions"], ["CUPID_WINDOWS_UTF8=1"])
        self.assertEqual(sources["windows_utf8"]["definitions"], ["_WIN32=1"])
        self.assertEqual(sources["windows_utf8_publication"]["definitions"],
                         ["_WIN32=1", "CUPID_WINDOWS_PUBLICATION=1"])
        self.assertEqual(sources["windows_utf8_build"]["definitions"],
                         ["_WIN32=1", "CUPID_WINDOWS_BUILD=1"])
        for tool, links in plan["links"].items():
            role = ("windows_utf8_build" if tool == "cupidbuild" else
                    "windows_utf8_publication" if tool in ("cupidasm", "cupidld") else "windows_utf8")
            self.assertEqual([name for name in links if name.startswith("windows_utf8")], [role])
            self.assertEqual(links.count("path_encoding"), 1)
            self.assertEqual(links[0], "start")

    def test_candidate_selects_existing_wide_startups_and_imports(self):
        plan = bootstrap._windows_build_plan(self.linux, utf8=True)
        for row in plan["assembly_sources"]:
            self.assertTrue(Path(row["path"]).name.startswith("utf8_"))
            self.assertTrue((ROOT / row["path"].lstrip("/")).is_file())
        for tool, imports in plan["imports"].items():
            expected = [(library, name) for library, names in bootstrap._windows_utf8_imports(tool)
                        for name in names]
            self.assertEqual([(row["library"], row["procedure"]) for row in imports], expected)
            names = {row["procedure"] for row in imports}
            self.assertTrue({"GetCommandLineW", "CreateFileW", "GetCurrentDirectoryW", "SetLastError"} <= names)
            self.assertFalse({"GetCommandLineA", "CreateFileA", "GetCurrentDirectoryA"} & names)

    def test_non_boolean_selection_is_rejected(self):
        for value in (None, 0, 1, "yes", [], {}):
            with self.subTest(value=value), self.assertRaisesRegex(bootstrap.BootstrapError, "Boolean"):
                bootstrap._windows_build_plan(self.linux, utf8=value)

    def test_reserved_candidate_source_names_are_rejected(self):
        for name in ("publication_runtime", "path_encoding", "windows_utf8",
                     "windows_utf8_publication", "windows_utf8_build"):
            plan = copy.deepcopy(self.linux)
            row = copy.deepcopy(plan["sources"][0])
            row["name"] = name
            plan["sources"].append(row)
            with self.subTest(name=name), self.assertRaisesRegex(bootstrap.BootstrapError, "reserved Windows source name"):
                bootstrap._windows_build_plan(plan, utf8=True)

    def test_installed_seed_pair_remains_verifiable(self):
        for platform in ("i386-linux", "i386-windows"):
            with self.subTest(platform=platform):
                bootstrap.verify_seed_inputs(ROOT / "bootstrap/seeds" / platform / "manifest.json")

    def test_complete_import_cohorts_select_matching_mode(self):
        for utf8 in (False, True):
            plan = bootstrap._windows_build_plan(self.linux, utf8=utf8)
            self.assertIs(bootstrap._windows_plan_uses_utf8(plan), utf8)

    def test_mixed_missing_duplicate_and_changed_imports_reject(self):
        baseline = bootstrap._windows_build_plan(self.linux, utf8=True)
        ansi = bootstrap._windows_build_plan(self.linux)
        mutations = []
        plan = copy.deepcopy(baseline)
        plan["imports"]["cupidc"] = ansi["imports"]["cupidc"]
        mutations.append(plan)
        plan = copy.deepcopy(baseline)
        del plan["imports"]["cupidbuild"]
        mutations.append(plan)
        plan = copy.deepcopy(baseline)
        plan["imports"]["cupidc"].append(copy.deepcopy(plan["imports"]["cupidc"][0]))
        mutations.append(plan)
        for field in ("library", "procedure", "slot"):
            plan = copy.deepcopy(baseline)
            plan["imports"]["cupidc"][0][field] = "wrong"
            mutations.append(plan)
        for index, plan in enumerate(mutations):
            with self.subTest(index=index), self.assertRaises(bootstrap.BootstrapError):
                bootstrap._windows_plan_uses_utf8(plan)

    def test_stage_link_and_image_validation_use_selected_profile(self):
        for utf8 in (False, True):
            with self.subTest(utf8=utf8), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                plan = bootstrap._windows_build_plan(self.linux, utf8=utf8)
                plan["workers"] = 1
                producers = {name: root / name for name in bootstrap.CANDIDATE_TOOL_NAMES}
                with mock.patch.object(bootstrap, "_run_clean") as run, \
                     mock.patch.object(bootstrap, "_validate_i386_relocatable"), \
                     mock.patch.object(bootstrap, "_certify_relocatable_code_anchors"), \
                     mock.patch.object(bootstrap, "_validate_static_i386_pe32") as validate:
                    bootstrap._build_windows_stage(
                        mock.Mock(), root, root / "stage", producers, plan, "test")
                linked = [call.args[2] for call in run.call_args_list
                          if call.args[1] == producers["cupidld"]]
                self.assertEqual(len(linked), len(plan["links"]))
                for tool, arguments, validation in zip(plan["links"], linked, validate.call_args_list):
                    expected = bootstrap._windows_utf8_imports(tool) if utf8 else bootstrap._windows_imports(tool)
                    selectors = [arguments[index + 1] for index, argument in enumerate(arguments)
                                 if argument == "--import"]
                    self.assertEqual(selectors, ["__imp_" + name + "=" + library + ":" + name
                                                for library, names in expected for name in names])
                    self.assertEqual(validation.args[2], expected)

    def test_invalid_imports_reject_before_stage_directory_creation(self):
        plan = bootstrap._windows_build_plan(self.linux, utf8=True)
        plan["imports"] = {}
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaises(bootstrap.BootstrapError):
                bootstrap._build_windows_stage(mock.Mock(), root, root / "stage", {}, plan, "test")
            self.assertFalse((root / "stage").exists())

    def test_utf8_capture_includes_every_candidate_source_and_startup(self):
        snapshot = bootstrap.capture_source_snapshot(ROOT, self.linux, windows_utf8=True)
        plan = bootstrap._windows_build_plan(self.linux, utf8=True)
        for row in plan["sources"] + plan["assembly_sources"]:
            self.assertIn(row["path"].lstrip("/"), snapshot)
        self.assertIn("toolchain/path_encoding.h", snapshot)
        before = bootstrap.capture_source_snapshot(ROOT, self.linux)
        self.assertEqual(set(snapshot) - set(before), {
            "toolchain/path_encoding.cc",
            "toolchain/hosted/i386-windows/windows_utf8.cc",
            "toolchain/hosted/i386-windows/utf8_tool_start.asm",
            "toolchain/hosted/i386-windows/utf8_publication_start.asm",
            "toolchain/hosted/i386-windows/utf8_cupidbuild_start.asm",
        })

    def test_frozen_utf8_mode_survives_revalidation_and_detects_drift(self):
        with tempfile.TemporaryDirectory() as temporary:
            frozen = bootstrap.freeze_source_inputs(
                ROOT, self.linux, Path(temporary) / "frozen", windows_utf8=True)
            self.assertTrue(frozen.windows_utf8)
            bootstrap.require_source_closures(frozen, ROOT, self.linux)
            adapter = frozen.root / "toolchain/hosted/i386-windows/windows_utf8.cc"
            adapter.write_bytes(adapter.read_bytes() + b"\n/* drift */\n")
            with self.assertRaisesRegex(bootstrap.BootstrapError, "frozen source inputs changed"):
                bootstrap.require_frozen_source_snapshot(frozen, self.linux)

    def test_missing_utf8_header_or_source_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            frozen = bootstrap.freeze_source_inputs(
                ROOT, self.linux, Path(temporary) / "frozen", windows_utf8=True)
            for name in ("toolchain/path_encoding.h", "toolchain/path_encoding.cc",
                         "toolchain/hosted/i386-windows/utf8_cupidbuild_start.asm"):
                path = frozen.root / name
                raw = path.read_bytes()
                path.unlink()
                try:
                    with self.subTest(name=name), self.assertRaises(bootstrap.BootstrapError):
                        bootstrap.capture_source_snapshot(frozen.root, self.linux, windows_utf8=True)
                finally:
                    path.write_bytes(raw)

    def test_invalid_source_capture_mode_is_rejected(self):
        with self.assertRaisesRegex(bootstrap.BootstrapError, "Boolean"):
            bootstrap.capture_source_snapshot(ROOT, self.linux, windows_utf8="yes")

    def test_invalid_freeze_mode_rejects_before_creating_destination(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary) / "frozen"
            for value in (None, 0, 1, "yes", [], {}):
                with self.subTest(value=value), self.assertRaisesRegex(
                        bootstrap.BootstrapError, "Boolean"):
                    bootstrap.freeze_source_inputs(
                        ROOT, self.linux, destination, windows_utf8=value)
                self.assertFalse(destination.exists())

    def test_missing_wide_source_rejects_before_creating_destination(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            live = bootstrap.freeze_source_inputs(
                ROOT, self.linux, root / "live", windows_utf8=True)
            (live.root / "toolchain/path_encoding.cc").unlink()
            destination = root / "frozen"
            with self.assertRaisesRegex(bootstrap.BootstrapError, "cannot resolve source input"):
                bootstrap.freeze_source_inputs(
                    live.root, self.linux, destination, windows_utf8=True)
            self.assertFalse(destination.exists())

    def test_live_utf8_drift_is_detected_with_frozen_mode(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            live = bootstrap.freeze_source_inputs(
                ROOT, self.linux, root / "live", windows_utf8=True)
            frozen = bootstrap.freeze_source_inputs(
                live.root, self.linux, root / "frozen", windows_utf8=True)
            bootstrap.require_source_closures(frozen, live.root, self.linux)
            adapter = live.root / "toolchain/hosted/i386-windows/windows_utf8.cc"
            adapter.write_bytes(adapter.read_bytes() + b"\n/* live drift */\n")
            bootstrap.require_frozen_source_snapshot(frozen, self.linux)
            with self.assertRaisesRegex(bootstrap.BootstrapError, "source inputs changed"):
                bootstrap.require_source_closures(frozen, live.root, self.linux)


if __name__ == "__main__":
    unittest.main()
