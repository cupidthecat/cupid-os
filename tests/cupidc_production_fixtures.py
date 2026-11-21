"""Seed paths and binary fixtures for production tests."""

import struct
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SEED_MANIFEST = (
    REPO_ROOT
    / "bootstrap"
    / "seeds"
    / "i386-linux"
    / "manifest.json"
)


def _run_captured_test_tool(
    _manifest,
    _working_directory,
    tool_name,
    arguments,
    *,
    timeout,
    frozen_seed,
    runner,
):
    if runner is None:
        raise AssertionError("test fixture requires an injected runner")
    return runner.run(frozen_seed.tools[tool_name], arguments, timeout)


def _align(value, alignment):
    return (value + alignment - 1) & ~(alignment - 1)


def _valid_elf32_object():
    text = struct.pack("<Ii", 0, -4)
    relocations = struct.pack("<IIII", 0, (2 << 8) | 1, 4, (2 << 8) | 2)
    strings = b"\0entry\0external\0"
    section_strings = b"\0.text\0.rel.text\0.symtab\0.strtab\0.shstrtab\0"

    text_offset = 52
    relocation_offset = text_offset + len(text)
    symbol_offset = relocation_offset + len(relocations)
    symbols = bytearray(3 * 16)
    struct.pack_into("<IIIBBH", symbols, 16, 1, 0, len(text), 0x12, 0, 1)
    struct.pack_into("<IIIBBH", symbols, 32, 7, 0, 0, 0x10, 0, 0)
    string_offset = symbol_offset + len(symbols)
    section_string_offset = string_offset + len(strings)
    section_offset = _align(section_string_offset + len(section_strings), 4)
    image = bytearray(section_offset + 6 * 40)
    image[0:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into(
        "<HHIIIIIHHHHHH",
        image,
        16,
        1,
        3,
        1,
        0,
        0,
        section_offset,
        0,
        52,
        0,
        0,
        40,
        6,
        5,
    )
    image[text_offset:relocation_offset] = text
    image[relocation_offset:symbol_offset] = relocations
    image[symbol_offset:string_offset] = symbols
    image[string_offset:section_string_offset] = strings
    image[
        section_string_offset : section_string_offset + len(section_strings)
    ] = section_strings
    sections = (
        (0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        (1, 1, 6, 0, text_offset, len(text), 0, 0, 4, 0),
        (
            7,
            9,
            0,
            0,
            relocation_offset,
            len(relocations),
            3,
            1,
            4,
            8,
        ),
        (17, 2, 0, 0, symbol_offset, len(symbols), 4, 1, 4, 16),
        (25, 3, 0, 0, string_offset, len(strings), 0, 0, 1, 0),
        (
            33,
            3,
            0,
            0,
            section_string_offset,
            len(section_strings),
            0,
            0,
            1,
            0,
        ),
    )
    for index, section in enumerate(sections):
        struct.pack_into(
            "<IIIIIIIIII",
            image,
            section_offset + index * 40,
            *section,
        )
    return bytes(image)


def _valid_user_executable(entry=0x01C00004):
    payload_offset = 0x100
    payload = b"\x90" * 16
    image = bytearray(payload_offset + len(payload))
    image[0:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into(
        "<HHIIIIIHHHHHH",
        image,
        16,
        2,
        3,
        1,
        entry,
        52,
        0,
        0,
        52,
        32,
        1,
        0,
        0,
        0,
    )
    struct.pack_into(
        "<IIIIIIII",
        image,
        52,
        1,
        payload_offset,
        0x01C00000,
        0x01C00000,
        len(payload),
        len(payload),
        5,
        1,
    )
    image[payload_offset:] = payload
    return bytes(image)


def _minimal_pe64_console_image():
    image = bytearray(0x200)
    image[0:2] = b"MZ"
    struct.pack_into("<I", image, 0x3C, 0x80)
    image[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<H", image, 0x84, 0x8664)
    struct.pack_into("<H", image, 0x94, 0xF0)
    struct.pack_into("<H", image, 0x98, 0x20B)
    struct.pack_into("<H", image, 0xDC, 3)
    return bytes(image)
