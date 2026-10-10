"""Rebuild the held image argument owner and replay both actual host oracles."""
import argparse
import concurrent.futures
import contextlib
import gzip
import hashlib
import inspect
import io
import json
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import shutil
import struct
import subprocess
import sys
import threading
import time
import traceback
import unicodedata
from unittest.mock import patch

created_output = None


def identity(data):
    return {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def actual_oracle(hostbuild, case, argv0):
    captures = []
    typed = {}
    original_get_value = argparse.ArgumentParser._get_value

    def get_value(parser, action, value):
        result = original_get_value(parser, action, value)
        if action.dest in ('hdd_mb', 'fat_start_lba'):
            typed[action.dest] = value
        return result

    def capture(image, bootloader, kernel, hdd_mb, fat_start_lba, stages,
                force_format, *, seed_manifest):
        def number(name, value):
            magnitude = abs(value)
            return [min(magnitude, 0xffffffff), sum(char.isdecimal() for char in typed[name]),
                    int(magnitude <= 0xffffffff), int(value < 0), int(value == 0)]
        captures.append({'paths': list(map(str, (seed_manifest, image, bootloader, kernel))),
            'numbers': [number('hdd_mb', hdd_mb), number('fat_start_lba', fat_start_lba)],
            'force': int(force_format), 'stages': [[str(stage.source), stage.dest] for stage in stages]})

    stdout, stderr = io.StringIO(), io.StringIO()
    previous = sys.get_int_max_str_digits()
    try:
        sys.set_int_max_str_digits(case['limit'])
        with patch.object(hostbuild, 'Path', PureWindowsPath if case['basename'] else PurePosixPath), \
             patch.object(hostbuild, 'create_or_update_image', capture), \
             patch.object(argparse.ArgumentParser, '_get_value', get_value), \
             patch.object(sys, 'argv', [argv0]), \
             contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            try:
                result = hostbuild.main(['image', *case['argv']])
                if result != 0 or len(captures) != 1:
                    raise ValueError('Original command did not reach exactly one image handoff')
                status = 0
                if case['expected_api'] is not None:
                    if case['expected_api'] != 5 or len(captures[0]['stages']) <= case['capacity']:
                        raise ValueError('Invalid explicit API-capacity expectation')
                    status = 5
            except SystemExit as error:
                if error.code not in (0, 2):
                    raise ValueError('Unexpected original exit code') from error
                status = 1 if error.code == 0 else 2
    finally:
        sys.set_int_max_str_digits(previous)
    return {'status': status, 'expected': captures[0] if status == 0 else None,
            'original_stdout': stdout.getvalue(), 'original_stderr': stderr.getvalue()}


def decode(payload, records):
    position = 0

    def u32():
        nonlocal position
        value, = struct.unpack_from('<I', payload, position)
        position += 4
        return value

    def text():
        nonlocal position
        size = u32()
        data = payload[position:position + size]
        if len(data) != size:
            raise ValueError('Truncated result string')
        position += size
        return data.decode('utf-8')

    for index, case in enumerate(records):
        status = u32()
        expected = None
        if status == 0:
            path_type = PureWindowsPath if case['basename'] else PurePosixPath
            paths = [str(path_type(text())) for _ in range(4)]
            numbers = [[u32() for _ in range(5)] for _ in range(2)]
            force, count = u32(), u32()
            if count > 32768:
                raise ValueError('Unexpected result stage count')
            stages = [[str(path_type(text())), text()] for _ in range(count)]
            expected = {'paths': paths, 'numbers': numbers, 'force': force, 'stages': stages}
        if (status, expected) != (case['status'], case['expected']):
            raise ValueError(('Original oracle differs', index, case['name']))
    if position != len(payload):
        raise ValueError('Trailing result bytes')


def main():
    global created_output
    arguments = argparse.ArgumentParser(description=__doc__)
    arguments.add_argument('--output', type=Path, required=True)
    arguments.add_argument('--manifest', type=Path)
    args = arguments.parse_args()
    artifact = Path(__file__).resolve().parent
    root = artifact.parents[2]
    output = args.output.resolve()
    if not output.is_relative_to(root / 'build') or output == root / 'build':
        raise ValueError('Use a fresh output directory below the repository build directory')
    if output.exists():
        raise ValueError('Output already exists; preserve it and choose a fresh directory')
    output.mkdir(parents=True)
    created_output = output
    sys.path.insert(0, str(root))
    from tools import bootstrap_toolchain as bootstrap
    from tools import hostbuild
    windows = os.name == 'nt'
    host, suffix = ('windows', 'exe') if windows else ('linux', 'elf')
    proof = json.loads((artifact.parent / 'evidence/native-image-options-20261010.json').read_bytes())
    fixture_bytes = (artifact / 'native-image-options-oracles.json.gz').read_bytes()
    patch_bytes = (artifact / 'native-image-options.patch').read_bytes()
    if identity(fixture_bytes) != proof['oracle_fixture'] or identity(patch_bytes) != proof['artifact_patch']:
        raise ValueError('Held prototype artifacts differ from the recorded evidence')
    fixture = json.loads(gzip.decompress(fixture_bytes))
    sources = output / 'source'
    sources.mkdir()
    chunks = patch_bytes.decode('ascii').split('--- /dev/null\n')[1:]
    for chunk in chunks:
        lines = chunk.splitlines()
        match = re.fullmatch(r'\+\+\+ b/toolchain/prototypes/native_image_options/([a-z_]+\.(?:cc|h|inc))', lines[0])
        hunk = re.fullmatch(r'@@ -0,0 \+1,(\d+) @@', lines[1])
        if not match or not hunk or int(hunk[1]) != len(lines[2:]) or not all(line.startswith('+') for line in lines[2:]):
            raise ValueError('Unexpected source patch structure')
        name = match[1]
        data = ('\n'.join(line[1:] for line in lines[2:]) + '\n').encode('ascii')
        if identity(data) != fixture['sources'][name] or (sources / name).exists():
            raise ValueError('Prototype source differs: ' + name)
        (sources / name).write_bytes(data)
    if {path.name for path in sources.iterdir()} != set(fixture['sources']) or len(chunks) != 9:
        raise ValueError('Prototype source set is incomplete')
    records = [case for oracle in fixture['original_oracles'] for case in oracle['records']]
    current_oracle = next(oracle for oracle in fixture['original_oracles'] if oracle['host'] == host)
    if unicodedata.unidata_version != current_oracle['unicode']:
        raise ValueError('The actual host Unicode profile differs from the captured profile')
    if identity((root / 'tools/hostbuild.py').read_bytes()) != current_oracle['original_hostbuild']:
        raise ValueError('The original image command source differs from the captured oracle')
    for index, case in enumerate(current_oracle['records']):
        actual = actual_oracle(hostbuild, case, fixture['capture_argv0'])
        if actual != {key: case[key] for key in ('status', 'expected', 'original_stdout', 'original_stderr')}:
            (output / 'original-oracle-difference.json').write_text(json.dumps(
                {'index': index, 'name': case['name'], 'actual': actual, 'original': case},
                indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
            raise ValueError(('Actual original command differs', index, case['name']))
    packet = bytearray(struct.pack('<I', len(records)))
    for case in records:
        packet.extend(struct.pack('<IIIIII', case['unicode'], case['limit'], case['grammar'],
                                  case['basename'], case['capacity'], len(case['argv'])))
        for value in case['argv']:
            data = value.encode('utf-8')
            if b'\0' in data:
                raise ValueError('Unexpected NUL in an original argument')
            packet.extend(struct.pack('<I', len(data)))
            packet.extend(data)
    if identity(packet) != fixture['complete_request']:
        raise ValueError('Complete request differs from the reviewed packet')
    request = output / 'request.bin'
    request.write_bytes(packet)
    manifest = args.manifest or root / ('bootstrap/seeds/i386-' + host + '/manifest.json')
    seed = bootstrap.freeze_seed_inputs(manifest, output / 'seed')
    for path in seed.tools.values():
        signature = path.read_bytes()[:4]
        if not (signature[:2] == b'MZ' if windows else signature == b'\x7fELF'):
            raise ValueError('Use the native host seed manifest')
    inputs = [root / 'tools/hostbuild.py', root / 'toolchain/path_encoding.cc', root / 'toolchain/path_encoding.h',
              root / 'toolchain/hosted/i386-linux/runtime.cc',
              root / ('toolchain/hosted/i386-' + host + '/runtime.cc'),
              root / ('toolchain/hosted/i386-' + host + '/' + ('tool_start.asm' if windows else 'start.asm')),
              Path(__file__), artifact / 'native-image-options.patch', artifact / 'native-image-options-oracles.json.gz']
    inputs.extend((root / 'toolchain/hosted/i386-linux/include').glob('*.h'))
    inputs.extend(sources.iterdir())
    captured = {path: path.read_bytes() for path in inputs}
    sentinels = {name: 'forbidden-host-' + name for name in ('CC', 'CXX', 'CPP', 'AS', 'AR', 'LD', 'NM', 'OBJCOPY', 'NASM')}
    environment = {**os.environ, **sentinels}
    commands = []
    receipt_lock = threading.Lock()
    started = time.monotonic()
    receipt = {'schema': 'cupid.native-image-options-replay.v1', 'status': 'running', 'host': host,
        'python': sys.version, 'unicode': unicodedata.unidata_version, 'commands': commands,
        'prototype_sources': fixture['sources'], 'oracle_fixture': identity(fixture_bytes),
        'producer_sentinels': sentinels, 'seed_manifest': identity(seed.manifest_bytes),
        'tools': {name: identity(path.read_bytes()) for name, path in seed.tools.items()},
        'inputs': {path.relative_to(root).as_posix(): identity(data) for path, data in captured.items()},
        'actual_current_host_oracle_cases': len(current_oracle['records']),
        'actual_argparse_source': identity(inspect.getsource(argparse).encode('utf-8')),
        'actual_original_stdout_and_stderr_equal': True, 'request': identity(packet)}

    def guard():
        bootstrap.require_live_seed_inputs(seed)
        for path, data in captured.items():
            if path.read_bytes() != data:
                raise ValueError('Input changed during replay: ' + str(path))

    def run(label, argv, timeout, *, native_build=False, runtime=False):
        guard()
        with receipt_lock:
            (output / 'running.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
        def memory_limit():
            import resource
            resource.setrlimit(resource.RLIMIT_AS, (33554432, 33554432))
        before = time.monotonic()
        result = subprocess.run(list(map(str, argv)), cwd=root, capture_output=True,
            env=dict(os.environ) if native_build else environment, timeout=timeout,
            preexec_fn=memory_limit if runtime and not windows else None)
        (output / (label + '-stdout.bin')).write_bytes(result.stdout)
        (output / (label + '-stderr.bin')).write_bytes(result.stderr)
        command = {'label': label, 'argv': list(map(str, argv)), 'exit_code': result.returncode,
            'stdout': identity(result.stdout), 'stderr': identity(result.stderr), 'timeout_seconds': timeout,
            'seconds': time.monotonic() - before, 'address_space_bytes': 33554432 if runtime and not windows else None}
        commands.append(command)
        if result.returncode != 0 or result.stdout or result.stderr:
            raise ValueError(command)
        guard()

    try:
        compiler = shutil.which('clang')
        if not compiler:
            raise ValueError('The optional native comparison requires clang')
        fault_definitions = ['calloc=cupidbuild_disk_options_fault_calloc',
            'malloc=cupidbuild_disk_options_fault_malloc', 'free=cupidbuild_disk_options_fault_free',
            *('cupidbuild_disk_options_' + name + '=cupidbuild_disk_options_fault_' + name
              for name in ('open', 'view', 'stage', 'close'))]
        common = [compiler, '-x', 'c', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
            *(['-D_CRT_SECURE_NO_WARNINGS=1'] if windows else []), '-I', root / 'toolchain']
        native_fault = output / 'native-options-fault.o'
        run('native-fault-build', [*common, *(value for definition in fault_definitions for value in ('-D', definition)),
            '-c', sources / 'cupidbuild_disk_options.cc', '-o', native_fault], 180, native_build=True)
        native = output / ('native.' + suffix)
        run('native-build', [*common, '-DCUPID_DISK_OPTIONS_NATIVE=1', sources / 'cupidbuild_disk_options.cc',
            sources / 'cupidbuild_disk_integer.cc', sources / 'cupidbuild_disk_options_contract.cc',
            sources / 'cupidbuild_disk_options_fault_contract.cc', root / 'toolchain/path_encoding.cc',
            '-x', 'none', native_fault, '-o', native], 180, native_build=True)
        objects = output / 'objects'
        objects.mkdir()

        def compile_one(name, source, gnu=False):
            destination = objects / (name + '.o')
            argv = [seed.tools['cupidc'], '--root', root]
            if gnu:
                argv.append('--gnu')
            if name == 'runtime' and windows:
                argv.extend(('-D', '_WIN32=1'))
            if name == 'options-fault':
                for definition in fault_definitions:
                    argv.extend(('-D', definition))
            argv.extend(('-c', '/' + source.relative_to(root).as_posix(), '--include-angle', '/toolchain',
                '--include-angle', '/toolchain/hosted/i386-linux/include',
                '-o', '/' + destination.relative_to(root).as_posix()))
            run('compile-' + name, argv, 360)
            bootstrap._validate_i386_relocatable(destination)

        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
            futures = [executor.submit(compile_one, name, source, gnu) for name, source, gnu in (
                ('options', sources / 'cupidbuild_disk_options.cc', False),
                ('options-fault', sources / 'cupidbuild_disk_options.cc', False),
                ('number', sources / 'cupidbuild_disk_integer.cc', False),
                ('contract', sources / 'cupidbuild_disk_options_contract.cc', False),
                ('fault-probe', sources / 'cupidbuild_disk_options_fault_contract.cc', False),
                ('fgetc', sources / 'fgetc_extension.cc', False),
                ('path', root / 'toolchain/path_encoding.cc', False),
                ('runtime', root / ('toolchain/hosted/i386-' + host + '/runtime.cc'), True))]
            for future in futures:
                future.result()
        startup = root / ('toolchain/hosted/i386-' + host + '/' + ('tool_start.asm' if windows else 'start.asm'))
        run('assemble', [seed.tools['cupidasm'], '-f', 'elf32', startup, '-o', objects / 'start.o'], 120)
        checked = output / ('checked.' + suffix)
        argv = [seed.tools['cupidld'], '-m', 'i386pe' if windows else 'elf_i386', '--text-address',
                '0x00401000' if windows else '0x08048000', '--entry', '_start']
        if windows:
            for library, names in bootstrap.WINDOWS_TOOL_IMPORTS:
                for name in names:
                    argv.extend(('--import', '__imp_' + name + '=' + library + ':' + name))
        argv.extend(('-o', checked, *(objects / name for name in ('start.o', 'options.o', 'options-fault.o',
            'number.o', 'contract.o', 'fault-probe.o', 'fgetc.o', 'path.o', 'runtime.o'))))
        run('link', argv, 180)
        if windows:
            bootstrap._validate_static_i386_pe32(checked, 0x00401000, bootstrap.WINDOWS_TOOL_IMPORTS)
        else:
            bootstrap._validate_static_i386_elf(checked, 0x08048000)
        for path in (*sorted(objects.glob('*.o')), checked):
            run('strict-' + path.name, [seed.tools['cupiddis'], '--require-known', '--require-local-targets',
                '--require-code-anchors', path], 120)
        for role, program in (('native', native), ('checked', checked)):
            destination = output / (role + '-results.bin')
            run(role + '-runtime', [program, request, destination], 60, runtime=True)
            result = destination.read_bytes()
            decode(result, records)
            if identity(result) != fixture['complete_result']:
                raise ValueError('Complete raw result differs: ' + role)
        if (output / 'native-results.bin').read_bytes() != (output / 'checked-results.bin').read_bytes():
            raise ValueError('Complete native and checked result bytes differ')
        guard()
        receipt.update(status='pass', cases_per_caller=len(records), calls=2 * len(records),
            positive_calls=2 * sum(case['status'] == 0 for case in records),
            help_calls=2 * sum(case['status'] == 1 for case in records),
            negative_calls=2 * sum(case['status'] not in (0, 1) for case in records),
            all_owner_allocation_failures_unwind=True, generated_wad_destinations_owned=True,
            argv_path_views_borrowed=True, fixture_io_chunk_bytes=65536,
            complete_result=identity((output / 'checked-results.bin').read_bytes()),
            objects={path.name: identity(path.read_bytes()) for path in objects.glob('*.o')},
            native_image=identity(native.read_bytes()), checked_image=identity(checked.read_bytes()))
    except BaseException as error:
        receipt.update(status='fail', error=repr(error))
        traceback.print_exc()
    receipt['seconds'] = time.monotonic() - started
    (output / 'closed.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
    print(json.dumps({key: receipt.get(key) for key in ('status', 'host', 'calls', 'positive_calls',
        'help_calls', 'negative_calls', 'seconds', 'error')}))
    return 0 if receipt['status'] == 'pass' else 1


if __name__ == '__main__':
    try:
        exit_code = main()
    except Exception as error:
        traceback.print_exc()
        if created_output is not None and not (created_output / 'closed.json').exists():
            (created_output / 'closed.json').write_text(json.dumps({'schema': 'cupid.native-image-options-replay.v1',
                'status': 'fail', 'stage': 'preflight', 'error': repr(error)}, indent=2) + '\n',
                encoding='utf-8', newline='\n')
        exit_code = 1
    raise SystemExit(exit_code)
