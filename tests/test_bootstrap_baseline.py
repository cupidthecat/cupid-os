import json
import struct
import tempfile
import unittest
from pathlib import Path

from tools import bootstrap_baseline


def _write_test_elf(path, text=b"TEXT!"):
    strings = b"\0.shstrtab\0.text\0"
    section_offset = 52
    strings_offset = section_offset + 3 * 40
    text_offset = strings_offset + len(strings)
    image = bytearray(text_offset + len(text))
    image[0:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into("<I", image, 32, section_offset)
    struct.pack_into("<H", image, 46, 40)
    struct.pack_into("<H", image, 48, 3)
    struct.pack_into("<H", image, 50, 1)
    struct.pack_into(
        "<IIIIIIIIII",
        image,
        section_offset + 40,
        1,
        3,
        0,
        0,
        strings_offset,
        len(strings),
        0,
        0,
        1,
        0,
    )
    struct.pack_into(
        "<IIIIIIIIII",
        image,
        section_offset + 80,
        11,
        1,
        6,
        0,
        text_offset,
        len(text),
        0,
        0,
        16,
        0,
    )
    image[strings_offset:text_offset] = strings
    image[text_offset:] = text
    path.write_bytes(image)


class BaselineArtifactTests(unittest.TestCase):
    def test_artifact_manifest_is_sorted_and_content_addressed(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "z.bin").write_bytes(b"")
            (root / "nested").mkdir()
            (root / "nested" / "a.bin").write_bytes(b"abc")

            manifest = bootstrap_baseline.hash_artifacts(
                root,
                ["z.bin", "nested/a.bin"],
            )

            self.assertEqual(
                [entry["path"] for entry in manifest["files"]],
                ["nested/a.bin", "z.bin"],
            )
            self.assertEqual(manifest["files"][0]["size"], 3)
            self.assertEqual(
                manifest["files"][0]["sha256"],
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            )
            self.assertEqual(
                manifest["files"][1]["sha256"],
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            )
            self.assertRegex(manifest["digest"], r"^[0-9a-f]{64}$")

    def test_comparison_reports_the_artifact_that_changed(self):
        first = {
            "digest": "first",
            "files": [
                {"path": "kernel/kernel.bin", "size": 3, "sha256": "aaa"},
                {"path": "boot/boot.bin", "size": 2, "sha256": "same"},
            ],
        }
        second = {
            "digest": "second",
            "files": [
                {"path": "boot/boot.bin", "size": 2, "sha256": "same"},
                {"path": "kernel/kernel.bin", "size": 4, "sha256": "bbb"},
            ],
        }

        comparison = bootstrap_baseline.compare_artifact_manifests(first, second)

        self.assertFalse(comparison["matched"])
        self.assertEqual(
            comparison["mismatches"],
            [
                {
                    "path": "kernel/kernel.bin",
                    "first": {"size": 3, "sha256": "aaa"},
                    "second": {"size": 4, "sha256": "bbb"},
                }
            ],
        )

    def test_elf32_section_size_reads_text_without_host_objdump(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "kernel.elf"
            _write_test_elf(path)

            self.assertEqual(bootstrap_baseline.elf32_section_size(path, ".text"), 5)

    def test_artifacts_are_hashed_before_runtime_can_mutate_the_image(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "kernel").mkdir()
            _write_test_elf(root / "kernel" / "kernel.elf")
            (root / "kernel" / "kernel.bin").write_bytes(b"kernel")
            image = root / "cupidos.img"
            image.write_bytes(b"clean")
            artifacts = ["kernel/kernel.elf", "kernel/kernel.bin", "cupidos.img"]
            commands = {}

            def invoke(check):
                commands[check.name] = check.command
                if check.name == "artifact-list":
                    return bootstrap_baseline.CommandResult(0, 0.1, json.dumps(artifacts))
                if check.name in {"cupidc-gui-smoke", "cupidasm-gui-smoke"}:
                    image.write_bytes(b"mutated")
                return bootstrap_baseline.CommandResult(0, 0.1, "ok\n")

            build = bootstrap_baseline.capture_build(
                root,
                run_index=1,
                jobs=1,
                tool_commands={
                    "make": ("make",),
                    "python": ("python",),
                    "qemu": ("qemu",),
                },
                invoke=invoke,
            )

            image_entry = next(
                entry
                for entry in build["artifacts"]["files"]
                if entry["path"] == "cupidos.img"
            )
            self.assertEqual(
                image_entry["sha256"],
                "3b066804f6d1d077173cfe4d06002e6a61e6f21c2b2e648417962115f1afcd8e",
            )
            self.assertEqual(image.read_bytes(), b"mutated")
            for name in ("cupidc-gui-smoke", "cupidasm-gui-smoke"):
                command = commands[name]
                pause = command.index("--key-pause")
                self.assertEqual(command[pause + 1], "0.60")


class BaselineCheckTests(unittest.TestCase):
    def test_preflight_excludes_retired_normal_build_tools(self):
        specs = bootstrap_baseline._tool_specs()

        self.assertNotIn("linker", specs)
        self.assertNotIn("object_copy", specs)

    def test_failed_check_is_recorded_and_dependent_checks_are_skipped(self):
        calls = []

        def invoke(check):
            calls.append(check.name)
            if check.name == "build":
                return bootstrap_baseline.CommandResult(1, 2.5, "compiler failed\n")
            return bootstrap_baseline.CommandResult(0, 1.0, "ok\n")

        checks = [
            bootstrap_baseline.CheckSpec("prepare", ("make", "distclean")),
            bootstrap_baseline.CheckSpec("build", ("make", "all")),
            bootstrap_baseline.CheckSpec("runtime", ("python", "smoke.py")),
        ]

        results = bootstrap_baseline.run_check_sequence(checks, invoke)

        self.assertEqual(calls, ["prepare", "build"])
        self.assertEqual(
            [result["status"] for result in results],
            ["pass", "fail", "skipped"],
        )
        self.assertEqual(results[1]["output_tail"], ["compiler failed"])
        self.assertEqual(results[2]["reason"], "blocked by failed check: build")


if __name__ == "__main__":
    unittest.main()
