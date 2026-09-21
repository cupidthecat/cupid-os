import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path
from tests.test_cupidbuild_compile_kernel import ROOT, SEED, SUFFIX, checked_run
from tools.bootstrap_toolchain import _windows_build_plan, _windows_link_arguments
from tools.cupidc_kernel_compile import KERNEL_I386_ARGUMENTS, validate_i386_relocatable_bytes
from tools.cupidc_production_compile import GENERATED_INSTALL_SOURCES, GENERATED_INCLUDE_CLOSURE, compile_production_source

SOURCE = GENERATED_INSTALL_SOURCES[0]


class CupidBuildCompileProductionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='.compile-production-', dir=ROOT / 'toolchain')
        cls.addClassCleanup(cls.build.cleanup)
        directory = Path(cls.build.name)
        cls.directory = directory
        suffix = '.exe' if os.name == 'nt' else ''
        cls.cli, cls.compiler = directory / ('cupidbuild' + suffix), directory / ('cupidc' + suffix)
        checked_run(['make', '-C', ROOT / 'toolchain', f'BUILD_DIR={directory.name}',
                     'CPPFLAGS=-DCUPIDBUILD_PUBLICATION_RACE_TEST',
                     f'{directory.name}/cupidbuild{suffix}', f'{directory.name}/cupidc{suffix}'])
        checked_run(['make', '-C', ROOT, '-o', 'FORCE', *GENERATED_INSTALL_SOURCES,
                     'PYTHON=python' if os.name == 'nt' else 'PYTHON=python3'], timeout=240)
        cls.contents = {name: (ROOT / name).read_bytes()
                        for name in (*GENERATED_INSTALL_SOURCES, *GENERATED_INCLUDE_CLOSURE)}

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='cupid-production-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        shutil.copytree(SEED, self.root / 'seed')
        for logical, contents in self.contents.items():
            path = self.root / logical
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(contents)
        self.output = self.root / Path(SOURCE).with_suffix('.o')

    def run_compile(self, source=SOURCE, output=None, env=None, extra=(), cli=None):
        return subprocess.run(list(map(str, [cli or self.cli, 'compile-production', '--root', self.root,
                              '--seed-manifest', 'seed/manifest.json', '--source', source,
                              '--output', output or Path(source).with_suffix('.o').as_posix()])) + list(extra),
                              capture_output=True, text=True, timeout=190, env=env)

    def previous(self):
        self.output.write_bytes(b'previous object')
        return self.output.stat().st_mtime_ns

    def assert_clean(self):
        self.assertEqual([str(p) for p in self.root.rglob('*') if p.name.startswith('.cupidbuild-')
                          or p.name.endswith('.cupidbuild.lock')], [])

    def assert_preserved(self, timestamp):
        self.assertEqual(self.output.read_bytes(), b'previous object')
        self.assertEqual(self.output.stat().st_mtime_ns, timestamp)
        self.assert_clean()

    def race(self, phase, mutate):
        ready, resume = self.root / 'ready', self.root / 'resume'
        ready.unlink(missing_ok=True)
        resume.unlink(missing_ok=True)
        env = os.environ.copy()
        env.update(CUPIDBUILD_PUBLICATION_TEST_PHASE=phase,
                   CUPIDBUILD_PUBLICATION_TEST_READY=str(ready),
                   CUPIDBUILD_PUBLICATION_TEST_RESUME=str(resume))
        errors = []
        def worker():
            try:
                deadline = time.monotonic() + 30
                while not ready.exists():
                    if time.monotonic() >= deadline:
                        raise AssertionError('checkpoint was not reached: ' + phase)
                    time.sleep(.001)
                mutate()
            except Exception as error:
                errors.append(error)
            finally:
                resume.write_bytes(b'continue')
        thread = threading.Thread(target=worker)
        thread.start()
        try:
            result = self.run_compile(env=env)
        finally:
            thread.join(timeout=35)
        self.assertFalse(thread.is_alive())
        self.assertEqual(errors, [])
        return result

    def test_real_three_source_parity_with_native_checked_and_wrapper(self):
        for source in GENERATED_INSTALL_SOURCES:
            with self.subTest(source=source):
                for compiler, name in ((self.compiler, 'ordinary.o'),
                                       (self.root / 'seed' / ('cupidc' + SUFFIX), 'checked.o')):
                    checked_run([compiler, '--root', self.root, '-c', '/' + source,
                                 '-o', '/' + name, *KERNEL_I386_ARGUMENTS])
                expected = (self.root / 'ordinary.o').read_bytes()
                self.assertEqual((self.root / 'checked.o').read_bytes(), expected)
                output = self.root / Path(source).with_suffix('.o')
                compile_production_source(self.root, 'generated-install', Path(source), output,
                    manifest=self.root / 'seed/manifest.json', tool_mode='checked-seed')
                self.assertEqual(output.read_bytes(), expected)
                output.write_bytes(b'previous object')
                result = self.run_compile(source)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(output.read_bytes(), expected)
                validate_i386_relocatable_bytes(expected)
        self.assert_clean()

    def test_exact_closures_match_python(self):
        source = (ROOT / 'toolchain/cupidbuild.cc').read_text()
        block = source.split('cupidbuild_compile_production_closures[] = {', 1)[1].split('\n};', 1)[0]
        matches = re.findall(r'\{"([^"]+)", \{(.*?)\}, (\d+)u\}', block, re.S)
        actual = {name: tuple(re.findall(r'"([^"]+)"', inputs)) for name, inputs, _ in matches}
        self.assertEqual(actual, {name: tuple(sorted((name, *GENERATED_INCLUDE_CLOSURE)))
                                 for name in GENERATED_INSTALL_SOURCES})
        self.assertTrue(all(int(count) == 6 for _, _, count in matches))

    def test_unchanged_output_keeps_timestamp_and_all_headers_remain_required(self):
        result = self.run_compile()
        self.assertEqual(result.returncode, 0, result.stderr)
        previous = self.output.read_bytes()
        os.utime(self.output, ns=(1_600_000_000_000_000_000,) * 2)
        before = self.output.stat().st_mtime_ns
        result = self.run_compile()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.output.stat().st_mtime_ns, before)
        for header in GENERATED_INCLUDE_CLOSURE:
            with self.subTest(header=header):
                path = self.root / header
                original = path.read_bytes()
                path.unlink()
                result = self.run_compile()
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(self.output.read_bytes(), previous)
                self.assertEqual(self.output.stat().st_mtime_ns, before)
                path.write_bytes(original)
        self.assert_clean()

    def test_invalid_requests_and_flags(self):
        for source, output, extra in (
                ('user/examples/hello.cc', 'user/build/hello.o', ()),
                ('kernel/cpu/ksyms_data.cc', None, ()), (SOURCE[:-2] + 'c', None, ()),
                ('../' + SOURCE, None, ()), (SOURCE, 'kernel/util/other.o', ()),
                (SOURCE, None, ('--gnu',)), (SOURCE, None, ('--timeout', '1')),
                (SOURCE, None, ('-D', 'DEBUG=0'))):
            with self.subTest(source=source, output=output, extra=extra):
                result = self.run_compile(source, output, extra=extra)
                self.assertNotEqual(result.returncode, 0)
                self.assertTrue(result.stderr)
                self.assert_clean()

    def test_missing_source_invalid_c_and_unbundled_live_header(self):
        path = self.root / SOURCE
        for contents in (None, b'int broken = ;\n', b'#include "live-only.h"\nint value;\n'):
            with self.subTest(contents=contents):
                before = self.previous()
                if contents is None:
                    path.unlink()
                else:
                    path.write_bytes(contents)
                (path.parent / 'live-only.h').write_bytes(b'int live;\n')
                result = self.run_compile()
                self.assertNotEqual(result.returncode, 0)
                self.assert_preserved(before)

    def test_kernel_profile_nested_includes_and_logical_file_names(self):
        (self.root / SOURCE).write_bytes(b'#include "../core/types.h"\n#ifndef DEBUG\n#error missing DEBUG\n#endif\n'
            b'#ifndef __GNUC__\n#error missing GNU\n#endif\nchar *logical = __FILE__;\nint value;\n')
        checked_run([self.compiler, '--root', self.root, '-c', '/' + SOURCE,
                     '-o', '/ordinary.o', *KERNEL_I386_ARGUMENTS])
        result = self.run_compile()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.output.read_bytes(), (self.root / 'ordinary.o').read_bytes())
        self.assertIn(('/' + SOURCE).encode(), self.output.read_bytes())
        self.assert_clean()

    def test_source_header_seed_drift_before_launch_and_after_install(self):
        for phase in ('before-tool-launch', 'after-install'):
            for logical in (SOURCE, *GENERATED_INCLUDE_CLOSURE, 'seed/manifest.json'):
                with self.subTest(phase=phase, logical=logical):
                    path = self.root / logical
                    original = path.read_bytes()
                    before = self.previous()
                    result = self.race(phase, lambda: path.write_bytes(original + b'\n'))
                    self.assertNotEqual(result.returncode, 0, result.stderr)
                    self.assert_preserved(before)
                    path.write_bytes(original)

    def test_distinct_generated_outputs_can_publish_concurrently(self):
        results = []
        result = self.race('before-mutation', lambda: results.append(self.run_compile(GENERATED_INSTALL_SOURCES[1])))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0].returncode, 0, results[0].stderr)
        self.assert_clean()

    def test_live_lock_and_input_alias_preserve_files(self):
        before = self.previous()
        lock = self.output.with_name(self.output.name + '.cupidbuild.lock')
        lock.write_text(f'{os.getpid()}\n')
        result = self.run_compile()
        self.assertNotEqual(result.returncode, 0)
        lock.unlink()
        self.assert_preserved(before)
        self.output.unlink()
        original = (self.root / SOURCE).read_bytes()
        os.link(self.root / SOURCE, self.output)
        result = self.run_compile()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.output.read_bytes(), original)
        self.assert_clean()

    def test_linked_header_is_rejected(self):
        header = self.root / GENERATED_INCLUDE_CLOSURE[0]
        saved = self.root / 'saved-header'
        header.rename(saved)
        try:
            header.symlink_to(saved)
        except OSError as error:
            saved.rename(header)
            self.skipTest(str(error))
        before = self.previous()
        result = self.run_compile()
        self.assertNotEqual(result.returncode, 0)
        self.assert_preserved(before)


    def test_successful_compiler_with_invalid_object_cannot_publish(self):
        directory = self.directory
        stub = directory / "invalid-compiler.cc"
        stub.write_text(r'''#include <stdio.h>
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
        plan = json.loads((ROOT / "bootstrap/seeds/i386-linux/manifest.json").read_text())["build_plan"]
        if os.name == "nt":
            plan = _windows_build_plan(plan)
        runtime = next(source for source in plan["sources"] if source["name"] == "runtime")
        order = ["start", "runtime", "invalid"]
        objects = {name: directory / ("invalid-" + name + ".o") for name in order}
        for name, source, gnu in (("invalid", "/" + stub.relative_to(ROOT).as_posix(), False),
                                  ("runtime", runtime["path"], True)):
            args = [self.compiler, "--root", ROOT, "-c", source, "-o",
                    "/" + objects[name].relative_to(ROOT).as_posix(), *plan["include_arguments"]]
            if gnu:
                args.append("--gnu")
            checked_run(args)
        start = ROOT / ("toolchain/hosted/i386-windows/tool_start.asm" if os.name == "nt"
                        else "toolchain/hosted/i386-linux/start.asm")
        checked_run([SEED / ("cupidasm" + SUFFIX), "-f", "elf32", start, "-o", objects["start"]])
        executable = directory / ("invalid-compiler" + SUFFIX)
        arguments = (_windows_link_arguments("cupidc", executable, objects, order) if os.name == "nt"
                     else ["-m", "elf_i386", "--text-address", "0x08048000", "--entry", "_start",
                           "-o", executable, *[objects[name] for name in order]])
        checked_run([SEED / ("cupidld" + SUFFIX), *arguments])
        contents = executable.read_bytes()
        seed_compiler = self.root / "seed" / ("cupidc" + SUFFIX)
        seed_compiler.write_bytes(contents)
        manifest = self.root / "seed/manifest.json"
        document = json.loads(manifest.read_text())
        entry = next(item for item in document["artifacts"] if item["name"] == "cupidc")
        entry.update(size=len(contents), sha256=hashlib.sha256(contents).hexdigest())
        manifest.write_text(json.dumps(document, indent=2) + "\n")
        before = self.previous()
        result = self.run_compile()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("compiler object validation failed", result.stderr)
        self.assert_preserved(before)

    def test_replaced_header_directory_is_rejected(self):
        for phase in ("before-tool-launch", "after-install"):
            with self.subTest(phase=phase):
                before = self.previous()
                directory = self.root / "drivers"
                displaced = self.root / ("saved-drivers-" + phase)
                def mutate():
                    directory.rename(displaced)
                    shutil.copytree(displaced, directory)
                result = self.race(phase, mutate)
                self.assertNotEqual(result.returncode, 0, result.stderr)
                self.assert_preserved(before)

    def test_cupid_built_coordinator_compiles_all_three_generated_sources(self):
        plan = json.loads((ROOT / "bootstrap/seeds/i386-linux/manifest.json").read_text())["build_plan"]
        if os.name == "nt":
            plan = _windows_build_plan(plan)
        order = plan["links"]["cupidbuild"]
        objects = {name: self.directory / ("target-" + name + ".o") for name in order}
        for source in plan["sources"]:
            if source["name"] not in order:
                continue
            output = objects[source["name"]]
            args = ["--root", ROOT, "-c", source["path"], "-o",
                    "/" + output.relative_to(ROOT).as_posix(), *plan["include_arguments"]]
            for definition in source.get("definitions", []):
                args += ["-D", definition]
            if source["gnu_extensions"]:
                args += ["--gnu"]
            checked_run([self.compiler, *args])
            expected = output.read_bytes()
            checked_run([SEED / ("cupidc" + SUFFIX), *args])
            self.assertEqual(output.read_bytes(), expected, source["path"])
        assembly = list(plan.get("assembly_sources", []))
        assembly.append({"name": "start", "path": "toolchain/hosted/i386-windows/tool_start.asm"
                         if os.name == "nt" else "toolchain/hosted/i386-linux/start.asm"})
        for source in assembly:
            if source["name"] in objects:
                checked_run([SEED / ("cupidasm" + SUFFIX), "-f", "elf32",
                             ROOT / source["path"].lstrip("/"), "-o", objects[source["name"]]])
        coordinator = self.directory / ("target-cupidbuild" + SUFFIX)
        args = (_windows_link_arguments("cupidbuild", coordinator, objects, order)
                if os.name == "nt" else ["-m", "elf_i386", "--text-address", "0x08048000",
                                        "--entry", "_start", "-o", coordinator,
                                        *[objects[name] for name in order]])
        checked_run([SEED / ("cupidld" + SUFFIX), *args])
        if os.name != "nt":
            coordinator.chmod(0o755)
        for source in GENERATED_INSTALL_SOURCES:
            with self.subTest(source=source):
                result = self.run_compile(source, cli=coordinator)
                self.assertEqual(result.returncode, 0, result.stderr)
                output = self.root / Path(source).with_suffix(".o")
                validate_i386_relocatable_bytes(output.read_bytes())
        self.assert_clean()
        from tools.bootstrap_toolchain import (
            Stage, freeze_seed_inputs, _check_cupidbuild_compile_production_behavior,
        )
        class NativeRunner:
            def run(self, executable, arguments, timeout):
                return subprocess.run([str(executable), *map(str, arguments)],
                                      capture_output=True, text=True, timeout=timeout)
        inputs = freeze_seed_inputs(self.root / "seed/manifest.json", self.root / "stage-seed")
        stage = Stage({}, {**inputs.tools, "cupidbuild": coordinator})
        behavior = self.root / "paired-behavior"
        behavior.mkdir()
        _check_cupidbuild_compile_production_behavior(
            NativeRunner(), behavior, stage, stage, inputs, "contract ",
        )


if __name__ == '__main__':
    unittest.main()
