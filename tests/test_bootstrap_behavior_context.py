"""Explicit reviewed context for byte-identical fixed-point behavior tools."""
from dataclasses import replace
import contextlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from tools import bootstrap_stage_release as release
from tools import bootstrap_toolchain as seed


ROOT = Path(__file__).resolve().parents[1]
LINUX = ROOT / 'bootstrap/seeds/i386-linux/manifest.json'
WINDOWS = ROOT / 'bootstrap/seeds/i386-windows/manifest.json'
RELEASE = ROOT / 'bootstrap/seeds/release.json'


class BootstrapBehaviorContextTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='cupid-bootstrap-context-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.linux = seed.verify_seed_inputs(LINUX)
        self.windows = seed.verify_seed_inputs(WINDOWS)
        self.record = self.root / 'release.json'
        self.record.write_bytes(RELEASE.read_bytes())
        self.plan = self.linux.manifest['build_plan']
        self.windows_plan = seed._windows_build_plan(self.plan, utf8=True,
            long_paths=True, user_link_aliases=True)

    def request(self, paired=True):
        return release.capture_seed_behavior_release(self.record, self.linux,
            self.windows if paired else None)

    def authorize(self, request, format_name='elf32', **changes):
        selected = self.windows if format_name == 'pe32' else self.linux
        stage = seed.Stage({}, dict(selected.tools))
        arguments = dict(source_inputs=None, source_root=self.root,
            linux_plan=self.plan, windows_plan=self.windows_plan,
            linux_seed=self.linux, windows_seed=self.windows if format_name == 'pe32' else None,
            stage_three=stage, stage_four=stage, format_name=format_name)
        arguments.update(changes)
        return request.authorize(**arguments)

    def test_linux_reuse_keeps_the_original_release_and_current_plan(self):
        authority = self.authorize(self.request(False))
        self.assertEqual(authority.format_name, 'elf32')
        self.assertEqual(authority.payload, RELEASE.read_bytes())
        self.assertEqual(json.loads(authority.linux_plan_bytes), self.plan)

    def test_native_windows_reuse_requires_the_complete_selected_pair(self):
        request = self.request()
        authority = self.authorize(request, 'pe32')
        self.assertEqual(authority.format_name, 'pe32')
        self.assertEqual(authority.payload, RELEASE.read_bytes())
        self.assertEqual(request.windows_seed.manifest_bytes, self.windows.manifest_bytes)

    def test_missing_windows_selection_rejects_native_reuse(self):
        with self.assertRaisesRegex(seed.BootstrapError, 'selection differs'):
            self.authorize(self.request(False), 'pe32')

    def test_unknown_execution_format_is_rejected(self):
        with self.assertRaisesRegex(seed.BootstrapError, 'selection differs'):
            self.authorize(self.request(), 'raw')

    def test_other_windows_profile_cannot_reuse_this_release(self):
        other_plan = seed._windows_build_plan(self.plan, utf8=True,
            long_paths=False, user_link_aliases=True)
        with self.assertRaisesRegex(seed.BootstrapError, 'plan differs'):
            self.authorize(self.request(), 'pe32', windows_plan=other_plan)

    def test_linux_request_rejects_a_windows_execution_selection(self):
        with self.assertRaisesRegex(seed.BootstrapError, 'selection differs'):
            self.authorize(self.request(), windows_seed=self.windows)

    def test_each_rebuilt_windows_tool_must_match_the_reviewed_bytes(self):
        request = self.request()
        for role in self.windows.tools:
            with self.subTest(role=role):
                changed = self.root / (role + '.exe')
                shutil.copyfile(self.windows.tools[role], changed)
                with changed.open('ab') as stream:
                    stream.write(b'changed')
                tools = {**self.windows.tools, role: changed}
                for stage_name in ('stage_three', 'stage_four'):
                    with self.assertRaisesRegex(seed.BootstrapError, 'stage tools differ'):
                        self.authorize(request, 'pe32', **{stage_name: seed.Stage({}, tools)})

    def test_each_rebuilt_linux_tool_must_match_the_reviewed_bytes(self):
        request = self.request(False)
        for role in self.linux.tools:
            with self.subTest(role=role):
                changed = self.root / (role + '.elf')
                shutil.copyfile(self.linux.tools[role], changed)
                with changed.open('ab') as stream:
                    stream.write(b'changed')
                tools = {**self.linux.tools, role: changed}
                for stage_name in ('stage_three', 'stage_four'):
                    with self.assertRaisesRegex(seed.BootstrapError, 'stage tools differ'):
                        self.authorize(request, **{stage_name: seed.Stage({}, tools)})

    def test_changed_windows_seed_bytes_are_rejected(self):
        altered = replace(self.windows, manifest_bytes=self.windows.manifest_bytes + b' ')
        with self.assertRaises(seed.BootstrapError):
            release.capture_seed_behavior_release(self.record, self.linux, altered)

    def test_changed_reviewed_release_is_rejected(self):
        request = self.request()
        self.record.write_bytes(self.record.read_bytes() + b' ')
        with self.assertRaisesRegex(seed.BootstrapError, 'release changed'):
            request.require_live()

    def test_missing_selected_release_rejects_final_revalidation(self):
        request = self.request()
        self.record.unlink()
        with self.assertRaisesRegex(seed.BootstrapError, 'release could not be read'):
            request.require_live()

    def test_corrupt_reviewed_release_is_rejected_before_stage_execution(self):
        self.record.write_bytes(b'{}')
        with mock.patch.object(seed, '_bootstrap_from_frozen_seed') as build:
            with self.assertRaisesRegex(seed.BootstrapError, 'release is invalid'):
                seed.bootstrap_from_seed(LINUX, ROOT, self.root / 'output', seed_release_path=self.record)
        build.assert_not_called()
        self.assertFalse((self.root / 'output').exists())

    def test_explicit_linux_context_is_captured_after_the_seed_freeze(self):
        selected = self.request(False)
        with mock.patch.object(seed, 'freeze_seed_inputs', return_value=self.linux), \
             mock.patch.object(release, 'capture_seed_behavior_release', return_value=selected) as capture, \
             mock.patch.object(seed, '_bootstrap_from_frozen_seed', return_value={'test': 'metadata-only'}) as build:
            result = seed.bootstrap_from_seed(LINUX, ROOT, self.root / 'output', seed_release_path=self.record)
        self.assertEqual(result, {'test': 'metadata-only'})
        capture.assert_called_once_with(self.record, self.linux)
        self.assertIs(build.call_args.kwargs['release_request'], selected)
        self.assertTrue(build.call_args.kwargs['compare_fixed_point'])

    def test_omitted_context_keeps_the_historical_entry_point(self):
        with mock.patch.object(seed, 'freeze_seed_inputs', return_value=self.linux), \
             mock.patch.object(release, 'capture_seed_behavior_release') as capture, \
             mock.patch.object(seed, '_bootstrap_from_frozen_seed', return_value={}) as build:
            seed.bootstrap_from_seed(LINUX, ROOT, self.root / 'output')
        capture.assert_not_called()
        self.assertNotIn('release_request', build.call_args.kwargs)
        self.assertTrue(build.call_args.kwargs['compare_fixed_point'])

    def test_authority_selections_are_mutually_exclusive(self):
        with self.assertRaisesRegex(seed.BootstrapError, 'mutually exclusive'):
            seed._bootstrap_from_seed_with_policy(LINUX, ROOT, self.root / 'output',
                compare_fixed_point=True, release_request=object(), seed_release_path=self.record)
        self.assertFalse((self.root / 'output').exists())

    def test_cli_passes_explicit_context_to_each_public_bootstrap(self):
        for command in ('bootstrap', 'bootstrap-windows'):
            arguments = [command, '--manifest', str(LINUX if command == 'bootstrap' else WINDOWS),
                '--root', str(ROOT), '--output', str(self.root / 'output'),
                '--seed-release', str(self.record), '--windows-long-paths']
            operation = 'bootstrap_from_seed'
            if command.endswith('windows'):
                arguments += ['--plan-manifest', str(LINUX)]
                operation = 'bootstrap_windows_from_seed'
            with self.subTest(command=command), \
                 mock.patch.object(seed, operation, return_value={}) as build, \
                 contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(seed.main(arguments), 0)
            self.assertEqual(build.call_args.kwargs['seed_release_path'], self.record)

    def check_cli_rejection(self, command, diagnostic, missing=False):
        if missing:
            self.record.unlink()
        else:
            self.record.write_bytes(b'not valid JSON\n')
        for entry in ([str(ROOT / 'tools/bootstrap_toolchain.py')],
                      ['-m', 'tools.bootstrap_toolchain']):
            with self.subTest(entry=entry):
                output = self.root / ('rejected-' + str(len(entry)))
                arguments = [sys.executable, *entry, command,
                    '--manifest', str(WINDOWS if command.endswith('windows') else LINUX),
                    '--root', str(ROOT), '--output', str(output),
                    '--seed-release', str(self.record)]
                if command.endswith('windows'):
                    arguments += ['--plan-manifest', str(LINUX)]
                result = subprocess.run(arguments, cwd=ROOT, capture_output=True,
                                        text=True, timeout=30)
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, '')
                message = ('behavior seed release could not be read' if missing else
                           'behavior seed release is invalid: release JSON is invalid')
                self.assertEqual(result.stderr, diagnostic + ': ' + message + '\n')
                self.assertFalse(output.exists())

    def test_linux_cli_release_rejection_has_one_bounded_diagnostic(self):
        self.check_cli_rejection('bootstrap', 'checked bootstrap failed')

    @unittest.skipUnless(os.name == 'nt', 'native Windows bootstrap requires a Windows host')
    def test_windows_cli_release_rejection_has_one_bounded_diagnostic(self):
        self.check_cli_rejection('bootstrap-windows', 'checked Windows bootstrap failed')

    def test_linux_cli_missing_release_has_one_bounded_diagnostic(self):
        self.check_cli_rejection('bootstrap', 'checked bootstrap failed', missing=True)

    @unittest.skipUnless(os.name == 'nt', 'native Windows bootstrap requires a Windows host')
    def test_windows_cli_missing_release_has_one_bounded_diagnostic(self):
        self.check_cli_rejection('bootstrap-windows', 'checked Windows bootstrap failed', missing=True)

    @unittest.skipUnless(os.name == 'nt', 'native Windows wrapper requires a Windows host')
    def test_native_windows_wrapper_captures_both_frozen_seed_roles(self):
        selected = self.request()
        with mock.patch.object(seed, 'freeze_seed_inputs', side_effect=[self.windows, self.linux]), \
             mock.patch.object(release, 'capture_seed_behavior_release', return_value=selected) as capture, \
             mock.patch.object(seed, '_bootstrap_windows_from_frozen_seed', return_value={}) as build:
            seed.bootstrap_windows_from_seed(WINDOWS, LINUX, ROOT, self.root / 'output',
                seed_release_path=self.record, windows_long_paths=True)
        capture.assert_called_once_with(self.record, self.linux, self.windows)
        self.assertIs(build.call_args.kwargs['release_request'], selected)

    def test_make_bootstraps_bind_the_reviewed_release(self):
        makefile = (ROOT / 'Makefile').read_text()
        for target, following in (('bootstrap-from-seed', 'bootstrap-windows-from-seed'),
                                  ('bootstrap-windows-from-seed', '# NASM')):
            recipe = makefile.split(target + ':', 1)[1].split(following, 1)[0]
            self.assertIn('$(PRODUCTION_SEED_RELEASE)', recipe.splitlines()[0])
            self.assertIn('--seed-release $(PRODUCTION_SEED_RELEASE)', recipe)
            self.assertIn('--windows-long-paths', recipe)


if __name__ == '__main__':
    unittest.main()
