## @file
#  @brief Check scalar, pointer, and aggregate code emission.
import subprocess
import tempfile
from pathlib import Path

from tests.cupidc_object_fixtures import REPO_ROOT, TOOLCHAIN_ROOT
from tools.cupidc_kernel_compile import validate_i386_relocatable_bytes

class ObjectEmissionCases:
    def test_unsigned_rotate_leaves_preserve_types_abi_and_recovery(self):
        result = subprocess.run(
            [str(self.contract_path), "unsigned-rotate-leaves", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT, text=True, capture_output=True, timeout=60,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout, "unsigned-rotate-leaves: ok\n")

    def test_static_definitions_emit_deterministic_elf32_objects(self):
        result = subprocess.run(
            [str(self.contract_path), "static-data", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "static-data: ok\n")

    def test_weak_definitions_emit_weak_elf_symbols(self):
        result = subprocess.run(
            [str(self.contract_path), "weak-symbols", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "weak-symbols: ok\n")

    def test_section_attributes_emit_named_elf32_sections_and_relocations(self):
        result = subprocess.run(
            [str(self.contract_path), "section-attributes", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "section-attributes: ok\n")

    def test_unused_attributes_do_not_change_object_code(self):
        result = subprocess.run(
            [str(self.contract_path), "unused-attributes", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "unused-attributes: ok\n")

    def test_used_attributes_retain_local_elf_definitions(self):
        result = subprocess.run(
            [str(self.contract_path), "used-attributes", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "used-attributes: ok\n")

    def test_function_codegen_attributes_validate_deterministic_emission(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "function-codegen-attributes",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout, "function-codegen-attributes: ok\n"
        )

    def test_static_typed_null_pointer_uses_zero_storage(self):
        result = subprocess.run(
            [str(self.contract_path), "static-typed-null", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "static-typed-null: ok\n")

    def test_direct_goto_emits_resolved_intra_function_branches(self):
        result = subprocess.run(
            [str(self.contract_path), "direct-goto", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "direct-goto: ok\n")

    def test_switch_emits_resolved_dispatch_branches(self):
        result = subprocess.run(
            [str(self.contract_path), "switch-object", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "switch-object: ok\n")

    def test_integer_mutation_emits_deterministic_i386_objects(self):
        result = subprocess.run(
            [str(self.contract_path), "integer-mutation", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "integer-mutation: ok\n")

    def test_object_pointer_values_emit_deterministic_i386_objects(self):
        result = subprocess.run(
            [str(self.contract_path), "pointer-values", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "pointer-values: ok\n")

    def test_object_pointer_comparisons_emit_unsigned_i386_predicates(self):
        result = subprocess.run(
            [str(self.contract_path), "pointer-comparisons", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "pointer-comparisons: ok\n")

    def test_object_pointer_conditions_emit_zero_tests_and_branches(self):
        result = subprocess.run(
            [str(self.contract_path), "pointer-conditions", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "pointer-conditions: ok\n")

    def test_object_pointer_arithmetic_emits_scaled_i386_operations(self):
        result = subprocess.run(
            [str(self.contract_path), "pointer-arithmetic", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "pointer-arithmetic: ok\n")

    def test_function_pointer_calls_emit_deterministic_i386_objects(self):
        result = subprocess.run(
            [str(self.contract_path), "function-pointers", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "function-pointers: ok\n")

    def test_function_pointer_casts_emit_deterministic_i386_objects(self):
        result = subprocess.run(
            [str(self.contract_path), "function-pointer-casts", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "function-pointer-casts: ok\n")

    def test_doom_pointer_compatibility_preserves_bits_through_indirect_calls(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "doom-compatibility-pointers",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "doom-compatibility-pointers: ok\n")

    def test_fixed_automatic_objects_use_target_sized_i386_frames(self):
        result = subprocess.run(
            [str(self.contract_path), "automatic-objects", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "automatic-objects: ok\n")

    def test_hosted_cupidc_probes_each_page_of_large_fixed_frames(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidc-stack-probes-", dir=REPO_ROOT
        ) as temp:
            root = Path(temp)
            source = root / "fixed-frames.cc"
            output = root / "fixed-frames.o"
            source.write_text(
                "unsigned int frame_one_page(void) {\n"
                "  unsigned char bytes[4096];\n"
                "  bytes[0] = 1;\n"
                "  return bytes[4095];\n"
                "}\n"
                "unsigned int frame_one_page_and_word(void) {\n"
                "  unsigned char bytes[4100];\n"
                "  bytes[0] = 2;\n"
                "  return bytes[4099];\n"
                "}\n"
                "unsigned int frame_two_pages(void) {\n"
                "  unsigned char bytes[8192];\n"
                "  bytes[0] = 3;\n"
                "  return bytes[8191];\n"
                "}\n",
                encoding="utf-8",
            )
            compile_result = subprocess.run(
                [
                    str(self.hosted_cupidc_path),
                    "--root",
                    str(root),
                    "--freestanding",
                    "-c",
                    "/fixed-frames.cc",
                    "-o",
                    "/fixed-frames.o",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )
            self.assertEqual(
                compile_result.returncode, 0, compile_result.stderr
            )
            self.assertEqual(compile_result.stdout, "")
            self.assertEqual(compile_result.stderr, "")
            validate_i386_relocatable_bytes(output.read_bytes())

            disassembly = subprocess.run(
                [
                    str(self.hosted_cupiddis_path),
                    "--disassemble",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )
            self.assertEqual(disassembly.returncode, 0, disassembly.stderr)
            self.assertEqual(disassembly.stderr, "")

            def function_listing(name, next_name=None):
                start = disassembly.stdout.index(f"<{name}>:")
                if next_name is None:
                    return disassembly.stdout[start:]
                end = disassembly.stdout.index(f"<{next_name}>:", start)
                return disassembly.stdout[start:end]

            one_page = function_listing(
                "frame_one_page", "frame_one_page_and_word"
            )
            one_page_and_word = function_listing(
                "frame_one_page_and_word", "frame_two_pages"
            )
            two_pages = function_listing("frame_two_pages")
            page_reservation = "sub esp, 0x1000"
            page_probe = "test dword [esp], esp"

            self.assertEqual(one_page.count(page_reservation), 1)
            self.assertNotIn(page_probe, one_page)
            self.assertTrue(
                one_page.startswith(
                    "<frame_one_page>:\n"
                    "00000000:  55  push ebp\n"
                    "00000001:  89 E5  mov ebp, esp\n"
                    "00000003:  81 EC 00 10 00 00  sub esp, 0x1000\n"
                ),
                one_page,
            )
            self.assertEqual(one_page_and_word.count(page_reservation), 1)
            self.assertEqual(
                one_page_and_word.count(page_probe), 2, one_page_and_word
            )
            self.assertIn("sub esp, 0x4", one_page_and_word)
            self.assertEqual(two_pages.count(page_reservation), 2)
            self.assertEqual(two_pages.count(page_probe), 2)

    def test_hosted_cupidc_recovers_after_a_fixed_frame_limit_failure(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidc-stack-probe-recovery-", dir=REPO_ROOT
        ) as temp:
            root = Path(temp)
            source = root / "fixed-frame.cc"
            output = root / "fixed-frame.o"
            sentinel = b"existing object"
            source.write_text(
                "void frame_too_large(void) {\n"
                "  unsigned char bytes[4294967295u];\n"
                "  bytes[0] = 1;\n"
                "}\n",
                encoding="utf-8",
            )
            output.write_bytes(sentinel)
            command = [
                str(self.hosted_cupidc_path),
                "--root",
                str(root),
                "--freestanding",
                "-c",
                "/fixed-frame.cc",
                "-o",
                "/fixed-frame.o",
            ]
            failed = subprocess.run(
                command,
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )
            self.assertEqual(failed.returncode, 1)
            self.assertEqual(failed.stdout, "")
            self.assertIn(
                "CupidC object emission exceeded a configured resource limit",
                failed.stderr,
            )
            self.assertEqual(output.read_bytes(), sentinel)

            source.write_text(
                "unsigned int recovered_frame(void) {\n"
                "  unsigned char bytes[4100];\n"
                "  bytes[0] = 4;\n"
                "  return bytes[4099];\n"
                "}\n",
                encoding="utf-8",
            )
            recovered = subprocess.run(
                command,
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )
            self.assertEqual(recovered.returncode, 0, recovered.stderr)
            self.assertEqual(recovered.stdout, "")
            self.assertEqual(recovered.stderr, "")
            self.assertNotEqual(output.read_bytes(), sentinel)
            validate_i386_relocatable_bytes(output.read_bytes())

    def test_block_extern_objects_emit_one_undefined_linked_symbol(self):
        result = subprocess.run(
            [str(self.contract_path), "block-externs", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "block-externs: ok\n")

    def test_block_function_declarations_match_file_scope_prototypes(self):
        result = subprocess.run(
            [str(self.contract_path), "block-functions", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "block-functions: ok\n")

    def test_block_static_assertions_add_no_runtime_code_or_symbols(self):
        result = subprocess.run(
            [str(self.contract_path), "block-static-asserts", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT, text=True, capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "block-static-asserts: ok\n")

    def test_block_static_assertions_work_in_cupid_built_driver(self):
        linked = self.build_cupid_tools()
        self.assertEqual(linked.returncode, 0, linked.stderr)
        with tempfile.TemporaryDirectory(prefix=".block-assert-driver-", dir=REPO_ROOT) as temp:
            root = Path(temp)
            source = root / "source.cc"
            output = root / "cupid.o"
            source.write_text(
                "int checked(int input) {\n"
                "_Static_assert(sizeof(enum { VALUE = 7 }) == 4, \"enum\");\n"
                "_Static_assert(sizeof(input) == 4, \"parameter\");\n"
                "return input + VALUE; }\n", encoding="utf-8",
            )
            arguments = ["--root", root, "-c", "/source.cc"]
            hosted = subprocess.run(
                [str(self.hosted_cupidc_path), *map(str, arguments), "-o", "/hosted.o"],
                cwd=REPO_ROOT, text=True, capture_output=True, timeout=60,
            )
            cupid = self.run_cupid_linux_tool(
                self.cupid_cupidc_path, [*arguments, "-o", "/cupid.o"], timeout=60,
            )
            self.assertEqual(hosted.returncode, 0, hosted.stderr)
            self.assertEqual(cupid.returncode, 0, cupid.stderr)
            self.assertEqual(output.read_bytes(), (root / "hosted.o").read_bytes())
            before = output.read_bytes()
            source.write_text(
                "void bad(void) {\n_Static_assert(0, \"blocked \" \"layout\");\n}\n",
                encoding="utf-8",
            )
            cupid = self.run_cupid_linux_tool(
                self.cupid_cupidc_path, [*arguments, "-o", "/cupid.o"], timeout=60,
            )
            self.assertNotEqual(cupid.returncode, 0)
            self.assertIn("/source.cc:2:1", cupid.stderr)
            self.assertIn("static assertion failed: blocked layout", cupid.stderr)
            self.assertEqual(output.read_bytes(), before)

    def test_block_enumerators_emit_like_direct_integer_constants(self):
        result = subprocess.run(
            [str(self.contract_path), "block-enums", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "block-enums: ok\n")

    def test_bit_field_assignments_emit_value_preserving_stores(self):
        result = subprocess.run(
            [str(self.contract_path), "bit-field-stores", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "bit-field-stores: ok\n")

    def test_narrow_unsigned_bit_field_promotions_emit_exact_code(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "bit-field-promotions",
                str(REPO_ROOT),
            ],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "bit-field-promotions: ok\n")

    def test_bit_field_mutations_execute_with_single_designator_evaluation(self):
        result = subprocess.run(
            [str(self.contract_path), "bit-field-mutations", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "bit-field-mutations: ok\n")

    def test_block_typedefs_do_not_change_emitted_objects(self):
        result = subprocess.run(
            [str(self.contract_path), "block-typedefs", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "block-typedefs: ok\n")

    def test_automatic_aggregate_initializers_zero_then_store_subobjects(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "aggregate-initializers",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "aggregate-initializers: ok\n")

    def test_union_initializers_emit_one_exact_active_member(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "union-initializers",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "union-initializers: ok\n")

    def test_narrow_integer_mutation_emits_exact_width_i386_stores(self):
        result = subprocess.run(
            [str(self.contract_path), "narrow-mutations", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "narrow-mutations: ok\n")

    def test_narrow_integer_values_use_width_correct_i386_operations(self):
        result = subprocess.run(
            [str(self.contract_path), "narrow-values", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "narrow-values: ok\n")

    def test_explicit_void_casts_emit_exact_discard_code(self):
        result = subprocess.run(
            [str(self.contract_path), "void-casts", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "void-casts: ok\n")
