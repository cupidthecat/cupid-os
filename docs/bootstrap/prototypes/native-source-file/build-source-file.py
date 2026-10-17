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
assert OUTPUT.parent == BASE and not OUTPUT.exists()
OUTPUT.mkdir()
sys.path.insert(0, str(BASE))
from consumer_audit_common import read, identity, git_bytes, SEED_FILES
sys.path.insert(0, str(ROOT))
from tools import bootstrap_toolchain as bootstrap

windows = os.name == 'nt'
host, suffix = ('windows', 'exe') if windows else ('linux', 'elf')
source_proof = json.loads(read(BASE / 'source-controls.json'))
root_proof_bytes = read(ROOT / 'docs/bootstrap/evidence/native-source-root-20261010.json')
root_proof = json.loads(root_proof_bytes)
prerequisite = json.loads(read(BASE / 'root-prerequisite.json'))
prerequisite_output = Path(prerequisite['output'])
prerequisite_bytes = read(prerequisite_output / 'closed.json')
assert identity(prerequisite_bytes) == prerequisite['closed']
assert json.loads(prerequisite_bytes)['status'] == 'pass'
baseline = prerequisite_output / ('products-' + host + '1')
baseline_bytes = read(baseline / 'closed.json')
baseline_record = json.loads(baseline_bytes)
assert baseline_record['status'] == 'pass' and baseline_record['objects'] == root_proof['hosts'][host]['objects']
sources = OUTPUT / 'source'
sources.mkdir()
for path in sorted((BASE / 'source1').iterdir()): shutil.copyfile(path, sources / path.name)
source_controls = {p.name: identity(read(p)) for p in sources.iterdir()}
for name, facts in root_proof['prototype_sources'].items(): assert source_controls[name] == facts
seed = bootstrap.freeze_seed_inputs(ROOT / ('bootstrap/seeds/i386-' + host + '/manifest.json'), OUTPUT / 'seed')
tools = seed.tools
sentinels = {name: 'forbidden-host-' + name for name in ('CC', 'CXX', 'CPP', 'AS', 'AR', 'LD', 'NM', 'OBJCOPY', 'NASM')}
environment = {**os.environ, **sentinels}
commands = []
started = time.monotonic()
receipt = {'schema': 'cupid.native-source-file-build.v1', 'status': 'running', 'host': host,
    'source_revision': source_proof['source_revision'], 'source_snapshot_sha256': source_proof['source_snapshot_sha256'],
    'sources': source_controls, 'root_prerequisite': identity(root_proof_bytes), 'root_build': identity(baseline_bytes),
    'baseline_objects': baseline_record['objects'], 'producer_sentinels': sentinels, 'commands': commands,
    'tools': {name: identity(read(path)) for name, path in tools.items()}, 'runner': identity(read(Path(__file__)))}

def guard(committed=False):
    bootstrap.require_live_seed_inputs(seed)
    assert read(ROOT / 'docs/bootstrap/evidence/native-source-root-20261010.json') == root_proof_bytes
    assert read(baseline / 'closed.json') == baseline_bytes
    assert read(prerequisite_output / 'closed.json') == prerequisite_bytes
    for name, facts in baseline_record['objects'].items(): assert identity(read(baseline / 'objects' / name)) == facts
    for name, facts in source_controls.items(): assert identity(read(sources / name)) == facts
    for name, facts in source_proof['source_inputs'].items():
        data = read(ROOT / name)
        assert identity(data) == facts
        if committed: assert data == git_bytes(source_proof['source_revision'], name)
    for name, facts in root_proof['support_inputs'].items(): assert identity(read(ROOT / name)) == facts
    for name in SEED_FILES: assert read(ROOT / name) == git_bytes('HEAD', name)

def run(label, argv, timeout, native=False):
    guard()
    (OUTPUT / 'running.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
    before = time.monotonic()
    result = subprocess.run(list(map(str, argv)), cwd=ROOT, env=dict(os.environ) if native else environment,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    (OUTPUT / (label + '-stdout.bin')).write_bytes(result.stdout)
    (OUTPUT / (label + '-stderr.bin')).write_bytes(result.stderr)
    commands.append({'label': label, 'argv': list(map(str, argv)), 'timeout_seconds': timeout, 'native': native,
        'exit_code': result.returncode, 'seconds': time.monotonic() - before,
        'stdout': identity(result.stdout), 'stderr': identity(result.stderr)})
    assert result.returncode == 0 and result.stdout == result.stderr == b'', commands[-1]
    guard()

try:
    guard(True)
    compiler = shutil.which('clang')
    assert compiler
    common = [compiler, '-x', 'c', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        '-D_CRT_SECURE_NO_WARNINGS=1', '-I', sources, '-I', ROOT / 'toolchain']
    if windows: common.append('-DCUPID_NATIVE_UTF8_ENABLE=1')
    native_objects = []
    for name in ('cupidbuild_host', 'path_encoding', 'cupidbuild_source_path', 'cupidbuild_source_file'):
        destination = OUTPUT / ('native-' + name + '.o')
        run('native-compile-' + name, [*common, '-c', sources / (name + '.cc'), '-o', destination], 180, native=True)
        native_objects.append(destination)
    if windows:
        destination = OUTPUT / 'native-utf8.o'
        run('native-compile-utf8', [*common, '-c', ROOT / 'toolchain/native_utf8.cc', '-o', destination], 180, native=True)
        native_objects.append(destination)
    native_image = OUTPUT / ('native-source-file.' + suffix)
    run('native-link-source-file', [*common, sources / 'source_file_contract.cc', '-x', 'none', *native_objects,
        *(['-lntdll'] if windows else []), '-o', native_image], 180, native=True)
    objects = OUTPUT / 'objects'
    objects.mkdir()
    new_sources = [('source-path', 'cupidbuild_source_path.cc'), ('source-file', 'cupidbuild_source_file.cc'),
        ('source-file-contract', 'source_file_contract.cc')]
    if not windows: new_sources.append(('path-encoding', 'path_encoding.cc'))
    new_names = tuple(name for name, source in new_sources)
    for name, source in new_sources:
        destination = objects / (name + '.o')
        argv = [tools['cupidc'], '--root', ROOT, '-c', '/' + (sources / source).relative_to(ROOT).as_posix(),
            '-I', '/' + sources.relative_to(ROOT).as_posix(), '-I', '/toolchain',
            '--include-angle', '/toolchain/hosted/i386-linux/include', '-o', '/' + destination.relative_to(ROOT).as_posix()]
        run('compile-' + name, argv, 360)
        bootstrap._validate_i386_relocatable(destination)
    plan = json.loads(read(ROOT / 'bootstrap/seeds/i386-linux/manifest.json'))['build_plan']
    if windows: plan = bootstrap._windows_build_plan(plan, utf8=True, user_link_aliases=True)
    shared = [row['name'] for row in plan['sources'] if row['name'] in {
        'runtime', 'publication_runtime', 'cupidbuild_host', 'path_encoding', 'windows_utf8_build'}]
    assembly = plan.get('assembly_sources', [{'name': 'start', 'path': '/toolchain/hosted/i386-linux/start.asm'}])
    order = ['start', *new_names, *shared, *(row['name'] for row in assembly if row['name'] != 'start')]
    obj = {name: (objects if name in new_names else baseline / 'objects') / (name + '.o') for name in order}
    checked_image = OUTPUT / ('checked-source-file.' + suffix)
    if windows:
        argv = bootstrap._windows_link_arguments('cupidbuild', checked_image, obj, order, utf8=True, user_link_aliases=True)
    else:
        argv = ['-m', 'elf_i386', '--text-address', '0x08048000', '--entry', '_start', '-o', checked_image,
            *(obj[name] for name in order)]
    run('link-source-file', [tools['cupidld'], *argv], 180)
    if windows:
        imports = tuple((library, tuple(row['procedure'] for row in plan['imports']['cupidbuild'] if row['library'] == library))
            for library in sorted({row['library'] for row in plan['imports']['cupidbuild']}))
        bootstrap._validate_static_i386_pe32(checked_image, 0x00401000, imports)
    else: bootstrap._validate_static_i386_elf(checked_image, 0x08048000)
    for path in (*sorted(objects.glob('*.o')), checked_image):
        run('strict-' + path.name, [tools['cupiddis'], '--require-known', '--require-local-targets', '--require-code-anchors', path], 120)
    guard(True)
    receipt.update(status='pass', objects={path.name: identity(read(path)) for path in objects.glob('*.o')},
        native_image=identity(read(native_image)), checked_image=identity(read(checked_image)))
except BaseException as error:
    receipt.update(status='fail', error=repr(error))
    traceback.print_exc()
receipt['seconds'] = time.monotonic() - started
(OUTPUT / 'closed.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
print(json.dumps({k: receipt.get(k) for k in ('status', 'host', 'seconds', 'error')}))
raise SystemExit(0 if receipt['status'] == 'pass' else 1)
