from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
import traceback

ROOT = Path(__file__).resolve().parents[2]
BASE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
from tools import bootstrap_toolchain as bootstrap

windows = os.name == 'nt'
host, suffix = ('windows', 'exe') if windows else ('linux', 'elf')
common = BASE / ('common-products-' + host + '1')
common_bytes = (common / 'closed.json').read_bytes()
accepted = json.loads(common_bytes)
assert accepted['status'] == 'pass'
output = BASE / ('common-file-request-group-products-' + host + '1')
assert not output.exists()
output.mkdir()
source = BASE / 'common-source'
fixture = BASE / 'file_request_group_contract.cc'
fixture_bytes = fixture.read_bytes()
identity = lambda data: {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
link = next(r for r in accepted['commands'] if r['label'] == 'link-source-stage')
order = [Path(arg).stem for arg in link['argv'] if arg.endswith('.o')]
order = ['file-request-group' if n == 'source-stage-contract' else n for n in order]
objects = {n: common / 'objects' / (n + '.o') for n in order if n != 'file-request-group'}
native_objects = [common / ('native-' + n + '.o') for n in ['cupidbuild_host', 'path_encoding',
    'cupidbuild_source_path', 'cupidbuild_source_file', 'cupidbuild_source_stage']]
if windows: native_objects.append(common / 'native-utf8.o')
native_controls = {p.name: identity(p.read_bytes()) for p in native_objects}
seed = bootstrap.freeze_seed_inputs(ROOT / ('bootstrap/seeds/i386-' + host + '/manifest.json'), output / 'seed')
checkpoint_bytes = (BASE / 'preceding-checkpoint-controls.json').read_bytes()
checkpoint = json.loads(checkpoint_bytes)
state = {'schema': 'cupid.shared-file-request-group-build.v1', 'status': 'running', 'host': host,
    'common_build': identity(common_bytes), 'sources': accepted['sources'], 'fixture': identity(fixture_bytes),
    'normal_source_inputs': accepted['normal_source_inputs'], 'projected_files': checkpoint['projected_files'], 'producer_sentinels': accepted['producer_sentinels'],
    'runner': identity(Path(__file__).read_bytes()), 'native_objects': native_controls, 'commands': []}
started = time.monotonic()

def guard():
    bootstrap.require_live_seed_inputs(seed)
    assert (common / 'closed.json').read_bytes() == common_bytes and fixture.read_bytes() == fixture_bytes
    assert (BASE / 'preceding-checkpoint-controls.json').read_bytes() == checkpoint_bytes
    for name, facts in checkpoint['projected_files'].items(): assert identity((ROOT / name).read_bytes()) == facts
    for name, facts in accepted['sources'].items(): assert identity((source / name).read_bytes()) == facts
    for name, facts in accepted['normal_source_inputs'].items(): assert identity((ROOT / name).read_bytes()) == facts
    for name, p in objects.items():
        if name != 'file-request-group': assert identity(p.read_bytes()) == accepted['objects'][p.name]
    for p in native_objects: assert identity(p.read_bytes()) == native_controls[p.name]

def run(label, argv, native=False):
    guard()
    before = time.monotonic()
    result = subprocess.run(list(map(str, argv)), cwd=ROOT, env=dict(os.environ) if native else
        {**os.environ, **accepted['producer_sentinels']}, capture_output=True, timeout=180)
    for stream in ('stdout', 'stderr'): (output / (label + '-' + stream + '.bin')).write_bytes(getattr(result, stream))
    state['commands'].append({'label': label, 'argv': list(map(str, argv)), 'native': native,
        'exit_code': result.returncode, 'seconds': time.monotonic() - before, 'timeout_seconds': 180,
        'stdout': identity(result.stdout), 'stderr': identity(result.stderr)})
    assert result.returncode == 0 and result.stdout == result.stderr == b'', state['commands'][-1]
    guard()

try:
    compiler = shutil.which('clang')
    assert compiler
    native_image = output / ('native-file-request-group.' + suffix)
    run('native-build', [compiler, '-x', 'c', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        '-D_CRT_SECURE_NO_WARNINGS=1', *(['-DCUPID_NATIVE_UTF8_ENABLE=1'] if windows else []),
        '-I', source, '-I', ROOT / 'toolchain', fixture, '-x', 'none', *native_objects,
        *(['-lntdll'] if windows else []), '-o', native_image], True)
    object_path = output / 'file-request-group.o'
    run('compile', [seed.tools['cupidc'], '--root', ROOT, '-c', '/' + fixture.relative_to(ROOT).as_posix(),
        '-I', '/' + source.relative_to(ROOT).as_posix(), '-I', '/toolchain', '--include-angle',
        '/toolchain/hosted/i386-linux/include', '-o', '/' + object_path.relative_to(ROOT).as_posix()])
    bootstrap._validate_i386_relocatable(object_path)
    objects['file-request-group'] = object_path
    checked_image = output / ('checked-file-request-group.' + suffix)
    plan = json.loads((ROOT / 'bootstrap/seeds/i386-linux/manifest.json').read_bytes())['build_plan']
    if windows:
        plan = bootstrap._windows_build_plan(plan, utf8=True, user_link_aliases=True)
        args = bootstrap._windows_link_arguments('cupidbuild', checked_image, objects, order, utf8=True, user_link_aliases=True)
    else: args = ['-m', 'elf_i386', '--text-address', '0x08048000', '--entry', '_start', '-o', checked_image, *(objects[n] for n in order)]
    run('link', [seed.tools['cupidld'], *args])
    if windows:
        imports = tuple((lib, tuple(r['procedure'] for r in plan['imports']['cupidbuild'] if r['library'] == lib))
            for lib in sorted({r['library'] for r in plan['imports']['cupidbuild']}))
        bootstrap._validate_static_i386_pe32(checked_image, 0x00401000, imports)
    else: bootstrap._validate_static_i386_elf(checked_image, 0x08048000)
    for p in (object_path, checked_image):
        run('strict-' + p.name, [seed.tools['cupiddis'], '--require-known', '--require-local-targets', '--require-code-anchors', p])
    state.update(status='pass', object=identity(object_path.read_bytes()), native_image=identity(native_image.read_bytes()),
        checked_image=identity(checked_image.read_bytes()), linked_objects={n: identity(objects[n].read_bytes()) for n in order}, linked_object_order=order)
except BaseException as error:
    state.update(status='fail', error=repr(error))
    traceback.print_exc()
state['seconds'] = time.monotonic() - started
(output / 'closed.json').write_text(json.dumps(state, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
print(json.dumps({k: state.get(k) for k in ('status', 'host', 'seconds', 'error')}))
raise SystemExit(0 if state['status'] == 'pass' else 1)
