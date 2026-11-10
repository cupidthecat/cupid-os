"""Test-only handling of NASM's equivalent 16-bit size-prefix order."""

import bisect
import re


LEGACY_PREFIXES = {0x26, 0x2E, 0x36, 0x3E, 0x64, 0x65, 0x66, 0x67,
                   0xF0, 0xF2, 0xF3}
ROW = re.compile(r"^([0-9A-Fa-f]{8}):  ((?:[0-9A-Fa-f]{2} )*[0-9A-Fa-f]{2})  (.+)$")


def canonicalize_size_prefix_order(image, range_map, disassembly):
    """Only commute adjacent 66/67 in independently validated code16 prefixes.

    The caller first runs strict CupidDis with this source-derived range map.
    Every rendered byte is checked against the image. Data, immediates, opcode
    bytes, code32 ranges and all other prefix ordering remain byte-exact.
    """
    lines = range_map.splitlines()
    if not lines or lines[0] != "cupid.raw-map.v2":
        raise ValueError("expected a v2 source range map")
    sizes = [int(line.split()[1], 0) for line in lines if line.startswith("size ")]
    bases = [int(line.split()[1], 0) for line in lines if line.startswith("base ")]
    ranges = [(int(line.split()[1], 0), line.split()[2])
              for line in lines if line.startswith("range ")]
    if sizes != [len(image)] or len(bases) != 1 or not ranges or ranges[0][0] != 0:
        raise ValueError("range map does not describe the image")
    starts = [offset for offset, _ in ranges]
    if starts != sorted(set(starts)) or any(mode not in {"code16", "code32", "data"}
                                         for _, mode in ranges):
        raise ValueError("invalid source range ordering or mode")
    canonical = bytearray(image)
    instructions = 0
    previous_end = 0
    for line in disassembly.splitlines():
        row = ROW.fullmatch(line)
        if row is None:
            continue
        offset = int(row[1], 16) - bases[0]
        raw = bytes.fromhex(row[2])
        if offset < previous_end or offset < 0 or offset + len(raw) > len(image):
            raise ValueError("rendered instruction is outside the image or overlaps")
        if image[offset:offset + len(raw)] != raw:
            raise ValueError("rendered bytes do not match the image")
        previous_end = offset + len(raw)
        index = bisect.bisect_right(starts, offset) - 1
        end = starts[index + 1] if index + 1 < len(starts) else len(image)
        if previous_end > end:
            raise ValueError("rendered instruction crosses a source range")
        if ranges[index][1] == "data":
            continue
        instructions += 1
        if ranges[index][1] != "code16":
            continue
        prefix_end = 0
        while prefix_end < len(raw) and raw[prefix_end] in LEGACY_PREFIXES:
            prefix_end += 1
        prefixes = raw[:prefix_end]
        if prefixes.count(0x66) > 1 or prefixes.count(0x67) > 1:
            raise ValueError("duplicate size prefixes are outside this oracle rule")
        for position in range(prefix_end - 1):
            if raw[position:position + 2] == b"\x67\x66":
                canonical[offset + position:offset + position + 2] = b"\x66\x67"
    if not instructions:
        raise ValueError("no validated instruction rows")
    return bytes(canonical)
