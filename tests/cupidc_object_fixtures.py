## @file
#  @brief Build and run hosted CupidC object test tools.
import json
import os
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"


DOOM_TREE_FRONTIER_FAILURES = {}
DOOM_COMPAT_OBJECTS = {
    "/kernel/doom/dglibc.cc": (
                                  88972,
                                  '139525ec8651f3cff341c5c5c037b73111e2aa2c76d1dd840d6ae066cab98ce9',
                              ),
    "/kernel/doom/doom_libc_stubs.cc": (
                                           16136,
                                           "8ac1971586b322ff112291c45251b74f"
                                           "34da8e5b9199ddeb8beb32b9b66e96d1",
                                       ),
    "/kernel/doom/doomgeneric_cupidos.cc": (
                                               10792,
                                               '431fd57dac2a6062ddc50aa2a428c735492044d25ae02afc46641c5993fe51de',
                                           ),
}
DOOM_I_VIDEO_OBJECT_SIZE = 9088
DOOM_I_VIDEO_OBJECT_SHA256 = (
    'e381fa61c65cd52eec177aac0bb03498658bc9fdf02e597adfcbb60f775fd43f'
)
LIBM_SOURCE_SIZE = 46508
LIBM_SOURCE_SHA256 = (
    '2b10a62a7aefdafa09020e69205fafc278fd130477f95944e6c6ac3bda378169'
)
KERNEL_SOURCE_SIZE = 31654
KERNEL_SOURCE_SHA256 = (
    '7dc9898969de8e484be92172bdfe0e5852c03caf8d90c12571f134dc0dcc39aa'
)


def _cleanup_temporary_directory(directory):
    attempts = 30 if os.name == "nt" else 1
    for attempt in range(attempts):
        try:
            directory.cleanup()
            return
        except PermissionError:
            if attempt + 1 == attempts:
                raise
            time.sleep(0.1)



class ObjectToolSetup:
    @classmethod
    def setUpClass(cls):
        cls._build_directory = tempfile.TemporaryDirectory(
            prefix=".cupidc-object-build-", dir=TOOLCHAIN_ROOT
        )
        build_path = Path(cls._build_directory.name)
        relative_build = build_path.relative_to(TOOLCHAIN_ROOT).as_posix()
        suffix = ".exe" if os.name == "nt" else ""
        cls.contract_path = build_path / ("cupidc-object-contract" + suffix)
        cls.hosted_cupidasm_path = build_path / ("cupidasm" + suffix)
        cls.hosted_cupiddis_path = build_path / ("cupiddis" + suffix)
        cls.hosted_cupidld_path = build_path / ("cupidld" + suffix)
        cls.hosted_cupidobj_path = build_path / ("cupidobj" + suffix)
        cls.hosted_cupidc_path = build_path / ("cupidc" + suffix)
        cls.cupid_cupidasm_path = build_path / "cupid-built-cupidasm.elf"
        cls.cupid_cupiddis_path = build_path / "cupid-built-cupiddis.elf"
        cls.cupid_cupidld_path = build_path / "cupid-built-cupidld.elf"
        cls.cupid_cupidobj_path = build_path / "cupid-built-cupidobj.elf"
        cls.cupid_cupidc_path = build_path / "cupid-built-cupidc.elf"
        cls.cupid_cupidbuild_path = build_path / "cupid-built-cupidbuild.elf"
        cls.cupid_runtime_path = build_path / "cupid-built-runtime.elf"
        cls._cupid_tool_link = None
        target = f"{relative_build}/cupidc-object-contract{suffix}"
        cupidasm_target = f"{relative_build}/cupidasm{suffix}"
        cupiddis_target = f"{relative_build}/cupiddis{suffix}"
        cupidld_target = f"{relative_build}/cupidld{suffix}"
        cupidobj_target = f"{relative_build}/cupidobj{suffix}"
        cupidc_target = f"{relative_build}/cupidc{suffix}"
        result = subprocess.run(
            [
                "make",
                "-C",
                str(TOOLCHAIN_ROOT),
                f"BUILD_DIR={relative_build}",
                target,
                cupidasm_target,
                cupiddis_target,
                cupidld_target,
                cupidobj_target,
                cupidc_target,
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        if (
            result.returncode != 0
            or not cls.contract_path.exists()
            or not cls.hosted_cupidasm_path.exists()
            or not cls.hosted_cupiddis_path.exists()
            or not cls.hosted_cupidld_path.exists()
            or not cls.hosted_cupidobj_path.exists()
            or not cls.hosted_cupidc_path.exists()
        ):
            _cleanup_temporary_directory(cls._build_directory)
            raise AssertionError(
                "CupidC object contract build failed\n"
                + result.stdout
                + result.stderr
            )

    @classmethod
    def tearDownClass(cls):
        _cleanup_temporary_directory(cls._build_directory)

    @classmethod
    def build_cupid_tools(cls):
        if cls._cupid_tool_link is None:
            cls._cupid_tool_link = subprocess.run(
                [
                    str(cls.contract_path),
                    "self-host-link-tools",
                    str(REPO_ROOT),
                    str(cls.cupid_cupidasm_path),
                    str(cls.cupid_cupiddis_path),
                    str(cls.cupid_cupidld_path),
                    str(cls.cupid_cupidobj_path),
                    str(cls.cupid_cupidc_path),
                    str(cls.cupid_cupidbuild_path),
                    str(cls.cupid_runtime_path),
                ],
                cwd=TOOLCHAIN_ROOT,
                text=True,
                capture_output=True,
                timeout=180,
            )
        return cls._cupid_tool_link

    def wsl_path(self, path):
        converted = subprocess.run(
            ["wsl", "-e", "wslpath", "-a", str(path)],
            text=True,
            capture_output=True,
        )
        self.assertEqual(
            converted.returncode,
            0,
            "WSL could not translate " + str(path) + "\n" + converted.stderr,
        )
        return converted.stdout.strip()

    def run_cupid_linux_tool(self, executable, arguments, timeout=20):
        if os.name != "nt":
            executable.chmod(0o755)
            try:
                return subprocess.run(
                    [
                        str(executable),
                        *[str(argument) for argument in arguments],
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=timeout,
                )
            except OSError as error:
                if error.errno == 8:
                    self.skipTest(
                        "this kernel cannot execute static i386 ELF files"
                    )
                raise
        if shutil.which("wsl") is None:
            self.skipTest("WSL is unavailable for the i386 Linux tool check")
        linux_executable = self.wsl_path(executable)
        linux_arguments = [
            self.wsl_path(argument)
            if isinstance(argument, Path)
            else str(argument)
            for argument in arguments
        ]
        script = (
            'probe="/tmp/cupid-built-tool-$$"; '
            "trap 'rm -f \"$probe\"' EXIT HUP INT TERM; "
            'cp "$1" "$probe" || exit 125; '
            'chmod +x "$probe" || exit 125; '
            'shift; "$probe" "$@"'
        )
        result = subprocess.run(
            [
                "wsl",
                "-e",
                "sh",
                "-c",
                script,
                "sh",
                linux_executable,
                *linux_arguments,
            ],
            text=True,
            capture_output=True,
            timeout=timeout,
        )
        if result.returncode in (126, 127):
            self.skipTest("WSL cannot execute static i386 ELF files")
        return result

    def load_cupidc_profile(self, name):
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        contract = audit["contracts"]["c_preprocessor_translation_units"]
        return (
            audit,
            next(
                profile
                for profile in contract["profiles"]
                if profile["name"] == name
            ),
        )

    def cupidc_profile_arguments(self, profile):
        arguments = [
            "--root",
            REPO_ROOT,
            "--gnu",
            "--doom-compat",
            "--freestanding",
        ]
        both_forms = (
            "(CTOOL_C_PP_INCLUDE_QUOTED | "
            "CTOOL_C_PP_INCLUDE_ANGLE)"
        )
        for include_root in profile["include_roots"]:
            self.assertEqual(include_root["forms"], both_forms)
            arguments.extend(["-I", include_root["path"]])
        for action in profile["macro_actions"]:
            if action["name"] == "__SIZEOF_POINTER__":
                self.assertEqual(action["replacement"], "4")
                continue
            arguments.append(
                "-D" + action["name"] + "=" + action["replacement"]
            )
        for forced_include in profile["forced_includes"]:
            arguments.extend(["-include", forced_include])
        return arguments
