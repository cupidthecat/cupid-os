"""Execute the installed help reader against its compact and formatted metadata."""

from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *document;
static int vfs_open(char *path, int flags) { (void)path; (void)flags; return 0; }
static int vfs_read(int fd, char *buffer, int capacity) {
    size_t size = strlen(document);
    (void)fd;
    if (size > (size_t)capacity) size = (size_t)capacity;
    memcpy(buffer, document, size);
    return (int)size;
}
static void vfs_close(int fd) { (void)fd; }
static void print(char *text) { fputs(text, stdout); }
static void println(char *text) { puts(text); }
READER
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    document = argv[1];
    return show_help_lines("fixture.cc", atoi(argv[2])) ? 0 : 1;
}
'''


class HelpMetadataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang" if os.name == "nt" else "cc")
        if compiler is None:
            raise RuntimeError("A native C compiler is required for the help reader contract")
        temporary = tempfile.TemporaryDirectory(prefix="cupid-help-metadata-")
        cls.addClassCleanup(temporary.cleanup)
        directory = Path(temporary.name)
        source = directory / "reader.c"
        reader = (ROOT / "bin/help.cc").read_text(encoding="utf-8").split(
            "void show_cmd_help(", 1)[0]
        source.write_text(HARNESS.replace("READER", reader), encoding="utf-8")
        cls.program = directory / ("reader.exe" if os.name == "nt" else "reader")
        result = subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                                 str(source), "-o", str(cls.program)],
                                capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def check_reader(self, source, expected, summary=False):
        result = subprocess.run([str(self.program), source, str(int(summary))],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0 if expected else 1, result.stderr)
        self.assertEqual(result.stdout, expected)
        self.assertEqual(result.stderr, "")

    def test_compact_and_formatted_metadata_keep_all_help_lines(self):
        for prefix in ("//help:", "// help:", "//\thelp:", "// \thelp:"):
            with self.subTest(prefix=prefix):
                self.check_reader(prefix + " First\n" + prefix + " Second\nint value;\n",
                                  "First\nSecond\n")

    def test_summary_prints_only_the_first_description(self):
        self.check_reader("// help: First\n// help: Second\n", "First", summary=True)

    def test_plain_comments_and_incomplete_markers_are_not_help(self):
        for source in ("// ordinary\nint value;", "/", "//", "// help", "// helpx: wrong\n",
                       "// help : wrong\n", "\n" * 4094 + "/"):
            with self.subTest(source=source[:30]):
                self.check_reader(source, "")

    def test_code_ends_the_metadata_header(self):
        self.check_reader("// help: First\nint value;\n// help: Later\n", "First\n")
