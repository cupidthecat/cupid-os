## @file
#  @brief Check active compiler profile reports and reject drift.
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from tests.build_graph_audit_fixtures import (
    ACTIVE_CASE_MANIFEST,
    AUDIT_TOOL,
    LINUX_BOOTSTRAP_SEED_INPUTS,
    REPO_ROOT,
    WINDOWS_PRODUCTION_SEED_INPUTS,
    _load_audit_module,
)
from tools.cupidc_kernel_compile import (
    APPROVED_DOOM_COMPAT_SOURCES,
    APPROVED_DOOM_TREE_SOURCES,
    FROZEN_KERNEL_INPUT_CLOSURES,
)


class CompilerProfileCases:
    def test_cupidc_active_manifest_fails_closed_on_compile_recipe_shape(self):
        module = _load_audit_module()

        def audit(inputs, recipe, sources=None):
            return {
                "build": {
                    "directory": ".",
                    "transforms": [
                        {
                            "output": "unit.o",
                            "inputs": inputs,
                            "tools": ["host_c_compiler"],
                            "operation": "compile_c_to_elf32_object",
                            "recipe": [recipe],
                        }
                    ],
                },
                "supplemental_builds": [],
                "sources": [] if sources is None else sources,
            }

        def hosted_audit(
            recipe,
            source="toolchain/unit.c",
            *,
            origin=None,
        ):
            return {
                "build": {"directory": ".", "transforms": []},
                "supplemental_builds": [
                    {
                        "directory": "toolchain",
                        "transforms": [
                            {
                                "output": "build/unit.o",
                                "inputs": [source],
                                "tools": ["host_c_compiler"],
                                "operation": "compile_c_to_host_object",
                                "recipe": [recipe],
                            }
                        ],
                    }
                ],
                "sources": (
                    []
                    if origin is None
                    else [{"path": source, "origin": origin}]
                ),
            }

        def contract_cohort_audit(
            *,
            include_seed=True,
            operation="generate_toolchain_manifest",
            tools=None,
            recipe=None,
        ):
            if tools is None:
                tools = [
                    "cupid_assembler",
                    "cupid_c_compiler",
                    "cupid_c_contract",
                    "cupid_linker",
                    "host_python",
                ]
            inputs = [
                path[1:]
                for path in (
                    module._C_PP_HOSTED_I386_STRICT_CASES
                    + module._C_PP_HOSTED_I386_GNU_CASES
                    + module._C_PP_TOOLCHAIN_CONTRACT_CASES
                )
            ]
            inputs.extend(
                (
                    "bootstrap/seeds/i386-linux/manifest.json",
                    "bootstrap/seeds/release.json",
                    "bootstrap/seeds/i386-linux/cupidasm.elf",
                    "bootstrap/seeds/i386-linux/cupiddis.elf",
                    "bootstrap/seeds/i386-linux/cupidld.elf",
                    "bootstrap/seeds/i386-linux/cupidobj.elf",
                    "link.ld",
                    "toolchain/hosted/i386-linux/start.asm",
                    "toolchain/hosted/i386-windows/start.asm",
                    "toolchain/hosted/i386-windows/tool_start.asm",
                    "toolchain/Makefile",
                    "tools/bootstrap_toolchain.py",
                    "tools/cupidc_toolchain_contracts.py",
                )
            )
            inputs.extend(
                path
                for path in module.TOOLCHAIN_CONTRACT_LINUX_INPUTS
                if path not in inputs
                and (
                    include_seed
                    or path != "bootstrap/seeds/i386-linux/cupidc.elf"
                )
            )
            if include_seed and (
                "bootstrap/seeds/i386-linux/cupidc.elf" not in inputs
            ):
                inputs.append("bootstrap/seeds/i386-linux/cupidc.elf")
            return {
                "build": {"directory": ".", "transforms": []},
                "supplemental_builds": [
                    {
                        "directory": "toolchain",
                        "transforms": [
                            {
                                "output": (
                                    "toolchain/build/cupidc-contracts/manifest.json"
                                ),
                                "inputs": inputs,
                                "tools": tools,
                                "operation": operation,
                                "recipe": (
                                    [
                                        "$(PYTHON) "
                                        "../tools/"
                                        "cupidc_toolchain_contracts.py "
                                        "build \\",
                                        "--root .. --manifest "
                                        "../bootstrap/seeds/i386-linux/"
                                        "manifest.json \\",
                                        "--output $(CONTRACT_DIR) \\",
                                        '--behavior-release "$(TOOLCHAIN_MANIFEST_BEHAVIOR_RELEASE)"',
                                    ]
                                    if recipe is None
                                    else recipe
                                ),
                            }
                        ],
                    }
                ],
                "sources": [],
            }

        cases = {
            "unknown marker": (
                audit(["unit.c"], "$(CC) $(NEW_CFLAGS) -c $< -o $@"),
                r"unknown recipe marker.*NEW_CFLAGS",
            ),
            "brace-form unknown marker": (
                audit(["unit.c"], "${CC} ${NEW_CFLAGS} -c $< -o $@"),
                r"unknown recipe marker.*NEW_CFLAGS",
            ),
            "escaped shell marker": (
                audit(
                    ["unit.c"], "$(CC) $(CFLAGS) $${NEW_CFLAGS} -c $< -o $@"
                ),
                r"unmodeled recipe dollar reference",
            ),
            "computed marker": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) $(value NEW_CFLAGS) -c $< -o $@",
                ),
                r"unmodeled recipe Make reference/function",
            ),
            "zero roots": (
                audit(["unit.h"], "$(CC) $(CFLAGS) -c $< -o $@"),
                r"exactly one C translation-unit root.*found 0",
            ),
            "multiple roots": (
                audit(
                    ["first.c", "second.c"],
                    "$(CC) $(CFLAGS) -c $< -o $@",
                ),
                r"exactly one C translation-unit root.*found 2",
            ),
            "literal macro": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) -DLOCAL=1 -c $< -o $@",
                ),
                r"literal preprocessor flag.*-DLOCAL=1",
            ),
            "literal contradictory target": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) -m64 -c $< -o $@",
                ),
                r"literal preprocessor flag.*-m64",
            ),
            "literal driver pass-through": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) -Wp,-DLOCAL=1 -c $< -o $@",
                ),
                r"literal preprocessor flag.*-Wp,-DLOCAL=1",
            ),
            "literal language mode": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) -xc++ -c $< -o $@",
                ),
                r"literal preprocessor flag.*-xc\+\+",
            ),
            "literal character mode": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) -funsigned-char -c $< -o $@",
                ),
                r"literal preprocessor flag.*-funsigned-char",
            ),
            "literal predefined accelerator macro": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) -fopenacc -c $< -o $@",
                ),
                r"literal preprocessor flag.*-fopenacc",
            ),
            "compiler response file": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) @extra.rsp -c $< -o $@",
                ),
                r"literal preprocessor flag.*@extra.rsp",
            ),
            "recipe environment include path": (
                audit(
                    ["unit.c"],
                    "CPATH=private $(CC) $(CFLAGS) -c $< -o $@",
                ),
                r"literal preprocessor flag.*CPATH=private",
            ),
            "hosted recipe omits include profile": (
                hosted_audit("$(CC) $(CFLAGS) -c $< -o $@"),
                r"hosted recipe markers differ.*CPPFLAGS",
            ),
            "hosted recipe duplicates include profile": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) $(CPPFLAGS) $(CFLAGS) -c $< -o $@"
                ),
                r"hosted recipe markers differ.*CPPFLAGS",
            ),
            "root recipe duplicates compiler marker": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CC) $(CFLAGS) -c $< -o $@",
                ),
                r"recipe markers differ.*CC",
            ),
            "root recipe duplicates profile marker": (
                audit(
                    ["unit.c"],
                    "$(CC) $(CFLAGS) $(CFLAGS) -c $< -o $@",
                ),
                r"recipe markers differ.*CFLAGS",
            ),
            "hosted bridge flag moves to ordinary source": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) -I../kernel/lang $(CFLAGS) -c $< -o $@"
                ),
                r"hosted bridge recipe differs.*toolchain/unit\.c",
            ),
            "hosted bridge source loses its include flag": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@",
                    "kernel/lang/as_elf.cc",
                ),
                r"hosted bridge recipe differs.*kernel/lang/as_elf\.cc",
            ),
            "renamed hosted bridge loses its C language mode": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) -I../kernel/lang $(CFLAGS) -c $< -o $@",
                    "kernel/lang/as_elf.cc",
                ),
                r"hosted bridge recipe differs.*kernel/lang/as_elf\.cc",
            ),
            "renamed hosted bridge selects C++ mode": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) -I../kernel/lang $(CFLAGS) -x c++ -c $< -o $@",
                    "kernel/lang/as_elf.cc",
                ),
                r"compiler argument profile differs"
                r".*kernel/lang/as_elf\.cc",
            ),
            "renamed hosted source loses its C language mode": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@",
                    "toolchain/ctool.cc",
                ),
                r"hosted bridge recipe differs.*toolchain/ctool\.cc",
            ),
            "renamed hosted source selects C++ mode": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) $(CFLAGS) -x c++ -c $< -o $@",
                    "toolchain/ctool.cc",
                ),
                r"compiler argument profile differs.*toolchain/ctool\.cc",
            ),
            "hosted bridge include precedes common include roots": (
                hosted_audit(
                    "$(CC) -I../kernel/lang $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@",
                    "kernel/lang/as_elf.cc",
                ),
                r"compiler argument profile differs"
                r".*kernel/lang/as_elf\.cc",
            ),
            "hosted include marker is shell quoted": (
                hosted_audit('$(CC) "$(CPPFLAGS)" $(CFLAGS) -c $< -o $@'),
                r"compiler argument profile differs.*toolchain/unit\.c",
            ),
            "hosted command substitution injects flags": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) $(CFLAGS) `cat extra.flags` -c $< -o $@"
                ),
                r"compile recipe has unmodeled shell substitution",
            ),
            "hosted markers hidden in shell comment": (
                hosted_audit("$(CC) $(CFLAGS) -c $< -o $@ # $(CPPFLAGS)"),
                r"compile recipe contains a shell comment",
            ),
            "hosted markers belong to a different command": (
                hosted_audit("echo $(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@"),
                r"compile recipe does not invoke.*CC",
            ),
            "optional hosted root is absent from source inventory": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@",
                    "toolchain/ctool_host.cc",
                ),
                r"root is absent from source inventory.*toolchain/ctool_host\.cc",
            ),
            "optional hosted root has generated origin": (
                hosted_audit(
                    "$(CC) $(CPPFLAGS) $(CFLAGS) -x c -c $< -o $@",
                    "toolchain/ctool_host.cc",
                    origin="generated",
                ),
                r"generated root has non-kernel profile"
                r".*toolchain/ctool_host\.cc",
            ),
            "contract cohort omits source closure": (
                {
                    "build": {"directory": ".", "transforms": []},
                    "supplemental_builds": [
                        {
                            "directory": "toolchain",
                            "transforms": [
                                {
                                    "output": (
                                        "toolchain/build/cupidc-contracts/manifest.json"
                                    ),
                                    "inputs": ["toolchain/ctool.cc"],
                                    "tools": [
                                        "cupid_assembler",
                                        "cupid_c_compiler",
                                        "cupid_c_contract",
                                        "cupid_linker",
                                        "host_python",
                                    ],
                                    "operation": (
                                        "generate_toolchain_manifest"
                                    ),
                                    "recipe": ["checked orchestration"],
                                }
                            ],
                        }
                    ],
                    "sources": [],
                },
                r"toolchain contract closure changed; missing=",
            ),
            "contract cohort loses compiler seed": (
                contract_cohort_audit(include_seed=False),
                r"contract cohort lost checked inputs.*cupidc\.elf",
            ),
            "contract cohort changes subcommand": (
                contract_cohort_audit(
                    recipe=[
                        "$(PYTHON) ../tools/cupidc_toolchain_contracts.py other "
                        "--root .. --manifest ../bootstrap/seeds/i386-linux/manifest.json "
                        '--output $(CONTRACT_DIR) --behavior-release "$(TOOLCHAIN_MANIFEST_BEHAVIOR_RELEASE)"'
                    ]
                ),
                r"recipe no longer invokes the checked fixed-point builder",
            ),
            "contract cohort loses manifest author": (
                contract_cohort_audit(tools=["host_python"]),
                r"cohort transform differs from the checked manifest-author",
            ),
        }
        for name, (synthetic, message) in cases.items():
            with (
                self.subTest(name=name),
                self.assertRaisesRegex(module.AuditError, message),
            ):
                module._c_preprocessor_active_cases_manifest(synthetic)

        malformed_wrap = {
            "build": {
                "directory": ".",
                "transforms": [
                    {
                        "output": "bin/malformed.o",
                        "inputs": None,
                        "tools": ["cupid_object"],
                        "operation": "wrap_text_as_elf32_relocatable",
                        "recipe": ["$(CUPIDOBJ) wrap-text $< -o $@"],
                    }
                ],
            },
            "supplemental_builds": [],
            "sources": [],
        }
        with self.assertRaisesRegex(
            module.AuditError, r"delivery transform inputs are absent"
        ):
            module._c_preprocessor_active_cases_manifest(malformed_wrap)

        for delivered_path in ("bin/unit.cc", "bin/unit.h"):
            binary_text_delivery = {
                "build": {
                    "directory": ".",
                    "transforms": [
                        {
                            "output": f"{delivered_path}.o",
                            "inputs": [delivered_path],
                            "tools": ["cupid_object"],
                            "operation": "wrap_binary_as_elf32_relocatable",
                            "recipe": ["$(CUPIDOBJ) wrap $< -o $@"],
                        }
                    ],
                },
                "supplemental_builds": [],
                "sources": [],
            }
            with (
                self.subTest(delivered_path=delivered_path),
                self.assertRaisesRegex(
                    module.AuditError,
                    r"unclassified Cupid delivery transform: bin/unit\.(?:cc|h)\.o "
                    r"\(wrap_binary_as_elf32_relocatable\)",
                ),
            ):
                module._c_preprocessor_active_cases_manifest(
                    binary_text_delivery
                )

    def test_cupidc_kernel_wrapper_has_a_closed_compile_recipe(self):
        module = _load_audit_module()
        transform = {
            "output": "kernel/crypto/aes.o",
            "inputs": [
                "kernel/crypto/aes.cc",
                "kernel/crypto/aes.h",
                "kernel/core/types.h",
            ],
            "tools": ["cupid_c_compiler", "host_python"],
            "operation": "compile_c_to_elf32_object",
            "recipe": [
                "$(CUPIDC_KERNEL_COMPILE) "
                "--source kernel/crypto/aes.cc "
                "--output kernel/crypto/aes.o"
            ],
        }
        self.assertEqual(
            module._c_preprocessor_profile_for_c_transform(".", transform),
            "KERNEL_I386",
        )

        changed = dict(transform)
        changed["recipe"] = [
            "$(CUPIDC_KERNEL_COMPILE) "
            "--source kernel/crypto/aes.cc "
            "--output build/aes.o"
        ]
        with self.assertRaisesRegex(
            module.AuditError, r"wrapper arguments differ"
        ):
            module._c_preprocessor_profile_for_c_transform(".", changed)

        doom_compat = {
            "output": "kernel/doom/dglibc.o",
            "inputs": [
                "kernel/doom/dglibc.cc",
                "kernel/doom/dglibc.h",
            ],
            "tools": ["cupid_c_compiler", "host_python"],
            "operation": "compile_c_to_elf32_object",
            "recipe": [
                "$(CUPIDC_KERNEL_COMPILE) --profile doom-compat "
                "--source kernel/doom/dglibc.cc "
                "--output kernel/doom/dglibc.o"
            ],
        }
        self.assertEqual(
            module._c_preprocessor_profile_for_c_transform(".", doom_compat),
            "DOOM_COMPAT_I386",
        )

        doom_tree = {
            "output": "kernel/doom/src/am_map.o",
            "inputs": [
                "kernel/doom/src/am_map.cc",
                "kernel/doom/src/am_map.h",
            ],
            "tools": ["cupid_c_compiler", "host_python"],
            "operation": "compile_c_to_elf32_object",
            "recipe": [
                "$(CUPIDC_KERNEL_COMPILE) --profile doom-tree --source $< --output $@"
            ],
        }
        self.assertEqual(
            module._c_preprocessor_profile_for_c_transform(".", doom_tree),
            "DOOM_TREE_I386",
        )

        wrong_doom_profile = dict(doom_compat)
        wrong_doom_profile["recipe"] = [
            "$(CUPIDC_KERNEL_COMPILE) --profile doom-tree "
            "--source kernel/doom/dglibc.cc "
            "--output kernel/doom/dglibc.o"
        ]
        with self.assertRaisesRegex(
            module.AuditError, r"wrapper arguments differ"
        ):
            module._c_preprocessor_profile_for_c_transform(
                ".", wrong_doom_profile
            )

    def test_checked_cupidc_active_manifest_classifies_non_roots_and_hosted(
        self,
    ):
        lines = ACTIVE_CASE_MANIFEST.read_text(encoding="utf-8").splitlines()
        include_only_pattern = re.compile(
            r'^CUPIDC_PP_INCLUDE_ONLY\("([^"]+)", "([^"]+)"\)$'
        )
        non_root_pattern = re.compile(
            r'^CUPIDC_PP_NON_ROOT\("([^"]+)", "([^"]+)"\)$'
        )
        deferred_pattern = re.compile(
            r'^CUPIDC_PP_DEFERRED_HOSTED\("([^"]+)", "([^"]+)"\)$'
        )
        include_only = [
            match.groups()
            for line in lines
            if (match := include_only_pattern.fullmatch(line)) is not None
        ]
        non_roots = [
            match.groups()
            for line in lines
            if (match := non_root_pattern.fullmatch(line)) is not None
        ]
        deferred = [
            match.groups()
            for line in lines
            if (match := deferred_pattern.fullmatch(line)) is not None
        ]

        self.assertEqual(len(include_only), 24)
        self.assertTrue(
            all(owner == "/bin/browser.cc" for _, owner in include_only)
        )
        self.assertEqual(
            [path for path, _ in non_roots],
            ["/bin/fat16.h", "/bin/shell.h", "/bin/ssh_channel.h"],
        )
        self.assertEqual(deferred, [])
        self.assertTrue(all(reason for _, reason in [*non_roots, *deferred]))

    def test_cupidc_active_manifest_renderer_is_grouped_and_c_escaped(self):
        module = _load_audit_module()
        manifest = module.CPreprocessorActiveCasesManifest(
            profiles=(
                module.CPreprocessorProfile(
                    name="SYNTH",
                    mode="CTOOL_C_PP_MODE_C11",
                    gnu_extensions="CTOOL_TRUE",
                    hosted_environment="CTOOL_FALSE",
                    implicit_function_declarations="CTOOL_FALSE",
                    compatibility_pointer_conversions="CTOOL_FALSE",
                ),
            ),
            include_roots=(
                (
                    "SYNTH",
                    '/root/??/"quoted"\\tab\n\N{GREEK CAPITAL LETTER OMEGA}',
                    "CTOOL_C_PP_INCLUDE_QUOTED",
                ),
            ),
            macros=(("SYNTH", 'A"B', "line\n\\end"),),
            forced_includes=(("SYNTH", "/forced.h"),),
            active_cases=(("SYNTH", "/active.c"),),
            generated_cases=(("SYNTH", "/generated.c"),),
            include_only=(("/fragment.cc", "/owner.cc"),),
            non_roots=(("/header.h", "delivered header"),),
            deferred_hosted=(("/host.c", "host-only contract"),),
        )

        first = module._render_c_preprocessor_active_cases(manifest)
        second = module._render_c_preprocessor_active_cases(manifest)
        self.assertEqual(first, second)
        self.assertIn(
            "CUPIDC_PP_PROFILE(SYNTH, CTOOL_C_PP_MODE_C11, CTOOL_TRUE, "
            "CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE)",
            first,
        )
        self.assertIn(
            "CUPIDC_PP_INCLUDE_ROOT(SYNTH, "
            '"/root/\\?\\?/\\"quoted\\"\\\\tab\\012\\316\\251", '
            "CTOOL_C_PP_INCLUDE_QUOTED)",
            first,
        )
        self.assertIn(
            'CUPIDC_PP_MACRO(SYNTH, "A\\"B", "line\\012\\\\end")',
            first,
        )
        row_prefixes = [
            "CUPIDC_PP_PROFILE(",
            "CUPIDC_PP_INCLUDE_ROOT(",
            "CUPIDC_PP_MACRO(",
            "CUPIDC_PP_FORCED_INCLUDE(",
            "CUPIDC_PP_ACTIVE_CASE(",
            "CUPIDC_PP_GENERATED_CASE(",
            "CUPIDC_PP_INCLUDE_ONLY(",
            "CUPIDC_PP_NON_ROOT(",
            "CUPIDC_PP_DEFERRED_HOSTED(",
        ]
        self.assertEqual(
            [
                next(
                    index
                    for index, line in enumerate(first.splitlines())
                    if line.startswith(prefix)
                )
                for prefix in row_prefixes
            ],
            sorted(
                next(
                    index
                    for index, line in enumerate(first.splitlines())
                    if line.startswith(prefix)
                )
                for prefix in row_prefixes
            ),
        )

    def test_cupidc_active_manifest_check_rejects_drift(self):
        with tempfile.TemporaryDirectory() as td:
            output = Path(td) / "audit.json"
            summary = Path(td) / "audit.md"
            manifest = Path(td) / "cupidc_pp_active_cases.inc"
            command = [
                sys.executable,
                str(AUDIT_TOOL),
                "--root",
                str(REPO_ROOT),
                "--supplemental-build",
                "user:all",
                "--supplemental-build",
                "toolchain:all",
                "--output",
                str(output),
                "--summary",
                str(summary),
                "--c-preprocessor-active-cases",
                str(manifest),
            ]
            generated = subprocess.run(command, text=True, capture_output=True)
            self.assertEqual(generated.returncode, 0, generated.stderr)
            self.assertTrue(
                summary.read_text(encoding="utf-8").endswith("\n\n")
            )
            audit_payload = json.loads(output.read_text(encoding="utf-8"))
            source_by_path = {
                source["path"]: source for source in audit_payload["sources"]
            }
            observed_contract_source = source_by_path[
                "toolchain/tests/core_contract.cc"
            ]
            self.assertEqual(
                observed_contract_source["build_owners"],
                ["cupid_c_contract"],
            )
            manifest_author_source = source_by_path[
                "toolchain/tests/toolchain_manifest_contract.cc"
            ]
            self.assertTrue(
                {
                    "cupid_assembler",
                    "cupid_c_compiler",
                    "cupid_c_contract",
                    "cupid_linker",
                }.issubset(manifest_author_source["build_owners"])
            )
            module = _load_audit_module()
            control_paths = {
                entry["path"]
                for entry in audit_payload["provenance"]["control_files"]
            }
            self.assertTrue(
                {
                    "tools/cupidc_kernel_compile.py",
                    "tools/kernel_cupidc_frontier.py",
                    "tools/bootstrap_toolchain.py",
                    "bootstrap/seeds/i386-windows/manifest.json",
                }.issubset(control_paths)
            )
            contract = audit_payload["contracts"][
                "c_preprocessor_translation_units"
            ]
            self.assertEqual(
                {
                    key: contract[key]
                    for key in (
                        "status",
                        "tracked_translation_units",
                        "generated_translation_units",
                        "total_translation_units",
                        "include_only_fragments",
                        "delivered_non_root_headers",
                        "deferred_hosted_translation_units",
                        "deferred_external_header_units",
                        "deferred_hermetic_units",
                    )
                },
                {
                    "status": "pass",
                    "tracked_translation_units": 428,
                    "generated_translation_units": 4,
                    "total_translation_units": 432,
                    "include_only_fragments": 24,
                    "delivered_non_root_headers": 3,
                    "deferred_hosted_translation_units": 0,
                    "deferred_external_header_units": 0,
                    "deferred_hermetic_units": 0,
                },
            )
            self.assertEqual(
                [
                    (
                        profile["name"],
                        profile["tracked_translation_units"],
                        profile["generated_translation_units"],
                    )
                    for profile in contract["profiles"]
                ],
                [
                    ("KERNEL_I386", 156, 4),
                    ("DOOM_COMPAT_I386", 3, 0),
                    ("DOOM_TREE_I386", 80, 0),
                    ("USER_I386", 3, 0),
                    ("FREESTANDING_I386", 1, 0),
                    ("CUPID_RUNTIME", 108, 0),
                    ("HOSTED_TOOLCHAIN_64", 0, 0),
                    ("HOSTED_KERNEL_BRIDGE_64", 0, 0),
                    ("HOSTED_I386_LINUX", 59, 0),
                    ("HOSTED_I386_WINDOWS", 9, 0),
                    ("HOSTED_I386_KERNEL_BRIDGE", 2, 0),
                    ("HOSTED_I386_LINUX_GNU", 3, 0),
                    ("HOSTED_I386_WINDOWS_UTF8", 1, 0),
                    ("HOSTED_I386_WINDOWS_PUBLICATION", 1, 0),
                    ("HOSTED_I386_WINDOWS_BUILD", 1, 0),
                    ("HOSTED_I386_WINDOWS_UTF8_GNU", 1, 0),
                ],
            )
            self.assertEqual(
                {
                    profile["name"]: profile["implicit_function_declarations"]
                    for profile in contract["profiles"]
                },
                {
                    "KERNEL_I386": False,
                    "DOOM_COMPAT_I386": True,
                    "DOOM_TREE_I386": True,
                    "USER_I386": False,
                    "FREESTANDING_I386": False,
                    "CUPID_RUNTIME": False,
                    "HOSTED_TOOLCHAIN_64": False,
                    "HOSTED_KERNEL_BRIDGE_64": False,
                    "HOSTED_I386_LINUX": False,
                    "HOSTED_I386_WINDOWS": False,
                    "HOSTED_I386_KERNEL_BRIDGE": False,
                    "HOSTED_I386_LINUX_GNU": False,
                    "HOSTED_I386_WINDOWS_UTF8": False,
                    "HOSTED_I386_WINDOWS_PUBLICATION": False,
                    "HOSTED_I386_WINDOWS_BUILD": False,
                    "HOSTED_I386_WINDOWS_UTF8_GNU": False,
                },
            )
            self.assertEqual(
                {
                    profile["name"]: profile[
                        "compatibility_pointer_conversions"
                    ]
                    for profile in contract["profiles"]
                },
                {
                    "KERNEL_I386": False,
                    "DOOM_COMPAT_I386": True,
                    "DOOM_TREE_I386": True,
                    "USER_I386": False,
                    "FREESTANDING_I386": False,
                    "CUPID_RUNTIME": False,
                    "HOSTED_TOOLCHAIN_64": False,
                    "HOSTED_KERNEL_BRIDGE_64": False,
                    "HOSTED_I386_LINUX": False,
                    "HOSTED_I386_WINDOWS": False,
                    "HOSTED_I386_KERNEL_BRIDGE": False,
                    "HOSTED_I386_LINUX_GNU": False,
                    "HOSTED_I386_WINDOWS_UTF8": False,
                    "HOSTED_I386_WINDOWS_PUBLICATION": False,
                    "HOSTED_I386_WINDOWS_BUILD": False,
                    "HOSTED_I386_WINDOWS_UTF8_GNU": False,
                },
            )
            self.assertEqual(
                audit_payload["summary"],
                {
                    "active_sources": 813,
                    "features": 257,
                    "transforms": 455,
                    "unreachable_sources": 106,
                },
            )
            features = {
                entry["id"]: entry for entry in audit_payload["features"]
            }
            unreachable = {
                entry["path"]: entry["classification"]
                for entry in audit_payload["unreachable_sources"]
            }
            for fixture in (
                "toolchain/tests/artifact_size_policy_contract.cc",
                "toolchain/tests/cupidbuild_artifact_allocation_contract.cc",
                "toolchain/tests/cupidbuild_artifact_race_contract.cc",
                "toolchain/tests/cupidbuild_observer_batch_contract.cc",
                "toolchain/tests/fat16_allocation_contract.cc",
            ):
                self.assertEqual(unreachable[fixture], "not_reached")
            for fixture in (
                "bootstrap/guest/cupidc_guest_linux_contract.cc",
                "bootstrap/guest/engine_image.cc",
                "bootstrap/guest/engine_image.h",
                "bootstrap/guest/engine_linux_contract.cc",
                "toolchain/tests/user_abi_allocation_probe.cc",
                "toolchain/tests/user_abi_observer_probe.cc",
            ):
                self.assertEqual(unreachable[fixture], "host_fixture")
            expected_c_expression_inventory = {
                "c.declaration.static_assert": (33, 8),
                "c.expression.sizeof": (6662, 202),
                "c.extension.builtin.offsetof": (13, 7),
                "c.extension.gnu_alignof": (1, 1),
            }
            for (
                feature_id,
                expected_counts,
            ) in expected_c_expression_inventory.items():
                feature = features[feature_id]
                self.assertEqual(
                    (feature["occurrences"], len(feature["files"])),
                    expected_counts,
                )
            self.assertEqual(
                features["c.extension.gnu_alignof"]["files"],
                ["kernel/core/process.cc"],
            )
            self.assertEqual(
                features["c.extension.gnu_alignof"]["examples"][0]["line"],
                41,
            )
            root_transform_by_output = {
                transform["output"]: transform
                for transform in audit_payload["build"]["transforms"]
            }
            user_build = next(
                build
                for build in audit_payload["supplemental_builds"]
                if build["directory"] == "user"
            )
            user_transform_by_output = {
                transform["output"]: transform
                for transform in user_build["transforms"]
            }
            for program in ("hello", "ls", "cat"):
                with self.subTest(user_program_disassembly_gate=program):
                    transform = user_transform_by_output[
                        f"user/build/{program}"
                    ]
                    self.assertEqual(
                        transform["tools"],
                        [
                            "cupid_builder",
                            "cupid_disassembler",
                            "cupid_linker",
                        ],
                    )
                    self.assertEqual(
                        transform["operation"],
                        "link_elf32_executable",
                    )
            iso_transform = root_transform_by_output["test_iso/hello.iso"]
            self.assertEqual(
                iso_transform["tools"],
                ["cupid_builder", "cupid_object"],
            )
            self.assertEqual(
                iso_transform["operation"],
                "package_iso9660_image",
            )
            self.assertEqual(
                set(iso_transform["inputs"]),
                {
                    "Makefile",
                    *WINDOWS_PRODUCTION_SEED_INPUTS,
                    *LINUX_BOOTSTRAP_SEED_INPUTS,
                    "test_iso/fixtures",
                    "test_iso/fixtures.manifest",
                    "test_iso/fixtures/big.bin",
                    "test_iso/fixtures/gen_big.sh",
                    "test_iso/fixtures/jpeg_baseline_8x8.jpg",
                    "test_iso/fixtures/long_named_file.txt",
                    "test_iso/fixtures/readme.txt",
                    "test_iso/fixtures/sub",
                    "test_iso/fixtures/sub/nested.txt",
                },
            )
            big_fixture_transform = root_transform_by_output[
                "test_iso/fixtures/big.bin"
            ]
            self.assertEqual(
                big_fixture_transform["tools"],
                ["cupid_assembler", "cupid_disassembler", "cupid_builder"],
            )
            self.assertEqual(
                big_fixture_transform["operation"],
                "assemble_flat_binary",
            )
            self.assertEqual(
                set(big_fixture_transform["inputs"]),
                {
                    "Makefile",
                    *WINDOWS_PRODUCTION_SEED_INPUTS,
                    "test_iso/big_pattern.asm",
                },
            )
            system_image_transform = root_transform_by_output["cupidos.img"]
            self.assertEqual(
                system_image_transform["operation"],
                "package_disk_image",
            )
            self.assertIn(
                "test_iso/hello.iso",
                system_image_transform["inputs"],
            )
            checked_cupidc_roots = []
            native_cupidc_roots = []
            python_cupidc_roots = []
            for transform in root_transform_by_output.values():
                if transform["operation"] != "compile_c_to_elf32_object":
                    continue
                self.assertIn(transform["tools"], (
                    ["cupid_builder", "cupid_c_compiler"],
                    ["cupid_c_compiler", "host_python"],
                ))
                roots = [
                    path
                    for path in transform["inputs"]
                    if Path(path).suffix in {".c", ".cc"}
                ]
                self.assertEqual(len(roots), 1, transform["output"])
                checked_cupidc_roots.extend(roots)
                if "cupid_builder" in transform["tools"]:
                    native_cupidc_roots.extend(roots)
                else:
                    python_cupidc_roots.extend(roots)
            self.assertEqual(len(native_cupidc_roots), 243)
            self.assertEqual(set(native_cupidc_roots), {
                *FROZEN_KERNEL_INPUT_CLOSURES,
                *APPROVED_DOOM_COMPAT_SOURCES,
                *APPROVED_DOOM_TREE_SOURCES,
                "kernel/util/bin_programs_gen.cc",
                "kernel/util/demos_programs_gen.cc",
                "kernel/util/docs_programs_gen.cc",
            })
            self.assertEqual(len(python_cupidc_roots), 0)
            self.assertEqual(set(python_cupidc_roots), set())
            self.assertFalse(set(native_cupidc_roots) & set(python_cupidc_roots))
            seed_bound_roots = {
                "toolchain/ctool.cc",
                "toolchain/cupidasm.cc",
                "toolchain/cupiddis.cc",
                "toolchain/elf32.cc",
                "toolchain/x86.cc",
            }
            self.assertEqual(len(checked_cupidc_roots), 243)
            self.assertEqual(
                {
                    path
                    for path in checked_cupidc_roots
                    if Path(path).suffix == ".c"
                },
                set(),
            )
            self.assertTrue(
                seed_bound_roots.issubset(set(checked_cupidc_roots))
            )
            self.assertEqual(
                sum(
                    Path(path).suffix == ".cc" for path in checked_cupidc_roots
                ),
                243,
            )
            symbol_transform = root_transform_by_output[
                "kernel/cpu/ksyms_data.cc"
            ]
            self.assertEqual(
                symbol_transform["tools"],
                [
                    "cupid_builder",
                    "cupid_disassembler",
                    "cupid_object",
                ],
            )
            self.assertEqual(
                symbol_transform["operation"], "generate_ksyms_source"
            )
            self.assertEqual(
                set(symbol_transform["inputs"]),
                {
                    "Makefile",
                    "kernel/kernel.elf.pass1",
                    *WINDOWS_PRODUCTION_SEED_INPUTS,
                },
            )
            self.assertFalse(
                any(
                    "host_symbol_reader" in transform["tools"]
                    for transform in audit_payload["build"]["transforms"]
                )
            )
            established_cupidc_kernel_sources = (
                "kernel/crypto/aes.cc",
                "kernel/crypto/aes_gcm.cc",
                "kernel/crypto/asn1.cc",
                "kernel/crypto/bigint.cc",
                "kernel/crypto/chacha20.cc",
                "kernel/crypto/chacha20poly1305.cc",
                "kernel/crypto/csprng.cc",
                "kernel/crypto/ct.cc",
                "kernel/crypto/ecdsa.cc",
                "kernel/crypto/ed25519.cc",
                "kernel/crypto/hkdf.cc",
                "kernel/crypto/hmac.cc",
                "kernel/crypto/p256.cc",
                "kernel/crypto/poly1305.cc",
                "kernel/crypto/rsa.cc",
                "kernel/crypto/sha256.cc",
                "kernel/crypto/sha512.cc",
                "kernel/crypto/x25519.cc",
                "kernel/crypto/x509.cc",
                "kernel/crypto/x509_chain.cc",
                "drivers/e1000.cc",
                "kernel/gui/desktop.cc",
                "kernel/network/socket.cc",
                "kernel/network/tcp.cc",
                "kernel/smp/acpi.cc",
                "kernel/smp/mp_tables.cc",
            )
            port_io_header_closures = {
                "drivers/ata.cc": (
                    "drivers/ata.h",
                    "kernel/core/debug.h",
                    "kernel/core/kernel.h",
                    "kernel/core/ports.h",
                    "kernel/core/types.h",
                    "kernel/cpu/isr.h",
                    "kernel/fs/blockdev.h",
                ),
                "drivers/keyboard.cc": (
                    "drivers/keyboard.h",
                    "drivers/rtc.h",
                    "drivers/serial.h",
                    "drivers/vga.h",
                    "kernel/core/kernel.h",
                    "kernel/core/ports.h",
                    "kernel/core/process.h",
                    "kernel/core/types.h",
                    "kernel/cpu/irq.h",
                    "kernel/cpu/isr.h",
                    "kernel/gui/desktop.h",
                    "kernel/gui/gui.h",
                    "kernel/lang/shell.h",
                    "kernel/util/calendar.h",
                ),
                "drivers/mouse.cc": (
                    "drivers/mouse.h",
                    "drivers/serial.h",
                    "drivers/vga.h",
                    "kernel/core/ports.h",
                    "kernel/core/string.h",
                    "kernel/core/types.h",
                    "kernel/cpu/isr.h",
                    "kernel/cpu/pic.h",
                    "kernel/gfx/graphics.h",
                ),
                "drivers/pci.cc": (
                    "drivers/pci.h",
                    "drivers/serial.h",
                    "kernel/core/ports.h",
                    "kernel/core/types.h",
                ),
                "drivers/pit.cc": (
                    "drivers/pit.h",
                    "kernel/core/ports.h",
                    "kernel/core/types.h",
                ),
                "drivers/rtc.cc": (
                    "drivers/rtc.h",
                    "drivers/serial.h",
                    "kernel/core/kernel.h",
                    "kernel/core/ports.h",
                    "kernel/core/types.h",
                    "kernel/cpu/isr.h",
                ),
                "drivers/rtl8139.cc": (
                    "drivers/pci.h",
                    "drivers/serial.h",
                    "kernel/core/ports.h",
                    "kernel/core/types.h",
                    "kernel/cpu/irq.h",
                    "kernel/cpu/isr.h",
                    "kernel/mm/memory.h",
                    "kernel/network/net_if.h",
                ),
                "drivers/speaker.cc": (
                    "drivers/pit.h",
                    "drivers/speaker.h",
                    "drivers/timer.h",
                    "kernel/core/kernel.h",
                    "kernel/core/ports.h",
                    "kernel/core/types.h",
                    "kernel/cpu/isr.h",
                ),
                "drivers/vga.cc": (
                    "drivers/timer.h",
                    "drivers/vga.h",
                    "kernel/core/kernel.h",
                    "kernel/core/ports.h",
                    "kernel/core/string.h",
                    "kernel/core/types.h",
                    "kernel/cpu/isr.h",
                    "kernel/cpu/simd.h",
                    "kernel/mm/memory.h",
                ),
                "kernel/audio/ac97.cc": (
                    "drivers/pci.h",
                    "drivers/serial.h",
                    "kernel/audio/ac97.h",
                    "kernel/core/kernel.h",
                    "kernel/core/ports.h",
                    "kernel/core/types.h",
                    "kernel/cpu/irq.h",
                    "kernel/cpu/isr.h",
                    "kernel/mm/memory.h",
                ),
                "kernel/core/syscall.cc": (
                    "drivers/ata.h",
                    "drivers/pci.h",
                    "drivers/pit.h",
                    "drivers/serial.h",
                    "drivers/speaker.h",
                    "drivers/timer.h",
                    "kernel/core/kernel.h",
                    "kernel/core/ports.h",
                    "kernel/core/process.h",
                    "kernel/core/string.h",
                    "kernel/core/syscall.h",
                    "kernel/core/types.h",
                    "kernel/cpu/isr.h",
                    "kernel/fs/blockdev.h",
                    "kernel/fs/vfs.h",
                    "kernel/fs/vfs_helpers.h",
                    "kernel/lang/exec.h",
                    "kernel/lang/shell.h",
                    "kernel/mm/memory.h",
                    "kernel/network/arp.h",
                    "kernel/network/dns.h",
                    "kernel/network/icmp.h",
                    "kernel/network/ip.h",
                    "kernel/network/net_if.h",
                    "kernel/network/socket.h",
                    "kernel/network/udp.h",
                    "kernel/smp/bkl.h",
                    "kernel/smp/lapic.h",
                ),
                "kernel/lang/shell.cc": (
                    "drivers/keyboard.h",
                    "drivers/pci.h",
                    "drivers/rtc.h",
                    "drivers/serial.h",
                    "drivers/timer.h",
                    "drivers/vga.h",
                    "kernel/core/app_launch.h",
                    "kernel/core/assert.h",
                    "kernel/core/kernel.h",
                    "kernel/core/panic.h",
                    "kernel/core/ports.h",
                    "kernel/core/process.h",
                    "kernel/core/string.h",
                    "kernel/core/types.h",
                    "kernel/cpu/irq.h",
                    "kernel/cpu/isr.h",
                    "kernel/cpu/math.h",
                    "kernel/fs/blockcache.h",
                    "kernel/fs/blockdev.h",
                    "kernel/fs/fat16.h",
                    "kernel/fs/fs.h",
                    "kernel/fs/vfs.h",
                    "kernel/gfx/gfx2d.h",
                    "kernel/gui/ansi.h",
                    "kernel/gui/desktop.h",
                    "kernel/gui/gui.h",
                    "kernel/gui/gui_themes.h",
                    "kernel/gui/terminal_app.h",
                    "kernel/lang/as.h",
                    "kernel/lang/cupidc.h",
                    "kernel/lang/cupidscript.h",
                    "kernel/lang/cupidscript_arrays.h",
                    "kernel/lang/cupidscript_jobs.h",
                    "kernel/lang/cupidscript_streams.h",
                    "kernel/lang/dis.h",
                    "kernel/lang/exec.h",
                    "kernel/lang/shell.h",
                    "kernel/mm/memory.h",
                    "kernel/mm/swap.h",
                    "kernel/network/arp.h",
                    "kernel/network/dns.h",
                    "kernel/network/icmp.h",
                    "kernel/network/ip.h",
                    "kernel/network/net_if.h",
                    "kernel/network/socket.h",
                    "kernel/network/sshd.h",
                    "kernel/smp/bkl.h",
                    "kernel/smp/percpu.h",
                    "kernel/smp/smp.h",
                    "kernel/usb/usb.h",
                    "kernel/usb/usb_hc.h",
                    "kernel/util/calendar.h",
                    "toolchain/ctool.h",
                    "toolchain/cupiddis.h",
                    "toolchain/elf32.h",
                    "toolchain/x86.h",
                    "kernel/gfx/gfx2d_icons.h",
                    "kernel/lang/as_elf.h",
                    "toolchain/cupidasm.h",
                    "toolchain/cupidld.h",
                    "toolchain/pe32.h",
                ),
                "kernel/usb/ehci.cc": (
                    "drivers/pci.h",
                    "drivers/serial.h",
                    "drivers/timer.h",
                    "kernel/core/kernel.h",
                    "kernel/core/panic.h",
                    "kernel/core/ports.h",
                    "kernel/core/types.h",
                    "kernel/cpu/irq.h",
                    "kernel/cpu/isr.h",
                    "kernel/mm/memory.h",
                    "kernel/usb/usb.h",
                    "kernel/usb/usb_hc.h",
                ),
                "kernel/usb/uhci.cc": (
                    "drivers/pci.h",
                    "drivers/serial.h",
                    "drivers/timer.h",
                    "kernel/core/kernel.h",
                    "kernel/core/panic.h",
                    "kernel/core/ports.h",
                    "kernel/core/types.h",
                    "kernel/cpu/irq.h",
                    "kernel/cpu/isr.h",
                    "kernel/mm/memory.h",
                    "kernel/usb/usb.h",
                    "kernel/usb/usb_hc.h",
                ),
            }
            self.assertEqual(len(port_io_header_closures), 14)
            floating_gfx_header_closures = {
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
            }
            libm_header_closures = {
                "kernel/cpu/libm.cc": (
                    "kernel/core/types.h",
                    "kernel/cpu/libm.h",
                ),
            }
            fpu_smp_header_closures = {
                "kernel/cpu/fpu.cc": (
                    "drivers/serial.h",
                    "kernel/core/panic.h",
                    "kernel/core/process.h",
                    "kernel/core/types.h",
                    "kernel/cpu/fpu.h",
                    "kernel/cpu/isr.h",
                    "kernel/cpu/libm.h",
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
            cupidc_control_inputs = (
                "Makefile",
                "tools/cupidc_kernel_compile.py",
                "tools/kernel_cupidc_frontier.py",
                "tools/bootstrap_toolchain.py",
                *WINDOWS_PRODUCTION_SEED_INPUTS,
            )
            closed_header_closures = {
                **port_io_header_closures,
                **floating_gfx_header_closures,
                **libm_header_closures,
                **fpu_smp_header_closures,
            }
            for source_path, headers in closed_header_closures.items():
                output_path = Path(source_path).with_suffix(".o").as_posix()
                with self.subTest(port_io_closure=source_path):
                    self.assertEqual(
                        root_transform_by_output[output_path]["inputs"],
                        [source_path, *headers, *(
                            ("Makefile", *WINDOWS_PRODUCTION_SEED_INPUTS)
                            if source_path in FROZEN_KERNEL_INPUT_CLOSURES
                            else cupidc_control_inputs
                        )],
                    )

            cupidc_kernel_sources = (
                *established_cupidc_kernel_sources,
                *port_io_header_closures,
                *floating_gfx_header_closures,
                *libm_header_closures,
                *fpu_smp_header_closures,
            )
            for source_path in cupidc_kernel_sources:
                output_path = Path(source_path).with_suffix(".o").as_posix()
                with self.subTest(cupidc_kernel_source=source_path):
                    transform = root_transform_by_output[output_path]
                    self.assertEqual(
                        transform["tools"],
                        (["cupid_builder", "cupid_c_compiler"]
                         if source_path in FROZEN_KERNEL_INPUT_CLOSURES
                         else ["cupid_c_compiler", "host_python"]),
                    )
                    self.assertEqual(
                        transform["operation"],
                        "compile_c_to_elf32_object",
                    )
                    self.assertIn(source_path, transform["inputs"])

            all_transforms = [
                *audit_payload["build"]["transforms"],
                *[
                    transform
                    for build in audit_payload["supplemental_builds"]
                    for transform in build["transforms"]
                ],
            ]
            self.assertEqual(
                {
                    tool: sum(
                        tool in transform["tools"]
                        for transform in all_transforms
                    )
                    for tool in (
                        "cupid_c_compiler",
                        "cupid_assembler",
                        "cupid_builder",
                        "cupid_object",
                        "cupid_linker",
                        "cupid_disassembler",
                        "cupid_c_contract",
                        "host_c_compiler",
                        "host_python",
                    )
                },
                {
                    "cupid_c_compiler": 248,
                    "cupid_assembler": 7,
                    "cupid_builder": 452,
                    "cupid_object": 195,
                    "cupid_linker": 7,
                    "cupid_disassembler": 10,
                    "cupid_c_contract": 2,
                    "host_c_compiler": 0,
                    "host_python": 3,
                },
            )
            self.assertFalse(
                any(
                    not {
                        "cupid_c_compiler",
                        "cupid_assembler",
                        "cupid_builder",
                        "cupid_object",
                        "cupid_linker",
                        "cupid_disassembler",
                        "cupid_c_contract",
                    }.intersection(transform["tools"])
                    for transform in all_transforms
                )
            )

            toolchain_cohort = next(
                cohort
                for cohort in audit_payload["roadmap"]["source_cohort_order"]
                if cohort["id"] == "toolchain_sources"
            )
            self.assertEqual(toolchain_cohort["source_count"], 155)
            user_program_cohort = next(
                cohort
                for cohort in audit_payload["roadmap"]["source_cohort_order"]
                if cohort["id"] == "user_programs"
            )
            self.assertEqual(user_program_cohort["source_count"], 4)
            self.assertEqual(
                user_program_cohort["rationale"],
                "Keep the checked-seed CupidC and CupidLD user build "
                "reproducible on Linux and Windows, keep the native Windows "
                "oracle explicit, then stage its validated executables "
                "deliberately.",
            )

            source_by_path = {
                source["path"]: source for source in audit_payload["sources"]
            }
            self.assertEqual(
                source_by_path["kernel/lang/as.cc"]["cohort"],
                "cupidasm",
            )
            for source_path in cupidc_kernel_sources:
                with self.subTest(cupidc_owned_source=source_path):
                    self.assertEqual(
                        source_by_path[source_path]["runtime_owner"],
                        "CupidC",
                    )
                    self.assertIn(
                        "cupid_c_compiler",
                        source_by_path[source_path]["build_owners"],
                    )
                    self.assertNotIn(
                        "host_c_compiler",
                        source_by_path[source_path]["build_owners"],
                    )
            self.assertEqual(
                source_by_path["kernel/crypto/aes.h"]["runtime_owner"],
                "CupidC",
            )
            frontend_sources = {
                "toolchain/cupidbuild_iso.cc": ("toolchain_core", "CupidC"),
                "toolchain/cupidbuild_iso_capture.cc": ("toolchain_core", "CupidC"),
                "toolchain/cupidbuild_iso_image.cc": ("toolchain_core", "CupidC"),
                "toolchain/cupidbuild_iso_publication.cc": ("toolchain_core", "CupidC"),
                "toolchain/cupidbuild_iso_publication.h": ("toolchain_core", "CupidC"),
                "toolchain/cupidbuild_artifacts.cc": ("toolchain_core", "CupidC"),
                "toolchain/cupidbuild_artifacts.h": ("toolchain_core", "CupidC"),
                "toolchain/artifact_size_policy.cc": ("toolchain_core", "CupidC"),
                "toolchain/seed_manifest.cc": ("toolchain_core", "CupidC"),
                "toolchain/seed_manifest.h": ("toolchain_core", "CupidC"),
                "toolchain/seed_release.cc": ("toolchain_core", "CupidC"),
                "toolchain/seed_release.h": ("toolchain_core", "CupidC"),
                "toolchain/cupidc_emit.cc": ("toolchain_core", "CupidC"),
                "toolchain/cupidc_emit.h": ("toolchain_core", "CupidC"),
                "toolchain/cupidc_frontend.cc": ("toolchain_core", "CupidC"),
                "toolchain/cupidc_frontend.h": ("toolchain_core", "CupidC"),
                "toolchain/cupidc_ir.cc": ("toolchain_core", "CupidC"),
                "toolchain/cupidc_ir.h": ("toolchain_core", "CupidC"),
                "toolchain/cupidc_main.cc": ("toolchain_core", "CupidC"),
                "toolchain/hosted/i386-linux/runtime.cc": (
                    "toolchain_core",
                    "CupidC",
                ),
                "toolchain/hosted/i386-linux/start.asm": (
                    "toolchain_core",
                    "CupidASM",
                ),
                "toolchain/hosted/i386-windows/runtime.cc": (
                    "toolchain_core",
                    "CupidC",
                ),
                "toolchain/hosted/i386-windows/publication_start.asm": (
                    "toolchain_core",
                    "CupidASM",
                ),
                "toolchain/hosted/i386-windows/start.asm": (
                    "toolchain_core",
                    "CupidASM",
                ),
                "toolchain/hosted/i386-windows/tool_start.asm": (
                    "toolchain_core",
                    "CupidASM",
                ),
                "toolchain/tests/core_contract.cc": (
                    "toolchain_contract",
                    "CupidC",
                ),
                "toolchain/tests/cupidasm_contract.cc": (
                    "toolchain_contract",
                    "CupidC",
                ),
                "toolchain/tests/cupidasm_demos_contract.cc": (
                    "toolchain_contract",
                    "CupidC",
                ),
                "toolchain/tests/cupidasm_kernel_elf_contract.cc": (
                    "toolchain_contract",
                    "CupidC",
                ),
                "toolchain/tests/cupidc_frontend_contract.cc": (
                    "toolchain_contract",
                    "CupidC",
                ),
                "toolchain/tests/cupidc_ir_contract.cc": (
                    "toolchain_contract",
                    "CupidC",
                ),
                "toolchain/tests/cupidc_object_contract.cc": (
                    "toolchain_contract",
                    "CupidC",
                ),
                "toolchain/tests/cupidc_pp_contract.cc": (
                    "toolchain_contract",
                    "CupidC",
                ),
                "toolchain/tests/cupidc_type_contract.cc": (
                    "toolchain_contract",
                    "CupidC",
                ),
                "toolchain/tests/cupiddis_contract.cc": (
                    "toolchain_contract",
                    "CupidC",
                ),
                "toolchain/tests/cupidld_contract.cc": (
                    "toolchain_contract",
                    "CupidC",
                ),
                "toolchain/tests/cupidobj_contract.cc": (
                    "toolchain_contract",
                    "CupidC",
                ),
                "toolchain/tests/elf32_contract.cc": (
                    "toolchain_contract",
                    "CupidC",
                ),
                "toolchain/tests/x86_contract.cc": (
                    "toolchain_contract",
                    "CupidC",
                ),
                "toolchain/tests/hosted_i386_runtime_contract.cc": (
                    "toolchain_contract",
                    "CupidC",
                ),
                "toolchain/tests/hosted_i386_windows_contract.cc": (
                    "toolchain_contract",
                    "CupidC",
                ),
                "toolchain/tests/hosted_i386_windows_runtime_contract.cc": (
                    "toolchain_contract",
                    "CupidC",
                ),
                "toolchain/tests/toolchain_manifest_contract.cc": (
                    "toolchain_contract",
                    "CupidC",
                ),
            }
            for path, (cohort, runtime_owner) in frontend_sources.items():
                with self.subTest(path=path):
                    self.assertEqual(source_by_path[path]["cohort"], cohort)
                    self.assertEqual(
                        source_by_path[path]["reachability"],
                        "direct_build_input",
                    )
                    self.assertEqual(
                        source_by_path[path]["runtime_owner"],
                        runtime_owner,
                    )
            self.assertEqual(
                source_by_path[
                    "toolchain/tests/hosted_i386_runtime_contract.cc"
                ]["language"],
                "cupid_c",
            )
            self.assertNotIn(
                "toolchain/tests/hosted_i386_runtime_contract.c",
                source_by_path,
            )

            toolchain_build = next(
                build
                for build in audit_payload["supplemental_builds"]
                if build["directory"] == "toolchain"
            )
            self.assertEqual(len(toolchain_build["transforms"]), 2)
            toolchain_transform_by_output = {
                transform["output"]: transform
                for transform in toolchain_build["transforms"]
            }
            self.assertNotIn("toolchain/build", toolchain_transform_by_output)
            contract_verifier = toolchain_transform_by_output["toolchain/all"]
            self.assertEqual(
                contract_verifier["operation"],
                "verify_toolchain_manifest",
            )
            self.assertEqual(
                contract_verifier["tools"],
                [
                    "cupid_assembler",
                    "cupid_c_compiler",
                    "cupid_c_contract",
                    "cupid_linker",
                    "host_python",
                ],
            )
            self.assertEqual(
                len(contract_verifier["inputs"]),
                len(module.TOOLCHAIN_MANIFEST_CONTRACT_TRANSFORM_INPUTS),
            )
            self.assertEqual(
                set(contract_verifier["inputs"]),
                module.TOOLCHAIN_MANIFEST_CONTRACT_TRANSFORM_INPUTS,
            )
            self.assertEqual(
                contract_verifier["recipe"],
                ["$(TOOLCHAIN_MANIFEST_CONTRACT)"],
            )
            contract_manifest = toolchain_transform_by_output[
                "toolchain/build/cupidc-contracts/manifest.json"
            ]
            self.assertEqual(
                contract_manifest["operation"],
                "generate_toolchain_manifest",
            )
            self.assertEqual(
                contract_manifest["tools"],
                [
                    "cupid_assembler",
                    "cupid_c_compiler",
                    "cupid_c_contract",
                    "cupid_linker",
                    "host_python",
                ],
            )
            for input_path in (
                "toolchain/hosted/i386-linux/runtime.cc",
                "toolchain/hosted/i386-linux/start.asm",
                "toolchain/hosted/i386-windows/runtime.cc",
                "toolchain/hosted/i386-windows/start.asm",
                "toolchain/hosted/i386-windows/tool_start.asm",
                "link.ld",
                "toolchain/tests/hosted_i386_runtime_contract.cc",
                "toolchain/tests/hosted_i386_windows_contract.cc",
                "toolchain/tests/hosted_i386_windows_runtime_contract.cc",
                "toolchain/cupidc_main.cc",
                "toolchain/tests/cupidc_object_contract.cc",
                "toolchain/tests/user_syscall_abi_contract.cc",
                "tools/cupidc_toolchain_contracts.py",
                "tools/user_syscall_abi.py",
                "user/cupid.h",
                "bootstrap/seeds/i386-linux/cupidc.elf",
            ):
                with self.subTest(contract_input=input_path):
                    self.assertIn(input_path, contract_manifest["inputs"])
            self.assertNotIn(
                "host_c_compiler",
                {
                    tool
                    for transform in toolchain_build["transforms"]
                    for tool in transform["tools"]
                },
            )
            self.assertIn(
                "`c_preprocessor_translation_units` | `pass` | "
                "428 tracked + 4 generated",
                summary.read_text(encoding="utf-8"),
            )
            audit_payload["build"]["transforms"].append(
                {
                    "output": "bin/new.h.o",
                    "inputs": ["bin/new.h"],
                    "tools": ["cupid_object", "cupid_builder"],
                    "operation": "wrap_text_as_elf32_relocatable",
                    "recipe": ["$(CUPIDOBJ) wrap-text $< -o $@"],
                }
            )
            with self.assertRaisesRegex(
                module.AuditError,
                r"CupidObj install-source delivery content inputs changed",
            ):
                module._c_preprocessor_active_cases_manifest(audit_payload)
            checked = subprocess.run(
                [*command, "--check"], text=True, capture_output=True
            )
            self.assertEqual(checked.returncode, 0, checked.stderr)

            manifest.write_text(
                manifest.read_text(encoding="utf-8") + "/* stale */\n",
                encoding="utf-8",
            )
            stale = subprocess.run(
                [*command, "--check"], text=True, capture_output=True
            )
            self.assertEqual(stale.returncode, 1)
            self.assertIn(manifest.name, stale.stderr)
