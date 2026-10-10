"""Bind current bundle plans, source counts and installed parents in the C reader."""
import copy
import ctypes
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tests import test_seed_manifest as historical
from tests.test_seed_release import ROOT, encode
from tools import bootstrap_toolchain as bootstrap


PROFILES = (
    (False, False, False, 80, 1),
    (True, False, False, 85, 2),
    (True, True, False, 86, 3),
    (True, False, True, 86, 4),
    (True, True, True, 87, 5),
)

PUBLICATION_PROFILES = (
    (False, False, False, 85, 1),
    (True, False, False, 90, 2),
    (True, True, False, 91, 3),
    (True, False, True, 91, 4),
    (True, True, True, 92, 5),
)

DISK_FOUNDATION_PROFILES = (
    (False, False, False, 92, 1),
    (True, False, False, 97, 2),
    (True, True, False, 98, 3),
    (True, False, True, 98, 4),
    (True, True, True, 99, 5),
)
DISK_OBJECTS = {'fat16_stage', 'fat16_names', 'disk_image'}


def bundle_only_plan(plan):
    plan = copy.deepcopy(plan)
    removed = {'cupidbuild_iso', 'cupidbuild_iso_capture', 'cupidbuild_iso_image', 'cupidbuild_iso_publication'}
    plan['sources'] = [r for r in plan['sources'] if r['name'] not in removed]
    plan['links']['cupidbuild'] = list(bootstrap.ISO_BUNDLE_CUPIDBUILD_LINK)
    return plan


class IsoSeedProfileTests(unittest.TestCase):
    profiles = PROFILES
    publication = False
    disk_foundation = False
    unload = classmethod(historical.ManifestTests.unload.__func__)

    @classmethod
    def setUpClass(cls):
        historical.ManifestTests.setUpClass.__func__(cls)
        cls.linux = bootstrap.verify_seed_inputs(ROOT / 'bootstrap/seeds/i386-linux/manifest.json')
        cls.windows = bootstrap.verify_seed_inputs(ROOT / 'bootstrap/seeds/i386-windows/manifest.json')
        bootstrap._require_seed_pair_identity(cls.windows, cls.linux)
        cls.plan = bootstrap._candidate_build_plan(cls.linux.manifest['build_plan'])
        if not cls.disk_foundation:
            cls.plan['sources'] = [r for r in cls.plan['sources'] if r['name'] not in DISK_OBJECTS]
            cls.plan['links']['cupidbuild'] = list(bootstrap.ISO_PUBLICATION_CUPIDBUILD_LINK)
        if not cls.publication:
            cls.plan = bundle_only_plan(cls.plan)
        cls.fixtures = []
        for utf8, long_paths, aliases, expected_count, profile in cls.profiles:
            snapshot = bootstrap.capture_source_snapshot(ROOT, cls.plan, windows_utf8=utf8,
                windows_long_paths=long_paths, windows_user_link_aliases=aliases)
            if not cls.disk_foundation:
                snapshot = {name: row for name, row in snapshot.items()
                            if name not in {'toolchain/' + object_name + '.h' for object_name in DISK_OBJECTS}}
            if not cls.publication:
                snapshot = {name: row for name, row in snapshot.items()
                            if name != 'toolchain/cupidbuild_iso_publication.h'}
            if len(snapshot) != expected_count:
                raise AssertionError((profile, len(snapshot), expected_count))
            native = bootstrap._windows_build_plan(cls.plan, utf8=utf8, long_paths=long_paths,
                user_link_aliases=aliases)
            windows = bootstrap._retarget_native_windows_behavior_seed(cls.windows,
                bootstrap._build_plan_sha256(native), cls.plan, snapshot,
                utf8=utf8, long_paths=long_paths, user_link_aliases=aliases,
                parent_plan_seed=cls.linux)
            linux = copy.deepcopy(cls.linux.manifest)
            linux['build_plan'] = copy.deepcopy(cls.plan)
            linux['build_plan_sha256'] = bootstrap._build_plan_sha256(cls.plan)
            linux['provenance'].update(source_input_count=len(snapshot),
                source_snapshot_sha256=bootstrap._source_snapshot_sha256(snapshot),
                parent_seed_manifest_sha256=cls.linux.manifest_sha256,
                parent_seed_source_revision=cls.linux.manifest['provenance']['source_revision'])
            cls.fixtures.append((linux, json.loads(windows.manifest_bytes), profile))

    def release(self, value, fmt):
        provenance = value['provenance']
        parent_revision = provenance['parent_execution_seed_source_revision' if fmt == 2 else 'parent_seed_source_revision']
        parent_linux = provenance['parent_plan_seed_manifest_sha256' if fmt == 2 else 'parent_seed_manifest_sha256']
        parent_windows = provenance['parent_execution_seed_manifest_sha256'] if fmt == 2 else self.windows.manifest_sha256
        return {
            'schema': 'cupid.seed-release.v1', 'source_revision': provenance['source_revision'],
            'source_snapshot_sha256': provenance['source_snapshot_sha256'],
            'source_input_count': provenance['source_input_count'],
            'parent_source_revision': parent_revision, 'parent_linux_manifest_sha256': parent_linux,
            'parent_windows_manifest_sha256': parent_windows,
            'linux_plan_sha256': provenance['linux_candidate_build_plan_sha256'] if fmt == 2 else value['build_plan_sha256'],
            'windows_plan_sha256': provenance['native_build_plan_sha256'] if fmt == 2 else self.windows.manifest['provenance']['native_build_plan_sha256'],
            'artifacts': [{'name': row['name'], 'format': artifact_format, 'size': row['size'], 'sha256': row['sha256']}
                for artifact_format, manifest in (('elf32', value if fmt == 1 else self.linux.manifest),
                    ('pe32', value if fmt == 2 else self.windows.manifest)) for row in manifest['artifacts']],
        }

    def check(self, value, fmt, profile=0, accepted=True, capacity=128, record=None, strict=False):
        raw = encode(value) if isinstance(value, dict) else value
        incoming = ctypes.create_string_buffer(raw)
        before = bytes(incoming)
        result = historical.Result()
        ctypes.memset(ctypes.byref(result), 0xa5, ctypes.sizeof(result))
        error = ctypes.create_string_buffer(b'!' * (capacity + 8))
        if strict:
            status = self.api(incoming, len(raw), fmt, ctypes.byref(result), error, capacity)
        else:
            if record is None:
                original = next((linux if fmt == 1 else windows for linux, windows, number in self.fixtures
                    if number == (profile or 1)), self.fixtures[0][0])
                record = self.release(original, fmt)
            release_raw = encode(record)
            release_buffer = ctypes.create_string_buffer(release_raw)
            release_before = bytes(release_buffer)
            status = self.release_api(release_buffer, len(release_raw), incoming, len(raw),
                fmt, ctypes.byref(result), error, capacity)
            self.assertEqual(bytes(release_buffer), release_before)
        self.assertEqual(status, int(accepted), (fmt, profile, error.value))
        self.assertEqual(bytes(incoming), before)
        self.assertEqual(bytes(error)[capacity:], b'!' * 8 + b'\0')
        if accepted:
            self.assertEqual((result.artifact_count, result.current_windows_plan),
                (6, profile if fmt == 2 else 0))
            if capacity:
                self.assertEqual(error.value, b'')
            document = json.loads(raw)
            for index, role in enumerate(('cupidasm', 'cupidc', 'cupiddis', 'cupidld', 'cupidobj', 'cupidbuild')):
                artifact = next(row for row in document['artifacts'] if row['name'] == role)
                self.assertEqual((result.artifacts[index].file.decode(), result.artifacts[index].size,
                    result.artifacts[index].sha256.decode()),
                    (artifact['file'], artifact['size'], artifact['sha256']))
        else:
            self.assertEqual(bytes(result), bytes(ctypes.sizeof(result)))
            if capacity > 1:
                self.assertTrue(error.value)
        if os.environ.get('CUPID_ISO_SEED_PROFILE_CALLER'):
            with tempfile.TemporaryDirectory(prefix='cupid-iso-profile-case-') as temporary:
                folder = Path(temporary)
                manifest_path = folder / 'manifest.json'
                release_path = folder / 'release.json'
                manifest_path.write_bytes(raw)
                release_path.write_bytes(b'' if strict else release_raw)
                checked = subprocess.run([os.environ['CUPID_ISO_SEED_PROFILE_CALLER'],
                    str(fmt), str(capacity), str(int(strict)), str(manifest_path), str(release_path)],
                    capture_output=True, text=True, timeout=30)
                self.assertEqual((checked.returncode, checked.stderr), (0, ''))
                fields = ['0']
                if accepted:
                    fields = ['1', str(result.artifact_count), str(result.current_windows_plan)]
                    for artifact in result.artifacts:
                        fields.extend((artifact.file.decode(), str(artifact.size), artifact.sha256.decode()))
                self.assertEqual(checked.stdout, ' '.join(fields) + '\n')
                self.assertEqual(manifest_path.read_bytes(), raw)
                self.assertEqual(release_path.read_bytes(), b'' if strict else release_raw)

    def test_current_linux_and_all_five_windows_profiles(self):
        for linux, windows, profile in self.fixtures:
            with self.subTest(profile=profile):
                self.check(linux, 1, record=self.release(linux, 1))
                self.check(windows, 2, profile)

    def test_semantic_field_order_and_whitespace_do_not_change_profile(self):
        for linux, windows, profile in self.fixtures:
            for fmt, original in ((1, linux), (2, windows)):
                value = dict(reversed(list(original.items())))
                value['provenance'] = dict(reversed(list(value['provenance'].items())))
                self.check(json.dumps(value, indent=2).encode(), fmt, profile, record=self.release(original, fmt))

    def test_source_count_and_both_plan_hashes_are_bound_together(self):
        for _, windows, profile in self.fixtures:
            for key in ('source_input_count', 'linux_candidate_build_plan_sha256', 'native_build_plan_sha256'):
                altered = copy.deepcopy(windows)
                old = altered['provenance'][key]
                altered['provenance'][key] = old + 1 if isinstance(old, int) else '0' * 64
                self.check(altered, 2, profile, False)

    def test_overlapping_counts_do_not_authorize_a_different_windows_plan(self):
        for _, windows, profile in self.fixtures:
            for _, other, other_profile in self.fixtures:
                if profile == other_profile:
                    continue
                changed = copy.deepcopy(windows)
                changed['provenance']['native_build_plan_sha256'] = other['provenance']['native_build_plan_sha256']
                # Long-path and alias profiles share a count but have distinct plans.
                accepted = profile in (3, 4) and other_profile in (3, 4)
                self.check(changed, 2, other_profile if accepted else profile, accepted,
                    record=self.release(changed if accepted else windows, 2))

    def test_each_parent_member_is_required(self):
        for linux, windows, profile in self.fixtures:
            for fmt, original in ((1, linux), (2, windows)):
                for field in original['provenance']:
                    if not field.startswith('parent_'):
                        continue
                    changed = copy.deepcopy(original)
                    changed['provenance'][field] = '0' * len(changed['provenance'][field])
                    self.check(changed, fmt, profile, False, record=self.release(original, fmt))

    def test_historical_parent_tuple_cannot_authorize_the_current_plan(self):
        for linux, windows, profile in self.fixtures:
            for fmt, original in ((1, linux), (2, windows)):
                changed = copy.deepcopy(original)
                old = historical.historical_manifest(fmt)['provenance']
                for field in changed['provenance']:
                    if field.startswith('parent_'):
                        changed['provenance'][field] = old[field]
                self.check(changed, fmt, profile, False, record=self.release(original, fmt))

    def test_codec_source_and_link_membership_are_exact(self):
        linux = self.fixtures[0][0]
        plan = linux['build_plan']
        index = next(index for index, row in enumerate(plan['sources']) if row['name'] == 'iso_fixture_bundle')
        for key in ('name', 'path', 'gnu_extensions'):
            changed = copy.deepcopy(linux)
            old = changed['build_plan']['sources'][index][key]
            changed['build_plan']['sources'][index][key] = not old if isinstance(old, bool) else 'wrong'
            self.check(changed, 1, accepted=False)
        for section, extra in (('sources', copy.deepcopy(plan['sources'][index])),
                               ('links', 'iso_fixture_bundle')):
            for operation in ('remove', 'duplicate'):
                changed = copy.deepcopy(linux)
                rows = changed['build_plan']['sources'] if section == 'sources' else changed['build_plan']['links']['cupidobj']
                if operation == 'remove':
                    rows.remove(extra)
                else:
                    rows.append(extra)
                self.check(changed, 1, accepted=False)

    def test_codec_link_order_and_unrelated_links_are_checked(self):
        linux = self.fixtures[0][0]
        for tool in linux['build_plan']['links']:
            changed = copy.deepcopy(linux)
            changed['build_plan']['links'][tool].reverse()
            self.check(changed, 1, accepted=False)
        changed = copy.deepcopy(linux)
        changed['build_plan']['links']['cupidbuild'].insert(1, 'iso_fixture_bundle')
        self.check(changed, 1, accepted=False)

    def test_legacy_manifest_still_uses_its_own_contract(self):
        self.check(historical.manifest(1), 1, strict=True)
        self.check(historical.manifest(2), 2, 5, strict=True)
        changed = copy.deepcopy(self.fixtures[0][0])
        changed['build_plan_sha256'] = historical.manifest(1)['build_plan_sha256']
        self.check(changed, 1, accepted=False)

    def test_failure_clears_result_and_recovers_with_bounded_diagnostics(self):
        for capacity in (0, 1, 2, 8, 128):
            self.check(b'bad', 2, accepted=False, capacity=capacity)
            self.check(self.fixtures[-1][1], 2, 5, capacity=capacity)

    def test_new_profiles_require_explicit_release_before_and_after_authorized_calls(self):
        for linux, windows, profile in self.fixtures:
            for fmt, value in ((1, linux), (2, windows)):
                self.check(value, fmt, profile, False, strict=True)
                self.check(value, fmt, profile, record=self.release(value, fmt))
                self.check(value, fmt, profile, False, strict=True)

    def test_matching_release_does_not_authorize_unknown_counts_or_plans(self):
        for linux, windows, profile in self.fixtures:
            for fmt, original in ((1, linux), (2, windows)):
                for key in ('source_input_count', 'native_build_plan_sha256' if fmt == 2 else 'build_plan_sha256'):
                    changed = copy.deepcopy(original)
                    target = changed['provenance'] if key != 'build_plan_sha256' else changed
                    old = target[key]
                    target[key] = old + 10 if isinstance(old, int) else '0' * 64
                    self.check(changed, fmt, profile, False, record=self.release(changed, fmt))


class IsoPublicationSeedProfileTests(IsoSeedProfileTests):
    profiles = PUBLICATION_PROFILES
    publication = True

    def test_guarded_modules_and_build_link_are_exact(self):
        linux = self.fixtures[0][0]
        for name in ('cupidbuild_iso', 'cupidbuild_iso_capture', 'cupidbuild_iso_image', 'cupidbuild_iso_publication'):
            index = next(i for i, row in enumerate(linux['build_plan']['sources']) if row['name'] == name)
            for field in ('name', 'path', 'gnu_extensions'):
                changed = copy.deepcopy(linux)
                value = changed['build_plan']['sources'][index][field]
                changed['build_plan']['sources'][index][field] = not value if isinstance(value, bool) else 'wrong'
                self.check(changed, 1, accepted=False)
            for action in ('remove', 'duplicate'):
                changed = copy.deepcopy(linux)
                source_rows = changed['build_plan']['sources']
                if action == 'remove': source_rows.pop(index)
                else: source_rows.append(copy.deepcopy(source_rows[index]))
                self.check(changed, 1, accepted=False)
                changed = copy.deepcopy(linux)
                link = changed['build_plan']['links']['cupidbuild']
                if action == 'remove': link.remove(name)
                else: link.append(name)
                self.check(changed, 1, accepted=False)

    def test_shared_count_does_not_exchange_generation_plans(self):
        new = self.fixtures[0][1]
        old = copy.deepcopy(new)
        old['provenance']['linux_candidate_build_plan_sha256'] = 'b42d1522b1e4a34753fcf4c0ed3506336c66c62ca8dce998de7f066492bdc46e'
        old['provenance']['native_build_plan_sha256'] = 'fac6966af84cd362d43c3f8b5beb3b55c37a8d5c7184cc4c1d221e64b12b4627'
        self.assertEqual(old['provenance']['source_input_count'], 85)
        self.check(old, 2, 2, record=self.release(old, 2))
        self.check(new, 2, 1, record=self.release(new, 2))
        for document, other in ((new, old), (old, new)):
            for field in ('native_build_plan_sha256', 'linux_candidate_build_plan_sha256'):
                changed = copy.deepcopy(document)
                changed['provenance'][field] = other['provenance'][field]
                self.check(changed, 2, accepted=False, record=self.release(changed, 2))


class DiskFoundationSeedProfileTests(IsoSeedProfileTests):
    profiles = DISK_FOUNDATION_PROFILES
    publication = True
    disk_foundation = True

    def test_disk_module_sources_and_links_are_exact(self):
        linux = self.fixtures[0][0]
        for name in sorted(DISK_OBJECTS):
            index = next(i for i, row in enumerate(linux['build_plan']['sources']) if row['name'] == name)
            for field in ('name', 'path', 'gnu_extensions'):
                changed = copy.deepcopy(linux)
                old = changed['build_plan']['sources'][index][field]
                changed['build_plan']['sources'][index][field] = not old if isinstance(old, bool) else 'wrong'
                self.check(changed, 1, accepted=False, record=self.release(changed, 1))
            for action in ('remove', 'duplicate', 'reorder'):
                changed = copy.deepcopy(linux)
                sources = changed['build_plan']['sources']
                if action == 'remove': sources.pop(index)
                elif action == 'duplicate': sources.append(copy.deepcopy(sources[index]))
                else: sources[index], sources[index - 1] = sources[index - 1], sources[index]
                self.check(changed, 1, accepted=False, record=self.release(changed, 1))
                changed = copy.deepcopy(linux)
                link = changed['build_plan']['links']['cupidbuild']
                position = link.index(name)
                if action == 'remove': link.pop(position)
                elif action == 'duplicate': link.append(name)
                else: link[position], link[position - 1] = link[position - 1], link[position]
                self.check(changed, 1, accepted=False, record=self.release(changed, 1))

    def test_shared_count_cannot_exchange_historical_iso_and_disk_plans(self):
        disk = self.fixtures[0][1]
        historical_plan = copy.deepcopy(self.plan)
        historical_plan['sources'] = [row for row in historical_plan['sources']
                                      if row['name'] not in DISK_OBJECTS]
        historical_plan['links']['cupidbuild'] = list(bootstrap.ISO_PUBLICATION_CUPIDBUILD_LINK)
        snapshot = bootstrap.capture_source_snapshot(ROOT, historical_plan, windows_utf8=True,
            windows_long_paths=True, windows_user_link_aliases=True)
        snapshot = {name: value for name, value in snapshot.items()
                    if name not in {'toolchain/' + object_name + '.h' for object_name in DISK_OBJECTS}}
        native = bootstrap._windows_build_plan(historical_plan, utf8=True, long_paths=True,
                                               user_link_aliases=True)
        digest = bootstrap._build_plan_sha256(native)
        self.assertEqual(digest, '0dfd1982dc1cd7c9d625c4c0546fc20f13fe3c9ae4dc8cbcf8835ff2e6b4e12d')
        previous = bootstrap._retarget_native_windows_behavior_seed(self.windows, digest,
            historical_plan, snapshot, utf8=True, long_paths=True, user_link_aliases=True,
            parent_plan_seed=self.linux)
        installed = json.loads(previous.manifest_bytes)
        self.check(installed, 2, 5, record=self.release(installed, 2))
        self.check(disk, 2, 1, record=self.release(disk, 2))
        self.assertEqual(disk['provenance']['source_input_count'], installed['provenance']['source_input_count'])
        for original, other in ((disk, installed), (installed, disk)):
            for field in ('native_build_plan_sha256', 'linux_candidate_build_plan_sha256'):
                changed = copy.deepcopy(original)
                changed['provenance'][field] = other['provenance'][field]
                self.check(changed, 2, accepted=False, record=self.release(changed, 2))


if __name__ == '__main__':
    unittest.main()
