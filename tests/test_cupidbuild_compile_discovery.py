import os
import shutil
import subprocess
import tempfile
import time
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HARNESS = r"""
#include "cupidbuild_host.h"
#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif
int main(int argc, char **argv) {
  cupidbuild_host_transaction_t *transaction = 0;
  cupidbuild_host_path_list_t paths;
  const char *roots[] = {"src"};
  const char *suffixes[] = {".cc", ".c", ".h", ".inc"};
  FILE *signal;
  unsigned int attempt;
  int valid;
  if (argc != 3 || !cupidbuild_host_transaction_open(
          argv[1], "src/input.cc", "src/output.o", &transaction)) return 10;
  if (strcmp(argv[2], "compile") == 0 &&
      !cupidbuild_host_begin_compile_discovery(transaction)) return 11;
  if (!cupidbuild_host_discover_files(transaction, roots, 1, suffixes, 4,
                                      0, 1, &paths)) return 12;
  cupidbuild_host_path_list_close(&paths);
  if (!(strcmp(argv[2], "compile") == 0
            ? cupidbuild_host_seal_compile_discovery(transaction)
            : cupidbuild_host_seal_discovery(transaction))) return 13;
  signal = fopen("ready", "wb");
  if (!signal || fclose(signal)) return 14;
  for (attempt = 0; attempt < 30000; attempt++) {
    signal = fopen("resume", "rb");
    if (signal) { fclose(signal); break; }
#if defined(_WIN32)
    Sleep(1);
#else
    usleep(1000);
#endif
  }
  valid = cupidbuild_host_require_publication_boundary(transaction);
  if (!valid) fprintf(stderr, "%s\n", cupidbuild_host_error(transaction));
  if (!cupidbuild_host_transaction_close(transaction)) return 15;
  return valid ? 0 : 1;
}
"""


class CompileDiscoveryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="cupidbuild-discovery-")
        directory = Path(cls.build.name)
        source = directory / "discovery.cc"
        source.write_text(HARNESS)
        cls.executable = directory / ("discovery.exe" if os.name == "nt" else "discovery")
        compiler = shutil.which("gcc") or shutil.which("clang")
        if not compiler:
            raise unittest.SkipTest("hosted C compiler unavailable")
        command = [compiler, "-x", "c", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                   "-I", str(ROOT / "toolchain"), str(source),
                   str(ROOT / "toolchain/cupidbuild_host.cc"), "-o", str(cls.executable)]
        if os.name == "nt":
            command += ["-lntdll"]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def check_mutation(self, mutation, accepted=False, mode="compile"):
        with tempfile.TemporaryDirectory(prefix="cupidbuild-discovery-input-") as temporary:
            root = Path(temporary)
            source = root / "src"
            source.mkdir()
            (source / "input.cc").write_text("int input;\n")
            (source / "input.h").write_text("extern int input;\n")
            (source / "nested").mkdir()
            process = subprocess.Popen([str(self.executable), str(root), mode], cwd=root,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                deadline = time.monotonic() + 20
                while not (root / "ready").exists() and process.poll() is None:
                    if time.monotonic() >= deadline:
                        self.fail("discovery did not reach its publication boundary")
                    time.sleep(0.01)
                if process.poll() is not None:
                    self.fail(str(process.communicate()))
                mutation(source)
                (root / "resume").touch()
                stdout, stderr = process.communicate(timeout=30)
                self.assertEqual(process.returncode, 0 if accepted else 1, stdout + stderr)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.communicate()

    def test_compiler_accepts_unrelated_object_publication(self):
        self.check_mutation(lambda source: (source / "other.o").write_bytes(b"object"), True)

    def test_strict_profile_rejects_unrelated_object_publication(self):
        self.check_mutation(lambda source: (source / "other.o").write_bytes(b"object"), mode="strict")

    def test_compiler_accepts_private_files(self):
        self.check_mutation(lambda source: (source / ".private-candidate").write_bytes(b"private"), True)

    def test_compiler_rejects_added_headers(self):
        self.check_mutation(lambda source: (source / "added.h").write_text("int added;"))

    def test_compiler_rejects_removed_headers(self):
        self.check_mutation(lambda source: (source / "input.h").unlink())

    def test_compiler_rejects_changed_header_contents(self):
        self.check_mutation(lambda source: (source / "input.h").write_text("int changed;"))

    def test_compiler_rejects_changed_source_contents(self):
        self.check_mutation(lambda source: (source / "input.cc").write_text("int changed;"))

    def test_compiler_rejects_added_legacy_sources(self):
        self.check_mutation(lambda source: (source / "added.c").write_text("int added;"))

    def test_compiler_rejects_matching_directories(self):
        self.check_mutation(lambda source: (source / "added.h").mkdir())

    def test_compiler_rejects_replaced_directories(self):
        def replace(source):
            (source / "nested").rename(source / "old")
            (source / "nested").mkdir()
        self.check_mutation(replace)


if __name__ == "__main__":
    unittest.main()
