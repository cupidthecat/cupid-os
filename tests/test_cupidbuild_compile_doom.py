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

from tests.test_cupidbuild_compile_kernel import checked_run, ROOT, SEED, SUFFIX
from tools.bootstrap_toolchain import _windows_build_plan, _windows_link_arguments
from tools.cupidc_kernel_compile import (
    APPROVED_DOOM_COMPAT_SOURCES, APPROVED_DOOM_TREE_SOURCES,
    DOOM_COMPAT_I386_ARGUMENTS, DOOM_TREE_I386_ARGUMENTS,
    _profile_input_manifest, validate_i386_relocatable_bytes,
)

SOURCES = (*APPROVED_DOOM_COMPAT_SOURCES, *APPROVED_DOOM_TREE_SOURCES)
SOURCE = "kernel/doom/src/d_items.cc"


class CupidBuildCompileDoomTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix=".compile-doom-", dir=ROOT / "toolchain")
        cls.addClassCleanup(cls.build.cleanup)
        cls.directory = Path(cls.build.name)
        suffix = ".exe" if os.name == "nt" else ""
        cls.cli = cls.directory / ("cupidbuild" + suffix)
        cls.compiler = cls.directory / ("cupidc" + suffix)
        checked_run(["make", "-C", ROOT / "toolchain", f"BUILD_DIR={cls.directory.name}",
                     "CPPFLAGS=-DCUPIDBUILD_PUBLICATION_RACE_TEST -DCUPIDBUILD_PROFILE_DIRECTORY_RACE_TEST",
                     f"{cls.directory.name}/cupidbuild{suffix}",
                     f"{cls.directory.name}/cupidc{suffix}"])
        document = _profile_input_manifest(ROOT)
        cls.inputs = {item["path"]: (ROOT / item["path"]).read_bytes()
                      for item in document["inputs"]}
        cls.inputs.update({source: (ROOT / source).read_bytes() for source in SOURCES})

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cupid-compile-doom-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        shutil.copytree(SEED, self.root / "seed")
        for logical, contents in self.inputs.items():
            path = self.root / logical
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(contents)
        self.output = self.root / Path(SOURCE).with_suffix(".o")

    def arguments(self, source=SOURCE, output=None, cli=None):
        return list(map(str, [cli or self.cli, "compile-doom", "--root", self.root,
                             "--seed-manifest", "seed/manifest.json", "--source", source,
                             "--output", output or Path(source).with_suffix(".o").as_posix()]))

    def run_compile(self, source=SOURCE, output=None, cli=None, env=None, extra=()):
        return subprocess.run(self.arguments(source, output, cli) + list(extra),
                              capture_output=True, text=True, timeout=190, env=env)

    def assert_clean(self):
        self.assertEqual([str(p.relative_to(self.root)) for p in self.root.rglob("*")
                          if p.name.startswith(".cupidbuild-") or
                          p.name.endswith(".cupidbuild.lock")], [])

    def previous_output(self):
        self.output.write_bytes(b"previous object")
        return self.output.stat().st_mtime_ns

    def assert_preserved(self, timestamp):
        self.assertEqual(self.output.read_bytes(), b"previous object")
        self.assertEqual(self.output.stat().st_mtime_ns, timestamp)
        self.assert_clean()

    def run_race(self, phase, mutate, source=SOURCE):
        ready, resume = self.root / "ready", self.root / "resume"
        ready.unlink(missing_ok=True)
        resume.unlink(missing_ok=True)
        environment = os.environ.copy()
        launched = self.root / "compiler-launched"
        launched.unlink(missing_ok=True)
        if phase == "before-launch":
            launch_resume = self.root / "launch-resume"
            launch_resume.write_bytes(b"continue")
            environment.update(CUPIDBUILD_PUBLICATION_TEST_PHASE="after-tool-launch",
                               CUPIDBUILD_PUBLICATION_TEST_READY=str(launched),
                               CUPIDBUILD_PUBLICATION_TEST_RESUME=str(launch_resume))
            environment.update(CUPIDBUILD_PROFILE_TEST_DIRECTORY_READY=str(ready),
                               CUPIDBUILD_PROFILE_TEST_DIRECTORY_RESUME=str(resume))
        else:
            environment.update(CUPIDBUILD_PUBLICATION_TEST_PHASE=phase,
                               CUPIDBUILD_PUBLICATION_TEST_READY=str(ready),
                               CUPIDBUILD_PUBLICATION_TEST_RESUME=str(resume))
        errors = []

        def worker():
            try:
                deadline = time.monotonic() + 30
                while not ready.exists():
                    if time.monotonic() >= deadline:
                        raise AssertionError(f"{phase} checkpoint was not reached")
                    time.sleep(0.001)
                mutate()
            except Exception as error:
                errors.append(error)
            finally:
                resume.write_bytes(b"continue")

        thread = threading.Thread(target=worker)
        thread.start()
        try:
            result = self.run_compile(source, env=environment)
        finally:
            thread.join(timeout=35)
        self.assertFalse(thread.is_alive())
        self.assertEqual(errors, [])
        result.compiler_launched = launched.exists()
        return result

    def test_all_83_transactions_match_exact_python_profiles(self):
        self.assertEqual(len(SOURCES), 83)
        for source in SOURCES:
            with self.subTest(source=source):
                profile = (DOOM_COMPAT_I386_ARGUMENTS if source in APPROVED_DOOM_COMPAT_SOURCES
                           else DOOM_TREE_I386_ARGUMENTS)
                checked_run([self.compiler, "--root", self.root, "-c", "/" + source,
                             "-o", "/ordinary.o", *profile])
                output = self.root / Path(source).with_suffix(".o")
                output.write_bytes(b"previous object")
                result = self.run_compile(source)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(output.read_bytes(), (self.root / "ordinary.o").read_bytes())
                validate_i386_relocatable_bytes(output.read_bytes())
        self.assert_clean()

    def test_profiles_keep_doom_definitions_and_forced_include(self):
        header = self.root / "kernel/doom/dglibc_compat.h"
        header.write_bytes(b"#define FORCED_DOOM_HEADER 37\n")
        common = (b"#ifdef DEBUG\n#error DEBUG must be absent\n#endif\n"
                  b"#ifndef __GNUC__\n#error GNU profile is required\n#endif\n")
        tree = (common + b"#ifndef DOOM_PORT_CUPIDOS\n#error missing Doom definition\n#endif\n"
                b"int forced_value = FORCED_DOOM_HEADER;\n"
                b"char *save_directory = DEFAULT_SAVEGAMEDIR;\n")
        compat = (common + b"#ifdef DOOM_PORT_CUPIDOS\n#error tree flag leaked\n#endif\n"
                  b"#ifdef FORCED_DOOM_HEADER\n#error tree include leaked\n#endif\nint value;\n")
        for source, contents, profile in (
                (SOURCE, tree, DOOM_TREE_I386_ARGUMENTS),
                (APPROVED_DOOM_COMPAT_SOURCES[0], compat, DOOM_COMPAT_I386_ARGUMENTS)):
            with self.subTest(source=source):
                (self.root / source).write_bytes(contents)
                checked_run([self.compiler, "--root", self.root, "-c", "/" + source,
                             "-o", "/ordinary.o", *profile])
                result = self.run_compile(source)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual((self.root / Path(source).with_suffix(".o")).read_bytes(),
                                 (self.root / "ordinary.o").read_bytes())
        self.assert_clean()

    def test_invalid_requests_and_caller_flags(self):
        cases = [("kernel/core/kernel.cc", None, ()), (SOURCE, "elsewhere.o", ()),
                 ("../" + SOURCE, None, ()), (SOURCE[:-2] + "c", None, ()),
                 (SOURCE, None, ("--gnu",)), (SOURCE, None, ("-D", "DEBUG=1")),
                 (SOURCE, None, ("--timeout", "1")),
                 (SOURCE, None, ("--source", SOURCE))]
        for source, output, extra in cases:
            with self.subTest(source=source, output=output, extra=extra):
                result = self.run_compile(source, output, extra=extra)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertTrue(result.stderr)
                self.assert_clean()

    def test_unchanged_object_keeps_timestamp(self):
        result = self.run_compile()
        self.assertEqual(result.returncode, 0, result.stderr)
        previous = self.output.read_bytes()
        os.utime(self.output, ns=(1_600_000_000_000_000_000,) * 2)
        before = self.output.stat().st_mtime_ns
        result = self.run_compile()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.output.read_bytes(), previous)
        self.assertEqual(self.output.stat().st_mtime_ns, before)
        extra = self.root / "kernel/doom/extra.cc"
        extra.write_text("int extra;\n")
        result = self.run_compile()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.output.read_bytes(), previous)
        self.assertEqual(self.output.stat().st_mtime_ns, before)
        self.assert_clean()

    def test_compiler_error_and_missing_bundled_header_preserve_output(self):
        source = self.root / SOURCE
        original = source.read_bytes()
        for contents in (b"int broken = ;\n", b'#include "absent-bundle.h"\nint value;\n'):
            with self.subTest(contents=contents):
                source.write_bytes(contents)
                before = self.previous_output()
                result = self.run_compile()
                self.assertNotEqual(result.returncode, 0)
                self.assert_preserved(before)
        source.write_bytes(original)

    def test_unbundled_live_include_cannot_be_used_as_fallback(self):
        source = self.root / SOURCE
        source.write_bytes(b'#include "live-only.txt"\nint value = LIVE_VALUE;\n')
        (source.parent / "live-only.txt").write_bytes(b"#define LIVE_VALUE 7\n")
        checked_run([self.compiler, "--root", self.root, "-c", "/" + SOURCE,
                     "-o", "/ordinary.o", *DOOM_TREE_I386_ARGUMENTS])
        before = self.previous_output()
        result = self.run_compile()
        self.assertNotEqual(result.returncode, 0)
        self.assert_preserved(before)

    def test_extra_missing_and_legacy_sources_reject_before_publication(self):
        for logical, remove in (("kernel/doom/extra.cc", False),
                                ("kernel/doom/extra.c", False),
                                (APPROVED_DOOM_COMPAT_SOURCES[-1], True),
                                (APPROVED_DOOM_TREE_SOURCES[-1], True)):
            with self.subTest(logical=logical):
                path = self.root / logical
                original = path.read_bytes() if remove else None
                if remove:
                    path.unlink()
                else:
                    path.write_bytes(b"int extra;\n")
                before = self.previous_output()
                result = self.run_compile()
                self.assertNotEqual(result.returncode, 0)
                self.assert_preserved(before)
                if remove:
                    path.write_bytes(original)
                else:
                    path.unlink()

    def test_input_membership_and_content_drift_before_launch_and_after_install(self):
        header = "kernel/doom/dglibc_compat.h"
        other_source = APPROVED_DOOM_COMPAT_SOURCES[-1]
        changes = ((SOURCE, "change"), (other_source, "change"), (header, "change"),
                   (header, "remove"), (other_source, "remove"),
                   ("kernel/doom/new.h", "add"), ("kernel/doom/new.inc", "add"),
                   ("kernel/doom/new.cc", "add"), ("kernel/doom/new.c", "add"))
        for phase in ("before-launch", "after-install"):
            for logical, action in changes:
                with self.subTest(phase=phase, logical=logical, action=action):
                    path = self.root / logical
                    original = path.read_bytes() if path.exists() else None
                    before = self.previous_output()

                    def mutate():
                        if action == "remove":
                            path.unlink()
                        else:
                            path.write_bytes((original or b"") + b"\n/* changed */\n")

                    result = self.run_race(phase, mutate)
                    self.assertNotEqual(result.returncode, 0, result.stderr)
                    if phase == "before-launch":
                        self.assertFalse(result.compiler_launched)
                    self.assert_preserved(before)
                    if original is None:
                        path.unlink(missing_ok=True)
                    else:
                        path.write_bytes(original)

    def test_unrelated_output_writes_are_allowed_at_each_boundary(self):
        for phase in ("before-launch", "before-mutation", "after-install"):
            with self.subTest(phase=phase):
                self.previous_output()

                def mutate():
                    for logical in ("kernel/doom/src/other.o", "kernel/core/other.o",
                                    "toolchain/other.o", "drivers/other.o"):
                        path = self.root / logical
                        path.write_bytes(b"unrelated object")
                        renamed = path.with_name("renamed.o")
                        path.rename(renamed)
                        renamed.unlink()

                result = self.run_race(phase, mutate)
                self.assertEqual(result.returncode, 0, result.stderr)
                validate_i386_relocatable_bytes(self.output.read_bytes())
                self.assert_clean()

    def test_distinct_objects_in_same_directory_publish_concurrently(self):
        second = "kernel/doom/src/d_event.cc"
        results = []

        def compile_second():
            results.append(self.run_compile(second))

        result = self.run_race("before-mutation", compile_second)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0].returncode, 0, results[0].stderr)
        for source in (SOURCE, second):
            validate_i386_relocatable_bytes((self.root / Path(source).with_suffix(".o")).read_bytes())
        self.assert_clean()

    def test_same_output_lock_preserves_previous_object(self):
        before = self.previous_output()
        lock = self.output.with_name(self.output.name + ".cupidbuild.lock")
        lock.write_text(f"{os.getpid()}\n")
        result = self.run_compile()
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(lock.exists())
        lock.unlink()
        self.assert_preserved(before)

    def test_replaced_include_directory_is_rejected(self):
        for phase in ("before-launch", "after-install"):
            with self.subTest(phase=phase):
                before = self.previous_output()
                parent = self.root / "drivers"
                displaced = self.root / ("saved-drivers-" + phase)

                def mutate():
                    parent.rename(displaced)
                    shutil.copytree(displaced, parent)

                result = self.run_race(phase, mutate)
                self.assertNotEqual(result.returncode, 0)
                if phase == "before-launch":
                    self.assertFalse(result.compiler_launched)
                self.assert_preserved(before)

    def test_non_file_source_and_header_are_rejected(self):
        for logical in (SOURCE, "kernel/doom/dglibc_compat.h"):
            with self.subTest(logical=logical):
                path = self.root / logical
                original = path.read_bytes()
                path.unlink()
                path.mkdir()
                before = self.previous_output()
                result = self.run_compile()
                self.assertNotEqual(result.returncode, 0)
                self.assert_preserved(before)
                path.rmdir()
                path.write_bytes(original)

    def test_linked_header_is_rejected(self):
        path = self.root / "kernel/doom/dglibc_compat.h"
        saved = self.root / "saved-header"
        path.rename(saved)
        try:
            path.symlink_to(saved)
        except OSError as error:
            saved.rename(path)
            self.skipTest(f"symbolic links are unavailable: {error}")
        before = self.previous_output()
        result = self.run_compile()
        self.assertNotEqual(result.returncode, 0)
        self.assert_preserved(before)

    @unittest.skipUnless(os.name == "nt", "Windows junction contract")
    def test_junction_include_directory_is_rejected(self):
        parent = self.root / "drivers"
        displaced = self.root / "saved-drivers"
        parent.rename(displaced)
        result = subprocess.run(["cmd", "/c", "mklink", "/J", str(parent), str(displaced)],
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        before = self.previous_output()
        try:
            result = self.run_compile()
            self.assertNotEqual(result.returncode, 0)
            self.assert_preserved(before)
        finally:
            parent.rmdir()
            displaced.rename(parent)

    def test_capture_count_overflow_preserves_output_before_launch(self):
        for index in range(220):
            (self.root / "kernel/doom" / f"overflow-{index}.h").write_bytes(b"")
        before = self.previous_output()
        ready = self.root / "launch-ready"
        resume = self.root / "launch-resume"
        resume.write_bytes(b"continue")
        environment = os.environ.copy()
        environment.update(CUPIDBUILD_PUBLICATION_TEST_PHASE="after-tool-launch",
                           CUPIDBUILD_PUBLICATION_TEST_READY=str(ready),
                           CUPIDBUILD_PUBLICATION_TEST_RESUME=str(resume))
        result = self.run_compile(env=environment)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(ready.exists(), "compiler launched with an overfull capture")
        self.assert_preserved(before)

    def test_capture_byte_overflow_preserves_output_before_launch(self):
        path = self.root / "kernel/doom/oversized.h"
        with path.open("wb") as stream:
            stream.truncate(64 * 1024 * 1024 + 1)
        before = self.previous_output()
        ready = self.root / "launch-ready"
        resume = self.root / "launch-resume"
        resume.write_bytes(b"continue")
        environment = os.environ.copy()
        environment.update(CUPIDBUILD_PUBLICATION_TEST_PHASE="after-tool-launch",
                           CUPIDBUILD_PUBLICATION_TEST_READY=str(ready),
                           CUPIDBUILD_PUBLICATION_TEST_RESUME=str(resume))
        result = self.run_compile(env=environment)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(ready.exists(), "compiler launched with an oversized capture")
        self.assert_preserved(before)

    def test_aggregate_bundle_overflow_preserves_output_before_launch(self):
        for name in ("oversized-a.h", "oversized-b.h"):
            with (self.root / "kernel/doom" / name).open("wb") as stream:
                stream.truncate(33 * 1024 * 1024)
        before = self.previous_output()
        ready = self.root / "launch-ready"
        resume = self.root / "launch-resume"
        resume.write_bytes(b"continue")
        environment = os.environ.copy()
        environment.update(CUPIDBUILD_PUBLICATION_TEST_PHASE="after-tool-launch",
                           CUPIDBUILD_PUBLICATION_TEST_READY=str(ready),
                           CUPIDBUILD_PUBLICATION_TEST_RESUME=str(resume))
        result = self.run_compile(env=environment)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(ready.exists(), "compiler launched with an oversized bundle")
        self.assert_preserved(before)

    def test_unchanged_object_rechecks_membership_before_accepting_it(self):
        result = self.run_compile()
        self.assertEqual(result.returncode, 0, result.stderr)
        previous = self.output.read_bytes()
        before = self.output.stat().st_mtime_ns

        def mutate():
            (self.root / "kernel/doom/new.h").write_bytes(b"/* newly added */")

        result = self.run_race("after-tool-launch", mutate)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.output.read_bytes(), previous)
        self.assertEqual(self.output.stat().st_mtime_ns, before)
        self.assert_clean()

    def test_cupid_built_coordinator_compiles_both_profiles(self):
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
        for source in (SOURCE, APPROVED_DOOM_COMPAT_SOURCES[0]):
            with self.subTest(source=source):
                result = self.run_compile(source, cli=coordinator)
                self.assertEqual(result.returncode, 0, result.stderr)
                output = self.root / Path(source).with_suffix(".o")
                validate_i386_relocatable_bytes(output.read_bytes())
        self.assert_clean()


class CupidBuildDoomPolicyTests(unittest.TestCase):
    def test_native_profiles_and_cohorts_match_the_python_contract(self):
        source = (ROOT / "toolchain/cupidbuild.cc").read_text()

        def strings(name):
            block = source.split(name + "[] = {", 1)[1].split("};", 1)[0]
            return tuple(json.loads(value) for value in re.findall(r'"(?:\\.|[^"\\])*"', block))

        compat = strings("cupidbuild_compile_doom_profile")
        extra = strings("cupidbuild_compile_doom_tree_extra")
        self.assertEqual(compat, DOOM_COMPAT_I386_ARGUMENTS)
        self.assertEqual(compat + extra, DOOM_TREE_I386_ARGUMENTS)
        self.assertEqual(strings("cupidbuild_profile_compat_sources"),
                         APPROVED_DOOM_COMPAT_SOURCES)
        self.assertEqual(strings("cupidbuild_profile_tree_sources"),
                         APPROVED_DOOM_TREE_SOURCES)


if __name__ == "__main__":
    unittest.main()
