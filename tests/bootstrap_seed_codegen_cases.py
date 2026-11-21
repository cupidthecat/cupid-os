## @file
#  @brief Check instruction encoding and production object generation.
import hashlib
import json
import os
import shutil
import tempfile
from pathlib import Path

from tools.bootstrap_toolchain import ToolRunner, freeze_seed_inputs

from tests.bootstrap_seed_suite_fixtures import (
    REPO_ROOT,
    SEED_MANIFEST,
)


class BootstrapSeedCodegenCases:
    def test_checked_seed_carries_parity_predicates_and_strict_decode_policy(
        self,
    ):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-strict-dis-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "parity.asm"
            output = root / "parity.bin"
            rejected_source = root / "parity-alias.asm"
            rejected_output = root / "parity-alias.bin"
            truncated = root / "truncated.bin"
            throughput = root / "strict-throughput.bin"
            source.write_text(
                "bits 32\n"
                "setp al\n"
                "setnp byte [eax + 4]\n",
                encoding="utf-8",
                newline="\n",
            )
            rejected_source.write_text(
                "bits 32\nsetpe al\n",
                encoding="utf-8",
                newline="\n",
            )
            rejected_output.write_bytes(b"sentinel")
            truncated.write_bytes(bytes([0x0F]))
            throughput.write_bytes(bytes([0x90]) * (128 * 1024))
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)

            assembled = runner.run(
                frozen.tools["cupidasm"],
                ["-f", "bin", source, "-o", output],
                60,
            )
            self.assertEqual(assembled.returncode, 0, assembled.stderr)
            self.assertEqual(assembled.stdout, "")
            self.assertEqual(assembled.stderr, "")
            self.assertEqual(
                output.read_bytes(), bytes.fromhex("0f9ac00f9b4004")
            )

            strict = runner.run(
                frozen.tools["cupiddis"],
                [
                    "--require-known",
                    "--raw",
                    "--mode=32",
                    "--base=0",
                    output,
                ],
                60,
            )
            self.assertEqual(strict.returncode, 0, strict.stderr)
            self.assertEqual(strict.stdout, "")
            self.assertEqual(strict.stderr, "")

            bounded_strict = runner.run(
                frozen.tools["cupiddis"],
                [
                    "--require-known",
                    "--raw",
                    "--mode=32",
                    "--base=0",
                    throughput,
                ],
                30,
            )
            self.assertEqual(
                bounded_strict.returncode, 0, bounded_strict.stderr
            )
            self.assertEqual(bounded_strict.stdout, "")
            self.assertEqual(bounded_strict.stderr, "")

            disassembled = runner.run(
                frozen.tools["cupiddis"],
                ["--raw", "--mode=32", "--base=0", output],
                60,
            )
            self.assertEqual(
                disassembled.returncode, 0, disassembled.stderr
            )
            self.assertEqual(disassembled.stderr, "")
            rendered = disassembled.stdout.casefold()
            self.assertIn("setp al", rendered)
            self.assertIn("setnp byte", rendered)

            rejected = runner.run(
                frozen.tools["cupidasm"],
                ["-f", "bin", rejected_source, "-o", rejected_output],
                60,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn(
                "unknown Cupid ASM instruction mnemonic", rejected.stderr
            )
            self.assertEqual(rejected_output.read_bytes(), b"sentinel")

            failed_strict = runner.run(
                frozen.tools["cupiddis"],
                [
                    "--require-known",
                    "--raw",
                    "--mode=32",
                    "--base=0",
                    output,
                    truncated,
                ],
                60,
            )
            self.assertEqual(failed_strict.returncode, 1)
            self.assertEqual(failed_strict.stdout, "")
            self.assertIn(
                "truncated.bin: code check failed: "
                "0 known, 0 unknown, 0 invalid, 1 truncated",
                failed_strict.stderr,
            )

    def test_checked_seed_carries_shrd_with_address_overrides(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-shrd-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "shrd.asm"
            output = root / "shrd.bin"
            rejected_source = root / "invalid-shrd.asm"
            rejected_output = root / "invalid-shrd.bin"
            source.write_text(
                "bits 16\n"
                "a32 shrd dword [ebx + 4], esi, 31\n"
                "bits 32\n"
                "a16 shrd word [bx + si + 0x7f], dx, cl\n",
                encoding="utf-8",
                newline="\n",
            )
            rejected_source.write_text(
                "bits 32\nshrd eax, edi, dl\n",
                encoding="utf-8",
                newline="\n",
            )
            rejected_output.write_bytes(b"sentinel")
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)

            assembled = runner.run(
                frozen.tools["cupidasm"],
                ["-f", "bin", source, "-o", output],
                60,
            )
            self.assertEqual(assembled.returncode, 0, assembled.stderr)
            self.assertEqual(assembled.stdout, "")
            self.assertEqual(assembled.stderr, "")
            self.assertEqual(
                output.read_bytes(),
                bytes(
                    [
                        0x66,
                        0x67,
                        0x0F,
                        0xAC,
                        0x73,
                        0x04,
                        0x1F,
                        0x66,
                        0x67,
                        0x0F,
                        0xAD,
                        0x50,
                        0x7F,
                    ]
                ),
            )

            disassembled = runner.run(
                frozen.tools["cupiddis"],
                [
                    "--raw",
                    "--mode=16",
                    "--range-at=7:32",
                    "--base=0",
                    output,
                ],
                60,
            )
            self.assertEqual(
                disassembled.returncode, 0, disassembled.stderr
            )
            self.assertEqual(disassembled.stderr, "")
            rendered = disassembled.stdout.casefold()
            self.assertEqual(rendered.count("shrd"), 2)
            self.assertIn("esi, 0x1f", rendered)
            self.assertIn("dx, cl", rendered)

            rejected = runner.run(
                frozen.tools["cupidasm"],
                ["-f", "bin", rejected_source, "-o", rejected_output],
                60,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn(
                "no x86 form matches the instruction", rejected.stderr
            )
            self.assertEqual(rejected_output.read_bytes(), b"sentinel")

    def test_checked_seed_carries_forward_x87_stack_subtraction(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-x87-subtraction-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "forward.asm"
            output = root / "forward.bin"
            rejected_source = root / "reversed.asm"
            rejected_output = root / "reversed.bin"
            compiler_source = root / "compiler-carriage.cc"
            compiler_object = root / "compiler-carriage.o"
            source.write_text(
                "bits 32\nsection .text\nfsub st1, st0\n",
                encoding="utf-8",
                newline="\n",
            )
            rejected_source.write_text(
                "bits 32\nsection .text\nfsub st0, st1\n",
                encoding="utf-8",
                newline="\n",
            )
            compiler_source.write_text(
                "void corrected(volatile double *out,\n"
                "               const volatile double *x,\n"
                "               const double *log2e) {\n"
                "  __asm__ __volatile__(\n"
                '      "fldl   %[x]\\n\\t"\n'
                '      "fldl   %[log2e]\\n\\t"\n'
                '      "fmulp\\n\\t"\n'
                '      "fld    %%st(0)\\n\\t"\n'
                '      "frndint\\n\\t"\n'
                '      "fsubr  %%st, %%st(1)\\n\\t"\n'
                '      "fxch\\n\\t"\n'
                '      "f2xm1\\n\\t"\n'
                '      "fld1\\n\\t"\n'
                '      "faddp\\n\\t"\n'
                '      "fscale\\n\\t"\n'
                '      "fstp   %%st(1)\\n\\t"\n'
                '      "fstpl  %[out]\\n\\t"\n'
                '      : [out] "=m" (*out)\n'
                '      : [x] "m" (*x), [log2e] "m" (*log2e)\n'
                '      : "memory");\n'
                "}\n"
                "void legacy(volatile double *out,\n"
                "            const volatile double *x,\n"
                "            const double *log2e) {\n"
                "  __asm__ __volatile__(\n"
                '      "fldl   %[x]\\n\\t"\n'
                '      "fldl   %[log2e]\\n\\t"\n'
                '      "fmulp\\n\\t"\n'
                '      "fld    %%st(0)\\n\\t"\n'
                '      "frndint\\n\\t"\n'
                '      "fsub   %%st, %%st(1)\\n\\t"\n'
                '      "fxch\\n\\t"\n'
                '      "f2xm1\\n\\t"\n'
                '      "fld1\\n\\t"\n'
                '      "faddp\\n\\t"\n'
                '      "fscale\\n\\t"\n'
                '      "fstp   %%st(1)\\n\\t"\n'
                '      "fstpl  %[out]\\n\\t"\n'
                '      : [out] "=m" (*out)\n'
                '      : [x] "m" (*x), [log2e] "m" (*log2e)\n'
                '      : "memory");\n'
                "}\n",
                encoding="utf-8",
                newline="\n",
            )
            rejected_output.write_bytes(b"sentinel")
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)

            assembled = runner.run(
                frozen.tools["cupidasm"],
                ["-f", "bin", source, "-o", output],
                60,
            )
            self.assertEqual(assembled.returncode, 0, assembled.stderr)
            self.assertEqual(assembled.stdout, "")
            self.assertEqual(assembled.stderr, "")
            self.assertEqual(output.read_bytes(), bytes([0xDC, 0xE9]))

            disassembled = runner.run(
                frozen.tools["cupiddis"],
                ["--raw", "--mode=32", "--base=0", output],
                60,
            )
            self.assertEqual(
                disassembled.returncode, 0, disassembled.stderr
            )
            self.assertEqual(disassembled.stderr, "")
            self.assertIn("fsub st1, st0", disassembled.stdout.casefold())

            rejected = runner.run(
                frozen.tools["cupidasm"],
                ["-f", "bin", rejected_source, "-o", rejected_output],
                60,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn(
                "no x86 form matches the instruction", rejected.stderr
            )
            self.assertEqual(rejected_output.read_bytes(), b"sentinel")

            compiled = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "--gnu",
                    "--freestanding",
                    "-c",
                    "/" + compiler_source.relative_to(REPO_ROOT).as_posix(),
                    "-o",
                    "/" + compiler_object.relative_to(REPO_ROOT).as_posix(),
                ],
                180,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            self.assertEqual(compiled.stdout, "")
            self.assertEqual(compiled.stderr, "")
            compiler_image = compiler_object.read_bytes()
            self.assertEqual(compiler_image.count(bytes([0xDC, 0xE9])), 1)
            self.assertEqual(compiler_image.count(bytes([0xDC, 0xE1])), 1)

            compiler_disassembly = runner.run(
                frozen.tools["cupiddis"],
                ["--disassemble", compiler_object],
                60,
            )
            self.assertEqual(
                compiler_disassembly.returncode,
                0,
                compiler_disassembly.stderr,
            )
            self.assertEqual(compiler_disassembly.stderr, "")
            rendered = compiler_disassembly.stdout.casefold()
            self.assertEqual(rendered.count("fsub st1, st0"), 1)
            self.assertEqual(rendered.count("fsubr st1, st0"), 1)

    def test_checked_seed_carries_x87_integer_and_long_double_frontier(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-x87-integer-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            assembly_source = root / "x87-integer.asm"
            assembly_output = root / "x87-integer.bin"
            compiler_source = root / "long-double-frontier.cc"
            compiler_object = root / "long-double-frontier.o"
            rejected_compiler_source = root / "invalid-long-double.cc"
            rejected_compiler_output = root / "invalid-long-double.o"
            startup_source = root / "start.asm"
            startup_object = root / "start.o"
            executable = root / "long-double-frontier.elf"
            assembly_source.write_text(
                "bits 32\n"
                "fild word [eax]\n"
                "fild dword [eax]\n"
                "fild qword [eax]\n"
                "fistp word [eax]\n"
                "fistp dword [eax]\n"
                "fistp qword [eax]\n"
                "fldcw word [eax]\n",
                encoding="utf-8",
                newline="\n",
            )
            compiler_source.write_text(
                "static long double widened_specials[6] = {\n"
                "  1.0f / 0.0f, -1.0f / 0.0f, 0.0f / 0.0f,\n"
                "  1.0 / 0.0, -1.0 / 0.0, 0.0 / 0.0\n"
                "};\n"
                "static float narrowed_float_specials[3] = {\n"
                "  (long double)(1.0 / 0.0),\n"
                "  (long double)(-1.0 / 0.0),\n"
                "  (long double)(0.0 / 0.0)\n"
                "};\n"
                "static double narrowed_double_specials[3] = {\n"
                "  (long double)(1.0f / 0.0f),\n"
                "  (long double)(-1.0f / 0.0f),\n"
                "  (long double)(0.0f / 0.0f)\n"
                "};\n"
                "static int special_controls[11] = {\n"
                "  !(long double)(1.0f / 0.0f),\n"
                "  !(long double)(-1.0 / 0.0),\n"
                "  !(long double)(0.0f / 0.0f),\n"
                "  (long double)(1.0f / 0.0f) > 1.0L,\n"
                "  (long double)(-1.0 / 0.0) < -1.0L,\n"
                "  (long double)(0.0 / 0.0) < 0.0L,\n"
                "  (long double)(0.0 / 0.0) <= 0.0L,\n"
                "  (long double)(0.0 / 0.0) > 0.0L,\n"
                "  (long double)(0.0 / 0.0) >= 0.0L,\n"
                "  (long double)(0.0 / 0.0) == 0.0L,\n"
                "  (long double)(0.0 / 0.0) != 0.0L\n"
                "};\n"
                "static long double converted_unsigned_max =\n"
                "    18446744073709551615ull;\n"
                "static long double folded_sum = 1.0L + 2.0L;\n"
                "int seed_long_double_frontier(void) {\n"
                "  const unsigned int *special_words =\n"
                "      (const unsigned int *)widened_specials;\n"
                "  const unsigned int *narrowed_float_words =\n"
                "      (const unsigned int *)narrowed_float_specials;\n"
                "  const unsigned int *narrowed_double_words =\n"
                "      (const unsigned int *)narrowed_double_specials;\n"
                "  const unsigned int *maximum_words =\n"
                "      (const unsigned int *)&converted_unsigned_max;\n"
                "  const unsigned int *folded_words =\n"
                "      (const unsigned int *)&folded_sum;\n"
                "  float updated_float = 1.0f;\n"
                "  double updated_double = 2.0;\n"
                "  float old_float = updated_float++;\n"
                "  double old_double = updated_double--;\n"
                "  short narrow = -123;\n"
                "  int word = -456789;\n"
                "  long long wide = -1234567890123ll;\n"
                "  unsigned long long unsigned_wide =\n"
                "      18446744073709551615ull;\n"
                "  long double narrow_extended = (long double)narrow;\n"
                "  long double word_extended = (long double)word;\n"
                "  long double wide_extended = (long double)wide;\n"
                "  long double unsigned_extended =\n"
                "      (long double)unsigned_wide;\n"
                "  unsigned short control = 0x037fu;\n"
                "  __asm__ volatile(\"fldcw %0\" : : \"m\"(control));\n"
                "  if (special_words[0] != 0u ||\n"
                "      special_words[1] != 0x80000000u ||\n"
                "      special_words[2] != 0x00007fffu ||\n"
                "      special_words[3] != 0u ||\n"
                "      special_words[4] != 0x80000000u ||\n"
                "      special_words[5] != 0x0000ffffu ||\n"
                "      special_words[6] != 0u ||\n"
                "      special_words[7] != 0xc0000000u ||\n"
                "      special_words[8] != 0x00007fffu) return 1;\n"
                "  if (special_words[9] != special_words[0] ||\n"
                "      special_words[10] != special_words[1] ||\n"
                "      special_words[11] != special_words[2] ||\n"
                "      special_words[12] != special_words[3] ||\n"
                "      special_words[13] != special_words[4] ||\n"
                "      special_words[14] != special_words[5] ||\n"
                "      special_words[15] != special_words[6] ||\n"
                "      special_words[16] != special_words[7] ||\n"
                "      special_words[17] != special_words[8]) return 2;\n"
                "  if (narrowed_float_words[0] != 0x7f800000u ||\n"
                "      narrowed_float_words[1] != 0xff800000u ||\n"
                "      narrowed_float_words[2] != 0x7fc00000u) return 3;\n"
                "  if (narrowed_double_words[0] != 0u ||\n"
                "      narrowed_double_words[1] != 0x7ff00000u ||\n"
                "      narrowed_double_words[2] != 0u ||\n"
                "      narrowed_double_words[3] != 0xfff00000u ||\n"
                "      narrowed_double_words[4] != 0u ||\n"
                "      narrowed_double_words[5] != 0x7ff80000u) return 4;\n"
                "  if (special_controls[0] != 0 ||\n"
                "      special_controls[1] != 0 ||\n"
                "      special_controls[2] != 0 ||\n"
                "      special_controls[3] != 1 ||\n"
                "      special_controls[4] != 1 ||\n"
                "      special_controls[5] != 0 ||\n"
                "      special_controls[6] != 0 ||\n"
                "      special_controls[7] != 0 ||\n"
                "      special_controls[8] != 0 ||\n"
                "      special_controls[9] != 0 ||\n"
                "      special_controls[10] != 1) return 5;\n"
                "  if (maximum_words[0] != 0xffffffffu ||\n"
                "      maximum_words[1] != 0xffffffffu ||\n"
                "      maximum_words[2] != 0x0000403eu) return 6;\n"
                "  if ((short)narrow_extended != narrow ||\n"
                "      (int)word_extended != word ||\n"
                "      (long long)wide_extended != wide ||\n"
                "      (unsigned long long)unsigned_extended !=\n"
                "          unsigned_wide) return 7;\n"
                "  if (folded_words[0] != 0u ||\n"
                "      folded_words[1] != 0xc0000000u ||\n"
                "      folded_words[2] != 0x00004000u) return 8;\n"
                "  if (old_float != 1.0f || updated_float != 2.0f ||\n"
                "      ++updated_float != 3.0f || old_double != 2.0 ||\n"
                "      updated_double != 1.0 || --updated_double != 0.0)\n"
                "    return 9;\n"
                "  return 0;\n"
                "}\n",
                encoding="utf-8",
                newline="\n",
            )
            rejected_compiler_source.write_text(
                "void bad(_Atomic float *value) { (*value)++; }\n",
                encoding="utf-8",
                newline="\n",
            )
            rejected_compiler_output.write_bytes(b"sentinel")
            startup_source.write_text(
                "bits 32\n"
                "section .text\n"
                "global _start\n"
                "extern seed_long_double_frontier\n"
                "_start:\n"
                "    call seed_long_double_frontier\n"
                "    mov ebx, eax\n"
                "    mov eax, 1\n"
                "    int 0x80\n",
                encoding="utf-8",
                newline="\n",
            )
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)

            assembled = runner.run(
                frozen.tools["cupidasm"],
                ["-f", "bin", assembly_source, "-o", assembly_output],
                60,
            )
            self.assertEqual(assembled.returncode, 0, assembled.stderr)
            self.assertEqual(assembled.stdout, "")
            self.assertEqual(assembled.stderr, "")
            self.assertEqual(
                assembly_output.read_bytes(),
                bytes.fromhex("df00db00df28df18db18df38d928"),
            )

            disassembled = runner.run(
                frozen.tools["cupiddis"],
                ["--raw", "--mode=32", "--base=0", assembly_output],
                60,
            )
            self.assertEqual(
                disassembled.returncode, 0, disassembled.stderr
            )
            self.assertEqual(disassembled.stderr, "")
            rendered = disassembled.stdout.casefold()
            for instruction in (
                "fild word [eax]",
                "fild dword [eax]",
                "fild qword [eax]",
                "fistp word [eax]",
                "fistp dword [eax]",
                "fistp qword [eax]",
                "fldcw word [eax]",
            ):
                self.assertEqual(rendered.count(instruction), 1)

            rejected_assembly_cases = (
                ("fild-register", "fild eax"),
                ("fistp-byte", "fistp byte [eax]"),
                ("fldcw-dword", "fldcw dword [eax]"),
            )
            for label, instruction in rejected_assembly_cases:
                rejected_source = root / f"invalid-{label}.asm"
                rejected_output = root / f"invalid-{label}.bin"
                rejected_source.write_text(
                    f"bits 32\n{instruction}\n",
                    encoding="utf-8",
                    newline="\n",
                )
                rejected_output.write_bytes(b"sentinel")
                rejected = runner.run(
                    frozen.tools["cupidasm"],
                    [
                        "-f",
                        "bin",
                        rejected_source,
                        "-o",
                        rejected_output,
                    ],
                    60,
                )
                self.assertEqual(rejected.returncode, 1)
                self.assertEqual(rejected.stdout, "")
                self.assertIn(
                    "no x86 form matches the instruction",
                    rejected.stderr,
                )
                self.assertEqual(rejected_output.read_bytes(), b"sentinel")

            compiled = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "--gnu",
                    "--freestanding",
                    "-c",
                    "/" + compiler_source.relative_to(REPO_ROOT).as_posix(),
                    "-o",
                    "/" + compiler_object.relative_to(REPO_ROOT).as_posix(),
                ],
                180,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            self.assertEqual(compiled.stdout, "")
            self.assertEqual(compiled.stderr, "")
            self.assertIn(bytes.fromhex("d928"), compiler_object.read_bytes())

            rejected_compile = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "--gnu",
                    "--freestanding",
                    "-c",
                    "/"
                    + rejected_compiler_source.relative_to(
                        REPO_ROOT
                    ).as_posix(),
                    "-o",
                    "/"
                    + rejected_compiler_output.relative_to(
                        REPO_ROOT
                    ).as_posix(),
                ],
                180,
            )
            self.assertEqual(rejected_compile.returncode, 1)
            self.assertEqual(rejected_compile.stdout, "")
            self.assertIn(
                "atomic floating-point updates are not supported",
                rejected_compile.stderr,
            )
            self.assertEqual(
                rejected_compiler_output.read_bytes(), b"sentinel"
            )

            assembled_start = runner.run(
                frozen.tools["cupidasm"],
                ["-f", "elf32", startup_source, "-o", startup_object],
                60,
            )
            self.assertEqual(
                assembled_start.returncode, 0, assembled_start.stderr
            )
            self.assertEqual(assembled_start.stdout, "")
            self.assertEqual(assembled_start.stderr, "")
            linked = runner.run(
                frozen.tools["cupidld"],
                [
                    "-m",
                    "elf_i386",
                    "--text-address",
                    "0x08048000",
                    "--entry",
                    "_start",
                    "-o",
                    executable,
                    startup_object,
                    compiler_object,
                ],
                180,
            )
            self.assertEqual(linked.returncode, 0, linked.stderr)
            self.assertEqual(linked.stdout, "")
            self.assertEqual(linked.stderr, "")
            executed = runner.run(executable, [], 60)
            self.assertEqual(executed.returncode, 0, executed.stderr)
            self.assertEqual(executed.stdout, "")
            self.assertEqual(executed.stderr, "")

    def test_checked_seed_preserves_returns_twice_call_operands(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-returns-twice-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "returns-twice.cc"
            rejected_source = root / "returns-twice-pointer.cc"
            rejected_output = root / "returns-twice-pointer.o"
            source.write_text(
                "extern int seed_restart(unsigned int env[6]) "
                "__attribute__((returns_twice));\n"
                "int add_after_restart(unsigned int env[6], int left) {\n"
                "  return left + seed_restart(env);\n"
                "}\n",
                encoding="utf-8",
                newline="\n",
            )
            rejected_source.write_text(
                "extern int seed_restart(unsigned int env[6]) "
                "__attribute__((returns_twice));\n"
                "int indirect_restart(unsigned int env[6]) {\n"
                "  int (*saved)(unsigned int env[6]) = seed_restart;\n"
                "  return saved(env);\n"
                "}\n",
                encoding="utf-8",
                newline="\n",
            )
            rejected_output.write_bytes(b"sentinel")
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)
            images = []
            for index in range(2):
                output = root / f"returns-twice-{index}.o"
                compiled = runner.run(
                    frozen.tools["cupidc"],
                    [
                        "--root",
                        REPO_ROOT,
                        "--gnu",
                        "--freestanding",
                        "-c",
                        "/" + source.relative_to(REPO_ROOT).as_posix(),
                        "-o",
                        "/" + output.relative_to(REPO_ROOT).as_posix(),
                    ],
                    180,
                )
                self.assertEqual(compiled.returncode, 0, compiled.stderr)
                self.assertEqual(compiled.stdout, "")
                self.assertEqual(compiled.stderr, "")
                images.append(output.read_bytes())

            self.assertEqual(images[0], images[1])
            self.assertEqual(
                (len(images[0]), hashlib.sha256(images[0]).hexdigest()),
                (
                    492,
                    'cef60b368df3a1a9252118a0694a79ee94c7ef1c9ccc161d2a18e5657ded6c80',
                ),
            )

            rejected = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "--gnu",
                    "--freestanding",
                    "-c",
                    "/"
                    + rejected_source.relative_to(REPO_ROOT).as_posix(),
                    "-o",
                    "/"
                    + rejected_output.relative_to(REPO_ROOT).as_posix(),
                ],
                180,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn(
                "CupidC requires returns_twice functions to be called "
                "directly instead of converted to a function pointer",
                rejected.stderr,
            )
            self.assertEqual(rejected_output.read_bytes(), b"sentinel")

    def test_checked_seed_compiles_links_and_runs_floating_truth(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        source_text = (
            "typedef union { float value; unsigned int bits; } float_box;\n"
            "typedef union {\n"
            "  double value;\n"
            "  struct { unsigned int low; unsigned int high; } words;\n"
            "} double_box;\n"
            "typedef union {\n"
            "  long double value;\n"
            "  struct {\n"
            "    unsigned int significand_low;\n"
            "    unsigned int significand_high;\n"
            "    unsigned int sign_exponent_padding;\n"
            "  } words;\n"
            "} long_box;\n"
            "int seed_floating_truth(void) {\n"
            "  float_box narrow;\n"
            "  double_box wide;\n"
            "  long_box extended;\n"
            "  float narrow_negative_zero;\n"
            "  float narrow_nan;\n"
            "  double wide_negative_zero;\n"
            "  double wide_nan;\n"
            "  _Bool truth;\n"
            "  narrow.bits = 0x80000000u;\n"
            "  narrow_negative_zero = narrow.value;\n"
            "  narrow.bits = 0x7fc00001u;\n"
            "  narrow_nan = narrow.value;\n"
            "  wide.words.low = 0u;\n"
            "  wide.words.high = 0x80000000u;\n"
            "  wide_negative_zero = wide.value;\n"
            "  wide.words.low = 1u;\n"
            "  wide.words.high = 0x7ff80000u;\n"
            "  wide_nan = wide.value;\n"
            "  extended.words.significand_low = 0u;\n"
            "  extended.words.significand_high = 0u;\n"
            "  extended.words.sign_exponent_padding = 0x8000u;\n"
            "  if (narrow_negative_zero || wide_negative_zero ||\n"
            "      extended.value) return 1;\n"
            "  extended.words.significand_low = 1u;\n"
            "  extended.words.significand_high = 0u;\n"
            "  extended.words.sign_exponent_padding = 0u;\n"
            "  if (!narrow_nan || !wide_nan || !extended.value) return 2;\n"
            "  if ((!narrow_negative_zero) != 1 || (!wide_nan) != 0 ||\n"
            "      (!extended.value) != 0) return 3;\n"
            "  truth = narrow_negative_zero;\n"
            "  if (truth != 0) return 4;\n"
            "  truth = wide_nan;\n"
            "  if (truth != 1) return 5;\n"
            "  truth = (_Bool)extended.value;\n"
            "  if (truth != 1) return 6;\n"
            "  if (!(narrow_nan && extended.value) ||\n"
            "      wide_negative_zero) return 7;\n"
            "  if ((wide_nan ? 11 : 13) != 11) return 8;\n"
            "  extended.value = 1.0000000000000000001L;\n"
            "  if (extended.words.significand_low != 1u ||\n"
            "      extended.words.significand_high != 0x80000000u ||\n"
            "      extended.words.sign_exponent_padding != 0x3fffu) return 9;\n"
            "  return 0;\n"
            "}\n"
        )
        start_text = (
            "bits 32\n"
            "section .text\n"
            "global _start\n"
            "extern seed_floating_truth\n"
            "_start:\n"
            "    call seed_floating_truth\n"
            "    mov ebx, eax\n"
            "    mov eax, 1\n"
            "    int 0x80\n"
        )
        atomic_source_text = (
            "int bad(_Atomic float value) { return !value; }\n"
        )
        precise_literal_failure_text = (
            "long double bad(void) { "
            "return 1.00000000000000000001L; }\n"
        )
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-floating-truth-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "floating-truth.cc"
            start = root / "start.asm"
            source.write_text(source_text, encoding="utf-8", newline="\n")
            start.write_text(start_text, encoding="utf-8", newline="\n")
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)
            source_object = root / "floating-truth.o"
            start_object = root / "start.o"
            executable = root / "floating-truth.elf"
            compile_result = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "--gnu",
                    "--freestanding",
                    "-c",
                    "/" + source.relative_to(REPO_ROOT).as_posix(),
                    "-o",
                    "/" + source_object.relative_to(REPO_ROOT).as_posix(),
                ],
                180,
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            self.assertEqual(compile_result.stdout, "")
            self.assertEqual(compile_result.stderr, "")
            assemble_result = runner.run(
                frozen.tools["cupidasm"],
                ["-f", "elf32", start, "-o", start_object],
                120,
            )
            self.assertEqual(
                assemble_result.returncode, 0, assemble_result.stderr
            )
            self.assertEqual(assemble_result.stdout, "")
            self.assertEqual(assemble_result.stderr, "")
            link_result = runner.run(
                frozen.tools["cupidld"],
                [
                    "-m",
                    "elf_i386",
                    "--text-address",
                    "0x08048000",
                    "--entry",
                    "_start",
                    "-o",
                    executable,
                    start_object,
                    source_object,
                ],
                180,
            )
            self.assertEqual(link_result.returncode, 0, link_result.stderr)
            self.assertEqual(link_result.stdout, "")
            self.assertEqual(link_result.stderr, "")
            run_result = runner.run(executable, [], 60)
            self.assertEqual(run_result.returncode, 0, run_result.stderr)
            self.assertEqual(run_result.stdout, "")
            self.assertEqual(run_result.stderr, "")

            atomic_source = root / "atomic-floating-truth.cc"
            atomic_object = root / "atomic-floating-truth.o"
            atomic_source.write_text(
                atomic_source_text, encoding="utf-8", newline="\n"
            )
            atomic_object.write_bytes(b"sentinel")
            atomic_result = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "--gnu",
                    "--freestanding",
                    "-c",
                    "/" + atomic_source.relative_to(REPO_ROOT).as_posix(),
                    "-o",
                    "/" + atomic_object.relative_to(REPO_ROOT).as_posix(),
                ],
                180,
            )
            self.assertEqual(atomic_result.returncode, 1)
            self.assertEqual(atomic_result.stdout, "")
            self.assertIn(
                "atomic floating logical operands are outside this body slice",
                atomic_result.stderr,
            )
            self.assertEqual(atomic_object.read_bytes(), b"sentinel")

            precise_literal_failure = root / "too-precise.cc"
            precise_literal_output = root / "too-precise.o"
            precise_literal_failure.write_text(
                precise_literal_failure_text,
                encoding="utf-8",
                newline="\n",
            )
            precise_literal_output.write_bytes(b"sentinel")
            precise_literal_result = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "--gnu",
                    "--freestanding",
                    "-c",
                    "/"
                    + precise_literal_failure.relative_to(
                        REPO_ROOT
                    ).as_posix(),
                    "-o",
                    "/"
                    + precise_literal_output.relative_to(
                        REPO_ROOT
                    ).as_posix(),
                ],
                180,
            )
            self.assertEqual(precise_literal_result.returncode, 1)
            self.assertEqual(precise_literal_result.stdout, "")
            self.assertIn(
                "decimal floating constant exceeds the supported precision",
                precise_literal_result.stderr,
            )
            self.assertEqual(
                precise_literal_output.read_bytes(), b"sentinel"
            )

    def test_checked_seed_disassembles_typed_raw_ranges_and_legacy_modes(
        self,
    ):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-raw-ranges-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            mixed = root / "typed-ranges.bin"
            mixed.write_bytes(
                bytes(
                    [
                        0xB8,
                        0x34,
                        0x12,
                        0x00,
                        0x00,
                        0x90,
                        0xC3,
                        0xB8,
                        0x78,
                        0x56,
                        0x34,
                        0x12,
                        0xB8,
                        0xCD,
                        0xAB,
                        0xC3,
                    ]
                )
            )
            code_only = root / "legacy-modes.bin"
            code_only.write_bytes(
                bytes(
                    [
                        0xB8,
                        0x34,
                        0x12,
                        0xB8,
                        0x78,
                        0x56,
                        0x34,
                        0x12,
                        0xB8,
                        0xCD,
                        0xAB,
                        0xC3,
                    ]
                )
            )
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)
            typed = runner.run(
                frozen.tools["cupiddis"],
                [
                    "--raw",
                    "--mode=16",
                    "--range-at=3:data",
                    "--range-at=7:32",
                    "--range-at=12:16",
                    "--base=0x7c00",
                    mixed,
                ],
                60,
            )
            self.assertEqual(typed.returncode, 0, typed.stderr)
            self.assertEqual(typed.stderr, "")
            self.assertIn("00007C00", typed.stdout)
            self.assertIn("mov ax, 0x1234", typed.stdout)
            self.assertIn("00007C03", typed.stdout)
            self.assertIn("db 0x00, 0x00, 0x90, 0xC3", typed.stdout)
            self.assertNotIn("add byte", typed.stdout)
            self.assertIn("00007C07", typed.stdout)
            self.assertIn("mov eax, 0x12345678", typed.stdout)
            self.assertIn("00007C0C", typed.stdout)
            self.assertIn("mov ax, 0xABCD", typed.stdout)

            legacy = runner.run(
                frozen.tools["cupiddis"],
                [
                    "--raw",
                    "--mode=16",
                    "--mode-at=3:32",
                    "--mode-at=8:16",
                    "--base=0x7c00",
                    code_only,
                ],
                60,
            )
            self.assertEqual(legacy.returncode, 0, legacy.stderr)
            self.assertEqual(legacy.stderr, "")
            self.assertIn("mov eax, 0x12345678", legacy.stdout)
            self.assertIn("00007C08", legacy.stdout)

    def test_checked_seed_emits_complete_active_libm_object(self):
        self._assert_checked_seed_emits_complete_unchanged_kernel_object(
            "kernel/cpu/libm.cc",
            46508,
            1376,
            '2b10a62a7aefdafa09020e69205fafc278fd130477f95944e6c6ac3bda378169',
            16004,
            '91cb378447c48e9aac0d94ea858257b2d5c44540ea77a78ed8abfb1ae83bcf36',
        )

    def test_checked_seed_emits_complete_unchanged_kernel_entry_object(self):
        self._assert_checked_seed_emits_complete_unchanged_kernel_object(
            "kernel/core/kernel.cc",
            31654,
            968,
            '7dc9898969de8e484be92172bdfe0e5852c03caf8d90c12571f134dc0dcc39aa',
            25968,
            '745ea24c6b51659270b61476d817f7a6fd923b2355e966b872407cec47e7601e',
        )

    def test_checked_seed_emits_page_aligned_kernel_stack_top(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        source_text = (
            "extern unsigned int _kernel_end;\n"
            "extern unsigned int _bss_start;\n"
            "void kmain(void);\n"
            "void _start(void) "
            "__attribute__((section(\".text.start\")));\n"
            "void _start(void) {\n"
            "  asm volatile("
            "\"mov $0x1100000, %%esp\\nmov %%esp, %%ebp\\n"
            "mov $_bss_start, %%edi\\nmov $_kernel_end, %%ecx\\n"
            "sub %%edi, %%ecx\\nshr $2, %%ecx\\n"
            "xor %%eax, %%eax\\ncld\\nrep stosl\\n\""
            " : : : \"eax\", \"ecx\", \"edi\", \"memory\");\n"
            "  kmain();\n"
            "}\n"
        )
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-stack-top-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "kernel-entry.cc"
            source.write_text(
                source_text,
                encoding="utf-8",
                newline="\n",
            )
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)
            images = []
            for index in range(2):
                output = root / f"kernel-entry-{index}.o"
                result = runner.run(
                    frozen.tools["cupidc"],
                    [
                        "--root",
                        REPO_ROOT,
                        "--gnu",
                        "--freestanding",
                        "-c",
                        "/" + source.relative_to(REPO_ROOT).as_posix(),
                        "-o",
                        "/" + output.relative_to(REPO_ROOT).as_posix(),
                    ],
                    180,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertEqual(result.stderr, "")
                images.append(output.read_bytes())
            self.assertEqual(images[0], images[1])
            self.assertEqual(images[0][:7], b"\x7fELF\x01\x01\x01")
            self.assertEqual(
                images[0].count(b"\xbc\x00\x00\x10\x01"),
                1,
            )
            self.assertNotIn(b"\xbc\x00\x00\xf0\x00", images[0])

    def test_checked_seed_emits_complete_unchanged_simd_object(self):
        self._assert_checked_seed_emits_complete_unchanged_kernel_object(
            "kernel/cpu/simd.cc",
            14189,
            462,
            '40a83184a29a5846cc058623d70b63ef61543d13eab132abc6616f2376329da2',
            8008,
            '7f63adc1b925f8cf8e0f8c3e202a1a5be0bbd53fc378e086a849da3495870378',
        )

    def test_checked_seed_emits_exact_doom_compatibility_objects_twice(
        self,
    ):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        audit = json.loads(
            (
                REPO_ROOT
                / "docs"
                / "bootstrap"
                / "audits"
                / "active-build.json"
            ).read_text(encoding="utf-8")
        )
        profile = next(
            item
            for item in audit["contracts"][
                "c_preprocessor_translation_units"
            ]["profiles"]
            if item["name"] == "DOOM_COMPAT_I386"
        )
        self.assertEqual(profile["tracked_translation_units"], 3)
        self.assertTrue(profile["gnu_extensions"])
        self.assertFalse(profile["hosted_environment"])
        self.assertTrue(profile["implicit_function_declarations"])
        self.assertTrue(profile["compatibility_pointer_conversions"])
        self.assertEqual(profile["forced_includes"], [])
        self.assertEqual(len(profile["include_roots"]), 20)
        self.assertEqual(
            profile["macro_actions"],
            [
                {"name": "__GNUC__", "replacement": "1"},
                {"name": "__SIZEOF_POINTER__", "replacement": "4"},
                {
                    "name": "__ORDER_LITTLE_ENDIAN__",
                    "replacement": "1234",
                },
                {
                    "name": "__ORDER_BIG_ENDIAN__",
                    "replacement": "4321",
                },
                {
                    "name": "__ORDER_PDP_ENDIAN__",
                    "replacement": "3412",
                },
                {
                    "name": "__BYTE_ORDER__",
                    "replacement": "__ORDER_LITTLE_ENDIAN__",
                },
                {"name": "__SSE2__", "replacement": "1"},
            ],
        )
        arguments: list[str | Path] = [
            "--root",
            REPO_ROOT,
            "--gnu",
            "--doom-compat",
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

        expected_objects = {
            "/kernel/doom/dglibc.cc": (
                32223,
                1281,
                '937ac24b1f4ce7daa09e38cca78a9a9bd44972c0ed9c47a54c57a086057a811b',
                88972,
                '139525ec8651f3cff341c5c5c037b73111e2aa2c76d1dd840d6ae066cab98ce9',
            ),
            "/kernel/doom/doom_libc_stubs.cc": (
                11385,
                430,
                '009872d1dd14aa96ca3e2f77761f6a8c97027aa5c5cf5230e9023c03f54dd809',
                16136,
                '8ac1971586b322ff112291c45251b74f34da8e5b9199ddeb8beb32b9b66e96d1',
            ),
            "/kernel/doom/doomgeneric_cupidos.cc": (
                15212,
                511,
                'b594baa54742eb91e69c581a0ca6aa818af8d789c1088af790a95a4973392539',
                10792,
                '431fd57dac2a6062ddc50aa2a428c735492044d25ae02afc46641c5993fe51de',
            ),
        }
        from tools.cupidc_kernel_compile import APPROVED_DOOM_COMPAT_SOURCES

        tracked_sources = sorted(
            "/" + transform["inputs"][0]
            for transform in audit["build"]["transforms"]
            if transform["inputs"]
            and transform["inputs"][0] in APPROVED_DOOM_COMPAT_SOURCES
            and transform["recipe"]
            == [
                "$(PRODUCTION_SEED_DIRECTORY)cupidbuild.$(PRODUCTION_SEED_SUFFIX) compile-doom " + chr(92),
                '--seed-manifest $(PRODUCTION_SEED_MANIFEST) --root "$(CURDIR)" ' + chr(92),
                '--seed-release $(PRODUCTION_SEED_RELEASE) ' + chr(92),
                f"--source {transform['inputs'][0]} --output {transform['output']}",
            ]
        )
        self.assertEqual(tracked_sources, sorted(expected_objects))
        for name, digest in (
            ("dglibc_compat.h", "bc3cedfb7a1f8156c040c9810e9c312edcc411fc2979795bbd5ea9a71d098e76"),
            ("dglibc_selftest.inc", "d509a63bc6a480779e8c7203bb037c5ff06572f47d7bc6311f4c62b9ffe440e2"),
        ):
            with self.subTest(include=name):
                data = (REPO_ROOT / "kernel/doom" / name).read_bytes()
                self.assertEqual(hashlib.sha256(data).hexdigest(), digest)
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-doom-compat-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)
            for source, expected in expected_objects.items():
                source_bytes = (REPO_ROOT / source.lstrip("/")).read_bytes()
                self.assertEqual(
                    (
                        len(source_bytes),
                        source_bytes.count(b"\n"),
                        hashlib.sha256(source_bytes).hexdigest(),
                    ),
                    expected[:3],
                )
                images = []
                for index in range(2):
                    output = root / (
                        Path(source).stem + f"-{index}.o"
                    )
                    logical_output = "/" + output.relative_to(
                        REPO_ROOT
                    ).as_posix()
                    result = runner.run(
                        frozen.tools["cupidc"],
                        [
                            *arguments,
                            "-c",
                            source,
                            "-o",
                            logical_output,
                        ],
                        180,
                    )
                    self.assertEqual(
                        result.returncode,
                        0,
                        f"{source}: {result.stderr}",
                    )
                    self.assertEqual(result.stdout, "")
                    self.assertEqual(result.stderr, "")
                    image = output.read_bytes()
                    self.assertEqual(
                        image[:7], b"\x7fELF\x01\x01\x01"
                    )
                    images.append(image)
                self.assertEqual(images[0], images[1])
                self.assertEqual(
                    (
                        len(images[0]),
                        hashlib.sha256(images[0]).hexdigest(),
                    ),
                    expected[3:],
                )
