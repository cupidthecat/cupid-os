## @file
#  @brief Rebuild the compiler engine and compare each stage with the checked inputs.
"""Verify the compiler engine load and its byte-identical rebuild."""

from pathlib import Path
import resource
import struct
import subprocess
import sys
import tempfile


def limit_stack():
    _, hard_limit = resource.getrlimit(resource.RLIMIT_STACK)
    resource.setrlimit(resource.RLIMIT_STACK, (2 * 1024 * 1024, hard_limit))


def write_zero_fill_fixture(path):
    ident = b"\x7fELF\x01\x01\x01" + bytes(9)
    header = struct.pack(
        "<16sHHIIIIIHHHHHH", ident, 2, 3, 1, 0x01100010, 52, 0, 0,
        52, 32, 2, 0, 0, 0,
    )
    payload = b"\xc3test"
    text = struct.pack("<8I", 1, 116, 0x01100010, 0x01100010, 1, 1, 5, 1)
    data = struct.pack("<8I", 1, 117, 0x01100030, 0x01100030, 4, 16, 6, 1)
    path.write_bytes(header + text + data + payload)


def load_controls(module, replay):
    image = module.read_bytes()
    (header_table,) = struct.unpack_from("<I", image, 28)
    header_size, count = struct.unpack_from("<HH", image, 42)
    headers = {
        offset: struct.unpack_from("<8I", image, offset)
        for offset in range(header_table, header_table + count * header_size, header_size)
    }
    loads = [offset for offset, fields in headers.items() if fields[0] == 1]
    stack = next(offset for offset, fields in headers.items() if fields[0] == 0x6474E551)
    text = next(offset for offset in loads if headers[offset][6] & 1)
    data = next(offset for offset in loads if headers[offset][4] and not (headers[offset][6] & 1))

    def mutate(name, *edits):
        changed = bytearray(image)
        for offset, value in edits:
            struct.pack_into("<I", changed, offset, value)
        path = replay / (name + ".elf")
        path.write_bytes(changed)
        return path

    yield mutate("entry-outside", (24, 0))
    yield mutate("entry-in-data", (24, headers[data][2]))
    _, _, start, _, filesz, memsz, _, _ = headers[text]
    yield mutate("entry-in-zero-fill", (24, start + filesz), (text + 20, max(filesz + 1, memsz)))
    yield mutate("outside-arena", (loads[-1] + 8, 0x01A00000), (loads[-1] + 28, 1))
    yield mutate("overlap", (loads[1] + 8, headers[loads[0]][2]), (loads[1] + 28, 1))
    yield mutate("unknown-flags", (text + 24, headers[text][6] | 8))
    yield mutate("executable-stack", (stack + 24, headers[stack][6] | 1))
    yield mutate("dynamic-header", (stack, 2))
    yield mutate("truncated-segment", (text + 4, len(image)))


def main(root, core_names, link):
    artifacts = root / "build/guest-engine"
    module = artifacts / "cupidc-engine.elf"
    harness = artifacts / "engine-linux-contract.elf"
    sources = [root / "toolchain" / (name + ".cc") for name in core_names]
    sources.append(root / "bootstrap/guest/cupidc_engine.cc")

    def logical(path):
        return "/" + path.relative_to(root).as_posix()

    def exercise(engine, *paths):
        return subprocess.run(
            [harness, root, logical(engine), *(logical(path) for path in paths)],
            preexec_fn=limit_stack, timeout=600,
            capture_output=True, text=True,
        )

    with tempfile.TemporaryDirectory(dir=artifacts, prefix="fixed-point-") as scratch:
        replay = Path(scratch)
        zero_fill = replay / "zero-fill.elf"
        write_zero_fill_fixture(zero_fill)
        for valid_module in (module, zero_fill):
            valid = exercise(valid_module)
            if valid.returncode or valid.stdout != "compiler engine load verified\n":
                raise RuntimeError(f"{valid_module.name} failed to load: {valid}")
        invalid_modules = list(load_controls(module, replay))
        for invalid in invalid_modules:
            rejected = exercise(invalid)
            if rejected.returncode != 4 or "compiler engine ELF rejected" not in rejected.stderr:
                raise RuntimeError(f"invalid engine {invalid.name} was not rejected: {rejected}")
        print(f"engine and zero-fill fixture passed; {len(invalid_modules)} invalid ELF images rejected",
              flush=True)

        if not link:
            return

        rejected_source = replay / "malformed.cc"
        rejected_source.write_text("int invalid = ;\n", encoding="ascii")
        protected_output = replay / "protected.o"
        sentinel = b"keep this output\n"
        protected_output.write_bytes(sentinel)

        rebuilt = []
        for source in sources:
            output = replay / (source.stem + ".o")
            result = exercise(module, source, output, rejected_source, protected_output)
            if result.returncode or not result.stdout.startswith("compiler engine emitted "):
                raise RuntimeError(f"{source.name} failed: {result}")
            if protected_output.read_bytes() != sentinel:
                raise RuntimeError(f"rejected request replaced output for {source.name}")
            if output.read_bytes() != (artifacts / output.name).read_bytes():
                raise RuntimeError(f"{source.name} produced different object bytes")
            print(f"{source.name}: {output.stat().st_size} bytes match", flush=True)
            rebuilt.append(output)

        second = replay / module.name
        subprocess.run([*link, "-o", second, *rebuilt], check=True, timeout=180)
        if second.read_bytes() != module.read_bytes():
            raise RuntimeError("relinked compiler engine differs")
        print(f"{module.name}: {second.stat().st_size} bytes match", flush=True)


if __name__ == "__main__":
    root = Path(sys.argv[1]).resolve()
    if sys.argv[2:] == ["--load-only"]:
        main(root, [], [])
    else:
        split = sys.argv.index("--")
        main(root, sys.argv[2:split], sys.argv[split + 1:])
