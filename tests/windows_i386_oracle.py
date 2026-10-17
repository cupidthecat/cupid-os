"""Execute the private compiler's fixed-address i386 oracle on Windows."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import time
import unittest


SUPPORT = Path(__file__).resolve().parent / "support"


class WindowsI386Oracle:
    """One test-owned PE launcher; each invocation gets fresh process memory."""

    def __init__(self, directory):
        if os.name != "nt":
            raise RuntimeError("the native i386 launcher requires Windows")
        self.clang = shutil.which("clang")
        self.linker = shutil.which("ld.lld")
        if not self.clang or not self.linker:
            raise unittest.SkipTest("Clang and LLVM ld.lld are required for the Windows i386 oracle")
        kits = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Windows Kits/10/Lib"
        libraries = sorted(kits.glob("*/um/x86/kernel32.Lib"))
        if not libraries:
            raise unittest.SkipTest("the Windows SDK x86 Kernel32 library is required for the i386 oracle")
        self.launcher = Path(directory) / "windows-i386-oracle.exe"
        command = [
            self.clang, "-target", "i686-pc-windows-msvc", "-x", "c", "-std=c11",
            "-O0", "-ffreestanding", "-fno-stack-protector", "-fuse-ld=lld",
            str(SUPPORT / "windows_i386_oracle.cc"), "-x", "assembler",
            str(SUPPORT / "windows_i386_stack.S"), "-x", "none", str(libraries[-1]),
            "-Wl,/entry:start,/subsystem:console,/nodefaultlib,/dynamicbase:no,/machine:x86,/safeseh:no",
            "-o", str(self.launcher),
        ]
        result = subprocess.run(command, text=True, capture_output=True, timeout=60)
        if result.returncode:
            raise AssertionError("Windows i386 launcher build failed\n" + result.stdout + result.stderr)
        image = self.launcher.read_bytes()
        pe = struct.unpack_from("<I", image, 0x3c)[0] if len(image) >= 64 else len(image)
        if (image[:2] != b"MZ" or pe + 26 > len(image) or image[pe:pe + 4] != b"PE\0\0"
                or struct.unpack_from("<H", image, pe + 4)[0] != 0x14c
                or struct.unpack_from("<H", image, pe + 24)[0] != 0x10b):
            raise AssertionError("Windows i386 launcher did not produce an i386 PE32 image")

    def run(self, directory, entry_offset, timeout=30):
        """Assemble unchanged GAS stubs, link their ELF, and execute emitted bytes."""
        directory = Path(directory)
        deadline = time.monotonic() + timeout
        commands = (
            [self.clang, "-target", "i386-unknown-linux-gnu", "-c", "runner.S", "-o", "runner.o"],
            [self.linker, "-m", "elf_i386", "-T", "runner.ld", "runner.o", "-o", "runner"],
            [str(self.launcher), str(directory / "runner"), hex(0x01100000 + entry_offset)],
        )
        for command in commands:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise subprocess.TimeoutExpired(command, timeout)
            result = subprocess.run(command, cwd=directory, text=True, capture_output=True,
                                    check=False, timeout=remaining)
            if result.returncode:
                return result
        return result
