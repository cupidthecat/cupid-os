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
    def test_generated_oracle_obeys_wrapper_binding_and_cleans_private_output(self):
        from tools.cupidc_production_compile import _validate_output_binding
        for fail in (False, True):
            with self.subTest(fail=fail), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve()
                source = "kernel/util/bin_programs_gen.cc"
                parent = root / "kernel/util"
                parent.mkdir(parents=True)
                live = parent / "bin_programs_gen.o"
                live.write_bytes(b"retained object")
                before = (live.read_bytes(), live.stat().st_mtime_ns)
                output = root / "build/bootstrap/report/oracle/bin_programs_gen.o"
                output.parent.mkdir(parents=True)
                seen = []

                def runner(command, working_root, log, timeout):
                    actual = Path(command[command.index("--output") + 1])
                    _validate_output_binding(root, "generated-install", root / source, actual)
                    self.assertEqual(working_root, root)
                    self.assertNotEqual(actual, live)
                    seen.append(actual)
                    actual.write_bytes(b"oracle object")
                    if fail:
                        raise RuntimeError("oracle failed")

                arguments = (root, {"PRODUCTION_SEED_MANIFEST": "seed/manifest.json"},
                             "generated-install", source, output, root / "oracle.log", 60, runner)
                if fail:
                    with self.assertRaisesRegex(RuntimeError, "oracle failed"):
                        validation.run_oracle(*arguments)
                    self.assertFalse(output.exists())
                else:
                    validation.run_oracle(*arguments)
                    self.assertEqual(output.read_bytes(), b"oracle object")
                self.assertEqual(len(seen), 1)
                self.assertFalse(seen[0].parent.exists())
                self.assertEqual(list(parent.iterdir()), [live])
                self.assertEqual((live.read_bytes(), live.stat().st_mtime_ns), before)

    def test_generated_cohort_controls_and_checked_wrapper(self):
        sources, operation = validation.selected_cohort("generated-install")
        self.assertEqual(sources, ["kernel/util/bin_programs_gen.cc",
                                  "kernel/util/demos_programs_gen.cc",
                                  "kernel/util/docs_programs_gen.cc"])
        self.assertEqual(operation, "compile-production")
        values = {"PRODUCTION_SEED_INPUTS": "seed/manifest.json seed/cupidc.exe",
                  "PRODUCTION_SEED_MANIFEST": "seed/manifest.json",
                  "DOOM_CUPIDC_HEADERS": "kernel/doom/doom.h"}
        controls = validation.cohort_controls(Path("."), "generated-install", values)
        self.assertTrue(set(sources).issubset(controls))
        self.assertTrue(set(validation.GENERATED_INCLUDE_CLOSURE).issubset(controls))
        self.assertIn("kernel/fs/homefs.h", controls)
        self.assertIn("tools/cupidc_production_compile.py", controls)
        self.assertIn("seed/manifest.json", controls)
        for source in sources:
            command = validation.oracle_command(Path("root"), values, "generated-install",
                                                source, Path("oracle/output.o"))
            self.assertEqual(command, [sys.executable, "tools/cupidc_production_compile.py",
                             "--root", "root", "--cohort", "generated-install",
                             "--tool-mode", "checked-seed", "--manifest", "seed/manifest.json",
                             "--source", source, "--output", str(Path("oracle/output.o"))])
        with self.assertRaisesRegex(RuntimeError, "outside the approved generated cohort"):
            validation.oracle_command(Path("root"), values, "generated-install",
                                      "user/examples/hello.cc", Path("output.o"))
        with mock.patch.object(validation, "GENERATED_INSTALL_SOURCES", sources[:-1]):
            with self.assertRaisesRegex(RuntimeError, "complete three-source"):
                validation.selected_cohort("generated-install")

    def test_generated_replay_rejects_other_cohorts_and_wrong_commands(self):
        command = ('seed/cupidbuild compile-production --seed-manifest seed/manifest.json '
                   '--root root --source kernel/util/bin_programs_gen.cc '
                   '--output kernel/util/bin_programs_gen.o\n')
        expected = validation.compile_rows(command, "compile-production")
        validation.check_census(command, expected, collections.Counter(), "compile-production")
        for changed in (command * 2, command.replace("bin_programs_gen.o", "other.o"),
                        command + validation.POISON,
                        command + command.replace("compile-production", "compile-kernel"),
                        command + command.replace("compile-production", "compile-doom")):
            with self.subTest(changed=changed), self.assertRaises(RuntimeError):
                validation.check_census(changed, expected, collections.Counter(), "compile-production")
        for other in ("compile-kernel", "compile-doom"):
            with self.assertRaisesRegex(RuntimeError, "outside its cohort"):
                validation.check_replay_plan(command + command.replace("compile-production", other),
                                             expected, collections.Counter(), "compile-production")

    def test_generated_plan_requires_full_preparation_and_three_forced_sources(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            report = root / "build/bootstrap/generated-plan"
            with mock.patch.object(sys, "argv", ["validate", "--root", str(root),
                                   "--report", str(report), "--cohort", "generated-install", "--plan-only"]):
                self.assertEqual(validation.main(), 0)
            plan = json.loads((report / "plan.json").read_text())
            self.assertEqual((plan["sources"], plan["operation"]), (3, "compile-production"))
            self.assertEqual(plan["preparation"][-1], "all")
            self.assertEqual(plan["replay"].count("-W"), 3)
            for source in validation.selected_cohort("generated-install")[0]:
                self.assertIn(source, plan["replay"])
                self.assertIn(Path(source).with_suffix(".o").as_posix(), plan["replay"])
            with mock.patch.object(sys, "argv", ["validate", "--root", str(root),
                                   "--report", str(report), "--cohort", "generated-install",
                                   "--prepare-target", "kernel/cpu/ksyms_data.o", "--plan-only"]):
                with self.assertRaisesRegex(RuntimeError, "complete normal all preparation"):
                    validation.main()

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
            expected = validation.compile_rows(f"{transaction} compile-kernel --source first.cc --output first.o\n"
                                               f"{transaction} compile-kernel --source symbols.cc --output symbols.o\n")
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
            collections.Counter({("seed/cupidbuild.exe", "compile-kernel", "--seed-manifest",
                                 "seed/manifest.json", "--root", "C:/work tree", "--source",
                                 "kernel/core/string.cc", "--output", "kernel/core/string.o"): 2}),
        )
        self.assertNotEqual(
            validation.compile_rows(command),
            validation.compile_rows(command.replace("--output kernel/core/string.o", "--output wrong.o")),
        )
        with self.assertRaises(RuntimeError):
            validation.check_census("seed/cupidbuild compile-kernel --source source.cc",
                                    validation.compile_rows(command), collections.Counter())

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


class DoomCompileHandoffValidationTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("make"), "GNU Make required")
    def test_doom_replay_runs_native_rows_and_keeps_real_generated_edges(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixture = root / "transaction.py"
            fixture.write_text("# A successful unchanged transaction preserves files.\n")
            command = f'"{Path(sys.executable).as_posix()}" transaction.py'
            profile_path = root / validation.PROFILE
            profile_path.parent.mkdir(parents=True)
            makefile = root / "Makefile"
            makefile.write_text(
                ".PHONY: FORCE\nFORCE:\n"
                "generated.h: generated.input\n\t$(PYTHON) forbidden-generator.py\n"
                f"{validation.PROFILE}: generated.h FORCE\n"
                f"\t{command} generate-profile-manifest --seed-manifest seed.json --root . --output {validation.PROFILE}\n"
                f"compat.o: compat.cc generated.h {validation.PROFILE} FORCE\n"
                f"\t{command} compile-doom --source compat.cc --output compat.o\n"
                f"tree.o: tree.cc generated.h {validation.PROFILE} FORCE\n"
                f"\t{command} compile-doom --source tree.cc --output tree.o\n")
            names = ["generated.input", "generated.h", "compat.cc", "tree.cc",
                     validation.PROFILE, "compat.o", "tree.o"]
            start = time.time_ns() - 20_000_000_000
            for index, name in enumerate(names):
                path = root / name
                path.write_bytes(name.encode())
                stamp = start + index * 1_000_000_000
                os.utime(path, ns=(stamp, stamp))
            data = {name: (root / name).read_bytes() for name in names}
            stamps = {name: (root / name).stat().st_mtime_ns for name in names}
            overlay = root / "overlay.mk"
            overlay.write_text(".PHONY: profile_check\nprofile_check:\n"
                               f"{validation.PROFILE}: profile_check\n")
            profile, replay = validation.validation_commands(
                ["make", "--no-print-directory", "-f", str(makefile)], overlay,
                ["compat.cc", "tree.cc"], ["compat.o", "tree.o"], 2)
            output = validation.run(profile, root, root / "profile.log", 20)
            expected_profile = validation.profile_rows(output)
            self.assertEqual(sum(expected_profile.values()), 1)
            validation.check_census(output, collections.Counter(), expected_profile, "compile-doom")
            expected = validation.compile_rows(f"{command} compile-doom --source compat.cc --output compat.o\n"
                                               f"{command} compile-doom --source tree.cc --output tree.o\n", "compile-doom")
            output = validation.run(replay, root, root / "replay.log", 20)
            validation.check_census(output, expected, collections.Counter(), "compile-doom")
            validation.compare_controls(root, data, stamps)
            # A real generator edge remains reachable; the replay cannot hide it.
            (root / "generated.input").write_bytes(b"changed generator input")
            with self.assertRaisesRegex(RuntimeError, "command failed"):
                validation.run(replay, root, root / "changed.log", 20)
            self.assertIn(validation.POISON, (root / "changed.log").read_text())

    def test_cohort_selection_preserves_kernel_default_and_complete_doom_set(self):
        kernel, operation = validation.selected_cohort("kernel")
        self.assertEqual(kernel, sorted(validation.FROZEN_KERNEL_INPUT_CLOSURES))
        self.assertEqual((len(kernel), operation), (157, "compile-kernel"))
        doom, operation = validation.selected_cohort("doom")
        self.assertEqual((len(doom), len(set(doom)), operation), (83, 83, "compile-doom"))
        self.assertIn("kernel/doom/i_sound_cupidos.cc", doom)
        self.assertEqual(sum(source.startswith("kernel/doom/src/") for source in doom), 79)
        self.assertEqual(collections.Counter(tuple(validation.oracle_profile_arguments("doom", source))
                                            for source in doom),
                         {("--profile", "doom-compat"): 3, ("--profile", "doom-tree"): 80})
        self.assertEqual(validation.oracle_profile_arguments("kernel", kernel[0]), [])

    def test_missing_duplicate_and_unknown_cohorts_fail(self):
        tree = validation.APPROVED_DOOM_TREE_SOURCES
        for changed in (tree[:-1], (*tree[:-1], validation.APPROVED_DOOM_COMPAT_SOURCES[0])):
            with mock.patch.object(validation, "APPROVED_DOOM_TREE_SOURCES", changed):
                with self.assertRaisesRegex(RuntimeError, "complete, disjoint"):
                    validation.selected_cohort("doom")
        with self.assertRaises(RuntimeError):
            validation.selected_cohort("unapproved")
        with self.assertRaisesRegex(RuntimeError, "outside the approved"):
            validation.oracle_profile_arguments("doom", "kernel/doom/unapproved.cc")

    def test_doom_census_rejects_lost_duplicate_wrong_and_foreign_calls(self):
        command = 'seed/cupidbuild compile-doom --source kernel/doom/dglibc.cc --output kernel/doom/dglibc.o\n'
        expected = collections.Counter({("seed/cupidbuild", "compile-doom", "--source",
                                        "kernel/doom/dglibc.cc", "--output", "kernel/doom/dglibc.o"): 1})
        validation.check_census(command, expected, collections.Counter(), "compile-doom")
        self.assertEqual(validation.check_replay_plan(command, expected, collections.Counter(), "compile-doom"),
                         (collections.Counter(), []))
        for changed in ("", command * 2, command.replace("--output kernel/doom/dglibc.o", "--output wrong.o"),
                        command.replace("compile-doom", "compile-kernel"),
                        command + 'seed/cupidbuild compile-kernel --source kernel/core/string.cc --output kernel/core/string.o\n'):
            with self.subTest(command=changed):
                with self.assertRaises(RuntimeError):
                    validation.check_census(changed, expected, collections.Counter(), "compile-doom")
                with self.assertRaises(RuntimeError):
                    validation.check_replay_plan(changed, expected, collections.Counter(), "compile-doom")
        with self.assertRaisesRegex(RuntimeError, "forbidden command"):
            validation.check_census(command + validation.POISON, expected, collections.Counter(), "compile-doom")

    def test_recursive_control_capture_keeps_all_sources_and_extra_headers(self):
        values = {"PRODUCTION_SEED_INPUTS": "seed/manifest.json seed/cupidbuild.exe",
                  "DOOM_CUPIDC_HEADERS": "kernel/doom/dglibc.h"}
        manifest = {"inputs": [{"path": "kernel/doom/deep/extra.inc"}]}
        with mock.patch.object(validation, "_profile_input_manifest", return_value=manifest) as discover:
            controls = validation.cohort_controls(Path("."), "doom", values)
        discover.assert_called_once_with(Path("."))
        self.assertTrue(set(validation.selected_cohort("doom")[0]).issubset(controls))
        self.assertTrue({"kernel/doom/deep/extra.inc", "kernel/doom/dglibc.h", "seed/manifest.json",
                         "seed/cupidbuild.exe", "Makefile", "link.ld", validation.PROFILE,
                         validation.GENERATED}.issubset(controls))
        with mock.patch.object(validation, "_profile_input_manifest", side_effect=RuntimeError("membership drift")):
            with self.assertRaisesRegex(RuntimeError, "membership drift"):
                validation.cohort_controls(Path("."), "doom", values)
        with mock.patch.object(validation, "_profile_input_manifest") as discover:
            validation.cohort_controls(Path("."), "kernel", values)
        discover.assert_not_called()

    def test_doom_plan_preserves_graph_edges_and_requires_full_preparation(self):
        sources, _ = validation.selected_cohort("doom")
        targets = [Path(source).with_suffix(".o").as_posix() for source in sources]
        profile, replay = validation.validation_commands(["make", "-f", "Makefile"], "overlay.mk",
                                                        sources, targets, 4)
        for command in (profile, replay):
            self.assertEqual([command[index + 1] for index, word in enumerate(command) if word == "-o"],
                             ["FORCE"])
            for name in validation.COMMANDS:
                self.assertIn(f"{name}={validation.POISON}", command)
        self.assertEqual([replay[index + 1] for index, word in enumerate(replay) if word == "-W"], sources)
        self.assertEqual(replay[-83:], targets)
        with (mock.patch.object(sys, "argv", ["validator", "--cohort", "doom", "--prepare-target",
                                              "kernel/cpu/ksyms_data.o", "--report", "unused"]),
              mock.patch.object(validation, "run") as run,
              self.assertRaisesRegex(RuntimeError, "complete normal all")):
            validation.main()
        run.assert_not_called()

    def test_plan_only_defaults_to_kernel_and_explicit_doom_records_83_calls(self):
        for cohort in (None, "doom"):
            with self.subTest(cohort=cohort), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                report = root / "build/bootstrap/evidence"
                arguments = ["validator", "--root", str(root), "--report", str(report), "--plan-only"]
                if cohort:
                    arguments += ["--cohort", cohort]
                with (mock.patch.object(sys, "argv", arguments),
                      mock.patch.object(validation, "REPORT", None),
                      mock.patch.object(validation, "EVIDENCE", {}),
                      mock.patch.object(validation, "run") as run,
                      contextlib.redirect_stdout(io.StringIO())):
                    self.assertEqual(validation.main(), 0)
                run.assert_not_called()
                plan = json.loads((report / "plan.json").read_text())
                self.assertEqual(plan["cohort"], cohort or "kernel")
                self.assertEqual(plan["sources"], 83 if cohort else 157)
                self.assertEqual(plan["operation"], "compile-doom" if cohort else "compile-kernel")
                self.assertEqual(plan["preparation"][-1], "all")
                self.assertEqual(plan["ignored_make_targets"], ["FORCE"])

class ReplayBindingReviewTests(unittest.TestCase):
    def test_each_cohort_binds_the_complete_checked_command(self):
        values = {"PRODUCTION_SEED_DIRECTORY": "seed/", "PRODUCTION_SEED_SUFFIX": "exe",
                  "PRODUCTION_SEED_MANIFEST": "seed/manifest.json"}
        for operation, source in (("compile-kernel", "kernel/core/string.cc"),
                                  ("compile-doom", "kernel/doom/dglibc.cc")):
            with self.subTest(operation=operation):
                root = Path("work tree")
                output = Path(source).with_suffix(".o").as_posix()
                command = (f'seed/cupidbuild.exe {operation} --seed-manifest seed/manifest.json '
                           f'--root "work tree" --source {source} --output {output}\n')
                expected = validation.expected_compile_rows(values, root, [source], operation)
                validation.check_census(command, expected, collections.Counter(), operation)
                validation.check_replay_plan(command, expected, collections.Counter(), operation)
                changes = (
                    "echo " + command,
                    command.replace("seed/cupidbuild.exe", "wrong-tool"),
                    command.replace('"work tree"', '"different root"'),
                    command.replace("seed/manifest.json", "invalid.json"),
                    command.rstrip() + " -DDEBUG=1\n",
                    command.rstrip() + " --timeout 1\n",
                    command.rstrip() + f" --source {source}\n",
                    command.rstrip() + " --seed-manifest seed/manifest.json\n",
                    command.replace(f"--output {output}", f"--output {output} --output {output}"),
                )
                for changed in changes:
                    with self.subTest(command=changed):
                        with self.assertRaises(RuntimeError):
                            validation.check_census(changed, expected, collections.Counter(), operation)
                        with self.assertRaises(RuntimeError):
                            validation.check_replay_plan(changed, expected, collections.Counter(), operation)

    def test_kernel_replay_still_allows_other_cohort_dependency_rows(self):
        command = "seed/cupidbuild compile-kernel --source selected.cc --output selected.o\n"
        dependency = "seed/cupidbuild compile-doom --source dependency.cc --output dependency.o\n"
        expected = validation.compile_rows(command)
        validation.check_census(command + dependency, expected, collections.Counter())
        validation.check_replay_plan(command + dependency, expected, collections.Counter())

    def test_control_boundaries_rediscover_new_headers_and_extra_sources(self):
        repository = Path(validation.audit.__file__).resolve().parents[1]
        inventory = validation._profile_input_manifest(repository)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for item in inventory["inputs"]:
                path = root / item["path"]
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"/* retained header */\n")
            for source in validation.selected_cohort("doom")[0]:
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"int value;\n")
            capture = validation._profile_input_manifest(root)
            retained = root / "control"
            retained.write_bytes(b"unchanged")
            data = {"control": retained.read_bytes()}
            stamps = {"control": retained.stat().st_mtime_ns}
            validation.compare_controls(root, data, stamps, capture)
            for logical in ("kernel/doom/new.h", "kernel/doom/new.inc", "kernel/doom/new.cc"):
                with self.subTest(path=logical):
                    added = root / logical
                    added.write_bytes(b"/* added after compiler publication */\n")
                    with self.assertRaises(RuntimeError):
                        validation.compare_controls(root, data, stamps, capture)
                    added.unlink()
                    validation.compare_controls(root, data, stamps, capture)


class ReplayResidueTests(unittest.TestCase):
    def directories(self, root):
        targets = ["kernel/doom/dglibc.o", "kernel/doom/src/d_items.o", "kernel/doom/src/d_event.o"]
        directories = validation.residue_directories(root, targets)
        for directory in directories:
            directory.mkdir(parents=True, exist_ok=True)
        self.assertEqual(set(directories), {root, root / "kernel/doom", root / "kernel/doom/src",
                                            root / "build/bootstrap"})
        return directories

    def test_each_output_parent_root_and_profile_parent_rejects_new_residue(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directories = self.directories(root)
            retained = root / ".cupidbuild-old-recovery"
            retained.write_bytes(b"retain this existing evidence")
            baseline = validation.residue_snapshot(directories)
            validation.require_residue_unchanged(directories, baseline)
            for directory in directories:
                for name in (".cupidbuild-leaked-candidate", "output.o.cupidbuild.lock"):
                    with self.subTest(directory=directory, name=name):
                        path = directory / name
                        path.write_bytes(b"leaked")
                        with self.assertRaisesRegex(RuntimeError, "private transaction entries changed"):
                            validation.require_residue_unchanged(directories, baseline)
                        self.assertEqual(path.read_bytes(), b"leaked")
                        self.assertEqual(retained.read_bytes(), b"retain this existing evidence")
                        path.unlink()

    def test_existing_recovery_removal_replacement_and_metadata_drift_fail(self):
        for mutation in ("remove", "replace", "timestamp"):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                directories = self.directories(root)
                entry = root / ".cupidbuild-retained"
                entry.write_bytes(b"old evidence")
                baseline = validation.residue_snapshot(directories)
                stamp = entry.stat().st_mtime_ns
                if mutation == "remove":
                    entry.unlink()
                elif mutation == "replace":
                    entry.rename(root / "saved-evidence")
                    entry.write_bytes(b"old evidence")
                    os.utime(entry, ns=(stamp, stamp))
                else:
                    os.utime(entry, ns=(stamp + 2_000_000_000, stamp + 2_000_000_000))
                with self.assertRaisesRegex(RuntimeError, "private transaction entries changed"):
                    validation.require_residue_unchanged(directories, baseline)

    def test_snapshot_does_not_recurse_or_follow_transaction_links(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directories = self.directories(root)
            nested = root / ".cupidbuild-existing-directory"
            nested.mkdir()
            (nested / "child").write_bytes(b"retained child")
            try:
                (root / ".cupidbuild-dangling-link").symlink_to(root / "absent")
            except OSError:
                pass
            iterated = []
            original = Path.iterdir
            def observed(path):
                iterated.append(path)
                return original(path)
            with mock.patch.object(Path, "iterdir", observed):
                snapshot = validation.residue_snapshot(directories)
            self.assertEqual(set(iterated), set(directories))
            self.assertIn(nested.as_posix(), snapshot)
            self.assertFalse(any(name.endswith("/child") for name in snapshot))
            link = root / ".cupidbuild-dangling-link"
            if link.is_symlink():
                self.assertIn(link.as_posix(), snapshot)
                self.assertTrue(validation.stat.S_ISLNK(snapshot[link.as_posix()][2]))

    def test_successful_and_failed_commands_check_residue_after_execution(self):
        for failed in (False, True):
            with self.subTest(failed=failed), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                directories = self.directories(root)
                baseline = validation.residue_snapshot(directories)
                leaked = root / "kernel/doom/src/.cupidbuild-leaked"
                def command(*args):
                    leaked.write_bytes(b"left behind")
                    if failed:
                        raise RuntimeError("tool failed")
                    return "normal output"
                with mock.patch.object(validation, "run", side_effect=command):
                    with self.assertRaisesRegex(RuntimeError, "private transaction entries changed"):
                        validation.run_without_residue(["tool"], root, root / "log", 10,
                                                       directories, baseline)
                self.assertEqual(leaked.read_bytes(), b"left behind")

    def test_boundary_drift_rejects_before_launch_and_unchanged_run_succeeds(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directories = self.directories(root)
            baseline = validation.residue_snapshot(directories)
            with mock.patch.object(validation, "run", return_value="ok") as run:
                self.assertEqual(validation.run_without_residue(["tool"], root, root / "log", 10,
                                                               directories, baseline), "ok")
                run.assert_called_once()
            (root / ".cupidbuild-new").write_bytes(b"unexpected")
            with mock.patch.object(validation, "run") as run:
                with self.assertRaisesRegex(RuntimeError, "private transaction entries changed"):
                    validation.run_without_residue(["tool"], root, root / "log", 10, directories, baseline)
            run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
