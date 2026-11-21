## @file
#  @brief Check production kernel compiler arguments and Make dependencies.
import json
import re
import subprocess
import unittest
from pathlib import Path

from tests.cupidc_kernel_compile_fixtures import (
    COMPILER_READY_SOURCES,
    CRYPTO_SOURCES,
    FROZEN_KERNEL_INPUT_CLOSURES,
    GENERATED_KERNEL_SOURCES,
    KERNEL_I386_ARGUMENTS,
    KERNEL_SOURCES,
    NEW_PRODUCTION_SOURCES,
    OPERAND_FREE_DEPENDENCIES,
    OPERAND_FREE_SOURCES,
    PORT_IO_DEPENDENCIES,
    PORT_IO_SOURCES,
    REPO_ROOT,
    SMP_SOURCES,
    SOURCE_DRIVEN_SOURCES,
    TOOLCHAIN_KERNEL_SOURCES,
)
from tools import bootstrap_toolchain
from tools import cupidc_kernel_compile as kernel_compile


class KernelCompileCommandTests(unittest.TestCase):
    def test_approved_sources_and_profile_are_exact(self):
        self.assertEqual(kernel_compile.APPROVED_CRYPTO_SOURCES, CRYPTO_SOURCES)
        self.assertEqual(kernel_compile.APPROVED_SMP_SOURCES, SMP_SOURCES)
        self.assertEqual(
            kernel_compile.APPROVED_OPERAND_FREE_SOURCES,
            OPERAND_FREE_SOURCES,
        )
        self.assertEqual(
            kernel_compile.APPROVED_PORT_IO_SOURCES,
            PORT_IO_SOURCES,
        )
        self.assertEqual(
            kernel_compile.APPROVED_COMPILER_READY_SOURCES,
            COMPILER_READY_SOURCES,
        )
        self.assertEqual(
            kernel_compile.APPROVED_TOOLCHAIN_KERNEL_SOURCES,
            TOOLCHAIN_KERNEL_SOURCES,
        )
        self.assertEqual(
            kernel_compile.APPROVED_SOURCE_DRIVEN_SOURCES,
            SOURCE_DRIVEN_SOURCES,
        )
        self.assertEqual(
            kernel_compile.APPROVED_GENERATED_KERNEL_SOURCES,
            GENERATED_KERNEL_SOURCES,
        )
        self.assertEqual(
            {source: kernel_compile.FROZEN_KERNEL_INPUT_CLOSURES[source]
             for source in FROZEN_KERNEL_INPUT_CLOSURES},
            FROZEN_KERNEL_INPUT_CLOSURES,
        )
        self.assertEqual(
            tuple(sorted(kernel_compile.FROZEN_KERNEL_INPUT_CLOSURES)),
            tuple(sorted(KERNEL_SOURCES + GENERATED_KERNEL_SOURCES)),
        )
        self.assertEqual(len(kernel_compile.FROZEN_KERNEL_INPUT_CLOSURES), 157)
        self.assertEqual(
            kernel_compile.APPROVED_KERNEL_SOURCES,
            KERNEL_SOURCES,
        )
        self.assertEqual(
            kernel_compile.APPROVED_KERNEL_COMPILE_SOURCES,
            tuple(sorted(KERNEL_SOURCES + GENERATED_KERNEL_SOURCES)),
        )
        self.assertEqual(len(KERNEL_SOURCES), 156)
        self.assertEqual(len(set(KERNEL_SOURCES)), 156)
        self.assertEqual(kernel_compile.KERNEL_I386_ARGUMENTS, KERNEL_I386_ARGUMENTS)

        command = kernel_compile.build_compile_arguments(
            "/kernel/crypto/ct.cc",
            "/build/cupid/ct.o",
            "/native/repository",
        )
        self.assertEqual(
            command,
            (
                "-c",
                "/kernel/crypto/ct.cc",
                "-o",
                "/build/cupid/ct.o",
                *KERNEL_I386_ARGUMENTS,
                "--root",
                "/native/repository",
            ),
        )

    def test_production_owned_roots_use_the_cupidc_extension(self):
        renamed_sources = KERNEL_SOURCES
        self.assertEqual(len(renamed_sources), 156)
        self.assertEqual(len(set(renamed_sources)), 156)
        self.assertEqual(
            tuple(
                source
                for source in KERNEL_SOURCES
                if Path(source).suffix == ".c"
            ),
            (),
        )
        self.assertEqual(
            sum(Path(source).suffix == ".cc" for source in KERNEL_SOURCES),
            156,
        )

        for source in renamed_sources:
            with self.subTest(source=source):
                self.assertEqual(Path(source).suffix, ".cc")
                self.assertTrue((REPO_ROOT / source).is_file())
                self.assertFalse(
                    (REPO_ROOT / Path(source).with_suffix(".c")).exists()
                )

    def test_kernel_compiler_reuses_the_shared_checked_seed_runner(self):
        self.assertIs(
            kernel_compile.run_seed_tool,
            bootstrap_toolchain.run_seed_tool,
        )
        self.assertIs(kernel_compile.ToolRunner, bootstrap_toolchain.ToolRunner)
        self.assertFalse(hasattr(kernel_compile, "SeedExecutor"))
        self.assertFalse(hasattr(kernel_compile, "build_wsl_invocation"))

    def test_compile_arguments_preserve_a_host_root_path(self):
        repository = Path("C:/repository").resolve()
        command = kernel_compile.build_compile_arguments(
            "/kernel/crypto/ct.cc",
            "/build/cupid/ct.o",
            repository,
        )
        self.assertIs(command[-1], repository)


class KernelCompileMakefileTests(unittest.TestCase):
    def test_closed_source_driven_roots_use_checked_cupidc(self):
        expected_closures = {
            "kernel/audio/nuked_opl3.cc": (
                "kernel/audio/nuked_opl3.h",
                "kernel/core/string.h",
                "kernel/core/types.h",
            ),
            "kernel/core/kernel.cc": (
                "drivers/ata.h",
                "drivers/keyboard.h",
                "drivers/mouse.h",
                "drivers/pci.h",
                "drivers/pit.h",
                "drivers/rtc.h",
                "drivers/serial.h",
                "drivers/speaker.h",
                "drivers/timer.h",
                "drivers/vga.h",
                "kernel/core/debug.h",
                "kernel/core/kernel.h",
                "kernel/core/panic.h",
                "kernel/core/ports.h",
                "kernel/core/process.h",
                "kernel/core/string.h",
                "kernel/core/syscall.h",
                "kernel/core/types.h",
                "kernel/cpu/fpu.h",
                "kernel/cpu/idt.h",
                "kernel/cpu/irq.h",
                "kernel/cpu/isr.h",
                "kernel/cpu/pic.h",
                "kernel/cpu/simd.h",
                "kernel/crypto/csprng.h",
                "kernel/fs/blockcache.h",
                "kernel/fs/blockdev.h",
                "kernel/fs/devfs.h",
                "kernel/fs/fat16.h",
                "kernel/fs/fat16_vfs.h",
                "kernel/fs/fs.h",
                "kernel/fs/homefs.h",
                "kernel/fs/iso9660_vfs.h",
                "kernel/fs/ramfs.h",
                "kernel/fs/vfs.h",
                "kernel/gfx/fontsys.h",
                "kernel/gfx/gfx2d.h",
                "kernel/gfx/gfx2d_assets.h",
                "kernel/gfx/graphics.h",
                "kernel/gui/clipboard.h",
                "kernel/gui/desktop.h",
                "kernel/gui/gui.h",
                "kernel/gui/gui_containers.h",
                "kernel/gui/gui_events.h",
                "kernel/gui/gui_menus.h",
                "kernel/gui/gui_themes.h",
                "kernel/gui/gui_widgets.h",
                "kernel/gui/ui.h",
                "kernel/lang/as.h",
                "kernel/lang/ctool_kernel.h",
                "kernel/lang/exec.h",
                "kernel/lang/shell.h",
                "kernel/mm/memory.h",
                "kernel/network/net_if.h",
                "kernel/smp/bkl.h",
                "kernel/smp/ioapic.h",
                "kernel/smp/lapic.h",
                "kernel/smp/percpu.h",
                "kernel/smp/smp.h",
                "kernel/tls/tls_selftest.h",
                "kernel/usb/usb.h",
                "kernel/usb/usb_hc.h",
                "kernel/util/calendar.h",
                "toolchain/ctool.h",
            ),
            "kernel/core/string.cc": (
                "kernel/core/string.h",
                "kernel/core/types.h",
            ),
            "kernel/cpu/fpu.cc": (
                "drivers/serial.h",
                "kernel/core/panic.h",
                "kernel/core/process.h",
                "kernel/core/types.h",
                "kernel/cpu/fpu.h",
                "kernel/cpu/isr.h",
                "kernel/cpu/libm.h",
            ),
            "kernel/cpu/libm.cc": (
                "kernel/core/types.h",
                "kernel/cpu/libm.h",
            ),
            "kernel/cpu/simd.cc": (
                "drivers/serial.h",
                "drivers/timer.h",
                "kernel/core/kernel.h",
                "kernel/core/string.h",
                "kernel/core/types.h",
                "kernel/cpu/isr.h",
                "kernel/cpu/simd.h",
            ),
            "kernel/gfx/glyph_raster.cc": (
                "kernel/core/string.h",
                "kernel/core/types.h",
                "kernel/gfx/glyph_raster.h",
                "kernel/mm/memory.h",
            ),
            "kernel/gfx/jpeg.cc": (
                "kernel/core/types.h",
                "kernel/cpu/libm.h",
                "kernel/gfx/jpeg.h",
                "kernel/mm/memory.h",
            ),
            "kernel/smp/percpu.cc": (
                "drivers/serial.h",
                "kernel/core/process.h",
                "kernel/core/types.h",
                "kernel/smp/percpu.h",
            ),
            "kernel/smp/smp.cc": (
                "drivers/serial.h",
                "kernel/core/process.h",
                "kernel/core/types.h",
                "kernel/cpu/fpu.h",
                "kernel/cpu/idt.h",
                "kernel/cpu/isr.h",
                "kernel/mm/memory.h",
                "kernel/smp/acpi.h",
                "kernel/smp/bkl.h",
                "kernel/smp/ioapic.h",
                "kernel/smp/lapic.h",
                "kernel/smp/mp_tables.h",
                "kernel/smp/percpu.h",
                "kernel/smp/smp.h",
            ),
        }

        makefile = (REPO_ROOT / "Makefile").read_text(encoding="utf-8")
        logical_makefile = makefile.replace("\\\n", " ")
        for source, headers in expected_closures.items():
            with self.subTest(source=source):
                self.assertIn(source, kernel_compile.APPROVED_KERNEL_SOURCES)
                if source.startswith("kernel/smp/"):
                    self.assertIn(
                        source,
                        kernel_compile.APPROVED_SMP_SOURCES,
                    )
                else:
                    self.assertIn(
                        source,
                        kernel_compile.APPROVED_SOURCE_DRIVEN_SOURCES,
                    )
                self.assertTrue((REPO_ROOT / source).is_file())
                self.assertFalse(
                    (REPO_ROOT / Path(source).with_suffix(".c")).exists()
                )
                self.assertEqual(
                    kernel_compile.FROZEN_KERNEL_INPUT_CLOSURES[source],
                    headers,
                )

                output = Path(source).with_suffix(".o").as_posix()
                rule = re.search(
                    rf"^{re.escape(output)}: ([^\n]+)$",
                    logical_makefile,
                    re.MULTILINE,
                )
                self.assertIsNotNone(rule)
                self.assertEqual(
                    set(rule.group(1).split()),
                    {
                        source,
                        *headers,
                        "Makefile",
                        "$(PRODUCTION_SEED_INPUTS)",
                    },
                )
                self.assertIn(
                    f"--source {source} --output {output}\n", makefile
                )

    def test_exact_approved_cohort_uses_the_checked_cupidc_wrapper(self):
        makefile = (REPO_ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn(
            "CUPIDC_KERNEL_COMPILE := $(PYTHON) "
            "tools/cupidc_kernel_compile.py --root . \\\n"
            "\t--manifest $(PRODUCTION_SEED_MANIFEST)",
            makefile,
        )
        expected_compile_inputs = {
            "Makefile",
            "tools/cupidc_kernel_compile.py",
            "tools/cupidc_kernel_inputs.py",
            "tools/cupidc_i386_object.py",
            "tools/kernel_cupidc_frontier.py",
            "tools/bootstrap_toolchain.py",
            "$(PRODUCTION_SEED_INPUTS)",
        }
        compile_inputs_match = re.search(
            r"(?ms)^CUPIDC_KERNEL_COMPILE_INPUTS := (.+?)\n"
            r"(?=[A-Z][A-Z0-9_]*\s*[:?]?=)",
            makefile,
        )
        self.assertIsNotNone(compile_inputs_match)
        actual_compile_inputs = set(
            compile_inputs_match.group(1).replace("\\\n", " ").split()
        )
        self.assertEqual(actual_compile_inputs, expected_compile_inputs)
        production_inputs_match = re.search(
            r"(?ms)^override PRODUCTION_SEED_INPUTS := (.+?)\n"
            r"(?=[A-Z][A-Z0-9_]*\s*[:?]?=)",
            makefile,
        )
        self.assertIsNotNone(production_inputs_match)
        self.assertEqual(
            set(
                production_inputs_match.group(1)
                .replace("\\\n", " ")
                .split()
            ),
            {
                "$(PRODUCTION_SEED_MANIFEST)",
                "$(PRODUCTION_SEED_RELEASE)",
                "$(PRODUCTION_SEED_DIRECTORY)cupidasm.$(PRODUCTION_SEED_SUFFIX)",
                "$(PRODUCTION_SEED_DIRECTORY)cupidc.$(PRODUCTION_SEED_SUFFIX)",
                "$(PRODUCTION_SEED_DIRECTORY)cupiddis.$(PRODUCTION_SEED_SUFFIX)",
                "$(PRODUCTION_SEED_DIRECTORY)cupidld.$(PRODUCTION_SEED_SUFFIX)",
                "$(PRODUCTION_SEED_DIRECTORY)cupidobj.$(PRODUCTION_SEED_SUFFIX)",
                "$(PRODUCTION_SEED_DIRECTORY)cupidbuild.$(PRODUCTION_SEED_SUFFIX)",
            },
        )
        recipe_pattern = re.compile(
            r"^\t\$\(CUPIDC_KERNEL_COMPILE\) --source (\S+) "
            r"--output (\S+)$",
            re.MULTILINE,
        )
        actual = {
            (source, output)
            for source, output in recipe_pattern.findall(makefile)
        }
        expected = {
            (source, str(Path(source).with_suffix(".o")).replace("\\", "/"))
            for source in KERNEL_SOURCES + GENERATED_KERNEL_SOURCES
            if source not in kernel_compile.FROZEN_KERNEL_INPUT_CLOSURES
        }
        self.assertEqual(actual, expected)

        logical_makefile = makefile.replace("\\\n", " ")
        for source, output in sorted(expected):
            rule_pattern = re.compile(
                rf"^{re.escape(output)}: [^\n]*"
                rf"[ \t]+\$\(CUPIDC_KERNEL_COMPILE_INPUTS\)"
                rf"\n\t\$\(CUPIDC_KERNEL_COMPILE\) "
                rf"--source {re.escape(source)} "
                rf"--output {re.escape(output)}$",
                re.MULTILINE,
            )
            self.assertRegex(logical_makefile, rule_pattern)

        self.assertIn(
            "kernel/crypto/ecdsa.o: kernel/crypto/ecdsa.cc "
            "kernel/crypto/ecdsa.h kernel/crypto/p256.h "
            "kernel/crypto/hmac.h kernel/crypto/sha256.h "
            "kernel/core/string.h kernel/core/types.h "
            "Makefile $(PRODUCTION_SEED_INPUTS)",
            makefile,
        )
        self.assertIn(
            "kernel/smp/mp_tables.o: kernel/smp/mp_tables.cc "
            "kernel/smp/mp_tables.h kernel/smp/ioapic.h "
            "kernel/smp/percpu.h kernel/core/process.h "
            "kernel/core/types.h drivers/serial.h "
            "Makefile $(PRODUCTION_SEED_INPUTS)",
            makefile,
        )
        self.assertIn(
            "kernel/smp/acpi.o: kernel/smp/acpi.cc kernel/smp/acpi.h "
            "kernel/smp/mp_tables.h kernel/smp/ioapic.h "
            "kernel/smp/percpu.h kernel/core/process.h "
            "kernel/core/types.h drivers/serial.h "
            "Makefile $(PRODUCTION_SEED_INPUTS)",
            makefile,
        )
        self.assertRegex(
            makefile,
            r"kernel/smp/bkl\.o \\\n"
            r"\s+kernel/smp/mp_tables\.o \\\n"
            r"\s+kernel/smp/acpi\.o \\\n"
            r"\s+kernel/smp/smp\.o",
        )

        for source, headers in OPERAND_FREE_DEPENDENCIES.items():
            output = Path(source).with_suffix(".o").as_posix()
            match = re.search(
                rf"^{re.escape(output)}: ([^\n]+)$",
                makefile,
                re.MULTILINE,
            )
            self.assertIsNotNone(match, source)
            self.assertEqual(
                set(match.group(1).split()),
                {
                    source,
                    *kernel_compile.FROZEN_KERNEL_INPUT_CLOSURES[source],
                    "Makefile", "$(PRODUCTION_SEED_INPUTS)",
                },
            )

        for source, headers in PORT_IO_DEPENDENCIES.items():
            output = Path(source).with_suffix(".o").as_posix()
            match = re.search(
                rf"^{re.escape(output)}: ([^\n]+)$",
                logical_makefile,
                re.MULTILINE,
            )
            self.assertIsNotNone(match, source)
            self.assertEqual(
                set(match.group(1).split()),
                {
                    source,
                    *kernel_compile.FROZEN_KERNEL_INPUT_CLOSURES[source],
                    "Makefile", "$(PRODUCTION_SEED_INPUTS)",
                },
            )

        audit = json.loads(
            (
                REPO_ROOT
                / "docs"
                / "bootstrap"
                / "audits"
                / "active-build.json"
            ).read_text(encoding="utf-8")
        )
        audited_sources = {
            entry["path"]: entry for entry in audit["sources"]
        }

        def recursive_includes(source):
            closure = set()
            pending = list(audited_sources[source]["includes"])
            while pending:
                dependency = pending.pop()
                if dependency in closure:
                    continue
                closure.add(dependency)
                entry = audited_sources.get(dependency)
                if entry is not None:
                    pending.extend(entry["includes"])
            return closure

        self.assertEqual(len(kernel_compile.APPROVED_KERNEL_COMPILE_SOURCES), 157)
        self.assertEqual(
            set(kernel_compile.FROZEN_KERNEL_INPUT_CLOSURES),
            set(kernel_compile.APPROVED_KERNEL_COMPILE_SOURCES),
        )
        for source in kernel_compile.APPROVED_KERNEL_COMPILE_SOURCES:
            with self.subTest(source=source):
                if source == "kernel/cpu/ksyms_data.cc":
                    # The generated table has no checked-in include graph.
                    expected_headers = {"kernel/cpu/ksyms.h", "kernel/core/types.h"}
                else:
                    expected_headers = recursive_includes(source)
                frozen_headers = kernel_compile.FROZEN_KERNEL_INPUT_CLOSURES[source]
                self.assertEqual(set(frozen_headers), expected_headers, source)
                self.assertEqual(len(frozen_headers), len(expected_headers), source)
                output = Path(source).with_suffix(".o").as_posix()
                match = re.search(
                    rf"^{re.escape(output)}: ([^\n]+)$",
                    logical_makefile,
                    re.MULTILINE,
                )
                self.assertIsNotNone(match, source)
                self.assertEqual(
                    set(match.group(1).split()),
                    {
                        source,
                        *expected_headers,
                        "Makefile", "$(PRODUCTION_SEED_INPUTS)",
                    },
                    source,
                )
        self.assertIn(
            "kernel/usb/usb_hc.h",
            recursive_includes("kernel/usb/usb.cc"),
        )

        for source in KERNEL_SOURCES + GENERATED_KERNEL_SOURCES:
            output = str(Path(source).with_suffix(".o")).replace("\\", "/")
            host_rule = re.compile(
                rf"^{re.escape(output)}: [^\n]*"
                rf"\n\t\$\(CC\) ",
                re.MULTILINE,
            )
            self.assertNotRegex(makefile, host_rule)

    def test_new_production_targets_do_not_expand_to_the_host_compiler(self):
        targets = [
            Path(source).with_suffix(".o").as_posix()
            for source in NEW_PRODUCTION_SOURCES
        ]
        result = subprocess.run(
            [
                "make",
                "-B",
                "-n",
                "CC=__host_c_compiler_must_not_run__",
                *targets,
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = [
            line
            for line in result.stdout.replace("\\\n", " ").splitlines()
            if "tools/cupidc_kernel_compile.py" in line or "compile-kernel " in line
        ]
        self.assertEqual(len(commands), len(NEW_PRODUCTION_SOURCES))
        for source in NEW_PRODUCTION_SOURCES:
            self.assertTrue(
                any(f"--source {source}" in command for command in commands),
                source,
            )
        self.assertNotIn(
            "__host_c_compiler_must_not_run__",
            result.stdout + result.stderr,
        )

    def test_source_driven_targets_do_not_expand_to_host_tools(self):
        poison = "__host_tool_must_not_run__"
        targets = [
            Path(source).with_suffix(".o").as_posix()
            for source in SOURCE_DRIVEN_SOURCES
        ]
        result = subprocess.run(
            [
                "make",
                "-B",
                "-n",
                *[
                    f"{variable}={poison}"
                    for variable in (
                        "CC",
                        "CXX",
                        "CPP",
                        "HOSTCC",
                        "HOSTCXX",
                        "ASM",
                        "AS",
                        "LD",
                        "AR",
                        "NM",
                        "OBJCOPY",
                    )
                ],
                *targets,
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = [
            line
            for line in result.stdout.replace("\\\n", " ").splitlines()
            if "tools/cupidc_kernel_compile.py" in line or "compile-kernel " in line
        ]
        self.assertEqual(len(commands), len(SOURCE_DRIVEN_SOURCES))
        for source in SOURCE_DRIVEN_SOURCES:
            self.assertEqual(
                sum(f"--source {source}" in command for command in commands),
                1,
                source,
            )
        self.assertNotIn(poison, result.stdout + result.stderr)

    def test_smp_targets_do_not_expand_to_the_host_compiler(self):
        poison = "__host_tool_must_not_run__"
        targets = [
            Path(source).with_suffix(".o").as_posix()
            for source in SMP_SOURCES
        ]
        result = subprocess.run(
            [
                "make",
                "-B",
                "-n",
                *[
                    f"{variable}={poison}"
                    for variable in (
                        "CC",
                        "CXX",
                        "CPP",
                        "HOSTCC",
                        "HOSTCXX",
                        "ASM",
                        "AS",
                        "LD",
                        "AR",
                        "NM",
                        "OBJCOPY",
                    )
                ],
                *targets,
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = [
            line
            for line in result.stdout.replace("\\\n", " ").splitlines()
            if "tools/cupidc_kernel_compile.py" in line or "compile-kernel " in line
        ]
        self.assertEqual(len(commands), len(SMP_SOURCES))
        for source in SMP_SOURCES:
            self.assertEqual(
                sum(f"--source {source}" in command for command in commands),
                1,
                source,
            )
        self.assertNotIn(poison, result.stdout + result.stderr)

    def test_operand_free_targets_do_not_expand_to_the_host_compiler(self):
        targets = [
            Path(source).with_suffix(".o").as_posix()
            for source in OPERAND_FREE_SOURCES
        ]
        result = subprocess.run(
            [
                "make",
                "-B",
                "-n",
                "CC=__host_c_compiler_must_not_run__",
                *targets,
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = [
            line
            for line in result.stdout.replace("\\\n", " ").splitlines()
            if "tools/cupidc_kernel_compile.py" in line or "compile-kernel " in line
        ]
        self.assertEqual(len(commands), len(OPERAND_FREE_SOURCES))
        for source in OPERAND_FREE_SOURCES:
            self.assertTrue(
                any(f"--source {source}" in command for command in commands),
                source,
            )
        self.assertNotIn(
            "__host_c_compiler_must_not_run__",
            result.stdout + result.stderr,
        )

    def test_port_io_targets_do_not_expand_to_the_host_compiler(self):
        targets = [
            Path(source).with_suffix(".o").as_posix()
            for source in PORT_IO_SOURCES
        ]
        result = subprocess.run(
            [
                "make",
                "-B",
                "-n",
                "CC=__host_c_compiler_must_not_run__",
                *targets,
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = [
            line
            for line in result.stdout.replace("\\\n", " ").splitlines()
            if "tools/cupidc_kernel_compile.py" in line or "compile-kernel " in line
        ]
        self.assertEqual(len(commands), len(PORT_IO_SOURCES))
        for source in PORT_IO_SOURCES:
            self.assertTrue(
                any(f"--source {source}" in command for command in commands),
                source,
            )
        self.assertNotIn(
            "__host_c_compiler_must_not_run__",
            result.stdout + result.stderr,
        )

if __name__ == "__main__":
    unittest.main()
