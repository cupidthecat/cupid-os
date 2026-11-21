## @file
#  @brief Check ISO construction, path tables, and fixture publication.
import contextlib
import io
import os
import re
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tests.hostbuild_fixtures import REPO_ROOT, SEED_MANIFEST
from tools import hostbuild


ISO_BLOCK_SIZE = 2048


def _iso_both_u16(payload, offset):
    little = struct.unpack_from("<H", payload, offset)[0]
    big = struct.unpack_from(">H", payload, offset + 2)[0]
    if little != big:
        raise AssertionError(
            f"mismatched both-endian 16-bit value at {offset}"
        )
    return little


def _iso_both_u32(payload, offset):
    little = struct.unpack_from("<I", payload, offset)[0]
    big = struct.unpack_from(">I", payload, offset + 4)[0]
    if little != big:
        raise AssertionError(
            f"mismatched both-endian 32-bit value at {offset}"
        )
    return little


def _iso_susp_entries(record):
    name_length = record[32]
    offset = 33 + name_length + (1 if name_length % 2 == 0 else 0)
    entries = []
    while offset < len(record):
        trailing = record[offset:]
        if (
            1 <= len(trailing) <= 3
            and trailing == b"\x00" * len(trailing)
        ):
            break
        if offset + 4 > len(record):
            raise AssertionError("partial SUSP entry")
        length = record[offset + 2]
        if length < 4 or offset + length > len(record):
            raise AssertionError("invalid SUSP entry length")
        entry = record[offset : offset + length]
        entries.append(entry)
        offset += length
        if entry[:2] == b"ST":
            if record[offset:] not in {b"", b"\x00"}:
                raise AssertionError("data follows a SUSP terminator")
            break
    return entries


def _iso_directory_records(image, extent, size):
    start = extent * ISO_BLOCK_SIZE
    directory = image[start : start + size]
    if len(directory) != size:
        raise AssertionError("directory extent lies outside the image")
    records = []
    offset = 0
    while offset < size:
        length = directory[offset]
        if length == 0:
            offset += ISO_BLOCK_SIZE - (offset % ISO_BLOCK_SIZE)
            continue
        if offset % ISO_BLOCK_SIZE + length > ISO_BLOCK_SIZE:
            raise AssertionError("directory record crosses a block")
        record = directory[offset : offset + length]
        if len(record) != length:
            raise AssertionError("partial directory record")
        name_length = record[32]
        identifier = bytes(record[33 : 33 + name_length])
        susp = _iso_susp_entries(record)
        rock_ridge_name = None
        for entry in susp:
            if entry[:2] == b"NM" and entry[3] == 1:
                rock_ridge_name = entry[5:].decode("ascii")
        if identifier in {b"\x00", b"\x01"}:
            display_name = "." if identifier == b"\x00" else ".."
        else:
            iso_name = identifier.decode("ascii")
            display_name = rock_ridge_name
            if display_name is None:
                display_name = iso_name.split(";", 1)[0].rstrip(".")
        records.append(
            {
                "record": record,
                "identifier": identifier,
                "name": display_name,
                "extent": _iso_both_u32(record, 2),
                "size": _iso_both_u32(record, 10),
                "date": bytes(record[18:25]),
                "flags": record[25],
                "susp": susp,
            }
        )
        offset += length
    return records


def _iso_path_table(image, extent, size, byte_order):
    table = image[
        extent * ISO_BLOCK_SIZE : extent * ISO_BLOCK_SIZE + size
    ]
    entries = []
    offset = 0
    prefix = "<" if byte_order == "little" else ">"
    while offset < size:
        name_length = table[offset]
        if name_length == 0:
            raise AssertionError("empty path-table identifier")
        entry_length = 8 + name_length + (name_length % 2)
        if offset + entry_length > size:
            raise AssertionError("partial path-table entry")
        identifier = bytes(table[offset + 8 : offset + 8 + name_length])
        entries.append(
            (
                identifier,
                struct.unpack_from(f"{prefix}I", table, offset + 2)[0],
                struct.unpack_from(f"{prefix}H", table, offset + 6)[0],
            )
        )
        offset += entry_length
    return entries


def _inspect_iso(image):
    if len(image) % ISO_BLOCK_SIZE:
        raise AssertionError("ISO image is not block aligned")
    pvd = image[16 * ISO_BLOCK_SIZE : 17 * ISO_BLOCK_SIZE]
    terminator = image[17 * ISO_BLOCK_SIZE : 18 * ISO_BLOCK_SIZE]
    if pvd[:7] != b"\x01CD001\x01":
        raise AssertionError("missing primary volume descriptor")
    if terminator[:7] != b"\xffCD001\x01":
        raise AssertionError("missing volume descriptor terminator")

    volume_blocks = _iso_both_u32(pvd, 80)
    block_size = _iso_both_u16(pvd, 128)
    if block_size != ISO_BLOCK_SIZE:
        raise AssertionError("unexpected ISO logical block size")
    if volume_blocks * block_size != len(image):
        raise AssertionError("volume size does not match the image")

    path_table_size = _iso_both_u32(pvd, 132)
    little_path_extent = struct.unpack_from("<I", pvd, 140)[0]
    big_path_extent = struct.unpack_from(">I", pvd, 148)[0]
    little_paths = _iso_path_table(
        image, little_path_extent, path_table_size, "little"
    )
    big_paths = _iso_path_table(
        image, big_path_extent, path_table_size, "big"
    )
    if little_paths != big_paths:
        raise AssertionError("little- and big-endian path tables differ")

    root_record = pvd[156 : 156 + pvd[156]]
    root_extent = _iso_both_u32(root_record, 2)
    root_size = _iso_both_u32(root_record, 10)
    files = {}
    directories = {}
    all_records = []

    def walk(extent, size, prefix):
        records = _iso_directory_records(image, extent, size)
        directories[prefix or "/"] = records
        all_records.extend(records)
        for entry in records:
            if entry["identifier"] in {b"\x00", b"\x01"}:
                continue
            path = f"{prefix}/{entry['name']}" if prefix else (
                f"/{entry['name']}"
            )
            if entry["flags"] & 0x02:
                walk(entry["extent"], entry["size"], path)
                continue
            start = entry["extent"] * ISO_BLOCK_SIZE
            files[path] = image[start : start + entry["size"]]

    walk(root_extent, root_size, "")
    return {
        "pvd": pvd,
        "paths": little_paths,
        "files": files,
        "directories": directories,
        "records": all_records,
        "volume_blocks": volume_blocks,
    }


class HostBuildIsoCases:
    def _write_fixture_tree(self, root):
        root.mkdir()
        (root / "readme.txt").write_bytes(b"Cupid ISO test fixture\n")
        (root / "long_named_file.txt").write_bytes(
            b"Rock Ridge keeps this name intact.\n"
        )
        (root / "abcdefgh-one.txt").write_bytes(b"first collision\n")
        (root / "abcdefgh-two.txt").write_bytes(b"second collision\n")
        (root / "big.bin").write_bytes(
            bytes(i & 0xFF for i in range(4096))
        )
        (root / "exact.bin").write_bytes(b"E" * ISO_BLOCK_SIZE)
        (root / "empty.bin").write_bytes(b"")
        (root / "sub").mkdir()
        (root / "sub" / "nested.txt").write_bytes(
            b"Nested fixture content.\n"
        )

    def _copy_fixture_tree_in_reverse(self, source, destination):
        destination.mkdir()
        directories = sorted(
            (path for path in source.rglob("*") if path.is_dir()),
            key=lambda path: path.as_posix(),
            reverse=True,
        )
        for directory in directories:
            (destination / directory.relative_to(source)).mkdir(
                parents=True,
                exist_ok=True,
            )
        files = sorted(
            (path for path in source.rglob("*") if path.is_file()),
            key=lambda path: path.as_posix(),
            reverse=True,
        )
        for file_path in files:
            target = destination / file_path.relative_to(source)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(file_path.read_bytes())

    def _write_fixture_manifest(self, manifest, fixtures):
        entries = sorted(
            path.relative_to(fixtures).as_posix()
            for path in fixtures.rglob("*")
        )
        manifest.write_text(
            "\n".join(entries) + "\n",
            encoding="ascii",
            newline="\n",
        )

    def test_build_iso_is_deterministic_and_uses_no_external_author(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            fixtures = work / "fixtures"
            reverse_fixtures = work / "fixtures-reversed"
            first = work / "first.iso"
            second = work / "second.iso"
            self._write_fixture_tree(fixtures)
            self._copy_fixture_tree_in_reverse(
                fixtures,
                reverse_fixtures,
            )
            for index, path in enumerate(fixtures.rglob("*")):
                stamp = 1_000_000_000 + index * 100
                os.utime(path, (stamp, stamp))
            for index, path in enumerate(reverse_fixtures.rglob("*")):
                stamp = 1_700_000_000 + index * 200
                os.utime(path, (stamp, stamp))

            with (
                mock.patch(
                    "tools.hostbuild.shutil.which",
                    side_effect=AssertionError(
                        "an external ISO author was queried"
                    ),
                ),
                mock.patch(
                    "tools.hostbuild.subprocess.run",
                    side_effect=AssertionError(
                        "an external ISO author was launched"
                    ),
                ),
                contextlib.redirect_stdout(io.StringIO()),
            ):
                hostbuild.build_iso(fixtures, first)
                hostbuild.build_iso(reverse_fixtures, second)

            first_image = first.read_bytes()
            self.assertEqual(first_image, second.read_bytes())
            inspected = _inspect_iso(first_image)
            self.assertEqual(
                first_image[: 16 * ISO_BLOCK_SIZE],
                b"\x00" * (16 * ISO_BLOCK_SIZE),
            )
            self.assertLess(inspected["volume_blocks"], 190)
            self.assertEqual(
                inspected["pvd"][40:72].rstrip(b" "),
                b"CUPID_OS_TEST",
            )
            for start, length in ((40, 32), (190, 128)):
                identifier = inspected["pvd"][
                    start : start + length
                ].rstrip(b" ")
                self.assertTrue(identifier)
                self.assertIsNotNone(
                    re.fullmatch(b"[A-Z0-9_]+", identifier)
                )
            self.assertEqual(_iso_both_u16(inspected["pvd"], 120), 1)
            self.assertEqual(_iso_both_u16(inspected["pvd"], 124), 1)
            self.assertEqual(
                inspected["pvd"][813:830],
                b"2000010100000000\x00",
            )
            self.assertEqual(
                inspected["pvd"][830:847],
                b"2000010100000000\x00",
            )
            self.assertEqual(
                inspected["pvd"][847:864],
                b"0000000000000000\x00",
            )
            self.assertEqual(
                inspected["pvd"][864:881],
                b"2000010100000000\x00",
            )
            self.assertEqual(inspected["pvd"][881], 1)
            self.assertEqual(
                inspected["files"],
                {
                    "/abcdefgh-one.txt": b"first collision\n",
                    "/abcdefgh-two.txt": b"second collision\n",
                    "/big.bin": bytes(i & 0xFF for i in range(4096)),
                    "/empty.bin": b"",
                    "/exact.bin": b"E" * ISO_BLOCK_SIZE,
                    "/long_named_file.txt": (
                        b"Rock Ridge keeps this name intact.\n"
                    ),
                    "/readme.txt": b"Cupid ISO test fixture\n",
                    "/sub/nested.txt": b"Nested fixture content.\n",
                },
            )
            self.assertEqual(
                [entry[0] for entry in inspected["paths"]],
                [b"\x00", b"SUB"],
            )
            self.assertTrue(
                all(
                    record["date"] == b"\x64\x01\x01\x00\x00\x00\x00"
                    for record in inspected["records"]
                )
            )
            self.assertTrue(
                all(
                    len(record["record"]) % 2 == 0
                    for record in inspected["records"]
                )
            )

            root_records = inspected["directories"]["/"]
            root_signatures = [
                entry[:2] for entry in root_records[0]["susp"]
            ]
            self.assertEqual(
                root_signatures,
                [b"SP", b"PX", b"TF", b"CE"],
            )
            self.assertEqual(
                root_records[0]["susp"][0],
                b"SP\x07\x01\xbe\xef\x00",
            )
            for record in inspected["records"]:
                signatures = [entry[:2] for entry in record["susp"]]
                self.assertIn(b"PX", signatures)
                self.assertIn(b"TF", signatures)
                px = next(
                    entry
                    for entry in record["susp"]
                    if entry[:2] == b"PX"
                )
                self.assertEqual((len(px), px[3]), (36, 1))
                self.assertIn(_iso_both_u32(px, 4) & 0o170000, {
                    0o040000,
                    0o100000,
                })
                tf = next(
                    entry
                    for entry in record["susp"]
                    if entry[:2] == b"TF"
                )
                self.assertEqual(
                    tf,
                    b"TF\x1a\x01\x0e"
                    + b"\x64\x01\x01\x00\x00\x00\x00" * 3,
                )

            continuation = root_records[0]["susp"][-1]
            continuation_extent = _iso_both_u32(continuation, 4)
            continuation_offset = _iso_both_u32(continuation, 12)
            continuation_length = _iso_both_u32(continuation, 20)
            self.assertEqual(continuation_offset, 0)
            self.assertLessEqual(
                continuation_offset + continuation_length,
                ISO_BLOCK_SIZE,
            )
            start = continuation_extent * ISO_BLOCK_SIZE
            er = first_image[start : start + continuation_length]
            self.assertEqual((er[:2], er[2], er[3]), (
                b"ER",
                continuation_length,
                1,
            ))
            identifier_length = er[4]
            descriptor_length = er[5]
            source_length = er[6]
            self.assertEqual(er[7], 1)
            self.assertEqual(
                8
                + identifier_length
                + descriptor_length
                + source_length,
                continuation_length,
            )
            self.assertEqual(
                er[8 : 8 + identifier_length],
                b"RRIP_1991A",
            )
            identifiers = {
                record["name"]: record["identifier"]
                for record in root_records
            }
            ordinary_identifiers = [
                record["identifier"]
                for record in root_records
                if record["identifier"] not in {b"\x00", b"\x01"}
            ]
            self.assertEqual(
                ordinary_identifiers,
                sorted(ordinary_identifiers),
            )
            for record in inspected["records"]:
                identifier = record["identifier"]
                if identifier in {b"\x00", b"\x01"}:
                    continue
                if record["flags"] & 0x02:
                    grammar = b"[A-Z0-9_]{1,8}"
                else:
                    grammar = (
                        b"[A-Z0-9_]{1,8}\\."
                        b"[A-Z0-9_]{0,3};1"
                    )
                self.assertIsNotNone(
                    re.fullmatch(grammar, identifier),
                    identifier,
                )
            self.assertNotEqual(
                identifiers["abcdefgh-one.txt"],
                identifiers["abcdefgh-two.txt"],
            )
            self.assertTrue(
                identifiers["abcdefgh-one.txt"].endswith(b";1")
            )
            self.assertTrue(
                identifiers["abcdefgh-two.txt"].endswith(b";1")
            )

    def test_build_iso_orders_identifiers_and_marks_an_empty_extension(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            fixtures = work / "fixtures"
            fixtures.mkdir()
            (fixtures / "-a.txt").write_bytes(b"punctuation")
            (fixtures / "a.txt").write_bytes(b"letter")
            (fixtures / "README").write_bytes(b"no extension")
            (fixtures / "-dir").mkdir()
            (fixtures / "adir").mkdir()
            output = work / "ordered.iso"

            with contextlib.redirect_stdout(io.StringIO()):
                hostbuild.build_iso(fixtures, output)
            inspected = _inspect_iso(output.read_bytes())
            identifiers = [
                record["identifier"]
                for record in inspected["directories"]["/"]
                if record["identifier"] not in {b"\x00", b"\x01"}
            ]

            self.assertEqual(identifiers, sorted(identifiers))
            self.assertIn(b"README.;1", identifiers)
            self.assertEqual(
                [entry[0] for entry in inspected["paths"]],
                [b"\x00", b"ADIR", b"_DIR"],
            )

    def test_build_iso_places_continuation_after_directory_stream(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            fixtures = work / "fixtures"
            output = work / "forward-ce.iso"
            self._write_fixture_tree(fixtures)

            with contextlib.redirect_stdout(io.StringIO()):
                hostbuild.build_iso(fixtures, output)

            inspected = _inspect_iso(output.read_bytes())
            root_dot = inspected["directories"]["/"][0]
            continuation = next(
                entry
                for entry in root_dot["susp"]
                if entry[:2] == b"CE"
            )
            continuation_extent = _iso_both_u32(continuation, 4)
            directory_end = max(
                records[0]["extent"]
                + (
                    records[0]["size"]
                    + ISO_BLOCK_SIZE
                    - 1
                )
                // ISO_BLOCK_SIZE
                for records in inspected["directories"].values()
            )

            self.assertGreaterEqual(continuation_extent, directory_end)
            self.assertTrue(
                all(
                    entry[:2] != b"ST"
                    for records in inspected["directories"].values()
                    for record in records
                    for entry in record["susp"]
                )
            )

    def test_build_iso_manifest_is_the_exact_fixture_universe(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            fixtures = work / "fixtures"
            manifest = work / "fixtures.manifest"
            output = work / "manifest.iso"
            self._write_fixture_tree(fixtures)
            self._write_fixture_manifest(manifest, fixtures)

            with contextlib.redirect_stdout(io.StringIO()):
                hostbuild.build_iso(fixtures, output, manifest)
            original = output.read_bytes()

            (fixtures / ".hidden").write_bytes(b"not declared")
            with self.assertRaisesRegex(
                hostbuild.IsoAuthoringError,
                "not declared in the ISO fixture manifest: .hidden",
            ):
                hostbuild.build_iso(fixtures, output, manifest)
            self.assertEqual(output.read_bytes(), original)

            (fixtures / ".hidden").unlink()
            manifest.write_text(
                manifest.read_text(encoding="ascii").replace(
                    "sub/nested.txt\n",
                    "",
                ),
                encoding="ascii",
                newline="\n",
            )
            with self.assertRaisesRegex(
                hostbuild.IsoAuthoringError,
                "not declared in the ISO fixture manifest: sub/nested.txt",
            ):
                hostbuild.build_iso(fixtures, output, manifest)
            self.assertEqual(output.read_bytes(), original)

    def test_build_iso_runs_checked_cupidobj_before_the_python_oracle(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            fixtures = work / "fixtures"
            manifest = work / "fixtures.manifest"
            output = work / "checked.iso"
            self._write_fixture_tree(fixtures)
            self._write_fixture_manifest(manifest, fixtures)
            manifest_payload = manifest.read_bytes()
            expected = hostbuild._render_iso_image(
                hostbuild._snapshot_iso_tree(fixtures)
            )
            state = {"checked": False}

            def run_checked(
                manifest_path,
                working_directory,
                tool_name,
                arguments,
                timeout,
            ):
                command = [str(argument) for argument in arguments]
                self.assertEqual(Path(manifest_path), SEED_MANIFEST)
                private = Path(working_directory).resolve()
                self.assertFalse(private.is_relative_to(output.parent))
                self.assertEqual(tool_name, "cupidobj")
                self.assertEqual(timeout, 60)
                self.assertEqual(command[0], "iso-fixture")
                frozen_manifest = Path(command[1])
                self.assertTrue(frozen_manifest.is_relative_to(private))
                self.assertNotEqual(frozen_manifest, manifest)
                self.assertEqual(
                    frozen_manifest.read_bytes(), manifest_payload
                )

                entries = {}
                index = 2
                while command[index] != "-o":
                    option = command[index]
                    logical = command[index + 1]
                    if option == "--directory":
                        entries[logical] = None
                        index += 2
                    else:
                        self.assertEqual(option, "--file")
                        native = Path(command[index + 2])
                        self.assertTrue(native.is_relative_to(private))
                        self.assertEqual(native.parent, private / "inputs")
                        self.assertRegex(native.name, r"^[0-9]{4}\.bin$")
                        self.assertNotEqual(
                            native,
                            fixtures / Path(logical),
                        )
                        self.assertEqual(
                            native.read_bytes(),
                            (fixtures / Path(logical)).read_bytes(),
                        )
                        entries[logical] = native
                        index += 3
                self.assertEqual(
                    set(entries), set(manifest.read_text().splitlines())
                )
                candidate = Path(command[index + 1])
                self.assertTrue(candidate.is_relative_to(private))
                candidate.write_bytes(expected)
                state["checked"] = True
                return subprocess.CompletedProcess(
                    command, 0, "", ""
                )

            original_render = hostbuild._render_iso_image
            with (
                mock.patch(
                    "tools.hostbuild_iso.run_seed_tool",
                    side_effect=run_checked,
                ),
                mock.patch(
                    "tools.hostbuild_iso._render_iso_image",
                    side_effect=lambda snapshot: (
                        self.assertTrue(state["checked"])
                        or original_render(snapshot)
                    ),
                ),
                contextlib.redirect_stdout(io.StringIO()),
            ):
                hostbuild.build_iso(
                    fixtures,
                    output,
                    manifest,
                    seed_manifest=SEED_MANIFEST,
                )

            self.assertTrue(state["checked"])
            self.assertEqual(output.read_bytes(), expected)

    def test_build_iso_rejects_checked_failure_and_oracle_mismatch(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            fixtures = work / "fixtures"
            manifest = work / "fixtures.manifest"
            output = work / "checked.iso"
            self._write_fixture_tree(fixtures)
            self._write_fixture_manifest(manifest, fixtures)
            output.write_bytes(b"existing image")

            failure = subprocess.CompletedProcess(
                ["cupidobj", "iso-fixture"],
                7,
                "",
                "checked failure",
            )
            with (
                mock.patch(
                    "tools.hostbuild_iso.run_seed_tool",
                    return_value=failure,
                ),
                self.assertRaisesRegex(
                    hostbuild.IsoAuthoringError,
                    "checked CupidObj failed with status 7: checked failure",
                ),
            ):
                hostbuild.build_iso(
                    fixtures,
                    output,
                    manifest,
                    seed_manifest=SEED_MANIFEST,
                )
            self.assertEqual(output.read_bytes(), b"existing image")

            successful_without_output = subprocess.CompletedProcess(
                ["cupidobj", "iso-fixture"],
                0,
                "",
                "",
            )
            with (
                mock.patch(
                    "tools.hostbuild_iso.run_seed_tool",
                    return_value=successful_without_output,
                ),
                self.assertRaisesRegex(
                    hostbuild.IsoAuthoringError,
                    "reported success without an ISO image",
                ),
            ):
                hostbuild.build_iso(
                    fixtures,
                    output,
                    manifest,
                    seed_manifest=SEED_MANIFEST,
                )
            self.assertEqual(output.read_bytes(), b"existing image")

            def make_directory_candidate(
                _manifest,
                _working_directory,
                _tool_name,
                arguments,
                timeout,
            ):
                self.assertEqual(timeout, 60)
                command = [str(argument) for argument in arguments]
                Path(command[-1]).mkdir()
                return subprocess.CompletedProcess(command, 0, "", "")

            with (
                mock.patch(
                    "tools.hostbuild_iso.run_seed_tool",
                    side_effect=make_directory_candidate,
                ),
                self.assertRaisesRegex(
                    hostbuild.IsoAuthoringError,
                    r"not a regular file: .*checked\.iso",
                ),
            ):
                hostbuild.build_iso(
                    fixtures,
                    output,
                    manifest,
                    seed_manifest=SEED_MANIFEST,
                )
            self.assertEqual(output.read_bytes(), b"existing image")

            def write_wrong_candidate(
                _manifest,
                _working_directory,
                _tool_name,
                arguments,
                timeout,
            ):
                self.assertEqual(timeout, 60)
                command = [str(argument) for argument in arguments]
                Path(command[-1]).write_bytes(b"wrong image")
                return subprocess.CompletedProcess(command, 0, "", "")

            with (
                mock.patch(
                    "tools.hostbuild_iso.run_seed_tool",
                    side_effect=write_wrong_candidate,
                ),
                self.assertRaisesRegex(
                    hostbuild.IsoAuthoringError,
                    "checked CupidObj ISO fixture differs from the Python oracle",
                ),
            ):
                hostbuild.build_iso(
                    fixtures,
                    output,
                    manifest,
                    seed_manifest=SEED_MANIFEST,
                )
            self.assertEqual(output.read_bytes(), b"existing image")

    def test_build_iso_rejects_checked_seed_drift_and_output_alias(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            fixtures = work / "fixtures"
            manifest = work / "fixtures.manifest"
            output = work / "checked.iso"
            seed = work / "seed"
            self._write_fixture_tree(fixtures)
            self._write_fixture_manifest(manifest, fixtures)
            shutil.copytree(SEED_MANIFEST.parent, seed)
            seed_manifest = seed / "manifest.json"
            original_seed_manifest = seed_manifest.read_bytes()
            output.write_bytes(b"existing image")
            expected = hostbuild._render_iso_image(
                hostbuild._snapshot_iso_tree(fixtures)
            )

            def write_then_drift_seed(
                _manifest,
                _working_directory,
                _tool_name,
                arguments,
                timeout,
            ):
                self.assertEqual(timeout, 60)
                command = [str(argument) for argument in arguments]
                Path(command[-1]).write_bytes(expected)
                seed_manifest.write_bytes(original_seed_manifest + b" ")
                return subprocess.CompletedProcess(command, 0, "", "")

            with (
                mock.patch(
                    "tools.hostbuild_iso.run_seed_tool",
                    side_effect=write_then_drift_seed,
                ),
                self.assertRaisesRegex(
                    hostbuild.IsoAuthoringError,
                    "checked seed inputs changed while authoring the ISO image",
                ),
            ):
                hostbuild.build_iso(
                    fixtures,
                    output,
                    manifest,
                    seed_manifest=seed_manifest,
                )
            self.assertEqual(output.read_bytes(), b"existing image")

            seed_manifest.write_bytes(original_seed_manifest)
            with self.assertRaisesRegex(
                hostbuild.IsoAuthoringError,
                "ISO output may not replace a checked seed input",
            ):
                hostbuild.build_iso(
                    fixtures,
                    seed_manifest,
                    manifest,
                    seed_manifest=seed_manifest,
                )

            nested_output = seed / "unlisted" / "payload.elf"
            with self.assertRaisesRegex(
                hostbuild.IsoAuthoringError,
                "ISO output may not be inside the checked seed directory",
            ):
                hostbuild.build_iso(
                    fixtures,
                    nested_output,
                    manifest,
                    seed_manifest=seed_manifest,
                )
            self.assertFalse(nested_output.parent.exists())
            hostbuild.verify_seed_inputs(seed_manifest)

            tool = seed / "cupidobj.elf"
            tool_alias = work / "tool-alias.iso"
            os.link(tool, tool_alias)
            with self.assertRaisesRegex(
                hostbuild.IsoAuthoringError,
                "ISO output may not replace a checked seed input",
            ):
                hostbuild.build_iso(
                    fixtures,
                    tool_alias,
                    manifest,
                    seed_manifest=seed_manifest,
                )
            self.assertTrue(os.path.samefile(tool_alias, tool))

    def test_build_iso_rejects_a_second_publisher_before_cupidobj(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            fixtures = work / "fixtures"
            manifest = work / "fixtures.manifest"
            output = work / "checked.iso"
            self._write_fixture_tree(fixtures)
            self._write_fixture_manifest(manifest, fixtures)
            output.write_bytes(b"existing image")
            original_timestamp = output.stat().st_mtime_ns
            publication_lock = hostbuild._acquire_iso_publication_lock(
                output.resolve()
            )
            try:
                with (
                    mock.patch(
                        "tools.hostbuild_iso.run_seed_tool",
                        side_effect=AssertionError(
                            "CupidObj must not run for a second publisher"
                        ),
                    ),
                    self.assertRaisesRegex(
                        hostbuild.IsoAuthoringError,
                        "another hostbuild publisher is active",
                    ),
                ):
                    hostbuild.build_iso(
                        fixtures,
                        output,
                        manifest,
                        seed_manifest=SEED_MANIFEST,
                    )
            finally:
                hostbuild._release_disk_publication_lock(publication_lock)

            self.assertEqual(output.read_bytes(), b"existing image")
            self.assertEqual(output.stat().st_mtime_ns, original_timestamp)

    def test_build_iso_rejects_manifest_drift_before_publication(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            fixtures = work / "fixtures"
            manifest = work / "fixtures.manifest"
            output = work / "manifest.iso"
            self._write_fixture_tree(fixtures)
            self._write_fixture_manifest(manifest, fixtures)
            output.write_bytes(b"existing image")
            original_render = hostbuild._render_iso_image

            def render_then_mutate(snapshot):
                image = original_render(snapshot)
                manifest.write_text(
                    manifest.read_text(encoding="ascii") + "late.txt\n",
                    encoding="ascii",
                    newline="\n",
                )
                return image

            with (
                mock.patch(
                    "tools.hostbuild_iso._render_iso_image",
                    side_effect=render_then_mutate,
                ),
                self.assertRaisesRegex(
                    hostbuild.IsoAuthoringError,
                    "manifest changed while authoring",
                ),
            ):
                hostbuild.build_iso(fixtures, output, manifest)

            self.assertEqual(output.read_bytes(), b"existing image")

    def test_build_iso_rejects_unsafe_manifest_forms_and_aliases(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            fixtures = work / "fixtures"
            manifest = work / "fixtures.manifest"
            self._write_fixture_tree(fixtures)

            cases = (
                ("", "manifest is empty"),
                ("\n", "blank entry"),
                ("../escape\n", "not normalized and relative"),
                ("sub\\nested.txt\n", "must use forward slashes"),
                ("readme file.txt\n", "may not contain whitespace"),
                (
                    "readme.txt\nREADME.TXT\n",
                    "duplicate or case-insensitive collision",
                ),
            )
            for payload, message in cases:
                with self.subTest(payload=payload):
                    manifest.write_text(
                        payload,
                        encoding="ascii",
                        newline="\n",
                    )
                    with self.assertRaisesRegex(
                        hostbuild.IsoAuthoringError,
                        message,
                    ):
                        hostbuild.build_iso(
                            fixtures,
                            work / "unsafe.iso",
                            manifest,
                        )

            inside = fixtures / "manifest.txt"
            self._write_fixture_manifest(inside, fixtures)
            with self.assertRaisesRegex(
                hostbuild.IsoAuthoringError,
                "must be outside the fixture tree",
            ):
                hostbuild.build_iso(
                    fixtures,
                    work / "inside.iso",
                    inside,
                )

            self._write_fixture_manifest(manifest, fixtures)
            with self.assertRaisesRegex(
                hostbuild.IsoAuthoringError,
                "may not alias the fixture manifest",
            ):
                hostbuild.build_iso(fixtures, manifest, manifest)

    def test_build_iso_enforces_the_primary_directory_depth_limit(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            fixtures = work / "fixtures"
            fixtures.mkdir()
            deepest = fixtures
            for index in range(7):
                deepest = deepest / f"d{index}"
                deepest.mkdir()
            (deepest / "level-eight.txt").write_bytes(b"accepted")

            with contextlib.redirect_stdout(io.StringIO()):
                hostbuild.build_iso(fixtures, work / "depth-eight.iso")

            too_deep = deepest / "d7"
            too_deep.mkdir()
            with self.assertRaisesRegex(
                hostbuild.IsoAuthoringError,
                "eight-level primary hierarchy limit",
            ):
                hostbuild.build_iso(fixtures, work / "depth-nine.iso")

    def test_build_iso_preserves_unchanged_output_and_big_fixture_times(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            fixtures = work / "fixtures"
            output = work / "hello.iso"
            self._write_fixture_tree(fixtures)

            with contextlib.redirect_stdout(io.StringIO()):
                hostbuild.gen_big(fixtures / "big.bin")
                hostbuild.build_iso(fixtures, output)
            first_output_time = output.stat().st_mtime_ns
            first_big_time = (fixtures / "big.bin").stat().st_mtime_ns
            os.utime(
                output,
                ns=(first_output_time - 1_000, first_output_time - 1_000),
            )
            expected_output_time = output.stat().st_mtime_ns

            with contextlib.redirect_stdout(io.StringIO()):
                hostbuild.gen_big(fixtures / "big.bin")
                hostbuild.build_iso(fixtures, output)

            self.assertEqual(output.stat().st_mtime_ns, expected_output_time)
            self.assertEqual(
                (fixtures / "big.bin").stat().st_mtime_ns,
                first_big_time,
            )

    def test_gen_big_uses_checked_cupidasm_and_reuses_equal_output(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            manifest = work / "manifest.json"
            source = work / "big_pattern.asm"
            output = work / "big.bin"
            manifest.write_bytes(b"checked manifest")
            source_payload = (
                REPO_ROOT / "test_iso" / "big_pattern.asm"
            ).read_bytes()
            source.write_bytes(source_payload)
            expected = bytes(index & 0xFF for index in range(4096))
            output.write_bytes(expected)
            original_time = output.stat().st_mtime_ns - 1_000_000
            os.utime(output, ns=(original_time, original_time))
            calls = []

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
                        tuple(arguments),
                        timeout,
                    )
                )
                self.assertEqual(tool_name, "cupidasm")
                self.assertEqual(arguments[:2], ["-f", "bin"])
                frozen_source = Path(arguments[2])
                candidate = Path(arguments[arguments.index("-o") + 1])
                self.assertEqual(frozen_source.read_bytes(), source_payload)
                candidate.write_bytes(expected)
                return subprocess.CompletedProcess(arguments, 0, "", "")

            stdout = io.StringIO()
            with (
                mock.patch(
                    "tools.hostbuild_iso.run_seed_tool",
                    side_effect=run_seed,
                ),
                contextlib.redirect_stdout(stdout),
            ):
                status = hostbuild.main(
                    [
                        "gen-big",
                        "--seed-manifest",
                        str(manifest),
                        "--source",
                        str(source),
                        str(output),
                    ]
                )

            self.assertEqual(status, 0)
            self.assertEqual(len(calls), 1)
            self.assertEqual(output.read_bytes(), expected)
            self.assertEqual(output.stat().st_mtime_ns, original_time)
            self.assertIn("[hostbuild] Reused", stdout.getvalue())

    def test_gen_big_preserves_output_when_checked_cupidasm_fails(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            manifest = work / "manifest.json"
            source = work / "big_pattern.asm"
            output = work / "big.bin"
            manifest.write_bytes(b"checked manifest")
            source.write_bytes(
                (REPO_ROOT / "test_iso" / "big_pattern.asm").read_bytes()
            )
            output.write_bytes(b"existing fixture")

            def run_seed(
                _manifest_path,
                _working_directory,
                _tool_name,
                arguments,
                *,
                timeout,
            ):
                self.assertEqual(timeout, 60)
                candidate = Path(arguments[arguments.index("-o") + 1])
                candidate.write_bytes(b"partial candidate")
                return subprocess.CompletedProcess(
                    arguments,
                    1,
                    "",
                    "bad TIMES expression",
                )

            diagnostic = io.StringIO()
            with (
                mock.patch(
                    "tools.hostbuild_iso.run_seed_tool",
                    side_effect=run_seed,
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "gen-big",
                        "--seed-manifest",
                        str(manifest),
                        "--source",
                        str(source),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertEqual(output.read_bytes(), b"existing fixture")
            self.assertIn(
                "checked CupidASM failed with status 1: "
                "bad TIMES expression",
                diagnostic.getvalue(),
            )

    def test_gen_big_rejects_cupid_assembler_parity_drift(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            manifest = work / "manifest.json"
            source = work / "big_pattern.asm"
            output = work / "big.bin"
            manifest.write_bytes(b"checked manifest")
            source.write_bytes(
                (REPO_ROOT / "test_iso" / "big_pattern.asm").read_bytes()
            )
            output.write_bytes(b"existing fixture")

            def run_seed(
                _manifest_path,
                _working_directory,
                _tool_name,
                arguments,
                *,
                timeout,
            ):
                self.assertEqual(timeout, 60)
                candidate = Path(arguments[arguments.index("-o") + 1])
                candidate.write_bytes(b"\x00" * 4096)
                return subprocess.CompletedProcess(arguments, 0, "", "")

            diagnostic = io.StringIO()
            with (
                mock.patch(
                    "tools.hostbuild_iso.run_seed_tool",
                    side_effect=run_seed,
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "gen-big",
                        "--seed-manifest",
                        str(manifest),
                        "--source",
                        str(source),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertEqual(output.read_bytes(), b"existing fixture")
            self.assertIn(
                "checked CupidASM big.bin differs from the 4096-byte "
                "fixture pattern",
                diagnostic.getvalue(),
            )

    def test_gen_big_rejects_live_source_drift_and_preserves_output(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            manifest = work / "manifest.json"
            source = work / "big_pattern.asm"
            output = work / "big.bin"
            manifest.write_bytes(b"checked manifest")
            source_payload = (
                REPO_ROOT / "test_iso" / "big_pattern.asm"
            ).read_bytes()
            source.write_bytes(source_payload)
            output.write_bytes(b"existing fixture")
            expected = bytes(index & 0xFF for index in range(4096))

            def run_seed(
                _manifest_path,
                _working_directory,
                _tool_name,
                arguments,
                *,
                timeout,
            ):
                self.assertEqual(timeout, 60)
                candidate = Path(arguments[arguments.index("-o") + 1])
                candidate.write_bytes(expected)
                source.write_bytes(source_payload + b"; concurrent edit\n")
                return subprocess.CompletedProcess(arguments, 0, "", "")

            diagnostic = io.StringIO()
            with (
                mock.patch(
                    "tools.hostbuild_iso.run_seed_tool",
                    side_effect=run_seed,
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "gen-big",
                        "--seed-manifest",
                        str(manifest),
                        "--source",
                        str(source),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertEqual(output.read_bytes(), b"existing fixture")
            self.assertEqual(
                source.read_bytes(),
                source_payload + b"; concurrent edit\n",
            )
            self.assertIn(
                "checked big-fixture inputs changed while CupidASM ran",
                diagnostic.getvalue(),
            )

    def test_gen_big_rejects_live_output_drift_without_overwriting_it(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            manifest = work / "manifest.json"
            source = work / "big_pattern.asm"
            output = work / "big.bin"
            manifest.write_bytes(b"checked manifest")
            source.write_bytes(
                (REPO_ROOT / "test_iso" / "big_pattern.asm").read_bytes()
            )
            output.write_bytes(b"existing fixture")
            expected = bytes(index & 0xFF for index in range(4096))

            def run_seed(
                _manifest_path,
                _working_directory,
                _tool_name,
                arguments,
                *,
                timeout,
            ):
                self.assertEqual(timeout, 60)
                candidate = Path(arguments[arguments.index("-o") + 1])
                candidate.write_bytes(expected)
                output.write_bytes(b"concurrent publisher")
                return subprocess.CompletedProcess(arguments, 0, "", "")

            diagnostic = io.StringIO()
            with (
                mock.patch(
                    "tools.hostbuild_iso.run_seed_tool",
                    side_effect=run_seed,
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "gen-big",
                        "--seed-manifest",
                        str(manifest),
                        "--source",
                        str(source),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertEqual(output.read_bytes(), b"concurrent publisher")
            self.assertIn(
                "generated ISO fixture changed while writing big.bin",
                diagnostic.getvalue(),
            )

    def test_build_iso_reproduces_the_tracked_guest_fixture(self):
        with tempfile.TemporaryDirectory() as td:
            output = Path(td) / "hello.iso"
            tracked = (REPO_ROOT / "test_iso" / "hello.iso").read_bytes()
            output.write_bytes(tracked)
            original_timestamp = output.stat().st_mtime_ns - 1_000_000
            os.utime(output, ns=(original_timestamp, original_timestamp))
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                status = hostbuild.main(
                    [
                        "build-iso",
                        "--seed-manifest",
                        str(SEED_MANIFEST),
                        "--fixtures",
                        str(REPO_ROOT / "test_iso" / "fixtures"),
                        "--manifest",
                        str(REPO_ROOT / "test_iso" / "fixtures.manifest"),
                        "--out",
                        str(output),
                    ]
                )
            self.assertEqual(status, 0)
            self.assertEqual(output.read_bytes(), tracked)
            self.assertEqual(output.stat().st_mtime_ns, original_timestamp)
            self.assertIn("[hostbuild] Reused", stdout.getvalue())

    def test_build_iso_rejects_source_drift_before_publication(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            fixtures = work / "fixtures"
            output = work / "hello.iso"
            self._write_fixture_tree(fixtures)
            output.write_bytes(b"existing image")
            original_render = hostbuild._render_iso_image

            def render_then_mutate(snapshot):
                image = original_render(snapshot)
                (fixtures / "readme.txt").write_bytes(b"changed during build")
                return image

            with (
                mock.patch(
                    "tools.hostbuild_iso._render_iso_image",
                    side_effect=render_then_mutate,
                ),
                self.assertRaisesRegex(
                    hostbuild.IsoAuthoringError,
                    "changed while authoring",
                ),
            ):
                hostbuild.build_iso(fixtures, output)

            self.assertEqual(output.read_bytes(), b"existing image")

    def test_build_iso_rejects_membership_and_output_drift(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            fixtures = work / "fixtures"
            output = work / "hello.iso"
            self._write_fixture_tree(fixtures)
            output.write_bytes(b"existing image")
            original_render = hostbuild._render_iso_image

            def render_then_add(snapshot):
                image = original_render(snapshot)
                (fixtures / "added.txt").write_bytes(b"late member")
                return image

            with (
                mock.patch(
                    "tools.hostbuild_iso._render_iso_image",
                    side_effect=render_then_add,
                ),
                self.assertRaisesRegex(
                    hostbuild.IsoAuthoringError,
                    "changed while authoring",
                ),
            ):
                hostbuild.build_iso(fixtures, output)
            self.assertEqual(output.read_bytes(), b"existing image")
            (fixtures / "added.txt").unlink()

            def render_then_replace_output(snapshot):
                image = original_render(snapshot)
                output.write_bytes(b"concurrent output")
                return image

            with (
                mock.patch(
                    "tools.hostbuild_iso._render_iso_image",
                    side_effect=render_then_replace_output,
                ),
                self.assertRaisesRegex(
                    hostbuild.IsoAuthoringError,
                    "output changed while authoring",
                ),
            ):
                hostbuild.build_iso(fixtures, output)
            self.assertEqual(output.read_bytes(), b"concurrent output")

    def test_build_iso_preserves_output_after_publication_failure(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            fixtures = work / "fixtures"
            output = work / "hello.iso"
            self._write_fixture_tree(fixtures)
            output.write_bytes(b"existing image")

            with (
                mock.patch(
                    "tools.hostbuild.os.replace",
                    side_effect=OSError("publication denied"),
                ),
                self.assertRaisesRegex(
                    hostbuild.IsoAuthoringError,
                    "could not be published",
                ),
            ):
                hostbuild.build_iso(fixtures, output)

            self.assertEqual(output.read_bytes(), b"existing image")

    def test_build_iso_rejects_unsafe_paths_and_unsupported_names(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            missing = work / "missing"
            with self.assertRaisesRegex(
                hostbuild.IsoAuthoringError,
                "cannot be resolved",
            ):
                hostbuild.build_iso(missing, work / "missing.iso")

            regular = work / "regular"
            regular.write_bytes(b"not a directory")
            with self.assertRaisesRegex(
                hostbuild.IsoAuthoringError,
                "not a directory",
            ):
                hostbuild.build_iso(regular, work / "regular.iso")

            fixtures = work / "fixtures"
            self._write_fixture_tree(fixtures)
            with self.assertRaisesRegex(
                hostbuild.IsoAuthoringError,
                "inside the fixture tree",
            ):
                hostbuild.build_iso(fixtures, fixtures / "output.iso")

            unsafe_for_make = fixtures / "unsafe;name.txt"
            unsafe_for_make.write_bytes(b"unsafe prerequisite grammar")
            with self.assertRaisesRegex(
                hostbuild.IsoAuthoringError,
                "portable filename characters",
            ):
                hostbuild.build_iso(fixtures, work / "make-unsafe.iso")
            unsafe_for_make.unlink()

            (fixtures / "snowman-\N{SNOWMAN}.txt").write_bytes(b"cold")
            with self.assertRaisesRegex(
                hostbuild.IsoAuthoringError,
                "ASCII",
            ):
                hostbuild.build_iso(fixtures, work / "unicode.iso")
            (fixtures / "snowman-\N{SNOWMAN}.txt").unlink()

            (fixtures / ("x" * 240 + ".txt")).write_bytes(b"long")
            with self.assertRaisesRegex(
                hostbuild.IsoAuthoringError,
                "directory record",
            ):
                hostbuild.build_iso(fixtures, work / "long.iso")

    def test_build_iso_rejects_links_and_non_file_outputs(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            fixtures = work / "fixtures"
            self._write_fixture_tree(fixtures)
            target = work / "target.txt"
            target.write_bytes(b"target")
            link = fixtures / "linked.txt"
            try:
                link.symlink_to(target)
            except OSError as error:
                self.skipTest(f"symbolic links are unavailable: {error}")

            with self.assertRaisesRegex(
                hostbuild.IsoAuthoringError,
                "symbolic link|junction",
            ):
                hostbuild.build_iso(fixtures, work / "linked.iso")
            link.unlink()

            directory_link = fixtures / "linked-directory"
            directory_link.symlink_to(
                fixtures / "sub",
                target_is_directory=True,
            )
            with self.assertRaisesRegex(
                hostbuild.IsoAuthoringError,
                "symbolic link|junction",
            ):
                hostbuild.build_iso(
                    fixtures,
                    work / "linked-directory.iso",
                )
            directory_link.unlink()

            output_directory = work / "output.iso"
            output_directory.mkdir()
            with self.assertRaisesRegex(
                hostbuild.IsoAuthoringError,
                "not a regular file",
            ):
                hostbuild.build_iso(fixtures, output_directory)

            output_link = work / "output-link.iso"
            output_link.symlink_to(target)
            with self.assertRaisesRegex(
                hostbuild.IsoAuthoringError,
                "symbolic link|junction",
            ):
                hostbuild.build_iso(fixtures, output_link)
            output_link.unlink()

            output_hard_link = work / "output-hard-link.iso"
            try:
                os.link(fixtures / "readme.txt", output_hard_link)
            except OSError:
                return
            with self.assertRaisesRegex(
                hostbuild.IsoAuthoringError,
                "hard link to a fixture",
            ):
                hostbuild.build_iso(fixtures, output_hard_link)

    @unittest.skipUnless(os.name == "nt", "NTFS junction test")
    def test_build_iso_rejects_a_nested_junction(self):
        work = Path(
            self.enterContext(tempfile.TemporaryDirectory())
        )
        fixtures = work / "fixtures"
        self._write_fixture_tree(fixtures)
        target = work / "junction-target"
        target.mkdir()
        (target / "outside.txt").write_bytes(b"outside")
        junction = fixtures / "junction"
        result = subprocess.run(
            (
                "cmd",
                "/c",
                "mklink",
                "/J",
                str(junction),
                str(target),
            ),
            text=True,
            capture_output=True,
            timeout=30,
            check=False,
        )
        if result.returncode != 0:
            self.skipTest(f"junction unavailable: {result.stderr}")
        self.addCleanup(
            lambda: junction.rmdir() if junction.exists() else None
        )
        self.assertTrue(junction.is_junction())
        with (
            mock.patch.object(os.path, "isjunction", None),
            self.assertRaisesRegex(
                hostbuild.IsoAuthoringError,
                "symbolic link|junction",
            ),
        ):
            hostbuild.build_iso(fixtures, work / "junction.iso")

    def test_build_iso_rejects_case_insensitive_name_collisions(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            fixtures = work / "fixtures"
            fixtures.mkdir()
            (fixtures / "Case.txt").write_bytes(b"upper")
            (fixtures / "case.txt").write_bytes(b"lower")
            if len(list(fixtures.iterdir())) != 2:
                self.skipTest(
                    "the host filesystem cannot create case-only siblings"
                )
            with self.assertRaisesRegex(
                hostbuild.IsoAuthoringError,
                "case-insensitive",
            ):
                hostbuild.build_iso(fixtures, work / "case.iso")

    def test_build_iso_cli_reports_a_useful_error(self):
        diagnostic = io.StringIO()
        with tempfile.TemporaryDirectory() as td:
            with contextlib.redirect_stderr(diagnostic):
                status = hostbuild.main(
                    [
                        "build-iso",
                        "--seed-manifest",
                        str(SEED_MANIFEST),
                        "--fixtures",
                        str(Path(td) / "missing"),
                        "--manifest",
                        str(Path(td) / "fixtures.manifest"),
                        "--out",
                        str(Path(td) / "hello.iso"),
                    ]
                )
        self.assertEqual(status, 1)
        self.assertIn(
            "[hostbuild] build-iso failed: ISO fixture tree cannot be resolved",
            diagnostic.getvalue(),
        )

    def test_gen_big_rejects_a_link_without_changing_its_target(self):
        with tempfile.TemporaryDirectory() as td:
            work = Path(td)
            target = work / "target.bin"
            target.write_bytes(b"keep this")
            output = work / "big.bin"
            try:
                output.symlink_to(target)
            except OSError as error:
                self.skipTest(f"symbolic links are unavailable: {error}")

            with self.assertRaisesRegex(
                hostbuild.IsoAuthoringError,
                "symbolic link|junction",
            ):
                hostbuild.gen_big(output)

            self.assertEqual(target.read_bytes(), b"keep this")
