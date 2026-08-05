import contextlib
import io
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import hostbuild

REPO_ROOT = Path(__file__).resolve().parents[1]
SEED_MANIFEST = REPO_ROOT / "bootstrap" / "seeds" / "i386-linux" / "manifest.json"
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
    IMAGE_MB = 4
    FAT_START_LBA = 8

    @property
    def image_bytes(self):
        return self.IMAGE_MB * 1024 * 1024

    @property
    def image_sectors(self):
        return self.image_bytes // hostbuild.SECTOR_SIZE

    def _disk_template_payload(self, boot_payload, kernel_payload):
        layout = hostbuild._choose_layout(
            self.image_sectors - self.FAT_START_LBA
        )
        template_sectors = (
            self.FAT_START_LBA
            + layout.reserved_sectors
            + layout.num_fats * layout.sectors_per_fat
            + layout.root_dir_sectors
        )
        stream = io.BytesIO(
            b"\x00" * (template_sectors * hostbuild.SECTOR_SIZE)
        )
        hostbuild._write_mbr(
            stream,
            boot_payload,
            self.FAT_START_LBA,
            self.image_sectors - self.FAT_START_LBA,
        )
        stream.seek(hostbuild.SECTOR_SIZE)
        stream.write(
            boot_payload[
                hostbuild.SECTOR_SIZE : 5 * hostbuild.SECTOR_SIZE
            ]
        )
        stream.seek(5 * hostbuild.SECTOR_SIZE)
        stream.write(kernel_payload)
        hostbuild._write_fat16_filesystem(
            stream,
            self.FAT_START_LBA,
            layout,
        )
        return stream.getvalue()

    def _write_valid_image(self, image, template):
        image.write_bytes(template)
        with image.open("r+b") as stream:
            stream.truncate(self.image_bytes)

    def _copy_checked_seed(self, root):
        copied_seed = root / "seed"
        shutil.copytree(SEED_MANIFEST.parent, copied_seed)
        return copied_seed / SEED_MANIFEST.name

    def _checked_disk_runner(
        self,
        *,
        manifest,
        boot,
        kernel,
        boot_payload,
        kernel_payload,
        template,
        after_write=None,
        calls=None,
    ):
        def run_seed(
            manifest_path,
            working_directory,
            tool_name,
            arguments,
            *,
            timeout,
        ):
            command = [str(argument) for argument in arguments]
            self.assertEqual(Path(manifest_path), manifest.resolve())
            private_root = Path(working_directory).resolve()
            self.assertTrue(private_root.is_dir())
            self.assertEqual(tool_name, "cupidobj")
            self.assertEqual(timeout, 60)
            self.assertEqual(command[0], "disk-template")
            self.assertEqual(command[2], "--kernel")
            self.assertEqual(command[4:6], ["--image-sectors", str(self.image_sectors)])
            self.assertEqual(
                command[6:8],
                ["--fat-start-lba", str(self.FAT_START_LBA)],
            )
            self.assertEqual(command[8], "-o")

            frozen_boot = Path(command[1])
            frozen_kernel = Path(command[3])
            candidate = Path(command[9])
            self.assertIn(private_root, frozen_boot.resolve().parents)
            self.assertIn(private_root, frozen_kernel.resolve().parents)
            self.assertIn(private_root, candidate.resolve().parents)
            self.assertNotEqual(frozen_boot.resolve(), boot.resolve())
            self.assertNotEqual(frozen_kernel.resolve(), kernel.resolve())
            self.assertEqual(frozen_boot.read_bytes(), boot_payload)
            self.assertEqual(frozen_kernel.read_bytes(), kernel_payload)
            candidate.write_bytes(template)
            if calls is not None:
                calls.append(
                    (
                        Path(manifest_path),
                        Path(working_directory),
                        tool_name,
                        tuple(command),
                        timeout,
                    )
                )
            if after_write is not None:
                after_write(candidate)
            return subprocess.CompletedProcess(
                ["cupidobj", *command],
                0,
                "",
                "",
            )

        return run_seed

    def _create_image(
        self,
        *,
        image,
        boot,
        kernel,
        manifest=SEED_MANIFEST,
        stage_files=(),
        force_format=False,
    ):
        hostbuild.create_or_update_image(
            image=image,
            bootloader=boot,
            kernel=kernel,
            hdd_mb=self.IMAGE_MB,
            fat_start_lba=self.FAT_START_LBA,
            stage_files=list(stage_files),
            force_format=force_format,
            seed_manifest=manifest,
        )

    def test_layout_advances_after_a_fat_size_cycle(self):
        layout = hostbuild._choose_layout(8288)

        self.assertEqual(layout.sectors_per_cluster, 2)
        self.assertEqual(layout.sectors_per_fat, 17)
        self.assertEqual(layout.data_sectors, 8221)
        self.assertEqual(layout.cluster_count, 4110)

    def test_image_uses_the_real_promoted_seed_for_a_compact_disk(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            image = root / "cupidos.img"
            oracle = root / "oracle.img"
            boot_payload = bytes(
                (index * 17 + 3) & 0xFF
                for index in range(5 * hostbuild.SECTOR_SIZE)
            )
            kernel_payload = b"real checked CupidObj kernel\n"
            boot.write_bytes(boot_payload)
            kernel.write_bytes(kernel_payload)

            with contextlib.redirect_stdout(io.StringIO()):
                self._create_image(
                    image=image,
                    boot=boot,
                    kernel=kernel,
                )
            oracle_size = hostbuild._write_pristine_disk_template(
                oracle,
                boot,
                kernel,
                self.image_sectors,
                self.FAT_START_LBA,
            )
            expected = oracle.read_bytes() + b"\x00" * (
                self.image_bytes - oracle_size
            )

            self.assertEqual(image.read_bytes(), expected)

    def test_image_runs_checked_cupidobj_before_oracle_and_publishes_full_image(
        self,
    ):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            image = root / "cupidos.img"
            boot_payload = bytes(
                (index * 17 + 3) & 0xFF
                for index in range(5 * hostbuild.SECTOR_SIZE)
            )
            kernel_payload = b"Cupid kernel\x00" * 41
            boot.write_bytes(boot_payload)
            kernel.write_bytes(kernel_payload)
            template = self._disk_template_payload(
                boot_payload,
                kernel_payload,
            )
            expected = template + b"\x00" * (
                self.image_bytes - len(template)
            )
            events = []
            calls = []
            real_write_mbr = hostbuild._write_mbr
            real_replace = os.replace
            publications = []

            def write_python_oracle(*args, **kwargs):
                events.append("python-oracle")
                return real_write_mbr(*args, **kwargs)

            runner = self._checked_disk_runner(
                manifest=SEED_MANIFEST,
                boot=boot,
                kernel=kernel,
                boot_payload=boot_payload,
                kernel_payload=kernel_payload,
                template=template,
                calls=calls,
            )

            def run_checked(*args, **kwargs):
                events.append("checked-cupidobj")
                return runner(*args, **kwargs)

            def publish(candidate, output):
                candidate_path = Path(candidate)
                output_path = Path(output)
                self.assertEqual(output_path, image)
                self.assertIn(image.parent.resolve(), candidate_path.resolve().parents)
                self.assertEqual(candidate_path.stat().st_size, self.image_bytes)
                publications.append((candidate_path, output_path))
                return real_replace(candidate, output)

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_checked,
                ),
                mock.patch(
                    "tools.hostbuild._write_mbr",
                    side_effect=write_python_oracle,
                ),
                mock.patch(
                    "tools.hostbuild.os.replace",
                    side_effect=publish,
                ),
                contextlib.redirect_stdout(io.StringIO()),
            ):
                self._create_image(
                    image=image,
                    boot=boot,
                    kernel=kernel,
                )

            self.assertEqual(events[0], "checked-cupidobj")
            self.assertIn("python-oracle", events)
            self.assertEqual(len(calls), 1)
            self.assertEqual(len(publications), 1)
            self.assertEqual(image.stat().st_size, self.image_bytes)
            self.assertEqual(image.read_bytes(), expected)

    def test_image_reuse_preserves_fat_content_while_updating_inputs(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            image = root / "cupidos.img"
            staged = root / "new.bin"
            old_boot = b"B" * (5 * hostbuild.SECTOR_SIZE)
            old_kernel = b"O" * 1200
            self._write_valid_image(
                image,
                self._disk_template_payload(old_boot, old_kernel),
            )
            fat_offset = self.FAT_START_LBA * hostbuild.SECTOR_SIZE
            with image.open("r+b") as stream:
                stream.seek(fat_offset + 39)
                stream.write(struct.pack("<I", 0x12345678))
                stream.write(b"PERSISTENT ")
            with hostbuild.Fat16Image(image, self.FAT_START_LBA) as fat:
                fat.write_file("/keep.txt", b"unrelated user data")

            boot_payload = bytes(
                (index * 29 + 7) & 0xFF
                for index in range(5 * hostbuild.SECTOR_SIZE)
            )
            kernel_payload = b"new kernel"
            boot.write_bytes(boot_payload)
            kernel.write_bytes(kernel_payload)
            staged.write_bytes(b"new staged payload")
            template = self._disk_template_payload(
                boot_payload,
                kernel_payload,
            )
            runner = self._checked_disk_runner(
                manifest=SEED_MANIFEST,
                boot=boot,
                kernel=kernel,
                boot_payload=boot_payload,
                kernel_payload=kernel_payload,
                template=template,
            )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=runner,
                ),
                contextlib.redirect_stdout(io.StringIO()),
            ):
                self._create_image(
                    image=image,
                    boot=boot,
                    kernel=kernel,
                    stage_files=(
                        hostbuild.StageFile(staged, "/new.bin"),
                    ),
                )

            data = image.read_bytes()
            self.assertEqual(len(data), self.image_bytes)
            self.assertEqual(data[:fat_offset], template[:fat_offset])
            self.assertEqual(
                struct.unpack_from("<I", data, fat_offset + 39)[0],
                0x12345678,
            )
            self.assertEqual(
                data[fat_offset + 43 : fat_offset + 54],
                b"PERSISTENT ",
            )
            self.assertIn(b"KEEP    TXT", data[fat_offset:])
            self.assertIn(b"unrelated user data", data[fat_offset:])
            self.assertIn(b"NEW     BIN", data[fat_offset:])
            self.assertIn(b"new staged payload", data[fat_offset:])
            kernel_start = 5 * hostbuild.SECTOR_SIZE
            self.assertEqual(
                data[kernel_start : kernel_start + len(kernel_payload)],
                kernel_payload,
            )
            self.assertEqual(
                data[
                    kernel_start + len(kernel_payload) :
                    kernel_start + len(old_kernel)
                ],
                b"\x00" * (len(old_kernel) - len(kernel_payload)),
            )

    def test_image_force_format_and_invalid_existing_image_recreate_pristine_fat(
        self,
    ):
        for existing_kind in ("forced valid image", "invalid image"):
            with (
                self.subTest(existing_kind=existing_kind),
                tempfile.TemporaryDirectory() as td,
            ):
                root = Path(td)
                boot = root / "boot.bin"
                kernel = root / "kernel.bin"
                image = root / "cupidos.img"
                boot_payload = bytes(
                    (index * 31 + 9) & 0xFF
                    for index in range(5 * hostbuild.SECTOR_SIZE)
                )
                kernel_payload = b"fresh kernel"
                boot.write_bytes(boot_payload)
                kernel.write_bytes(kernel_payload)
                template = self._disk_template_payload(
                    boot_payload,
                    kernel_payload,
                )
                expected = template + b"\x00" * (
                    self.image_bytes - len(template)
                )

                if existing_kind == "forced valid image":
                    self._write_valid_image(image, template)
                    with hostbuild.Fat16Image(
                        image,
                        self.FAT_START_LBA,
                    ) as fat:
                        fat.write_file(
                            "/old.txt",
                            b"content that force-format must remove",
                        )
                    force_format = True
                else:
                    with image.open("wb") as stream:
                        stream.truncate(self.image_bytes)
                        stream.seek(
                            self.FAT_START_LBA * hostbuild.SECTOR_SIZE
                        )
                        stream.write(
                            b"OLD     TXT"
                            b"content that invalid recreation must remove"
                        )
                    force_format = False

                runner = self._checked_disk_runner(
                    manifest=SEED_MANIFEST,
                    boot=boot,
                    kernel=kernel,
                    boot_payload=boot_payload,
                    kernel_payload=kernel_payload,
                    template=template,
                )
                with (
                    mock.patch(
                        "tools.hostbuild.run_seed_tool",
                        side_effect=runner,
                    ),
                    contextlib.redirect_stdout(io.StringIO()),
                ):
                    self._create_image(
                        image=image,
                        boot=boot,
                        kernel=kernel,
                        force_format=force_format,
                    )

                data = image.read_bytes()
                self.assertEqual(data, expected)
                self.assertNotIn(b"OLD     TXT", data)
                self.assertNotIn(b"content that", data)

    def test_image_recreates_superficially_valid_images_with_bad_fat_geometry(
        self,
    ):
        def zero_sectors_per_cluster(bpb, _partition_sectors):
            bpb[13] = 0

        def write_wrong_total_sectors(bpb, partition_sectors):
            struct.pack_into("<H", bpb, 19, partition_sectors - 1)

        def write_undersized_fat(bpb, _partition_sectors):
            struct.pack_into("<H", bpb, 22, 1)

        def write_out_of_range_cluster_geometry(
            bpb,
            _partition_sectors,
        ):
            bpb[13] = 64

        corruptions = (
            ("zero sectors per cluster", zero_sectors_per_cluster),
            ("wrong total sectors", write_wrong_total_sectors),
            ("undersized FAT", write_undersized_fat),
            (
                "out-of-range cluster geometry",
                write_out_of_range_cluster_geometry,
            ),
        )
        for description, corrupt in corruptions:
            with (
                self.subTest(description=description),
                tempfile.TemporaryDirectory() as td,
            ):
                root = Path(td)
                boot = root / "boot.bin"
                kernel = root / "kernel.bin"
                image = root / "cupidos.img"
                boot_payload = bytes(
                    (index * 13 + 5) & 0xFF
                    for index in range(5 * hostbuild.SECTOR_SIZE)
                )
                kernel_payload = b"replacement kernel"
                boot.write_bytes(boot_payload)
                kernel.write_bytes(kernel_payload)
                template = self._disk_template_payload(
                    boot_payload,
                    kernel_payload,
                )
                expected = template + b"\x00" * (
                    self.image_bytes - len(template)
                )
                self._write_valid_image(image, template)
                with hostbuild.Fat16Image(
                    image,
                    self.FAT_START_LBA,
                ) as fat:
                    fat.write_file(
                        "/old.txt",
                        b"content from malformed FAT",
                    )

                fat_offset = self.FAT_START_LBA * hostbuild.SECTOR_SIZE
                partition_sectors = (
                    self.image_sectors - self.FAT_START_LBA
                )
                with image.open("r+b") as stream:
                    stream.seek(fat_offset)
                    bpb = bytearray(stream.read(hostbuild.SECTOR_SIZE))
                    corrupt(bpb, partition_sectors)
                    stream.seek(fat_offset)
                    stream.write(bpb)
                self.assertEqual(
                    hostbuild._partition_info(image),
                    (0x06, self.FAT_START_LBA, partition_sectors),
                )
                runner = self._checked_disk_runner(
                    manifest=SEED_MANIFEST,
                    boot=boot,
                    kernel=kernel,
                    boot_payload=boot_payload,
                    kernel_payload=kernel_payload,
                    template=template,
                )
                output = io.StringIO()

                with (
                    mock.patch(
                        "tools.hostbuild.run_seed_tool",
                        side_effect=runner,
                    ),
                    contextlib.redirect_stdout(output),
                ):
                    self._create_image(
                        image=image,
                        boot=boot,
                        kernel=kernel,
                    )

                data = image.read_bytes()
                self.assertEqual(data, expected)
                self.assertNotIn(b"OLD     TXT", data)
                self.assertNotIn(b"content from malformed FAT", data)
                self.assertIn("Created", output.getvalue())
                self.assertNotIn("Reused", output.getvalue())

    def test_image_republishes_a_byte_identical_rebuild_with_a_new_timestamp(
        self,
    ):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            image = root / "cupidos.img"
            boot_payload = b"B" * (5 * hostbuild.SECTOR_SIZE)
            kernel_payload = b"unchanged kernel"
            boot.write_bytes(boot_payload)
            kernel.write_bytes(kernel_payload)
            template = self._disk_template_payload(
                boot_payload,
                kernel_payload,
            )
            self._write_valid_image(image, template)
            original = image.read_bytes()
            old_timestamp = image.stat().st_mtime_ns - 10_000_000_000
            os.utime(image, ns=(old_timestamp, old_timestamp))
            self.assertEqual(image.stat().st_mtime_ns, old_timestamp)
            runner = self._checked_disk_runner(
                manifest=SEED_MANIFEST,
                boot=boot,
                kernel=kernel,
                boot_payload=boot_payload,
                kernel_payload=kernel_payload,
                template=template,
            )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=runner,
                ),
                contextlib.redirect_stdout(io.StringIO()),
            ):
                self._create_image(
                    image=image,
                    boot=boot,
                    kernel=kernel,
                )

            self.assertEqual(image.read_bytes(), original)
            self.assertGreater(image.stat().st_mtime_ns, old_timestamp)

    def test_image_private_composition_accepts_arbitrary_output_basenames(self):
        for build_kind in ("fresh", "reuse"):
            for basename in (
                "checked-template.img",
                "existing.img",
                "python-template.img",
                "input",
            ):
                with (
                    self.subTest(
                        build_kind=build_kind,
                        basename=basename,
                    ),
                    tempfile.TemporaryDirectory() as td,
                ):
                    root = Path(td)
                    boot = root / "boot.bin"
                    kernel = root / "kernel.bin"
                    image = root / basename
                    boot_payload = bytes(
                        (index * 19 + 1) & 0xFF
                        for index in range(5 * hostbuild.SECTOR_SIZE)
                    )
                    kernel_payload = b"collision-free kernel"
                    boot.write_bytes(boot_payload)
                    kernel.write_bytes(kernel_payload)
                    template = self._disk_template_payload(
                        boot_payload,
                        kernel_payload,
                    )
                    if build_kind == "reuse":
                        self._write_valid_image(image, template)
                        with hostbuild.Fat16Image(
                            image,
                            self.FAT_START_LBA,
                        ) as fat:
                            fat.write_file(
                                "/keep.txt",
                                b"content preserved across composition",
                            )
                        expected = image.read_bytes()
                    else:
                        expected = template + b"\x00" * (
                            self.image_bytes - len(template)
                        )
                    runner = self._checked_disk_runner(
                        manifest=SEED_MANIFEST,
                        boot=boot,
                        kernel=kernel,
                        boot_payload=boot_payload,
                        kernel_payload=kernel_payload,
                        template=template,
                    )
                    output = io.StringIO()

                    with (
                        mock.patch(
                            "tools.hostbuild.run_seed_tool",
                            side_effect=runner,
                        ),
                        contextlib.redirect_stdout(output),
                    ):
                        self._create_image(
                            image=image,
                            boot=boot,
                            kernel=kernel,
                        )

                    data = image.read_bytes()
                    self.assertEqual(data, expected)
                    if build_kind == "reuse":
                        self.assertIn(b"KEEP    TXT", data)
                        self.assertIn(
                            b"content preserved across composition",
                            data,
                        )
                        self.assertIn("Reused", output.getvalue())
                    else:
                        self.assertIn("Created", output.getvalue())

    def test_image_keeps_the_missing_stage_skip_behavior(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            image = root / "cupidos.img"
            missing = root / "optional.wad"
            boot_payload = b"B" * (5 * hostbuild.SECTOR_SIZE)
            kernel_payload = b"kernel"
            boot.write_bytes(boot_payload)
            kernel.write_bytes(kernel_payload)
            template = self._disk_template_payload(
                boot_payload,
                kernel_payload,
            )
            runner = self._checked_disk_runner(
                manifest=SEED_MANIFEST,
                boot=boot,
                kernel=kernel,
                boot_payload=boot_payload,
                kernel_payload=kernel_payload,
                template=template,
            )
            output = io.StringIO()

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=runner,
                ),
                contextlib.redirect_stdout(output),
            ):
                self._create_image(
                    image=image,
                    boot=boot,
                    kernel=kernel,
                    stage_files=(
                        hostbuild.StageFile(missing, "/optional.wad"),
                    ),
                )

            self.assertEqual(
                image.read_bytes(),
                template + b"\x00" * (self.image_bytes - len(template)),
            )
            self.assertIn(
                f"Skipping missing stage file {missing}",
                output.getvalue(),
            )

    def test_image_reuse_preserves_fat_when_optional_stage_is_missing(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            image = root / "cupidos.img"
            missing = root / "optional.wad"
            boot_payload = b"B" * (5 * hostbuild.SECTOR_SIZE)
            kernel_payload = b"kernel"
            boot.write_bytes(boot_payload)
            kernel.write_bytes(kernel_payload)
            template = self._disk_template_payload(
                boot_payload,
                kernel_payload,
            )
            self._write_valid_image(image, template)
            with hostbuild.Fat16Image(
                image,
                self.FAT_START_LBA,
            ) as fat:
                fat.write_file(
                    "/keep.txt",
                    b"persistent FAT content",
                )
            original = image.read_bytes()
            runner = self._checked_disk_runner(
                manifest=SEED_MANIFEST,
                boot=boot,
                kernel=kernel,
                boot_payload=boot_payload,
                kernel_payload=kernel_payload,
                template=template,
            )
            output = io.StringIO()

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=runner,
                ),
                contextlib.redirect_stdout(output),
            ):
                self._create_image(
                    image=image,
                    boot=boot,
                    kernel=kernel,
                    stage_files=(
                        hostbuild.StageFile(missing, "/optional.wad"),
                    ),
                )

            self.assertEqual(image.read_bytes(), original)
            self.assertIn(b"KEEP    TXT", image.read_bytes())
            self.assertIn(b"persistent FAT content", image.read_bytes())
            self.assertIn("Reused", output.getvalue())
            self.assertIn("preserving FAT data", output.getvalue())
            self.assertIn(
                f"Skipping missing stage file {missing}",
                output.getvalue(),
            )

    def test_image_preserves_output_when_checked_cupidobj_fails(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            image = root / "cupidos.img"
            boot.write_bytes(b"B" * (5 * hostbuild.SECTOR_SIZE))
            kernel.write_bytes(b"kernel")
            image.write_bytes(b"existing disk image")

            def run_seed(
                _manifest,
                _working_directory,
                _tool_name,
                arguments,
                *,
                timeout,
            ):
                self.assertEqual(timeout, 60)
                Path(arguments[-1]).write_bytes(b"partial template")
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments],
                    7,
                    "",
                    "disk template denied",
                )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                ),
                self.assertRaisesRegex(
                    hostbuild.DiskImageError,
                    "checked CupidObj failed with status 7: "
                    "disk template denied",
                ),
            ):
                self._create_image(
                    image=image,
                    boot=boot,
                    kernel=kernel,
                )

            self.assertEqual(image.read_bytes(), b"existing disk image")

    def test_image_rejects_checked_template_parity_drift(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            image = root / "cupidos.img"
            boot_payload = b"B" * (5 * hostbuild.SECTOR_SIZE)
            kernel_payload = b"kernel"
            boot.write_bytes(boot_payload)
            kernel.write_bytes(kernel_payload)
            image.write_bytes(b"existing disk image")
            template = bytearray(
                self._disk_template_payload(boot_payload, kernel_payload)
            )
            template[self.FAT_START_LBA * hostbuild.SECTOR_SIZE + 43] ^= 1
            runner = self._checked_disk_runner(
                manifest=SEED_MANIFEST,
                boot=boot,
                kernel=kernel,
                boot_payload=boot_payload,
                kernel_payload=kernel_payload,
                template=bytes(template),
            )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=runner,
                ),
                self.assertRaisesRegex(
                    hostbuild.DiskImageError,
                    "checked CupidObj disk template differs from the "
                    "Python oracle",
                ),
            ):
                self._create_image(
                    image=image,
                    boot=boot,
                    kernel=kernel,
                )

            self.assertEqual(image.read_bytes(), b"existing disk image")

    def test_image_rejects_unsafe_checked_candidates_before_oracle(self):
        def leave_missing(_candidate, _template):
            return None

        def make_directory(candidate, _template):
            candidate.mkdir()

        def write_short(candidate, template):
            candidate.write_bytes(template[:-1])

        def write_long(candidate, template):
            candidate.write_bytes(template + b"\x00")

        cases = (
            ("missing", leave_missing, "cannot be resolved"),
            ("directory", make_directory, "not a regular file"),
            ("short", write_short, "disk template has the wrong size"),
            ("long", write_long, "disk template has the wrong size"),
        )
        for name, prepare_candidate, expected in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                boot = root / "boot.bin"
                kernel = root / "kernel.bin"
                image = root / "cupidos.img"
                boot_payload = b"B" * (5 * hostbuild.SECTOR_SIZE)
                kernel_payload = b"kernel"
                boot.write_bytes(boot_payload)
                kernel.write_bytes(kernel_payload)
                image.write_bytes(b"existing disk image")
                template = self._disk_template_payload(
                    boot_payload,
                    kernel_payload,
                )

                def run_seed(
                    _manifest,
                    _working_directory,
                    _tool_name,
                    arguments,
                    **_kwargs,
                ):
                    prepare_candidate(Path(arguments[-1]), template)
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
                    mock.patch(
                        "tools.hostbuild._write_mbr",
                        side_effect=AssertionError(
                            "Python oracle must not inspect an unsafe "
                            "checked candidate"
                        ),
                    ),
                    self.assertRaisesRegex(
                        hostbuild.DiskImageError,
                        expected,
                    ),
                ):
                    self._create_image(
                        image=image,
                        boot=boot,
                        kernel=kernel,
                    )

                self.assertEqual(image.read_bytes(), b"existing disk image")

    def test_image_rejects_a_symbolic_checked_candidate_before_oracle(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            image = root / "cupidos.img"
            boot_payload = b"B" * (5 * hostbuild.SECTOR_SIZE)
            kernel_payload = b"kernel"
            boot.write_bytes(boot_payload)
            kernel.write_bytes(kernel_payload)
            image.write_bytes(b"existing disk image")
            template = self._disk_template_payload(
                boot_payload,
                kernel_payload,
            )

            def run_seed(
                _manifest,
                _working_directory,
                _tool_name,
                arguments,
                **_kwargs,
            ):
                candidate = Path(arguments[-1])
                target = candidate.parent / "linked-template.bin"
                target.write_bytes(template)
                try:
                    candidate.symlink_to(target)
                except OSError as error:
                    self.skipTest(
                        f"symbolic links are unavailable: {error}"
                    )
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
                mock.patch(
                    "tools.hostbuild._write_mbr",
                    side_effect=AssertionError(
                        "Python oracle must not inspect a linked candidate"
                    ),
                ),
                self.assertRaisesRegex(
                    hostbuild.DiskImageError,
                    "symbolic link|regular disk template",
                ),
            ):
                self._create_image(
                    image=image,
                    boot=boot,
                    kernel=kernel,
                )

            self.assertEqual(image.read_bytes(), b"existing disk image")

    def test_image_preserves_output_when_staging_fails(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            staged = root / "new.bin"
            image = root / "cupidos.img"
            boot_payload = b"B" * (5 * hostbuild.SECTOR_SIZE)
            kernel_payload = b"kernel"
            boot.write_bytes(boot_payload)
            kernel.write_bytes(kernel_payload)
            staged.write_bytes(b"stage me")
            image.write_bytes(b"existing disk image")
            template = self._disk_template_payload(
                boot_payload,
                kernel_payload,
            )
            runner = self._checked_disk_runner(
                manifest=SEED_MANIFEST,
                boot=boot,
                kernel=kernel,
                boot_payload=boot_payload,
                kernel_payload=kernel_payload,
                template=template,
            )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=runner,
                ),
                mock.patch.object(
                    hostbuild.Fat16Image,
                    "write_file",
                    side_effect=OSError("stage denied"),
                ),
                self.assertRaisesRegex(
                    hostbuild.DiskImageError,
                    "stage denied",
                ),
            ):
                self._create_image(
                    image=image,
                    boot=boot,
                    kernel=kernel,
                    stage_files=(
                        hostbuild.StageFile(staged, "/new.bin"),
                    ),
                )

            self.assertEqual(image.read_bytes(), b"existing disk image")

    def test_image_corrupt_fat_chain_rolls_back_reuse_staging(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            staged = root / "replace.bin"
            image = root / "cupidos.img"
            boot_payload = b"B" * (5 * hostbuild.SECTOR_SIZE)
            kernel_payload = b"kernel"
            boot.write_bytes(boot_payload)
            kernel.write_bytes(kernel_payload)
            staged.write_bytes(b"replacement payload")
            template = self._disk_template_payload(
                boot_payload,
                kernel_payload,
            )
            self._write_valid_image(image, template)
            with hostbuild.Fat16Image(
                image,
                self.FAT_START_LBA,
            ) as fat:
                fat.write_file("/replace.bin", b"old payload")
                found = fat._find_entry(
                    None,
                    fat._short_name("replace.bin"),
                )
                self.assertIsNotNone(found)
                entry_index, entries, _ = found
                first_cluster = struct.unpack_from(
                    "<H",
                    entries,
                    entry_index + 26,
                )[0]
                fat._write_fat(
                    first_cluster,
                    fat.cluster_count + 2,
                )
            original = image.read_bytes()
            original_timestamp = image.stat().st_mtime_ns
            runner = self._checked_disk_runner(
                manifest=SEED_MANIFEST,
                boot=boot,
                kernel=kernel,
                boot_payload=boot_payload,
                kernel_payload=kernel_payload,
                template=template,
            )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=runner,
                ),
                self.assertRaisesRegex(
                    hostbuild.DiskImageError,
                    "FAT16 file chain is corrupt",
                ),
            ):
                self._create_image(
                    image=image,
                    boot=boot,
                    kernel=kernel,
                    stage_files=(
                        hostbuild.StageFile(staged, "/replace.bin"),
                    ),
                )

            self.assertEqual(image.read_bytes(), original)
            self.assertEqual(
                image.stat().st_mtime_ns,
                original_timestamp,
            )

    def test_image_preserves_output_when_publication_fails(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            image = root / "cupidos.img"
            boot_payload = b"B" * (5 * hostbuild.SECTOR_SIZE)
            kernel_payload = b"kernel"
            boot.write_bytes(boot_payload)
            kernel.write_bytes(kernel_payload)
            image.write_bytes(b"existing disk image")
            template = self._disk_template_payload(
                boot_payload,
                kernel_payload,
            )
            runner = self._checked_disk_runner(
                manifest=SEED_MANIFEST,
                boot=boot,
                kernel=kernel,
                boot_payload=boot_payload,
                kernel_payload=kernel_payload,
                template=template,
            )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=runner,
                ),
                mock.patch(
                    "tools.hostbuild.os.replace",
                    side_effect=OSError("publication denied"),
                ),
                self.assertRaisesRegex(
                    hostbuild.DiskImageError,
                    "disk image could not be published: .*"
                    "publication denied",
                ),
            ):
                self._create_image(
                    image=image,
                    boot=boot,
                    kernel=kernel,
                )

            self.assertEqual(image.read_bytes(), b"existing disk image")

    def test_image_rejects_a_second_publisher_while_the_lock_is_held(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            image = root / "cupidos.img"
            boot.write_bytes(b"B" * (5 * hostbuild.SECTOR_SIZE))
            kernel.write_bytes(b"kernel")
            image.write_bytes(b"existing disk image")
            original = image.read_bytes()
            original_timestamp = image.stat().st_mtime_ns
            lock = hostbuild._acquire_disk_publication_lock(
                image.resolve()
            )
            try:
                child = subprocess.run(
                    [
                        sys.executable,
                        "-c",
                        (
                            "import sys\n"
                            "from pathlib import Path\n"
                            "from tools import hostbuild\n"
                            "try:\n"
                            "    held = hostbuild."
                            "_acquire_disk_publication_lock(Path(sys.argv[1]))\n"
                            "except hostbuild.DiskImageError as error:\n"
                            "    print(error)\n"
                            "    raise SystemExit(0)\n"
                            "hostbuild._release_disk_publication_lock(held)\n"
                            "raise SystemExit('second process acquired lock')\n"
                        ),
                        str(image.resolve()),
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=30,
                )
                self.assertEqual(child.returncode, 0, child.stderr)
                self.assertIn(
                    "another hostbuild publisher is active",
                    child.stdout,
                )
                with (
                    mock.patch(
                        "tools.hostbuild.run_seed_tool",
                        side_effect=AssertionError(
                            "CupidObj must not run for a second publisher"
                        ),
                    ),
                    self.assertRaisesRegex(
                        hostbuild.DiskImageError,
                        "another hostbuild publisher is active",
                    ),
                ):
                    self._create_image(
                        image=image,
                        boot=boot,
                        kernel=kernel,
                    )
            finally:
                hostbuild._release_disk_publication_lock(lock)

            self.assertEqual(image.read_bytes(), original)
            self.assertEqual(
                image.stat().st_mtime_ns,
                original_timestamp,
            )

    def test_image_rejects_live_input_drift_and_preserves_output(self):
        expected_diagnostics = {
            "manifest": "checked seed manifest changed while authoring",
            "seed image": (
                "checked seed artifact cupidasm\\.elf changed while "
                "authoring"
            ),
            "bootloader": "bootloader input changed while authoring",
            "kernel": "kernel input changed while authoring",
            "stage file": "stage input changed while authoring",
        }
        for changed_input, expected in expected_diagnostics.items():
            with (
                self.subTest(changed_input=changed_input),
                tempfile.TemporaryDirectory() as td,
            ):
                root = Path(td)
                manifest = self._copy_checked_seed(root)
                boot = root / "boot.bin"
                kernel = root / "kernel.bin"
                staged = root / "new.bin"
                image = root / "cupidos.img"
                boot_payload = b"B" * (5 * hostbuild.SECTOR_SIZE)
                kernel_payload = b"kernel"
                boot.write_bytes(boot_payload)
                kernel.write_bytes(kernel_payload)
                staged.write_bytes(b"stage payload")
                image.write_bytes(b"existing disk image")
                template = self._disk_template_payload(
                    boot_payload,
                    kernel_payload,
                )

                def mutate_live_input(_candidate):
                    if changed_input == "manifest":
                        manifest.write_bytes(
                            manifest.read_bytes() + b"\n"
                        )
                    elif changed_input == "seed image":
                        seed_image = manifest.parent / "cupidasm.elf"
                        seed_payload = bytearray(seed_image.read_bytes())
                        seed_payload[-1] ^= 1
                        seed_image.write_bytes(seed_payload)
                    elif changed_input == "bootloader":
                        boot.write_bytes(boot_payload + b"late edit")
                    elif changed_input == "kernel":
                        kernel.write_bytes(kernel_payload + b" late edit")
                    else:
                        staged.write_bytes(b"changed stage payload")

                runner = self._checked_disk_runner(
                    manifest=manifest,
                    boot=boot,
                    kernel=kernel,
                    boot_payload=boot_payload,
                    kernel_payload=kernel_payload,
                    template=template,
                    after_write=mutate_live_input,
                )

                with (
                    mock.patch(
                        "tools.hostbuild.run_seed_tool",
                        side_effect=runner,
                    ),
                    self.assertRaisesRegex(
                        hostbuild.DiskImageError,
                        expected,
                    ),
                ):
                    self._create_image(
                        image=image,
                        boot=boot,
                        kernel=kernel,
                        manifest=manifest,
                        stage_files=(
                            hostbuild.StageFile(staged, "/new.bin"),
                        ),
                    )

                self.assertEqual(
                    image.read_bytes(),
                    b"existing disk image",
                )

    def test_image_rejects_output_drift_without_overwriting_it(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            image = root / "cupidos.img"
            boot_payload = b"B" * (5 * hostbuild.SECTOR_SIZE)
            kernel_payload = b"kernel"
            boot.write_bytes(boot_payload)
            kernel.write_bytes(kernel_payload)
            image.write_bytes(b"existing disk image")
            template = self._disk_template_payload(
                boot_payload,
                kernel_payload,
            )

            def replace_live_output(_candidate):
                image.write_bytes(b"concurrent publisher")

            runner = self._checked_disk_runner(
                manifest=SEED_MANIFEST,
                boot=boot,
                kernel=kernel,
                boot_payload=boot_payload,
                kernel_payload=kernel_payload,
                template=template,
                after_write=replace_live_output,
            )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=runner,
                ),
                self.assertRaisesRegex(
                    hostbuild.DiskImageError,
                    "disk image output changed while authoring the image",
                ),
            ):
                self._create_image(
                    image=image,
                    boot=boot,
                    kernel=kernel,
                )

            self.assertEqual(image.read_bytes(), b"concurrent publisher")

    def test_image_rejects_output_hard_links_to_every_input(self):
        input_names = (
            "manifest",
            "seed image",
            "bootloader",
            "kernel",
            "stage file",
        )
        for input_name in input_names:
            with (
                self.subTest(input_name=input_name),
                tempfile.TemporaryDirectory() as td,
            ):
                root = Path(td)
                manifest = self._copy_checked_seed(root)
                boot = root / "boot.bin"
                kernel = root / "kernel.bin"
                staged = root / "new.bin"
                image = root / "cupidos.img"
                boot.write_bytes(b"B" * (5 * hostbuild.SECTOR_SIZE))
                kernel.write_bytes(b"kernel")
                staged.write_bytes(b"stage payload")
                inputs = {
                    "manifest": manifest,
                    "seed image": manifest.parent / "cupidobj.elf",
                    "bootloader": boot,
                    "kernel": kernel,
                    "stage file": staged,
                }
                aliased_input = inputs[input_name]
                original = aliased_input.read_bytes()
                try:
                    os.link(aliased_input, image)
                except OSError as error:
                    self.skipTest(f"hard links are unavailable: {error}")

                with (
                    mock.patch(
                        "tools.hostbuild.run_seed_tool",
                        side_effect=AssertionError(
                            "CupidObj must not run for an aliased output"
                        ),
                    ),
                    self.assertRaisesRegex(
                        hostbuild.DiskImageError,
                        "disk image output may not replace an input",
                    ),
                ):
                    self._create_image(
                        image=image,
                        boot=boot,
                        kernel=kernel,
                        manifest=manifest,
                        stage_files=(
                            hostbuild.StageFile(staged, "/new.bin"),
                        ),
                        force_format=True,
                    )

                self.assertEqual(image.read_bytes(), original)
                self.assertEqual(aliased_input.read_bytes(), original)

    def test_image_rejects_a_symbolic_output_before_cupidobj(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            target = root / "existing.img"
            image = root / "cupidos.img"
            boot.write_bytes(b"B" * (5 * hostbuild.SECTOR_SIZE))
            kernel.write_bytes(b"kernel")
            target.write_bytes(b"existing disk image")
            try:
                image.symlink_to(target)
            except OSError as error:
                self.skipTest(f"symbolic links are unavailable: {error}")

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=AssertionError(
                        "CupidObj must not run for a linked output"
                    ),
                ),
                self.assertRaisesRegex(
                    hostbuild.DiskImageError,
                    "disk image output may not be a symbolic link",
                ),
            ):
                self._create_image(
                    image=image,
                    boot=boot,
                    kernel=kernel,
                    force_format=True,
                )

            self.assertEqual(target.read_bytes(), b"existing disk image")

    def test_image_validates_all_five_seed_images_before_cupidobj(self):
        for tool_name in (
            "cupidasm.elf",
            "cupidc.elf",
            "cupiddis.elf",
            "cupidld.elf",
            "cupidobj.elf",
        ):
            with (
                self.subTest(tool_name=tool_name),
                tempfile.TemporaryDirectory() as td,
            ):
                root = Path(td)
                manifest = self._copy_checked_seed(root)
                boot = root / "boot.bin"
                kernel = root / "kernel.bin"
                image = root / "cupidos.img"
                boot.write_bytes(b"B" * (5 * hostbuild.SECTOR_SIZE))
                kernel.write_bytes(b"kernel")
                image.write_bytes(b"existing disk image")
                seed_image = manifest.parent / tool_name
                payload = bytearray(seed_image.read_bytes())
                payload[-1] ^= 1
                seed_image.write_bytes(payload)

                with (
                    mock.patch(
                        "tools.hostbuild.run_seed_tool",
                        side_effect=AssertionError(
                            "CupidObj must not run with an invalid seed"
                        ),
                    ),
                    self.assertRaisesRegex(
                        hostbuild.DiskImageError,
                        f"SHA-256 differs for {re.escape(tool_name)}",
                    ),
                ):
                    self._create_image(
                        image=image,
                        boot=boot,
                        kernel=kernel,
                        manifest=manifest,
                    )

                self.assertEqual(
                    image.read_bytes(),
                    b"existing disk image",
                )

    def test_image_validates_the_seed_manifest_before_cupidobj(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = self._copy_checked_seed(root)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            image = root / "cupidos.img"
            boot.write_bytes(b"B" * (5 * hostbuild.SECTOR_SIZE))
            kernel.write_bytes(b"kernel")
            image.write_bytes(b"existing disk image")
            manifest.write_text("{}\n", encoding="ascii", newline="\n")

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=AssertionError(
                        "CupidObj must not run with an invalid manifest"
                    ),
                ),
                self.assertRaisesRegex(
                    hostbuild.DiskImageError,
                    "manifest keys differ",
                ),
            ):
                self._create_image(
                    image=image,
                    boot=boot,
                    kernel=kernel,
                    manifest=manifest,
                )

            self.assertEqual(image.read_bytes(), b"existing disk image")

    def test_image_rejects_kernel_overlap_with_fat_partition(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            image = root / "cupidos.img"
            boot.write_bytes(b"B" * (5 * hostbuild.SECTOR_SIZE))
            kernel.write_bytes(b"K" * (3 * hostbuild.SECTOR_SIZE + 1))

            def run_seed(
                _manifest,
                _working_directory,
                _tool_name,
                arguments,
                **_kwargs,
            ):
                self.assertEqual(
                    Path(arguments[3]).stat().st_size,
                    3 * hostbuild.SECTOR_SIZE + 1,
                )
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments],
                    1,
                    "",
                    f"{arguments[3]} overlaps FAT partition at LBA 8",
                )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                ),
                self.assertRaisesRegex(
                    hostbuild.DiskImageError,
                    "overlaps FAT partition at LBA 8",
                ),
            ):
                self._create_image(
                    image=image,
                    boot=boot,
                    kernel=kernel,
                )

            self.assertFalse(image.exists())

    def test_image_cli_requires_the_checked_seed_manifest(self):
        diagnostic = io.StringIO()
        with (
            contextlib.redirect_stderr(diagnostic),
            self.assertRaises(SystemExit) as raised,
        ):
            hostbuild.main(
                [
                    "image",
                    "--image",
                    "cupidos.img",
                    "--bootloader",
                    "boot.bin",
                    "--kernel",
                    "kernel.bin",
                    "--hdd-mb",
                    str(self.IMAGE_MB),
                    "--fat-start-lba",
                    str(self.FAT_START_LBA),
                ]
            )

        self.assertEqual(raised.exception.code, 2)
        self.assertIn("--seed-manifest", diagnostic.getvalue())
        self.assertIn("required", diagnostic.getvalue())

    def test_image_cli_reports_failures_without_a_traceback(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            image = root / "cupidos.img"
            boot.write_bytes(b"B" * (5 * hostbuild.SECTOR_SIZE))
            kernel.write_bytes(b"kernel")
            image.write_bytes(b"existing disk image")
            diagnostic = io.StringIO()
            failure = subprocess.CompletedProcess(
                ["cupidobj", "disk-template"],
                9,
                "",
                "template failed cleanly",
            )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    return_value=failure,
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "image",
                        "--seed-manifest",
                        str(SEED_MANIFEST),
                        "--image",
                        str(image),
                        "--bootloader",
                        str(boot),
                        "--kernel",
                        str(kernel),
                        "--hdd-mb",
                        str(self.IMAGE_MB),
                        "--fat-start-lba",
                        str(self.FAT_START_LBA),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn("[hostbuild] image failed:", diagnostic.getvalue())
            self.assertIn(
                "checked CupidObj failed with status 9: "
                "template failed cleanly",
                diagnostic.getvalue(),
            )
            self.assertNotIn("Traceback", diagnostic.getvalue())
            self.assertEqual(image.read_bytes(), b"existing disk image")


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
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_checked,
                ),
                mock.patch(
                    "tools.hostbuild._render_iso_image",
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
                    "tools.hostbuild.run_seed_tool",
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
                    "tools.hostbuild.run_seed_tool",
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
                    "tools.hostbuild.run_seed_tool",
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
                    "tools.hostbuild.run_seed_tool",
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
                    "tools.hostbuild.run_seed_tool",
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
                        "tools.hostbuild.run_seed_tool",
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
                    "tools.hostbuild.run_seed_tool",
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
                    "tools.hostbuild.run_seed_tool",
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
                    "tools.hostbuild.run_seed_tool",
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
                    "tools.hostbuild.run_seed_tool",
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
                    "tools.hostbuild.run_seed_tool",
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
            events = []
            prepare_baseline = hostbuild._prepare_baseline_jpeg
            replace = os.replace

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
                self.assertEqual(arguments[0], "wrap-jpeg")
                self.assertEqual(arguments[2:4], ["--identity", str(src)])
                frozen = Path(arguments[1])
                candidate = Path(arguments[-1])
                self.assertEqual(frozen.read_bytes(), BASELINE_JPEG)
                self.assertNotEqual(frozen, src)
                self.assertEqual(frozen.parent, candidate.parent)
                self.assertEqual(candidate.parent.parent, root)
                self.assertTrue(
                    candidate.parent.name.startswith(
                        f".{out.name}.embed-jpeg-"
                    )
                )
                events.append("cupidobj")
                candidate.write_bytes(b"checked object")
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments],
                    0,
                    "",
                    "",
                )

            def run_oracle(frozen, oracle):
                events.append("python-oracle")
                self.assertEqual(frozen.read_bytes(), BASELINE_JPEG)
                prepare_baseline(frozen, oracle)

            def publish(candidate, destination):
                events.append("publish")
                self.assertEqual(Path(candidate).read_bytes(), b"checked object")
                self.assertEqual(Path(destination), out)
                replace(candidate, destination)

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                    create=True,
                ),
                mock.patch(
                    "tools.hostbuild._prepare_baseline_jpeg",
                    side_effect=run_oracle,
                ),
                mock.patch(
                    "tools.hostbuild.os.replace",
                    side_effect=publish,
                ),
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
            self.assertEqual(
                events,
                ["cupidobj", "python-oracle", "publish"],
            )
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
            calls = []
            diagnostic = io.StringIO()

            def run_seed(
                _manifest,
                _working_directory,
                _tool_name,
                arguments,
                **_kwargs,
            ):
                calls.append(arguments)
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments],
                    6,
                    "",
                    "wrap failed\n",
                )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                    create=True,
                ),
                mock.patch(
                    "tools.hostbuild._prepare_baseline_jpeg",
                    side_effect=AssertionError(
                        "Python oracle must not run before CupidObj"
                    ),
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
            self.assertEqual(calls[0][0], "wrap-jpeg")
            self.assertIn(
                "checked CupidObj failed with status 6: wrap failed",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

    def test_embed_jpeg_rejects_manifest_drift_before_publication(self):
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
                **_kwargs,
            ):
                Path(arguments[-1]).write_bytes(b"checked object")
                manifest.write_bytes(b"changed manifest")
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments], 0, "", ""
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
            calls = []

            def run_seed(
                _manifest,
                _working_directory,
                _tool_name,
                arguments,
                **_kwargs,
            ):
                calls.append(arguments)
                self.assertEqual(arguments[0], "wrap-jpeg")
                self.assertEqual(
                    Path(arguments[1]).read_bytes(), PROGRESSIVE_JPEG
                )
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments],
                    1,
                    "",
                    "unsupported progressive JPEG frame; "
                    "check in a baseline SOF0/SOF1 asset\n",
                )

            with (
                mock.patch(
                    "tools.hostbuild.shutil.which",
                    side_effect=AssertionError("host converter lookup"),
                ),
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                ),
                mock.patch(
                    "tools.hostbuild._prepare_baseline_jpeg",
                    side_effect=AssertionError(
                        "Python oracle must not run before CupidObj"
                    ),
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
            self.assertEqual(len(calls), 1)
            self.assertIn(
                "unsupported progressive JPEG frame",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

    def test_embed_jpeg_uses_cupidobj_to_reject_a_malformed_frame(self):
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
            calls = []

            def run_seed(
                _manifest,
                _working_directory,
                _tool_name,
                arguments,
                **_kwargs,
            ):
                calls.append(arguments)
                self.assertEqual(arguments[0], "wrap-jpeg")
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments],
                    1,
                    "",
                    "JPEG frame header has an invalid component table\n",
                )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                ),
                mock.patch(
                    "tools.hostbuild._prepare_baseline_jpeg",
                    side_effect=AssertionError(
                        "Python oracle must not run before CupidObj"
                    ),
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
            self.assertEqual(len(calls), 1)
            self.assertIn(
                "JPEG frame header has an invalid component table",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

    def test_embed_jpeg_rejects_python_oracle_disagreement(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(BASELINE_JPEG)
            out.write_bytes(b"existing object")
            diagnostic = io.StringIO()
            calls = []

            def run_seed(
                _manifest,
                _working_directory,
                _tool_name,
                arguments,
                **_kwargs,
            ):
                calls.append(arguments)
                Path(arguments[-1]).write_bytes(b"checked object")
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments], 0, "", ""
                )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                ),
                mock.patch(
                    "tools.hostbuild._prepare_baseline_jpeg",
                    side_effect=hostbuild.EmbedJpegError(
                        "Python rejected the frame"
                    ),
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
            self.assertEqual(len(calls), 1)
            self.assertIn(
                "checked CupidObj JPEG acceptance differs from the "
                "Python oracle: Python rejected the frame",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

    def test_embed_jpeg_reports_python_oracle_write_failure(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(BASELINE_JPEG)
            out.write_bytes(b"existing object")
            diagnostic = io.StringIO()
            write_bytes = Path.write_bytes

            def run_seed(
                _manifest,
                _working_directory,
                _tool_name,
                arguments,
                **_kwargs,
            ):
                Path(arguments[-1]).write_bytes(b"checked object")
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments], 0, "", ""
                )

            def guarded_write(path, payload):
                if path.name == "asset.python-oracle.jpg":
                    raise OSError("oracle copy denied")
                return write_bytes(path, payload)

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                ),
                mock.patch(
                    "pathlib.Path.write_bytes",
                    new=guarded_write,
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
                "Python JPEG oracle could not write its private copy",
                diagnostic.getvalue(),
            )
            self.assertNotIn(
                "JPEG acceptance differs",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

    def test_embed_jpeg_rejects_python_oracle_byte_drift(self):
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
                **_kwargs,
            ):
                Path(arguments[-1]).write_bytes(b"checked object")
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments], 0, "", ""
                )

            def alter_oracle(_frozen, oracle):
                oracle.write_bytes(BASELINE_JPEG + b"\x00")

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                ),
                mock.patch(
                    "tools.hostbuild._prepare_baseline_jpeg",
                    side_effect=alter_oracle,
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
                "Python JPEG oracle changed the frozen input bytes",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

    def test_embed_jpeg_rejects_missing_checked_candidate(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(BASELINE_JPEG)
            out.write_bytes(b"existing object")
            diagnostic = io.StringIO()
            success = subprocess.CompletedProcess(
                ["cupidobj", "wrap-jpeg"], 0, "", ""
            )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    return_value=success,
                ),
                mock.patch(
                    "tools.hostbuild._prepare_baseline_jpeg",
                    side_effect=AssertionError(
                        "Python oracle must not inspect a missing candidate"
                    ),
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
                "checked CupidObj reported success without a regular object",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

    def test_embed_jpeg_rejects_a_symbolic_checked_candidate(self):
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
                **_kwargs,
            ):
                candidate = Path(arguments[-1])
                target = candidate.parent / "candidate-target.o"
                target.write_bytes(b"checked object")
                try:
                    candidate.symlink_to(target)
                except OSError as error:
                    self.skipTest(
                        f"symbolic links are unavailable: {error}"
                    )
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments], 0, "", ""
                )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                ),
                mock.patch(
                    "tools.hostbuild._prepare_baseline_jpeg",
                    side_effect=AssertionError(
                        "Python oracle must not inspect a linked candidate"
                    ),
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
                "checked CupidObj reported success without a regular object",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

    def test_embed_jpeg_rejects_unsafe_output_paths_before_cupidobj(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            directory = root / "directory.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(BASELINE_JPEG)
            directory.mkdir()

            cases = (
                (src, "may not replace an input"),
                (manifest, "may not replace an input"),
                (directory, "embedded JPEG output is not a file"),
            )
            for output, expected in cases:
                with self.subTest(output=output.name):
                    diagnostic = io.StringIO()
                    with (
                        mock.patch(
                            "tools.hostbuild.run_seed_tool",
                            side_effect=AssertionError(
                                "CupidObj must not run for an unsafe path"
                            ),
                        ),
                        contextlib.redirect_stderr(diagnostic),
                    ):
                        status = hostbuild.main(
                            [
                                "embed-jpeg",
                                "--seed-manifest",
                                str(manifest),
                                str(src),
                                str(output),
                            ]
                        )
                    self.assertEqual(status, 1)
                    self.assertIn(expected, diagnostic.getvalue())

    def test_embed_jpeg_rejects_missing_and_non_file_inputs_before_cupidobj(
        self,
    ):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            missing = root / "missing.jpg"
            directory = root / "directory.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            directory.mkdir()

            cases = (
                (missing, "checked JPEG path cannot be resolved"),
                (directory, "JPEG input is not a file"),
            )
            for source, expected in cases:
                with self.subTest(source=source.name):
                    out.write_bytes(b"existing object")
                    diagnostic = io.StringIO()
                    with (
                        mock.patch(
                            "tools.hostbuild.run_seed_tool",
                            side_effect=AssertionError(
                                "CupidObj must not run for an unsafe input"
                            ),
                        ),
                        contextlib.redirect_stderr(diagnostic),
                    ):
                        status = hostbuild.main(
                            [
                                "embed-jpeg",
                                "--seed-manifest",
                                str(manifest),
                                str(source),
                                str(out),
                            ]
                        )
                    self.assertEqual(status, 1)
                    self.assertIn(expected, diagnostic.getvalue())
                    self.assertEqual(
                        out.read_bytes(), b"existing object"
                    )

    def test_embed_jpeg_rejects_a_symbolic_output_before_cupidobj(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            target = root / "existing.o"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(BASELINE_JPEG)
            target.write_bytes(b"existing object")
            try:
                out.symlink_to(target)
            except OSError as error:
                self.skipTest(f"symbolic links are unavailable: {error}")
            diagnostic = io.StringIO()

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=AssertionError(
                        "CupidObj must not run for a linked output"
                    ),
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
                "embedded JPEG output may not be a symlink",
                diagnostic.getvalue(),
            )
            self.assertEqual(target.read_bytes(), b"existing object")

    def test_embed_jpeg_preserves_output_after_publication_failure(self):
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
                **_kwargs,
            ):
                self.assertEqual(arguments[0], "wrap-jpeg")
                Path(arguments[-1]).write_bytes(b"checked object")
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments], 0, "", ""
                )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                ),
                mock.patch(
                    "tools.hostbuild.os.replace",
                    side_effect=OSError("publication denied"),
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
                "embedded JPEG object could not be published: "
                "publication denied",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")


if __name__ == "__main__":
    unittest.main()
