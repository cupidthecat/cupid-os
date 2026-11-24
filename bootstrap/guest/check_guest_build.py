#!/usr/bin/env python3
## @file
#  @brief Check guest build plans, checked outputs and failure boundaries.
"""Run the guest build driver with the Linux syscall fixture."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from check_guest_runtime import BUILD, ROOT, SEED_MANIFEST

PLAN = b"compile\n/home/hello.cj\nlink\n/home/hello.lj\n"
COMPILE = b"/user/examples/hello.cc\n/home/hello.o\ninclude /user\n"
LINK = b"/home/hello.elf\n0x01c00000\n_start\n/home/hello.o\n"


def check_object_lists(scratch, command, reference_object, invoke):
    jobs = scratch / "plan/jobs"
    plan = scratch / "plan/cupidbuild.job"
    compile_list = jobs / "sources.job"
    asset_list = jobs / "assets.job"
    compile_step = b"compile-list\n/plan/jobs/sources.job\n"
    asset_step = b"asset-list\n/plan/jobs/assets.job\n"
    head = b"include /user\nsources\n"
    first = b"/user/examples/hello.cc\n/home/list.o\n"
    second = b"/user/examples/hello.cc\n/tmp/list.o\n"
    outputs = (scratch / "home/list.o", scratch / "tmp/list.o")

    plan.write_bytes(compile_step)
    for invalid in (
        head + first + first,
        head + first + b"/user/examples/hello.cc\n",
        head + first + second.replace(b"/tmp/list.o", b"/home/.cupidc-pending.o"),
        head + first + second.replace(b"/tmp/list.o", b"/tmp/.cupidc-pending.o"),
        head + first + second.replace(b"/tmp/list.o", b"/disk/list.o"),
        head.replace(b"sources\n", b"") + first,
        b"unknown /user\nsources\n" + first,
        b"sources\n" + b"".join(
            f"/user/examples/hello.cc\n/tmp/o{index}.o\n".encode("ascii") for index in range(257)
        ),
    ):
        compile_list.write_bytes(invalid)
        log = invoke(b"[cupidc-guest] invalid compile list", "--media")
        assert b"[cupidc-guest] wrote" not in log
        assert all(not path.exists() for path in outputs)

    compile_list.write_bytes(head + first + second.replace(b"hello.cc", b"missing.cc") +
                             second.replace(b"/tmp/list.o", b"/tmp/third.o"))
    log = invoke(b"[cupidc-guest] list stopped at /user/examples/missing.cc", "--media")
    assert outputs[0].read_bytes() == reference_object.read_bytes()
    assert not outputs[1].exists() and not (scratch / "tmp/third.o").exists()
    assert b"build complete" not in log

    compile_list.write_bytes(head + first + second)
    log = invoke(b"[cupidbuild-guest] build complete", "--media")
    assert log.index(b"wrote /home/list.o") < log.index(b"wrote /tmp/list.o")
    assert all(path.read_bytes() == reference_object.read_bytes() for path in outputs)

    assets = scratch / "assets/bin"
    assets.mkdir(parents=True)
    members = [("bin/a.txt", "home/asset.o", b"Cupid\r\nLine\n"),
               ("bin/b.txt", "tmp/asset.o", b"Other\r\nLine\rEnd\n")]
    pairs = b""
    expected = {}
    for index, (logical, output, contents) in enumerate(members):
        source = scratch / "assets" / logical
        source.write_bytes(contents)
        reference = scratch / f"asset{index}.o"
        subprocess.run([*command, "cupidobj", "--", "wrap-text", source,
                        "--identity", logical, "-o", reference], check=True, timeout=120)
        expected[scratch / output] = reference.read_bytes()
        pairs += f"/{logical}\n/{output}\n".encode("ascii")

    plan.write_bytes(asset_step)
    asset_head = b"wrap-text\n/assets\n"
    for invalid in (asset_head.replace(b"wrap-text", b"wrap-readonly") + pairs,
                    asset_head.replace(b"wrap-text", b"wrap-bundle") + pairs,
                    asset_head.replace(b"/assets", b"/assets/../src") + pairs,
                    asset_head + pairs.replace(b"/tmp/asset.o", b"/home/asset.o"),
                    asset_head + pairs.replace(b"/bin/b.txt", b"/bin/../b.txt")):
        asset_list.write_bytes(invalid)
        log = invoke(b"[cupidobj-guest] invalid asset list", "--media")
        assert b"[cupidobj-guest] wrote" not in log
        assert all(not path.exists() for path in expected)

    asset_list.write_bytes(asset_head + pairs.replace(b"/bin/b.txt", b"/bin/missing.txt"))
    log = invoke(b"[cupidobj-guest] list stopped at /bin/missing.txt", "--media")
    assert not (scratch / "tmp/asset.o").exists() and b"build complete" not in log
    assert (scratch / "home/asset.o").read_bytes() == expected[scratch / "home/asset.o"]

    asset_list.write_bytes(asset_head + pairs)
    plan.write_bytes(compile_step + asset_step)
    log = invoke(b"[cupidbuild-guest] build complete", "--media")
    assert log.index(b"wrote /tmp/list.o") < log.index(b"wrote /home/asset.o")
    assert log.index(b"wrote /home/asset.o") < log.index(b"wrote /tmp/asset.o")
    assert all(path.read_bytes() == data for path, data in expected.items())

    project = scratch / "home/project"
    project.mkdir()
    (scratch / "src/user").rename(project / "user")
    (scratch / "home/cupidbuild.job").write_bytes(b"compile-list\n/home/project/sources.job\n")
    local = project / "sources.job"
    prior = b"previous output"
    outputs[0].write_bytes(prior)
    for root in (b"/home/project/../project", b"/" + b"a" * 510):
        local.write_bytes(b"root " + root + b"\n" + head + first + second)
        invoke(b"[cupidc-guest] invalid compile list")
        assert outputs[0].read_bytes() == prior
    local.write_bytes(b"root /home/project\n" + head + first + second)
    invoke(b"[cupidbuild-guest] build complete")
    assert all(path.read_bytes() == reference_object.read_bytes() for path in outputs)

    local.write_bytes(b"root .\n" + head + first + second)
    invoke(b"[cupidc-guest] invalid compile list")
    project_plan = project / "cupidbuild.job"
    project_plan.write_bytes(b"compile-list\nsources.job\n")
    shutil.copytree(project / "user", project / "inputs/user")
    for root in (b"", b"../project", b"inputs/..", b"a" * 512):
        outputs[0].write_bytes(prior)
        local.write_bytes(b"root " + root + b"\n" + head + first + second)
        invoke(b"[cupidc-guest] invalid compile list")
        assert outputs[0].read_bytes() == prior
    for root in (b".", b"inputs"):
        local.write_bytes(b"root " + root + b"\n" + head + first + second)
        invoke(b"[cupidbuild-guest] build complete")
        assert all(path.read_bytes() == reference_object.read_bytes() for path in outputs)
    project_plan.unlink()


def check(scratch: Path) -> None:
    sources = scratch / "src"
    (sources / "user/examples").mkdir(parents=True)
    for path in ("user/cupid.h", "user/examples/hello.cc"):
        shutil.copyfile(ROOT / path, sources / path)
    reference_object = sources / "check.o"
    reference_executable = sources / "check.elf"
    manifest = ROOT / SEED_MANIFEST
    command = [manifest.with_name("cupidbuild.elf"), "run", "--root", ROOT,
               "--seed-manifest", manifest, "--seed-release", ROOT / "bootstrap/seeds/release.json",
               "--tool"]
    subprocess.run([*command, "cupidc", "--", "--root", sources, "--freestanding",
                    "--gnu", "-I", "/user", "-c", "/user/examples/hello.cc", "-o", "/check.o"],
                   check=True, timeout=120)
    subprocess.run([*command, "cupidld", "--", "-m", "elf_i386", "--text-address",
                    "0x01c00000", "--entry", "_start", "-o", reference_executable, reference_object],
                   check=True, timeout=120)
    reference_install = scratch / "install.cc"
    subprocess.run([*command, "cupidobj", "--", "install-source", "demos", "--demos",
                    "demos/hello.asm", "-o", reference_install], cwd=ROOT, check=True, timeout=120)
    assembly = scratch / "input.asm"
    assembly_source = b"[BITS 32]\nsection .text\nglobal sample\nsample: ret\n"
    assembly.write_bytes(assembly_source)
    reference_assembly = scratch / "reference.o"
    subprocess.run([sys.executable, ROOT / "tools/bootstrap_toolchain.py", "run",
                    "--manifest", manifest, "--root", ROOT, "--tool", "cupidasm",
                    "--", "-f", "elf32", assembly, "-o", reference_assembly],
                   check=True, timeout=120)

    home = scratch / "home"
    home.mkdir()
    (home / "hello.cj").write_bytes(COMPILE)
    (home / "hello.lj").write_bytes(LINK)
    assemble = b"assemble\n/home/assemble.job\n"
    install = b"install\n/home/install.job\n"
    assemble_job = home / "assemble.job"
    assemble_job.write_bytes(b"/input.asm\n/home/assembled.o\n")
    install_job = home / "install.job"
    install_job.write_bytes(b"/home/installed.cc\ndemos\n--demos\n/demos/hello.asm\n")
    plan = home / "cupidbuild.job"
    object_path = home / "hello.o"
    executable = home / "hello.elf"

    def invoke(marker: bytes, *arguments: str) -> bytes:
        result = subprocess.run([BUILD / "cupidbuild-guest-linux-contract.elf", scratch, *arguments],
                                check=True, capture_output=True, timeout=120)
        assert marker in result.stdout, result.stdout
        return result.stdout

    for invalid in (PLAN.replace(b"link\n", b"unknown\n"),
                    PLAN.replace(b"/home/hello.lj\n", b"/home/../hello.lj\n"),
                    PLAN + b"link\n", PLAN * 256 + assemble):
        plan.write_bytes(invalid)
        invoke(b"[cupidbuild-guest] invalid request")
        assert not object_path.exists() and not executable.exists()

    plan.write_bytes(assemble + install + PLAN)
    log = invoke(b"[cupidbuild-guest] build complete")
    assert log.index(b"wrote /home/assembled.o") < log.index(b"wrote /home/installed.cc")
    assert log.index(b"wrote /home/hello.o") < log.index(b"wrote /home/hello.elf")
    assert (home / "assembled.o").read_bytes() == reference_assembly.read_bytes()
    assert (home / "installed.cc").read_bytes() == reference_install.read_bytes()
    assert object_path.read_bytes() == reference_object.read_bytes()
    assert executable.read_bytes() == reference_executable.read_bytes()

    sentinel = b"preceding executable"
    executable.write_bytes(sentinel)
    (home / "hello.cj").write_bytes(COMPILE.replace(b"/hello.cc", b"/missing.cc"))
    log = invoke(b"[cupidbuild-guest] stopped at /home/hello.cj")
    assert b"[cupidld-guest]" not in log
    assert object_path.read_bytes() == reference_object.read_bytes()
    assert executable.read_bytes() == sentinel

    (home / "hello.cj").write_bytes(COMPILE)
    (home / "hello.lj").write_bytes(LINK.replace(b"/hello.o", b"/missing.o"))
    log = invoke(b"[cupidbuild-guest] stopped at /home/hello.lj")
    assert b"wrote /home/hello.o" in log and b"build complete" not in log
    assert executable.read_bytes() == sentinel
    raw = home / "kernel.bin"
    raw.write_bytes(b"preceding raw kernel")
    (home / "flatten.job").write_bytes(b"/home/hello.elf\n/home/kernel.bin\n")
    (home / "disk.job").write_bytes(b"/home/boot.bin\n/home/kernel.bin\n/home/disk.img\n")
    plan.write_bytes(b"object\n/home/flatten.job\ndisk\n/home/disk.job\n")
    log = invoke(b"[cupidbuild-guest] stopped at /home/flatten.job")
    assert b"[cupiddisk-guest]" not in log and raw.read_bytes() == b"preceding raw kernel"

    boot = home / "boot.bin"
    boot.write_bytes(b"preceding bootloader")
    (home / "boot.job").write_bytes(b"/missing.asm\n/home/boot.bin\nbootloader\n")
    plan.write_bytes(b"raw\n/home/boot.job\n" + PLAN)
    log = invoke(b"[cupidbuild-guest] stopped at /home/boot.job")
    assert b"[cupidc-guest]" not in log and boot.read_bytes() == b"preceding bootloader"

    assembly.write_bytes(b"[BITS 32]\nsection .text\ndb 0x0f\n")
    plan.write_bytes(assemble + install * 511)
    log = invoke(b"[cupidbuild-guest] stopped at /home/assemble.job")
    assert b"object code check failed" in log and b"[cupidinstall-guest]" not in log
    assert (home / "assembled.o").read_bytes() == reference_assembly.read_bytes()
    assembly.write_bytes(assembly_source)
    install_job.write_bytes(b"/home/installed.cc\ndemos\n--demos\n/demos/hello.cc\n")
    plan.write_bytes(install + PLAN)
    log = invoke(b"[cupidbuild-guest] stopped at /home/install.job")
    assert b"generation failed: input" in log and b"[cupidc-guest]" not in log
    assert (home / "installed.cc").read_bytes() == reference_install.read_bytes()

    temporary = scratch / "tmp"
    temporary.mkdir()
    home_pending = home / ".cupidc-pending.o"
    home_pending.write_bytes(b"preceding HomeFS candidate")
    (home / "hello.cj").write_bytes(COMPILE.replace(b"/home/hello.o", b"/tmp/hello.o"))
    (home / "hello.lj").write_bytes(LINK.replace(b"/home/hello.o", b"/tmp/hello.o"))
    plan.write_bytes(PLAN)
    log = invoke(b"[cupidbuild-guest] build complete")
    assert b"wrote /home/hello.elf" in log
    assert (temporary / "hello.o").read_bytes() == reference_object.read_bytes()
    assert executable.read_bytes() == reference_executable.read_bytes()
    assert home_pending.read_bytes() == b"preceding HomeFS candidate"

    shutil.copytree(sources / "user", temporary / "user")
    (home / "hello.cj").write_bytes(b"/tmp/user/examples/hello.cc\n/tmp/hello.o\ninclude /tmp/user\n")
    invoke(b"[cupidbuild-guest] build complete")
    assert (temporary / "hello.o").read_bytes() == reference_object.read_bytes()

    scratch_pending = temporary / ".cupidc-pending.o"
    assert not scratch_pending.exists()
    scratch_pending.write_bytes(b"preceding temporary candidate")
    log = invoke(b"[cupidbuild-guest] stopped at /home/hello.cj")
    assert b"[cupidld-guest]" not in log
    assert scratch_pending.read_bytes() == b"preceding temporary candidate"
    assert executable.read_bytes() == reference_executable.read_bytes()

    media_root = scratch / "plan"
    jobs = media_root / "jobs"
    jobs.mkdir(parents=True)
    media_plan = media_root / "cupidbuild.job"
    media_request = PLAN.replace(b"/home/hello.", b"/plan/jobs/hello.")
    (jobs / "hello.cj").write_bytes(COMPILE)
    (jobs / "hello.lj").write_bytes(LINK)
    (home / "hello.cj").write_bytes(COMPILE)
    (home / "hello.lj").write_bytes(LINK)
    home_pending.unlink()
    object_path.unlink()
    executable.unlink()
    invoke(b"invalid request file: /plan/cupidbuild.job", "--media")
    assert not object_path.exists() and not executable.exists()
    for wrong_path in (b"/home/hello.lj", b"/plan/jobs-old/hello.lj", b"/plan/jobs/../hello.lj"):
        media_plan.write_bytes(media_request.replace(b"/plan/jobs/hello.lj", wrong_path))
        invoke(b"invalid request file: /plan/cupidbuild.job", "--media")
        assert not object_path.exists() and not executable.exists()

    plan.unlink()
    media_plan.write_bytes(media_request)
    invoke(b"[cupidbuild-guest] build complete", "--media")
    assert object_path.read_bytes() == reference_object.read_bytes()
    assert executable.read_bytes() == reference_executable.read_bytes()
    assert not plan.exists() and media_plan.read_bytes() == media_request

    kernel_list = jobs / "kernel.job"
    media_plan.write_bytes(b"kernel\n/plan/jobs/kernel.job\n")
    for invalid in (b"", b"/disk/object.elf\n", b"/disk/../input.o\n",
                    b"/disk/input.o\n/disk/input.o\n", b"/home/ksyms_data.o\n",
                    b"root /home/../src\n/disk/input.o\n",
                    b"root /" + b"a" * 510 + b"\n/disk/input.o\n",
                    b"".join(f"/disk/i{index}.o\n".encode("ascii") for index in range(512))):
        kernel_list.write_bytes(invalid)
        log = invoke(b"[cupidbuild-guest] invalid kernel object list", "--media")
        assert b"[cupidraw-guest]" not in log and b"build complete" not in log
        assert boot.read_bytes() == b"preceding bootloader"

    kernel_list.write_bytes(b"/disk/input.o\n")
    log = invoke(b"[cupidbuild-guest] kernel build stopped at bootloader", "--media")
    assert b"[cupidld-guest]" not in log and b"build complete" not in log
    assert boot.read_bytes() == b"preceding bootloader"
    plan.write_bytes(b"kernel\n/home/kernel.job\n")
    (home / "kernel.job").write_bytes(b"root /home/project\n/disk/input.o\n")
    invoke(b"[cupidbuild-guest] kernel build stopped at bootloader")
    assert boot.read_bytes() == b"preceding bootloader"
    scratch_pending.unlink()
    check_object_lists(scratch, command, reference_object, invoke)
    project = home / "project"
    project_jobs = project / "jobs"
    project_jobs.mkdir()
    local_plan = project / "cupidbuild.job"
    build_directory = project / "build"
    build_directory.mkdir()
    project_boot = build_directory / "boot.bin"
    project_boot.write_bytes(b"preceding project bootloader")
    local_plan.write_bytes(b"kernel\njobs/kernel.job\n")
    project_kernel = project_jobs / "kernel.job"
    for invalid in (b"output /home/project/../build\n/disk/input.o\n",
                    b"output /src\n/disk/input.o\n",
                    b"output /home/" + b"a" * 500 + b"\n/disk/input.o\n",
                    b"output /home/project/build\n/home/project/build/ksyms_data.o\n",
                    b"output build\n/home/project/build/ksyms_data.o\n",
                    b"output ../build\n/disk/input.o\n",
                    b"output \n/disk/input.o\n",
                    b"root ../project\noutput build\n/disk/input.o\n",
                    b"output build\nimage 2048\n/disk/input.o\n",
                    b"output /tmp\nimage 32\n/disk/input.o\n"):
        project_kernel.write_bytes(invalid)
        log = invoke(b"[cupidbuild-guest] invalid kernel object list")
        assert b"[cupidraw-guest]" not in log
        assert project_boot.read_bytes() == b"preceding project bootloader"
        assert boot.read_bytes() == b"preceding bootloader"
    project_kernel.write_bytes(b"root /home/project\noutput /home/project/build\n/disk/input.o\n")
    invoke(b"[cupidbuild-guest] kernel build stopped at bootloader")
    assert project_boot.read_bytes() == b"preceding project bootloader"
    assert boot.read_bytes() == b"preceding bootloader"
    project_kernel.write_bytes(b"root .\noutput build\nimage 32\n/disk/input.o\n")
    invoke(b"[cupidbuild-guest] kernel build stopped at bootloader")
    assert project_boot.read_bytes() == b"preceding project bootloader"
    local_output = project / "obj"
    local_object = local_output / "local.o"
    (project_jobs / "assemble.job").write_bytes(b"/input.asm\n/home/project/obj/local.o\n")
    local_request = b"assemble\njobs/assemble.job\n"
    directory_request = b"mkdir\nobj\n"
    plan.write_bytes(assemble)
    (home / "assembled.o").write_bytes(b"previous HomeFS output")
    for name in (b"../assemble.job", b"jobs/../assemble.job", b"a" * 512):
        local_plan.write_bytes(local_request + b"assemble\n" + name + b"\n")
        invoke(b"invalid request file: /home/project/cupidbuild.job")
        assert not local_object.exists()
        assert (home / "assembled.o").read_bytes() == b"previous HomeFS output"
    local_plan.write_bytes(b"assemble\njobs/missing.job\n")
    invoke(b"stopped at /home/project/jobs/missing.job")
    for invalid_directory in (b"../obj", b"/home/obj", b"a" * 512):
        local_plan.write_bytes(directory_request + b"mkdir\n" + invalid_directory + b"\n")
        invoke(b"invalid request file: /home/project/cupidbuild.job")
        assert not local_output.exists()
    for blocked_directory in (b"jobs/assemble.job", b"missing/obj"):
        local_plan.write_bytes(b"mkdir\n" + blocked_directory + b"\n" + local_request)
        invoke(b"stopped at /home/project/" + blocked_directory)
        assert not local_output.exists()
    local_plan.write_bytes(directory_request + local_request)
    media_plan.write_bytes(b"mkdir\n/plan/jobs/obj\n")
    invoke(b"invalid request file: /plan/cupidbuild.job", "--media")
    assert not (scratch / "plan/jobs/obj").exists()
    media_plan.unlink()
    invoke(b"invalid request file: /plan/cupidbuild.job", "--media")
    assert not local_object.exists()
    for _ in range(2):
        invoke(b"[cupidbuild-guest] build complete")
        assert local_object.read_bytes() == reference_assembly.read_bytes()
    assert (home / "assembled.o").read_bytes() == b"previous HomeFS output"
    print("Guest builds matched checked outputs from HomeFS and source-media plans.")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(dir=BUILD, prefix="guest-build-") as temporary:
        check(Path(temporary))
