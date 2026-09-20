import os
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.cupidc_kernel_compile import (
    FROZEN_KERNEL_INPUT_CLOSURES,
    KERNEL_I386_ARGUMENTS,
    validate_i386_relocatable_bytes,
)


ROOT = Path(__file__).resolve().parents[1]


def active_input_bytes(path):
    source = ROOT / path
    if path == "kernel/cpu/ksyms_data.cc" and not source.exists():
        return (b'#include "ksyms.h"\nconst unsigned int '
                b'__attribute__((section(".ksyms"), used, aligned(4))) '
                b'ksym_blob[] = {0x4d59534bu, 0u, 16u, 16u};\n'
                b'const unsigned int ksym_blob_size = 16u;\n')
    return source.read_bytes()


def source_bundle(entries):
    result = bytearray(b"CUPSRC1\n" + struct.pack("<I", len(entries)))
    for path, contents in entries:
        name = path.encode("ascii") if isinstance(path, str) else path
        result += struct.pack("<II", len(name), len(contents)) + name + contents
    return bytes(result)


class CupidCSourceBundleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix=".source-bundle-", dir=ROOT / "toolchain")
        cls.addClassCleanup(cls.build.cleanup)
        build = Path(cls.build.name)
        suffix = ".exe" if os.name == "nt" else ""
        cls.compiler = build / ("cupidc" + suffix)
        result = subprocess.run(
            ["make", "-C", str(ROOT / "toolchain"), f"BUILD_DIR={build.name}",
             f"{build.name}/cupidc{suffix}"], capture_output=True, text=True, timeout=120,
        )
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cupid-source-bundle-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.bundle = self.root / "source.cupsrc"
        self.output = self.root / "out.o"

    def compile(self, entries, *, source="/src/main.cc", extra=(), raw=None):
        self.bundle.write_bytes(source_bundle(entries) if raw is None else raw)
        return subprocess.run(
            [str(self.compiler), "-c", source, "-o", "/out.o", "--root", str(self.root),
             "--source-bundle", str(self.bundle), *extra],
            capture_output=True, text=True, timeout=180,
        )

    def test_nested_includes_forced_includes_and_logical_file_identity(self):
        entries = sorted({
            "/src/main.cc": b'#include "nested/a.h"\n#include <config.h>\n'
                            b'const char *name = __FILE__; int answer(void) { return VALUE + FORCED; }\n',
            "/src/nested/a.h": b'#include "../../include/base.h"\n',
            "/include/base.h": b"#define BASE 40\n",
            "/include/config.h": b"#define VALUE (BASE + 1)\n",
            "/include/forced.h": b"#define FORCED 1\n",
        }.items())
        extra = ("-I", "/include", "-include", "/include/forced.h")
        result = self.compile(entries, extra=extra)
        self.assertEqual(result.returncode, 0, result.stderr)
        bundled = self.output.read_bytes()
        validate_i386_relocatable_bytes(bundled, require_executable=True)
        self.assertIn(b"/src/main.cc\0", bundled)
        for name, contents in entries:
            path = self.root / name.lstrip("/")
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(contents)
        result = subprocess.run(
            [str(self.compiler), "-c", "/src/main.cc", "-o", "/out.o", "--root",
             str(self.root), *extra], capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.output.read_bytes(), bundled)

    def test_missing_entries_never_fall_back_to_live_files(self):
        (self.root / "src").mkdir()
        (self.root / "src/main.cc").write_text("int answer(void) { return 7; }\n")
        (self.root / "src/live.h").write_text("#define VALUE 7\n")
        cases = (
            [("/unrelated.h", b"")],
            [("/src/main.cc", b'#include "live.h"\nint x = VALUE;\n')],
        )
        for entries in cases:
            with self.subTest(entries=entries):
                self.output.write_bytes(b"previous object")
                result = self.compile(entries)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(self.output.read_bytes(), b"previous object")

    def test_rejects_malformed_bundles_before_touching_output(self):
        valid = source_bundle([("/src/main.cc", b"int x;\n")])
        cases = [b"", valid[:11], b"BADMAGIC" + valid[8:], valid + b"x",
                 valid[:-1], b"CUPSRC1\n" + struct.pack("<I", 0),
                 b"CUPSRC1\n" + struct.pack("<I", 513),
                 valid[:12] + struct.pack("<II", 0xFFFFFFFF, 0),
                 valid[:16] + struct.pack("<I", 0xFFFFFFFF) + valid[20:],
                 source_bundle([("/a", b""), ("/a", b"")]),
                 source_bundle([("/b", b""), ("/a", b"")])]
        for path in (b"", b"/", b"relative", b"//a", b"/a/", b"/a//b", b"/./a",
                     b"/a/../b", b"/a\\b", b"/C:/a", b"/a\0b", b"/a\nb", b"/\x80", b"/" + b"a" * 4095):
            cases.append(source_bundle([(path, b"")]))
        for index, malformed in enumerate(cases):
            with self.subTest(case=index):
                self.output.write_bytes(b"previous object")
                result = self.compile([], raw=malformed)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("invalid or unreadable CUPSRC1", result.stderr)
                self.assertEqual(self.output.read_bytes(), b"previous object")

    def test_accepts_exact_entry_and_path_limits_and_empty_header(self):
        entries = [(f"/empty/{index:03}.h", b"") for index in range(510)]
        entries += [("/src/main.cc", b'#include "/empty/000.h"\nint x;\n'),
                    ("/" + "z" * 4094, b"")]
        result = self.compile(sorted(entries))
        self.assertEqual(result.returncode, 0, result.stderr)
        validate_i386_relocatable_bytes(self.output.read_bytes())

    def test_source_output_collision_is_rejected(self):
        self.output.write_bytes(b"previous object")
        result = self.compile([("/out.o", b"int x;\n")], source="/out.o")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.output.read_bytes(), b"previous object")

    def test_cli_requires_root_and_one_nonempty_bundle_argument(self):
        for args in (("--source-bundle", str(self.bundle)),
                     ("--root", str(self.root), "--source-bundle"),
                     ("--root", str(self.root), "--source-bundle", ""),
                     ("--root", str(self.root), "--source-bundle", "a", "--source-bundle", "b")):
            with self.subTest(args=args):
                result = subprocess.run(
                    [str(self.compiler), "-c", "/a.cc", "-o", "/out.o", *args],
                    capture_output=True, text=True, timeout=30,
                )
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertFalse(self.output.exists())

    def test_all_eleven_active_frozen_closures_match_live_compilation(self):
        for source, headers in FROZEN_KERNEL_INPUT_CLOSURES.items():
            with self.subTest(source=source):
                entries = sorted(("/" + path, active_input_bytes(path))
                                 for path in (source, *headers))
                result = self.compile(entries, source="/" + source, extra=KERNEL_I386_ARGUMENTS)
                self.assertEqual(result.returncode, 0, result.stderr)
                bundled = self.output.read_bytes()
                validate_i386_relocatable_bytes(bundled)
                for name, contents in entries:
                    path = self.root / name.lstrip("/")
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(contents)
                result = subprocess.run(
                    [str(self.compiler), "-c", "/" + source, "-o", "/ordinary.o",
                     "--root", str(self.root), *KERNEL_I386_ARGUMENTS],
                    capture_output=True, text=True, timeout=180,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual((self.root / "ordinary.o").read_bytes(), bundled)


if __name__ == "__main__":
    unittest.main()
