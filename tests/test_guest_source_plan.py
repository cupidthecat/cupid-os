## @file
#  @brief Check the source-only guest plan and its output inventory.

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import DEFAULT, patch


GUEST_TOOLS = Path(__file__).resolve().parents[1] / "bootstrap" / "guest"
with patch.object(sys, "path", [str(GUEST_TOOLS), *sys.path]):
    import check_guest_kernel_plan as kernel_plan
    import check_guest_source_plan as guest_plan


class GuestSourcePlanTests(unittest.TestCase):
    def setUp(self):
        self.root = Path("production")
        self.objects = [
            "kernel/doom/play.o",
            "bin/readme.o",
            "kernel/smp/smp_trampoline.o",
            "kernel/core/isr.o",
            "kernel/core/main.o",
            "kernel/util/bin_programs_gen.o",
            "kernel/cpu/ksyms_data.o",
        ]
        self.install = "kernel/util/bin_programs_gen.cc"
        mocks = patch.multiple(
            guest_plan,
            ASSEMBLY_SOURCES={"isr": "kernel/core/isr.asm"},
            SOURCE_PROFILES={
                "kernel/core/main.cc": "kernel",
                "kernel/doom/play.cc": "doom",
            },
            production_objects=lambda root: self.objects,
            production_assets=lambda root: {"bin/readme.o": ("bin/readme.txt", "wrap")},
            installation_jobs=lambda root: {self.install: f"/home/{self.install}\nbin\n".encode("ascii")},
            _kernel_input_paths=lambda root, source, profile: [
                root / source, root / Path(source).with_suffix(".h")
            ],
            profile_options=lambda profile: (f"define {profile}",),
        )
        mocks.start()
        self.addCleanup(mocks.stop)

    def test_source_mounts_and_link_order_match_in_both_output_modes(self):
        persistent, inputs, references = guest_plan.source_plan(self.root, False)
        temporary, temporary_inputs, temporary_references = guest_plan.source_plan(self.root, True)
        self.assertEqual(inputs, temporary_inputs)
        self.assertEqual(inputs[0][0], self.root)
        self.assertIn("kernel/doom", inputs[0][1])
        self.assertNotIn("kernel/doom/play.cc", inputs[0][1])
        self.assertEqual(inputs[1], (self.root / "kernel/doom", ("play.cc", "play.h")))
        self.assertEqual(inputs[2], (self.root, ("bin", "bin/readme.txt")))

        for steps, directory in ((persistent, "/home"), (temporary, "/tmp")):
            requests = {name: (operation, contents) for operation, name, contents in steps}
            link_order = [f"{directory}/i{index:03}.o" for index in range(len(self.objects) - 1)]
            self.assertEqual(
                requests[b"kernel.job"],
                ("kernel", guest_plan.job_contents(*link_order)),
            )
            self.assertEqual(
                requests[b"a0.job"],
                ("assemble", guest_plan.job_contents("/src/kernel/core/isr.asm", link_order[3])),
            )
            self.assertEqual(
                requests[b"source1.job"],
                ("compile-list", guest_plan.job_contents(
                    "define doom", "sources", "/kernel/doom/play.cc", link_order[0])),
            )
            self.assertEqual(
                requests[b"asset0.job"],
                ("asset-list", guest_plan.job_contents("wrap", "/plan/assets", "/bin/readme.txt", link_order[1])),
            )

        self.assertEqual(references["i000.o"], self.root / self.objects[0])
        install_name = Path(self.install).name
        self.assertEqual(references[install_name], self.root / self.install)
        self.assertEqual(set(temporary_references), {"smp.bin", install_name})
        self.assertIn("kernel/cpu/ksyms.h", inputs[0][1])
        self.assertIn("boot/boot.asm", inputs[0][1])
        self.assertIn("link.ld", inputs[0][1])

    def test_missing_source_job_fails_before_creating_media(self):
        self.objects.insert(0, "kernel/missing.o")
        with self.assertRaisesRegex(ValueError, "no guest source job for kernel/missing[.]o"):
            guest_plan.source_plan(self.root, False)

    def test_source_lists_preserve_member_order_and_split_at_the_limit(self):
        extra = ["bin/link.o", "bin/logo.o", "bin/title.o", "kernel/core/helpers.o"]
        self.objects[6:6] = extra
        assets = {
            "bin/readme.o": ("bin/readme.txt", "wrap"),
            "bin/link.o": ("bin/link.txt", "wrap"),
            "bin/logo.o": ("bin/logo.bmp", "wrap"),
            "bin/title.o": ("bin/title.txt", "wrap-text"),
        }
        profiles = {
            "kernel/core/main.cc": "kernel",
            "kernel/core/helpers.cc": "kernel",
            "kernel/doom/play.cc": "doom",
        }
        with patch.multiple(guest_plan, OBJECT_LIST_LIMIT=2, SOURCE_PROFILES=profiles,
                            production_assets=lambda root: assets):
            steps, _, _ = guest_plan.source_plan(self.root, True)
        jobs = {name: contents for _, name, contents in steps}
        self.assertEqual(jobs[b"asset0.job"], guest_plan.job_contents(
            "wrap", "/plan/assets", "/bin/readme.txt", "/tmp/i001.o", "/bin/link.txt", "/tmp/i006.o",
        ))
        self.assertEqual(jobs[b"asset1.job"], guest_plan.job_contents(
            "wrap", "/plan/assets", "/bin/logo.bmp", "/tmp/i007.o",
        ))
        self.assertEqual(jobs[b"asset2.job"], guest_plan.job_contents(
            "wrap-text", "/plan/assets", "/bin/title.txt", "/tmp/i008.o",
        ))
        self.assertEqual(jobs[b"source0.job"], guest_plan.job_contents(
            "define kernel", "sources",
            "/kernel/core/main.cc", "/tmp/i004.o", "/kernel/core/helpers.cc", "/tmp/i009.o",
        ))

    def test_guest_manifest_keeps_step_order_and_reads_requests_from_the_source_mount(self):
        steps, _, _ = guest_plan.source_plan(self.root, True)
        files = guest_plan.media_plan_files(steps)
        manifest = files[Path("cupidbuild.job")].splitlines()
        request_root = b"/plan/jobs/00/"
        self.assertEqual(manifest[:2], [b"assemble", request_root + b"a0.job"])
        install = manifest.index(b"install")
        self.assertEqual(manifest[install:install + 4], [
            b"install", request_root + b"i0.job", b"compile", request_root + b"install0.job",
        ])
        self.assertEqual(manifest[-2:], [b"kernel", request_root + b"kernel.job"])
        self.assertEqual(files[Path("jobs/00/i0.job")], b"/home/bin_programs_gen.cc\nbin\n")
        self.assertIn(b"include /src/kernel/fs\n", files[Path("jobs/00/install0.job")])

    def test_media_groups_jobs_without_changing_step_order(self):
        steps = tuple(("compile", f"a{index}.job".encode("ascii"), bytes([index])) for index in range(65))
        files = guest_plan.media_plan_files(steps)
        manifest = files[Path("cupidbuild.job")].splitlines()
        self.assertEqual(manifest[127], b"/plan/jobs/00/a63.job")
        self.assertEqual(manifest[129], b"/plan/jobs/01/a64.job")
        self.assertEqual(files[Path("jobs/01/a64.job")], b"\x40")

    def test_media_rejects_duplicate_or_outside_job_paths(self):
        for names in ((b"same", b"same"), (b"../outside",)):
            with self.subTest(names=names), self.assertRaises(ValueError):
                guest_plan.media_plan_files(tuple(("compile", name, b"") for name in names))

    def test_plan_iso_stages_only_generated_job_files(self):
        with tempfile.TemporaryDirectory() as temp:
            scratch = Path(temp)

            def publish(publisher, root, engine, output, members, *, source_directory, source_overrides):
                self.assertEqual(root, self.root)
                self.assertEqual(members, ("jobs", "jobs/00", "cupidbuild.job", "jobs/00/one.job"))
                self.assertEqual(source_overrides, {})
                self.assertEqual((source_directory / "cupidbuild.job").read_bytes(),
                                 b"compile\n/plan/jobs/00/one.job\n")
                self.assertEqual((source_directory / "jobs/00/one.job").read_bytes(), b"request\n")
                return 0

            with patch.object(guest_plan, "publish_source_iso", side_effect=publish):
                self.assertEqual(guest_plan.publish_plan_iso(
                    scratch / "publisher", self.root, scratch / "p.iso",
                    (("compile", b"one.job", b"request\n"),)), 0)

    def test_production_plan_carries_assets_under_the_plan_mount(self):
        scratch = Path("scratch")
        with patch.object(guest_plan, "replay") as replay:
            guest_plan.check(scratch, self.root, "qemu", 10, True)
        _, _, _, _, steps, mounts, inputs, _ = replay.call_args.args
        self.assertEqual(mounts, (("s.iso", "/src"), ("d.iso", "/src/kernel/doom")))
        self.assertEqual(len(inputs), 2)
        assets = replay.call_args.kwargs["asset_input"]
        self.assertEqual(assets, (self.root, ("bin", "bin/readme.txt")))
        with tempfile.TemporaryDirectory() as temporary:
            with patch.object(guest_plan, "publish_source_iso", return_value=0) as publish:
                guest_plan.publish_plan_iso(Path("publisher"), self.root,
                                            Path(temporary) / "p.iso", steps, assets)
            self.assertEqual(publish.call_args.args[-1][-3:], (
                "assets", "assets/bin", "assets/bin/readme.txt",
            ))
            self.assertEqual(publish.call_args.kwargs["source_overrides"], {
                "assets": self.root,
                "assets/bin": self.root / "bin",
                "assets/bin/readme.txt": self.root / "bin/readme.txt",
            })

    def test_duplicate_link_input_fails_before_creating_media(self):
        self.objects.insert(0, self.objects[0])
        with self.assertRaisesRegex(ValueError, "repeats a pass-one object"):
            guest_plan.source_plan(self.root, False)

    def test_unknown_asset_output_fails_before_creating_media(self):
        self.objects.remove("bin/readme.o")
        with self.assertRaisesRegex(ValueError, "unexpected source-plan object bin/readme[.]o"):
            guest_plan.source_plan(self.root, False)

    def test_link_only_stages_checked_objects_and_one_kernel_request(self):
        scratch = Path("scratch")
        with patch.object(Path, "is_file", return_value=True), patch.object(guest_plan, "replay") as replay:
            guest_plan.check_kernel_job(scratch, self.root, "qemu", 10)
        _, _, _, _, steps, mounts, inputs, references, staged = replay.call_args.args
        self.assertEqual(mounts, (("s.iso", "/src"),))
        self.assertEqual(inputs, ((self.root, guest_plan.iso_members(guest_plan.KERNEL_BUILD_FILES)),))
        self.assertEqual(references, {})
        self.assertEqual(staged, tuple(
            f"{self.root / obj}:/obj0/i{index:03}.o" for index, obj in enumerate(self.objects[:-1])
        ))
        self.assertEqual(steps, (("kernel", b"kernel.job", guest_plan.job_contents(
            *(f"/disk/obj0/i{index:03}.o" for index in range(len(self.objects) - 1))
        )),))

    def test_link_only_rejects_missing_checked_objects_before_staging(self):
        with patch.object(Path, "is_file", lambda path: path.name != "play.o"), \
                patch.object(guest_plan, "replay") as replay:
            with self.assertRaisesRegex(ValueError, "checked pass-one objects are missing: kernel/doom/play.o"):
                guest_plan.check_kernel_job(Path("scratch"), self.root, "qemu", 10)
        replay.assert_not_called()


class GuestMountRetryTests(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.scratch = Path(scratch.name)
        self.mounts = (("s.iso", "/src"), ("a.iso", "/assets"))
        self.markers = [f"VFS: mounted 'iso9660' at '{mount}'" for _, mount in self.mounts]
        self.failure = subprocess.CalledProcessError(1, "gui_terminal_smoke.py")
        self.runners = self.enterContext(patch.multiple(
            kernel_plan, BUILD=self.scratch, run=DEFAULT,
            write_reference_prefix=DEFAULT, read_homefs=DEFAULT,
            compare_homefs_files=DEFAULT, boot_prefix=DEFAULT,
        ))
        self.runners["compare_homefs_files"].return_value = {"disk-prefix.img": b"checked prefix"}

    def execute(self, transcript):
        log = self.scratch / "kernel-source-plan-runtime.log"
        log.write_text(transcript, encoding="utf-8")
        kernel_plan.execute_kernel_plan(
            self.scratch, self.scratch, self.scratch / "build.img", "qemu", 10, {}, self.mounts,
        )

    def test_retry_once_when_a_mount_command_misses_its_marker(self):
        run = self.runners["run"]
        run.side_effect = [self.failure, None]
        self.execute(f"{self.markers[0]}\n[terminal] command complete\n")
        self.assertEqual(run.call_count, 2)
        self.assertEqual(*run.call_args_list)
        self.runners["boot_prefix"].assert_called_once()

    def test_worker_failure_after_mounts_is_reported_without_retry(self):
        run = self.runners["run"]
        run.side_effect = self.failure
        transcript = "\n".join((*self.markers, "[terminal] command complete",
                                "[cupidbuild-guest] stopped at /home/compile.job"))
        with self.assertRaises(subprocess.CalledProcessError):
            self.execute(transcript)
        run.assert_called_once()
        self.runners["boot_prefix"].assert_not_called()
