"""Validate captured seed images in either executable format on either host."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ROLES = ('cupidasm', 'cupidc', 'cupiddis', 'cupidld', 'cupidobj', 'cupidbuild')
CALLER = r'''
#include "cupidbuild.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
  FILE *file;
  long length;
  unsigned char *bytes;
  unsigned char *before;
  int values[4];
  int index;
  int accepted;
  if (argc != 6) return 2;
  for (index = 0; index < 4; index++) {
    if (strlen(argv[index + 2]) != 1u || argv[index + 2][0] < '0' ||
        argv[index + 2][0] > '9') return 2;
    values[index] = argv[index + 2][0] - '0';
  }
  if (strcmp(argv[1], "null") == 0 || strcmp(argv[1], "oversize") == 0) {
    unsigned char sentinel = 0xa5u;
    accepted = cupidbuild_validate_seed_image_bytes(
        strcmp(argv[1], "null") == 0 ? (const unsigned char *)0 : &sentinel,
        strcmp(argv[1], "null") == 0 ? 1u : 67108865u,
        (cupidbuild_seed_image_format_t)values[0], (size_t)values[1],
        values[2], values[3]);
    return sentinel == 0xa5u ? (accepted ? 0 : 1) : 3;
  }
  file = fopen(argv[1], "rb");
  if (!file) return 2;
  if (fseek(file, 0L, SEEK_END) || (length = ftell(file)) < 0L ||
      fseek(file, 0L, SEEK_SET)) { fclose(file); return 2; }
  bytes = (unsigned char *)malloc((size_t)length + 1u);
  before = (unsigned char *)malloc((size_t)length + 1u);
  if (!bytes || !before) { free(bytes); free(before); fclose(file); return 2; }
  if (fread(bytes, 1u, (size_t)length, file) != (size_t)length) {
    free(bytes); free(before); fclose(file); return 2;
  }
  if (fclose(file)) { free(bytes); free(before); return 2; }
  memcpy(before, bytes, (size_t)length);
  accepted = cupidbuild_validate_seed_image_bytes(
      bytes, (size_t)length, (cupidbuild_seed_image_format_t)values[0],
      (size_t)values[1], values[2], values[3]);
  if (memcmp(before, bytes, (size_t)length) != 0) accepted = -1;
  free(bytes);
  free(before);
  return accepted < 0 ? 3 : (accepted ? 0 : 1);
}
'''


def selected_program(environment):
    if 'CUPID_SEED_IMAGE_PROGRAM' not in environment:
        return None
    value = environment['CUPID_SEED_IMAGE_PROGRAM']
    if not value or not Path(value).is_file():
        raise ValueError('CUPID_SEED_IMAGE_PROGRAM must name an existing file')
    return Path(value).resolve()


class SeedImageSelectionTests(unittest.TestCase):
    def test_missing_selector_uses_host_build(self):
        self.assertIsNone(selected_program({}))

    def test_bad_explicit_selector_never_falls_back(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('', directory, str(Path(directory) / 'missing')):
                with self.subTest(value=value), self.assertRaises(ValueError):
                    selected_program({'CUPID_SEED_IMAGE_PROGRAM': value})


class SeedImageProfileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.selected = selected_program(os.environ)
        cls.temporary = tempfile.TemporaryDirectory(prefix='cupid-seed-image-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = Path(cls.temporary.name)
        if cls.selected is not None:
            cls.program = cls.selected
            return
        compiler = shutil.which('clang' if os.name == 'nt' else 'cc')
        if not compiler:
            raise RuntimeError('host compiler is unavailable')
        caller = cls.directory / 'caller.cc'
        caller.write_text(CALLER, encoding='ascii')
        cls.program = cls.directory / ('validate.exe' if os.name == 'nt' else 'validate')
        command = [compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-D_CRT_SECURE_NO_WARNINGS', '-I', str(ROOT / 'toolchain'), '-x', 'c',
                   str(caller), *[str(ROOT / 'toolchain' / (name + '.cc')) for name in
                    ('ctool', 'ctool_host', 'elf32', 'cupidbuild_host', 'cupidbuild',
                     'seed_manifest', 'seed_release', 'contract_parse_internal', 'path_encoding')],
                   *(['-lntdll'] if os.name == 'nt' else []), '-o', str(cls.program)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=180)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def check(self, payload, fmt, role=1, promoted=1, current=None, accept=False):
        current = int(fmt == 2) if current is None else current
        if isinstance(payload, bytes):
            path = self.directory / 'candidate.bin'
            path.write_bytes(payload)
        else:
            path = payload
        result = subprocess.run([str(self.program), str(path), str(fmt), str(role),
                                 str(promoted), str(current)], capture_output=True, timeout=60)
        self.assertEqual(result.returncode, 0 if accept else 1,
                         result.stdout.decode(errors='replace') + result.stderr.decode(errors='replace'))
        self.assertEqual(result.stdout, b'')
        if accept:
            self.assertEqual(result.stderr, b'')
        if isinstance(payload, bytes):
            self.assertEqual(path.read_bytes(), payload)

    def image(self, fmt, role=1):
        platform, suffix = ('i386-windows', '.exe') if fmt == 2 else ('i386-linux', '.elf')
        return (ROOT / 'bootstrap/seeds' / platform / (ROLES[role] + suffix)).read_bytes()

    def test_all_twelve_seed_images_accept_on_this_host(self):
        for fmt in (1, 2):
            for role, name in enumerate(ROLES):
                with self.subTest(format=fmt, role=name):
                    self.check(self.image(fmt, role), fmt, role, accept=True)

    def test_opposite_format_rejects(self):
        for fmt in (1, 2):
            self.check(self.image(fmt), 3 - fmt)

    def test_empty_truncated_and_malformed_images_reject(self):
        for fmt in (1, 2):
            original = self.image(fmt)
            for data in (b'', b'not an executable', original[:16], original[:100], original[:-1]):
                with self.subTest(format=fmt, size=len(data)):
                    self.check(data, fmt)

    def test_invalid_profile_arguments_reject(self):
        for fmt, role, promoted, current in ((0, 1, 1, 0), (9, 1, 1, 0),
                (2, 6, 1, 1), (2, 5, 0, 0), (2, 1, 2, 1),
                (2, 1, 1, 3), (2, 1, 0, 1), (1, 1, 1, 1),
                (2, 1, 0, 2), (1, 1, 1, 2)):
            with self.subTest(profile=(fmt, role, promoted, current)):
                # Keep the image valid for the requested format so an argument
                # regression cannot be hidden by an unrelated format failure.
                data = self.image(fmt if fmt in (1, 2) else 2)
                self.check(data, fmt, role, promoted, current)

    def test_null_and_oversize_reject_before_read(self):
        for value in ('null', 'oversize'):
            self.check(value, 1)

    def test_windows_entry_point_and_machine_reject(self):
        original = self.image(2)
        pe = struct.unpack_from('<I', original, 0x3c)[0]
        for offset, code, value in ((pe + 4, '<H', 0x8664), (pe + 24 + 16, '<I', 0x1001)):
            data = bytearray(original)
            struct.pack_into(code, data, offset, value)
            self.check(bytes(data), 2)

    def test_windows_exact_import_library_and_procedure_reject(self):
        original = self.image(2)
        for old, new in ((b'KERNEL32.dll\0', b'USER3232.dll\0'),
                         (b'GetLastError\0', b'GetLastErr0r\0')):
            self.assertEqual(original.count(old), 1)
            self.check(original.replace(old, new), 2)

    def test_windows_role_and_plan_profiles_reject(self):
        for role, wrong in ((1, 3), (3, 1), (5, 1), (1, 5)):
            with self.subTest(role=role, wrong=wrong):
                self.check(self.image(2, role), 2, wrong)
        for role in (0, 1, 5):
            with self.subTest(role=role, plan='legacy'):
                self.check(self.image(2, role), 2, role, current=0)

    def wide_image(self, role):
        from tests.test_toolchain_bootstrap_seed import ToolchainBootstrapSeedCliTests
        from tools import bootstrap_toolchain as bootstrap
        return bytes(ToolchainBootstrapSeedCliTests._minimal_cupidbuild_profile_pe32(
            bootstrap._windows_utf8_imports(ROLES[role])))

    def test_exact_utf8_profiles_accept_only_selected_generation(self):
        for role, name in enumerate(ROLES):
            with self.subTest(role=name):
                wide = self.wide_image(role)
                self.check(wide, 2, role, current=2, accept=True)
                self.check(wide, 2, role, current=0)
                self.check(wide, 2, role, current=1)
                self.check(self.image(2, role), 2, role, current=2)

    def test_utf8_profiles_reject_wrong_roles_and_mixed_apis(self):
        for role, name in enumerate(ROLES):
            with self.subTest(role=name):
                wide = self.wide_image(role)
                self.check(wide, 2, 1 if role in (0, 3, 5) else 0, current=2)
                for old, new in ((b'GetCommandLineW\0', b'GetCommandLineA\0'),
                                 (b'CreateFileW\0', b'CreateFileA\0'),
                                 (b'SetLastError\0', b'SetLastErroX\0')):
                    self.assertEqual(wide.count(old), 1)
                    self.check(wide.replace(old, new), 2, role, current=2)

    def test_linux_entry_point_machine_and_dynamic_segments_reject(self):
        original = self.image(1)
        phoff = struct.unpack_from('<I', original, 28)[0]
        for offset, code, value in ((24, '<I', 0x08048001), (18, '<H', 62),
                                   (phoff, '<I', 2), (phoff, '<I', 3)):
            data = bytearray(original)
            struct.pack_into(code, data, offset, value)
            self.check(bytes(data), 1)

    def test_linux_writable_code_and_absent_loads_reject(self):
        original = self.image(1)
        phoff = struct.unpack_from('<I', original, 28)[0]
        phsize, count = struct.unpack_from('<HH', original, 42)
        data = bytearray(original)
        found = False
        for index in range(count):
            offset = phoff + phsize * index
            if struct.unpack_from('<I', data, offset)[0] == 1:
                flags = struct.unpack_from('<I', data, offset + 24)[0]
                if flags & 1:
                    struct.pack_into('<I', data, offset + 24, flags | 2)
                    found = True
        self.assertTrue(found)
        self.check(bytes(data), 1)
        data = bytearray(original)
        for index in range(count):
            struct.pack_into('<I', data, phoff + phsize * index, 0)
        self.check(bytes(data), 1)


if __name__ == '__main__':
    unittest.main(verbosity=2)
