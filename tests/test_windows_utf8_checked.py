"""Exercise checked Windows conversion logic with mocked APIs on either host."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tools import artifact_size_contract as contract
from tools import bootstrap_toolchain as bootstrap


ROOT = Path(__file__).resolve().parents[1]


class CheckedWindowsUtf8Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="cupid-checked-utf8-")
        cls.addClassCleanup(cls.temporary.cleanup)
        directory = Path(cls.temporary.name)
        cls.source = directory / "source"
        names = [
            "toolchain/path_encoding.cc", "toolchain/path_encoding.h",
            "toolchain/tests/windows_utf8_contract.cc",
            "toolchain/tests/windows_utf8_entry_contract.cc",
            "toolchain/hosted/i386-windows/windows_utf8.cc",
            "toolchain/hosted/i386-windows/runtime.cc",
            "toolchain/hosted/i386-windows/tool_start.asm",
            "toolchain/hosted/i386-windows/utf8_tool_start.asm",
            "toolchain/hosted/i386-windows/utf8_publication_start.asm",
            "toolchain/hosted/i386-windows/utf8_cupidbuild_start.asm",
            "toolchain/hosted/i386-linux/runtime.cc",
            "toolchain/hosted/i386-linux/start.asm",
        ]
        names.extend(path.relative_to(ROOT).as_posix() for path in
                     (ROOT / "toolchain/hosted/i386-linux/include").glob("*.h"))
        for name in names:
            target = cls.source / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, target)
        host = "i386-windows" if os.name == "nt" else "i386-linux"
        cls.seed = bootstrap.freeze_seed_inputs(
            ROOT / "bootstrap/seeds" / host / "manifest.json", directory / "seed")
        cls.addClassCleanup(bootstrap.require_live_seed_inputs, cls.seed)
        cls.runner = bootstrap.ToolRunner(cls.source)
        objects = {}
        for name, logical, definitions in (
            ("runtime", "toolchain/hosted/" + host + "/runtime.cc",
             ["calloc=runtime_calloc"] + (["_WIN32=1"] if os.name == "nt" else [])),
            ("codec", "toolchain/path_encoding.cc", []),
            ("contract", "toolchain/tests/windows_utf8_contract.cc", ["_WIN32=1"]),
        ):
            obj = cls.source / (name + ".o")
            contract._compile_source(cls.seed, cls.runner, cls.source, logical,
                                     obj, definitions, True, 600)
            objects[name] = obj
        assembly = "tool_start.asm" if os.name == "nt" else "start.asm"
        objects["start"] = cls.source / "start.o"
        contract._run_checked_tool(cls.seed, cls.runner, "cupidasm", [
            "-f", "elf32", cls.source / "toolchain/hosted" / host / assembly,
            "-o", objects["start"]], "UTF-8 contract startup", 180)
        cls.program = cls.source / ("contract.exe" if os.name == "nt" else "contract")
        order = ["start", "runtime", "codec", "contract"]
        if os.name == "nt":
            arguments = bootstrap._windows_link_arguments("cupidc", cls.program,
                                                          objects, order)
            contract._run_checked_tool(cls.seed, cls.runner, "cupidld", arguments,
                                       "UTF-8 contract link", 180)
            bootstrap._validate_static_i386_pe32(
                cls.program, 0x00401000, bootstrap.WINDOWS_TOOL_IMPORTS)
        else:
            contract._link_contract(cls.seed, cls.runner,
                                    [objects[name] for name in order],
                                    cls.program, False, 180)
            cls.program.chmod(0o700)

    def test_allocation_unicode_api_error_and_cleanup_contract(self):
        result = subprocess.run([str(self.program)], capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout + result.stderr, b"")

    def test_conflicting_roles_reject_and_preserve_output(self):
        output = self.source / "preserved.o"
        output.write_bytes(b"previous output")
        with self.assertRaisesRegex(contract.ArtifactSizeContractError, "CT900000E"):
            contract._compile_source(
                self.seed, self.runner, self.source,
                "toolchain/hosted/i386-windows/windows_utf8.cc", output,
                ["_WIN32=1", "CUPID_WINDOWS_BUILD=1", "CUPID_WINDOWS_PUBLICATION=1"],
                True, 180)
        self.assertEqual(output.read_bytes(), b"previous output")

    def test_each_role_compiles(self):
        for role, definitions in (
            ("ordinary", []),
            ("publication", ["CUPID_WINDOWS_PUBLICATION=1"]),
            ("build", ["CUPID_WINDOWS_BUILD=1"]),
        ):
            with self.subTest(role=role):
                contract._compile_source(
                    self.seed, self.runner, self.source,
                    "toolchain/hosted/i386-windows/windows_utf8.cc",
                    self.source / (role + ".o"), ["_WIN32=1", *definitions],
                    True, 180)

    @unittest.skipUnless(os.name == "nt", "executes the Windows wide API startup")
    def test_wide_startup_arguments_and_file_io(self):
        objects = {"codec": self.source / "codec.o"}
        for name, logical, definitions in (
            ("wide-runtime", "toolchain/hosted/i386-windows/runtime.cc",
             ["_WIN32=1", "CUPID_WINDOWS_UTF8=1"]),
            ("wide-adapter", "toolchain/hosted/i386-windows/windows_utf8.cc", ["_WIN32=1"]),
            ("wide-caller", "toolchain/tests/windows_utf8_entry_contract.cc", ["_WIN32=1"]),
        ):
            objects[name] = self.source / (name + ".o")
            contract._compile_source(self.seed, self.runner, self.source, logical,
                                     objects[name], definitions, True, 600)
        objects["wide-start"] = self.source / "wide-start.o"
        contract._run_checked_tool(self.seed, self.runner, "cupidasm", [
            "-f", "elf32", self.source / "toolchain/hosted/i386-windows/utf8_tool_start.asm",
            "-o", objects["wide-start"]], "wide startup", 180)
        imports = bootstrap._windows_utf8_imports("cupidc")
        program = self.source / "wide-entry.exe"
        arguments = ["-m", "i386pe", "--text-address", "0x00401000", "--entry", "_start"]
        for library, names in imports:
            for name in names:
                arguments.extend(["--import", "__imp_" + name + "=" + library + ":" + name])
        arguments.extend(["-o", program, *[objects[name] for name in
                          ("wide-start", "wide-runtime", "codec", "wide-adapter", "wide-caller")]])
        contract._run_checked_tool(self.seed, self.runner, "cupidld", arguments,
                                   "wide entry link", 180)
        bootstrap._validate_static_i386_pe32(program, 0x00401000, imports)
        directory = self.source / "\u6771\u4eac \U0001f63a"
        directory.mkdir()
        executable = directory / "\u00e9 tool.exe"
        shutil.copyfile(program, executable)
        input_name, output_name = "\u6771\u4eac input.bin", "\U0001f63a output.bin"
        (directory / input_name).write_bytes(b"data")
        result = subprocess.run([
            str(executable), "caf\u00e9", "\U0001f63a space", 'a "quoted" path\\',
            input_name, output_name], cwd=directory, capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((directory / output_name).read_bytes(), b"data")
        self.assertEqual({path.name for path in directory.iterdir()},
                         {executable.name, input_name, output_name})

    @unittest.skipUnless(os.name == "nt", "links PE32 wide-API profiles")
    def test_publication_and_build_startups_match_exact_profiles(self):
        objects = {"codec": self.source / "codec.o"}
        for name, logical, definitions in (
            ("profile-runtime", "toolchain/hosted/i386-windows/runtime.cc",
             ["_WIN32=1", "CUPID_WINDOWS_UTF8=1"]),
            ("profile-caller", "toolchain/tests/windows_utf8_entry_contract.cc", ["_WIN32=1"]),
        ):
            objects[name] = self.source / (name + ".o")
            contract._compile_source(self.seed, self.runner, self.source, logical,
                                     objects[name], definitions, True, 600)
        for name in ("tool", "publication", "cupidbuild"):
            objects[name] = self.source / ("profile-" + name + ".o")
            contract._run_checked_tool(self.seed, self.runner, "cupidasm", [
                "-f", "elf32", self.source / ("toolchain/hosted/i386-windows/utf8_" + name + "_start.asm"),
                "-o", objects[name]], name + " wide wrappers", 180)
        for role, definition, starts in (
            ("cupidasm", "CUPID_WINDOWS_PUBLICATION=1", ["tool", "publication"]),
            ("cupidbuild", "CUPID_WINDOWS_BUILD=1", ["tool", "publication", "cupidbuild"]),
        ):
            with self.subTest(role=role):
                adapter = self.source / ("profile-adapter-" + role + ".o")
                contract._compile_source(
                    self.seed, self.runner, self.source,
                    "toolchain/hosted/i386-windows/windows_utf8.cc", adapter,
                    ["_WIN32=1", definition], True, 180)
                imports = bootstrap._windows_utf8_imports(role)
                program = self.source / ("profile-" + role + ".exe")
                arguments = ["-m", "i386pe", "--text-address", "0x00401000", "--entry", "_start"]
                for library, names in imports:
                    for name in names:
                        arguments.extend(["--import", "__imp_" + name + "=" + library + ":" + name])
                arguments.extend(["-o", program, *[objects[name] for name in starts],
                                  objects["profile-runtime"], objects["codec"], adapter,
                                  objects["profile-caller"]])
                contract._run_checked_tool(self.seed, self.runner, "cupidld", arguments,
                                           role + " profile link", 180)
                bootstrap._validate_static_i386_pe32(program, 0x00401000, imports)
                for rejected in (bootstrap._windows_imports(role),
                                 bootstrap._windows_utf8_imports("cupidc")):
                    with self.assertRaises(bootstrap.BootstrapError):
                        bootstrap._validate_static_i386_pe32(program, 0x00401000, rejected)

    def test_unknown_import_role_is_rejected(self):
        with self.assertRaisesRegex(bootstrap.BootstrapError, "unknown Windows UTF-8 tool role"):
            bootstrap._windows_utf8_imports("unknown")


if __name__ == "__main__":
    unittest.main()
