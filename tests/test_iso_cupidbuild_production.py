import hashlib
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
ISO_PATTERN_TARGET = "test_iso/fixtures/big.bin"
ISO_PATTERN_SHA256 = (
    "c8f5d0341d54d951a71b136e6e2afcb14d11ed8489a7ae126a8fee0df6ecf193"
)


class IsoCupidBuildProductionTests(unittest.TestCase):
    def setUp(self):
        if shutil.which("make") is None:
            self.skipTest("GNU Make is unavailable")

    def _fixture(self):
        temporary = tempfile.TemporaryDirectory(prefix="cupid-iso-production-")
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name).resolve()
        shutil.copy2(REPO_ROOT / "Makefile", root / "Makefile")
        source = root / "test_iso" / "big_pattern.asm"
        source.parent.mkdir()
        shutil.copy2(REPO_ROOT / "test_iso" / "big_pattern.asm", source)
        output = root / ISO_PATTERN_TARGET
        output.parent.mkdir()
        platform = "i386-windows" if os.name == "nt" else "i386-linux"
        suffix = "exe" if os.name == "nt" else "elf"
        checked = REPO_ROOT / "bootstrap" / "seeds" / platform
        selected = os.environ.get("CUPIDBUILD_TEST_SEED_MANIFEST")
        if selected:
            checked = Path(selected).resolve().parent
        seed = root / "bootstrap" / "seeds" / platform
        seed.mkdir(parents=True)
        manifest = (
            Path(selected).resolve() if selected else checked / "manifest.json"
        )
        shutil.copy2(manifest, seed / "manifest.json")
        for tool in (
            "cupidc", "cupidasm", "cupiddis", "cupidld", "cupidobj", "cupidbuild"
        ):
            shutil.copy2(checked / f"{tool}.{suffix}", seed / f"{tool}.{suffix}")
        return root, source, output, seed

    def _make(self, root):
        marker = "ISO_HOST_TOOL_MUST_NOT_RUN"
        return subprocess.run(
            (
                "make", "--always-make", ISO_PATTERN_TARGET,
                "OS=Windows_NT" if os.name == "nt" else "OS=Linux",
                f"PYTHON={marker}",
                f"CUPIDASM={marker}",
                f"CUPIDDIS={marker}",
                f"CHECKED_SEED_RUN={marker}",
            ),
            cwd=root,
            text=True,
            capture_output=True,
            timeout=60,
            check=False,
        )

    def test_real_make_publishes_exact_pattern_and_reuses_timestamp(self):
        root, _source, output, _seed = self._fixture()
        for mode in ("create", "replace", "reuse"):
            with self.subTest(mode=mode):
                if mode == "replace":
                    output.write_bytes(b"previous ISO pattern")
                stable_time = 1_700_000_000_000_000_000
                if mode != "create":
                    os.utime(output, ns=(stable_time, stable_time))
                result = self._make(root)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )
                payload = output.read_bytes()
                self.assertEqual(len(payload), 4096)
                self.assertEqual(
                    hashlib.sha256(payload).hexdigest(), ISO_PATTERN_SHA256
                )
                if mode == "reuse":
                    self.assertEqual(output.stat().st_mtime_ns, stable_time)
                self.assertEqual(
                    sorted(path.name for path in output.parent.iterdir()),
                    ["big.bin"],
                )
                self.assertEqual(list(root.glob(".cupidbuild*")), [])

    def test_real_make_preserves_output_when_pattern_is_wrong(self):
        root, source, output, _seed = self._fixture()
        source.write_text("bits 32\norg 0\ntimes 4096 db 0\n", encoding="ascii")
        output.write_bytes(b"previous ISO pattern")
        stable_time = 1_700_000_000_000_000_000
        os.utime(output, ns=(stable_time, stable_time))

        result = self._make(root)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ISO pattern differs", result.stdout + result.stderr)
        self.assertEqual(output.read_bytes(), b"previous ISO pattern")
        self.assertEqual(output.stat().st_mtime_ns, stable_time)
        self.assertEqual(
            sorted(path.name for path in output.parent.iterdir()), ["big.bin"]
        )
        self.assertEqual(list(root.glob(".cupidbuild*")), [])

    def test_real_make_rejects_drift_in_a_seed_tool_it_does_not_launch(self):
        root, _source, output, seed = self._fixture()
        suffix = "exe" if os.name == "nt" else "elf"
        compiler = seed / f"cupidc.{suffix}"
        compiler.write_bytes(compiler.read_bytes() + b"changed seed bytes")
        output.write_bytes(b"previous ISO pattern")
        stable_time = 1_700_000_000_000_000_000
        os.utime(output, ns=(stable_time, stable_time))

        result = self._make(root)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            "checked CupidC digest mismatch", result.stdout + result.stderr
        )
        self.assertEqual(output.read_bytes(), b"previous ISO pattern")
        self.assertEqual(output.stat().st_mtime_ns, stable_time)
        self.assertEqual(
            sorted(path.name for path in output.parent.iterdir()), ["big.bin"]
        )
        self.assertEqual(list(root.glob(".cupidbuild*")), [])

    def test_make_uses_typed_iso_publisher_on_both_hosts(self):
        marker = "ISO_HOST_TOOL_MUST_NOT_RUN"
        for platform, suffix in (("Windows_NT", "exe"), ("Linux", "elf")):
            with self.subTest(platform=platform):
                result = subprocess.run(
                    (
                        "make", "--dry-run", "--always-make", ISO_PATTERN_TARGET,
                        f"OS={platform}",
                        f"PYTHON={marker}",
                        f"CUPIDASM={marker}",
                        f"CUPIDASM_INPUTS={marker}",
                        f"CUPIDDIS={marker}",
                        f"CUPIDDIS_INPUTS={marker}",
                        f"CHECKED_SEED_RUN={marker}",
                        f"CHECKED_SEED_INPUTS={marker}",
                        f"PRODUCTION_SEED_DIRECTORY={marker}",
                        f"PRODUCTION_SEED_SUFFIX={marker}",
                        f"PRODUCTION_SEED_INPUTS={marker}",
                        "ISO_BIG_FIXTURE_SOURCE=Makefile",
                    ),
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=60,
                    check=False,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertNotIn(marker, result.stdout)
                self.assertIn(
                    f"cupidbuild.{suffix} assemble-iso-pattern", result.stdout
                )
                self.assertIn('--root "', result.stdout)
                self.assertIn("--source test_iso/big_pattern.asm", result.stdout)
                self.assertIn(f"--output {ISO_PATTERN_TARGET}", result.stdout)
                self.assertNotIn("tools/hostbuild.py", result.stdout)
                self.assertNotIn("tools/bootstrap_toolchain.py", result.stdout)


if __name__ == "__main__":
    unittest.main()
