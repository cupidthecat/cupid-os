import subprocess
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from tests.test_cupidc_production import _valid_elf32_object
from tools import cupidc_production_compile as production_compile


class InspectingCompilerExecutor:
    def __init__(self, root, live_source, live_header):
        self.root = root
        self.live_source = live_source
        self.live_header = live_header
        self.observed_source = None
        self.observed_header = None
        self.mapped_roots = []

    def run(self, executable, arguments, timeout):
        compiler_root = Path(arguments[arguments.index("--root") + 1])
        self.mapped_roots.append(compiler_root)
        logical_source = arguments[arguments.index("-c") + 1]
        logical_output = arguments[arguments.index("-o") + 1]

        source_before = self.live_source.read_bytes()
        header_before = self.live_header.read_bytes()
        self.live_source.write_bytes(b"changed source\n")
        self.live_header.write_bytes(b"changed header\n")
        try:
            self.observed_source = (
                compiler_root / logical_source.lstrip("/")
            ).read_bytes()
            self.observed_header = (
                compiler_root / "user/cupid.h"
            ).read_bytes()
        finally:
            self.live_source.write_bytes(source_before)
            self.live_header.write_bytes(header_before)

        output = compiler_root / logical_output.lstrip("/")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(_valid_elf32_object())
        return subprocess.CompletedProcess([], 0, "", "")


class ProductionCompileFreezeTests(unittest.TestCase):
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
        self.source = self.root / "user/examples/hello.cc"
        self.source.write_bytes(
            b'#include "../cupid.h"\nvoid _start(void) {}\n'
        )
        for relative in production_compile.USER_INCLUDE_CLOSURE:
            (self.root / relative).write_bytes(b"/* user */\n")
        for relative in production_compile.GENERATED_INCLUDE_CLOSURE:
            (self.root / relative).write_bytes(b"/* generated */\n")
        self.seed = self.root / "cupidc.elf"
        self.seed.write_bytes(b"seed")
        self.seed_inputs = SimpleNamespace(tools={"cupidc": self.seed})

    def tearDown(self):
        self.temporary.cleanup()

    def test_compiler_reads_an_immutable_source_and_header_copy(self):
        header = self.root / "user/cupid.h"
        executor = InspectingCompilerExecutor(
            self.root, self.source, header
        )
        with mock.patch.object(
            production_compile,
            "freeze_seed_inputs",
            return_value=self.seed_inputs,
        ), mock.patch.object(
            production_compile,
            "run_seed_tool",
            side_effect=lambda _manifest, _root, tool, arguments,
            *, timeout, frozen_seed, runner: runner.run(
                frozen_seed.tools[tool], arguments, timeout
            ),
        ):
            production_compile.compile_production_source(
                self.root,
                "user",
                Path("user/examples/hello.cc"),
                Path("user/build/hello.o"),
                executor=executor,
            )

        self.assertEqual(
            executor.observed_source,
            b'#include "../cupid.h"\nvoid _start(void) {}\n',
        )
        self.assertEqual(executor.observed_header, b"/* user */\n")
        self.assertEqual(len(executor.mapped_roots), 1)
        self.assertNotEqual(executor.mapped_roots[0], self.root)
        self.assertEqual(
            (self.root / "user/build/hello.o").read_bytes(),
            _valid_elf32_object(),
        )

    def test_user_source_is_coupled_to_its_output_name_and_tree(self):
        cases = (
            Path("user/build/cat.o"),
            Path("user/examples/hello.o"),
            Path("kernel/core/hello.o"),
        )
        for output in cases:
            with self.subTest(output=output):
                with self.assertRaisesRegex(
                    production_compile.ProductionCompileError,
                    "approved output",
                ):
                    production_compile.compile_production_source(
                        self.root,
                        "user",
                        Path("user/examples/hello.cc"),
                        output,
                    )

    def test_generated_source_has_one_exact_output(self):
        generated = self.root / "kernel/util/bin_programs_gen.cc"
        generated.write_bytes(b"void install(void) {}\n")
        isolated = self.root / "kernel/util/.frontier/bin_programs_gen.o"
        isolated.parent.mkdir()
        production_compile._validate_output_binding(
            self.root,
            "generated-install",
            generated,
            isolated,
        )
        with self.assertRaisesRegex(
            production_compile.ProductionCompileError,
            "approved output",
        ):
            production_compile.compile_production_source(
                self.root,
                "generated-install",
                Path("kernel/util/bin_programs_gen.cc"),
                Path("kernel/util/docs_programs_gen.o"),
            )


if __name__ == "__main__":
    unittest.main()
