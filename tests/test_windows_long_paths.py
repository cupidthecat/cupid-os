"""Real Windows long paths through native and checked Cupid file adapters."""
import json
import hashlib
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tools import artifact_size_contract as contract
from tools import bootstrap_toolchain as bootstrap

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name == "nt", "Windows extended file paths")
class WindowsLongPathTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="cupid-long-path-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.root = Path(cls.temporary.name)
        cls.source = cls.root / "source"
        linux = json.loads((ROOT / "bootstrap/seeds/i386-linux/manifest.json").read_bytes())["build_plan"]
        plan = bootstrap._windows_build_plan(linux, utf8=True, long_paths=True)
        order = plan["links"]["cupidc"]
        rows = [row for row in plan["sources"] if row["name"] in order]
        assembly = [row for row in plan["assembly_sources"] if row["name"] in order]
        inputs = [ROOT / row["path"].lstrip("/") for row in rows + assembly]
        inputs.extend((ROOT / "toolchain").glob("*.h"))
        inputs.extend(path for path in (ROOT / "toolchain/hosted").rglob("*") if path.is_file())
        for path in inputs:
            destination = cls.source / path.relative_to(ROOT)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, destination)
        cls.seed = bootstrap.freeze_seed_inputs(
            ROOT / "bootstrap/seeds/i386-windows/manifest.json", cls.root / "seed")
        cls.addClassCleanup(bootstrap.require_live_seed_inputs, cls.seed)
        runner = bootstrap.ToolRunner(cls.source)
        def compile_row(row):
            obj = cls.source / (row["name"] + ".o")
            contract._compile_source(cls.seed, runner, cls.source,
                row["path"].lstrip("/"), obj, row["definitions"], row["gnu_extensions"], 600)
            return row["name"], obj
        with ThreadPoolExecutor(max_workers=4) as executor:
            objects = dict(executor.map(compile_row, rows))
        for row in assembly:
            obj = cls.source / (row["name"] + ".o")
            contract._run_checked_tool(cls.seed, runner, "cupidasm",
                ["-f", "elf32", cls.source / row["path"].lstrip("/"), "-o", obj],
                row["name"], 180)
            objects[row["name"]] = obj
        cls.checked = cls.source / "cupidc.exe"
        arguments = ["-m", "i386pe", "--text-address", "0x00401000", "--entry", "_start"]
        imports = bootstrap._windows_utf8_imports("cupidc", long_paths=True)
        for library, names in imports:
            for name in names:
                arguments.extend(["--import", "__imp_" + name + "=" + library + ":" + name])
        arguments.extend(["-o", cls.checked, *[objects[name] for name in order]])
        contract._run_checked_tool(cls.seed, runner, "cupidld", arguments, "long-path CupidC", 180)
        bootstrap._validate_static_i386_pe32(cls.checked, 0x00401000, imports)
        try:
            bootstrap._validate_static_i386_pe32(cls.checked, 0x00401000,
                                                bootstrap._windows_utf8_imports("cupidc"))
        except bootstrap.BootstrapError:
            pass
        else:
            raise AssertionError("long-path CupidC accepted the preceding import profile")
        cls.native_dir = cls.root / "native"
        cls.native_dir.mkdir()
        native = cls.native_dir / "cupidc.exe"
        result = subprocess.run(["make", "-C", str(ROOT / "toolchain"), "-j4",
            "CC=clang", "BUILD_DIR=" + cls.native_dir.as_posix(),
            "HOST_REPRO_LDFLAGS=-fuse-ld=lld -Wl,/Brepro", native.as_posix()],
            capture_output=True, timeout=240)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        cls.programs = (native, cls.checked)
        evidence = os.environ.get("CUPID_WINDOWS_LONG_PATH_EVIDENCE")
        if evidence:
            destination = Path(evidence).resolve()
            destination.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(native, destination / "native-cupidc.exe")
            shutil.copyfile(cls.checked, destination / "checked-cupidc.exe")
            report = {
                "scope": "Focused long-path CupidC fixture, not staged self-bootstrap or promotion",
                "windows_plan_sha256": bootstrap._build_plan_sha256(plan),
                "imports": imports,
                "checked_inputs": {path.relative_to(cls.source).as_posix():
                    hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in cls.source.rglob("*") if path.is_file() and
                    path.suffix in (".cc", ".h", ".asm")},
                "programs": {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                             for path in destination.glob("*-cupidc.exe")},
            }
            (destination / "fixture.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    def setUp(self):
        self.case = Path(tempfile.mkdtemp(prefix="case-", dir=self.root))
        self.addCleanup(shutil.rmtree, self.case)
        self.short_source = self.case / "probe.cc"
        self.short_source.write_bytes(b"int value(void) { return 42; }\n")
        self.long = self.case / ("a" * 100) / ("b" * 100) / ("caf\u00e9 \u6771\u4eac \U0001f63a" + "c" * 50)
        self.long.mkdir(parents=True)
        self.assertGreater(len(str(self.long)), 300)
        (self.long / "probe.cc").write_bytes(self.short_source.read_bytes())

    def compile(self, program, source, output, *, expected=0):
        result = subprocess.run([str(program), "--root", str(self.case), "-c", source,
                                  "-o", output, "--freestanding"], cwd=self.case,
                                 capture_output=True, timeout=60)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def test_real_compiler_long_source_and_output_match_short_output(self):
        for index, program in enumerate(self.programs):
            with self.subTest(program=program.name, cohort=index):
                reference = self.case / (str(index) + ".o")
                self.compile(program, "/probe.cc", "/" + reference.name)
                output = self.long / (str(index) + ".o")
                source = "/" + (self.long / "probe.cc").relative_to(self.case).as_posix()
                logical = "/" + output.relative_to(self.case).as_posix()
                self.compile(program, source, logical)
                self.assertEqual(output.read_bytes(), reference.read_bytes())

    def test_long_lexical_alias_and_parent_components(self):
        for index, program in enumerate(self.programs):
            output = self.long / (str(index) + ".o")
            logical = "/" + output.relative_to(self.case).as_posix()
            alias = logical.rsplit("/", 1)[0] + "/./../" + self.long.name + "/" + output.name
            self.compile(program, "/probe.cc", alias)
            self.assertTrue(output.is_file())

    def test_long_repository_root_and_relative_root(self):
        for index, program in enumerate(self.programs):
            with self.subTest(cohort=index):
                output = self.long / ("root-" + str(index) + ".o")
                result = subprocess.run([str(program), "--root", str(self.long),
                    "-c", "/probe.cc", "-o", "/" + output.name, "--freestanding"],
                    cwd=self.case, capture_output=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertTrue(output.is_file())
                relative = self.long.relative_to(self.case).as_posix() + "/relative.o"
                result = subprocess.run([str(program), "--root", ".", "-c", "/probe.cc",
                    "-o", "/" + relative, "--freestanding"],
                    cwd=self.case, capture_output=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual((self.case / relative).read_bytes(), output.read_bytes())
                relocated = self.long / ("compiler-" + str(index) + ".exe")
                shutil.copyfile(program, relocated)
                result = subprocess.run([str(relocated), "--root", str(self.long), "-c", "/probe.cc",
                    "-o", "/relocated.o", "--freestanding"], executable=str(relocated), cwd=self.case,
                    capture_output=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual((self.long / "relocated.o").read_bytes(), output.read_bytes())

    def test_resolved_long_device_output_keeps_windows_semantics(self):
        logical = "/" + self.long.relative_to(self.case).as_posix() + "/NUL"
        for program in self.programs:
            self.compile(program, "/probe.cc", logical)
        self.assertFalse(any(path.name.upper() == "NUL" for path in self.long.iterdir()))

    def test_missing_long_input_preserves_previous_output(self):
        for program in self.programs:
            output = self.long / "previous.o"
            output.write_bytes(b"previous")
            stamp = output.stat().st_mtime_ns
            source = "/" + (self.long / "missing.cc").relative_to(self.case).as_posix()
            logical = "/" + output.relative_to(self.case).as_posix()
            result = self.compile(program, source, logical, expected=1)
            self.assertIn(b"cannot load", result.stderr)
            self.assertEqual(output.read_bytes(), b"previous")
            self.assertEqual(output.stat().st_mtime_ns, stamp)


if __name__ == "__main__":
    unittest.main()
