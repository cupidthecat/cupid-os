"""Complete retained publication through an explicitly authorized test release."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest

from . import test_iso_fixture_bundle as bundle
from . import test_cupidbuild_private_output_bounds as bounds
from tools import bootstrap_toolchain as bootstrap


ROOT = Path(__file__).resolve().parents[1]
SOURCES = ("ctool", "ctool_host", "elf32", "seed_manifest", "seed_release",
           "contract_parse_internal", "path_encoding", "cupidbuild_host", "cupidbuild",
           "cupidbuild_iso", "cupidbuild_iso_capture", "cupidbuild_iso_image",
           "iso_fixture_bundle", "cupidbuild_iso_publication")
BAD_AUTHOR = r'''
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
  FILE *file;
  if (argc != 5 || strcmp(argv[1], "iso-fixture-bundle") || strcmp(argv[3], "-o")) return 11;
  file = fopen(argv[4], "wb");
  if (!file) return 12;
  if (fwrite("invalid ISO candidate", 1u, 21u, file) != 21u) return 13;
  return fclose(file) == 0 ? 0 : 14;
}
'''


def build_checked(directory, name, role, ordinary, main_path, main_text=None):
    """Build actual i386 callers/authors from the installed checked tools."""
    from tools import artifact_size_contract as contract
    source = directory / (name + "-source")
    paths = [p for p in (ROOT / "toolchain").iterdir() if p.suffix in (".cc", ".h")]
    paths.extend(p for p in (ROOT / "toolchain/hosted").rglob("*") if p.is_file())
    paths.append(ROOT / main_path)
    for path in paths:
        target = source / path.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    if main_text is not None:
        (source / main_path).write_text(main_text, encoding="ascii")
    selected = bootstrap.verify_seed_inputs(ROOT / "bootstrap/seeds" /
        ("i386-windows" if os.name == "nt" else "i386-linux") / "manifest.json")
    plan = json.loads((ROOT / "bootstrap/seeds/i386-linux/manifest.json").read_bytes())["build_plan"]
    if os.name == "nt":
        plan = bootstrap._windows_build_plan(plan, utf8=True, long_paths=True, user_link_aliases=True)
    rows = {row["name"]: row for row in plan["sources"]}
    support = [n for n in plan["links"][role] if n in rows and
               n in ("runtime", "publication_runtime", "windows_utf8", "windows_utf8_build", "path_encoding")]
    main_name = name + "_main"
    compile_names = [main_name, *ordinary, *[n for n in support if n not in ordinary]]
    runner = bootstrap.ToolRunner(source)
    objects = {}
    for item in compile_names:
        row = rows.get(item, {"name": item, "path": "/toolchain/" + item + ".cc",
                             "definitions": ["_WIN32=1"] if os.name == "nt" else [],
                             "gnu_extensions": False})
        path = main_path if item == main_name else row["path"].lstrip("/")
        obj = source / (item + ".o")
        contract._compile_source(selected, runner, source, path, obj,
            row.get("definitions", []), row["gnu_extensions"], 600)
        objects[item] = obj
    assembly = plan.get("assembly_sources", [{"name": "start", "path": "/toolchain/hosted/i386-linux/start.asm"}])
    starts = [n for n in plan["links"][role] if any(r["name"] == n for r in assembly)]
    for row in assembly:
        if row["name"] not in starts:
            continue
        obj = source / (row["name"] + ".o")
        contract._run_checked_tool(selected, runner, "cupidasm", ["-f", "elf32",
            source / row["path"].lstrip("/"), "-o", obj], row["name"], 180)
        objects[row["name"]] = obj
    order = [*starts, *compile_names]
    program = directory / (name + (".exe" if os.name == "nt" else ".elf"))
    if os.name == "nt":
        args = bootstrap._windows_link_arguments(role, program, objects, order,
            utf8=True, long_paths=True, user_link_aliases=True)
        contract._run_checked_tool(selected, runner, "cupidld", args, name, 180)
    else:
        contract._link_contract(selected, runner, [objects[n] for n in order], program, False, 180)
        program.chmod(0o700)
    bootstrap.require_live_seed_inputs(selected)
    return program


def release_fixture(destination, author):
    """Declare test authority over real role/profile-valid images, without promotion."""
    manifests = []
    for host, target in (("i386-linux", "linux"), ("i386-windows", "windows")):
        original = ROOT / "bootstrap/seeds" / host
        directory = destination / target
        directory.mkdir(parents=True)
        value = json.loads((original / "manifest.json").read_bytes())
        for artifact in value["artifacts"]:
            image = original / artifact["file"]
            if artifact["name"] == "cupidobj" and (host == "i386-windows") == (os.name == "nt"):
                image = author
            data = image.read_bytes()
            (directory / artifact["file"]).write_bytes(data)
            artifact.update(size=len(data), sha256=hashlib.sha256(data).hexdigest())
        manifests.append(value)
    linux, windows = manifests
    linux_bytes = json.dumps(linux, sort_keys=True, indent=2).encode() + b"\n"
    windows["provenance"]["plan_seed_manifest_sha256"] = hashlib.sha256(linux_bytes).hexdigest()
    (destination / "linux/manifest.json").write_bytes(linux_bytes)
    (destination / "windows/manifest.json").write_bytes(json.dumps(windows, sort_keys=True, indent=2).encode() + b"\n")
    lp, wp = linux["provenance"], windows["provenance"]
    record = {
        "schema": "cupid.seed-release.v1", "source_revision": lp["source_revision"],
        "source_snapshot_sha256": lp["source_snapshot_sha256"], "source_input_count": lp["source_input_count"],
        "parent_source_revision": lp["parent_seed_source_revision"],
        "parent_linux_manifest_sha256": lp["parent_seed_manifest_sha256"],
        "parent_windows_manifest_sha256": wp["parent_execution_seed_manifest_sha256"],
        "linux_plan_sha256": linux["build_plan_sha256"], "windows_plan_sha256": wp["native_build_plan_sha256"],
        "artifacts": [{"name": a["name"], "format": fmt, "size": a["size"], "sha256": a["sha256"]}
            for fmt, value in (("elf32", linux), ("pe32", windows)) for a in value["artifacts"]],
    }
    (destination / "release.json").write_bytes(json.dumps(record, sort_keys=True, indent=2).encode() + b"\n")


class CupidBuildIsoPublicationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="iso-publication-build-")
        cls.addClassCleanup(cls.build.cleanup)
        directory = Path(cls.build.name)
        supplied = os.environ.get("CUPIDBUILD_ISO_PUBLICATION_PROGRAM")
        cls.program = Path(supplied).resolve(strict=True) if supplied else directory / ("caller.exe" if os.name == "nt" else "caller")
        if not supplied:
            compiler = shutil.which("clang" if os.name == "nt" else "cc")
            if compiler is None:
                raise AssertionError("Native contract compiler unavailable")
            command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                "-D_CRT_SECURE_NO_WARNINGS", "-DCUPIDBUILD_PUBLICATION_RACE_TEST",
                "-I", str(ROOT / "toolchain"), "-x", "c",
                *[str(ROOT / "toolchain" / (n + ".cc")) for n in SOURCES],
                str(ROOT / "toolchain/tests/cupidbuild_iso_publication_contract.cc"),
                *(["-lntdll"] if os.name == "nt" else []), "-o", str(cls.program)]
            result = subprocess.run(command, capture_output=True, timeout=180)
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)
        configured = os.environ.get("CUPIDBUILD_ISO_PUBLICATION_AUTHOR")
        cls.author = Path(configured).resolve(strict=True) if configured else build_checked(directory,
            "author", "cupidobj", ("ctool", "ctool_host", "elf32", "cupidobj", "iso_fixture_bundle"),
            "toolchain/cupidobj_main.cc")
        configured = os.environ.get("CUPIDBUILD_ISO_PUBLICATION_BAD_AUTHOR")
        cls.bad_author = Path(configured).resolve(strict=True) if configured else build_checked(directory,
            "bad-author", "cupidobj", (), "toolchain/cupidobj_main.cc", BAD_AUTHOR)
        cls.cohort = directory / "cohort"
        release_fixture(cls.cohort, cls.author)
        cls.bad_cohort = directory / "bad-cohort"
        release_fixture(cls.bad_cohort, cls.bad_author)

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="iso-publication-case-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        shutil.copytree(self.cohort, self.root / "seed")
        self.paths = ["fixtures.manifest", "fixtures", "output/image.iso",
                      "seed/linux/manifest.json", "seed/windows/manifest.json", "seed/release.json"]
        (self.root / "fixtures").mkdir()
        self.output = self.root / self.paths[2]
        self.output.parent.mkdir()
        self.output.write_bytes(b"prior output")
        os.utime(self.output, ns=(1600000000000000000,) * 2)
        self.before = (self.output.read_bytes(), self.output.stat().st_mtime_ns)

    def arrange(self, rows, manifest=None):
        for name, payload in rows:
            path = self.root / self.paths[1] / name
            path.parent.mkdir(parents=True, exist_ok=True)
            if payload is None: path.mkdir(exist_ok=True)
            else: path.write_bytes(payload)
        (self.root / self.paths[0]).parent.mkdir(parents=True, exist_ok=True)
        (self.root / self.paths[0]).write_bytes(manifest if manifest is not None else
            "\n".join(n for n, _ in rows).encode() + (b"\n" if rows else b""))

    def command(self, mode="publish"):
        return [str(self.program), *[str(p).encode("utf-8").hex() for p in [self.root, *self.paths]], mode]

    def run_case(self, rows=None, success=True, mode="publish", changed=1):
        # Complete retained checks around a 64 MiB image can take more than ten
        # minutes in a checked Windows caller. The author still has its own
        # production deadline; this limit covers the surrounding validation.
        result = subprocess.run(self.command(mode), capture_output=True, timeout=1200)
        self.assertEqual(result.returncode, 0 if success else 1, result.stdout + result.stderr)
        if success:
            expected = bundle.image([(n.encode(), data) for n, data in rows])
            self.assertEqual(self.output.read_bytes(), expected)
            values = result.stdout.decode().split()
            self.assertEqual(values[:3], ["publish", str(changed), str(changed)])
            self.assertEqual(int(values[-1]), len(expected))
            self.assertEqual(result.stderr, b"")
        else:
            self.assertTrue(result.stderr.startswith(b"rejected"), result.stderr)
            self.assertEqual((self.output.read_bytes(), self.output.stat().st_mtime_ns), self.before)
        self.assertFalse(list(self.root.rglob(".cupidbuild*")))
        return result

    def test_complete_active_fixture_and_equal_timestamp(self):
        shutil.copytree(ROOT / "test_iso/fixtures", self.root / "fixtures", dirs_exist_ok=True)
        (self.root / "fixtures/big.bin").write_bytes(bytes(range(256)) * 16)
        rows = [(p.relative_to(self.root / "fixtures").as_posix(), None if p.is_dir() else p.read_bytes())
                for p in (self.root / "fixtures").rglob("*")]
        self.arrange(rows, (ROOT / "test_iso/fixtures.manifest").read_bytes())
        self.run_case(rows)
        os.utime(self.output, ns=(1600000000123456789,) * 2)
        stamp = self.output.stat().st_mtime_ns
        self.run_case(rows, changed=0)
        self.assertEqual(self.output.stat().st_mtime_ns, stamp)

    def test_absent_output_empty_manifest_rejection_and_recovery(self):
        self.arrange([])
        self.output.unlink()
        rejected = subprocess.run(self.command(), capture_output=True, timeout=600)
        self.assertEqual(rejected.returncode, 1, rejected.stdout + rejected.stderr)
        self.assertIn(b"empty", rejected.stderr)
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.rglob(".cupidbuild*")))
        rows = [("empty", None)]
        self.arrange(rows)
        self.run_case(rows)
        self.run_case(rows, changed=0)

    def test_empty_members_order_and_crlf(self):
        rows = [("d", None), ("d/f", b"payload"), ("empty", b""), ("unused", None)]
        self.arrange(rows, b"unused\r\nempty\r\nd/f\r\nd")
        self.run_case(rows)

    def test_full_512_file_inventory(self):
        rows = [(f"file-{i:03d}", bytes([i % 256])) for i in range(512)]
        self.arrange(rows)
        self.run_case(rows)

    def test_full_large_request_maximum_name_and_depth(self):
        rows = [(name.decode(), data) for name, data in bounds.large_rows()]
        self.arrange(rows)
        self.run_case(rows)

    def test_unicode_and_long_host_paths(self):
        self.paths[0] = "documents/caf\u00e9 \U0001f600.manifest"
        self.paths[1] = "data/\u8cc7\u6599 \U0001f600/" + "a" * 100 + "/" + "b" * 100
        rows = [("directory", None), ("directory/payload", b"utf8")]
        self.arrange(rows)
        self.run_case(rows)

    def test_useful_rejections_preserve_output_and_recover(self):
        self.arrange([("payload", b"content")])
        original = (self.root / self.paths[0]).read_bytes()
        for manifest in (b"missing\n", b"payload\npayload\n", b"../payload\n", b"PAYLOAD\n"):
            with self.subTest(manifest=manifest):
                (self.root / self.paths[0]).write_bytes(manifest)
                self.run_case(success=False)
        (self.root / self.paths[0]).write_bytes(original)
        self.run_case([("payload", b"content")])

    def test_exact_memberships_and_authority_corruption(self):
        self.arrange([("payload", b"content")])
        for logical in ("fixtures/extra", "seed/linux/extra", "seed/windows/extra"):
            with self.subTest(extra=logical):
                (self.root / logical).write_bytes(b"unexpected")
                self.run_case(success=False)
                (self.root / logical).unlink()
        for logical in self.paths[3:]:
            with self.subTest(authority=logical):
                path = self.root / logical
                saved = path.read_bytes()
                value = json.loads(saved)
                if "provenance" in value: value["provenance"]["source_revision"] = "e" * 40
                else: value["source_revision"] = "e" * 40
                path.write_bytes(json.dumps(value).encode())
                self.run_case(success=False)
                path.write_bytes(saved)
        self.run_case([("payload", b"content")])

    def test_both_formats_all_roles_digest_and_execution_profile(self):
        self.arrange([("payload", b"content")])
        for host in ("linux", "windows"):
            for artifact in json.loads((self.root / "seed" / host / "manifest.json").read_bytes())["artifacts"]:
                with self.subTest(host=host, role=artifact["name"]):
                    path = self.root / "seed" / host / artifact["file"]
                    data = path.read_bytes(); changed = bytearray(data); changed[0] ^= 1
                    path.write_bytes(changed)
                    self.run_case(success=False)
                    path.write_bytes(data)
        # Matching declared digests cannot authorize an invalid execution image.
        host = "windows" if os.name == "nt" else "linux"
        path = self.root / "seed" / host / ("cupidobj.exe" if os.name == "nt" else "cupidobj.elf")
        data = bytearray(path.read_bytes()); data[0] ^= 1; path.write_bytes(data)
        digest = hashlib.sha256(data).hexdigest()
        for logical in (f"seed/{host}/manifest.json", "seed/release.json"):
            value = json.loads((self.root / logical).read_bytes())
            for row in value["artifacts"]:
                if row["name"] == "cupidobj" and row.get("format", "pe32" if os.name == "nt" else "elf32") == ("pe32" if os.name == "nt" else "elf32"):
                    row["sha256"] = digest
            (self.root / logical).write_bytes(json.dumps(value, sort_keys=True, indent=2).encode() + b"\n")
        if host == "linux":
            path = self.root / "seed/windows/manifest.json"; value = json.loads(path.read_bytes())
            value["provenance"]["plan_seed_manifest_sha256"] = hashlib.sha256((self.root / "seed/linux/manifest.json").read_bytes()).hexdigest()
            path.write_bytes(json.dumps(value, sort_keys=True, indent=2).encode() + b"\n")
        result = self.run_case(success=False)
        self.assertIn(b"execution profile", result.stderr)

    def test_independent_checker_rejects_profile_valid_bad_author(self):
        shutil.copytree(self.bad_cohort, self.root / "seed", dirs_exist_ok=True)
        self.arrange([("payload", b"content")])
        self.run_case(success=False)
        shutil.copytree(self.cohort, self.root / "seed", dirs_exist_ok=True)
        self.run_case([("payload", b"content")])

    def test_output_aliases_and_forbidden_subtrees(self):
        self.arrange([("payload", b"content")])
        os.link(self.root / "fixtures/payload", self.root / "output/alias.iso")
        original = self.paths[2]
        for output in ("output/alias.iso", "fixtures/new.iso", "seed/linux/new.iso", "seed/windows/new.iso", self.paths[0], self.paths[5]):
            with self.subTest(output=output):
                self.paths[2] = output
                self.run_case(success=False)
        self.paths[2] = original
        self.run_case([("payload", b"content")])

    def test_shared_input_identities_are_rejected_and_recover(self):
        rows = [("first", b"content"), ("second", b"content")]
        self.arrange(rows)
        second = self.root / "fixtures/second"
        second.unlink(); os.link(self.root / "fixtures/first", second)
        self.run_case(success=False)
        second.unlink(); second.write_bytes(b"content")
        self.run_case(rows)

    def test_missing_seed_and_invalid_fixture_parent_are_rejected(self):
        rows = [("directory", None), ("directory/child", b"child"), ("payload", b"content")]
        self.arrange(rows)
        directory = self.root / "fixtures/directory"
        (directory / "child").unlink(); directory.rmdir(); directory.write_bytes(b"wrong parent kind")
        self.run_case(success=False)
        directory.unlink(); directory.mkdir(); (directory / "child").write_bytes(b"child")
        for host in ("linux", "windows"):
            directory = self.root / "seed" / host
            row = json.loads((directory / "manifest.json").read_bytes())["artifacts"][0]
            path = directory / row["file"]; saved = path.read_bytes()
            path.unlink(); self.run_case(success=False)
            path.mkdir(); self.run_case(success=False)
            path.rmdir(); path.write_bytes(saved)
        self.run_case(rows)

    def test_linked_fixture_directory_is_rejected(self):
        self.arrange([("empty", None)])
        target = self.root / "elsewhere"; target.mkdir()
        linked = self.root / "fixtures/empty"; linked.rmdir()
        if os.name == "nt":
            result = subprocess.run(["cmd", "/c", "mklink", "/J", str(linked), str(target)],
                                    capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            linked.symlink_to(target, target_is_directory=True)
        self.run_case(success=False)
        if os.name == "nt": linked.rmdir()
        else: linked.unlink()
        linked.mkdir(); self.run_case([("empty", None)])

    def test_diagnostics_null_arguments_and_unsafe_paths(self):
        self.arrange([("payload", b"content")])
        for mode in ("null-request", "null-result", "null-error"):
            self.run_case(success=False, mode=mode)
        original = self.paths[0]
        for logical in ("", "../fixtures.manifest", "./fixtures.manifest", "/fixtures.manifest", "x//y", "x\\y", "x:y", "x/"):
            self.paths[0] = logical
            self.run_case(success=False, mode="cap1")
        self.paths[0] = original
        self.run_case([("payload", b"content")], mode="cap0")

    def race(self, phase, mutation, existing=True):
        if os.environ.get("CUPIDBUILD_ISO_PUBLICATION_NO_RACES"):
            self.skipTest("rename fault injection uses the native host adapter")
        rows = [("empty", None), ("payload", b"content")]
        self.arrange(rows)
        if not existing:
            self.output.unlink()
        ready, resume = self.root / "ready", self.root / "resume"
        environment = dict(os.environ, CUPIDBUILD_PUBLICATION_TEST_PHASE=phase,
            CUPIDBUILD_PUBLICATION_TEST_READY=str(ready), CUPIDBUILD_PUBLICATION_TEST_RESUME=str(resume))
        process = subprocess.Popen(self.command(), stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=environment)
        try:
            deadline = time.monotonic() + 90
            while not ready.exists() and process.poll() is None and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertTrue(ready.exists(), "publication boundary not reached")
            undo = mutation()
            resume.write_bytes(b"continue")
            stdout, stderr = process.communicate(timeout=600)
            self.assertEqual(process.returncode, 1, stdout + stderr)
            if existing:
                self.assertEqual((self.output.read_bytes(), self.output.stat().st_mtime_ns), self.before)
            else:
                self.assertFalse(self.output.exists())
            self.assertFalse(list(self.root.rglob(".cupidbuild*")))
            undo(); ready.unlink(); resume.unlink()
            self.run_case(rows)
        finally:
            if process.poll() is None: process.kill(); process.communicate(timeout=10)
            for stream in (process.stdout, process.stderr): stream.close()

    def test_directory_drift_before_launch_and_at_both_rename_boundaries(self):
        for phase in ("before-tool-launch", "before-mutation", "after-install"):
            with self.subTest(phase=phase):
                def mutate():
                    added = self.root / "fixtures/empty/extra"
                    added.write_bytes(b"drift")
                    return added.unlink
                self.race(phase, mutate)
                # Each subcase needs its original prior-output state.
                self.output.write_bytes(self.before[0]); os.utime(self.output, ns=(self.before[1],) * 2)

    def test_same_size_payload_edit_with_restored_timestamp_after_install(self):
        def mutate():
            path = self.root / "fixtures/payload"; stat = path.stat()
            path.write_bytes(b"changed"); os.utime(path, ns=(stat.st_atime_ns, stat.st_mtime_ns))
            def undo():
                path.write_bytes(b"content"); os.utime(path, ns=(stat.st_atime_ns, stat.st_mtime_ns))
            return undo
        self.race("after-install", mutate)

    def test_seed_authority_and_opposite_cohort_drift_after_install(self):
        opposite = "linux/cupidc.elf" if os.name == "nt" else "windows/cupidc.exe"
        for logical in ("release.json", opposite):
            with self.subTest(logical=logical):
                def mutate():
                    path = self.root / "seed" / logical
                    saved, stat = path.read_bytes(), path.stat()
                    data = bytearray(saved); data[-1] ^= 1
                    path.write_bytes(data); os.utime(path, ns=(stat.st_atime_ns, stat.st_mtime_ns))
                    def undo():
                        path.write_bytes(saved); os.utime(path, ns=(stat.st_atime_ns, stat.st_mtime_ns))
                    return undo
                self.race("after-install", mutate)
                self.output.write_bytes(self.before[0]); os.utime(self.output, ns=(self.before[1],) * 2)

    def test_absent_output_after_install_drift_restores_absence(self):
        def mutate():
            path = self.root / "fixtures/empty/extra"
            path.write_bytes(b"added after install")
            return path.unlink
        self.race("after-install", mutate, existing=False)
