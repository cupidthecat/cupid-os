from pathlib import Path
import json

from consumer_audit_common import read, identity, git_bytes


def make_guard(root, base):
    root, base = Path(root), Path(base)
    controls_path = base / 'replay-controls.json'
    controls_bytes = read(controls_path)
    controls = json.loads(controls_bytes)
    assert controls['schema'] == 'cupid.native-source-optional-replay-controls.v1'
    assert controls['status'] == 'pass'
    assert len(controls['root_files']) == 20 and len(controls['source_inputs']) == 99
    captured = {root / name: read(root / name) for name in controls['root_files']}
    for name, facts in controls['root_files'].items():
        assert identity(captured[root / name]) == facts
        assert captured[root / name] == git_bytes('HEAD', name)
    for name, facts in controls['source_inputs'].items():
        captured[root / name] = read(root / name)
        assert identity(captured[root / name]) == facts
        assert captured[root / name] == git_bytes(controls['source_revision'], name)
    for name, facts in controls['support_inputs'].items():
        captured[root / name] = read(root / name)
        assert identity(captured[root / name]) == facts
    for name, facts in controls['sources'].items():
        captured[base / 'source3' / name] = read(base / 'source3' / name)
        assert identity(captured[base / 'source3' / name]) == facts

    def guard():
        assert read(controls_path) == controls_bytes
        for path, data in captured.items():
            assert read(path) == data, str(path)

    guard()
    return guard
