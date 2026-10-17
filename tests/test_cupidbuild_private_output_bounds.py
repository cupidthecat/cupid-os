"""Retained private inputs that include the complete ISO bundle metadata."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from . import test_cupidbuild_observer as observer
from . import test_iso_fixture_bundle as bundle
from . import test_cupidbuild_iso_image as image_checker


ROOT = Path(__file__).resolve().parents[1]
FILE_LIMIT = 64 * 1024 * 1024
PRIVATE_CAPACITY = FILE_LIMIT + 16 + 1025 * 512 + 1039 * 512
PRIVATE_CASE = r'''
static int private_read(const char *path) {
  unsigned char magic[8];
#if defined(NATIVE_OBSERVER_WINDOWS_WRITER)
  DWORD count = 0;
  HANDLE file = CreateFileA(path, GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  int ok = file != INVALID_HANDLE_VALUE && ReadFile(file, magic, 8u, &count, NULL) &&
           count == 8u && memcmp(magic, "CUPISO1\0", 8u) == 0;
  if (file != INVALID_HANDLE_VALUE && !CloseHandle(file)) ok = 0;
#else
  FILE *file = fopen(path, "rb");
  int ok = file && fread(magic, 1u, 8u, file) == 8u &&
           memcmp(magic, "CUPISO1\0", 8u) == 0;
  if (file && fclose(file) != 0) ok = 0;
#endif
  return ok ? 0 : 91;
}
static int private_case(const char *root, const char *mode, const char *path) {
  cupidbuild_host_transaction_t *transaction = NULL;
  cupidbuild_host_snapshot_t snapshot;
  cupidbuild_host_snapshot_t candidate;
  unsigned char *bytes = NULL;
  unsigned char *captured = NULL;
  size_t size = 4u;
  const unsigned char tiny[4] = {'k', 'e', 'e', 'p'};
  const char *frozen = NULL;
  const char *arguments[5];
  char writer[8192];
  int changed = 0;
  int ok = cupidbuild_host_transaction_open(root, "source.cc", "nested/file.o", &transaction);
  int large = strcmp(mode, "private-large") == 0 || strcmp(mode, "private-publish") == 0 ||
              strcmp(mode, "private-legacy") == 0 || strcmp(mode, "private-rewrite") == 0 ||
              strcmp(mode, "private-author") == 0;
  snprintf(writer, sizeof(writer), "%s/writer", root);
  if (ok) ok = cupidbuild_host_freeze_input(transaction, writer, "writer.exe", &frozen, NULL) &&
               cupidbuild_host_make_input_executable(transaction, frozen);
  if (large && ok) {
    FILE *file = fopen(path, "rb");
    long length = -1;
    ok = file && fseek(file, 0, SEEK_END) == 0 && (length = ftell(file)) > 67108864L &&
         (unsigned long)length <= 67108864u + 16u + 1025u * 512u + 1039u * 512u &&
         fseek(file, 0, SEEK_SET) == 0;
    size = ok ? (size_t)length : 0u;
    bytes = ok ? (unsigned char *)malloc(size) : NULL;
    if (ok) ok = bytes && fread(bytes, 1u, size, file) == size;
    if (file && fclose(file) != 0) ok = 0;
  } else if (ok) {
    bytes = (unsigned char *)malloc(size);
    if (bytes) memcpy(bytes, tiny, size); else ok = 0;
  }
  if (ok && strcmp(mode, "private-legacy") == 0) {
    ok = !cupidbuild_host_write_private_output(transaction, bytes, size);
  } else if (ok) {
    size_t capacity = large ? 67108864u + 16u + 1025u * 512u + 1039u * 512u : 4u;
    if (strcmp(mode, "private-empty") == 0) { size = 0u; capacity = 1u; }
    if (strcmp(mode, "private-reset") == 0) { size = 2u; capacity = 2u; }
#if defined(CUPIDBUILD_PRIVATE_BOUNDS_BASELINE)
    ok = cupidbuild_host_write_private_output(transaction, bytes, size);
    (void)capacity;
#else
    ok = cupidbuild_host_write_private_output_bounded(transaction, bytes, size, capacity);
#endif
    if (ok) ok = cupidbuild_host_capture_private_output(transaction, &snapshot, &captured) &&
                 snapshot.size == size && (size == 0u || memcmp(bytes, captured, size) == 0);
    free(captured); captured = NULL;
    if (ok && strcmp(mode, "private-invalid") == 0) {
#if !defined(CUPIDBUILD_PRIVATE_BOUNDS_BASELINE)
      ok = !cupidbuild_host_write_private_output_bounded(NULL, tiny, 4u, 4u) &&
           !cupidbuild_host_write_private_output_bounded(transaction, NULL, 0u, 4u) &&
           !cupidbuild_host_write_private_output_bounded(transaction, tiny, 4u, 0u) &&
           !cupidbuild_host_write_private_output_bounded(transaction, tiny, 4u, 2147483648u) &&
           !cupidbuild_host_write_private_output_bounded(transaction, tiny, 4u, (size_t)-1) &&
           !cupidbuild_host_write_private_output_bounded(transaction, tiny, 4u, 3u);
#endif
    }
    if (ok && strcmp(mode, "private-snapshot") == 0) {
      cupidbuild_host_snapshot_t wrong = snapshot;
      wrong.sha256[0] ^= 1u;
      ok = !cupidbuild_host_require_private_output(transaction, &wrong);
    }
    if (ok) ok = cupidbuild_host_require_private_output(transaction, &snapshot);
    if (ok && strcmp(mode, "private-reset") == 0) {
      arguments[0] = "private-small-stream";
      arguments[1] = NULL;
      ok = cupidbuild_host_run_to_private_output(transaction, frozen, arguments, 10000u) == 0 &&
           cupidbuild_host_capture_private_output(transaction, &snapshot, &captured) &&
           snapshot.size == 4u && memcmp(captured, tiny, 4u) == 0 &&
           cupidbuild_host_require_private_output(transaction, &snapshot);
      free(captured); captured = NULL;
    }
    if (ok && strcmp(mode, "private-rewrite") == 0) {
      ok = cupidbuild_host_write_private_output(transaction, tiny, 4u) &&
           cupidbuild_host_capture_private_output(transaction, &snapshot, &captured) &&
           snapshot.size == 4u && memcmp(captured, tiny, 4u) == 0 &&
           cupidbuild_host_require_private_output(transaction, &snapshot);
      free(captured); captured = NULL;
    }
    if (ok && (strcmp(mode, "private-large") == 0 || strcmp(mode, "private-publish") == 0)) {
      arguments[0] = "private-read";
      arguments[1] = cupidbuild_host_private_output(transaction);
      arguments[2] = NULL;
      ok = cupidbuild_host_run(transaction, frozen, arguments, 10000u) == 0 &&
           cupidbuild_host_require_private_output(transaction, &snapshot);
    }
    if (ok && strcmp(mode, "private-author") == 0) {
      arguments[0] = "iso-fixture-bundle";
      arguments[1] = cupidbuild_host_private_output(transaction);
      arguments[2] = "-o";
      arguments[3] = cupidbuild_host_candidate(transaction);
      arguments[4] = NULL;
      ok = cupidbuild_host_run(transaction, frozen, arguments, 60000u) == 0 &&
           cupidbuild_host_require_private_output(transaction, &snapshot) &&
           cupidbuild_host_capture_candidate(transaction, &candidate, &captured) &&
           candidate.size == 66342912u &&
           cupidbuild_host_publish_if_changed(transaction, &changed);
      free(captured); captured = NULL;
    }
    if (ok && strcmp(mode, "private-publish") == 0) {
      arguments[0] = "emit";
      arguments[1] = cupidbuild_host_candidate(transaction);
      arguments[2] = NULL;
      ok = cupidbuild_host_run(transaction, frozen, arguments, 10000u) == 0 &&
           cupidbuild_host_capture_candidate(transaction, &candidate, &captured) &&
           candidate.size == 3u && memcmp(captured, "new", 3u) == 0 &&
           cupidbuild_host_require_private_output(transaction, &snapshot) &&
           cupidbuild_host_publish_if_changed(transaction, &changed);
      free(captured);
    }
  }
  free(bytes);
  if (!ok) fprintf(stderr, "%s\n", cupidbuild_host_error(transaction));
  if (!cupidbuild_host_transaction_close(transaction)) return 93;
  return ok ? 0 : 3;
}
'''
CALLER = observer.CALLER.replace("int main(int argc, char **argv) {", PRIVATE_CASE + "\nint main(int argc, char **argv) {", 1)
CALLER = CALLER.replace('  if (argc != 4 || !decode(argv[1]) || !decode(argv[3])) return 90;',
                        '  if (argc == 2 && strcmp(argv[1], "private-small-stream") == 0) return fwrite("keep", 1u, 4u, stdout) == 4u && fflush(stdout) == 0 ? 0 : 91;\n'
                        '  if (argc == 3 && strcmp(argv[1], "private-read") == 0) return private_read(argv[2]);\n'
                        '  if (argc != 4 || !decode(argv[1]) || !decode(argv[3])) return 90;\n'
                        '  if (strncmp(argv[2], "private-", 8) == 0) return private_case(argv[1], argv[2], argv[3]);', 1)


def large_rows():
    rows = []
    parent = b""
    for index in range(7):
        component = bytes([ord("a") + index]) * 127
        parent = parent + (b"/" if parent else b"") + component
        rows.append((parent, None))
    payload = b"P" * 131072
    for index in range(505):
        name = f"f{index:03}_".encode() + b"x" * 59
        rows.append((parent + b"/" + name, payload))
    return rows


class CupidBuildPrivateOutputBoundsTests(unittest.TestCase):
    @classmethod
    def build_checked(cls, directory):
        previous = observer.CALLER
        observer.CALLER = CALLER
        try:
            observer.CupidBuildObserverTests.build_checked.__func__(cls, directory)
        finally:
            observer.CALLER = previous

    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="cupid-private-bounds-")
        cls.addClassCleanup(cls.build.cleanup)
        directory = Path(cls.build.name)
        configured = os.environ.get("CUPIDBUILD_PRIVATE_BOUNDS_PROGRAM")
        cls.program = Path(configured).resolve(strict=True) if configured else directory / ("bounds.exe" if os.name == "nt" else "bounds.elf")
        if not configured:
            compiler = shutil.which("clang")
            if not compiler:
                raise AssertionError("Native contract compiler is unavailable")
            caller = directory / "caller.cc"
            caller.write_text(CALLER, encoding="ascii")
            command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-D_CRT_SECURE_NO_WARNINGS",
                       "-I", str(ROOT / "toolchain"), "-x", "c", str(caller),
                       str(ROOT / "toolchain/cupidbuild_host.cc"), str(ROOT / "toolchain/path_encoding.cc"),
                       *(["-DNATIVE_OBSERVER_WINDOWS_WRITER", "-lntdll"] if os.name == "nt" else []), "-o", str(cls.program)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=180)
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)
        cls.request = directory / "large.bundle"
        cls.request.write_bytes(bundle.wire(large_rows()))
        if not FILE_LIMIT < cls.request.stat().st_size <= PRIVATE_CAPACITY:
            raise AssertionError("Full request must exceed the ordinary private-file bound")

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cupid-private-bounds-case-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "nested").mkdir()
        (self.root / "source.cc").write_bytes(b"int value;")
        self.output = self.root / "nested/file.o"
        self.output.write_bytes(b"old")
        shutil.copyfile(self.program, self.root / "writer")

    def call(self, mode, publish=False, expected=b"new"):
        before = (self.output.read_bytes(), self.output.stat().st_mtime_ns)
        result = subprocess.run([str(self.program), str(self.root).encode().hex(), mode, str(self.request).encode().hex()],
                                capture_output=True, text=True, timeout=600)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stderr, "")
        if publish:
            self.assertEqual(self.output.read_bytes(), expected)
        else:
            self.assertEqual((self.output.read_bytes(), self.output.stat().st_mtime_ns), before)
        self.assertFalse(list(self.root.rglob(".cupidbuild*")))

    def test_full_iso_bundle_exceeds_legacy_bound_and_survives_child_read(self):
        self.call("private-large")

    def test_large_private_input_allows_publication_and_equal_timestamp_reuse(self):
        self.call("private-publish", publish=True)
        before = self.output.stat().st_mtime_ns
        self.call("private-publish", publish=True)
        self.assertEqual(self.output.stat().st_mtime_ns, before)

    def test_full_request_authors_bounded_image_and_passes_independent_checker(self):
        rows = large_rows()
        expected = bundle.image(rows)
        self.assertEqual(len(expected), 66342912)
        self.assertLessEqual(len(expected), FILE_LIMIT)
        native = bundle._build_cli(self.root, "cupidobj", [
            "ctool.cc", "ctool_host.cc", "elf32.cc", "cupidobj.cc",
            "iso_fixture_bundle.cc", "cupidobj_main.cc"])
        native_output = self.root / "native.iso"
        command = [str(native), "iso-fixture-bundle", str(self.request), "-o", str(native_output)]
        self.assertLess(len(subprocess.list2cmdline(command)), 32767)
        result = subprocess.run(command, capture_output=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        self.assertEqual(native_output.read_bytes(), expected)
        # The native Windows CRT does not share delete access with retained files.
        # The Cupid-built producer exercises that transaction path when supplied.
        programs = [] if os.name == "nt" else [native]
        configured = os.environ.get("CUPIDOBJ_ISO_BUNDLE_PROGRAM")
        if configured:
            programs.append(Path(configured).resolve(strict=True))
        checker = image_checker.CupidBuildIsoImageTests
        checker.setUpClass()
        try:
            checker().check([(path.decode("ascii"), payload) for path, payload in rows],
                            native_output.read_bytes(), checked=False, limit=16 * 1024 * 1024)
            for program in programs:
                with self.subTest(producer=str(program)):
                    shutil.copyfile(program, self.root / "writer")
                    self.call("private-author", publish=True, expected=expected)
                    report = checker().check(
                        [(path.decode("ascii"), payload) for path, payload in rows],
                        self.output.read_bytes(), checked=False, limit=16 * 1024 * 1024)
                    self.assertEqual((report.inventory.directories, report.inventory.files,
                                      report.inventory.directory_depth), (8, 505, 8))
        finally:
            checker.doClassCleanups()

    def test_legacy_writer_keeps_its_original_bound(self):
        self.call("private-legacy")

    def test_legacy_rewrite_replaces_and_cleans_the_large_private_input(self):
        self.call("private-rewrite")

    def test_invalid_capacity_and_null_calls_preserve_prior_private_input(self):
        self.call("private-invalid")

    def test_empty_private_input_keeps_a_valid_snapshot(self):
        self.call("private-empty")

    def test_private_output_launch_resets_the_previous_small_capacity(self):
        self.call("private-reset")

    def test_wrong_snapshot_is_rejected_and_current_snapshot_recovers(self):
        self.call("private-snapshot")


if __name__ == "__main__":
    unittest.main()
