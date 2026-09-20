"""Replay every guarded kernel compile through Make and compare retained bytes."""
import argparse
import collections
import hashlib
import json
import os
import signal
import shutil
import filecmp
from pathlib import Path
import shlex
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools import build_graph_audit as audit
from tools.cupidc_kernel_compile import FROZEN_KERNEL_INPUT_CLOSURES

POISON = "__cupid_kernel_validation_forbidden_command__"
COMMANDS = (
    "PYTHON", "CC", "CXX", "CPP", "HOSTCC", "HOSTCXX", "ASM", "AS",
    "LD", "AR", "NM", "OBJCOPY", "CHECKED_SEED_RUN", "CUPIDC_KERNEL_COMPILE",
    "CUPIDC_PRODUCTION_COMPILE",
)
PROFILE = "build/bootstrap/doom-cupidc-inputs.json"
GENERATED = "kernel/cpu/ksyms_data.cc"
EVIDENCE = {"status": "started", "phase": "arguments"}
REPORT = None
PROCESS_TERMINATION_GRACE_SECONDS = 5.0


def save_evidence(**updates):
    EVIDENCE.update(updates)
    if REPORT is not None:
        temporary = REPORT / "result.json.tmp"
        temporary.write_text(json.dumps(EVIDENCE, indent=2) + "\n", encoding="utf-8")
        temporary.replace(REPORT / "result.json")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def run(command, root, log, timeout):
    options = ({"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP}
               if os.name == "nt" else {"start_new_session": True})
    with log.open("wb") as output:
        process = subprocess.Popen(command, cwd=root, stdout=output,
                                   stderr=subprocess.STDOUT, **options)
        try:
            returncode = process.wait(timeout=timeout)
        except (subprocess.TimeoutExpired, KeyboardInterrupt):
            if os.name == "nt":
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                               stdout=output, stderr=subprocess.STDOUT, timeout=30)
            else:
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                # Retain the unreaped leader until after the final group kill,
                # so its process ID cannot be reused during the grace period.
                time.sleep(PROCESS_TERMINATION_GRACE_SECONDS)
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            process.wait(timeout=30)
            raise
    if returncode:
        raise RuntimeError(f"command failed ({returncode}); see {log}")
    return log.read_text(encoding="utf-8", errors="replace")


def file_digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def compile_rows(text):
    rows = []
    for line in text.replace("\\\n", " ").splitlines():
        if " compile-kernel " not in line:
            continue
        tokens = shlex.split(line)
        rows.append((tokens[tokens.index("--source") + 1],
                     tokens[tokens.index("--output") + 1]))
    return collections.Counter(rows)


def profile_rows(text):
    rows = []
    for line in text.replace("\\\n", " ").splitlines():
        if " generate-profile-manifest " in line:
            tokens = shlex.split(line)
            rows.append(tuple(tokens))
    return collections.Counter(rows)


def check_census(text, expected_compiles, expected_profiles):
    if POISON in text:
        raise RuntimeError("a forbidden command remains reachable; inspect the phase log")
    if compile_rows(text) != expected_compiles or profile_rows(text) != expected_profiles:
        raise RuntimeError("command cohort differs from the exact transactions for this phase")


def check_replay_plan(text, expected_compiles, expected_profile):
    profiles = profile_rows(text)
    if compile_rows(text) != expected_compiles or (profiles and profiles != expected_profile):
        raise RuntimeError("dry-run census differs from the allowed compile/profile transactions")
    # Make -n cannot observe a transaction retaining an equal output's mtime.
    # Keep predicted wrapper calls as evidence; actual execution stays poisoned.
    predicted_forbidden = [line for line in text.splitlines() if POISON in line]
    return profiles, predicted_forbidden


def validation_commands(common, overlay, sources, targets, jobs):
    guarded = [*common, "-o", "FORCE", "-j", str(jobs),
               *[f"{name}={POISON}" for name in COMMANDS]]
    profile = [*guarded, "-f", str(overlay), PROFILE]
    replay = [*guarded, *[item for source in sources for item in ("-W", source)], *targets]
    return profile, replay


def compare_controls(root, control_bytes, control_mtimes):
    for name, data in control_bytes.items():
        path = root / name
        if path.is_symlink() or path.read_bytes() != data:
            raise RuntimeError(f"input bytes changed: {name}")
        if path.stat().st_mtime_ns != control_mtimes[name]:
            raise RuntimeError(f"input timestamp changed: {name}")


def compare_artifacts(root, report, before, sources_by_output):
    artifact_rows, rows = [], []
    filecmp.clear_cache()
    for output, baseline in before.items():
        path = root / output
        if path.is_symlink() or not path.is_file():
            raise RuntimeError(f"artifact must remain a regular file: {output}")
        if output in sources_by_output:
            data, mtime_ns = compare_object(path, report / "before" / output, baseline["mtime_ns"])
            rows.append({"source": sources_by_output[output], "output": output, "size": len(data),
                         "sha256": digest(data), "before_mtime_ns": baseline["mtime_ns"],
                         "after_mtime_ns": mtime_ns, "byte_equal": True, "timestamp_equal": True})
        else:
            if not filecmp.cmp(path, report / "before" / output, shallow=False):
                raise RuntimeError(f"artifact bytes changed: {output}")
            mtime_ns = path.stat().st_mtime_ns
            if mtime_ns != baseline["mtime_ns"]:
                raise RuntimeError(f"artifact timestamp changed: {output}")
        artifact_rows.append({"output": output, **baseline,
                              "after_mtime_ns": mtime_ns, "byte_equal": True, "timestamp_equal": True})
    return artifact_rows, rows


def compare_object(path, retained, expected_mtime_ns):
    data = path.read_bytes()
    if data != retained.read_bytes():
        raise RuntimeError(f"object bytes changed: {path}")
    actual_mtime_ns = path.stat().st_mtime_ns
    if actual_mtime_ns != expected_mtime_ns:
        raise RuntimeError(
            f"object timestamp changed: {path}: "
            f"before={expected_mtime_ns}, after={actual_mtime_ns}"
        )
    return data, actual_mtime_ns


def make_configuration(root, make):
    return audit._read_evaluated_make_variables(
        root, make,
        ("PRODUCTION_SEED_INPUTS", "PRODUCTION_SEED_MANIFEST",
         "PRODUCTION_SEED_DIRECTORY", "PRODUCTION_SEED_SUFFIX", "DOOM_CUPIDC_HEADERS",
         "BOOTSTRAP_ARTIFACTS", "ARTIFACT_SIZE_OUTPUTS"),
        make_variables=("OS=Windows_NT" if os.name == "nt" else "OS=Linux",),
    )


def main():
    global REPORT
    parser = argparse.ArgumentParser(
        description=__doc__,
        epilog=("Run after seed promotion and Make adoption. Example: "
                "python tools/validate_kernel_compile_handoff.py --prepare-target all "
                "--jobs 4 --report build/bootstrap/kernel-handoff-validation. "
                "Add --wrapper-oracle for a separate compile through the Python coordinator. "
                "This optional proof is not a normal-build dependency."),
    )
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--make", default="make")
    parser.add_argument("--prepare-target", choices=("all", "kernel/cpu/ksyms_data.o"), default="all",
                        help="normal build to establish before replay (default: all)")
    parser.add_argument("--jobs", type=int, default=2, help="Make jobs, 1..4 (default: 2)")
    parser.add_argument("--phase-timeout", type=int, default=7200,
                        help="maximum seconds per build or oracle phase (default: 7200)")
    parser.add_argument("--report", type=Path, required=True,
                        help="new report directory inside the repository's build/bootstrap")
    parser.add_argument("--plan-only", action="store_true", help="write the command plan without running builds")
    parser.add_argument("--wrapper-oracle", action="store_true",
                        help="also compare all objects with the Python coordinator using the same seed")
    args = parser.parse_args()
    root = args.root.resolve(strict=True)
    report = args.report.resolve()
    if report.exists():
        raise RuntimeError("report directory must be new so earlier evidence stays intact")
    if args.jobs not in range(1, 5) or args.phase_timeout <= 0:
        raise RuntimeError("jobs must be 1..4 and the phase timeout must be positive")
    if not report.is_relative_to(root / "build" / "bootstrap"):
        raise RuntimeError("report directory must be under build/bootstrap")
    report.mkdir(parents=True)
    REPORT = report
    sources = sorted(FROZEN_KERNEL_INPUT_CLOSURES)
    if len(sources) != 157:
        raise RuntimeError("expected the complete 157-source kernel cohort")
    expected = collections.Counter((source, Path(source).with_suffix(".o").as_posix())
                                   for source in sources)
    # Only the empty, phony global rebuild trigger is ignored. Every real
    # source, header, seed, generated file, and object keeps its Make edges.
    overlay = report / "profile-check.mk"
    overlay.write_text(
        ".PHONY: __cupid_validation_profile_check\n"
        "__cupid_validation_profile_check:\n"
        f"{PROFILE}: __cupid_validation_profile_check\n", encoding="utf-8")
    makefile = root / "Makefile"
    common = [args.make, "OS=Windows_NT" if os.name == "nt" else "OS=Linux",
              "--no-print-directory", "-f", str(makefile)]
    targets = [output for source, output in expected]
    profile_check, replay = validation_commands(common, overlay, sources, targets, args.jobs)
    result = {"status": "started", "sources": len(sources), "commands_poisoned": COMMANDS,
              "preparation": [*common, "-j", str(args.jobs), args.prepare_target],
              "profile_check": profile_check, "replay": replay, "ignored_make_targets": ["FORCE"],
              "profile_rechecked": False}
    (report / "plan.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    save_evidence(**result, phase="plan")
    if args.plan_only:
        save_evidence(status="plan-only")
        print(f"Plan written to {report / 'plan.json'}; no build was run")
        return 0
    # This establishes Doom, generated installation objects, pass-one ELF,
    # symbols, profile discovery, and their existing verification inputs.
    save_evidence(phase="preparation")
    run(result["preparation"], root, report / "prepare.log", args.phase_timeout)
    save_evidence(phase="snapshot", preparation_passed=True)
    controls = {"Makefile", "link.ld", PROFILE, GENERATED}
    for source, headers in FROZEN_KERNEL_INPUT_CLOSURES.items():
        controls.update((source, *headers))
    values = make_configuration(root, args.make)
    controls.update(values["PRODUCTION_SEED_INPUTS"].split())
    controls.update(values["DOOM_CUPIDC_HEADERS"].split())
    control_bytes = {name: (root / name).read_bytes() for name in sorted(controls)}
    control_mtimes = {name: (root / name).stat().st_mtime_ns for name in control_bytes}
    compare_controls(root, control_bytes, control_mtimes)
    save_evidence(controls={name: digest(data) for name, data in control_bytes.items()},
                  control_mtimes_ns=control_mtimes)
    artifacts = sorted(set(targets) | (
        set(values["BOOTSTRAP_ARTIFACTS"].split()) | set(values["ARTIFACT_SIZE_OUTPUTS"].split())
        if args.prepare_target == "all" else set()
    ))
    before = {}
    for output in artifacts:
        path = root / output
        if not path.is_file() or path.is_symlink():
            raise RuntimeError(f"artifact must be a regular file: {output}")
        saved = report / "before" / output
        saved.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, saved)
        if not filecmp.cmp(path, saved, shallow=False):
            raise RuntimeError(f"artifact changed while copying baseline: {output}")
        before[output] = {"size": path.stat().st_size, "sha256": file_digest(saved),
                          "mtime_ns": path.stat().st_mtime_ns}
    (report / "before.json").write_text(json.dumps(before, indent=2) + "\n", encoding="utf-8")
    if args.wrapper_oracle:
        save_evidence(phase="wrapper-oracle")
        oracle_deadline = time.monotonic() + args.phase_timeout
        for index, source in enumerate(sources):
            remaining = oracle_deadline - time.monotonic()
            if remaining <= 0:
                raise RuntimeError("wrapper oracle exceeded its phase deadline")
            output = report / "oracle" / Path(source).with_suffix(".o")
            output.parent.mkdir(parents=True, exist_ok=True)
            run([sys.executable, "tools/cupidc_kernel_compile.py", "--root", str(root),
                 "--manifest", values["PRODUCTION_SEED_MANIFEST"], "--source", source,
                 "--output", str(output)], root, report / f"oracle-{index:03d}.log", min(660, remaining))
            if output.read_bytes() != (report / "before" / Path(source).with_suffix(".o")).read_bytes():
                raise RuntimeError(f"wrapper oracle differs for {source}")
    # A forced profile rule makes Make -n predict that its content dependents
    # will rebuild, even when the real transaction retains equal bytes/mtime.
    # Execute that rule separately, then traverse the unchanged original graph.
    # A newer real prerequisite may require it again: retaining the output's
    # timestamp deliberately leaves that relationship intact.
    expected_profile = collections.Counter({(
        values["PRODUCTION_SEED_DIRECTORY"] + "cupidbuild." + values["PRODUCTION_SEED_SUFFIX"],
        "generate-profile-manifest", "--seed-manifest", values["PRODUCTION_SEED_MANIFEST"],
        "--root", root.as_posix(), "--output", PROFILE,
    ): 1})
    sources_by_output = {output: source for source, output in expected}
    compare_controls(root, control_bytes, control_mtimes)
    save_evidence(phase="profile-dry-run")
    profile_plan = run([*profile_check[:1], "-n", *profile_check[1:]],
                       root, report / "profile-dry-run.log", 120)
    check_census(profile_plan, collections.Counter(), expected_profile)
    compare_controls(root, control_bytes, control_mtimes)
    save_evidence(phase="profile-replay")
    profile_executed = run(profile_check, root, report / "profile-replay.log", args.phase_timeout)
    check_census(profile_executed, collections.Counter(), expected_profile)
    compare_controls(root, control_bytes, control_mtimes)
    profile_artifacts, _ = compare_artifacts(root, report, before, sources_by_output)
    save_evidence(profile_rechecked=True, profile_command_count=sum(profile_rows(profile_executed).values()),
                  profile_artifacts=profile_artifacts)
    save_evidence(phase="dry-run")
    plan = run([*replay[:1], "-n", *replay[1:]], root, report / "dry-run.log", 120)
    replay_profiles, predicted_forbidden = check_replay_plan(plan, expected, expected_profile)
    save_evidence(dry_run_advisory=True, planned_compile_count=sum(compile_rows(plan).values()),
                  planned_profile_count=sum(replay_profiles.values()),
                  predicted_forbidden_commands=predicted_forbidden)
    compare_controls(root, control_bytes, control_mtimes)
    started = time.monotonic()
    save_evidence(phase="poisoned-replay")
    executed = run(replay, root, report / "replay.log", args.phase_timeout)
    check_census(executed, expected, replay_profiles)
    save_evidence(phase="comparison", replay_seconds=time.monotonic() - started,
                  executed_compile_count=sum(compile_rows(executed).values()),
                  executed_profile_count=sum(profile_rows(executed).values()),
                  executed_forbidden_commands=[])
    artifact_rows, rows = compare_artifacts(root, report, before, sources_by_output)
    compare_controls(root, control_bytes, control_mtimes)
    save_evidence(status="pass", phase="complete", objects=rows, artifacts=artifact_rows,
                  wrapper_oracle=args.wrapper_oracle,
                  controls={name: digest(data) for name, data in control_bytes.items()})
    print(f"157 guarded Make compiles passed; every object retains its bytes and timestamp: {report / 'result.json'}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, IndexError, audit.AuditError,
            subprocess.TimeoutExpired, KeyboardInterrupt) as error:
        save_evidence(status="fail", error=f"{type(error).__name__}: {error}")
        print(f"kernel handoff validation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
