from pathlib import Path
import json
import os
import shutil
import subprocess
import sys
import time
import traceback

ROOT = Path(__file__).resolve().parents[2]
BASE = Path(__file__).resolve().parent
OUTPUT = Path(sys.argv[1]).resolve()
assert OUTPUT.is_relative_to(ROOT / 'build') and not OUTPUT.exists()
OUTPUT.mkdir(parents=True)
sys.path.insert(0, str(BASE))
from consumer_audit_common import read, identity, git_bytes, SEED_FILES
sys.path.insert(0, str(ROOT))
from tools import bootstrap_toolchain as bootstrap

windows = os.name == 'nt'
host, suffix = ('windows', 'exe') if windows else ('linux', 'elf')
proof = json.loads(read(ROOT / 'docs/bootstrap/evidence/native-observer-paths-20261010.json'))
baseline = Path(json.loads(read(BASE / 'observer-products.json'))['directory']).resolve()
assert baseline.is_relative_to(ROOT / 'build') and baseline != OUTPUT
baseline_receipt = json.loads(read(baseline / 'closed.json'))
assert baseline_receipt['status'] == 'pass'
assert baseline_receipt['sources'] == proof['prototype_sources']
assert baseline_receipt['objects'] == proof['hosts'][host]['objects']
assert baseline_receipt['checked_images'] == {name.removeprefix('checked-').removesuffix('.' + suffix): facts
    for name, facts in proof['hosts'][host]['images'].items() if name.startswith('checked-')}
sources = OUTPUT / 'source'
sources.mkdir()
source_generation = 'source'
assert len(sys.argv) == 2
for path in sorted((BASE / source_generation).iterdir()):
    shutil.copyfile(path, sources / path.name)
for name in ('path_encoding.cc', 'path_encoding.h'):
    shutil.copyfile(ROOT / 'toolchain' / name, sources / name)
shutil.copyfile(baseline / 'source/cupidbuild_host.h', sources / 'cupidbuild_host.h')
source_controls = {path.name: identity(read(path)) for path in sources.iterdir()}
assert source_controls['cupidbuild_host.h'] == proof['prototype_sources']['cupidbuild_host.h']
seed = bootstrap.freeze_seed_inputs(ROOT / ('bootstrap/seeds/i386-' + host + '/manifest.json'), OUTPUT / 'seed')
sentinels = {name: 'forbidden-host-' + name for name in ('CC', 'CXX', 'CPP', 'AS', 'AR', 'LD', 'NM', 'OBJCOPY', 'NASM')}
environment = {**os.environ, **sentinels}
baseline_controls = {path: read(path) for path in (baseline / 'native-host.o', baseline / 'native-path.o',
    baseline / 'source/cupidbuild_host.cc', baseline / 'closed.json')}
assert identity(baseline_controls[baseline / 'source/cupidbuild_host.cc']) == proof['prototype_sources']['cupidbuild_host.cc']
for name, facts in baseline_receipt['objects'].items():
    path = baseline / 'objects' / name
    data = read(path)
    assert identity(data) == facts
    baseline_controls[path] = data
commands = []
started = time.monotonic()
receipt = {'schema': 'cupid.native-source-path-build.v1', 'status': 'running', 'host': host,
    'sources': source_controls, 'producer_sentinels': sentinels, 'commands': commands,
    'runner': identity(read(Path(__file__))), 'seed_manifest': identity(seed.manifest_bytes),
    'tools': {name: identity(read(path)) for name, path in seed.tools.items()},
    'baseline_build': identity(read(baseline / 'closed.json')),
    'baseline_objects': {path.name: identity(data) for path, data in baseline_controls.items()},
    'source_revision': proof['source_revision'], 'source_snapshot_sha256': proof['source_snapshot_sha256']}
support_controls = {path: read(path) for path in
    (BASE / 'consumer_audit_common.py', ROOT / 'tools/bootstrap_toolchain.py')}
if windows:
    for name in ('native_utf8.cc', 'native_utf8.h'):
        support_controls[ROOT / 'toolchain' / name] = read(ROOT / 'toolchain' / name)
receipt['support_inputs'] = {str(path): identity(data) for path, data in support_controls.items()}


def guard(committed=False):
    bootstrap.require_live_seed_inputs(seed)
    for path, data in support_controls.items(): assert read(path) == data
    for name, facts in source_controls.items():
        assert identity(read(sources / name)) == facts
    for path, data in baseline_controls.items():
        assert read(path) == data
    for name, facts in proof['source_inputs'].items():
        data = read(ROOT / name)
        assert identity(data) == facts
        if committed: assert data == git_bytes(proof['source_revision'], name)
    for name in SEED_FILES: assert read(ROOT / name) == git_bytes('HEAD', name)


def run(label, argv, timeout, native=False):
    guard()
    (OUTPUT / 'running.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
    before = time.monotonic()
    result = subprocess.run(list(map(str, argv)), cwd=ROOT, env=environment,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    (OUTPUT / (label + '-stdout.bin')).write_bytes(result.stdout)
    (OUTPUT / (label + '-stderr.bin')).write_bytes(result.stderr)
    commands.append({'label': label, 'argv': list(map(str, argv)), 'native': native,
        'timeout_seconds': timeout, 'exit_code': result.returncode,
        'stdout': identity(result.stdout), 'stderr': identity(result.stderr), 'seconds': time.monotonic() - before})
    assert result.returncode == 0 and result.stdout == result.stderr == b'', commands[-1]
    guard()


try:
    guard(True)
    native_template = next(row for row in baseline_receipt['commands'] if row['label'] == 'native-link-resolved-parent')
    native_program = OUTPUT / ('native-source-path.' + suffix)
    native_argv = []
    for argument in native_template['argv']:
        if Path(argument).name == 'unc_resolved_parent_contract.cc':
            native_argv.extend((sources / 'source_path_contract.cc', sources / 'cupidbuild_source_path.cc'))
        elif Path(argument).name == 'native-resolved-parent.' + suffix:
            native_argv.append(native_program)
        else: native_argv.append(argument)
    native_argv[1:1] = ['-I', sources]
    if windows:
        native_compiler = native_argv[0]
        native_common = [native_compiler, '-x', 'c', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-D_CRT_SECURE_NO_WARNINGS=1', '-DCUPID_NATIVE_UTF8_ENABLE=1', '-I', sources, '-I', ROOT / 'toolchain']
        native_host = OUTPUT / 'native-host-utf8.o'
        native_utf8 = OUTPUT / 'native-utf8.o'
        run('native-compile-host-utf8', [*native_common, '-c', baseline / 'source/cupidbuild_host.cc', '-o', native_host], 180, native=True)
        run('native-compile-utf8', [*native_common, '-c', ROOT / 'toolchain/native_utf8.cc', '-o', native_utf8], 180, native=True)
        native_argv = [native_host if Path(str(argument)).name == 'native-host.o' else argument for argument in native_argv]
        native_argv[1:1] = ['-DNATIVE_SOURCE_PATH_WINDOWS=1']
        native_argv.append(native_utf8)
    if not windows:
        native_host = OUTPUT / 'native-host.o'
        native_host_template = next(row for row in baseline_receipt['commands'] if row['label'] == 'native-compile-host')
        native_host_argv = [native_host if Path(argument).name == 'native-host.o' else argument for argument in native_host_template['argv']]
        run('native-compile-host', native_host_argv, 180, native=True)
        native_argv = [native_host if Path(str(argument)).name == 'native-host.o' else argument for argument in native_argv]
    native_path = OUTPUT / 'native-path.o'
    native_path_template = next(row for row in baseline_receipt['commands'] if row['label'] == 'native-compile-path')
    native_path_argv = []
    for argument in native_path_template['argv']:
        if Path(argument).name == 'native-path.o': native_path_argv.append(native_path)
        elif Path(argument).name == 'path_encoding.cc': native_path_argv.append(sources / 'path_encoding.cc')
        else: native_path_argv.append(argument)
    run('native-compile-path', native_path_argv, 180, native=True)
    native_argv = [native_path if Path(str(argument)).name == 'native-path.o' else argument for argument in native_argv]
    run('native-link-source-path', native_argv, 180, native=True)
    objects = OUTPUT / 'objects'
    objects.mkdir()
    for label, name in (('source-path', 'cupidbuild_source_path.cc'),
                        ('source-contract', 'source_path_contract.cc'), ('path-encoding', 'path_encoding.cc')):
        output = objects / (label + '.o')
        run('compile-' + label, [seed.tools['cupidc'], '--root', ROOT,
            '-c', '/' + (sources / name).relative_to(ROOT).as_posix(),
            '-I', '/' + sources.relative_to(ROOT).as_posix(), '-I', '/toolchain',
            '--include-angle', '/toolchain/hosted/i386-linux/include',
            '-o', '/' + output.relative_to(ROOT).as_posix()], 360)
        bootstrap._validate_i386_relocatable(output)
    link_template = next(row for row in baseline_receipt['commands'] if row['label'] == 'link-resolved-parent')
    checked_program = OUTPUT / ('checked-source-path.' + suffix)
    checked_argv = []
    for argument in link_template['argv']:
        name = Path(argument).name
        if name == 'resolved-parent.o': checked_argv.append(objects / 'source-contract.o')
        elif name == 'path_encoding.o': checked_argv.append(objects / 'path-encoding.o')
        elif name == 'checked-resolved-parent.' + suffix: checked_argv.append(checked_program)
        else: checked_argv.append(argument)
    checked_argv[0] = seed.tools['cupidld']
    checked_argv.append(objects / 'source-path.o')
    if not windows: checked_argv.append(objects / 'path-encoding.o')
    run('link-source-path', checked_argv, 180)
    if windows:
        plan = bootstrap._windows_build_plan(json.loads(read(ROOT / 'bootstrap/seeds/i386-linux/manifest.json'))['build_plan'],
            utf8=True, user_link_aliases=True)
        imports = tuple((library, tuple(row['procedure'] for row in plan['imports']['cupidbuild']
            if row['library'] == library)) for library in sorted({row['library'] for row in plan['imports']['cupidbuild']}))
        bootstrap._validate_static_i386_pe32(checked_program, 0x00401000, imports)
    else: bootstrap._validate_static_i386_elf(checked_program, 0x08048000)
    for path in (*sorted(objects.glob('*.o')), checked_program):
        run('strict-' + path.name, [seed.tools['cupiddis'], '--require-known', '--require-local-targets',
            '--require-code-anchors', path], 120)
    guard(True)
    receipt.update(status='pass', normal_99_raw_committed_inputs_unchanged=True,
        installed_root_seed_files_unchanged=15, objects={path.name: identity(read(path)) for path in objects.glob('*.o')},
        programs={role: identity(read(path)) for role, path in (('native', native_program), ('checked', checked_program))})
except BaseException as error:
    receipt.update(status='fail', error=repr(error))
    traceback.print_exc()
receipt['seconds'] = time.monotonic() - started
(OUTPUT / 'closed.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
print(json.dumps({key: receipt.get(key) for key in ('status', 'host', 'seconds', 'error')}))
raise SystemExit(0 if receipt['status'] == 'pass' else 1)
