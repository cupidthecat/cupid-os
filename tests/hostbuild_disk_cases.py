## @file
#  @brief Check FAT16 creation, staging, and disk publication.
import contextlib
import io
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from unittest import mock

from tests.hostbuild_fixtures import REPO_ROOT, SEED_MANIFEST
from tools import hostbuild


class HostBuildDiskCases:
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

    def test_image_validates_all_six_seed_images_before_cupidobj(self):
        for tool_name in (
            "cupidasm.elf",
            "cupidc.elf",
            "cupiddis.elf",
            "cupidld.elf",
            "cupidobj.elf",
            "cupidbuild.elf",
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
            manifest.write_text(
                '{"schema":"cupid.bootstrap-seed.v2"}\n',
                encoding="ascii",
                newline="\n",
            )

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
