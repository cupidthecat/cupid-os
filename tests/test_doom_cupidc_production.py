import hashlib
import json
import os
import re
import shutil
import subprocess
import unittest
import tempfile
from pathlib import Path
from unittest import mock

from tools import cupidc_kernel_compile as kernel_compile
from tests.test_cupidc_kernel_compile import (
    FakeExecutor,
    SEED_MANIFEST,
    _valid_elf32_object,
)


REPO_ROOT = Path(__file__).resolve().parents[1]

DOOM_COMPAT_SOURCES = (
    "kernel/doom/dglibc.cc",
    "kernel/doom/doom_libc_stubs.cc",
    "kernel/doom/doomgeneric_cupidos.cc",
)


class DoomCupidCProductionTests(unittest.TestCase):
    def _profile_fixture(self):
        temporary = tempfile.TemporaryDirectory(
            prefix="cupid-doom-production-"
        )
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name).resolve()
        arguments = kernel_compile.DOOM_COMPAT_I386_ARGUMENTS
        for index, argument in enumerate(arguments):
            if argument == "-I":
                (root / arguments[index + 1].lstrip("/")).mkdir(
                    parents=True,
                    exist_ok=True,
                )
        for relative_name in (
            kernel_compile.APPROVED_DOOM_COMPAT_SOURCES
            + kernel_compile.APPROVED_DOOM_TREE_SOURCES
        ):
            member = root / relative_name
            member.parent.mkdir(parents=True, exist_ok=True)
            member.write_text(
                "int doom_profile_fixture;\n",
                encoding="utf-8",
            )
        source = root / "kernel" / "doom" / "dglibc.cc"
        source.write_text(
            '#include "shadow.h"\nint doom_fixture;\n',
            encoding="utf-8",
        )
        lower_header = root / "kernel" / "core" / "shadow.h"
        lower_header.write_text(
            "#define DOOM_SHADOW 1\n",
            encoding="utf-8",
        )
        seed = root / "seed" / "cupidc.elf"
        seed.parent.mkdir()
        seed.write_bytes(b"seed")
        manifest = seed.parent / "manifest.json"
        manifest.write_text("{}\n", encoding="utf-8")
        output = source.with_suffix(".o")
        return root, source, lower_header, seed, manifest, output

    def _freeze_seed(self, seed):
        def freeze(_manifest, snapshot):
            return mock.Mock(
                tools={
                    "cupidc": shutil.copyfile(
                        seed,
                        snapshot / seed.name,
                    )
                }
            )

        return freeze

    def test_profiles_pin_the_complete_doom_source_cohort(self):
        self.assertEqual(
            kernel_compile.APPROVED_DOOM_COMPAT_SOURCES,
            DOOM_COMPAT_SOURCES,
        )
        tree_sources = kernel_compile.APPROVED_DOOM_TREE_SOURCES
        self.assertEqual(len(tree_sources), 80)
        self.assertEqual(len(set(tree_sources)), 80)
        self.assertEqual(tree_sources[0], "kernel/doom/i_sound_cupidos.cc")
        self.assertTrue(
            all(
                source.startswith("kernel/doom/src/")
                for source in tree_sources[1:]
            )
        )
        self.assertTrue(all(source.endswith(".cc") for source in tree_sources))

        owned_sources = set(DOOM_COMPAT_SOURCES + tree_sources)
        discovered_sources = {
            path.relative_to(REPO_ROOT).as_posix()
            for path in (REPO_ROOT / "kernel" / "doom").glob("*.cc")
        }
        discovered_sources.update(
            path.relative_to(REPO_ROOT).as_posix()
            for path in (REPO_ROOT / "kernel" / "doom" / "src").glob("*.cc")
        )
        self.assertEqual(discovered_sources, owned_sources)
        self.assertEqual(len(owned_sources), 83)
        legacy_sources = {
            path.relative_to(REPO_ROOT).as_posix()
            for path in (REPO_ROOT / "kernel" / "doom").glob("*.c")
        }
        legacy_sources.update(
            path.relative_to(REPO_ROOT).as_posix()
            for path in (REPO_ROOT / "kernel" / "doom" / "src").glob("*.c")
        )
        self.assertEqual(legacy_sources, set())

    def test_checked_seed_compiles_g_game_subobject_pointer_initializers(self):
        if not SEED_MANIFEST.is_file():
            self.skipTest("checked seed manifest is not present")
        seed = SEED_MANIFEST.parent / "cupidc.elf"
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        if os.name != "nt" and not os.access(seed, os.X_OK):
            self.skipTest("checked seed is not executable")

        with tempfile.TemporaryDirectory(
            prefix=".doom-g-game-",
            dir=REPO_ROOT,
        ) as temporary:
            output = Path(temporary) / "g_game.o"
            kernel_compile.compile_kernel_source(
                REPO_ROOT,
                REPO_ROOT / "kernel" / "doom" / "src" / "g_game.cc",
                output,
                profile="doom-tree",
            )
            image = output.read_bytes()

        self.assertEqual(
            (len(image), hashlib.sha256(image).hexdigest()),
            (
                51492,
                "c9da48e696eb521441e8bee0a2b69bfdd691db57b7fbbda42450d208e78d9034",
            ),
        )

    def test_profiles_build_the_exact_compiler_argument_vectors(self):
        compat = kernel_compile.build_compile_arguments(
            "/kernel/doom/dglibc.cc",
            "/kernel/doom/dglibc.o",
            "/frozen/repository",
            profile="doom-compat",
        )
        tree = kernel_compile.build_compile_arguments(
            "/kernel/doom/src/am_map.cc",
            "/kernel/doom/src/am_map.o",
            "/frozen/repository",
            profile="doom-tree",
        )
        self.assertEqual(
            compat,
            (
                "-c",
                "/kernel/doom/dglibc.cc",
                "-o",
                "/kernel/doom/dglibc.o",
                *kernel_compile.DOOM_COMPAT_I386_ARGUMENTS,
                "--root",
                "/frozen/repository",
            ),
        )
        self.assertEqual(
            tree,
            (
                "-c",
                "/kernel/doom/src/am_map.cc",
                "-o",
                "/kernel/doom/src/am_map.o",
                *kernel_compile.DOOM_TREE_I386_ARGUMENTS,
                "--root",
                "/frozen/repository",
            ),
        )
        self.assertEqual(compat.count("--doom-compat"), 1)
        self.assertNotIn("DEBUG=1", compat)
        self.assertIn("/kernel/doom/src", compat)
        self.assertIn("/kernel/doom/src/include_stubs", compat)
        self.assertIn(
            'DEFAULT_SAVEGAMEDIR="/home/doom/"',
            tree,
        )
        self.assertIn("DOOM_PORT_CUPIDOS=1", tree)
        self.assertEqual(
            tree[tree.index("-include") + 1],
            "/kernel/doom/dglibc_compat.h",
        )

    def test_wrapper_profiles_exactly_match_the_audited_make_profiles(self):
        audit = json.loads(
            (
                REPO_ROOT
                / "docs"
                / "bootstrap"
                / "audits"
                / "active-build.json"
            ).read_text(encoding="utf-8")
        )
        profiles = {
            profile["name"]: profile
            for profile in audit["contracts"][
                "c_preprocessor_translation_units"
            ]["profiles"]
        }
        for name, arguments in (
            ("DOOM_COMPAT_I386", kernel_compile.DOOM_COMPAT_I386_ARGUMENTS),
            ("DOOM_TREE_I386", kernel_compile.DOOM_TREE_I386_ARGUMENTS),
        ):
            with self.subTest(profile=name):
                includes = []
                definitions = []
                forced = []
                index = 0
                self.assertEqual(
                    arguments[:3],
                    ("--gnu", "--doom-compat", "--freestanding"),
                )
                index = 3
                while index < len(arguments):
                    option = arguments[index]
                    value = arguments[index + 1]
                    if option == "-I":
                        includes.append(value)
                    elif option == "-D":
                        macro, replacement = value.split("=", 1)
                        definitions.append((macro, replacement))
                    elif option == "-include":
                        forced.append(value)
                    else:
                        self.fail(
                            f"unexpected {name} wrapper option: {option}"
                        )
                    index += 2
                profile = profiles[name]
                self.assertEqual(
                    includes,
                    [entry["path"] for entry in profile["include_roots"]],
                )
                audited_definitions = [
                    (action["name"], action["replacement"])
                    for action in profile["macro_actions"]
                    if action["name"] != "__SIZEOF_POINTER__"
                ]
                self.assertEqual(definitions, audited_definitions)
                self.assertEqual(forced, profile["forced_includes"])
                pointer_action = next(
                    action
                    for action in profile["macro_actions"]
                    if action["name"] == "__SIZEOF_POINTER__"
                )
                self.assertEqual(pointer_action["replacement"], "4")

    def test_cross_profile_source_is_rejected_before_seed_execution(self):
        with self.assertRaisesRegex(
            kernel_compile.KernelCompileError,
            "source is outside the approved doom-tree CupidC cohort",
        ):
            kernel_compile.compile_kernel_source(
                REPO_ROOT,
                REPO_ROOT / "kernel" / "doom" / "dglibc.cc",
                REPO_ROOT / "kernel" / "doom" / "dglibc.o",
                profile="doom-tree",
            )

    def test_doom_profile_freezes_the_source_and_complete_header_space(self):
        inputs = kernel_compile._kernel_input_paths(
            REPO_ROOT,
            "kernel/doom/dglibc.cc",
            "doom-compat",
        )
        relative = {
            path.relative_to(REPO_ROOT).as_posix() for path in inputs
        }
        self.assertIn("kernel/doom/dglibc.cc", relative)
        self.assertIn("drivers/serial.h", relative)
        self.assertIn("kernel/core/types.h", relative)
        self.assertIn("kernel/doom/src/doomdef.h", relative)
        self.assertIn(
            "kernel/doom/src/include_stubs/stdint.h",
            relative,
        )
        self.assertIn("toolchain/ctool.h", relative)
        self.assertEqual(
            {
                path
                for path in relative
                if Path(path).suffix == ".cc"
            },
            {"kernel/doom/dglibc.cc"},
        )
        self.assertTrue(
            all(
                Path(path).suffix in {".cc", ".h", ".inc"}
                for path in relative
            )
        )

    def test_make_header_dependencies_equal_the_frozen_profile_space(self):
        result = subprocess.run(
            ("make", "--print-data-base", "--dry-run", "FORCE"),
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=60,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        match = re.search(
            r"^DOOM_CUPIDC_HEADERS := (.*)$",
            result.stdout,
            re.MULTILINE,
        )
        self.assertIsNotNone(match)
        make_headers = set(match.group(1).split())
        frozen_headers = {
            path.relative_to(REPO_ROOT).as_posix()
            for path in kernel_compile._profile_header_paths(
                REPO_ROOT,
                "doom-compat",
            )
        }
        self.assertEqual(make_headers, frozen_headers)

    def test_doom_compile_runs_from_one_frozen_profile_snapshot(self):
        root, source, header, seed, manifest, output = (
            self._profile_fixture()
        )
        captured = {}

        class ClosureExecutor(FakeExecutor):
            def run(self, executable, arguments, timeout):
                compiler_root = Path(
                    arguments[arguments.index("--root") + 1]
                )
                for path in (source, header):
                    relative = path.relative_to(root).as_posix()
                    captured[relative] = (
                        compiler_root / relative
                    ).read_bytes()
                return super().run(executable, arguments, timeout)

        executor = ClosureExecutor(root, payload=_valid_elf32_object())
        with mock.patch.object(
            kernel_compile,
            "freeze_seed_inputs",
            side_effect=self._freeze_seed(seed),
        ):
            kernel_compile.compile_kernel_source(
                root,
                source,
                output,
                manifest=manifest,
                executor=executor,
                profile="doom-compat",
            )

        self.assertEqual(
            captured,
            {
                "kernel/doom/dglibc.cc": source.read_bytes(),
                "kernel/core/shadow.h": header.read_bytes(),
            },
        )
        self.assertEqual(output.read_bytes(), _valid_elf32_object())
        arguments = executor.calls[0][1]
        self.assertIn("--doom-compat", arguments)
        self.assertNotEqual(
            Path(arguments[arguments.index("--root") + 1]),
            root,
        )

    def test_doom_header_drift_preserves_the_existing_object(self):
        root, source, header, seed, manifest, output = (
            self._profile_fixture()
        )
        output.write_bytes(b"existing object")

        class DriftingExecutor(FakeExecutor):
            def run(self, executable, arguments, timeout):
                header.write_text(
                    "#define DOOM_SHADOW 2\n",
                    encoding="utf-8",
                )
                return super().run(executable, arguments, timeout)

        executor = DriftingExecutor(root, payload=_valid_elf32_object())
        with mock.patch.object(
            kernel_compile,
            "freeze_seed_inputs",
            side_effect=self._freeze_seed(seed),
        ):
            with self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                "doom-compat profile inputs changed while compiling "
                "kernel/doom/dglibc.cc",
            ):
                kernel_compile.compile_kernel_source(
                    root,
                    source,
                    output,
                    manifest=manifest,
                    executor=executor,
                    profile="doom-compat",
                )
        self.assertEqual(output.read_bytes(), b"existing object")

    def test_doom_header_membership_drift_preserves_the_existing_object(self):
        root, source, _header, seed, manifest, output = (
            self._profile_fixture()
        )
        output.write_bytes(b"existing object")
        shadowing_header = root / "kernel" / "shadow.h"

        class AddingExecutor(FakeExecutor):
            def run(self, executable, arguments, timeout):
                shadowing_header.write_text(
                    "#define DOOM_SHADOW 3\n",
                    encoding="utf-8",
                )
                return super().run(executable, arguments, timeout)

        executor = AddingExecutor(root, payload=_valid_elf32_object())
        with mock.patch.object(
            kernel_compile,
            "freeze_seed_inputs",
            side_effect=self._freeze_seed(seed),
        ):
            with self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                "doom-compat profile inputs changed while compiling "
                "kernel/doom/dglibc.cc",
            ):
                kernel_compile.compile_kernel_source(
                    root,
                    source,
                    output,
                    manifest=manifest,
                    executor=executor,
                    profile="doom-compat",
                )
        self.assertEqual(output.read_bytes(), b"existing object")

    def test_doom_source_membership_drift_preserves_the_existing_object(self):
        root, source, _header, seed, manifest, output = (
            self._profile_fixture()
        )
        output.write_bytes(b"existing object")
        unlisted_source = root / "kernel" / "doom" / "src" / "added.cc"

        class AddingExecutor(FakeExecutor):
            def run(self, executable, arguments, timeout):
                unlisted_source.write_text(
                    "int unlisted_doom_source;\n",
                    encoding="utf-8",
                )
                return super().run(executable, arguments, timeout)

        executor = AddingExecutor(root, payload=_valid_elf32_object())
        with mock.patch.object(
            kernel_compile,
            "freeze_seed_inputs",
            side_effect=self._freeze_seed(seed),
        ):
            with self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                "Doom profile source membership differs from the "
                "approved cohort",
            ):
                kernel_compile.compile_kernel_source(
                    root,
                    source,
                    output,
                    manifest=manifest,
                    executor=executor,
                    profile="doom-compat",
                )
        self.assertEqual(output.read_bytes(), b"existing object")

    def test_doom_profile_rejects_a_nested_symlink(self):
        root, _source, _header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        link = root / "kernel" / "doom" / "linked-headers"
        try:
            link.symlink_to(
                root / "kernel" / "core",
                target_is_directory=True,
            )
        except OSError as error:
            self.skipTest(f"directory symlink unavailable: {error}")
        with self.assertRaisesRegex(
            kernel_compile.KernelCompileError,
            "CupidC profile input may not be a link or junction",
        ):
            kernel_compile._profile_header_paths(
                root,
                "doom-compat",
            )

    def test_doom_profile_rejects_a_header_symlink(self):
        root, _source, header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        link = root / "kernel" / "doom" / "linked.h"
        try:
            link.symlink_to(header)
        except OSError as error:
            self.skipTest(f"file symlink unavailable: {error}")
        with self.assertRaisesRegex(
            kernel_compile.KernelCompileError,
            "CupidC profile input may not be a link or junction",
        ):
            kernel_compile._profile_header_paths(
                root,
                "doom-compat",
            )

    @unittest.skipUnless(os.name == "nt", "NTFS junction test")
    def test_doom_profile_rejects_a_nested_junction(self):
        root, _source, _header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        link = root / "kernel" / "doom" / "junction-headers"
        result = subprocess.run(
            (
                "cmd",
                "/c",
                "mklink",
                "/J",
                str(link),
                str(root / "kernel" / "core"),
            ),
            text=True,
            capture_output=True,
            timeout=30,
            check=False,
        )
        if result.returncode != 0:
            self.skipTest(f"junction unavailable: {result.stderr}")
        self.addCleanup(
            lambda: link.rmdir() if link.exists() else None
        )
        self.assertTrue(link.is_junction())
        with self.assertRaisesRegex(
            kernel_compile.KernelCompileError,
            "CupidC profile input may not be a link or junction",
        ):
            kernel_compile._profile_header_paths(
                root,
                "doom-compat",
            )

    def test_profile_input_manifest_records_every_approved_source(self):
        root, _source, _header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        output = root / "build" / "doom-inputs.json"
        output.parent.mkdir()
        kernel_compile.write_profile_input_manifest(root, output)
        document = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(
            document["sources"],
            {
                "doom-compat": list(
                    kernel_compile.APPROVED_DOOM_COMPAT_SOURCES
                ),
                "doom-tree": list(
                    kernel_compile.APPROVED_DOOM_TREE_SOURCES
                ),
            },
        )
        self.assertEqual(
            sum(len(members) for members in document["sources"].values()),
            83,
        )

    def test_profile_input_manifest_rejects_a_legacy_c_source(self):
        root, _source, _header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        legacy = (
            root
            / "kernel"
            / "doom"
            / "src"
            / "unlisted"
            / "legacy.c"
        )
        legacy.parent.mkdir()
        legacy.write_text("int legacy_doom_source;\n", encoding="utf-8")
        output = root / "build" / "doom-inputs.json"

        with self.assertRaisesRegex(
            kernel_compile.KernelCompileError,
            r"unlisted kernel/doom/src/unlisted/legacy\.c",
        ):
            kernel_compile.write_profile_input_manifest(root, output)
        self.assertFalse(output.exists())

    def test_profile_input_manifest_changes_only_with_the_header_space(self):
        root, _source, header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        output = root / "build" / "doom-inputs.json"
        output.parent.mkdir()
        kernel_compile.write_profile_input_manifest(root, output)
        first = output.read_bytes()
        first_time = output.stat().st_mtime_ns
        kernel_compile.write_profile_input_manifest(root, output)
        self.assertEqual(output.read_bytes(), first)
        self.assertEqual(output.stat().st_mtime_ns, first_time)

        original_header = header.read_bytes()
        header.write_text(
            "#define DOOM_SHADOW 9\n",
            encoding="utf-8",
        )
        kernel_compile.write_profile_input_manifest(root, output)
        self.assertNotEqual(output.read_bytes(), first)

        header.write_bytes(original_header)
        kernel_compile.write_profile_input_manifest(root, output)
        self.assertEqual(output.read_bytes(), first)

        added = root / "kernel" / "doom" / "added.h"
        added.write_text("#define ADDED 1\n", encoding="utf-8")
        kernel_compile.write_profile_input_manifest(root, output)
        changed = output.read_bytes()
        self.assertNotEqual(changed, first)

        added.unlink()
        kernel_compile.write_profile_input_manifest(root, output)
        self.assertEqual(output.read_bytes(), first)

    def test_normal_make_object_rejects_a_renamed_doom_source(self):
        root, _source, _header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        shutil.copyfile(REPO_ROOT / "Makefile", root / "Makefile")
        tools = root / "tools"
        tools.mkdir()
        for name in (
            "bootstrap_toolchain.py",
            "cupidc_kernel_compile.py",
            "kernel_cupidc_frontier.py",
        ):
            shutil.copyfile(REPO_ROOT / "tools" / name, tools / name)
        seed_root = root / "bootstrap" / "seeds" / "i386-linux"
        seed_root.mkdir(parents=True)
        (seed_root / "manifest.json").write_text("{}\n", encoding="utf-8")
        for name in (
            "cupidasm.elf",
            "cupidc.elf",
            "cupiddis.elf",
            "cupidld.elf",
            "cupidobj.elf",
        ):
            (seed_root / name).write_bytes(b"seed")

        manifest_target = "build/bootstrap/doom-cupidc-inputs.json"
        first = subprocess.run(
            ("make", manifest_target),
            cwd=root,
            text=True,
            capture_output=True,
            timeout=60,
            check=False,
        )
        self.assertEqual(
            first.returncode,
            0,
            msg=first.stderr or first.stdout,
        )
        manifest = root / manifest_target
        published = manifest.read_bytes()

        source = root / "kernel" / "doom" / "src" / "am_map.cc"
        source.rename(source.with_name("am_map-renamed.cc"))
        second = subprocess.run(
            ("make", "kernel/doom/src/d_event.o"),
            cwd=root,
            text=True,
            capture_output=True,
            timeout=60,
            check=False,
        )
        self.assertNotEqual(second.returncode, 0)
        self.assertIn(
            "CupidC profile source is unavailable: "
            "kernel/doom/src/am_map.cc",
            second.stderr + second.stdout,
        )
        self.assertEqual(manifest.read_bytes(), published)

    def test_doom_profile_rejects_an_incomplete_include_space(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-doom-profile-"
        ) as temporary:
            root = Path(temporary)
            source = root / "kernel" / "doom" / "dglibc.cc"
            source.parent.mkdir(parents=True)
            source.write_text("int source;\n", encoding="utf-8")
            with self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                "CupidC profile include root is unavailable: /kernel",
            ):
                kernel_compile._kernel_input_paths(
                    root,
                    "kernel/doom/dglibc.cc",
                    "doom-compat",
                )

    def test_makefile_uses_explicit_checked_profiles_for_every_doom_root(self):
        makefile = (REPO_ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertNotIn("$(CC) $(CFLAGS_DOOM)", makefile)
        self.assertNotIn("$(CC) $(CFLAGS_DOOM_TREE)", makefile)
        self.assertNotIn("kernel/doom/src/*.c)", makefile)
        self.assertIn("kernel/doom/src/*.cc)", makefile)
        self.assertEqual(
            makefile.count("--profile doom-compat"),
            len(DOOM_COMPAT_SOURCES),
        )
        self.assertEqual(makefile.count("--profile doom-tree"), 2)
        self.assertIn(
            "kernel/doom/src/%.o: kernel/doom/src/%.cc",
            makefile,
        )
        self.assertIn(
            "kernel/doom/src/include_stubs/*/*.h",
            makefile,
        )
        self.assertIn("toolchain/tests/*.inc", makefile)

    def test_make_dry_run_keeps_host_tools_out_of_every_doom_object(self):
        sources = DOOM_COMPAT_SOURCES + (
            kernel_compile.APPROVED_DOOM_TREE_SOURCES
        )
        targets = tuple(
            Path(source).with_suffix(".o").as_posix() for source in sources
        )
        environment = os.environ.copy()
        marker = "DOOM_HOST_TOOL_MUST_NOT_RUN"
        for variable in (
            "CC",
            "CXX",
            "CPP",
            "HOSTCC",
            "HOSTCXX",
            "ASM",
            "AS",
            "LD",
            "AR",
            "NM",
            "OBJCOPY",
        ):
            environment[variable] = marker
        result = subprocess.run(
            ("make", "--dry-run", "--always-make", *targets),
            cwd=REPO_ROOT,
            env=environment,
            text=True,
            capture_output=True,
            timeout=60,
            check=False,
        )
        self.assertEqual(
            result.returncode,
            0,
            msg=result.stderr or result.stdout,
        )
        self.assertNotIn(marker, result.stdout)
        self.assertEqual(
            result.stdout.count("--profile doom-compat"),
            len(DOOM_COMPAT_SOURCES),
        )
        self.assertEqual(
            result.stdout.count("--profile doom-tree"),
            len(kernel_compile.APPROVED_DOOM_TREE_SOURCES),
        )
        for source in sources:
            self.assertIn(f"--source {source}", result.stdout)


if __name__ == "__main__":
    unittest.main()
