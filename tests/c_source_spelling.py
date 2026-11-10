"""Read C source fixtures without depending on whitespace or comments."""

from functools import lru_cache
import re


_C_SPELLING_TOKEN = re.compile(
    r'//[^\n]*|/\*.*?\*/|(?:u8|[LuU])?"(?:\\.|[^"\\\n])*"'
    r"|(?:[LuU])?'(?:\\.|[^'\\\n])*'"
    r'|[A-Za-z_][A-Za-z_0-9]*'
    r'|(?:\d|\.\d)(?:[A-Za-z_0-9.]|(?<=[eEpP])[+-])*'
    r'|>>=|<<=|\.\.\.|->|\+\+|--|&&|\|\||<=|>=|==|!=|<<|>>'
    r'|[+*/%&|^=-]=|##|[^\s]',
    re.S,
)


@lru_cache(maxsize=16)
def _c_spelling_tokens(source):
    tokens = []
    for match in _C_SPELLING_TOKEN.finditer(source):
        value = match.group()
        if value == '/' and source.startswith('/*', match.start()):
            raise ValueError('incomplete C comment in source fixture')
        if value.startswith(('//', '/*')):
            continue
        if value in ('"', "'"):
            raise ValueError('incomplete C literal in source fixture')
        tokens.append((value, match.start(), match.end()))
    return tuple(tokens)


def _c_spelling_ranges(source, fragment, start=0):
    tokens = _c_spelling_tokens(source)
    wanted = tuple(value for value, _start, _end in _c_spelling_tokens(fragment))
    if not wanted:
        raise ValueError('empty C source fixture')
    values = tuple(value for value, _start, _end in tokens)
    count = len(wanted)
    return [(tokens[index][1], tokens[index + count - 1][2])
            for index in range(len(tokens) - count + 1)
            if tokens[index][1] >= start and values[index] == wanted[0]
            and values[index:index + count] == wanted]


def c_block(source, fragment):
    matches = _c_spelling_ranges(source, fragment)
    if len(matches) != 1:
        raise ValueError(f"expected one block fixture, found {len(matches)}: {fragment}")
    start, end = matches[0]
    depth = 0
    opening = None
    for value, left, right in _c_spelling_tokens(source):
        if left < end:
            continue
        if value == "{":
            if opening is None:
                opening = left
            depth += 1
        elif value == "}" and opening is not None:
            depth -= 1
            if depth == 0:
                return start, right
        elif opening is None:
            raise ValueError("fixture is not followed by a block")
    raise ValueError("unterminated C fixture block")


def c_replace(source, fragment, replacement):
    """Replace one token sequence, rejecting missing or ambiguous fixtures."""
    matches = _c_spelling_ranges(source, fragment)
    if len(matches) != 1:
        raise ValueError(f"expected one mutation fixture, found {len(matches)}: {fragment}")
    start, end = matches[0]
    return source[:start] + replacement + source[end:]


def c_literal_text(source):
    """Join adjacent ordinary string literals while retaining escape spelling."""
    groups = []
    previous_literal = False
    for value, _left, _right in _c_spelling_tokens(source):
        literal = value.startswith('"')
        if literal:
            if previous_literal:
                groups[-1] += value[1:-1]
            else:
                groups.append(value[1:-1])
        previous_literal = literal
    return '\0'.join(groups)
