## @file
#  @brief Check raw CupidDis decoding, mode ranges and source control edges.
import hashlib
import subprocess
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


class RawDisassemblyTests:
    def test_cli_raw_mode_requires_explicit_mode_and_base(self):
        missing = subprocess.run(
            [str(self.cli_path), "--raw", str(self.raw_path)],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(missing.returncode, 2)
        self.assertIn("usage: cupiddis", missing.stderr)
        decoded = subprocess.run(
            [
                str(self.cli_path),
                "--raw",
                "--mode=16",
                "--base",
                "0x7c00",
                str(self.raw_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(decoded.returncode, 0, decoded.stderr)
        self.assertIn("00007C00", decoded.stdout)
        self.assertIn("mov ax, 0x1234", decoded.stdout)

    def test_cupidasm_source_selectors_round_trip_through_cupiddis(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "shared-x86-selectors.asm"
            image = root / "shared-x86-selectors.bin"
            source.write_text(
                "BITS 32\n"
                "    shrd eax, edi, cl\n"
                "    nop word [eax]\n"
                "    setc dl\n"
                "    iretd\n"
                "    pushad\n"
                "    popad\n"
                "    pushfd\n"
                "    popfd\n",
                encoding="utf-8",
            )
            assembled = subprocess.run(
                [
                    str(self.asm_path),
                    "-f",
                    "bin",
                    str(source),
                    "-o",
                    str(image),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(assembled.returncode, 0, assembled.stderr)
            self.assertEqual(
                image.read_bytes(),
                bytes.fromhex(
                    "0f ad f8 66 0f 1f 00 0f 92 c2 cf 60 61 9c 9d"
                ),
            )

            command = [
                str(self.cli_path),
                "--raw",
                "--mode=32",
                "--base=0x2000",
                str(image),
            ]
            decoded = subprocess.run(
                command, cwd=REPO_ROOT, text=True, capture_output=True
            )
            repeated = subprocess.run(
                command, cwd=REPO_ROOT, text=True, capture_output=True
            )
            checked = subprocess.run(
                [
                    str(self.cli_path),
                    "--require-known",
                    "--raw",
                    "--mode=32",
                    "--base=0x2000",
                    str(image),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(decoded.returncode, 0, decoded.stderr)
            self.assertEqual(repeated.returncode, 0, repeated.stderr)
            self.assertEqual(checked.returncode, 0, checked.stderr)
            self.assertEqual(decoded.stdout, repeated.stdout)
            self.assertEqual(checked.stdout, "")
            self.assertIn("shrd eax, edi, cl", decoded.stdout)
            self.assertIn("nop word [eax]", decoded.stdout)
            self.assertIn("setb dl", decoded.stdout)
            self.assertIn("iret", decoded.stdout)
            self.assertIn("pusha", decoded.stdout)
            self.assertIn("popa", decoded.stdout)
            self.assertIn("pushf", decoded.stdout)
            self.assertIn("popf", decoded.stdout)

    def test_cli_decodes_active_ctool_double_precision_right_shifts(self):
        decoded = subprocess.run(
            [
                str(self.cli_path),
                "--raw",
                "--mode=32",
                "--base=0x1790",
                str(self.shrd_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(decoded.returncode, 0, decoded.stderr)
        self.assertIn(
            "00001790:  0F AD F8  shrd eax, edi, cl", decoded.stdout
        )
        self.assertIn(
            "00001793:  0F AD F8  shrd eax, edi, cl", decoded.stdout
        )
        self.assertIn("00001796:  C3  ret", decoded.stdout)
        self.assertNotIn("db 0x0F", decoded.stdout)
        self.assertNotIn("clc", decoded.stdout)

    def test_cli_decodes_private_cupidc_parity_setcc_sequences(self):
        decoded = subprocess.run(
            [
                str(self.cli_path),
                "--raw",
                "--mode=32",
                "--base=0x1800",
                str(self.parity_setcc_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(decoded.returncode, 0, decoded.stderr)
        self.assertIn("00001803:  0F 9B C2  setnp dl", decoded.stdout)
        self.assertIn("00001806:  20 D0  and al, dl", decoded.stdout)
        self.assertIn("0000180E:  0F 9A C2  setp dl", decoded.stdout)
        self.assertIn("00001811:  08 D0  or al, dl", decoded.stdout)
        self.assertIn(
            "00001813:  0F B6 C0  movzx eax, al", decoded.stdout
        )
        self.assertIn("00001816:  C3  ret", decoded.stdout)
        self.assertNotIn("db 0x0F", decoded.stdout)

    def test_cli_raw_mode_changes_decode_one_flat_image(self):
        decoded = subprocess.run(
            [
                str(self.cli_path),
                "--raw",
                "--mode=16",
                "--range-at",
                "3:data",
                "--range-at=7:32",
                "--range-at=12:16",
                "--base",
                "0x7c00",
                str(self.mixed_raw_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(decoded.returncode, 0, decoded.stderr)
        self.assertIn("00007C00", decoded.stdout)
        self.assertIn("mov ax, 0x1234", decoded.stdout)
        self.assertIn("00007C03", decoded.stdout)
        self.assertIn("db 0x00, 0x00, 0x90, 0xC3", decoded.stdout)
        self.assertNotIn("add byte", decoded.stdout)
        self.assertIn("00007C07", decoded.stdout)
        self.assertIn("mov eax, 0x12345678", decoded.stdout)
        self.assertIn("00007C0C", decoded.stdout)
        self.assertIn("mov ax, 0xABCD", decoded.stdout)

        duplicate_start = subprocess.run(
            [
                str(self.cli_path),
                "--raw",
                "--mode=16",
                "--mode-at=0:32",
                "--base=0x7c00",
                str(self.mixed_raw_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(duplicate_start.returncode, 1)
        self.assertIn(
            "raw range starts must increase without overlap",
            duplicate_start.stderr,
        )

        outside_input = subprocess.run(
            [
                str(self.cli_path),
                "--raw",
                "--mode=16",
                f"--range-at={len(self.mixed_raw_path.read_bytes())}:data",
                "--base=0x7c00",
                str(self.mixed_raw_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(outside_input.returncode, 1)
        self.assertIn("raw range start is outside input", outside_input.stderr)

        code_only_alias = subprocess.run(
            [
                str(self.cli_path),
                "--raw",
                "--mode=16",
                "--mode-at=3:32",
                "--mode-at=8:16",
                "--base=0x7c00",
                str(self.mode_alias_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(code_only_alias.returncode, 0, code_only_alias.stderr)
        self.assertIn("mov eax, 0x12345678", code_only_alias.stdout)
        self.assertIn("00007C08", code_only_alias.stdout)

    def test_cli_consumes_a_size_bound_raw_range_map(self):
        range_map = Path(self._fixture_directory.name) / "mixed-mode.cupidmap"
        range_map.write_text(
            "cupid.raw-map.v1\n"
            "size 16\n"
            "base 0x7c00\n"
            "range 0 code16\n"
            "range 3 data\n"
            "range 7 code32\n"
            "range 12 code16\n",
            encoding="ascii",
        )
        command = [
            str(self.cli_path),
            "--raw",
            "--range-map",
            str(range_map),
            str(self.mixed_raw_path),
        ]
        decoded = subprocess.run(
            command, cwd=REPO_ROOT, text=True, capture_output=True
        )
        repeated = subprocess.run(
            command, cwd=REPO_ROOT, text=True, capture_output=True
        )
        checked = subprocess.run(
            [
                str(self.cli_path),
                "--require-known",
                "--raw",
                "--range-map",
                str(range_map),
                str(self.mixed_raw_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )

        self.assertEqual(decoded.returncode, 0, decoded.stderr)
        self.assertEqual(repeated.returncode, 0, repeated.stderr)
        self.assertEqual(decoded.stdout, repeated.stdout)
        self.assertIn("mov ax, 0x1234", decoded.stdout)
        self.assertIn("db 0x00, 0x00, 0x90, 0xC3", decoded.stdout)
        self.assertIn("mov eax, 0x12345678", decoded.stdout)
        self.assertIn("mov ax, 0xABCD", decoded.stdout)
        self.assertEqual(checked.returncode, 0, checked.stderr)
        self.assertEqual(checked.stdout, "")
        self.assertEqual(checked.stderr, "")

    def test_cli_binds_a_valid_boundary_target_to_the_source_resolved_edge(self):
        fixture_root = Path(self._fixture_directory.name)
        image = fixture_root / "source-edge.bin"
        changed = fixture_root / "source-edge-changed.bin"
        range_map = fixture_root / "source-edge.cupidmap"
        image.write_bytes(bytes.fromhex("eb 00 c3 c3"))
        changed.write_bytes(bytes.fromhex("eb 01 c3 c3"))
        range_map.write_text(
            "cupid.raw-map.v2\n"
            "size 4\n"
            "base 0x1000\n"
            "edges 1\n"
            "range 0 code32\n"
            "edge 0 relative local 2 0x1002 32 0\n",
            encoding="ascii",
        )
        policy = [
            str(self.cli_path),
            "--require-known",
            "--require-local-targets",
            "--require-source-edges",
            "--raw",
            "--range-map",
            str(range_map),
        ]

        valid = subprocess.run(
            [*policy, str(image)], cwd=REPO_ROOT, text=True, capture_output=True
        )
        structural_only = subprocess.run(
            [
                str(self.cli_path),
                "--require-known",
                "--require-local-targets",
                "--raw",
                "--range-map",
                str(range_map),
                str(changed),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        rejected = subprocess.run(
            [*policy, str(changed)],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )

        self.assertEqual(valid.returncode, 0, valid.stderr)
        self.assertEqual(valid.stdout, "")
        self.assertEqual(structural_only.returncode, 0, structural_only.stderr)
        self.assertEqual(rejected.returncode, 1)
        self.assertEqual(rejected.stdout, "")
        self.assertIn(
            "source control-edge check failed: 1 of 1 edges invalid",
            rejected.stderr,
        )
        self.assertIn("1 target mismatch", rejected.stderr)

    def test_cli_rejects_malformed_and_instruction_mismatched_edge_rows(self):
        fixture_root = Path(self._fixture_directory.name)
        image = fixture_root / "edge-map-errors.bin"
        image.write_bytes(bytes.fromhex("eb 00 c3 c3"))
        parse_cases = (
            (
                "missing count",
                "cupid.raw-map.v2\nsize 4\nbase 0x1000\n"
                "range 0 code32\n",
                "raw range map v2 requires one edge count",
            ),
            (
                "count mismatch",
                "cupid.raw-map.v2\nsize 4\nbase 0x1000\nedges 2\n"
                "range 0 code32\n"
                "edge 0 relative local 2 0x1002 32 0\n",
                "raw range map edge count does not match its records",
            ),
            (
                "duplicate source",
                "cupid.raw-map.v2\nsize 4\nbase 0x1000\nedges 2\n"
                "range 0 code32\n"
                "edge 0 relative local 2 0x1002 32 0\n"
                "edge 0 relative local 2 0x1002 32 0\n",
                "raw edge sources must increase without overlap",
            ),
            (
                "outside source",
                "cupid.raw-map.v2\nsize 4\nbase 0x1000\nedges 1\n"
                "range 0 code32\n"
                "edge 4 relative local 2 0x1002 32 0\n",
                "raw edge source is outside the mapped image",
            ),
            (
                "wrong target mode",
                "cupid.raw-map.v2\nsize 4\nbase 0x1000\nedges 1\n"
                "range 0 code32\n"
                "edge 0 relative local 2 0x1002 16 0\n",
                "raw local edge target mode disagrees with ranges",
            ),
            (
                "external target inside image",
                "cupid.raw-map.v2\nsize 4\nbase 0x1000\nedges 1\n"
                "range 0 code32\n"
                "edge 0 relative external - 0x1002 32 0\n",
                "raw external edge target is inside the mapped image",
            ),
        )
        for name, contents, expected in parse_cases:
            with self.subTest(name=name):
                range_map = fixture_root / f"edge-{name.replace(' ', '-')}.map"
                range_map.write_text(contents, encoding="ascii")
                result = subprocess.run(
                    [
                        str(self.cli_path),
                        "--require-known",
                        "--require-source-edges",
                        "--raw",
                        "--range-map",
                        str(range_map),
                        str(image),
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                )
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, "")
                self.assertIn(expected, result.stderr)

        semantic_cases = (
            (
                "missing row",
                "cupid.raw-map.v2\nsize 4\nbase 0x1000\nedges 0\n"
                "range 0 code32\n",
                "1 of 1 edges invalid",
            ),
            (
                "middle source",
                "cupid.raw-map.v2\nsize 4\nbase 0x1000\nedges 1\n"
                "range 0 code32\n"
                "edge 1 relative local 2 0x1002 32 0\n",
                "2 of 2 edges invalid",
            ),
        )
        for name, contents, expected in semantic_cases:
            with self.subTest(name=name):
                range_map = fixture_root / f"edge-{name.replace(' ', '-')}.map"
                range_map.write_text(contents, encoding="ascii")
                result = subprocess.run(
                    [
                        str(self.cli_path),
                        "--require-known",
                        "--require-source-edges",
                        "--raw",
                        "--range-map",
                        str(range_map),
                        str(image),
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                )
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, "")
                self.assertIn(expected, result.stderr)
                self.assertIn("source mismatch", result.stderr)

    def test_cli_accepts_cross_mode_external_wrapping_and_maximum_targets(self):
        fixture_root = Path(self._fixture_directory.name)
        cases = (
            (
                "cross-mode-external",
                bytes.fromhex("66 ea 00 00 10 00 08 00"),
                "cupid.raw-map.v2\nsize 8\nbase 0x8000\nedges 1\n"
                "range 0 code16\n"
                "edge 0 far external - 0x00100000 32 8\n",
            ),
            (
                "wrapped-local",
                bytes.fromhex("eb 00 c3"),
                "cupid.raw-map.v2\nsize 3\nbase 0xffff\nedges 1\n"
                "range 0 code16\n"
                "edge 0 relative local 2 0x10001 16 0\n",
            ),
            (
                "maximum-external",
                bytes.fromhex("ea ff ff ff ff 08 00"),
                "cupid.raw-map.v2\nsize 7\nbase 0\nedges 1\n"
                "range 0 code32\n"
                "edge 0 far external - 0xffffffff 32 8\n",
            ),
        )
        for name, contents, raw_map in cases:
            with self.subTest(name=name):
                image = fixture_root / f"{name}.bin"
                range_map = fixture_root / f"{name}.map"
                image.write_bytes(contents)
                range_map.write_text(raw_map, encoding="ascii")
                result = subprocess.run(
                    [
                        str(self.cli_path),
                        "--require-known",
                        "--require-source-edges",
                        "--raw",
                        "--range-map",
                        str(range_map),
                        str(image),
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertEqual(result.stderr, "")

    def test_cli_rejects_stale_and_malformed_raw_range_maps(self):
        fixture_root = Path(self._fixture_directory.name)
        cases = (
            (
                "unknown schema",
                "cupid.raw-map.v3\nsize 16\nbase 0\nrange 0 code16\n",
                "raw range map has an unsupported schema",
            ),
            (
                "missing size",
                "cupid.raw-map.v1\nbase 0\nrange 0 code16\n",
                "raw range map requires one size",
            ),
            (
                "duplicate start",
                "cupid.raw-map.v1\nsize 16\nbase 0\n"
                "range 0 code16\nrange 0 data\n",
                "raw range starts must increase",
            ),
            (
                "invalid kind",
                "cupid.raw-map.v1\nsize 16\nbase 0\nrange 0 maybe\n",
                "raw range kind must be code16, code32, or data",
            ),
        )
        for name, contents, expected in cases:
            with self.subTest(name=name):
                range_map = fixture_root / f"bad-{name.replace(' ', '-')}.cupidmap"
                range_map.write_text(contents, encoding="ascii")
                result = subprocess.run(
                    [
                        str(self.cli_path),
                        "--raw",
                        "--range-map",
                        str(range_map),
                        str(self.mixed_raw_path),
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                )
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, "")
                self.assertIn(expected, result.stderr)

        stale_map = fixture_root / "stale.cupidmap"
        stale_map.write_text(
            "cupid.raw-map.v1\n"
            "size 15\n"
            "base 0x7c00\n"
            "range 0 code16\n",
            encoding="ascii",
        )
        stale = subprocess.run(
            [
                str(self.cli_path),
                "--raw",
                "--range-map",
                str(stale_map),
                str(self.mixed_raw_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(stale.returncode, 1)
        self.assertEqual(stale.stdout, "")
        self.assertIn(
            "raw range map expects 15 bytes, input has 16",
            stale.stderr,
        )

        conflict = subprocess.run(
            [
                str(self.cli_path),
                "--raw",
                "--range-map",
                str(stale_map),
                "--mode=16",
                "--base=0x7c00",
                str(self.mixed_raw_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(conflict.returncode, 2)
        self.assertEqual(conflict.stdout, "")
        self.assertIn("usage: cupiddis", conflict.stderr)

    def test_cli_typed_ranges_follow_the_active_smp_trampoline_layout(self):
        trampoline = Path(self._fixture_directory.name) / "smp-trampoline.bin"
        assembled = subprocess.run(
            [
                str(self.asm_path),
                "-f",
                "bin",
                str(REPO_ROOT / "kernel" / "smp" / "smp_trampoline.S"),
                "-o",
                str(trampoline),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(assembled.returncode, 0, assembled.stderr)
        image = trampoline.read_bytes()
        self.assertEqual(len(image), 4096)
        self.assertEqual(
            hashlib.sha256(image).hexdigest(),
            "b738ebb68f28b9b07e330761f4e9a7898f0424ab0a3835cd6079ae7d4a189e90",
        )

        command = [
            str(self.cli_path),
            "--raw",
            "--mode=16",
            "--range-at=0x1f:data",
            "--range-at=0x210:32",
            "--range-at=0x254:data",
            "--base=0x8000",
            str(trampoline),
        ]
        decoded = subprocess.run(
            command, cwd=REPO_ROOT, text=True, capture_output=True
        )
        repeated = subprocess.run(
            command, cwd=REPO_ROOT, text=True, capture_output=True
        )
        self.assertEqual(decoded.returncode, 0, decoded.stderr)
        self.assertEqual(repeated.returncode, 0, repeated.stderr)
        self.assertEqual(decoded.stdout, repeated.stdout)
        self.assertIn("00008000", decoded.stdout)
        self.assertIn("cli", decoded.stdout)
        self.assertIn("0000801F", decoded.stdout)
        self.assertIn("db 0x00", decoded.stdout)
        self.assertIn("00008210", decoded.stdout)
        self.assertIn("mov ax, 0x10", decoded.stdout)
        self.assertIn("00008254", decoded.stdout)
        self.assertNotIn("add byte [bx+si], al", decoded.stdout)
        self.assertNotIn("add byte [eax], al", decoded.stdout)
