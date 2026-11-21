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
                requests[b"pass1.job"],
                ("link", guest_plan.job_contents("/home/pass1.elf", "script", "/src/link.ld", *link_order)),
            )
            self.assertEqual(
                requests[b"final.job"],
                ("link", guest_plan.job_contents(
                    "/home/kernel.elf", "script", "/src/link.ld", *link_order, "/home/ksyms_data.o")),
            )
            self.assertEqual(
                requests[b"a0.job"],
                ("assemble", guest_plan.job_contents("/src/kernel/core/isr.asm", link_order[3])),
            )
            self.assertEqual(
                requests[b"source1.job"],
                ("compile", guest_plan.job_contents("/kernel/doom/play.cc", link_order[0], "define doom")),
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
        self.assertEqual(manifest[-2:], [b"disk", request_root + b"disk.job"])
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

            def publish(publisher, root, engine, output, members, *, source_directory):
                self.assertEqual(root, self.root)
                self.assertEqual(members, ("jobs", "jobs/00", "cupidbuild.job", "jobs/00/one.job"))
                self.assertEqual((source_directory / "cupidbuild.job").read_bytes(),
                                 b"compile\n/plan/jobs/00/one.job\n")
                self.assertEqual((source_directory / "jobs/00/one.job").read_bytes(), b"request\n")
                return 0

            with patch.object(guest_plan, "publish_source_iso", side_effect=publish):
                self.assertEqual(guest_plan.publish_plan_iso(
                    scratch / "publisher", self.root, scratch / "p.iso",
                    (("compile", b"one.job", b"request\n"),)), 0)

    def test_duplicate_link_input_fails_before_creating_media(self):
        self.objects.insert(0, self.objects[0])
        with self.assertRaisesRegex(ValueError, "repeats a pass-one object"):
            guest_plan.source_plan(self.root, False)

    def test_unknown_asset_output_fails_before_creating_media(self):
        self.objects.remove("bin/readme.o")
        with self.assertRaisesRegex(ValueError, "unexpected source-plan object bin/readme[.]o"):
            guest_plan.source_plan(self.root, False)


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
