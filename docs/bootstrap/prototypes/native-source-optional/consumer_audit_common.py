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
        git_reference = read(git_directory).decode('utf-8').strip()
        assert git_reference.startswith('gitdir: ')
        reference = git_reference[len('gitdir: '):].replace('\\', '/')
        if os.name != 'nt' and re.match(r'^[A-Za-z]:/', reference):
            reference = '/mnt/' + reference[0].lower() + reference[2:]
        git_directory = Path(reference)
        if not git_directory.is_absolute():
            git_directory = ROOT / git_directory
    assert git_directory.is_dir()
    environment.update(GIT_DIR=str(git_directory), GIT_WORK_TREE=str(ROOT))
    return subprocess.run(['git', 'show', revision + ':' + name], cwd=ROOT,
                          env=environment, check=True, stdout=subprocess.PIPE).stdout
