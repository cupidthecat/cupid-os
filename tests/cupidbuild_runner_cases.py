## @file
#  @brief Check the guarded runner and its private process state.
import errno
import os
import shutil
import struct
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path

from tests.cupidbuild_cli_fixtures import CUPIDBUILD_DISK_MODULES, REPO_ROOT, TOOLCHAIN_ROOT


class CheckedRunnerCases:
    def test_invalid_command_returns_usage_status_without_output(self):
        result = subprocess.run(
            [str(self.cli_path), "unknown"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )

        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, "")
        self.assertIn("usage: cupidbuild", result.stderr)

    def test_help_names_the_checked_tool_runner(self):
        result = subprocess.run(
            [str(self.cli_path), "--help"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertRegex(
            result.stdout,
            r"cupidbuild run --seed-manifest MANIFEST\s+"
            r"--root ROOT --tool \{cupidc\|cupidobj\|cupidld\} "
            r"\[--timeout SECONDS\]\s+"
            r"\[--seed-release RELEASE\]\s+"
            r"-- TOOL_ARGS\.\.\.",
        )

    def test_checked_tool_runner_matches_admitted_tools_success_and_failure(self):
        cases = (
            ("help", ["--help"], 30),
            ("invalid option", ["--not-a-tool-option"], None),
        )
        for tool in ("cupidc", "cupidobj", "cupidld"):
            executable = self._production_tool(tool)
            for name, arguments, timeout in cases:
                with self.subTest(tool=tool, name=name):
                    direct = subprocess.run(
                        [str(executable), *arguments],
                        cwd=REPO_ROOT,
                        text=True,
                        capture_output=True,
                        timeout=90,
                    )
                    checked = self._run_checked_tool(
                        tool, arguments, timeout=timeout
                    )

                    self.assertEqual(
                        (
                            checked.returncode,
                            checked.stdout,
                            checked.stderr,
                        ),
                        (
                            direct.returncode,
                            direct.stdout,
                            direct.stderr,
                        ),
                    )

    def test_checked_cupidc_runner_compiles_one_exact_relocatable_object(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-cupidc-compile-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "valid.cc"
            direct_output = root / "direct.o"
            checked_output = root / "checked.o"
            source.write_text(
                "int checked_runner_value(void) { return 42; }\n",
                encoding="utf-8",
            )
            common = [
                "--root",
                str(root),
                "--freestanding",
                "-c",
                "/valid.cc",
            ]
            direct = subprocess.run(
                [
                    str(self._production_tool("cupidc")),
                    *common,
                    "-o",
                    "/direct.o",
                ],
                cwd=root,
                text=True,
                capture_output=True,
                timeout=90,
            )
            checked = self._run_checked_tool(
                "cupidc",
                [*common, "-o", "/checked.o"],
                root=root,
            )

            self.assertEqual(direct.returncode, 0, direct.stderr)
            self.assertEqual(
                (checked.returncode, checked.stdout, checked.stderr),
                (direct.returncode, direct.stdout, direct.stderr),
            )
            direct_object = direct_output.read_bytes()
            checked_object = checked_output.read_bytes()
            self.assertEqual(checked_object, direct_object)
            self.assertGreater(len(checked_object), 52)
            self.assertEqual(checked_object[:7], b"\x7fELF\x01\x01\x01")
            self.assertEqual(
                struct.unpack_from("<HHI", checked_object, 16),
                (1, 3, 1),
            )
            self.assertEqual(list(root.glob("*.cupid-tmp-*")), [])

    def test_checked_cupidc_runner_preserves_failed_compile_output(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-cupidc-failure-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "invalid.cc"
            direct_output = root / "direct.o"
            checked_output = root / "checked.o"
            source.write_text(
                "int checked_runner_broken( {\n", encoding="utf-8"
            )
            sentinel = b"checked-cupidc-failure-sentinel"
            direct_output.write_bytes(sentinel)
            checked_output.write_bytes(sentinel)
            common = [
                "--root",
                str(root),
                "--freestanding",
                "-c",
                "/invalid.cc",
            ]
            direct = subprocess.run(
                [
                    str(self._production_tool("cupidc")),
                    *common,
                    "-o",
                    "/direct.o",
                ],
                cwd=root,
                text=True,
                capture_output=True,
                timeout=90,
            )
            checked = self._run_checked_tool(
                "cupidc",
                [*common, "-o", "/checked.o"],
                root=root,
            )

            self.assertEqual(direct.returncode, 1, direct.stderr)
            self.assertEqual(
                (checked.returncode, checked.stdout, checked.stderr),
                (direct.returncode, direct.stdout, direct.stderr),
            )
            self.assertIn("/invalid.cc:1:", checked.stderr)
            self.assertEqual(direct_output.read_bytes(), sentinel)
            self.assertEqual(checked_output.read_bytes(), sentinel)

    def test_checked_cupidld_runner_links_one_exact_fixed_elf(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-cupidld-link-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "entry.asm"
            object_path = root / "entry.o"
            direct_output = root / "direct.elf"
            checked_output = root / "checked.elf"
            source.write_text(
                "BITS 32\n"
                "global _start:function\n"
                "section .text\n"
                "_start:\n"
                "    ret\n",
                encoding="ascii",
            )
            assembled = subprocess.run(
                [
                    str(self._production_tool("cupidasm")),
                    "-f",
                    "elf32",
                    source.name,
                    "-o",
                    object_path.name,
                ],
                cwd=root,
                text=True,
                capture_output=True,
                timeout=90,
            )
            self.assertEqual(assembled.returncode, 0, assembled.stderr)
            self.assertEqual((assembled.stdout, assembled.stderr), ("", ""))
            common = [
                "-m",
                "elf_i386",
                "--text-address",
                "0x01C00000",
                "--entry",
                "_start",
            ]
            direct = subprocess.run(
                [
                    str(self._production_tool("cupidld")),
                    *common,
                    "-o",
                    direct_output.name,
                    object_path.name,
                ],
                cwd=root,
                text=True,
                capture_output=True,
                timeout=90,
            )
            checked = self._run_checked_tool(
                "cupidld",
                [*common, "-o", checked_output.name, object_path.name],
                root=root,
            )

            self.assertEqual(direct.returncode, 0, direct.stderr)
            self.assertEqual(
                (checked.returncode, checked.stdout, checked.stderr),
                (direct.returncode, direct.stdout, direct.stderr),
            )
            direct_image = direct_output.read_bytes()
            checked_image = checked_output.read_bytes()
            self.assertEqual(checked_image, direct_image)
            self.assertGreater(len(checked_image), 52)
            self.assertEqual(checked_image[:4], b"\x7fELF")
            self.assertEqual(
                struct.unpack_from("<HHII", checked_image, 16),
                (2, 3, 1, 0x01C00000),
            )
            self.assertEqual(list(root.glob("*.cupid-tmp-*")), [])

    def test_checked_tool_runner_forwards_more_than_thirty_one_arguments(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-many-arguments-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            demos = root / "demos"
            demos.mkdir()
            demo_paths = []
            for index in range(40):
                source = demos / f"demo{index:02d}.asm"
                source.write_text("bits 32\nret\n", encoding="ascii")
                demo_paths.append(source.relative_to(root).as_posix())
            direct_output = root / "direct.cc"
            checked_output = root / "checked.cc"
            common = ["install-source", "demos", "--demos", *demo_paths]
            direct_arguments = [*common, "-o", direct_output.name]
            checked_arguments = [*common, "-o", checked_output.name]
            self.assertGreater(len(checked_arguments), 31)

            direct = subprocess.run(
                [str(self._production_tool("cupidobj")), *direct_arguments],
                cwd=root,
                text=True,
                capture_output=True,
                timeout=90,
            )
            checked = self._run_checked_tool(
                "cupidobj", checked_arguments, root=root
            )

            self.assertEqual(direct.returncode, 0, direct.stderr)
            self.assertEqual(
                (checked.returncode, checked.stdout, checked.stderr),
                (direct.returncode, direct.stdout, direct.stderr),
            )
            self.assertEqual(checked_output.read_bytes(), direct_output.read_bytes())

    def test_checked_tool_runner_preserves_quoted_arguments(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-quoted-arguments-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            spaced = root / "path with spaces"
            spaced.mkdir()
            source = spaced / "source name.txt"
            source.write_bytes(b"checked quoting input\n")
            direct_output = spaced / "direct output.o"
            checked_output = spaced / "checked output.o"
            identity = 'quoted"identity\\'
            source_argument = source.relative_to(root).as_posix()
            direct_arguments = [
                "wrap-text", source_argument, "-o",
                direct_output.relative_to(root).as_posix(),
                "--identity", identity,
            ]
            checked_arguments = [
                "wrap-text", source_argument, "-o",
                checked_output.relative_to(root).as_posix(),
                "--identity", identity,
            ]

            direct = subprocess.run(
                [str(self._production_tool("cupidobj")), *direct_arguments],
                cwd=root,
                text=True,
                capture_output=True,
                timeout=90,
            )
            checked = self._run_checked_tool(
                "cupidobj", checked_arguments, root=root
            )

            self.assertEqual(direct.returncode, 0, direct.stderr)
            self.assertEqual(
                (checked.returncode, checked.stdout, checked.stderr),
                (direct.returncode, direct.stdout, direct.stderr),
            )
            self.assertEqual(checked_output.read_bytes(), direct_output.read_bytes())

    def test_checked_tool_runner_rejects_missing_or_unsupported_tool(self):
        manifest = self._production_manifest()
        prefix = [
            str(self.cli_path),
            "run",
            "--seed-manifest",
            str(manifest),
            "--root",
            str(REPO_ROOT),
        ]
        cases = (
            ("missing", [*prefix, "--", "--help"]),
            (
                "unsupported",
                [*prefix, "--tool", "cupidasm", "--", "--help"],
            ),
        )
        for name, command in cases:
            with self.subTest(name=name):
                result = subprocess.run(
                    command,
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=90,
                )

                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, "")
                self.assertIn("usage: cupidbuild run", result.stderr)

    def test_checked_tool_runner_requires_the_argument_separator(self):
        result = subprocess.run(
            [
                str(self.cli_path),
                "run",
                "--seed-manifest",
                str(self._production_manifest()),
                "--root",
                str(REPO_ROOT),
                "--tool",
                "cupidobj",
                "--help",
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=90,
        )

        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, "")
        self.assertIn("usage: cupidbuild run", result.stderr)

    def test_checked_tool_runner_rejects_duplicate_options_and_bad_timeouts(self):
        manifest = str(self._production_manifest())
        root = str(REPO_ROOT)
        required = [
            "--seed-manifest", manifest,
            "--root", root,
            "--tool", "cupidobj",
        ]
        cases = {
            "duplicate manifest": ["--seed-manifest", manifest],
            "duplicate root": ["--root", root],
            "duplicate tool": ["--tool", "cupidobj"],
            "duplicate timeout": ["--timeout", "1", "--timeout", "2"],
            "zero timeout": ["--timeout", "0"],
            "negative timeout": ["--timeout", "-1"],
            "too large timeout": ["--timeout", "86401"],
            "nonnumeric timeout": ["--timeout", "soon"],
        }
        for name, extra in cases.items():
            with self.subTest(name=name):
                result = subprocess.run(
                    [
                        str(self.cli_path), "run", *required, *extra,
                        "--", "--help",
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=90,
                )

                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, "")
                self.assertIn("usage: cupidbuild run", result.stderr)

    def test_checked_tool_runner_removes_private_roots_after_tool_exit(self):
        cases = (
            ("success", ["--help"], 0),
            ("tool failure", ["--not-a-cupidobj-option"], 2),
        )
        for name, arguments, expected_status in cases:
            with self.subTest(name=name):
                before = self._run_private_roots()
                result = self._run_checked_tool("cupidobj", arguments)
                after = self._run_private_roots()

                self.assertEqual(after, before)
                self.assertEqual(
                    result.returncode,
                    expected_status,
                    result.stderr,
                )

    def test_checked_tool_runner_times_out_and_cleans_up(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-timeout-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "large.txt"
            output = root / "large.o"
            source.write_bytes(b"checked timeout input\n" * 1_500_000)

            result = self._run_checked_tool(
                "cupidobj",
                ["wrap-text", source.name, "-o", output.name],
                root=root,
                timeout=1,
            )

            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertIn("checked CupidObj timed out", result.stderr)
            self.assertEqual(list(root.glob(".cupidbuild-run-*")), [])

    def test_checked_tool_runner_reports_seed_drift_after_timeout(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-timeout-drift-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            source = root / "large.txt"
            output = root / "large.o"
            source.write_bytes(b"checked timeout drift input\n" * 1_500_000)
            ready = root / "tool-launch-ready"
            resume = root / "tool-launch-resume"
            changed = threading.Event()
            mutation_errors = []

            def change_manifest_after_launch():
                try:
                    deadline = time.monotonic() + 20
                    while time.monotonic() < deadline:
                        if ready.is_file():
                            manifest.write_bytes(manifest.read_bytes() + b" \n")
                            changed.set()
                            return
                        time.sleep(0.001)
                    mutation_errors.append("checked tool launch was not observed")
                except Exception as error:
                    mutation_errors.append(error)
                finally:
                    try:
                        resume.write_bytes(b"continue")
                    except Exception as error:
                        mutation_errors.append(error)

            environment = os.environ.copy()
            environment["CUPIDBUILD_PUBLICATION_TEST_PHASE"] = (
                "after-tool-launch"
            )
            environment["CUPIDBUILD_PUBLICATION_TEST_READY"] = str(ready)
            environment["CUPIDBUILD_PUBLICATION_TEST_RESUME"] = str(resume)
            mutator = threading.Thread(
                target=change_manifest_after_launch, daemon=True
            )
            mutator.start()
            result = self._run_checked_tool(
                "cupidobj",
                ["wrap-text", source.name, "-o", output.name],
                root=root,
                timeout=1,
                manifest=manifest,
                cli=self.race_cli_path,
                env=environment,
            )
            mutator.join(timeout=20)

            self.assertFalse(mutator.is_alive(), "the seed mutator did not stop")
            self.assertFalse(mutation_errors, repr(mutation_errors))
            self.assertTrue(changed.is_set(), "checked tool launch was not observed")
            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertIn("checked seed inputs changed", result.stderr)
            self.assertNotIn("checked CupidObj timed out", result.stderr)
            self.assertEqual(list(root.glob(".cupidbuild-run-*")), [])

    @unittest.skipIf(os.name == "nt", "POSIX fork and descriptor cleanup")
    def test_checked_tool_runner_cleans_up_after_launch_pause_failure(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-pause-failure-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            ready = root / "ready-is-a-directory"
            ready.mkdir()
            source = root / "pause_cleanup.cc"
            executable = root / "pause_cleanup"
            source.write_text(
                r'''#define _GNU_SOURCE
#include "cupidbuild.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

pid_t __real_fork(void);
pid_t __real_waitpid(pid_t child, int *status, int options);
int __real_pipe(int descriptors[2]);
pid_t __wrap_fork(void);
pid_t __wrap_waitpid(pid_t child, int *status, int options);
int __wrap_pipe(int descriptors[2]);

static pid_t observed_child = -1;
static int child_was_live;
static int child_was_killed;
static int launch_pipe[2] = {-1, -1};

pid_t __wrap_fork(void) {
  pid_t child = __real_fork();
  if (child == 0) {
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0) _exit(89);
    if (raise(SIGSTOP) != 0) _exit(90);
  } else if (child > 0) {
    int status = 0;
    pid_t waited;
    observed_child = child;
    do {
      waited = __real_waitpid(child, &status, WUNTRACED);
    } while (waited < 0 && errno == EINTR);
    child_was_live = waited == child && WIFSTOPPED(status);
  }
  return child;
}

pid_t __wrap_waitpid(pid_t child, int *status, int options) {
  pid_t waited = __real_waitpid(child, status, options);
  if (waited == observed_child && status != NULL && WIFSIGNALED(*status) &&
      WTERMSIG(*status) == SIGKILL) child_was_killed = 1;
  return waited;
}

int __wrap_pipe(int descriptors[2]) {
  int result = __real_pipe(descriptors);
  if (result == 0) {
    launch_pipe[0] = descriptors[0];
    launch_pipe[1] = descriptors[1];
  }
  return result;
}

static int descriptor_count(void) {
  DIR *directory = opendir("/proc/self/fd");
  struct dirent *entry;
  int count = 0;
  if (directory == NULL) return -1;
  while ((entry = readdir(directory)) != NULL) {
    if (entry->d_name[0] >= '0' && entry->d_name[0] <= '9') count++;
  }
  if (closedir(directory) != 0) return -1;
  return count;
}

int main(int argc, char **argv) {
  const char *arguments[] = {"--help", NULL};
  cupidbuild_run_request_t request;
  int descriptors_before;
  int status;
  int index;
  int child_status = 0;
  int failure = 0;
  if (argc != 4) return 91;
  request.seed_manifest = argv[1];
  request.working_directory = argv[2];
  request.tool = "cupidobj";
  request.arguments = arguments;
  request.timeout_seconds = 1u;
  descriptors_before = descriptor_count();
  status = cupidbuild_run_checked_tool_with_release(&request, argv[3]);
  if (status != 1 || !child_was_live || !child_was_killed) failure = 92;
  errno = 0;
  if (__real_waitpid(observed_child, &child_status, WNOHANG) != -1 ||
      errno != ECHILD) failure = 93;
  for (index = 0; index < 2; index++) {
    errno = 0;
    if (launch_pipe[index] < 0 || fcntl(launch_pipe[index], F_GETFD) != -1 ||
        errno != EBADF) failure = 94;
  }
  if (descriptors_before < 0 || descriptor_count() != descriptors_before)
    failure = 95;
  if (failure != 0) {
    if (observed_child > 0) {
      (void)kill(observed_child, SIGKILL);
      (void)__real_waitpid(observed_child, &child_status, 0);
    }
    fprintf(stderr, "pause cleanup contract failed: %d\n", failure);
    return failure;
  }
  puts("pause cleanup contract: live child killed and reaped; descriptors closed");
  return 0;
}
''',
                encoding="ascii",
            )
            objects = [
                self.race_cli_path.parent / f"{name}.o"
                for name in (
                    "ctool", "ctool_host", "elf32", "cupidbuild_host", "cupidbuild",
                    "seed_manifest", "seed_release", "contract_parse_internal",
                    "path_encoding", *CUPIDBUILD_DISK_MODULES,
                )
            ]
            built = subprocess.run(
                [
                    "gcc", "-x", "c", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(TOOLCHAIN_ROOT), str(source), "-x", "none",
                    *(str(path) for path in objects),
                    "-Wl,--wrap=fork", "-Wl,--wrap=waitpid", "-Wl,--wrap=pipe",
                    "-o", str(executable),
                ],
                cwd=root, text=True, capture_output=True, timeout=90,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            before = set(root.rglob("*"))
            environment = os.environ.copy()
            environment["CUPIDBUILD_PUBLICATION_TEST_PHASE"] = "after-tool-launch"
            environment["CUPIDBUILD_PUBLICATION_TEST_READY"] = str(ready)
            environment["CUPIDBUILD_PUBLICATION_TEST_RESUME"] = str(root / "resume")
            result = subprocess.run(
                [str(executable), str(manifest), str(root), str(root / "release.json")],
                cwd=root, env=environment, text=True, capture_output=True, timeout=10,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(
                result.stdout,
                "pause cleanup contract: live child killed and reaped; descriptors closed\n",
            )
            self.assertEqual(
                result.stderr,
                "cupidbuild: checked tool could not be started or captured\n",
            )
            self.assertEqual(set(root.rglob("*")), before)
            self.assertEqual(self._private_roots(root), set())

    @unittest.skipUnless(os.name == "nt", "Windows uses private runner roots")
    def test_checked_tool_runner_fails_when_private_cleanup_is_incomplete(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-cleanup-failure-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "large.txt"
            output = root / "large.o"
            source.write_bytes(b"checked cleanup input\n" * 1_000_000)
            planted = threading.Event()

            def plant_unowned_file_after_tool_starts():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    for private in root.glob(".cupidbuild-run-*"):
                        if private.name.startswith(
                            ".cupidbuild-run-cleanup-failure-"
                        ):
                            continue
                        if (private.joinpath("tool.stdout").is_file()):
                            private.joinpath("unowned.txt").write_text(
                                "leave me alone\n", encoding="ascii"
                            )
                            planted.set()
                            return
                    time.sleep(0.001)

            mutator = threading.Thread(
                target=plant_unowned_file_after_tool_starts, daemon=True
            )
            mutator.start()
            result = self._run_checked_tool(
                "cupidobj",
                ["wrap-text", source.name, "-o", output.name],
                root=root,
            )
            mutator.join(timeout=20)
            private_roots = list(root.glob(".cupidbuild-run-*"))

            try:
                self.assertTrue(
                    planted.is_set(), "checked tool launch was not observed"
                )
                self.assertEqual(result.returncode, 1)
                self.assertIn(
                    "private checked-tool cleanup failed", result.stderr
                )
                self.assertEqual(len(private_roots), 1)
                self.assertEqual(
                    private_roots[0].joinpath("unowned.txt").read_text(
                        encoding="ascii"
                    ),
                    "leave me alone\n",
                )
            finally:
                for private in private_roots:
                    shutil.rmtree(private)

    @unittest.skipUnless(os.name == "nt", "Windows uses named stream files")
    def test_checked_tool_runner_does_not_delete_a_planted_stream_path(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-planted-stream-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            planted = threading.Event()

            def plant_stdout_after_private_root_appears():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    for private in root.glob(".cupidbuild-run-*"):
                        stream = private / "tool.stdout"
                        if not stream.exists():
                            try:
                                stream.write_text(
                                    "unowned stream\n", encoding="ascii"
                                )
                            except OSError:
                                continue
                            planted.set()
                            return
                    time.sleep(0.001)

            mutator = threading.Thread(
                target=plant_stdout_after_private_root_appears, daemon=True
            )
            mutator.start()
            result = self._run_checked_tool("cupidobj", ["--help"], root=root)
            mutator.join(timeout=20)
            private_roots = list(root.glob(".cupidbuild-run-*"))

            try:
                self.assertTrue(planted.is_set(), "private root was not observed")
                self.assertEqual(result.returncode, 1)
                self.assertIn(
                    "private checked-tool cleanup failed", result.stderr
                )
                planted_path = next(
                    private / "tool.stdout"
                    for private in private_roots
                    if private.joinpath("tool.stdout").is_file()
                )
                self.assertEqual(
                    planted_path.read_text(encoding="ascii"),
                    "unowned stream\n",
                )
            finally:
                for private in private_roots:
                    shutil.rmtree(private)

    @unittest.skipUnless(os.name == "nt", "Windows uses named frozen inputs")
    def test_checked_tool_runner_seals_private_seed_before_launch(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-private-seed-seal-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "large.txt"
            output = root / "large.o"
            source.write_bytes(b"private seed drift input\n" * 1_000_000)
            ready = root / "tool-launch-ready"
            resume = root / "tool-launch-resume"
            denied = threading.Event()
            mutation_errors = []

            def try_to_change_frozen_tool_after_launch():
                try:
                    deadline = time.monotonic() + 20
                    while time.monotonic() < deadline:
                        if ready.is_file():
                            frozen = next(
                                (
                                    path
                                    for private in root.glob(".cupidbuild-run-*")
                                    for path in private.glob("cupidc.*")
                                ),
                                None,
                            )
                            if frozen is None:
                                raise AssertionError("frozen CupidObj is missing")
                            try:
                                with frozen.open("ab") as stream:
                                    stream.write(b"drift")
                            except OSError:
                                denied.set()
                            else:
                                mutation_errors.append(
                                    "the sealed frozen tool accepted a write"
                                )
                            return
                        time.sleep(0.001)
                    mutation_errors.append("tool launch checkpoint was not observed")
                except Exception as error:
                    mutation_errors.append(error)
                finally:
                    try:
                        resume.write_bytes(b"continue")
                    except Exception as error:
                        mutation_errors.append(error)

            mutator = threading.Thread(
                target=try_to_change_frozen_tool_after_launch, daemon=True
            )
            environment = os.environ.copy()
            environment["CUPIDBUILD_PUBLICATION_TEST_PHASE"] = "after-tool-launch"
            environment["CUPIDBUILD_PUBLICATION_TEST_READY"] = str(ready)
            environment["CUPIDBUILD_PUBLICATION_TEST_RESUME"] = str(resume)
            mutator.start()
            result = self._run_checked_tool(
                "cupidobj",
                ["wrap-text", source.name, "-o", output.name],
                root=root,
                cli=self.race_cli_path,
                env=environment,
            )
            mutator.join(timeout=20)
            private_roots = list(root.glob(".cupidbuild-run-*"))

            try:
                self.assertFalse(mutator.is_alive(), "the seed mutator did not stop")
                self.assertFalse(mutation_errors, repr(mutation_errors))
                self.assertTrue(denied.is_set(), "the frozen tool was not sealed")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertEqual(result.stderr, "")
                self.assertTrue(output.is_file())
                self.assertEqual(private_roots, [])
            finally:
                for private in private_roots:
                    shutil.rmtree(private)

    @unittest.skipIf(os.name == "nt", "Linux uses anonymous runner files")
    def test_checked_tool_runner_uses_sealed_anonymous_inputs(self):
        import fcntl

        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-anonymous-inputs-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "large.txt"
            output = root / "large.o"
            source.write_bytes(b"anonymous checked input\n" * 1_500_000)
            ready = root / "launch.ready"
            resume = root / "launch.resume"
            command = self._checked_tool_command(
                "cupidobj", ["wrap-text", source.name, "-o", output.name],
                root=root,
            )
            command[0] = str(self.race_cli_path)
            environment = os.environ.copy()
            environment["CUPIDBUILD_PUBLICATION_TEST_PHASE"] = "after-tool-launch"
            environment["CUPIDBUILD_PUBLICATION_TEST_READY"] = str(ready)
            environment["CUPIDBUILD_PUBLICATION_TEST_RESUME"] = str(resume)
            process = subprocess.Popen(
                command, cwd=root, text=True, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, env=environment,
            )
            metadata_changed = False
            try:
                # A memfd exists while it is still being filled. Observe the
                # completed capture at launch, not a racy descriptor count.
                deadline = time.monotonic() + 20
                while not ready.exists() and process.poll() is None:
                    if time.monotonic() >= deadline:
                        break
                    time.sleep(0.001)
                self.assertTrue(ready.is_file(), "tool launch checkpoint was not observed")
                self.assertEqual(list(root.glob(".cupidbuild-run-*")), [])
                descriptors = Path(f"/proc/{process.pid}/fd")
                links = {path: os.readlink(path) for path in descriptors.iterdir()}
                frozen = [
                    path for path, link in links.items()
                    if "/memfd:" in link
                    and "cupidbuild-stdout" not in link
                    and "cupidbuild-stderr" not in link
                ]
                self.assertCountEqual(
                    [links[path] for path in frozen],
                    [f"/memfd:{name} (deleted)" for name in (
                        "manifest.json", "seed-release.json", "cupidasm.elf",
                        "cupidc.elf", "cupiddis.elf", "cupidld.elf",
                        "cupidobj.elf", "cupidbuild.elf",
                    )],
                )
                for path in frozen:
                    descriptor = os.open(path, os.O_RDONLY)
                    try:
                        self.assertEqual(fcntl.fcntl(descriptor, 1034), 15)
                    finally:
                        os.close(descriptor)
                    try:
                        descriptor = os.open(path, os.O_RDWR)
                    except OSError as error:
                        # The executing tool also has the kernel's text lock.
                        self.assertEqual(error.errno, errno.ETXTBSY)
                        self.assertIn("/memfd:cupidobj.elf ", links[path])
                    else:
                        try:
                            before = os.fstat(descriptor)
                            with self.assertRaises(OSError):
                                os.write(descriptor, b"drift")
                            with self.assertRaises(OSError):
                                os.ftruncate(descriptor, 0)
                            after = os.fstat(descriptor)
                            self.assertEqual(after.st_size, before.st_size)
                            # This fixture builds the hosted POSIX adapter, whose
                            # snapshot stores st_mtime in whole seconds.
                            metadata_changed |= (
                                after.st_mtime_ns // 1_000_000_000
                                != before.st_mtime_ns // 1_000_000_000
                            )
                        finally:
                            os.close(descriptor)
            finally:
                resume.write_bytes(b"continue")
                try:
                    stdout, stderr = process.communicate(timeout=90)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.communicate()
                    raise

            if metadata_changed:
                # Linux may update mtime before a sealed write is rejected.
                # The retained snapshot must reject that drift too.
                self.assertNotEqual(process.returncode, 0)
                self.assertIn("private checked seed changed while checked tool ran", stderr)
            else:
                self.assertEqual(process.returncode, 0, stderr)
                self.assertEqual(stderr, "")
            self.assertEqual(stdout, "")
            self.assertTrue(output.is_file())
            self.assertEqual(list(root.glob(".cupidbuild-run-*")), [])

    @unittest.skipUnless(os.name == "nt", "Windows uses private runner roots")
    def test_checked_tool_runner_does_not_delete_a_replacement_private_root(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-directory-replacement-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "large.txt"
            output = root / "large.o"
            stolen = root / "stolen-private-root"
            source.write_bytes(b"checked replacement input\n" * 1_000_000)
            attempted = threading.Event()
            replaced = threading.Event()

            def replace_private_root_after_tool_starts():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    for private in root.glob(".cupidbuild-run-*"):
                        if private.name.startswith(
                            ".cupidbuild-run-directory-replacement-"
                        ):
                            continue
                        if not private.joinpath("tool.stdout").is_file():
                            continue
                        attempted.set()
                        try:
                            private.rename(stolen)
                        except OSError:
                            return
                        private.mkdir()
                        private.joinpath("replacement.txt").write_text(
                            "replacement stays\n", encoding="ascii"
                        )
                        replaced.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(
                target=replace_private_root_after_tool_starts, daemon=True
            )
            mutator.start()
            result = self._run_checked_tool(
                "cupidobj",
                ["wrap-text", source.name, "-o", output.name],
                root=root,
            )
            mutator.join(timeout=20)
            private_roots = list(root.glob(".cupidbuild-run-*"))

            try:
                self.assertTrue(
                    attempted.is_set(), "checked tool launch was not observed"
                )
                self.assertFalse(
                    replaced.is_set(),
                    "the pinned Windows directory was renamed",
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(private_roots, [])
            finally:
                for private in private_roots:
                    shutil.rmtree(private)
                if stolen.exists():
                    shutil.rmtree(stolen)

    def test_checked_tool_runner_suppresses_output_after_live_seed_drift(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-seed-drift-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            source = root / "large.txt"
            output = root / "large.o"
            source.write_bytes(b"checked runner input\n" * 1_500_000)
            changed = threading.Event()

            def change_manifest_after_tool_starts():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    if output.exists():
                        manifest.write_bytes(manifest.read_bytes() + b" \n")
                        changed.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(
                target=change_manifest_after_tool_starts, daemon=True
            )
            mutator.start()
            result = self._run_checked_tool(
                "cupidobj",
                ["wrap-text", source.name, "-o", output.name],
                root=root,
                manifest=manifest,
            )
            mutator.join(timeout=20)

            self.assertTrue(changed.is_set(), "checked tool launch was not observed")
            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertIn("checked seed inputs changed", result.stderr)
            self.assertEqual(set(root.glob(".cupidbuild-run-*")), set())

    def test_private_transaction_census_covers_both_host_layouts(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-transaction-census-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            directory_token = root / ".cupidbuild-object-00000001"
            flat_token = root / ".cupidbuild-object-00000002.reserve"
            orphaned_flat_token = root / ".cupidbuild-object-00000003.reserve"
            cleanup_alias_token = root / ".cupidbuild-object-00000004.reserve"
            directory_token.mkdir()
            flat_token.write_bytes(b"")
            (root / ".cupidbuild-object-00000003.tool.stderr").write_bytes(
                b"orphaned stream"
            )
            (
                root
                / "..cupidbuild-object-00000004.candidate.o."
                "cleanup-link-00001234"
            ).write_bytes(b"orphaned cleanup alias")
            (root / ".cupidbuild-object-not-a-token.reserve").write_bytes(
                b"unrelated"
            )

            self.assertEqual(
                self._private_roots(root),
                {
                    directory_token,
                    flat_token,
                    orphaned_flat_token,
                    cleanup_alias_token,
                },
            )
            self.assertEqual(
                self._private_entry(directory_token, "candidate.o"),
                directory_token / "candidate.o",
            )
            self.assertEqual(
                self._private_entry(flat_token, "candidate.o"),
                root / ".cupidbuild-object-00000002.candidate.o",
            )

    @unittest.skipIf(os.name == "nt", "POSIX uses the flat transaction namespace")
    def test_posix_private_tool_uses_proc_without_relative_repository_writes(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-proc-cwd-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text(
                "bits 32\nsection .text\nglobal entry\nentry:\n"
                + "nop\n" * 10_000
                + "ret\n",
                encoding="ascii",
            )
            sentinels = {
                root / name: f"owned {name}\n".encode("ascii")
                for name in (
                    "candidate.o",
                    "candidate.map",
                    "source.asm",
                    "tool.stdout",
                    "tool.stderr",
                )
            }
            for path, payload in sentinels.items():
                path.write_bytes(payload)
            before = self._private_roots(root)
            process = subprocess.Popen(
                self._assembly_command(
                    "assemble-cupidasm-object",
                    source,
                    output,
                    manifest=manifest,
                    root=root,
                ),
                cwd=root,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            deadline = time.monotonic() + 20
            observed_proc_cwd = False
            children_path = Path(
                f"/proc/{process.pid}/task/{process.pid}/children"
            )
            while time.monotonic() < deadline and process.poll() is None:
                try:
                    child_pids = children_path.read_text(
                        encoding="ascii"
                    ).split()
                except (FileNotFoundError, PermissionError, ProcessLookupError):
                    child_pids = []
                for child_pid in child_pids:
                    try:
                        if os.readlink(f"/proc/{child_pid}/cwd") == "/proc":
                            observed_proc_cwd = True
                            break
                    except (FileNotFoundError, PermissionError, ProcessLookupError):
                        continue
                if observed_proc_cwd:
                    break
                time.sleep(0.001)
            stdout, stderr = process.communicate(timeout=90)

            self.assertEqual(process.returncode, 0, stderr)
            self.assertEqual((stdout, stderr), ("", ""))
            self.assertTrue(observed_proc_cwd, "the private tool cwd was not observed")
            self.assertEqual(output.read_bytes()[:7], b"\x7fELF\x01\x01\x01")
            for path, payload in sentinels.items():
                self.assertEqual(path.read_bytes(), payload)
            self.assertEqual(self._private_roots(root), before)

    @unittest.skipIf(os.name == "nt", "POSIX assigns numeric file descriptors")
    def test_posix_private_tool_launches_with_closed_standard_descriptors(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-low-fds-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text(
                "bits 32\nsection .text\nglobal entry\nentry: nop\nret\n",
                encoding="ascii",
            )
            before = self._private_roots(root)

            def close_standard_descriptors():
                for descriptor in (0, 1, 2):
                    try:
                        os.close(descriptor)
                    except OSError:
                        pass

            result = self._run_object(
                source,
                output,
                manifest,
                root=root,
                preexec_fn=close_standard_descriptors,
            )

            self.assertEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes()[:7], b"\x7fELF\x01\x01\x01")
            self.assertEqual(self._private_roots(root), before)
