## @file
#  @brief Check platform behavior evidence and promoted bootstrap reports.
import hashlib
import json
import shutil
import subprocess
import struct
import tempfile
from pathlib import Path
from unittest import mock

from tools.bootstrap_toolchain import (
    BootstrapError,
    CANDIDATE_TOOL_NAMES,
    PROMOTED_LINUX_PLAN_SHA256,
    PROMOTED_SOURCE_REVISION,
    PROMOTED_SEED_SCHEMA,
    PRODUCER_NAMES,
    PROMOTED_WINDOWS_PLAN_SHA256,
    SEED_SOURCE_REVISION,
    SeedInputs,
    Stage,
    TOOL_NAMES,
    ToolRunner,
    _build_plan_sha256,
    _bootstrap_from_frozen_seed,
    _bootstrap_windows_from_frozen_seed,
    _candidate_build_plan,
    _corrupt_candidate_entry_instruction,
    _local_target_executable_payload,
    _retarget_native_windows_behavior_seed,
    _run_behavior_checks,
    _run_native_windows_behavior_checks,
    _unowned_relocation_object_payload,
    _windows_build_plan,
    capture_source_snapshot,
    freeze_seed_inputs,
)

from tests.bootstrap_seed_suite_fixtures import (
    REPO_ROOT,
    SEED_MANIFEST,
    WINDOWS_SEED_MANIFEST,
)


class BootstrapSeedCertificationCases:
    def test_candidate_behavior_checks_run_cupidbuild_on_both_targets(self):
        class BehaviorBoundaryReached(Exception):
            pass

        with tempfile.TemporaryDirectory(
            prefix=".candidate-behavior-inventory-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            tool = root / "tool"
            tool.write_bytes(b"tool")
            stage = Stage(
                objects={},
                tools={name: tool for name in CANDIDATE_TOOL_NAMES},
            )
            artifacts = []
            artifact_bytes = []
            seed_tools = {}
            for name in CANDIDATE_TOOL_NAMES:
                filename = f"{name}.elf"
                payload = f"seed {name}\n".encode("ascii")
                seed_tool = root / filename
                seed_tool.write_bytes(payload)
                artifacts.append(
                    {
                        "file": filename,
                        "name": name,
                        "producer": name in PRODUCER_NAMES,
                        "sha256": hashlib.sha256(payload).hexdigest(),
                        "size": len(payload),
                    }
                )
                artifact_bytes.append((name, payload))
                seed_tools[name] = seed_tool
            checked_plan = json.loads(SEED_MANIFEST.read_text())["build_plan"]
            candidate_plan = _candidate_build_plan(checked_plan)
            snapshot = capture_source_snapshot(REPO_ROOT, candidate_plan, windows_utf8=True)
            manifest = {
                "build_plan": checked_plan,
                "artifacts": artifacts,
                "schema": PROMOTED_SEED_SCHEMA,
            }
            manifest_bytes = (
                json.dumps(manifest, indent=2, sort_keys=True) + "\n"
            ).encode("ascii")
            seed_inputs = SeedInputs(
                manifest=manifest,
                manifest_bytes=manifest_bytes,
                manifest_sha256=hashlib.sha256(manifest_bytes).hexdigest(),
                live_manifest_path=root / "manifest.json",
                artifact_bytes=tuple(artifact_bytes),
                tools=seed_tools,
            )
            linux_output = root / "linux"
            linux_output.mkdir()
            windows_output = root / "windows"
            windows_output.mkdir()
            windows_seed_inputs = freeze_seed_inputs(
                WINDOWS_SEED_MANIFEST, root / "windows-checked-seed"
            )
            native_plan = _windows_build_plan(candidate_plan, utf8=True)

            linux_calls: list[tuple[str, tuple[str, ...]]] = []
            linux_manifest_pairs: list[tuple[Path, Path]] = []

            def linux_pair(
                _runner,
                _stage_two,
                _stage_three,
                tool_name,
                stage_two_arguments,
                stage_three_arguments=None,
                *_args,
            ):
                arguments = tuple(str(item) for item in stage_two_arguments)
                paired_arguments = tuple(
                    str(item)
                    for item in (
                        stage_three_arguments
                        if stage_three_arguments is not None
                        else stage_two_arguments
                    )
                )
                linux_calls.append((tool_name, arguments))
                if "--seed-manifest" in arguments:
                    linux_manifest_pairs.append(
                        (
                            Path(
                                arguments[
                                    arguments.index("--seed-manifest") + 1
                                ]
                            ),
                            Path(
                                paired_arguments[
                                    paired_arguments.index("--seed-manifest")
                                    + 1
                                ]
                            ),
                        )
                    )
                if arguments == ("--help",):
                    output = "usage: candidate\n"
                    if tool_name == "cupidobj":
                        output += (
                            "wrap-jpeg disk-template iso-fixture "
                            "profile-manifest\n"
                        )
                    if tool_name == "cupidld":
                        output += "i386pe\n"
                    return subprocess.CompletedProcess([], 0, output, "")
                if arguments[0] == "embed-jpeg":
                    source_name = arguments[arguments.index("--source") + 1]
                    if Path(source_name).name == "progressive.jpg":
                        return subprocess.CompletedProcess(
                            [],
                            1,
                            "",
                            "cupidbuild: checked CupidObj failed\n",
                        )
                    for command in (arguments, paired_arguments):
                        command_root = Path(
                            command[command.index("--root") + 1]
                        )
                        output_name = command[command.index("--output") + 1]
                        (command_root / output_name).write_bytes(
                            _unowned_relocation_object_payload()
                        )
                    return subprocess.CompletedProcess([], 0, "", "")
                if arguments[0] == "run":
                    tool = arguments[arguments.index("--tool") + 1]
                    forwarded = arguments[arguments.index("--") + 1 :]
                    if tool == "cupidobj" and forwarded == (
                        "--definitely-invalid-option",
                    ):
                        return subprocess.CompletedProcess(
                            [], 2, "", "usage: cupidobj\n"
                        )
                    if tool == "cupidc" and forwarded == ("--help",):
                        return subprocess.CompletedProcess(
                            [], 0, "usage: cupidc\n", ""
                        )
                    if (
                        tool == "cupidc"
                        and "/runner-invalid.cc" in forwarded
                    ):
                        return subprocess.CompletedProcess(
                            [], 1, "", "/runner-invalid.cc:1: error\n"
                        )
                    for command in (arguments, paired_arguments):
                        command_root = Path(
                            command[command.index("--root") + 1]
                        )
                        output_name = command[command.index("-o") + 1]
                        (command_root / output_name.lstrip("/")).write_bytes(
                            _unowned_relocation_object_payload()
                        )
                    return subprocess.CompletedProcess([], 0, "", "")
                raise BehaviorBoundaryReached

            linux_behavior_runner = ToolRunner(root)
            with mock.patch(
                "tools.bootstrap_toolchain._run_stage_pair",
                side_effect=linux_pair,
            ), mock.patch(
                "tools.bootstrap_toolchain."
                "_check_candidate_image_certification_behavior",
            ), mock.patch(
                "tools.bootstrap_toolchain."
                "_check_cupidbuild_compile_kernel_behavior",
            ) as linux_compile, self.assertRaises(BehaviorBoundaryReached):
                _run_behavior_checks(
                    linux_behavior_runner,
                    root,
                    linux_output,
                    stage,
                    stage,
                    seed_inputs,
                )

            linux_compile.assert_called_once_with(
                linux_behavior_runner, linux_output / "behavior",
                stage, stage, seed_inputs, "",
            )

            self.assertEqual(
                linux_calls[: len(CANDIDATE_TOOL_NAMES)],
                [
                    (name, ("--help",))
                    for name in CANDIDATE_TOOL_NAMES
                ],
            )
            self.assertTrue(linux_manifest_pairs)
            self.assertTrue(
                all(
                    first.parent.name == "stage-three-seed"
                    and second.parent.name == "stage-four-seed"
                    for first, second in linux_manifest_pairs
                )
            )
            linux_runner_root = linux_output / "behavior" / "cupidobj-runner"
            linux_runner_success = linux_calls[len(CANDIDATE_TOOL_NAMES)]
            self.assertEqual(linux_runner_success[0], "cupidbuild")
            self.assertEqual(linux_runner_success[1][0], "run")
            self.assertEqual(
                Path(
                    linux_runner_success[1][
                        linux_runner_success[1].index("--root") + 1
                    ]
                ),
                linux_runner_root,
            )
            self.assertEqual(
                linux_runner_success[1][
                    linux_runner_success[1].index("--tool") :
                ],
                (
                    "--tool",
                    "cupidobj",
                    "--",
                    "wrap-text",
                    "runner-input.txt",
                    "--identity",
                    "fixed-point-runner.txt",
                    "-o",
                    "stage-three-cupidobj-runner.o",
                ),
            )
            linux_runner_failure = linux_calls[
                len(CANDIDATE_TOOL_NAMES) + 1
            ]
            self.assertEqual(linux_runner_failure[0], "cupidbuild")
            self.assertEqual(
                linux_runner_failure[1][
                    linux_runner_failure[1].index("--tool") :
                ],
                (
                    "--tool",
                    "cupidobj",
                    "--",
                    "--definitely-invalid-option",
                ),
            )
            linux_cupidc_root = linux_output / "behavior" / "cupidc-runner"
            linux_cupidc_help = linux_calls[len(CANDIDATE_TOOL_NAMES) + 2]
            self.assertEqual(
                linux_cupidc_help,
                (
                    "cupidbuild",
                    (
                        "run",
                        "--seed-manifest",
                        str(
                            linux_cupidc_root
                            / "stage-three-seed"
                            / "manifest.json"
                        ),
                        "--root",
                        str(linux_cupidc_root),
                        "--tool",
                        "cupidc",
                        "--",
                        "--help",
                    ),
                ),
            )
            linux_cupidc_success = linux_calls[
                len(CANDIDATE_TOOL_NAMES) + 3
            ]
            self.assertEqual(linux_cupidc_success[0], "cupidbuild")
            self.assertEqual(
                linux_cupidc_success[1][
                    linux_cupidc_success[1].index("--tool") :
                ],
                (
                    "--tool",
                    "cupidc",
                    "--",
                    "--root",
                    str(linux_cupidc_root),
                    "--freestanding",
                    "-c",
                    "/runner-valid.cc",
                    "-o",
                    "/stage-three-cupidc-runner.o",
                ),
            )
            linux_cupidc_failure = linux_calls[
                len(CANDIDATE_TOOL_NAMES) + 4
            ]
            self.assertIn("/runner-invalid.cc", linux_cupidc_failure[1])
            self.assertEqual(
                linux_calls[len(CANDIDATE_TOOL_NAMES) + 5][0],
                "cupidbuild",
            )
            self.assertEqual(
                linux_calls[len(CANDIDATE_TOOL_NAMES) + 5][1][0],
                "embed-jpeg",
            )
            linux_jpeg_root = linux_output / "behavior" / "cupidbuild-jpeg"
            self.assertEqual(
                linux_calls[len(CANDIDATE_TOOL_NAMES) + 5][1],
                (
                    "embed-jpeg",
                    "--seed-manifest",
                    str(linux_jpeg_root / "stage-three-seed" / "manifest.json"),
                    "--root",
                    str(root),
                    "--source",
                    (linux_jpeg_root / "asset.jpg")
                    .relative_to(root)
                    .as_posix(),
                    "--output",
                    (linux_jpeg_root / "stage-three-cupidbuild-jpeg.o")
                    .relative_to(root)
                    .as_posix(),
                ),
            )
            self.assertEqual(
                linux_calls[len(CANDIDATE_TOOL_NAMES) + 6][1][0],
                "embed-jpeg",
            )
            self.assertEqual(
                Path(
                    linux_calls[len(CANDIDATE_TOOL_NAMES) + 6][1][
                        linux_calls[len(CANDIDATE_TOOL_NAMES) + 6][1].index(
                            "--source"
                        )
                        + 1
                    ]
                ).name,
                "progressive.jpg",
            )
            self.assertEqual(
                linux_calls[len(CANDIDATE_TOOL_NAMES) + 7][0],
                "cupidbuild",
            )
            self.assertEqual(
                linux_calls[len(CANDIDATE_TOOL_NAMES) + 7][1][0],
                "generate-ksyms",
            )

            windows_calls: list[tuple[str, tuple[str, ...]]] = []
            windows_manifest_pairs: list[tuple[Path, Path]] = []

            def windows_pair(
                _runner,
                _stage_two,
                _stage_three,
                tool_name,
                stage_two_arguments,
                stage_three_arguments=None,
                *_args,
            ):
                arguments = tuple(str(item) for item in stage_two_arguments)
                paired_arguments = tuple(
                    str(item)
                    for item in (
                        stage_three_arguments
                        if stage_three_arguments is not None
                        else stage_two_arguments
                    )
                )
                windows_calls.append((tool_name, arguments))
                if "--seed-manifest" in arguments:
                    windows_manifest_pairs.append(
                        (
                            Path(
                                arguments[
                                    arguments.index("--seed-manifest") + 1
                                ]
                            ),
                            Path(
                                paired_arguments[
                                    paired_arguments.index("--seed-manifest")
                                    + 1
                                ]
                            ),
                        )
                    )
                if arguments == ("--help",):
                    return subprocess.CompletedProcess(
                        [], 0, "usage: candidate\n", ""
                    )
                if arguments == ("--definitely-invalid-option",):
                    return subprocess.CompletedProcess(
                        [], 2, "", "usage: candidate\n"
                    )
                if arguments[0] == "embed-jpeg":
                    source_name = arguments[arguments.index("--source") + 1]
                    if Path(source_name).name == "progressive.jpg":
                        return subprocess.CompletedProcess(
                            [],
                            1,
                            "",
                            "cupidbuild: checked CupidObj failed\n",
                        )
                    for command in (arguments, paired_arguments):
                        command_root = Path(
                            command[command.index("--root") + 1]
                        )
                        output_name = command[command.index("--output") + 1]
                        (command_root / output_name).write_bytes(
                            _unowned_relocation_object_payload()
                        )
                    return subprocess.CompletedProcess([], 0, "", "")
                if arguments[0] == "run":
                    tool = arguments[arguments.index("--tool") + 1]
                    forwarded = arguments[arguments.index("--") + 1 :]
                    if tool == "cupidobj" and forwarded == (
                        "--definitely-invalid-option",
                    ):
                        return subprocess.CompletedProcess(
                            [], 2, "", "usage: cupidobj\n"
                        )
                    if tool == "cupidc" and forwarded == ("--help",):
                        return subprocess.CompletedProcess(
                            [], 0, "usage: cupidc\n", ""
                        )
                    if (
                        tool == "cupidc"
                        and "/runner-invalid.cc" in forwarded
                    ):
                        return subprocess.CompletedProcess(
                            [], 1, "", "/runner-invalid.cc:1: error\n"
                        )
                    for command in (arguments, paired_arguments):
                        command_root = Path(
                            command[command.index("--root") + 1]
                        )
                        output_name = command[command.index("-o") + 1]
                        (command_root / output_name.lstrip("/")).write_bytes(
                            _unowned_relocation_object_payload()
                        )
                    return subprocess.CompletedProcess([], 0, "", "")
                raise BehaviorBoundaryReached

            windows_behavior_runner = ToolRunner(root)
            with mock.patch(
                "tools.bootstrap_toolchain._run_stage_pair",
                side_effect=windows_pair,
            ), mock.patch(
                "tools.bootstrap_toolchain."
                "_check_candidate_image_certification_behavior",
            ), mock.patch(
                "tools.bootstrap_toolchain."
                "_check_cupidbuild_compile_kernel_behavior",
            ) as windows_compile, mock.patch(
                "tools.bootstrap_toolchain.capture_source_snapshot", return_value=snapshot,
            ) as windows_capture, self.assertRaises(BehaviorBoundaryReached):
                _run_native_windows_behavior_checks(
                    windows_behavior_runner,
                    windows_output,
                    stage,
                    stage,
                    native_plan,
                    windows_seed_inputs,
                    seed_inputs,
                )

            windows_capture.assert_called_once_with(windows_output, candidate_plan, windows_utf8=True, windows_long_paths=False, windows_user_link_aliases=False, windows_observer_workers=False)
            windows_compile.assert_called_once_with(
                windows_behavior_runner, windows_output / "behavior",
                stage, stage,
                _retarget_native_windows_behavior_seed(
                    windows_seed_inputs, _build_plan_sha256(native_plan), candidate_plan, snapshot, utf8=True
                ),
                "native Windows ",
            )

            expected_windows_calls = []
            for name in CANDIDATE_TOOL_NAMES:
                expected_windows_calls.extend(
                    [
                        (name, ("--help",)),
                        (name, ("--definitely-invalid-option",)),
                    ]
                )
            self.assertEqual(
                windows_calls[: len(expected_windows_calls)],
                expected_windows_calls,
            )
            self.assertTrue(windows_manifest_pairs)
            self.assertTrue(
                all(
                    first.parent.name == "stage-three-seed"
                    and second.parent.name == "stage-four-seed"
                    for first, second in windows_manifest_pairs
                )
            )
            expected_native_plan = _build_plan_sha256(native_plan)
            for manifest_pair in windows_manifest_pairs:
                for manifest_path in manifest_pair:
                    materialized = json.loads(
                        manifest_path.read_text(encoding="ascii")
                    )
                    self.assertEqual(
                        materialized["provenance"][
                            "native_build_plan_sha256"
                        ],
                        expected_native_plan,
                    )
            self.assertEqual(
                windows_seed_inputs.manifest["provenance"][
                    "native_build_plan_sha256"
                ],
                PROMOTED_WINDOWS_PLAN_SHA256,
            )
            windows_runner_root = (
                windows_output / "behavior" / "cupidobj-runner"
            )
            windows_runner_success = windows_calls[len(expected_windows_calls)]
            self.assertEqual(windows_runner_success[0], "cupidbuild")
            self.assertEqual(windows_runner_success[1][0], "run")
            self.assertEqual(
                Path(
                    windows_runner_success[1][
                        windows_runner_success[1].index("--root") + 1
                    ]
                ),
                windows_runner_root,
            )
            self.assertEqual(
                windows_runner_success[1][
                    windows_runner_success[1].index("--tool") :
                ],
                (
                    "--tool",
                    "cupidobj",
                    "--",
                    "wrap-text",
                    "runner-input.txt",
                    "--identity",
                    "fixed-point-runner.txt",
                    "-o",
                    "stage-three-cupidobj-runner.o",
                ),
            )
            windows_runner_failure = windows_calls[
                len(expected_windows_calls) + 1
            ]
            self.assertEqual(windows_runner_failure[0], "cupidbuild")
            self.assertEqual(
                windows_runner_failure[1][
                    windows_runner_failure[1].index("--tool") :
                ],
                (
                    "--tool",
                    "cupidobj",
                    "--",
                    "--definitely-invalid-option",
                ),
            )
            windows_cupidc_root = (
                windows_output / "behavior" / "cupidc-runner"
            )
            windows_cupidc_help = windows_calls[
                len(expected_windows_calls) + 2
            ]
            self.assertEqual(windows_cupidc_help[0], "cupidbuild")
            self.assertEqual(
                windows_cupidc_help[1][
                    windows_cupidc_help[1].index("--tool") :
                ],
                ("--tool", "cupidc", "--", "--help"),
            )
            windows_cupidc_success = windows_calls[
                len(expected_windows_calls) + 3
            ]
            self.assertEqual(windows_cupidc_success[0], "cupidbuild")
            self.assertEqual(
                windows_cupidc_success[1][
                    windows_cupidc_success[1].index("--tool") :
                ],
                (
                    "--tool",
                    "cupidc",
                    "--",
                    "--root",
                    str(windows_cupidc_root),
                    "--freestanding",
                    "-c",
                    "/runner-valid.cc",
                    "-o",
                    "/stage-three-cupidc-runner.o",
                ),
            )
            windows_cupidc_failure = windows_calls[
                len(expected_windows_calls) + 4
            ]
            self.assertIn("/runner-invalid.cc", windows_cupidc_failure[1])
            self.assertEqual(
                windows_calls[len(expected_windows_calls) + 5][0],
                "cupidbuild",
            )
            self.assertEqual(
                windows_calls[len(expected_windows_calls) + 5][1][0],
                "embed-jpeg",
            )
            windows_jpeg_root = (
                windows_output / "behavior" / "cupidbuild-jpeg"
            )
            self.assertEqual(
                windows_calls[len(expected_windows_calls) + 5][1],
                (
                    "embed-jpeg",
                    "--seed-manifest",
                    str(
                        windows_jpeg_root
                        / "stage-three-seed"
                        / "manifest.json"
                    ),
                    "--root",
                    str(windows_output),
                    "--source",
                    (windows_jpeg_root / "asset.jpg")
                    .relative_to(windows_output)
                    .as_posix(),
                    "--output",
                    (windows_jpeg_root / "stage-three-cupidbuild-jpeg.o")
                    .relative_to(windows_output)
                    .as_posix(),
                ),
            )
            self.assertEqual(
                windows_calls[len(expected_windows_calls) + 6][1][0],
                "embed-jpeg",
            )
            self.assertEqual(
                Path(
                    windows_calls[len(expected_windows_calls) + 6][1][
                        windows_calls[
                            len(expected_windows_calls) + 6
                        ][1].index("--source")
                        + 1
                    ]
                ).name,
                "progressive.jpg",
            )
            self.assertEqual(
                windows_calls[len(expected_windows_calls) + 7][0],
                "cupidbuild",
            )
            self.assertEqual(
                windows_calls[len(expected_windows_calls) + 7][1][0],
                "generate-ksyms",
            )

    def test_candidate_entry_corruption_requires_two_file_backed_pe_bytes(
        self,
    ):
        with self.assertRaisesRegex(
            BootstrapError,
            "^cannot locate file-backed candidate entry: candidate.exe$",
        ):
            _corrupt_candidate_entry_instruction(
                bytes(self._minimal_pe32()), "candidate.exe"
            )

    def test_candidate_behavior_certifies_each_compared_tool_image(self):
        class BehaviorBoundaryReached(Exception):
            pass

        strict_flags = (
            "--require-known",
            "--require-local-targets",
            "--require-code-anchors",
        )
        with tempfile.TemporaryDirectory(
            prefix=".candidate-image-certification-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            linux_tools = {}
            windows_tools = {}
            linux_image = _local_target_executable_payload(5)
            windows_image_buffer = self._minimal_pe32()
            struct.pack_into(
                "<I", windows_image_buffer, 0x98 + 0xE0 + 8, 2
            )
            windows_image = bytes(windows_image_buffer)
            for name in CANDIDATE_TOOL_NAMES:
                linux_path = root / "linux-tools" / f"{name}.elf"
                linux_path.parent.mkdir(exist_ok=True)
                linux_path.write_bytes(linux_image)
                linux_tools[name] = linux_path
                windows_path = root / "windows-tools" / f"{name}.exe"
                windows_path.parent.mkdir(exist_ok=True)
                windows_path.write_bytes(windows_image)
                windows_tools[name] = windows_path
            linux_stage = Stage(objects={}, tools=linux_tools)
            windows_stage = Stage(objects={}, tools=windows_tools)
            seed_inputs = SeedInputs(
                manifest={"build_plan": json.loads(
                    SEED_MANIFEST.read_text(encoding="utf-8")
                )["build_plan"]},
                manifest_bytes=b"{}\n",
                manifest_sha256="1" * 64,
                live_manifest_path=root / "manifest.json",
                artifact_bytes=tuple(
                    (name, b"seed") for name in CANDIDATE_TOOL_NAMES
                ),
                tools={
                    name: root / f"{name}.elf"
                    for name in CANDIDATE_TOOL_NAMES
                },
            )

            linux_calls = []
            linux_strict_timeouts = []

            def linux_pair(
                _runner,
                _stage_two,
                _stage_three,
                tool_name,
                stage_two_arguments,
                stage_three_arguments=None,
                *_args,
            ):
                first = tuple(str(item) for item in stage_two_arguments)
                second = tuple(
                    str(item)
                    for item in (
                        stage_three_arguments
                        if stage_three_arguments is not None
                        else stage_two_arguments
                    )
                )
                linux_calls.append((tool_name, first, second))
                if first == ("--help",):
                    output = "usage: candidate\n"
                    if tool_name == "cupidobj":
                        output += (
                            "wrap-jpeg disk-template iso-fixture "
                            "profile-manifest\n"
                        )
                    if tool_name == "cupidld":
                        output += "i386pe\n"
                    return subprocess.CompletedProcess([], 0, output, "")
                if first[:3] == strict_flags:
                    linux_strict_timeouts.append(_args)
                    if Path(first[-1]).name == "corrupted-cupidbuild.elf":
                        self.assertNotEqual(
                            Path(first[-1]).read_bytes(),
                            linux_tools["cupidbuild"].read_bytes(),
                        )
                        return subprocess.CompletedProcess(
                            [], 1, "", "code check failed\n"
                        )
                    return subprocess.CompletedProcess([], 0, "", "")
                raise AssertionError(f"unexpected Linux call: {first}")

            linux_output = root / "linux"
            linux_output.mkdir()
            with mock.patch(
                "tools.bootstrap_toolchain._run_stage_pair",
                side_effect=linux_pair,
            ), mock.patch(
                "tools.bootstrap_toolchain."
                "_check_cupidbuild_cupidobj_runner_behavior",
                side_effect=BehaviorBoundaryReached,
            ), self.assertRaises(BehaviorBoundaryReached):
                _run_behavior_checks(
                    ToolRunner(root),
                    root,
                    linux_output,
                    linux_stage,
                    linux_stage,
                    seed_inputs,
                )

            expected_linux_certifications = [
                (
                    "cupiddis",
                    (*strict_flags, str(linux_tools[name])),
                    (*strict_flags, str(linux_tools[name])),
                )
                for name in CANDIDATE_TOOL_NAMES
            ]
            corrupted_linux = linux_output / "behavior" / (
                "corrupted-cupidbuild.elf"
            )
            expected_linux_certifications.append(
                (
                    "cupiddis",
                    (*strict_flags, str(corrupted_linux)),
                    (*strict_flags, str(corrupted_linux)),
                )
            )
            self.assertEqual(
                linux_calls[len(CANDIDATE_TOOL_NAMES) :],
                expected_linux_certifications,
            )
            self.assertEqual(
                linux_strict_timeouts,
                [(360,)] * (len(CANDIDATE_TOOL_NAMES) + 1),
            )

            windows_calls = []
            windows_strict_timeouts = []

            def windows_pair(
                _runner,
                _stage_two,
                _stage_three,
                tool_name,
                stage_two_arguments,
                stage_three_arguments=None,
                *_args,
            ):
                first = tuple(str(item) for item in stage_two_arguments)
                second = tuple(
                    str(item)
                    for item in (
                        stage_three_arguments
                        if stage_three_arguments is not None
                        else stage_two_arguments
                    )
                )
                windows_calls.append((tool_name, first, second))
                if first == ("--help",):
                    return subprocess.CompletedProcess(
                        [], 0, "usage: candidate\n", ""
                    )
                if first == ("--definitely-invalid-option",):
                    return subprocess.CompletedProcess(
                        [], 2, "", "usage: candidate\n"
                    )
                if first[:3] == strict_flags:
                    windows_strict_timeouts.append(_args)
                    if Path(first[-1]).name == "corrupted-cupidbuild.exe":
                        self.assertNotEqual(
                            Path(first[-1]).read_bytes(),
                            windows_tools["cupidbuild"].read_bytes(),
                        )
                        return subprocess.CompletedProcess(
                            [], 1, "", "code check failed\n"
                        )
                    return subprocess.CompletedProcess([], 0, "", "")
                raise AssertionError(f"unexpected Windows call: {first}")

            windows_output = root / "windows"
            windows_output.mkdir()
            with mock.patch(
                "tools.bootstrap_toolchain._run_stage_pair",
                side_effect=windows_pair,
            ), mock.patch(
                "tools.bootstrap_toolchain.capture_source_snapshot",
                return_value={},
            ), mock.patch(
                "tools.bootstrap_toolchain."
                "_check_cupidbuild_cupidobj_runner_behavior",
                side_effect=BehaviorBoundaryReached,
            ), self.assertRaises(BehaviorBoundaryReached):
                _run_native_windows_behavior_checks(
                    ToolRunner(root),
                    windows_output,
                    windows_stage,
                    windows_stage,
                    _windows_build_plan(
                        _candidate_build_plan(seed_inputs.manifest["build_plan"]),
                        utf8=True,
                    ),
                    seed_inputs,
                    seed_inputs,
                )

            expected_windows_certifications = [
                (
                    "cupiddis",
                    (*strict_flags, str(windows_tools[name])),
                    (*strict_flags, str(windows_tools[name])),
                )
                for name in CANDIDATE_TOOL_NAMES
            ]
            corrupted_windows = windows_output / "behavior" / (
                "corrupted-cupidbuild.exe"
            )
            expected_windows_certifications.append(
                (
                    "cupiddis",
                    (*strict_flags, str(corrupted_windows)),
                    (*strict_flags, str(corrupted_windows)),
                )
            )
            self.assertEqual(
                windows_calls[len(CANDIDATE_TOOL_NAMES) * 2 :],
                expected_windows_certifications,
            )
            self.assertEqual(
                windows_strict_timeouts,
                [(360,)] * (len(CANDIDATE_TOOL_NAMES) + 1),
            )

    def test_manifest_author_uses_the_six_tool_candidate_plan(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupid-bootstrap-retained-seed-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source_root = root / "source"
            (source_root / "toolchain").mkdir(parents=True)
            removed_seed = root / "removed-seed"
            removed_seed.mkdir()
            seed_tools = {
                name: removed_seed / f"{name}.elf" for name in TOOL_NAMES
            }
            for path in seed_tools.values():
                path.write_bytes(b"retained seed image")
            checked_plan = json.loads(
                SEED_MANIFEST.read_text(encoding="utf-8")
            )["build_plan"]
            seed_inputs = SeedInputs(
                manifest={
                    "build_plan": checked_plan,
                    "build_plan_sha256": _build_plan_sha256(checked_plan),
                    "provenance": {
                        "source_revision": SEED_SOURCE_REVISION
                    },
                },
                manifest_bytes=b"{}",
                manifest_sha256="1" * 64,
                live_manifest_path=root / "manifest.json",
                artifact_bytes=tuple(
                    (name, b"retained seed image") for name in TOOL_NAMES
                ),
                tools=seed_tools,
            )
            shutil.rmtree(removed_seed)
            observed_plans: list[dict[str, object]] = []
            observed_producers: list[dict[str, Path]] = []

            def freeze_sources(_root, _plan, destination, *, windows_utf8, windows_long_paths=False,
                               windows_user_link_aliases=False, windows_observer_workers=False):
                self.assertTrue(windows_utf8)
                self.assertFalse(windows_long_paths)
                destination.mkdir()
                self.assertTrue(windows_observer_workers)
                return mock.Mock(root=destination, inventory={}, windows_utf8=windows_utf8,
                    windows_long_paths=windows_long_paths,
                    windows_user_link_aliases=windows_user_link_aliases,
                    windows_observer_workers=windows_observer_workers)

            def build_stage(
                _runner,
                _source_root,
                stage_directory,
                _producers,
                plan,
                _stage_name,
            ):
                stage_directory.mkdir()
                observed_plans.append(plan)
                observed_producers.append(dict(_producers))
                tools = {}
                for name in CANDIDATE_TOOL_NAMES:
                    tool = stage_directory / f"{name}.elf"
                    tool.write_bytes(
                        b"retained seed image"
                        if name in TOOL_NAMES
                        else b"candidate CupidBuild image"
                    )
                    tools[name] = tool
                return Stage(objects={}, tools=tools)

            def run_behavior(
                _runner,
                private_source_root,
                _behavior_root,
                _stage_three,
                _stage_four,
                _seed_inputs,
                evidence,
                profile_source_root,
            ):
                self.assertEqual(profile_source_root, source_root)
                (private_source_root / "behavior").mkdir()
                evidence["windows_runtime"] = {
                    "artifacts": {},
                    "cupiddis": {"loader": {}},
                    "loader": {},
                    "native_tools": {
                        name: {"loader": {}}
                        for name in (
                            "cupidasm",
                            "cupidc",
                            "cupidld",
                            "cupidobj",
                        )
                    },
                    "runtime_contract": {"loader": {}},
                }
                return {"success_cases": 0}

            with (
                mock.patch(
                    "tools.bootstrap_toolchain.freeze_source_inputs",
                    side_effect=freeze_sources,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.require_source_closures"
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.require_live_seed_inputs"
                ),
                mock.patch(
                    "tools.bootstrap_toolchain._build_stage",
                    side_effect=build_stage,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain._compare_stages",
                    return_value={"all_equal": True},
                ),
                mock.patch(
                    "tools.bootstrap_toolchain._run_behavior_checks",
                    side_effect=run_behavior,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.publish_bootstrap_outputs"
                ),
            ):
                report = _bootstrap_from_frozen_seed(
                    seed_inputs,
                    source_root,
                    source_root / "published",
                    compare_fixed_point=False,
                    windows_user_link_aliases=False,
                )

            self.assertEqual(
                report["initial_seed_matches_stage_two"],
                {name: True for name in TOOL_NAMES},
            )
            self.assertEqual(len(observed_plans), 3)
            self.assertEqual(
                observed_producers[0],
                {
                    name: seed_tools[name]
                    for name in (*PRODUCER_NAMES, "cupiddis")
                },
            )
            for plan in observed_plans:
                self.assertEqual(set(plan["links"]), set(CANDIDATE_TOOL_NAMES))
                self.assertEqual(len(plan["sources"]), 44)
            self.assertEqual(
                report["build_plan_sha256"],
                _build_plan_sha256(checked_plan),
            )
            self.assertEqual(
                report["candidate_build_plan"], observed_plans[0]
            )
            self.assertEqual(
                report["candidate_build_plan_sha256"],
                _build_plan_sha256(observed_plans[0]),
            )
            self.assertEqual(
                report["candidate_tools"], list(CANDIDATE_TOOL_NAMES)
            )
            self.assertEqual(report["status"], "pending-fixed-point-author")
            self.assertEqual(
                report["seed_source_revision"], SEED_SOURCE_REVISION
            )
            self.assertNotIn("comparisons", report)
            for stage in report["stages"].values():
                self.assertEqual(
                    set(stage["tools"]), set(CANDIDATE_TOOL_NAMES)
                )

    def test_promoted_linux_report_compares_all_six_seed_tools(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupid-promoted-linux-report-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source_root = root / "source"
            (source_root / "toolchain").mkdir(parents=True)
            seed_root = root / "seed"
            seed_root.mkdir()
            payloads = {
                name: f"promoted Linux {name} image".encode("ascii")
                for name in CANDIDATE_TOOL_NAMES
            }
            seed_tools = {
                name: seed_root / f"{name}.elf"
                for name in CANDIDATE_TOOL_NAMES
            }
            for name, path in seed_tools.items():
                path.write_bytes(payloads[name])

            manifest = self._promote_manifest_fixture(
                json.loads(SEED_MANIFEST.read_text(encoding="utf-8")),
                False,
            )
            for artifact in manifest["artifacts"]:
                payload = payloads[artifact["name"]]
                artifact["size"] = len(payload)
                artifact["sha256"] = hashlib.sha256(payload).hexdigest()
            manifest_bytes = (
                json.dumps(manifest, indent=2, sort_keys=True) + "\n"
            ).encode("ascii")
            (seed_root / "manifest.json").write_bytes(manifest_bytes)
            seed_inputs = SeedInputs(
                manifest=manifest,
                manifest_bytes=manifest_bytes,
                manifest_sha256=hashlib.sha256(manifest_bytes).hexdigest(),
                live_manifest_path=seed_root / "manifest.json",
                artifact_bytes=tuple(
                    (name, payloads[name]) for name in CANDIDATE_TOOL_NAMES
                ),
                tools=seed_tools,
            )
            observed_plans: list[dict[str, object]] = []

            def freeze_sources(_root, _plan, destination, *, windows_utf8, windows_long_paths=False,
                               windows_user_link_aliases=False, windows_observer_workers=False):
                self.assertTrue(windows_utf8)
                self.assertFalse(windows_long_paths)
                destination.mkdir()
                self.assertTrue(windows_observer_workers)
                return mock.Mock(root=destination, inventory={}, windows_utf8=windows_utf8,
                    windows_long_paths=windows_long_paths,
                    windows_user_link_aliases=windows_user_link_aliases,
                    windows_observer_workers=windows_observer_workers)

            def build_stage(
                _runner,
                _source_root,
                stage_directory,
                _producers,
                plan,
                _stage_name,
            ):
                stage_directory.mkdir()
                observed_plans.append(plan)
                tools = {}
                for name in CANDIDATE_TOOL_NAMES:
                    tool = stage_directory / f"{name}.elf"
                    tool.write_bytes(payloads[name])
                    tools[name] = tool
                return Stage(objects={}, tools=tools)

            def run_behavior(
                _runner,
                private_source_root,
                _behavior_root,
                _stage_three,
                _stage_four,
                _seed_inputs,
                evidence,
                profile_source_root,
            ):
                self.assertEqual(profile_source_root, source_root)
                (private_source_root / "behavior").mkdir()
                evidence["windows_runtime"] = {
                    "artifacts": {},
                    "cupiddis": {"loader": {}},
                    "loader": {},
                    "native_tools": {
                        name: {"loader": {}}
                        for name in (
                            "cupidasm",
                            "cupidc",
                            "cupidld",
                            "cupidobj",
                        )
                    },
                    "runtime_contract": {"loader": {}},
                }
                return {"success_cases": 0}

            with (
                mock.patch(
                    "tools.bootstrap_toolchain.freeze_source_inputs",
                    side_effect=freeze_sources,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.require_source_closures"
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.require_live_seed_inputs"
                ),
                mock.patch(
                    "tools.bootstrap_toolchain._build_stage",
                    side_effect=build_stage,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain._compare_stages",
                    return_value={"all_equal": True},
                ),
                mock.patch(
                    "tools.bootstrap_toolchain._run_behavior_checks",
                    side_effect=run_behavior,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.publish_bootstrap_outputs"
                ),
            ):
                report = _bootstrap_from_frozen_seed(
                    seed_inputs,
                    source_root,
                    source_root / "published",
                    compare_fixed_point=True,
                    windows_user_link_aliases=False,
                )

            self.assertEqual(
                report["initial_seed_matches_stage_two"],
                {name: True for name in CANDIDATE_TOOL_NAMES},
            )
            self.assertEqual(
                report["seed_source_revision"],
                PROMOTED_SOURCE_REVISION,
            )
            self.assertEqual(
                report["build_plan_sha256"], PROMOTED_LINUX_PLAN_SHA256
            )
            self.assertEqual(
                report["candidate_build_plan_sha256"],
                "e58a047f92d21a9ffbae6cc19726477f95ca3a872f1238d9cbab58dae950faaa",
            )
            self.assertEqual(
                report["candidate_tools"], list(CANDIDATE_TOOL_NAMES)
            )
            self.assertEqual(report["comparisons"], {"all_equal": True})
            self.assertEqual(report["status"], "pass")
            self.assertEqual(len(observed_plans), 3)
            for plan in observed_plans:
                self.assertEqual(set(plan["links"]), set(CANDIDATE_TOOL_NAMES))
            for stage in report["stages"].values():
                self.assertEqual(
                    set(stage["tools"]), set(CANDIDATE_TOOL_NAMES)
                )

    def test_windows_report_compares_stage_two_with_retained_seed_bytes(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupid-windows-retained-seed-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source_root = root / "source"
            (source_root / "toolchain").mkdir(parents=True)
            removed_seed = root / "removed-seed"
            removed_seed.mkdir()
            seed_tools = {
                name: removed_seed / f"{name}.exe" for name in TOOL_NAMES
            }
            for path in seed_tools.values():
                path.write_bytes(b"retained Windows seed image")
            plan_compiler = removed_seed / "cupidc.elf"
            plan_compiler.write_bytes(b"retained Linux plan compiler")
            seed_inputs = SeedInputs(
                manifest={
                    "provenance": {
                        "source_revision": SEED_SOURCE_REVISION
                    }
                },
                manifest_bytes=b"{}",
                manifest_sha256="1" * 64,
                live_manifest_path=root / "windows-manifest.json",
                artifact_bytes=tuple(
                    (name, b"retained Windows seed image")
                    for name in TOOL_NAMES
                ),
                tools=seed_tools,
            )
            checked_plan = json.loads(
                SEED_MANIFEST.read_text(encoding="utf-8")
            )["build_plan"]
            plan_inputs = SeedInputs(
                manifest={
                    "build_plan": checked_plan,
                    "provenance": {
                        "source_revision": SEED_SOURCE_REVISION
                    },
                },
                manifest_bytes=b"{}",
                manifest_sha256="2" * 64,
                live_manifest_path=root / "linux-manifest.json",
                artifact_bytes=(
                    ("cupidc", b"retained Linux plan compiler"),
                ),
                tools={"cupidc": plan_compiler},
            )
            shutil.rmtree(removed_seed)
            observed_plans: list[dict[str, object]] = []
            observed_producers: list[dict[str, Path]] = []

            def freeze_sources(_root, _plan, destination, *, windows_utf8, windows_long_paths=False,
                               windows_user_link_aliases=False, windows_observer_workers=False):
                self.assertTrue(windows_utf8)
                self.assertFalse(windows_long_paths)
                destination.mkdir()
                self.assertTrue(windows_observer_workers)
                return mock.Mock(root=destination, inventory={}, windows_utf8=windows_utf8,
                    windows_long_paths=windows_long_paths,
                    windows_user_link_aliases=windows_user_link_aliases,
                    windows_observer_workers=windows_observer_workers)

            def build_stage(
                _runner,
                _source_root,
                stage_directory,
                _producers,
                plan,
                _stage_name,
            ):
                stage_directory.mkdir()
                observed_plans.append(plan)
                observed_producers.append(dict(_producers))
                tools = {}
                for name in CANDIDATE_TOOL_NAMES:
                    tool = stage_directory / f"{name}.exe"
                    tool.write_bytes(
                        b"retained Windows seed image"
                        if name in TOOL_NAMES
                        else b"candidate Windows CupidBuild image"
                    )
                    tools[name] = tool
                return Stage(objects={}, tools=tools)

            def run_behavior(
                _runner,
                private_source_root,
                _stage_three,
                _stage_four,
                _native_plan,
                _seed_inputs,
                _linux_seed_inputs,
                profile_source_root,
            ):
                self.assertEqual(profile_source_root, source_root)
                (private_source_root / "behavior").mkdir()
                return {"success_cases": 0}

            with (
                mock.patch(
                    "tools.bootstrap_toolchain.freeze_source_inputs",
                    side_effect=freeze_sources,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.require_source_closures"
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.require_live_seed_inputs"
                ),
                mock.patch(
                    "tools.bootstrap_toolchain._build_windows_stage",
                    side_effect=build_stage,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain._compare_windows_stages",
                    return_value={"all_equal": True},
                ),
                mock.patch(
                    "tools.bootstrap_toolchain."
                    "_run_native_windows_behavior_checks",
                    side_effect=run_behavior,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.publish_bootstrap_outputs"
                ),
            ):
                report = _bootstrap_windows_from_frozen_seed(
                    seed_inputs,
                    plan_inputs,
                    source_root,
                    source_root / "published",
                    windows_user_link_aliases=False,
                )

            self.assertEqual(
                report["initial_seed_matches_stage_two"],
                {name: True for name in TOOL_NAMES},
            )
            self.assertEqual(len(observed_plans), 3)
            self.assertEqual(
                observed_producers[0],
                {
                    name: seed_tools[name]
                    for name in (*PRODUCER_NAMES, "cupiddis")
                },
            )
            for plan in observed_plans:
                self.assertEqual(set(plan["links"]), set(CANDIDATE_TOOL_NAMES))
                self.assertEqual(len(plan["sources"]), 48)
                self.assertEqual(len(plan["assembly_sources"]), 4)
            self.assertEqual(
                report["candidate_tools"], list(CANDIDATE_TOOL_NAMES)
            )
            self.assertEqual(
                report["seed_source_revision"], SEED_SOURCE_REVISION
            )
            self.assertEqual(
                report["plan_source_revision"], SEED_SOURCE_REVISION
            )
            self.assertEqual(
                report["stages"]["stage-two"]["producer_generation"],
                "checked-windows-execution-seed",
            )
            for stage in report["stages"].values():
                self.assertEqual(
                    set(stage["tools"]), set(CANDIDATE_TOOL_NAMES)
                )

    def test_promoted_windows_report_compares_all_six_seed_tools(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupid-promoted-windows-report-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source_root = root / "source"
            (source_root / "toolchain").mkdir(parents=True)
            execution_root = root / "execution-seed"
            plan_root = root / "plan-seed"
            execution_root.mkdir()
            plan_root.mkdir()
            execution_payloads = {
                name: f"promoted Windows {name} image".encode("ascii")
                for name in CANDIDATE_TOOL_NAMES
            }
            plan_payloads = {
                name: f"promoted Linux {name} image".encode("ascii")
                for name in CANDIDATE_TOOL_NAMES
            }
            execution_tools = {
                name: execution_root / f"{name}.exe"
                for name in CANDIDATE_TOOL_NAMES
            }
            plan_tools = {
                name: plan_root / f"{name}.elf"
                for name in CANDIDATE_TOOL_NAMES
            }
            for name in CANDIDATE_TOOL_NAMES:
                execution_tools[name].write_bytes(execution_payloads[name])
                plan_tools[name].write_bytes(plan_payloads[name])

            execution_manifest = self._promote_manifest_fixture(
                json.loads(
                    WINDOWS_SEED_MANIFEST.read_text(encoding="utf-8")
                ),
                True,
            )
            plan_manifest = self._promote_manifest_fixture(
                json.loads(SEED_MANIFEST.read_text(encoding="utf-8")),
                False,
            )
            for artifact in execution_manifest["artifacts"]:
                payload = execution_payloads[artifact["name"]]
                artifact["size"] = len(payload)
                artifact["sha256"] = hashlib.sha256(payload).hexdigest()
            for artifact in plan_manifest["artifacts"]:
                payload = plan_payloads[artifact["name"]]
                artifact["size"] = len(payload)
                artifact["sha256"] = hashlib.sha256(payload).hexdigest()
            plan_manifest_bytes = (
                json.dumps(plan_manifest, indent=2, sort_keys=True) + "\n"
            ).encode("ascii")
            plan_manifest_sha256 = hashlib.sha256(
                plan_manifest_bytes
            ).hexdigest()
            execution_manifest["provenance"][
                "plan_seed_manifest_sha256"
            ] = plan_manifest_sha256
            execution_manifest_bytes = (
                json.dumps(execution_manifest, indent=2, sort_keys=True)
                + "\n"
            ).encode("ascii")
            (execution_root / "manifest.json").write_bytes(
                execution_manifest_bytes
            )
            (plan_root / "manifest.json").write_bytes(plan_manifest_bytes)
            seed_inputs = SeedInputs(
                manifest=execution_manifest,
                manifest_bytes=execution_manifest_bytes,
                manifest_sha256=hashlib.sha256(
                    execution_manifest_bytes
                ).hexdigest(),
                live_manifest_path=execution_root / "manifest.json",
                artifact_bytes=tuple(
                    (name, execution_payloads[name])
                    for name in CANDIDATE_TOOL_NAMES
                ),
                tools=execution_tools,
            )
            plan_inputs = SeedInputs(
                manifest=plan_manifest,
                manifest_bytes=plan_manifest_bytes,
                manifest_sha256=hashlib.sha256(
                    plan_manifest_bytes
                ).hexdigest(),
                live_manifest_path=plan_root / "manifest.json",
                artifact_bytes=tuple(
                    (name, plan_payloads[name])
                    for name in CANDIDATE_TOOL_NAMES
                ),
                tools=plan_tools,
            )
            observed_plans: list[dict[str, object]] = []

            def freeze_sources(_root, _plan, destination, *, windows_utf8, windows_long_paths=False,
                               windows_user_link_aliases=False, windows_observer_workers=False):
                self.assertTrue(windows_utf8)
                self.assertFalse(windows_long_paths)
                destination.mkdir()
                self.assertTrue(windows_observer_workers)
                return mock.Mock(root=destination, inventory={}, windows_utf8=windows_utf8,
                    windows_long_paths=windows_long_paths,
                    windows_user_link_aliases=windows_user_link_aliases,
                    windows_observer_workers=windows_observer_workers)

            def build_stage(
                _runner,
                _source_root,
                stage_directory,
                _producers,
                plan,
                _stage_name,
            ):
                stage_directory.mkdir()
                observed_plans.append(plan)
                objects = {}
                for source in (
                    *plan["sources"],
                    *plan["assembly_sources"],
                ):
                    name = source["name"]
                    object_path = stage_directory / f"{name}.o"
                    object_path.write_bytes(
                        f"converged {name} object".encode("ascii")
                    )
                    objects[name] = object_path
                tools = {}
                for name in CANDIDATE_TOOL_NAMES:
                    tool = stage_directory / f"{name}.exe"
                    tool.write_bytes(execution_payloads[name])
                    tools[name] = tool
                return Stage(objects=objects, tools=tools)

            def run_behavior(
                _runner,
                private_source_root,
                _stage_three,
                _stage_four,
                _native_plan,
                _seed_inputs,
                _linux_seed_inputs,
                profile_source_root,
            ):
                self.assertEqual(profile_source_root, source_root)
                (private_source_root / "behavior").mkdir()
                return {"success_cases": 0}

            with (
                mock.patch(
                    "tools.bootstrap_toolchain.freeze_source_inputs",
                    side_effect=freeze_sources,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.require_source_closures"
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.require_live_seed_inputs"
                ),
                mock.patch(
                    "tools.bootstrap_toolchain._build_windows_stage",
                    side_effect=build_stage,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain."
                    "_run_native_windows_behavior_checks",
                    side_effect=run_behavior,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.publish_bootstrap_outputs"
                ),
            ):
                report = _bootstrap_windows_from_frozen_seed(
                    seed_inputs,
                    plan_inputs,
                    source_root,
                    source_root / "published",
                    windows_user_link_aliases=False,
                )

            self.assertEqual(
                report["initial_seed_matches_stage_two"],
                {name: True for name in CANDIDATE_TOOL_NAMES},
            )
            self.assertEqual(
                report["seed_source_revision"],
                PROMOTED_SOURCE_REVISION,
            )
            self.assertEqual(
                report["plan_source_revision"],
                PROMOTED_SOURCE_REVISION,
            )
            source_head_windows_plan_sha256 = _build_plan_sha256(
                _windows_build_plan(
                    _candidate_build_plan(plan_manifest["build_plan"]), utf8=True, observer_workers=True
                )
            )
            self.assertEqual(
                report["candidate_build_plan_sha256"],
                source_head_windows_plan_sha256,
            )
            self.assertEqual(
                report["build_plan_sha256"],
                source_head_windows_plan_sha256,
            )
            self.assertEqual(
                source_head_windows_plan_sha256,
                "17664a10f3d08b88d57a0be8d2824859faa8cbc3503fcd9bae73a60e6b857ca1",
            )
            self.assertEqual(
                report["candidate_tools"], list(CANDIDATE_TOOL_NAMES)
            )
            self.assertEqual(
                report["comparisons"],
                {
                    "all_equal": True,
                    "assembly_objects": 4,
                    "c_objects": 48,
                    "compared_generations": [
                        "stage-three",
                        "stage-four",
                    ],
                    "tool_images": 6,
                },
            )
            self.assertEqual(report["status"], "pass")
            self.assertEqual(len(observed_plans), 3)
            for plan in observed_plans:
                self.assertEqual(set(plan["links"]), set(CANDIDATE_TOOL_NAMES))
            for stage in report["stages"].values():
                self.assertEqual(
                    set(stage["tools"]), set(CANDIDATE_TOOL_NAMES)
                )
