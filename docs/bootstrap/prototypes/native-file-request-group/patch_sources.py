from pathlib import Path
import hashlib
import re

def identity(data):
    return {'size':len(data),'sha256':hashlib.sha256(data).hexdigest()}

def apply_sources(patch_bytes, root, destination, expected, source_map):
    """Apply the reviewed source map, checking every context, hunk and output."""
    assert len(source_map) == 24
    allowed = {'b/' + row['path']: row['path'] if row['before'] else None for row in source_map.values()}
    lines = patch_bytes.decode('utf-8').splitlines(keepends=True)
    position = 0
    seen = set()
    while position < len(lines):
        old_header, new_header = lines[position:position + 2]
        if not old_header.startswith('--- ') or not new_header.startswith('+++ '):
            raise ValueError('Unexpected source patch header')
        new_name = new_header[4:].strip()
        if new_name not in allowed or new_name in seen:
            raise ValueError('Unexpected or repeated source patch file')
        seen.add(new_name)
        source_name = allowed[new_name]
        if old_header[4:].strip() != ('a/' + source_name if source_name else '/dev/null'):
            raise ValueError('Unexpected source patch base')
        before = (root / source_name).read_bytes().decode('utf-8').splitlines(keepends=True) if source_name else []
        after = []
        cursor = 0
        position += 2
        while position < len(lines) and not lines[position].startswith('--- '):
            match = re.fullmatch(r'@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@[^\n]*\n', lines[position])
            if not match:
                raise ValueError('Unexpected source patch hunk')
            old_start, old_count = int(match[1]), int(match[2] or 1)
            new_start, new_count = int(match[3]), int(match[4] or 1)
            start = old_start - 1 if old_count else old_start
            if not cursor <= start <= len(before):
                raise ValueError('Source patch hunks overlap or exceed the base')
            after.extend(before[cursor:start])
            if len(after) != (new_start - 1 if new_count else new_start):
                raise ValueError('Source patch new position differs')
            cursor = start
            position += 1
            removed = added = 0
            while removed < old_count or added < new_count:
                line = lines[position]
                position += 1
                if line == '\n':
                    line = ' \n'
                marker, value = line[0], line[1:]
                if marker in (' ', '-'):
                    if cursor >= len(before) or before[cursor] != value:
                        raise ValueError('Source patch base context differs')
                    cursor += 1
                    removed += 1
                if marker in (' ', '+'):
                    after.append(value)
                    added += 1
                if marker not in (' ', '-', '+') or removed > old_count or added > new_count:
                    raise ValueError('Source patch line counts differ')
            if removed != old_count or added != new_count:
                raise ValueError('Source patch hunk is incomplete')
        after.extend(before[cursor:])
        data = ''.join(after).encode('utf-8')
        name = Path(new_name).name
        if identity(data) != expected[name]:
            raise ValueError('Held source identity differs: ' + name)
        (destination / name).write_bytes(data)
    if seen != set(allowed):
        raise ValueError('Held source patch is incomplete')
