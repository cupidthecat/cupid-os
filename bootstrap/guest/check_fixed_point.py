"""Check that the loaded freestanding compiler reproduces its complete ELF."""

from pathlib import Path
import resource
import struct
import subprocess
import sys
import tempfile


def limit_stack():
    _, hard_limit = resource.getrlimit(resource.RLIMIT_STACK)
    resource.setrlimit(resource.RLIMIT_STACK, (2 * 1024 * 1024, hard_limit))


def main(root, core_names, link):
    artifacts = root / "build/guest-engine"
    module = artifacts / "cupidc-engine.elf"
    harness = artifacts / "engine-linux-contract.elf"
    sources = [root / "toolchain" / (name + ".cc") for name in core_names]
    sources.append(root / "bootstrap/guest/cupidc_engine.cc")

    def logical(path):
        return "/" + path.relative_to(root).as_posix()

    with tempfile.TemporaryDirectory(dir=artifacts, prefix="fixed-point-") as scratch:
        replay = Path(scratch)
        rejected_source = replay / "malformed.cc"
        rejected_source.write_text("int invalid = ;\n", encoding="ascii")
        protected_output = replay / "protected.o"
        sentinel = b"keep this output\n"
        protected_output.write_bytes(sentinel)
        invalid_module = replay / "invalid.elf"
        invalid = bytearray(module.read_bytes())
        struct.pack_into("<I", invalid, 24, 0)
        invalid_module.write_bytes(invalid)

        def exercise(engine, source, output):
            return subprocess.run(
                [harness, root, logical(engine), logical(source), logical(output),
                 logical(rejected_source), logical(protected_output)],
                preexec_fn=limit_stack, timeout=600,
                capture_output=True, text=True,
            )

        rejected = exercise(invalid_module, sources[0], protected_output)
        if (rejected.returncode != 4 or
            "compiler engine ELF rejected" not in rejected.stderr or
            protected_output.read_bytes() != sentinel):
            raise RuntimeError(f"invalid ELF reached the engine: {rejected}")

        rebuilt = []
        for source in sources:
            output = replay / (source.stem + ".o")
            result = exercise(module, source, output)
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
    split = sys.argv.index("--")
    main(Path(sys.argv[1]).resolve(), sys.argv[2:split], sys.argv[split + 1:])
