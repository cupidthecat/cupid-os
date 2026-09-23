import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path

from tools.bootstrap_toolchain import (
    _candidate_build_plan,
    SeedInputs, Stage, _check_cupidbuild_compile_kernel_behavior,
    _windows_build_plan, _windows_link_arguments,
)
from tests.test_cupidc_source_bundle import active_input_bytes
from tools.cupidc_kernel_compile import (
    APPROVED_KERNEL_COMPILE_SOURCES,
    FROZEN_KERNEL_INPUT_CLOSURES,
    KERNEL_I386_ARGUMENTS,
    validate_i386_relocatable_bytes,
)


ROOT = Path(__file__).resolve().parents[1]
SUFFIX = ".exe" if os.name == "nt" else ".elf"
SEED = ROOT / "bootstrap/seeds" / ("i386-windows" if os.name == "nt" else "i386-linux")


def checked_run(arguments, **kwargs):
    result = subprocess.run([str(arg) for arg in arguments], capture_output=True, text=True,
                            timeout=kwargs.pop("timeout", 180), **kwargs)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    return result


def build_target_compiler(directory, native_compiler):
    plan = json.loads((ROOT / "bootstrap/seeds/i386-linux/manifest.json").read_text())["build_plan"]
    if os.name == "nt":
        plan = _windows_build_plan(plan)
    order = plan["links"]["cupidc"]
    objects = {name: directory / ("target-" + name + ".o") for name in order}
    for source in plan["sources"]:
        name = source["name"]
        if name not in order:
            continue
        arguments = [native_compiler, "--root", ROOT, "-c", source["path"],
                     "-o", "/" + objects[name].relative_to(ROOT).as_posix(),
                     *plan["include_arguments"]]
        for definition in source.get("definitions", []):
            arguments += ["-D", definition]
        if source["gnu_extensions"]:
            arguments += ["--gnu"]
        checked_run(arguments)
    start = (ROOT / "toolchain/hosted/i386-windows/tool_start.asm" if os.name == "nt"
             else ROOT / "toolchain/hosted/i386-linux/start.asm")
    checked_run([SEED / ("cupidasm" + SUFFIX), "-f", "elf32", start, "-o", objects["start"]])
    compiler = directory / ("target-cupidc" + SUFFIX)
    arguments = (_windows_link_arguments("cupidc", compiler, objects, order) if os.name == "nt"
                 else ["-m", "elf_i386", "--text-address", "0x08048000", "--entry", "_start", "-o", compiler,
                       *[objects[name] for name in order]])
    checked_run([SEED / ("cupidld" + SUFFIX), *arguments])
    if os.name != "nt":
        compiler.chmod(0o755)
    return compiler, objects["cupidc_main"]


class CupidBuildCompileKernelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix=".compile-kernel-", dir=ROOT / "toolchain")
        cls.addClassCleanup(cls.build.cleanup)
        directory = Path(cls.build.name)
        suffix = ".exe" if os.name == "nt" else ""
        cls.cli = directory / ("cupidbuild" + suffix)
        cls.native_compiler = directory / ("cupidc" + suffix)
        checked_run(["make", "-C", ROOT / "toolchain", f"BUILD_DIR={directory.name}",
                     "CPPFLAGS=-DCUPIDBUILD_PUBLICATION_RACE_TEST",
                     f"{directory.name}/cupidbuild{suffix}", f"{directory.name}/cupidc{suffix}"])
        cls.compiler, cls.driver_object = build_target_compiler(directory, cls.native_compiler)
        cls.target_directory = directory
        harness = directory / "validate.cc"
        harness.write_text('''#include "cupidbuild.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
  FILE *file;
  long size;
  unsigned char *bytes;
  int valid;
  if (argc != 2 || (file = fopen(argv[1], "rb")) == NULL) return 2;
  if (fseek(file, 0, SEEK_END) || (size = ftell(file)) < 0 ||
      fseek(file, 0, SEEK_SET)) { fclose(file); return 2; }
  bytes = (unsigned char *)malloc((size_t)size + 1);
  if (!bytes) { fclose(file); return 2; }
  if (fread(bytes, 1, (size_t)size, file) != (size_t)size) {
    free(bytes); fclose(file); return 2;
  }
  fclose(file);
  valid = cupidbuild_validate_compiler_object_bytes(bytes, (size_t)size);
  free(bytes);
  return valid ? 0 : 1;
}
''')
        cls.validator = directory / ("validate" + suffix)
        checked_run(["clang" if os.name == "nt" else "cc", "-I", ROOT / "toolchain",
                     "-x", "c", harness, "-x", "none",
                     *[directory / (name + ".o") for name in
                       ("ctool", "ctool_host", "elf32", "cupidbuild_host", "cupidbuild",
                        "seed_manifest", "seed_release", "contract_parse_internal")],
                     *(["-lntdll"] if os.name == "nt" else []), "-o", cls.validator])

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cupid-compile-kernel-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.seed = self.root / "seed"
        shutil.copytree(SEED, self.seed)
        self.manifest = self.seed / "manifest.json"
        self.replace_compiler(self.compiler.read_bytes())

    def replace_compiler(self, contents):
        path = self.seed / ("cupidc" + SUFFIX)
        path.write_bytes(contents)
        document = json.loads(self.manifest.read_text())
        entry = next(item for item in document["artifacts"] if item["name"] == "cupidc")
        entry.update(size=len(contents), sha256=hashlib.sha256(contents).hexdigest())
        self.manifest.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n")

    def closure(self, source="kernel/cpu/ksyms_data.cc", contents=None):
        for logical in (source, *FROZEN_KERNEL_INPUT_CLOSURES[source]):
            destination = self.root / logical
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(active_input_bytes(logical))
        if contents is not None:
            (self.root / source).write_bytes(contents)
        return self.root / (source[:-2] + "o")

    def run_compile(self, source="kernel/cpu/ksyms_data.cc", output=None, env=None, cli=None):
        return subprocess.run(
            [str(cli or self.cli), "compile-kernel", "--root", str(self.root),
             "--seed-manifest", "seed/manifest.json", "--source", source,
             "--output", output or source[:-2] + "o"],
            capture_output=True, text=True,
            timeout=610 if source == "kernel/cpu/ksyms_data.cc" else 190, env=env,
        )

    def assert_clean(self):
        residue = [p for p in self.root.rglob("*")
                   if p.name.startswith(".cupidbuild-") or p.name.endswith(".cupidbuild.lock")]
        self.assertEqual(residue, [])

    def test_current_driver_compiles_itself_byte_for_byte(self):
        directory = Path(self.build.name)
        output = directory / "driver-self.o"
        arguments = ["--root", ROOT, "-c", "/toolchain/cupidc_main.cc", "-I", "/toolchain",
                     "--include-angle", "/toolchain/hosted/i386-linux/include",
                     "-o", "/" + output.relative_to(ROOT).as_posix()]
        if os.name == "nt":
            arguments += ["-D", "_WIN32=1"]
        checked_run([self.compiler, *arguments])
        self.assertEqual(output.read_bytes(), self.driver_object.read_bytes())
        checked_run([SEED / ("cupidc" + SUFFIX), *arguments])
        self.assertEqual(output.read_bytes(), self.driver_object.read_bytes())

    def test_all_kernel_transactions_match_the_ordinary_compiler(self):
        self.assertEqual(tuple(sorted(FROZEN_KERNEL_INPUT_CLOSURES)),
                         APPROVED_KERNEL_COMPILE_SOURCES)
        self.assertEqual(len(FROZEN_KERNEL_INPUT_CLOSURES), 157)
        self.assertEqual(len(FROZEN_KERNEL_INPUT_CLOSURES["kernel/lang/as.cc"]) + 1, 79)
        self.assertEqual(len(FROZEN_KERNEL_INPUT_CLOSURES["kernel/lang/cupidc.cc"]) + 1, 90)
        for source in FROZEN_KERNEL_INPUT_CLOSURES:
            with self.subTest(source=source):
                output = self.closure(source)
                expected = self.root / "ordinary.o"
                checked_run([self.native_compiler, "-c", "/" + source, "-o", "/ordinary.o",
                             "--root", self.root, *KERNEL_I386_ARGUMENTS],
                            timeout=600 if source == "kernel/cpu/ksyms_data.cc" else 180)
                output.write_bytes(b"previous object")
                result = self.run_compile(source)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(output.read_bytes(), expected.read_bytes())
                validate_i386_relocatable_bytes(output.read_bytes())
                self.assert_clean()

    def test_missing_last_header_in_large_closures_preserves_output(self):
        for source, input_count in (("kernel/lang/as.cc", 79),
                                    ("kernel/lang/cupidc.cc", 90)):
            with self.subTest(source=source):
                inputs = sorted((source, *FROZEN_KERNEL_INPUT_CLOSURES[source]))
                self.assertEqual(len(inputs), input_count)
                self.assertEqual(inputs[-1], "toolchain/x86.h")
                output = self.closure(source)
                output.write_bytes(b"previous large-closure object")
                before = output.stat().st_mtime_ns
                (self.root / inputs[-1]).unlink()
                result = self.run_compile(source)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("closure cannot be captured", result.stderr)
                self.assertEqual(output.read_bytes(), b"previous large-closure object")
                self.assertEqual(output.stat().st_mtime_ns, before)
                self.assert_clean()

    def test_coordinator_sources_compile_identically_with_the_checked_seed(self):
        plan = json.loads((ROOT / "bootstrap/seeds/i386-linux/manifest.json").read_text())["build_plan"]
        plan = _candidate_build_plan(plan)
        if os.name == "nt":
            plan = _windows_build_plan(plan)
        for source in plan["sources"]:
            if source["name"] not in ("cupidbuild", "cupidbuild_main", "cupidbuild_host",
                                      "publication_runtime", "seed_manifest", "seed_release",
                                      "contract_parse_internal"):
                continue
            with self.subTest(source=source["path"]):
                output = self.target_directory / ("target-" + source["name"] + ".o")
                args = ["--root", ROOT, "-c", source["path"], "-o",
                        "/" + output.relative_to(ROOT).as_posix(), *plan["include_arguments"]]
                for definition in source.get("definitions", []):
                    args += ["-D", definition]
                if source["gnu_extensions"]:
                    args += ["--gnu"]
                checked_run([self.native_compiler, *args])
                expected = output.read_bytes()
                checked_run([SEED / ("cupidc" + SUFFIX), *args])
                self.assertEqual(output.read_bytes(), expected)

        order = plan["links"]["cupidbuild"]
        objects = {name: self.target_directory / ("target-" + name + ".o") for name in order}
        for source in plan.get("assembly_sources", []):
            name = source["name"]
            if name in objects and not objects[name].exists():
                checked_run([SEED / ("cupidasm" + SUFFIX), "-f", "elf32",
                             ROOT / source["path"].lstrip("/"), "-o", objects[name]])
        coordinator = self.target_directory / ("target-cupidbuild" + SUFFIX)
        arguments = (_windows_link_arguments("cupidbuild", coordinator, objects, order)
                     if os.name == "nt" else
                     ["-m", "elf_i386", "--text-address", "0x08048000", "--entry", "_start",
                      "-o", coordinator, *[objects[name] for name in order]])
        checked_run([SEED / ("cupidld" + SUFFIX), *arguments])
        if os.name != "nt":
            coordinator.chmod(0o755)
        output = self.closure(contents=b"int target_coordinator_value = 42;\n")
        result = self.run_compile(cli=coordinator)
        self.assertEqual(result.returncode, 0, result.stderr)
        previous = output.read_bytes()
        validate_i386_relocatable_bytes(previous)
        previous_mtime = output.stat().st_mtime_ns
        self.closure(contents=b"int broken = ;\n")
        result = self.run_compile(cli=coordinator)
        self.assertNotEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output.read_bytes(), previous)
        self.assertEqual(output.stat().st_mtime_ns, previous_mtime)
        self.assert_clean()

        class NativeRunner:
            def run(self, executable, arguments, timeout):
                return subprocess.run(
                    [str(executable), *map(str, arguments)],
                    capture_output=True, text=True, timeout=timeout,
                )

        document = json.loads(self.manifest.read_text())
        tools = {entry["name"]: self.seed / entry["file"]
                 for entry in document["artifacts"]}
        inputs = SeedInputs(
            document, self.manifest.read_bytes(),
            hashlib.sha256(self.manifest.read_bytes()).hexdigest(), self.manifest,
            tuple((name, path.read_bytes()) for name, path in tools.items()), tools,
        )
        stage = Stage({}, {**tools, "cupidbuild": coordinator})
        behavior = self.root / "paired-behavior"
        behavior.mkdir()
        _check_cupidbuild_compile_kernel_behavior(
            NativeRunner(), behavior, stage, stage, inputs, "contract ",
        )

    def test_compiler_error_and_missing_header_preserve_output(self):
        for missing_header in (False, True):
            with self.subTest(missing_header=missing_header):
                output = self.closure(contents=b'#include "ksyms.h"\nint broken( {\n')
                if missing_header:
                    (self.root / "kernel/cpu/ksyms.h").unlink()
                output.write_bytes(b"previous object")
                before = output.stat().st_mtime_ns
                result = self.run_compile()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("closure cannot be captured" if missing_header else "checked CupidC failed",
                              result.stderr)
                self.assertEqual(output.read_bytes(), b"previous object")
                self.assertEqual(output.stat().st_mtime_ns, before)
                self.assert_clean()

    def test_unchanged_object_keeps_timestamp_and_still_checks_inputs(self):
        output = self.closure(contents=b'#include "ksyms.h"\nint x;\n')
        result = self.run_compile()
        self.assertEqual(result.returncode, 0, result.stderr)
        previous = output.read_bytes()
        os.utime(output, ns=(1_600_000_000_000_000_000,) * 2)
        before = output.stat().st_mtime_ns
        result = self.run_compile()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output.read_bytes(), previous)
        self.assertEqual(output.stat().st_mtime_ns, before)
        self.assert_clean()

        (self.root / "kernel/cpu/ksyms.h").unlink()
        result = self.run_compile()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("closure cannot be captured", result.stderr)
        self.assertEqual(output.read_bytes(), previous)
        self.assertEqual(output.stat().st_mtime_ns, before)
        self.assert_clean()

    def test_promoted_seed_compiles_closed_inputs_and_preserves_failed_output(self):
        source = "kernel/cpu/ksyms_data.cc"
        output = self.closure(contents=b'#include "ksyms.h"\nint x;\n')
        self.replace_compiler((SEED / ("cupidc" + SUFFIX)).read_bytes())
        expected = self.root / "ordinary.o"
        checked_run([self.seed / ("cupidc" + SUFFIX), "-c", "/" + source,
                     "-o", "/ordinary.o", "--root", self.root,
                     *KERNEL_I386_ARGUMENTS])
        for cli in (self.cli, self.seed / ("cupidbuild" + SUFFIX)):
            with self.subTest(coordinator=cli.name):
                output.write_bytes(b"previous object")
                result = self.run_compile(cli=cli)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(output.read_bytes(), expected.read_bytes())
                before = output.stat().st_mtime_ns
                result = self.run_compile(cli=cli)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(output.stat().st_mtime_ns, before)
                self.assert_clean()
        previous = output.read_bytes()
        (self.root / "kernel/cpu/ksyms.h").unlink()
        result = self.run_compile(cli=self.seed / ("cupidbuild" + SUFFIX))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("closure cannot be captured", result.stderr)
        self.assertEqual(output.read_bytes(), previous)
        self.assertEqual(output.stat().st_mtime_ns, before)
        self.assert_clean()

    def test_successful_compiler_with_invalid_object_cannot_publish(self):
        directory = self.target_directory
        stub = directory / "invalid-compiler.cc"
        stub.write_text('''#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
  const char *root = "";
  const char *output = "";
  char path[8192];
  int index;
  FILE *file;
  for (index = 1; index + 1 < argc; index++) {
    if (strcmp(argv[index], "--root") == 0) root = argv[index + 1];
    if (strcmp(argv[index], "-o") == 0) output = argv[index + 1];
  }
  if (snprintf(path, sizeof(path), "%s%s", root, output) < 0) return 1;
  file = fopen(path, "wb");
  if (!file) return 1;
  if (fwrite("invalid ELF", 1, 11, file) != 11) { fclose(file); return 1; }
  return fclose(file) != 0;
}
''')
        stub_object = directory / "invalid-compiler.o"
        checked_run([self.native_compiler, "--root", ROOT, "-c",
                     "/" + stub.relative_to(ROOT).as_posix(), "-o",
                     "/" + stub_object.relative_to(ROOT).as_posix(),
                     "--include-angle", "/toolchain/hosted/i386-linux/include"])
        plan = json.loads((ROOT / "bootstrap/seeds/i386-linux/manifest.json").read_text())["build_plan"]
        if os.name == "nt":
            plan = _windows_build_plan(plan)
        order = plan["links"]["cupidc"]
        objects = {name: directory / ("target-" + name + ".o") for name in order}
        objects["cupidc_main"] = stub_object
        executable = directory / ("invalid-compiler" + SUFFIX)
        arguments = (_windows_link_arguments("cupidc", executable, objects, order) if os.name == "nt"
                     else ["-m", "elf_i386", "--text-address", "0x08048000", "--entry", "_start",
                           "-o", executable, *[objects[name] for name in order]])
        checked_run([SEED / ("cupidld" + SUFFIX), *arguments])
        self.replace_compiler(executable.read_bytes())
        output = self.closure(contents=b"int x;\n")
        output.write_bytes(b"previous object")
        before = output.stat().st_mtime_ns
        result = self.run_compile()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("compiler object validation failed", result.stderr)
        self.assertEqual(output.read_bytes(), b"previous object")
        self.assertEqual(output.stat().st_mtime_ns, before)
        self.assert_clean()

    def test_rejects_unapproved_sources_and_wrong_output_bindings(self):
        unapproved = (
            "kernel/doom/src/d_main.cc",
            "kernel/doom/dglibc.cc",
            "user/examples/hello.cc",
            "kernel/util/bin_programs_gen.cc",
            "kernel/util/demos_programs_gen.cc",
            "kernel/util/docs_programs_gen.cc",
            "kernel/cpu/ksyms_data.c",
        )
        cases = [(source, Path(source).with_suffix(".o").as_posix(),
                  "source has no approved frozen kernel closure")
                 for source in unapproved]
        cases.extend((
            ("kernel/cpu/ksyms_data.cc", "other.o", "kernel source and output binding differ"),
            ("../kernel/cpu/ksyms_data.cc", "other.o", "invalid kernel compile request"),
        ))
        for source, output, diagnostic in cases:
            with self.subTest(source=source, output=output):
                result = self.run_compile(source, output)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(diagnostic, result.stderr)
                self.assert_clean()

    def test_live_output_lock_and_input_alias_preserve_files(self):
        output = self.closure(contents=b"int x;\n")
        output.write_bytes(b"previous object")
        lock = output.with_name(output.name + ".cupidbuild.lock")
        lock.write_text(f"{os.getpid()}\n")
        result = self.run_compile()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(output.read_bytes(), b"previous object")
        self.assertTrue(lock.exists())
        lock.unlink()
        output.unlink()
        os.link(self.root / "kernel/cpu/ksyms_data.cc", output)
        result = self.run_compile()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(output.read_bytes(), b"int x;\n")
        self.assert_clean()

    def test_native_policy_matches_all_python_closures_and_profile(self):
        source = (ROOT / "toolchain/cupidbuild.cc").read_text()
        block = source.split("cupidbuild_compile_closures[] = {", 1)[1].split("\n};", 1)[0]
        matches = re.findall(r'\{"([^"]+)", \{(.*?)\}, (\d+)u\}', block, re.S)
        actual = {name: re.findall(r'"([^"]+)"', inputs) for name, inputs, _ in matches}
        expected = {name: sorted((name, *headers)) for name, headers in FROZEN_KERNEL_INPUT_CLOSURES.items()}
        self.assertEqual(len(matches), 157)
        self.assertEqual(tuple(sorted(actual)), APPROVED_KERNEL_COMPILE_SOURCES)
        self.assertEqual(max(int(count) for _, _, count in matches), 90)
        self.assertEqual(actual, expected)
        for name, _, count in matches:
            self.assertEqual(int(count), len(expected[name]))
        profile = source.split("cupidbuild_compile_profile[] = {", 1)[1].split("\n};", 1)[0]
        self.assertEqual(re.findall(r'"([^"]+)"', profile), list(KERNEL_I386_ARGUMENTS))

    def test_native_object_validator_keeps_compiler_relocation_policy(self):
        from tests.test_cupidc_kernel_compile import _valid_elf32_object, _data_only_elf32_object

        original = _valid_elf32_object()
        absolute = bytearray(original)
        struct.pack_into("<i", absolute, 52, 4)
        bad_pc = bytearray(original)
        struct.pack_into("<i", bad_pc, 56, 0)
        cases = [(original, True), (bytes(absolute), True), (bytes(bad_pc), False),
                 (_data_only_elf32_object(), True),
                 (_data_only_elf32_object(symbol_size=5), False),
                 (original[:51], False), (b"not ELF", False)]
        program_entry_size = bytearray(original)
        struct.pack_into("<H", program_entry_size, 42, 32)
        cases.append((bytes(program_entry_size), False))
        section_offset = struct.unpack_from("<I", original, 32)[0]
        section_count = struct.unpack_from("<H", original, 48)[0]
        for index in range(1, section_count):
            header = section_offset + index * 40
            section_type, = struct.unpack_from("<I", original, header + 4)
            if section_type == 9:
                rel_offset, = struct.unpack_from("<I", original, header + 16)
                target, = struct.unpack_from("<I", original, header + 28)
                empty_rel = bytearray(original)
                struct.pack_into("<I", empty_rel, header + 20, 0)
                cases.append((bytes(empty_rel), True))
                struct.pack_into("<I", empty_rel, section_offset + target * 40 + 4, 8)
                cases.append((bytes(empty_rel), False))
                for field, value in ((rel_offset, 0xFFFFFFFF), (rel_offset + 4, 0xFFFFFF01),
                                     (rel_offset + 4, 3), (header + 4, 4), (header + 28, 0)):
                    malformed = bytearray(original)
                    struct.pack_into("<I", malformed, field, value)
                    cases.append((bytes(malformed), False))
        for index, (image, valid) in enumerate(cases):
            with self.subTest(case=index):
                path = self.root / "validate.o"
                path.write_bytes(image)
                result = subprocess.run([str(self.validator), str(path)], capture_output=True,
                                        text=True, timeout=15)
                self.assertEqual(result.returncode, 0 if valid else 1, result.stderr)
                if valid:
                    validate_i386_relocatable_bytes(image)

    def test_header_drift_after_install_restores_previous_object(self):
        output = self.closure(contents=b"int x;\n")
        output.write_bytes(b"previous object")
        before = output.stat().st_mtime_ns
        ready = self.root / "ready"
        resume = self.root / "resume"
        errors = []

        def mutate():
            try:
                deadline = time.monotonic() + 30
                while not ready.exists():
                    if time.monotonic() >= deadline:
                        raise AssertionError("publication checkpoint was not reached")
                    time.sleep(0.001)
                header = self.root / "kernel/cpu/ksyms.h"
                header.write_bytes(header.read_bytes() + b"\n/* changed input */\n")
            except Exception as error:
                errors.append(error)
            finally:
                resume.write_bytes(b"continue")

        environment = os.environ.copy()
        environment.update(CUPIDBUILD_PUBLICATION_TEST_PHASE="after-install",
                           CUPIDBUILD_PUBLICATION_TEST_READY=str(ready),
                           CUPIDBUILD_PUBLICATION_TEST_RESUME=str(resume))
        thread = threading.Thread(target=mutate)
        thread.start()
        try:
            result = self.run_compile(env=environment)
        finally:
            thread.join(timeout=35)
        self.assertFalse(thread.is_alive())
        self.assertEqual(errors, [])
        self.assertNotEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output.read_bytes(), b"previous object")
        self.assertEqual(output.stat().st_mtime_ns, before)
        self.assert_clean()


if __name__ == "__main__":
    unittest.main()
