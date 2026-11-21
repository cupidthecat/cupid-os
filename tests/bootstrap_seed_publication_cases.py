## @file
#  @brief Check generated assets, images, and installation sources.
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from tools import hostbuild
from tools.bootstrap_toolchain import ToolRunner, _profile_snapshot_payload, freeze_seed_inputs

from tests.bootstrap_seed_suite_fixtures import (
    BOOTSTRAP_TOOL,
    REPO_ROOT,
    SEED_MANIFEST,
)


class BootstrapSeedPublicationCases:
    def test_checked_seed_run_forwards_cupidasm_help(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        result = subprocess.run(
            [
                sys.executable,
                str(BOOTSTRAP_TOOL),
                "run",
                "--manifest",
                str(SEED_MANIFEST),
                "--root",
                str(REPO_ROOT),
                "--tool",
                "cupidasm",
                "--",
                "--help",
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=60,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("usage:", result.stdout.casefold())
        self.assertIn("cupidasm", result.stdout.casefold())
        self.assertEqual(result.stderr, "")

    def test_checked_seed_wraps_jpeg_and_preserves_failed_output(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        baseline_jpeg = (
            b"\xff\xd8"
            b"\xff\xc0\x00\x0b\x08\x00\x01\x00\x01\x01\x01\x11\x00"
            b"\xff\xda\x00\x08\x01\x01\x00\x00\x3f\x00"
            b"\xff\xd9"
        )
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-jpeg-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "asset.jpg"
            progressive_source = root / "progressive.jpg"
            wrapped_output = root / "wrapped.o"
            jpeg_output = root / "jpeg.o"
            failed_output = root / "progressive.o"
            source.write_bytes(baseline_jpeg)
            progressive_source.write_bytes(
                baseline_jpeg[:3] + b"\xc2" + baseline_jpeg[4:]
            )
            failed_output.write_bytes(b"sentinel")
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)
            object_options = [
                "--stem",
                "fixed_point_asset",
                "--section",
                ".rodata",
                "--readonly",
            ]

            wrapped = runner.run(
                frozen.tools["cupidobj"],
                [
                    "wrap",
                    source,
                    *object_options,
                    "-o",
                    wrapped_output,
                ],
                60,
            )
            self.assertEqual(wrapped.returncode, 0, wrapped.stderr)
            self.assertEqual(wrapped.stdout, "")
            self.assertEqual(wrapped.stderr, "")

            checked = runner.run(
                frozen.tools["cupidobj"],
                [
                    "wrap-jpeg",
                    source,
                    *object_options,
                    "-o",
                    jpeg_output,
                ],
                60,
            )
            self.assertEqual(checked.returncode, 0, checked.stderr)
            self.assertEqual(checked.stdout, "")
            self.assertEqual(checked.stderr, "")
            self.assertEqual(
                jpeg_output.read_bytes(), wrapped_output.read_bytes()
            )
            self.assertEqual(
                hashlib.sha256(jpeg_output.read_bytes()).hexdigest(),
                "a4950b4f13759a63540da33f08b584e804b6fb4f98afaa97a82e3d0a9191c35a",
            )

            rejected = runner.run(
                frozen.tools["cupidobj"],
                [
                    "wrap-jpeg",
                    progressive_source,
                    *object_options,
                    "-o",
                    failed_output,
                ],
                60,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn(
                "unsupported progressive JPEG frame; "
                "check in a baseline SOF0/SOF1 asset",
                rejected.stderr,
            )
            self.assertEqual(failed_output.read_bytes(), b"sentinel")

    def test_checked_seed_builds_disk_template_and_preserves_failed_output(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-disk-template-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            overlapping_kernel = root / "overlapping-kernel.bin"
            output = root / "template.bin"
            failed_output = root / "failed-template.bin"
            boot.write_bytes(
                bytes((index * 37 + 11) & 0xFF for index in range(5 * 512))
            )
            kernel.write_bytes(b"CUPID-OS")
            overlapping_kernel.write_bytes(b"K" * (3 * 512 + 1))
            failed_output.write_bytes(b"sentinel")
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)

            generated = runner.run(
                frozen.tools["cupidobj"],
                [
                    "disk-template",
                    boot,
                    "--kernel",
                    kernel,
                    "--image-sectors",
                    "4208",
                    "--fat-start-lba",
                    "8",
                    "-o",
                    output,
                ],
                60,
            )
            self.assertEqual(generated.returncode, 0, generated.stderr)
            self.assertEqual(generated.stdout, "")
            self.assertEqual(generated.stderr, "")
            template = output.read_bytes()
            self.assertEqual(len(template), 38400)
            self.assertEqual(
                hashlib.sha256(template).hexdigest(),
                "a1784fde1833c6cd24f49dff105ff8a70de5b9e619dd8883b4d92d597f241501",
            )

            rejected = runner.run(
                frozen.tools["cupidobj"],
                [
                    "disk-template",
                    boot,
                    "--kernel",
                    overlapping_kernel,
                    "--image-sectors",
                    "4208",
                    "--fat-start-lba",
                    "8",
                    "-o",
                    failed_output,
                ],
                60,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn(
                "overlaps FAT partition at LBA 8",
                rejected.stderr,
            )
            self.assertEqual(failed_output.read_bytes(), b"sentinel")

    def test_checked_seed_builds_iso_fixture_and_preserves_failed_output(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-iso-fixture-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            fixtures = REPO_ROOT / "test_iso" / "fixtures"
            manifest = REPO_ROOT / "test_iso" / "fixtures.manifest"
            output = root / "hello.iso"
            failed_manifest = root / "failed.manifest"
            failed_source = root / "payload.bin"
            failed_output = root / "failed.iso"
            failed_manifest.write_text(
                "lost/payload.bin\n", encoding="ascii", newline="\n"
            )
            failed_source.write_bytes(b"payload")
            failed_output.write_bytes(b"sentinel")
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)

            generated = runner.run(
                frozen.tools["cupidobj"],
                [
                    "iso-fixture",
                    manifest,
                    "--file",
                    "big.bin",
                    fixtures / "big.bin",
                    "--file",
                    "gen_big.sh",
                    fixtures / "gen_big.sh",
                    "--file",
                    "jpeg_baseline_8x8.jpg",
                    fixtures / "jpeg_baseline_8x8.jpg",
                    "--file",
                    "long_named_file.txt",
                    fixtures / "long_named_file.txt",
                    "--file",
                    "readme.txt",
                    fixtures / "readme.txt",
                    "--directory",
                    "sub",
                    "--file",
                    "sub/nested.txt",
                    fixtures / "sub" / "nested.txt",
                    "-o",
                    output,
                ],
                60,
            )
            self.assertEqual(generated.returncode, 0, generated.stderr)
            self.assertEqual(generated.stdout, "")
            self.assertEqual(generated.stderr, "")
            image = output.read_bytes()
            self.assertEqual(len(image), 61440)
            self.assertEqual(
                hashlib.sha256(image).hexdigest(),
                "40359c1cec72219f21e87ce71b31e621209036042440e1b38c5e59de157e0fb6",
            )
            self.assertEqual(
                image,
                (REPO_ROOT / "test_iso" / "hello.iso").read_bytes(),
            )

            rejected = runner.run(
                frozen.tools["cupidobj"],
                [
                    "iso-fixture",
                    failed_manifest,
                    "--file",
                    "lost/payload.bin",
                    failed_source,
                    "-o",
                    failed_output,
                ],
                60,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn("directory parent", rejected.stderr)
            self.assertEqual(failed_output.read_bytes(), b"sentinel")

    def test_checked_seed_builds_profile_manifest_and_preserves_failed_output(
        self,
    ):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-profile-manifest-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            snapshot = root / "profile.snapshot"
            output = root / "profile.json"
            failed_snapshot = root / "failed.snapshot"
            failed_output = root / "failed.json"
            schema = "cupid.profile-inputs.v1"
            inputs = (
                ("headers/empty.h", b""),
                ("headers/repeated.h", bytes(range(129))),
            )
            profiles = (
                (
                    "profile-tree",
                    tuple(path for path, _contents in inputs),
                    ("sources/tree.cc",),
                ),
                (
                    "profile-compat",
                    ("headers/repeated.h",),
                    ("sources/compat.cc",),
                ),
            )
            snapshot.write_bytes(
                _profile_snapshot_payload(schema, profiles, inputs)
            )
            failed_snapshot.write_bytes(
                _profile_snapshot_payload(
                    schema,
                    (
                        (
                            "profile-tree",
                            ("../unsafe.h",),
                            ("sources/tree.cc",),
                        ),
                    ),
                    (("../unsafe.h", b"unsafe"),),
                )
            )
            failed_output.write_bytes(b"sentinel")
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)

            generated = runner.run(
                frozen.tools["cupidobj"],
                ["profile-manifest", snapshot, "-o", output],
                60,
            )
            self.assertEqual(generated.returncode, 0, generated.stderr)
            self.assertEqual(generated.stdout, "")
            self.assertEqual(generated.stderr, "")
            expected = (
                json.dumps(
                    {
                        "schema": schema,
                        "profiles": {
                            name: sorted(headers)
                            for name, headers, _sources in profiles
                        },
                        "sources": {
                            name: sorted(sources)
                            for name, _headers, sources in profiles
                        },
                        "inputs": [
                            {
                                "path": path,
                                "bytes": len(contents),
                                "sha256": hashlib.sha256(
                                    contents
                                ).hexdigest(),
                            }
                            for path, contents in sorted(inputs)
                        ],
                    },
                    indent=2,
                    sort_keys=True,
                )
                + "\n"
            ).encode("ascii")
            self.assertEqual(output.read_bytes(), expected)

            rejected = runner.run(
                frozen.tools["cupidobj"],
                [
                    "profile-manifest",
                    failed_snapshot,
                    "-o",
                    failed_output,
                ],
                60,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn("repository path is invalid", rejected.stderr)
            self.assertEqual(failed_output.read_bytes(), b"sentinel")

    def test_checked_seed_generates_canonical_install_sources(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        expected_bin = (
            "/* Auto-generated -- do not edit. */\n"
            "/* Lists all embedded CupidC programs from bin/ directory */\n"
            '#include "ramfs.h"\n'
            '#include "types.h"\n'
            '#include "../drivers/serial.h"\n'
            "extern const char _binary_bin_hello_cc_start[];\n"
            "extern const char _binary_bin_hello_cc_end[];\n"
            "void install_bin_programs(void *fs_private);\n"
            "void install_bin_programs(void *fs_private) {\n"
            "    { uint32_t sz = (uint32_t)(_binary_bin_hello_cc_end - "
            "_binary_bin_hello_cc_start); ramfs_add_file(fs_private, "
            '"bin/hello.cc", _binary_bin_hello_cc_start, sz); '
            'serial_printf("[kernel] Installed /bin/hello.cc (%u bytes)'
            '\\n", sz); }\n'
            "}\n"
        ).encode("utf-8")
        expected_docs = (
            "/* Auto-generated -- do not edit. */\n"
            "/* Lists all embedded CupidDoc files from cupidos-txt/ "
            "directory */\n"
            '#include "homefs.h"\n'
            '#include "ramfs.h"\n'
            '#include "types.h"\n'
            '#include "vfs.h"\n'
            '#include "../drivers/serial.h"\n'
            "extern const char "
            "_binary_cupidos_txt_00INDEX_CTXT_start[];\n"
            "extern const char _binary_cupidos_txt_00INDEX_CTXT_end[];\n"
            "static void install_home_asset(const char *path, const char "
            "*data, uint32_t size) {\n"
            "    int fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);\n"
            '    if (fd < 0) { serial_printf("[kernel] Failed to open %s '
            '(%d)\\n", path, fd); return; }\n'
            "    uint32_t off = 0;\n"
            "    while (off < size) {\n"
            "        int n = vfs_write(fd, data + off, size - off);\n"
            "        if (n <= 0) break;\n"
            "        off += (uint32_t)n;\n"
            "    }\n"
            "    vfs_close(fd);\n"
            '    serial_printf("[kernel] Installed %s (%u bytes)\\n", '
            "path, off);\n"
            "}\n"
            "void install_docs_programs(void *fs_private);\n"
            "void install_docs_programs(void *fs_private) {\n"
            "    { uint32_t sz = (uint32_t)("
            "_binary_cupidos_txt_00INDEX_CTXT_end - "
            "_binary_cupidos_txt_00INDEX_CTXT_start); "
            'ramfs_add_file(fs_private, "docs/00INDEX.ctxt", '
            "_binary_cupidos_txt_00INDEX_CTXT_start, sz); "
            'serial_printf("[kernel] Installed /docs/00INDEX.ctxt '
            '(%u bytes)\\n", sz); }\n'
            "    homefs_seed_begin();\n"
            "    homefs_seed_end();\n"
            "}\n"
        ).encode("utf-8")
        expected_demos = (
            "/* Auto-generated -- do not edit. */\n"
            "/* Lists all embedded CupidASM demos from demos/ directory */\n"
            '#include "ramfs.h"\n'
            '#include "types.h"\n'
            '#include "../drivers/serial.h"\n'
            "extern const char _binary_demos_hello_asm_start[];\n"
            "extern const char _binary_demos_hello_asm_end[];\n"
            "void install_demo_programs(void *fs_private);\n"
            "void install_demo_programs(void *fs_private) {\n"
            "    { uint32_t sz = (uint32_t)(_binary_demos_hello_asm_end - "
            "_binary_demos_hello_asm_start); ramfs_add_file(fs_private, "
            '"demos/hello.asm", _binary_demos_hello_asm_start, sz); '
            'serial_printf("[kernel] Installed /demos/hello.asm (%u bytes)'
            '\\n", sz); ramfs_add_file(fs_private, '
            '"docs/demos/hello.asm", _binary_demos_hello_asm_start, sz); '
            'serial_printf("[kernel] Installed /docs/demos/hello.asm '
            '(%u bytes)\\n", sz); }\n'
            "}\n"
        ).encode("utf-8")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-install-source-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            (root / "bin").mkdir()
            (root / "cupidos-txt").mkdir()
            (root / "demos").mkdir()
            (root / "bin" / "hello.cc").write_text(
                "int main(void) { return 0; }\n",
                encoding="utf-8",
                newline="\n",
            )
            (root / "cupidos-txt" / "00INDEX.CTXT").write_text(
                "Index\n", encoding="utf-8", newline="\n"
            )
            (root / "demos" / "hello.asm").write_text(
                "ret\n", encoding="utf-8", newline="\n"
            )
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(root)
            cases = (
                (
                    "bin",
                    ["--bin", "bin/hello.cc"],
                    expected_bin,
                ),
                (
                    "docs",
                    ["--ctxt", "cupidos-txt/00INDEX.CTXT"],
                    expected_docs,
                ),
                (
                    "demos",
                    ["--demos", "demos/hello.asm"],
                    expected_demos,
                ),
            )
            for mode, arguments, expected in cases:
                with self.subTest(mode=mode):
                    output = root / f"{mode}-install.cc"
                    result = runner.run(
                        frozen.tools["cupidobj"],
                        [
                            "install-source",
                            mode,
                            *arguments,
                            "-o",
                            output,
                        ],
                        60,
                    )
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout, "")
                    self.assertEqual(result.stderr, "")
                    self.assertEqual(output.read_bytes(), expected)

            sentinel = root / "invalid-install.cc"
            sentinel.write_bytes(b"sentinel")
            rejected = runner.run(
                frozen.tools["cupidobj"],
                [
                    "install-source",
                    "demos",
                    "--demos",
                    "bin/hello.cc",
                    "-o",
                    sentinel,
                ],
                60,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn("must match demos/NAME.asm", rejected.stderr)
            self.assertEqual(sentinel.read_bytes(), b"sentinel")

    def test_checked_seed_enforces_install_request_bounds_order_and_symbols(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-install-contract-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            (root / "bin" / "browser").mkdir(parents=True)
            (root / "cupidos-txt").mkdir()
            for name in (
                "first.png",
                "second.jpeg",
                "third.bmp",
                "fourth.jpg",
                "fifth.bmp",
            ):
                (root / name).write_bytes(b"asset")
            boundary_paths = [
                f"bin/program_{index}.cc" for index in range(513)
            ]
            for relative in boundary_paths:
                (root / relative).write_text(
                    "int main(void) { return 0; }\n",
                    encoding="utf-8",
                    newline="\n",
                )
            for relative in (
                "bin/browser_alpha.cc",
                "bin/browser/alpha.cc",
                "cupidos-txt/a-b.CTXT",
                "cupidos-txt/a_b.CTXT",
                "a-b.bmp",
                "a_b.bmp",
                "shared.bmp",
            ):
                path = root / relative
                path.write_bytes(b"fixture")
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(root)

            ordered = root / "ordered-home-install.cc"
            result = runner.run(
                frozen.tools["cupidobj"],
                [
                    "install-source",
                    "docs",
                    "--home-assets",
                    "first.png",
                    "second.jpeg",
                    "third.bmp",
                    "fourth.jpg",
                    "fifth.bmp",
                    "-o",
                    ordered,
                ],
                60,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "")
            self.assertEqual(result.stderr, "")
            output = ordered.read_text(encoding="utf-8")
            entries = (
                'install_home_asset("/home/first.png"',
                'install_home_asset("/home/second.jpeg"',
                'install_home_asset("/home/third.bmp"',
                'install_home_asset("/home/fourth.jpg"',
                'install_home_asset("/home/fifth.bmp"',
            )
            positions = [output.index(entry) for entry in entries]
            self.assertEqual(positions, sorted(positions))

            boundary = root / "boundary-install.cc"
            accepted = runner.run(
                frozen.tools["cupidobj"],
                [
                    "install-source",
                    "bin",
                    "--bin",
                    *boundary_paths[:512],
                    "-o",
                    boundary,
                ],
                60,
            )
            self.assertEqual(accepted.returncode, 0, accepted.stderr)
            self.assertEqual(accepted.stdout, "")
            self.assertEqual(accepted.stderr, "")
            self.assertGreater(boundary.stat().st_size, 0)

            alias = root / "shared-alias-install.cc"
            shared = runner.run(
                frozen.tools["cupidobj"],
                [
                    "install-source",
                    "docs",
                    "--doc-assets",
                    "shared.bmp",
                    "--home-assets",
                    "shared.bmp",
                    "-o",
                    alias,
                ],
                60,
            )
            self.assertEqual(shared.returncode, 0, shared.stderr)
            self.assertEqual(shared.stdout, "")
            self.assertEqual(shared.stderr, "")
            self.assertGreater(alias.stat().st_size, 0)

            collision_cases = (
                (
                    "bin",
                    [
                        "--bin",
                        "bin/browser_alpha.cc",
                        "--browser",
                        "bin/browser/alpha.cc",
                    ],
                ),
                (
                    "docs",
                    [
                        "--ctxt",
                        "cupidos-txt/a-b.CTXT",
                        "cupidos-txt/a_b.CTXT",
                    ],
                ),
                (
                    "docs",
                    [
                        "--doc-assets",
                        "a-b.bmp",
                        "--home-assets",
                        "a_b.bmp",
                    ],
                ),
            )
            collision_output = root / "collision-install.cc"
            for mode, arguments in collision_cases:
                with self.subTest(mode=mode, arguments=arguments):
                    collision_output.write_bytes(b"sentinel")
                    collision = runner.run(
                        frozen.tools["cupidobj"],
                        [
                            "install-source",
                            mode,
                            *arguments,
                            "-o",
                            collision_output,
                        ],
                        60,
                    )
                    self.assertEqual(collision.returncode, 1)
                    self.assertEqual(collision.stdout, "")
                    self.assertIn("same binary symbol", collision.stderr)
                    self.assertEqual(
                        collision_output.read_bytes(), b"sentinel"
                    )

            sentinel = root / "oversized-install.cc"
            sentinel.write_bytes(b"sentinel")
            rejected = runner.run(
                frozen.tools["cupidobj"],
                [
                    "install-source",
                    "bin",
                    "--bin",
                    *boundary_paths,
                    "-o",
                    sentinel,
                ],
                60,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn("exceeds 512 paths", rejected.stderr)
            self.assertEqual(sentinel.read_bytes(), b"sentinel")

    def test_checked_seed_generates_kernel_symbol_source_transactionally(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-ksyms-source-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            symbols = root / "kernel.symbols"
            output = root / "ksyms_data.cc"
            symbol_text = (
                "00102000 T second\n"
                "00101000 t first\n"
                "00101000 W duplicate\n"
                "         U unresolved\n"
                "00103000 D data_only\n"
            )
            symbols.write_text(symbol_text, encoding="ascii", newline="\n")
            expected = hostbuild._render_ksyms_source(
                hostbuild.build_ksyms_blob(
                    hostbuild._parse_nm_symbols(symbol_text)
                )
            )
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(root)

            generated = runner.run(
                frozen.tools["cupidobj"],
                ["ksyms-source", str(symbols), "-o", str(output)],
                60,
            )
            self.assertEqual(generated.returncode, 0, generated.stderr)
            self.assertEqual(generated.stdout, "")
            self.assertEqual(generated.stderr, "")
            self.assertEqual(output.read_bytes(), expected)

            symbols.write_text(
                "00101000 T valid\nnot-an-address T broken\n",
                encoding="ascii",
                newline="\n",
            )
            output.write_bytes(b"existing generated source")
            rejected = runner.run(
                frozen.tools["cupidobj"],
                ["ksyms-source", str(symbols), "-o", str(output)],
                60,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn(":2:0: error CT8000002:", rejected.stderr)
            self.assertEqual(output.read_bytes(), b"existing generated source")
