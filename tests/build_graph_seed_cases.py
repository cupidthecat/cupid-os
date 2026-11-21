## @file
#  @brief Check checked seed recipes, profiles, and generated sources.
import ast
import json
import re
import shutil
import tempfile
from pathlib import Path
from unittest import mock
from tests.build_graph_audit_fixtures import (
    ACTIVE_BUILD_MANIFEST,
    ACTIVE_CASE_MANIFEST,
    LINUX_BOOTSTRAP_SEED_INPUTS,
    REPO_ROOT,
    WINDOWS_PRODUCTION_SEED_INPUTS,
    _load_audit_module,
    _write,
)


class SeedDeliveryCases:
    def test_checked_cupidc_active_manifest_has_exact_source_cohorts(self):
        lines = ACTIVE_CASE_MANIFEST.read_text(encoding="utf-8").splitlines()
        profile_pattern = re.compile(
            r"^CUPIDC_PP_PROFILE\(([A-Z0-9_]+), ([A-Z0-9_]+), "
            r"(CTOOL_(?:TRUE|FALSE)), (CTOOL_(?:TRUE|FALSE)), "
            r"(CTOOL_(?:TRUE|FALSE)), (CTOOL_(?:TRUE|FALSE))\)$"
        )
        active_pattern = re.compile(
            r'^CUPIDC_PP_ACTIVE_CASE\(([A-Z0-9_]+), "([^"]+)"\)$'
        )
        generated_pattern = re.compile(
            r'^CUPIDC_PP_GENERATED_CASE\(([A-Z0-9_]+), "([^"]+)"\)$'
        )
        profiles = [
            match.groups()
            for line in lines
            if (match := profile_pattern.fullmatch(line)) is not None
        ]
        active = [
            match.groups()
            for line in lines
            if (match := active_pattern.fullmatch(line)) is not None
        ]
        generated = [
            match.groups()
            for line in lines
            if (match := generated_pattern.fullmatch(line)) is not None
        ]

        self.assertEqual(
            profiles,
            [
                (
                    "KERNEL_I386",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
                (
                    "DOOM_COMPAT_I386",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                    "CTOOL_TRUE",
                    "CTOOL_TRUE",
                ),
                (
                    "DOOM_TREE_I386",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                    "CTOOL_TRUE",
                    "CTOOL_TRUE",
                ),
                (
                    "USER_I386",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
                (
                    "FREESTANDING_I386",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
                (
                    "CUPID_RUNTIME",
                    "CTOOL_C_PP_MODE_CUPID",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
                (
                    "HOSTED_TOOLCHAIN_64",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_FALSE",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
                (
                    "HOSTED_KERNEL_BRIDGE_64",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_FALSE",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
                (
                    "HOSTED_I386_LINUX",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_FALSE",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
                (
                    "HOSTED_I386_WINDOWS",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_FALSE",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
                (
                    "HOSTED_I386_KERNEL_BRIDGE",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_FALSE",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
                (
                    "HOSTED_I386_LINUX_GNU",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_TRUE",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
                (
                    "HOSTED_I386_WINDOWS_UTF8",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_TRUE",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
                (
                    "HOSTED_I386_WINDOWS_PUBLICATION",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_TRUE",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
                (
                    "HOSTED_I386_WINDOWS_BUILD",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_TRUE",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
                (
                    "HOSTED_I386_WINDOWS_UTF8_GNU",
                    "CTOOL_C_PP_MODE_C11",
                    "CTOOL_TRUE",
                    "CTOOL_TRUE",
                    "CTOOL_FALSE",
                    "CTOOL_FALSE",
                ),
            ],
        )
        self.assertEqual(
            {
                name: sum(case_name == name for case_name, _ in active)
                for name, _, _, _, _, _ in profiles
            },
            {
                "KERNEL_I386": 156,
                "DOOM_COMPAT_I386": 3,
                "DOOM_TREE_I386": 80,
                "USER_I386": 3,
                "FREESTANDING_I386": 1,
                "CUPID_RUNTIME": 108,
                "HOSTED_TOOLCHAIN_64": 0,
                "HOSTED_KERNEL_BRIDGE_64": 0,
                "HOSTED_I386_LINUX": 59,
                "HOSTED_I386_WINDOWS": 9,
                "HOSTED_I386_KERNEL_BRIDGE": 2,
                "HOSTED_I386_LINUX_GNU": 3,
                "HOSTED_I386_WINDOWS_UTF8": 1,
                "HOSTED_I386_WINDOWS_PUBLICATION": 1,
                "HOSTED_I386_WINDOWS_BUILD": 1,
                "HOSTED_I386_WINDOWS_UTF8_GNU": 1,
            },
        )
        self.assertEqual(len(active), 428)
        for expected in (
            ("KERNEL_I386", "/kernel/core/kernel.cc"),
            ("KERNEL_I386", "/kernel/audio/memio.cc"),
            ("KERNEL_I386", "/kernel/audio/mus2midi.cc"),
            ("KERNEL_I386", "/kernel/audio/nuked_opl3.cc"),
            ("DOOM_TREE_I386", "/kernel/doom/i_sound_cupidos.cc"),
            ("DOOM_TREE_I386", "/kernel/doom/src/d_main.cc"),
            ("USER_I386", "/user/examples/hello.cc"),
            (
                "FREESTANDING_I386",
                "/toolchain/tests/hosted_i386_windows_contract.cc",
            ),
            ("CUPID_RUNTIME", "/bin/browser.cc"),
            ("HOSTED_I386_LINUX", "/toolchain/ctool.cc"),
            ("HOSTED_I386_LINUX", "/toolchain/cupidc_emit.cc"),
            ("HOSTED_I386_LINUX", "/toolchain/cupidc_ir.cc"),
            ("HOSTED_I386_LINUX", "/toolchain/x86.cc"),
            ("HOSTED_I386_KERNEL_BRIDGE", "/kernel/lang/as_elf.cc"),
            ("HOSTED_I386_LINUX", "/toolchain/ctool_host.cc"),
            ("HOSTED_I386_LINUX", "/toolchain/cupidc_main.cc"),
            ("HOSTED_I386_WINDOWS", "/toolchain/ctool_host.cc"),
            ("HOSTED_I386_WINDOWS", "/toolchain/cupidasm_main.cc"),
            ("HOSTED_I386_WINDOWS", "/toolchain/cupidc_main.cc"),
            ("HOSTED_I386_WINDOWS", "/toolchain/cupidld_main.cc"),
            ("HOSTED_I386_WINDOWS", "/toolchain/cupidobj_main.cc"),
            (
                "HOSTED_I386_WINDOWS",
                "/toolchain/hosted/i386-windows/publication_runtime.cc",
            ),
            (
                "HOSTED_I386_LINUX",
                "/toolchain/tests/cupidc_object_contract.cc",
            ),
            (
                "HOSTED_I386_LINUX",
                "/toolchain/tests/user_syscall_abi_contract.cc",
            ),
            (
                "HOSTED_I386_LINUX_GNU",
                "/toolchain/tests/hosted_i386_runtime_contract.cc",
            ),
            (
                "HOSTED_I386_KERNEL_BRIDGE",
                "/toolchain/tests/cupidasm_kernel_elf_contract.cc",
            ),
            (
                "HOSTED_I386_LINUX_GNU",
                "/toolchain/hosted/i386-linux/runtime.cc",
            ),
            (
                "HOSTED_I386_LINUX_GNU",
                "/toolchain/hosted/i386-windows/runtime.cc",
            ),
            (
                "HOSTED_I386_LINUX",
                "/toolchain/tests/hosted_i386_windows_runtime_contract.cc",
            ),
        ):
            self.assertIn(expected, active)
        self.assertNotIn(
            (
                "HOSTED_I386_LINUX",
                "/toolchain/tests/hosted_i386_runtime_contract.c",
            ),
            active,
        )
        self.assertNotIn(
            ("HOSTED_I386_LINUX", "/toolchain/tests/artifact_size_policy_contract.cc"),
            active,
        )
        self.assertTrue((REPO_ROOT / "toolchain/tests/artifact_size_policy_contract.cc").is_file())
        self.assertNotIn(
            ("DOOM_COMPAT_I386", "/kernel/audio/memio.cc"),
            active,
        )
        self.assertNotIn(
            ("DOOM_COMPAT_I386", "/kernel/audio/mus2midi.cc"),
            active,
        )
        self.assertEqual(
            generated,
            [
                ("KERNEL_I386", "/kernel/cpu/ksyms_data.cc"),
                ("KERNEL_I386", "/kernel/util/bin_programs_gen.cc"),
                ("KERNEL_I386", "/kernel/util/demos_programs_gen.cc"),
                ("KERNEL_I386", "/kernel/util/docs_programs_gen.cc"),
            ],
        )

    def test_cupidobj_profile_manifest_is_a_checked_generator(self):
        module = _load_audit_module()
        target = module._CUPIDOBJ_PROFILE_MANIFEST_OUTPUT
        expected_inputs = module._cupidobj_profile_manifest_expected_inputs(
            REPO_ROOT
        )
        delivery = module._build_transforms(
            ".",
            {target},
            {
                target: module.MakeRule(
                    prerequisites=expected_inputs,
                    recipe=list(module._CUPIDOBJ_PROFILE_MANIFEST_RECIPE),
                )
            },
        )[0]

        self.assertEqual(delivery["operation"], "generate_profile_manifest")
        self.assertEqual(delivery["tools"], ["cupid_builder", "cupid_object"])
        self.assertEqual(delivery["inputs"], expected_inputs)
        self.assertIn("toolchain/fat16_name_profiles.inc", delivery["inputs"])
        module._validate_cupidobj_profile_manifest_delivery(
            REPO_ROOT,
            [delivery],
        )

        seed_inputs = {
            "Makefile",
            *WINDOWS_PRODUCTION_SEED_INPUTS,
        }
        self.assertTrue(seed_inputs.issubset(delivery["inputs"]))

        changes = {
            "missing delivery": [],
            "wrong output": [{**delivery, "output": "build/profile.json"}],
            "wrong operation": [
                {**delivery, "operation": "host_orchestration"}
            ],
            "wrong tools": [{**delivery, "tools": ["host_python"]}],
            "changed recipe": [
                {
                    **delivery,
                    "recipe": [
                        "$(PYTHON) tools/cupidc_kernel_compile.py --root . \\",
                        "--write-profile-input-manifest $@",
                    ],
                }
            ],
            "missing seed": [
                {
                    **delivery,
                    "inputs": [
                        path
                        for path in delivery["inputs"]
                        if path != "bootstrap/seeds/i386-windows/cupidobj.exe"
                    ],
                }
            ],
            "missing builder": [
                {
                    **delivery,
                    "inputs": [
                        path
                        for path in delivery["inputs"]
                        if path != "bootstrap/seeds/i386-windows/cupidbuild.exe"
                    ],
                }
            ],
            "duplicate input": [
                {**delivery, "inputs": [*delivery["inputs"], "Makefile"]}
            ],
            "missing profile input": [
                {
                    **delivery,
                    "inputs": [
                        path
                        for path in delivery["inputs"]
                        if path != "kernel/doom/src/doom.h"
                    ],
                }
            ],
            "missing Unicode name table": [
                {
                    **delivery,
                    "inputs": [
                        path for path in delivery["inputs"]
                        if path != "toolchain/fat16_name_profiles.inc"
                    ],
                }
            ],
            "unexpected input": [
                {
                    **delivery,
                    "inputs": [*delivery["inputs"], "build/untracked.input"],
                }
            ],
        }
        for name, changed in changes.items():
            with (
                self.subTest(name=name),
                self.assertRaisesRegex(
                    module.AuditError,
                    r"CupidObj profile manifest",
                ),
            ):
                module._validate_cupidobj_profile_manifest_delivery(
                    REPO_ROOT,
                    changed,
                )

        near_match = module._build_transforms(
            ".",
            {target},
            {
                target: module.MakeRule(
                    prerequisites=expected_inputs,
                    recipe=[
                        "$(PYTHON) tools/cupidc_kernel_compile.py --root . "
                        "--write-profile-input-manifests $@"
                    ],
                )
            },
        )[0]
        self.assertNotEqual(
            near_match["operation"],
            "generate_profile_manifest",
        )
        self.assertEqual(near_match["operation"], "host_orchestration")
        self.assertEqual(near_match["tools"], ["host_python"])

    def test_real_profile_make_rule_captures_the_unicode_name_table(self):
        module = _load_audit_module()
        make = shutil.which("make")
        self.assertIsNotNone(make)
        rules = module._parse_make_rules(module._run_make_database(REPO_ROOT, make, "all"))
        target = module._CUPIDOBJ_PROFILE_MANIFEST_OUTPUT
        expected = module._cupidobj_profile_manifest_expected_inputs(REPO_ROOT)
        delivery = module._build_transforms(".", {target}, rules)[0]
        self.assertEqual(delivery["inputs"], expected)
        self.assertIn("toolchain/fat16_name_profiles.inc", delivery["inputs"])
        module._validate_cupidobj_profile_manifest_delivery(REPO_ROOT, [delivery])

    def test_cupidobj_profile_manifest_tracks_nested_profile_headers(self):
        module = _load_audit_module()
        wrapper_source = (
            REPO_ROOT / "tools" / "cupidc_kernel_compile.py"
        ).read_text(encoding="utf-8")
        current_inputs = module._cupidobj_profile_manifest_expected_inputs(
            REPO_ROOT
        )

        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for relative in current_inputs:
                if Path(relative).suffix in {".h", ".inc"}:
                    (root / relative).parent.mkdir(parents=True, exist_ok=True)
            _write(
                root / "tools" / "cupidc_kernel_compile.py",
                wrapper_source,
            )
            _write(root / "toolchain" / "cupidobj.cc", "// fixture\n")
            nested_header = (
                "kernel/doom/src/include_stubs/sys/nested/profile.h"
            )
            _write(root / nested_header, "#define PROFILE_NESTED 1\n")
            _write(
                root / "Makefile",
                """
                CUPIDC_KERNEL_COMPILE := $(PYTHON) tools/cupidc_kernel_compile.py --root .
                KERNEL=kernel/kernel.bin
                OS_IMAGE=cupidos.img
                override DOOM_CUPIDC_INPUT_MANIFEST := build/bootstrap/doom-cupidc-inputs.json
                $(DOOM_CUPIDC_INPUT_MANIFEST): FORCE $(DOOM_CUPIDC_HEADERS) \
                    Makefile $(PRODUCTION_SEED_INPUTS)
                ifneq ($(OS),Windows_NT)
                \ttest ! -L build && test ! -L build/bootstrap && mkdir -p build/bootstrap
                endif
                \t$(PRODUCTION_SEED_DIRECTORY)cupidbuild.$(PRODUCTION_SEED_SUFFIX) generate-profile-manifest \
                \t\t--seed-manifest $(PRODUCTION_SEED_MANIFEST) --root "$(CURDIR)" \
                  \t\t--seed-release $(PRODUCTION_SEED_RELEASE) \
                \t\t--output $@
                BOOTSTRAP_ARTIFACTS := kernel/doom/unit.o
                $(filter kernel/% drivers/% toolchain/%,$(BOOTSTRAP_ARTIFACTS)) \
                    kernel/util/bin_programs_gen.cc kernel/util/docs_programs_gen.cc \
                    kernel/util/demos_programs_gen.cc: | $(DOOM_CUPIDC_INPUT_MANIFEST)
                """,
            )

            expected_inputs = (
                module._cupidobj_profile_manifest_expected_inputs(root)
            )
            self.assertIn(nested_header, expected_inputs)
            delivery = {
                "output": module._CUPIDOBJ_PROFILE_MANIFEST_OUTPUT,
                "operation": "generate_profile_manifest",
                "tools": ["cupid_builder", "cupid_object"],
                "recipe": list(module._CUPIDOBJ_PROFILE_MANIFEST_RECIPE),
                "inputs": [
                    path for path in expected_inputs if path != nested_header
                ],
            }
            with self.assertRaisesRegex(
                module.AuditError,
                rf"CupidObj profile manifest inputs changed.*{re.escape(nested_header)}",
            ):
                module._validate_cupidobj_profile_manifest_delivery(
                    root,
                    [delivery],
                )

    def test_cupidobj_profile_manifest_cannot_disappear_from_production_root(
        self,
    ):
        module = _load_audit_module()
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            _write(root / "Makefile", ".PHONY: all\nall:\n")
            module._validate_cupidobj_profile_manifest_delivery(root, [])

            _write(
                root / "Makefile",
                """
                CUPIDC_KERNEL_COMPILE := $(PYTHON) tools/cupidc_kernel_compile.py --root .
                KERNEL=kernel/renamed.bin
                OS_IMAGE=cupidos.img
                .PHONY: all
                all:
                """,
            )
            _write(
                root / "tools" / "cupidc_kernel_compile.py",
                "# production wrapper fixture\n",
            )
            _write(root / "toolchain" / "cupidobj.cc", "// fixture\n")
            _write(root / "CONTEXT.md", "# Cupid OS\n")
            _write(
                root / "docs" / "bootstrap" / "README.md",
                "# Bootstrap\n",
            )
            (root / "kernel" / "doom" / "src").mkdir(
                parents=True,
                exist_ok=True,
            )

            with self.assertRaisesRegex(
                module.AuditError,
                r"CupidObj profile manifest Make contract changed",
            ):
                module._validate_cupidobj_profile_manifest_delivery(root, [])

    def test_cupidobj_profile_manifest_make_contract_rejects_drift(self):
        module = _load_audit_module()
        source = (REPO_ROOT / "Makefile").read_text(encoding="utf-8")
        mutations = {
            "renamed output": (
                "DOOM_CUPIDC_INPUT_MANIFEST := build/bootstrap/doom-cupidc-inputs.json",
                "DOOM_CUPIDC_INPUT_MANIFEST := build/bootstrap/doom-inputs.json",
            ),
            "unchecked prerequisites": (
                "$(DOOM_CUPIDC_INPUT_MANIFEST): FORCE $(DOOM_CUPIDC_HEADERS) \\\n"
                "\tMakefile $(PRODUCTION_SEED_INPUTS)",
                "$(DOOM_CUPIDC_INPUT_MANIFEST): FORCE $(DOOM_CUPIDC_HEADERS)",
            ),
            "unpinned seed manifest": (
                "$(PRODUCTION_SEED_SUFFIX) generate-profile-manifest \\\n"
                '\t\t--seed-manifest $(PRODUCTION_SEED_MANIFEST) --root "$(CURDIR)"',
                "$(PRODUCTION_SEED_SUFFIX) generate-profile-manifest \\\n"
                '\t\t--seed-manifest bootstrap/unchecked.json --root "$(CURDIR)"',
            ),
            "different publisher mode": (
                "$(PRODUCTION_SEED_SUFFIX) generate-profile-manifest \\",
                "$(PRODUCTION_SEED_SUFFIX) run \\",
            ),
            "missing POSIX parents": (
                "\ttest ! -L build && test ! -L build/bootstrap && mkdir -p build/bootstrap\n",
                "",
            ),
            "unguarded POSIX parents": (
                "test ! -L build && test ! -L build/bootstrap && mkdir -p build/bootstrap",
                "mkdir -p build/bootstrap",
            ),
            "unprotected build symlink": (
                "test ! -L build && test ! -L build/bootstrap",
                "test ! -L build/bootstrap",
            ),
            "unprotected bootstrap symlink": (
                "test ! -L build && test ! -L build/bootstrap",
                "test ! -L build",
            ),
            "parent platform reversed": (
                "ifneq ($(OS),Windows_NT)\n\ttest ! -L build",
                "ifeq ($(OS),Windows_NT)\n\ttest ! -L build",
            ),
            "overridable output": (
                "override DOOM_CUPIDC_INPUT_MANIFEST :=",
                "DOOM_CUPIDC_INPUT_MANIFEST :=",
            ),
        }
        module._validate_cupidobj_profile_manifest_make_source(source)
        normalized = "\n".join(module._normalized_make_lines(source))
        barrier = module._CUPIDOBJ_PROFILE_WRITER_BARRIER
        for member in (
            "kernel/%", "drivers/%", "toolchain/%",
            "kernel/util/bin_programs_gen.cc",
            "kernel/util/docs_programs_gen.cc",
            "kernel/util/demos_programs_gen.cc", "|",
        ):
            with self.subTest(missing_writer_barrier=member):
                changed = normalized.replace(
                    barrier, barrier.replace(member, "")
                )
                with self.assertRaisesRegex(
                    module.AuditError, "Make contract changed"
                ):
                    module._validate_cupidobj_profile_manifest_make_source(changed)
        early = normalized.replace(barrier, "").replace(
            "BOOTSTRAP_ARTIFACTS :=", barrier + "\nBOOTSTRAP_ARTIFACTS :=", 1
        )
        with self.assertRaisesRegex(module.AuditError, "complete artifact cohort"):
            module._validate_cupidobj_profile_manifest_make_source(early)
        for name, (old, new) in mutations.items():
            with self.subTest(name=name):
                self.assertEqual(source.count(old), 1)
                changed = source.replace(old, new, 1)
                with self.assertRaisesRegex(
                    module.AuditError,
                    r"CupidObj profile manifest Make contract changed",
                ):
                    module._validate_cupidobj_profile_manifest_make_source(
                        changed
                    )

    def test_checked_seed_runner_contract_rejects_drift(self):
        module = _load_audit_module()
        module._validate_checked_seed_runner_contract(REPO_ROOT)
        sources = {
            relative: (REPO_ROOT / relative).read_text(encoding="utf-8")
            for relative in module._CHECKED_SEED_RUNNER_FILES
        }
        mutations = (
            (
                "optional runner removed",
                "tools/bootstrap_toolchain.py",
                "runner: ToolRunner | None = None,",
                "runner: ToolRunner | None = ToolRunner,",
            ),
            (
                "live cohort not reloaded",
                "tools/bootstrap_toolchain.py",
                "live_seed = _load_seed_inputs(manifest_path, None)",
                "live_seed = _load_seed_inputs(manifest_path, seed_inputs)",
            ),
            (
                "kernel runner not forwarded",
                "tools/cupidc_kernel_compile.py",
                "                        runner=executor,",
                "                        runner=None,",
            ),
            (
                "production capture not forwarded",
                "tools/cupidc_production_compile.py",
                "                        frozen_seed=seed_inputs,",
                "                        frozen_seed=None,",
            ),
            (
                "linker selection changed",
                "tools/cupidld_user_link.py",
                '                        "cupidld",',
                '                        "cupidc",',
            ),
            (
                "user disassembler selection changed",
                "tools/cupidld_user_link.py",
                '                        "cupiddis",',
                '                        "cupidobj",',
            ),
            (
                "user disassembler left unreachable",
                "tools/cupidld_user_link.py",
                "            if native_snapshot is None:\n"
                "                try:\n"
                "                    inspected = run_seed_tool(",
                "            if False and native_snapshot is None:\n"
                "                try:\n"
                "                    inspected = run_seed_tool(",
            ),
            (
                "user disassembler status ignored",
                "tools/cupidld_user_link.py",
                "                if inspected.returncode != 0:\n",
                "                if False and inspected.returncode != 0:\n",
            ),
            (
                "user disassembler stdout ignored",
                "tools/cupidld_user_link.py",
                "                if inspected.stdout:\n",
                "                if False and inspected.stdout:\n",
            ),
            (
                "user disassembler stderr ignored",
                "tools/cupidld_user_link.py",
                "                if inspected.stderr:\n",
                "                if False and inspected.stderr:\n",
            ),
            (
                "user disassembler candidate recheck removed",
                "tools/cupidld_user_link.py",
                "                inspected_payload, inspected_identity = (\n"
                "                    _capture_private_candidate(",
                "                inspected_payload, inspected_identity = (\n"
                "                    _capture_unchecked_candidate(",
            ),
            (
                "user disassembler candidate comparison disabled",
                "tools/cupidld_user_link.py",
                "                if (\n"
                "                    inspected_identity != candidate_identity",
                "                if False and (\n"
                "                    inspected_identity != candidate_identity",
            ),
            (
                "user publication bytes reloaded from a mutable path",
                "tools/cupidld_user_link.py",
                "            publication_output.write_bytes(candidate_payload)",
                "            publication_output.write_bytes("
                "temporary_output.read_bytes())",
            ),
            (
                "user publication bypasses the frozen payload",
                "tools/cupidld_user_link.py",
                "            os.replace(publication_output, output)",
                "            os.replace(temporary_output, output)",
            ),
            (
                "injected runner left as a dead marker",
                "tools/bootstrap_toolchain.py",
                "            result = active_runner.run("
                "executable, arguments, timeout)",
                "            if False:\n"
                "                active_runner.run("
                "executable, arguments, timeout)\n"
                "            result = ToolRunner(root).run("
                "executable, arguments, timeout)",
            ),
            (
                "live seed reload left as a dead marker",
                "tools/bootstrap_toolchain.py",
                "            live_seed = _load_seed_inputs(manifest_path, None)",
                "            if False:\n"
                "                _load_seed_inputs(manifest_path, None)\n"
                "            live_seed = seed_inputs",
            ),
            (
                "manifest mismatch does not raise",
                "tools/bootstrap_toolchain.py",
                "            raise BootstrapError(\n"
                '                f"checked seed inputs changed while '
                '{display_name} ran: "\n'
                '                "manifest content differs"\n'
                "            )",
                "            pass",
            ),
            (
                "frozen seed rebound from live inputs",
                "tools/cupidc_kernel_compile.py",
                "                try:\n"
                "                    result = run_seed_tool(\n"
                "                        manifest_path,\n"
                "                        root,\n"
                '                        "cupidc",',
                "                seed_inputs = verify_seed_inputs("
                "manifest_path)\n"
                "                try:\n"
                "                    result = run_seed_tool(\n"
                "                        manifest_path,\n"
                "                        root,\n"
                '                        "cupidc",',
            ),
            (
                "kernel delegation left in dead code",
                "tools/cupidc_kernel_compile.py",
                "                    result = run_seed_tool(\n"
                "                        manifest_path,\n"
                "                        root,\n"
                '                        "cupidc",\n'
                "                        arguments,\n"
                "                        timeout=timeout,\n"
                "                        frozen_seed=seed_inputs,\n"
                "                        runner=executor,\n"
                "                    )",
                "                    if False:\n"
                "                        result = run_seed_tool(\n"
                "                            manifest_path,\n"
                "                            root,\n"
                '                            "cupidc",\n'
                "                            arguments,\n"
                "                            timeout=timeout,\n"
                "                            frozen_seed=seed_inputs,\n"
                "                            runner=executor,\n"
                "                        )\n"
                "                    result = (\n"
                "                        executor\n"
                "                        if executor is not None\n"
                "                        else ToolRunner(root)\n"
                "                    ).run(\n"
                '                        seed_inputs.tools["cupidc"],\n'
                "                        arguments,\n"
                "                        timeout,\n"
                "                    )",
            ),
            (
                "outer frozen dispatch bypasses checked helper",
                "tools/bootstrap_toolchain.py",
                "    if frozen_seed is not None:\n"
                "        return run_frozen(frozen_seed)",
                "    if frozen_seed is not None:\n"
                "        return (\n"
                "            runner\n"
                "            if runner is not None\n"
                "            else ToolRunner(root)\n"
                "        ).run(\n"
                "            frozen_seed.tools[tool_name],\n"
                "            arguments,\n"
                "            timeout,\n"
                "        )",
            ),
            (
                "frozen seed tool map mutated in place",
                "tools/cupidc_kernel_compile.py",
                "                try:\n"
                "                    result = run_seed_tool(\n"
                "                        manifest_path,\n"
                "                        root,\n"
                '                        "cupidc",',
                '                seed_inputs.tools["cupidc"] = (\n'
                "                    verify_seed_inputs("
                'manifest_path).tools["cupidc"]\n'
                "                )\n"
                "                try:\n"
                "                    result = run_seed_tool(\n"
                "                        manifest_path,\n"
                "                        root,\n"
                '                        "cupidc",',
            ),
            (
                "publication moved into an exception handler",
                "tools/cupidc_kernel_compile.py",
                "                _replace_with_retry(temporary_output, output)",
                "                try:\n"
                "                    pass\n"
                "                except Exception:\n"
                "                    _replace_with_retry("
                "temporary_output, output)",
            ),
            (
                "wrapper returns before checked execution",
                "tools/cupidc_kernel_compile.py",
                '    """Compile one approved source and atomically publish '
                'a checked object."""',
                '    """Compile one approved source and atomically publish '
                'a checked object."""\n'
                "    return",
            ),
            (
                "shared runner shadowed by a local unchecked adapter",
                "tools/cupidc_kernel_compile.py",
                "                try:\n"
                "                    result = run_seed_tool(\n"
                "                        manifest_path,\n"
                "                        root,\n"
                '                        "cupidc",',
                "                run_seed_tool = lambda manifest_path, "
                "root, tool, arguments, **kwargs: (\n"
                "                    executor\n"
                "                    if executor is not None\n"
                "                    else ToolRunner(root)\n"
                "                ).run(\n"
                '                    kwargs["frozen_seed"].tools[tool],\n'
                "                    arguments,\n"
                '                    kwargs["timeout"],\n'
                "                )\n"
                "                try:\n"
                "                    result = run_seed_tool(\n"
                "                        manifest_path,\n"
                "                        root,\n"
                '                        "cupidc",',
            ),
            (
                "shared runner replaced through the module namespace",
                "tools/cupidc_kernel_compile.py",
                "                try:\n"
                "                    result = run_seed_tool(\n"
                "                        manifest_path,\n"
                "                        root,\n"
                '                        "cupidc",',
                "                globals().__setitem__(\n"
                '                    "run_seed_tool",\n'
                "                    lambda manifest_path, root, tool, "
                "arguments, **kwargs: (\n"
                "                        executor\n"
                "                        if executor is not None\n"
                "                        else ToolRunner(root)\n"
                "                    ).run(\n"
                '                        kwargs["frozen_seed"].tools[tool],\n'
                "                        arguments,\n"
                '                        kwargs["timeout"],\n'
                "                    ),\n"
                "                )\n"
                "                try:\n"
                "                    result = run_seed_tool(\n"
                "                        manifest_path,\n"
                "                        root,\n"
                '                        "cupidc",',
            ),
            (
                "frozen tool map replaced from the live seed",
                "tools/bootstrap_toolchain.py",
                "    def run_frozen(seed_inputs: SeedInputs) -> "
                "subprocess.CompletedProcess[str]:\n"
                "        try:\n"
                "            executable = seed_inputs.tools[tool_name]",
                "    def run_frozen(seed_inputs: SeedInputs) -> "
                "subprocess.CompletedProcess[str]:\n"
                "        seed_inputs.tools[tool_name] = "
                "_load_seed_inputs(\n"
                "            manifest_path, None\n"
                "        ).tools[tool_name]\n"
                "        try:\n"
                "            executable = seed_inputs.tools[tool_name]",
            ),
            (
                "wrapper converted into an inert generator",
                "tools/cupidc_kernel_compile.py",
                '    """Compile one approved source and atomically publish '
                'a checked object."""',
                '    """Compile one approved source and atomically publish '
                'a checked object."""\n'
                "    if False:\n"
                "        yield None",
            ),
            (
                "shared runner replaced by a decorator",
                "tools/bootstrap_toolchain.py",
                "def run_seed_tool(\n",
                "@unchecked_run_seed_tool\ndef run_seed_tool(\n",
            ),
            (
                "candidate published through an early alias",
                "tools/cupidc_kernel_compile.py",
                "                    raise KernelCompileError(str(error)) "
                "from error\n"
                "                if result.returncode != 0:",
                "                    raise KernelCompileError(str(error)) "
                "from error\n"
                "                publish_unchecked = _replace_with_retry\n"
                "                publish_unchecked(temporary_output, output)\n"
                "                if result.returncode != 0:",
            ),
            (
                "live seed reload failure returns tool success",
                "tools/bootstrap_toolchain.py",
                "        except BootstrapError as error:\n"
                "            raise BootstrapError(\n"
                '                f"checked seed inputs changed while "\n'
                '                f"{display_name} ran: {error}"\n'
                "            ) from error",
                "        except BootstrapError:\n            return result",
            ),
            (
                "seed freezer shadowed by a live verifier",
                "tools/cupidc_kernel_compile.py",
                "            try:\n"
                "                seed_inputs = freeze_seed_inputs(\n"
                "                    manifest_path, Path(seed_temporary)\n"
                "                )",
                "            freeze_seed_inputs = lambda manifest_path, "
                "directory: verify_seed_inputs(manifest_path)\n"
                "            try:\n"
                "                seed_inputs = freeze_seed_inputs(\n"
                "                    manifest_path, Path(seed_temporary)\n"
                "                )",
            ),
            (
                "exported runner rebound after its checked definition",
                "tools/bootstrap_toolchain.py",
                "\n\ndef _build_stage(\n",
                "\n\nrun_seed_tool = unchecked_seed_tool\n\n\ndef _build_stage(\n",
            ),
            (
                "candidate published through Path.replace before validation",
                "tools/cupidc_kernel_compile.py",
                "                    raise KernelCompileError(str(error)) "
                "from error\n"
                "                if result.returncode != 0:",
                "                    raise KernelCompileError(str(error)) "
                "from error\n"
                "                temporary_output.replace(output)\n"
                "                if result.returncode != 0:",
            ),
            (
                "outer transaction handler suppresses failure",
                "tools/cupidc_kernel_compile.py",
                "    except OSError as error:\n"
                "        raise KernelCompileError(\n"
                '            f"could not publish kernel object {output}: '
                '{error}"\n'
                "        ) from error",
                "    except BaseException:\n        pass",
            ),
            (
                "linked kernel local-target validation removed",
                "tools/hostbuild.py",
                '                        "--require-local-targets",\n'
                '                        "--require-code-anchors",\n'
                '                        "kernel/kernel.elf.pass1",',
                '                        "--require-local-targets-removed",\n'
                '                        "--require-code-anchors",\n'
                '                        "kernel/kernel.elf.pass1",',
            ),
            (
                "linked kernel code-anchor validation removed",
                "tools/hostbuild.py",
                '                        "--require-code-anchors",\n'
                '                        "kernel/kernel.elf.pass1",',
                '                        "--require-code-anchors-removed",\n'
                '                        "kernel/kernel.elf.pass1",',
            ),
            (
                "CupidASM object code-anchor validation removed",
                "tools/hostbuild.py",
                '                        "--require-code-anchors",\n'
                "                        candidate_logical,",
                '                        "--require-code-anchors-removed",\n'
                "                        candidate_logical,",
            ),
            (
                "SMP source-edge validation removed",
                "tools/hostbuild.py",
                '                        "--require-source-edges",\n'
                "                        candidate_logical,",
                '                        "--require-source-edges-removed",\n'
                "                        candidate_logical,",
            ),
            (
                "boot source-edge validation removed",
                "tools/hostbuild.py",
                '                        "--require-source-edges",\n'
                '                        "--raw",',
                '                        "--require-source-edges-removed",\n'
                '                        "--raw",',
            ),
            (
                "linked kernel code validation left in dead code",
                "tools/hostbuild.py",
                "            try:\n                linked_validation = run_seed_tool(",
                "            try:\n"
                "                if False:\n"
                "                    linked_validation = run_seed_tool(",
            ),
            (
                "linked kernel code validation duplicated",
                "tools/hostbuild.py",
                "            try:\n                linked_validation = run_seed_tool(",
                "            try:\n"
                "                linked_validation = run_seed_tool(\n"
                "                    live_seed_manifest,\n"
                "                    private_root,\n"
                '                    "cupiddis",\n'
                "                    (\n"
                '                        "--require-known",\n'
                '                        "--require-local-targets",\n'
                '                        "--require-code-anchors",\n'
                '                        "kernel/kernel.elf.pass1",\n'
                '                        "kernel/kernel.elf",\n'
                "                    ),\n"
                "                    timeout=600,\n"
                "                    frozen_seed=frozen_seed,\n"
                "                )\n"
                "                linked_validation = run_seed_tool(",
            ),
            (
                "linked kernel status failure does not stop flattening",
                "tools/hostbuild.py",
                "            if linked_validation.returncode != 0:\n"
                "                return subprocess.CompletedProcess(",
                "            if linked_validation.returncode != 0:\n"
                "                subprocess.CompletedProcess(",
            ),
            (
                "linked kernel status guard left under a dead ancestor",
                "tools/hostbuild.py",
                "            if linked_validation.returncode != 0:\n"
                "                return subprocess.CompletedProcess(\n"
                "                    linked_validation.args,\n"
                "                    linked_validation.returncode,\n"
                '                    "",\n'
                "                    linked_stderr,\n"
                "                )",
                "            if False:\n"
                "                if linked_validation.returncode != 0:\n"
                "                    return subprocess.CompletedProcess(\n"
                "                        linked_validation.args,\n"
                "                        linked_validation.returncode,\n"
                '                        "",\n'
                "                        linked_stderr,\n"
                "                    )",
            ),
            (
                "linked kernel runner failure does not stop flattening",
                "tools/hostbuild.py",
                "            except BootstrapError as error:\n"
                "                raise CodeValidationError(\n"
                '                    "checked CupidDis linked-code validation ',
                "            except BootstrapError as error:\n"
                "                if False:\n"
                "                    raise CodeValidationError(\n"
                '                    "checked CupidDis linked-code validation ',
            ),
            (
                "linked kernel stdout failure does not stop flattening",
                "tools/hostbuild.py",
                "            if linked_validation.stdout:\n"
                "                raise CodeValidationError(",
                "            if linked_validation.stdout:\n"
                "                if False:\n"
                "                    raise CodeValidationError(",
            ),
            (
                "linked kernel stdout guard left under a swallowing handler",
                "tools/hostbuild.py",
                "            if linked_validation.stdout:\n"
                "                raise CodeValidationError(\n"
                '                    "checked CupidDis linked-code '
                'validation wrote unexpected "\n'
                '                    "standard output",\n'
                "                    tool_stderr=linked_stderr,\n"
                "                )",
                "            try:\n"
                "                if linked_validation.stdout:\n"
                "                    raise CodeValidationError(\n"
                '                        "checked CupidDis linked-code '
                'validation wrote unexpected "\n'
                '                        "standard output",\n'
                "                        tool_stderr=linked_stderr,\n"
                "                    )\n"
                "            except CodeValidationError:\n"
                "                pass",
            ),
            (
                "linked kernel stdout guard left under a swallowing exception group",
                "tools/hostbuild.py",
                "            if linked_validation.stdout:\n"
                "                raise CodeValidationError(\n"
                '                    "checked CupidDis linked-code '
                'validation wrote unexpected "\n'
                '                    "standard output",\n'
                "                    tool_stderr=linked_stderr,\n"
                "                )",
                "            try:\n"
                "                if linked_validation.stdout:\n"
                "                    raise CodeValidationError(\n"
                '                        "checked CupidDis linked-code '
                'validation wrote unexpected "\n'
                '                        "standard output",\n'
                "                        tool_stderr=linked_stderr,\n"
                "                    )\n"
                "            except* CodeValidationError:\n"
                "                pass",
            ),
            (
                "linked kernel stdout guard left under a suppressing context",
                "tools/hostbuild.py",
                "            if linked_validation.stdout:\n"
                "                raise CodeValidationError(\n"
                '                    "checked CupidDis linked-code '
                'validation wrote unexpected "\n'
                '                    "standard output",\n'
                "                    tool_stderr=linked_stderr,\n"
                "                )",
                "            with contextlib.suppress(CodeValidationError):\n"
                "                if linked_validation.stdout:\n"
                "                    raise CodeValidationError(\n"
                '                        "checked CupidDis linked-code '
                'validation wrote unexpected "\n'
                '                        "standard output",\n'
                "                        tool_stderr=linked_stderr,\n"
                "                    )",
            ),
            (
                "broad kernel validation drops the frozen seed",
                "tools/hostbuild.py",
                '                    **({"frozen_seed": frozen_seed} '
                "if frozen_seed is not None else {}),",
                "                    **{},",
            ),
            (
                "linked kernel validation loses its two-image budget",
                "tools/hostbuild.py",
                "                    timeout=600,\n"
                "                    frozen_seed=frozen_seed,",
                "                    timeout=300,\n"
                "                    frozen_seed=frozen_seed,",
            ),
            (
                "linked kernel input drift guard removed",
                "tools/hostbuild.py",
                "            _require_code_inputs_unchanged(\n"
                "                repository_root,\n"
                "                manifest_snapshot,\n"
                "                snapshots,\n"
                '                activity="CupidDis linked-code validation",',
                "            _require_code_inputs_unchanged_removed(\n"
                "                repository_root,\n"
                "                manifest_snapshot,\n"
                "                snapshots,\n"
                '                activity="CupidDis linked-code validation",',
            ),
            (
                "linked kernel seed drift guard removed",
                "tools/hostbuild.py",
                "            _require_code_seed_inputs_unchanged(\n"
                "                repository_root,\n"
                "                seed_snapshots,\n"
                "                tool_stderr=linked_stderr,\n"
                "            )\n"
                "            if linked_validation.stdout:",
                "            _require_code_seed_inputs_unchanged_removed(\n"
                "                repository_root,\n"
                "                seed_snapshots,\n"
                "                tool_stderr=linked_stderr,\n"
                "            )\n"
                "            if linked_validation.stdout:",
            ),
        )
        for name, relative, old, new in mutations:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                for source_relative, source in sources.items():
                    changed = source
                    if source_relative == relative:
                        self.assertEqual(source.count(old), 1)
                        changed = source.replace(old, new, 1)
                    _write(root / source_relative, changed)
                with self.assertRaisesRegex(
                    module.AuditError,
                    r"checked-seed runner contract changed",
                ):
                    module._validate_checked_seed_runner_contract(root)

    def test_checked_seed_runner_production_identity_uses_build_inputs(self):
        module = _load_audit_module()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for relative in (
                "Makefile",
                "toolchain/cupidobj.cc",
                "bootstrap/seeds/i386-linux/manifest.json",
            ):
                _write(root / relative, "fixture\n")
            for relative in ("kernel/doom/src", "user/examples"):
                (root / relative).mkdir(parents=True)
            _write(root / "CONTEXT.md", "# mutable documentation\n")
            self.assertTrue(
                module._is_checked_seed_runner_production_root(root)
            )
            (root / "CONTEXT.md").unlink()
            self.assertTrue(
                module._is_checked_seed_runner_production_root(root)
            )
            (root / "Makefile").unlink()
            self.assertFalse(
                module._is_checked_seed_runner_production_root(root)
            )

    def test_cupidobj_profile_manifest_wrapper_rejects_drift(self):
        module = _load_audit_module()
        source = (REPO_ROOT / "tools" / "cupidc_kernel_compile.py").read_text(
            encoding="utf-8"
        )
        start = source.index("def write_profile_input_manifest(")
        end = source.index("\ndef ", start + 4)
        publisher = source[start:end]
        mutations = {
            "no publication lock": (
                "publication_lock = _acquire_profile_manifest_lock(resolved)",
                "publication_lock = None",
            ),
            "unchecked seed": (
                "checked_seed = verify_seed_inputs(manifest_path)",
                "checked_seed = load_seed_inputs(manifest_path)",
            ),
            "live seed not rechecked": (
                "live_seed = verify_seed_inputs(manifest_path)",
                "live_seed = checked_seed",
            ),
            "wrong checked tool": ('"cupidobj",', '"cupidasm",'),
            "no parity gate": (
                "candidate_capture.payload != oracle_payload",
                "candidate_capture.payload == oracle_payload",
            ),
            "live profile not rechecked": (
                "_require_profile_inputs_unchanged(root, capture)",
                "pass  # profile inputs were not rechecked",
            ),
            "output not rechecked": (
                "_require_profile_output_unchanged(resolved, initial_output)",
                "pass  # output was not rechecked",
            ),
            "non-atomic publication": (
                "_replace_profile_candidate_with_retry(",
                "_replace_profile_candidate_without_checks(",
            ),
            "reordered live checks": (
                "            _require_profile_inputs_unchanged(root, capture)\n"
                "            _require_profile_directory_unchanged(output_directory)\n"
                "            _require_profile_output_unchanged(resolved, initial_output)",
                "            _require_profile_output_unchanged(resolved, initial_output)\n"
                "            _require_profile_directory_unchanged(output_directory)\n"
                "            _require_profile_inputs_unchanged(root, capture)",
            ),
            "late post-tool directory recheck": (
                "                _require_profile_directory_unchanged(output_directory)\n"
                "                _require_profile_file_unchanged(\n"
                "                    snapshot_capture,\n"
                '                    "CupidObj profile snapshot",\n'
                "                )",
                "                _require_profile_file_unchanged(\n"
                "                    snapshot_capture,\n"
                '                    "CupidObj profile snapshot",\n'
                "                )\n"
                "                _require_profile_directory_unchanged(output_directory)",
            ),
            "dead checked-tool marker": (
                "                    result = run_seed_tool(",
                '                    "result = run_seed_tool("\n'
                "                    result = run_unchecked_tool(",
            ),
        }
        module._validate_cupidobj_profile_manifest_wrapper(REPO_ROOT)
        for name, (old, new) in mutations.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                self.assertEqual(publisher.count(old), 1)
                changed_publisher = publisher.replace(old, new, 1)
                root = Path(td)
                _write(
                    root / "tools" / "cupidc_kernel_compile.py",
                    source[:start] + changed_publisher + source[end:],
                )
                with self.assertRaisesRegex(
                    module.AuditError,
                    r"CupidObj profile manifest wrapper .* changed",
                ):
                    module._validate_cupidobj_profile_manifest_wrapper(root)

    def test_cupidobj_install_source_is_a_checked_delivery_generator(self):
        module = _load_audit_module()
        audit = json.loads(ACTIVE_BUILD_MANIFEST.read_text(encoding="utf-8"))
        expected_content = module._cupidobj_install_source_expected_content(
            audit["build"]["transforms"]
        )
        bin_content = expected_content["kernel/util/bin_programs_gen.cc"]
        self.assertIn("bin/feature13_derived_aot.cc", bin_content)
        self.assertEqual(
            len(
                {
                    path
                    for path in bin_content
                    if path.startswith("bin/")
                    and path.count("/") == 1
                    and path.endswith(".cc")
                }
            ),
            108,
        )
        checked_inputs = [
            "Makefile",
            *WINDOWS_PRODUCTION_SEED_INPUTS,
        ]
        ignored_inputs = {
            *checked_inputs,
            *LINUX_BOOTSTRAP_SEED_INPUTS,
            "tools/hostbuild.py",
        }
        cases = (
            (
                "kernel/util/bin_programs_gen.cc",
                "$(CUPIDOBJ) install-source bin "
                "--bin $(BIN_CC_SRCS) --headers $(BIN_HDR_SRCS) "
                "--browser $(BROWSER_SUB_SRCS) -o $@",
            ),
            (
                "kernel/util/docs_programs_gen.cc",
                "$(CUPIDOBJ) install-source docs "
                "--ctxt $(DOC_CTXT_SRCS) --doc-assets $(DOC_ASSET_SRCS) "
                "--home-assets $(HOME_ASSET_SRCS) -o $@",
            ),
            (
                "kernel/util/demos_programs_gen.cc",
                "$(CUPIDOBJ) install-source demos --demos $(DEMO_ASM_SRCS) -o $@",
            ),
        )
        deliveries = {}
        for delivery_target, recipe in cases:
            with self.subTest(delivery_target=delivery_target):
                checked = next(
                    item
                    for item in audit["build"]["transforms"]
                    if item["output"] == delivery_target
                )
                content_inputs = [
                    path
                    for path in checked["inputs"]
                    if path not in ignored_inputs
                ]
                delivery = module._build_transforms(
                    ".",
                    {delivery_target},
                    {
                        delivery_target: module.MakeRule(
                            prerequisites=[*content_inputs, *checked_inputs],
                            recipe=[recipe],
                        )
                    },
                    cupidobj_runner_owner="cupid_builder",
                )[0]
                self.assertEqual(
                    delivery["operation"], "generate_install_source"
                )
                module._validate_cupidobj_install_source_delivery(
                    ".", delivery, expected_content[delivery_target]
                )
                checked.update(delivery)
                deliveries[delivery_target] = delivery

        manifest = module._c_preprocessor_active_cases_manifest(audit)
        for delivery_target, _recipe in cases:
            self.assertIn(
                ("KERNEL_I386", f"/{delivery_target}"),
                manifest.generated_cases,
            )

        target = "kernel/util/bin_programs_gen.cc"
        transform = deliveries[target]
        changes = {
            "wrong output": {**transform, "output": "bin/install.cc"},
            "wrong operation": {
                **transform,
                "operation": "transform_object",
            },
            "wrong tools": {**transform, "tools": ["cupid_object"]},
            "missing checked input": {
                **transform,
                "inputs": [
                    path
                    for path in transform["inputs"]
                    if path != "bootstrap/seeds/i386-windows/cupidobj.exe"
                ],
            },
            "missing content input": {
                **transform,
                "inputs": [
                    path
                    for path in transform["inputs"]
                    if path != "bin/fat16.h"
                ],
            },
            "duplicate content input": {
                **transform,
                "inputs": [*transform["inputs"], "bin/fat16.h"],
            },
            "changed recipe": {
                **transform,
                "recipe": [
                    "$(CUPIDOBJ) install-source bin --bin $(BIN_CC_SRCS) -o $@"
                ],
            },
        }
        for name, changed in changes.items():
            with (
                self.subTest(name=name),
                self.assertRaisesRegex(
                    module.AuditError,
                    r"CupidObj install-source delivery",
                ),
            ):
                module._validate_cupidobj_install_source_delivery(
                    ".", changed, expected_content[target]
                )

        substitutions = (
            (
                "kernel/util/bin_programs_gen.cc",
                "bin/cat.cc",
                "bin/not_a_real_program.cc",
            ),
            (
                "kernel/util/docs_programs_gen.cc",
                "cupidos-txt/00INDEX.CTXT",
                "cupidos-txt/99NOTREAL.CTXT",
            ),
            (
                "kernel/util/demos_programs_gen.cc",
                "demos/hello.asm",
                "demos/not_a_real_demo.asm",
            ),
        )
        for delivery_target, existing, replacement in substitutions:
            changed = deliveries[delivery_target]
            changed = {
                **changed,
                "inputs": [
                    replacement if path == existing else path
                    for path in changed["inputs"]
                ],
            }
            with (
                self.subTest(delivery_target=delivery_target),
                self.assertRaisesRegex(
                    module.AuditError,
                    r"CupidObj install-source delivery content inputs changed",
                ),
            ):
                module._validate_cupidobj_install_source_delivery(
                    ".", changed, expected_content[delivery_target]
                )

        near_match = module._build_transforms(
            ".",
            {target},
            {
                target: module.MakeRule(
                    prerequisites=transform["inputs"],
                    recipe=[
                        "$(CUPIDOBJ) install-sources bin --bin $(BIN_CC_SRCS) -o $@"
                    ],
                )
            },
        )[0]
        self.assertEqual(near_match["operation"], "transform_object")

        misplaced_tokens = {
            "$(CUPIDOBJ) wrap-text bin/hello.cc -o install-source": (
                "wrap_text_as_elf32_relocatable"
            ),
            "echo install-source && $(CUPIDOBJ) wrap-text bin/hello.cc -o hello.o": (
                "wrap_text_as_elf32_relocatable"
            ),
            "echo $(CUPIDOBJ) install-source bin": "transform_object",
            "echo '; $(CUPIDOBJ) install-source bin'": "transform_object",
            'printf "x && $(CUPIDOBJ) install-source bin"': (
                "transform_object"
            ),
        }
        for recipe, expected_operation in misplaced_tokens.items():
            with self.subTest(recipe=recipe):
                misplaced = module._build_transforms(
                    ".",
                    {target},
                    {
                        target: module.MakeRule(
                            prerequisites=transform["inputs"],
                            recipe=[recipe],
                        )
                    },
                )[0]
                self.assertEqual(misplaced["operation"], expected_operation)

    def test_hosted_tool_builds_remain_optional(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        module = _load_audit_module()
        rules = module._parse_make_rules(
            module._run_make_database(REPO_ROOT, make, "all")
        )
        hosted_tools = [
            f"toolchain/build/{tool}.exe"
            for tool in ("cupidasm", "cupiddis", "cupidld", "cupidobj")
        ]
        self.assertTrue(
            set(hosted_tools).issubset(rules),
        )
        self.assertTrue(
            set(hosted_tools).isdisjoint(module._reachable_rules(rules, "all"))
        )

    def test_checked_cupidc_active_manifest_keeps_profiles_isolated(self):
        lines = ACTIVE_CASE_MANIFEST.read_text(encoding="utf-8").splitlines()
        roots = [
            line
            for line in lines
            if line.startswith("CUPIDC_PP_INCLUDE_ROOT(")
        ]
        macros = [
            line for line in lines if line.startswith("CUPIDC_PP_MACRO(")
        ]
        forced = [
            line
            for line in lines
            if line.startswith("CUPIDC_PP_FORCED_INCLUDE(")
        ]
        both_forms = "(CTOOL_C_PP_INCLUDE_QUOTED | CTOOL_C_PP_INCLUDE_ANGLE)"
        angle_forms = "CTOOL_C_PP_INCLUDE_ANGLE"
        kernel_roots = [
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
        ]
        expected_root_paths = {
            "KERNEL_I386": kernel_roots,
            "DOOM_COMPAT_I386": [
                *kernel_roots,
                "/kernel/doom/src",
                "/kernel/doom/src/include_stubs",
            ],
            "DOOM_TREE_I386": [
                *kernel_roots,
                "/kernel/doom/src",
                "/kernel/doom/src/include_stubs",
            ],
            "USER_I386": ["/user"],
            "FREESTANDING_I386": [],
            "CUPID_RUNTIME": [],
            "HOSTED_TOOLCHAIN_64": ["/toolchain"],
            "HOSTED_KERNEL_BRIDGE_64": ["/toolchain", "/kernel/lang"],
            "HOSTED_I386_LINUX": [
                "/toolchain",
                "/toolchain/hosted/i386-linux/include",
            ],
            "HOSTED_I386_WINDOWS": [
                "/toolchain",
                "/toolchain/hosted/i386-linux/include",
            ],
            "HOSTED_I386_KERNEL_BRIDGE": [
                "/toolchain",
                "/kernel/lang",
                "/toolchain/hosted/i386-linux/include",
            ],
            "HOSTED_I386_LINUX_GNU": [
                "/toolchain",
                "/toolchain/hosted/i386-linux/include",
            ],
            "HOSTED_I386_WINDOWS_UTF8": [
                "/toolchain",
                "/toolchain/hosted/i386-linux/include",
            ],
            "HOSTED_I386_WINDOWS_PUBLICATION": [
                "/toolchain",
                "/toolchain/hosted/i386-linux/include",
            ],
            "HOSTED_I386_WINDOWS_BUILD": [
                "/toolchain",
                "/toolchain/hosted/i386-linux/include",
            ],
            "HOSTED_I386_WINDOWS_UTF8_GNU": [
                "/toolchain",
                "/toolchain/hosted/i386-linux/include",
            ],
        }
        expected_roots = {
            name: [
                (
                    path,
                    angle_forms
                    if name
                    in {
                        "HOSTED_I386_LINUX",
                        "HOSTED_I386_WINDOWS",
                        "HOSTED_I386_KERNEL_BRIDGE",
                        "HOSTED_I386_LINUX_GNU",
                        "HOSTED_I386_WINDOWS_UTF8",
                        "HOSTED_I386_WINDOWS_PUBLICATION",
                        "HOSTED_I386_WINDOWS_BUILD",
                        "HOSTED_I386_WINDOWS_UTF8_GNU",
                    }
                    and path.endswith("/hosted/i386-linux/include")
                    else both_forms,
                )
                for path in paths
            ]
            for name, paths in expected_root_paths.items()
        }
        root_pattern = re.compile(
            r'^CUPIDC_PP_INCLUDE_ROOT\(([A-Z0-9_]+), "([^"]+)", '
            r"(.+)\)$"
        )
        actual_roots = {name: [] for name in expected_roots}
        for line in roots:
            match = root_pattern.fullmatch(line)
            self.assertIsNotNone(match, line)
            name, path, actual_forms = match.groups()
            actual_roots[name].append((path, actual_forms))
        self.assertEqual(actual_roots, expected_roots)

        self.assertEqual(
            roots[0],
            f'CUPIDC_PP_INCLUDE_ROOT(KERNEL_I386, "/kernel", {both_forms})',
        )
        common_macros = [
            ("__GNUC__", "1"),
            ("__SIZEOF_POINTER__", "4"),
            ("__ORDER_LITTLE_ENDIAN__", "1234"),
            ("__ORDER_BIG_ENDIAN__", "4321"),
            ("__ORDER_PDP_ENDIAN__", "3412"),
            ("__BYTE_ORDER__", "__ORDER_LITTLE_ENDIAN__"),
        ]
        expected_macros = {
            "KERNEL_I386": [
                *common_macros,
                ("__SSE2__", "1"),
                ("DEBUG", "1"),
            ],
            "DOOM_COMPAT_I386": [*common_macros, ("__SSE2__", "1")],
            "DOOM_TREE_I386": [
                *common_macros,
                ("__SSE2__", "1"),
                ("DEFAULT_SAVEGAMEDIR", '"/home/doom/"'),
                ("DOOM_PORT_CUPIDOS", "1"),
            ],
            "USER_I386": common_macros,
            "FREESTANDING_I386": [("__SIZEOF_POINTER__", "4")],
            "CUPID_RUNTIME": [],
            "HOSTED_TOOLCHAIN_64": [("__SIZEOF_POINTER__", "8")],
            "HOSTED_KERNEL_BRIDGE_64": [("__SIZEOF_POINTER__", "8")],
            "HOSTED_I386_LINUX": [("__SIZEOF_POINTER__", "4")],
            "HOSTED_I386_WINDOWS": [
                ("__SIZEOF_POINTER__", "4"),
                ("_WIN32", "1"),
            ],
            "HOSTED_I386_KERNEL_BRIDGE": [("__SIZEOF_POINTER__", "4")],
            "HOSTED_I386_LINUX_GNU": [("__SIZEOF_POINTER__", "4")],
            "HOSTED_I386_WINDOWS_UTF8": [
                ("__SIZEOF_POINTER__", "4"),
                ("_WIN32", "1"),
            ],
            "HOSTED_I386_WINDOWS_PUBLICATION": [
                ("__SIZEOF_POINTER__", "4"),
                ("_WIN32", "1"),
                ("CUPID_WINDOWS_PUBLICATION", "1"),
            ],
            "HOSTED_I386_WINDOWS_BUILD": [
                ("__SIZEOF_POINTER__", "4"),
                ("_WIN32", "1"),
                ("CUPID_WINDOWS_BUILD", "1"),
            ],
            "HOSTED_I386_WINDOWS_UTF8_GNU": [
                ("__SIZEOF_POINTER__", "4"),
                ("CUPID_WINDOWS_UTF8", "1"),
            ],
        }

        def macro_line(profile, name, replacement):
            escaped_name = name.replace("\\", "\\\\").replace('"', '\\"')
            escaped_replacement = replacement.replace("\\", "\\\\").replace(
                '"', '\\"'
            )
            return f'CUPIDC_PP_MACRO({profile}, "{escaped_name}", "{escaped_replacement}")'

        self.assertEqual(
            macros,
            [
                macro_line(profile, name, replacement)
                for profile in expected_macros
                for name, replacement in expected_macros[profile]
            ],
        )
        self.assertEqual(
            forced,
            [
                "CUPIDC_PP_FORCED_INCLUDE(DOOM_TREE_I386, "
                '"/kernel/doom/dglibc_compat.h")'
            ],
        )

    def test_hosted_i386_contract_profiles_fail_closed_at_the_c_seam(self):
        module = _load_audit_module()
        module._validate_hosted_i386_contract_profiles(REPO_ROOT)
        contract = (
            REPO_ROOT / "toolchain" / "tests" / "cupidc_object_contract.cc"
        ).read_text(encoding="utf-8")
        def fragment_pattern(text):
            return r"\s*".join(re.escape(piece) for piece in text.split())

        mutations = {
            "codec source removed": (
                '      {"/toolchain/iso_fixture_bundle.cc", "/toolchain/iso_fixture_bundle.o",\n'
                "       HOST_TOOL_SOURCE_C, CTOOL_FALSE}",
                '      {"/toolchain/unknown_codec.cc", "/toolchain/iso_fixture_bundle.o",\n'
                "       HOST_TOOL_SOURCE_C, CTOOL_FALSE}",
                r"source-profile rows differ.*iso_fixture_bundle",
            ),
            "codec gains GNU mode": (
                '"/toolchain/iso_fixture_bundle.o",\n       HOST_TOOL_SOURCE_C, CTOOL_FALSE',
                '"/toolchain/iso_fixture_bundle.o",\n       HOST_TOOL_SOURCE_C, CTOOL_TRUE',
                r"source-profile rows differ.*iso_fixture_bundle",
            ),
            "runtime loses GNU mode": (
                '"/toolchain/hosted/i386-linux/runtime.o", '
                "HOST_TOOL_SOURCE_C,\n       CTOOL_TRUE}",
                '"/toolchain/hosted/i386-linux/runtime.o", '
                "HOST_TOOL_SOURCE_C,\n       CTOOL_FALSE}",
                r"source-profile rows differ.*runtime\.c",
            ),
            "preprocessor ignores the selected mode": (
                "pp_request = profile->request;\n"
                "  pp_request.gnu_extensions = gnu_extensions;",
                "pp_request = profile->request;\n"
                "  pp_request.gnu_extensions = CTOOL_FALSE;",
                r"profile emitter does not forward the checked GNU mode",
            ),
            "compile loop ignores the selected mode": (
                "job, &source, &profile, "
                "source_cases[index].gnu_extensions,\n"
                "          compiled_objects[index]",
                "job, &source, &profile, CTOOL_FALSE,\n"
                "          compiled_objects[index]",
                r"compile loop does not consume each checked source profile",
            ),
        }
        for module_name in ("fat16_stage", "fat16_names", "disk_image"):
            row = (f'      {{"/toolchain/{module_name}.cc", '
                   f'"/toolchain/{module_name}.o",\n'
                   "       HOST_TOOL_SOURCE_C, CTOOL_FALSE}")
            self.assertEqual(len(re.findall(fragment_pattern(row), contract)), 1)
            mutations[f"{module_name} source removed"] = (
                row, row.replace(module_name + ".cc", "unknown_disk.cc", 1),
                "source-profile rows differ.*" + module_name,
            )
            mutations[f"{module_name} gains GNU mode"] = (
                row, row.replace("CTOOL_FALSE", "CTOOL_TRUE", 1),
                "source-profile rows differ.*" + module_name,
            )
        self.assertRegex(
            contract, fragment_pattern("34u, 35u, 36u, 37u, 38u, 39u, 40u, 41u,\n"
            "      42u, 43u, 44u, 45u, 46u, 47u, 29u, 1u};"),
        )
        for name, (old, new, message) in mutations.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                target = (
                    root / "toolchain" / "tests" / "cupidc_object_contract.cc"
                )
                target.parent.mkdir(parents=True)
                matches = list(re.finditer(fragment_pattern(old), contract))
                self.assertEqual(len(matches), 1, name)
                mutated = contract[:matches[0].start()] + new + contract[matches[0].end():]
                self.assertNotEqual(mutated, contract)
                target.write_text(mutated, encoding="utf-8")
                with self.assertRaisesRegex(module.AuditError, message):
                    module._validate_hosted_i386_contract_profiles(root)

    def test_disk_foundations_keep_the_strict_hosted_preprocessing_profile(self):
        module = _load_audit_module()
        audit = json.loads(ACTIVE_BUILD_MANIFEST.read_bytes())
        manifest = module._c_preprocessor_active_cases_manifest(audit)
        sources = {"/toolchain/fat16_stage.cc", "/toolchain/fat16_names.cc",
                   "/toolchain/disk_image.cc"}
        for source in sources:
            self.assertIn(("HOSTED_I386_LINUX", source), manifest.active_cases)
            self.assertEqual([profile for profile, path in manifest.active_cases if path == source],
                             ["HOSTED_I386_LINUX"])
        for source in sources:
            with self.subTest(source=source):
                changed = json.loads(json.dumps(audit))
                build = next(row for row in changed["supplemental_builds"] if row["directory"] == "toolchain")
                transform = next(row for row in build["transforms"]
                                 if row["output"] == "toolchain/build/cupidc-contracts/manifest.json")
                transform["inputs"].remove(source.lstrip("/"))
                with self.assertRaisesRegex(module.AuditError, "toolchain contract closure changed"):
                    module._c_preprocessor_active_cases_manifest(changed)

    def test_disk_foundation_fixed_point_inventory_and_capture_fail_closed(self):
        module = _load_audit_module()
        contract = module._cupid_toolchain_fixed_point_contract(REPO_ROOT)
        self.assertEqual(contract["tool_c_sources"], 44)
        self.assertEqual(contract["compared_c_objects"], 44)
        self.assertEqual(contract["contract_manifest_inputs"], 121)
        names = (
            "tests/test_toolchain_cupidc_object.py", "toolchain/cupidc_main.cc",
            "toolchain/cupidld.h", "toolchain/cupidld_main.cc", "toolchain/cupidld.cc",
            "tools/bootstrap_toolchain.py", "tools/cupidc_toolchain_contracts.py",
            "tools/bootstrap_user_abi.py",
            "toolchain/tests/hosted_i386_windows_runtime_contract.cc",
            "toolchain/hosted/i386-linux/include/windows.h",
            "toolchain/hosted/i386-windows/publication_runtime.cc",
            "toolchain/hosted/i386-windows/publication_start.asm",
        )
        inputs = {name: (REPO_ROOT / name).read_text(encoding="utf-8") for name in names}
        bootstrap = inputs["tools/bootstrap_toolchain.py"]
        rows = {
            name: f'    ("{name}", "/toolchain/{name}.cc", False),\n'
            for name in ("fat16_stage", "fat16_names", "disk_image")
        }
        mutations = {"omit " + name: bootstrap.replace(row, "", 1)
                     for name, row in rows.items()}
        ordered = "".join(rows.values())
        self.assertEqual(bootstrap.count(ordered), 1)
        mutations["reordered disk sources"] = bootstrap.replace(
            ordered, rows["disk_image"] + rows["fat16_names"] + rows["fat16_stage"], 1
        )
        candidate = "PROMOTED_SOURCES = (*ISO_PUBLICATION_SOURCES, *DISK_FOUNDATION_SOURCES)"
        self.assertEqual(bootstrap.count(candidate), 1)
        mutations["cyclic source constant"] = bootstrap.replace(
            candidate, "PROMOTED_SOURCES = PROMOTED_SOURCES", 1
        )
        mutations["second live source binding"] = bootstrap.replace(
            candidate, candidate + "\nDISK_FOUNDATION_SOURCES = ()", 1
        )
        mutations["mutable disk source expansion"] = bootstrap.replace(
            "DISK_FOUNDATION_SOURCES = (", "DISK_FOUNDATION_SOURCES = [", 1
        ).replace(
            ")\n" + candidate, "]\n" + candidate, 1
        )
        table = 'paths.append(source_root / "toolchain/fat16_name_profiles.inc")'
        self.assertEqual(bootstrap.count(table), 1)
        mutations["substituted Unicode capture"] = bootstrap.replace(
            table, 'paths.append(source_root / "toolchain/other.inc")', 1
        )
        for label, changed in mutations.items():
            with self.subTest(label=label), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                self.assertNotEqual(changed, bootstrap)
                ast.parse(changed)
                for name, text in inputs.items():
                    path = root / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(changed if name == "tools/bootstrap_toolchain.py" else text,
                                    encoding="utf-8")
                with self.assertRaisesRegex(module.AuditError, "fixed-point source freeze differs"):
                    module._cupid_toolchain_fixed_point_contract(root)

    def test_windows_runtime_contract_accepts_layout_but_rejects_changed_calls(self):
        module = _load_audit_module()
        path = REPO_ROOT / "toolchain/tests/hosted_i386_windows_runtime_contract.cc"
        original = path.read_text(encoding="utf-8")
        read_text = Path.read_text
        formatted = re.sub(r"(?m)^    ", "\t", original)
        changed = original.replace("result = allocator_contract();",
                                   "result = directory_contract();", 1)
        self.assertNotEqual(changed, original)
        for payload, accepted in ((formatted, True), (changed, False)):
            def read(candidate, *args, **kwargs):
                return payload if candidate == path else read_text(candidate, *args, **kwargs)
            with self.subTest(accepted=accepted), mock.patch.object(Path, "read_text", read):
                if accepted:
                    module._cupid_toolchain_fixed_point_contract(REPO_ROOT)
                else:
                    with self.assertRaisesRegex(module.AuditError, "Windows runtime contract differs"):
                        module._cupid_toolchain_fixed_point_contract(REPO_ROOT)
