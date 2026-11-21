## @file
#  @brief Check compiler profiles, include roots, and Doom objects.
import hashlib
import os
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from tests.cupidc_object_fixtures import (
    DOOM_COMPAT_OBJECTS, DOOM_I_VIDEO_OBJECT_SHA256, DOOM_I_VIDEO_OBJECT_SIZE,
    DOOM_TREE_FRONTIER_FAILURES, REPO_ROOT,
)
from tools.cupidc_kernel_compile import validate_i386_relocatable_bytes

class ObjectProfileCases:
    def test_doom_compatibility_is_explicit_and_self_hosted(self):
        linked = self.build_cupid_tools()
        self.assertEqual(linked.returncode, 0, linked.stderr)
        with tempfile.TemporaryDirectory(
            prefix=".cupidc-doom-compat-", dir=REPO_ROOT
        ) as temp:
            root = Path(temp)
            source = root / "implicit.c"
            source.write_text(
                "int invoke(void) { return missing(3); }\n",
                encoding="utf-8",
            )
            pointer_source = root / "pointer.c"
            pointer_source.write_text(
                "typedef unsigned int (*callback)(unsigned int);\n"
                "void *store(callback value) { return value; }\n",
                encoding="utf-8",
            )
            expected = (
                "/implicit.c:1:27: error CTB000010: "
                "expression identifier is not declared\n"
            )
            for name, extra in (("strict", []), ("gnu", ["--gnu"])):
                output = root / f"{name}.o"
                result = subprocess.run(
                    [
                        str(self.hosted_cupidc_path),
                        "--root",
                        str(root),
                        *extra,
                        "-c",
                        "/implicit.c",
                        "-o",
                        f"/{name}.o",
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=60,
                )
                with self.subTest(profile=name):
                    self.assertEqual(result.returncode, 1, result.stderr)
                    self.assertEqual(result.stdout, "")
                    self.assertEqual(result.stderr, expected)
                    self.assertFalse(output.exists())
                pointer_output = root / f"{name}-pointer.o"
                pointer_result = subprocess.run(
                    [
                        str(self.hosted_cupidc_path),
                        "--root",
                        str(root),
                        *extra,
                        "-c",
                        "/pointer.c",
                        "-o",
                        f"/{name}-pointer.o",
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=60,
                )
                pointer_expected = (
                    "/pointer.c:2:38: error CTB000010: "
                    "return expression is not convertible to function "
                    "result type\n"
                )
                with self.subTest(profile=name, feature="pointer"):
                    self.assertEqual(
                        pointer_result.returncode, 1, pointer_result.stderr
                    )
                    self.assertEqual(pointer_result.stdout, "")
                    self.assertEqual(pointer_result.stderr, pointer_expected)
                    self.assertFalse(pointer_output.exists())

            hosted = subprocess.run(
                [
                    str(self.hosted_cupidc_path),
                    "--root",
                    str(root),
                    "--doom-compat",
                    "-c",
                    "/implicit.c",
                    "-o",
                    "/hosted.o",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )
            cupid = self.run_cupid_linux_tool(
                self.cupid_cupidc_path,
                [
                    "--root",
                    root,
                    "--doom-compat",
                    "-c",
                    "/implicit.c",
                    "-o",
                    "/cupid.o",
                ],
                timeout=60,
            )
            self.assertEqual(hosted.returncode, 0, hosted.stderr)
            self.assertEqual(cupid.returncode, hosted.returncode, cupid.stderr)
            self.assertEqual(cupid.stdout, hosted.stdout)
            self.assertEqual(cupid.stderr, hosted.stderr)
            self.assertEqual(
                (root / "cupid.o").read_bytes(),
                (root / "hosted.o").read_bytes(),
            )
            self.assertEqual(
                (root / "hosted.o").read_bytes()[:7],
                b"\x7fELF\x01\x01\x01",
            )
            hosted_pointer = subprocess.run(
                [
                    str(self.hosted_cupidc_path),
                    "--root",
                    str(root),
                    "--doom-compat",
                    "-c",
                    "/pointer.c",
                    "-o",
                    "/hosted-pointer.o",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )
            cupid_pointer = self.run_cupid_linux_tool(
                self.cupid_cupidc_path,
                [
                    "--root",
                    root,
                    "--doom-compat",
                    "-c",
                    "/pointer.c",
                    "-o",
                    "/cupid-pointer.o",
                ],
                timeout=60,
            )
            self.assertEqual(
                hosted_pointer.returncode, 0, hosted_pointer.stderr
            )
            self.assertEqual(
                cupid_pointer.returncode,
                hosted_pointer.returncode,
                cupid_pointer.stderr,
            )
            self.assertEqual(cupid_pointer.stdout, hosted_pointer.stdout)
            self.assertEqual(cupid_pointer.stderr, hosted_pointer.stderr)
            self.assertEqual(
                (root / "cupid-pointer.o").read_bytes(),
                (root / "hosted-pointer.o").read_bytes(),
            )

    def test_cupidc_angle_only_roots_do_not_change_quoted_lookup(self):
        linked = self.build_cupid_tools()
        self.assertEqual(linked.returncode, 0, linked.stderr)
        with tempfile.TemporaryDirectory(
            prefix=".cupidc-include-forms-", dir=REPO_ROOT
        ) as temp:
            root = Path(temp)
            quoted_root = root / "quoted"
            angle_root = root / "angle"
            quoted_root.mkdir()
            angle_root.mkdir()
            (quoted_root / "select.h").write_text(
                "#define SELECTED_VALUE 11\n", encoding="utf-8"
            )
            (angle_root / "select.h").write_text(
                "#define SELECTED_VALUE 22\n", encoding="utf-8"
            )
            (root / "quoted.c").write_text(
                '#include "select.h"\n'
                "int quote_selection(void) { return SELECTED_VALUE; }\n",
                encoding="utf-8",
            )
            (root / "angle.c").write_text(
                "#include <select.h>\n"
                "int angle_selection(void) { return SELECTED_VALUE; }\n",
                encoding="utf-8",
            )

            compile_cases = (
                (
                    "/quoted.c",
                    "/quoted",
                    "/expected-quoted.o",
                    "/hosted-quoted.o",
                    "/cupid-quoted.o",
                ),
                (
                    "/angle.c",
                    "/angle",
                    "/expected-angle.o",
                    "/hosted-angle.o",
                    "/cupid-angle.o",
                ),
            )
            for (
                source,
                expected_include,
                expected_output,
                hosted_output,
                cupid_output,
            ) in compile_cases:
                expected = subprocess.run(
                    [
                        str(self.hosted_cupidc_path),
                        "--root",
                        str(root),
                        "-c",
                        source,
                        "-I",
                        expected_include,
                        "-o",
                        expected_output,
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=60,
                )
                hosted = subprocess.run(
                    [
                        str(self.hosted_cupidc_path),
                        "--root",
                        str(root),
                        "-c",
                        source,
                        "--include-angle",
                        "/angle",
                        "-I",
                        "/quoted",
                        "-o",
                        hosted_output,
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=60,
                )
                cupid = self.run_cupid_linux_tool(
                    self.cupid_cupidc_path,
                    [
                        "--root",
                        root,
                        "-c",
                        source,
                        "--include-angle",
                        "/angle",
                        "-I",
                        "/quoted",
                        "-o",
                        cupid_output,
                    ],
                    timeout=60,
                )
                self.assertEqual(expected.returncode, 0, expected.stderr)
                self.assertEqual(hosted.returncode, 0, hosted.stderr)
                self.assertEqual(cupid.returncode, 0, cupid.stderr)
                self.assertEqual(expected.stdout, "")
                self.assertEqual(hosted.stdout, "")
                self.assertEqual(cupid.stdout, "")
                self.assertEqual(expected.stderr, "")
                self.assertEqual(hosted.stderr, "")
                self.assertEqual(cupid.stderr, "")
                expected_object = root / expected_output[1:]
                hosted_object = root / hosted_output[1:]
                cupid_object = root / cupid_output[1:]
                self.assertEqual(
                    hosted_object.read_bytes(), expected_object.read_bytes()
                )
                self.assertEqual(
                    cupid_object.read_bytes(), expected_object.read_bytes()
                )
                self.assertEqual(
                    expected_object.read_bytes()[:7],
                    b"\x7fELF\x01\x01\x01",
                )

    def test_cupidc_forced_includes_run_in_command_order(self):
        linked = self.build_cupid_tools()
        self.assertEqual(linked.returncode, 0, linked.stderr)
        with tempfile.TemporaryDirectory(
            prefix=".cupidc-forced-includes-", dir=REPO_ROOT
        ) as temp:
            root = Path(temp)
            (root / "first.h").write_text(
                "#define FORCED_VALUE 40\n", encoding="utf-8"
            )
            (root / "second.h").write_text(
                "#if FORCED_VALUE != 40\n"
                '#error "forced include order changed"\n'
                "#endif\n"
                "#undef FORCED_VALUE\n"
                "#define FORCED_VALUE 42\n",
                encoding="utf-8",
            )
            (root / "source.c").write_text(
                "int forced_include_value(void) { return FORCED_VALUE; }\n",
                encoding="utf-8",
            )
            hosted_output = root / "hosted.o"
            cupid_output = root / "cupid.o"
            hosted = subprocess.run(
                [
                    str(self.hosted_cupidc_path),
                    "--root",
                    str(root),
                    "-c",
                    "/source.c",
                    "-include",
                    "/first.h",
                    "-include",
                    "/second.h",
                    "-o",
                    "/hosted.o",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )
            cupid = self.run_cupid_linux_tool(
                self.cupid_cupidc_path,
                [
                    "--root",
                    root,
                    "-c",
                    "/source.c",
                    "-include",
                    "/first.h",
                    "-include",
                    "/second.h",
                    "-o",
                    "/cupid.o",
                ],
                timeout=60,
            )
            self.assertEqual(hosted.returncode, 0, hosted.stderr)
            self.assertEqual(cupid.returncode, 0, cupid.stderr)
            self.assertEqual(hosted.stdout, "")
            self.assertEqual(cupid.stdout, "")
            self.assertEqual(hosted.stderr, "")
            self.assertEqual(cupid.stderr, "")
            self.assertEqual(
                cupid_output.read_bytes(), hosted_output.read_bytes()
            )
            self.assertEqual(
                hosted_output.read_bytes()[:7], b"\x7fELF\x01\x01\x01"
            )

    def test_cupidc_exact_doom_tree_profile_has_a_frozen_object_frontier(
        self,
    ):
        audit, profile = self.load_cupidc_profile("DOOM_TREE_I386")
        transforms = [
            transform
            for transform in audit["build"]["transforms"]
            if transform["inputs"]
            and (
                transform["inputs"][0]
                == "kernel/doom/i_sound_cupidos.cc"
                or (
                    transform["inputs"][0].startswith("kernel/doom/src/")
                    and transform["inputs"][0].endswith(".cc")
                )
            )
        ]
        sources = sorted(
            "/" + transform["inputs"][0] for transform in transforms
        )
        self.assertEqual(len(sources), 80)
        for transform in transforms:
            source = transform["inputs"][0]
            output = transform["output"]
            expected_source = (
                source
                if source == "kernel/doom/i_sound_cupidos.cc"
                else "$<"
            )
            expected_output = (
                output
                if source == "kernel/doom/i_sound_cupidos.cc"
                else "$@"
            )
            self.assertEqual(
                transform["recipe"],
                [
                    "$(PRODUCTION_SEED_DIRECTORY)cupidbuild.$(PRODUCTION_SEED_SUFFIX) compile-doom \\",
                    '--seed-manifest $(PRODUCTION_SEED_MANIFEST) --root "$(CURDIR)" \\',
                    "--seed-release $(PRODUCTION_SEED_RELEASE) \\",
                    f"--source {expected_source} "
                    f"--output {expected_output}"
                ],
            )
        self.assertEqual(profile["tracked_translation_units"], 80)
        self.assertTrue(profile["gnu_extensions"])
        self.assertFalse(profile["hosted_environment"])
        self.assertTrue(profile["implicit_function_declarations"])
        self.assertTrue(profile["compatibility_pointer_conversions"])
        self.assertEqual(
            profile["forced_includes"],
            ["/kernel/doom/dglibc_compat.h"],
        )

        arguments = self.cupidc_profile_arguments(profile)

        with tempfile.TemporaryDirectory(
            prefix=".cupidc-doom-frontier-", dir=REPO_ROOT
        ) as temp:
            output_root = Path(temp)

            def compile_source(index_and_source):
                index, source = index_and_source
                output = output_root / f"{index:02d}.o"
                logical_output = "/" + output.relative_to(REPO_ROOT).as_posix()
                result = subprocess.run(
                    [
                        str(self.hosted_cupidc_path),
                        *arguments,
                        "-c",
                        source,
                        "-o",
                        logical_output,
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=180,
                )
                return source, output, result

            with ThreadPoolExecutor(max_workers=4) as executor:
                results = list(
                    executor.map(compile_source, enumerate(sources))
                )

            failures = {}
            for source, output, result in results:
                expected = DOOM_TREE_FRONTIER_FAILURES.get(source)
                with self.subTest(source=source):
                    self.assertEqual(result.stdout, "")
                    if expected is None:
                        object_bytes = output.read_bytes()
                        self.assertEqual(
                            result.returncode, 0, result.stderr
                        )
                        self.assertEqual(result.stderr, "")
                        self.assertEqual(
                            object_bytes[:7],
                            b"\x7fELF\x01\x01\x01",
                        )
                        if source == "/kernel/doom/src/info.cc":
                            self.assertEqual(output.stat().st_size, 51200)
                        if source == "/kernel/doom/src/i_video.cc":
                            self.assertEqual(
                                len(object_bytes),
                                DOOM_I_VIDEO_OBJECT_SIZE,
                            )
                            self.assertEqual(
                                hashlib.sha256(object_bytes).hexdigest(),
                                DOOM_I_VIDEO_OBJECT_SHA256,
                            )
                            repeated_output = output.with_suffix(
                                ".repeat.o"
                            )
                            repeated_logical_output = (
                                "/"
                                + repeated_output.relative_to(
                                    REPO_ROOT
                                ).as_posix()
                            )
                            repeated = subprocess.run(
                                [
                                    str(self.hosted_cupidc_path),
                                    *arguments,
                                    "-c",
                                    source,
                                    "-o",
                                    repeated_logical_output,
                                ],
                                cwd=REPO_ROOT,
                                text=True,
                                capture_output=True,
                                timeout=180,
                            )
                            self.assertEqual(
                                repeated.returncode,
                                0,
                                repeated.stderr,
                            )
                            self.assertEqual(repeated.stdout, "")
                            self.assertEqual(repeated.stderr, "")
                            self.assertEqual(
                                repeated_output.read_bytes(),
                                object_bytes,
                            )
                        continue
                    failures[source] = result.stderr
                    self.assertEqual(result.returncode, 1, result.stderr)
                    self.assertEqual(result.stderr, expected)
                    self.assertFalse(output.exists())
            self.assertEqual(failures, DOOM_TREE_FRONTIER_FAILURES)
            self.assertEqual(len(results) - len(failures), 80)

    def test_cupidc_exact_doom_compat_profile_is_complete_and_self_hosted(
        self,
    ):
        audit, profile = self.load_cupidc_profile("DOOM_COMPAT_I386")
        expected_sources = {
            source.lstrip("/") for source in DOOM_COMPAT_OBJECTS
        }
        transforms = [
            transform
            for transform in audit["build"]["transforms"]
            if transform["inputs"]
            and transform["inputs"][0] in expected_sources
        ]
        sources = sorted(
            "/" + transform["inputs"][0] for transform in transforms
        )
        self.assertEqual(sources, sorted(DOOM_COMPAT_OBJECTS))
        for transform in transforms:
            source = transform["inputs"][0]
            self.assertEqual(
                transform["recipe"],
                [
                    "$(PRODUCTION_SEED_DIRECTORY)cupidbuild.$(PRODUCTION_SEED_SUFFIX) compile-doom \\",
                    '--seed-manifest $(PRODUCTION_SEED_MANIFEST) --root "$(CURDIR)" \\',
                    "--seed-release $(PRODUCTION_SEED_RELEASE) \\",
                    f"--source {source} --output {transform['output']}"
                ],
            )
        self.assertEqual(profile["tracked_translation_units"], 3)
        self.assertTrue(profile["gnu_extensions"])
        self.assertFalse(profile["hosted_environment"])
        self.assertTrue(profile["implicit_function_declarations"])
        self.assertTrue(profile["compatibility_pointer_conversions"])
        self.assertEqual(profile["forced_includes"], [])
        arguments = self.cupidc_profile_arguments(profile)

        linked = self.build_cupid_tools()
        self.assertEqual(linked.returncode, 0, linked.stderr)
        with tempfile.TemporaryDirectory(
            prefix=".cupidc-doom-compat-frontier-", dir=REPO_ROOT
        ) as temp:
            output_root = Path(temp)
            logical_root = "/" + output_root.relative_to(
                REPO_ROOT
            ).as_posix()
            for index, source in enumerate(sources):
                hosted_output = output_root / f"{index}-hosted.o"
                cupid_output = output_root / f"{index}-cupid.o"
                hosted = subprocess.run(
                    [
                        str(self.hosted_cupidc_path),
                        *arguments,
                        "-c",
                        source,
                        "-o",
                        f"{logical_root}/{index}-hosted.o",
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=180,
                )
                cupid = self.run_cupid_linux_tool(
                    self.cupid_cupidc_path,
                    [
                        *arguments,
                        "-c",
                        source,
                        "-o",
                        f"{logical_root}/{index}-cupid.o",
                    ],
                    timeout=180,
                )
                with self.subTest(source=source):
                    self.assertEqual(hosted.returncode, 0, hosted.stderr)
                    self.assertEqual(cupid.returncode, 0, cupid.stderr)
                    self.assertEqual(hosted.stdout, "")
                    self.assertEqual(cupid.stdout, "")
                    self.assertEqual(hosted.stderr, "")
                    self.assertEqual(cupid.stderr, "")
                    hosted_bytes = hosted_output.read_bytes()
                    self.assertEqual(cupid_output.read_bytes(), hosted_bytes)
                    expected_size, expected_sha256 = DOOM_COMPAT_OBJECTS[
                        source
                    ]
                    self.assertEqual(len(hosted_bytes), expected_size)
                    self.assertEqual(
                        hashlib.sha256(hosted_bytes).hexdigest(),
                        expected_sha256,
                    )

    def test_cupidc_forced_include_rejects_bad_values_and_root_paths(self):
        linked = self.build_cupid_tools()
        self.assertEqual(linked.returncode, 0, linked.stderr)
        usage = (
            "usage: cupidc -c INPUT -o OUTPUT [-I PATH] "
            "[--include-angle PATH] [-include FILE] [-D NAME[=VALUE]] "
            "[-U NAME] [--cupid] [--gnu] [--doom-compat] [--freestanding] "
            "[--root NATIVE_ROOT] [--source-bundle CUPSRC1_FILE]\n"
        )
        with tempfile.TemporaryDirectory(
            prefix=".cupidc-forced-include-errors-", dir=REPO_ROOT
        ) as temp:
            root = Path(temp)
            (root / "source.c").write_text(
                "int forced_include_error_source(void) { return 0; }\n",
                encoding="utf-8",
            )
            cases = (
                (
                    "missing",
                    [
                        "--root",
                        root,
                        "-c",
                        "/source.c",
                        "-o",
                        "/hosted-missing.o",
                        "-include",
                    ],
                    [
                        "--root",
                        root,
                        "-c",
                        "/source.c",
                        "-o",
                        "/cupid-missing.o",
                        "-include",
                    ],
                    2,
                    usage,
                    root / "hosted-missing.o",
                    root / "cupid-missing.o",
                ),
                (
                    "empty",
                    [
                        "--root",
                        root,
                        "-c",
                        "/source.c",
                        "-include",
                        "",
                        "-o",
                        "/hosted-empty.o",
                    ],
                    [
                        "--root",
                        root,
                        "-c",
                        "/source.c",
                        "-include",
                        "",
                        "-o",
                        "/cupid-empty.o",
                    ],
                    2,
                    usage,
                    root / "hosted-empty.o",
                    root / "cupid-empty.o",
                ),
                (
                    "relative",
                    [
                        "--root",
                        root,
                        "-c",
                        "/source.c",
                        "-include",
                        "forced.h",
                        "-o",
                        "/hosted-relative.o",
                    ],
                    [
                        "--root",
                        root,
                        "-c",
                        "/source.c",
                        "-include",
                        "forced.h",
                        "-o",
                        "/cupid-relative.o",
                    ],
                    1,
                    "cupidc: --root requires logical forced include paths\n",
                    root / "hosted-relative.o",
                    root / "cupid-relative.o",
                ),
            )
            for (
                name,
                hosted_arguments,
                cupid_arguments,
                returncode,
                stderr,
                hosted_output,
                cupid_output,
            ) in cases:
                with self.subTest(name=name):
                    hosted = subprocess.run(
                        [
                            str(self.hosted_cupidc_path),
                            *[str(argument) for argument in hosted_arguments],
                        ],
                        cwd=REPO_ROOT,
                        text=True,
                        capture_output=True,
                        timeout=60,
                    )
                    cupid = self.run_cupid_linux_tool(
                        self.cupid_cupidc_path,
                        cupid_arguments,
                        timeout=60,
                    )
                    self.assertEqual(
                        hosted.returncode, returncode, hosted.stderr
                    )
                    self.assertEqual(
                        cupid.returncode, returncode, cupid.stderr
                    )
                    self.assertEqual(hosted.stdout, "")
                    self.assertEqual(cupid.stdout, "")
                    self.assertEqual(hosted.stderr, stderr)
                    self.assertEqual(cupid.stderr, stderr)
                    self.assertFalse(hosted_output.exists())
                    self.assertFalse(cupid_output.exists())

            hosted_output = root / "hosted-sentinel.o"
            cupid_output = root / "cupid-sentinel.o"
            hosted_output.write_bytes(b"hosted-sentinel")
            cupid_output.write_bytes(b"cupid-sentinel")
            arguments = [
                "--root",
                root,
                "-c",
                "/source.c",
                "-include",
                "/missing.h",
            ]
            hosted = subprocess.run(
                [
                    str(self.hosted_cupidc_path),
                    *[str(argument) for argument in arguments],
                    "-o",
                    "/hosted-sentinel.o",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )
            cupid = self.run_cupid_linux_tool(
                self.cupid_cupidc_path,
                [*arguments, "-o", "/cupid-sentinel.o"],
                timeout=60,
            )
            self.assertEqual(hosted.returncode, 1, hosted.stderr)
            self.assertEqual(cupid.returncode, hosted.returncode, cupid.stderr)
            self.assertEqual(cupid.stdout, hosted.stdout)
            self.assertEqual(cupid.stderr, hosted.stderr)
            self.assertEqual(
                hosted.stderr,
                "/missing.h:0:0: error CT9000009: "
                "CupidC forced include file was not found\n",
            )
            self.assertEqual(hosted_output.read_bytes(), b"hosted-sentinel")
            self.assertEqual(cupid_output.read_bytes(), b"cupid-sentinel")

    def test_cupidc_angle_only_root_rejects_bad_values_and_root_paths(self):
        linked = self.build_cupid_tools()
        self.assertEqual(linked.returncode, 0, linked.stderr)
        usage = (
            "usage: cupidc -c INPUT -o OUTPUT [-I PATH] "
            "[--include-angle PATH] [-include FILE] [-D NAME[=VALUE]] "
            "[-U NAME] [--cupid] [--gnu] [--doom-compat] [--freestanding] "
            "[--root NATIVE_ROOT] [--source-bundle CUPSRC1_FILE]\n"
        )
        with tempfile.TemporaryDirectory(
            prefix=".cupidc-include-errors-", dir=REPO_ROOT
        ) as temp:
            root = Path(temp)
            (root / "source.c").write_text(
                "int include_error_source(void) { return 0; }\n",
                encoding="utf-8",
            )
            cases = (
                (
                    "missing",
                    [
                        "--root",
                        root,
                        "-c",
                        "/source.c",
                        "-o",
                        "/hosted-missing.o",
                        "--include-angle",
                    ],
                    [
                        "--root",
                        root,
                        "-c",
                        "/source.c",
                        "-o",
                        "/cupid-missing.o",
                        "--include-angle",
                    ],
                    2,
                    usage,
                    root / "hosted-missing.o",
                    root / "cupid-missing.o",
                ),
                (
                    "empty",
                    [
                        "--root",
                        root,
                        "-c",
                        "/source.c",
                        "--include-angle",
                        "",
                        "-o",
                        "/hosted-empty.o",
                    ],
                    [
                        "--root",
                        root,
                        "-c",
                        "/source.c",
                        "--include-angle",
                        "",
                        "-o",
                        "/cupid-empty.o",
                    ],
                    2,
                    usage,
                    root / "hosted-empty.o",
                    root / "cupid-empty.o",
                ),
                (
                    "relative",
                    [
                        "--root",
                        root,
                        "-c",
                        "/source.c",
                        "--include-angle",
                        "angle",
                        "-o",
                        "/hosted-relative.o",
                    ],
                    [
                        "--root",
                        root,
                        "-c",
                        "/source.c",
                        "--include-angle",
                        "angle",
                        "-o",
                        "/cupid-relative.o",
                    ],
                    1,
                    "cupidc: --root requires logical include paths\n",
                    root / "hosted-relative.o",
                    root / "cupid-relative.o",
                ),
            )
            for (
                name,
                hosted_arguments,
                cupid_arguments,
                returncode,
                stderr,
                hosted_output,
                cupid_output,
            ) in cases:
                with self.subTest(name=name):
                    hosted = subprocess.run(
                        [
                            str(self.hosted_cupidc_path),
                            *[str(argument) for argument in hosted_arguments],
                        ],
                        cwd=REPO_ROOT,
                        text=True,
                        capture_output=True,
                        timeout=60,
                    )
                    cupid = self.run_cupid_linux_tool(
                        self.cupid_cupidc_path,
                        cupid_arguments,
                        timeout=60,
                    )
                    self.assertEqual(
                        hosted.returncode, returncode, hosted.stderr
                    )
                    self.assertEqual(
                        cupid.returncode, returncode, cupid.stderr
                    )
                    self.assertEqual(hosted.stdout, "")
                    self.assertEqual(cupid.stdout, "")
                    self.assertEqual(hosted.stderr, stderr)
                    self.assertEqual(cupid.stderr, stderr)
                    self.assertFalse(hosted_output.exists())
                    self.assertFalse(cupid_output.exists())

    def test_cupid_built_cupidc_matches_help_and_usage_failures(self):
        linked = self.build_cupid_tools()
        self.assertEqual(linked.returncode, 0, linked.stderr)
        usage = (
            "usage: cupidc -c INPUT -o OUTPUT [-I PATH] "
            "[--include-angle PATH] [-include FILE] [-D NAME[=VALUE]] "
            "[-U NAME] [--cupid] [--gnu] [--doom-compat] [--freestanding] "
            "[--root NATIVE_ROOT] [--source-bundle CUPSRC1_FILE]\n"
        )
        hosted_help = subprocess.run(
            [str(self.hosted_cupidc_path), "--help"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=20,
        )
        cupid_help = self.run_cupid_linux_tool(
            self.cupid_cupidc_path, ["--help"], timeout=20
        )
        self.assertEqual(hosted_help.returncode, 0, hosted_help.stderr)
        self.assertEqual(cupid_help.returncode, hosted_help.returncode)
        self.assertEqual(hosted_help.stdout, usage)
        self.assertEqual(cupid_help.stdout, usage)
        self.assertEqual(hosted_help.stderr, "")
        self.assertEqual(cupid_help.stderr, "")

        hosted_invalid = subprocess.run(
            [str(self.hosted_cupidc_path), "-c", "missing.c"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=20,
        )
        cupid_invalid = self.run_cupid_linux_tool(
            self.cupid_cupidc_path,
            ["-c", "missing.c"],
            timeout=20,
        )
        self.assertEqual(hosted_invalid.returncode, 2)
        self.assertEqual(cupid_invalid.returncode, hosted_invalid.returncode)
        self.assertEqual(hosted_invalid.stdout, "")
        self.assertEqual(cupid_invalid.stdout, "")
        self.assertEqual(hosted_invalid.stderr, usage)
        self.assertEqual(cupid_invalid.stderr, usage)

    def test_public_cupid_mode_compiles_native_types_with_both_drivers(self):
        linked = self.build_cupid_tools()
        self.assertEqual(linked.returncode, 0, linked.stderr)
        with tempfile.TemporaryDirectory(
            prefix=".cupidc-public-cupid-mode-", dir=REPO_ROOT
        ) as temp:
            root = Path(temp)
            (root / "native-types.cc").write_text(
                "U32 cupid_add(U32 left, U32 right) {\n"
                "  return left + right;\n"
                "}\n",
                encoding="utf-8",
            )
            common = [
                "--root",
                root,
                "--cupid",
                "-c",
                "/native-types.cc",
            ]
            hosted = subprocess.run(
                [
                    str(self.hosted_cupidc_path),
                    *[str(argument) for argument in common],
                    "-o",
                    "/hosted.o",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )
            cupid = self.run_cupid_linux_tool(
                self.cupid_cupidc_path,
                [*common, "-o", "/cupid.o"],
                timeout=60,
            )
            self.assertEqual(hosted.returncode, 0, hosted.stderr)
            self.assertEqual(cupid.returncode, hosted.returncode, cupid.stderr)
            self.assertEqual(hosted.stdout, "")
            self.assertEqual(cupid.stdout, hosted.stdout)
            self.assertEqual(hosted.stderr, "")
            self.assertEqual(cupid.stderr, hosted.stderr)
            hosted_bytes = (root / "hosted.o").read_bytes()
            self.assertEqual((root / "cupid.o").read_bytes(), hosted_bytes)
            validate_i386_relocatable_bytes(hosted_bytes)

            logical_root = "/" + root.relative_to(REPO_ROOT).as_posix()
            active_common = [
                "--root",
                REPO_ROOT,
                "--cupid",
                "-c",
                "/kernel/cpu/simd_intrin.h",
            ]
            active_hosted = subprocess.run(
                [
                    str(self.hosted_cupidc_path),
                    *[str(argument) for argument in active_common],
                    "-o",
                    logical_root + "/hosted-simd-intrin.o",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )
            active_cupid = self.run_cupid_linux_tool(
                self.cupid_cupidc_path,
                [
                    *active_common,
                    "-o",
                    logical_root + "/cupid-simd-intrin.o",
                ],
                timeout=60,
            )
            self.assertEqual(
                active_hosted.returncode, 0, active_hosted.stderr
            )
            self.assertEqual(
                active_cupid.returncode,
                active_hosted.returncode,
                active_cupid.stderr,
            )
            self.assertEqual(active_hosted.stdout, "")
            self.assertEqual(active_cupid.stdout, active_hosted.stdout)
            self.assertEqual(active_hosted.stderr, "")
            self.assertEqual(active_cupid.stderr, active_hosted.stderr)
            active_bytes = (root / "hosted-simd-intrin.o").read_bytes()
            self.assertEqual(
                (root / "cupid-simd-intrin.o").read_bytes(), active_bytes
            )
            validate_i386_relocatable_bytes(active_bytes)

    def test_public_cupid_mode_has_ordered_extension_and_profile_rules(self):
        linked = self.build_cupid_tools()
        self.assertEqual(linked.returncode, 0, linked.stderr)
        usage = (
            "usage: cupidc -c INPUT -o OUTPUT [-I PATH] "
            "[--include-angle PATH] [-include FILE] [-D NAME[=VALUE]] "
            "[-U NAME] [--cupid] [--gnu] [--doom-compat] "
            "[--freestanding] [--root NATIVE_ROOT] [--source-bundle CUPSRC1_FILE]\n"
        )
        conflict = (
            "cupidc: --cupid cannot be combined with --doom-compat\n"
            + usage
        )
        with tempfile.TemporaryDirectory(
            prefix=".cupidc-public-cupid-options-", dir=REPO_ROOT
        ) as temp:
            root = Path(temp)
            (root / "native-types.cc").write_text(
                "U32 cupid_identity(U32 value) { return value; }\n",
                encoding="utf-8",
            )

            def run_hosted(arguments):
                return subprocess.run(
                    [
                        str(self.hosted_cupidc_path),
                        *[str(argument) for argument in arguments],
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=60,
                )

            drivers = (
                ("hosted", run_hosted),
                (
                    "cupid-built",
                    lambda arguments: self.run_cupid_linux_tool(
                        self.cupid_cupidc_path, arguments, timeout=60
                    ),
                ),
            )
            successful_objects = []
            for driver_name, run_driver in drivers:
                for order_name, options in (
                    ("cupid-then-gnu", ["--cupid", "--gnu"]),
                    ("gnu-then-cupid", ["--gnu", "--cupid"]),
                ):
                    output_name = f"/{driver_name}-{order_name}.o"
                    result = run_driver(
                        [
                            "--root",
                            root,
                            *options,
                            "-c",
                            "/native-types.cc",
                            "-o",
                            output_name,
                        ]
                    )
                    with self.subTest(
                        driver=driver_name, option_order=order_name
                    ):
                        self.assertEqual(result.returncode, 0, result.stderr)
                        self.assertEqual(result.stdout, "")
                        self.assertEqual(result.stderr, "")
                    successful_objects.append(
                        (root / output_name.lstrip("/")).read_bytes()
                    )
            self.assertTrue(successful_objects)
            self.assertTrue(
                all(
                    image == successful_objects[0]
                    for image in successful_objects[1:]
                )
            )

            for driver_name, run_driver in drivers:
                for order_name, options in (
                    (
                        "cupid-then-doom",
                        ["--cupid", "--doom-compat"],
                    ),
                    (
                        "doom-then-cupid",
                        ["--doom-compat", "--cupid"],
                    ),
                ):
                    output_name = f"{driver_name}-{order_name}.o"
                    output = root / output_name
                    output.write_bytes(b"sentinel")
                    result = run_driver(
                        [
                            "--root",
                            root,
                            *options,
                            "-c",
                            "/native-types.cc",
                            "-o",
                            "/" + output_name,
                        ]
                    )
                    with self.subTest(
                        driver=driver_name, conflict_order=order_name
                    ):
                        self.assertEqual(result.returncode, 2, result.stderr)
                        self.assertEqual(result.stdout, "")
                        self.assertEqual(result.stderr, conflict)
                        self.assertEqual(output.read_bytes(), b"sentinel")

    def test_hosted_cupidc_resolves_native_absolute_and_relative_paths(self):
        with tempfile.TemporaryDirectory(
            prefix=".hosted-cupidc-paths-", dir=REPO_ROOT
        ) as temp:
            root = Path(temp)
            source = root / "source.c"
            absolute_object = root / "absolute.o"
            relative_object = root / "relative.o"
            source.write_text(
                "int native_path_value(void) { return 32; }\n",
                encoding="utf-8",
            )
            absolute = subprocess.run(
                [
                    str(self.hosted_cupidc_path),
                    "-c",
                    str(source.resolve()),
                    "-o",
                    str(absolute_object.resolve()),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )
            relative = subprocess.run(
                [
                    str(self.hosted_cupidc_path),
                    "-c",
                    source.name,
                    "-o",
                    relative_object.name,
                ],
                cwd=root,
                text=True,
                capture_output=True,
                timeout=60,
            )
            self.assertEqual(absolute.returncode, 0, absolute.stderr)
            self.assertEqual(relative.returncode, 0, relative.stderr)
            self.assertEqual(absolute.stdout, "")
            self.assertEqual(relative.stdout, "")
            self.assertEqual(absolute.stderr, "")
            self.assertEqual(relative.stderr, "")
            self.assertEqual(
                relative_object.read_bytes(), absolute_object.read_bytes()
            )
            self.assertEqual(
                absolute_object.read_bytes()[:7], b"\x7fELF\x01\x01\x01"
            )
            if os.name == "nt":
                root_relative_object = root / "root-relative.o"
                source_absolute = source.resolve()
                object_absolute = root_relative_object.resolve()
                root_relative_source = (
                    "\\"
                    + str(source_absolute.relative_to(source_absolute.anchor))
                )
                root_relative_output = (
                    "\\"
                    + str(object_absolute.relative_to(object_absolute.anchor))
                )
                root_relative = subprocess.run(
                    [
                        str(self.hosted_cupidc_path),
                        "-c",
                        root_relative_source,
                        "-o",
                        root_relative_output,
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=60,
                )
                self.assertEqual(
                    root_relative.returncode, 0, root_relative.stderr
                )
                self.assertEqual(root_relative.stdout, "")
                self.assertEqual(root_relative.stderr, "")
                self.assertEqual(
                    root_relative_object.read_bytes(),
                    absolute_object.read_bytes(),
                )

    def test_cupid_built_cupidc_reproduces_a_compiler_implementation_object(self):
        linked = self.build_cupid_tools()
        self.assertEqual(linked.returncode, 0, linked.stderr)
        with tempfile.TemporaryDirectory(
            prefix=".cupid-built-cupidc-generation-", dir=REPO_ROOT
        ) as temp:
            root = Path(temp)
            relative = root.relative_to(REPO_ROOT).as_posix()
            hosted_object = root / "generation-zero.o"
            first_object = root / "generation-one-first.o"
            second_object = root / "generation-one-second.o"
            common = [
                "--root",
                REPO_ROOT,
                "-c",
                "/toolchain/cupidc_ir.cc",
                "-I",
                "/toolchain",
            ]
            hosted = subprocess.run(
                [
                    str(self.hosted_cupidc_path),
                    *[str(argument) for argument in common],
                    "-o",
                    f"/{relative}/generation-zero.o",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=90,
            )
            first = self.run_cupid_linux_tool(
                self.cupid_cupidc_path,
                [
                    *common,
                    "-o",
                    f"/{relative}/generation-one-first.o",
                ],
                timeout=180,
            )
            second = self.run_cupid_linux_tool(
                self.cupid_cupidc_path,
                [
                    *common,
                    "-o",
                    f"/{relative}/generation-one-second.o",
                ],
                timeout=180,
            )
            self.assertEqual(hosted.returncode, 0, hosted.stderr)
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertEqual(first.stdout, hosted.stdout)
            self.assertEqual(first.stderr, hosted.stderr)
            self.assertEqual(second.stdout, hosted.stdout)
            self.assertEqual(second.stderr, hosted.stderr)
            self.assertEqual(first_object.read_bytes(), hosted_object.read_bytes())
            self.assertEqual(second_object.read_bytes(), hosted_object.read_bytes())
            self.assertGreater(len(hosted_object.read_bytes()), 100000)
            self.assertEqual(hosted_object.read_bytes()[:7], b"\x7fELF\x01\x01\x01")
