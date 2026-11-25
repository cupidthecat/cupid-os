## @file
#  @brief Check checked seed authority for the ISO, symbols, and kernel.
import shutil
import tempfile
from pathlib import Path
from unittest import mock
from tests.build_graph_audit_fixtures import (
    LINUX_BOOTSTRAP_SEED_INPUTS,
    LINUX_PRODUCTION_SEED_INPUTS,
    REPO_ROOT,
    WINDOWS_EXECUTION_SEED_INPUTS,
    WINDOWS_PRODUCTION_SEED_INPUTS,
    _load_audit_module,
)


class SeedProductCases:
    def test_production_seed_inputs_follow_the_make_host_branch(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        module = _load_audit_module()
        cases = (
            (
                "OS=Windows_NT",
                "i386-windows",
                "exe",
            ),
            (
                "OS=Linux",
                "i386-linux",
                "elf",
            ),
        )
        for os_assignment, platform, suffix in cases:
            with (
                self.subTest(os_assignment=os_assignment),
                mock.patch.object(
                    module,
                    "CANONICAL_MAKE_VARIABLES",
                    (os_assignment,),
                ),
            ):
                root_values = module._read_evaluated_make_variables(
                    REPO_ROOT,
                    make,
                    (
                        "BOOTSTRAP_SEED_MANIFEST",
                        "BOOTSTRAP_WINDOWS_SEED_MANIFEST",
                        "PRODUCTION_SEED_MANIFEST",
                        "PRODUCTION_SEED_INPUTS",
                        "CHECKED_SEED_INPUTS",
                    ),
                )
                user_values = module._read_evaluated_make_variables(
                    REPO_ROOT / "user",
                    make,
                    (
                        "BOOTSTRAP_SEED_MANIFEST",
                        "BOOTSTRAP_SEED_INPUTS",
                        "PRODUCTION_SEED_MANIFEST",
                        "CHECKED_SEED_INPUTS",
                    ),
                )

            root_manifest = f"bootstrap/seeds/{platform}/manifest.json"
            root_images = {
                f"bootstrap/seeds/{platform}/{tool}.{suffix}"
                for tool in (
                    "cupidasm",
                    "cupidc",
                    "cupiddis",
                    "cupidld",
                    "cupidobj",
                    "cupidbuild",
                )
            }
            self.assertEqual(
                root_values["PRODUCTION_SEED_MANIFEST"],
                root_manifest,
            )
            self.assertEqual(
                set(root_values["PRODUCTION_SEED_INPUTS"].split()),
                {root_manifest, "bootstrap/seeds/release.json", *root_images},
            )
            self.assertEqual(
                set(root_values["CHECKED_SEED_INPUTS"].split()),
                {
                    "Makefile",
                    "tools/bootstrap_toolchain.py",
                    root_manifest,
                    "bootstrap/seeds/release.json",
                    *root_images,
                },
            )

            user_manifest = f"../{root_manifest}"
            user_images = {f"../{path}" for path in root_images}
            self.assertEqual(
                user_values["PRODUCTION_SEED_MANIFEST"],
                user_manifest,
            )
            self.assertEqual(
                set(user_values["CHECKED_SEED_INPUTS"].split()),
                {user_manifest, "../bootstrap/seeds/release.json", *user_images},
            )
            self.assertEqual(
                root_values["BOOTSTRAP_SEED_MANIFEST"],
                "bootstrap/seeds/i386-linux/manifest.json",
            )
            self.assertEqual(
                root_values["BOOTSTRAP_WINDOWS_SEED_MANIFEST"],
                "bootstrap/seeds/i386-windows/manifest.json",
            )
            self.assertEqual(
                user_values["BOOTSTRAP_SEED_MANIFEST"],
                "../bootstrap/seeds/i386-linux/manifest.json",
            )
            self.assertEqual(
                set(user_values["BOOTSTRAP_SEED_INPUTS"].split()),
                {f"../{path}" for path in LINUX_BOOTSTRAP_SEED_INPUTS},
            )

    def test_bootstrap_recipes_keep_their_explicit_seed_roles(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        module = _load_audit_module()
        rules = module._parse_make_rules(
            module._run_make_database(
                REPO_ROOT,
                make,
                "bootstrap-from-seed",
            )
        )
        expected_fragments = {
            "verify-bootstrap-seed": (
                "tools/bootstrap_toolchain.py verify",
                "--manifest $(BOOTSTRAP_SEED_MANIFEST)",
            ),
            "bootstrap-from-seed": (
                "tools/bootstrap_toolchain.py bootstrap",
                "--root . --manifest $(BOOTSTRAP_SEED_MANIFEST)",
            ),
            "verify-windows-bootstrap-seed": (
                "tools/bootstrap_toolchain.py verify",
                "--manifest $(BOOTSTRAP_WINDOWS_SEED_MANIFEST)",
            ),
            "bootstrap-windows-from-seed": (
                "tools/bootstrap_toolchain.py bootstrap-windows",
                "--manifest $(BOOTSTRAP_WINDOWS_SEED_MANIFEST)",
                "--plan-manifest $(BOOTSTRAP_SEED_MANIFEST)",
            ),
            "verify-artifact-sizes": (
                "$(PRODUCTION_SEED_DIRECTORY)cupidbuild.$(PRODUCTION_SEED_SUFFIX) verify-artifact-sizes",
                '--root "$(CURDIR)" --policy $(ARTIFACT_SIZE_POLICY)',
                "--seed-manifest $(BOOTSTRAP_SEED_MANIFEST)",
            ),
        }
        for target, fragments in expected_fragments.items():
            with self.subTest(target=target):
                recipe = "\n".join(rules[target].recipe)
                for fragment in fragments:
                    self.assertIn(fragment, recipe)
                if target != "verify-artifact-sizes":
                    self.assertNotIn("PRODUCTION_SEED_MANIFEST", recipe)
        contract_command = " ".join(rules["verify-artifact-sizes"].recipe)
        self.assertNotIn("$(PYTHON)", contract_command)
        self.assertIn("--checked-manifest $(BOOTSTRAP_WINDOWS_SEED_MANIFEST)", contract_command)
        self.assertIn("--execution-manifest $(PRODUCTION_SEED_MANIFEST)", contract_command)

    def test_root_production_seed_override_moves_the_trust_unit(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        module = _load_audit_module()
        with tempfile.TemporaryDirectory(
            prefix=".audit-seed-",
            dir=REPO_ROOT,
        ) as td:
            seed_directory = Path(td)
            manifest = seed_directory / "manifest.json"
            manifest.write_text("{}\n", encoding="utf-8")
            for tool in (
                "cupidasm",
                "cupidc",
                "cupiddis",
                "cupidld",
                "cupidobj",
                "cupidbuild",
            ):
                (seed_directory / f"{tool}.exe").write_bytes(b"seed\n")
            relative_manifest = manifest.relative_to(REPO_ROOT).as_posix()
            variables = (
                *module.CANONICAL_MAKE_VARIABLES,
                f"PRODUCTION_SEED_MANIFEST={relative_manifest}",
            )
            with mock.patch.object(
                module,
                "CANONICAL_MAKE_VARIABLES",
                variables,
            ):
                rules = module._parse_make_rules(
                    module._run_make_database(
                        REPO_ROOT,
                        make,
                        "boot/boot.bin",
                    )
                )

            expected = {
                relative_manifest,
                *{
                    (seed_directory / f"{tool}.exe")
                    .relative_to(REPO_ROOT)
                    .as_posix()
                    for tool in (
                        "cupidasm",
                        "cupidc",
                        "cupiddis",
                        "cupidld",
                        "cupidobj",
                        "cupidbuild",
                    )
                },
            }
            for target in (
                "boot/boot.bin",
                "build/bootstrap/doom-cupidc-inputs.json",
            ):
                with self.subTest(target=target):
                    inputs = set(rules[target].prerequisites)
                    self.assertTrue(expected.issubset(inputs))
                    self.assertNotIn(
                        "bootstrap/seeds/i386-windows/manifest.json",
                        inputs,
                    )

            artifact_inputs = set(rules["verify-artifact-sizes"].prerequisites)
            self.assertTrue(expected.issubset(artifact_inputs))
            self.assertTrue(set(LINUX_BOOTSTRAP_SEED_INPUTS).issubset(artifact_inputs))
            self.assertTrue(set(WINDOWS_PRODUCTION_SEED_INPUTS).issubset(artifact_inputs))
            self.assertIn("bootstrap/seeds/release.json", artifact_inputs)

    def test_guarded_objects_ignore_standalone_tool_overrides(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        module = _load_audit_module()
        with tempfile.TemporaryDirectory(
            prefix=".audit-tool-",
            dir=REPO_ROOT,
        ) as td:
            driver = Path(td) / "cupidasm-driver"
            driver.write_bytes(b"driver\n")
            relative_driver = driver.relative_to(REPO_ROOT).as_posix()
            variables = (
                *module.CANONICAL_MAKE_VARIABLES,
                "CUPIDASM=custom-cupidasm",
                f"CUPIDASM_INPUTS={relative_driver}",
                "CUPIDDIS=custom-cupiddis",
                f"CUPIDDIS_INPUTS={relative_driver}",
                "PYTHON=custom-python",
            )
            with mock.patch.object(
                module,
                "CANONICAL_MAKE_VARIABLES",
                variables,
            ):
                rules_by_target = {
                    target: module._parse_make_rules(
                        module._run_make_database(
                            REPO_ROOT,
                            make,
                            target,
                        )
                    )
                    for target in (
                        "kernel/cpu/isr.o",
                        "kernel/core/context_switch.o",
                    )
                }

            for target, rules in rules_by_target.items():
                with self.subTest(target=target):
                    rule = rules[target]
                    inputs = set(rule.prerequisites)
                    self.assertNotIn(relative_driver, inputs)
                    self.assertTrue(
                        {
                            "Makefile",
                            *WINDOWS_PRODUCTION_SEED_INPUTS,
                        }.issubset(inputs)
                    )
                    recipe = "\n".join(rule.recipe)
                    self.assertIn(
                        "$(PRODUCTION_SEED_DIRECTORY)cupidbuild."
                        "$(PRODUCTION_SEED_SUFFIX) "
                        "assemble-cupidasm-object",
                        recipe,
                    )
                    self.assertIn('--root "$(CURDIR)"', recipe)
                    self.assertNotIn("custom-cupidasm", recipe)
                    self.assertNotIn("custom-cupiddis", recipe)
                    self.assertNotIn("custom-python", recipe)
                    self.assertNotIn("tools/hostbuild.py", recipe)

    def test_guarded_iso_image_retains_both_complete_seed_cohorts(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        module = _load_audit_module()
        fixtures = {
            "test_iso/fixtures",
            "test_iso/fixtures.manifest",
            *{
                "test_iso/fixtures/" + name
                for name in (REPO_ROOT / "test_iso/fixtures.manifest")
                .read_text(encoding="ascii").splitlines()
            },
        }
        def check(host):
            with mock.patch.object(module, "CANONICAL_MAKE_VARIABLES", (f"OS={host}",)):
                rules = module._parse_make_rules(
                    module._run_make_database(REPO_ROOT, make, "test_iso/hello.iso")
                )
            rule = rules["test_iso/hello.iso"]
            self.assertEqual(set(rule.prerequisites), {
                "Makefile", *fixtures, "bootstrap/seeds/release.json",
                *LINUX_BOOTSTRAP_SEED_INPUTS, *WINDOWS_EXECUTION_SEED_INPUTS,
            })
            self.assertEqual(rule.order_only_prerequisites, [])
            recipe = "\n".join(rule.recipe)
            self.assertIn(
                "$(PRODUCTION_SEED_DIRECTORY)cupidbuild."
                "$(PRODUCTION_SEED_SUFFIX) publish-iso-fixture", recipe,
            )
            for context in (
                '--root "$(CURDIR)"',
                "--manifest $(ISO_FIXTURE_MANIFEST)",
                "--fixtures test_iso/fixtures",
                "--output $@",
                "--linux-manifest $(BOOTSTRAP_SEED_MANIFEST)",
                "--windows-manifest $(BOOTSTRAP_WINDOWS_SEED_MANIFEST)",
                "--seed-release $(PRODUCTION_SEED_RELEASE)",
            ):
                self.assertIn(context, recipe)
            self.assertNotIn("$(PYTHON)", recipe)
            self.assertNotIn("tools/hostbuild.py", recipe)

        for host in ("Windows_NT", "Linux"):
            with self.subTest(host=host):
                check(host)

    def test_guarded_iso_image_preserves_pair_under_tool_and_closure_overrides(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        module = _load_audit_module()
        expected = {
            "Makefile", "test_iso/fixtures", "test_iso/fixtures.manifest",
            "bootstrap/seeds/release.json",
            *LINUX_BOOTSTRAP_SEED_INPUTS, *WINDOWS_EXECUTION_SEED_INPUTS,
            *{
                "test_iso/fixtures/" + name
                for name in (REPO_ROOT / "test_iso/fixtures.manifest")
                .read_text(encoding="ascii").splitlines()
            },
        }
        def check(host):
            variables = (
                f"OS={host}", "PYTHON=missing-python", "CUPIDOBJ=missing-object-author",
                "CUPIDOBJ_INPUTS=README.md",
                "CHECKED_SEED_RUN=missing-runner", "CHECKED_SEED_INPUTS=README.md",
                "PRODUCTION_SEED_DIRECTORY=missing-production-directory/",
                "PRODUCTION_SEED_SUFFIX=missing-suffix",
                "PRODUCTION_SEED_INPUTS=README.md",
                "ISO_PUBLICATION_SEED_INPUTS=README.md",
                "ISO_LINUX_SEED_DIRECTORY=missing-linux-directory/",
                "ISO_WINDOWS_SEED_DIRECTORY=missing-windows-directory/",
            )
            with mock.patch.object(module, "CANONICAL_MAKE_VARIABLES", variables):
                rules = module._parse_make_rules(
                    module._run_make_database(REPO_ROOT, make, "test_iso/hello.iso")
                )
            rule = rules["test_iso/hello.iso"]
            self.assertEqual(set(rule.prerequisites), expected)
            self.assertEqual(rule.order_only_prerequisites, [])
            recipe = "\n".join(rule.recipe)
            self.assertIn(
                "$(PRODUCTION_SEED_SUFFIX) publish-iso-fixture", recipe,
            )
            self.assertNotIn("missing-", recipe)
            self.assertNotIn("$(PYTHON)", recipe)
            self.assertNotIn("tools/hostbuild.py", recipe)

        for host in ("Windows_NT", "Linux"):
            with self.subTest(host=host):
                check(host)

    def test_guarded_iso_lane_ignores_standalone_assembler_overrides(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        module = _load_audit_module()
        with tempfile.TemporaryDirectory(
            prefix=".audit-tool-",
            dir=REPO_ROOT,
        ) as td:
            driver = Path(td) / "cupidasm-driver"
            driver.write_bytes(b"driver\n")
            relative_driver = driver.relative_to(REPO_ROOT).as_posix()
            variables = (
                *module.CANONICAL_MAKE_VARIABLES,
                "CUPIDASM=custom-cupidasm",
                f"CUPIDASM_INPUTS={relative_driver}",
            )
            with mock.patch.object(
                module,
                "CANONICAL_MAKE_VARIABLES",
                variables,
            ):
                rules = module._parse_make_rules(
                    module._run_make_database(
                        REPO_ROOT,
                        make,
                        "test_iso/fixtures/big.bin",
                    )
                )

            rule = rules["test_iso/fixtures/big.bin"]
            inputs = set(rule.prerequisites)
            self.assertNotIn(relative_driver, inputs)
            self.assertEqual(
                inputs,
                {
                    "Makefile",
                    "test_iso/big_pattern.asm",
                    *WINDOWS_PRODUCTION_SEED_INPUTS,
                },
            )
            recipe = "\n".join(rule.recipe)
            self.assertIn(
                "$(PRODUCTION_SEED_DIRECTORY)cupidbuild."
                "$(PRODUCTION_SEED_SUFFIX) assemble-iso-pattern",
                recipe,
            )
            self.assertIn('--root "$(CURDIR)"', recipe)
            self.assertNotIn("custom-cupidasm", recipe)
            self.assertNotIn("tools/hostbuild.py", recipe)

    def test_guarded_iso_image_is_classified_as_native_complete_image_publication(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        module = _load_audit_module()
        for host in ("Windows_NT", "Linux"):
            with self.subTest(host=host):
                with mock.patch.object(module, "CANONICAL_MAKE_VARIABLES", (f"OS={host}",)):
                    rules = module._parse_make_rules(
                        module._run_make_database(REPO_ROOT, make, "test_iso/hello.iso")
                    )
                rule = rules["test_iso/hello.iso"]
                tools = module._tools_for_recipe(rule.recipe)
                self.assertEqual(tools, ["cupid_builder", "cupid_object"])
                self.assertEqual(module._operation_for_recipe(
                    rule.recipe, tools, "test_iso/hello.iso",
                    "compile_elf32_relocatable", rule.prerequisites,
                ), "package_iso9660_image")

    def test_guarded_iso_image_contract_rejects_missing_or_redirected_authority(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        module = _load_audit_module()
        rules = module._parse_make_rules(
            module._run_make_database(REPO_ROOT, make, "test_iso/hello.iso")
        )
        rule = rules["test_iso/hello.iso"]
        pair_inputs = [
            *LINUX_BOOTSTRAP_SEED_INPUTS, *WINDOWS_EXECUTION_SEED_INPUTS,
            "bootstrap/seeds/release.json",
        ]
        fixture_inputs = [
            "test_iso/fixtures", "test_iso/fixtures.manifest",
            *[
                "test_iso/fixtures/" + name
                for name in (REPO_ROOT / "test_iso/fixtures.manifest")
                .read_text(encoding="ascii").splitlines()
            ],
        ]
        valid = {"output": "test_iso/hello.iso", "operation": "package_iso9660_image",
                 "tools": ["cupid_builder", "cupid_object"], "recipe": rule.recipe,
                 "inputs": rule.prerequisites, "order_only_inputs": []}

        def validate(transform):
            module._validate_iso_image_delivery([transform], pair_inputs=pair_inputs,
                                                fixture_inputs=fixture_inputs)

        validate(valid)
        for path in valid["inputs"]:
            with self.subTest(missing=path), self.assertRaises(module.AuditError):
                validate({**valid, "inputs": [name for name in valid["inputs"] if name != path]})
            with self.subTest(scheduling_only=path), self.assertRaises(module.AuditError):
                validate({**valid, "inputs": [name for name in valid["inputs"] if name != path],
                          "order_only_inputs": [path]})
        tokens = module._recipe_tokens(rule.recipe)
        for option in ("--root", "--manifest", "--fixtures", "--output",
                       "--linux-manifest", "--windows-manifest", "--seed-release"):
            offset = tokens.index(option)
            changed = [*tokens[:offset + 1], "redirected", *tokens[offset + 2:]]
            for label, arguments in (
                ("redirected", changed),
                ("missing", [*tokens[:offset], *tokens[offset + 2:]]),
                ("duplicate", [*tokens, option, tokens[offset + 1]]),
            ):
                with self.subTest(option=option, mutation=label), self.assertRaises(module.AuditError):
                    validate({**valid, "recipe": [" ".join(arguments)]})
        for mutation in (
            {"recipe": [*rule.recipe, "--unexpected"]},
            {"tools": ["cupid_builder", "cupid_object", "host_python"]},
            {"operation": "package_disk_image"},
            {"inputs": [*valid["inputs"], "README.md"]},
            {"inputs": [*valid["inputs"], valid["inputs"][0]]},
        ):
            with self.subTest(mutation=mutation), self.assertRaises(module.AuditError):
                validate({**valid, **mutation})
        with self.assertRaises(module.AuditError):
            module._validate_iso_image_delivery([], pair_inputs=pair_inputs, fixture_inputs=fixture_inputs)
        with self.assertRaises(module.AuditError):
            module._validate_iso_image_delivery([valid, valid], pair_inputs=pair_inputs,
                                                fixture_inputs=fixture_inputs)

    def test_guarded_iso_image_uses_paired_context_in_root_release_validation(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        module = _load_audit_module()
        for host, seed_inputs in (("Windows_NT", WINDOWS_PRODUCTION_SEED_INPUTS),
                                  ("Linux", LINUX_PRODUCTION_SEED_INPUTS)):
            with self.subTest(host=host):
                with mock.patch.object(module, "CANONICAL_MAKE_VARIABLES", (f"OS={host}",)):
                    rule = module._parse_make_rules(module._run_make_database(
                        REPO_ROOT, make, "test_iso/hello.iso",
                    ))["test_iso/hello.iso"]
                transform = {"output": "test_iso/hello.iso", "recipe": rule.recipe,
                             "tools": ["cupid_builder", "cupid_object"],
                             "inputs": rule.prerequisites, "order_only_inputs": []}
                module._validate_cupidbuild_root_release_context(
                    [transform], seed_inputs=list(seed_inputs),
                )

    def test_guarded_iso_image_binds_evaluated_pair_and_manifest_inventory(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        module = _load_audit_module()
        for host in ("Windows_NT", "Linux"):
            with self.subTest(host=host):
                with mock.patch.object(module, "CANONICAL_MAKE_VARIABLES", (f"OS={host}",)):
                    pair, fixtures = module._validate_iso_image_make_binding(REPO_ROOT, make)
                    rule = module._parse_make_rules(module._run_make_database(
                        REPO_ROOT, make, "test_iso/hello.iso",
                    ))["test_iso/hello.iso"]
                self.assertEqual(pair, sorted([
                    *LINUX_BOOTSTRAP_SEED_INPUTS, *WINDOWS_EXECUTION_SEED_INPUTS,
                    "bootstrap/seeds/release.json",
                ]))
                module._validate_iso_image_delivery([{
                    "output": "test_iso/hello.iso", "operation": "package_iso9660_image",
                    "tools": ["cupid_builder", "cupid_object"], "recipe": rule.recipe,
                    "inputs": rule.prerequisites, "order_only_inputs": [],
                }], pair_inputs=pair, fixture_inputs=fixtures)

    def test_guarded_iso_image_make_binding_rejects_incomplete_or_mixed_context(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        module = _load_audit_module()
        variables = (
            "BOOTSTRAP_SEED_MANIFEST", "BOOTSTRAP_WINDOWS_SEED_MANIFEST",
            "PRODUCTION_SEED_MANIFEST", "PRODUCTION_SEED_SUFFIX", "PRODUCTION_SEED_RELEASE",
            "ISO_LINUX_SEED_DIRECTORY", "ISO_WINDOWS_SEED_DIRECTORY", "ISO_PUBLICATION_SEED_INPUTS",
            "ISO_FIXTURE_MANIFEST", "ISO_FIXTURE_RELATIVE", "TEST_ISO_FIXTURES",
        )
        values = module._read_evaluated_make_variables(REPO_ROOT, make, variables)
        cases = {
            "BOOTSTRAP_SEED_MANIFEST": "redirected/manifest.json",
            "BOOTSTRAP_WINDOWS_SEED_MANIFEST": values["BOOTSTRAP_SEED_MANIFEST"],
            "PRODUCTION_SEED_MANIFEST": "third-cohort/manifest.json",
            "PRODUCTION_SEED_SUFFIX": "unknown",
            "PRODUCTION_SEED_RELEASE": "different-release.json",
            "ISO_LINUX_SEED_DIRECTORY": "redirected/",
            "ISO_WINDOWS_SEED_DIRECTORY": "redirected/",
            "ISO_PUBLICATION_SEED_INPUTS": " ".join(values["ISO_PUBLICATION_SEED_INPUTS"].split()[:-1]),
            "ISO_FIXTURE_MANIFEST": "other.manifest",
            "ISO_FIXTURE_RELATIVE": "unlisted.txt",
            "TEST_ISO_FIXTURES": " ".join(values["TEST_ISO_FIXTURES"].split()[:-1]),
        }
        for variable, value in cases.items():
            with self.subTest(variable=variable):
                with mock.patch.object(module, "_read_evaluated_make_variables",
                                       return_value={**values, variable: value}):
                    with self.assertRaises(module.AuditError):
                        module._validate_iso_image_make_binding(REPO_ROOT, make)
        for variable in ("ISO_PUBLICATION_SEED_INPUTS", "TEST_ISO_FIXTURES"):
            with self.subTest(duplicate=variable):
                repeated = values[variable] + " " + values[variable].split()[0]
                with mock.patch.object(module, "_read_evaluated_make_variables",
                                       return_value={**values, variable: repeated}):
                    with self.assertRaises(module.AuditError):
                        module._validate_iso_image_make_binding(REPO_ROOT, make)

    def test_raw_assembly_keeps_seed_closure_under_tool_overrides(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        module = _load_audit_module()
        with tempfile.TemporaryDirectory(
            prefix=".audit-tool-",
            dir=REPO_ROOT,
        ) as td:
            driver = Path(td) / "cupidasm-driver"
            driver.write_bytes(b"driver\n")
            relative_driver = driver.relative_to(REPO_ROOT).as_posix()
            for host, seed_inputs in (
                ("Windows_NT", WINDOWS_PRODUCTION_SEED_INPUTS),
                ("Linux", LINUX_PRODUCTION_SEED_INPUTS),
            ):
                variables = (
                    f"OS={host}",
                    "CUPIDASM=custom-cupidasm",
                    f"CUPIDASM_INPUTS={relative_driver}",
                    "CUPIDDIS=custom-cupiddis",
                    f"CUPIDDIS_INPUTS={relative_driver}",
                    "PYTHON=custom-python",
                )
                with mock.patch.object(
                    module,
                    "CANONICAL_MAKE_VARIABLES",
                    variables,
                ):
                    for target, source, operation in (
                        (
                            "boot/boot.bin",
                            "boot/boot.asm",
                            "assemble-bootloader",
                        ),
                        (
                            "kernel/smp_trampoline.bin",
                            "kernel/smp/smp_trampoline.S",
                            "assemble-smp-trampoline",
                        ),
                        (
                            "test_iso/fixtures/big.bin",
                            "test_iso/big_pattern.asm",
                            "assemble-iso-pattern",
                        ),
                    ):
                        with self.subTest(host=host, target=target):
                            rules = module._parse_make_rules(
                                module._run_make_database(
                                    REPO_ROOT,
                                    make,
                                    target,
                                )
                            )

                            rule = rules[target]
                            inputs = set(rule.prerequisites)
                            self.assertEqual(
                                inputs,
                                {
                                    "Makefile",
                                    source,
                                    *seed_inputs,
                                },
                            )
                            recipe = "\n".join(rule.recipe)
                            self.assertIn(
                                "$(PRODUCTION_SEED_DIRECTORY)cupidbuild."
                                "$(PRODUCTION_SEED_SUFFIX) " + operation,
                                recipe,
                            )
                            self.assertIn('--root "$(CURDIR)"', recipe)
                            self.assertNotIn("custom-cupidasm", recipe)
                            self.assertNotIn("custom-cupiddis", recipe)
                            self.assertNotIn("custom-python", recipe)
                            self.assertNotIn("tools/hostbuild.py", recipe)

    def test_kernel_symbol_transaction_keeps_seed_closure_under_overrides(
        self,
    ):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        module = _load_audit_module()
        with tempfile.TemporaryDirectory(
            prefix=".audit-ksyms-poison-",
            dir=REPO_ROOT,
        ) as td:
            poison_root = Path(td)
            poison_inputs = {}
            for name in (
                "cupiddis",
                "cupidobj",
                "checked-seed",
                "production-seed",
            ):
                path = poison_root / f"poison-{name}.bin"
                path.write_bytes(b"poison\n")
                poison_inputs[name] = path.relative_to(REPO_ROOT).as_posix()
            for host, seed_inputs in (
                ("Windows_NT", WINDOWS_PRODUCTION_SEED_INPUTS),
                ("Linux", LINUX_PRODUCTION_SEED_INPUTS),
            ):
                variables = (
                    f"OS={host}",
                    "PYTHON=custom-python",
                    "CUPIDDIS=custom-cupiddis",
                    f"CUPIDDIS_INPUTS={poison_inputs['cupiddis']}",
                    "CUPIDOBJ=custom-cupidobj",
                    f"CUPIDOBJ_INPUTS={poison_inputs['cupidobj']}",
                    f"CHECKED_SEED_INPUTS={poison_inputs['checked-seed']}",
                    f"PRODUCTION_SEED_DIRECTORY={poison_root.as_posix()}/",
                    "PRODUCTION_SEED_SUFFIX=poison",
                    f"PRODUCTION_SEED_INPUTS={poison_inputs['production-seed']}",
                )
                with (
                    self.subTest(host=host),
                    mock.patch.object(
                        module,
                        "CANONICAL_MAKE_VARIABLES",
                        variables,
                    ),
                ):
                    rules = module._parse_make_rules(
                        module._run_make_database(
                            REPO_ROOT,
                            make,
                            "kernel/cpu/ksyms_data.cc",
                        )
                    )
                    rule = rules["kernel/cpu/ksyms_data.cc"]
                    self.assertEqual(
                        set(rule.prerequisites),
                        {
                            "Makefile",
                            "kernel/kernel.elf.pass1",
                            *seed_inputs,
                        },
                    )
                    self.assertEqual(
                        rule.recipe,
                        [
                            "$(PRODUCTION_SEED_DIRECTORY)cupidbuild."
                            "$(PRODUCTION_SEED_SUFFIX) generate-ksyms \\",
                            "--seed-manifest $(PRODUCTION_SEED_MANIFEST) "
                            '--root "$(CURDIR)" \\',
                            '--seed-release $(PRODUCTION_SEED_RELEASE) \\',
                            "--source $< --output $@",
                        ],
                    )
                    serialized = "\n".join([*rule.prerequisites, *rule.recipe])
                    for poison in (
                        "custom-python",
                        "custom-cupiddis",
                        "custom-cupidobj",
                        ".poison",
                        *poison_inputs.values(),
                        "tools/hostbuild.py",
                        "tools/bootstrap_toolchain.py",
                    ):
                        self.assertNotIn(poison, serialized)

    def test_kernel_flatten_keeps_seed_closure_under_overrides(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        module = _load_audit_module()
        with tempfile.TemporaryDirectory(
            prefix=".audit-kernel-flatten-poison-",
            dir=REPO_ROOT,
        ) as td:
            poison_root = Path(td)
            poison_inputs = {}
            for name in (
                "cupiddis",
                "cupidobj",
                "checked-seed",
                "production-seed",
            ):
                path = poison_root / f"poison-{name}.bin"
                path.write_bytes(b"poison\n")
                poison_inputs[name] = path.relative_to(REPO_ROOT).as_posix()
            for host, seed_inputs in (
                ("Windows_NT", WINDOWS_PRODUCTION_SEED_INPUTS),
                ("Linux", LINUX_PRODUCTION_SEED_INPUTS),
            ):
                variables = (
                    f"OS={host}",
                    "PYTHON=custom-python",
                    "CUPIDDIS=custom-cupiddis",
                    f"CUPIDDIS_INPUTS={poison_inputs['cupiddis']}",
                    "CUPIDOBJ=custom-cupidobj",
                    f"CUPIDOBJ_INPUTS={poison_inputs['cupidobj']}",
                    f"CHECKED_SEED_INPUTS={poison_inputs['checked-seed']}",
                    f"PRODUCTION_SEED_DIRECTORY={poison_root.as_posix()}/",
                    "PRODUCTION_SEED_SUFFIX=poison",
                    f"PRODUCTION_SEED_INPUTS={poison_inputs['production-seed']}",
                )
                with (
                    self.subTest(host=host),
                    mock.patch.object(
                        module,
                        "CANONICAL_MAKE_VARIABLES",
                        variables,
                    ),
                ):
                    rules = module._parse_make_rules(
                        module._run_make_database(
                            REPO_ROOT,
                            make,
                            "kernel/kernel.bin",
                        )
                    )
                    values = module._read_evaluated_make_variables(
                        REPO_ROOT,
                        make,
                        (
                            "CUPIDDIS_PRODUCTION_INPUT_MANIFEST",
                            "CUPIDDIS_PRODUCTION_INPUTS",
                        ),
                    )
                    rule = rules["kernel/kernel.bin"]
                    self.assertEqual(
                        set(rule.prerequisites),
                        {
                            "Makefile",
                            values["CUPIDDIS_PRODUCTION_INPUT_MANIFEST"],
                            *values["CUPIDDIS_PRODUCTION_INPUTS"].split(),
                            *seed_inputs,
                        },
                    )
                    self.assertEqual(
                        rule.recipe,
                        [
                            "$(PRODUCTION_SEED_DIRECTORY)cupidbuild."
                            "$(PRODUCTION_SEED_SUFFIX) flatten-kernel \\",
                            "--seed-manifest $(PRODUCTION_SEED_MANIFEST) "
                            '--root "$(CURDIR)" \\',
                            '--seed-release $(PRODUCTION_SEED_RELEASE) \\',
                            "--input-manifest $(CUPIDDIS_PRODUCTION_INPUT_MANIFEST) \\",
                            "--output $(KERNEL)",
                        ],
                    )
                    serialized = "\n".join([*rule.prerequisites, *rule.recipe])
                    for poison in (
                        "custom-python",
                        "custom-cupiddis",
                        "custom-cupidobj",
                        ".poison",
                        *poison_inputs.values(),
                        "tools/hostbuild.py",
                        "tools/bootstrap_toolchain.py",
                        "$(CUPIDDIS)",
                        "$(CUPIDOBJ)",
                    ):
                        self.assertNotIn(poison, serialized)

    def test_iso_pattern_audit_contract_rejects_mutations(self):
        module = _load_audit_module()
        seed_inputs = list(WINDOWS_PRODUCTION_SEED_INPUTS)
        transform = {
            "output": "test_iso/fixtures/big.bin",
            "inputs": ["test_iso/big_pattern.asm", "Makefile", *seed_inputs],
            "tools": ["cupid_assembler", "cupid_disassembler", "cupid_builder"],
            "operation": "assemble_flat_binary",
            "recipe": [
                "$(PRODUCTION_SEED_DIRECTORY)cupidbuild."
                "$(PRODUCTION_SEED_SUFFIX) assemble-iso-pattern \\",
                '--seed-manifest $(PRODUCTION_SEED_MANIFEST) --root "$(CURDIR)" \\',
                '--seed-release $(PRODUCTION_SEED_RELEASE) \\',
                "--source $< --output $@",
            ],
        }
        module._validate_iso_pattern_delivery([transform], seed_inputs=seed_inputs)
        mutations = {
            "output": {"output": "test_iso/fixtures/other.bin"},
            "operation": {"operation": "assemble"},
            "unchecked owner": {"tools": ["cupid_assembler", "host_python"]},
            "unchecked recipe": {"recipe": ["$(PYTHON) tools/hostbuild.py gen-big"]},
            "relative root": {"recipe": [
                line.replace('"$(CURDIR)"', ".") for line in transform["recipe"]
            ]},
            "missing output option": {"recipe": [*transform["recipe"][:-1], "--source $<"]},
            "host input": {"inputs": [*transform["inputs"], "tools/hostbuild.py"]},
            "duplicate input": {"inputs": [*transform["inputs"], "Makefile"]},
            "Makefile before assembly": {"inputs": [
                "Makefile", "test_iso/big_pattern.asm", *seed_inputs
            ]},
            "seed before assembly": {"inputs": [
                seed_inputs[1], "test_iso/big_pattern.asm", "Makefile",
                *[path for path in seed_inputs if path != seed_inputs[1]],
            ]},
            "profile scheduling edge": {"order_only_inputs": [
                "build/bootstrap/doom-cupidc-inputs.json"
            ]},
        }
        for path in transform["inputs"]:
            mutations[f"missing {path}"] = {
                "inputs": [member for member in transform["inputs"] if member != path]
            }
        for name, replacement in mutations.items():
            with self.subTest(name=name), self.assertRaisesRegex(module.AuditError, "ISO pattern"):
                module._validate_iso_pattern_delivery(
                    [{**transform, **replacement}], seed_inputs=seed_inputs
                )
        for deliveries in ([], [transform, transform]):
            with self.subTest(deliveries=len(deliveries)), self.assertRaisesRegex(
                module.AuditError, "ISO pattern"
            ):
                module._validate_iso_pattern_delivery(deliveries, seed_inputs=seed_inputs)

    def test_kernel_flatten_audit_contract_rejects_mutations(self):
        module = _load_audit_module()
        manifest = "bootstrap/cupiddis-production-inputs.txt"
        code_inputs = [
            "kernel/core/kernel.o",
            "kernel/cpu/ksyms_data.o",
            "kernel/kernel.elf.pass1",
            "kernel/kernel.elf",
        ]
        seed_inputs = list(WINDOWS_PRODUCTION_SEED_INPUTS)
        transform = {
            "output": "kernel/kernel.bin",
            "inputs": [
                *code_inputs,
                manifest,
                "Makefile",
                *seed_inputs,
            ],
            "tools": ["cupid_builder", "cupid_disassembler", "cupid_object"],
            "operation": "extract_raw_binary",
            "recipe": [
                "$(PRODUCTION_SEED_DIRECTORY)cupidbuild."
                "$(PRODUCTION_SEED_SUFFIX) flatten-kernel \\",
                '--seed-manifest $(PRODUCTION_SEED_MANIFEST) --root "$(CURDIR)" \\',
                '--seed-release $(PRODUCTION_SEED_RELEASE) \\',
                "--input-manifest $(CUPIDDIS_PRODUCTION_INPUT_MANIFEST) \\",
                "--output $(KERNEL)",
            ],
        }
        module._validate_kernel_flatten_transform(
            transform,
            code_inputs=code_inputs,
            input_manifest=manifest,
            seed_inputs=seed_inputs,
        )

        mutations = {
            "operation": {"operation": "transform_object"},
            "tool owner": {
                "tools": [
                    "cupid_disassembler",
                    "cupid_object",
                    "host_python",
                ]
            },
            "recipe": {
                "recipe": ["$(PYTHON) tools/hostbuild.py validate-code"]
            },
            "host input": {
                "inputs": [*transform["inputs"], "tools/hostbuild.py"]
            },
            "missing seed": {
                "inputs": [
                    path
                    for path in transform["inputs"]
                    if path != seed_inputs[-1]
                ]
            },
            "duplicate input": {
                "inputs": [*transform["inputs"], code_inputs[0]]
            },
        }
        for name, replacement in mutations.items():
            with (
                self.subTest(name=name),
                self.assertRaisesRegex(
                    module.AuditError,
                    "kernel flatten",
                ),
            ):
                module._validate_kernel_flatten_transform(
                    {**transform, **replacement},
                    code_inputs=code_inputs,
                    input_manifest=manifest,
                    seed_inputs=seed_inputs,
                )
