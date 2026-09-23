"""Real-filesystem contract for retained, read-only native observations."""
import os
import json
from pathlib import Path
import queue
import shutil
import subprocess
import tempfile
import threading
import unittest


ROOT = Path(__file__).resolve().parents[1]
CALLER = r'''
#include "cupidbuild_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int decode(char *text) {
  size_t size = strlen(text);
  size_t index;
  if (size % 2) return 0;
  for (index = 0; index < size; index += 2) {
    unsigned int high = (unsigned char)text[index];
    unsigned int low = (unsigned char)text[index + 1];
    unsigned int value;
    high = high >= '0' && high <= '9' ? high - '0' : high - 'a' + 10;
    low = low >= '0' && low <= '9' ? low - '0' : low - 'a' + 10;
    if (high > 15 || low > 15) return 0;
    value = high * 16 + low;
    if (value == 0) return 0;
    text[index / 2] = (char)value;
  }
  text[size / 2] = 0;
  return 1;
}
int main(int argc, char **argv) {
  cupidbuild_host_observer_t *observer = NULL;
  unsigned char *bytes = NULL;
  uint64_t size = 0;
  const char *members[] = {"tool.elf", "manifest.json"};
  int result;
  int valid;
  char resume;
  if (argc != 4 || !decode(argv[1]) || !decode(argv[3])) return 90;
  if (strcmp(argv[2], "strcpy") == 0) {
    char destination[8];
    memset(destination, 'x', sizeof(destination));
    if (strcpy(destination, "\xce\xbb") != destination ||
        memcmp(destination, "\xce\xbb\0x", 4) != 0) return 103;
    if (strcpy(destination, "") != destination || destination[0] != 0 ||
        (unsigned char)destination[1] != 0xbb) return 104;
    return 0;
  }
  if (strcmp(argv[2], "stdin") == 0) {
    unsigned char input[8];
    size_t count = fread(input, 1, sizeof(input), stdin);
    if (ferror(stdin) || count != 4 || memcmp(input, "seed", 4) != 0) return 95;
    if (fread(input, 1, 1, stdin) != 0 || ferror(stdin)) return 96;
    return 0;
  }
  if (strcmp(argv[2], "stdin-invalid") == 0) {
    if (fread(NULL, 1, 1, stdin) != 0 || !ferror(stdin)) return 97;
    return 0;
  }
  if (strcmp(argv[2], "cleanup") == 0) {
    unsigned int iteration;
    for (iteration = 0; iteration < 1200; iteration++) {
      if (!cupidbuild_host_observer_open(argv[1], &observer)) return 98;
      bytes = (unsigned char *)1;
      size = 123;
      if (cupidbuild_host_observer_file(observer, "missing", 4, &bytes, &size) ||
          bytes != NULL || size != 0 ||
          cupidbuild_host_observer_require_unchanged(observer)) return 99;
      if (cupidbuild_host_observer_file(observer, argv[3], 4, NULL, &size) || size != 0)
        return 100;
      if (!cupidbuild_host_observer_close(observer)) return 101;
    }
    return cupidbuild_host_observer_close(NULL) ? 0 : 102;
  }
  valid = cupidbuild_host_observer_open(argv[1], &observer);
  if (valid && strcmp(argv[2], "unicode-directory") == 0) {
    members[0] = "\xce\xbb-\xf0\x9f\x90\xb1";
    valid = cupidbuild_host_observer_directory(observer, argv[3], members, 2);
  } else if (valid && strcmp(argv[2], "duplicate-directory") == 0) {
    members[1] = members[0];
    valid = cupidbuild_host_observer_directory(observer, argv[3], members, 2);
  } else if (valid && strcmp(argv[2], "unsafe-directory") == 0) {
    members[0] = "../tool.elf";
    valid = cupidbuild_host_observer_directory(observer, argv[3], members, 2);
  } else if (valid && strcmp(argv[2], "empty-directory") == 0) {
    valid = cupidbuild_host_observer_directory(observer, argv[3], NULL, 0);
  } else if (valid && strcmp(argv[2], "member-limit") == 0) {
    valid = cupidbuild_host_observer_directory(observer, argv[3], members, 4097);
  } else if (valid && strcmp(argv[2], "directory") == 0) {
    valid = cupidbuild_host_observer_directory(observer, argv[3], members, 2);
  } else if (valid) {
    valid = cupidbuild_host_observer_file(observer, argv[3], 4,
                strcmp(argv[2], "payload") == 0 ? &bytes : NULL, &size);
    if (valid && bytes != NULL && (size != 4 || memcmp(bytes, "seed", 4) != 0))
      valid = 0;
  }
  free(bytes);
  if (!valid) {
    result = cupidbuild_host_observer_require_unchanged(observer) ? 91 : 2;
    fprintf(stderr, "%s\n", cupidbuild_host_observer_error(observer));
    cupidbuild_host_observer_close(observer);
    return result;
  }
  if (!cupidbuild_host_observer_require_unchanged(observer)) return 94;
  printf("ready %u %u\n", (unsigned int)(size >> 32), (unsigned int)size);
  fflush(stdout);
  if (fread(&resume, 1, 1, stdin) != 1) {
    cupidbuild_host_observer_close(observer);
    return 92;
  }
  result = cupidbuild_host_observer_require_unchanged(observer) ? 0 : 3;
  if (result) fprintf(stderr, "%s\n", cupidbuild_host_observer_error(observer));
  if (!cupidbuild_host_observer_close(observer)) return 93;
  return result;
}
'''


class CupidBuildObserverTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        configured = os.environ.get("CUPIDBUILD_OBSERVER_PROGRAM")
        if configured:
            cls.program = Path(configured).resolve(strict=True)
            return
        cls.build = tempfile.TemporaryDirectory(prefix="cupid-observer-build-")
        cls.addClassCleanup(cls.build.cleanup)
        directory = Path(cls.build.name)
        caller = directory / "caller.cc"
        caller.write_text(CALLER, encoding="ascii")
        cls.program = directory / ("observer.exe" if os.name == "nt" else "observer")
        if os.environ.get("CUPIDBUILD_OBSERVER_CHECKED") == "1":
            cls.build_checked(directory)
            return
        compiler = shutil.which("clang")
        if not compiler:
            raise RuntimeError("Clang is required for the native observer contract")
        command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                   "-D_CRT_SECURE_NO_WARNINGS", "-I", str(ROOT / "toolchain"),
                   "-x", "c", str(caller), str(ROOT / "toolchain/cupidbuild_host.cc"),
                   *(["-lntdll"] if os.name == "nt" else []), "-o", str(cls.program)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=180)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    @classmethod
    def build_checked(cls, directory):
        from tools import artifact_size_contract as contract
        from tools import bootstrap_toolchain as seed

        source = directory / "source"
        paths = [ROOT / "toolchain/cupidbuild_host.cc", ROOT / "toolchain/cupidbuild_host.h"]
        paths.extend(path for path in (ROOT / "toolchain/hosted").rglob("*") if path.is_file())
        for path in paths:
            destination = source / path.relative_to(ROOT)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, destination)
        (source / "toolchain/observer_caller.cc").write_text(CALLER, encoding="ascii")
        host = "i386-windows" if os.name == "nt" else "i386-linux"
        checked = seed.verify_seed_inputs(ROOT / "bootstrap/seeds" / host / "manifest.json")
        cls.addClassCleanup(seed.require_live_seed_inputs, checked)
        plan = json.loads((ROOT / "bootstrap/seeds/i386-linux/manifest.json").read_text())["build_plan"]
        if os.name == "nt":
            plan = seed._windows_build_plan(plan)
        rows = [row for row in plan["sources"]
                if row["name"] in {"runtime", "publication_runtime", "cupidbuild_host"}]
        rows.insert(0, {"name": "observer_caller", "path": "/toolchain/observer_caller.cc",
                        "definitions": [], "gnu_extensions": False})
        runner = seed.ToolRunner(source)
        objects = {}
        for row in rows:
            obj = source / (row["name"] + ".o")
            contract._compile_source(checked, runner, source, row["path"].lstrip("/"), obj,
                                     row.get("definitions", []), row["gnu_extensions"], 600)
            objects[row["name"]] = obj
        assembly = plan.get("assembly_sources", [
            {"name": "start", "path": "/toolchain/hosted/i386-linux/start.asm"}])
        for row in assembly:
            obj = source / (row["name"] + ".o")
            contract._run_checked_tool(checked, runner, "cupidasm", ["-f", "elf32",
                source / row["path"].lstrip("/"), "-o", obj], row["name"], 180)
            objects[row["name"]] = obj
        order = ["start", *[row["name"] for row in rows],
                 *[row["name"] for row in assembly if row["name"] != "start"]]
        if os.name == "nt":
            arguments = seed._windows_link_arguments("cupidbuild", cls.program, objects, order)
            contract._run_checked_tool(checked, runner, "cupidld", arguments, "observer", 180)
        else:
            contract._link_contract(checked, runner, [objects[name] for name in order],
                                    cls.program, False, 180)
            cls.program.chmod(0o700)
        seed.require_live_seed_inputs(checked)

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cupid-observer-case-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.parent = self.root / "nested"
        self.parent.mkdir()
        self.leaf = self.parent / "artifact.dat"
        self.leaf.write_bytes(b"seed")

    def observe(self, mode="metadata", logical="nested/artifact.dat", mutate=None,
                expected_size=4, rejected=False, unchanged=False):
        before = sorted(p.relative_to(self.root).as_posix() for p in self.root.rglob("*"))
        # ASCII transport avoids the native Windows CRT's locale-dependent argv.
        process = subprocess.Popen([str(self.program), str(self.root).encode("utf-8").hex(),
                                    mode, (logical if isinstance(logical, bytes)
                                           else logical.encode("utf-8")).hex()],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True, encoding="utf-8")
        try:
            if rejected:
                stdout, stderr = process.communicate("x", timeout=10)
                self.assertEqual(process.returncode, 2, (stdout, stderr))
                self.assertTrue(stderr.strip())
                return
            # communicate is used for stable cases; mutations need the ready barrier.
            if mutate is None:
                stdout, stderr = process.communicate("x", timeout=10)
                self.assertEqual(process.returncode, 0, (stdout, stderr))
                self.assertEqual(stdout, f"ready {expected_size >> 32} {expected_size & 0xffffffff}\n")
                self.assertEqual(before, sorted(p.relative_to(self.root).as_posix()
                                               for p in self.root.rglob("*")))
            else:
                ready = queue.Queue()
                reader = threading.Thread(target=lambda: ready.put(process.stdout.readline()),
                                          daemon=True)
                reader.start()
                self.assertEqual(ready.get(timeout=10),
                                 f"ready {expected_size >> 32} {expected_size & 0xffffffff}\n")
                reader.join(timeout=1)
                mutate()
                stdout, stderr = process.communicate("x", timeout=10)
                self.assertEqual(process.returncode, 0 if unchanged else 3, (stdout, stderr))
                if not unchanged:
                    self.assertTrue(stderr.strip())
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate(timeout=10)
            for stream in (process.stdin, process.stdout, process.stderr):
                stream.close()

    def test_metadata_observation_creates_no_files(self):
        self.observe()

    def test_standard_input_pipe_and_eof(self):
        result = subprocess.run([str(self.program), "2f", "stdin", ""],
                                input=b"seed", capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_string_copy_terminator_and_return_value(self):
        result = subprocess.run([str(self.program), "2f", "strcpy", ""],
                                capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_standard_input_invalid_destination(self):
        if not (os.environ.get("CUPIDBUILD_OBSERVER_PROGRAM") or
                os.environ.get("CUPIDBUILD_OBSERVER_CHECKED") == "1"):
            self.skipTest("invalid destination is a Cupid runtime contract")
        result = subprocess.run([str(self.program), "2f", "stdin-invalid", ""],
                                input=b"seed", capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_failed_observation_cleanup_and_poisoning(self):
        result = subprocess.run([str(self.program), str(self.root).encode().hex(),
                                 "cleanup", "nested/artifact.dat".encode().hex()],
                                capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_directory_member_limit(self):
        self.observe("member-limit", "nested", rejected=True)

    def test_large_metadata_keeps_high_size_word(self):
        size = (1 << 32) + 37
        with self.leaf.open("r+b") as stream:
            if os.name == "nt":
                import ctypes
                from ctypes import wintypes
                import msvcrt
                ioctl = ctypes.WinDLL("kernel32", use_last_error=True).DeviceIoControl
                ioctl.argtypes = [wintypes.HANDLE, wintypes.DWORD, ctypes.c_void_p,
                                  wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD,
                                  ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
                ioctl.restype = wintypes.BOOL
                count = wintypes.DWORD()
                self.assertTrue(ioctl(msvcrt.get_osfhandle(stream.fileno()), 0x900c4,
                                      None, 0, None, 0, ctypes.byref(count), None),
                                ctypes.get_last_error())
            stream.truncate(size)
        self.observe(expected_size=size)
        self.observe("payload", rejected=True)

    def test_bounded_payload(self):
        self.observe("payload")

    def test_payload_limit_rejects_growth(self):
        self.leaf.write_bytes(b"seeds")
        self.observe("payload", rejected=True)

    def test_empty_regular_file(self):
        self.leaf.write_bytes(b"")
        self.observe(expected_size=0)

    def test_missing_file(self):
        self.observe(logical="nested/missing", rejected=True)

    def test_directory_is_not_a_file(self):
        self.observe(logical="nested", rejected=True)

    def test_unsafe_paths(self):
        for path in ("../artifact.dat", "/nested/artifact.dat", "nested/../nested/artifact.dat",
                     "nested//artifact.dat", "./nested/artifact.dat", "nested/./artifact.dat"):
            with self.subTest(path=path):
                self.observe(logical=path, rejected=True)

    def test_size_change(self):
        self.observe(mutate=lambda: self.leaf.write_bytes(b"longer"))

    def test_payload_edit_with_restored_mtime(self):
        original = self.leaf.stat()
        def mutate():
            self.leaf.write_bytes(b"edit")
            os.utime(self.leaf, ns=(original.st_atime_ns, original.st_mtime_ns))
        self.observe("payload", mutate=mutate)

    def test_unicode_parent(self):
        directory = self.root / "seed-\u03bb-\U0001f431"
        directory.mkdir()
        (directory / "artifact.dat").write_bytes(b"seed")
        self.observe(logical=directory.name + "/artifact.dat")

    def seed_directory(self):
        directory = self.root / "seed"
        directory.mkdir()
        (directory / "manifest.json").write_text("{}")
        (directory / "tool.elf").write_bytes(b"seed")
        return directory

    def test_exact_directory_membership(self):
        self.seed_directory()
        self.observe("directory", "seed", expected_size=0)

    def test_extra_directory_member(self):
        directory = self.seed_directory()
        (directory / "extra").write_bytes(b"")
        self.observe("directory", "seed", rejected=True)

    def test_directory_member_added_after_capture(self):
        directory = self.seed_directory()
        self.observe("directory", "seed", expected_size=0,
                     mutate=lambda: (directory / "extra").write_bytes(b""))

    def test_empty_directory(self):
        (self.root / "empty").mkdir()
        self.observe("empty-directory", "empty", expected_size=0)

    def test_root_directory_membership(self):
        self.leaf.unlink()
        self.parent.rmdir()
        (self.root / "tool.elf").write_bytes(b"seed")
        (self.root / "manifest.json").write_bytes(b"{}")
        self.observe("directory", "", expected_size=0)

    def test_unicode_directory_membership(self):
        directory = self.seed_directory()
        (directory / "tool.elf").rename(directory / "\u03bb-\U0001f431")
        self.observe("unicode-directory", "seed", expected_size=0)

    def test_duplicate_and_unsafe_membership_requests(self):
        self.seed_directory()
        for mode in ("duplicate-directory", "unsafe-directory"):
            with self.subTest(mode=mode):
                self.observe(mode, "seed", rejected=True)

    def test_malformed_utf8_paths(self):
        for name in (b"\xc0\xaf", b"\xed\xa0\x80", b"\xf4\x90\x80\x80",
                     b"\xe2\x82", b"\xff", b"\x80"):
            with self.subTest(name=name):
                self.observe(logical=b"nested/" + name, rejected=True)

    def test_unicode_repository_root(self):
        previous = self.root
        self.root = self.root / "root-\u03bb-\U0001f431"
        self.root.mkdir()
        self.parent.rename(self.root / "nested")
        self.observe()
        self.root = previous

    def test_leaf_replacement(self):
        replacement = self.parent / "replacement"
        replacement.write_bytes(b"seed")
        original = self.leaf.stat()
        os.utime(replacement, ns=(original.st_atime_ns, original.st_mtime_ns))
        def mutate():
            if os.name == "nt":
                with self.assertRaises(PermissionError):
                    os.replace(replacement, self.leaf)
            else:
                os.replace(replacement, self.leaf)
        self.observe(mutate=mutate, unchanged=os.name == "nt")

    def test_parent_replacement(self):
        def mutate():
            if os.name == "nt":
                with self.assertRaises(PermissionError):
                    self.parent.rename(self.root / "displaced")
            else:
                self.parent.rename(self.root / "displaced")
                self.parent.mkdir()
                self.leaf.write_bytes(b"seed")
        self.observe(mutate=mutate, unchanged=os.name == "nt")

    def test_repository_replacement(self):
        self.root = self.root / "repository"
        self.root.mkdir()
        self.parent.rename(self.root / "nested")
        def mutate():
            displaced = self.root.with_name("displaced")
            if os.name == "nt":
                with self.assertRaises(PermissionError):
                    self.root.rename(displaced)
            else:
                self.root.rename(displaced)
                self.root.mkdir()
        self.observe(mutate=mutate, unchanged=os.name == "nt")

    @unittest.skipIf(os.name == "nt", "POSIX permits retained-file rename")
    def test_mutation_of_retained_original(self):
        def mutate():
            displaced = self.parent / "displaced"
            self.leaf.rename(displaced)
            self.leaf.write_bytes(b"seed")
            displaced.write_bytes(b"longer")
        self.observe("payload", mutate=mutate)

    def test_membership_does_not_imply_file_kind(self):
        directory = self.seed_directory()
        (directory / "tool.elf").unlink()
        (directory / "tool.elf").mkdir()
        self.observe("directory", "seed", expected_size=0)

    def test_unobserved_membership_can_change(self):
        self.observe(mutate=lambda: (self.parent / "unrelated").write_bytes(b""),
                     unchanged=True)

    def test_repository_root_metadata_drift_is_rejected(self):
        self.observe(mutate=lambda: (self.root / "unrelated").write_bytes(b""))

    def test_metadata_only_does_not_promise_payload_identity(self):
        original = self.leaf.stat()
        def mutate():
            self.leaf.write_bytes(b"edit")
            os.utime(self.leaf, ns=(original.st_atime_ns, original.st_mtime_ns))
        self.observe(mutate=mutate, unchanged=True)

    @unittest.skipUnless(os.name == "nt", "Windows directory junction")
    def test_windows_junction_rejected(self):
        alias = self.root / "alias"
        result = subprocess.run(["cmd", "/c", "mklink", "/J", str(alias), str(self.parent)],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        try:
            self.observe(logical="alias/artifact.dat", rejected=True)
            self.observe("empty-directory", "alias", rejected=True)
            previous = self.root
            self.root = alias
            try:
                self.observe(logical="artifact.dat", rejected=True)
            finally:
                self.root = previous
        finally:
            alias.rmdir()

    @unittest.skipIf(os.name == "nt", "POSIX FIFO")
    def test_fifo_rejected_without_blocking(self):
        os.mkfifo(self.parent / "pipe")
        self.observe(logical="nested/pipe", rejected=True)

    @unittest.skipIf(os.name == "nt", "POSIX symbolic link")
    def test_leaf_and_parent_symlinks(self):
        (self.parent / "alias").symlink_to(self.leaf)
        self.observe(logical="nested/alias", rejected=True)
        (self.root / "alias").symlink_to(self.parent, target_is_directory=True)
        self.observe(logical="alias/artifact.dat", rejected=True)


if __name__ == "__main__":
    unittest.main()
