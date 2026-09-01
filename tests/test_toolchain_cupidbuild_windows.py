import os
import shlex
import shutil
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"


def _host_compiler():
    configured = os.environ.get("CC")
    candidates = []
    if configured:
        command = shlex.split(configured, posix=os.name != "nt")
        if os.name == "nt":
            command = [
                item[1:-1]
                if len(item) >= 2 and item[0] == item[-1] == '"'
                else item
                for item in command
            ]
        candidates.append(command)
    candidates.extend(([name] for name in ("clang", "gcc", "cc")))
    for command in candidates:
        if command and shutil.which(command[0]):
            return command
    raise unittest.SkipTest("a hosted C compiler is required")


@unittest.skipUnless(os.name == "nt", "native Win32 process test")
class CupidBuildWindowsProcessTests(unittest.TestCase):
    def test_frozen_input_allows_legacy_read_sharing_but_rejects_writes(self):
        compiler = _host_compiler()
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-frozen-sharing-", dir=TOOLCHAIN_ROOT
        ) as temporary:
            build_root = Path(temporary)
            root = build_root / "repository"
            root.mkdir()
            (root / "source.txt").write_text("frozen input\n", encoding="utf-8")
            driver_source = build_root / "frozen_sharing_driver.cc"
            driver = build_root / "frozen_sharing_driver.exe"

            driver_source.write_text(
                "#include <windows.h>\n"
                '#include "cupidbuild_host.h"\n'
                "int main(int argc, char **argv) {\n"
                "  cupidbuild_host_transaction_t *transaction = 0;\n"
                "  HANDLE reader = INVALID_HANDLE_VALUE;\n"
                "  HANDLE writer = INVALID_HANDLE_VALUE;\n"
                "  const char *frozen;\n"
                "  int cleaned;\n"
                "  if (argc != 2) return 10;\n"
                '  if (!cupidbuild_host_transaction_open(argv[1], "source.txt",\n'
                '          "output.bin", &transaction)) {\n'
                "    (void)cupidbuild_host_transaction_close(transaction);\n"
                "    return 11;\n"
                "  }\n"
                "  frozen = cupidbuild_host_frozen_source(transaction);\n"
                "  reader = CreateFileA(frozen, GENERIC_READ, FILE_SHARE_READ, 0,\n"
                "      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);\n"
                "  if (reader == INVALID_HANDLE_VALUE) {\n"
                "    (void)cupidbuild_host_transaction_close(transaction);\n"
                "    return 12;\n"
                "  }\n"
                "  writer = CreateFileA(frozen, GENERIC_WRITE,\n"
                "      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0,\n"
                "      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);\n"
                "  if (writer != INVALID_HANDLE_VALUE) {\n"
                "    (void)CloseHandle(writer);\n"
                "    (void)CloseHandle(reader);\n"
                "    (void)cupidbuild_host_transaction_close(transaction);\n"
                "    return 13;\n"
                "  }\n"
                "  if (!CloseHandle(reader)) {\n"
                "    (void)cupidbuild_host_transaction_close(transaction);\n"
                "    return 14;\n"
                "  }\n"
                "  cleaned = cupidbuild_host_transaction_close(transaction);\n"
                "  return cleaned ? 0 : 15;\n"
                "}\n",
                encoding="utf-8",
                newline="\n",
            )

            built = subprocess.run(
                [
                    *compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-x",
                    "c",
                    "-I",
                    str(TOOLCHAIN_ROOT),
                    str(driver_source),
                    str(TOOLCHAIN_ROOT / "cupidbuild_host.cc"),
                    "-o",
                    str(driver),
                    "-lntdll",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=180,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)

            launched = subprocess.run(
                [str(driver), str(root)],
                cwd=root,
                text=True,
                capture_output=True,
                timeout=30,
            )

            self.assertEqual(
                launched.returncode,
                0,
                launched.stdout + launched.stderr,
            )
            self.assertEqual(list(root.glob(".cupidbuild-*")), [])

    @unittest.skip(
        "the promoted Windows CupidObj predates shared runtime output handles"
    )
    def test_checked_tool_can_replace_its_retained_private_candidate(self):
        compiler = _host_compiler()
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-private-candidate-", dir=TOOLCHAIN_ROOT
        ) as temporary:
            build_root = Path(temporary)
            root = build_root / "repository"
            root.mkdir()
            source = root / "source.txt"
            source.write_text("private candidate input\n", encoding="utf-8")
            tool = root / "cupidobj.exe"
            shutil.copy2(
                REPO_ROOT / "bootstrap" / "seeds" / "i386-windows" / "cupidobj.exe",
                tool,
            )
            driver_source = build_root / "private_candidate_driver.cc"
            driver = build_root / "private_candidate_driver.exe"

            driver_source.write_text(
                "#include <stdio.h>\n"
                "#include <stdlib.h>\n"
                '#include "cupidbuild_host.h"\n'
                "int main(int argc, char **argv) {\n"
                "  cupidbuild_host_transaction_t *transaction = 0;\n"
                "  cupidbuild_host_snapshot_t tool_snapshot;\n"
                "  cupidbuild_host_snapshot_t candidate_snapshot;\n"
                "  const char *frozen_tool = 0;\n"
                "  const char *arguments[5];\n"
                "  unsigned char *candidate = 0;\n"
                "  int result;\n"
                "  int cleaned;\n"
                "  if (argc != 3) return 10;\n"
                '  if (!cupidbuild_host_transaction_open(argv[1], "source.txt",\n'
                '          "output.o", &transaction)) {\n'
                '    (void)fprintf(stderr, "open: %s\\n",\n'
                "                  cupidbuild_host_error(transaction));\n"
                "    (void)cupidbuild_host_transaction_close(transaction);\n"
                "    return 11;\n"
                "  }\n"
                "  if (!cupidbuild_host_freeze_input(transaction, argv[2],\n"
                '          "cupidobj.exe", &frozen_tool, &tool_snapshot) ||\n'
                "      !cupidbuild_host_make_input_executable(transaction,\n"
                "                                               frozen_tool)) {\n"
                '    (void)fprintf(stderr, "freeze: %s\\n",\n'
                "                  cupidbuild_host_error(transaction));\n"
                "    (void)cupidbuild_host_transaction_close(transaction);\n"
                "    return 12;\n"
                "  }\n"
                '  arguments[0] = "wrap-text";\n'
                "  arguments[1] = cupidbuild_host_frozen_source(transaction);\n"
                '  arguments[2] = "-o";\n'
                "  arguments[3] = cupidbuild_host_candidate(transaction);\n"
                "  arguments[4] = 0;\n"
                "  result = cupidbuild_host_run_in_private(\n"
                "      transaction, frozen_tool, arguments, 10000u);\n"
                "  if (result != 0)\n"
                '    (void)fprintf(stderr, "run %d: %s\\n", result,\n'
                "                  cupidbuild_host_error(transaction));\n"
                "  if (result == 0 && !cupidbuild_host_capture_candidate(\n"
                "          transaction, &candidate_snapshot, &candidate)) {\n"
                '    (void)fprintf(stderr, "capture: %s\\n",\n'
                "                  cupidbuild_host_error(transaction));\n"
                "    result = -1;\n"
                "  }\n"
                "  free(candidate);\n"
                "  cleaned = cupidbuild_host_transaction_close(transaction);\n"
                "  if (result != 0) return 13;\n"
                "  return cleaned ? 0 : 14;\n"
                "}\n",
                encoding="utf-8",
                newline="\n",
            )

            built = subprocess.run(
                [
                    *compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-x",
                    "c",
                    "-I",
                    str(TOOLCHAIN_ROOT),
                    str(driver_source),
                    str(TOOLCHAIN_ROOT / "cupidbuild_host.cc"),
                    "-o",
                    str(driver),
                    "-lntdll",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=180,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)

            launched = subprocess.run(
                [str(driver), str(root), str(tool)],
                cwd=root,
                text=True,
                capture_output=True,
                timeout=30,
            )

            self.assertEqual(
                launched.returncode,
                0,
                launched.stdout + launched.stderr,
            )

    def test_failed_candidate_seal_retains_cleanup_authority(self):
        compiler = _host_compiler()
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-candidate-cleanup-", dir=TOOLCHAIN_ROOT
        ) as temporary:
            build_root = Path(temporary)
            root = build_root / "repository"
            root.mkdir()
            (root / "source.txt").write_text("candidate input\n", encoding="utf-8")
            writer_source = build_root / "candidate_writer.cc"
            writer = root / "candidate_writer.exe"
            driver_source = build_root / "candidate_cleanup_driver.cc"
            driver = build_root / "candidate_cleanup_driver.exe"

            writer_source.write_text(
                "#define WIN32_LEAN_AND_MEAN\n"
                "#include <windows.h>\n"
                "int main(int argc, char **argv) {\n"
                '  static const char bytes[] = "candidate\\n";\n'
                "  DWORD written = 0;\n"
                "  HANDLE output;\n"
                "  if (argc != 2) return 2;\n"
                "  output = CreateFileA(argv[1], GENERIC_WRITE,\n"
                "      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,\n"
                "      0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);\n"
                "  if (output == INVALID_HANDLE_VALUE) return 3;\n"
                "  if (!WriteFile(output, bytes, sizeof(bytes) - 1u, &written, 0) ||\n"
                "      written != sizeof(bytes) - 1u) {\n"
                "    (void)CloseHandle(output);\n"
                "    return 4;\n"
                "  }\n"
                "  if (!CloseHandle(output)) return 5;\n"
                "  Sleep(1000u);\n"
                "  return 0;\n"
                "}\n",
                encoding="utf-8",
                newline="\n",
            )

            driver_source.write_text(
                "#define WIN32_LEAN_AND_MEAN\n"
                "#include <windows.h>\n"
                "#include <stdio.h>\n"
                '#include "cupidbuild_host.h"\n'
                "typedef struct {\n"
                "  const char *path;\n"
                "  HANDLE ready;\n"
                "  HANDLE release;\n"
                "  HANDLE file;\n"
                "} blocker_context_t;\n"
                "static DWORD WINAPI hold_candidate(void *opaque) {\n"
                "  blocker_context_t *context = (blocker_context_t *)opaque;\n"
                "  ULONGLONG deadline = GetTickCount64() + 10000u;\n"
                "  while (GetTickCount64() < deadline) {\n"
                "    context->file = CreateFileA(context->path,\n"
                "        GENERIC_WRITE | DELETE,\n"
                "        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,\n"
                "        0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);\n"
                "    if (context->file != INVALID_HANDLE_VALUE) {\n"
                "      (void)SetEvent(context->ready);\n"
                "      (void)WaitForSingleObject(context->release, INFINITE);\n"
                "      (void)CloseHandle(context->file);\n"
                "      return 0;\n"
                "    }\n"
                "    Sleep(1u);\n"
                "  }\n"
                "  return 1;\n"
                "}\n"
                "int main(int argc, char **argv) {\n"
                "  cupidbuild_host_transaction_t *transaction = 0;\n"
                "  cupidbuild_host_snapshot_t snapshot;\n"
                "  const char *frozen = 0;\n"
                "  const char *arguments[2];\n"
                "  blocker_context_t context;\n"
                "  HANDLE blocker_thread;\n"
                "  char candidate[8192];\n"
                "  int result;\n"
                "  int cleaned;\n"
                "  DWORD missing_error;\n"
                "  if (argc != 3) return 10;\n"
                '  if (!cupidbuild_host_transaction_open(argv[1], "source.txt",\n'
                '          "output.o", &transaction)) return 11;\n'
                "  if (snprintf(candidate, sizeof(candidate), \"%s\",\n"
                "          cupidbuild_host_candidate(transaction)) < 0) {\n"
                "    (void)cupidbuild_host_transaction_close(transaction);\n"
                "    return 17;\n"
                "  }\n"
                "  if (!cupidbuild_host_freeze_input(transaction, argv[2],\n"
                '          "candidate-writer.exe", &frozen, &snapshot) ||\n'
                "      !cupidbuild_host_make_input_executable(transaction, frozen)) {\n"
                "    (void)cupidbuild_host_transaction_close(transaction);\n"
                "    return 18;\n"
                "  }\n"
                "  arguments[0] = candidate;\n"
                "  arguments[1] = 0;\n"
                "  context.path = candidate;\n"
                "  context.ready = CreateEventA(0, TRUE, FALSE, 0);\n"
                "  context.release = CreateEventA(0, TRUE, FALSE, 0);\n"
                "  context.file = INVALID_HANDLE_VALUE;\n"
                "  if (context.ready == 0 || context.release == 0) {\n"
                "    (void)cupidbuild_host_transaction_close(transaction);\n"
                "    return 19;\n"
                "  }\n"
                "  blocker_thread = CreateThread(0, 0, hold_candidate,\n"
                "      &context, 0, 0);\n"
                "  if (blocker_thread == 0) {\n"
                "    (void)cupidbuild_host_transaction_close(transaction);\n"
                "    return 20;\n"
                "  }\n"
                "  result = cupidbuild_host_run_in_private(\n"
                "      transaction, frozen, arguments, 10000u);\n"
                "  if (WaitForSingleObject(context.ready, 0u) != WAIT_OBJECT_0)\n"
                "    result = -2;\n"
                "  (void)SetEvent(context.release);\n"
                "  if (WaitForSingleObject(blocker_thread, 5000u) != WAIT_OBJECT_0)\n"
                "    return 21;\n"
                "  (void)CloseHandle(blocker_thread);\n"
                "  (void)CloseHandle(context.ready);\n"
                "  (void)CloseHandle(context.release);\n"
                "  cleaned = cupidbuild_host_transaction_close(transaction);\n"
                "  if (result != -1) return 13;\n"
                "  if (cleaned == 0) return 14;\n"
                "  if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES)\n"
                "    return 15;\n"
                "  missing_error = GetLastError();\n"
                "  return missing_error == ERROR_FILE_NOT_FOUND ||\n"
                "         missing_error == ERROR_PATH_NOT_FOUND ? 0 : 16;\n"
                "}\n",
                encoding="utf-8",
                newline="\n",
            )

            writer_built = subprocess.run(
                [
                    *compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-x",
                    "c",
                    str(writer_source),
                    "-o",
                    str(writer),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=90,
            )
            self.assertEqual(
                writer_built.returncode,
                0,
                writer_built.stdout + writer_built.stderr,
            )
            built = subprocess.run(
                [
                    *compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-x",
                    "c",
                    "-I",
                    str(TOOLCHAIN_ROOT),
                    str(driver_source),
                    str(TOOLCHAIN_ROOT / "cupidbuild_host.cc"),
                    "-o",
                    str(driver),
                    "-lntdll",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=180,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)

            launched = subprocess.run(
                [str(driver), str(root), str(writer)],
                cwd=root,
                text=True,
                capture_output=True,
                timeout=30,
            )

            self.assertEqual(
                launched.returncode,
                0,
                launched.stdout + launched.stderr,
            )

    def test_matching_regular_file_is_read_through_discovery_handle(self):
        compiler = _host_compiler()
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-discovery-read-", dir=TOOLCHAIN_ROOT
        ) as temporary:
            build_root = Path(temporary)
            root = build_root / "repository"
            root.mkdir()
            drivers = root / "drivers"
            drivers.mkdir()
            header = drivers / "keep.h"
            header.write_text("#define KEEP 1\n", encoding="utf-8")
            (root / "seed.txt").write_text("seed\n", encoding="utf-8")
            driver_source = build_root / "discovery_read_driver.cc"
            driver = build_root / "discovery_read_driver.exe"

            driver_source.write_text(
                "#include <stdio.h>\n"
                "#include <string.h>\n"
                '#include "cupidbuild_host.h"\n'
                "int main(int argc, char **argv) {\n"
                "  cupidbuild_host_transaction_t *transaction = 0;\n"
                "  cupidbuild_host_path_list_t paths;\n"
                '  const char *roots[] = {"drivers"};\n'
                '  const char *suffixes[] = {".h"};\n'
                "  int discovered;\n"
                "  int cleaned;\n"
                "  if (argc != 2) return 10;\n"
                '  if (!cupidbuild_host_transaction_open(argv[1], "seed.txt",\n'
                '          "out.bin", &transaction)) {\n'
                '    (void)fprintf(stderr, "open: %s\\n",\n'
                "                  cupidbuild_host_error(transaction));\n"
                "    (void)cupidbuild_host_transaction_close(transaction);\n"
                "    return 11;\n"
                "  }\n"
                "  discovered = cupidbuild_host_discover_files(\n"
                "      transaction, roots, 1u, suffixes, 1u, 0, 1, &paths);\n"
                "  if (!discovered) {\n"
                '    (void)fprintf(stderr, "discover: %s\\n",\n'
                "                  cupidbuild_host_error(transaction));\n"
                "    (void)cupidbuild_host_transaction_close(transaction);\n"
                "    return 12;\n"
                "  }\n"
                "  if (paths.count != 1u ||\n"
                '      strcmp(paths.paths[0], "drivers/keep.h") != 0 ||\n'
                "      paths.snapshots[0].present == 0) {\n"
                "    cupidbuild_host_path_list_close(&paths);\n"
                "    (void)cupidbuild_host_transaction_close(transaction);\n"
                "    return 13;\n"
                "  }\n"
                "  cupidbuild_host_path_list_close(&paths);\n"
                "  cleaned = cupidbuild_host_transaction_close(transaction);\n"
                "  return cleaned ? 0 : 14;\n"
                "}\n",
                encoding="utf-8",
                newline="\n",
            )

            built = subprocess.run(
                [
                    *compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-x",
                    "c",
                    "-I",
                    str(TOOLCHAIN_ROOT),
                    str(driver_source),
                    str(TOOLCHAIN_ROOT / "cupidbuild_host.cc"),
                    "-o",
                    str(driver),
                    "-lntdll",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=180,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)

            launched = subprocess.run(
                [str(driver), str(root)],
                cwd=root,
                text=True,
                capture_output=True,
                timeout=30,
            )

            self.assertEqual(
                launched.returncode,
                0,
                launched.stdout + launched.stderr,
            )

    def test_directory_change_between_named_and_handle_samples_is_rejected(self):
        compiler = _host_compiler()
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-directory-record-", dir=TOOLCHAIN_ROOT
        ) as temporary:
            build_root = Path(temporary)
            root = build_root / "repository"
            root.mkdir()
            drivers = root / "drivers"
            drivers.mkdir()
            (root / "seed.txt").write_text("seed\n", encoding="utf-8")
            driver_source = build_root / "directory_record_driver.cc"
            driver = build_root / "directory_record_driver.exe"
            ready = root / "directory-query-ready"
            resume = root / "directory-query-resume"
            original_times = drivers.stat()
            os.utime(
                drivers,
                ns=(
                    original_times.st_atime_ns,
                    original_times.st_mtime_ns
                    - original_times.st_mtime_ns % 1_000_000_000,
                ),
            )
            original_times = drivers.stat()
            changed = threading.Event()
            mutation_errors = []

            driver_source.write_text(
                "#include <stdio.h>\n"
                '#include "cupidbuild_host.h"\n'
                "int main(int argc, char **argv) {\n"
                "  cupidbuild_host_transaction_t *transaction = 0;\n"
                "  cupidbuild_host_path_list_t paths;\n"
                '  const char *roots[] = {"drivers"};\n'
                '  const char *suffixes[] = {".h"};\n'
                "  int discovered;\n"
                "  int cleaned;\n"
                "  if (argc != 2) return 10;\n"
                '  if (!cupidbuild_host_transaction_open(argv[1], "seed.txt",\n'
                '          "out.bin", &transaction)) {\n'
                '    (void)fprintf(stderr, "open: %s\\n",\n'
                "                  cupidbuild_host_error(transaction));\n"
                "    (void)cupidbuild_host_transaction_close(transaction);\n"
                "    return 11;\n"
                "  }\n"
                "  discovered = cupidbuild_host_discover_files(\n"
                "      transaction, roots, 1u, suffixes, 1u, 0, 1, &paths);\n"
                "  cupidbuild_host_path_list_close(&paths);\n"
                "  cleaned = cupidbuild_host_transaction_close(transaction);\n"
                "  if (discovered) return 12;\n"
                "  return cleaned ? 0 : 13;\n"
                "}\n",
                encoding="utf-8",
                newline="\n",
            )

            built = subprocess.run(
                [
                    *compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-DCUPIDBUILD_PROFILE_DIRECTORY_RACE_TEST",
                    "-x",
                    "c",
                    "-I",
                    str(TOOLCHAIN_ROOT),
                    str(driver_source),
                    str(TOOLCHAIN_ROOT / "cupidbuild_host.cc"),
                    "-o",
                    str(driver),
                    "-lntdll",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=180,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)

            def change_directory_after_the_named_query():
                try:
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline:
                        if ready.is_file():
                            transient = drivers / "late-empty-directory"
                            transient.mkdir()
                            transient.rmdir()
                            os.utime(
                                drivers,
                                ns=(
                                    original_times.st_atime_ns,
                                    original_times.st_mtime_ns,
                                ),
                            )
                            changed.set()
                            return
                        time.sleep(0.001)
                except Exception as error:
                    mutation_errors.append(error)
                finally:
                    try:
                        resume.write_bytes(b"continue")
                    except Exception as error:
                        mutation_errors.append(error)

            environment = os.environ.copy()
            environment["CUPIDBUILD_PROFILE_TEST_DIRECTORY_QUERY_LOGICAL"] = (
                "drivers"
            )
            environment["CUPIDBUILD_PROFILE_TEST_DIRECTORY_QUERY_READY"] = str(
                ready
            )
            environment["CUPIDBUILD_PROFILE_TEST_DIRECTORY_QUERY_RESUME"] = str(
                resume
            )
            mutator = threading.Thread(
                target=change_directory_after_the_named_query,
                daemon=True,
            )
            mutator.start()
            launched = subprocess.run(
                [str(driver), str(root)],
                cwd=root,
                env=environment,
                text=True,
                capture_output=True,
                timeout=45,
            )
            mutator.join(timeout=35)

            self.assertFalse(mutator.is_alive(), "the directory mutator did not stop")
            self.assertFalse(mutation_errors, repr(mutation_errors))
            self.assertTrue(
                changed.is_set(),
                "the named query was not observed\n"
                + launched.stdout
                + launched.stderr
                + f"driver return code: {launched.returncode}",
            )
            self.assertEqual(
                launched.returncode,
                0,
                launched.stdout + launched.stderr,
            )

    def test_checked_child_cannot_use_an_unlisted_inheritable_handle(self):
        compiler = _host_compiler()
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-handle-list-", dir=TOOLCHAIN_ROOT
        ) as temporary:
            root = Path(temporary)
            child_source = root / "handle_probe.cc"
            child = root / "handle_probe.exe"
            driver_source = root / "runner_driver.cc"
            driver = root / "runner_driver.exe"
            sentinel = root / "sentinel.bin"

            child_source.write_text(
                "#define WIN32_LEAN_AND_MEAN\n"
                "#include <windows.h>\n"
                "#include <stdint.h>\n"
                "#include <stdlib.h>\n"
                "int main(int argc, char **argv) {\n"
                "  char *end = 0;\n"
                "  unsigned long long raw;\n"
                "  DWORD written = 0;\n"
                '  static const char marker[] = "inherited\\n";\n'
                '  static const char output[] = "allowlisted stdout\\n";\n'
                '  static const char error[] = "allowlisted stderr\\n";\n'
                "  if (argc != 2) return 2;\n"
                "  raw = _strtoui64(argv[1], &end, 10);\n"
                "  if (end == argv[1] || *end != '\\0') return 3;\n"
                "  if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), output,\n"
                "          (DWORD)(sizeof(output) - 1u), &written, 0)) return 4;\n"
                "  if (!WriteFile(GetStdHandle(STD_ERROR_HANDLE), error,\n"
                "          (DWORD)(sizeof(error) - 1u), &written, 0)) return 5;\n"
                "  (void)WriteFile((HANDLE)(uintptr_t)raw, marker,\n"
                "                  (DWORD)(sizeof(marker) - 1u), &written, 0);\n"
                "  return 0;\n"
                "}\n",
                encoding="utf-8",
                newline="\n",
            )
            driver_source.write_text(
                "#define WIN32_LEAN_AND_MEAN\n"
                "#include <windows.h>\n"
                "#include <stdint.h>\n"
                "#include <stdio.h>\n"
                '#include "cupidbuild_host.h"\n'
                "int main(int argc, char **argv) {\n"
                "  cupidbuild_host_transaction_t *transaction = 0;\n"
                "  cupidbuild_host_snapshot_t snapshot;\n"
                "  const char *frozen = 0;\n"
                "  SECURITY_ATTRIBUTES security;\n"
                "  HANDLE sentinel;\n"
                "  HANDLE check;\n"
                "  DWORD size;\n"
                "  char handle_text[32];\n"
                "  const char *arguments[2];\n"
                "  int result;\n"
                "  int cleaned;\n"
                "  if (argc != 4) return 10;\n"
                "  if (!cupidbuild_host_runner_open(argv[1], &transaction))\n"
                "    return 11;\n"
                "  if (!cupidbuild_host_freeze_input(transaction, argv[2],\n"
                '          "handle-probe.exe", &frozen, &snapshot) ||\n'
                "      !cupidbuild_host_make_input_executable(transaction, frozen)) {\n"
                "    (void)fprintf(stderr, \"freeze: %s\\n\",\n"
                "                  cupidbuild_host_error(transaction));\n"
                "    (void)cupidbuild_host_transaction_close(transaction);\n"
                "    return 12;\n"
                "  }\n"
                "  security.nLength = sizeof(security);\n"
                "  security.lpSecurityDescriptor = 0;\n"
                "  security.bInheritHandle = TRUE;\n"
                "  sentinel = CreateFileA(argv[3], GENERIC_WRITE, FILE_SHARE_READ,\n"
                "      &security, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, 0);\n"
                "  if (sentinel == INVALID_HANDLE_VALUE) {\n"
                "    (void)cupidbuild_host_transaction_close(transaction);\n"
                "    return 13;\n"
                "  }\n"
                '  if (snprintf(handle_text, sizeof(handle_text), "%llu",\n'
                "          (unsigned long long)(uintptr_t)sentinel) < 0) {\n"
                "    (void)CloseHandle(sentinel);\n"
                "    (void)cupidbuild_host_transaction_close(transaction);\n"
                "    return 14;\n"
                "  }\n"
                "  arguments[0] = handle_text;\n"
                "  arguments[1] = 0;\n"
                "  if (!cupidbuild_host_require_frozen_inputs(transaction)) {\n"
                "    (void)fprintf(stderr, \"frozen: %s\\n\",\n"
                "                  cupidbuild_host_error(transaction));\n"
                "    (void)CloseHandle(sentinel);\n"
                "    (void)cupidbuild_host_transaction_close(transaction);\n"
                "    return 19;\n"
                "  }\n"
                "  result = cupidbuild_host_run_captured(\n"
                "      transaction, frozen, arguments, 10000u);\n"
                "  (void)CloseHandle(sentinel);\n"
                "  if (result != 0) (void)fprintf(stderr,\n"
                "      \"run result %d: %s\\n\", result,\n"
                "      cupidbuild_host_error(transaction));\n"
                "  if (result == 0 &&\n"
                "      !cupidbuild_host_forward_captured(transaction)) result = -1;\n"
                "  cleaned = cupidbuild_host_transaction_close(transaction);\n"
                "  if (result != 0) return 15;\n"
                "  if (!cleaned) return 16;\n"
                "  check = CreateFileA(argv[3], GENERIC_READ, FILE_SHARE_READ, 0,\n"
                "      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);\n"
                "  if (check == INVALID_HANDLE_VALUE) return 17;\n"
                "  size = GetFileSize(check, 0);\n"
                "  (void)CloseHandle(check);\n"
                "  if (size == INVALID_FILE_SIZE || size != 0u) return 18;\n"
                "  return 0;\n"
                "}\n",
                encoding="utf-8",
                newline="\n",
            )

            child_build = subprocess.run(
                [
                    *compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-x",
                    "c",
                    str(child_source),
                    "-o",
                    str(child),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=90,
            )
            self.assertEqual(
                child_build.returncode,
                0,
                child_build.stdout + child_build.stderr,
            )
            driver_build = subprocess.run(
                [
                    *compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-x",
                    "c",
                    "-I",
                    str(TOOLCHAIN_ROOT),
                    str(driver_source),
                    str(TOOLCHAIN_ROOT / "cupidbuild_host.cc"),
                    "-o",
                    str(driver),
                    "-lntdll",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=180,
            )
            self.assertEqual(
                driver_build.returncode,
                0,
                driver_build.stdout + driver_build.stderr,
            )

            launched = subprocess.run(
                [str(driver), str(root), str(child), str(sentinel)],
                cwd=root,
                text=True,
                capture_output=True,
                timeout=30,
            )
            sentinel_bytes = (
                sentinel.read_bytes() if sentinel.exists() else b"<missing>"
            )

            self.assertEqual(
                launched.returncode,
                0,
                launched.stdout
                + launched.stderr
                + f"sentinel bytes: {sentinel_bytes!r}",
            )
            self.assertEqual(launched.stdout, "allowlisted stdout\n")
            self.assertEqual(launched.stderr, "allowlisted stderr\n")


if __name__ == "__main__":
    unittest.main()
