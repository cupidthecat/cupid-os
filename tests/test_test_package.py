import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class RepositoryTestPackageTests(unittest.TestCase):
    def test_repository_tests_take_precedence_over_an_installed_tests_package(self):
        with tempfile.TemporaryDirectory(prefix="cupid-test-package-") as temporary:
            installed = Path(temporary)
            package = installed / "tests"
            package.mkdir()
            (package / "__init__.py").write_text(
                'raise RuntimeError("unrelated installed tests package loaded")\n',
                encoding="utf-8",
            )
            child = subprocess.run(
                [
                    sys.executable,
                    "-S",
                    "-c",
                    "import sys; "
                    "sys.path[:0] = sys.argv[1:3]; "
                    "import tests; "
                    "from pathlib import Path; "
                    "assert Path(tests.__file__).resolve() == "
                    "Path(sys.argv[1]) / 'tests' / '__init__.py'",
                    str(ROOT),
                    str(installed),
                ],
                cwd=installed,
                capture_output=True,
                text=True,
                timeout=30,
            )
            self.assertEqual(child.returncode, 0, child.stdout + child.stderr)


if __name__ == "__main__":
    unittest.main()
