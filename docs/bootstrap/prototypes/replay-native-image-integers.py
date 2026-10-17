"""Build the held integer/byte-reader prototype and replay both frozen oracles."""
import argparse
import concurrent.futures
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time
import traceback
import unicodedata

created_output = None


def identity(data):
    return {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def oracle(case):
    previous = sys.get_int_max_str_digits()
    try:
        sys.set_int_max_str_digits(case['limit'])
        if case['profile'] not in (15, 16):
            return [3, 0, 0, 0, 0, 0]
        try:
            text = bytes.fromhex(case['bytes']).decode('utf-8')
        except UnicodeDecodeError:
            return [2, 0, 0, 0, 0, 0]
        try:
            value = int(text)
        except ValueError as error:
            return [4 if 'Exceeds the limit' in str(error) else 1, 0, 0, 0, 0, 0]
        magnitude = abs(value)
        return [0, min(magnitude, 0xffffffff), sum(char.isdecimal() for char in text),
                int(magnitude <= 0xffffffff), int(value < 0), int(value == 0)]
    finally:
        sys.set_int_max_str_digits(previous)


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
    windows = os.name == 'nt'
    host = 'windows' if windows else 'linux'
    suffix = 'exe' if windows else 'elf'
    manifest = args.manifest or root / ('bootstrap/seeds/i386-' + host + '/manifest.json')
    fixture_bytes = (artifact / 'native-image-integer-oracles.json.gz').read_bytes()
    fixture = json.loads(gzip.decompress(fixture_bytes))
    proof = json.loads((artifact.parent / 'evidence/native-image-integers-20261010.json').read_bytes())
    if identity(fixture_bytes) != proof['oracle_fixture']:
        raise ValueError('Oracle fixture differs from the recorded artifact')
    patch_bytes = (artifact / 'native-image-integers.patch').read_bytes()
    if identity(patch_bytes) != proof['artifact_patch']:
        raise ValueError('Source patch differs from the recorded artifact')
    sources = output / 'source'
    sources.mkdir()
    chunks = patch_bytes.decode('ascii').split('--- /dev/null\n')[1:]
    for chunk in chunks:
        lines = chunk.splitlines()
        match = re.fullmatch(r'\+\+\+ b/toolchain/prototypes/native_image_integers/([a-z_]+\.(?:cc|h|inc))', lines[0])
        hunk = re.fullmatch(r'@@ -0,0 \+1,(\d+) @@', lines[1])
        if not match or not hunk or int(hunk[1]) != len(lines[2:]) or not all(line.startswith('+') for line in lines[2:]):
            raise ValueError('Unexpected prototype patch structure')
        name = match[1]
        data = ('\n'.join(line[1:] for line in lines[2:]) + '\n').encode('ascii')
        if identity(data) != fixture['sources'][name] or (sources / name).exists():
            raise ValueError('Prototype source differs: ' + name)
        (sources / name).write_bytes(data)
    if {path.name for path in sources.iterdir()} != set(fixture['sources']) or len(chunks) != 6:
        raise ValueError('Prototype source set is incomplete')
    if unicodedata.unidata_version != fixture['profiles'][host]['unicode']:
        raise ValueError('Replay requires the recorded current-host Unicode profile')
    records = fixture['records']
    if len(records) != 16704 or len({case['name'] for case in records}) != len(records):
        raise ValueError('Frozen oracle selection is incomplete')
    for case in records:
        if case['oracle_host'] == host and oracle(case) != case['expected']:
            raise ValueError('Actual installed integer parser differs: ' + case['name'])
    packet = bytearray(struct.pack('<I', len(records)))
    for case in records:
        payload = bytes.fromhex(case['bytes'])
        packet.extend(struct.pack('<III', case['profile'], case['limit'], len(payload)))
        packet.extend(payload)
    expected = ''.join(' '.join(map(str, case['expected'])) + '\n' for case in records).encode('ascii')
    request = output / 'request.bin'
    request.write_bytes(packet)
    (output / 'expected.bin').write_bytes(expected)
    byte_input = output / 'byte-input.bin'
    byte_input.write_bytes(bytes(range(256)))
    seed = bootstrap.freeze_seed_inputs(manifest, output / 'seed')
    for path in seed.tools.values():
        signature = path.read_bytes()[:4]
        native_format = signature[:2] == b'MZ' if windows else signature == b'\x7fELF'
        if not native_format:
            raise ValueError('Use the native host seed manifest')
    inputs = [root / 'toolchain/hosted/i386-linux/runtime.cc',
              root / ('toolchain/hosted/i386-' + host + '/runtime.cc'),
              root / ('toolchain/hosted/i386-' + host + '/' + ('tool_start.asm' if windows else 'start.asm')),
              Path(__file__), artifact / 'native-image-integers.patch',
              artifact / 'native-image-integer-oracles.json.gz']
    inputs.extend((root / 'toolchain/hosted/i386-linux/include').glob('*.h'))
    inputs.extend(sources.iterdir())
    captured = {path: path.read_bytes() for path in inputs}
    sentinels = {name: 'forbidden-host-' + name for name in ('CC', 'CXX', 'CPP', 'AS', 'AR', 'LD', 'NM', 'OBJCOPY', 'NASM')}
    environment = {**os.environ, **sentinels}
    commands = []
    started = time.monotonic()

    def guard():
        bootstrap.require_live_seed_inputs(seed)
        for path, data in captured.items():
            if path.read_bytes() != data:
                raise ValueError('Input changed during replay: ' + str(path))

    def run(label, argv, timeout, *, native_build=False, runtime=False):
        guard()
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
                   'stdout': identity(result.stdout), 'stderr': identity(result.stderr),
                   'timeout_seconds': timeout, 'seconds': time.monotonic() - before,
                   'address_space_bytes': 33554432 if runtime and not windows else None}
        commands.append(command)
        if result.returncode != 0 or result.stdout or result.stderr:
            raise ValueError(command)
        guard()

    receipt = {'schema': 'cupid.native-image-integer-replay.v1', 'status': 'running', 'host': host,
               'unicode': unicodedata.unidata_version, 'python': sys.version,
               'commands': commands, 'prototype_sources': fixture['sources'],
               'oracle_fixture': identity(fixture_bytes), 'producer_sentinels': sentinels,
               'seed_manifest': identity(seed.manifest_bytes),
               'tools': {name: identity(path.read_bytes()) for name, path in seed.tools.items()},
               'inputs': {path.relative_to(root).as_posix(): identity(data) for path, data in captured.items()}}
    try:
        compiler = shutil.which('clang')
        if not compiler:
            raise ValueError('The optional native oracle replay requires clang')
        native = output / ('native.' + suffix)
        run('native-build', [compiler, '-x', 'c', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-DCUPID_DISK_INTEGER_NATIVE=1', *(['-D_CRT_SECURE_NO_WARNINGS=1'] if windows else []),
            sources / 'cupidbuild_disk_integer.cc', sources / 'cupidbuild_disk_integer_contract.cc', '-o', native],
            180, native_build=True)
        objects = output / 'objects'
        objects.mkdir()

        def compile_one(name, source, *, gnu=False):
            destination = objects / (name + '.o')
            argv = [seed.tools['cupidc'], '--root', root]
            if gnu:
                argv.append('--gnu')
            if gnu and windows:
                argv.extend(('-D', '_WIN32=1'))
            argv.extend(('-c', '/' + source.relative_to(root).as_posix(),
                         '--include-angle', '/toolchain/hosted/i386-linux/include',
                         '-o', '/' + destination.relative_to(root).as_posix()))
            run('compile-' + name, argv, 360)
            bootstrap._validate_i386_relocatable(destination)

        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
            futures = [executor.submit(compile_one, name, source, gnu=gnu) for name, source, gnu in (
                ('number', sources / 'cupidbuild_disk_integer.cc', False),
                ('contract', sources / 'cupidbuild_disk_integer_contract.cc', False),
                ('fgetc', sources / 'fgetc_extension.cc', False),
                ('runtime', root / ('toolchain/hosted/i386-' + host + '/runtime.cc'), True))]
            for future in futures:
                future.result()
        startup = root / ('toolchain/hosted/i386-' + host + '/' + ('tool_start.asm' if windows else 'start.asm'))
        run('assemble', [seed.tools['cupidasm'], '-f', 'elf32', startup, '-o', objects / 'start.o'], 120)
        checked = output / ('checked.' + suffix)
        argv = [seed.tools['cupidld'], '-m', 'i386pe' if windows else 'elf_i386',
                '--text-address', '0x00401000' if windows else '0x08048000', '--entry', '_start']
        if windows:
            for library, names in bootstrap.WINDOWS_TOOL_IMPORTS:
                for name in names:
                    argv.extend(('--import', '__imp_' + name + '=' + library + ':' + name))
        argv.extend(('-o', checked, *(objects / name for name in ('start.o', 'contract.o', 'number.o', 'fgetc.o', 'runtime.o'))))
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
            run(role + '-runtime', [program, request, destination, byte_input], 60, runtime=True)
            if destination.read_bytes() != expected:
                raise ValueError('Complete oracle results differ: ' + role)
        guard()
        receipt.update(status='pass', parser_calls=2 * len(records), cases_per_caller=len(records),
            positive_calls=2 * sum(case['expected'][0] == 0 for case in records),
            negative_calls=2 * sum(case['expected'][0] != 0 for case in records),
            actual_current_host_oracle_cases=sum(case['oracle_host'] == host for case in records),
            expected=identity(expected), native_result=identity((output / 'native-results.bin').read_bytes()),
            checked_result=identity((output / 'checked-results.bin').read_bytes()),
            objects={path.name: identity(path.read_bytes()) for path in objects.glob('*.o')},
            native_image=identity(native.read_bytes()), checked_image=identity(checked.read_bytes()),
            fgetc_all_unsigned_byte_values=256, fgetc_repeated_eof_and_clear_seek_recovery=True)
    except BaseException as error:
        receipt.update(status='fail', error=repr(error))
        traceback.print_exc()
    receipt['seconds'] = time.monotonic() - started
    (output / 'closed.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
    print(json.dumps({key: receipt.get(key) for key in ('status', 'host', 'parser_calls', 'positive_calls', 'negative_calls', 'seconds', 'error')}))
    return 0 if receipt['status'] == 'pass' else 1


if __name__ == '__main__':
    try:
        exit_code = main()
    except Exception as error:
        traceback.print_exc()
        if created_output is not None:
            closed = created_output / 'closed.json'
            if not closed.exists():
                closed.write_text(json.dumps({'schema': 'cupid.native-image-integer-replay.v1',
                    'status': 'fail', 'stage': 'preflight', 'error': repr(error)}, indent=2) + '\n',
                    encoding='utf-8', newline='\n')
        exit_code = 1
    raise SystemExit(exit_code)
