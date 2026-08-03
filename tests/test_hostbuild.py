import contextlib
import io
import os
import re
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import hostbuild

REPO_ROOT = Path(__file__).resolve().parents[1]
BASELINE_JPEG = (
    b"\xff\xd8"
    b"\xff\xc0\x00\x0b\x08\x00\x01\x00\x01\x01\x01\x11\x00"
    b"\xff\xda\x00\x08\x01\x01\x00\x00\x3f\x00"
    b"\xff\xd9"
)
PROGRESSIVE_JPEG = BASELINE_JPEG.replace(b"\xff\xc0", b"\xff\xc2", 1)
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


class HostBuildImageTests(unittest.TestCase):
    def test_image_create_stages_file_and_preserves_existing_fat(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            image = root / "cupidos.img"
            staged = root / "hello.iso"

            boot.write_bytes(bytes((i & 0xFF) for i in range(5 * 512)))
            kernel.write_bytes(b"KERNEL" * 200)
            staged.write_bytes(b"iso fixture")

            hostbuild.create_or_update_image(
                image=image,
                bootloader=boot,
                kernel=kernel,
                hdd_mb=16,
                fat_start_lba=2048,
                stage_files=[hostbuild.StageFile(staged, "/hello.iso")],
                force_format=False,
            )

            data = image.read_bytes()
            self.assertEqual(data[510:512], b"\x55\xaa")
            self.assertEqual(data[446], 0x80)
            self.assertEqual(data[450], 0x06)
            self.assertEqual(struct.unpack_from("<I", data, 454)[0], 2048)
            self.assertEqual(data[:446], boot.read_bytes()[:446])
            self.assertEqual(data[512:5 * 512], boot.read_bytes()[512:5 * 512])
            self.assertEqual(data[5 * 512:5 * 512 + kernel.stat().st_size], kernel.read_bytes())

            fat_offset = 2048 * 512
            self.assertEqual(data[fat_offset + 510:fat_offset + 512], b"\x55\xaa")
            self.assertEqual(struct.unpack_from("<H", data, fat_offset + 11)[0], 512)
            self.assertEqual(data[fat_offset + 54:fat_offset + 62].rstrip(), b"FAT16")
            self.assertIn(b"HELLO   ISO", data[fat_offset:fat_offset + 256 * 1024])

            kernel.write_bytes(b"NEWKERNEL")
            hostbuild.create_or_update_image(
                image=image,
                bootloader=boot,
                kernel=kernel,
                hdd_mb=16,
                fat_start_lba=2048,
                stage_files=[],
                force_format=False,
            )

            data2 = image.read_bytes()
            self.assertIn(b"HELLO   ISO", data2[fat_offset:fat_offset + 256 * 1024])
            self.assertEqual(data2[5 * 512:5 * 512 + kernel.stat().st_size], b"NEWKERNEL")

    def test_image_rejects_kernel_overlap_with_fat_partition(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            image = root / "cupidos.img"

            boot.write_bytes(b"B" * (5 * 512))
            kernel.write_bytes(b"K" * (20 * 512))

            with self.assertRaisesRegex(ValueError, "overlaps FAT partition"):
                hostbuild.create_or_update_image(
                    image=image,
                    bootloader=boot,
                    kernel=kernel,
                    hdd_mb=8,
                    fat_start_lba=16,
                    stage_files=[],
                    force_format=False,
                )


class HostBuildIsoTests(unittest.TestCase):
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
                    "tools.hostbuild._render_iso_image",
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

    def test_build_iso_reproduces_the_tracked_guest_fixture(self):
        with tempfile.TemporaryDirectory() as td:
            output = Path(td) / "hello.iso"
            with contextlib.redirect_stdout(io.StringIO()):
                hostbuild.build_iso(
                    REPO_ROOT / "test_iso" / "fixtures",
                    output,
                    REPO_ROOT / "test_iso" / "fixtures.manifest",
                )
            self.assertEqual(
                output.read_bytes(),
                (REPO_ROOT / "test_iso" / "hello.iso").read_bytes(),
            )

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
                    "tools.hostbuild._render_iso_image",
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
                    "tools.hostbuild._render_iso_image",
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
                    "tools.hostbuild._render_iso_image",
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
                        "--fixtures",
                        str(Path(td) / "missing"),
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


class HostBuildSymbolTests(unittest.TestCase):
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
                "tools.hostbuild.run_seed_tool",
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
                    "tools.hostbuild.run_seed_tool",
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
                    "tools.hostbuild.run_seed_tool",
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
                    "tools.hostbuild.run_seed_tool",
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
                    "tools.hostbuild.run_seed_tool",
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
                    "tools.hostbuild.run_seed_tool",
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
                    "tools.hostbuild.run_seed_tool",
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
                hostbuild,
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


class HostBuildAssetTests(unittest.TestCase):
    def test_embed_jpeg_runs_checked_cupidobj_from_the_seed(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(BASELINE_JPEG)
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
                        arguments,
                        timeout,
                    )
                )
                self.assertEqual(tool_name, "cupidobj")
                self.assertEqual(arguments[0], "wrap")
                self.assertEqual(arguments[2:4], ["--identity", str(src)])
                Path(arguments[-1]).write_bytes(b"checked object")
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments],
                    0,
                    "",
                    "",
                )

            with mock.patch(
                "tools.hostbuild.run_seed_tool",
                side_effect=run_seed,
                create=True,
            ):
                status = hostbuild.main(
                    [
                        "embed-jpeg",
                        "--seed-manifest",
                        str(manifest),
                        str(src),
                        str(out),
                    ]
                )

            self.assertEqual(status, 0)
            self.assertEqual(len(calls), 1)
            self.assertEqual(out.read_bytes(), b"checked object")

    def test_embed_jpeg_preserves_output_after_checked_seed_failure(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(BASELINE_JPEG)
            out.write_bytes(b"existing object")
            failure = subprocess.CompletedProcess(
                ["cupidobj", "wrap"],
                6,
                "",
                "wrap failed\n",
            )
            diagnostic = io.StringIO()

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    return_value=failure,
                    create=True,
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "embed-jpeg",
                        "--seed-manifest",
                        str(manifest),
                        str(src),
                        str(out),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn(
                "checked CupidObj failed with status 6: wrap failed",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

    def test_embed_jpeg_rejects_input_drift_before_publication(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(BASELINE_JPEG)
            out.write_bytes(b"existing object")
            diagnostic = io.StringIO()

            def run_seed(
                _manifest,
                _working_directory,
                _tool_name,
                arguments,
                *,
                timeout,
            ):
                self.assertEqual(timeout, 60)
                src.write_bytes(BASELINE_JPEG + b"\x00")
                Path(arguments[-1]).write_bytes(b"checked object")
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments],
                    0,
                    "",
                    "",
                )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "embed-jpeg",
                        "--seed-manifest",
                        str(manifest),
                        str(src),
                        str(out),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn(
                "checked JPEG inputs changed while wrapping the object",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

    def test_embed_jpeg_wraps_checked_in_bytes_with_original_identity(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            src.write_bytes(BASELINE_JPEG)
            object_tool_commands = []

            def fake_run(args, **kwargs):
                if args[0] == "cupidobj":
                    object_tool_commands.append(args)
                    self.assertEqual(
                        Path(args[2]).read_bytes(),
                        BASELINE_JPEG,
                    )
                    Path(args[-1]).write_bytes(b"object")
                    return subprocess.CompletedProcess(args, 0)
                raise AssertionError(f"unexpected command: {args}")

            with mock.patch(
                "tools.hostbuild.shutil.which",
                side_effect=AssertionError("host converter lookup"),
            ), mock.patch(
                "tools.hostbuild.subprocess.run",
                side_effect=fake_run,
            ):
                hostbuild.embed_jpeg("cupidobj", src, out)

            self.assertEqual(
                object_tool_commands,
                [[
                    "cupidobj",
                    "wrap",
                    str(out) + ".baseline.jpg",
                    "--identity",
                    str(src),
                    "-o",
                    str(out),
                ]],
            )
            self.assertTrue(out.exists())

    def test_embed_jpeg_rejects_progressive_input_without_replacing_output(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(PROGRESSIVE_JPEG)
            out.write_bytes(b"existing object")
            diagnostic = io.StringIO()

            with mock.patch(
                "tools.hostbuild.shutil.which",
                side_effect=AssertionError("host converter lookup"),
            ), mock.patch(
                "tools.hostbuild.run_seed_tool",
                side_effect=AssertionError("CupidObj must not run"),
            ), contextlib.redirect_stderr(diagnostic):
                status = hostbuild.main(
                    [
                        "embed-jpeg",
                        "--seed-manifest",
                        str(manifest),
                        str(src),
                        str(out),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn(
                "unsupported progressive JPEG frame",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

    def test_embed_jpeg_rejects_malformed_frame_without_running_cupidobj(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(
                BASELINE_JPEG.replace(
                    b"\xff\xc0\x00\x0b",
                    b"\xff\xc0\x00\x08",
                    1,
                )
            )
            out.write_bytes(b"existing object")
            diagnostic = io.StringIO()

            with mock.patch(
                "tools.hostbuild.run_seed_tool",
                side_effect=AssertionError("CupidObj must not run"),
            ), contextlib.redirect_stderr(diagnostic):
                status = hostbuild.main(
                    [
                        "embed-jpeg",
                        "--seed-manifest",
                        str(manifest),
                        str(src),
                        str(out),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn(
                "JPEG frame header has an invalid component table",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")


if __name__ == "__main__":
    unittest.main()
