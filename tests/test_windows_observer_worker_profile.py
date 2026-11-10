"""Bind the worker adapter without changing installed Windows profiles."""

import copy
from dataclasses import replace
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from tools import bootstrap_toolchain as bootstrap

ROOT = Path(__file__).resolve().parents[1]
BRIDGE = 'toolchain/hosted/i386-windows/observer_thread.asm'
IMPORT = {'library': 'KERNEL32.dll', 'procedure': 'CreateThread',
          'slot': '__imp_CreateThread'}
PROFILES = ((False, False, False), (True, False, False), (True, True, False),
            (True, False, True), (True, True, True))


class WindowsObserverWorkerProfileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.linux = json.loads((ROOT / 'bootstrap/seeds/i386-linux/manifest.json')
                               .read_bytes())['build_plan']

    def test_workers_add_one_cupidbuild_import_and_bridge_to_each_legacy_profile(self):
        for utf8, long_paths, aliases in PROFILES:
            flags = dict(utf8=utf8, long_paths=long_paths, user_link_aliases=aliases)
            with self.subTest(flags=flags):
                old = bootstrap._windows_build_plan(self.linux, **flags)
                original = copy.deepcopy(old)
                new = bootstrap._windows_build_plan(self.linux, **flags, observer_workers=True)
                self.assertEqual(bootstrap._windows_plan_configuration(old),
                    bootstrap.WindowsPlanProfile(utf8, long_paths, aliases, False))
                self.assertEqual(bootstrap._windows_plan_configuration(new),
                    bootstrap.WindowsPlanProfile(utf8, long_paths, aliases, True))
                self.assertEqual(bootstrap._windows_plan_profile(new), (utf8, long_paths, aliases))
                self.assertEqual(new['sources'], old['sources'])
                self.assertEqual(new['assembly_sources'], old['assembly_sources'] +
                                 [{'name': 'observer_thread', 'path': '/' + BRIDGE}])
                for name in new['links']:
                    expected_imports = old['imports'][name].copy()
                    if name == 'cupidbuild':
                        position = next(index for index, row in enumerate(expected_imports)
                            if row['library'] != 'KERNEL32.dll' or row['procedure'] > 'CreateThread')
                        expected_imports.insert(position, IMPORT)
                    self.assertEqual(new['imports'][name], expected_imports)
                    order = new['links'][name].copy()
                    if name == 'cupidbuild':
                        self.assertEqual(order.count('observer_thread'), 1)
                        order.remove('observer_thread')
                    self.assertEqual(order, old['links'][name])
                    objects = {key: Path(key + '.o') for key in new['links'][name]}
                    arguments = bootstrap._windows_link_arguments(name, Path('out.exe'),
                        objects, new['links'][name], **flags, observer_workers=True)
                    self.assertEqual(arguments.count(
                        '__imp_CreateThread=KERNEL32.dll:CreateThread'), int(name == 'cupidbuild'))
                self.assertEqual(old, original)
                self.assertEqual(bootstrap._windows_build_plan(self.linux, **flags), original)

    def test_worker_profile_rejects_partial_or_foreign_adapter_and_import_bindings(self):
        original = bootstrap._windows_build_plan(self.linux, utf8=True,
            long_paths=True, user_link_aliases=True, observer_workers=True)
        mutations = []
        for location in ('assembly_sources', 'links', 'imports'):
            changed = copy.deepcopy(original)
            if location == 'assembly_sources':
                changed[location] = [r for r in changed[location] if r['name'] != 'observer_thread']
            elif location == 'links':
                changed[location]['cupidbuild'].remove('observer_thread')
            else:
                changed[location]['cupidbuild'].remove(IMPORT)
            mutations.append(changed)
        changed = copy.deepcopy(original)
        changed['assembly_sources'][-1]['path'] = '/toolchain/hosted/i386-windows/tool_start.asm'
        mutations.append(changed)
        changed = copy.deepcopy(original)
        changed['assembly_sources'].append(changed['assembly_sources'][-1].copy())
        mutations.append(changed)
        for name in ('cupidbuild', 'cupidc'):
            changed = copy.deepcopy(original)
            changed['links'][name].append('observer_thread')
            mutations.append(changed)
            changed = copy.deepcopy(original)
            changed['imports'][name].append(IMPORT.copy())
            mutations.append(changed)
        legacy = bootstrap._windows_build_plan(self.linux, utf8=True)
        legacy['assembly_sources'].append({'name': 'observer_thread', 'path': '/' + BRIDGE})
        mutations.append(legacy)
        reserved = copy.deepcopy(original)
        next(row for row in reserved['imports']['cupidbuild']
             if row['procedure'] == 'CreateThread')['slot'] = '__imp_wrong'
        mutations.append(reserved)
        for index, changed in enumerate(mutations):
            with self.subTest(index=index), self.assertRaises(bootstrap.BootstrapError):
                bootstrap._windows_plan_configuration(changed)

    def test_checked_linker_emits_the_exact_worker_import_profile(self):
        with tempfile.TemporaryDirectory(prefix='.worker-profile-', dir=ROOT) as temporary:
            directory = Path(temporary)
            host = 'windows' if os.name == 'nt' else 'linux'
            seed = bootstrap.freeze_seed_inputs(ROOT / ('bootstrap/seeds/i386-' + host + '/manifest.json'),
                                                directory / 'seed')
            source = directory / 'entry.asm'
            imports = bootstrap._windows_profile_imports('cupidbuild', utf8=True, long_paths=True,
                user_link_aliases=True, observer_workers=True)
            slots = ['__imp_' + name for _library, procedures in imports for name in procedures]
            source.write_text('[BITS 32]\nglobal _start:function\n' +
                ''.join('extern ' + slot + '\n' for slot in slots) +
                'section .text\n_start:\n xor eax, eax\n ret\nsection .data\n' +
                ''.join('dd ' + slot + '\n' for slot in slots), encoding='utf-8')
            obj, program = directory / 'entry.o', directory / 'worker.exe'
            runner = bootstrap.ToolRunner(ROOT)
            bootstrap._run_clean(runner, seed.tools['cupidasm'],
                ['-f', 'elf32', source, '-o', obj], 'worker profile assembly', 120)
            bootstrap._run_clean(runner, seed.tools['cupidld'],
                bootstrap._windows_link_arguments('cupidbuild', program, {'start': obj}, ['start'],
                    utf8=True, long_paths=True, user_link_aliases=True, observer_workers=True),
                'worker profile PE link', 120)
            bootstrap._validate_static_i386_pe32(program, int(bootstrap.EXPECTED_WINDOWS_TARGET['entry']),
                bootstrap._windows_profile_imports('cupidbuild', utf8=True, long_paths=True,
                    user_link_aliases=True, observer_workers=True))
            bootstrap.require_live_seed_inputs(seed)

    def test_stage_uses_the_worker_profile_for_linking_and_pe_validation(self):
        plan = bootstrap._windows_build_plan(self.linux, utf8=True,
            long_paths=True, user_link_aliases=True, observer_workers=True)
        with tempfile.TemporaryDirectory(prefix='.worker-profile-', dir=ROOT) as temporary, mock.patch.object(
                bootstrap, '_run_clean') as run, mock.patch.object(
                bootstrap, '_validate_i386_relocatable'), mock.patch.object(
                bootstrap, '_certify_relocatable_code_anchors'), mock.patch.object(
                bootstrap, '_validate_static_i386_pe32') as validate:
            producers = {name: Path(name) for name in bootstrap.CANDIDATE_TOOL_NAMES}
            stage = bootstrap._build_windows_stage(mock.Mock(), ROOT,
                Path(temporary) / 'stage', producers, plan, 'worker fixture')
            self.assertEqual(set(stage.tools), set(plan['links']))
            calls = [call for call in run.call_args_list if call.args[1] == producers['cupidld']]
            self.assertEqual(len(calls), len(plan['links']))
            links_by_name = {Path(str(call.args[2][call.args[2].index('-o') + 1])).stem: call
                             for call in calls}
            validation_by_name = {Path(str(call.args[0])).stem: call
                                  for call in validate.call_args_list}
            for name in plan['links']:
                call, validation = links_by_name[name], validation_by_name[name]
                arguments = call.args[2]
                self.assertEqual(arguments.count('__imp_CreateThread=KERNEL32.dll:CreateThread'),
                                 int(name == 'cupidbuild'))
                self.assertEqual(sum(Path(str(arg)).name == 'observer_thread.o'
                                     for arg in arguments), int(name == 'cupidbuild'))
                actual_imports = [(library, procedure) for library, procedures in validation.args[2]
                                  for procedure in procedures]
                self.assertEqual(actual_imports, [(r['library'], r['procedure'])
                                                  for r in plan['imports'][name]])

    def test_behavior_retarget_binds_worker_selection_and_parent_identity(self):
        parent = bootstrap.verify_seed_inputs(ROOT / 'bootstrap/seeds/i386-linux/manifest.json')
        native = bootstrap.verify_seed_inputs(ROOT / 'bootstrap/seeds/i386-windows/manifest.json')
        plan = bootstrap._windows_build_plan(self.linux, observer_workers=True)
        digest = bootstrap._build_plan_sha256(plan)
        snapshot = bootstrap.capture_source_snapshot(ROOT, self.linux, windows_observer_workers=True)
        changed = bootstrap._retarget_native_windows_behavior_seed(native, digest,
            self.linux, snapshot, observer_workers=True, parent_plan_seed=parent)
        provenance = changed.manifest['provenance']
        self.assertEqual(provenance['native_build_plan_sha256'], digest)
        self.assertEqual(provenance['source_snapshot_sha256'], bootstrap._source_snapshot_sha256(snapshot))
        self.assertEqual(provenance['source_input_count'], len(snapshot))
        self.assertEqual(provenance['parent_execution_seed_manifest_sha256'], native.manifest_sha256)
        self.assertEqual(provenance['parent_plan_seed_manifest_sha256'], parent.manifest_sha256)
        self.assertEqual(changed.artifact_bytes, native.artifact_bytes)
        with self.assertRaisesRegex(bootstrap.BootstrapError, 'build plan differs'):
            bootstrap._retarget_native_windows_behavior_seed(native, digest,
                self.linux, snapshot, parent_plan_seed=parent)
        with self.assertRaisesRegex(bootstrap.BootstrapError, 'parent plan seed is unavailable'):
            bootstrap._retarget_native_windows_behavior_seed(native, digest,
                self.linux, snapshot, observer_workers=True)

    def test_non_boolean_selection_and_reserved_name_fail_before_freeze(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary) / 'frozen'
            for value in (None, 0, 1, 'yes', [], {}):
                with self.subTest(value=value):
                    for action in (
                        lambda: bootstrap._windows_build_plan(self.linux, observer_workers=value),
                        lambda: bootstrap._windows_import_selectors('cupidbuild', observer_workers=value),
                        lambda: bootstrap.freeze_source_inputs(ROOT, self.linux, destination,
                            windows_observer_workers=value),
                    ):
                        with self.assertRaisesRegex(bootstrap.BootstrapError, 'Boolean'):
                            action()
                    self.assertFalse(destination.exists())
        changed = copy.deepcopy(self.linux)
        changed['sources'].append({'name': 'observer_thread', 'path': '/bad.cc',
                                   'gnu_extensions': False})
        with self.assertRaisesRegex(bootstrap.BootstrapError, 'reserved Windows source'):
            bootstrap._windows_build_plan(changed, observer_workers=True)

    def test_selected_adapter_is_frozen_and_both_live_and_frozen_drift_are_rejected(self):
        flags = dict(windows_utf8=True, windows_long_paths=True,
                     windows_user_link_aliases=True, windows_observer_workers=True)
        old = bootstrap.capture_source_snapshot(ROOT, self.linux,
            **{**flags, 'windows_observer_workers': False})
        current = bootstrap.capture_source_snapshot(ROOT, self.linux, **flags)
        self.assertEqual(set(current) - set(old), {BRIDGE})
        with tempfile.TemporaryDirectory() as temporary:
            top = Path(temporary)
            live = bootstrap.freeze_source_inputs(ROOT, self.linux, top / 'live', **flags)
            frozen = bootstrap.freeze_source_inputs(live.root, self.linux, top / 'frozen', **flags)
            self.assertTrue(frozen.windows_observer_workers)
            bootstrap.require_source_closures(frozen, live.root, self.linux)
            for directory, label in ((live.root, 'source inputs'),
                                     (frozen.root, 'frozen source inputs')):
                path = directory / BRIDGE
                saved = path.read_bytes()
                path.write_bytes(saved + b'\n; mutation\n')
                with self.assertRaisesRegex(bootstrap.BootstrapError, label + ' changed'):
                    bootstrap.require_source_closures(frozen, live.root, self.linux)
                path.write_bytes(saved)
                bootstrap.require_source_closures(frozen, live.root, self.linux)
            with self.assertRaisesRegex(bootstrap.BootstrapError, 'frozen source inputs changed'):
                bootstrap.require_frozen_source_snapshot(
                    replace(frozen, windows_observer_workers=False), self.linux)
            (live.root / BRIDGE).unlink()
            with self.assertRaisesRegex(bootstrap.BootstrapError, 'cannot resolve source input'):
                bootstrap.freeze_source_inputs(live.root, self.linux, top / 'missing', **flags)
            self.assertFalse((top / 'missing').exists())


if __name__ == '__main__':
    unittest.main()
