## @file
#  @brief Verify the guest compiler through its Linux syscall fixture.

from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]).resolve()
build = root / "build/guest-engine"
runner = build / "cupidc-guest-linux-contract.elf"

with tempfile.TemporaryDirectory(dir=build, prefix="guest-driver-") as temporary:
    jail = Path(temporary)
    sources = jail / "src/toolchain"
    sources.mkdir(parents=True)
    for source in ("ctool.cc", "ctool.h"):
        shutil.copyfile(root / "toolchain" / source, sources / source)
    (jail / "src/bad.cc").write_bytes(b"int invalid = ;\n")
    home = jail / "home"
    home.mkdir()
    object_file = home / "ctool.o"
    pending = home / ".cupidc-pending.o"
    job = home / "cupidc.job"
    sentinel = b"preceding artifact"

    def check(request, marker):
        job.write_bytes(request)
        result = subprocess.run([runner, jail], check=True, capture_output=True, timeout=90)
        assert marker in result.stdout, result.stdout

    success = b"/toolchain/ctool.cc\n/home/ctool.o\n"
    object_file.write_bytes(sentinel)
    check(success, b"[cupidc-guest] wrote /home/ctool.o")
    assert object_file.read_bytes() == (build / "ctool.o").read_bytes()
    assert not pending.exists()

    object_file.write_bytes(sentinel)
    check(b"/bad.cc\n/home/ctool.o\n", b"[cupidc-guest] compilation failed")
    assert object_file.read_bytes() == sentinel and not pending.exists()
    check(b"/missing.cc\n/home/ctool.o\n", b"[cupidc-guest] compilation failed")
    assert object_file.read_bytes() == sentinel and not pending.exists()
    check(b"/toolchain/ctool.cc\n/home/../ctool.o\n", b"[cupidc-guest] invalid request file")
    assert not (jail / "ctool.o").exists()
    check(b"/toolchain/ctool.cc\n/home/ctool.o\nextra\n", b"[cupidc-guest] invalid request file")
    assert object_file.read_bytes() == sentinel
    check(b"/toolchain/ctool.cc\n/home/.cupidc-pending.o\n", b"[cupidc-guest] invalid request file")
    assert not pending.exists()

    pending.write_bytes(sentinel)
    check(success, b"[cupidc-guest] compilation failed")
    assert object_file.read_bytes() == sentinel and pending.read_bytes() == sentinel
    pending.unlink()
    blocked = home / "blocked.o"
    blocked.mkdir()
    check(b"/toolchain/ctool.cc\n/home/blocked.o\n", b"[cupidc-guest] compilation failed")
    assert blocked.is_dir() and not pending.exists()

    check(success, b"[cupidc-guest] wrote /home/ctool.o")
    assert object_file.read_bytes() == (build / "ctool.o").read_bytes()
    assert not pending.exists()

    (home / "value.h").write_bytes(b"#define VALUE 23\n")
    (home / "source.cc").write_bytes(b'#include "value.h"\nint value(void) { return VALUE; }\n')
    seed = root / "bootstrap/seeds/i386-linux"
    subprocess.run([seed / "cupidbuild.elf", "run", "--root", root,
                    "--seed-manifest", seed / "manifest.json", "--seed-release",
                    root / "bootstrap/seeds/release.json", "--tool", "cupidc", "--",
                    "--root", jail, "--gnu", "--freestanding", "-c", "/home/source.cc",
                    "-o", "/home/checked.o"], check=True, timeout=180)
    check(b"/home/source.cc\n/home/local.o\n", b"[cupidc-guest] wrote /home/local.o")
    local = home / "local.o"
    reference = (home / "checked.o").read_bytes()
    assert local.read_bytes() == reference and not pending.exists()
    check(b"/home/missing.cc\n/home/local.o\n", b"compilation failed: not_found")
    assert local.read_bytes() == reference and not pending.exists()
    print("Guest compiler matched the ISO and HomeFS objects and preserved eight rejected outputs.")

    words = range(32768)
    (home / "ordered.cc").write_text(
        "const unsigned words[] = {\n" + ", ".join(map(str, words)) + "\n};\n", encoding="ascii"
    )
    check(b"/home/ordered.cc\n/home/ordered.o\n", b"wrote /home/ordered.o")
    assert struct.pack(f"<{len(words)}I", *words) in (home / "ordered.o").read_bytes()

    unordered = home / "unordered.cc"
    unordered.write_text(
        "const unsigned values[] = {[7] = 9, [2] = 5, [0] = 4, [4] = 6};\n"
        "int f(void) { int values[4] = {[3] = 6, [0] = 8}; return values[3] + values[0]; }\n",
        encoding="ascii",
    )
    designators = b"/home/unordered.cc\n/home/unordered.o\n"
    check(designators, b"wrote /home/unordered.o")
    result = (home / "unordered.o").read_bytes()
    assert struct.pack("<8I", 4, 0, 5, 0, 6, 0, 0, 9) in result
    unordered.write_text(
        "const unsigned values[] = {[7] = 9, [2] = 5, [2] = 4};\n", encoding="ascii"
    )
    check(designators, b"overriding initialized subobjects")
    assert (home / "unordered.o").read_bytes() == result and not pending.exists()
    print("Ordered and out-of-order initializers passed; a duplicate preserved the preceding object.")

    src = jail / "src"
    (src / "include").mkdir()
    (src / "angle").mkdir()
    (src / "include/quoted.h").write_text("#define QUOTED 3\n", encoding="ascii")
    (src / "angle/angle.h").write_text("#define ANGLE 4\n", encoding="ascii")
    (src / "forced.h").write_text("#define EXTRA (VALUE + 8)\n", encoding="ascii")
    (src / "profile.cc").write_text(
        '#include "quoted.h"\n#include <angle.h>\n'
        "int value(void) { return legacy_call() + ACTIVE + QUOTED + ANGLE + EXTRA; }\n",
        encoding="ascii",
    )
    checked = [seed / "cupidbuild.elf", "run", "--root", root,
               "--seed-manifest", seed / "manifest.json", "--seed-release",
               root / "bootstrap/seeds/release.json", "--tool"]
    checked_profile = [*checked, "cupidc", "--", "--root", src, "--gnu", "--doom-compat",
                       "--freestanding", "-D", "VALUE=7", "-D", "ACTIVE", "-I", "/include",
                       "--include-angle", "/angle", "-include", "/forced.h"]
    subprocess.run([*checked_profile, "-c", "/profile.cc", "-o", "/reference.o"],
                   check=True, timeout=180)
    profile = (b"/profile.cc\n/home/profile.o\ndoom\ndefine VALUE=7\ndefine ACTIVE\n"
               b"include /include\nangle /angle\nforce /forced.h\n")
    check(profile, b"wrote /home/profile.o")
    selected = home / "profile.o"
    expected = (src / "reference.o").read_bytes()
    assert selected.read_bytes() == expected and not pending.exists()
    for request, marker in (
        (profile.replace(b"doom\n", b""), b"compilation failed"),
        (profile + b"doom\n", b"invalid request"),
        (profile.replace(b"include /include", b"include /../include"), b"invalid request"),
        (profile.replace(b"force /forced.h", b"force /missing.h"), b"compilation failed"),
        (profile + b"define =0\n", b"invalid request"),
        (profile + b"define EXTRA_FLAG\n" * 65, b"invalid request"),
    ):
        check(request, marker)
        assert selected.read_bytes() == expected and not pending.exists()
    print("Guest compiler matched the checked profile and preserved output on six rejections.")

    definitions = b"".join(
        f"define FLAG_{index:02}{'A' * 503}={'B' * 511}\n".encode("ascii")
        for index in range(64)
    )
    include_roots = (b"include /" + b"r" * 510 + b"\n") * 32
    (home / "limits.cc").write_bytes(b"int wide_profile(void) { return 42; }\n")
    limits = b"/home/limits.cc\n/home/limits.o\n"
    check(limits, b"wrote /home/limits.o")
    limited = home / "limits.o"
    limits_reference = limited.read_bytes()
    check(limits + definitions + include_roots, b"wrote /home/limits.o")
    assert limited.read_bytes() == limits_reference and not pending.exists()
    check(limits + definitions + include_roots + b"define ONE_MORE\n", b"invalid request")
    assert limited.read_bytes() == limits_reference and not pending.exists()
    print("Guest compiler accepted wide macro definitions and rejected a 65th definition.")

    (src / "helper.cc").write_bytes(b"int helper(void) { return EXTRA + ACTIVE; }\n")
    subprocess.run([*checked_profile, "-c", "/helper.cc", "-o", "/helper.o"], check=True, timeout=180)
    paths = jail / "iso.paths"
    paths.write_bytes(b"profile.o\nhelper.o\n")
    expected_iso = jail / "expected.iso"
    subprocess.run([*checked, "cupidobj", "--", "iso-fixture", paths,
                    "--file", "profile.o", src / "reference.o",
                    "--file", "helper.o", src / "helper.o", "-o", expected_iso],
                   check=True, timeout=120)

    list_file = home / "compile.list"
    members = b"/profile.cc\n/profile.o\n/helper.cc\n/helper.o\n"
    list_file.write_bytes(members)
    bundle_job = b"/home/compile.list\n/home/bundle.iso\nbundle\n" + profile.split(b"\n", 2)[2]
    bundle = home / "bundle.iso"
    check(bundle_job, b"wrote /home/bundle.iso")
    assert bundle.read_bytes() == expected_iso.read_bytes() and not pending.exists()
    for bad in (members.replace(b"helper.cc", b"bad.cc"),
                members.replace(b"helper.cc", b"missing.cc"),
                members.replace(b"/helper.cc", b"/../helper.cc"),
                members.replace(b"helper.o", b"Profile.o")):
        list_file.write_bytes(bad)
        check(bundle_job, b"compilation failed")
        assert bundle.read_bytes() == expected_iso.read_bytes() and not pending.exists()
    print("Guest compiler matched both bundled objects and preserved the ISO after four rejections.")
