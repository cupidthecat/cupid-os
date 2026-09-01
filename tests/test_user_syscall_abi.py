import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools.bootstrap_toolchain import (
    BootstrapError,
    ToolRunner,
    freeze_seed_inputs,
    run_seed_tool,
)
from tools import user_syscall_abi


REPO_ROOT = Path(__file__).resolve().parents[1]


class UserSyscallAbiTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        for relative in (
            "kernel/core/types.h",
            "kernel/core/syscall.h",
            "kernel/core/syscall.cc",
            "kernel/fs/vfs.h",
            "kernel/network/socket.h",
            "user/cupid.h",
        ):
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(REPO_ROOT / relative, target)

    def tearDown(self):
        self.temporary.cleanup()

    def test_repository_headers_and_initializer_define_one_i386_abi(self):
        report = user_syscall_abi.check_syscall_abi(REPO_ROOT)

        self.assertEqual(report["schema"], "cupid.user-syscall-abi.v1")
        self.assertEqual(report["version"], 5)
        self.assertEqual(report["field_count"], 103)
        self.assertEqual(report["table_size"], 412)
        self.assertEqual(report["dirent_size"], 136)
        self.assertEqual(
            report["dirent_offsets"],
            {"name": 0, "size": 128, "type": 132},
        )
        self.assertEqual(report["stat_size"], 8)
        self.assertEqual(report["stat_offsets"], {"size": 0, "type": 4})
        self.assertEqual(
            report["scalar_types"],
            {
                "int32_t": {"bytes": 4, "signed": True},
                "size_t": {"bytes": 4, "signed": False},
                "uint16_t": {"bytes": 2, "signed": False},
                "uint32_t": {"bytes": 4, "signed": False},
                "uint8_t": {"bytes": 1, "signed": False},
            },
        )
        self.assertEqual(report["first_function"], "print")
        self.assertEqual(report["last_function"], "sock_state")
        self.assertEqual(len(report["abi_sha256"]), 64)
        self.assertEqual(report["provider_count"], 101)
        self.assertEqual(len(report["provider_sha256"]), 64)

    def test_reordered_user_fields_are_rejected(self):
        header = self.root / "user/cupid.h"
        source = header.read_text(encoding="utf-8")
        source = source.replace(
            "    void (*print)(const char *str);\n"
            "    void (*putchar)(char c);",
            "    void (*putchar)(char c);\n"
            "    void (*print)(const char *str);",
        )
        header.write_text(source, encoding="utf-8")

        with self.assertRaisesRegex(
            user_syscall_abi.UserSyscallAbiError,
            "field 2 differs.*kernel print.*user putchar",
        ):
            user_syscall_abi.check_syscall_abi(self.root)

    def test_user_version_drift_is_rejected(self):
        header = self.root / "user/cupid.h"
        source = header.read_text(encoding="utf-8").replace(
            "#define CUPID_SYSCALL_VERSION 5",
            "#define CUPID_SYSCALL_VERSION 4",
        )
        header.write_text(source, encoding="utf-8")

        with self.assertRaisesRegex(
            user_syscall_abi.UserSyscallAbiError,
            "syscall version differs.*kernel 5.*user 4",
        ):
            user_syscall_abi.check_syscall_abi(self.root)

    def test_matching_unreviewed_version_drift_is_rejected(self):
        for relative in ("kernel/core/syscall.h", "user/cupid.h"):
            header = self.root / relative
            source = header.read_text(encoding="utf-8").replace(
                "#define CUPID_SYSCALL_VERSION 5",
                "#define CUPID_SYSCALL_VERSION 6",
            )
            header.write_text(source, encoding="utf-8")

        with self.assertRaisesRegex(
            user_syscall_abi.UserSyscallAbiError,
            "syscall version 6 is not the reviewed version 5",
        ):
            user_syscall_abi.check_syscall_abi(self.root)

    def test_user_function_signature_drift_is_rejected(self):
        header = self.root / "user/cupid.h"
        source = header.read_text(encoding="utf-8").replace(
            "    void (*print_int)(uint32_t num);",
            "    void (*print_int)(uint16_t num);",
        )
        header.write_text(source, encoding="utf-8")

        with self.assertRaisesRegex(
            user_syscall_abi.UserSyscallAbiError,
            "field 4 differs.*kernel print_int.*user print_int",
        ):
            user_syscall_abi.check_syscall_abi(self.root)

    def test_vfs_record_width_drift_is_rejected(self):
        header = self.root / "user/cupid.h"
        source = header.read_text(encoding="utf-8").replace(
            "#define VFS_MAX_NAME    128",
            "#define VFS_MAX_NAME    64",
        )
        header.write_text(source, encoding="utf-8")

        with self.assertRaisesRegex(
            user_syscall_abi.UserSyscallAbiError,
            "VFS_MAX_NAME differs.*kernel 128.*user 64",
        ):
            user_syscall_abi.check_syscall_abi(self.root)

    def test_vfs_record_field_drift_is_rejected(self):
        header = self.root / "user/cupid.h"
        source = header.read_text(encoding="utf-8").replace(
            "    uint8_t  type;\n} cupid_dirent_t;",
            "    uint16_t type;\n} cupid_dirent_t;",
        )
        header.write_text(source, encoding="utf-8")

        with self.assertRaisesRegex(
            user_syscall_abi.UserSyscallAbiError,
            "cupid_dirent_t does not match vfs_dirent_t",
        ):
            user_syscall_abi.check_syscall_abi(self.root)

    def test_exported_scalar_typedef_drift_is_rejected(self):
        header = self.root / "user/cupid.h"
        source = header.read_text(encoding="utf-8").replace(
            "typedef unsigned long      size_t;",
            "typedef unsigned long long size_t;",
        )
        header.write_text(source, encoding="utf-8")

        with self.assertRaisesRegex(
            user_syscall_abi.UserSyscallAbiError,
            "size_t differs.*kernel unsigned long.*user unsigned long long",
        ):
            user_syscall_abi.check_syscall_abi(self.root)

    def test_exported_socket_constant_drift_is_rejected(self):
        header = self.root / "user/cupid.h"
        source = header.read_text(encoding="utf-8").replace(
            "#define SOCK_TCP       2",
            "#define SOCK_TCP       3",
        )
        header.write_text(source, encoding="utf-8")

        with self.assertRaisesRegex(
            user_syscall_abi.UserSyscallAbiError,
            "SOCK_TCP differs.*kernel 2.*user 3",
        ):
            user_syscall_abi.check_syscall_abi(self.root)

    def test_missing_kernel_initializer_is_rejected(self):
        implementation = self.root / "kernel/core/syscall.cc"
        source = implementation.read_text(encoding="utf-8").replace(
            "  syscall_table.print_hex = print_hex;\n",
            "",
        )
        implementation.write_text(source, encoding="utf-8")

        with self.assertRaisesRegex(
            user_syscall_abi.UserSyscallAbiError,
            "missing initializer assignments: print_hex",
        ):
            user_syscall_abi.check_syscall_abi(self.root)

    def test_kernel_provider_drift_is_rejected(self):
        implementation = self.root / "kernel/core/syscall.cc"
        source = implementation.read_text(encoding="utf-8").replace(
            "syscall_table.ntohs            = htons;",
            "syscall_table.ntohs            = ntohs;",
        )
        implementation.write_text(source, encoding="utf-8")

        with self.assertRaisesRegex(
            user_syscall_abi.UserSyscallAbiError,
            "syscall provider contract changed",
        ):
            user_syscall_abi.check_syscall_abi(self.root)

    def test_live_input_drift_is_rejected_before_success(self):
        early_input = self.root / "kernel/core/types.h"
        original_payload = early_input.read_bytes()
        original_read = user_syscall_abi._read_input
        mutated = False

        def read_and_mutate(root, relative):
            nonlocal mutated
            result = original_read(root, relative)
            if relative == "user/cupid.h" and not mutated:
                early_input.write_bytes(original_payload + b"\n")
                mutated = True
            return result

        try:
            with mock.patch.object(
                user_syscall_abi,
                "_read_input",
                side_effect=read_and_mutate,
            ):
                with self.assertRaisesRegex(
                    user_syscall_abi.UserSyscallAbiError,
                    "ABI input changed while checking: "
                    "kernel/core/types.h",
                ):
                    user_syscall_abi.check_syscall_abi(self.root)
        finally:
            early_input.write_bytes(original_payload)

        report = user_syscall_abi.check_syscall_abi(self.root)
        self.assertEqual(
            report["abi_sha256"],
            user_syscall_abi.EXPECTED_ABI_SHA256,
        )

    def test_cli_emits_the_checked_contract_as_json(self):
        result = subprocess.run(
            [
                sys.executable,
                str(REPO_ROOT / "tools/user_syscall_abi.py"),
                "--root",
                str(REPO_ROOT),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=30,
        )

        self.assertEqual(
            result.returncode,
            0,
            msg=(result.stderr + result.stdout)[-4000:],
        )
        report = json.loads(result.stdout)
        self.assertEqual(report["field_count"], 103)
        self.assertEqual(report["table_size"], 412)

    def test_supported_user_build_runs_the_abi_gate(self):
        makefile = (REPO_ROOT / "user/Makefile").read_text(encoding="utf-8")
        logical = makefile.replace("\\\n", " ")

        self.assertRegex(logical, r"(?m)^all: test-syscall-abi ")
        self.assertIn(
            "test-syscall-abi: $(USER_SYSCALL_ABI_INPUTS) Makefile",
            logical,
        )
        for relative in user_syscall_abi.ABI_INPUTS:
            make_path = (
                "cupid.h"
                if relative == "user/cupid.h"
                else f"../{relative}"
            )
            self.assertIn(make_path, logical)
        self.assertRegex(
            logical,
            r"\$\(PYTHON\) \.\./tools/cupidc_toolchain_contracts\.py\s+"
            r"user-abi --root \.\.",
        )
        self.assertIn(
            "--output ../toolchain/build/cupidc-contracts", logical
        )
        self.assertIn(
            "../toolchain/tests/user_syscall_abi_contract.cc", logical
        )
        self.assertIn("../tools/user_syscall_abi.py", logical)
        self.assertRegex(
            logical,
            r"(?m)^test-cupidc-frontier: all test-syscall-abi ",
        )

    def test_toolchain_contract_cohort_tracks_every_abi_input(self):
        makefile = (
            REPO_ROOT / "toolchain/Makefile"
        ).read_text(encoding="utf-8")
        logical = makefile.replace("\\\n", " ")

        self.assertIn(
            "$(USER_SYSCALL_ABI_INPUTS) Makefile", logical
        )
        self.assertIn("../tools/user_syscall_abi.py", logical)
        for relative in user_syscall_abi.ABI_INPUTS:
            self.assertIn(f"../{relative}", logical)


class CupidBuiltUserSyscallAbiContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.contract_build = tempfile.TemporaryDirectory(
            prefix=".checked-user-syscall-abi-", dir=REPO_ROOT
        )
        build = Path(cls.contract_build.name)
        manifest = REPO_ROOT / "bootstrap/seeds/i386-linux/manifest.json"
        cls.runner = ToolRunner(REPO_ROOT)
        try:
            cls.frozen_seed = freeze_seed_inputs(manifest, build / "seed")
            contract_object = build / "contract.o"
            runtime_object = build / "runtime.o"
            start_object = build / "start.o"
            cls.contract = build / "user-abi.elf"

            def logical(path):
                return "/" + path.relative_to(REPO_ROOT).as_posix()

            commands = (
                (
                    "cupidc",
                    (
                        "--root",
                        REPO_ROOT,
                        "-c",
                        "/toolchain/tests/user_syscall_abi_contract.cc",
                        "-I",
                        "/toolchain",
                        "--include-angle",
                        "/toolchain/hosted/i386-linux/include",
                        "-o",
                        logical(contract_object),
                    ),
                ),
                (
                    "cupidc",
                    (
                        "--root",
                        REPO_ROOT,
                        "--gnu",
                        "-c",
                        "/toolchain/hosted/i386-linux/runtime.cc",
                        "-I",
                        "/toolchain",
                        "--include-angle",
                        "/toolchain/hosted/i386-linux/include",
                        "-o",
                        logical(runtime_object),
                    ),
                ),
                (
                    "cupidasm",
                    (
                        "-f",
                        "elf32",
                        REPO_ROOT / "toolchain/hosted/i386-linux/start.asm",
                        "-o",
                        start_object,
                    ),
                ),
                (
                    "cupidld",
                    (
                        "-m",
                        "elf_i386",
                        "--text-address",
                        "0x08048000",
                        "--entry",
                        "_start",
                        "-o",
                        cls.contract,
                        start_object,
                        contract_object,
                        runtime_object,
                    ),
                ),
            )
            for tool_name, arguments in commands:
                result = run_seed_tool(
                    manifest,
                    REPO_ROOT,
                    tool_name,
                    arguments,
                    timeout=180,
                    frozen_seed=cls.frozen_seed,
                    runner=cls.runner,
                )
                if result.returncode != 0 or result.stdout or result.stderr:
                    raise AssertionError(
                        f"{tool_name} failed with status "
                        f"{result.returncode}: "
                        f"{(result.stderr + result.stdout)[-8000:]}"
                    )
        except (BootstrapError, OSError, AssertionError):
            cls.contract_build.cleanup()
            raise

    @classmethod
    def tearDownClass(cls):
        cls.contract_build.cleanup()

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name) / "snapshot"
        self._copy_inputs(self.root)

    def tearDown(self):
        self.temporary.cleanup()

    @staticmethod
    def _copy_inputs(root):
        for relative in user_syscall_abi.ABI_INPUTS:
            target = root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(REPO_ROOT / relative, target)

    def _run(self, *arguments):
        return self.runner.run(self.contract, arguments, 30)

    def test_contract_report_matches_the_independent_python_oracle(self):
        result = self._run("check", self.root)

        self.assertEqual(
            result.returncode,
            0,
            msg=(result.stderr + result.stdout)[-4000:],
        )
        contract_report = json.loads(result.stdout)
        oracle_report = user_syscall_abi.check_syscall_abi(self.root)
        self.assertEqual(contract_report, oracle_report)
        self.assertEqual(contract_report["field_count"], 103)
        self.assertEqual(contract_report["table_size"], 412)
        self.assertEqual(
            contract_report["abi_sha256"],
            user_syscall_abi.EXPECTED_ABI_SHA256,
        )
        self.assertEqual(
            contract_report["provider_sha256"],
            user_syscall_abi.EXPECTED_PROVIDER_SHA256,
        )

    def test_contract_rejects_a_field_order_mutation(self):
        header = self.root / "user/cupid.h"
        source = header.read_text(encoding="utf-8").replace(
            "    void (*print)(const char *str);\n"
            "    void (*putchar)(char c);",
            "    void (*putchar)(char c);\n"
            "    void (*print)(const char *str);",
        )
        header.write_text(source, encoding="utf-8")

        result = self._run("check", self.root)

        self.assertEqual(result.returncode, 1)
        self.assertIn("syscall field 2 differs", result.stderr)
        self.assertIn("kernel print", result.stderr)
        self.assertIn("user putchar", result.stderr)

    def test_contract_rejects_scalar_layout_and_constant_mutations(self):
        header = self.root / "user/cupid.h"
        source = header.read_text(encoding="utf-8").replace(
            "typedef unsigned long      size_t;",
            "typedef unsigned long long size_t;",
        )
        header.write_text(source, encoding="utf-8")
        result = self._run("check", self.root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("size_t differs", result.stderr)

        self._copy_inputs(self.root)
        header = self.root / "user/cupid.h"
        source = header.read_text(encoding="utf-8").replace(
            "#define SOCK_TCP       2",
            "#define SOCK_TCP       3",
        )
        header.write_text(source, encoding="utf-8")
        result = self._run("check", self.root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("SOCK_TCP differs: kernel 2, user 3", result.stderr)

    def test_contract_rejects_layout_and_provider_mutations(self):
        header = self.root / "user/cupid.h"
        source = header.read_text(encoding="utf-8").replace(
            "    uint8_t  type;\n} cupid_dirent_t;",
            "    uint16_t type;\n} cupid_dirent_t;",
        )
        header.write_text(source, encoding="utf-8")
        result = self._run("check", self.root)
        self.assertEqual(result.returncode, 1)
        self.assertIn(
            "cupid_dirent_t does not match vfs_dirent_t", result.stderr
        )

        self._copy_inputs(self.root)
        implementation = self.root / "kernel/core/syscall.cc"
        source = implementation.read_text(encoding="utf-8").replace(
            "syscall_table.ntohs            = htons;",
            "syscall_table.ntohs            = ntohs;",
        )
        implementation.write_text(source, encoding="utf-8")
        result = self._run("check", self.root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("syscall provider contract changed", result.stderr)

    def test_contract_rereads_every_snapshot_before_success(self):
        reread_root = Path(self.temporary.name) / "reread"
        self._copy_inputs(reread_root)
        changed = reread_root / "kernel/core/types.h"
        changed.write_bytes(changed.read_bytes() + b"\n")

        result = self._run("check-snapshot", self.root, reread_root)

        self.assertEqual(result.returncode, 1)
        self.assertIn(
            "ABI input changed while checking: kernel/core/types.h",
            result.stderr,
        )

    def test_contract_rejects_an_unknown_selector(self):
        result = self._run("unknown", self.root)

        self.assertEqual(result.returncode, 2)
        self.assertIn(
            "usage: user-syscall-abi-contract check ROOT", result.stderr
        )


if __name__ == "__main__":
    unittest.main()
