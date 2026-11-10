import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name == "nt", "native Windows adapter")
class NativeUtf8Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang")
        if compiler is None:
            raise unittest.SkipTest("Clang is required for the native adapter tests")
        cls.temporary = tempfile.TemporaryDirectory(prefix="cupid-native-utf8-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.build = Path(cls.temporary.name)
        source = ROOT / "toolchain"
        cls.programs = {}
        for name, inputs in (
            ("faults", ("path_encoding.cc", "tests/native_utf8_fault_contract.cc")),
            ("argv", ("path_encoding.cc", "native_utf8.cc", "native_utf8_entry.cc",
                      "tests/native_utf8_argv_contract.cc")),
        ):
            output = cls.build / (name + ".exe")
            command = [
                compiler, "-I" + str(source), "-std=c11", "-O2", "-pedantic",
                "-Werror", "-Wall", "-Wextra", "-Wshadow", "-Wpointer-arith",
                "-Wcast-qual", "-Wstrict-prototypes", "-Wmissing-prototypes",
                "-Wconversion", "-Wsign-conversion", "-x", "c",
                *(str(source / item) for item in inputs),
                "-fuse-ld=lld", "-Wl,/Brepro", "-o", str(output),
            ]
            built = subprocess.run(command, capture_output=True, timeout=120)
            if built.returncode != 0:
                raise AssertionError((built.stdout + built.stderr).decode(errors="replace"))
            cls.programs[name] = output

    def run_arguments(self, arguments):
        result = subprocess.run(
            [str(self.programs["argv"]), *arguments],
            capture_output=True, timeout=30,
        )
        self.assertEqual(result.returncode, 17, result.stderr)
        self.assertEqual(result.stdout.decode("utf-8").splitlines(), arguments)
        self.assertEqual(result.stderr, b"")

    def test_allocation_failures_invalid_unicode_and_cleanup(self):
        result = subprocess.run(
            [str(self.programs["faults"]), "contract"],
            capture_output=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(b"native UTF-8 fault contract: ok", result.stdout)
        self.assertIn(b"packaged native entry cleanup and argv faults: ok", result.stdout)
        self.assertEqual(result.stderr, b"")

    def test_diagnostic_name_is_an_ordinary_argument(self):
        self.run_arguments(["--private-code-page"])

    def test_unicode_arguments_and_callback_result(self):
        self.run_arguments(["caf\u00e9", "\u6771\u4eac", "\U0001f63a space"])

    def test_no_user_arguments(self):
        self.run_arguments([])

    def test_empty_argument(self):
        self.run_arguments([""])

    def test_spaces_quotes_and_backslashes(self):
        self.run_arguments(['a "quoted" path', "space at end ", "trailing\\"])


if __name__ == "__main__":
    unittest.main()
