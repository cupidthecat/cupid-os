from pathlib import Path
import concurrent.futures
import json
import os
import shutil
import subprocess
import sys
import threading
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
from tests import test_cupidbuild_observer as legacy

windows = os.name == 'nt'
host, short, suffix = ('windows', 'w', 'exe') if windows else ('linux', 'l', 'elf')
qualification = json.loads(read(BASE / 'source-controls.json'))
assert qualification['status'] == 'pass'
sources = OUTPUT / 'source'
sources.mkdir()
for path in sorted((BASE / 'source').iterdir()):
    shutil.copyfile(path, sources / path.name)
(sources / 'legacy_observer_contract.cc').write_text(legacy.CALLER, encoding='ascii', newline='\n')
shutil.copyfile(ROOT / 'toolchain/tests/cupidbuild_stream_contract.cc', sources / 'legacy_stream_contract.cc')
source_controls = {path.name: identity(read(path)) for path in sources.iterdir()}
tools = {name: Path(path) for name, path in json.loads(read(BASE / 'checked-tools.json')).items()}

tool_controls = {name: identity(read(path)) for name, path in tools.items()}
sentinels = {name: 'forbidden-host-' + name for name in ('CC', 'CXX', 'CPP', 'AS', 'AR', 'LD', 'NM', 'OBJCOPY', 'NASM')}
environment = {**os.environ, **sentinels}
lock = threading.Lock()
commands = []
started = time.monotonic()
receipt = {'schema': 'cupid.native-retained-observer-build.v1', 'status': 'running', 'host': host,
    'source_revision': qualification['source_revision'], 'source_snapshot_sha256': qualification['source_snapshot_sha256'],
    'sources': source_controls, 'qualified_tools': tool_controls, 'producer_sentinels': sentinels,
    'commands': commands, 'runner': identity(read(Path(__file__)))}

def guard(committed=False):
    for name, facts in source_controls.items():
        assert identity(read(sources / name)) == facts
    for name, facts in tool_controls.items():
        assert identity(read(tools[name])) == facts
    for name, facts in qualification['source_inputs'].items():
        data = read(ROOT / name)
        assert identity(data) == facts
        if committed:
            assert data == git_bytes(qualification['source_revision'], name)
    for name in SEED_FILES:
        assert read(ROOT / name) == git_bytes('HEAD', name)

def run(label, argv, timeout, native=False):
    guard()
    with lock:
        (OUTPUT / 'running.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
    before = time.monotonic()
    result = subprocess.run(list(map(str, argv)), cwd=ROOT, env=dict(os.environ) if native else environment,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    (OUTPUT / (label + '-stdout.bin')).write_bytes(result.stdout)
    (OUTPUT / (label + '-stderr.bin')).write_bytes(result.stderr)
    commands.append({'label': label, 'argv': list(map(str, argv)), 'exit_code': result.returncode,
        'timeout_seconds': timeout, 'native': native, 'stdout': identity(result.stdout),
        'stderr': identity(result.stderr), 'seconds': time.monotonic() - before})
    assert result.returncode == 0 and result.stdout == result.stderr == b'', commands[-1]
    guard()

try:
    guard(True)
    compiler = shutil.which('clang')
    assert compiler
    native_host = OUTPUT / 'native-host.o'
    native_path = OUTPUT / 'native-path.o'
    common = [compiler, '-x', 'c', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        '-D_CRT_SECURE_NO_WARNINGS=1', '-I', sources, '-I', ROOT / 'toolchain']
    for name, source, destination in (('host', sources / 'cupidbuild_host.cc', native_host),
                                     ('path', sources / 'path_encoding.cc', native_path)):
        run('native-compile-' + name, [*common, '-c', source, '-o', destination], 180, native=True)
    contracts = {'retained': sources / 'retained_observer_contract.cc',
                 'legacy-observer': sources / 'legacy_observer_contract.cc',
                 'legacy-stream': sources / 'legacy_stream_contract.cc',
                 'missing-unc': sources / 'observer_missing_unc_contract.cc',
                 'resolved-parent': sources / 'unc_resolved_parent_contract.cc'}
    native_images = {}
    for name, source in contracts.items():
        destination = OUTPUT / ('native-' + name + '.' + suffix)
        extra = ['-DNATIVE_OBSERVER_WINDOWS_WRITER=1', '-lntdll'] if windows else []
        run('native-link-' + name, [*common, source, '-x', 'none', native_host, native_path,
            *extra, '-o', destination], 180, native=True)
        native_images[name] = identity(read(destination))
    plan = json.loads(read(ROOT / 'bootstrap/seeds/i386-linux/manifest.json'))['build_plan']
    if windows:
        plan = bootstrap._windows_build_plan(plan, utf8=True, user_link_aliases=True)
    selected = [row for row in plan['sources'] if row['name'] in {
        'runtime', 'publication_runtime', 'cupidbuild_host', 'path_encoding', 'windows_utf8_build'}]
    objects = OUTPUT / 'objects'
    objects.mkdir()
    rows = []
    for row in selected:
        source = sources / Path(row['path']).name if row['name'] in ('cupidbuild_host', 'path_encoding') else ROOT / row['path'].lstrip('/')
        rows.append((row['name'], source, row.get('definitions', []), row.get('gnu_extensions', False)))
    rows.extend((name, source, [], False) for name, source in contracts.items())
    def compile_one(name, source, definitions, gnu):
        output = objects / (name + '.o')
        argv = [tools['cupidc'], '--root', ROOT]
        if gnu:
            argv.append('--gnu')
        for definition in definitions:
            argv.extend(('-D', definition))
        argv.extend(('-c', '/' + source.relative_to(ROOT).as_posix(), '-I', '/' + sources.relative_to(ROOT).as_posix(),
            '-I', '/toolchain', '--include-angle', '/toolchain/hosted/i386-linux/include',
            '-o', '/' + output.relative_to(ROOT).as_posix()))
        run('compile-' + name, argv, 360)
        bootstrap._validate_i386_relocatable(output)
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
        futures = [executor.submit(compile_one, *row) for row in rows]
        for future in futures:
            future.result()
    assembly = plan.get('assembly_sources', [{'name': 'start', 'path': '/toolchain/hosted/i386-linux/start.asm'}])
    for row in assembly:
        run('assemble-' + row['name'], [tools['cupidasm'], '-f', 'elf32', ROOT / row['path'].lstrip('/'),
            '-o', objects / (row['name'] + '.o')], 120)
    checked_images = {}
    shared = [row['name'] for row in selected]
    for name in contracts:
        destination = OUTPUT / ('checked-' + name + '.' + suffix)
        order = ['start', name, *shared, *(row['name'] for row in assembly if row['name'] != 'start')]
        obj = {key: objects / (key + '.o') for key in order}
        if windows:
            argv = bootstrap._windows_link_arguments('cupidbuild', destination, obj, order,
                utf8=True, user_link_aliases=True)
        else:
            argv = ['-m', 'elf_i386', '--text-address', '0x08048000', '--entry', '_start',
                '-o', destination, *(obj[key] for key in order)]
        run('link-' + name, [tools['cupidld'], *argv], 180)
        if windows:
            imports = tuple((library, tuple(row['procedure'] for row in plan['imports']['cupidbuild']
                if row['library'] == library)) for library in sorted({row['library'] for row in plan['imports']['cupidbuild']}))
            bootstrap._validate_static_i386_pe32(destination, 0x00401000, imports)
        else:
            bootstrap._validate_static_i386_elf(destination, 0x08048000)
        checked_images[name] = identity(read(destination))
    for path in (*sorted(objects.glob('*.o')), *(OUTPUT / ('checked-' + name + '.' + suffix) for name in contracts)):
        run('strict-' + path.name, [tools['cupiddis'], '--require-known', '--require-local-targets',
            '--require-code-anchors', path], 120)
    guard(True)
    receipt.update(status='pass', native_images=native_images, checked_images=checked_images,
        objects={path.name: identity(read(path)) for path in objects.glob('*.o')},
        normal_99_raw_committed_inputs_unchanged=True, installed_root_seed_files_unchanged=15)
except BaseException as error:
    receipt.update(status='fail', error=repr(error))
    traceback.print_exc()
receipt['seconds'] = time.monotonic() - started
(OUTPUT / 'closed.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
print(json.dumps({key: receipt.get(key) for key in ('status', 'host', 'seconds', 'error')}))
raise SystemExit(0 if receipt['status'] == 'pass' else 1)
