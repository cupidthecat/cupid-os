import re
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from tools import build_graph_audit
from tools import cupidc_production_compile as production_compile
from tools import cupidc_production_frontier as production_frontier
from tools import cupidld_user_link as user_link


REPO_ROOT = Path(__file__).resolve().parents[1]


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


def _valid_user_executable(entry=0x00F00004):
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
        0x00F00000,
        0x00F00000,
        len(payload),
        len(payload),
        5,
        1,
    )
    image[payload_offset:] = payload
    return bytes(image)


class FakeCompilerExecutor:
    def __init__(self, root, payload=None, result=None, mutate=None):
        self.root = root
        self.compiler_root = "/repository"
        self.payload = payload
        self.result = result or subprocess.CompletedProcess([], 0, "", "")
        self.mutate = mutate
        self.calls = []

    def compiler_root_for(self, path):
        return str(path.resolve())

    def run(self, executable, arguments, timeout):
        self.calls.append((executable, tuple(arguments), timeout))
        if self.payload is not None:
            compiler_root = Path(
                arguments[arguments.index("--root") + 1]
            )
            logical_output = arguments[arguments.index("-o") + 1]
            output = compiler_root / logical_output.lstrip("/")
            output.write_bytes(self.payload)
        if self.mutate is not None:
            self.mutate()
        return self.result


class FakeLinkRunner:
    def __init__(self, payload=None, result=None, mutate=None):
        self.payload = payload
        self.result = result or subprocess.CompletedProcess([], 0, "", "")
        self.mutate = mutate
        self.calls = []

    def run(self, executable, arguments, timeout):
        self.calls.append((executable, tuple(arguments), timeout))
        if self.payload is not None:
            output = Path(arguments[arguments.index("-o") + 1])
            output.write_bytes(self.payload)
        if self.mutate is not None:
            self.mutate()
        return self.result


class ProductionCompileTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        for relative in (
            "user/examples",
            "user/build",
            "kernel/util",
            "drivers",
            "kernel/core",
            "kernel/fs",
        ):
            (self.root / relative).mkdir(parents=True, exist_ok=True)
        (self.root / "user/examples/hello.cc").write_text(
            '#include "../cupid.h"\nvoid _start(void) {}\n',
            encoding="utf-8",
        )
        for relative in production_compile.USER_INCLUDE_CLOSURE:
            (self.root / relative).write_text("/* user */\n", encoding="utf-8")
        for relative in production_compile.GENERATED_INCLUDE_CLOSURE:
            (self.root / relative).write_text(
                "/* generated */\n", encoding="utf-8"
            )
        self.seed = self.root / "cupidc.elf"
        self.seed.write_bytes(b"seed")
        self.seed_inputs = SimpleNamespace(tools={"cupidc": self.seed})

    def tearDown(self):
        self.temporary.cleanup()

    def test_profiles_and_approved_renamed_sources_are_exact(self):
        self.assertEqual(
            production_compile.USER_SOURCES,
            (
                "user/examples/cat.cc",
                "user/examples/hello.cc",
                "user/examples/ls.cc",
            ),
        )
        self.assertEqual(
            production_compile.GENERATED_INSTALL_SOURCES,
            (
                "kernel/util/bin_programs_gen.cc",
                "kernel/util/demos_programs_gen.cc",
                "kernel/util/docs_programs_gen.cc",
            ),
        )
        command = production_compile.build_compile_arguments(
            "user",
            "/user/examples/hello.cc",
            "/user/build/hello.o",
            "/repository",
        )
        self.assertEqual(
            command,
            (
                "-c",
                "/user/examples/hello.cc",
                "-o",
                "/user/build/hello.o",
                "--freestanding",
                "-I",
                "/user",
                "--root",
                "/repository",
            ),
        )

    def test_checked_compile_publishes_only_a_valid_object(self):
        output = self.root / "user/build/hello.o"
        executor = FakeCompilerExecutor(
            self.root, payload=_valid_elf32_object()
        )
        with mock.patch.object(
            production_compile,
            "freeze_seed_inputs",
            return_value=self.seed_inputs,
        ):
            production_compile.compile_production_source(
                self.root,
                "user",
                Path("user/examples/hello.cc"),
                Path("user/build/hello.o"),
                executor=executor,
            )
        self.assertEqual(output.read_bytes(), _valid_elf32_object())
        self.assertEqual(len(executor.calls), 1)

    def test_compile_failure_preserves_the_previous_object(self):
        output = self.root / "user/build/hello.o"
        output.write_bytes(b"previous object")
        executor = FakeCompilerExecutor(
            self.root,
            result=subprocess.CompletedProcess([], 7, "", "bad source"),
        )
        with mock.patch.object(
            production_compile,
            "freeze_seed_inputs",
            return_value=self.seed_inputs,
        ):
            with self.assertRaisesRegex(
                production_compile.ProductionCompileError,
                "status 7: bad source",
            ):
                production_compile.compile_production_source(
                    self.root,
                    "user",
                    Path("user/examples/hello.cc"),
                    Path("user/build/hello.o"),
                    executor=executor,
                )
        self.assertEqual(output.read_bytes(), b"previous object")

    def test_input_change_during_compile_prevents_publication(self):
        output = self.root / "user/build/hello.o"
        output.write_bytes(b"previous object")
        header = self.root / "user/cupid.h"
        executor = FakeCompilerExecutor(
            self.root,
            payload=_valid_elf32_object(),
            mutate=lambda: header.write_text("changed\n", encoding="utf-8"),
        )
        with mock.patch.object(
            production_compile,
            "freeze_seed_inputs",
            return_value=self.seed_inputs,
        ):
            with self.assertRaisesRegex(
                production_compile.ProductionCompileError,
                "inputs changed",
            ):
                production_compile.compile_production_source(
                    self.root,
                    "user",
                    Path("user/examples/hello.cc"),
                    Path("user/build/hello.o"),
                    executor=executor,
                )
        self.assertEqual(output.read_bytes(), b"previous object")


class UserLinkTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "user/build").mkdir(parents=True)
        self.source = self.root / "user/build/hello.o"
        self.output = self.root / "user/build/hello"
        self.source.write_bytes(_valid_elf32_object())
        self.seed = self.root / "cupidld.elf"
        self.seed.write_bytes(b"seed")
        self.seed_inputs = SimpleNamespace(tools={"cupidld": self.seed})

    def tearDown(self):
        self.temporary.cleanup()

    def test_entry_may_follow_the_external_arena_base(self):
        user_link.validate_user_executable_bytes(
            _valid_user_executable(entry=0x00F00004)
        )

    def test_entry_must_be_in_executable_file_backed_bytes(self):
        with self.assertRaisesRegex(
            user_link.UserLinkError,
            "entry point is not in executable file-backed bytes",
        ):
            user_link.validate_user_executable_bytes(
                _valid_user_executable(entry=0x00F00040)
            )

    def test_program_table_matches_the_kernel_loader_contract(self):
        cases = []

        unsupported = bytearray(_valid_user_executable())
        struct.pack_into("<I", unsupported, 52, 4)
        cases.append(
            ("unsupported type", unsupported, "unsupported program type")
        )

        stack_payload = bytearray(_valid_user_executable())
        struct.pack_into("<I", stack_payload, 52, 0x6474E551)
        cases.append(
            (
                "non-load payload",
                stack_payload,
                "non-load program header has a payload",
            )
        )

        unknown_flags = bytearray(_valid_user_executable())
        struct.pack_into("<I", unknown_flags, 52 + 24, 8)
        cases.append(
            ("unknown flags", unknown_flags, "unknown permission flags")
        )

        incongruent = bytearray(_valid_user_executable())
        struct.pack_into("<I", incongruent, 52 + 28, 0x1000)
        cases.append(
            ("incongruent alignment", incongruent, "alignment is incongruent")
        )

        overlapping = bytearray(_valid_user_executable())
        struct.pack_into("<H", overlapping, 44, 2)
        overlapping[84:116] = overlapping[52:84]
        struct.pack_into("<I", overlapping, 84 + 8, 0x00F00008)
        struct.pack_into("<I", overlapping, 84 + 12, 0x00F00008)
        cases.append(
            ("overlapping loads", overlapping, "load segments overlap")
        )

        empty_load = bytearray(_valid_user_executable())
        struct.pack_into("<I", empty_load, 52 + 16, 0)
        struct.pack_into("<I", empty_load, 52 + 20, 0)
        cases.append(
            ("empty load", empty_load, "no nonempty loadable segment")
        )

        too_many = bytearray(_valid_user_executable())
        struct.pack_into("<H", too_many, 44, 17)
        cases.append(
            ("too many headers", too_many, "more than 16 program headers")
        )

        for label, image, message in cases:
            with self.subTest(label=label):
                with self.assertRaisesRegex(user_link.UserLinkError, message):
                    user_link.validate_user_executable_bytes(bytes(image))

    def test_program_table_cannot_overlap_the_elf_header(self):
        payload_offset = 0x100
        image = bytearray(payload_offset + 16)
        image[0:7] = b"\x7fELF\x01\x01\x01"
        struct.pack_into(
            "<HHIIIIIHHHHHH",
            image,
            16,
            2,
            3,
            1,
            0x00F00000,
            32,
            0,
            0,
            52,
            32,
            2,
            0,
            0,
            0,
        )
        struct.pack_into(
            "<IIIIIIII",
            image,
            64,
            1,
            payload_offset,
            0x00F00000,
            0x00F00000,
            16,
            16,
            5,
            1,
        )
        image[payload_offset:] = b"\x90" * 16

        with self.assertRaisesRegex(
            user_link.UserLinkError, "invalid program-header offset"
        ):
            user_link.validate_user_executable_bytes(bytes(image))

    def test_program_table_must_fit_the_loader_signed_seek(self):
        image = bytearray(_valid_user_executable())
        struct.pack_into("<I", image, 28, 0x80000000)

        with self.assertRaisesRegex(
            user_link.UserLinkError, "invalid program-header offset"
        ):
            user_link.validate_user_executable_bytes(bytes(image))

    def test_zero_sized_gnu_stack_header_is_allowed(self):
        image = bytearray(_valid_user_executable())
        struct.pack_into("<H", image, 44, 2)
        struct.pack_into(
            "<IIIIIIII",
            image,
            84,
            0x6474E551,
            0,
            0,
            0,
            0,
            0,
            6,
            16,
        )
        user_link.validate_user_executable_bytes(bytes(image))

    def test_checked_link_is_atomic_and_uses_the_fixed_contract(self):
        runner = FakeLinkRunner(payload=_valid_user_executable())
        with mock.patch.object(
            user_link,
            "freeze_seed_inputs",
            return_value=self.seed_inputs,
        ):
            user_link.link_user_program(
                self.root,
                Path("user/build/hello.o"),
                Path("user/build/hello"),
                runner=runner,
            )
        self.assertEqual(self.output.read_bytes(), _valid_user_executable())
        arguments = runner.calls[0][1]
        self.assertIn("0x00F00000", arguments)
        self.assertIn("_start", arguments)

    def test_link_failure_preserves_the_previous_executable(self):
        self.output.write_bytes(b"previous executable")
        runner = FakeLinkRunner(
            result=subprocess.CompletedProcess([], 9, "", "missing entry")
        )
        with mock.patch.object(
            user_link,
            "freeze_seed_inputs",
            return_value=self.seed_inputs,
        ):
            with self.assertRaisesRegex(
                user_link.UserLinkError,
                "status 9: missing entry",
            ):
                user_link.link_user_program(
                    self.root,
                    Path("user/build/hello.o"),
                    Path("user/build/hello"),
                    runner=runner,
                )
        self.assertEqual(self.output.read_bytes(), b"previous executable")

    def test_link_rejects_a_changed_input_object(self):
        self.output.write_bytes(b"previous executable")
        runner = FakeLinkRunner(
            payload=_valid_user_executable(),
            mutate=lambda: self.source.write_bytes(_valid_elf32_object() + b"x"),
        )
        with mock.patch.object(
            user_link,
            "freeze_seed_inputs",
            return_value=self.seed_inputs,
        ):
            with self.assertRaisesRegex(
                user_link.UserLinkError,
                "input object changed",
            ):
                user_link.link_user_program(
                    self.root,
                    Path("user/build/hello.o"),
                    Path("user/build/hello"),
                    runner=runner,
            )
        self.assertEqual(self.output.read_bytes(), b"previous executable")

    def test_linker_reads_an_immutable_copy_before_rejecting_live_drift(self):
        original = self.source.read_bytes()
        self.output.write_bytes(b"previous executable")

        class MutatingLinkRunner:
            def __init__(self, live_source):
                self.live_source = live_source
                self.link_input = None
                self.link_payload = None

            def run(self, _executable, arguments, _timeout):
                self.live_source.write_bytes(original + b"changed")
                self.link_input = Path(arguments[-1])
                self.link_payload = self.link_input.read_bytes()
                output = Path(arguments[arguments.index("-o") + 1])
                output.write_bytes(_valid_user_executable())
                return subprocess.CompletedProcess([], 0, "", "")

        runner = MutatingLinkRunner(self.source)
        with mock.patch.object(
            user_link,
            "freeze_seed_inputs",
            return_value=self.seed_inputs,
        ):
            with self.assertRaisesRegex(
                user_link.UserLinkError, "input object changed"
            ):
                user_link.link_user_program(
                    self.root,
                    Path("user/build/hello.o"),
                    Path("user/build/hello"),
                    runner=runner,
                )

        self.assertNotEqual(runner.link_input, self.source)
        self.assertEqual(runner.link_payload, original)
        self.assertEqual(self.output.read_bytes(), b"previous executable")

    def test_restored_live_input_cannot_change_the_linked_snapshot(self):
        original = self.source.read_bytes()

        class RestoringLinkRunner:
            def __init__(self, live_source):
                self.live_source = live_source
                self.link_payload = None

            def run(self, _executable, arguments, _timeout):
                self.live_source.write_bytes(original + b"transient")
                try:
                    self.link_payload = Path(arguments[-1]).read_bytes()
                finally:
                    self.live_source.write_bytes(original)
                output = Path(arguments[arguments.index("-o") + 1])
                output.write_bytes(_valid_user_executable())
                return subprocess.CompletedProcess([], 0, "", "")

        runner = RestoringLinkRunner(self.source)
        with mock.patch.object(
            user_link,
            "freeze_seed_inputs",
            return_value=self.seed_inputs,
        ):
            user_link.link_user_program(
                self.root,
                Path("user/build/hello.o"),
                Path("user/build/hello"),
                runner=runner,
            )

        self.assertEqual(runner.link_payload, original)
        self.assertEqual(self.output.read_bytes(), _valid_user_executable())


class ProductionFrontierTests(unittest.TestCase):
    def test_user_frontier_rejects_an_installed_object_that_differs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            closure = production_frontier._user_inputs()
            for relative in closure:
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"input")

            object_payload = b"replayed object"
            executable_payload = _valid_user_executable()
            for relative in production_compile.USER_SOURCES:
                name = Path(relative).stem
                (root / relative).write_bytes(b"source")
                build = root / "user" / "build"
                build.mkdir(parents=True, exist_ok=True)
                (build / f"{name}.o").write_bytes(object_payload)
                (build / name).write_bytes(executable_payload)
            (
                root / "user" / "build" / "cat.o"
            ).write_bytes(b"stale object")

            def compile_source(_root, _cohort, _source, output):
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_bytes(object_payload)

            def link_source(_root, _source, output):
                output.write_bytes(executable_payload)

            with (
                mock.patch.object(
                    production_frontier,
                    "compile_production_source",
                    compile_source,
                ),
                mock.patch.object(
                    production_frontier,
                    "link_user_program",
                    link_source,
                ),
            ):
                with self.assertRaisesRegex(
                    production_frontier.FrontierError,
                    "production user object differs from the frontier",
                ):
                    production_frontier.run_user_frontier(root)

    def test_generated_frontier_rejects_an_installed_object_that_differs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "image.bmp").write_bytes(b"asset")
            generator_inputs = production_frontier._generator_inputs(root)
            closure = production_frontier._generated_inputs(
                root, generator_inputs
            )
            for relative in closure:
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                if not path.exists():
                    path.write_bytes(b"input")

            source_payload = b"generated source\n"
            object_payload = b"replayed object"
            for relative in production_compile.GENERATED_INSTALL_SOURCES:
                source = root / relative
                source.write_bytes(source_payload)
                source.with_suffix(".o").write_bytes(object_payload)
            (
                root / production_compile.GENERATED_INSTALL_SOURCES[0]
            ).with_suffix(".o").write_bytes(b"stale object")

            def generate(_root, _command, _output):
                return source_payload

            def compile_source(_root, _cohort, _source, output):
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_bytes(object_payload)

            with (
                mock.patch.object(
                    production_frontier, "_run_generator", generate
                ),
                mock.patch.object(
                    production_frontier,
                    "compile_production_source",
                    compile_source,
                ),
            ):
                with self.assertRaisesRegex(
                    production_frontier.FrontierError,
                    "production generated object differs from the frontier",
                ):
                    production_frontier.run_generated_frontier(root)


class ProductionBuildContractTests(unittest.TestCase):
    def test_build_graph_assigns_both_cohorts_to_checked_cupid_tools(self):
        root_model = build_graph_audit._collect_build_model(
            REPO_ROOT, "make", "all", "."
        )
        user_model = build_graph_audit._collect_build_model(
            REPO_ROOT, "make", "all", "user"
        )
        transforms = {
            transform["output"]: transform
            for model in (root_model, user_model)
            for transform in model.transforms
        }
        for name in ("bin", "docs", "demos"):
            output = f"kernel/util/{name}_programs_gen.o"
            transform = transforms[output]
            self.assertEqual(
                transform["tools"],
                ["cupid_c_compiler", "host_python"],
            )
            self.assertNotIn("host_c_compiler", transform["tools"])
            self.assertIn(
                f"kernel/util/{name}_programs_gen.cc",
                transform["inputs"],
            )
        for name in ("hello", "ls", "cat"):
            compile_transform = transforms[f"user/build/{name}.o"]
            link_transform = transforms[f"user/build/{name}"]
            self.assertEqual(
                compile_transform["tools"],
                ["cupid_c_compiler", "host_python"],
            )
            self.assertEqual(
                link_transform["tools"],
                ["cupid_linker", "host_python"],
            )
            self.assertIn(
                f"user/examples/{name}.cc",
                compile_transform["inputs"],
            )
            self.assertIn(
                "bootstrap/seeds/i386-linux/manifest.json",
                compile_transform["inputs"],
            )
            self.assertIn(
                "bootstrap/seeds/i386-linux/cupidld.elf",
                link_transform["inputs"],
            )

    def test_user_makefile_has_no_host_compiler_or_host_linker_recipe(self):
        makefile = (REPO_ROOT / "user/Makefile").read_text(encoding="utf-8")
        logical = makefile.replace("\\\n", " ")
        self.assertNotIn("$(CC)", makefile)
        self.assertNotIn("toolchain/build/cupidld", makefile)
        self.assertIn("examples/%.cc", logical)
        self.assertIn(
            "$(CUPIDC_PRODUCTION_COMPILE) --source user/$< "
            "--output user/$@",
            logical,
        )
        self.assertIn(
            "$(CUPIDLD_USER_LINK) --input user/$< --output user/$@",
            logical,
        )
        for seed in ("cupidc.elf", "cupidld.elf", "manifest.json"):
            self.assertIn(seed, makefile)

    def test_generated_install_tables_use_checked_cupidc_and_cc_paths(self):
        makefile = (REPO_ROOT / "Makefile").read_text(encoding="utf-8")
        logical = makefile.replace("\\\n", " ")
        for name in ("bin", "docs", "demos"):
            source = f"kernel/util/{name}_programs_gen.cc"
            target = f"kernel/util/{name}_programs_gen.o"
            self.assertIn(source, makefile)
            self.assertRegex(
                logical,
                rf"(?m)^{target}: {source} .*$",
            )
        generated_block = logical[
            logical.index("kernel/util/bin_programs_gen.cc:")
            : logical.index("# Pattern rule: embed any bin/*.cc")
        ]
        self.assertNotIn("$(CC)", generated_block)
        self.assertEqual(
            generated_block.count("$(CUPIDC_PRODUCTION_COMPILE) --source"),
            3,
        )

    def test_frontier_closures_name_every_seed_and_renamed_source(self):
        user_inputs = production_frontier._user_inputs()
        for source in production_compile.USER_SOURCES:
            self.assertIn(source, user_inputs)
        for seed in production_frontier.SEED_FILES:
            self.assertIn(seed, user_inputs)

        generator_inputs = production_frontier._generator_inputs(REPO_ROOT)
        generated_inputs = production_frontier._generated_inputs(
            REPO_ROOT, generator_inputs
        )
        for source in production_compile.GENERATED_INSTALL_SOURCES:
            self.assertIn(source, generated_inputs)
        self.assertNotIn("bin/old_cc2.cc", generator_inputs["bin"])
        self.assertEqual(
            generator_inputs["demos"],
            tuple(sorted(generator_inputs["demos"])),
        )

    def test_poisoned_host_compiler_cannot_break_user_build(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        with tempfile.TemporaryDirectory(
            prefix=".poison-user-build-",
            dir=REPO_ROOT / "user",
        ) as temporary:
            build = Path(temporary).name
            result = subprocess.run(
                [
                    make,
                    "-C",
                    "user",
                    "-B",
                    f"BUILD={build}",
                    "CC=host-compiler-must-not-run",
                    "all",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=600,
            )
        self.assertEqual(
            result.returncode,
            0,
            msg=(result.stderr + result.stdout)[-4000:],
        )
        self.assertNotIn("host-compiler-must-not-run", result.stdout)

    def test_poisoned_host_compiler_cannot_break_generated_objects(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        targets = [
            f"kernel/util/{name}_programs_gen.o"
            for name in ("bin", "docs", "demos")
        ]
        result = subprocess.run(
            [
                make,
                "-B",
                "CC=host-compiler-must-not-run",
                *targets,
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=600,
        )
        self.assertEqual(
            result.returncode,
            0,
            msg=(result.stderr + result.stdout)[-4000:],
        )
        self.assertNotIn("host-compiler-must-not-run", result.stdout)

    def test_user_runtime_target_has_a_closed_execution_gate(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        model = build_graph_audit._collect_build_model(
            REPO_ROOT, make, "test-user-cupidc-runtime", "."
        )
        rule = model.rules["test-user-cupidc-runtime"]
        self.assertIn("sync-user-runtime", rule.prerequisites)
        self.assertIn("tools/gui_terminal_smoke.py", rule.prerequisites)
        recipe = " ".join(rule.recipe)
        self.assertEqual(recipe.count("tools/gui_terminal_smoke.py"), 3)
        for program in ("hello", "ls", "cat"):
            self.assertIn(f'--command "exec /disk/{program}"', recipe)
            self.assertIn(
                f"$(USER_CUPIDC_RUNTIME_{program.upper()}_SUCCESS)",
                recipe,
            )
        self.assertEqual(recipe.count("--repeat 1"), 3)
        self.assertEqual(recipe.count("--key-pause 0.60"), 3)
        values = build_graph_audit._read_evaluated_make_variables(
            REPO_ROOT,
            make,
            (
                "USER_CUPIDC_RUNTIME_LOG",
                "USER_CUPIDC_RUNTIME_LS_LOG",
                "USER_CUPIDC_RUNTIME_CAT_LOG",
                "USER_CUPIDC_RUNTIME_HELLO_SUCCESS",
            ),
        )
        self.assertEqual(
            values["USER_CUPIDC_RUNTIME_LOG"],
            "tests/user-cupidc-runtime.log",
        )
        self.assertEqual(
            values["USER_CUPIDC_RUNTIME_LS_LOG"],
            "tests/user-cupidc-runtime-ls.log",
        )
        self.assertEqual(
            values["USER_CUPIDC_RUNTIME_CAT_LOG"],
            "tests/user-cupidc-runtime-cat.log",
        )
        success_pattern = values["USER_CUPIDC_RUNTIME_HELLO_SUCCESS"]
        self.assertIn(
            r"\[elf\] Loaded /disk/hello as PID "
            r"(?P<hello_pid>[1-9][0-9]*)",
            success_pattern,
        )
        self.assertIn(
            r"op=print bytes=27 fnv1a=0x6d2edfa6",
            success_pattern,
        )
        self.assertEqual(success_pattern.count("op=print_int"), 2)
        self.assertIn("(?P=hello_pid) op=exit", success_pattern)
        self.assertIn(r"\[PROCESS\] PID", success_pattern)
        self.assertIn("/disk/hello.* exiting", success_pattern)
        runtime_log = (
            "[shell_exec_cmd] prog='/disk/hello' rpath='/disk/hello' args=''\n"
            "[elf] Loaded /disk/hello as PID 4 "
            "(ELF32, 8196 bytes at 0x0x00f00000)\n"
            "[elf-syscall] pid=4 op=print bytes=27 "
            "fnv1a=0x6d2edfa6\n"
            "[elf-syscall] pid=4 op=print_int value=4\n"
            "[elf-syscall] pid=4 op=print_int value=13540\n"
            "[elf-syscall] pid=4 op=exit\n"
            '[PROCESS] PID 4 "/disk/hello" exiting\n'
        )
        self.assertIsNotNone(re.search(success_pattern, runtime_log, re.S))
        self.assertIsNone(
            re.search(
                success_pattern,
                runtime_log.replace(
                    "[elf-syscall] pid=4 op=exit",
                    "[elf-syscall] pid=5 op=exit",
                ),
                re.S,
            )
        )

        dry_run = subprocess.run(
            [
                make,
                "--no-print-directory",
                "-n",
                "-o",
                "cupidos.img",
                "test-user-cupidc-runtime",
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=120,
        )
        self.assertEqual(
            dry_run.returncode,
            0,
            msg=(dry_run.stderr + dry_run.stdout)[-4000:],
        )
        commands = dry_run.stdout.replace("\\\n", " ")
        self.assertIn(
            "user/build/hello:/hello user/build/ls:/ls "
            "user/build/cat:/cat",
            commands,
        )
        self.assertIn(
            "build/user-runtime-fixture.txt:/catfix.txt",
            commands,
        )
        self.assertIn(
            'tools/gui_terminal_smoke.py --qemu "qemu-system-i386"',
            commands,
        )
        for program in ("hello", "ls", "cat"):
            self.assertIn(f'--command "exec /disk/{program}"', commands)

    def test_external_elf_print_has_a_serial_runtime_contract(self):
        source = (REPO_ROOT / "kernel/core/syscall.cc").read_text(
            encoding="utf-8"
        )
        wrapper = re.search(
            r"static void syscall_print\(const char \*str\) \{"
            r"(?P<body>.*?)\n\}",
            source,
            re.S,
        )
        self.assertIsNotNone(wrapper)
        body = wrapper.group("body")
        self.assertIsNotNone(
            re.search(
                r"hash = syscall_print_fingerprint\(str, &length\);"
                r".*?pid = process_get_current_pid\(\);"
                r'.*?serial_printf\("\[elf-syscall\] pid=%u op=print '
                r'bytes=%u fnv1a=0x%08x\\n",'
                r"\s+pid, length, hash\);"
                r"\s+print\(str\);",
                body,
                re.S,
            )
        )
        self.assertIn("syscall_table.print = syscall_print;", source)
        self.assertNotIn("syscall_table.print = print;", source)
        self.assertNotRegex(
            source,
            r"serial_printf\([^;]*%s[^;]*\bstr\b",
        )


if __name__ == "__main__":
    unittest.main()
