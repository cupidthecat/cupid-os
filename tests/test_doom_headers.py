"""Compile public Doom headers without relying on a caller's include order."""

from pathlib import Path
import os
import shutil
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DoomHeaderTests(unittest.TestCase):
    def check_header(self, source):
        compiler = shutil.which("clang" if os.name == "nt" else "cc")
        if compiler is None:
            raise RuntimeError("A native C compiler is required for the Doom header contract")
        result = subprocess.run(
            [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
             "-I", str(ROOT / "kernel/doom/src"), "-x", "c", "-fsyntax-only", "-"],
            input=source, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_scale_header_declares_its_video_types_and_can_be_included_twice(self):
        self.check_header('#include <stdbool.h>\n'
                  '#include "i_scale.h"\n'
                  '#include "i_scale.h"\n'
                  '#include "i_video.h"\n'
                          'screen_mode_t *scale_mode(void) { return &mode_scale_1x; }\n')

    def test_wad_header_declares_its_boolean_return_type(self):
        self.check_header('#include <stdbool.h>\n'
                          '#include "w_main.h"\n'
                          '#include "w_main.h"\n'
                          'boolean (*parse_arguments)(void) = W_ParseCommandLine;\n')
