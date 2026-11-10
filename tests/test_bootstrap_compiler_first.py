"""A validated source compiler precedes compilation of its peer tools."""
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from tools import bootstrap_toolchain as b


class CompilerFirstStageTests(unittest.TestCase):
    def plan(self, windows):
        plan = {'sources': [
            {'name': 'runtime', 'path': '/runtime.cc', 'gnu_extensions': True},
            {'name': 'cupidbuild_host', 'path': '/peer.cc', 'gnu_extensions': False}],
            'startup': '/start.asm', 'include_arguments': [], 'workers': 2,
            'links': {name: ['start', 'runtime', *(['cupidbuild_host'] if name == 'cupidbuild' else [])]
                      for name in b.CANDIDATE_TOOL_NAMES}}
        return b._windows_build_plan(plan, utf8=True, long_paths=True,
            user_link_aliases=True, observer_workers=True) if windows else plan

    def run_stage(self, windows, reject_compiler=False):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stage_root = root / 'stage'
            producers = {name: root / ('incoming-' + name) for name in
                         ('cupidc', 'cupidasm', 'cupidld', 'cupiddis')}
            plan = self.plan(windows)
            events = []

            def run(_runner, executable, arguments, _label, _timeout):
                if '-c' in arguments:
                    events.append(('compile', Path(str(arguments[-1])).stem, Path(executable)))
                elif '-o' in arguments and executable == producers['cupidld']:
                    path = Path(arguments[arguments.index('-o') + 1])
                    events.append(('link', path.stem, Path(executable)))

            def certify(_runner, _tool, path, _label):
                events.append(('certify', path.stem, None))

            def image(path, *_arguments):
                events.append(('image', path.stem, None))
                if reject_compiler and path.stem == 'cupidc':
                    raise b.BootstrapError('rejected compiler image')

            with mock.patch.object(b, '_run_clean', side_effect=run), mock.patch.object(
                    b, '_validate_i386_relocatable'), mock.patch.object(
                    b, '_certify_relocatable_code_anchors', side_effect=certify), mock.patch.object(
                    b, '_validate_static_i386_elf', side_effect=image), mock.patch.object(
                    b, '_validate_static_i386_pe32', side_effect=image):
                stage = None
                try:
                    builder = b._build_windows_stage if windows else b._build_stage
                    stage = builder(mock.Mock(), root, stage_root, producers, plan, 'compiler first')
                except b.BootstrapError:
                    if not reject_compiler:
                        raise
                if reject_compiler:
                    self.assertIsNone(stage)
                    self.assertIn(('image', 'cupidc', None), events)

            source_names = {row['name'] for row in plan['sources']}
            compiler_names = source_names & set(plan['links']['cupidc'])
            peer_names = source_names - compiler_names
            current = stage_root / ('cupidc.exe' if windows else 'cupidc.elf')
            compiler_link = events.index(('link', 'cupidc', producers['cupidld']))
            compiler_validation = events.index(('image', 'cupidc', None))
            for name in compiler_names:
                self.assertLess(events.index(('compile', name, producers['cupidc'])), compiler_link)
                self.assertLess(events.index(('certify', name, None)), compiler_link)
            if reject_compiler:
                self.assertFalse(any(event[0] == 'compile' and event[1] in peer_names for event in events))
                self.assertEqual([event[1] for event in events if event[0] == 'link'], ['cupidc'])
            else:
                self.assertEqual([event[1] for event in events if event[0] == 'compile'].count('runtime'), 1)
                self.assertEqual({event[1] for event in events if event[0] == 'compile'}, source_names)
                self.assertEqual(len([event for event in events if event[0] == 'compile']), len(source_names))
                for name in peer_names:
                    self.assertLess(compiler_validation, events.index(('compile', name, current)))
                    for tool, order in plan['links'].items():
                        if name in order:
                            self.assertLess(events.index(('certify', name, None)),
                                            events.index(('link', tool, producers['cupidld'])))
                self.assertEqual(set(stage.tools), set(b.CANDIDATE_TOOL_NAMES))
                self.assertEqual(len([event for event in events if event[0] == 'link']), 6)

    def test_compiler_is_certified_before_any_peer_compilation(self):
        for windows in (False, True):
            with self.subTest(windows=windows):
                self.run_stage(windows)

    def test_invalid_compiler_image_stops_before_any_peer_compilation(self):
        for windows in (False, True):
            with self.subTest(windows=windows):
                self.run_stage(windows, reject_compiler=True)

    def test_source_partition_rejects_duplicates_and_unknown_compiler_objects(self):
        sources = [{'name': 'core'}, {'name': 'peer'}]
        with self.assertRaisesRegex(b.BootstrapError, 'repeats'):
            b._compiler_stage_sources(sources + [sources[0]], {'cupidc': ['start', 'core']}, ('start',))
        with self.assertRaisesRegex(b.BootstrapError, 'unknown objects'):
            b._compiler_stage_sources(sources, {'cupidc': ['start', 'missing']}, ('start',))
