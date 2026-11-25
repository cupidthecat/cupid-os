"""Run the real root Make graph with small, synchronized frontier recipes."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
GENERATED = ("bin_programs_gen", "docs_programs_gen", "demos_programs_gen")
PROBE = '''
from pathlib import Path
import sys
import time

root, role = Path(sys.argv[1]), sys.argv[2]
if role in ("left", "right"):
    (root / role).touch()
    peer = "right" if role == "left" else "left"
    deadline = time.monotonic() + 5
    while not (root / peer).exists():
        if time.monotonic() > deadline:
            raise SystemExit("generated prerequisites lost parallel execution")
        time.sleep(0.01)
elif role == "generated":
    time.sleep(0.2)
    (root / "generated-done").touch()
elif role == "user":
    if not (root / "generated-done").exists():
        raise SystemExit("user frontier overlapped generated frontier")
    (root / "user-done").touch()
elif role == "suite":
    if not (root / "user-done").exists():
        raise SystemExit("test suite started before its frontiers completed")
else:
    raise SystemExit("unknown probe role")
'''


class MakeFrontierOrderTests(unittest.TestCase):
    def test_parallel_test_serializes_frontiers_and_keeps_nested_jobs_parallel(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        with tempfile.TemporaryDirectory(prefix="cupid-make-frontiers-") as temporary:
            directory = Path(temporary)
            script = directory / "probe.py"
            script.write_text(PROBE, encoding="utf-8")
            command = (f'"{Path(sys.executable).as_posix()}" '
                       f'"{script.as_posix()}" "{directory.as_posix()}"')
            overlay = directory / "frontiers.mk"
            overlay.write_text(
                ".PHONY: fixture-left fixture-right\n"
                "fixture-left:\n\t" + command + " left\n"
                "fixture-right:\n\t" + command + " right\n"
                "test-generated-cupidc-frontier: fixture-left fixture-right\n\t" +
                command + " generated\n"
                "test-user-cupidc-frontier:\n\t" + command + " user\n"
                "test:\n\t" + command + " suite\n", encoding="utf-8")
            arguments = [make, "--no-print-directory", "-j4", "-f", str(ROOT / "Makefile"),
                         "-f", str(overlay), "CUPIDC_PRODUCTION_FRONTIER_INPUTS="]
            for name in GENERATED:
                for suffix in ("cc", "o"):
                    arguments.extend(("-o", f"kernel/util/{name}.{suffix}"))
            result = subprocess.run([*arguments, "test"], cwd=ROOT,
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for name in ("left", "right", "generated-done", "user-done"):
                self.assertTrue((directory / name).is_file(), name)


if __name__ == "__main__":
    unittest.main()
