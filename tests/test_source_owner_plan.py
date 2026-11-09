"""Keep current owner dependencies exact and historical seed plans unchanged."""
import copy
from dataclasses import replace
import json
from pathlib import Path
import tempfile
import unittest

from tests.bootstrap_seed_fixture_profiles import historical_disk_manifest
from tools import artifact_size_policy as policy
from tools import bootstrap_toolchain as b
from tools import toolchain_manifest_contract as sdk

ROOT = Path(__file__).resolve().parents[1]
TABLE = 'toolchain/cupidbuild_disk_integer_tables.inc'
BRIDGE = 'toolchain/hosted/i386-windows/observer_thread.asm'
MODULES = ('cupidbuild_source_path', 'cupidbuild_source_file', 'cupidbuild_source_stage',
           'cupidbuild_disk_integer', 'cupidbuild_disk_options', 'cupidbuild_disk_input_discovery')


class SourceOwnerPlanTests(unittest.TestCase):
    def setUp(self):
        self.parent = historical_disk_manifest(json.loads(
            (ROOT / 'bootstrap/seeds/i386-linux/manifest.json').read_bytes()))
        self.plan = b._candidate_build_plan(self.parent['build_plan'])
        self.flags = dict(windows_utf8=True, windows_long_paths=True,
                          windows_user_link_aliases=True, windows_observer_workers=True)

    def test_current_candidate_adds_six_modules_and_one_common_codec(self):
        before = copy.deepcopy(self.parent)
        self.assertEqual(len(self.parent['build_plan']['sources']), 37)
        self.assertEqual(len(self.plan['sources']), 44)
        self.assertEqual(len(self.plan['links']['cupidbuild']), 30)
        self.assertEqual(self.plan['links']['cupidbuild'][-8:], [*MODULES, 'path_encoding', 'runtime'])
        self.assertEqual(b._build_plan_sha256(self.plan),
                         'e58a047f92d21a9ffbae6cc19726477f95ca3a872f1238d9cbab58dae950faaa')
        self.assertEqual(b._candidate_build_plan(self.plan), self.plan)
        b._validate_build_plan(self.parent, promoted=True)
        self.assertEqual(self.parent, before)
        self.assertEqual(tuple(self.parent['build_plan']['links']['cupidbuild']), b.PROMOTED_CUPIDBUILD_LINK)

    def test_candidate_reserved_sources_and_peer_link_closure_fail_closed(self):
        for name in (*MODULES, 'path_encoding'):
            changed = copy.deepcopy(self.plan)
            next(row for row in changed['sources'] if row['name'] == name)['path'] = '/wrong.cc'
            with self.subTest(name=name), self.assertRaisesRegex(b.BootstrapError, 'reserved candidate source'):
                b._candidate_build_plan(changed)
        for mode in ('missing', 'duplicate', 'order'):
            changed = copy.deepcopy(self.plan)
            order = changed['links']['cupidbuild']
            if mode == 'missing':
                order.remove('cupidbuild_disk_input_discovery')
            elif mode == 'duplicate':
                order.insert(-1, 'path_encoding')
            else:
                order[-2], order[-3] = order[-3], order[-2]
            with self.subTest(mode=mode), self.assertRaisesRegex(b.BootstrapError, 'candidate link differs'):
                b._candidate_build_plan(changed)

    def test_current_source_and_sdk_capture_agree_with_no_duplicate_codec(self):
        captured = b.capture_source_snapshot(ROOT, self.plan, **self.flags)
        self.assertEqual(len(captured), 114)
        self.assertIn(TABLE, captured)
        self.assertIn(BRIDGE, captured)
        with policy._PinnedRepository(ROOT) as reader:
            selected = sdk._bootstrap_input_logical_paths(reader, self.plan,
                windows_long_paths=True, windows_user_link_aliases=True, windows_observer_workers=True)
            self.assertEqual(set(selected), set(captured))
            self.assertEqual(selected.count('toolchain/path_encoding.cc'), 1)
        native = b._windows_build_plan(self.plan, utf8=True, long_paths=True,
                                      user_link_aliases=True, observer_workers=True)
        self.assertEqual((len(native['sources']), len(native['assembly_sources'])), (48, 6))
        self.assertEqual(sum(row['name'] == 'path_encoding' for row in native['sources']), 1)
        self.assertEqual(native['links']['cupidbuild'].count('path_encoding'), 1)
        for row in native['sources'] + native['assembly_sources']:
            self.assertIn(row['path'].lstrip('/'), captured)

    def test_duplicate_codec_input_and_missing_integer_table_reject_before_freeze(self):
        changed = copy.deepcopy(self.plan)
        changed['sources'].append({'name': 'duplicate_codec', 'path': '/toolchain/path_encoding.cc',
                                   'gnu_extensions': False})
        with self.assertRaisesRegex(b.BootstrapError, 'source input is duplicated'):
            b.capture_source_snapshot(ROOT, changed, **self.flags)
        with tempfile.TemporaryDirectory() as temporary:
            top = Path(temporary)
            live = b.freeze_source_inputs(ROOT, self.plan, top / 'live', **self.flags)
            (live.root / TABLE).unlink()
            with self.assertRaisesRegex(b.BootstrapError, 'cannot resolve source input'):
                b.freeze_source_inputs(live.root, self.plan, top / 'missing', **self.flags)
            self.assertFalse((top / 'missing').exists())

    def test_integer_table_and_worker_selection_survive_both_closure_checks(self):
        with tempfile.TemporaryDirectory() as temporary:
            top = Path(temporary)
            live = b.freeze_source_inputs(ROOT, self.plan, top / 'live', **self.flags)
            frozen = b.freeze_source_inputs(live.root, self.plan, top / 'frozen', **self.flags)
            for directory in (live.root, frozen.root):
                path = directory / TABLE
                original = path.read_bytes()
                path.write_bytes(original + b'\n/* mutation */\n')
                with self.assertRaisesRegex(b.BootstrapError, 'source inputs changed'):
                    b.require_source_closures(frozen, live.root, self.plan)
                path.write_bytes(original)
                b.require_source_closures(frozen, live.root, self.plan)
            with self.assertRaisesRegex(b.BootstrapError, 'source inputs changed'):
                b.require_frozen_source_snapshot(replace(frozen, windows_observer_workers=False), self.plan)
