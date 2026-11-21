## @file
#  @brief Provide a separate source and seed tree for Doom profile tests.
import tempfile
from pathlib import Path

from tools import cupidc_kernel_compile as kernel_compile


class DoomProfileFixture:
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
