"""Exercise the native CupidBuild image command with the installed seed."""

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tools.hostbuild_disk import StageFile, _wad_dest, create_or_update_image


ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = ROOT / 'toolchain'
SEED = ROOT / 'bootstrap/seeds' / ('i386-windows' if os.name == 'nt' else 'i386-linux')


def file_identity(path):
    stat = path.stat()
    with path.open('rb') as stream:
        return stat.st_ino, stat.st_mtime_ns, hashlib.file_digest(stream, 'sha256').hexdigest()


class CupidBuildDiskCommandTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if program := os.environ.get('CUPIDBUILD_IMAGE_COMMAND'):
            cls.program = Path(program).resolve(strict=True)
            return
        build = tempfile.TemporaryDirectory(prefix='.image-command-', dir=TOOLCHAIN)
        cls.addClassCleanup(build.cleanup)
        directory = Path(build.name)
        cls.program = directory / ('cupidbuild.exe' if os.name == 'nt' else 'cupidbuild')
        relative = directory.relative_to(TOOLCHAIN).as_posix()
        result = subprocess.run(['make', '-C', str(TOOLCHAIN), f'BUILD_DIR={relative}',
                                 f'{relative}/{cls.program.name}'],
                                capture_output=True, text=True, timeout=180)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix='cupid-image-command-')
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.project = self.root / 'project'
        self.external = self.root / 'external'
        self.project.mkdir()
        self.external.mkdir()
        self.output = self.root / 'products/nested/image.img'
        self.seed = self.external / 'checked'
        shutil.copytree(SEED, self.seed)
        self.release = self.project / 'release.json'
        shutil.copyfile(SEED.parent / 'release.json', self.release)
        self.boot = self.project / 'boot.bin'
        self.kernel = self.external / 'kernel.bin'
        self.boot.write_bytes(bytes(range(256)) * 12)
        self.kernel.write_bytes(b'kernel\x00' * 192)

    def run_image(self, *options):
        return subprocess.run([
            self.program, 'image', '--root', self.project, '--seed-release', 'release.json',
            '--seed-manifest', '../external/checked/manifest.json',
            '--image', '../products/nested/image.img', '--bootloader', 'boot.bin',
            '--kernel', '../external/kernel.bin', '--hdd-mb', '8', '--fat-start-lba', '32',
            *options,
        ], cwd=self.project, capture_output=True, text=True, encoding='utf-8', timeout=180)

    def test_external_seed_output_and_wad_stages_match_python(self):
        payload = self.external / 'payload.bin'
        wad = self.external / 'freedoom2.wad'
        payload.write_bytes(b'payload\x00' * 103)
        wad.write_bytes(b'staged WAD bytes')
        expected = self.root / 'expected.img'
        stages = [StageFile(payload, '/BIN/A.BIN'), StageFile(payload, '/DOCS/Straße.txt'),
                  StageFile(wad, _wad_dest(wad, 1))]
        create_or_update_image(expected, self.boot, self.kernel, 8, 32, stages, False,
                               seed_manifest=SEED / 'manifest.json')
        result = self.run_image('--stage', '../external/payload.bin:/BIN/A.BIN',
                                '--stage', '../external/payload.bin:/DOCS/Straße.txt',
                                '--stage', '../external/missing.bin:/MISSING.DAT',
                                '--wads', '../external/freedoom2.wad')
        self.assertEqual(result.returncode, 0, (result.stdout, result.stderr))
        self.assertEqual(file_identity(self.output)[2], file_identity(expected)[2])
        before = file_identity(self.output)
        result = self.run_image()
        self.assertEqual(result.returncode, 0, (result.stdout, result.stderr))
        self.assertEqual(file_identity(self.output), before)

    def test_unlisted_seed_tool_and_bad_release_preserve_output(self):
        self.output.parent.mkdir(parents=True)
        self.output.write_bytes(b'prior image')
        before = file_identity(self.output)
        extra = self.seed / ('extra.EXE' if os.name == 'nt' else 'extra.ELF')
        extra.write_bytes(b'unlisted tool')
        result = self.run_image()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('image inputs cannot be frozen', result.stderr)
        self.assertEqual(file_identity(self.output), before)
        extra.unlink()
        self.release.write_bytes(b'{}')
        result = self.run_image()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('checked seed manifest is invalid', result.stderr)
        self.assertEqual(file_identity(self.output), before)
        self.assertFalse(list(self.output.parent.glob('*cupidbuild*')))
