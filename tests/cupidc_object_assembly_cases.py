## @file
#  @brief Check inline assembly and byte-compare active kernel sources.
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path

from tests.c_source_spelling import _c_spelling_tokens, c_block
from tests.cupidc_object_fixtures import (
    KERNEL_SOURCE_SHA256, KERNEL_SOURCE_SIZE, LIBM_SOURCE_SHA256, LIBM_SOURCE_SIZE,
    REPO_ROOT, TOOLCHAIN_ROOT,
)
from tools.cupidc_kernel_compile import validate_i386_relocatable_bytes

class ObjectAssemblyCases:
    def test_inline_assembly_preserves_legacy_register_and_tied_input_bytes(
        self,
    ):
        result = subprocess.run(
            [
                str(self.contract_path),
                "inline-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "inline-assembly: ok\n")

    def test_pointer_output_assembly_emits_segmented_i386_load(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "pointer-output-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "pointer-output-assembly: ok\n")

    def test_register_snapshot_and_flags_restore_emit_exact_i386_state(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "register-snapshot-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "register-snapshot-assembly: ok\n")

    def test_call_next_assembly_emits_exact_i386_state_read(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "call-next-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "call-next-assembly: ok\n")

    def test_port_io_assembly_emits_exact_i386_and_preserves_callee_registers(
        self,
    ):
        result = subprocess.run(
            [
                str(self.contract_path),
                "port-io-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "port-io-assembly: ok\n")

    def test_privileged_register_assembly_emits_exact_i386(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "privileged-register-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout, "privileged-register-assembly: ok\n"
        )

    def test_fxsave_assembly_emits_exact_i386_memory_instruction(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "fxsave-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "fxsave-assembly: ok\n")

    def test_ldmxcsr_memory_input_emits_exact_i386_instruction(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "ldmxcsr-memory-input",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "ldmxcsr-memory-input: ok\n")

    def test_movss_memory_assembly_emits_exact_i386_instructions(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "movss-memory-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "movss-memory-assembly: ok\n")

    def test_kernel_simd_assembly_emits_exact_i386_instructions(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "kernel-simd-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "kernel-simd-assembly: ok\n")

    def test_x87_sine_memory_assembly_emits_balanced_exact_i386_sequence(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "x87-sine-memory-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "x87-sine-memory-assembly: ok\n")

    def test_x87_round_down_memory_assembly_emits_exact_control_sequence(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "x87-round-down-memory-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "x87-round-down-memory-assembly: ok\n")

    def test_x87_pow_memory_assembly_emits_balanced_exact_i386_sequence(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "x87-pow-memory-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "x87-pow-memory-assembly: ok\n")

    def test_x87_powf_memory_assembly_emits_mixed_width_i386_sequence(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "x87-powf-memory-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "x87-powf-memory-assembly: ok\n")

    def test_sqrtsd_assembly_emits_exact_shared_model_sequence(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "sqrtsd-register-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "sqrtsd-register-assembly: ok\n")

    def test_x87_atan2_assembly_emits_exact_shared_model_sequence(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "x87-atan2-memory-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "x87-atan2-memory-assembly: ok\n")

    def test_x87_exp_assembly_emits_exact_shared_model_sequence(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "x87-exp-memory-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "x87-exp-memory-assembly: ok\n")

    def test_descriptor_table_assembly_emits_exact_segment_transitions(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "descriptor-table-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "descriptor-table-assembly: ok\n")

    def test_unchanged_percpu_source_emits_a_deterministic_object(self):
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        contract = audit["contracts"]["c_preprocessor_translation_units"]
        profile = next(
            item
            for item in contract["profiles"]
            if item["name"] == "KERNEL_I386"
        )
        self.assertTrue(profile["gnu_extensions"])
        self.assertFalse(profile["hosted_environment"])
        self.assertFalse(profile["implicit_function_declarations"])
        self.assertFalse(profile["compatibility_pointer_conversions"])
        self.assertEqual(profile["forced_includes"], [])

        source_path = REPO_ROOT / "kernel/smp/percpu.cc"
        source_image = source_path.read_bytes()
        source_digest = hashlib.sha256(source_image).hexdigest()
        self.assertEqual(
            (len(source_image), source_digest),
            (
                5303,
                'e99c8298916b8dfd34b90f45a924443403f6a88d836ef1282abfb46d6202ecb1',
            ),
            (
                "percpu source lock changed: "
                f"{len(source_image)} {source_digest}"
            ),
        )

        arguments = [
            "--root",
            str(REPO_ROOT),
            "--gnu",
            "--freestanding",
        ]
        both_forms = (
            "(CTOOL_C_PP_INCLUDE_QUOTED | "
            "CTOOL_C_PP_INCLUDE_ANGLE)"
        )
        for include_root in profile["include_roots"]:
            self.assertEqual(include_root["forms"], both_forms)
            arguments.extend(["-I", include_root["path"]])
        for action in profile["macro_actions"]:
            if action["name"] == "__SIZEOF_POINTER__":
                self.assertEqual(action["replacement"], "4")
                continue
            arguments.append(
                "-D" + action["name"] + "=" + action["replacement"]
            )

        with tempfile.TemporaryDirectory(
            prefix=".cupidc-percpu-frontier-", dir=REPO_ROOT
        ) as temp:
            output_root = Path(temp)
            objects = []
            for index in range(2):
                output = output_root / f"percpu-{index}.o"
                logical_output = "/" + output.relative_to(
                    REPO_ROOT
                ).as_posix()
                result = subprocess.run(
                    [
                        str(self.hosted_cupidc_path),
                        *arguments,
                        "-c",
                        "/kernel/smp/percpu.cc",
                        "-o",
                        logical_output,
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=180,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertEqual(result.stderr, "")
                self.assertTrue(output.is_file())
                image = output.read_bytes()
                validate_i386_relocatable_bytes(image)
                objects.append(image)
            self.assertEqual(objects[0], objects[1])
            digest = hashlib.sha256(objects[0]).hexdigest()
            self.assertEqual(
                (len(objects[0]), digest),
                (
                    6632,
                    "c9ccaa9a23a5e1f9b3076ca81c2971a9"
                    "02fa85b7ae78a409ec37e7b93253f956",
                ),
                (
                    "percpu object lock changed: "
                    f"{len(objects[0])} {digest}"
                ),
            )

    def test_unchanged_fpu_source_emits_a_deterministic_object(self):
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        contract = audit["contracts"]["c_preprocessor_translation_units"]
        profile = next(
            item
            for item in contract["profiles"]
            if item["name"] == "KERNEL_I386"
        )
        self.assertTrue(profile["gnu_extensions"])
        self.assertFalse(profile["hosted_environment"])
        self.assertFalse(profile["implicit_function_declarations"])
        self.assertFalse(profile["compatibility_pointer_conversions"])
        self.assertEqual(profile["forced_includes"], [])

        arguments = [
            "--root",
            str(REPO_ROOT),
            "--gnu",
            "--freestanding",
        ]
        both_forms = (
            "(CTOOL_C_PP_INCLUDE_QUOTED | "
            "CTOOL_C_PP_INCLUDE_ANGLE)"
        )
        for include_root in profile["include_roots"]:
            self.assertEqual(include_root["forms"], both_forms)
            arguments.extend(["-I", include_root["path"]])
        for action in profile["macro_actions"]:
            if action["name"] == "__SIZEOF_POINTER__":
                self.assertEqual(action["replacement"], "4")
                continue
            arguments.append(
                "-D" + action["name"] + "=" + action["replacement"]
            )

        with tempfile.TemporaryDirectory(
            prefix=".cupidc-fpu-frontier-", dir=REPO_ROOT
        ) as temp:
            output_root = Path(temp)
            objects = []
            for index in range(2):
                output = output_root / f"fpu-{index}.o"
                logical_output = "/" + output.relative_to(
                    REPO_ROOT
                ).as_posix()
                result = subprocess.run(
                    [
                        str(self.hosted_cupidc_path),
                        *arguments,
                        "-c",
                        "/kernel/cpu/fpu.cc",
                        "-o",
                        logical_output,
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=180,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertEqual(result.stderr, "")
                self.assertTrue(output.is_file())
                image = output.read_bytes()
                self.assertEqual(image[:7], b"\x7fELF\x01\x01\x01")
                self.assertEqual(int.from_bytes(image[16:18], "little"), 1)
                self.assertEqual(int.from_bytes(image[18:20], "little"), 3)
                objects.append(image)
            self.assertEqual(objects[0], objects[1])
            digest = hashlib.sha256(objects[0]).hexdigest()
            self.assertEqual(
                (len(objects[0]), digest),
                (
                    6484,
                    "d732a346a4db31320379bfc7f48d1056"
                    "bd5a37e164d33588b3c3b7ca9490a622",
                ),
                f"FPU object lock changed: {len(objects[0])} {digest}",
            )
            policy = subprocess.run(
                [
                    str(self.contract_path),
                    "fpu-init-codegen-policy",
                    str(REPO_ROOT),
                    logical_output,
                ],
                cwd=TOOLCHAIN_ROOT,
                text=True,
                capture_output=True,
                timeout=30,
            )
            self.assertEqual(policy.returncode, 0, policy.stderr)
            self.assertEqual(
                policy.stdout,
                "fpu-init-codegen-policy: ok\n",
            )
            invalid_image = bytearray(objects[0])
            cr4_write = b"\x0f\x22\xe0"
            self.assertEqual(invalid_image.count(cr4_write), 1)
            cr4_offset = invalid_image.index(cr4_write)
            invalid_image[cr4_offset : cr4_offset + len(cr4_write)] = (
                b"\x90\x90\x90"
            )
            invalid_output = output_root / "fpu-without-cr4-enable.o"
            invalid_output.write_bytes(invalid_image)
            invalid_logical_output = "/" + invalid_output.relative_to(
                REPO_ROOT
            ).as_posix()
            rejected = subprocess.run(
                [
                    str(self.contract_path),
                    "fpu-init-codegen-policy",
                    str(REPO_ROOT),
                    invalid_logical_output,
                ],
                cwd=TOOLCHAIN_ROOT,
                text=True,
                capture_output=True,
                timeout=30,
            )
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn(
                "FPU work appears before the CR4 enable",
                rejected.stderr,
            )

    def test_active_str_floor_block_emits_a_deterministic_object(self):
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        contract = audit["contracts"]["c_preprocessor_translation_units"]
        profile = next(
            item
            for item in contract["profiles"]
            if item["name"] == "KERNEL_I386"
        )
        arguments = [
            "--root",
            str(REPO_ROOT),
            "--gnu",
            "--freestanding",
        ]
        for include_root in profile["include_roots"]:
            arguments.extend(["-I", include_root["path"]])
        for action in profile["macro_actions"]:
            if action["name"] == "__SIZEOF_POINTER__":
                continue
            arguments.append(
                "-D" + action["name"] + "=" + action["replacement"]
            )

        active_source = (
            REPO_ROOT / "kernel/core/string.cc"
        ).read_text(encoding="utf-8")
        start, end = c_block(active_source, "static double str_floor(double x)")
        floor_source = active_source[start:end] + "\n"
        tokens = [value for value, _start, _end in _c_spelling_tokens(floor_source)]
        self.assertEqual(
            hashlib.sha256(json.dumps(tokens, separators=(",", ":")).encode()).hexdigest(),
            "363cd4284b53b496df0baf41050b7cf3c3a816878b81e8f439079caef48be6af",
        )
        self.assertIn(': "ax", "memory");', floor_source)

        with tempfile.TemporaryDirectory(
            prefix=".cupidc-active-str-floor-", dir=REPO_ROOT
        ) as temp:
            output_root = Path(temp)
            source = output_root / "str_floor.c"
            source.write_text(floor_source, encoding="utf-8", newline="\n")
            logical_source = "/" + source.relative_to(
                REPO_ROOT
            ).as_posix()
            objects = []
            for index in range(2):
                output = output_root / f"str-floor-{index}.o"
                logical_output = "/" + output.relative_to(
                    REPO_ROOT
                ).as_posix()
                result = subprocess.run(
                    [
                        str(self.hosted_cupidc_path),
                        *arguments,
                        "-c",
                        logical_source,
                        "-o",
                        logical_output,
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=180,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertEqual(result.stderr, "")
                self.assertTrue(output.is_file())
                image = output.read_bytes()
                self.assertEqual(image[:7], b"\x7fELF\x01\x01\x01")
                self.assertEqual(int.from_bytes(image[16:18], "little"), 1)
                self.assertEqual(int.from_bytes(image[18:20], "little"), 3)
                objects.append(image)
            self.assertEqual(objects[0], objects[1])
            digest = hashlib.sha256(objects[0]).hexdigest()
            self.assertEqual(
                (len(objects[0]), digest),
                (
                    420,
                    "448012fe57ec625c6075e97cf91163b994a0443238c5d6bdf"
                    "25e4b839763f14e",
                ),
                (
                    "active str_floor object lock changed: "
                    f"{len(objects[0])} {digest}"
                ),
            )

    def test_unchanged_string_source_emits_a_deterministic_object(self):
        source = REPO_ROOT / "kernel/core/string.cc"
        source_bytes = source.read_bytes()
        self.assertEqual(len(source_bytes), 9247)
        self.assertEqual(source_bytes.count(b"\n"), 374)
        self.assertEqual(
            hashlib.sha256(source_bytes).hexdigest(),
            'd334e6ed1938ba89f3c9ecf861f6a82e044ca107c6305256cb527aa84c88a144',
        )
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        contract = audit["contracts"]["c_preprocessor_translation_units"]
        profile = next(
            item
            for item in contract["profiles"]
            if item["name"] == "KERNEL_I386"
        )
        arguments = [
            "--root",
            str(REPO_ROOT),
            "--gnu",
            "--freestanding",
        ]
        for include_root in profile["include_roots"]:
            arguments.extend(["-I", include_root["path"]])
        for action in profile["macro_actions"]:
            if action["name"] == "__SIZEOF_POINTER__":
                continue
            arguments.append(
                "-D" + action["name"] + "=" + action["replacement"]
            )
        with tempfile.TemporaryDirectory(
            prefix=".cupidc-string-frontier-", dir=REPO_ROOT
        ) as temp:
            output_root = Path(temp)
            objects = []
            for index in range(2):
                output = output_root / f"string-{index}.o"
                logical_output = "/" + output.relative_to(
                    REPO_ROOT
                ).as_posix()
                result = subprocess.run(
                    [
                        str(self.hosted_cupidc_path),
                        *arguments,
                        "-c",
                        "/kernel/core/string.cc",
                        "-o",
                        logical_output,
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=180,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertEqual(result.stderr, "")
                self.assertTrue(output.is_file())
                image = output.read_bytes()
                self.assertEqual(image[:7], b"\x7fELF\x01\x01\x01")
                self.assertEqual(int.from_bytes(image[16:18], "little"), 1)
                self.assertEqual(int.from_bytes(image[18:20], "little"), 3)
                objects.append(image)
            self.assertEqual(objects[0], objects[1])
            digest = hashlib.sha256(objects[0]).hexdigest()
            self.assertEqual(
                (len(objects[0]), digest),
                (
                    13820,
                    "b748de8c4a5f1f5ab4a15c567686d989"
                    "86bf62ad6014eba8c703d5053f579a7d",
                ),
                (
                    "unchanged string object lock changed: "
                    f"{len(objects[0])} {digest}"
                ),
            )

    def test_unchanged_kernel_source_emits_a_deterministic_object(self):
        source = REPO_ROOT / "kernel/core/kernel.cc"
        source_bytes = source.read_bytes()
        self.assertEqual(len(source_bytes), KERNEL_SOURCE_SIZE)
        self.assertEqual(source_bytes.count(b"\n"), 968)
        self.assertEqual(
            hashlib.sha256(source_bytes).hexdigest(),
            KERNEL_SOURCE_SHA256,
        )
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        contract = audit["contracts"]["c_preprocessor_translation_units"]
        profile = next(
            item
            for item in contract["profiles"]
            if item["name"] == "KERNEL_I386"
        )
        arguments = [
            "--root",
            str(REPO_ROOT),
            "--gnu",
            "--freestanding",
        ]
        both_forms = (
            "(CTOOL_C_PP_INCLUDE_QUOTED | "
            "CTOOL_C_PP_INCLUDE_ANGLE)"
        )
        for include_root in profile["include_roots"]:
            self.assertEqual(include_root["forms"], both_forms)
            arguments.extend(["-I", include_root["path"]])
        for action in profile["macro_actions"]:
            if action["name"] == "__SIZEOF_POINTER__":
                self.assertEqual(action["replacement"], "4")
                continue
            arguments.append(
                "-D" + action["name"] + "=" + action["replacement"]
            )

        with tempfile.TemporaryDirectory(
            prefix=".cupidc-kernel-frontier-", dir=REPO_ROOT
        ) as temp:
            output_root = Path(temp)
            objects = []
            for index in range(2):
                output = output_root / f"kernel-{index}.o"
                logical_output = "/" + output.relative_to(
                    REPO_ROOT
                ).as_posix()
                result = subprocess.run(
                    [
                        str(self.hosted_cupidc_path),
                        *arguments,
                        "-c",
                        "/kernel/core/kernel.cc",
                        "-o",
                        logical_output,
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=180,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertEqual(result.stderr, "")
                self.assertTrue(output.is_file())
                image = output.read_bytes()
                validate_i386_relocatable_bytes(image)
                objects.append(image)
            self.assertEqual(objects[0], objects[1])
            digest = hashlib.sha256(objects[0]).hexdigest()
            self.assertEqual(
                (len(objects[0]), digest),
                (
                    25968,
                    '745ea24c6b51659270b61476d817f7a6fd923b2355e966b872407cec47e7601e',
                ),
                (
                    "kernel object lock changed: "
                    f"{len(objects[0])} {digest}"
                ),
            )

    def test_active_libm_source_emits_a_deterministic_object(self):
        source = REPO_ROOT / "kernel/cpu/libm.cc"
        source_bytes = source.read_bytes()
        self.assertEqual(len(source_bytes), LIBM_SOURCE_SIZE)
        self.assertEqual(source_bytes.count(b"\n"), 1376)
        self.assertEqual(
            hashlib.sha256(source_bytes).hexdigest(),
            LIBM_SOURCE_SHA256,
        )
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        contract = audit["contracts"]["c_preprocessor_translation_units"]
        profile = next(
            item
            for item in contract["profiles"]
            if item["name"] == "KERNEL_I386"
        )
        arguments = [
            "--root",
            str(REPO_ROOT),
            "--gnu",
            "--freestanding",
        ]
        both_forms = (
            "(CTOOL_C_PP_INCLUDE_QUOTED | "
            "CTOOL_C_PP_INCLUDE_ANGLE)"
        )
        for include_root in profile["include_roots"]:
            self.assertEqual(include_root["forms"], both_forms)
            arguments.extend(["-I", include_root["path"]])
        for action in profile["macro_actions"]:
            if action["name"] == "__SIZEOF_POINTER__":
                self.assertEqual(action["replacement"], "4")
                continue
            arguments.append(
                "-D" + action["name"] + "=" + action["replacement"]
            )

        with tempfile.TemporaryDirectory(
            prefix=".cupidc-libm-frontier-", dir=REPO_ROOT
        ) as temp:
            output_root = Path(temp)
            objects = []
            for index in range(2):
                output = output_root / f"libm-{index}.o"
                logical_output = "/" + output.relative_to(
                    REPO_ROOT
                ).as_posix()
                result = subprocess.run(
                    [
                        str(self.hosted_cupidc_path),
                        *arguments,
                        "-c",
                        "/kernel/cpu/libm.cc",
                        "-o",
                        logical_output,
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=180,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertEqual(result.stderr, "")
                self.assertTrue(output.is_file())
                image = output.read_bytes()
                validate_i386_relocatable_bytes(image)
                objects.append(image)
            self.assertEqual(objects[0], objects[1])
            digest = hashlib.sha256(objects[0]).hexdigest()
            self.assertEqual(
                (len(objects[0]), digest),
                (
                    16004,
                    "91cb378447c48e9aac0d94ea858257b2"
                    "d5c44540ea77a78ed8abfb1ae83bcf36",
                ),
                (
                    "active libm object lock changed: "
                    f"{len(objects[0])} {digest}"
                ),
            )
        self.assertEqual(source.read_bytes(), source_bytes)

    def test_legacy_port_constraints_emit_through_dx(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "legacy-port-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "legacy-port-assembly: ok\n")

    def test_machine_state_memory_outputs_emit_exact_i386_instructions(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "state-memory-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "state-memory-assembly: ok\n")

    def test_atomic_builtins_emit_width_correct_i386_instructions(self):
        result = subprocess.run(
            [str(self.contract_path), "atomic-builtins", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "atomic-builtins: ok\n")

    def test_operand_free_and_empty_barrier_assembly_emits_exact_i386(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "operand-free-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "operand-free-assembly: ok\n")

    def test_kernel_start_assembly_emits_exact_i386_and_relocations(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "kernel-start-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "kernel-start-assembly: ok\n")

    def test_file_scope_math_rounding_and_fmod_assembly_emits_exact_i386(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "file-scope-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "file-scope-assembly: ok\n")

    def test_fabs_masks_and_wrappers_emit_exact_relocatable_i386(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "file-scope-fabs-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "file-scope-fabs-assembly: ok\n",
        )

    def test_exp_log_constants_and_wrappers_emit_exact_relocatable_i386(
        self,
    ):
        result = subprocess.run(
            [
                str(self.contract_path),
                "file-scope-exp-log-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "file-scope-exp-log-assembly: ok\n",
        )

    def test_libm_cdecl_bridges_emit_exact_relocatable_i386(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "file-scope-cdecl-bridges",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "file-scope-cdecl-bridges: ok\n",
        )

    def test_dglibc_file_scope_jumps_emit_exact_i386_functions(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "dglibc-jump-assembly",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "dglibc-jump-assembly: ok\n")

    def test_returns_twice_calls_preserve_live_expression_operands(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "returns-twice-calls",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "returns-twice-calls: ok\n")

    def test_naked_ipi_wrappers_emit_exact_i386_without_a_c_frame(self):
        result = subprocess.run(
            [str(self.contract_path), "naked-functions", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "naked-functions: ok\n")

    def test_unchanged_smp_source_emits_a_deterministic_object(self):
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        contract = audit["contracts"]["c_preprocessor_translation_units"]
        profile = next(
            item
            for item in contract["profiles"]
            if item["name"] == "KERNEL_I386"
        )
        self.assertTrue(profile["gnu_extensions"])
        self.assertFalse(profile["hosted_environment"])
        self.assertFalse(profile["implicit_function_declarations"])
        self.assertFalse(profile["compatibility_pointer_conversions"])
        self.assertEqual(profile["forced_includes"], [])

        arguments = [
            "--root",
            str(REPO_ROOT),
            "--gnu",
            "--freestanding",
        ]
        both_forms = (
            "(CTOOL_C_PP_INCLUDE_QUOTED | "
            "CTOOL_C_PP_INCLUDE_ANGLE)"
        )
        for include_root in profile["include_roots"]:
            self.assertEqual(include_root["forms"], both_forms)
            arguments.extend(["-I", include_root["path"]])
        for action in profile["macro_actions"]:
            if action["name"] == "__SIZEOF_POINTER__":
                self.assertEqual(action["replacement"], "4")
                continue
            arguments.append(
                "-D" + action["name"] + "=" + action["replacement"]
            )

        with tempfile.TemporaryDirectory(
            prefix=".cupidc-smp-frontier-", dir=REPO_ROOT
        ) as temp:
            output_root = Path(temp)
            objects = []
            for index in range(2):
                output = output_root / f"smp-{index}.o"
                logical_output = "/" + output.relative_to(
                    REPO_ROOT
                ).as_posix()
                result = subprocess.run(
                    [
                        str(self.hosted_cupidc_path),
                        *arguments,
                        "-c",
                        "/kernel/smp/smp.cc",
                        "-o",
                        logical_output,
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=180,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertEqual(result.stderr, "")
                self.assertTrue(output.is_file())
                image = output.read_bytes()
                self.assertEqual(image[:7], b"\x7fELF\x01\x01\x01")
                self.assertEqual(int.from_bytes(image[16:18], "little"), 1)
                self.assertEqual(int.from_bytes(image[18:20], "little"), 3)
                objects.append(image)
            self.assertEqual(objects[0], objects[1])
            digest = hashlib.sha256(objects[0]).hexdigest()
            self.assertEqual(
                (len(objects[0]), digest),
                (
                    8032,
                    '2d89990c376bbb0ef122c0b360728cf3eb0d8dd7b7c3d7694b4cf0c3d6824f86',
                ),
                f"SMP object lock changed: {len(objects[0])} {digest}",
            )
