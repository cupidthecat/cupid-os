"""Real sparse-file contracts for the hosted signed 64-bit stdio extension."""
import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

REPO_ROOT = Path(__file__).resolve().parents[1]
SOURCE = REPO_ROOT / 'toolchain/tests/hosted_wide_file_contract.cc'
FOUR_GIB = 1 << 32
MARKERS = {33: b'A', 1 << 31: b'B', FOUR_GIB - 1: b'C',
           FOUR_GIB + 9: b'D', FOUR_GIB + 33: b'E', FOUR_GIB + 48: b'F'}


def allocated_size(path):
    if os.name != 'nt': return path.stat().st_blocks * 512
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    query = kernel.GetCompressedFileSizeW
    query.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_uint32)]
    query.restype = ctypes.c_uint32
    high = ctypes.c_uint32()
    low = query(str(path), ctypes.byref(high))
    if low == 0xffffffff and ctypes.get_last_error(): raise ctypes.WinError(ctypes.get_last_error())
    return (high.value << 32) | low


def sparse_file(path, length):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('x+b', buffering=0) as stream:
        if os.name == 'nt':
            import msvcrt
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            device = kernel.DeviceIoControl
            device.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p,
                ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32,
                ctypes.POINTER(ctypes.c_uint32), ctypes.c_void_p]
            device.restype = ctypes.c_int
            handle = ctypes.c_void_p(msvcrt.get_osfhandle(stream.fileno()))
            returned = ctypes.c_uint32()
            if not device(handle, 0x900c4, None, 0, None, 0, ctypes.byref(returned), None):
                raise ctypes.WinError(ctypes.get_last_error())
            seek = kernel.SetFilePointerEx
            seek.argtypes = [ctypes.c_void_p, ctypes.c_int64, ctypes.POINTER(ctypes.c_int64), ctypes.c_uint32]
            seek.restype = ctypes.c_int
            end = kernel.SetEndOfFile
            end.argtypes, end.restype = [ctypes.c_void_p], ctypes.c_int
            if not seek(handle, length, None, 0) or not end(handle):
                raise ctypes.WinError(ctypes.get_last_error())
            region = (ctypes.c_int64 * 2)(0, length)
            if not device(handle, 0x980c8, ctypes.byref(region), ctypes.sizeof(region),
                          None, 0, ctypes.byref(returned), None):
                raise ctypes.WinError(ctypes.get_last_error())
        else:
            os.ftruncate(stream.fileno(), length)
        for offset, value in MARKERS.items():
            if offset < length:
                stream.seek(offset)
                stream.write(value)
    assert path.stat().st_size == length
    assert allocated_size(path) < 1024 * 1024, 'large fixture must remain physically sparse'


class HostedWideFileIOTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.checked = bool(os.environ.get('CUPID_WIDE_FILE_EXE'))
        cls.temporary = None
        if os.environ.get('CUPID_WIDE_FILE_PRODUCTS'):
            cls.products = Path(os.environ['CUPID_WIDE_FILE_PRODUCTS']).resolve()
            cls.products.mkdir(parents=True, exist_ok=True)
        else:
            base = REPO_ROOT / 'build'
            base.mkdir(exist_ok=True)
            cls.temporary = tempfile.TemporaryDirectory(prefix='wide-file-contract-', dir=base)
            cls.products = Path(cls.temporary.name)
            assert cls.products.resolve().is_relative_to(base.resolve())
        executable = os.environ.get('CUPID_WIDE_FILE_EXE') or os.environ.get('CUPID_WIDE_REFERENCE_EXE')
        if executable:
            cls.executable = Path(executable).resolve()
        else:
            compiler = shutil.which('gcc')
            if compiler is None: raise RuntimeError('native reference compiler is unavailable')
            cls.executable = cls.products / ('wide-file-contract.exe' if os.name == 'nt' else 'wide-file-contract')
            command = [compiler, '-x', 'c', '-std=c11', '-O2', '-Wall', '-Wextra', str(SOURCE), '-o', str(cls.executable)]
            result = subprocess.run(command, capture_output=True, timeout=60)
            (cls.products / 'native-compile.stdout').write_bytes(result.stdout)
            (cls.products / 'native-compile.stderr').write_bytes(result.stderr)
            if result.returncode: raise AssertionError(result.stderr.decode(errors='replace'))
            (cls.products / 'native-command.json').write_text(json.dumps(command, indent=2) + '\n')

    @classmethod
    def tearDownClass(cls):
        if cls.temporary: cls.temporary.cleanup()

    def run_mode(self, mode, *, position, value=0, count=0, failed=False, stream=True, mutation=None):
        directory = self.products / self._testMethodName
        directory.mkdir()
        path = directory / 'fixture.bin'
        length = FOUR_GIB - 1 if mode == 'h' else FOUR_GIB + 65
        if mode not in ('m', 'o'): sparse_file(path, length)
        description = {'mode': mode, 'logical_size': length,
            'markers': {str(offset): value.hex() for offset, value in MARKERS.items() if offset < length},
            'expected_position': position, 'expected_value': value, 'expected_count': count,
            'expected_failure': failed, 'mutation': mutation}
        (directory / 'fixture.json').write_text(json.dumps(description, indent=2, sort_keys=True) + '\n')
        result = subprocess.run([str(self.executable), mode, str(path)], capture_output=True, timeout=30)
        (directory / 'command.stdout').write_bytes(result.stdout)
        (directory / 'command.stderr').write_bytes(result.stderr)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = dict((key, int(number)) for key, number in
            (item.split('=') for item in result.stdout.decode('ascii').split()))
        expected = {'open': int(stream), 'seek': -1 if failed else 0, 'tell': 0,
            'high': position >> 32, 'low': position & 0xffffffff,
            'count': count, 'value': value, 'errno': int(failed),
            'errno_code': (9 if mode == 'm' else 2 if mode == 'o' else 22) if failed else 0,
            'error': int(self.checked and (failed or mode == 'r')) if stream else -1}
        if mode == 'm': expected['tell'] = -1
        if mode == 'o': expected.update(seek=-9, tell=-9)
        self.assertEqual(report, expected)
        if mode in ('m', 'o'):
            self.assertFalse(path.exists())
        else:
            final_size = length + (2 if mode in ('g', 'h') else 0)
            self.assertEqual(path.stat().st_size, final_size)
            with path.open('rb', buffering=0) as image:
                expected_marks = {offset: mark for offset, mark in MARKERS.items() if offset < length}
                if mode == 'f': expected_marks[FOUR_GIB + 9] = b'wide'
                if mode in ('g', 'h'): expected_marks[length] = b'++'
                for offset, mark in expected_marks.items():
                    image.seek(offset)
                    self.assertEqual(image.read(len(mark)), mark, offset)
                for offset in (0, 64, (1 << 31) - 32, FOUR_GIB - 32, FOUR_GIB + 16):
                    if offset + 8 <= final_size:
                        image.seek(offset)
                        self.assertEqual(image.read(8), bytes(8), offset)
            allocation = allocated_size(path)
            self.assertLess(allocation, 1024 * 1024)
            (directory / 'allocation.json').write_text(json.dumps({'logical_size': final_size,
                'allocated_bytes': allocation}, indent=2) + '\n')

    def test_read_at_two_gib(self): self.run_mode('a', position=(1 << 31) + 1, value=66, count=1)
    def test_read_above_four_gib(self): self.run_mode('b', position=FOUR_GIB + 34, value=69, count=1)
    def test_valid_all_ones_low_position(self): self.run_mode('c', position=FOUR_GIB, value=67, count=1)
    def test_all_ones_low_position_after_os_error(self): self.run_mode('r', position=FOUR_GIB, value=67, count=1)
    def test_negative_wide_relative_seek(self): self.run_mode('d', position=34, value=65, count=1)
    def test_negative_end_seek(self): self.run_mode('e', position=FOUR_GIB + 49, value=70, count=1)
    def test_write_above_four_gib(self): self.run_mode('f', position=FOUR_GIB + 13, count=4, mutation='wide_at_4g_plus_9')
    def test_append_after_rewind_above_four_gib(self): self.run_mode('g', position=FOUR_GIB + 67, count=2, mutation='append_two')
    def test_append_at_all_ones_low_end(self): self.run_mode('h', position=FOUR_GIB + 1, count=2, mutation='append_two')
    def test_positive_wide_relative_seek(self): self.run_mode('i', position=FOUR_GIB + 34, value=69, count=1)
    def test_invalid_origin_preserves_position(self): self.run_mode('j', position=33, failed=True)
    def test_negative_absolute_seek_preserves_position(self): self.run_mode('k', position=33, failed=True)
    def test_negative_final_position_is_rejected(self): self.run_mode('l', position=33, failed=True)
    def test_null_stream_clears_tell_output(self): self.run_mode('m', position=0, failed=True, stream=False)
    def test_null_tell_output_is_rejected(self): self.run_mode('n', position=33, failed=True)
    def test_missing_file_is_not_created(self): self.run_mode('o', position=0, failed=True, stream=False)
    def test_negative_wide_end_seek(self): self.run_mode('q', position=34, value=65, count=1)


if __name__ == '__main__': unittest.main()
