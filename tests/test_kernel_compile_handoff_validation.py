import collections
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import shutil
import tempfile
import sys
import time
import unittest
from unittest import mock

from tools import validate_kernel_compile_handoff as validation


class KernelCompileHandoffValidationTests(unittest.TestCase):
    def test_profile_census_rejects_wrong_bindings_duplicates_and_poison(self):
        command = 'seed/cupidbuild generate-profile-manifest --seed-manifest seed/manifest.json --root "work tree" --output profile.json\n'
        expected = validation.profile_rows(command)
        validation.check_census(command, collections.Counter(), expected)
        for incorrect in (command * 2, command.replace("profile.json", "other.json"),
                          command + validation.POISON, ""):
            with self.subTest(command=incorrect), self.assertRaises(RuntimeError):
                validation.check_census(incorrect, collections.Counter(), expected)

    @unittest.skipUnless(shutil.which("make"), "GNU Make required")
    def test_split_profile_check_avoids_dry_run_phantom_rebuild(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            # The first target orders profile discovery; a later target uses
            # the profile as a content prerequisite, as the Doom rules do.
            makefile = root / "Makefile"
            profile = root / validation.PROFILE
            profile.parent.mkdir(parents=True)
            fixture = root / "transaction.py"
            fixture.write_text("# Successful transactions retain equal files.\n", encoding="utf-8")
            transaction = f'"{Path(sys.executable).as_posix()}" transaction.py'
            makefile.write_text(
                ".PHONY: FORCE\nFORCE:\n"
                f"{validation.PROFILE}: header.h FORCE\n"
                f"\t{transaction} generate-profile-manifest --seed-manifest seed.json --root . --output {validation.PROFILE}\n"
                f"first.o: first.cc FORCE | {validation.PROFILE}\n"
                f"\t{transaction} compile-kernel --source first.cc --output first.o\n"
                f"doom.o: doom.cc {validation.PROFILE} FORCE\n"
                "\t$(PYTHON) forbidden-wrapper.py\n"
                "symbols.o: symbols.cc first.o doom.o FORCE\n"
                f"\t{transaction} compile-kernel --source symbols.cc --output symbols.o\n",
                encoding="utf-8",
            )
            names = ["header.h", "first.cc", "doom.cc", "symbols.cc", validation.PROFILE,
                     "first.o", "doom.o", "symbols.o"]
            baseline_time = time.time_ns() - 20_000_000_000
            for index, name in enumerate(names):
                path = root / name
                path.write_bytes(name.encode())
                stamp = baseline_time + index * 1_000_000_000
                os.utime(path, ns=(stamp, stamp))
            baseline = {name: ((root / name).read_bytes(), (root / name).stat().st_mtime_ns)
                        for name in names}
            overlay = root / "profile-check.mk"
            overlay.write_text(".PHONY: profile_check\nprofile_check:\n"
                               f"{validation.PROFILE}: profile_check\n", encoding="utf-8")
            common = ["make", "--no-print-directory", "-f", makefile.as_posix()]
            profile_command, replay = validation.validation_commands(
                common, overlay.as_posix(), ["first.cc", "symbols.cc"], ["first.o", "symbols.o"], 1)
            combined = validation.run([*replay[:1], "-n", *replay[1:], "-f", overlay.as_posix()],
                                      root, root / "combined.log", 20)
            self.assertIn(validation.POISON, combined)
            profile_plan = validation.run([*profile_command[:1], "-n", *profile_command[1:]],
                                          root, root / "profile-plan.log", 20)
            self.assertNotIn(validation.POISON, profile_plan)
            self.assertEqual(sum(validation.profile_rows(profile_plan).values()), 1)
            self.assertFalse(validation.compile_rows(profile_plan))
            profile_execution = validation.run(profile_command, root, root / "profile.log", 20)
            validation.check_census(profile_execution, collections.Counter(), validation.profile_rows(profile_plan))
            expected = collections.Counter({("first.cc", "first.o"): 1, ("symbols.cc", "symbols.o"): 1})
            plan = validation.run([*replay[:1], "-n", *replay[1:]], root, root / "plan.log", 20)
            validation.check_census(plan, expected, collections.Counter())
            execution = validation.run(replay, root, root / "replay.log", 20)
            validation.check_census(execution, expected, collections.Counter())
            for name, (data, stamp) in baseline.items():
                self.assertEqual((root / name).read_bytes(), data)
                self.assertEqual((root / name).stat().st_mtime_ns, stamp)
            # An unchanged profile stays older than a newly installed input.
            # The original graph must rerun it without inventing a new mtime.
            newer_header = baseline[validation.PROFILE][1] + 3_000_000_000
            os.utime(root / "header.h", ns=(newer_header, newer_header))
            baseline["header.h"] = (baseline["header.h"][0], newer_header)
            stale_profile_plan = validation.run([*replay[:1], "-n", *replay[1:]],
                                                root, root / "stale-profile-plan.log", 20)
            profiles, predicted = validation.check_replay_plan(
                stale_profile_plan, expected, validation.profile_rows(profile_plan))
            self.assertEqual(sum(profiles.values()), 1)
            self.assertEqual(len(predicted), 1)
            stale_profile_execution = validation.run(replay, root, root / "stale-profile-replay.log", 20)
            validation.check_census(stale_profile_execution, expected, profiles)
            for name, (data, stamp) in baseline.items():
                self.assertEqual((root / name).read_bytes(), data)
                self.assertEqual((root / name).stat().st_mtime_ns, stamp)
            # A real changed dependency must still be reachable in the replay.
            (root / "doom.cc").write_bytes(b"changed Doom input")
            changed_plan = validation.run([*replay[:1], "-n", *replay[1:]],
                                          root, root / "changed-plan.log", 20)
            with self.assertRaisesRegex(RuntimeError, "forbidden command"):
                validation.check_census(changed_plan, expected, collections.Counter())
            # The advisory plan may contain this call, but real execution fails.
            validation.check_replay_plan(changed_plan, expected, validation.profile_rows(profile_plan))
            with self.assertRaisesRegex(RuntimeError, "command failed"):
                validation.run(replay, root, root / "changed-replay.log", 20)
            self.assertIn(validation.POISON, (root / "changed-replay.log").read_text())

    def test_advisory_plan_rejects_wrong_profile_and_compile_censuses(self):
        profile = 'seed/cupidbuild generate-profile-manifest --seed-manifest seed.json --root . --output profile.json\n'
        compile_command = 'seed/cupidbuild compile-kernel --source first.cc --output first.o\n'
        expected = validation.compile_rows(compile_command)
        expected_profile = validation.profile_rows(profile)
        for incorrect in (compile_command * 2, profile, compile_command + profile * 2,
                          compile_command + profile.replace("seed.json", "wrong.json")):
            with self.subTest(command=incorrect), self.assertRaisesRegex(RuntimeError, "dry-run census"):
                validation.check_replay_plan(incorrect, expected, expected_profile)
        self.assertEqual(validation.check_replay_plan(compile_command, expected, expected_profile),
                         (collections.Counter(), []))

    def test_control_comparison_rejects_profile_timestamp_and_header_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / "profile.json"
            path.write_bytes(b"profile")
            stamp = path.stat().st_mtime_ns
            validation.compare_controls(root, {"profile.json": b"profile"}, {"profile.json": stamp})
            os.utime(path, ns=(stamp + 2_000_000_000, stamp + 2_000_000_000))
            with self.assertRaisesRegex(RuntimeError, "input timestamp changed"):
                validation.compare_controls(root, {"profile.json": b"profile"}, {"profile.json": stamp})
            path.write_bytes(b"changed")
            os.utime(path, ns=(stamp, stamp))
            with self.assertRaisesRegex(RuntimeError, "input bytes changed"):
                validation.compare_controls(root, {"profile.json": b"profile"}, {"profile.json": stamp})

    def test_command_census_preserves_bindings_and_duplicate_counts(self):
        command = (
            'seed/cupidbuild.exe compile-kernel \\\n'
            ' --seed-manifest seed/manifest.json --root "C:/work tree" \\\n'
            ' --source kernel/core/string.cc --output kernel/core/string.o\n'
        )
        self.assertEqual(
            validation.compile_rows(command * 2),
            collections.Counter({("kernel/core/string.cc", "kernel/core/string.o"): 2}),
        )
        self.assertNotEqual(
            validation.compile_rows(command),
            validation.compile_rows(command.replace("--output kernel/core/string.o", "--output wrong.o")),
        )
        with self.assertRaises(ValueError):
            validation.compile_rows("seed/cupidbuild compile-kernel --source source.cc")

    def test_nonzero_command_keeps_its_log_and_reports_status(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            process = mock.Mock()
            process.wait.return_value = 7
            with mock.patch.object(validation.subprocess, "Popen", return_value=process):
                with self.assertRaisesRegex(RuntimeError, r"failed \(7\).*command.log"):
                    validation.run(["test-command"], root, root / "command.log", 10)
            self.assertTrue((root / "command.log").is_file())

    def test_timeout_terminates_only_the_launched_process_tree(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            process = mock.Mock(pid=24680)
            process.wait.side_effect = [subprocess.TimeoutExpired("test-command", 1), 0, 0]
            with (
                mock.patch.object(validation.subprocess, "Popen", return_value=process) as launch,
                mock.patch.object(validation.subprocess, "run") as terminate_tree,
                mock.patch.object(validation.os, "killpg", create=True) as terminate_group,
                mock.patch.object(validation.time, "sleep"),
                self.assertRaises(subprocess.TimeoutExpired),
            ):
                validation.run(["test-command"], root, root / "timeout.log", 1)
            if os.name == "nt":
                terminate_tree.assert_called_once()
                self.assertEqual(terminate_tree.call_args.args[0],
                                 ["taskkill", "/PID", "24680", "/T", "/F"])
                self.assertEqual(launch.call_args.kwargs["creationflags"],
                                 subprocess.CREATE_NEW_PROCESS_GROUP)
                terminate_group.assert_not_called()
            else:
                self.assertEqual(terminate_group.call_args_list, [
                    mock.call(24680, validation.signal.SIGTERM),
                    mock.call(24680, validation.signal.SIGKILL),
                ])
                self.assertTrue(launch.call_args.kwargs["start_new_session"])
                terminate_tree.assert_not_called()

    @unittest.skipUnless(sys.platform == "linux", "Linux process-group cleanup")
    def test_timeout_kills_child_that_ignores_term_after_leader_exits(self):
        child = None
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            log = root / "timeout.log"
            command = (
                "import os, signal, sys, time\n"
                "reader, writer = os.pipe()\n"
                "child = os.fork()\n"
                "if child == 0:\n"
                "    signal.signal(signal.SIGTERM, signal.SIG_IGN)\n"
                "    os.write(writer, b'ready')\n"
                "    time.sleep(30)\n"
                "    os._exit(0)\n"
                "os.read(reader, 5)\n"
                "print(child, flush=True)\n"
                "signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))\n"
                "time.sleep(30)\n"
            )
            with (
                mock.patch.object(validation, "PROCESS_TERMINATION_GRACE_SECONDS", 0.1),
                self.assertRaises(subprocess.TimeoutExpired),
            ):
                validation.run([sys.executable, "-c", command], root, log, 1)
            child = int(log.read_text(encoding="utf-8").strip())
            stat_path = Path(f"/proc/{child}/stat")
            deadline = time.monotonic() + 2
            while True:
                try:
                    state = stat_path.read_text().rsplit(") ", 1)[1].split()[0]
                except FileNotFoundError:
                    return
                if state in {"Z", "X"}:
                    return
                if time.monotonic() >= deadline:
                    os.kill(child, validation.signal.SIGKILL)
                    self.fail("owned child survived timeout cleanup")
                time.sleep(0.01)

    def test_alternate_makefile_option_is_rejected_before_execution(self):
        with (
            mock.patch.object(sys, "argv", ["validator", "--makefile", "alternate.mk",
                                          "--report", "build/bootstrap/unused"]),
            mock.patch.object(validation, "run") as run,
            contextlib.redirect_stderr(io.StringIO()),
            self.assertRaises(SystemExit) as error,
        ):
            validation.main()
        self.assertEqual(error.exception.code, 2)
        run.assert_not_called()

    def test_configuration_reads_the_native_seed_branch(self):
        with mock.patch.object(validation.audit, "_read_evaluated_make_variables", return_value={}) as read:
            validation.make_configuration(Path("."), "make")
        self.assertEqual(
            read.call_args.kwargs["make_variables"],
            ("OS=Windows_NT" if os.name == "nt" else "OS=Linux",),
        )

    def test_object_comparison_rejects_timestamp_only_changes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output, retained = root / "source.o", root / "before.o"
            output.write_bytes(b"retained object bytes")
            retained.write_bytes(output.read_bytes())
            before = output.stat().st_mtime_ns
            self.assertEqual(validation.compare_object(output, retained, before),
                             (b"retained object bytes", before))
            os.utime(output, ns=(before + 2_000_000_000, before + 2_000_000_000))
            self.assertEqual(output.read_bytes(), retained.read_bytes())
            with self.assertRaisesRegex(RuntimeError, "object timestamp changed"):
                validation.compare_object(output, retained, before)

    def test_object_comparison_rejects_bytes_with_preserved_timestamp(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output, retained = root / "source.o", root / "before.o"
            output.write_bytes(b"old bytes")
            retained.write_bytes(output.read_bytes())
            before = output.stat().st_mtime_ns
            output.write_bytes(b"new bytes")
            os.utime(output, ns=(before, before))
            with self.assertRaisesRegex(RuntimeError, "object bytes changed"):
                validation.compare_object(output, retained, before)

    def test_failure_evidence_keeps_the_active_phase(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with (
                mock.patch.object(validation, "REPORT", root),
                mock.patch.object(validation, "EVIDENCE", {}),
            ):
                validation.save_evidence(status="started", phase="poisoned-replay")
                validation.save_evidence(status="fail", error="expected command failure")
            evidence = json.loads((root / "result.json").read_text(encoding="utf-8"))
            self.assertEqual(evidence, {
                "status": "fail", "phase": "poisoned-replay", "error": "expected command failure",
            })
            self.assertFalse((root / "result.json.tmp").exists())


if __name__ == "__main__":
    unittest.main()
