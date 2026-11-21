## @file
#  @brief Provide rejected changes to fixed-point producer recipes and checks.
import textwrap


def bootstrap_mutations(native_build_loop_feature):
    return {
        "profile positive stops comparing stages": (
            "bootstrap",
            "    profile_result = _run_stage_pair(\n",
            "    profile_result = _run_one_stage(\n",
            r"fixed-point profile behavior differs",
        ),
        "truncated profile failure disappears": (
            "bootstrap",
            '        ("truncated", profile_payload[:-1], '
            '"snapshot is truncated"),\n',
            '        ("short-input", profile_payload[:-1], '
            '"snapshot is truncated"),\n',
            r"fixed-point profile behavior differs",
        ),
        "unsafe profile failure disappears": (
            "bootstrap",
            '            "unsafe-path",\n',
            '            "invalid-path",\n',
            r"fixed-point profile behavior differs",
        ),
        "profile case collision failure disappears": (
            "bootstrap",
            '            "case-collision",\n',
            '            "case-mismatch",\n',
            r"fixed-point profile behavior differs",
        ),
        "profile failures stop comparing stages": (
            "bootstrap",
            "        stage_three_profile_failure.write_bytes(sentinel)\n"
            "        failure_result = _run_stage_pair(\n",
            "        stage_three_profile_failure.write_bytes(sentinel)\n"
            "        failure_result = _run_one_stage(\n",
            r"fixed-point profile behavior differs",
        ),
        "profile failures stop checking diagnostics": (
            "bootstrap",
            "            or failure_message not in failure_result.stderr\n",
            '            or failure_result.stderr == ""\n',
            r"fixed-point profile behavior differs",
        ),
        "profile failures stop preserving the second output": (
            "bootstrap",
            "            or stage_three_profile_failure.read_bytes() != sentinel\n",
            "            or stage_two_profile_failure.read_bytes() != sentinel\n",
            r"fixed-point profile behavior differs",
        ),
        "PE32 positive stops comparing stages": (
            "bootstrap",
            "    pe32_result = _run_stage_pair(\n",
            "    pe32_result = _run_one_stage(\n",
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 positive loses its output mode": (
            "bootstrap",
            '            "i386pe",\n'
            '            "--text-address",\n'
            '            "0x00401000",\n',
            '            "elf_i386",\n'
            '            "--text-address",\n'
            '            "0x00401000",\n',
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 positive compares the first output twice": (
            "bootstrap",
            "        or stage_two_pe32.read_bytes()\n"
            "        != stage_three_pe32.read_bytes()\n",
            "        or stage_two_pe32.read_bytes()\n"
            "        != stage_two_pe32.read_bytes()\n",
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 staged parser disappears": (
            "bootstrap",
            "    _validate_static_i386_pe32(\n        stage_two_pe32,\n",
            "    _skip_static_i386_pe32_validation(\n        stage_two_pe32,\n",
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 staged parser stops reading the image": (
            "bootstrap",
            "        path.read_bytes(), path.name, expected_entry, "
            "expected_imports\n",
            '        b"", path.name, expected_entry, expected_imports\n',
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 staged parser accepts a changed DOS stub": (
            "bootstrap",
            '    "4d5a90000300000004000000ffff0000"\n',
            '    "4d5a91000300000004000000ffff0000"\n',
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 staged parser reads the checksum from the wrong field": (
            "bootstrap",
            '    checksum = read_u32(optional_offset + 64, "PE32 checksum")\n',
            '    checksum = read_u32(optional_offset + 60, "PE32 checksum")\n',
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 staged parser allows writable read-only data": (
            "bootstrap",
            '        ".rodata": (1, 0x40000040),\n',
            '        ".rodata": (1, 0xC0000040),\n',
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 staged parser drops the empty-section guard": (
            "bootstrap",
            "        if virtual_size == 0:\n"
            "            raise BootstrapError("
            'f"{name} has an empty PE32 section")\n',
            "",
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 staged parser guards only empty BSS": (
            "bootstrap",
            "        if virtual_size == 0:\n"
            "            raise BootstrapError("
            'f"{name} has an empty PE32 section")\n',
            '        if virtual_size == 0 and name == ".bss":\n'
            "            raise BootstrapError("
            'f"{name} has an empty PE32 section")\n',
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 staged parser skips the first header padding byte": (
            "bootstrap",
            "        data[section_table_end:headers_size]\n",
            "        data[section_table_end + 1:headers_size]\n",
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 staged parser skips the first section padding byte": (
            "bootstrap",
            "            if any(data[raw_offset + virtual_size "
            ": raw_offset + raw_size]):\n",
            "            if any(data[raw_offset + virtual_size + 1 "
            ": raw_offset + raw_size]):\n",
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 staged parser weakens the final file extent": (
            "bootstrap",
            "        or expected_raw_offset != len(data)\n",
            "        or expected_raw_offset > len(data)\n",
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 staged parser expects the wrong entry": (
            "bootstrap",
            "    _validate_static_i386_pe32(\n"
            "        stage_two_pe32,\n"
            "        0x00401000,\n"
            "    )\n",
            "    _validate_static_i386_pe32(\n"
            "        stage_two_pe32,\n"
            "        0x00402000,\n"
            "    )\n",
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 import parser resolves outside idata": (
            "bootstrap",
            '        ) = sections[".idata"]\n',
            '        ) = sections[".text"]\n',
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 import parser accepts a displaced lookup table": (
            "bootstrap",
            "            if lookup_rva != idata_virtual_address + cursor:\n",
            "            if False:\n",
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 import parser accepts trailing payload": (
            "bootstrap",
            "        if cursor != idata_virtual_size:\n",
            "        if cursor > idata_virtual_size:\n",
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 failure stops comparing stages": (
            "bootstrap",
            "    invalid_pe32_result = _run_stage_pair(\n",
            "    invalid_pe32_result = _run_one_stage(\n",
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 failure skips the second sentinel setup": (
            "bootstrap",
            "    stage_three_pe32_failure.write_bytes(sentinel)\n",
            "    stage_two_pe32_failure.write_bytes(sentinel)\n",
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 failure uses the accepted text address": (
            "bootstrap",
            '            "i386pe",\n'
            '            "--text-address",\n'
            '            "0x00402000",\n',
            '            "i386pe",\n'
            '            "--text-address",\n'
            '            "0x00401000",\n',
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 failure loses its diagnostic": (
            "bootstrap",
            '        or "CupidLD PE32 requires text address 0x00401000"\n',
            '        or "CupidLD PE32 text address differs"\n',
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 failure stops preserving the second output": (
            "bootstrap",
            "        or stage_three_pe32_failure.read_bytes() != sentinel\n",
            "        or stage_two_pe32_failure.read_bytes() != sentinel\n",
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 success count becomes stale": (
            "bootstrap",
            '        "success_cases": 73,\n',
            '        "success_cases": 36,\n',
            r"fixed-point behavior matrix differs",
        ),
        "_run_behavior_checks kernel compiler gate moves under a dead block": (
            "bootstrap",
            '    _check_cupidbuild_compile_kernel_behavior(\n'
            '        runner,\n'
            '        behavior_root,\n'
            '        stage_two,\n'
            '        stage_three,\n'
            '        seed_inputs,\n'
            '        "",\n'
            '    )\n',
            '    if False:\n'
            '        _check_cupidbuild_compile_kernel_behavior(\n'
            '            runner,\n'
            '            behavior_root,\n'
            '            stage_two,\n'
            '            stage_three,\n'
            '            seed_inputs,\n'
            '            "",\n'
            '        )\n',
            r"fixed-point kernel compile behavior differs:.*live code",
        ),
        "_run_behavior_checks doom compiler gate moves under a dead block": (
            "bootstrap",
            '    _check_cupidbuild_compile_doom_behavior(\n'
            '        runner, profile_source_root, behavior_root, stage_two, stage_three,\n'
            '        seed_inputs, "",\n'
            '    )\n',
            '    if False:\n'
            '        _check_cupidbuild_compile_doom_behavior(\n'
            '            runner, profile_source_root, behavior_root, stage_two, stage_three,\n'
            '            seed_inputs, "",\n'
            '        )\n',
            r"fixed-point Doom compile behavior differs:.*live code",
        ),
        "_run_behavior_checks production compiler gate moves under a dead block": (
            "bootstrap",
            '    _check_cupidbuild_compile_production_behavior(\n'
            '        runner, behavior_root, stage_two, stage_three, seed_inputs, "",\n'
            '    )\n',
            '    if False:\n'
            '        _check_cupidbuild_compile_production_behavior(\n'
            '            runner, behavior_root, stage_two, stage_three, seed_inputs, "",\n'
            '        )\n',
            r"fixed-point production compile behavior differs:.*live code",
        ),
        "_run_native_windows_behavior_checks kernel compiler gate moves under a dead block": (
            "bootstrap",
            '    _check_cupidbuild_compile_kernel_behavior(\n'
            '        runner,\n'
            '        behavior_root,\n'
            '        stage_two,\n'
            '        stage_three,\n'
            '        behavior_seed_inputs,\n'
            '        "native Windows ",\n'
            '    )\n',
            '    if False:\n'
            '        _check_cupidbuild_compile_kernel_behavior(\n'
            '            runner,\n'
            '            behavior_root,\n'
            '            stage_two,\n'
            '            stage_three,\n'
            '            behavior_seed_inputs,\n'
            '            "native Windows ",\n'
            '        )\n',
            r"fixed-point kernel compile behavior differs:.*live code",
        ),
        "_run_native_windows_behavior_checks doom compiler gate moves under a dead block": (
            "bootstrap",
            '    _check_cupidbuild_compile_doom_behavior(\n'
            '        runner, profile_source_root, behavior_root, stage_two, stage_three,\n'
            '        behavior_seed_inputs, "native Windows ",\n'
            '    )\n',
            '    if False:\n'
            '        _check_cupidbuild_compile_doom_behavior(\n'
            '            runner, profile_source_root, behavior_root, stage_two, stage_three,\n'
            '            behavior_seed_inputs, "native Windows ",\n'
            '        )\n',
            r"fixed-point Doom compile behavior differs:.*live code",
        ),
        "_run_native_windows_behavior_checks production compiler gate moves under a dead block": (
            "bootstrap",
            '    _check_cupidbuild_compile_production_behavior(\n'
            '        runner, behavior_root, stage_two, stage_three, behavior_seed_inputs, "native Windows ",\n'
            '    )\n',
            '    if False:\n'
            '        _check_cupidbuild_compile_production_behavior(\n'
            '            runner, behavior_root, stage_two, stage_three, behavior_seed_inputs, "native Windows ",\n'
            '        )\n',
            r"fixed-point production compile behavior differs:.*live code",
        ),
        "kernel compile loses failure timestamp preservation": (
            "bootstrap",
            "tuple(output.stat().st_mtime_ns for output in outputs) != before",
            "False",
            r"fixed-point kernel compile behavior differs",
        ),
        "kernel compile stops checking recovery bytes": (
            "bootstrap",
            "success(code_source, expected=expected_code)",
            "success(code_source)",
            r"fixed-point kernel compile behavior differs",
        ),
        "ISO pattern fixture leaves the raw operation matrix": (
            "bootstrap",
            '        ("guarded-iso-pattern.asm", 4096),\n',
            '        ("guarded-iso-pattern.asm", 4095),\n',
            r"fixed-point CupidBuild behavior differs",
        ),
        "ISO pattern output loses its byte check": (
            "bootstrap",
            "and stage_two_raw.read_bytes() != bytes(range(256)) * 16",
            "and stage_two_raw.read_bytes() != stage_three_raw.read_bytes()",
            r"fixed-point CupidBuild behavior differs",
        ),
        "ISO pattern rollback accepts any diagnostic": (
            "bootstrap",
            "or diagnostic not in malformed_raw_result.stderr",
            'or "cupidbuild:" not in malformed_raw_result.stderr',
            r"fixed-point CupidBuild behavior differs",
        ),
        "local-target failure count becomes stale": (
            "bootstrap",
            '        "failure_cases": 66,\n',
            '        "failure_cases": 30,\n',
            r"fixed-point behavior matrix differs",
        ),
        "native Windows link loses UTF-8 import selection": (
            "bootstrap",
            'for selector in _windows_import_selectors(tool_name, utf8=utf8,\n'
            '        long_paths=long_paths, user_link_aliases=user_link_aliases,\n'
            '        observer_workers=observer_workers):',
            "for selector in _windows_import_selectors(tool_name):",
            r"native Windows fixed-point behavior differs",
        ),
        "native Windows driver drops long-profile capture selection": (
            "bootstrap",
            "            linux_plan,\n"
            "            private_workspace / \"source\",\n"
            "            windows_utf8=True,\n"
            "            windows_long_paths=windows_long_paths,\n",
            "            linux_plan,\n"
            "            private_workspace / \"source\",\n"
            "            windows_utf8=True,\n",
            r"fixed-point source freeze differs",
        ),
        "Linux driver drops long-profile capture selection": (
            "bootstrap",
            "            plan,\n"
            "            private_workspace / \"source\",\n"
            "            windows_utf8=True,\n"
            "            windows_long_paths=windows_long_paths,\n",
            "            plan,\n"
            "            private_workspace / \"source\",\n"
            "            windows_utf8=True,\n",
            r"fixed-point source freeze differs",
        ),
        "native Windows driver loses UTF-8 plan selection": (
            "bootstrap",
            "native_plan = _windows_build_plan(linux_plan, utf8=True, long_paths=windows_long_paths,\n"
            "        user_link_aliases=windows_user_link_aliases, observer_workers=True)",
            "native_plan = _windows_build_plan(linux_plan)",
            r"fixed-point source freeze differs",
        ),
        "native Windows linked-target count becomes stale": (
            "bootstrap",
            '        "failure_cases": len(tool_names) + 48,\n',
            '        "failure_cases": len(tool_names) + 12,\n',
            r"native Windows fixed-point behavior differs",
        ),
        "Linux fixed-point C objects skip CupidDis certification": (
            "bootstrap",
            "        _certify_relocatable_code_anchors(\n"
            "            runner,\n"
            '            producers["cupiddis"],\n'
            "            object_path,\n"
            '            f"{stage_name} CupidC for {logical_source}",\n'
            "        )\n",
            "        _skip_relocatable_code_certification(\n"
            "            runner,\n"
            '            producers["cupiddis"],\n'
            "            object_path,\n"
            '            f"{stage_name} CupidC for {logical_source}",\n'
            "        )\n",
            r"fixed-point C object certification differs",
        ),
        "native Windows fixed-point C objects skip CupidDis certification": (
            "bootstrap",
            "        _certify_relocatable_code_anchors(\n"
            "            runner,\n"
            '            producers["cupiddis"],\n'
            "            object_path,\n"
            '            f"{stage_name} native CupidC for '
            '{logical_source}",\n'
            "        )\n",
            "        _skip_relocatable_code_certification(\n"
            "            runner,\n"
            '            producers["cupiddis"],\n'
            "            object_path,\n"
            '            f"{stage_name} native CupidC for '
            '{logical_source}",\n'
            "        )\n",
            r"fixed-point C object certification differs",
        ),
        "fixed-point C object certification weakens strict decode": (
            "bootstrap",
            '            "--require-known",\n'
            '            "--require-local-targets",\n'
            '            "--require-code-anchors",\n'
            "            object_path,\n",
            '            "--require-known",\n'
            '            "--skip-local-targets",\n'
            '            "--require-code-anchors",\n'
            "            object_path,\n",
            r"fixed-point C object certification differs",
        ),
        "candidate image certification helper disappears": (
            "bootstrap",
            "def _check_candidate_image_certification_behavior(\n",
            "def _removed_candidate_image_certification_behavior(\n",
            r"candidate image certification differs",
        ),
        "candidate image certification weakens strict decode": (
            "bootstrap",
            '        "--require-code-anchors",\n'
            "    )\n"
            "    for tool_name in CANDIDATE_TOOL_NAMES:\n",
            '        "--skip-code-anchors",\n'
            "    )\n"
            "    for tool_name in CANDIDATE_TOOL_NAMES:\n",
            r"candidate image certification differs",
        ),
        "candidate image certification stops corrupting CupidBuild": (
            "bootstrap",
            "        _corrupt_candidate_entry_instruction(\n"
            "            source_image.read_bytes(), source_image.name\n"
            "        )\n",
            "        source_image.read_bytes()\n",
            r"candidate image certification differs",
        ),
        "candidate image certification accepts negative stdout": (
            "bootstrap",
            "        failure_result.stdout\n"
            '        or "code check failed" not in failure_result.stderr\n',
            '        "code check failed" not in failure_result.stderr\n',
            r"candidate image certification differs",
        ),
        "candidate image certification restores the short timeout": (
            "bootstrap",
            "(*strict_flags, stage_three.tools[tool_name]),\n            360,\n",
            "(*strict_flags, stage_three.tools[tool_name]),\n            120,\n",
            r"candidate image certification differs",
        ),
        "candidate image corruption drops PE32": (
            "bootstrap",
            '    elif image[:2] == b"MZ":\n',
            "    elif False:\n",
            r"candidate image certification differs",
        ),
        "candidate image corruption writes valid no-ops": (
            "bootstrap",
            '    image[entry_offset : entry_offset + 2] = b"\\x0f\\xff"\n',
            '    image[entry_offset : entry_offset + 2] = b"\\x90\\x90"\n',
            r"candidate image certification differs",
        ),
        "candidate image corruption ignores the PE virtual extent": (
            "bootstrap",
            "    file_backed_size = min(virtual_size, file_size)\n",
            "    file_backed_size = file_size\n",
            r"candidate image certification differs",
        ),
        "Linux candidate image certification moves under a dead block": (
            "bootstrap",
            "    _check_candidate_image_certification_behavior(\n"
            "        runner,\n"
            "        behavior_root,\n"
            "        stage_two,\n"
            "        stage_three,\n"
            '        "",\n'
            "    )\n",
            "    if False:\n"
            "        _check_candidate_image_certification_behavior(\n"
            "            runner,\n"
            "            behavior_root,\n"
            "            stage_two,\n"
            "            stage_three,\n"
            '            "",\n'
            "        )\n",
            r"candidate image certification call",
        ),
        "Windows candidate image certification moves under a dead block": (
            "bootstrap",
            "    _check_candidate_image_certification_behavior(\n"
            "        runner,\n"
            "        behavior_root,\n"
            "        stage_two,\n"
            "        stage_three,\n"
            '        "native Windows ",\n'
            "    )\n",
            "    if False:\n"
            "        _check_candidate_image_certification_behavior(\n"
            "            runner,\n"
            "            behavior_root,\n"
            "            stage_two,\n"
            "            stage_three,\n"
            '            "native Windows ",\n'
            "        )\n",
            r"candidate image certification call",
        ),
        "checked CupidObj runner helper disappears": (
            "bootstrap",
            "def _check_cupidbuild_cupidobj_runner_behavior(\n",
            "def _removed_cupidbuild_cupidobj_runner_behavior(\n",
            r"checked CupidObj runner call differs",
        ),
        "checked CupidObj runner stops comparing output": (
            "bootstrap",
            "        or stage_two_output.read_bytes() "
            "!= stage_three_output.read_bytes()\n",
            "        or False\n",
            r"checked CupidObj runner behavior differs",
        ),
        "Linux checked CupidObj runner moves under a dead block": (
            "bootstrap",
            "    _check_cupidbuild_cupidobj_runner_behavior(\n"
            "        runner,\n"
            "        behavior_root,\n"
            "        stage_two,\n"
            "        stage_three,\n"
            "        seed_inputs,\n"
            '        "",\n'
            "    )\n",
            "    if False:\n"
            "        _check_cupidbuild_cupidobj_runner_behavior(\n"
            "            runner,\n"
            "            behavior_root,\n"
            "            stage_two,\n"
            "            stage_three,\n"
            "            seed_inputs,\n"
            '            "",\n'
            "        )\n",
            r"checked CupidObj runner call",
        ),
        "Windows checked CupidObj runner moves under a dead block": (
            "bootstrap",
            "    _check_cupidbuild_cupidobj_runner_behavior(\n"
            "        runner,\n"
            "        behavior_root,\n"
            "        stage_two,\n"
            "        stage_three,\n"
            "        behavior_seed_inputs,\n"
            '        "native Windows ",\n'
            "    )\n",
            "    if False:\n"
            "        _check_cupidbuild_cupidobj_runner_behavior(\n"
            "            runner,\n"
            "            behavior_root,\n"
            "            stage_two,\n"
            "            stage_three,\n"
            "            seed_inputs,\n"
            '            "native Windows ",\n'
            "        )\n",
            r"checked CupidObj runner call",
        ),
        "checked CupidC runner helper disappears": (
            "bootstrap",
            "def _check_cupidbuild_cupidc_runner_behavior(\n",
            "def _removed_cupidbuild_cupidc_runner_behavior(\n",
            r"checked CupidC runner call differs",
        ),
        "checked CupidC runner stops comparing output": (
            "bootstrap",
            "        or stage_two_output.read_bytes() "
            "!= stage_three_output.read_bytes()\n"
            "    ):\n"
            "        raise BootstrapError(\n"
            "            f\"{label_prefix}CupidBuild checked CupidC "
            "output differs\"\n"
            "        )\n",
            "        or False\n"
            "    ):\n"
            "        raise BootstrapError(\n"
            "            f\"{label_prefix}CupidBuild checked CupidC "
            "output differs\"\n"
            "        )\n",
            r"checked CupidC runner behavior differs",
        ),
        "Linux checked CupidC runner moves under a dead block": (
            "bootstrap",
            "    _check_cupidbuild_cupidc_runner_behavior(\n"
            "        runner,\n"
            "        behavior_root,\n"
            "        stage_two,\n"
            "        stage_three,\n"
            "        seed_inputs,\n"
            '        "",\n'
            "    )\n",
            "    if False:\n"
            "        _check_cupidbuild_cupidc_runner_behavior(\n"
            "            runner,\n"
            "            behavior_root,\n"
            "            stage_two,\n"
            "            stage_three,\n"
            "            seed_inputs,\n"
            '            "",\n'
            "        )\n",
            r"checked CupidC runner call",
        ),
        "Windows checked CupidC runner moves under a dead block": (
            "bootstrap",
            "    _check_cupidbuild_cupidc_runner_behavior(\n"
            "        runner,\n"
            "        behavior_root,\n"
            "        stage_two,\n"
            "        stage_three,\n"
            "        behavior_seed_inputs,\n"
            '        "native Windows ",\n'
            "    )\n",
            "    if False:\n"
            "        _check_cupidbuild_cupidc_runner_behavior(\n"
            "            runner,\n"
            "            behavior_root,\n"
            "            stage_two,\n"
            "            stage_three,\n"
            "            seed_inputs,\n"
            '            "native Windows ",\n'
            "        )\n",
            r"checked CupidC runner call",
        ),
        "JPEG success stage comparison moves under a dead block": (
            "bootstrap",
            "    success_result = _run_stage_pair(\n"
            "        runner,\n"
            "        stage_two,\n"
            "        stage_three,\n"
            '        "cupidbuild",\n'
            "        [\n"
            "            *common_arguments(stage_two_manifest),\n"
            '            "--output",\n'
            "            stage_two_output.relative_to("
            "source_root).as_posix(),\n"
            "        ],\n"
            "        [\n"
            "            *common_arguments(stage_three_manifest),\n"
            '            "--output",\n'
            "            stage_three_output.relative_to("
            "source_root).as_posix(),\n"
            "        ],\n"
            "        180,\n"
            "    )\n",
            "    if 0:\n"
            "        success_result = _run_stage_pair(\n"
            "            runner,\n"
            "            stage_two,\n"
            "            stage_three,\n"
            '            "cupidbuild",\n'
            "            [\n"
            "                *common_arguments(stage_two_manifest),\n"
            '                "--output",\n'
            "                stage_two_output.relative_to("
            "source_root).as_posix(),\n"
            "            ],\n"
            "            [\n"
            "                *common_arguments(stage_three_manifest),\n"
            '                "--output",\n'
            "                stage_three_output.relative_to("
            "source_root).as_posix(),\n"
            "            ],\n"
            "            180,\n"
            "        )\n"
            "    else:\n"
            "        success_result = None\n",
            r"fixed-point JPEG publication differs",
        ),
        "JPEG success output guard stops rejecting drift": (
            "bootstrap",
            "        raise BootstrapError(\n"
            '            f"{label_prefix}CupidBuild JPEG output '
            'differs"\n'
            "        )\n",
            "        pass\n",
            r"fixed-point JPEG publication behavior differs",
        ),
        "typed profile command loses its production entry point": (
            "bootstrap",
            '        "generate-profile-manifest",\n',
            '        "profile-manifest",\n',
            r"typed profile publication behavior differs",
        ),
        "typed profile fixed point stops exercising the clean parent": (
            "bootstrap",
            '    output_logical = Path('
            '"build/bootstrap/doom-cupidc-inputs.json")\n',
            '    output_logical = Path("doom-cupidc-inputs.json")\n',
            r"typed profile publication behavior differs",
        ),
        "typed profile fixed point stops checking parent rollback": (
            "bootstrap",
            '        or (stage_two_failure_root / "build").exists()\n',
            "        or False\n",
            r"typed profile publication behavior differs",
        ),
        "typed profile behavior reads the private compiler closure": (
            "bootstrap",
            "    _check_cupidbuild_generate_profile_behavior(\n"
            "        runner,\n"
            "        profile_source_root,\n"
            "        behavior_root,\n"
            "        stage_two,\n"
            "        stage_three,\n"
            "        seed_inputs,\n"
            '        "",\n'
            "        False,\n"
            "    )\n",
            "    _check_cupidbuild_generate_profile_behavior(\n"
            "        runner,\n"
            "        source_root,\n"
            "        behavior_root,\n"
            "        stage_two,\n"
            "        stage_three,\n"
            "        seed_inputs,\n"
            '        "",\n'
            "        False,\n"
            "    )\n",
            r"typed profile publication differs",
        ),
        "typed profile Linux claims unsafe clean-parent creation": (
            "bootstrap",
            '        "",\n'
            "        False,\n"
            "    )\n\n"
            "    _check_cupidbuild_guarded_object_behavior(\n",
            '        "",\n'
            "        True,\n"
            "    )\n\n"
            "    _check_cupidbuild_guarded_object_behavior(\n",
            r"typed profile publication differs",
        ),
        "linked-target behavior helper disappears": (
            "bootstrap",
            "def _check_executable_local_target_behavior(\n",
            "def _removed_executable_local_target_behavior(\n",
            r"linked-code policy helpers differ",
        ),
        "code-anchor behavior helper disappears": (
            "bootstrap",
            "def _check_executable_code_anchor_behavior(\n",
            "def _removed_executable_code_anchor_behavior(\n",
            r"linked-code policy helpers differ",
        ),
        "Linux linked-target behavior moves under a dead block": (
            "bootstrap",
            "    _check_executable_local_target_behavior(\n"
            "        runner,\n"
            "        stage_two,\n"
            "        stage_three,\n"
            "        behavior_root,\n"
            '        "",\n'
            "    )\n",
            "    if False:\n"
            "        _check_executable_local_target_behavior(\n"
            "            runner,\n"
            "            stage_two,\n"
            "            stage_three,\n"
            "            behavior_root,\n"
            '            "",\n'
            "        )\n",
            r"linked-code policy calls differ",
        ),
        "native Windows linked-target behavior moves under a dead block": (
            "bootstrap",
            "    _check_executable_local_target_behavior(\n"
            "        runner,\n"
            "        stage_two,\n"
            "        stage_three,\n"
            "        behavior_root,\n"
            '        "native Windows ",\n'
            "    )\n",
            "    if False:\n"
            "        _check_executable_local_target_behavior(\n"
            "            runner,\n"
            "            stage_two,\n"
            "            stage_three,\n"
            "            behavior_root,\n"
            '            "native Windows ",\n'
            "        )\n",
            r"linked-code policy calls differ",
        ),
        "PE32 import source leaves the frozen closure": (
            "bootstrap",
            'source_root / "toolchain/hosted/i386-windows/start.asm"',
            'source_root / "toolchain/hosted/i386-linux/start.asm"',
            r"fixed-point source freeze differs",
        ),
        "Linux bootstrap bypasses the candidate plan": (
            "bootstrap",
            "    plan = _candidate_build_plan(checked_plan)\n",
            "    plan = checked_plan\n",
            r"fixed-point source freeze differs",
        ),
        "Linux candidate call is hidden in a false conditional": (
            "bootstrap",
            "    plan = _candidate_build_plan(checked_plan)\n",
            "    plan = (\n"
            "        _candidate_build_plan(checked_plan)\n"
            "        if False\n"
            "        else checked_plan\n"
            "    )\n",
            r"fixed-point source freeze differs",
        ),
        "Linux candidate plan is reassigned before freezing": (
            "bootstrap",
            "    plan = _candidate_build_plan(checked_plan)\n",
            "    plan = _candidate_build_plan(checked_plan)\n"
            "    plan = checked_plan\n",
            r"fixed-point source freeze differs",
        ),
        "Windows bootstrap bypasses the candidate plan": (
            "bootstrap",
            "    linux_plan = _candidate_build_plan(checked_linux_plan)\n",
            "    linux_plan = checked_linux_plan\n",
            r"fixed-point source freeze differs",
        ),
        "candidate helper returns the checked source list": (
            "bootstrap",
            '    candidate["sources"] = sources\n',
            '    candidate["sources"] = raw_sources\n',
            r"fixed-point source freeze differs",
        ),
        "candidate helper stops upgrading v1 sources": (
            "bootstrap",
            "        if actual is None:\n            sources.append(expected)\n",
            "        if actual is None:\n            continue\n",
            r"fixed-point source freeze differs",
        ),
        "candidate helper duplicates exact v2 sources": (
            "bootstrap",
            "        if actual is None:\n            sources.append(expected)\n",
            "        if True:\n            sources.append(expected)\n",
            r"fixed-point source freeze differs",
        ),
        "candidate helper accepts a conflicting candidate source": (
            "bootstrap",
            "        if actual is not None and actual != expected:\n",
            "        if False:\n",
            r"fixed-point source freeze differs",
        ),
        "candidate helper accepts another tool set": (
            "bootstrap",
            "    if set(raw_links) not in "
            "(set(TOOL_NAMES), set(CANDIDATE_TOOL_NAMES)):\n",
            "    if not raw_links:\n",
            r"fixed-point source freeze differs",
        ),
        "candidate helper accepts a conflicting candidate link": (
            "bootstrap",
            "        if tuple(cupidbuild_link) not in (\n"
            "            EARLIER_PROMOTED_CUPIDBUILD_LINK, ISO_BUNDLE_CUPIDBUILD_LINK,\n"
            "            ISO_PUBLICATION_CUPIDBUILD_LINK, PROMOTED_CUPIDBUILD_LINK,\n"
            "            CANDIDATE_CUPIDBUILD_LINK\n"
            "        ):\n",
            "        if False:\n",
            r"fixed-point source freeze differs",
        ),
        "candidate helper overwrites its checked source list": (
            "bootstrap",
            '    candidate["links"] = links\n    return candidate\n',
            '    candidate["links"] = links\n'
            '    candidate["sources"] = []\n'
            "    return candidate\n",
            r"fixed-point source freeze differs",
        ),
        "candidate helper overwrites its retained five links": (
            "bootstrap",
            '    candidate["links"] = links\n    return candidate\n',
            '    candidate["links"] = links\n'
            '    candidate["links"] = {}\n'
            "    return candidate\n",
            r"fixed-point source freeze differs",
        ),
        "candidate helper drops the checked source copy": (
            "bootstrap",
            "    sources = [\n"
            '        dict(_require_object(source, "build source"))\n'
            "        for source in raw_sources\n"
            "    ]\n",
            "    sources = []\n",
            r"fixed-point source freeze differs",
        ),
        "candidate helper empties the retained five links": (
            "bootstrap",
            "    links = {\n"
            "        name: [\n"
            "            str(item)\n"
            "            for item in _require_list(\n"
            "                raw_links.get(name), "
            'f"build_plan.links.{name}"\n'
            "            )\n"
            "        ]\n"
            "        for name in TOOL_NAMES\n"
            "    }\n",
            "    links = {name: [] for name in TOOL_NAMES}\n",
            r"fixed-point source freeze differs",
        ),
        "candidate source constant is reassigned in a live branch": (
            "bootstrap",
            "CANDIDATE_CUPIDBUILD_LINK = (\n",
            "if True:\n    CANDIDATE_SOURCES = ()\nCANDIDATE_CUPIDBUILD_LINK = (\n",
            r"fixed-point source freeze differs",
        ),
        "candidate source constant drops CupidBuild main": (
            "bootstrap",
            '    ("cupidbuild_main", "/toolchain/cupidbuild_main.cc", False),\n'
            '    ("seed_manifest", "/toolchain/seed_manifest.cc", False),\n',
            '    ("cupidbuild_main", "/toolchain/cupidbuild_main-x.cc", False),\n'
            '    ("seed_manifest", "/toolchain/seed_manifest.cc", False),\n',
            r"fixed-point source freeze differs",
        ),
        "candidate source constant redirects the manifest reader": (
            "bootstrap",
            '    ("seed_manifest", "/toolchain/seed_manifest.cc", False),\n',
            '    ("seed_manifest", "/toolchain/seed_manifest-other.cc", False),\n',
            r"fixed-point source freeze differs",
        ),
        "candidate source constant redirects the release reader": (
            "bootstrap",
            '    ("seed_release", "/toolchain/seed_release.cc", False),\n',
            '    ("seed_release", "/toolchain/seed_release-other.cc", False),\n',
            r"fixed-point source freeze differs",
        ),
        "candidate source constant redirects the shared parser": (
            "bootstrap",
            '    ("contract_parse_internal", "/toolchain/contract_parse_internal.cc", False),\n',
            '    ("contract_parse_internal", "/toolchain/contract_parse_internal-other.cc", False),\n',
            r"fixed-point source freeze differs",
        ),
        "candidate source constant omits cupidbuild_artifacts": (
            "bootstrap",
            '    ("cupidbuild_artifacts", "/toolchain/cupidbuild_artifacts.cc", False),\n',
            "",
            r"fixed-point source freeze differs",
        ),
        "candidate source constant omits artifact_size_policy": (
            "bootstrap",
            '    ("artifact_size_policy", "/toolchain/artifact_size_policy.cc", False),\n',
            "",
            r"fixed-point source freeze differs",
        ),
        "candidate source constant omits user_syscall_abi": (
            "bootstrap",
            '    ("user_syscall_abi", "/toolchain/user_syscall_abi.cc", False),\n',
            "",
            r"fixed-point source freeze differs",
        ),
        "candidate source constant omits cupidbuild_user_abi": (
            "bootstrap",
            '    ("cupidbuild_user_abi", "/toolchain/cupidbuild_user_abi.cc", False),\n',
            "",
            r"fixed-point source freeze differs",
        ),
        "candidate source constant omits guarded ISO inventory": (
            "bootstrap",
            '    ("cupidbuild_iso", "/toolchain/cupidbuild_iso.cc", False),\n',
            "",
            r"fixed-point source freeze differs",
        ),
        "candidate source constant omits guarded ISO capture": (
            "bootstrap",
            '    ("cupidbuild_iso_capture", "/toolchain/cupidbuild_iso_capture.cc", False),\n',
            "",
            r"fixed-point source freeze differs",
        ),
        "candidate source constant omits guarded ISO image checker": (
            "bootstrap",
            '    ("cupidbuild_iso_image", "/toolchain/cupidbuild_iso_image.cc", False),\n',
            "",
            r"fixed-point source freeze differs",
        ),
        "candidate source constant omits guarded ISO publication": (
            "bootstrap",
            '    ("cupidbuild_iso_publication", "/toolchain/cupidbuild_iso_publication.cc", False),\n',
            "",
            r"fixed-point source freeze differs",
        ),
        "public bootstrap defaults to an unfinalized report": (
            "bootstrap",
            "        compare_fixed_point=True,\n",
            "        compare_fixed_point=False,\n",
            r"fixed-point source freeze differs",
        ),
        "pending bootstrap claims final success": (
            "bootstrap",
            '                else "pending-fixed-point-author"\n',
            '                else "pass"\n',
            r"fixed-point source freeze differs",
        ),
        "Linux bootstrap comparison becomes unconditional": (
            "bootstrap",
            "        comparisons = (\n"
            "            _compare_stages("
            "stage_three, stage_four, source_names)\n"
            "            if compare_fixed_point\n"
            "            else None\n"
            "        )\n",
            "        comparisons = _compare_stages(\n"
            "            stage_three, stage_four, source_names\n"
            "        )\n",
            r"fixed-point source freeze differs",
        ),
        "PE32 Windows assembly stops comparing stages": (
            "bootstrap",
            "    windows_assembly_result = _run_stage_pair(\n",
            "    windows_assembly_result = _run_one_stage(\n",
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 Windows compile loses freestanding mode": (
            "bootstrap",
            "    windows_compiler_arguments: list[str | Path] = [\n"
            '        "--root",\n'
            "        behavior_root,\n"
            '        "--freestanding",\n',
            "    windows_compiler_arguments: list[str | Path] = [\n"
            '        "--root",\n'
            "        behavior_root,\n"
            '        "--gnu",\n',
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 Windows link stops comparing stages": (
            "bootstrap",
            "    windows_link_result = _run_stage_pair(\n",
            "    windows_link_result = _run_one_stage(\n",
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 imported image skips validation": (
            "bootstrap",
            "    _validate_static_i386_pe32(\n        stage_two_windows_image,\n",
            "    _skip_static_i386_pe32_validation(\n"
            "        stage_two_windows_image,\n",
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 native probe loses its timeout": (
            "bootstrap",
            "                timeout=10,\n",
            "                timeout=None,\n",
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 native probe accepts stderr": (
            "bootstrap",
            "            or native_result.stderr\n",
            "            or False\n",
            r"fixed-point PE32 behavior differs",
        ),
        "Windows CupidDis stops comparing native output": (
            "bootstrap",
            "            or native_disassembly.stdout "
            "!= reference_disassembly.stdout\n",
            "            or False\n",
            r"fixed-point PE32 behavior differs",
        ),
        "Windows CupidDis accepts the wrong missing-input exit": (
            "bootstrap",
            "            or native_missing.returncode != 1\n",
            "            or False\n",
            r"fixed-point PE32 behavior differs",
        ),
        "Windows runtime contract stops checking output bytes": (
            "bootstrap",
            '            or contract_output.read_bytes() != b"headtail"\n',
            "            or False\n",
            r"fixed-point PE32 behavior differs",
        ),
        "Windows runtime contract accepts the wrong negative exit": (
            "bootstrap",
            "            or native_contract_failure.returncode != 41\n",
            "            or False\n",
            r"fixed-point PE32 behavior differs",
        ),
        "Windows native tools stop comparing successful output": (
            "bootstrap",
            "                or native_output.read_bytes() "
            "!= reference_output.read_bytes()\n",
            "                or False\n",
            r"fixed-point PE32 behavior differs",
        ),
        "Windows native tools stop preserving failure output": (
            "bootstrap",
            "                or failure_output.read_bytes() != sentinel\n",
            "                or False\n",
            r"fixed-point PE32 behavior differs",
        ),
        "Windows native CupidASM drops its publication closure": (
            "bootstrap",
            '        "cupidasm": (\n'
            '            "publication_start",\n'
            '            "cupidasm_main",\n'
            '            "cupidasm",\n'
            '            "ctool_host",\n'
            '            "ctool",\n'
            '            "elf32",\n'
            '            "x86",\n'
            '            "publication_runtime",\n'
            "        ),\n",
            '        "cupidasm": (\n'
            '            "cupidasm_main",\n'
            '            "cupidasm",\n'
            '            "ctool_host",\n'
            '            "ctool",\n'
            '            "elf32",\n'
            '            "x86",\n'
            "        ),\n",
            r"fixed-point PE32 behavior differs",
        ),
        "Windows native CupidASM drops its publication imports": (
            "bootstrap",
            '    if tool_name in ("cupidasm", "cupidld"):\n',
            '    if tool_name == "cupidld":\n',
            r"native Windows fixed-point behavior differs",
        ),
        "Windows CupidLD stops comparing published output": (
            "bootstrap",
            "            or native_cupidld_output.read_bytes()\n"
            "            != stage_two_windows_runtime_contract_image.read_bytes()\n",
            "            or False\n",
            r"fixed-point PE32 behavior differs",
        ),
        "Windows CupidLD stops checking failed-publication cleanup": (
            "bootstrap",
            "            or remaining_blocked_candidates\n",
            "            or False\n",
            r"fixed-point PE32 behavior differs",
        ),
        "Windows publication runtime compiles the wrong source": (
            "bootstrap",
            '            "/toolchain/hosted/i386-windows/'
            'publication_runtime.cc",\n',
            '            "/toolchain/hosted/i386-windows/runtime.cc",\n',
            r"fixed-point PE32 behavior differs",
        ),
        "Windows native tool behavior moves under a dead block": (
            "bootstrap",
            '    if os.name == "nt":\n'
            "        windows_invalid_assembly = behavior_root / ",
            '    if os.name == "nt":\n'
            "        pass\n"
            "    if False:\n"
            "        windows_invalid_assembly = behavior_root / ",
            r"fixed-point PE32 behavior differs",
        ),
        "Windows helper link moves under a dead block": (
            "bootstrap",
            "        return arguments\n\n    link_result = _run_stage_pair(\n",
            "        return arguments\n\n"
            "    if False:\n"
            "        link_result = _run_stage_pair(\n",
            r"fixed-point PE32 behavior differs",
        ),
        "Windows helper uses the wrong platform define": (
            "bootstrap",
            '                "_WIN32=1",\n',
            '                "_WIN64=1",\n',
            r"fixed-point PE32 behavior differs",
        ),
        "Windows CupidDis driver loses its Windows profile": (
            "bootstrap",
            '            "_WIN32=1",\n'
            '            "-c",\n'
            '            "/toolchain/cupiddis_main.cc",\n',
            '            "_WIN64=1",\n'
            '            "-c",\n'
            '            "/toolchain/cupiddis_main.cc",\n',
            r"fixed-point PE32 behavior differs",
        ),
        "Windows CupidDis replacement crosses generations": (
            "bootstrap",
            "    stage_two_windows_cupiddis_replacements = {\n"
            '        "cupiddis_main": stage_two_windows_cupiddis_main,\n',
            "    stage_two_windows_cupiddis_replacements = {\n"
            '        "cupiddis_main": stage_three_windows_cupiddis_main,\n',
            r"fixed-point PE32 behavior differs",
        ),
        "Windows helper links the wrong image format": (
            "bootstrap",
            '            "i386pe",\n',
            '            "elf_i386",  # "i386pe",\n',
            r"fixed-point PE32 behavior differs",
        ),
        "Windows helper stops comparing compiled mains": (
            "bootstrap",
            "            or stage_two_object.read_bytes() "
            "!= stage_three_object.read_bytes()\n",
            "            or False  # stage_two_object.read_bytes() "
            "!= stage_three_object.read_bytes()\n",
            r"fixed-point PE32 behavior differs",
        ),
        "Windows helper stops comparing linked images": (
            "bootstrap",
            "        or stage_two_image.read_bytes() "
            "!= stage_three_image.read_bytes()\n",
            "        or False  # stage_two_image.read_bytes() "
            "!= stage_three_image.read_bytes()\n",
            r"fixed-point PE32 behavior differs",
        ),
        "Windows helper skips platform-sensitive mains": (
            "bootstrap",
            "    replacement_names = windows_sources.get(tool_name, ())\n",
            "    replacement_names = ()\n",
            r"fixed-point PE32 behavior differs",
        ),
        "Windows native image loop moves under a dead block": (
            "bootstrap",
            native_build_loop_feature,
            "    if False:\n"
            + textwrap.indent(native_build_loop_feature, "    "),
            r"fixed-point PE32 behavior differs",
        ),
        "Windows native image loop calls a dead helper": (
            "bootstrap",
            "            _build_windows_tool_image(\n",
            "            _skip_build_windows_tool_image(\n",
            r"fixed-point PE32 behavior differs",
        ),
        "Windows native execution loop becomes empty": (
            "bootstrap",
            "        ) in native_checks.items():\n",
            "        ) in ():\n",
            r"fixed-point PE32 behavior differs",
        ),
        "Windows CupidASM compares its reference with itself": (
            "bootstrap",
            '                behavior_root / "native-cupidasm.bin",\n'
            "                behavior_root / "
            '"native-cupidasm-failure.bin",\n'
            "                stage_two_binary,\n",
            "                stage_two_binary,\n"
            "                behavior_root / "
            '"native-cupidasm-failure.bin",\n'
            "                stage_two_binary,\n",
            r"fixed-point PE32 behavior differs",
        ),
        "Windows CupidDis substitutes help for raw disassembly": (
            "bootstrap",
            "        raw_arguments: list[str | Path] = [\n"
            '            "--raw",\n'
            '            "--mode",\n'
            '            "32",\n'
            '            "--base",\n'
            '            "0",\n'
            "            windows_cupiddis_input,\n"
            "        ]\n",
            '        raw_arguments: list[str | Path] = ["--help"]\n',
            r"fixed-point PE32 behavior differs",
        ),
        "Windows native execution loop skips every tool": (
            "bootstrap",
            "        ) in native_checks.items():\n"
            "            failure_output.write_bytes(sentinel)\n",
            "        ) in native_checks.items():\n"
            "            continue\n"
            "            failure_output.write_bytes(sentinel)\n",
            r"fixed-point PE32 behavior differs",
        ),
        "Windows CupidDis overwrites native evidence": (
            "bootstrap",
            "        if (\n            reference_disassembly.returncode != 0\n",
            "        native_disassembly = reference_disassembly\n"
            "        if (\n"
            "            reference_disassembly.returncode != 0\n",
            r"fixed-point PE32 behavior differs",
        ),
        "Windows native tools overwrite output evidence": (
            "bootstrap",
            "            if (\n                reference_help.returncode != 0\n",
            "            native_output.write_bytes(\n"
            "                reference_output.read_bytes()\n"
            "            )\n"
            "            if (\n"
            "                reference_help.returncode != 0\n",
            r"fixed-point PE32 behavior differs",
        ),
        "Windows runtime contract manufactures output evidence": (
            "bootstrap",
            "        if (\n            native_contract.returncode != 0\n",
            '        contract_output.write_bytes(b"headtail")\n'
            "        if (\n"
            "            native_contract.returncode != 0\n",
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 direct IAT failure stops comparing stages": (
            "bootstrap",
            "    invalid_import_result = _run_stage_pair(\n",
            "    invalid_import_result = _run_one_stage(\n",
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 direct IAT failure loses its diagnostic": (
            "bootstrap",
            '        or "IAT symbols require an absolute zero-addend relocation"\n',
            '        or "IAT relocation differs"\n',
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 direct IAT failure loses the second sentinel": (
            "bootstrap",
            "        or stage_three_invalid_import_image.read_bytes() "
            "!= sentinel\n",
            "        or stage_two_invalid_import_image.read_bytes() != sentinel\n",
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 direct IAT fixture collapses assembler outputs": (
            "bootstrap",
            "            stage_three_invalid_import_object,\n"
            "        ],\n"
            "    )\n"
            "    _expect_status(\n"
            "        invalid_import_assembly_result, 0,",
            "            stage_two_invalid_import_object,\n"
            "        ],\n"
            "    )\n"
            "    _expect_status(\n"
            "        invalid_import_assembly_result, 0,",
            r"fixed-point PE32 behavior differs",
        ),
        "PE32 direct IAT failure uses a stage-specific input": (
            "bootstrap",
            "            invalid_import_object,\n"
            "        ],\n"
            "    )\n"
            "    _expect_status(\n"
            "        invalid_import_result, 1,",
            "            stage_three_invalid_import_object,\n"
            "        ],\n"
            "    )\n"
            "    _expect_status(\n"
            "        invalid_import_result, 1,",
            r"fixed-point PE32 behavior differs",
        ),
        "checked source closure is not frozen": (
            "bootstrap",
            "        source_inputs = freeze_source_inputs(\n",
            "        source_inputs = capture_source_inputs(\n",
            r"fixed-point source freeze differs",
        ),
        "checked stage runs from the live root": (
            "bootstrap",
            "        runner = ToolRunner(private_source_root)\n",
            "        runner = ToolRunner(source_root)\n",
            r"fixed-point source freeze differs",
        ),
        "checked private root aliases the live root": (
            "bootstrap",
            "        private_source_root = source_inputs.root\n",
            "        private_source_root = source_root\n",
            r"fixed-point source freeze differs",
        ),
        "checked stage output leaves the private root": (
            "bootstrap",
            '            private_source_root / "stage-two",\n',
            '            output_root / "stage-two",\n',
            r"fixed-point source freeze differs",
        ),
        "checked stage four uses an older compiler generation": (
            "bootstrap",
            '            private_source_root / "stage-four",\n'
            "            stage_three_producers,\n"
            "            plan,\n",
            '            private_source_root / "stage-four",\n'
            "            stage_two_producers,\n"
            "            plan,\n",
            r"fixed-point source freeze differs",
        ),
        "checked comparison stops one generation early": (
            "bootstrap",
            "            _compare_stages(stage_three, stage_four, source_names)\n",
            "            _compare_stages(stage_two, stage_three, source_names)\n",
            r"fixed-point source freeze differs",
        ),
        "checked behavior stops one generation early": (
            "bootstrap",
            "            private_source_root,\n"
            "            stage_three,\n"
            "            stage_four,\n"
            "            seed_inputs,\n"
            "            behavior_evidence,\n",
            "            private_source_root,\n"
            "            stage_two,\n"
            "            stage_three,\n"
            "            seed_inputs,\n"
            "            behavior_evidence,\n",
            r"fixed-point source freeze differs",
        ),
        "checked publication omits stage four": (
            "bootstrap",
            '    "stage-three",\n    "stage-four",\n    "behavior",\n',
            '    "stage-three",\n    "behavior",\n',
            r"fixed-point source freeze differs",
        ),
        "checked closure boundary rehash disappears": (
            "bootstrap",
            "        require_source_closures(source_inputs, source_root, plan)\n",
            "        require_source_snapshot(\n"
            "            source_root, plan, source_inputs.inventory\n"
            "        )\n",
            r"fixed-point source freeze differs",
        ),
        "checked closure stops rehashing the private root": (
            "bootstrap",
            "    require_frozen_source_snapshot(source_inputs, plan)\n",
            "    require_source_snapshot(\n"
            "        live_source_root, plan, source_inputs.inventory\n"
            "    )\n",
            r"fixed-point source freeze differs",
        ),
        "checked behavior returns to the live root": (
            "bootstrap",
            "        behavior = _run_behavior_checks(\n"
            "            runner,\n"
            "            private_source_root,\n"
            "            private_source_root,",
            "        behavior = _run_behavior_checks(\n"
            "            runner,\n"
            "            source_root,\n"
            "            private_source_root,",
            r"fixed-point source freeze differs",
        ),
        "checked report leaves the private root": (
            "bootstrap",
            '        report_path = private_source_root / "bootstrap-report.json"\n',
            '        report_path = output_root / "bootstrap-report.json"\n',
            r"fixed-point source freeze differs",
        ),
        "checked bundle leaves the private workspace": (
            "bootstrap",
            '        publication_root = private_workspace / "publication"\n',
            '        publication_root = output_root / "publication"\n',
            r"fixed-point source freeze differs",
        ),
        "checked bundle moves public stage paths": (
            "bootstrap",
            "            (private_source_root / name).replace(\n",
            "            (output_root / name).replace(\n",
            r"fixed-point source freeze differs",
        ),
        "checked output bypasses gated publication": (
            "bootstrap",
            "        publish_bootstrap_outputs(publication_root, output_root)\n",
            "        publication_root.replace(output_root)\n",
            r"fixed-point source freeze differs",
        ),
        "native Windows source closure is not frozen": (
            "bootstrap",
            "        source_inputs = freeze_source_inputs(\n"
            "            source_root,\n"
            "            linux_plan,\n",
            "        source_inputs = capture_source_inputs(\n"
            "            source_root,\n"
            "            linux_plan,\n",
            r"fixed-point source freeze differs",
        ),
        "native Windows stage leaves the frozen source root": (
            "bootstrap",
            "        stage_two = _build_windows_stage(\n"
            "            runner,\n"
            "            private_source_root,\n",
            "        stage_two = _build_windows_stage(\n"
            "            runner,\n"
            "            source_root,\n",
            r"fixed-point source freeze differs",
        ),
        "native Windows stage four uses an older compiler generation": (
            "bootstrap",
            '            private_source_root / "stage-four",\n'
            "            stage_three_producers,\n"
            "            native_plan,\n",
            '            private_source_root / "stage-four",\n'
            "            stage_two_producers,\n"
            "            native_plan,\n",
            r"fixed-point source freeze differs",
        ),
        "native Windows comparison stops one generation early": (
            "bootstrap",
            "        comparisons = _compare_windows_stages(\n"
            "            stage_three,\n"
            "            stage_four,\n",
            "        comparisons = _compare_windows_stages(\n"
            "            stage_two,\n"
            "            stage_three,\n",
            r"fixed-point source freeze differs",
        ),
        "native Windows comparison accepts a five-tool candidate": (
            "bootstrap",
            "            CANDIDATE_TOOL_NAMES,\n"
            "        )\n"
            "        if prepare_stages:",
            "            TOOL_NAMES,\n"
            "        )\n"
            "        if prepare_stages:",
            r"fixed-point source freeze differs",
        ),
        "native Windows comparison drops its expected inventory check": (
            "bootstrap",
            "    if tuple(tool_names) != tuple(expected_tool_names):\n",
            "    if tuple(tool_names) == tuple(expected_tool_names):\n",
            r"fixed-point source freeze differs",
        ),
        "native Windows behavior stops one generation early": (
            "bootstrap",
            "        behavior = _run_native_windows_behavior_checks(\n"
            "            runner,\n"
            "            private_source_root,\n"
            "            stage_three,\n"
            "            stage_four,\n"
            "            native_plan,\n"
            "            seed_inputs,\n"
            "            plan_inputs,\n"
            "            source_root,\n"
            "        )\n",
            "        behavior = _run_native_windows_behavior_checks(\n"
            "            runner,\n"
            "            private_source_root,\n"
            "            stage_two,\n"
            "            stage_three,\n"
            "            native_plan,\n"
            "            seed_inputs,\n"
            "            plan_inputs,\n"
            "            source_root,\n"
            "        )\n",
            r"fixed-point source freeze differs",
        ),
        "native Windows behavior keeps the promoted plan identity": (
            "bootstrap",
            "    behavior_seed_inputs = "
            "_retarget_native_windows_behavior_seed(\n"
            "        seed_inputs, _build_plan_sha256(native_plan),\n"
            "        behavior_linux_plan, behavior_source_snapshot,\n"
            "        utf8=behavior_utf8, long_paths=behavior_long_paths,\n"
            "        user_link_aliases=behavior_user_link_aliases,\n"
            "        observer_workers=behavior_profile.observer_workers,\n"
            "        parent_plan_seed=linux_seed_inputs,\n"
            "    )\n",
            "    behavior_seed_inputs = seed_inputs\n",
            r"native Windows fixed-point behavior differs",
        ),
        "native Windows closure boundary rehash disappears": (
            "bootstrap",
            "        require_source_closures(\n"
            "            source_inputs, source_root, linux_plan\n"
            "        )\n",
            "        require_source_snapshot(\n"
            "            source_root, linux_plan, source_inputs.inventory\n"
            "        )\n",
            r"fixed-point source freeze differs",
        ),
        "native Windows behavior leaves the frozen source root": (
            "bootstrap",
            "        behavior = _run_native_windows_behavior_checks(\n"
            "            runner,\n"
            "            private_source_root,\n",
            "        behavior = _run_native_windows_behavior_checks(\n"
            "            runner,\n"
            "            source_root,\n",
            r"fixed-point source freeze differs",
        ),
        "native Windows report loses its schema": (
            "bootstrap",
            '            "schema": WINDOWS_REPORT_SCHEMA,\n',
            '            "schema": REPORT_SCHEMA,\n',
            r"fixed-point source freeze differs",
        ),
        "native Windows plan reuses the Linux runtime": (
            "bootstrap",
            '            path = "/toolchain/hosted/i386-windows/runtime.cc"\n',
            '            path = "/toolchain/hosted/i386-linux/runtime.cc"\n',
            r"native Windows fixed-point behavior differs",
        ),
        "native Windows CupidDis loses its Windows profile": (
            "bootstrap",
            '        "cupidc_main",\n'
            '        "cupiddis_main",\n'
            '        "cupidld_main",\n',
            '        "cupidc_main",\n        "cupidld_main",\n',
            r"fixed-point source freeze differs",
        ),
        "native Windows includes bypass the Linux plan": (
            "bootstrap",
            '            linux_plan.get("include_arguments"),\n',
            "            EXPECTED_INCLUDE_ARGUMENTS,\n",
            r"native Windows fixed-point behavior differs",
        ),
        "native Windows links bypass the Linux plan": (
            "bootstrap",
            "        native_order = list(linux_order)\n",
            "        native_order = list(EXPECTED_LINKS[tool_name])\n",
            r"native Windows fixed-point behavior differs",
        ),
        "native Windows linker bypasses the derived order": (
            "bootstrap",
            "    arguments.extend(objects[name] for name in link_order)\n",
            "    arguments.extend(\n"
            "        objects[name] for name in EXPECTED_LINKS[tool_name]\n"
            "    )\n",
            r"native Windows fixed-point behavior differs",
        ),
        "native Windows stage skips PE validation": (
            "bootstrap",
            "        _validate_static_i386_pe32(\n            executable,\n",
            "        _skip_static_i386_pe32_validation(\n            executable,\n",
            r"native Windows fixed-point behavior differs",
        ),
        "native Windows failure runs one stage": (
            "bootstrap",
            "        failure_result = _run_stage_pair(\n"
            "            runner,\n"
            "            stage_two,\n"
            "            stage_three,\n"
            "            tool_name,\n"
            '            ["--definitely-invalid-option"],\n',
            "        failure_result = _run_one_stage(\n"
            "            runner,\n"
            "            stage_two,\n"
            "            stage_three,\n"
            "            tool_name,\n"
            '            ["--definitely-invalid-option"],\n',
            r"native Windows fixed-point behavior differs",
        ),
        "native Windows tool comparison disappears": (
            "bootstrap",
            "            stage_three.tools[name].read_bytes()\n"
            "            != stage_four.tools[name].read_bytes()\n",
            "            stage_three.tools[name].read_bytes()\n"
            "            != stage_three.tools[name].read_bytes()\n",
            r"native Windows fixed-point behavior differs",
        ),
        "native Windows relink moves under a dead block": (
            "bootstrap",
            '            raw_links.get("cupidasm"),\n'
            '            "Windows build plan links: cupidasm",\n'
            "        )\n"
            "    ]\n"
            "    link_result = _run_stage_pair(\n",
            '            raw_links.get("cupidasm"),\n'
            '            "Windows build plan links: cupidasm",\n'
            "        )\n"
            "    ]\n"
            "    if False:\n"
            "        link_result = _run_stage_pair(\n",
            r"native Windows fixed-point behavior differs",
        ),
        "behavior evidence relabels generation four as generation two": (
            "bootstrap",
            'f"stage-four-windows-{tool_name}-{source_name}.o"',
            'f"stage-two-windows-{tool_name}-{source_name}.o"',
            r"fixed-point PE32 behavior differs",
        ),
    }
