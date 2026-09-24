import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
ROLES = ("cupidc", "cupidasm", "cupiddis", "cupidobj", "cupidld", "cupidbuild")


class NativeUtf8ToolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        make = shutil.which("make")
        compiler = shutil.which("clang")
        if make is None or compiler is None:
            raise unittest.SkipTest("Make and Clang are required for native tool tests")
        cls.temporary = tempfile.TemporaryDirectory(prefix="cupid-native-tools-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.root = Path(cls.temporary.name)
        cls.build = cls.root / "build"
        cls.suffix = ".exe" if os.name == "nt" else ""
        targets = [(cls.build / (role + cls.suffix)).as_posix() for role in ROLES]
        command = [make, "-C", str(ROOT / "toolchain"), "-j4",
                   "CC=" + compiler, "BUILD_DIR=" + cls.build.as_posix()]
        if os.name == "nt":
            command.append("HOST_REPRO_LDFLAGS=-fuse-ld=lld -Wl,/Brepro")
        result = subprocess.run(command + targets, capture_output=True, timeout=240)
        if result.returncode != 0:
            raise AssertionError((result.stdout + result.stderr).decode(errors="replace"))
        platform = "i386-windows" if os.name == "nt" else "i386-linux"
        cls.seed = ROOT / "bootstrap/seeds" / platform

    @classmethod
    def prepare_checked_seed(cls):
        if os.name == "nt":
            import json
            from tools import bootstrap_toolchain as bootstrap
            execution = bootstrap.freeze_seed_inputs(cls.seed / "manifest.json", cls.root / "execution-seed")
            cls.addClassCleanup(bootstrap.require_live_seed_inputs, execution)
            linux = json.loads((ROOT / "bootstrap/seeds/i386-linux/manifest.json").read_bytes())["build_plan"]
            plan = bootstrap._windows_build_plan(linux, utf8=True)
            sources = bootstrap.freeze_source_inputs(
                ROOT, linux, cls.root / "checked-source", windows_utf8=True)
            cls.addClassCleanup(bootstrap.require_source_closures, sources, ROOT, linux)
            stage = bootstrap._build_windows_stage(
                bootstrap.ToolRunner(sources.root), sources.root, sources.root / "stage",
                execution.tools, plan, "UTF-8 test cohort")
            behavior = bootstrap._retarget_native_windows_behavior_seed(
                execution, bootstrap._build_plan_sha256(plan), linux, sources.inventory, utf8=True)
            cls.seed = bootstrap._materialize_behavior_seed(
                behavior, cls.root, "candidate-seed", stage).parent

    def invoke(self, programs, role, arguments, cwd, expected=0):
        result = subprocess.run(
            [str(programs / (role + self.suffix)), *map(str, arguments)],
            cwd=cwd, capture_output=True, timeout=120)
        self.assertEqual(result.returncode, expected,
                         (role, result.stdout, result.stderr))
        return result

    def test_all_six_native_tools_accept_help(self):
        for role in ROLES:
            with self.subTest(role=role):
                self.invoke(self.build, role, ["--help"], self.root)

    def test_platform_selects_matching_entry_and_host_objects(self):
        selected = {path.name for path in self.build.glob("*.o")}
        wide = {"native_utf8.o", "native_utf8_entry.o", "ctool_host_utf8.o",
                "cupidbuild_host_utf8.o", "path_encoding.o",
                *(role + "_native_utf8_main.o" for role in ROLES)}
        if os.name == "nt":
            self.assertTrue(wide <= selected)
            self.assertNotIn("ctool_host.o", selected)
            self.assertNotIn("cupidbuild_host.o", selected)
        else:
            self.assertFalse(wide & selected)
            self.assertIn("ctool_host.o", selected)
            self.assertIn("cupidbuild_host.o", selected)

    def test_unicode_executables_paths_publication_and_rejection(self):
        self.prepare_checked_seed()
        expected_artifacts = None
        seed = self.seed
        before = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                  for path in seed.iterdir() if path.is_file()}
        for index, name in enumerate(("ascii space", "caf\u00e9 space", "\u6771\u4eac", "\U0001f63a space")):
            with self.subTest(root=name):
                directory = self.root / name
                directory.mkdir()
                local_seed = directory / "seed"
                shutil.copytree(seed, local_seed)
                programs = directory / "tools"
                programs.mkdir()
                for role in ROLES:
                    shutil.copy2(self.build / (role + self.suffix), programs / (role + self.suffix))
                source = "\u6771\u4eac source.cc"
                assembly = "caf\u00e9 source.asm"
                assembled = "\U0001f63a assembled.o"
                compiled = "\U0001f63a compiled.o"
                payload = "\u6771\u4eac payload.txt"
                wrapped = "caf\u00e9 wrapped.o"
                linked = "\U0001f63a linked.elf"
                guarded = "\u6771\u4eac guarded.o"
                (directory / source).write_bytes(b"int checked_runner_value(void) { return 42; }\n")
                (directory / assembly).write_bytes(b"bits 32\nsection .text\nglobal entry\nentry:\nmov eax, 42\nret\n")
                (directory / payload).write_bytes(b"wide runtime text\r\n")
                self.invoke(programs, "cupidc", ["--freestanding", "-c", source, "-o", compiled], directory)
                self.invoke(programs, "cupidasm", ["-f", "elf32", "-o", assembled, assembly], directory)
                result = self.invoke(programs, "cupiddis", [assembled], directory)
                self.assertIn(b"ret", result.stdout)
                self.invoke(programs, "cupidobj", ["wrap-text", payload, "-o", wrapped], directory)
                self.invoke(programs, "cupidld", ["-m", "elf_i386", "--text-address", "0x08048000",
                                                "--entry", "entry", "-o", linked, assembled], directory)
                self.invoke(programs, "cupidbuild", ["assemble-cupidasm-object", "--seed-manifest",
                           local_seed / "manifest.json", "--root", directory, "--source", assembly,
                           "--output", guarded], directory)
                outputs = {item: (directory / item).read_bytes()
                           for item in (compiled, assembled, wrapped, linked, guarded)}
                self.assertEqual(outputs[assembled], outputs[guarded])
                self.assertEqual(hashlib.sha256(outputs[compiled]).hexdigest(),
                                 "2609bce457b2ab026ea78c6df1b84b4b2bfd1b48f38767eff6b5edb0d0fb0186")
                self.assertEqual(hashlib.sha256(outputs[assembled]).hexdigest(),
                                 "6aa552c7fbc6d9cf3d8e141311b05cc4f2bffc31fb65ca009b08320bbc32a105")
                if index == 0:
                    expected_artifacts = outputs
                else:
                    self.assertEqual(outputs, expected_artifacts)
                (directory / assembly).write_bytes(b"invalid instruction\n")
                self.invoke(programs, "cupidasm", ["-f", "elf32", "-o", assembled, assembly], directory, 1)
                self.assertEqual((directory / assembled).read_bytes(), outputs[assembled])
                self.assertEqual({path.name for path in directory.iterdir()},
                                 {"tools", "seed", source, assembly, payload, *outputs})
                self.assertEqual(before, {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                                         for path in local_seed.iterdir() if path.is_file()})
        self.assertEqual(before, {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                                 for path in seed.iterdir() if path.is_file()})


if __name__ == "__main__":
    unittest.main()
