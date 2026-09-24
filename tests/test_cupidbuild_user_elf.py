import json
import os
import random
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.bootstrap_toolchain import (
    _candidate_build_plan,
    WINDOWS_CUPIDBUILD_IMPORTS,
    _validate_i386_relocatable,
    _validate_static_i386_elf,
    _validate_static_i386_pe32,
    _windows_build_plan,
    _windows_link_arguments,
    freeze_seed_inputs,
    require_live_seed_inputs,
)
from tools.cupidld_user_link import UserLinkError, validate_user_executable_bytes


ROOT = Path(__file__).resolve().parents[1]
CONTRACT = "/toolchain/tests/cupidbuild_user_elf_contract.cc"
BASE = 0x01C00000
END = 0x01E00000


def image(segments=None, entry=BASE):
    if segments is None:
        segments = [(1, 1024, BASE, 0, 16, 32, 5, 1)]
    result = bytearray(2048)
    result[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into("<HHIIIIIHHHHHH", result, 16,
                     2, 3, 1, entry, 52, 0, 0, 52, 32, len(segments), 0, 0, 0)
    for index, segment in enumerate(segments):
        struct.pack_into("<8I", result, 52 + index * 32, *segment)
    result[1024:1040] = b"\x90" * 15 + b"\xc3"
    return bytes(result)


def changed(source, offset, value, width=4):
    result = bytearray(source)
    struct.pack_into("<I" if width == 4 else "<H", result, offset, value)
    return bytes(result)


def oracle(payload):
    try:
        validate_user_executable_bytes(payload)
    except UserLinkError as error:
        return str(error)
    return "ok"


def run(arguments, timeout=300):
    result = subprocess.run(list(map(str, arguments)), cwd=ROOT,
                            capture_output=True, text=True, timeout=timeout)
    if result.returncode:
        raise AssertionError(f"command failed: {arguments}\n{result.stdout}{result.stderr}")
    return result


class CupidBuildUserElfTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix=".user-elf-", dir=ROOT / "toolchain")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = Path(cls.temporary.name)
        suffix = ".exe" if os.name == "nt" else ""
        cls.host = cls.directory / ("cupidbuild-user-elf-contract" + suffix)
        run(["make", "-C", ROOT / "toolchain", f"BUILD_DIR={cls.directory.name}",
             f"{cls.directory.name}/{cls.host.name}"])
        seed_path = ROOT / "bootstrap/seeds" / (
            "i386-windows" if os.name == "nt" else "i386-linux") / "manifest.json"
        cls.seed = freeze_seed_inputs(seed_path, cls.directory / "seed")
        plan = json.loads((ROOT / "bootstrap/seeds/i386-linux/manifest.json").read_text())["build_plan"]
        plan = _candidate_build_plan(plan)
        if os.name == "nt":
            plan = _windows_build_plan(plan)
        order = plan["links"]["cupidbuild"]
        objects = {name: cls.directory / (name + ".target.o") for name in order}
        for source in plan["sources"]:
            name = source["name"]
            if name not in objects:
                continue
            path = CONTRACT if name == "cupidbuild_main" else source["path"]
            arguments = ["--root", ROOT, "-c", path,
                         "-o", "/" + objects[name].relative_to(ROOT).as_posix(),
                         *plan["include_arguments"]]
            for definition in source.get("definitions", []):
                arguments += ["-D", definition]
            if source["gnu_extensions"]:
                arguments += ["--gnu"]
            run([cls.seed.tools["cupidc"], *arguments])
            _validate_i386_relocatable(objects[name])
            require_live_seed_inputs(cls.seed)
        assembly = list(plan.get("assembly_sources", [])) + [{
            "name": "start", "path": "toolchain/hosted/i386-windows/tool_start.asm"
            if os.name == "nt" else "toolchain/hosted/i386-linux/start.asm"}]
        for source in assembly:
            if source["name"] in objects:
                run([cls.seed.tools["cupidasm"], "-f", "elf32",
                     ROOT / source["path"].lstrip("/"), "-o", objects[source["name"]]])
                _validate_i386_relocatable(objects[source["name"]])
                require_live_seed_inputs(cls.seed)
        cls.checked = cls.directory / ("checked-contract.exe" if os.name == "nt" else "checked-contract.elf")
        arguments = (_windows_link_arguments("cupidbuild", cls.checked, objects, order)
                     if os.name == "nt" else ["-m", "elf_i386", "--text-address", "0x08048000",
                                             "--entry", "_start", "-o", cls.checked,
                                             *[objects[name] for name in order]])
        run([cls.seed.tools["cupidld"], *arguments])
        require_live_seed_inputs(cls.seed)
        if os.name == "nt":
            _validate_static_i386_pe32(cls.checked, 0x00401000,
                                      WINDOWS_CUPIDBUILD_IMPORTS)
        else:
            _validate_static_i386_elf(cls.checked, 0x08048000)
            cls.checked.chmod(0o755)

    def compare(self, cases):
        request = self.directory / "request.bin"
        request.write_bytes(b"".join(struct.pack("<I", len(data)) + data for _, data, _ in cases))
        expected = [result for _, _, result in cases]
        for name, data, result in cases:
            self.assertEqual(oracle(data), result, name)
        for executable in (self.host, self.checked):
            with self.subTest(executable=executable.name):
                result = run([executable, "batch", request])
                self.assertEqual(result.stderr, "")
                actual = result.stdout.splitlines()
                self.assertEqual(len(actual), len(cases))
                for (name, _, _), received, wanted in zip(cases, actual, expected):
                    self.assertEqual(received, wanted, name)

    def test_valid_loader_boundaries(self):
        code = (1, 1024, BASE, 0, 16, 32, 5, 1)
        null = (0, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0, 0, 7, 0)
        stack = (0x6474E551, 0, 0, 0, 0, 0, 6, 16)
        data = (1, 1040, END - 16, 0, 0, 16, 6, 1)
        cases = [("basic", image()), ("last file byte", image(entry=BASE + 15)),
                 ("null and stack", image([null, code, stack])),
                 ("arena end bss", image([code, data])),
                 ("reverse load order", image([data, code])),
                 ("all 16 program headers", image([null] * 15 + [code])),
                 ("all 16 load ranges", image([
                     (1, 1024, BASE + index * 32, 0, 16, 32, 5, 1)
                     for index in range(16)])),
                 ("exact file end", changed(image(), 56, 2032)),
                 ("empty load outside arena", image([code, (1, 0, 0xFFFFFFFF, 0, 0, 0, 0, 0)])),
                 ("touching load ranges", image([code, (1, 1040, BASE + 32, 0, 16, 16, 4, 1)])),
                 ("physical and section fields ignored", changed(changed(image(), 64, 0xFFFFFFFF), 32, 0xFFFFFFFF))]
        for alignment in (0, 1, 2, 4, 16, 256, 1024):
            cases.append((f"alignment {alignment}", changed(image(), 80, alignment)))
        for flags in range(8):
            if flags & 1:
                cases.append((f"executable permissions {flags}", changed(image(), 76, flags)))
        self.compare([(name, payload, "ok") for name, payload in cases])

    def test_rejects_each_header_and_segment_boundary(self):
        good = image()
        cases = []
        def change(name, offset, value, reason, width=4):
            cases.append((name, changed(good, offset, value, width), reason))
        for length in range(52):
            cases.append((f"short header {length}", good[:length], "ELF header is outside the linked executable"))
        for length in range(52, 84):
            cases.append((f"short program table {length}", good[:length],
                          "linked executable has a truncated program table"))
        for offset in range(7):
            payload = bytearray(good)
            payload[offset] ^= 0xFF
            cases.append((f"ident {offset}", bytes(payload), "linked executable is not little-endian ELF32 version 1"))
        for offset, value, width in ((16, 1, 2), (18, 62, 2), (20, 2, 4)):
            change("ELF target", offset, value, "linked executable is not an i386 ELF32 executable", width)
        for offset, value in ((40, 51), (42, 31), (44, 0)):
            change("program table shape", offset, value, "linked executable has an invalid program table", 2)
        change("too many headers", 44, 17, "linked executable has more than 16 program headers", 2)
        for offset in (0, 51, 0x80000000, 0xFFFFFFFF):
            change("program offset", 28, offset, "linked executable has an invalid program-header offset")
        for offset in (len(good), len(good) + 1, 0x7FFFFFFF):
            change("truncated table", 28, offset, "linked executable has a truncated program table")
        for kind in (2, 3, 4, 6, 0xFFFFFFFF):
            change("unsupported segment", 52, kind, "program header 0 has an unsupported program type")
        change("unknown flags", 76, 8, "program header 0 has unknown permission flags")
        change("alignment shape", 80, 3, "program header 0 alignment is not a power of two")
        change("incongruent alignment", 80, 4096, "load segment 0 alignment is incongruent")
        for kind in (0, 0x6474E551):
            change("non-load payload", 52, kind, "non-load program header has a payload at index 0")
            for field, value, diagnostic in (
                    (6, 8, "program header 1 has unknown permission flags"),
                    (7, 3, "program header 1 alignment is not a power of two"),
                    (4, 1, "non-load program header has a payload at index 1"),
                    (5, 1, "non-load program header has a payload at index 1")):
                segment = [kind, 0, 0, 0, 0, 0, 0, 0]
                segment[field] = value
                cases.append(("non-load metadata", image([
                    (1, 1024, BASE, 0, 16, 32, 5, 1), segment]), diagnostic))
        change("file exceeds memory", 72, 15, "load segment 0 has more file bytes than memory bytes")
        change("file range overflow", 56, 0xFFFFFFF8, "load file range overflows the i386 address space")
        change("memory range overflow", 60, 0xFFFFFFF0, "load memory range overflows the i386 address space")
        change("file range outside candidate", 56, len(good), "load segment 0 extends beyond the executable")
        for address in (BASE - 1, END - 31, END):
            change("arena range", 60, address, "load segment 0 is outside the external executable arena")
        for entry in (BASE - 1, BASE + 16, BASE + 31, BASE + 32, END):
            change("entry range", 24, entry, "entry point is not in executable file-backed bytes")
        change("nonexecutable entry", 76, 6, "entry point is not in executable file-backed bytes")
        code = (1, 1024, BASE, 0, 16, 32, 5, 1)
        for address in (BASE, BASE + 1, BASE + 31):
            cases.append(("overlap", image([code, (1, 1040, address, 0, 16, 32, 6, 1)]),
                          "linked executable load segments overlap"))
        loads = [(1, 1024, BASE + index * 32, 0, 16, 32, 5, 1)
                 for index in range(16)]
        loads[-1] = (1, 1024, BASE + 1, 0, 16, 32, 5, 1)
        cases.append(("sixteenth load overlaps first", image(loads),
                      "linked executable load segments overlap"))
        for segments in ([(0, 0, 0, 0, 0, 0, 0, 0)], [(1, 0, 0, 0, 0, 0, 0, 0)]):
            cases.append(("no nonempty load", image(segments), "linked executable has no nonempty loadable segment"))
        self.compare(cases)

    def test_deterministic_mutations_match_python_diagnostics(self):
        rng = random.Random(399)
        cases = []
        seeds = [image(), image([(0, 0, 0, 0, 0, 0, 0, 0),
                                (1, 1024, BASE, 0, 16, 32, 5, 1)])]
        for index in range(1200):
            payload = bytearray(seeds[index % len(seeds)])
            for _ in range(rng.randint(1, 4)):
                payload[rng.randrange(116)] = rng.randrange(256)
            if index % 7 == 0:
                payload = payload[:rng.randrange(len(payload))]
            payload = bytes(payload)
            cases.append((f"mutation {index}", payload, oracle(payload)))
        self.compare(cases)

    def test_real_checked_user_executables(self):
        cases = []
        for name in ("cat", "hello", "ls"):
            output = self.directory / (name + ".user.o")
            run([self.seed.tools["cupidc"], "--root", ROOT, "--freestanding",
                 "-I", "/user", "-c", f"/user/examples/{name}.cc",
                 "-o", "/" + output.relative_to(ROOT).as_posix()])
            _validate_i386_relocatable(output)
            executable = self.directory / (name + ".user.elf")
            run([self.seed.tools["cupidld"], "-m", "elf_i386", "--text-address", hex(BASE),
                 "--entry", "_start", "-o", executable, output])
            require_live_seed_inputs(self.seed)
            cases.append((name, executable.read_bytes(), "ok"))
        self.compare(cases)


if __name__ == "__main__":
    unittest.main()
