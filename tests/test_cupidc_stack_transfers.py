"""Check validated register transfers and nearby literal and value-width boundaries."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile

from tests.test_cupidc_frame_load import FrameToolCase
from tests.test_cupidc_frame_load import ROOT, function_bytes


class CupidCStackTransferTests(FrameToolCase):
    fixture_source = '/toolchain/tests/cupidc_stack_transfer_runtime.cc'

    def test_binary_and_unary_word_transfers_use_register_moves(self):
        self.assertIn(bytes.fromhex('89c15831c889c0'), self.code['binary_words'])
        self.assertIn(bytes.fromhex('89c0f7d089c0'), self.code['unary_word'])

    def test_arithmetic_results_transfer_to_binary_and_unary_consumers(self):
        self.assertIn(bytes.fromhex("01c889c15831c889c0"), self.code["right_binary_result"])
        self.assertIn(bytes.fromhex("31c889c0f7d089c0"), self.code["unary_binary_result"])
        self.assertIn(bytes.fromhex("f7d089c15831c889c0"), self.code["right_unary_result"])
        self.assertIn(bytes.fromhex("f7d889c0f7d089c0"), self.code["unary_unary_result"])

    def test_word_stores_and_returns_preserve_scalar_register_values(self):
        self.assertIn(bytes.fromhex("01c889c1588908"), self.code["stored_sum"])
        self.assertIn(bytes.fromhex("31c889c0c9c3"), self.code["binary_words"])
        self.assertIn(bytes.fromhex("5059588808"), self.code["narrow_store"])

    def test_literal_tail_and_wide_value_keep_their_original_protocols(self):
        self.assertIn(bytes.fromhex('6800000050595831c889c0'), self.code['literal_tail'])
        self.assertGreater(len(self.code['full_width']), len(self.code['binary_words']))
        self.assertIn(bytes.fromhex('5958'), self.code['full_width'])

    def test_enum_parameter_conversion_preserves_int_object_equivalence(self):
        for name, enum_source, int_source in (
            ("return", "int f(enum { A } value) { return value; }",
             "int f(int value) { return value; }"),
            ("arithmetic", "int f(enum { A = 13, B = 17 } value) { return value + A + B; }",
             "int f(int value) { return value + 13 + 17; }"),
        ):
            objects = []
            for kind, source in (("enum", enum_source), ("int", int_source)):
                path = self.output / (name + "-" + kind + ".cc")
                path.write_bytes((source + "\n").encode())
                output = path.with_suffix(".o")
                self.run_tool("cupidc", "--root", ROOT, "-c",
                              "/" + path.relative_to(ROOT).as_posix(), "-o",
                              "/" + output.relative_to(ROOT).as_posix())
                self.run_tool("cupiddis", "--require-known", "--require-local-targets",
                              "--require-code-anchors", output)
                objects.append(output.read_bytes())
            with self.subTest(name=name):
                self.assertEqual(objects[0], objects[1])

    def test_incomplete_enum_parameter_rejects_without_replacing_output(self):
        source = self.output / "incomplete-enum.cc"
        source.write_bytes(b"int f(enum Missing value) { return value; }\n")
        output = self.output / "preserved.o"
        output.write_bytes(b"preserved compiler output")
        result = subprocess.run([str(self.tools["cupidc"]), "--root", str(ROOT), "-c",
            "/" + source.relative_to(ROOT).as_posix(), "-o",
            "/" + output.relative_to(ROOT).as_posix()], capture_output=True, text=True, timeout=60)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("enum", result.stderr.lower())
        self.assertEqual(output.read_bytes(), b"preserved compiler output")

    def test_pointer_reads_branch_loops_and_complete_values_execute(self):
        self.execute_runtime()


class CupidCStackEntryTests(FrameToolCase):
    entry_mode = ()
    target_prefix = bytes.fromhex("595831c889c0")
    expected_value = staticmethod(lambda left, right: left ^ right)
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        supplied = os.environ.get('CUPIDC_STACK_ENTRY_CONTRACT')
        if supplied:
            cls.entry_program = Path(supplied)
            return
        if cls.native_build is None:
            raise AssertionError('provide CUPIDC_STACK_ENTRY_CONTRACT with supplied tools')
        build = Path(cls.native_build.name)
        cls.entry_program = build / ('cupidc-stack-entry-contract' +
                                     ('.exe' if os.name == 'nt' else ''))
        relative = cls.entry_program.relative_to(ROOT / 'toolchain').as_posix()
        result = subprocess.run(
            ['make', '-C', str(ROOT / 'toolchain'),
             'BUILD_DIR=' + build.relative_to(ROOT / 'toolchain').as_posix(), relative],
            capture_output=True, text=True, timeout=180)
        if result.returncode:
            cls.native_build.cleanup()
            raise AssertionError(result.stdout + result.stderr)

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(
            prefix='.stack-entry-case-', dir=ROOT / 'toolchain')
        self.addCleanup(self.directory.cleanup)
        self.output = Path(self.directory.name)
        self.object = self.output / 'entry.o'
        result = subprocess.run([str(self.entry_program), str(ROOT), str(self.object), *self.entry_mode],
                                capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.code = function_bytes(self.object.read_bytes())['entry_guard']
        self.run_tool('cupiddis', '--require-known', '--require-local-targets',
                      '--require-code-anchors', self.object)

    def test_branch_target_keeps_both_live_stack_operands(self):
        jumps = [index for index, byte in enumerate(self.code) if byte == 0xe9]
        self.assertEqual(len(jumps), 1)
        jump = jumps[0]
        target = jump + 5 + struct.unpack_from('<i', self.code, jump + 1)[0]
        self.assertEqual(self.code[target:target + len(self.target_prefix)], self.target_prefix)

    def test_branch_entry_executes_complete_operand_values(self):
        host = 'windows' if os.name == 'nt' else 'linux'
        lines = ['BITS 32', 'SECTION .text', 'GLOBAL _start', 'EXTERN entry_guard']
        if host == 'windows':
            lines.append('EXTERN __imp_ExitProcess')
        lines.append('_start:')
        values = (0, 1, 0x7fffffff, 0x80000000, 0xffffffff)
        for left in values:
            for right in values:
                lines.extend((f' push {right}', f' push {left}', ' call entry_guard',
                              ' add esp, 8', f' cmp eax, {self.expected_value(left, right)}', ' jne wrong'))
        lines.extend((' xor eax, eax', ' jmp finish', 'wrong:', ' mov eax, 1', 'finish:'))
        if host == 'windows':
            lines.extend((' push eax', ' call dword [__imp_ExitProcess]'))
        else:
            lines.extend((' mov ebx, eax', ' mov eax, 1', ' int 0x80'))
        source = self.output / 'caller.asm'
        source.write_bytes(('\n'.join(lines) + '\n').encode())
        caller = self.output / 'caller.o'
        self.run_tool('cupidasm', '-f', 'elf32', source, '-o', caller)
        program = self.output / ('entry.exe' if host == 'windows' else 'entry.elf')
        arguments = ['-m', 'i386pe' if host == 'windows' else 'elf_i386',
                     '--text-address', '0x00401000' if host == 'windows' else '0x08048000',
                     '--entry', '_start']
        if host == 'windows':
            arguments += ['--import', '__imp_ExitProcess=KERNEL32.dll:ExitProcess']
        self.run_tool('cupidld', *arguments, '-o', program, caller, self.object)
        if host == 'linux':
            program.chmod(0o700)
        result = subprocess.run([str(program)], capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class CupidCStackResultEntryTests(CupidCStackEntryTests):
    entry_mode = ("result",)
    target_prefix = bytes.fromhex("58f7d089c0")
    expected_value = staticmethod(lambda left, right: (left ^ right) ^ 0xffffffff)


class CupidCStackConversionEntryTests(CupidCStackEntryTests):
    entry_mode = ("conversion",)
    target_prefix = bytes.fromhex("58c9c3")
    expected_value = staticmethod(lambda left, right: left)
