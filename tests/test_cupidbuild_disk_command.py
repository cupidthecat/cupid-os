"""Exercise the native CupidBuild image command with the installed seed."""

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest

from tests import test_cupidbuild_observer as observer_fixtures
from tools.hostbuild_disk import StageFile, _wad_dest, create_or_update_image


ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = ROOT / 'toolchain'
SEED = ROOT / 'bootstrap/seeds' / ('i386-windows' if os.name == 'nt' else 'i386-linux')


def file_identity(path):
    stat = path.stat()
    with path.open('rb') as stream:
        return stat.st_ino, stat.st_mtime_ns, hashlib.file_digest(stream, 'sha256').hexdigest()


class CupidBuildDiskCommandTests(unittest.TestCase):
    make_source_junction = observer_fixtures.CupidBuildObserverTests.make_source_junction
    remove_source_junction = observer_fixtures.CupidBuildObserverTests.remove_source_junction

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
                                 'CPPFLAGS=-DCUPIDBUILD_PUBLICATION_RACE_TEST',
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

    def image_command(self, *options, kernel='../external/kernel.bin'):
        return [
            self.program, 'image', '--root', self.project, '--seed-release', 'release.json',
            '--seed-manifest', '../external/checked/manifest.json',
            '--image', '../products/nested/image.img', '--bootloader', 'boot.bin',
            '--kernel', kernel, '--hdd-mb', '8', '--fat-start-lba', '32',
            *options,
        ]

    def run_image(self, *options, kernel='../external/kernel.bin'):
        return subprocess.run(self.image_command(*options, kernel=kernel), cwd=self.project,
                              capture_output=True, text=True, encoding='utf-8', timeout=180)

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

    def test_seed_and_outputs_under_build_root_keep_the_retained_manifest(self):
        seed = self.project / 'bootstrap' / self.seed.name
        shutil.copytree(self.seed, seed)
        release = Path('bootstrap') / 'release.json'
        shutil.copyfile(self.release, self.project / release)
        kernel = self.project / 'kernel.bin'
        shutil.copyfile(self.kernel, kernel)
        stage = self.project / 'stage.bin'
        stage.write_bytes(b'staged from the build root')
        expected = self.root / 'expected.img'
        create_or_update_image(expected, self.boot, kernel, 8, 32,
                               [StageFile(stage, '/DATA.BIN')], False,
                               seed_manifest=seed / 'manifest.json')
        for name in ('direct.img', 'products/nested.img'):
            with self.subTest(image=name):
                command = [self.program, 'image', '--root', self.project,
                           '--seed-release', str(release),
                           '--seed-manifest', 'bootstrap/checked/manifest.json',
                           '--image', name, '--bootloader', 'boot.bin', '--kernel', 'kernel.bin',
                           '--hdd-mb', '8', '--fat-start-lba', '32', '--stage', 'stage.bin:/DATA.BIN']
                result = subprocess.run(command, cwd=self.project, capture_output=True,
                                        text=True, encoding='utf-8', timeout=180)
                self.assertEqual(result.returncode, 0, (result.stdout, result.stderr))
                output = self.project / name
                before = file_identity(output)
                self.assertEqual(before[2], file_identity(expected)[2])
                extra = seed / ('extra.EXE' if os.name == 'nt' else 'extra.ELF')
                extra.write_bytes(b'unlisted executable')
                try:
                    result = subprocess.run(command, cwd=self.project, capture_output=True,
                                            text=True, encoding='utf-8', timeout=180)
                    self.assertNotEqual(result.returncode, 0, (result.stdout, result.stderr))
                    self.assertEqual(file_identity(output), before)
                finally:
                    extra.unlink()

    def test_image_inputs_and_stage_aliases_share_one_source(self):
        alias = self.external / 'boot-copy.bin'
        os.link(self.boot, alias)
        expected = self.root / 'expected.img'
        stages = [StageFile(alias, '/ALIAS.BIN'), StageFile(self.boot, '/BOOT.BIN')]
        create_or_update_image(expected, self.boot, alias, 8, 32, stages, False,
                               seed_manifest=SEED / 'manifest.json')
        result = self.run_image('--stage', '../external/boot-copy.bin:/ALIAS.BIN',
                                '--stage', 'boot.bin:/BOOT.BIN', kernel='../external/boot-copy.bin')
        self.assertEqual(result.returncode, 0, (result.stdout, result.stderr))
        self.assertEqual(file_identity(self.output)[2], file_identity(expected)[2])

    def test_hardlinked_stage_cannot_replace_the_output(self):
        self.output.parent.mkdir(parents=True)
        self.output.write_bytes(b'prior image')
        alias = self.external / 'output-alias.bin'
        os.link(self.output, alias)
        before = file_identity(self.output)
        result = self.run_image('--stage', '../external/output-alias.bin:/ALIAS.BIN')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('output may not replace an input', result.stderr)
        self.assertEqual(file_identity(self.output), before)
        self.assertEqual(file_identity(alias), before)

    def test_rejects_image_larger_than_the_mbr_sector_count(self):
        command = self.image_command()
        command[command.index('--hdd-mb') + 1] = '2097152'
        result = subprocess.run(command, cwd=self.project, capture_output=True,
                                text=True, encoding='utf-8', timeout=180)
        self.assertNotEqual(result.returncode, 0, (result.stdout, result.stderr))
        self.assertIn('image geometry exceeds the bounded store', result.stderr)
        self.assertFalse(self.output.parent.exists())

    @unittest.skipIf(os.environ.get('CUPIDBUILD_IMAGE_COMMAND'), 'requires native pause hooks')
    def test_rebinding_a_shared_stage_preserves_previous_output(self):
        self.output.parent.mkdir(parents=True)
        self.output.write_bytes(b'prior image')
        before = file_identity(self.output)
        ready, resume = self.root / 'ready', self.root / 'resume'
        substitute = self.root / 'substitute'
        substitute.mkdir()
        os.link(self.boot, self.external / 'boot-copy.bin')
        (substitute / 'boot-copy.bin').write_bytes(self.boot.read_bytes())
        command = self.image_command('--stage', '../alias/boot-copy.bin:/ALIAS.BIN')
        for phase in ('before-mutation', 'after-install'):
            with self.subTest(phase=phase):
                self.make_source_junction(self.external)
                environment = dict(os.environ, CUPIDBUILD_PUBLICATION_TEST_PHASE=phase,
                                   CUPIDBUILD_PUBLICATION_TEST_READY=str(ready),
                                   CUPIDBUILD_PUBLICATION_TEST_RESUME=str(resume))
                with subprocess.Popen(command, cwd=self.project, env=environment,
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                      text=True, encoding='utf-8') as process:
                    try:
                        deadline = time.monotonic() + 30
                        while not ready.exists() and process.poll() is None and time.monotonic() < deadline:
                            time.sleep(0.01)
                        self.assertTrue(ready.exists(), 'publication checkpoint was not reached')
                        self.remove_source_junction()
                        self.make_source_junction(substitute)
                        resume.touch()
                        stdout, stderr = process.communicate(timeout=20)
                        self.assertNotEqual(process.returncode, 0, (stdout, stderr))
                        self.assertIn('original source path changed', stderr)
                    finally:
                        if process.poll() is None:
                            process.kill()
                            process.communicate(timeout=10)
                self.assertEqual(file_identity(self.output), before)
                self.assertFalse(list(self.output.parent.glob('*cupidbuild*')))
                self.remove_source_junction()
                ready.unlink()
                resume.unlink()
