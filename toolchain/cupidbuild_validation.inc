/**
 * @file cupidbuild_validation.inc
 * @brief Validate checked ELF objects, user executables, and seed programs.
 */

static int cupidbuild_validate_relocatable(const unsigned char *bytes, size_t size,
                                           int require_executable) {
    ctool_host_adapter_t adapter;
    ctool_job_config_t config;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_elf32_object_t object;
    ctool_u32 index;
    int relocatable = 0;
    int executable = 0;
    if (size > 4294967295u || ctool_host_adapter_init(&adapter, ".") != CTOOL_OK) {
        return 0;
    }
    config = ctool_host_job_config(&adapter, ctool_default_limits());
    if (ctool_job_open(&config, &job) != CTOOL_OK) {
        return 0;
    }
    source.path.text = ctool_string("/candidate.o");
    source.contents = ctool_bytes(bytes, (ctool_u32)size);
    if (ctool_elf32_read(job, &source, &object) == CTOOL_OK &&
        object.file_type == CTOOL_ELF32_ET_REL) {
        relocatable = 1;
        for (index = 0u; index < object.section_count; index++) {
            const ctool_elf32_section_t *section = &object.sections[index];
            if (section->type == CTOOL_ELF32_SHT_PROGBITS &&
                (section->flags & CTOOL_ELF32_SHF_EXECINSTR) != 0u && section->size != 0u) {
                executable = 1;
            }
        }
    }
    ctool_job_close(job);
    return relocatable != 0 && (require_executable == 0 || executable != 0);
}

static int cupidbuild_jpeg_symbol_name_matches(ctool_string_t actual, const char *identity,
                                               const char *suffix) {
    static const char prefix[] = "_binary_";
    size_t prefix_size = sizeof(prefix) - 1u;
    size_t identity_size = strlen(identity);
    size_t suffix_size = strlen(suffix);
    size_t index;
    if (identity_size > (size_t)-1 - prefix_size - suffix_size ||
        prefix_size + identity_size + suffix_size != (size_t)actual.size ||
        memcmp(actual.data, prefix, prefix_size) != 0 ||
        memcmp(actual.data + prefix_size + identity_size, suffix, suffix_size) != 0) {
        return 0;
    }
    for (index = 0u; index < identity_size; index++) {
        unsigned char character = (unsigned char)identity[index];
        char expected = ((character >= (unsigned char)'a' && character <= (unsigned char)'z') ||
                         (character >= (unsigned char)'A' && character <= (unsigned char)'Z') ||
                         (character >= (unsigned char)'0' && character <= (unsigned char)'9'))
                            ? (char)character
                            : '_';
        if (actual.data[prefix_size + index] != expected) {
            return 0;
        }
    }
    return 1;
}

static int cupidbuild_string_equals(ctool_string_t actual, const char *expected);

int cupidbuild_validate_compiler_object_bytes(const unsigned char *bytes, size_t size) {
    ctool_host_adapter_t adapter;
    ctool_job_config_t config;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_elf32_object_t object;
    ctool_u32 index;
    unsigned int required = 0u;
    int valid = 0;
    if (bytes == (const unsigned char *)0 || size > 4294967295u ||
        ctool_host_adapter_init(&adapter, ".") != CTOOL_OK) {
        return 0;
    }
    config = ctool_host_job_config(&adapter, ctool_default_limits());
    if (ctool_job_open(&config, &job) != CTOOL_OK) {
        return 0;
    }
    source.path.text = ctool_string("/compiler-candidate.o");
    source.contents = ctool_bytes(bytes, (ctool_u32)size);
    if (ctool_elf32_read(job, &source, &object) != CTOOL_OK ||
        object.file_type != CTOOL_ELF32_ET_REL || object.program_header_count != 0u ||
        object.symbol_count == 0u || bytes[42] != 0u || bytes[43] != 0u) {
        goto done;
    }
    for (index = 0u; index < object.section_count; index++) {
        const ctool_elf32_section_t *section = &object.sections[index];
        ctool_u32 character;
        if (section->type == 4u ||
            (section->type == 9u &&
             object.sections[section->info].type == CTOOL_ELF32_SHT_NOBITS) ||
            (section->type != CTOOL_ELF32_SHT_NOBITS && section->alignment > 1u &&
             section->file_offset % section->alignment != 0u)) {
            goto done;
        }
        for (character = 0u; character < section->name.size; character++) {
            if ((unsigned char)section->name.data[character] >= 128u) {
                goto done;
            }
        }
        if (cupidbuild_string_equals(section->name, ".symtab")) {
            if (section->type != 2u) {
                goto done;
            }
            required |= 1u;
        } else if (cupidbuild_string_equals(section->name, ".strtab")) {
            if (section->type != 3u) {
                goto done;
            }
            required |= 2u;
        } else if (cupidbuild_string_equals(section->name, ".shstrtab")) {
            if (section->type != 3u) {
                goto done;
            }
            required |= 4u;
        }
    }
    for (index = 0u; index < object.symbol_count; index++) {
        if (object.symbols[index].placement == CTOOL_ELF32_SYMBOL_RESERVED) {
            goto done;
        }
    }
    for (index = 0u; index < object.relocation_count; index++) {
        const ctool_elf32_relocation_t *relocation = &object.relocations[index];
        if ((relocation->type != CTOOL_ELF32_R_386_32 &&
             relocation->type != CTOOL_ELF32_R_386_PC32) ||
            relocation->addend_known == CTOOL_FALSE ||
            (relocation->type == CTOOL_ELF32_R_386_PC32 && relocation->addend != -4)) {
            goto done;
        }
    }
    valid = required == 7u;
done:
    ctool_job_close(job);
    return valid;
}

static ctool_u32 cupidbuild_user_elf_u16(const unsigned char *bytes) {
    return (ctool_u32)bytes[0] | ((ctool_u32)bytes[1] << 8u);
}

static ctool_u32 cupidbuild_user_elf_u32(const unsigned char *bytes) {
    return cupidbuild_user_elf_u16(bytes) | (cupidbuild_user_elf_u16(bytes + 2u) << 16u);
}

static int cupidbuild_user_elf_failure(char *reason, size_t capacity, const char *format,
                                       unsigned int index) {
    (void)snprintf(reason, capacity, format, index);
    return 0;
}

int cupidbuild_validate_user_executable_bytes(const unsigned char *bytes, size_t size, char *reason,
                                              size_t reason_capacity) {
    ctool_u32 entry;
    ctool_u32 program_offset;
    ctool_u32 program_count;
    ctool_u32 starts[16];
    ctool_u32 ends[16];
    unsigned int load_count = 0u;
    unsigned int index = 0u;
    int entry_is_executable = 0;
    if (reason == (char *)0 || reason_capacity == 0u) {
        return 0;
    }
    reason[0] = '\0';
    if (bytes == (const unsigned char *)0 || size < 52u) {
        return cupidbuild_user_elf_failure(reason, reason_capacity,
                                           "ELF header is outside the linked executable", index);
    }
    if (memcmp(bytes, "\177ELF\001\001\001", 7u) != 0) {
        return cupidbuild_user_elf_failure(reason, reason_capacity,
                                           "linked executable is not little-endian ELF32 version 1",
                                           index);
    }
    if (cupidbuild_user_elf_u16(bytes + 16u) != 2u || cupidbuild_user_elf_u16(bytes + 18u) != 3u ||
        cupidbuild_user_elf_u32(bytes + 20u) != 1u) {
        return cupidbuild_user_elf_failure(
            reason, reason_capacity, "linked executable is not an i386 ELF32 executable", index);
    }
    entry = cupidbuild_user_elf_u32(bytes + 24u);
    program_offset = cupidbuild_user_elf_u32(bytes + 28u);
    program_count = cupidbuild_user_elf_u16(bytes + 44u);
    if (cupidbuild_user_elf_u16(bytes + 40u) != 52u ||
        cupidbuild_user_elf_u16(bytes + 42u) != 32u || program_count == 0u) {
        return cupidbuild_user_elf_failure(reason, reason_capacity,
                                           "linked executable has an invalid program table", index);
    }
    if (program_count > 16u) {
        return cupidbuild_user_elf_failure(
            reason, reason_capacity, "linked executable has more than 16 program headers", index);
    }
    if (program_offset < 52u || program_offset > 0x7fffffffu) {
        return cupidbuild_user_elf_failure(reason, reason_capacity,
                                           "linked executable has an invalid program-header offset",
                                           index);
    }
    if ((size_t)program_offset > size || program_count * 32u > size - (size_t)program_offset) {
        return cupidbuild_user_elf_failure(
            reason, reason_capacity, "linked executable has a truncated program table", index);
    }
    for (index = 0u; index < program_count; index++) {
        const unsigned char *header = bytes + program_offset + index * 32u;
        ctool_u32 type = cupidbuild_user_elf_u32(header);
        ctool_u32 offset = cupidbuild_user_elf_u32(header + 4u);
        ctool_u32 address = cupidbuild_user_elf_u32(header + 8u);
        ctool_u32 file_size = cupidbuild_user_elf_u32(header + 16u);
        ctool_u32 memory_size = cupidbuild_user_elf_u32(header + 20u);
        ctool_u32 flags = cupidbuild_user_elf_u32(header + 24u);
        ctool_u32 alignment = cupidbuild_user_elf_u32(header + 28u);
        ctool_u32 file_end;
        ctool_u32 memory_end;
        unsigned int previous;
        if (type != 0u && type != 1u && type != 0x6474e551u) {
            return cupidbuild_user_elf_failure(reason, reason_capacity,
                                               "program header %u has an unsupported program type",
                                               index);
        }
        if ((flags & ~7u) != 0u) {
            return cupidbuild_user_elf_failure(
                reason, reason_capacity, "program header %u has unknown permission flags", index);
        }
        if (alignment != 0u && (alignment & (alignment - 1u)) != 0u) {
            return cupidbuild_user_elf_failure(reason, reason_capacity,
                                               "program header %u alignment is not a power of two",
                                               index);
        }
        if (type != 1u) {
            if (file_size != 0u || memory_size != 0u) {
                return cupidbuild_user_elf_failure(
                    reason, reason_capacity, "non-load program header has a payload at index %u",
                    index);
            }
            continue;
        }
        if (alignment > 1u && (offset & (alignment - 1u)) != (address & (alignment - 1u))) {
            return cupidbuild_user_elf_failure(reason, reason_capacity,
                                               "load segment %u alignment is incongruent", index);
        }
        if (file_size > memory_size) {
            return cupidbuild_user_elf_failure(
                reason, reason_capacity, "load segment %u has more file bytes than memory bytes",
                index);
        }
        if (offset > 0xffffffffu - file_size) {
            return cupidbuild_user_elf_failure(
                reason, reason_capacity, "load file range overflows the i386 address space", index);
        }
        if (address > 0xffffffffu - memory_size) {
            return cupidbuild_user_elf_failure(reason, reason_capacity,
                                               "load memory range overflows the i386 address space",
                                               index);
        }
        file_end = offset + file_size;
        memory_end = address + memory_size;
        if ((size_t)file_end > size) {
            return cupidbuild_user_elf_failure(
                reason, reason_capacity, "load segment %u extends beyond the executable", index);
        }
        if (file_size > 0u && offset > 0x7fffffffu) {
            return cupidbuild_user_elf_failure(
                reason, reason_capacity, "load segment %u cannot be reached by the loader", index);
        }
        if (memory_size == 0u) {
            continue;
        }
        if (address < 0x01c00000u || memory_end > 0x01e00000u) {
            return cupidbuild_user_elf_failure(
                reason, reason_capacity, "load segment %u is outside the external executable arena",
                index);
        }
        for (previous = 0u; previous < load_count; previous++) {
            if (address < ends[previous] && starts[previous] < memory_end) {
                return cupidbuild_user_elf_failure(
                    reason, reason_capacity, "linked executable load segments overlap", index);
            }
        }
        starts[load_count] = address;
        ends[load_count] = memory_end;
        load_count++;
        if ((flags & 1u) != 0u && address <= entry && entry - address < file_size) {
            entry_is_executable = 1;
        }
    }
    if (load_count == 0u) {
        return cupidbuild_user_elf_failure(
            reason, reason_capacity, "linked executable has no nonempty loadable segment", index);
    }
    if (!entry_is_executable) {
        return cupidbuild_user_elf_failure(
            reason, reason_capacity, "entry point is not in executable file-backed bytes", index);
    }
    return 1;
}

/* Keep the hosted JPEG validators in their established output order. */
#include "cupidbuild_jpeg.h"

static int cupidbuild_string_equals(ctool_string_t actual, const char *expected) {
    size_t expected_size = strlen(expected);
    return expected_size <= 4294967295u && actual.size == (ctool_u32)expected_size &&
           memcmp(actual.data, expected, expected_size) == 0;
}

int cupidbuild_validate_seed_image_bytes(const unsigned char *bytes, size_t size,
                                         cupidbuild_seed_image_format_t format,
                                         size_t artifact_index, int promoted,
                                         int current_windows_plan) {
    ctool_host_adapter_t adapter;
    ctool_job_config_t config;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    int valid = 0;
    int observer_workers = current_windows_plan >= 6;
    if (bytes == (const unsigned char *)0 || size == 0u || size > CUPIDBUILD_TOOL_BYTES ||
        (format != CUPIDBUILD_SEED_ELF32 && format != CUPIDBUILD_SEED_PE32) ||
        artifact_index >= (promoted ? CUPIDBUILD_SEED_ARTIFACTS : 5u) ||
        (promoted != 0 && promoted != 1) ||
        (current_windows_plan < 0 || current_windows_plan > 10) ||
        (current_windows_plan && (!promoted || format != CUPIDBUILD_SEED_PE32)) ||
        ctool_host_adapter_init(&adapter, ".") != CTOOL_OK) {
        return 0;
    }
    if (observer_workers)
        current_windows_plan -= 5;
    config = ctool_host_job_config(&adapter, ctool_default_limits());
    if (ctool_job_open(&config, &job) != CTOOL_OK) {
        return 0;
    }
    source.path.text = ctool_string("/checked-seed-tool");
    source.contents = ctool_bytes(bytes, (ctool_u32)size);
    if (format == CUPIDBUILD_SEED_PE32) {
        static const char *const ordinary_seed_imports[] = {
            "CloseHandle",          "CreateFileA",  "ExitProcess",  "GetCommandLineA",
            "GetCurrentDirectoryA", "GetLastError", "GetStdHandle", "ReadFile",
            "SetFilePointer",       "VirtualAlloc", "VirtualFree",  "WriteFile"};
        static const char *const ordinary_current_imports[] = {
            "CloseHandle",     "CreateFileA",          "ExitProcess",
            "GetCommandLineA", "GetCurrentDirectoryA", "GetFileInformationByHandle",
            "GetLastError",    "GetStdHandle",         "ReadFile",
            "SetFilePointer",  "VirtualAlloc",         "VirtualFree",
            "WriteFile"};
        static const char *const linker_seed_imports[] = {
            "CloseHandle",      "CreateFileA",     "DeleteFileA",          "ExitProcess",
            "FlushFileBuffers", "GetCommandLineA", "GetCurrentDirectoryA", "GetFullPathNameA",
            "GetLastError",     "GetStdHandle",    "MoveFileExA",          "ReadFile",
            "SetFilePointer",   "VirtualAlloc",    "VirtualFree",          "WriteFile"};
        static const char *const linker_current_imports[] = {"CloseHandle",
                                                             "CreateFileA",
                                                             "DeleteFileA",
                                                             "ExitProcess",
                                                             "FlushFileBuffers",
                                                             "GetCommandLineA",
                                                             "GetCurrentDirectoryA",
                                                             "GetFileInformationByHandle",
                                                             "GetFullPathNameA",
                                                             "GetLastError",
                                                             "GetStdHandle",
                                                             "MoveFileExA",
                                                             "ReadFile",
                                                             "SetFilePointer",
                                                             "VirtualAlloc",
                                                             "VirtualFree",
                                                             "WriteFile"};
        static const char *const cupidbuild_legacy_imports[] = {"CloseHandle",
                                                                "CreateDirectoryA",
                                                                "CreateFileA",
                                                                "CreateProcessA",
                                                                "DeleteFileA",
                                                                "ExitProcess",
                                                                "FindClose",
                                                                "FindFirstFileA",
                                                                "FindNextFileA",
                                                                "FlushFileBuffers",
                                                                "GetCommandLineA",
                                                                "GetCurrentDirectoryA",
                                                                "GetCurrentProcessId",
                                                                "GetExitCodeProcess",
                                                                "GetFileAttributesA",
                                                                "GetFileInformationByHandle",
                                                                "GetFullPathNameA",
                                                                "GetLastError",
                                                                "GetStdHandle",
                                                                "MoveFileExA",
                                                                "OpenProcess",
                                                                "ReadFile",
                                                                "RemoveDirectoryA",
                                                                "SetFilePointer",
                                                                "TerminateProcess",
                                                                "VirtualAlloc",
                                                                "VirtualFree",
                                                                "WaitForSingleObject",
                                                                "WriteFile"};
        static const char *const cupidbuild_current_imports[] = {
            "CloseHandle",
            "CreateDirectoryA",
            "CreateFileA",
            "CreateProcessA",
            "DeleteFileA",
            "DeleteProcThreadAttributeList",
            "ExitProcess",
            "FindClose",
            "FindFirstFileA",
            "FindNextFileA",
            "FlushFileBuffers",
            "GetCommandLineA",
            "GetCurrentDirectoryA",
            "GetCurrentProcessId",
            "GetExitCodeProcess",
            "GetFileAttributesA",
            "GetFileInformationByHandle",
            "GetFullPathNameA",
            "GetLastError",
            "GetStdHandle",
            "InitializeProcThreadAttributeList",
            "MoveFileExA",
            "OpenProcess",
            "ReadFile",
            "RemoveDirectoryA",
            "SetFilePointer",
            "SetHandleInformation",
            "TerminateProcess",
            "UpdateProcThreadAttribute",
            "VirtualAlloc",
            "VirtualFree",
            "WaitForSingleObject",
            "WriteFile"};
        static const char *const ordinary_utf8_imports[] = {"CloseHandle",
                                                            "CreateFileW",
                                                            "ExitProcess",
                                                            "GetCommandLineW",
                                                            "GetCurrentDirectoryW",
                                                            "GetFileInformationByHandle",
                                                            "GetLastError",
                                                            "GetStdHandle",
                                                            "ReadFile",
                                                            "SetFilePointer",
                                                            "SetLastError",
                                                            "VirtualAlloc",
                                                            "VirtualFree",
                                                            "WriteFile"};
        static const char *const ordinary_long_path_imports[] = {
            "CloseHandle",      "CreateFileW",          "ExitProcess",
            "GetCommandLineW",  "GetCurrentDirectoryW", "GetFileInformationByHandle",
            "GetFullPathNameW", "GetLastError",         "GetStdHandle",
            "ReadFile",         "SetFilePointer",       "SetLastError",
            "VirtualAlloc",     "VirtualFree",          "WriteFile"};
        static const char *const linker_utf8_imports[] = {"CloseHandle",
                                                          "CreateFileW",
                                                          "DeleteFileW",
                                                          "ExitProcess",
                                                          "FlushFileBuffers",
                                                          "GetCommandLineW",
                                                          "GetCurrentDirectoryW",
                                                          "GetFileInformationByHandle",
                                                          "GetFullPathNameW",
                                                          "GetLastError",
                                                          "GetStdHandle",
                                                          "MoveFileExW",
                                                          "ReadFile",
                                                          "SetFilePointer",
                                                          "SetLastError",
                                                          "VirtualAlloc",
                                                          "VirtualFree",
                                                          "WriteFile"};
        static const char *const cupidbuild_utf8_imports[] = {"CloseHandle",
                                                              "CreateDirectoryW",
                                                              "CreateFileW",
                                                              "CreateProcessW",
                                                              "DeleteFileW",
                                                              "DeleteProcThreadAttributeList",
                                                              "ExitProcess",
                                                              "FindClose",
                                                              "FindFirstFileW",
                                                              "FindNextFileW",
                                                              "FlushFileBuffers",
                                                              "GetCommandLineW",
                                                              "GetCurrentDirectoryW",
                                                              "GetCurrentProcessId",
                                                              "GetExitCodeProcess",
                                                              "GetFileAttributesW",
                                                              "GetFileInformationByHandle",
                                                              "GetFullPathNameW",
                                                              "GetLastError",
                                                              "GetStdHandle",
                                                              "InitializeProcThreadAttributeList",
                                                              "MoveFileExW",
                                                              "OpenProcess",
                                                              "ReadFile",
                                                              "RemoveDirectoryW",
                                                              "SetFilePointer",
                                                              "SetHandleInformation",
                                                              "SetLastError",
                                                              "TerminateProcess",
                                                              "UpdateProcThreadAttribute",
                                                              "VirtualAlloc",
                                                              "VirtualFree",
                                                              "WaitForSingleObject",
                                                              "WriteFile"};
        static const char *const cupidbuild_user_link_imports[] = {
            "CloseHandle",
            "CreateDirectoryW",
            "CreateFileW",
            "CreateProcessW",
            "DeleteFileW",
            "DeleteProcThreadAttributeList",
            "ExitProcess",
            "FindClose",
            "FindFirstFileW",
            "FindNextFileW",
            "FlushFileBuffers",
            "GetCommandLineW",
            "GetCurrentDirectoryW",
            "GetCurrentProcessId",
            "GetExitCodeProcess",
            "GetFileAttributesW",
            "GetFileInformationByHandle",
            "GetFinalPathNameByHandleW",
            "GetFullPathNameW",
            "GetLastError",
            "GetStdHandle",
            "InitializeProcThreadAttributeList",
            "MoveFileExW",
            "OpenProcess",
            "ReadFile",
            "RemoveDirectoryW",
            "SetFilePointer",
            "SetHandleInformation",
            "SetLastError",
            "TerminateProcess",
            "UpdateProcThreadAttribute",
            "VirtualAlloc",
            "VirtualFree",
            "WaitForSingleObject",
            "WriteFile"};
        static const char *const cupidbuild_legacy_ntdll_imports[] = {"NtSetInformationFile"};
        static const char *const cupidbuild_current_ntdll_imports[] = {
            "NtCreateFile", "NtQueryDirectoryFile", "NtSetInformationFile"};
        const char *worker_imports[sizeof(cupidbuild_user_link_imports) /
                                       sizeof(cupidbuild_user_link_imports[0]) +
                                   1u];
        const char *const *expected_imports =
            current_windows_plan ? ordinary_current_imports : ordinary_seed_imports;
        const char *const *expected_ntdll_imports = (const char *const *)0;
        size_t expected_count =
            current_windows_plan
                ? sizeof(ordinary_current_imports) / sizeof(ordinary_current_imports[0])
                : sizeof(ordinary_seed_imports) / sizeof(ordinary_seed_imports[0]);
        size_t expected_ntdll_count = 0u;
        size_t expected_library_count = artifact_index == 5u ? 2u : 1u;
        ctool_pe32_image_t image;
        ctool_u32 index;
        if ((promoted && artifact_index == 0u) || artifact_index == 3u) {
            expected_imports = current_windows_plan ? linker_current_imports : linker_seed_imports;
            expected_count =
                current_windows_plan
                    ? sizeof(linker_current_imports) / sizeof(linker_current_imports[0])
                    : sizeof(linker_seed_imports) / sizeof(linker_seed_imports[0]);
        }
        if (current_windows_plan >= 2) {
            if (artifact_index == 0u || artifact_index == 3u) {
                expected_imports = linker_utf8_imports;
                expected_count = sizeof(linker_utf8_imports) / sizeof(linker_utf8_imports[0]);
            } else {
                expected_imports = (current_windows_plan == 3 || current_windows_plan == 5)
                                       ? ordinary_long_path_imports
                                       : ordinary_utf8_imports;
                expected_count =
                    (current_windows_plan == 3 || current_windows_plan == 5)
                        ? sizeof(ordinary_long_path_imports) / sizeof(ordinary_long_path_imports[0])
                        : sizeof(ordinary_utf8_imports) / sizeof(ordinary_utf8_imports[0]);
            }
        }
        if (ctool_pe32_read(job, &source, &image) == CTOOL_OK && image.entry_point == 0x00401000u &&
            image.import_library_count == (ctool_u32)expected_library_count &&
            cupidbuild_string_equals(image.import_libraries[0].name, "KERNEL32.dll")) {
            valid = 1;
            if (artifact_index == 5u) {
                size_t legacy_count =
                    sizeof(cupidbuild_legacy_imports) / sizeof(cupidbuild_legacy_imports[0]);
                size_t legacy_ntdll_count = sizeof(cupidbuild_legacy_ntdll_imports) /
                                            sizeof(cupidbuild_legacy_ntdll_imports[0]);
                size_t current_count =
                    sizeof(cupidbuild_current_imports) / sizeof(cupidbuild_current_imports[0]);
                size_t current_ntdll_count = sizeof(cupidbuild_current_ntdll_imports) /
                                             sizeof(cupidbuild_current_ntdll_imports[0]);
                if (current_windows_plan) {
                    expected_imports = current_windows_plan >= 4   ? cupidbuild_user_link_imports
                                       : current_windows_plan >= 2 ? cupidbuild_utf8_imports
                                                                   : cupidbuild_current_imports;
                    expected_count =
                        current_windows_plan >= 4 ? sizeof(cupidbuild_user_link_imports) /
                                                        sizeof(cupidbuild_user_link_imports[0])
                        : current_windows_plan >= 2
                            ? sizeof(cupidbuild_utf8_imports) / sizeof(cupidbuild_utf8_imports[0])
                            : current_count;
                    expected_ntdll_imports = cupidbuild_current_ntdll_imports;
                    expected_ntdll_count = current_ntdll_count;
                } else {
                    expected_imports = cupidbuild_legacy_imports;
                    expected_count = legacy_count;
                    expected_ntdll_imports = cupidbuild_legacy_ntdll_imports;
                    expected_ntdll_count = legacy_ntdll_count;
                }
                if (observer_workers) {
                    size_t import_index;
                    /* Each exact Build profile sorts CreateProcess before DeleteFile. */
                    for (import_index = 0u; import_index < expected_count; import_index++) {
                        worker_imports[import_index < 4u ? import_index : import_index + 1u] =
                            expected_imports[import_index];
                    }
                    worker_imports[4] = "CreateThread";
                    expected_imports = worker_imports;
                    expected_count++;
                }
                if (!cupidbuild_string_equals(image.import_libraries[1].name, "NTDLL.dll") ||
                    (size_t)image.import_count != expected_count + expected_ntdll_count) {
                    valid = 0;
                }
            } else if ((size_t)image.import_count != expected_count) {
                valid = 0;
            }
            for (index = 0u; valid && index < (ctool_u32)expected_count; index++) {
                if (!cupidbuild_string_equals(image.imports[index].library_name, "KERNEL32.dll") ||
                    !cupidbuild_string_equals(image.imports[index].procedure_name,
                                              expected_imports[index])) {
                    valid = 0;
                    break;
                }
            }
            if (valid && artifact_index == 5u) {
                for (index = 0u; valid && index < (ctool_u32)expected_ntdll_count; index++) {
                    if (!cupidbuild_string_equals(
                            image.imports[expected_count + index].library_name, "NTDLL.dll") ||
                        !cupidbuild_string_equals(
                            image.imports[expected_count + index].procedure_name,
                            expected_ntdll_imports[index])) {
                        valid = 0;
                    }
                }
            }
        }
    } else {
        ctool_elf32_object_t object;
        ctool_u32 index;
        ctool_u32 load_count = 0u;
        int entry_in_code = 0;
        if (ctool_elf32_read(job, &source, &object) == CTOOL_OK &&
            object.file_type == CTOOL_ELF32_ET_EXEC && object.entry_point == 0x08048000u &&
            object.program_header_count != 0u) {
            valid = 1;
            for (index = 0u; index < object.program_header_count; index++) {
                const ctool_elf32_program_header_t *header = &object.program_headers[index];
                if (header->type == CTOOL_ELF32_PT_DYNAMIC ||
                    header->type == CTOOL_ELF32_PT_INTERP) {
                    valid = 0;
                    break;
                }
                if (header->type != CTOOL_ELF32_PT_LOAD) {
                    continue;
                }
                load_count++;
                if ((header->flags & (CTOOL_ELF32_PF_W | CTOOL_ELF32_PF_X)) ==
                    (CTOOL_ELF32_PF_W | CTOOL_ELF32_PF_X)) {
                    valid = 0;
                    break;
                }
                if ((header->flags & CTOOL_ELF32_PF_X) != 0u &&
                    object.entry_point >= header->virtual_address &&
                    object.entry_point - header->virtual_address < header->file_size) {
                    entry_in_code = 1;
                }
            }
            if (load_count == 0u || entry_in_code == 0) {
                valid = 0;
            }
        }
    }
    ctool_job_close(job);
    return valid;
}

static int cupidbuild_validate_execution_profile(cupidbuild_host_transaction_t *transaction,
                                                 const char *path, size_t artifact_index,
                                                 int promoted, int current_windows_plan) {
    unsigned char *bytes;
    size_t size = 0u;
    int valid;
    bytes = cupidbuild_host_read_frozen_input(transaction, path, CUPIDBUILD_TOOL_BYTES, &size);
    valid = cupidbuild_validate_seed_image_bytes(bytes, size,
#if defined(_WIN32)
                                                 CUPIDBUILD_SEED_PE32,
#else
                                                 CUPIDBUILD_SEED_ELF32,
#endif
                                                 artifact_index, promoted, current_windows_plan);
    free(bytes);
    return valid;
}
