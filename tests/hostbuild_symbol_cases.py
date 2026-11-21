## @file
#  @brief Check checked kernel symbol extraction and source publication.
import contextlib
import io
import struct
import subprocess
import tempfile
from pathlib import Path
from unittest import mock

from tests.hostbuild_fixtures import REPO_ROOT
from tools import hostbuild, hostbuild_assets


class HostBuildSymbolCases:
    def test_mksyms_runs_checked_cupiddis_then_cupidobj_from_the_seed(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            manifest.write_bytes(b"checked manifest")
            elf.write_bytes(b"pass-one ELF")
            calls = []
            symbol_text = "00001000 T first\n00002000 T second\n"
            expected = hostbuild._render_ksyms_source(
                hostbuild.build_ksyms_blob(
                    hostbuild._parse_nm_symbols(symbol_text)
                )
            )

            def run_seed(
                manifest_path,
                working_directory,
                tool_name,
                arguments,
                *,
                timeout,
            ):
                calls.append(
                    (
                        manifest_path,
                        working_directory,
                        tool_name,
                        arguments,
                        timeout,
                    )
                )
                if tool_name == "cupiddis":
                    self.assertEqual(arguments[0], "-n")
                    self.assertNotEqual(Path(arguments[1]), elf)
                    self.assertEqual(
                        Path(arguments[1]).read_bytes(),
                        elf.read_bytes(),
                    )
                    return subprocess.CompletedProcess(
                        ["cupiddis", *arguments],
                        0,
                        symbol_text,
                        "",
                    )
                self.assertEqual(tool_name, "cupidobj")
                self.assertEqual(arguments[0], "ksyms-source")
                self.assertEqual(
                    Path(arguments[1]).read_bytes(),
                    symbol_text.encode("utf-8"),
                )
                self.assertEqual(arguments[2], "-o")
                self.assertNotEqual(Path(arguments[3]), output)
                Path(arguments[3]).write_bytes(expected)
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments], 0, "", ""
                )

            with mock.patch(
                "tools.hostbuild_assets.run_seed_tool",
                side_effect=run_seed,
                create=True,
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--seed-manifest",
                        str(manifest),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 0)
            self.assertEqual(
                [call[2] for call in calls], ["cupiddis", "cupidobj"]
            )
            self.assertEqual(output.read_bytes(), expected)

    def test_mksyms_maps_checked_cupidobj_failure_and_preserves_output(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            manifest.write_bytes(b"checked manifest")
            elf.write_bytes(b"pass-one ELF")
            output.write_bytes(b"existing generated source")
            calls = []

            def run_seed(
                _manifest,
                _working_directory,
                tool_name,
                arguments,
                *,
                timeout,
            ):
                calls.append((tool_name, arguments, timeout))
                if tool_name == "cupiddis":
                    return subprocess.CompletedProcess(
                        ["cupiddis", *arguments],
                        0,
                        "00001000 T first\n",
                        "",
                    )
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments],
                    7,
                    "",
                    "output arena exhausted\n",
                )

            diagnostic = io.StringIO()
            with (
                mock.patch(
                    "tools.hostbuild_assets.run_seed_tool",
                    side_effect=run_seed,
                    create=True,
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--seed-manifest",
                        str(manifest),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertEqual(
                [call[0] for call in calls], ["cupiddis", "cupidobj"]
            )
            self.assertIn(
                "checked CupidObj failed with status 7: output arena exhausted",
                diagnostic.getvalue(),
            )
            self.assertEqual(output.read_bytes(), b"existing generated source")

    def test_mksyms_rejects_cupidobj_oracle_mismatch(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            manifest.write_bytes(b"checked manifest")
            elf.write_bytes(b"pass-one ELF")
            output.write_bytes(b"existing generated source")

            def run_seed(
                _manifest,
                _working_directory,
                tool_name,
                arguments,
                **_kwargs,
            ):
                if tool_name == "cupiddis":
                    return subprocess.CompletedProcess(
                        ["cupiddis", *arguments],
                        0,
                        "00001000 T first\n",
                        "",
                    )
                Path(arguments[3]).write_bytes(b"wrong generated source")
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments], 0, "", ""
                )

            diagnostic = io.StringIO()
            with (
                mock.patch(
                    "tools.hostbuild_assets.run_seed_tool",
                    side_effect=run_seed,
                    create=True,
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--seed-manifest",
                        str(manifest),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn(
                "checked CupidObj output differs from the Python oracle",
                diagnostic.getvalue(),
            )
            self.assertEqual(output.read_bytes(), b"existing generated source")

    def test_mksyms_rejects_missing_cupidobj_output(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            manifest.write_bytes(b"checked manifest")
            elf.write_bytes(b"pass-one ELF")
            output.write_bytes(b"existing generated source")

            def run_seed(
                _manifest,
                _working_directory,
                tool_name,
                arguments,
                **_kwargs,
            ):
                stdout = "00001000 T first\n" if tool_name == "cupiddis" else ""
                return subprocess.CompletedProcess(
                    [tool_name, *arguments], 0, stdout, ""
                )

            diagnostic = io.StringIO()
            with (
                mock.patch(
                    "tools.hostbuild_assets.run_seed_tool",
                    side_effect=run_seed,
                    create=True,
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--seed-manifest",
                        str(manifest),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn(
                "checked CupidObj did not produce a regular source file",
                diagnostic.getvalue(),
            )
            self.assertEqual(output.read_bytes(), b"existing generated source")

    def test_mksyms_maps_checked_seed_failure_and_preserves_output(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            manifest.write_bytes(b"checked manifest")
            elf.write_bytes(b"pass-one ELF")
            output.write_bytes(b"existing generated source")
            failure = subprocess.CompletedProcess(
                ["cupiddis", "-n"],
                9,
                "",
                "invalid pass-one ELF\n",
            )
            diagnostic = io.StringIO()

            with (
                mock.patch(
                    "tools.hostbuild_assets.run_seed_tool",
                    return_value=failure,
                    create=True,
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--seed-manifest",
                        str(manifest),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn(
                "checked CupidDis failed with status 9: "
                "invalid pass-one ELF",
                diagnostic.getvalue(),
            )
            self.assertEqual(
                output.read_bytes(),
                b"existing generated source",
            )

    def test_mksyms_rejects_seed_manifest_drift(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            manifest.write_bytes(b"checked manifest")
            elf.write_bytes(b"pass-one ELF")
            output.write_bytes(b"existing generated source")
            diagnostic = io.StringIO()

            def run_seed(*_args, **_kwargs):
                manifest.write_bytes(b"changed manifest")
                return subprocess.CompletedProcess(
                    ["cupiddis", "-n"],
                    0,
                    "00001000 T first\n",
                    "",
                )

            with (
                mock.patch(
                    "tools.hostbuild_assets.run_seed_tool",
                    side_effect=run_seed,
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--seed-manifest",
                        str(manifest),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn(
                "kernel symbol inputs changed while generating source",
                diagnostic.getvalue(),
            )
            self.assertEqual(
                output.read_bytes(),
                b"existing generated source",
            )

    def test_mksyms_rejects_seed_manifest_drift_after_cupidobj(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            manifest.write_bytes(b"checked manifest")
            elf.write_bytes(b"pass-one ELF")
            output.write_bytes(b"existing generated source")
            symbol_text = "00001000 T first\n"
            expected = hostbuild._render_ksyms_source(
                hostbuild.build_ksyms_blob(
                    hostbuild._parse_nm_symbols(symbol_text)
                )
            )
            calls = []

            def run_seed(
                _manifest,
                _working_directory,
                tool_name,
                arguments,
                **_kwargs,
            ):
                calls.append(tool_name)
                if tool_name == "cupiddis":
                    return subprocess.CompletedProcess(
                        ["cupiddis", *arguments],
                        0,
                        symbol_text,
                        "",
                    )
                Path(arguments[3]).write_bytes(expected)
                manifest.write_bytes(b"changed manifest")
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments], 0, "", ""
                )

            diagnostic = io.StringIO()
            with (
                mock.patch(
                    "tools.hostbuild_assets.run_seed_tool",
                    side_effect=run_seed,
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--seed-manifest",
                        str(manifest),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertEqual(calls, ["cupiddis", "cupidobj"])
            self.assertIn(
                "kernel symbol inputs changed while generating source",
                diagnostic.getvalue(),
            )
            self.assertEqual(
                output.read_bytes(),
                b"existing generated source",
            )

    def test_mksyms_uses_one_frozen_reader_and_elf_snapshot(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            reader = root / "cupiddis.exe"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            reader.write_bytes(b"checked CupidDis")
            elf.write_bytes(b"pass-one ELF")
            calls = []

            def run(command, **kwargs):
                calls.append((command, kwargs))
                self.assertNotEqual(Path(command[0]), reader)
                self.assertNotEqual(Path(command[-1]), elf)
                self.assertEqual(Path(command[0]).read_bytes(), reader.read_bytes())
                self.assertEqual(Path(command[-1]).read_bytes(), elf.read_bytes())
                return subprocess.CompletedProcess(
                    command,
                    0,
                    "00001000 T first\n00002000 T second\n",
                    "",
                )

            with mock.patch(
                "tools.hostbuild.subprocess.run",
                side_effect=run,
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--nm",
                        str(reader),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 0)
            self.assertEqual(len(calls), 1)
            source = output.read_text(encoding="utf-8")
            self.assertIn("const unsigned int", source)
            self.assertIn("const unsigned int ksym_blob_size = 45u;", source)

    def test_mksyms_keeps_same_basename_inputs_in_distinct_snapshots(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            reader = root / "reader" / "shared.exe"
            elf = root / "input" / "shared.exe"
            output = root / "ksyms_data.cc"
            reader.parent.mkdir()
            elf.parent.mkdir()
            reader.write_bytes(b"checked CupidDis")
            elf.write_bytes(b"pass-one ELF")

            def run(command, **_kwargs):
                frozen_reader = Path(command[0])
                frozen_elf = Path(command[-1])
                self.assertNotEqual(frozen_reader, frozen_elf)
                self.assertEqual(frozen_reader.read_bytes(), reader.read_bytes())
                self.assertEqual(frozen_elf.read_bytes(), elf.read_bytes())
                return subprocess.CompletedProcess(
                    command,
                    0,
                    "00001000 T first\n",
                    "",
                )

            with mock.patch(
                "tools.hostbuild.subprocess.run",
                side_effect=run,
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--nm",
                        str(reader),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 0)
            self.assertTrue(output.is_file())

    def test_mksyms_rejects_malformed_symbol_output_without_replacing_source(
        self,
    ):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            reader = root / "cupiddis.exe"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            reader.write_bytes(b"checked CupidDis")
            elf.write_bytes(b"pass-one ELF")
            output.write_bytes(b"existing generated source")
            completed = subprocess.CompletedProcess(
                [str(reader), "-n", str(elf)],
                0,
                "not-an-address T broken\n",
                "",
            )

            with mock.patch(
                "tools.hostbuild.subprocess.run",
                return_value=completed,
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--nm",
                        str(reader),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertEqual(output.read_bytes(), b"existing generated source")

    def test_mksyms_rejects_a_defined_symbol_without_an_address(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            reader = root / "cupiddis.exe"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            reader.write_bytes(b"checked CupidDis")
            elf.write_bytes(b"pass-one ELF")
            output.write_bytes(b"existing generated source")
            completed = subprocess.CompletedProcess(
                [str(reader), "-n", str(elf)],
                0,
                "T missing_address\n00001000 T valid\n",
                "",
            )

            with mock.patch(
                "tools.hostbuild.subprocess.run",
                return_value=completed,
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--nm",
                        str(reader),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertEqual(output.read_bytes(), b"existing generated source")

    def test_mksyms_rejects_an_address_outside_i386(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            reader = root / "cupiddis.exe"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            reader.write_bytes(b"checked CupidDis")
            elf.write_bytes(b"pass-one ELF")
            output.write_bytes(b"existing generated source")
            completed = subprocess.CompletedProcess(
                [str(reader), "-n", str(elf)],
                0,
                "100000000 T too_wide\n",
                "",
            )

            with mock.patch(
                "tools.hostbuild.subprocess.run",
                return_value=completed,
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--nm",
                        str(reader),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertEqual(output.read_bytes(), b"existing generated source")

    def test_mksyms_rejects_an_empty_text_symbol_set(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            reader = root / "cupiddis.exe"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            reader.write_bytes(b"checked CupidDis")
            elf.write_bytes(b"pass-one ELF")
            output.write_bytes(b"existing generated source")
            completed = subprocess.CompletedProcess(
                [str(reader), "-n", str(elf)],
                0,
                "00002000 D data_only\n",
                "",
            )

            with mock.patch(
                "tools.hostbuild.subprocess.run",
                return_value=completed,
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--nm",
                        str(reader),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertEqual(output.read_bytes(), b"existing generated source")

    def test_mksyms_maps_reader_failure_and_preserves_the_source(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            reader = root / "cupiddis.exe"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            reader.write_bytes(b"checked CupidDis")
            elf.write_bytes(b"pass-one ELF")
            output.write_bytes(b"existing generated source")
            failure = subprocess.CalledProcessError(
                7,
                [str(reader), "-n", str(elf)],
                stderr="invalid ELF",
            )
            diagnostic = io.StringIO()

            with mock.patch(
                "tools.hostbuild.subprocess.run",
                side_effect=failure,
            ), contextlib.redirect_stderr(diagnostic):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--nm",
                        str(reader),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn(
                "symbol reader failed with status 7: invalid ELF",
                diagnostic.getvalue(),
            )
            self.assertEqual(output.read_bytes(), b"existing generated source")

    def test_mksyms_rejects_live_input_drift_without_replacing_source(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            reader = root / "cupiddis.exe"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            reader.write_bytes(b"checked CupidDis")
            elf.write_bytes(b"pass-one ELF")
            output.write_bytes(b"existing generated source")

            def run(command, **_kwargs):
                elf.write_bytes(b"changed pass-one ELF")
                return subprocess.CompletedProcess(
                    command,
                    0,
                    "00001000 T first\n",
                    "",
                )

            with mock.patch(
                "tools.hostbuild.subprocess.run",
                side_effect=run,
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--nm",
                        str(reader),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertEqual(output.read_bytes(), b"existing generated source")

    def test_mksyms_rejects_reader_drift_without_replacing_source(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            reader = root / "cupiddis.exe"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            reader.write_bytes(b"checked CupidDis")
            elf.write_bytes(b"pass-one ELF")
            output.write_bytes(b"existing generated source")

            def run(command, **_kwargs):
                reader.write_bytes(b"changed CupidDis")
                return subprocess.CompletedProcess(
                    command,
                    0,
                    "00001000 T first\n",
                    "",
                )

            with mock.patch(
                "tools.hostbuild.subprocess.run",
                side_effect=run,
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--nm",
                        str(reader),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertEqual(output.read_bytes(), b"existing generated source")

    def test_symbol_reader_preserves_configured_command_arguments(self):
        reader = ("custom-nm", "--target=i386")
        elf = Path("kernel.elf")
        completed = subprocess.CompletedProcess(
            [*reader, "-n", str(elf)],
            0,
            "00001000 T first\n00002000 D ignored\n",
            "",
        )

        with mock.patch(
            "tools.hostbuild.subprocess.run", return_value=completed
        ) as run:
            symbols = hostbuild._symbols_from_nm(reader, elf)

        run.assert_called_once_with(
            [*reader, "-n", str(elf)],
            check=True,
            text=True,
            capture_output=True,
        )
        self.assertEqual(symbols, [(0x1000, "first")])

    def test_ksyms_blob_is_stable_sorted_and_deduplicated(self):
        blob = hostbuild.build_ksyms_blob(
            [
                (0x2000, "second"),
                (0x1000, "first"),
                (0x1000, "duplicate"),
                (0x3000, ".Llocal"),
            ]
        )

        magic, count, string_off, total_size = struct.unpack_from("<IIII", blob, 0)
        self.assertEqual(magic, 0x4D59534B)
        self.assertEqual(count, 2)
        self.assertEqual(total_size, len(blob))
        entries = [
            struct.unpack_from("<II", blob, 16 + i * 8)
            for i in range(count)
        ]
        self.assertEqual([addr for addr, _ in entries], [0x1000, 0x2000])
        strings = blob[string_off:]
        self.assertIn(b"first\x00", strings)
        self.assertIn(b"second\x00", strings)

    def test_ksyms_words_preserve_little_endian_bytes_and_padding(self):
        cases = (
            (b"\x01\x02\x03\x04\x05", b"\0\0\0"),
            (b"\x01\x02\x03\x04\x05\x06", b"\0\0"),
            (b"\x01\x02\x03\x04\x05\x06\x07", b"\0"),
        )

        for blob, padding in cases:
            with self.subTest(blob_size=len(blob)):
                words = hostbuild._pack_ksyms_words(blob)
                encoded = b"".join(
                    struct.pack("<I", word) for word in words
                )
                self.assertEqual(encoded[: len(blob)], blob)
                self.assertEqual(encoded[len(blob) :], padding)

    def test_ksyms_source_uses_words_and_keeps_the_exact_blob_size(self):
        symbols = [(0x1000, "first"), (0x2000, "second")]
        blob = hostbuild.build_ksyms_blob(symbols)

        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            reader = root / "cupiddis.exe"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            reader.write_bytes(b"checked CupidDis")
            elf.write_bytes(b"pass-one ELF")
            with mock.patch.object(
                hostbuild_assets,
                "_symbols_from_nm",
                return_value=symbols,
            ):
                hostbuild.write_ksyms_source(str(reader), elf, output)
            source = output.read_text(encoding="utf-8")

        self.assertIn("const unsigned int\n", source)
        self.assertNotIn("const unsigned char\n", source)
        for word in hostbuild._pack_ksyms_words(blob):
            self.assertIn(f"0x{word:08x}u,", source)
        self.assertIn(
            f"const unsigned int ksym_blob_size = {len(blob)}u;",
            source,
        )

        consumer = (
            REPO_ROOT / "kernel" / "cpu" / "ksyms.cc"
        ).read_text(encoding="utf-8")
        self.assertIn("extern const unsigned int ksym_blob[];", consumer)
        self.assertIn("const unsigned int ksym_blob[4]", consumer)
        self.assertNotIn("extern const unsigned char ksym_blob[];", consumer)
