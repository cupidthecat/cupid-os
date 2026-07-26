import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

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
        self.assertIn(
            "$(PYTHON) ../tools/user_syscall_abi.py --root ..",
            logical,
        )
        self.assertRegex(
            logical,
            r"(?m)^test-cupidc-frontier: all test-syscall-abi ",
        )


if __name__ == "__main__":
    unittest.main()
