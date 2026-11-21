## @file
#  @brief Validate the i386 ELF relocatable files consumed by CupidLD.
import struct
from pathlib import Path


class KernelCompileError(RuntimeError):
    """A checked CupidC production operation could not publish its output."""


def _read_bytes(path: Path) -> bytes:
    try:
        return path.read_bytes()
    except OSError as error:
        raise KernelCompileError(
            f"cannot read emitted object {path}: {error}"
        ) from error


def validate_i386_relocatable_bytes(
    image: bytes,
    *,
    require_executable: bool = False,
) -> None:
    """Validate the ELF32 structure and relocation contract CupidLD consumes."""
    if len(image) < 52:
        raise KernelCompileError("ELF header is outside the emitted object")
    if image[0:7] != b"\x7fELF\x01\x01\x01":
        raise KernelCompileError(
            "emitted object is not little-endian ELF32 version 1"
        )
    (
        object_type,
        machine,
        version,
        _entry,
        program_offset,
        section_offset,
        _flags,
        header_size,
        program_entry_size,
        program_count,
        section_entry_size,
        section_count,
        section_name_index,
    ) = struct.unpack_from("<HHIIIIIHHHHHH", image, 16)
    if object_type != 1 or machine != 3 or version != 1:
        raise KernelCompileError(
            "emitted object is not an i386 ELF32 relocatable object"
        )
    if header_size != 52 or section_entry_size != 40:
        raise KernelCompileError(
            "emitted object has an invalid ELF or section header size"
        )
    if program_count != 0 or program_offset != 0 or program_entry_size != 0:
        raise KernelCompileError(
            "emitted relocatable object unexpectedly has program headers"
        )
    if section_count == 0 or section_name_index >= section_count:
        raise KernelCompileError(
            "emitted object has an invalid section table"
        )
    section_bytes = section_count * section_entry_size
    if (
        section_offset > len(image)
        or section_bytes > len(image) - section_offset
    ):
        raise KernelCompileError(
            "emitted object has a truncated section header table"
        )

    sections = []
    executable_bytes = 0
    for index in range(section_count):
        section = struct.unpack_from(
            "<IIIIIIIIII",
            image,
            section_offset + index * section_entry_size,
        )
        (
            _name,
            section_type,
            section_flags,
            _section_address,
            payload_offset,
            payload_size,
            _link,
            _info,
            alignment,
            _entry_size,
        ) = section
        if (
            section_type != 8
            and (
                payload_offset > len(image)
                or payload_size > len(image) - payload_offset
            )
        ):
            raise KernelCompileError(
                f"emitted object section {index} payload is outside the file"
            )
        if alignment != 0 and alignment & (alignment - 1):
            raise KernelCompileError(
                f"emitted object section {index} alignment is not a power of two"
            )
        if (
            section_type != 8
            and alignment > 1
            and payload_offset % alignment != 0
        ):
            raise KernelCompileError(
                f"emitted object section {index} payload is misaligned"
            )
        if section_type == 1 and section_flags & 0x4:
            executable_bytes += payload_size
        sections.append(section)

    if require_executable and executable_bytes == 0:
        raise KernelCompileError(
            "emitted object has no executable section bytes"
        )

    name_section = sections[section_name_index]
    if name_section[1] != 3:
        raise KernelCompileError(
            "emitted object section name table is not a string table"
        )
    name_data = image[name_section[4] : name_section[4] + name_section[5]]
    section_names = []
    for index, section in enumerate(sections):
        name_offset = section[0]
        if name_offset >= len(name_data):
            raise KernelCompileError(
                f"emitted object section {index} name is outside the string table"
            )
        terminator = name_data.find(b"\0", name_offset)
        if terminator < 0:
            raise KernelCompileError(
                f"emitted object section {index} name is not terminated"
            )
        try:
            section_names.append(
                name_data[name_offset:terminator].decode("ascii")
            )
        except UnicodeDecodeError as error:
            raise KernelCompileError(
                f"emitted object section {index} name is not ASCII"
            ) from error

    required_sections = {".symtab", ".strtab", ".shstrtab"}
    missing_sections = sorted(required_sections - set(section_names))
    if missing_sections:
        raise KernelCompileError(
            "emitted object is missing required section "
            + ", ".join(missing_sections)
        )

    symbol_counts = {}
    for section_index, section in enumerate(sections):
        if section[1] != 2:
            continue
        payload_offset = section[4]
        payload_size = section[5]
        string_index = section[6]
        first_nonlocal = section[7]
        entry_size = section[9]
        if entry_size != 16 or payload_size % entry_size != 0:
            raise KernelCompileError(
                f"emitted object symbol table {section_index} has invalid entries"
            )
        if string_index >= len(sections) or sections[string_index][1] != 3:
            raise KernelCompileError(
                f"emitted object symbol table {section_index} has no string table"
            )
        symbol_count = payload_size // entry_size
        symbol_counts[section_index] = symbol_count
        if symbol_count == 0 or first_nonlocal > symbol_count:
            raise KernelCompileError(
                f"emitted object symbol table {section_index} has an invalid boundary"
            )
        string_section = sections[string_index]
        string_data = image[
            string_section[4] : string_section[4] + string_section[5]
        ]
        for symbol_index in range(symbol_count):
            (
                symbol_name,
                symbol_value,
                symbol_size,
                _symbol_info,
                _symbol_other,
                symbol_section,
            ) = struct.unpack_from(
                "<IIIBBH",
                image,
                payload_offset + symbol_index * entry_size,
            )
            if (
                symbol_name >= len(string_data)
                or string_data.find(b"\0", symbol_name) < 0
            ):
                raise KernelCompileError(
                    f"emitted object symbol {symbol_index} has an invalid name"
                )
            if symbol_section < 0xFF00:
                if symbol_section >= len(sections):
                    raise KernelCompileError(
                        f"emitted object symbol {symbol_index} has an invalid section"
                    )
                if symbol_section != 0:
                    target_size = sections[symbol_section][5]
                    if (
                        symbol_value > target_size
                        or symbol_size > target_size - symbol_value
                    ):
                        raise KernelCompileError(
                            f"emitted object symbol {symbol_index} exceeds its section"
                        )
            elif symbol_section not in (0xFFF1, 0xFFF2):
                raise KernelCompileError(
                    f"emitted object symbol {symbol_index} has an unsupported section"
                )
    if not symbol_counts:
        raise KernelCompileError("emitted object has no symbol table")

    for section_index, section in enumerate(sections):
        if section[1] == 4:
            raise KernelCompileError(
                f"emitted object relocation section {section_index} uses RELA"
            )
        if section[1] != 9:
            continue
        payload_offset = section[4]
        payload_size = section[5]
        symbol_table_index = section[6]
        target_index = section[7]
        entry_size = section[9]
        if entry_size != 8 or payload_size % entry_size != 0:
            raise KernelCompileError(
                f"emitted object relocation section {section_index} has invalid entries"
            )
        if symbol_table_index not in symbol_counts:
            raise KernelCompileError(
                f"emitted object relocation section {section_index} has no symbol table"
            )
        if target_index == 0 or target_index >= len(sections):
            raise KernelCompileError(
                f"emitted object relocation section {section_index} has no target"
            )
        target = sections[target_index]
        if target[1] == 8:
            raise KernelCompileError(
                f"emitted object relocation section {section_index} targets NOBITS"
            )
        relocation_count = payload_size // entry_size
        for relocation_index in range(relocation_count):
            relocation_offset, relocation_info = struct.unpack_from(
                "<II",
                image,
                payload_offset + relocation_index * entry_size,
            )
            if relocation_offset + 4 > target[5]:
                raise KernelCompileError(
                    f"emitted object relocation {relocation_index} "
                    "is outside its target"
                )
            symbol_index = relocation_info >> 8
            if symbol_index >= symbol_counts[symbol_table_index]:
                raise KernelCompileError(
                    f"emitted object relocation {relocation_index} "
                    "has an invalid symbol"
                )
            relocation_type = relocation_info & 0xFF
            if relocation_type not in (1, 2):
                raise KernelCompileError(
                    f"emitted object relocation {relocation_index} uses "
                    f"unsupported i386 type {relocation_type}"
                )
            addend = struct.unpack_from(
                "<i",
                image,
                target[4] + relocation_offset,
            )[0]
            # An absolute relocation may select a static subobject through
            # its signed addend. Direct PC-relative calls still use -4.
            if relocation_type == 2 and addend != -4:
                raise KernelCompileError(
                    f"PC-relative relocation addend is {addend}, expected -4"
                )


def validate_i386_relocatable(path: Path) -> None:
    validate_i386_relocatable_bytes(_read_bytes(path))
