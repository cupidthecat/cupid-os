from pathlib import Path
import hashlib
import json
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCE_REVISION = '1d852023df98c03481c4b3979dccb398288b7115'
SOURCE_DIGEST = '4ae98403b0896b24e50b81b11b213b8117207421b9152e95d0500e00e612d07e'
TOOLS = ('cupidasm', 'cupidc', 'cupiddis', 'cupidld', 'cupidobj', 'cupidbuild')
SEED_FILES = ['bootstrap/seeds/release.json']
for profile, suffix in (('i386-linux', 'elf'), ('i386-windows', 'exe')):
    prefix = 'bootstrap/seeds/' + profile + '/'
    SEED_FILES.append(prefix + 'manifest.json')
    SEED_FILES.extend(prefix + name + '.' + suffix for name in TOOLS)


def read(path):
    assert path.is_file() and not path.is_symlink(), path
    return path.read_bytes()


def identity(payload):
    return {'size': len(payload), 'sha256': hashlib.sha256(payload).hexdigest()}


def inventory(directory):
    result = {}
    for path in sorted(directory.rglob('*')):
        assert not path.is_symlink(), path
        if path.is_file():
            result[path.relative_to(directory).as_posix()] = identity(read(path))
    assert result, directory
    return result


def git_bytes(revision, name):
    environment = dict(os.environ)
    for key in ('GIT_DIR', 'GIT_WORK_TREE', 'GIT_COMMON_DIR', 'GIT_INDEX_FILE'):
        environment.pop(key, None)
    git_directory = ROOT / '.git'
    if git_directory.is_file():
        reference = read(git_directory).decode('utf-8').strip()
        assert reference.startswith('gitdir: ')
        value = reference[len('gitdir: '):].replace('\\', '/')
        if os.name != 'nt' and re.match(r'^[A-Za-z]:/', value): value = '/mnt/' + value[0].lower() + value[2:]
        git_directory = Path(value)
        if not git_directory.is_absolute(): git_directory = ROOT / git_directory
    assert git_directory.is_dir()
    environment.update(GIT_DIR=str(git_directory), GIT_WORK_TREE=str(ROOT))
    workflow = environment.get('CUPID_GIT_HUMAN_WORKFLOW')
    if workflow:
        prefix = ['powershell', '-NoProfile', '-File', workflow] if workflow.endswith('.ps1') else ['bash', workflow]
        argv = [*prefix, 'git', 'show', revision + ':' + name]
    else:
        argv = ['git', 'show', revision + ':' + name]
    return subprocess.run(argv, cwd=ROOT, env=environment, check=True, stdout=subprocess.PIPE).stdout
