## @file
#  @brief Exercise recovery and atomic publication of the CupidASM raw output pair.
import ctypes
import os
import stat
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def _raw_publication_record(*targets, version=1):
    fields = []
    for target in targets:
        stem = target.resolve().as_posix().encode("utf-8")
        fields.extend(
            (stem + b".cupid-as-old", stem + b".cupid-as-done")
        )
    record = bytearray(b"CUPIDAS" + bytes([version]) + struct.pack("<I", len(targets)))
    for field in fields:
        record.extend(struct.pack("<I", len(field)))
        record.extend(field)
        record.append(0)
    return bytes(record)


class RawPublicationTests:
    def test_cli_keeps_existing_raw_pair_when_map_candidate_write_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "replacement.asm"
            output = root / "replacement.bin"
            range_map = root / "replacement.cupidmap"
            map_candidate = Path(str(range_map) + ".cupid-as-new")
            source.write_text("BITS 32\n    ret\n", encoding="utf-8")
            output.write_bytes(b"prior image")
            range_map.write_bytes(b"prior map")
            map_candidate.mkdir()
            range_map.chmod(stat.S_IREAD)

            try:
                result = subprocess.run(
                    [
                        str(self.cli_path),
                        "-f",
                        "bin",
                        "--map",
                        str(range_map),
                        str(source),
                        "-o",
                        str(output),
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                )

                self.assertEqual(result.returncode, 1)
                self.assertEqual(output.read_bytes(), b"prior image")
                self.assertEqual(range_map.read_bytes(), b"prior map")
            finally:
                range_map.chmod(stat.S_IREAD | stat.S_IWRITE)

    def test_cli_restores_uncommitted_raw_pair_before_later_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "recovery.asm"
            output = root / "recovery.bin"
            range_map = root / "recovery.cupidmap"
            output_backup = Path(str(output) + ".cupid-as-old")
            map_backup = Path(str(range_map) + ".cupid-as-old")
            map_candidate = Path(str(range_map) + ".cupid-as-new")
            source.write_text("BITS 32\n    ret\n", encoding="utf-8")
            output.write_bytes(b"partial image")
            range_map.write_bytes(b"partial map")
            output_backup.write_bytes(b"prior image")
            map_backup.write_bytes(b"prior map")
            map_candidate.mkdir()

            result = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "bin",
                    "--map",
                    str(range_map),
                    str(source),
                    "-o",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertEqual(output.read_bytes(), b"prior image")
            self.assertEqual(range_map.read_bytes(), b"prior map")
            self.assertFalse(output_backup.exists())
            self.assertFalse(map_backup.exists())

    @unittest.skipUnless(os.name == "nt", "requires Windows file sharing")
    def test_cli_failed_backup_replace_preserves_public_target(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "failed-recovery.asm"
            output = root / "failed-recovery.bin"
            range_map = root / "failed-recovery.cupidmap"
            output_backup = Path(str(output) + ".cupid-as-old")
            source.write_text("BITS 32\n    ret\n", encoding="utf-8")
            output.write_bytes(b"interrupted public image")
            range_map.write_bytes(b"public map")
            output_backup.write_bytes(b"prior image")

            kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel32.CreateFileW.argtypes = [
                ctypes.c_wchar_p,
                ctypes.c_uint32,
                ctypes.c_uint32,
                ctypes.c_void_p,
                ctypes.c_uint32,
                ctypes.c_uint32,
                ctypes.c_void_p,
            ]
            kernel32.CreateFileW.restype = ctypes.c_void_p
            kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
            kernel32.CloseHandle.restype = ctypes.c_int
            handle = kernel32.CreateFileW(
                str(output_backup),
                0x80000000,
                0x00000001 | 0x00000002,
                None,
                3,
                0x00000080,
                None,
            )
            self.assertNotEqual(handle, ctypes.c_void_p(-1).value)
            try:
                result = subprocess.run(
                    [
                        str(self.cli_path),
                        "-f",
                        "bin",
                        "--map",
                        str(range_map),
                        str(source),
                        "-o",
                        str(output),
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                )
            finally:
                kernel32.CloseHandle(handle)

            self.assertEqual(result.returncode, 1)
            self.assertEqual(output.read_bytes(), b"interrupted public image")
            self.assertEqual(output_backup.read_bytes(), b"prior image")

    def test_cli_removes_uncommitted_first_raw_pair_before_later_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "absent-recovery.asm"
            output = root / "absent-recovery.bin"
            range_map = root / "absent-recovery.cupidmap"
            output_absent = Path(str(output) + ".cupid-as-absent")
            map_absent = Path(str(range_map) + ".cupid-as-absent")
            map_candidate = Path(str(range_map) + ".cupid-as-new")
            source.write_text("BITS 32\n    ret\n", encoding="utf-8")
            output.write_bytes(b"interrupted new image")
            range_map.write_bytes(b"interrupted new map")
            output_absent.write_bytes(b"CUPID-AS-ABSENT\x01")
            map_absent.write_bytes(b"CUPID-AS-ABSENT\x01")
            map_candidate.mkdir()

            result = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "bin",
                    "--map",
                    str(range_map),
                    str(source),
                    "-o",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertFalse(output.exists())
            self.assertFalse(range_map.exists())
            self.assertFalse(output_absent.exists())
            self.assertFalse(map_absent.exists())

    def test_cli_recovers_mixed_existing_and_absent_raw_pair_state(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "mixed-recovery.asm"
            output = root / "mixed-recovery.bin"
            range_map = root / "mixed-recovery.cupidmap"
            output_backup = Path(str(output) + ".cupid-as-old")
            map_absent = Path(str(range_map) + ".cupid-as-absent")
            map_candidate = Path(str(range_map) + ".cupid-as-new")
            source.write_text("BITS 32\n    ret\n", encoding="utf-8")
            output.write_bytes(b"interrupted new image")
            range_map.write_bytes(b"interrupted new map")
            output_backup.write_bytes(b"prior image")
            map_absent.write_bytes(b"CUPID-AS-ABSENT\x01")
            map_candidate.mkdir()

            result = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "bin",
                    "--map",
                    str(range_map),
                    str(source),
                    "-o",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertEqual(output.read_bytes(), b"prior image")
            self.assertFalse(range_map.exists())
            self.assertFalse(output_backup.exists())
            self.assertFalse(map_absent.exists())

    def test_cli_rejects_malformed_raw_pair_recovery_without_mutation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "malformed-recovery.asm"
            output = root / "malformed-recovery.bin"
            range_map = root / "malformed-recovery.cupidmap"
            output_backup = Path(str(output) + ".cupid-as-old")
            map_backup = Path(str(range_map) + ".cupid-as-old")
            output_commit = Path(str(output) + ".cupid-as-done")
            source.write_text("BITS 32\n    ret\n", encoding="utf-8")
            output.write_bytes(b"public image")
            range_map.write_bytes(b"public map")
            output_backup.write_bytes(b"backup image")
            map_backup.write_bytes(b"backup map")
            output_commit.write_bytes(b"CUPIDAS\x01\x02\x00")

            result = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "bin",
                    "--map",
                    str(range_map),
                    str(source),
                    "-o",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertEqual(output.read_bytes(), b"public image")
            self.assertEqual(range_map.read_bytes(), b"public map")
            self.assertEqual(output_backup.read_bytes(), b"backup image")
            self.assertEqual(map_backup.read_bytes(), b"backup map")
            self.assertEqual(output_commit.read_bytes(), b"CUPIDAS\x01\x02\x00")

    def test_cli_rejects_recovery_record_naming_unrelated_public_target(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "hostile-recovery.asm"
            output = root / "hostile-recovery.bin"
            range_map = root / "hostile-recovery.cupidmap"
            unrelated = root / "unrelated.bin"
            output_commit = Path(str(output) + ".cupid-as-done")
            source.write_text("BITS 32\n    ret\n", encoding="utf-8")
            output.write_bytes(b"public image")
            range_map.write_bytes(b"public map")
            unrelated.write_bytes(b"unrelated public bytes")
            hostile_backup = unrelated.resolve().as_posix().encode("utf-8")
            marker = output_commit.resolve().as_posix().encode("utf-8")
            record = bytearray(b"CUPIDAS\x01" + struct.pack("<I", 1))
            for field in (hostile_backup, marker):
                record.extend(struct.pack("<I", len(field)))
                record.extend(field)
                record.append(0)
            output_commit.write_bytes(record)

            result = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "bin",
                    "--map",
                    str(range_map),
                    str(source),
                    "-o",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertEqual(output.read_bytes(), b"public image")
            self.assertEqual(range_map.read_bytes(), b"public map")
            self.assertEqual(unrelated.read_bytes(), b"unrelated public bytes")
            self.assertEqual(output_commit.read_bytes(), bytes(record))

    def test_cli_recovery_preserves_an_unrelated_private_pair(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "bounded-recovery.asm"
            output = root / "bounded-recovery.bin"
            range_map = root / "bounded-recovery.cupidmap"
            unrelated = root / "unrelated.bin"
            output_commit = Path(str(output) + ".cupid-as-done")
            unrelated_backup = Path(str(unrelated) + ".cupid-as-old")
            unrelated_commit = Path(str(unrelated) + ".cupid-as-done")
            source.write_text("BITS 32\n    ret\n", encoding="utf-8")
            output.write_bytes(b"committed image")
            range_map.write_bytes(b"committed map")
            unrelated_backup.write_bytes(b"unrelated backup")
            unrelated_commit.write_bytes(b"unrelated commit")
            output_commit.write_bytes(_raw_publication_record(output, unrelated))

            result = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "bin",
                    "--map",
                    str(range_map),
                    str(source),
                    "-o",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(unrelated_backup.read_bytes(), b"unrelated backup")
            self.assertEqual(unrelated_commit.read_bytes(), b"unrelated commit")

    def test_cli_rejects_malformed_absence_tombstone_without_mutation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "malformed-absence.asm"
            output = root / "malformed-absence.bin"
            range_map = root / "malformed-absence.cupidmap"
            output_absent = Path(str(output) + ".cupid-as-absent")
            source.write_text("BITS 32\n    ret\n", encoding="utf-8")
            output.write_bytes(b"public image")
            range_map.write_bytes(b"public map")
            output_absent.write_bytes(b"not an absence tombstone")

            result = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "bin",
                    "--map",
                    str(range_map),
                    str(source),
                    "-o",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertEqual(output.read_bytes(), b"public image")
            self.assertEqual(range_map.read_bytes(), b"public map")
            self.assertEqual(
                output_absent.read_bytes(), b"not an absence tombstone"
            )

    def test_cli_keeps_committed_pair_while_finishing_stale_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "committed-recovery.asm"
            output = root / "committed-recovery.bin"
            range_map = root / "committed-recovery.cupidmap"
            output_backup = Path(str(output) + ".cupid-as-old")
            map_absent = Path(str(range_map) + ".cupid-as-absent")
            output_commit = Path(str(output) + ".cupid-as-done")
            map_commit = Path(str(range_map) + ".cupid-as-done")
            map_candidate = Path(str(range_map) + ".cupid-as-new")
            source.write_text("BITS 32\n    ret\n", encoding="utf-8")
            output.write_bytes(b"committed image")
            range_map.write_bytes(b"committed map")
            output_backup.write_bytes(b"prior image")
            map_absent.write_bytes(b"CUPID-AS-ABSENT\x01")
            record = _raw_publication_record(output, range_map)
            output_commit.write_bytes(record)
            map_commit.write_bytes(record)
            map_candidate.mkdir()

            result = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "bin",
                    "--map",
                    str(range_map),
                    str(source),
                    "-o",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertEqual(output.read_bytes(), b"committed image")
            self.assertEqual(range_map.read_bytes(), b"committed map")
            self.assertFalse(output_backup.exists())
            self.assertFalse(map_absent.exists())
            self.assertFalse(output_commit.exists())
            self.assertFalse(map_commit.exists())

    def test_cli_keeps_committed_pair_when_only_one_commit_marker_survives(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "one-marker-recovery.asm"
            output = root / "one-marker-recovery.bin"
            range_map = root / "one-marker-recovery.cupidmap"
            output_backup = Path(str(output) + ".cupid-as-old")
            map_backup = Path(str(range_map) + ".cupid-as-old")
            output_commit = Path(str(output) + ".cupid-as-done")
            map_commit = Path(str(range_map) + ".cupid-as-done")
            map_candidate = Path(str(range_map) + ".cupid-as-new")
            source.write_text("BITS 32\n    ret\n", encoding="utf-8")
            output.write_bytes(b"committed image")
            range_map.write_bytes(b"committed map")
            output_backup.write_bytes(b"prior image")
            map_backup.write_bytes(b"prior map")
            output_commit.write_bytes(_raw_publication_record(output, range_map))
            map_candidate.mkdir()

            result = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "bin",
                    "--map",
                    str(range_map),
                    str(source),
                    "-o",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertEqual(output.read_bytes(), b"committed image")
            self.assertEqual(range_map.read_bytes(), b"committed map")
            self.assertFalse(output_backup.exists())
            self.assertFalse(map_backup.exists())
            self.assertFalse(output_commit.exists())
            self.assertFalse(map_commit.exists())

    @unittest.skipUnless(os.name == "nt", "requires Windows file sharing")
    def test_cli_retries_one_marker_pair_cleanup_without_rolling_back(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "one-marker-retry.asm"
            output = root / "one-marker-retry.bin"
            range_map = root / "one-marker-retry.cupidmap"
            output_backup = Path(str(output) + ".cupid-as-old")
            map_backup = Path(str(range_map) + ".cupid-as-old")
            output_commit = Path(str(output) + ".cupid-as-done")
            map_commit = Path(str(range_map) + ".cupid-as-done")
            map_candidate = Path(str(range_map) + ".cupid-as-new")
            source.write_text("BITS 32\n    ret\n", encoding="utf-8")
            output.write_bytes(b"committed image")
            range_map.write_bytes(b"committed map")
            output_backup.write_bytes(b"prior image")
            map_backup.write_bytes(b"prior map")
            output_commit.write_bytes(_raw_publication_record(output, range_map))
            map_candidate.mkdir()

            kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel32.CreateFileW.argtypes = [
                ctypes.c_wchar_p,
                ctypes.c_uint32,
                ctypes.c_uint32,
                ctypes.c_void_p,
                ctypes.c_uint32,
                ctypes.c_uint32,
                ctypes.c_void_p,
            ]
            kernel32.CreateFileW.restype = ctypes.c_void_p
            kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
            kernel32.CloseHandle.restype = ctypes.c_int
            handle = kernel32.CreateFileW(
                str(map_backup),
                0x80000000,
                0x00000001 | 0x00000002,
                None,
                3,
                0x00000080,
                None,
            )
            self.assertNotEqual(handle, ctypes.c_void_p(-1).value)
            try:
                blocked = subprocess.run(
                    [
                        str(self.cli_path),
                        "-f",
                        "bin",
                        "--map",
                        str(range_map),
                        str(source),
                        "-o",
                        str(output),
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                )
            finally:
                kernel32.CloseHandle(handle)

            self.assertEqual(blocked.returncode, 1)
            self.assertEqual(output.read_bytes(), b"committed image")
            self.assertEqual(range_map.read_bytes(), b"committed map")
            self.assertTrue(output_commit.exists())
            self.assertFalse(map_commit.exists())

            retried = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "bin",
                    "--map",
                    str(range_map),
                    str(source),
                    "-o",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(retried.returncode, 1)
            self.assertEqual(output.read_bytes(), b"committed image")
            self.assertEqual(range_map.read_bytes(), b"committed map")
            self.assertFalse(output_backup.exists())
            self.assertFalse(map_backup.exists())
            self.assertFalse(output_commit.exists())
            self.assertFalse(map_commit.exists())

    def test_cli_second_marker_failure_keeps_committed_pair_for_retry(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "marker-two-failure.asm"
            output = root / "marker-two-failure.bin"
            range_map = root / "marker-two-failure.cupidmap"
            output_backup = Path(str(output) + ".cupid-as-old")
            map_backup = Path(str(range_map) + ".cupid-as-old")
            output_commit = Path(str(output) + ".cupid-as-done")
            map_commit = Path(str(range_map) + ".cupid-as-done")
            source.write_text("BITS 32\n    ret\n", encoding="utf-8")
            output.write_bytes(b"prior image")
            range_map.write_bytes(b"prior map")
            environment = os.environ.copy()
            environment.update(
                {
                    "CUPIDASM_TEST_FAIL_COMMIT_INDEX": "1",
                    "CUPIDASM_TEST_FAIL_BACKUP_CLEANUP_INDEX": "1",
                }
            )

            blocked = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "bin",
                    "--map",
                    str(range_map),
                    str(source),
                    "-o",
                    str(output),
                ],
                cwd=REPO_ROOT,
                env=environment,
                text=True,
                capture_output=True,
            )

            self.assertEqual(blocked.returncode, 1)
            self.assertEqual(output.read_bytes(), b"\xc3")
            self.assertNotEqual(range_map.read_bytes(), b"prior map")
            self.assertFalse(output_backup.exists())
            self.assertEqual(map_backup.read_bytes(), b"prior map")
            self.assertTrue(output_commit.exists())
            self.assertTrue(map_commit.exists())

            markerless_member = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "bin",
                    str(source),
                    "-o",
                    str(range_map),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(markerless_member.returncode, 1)
            self.assertNotEqual(range_map.read_bytes(), b"prior map")
            self.assertFalse(map_backup.exists())
            self.assertTrue(output_commit.exists())
            self.assertTrue(map_commit.exists())

            retried = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "bin",
                    "--map",
                    str(range_map),
                    str(source),
                    "-o",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(retried.returncode, 0, retried.stderr)
            self.assertEqual(output.read_bytes(), b"\xc3")
            self.assertFalse(output_backup.exists())
            self.assertFalse(map_backup.exists())
            self.assertFalse(output_commit.exists())
            self.assertFalse(map_commit.exists())

    def test_cli_pending_record_does_not_accept_legacy_peer_as_commit(self):
        for output_version, map_version in ((2, 1), (1, 2)):
            with self.subTest(
                output_version=output_version, map_version=map_version
            ):
                with tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    source = root / "legacy-peer.asm"
                    output = root / "legacy-peer.bin"
                    range_map = root / "legacy-peer.cupidmap"
                    output_backup = Path(str(output) + ".cupid-as-old")
                    map_backup = Path(str(range_map) + ".cupid-as-old")
                    output_commit = Path(str(output) + ".cupid-as-done")
                    map_commit = Path(str(range_map) + ".cupid-as-done")
                    map_candidate = Path(str(range_map) + ".cupid-as-new")
                    source.write_text("BITS 32\n    ret\n", encoding="utf-8")
                    output.write_bytes(b"interrupted image")
                    range_map.write_bytes(b"interrupted map")
                    output_backup.write_bytes(b"prior image")
                    map_backup.write_bytes(b"prior map")
                    output_commit.write_bytes(
                        _raw_publication_record(
                            output, range_map, version=output_version
                        )
                    )
                    map_commit.write_bytes(
                        _raw_publication_record(
                            output, range_map, version=map_version
                        )
                    )
                    map_candidate.mkdir()

                    result = subprocess.run(
                        [
                            str(self.cli_path),
                            "-f",
                            "bin",
                            "--map",
                            str(range_map),
                            str(source),
                            "-o",
                            str(output),
                        ],
                        cwd=REPO_ROOT,
                        text=True,
                        capture_output=True,
                    )

                    self.assertEqual(result.returncode, 1)
                    self.assertEqual(output.read_bytes(), b"prior image")
                    self.assertEqual(range_map.read_bytes(), b"prior map")
                    self.assertFalse(output_backup.exists())
                    self.assertFalse(map_backup.exists())
                    self.assertFalse(output_commit.exists())
                    self.assertFalse(map_commit.exists())

    def test_cli_retains_exact_witness_when_malformed_marker_removal_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "malformed-marker-removal.asm"
            output = root / "malformed-marker-removal.bin"
            range_map = root / "malformed-marker-removal.cupidmap"
            output_backup = Path(str(output) + ".cupid-as-old")
            map_backup = Path(str(range_map) + ".cupid-as-old")
            output_commit = Path(str(output) + ".cupid-as-done")
            map_commit = Path(str(range_map) + ".cupid-as-done")
            source.write_text("BITS 32\n    ret\n", encoding="utf-8")
            output.write_bytes(b"prior image")
            range_map.write_bytes(b"prior map")
            environment = os.environ.copy()
            environment.update(
                {
                    "CUPIDASM_TEST_CORRUPT_COMMIT_INDEX": "1",
                    "CUPIDASM_TEST_FAIL_COMMIT_REMOVE_INDEX": "1",
                }
            )

            blocked = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "bin",
                    "--map",
                    str(range_map),
                    str(source),
                    "-o",
                    str(output),
                ],
                cwd=REPO_ROOT,
                env=environment,
                text=True,
                capture_output=True,
            )

            self.assertEqual(blocked.returncode, 1)
            self.assertEqual(output.read_bytes(), b"\xc3")
            self.assertNotEqual(range_map.read_bytes(), b"prior map")
            self.assertFalse(output_backup.exists())
            self.assertFalse(map_backup.exists())
            self.assertTrue(output_commit.exists())
            self.assertEqual(map_commit.read_bytes(), b"CUPI")

            retried = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "bin",
                    "--map",
                    str(range_map),
                    str(source),
                    "-o",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(retried.returncode, 0, retried.stderr)
            self.assertEqual(output.read_bytes(), b"\xc3")
            self.assertFalse(output_commit.exists())
            self.assertFalse(map_commit.exists())

    def test_cli_failed_live_backup_restore_preserves_published_target(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "live-rollback-failure.asm"
            output = root / "live-rollback-failure.bin"
            range_map = root / "live-rollback-failure.cupidmap"
            output_backup = Path(str(output) + ".cupid-as-old")
            map_backup = Path(str(range_map) + ".cupid-as-old")
            output_commit = Path(str(output) + ".cupid-as-done")
            map_commit = Path(str(range_map) + ".cupid-as-done")
            source.write_text("BITS 32\n    ret\n", encoding="utf-8")
            output.write_bytes(b"prior image")
            range_map.write_bytes(b"prior map")
            environment = os.environ.copy()
            environment.update(
                {
                    "CUPIDASM_TEST_FAIL_PUBLISH_INDEX": "1",
                    "CUPIDASM_TEST_FAIL_ROLLBACK_RESTORE_INDEX": "0",
                }
            )

            result = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "bin",
                    "--map",
                    str(range_map),
                    str(source),
                    "-o",
                    str(output),
                ],
                cwd=REPO_ROOT,
                env=environment,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertEqual(output.read_bytes(), b"\xc3")
            self.assertEqual(output_backup.read_bytes(), b"prior image")
            self.assertEqual(range_map.read_bytes(), b"prior map")
            self.assertFalse(map_backup.exists())
            self.assertFalse(output_commit.exists())
            self.assertFalse(map_commit.exists())
