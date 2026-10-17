"""Capture the actual image command's required and optional source path rules."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import traceback

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools import hostbuild
created_output = None


def identity(data):
    return {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def main():
    global created_output
    arguments = argparse.ArgumentParser(description=__doc__)
    arguments.add_argument('--output', type=Path, required=True)
    args = arguments.parse_args()
    output = args.output.resolve()
    assert output.is_relative_to(ROOT / 'build') and output != ROOT / 'build' and not output.exists()
    controls = {name: (ROOT / name).read_bytes() for name in
        ('tools/hostbuild.py', 'tools/bootstrap_toolchain.py')}
    output.mkdir(parents=True)
    created_output = output
    if os.name == 'nt':
        case = output / 'cases'
        case.mkdir()
    else:
        case = Path(tempfile.mkdtemp(prefix='cupid-source-path-oracles-', dir='/var/tmp'))
    ordinary = case / 'ordinary'
    ordinary.mkdir()
    (ordinary / 'nested').mkdir()
    (ordinary / 'payload.bin').write_bytes(bytes(range(256)))
    (ordinary / 'empty.bin').write_bytes(b'')
    (ordinary / '\u03b4-\u732b.bin').write_bytes(b'unicode\n')
    long_parent = ordinary
    for index in range(8):
        long_parent = long_parent / ('long-' + str(index) + '-' + 'x' * 70)
        long_parent.mkdir()
    (long_parent / '\u732b.bin').write_bytes(b'long unicode\n')
    target = case / 'external' / 'nested'
    target.mkdir(parents=True)
    (target / 'payload.bin').write_bytes(b'parent alias\n')
    alias = case / 'alias'
    alias_command = None
    if os.name == 'nt':
        command = ['cmd.exe', '/d', '/c', 'mklink', '/J', str(alias), str(target)]
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        alias_command = {'argv': command, 'exit_code': result.returncode,
            'stdout': identity(result.stdout), 'stderr': identity(result.stderr)}
    else:
        alias.symlink_to(target, target_is_directory=True)
    spellings = [
        ('relative-regular', 'ordinary/payload.bin'),
        ('absolute-regular', str(ordinary / 'payload.bin')),
        ('empty-file', 'ordinary/empty.bin'),
        ('unicode-file', 'ordinary/\u03b4-\u732b.bin'),
        ('long-unicode-file', str(long_parent / '\u732b.bin')),
        ('dot-component', 'ordinary/./payload.bin'),
        ('parent-component', 'ordinary/nested/../payload.bin'),
        ('missing-cancelled-before-resolution', 'absent/../ordinary/payload.bin'),
        ('alias-parent', 'alias/payload.bin'),
        ('alias-parent-before-dotdot', 'alias/../ordinary/payload.bin'),
        ('existing-directory', 'ordinary'),
        ('missing-leaf', 'ordinary/absent.bin'),
        ('missing-ancestors', 'ordinary/absent/nested/absent.bin'),
        ('file-as-parent', 'ordinary/payload.bin/child.bin'),
        ('trailing-separator', 'ordinary/payload.bin/'),
        ('leading-space', 'ordinary/ leading.bin'),
        ('newline-name', 'ordinary/line\nname.bin'),
    ]
    (ordinary / ' leading.bin').write_bytes(b'space\n')
    if os.name != 'nt':
        (ordinary / 'line\nname.bin').write_bytes(b'newline\n')
        for name in ('colon:name.bin', 'backslash\\name.bin'):
            (ordinary / name).write_bytes(b'posix name\n')
            spellings.append(('posix-' + ('colon' if ':' in name else 'backslash'), 'ordinary/' + name))
        (ordinary / 'linked.bin').symlink_to(ordinary / 'payload.bin')
        (ordinary / 'broken.bin').symlink_to(ordinary / 'absent.bin')
        (ordinary / 'broken-parent').symlink_to(ordinary / 'absent-parent', target_is_directory=True)
        os.mkfifo(ordinary / 'fifo')
        spellings.extend((('linked-leaf', 'ordinary/linked.bin'),
            ('broken-linked-leaf', 'ordinary/broken.bin'),
            ('broken-parent-alias', 'ordinary/broken-parent/child.bin'), ('fifo-leaf', 'ordinary/fifo')))
    else:
        relative = case.as_posix()[3:].replace('/', '\\')
        spellings.extend((('unc-localhost', '\\\\localhost\\C$\\' + relative + '\\ordinary\\payload.bin'),
            ('unc-address', '\\\\127.0.0.1\\C$\\' + relative + '\\ordinary\\payload.bin')))

    old_cwd = Path.cwd()
    records = []
    started = time.monotonic()
    try:
        os.chdir(case)
        for label, spelling in spellings:
            request = Path(spelling)
            row = {'label': label, 'spelling': spelling, 'absolute': str(hostbuild._disk_absolute(request))}
            for mode in ('required', 'optional'):
                before = time.monotonic()
                try:
                    if mode == 'required':
                        resolved = hostbuild._resolve_disk_regular(request, 'oracle input')
                        facts = {'status': 'present', 'resolved': str(resolved)}
                    else:
                        absolute = hostbuild._disk_absolute(request)
                        # Exact original create_or_update_image stage decision.
                        if hostbuild._disk_is_link_or_junction(absolute, 'oracle stage'):
                            raise hostbuild.DiskImageError('stage input may not be a symbolic link or junction')
                        if absolute.exists():
                            resolved = hostbuild._resolve_disk_regular(absolute, 'oracle stage')
                            facts = {'status': 'present', 'resolved': str(resolved)}
                        else:
                            facts = {'status': 'missing', 'requested': str(absolute)}
                    if facts['status'] == 'present':
                        facts['payload'] = identity(Path(facts['resolved']).read_bytes())
                except (hostbuild.DiskImageError, OSError) as error:
                    facts = {'status': 'reject', 'error_type': type(error).__name__, 'error': str(error)}
                facts['seconds'] = time.monotonic() - before
                row[mode] = facts
            records.append(row)
    finally:
        os.chdir(old_cwd)
    for name, data in controls.items():
        assert (ROOT / name).read_bytes() == data
    receipt = {'schema': 'cupid.native-source-path-oracles.v1', 'status': 'pass',
        'host': sys.platform, 'python': sys.version, 'records': records,
        'source_inputs': {name: identity(data) for name, data in controls.items()},
        'runner': identity(Path(__file__).read_bytes()), 'alias_command': alias_command,
        'working_root': str(case), 'seconds': time.monotonic() - started,
        'runtime_oracle_only': True, 'normal_source_changed': False, 'products_retained': True}
    (output / 'closed.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n',
        encoding='utf-8', newline='\n')
    print(json.dumps({'status': receipt['status'], 'host': receipt['host'], 'cases': len(records),
        'outcomes': {row['label']: [row['required']['status'], row['optional']['status']] for row in records}}))


if __name__ == '__main__':
    try:
        main()
    except BaseException as error:
        traceback.print_exc()
        if created_output is not None and not (created_output / 'closed.json').exists():
            (created_output / 'closed.json').write_text(json.dumps({'status': 'fail', 'error': repr(error)},
                indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
        raise SystemExit(1)
