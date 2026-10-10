"""Build and exercise the actual i386 hosted allocator with checked Cupid tools."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tools.bootstrap_toolchain import (
    ToolRunner, WINDOWS_TOOL_IMPORTS, freeze_seed_inputs,
    require_live_seed_inputs, _windows_utf8_imports,
)

ROOT = Path(__file__).resolve().parents[1]


class HostedHeapTests(unittest.TestCase):
    utf8 = False

    @classmethod
    def setUpClass(cls):
        base = ROOT / 'build'
        base.mkdir(exist_ok=True)
        retained = os.environ.get('CUPID_HOSTED_HEAP_PRODUCTS')
        if retained:
            cls.products = Path(retained).resolve()
            if cls.utf8:
                cls.products = cls.products / 'utf8'
            if not cls.products.is_relative_to(ROOT):
                raise AssertionError('Allocator products must be inside the source root')
            cls.products.mkdir(parents=True, exist_ok=True)
            if any(cls.products.iterdir()):
                raise AssertionError('Allocator products directory must be empty')
        else:
            temporary = tempfile.TemporaryDirectory(prefix='hosted-heap-', dir=base)
            cls.addClassCleanup(temporary.cleanup)
            cls.products = Path(temporary.name)
        if not cls.products.is_relative_to(ROOT):
            raise AssertionError('Allocator products must be inside the source root')
        cls.windows = os.name == 'nt'
        platform = 'i386-windows' if cls.windows else 'i386-linux'
        startup = ROOT / ('toolchain/hosted/i386-windows/' +
                          ('utf8_tool_start.asm' if cls.utf8 else 'tool_start.asm')
                          if cls.windows else 'toolchain/hosted/i386-linux/start.asm')
        inputs = [ROOT / 'toolchain/hosted/i386-linux/runtime.cc',
                  ROOT / ('toolchain/hosted/' + platform + '/runtime.cc'),
                  startup,
                  ROOT / 'toolchain/tests/hosted_heap_density_contract.cc',
                  ROOT / 'toolchain/tests/hosted_heap_fault_contract.cc',
                  ROOT / ('toolchain/tests/hosted_i386_windows_runtime_contract.cc'
                          if cls.windows else 'toolchain/tests/hosted_i386_runtime_contract.cc'),
                  Path(__file__)]
        if cls.utf8:
            inputs.extend(ROOT / name for name in (
                'toolchain/hosted/i386-windows/utf8_long_path_start.asm',
                'toolchain/hosted/i386-windows/windows_utf8.cc',
                'toolchain/path_encoding.cc', 'toolchain/path_encoding.h'))
        inputs.extend((ROOT / 'toolchain/hosted/i386-linux/include').glob('*.h'))
        cls.source_bytes = {path: path.read_bytes() for path in inputs}
        (cls.products / 'inputs.json').write_text(json.dumps({
            path.relative_to(ROOT).as_posix(): {
                'size': len(payload), 'sha256': hashlib.sha256(payload).hexdigest()}
            for path, payload in cls.source_bytes.items()}, indent=2, sort_keys=True) + '\n')
        cls.seed = freeze_seed_inputs(
            ROOT / 'bootstrap/seeds' / platform / 'manifest.json',
            cls.products / 'seed')
        cls.runner = ToolRunner(ROOT)
        cls.commands = []
        cls.start = cls.products / 'start.o'
        cls.tool('cupidasm', ['-f', 'elf32', startup, '-o', cls.start], 120)
        cls.support = []
        runtime_definitions = ['CUPID_WINDOWS_UTF8=1'] if cls.utf8 else []
        if cls.utf8:
            extra_start = cls.products / 'long-path-start.o'
            cls.tool('cupidasm', ['-f', 'elf32', ROOT /
                     'toolchain/hosted/i386-windows/utf8_long_path_start.asm',
                     '-o', extra_start], 120)
            cls.certify(extra_start)
            cls.support.extend([extra_start,
                cls.compile('toolchain/path_encoding.cc', 'path-encoding.o'),
                cls.compile('toolchain/hosted/i386-windows/windows_utf8.cc',
                            'windows-utf8.o', gnu=True,
                            definitions=['_WIN32=1', 'CUPID_WINDOWS_LONG_PATHS=1'])])
        runtime = 'toolchain/hosted/' + platform + '/runtime.cc'
        cls.runtime = cls.compile(runtime, 'runtime.o', gnu=True,
                                  definitions=runtime_definitions)
        repeated = cls.compile(runtime, 'runtime-repeat.o', gnu=True,
                               definitions=runtime_definitions)
        if cls.runtime.read_bytes() != repeated.read_bytes():
            raise AssertionError('Repeated runtime compilation differs')
        cls.program = cls.link('heap', cls.compile(
            'toolchain/tests/hosted_heap_density_contract.cc', 'density.o'), cls.runtime)
        definitions = (['cupid_windows_virtual_alloc=cupid_heap_contract_virtual_alloc',
                        'cupid_windows_virtual_free=cupid_heap_contract_virtual_free']
                       if cls.windows else ['cupid_linux_syscall1=cupid_heap_contract_syscall1'])
        fault_runtime = cls.compile(runtime, 'fault-runtime.o', gnu=True,
                                    definitions=[*runtime_definitions, *definitions])
        cls.fault_program = cls.link('heap-fault', cls.compile(
            'toolchain/tests/hosted_heap_fault_contract.cc', 'fault.o',
            definitions=['CUPID_HEAP_WINDOWS=1'] if cls.windows else []), fault_runtime)
        legacy_source = ('toolchain/tests/hosted_i386_windows_runtime_contract.cc'
                         if cls.windows else 'toolchain/tests/hosted_i386_runtime_contract.cc')
        cls.legacy_program = cls.link('runtime-contract', cls.compile(
            legacy_source, 'legacy.o', gnu=True), cls.runtime)
        cls.certify(cls.start)
        cls.certify(cls.runtime)
        require_live_seed_inputs(cls.seed)

    @classmethod
    def tool(cls, name, arguments, timeout):
        result = cls.runner.run(cls.seed.tools[name], arguments, timeout)
        cls.commands.append({'tool': name, 'arguments': list(map(str, arguments)),
                             'timeout_seconds': timeout, 'exit_code': result.returncode,
                             'stdout': result.stdout, 'stderr': result.stderr})
        (cls.products / 'commands.json').write_text(
            json.dumps(cls.commands, indent=2) + '\n', encoding='utf-8')
        if (result.returncode, result.stdout, result.stderr) != (0, '', ''):
            raise AssertionError(cls.commands[-1])
        cls.require_sources()
        return result

    @classmethod
    def require_sources(cls):
        for path, payload in cls.source_bytes.items():
            if path.read_bytes() != payload:
                raise AssertionError('Allocator input changed during verification: ' + str(path))

    @classmethod
    def certify(cls, path):
        cls.tool('cupiddis', ['--require-known', '--require-local-targets',
                             '--require-code-anchors', path], 120)

    @classmethod
    def compile(cls, source, name, *, gnu=False, definitions=()):
        output = cls.products / name
        arguments = ['--root', ROOT, '-c', '/' + source,
                     '--include-angle', '/toolchain/hosted/i386-linux/include']
        if gnu:
            arguments.append('--gnu')
        for definition in definitions:
            arguments.extend(['-D', definition])
        arguments.extend(['-o', '/' + output.relative_to(ROOT).as_posix()])
        cls.tool('cupidc', arguments, 360)
        cls.certify(output)
        return output

    @classmethod
    def link(cls, name, fixture, runtime):
        output = cls.products / (name + ('.exe' if cls.windows else '.elf'))
        arguments = ['-m', 'i386pe' if cls.windows else 'elf_i386',
                     '--text-address', '0x00401000' if cls.windows else '0x08048000',
                     '--entry', '_start']
        if cls.windows:
            imports = (_windows_utf8_imports('cupidc', long_paths=True)
                       if cls.utf8 else WINDOWS_TOOL_IMPORTS)
            for library, names in imports:
                for symbol in names:
                    arguments.extend(['--import', '__imp_' + symbol + '=' + library + ':' + symbol])
        arguments.extend(['-o', output, cls.start, fixture, runtime, *cls.support])
        cls.tool('cupidld', arguments, 180)
        cls.certify(output)
        return output

    def invoke(self, *arguments, program=None):
        executable = program or self.program
        if self.windows:
            result = self.runner.run(executable, arguments, 60)
        else:
            import resource

            def bound_memory():
                resource.setrlimit(resource.RLIMIT_AS, (32 * 1024 * 1024,) * 2)

            result = subprocess.run([str(executable), *map(str, arguments)], cwd=ROOT,
                                    capture_output=True, text=True, timeout=60,
                                    preexec_fn=bound_memory)
        (self.products / (self._testMethodName + '.json')).write_text(json.dumps({
            'program': str(executable),
            'program_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
            'arguments': list(map(str, arguments)), 'timeout_seconds': 60,
            'address_space_limit_bytes': None if self.windows else 32 * 1024 * 1024,
            'exit_code': result.returncode, 'stdout': result.stdout,
            'stderr': result.stderr}, indent=2) + '\n', encoding='utf-8')
        self.assertEqual((result.returncode, result.stderr), (0, ''), result.stdout)
        require_live_seed_inputs(self.seed)
        self.require_sources()
        return json.loads(result.stdout)

    def test_65536_one_byte_allocations_reuse_zeroed_aligned_storage(self):
        self.assertEqual(self.invoke('65536', '1'),
                         {'requested': 65536, 'allocated': 65536, 'reuse': 1, 'alignment': 16})

    def test_65536_cache_line_allocations_preserve_live_neighbors(self):
        self.assertEqual(self.invoke('65536', '64'),
                         {'requested': 65536, 'allocated': 65536, 'reuse': 1, 'alignment': 16})

    def test_page_sized_allocations_survive_fragmentation(self):
        self.assertEqual(self.invoke('1024', '4096'),
                         {'requested': 1024, 'allocated': 1024, 'reuse': 1, 'alignment': 16})

    def test_mixed_size_reallocations_preserve_all_live_payloads(self):
        self.assertEqual(self.invoke('stress'), {
            'operations': 20000, 'live_slots': 256, 'preserved_data': 1, 'alignment': 16})

    def test_split_growth_overflow_failure_and_zero_size_contracts(self):
        self.assertEqual(self.invoke('contracts'), {
            'contracts': 1, 'overflow': 1, 'failed_realloc_preserves_data': 1, 'zero_size': 1})

    def test_failed_os_allocation_preserves_realloc_data_and_recovers(self):
        self.assertEqual(self.invoke('allocation-failure', program=self.fault_program), {
            'allocation_failure': 1, 'preserved_data': 1, 'recovery': 1})

    def test_failed_os_release_keeps_free_storage_available(self):
        self.assertEqual(self.invoke('release-failure', program=self.fault_program), {
            'release_failure': 1, 'reused_without_growth': 1, 'recovery': 1})

    def test_overflow_is_rejected_before_requesting_os_storage(self):
        self.assertEqual(self.invoke('overflow', program=self.fault_program), {
            'overflow': 1, 'no_os_allocation': 1})

    def test_complete_existing_runtime_contract_with_identical_runtime_object(self):
        output = self.products / 'legacy-output.bin'
        missing = self.products / 'missing-file.bin'
        self.check_legacy_runtime(output, missing, 'legacy-result.json')

    def check_legacy_runtime(self, output, missing, record_name):
        arguments = (['plain', 'space arg', 'quote"arg', 'trailing\\', output, missing]
                     if self.windows else [output, missing])
        result = self.runner.run(self.legacy_program, arguments, 60)
        (self.products / record_name).write_text(json.dumps({
            'exit_code': result.returncode, 'stdout': result.stdout,
            'stderr': result.stderr, 'arguments': list(map(str, arguments)),
            'output_file': output.relative_to(self.products).as_posix()}, indent=2) + '\n')
        self.assertEqual((result.returncode, result.stderr), (0, ''), result.stdout)
        self.assertEqual(result.stdout, 'Cupid-built Windows tool runtime: ok\n'
                         if self.windows else 'printf-ok 7\nputs-ok\nfputs-ok\nruntime-ok\n')
        self.assertEqual(output.read_bytes(), b'headtail' if self.windows else b'ok -12 0000002A\n')
        self.assertFalse(missing.exists())
        require_live_seed_inputs(self.seed)
        self.require_sources()


@unittest.skipUnless(os.name == 'nt', 'The UTF-8 startup profile requires Windows')
class HostedWindowsUtf8HeapTests(HostedHeapTests):
    utf8 = True

    def test_existing_runtime_contract_handles_unicode_long_paths(self):
        directory = self.products / ('unicode-cupid-' + 'a' * 100) / ('b' * 100)
        directory.mkdir(parents=True)
        output = directory / 'cupid-\u2665-output.bin'
        missing = directory / 'cupid-\u2665-missing.bin'
        self.assertGreater(len(str(output)), 260)
        self.check_legacy_runtime(output, missing, 'legacy-unicode-result.json')


class HostedHeapHarnessTests(unittest.TestCase):
    def test_existing_receipts_are_rejected_without_overwriting_inputs(self):
        base = ROOT / 'build'
        base.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='heap-receipts-', dir=base) as directory:
            inputs = Path(directory) / 'inputs.json'
            inputs.write_bytes(b'previous retained inputs\n')
            with patch.dict(os.environ, {'CUPID_HOSTED_HEAP_PRODUCTS': directory}):
                with self.assertRaisesRegex(AssertionError, 'must be empty'):
                    HostedHeapTests.setUpClass()
            self.assertEqual(inputs.read_bytes(), b'previous retained inputs\n')
            self.assertEqual(list(Path(directory).iterdir()), [inputs])


if __name__ == '__main__':
    unittest.main()
