"""Reread every whole product and case in a completed temporary source-selection group replay."""
import argparse
import base64
import gzip
import hashlib
import json
from pathlib import Path

CASE_KEYS = ('role', 'label', 'case', 'mode', 'profile', 'family', 'namespace', 'shape', 'mutation',
    'parent_identity_only', 'expected_exit', 'expected_exit_code', 'expected_same', 'rejection_control',
    'expected_kind', 'distinct_requests', 'original_python_missing', 'status', 'test', 'group',
    'stage_count', 'expected_final_line', 'aggregate_payload_bytes', 'per_file_stream_limits_bytes', 'publication_namespace', 'source_payload_bytes', 'payload_streaming', 'previous_digest_limit_bytes', 'count')


def identity(data):
    return {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def case_plan(record):
    result = [{k: row[k] for k in CASE_KEYS if k in row} for row in record.get('records', [])]
    if record.get('schema') in ('cupid.retained-stream-group-contracts.v1', 'cupid.path-observation-group-contracts.v1'):
        for row in result:
            mutation = row.get('mutation')
            if isinstance(mutation, dict):
                if mutation['kind'] in ('same-size-restored-mtime', 'earlier-file-size-change'):
                    row['mutation'] = {k: mutation[k] for k in ('kind', 'original', 'changed')}
                elif mutation['kind'] == 'original-root-retarget': row['mutation'] = {'kind': mutation['kind']}
                else: raise ValueError('Unexpected group mutation')
    if record.get('schema') == 'cupid.file-request-group-contracts.v1':
        for row in result:
            mutation = row.get('mutation')
            if isinstance(mutation, dict):
                if mutation['kind'] in ('mutate-current', 'payload-restore', 'earlier-payload'):
                    row['mutation'] = {k: mutation[k] for k in ('kind', 'original', 'changed')}
                elif mutation['kind'] in ('parent-identity', 'parent-metadata', 'original-parent-alias-retarget', 'original-root-retarget'):
                    row['mutation'] = {'kind': mutation['kind']}
                else:
                    raise ValueError('Unexpected original-request group mutation')
    if record.get('schema') == 'cupid.optional-selection-path-group-contracts.v1':
        for row in result:
            mutation = row.get('mutation')
            if isinstance(mutation, dict):
                if mutation['kind'] in ('same-size-restored-mtime', 'earlier-file-size-change'):
                    row['mutation'] = {key: mutation[key] for key in ('kind', 'original', 'changed')}
                elif mutation['kind'] in ('original-root-retarget', 'root-metadata', 'uncaptured-root-sibling'):
                    row['mutation'] = {'kind': mutation['kind']}
                else:
                    raise ValueError('Unexpected source-selection group mutation')
    return result


def commands(folder, record, empty=True):
    strict = 0
    for row in record['commands']:
        if row['exit_code'] != 0:
            raise ValueError('A complete command failed: ' + row['label'])
        for stream in ('stdout', 'stderr'):
            data = (folder / (row['label'] + '-' + stream + '.bin')).read_bytes()
            if identity(data) != row[stream] or (empty and data != b''):
                raise ValueError('A complete command stream differs: ' + row['label'])
        if row['label'].startswith('strict-'):
            if not all(flag in row['argv'] for flag in ['--require-known', '--require-local-targets', '--require-code-anchors']):
                raise ValueError('A strict flag is missing')
            strict += 1
    return strict


def selection_output(raw, record):
    """Normalize only the replay directory and its native fixture directory."""
    fixture = record.get('native_fixture_root')
    if fixture:
        raw = raw.replace(fixture.replace('\\', '/').encode('utf-8'), b'<fixture>')
    base = Path(record['records'][0]['argv'][0]).parent.parent.name
    return raw.replace(('/' + base + '/').encode('utf-8'), b'/<replay>/')


def runtime(folder, label, record, original, original_streams, host):
    bounds = [('address_space_bytes', 33554432), ('linux_address_space_bytes', 33554432),
              ('descriptor_limit', 10240), ('linux_descriptor_limit', 10240)]
    if record.get('timeout_seconds') != 60:
        raise ValueError('A runtime deadline changed')
    for key, value in bounds:
        if record.get(key) is not None and record[key] != value:
            raise ValueError('A runtime resource bound changed')
    found = {p.name for p in folder.glob('*-stdout.bin')} | {p.name for p in folder.glob('*-stderr.bin')}
    if found != set(original_streams):
        raise ValueError('The complete runtime stream inventory differs')
    for case, old in zip(record['records'], original['records'], strict=True):
        deadline = case.get('timeout_seconds', record['timeout_seconds'])
        if not 0 <= case['seconds'] < deadline == 60:
            raise ValueError('A runtime deadline changed')
        if case.get('error') is not None or case.get('status', 'pass') not in ('pass', 'skip'):
            raise ValueError('A complete runtime result differs')
        for key, value in bounds:
            if case.get(key) is not None and case[key] != value:
                raise ValueError('A runtime resource bound changed')
        raw_key = 'raw_stdout_base64' if 'raw_stdout_base64' in case else 'stdout_base64' if 'stdout_base64' in case else None
        if raw_key:
            raw = base64.b64decode(case[raw_key], validate=True)
            old_raw = base64.b64decode(old[raw_key], validate=True)
            if identity(raw) != case['stdout']:
                raise ValueError('The complete runtime stdout identity differs')
            normalized = selection_output(raw, record) if label == 'input-selection-contracts' else raw
            old_normalized = selection_output(old_raw, original) if label == 'input-selection-contracts' else old_raw
            if normalized != old_normalized:
                raise ValueError('The complete original runtime stdout differs')
            prefix = '-'.join(str(case[k]) for k in ('role', 'namespace', 'mode', 'count')) if label == 'file-request-group-contracts' else '-'.join([case['role'], case['namespace'], case['mode'], str(case['stage_count'])]) if label in ('stream-group-contracts', 'path-group-contracts', 'selection-path-group-contracts') else '-'.join(case[k] for k in ('role', 'namespace', 'family', 'label')) if label == 'input-selection-contracts' else case['case']
            if (folder / (prefix + '-stdout.bin')).read_bytes() != raw:
                raise ValueError('A complete runtime command stream differs')
            stderr = (folder / (prefix + '-stderr.bin')).read_bytes()
            if stderr != b'' or identity(stderr) != case['stderr']:
                raise ValueError('A complete runtime command stream differs')
        else:
            if case.get('stdout') != old.get('stdout') or case.get('stderr') != old.get('stderr'):
                raise ValueError('The complete original runtime output differs')
            if 'expected_stdout' in case and case['stdout'] != case['expected_stdout']:
                raise ValueError('A complete runtime stdout oracle differs')
            for stream in ('stdout', 'stderr'):
                if stream + '_identity' in case and identity(case[stream].encode('utf-8')) != case[stream + '_identity']:
                    raise ValueError('The complete runtime output identity differs')
            if case.get('stderr') != '':
                raise ValueError('A complete runtime stderr differs')
        if label == 'stream-group-contracts':
            if case['expected_final_line'] not in ('accept', 'reject') or raw.splitlines()[-1] != case['expected_final_line'].encode('ascii'):
                raise ValueError('A group final result differs')
            size = 131083 if case['mode'] in ('chunked', 'chunked-limit') else 0 if case['mode'] == 'empty' else 12
            if case['aggregate_payload_bytes'] != case['stage_count'] * size or len(case['per_file_stream_limits_bytes']) != case['stage_count']:
                raise ValueError('A group payload or request bound differs')
            if case['public_output_before'] != case['public_output_after']:
                raise ValueError('Group capture altered public output')
            mutation = case.get('mutation')
            if case['mode'] in ('mutate', 'previous-digest'):
                if not mutation or mutation['kind'] != 'same-size-restored-mtime' or mutation['original']['size'] != mutation['changed']['size'] or mutation['original'] == mutation['changed'] or mutation['mtime_ns_before'] != mutation['mtime_ns_after']:
                    raise ValueError('A restored-time group mutation differs')
            if case['mode'] == 'root-retarget' and (not mutation or mutation['kind'] != 'original-root-retarget' or mutation['before'] == mutation['after']):
                raise ValueError('Original group root retargeting is absent')
        if label == 'path-group-contracts':
            if case['expected_final_line'] not in ('accept', 'reject') or raw.splitlines()[-1] != case['expected_final_line'].encode('ascii'):
                raise ValueError('A path group final result differs')
            if case['public_output_before'] != case['public_output_after']:
                raise ValueError('Path observation altered public output')
            if case['source_payload_bytes'] != case['stage_count'] * 12 or case['payload_streaming'] != (case['mode'] == 'previous-digest'):
                raise ValueError('A path group fixture or stream scope differs')
            if case['previous_digest_limit_bytes'] != (64 if case['mode'] == 'previous-digest' else None):
                raise ValueError('A path group digest limit differs')
            mutation = case.get('mutation')
            if case['mode'] == 'mutate' and (not mutation or mutation['kind'] != 'earlier-file-size-change' or mutation['original']['size'] == mutation['changed']['size']):
                raise ValueError('An earlier path mutation is absent')
            if case['mode'] == 'previous-digest' and (not mutation or mutation['kind'] != 'same-size-restored-mtime' or mutation['original']['size'] != mutation['changed']['size'] or mutation['original'] == mutation['changed'] or mutation['mtime_ns_before'] != mutation['mtime_ns_after']):
                raise ValueError('A restored-time path digest mutation differs')
            if case['mode'] == 'root-retarget' and (not mutation or mutation['kind'] != 'original-root-retarget' or mutation['before'] == mutation['after']):
                raise ValueError('Original path group root retargeting is absent')

        if label == 'file-request-group-contracts':
            for key, bound in [('linux_address_space_bytes', 33554432), ('linux_descriptor_limit', 10240)]:
                if case.get(key) != (bound if host == 'linux' else None):
                    raise ValueError('An original-request group resource bound is missing or differs')
            if case['expected_final_line'] not in ('accept', 'reject') or raw.splitlines()[-1] != case['expected_final_line'].encode('ascii'):
                raise ValueError('An original-request group final result differs')
            if case['timed_out'] or case['exit_code'] != 0 or case['public_output_before'] != case['public_output_after']:
                raise ValueError('An original-request group outcome or publication boundary differs')
            if raw.splitlines() != [marker['text'].encode('ascii') for marker in case['markers']]:
                raise ValueError('The complete original-request group markers differ')
            mutation = case.get('mutation')
            mode = case['mode']
            if mode in ('payload-restore', 'earlier-payload'):
                if not mutation or mutation['kind'] != mode or mutation['original']['size'] != mutation['changed']['size'] or mutation['original'] == mutation['changed'] or mutation['mtime_ns_before'] != mutation['mtime_ns_after']:
                    raise ValueError('An original-request group restored-time payload mutation differs')
                if 'group=1 frame=0' not in case['markers'][-2]['text']:
                    raise ValueError('The caller payload-validation frame is absent')
            if mode == 'mutate-current' and (not mutation or mutation['kind'] != mode or mutation['original']['size'] == mutation['changed']['size']):
                raise ValueError('A current original-request group file mutation is absent')
            if mode in ('parent-identity', 'parent-metadata') and (not mutation or mutation['kind'] != mode or mutation['mtime_ns_before'] == mutation['mtime_ns_after']):
                raise ValueError('An original-request group parent metadata mutation is absent')
            if mode in ('parent-retarget', 'root-retarget'):
                kind = 'original-parent-alias-retarget' if mode == 'parent-retarget' else 'original-root-retarget'
                if not mutation or mutation['kind'] != kind or mutation['before'] == mutation['after']:
                    raise ValueError('An original-request group alias retarget is absent')

        if label == 'selection-path-group-contracts':
            if base64.b64decode(case['raw_stderr_base64'], validate=True) != stderr or stderr != base64.b64decode(old['raw_stderr_base64'], validate=True):
                raise ValueError('The complete source-selection group embedded stderr differs')
            for key, bound in [('linux_address_space_bytes', 33554432), ('linux_descriptor_limit', 10240)]:
                if case.get(key) != (bound if host == 'linux' else None):
                    raise ValueError('A source-selection group resource bound is missing or differs')
            if case['expected_final_line'] not in ('accept', 'reject') or raw.splitlines()[-1] != case['expected_final_line'].encode('ascii'):
                raise ValueError('A source-selection group final result differs')
            if case['timed_out'] or case['exit_code'] != 0 or case['public_output_before'] != case['public_output_after']:
                raise ValueError('A source-selection group outcome or publication boundary differs')
            if raw.splitlines() != [entry['text'].encode('ascii') for entry in case['markers']]:
                raise ValueError('The complete source-selection group markers differ')
            if case['source_payload_bytes'] != case['stage_count'] * 12 or case['payload_streaming'] != (case['mode'] == 'previous-digest'):
                raise ValueError('A source-selection group fixture or stream scope differs')
            if case['previous_digest_limit_bytes'] != (64 if case['mode'] == 'previous-digest' else None):
                raise ValueError('A source-selection group digest limit differs')
            mode, mutation = case['mode'], case.get('mutation')
            if mode == 'mutate' and (not mutation or mutation['kind'] != 'earlier-file-size-change' or mutation['original']['size'] == mutation['changed']['size']):
                raise ValueError('An earlier selected-path mutation is absent')
            if mode == 'previous-digest' and (not mutation or mutation['kind'] != 'same-size-restored-mtime' or mutation['original']['size'] != mutation['changed']['size'] or mutation['original'] == mutation['changed'] or mutation['mtime_ns_before'] != mutation['mtime_ns_after']):
                raise ValueError('A restored-time selected payload mutation differs')
            if mode == 'root-retarget' and (not mutation or mutation['kind'] != 'original-root-retarget' or mutation['before'] == mutation['after']):
                raise ValueError('Original selection root retargeting is absent')
            if mode in ('source-root-metadata', 'source-root-sibling', 'ordinary-root-metadata'):
                kind = 'uncaptured-root-sibling' if mode == 'source-root-sibling' else 'root-metadata'
                expected = 'reject' if mode == 'ordinary-root-metadata' else 'accept'
                if not mutation or mutation['kind'] != kind or mutation['mtime_ns_before'] == mutation['mtime_ns_after'] or case['expected_final_line'] != expected:
                    raise ValueError('The source-selection root metadata control differs')



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--replay', type=Path, required=True)
    args = parser.parse_args()
    artifact = Path(__file__).resolve().parent
    root = artifact.parents[2]
    folder = args.replay.resolve()
    if folder.parent != root / 'build' or not folder.is_dir():
        raise ValueError('Use a completed direct child of the repository build directory')
    proof_path = artifact.parent / 'evidence/native-source-selection-group-20261011.json'
    proof_bytes = proof_path.read_bytes()
    proof = json.loads(proof_bytes)
    if identity(Path(__file__).read_bytes()) != proof['replay_verifier']:
        raise ValueError('The independent verifier differs')
    archive_bytes = (artifact.parent / 'evidence/native-source-selection-group-originals-20261011.json.gz').read_bytes()
    raw = gzip.decompress(archive_bytes)
    if identity(archive_bytes) != proof['original_receipts_archive'] or identity(raw) != proof['original_receipts_uncompressed']:
        raise ValueError('The complete original archive differs')
    originals = json.loads(raw)
    closed = json.loads((folder / 'closed.json').read_bytes())
    accepted_proofs = [identity(proof_bytes)]
    if 'accepted_replay_proof' in proof:
        replay_archive = (artifact.parent / 'evidence/native-source-selection-group-replays-20261011.json.gz').read_bytes()
        replay_raw = gzip.decompress(replay_archive)
        if identity(replay_archive) != proof['replay_receipts_archive'] or identity(replay_raw) != proof['replay_receipts_uncompressed']:
            raise ValueError('The complete replay archive differs')
        accepted = json.loads(replay_raw)['accepted_replay_proof_text'].encode('utf-8')
        if identity(accepted) != proof['accepted_replay_proof']:
            raise ValueError('The original accepted replay proof differs')
        accepted_proofs.append(identity(accepted))
    if closed['status'] != 'pass' or closed['proof'] not in accepted_proofs or closed['runner'] != proof['replay_runner']:
        raise ValueError('The completed replay differs from this acceptance')
    for name, facts in closed['controlled_file_identities'].items():
        if Path(name).resolve() == proof_path.resolve() and facts == proof.get('accepted_replay_proof'):
            continue
        if identity(Path(name).read_bytes()) != facts:
            raise ValueError('A complete controlled file differs: ' + name)
    host = closed['host']
    suffix = 'exe' if host == 'windows' else 'elf'
    expected = proof['original_hosts'][host]
    if set(closed['child_receipts']) != set(originals['hosts'][host]) or len(closed['commands']) != 20:
        raise ValueError('The complete child inventory differs')
    commands(folder, closed, empty=False)
    objects, programs, api_calls, strict = {}, {}, 0, 0
    for label, row in originals['hosts'][host].items():
        child_folder = folder / row['directory']
        data = (child_folder / 'closed.json').read_bytes()
        record = json.loads(data)
        original = json.loads(row['receipt_text'])
        if record['status'] != 'pass' or identity(data) != closed['child_receipts'][label]:
            raise ValueError('A complete child receipt differs: ' + label)
        if case_plan(record) != case_plan(original):
            raise ValueError('The complete original case plan differs: ' + label)
        if 'calls' in record:
            runtime(child_folder, label, record, original, row['command_streams'], host)
            if record['calls'] != original['calls'] or record['calls'] != len(record['records']):
                raise ValueError('A complete API call is missing')
            api_calls += record['calls']
            for case in record['records']:
                if case.get('error') is not None or ('exit_code' in case and case['exit_code'] != case.get('expected_exit', case.get('expected_exit_code', 0))):
                    raise ValueError('A complete runtime result differs')
                if 'seconds' in case and 'timeout_seconds' in case and not 0 <= case['seconds'] < case['timeout_seconds'] <= 60:
                    raise ValueError('A runtime deadline changed')
                for key, value in [('address_space_bytes', 33554432), ('linux_address_space_bytes', 33554432),
                                   ('descriptor_limit', 10240), ('linux_descriptor_limit', 10240)]:
                    if case.get(key) is not None and case[key] != value:
                        raise ValueError('A runtime resource bound changed')
                if label == 'stage-capacity-contracts' and (case['distinct_requests'] != 4096 or len(set(case['requests'])) != 4096 or
                        len(case['python_final_rejects']) != 4096 or sum(case['python_final_rejects']) != (1 if case['mutation'] == 'last-leaf' else 0)):
                    raise ValueError('A complete capacity or Python decision differs')
        for key in ['positive_calls', 'negative_calls', 'api_calls', 'selected_methods', 'passed_methods', 'declared_skips',
                    'collision_comparisons', 'stale_validation_rejections', 'publication_rejection_checks',
                    'quota_exhaustion_controls', 'rejection_clear_poison_checks', 'repeated_binding_checks']:
            if key in original and record.get(key) != original[key]:
                raise ValueError('The accepted scope differs: ' + label + ':' + key)
        if label == 'build':
            if record['objects'] != original['objects'] or record['checked_images'] != original['checked_images'] or record['sources'] != proof['core_sources']:
                raise ValueError('The complete shared build differs')
            for name, facts in record['objects'].items():
                if identity((child_folder / 'objects' / name).read_bytes()) != facts:
                    raise ValueError('A complete checked object differs: ' + name)
            objects.update(record['objects'])
            for role in ('native', 'checked'):
                for name, facts in record[role + '_images'].items():
                    if identity((child_folder / (role + '-' + name + '.' + suffix)).read_bytes()) != facts:
                        raise ValueError('A complete program differs: ' + role + '-' + name)
            programs.update(record['checked_images'])
            strict += commands(child_folder, record)
        elif label in ('stage-capacity-build', 'parent-binding-build', 'stream-group-build', 'path-group-build', 'file-request-group-build', 'selection-path-group-build'):
            kind = label[:-6]
            if record['object'] != original['object'] or record['checked_image'] != original['checked_image']:
                raise ValueError('A complete supplementary checked build differs')
            if identity((child_folder / (kind + '.o')).read_bytes()) != record['object']:
                raise ValueError('A complete supplementary object differs')
            objects[kind + '.o'] = record['object']
            for role in ('native', 'checked'):
                if identity((child_folder / (role + '-' + kind + '.' + suffix)).read_bytes()) != record[role + '_image']:
                    raise ValueError('A complete supplementary program differs')
            programs[kind] = record['checked_image']
            strict += commands(child_folder, record)
        elif label == 'alias':
            if record['alias_object'] != original['alias_object'] or record['programs']['checked'] != original['programs']['checked']:
                raise ValueError('The complete checked alias build differs')
            if identity((child_folder / 'alias.o').read_bytes()) != record['alias_object']:
                raise ValueError('The complete alias object differs')
            objects['alias.o'] = record['alias_object']
            for role, facts in record['programs'].items():
                if identity((child_folder / (role + '-alias.' + suffix)).read_bytes()) != facts:
                    raise ValueError('The complete alias program differs')
            programs['alias'] = record['programs']['checked']
            strict += commands(child_folder, record)
        elif label == 'aggregate':
            commands(child_folder, record, empty=False)
    if objects != expected['whole_checked_objects'] or programs != expected['whole_checked_programs']:
        raise ValueError('The complete checked product inventory differs')
    if api_calls != closed['complete_api_calls'] or api_calls != expected['complete_api_calls'] or strict != expected['strict_check_executions']:
        raise ValueError('The complete acceptance count differs')
    print(json.dumps({'schema': 'cupid.source-selection-group-replay-verification.v1', 'status': 'pass', 'host': host,
        'complete_api_calls': api_calls, 'source_selection_group_api_calls': expected['source_selection_group_api_calls'], 'whole_checked_objects': len(objects),
        'whole_checked_programs': len(programs), 'strict_check_executions': strict}))


if __name__ == '__main__':
    main()
