"""Run the same bounded disk-template caller with native or supplied Cupid code.

CUPIDOBJ_DISK_STREAM_PROGRAM supplies an absolute executable and disables native
compilation. CUPIDOBJ_DISK_STREAM_PRODUCTS supplies a fresh absolute directory
outside the checkout. Products and command receipts are retained on every exit.
"""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = ROOT / "toolchain"


def _products():
    selected = os.environ.get("CUPIDOBJ_DISK_STREAM_PRODUCTS")
    if selected is None:
        parent = Path(tempfile.gettempdir()).resolve()
        if parent == ROOT or ROOT in parent.parents:
            raise ValueError("stream contract products must be outside the checkout")
        return Path(tempfile.mkdtemp(prefix="cupidobj-disk-stream-"))
    path = Path(selected)
    if not selected or not path.is_absolute():
        raise ValueError("CUPIDOBJ_DISK_STREAM_PRODUCTS must be absolute")
    path = path.resolve()
    if path == ROOT or ROOT in path.parents:
        raise ValueError("stream contract products must be outside the checkout")
    path.mkdir()
    return path


def _run(products, label, command, timeout):
    receipt = {"argv": [str(arg) for arg in command], "cwd": str(ROOT)}
    try:
        completed = subprocess.run(
            command, cwd=ROOT, capture_output=True, timeout=timeout, check=False
        )
        receipt.update(
            returncode=completed.returncode,
            stdout_hex=completed.stdout.hex(),
            stderr_hex=completed.stderr.hex(),
            error=None,
        )
    except subprocess.TimeoutExpired as error:
        receipt.update(
            returncode=None,
            stdout_hex=(error.stdout or b"").hex(),
            stderr_hex=(error.stderr or b"").hex(),
            error="timeout",
        )
        raise
    except OSError as error:
        receipt.update(returncode=None, error=str(error))
        raise
    finally:
        with (products / (label + ".json")).open("x", encoding="utf-8") as stream:
            json.dump(receipt, stream, sort_keys=True, indent=2)
            stream.write("\n")
    return completed


def _program(products):
    supplied = os.environ.get("CUPIDOBJ_DISK_STREAM_PROGRAM")
    if supplied is not None:
        program = Path(supplied)
        if not supplied or not program.is_absolute() or not program.is_file():
            raise ValueError("CUPIDOBJ_DISK_STREAM_PROGRAM must name an absolute file")
        if program.is_symlink():
            raise ValueError("supplied stream contract must be a regular executable")
        return program
    configured = os.environ.get("CC")
    candidates = [configured] if configured else ["clang", "gcc", "cc"]
    compiler = next((shutil.which(name) for name in candidates if shutil.which(name)), None)
    if compiler is None:
        raise AssertionError("a native compiler or supplied stream contract is required")
    output = products / ("disk-stream.exe" if os.name == "nt" else "disk-stream")
    command = [compiler]
    if Path(compiler).name.lower() in {"clang", "clang.exe"}:
        command.append("--no-default-config")
    command += [
        "-I", str(TOOLCHAIN), "-std=c11", "-O2", "-pedantic", "-Werror",
        "-Wall", "-Wextra", "-Wshadow", "-Wpointer-arith", "-Wcast-qual",
        "-Wstrict-prototypes", "-Wmissing-prototypes", "-Wconversion",
        "-Wsign-conversion", "-x", "c",
    ]
    command += [str(TOOLCHAIN / source) for source in (
        "ctool.cc", "ctool_host.cc", "elf32.cc", "cupidobj.cc",
        "tests/cupidobj_disk_stream_contract.cc",
    )]
    command += ["-o", str(output)]
    completed = _run(products, "native-build", command, 180)
    if completed.returncode != 0:
        raise AssertionError(completed.stdout.decode("utf-8", "replace") +
                             completed.stderr.decode("utf-8", "replace"))
    return output


class CupidObjDiskStreamTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.products = _products()
        cls.program = _program(cls.products)

    def run_case(self, name, expected_bytes=None):
        completed = _run(self.products, name, [str(self.program), name],
                         600 if name == "wide-u64" else 180)
        self.assertEqual(completed.returncode, 0,
                         completed.stderr.decode("utf-8", "replace") +
                         completed.stdout.decode("utf-8", "replace"))
        self.assertEqual(completed.stderr, b"")
        records = [json.loads(line) for line in completed.stdout.splitlines()]
        self.assertTrue(records)
        self.assertTrue(all(record["ok"] is True for record in records))
        self.assertEqual(records[-1]["case"], name)
        self.assertEqual(len(records), 2 if name == "admission" else 1)
        record = records[-1]
        if expected_bytes is not None:
            self.assertEqual(record["bytes"], expected_bytes)
            self.assertEqual(record["writes"], (expected_bytes + 65535) // 65536)
            self.assertEqual(record["last_count"], expected_bytes % 65536 or 65536)
            self.assertEqual(record["highest_write"],
                             ((expected_bytes - 1) // 65536) * 65536)
        return record

    def test_small_buffered_parity(self):
        self.run_case("parity-small", 38400)

    def test_active_buffered_parity(self):
        self.run_case("parity-active", 10697216)

    def test_fat_cycle(self):
        self.run_case("fat-cycle", 42496)

    def test_exact_kernel_boundary(self):
        self.run_case("exact-fit", 38400)

    def test_empty_kernel_without_reader(self):
        self.run_case("empty-kernel", 38400)

    def test_exact_chunk_boundary(self):
        self.run_case("chunk-boundary", 65536)

    def test_completed_prefix_above_64_mib(self):
        self.run_case("large-prefix", 67143168)

    def test_u64_source_and_output_ranges(self):
        record = self.run_case("wide-u64", 4295132672)
        self.assertEqual(record["kernel_bytes"], 4295095808)
        self.assertGreater(record["highest_read"], 0xffffffff)
        self.assertGreater(record["highest_write"], 0xffffffff)

    def test_semantic_rejection_before_callbacks_and_recovery(self):
        record = self.run_case("admission")
        self.assertEqual(record["rejections"], 11)

    def test_sink_failures_and_recovery(self):
        record = self.run_case("sink-failures")
        self.assertEqual(record["failures"], 3)
        self.assertEqual(record["recoveries"], 3)

    def test_source_failures_and_recovery(self):
        record = self.run_case("source-failures")
        self.assertEqual(record["failures"], 2)
        self.assertEqual(record["recoveries"], 2)


if __name__ == "__main__":
    unittest.main()
