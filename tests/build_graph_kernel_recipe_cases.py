## @file
#  @brief Check kernel compiler recipes and supported audit commands.
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from tests.build_graph_audit_fixtures import (
    AUDIT_TOOL,
    CUPIDC_PP_CONTRACT,
    REPO_ROOT,
    TOOLCHAIN_MAKEFILE,
    WINDOWS_PRODUCTION_SEED_INPUTS,
    _load_audit_module,
    _write,
)


class KernelRecipeCases:
    def test_generated_kernel_symbols_use_the_checked_cupidc_graph(self):
        with tempfile.TemporaryDirectory() as td:
            output = Path(td) / "audit.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(REPO_ROOT),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)

            audit = json.loads(output.read_text(encoding="utf-8"))
            transforms = {
                entry["output"]: entry
                for entry in audit["build"]["transforms"]
            }
            generated = transforms["kernel/cpu/ksyms_data.cc"]
            self.assertEqual(
                generated["tools"],
                [
                    "cupid_builder",
                    "cupid_disassembler",
                    "cupid_object",
                ],
            )
            self.assertEqual(generated["operation"], "generate_ksyms_source")
            self.assertEqual(
                set(generated["inputs"]),
                {
                    "kernel/kernel.elf.pass1",
                    "Makefile",
                    *WINDOWS_PRODUCTION_SEED_INPUTS,
                },
            )

            compiled = transforms["kernel/cpu/ksyms_data.o"]
            self.assertEqual(
                compiled["tools"],
                ["cupid_builder", "cupid_c_compiler"],
            )
            self.assertEqual(
                compiled["operation"],
                "compile_c_to_elf32_object",
            )
            self.assertEqual(
                set(compiled["inputs"]),
                {
                    "kernel/cpu/ksyms_data.cc",
                    "kernel/cpu/ksyms.h",
                    "kernel/core/types.h",
                    "Makefile",
                    *WINDOWS_PRODUCTION_SEED_INPUTS,
                },
            )
            sources = {entry["path"]: entry for entry in audit["sources"]}
            self.assertEqual(
                sources["kernel/cpu/ksyms_data.cc"]["language"],
                "cupid_c",
            )
            self.assertEqual(
                sources["kernel/cpu/ksyms_data.cc"]["origin"],
                "generated",
            )
            self.assertNotIn("kernel/cpu/ksyms_data.c", transforms)

    def test_make_include_extraction_keeps_assignment_adjacent_first_root(
        self,
    ):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                .SUFFIXES:
                CC = host-cc
                KERNEL_INCLUDES=-I./kernel -I./kernel/core
                CFLAGS = $(KERNEL_INCLUDES)
                .PHONY: all
                all: main.o
                main.o: main.c
                \t$(CC) $(CFLAGS) -c $< -o $@
                """,
            )
            _write(root / "main.c", "int main(void) { return 0; }\n")
            (root / "kernel" / "core").mkdir(parents=True)
            output = root / "audit.json"

            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDIT_TOOL),
                    "--root",
                    str(root),
                    "--output",
                    str(output),
                ],
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                json.loads(output.read_text(encoding="utf-8"))["build"][
                    "include_search_paths"
                ],
                ["kernel", "kernel/core"],
            )

    def test_cupidc_make_profile_validation_rejects_preprocessor_drift(self):
        module = _load_audit_module()
        kernel_roots = " ".join(
            f"-I.{path}"
            for path in (
                "/kernel",
                "/kernel/audio",
                "/kernel/core",
                "/kernel/cpu",
                "/kernel/crypto",
                "/kernel/doom",
                "/kernel/fs",
                "/kernel/gfx",
                "/kernel/gui",
                "/kernel/lang",
                "/kernel/mm",
                "/kernel/network",
                "/kernel/smp",
                "/kernel/tls",
                "/kernel/usb",
                "/kernel/util",
                "/drivers",
                "/toolchain",
            )
        )

        def fixture(
            root,
            *,
            cflags_extra="",
            opt_extra="",
            roots_extra="",
            doom_implicit_flag="-Wno-implicit-function-declaration",
            hosted_cppflags_extra="",
            hosted_cflags_extra="",
        ):
            _write(
                root / "Makefile",
                f"""
                KERNEL_INCLUDES={kernel_roots} {roots_extra}
                CFLAGS=-m32 -ffreestanding -nostdinc -msse2 -pedantic \\
                       $(KERNEL_INCLUDES) -DDEBUG {cflags_extra}
                CFLAGS_DOOM=-m32 -ffreestanding -nostdinc -msse2 \\
                            $(KERNEL_INCLUDES) -I./kernel/doom/src \\
                            -I./kernel/doom/src/include_stubs \\
                            {doom_implicit_flag}
                CFLAGS_DOOM_TREE=$(CFLAGS_DOOM) \\
                    -include kernel/doom/dglibc_compat.h \\
                    -DDEFAULT_SAVEGAMEDIR=\\\"/home/doom/\\\" \\
                    -DDOOM_PORT_CUPIDOS=1
                OPT=-O2 {opt_extra}
                """,
            )
            _write(
                root / "user" / "Makefile",
                "CFLAGS=-m32 -ffreestanding -I.\n",
            )
            _write(
                root / "toolchain" / "Makefile",
                f"CPPFLAGS=-I. {hosted_cppflags_extra}\n"
                f"CFLAGS=-std=c11 {hosted_cflags_extra}\n",
            )

        cases = {
            "strict standard": (
                {"cflags_extra": "-std=c11"},
                r"unmodeled preprocessor flag.*-std=c11",
            ),
            "undefinition": (
                {"cflags_extra": "-UDEBUG"},
                r"unmodeled preprocessor flag.*-UDEBUG",
            ),
            "alternate include": (
                {"cflags_extra": "-iquote./private"},
                r"unmodeled preprocessor flag.*-iquote",
            ),
            "contradictory word size": (
                {"cflags_extra": "-m64"},
                r"unmodeled preprocessor flag.*-m64",
            ),
            "disabled SIMD target": (
                {"cflags_extra": "-mno-sse2"},
                r"unmodeled preprocessor flag.*-mno-sse2",
            ),
            "driver preprocessor pass-through": (
                {"cflags_extra": "-Wp,-DLOCAL=1"},
                r"unmodeled preprocessor flag.*-Wp,-DLOCAL=1",
            ),
            "alternate language mode": (
                {"cflags_extra": "-xc++"},
                r"unmodeled preprocessor flag.*-xc\+\+",
            ),
            "missing Doom implicit-call policy": (
                {"doom_implicit_flag": ""},
                r"DOOM_COMPAT_I386 implicit-function policy differs",
            ),
            "unsigned character mode": (
                {"cflags_extra": "-funsigned-char"},
                r"unmodeled preprocessor flag.*-funsigned-char",
            ),
            "negated signed character mode": (
                {"cflags_extra": "-fno-signed-char"},
                r"unmodeled preprocessor flag.*-fno-signed-char",
            ),
            "short wchar mode": (
                {"cflags_extra": "-fshort-wchar"},
                r"unmodeled preprocessor flag.*-fshort-wchar",
            ),
            "accelerator predefined macro": (
                {"cflags_extra": "-fopenacc"},
                r"unmodeled preprocessor flag.*-fopenacc",
            ),
            "extra include root": (
                {"roots_extra": "-I./private"},
                r"include-root order differs",
            ),
            "OPT-only macro": (
                {"opt_extra": "-DLOCAL=1"},
                r"OPT has preprocessor effects",
            ),
            "hosted alternate language mode": (
                {"hosted_cflags_extra": "-std=gnu11"},
                r"unmodeled preprocessor flag.*-std=gnu11",
            ),
            "hosted configured macro": (
                {"hosted_cflags_extra": "-DLOCAL=1"},
                r"configured macros differ.*CPPFLAGS\+CFLAGS",
            ),
            "hosted extra include root": (
                {"hosted_cppflags_extra": "-Iprivate"},
                r"HOSTED_TOOLCHAIN_64 include-root order differs",
            ),
            "hosted contradictory word size": (
                {"hosted_cflags_extra": "-m32"},
                r"HOSTED_TOOLCHAIN_64 has unmodeled preprocessor flag.*-m32",
            ),
            "hosted freestanding mode": (
                {"hosted_cflags_extra": "-ffreestanding"},
                r"HOSTED_TOOLCHAIN_64 has unmodeled preprocessor flag"
                r".*-ffreestanding",
            ),
        }
        for name, (changes, message) in cases.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                fixture(root, **changes)
                with self.assertRaisesRegex(module.AuditError, message):
                    module._validate_c_preprocessor_make_profiles(root, "make")

    def test_make_profile_values_use_gnu_make_assignment_timing(self):
        module = _load_audit_module()
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                ROOT=-I/a
                CFLAGS := $(ROOT)
                ROOT=-I/b
                .PHONY: all
                all:
                """,
            )

            values = module._read_evaluated_make_variables(
                root, "make", ("CFLAGS", "ROOT")
            )

            self.assertEqual(values, {"CFLAGS": "-I/a", "ROOT": "-I/b"})

    def test_cupidc_kernel_wrapper_make_binding_rejects_drift(self):
        module = _load_audit_module()
        transforms = [
            {
                "tools": ["cupid_c_compiler", "host_python"],
                "recipe": ["$(CUPIDC_KERNEL_COMPILE)"],
            }
        ]
        cases = {
            "non-Python launcher": (
                "PYTHON := clang\n"
                "PRODUCTION_SEED_MANIFEST := seed.json\n"
                "CUPIDC_KERNEL_COMPILE := $(PYTHON) "
                "tools/cupidc_kernel_compile.py --root . "
                "--manifest $(PRODUCTION_SEED_MANIFEST)\n",
                "is not a Python launcher",
            ),
            "Python command string": (
                "PYTHON := python3 -c pass\n"
                "PRODUCTION_SEED_MANIFEST := seed.json\n"
                "CUPIDC_KERNEL_COMPILE := $(PYTHON) "
                "tools/cupidc_kernel_compile.py --root . "
                "--manifest $(PRODUCTION_SEED_MANIFEST)\n",
                "must contain only the Python launcher",
            ),
            "Python module": (
                "PYTHON := python3 -m unchecked\n"
                "PRODUCTION_SEED_MANIFEST := seed.json\n"
                "CUPIDC_KERNEL_COMPILE := $(PYTHON) "
                "tools/cupidc_kernel_compile.py --root . "
                "--manifest $(PRODUCTION_SEED_MANIFEST)\n",
                "must contain only the Python launcher",
            ),
            "different wrapper": (
                "PYTHON := python3\n"
                "PRODUCTION_SEED_MANIFEST := seed.json\n"
                "CUPIDC_KERNEL_COMPILE := $(PYTHON) "
                "tools/other_wrapper.py --root . "
                "--manifest $(PRODUCTION_SEED_MANIFEST)\n",
                "differs from the checked command",
            ),
            "extra option": (
                "PYTHON := python3\n"
                "PRODUCTION_SEED_MANIFEST := seed.json\n"
                "CUPIDC_KERNEL_COMPILE := $(PYTHON) "
                "tools/cupidc_kernel_compile.py --root . "
                "--manifest $(PRODUCTION_SEED_MANIFEST) --unchecked\n",
                "differs from the checked command",
            ),
        }
        for name, (binding, message) in cases.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                _write(
                    root / "Makefile",
                    binding + ".PHONY: all\nall:\n",
                )
                with self.assertRaisesRegex(module.AuditError, message):
                    module._validate_cupidc_kernel_compile_make_binding(
                        root,
                        "make",
                        transforms,
                    )

    def test_cupidc_kernel_wrapper_make_binding_accepts_checked_command(self):
        module = _load_audit_module()
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(
                root / "Makefile",
                """
                PYTHON := python3
                PRODUCTION_SEED_MANIFEST := seed.json
                CUPIDC_KERNEL_COMPILE := $(PYTHON) \
                    tools/cupidc_kernel_compile.py --root . \
                    --manifest $(PRODUCTION_SEED_MANIFEST)
                .PHONY: all
                all:
                """,
            )

            module._validate_cupidc_kernel_compile_make_binding(
                root,
                "make",
                [{"tools": ["cupid_c_compiler", "host_python"],
                  "recipe": ["$(CUPIDC_KERNEL_COMPILE)"]}],
            )

    def test_production_wrappers_require_the_production_seed_binding(self):
        module = _load_audit_module()
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            root_makefile = """
                PYTHON := python3
                PRODUCTION_SEED_MANIFEST := windows-seed.json
                CUPIDC_PRODUCTION_COMPILE := $(PYTHON) \
                    tools/cupidc_production_compile.py --root . \
                    --cohort generated-install \
                    --manifest $(PRODUCTION_SEED_MANIFEST)
                .PHONY: all
                all:
            """
            user_makefile = """
                PYTHON := python3
                BOOTSTRAP_SEED_MANIFEST := ../linux-seed.json
                PRODUCTION_SEED_MANIFEST := ../windows-seed.json
                CUPIDC_PRODUCTION_COMPILE := $(PYTHON) \
                    ../tools/cupidc_production_compile.py --root .. \
                    --cohort user \
                    --manifest $(PRODUCTION_SEED_MANIFEST)
                CUPIDLD_USER_LINK := $(PYTHON) \
                    ../tools/cupidld_user_link.py --root .. \
                    --manifest $(PRODUCTION_SEED_MANIFEST)
                .PHONY: all
                all:
            """
            _write(root / "Makefile", root_makefile)
            _write(root / "user" / "Makefile", user_makefile)

            def model(directory, recipes):
                return module.BuildModel(
                    directory=directory,
                    root_target="all",
                    rules={},
                    reachable=set(),
                    direct_sources=set(),
                    generated_sources=set(),
                    forced_sources=set(),
                    includes_by_source={},
                    include_search_paths=[],
                    transforms=[{"recipe": [recipe]} for recipe in recipes],
                )

            models = [
                model(".", ("$(CUPIDC_PRODUCTION_COMPILE)",)),
                model(
                    "user",
                    (
                        "$(CUPIDC_PRODUCTION_COMPILE)",
                        "$(CUPIDLD_USER_LINK)",
                    ),
                ),
            ]
            module._validate_cupidc_production_make_bindings(
                root,
                "make",
                models,
            )

            _write(
                root / "Makefile",
                root_makefile.replace(
                    "--manifest $(PRODUCTION_SEED_MANIFEST)",
                    "--unchecked",
                    1,
                ),
            )
            with self.assertRaisesRegex(
                module.AuditError,
                "production wrapper binding differs",
            ):
                module._validate_cupidc_production_make_bindings(
                    root,
                    "make",
                    models,
                )

            _write(root / "Makefile", root_makefile)
            link_prefix, link_marker, link_suffix = user_makefile.rpartition(
                "--manifest $(PRODUCTION_SEED_MANIFEST)"
            )
            self.assertTrue(link_marker)
            _write(
                root / "user" / "Makefile",
                link_prefix
                + "--manifest $(BOOTSTRAP_SEED_MANIFEST)"
                + link_suffix,
            )
            with self.assertRaisesRegex(
                module.AuditError,
                "user wrapper binding differs",
            ):
                module._validate_cupidc_production_make_bindings(
                    root,
                    "make",
                    models,
                )

    def test_cupidc_ownership_provenance_tracks_wrapper_drift(self):
        module = _load_audit_module()
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for relative in (
                "Makefile",
                *module.CUPIDC_KERNEL_CONTROL_FILES,
            ):
                _write(root / relative, f"{relative}\n")
            model = module.BuildModel(
                directory=".",
                root_target="all",
                rules={},
                reachable=set(),
                direct_sources=set(),
                generated_sources=set(),
                forced_sources=set(),
                includes_by_source={},
                include_search_paths=[],
                transforms=[
                    {
                        "tools": ["cupid_c_compiler", "host_python"],
                    }
                ],
            )

            first = module._provenance(root, [model], [])
            wrapper = root / "tools" / "cupidc_kernel_compile.py"
            _write(wrapper, "changed wrapper\n")
            second = module._provenance(root, [model], [])
            first_controls = {
                entry["path"]: entry["sha256"]
                for entry in first["control_files"]
            }
            second_controls = {
                entry["path"]: entry["sha256"]
                for entry in second["control_files"]
            }

            self.assertNotEqual(
                first_controls["tools/cupidc_kernel_compile.py"],
                second_controls["tools/cupidc_kernel_compile.py"],
            )
            self.assertEqual(
                first_controls["bootstrap/seeds/i386-windows/manifest.json"],
                second_controls["bootstrap/seeds/i386-windows/manifest.json"],
            )

    def test_supported_audit_targets_check_and_consume_active_manifest(self):
        root_makefile = (REPO_ROOT / "Makefile").read_text(encoding="utf-8")
        toolchain_makefile = TOOLCHAIN_MAKEFILE.read_text(encoding="utf-8")

        self.assertEqual(
            root_makefile.count("--c-preprocessor-active-cases "), 3
        )
        self.assertIn(
            "BOOTSTRAP_CUPIDC_ACTIVE_CASES := "
            "toolchain/tests/cupidc_pp_active_cases.inc",
            root_makefile,
        )
        self.assertIn("$(CUPIDC_PP_ACTIVE_CASES) \\", toolchain_makefile)
        self.assertIn(
            '#include "cupidc_pp_active_cases.inc"',
            CUPIDC_PP_CONTRACT.read_text(encoding="utf-8"),
        )
